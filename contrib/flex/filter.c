/* filter - postprocessing of flex output through filters */

/*  This file is part of flex. */

/*  Redistribution and use in source and binary forms, with or without */
/*  modification, are permitted provided that the following conditions */
/*  are met: */

/*  1. Redistributions of source code must retain the above copyright */
/*     notice, this list of conditions and the following disclaimer. */
/*  2. Redistributions in binary form must reproduce the above copyright */
/*     notice, this list of conditions and the following disclaimer in the */
/*     documentation and/or other materials provided with the distribution. */

/*  Neither the name of the University nor the names of its contributors */
/*  may be used to endorse or promote products derived from this software */
/*  without specific prior written permission. */

/*  THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR */
/*  IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED */
/*  WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR */
/*  PURPOSE. */

#include "flexdef.h"
static const char * check_4_gnu_m4 =
    "m4_dnl ifdef(`__gnu__', ,"
    "`errprint(Flex requires GNU M4. Set the PATH or set the M4 environment variable to its path name.)"
    " m4exit(2)')\n";


/** global chain. */
struct filter *output_chain = NULL;

/* Allocate and initialize an external filter.
 * @param chain the current chain or NULL for new chain
 * @param cmd the command to execute.
 * @param ... a NULL terminated list of (const char*) arguments to command,
 *            not including argv[0].
 * @return newest filter in chain
 */
struct filter *filter_create_ext (struct filter *chain, const char *cmd,
				  ...)
{
	struct filter *f;
	int     max_args;
	const char *s;
	va_list ap;

	/* allocate and initialize new filter */
	f = (struct filter *) flex_alloc (sizeof (struct filter));
	if (!f)
		flexerror (_("flex_alloc failed (f) in filter_create_ext"));
	memset (f, 0, sizeof (*f));
	f->filter_func = NULL;
	f->extra = NULL;
	f->next = NULL;
	f->argc = 0;

	if (chain != NULL) {
		/* append f to end of chain */
		while (chain->next)
			chain = chain->next;
		chain->next = f;
	}


	/* allocate argv, and populate it with the argument list. */
	max_args = 8;
	f->argv =
		(const char **) flex_alloc (sizeof (char *) *
					    (max_args + 1));
	if (!f->argv)
		flexerror (_("flex_alloc failed (f->argv) in filter_create_ext"));
	f->argv[f->argc++] = cmd;

	va_start (ap, cmd);
	while ((s = va_arg (ap, const char *)) != NULL) {
		if (f->argc >= max_args) {
			max_args += 8;
			f->argv =
				(const char **) flex_realloc (f->argv,
							      sizeof (char
								      *) *
							      (max_args +
							       1));
		}
		f->argv[f->argc++] = s;
	}
	f->argv[f->argc] = NULL;

	va_end (ap);
	return f;
}

/* Allocate and initialize an internal filter.
 * @param chain the current chain or NULL for new chain
 * @param filter_func The function that will perform the filtering.
 *        filter_func should return 0 if successful, and -1
 *        if an error occurs -- or it can simply exit().
 * @param extra optional user-defined data to pass to the filter.
 * @return newest filter in chain
 */
struct filter *filter_create_int (struct filter *chain,
				  int (*filter_func) (struct filter *),
				  void *extra)
{
	struct filter *f;

	/* allocate and initialize new filter */
	f = (struct filter *) flex_alloc (sizeof (struct filter));
	if (!f)
		flexerror (_("flex_alloc failed in filter_create_int"));
	memset (f, 0, sizeof (*f));
	f->next = NULL;
	f->argc = 0;
	f->argv = NULL;

	f->filter_func = filter_func;
	f->extra = extra;

	if (chain != NULL) {
		/* append f to end of chain */
		while (chain->next)
			chain = chain->next;
		chain->next = f;
	}

	return f;
}

/** Fork and exec entire filter chain.
 *  @param chain The head of the chain.
 *  @return true on success.
 */
