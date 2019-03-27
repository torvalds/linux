/*	$NetBSD: t_threads.c,v 1.9 2017/01/13 05:18:22 christos Exp $	*/

/*-
 * Copyright (c) 2016 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */


#include <sys/cdefs.h>
__RCSID("$NetBSD: t_threads.c,v 1.9 2017/01/13 05:18:22 christos Exp $");

#include <dlfcn.h>
#include <pthread.h>
#include <pthread_dbg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <atf-c.h>

#include "h_common.h"

#define MAX_THREADS (size_t)10

ATF_TC(threads1);
ATF_TC_HEAD(threads1, tc)
{

	atf_tc_set_md_var(tc, "descr",
	    "Asserts that td_thr_iter() call without extra logic works");
}

static volatile int exiting1;

static void *
busyFunction1(void *arg)
{

	while (exiting1 == 0)
		usleep(50000);

	return NULL;
}

static int
iterateThreads1(td_thread_t *thread, void *arg)
{

	return TD_ERR_OK;
}

ATF_TC_BODY(threads1, tc)
{
	struct td_proc_callbacks_t dummy_callbacks;
	td_proc_t *main_ta;
	size_t i;
	pthread_t threads[MAX_THREADS];

	dummy_callbacks.proc_read	= basic_proc_read;
	dummy_callbacks.proc_write	= basic_proc_write;
	dummy_callbacks.proc_lookup	= basic_proc_lookup;
	dummy_callbacks.proc_regsize	= dummy_proc_regsize;
	dummy_callbacks.proc_getregs	= dummy_proc_getregs;
	dummy_callbacks.proc_setregs	= dummy_proc_setregs;

	for (i = 0; i < MAX_THREADS; i++) {
		printf("Creating thread %zu\n", i);
		PTHREAD_REQUIRE
		    (pthread_create(&threads[i], NULL, busyFunction1, NULL));
	}

	printf("Calling td_open(3)\n");
	ATF_REQUIRE(td_open(&dummy_callbacks, NULL, &main_ta) == TD_ERR_OK);

	ATF_REQUIRE(td_thr_iter(main_ta, iterateThreads1, NULL) == TD_ERR_OK);

	exiting1 = 1;

	printf("Calling td_close(3)\n");
	ATF_REQUIRE(td_close(main_ta) == TD_ERR_OK);
}

ATF_TC(threads2);
ATF_TC_HEAD(threads2, tc)
{

	atf_tc_set_md_var(tc, "descr",
	    "Asserts that td_thr_iter() call is executed for each thread once");
}

static volatile int exiting2;

static void *
busyFunction2(void *arg)
{

	while (exiting2 == 0)
		usleep(50000);

	return NULL;
}

static int
iterateThreads2(td_thread_t *thread, void *arg)
{
	int *counter = (int *)arg;

	++(*counter);

	return TD_ERR_OK;
}

ATF_TC_BODY(threads2, tc)
{
	struct td_proc_callbacks_t dummy_callbacks;
	td_proc_t *main_ta;
	size_t i;
	pthread_t threads[MAX_THREADS];
	int count = 0;

	dummy_callbacks.proc_read	= basic_proc_read;
	dummy_callbacks.proc_write	= basic_proc_write;
	dummy_callbacks.proc_lookup	= basic_proc_lookup;
	dummy_callbacks.proc_regsize	= dummy_proc_regsize;
	dummy_callbacks.proc_getregs	= dummy_proc_getregs;
	dummy_callbacks.proc_setregs	= dummy_proc_setregs;


	for (i = 0; i < MAX_THREADS; i++) {
		printf("Creating thread %zu\n", i);
		PTHREAD_REQUIRE
		    (pthread_create(&threads[i], NULL, busyFunction2, NULL));
	}

	printf("Calling td_open(3)\n");
	ATF_REQUIRE(td_open(&dummy_callbacks, NULL, &main_ta) == TD_ERR_OK);

	ATF_REQUIRE(td_thr_iter(main_ta, iterateThreads2, &count) == TD_ERR_OK);

	exiting2 = 1;

	printf("Calling td_close(3)\n");
	ATF_REQUIRE(td_close(main_ta) == TD_ERR_OK);

	ATF_REQUIRE_EQ_MSG(count, MAX_THREADS + 1,
	    "counted threads (%d) != expected threads (%zu)",
	    count, MAX_THREADS + 1);
}

