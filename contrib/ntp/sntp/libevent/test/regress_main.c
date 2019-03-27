/*
 * Copyright (c) 2003-2007 Niels Provos <provos@citi.umich.edu>
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
#include "util-internal.h"

#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#endif

#if defined(__APPLE__) && defined(__ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__)
#if (__ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ >= 1060 && \
    __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1070)
#define FORK_BREAKS_GCOV
#include <vproc.h>
#endif
#endif

#include "event2/event-config.h"

#ifdef EVENT____func__
#define __func__ EVENT____func__
#endif

#if 0
#include <sys/types.h>
#include <sys/stat.h>
#ifdef EVENT__HAVE_SYS_TIME_H
#include <sys/time.h>
#endif
#include <sys/queue.h>
#include <signal.h>
#include <errno.h>
#endif

#include <sys/types.h>
#ifdef EVENT__HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif

#ifndef _WIN32
#include <sys/socket.h>
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>
#include <netdb.h>
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "event2/util.h"
#include "event2/event.h"
#include "event2/event_compat.h"
#include "event2/dns.h"
#include "event2/dns_compat.h"
#include "event2/thread.h"

#include "event2/event-config.h"
#include "regress.h"
#include "tinytest.h"
#include "tinytest_macros.h"
#include "../iocp-internal.h"
#include "../event-internal.h"

struct evutil_weakrand_state test_weakrand_state;

long
timeval_msec_diff(const struct timeval *start, const struct timeval *end)
{
	long ms = end->tv_sec - start->tv_sec;
	ms *= 1000;
	ms += ((end->tv_usec - start->tv_usec)+500) / 1000;
	return ms;
}

/* ============================================================ */
/* Code to wrap up old legacy test cases that used setup() and cleanup().
 *
 * Not all of the tests designated "legacy" are ones that used setup() and
 * cleanup(), of course.  A test is legacy it it uses setup()/cleanup(), OR
 * if it wants to find its event base/socketpair in global variables (ugh),
 * OR if it wants to communicate success/failure through test_ok.
 */

/* This is set to true if we're inside a legacy test wrapper.  It lets the
   setup() and cleanup() functions in regress.c know they're not needed.
 */
int in_legacy_test_wrapper = 0;

static void dnslogcb(int w, const char *m)
{
	TT_BLATHER(("%s", m));
}

/* creates a temporary file with the data in it.  If *filename_out gets set,
 * the caller should try to unlink it. */