bool filter_apply_chain (struct filter * chain)
{
	int     pid, pipes[2];


	/* Tricky recursion, since we want to begin the chain
	 * at the END. Why? Because we need all the forked processes
	 * to be children of the main flex process.
	 */
	if (chain)
		filter_apply_chain (chain->next);
	else
		return true;

	/* Now we are the right-most unprocessed link in the chain.
	 */

	fflush (stdout);
	fflush (stderr);


	if (pipe (pipes) == -1)
		flexerror (_("pipe failed"));

	if ((pid = fork ()) == -1)
		flexerror (_("fork failed"));

	if (pid == 0) {
		/* child */

        /* We need stdin (the FILE* stdin) to connect to this new pipe.
         * There is no portable way to set stdin to a new file descriptor,
         * as stdin is not an lvalue on some systems (BSD).
         * So we dup the new pipe onto the stdin descriptor and use a no-op fseek
         * to sync the stream. This is a Hail Mary situation. It seems to work.
         */
		close (pipes[1]);
clearerr(stdin);
		if (dup2 (pipes[0], fileno (stdin)) == -1)
			flexfatal (_("dup2(pipes[0],0)"));
		close (pipes[0]);
        fseek (stdin, 0, SEEK_CUR);

		/* run as a filter, either internally or by exec */
		if (chain->filter_func) {
			int     r;

			if ((r = chain->filter_func (chain)) == -1)
				flexfatal (_("filter_func failed"));
			exit (0);
		}
		else {
			execvp (chain->argv[0],
				(char **const) (chain->argv));
            lerrsf_fatal ( _("exec of %s failed"),
                    chain->argv[0]);
		}

		exit (1);
	}

	/* Parent */
	close (pipes[0]);
	if (dup2 (pipes[1], fileno (stdout)) == -1)
		flexfatal (_("dup2(pipes[1],1)"));
	close (pipes[1]);
    fseek (stdout, 0, SEEK_CUR);

	return true;
}

/** Truncate the chain to max_len number of filters.
 * @param chain the current chain.
 * @param max_len the maximum length of the chain.
 * @return the resulting length of the chain.
 */
int filter_truncate (struct filter *chain, int max_len)
{
	int     len = 1;

	if (!chain)
		return 0;

	while (chain->next && len < max_len) {
		chain = chain->next;
		++len;
	}

	chain->next = NULL;
	return len;
}

/** Splits the chain in order to write to a header file.
 *  Similar in spirit to the 'tee' program.
 *  The header file name is in extra.
 *  @return 0 (zero) on success, and -1 on failure.
 */
int filter_tee_header (struct filter *chain)
{
	/* This function reads from stdin and writes to both the C file and the
	 * header file at the same time.
	 */

	const int readsz = 512;
	char   *buf;
	int     to_cfd = -1;
	FILE   *to_c = NULL, *to_h = NULL;
	bool    write_header;

	write_header = (chain->extra != NULL);

	/* Store a copy of the stdout pipe, which is already piped to C file
	 * through the running chain. Then create a new pipe to the H file as
	 * stdout, and fork the rest of the chain again.
	 */

	if ((to_cfd = dup (1)) == -1)
		flexfatal (_("dup(1) failed"));
	to_c = fdopen (to_cfd, "w");

	if (write_header) {
		if (freopen ((char *) chain->extra, "w", stdout) == NULL)
			flexfatal (_("freopen(headerfilename) failed"));

		filter_apply_chain (chain->next);
		to_h = stdout;
	}

	/* Now to_c is a pipe to the C branch, and to_h is a pipe to the H branch.
	 */

	if (write_header) {
        fputs (check_4_gnu_m4, to_h);
		fputs ("m4_changecom`'m4_dnl\n", to_h);
		fputs ("m4_changequote`'m4_dnl\n", to_h);
		fputs ("m4_changequote([[,]])[[]]m4_dnl\n", to_h);
	    fputs ("m4_define([[M4_YY_NOOP]])[[]]m4_dnl\n", to_h);
		fputs ("m4_define( [[M4_YY_IN_HEADER]],[[]])m4_dnl\n",
		       to_h);
		fprintf (to_h, "#ifndef %sHEADER_H\n", prefix);
		fprintf (to_h, "#define %sHEADER_H 1\n", prefix);
		fprintf (to_h, "#define %sIN_HEADER 1\n\n", prefix);
		fprintf (to_h,
			 "m4_define( [[M4_YY_OUTFILE_NAME]],[[%s]])m4_dnl\n",
			 headerfilename ? headerfilename : "<stdout>");

	}

    fputs (check_4_gnu_m4, to_c);
	fputs ("m4_changecom`'m4_dnl\n", to_c);
	fputs ("m4_changequote`'m4_dnl\n", to_c);
	fputs ("m4_changequote([[,]])[[]]m4_dnl\n", to_c);
	fputs ("m4_define([[M4_YY_NOOP]])[[]]m4_dnl\n", to_c);
	fprintf (to_c, "m4_define( [[M4_YY_OUTFILE_NAME]],[[%s]])m4_dnl\n",
		 outfilename ? outfilename : "<stdout>");

	buf = (char *) flex_alloc (readsz);
	if (!buf)
		flexerror (_("flex_alloc failed in filter_tee_header"));
	while (fgets (buf, readsz, stdin)) {
		fputs (buf, to_c);
		if (write_header)
			fputs (buf, to_h);
	}

	if (write_header) {
		fprintf (to_h, "\n");

		/* write a fake line number. It will get fixed by the linedir filter. */
		fprintf (to_h, "#line 4000 \"M4_YY_OUTFILE_NAME\"\n");

		fprintf (to_h, "#undef %sIN_HEADER\n", prefix);
		fprintf (to_h, "#endif /* %sHEADER_H */\n", prefix);
		fputs ("m4_undefine( [[M4_YY_IN_HEADER]])m4_dnl\n", to_h);

		fflush (to_h);
		if (ferror (to_h))
			lerrsf (_("error writing output file %s"),
				(char *) chain->extra);

		else if (fclose (to_h))
			lerrsf (_("error closing output file %s"),
				(char *) chain->extra);
	}

	fflush (to_c);
	if (ferror (to_c))
		lerrsf (_("error writing output file %s"),
			outfilename ? outfilename : "<stdout>");

	else if (fclose (to_c))
		lerrsf (_("error closing output file %s"),
			outfilename ? outfilename : "<stdout>");

	while (wait (0) > 0) ;

	exit (0);
	return 0;
}

