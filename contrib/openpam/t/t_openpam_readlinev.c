/*-
 * Copyright (c) 2012-2017 Dag-Erling Smørgrav
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
 * 3. The name of the author may not be used to endorse or promote
 *    products derived from this software without specific prior written
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $OpenPAM: t_openpam_readlinev.c 938 2017-04-30 21:34:42Z des $
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <err.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cryb/test.h>

#include <security/pam_appl.h>
#include <security/openpam.h>

#include "openpam_impl.h"

#define T_FUNC(n, d)							\
	static const char *t_ ## n ## _desc = d;			\
	static int t_ ## n ## _func(OPENPAM_UNUSED(char **desc),	\
	    OPENPAM_UNUSED(void *arg))

#define T(n)								\
	t_add_test(&t_ ## n ## _func, NULL, "%s", t_ ## n ## _desc)

/*
 * Read a line from the temp file and verify that the result matches our
 * expectations: whether a line was read at all, how many and which words
 * it contained, how many lines were read (in case of quoted or escaped
 * newlines) and whether we reached the end of the file.
 */
static int
orlv_expect(struct t_file *tf, const char **expectedv, int lines, int eof)
{
	int expectedc, gotc, i, lineno = 0;
	char **gotv;
	int ret;

	ret = 1;
	expectedc = 0;
	if (expectedv != NULL)
		while (expectedv[expectedc] != NULL)
			++expectedc;
	gotv = openpam_readlinev(tf->file, &lineno, &gotc);
	if (t_ferror(tf))
		err(1, "%s(): %s", __func__, tf->name);
	if (expectedv != NULL && gotv == NULL) {
		t_printv("expected %d words, got nothing\n", expectedc);
		ret = 0;
	} else if (expectedv == NULL && gotv != NULL) {
		t_printv("expected nothing, got %d words\n", gotc);
		ret = 0;
	} else if (expectedv != NULL && gotv != NULL) {
		if (expectedc != gotc) {
			t_printv("expected %d words, got %d\n",
			    expectedc, gotc);
			ret = 0;
		}
		for (i = 0; i < gotc; ++i) {
			if (strcmp(expectedv[i], gotv[i]) != 0) {
				t_printv("word %d: expected <<%s>>, "
				    "got <<%s>>\n", i, expectedv[i], gotv[i]);
				ret = 0;
			}
		}
	}
	FREEV(gotc, gotv);
	if (lineno != lines) {
		t_printv("expected to advance %d lines, advanced %d lines\n",
		    lines, lineno);
		ret = 0;
	}
	if (eof && !t_feof(tf)) {
		t_printv("expected EOF, but didn't get it\n");
		ret = 0;
	} else if (!eof && t_feof(tf)) {
		t_printv("didn't expect EOF, but got it anyway\n");
		ret = 0;
	}
	return (ret);
}


/***************************************************************************
 * Commonly-used lines
 */

static const char *empty[] = {
	NULL
};

static const char *hello[] = {
	"hello",
	NULL
};

static const char *hello_world[] = {
	"hello",
	"world",
	NULL
};

static const char *numbers[] = {
	"zero", "one", "two", "three", "four", "five", "six", "seven",
	"eight", "nine", "ten", "eleven", "twelve", "thirteen", "fourteen",
	"fifteen", "sixteen", "seventeen", "nineteen", "twenty",
	"twenty-one", "twenty-two", "twenty-three", "twenty-four",
	"twenty-five", "twenty-six", "twenty-seven", "twenty-eight",
	"twenty-nine", "thirty", "thirty-one", "thirty-two", "thirty-three",
	"thirty-four", "thirty-five", "thirty-six", "thirty-seven",
	"thirty-eight", "thirty-nine", "fourty", "fourty-one", "fourty-two",
	"fourty-three", "fourty-four", "fourty-five", "fourty-six",
	"fourty-seven", "fourty-eight", "fourty-nine", "fifty", "fifty-one",
	"fifty-two", "fifty-three", "fifty-four", "fifty-five", "fifty-six",
	"fifty-seven", "fifty-eight", "fifty-nine", "sixty", "sixty-one",
	"sixty-two", "sixty-three",
	NULL
};


/***************************************************************************
 * Lines without words
 */

T_FUNC(empty_input, "empty input")
{
	struct t_file *tf;
	int ret;

	tf = t_fopen(NULL);
	ret = orlv_expect(tf, NULL, 0 /*lines*/, 1 /*eof*/);
	t_fclose(tf);
	return (ret);
}

