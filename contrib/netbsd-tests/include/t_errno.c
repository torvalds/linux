/*	$NetBSD: t_errno.c,v 1.1 2011/05/01 17:07:05 jruoho Exp $ */

/*-
 * Copyright (c) 2011 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jukka Ruohonen.
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
__RCSID("$NetBSD: t_errno.c,v 1.1 2011/05/01 17:07:05 jruoho Exp $");

#include <atf-c.h>
#include <errno.h>

ATF_TC(errno_constants);
ATF_TC_HEAD(errno_constants, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test POSIX constants in <errno.h>");
}

ATF_TC_BODY(errno_constants, tc)
{
	bool fail;

	/*
	 * The following definitions should be available
	 * according to IEEE Std 1003.1-2008, issue 7.
	 */
	atf_tc_expect_fail("PR standards/44921");

	fail = true;

#ifdef E2BIG
	fail = false;
#endif
	if (fail != false)
		atf_tc_fail_nonfatal("E2BIG not defined");

	fail = true;

#ifdef EACCES
	fail = false;
#endif
	if (fail != false)
		atf_tc_fail_nonfatal("EACCES not defined");

	fail = true;

#ifdef EADDRINUSE
	fail = false;
#endif
	if (fail != false)
		atf_tc_fail_nonfatal("EADDRINUSE not defined");

	fail = true;

#ifdef EADDRNOTAVAIL
	fail = false;
#endif
	if (fail != false)
		atf_tc_fail_nonfatal("EADDRNOTAVAIL not defined");

	fail = true;

#ifdef EAFNOSUPPORT
	fail = false;
#endif
	if (fail != false)
		atf_tc_fail_nonfatal("EAFNOSUPPORT not defined");

	fail = true;

#ifdef EAGAIN
	fail = false;
#endif
	if (fail != false)
		atf_tc_fail_nonfatal("EAGAIN not defined");

	fail = true;

#ifdef EALREADY
	fail = false;
#endif
	if (fail != false)
		atf_tc_fail_nonfatal("EALREADY not defined");

	fail = true;

#ifdef EBADF
	fail = false;
#endif
	if (fail != false)
		atf_tc_fail_nonfatal("EBADF not defined");

	fail = true;

#ifdef EBADMSG
	fail = false;
#endif
	if (fail != false)
		atf_tc_fail_nonfatal("EBADMSG not defined");

	fail = true;

#ifdef EBUSY
	fail = false;
#endif
	if (fail != false)
		atf_tc_fail_nonfatal("EBUSY not defined");

	fail = true;

#ifdef ECANCELED
	fail = false;
#endif
	if (fail != false)
		atf_tc_fail_nonfatal("ECANCELED not defined");

	fail = true;

#ifdef ECHILD
	fail = false;
#endif
	if (fail != false)
		atf_tc_fail_nonfatal("ECHILD not defined");

	fail = true;

#ifdef ECONNABORTED
	fail = false;
#endif
	if (fail != false)
		atf_tc_fail_nonfatal("ECONNABORTED not defined");

	fail = true;

#ifdef ECONNREFUSED
	fail = false;
#endif
	if (fail != false)
		atf_tc_fail_nonfatal("ECONNREFUSED not defined");

	fail = true;

#ifdef ECONNRESET
	fail = false;
#endif
	if (fail != false)
		atf_tc_fail_nonfatal("ECONNRESET not defined");

	fail = true;

#ifdef EDEADLK
	fail = false;
#endif
	if (fail != false)
		atf_tc_fail_nonfatal("EDEADLK not defined");

	fail = true;

#ifdef EDESTADDRREQ
	fail = false;
#endif
	if (fail != false)
		atf_tc_fail_nonfatal("EDESTADDRREQ not defined");

	fail = true;

#ifdef EDOM
	fail = false;
#endif
	if (fail != false)
		atf_tc_fail_nonfatal("EDOM not defined");

	fail = true;

#ifdef EDQUOT
	fail = false;
#endif
	if (fail != false)
		atf_tc_fail_nonfatal("EDQUOT not defined");

	fail = true;

#ifdef EEXIST
	fail = false;
#endif
	if (fail != false)
		atf_tc_fail_nonfatal("EEXIST not defined");

	fail = true;

#ifdef EFAULT
	fail = false;
#endif
	if (fail != false)
		atf_tc_fail_nonfatal("EFAULT not defined");

	fail = true;

#ifdef EFBIG
	fail = false;
#endif
	if (fail != false)
		atf_tc_fail_nonfatal("EFBIG not defined");

	fail = true;

#ifdef EHOSTUNREACH
	fail = false;
#endif
	if (fail != false)
		atf_tc_fail_nonfatal("EHOSTUNREACH not defined");

	fail = true;

#ifdef EIDRM
	fail = false;
#endif
	if (fail != false)
		atf_tc_fail_nonfatal("EIDRM not defined");

	fail = true;

