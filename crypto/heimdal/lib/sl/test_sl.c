/*
 * Copyright (c) 2006 Kungliga Tekniska Högskolan
 * (Royal Institute of Technology, Stockholm, Sweden).
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of KTH nor the names of its contributors may be
 *    used to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY KTH AND ITS CONTRIBUTORS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL KTH OR ITS CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. */

#include "sl_locl.h"

struct {
    int ok;
    const char *line;
    int argc;
    const char *argv[4];
} lines[] = {
    { 1, "", 1, { "" } },
    { 1, "foo", 1, { "foo" } },
    { 1, "foo bar", 2, { "foo", "bar" }},
    { 1, "foo bar baz", 3, { "foo", "bar", "baz" }},
    { 1, "foobar baz", 2, { "foobar", "baz" }},
    { 1, " foo", 1, { "foo" } },
    { 1, "foo   ", 1, { "foo" } },
    { 1, " foo  ", 1, { "foo" } },
    { 1, " foo  bar", 2, { "foo", "bar" } },
    { 1, "foo\\ bar", 1, { "foo bar" } },
    { 1, "\"foo bar\"", 1, { "foo bar" } },
    { 1, "\"foo\\ bar\"", 1, { "foo bar" } },
    { 1, "\"foo\\\" bar\"", 1, { "foo\" bar" } },
    { 1, "\"\"f\"\"oo\"\"", 1, { "foo" } },
    { 1, "\"foobar\"baz", 1, { "foobarbaz" }},
    { 1, "foo\tbar baz", 3, { "foo", "bar", "baz" }},
    { 1, "\"foo bar\" baz", 2, { "foo bar", "baz" }},
    { 1, "\"foo bar baz\"", 1, { "foo bar baz" }},
    { 1, "\\\"foo bar baz", 3, { "\"foo", "bar", "baz" }},
    { 1, "\\ foo bar baz", 3, { " foo", "bar", "baz" }},
    { 0, "\\", 0, { "" }},
    { 0, "\"", 0, { "" }}
};

int
main(int argc, char **argv)
{
    int ret, i;

    for (i = 0; i < sizeof(lines)/sizeof(lines[0]); i++) {
	int j, rargc = 0;
	char **rargv = NULL;
	char *buf = strdup(lines[i].line);

	ret = sl_make_argv(buf, &rargc, &rargv);
	if (ret) {
	    if (!lines[i].ok)
		goto next;
	    errx(1, "sl_make_argv test %d failed", i);
	} else if (!lines[i].ok)
	    errx(1, "sl_make_argv passed test %d when it shouldn't", i);
	if (rargc != lines[i].argc)
	    errx(1, "result argc (%d) != should be argc (%d) for test %d",
		 rargc, lines[i].argc, i);
	for (j = 0; j < rargc; j++)
	    if (strcmp(rargv[j], lines[i].argv[j]) != 0)
		errx(1, "result argv (%s) != should be argv (%s) for test %d",
		     rargv[j], lines[i].argv[j], i);
    next:
	free(buf);
	free(rargv);
    }

    return 0;
}
