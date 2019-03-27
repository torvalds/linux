/* $NetBSD: t_system.c,v 1.1 2011/09/11 10:32:23 jruoho Exp $ */

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
__RCSID("$NetBSD: t_system.c,v 1.1 2011/09/11 10:32:23 jruoho Exp $");

#include <atf-c.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char *path = "system";

ATF_TC_WITH_CLEANUP(system_basic);
ATF_TC_HEAD(system_basic, tc)
{
	atf_tc_set_md_var(tc, "descr", "A basic test of system(3)");
}

ATF_TC_BODY(system_basic, tc)
{
	char buf[23];
	int fd, i = 2;

	ATF_REQUIRE(system("/bin/echo -n > system") == 0);

	while (i >= 0) {
		ATF_REQUIRE(system("/bin/echo -n garbage >> system") == 0);
		i--;
	}

	fd = open(path, O_RDONLY);
	ATF_REQUIRE(fd >= 0);

	(void)memset(buf, '\0', sizeof(buf));

	ATF_REQUIRE(read(fd, buf, 21) == 21);
	ATF_REQUIRE(strcmp(buf, "garbagegarbagegarbage") == 0);

	ATF_REQUIRE(close(fd) == 0);
	ATF_REQUIRE(unlink(path) == 0);
}

ATF_TC_CLEANUP(system_basic, tc)
{
	(void)unlink(path);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, system_basic);

	return atf_no_error();
}