/** Adjust the line numbers in the #line directives of the generated scanner.
 * After the m4 expansion, the line numbers are incorrect since the m4 macros
 * can add or remove lines.  This only adjusts line numbers for generated code,
 * not user code. This also happens to be a good place to squeeze multiple
 * blank lines into a single blank line.
 */
int filter_fix_linedirs (struct filter *chain)
{
	char   *buf;
	const int readsz = 512;
	int     lineno = 1;
	bool    in_gen = true;	/* in generated code */
	bool    last_was_blank = false;

	if (!chain)
		return 0;

	buf = (char *) flex_alloc (readsz);
	if (!buf)
		flexerror (_("flex_alloc failed in filter_fix_linedirs"));

	while (fgets (buf, readsz, stdin)) {

		regmatch_t m[10];

		/* Check for #line directive. */
		if (buf[0] == '#'
			&& regexec (&regex_linedir, buf, 3, m, 0) == 0) {

			int     num;
			char   *fname;

			/* extract the line number and filename */
			num = regmatch_strtol (&m[1], buf, NULL, 0);
			fname = regmatch_dup (&m[2], buf);

			if (strcmp (fname,
				outfilename ? outfilename : "<stdout>")
					== 0
			 || strcmp (fname,
			 	headerfilename ? headerfilename : "<stdout>")
					== 0) {

				char    *s1, *s2;
				char	filename[MAXLINE];

				s1 = fname;
				s2 = filename;

				while ((s2 - filename) < (MAXLINE - 1) && *s1) {
					/* Escape the backslash */
					if (*s1 == '\\')
						*s2++ = '\\';
					/* Escape the double quote */
					if (*s1 == '\"')
						*s2++ = '\\';
					/* Copy the character as usual */
					*s2++ = *s1++;
				}

				*s2 = '\0';

				/* Adjust the line directives. */
				in_gen = true;
				snprintf (buf, readsz, "#line %d \"%s\"\n",
					  lineno + 1, filename);
			}
			else {
				/* it's a #line directive for code we didn't write */
				in_gen = false;
			}

			free (fname);
			last_was_blank = false;
		}

		/* squeeze blank lines from generated code */
		else if (in_gen
			 && regexec (&regex_blank_line, buf, 0, NULL,
				     0) == 0) {
			if (last_was_blank)
				continue;
			else
				last_was_blank = true;
		}

		else {
			/* it's a line of normal, non-empty code. */
			last_was_blank = false;
		}

		fputs (buf, stdout);
		lineno++;
	}
	fflush (stdout);
	if (ferror (stdout))
		lerrsf (_("error writing output file %s"),
			outfilename ? outfilename : "<stdout>");

	else if (fclose (stdout))
		lerrsf (_("error closing output file %s"),
			outfilename ? outfilename : "<stdout>");

	return 0;
}

/* vim:set expandtab cindent tabstop=4 softtabstop=4 shiftwidth=4 textwidth=0: */
