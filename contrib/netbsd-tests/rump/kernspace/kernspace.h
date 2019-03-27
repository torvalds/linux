/*	$NetBSD: kernspace.h,v 1.4 2011/01/14 13:08:00 pooka Exp $	*/

/*-
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND
 * CONTRIBUTORS ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _TESTS_RUMP_KERNSPACE_KERNSPACE_H_
#define _TESTS_RUMP_KERNSPACE_KERNSPACE_H_

enum locktest { LOCKME_MTX, LOCKME_RWDOUBLEX, LOCKME_RWRX, LOCKME_RWXR,
	LOCKME_DESTROYHELD, LOCKME_DOUBLEINIT, LOCKME_DOUBLEFREE,
	LOCKME_MEMFREE };

void rumptest_busypage(void);
void rumptest_threadjoin(void);
void rumptest_thread(void);
void rumptest_tsleep(void);
void rumptest_alloc(size_t);
void rumptest_lockme(enum locktest);

void rumptest_sendsig(char *);
void rumptest_localsig(int);

#endif /* _TESTS_RUMP_KERNSPACE_KERNSPACE_H_ */