ATF_TC(threads3);
ATF_TC_HEAD(threads3, tc)
{

	atf_tc_set_md_var(tc, "descr",
	    "Asserts that for each td_thr_iter() call td_thr_info() is valid");
}

static volatile int exiting3;

static void *
busyFunction3(void *arg)
{

	while (exiting3 == 0)
		usleep(50000);

	return NULL;
}

static int
iterateThreads3(td_thread_t *thread, void *arg)
{
	int *counter = (int *)arg;
	td_thread_info_t info;

	ATF_REQUIRE(td_thr_info(thread, &info) == TD_ERR_OK);

	++(*counter);

	return TD_ERR_OK;
}

ATF_TC_BODY(threads3, tc)
{
	struct td_proc_callbacks_t dummy_callbacks;
	td_proc_t *main_ta;
	size_t i;
	pthread_t threads[MAX_THREADS];
	int count = 0;

	dummy_callbacks.proc_read	= basic_proc_read;
	dummy_callbacks.proc_write	= basic_proc_write;
	dummy_callbacks.proc_lookup	= basic_proc_lookup;
	dummy_callbacks.proc_regsize	= dummy_proc_regsize;
	dummy_callbacks.proc_getregs	= dummy_proc_getregs;
	dummy_callbacks.proc_setregs	= dummy_proc_setregs;


	for (i = 0; i < MAX_THREADS; i++) {
		printf("Creating thread %zu\n", i);
		PTHREAD_REQUIRE
		    (pthread_create(&threads[i], NULL, busyFunction3, NULL));
	}

	printf("Calling td_open(3)\n");
	ATF_REQUIRE(td_open(&dummy_callbacks, NULL, &main_ta) == TD_ERR_OK);

	ATF_REQUIRE(td_thr_iter(main_ta, iterateThreads3, &count) == TD_ERR_OK);

	exiting3 = 1;

	printf("Calling td_close(3)\n");
	ATF_REQUIRE(td_close(main_ta) == TD_ERR_OK);

	ATF_REQUIRE_EQ_MSG(count, MAX_THREADS + 1,
	    "counted threads (%d) != expected threads (%zu)",
	    count, MAX_THREADS + 1);
}

ATF_TC(threads4);
ATF_TC_HEAD(threads4, tc)
{

	atf_tc_set_md_var(tc, "descr",
	    "Asserts that for each td_thr_iter() call td_thr_getname() is "
	    "valid");
}

static volatile int exiting4;

static void *
busyFunction4(void *arg)
{

	while (exiting4 == 0)
		usleep(50000);

	return NULL;
}

static int
iterateThreads4(td_thread_t *thread, void *arg)
{
	int *counter = (int *)arg;
	char name[PTHREAD_MAX_NAMELEN_NP];

	ATF_REQUIRE(td_thr_getname(thread, name, sizeof(name)) == TD_ERR_OK);

	printf("Thread name: %s\n", name);

	++(*counter);

	return TD_ERR_OK;
}

ATF_TC_BODY(threads4, tc)
{
	struct td_proc_callbacks_t dummy_callbacks;
	td_proc_t *main_ta;
	size_t i;
	pthread_t threads[MAX_THREADS];
	int count = 0;

	dummy_callbacks.proc_read	= basic_proc_read;
	dummy_callbacks.proc_write	= basic_proc_write;
	dummy_callbacks.proc_lookup	= basic_proc_lookup;
	dummy_callbacks.proc_regsize	= dummy_proc_regsize;
	dummy_callbacks.proc_getregs	= dummy_proc_getregs;
	dummy_callbacks.proc_setregs	= dummy_proc_setregs;

	for (i = 0; i < MAX_THREADS; i++) {
		printf("Creating thread %zu\n", i);
		PTHREAD_REQUIRE
		    (pthread_create(&threads[i], NULL, busyFunction4, NULL));
	}

	for (i = 0; i < MAX_THREADS; i++) {
		PTHREAD_REQUIRE
		    (pthread_setname_np(threads[i], "test_%d", (void*)i));
	}

	printf("Calling td_open(3)\n");
	ATF_REQUIRE(td_open(&dummy_callbacks, NULL, &main_ta) == TD_ERR_OK);

	ATF_REQUIRE(td_thr_iter(main_ta, iterateThreads4, &count) == TD_ERR_OK);

	exiting4 = 1;

	printf("Calling td_close(3)\n");
	ATF_REQUIRE(td_close(main_ta) == TD_ERR_OK);

	ATF_REQUIRE_EQ_MSG(count, MAX_THREADS + 1,
	    "counted threads (%d) != expected threads (%zu)",
	    count, MAX_THREADS + 1);
}

