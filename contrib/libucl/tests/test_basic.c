/* Copyright (c) 2013, Vsevolod Stakhov
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

#include "ucl.h"
#include "ucl_internal.h"
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>


int
main (int argc, char **argv)
{
	char *inbuf = NULL;
	struct ucl_parser *parser = NULL, *parser2 = NULL;
	ucl_object_t *obj, *comments = NULL;
	ssize_t bufsize, r;
	FILE *in, *out;
	unsigned char *emitted = NULL;
	const char *fname_in = NULL, *fname_out = NULL;
	int ret = 0, opt, json = 0, compact = 0, yaml = 0,
			save_comments = 0, skip_macro = 0,
			flags, fd_out, fd_in, use_fd = 0;
	struct ucl_emitter_functions *func;

	while ((opt = getopt(argc, argv, "fjcyCM")) != -1) {
		switch (opt) {
		case 'j':
			json = 1;
			break;
		case 'c':
			compact = 1;
			break;
		case 'C':
			save_comments = 1;
			break;
		case 'y':
			yaml = 1;
			break;
		case 'M':
			skip_macro = true;
			break;
		case 'f':
			use_fd = true;
			break;
		default: /* '?' */
			fprintf (stderr, "Usage: %s [-jcy] [-CM] [-f] [in] [out]\n",
					argv[0]);
			exit (EXIT_FAILURE);
		}
	}

	argc -= optind;
	argv += optind;

	switch (argc) {
	case 1:
		fname_in = argv[0];
		break;
	case 2:
		fname_in = argv[0];
		fname_out = argv[1];
		break;
	}

	if (!use_fd) {
		if (fname_in != NULL) {
			in = fopen (fname_in, "r");
			if (in == NULL) {
				exit (-errno);
			}
		}
		else {
			in = stdin;
		}
	}
	else {
		if (fname_in != NULL) {
			fd_in = open (fname_in, O_RDONLY);
			if (fd_in == -1) {
				exit (-errno);
			}
		}
		else {
			fd_in = STDIN_FILENO;
		}
	}

	flags = UCL_PARSER_KEY_LOWERCASE;

	if (save_comments) {
		flags |= UCL_PARSER_SAVE_COMMENTS;
	}

	if (skip_macro) {
		flags |= UCL_PARSER_DISABLE_MACRO;
	}

	parser = ucl_parser_new (flags);
	ucl_parser_register_variable (parser, "ABI", "unknown");

	if (fname_in != NULL) {
		ucl_parser_set_filevars (parser, fname_in, true);
	}

	if (!use_fd) {
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

		ucl_parser_add_chunk (parser, (const unsigned char *)inbuf, r);
		fclose (in);
	}
	else {
		ucl_parser_add_fd (parser, fd_in);
		close (fd_in);
	}

	if (!use_fd) {
		if (fname_out != NULL) {
			out = fopen (fname_out, "w");
			if (out == NULL) {
				exit (-errno);
			}
		}
		else {
			out = stdout;
		}
	}
	else {
		if (fname_out != NULL) {
			fd_out = open (fname_out, O_WRONLY | O_CREAT, 00644);
			if (fd_out == -1) {
				exit (-errno);
			}
		}
		else {
			fd_out = STDOUT_FILENO;
		}
	}


	if (ucl_parser_get_error (parser) != NULL) {
		fprintf (out, "Error occurred (phase 1): %s\n",
						ucl_parser_get_error(parser));
		ret = 1;
		goto end;
	}

	obj = ucl_parser_get_object (parser);

	if (save_comments) {
		comments = ucl_object_ref (ucl_parser_get_comments (parser));
	}

	if (json) {
		if (compact) {
			emitted = ucl_object_emit (obj, UCL_EMIT_JSON_COMPACT);
		}
		else {
			emitted = ucl_object_emit (obj, UCL_EMIT_JSON);
		}
	}
	else if (yaml) {
		emitted = ucl_object_emit (obj, UCL_EMIT_YAML);
	}
	else {
		emitted = NULL;
		func = ucl_object_emit_memory_funcs ((void **)&emitted);

		if (func != NULL) {
			ucl_object_emit_full (obj, UCL_EMIT_CONFIG, func, comments);
			ucl_object_emit_funcs_free (func);
		}
	}

#if 0
	fprintf (out, "%s\n****\n", emitted);
#endif

	ucl_parser_free (parser);
	ucl_object_unref (obj);
	parser2 = ucl_parser_new (flags);
	ucl_parser_add_string (parser2, (const char *)emitted, 0);

	if (ucl_parser_get_error(parser2) != NULL) {
		fprintf (out, "Error occurred (phase 2): %s\n",
				ucl_parser_get_error(parser2));
		fprintf (out, "%s\n", emitted);
		ret = 1;
		goto end;
	}

	if (emitted != NULL) {
		free (emitted);
	}
	if (comments) {
		ucl_object_unref (comments);
		comments = NULL;
	}

	if (save_comments) {
		comments = ucl_object_ref (ucl_parser_get_comments (parser2));
	}

	obj = ucl_parser_get_object (parser2);

	if (!use_fd) {
		func = ucl_object_emit_file_funcs (out);
	}
	else {
		func = ucl_object_emit_fd_funcs (fd_out);
	}

	if (func != NULL) {
		if (json) {
			if (compact) {
				ucl_object_emit_full (obj, UCL_EMIT_JSON_COMPACT,
						func, comments);
			}
			else {
				ucl_object_emit_full (obj, UCL_EMIT_JSON,
						func, comments);
			}
		}
		else if (yaml) {
			ucl_object_emit_full (obj, UCL_EMIT_YAML,
					func, comments);
		}
		else {
			ucl_object_emit_full (obj, UCL_EMIT_CONFIG,
					func, comments);
		}

		ucl_object_emit_funcs_free (func);
	}

	if (!use_fd) {
		fprintf (out, "\n");
		fclose (out);
	}
	else {
		write (fd_out, "\n", 1);
		close (fd_out);
	}

	ucl_object_unref (obj);

end:
	if (parser2 != NULL) {
		ucl_parser_free (parser2);
	}
	if (comments) {
		ucl_object_unref (comments);
	}
	if (inbuf != NULL) {
		free (inbuf);
	}

	return ret;
}
