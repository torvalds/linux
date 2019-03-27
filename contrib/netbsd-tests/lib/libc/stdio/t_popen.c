/*	$NetBSD: t_popen.c,v 1.4 2013/02/15 23:27:19 christos Exp $ */

/*-
 * Copyright (c) 1999 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Matthias Scheler.
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
#ifndef lint
__COPYRIGHT("@(#) Copyright (c) 1999\
 The NetBSD Foundation, Inc.  All rights reserved.");
#endif /* not lint */

#ifndef lint
__RCSID("$NetBSD: t_popen.c,v 1.4 2013/02/15 23:27:19 christos Exp $");
#endif /* not lint */

#include <atf-c.h>

#include <sys/param.h>

#include <errno.h>
#include <err.h>
#include <paths.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#define _PATH_CAT	"/bin/cat"
#define BUFSIZE		(640*1024)
			/* 640KB ought to be enough for everyone. */
#define DATAFILE	"popen.data"

#define TEST_ERROR(a)						\
	do							\
	{							\
		warn(a);					\
		atf_tc_fail("Check stderr for error details.");	\
	} while ( /*CONSTCOND*/ 0 )

ATF_TC_WITH_CLEANUP(popen_zeropad);
ATF_TC_HEAD(popen_zeropad, tc)
{

	atf_tc_set_md_var(tc, "descr", "output format zero padding");
}

ATF_TC_BODY(popen_zeropad, tc)
{
	char *buffer, command[MAXPATHLEN];
	int idx, in;
	FILE *my_pipe;

	if ((buffer = malloc(BUFSIZE)) == NULL)
		atf_tc_skip("Unable to allocate buffer.");

	srand ((unsigned int)time(NULL));

	for (idx = 0; idx < BUFSIZE; idx++)
		buffer[idx]=(char)rand();

	(void)snprintf(command, sizeof(command), "%s >%s",
	    _PATH_CAT, DATAFILE);

	if ((my_pipe = popen(command, "w")) == NULL)
		TEST_ERROR("popen write");

	if (fwrite(buffer, sizeof(char), BUFSIZE, my_pipe) != BUFSIZE)
		TEST_ERROR("fwrite");

	if (pclose(my_pipe) == -1)
		TEST_ERROR("pclose");

	(void)snprintf(command, sizeof(command), "%s %s", _PATH_CAT, DATAFILE);

	if ((my_pipe = popen(command, "r")) == NULL)
		TEST_ERROR("popen read");

	idx = 0;
	while ((in = fgetc(my_pipe)) != EOF)
		if (idx == BUFSIZE) {
			errno = EFBIG;
			TEST_ERROR("read");
		}
		else if ((char)in != buffer[idx++]) {
		    	errno = EINVAL;
			TEST_ERROR("read");
		}

	if (idx < BUFSIZE) {
		errno = EIO;
		TEST_ERROR("read");
	}

	if (pclose(my_pipe) == -1)
		TEST_ERROR("pclose");
}

ATF_TC_CLEANUP(popen_zeropad, tc)
{
	(void)unlink(DATAFILE);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, popen_zeropad);

	return atf_no_error();
}
