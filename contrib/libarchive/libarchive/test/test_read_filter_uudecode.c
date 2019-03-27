/*-
 * Copyright (c) 2003-2007 Tim Kientzle
 * Copyright (c) 2009-2011 Michihiro NAKAJIMA
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include "test.h"
__FBSDID("$FreeBSD: head/lib/libarchive/test/test_read_uu.c 201248 2009-12-30 06:12:03Z kientzle $");

static const char archive[] = {
"begin 644 test_read_uu.Z\n"
"M'YV0+@`('$BPH,&#\"!,J7,BP(4(8$&_4J`$\"`,08$F%4O)AQ(\\2/(#7&@#%C\n"
"M!@T8-##.L`$\"QL@:-F(``%'#H<V;.'/J!%!G#ITP<BS\"H).FS<Z$1(T>/1A2\n"
"IHU\"0%9=*G4JUJM6K6+-JW<JUJ]>O8,.*'4NVK-FS:-.J7<NVK=NW9P$`\n"
"`\n"
"end\n"
};

static const char archive64[] = {
"begin-base64 644 test_read_uu.Z\n"
"H52QLgAIHEiwoMGDCBMqXMiwIUIYEG/UqAECAMQYEmFUvJhxI8SPIDXGgDFjBg0YNDDOsAECxsga\n"
"NmIAAFHDoc2bOHPqBFBnDp0wcizCoJOmzc6ERI0ePRhSo1CQFZdKnUq1qtWrWLNq3cq1q9evYMOK\n"
"HUu2rNmzaNOqXcu2rdu3ZwE=\n"
"====\n"
};

static const char extradata[] = {
"From uudecode@libarchive Mon Jun  2 03:03:31 2008\n"
"Return-Path: <uudecode@libarchive>\n"
"Received: from libarchive (localhost [127.0.0.1])\n"
"        by libarchive (8.14.2/8.14.2) with ESMTP id m5233UT1006448\n"
"        for <uudecode@libarchive>; Mon, 2 Jun 2008 03:03:31 GMT\n"
"        (envelope-from uudecode@libarchive)\n"
"Received: (from uudecode@localhost)\n"
"        by libarchive (8.14.2/8.14.2/Submit) id m5233U3e006406\n"
"        for uudecode; Mon, 2 Jun 2008 03:03:30 GMT\n"
"        (envelope-from root)\n"
"Date: Mon, 2 Jun 2008 03:03:30 GMT\n"
"From: Libarchive Test <uudecode@libarchive>\n"
"Message-Id: <200806020303.m5233U3e006406@libarchive>\n"
"To: uudecode@libarchive\n"
"Subject: Libarchive uudecode test\n"
"\n"
"* Redistribution and use in source and binary forms, with or without\n"
"* modification, are permitted provided that the following conditions\n"
"* are met:\n"
"\n"
"01234567890abcdeghijklmnopqrstuvwxyz\n"
"01234567890ABCEFGHIJKLMNOPQRSTUVWXYZ\n"
"\n"
};

static void
test_read_uu_sub(const char *uudata, size_t uusize, int no_nl)
{
	struct archive_entry *ae;
	struct archive *a;
	char *buff;
	char extradata_no_nl[sizeof(extradata)];
	const char *extradata_ptr;
	int extra;
	size_t size;

	if (no_nl) {
		/* Remove '\n' from extra data to make a very long line. */
		char *p;
		memcpy(extradata_no_nl, extradata, sizeof(extradata));
		extradata_ptr = extradata_no_nl;
		for (p = extradata_no_nl;
		    *p && (p = strchr(p, '\n')) != NULL; p++)
			*p = ' ';/* Replace '\n' with ' ' a space character. */
	} else
		extradata_ptr = extradata;

	assert(NULL != (buff = malloc(uusize + 1024 * 1024)));
	if (buff == NULL)
		return;
	for (extra = 0; extra <= 64; extra = extra==0?1:extra*2) {
		char *p = buff;

		size = extra * 1024;
		/* Add extra text size of which is from 1K bytes to
		 * 64Kbytes before uuencoded data. */
		while (size) {
			if (size > sizeof(extradata)-1) {
				memcpy(p, extradata_ptr, sizeof(extradata)-1);
				p += sizeof(extradata)-1;
				size -= sizeof(extradata)-1;
			} else {
				memcpy(p, extradata_ptr, size-1);
				p += size-1;
				*p++ = '\n';/* the last of extra text must have
					     * '\n' character. */
				break;
			}
		}
		memcpy(p, uudata, uusize);
		size = extra * 1024 + uusize;

		assert((a = archive_read_new()) != NULL);
		assertEqualIntA(a, ARCHIVE_OK,
		    archive_read_support_filter_all(a));
		assertEqualIntA(a, ARCHIVE_OK,
		    archive_read_support_format_all(a));
		assertEqualIntA(a, ARCHIVE_OK,
		    read_open_memory(a, buff, size, 2));
		assertEqualIntA(a, ARCHIVE_OK,
		    archive_read_next_header(a, &ae));
		failure("archive_filter_name(a, 0)=\"%s\""
		    "extra %d, NL %d",
		    archive_filter_name(a, 0), extra, !no_nl);
		assertEqualInt(archive_filter_code(a, 0),
		    ARCHIVE_FILTER_COMPRESS);
		failure("archive_format_name(a)=\"%s\""
		    "extra %d, NL %d",
		    archive_format_name(a), extra, !no_nl);
		assertEqualInt(archive_format(a),
		    ARCHIVE_FORMAT_TAR_USTAR);
		assertEqualIntA(a, ARCHIVE_OK, archive_read_close(a));
		assertEqualInt(ARCHIVE_OK, archive_read_free(a));
	}

	/* UUdecode bidder shouldn't scan too much data; make sure it
	 * fails if we put 512k of data before the start. */
	size = 512 * 1024;
	for (extra = 0; (size_t)extra < size; ++extra)
		buff[extra + 1024] = buff[extra];
	buff[size - 1] = '\n';
	memcpy(buff + size, uudata, uusize);
	size += uusize;
	assert((a = archive_read_new()) != NULL);
	assertEqualIntA(a, ARCHIVE_OK,
	    archive_read_support_filter_all(a));
	assertEqualIntA(a, ARCHIVE_OK,
	    archive_read_support_format_all(a));
	assertEqualIntA(a, ARCHIVE_FATAL,
	    read_open_memory(a, buff, size, 2));
	assertEqualInt(ARCHIVE_OK, archive_read_free(a));

	free(buff);
}

DEFINE_TEST(test_read_filter_uudecode)
{
	/* Read the traditional uuencoded data. */
	test_read_uu_sub(archive, sizeof(archive)-1, 0);
	/* Read the traditional uuencoded data with very long line extra
	 * data in front of it. */
	test_read_uu_sub(archive, sizeof(archive)-1, 1);
}

DEFINE_TEST(test_read_filter_uudecode_base64)
{
	/* Read the Base64 uuencoded data. */
	test_read_uu_sub(archive64, sizeof(archive64)-1, 0);
	/* Read the Base64 uuencoded data with very long line extra data
	 * in front of it. */
	test_read_uu_sub(archive64, sizeof(archive64)-1, 1);
}
