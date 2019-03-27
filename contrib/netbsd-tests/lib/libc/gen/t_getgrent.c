/*	$NetBSD: t_getgrent.c,v 1.2 2011/05/11 19:06:45 njoly Exp $ */

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

/*
 * Copyright (c) 2009, Stathis Kamperis
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
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include <sys/cdefs.h>
__RCSID("$NetBSD: t_getgrent.c,v 1.2 2011/05/11 19:06:45 njoly Exp $");

#include <sys/wait.h>

#include <atf-c.h>
#include <grp.h>
#include <stdlib.h>
#include <unistd.h>

ATF_TC(getgrent_loop);
ATF_TC_HEAD(getgrent_loop, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test sequential getgrent(2)");
}

ATF_TC_BODY(getgrent_loop, tc)
{
	struct group *gr;
	size_t i, j;

	/*
	 * Loop over the group database. The first
	 * call returns the first entry and subsequent
	 * calls return the rest of the entries.
	 */
	i = j = 0;

	while((gr = getgrent()) != NULL)
		i++;

	/*
	 * Rewind the database to the beginning
	 * and loop over again until the end.
	 */
	setgrent();

        while((gr = getgrent()) != NULL)
		j++;

	if (i != j)
		atf_tc_fail("sequential getgrent(3) failed");

	/*
	 * Close the database and reopen it.
	 * The getgrent(3) call should always
	 * automatically rewind the database.
	 */
	endgrent();

        j = 0;

        while((gr = getgrent()) != NULL)
                j++;

	if (i != j)
		atf_tc_fail("getgrent(3) did not rewind");
}

ATF_TC(getgrent_setgid);
ATF_TC_HEAD(getgrent_setgid, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test consistency of the group db");
	atf_tc_set_md_var(tc, "require.user", "root");
}

ATF_TC_BODY(getgrent_setgid, tc)
{
	struct group *gr, *gr1, *gr2;
	int rv, sta;
	pid_t pid;

	/*
	 * Verify that the database is consistent.
	 *
	 * Note that because of the static buffers
	 * used by getgrent(3), fork(2) is required,
	 * even without the setgid(2) check.
	 */
        while((gr = getgrent()) != NULL) {

		pid = fork();
		ATF_REQUIRE(pid >= 0);

		if (pid == 0) {

			gr1 = getgrgid(gr->gr_gid);

			if (gr1 == NULL)
				_exit(EXIT_FAILURE);

			gr2 = getgrnam(gr->gr_name);

			if (gr2 == NULL)
				_exit(EXIT_FAILURE);

			rv = setgid(gr->gr_gid);

			if (rv != 0)
				_exit(EXIT_FAILURE);

			_exit(EXIT_SUCCESS);
		}

		(void)wait(&sta);

		if (WIFEXITED(sta) == 0 || WEXITSTATUS(sta) != EXIT_SUCCESS)
			goto fail;
	}

	return;

fail:
	atf_tc_fail("group database is inconsistent");
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, getgrent_loop);
	ATF_TP_ADD_TC(tp, getgrent_setgid);

	return atf_no_error();
}