ATF_TC(threads5);
ATF_TC_HEAD(threads5, tc)
{

	atf_tc_set_md_var(tc, "descr",
	    "Asserts that td_thr_getname() handles shorter buffer parameter "
	    "and the result is properly truncated");
}

static volatile int exiting5;

static void *
busyFunction5(void *arg)
{

	while (exiting5 == 0)
		usleep(50000);

	return NULL;
}

static int
iterateThreads5(td_thread_t *thread, void *arg)
{
	int *counter = (int *)arg;
	/* Arbitrarily short string buffer */
	char name[3];

	ATF_REQUIRE(td_thr_getname(thread, name, sizeof(name)) == TD_ERR_OK);

	printf("Thread name: %s\n", name);

	/* strlen(3) does not count including a '\0' character */
	ATF_REQUIRE(strlen(name) < sizeof(name));

	++(*counter);

	return TD_ERR_OK;
}

ATF_TC_BODY(threads5, tc)
{
	struct td_proc_callbacks_t dummy_callbacks;
	td_proc_t *main_ta;
	size_t i;
	pthread_t threads[MAX_THREADS];
	int count = 0;

	dummy_callbacks.proc_read	= basic_proc_read;
	dummy_callbacks.proc_write	= basic_proc_write;
	dummy_callbacks.proc_lookup	= basic_proc_lookup;
	dummy_callbacks.proc_regsize	= dummy_proc_regsize;
	dummy_callbacks.proc_getregs	= dummy_proc_getregs;
	dummy_callbacks.proc_setregs	= dummy_proc_setregs;

	for (i = 0; i < MAX_THREADS; i++) {
		printf("Creating thread %zu\n", i);
		PTHREAD_REQUIRE
		    (pthread_create(&threads[i], NULL, busyFunction5, NULL));
	}

	for (i = 0; i < MAX_THREADS; i++) {
		PTHREAD_REQUIRE
		    (pthread_setname_np(threads[i], "test_%d", (void*)i));
	}

	printf("Calling td_open(3)\n");
	ATF_REQUIRE(td_open(&dummy_callbacks, NULL, &main_ta) == TD_ERR_OK);

	ATF_REQUIRE(td_thr_iter(main_ta, iterateThreads5, &count) == TD_ERR_OK);

	exiting5 = 1;

	printf("Calling td_close(3)\n");
	ATF_REQUIRE(td_close(main_ta) == TD_ERR_OK);

	ATF_REQUIRE_EQ_MSG(count, MAX_THREADS + 1,
	    "counted threads (%d) != expected threads (%zu)",
	    count, MAX_THREADS + 1);
}

ATF_TC(threads6);
ATF_TC_HEAD(threads6, tc)
{

	atf_tc_set_md_var(tc, "descr",
	    "Asserts that pthread_t can be translated with td_map_pth2thr() "
	    "to td_thread_t -- and assert earlier that td_thr_iter() call is "
	    "valid");
}

static volatile int exiting6;

static void *
busyFunction6(void *arg)
{

	while (exiting6 == 0)
		usleep(50000);

	return NULL;
}

static int
iterateThreads6(td_thread_t *thread, void *arg)
{
	int *counter = (int *)arg;

	++(*counter);

	return TD_ERR_OK;
}

ATF_TC_BODY(threads6, tc)
{
	struct td_proc_callbacks_t dummy_callbacks;
	td_proc_t *main_ta;
	size_t i;
	pthread_t threads[MAX_THREADS];
	int count = 0;

	dummy_callbacks.proc_read	= basic_proc_read;
	dummy_callbacks.proc_write	= basic_proc_write;
	dummy_callbacks.proc_lookup	= basic_proc_lookup;
	dummy_callbacks.proc_regsize	= dummy_proc_regsize;
	dummy_callbacks.proc_getregs	= dummy_proc_getregs;
	dummy_callbacks.proc_setregs	= dummy_proc_setregs;

	for (i = 0; i < MAX_THREADS; i++) {
		printf("Creating thread %zu\n", i);
		PTHREAD_REQUIRE
		    (pthread_create(&threads[i], NULL, busyFunction6, NULL));
	}

	printf("Calling td_open(3)\n");
	ATF_REQUIRE(td_open(&dummy_callbacks, NULL, &main_ta) == TD_ERR_OK);

	ATF_REQUIRE(td_thr_iter(main_ta, iterateThreads6, &count) == TD_ERR_OK);

	for (i = 0; i < MAX_THREADS; i++) {
		td_thread_t *td_thread;
		ATF_REQUIRE(td_map_pth2thr(main_ta, threads[i], &td_thread)
		    == TD_ERR_OK);
	}

	exiting6 = 1;

	printf("Calling td_close(3)\n");
	ATF_REQUIRE(td_close(main_ta) == TD_ERR_OK);

	ATF_REQUIRE_EQ_MSG(count, MAX_THREADS + 1,
	    "counted threads (%d) != expected threads (%zu)",
	    count, MAX_THREADS + 1);
}

