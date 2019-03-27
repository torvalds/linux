/*
 * Based on the OpenBSD test
 * Copyright (c) 2011 Martin Pieuchot <mpi@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/cdefs.h>
__RCSID("$NetBSD: t_open_memstream.c,v 1.2 2014/10/19 11:17:43 justin Exp $");

#include <atf-c.h>
#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

ATF_TC(test_open_memstream);
ATF_TC_HEAD(test_open_memstream, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test open_memstream functionality");
}

#define OFFSET 16384

const char start[] = "start";
const char hello[] = "hello";

ATF_TC_BODY(test_open_memstream, tc)
{
	FILE	*fp;
	char	*buf = (char *)0xff;
	size_t	 size = 0;
	off_t	 off;
	int	 i;

	fp = open_memstream(&buf, &size);
	ATF_REQUIRE(fp != NULL);

	off = ftello(fp);
	ATF_CHECK(off == 0);

	ATF_CHECK(fflush(fp) == 0);
	ATF_CHECK(size == 0);
	ATF_CHECK(buf != (char *)0xff);
	ATF_CHECK(fseek(fp, -6, SEEK_SET) == -1);
	ATF_CHECK(fseek(fp, OFFSET, SEEK_SET) == 0);
	ATF_CHECK(fprintf(fp, hello) != EOF);
	ATF_CHECK(fflush(fp) != EOF);
	ATF_CHECK(size == OFFSET + sizeof(hello)-1);
	ATF_CHECK(fseek(fp, 0, SEEK_SET) == 0);
	ATF_CHECK(fprintf(fp, start) != EOF);
	ATF_CHECK(fflush(fp) != EOF);
	ATF_CHECK(size == sizeof(start)-1);

	/* Needed for sparse files */
	ATF_CHECK(strncmp(buf, start, sizeof(start)-1) == 0);
	for (i = sizeof(start)-1; i < OFFSET; i++)
		ATF_CHECK(buf[i] == '\0');

	ATF_CHECK(memcmp(buf + OFFSET, hello, sizeof(hello)-1) == 0);

	/* verify that simply seeking past the end doesn't increase the size */
	ATF_CHECK(fseek(fp, 100, SEEK_END) == 0);
	ATF_CHECK(fflush(fp) != EOF);
	ATF_CHECK(size == OFFSET + sizeof(hello)-1);
	ATF_CHECK(fseek(fp, 8, SEEK_SET) == 0);
	ATF_CHECK(ftell(fp) == 8);

	/* Try to seek backward */
	ATF_CHECK(fseek(fp, -1, SEEK_CUR) == 0);
	ATF_CHECK(ftell(fp) == 7);
	ATF_CHECK(fseek(fp, 5, SEEK_CUR) == 0);
	ATF_CHECK(fclose(fp) != EOF);
	ATF_CHECK(size == 12);

	free(buf);
}

ATF_TP_ADD_TCS(tp)
{

        ATF_TP_ADD_TC(tp, test_open_memstream);

        return atf_no_error();
}