#ifdef EILSEQ
	fail = false;
#endif

	if (fail != false)
		atf_tc_fail_nonfatal("EILSEQ not defined");

	fail = true;

#ifdef EINPROGRESS
	fail = false;
#endif

	if (fail != false)
		atf_tc_fail_nonfatal("EINPROGRESS not defined");

	fail = true;

#ifdef EINTR
	fail = false;
#endif

	if (fail != false)
		atf_tc_fail_nonfatal("EINTR not defined");

	fail = true;

#ifdef EINVAL
	fail = false;
#endif

	if (fail != false)
		atf_tc_fail_nonfatal("EINVAL not defined");

	fail = true;

#ifdef EIO
	fail = false;
#endif

	if (fail != false)
		atf_tc_fail_nonfatal("EIO not defined");

	fail = true;

#ifdef EISCONN
	fail = false;
#endif

	if (fail != false)
		atf_tc_fail_nonfatal("EISCONN not defined");

	fail = true;

#ifdef EISDIR
	fail = false;
#endif

	if (fail != false)
		atf_tc_fail_nonfatal("EISDIR not defined");

	fail = true;

#ifdef ELOOP
	fail = false;
#endif

	if (fail != false)
		atf_tc_fail_nonfatal("ELOOP not defined");

	fail = true;

#ifdef EMFILE
	fail = false;
#endif

	if (fail != false)
		atf_tc_fail_nonfatal("EMFILE not defined");

	fail = true;

#ifdef EMLINK
	fail = false;
#endif

	if (fail != false)
		atf_tc_fail_nonfatal("EMLINK not defined");

	fail = true;

#ifdef EMSGSIZE
	fail = false;
#endif

	if (fail != false)
		atf_tc_fail_nonfatal("EMSGSIZE not defined");

	fail = true;

#ifdef EMULTIHOP
	fail = false;
#endif

	if (fail != false)
		atf_tc_fail_nonfatal("EMULTIHOP not defined");

	fail = true;

#ifdef ENAMETOOLONG
	fail = false;
#endif

	if (fail != false)
		atf_tc_fail_nonfatal("ENAMETOOLONG not defined");

	fail = true;

#ifdef ENETDOWN
	fail = false;
#endif

	if (fail != false)
		atf_tc_fail_nonfatal("ENETDOWN not defined");

	fail = true;

#ifdef ENETRESET
	fail = false;
#endif

	if (fail != false)
		atf_tc_fail_nonfatal("ENETRESET not defined");

	fail = true;

#ifdef ENETUNREACH
	fail = false;
#endif

	if (fail != false)
		atf_tc_fail_nonfatal("ENETUNREACH not defined");

	fail = true;

#ifdef ENFILE
	fail = false;
#endif

	if (fail != false)
		atf_tc_fail_nonfatal("ENFILE not defined");

	fail = true;

#ifdef ENOBUFS
	fail = false;
#endif

	if (fail != false)
		atf_tc_fail_nonfatal("ENOBUFS not defined");

	fail = true;

#ifdef ENODATA
	fail = false;
#endif

	if (fail != false)
		atf_tc_fail_nonfatal("ENODATA not defined");

	fail = true;

#ifdef ENODEV
	fail = false;
#endif

	if (fail != false)
		atf_tc_fail_nonfatal("ENODEV not defined");

	fail = true;

#ifdef ENOENT
	fail = false;
#endif

	if (fail != false)
		atf_tc_fail_nonfatal("ENOENT not defined");

	fail = true;

#ifdef ENOEXEC
	fail = false;
#endif

	if (fail != false)
		atf_tc_fail_nonfatal("ENOEXEC not defined");

	fail = true;

#ifdef ENOLCK
	fail = false;
#endif

	if (fail != false)
		atf_tc_fail_nonfatal("ENOLCK not defined");

	fail = true;

#ifdef ENOLINK
	fail = false;
#endif

	if (fail != false)
		atf_tc_fail_nonfatal("ENOLINK not defined");

	fail = true;

#ifdef ENOMEM
	fail = false;
#endif

	if (fail != false)
		atf_tc_fail_nonfatal("ENOMEM not defined");

	fail = true;

#ifdef ENOMSG
	fail = false;
#endif

	if (fail != false)
		atf_tc_fail_nonfatal("ENOMSG not defined");

	fail = true;

#ifdef ENOPROTOOPT
	fail = false;
#endif

	if (fail != false)
		atf_tc_fail_nonfatal("ENOPROTOOPT not defined");

	fail = true;

#ifdef ENOSPC
	fail = false;
#endif

	if (fail != false)
		atf_tc_fail_nonfatal("ENOSPC not defined");

	fail = true;

#ifdef ENOSR
	fail = false;
#endif

	if (fail != false)
		atf_tc_fail_nonfatal("ENOSR not defined");

	fail = true;

