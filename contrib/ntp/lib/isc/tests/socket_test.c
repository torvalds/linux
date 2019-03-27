/*
 * Copyright (C) 2011, 2012  Internet Systems Consortium, Inc. ("ISC")
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND ISC DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS.  IN NO EVENT SHALL ISC BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE
 * OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

/* $Id$ */

/*! \file */

#include <config.h>

#include <atf-c.h>

#include <unistd.h>
#include <time.h>

#include <isc/socket.h>

#include "../task_p.h"
#include "isctest.h"

/*
 * Helper functions
 */
typedef struct {
	isc_boolean_t done;
	isc_result_t result;
} completion_t;

static void
completion_init(completion_t *completion) {
	completion->done = ISC_FALSE;
}

static void
event_done(isc_task_t *task, isc_event_t *event) {
	isc_socketevent_t *dev;
	completion_t *completion = event->ev_arg;

	UNUSED(task);

	dev = (isc_socketevent_t *) event;
	completion->result = dev->result;
	completion->done = ISC_TRUE;
	isc_event_free(&event);
}

static isc_result_t
waitfor(completion_t *completion) {
	int i = 0;
	while (!completion->done && i++ < 5000) {
#ifndef ISC_PLATFORM_USETHREADS
		while (isc__taskmgr_ready(taskmgr))
			isc__taskmgr_dispatch(taskmgr);
#endif
		isc_test_nap(1000);
	}
	if (completion->done)
		return (ISC_R_SUCCESS);
	return (ISC_R_FAILURE);
}

/*
 * Individual unit tests
 */

/* Test UDP sendto/recv (IPv4) */
ATF_TC(udp_sendto);
ATF_TC_HEAD(udp_sendto, tc) {
	atf_tc_set_md_var(tc, "descr", "UDP sendto/recv");
}
ATF_TC_BODY(udp_sendto, tc) {
	isc_result_t result;
	isc_sockaddr_t addr1, addr2;
	struct in_addr in;
	isc_socket_t *s1 = NULL, *s2 = NULL;
	isc_task_t *task = NULL;
	char sendbuf[BUFSIZ], recvbuf[BUFSIZ];
	completion_t completion;
	isc_region_t r;

	UNUSED(tc);

	result = isc_test_begin(NULL, ISC_TRUE);
	ATF_REQUIRE_EQ(result, ISC_R_SUCCESS);

	/*
	 * Create two sockets: 127.0.0.1/5444 and 127.0.0.1/5445, talking to
	 * each other.
	 */
	in.s_addr = inet_addr("127.0.0.1");
	isc_sockaddr_fromin(&addr1, &in, 5444);
	isc_sockaddr_fromin(&addr2, &in, 5445);

	result = isc_socket_create(socketmgr, PF_INET, isc_sockettype_udp, &s1);
	ATF_REQUIRE_EQ(result, ISC_R_SUCCESS);
	result = isc_socket_bind(s1, &addr1, ISC_SOCKET_REUSEADDRESS);
	ATF_REQUIRE_EQ(result, ISC_R_SUCCESS);

	result = isc_socket_create(socketmgr, PF_INET, isc_sockettype_udp, &s2);
	ATF_REQUIRE_EQ(result, ISC_R_SUCCESS);
	result = isc_socket_bind(s2, &addr2, ISC_SOCKET_REUSEADDRESS);
	ATF_REQUIRE_EQ(result, ISC_R_SUCCESS);

	result = isc_task_create(taskmgr, 0, &task);
	ATF_REQUIRE_EQ(result, ISC_R_SUCCESS);

	strcpy(sendbuf, "Hello");
	r.base = (void *) sendbuf;
	r.length = strlen(sendbuf) + 1;

	completion_init(&completion);
	result = isc_socket_sendto(s1, &r, task, event_done, &completion,
				   &addr2, NULL);
	ATF_CHECK_EQ(result, ISC_R_SUCCESS);
	waitfor(&completion);
	ATF_CHECK(completion.done);
	ATF_CHECK_EQ(completion.result, ISC_R_SUCCESS);

	r.base = (void *) recvbuf;
	r.length = BUFSIZ;
	completion_init(&completion);
	result = isc_socket_recv(s2, &r, 1, task, event_done, &completion);
	ATF_CHECK_EQ(result, ISC_R_SUCCESS);
	waitfor(&completion);
	ATF_CHECK(completion.done);
	ATF_CHECK_EQ(completion.result, ISC_R_SUCCESS);
	ATF_CHECK_STREQ(recvbuf, "Hello");

	isc_task_detach(&task);

	isc_socket_detach(&s1);
	isc_socket_detach(&s2);

	isc_test_end();
}

