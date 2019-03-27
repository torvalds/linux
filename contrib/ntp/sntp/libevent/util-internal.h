/*
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
#ifndef UTIL_INTERNAL_H_INCLUDED_
#define UTIL_INTERNAL_H_INCLUDED_

#include "event2/event-config.h"
#include "evconfig-private.h"

#include <errno.h>

/* For EVUTIL_ASSERT */
#include "log-internal.h"
#include <stdio.h>
#include <stdlib.h>
#ifdef EVENT__HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif
#ifdef EVENT__HAVE_SYS_EVENTFD_H
#include <sys/eventfd.h>
#endif
#include "event2/util.h"

#include "time-internal.h"
#include "ipv6-internal.h"

#ifdef __cplusplus
extern "C" {
#endif

/* If we need magic to say "inline", get it for free internally. */
#ifdef EVENT__inline
#define inline EVENT__inline
#endif
#ifdef EVENT____func__
#define __func__ EVENT____func__
#endif

/* A good no-op to use in macro definitions. */
#define EVUTIL_NIL_STMT_ ((void)0)
/* A no-op that tricks the compiler into thinking a condition is used while
 * definitely not making any code for it.  Used to compile out asserts while
 * avoiding "unused variable" warnings.  The "!" forces the compiler to
 * do the sizeof() on an int, in case "condition" is a bitfield value.
 */
#define EVUTIL_NIL_CONDITION_(condition) do { \
	(void)sizeof(!(condition));  \
} while(0)

/* Internal use only: macros to match patterns of error codes in a
   cross-platform way.  We need these macros because of two historical
   reasons: first, nonblocking IO functions are generally written to give an
   error on the "blocked now, try later" case, so sometimes an error from a
   read, write, connect, or accept means "no error; just wait for more
   data," and we need to look at the error code.  Second, Windows defines
   a different set of error codes for sockets. */

#ifndef _WIN32

#if EAGAIN == EWOULDBLOCK
#define EVUTIL_ERR_IS_EAGAIN(e) \
	((e) == EAGAIN)
#else
#define EVUTIL_ERR_IS_EAGAIN(e) \
	((e) == EAGAIN || (e) == EWOULDBLOCK)
#endif

/* True iff e is an error that means a read/write operation can be retried. */
#define EVUTIL_ERR_RW_RETRIABLE(e)				\
	((e) == EINTR || EVUTIL_ERR_IS_EAGAIN(e))
/* True iff e is an error that means an connect can be retried. */
#define EVUTIL_ERR_CONNECT_RETRIABLE(e)			\
	((e) == EINTR || (e) == EINPROGRESS)
/* True iff e is an error that means a accept can be retried. */
#define EVUTIL_ERR_ACCEPT_RETRIABLE(e)			\
	((e) == EINTR || EVUTIL_ERR_IS_EAGAIN(e) || (e) == ECONNABORTED)

/* True iff e is an error that means the connection was refused */
#define EVUTIL_ERR_CONNECT_REFUSED(e)					\
	((e) == ECONNREFUSED)

#else
/* Win32 */

#define EVUTIL_ERR_IS_EAGAIN(e) \
	((e) == WSAEWOULDBLOCK || (e) == EAGAIN)

#define EVUTIL_ERR_RW_RETRIABLE(e)					\
	((e) == WSAEWOULDBLOCK ||					\
	    (e) == WSAEINTR)

#define EVUTIL_ERR_CONNECT_RETRIABLE(e)					\
	((e) == WSAEWOULDBLOCK ||					\
	    (e) == WSAEINTR ||						\
	    (e) == WSAEINPROGRESS ||					\
	    (e) == WSAEINVAL)

#define EVUTIL_ERR_ACCEPT_RETRIABLE(e)			\
	EVUTIL_ERR_RW_RETRIABLE(e)

#define EVUTIL_ERR_CONNECT_REFUSED(e)					\
	((e) == WSAECONNREFUSED)

#endif

/* Arguments for shutdown() */
#ifdef SHUT_RD
#define EVUTIL_SHUT_RD SHUT_RD
#else
#define EVUTIL_SHUT_RD 0
#endif
#ifdef SHUT_WR
#define EVUTIL_SHUT_WR SHUT_WR
#else
#define EVUTIL_SHUT_WR 1
#endif
#ifdef SHUT_BOTH
#define EVUTIL_SHUT_BOTH SHUT_BOTH
#else
#define EVUTIL_SHUT_BOTH 2
#endif

/* Helper: Verify that all the elements in 'dlist' are internally consistent.
 * Checks for circular lists and bad prev/next pointers.
 *
 * Example usage:
 *    EVUTIL_ASSERT_LIST_OK(eventlist, event, ev_next);
 */