ATF_TC(threads7);
ATF_TC_HEAD(threads7, tc)
{

	atf_tc_set_md_var(tc, "descr",
	    "Asserts that pthread_t can be translated with td_map_pth2thr() "
	    "to td_thread_t -- and assert later that td_thr_iter() call is "
	    "valid");
}

static volatile int exiting7;

static void *
busyFunction7(void *arg)
{

	while (exiting7 == 0)
		usleep(50000);

	return NULL;
}

static int
iterateThreads7(td_thread_t *thread, void *arg)
{
	int *counter = (int *)arg;

	++(*counter);

	return TD_ERR_OK;
}

ATF_TC_BODY(threads7, tc)
{
	struct td_proc_callbacks_t dummy_callbacks;
	td_proc_t *main_ta;
	size_t i;
	pthread_t threads[MAX_THREADS];
	int count = 0;

	dummy_callbacks.proc_read	= basic_proc_read;
	dummy_callbacks.proc_write	= basic_proc_write;
	dummy_callbacks.proc_lookup	= basic_proc_lookup;
	dummy_callbacks.proc_regsize	= dummy_proc_regsize;
	dummy_callbacks.proc_getregs	= dummy_proc_getregs;
	dummy_callbacks.proc_setregs	= dummy_proc_setregs;

	for (i = 0; i < MAX_THREADS; i++) {
		printf("Creating thread %zu\n", i);
		PTHREAD_REQUIRE
		    (pthread_create(&threads[i], NULL, busyFunction7, NULL));
	}

	printf("Calling td_open(3)\n");
	ATF_REQUIRE(td_open(&dummy_callbacks, NULL, &main_ta) == TD_ERR_OK);

	for (i = 0; i < MAX_THREADS; i++) {
		td_thread_t *td_thread;
		ATF_REQUIRE(td_map_pth2thr(main_ta, threads[i], &td_thread)
		    == TD_ERR_OK);
	}

	ATF_REQUIRE(td_thr_iter(main_ta, iterateThreads7, &count) == TD_ERR_OK);

	exiting7 = 1;

	printf("Calling td_close(3)\n");
	ATF_REQUIRE(td_close(main_ta) == TD_ERR_OK);

	ATF_REQUIRE_EQ_MSG(count, MAX_THREADS + 1,
	    "counted threads (%d) != expected threads (%zu)",
	    count, MAX_THREADS + 1);
}

ATF_TC(threads8);
ATF_TC_HEAD(threads8, tc)
{

	atf_tc_set_md_var(tc, "descr",
	    "Asserts that pthread_t can be translated with td_map_pth2thr() "
	    "to td_thread_t -- compare thread's name of pthread_t and "
	    "td_thread_t");
}

static volatile int exiting8;

static void *
busyFunction8(void *arg)
{

	while (exiting8 == 0)
		usleep(50000);

	return NULL;
}

static int
iterateThreads8(td_thread_t *thread, void *arg)
{
	int *counter = (int *)arg;

	++(*counter);

	return TD_ERR_OK;
}