int
regress_make_tmpfile(const void *data, size_t datalen, char **filename_out)
{
#ifndef _WIN32
	char tmpfilename[32];
	int fd;
	*filename_out = NULL;
	strcpy(tmpfilename, "/tmp/eventtmp.XXXXXX");
#ifdef EVENT__HAVE_UMASK
	umask(0077);
#endif
	fd = mkstemp(tmpfilename);
	if (fd == -1)
		return (-1);
	if (write(fd, data, datalen) != (int)datalen) {
		close(fd);
		return (-1);
	}
	lseek(fd, 0, SEEK_SET);
	/* remove it from the file system */
	unlink(tmpfilename);
	return (fd);
#else
	/* XXXX actually delete the file later */
	char tmpfilepath[MAX_PATH];
	char tmpfilename[MAX_PATH];
	DWORD r, written;
	int tries = 16;
	HANDLE h;
	r = GetTempPathA(MAX_PATH, tmpfilepath);
	if (r > MAX_PATH || r == 0)
		return (-1);
	for (; tries > 0; --tries) {
		r = GetTempFileNameA(tmpfilepath, "LIBEVENT", 0, tmpfilename);
		if (r == 0)
			return (-1);
		h = CreateFileA(tmpfilename, GENERIC_READ|GENERIC_WRITE,
		    0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
		if (h != INVALID_HANDLE_VALUE)
			break;
	}
	if (tries == 0)
		return (-1);
	written = 0;
	*filename_out = strdup(tmpfilename);
	WriteFile(h, data, (DWORD)datalen, &written, NULL);
	/* Closing the fd returned by this function will indeed close h. */
	return _open_osfhandle((intptr_t)h,_O_RDONLY);
#endif
}

#ifndef _WIN32
pid_t
regress_fork(void)
{
	pid_t pid = fork();
#ifdef FORK_BREAKS_GCOV
	vproc_transaction_begin(0);
#endif
	return pid;
}
#endif

static void
ignore_log_cb(int s, const char *msg)
{
}

static void *
basic_test_setup(const struct testcase_t *testcase)
{
	struct event_base *base = NULL;
	evutil_socket_t spair[2] = { -1, -1 };
	struct basic_test_data *data = NULL;

#ifndef _WIN32
	if (testcase->flags & TT_ENABLE_IOCP_FLAG)
		return (void*)TT_SKIP;
#endif

	if (testcase->flags & TT_NEED_THREADS) {
		if (!(testcase->flags & TT_FORK))
			return NULL;
#if defined(EVTHREAD_USE_PTHREADS_IMPLEMENTED)
		if (evthread_use_pthreads())
			exit(1);
#elif defined(EVTHREAD_USE_WINDOWS_THREADS_IMPLEMENTED)
		if (evthread_use_windows_threads())
			exit(1);
#else
		return (void*)TT_SKIP;
#endif
	}

	if (testcase->flags & TT_NEED_SOCKETPAIR) {
		if (evutil_socketpair(AF_UNIX, SOCK_STREAM, 0, spair) == -1) {
			fprintf(stderr, "%s: socketpair\n", __func__);
			exit(1);
		}

		if (evutil_make_socket_nonblocking(spair[0]) == -1) {
			fprintf(stderr, "fcntl(O_NONBLOCK)");
			exit(1);
		}

		if (evutil_make_socket_nonblocking(spair[1]) == -1) {
			fprintf(stderr, "fcntl(O_NONBLOCK)");
			exit(1);
		}
	}
	if (testcase->flags & TT_NEED_BASE) {
		if (testcase->flags & TT_LEGACY)
			base = event_init();
		else
			base = event_base_new();
		if (!base)
			exit(1);
	}
	if (testcase->flags & TT_ENABLE_IOCP_FLAG) {
		if (event_base_start_iocp_(base, 0)<0) {
			event_base_free(base);
			return (void*)TT_SKIP;
		}
	}

	if (testcase->flags & TT_NEED_DNS) {
		evdns_set_log_fn(dnslogcb);
		if (evdns_init())
			return NULL; /* fast failure */ /*XXX asserts. */
	}

	if (testcase->flags & TT_NO_LOGS)
		event_set_log_callback(ignore_log_cb);

	data = calloc(1, sizeof(*data));
	if (!data)
		exit(1);
	data->base = base;
	data->pair[0] = spair[0];
	data->pair[1] = spair[1];
	data->setup_data = testcase->setup_data;
	return data;
}

static int
basic_test_cleanup(const struct testcase_t *testcase, void *ptr)
{
	struct basic_test_data *data = ptr;

	if (testcase->flags & TT_NO_LOGS)
		event_set_log_callback(NULL);

	if (testcase->flags & TT_NEED_SOCKETPAIR) {
		if (data->pair[0] != -1)
			evutil_closesocket(data->pair[0]);
		if (data->pair[1] != -1)
			evutil_closesocket(data->pair[1]);
	}

	if (testcase->flags & TT_NEED_DNS) {
		evdns_shutdown(0);
	}

	if (testcase->flags & TT_NEED_BASE) {
		if (data->base) {
			event_base_assert_ok_(data->base);
			event_base_free(data->base);
		}
	}

	if (testcase->flags & TT_FORK)
		libevent_global_shutdown();

	free(data);

	return 1;
}

const struct testcase_setup_t basic_setup = {
	basic_test_setup, basic_test_cleanup
};

/* The "data" for a legacy test is just a pointer to the void fn(void)
   function implementing the test case.  We need to set up some globals,
   though, since that's where legacy tests expect to find a socketpair
   (sometimes) and a global event_base (sometimes).
 */
static void *
legacy_test_setup(const struct testcase_t *testcase)
{
	struct basic_test_data *data = basic_test_setup(testcase);
	if (data == (void*)TT_SKIP || data == NULL)
		return data;
	global_base = data->base;
	pair[0] = data->pair[0];
	pair[1] = data->pair[1];
	data->legacy_test_fn = testcase->setup_data;
	return data;
}

/* This function is the implementation of every legacy test case.  It
   sets test_ok to 0, invokes the test function, and tells tinytest that
   the test failed if the test didn't set test_ok to 1.
 */
void
run_legacy_test_fn(void *ptr)
{
	struct basic_test_data *data = ptr;
	test_ok = called = 0;

	in_legacy_test_wrapper = 1;
	data->legacy_test_fn(); /* This part actually calls the test */
	in_legacy_test_wrapper = 0;

	if (!test_ok)
		tt_abort_msg("Legacy unit test failed");

end:
	test_ok = 0;
}

/* This function doesn't have to clean up ptr (which is just a pointer
   to the test function), but it may need to close the socketpair or
   free the event_base.
 */
static int
legacy_test_cleanup(const struct testcase_t *testcase, void *ptr)
{
	int r = basic_test_cleanup(testcase, ptr);
	pair[0] = pair[1] = -1;
	global_base = NULL;
	return r;
}

const struct testcase_setup_t legacy_setup = {
	legacy_test_setup, legacy_test_cleanup
};

/* ============================================================ */

#if (!defined(EVENT__HAVE_PTHREADS) && !defined(_WIN32)) || defined(EVENT__DISABLE_THREAD_SUPPORT)
struct testcase_t thread_testcases[] = {
	{ "basic", NULL, TT_SKIP, NULL, NULL },
	END_OF_TESTCASES
};
#endif

struct testgroup_t testgroups[] = {
	{ "main/", main_testcases },
	{ "heap/", minheap_testcases },
	{ "et/", edgetriggered_testcases },
	{ "finalize/", finalize_testcases },
	{ "evbuffer/", evbuffer_testcases },
	{ "signal/", signal_testcases },
	{ "util/", util_testcases },
	{ "bufferevent/", bufferevent_testcases },
	{ "http/", http_testcases },
	{ "dns/", dns_testcases },
	{ "evtag/", evtag_testcases },
	{ "rpc/", rpc_testcases },
	{ "thread/", thread_testcases },
	{ "listener/", listener_testcases },
#ifdef _WIN32
	{ "iocp/", iocp_testcases },
	{ "iocp/bufferevent/", bufferevent_iocp_testcases },
	{ "iocp/listener/", listener_iocp_testcases },
#endif
#ifdef EVENT__HAVE_OPENSSL
	{ "ssl/", ssl_testcases },
#endif
	END_OF_GROUPS
};

const char *alltests[] = { "+..", NULL };
const char *livenettests[] = {
	"+util/getaddrinfo_live",
	"+dns/gethostby..",
	"+dns/resolve_reverse",
	NULL
};
const char *finetimetests[] = {
	"+util/monotonic_res_precise",
	"+util/monotonic_res_fallback",
	"+thread/deferred_cb_skew",
	"+http/connection_retry",
	NULL
};
struct testlist_alias_t testaliases[] = {
	{ "all", alltests },
	{ "live_net", livenettests },
	{ "fine_timing", finetimetests },
	END_OF_ALIASES
};

int libevent_tests_running_in_debug_mode = 0;

int
main(int argc, const char **argv)
{
#ifdef _WIN32
	WORD wVersionRequested;
	WSADATA wsaData;

	wVersionRequested = MAKEWORD(2, 2);

	(void) WSAStartup(wVersionRequested, &wsaData);
#endif

#ifndef _WIN32
	if (signal(SIGPIPE, SIG_IGN) == SIG_ERR)
		return 1;
#endif

#ifdef _WIN32
	tinytest_skip(testgroups, "http/connection_retry");
#endif

#ifndef EVENT__DISABLE_THREAD_SUPPORT
	if (!getenv("EVENT_NO_DEBUG_LOCKS"))
		evthread_enable_lock_debugging();
#endif

	if (getenv("EVENT_DEBUG_MODE")) {
		event_enable_debug_mode();
		libevent_tests_running_in_debug_mode = 1;
	}
	if (getenv("EVENT_DEBUG_LOGGING_ALL")) {
		event_enable_debug_logging(EVENT_DBG_ALL);
	}

	tinytest_set_aliases(testaliases);

	evutil_weakrand_seed_(&test_weakrand_state, 0);

	if (tinytest_main(argc,argv,testgroups))
		return 1;

	libevent_global_shutdown();

	return 0;
}