#define EVUTIL_ASSERT_LIST_OK(dlist, type, field) do {			\
		struct type *elm1, *elm2, **nextp;			\
		if (LIST_EMPTY((dlist)))				\
			break;						\
									\
		/* Check list for circularity using Floyd's */		\
		/* 'Tortoise and Hare' algorithm */			\
		elm1 = LIST_FIRST((dlist));				\
		elm2 = LIST_NEXT(elm1, field);				\
		while (elm1 && elm2) {					\
			EVUTIL_ASSERT(elm1 != elm2);			\
			elm1 = LIST_NEXT(elm1, field);			\
			elm2 = LIST_NEXT(elm2, field);			\
			if (!elm2)					\
				break;					\
			EVUTIL_ASSERT(elm1 != elm2);			\
			elm2 = LIST_NEXT(elm2, field);			\
		}							\
									\
		/* Now check next and prev pointers for consistency. */ \
		nextp = &LIST_FIRST((dlist));				\
		elm1 = LIST_FIRST((dlist));				\
		while (elm1) {						\
			EVUTIL_ASSERT(*nextp == elm1);			\
			EVUTIL_ASSERT(nextp == elm1->field.le_prev);	\
			nextp = &LIST_NEXT(elm1, field);		\
			elm1 = *nextp;					\
		}							\
	} while (0)

/* Helper: Verify that all the elements in a TAILQ are internally consistent.
 * Checks for circular lists and bad prev/next pointers.
 *
 * Example usage:
 *    EVUTIL_ASSERT_TAILQ_OK(activelist, event, ev_active_next);
 */
#define EVUTIL_ASSERT_TAILQ_OK(tailq, type, field) do {			\
		struct type *elm1, *elm2, **nextp;			\
		if (TAILQ_EMPTY((tailq)))				\
			break;						\
									\
		/* Check list for circularity using Floyd's */		\
		/* 'Tortoise and Hare' algorithm */			\
		elm1 = TAILQ_FIRST((tailq));				\
		elm2 = TAILQ_NEXT(elm1, field);				\
		while (elm1 && elm2) {					\
			EVUTIL_ASSERT(elm1 != elm2);			\
			elm1 = TAILQ_NEXT(elm1, field);			\
			elm2 = TAILQ_NEXT(elm2, field);			\
			if (!elm2)					\
				break;					\
			EVUTIL_ASSERT(elm1 != elm2);			\
			elm2 = TAILQ_NEXT(elm2, field);			\
		}							\
									\
		/* Now check next and prev pointers for consistency. */ \
		nextp = &TAILQ_FIRST((tailq));				\
		elm1 = TAILQ_FIRST((tailq));				\
		while (elm1) {						\
			EVUTIL_ASSERT(*nextp == elm1);			\
			EVUTIL_ASSERT(nextp == elm1->field.tqe_prev);	\
			nextp = &TAILQ_NEXT(elm1, field);		\
			elm1 = *nextp;					\
		}							\
		EVUTIL_ASSERT(nextp == (tailq)->tqh_last);		\
	} while (0)

/* Locale-independent replacements for some ctypes functions.  Use these
 * when you care about ASCII's notion of character types, because you are about
 * to send those types onto the wire.
 */
int EVUTIL_ISALPHA_(char c);
int EVUTIL_ISALNUM_(char c);
int EVUTIL_ISSPACE_(char c);
int EVUTIL_ISDIGIT_(char c);
int EVUTIL_ISXDIGIT_(char c);
int EVUTIL_ISPRINT_(char c);
int EVUTIL_ISLOWER_(char c);
int EVUTIL_ISUPPER_(char c);
char EVUTIL_TOUPPER_(char c);
char EVUTIL_TOLOWER_(char c);

/** Remove all trailing horizontal whitespace (space or tab) from the end of a
 * string */
void evutil_rtrim_lws_(char *);


/** Helper macro.  If we know that a given pointer points to a field in a
    structure, return a pointer to the structure itself.  Used to implement
    our half-baked C OO.  Example:

    struct subtype {
	int x;
	struct supertype common;
	int y;
    };
    ...
    void fn(struct supertype *super) {
	struct subtype *sub = EVUTIL_UPCAST(super, struct subtype, common);
	...
    }
 */
#define EVUTIL_UPCAST(ptr, type, field)				\
	((type *)(((char*)(ptr)) - evutil_offsetof(type, field)))

/* As open(pathname, flags, mode), except that the file is always opened with
 * the close-on-exec flag set. (And the mode argument is mandatory.)
 */
int evutil_open_closeonexec_(const char *pathname, int flags, unsigned mode);