T_FUNC(empty_line, "empty line")
{
	struct t_file *tf;
	int ret;

	tf = t_fopen(NULL);
	t_fprintf(tf, "\n");
	t_frewind(tf);
	ret = orlv_expect(tf, empty, 1 /*lines*/, 0 /*eof*/);
	t_fclose(tf);
	return (ret);
}

T_FUNC(unterminated_empty_line, "unterminated empty line")
{
	struct t_file *tf;
	int ret;

	tf = t_fopen(NULL);
	t_fprintf(tf, " ");
	t_frewind(tf);
	ret = orlv_expect(tf, NULL, 0 /*lines*/, 1 /*eof*/);
	t_fclose(tf);
	return (ret);
}

T_FUNC(whitespace, "whitespace")
{
	struct t_file *tf;
	int ret;

	tf = t_fopen(NULL);
	t_fprintf(tf, " \n");
	t_frewind(tf);
	ret = orlv_expect(tf, empty, 1 /*lines*/, 0 /*eof*/);
	t_fclose(tf);
	return (ret);
}

T_FUNC(comment, "comment")
{
	struct t_file *tf;
	int ret;

	tf = t_fopen(NULL);
	t_fprintf(tf, "# comment\n");
	t_frewind(tf);
	ret = orlv_expect(tf, empty, 1 /*lines*/, 0 /*eof*/);
	t_fclose(tf);
	return (ret);
}

T_FUNC(whitespace_before_comment, "whitespace before comment")
{
	struct t_file *tf;
	int ret;

	tf = t_fopen(NULL);
	t_fprintf(tf, " # comment\n");
	t_frewind(tf);
	ret = orlv_expect(tf, empty, 1 /*lines*/, 0 /*eof*/);
	t_fclose(tf);
	return (ret);
}

T_FUNC(line_continuation_within_whitespace, "line continuation within whitespace")
{
	struct t_file *tf;
	int ret;

	tf = t_fopen(NULL);
	t_fprintf(tf, "%s \\\n %s\n", hello_world[0], hello_world[1]);
	t_frewind(tf);
	ret = orlv_expect(tf, hello_world, 2 /*lines*/, 0 /*eof*/) &&
	    orlv_expect(tf, NULL, 0 /*lines*/, 1 /*eof*/);
	t_fclose(tf);
	return (ret);
}


/***************************************************************************
 * Simple words
 */

T_FUNC(one_word, "one word")
{
	struct t_file *tf;
	int ret;

	tf = t_fopen(NULL);
	t_fprintf(tf, "hello\n");
	t_frewind(tf);
	ret = orlv_expect(tf, hello, 1 /*lines*/, 0 /*eof*/);
	t_fclose(tf);
	return (ret);
}

T_FUNC(two_words, "two words")
{
	struct t_file *tf;
	int ret;

	tf = t_fopen(NULL);
	t_fprintf(tf, "hello world\n");
	t_frewind(tf);
	ret = orlv_expect(tf, hello_world, 1 /*lines*/, 0 /*eof*/);
	t_fclose(tf);
	return (ret);
}

T_FUNC(many_words, "many words")
{
	struct t_file *tf;
	const char **word;
	int ret;

	tf = t_fopen(NULL);
	for (word = numbers; *word; ++word)
		t_fprintf(tf, " %s", *word);
	t_fprintf(tf, "\n");
	t_frewind(tf);
	ret = orlv_expect(tf, numbers, 1 /*lines*/, 0 /*eof*/);
	t_fclose(tf);
	return (ret);
}

T_FUNC(unterminated_line, "unterminated line")
{
	struct t_file *tf;
	int ret;

	tf = t_fopen(NULL);
	t_fprintf(tf, "hello world");
	t_frewind(tf);
	ret = orlv_expect(tf, hello_world, 0 /*lines*/, 1 /*eof*/);
	t_fclose(tf);
	return (ret);
}


/***************************************************************************
 * Boilerplate
 */

static int
t_prepare(int argc, char *argv[])
{

	(void)argc;
	(void)argv;

	T(empty_input);
	T(empty_line);
	T(unterminated_empty_line);
	T(whitespace);
	T(comment);
	T(whitespace_before_comment);
	T(line_continuation_within_whitespace);

	T(one_word);
	T(two_words);
	T(many_words);
	T(unterminated_line);

	return (0);
}

int
main(int argc, char *argv[])
{

	t_main(t_prepare, NULL, argc, argv);
}
