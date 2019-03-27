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
 * $OpenPAM: t_openpam_readword.c 938 2017-04-30 21:34:42Z des $
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <err.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <cryb/test.h>

#include <security/pam_appl.h>
#include <security/openpam.h>

#define T_FUNC(n, d)							\
	static const char *t_ ## n ## _desc = d;			\
	static int t_ ## n ## _func(OPENPAM_UNUSED(char **desc),	\
	    OPENPAM_UNUSED(void *arg))

#define T(n)								\
	t_add_test(&t_ ## n ## _func, NULL, "%s", t_ ## n ## _desc)

/*
 * Read a word from the temp file and verify that the result matches our
 * expectations: whether a word was read at all, how many lines were read
 * (in case of quoted or escaped newlines), whether we reached the end of
 * the file and whether we reached the end of the line.
 */
static int
orw_expect(struct t_file *tf, const char *expected, int lines, int eof, int eol)
{
	int ch, lineno = 0;
	char *got;
	size_t len;
	int ret;

	got = openpam_readword(tf->file, &lineno, &len);
	ret = 1;
	if (t_ferror(tf))
		err(1, "%s(): %s", __func__, tf->name);
	if (expected != NULL && got == NULL) {
		t_printv("expected <<%s>>, got nothing\n", expected);
		ret = 0;
	} else if (expected == NULL && got != NULL) {
		t_printv("expected nothing, got <<%s>>\n", got);
		ret = 0;
	} else if (expected != NULL && got != NULL && strcmp(expected, got) != 0) {
		t_printv("expected <<%s>>, got <<%s>>\n", expected, got);
		ret = 0;
	}
	free(got);
	if (lineno != lines) {
		t_printv("expected to advance %d lines, advanced %d lines\n",
		    lines, lineno);
		ret = 0;
	}
	if (eof && !t_feof(tf)) {
		t_printv("expected EOF, but didn't get it\n");
		ret = 0;
	}
	if (!eof && t_feof(tf)) {
		t_printv("didn't expect EOF, but got it anyway\n");
		ret = 0;
	}
	ch = fgetc(tf->file);
	if (t_ferror(tf))
		err(1, "%s(): %s", __func__, tf->name);
	if (eol && ch != '\n') {
		t_printv("expected EOL, but didn't get it\n");
		ret = 0;
	} else if (!eol && ch == '\n') {
		t_printv("didn't expect EOL, but got it anyway\n");
		ret = 0;
	}
	if (ch != EOF)
		ungetc(ch, tf->file);
	return (ret);
}


/***************************************************************************
 * Lines without words
 */

T_FUNC(empty_input, "empty input")
{
	struct t_file *tf;
	int ret;

	tf = t_fopen(NULL);
	ret = orw_expect(tf, NULL, 0 /*lines*/, 1 /*eof*/, 0 /*eol*/);
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
	ret = orw_expect(tf, NULL, 0 /*lines*/, 0 /*eof*/, 1 /*eol*/);
	t_fclose(tf);
	return (ret);
}

T_FUNC(unterminated_line, "unterminated line")
{
	struct t_file *tf;
	int ret;

	tf = t_fopen(NULL);
	t_fprintf(tf, " ");
	t_frewind(tf);
	ret = orw_expect(tf, NULL, 0 /*lines*/, 1 /*eof*/, 0 /*eol*/);
	t_fclose(tf);
	return (ret);
}

T_FUNC(single_whitespace, "single whitespace")
{
	struct t_file *tf;
	int ret;

	tf = t_fopen(NULL);
	t_fprintf(tf, " \n");
	t_frewind(tf);
	ret = orw_expect(tf, NULL, 0 /*lines*/, 0 /*eof*/, 1 /*eol*/);
	t_fclose(tf);
	return (ret);
}

T_FUNC(multiple_whitespace, "multiple whitespace")
{
	struct t_file *tf;
	int ret;

	tf = t_fopen(NULL);
	t_fprintf(tf, " \t\r\n");
	t_frewind(tf);
	ret = orw_expect(tf, NULL, 0 /*lines*/, 0 /*eof*/, 1 /*eol*/);
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
	ret = orw_expect(tf, NULL, 0 /*lines*/, 0 /*eof*/, 1 /*eol*/);
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
	ret = orw_expect(tf, NULL, 0 /*lines*/, 0 /*eof*/, 1 /*eol*/);
	t_fclose(tf);
	return (ret);
}

T_FUNC(single_quoted_comment, "single-quoted comment")
{
	struct t_file *tf;
	int ret;

	tf = t_fopen(NULL);
	t_fprintf(tf, " '# comment'\n");
	t_frewind(tf);
	ret = orw_expect(tf, "# comment", 0 /*lines*/, 0 /*eof*/, 1 /*eol*/);
	t_fclose(tf);
	return (ret);
}

T_FUNC(double_quoted_comment, "double-quoted comment")
{
	struct t_file *tf;
	int ret;

	tf = t_fopen(NULL);
	t_fprintf(tf, " \"# comment\"\n");
	t_frewind(tf);
	ret = orw_expect(tf, "# comment", 0 /*lines*/, 0 /*eof*/, 1 /*eol*/);
	t_fclose(tf);
	return (ret);
}

T_FUNC(comment_at_eof, "comment at end of file")
{
	struct t_file *tf;
	int ret;

	tf = t_fopen(NULL);
	t_fprintf(tf, "# comment");
	t_frewind(tf);
	ret = orw_expect(tf, NULL, 0 /*lines*/, 1 /*eof*/, 0 /*eol*/);
	t_fclose(tf);
	return (ret);
}


/***************************************************************************
 * Simple cases - no quotes or escapes
 */

T_FUNC(single_word, "single word")
{
	const char *word = "hello";
	struct t_file *tf;
	int ret;

	tf = t_fopen(NULL);
	t_fprintf(tf, "%s\n", word);
	t_frewind(tf);
	ret = orw_expect(tf, word, 0 /*lines*/, 0 /*eof*/, 1 /*eol*/);
	t_fclose(tf);
	return (ret);
}

T_FUNC(single_whitespace_before_word, "single whitespace before word")
{
	const char *word = "hello";
	struct t_file *tf;
	int ret;

	tf = t_fopen(NULL);
	t_fprintf(tf, " %s\n", word);
	t_frewind(tf);
	ret = orw_expect(tf, word, 0 /*lines*/, 0 /*eof*/, 1 /*eol*/);
	t_fclose(tf);
	return (ret);
}

T_FUNC(double_whitespace_before_word, "double whitespace before word")
{
	const char *word = "hello";
	struct t_file *tf;
	int ret;

	tf = t_fopen(NULL);
	t_fprintf(tf, "  %s\n", word);
	t_frewind(tf);
	ret = orw_expect(tf, word, 0 /*lines*/, 0 /*eof*/, 1 /*eol*/);
	t_fclose(tf);
	return (ret);
}

T_FUNC(single_whitespace_after_word, "single whitespace after word")
{
	const char *word = "hello";
	struct t_file *tf;
	int ret;

	tf = t_fopen(NULL);
	t_fprintf(tf, "%s \n", word);
	t_frewind(tf);
	ret = orw_expect(tf, word, 0 /*lines*/, 0 /*eof*/, 0 /*eol*/);
	t_fclose(tf);
	return (ret);
}

T_FUNC(double_whitespace_after_word, "double whitespace after word")
{
	const char *word = "hello";
	struct t_file *tf;
	int ret;

	tf = t_fopen(NULL);
	t_fprintf(tf, "%s  \n", word);
	t_frewind(tf);
	ret = orw_expect(tf, word, 0 /*lines*/, 0 /*eof*/, 0 /*eol*/);
	t_fclose(tf);
	return (ret);
}

T_FUNC(comment_after_word, "comment after word")
{
	const char *word = "hello";
	struct t_file *tf;
	int ret;

	tf = t_fopen(NULL);
	t_fprintf(tf, "%s # comment\n", word);
	t_frewind(tf);
	ret = orw_expect(tf, word, 0 /*lines*/, 0 /*eof*/, 0 /*eol*/) &&
	    orw_expect(tf, NULL, 0 /*lines*/, 0 /*eof*/, 1 /*eol*/);
	t_fclose(tf);
	return (ret);
}

T_FUNC(word_containing_hash, "word containing hash")
{
	const char *word = "hello#world";
	struct t_file *tf;
	int ret;

	tf = t_fopen(NULL);
	t_fprintf(tf, "%s\n", word);
	t_frewind(tf);
	ret = orw_expect(tf, word, 0 /*lines*/, 0 /*eof*/, 1 /*eol*/);
	t_fclose(tf);
	return (ret);
}

T_FUNC(two_words, "two words")
{
	const char *word[] = { "hello", "world" };
	struct t_file *tf;
	int ret;

	tf = t_fopen(NULL);
	t_fprintf(tf, "%s %s\n", word[0], word[1]);
	t_frewind(tf);
	ret = orw_expect(tf, word[0], 0 /*lines*/, 0 /*eof*/, 0 /*eol*/) &&
	    orw_expect(tf, word[1], 0 /*lines*/, 0 /*eof*/, 1 /*eol*/);
	t_fclose(tf);
	return (ret);
}


/***************************************************************************
 * Escapes
 */

T_FUNC(naked_escape, "naked escape")
{
	struct t_file *tf;
	int ret;

	tf = t_fopen(NULL);
	t_fprintf(tf, "\\");
	t_frewind(tf);
	ret = orw_expect(tf, NULL, 0 /*lines*/, 1 /*eof*/, 0 /*eol*/);
	t_fclose(tf);
	return (ret);
}

T_FUNC(escaped_escape, "escaped escape")
{
	struct t_file *tf;
	int ret;

	tf = t_fopen(NULL);
	t_fprintf(tf, "\\\\\n");
	t_frewind(tf);
	ret = orw_expect(tf, "\\", 0 /*lines*/, 0 /*eof*/, 1 /*eol*/);
	t_fclose(tf);
	return (ret);
}

T_FUNC(escaped_whitespace, "escaped whitespace")
{
	struct t_file *tf;
	int ret;

	tf = t_fopen(NULL);
	t_fprintf(tf, "\\  \\\t \\\r \\\n\n");
	t_frewind(tf);
	ret = orw_expect(tf, " ", 0 /*lines*/, 0 /*eof*/, 0 /*eol*/) &&
	    orw_expect(tf, "\t", 0 /*lines*/, 0 /*eof*/, 0 /*eol*/) &&
	    orw_expect(tf, "\r", 0 /*lines*/, 0 /*eof*/, 0 /*eol*/) &&
	    /* this last one is a line continuation */
	    orw_expect(tf, NULL, 1 /*lines*/, 0 /*eof*/, 1 /*eol*/);
	t_fclose(tf);
	return (ret);
}

T_FUNC(escaped_newline_before_word, "escaped newline before word")
{
	struct t_file *tf;
	int ret;

	tf = t_fopen(NULL);
	t_fprintf(tf, "\\\nhello world\n");
	t_frewind(tf);
	ret = orw_expect(tf, "hello", 1 /*lines*/, 0 /*eof*/, 0 /*eol*/);
	t_fclose(tf);
	return (ret);
}

T_FUNC(escaped_newline_within_word, "escaped newline within word")
{
	struct t_file *tf;
	int ret;

	tf = t_fopen(NULL);
	t_fprintf(tf, "hello\\\nworld\n");
	t_frewind(tf);
	ret = orw_expect(tf, "helloworld", 1 /*lines*/, 0 /*eof*/, 1 /*eol*/);
	t_fclose(tf);
	return (ret);
}

T_FUNC(escaped_newline_after_word, "escaped newline after word")
{
	struct t_file *tf;
	int ret;

	tf = t_fopen(NULL);
	t_fprintf(tf, "hello\\\n world\n");
	t_frewind(tf);
	ret = orw_expect(tf, "hello", 1 /*lines*/, 0 /*eof*/, 0 /*eol*/);
	t_fclose(tf);
	return (ret);
}

T_FUNC(escaped_letter, "escaped letter")
{
	struct t_file *tf;
	int ret;

	tf = t_fopen(NULL);
	t_fprintf(tf, "\\z\n");
	t_frewind(tf);
	ret = orw_expect(tf, "z", 0 /*lines*/, 0 /*eof*/, 1 /*eol*/);
	t_fclose(tf);
	return (ret);
}

T_FUNC(escaped_comment, "escaped comment")
{
	struct t_file *tf;
	int ret;

	tf = t_fopen(NULL);
	t_fprintf(tf, " \\# comment\n");
	t_frewind(tf);
	ret = orw_expect(tf, "#", 0 /*lines*/, 0 /*eof*/, 0 /*eol*/) &&
	    orw_expect(tf, "comment", 0 /*lines*/, 0 /*eof*/, 1 /*eol*/);
	t_fclose(tf);
	return (ret);
}

T_FUNC(escape_at_eof, "escape at end of file")
{
	struct t_file *tf;
	int ret;

	tf = t_fopen(NULL);
	t_fprintf(tf, "z\\");
	t_frewind(tf);
	ret = orw_expect(tf, NULL, 0 /*lines*/, 1 /*eof*/, 0 /*eol*/);
	t_fclose(tf);
	return (ret);
}


/***************************************************************************
 * Quotes
 */

T_FUNC(naked_single_quote, "naked single quote")
{
	struct t_file *tf;
	int ret;

	tf = t_fopen(NULL);
	t_fprintf(tf, "'");
	t_frewind(tf);
	ret = orw_expect(tf, NULL, 0 /*lines*/, 1 /*eof*/, 0 /*eol*/);
	t_fclose(tf);
	return (ret);
}

T_FUNC(naked_double_quote, "naked double quote")
{
	struct t_file *tf;
	int ret;

	tf = t_fopen(NULL);
	t_fprintf(tf, "\"");
	t_frewind(tf);
	ret = orw_expect(tf, NULL, 0 /*lines*/, 1 /*eof*/, 0 /*eol*/);
	t_fclose(tf);
	return (ret);
}

T_FUNC(empty_single_quotes, "empty single quotes")
{
	struct t_file *tf;
	int ret;

	tf = t_fopen(NULL);
	t_fprintf(tf, "''\n");
	t_frewind(tf);
	ret = orw_expect(tf, "", 0 /*lines*/, 0 /*eof*/, 1 /*eol*/);
	t_fclose(tf);
	return (ret);
}

T_FUNC(empty_double_quotes, "empty double quotes")
{
	struct t_file *tf;
	int ret;

	tf = t_fopen(NULL);
	t_fprintf(tf, "\"\"\n");
	t_frewind(tf);
	ret = orw_expect(tf, "", 0 /*lines*/, 0 /*eof*/, 1 /*eol*/);
	t_fclose(tf);
	return (ret);
}

T_FUNC(single_quotes_within_double_quotes, "single quotes within double quotes")
{
	struct t_file *tf;
	int ret;

	tf = t_fopen(NULL);
	t_fprintf(tf, "\"' '\"\n");
	t_frewind(tf);
	ret = orw_expect(tf, "' '", 0 /*lines*/, 0 /*eof*/, 1 /*eol*/);
	t_fclose(tf);
	return (ret);
}

T_FUNC(double_quotes_within_single_quotes, "double quotes within single quotes")
{
	struct t_file *tf;
	int ret;

	tf = t_fopen(NULL);
	t_fprintf(tf, "'\" \"'\n");
	t_frewind(tf);
	ret = orw_expect(tf, "\" \"", 0 /*lines*/, 0 /*eof*/, 1 /*eol*/);
	t_fclose(tf);
	return (ret);
}

T_FUNC(single_quoted_whitespace, "single-quoted whitespace")
{
	struct t_file *tf;
	int ret;

	tf = t_fopen(NULL);
	t_fprintf(tf, "' ' '\t' '\r' '\n'\n");
	t_frewind(tf);
	ret = orw_expect(tf, " ", 0 /*lines*/, 0 /*eof*/, 0 /*eol*/) &&
	    orw_expect(tf, "\t", 0 /*lines*/, 0 /*eof*/, 0 /*eol*/) &&
	    orw_expect(tf, "\r", 0 /*lines*/, 0 /*eof*/, 0 /*eol*/) &&
	    orw_expect(tf, "\n", 1 /*lines*/, 0 /*eof*/, 1 /*eol*/);
	t_fclose(tf);
	return (ret);
}

T_FUNC(double_quoted_whitespace, "double-quoted whitespace")
{
	struct t_file *tf;
	int ret;

	tf = t_fopen(NULL);
	t_fprintf(tf, "\" \" \"\t\" \"\r\" \"\n\"\n");
	t_frewind(tf);
	ret = orw_expect(tf, " ", 0 /*lines*/, 0 /*eof*/, 0 /*eol*/) &&
	    orw_expect(tf, "\t", 0 /*lines*/, 0 /*eof*/, 0 /*eol*/) &&
	    orw_expect(tf, "\r", 0 /*lines*/, 0 /*eof*/, 0 /*eol*/) &&
	    orw_expect(tf, "\n", 1 /*lines*/, 0 /*eof*/, 1 /*eol*/);
	t_fclose(tf);
	return (ret);
}

T_FUNC(single_quoted_words, "single-quoted words")
{
	struct t_file *tf;
	int ret;

	tf = t_fopen(NULL);
	t_fprintf(tf, "'hello world'\n");
	t_frewind(tf);
	ret = orw_expect(tf, "hello world", 0 /*lines*/, 0 /*eof*/, 1 /*eol*/);
	t_fclose(tf);
	return (ret);
}

T_FUNC(double_quoted_words, "double-quoted words")
{
	struct t_file *tf;
	int ret;

	tf = t_fopen(NULL);
	t_fprintf(tf, "\"hello world\"\n");
	t_frewind(tf);
	ret = orw_expect(tf, "hello world", 0 /*lines*/, 0 /*eof*/, 1 /*eol*/);
	t_fclose(tf);
	return (ret);
}


/***************************************************************************
 * Combinations of quoted and unquoted text
 */

T_FUNC(single_quote_before_word, "single quote before word")
{
	struct t_file *tf;
	int ret;

	tf = t_fopen(NULL);
	t_fprintf(tf, "'hello 'world\n");
	t_frewind(tf);
	ret = orw_expect(tf, "hello world", 0 /*lines*/, 0 /*eof*/, 1 /*eol*/);
	t_fclose(tf);
	return (ret);
}

T_FUNC(double_quote_before_word, "double quote before word")
{
	struct t_file *tf;
	int ret;

	tf = t_fopen(NULL);
	t_fprintf(tf, "\"hello \"world\n");
	t_frewind(tf);
	ret = orw_expect(tf, "hello world", 0 /*lines*/, 0 /*eof*/, 1 /*eol*/);
	t_fclose(tf);
	return (ret);
}

T_FUNC(single_quote_within_word, "single quote within word")
{
	struct t_file *tf;
	int ret;

	tf = t_fopen(NULL);
	t_fprintf(tf, "hello' 'world\n");
	t_frewind(tf);
	ret = orw_expect(tf, "hello world", 0 /*lines*/, 0 /*eof*/, 1 /*eol*/);
	t_fclose(tf);
	return (ret);
}

T_FUNC(double_quote_within_word, "double quote within word")
{
	struct t_file *tf;
	int ret;

	tf = t_fopen(NULL);
	t_fprintf(tf, "hello\" \"world\n");
	t_frewind(tf);
	ret = orw_expect(tf, "hello world", 0 /*lines*/, 0 /*eof*/, 1 /*eol*/);
	t_fclose(tf);
	return (ret);
}

T_FUNC(single_quote_after_word, "single quote after word")
{
	struct t_file *tf;
	int ret;

	tf = t_fopen(NULL);
	t_fprintf(tf, "hello' world'\n");
	t_frewind(tf);
	ret = orw_expect(tf, "hello world", 0 /*lines*/, 0 /*eof*/, 1 /*eol*/);
	t_fclose(tf);
	return (ret);
}

T_FUNC(double_quote_after_word, "double quote after word")
{
	struct t_file *tf;
	int ret;

	tf = t_fopen(NULL);
	t_fprintf(tf, "hello\" world\"\n");
	t_frewind(tf);
	ret = orw_expect(tf, "hello world", 0 /*lines*/, 0 /*eof*/, 1 /*eol*/);
	t_fclose(tf);
	return (ret);
}


/***************************************************************************
 * Combinations of escape and quotes
 */

T_FUNC(escaped_single_quote,
    "escaped single quote")
{
	struct t_file *tf;
	int ret;

	tf = t_fopen(NULL);
	t_fprintf(tf, "\\'\n");
	t_frewind(tf);
	ret = orw_expect(tf, "'", 0 /*lines*/, 0 /*eof*/, 1 /*eol*/);
	t_fclose(tf);
	return (ret);
}

T_FUNC(escaped_double_quote,
    "escaped double quote")
{
	struct t_file *tf;
	int ret;

	tf = t_fopen(NULL);
	t_fprintf(tf, "\\\"\n");
	t_frewind(tf);
	ret = orw_expect(tf, "\"", 0 /*lines*/, 0 /*eof*/, 1 /*eol*/);
	t_fclose(tf);
	return (ret);
}

T_FUNC(escaped_whitespace_within_single_quotes,
    "escaped whitespace within single quotes")
{
	struct t_file *tf;
	int ret;

	tf = t_fopen(NULL);
	t_fprintf(tf, "'\\ ' '\\\t' '\\\r' '\\\n'\n");
	t_frewind(tf);
	ret = orw_expect(tf, "\\ ", 0 /*lines*/, 0 /*eof*/, 0 /*eol*/) &&
	    orw_expect(tf, "\\\t", 0 /*lines*/, 0 /*eof*/, 0 /*eol*/) &&
	    orw_expect(tf, "\\\r", 0 /*lines*/, 0 /*eof*/, 0 /*eol*/) &&
	    orw_expect(tf, "\\\n", 1 /*lines*/, 0 /*eof*/, 1 /*eol*/);
	t_fclose(tf);
	return (ret);
}

T_FUNC(escaped_whitespace_within_double_quotes,
    "escaped whitespace within double quotes")
{
	struct t_file *tf;
	int ret;

	tf = t_fopen(NULL);
	t_fprintf(tf, "\"\\ \" \"\\\t\" \"\\\r\" \"\\\n\"\n");
	t_frewind(tf);
	ret = orw_expect(tf, "\\ ", 0 /*lines*/, 0 /*eof*/, 0 /*eol*/) &&
	    orw_expect(tf, "\\\t", 0 /*lines*/, 0 /*eof*/, 0 /*eol*/) &&
	    orw_expect(tf, "\\\r", 0 /*lines*/, 0 /*eof*/, 0 /*eol*/) &&
	    /* this last one is a line continuation */
	    orw_expect(tf, "", 1 /*lines*/, 0 /*eof*/, 1 /*eol*/);
	t_fclose(tf);
	return (ret);
}

T_FUNC(escaped_letter_within_single_quotes,
    "escaped letter within single quotes")
{
	struct t_file *tf;
	int ret;

	tf = t_fopen(NULL);
	t_fprintf(tf, "'\\z'\n");
	t_frewind(tf);
	ret = orw_expect(tf, "\\z", 0 /*lines*/, 0 /*eof*/, 1 /*eol*/);
	t_fclose(tf);
	return (ret);
}

T_FUNC(escaped_letter_within_double_quotes,
    "escaped letter within double quotes")
{
	struct t_file *tf;
	int ret;

	tf = t_fopen(NULL);
	t_fprintf(tf, "\"\\z\"\n");
	t_frewind(tf);
	ret = orw_expect(tf, "\\z", 0 /*lines*/, 0 /*eof*/, 1 /*eol*/);
	t_fclose(tf);
	return (ret);
}

T_FUNC(escaped_escape_within_single_quotes,
    "escaped escape within single quotes")
{
	struct t_file *tf;
	int ret;

	tf = t_fopen(NULL);
	t_fprintf(tf, "'\\\\'\n");
	t_frewind(tf);
	ret = orw_expect(tf, "\\\\", 0 /*lines*/, 0 /*eof*/, 1 /*eol*/);
	t_fclose(tf);
	return (ret);
}

T_FUNC(escaped_escape_within_double_quotes,
    "escaped escape within double quotes")
{
	struct t_file *tf;
	int ret;

	tf = t_fopen(NULL);
	t_fprintf(tf, "\"\\\\\"\n");
	t_frewind(tf);
	ret = orw_expect(tf, "\\", 0 /*lines*/, 0 /*eof*/, 1 /*eol*/);
	t_fclose(tf);
	return (ret);
}

T_FUNC(escaped_single_quote_within_single_quotes,
    "escaped single quote within single quotes")
{
	struct t_file *tf;
	int ret;

	tf = t_fopen(NULL);
	t_fprintf(tf, "'\\''\n");
	t_frewind(tf);
	ret = orw_expect(tf, NULL, 1 /*lines*/, 1 /*eof*/, 0 /*eol*/);
	t_fclose(tf);
	return (ret);
}

T_FUNC(escaped_double_quote_within_single_quotes,
    "escaped double quote within single quotes")
{
	struct t_file *tf;
	int ret;

	tf = t_fopen(NULL);
	t_fprintf(tf, "'\\\"'\n");
	t_frewind(tf);
	ret = orw_expect(tf, "\\\"", 0 /*lines*/, 0 /*eof*/, 1 /*eol*/);
	t_fclose(tf);
	return (ret);
}

T_FUNC(escaped_single_quote_within_double_quotes,
    "escaped single quote within double quotes")
{
	struct t_file *tf;
	int ret;

	tf = t_fopen(NULL);
	t_fprintf(tf, "\"\\'\"\n");
	t_frewind(tf);
	ret = orw_expect(tf, "\\'", 0 /*lines*/, 0 /*eof*/, 1 /*eol*/);
	t_fclose(tf);
	return (ret);
}

T_FUNC(escaped_double_quote_within_double_quotes,
    "escaped double quote within double quotes")
{
	struct t_file *tf;
	int ret;

	tf = t_fopen(NULL);
	t_fprintf(tf, "\"\\\"\"\n");
	t_frewind(tf);
	ret = orw_expect(tf, "\"", 0 /*lines*/, 0 /*eof*/, 1 /*eol*/);
	t_fclose(tf);
	return (ret);
}


/***************************************************************************
 * Line continuation
 */

T_FUNC(line_continuation_within_whitespace, "line continuation within whitespace")
{
	struct t_file *tf;
	int ret;

	tf = t_fopen(NULL);
	t_fprintf(tf, "hello \\\n world\n");
	t_frewind(tf);
	ret = orw_expect(tf, "hello", 0 /*lines*/, 0 /*eof*/, 0 /*eol*/) &&
	    orw_expect(tf, "world", 1 /*lines*/, 0 /*eof*/, 1 /*eol*/);
	t_fclose(tf);
	return (ret);
}

T_FUNC(line_continuation_before_whitespace, "line continuation before whitespace")
{
	struct t_file *tf;
	int ret;

	tf = t_fopen(NULL);
	t_fprintf(tf, "hello\\\n world\n");
	t_frewind(tf);
	ret = orw_expect(tf, "hello", 1 /*lines*/, 0 /*eof*/, 0 /*eol*/) &&
	    orw_expect(tf, "world", 0 /*lines*/, 0 /*eof*/, 1 /*eol*/);
	t_fclose(tf);
	return (ret);
}

T_FUNC(line_continuation_after_whitespace, "line continuation after whitespace")
{
	struct t_file *tf;
	int ret;

	tf = t_fopen(NULL);
	t_fprintf(tf, "hello \\\nworld\n");
	t_frewind(tf);
	ret = orw_expect(tf, "hello", 0 /*lines*/, 0 /*eof*/, 0 /*eol*/) &&
	    orw_expect(tf, "world", 1 /*lines*/, 0 /*eof*/, 1 /*eol*/);
	t_fclose(tf);
	return (ret);
}

T_FUNC(line_continuation_within_word, "line continuation within word")
{
	struct t_file *tf;
	int ret;

	tf = t_fopen(NULL);
	t_fprintf(tf, "hello\\\nworld\n");
	t_frewind(tf);
	ret = orw_expect(tf, "helloworld", 1 /*lines*/, 0 /*eof*/, 1 /*eol*/);
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
	T(unterminated_line);
	T(single_whitespace);
	T(multiple_whitespace);
	T(comment);
	T(whitespace_before_comment);
	T(single_quoted_comment);
	T(double_quoted_comment);
	T(comment_at_eof);

	T(single_word);
	T(single_whitespace_before_word);
	T(double_whitespace_before_word);
	T(single_whitespace_after_word);
	T(double_whitespace_after_word);
	T(comment_after_word);
	T(word_containing_hash);
	T(two_words);

	T(naked_escape);
	T(escaped_escape);
	T(escaped_whitespace);
	T(escaped_newline_before_word);
	T(escaped_newline_within_word);
	T(escaped_newline_after_word);
	T(escaped_letter);
	T(escaped_comment);
	T(escape_at_eof);

	T(naked_single_quote);
	T(naked_double_quote);
	T(empty_single_quotes);
	T(empty_double_quotes);
	T(single_quotes_within_double_quotes);
	T(double_quotes_within_single_quotes);
	T(single_quoted_whitespace);
	T(double_quoted_whitespace);
	T(single_quoted_words);
	T(double_quoted_words);

	T(single_quote_before_word);
	T(double_quote_before_word);
	T(single_quote_within_word);
	T(double_quote_within_word);
	T(single_quote_after_word);
	T(double_quote_after_word);

	T(escaped_single_quote);
	T(escaped_double_quote);
	T(escaped_whitespace_within_single_quotes);
	T(escaped_whitespace_within_double_quotes);
	T(escaped_letter_within_single_quotes);
	T(escaped_letter_within_double_quotes);
	T(escaped_escape_within_single_quotes);
	T(escaped_escape_within_double_quotes);
	T(escaped_single_quote_within_single_quotes);
	T(escaped_double_quote_within_single_quotes);
	T(escaped_single_quote_within_double_quotes);
	T(escaped_double_quote_within_double_quotes);

	T(line_continuation_within_whitespace);
	T(line_continuation_before_whitespace);
	T(line_continuation_after_whitespace);
	T(line_continuation_within_word);

	return (0);
}

int
main(int argc, char *argv[])
{

	t_main(t_prepare, NULL, argc, argv);
}
