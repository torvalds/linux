/* $NetBSD: t_file2.c,v 1.4 2017/01/13 21:30:41 christos Exp $ */

/*-
 * Copyright (c) 2002, 2008 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jaromir Dolecek.
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
__COPYRIGHT("@(#) Copyright (c) 2008\
 The NetBSD Foundation, inc. All rights reserved.");
__RCSID("$NetBSD: t_file2.c,v 1.4 2017/01/13 21:30:41 christos Exp $");

#include <sys/event.h>

#include <fcntl.h>
#include <unistd.h>

#include <atf-c.h>

#include "h_macros.h"

ATF_TC(file2);
ATF_TC_HEAD(file2, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Checks EVFILT_READ for regular files. This test used to "
	    "trigger deadlock caused by problem fixed in revision 1.79.2.10 "
	    "of sys/kern/kern_descrip.c");
}
ATF_TC_BODY(file2, tc)
{
	int fd1, fd2, kq;
	struct kevent event[1];

	RL(fd1 = open("afile", O_RDONLY|O_CREAT, 0644));
	RL(fd2 = open("bfile", O_RDONLY|O_CREAT, 0644));

#if 1		/* XXX: why was this disabled? */
	RL(lseek(fd1, 0, SEEK_END));
#endif

	RL(kq = kqueue());

	EV_SET(&event[0], fd1, EVFILT_READ, EV_ADD|EV_ENABLE, 0, 0, 0);
	RL(kevent(kq, event, 1, NULL, 0, NULL));

	RL(dup2(fd2, fd1));
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, file2);

	return atf_no_error();
}
