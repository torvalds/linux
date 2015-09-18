/* Include paths to be used in interface defining headers */
#ifndef POWERPC_PERF_REQ_GEN_H_
#define POWERPC_PERF_REQ_GEN_H_

#define CAT2_STR_(t, s) __stringify(t/s)
#define CAT2_STR(t, s) CAT2_STR_(t, s)
#define I(...) __VA_ARGS__

#endif

#define REQ_GEN_PREFIX req-gen
#define REQUEST_BEGIN CAT2_STR(REQ_GEN_PREFIX, _request-begin.h)
#define REQUEST_END   CAT2_STR(REQ_GEN_PREFIX, _request-end.h)