#ifdef ENOSTR
	fail = false;
#endif

	if (fail != false)
		atf_tc_fail_nonfatal("ENOSTR not defined");

	fail = true;

#ifdef ENOSYS
	fail = false;
#endif

	if (fail != false)
		atf_tc_fail_nonfatal("ENOSYS not defined");

	fail = true;

#ifdef ENOTCONN
	fail = false;
#endif

	if (fail != false)
		atf_tc_fail_nonfatal("ENOTCONN not defined");

	fail = true;

#ifdef ENOTDIR
	fail = false;
#endif

	if (fail != false)
		atf_tc_fail_nonfatal("ENOTDIR not defined");

	fail = true;

#ifdef ENOTEMPTY
	fail = false;
#endif

	if (fail != false)
		atf_tc_fail_nonfatal("ENOTEMPTY not defined");

	fail = true;

#ifdef ENOTRECOVERABLE
	fail = false;
#endif

	if (fail != false)
		atf_tc_fail_nonfatal("ENOTRECOVERABLE not defined");

	fail = true;

#ifdef ENOTSOCK
	fail = false;
#endif

	if (fail != false)
		atf_tc_fail_nonfatal("ENOTSOCK not defined");

	fail = true;

#ifdef ENOTSUP
	fail = false;
#endif

	if (fail != false)
		atf_tc_fail_nonfatal("ENOTSUP not defined");

	fail = true;

#ifdef ENOTTY
	fail = false;
#endif

	if (fail != false)
		atf_tc_fail_nonfatal("ENOTTY not defined");

	fail = true;

#ifdef ENXIO
	fail = false;
#endif

	if (fail != false)
		atf_tc_fail_nonfatal("ENXIO not defined");

	fail = true;

#ifdef EOPNOTSUPP
	fail = false;
#endif

	if (fail != false)
		atf_tc_fail_nonfatal("EOPNOTSUPP not defined");

	fail = true;

#ifdef EOVERFLOW
	fail = false;
#endif

	if (fail != false)
		atf_tc_fail_nonfatal("EOVERFLOW not defined");

	fail = true;

#ifdef EOWNERDEAD
	fail = false;
#endif

	if (fail != false)
		atf_tc_fail_nonfatal("EOWNERDEAD not defined");

	fail = true;

#ifdef EPERM
	fail = false;
#endif

	if (fail != false)
		atf_tc_fail_nonfatal("EPERM not defined");

	fail = true;

#ifdef EPIPE
	fail = false;
#endif

	if (fail != false)
		atf_tc_fail_nonfatal("EPIPE not defined");

	fail = true;

#ifdef EPROTO
	fail = false;
#endif

	if (fail != false)
		atf_tc_fail_nonfatal("EPROTO not defined");

	fail = true;

#ifdef EPROTONOSUPPORT
	fail = false;
#endif

	if (fail != false)
		atf_tc_fail_nonfatal("EPROTONOSUPPORT not defined");

	fail = true;

#ifdef EPROTOTYPE
	fail = false;
#endif

	if (fail != false)
		atf_tc_fail_nonfatal("EPROTOTYPE not defined");

	fail = true;

#ifdef ERANGE
	fail = false;
#endif

	if (fail != false)
		atf_tc_fail_nonfatal("ERANGE not defined");

	fail = true;

#ifdef EROFS
	fail = false;
#endif

	if (fail != false)
		atf_tc_fail_nonfatal("EROFS not defined");

	fail = true;

#ifdef ESPIPE
	fail = false;
#endif

	if (fail != false)
		atf_tc_fail_nonfatal("ESPIPE not defined");

	fail = true;

#ifdef ESRCH
	fail = false;
#endif

	if (fail != false)
		atf_tc_fail_nonfatal("ESRCH not defined");

	fail = true;

#ifdef ESTALE
	fail = false;
#endif

	if (fail != false)
		atf_tc_fail_nonfatal("ESTALE not defined");

	fail = true;

#ifdef ETIME
	fail = false;
#endif

	if (fail != false)
		atf_tc_fail_nonfatal("ETIME not defined");

	fail = true;

#ifdef ETIMEDOUT
	fail = false;
#endif

	if (fail != false)
		atf_tc_fail_nonfatal("ETIMEDOUT not defined");

	fail = true;

#ifdef ETXTBSY
	fail = false;
#endif

	if (fail != false)
		atf_tc_fail_nonfatal("ETXTBSY not defined");

	fail = true;

#ifdef EWOULDBLOCK
	fail = false;
#endif

	if (fail != false)
		atf_tc_fail_nonfatal("EWOULDBLOCK not defined");

	fail = true;

#ifdef EXDEV
	fail = false;
#endif

	if (fail != false)
		atf_tc_fail_nonfatal("EXDEV not defined");
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, errno_constants);

	return atf_no_error();
}
