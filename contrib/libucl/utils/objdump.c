/* Copyright (c) 2013, Dmitriy V. Reshetnikov
 * Copyright (c) 2013, Vsevolod Stakhov
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

#include "ucl.h"

void
ucl_obj_dump (const ucl_object_t *obj, unsigned int shift)
{
	int num = shift * 4 + 5;
	char *pre = (char *) malloc (num * sizeof(char));
	const ucl_object_t *cur, *tmp;
	ucl_object_iter_t it = NULL, it_obj = NULL;

	pre[--num] = 0x00;
	while (num--)
		pre[num] = 0x20;

	tmp = obj;

	while ((obj = ucl_object_iterate (tmp, &it, false))) {
		printf ("%sucl object address: %p\n", pre + 4, obj);
		if (obj->key != NULL) {
			printf ("%skey: \"%s\"\n", pre, ucl_object_key (obj));
		}
		printf ("%sref: %u\n", pre, obj->ref);
		printf ("%slen: %u\n", pre, obj->len);
		printf ("%sprev: %p\n", pre, obj->prev);
		printf ("%snext: %p\n", pre, obj->next);
		if (obj->type == UCL_OBJECT) {
			printf ("%stype: UCL_OBJECT\n", pre);
			printf ("%svalue: %p\n", pre, obj->value.ov);
			it_obj = NULL;
			while ((cur = ucl_object_iterate (obj, &it_obj, true))) {
				ucl_obj_dump (cur, shift + 2);
			}
		}
		else if (obj->type == UCL_ARRAY) {
			printf ("%stype: UCL_ARRAY\n", pre);
			printf ("%svalue: %p\n", pre, obj->value.av);
			it_obj = NULL;
			while ((cur = ucl_object_iterate (obj, &it_obj, true))) {
				ucl_obj_dump (cur, shift + 2);
			}
		}
		else if (obj->type == UCL_INT) {
			printf ("%stype: UCL_INT\n", pre);
			printf ("%svalue: %jd\n", pre, (intmax_t)ucl_object_toint (obj));
		}
		else if (obj->type == UCL_FLOAT) {
			printf ("%stype: UCL_FLOAT\n", pre);
			printf ("%svalue: %f\n", pre, ucl_object_todouble (obj));
		}
		else if (obj->type == UCL_STRING) {
			printf ("%stype: UCL_STRING\n", pre);
			printf ("%svalue: \"%s\"\n", pre, ucl_object_tostring (obj));
		}
		else if (obj->type == UCL_BOOLEAN) {
			printf ("%stype: UCL_BOOLEAN\n", pre);
			printf ("%svalue: %s\n", pre, ucl_object_tostring_forced (obj));
		}
		else if (obj->type == UCL_TIME) {
			printf ("%stype: UCL_TIME\n", pre);
			printf ("%svalue: %f\n", pre, ucl_object_todouble (obj));
		}
		else if (obj->type == UCL_USERDATA) {
			printf ("%stype: UCL_USERDATA\n", pre);
			printf ("%svalue: %p\n", pre, obj->value.ud);
		}
	}

	free (pre);
}

int
main(int argc, char **argv)
{
	const char *fn = NULL;
	unsigned char *inbuf;
	struct ucl_parser *parser;
	int k, ret = 0, r = 0;
	ssize_t bufsize;
	ucl_object_t *obj = NULL;
	const ucl_object_t *par;
	FILE *in;

	if (argc > 1) {
		fn = argv[1];
	}

	if (fn != NULL) {
		in = fopen (fn, "r");
		if (in == NULL) {
			exit (-errno);
		}
	}
	else {
		in = stdin;
	}

	parser = ucl_parser_new (0);
	inbuf = malloc (BUFSIZ);
	bufsize = BUFSIZ;
	r = 0;

	while (!feof (in) && !ferror (in)) {
		if (r == bufsize) {
			inbuf = realloc (inbuf, bufsize * 2);
			bufsize *= 2;
			if (inbuf == NULL) {
				perror ("realloc");
				exit (EXIT_FAILURE);
			}
		}
		r += fread (inbuf + r, 1, bufsize - r, in);
	}

	if (ferror (in)) {
		fprintf (stderr, "Failed to read the input file.\n");
		exit (EXIT_FAILURE);
	}

	ucl_parser_add_chunk (parser, inbuf, r);
	fclose (in);
	if (ucl_parser_get_error(parser)) {
		printf ("Error occured: %s\n", ucl_parser_get_error(parser));
		ret = 1;
		goto end;
	}

	obj = ucl_parser_get_object (parser);
	if (ucl_parser_get_error (parser)) {
		printf ("Error occured: %s\n", ucl_parser_get_error(parser));
		ret = 1;
		goto end;
	}

	if (argc > 2) {
		for (k = 2; k < argc; k++) {
			printf ("search for \"%s\"... ", argv[k]);
			par = ucl_object_lookup (obj, argv[k]);
			printf ("%sfound\n", (par == NULL )?"not ":"");
			ucl_obj_dump (par, 0);
		}
	}
	else {
		ucl_obj_dump (obj, 0);
	}

end:
	if (parser != NULL) {
		ucl_parser_free (parser);
	}
	if (obj != NULL) {
		ucl_object_unref (obj);
	}

	return ret;
}
