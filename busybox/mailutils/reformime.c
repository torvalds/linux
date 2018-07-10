/* vi: set sw=4 ts=4: */
/*
 * reformime: parse MIME-encoded message
 *
 * Copyright (C) 2008 by Vladimir Dronnikov <dronnikov@gmail.com>
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */
//config:config REFORMIME
//config:	bool "reformime (7.5 kb)"
//config:	default y
//config:	help
//config:	Parse MIME-formatted messages.
//config:
//config:config FEATURE_REFORMIME_COMPAT
//config:	bool "Accept and ignore options other than -x and -X"
//config:	default y
//config:	depends on REFORMIME
//config:	help
//config:	Accept (for compatibility only) and ignore options
//config:	other than -x and -X.

//applet:IF_REFORMIME(APPLET(reformime, BB_DIR_BIN, BB_SUID_DROP))

//kbuild:lib-$(CONFIG_REFORMIME) += reformime.o mail.o

#include "libbb.h"
#include "mail.h"

#if 0
# define dbg_error_msg(...) bb_error_msg(__VA_ARGS__)
#else
# define dbg_error_msg(...) ((void)0)
#endif

static const char *find_token(const char *const string_array[], const char *key, const char *defvalue)
{
	const char *r = NULL;
	int i;
	for (i = 0; string_array[i] != NULL; i++) {
		if (strcasecmp(string_array[i], key) == 0) {
			r = (char *)string_array[i+1];
			break;
		}
	}
	return (r) ? r : defvalue;
}

static const char *xfind_token(const char *const string_array[], const char *key)
{
	const char *r = find_token(string_array, key, NULL);
	if (r)
		return r;
	bb_error_msg_and_die("not found: '%s'", key);
}

enum {
	OPT_x = 1 << 0,
	OPT_X = 1 << 1,
#if ENABLE_FEATURE_REFORMIME_COMPAT
	OPT_d = 1 << 2,
	OPT_e = 1 << 3,
	OPT_i = 1 << 4,
	OPT_s = 1 << 5,
	OPT_r = 1 << 6,
	OPT_c = 1 << 7,
	OPT_m = 1 << 8,
	OPT_h = 1 << 9,
	OPT_o = 1 << 10,
	OPT_O = 1 << 11,
#endif
};

