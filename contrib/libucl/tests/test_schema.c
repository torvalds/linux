/* Copyright (c) 2014, Vsevolod Stakhov
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *       * Redistributions of source code must retain the above copyright
 *         notice, this list of conditions and the following disclaimer.
 *       * Redistributions in binary form must reproduce the above copyright
 *         notice, this list of conditions and the following disclaimer in the
 *         documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED ''AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include "ucl.h"

static int
read_stdin (char **buf)
{
	int size = BUFSIZ, remain, ret;
	char *p;

	*buf = malloc (size);
	if (*buf == NULL) {
		return -1;
	}

	p = *buf;
	remain = size;

	while ((ret = read (STDIN_FILENO, p, remain - 1)) > 0) {
		remain -= ret;
		p += ret;

		if (remain <= 1) {
			*buf = realloc (*buf, size * 2);
			if (*buf == NULL) {
				return -1;
			}

			p = *buf + size - 1;
			remain = size + 1;
			size *= 2;
		}
	}

	*p = '\0';

	return ret;
}

static bool
perform_test (const ucl_object_t *schema, const ucl_object_t *obj,
		struct ucl_schema_error *err)
{
	const ucl_object_t *valid, *data, *description;
	bool match;

	data = ucl_object_lookup (obj, "data");
	description = ucl_object_lookup (obj, "description");
	valid = ucl_object_lookup (obj, "valid");

	if (data == NULL || description == NULL || valid == NULL) {
		fprintf (stdout, "Bad test case\n");
		return false;
	}

	match = ucl_object_validate (schema, data, err);
	if (match != ucl_object_toboolean (valid)) {
		fprintf (stdout, "Test case '%s' failed (expected %s): '%s'\n",
				ucl_object_tostring (description),
				ucl_object_toboolean (valid) ? "valid" : "invalid",
						err->msg);
		fprintf (stdout, "%s\n", ucl_object_emit (data, UCL_EMIT_CONFIG));
		fprintf (stdout, "%s\n", ucl_object_emit (schema, UCL_EMIT_CONFIG));
		return false;
	}

	return true;
}

static int
perform_tests (const ucl_object_t *obj)
{
	struct ucl_schema_error err;
	ucl_object_iter_t iter = NULL;
	const ucl_object_t *schema, *tests, *description, *test;

	if (obj->type != UCL_OBJECT) {
		fprintf (stdout, "Bad test case\n");
		return EXIT_FAILURE;
	}

	schema = ucl_object_lookup (obj, "schema");
	tests = ucl_object_lookup (obj, "tests");
	description = ucl_object_lookup (obj, "description");

	if (schema == NULL || tests == NULL || description == NULL) {
		fprintf (stdout, "Bad test case\n");
		return EXIT_FAILURE;
	}

	memset (&err, 0, sizeof (err));

	while ((test = ucl_object_iterate (tests, &iter, true)) != NULL) {
		if (!perform_test (schema, test, &err)) {
			fprintf (stdout, "Test suite '%s' failed\n",
							ucl_object_tostring (description));
			return EXIT_FAILURE;
		}
	}

	return 0;
}

int
main (int argc, char **argv)
{
	char *buf = NULL;
	struct ucl_parser *parser;
	ucl_object_t *obj = NULL;
	const ucl_object_t *elt;
	ucl_object_iter_t iter = NULL;
	int ret = 0;

	if (read_stdin (&buf) == -1) {
		exit (EXIT_FAILURE);
	}

	parser = ucl_parser_new (0);

	ucl_parser_add_string (parser, buf, 0);

	if (ucl_parser_get_error (parser) != NULL) {
		fprintf (stdout, "Error occurred: %s\n", ucl_parser_get_error (parser));
		ret = 1;
		return EXIT_FAILURE;
	}
	obj = ucl_parser_get_object (parser);
	ucl_parser_free (parser);

	while ((elt = ucl_object_iterate (obj, &iter, true)) != NULL) {
		ret = perform_tests (elt);
		if (ret != 0) {
			break;
		}
	}

	ucl_object_unref (obj);

	return ret;
}