/* Test UDP sendto/recv with duplicated socket */
ATF_TC(udp_dup);
ATF_TC_HEAD(udp_dup, tc) {
	atf_tc_set_md_var(tc, "descr", "duplicated socket sendto/recv");
}
ATF_TC_BODY(udp_dup, tc) {
	isc_result_t result;
	isc_sockaddr_t addr1, addr2;
	struct in_addr in;
	isc_socket_t *s1 = NULL, *s2 = NULL, *s3 = NULL;
	isc_task_t *task = NULL;
	char sendbuf[BUFSIZ], recvbuf[BUFSIZ];
	completion_t completion;
	isc_region_t r;

	UNUSED(tc);

	result = isc_test_begin(NULL, ISC_TRUE);
	ATF_REQUIRE_EQ(result, ISC_R_SUCCESS);

	/*
	 * Create two sockets: 127.0.0.1/5444 and 127.0.0.1/5445, talking to
	 * each other.
	 */
	in.s_addr = inet_addr("127.0.0.1");
	isc_sockaddr_fromin(&addr1, &in, 5444);
	isc_sockaddr_fromin(&addr2, &in, 5445);

	result = isc_socket_create(socketmgr, PF_INET, isc_sockettype_udp, &s1);
	ATF_REQUIRE_EQ(result, ISC_R_SUCCESS);
	result = isc_socket_bind(s1, &addr1, ISC_SOCKET_REUSEADDRESS);
	ATF_REQUIRE_EQ(result, ISC_R_SUCCESS);

	result = isc_socket_create(socketmgr, PF_INET, isc_sockettype_udp, &s2);
	ATF_REQUIRE_EQ(result, ISC_R_SUCCESS);
	result = isc_socket_bind(s2, &addr2, ISC_SOCKET_REUSEADDRESS);
	ATF_REQUIRE_EQ(result, ISC_R_SUCCESS);

	result = isc_socket_dup(s2, &s3);
	ATF_REQUIRE_EQ(result, ISC_R_SUCCESS);

	result = isc_task_create(taskmgr, 0, &task);
	ATF_REQUIRE_EQ(result, ISC_R_SUCCESS);

	strcpy(sendbuf, "Hello");
	r.base = (void *) sendbuf;
	r.length = strlen(sendbuf) + 1;

	completion_init(&completion);
	result = isc_socket_sendto(s1, &r, task, event_done, &completion,
				   &addr2, NULL);
	ATF_CHECK_EQ(result, ISC_R_SUCCESS);
	waitfor(&completion);
	ATF_CHECK(completion.done);
	ATF_CHECK_EQ(completion.result, ISC_R_SUCCESS);

	strcpy(sendbuf, "World");
	r.base = (void *) sendbuf;
	r.length = strlen(sendbuf) + 1;

	completion_init(&completion);
	result = isc_socket_sendto(s1, &r, task, event_done, &completion,
				   &addr2, NULL);
	ATF_CHECK_EQ(result, ISC_R_SUCCESS);
	waitfor(&completion);
	ATF_CHECK(completion.done);
	ATF_CHECK_EQ(completion.result, ISC_R_SUCCESS);

	r.base = (void *) recvbuf;
	r.length = BUFSIZ;
	completion_init(&completion);
	result = isc_socket_recv(s2, &r, 1, task, event_done, &completion);
	ATF_CHECK_EQ(result, ISC_R_SUCCESS);
	waitfor(&completion);
	ATF_CHECK(completion.done);
	ATF_CHECK_EQ(completion.result, ISC_R_SUCCESS);
	ATF_CHECK_STREQ(recvbuf, "Hello");

	r.base = (void *) recvbuf;
	r.length = BUFSIZ;
	completion_init(&completion);
	result = isc_socket_recv(s3, &r, 1, task, event_done, &completion);
	ATF_CHECK_EQ(result, ISC_R_SUCCESS);
	waitfor(&completion);
	ATF_CHECK(completion.done);
	ATF_CHECK_EQ(completion.result, ISC_R_SUCCESS);
	ATF_CHECK_STREQ(recvbuf, "World");

	isc_task_detach(&task);

	isc_socket_detach(&s1);
	isc_socket_detach(&s2);
	isc_socket_detach(&s3);

	isc_test_end();
}

/*
 * Main
 */
ATF_TP_ADD_TCS(tp) {
	ATF_TP_ADD_TC(tp, udp_sendto);
	ATF_TP_ADD_TC(tp, udp_dup);

	return (atf_no_error());
}

