/*
  Copyright (c) 2014-2015 DataStax

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

  http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
*/

#ifndef __CASS_HANDLER_HPP_INCLUDED__
#define __CASS_HANDLER_HPP_INCLUDED__

#include "buffer.hpp"
#include "constants.hpp"
#include "cassandra.h"
#include "utils.hpp"
#include "list.hpp"
#include "request.hpp"
#include "scoped_ptr.hpp"

#include <string>
#include <uv.h>

namespace cass {

class Config;
class Connection;
class ResponseMessage;

typedef std::vector<uv_buf_t> UvBufVec;

struct RequestTimer {
  typedef void (*Callback)(RequestTimer*);

  RequestTimer()
    : handle_(NULL)
    , data_(NULL) { }

  ~RequestTimer() {
    if (handle_ != NULL) {
      uv_close(copy_cast<uv_timer_t*, uv_handle_t*>(handle_), on_close);
    }
  }

  void* data() const { return data_; }

  void start(uv_loop_t* loop, uint64_t timeout, void* data,
                   Callback cb) {
    if (handle_ == NULL) {
      handle_ = new  uv_timer_t;
      handle_->data = this;
      uv_timer_init(loop, handle_);
    }
    data_ = data;
    cb_ = cb;
    uv_timer_start(handle_, on_timeout, timeout, 0);
  }

  void stop() {
    if (handle_ != NULL) {
      uv_timer_stop(handle_);
    }
  }

#if UV_VERSION_MAJOR == 0
  static void on_timeout(uv_timer_t* handle, int status) {
#else
  static void on_timeout(uv_timer_t* handle) {
#endif
    RequestTimer* timer = static_cast<RequestTimer*>(handle->data);
    timer->cb_(timer);
  }

  static void on_close(uv_handle_t* handle) {
    delete copy_cast<uv_handle_t*, uv_timer_t*>(handle);
  }

private:
  uv_timer_t* handle_;
  void* data_;
  Callback cb_;
};

class Handler : public RefCounted<Handler>, public List<Handler>::Node {
public:
  enum State {
    REQUEST_STATE_NEW,
    REQUEST_STATE_WRITING,
    REQUEST_STATE_READING,
    REQUEST_STATE_TIMEOUT,
    REQUEST_STATE_TIMEOUT_WRITE_OUTSTANDING,
    REQUEST_STATE_READ_BEFORE_WRITE,
    REQUEST_STATE_RETRY_WRITE_OUTSTANDING,
    REQUEST_STATE_DONE
  };

  Handler()
    : connection_(NULL)
    , stream_(-1)
    , state_(REQUEST_STATE_NEW)
    , cl_(CASS_CONSISTENCY_UNKNOWN)
    , default_timestamp_(CASS_INT64_MIN) { }

  virtual ~Handler() {}

  virtual const Request* request() const = 0;

  int32_t encode(int version, int flags, BufferVec* bufs);

  virtual void start_request() {}

  virtual void on_set(ResponseMessage* response) = 0;
  virtual void on_error(CassError code, const std::string& message) = 0;
  virtual void on_timeout() = 0;

  virtual void retry() { }

  Connection* connection() const { return connection_; }

  void set_connection(Connection* connection) {
    connection_ = connection;
  }

  int8_t stream() const { return stream_; }

  void set_stream(int8_t stream) {
    stream_ = stream;
  }

  State state() const { return state_; }

  void set_state(State next_state);

  void start_timer(uv_loop_t* loop, uint64_t timeout, void* data,
                   RequestTimer::Callback cb) {
    timer_.start(loop, timeout, data, cb);
  }

  void stop_timer() {
    timer_.stop();
  }

  CassConsistency consistency() const {
    return cl_ != CASS_CONSISTENCY_UNKNOWN ? cl_ : request()->consistency();
  }

  void set_consistency(CassConsistency cl) { cl_ = cl; }

  int64_t default_timestamp() const {
    return default_timestamp_;
  }

  void set_default_timestamp(int64_t timestamp) {
    default_timestamp_ = timestamp;
  }

  Request::EncodingCache* encoding_cache() { return &encoding_cache_; }

protected:
  Connection* connection_;

private:
  RequestTimer timer_;
  int16_t stream_;
  State state_;
  CassConsistency cl_;
  int64_t default_timestamp_;
  Request::EncodingCache encoding_cache_;

private:
  DISALLOW_COPY_AND_ASSIGN(Handler);
};

} // namespace cass

#endif
