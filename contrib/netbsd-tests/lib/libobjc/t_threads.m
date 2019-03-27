/* $NetBSD: t_threads.m,v 1.2 2013/10/31 21:02:11 christos Exp $ */

/*
 * Copyright (c) 2010 The NetBSD Foundation, Inc.
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

/* Originally written by David Wetzel */

#include <assert.h>
#include <stdio.h>
#include <unistd.h>

#include <atf-c.h>

#include <objc/objc.h>
#include <objc/thr.h>
#include <objc/Object.h>
#if __GNUC_PREREQ__(4,8)
#include <objc/runtime.h>
#endif

static int IsMultithreaded = 0;
static objc_mutex_t Mutex;
static objc_condition_t Condition;

@interface MyClass : Object
{
}
-(void)start;
#if __GNUC_PREREQ__(4,8)
-init;
+new;
+alloc;
-free;
#endif
@end

@implementation MyClass
-(void)start
{
	printf("detached thread started!\n");

	objc_condition_signal(Condition);
}
#if __GNUC_PREREQ__(4,8)
-init
{
	return self;
}

+new
{
	return [[self alloc] init];
}

+alloc
{
	return class_createInstance(self, 0);
}

-free
{
	return object_dispose(self);
}
#endif
@end

static void
becomeMultiThreaded(void)
{
	printf("becoming multithreaded!\n");
	IsMultithreaded++;
}

ATF_TC(thread_callback);
ATF_TC_HEAD(thread_callback, tc)
{
	atf_tc_set_md_var(tc, "descr", "Checks that the thread callback is only"
	    "called once");
}
ATF_TC_BODY(thread_callback, tc)
{
	id o = [MyClass new];
	objc_thread_callback cb;
	objc_thread_t rv;

	cb = objc_set_thread_callback(becomeMultiThreaded);
	printf("Old Callback: %p\n", cb);
	ATF_CHECK(cb == 0);

	Mutex = objc_mutex_allocate();
	Condition = objc_condition_allocate();

	ATF_CHECK_EQ(0, IsMultithreaded);

	rv = objc_thread_detach(@selector(start), o, nil);
	printf("detach value: %p\n", rv);
	assert(rv != NULL);

	objc_mutex_lock(Mutex);
	objc_condition_wait(Condition, Mutex);
	objc_mutex_unlock(Mutex);

	ATF_CHECK_EQ(1, IsMultithreaded);
	printf("Shutting down\n");
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, thread_callback);

	return atf_no_error();
}