int evutil_read_file_(const char *filename, char **content_out, size_t *len_out,
    int is_binary);

int evutil_socket_connect_(evutil_socket_t *fd_ptr, struct sockaddr *sa, int socklen);

int evutil_socket_finished_connecting_(evutil_socket_t fd);

int evutil_ersatz_socketpair_(int, int , int, evutil_socket_t[]);

int evutil_resolve_(int family, const char *hostname, struct sockaddr *sa,
    ev_socklen_t *socklen, int port);

const char *evutil_getenv_(const char *name);

/* Structure to hold the state of our weak random number generator.
 */
struct evutil_weakrand_state {
	ev_uint32_t seed;
};

#define EVUTIL_WEAKRAND_MAX EV_INT32_MAX

/* Initialize the state of a week random number generator based on 'seed'.  If
 * the seed is 0, construct a new seed based on not-very-strong platform
 * entropy, like the PID and the time of day.
 *
 * This function, and the other evutil_weakrand* functions, are meant for
 * speed, not security or statistical strength.  If you need a RNG which an
 * attacker can't predict, or which passes strong statistical tests, use the
 * evutil_secure_rng* functions instead.
 */
ev_uint32_t evutil_weakrand_seed_(struct evutil_weakrand_state *state, ev_uint32_t seed);
/* Return a pseudorandom value between 0 and EVUTIL_WEAKRAND_MAX inclusive.
 * Updates the state in 'seed' as needed -- this value must be protected by a
 * lock.
 */
ev_int32_t evutil_weakrand_(struct evutil_weakrand_state *seed);
/* Return a pseudorandom value x such that 0 <= x < top. top must be no more
 * than EVUTIL_WEAKRAND_MAX. Updates the state in 'seed' as needed -- this
 * value must be proteced by a lock */
ev_int32_t evutil_weakrand_range_(struct evutil_weakrand_state *seed, ev_int32_t top);

/* Evaluates to the same boolean value as 'p', and hints to the compiler that
 * we expect this value to be false. */
#if defined(__GNUC__) && __GNUC__ >= 3         /* gcc 3.0 or later */
#define EVUTIL_UNLIKELY(p) __builtin_expect(!!(p),0)
#else
#define EVUTIL_UNLIKELY(p) (p)
#endif

