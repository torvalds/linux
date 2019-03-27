/*	$NetBSD: t_posix_fallocate.c,v 1.1 2015/01/31 23:06:57 christos Exp $	*/

/*-
 * Copyright 2015, Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *  * Neither the name of Google nor the names of its contributors may
 *    be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__RCSID("$NetBSD: t_posix_fallocate.c,v 1.1 2015/01/31 23:06:57 christos Exp $");

#include <atf-c.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

ATF_TC_WITHOUT_HEAD(ebadf);
ATF_TC_BODY(ebadf, tc)
{
	int rc, saved;

	errno = 1111;
	rc = posix_fallocate(-1, 0, 4096);
	saved = errno;
	if (rc == -1 && saved != 1111)
		atf_tc_fail("Should return error %s without setting errno.",
		    strerror(saved));
	if (rc != EBADF)
		atf_tc_fail("returned %s but expected %s.",
		    strerror(saved), strerror(EBADF));
	if (saved != 1111)
		atf_tc_fail("errno should be %d but got changed to %d.",
		    1111, saved);
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, ebadf);
	return atf_no_error();
}
