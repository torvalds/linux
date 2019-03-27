/* $NetBSD: t_fifo.c,v 1.4 2017/01/13 21:30:41 christos Exp $ */

/*-
 * Copyright (c) 2008 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Luke Mewburn and Jaromir Dolecek.
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
__RCSID("$NetBSD: t_fifo.c,v 1.4 2017/01/13 21:30:41 christos Exp $");

#include <sys/event.h>
#include <sys/stat.h>

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <inttypes.h>

#include <atf-c.h>

#include "h_macros.h"

#define FIFONAME "fifo"

ATF_TC(fifo);
ATF_TC_HEAD(fifo, tc)
{
	atf_tc_set_md_var(tc, "descr", "Checks EVFILT_READ on fifo");
}
ATF_TC_BODY(fifo, tc)
{
	int kq, n, fd;
	struct kevent event[1];
	char buffer[128];

	RL(mkfifo(FIFONAME, 0644));
	RL(fd = open(FIFONAME, O_RDWR, 0644));

	RL(kq = kqueue());

	EV_SET(&event[0], fd, EVFILT_READ, EV_ADD|EV_ENABLE, 0, 0, 0);
	RL(kevent(kq, event, 1, NULL, 0, NULL));

	/* make sure there is something in the fifo */
	RL(write(fd, "foo", 3));
	(void)printf("fifo: wrote 'foo'\n");

	(void)memset(event, 0, sizeof(event));

	RL(n = kevent(kq, NULL, 0, event, 1, NULL));

	(void)printf("kevent num %d filt %d flags: %#x, fflags: %#x, "
#ifdef __FreeBSD__
	    "data: %" PRIdPTR "\n", n, event[0].filter, event[0].flags,
#else
	    "data: %" PRId64 "\n", n, event[0].filter, event[0].flags,
#endif
	    event[0].fflags, event[0].data);

	ATF_REQUIRE_EQ(event[0].filter, EVFILT_READ);

	RL(n = read(fd, buffer, event[0].data));
	buffer[n] = '\0';
	(void)printf("fifo: read '%s'\n", buffer);

	RL(close(fd));
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, fifo);

	return atf_no_error();
}
