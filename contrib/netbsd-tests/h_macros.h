/* $NetBSD: h_macros.h,v 1.13 2016/08/20 15:49:08 christos Exp $ */

/*-
 * Copyright (c) 2008, 2009 The NetBSD Foundation, Inc.
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

#ifndef SRC_TESTS_H_MACROS_H_
#define SRC_TESTS_H_MACROS_H_

#include <sys/types.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <atf-c.h>

#define REQUIRE_LIBC(x, v) \
	ATF_REQUIRE_MSG((x) != (v), "%s: %s", #x, strerror(errno))

#define CHECK_LIBC(x, v) \
	ATF_CHECK_MSG((x) != (v), "%s: %s", #x, strerror(errno))

#define RL(x) REQUIRE_LIBC(x, -1)
#define RLF(x, fmt, arg) \
	ATF_CHECK_MSG((x) != -1, "%s [" fmt "]: %s", #x, arg, strerror(errno))
#define RZ(x)								\
do {									\
	int RZ_rv = x;							\
	ATF_REQUIRE_MSG(RZ_rv == 0, "%s: %s", #x, strerror(RZ_rv));	\
} while (/*CONSTCOND*/0)

__dead static __inline __printflike(1, 2) void
atf_tc_fail_errno(const char *fmt, ...)
{
	va_list ap;
	char buf[1024];
	int sverrno = errno;

	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);

	strlcat(buf, ": ", sizeof(buf));
	strlcat(buf, strerror(sverrno), sizeof(buf));

	atf_tc_fail("%s", buf);
}

static __inline void
tests_makegarbage(void *space, size_t len)
{
	uint16_t *sb = space;
	uint16_t randval;

	while (len >= sizeof(randval)) {
		*sb++ = (uint16_t)random();
		len -= sizeof(*sb);
	}
	randval = (uint16_t)random();
	memcpy(sb, &randval, len);
}

#endif