static int parse(const char *boundary, char **argv)
{
	int boundary_len = strlen(boundary);
	char uniq[sizeof("%%llu.%u") + sizeof(int)*3];

	dbg_error_msg("BOUNDARY[%s]", boundary);

	// prepare unique string pattern
	sprintf(uniq, "%%llu.%u", (unsigned)getpid());
	dbg_error_msg("UNIQ[%s]", uniq);

	while (1) {
		char *header;
		const char *tokens[32]; /* 32 is enough */
		const char *type;

		/* Read the header (everything up to two \n) */
		{
			unsigned header_idx = 0;
			int last_ch = 0;
			header = NULL;
			while (1) {
				int ch = fgetc(stdin);
				if (ch == '\r') /* Support both line endings */
					continue;
				if (ch == EOF)
					break;
				if (ch == '\n' && last_ch == ch)
					break;
				if (!(header_idx & 0xff))
					header = xrealloc(header, header_idx + 0x101);
				header[header_idx++] = last_ch = ch;
			}
			if (!header) {
				dbg_error_msg("EOF");
				break;
			}
			header[header_idx] = '\0';
			dbg_error_msg("H:'%s'", p);
		}

		/* Split to tokens */
		{
			char *s, *p;
			unsigned ntokens;
			const char *delims = ";=\" \t\n";

			/* Skip to last Content-Type: */
			s = p = header;
			while ((p = strchr(p, '\n')) != NULL) {
				p++;
				if (strncasecmp(p, "Content-Type:", sizeof("Content-Type:")-1) == 0)
					s = p;
			}
			dbg_error_msg("L:'%s'", p);
			ntokens = 0;
			s = strtok(s, delims);
			while (s) {
				tokens[ntokens] = s;
				if (ntokens < ARRAY_SIZE(tokens) - 1)
					ntokens++;
				dbg_error_msg("L[%d]='%s'", ntokens, s);
				s = strtok(NULL, delims);
			}
			tokens[ntokens] = NULL;
			dbg_error_msg("EMPTYLINE, ntokens:%d", ntokens);
			if (ntokens == 0)
				break;
		}

		/* Is it multipart? */
		type = find_token(tokens, "Content-Type:", "text/plain");
		dbg_error_msg("TYPE:'%s'", type);
		if (0 == strncasecmp(type, "multipart/", 10)) {
			/* Yes, recurse */
			if (strcasecmp(type + 10, "mixed") != 0)
				bb_error_msg_and_die("no support of content type '%s'", type);
			parse(xfind_token(tokens, "boundary"), argv);
		} else {
			/* No, process one non-multipart section */
			char *end;
			pid_t pid = pid;
			FILE *fp;

			const char *charset = find_token(tokens, "charset", CONFIG_FEATURE_MIME_CHARSET);
			const char *encoding = find_token(tokens, "Content-Transfer-Encoding:", "7bit");

			/* Compose target filename */
			char *filename = (char *)find_token(tokens, "filename", NULL);
			if (!filename)
				filename = xasprintf(uniq, monotonic_us());
			else
				filename = bb_get_last_path_component_strip(xstrdup(filename));

			if (opts & OPT_X) {
				int fd[2];

				/* start external helper */
				xpipe(fd);
				pid = vfork();
				if (0 == pid) {
					/* child reads from fd[0] */
					close(fd[1]);
					xmove_fd(fd[0], STDIN_FILENO);
					xsetenv("CONTENT_TYPE", type);
					xsetenv("CHARSET", charset);
					xsetenv("ENCODING", encoding);
					xsetenv("FILENAME", filename);
					BB_EXECVP_or_die(argv);
				}
				/* parent will write to fd[1] */
				close(fd[0]);
				fp = xfdopen_for_write(fd[1]);
				signal(SIGPIPE, SIG_IGN);
			} else {
				/* write to file */
				char *fname = xasprintf("%s%s", *argv, filename);
				fp = xfopen_for_write(fname);
				free(fname);
			}
			free(filename);

			/* write to fp */
			end = NULL;
			if (0 == strcasecmp(encoding, "base64")) {
				read_base64(stdin, fp, '-');
			} else
			if (0 != strcasecmp(encoding, "7bit")
			 && 0 != strcasecmp(encoding, "8bit")
			) {
				/* quoted-printable, binary, user-defined are unsupported so far */
				bb_error_msg_and_die("encoding '%s' not supported", encoding);
			} else {
				/* plain 7bit or 8bit */
				while ((end = xmalloc_fgets(stdin)) != NULL) {
					if ('-' == end[0]
					 && '-' == end[1]
					 && strncmp(end + 2, boundary, boundary_len) == 0
					) {
						break;
					}
					fputs(end, fp);
				}
			}
			fclose(fp);

			/* Wait for child */
			if (opts & OPT_X) {
				int rc;
				signal(SIGPIPE, SIG_DFL);
				rc = (wait4pid(pid) & 0xff);
				if (rc != 0)
					return rc + 20;
			}

			/* Multipart ended? */
			if (end && '-' == end[2 + boundary_len] && '-' == end[2 + boundary_len + 1]) {
				dbg_error_msg("FINISHED MPART:'%s'", end);
				break;
			}
			dbg_error_msg("FINISHED:'%s'", end);
			free(end);
		} /* end of "handle one non-multipart block" */

		free(header);
	} /* while (1) */

	dbg_error_msg("ENDPARSE[%s]", boundary);

	return EXIT_SUCCESS;
}

//usage:#define reformime_trivial_usage
//usage:       "[OPTIONS]"
//usage:#define reformime_full_usage "\n\n"
//usage:       "Parse MIME-encoded message on stdin\n"
//usage:     "\n	-x PREFIX	Extract content of MIME sections to files"
//usage:     "\n	-X PROG ARGS	Filter content of MIME sections through PROG"
//usage:     "\n			Must be the last option"
//usage:     "\n"
//usage:     "\nOther options are silently ignored"

/*
Usage: reformime [options]
    -d - parse a delivery status notification.
    -e - extract contents of MIME section.
    -x - extract MIME section to a file.
    -X - pipe MIME section to a program.
    -i - show MIME info.
    -s n.n.n.n - specify MIME section.
    -r - rewrite message, filling in missing MIME headers.
    -r7 - also convert 8bit/raw encoding to quoted-printable, if possible.
    -r8 - also convert quoted-printable encoding to 8bit, if possible.
    -c charset - default charset for rewriting, -o, and -O.
    -m [file] [file]... - create a MIME message digest.
    -h "header" - decode RFC 2047-encoded header.
    -o "header" - encode unstructured header using RFC 2047.
    -O "header" - encode address list header using RFC 2047.
*/

int reformime_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int reformime_main(int argc UNUSED_PARAM, char **argv)
{
	const char *opt_prefix = "";

	INIT_G();

	// parse options
	// N.B. only -x and -X are supported so far
	opts = getopt32(argv, "^"
		"x:X" IF_FEATURE_REFORMIME_COMPAT("deis:r:c:m:*h:o:O:")
		"\0" "x--X:X--x",
		&opt_prefix
		IF_FEATURE_REFORMIME_COMPAT(, NULL, NULL, &G.opt_charset, NULL, NULL, NULL, NULL)
	);
	argv += optind;

	return parse("", (opts & OPT_X) ? argv : (char **)&opt_prefix);
}