ATF_TC_BODY(threads8, tc)
{
	struct td_proc_callbacks_t dummy_callbacks;
	td_proc_t *main_ta;
	size_t i;
	pthread_t threads[MAX_THREADS];
	int count = 0;

	dummy_callbacks.proc_read	= basic_proc_read;
	dummy_callbacks.proc_write	= basic_proc_write;
	dummy_callbacks.proc_lookup	= basic_proc_lookup;
	dummy_callbacks.proc_regsize	= dummy_proc_regsize;
	dummy_callbacks.proc_getregs	= dummy_proc_getregs;
	dummy_callbacks.proc_setregs	= dummy_proc_setregs;

	for (i = 0; i < MAX_THREADS; i++) {
		printf("Creating thread %zu\n", i);
		PTHREAD_REQUIRE
		    (pthread_create(&threads[i], NULL, busyFunction8, NULL));
	}

	printf("Calling td_open(3)\n");
	ATF_REQUIRE(td_open(&dummy_callbacks, NULL, &main_ta) == TD_ERR_OK);

	ATF_REQUIRE(td_thr_iter(main_ta, iterateThreads8, &count) == TD_ERR_OK);

	for (i = 0; i < MAX_THREADS; i++) {
		td_thread_t *td_thread;
		char td_threadname[PTHREAD_MAX_NAMELEN_NP];
		char pth_threadname[PTHREAD_MAX_NAMELEN_NP];
		ATF_REQUIRE(td_map_pth2thr(main_ta, threads[i], &td_thread)
		    == TD_ERR_OK);
		ATF_REQUIRE(td_thr_getname(td_thread, td_threadname,
		    sizeof(td_threadname)) == TD_ERR_OK);
		PTHREAD_REQUIRE(pthread_getname_np(threads[i], pth_threadname,
		    sizeof(pth_threadname)));
		ATF_REQUIRE(strcmp(td_threadname, pth_threadname) == 0);
	}

	exiting8 = 1;

	printf("Calling td_close(3)\n");
	ATF_REQUIRE(td_close(main_ta) == TD_ERR_OK);

	ATF_REQUIRE_EQ_MSG(count, MAX_THREADS + 1,
	    "counted threads (%d) != expected threads (%zu)",
	    count, MAX_THREADS + 1);
}

ATF_TC(threads9);
ATF_TC_HEAD(threads9, tc)
{

	atf_tc_set_md_var(tc, "descr",
	    "Asserts that pthread_t can be translated with td_map_pth2thr() "
	    "to td_thread_t -- assert that thread is in the TD_STATE_RUNNING "
            "state");
}

static volatile int exiting9;

static void *
busyFunction9(void *arg)
{

	while (exiting9 == 0)
		usleep(50000);

	return NULL;
}

static int
iterateThreads9(td_thread_t *thread, void *arg)
{
	int *counter = (int *)arg;

	++(*counter);

	return TD_ERR_OK;
}

ATF_TC_BODY(threads9, tc)
{
	struct td_proc_callbacks_t dummy_callbacks;
	td_proc_t *main_ta;
	size_t i;
	pthread_t threads[MAX_THREADS];
	int count = 0;

	dummy_callbacks.proc_read	= basic_proc_read;
	dummy_callbacks.proc_write	= basic_proc_write;
	dummy_callbacks.proc_lookup	= basic_proc_lookup;
	dummy_callbacks.proc_regsize	= dummy_proc_regsize;
	dummy_callbacks.proc_getregs	= dummy_proc_getregs;
	dummy_callbacks.proc_setregs	= dummy_proc_setregs;

	for (i = 0; i < MAX_THREADS; i++) {
		printf("Creating thread %zu\n", i);
		PTHREAD_REQUIRE
		    (pthread_create(&threads[i], NULL, busyFunction9, NULL));
	}

	printf("Calling td_open(3)\n");
	ATF_REQUIRE(td_open(&dummy_callbacks, NULL, &main_ta) == TD_ERR_OK);

	for (i = 0; i < MAX_THREADS; i++) {
		td_thread_t *td_thread;
		td_thread_info_t info;
		ATF_REQUIRE(td_map_pth2thr(main_ta, threads[i], &td_thread)
		    == TD_ERR_OK);
		ATF_REQUIRE(td_thr_info(td_thread, &info) == TD_ERR_OK);
		ATF_REQUIRE_EQ(info.thread_state, TD_STATE_RUNNING);
	}

	ATF_REQUIRE(td_thr_iter(main_ta, iterateThreads9, &count) == TD_ERR_OK);

	exiting9 = 1;

	printf("Calling td_close(3)\n");
	ATF_REQUIRE(td_close(main_ta) == TD_ERR_OK);

	ATF_REQUIRE_EQ_MSG(count, MAX_THREADS + 1,
	    "counted threads (%d) != expected threads (%zu)",
	    count, MAX_THREADS + 1);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, threads1);
	ATF_TP_ADD_TC(tp, threads2);
	ATF_TP_ADD_TC(tp, threads3);
	ATF_TP_ADD_TC(tp, threads4);
	ATF_TP_ADD_TC(tp, threads5);
	ATF_TP_ADD_TC(tp, threads6);
	ATF_TP_ADD_TC(tp, threads7);
	ATF_TP_ADD_TC(tp, threads8);
	ATF_TP_ADD_TC(tp, threads9);

	return atf_no_error();
}