/* Replacement for assert() that calls event_errx on failure. */
#ifdef NDEBUG
#define EVUTIL_ASSERT(cond) EVUTIL_NIL_CONDITION_(cond)
#define EVUTIL_FAILURE_CHECK(cond) 0
#else
#define EVUTIL_ASSERT(cond)						\
	do {								\
		if (EVUTIL_UNLIKELY(!(cond))) {				\
			event_errx(EVENT_ERR_ABORT_,			\
			    "%s:%d: Assertion %s failed in %s",		\
			    __FILE__,__LINE__,#cond,__func__);		\
			/* In case a user-supplied handler tries to */	\
			/* return control to us, log and abort here. */	\
			(void)fprintf(stderr,				\
			    "%s:%d: Assertion %s failed in %s",		\
			    __FILE__,__LINE__,#cond,__func__);		\
			abort();					\
		}							\
	} while (0)
#define EVUTIL_FAILURE_CHECK(cond) EVUTIL_UNLIKELY(cond)
#endif

#ifndef EVENT__HAVE_STRUCT_SOCKADDR_STORAGE
/* Replacement for sockaddr storage that we can use internally on platforms
 * that lack it.  It is not space-efficient, but neither is sockaddr_storage.
 */
struct sockaddr_storage {
	union {
		struct sockaddr ss_sa;
		struct sockaddr_in ss_sin;
		struct sockaddr_in6 ss_sin6;
		char ss_padding[128];
	} ss_union;
};
#define ss_family ss_union.ss_sa.sa_family
#endif

/* Internal addrinfo error code.  This one is returned from only from
 * evutil_getaddrinfo_common_, when we are sure that we'll have to hit a DNS
 * server. */
#define EVUTIL_EAI_NEED_RESOLVE      -90002

struct evdns_base;
struct evdns_getaddrinfo_request;
typedef struct evdns_getaddrinfo_request* (*evdns_getaddrinfo_fn)(
    struct evdns_base *base,
    const char *nodename, const char *servname,
    const struct evutil_addrinfo *hints_in,
    void (*cb)(int, struct evutil_addrinfo *, void *), void *arg);

void evutil_set_evdns_getaddrinfo_fn_(evdns_getaddrinfo_fn fn);

struct evutil_addrinfo *evutil_new_addrinfo_(struct sockaddr *sa,
    ev_socklen_t socklen, const struct evutil_addrinfo *hints);
struct evutil_addrinfo *evutil_addrinfo_append_(struct evutil_addrinfo *first,
    struct evutil_addrinfo *append);
void evutil_adjust_hints_for_addrconfig_(struct evutil_addrinfo *hints);
int evutil_getaddrinfo_common_(const char *nodename, const char *servname,
    struct evutil_addrinfo *hints, struct evutil_addrinfo **res, int *portnum);

int evutil_getaddrinfo_async_(struct evdns_base *dns_base,
    const char *nodename, const char *servname,
    const struct evutil_addrinfo *hints_in,
    void (*cb)(int, struct evutil_addrinfo *, void *), void *arg);

/** Return true iff sa is a looback address. (That is, it is 127.0.0.1/8, or
 * ::1). */
int evutil_sockaddr_is_loopback_(const struct sockaddr *sa);


/**
    Formats a sockaddr sa into a string buffer of size outlen stored in out.
    Returns a pointer to out.  Always writes something into out, so it's safe
    to use the output of this function without checking it for NULL.
 */
const char *evutil_format_sockaddr_port_(const struct sockaddr *sa, char *out, size_t outlen);

int evutil_hex_char_to_int_(char c);


void evutil_free_secure_rng_globals_(void);
void evutil_free_globals_(void);

#ifdef _WIN32
HMODULE evutil_load_windows_system_library_(const TCHAR *library_name);
#endif

#ifndef EV_SIZE_FMT
#if defined(_MSC_VER) || defined(__MINGW32__) || defined(__MINGW64__)
#define EV_U64_FMT "%I64u"
#define EV_I64_FMT "%I64d"
#define EV_I64_ARG(x) ((__int64)(x))
#define EV_U64_ARG(x) ((unsigned __int64)(x))
#else
#define EV_U64_FMT "%llu"
#define EV_I64_FMT "%lld"
#define EV_I64_ARG(x) ((long long)(x))
#define EV_U64_ARG(x) ((unsigned long long)(x))
#endif
#endif

#ifdef _WIN32
#define EV_SOCK_FMT EV_I64_FMT
#define EV_SOCK_ARG(x) EV_I64_ARG((x))
#else
#define EV_SOCK_FMT "%d"
#define EV_SOCK_ARG(x) (x)
#endif

#if defined(__STDC__) && defined(__STDC_VERSION__)
#if (__STDC_VERSION__ >= 199901L)
#define EV_SIZE_FMT "%zu"
#define EV_SSIZE_FMT "%zd"
#define EV_SIZE_ARG(x) (x)
#define EV_SSIZE_ARG(x) (x)
#endif
#endif

#ifndef EV_SIZE_FMT
#if (EVENT__SIZEOF_SIZE_T <= EVENT__SIZEOF_LONG)
#define EV_SIZE_FMT "%lu"
#define EV_SSIZE_FMT "%ld"
#define EV_SIZE_ARG(x) ((unsigned long)(x))
#define EV_SSIZE_ARG(x) ((long)(x))
#else
#define EV_SIZE_FMT EV_U64_FMT
#define EV_SSIZE_FMT EV_I64_FMT
#define EV_SIZE_ARG(x) EV_U64_ARG(x)
#define EV_SSIZE_ARG(x) EV_I64_ARG(x)
#endif
#endif

evutil_socket_t evutil_socket_(int domain, int type, int protocol);
evutil_socket_t evutil_accept4_(evutil_socket_t sockfd, struct sockaddr *addr,
    ev_socklen_t *addrlen, int flags);

    /* used by one of the test programs.. */
EVENT2_EXPORT_SYMBOL
int evutil_make_internal_pipe_(evutil_socket_t fd[2]);
evutil_socket_t evutil_eventfd_(unsigned initval, int flags);

#ifdef SOCK_NONBLOCK
#define EVUTIL_SOCK_NONBLOCK SOCK_NONBLOCK
#else
#define EVUTIL_SOCK_NONBLOCK 0x4000000
#endif
#ifdef SOCK_CLOEXEC
#define EVUTIL_SOCK_CLOEXEC SOCK_CLOEXEC
#else
#define EVUTIL_SOCK_CLOEXEC 0x80000000
#endif
#ifdef EFD_NONBLOCK
#define EVUTIL_EFD_NONBLOCK EFD_NONBLOCK
#else
#define EVUTIL_EFD_NONBLOCK 0x4000
#endif
#ifdef EFD_CLOEXEC
#define EVUTIL_EFD_CLOEXEC EFD_CLOEXEC
#else
#define EVUTIL_EFD_CLOEXEC 0x8000
#endif

void evutil_memclear_(void *mem, size_t len);

#ifdef __cplusplus
}
#endif

#endif
