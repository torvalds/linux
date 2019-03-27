/*
 * Copyright (c) 2000-2007 Niels Provos <provos@citi.umich.edu>
 * Copyright (c) 2007-2012 Niels Provos and Nick Mathewson
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#ifndef REGRESS_H_INCLUDED_
#define REGRESS_H_INCLUDED_

#ifdef __cplusplus
extern "C" {
#endif

#include "tinytest.h"
#include "tinytest_macros.h"

extern struct testcase_t main_testcases[];
extern struct testcase_t evtag_testcases[];
extern struct testcase_t evbuffer_testcases[];
extern struct testcase_t finalize_testcases[];
extern struct testcase_t bufferevent_testcases[];
extern struct testcase_t bufferevent_iocp_testcases[];
extern struct testcase_t util_testcases[];
extern struct testcase_t signal_testcases[];
extern struct testcase_t http_testcases[];
extern struct testcase_t dns_testcases[];
extern struct testcase_t rpc_testcases[];
extern struct testcase_t edgetriggered_testcases[];
extern struct testcase_t minheap_testcases[];
extern struct testcase_t iocp_testcases[];
extern struct testcase_t ssl_testcases[];
extern struct testcase_t listener_testcases[];
extern struct testcase_t listener_iocp_testcases[];
extern struct testcase_t thread_testcases[];

extern struct evutil_weakrand_state test_weakrand_state;

#define test_weakrand() (evutil_weakrand_(&test_weakrand_state))

void regress_threads(void *);
void test_bufferevent_zlib(void *);

/* Helpers to wrap old testcases */
extern evutil_socket_t pair[2];
extern int test_ok;
extern int called;
extern struct event_base *global_base;
extern int in_legacy_test_wrapper;

int regress_make_tmpfile(const void *data, size_t datalen, char **filename_out);

struct basic_test_data {
	struct event_base *base;
	evutil_socket_t pair[2];

	void (*legacy_test_fn)(void);

	void *setup_data;
};
extern const struct testcase_setup_t basic_setup;


extern const struct testcase_setup_t legacy_setup;
void run_legacy_test_fn(void *ptr);

extern int libevent_tests_running_in_debug_mode;

/* A couple of flags that basic/legacy_setup can support. */
#define TT_NEED_SOCKETPAIR	TT_FIRST_USER_FLAG
#define TT_NEED_BASE		(TT_FIRST_USER_FLAG<<1)
#define TT_NEED_DNS		(TT_FIRST_USER_FLAG<<2)
#define TT_LEGACY		(TT_FIRST_USER_FLAG<<3)
#define TT_NEED_THREADS		(TT_FIRST_USER_FLAG<<4)
#define TT_NO_LOGS		(TT_FIRST_USER_FLAG<<5)
#define TT_ENABLE_IOCP_FLAG	(TT_FIRST_USER_FLAG<<6)
#define TT_ENABLE_IOCP		(TT_ENABLE_IOCP_FLAG|TT_NEED_THREADS)

/* All the flags that a legacy test needs. */
#define TT_ISOLATED TT_FORK|TT_NEED_SOCKETPAIR|TT_NEED_BASE


#define BASIC(name,flags)						\
	{ #name, test_## name, flags, &basic_setup, NULL }

#define LEGACY(name,flags)						\
	{ #name, run_legacy_test_fn, flags|TT_LEGACY, &legacy_setup,	\
	  test_## name }

struct evutil_addrinfo;
struct evutil_addrinfo *ai_find_by_family(struct evutil_addrinfo *ai, int f);
struct evutil_addrinfo *ai_find_by_protocol(struct evutil_addrinfo *ai, int p);
int test_ai_eq_(const struct evutil_addrinfo *ai, const char *sockaddr_port,
    int socktype, int protocol, int line);

#define test_ai_eq(ai, str, s, p) do {					\
		if (test_ai_eq_((ai), (str), (s), (p), __LINE__)<0)	\
			goto end;					\
	} while (0)

#define test_timeval_diff_leq(tv1, tv2, diff, tolerance)		\
	tt_int_op(labs(timeval_msec_diff((tv1), (tv2)) - diff), <=, tolerance)

#define test_timeval_diff_eq(tv1, tv2, diff)				\
	test_timeval_diff_leq((tv1), (tv2), (diff), 50)

long timeval_msec_diff(const struct timeval *start, const struct timeval *end);

#ifndef _WIN32
pid_t regress_fork(void);
#endif

#ifdef EVENT__HAVE_OPENSSL
#include <openssl/ssl.h>
EVP_PKEY *ssl_getkey(void);
X509 *ssl_getcert(void);
SSL_CTX *get_ssl_ctx(void);
void init_ssl(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* REGRESS_H_INCLUDED_ */
