/* $NetBSD: t_setdomainname.c,v 1.2 2012/03/25 08:17:54 joerg Exp $ */

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
__RCSID("$NetBSD: t_setdomainname.c,v 1.2 2012/03/25 08:17:54 joerg Exp $");

#include <sys/param.h>

#include <atf-c.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#define	DOMAIN_BACKUP_FILE "domain.bak"

static const char domains[][MAXHOSTNAMELEN] = {
	"1234567890",
	"abcdefghijklmnopqrst",
	"!#\xa4%&/(..xasS812=!=!(I(!;X;;X.as.dasa=?;,..<>|**^\xa8",
	"--------------------------------------------------------------------"
};

static void
backup_domain(void)
{
	char domain[MAXHOSTNAMELEN];
	int fd;
	size_t l;
	ssize_t r,n = 0;

	memset(domain, 0, sizeof(domain));

	ATF_REQUIRE_EQ(0, getdomainname(domain, sizeof(domain)));
	l = strnlen(domain, MAXHOSTNAMELEN);
	fd = open(DOMAIN_BACKUP_FILE, O_WRONLY | O_CREAT | O_EXCL, 0644);
	ATF_REQUIRE(fd >= 0);
	while ((r = write(fd, domain + n, l - n)) > 0)
		n += r;
	ATF_REQUIRE_EQ(0, r);
	close(fd);
}

static void
restore_domain(void)
{
	char domain[MAXHOSTNAMELEN];
	int fd;
	ssize_t r, n = 0;

	memset(domain, 0, sizeof(domain));
	if ((fd = open(DOMAIN_BACKUP_FILE, O_RDONLY)) < 0)
		err(1, "open");
	while ((r = read(fd, domain + n, sizeof(domain) - n)) > 0)
		n += r;
	if (r < 0)
		err(1, "read");
	if (setdomainname(domain, n) != 0)
		err(1, "setdomainname");
	close(fd);
}

ATF_TC_WITH_CLEANUP(setdomainname_basic);
ATF_TC_HEAD(setdomainname_basic, tc)
{
	atf_tc_set_md_var(tc, "descr", "A basic test of setdomainname(3)");
	atf_tc_set_md_var(tc, "require.user", "root");
}

ATF_TC_BODY(setdomainname_basic, tc)
{
	char name[MAXHOSTNAMELEN];
	size_t i;

	backup_domain();
	for (i = 0; i < __arraycount(domains); i++) {

		(void)memset(name, 0, sizeof(name));

#ifdef __FreeBSD__
		/*
		 * Sanity checks to ensure that the wrong invariant isn't being
		 * tested for per PR # 181127
		 */
		ATF_REQUIRE_EQ(sizeof(domains[i]), MAXHOSTNAMELEN);
		ATF_REQUIRE_EQ(sizeof(name), MAXHOSTNAMELEN);

		ATF_REQUIRE(setdomainname(domains[i],sizeof(domains[i]) -  1) == 0);
		ATF_REQUIRE(getdomainname(name, sizeof(name) - 1) == 0);
#else
		ATF_REQUIRE(setdomainname(domains[i],sizeof(domains[i])) == 0);
		ATF_REQUIRE(getdomainname(name, sizeof(name)) == 0);
#endif
		ATF_REQUIRE(strcmp(domains[i], name) == 0);
	}

}

ATF_TC_CLEANUP(setdomainname_basic, tc)
{
	restore_domain();
}

ATF_TC_WITH_CLEANUP(setdomainname_limit);
ATF_TC_HEAD(setdomainname_limit, tc)
{
	atf_tc_set_md_var(tc, "descr", "Too long domain name errors out?");
	atf_tc_set_md_var(tc, "require.user", "root");
}

ATF_TC_BODY(setdomainname_limit, tc)
{
	char name[MAXHOSTNAMELEN + 1];

	(void)memset(name, 0, sizeof(name));
	backup_domain();

#ifdef __FreeBSD__
	ATF_REQUIRE(setdomainname(name, MAXHOSTNAMELEN - 1 ) == 0);
	ATF_REQUIRE(setdomainname(name, MAXHOSTNAMELEN) == -1);
#endif
	ATF_REQUIRE(setdomainname(name, sizeof(name)) == -1);
}

ATF_TC_CLEANUP(setdomainname_limit, tc)
{
	restore_domain();
}

ATF_TC(setdomainname_perm);
ATF_TC_HEAD(setdomainname_perm, tc)
{
	atf_tc_set_md_var(tc, "descr", "Can normal user set the domain name?");
	atf_tc_set_md_var(tc, "require.user", "unprivileged");
}

ATF_TC_BODY(setdomainname_perm, tc)
{
	char domain[MAXHOSTNAMELEN];

	memset(domain, 0, sizeof(domain));

	errno = 0;
	ATF_REQUIRE_ERRNO(EPERM, setdomainname(domain, sizeof(domain)) == -1);
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, setdomainname_basic);
	ATF_TP_ADD_TC(tp, setdomainname_limit);
	ATF_TP_ADD_TC(tp, setdomainname_perm);

	return atf_no_error();
}
