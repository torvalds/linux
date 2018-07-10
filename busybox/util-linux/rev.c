/*
 * rev implementation for busybox
 *
 * Copyright (C) 2010 Marek Polacek <mmpolacek@gmail.com>
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */
//config:config REV
//config:	bool "rev (4.5 kb)"
//config:	default y
//config:	help
//config:	Reverse lines of a file or files.

//applet:IF_REV(APPLET(rev, BB_DIR_BIN, BB_SUID_DROP))

//kbuild:lib-$(CONFIG_REV) += rev.o

//usage:#define rev_trivial_usage
//usage:	"[FILE]..."
//usage:#define rev_full_usage "\n\n"
//usage:	"Reverse lines of FILE"

#include "libbb.h"
#include "unicode.h"

#undef CHAR_T
#if ENABLE_UNICODE_SUPPORT
# define CHAR_T wchar_t
#else
# define CHAR_T char
#endif

/* In-place invert */
static void strrev(CHAR_T *s, int len)
{
	int i;

	if (len != 0) {
		len--;
		if (len != 0 && s[len] == '\n')
			len--;
	}

	for (i = 0; i < len; i++, len--) {
		CHAR_T c = s[i];
		s[i] = s[len];
		s[len] = c;
	}
}

int rev_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int rev_main(int argc UNUSED_PARAM, char **argv)
{
	int retval;
	size_t bufsize;
	char *buf;

	init_unicode();

	getopt32(argv, "");
	argv += optind;
	if (!argv[0])
		argv = (char **)&bb_argv_dash;

	retval = EXIT_SUCCESS;
	bufsize = 256;
	buf = xmalloc(bufsize);
	do {
		size_t pos;
		FILE *fp;

		fp = fopen_or_warn_stdin(*argv++);
		if (!fp) {
			retval = EXIT_FAILURE;
			continue;
		}

		pos = 0;
		while (1) {
			/* Read one line */
			buf[bufsize - 1] = 1; /* not 0 */
			if (!fgets(buf + pos, bufsize - pos, fp))
				break; /* EOF/error */
			if (buf[bufsize - 1] == '\0' /* fgets filled entire buffer */
			 && buf[bufsize - 2] != '\n' /* and did not read '\n' */
			 && !feof(fp)
			) {
				/* Line is too long, extend buffer */
				pos = bufsize - 1;
				bufsize += 64 + bufsize / 8;
				buf = xrealloc(buf, bufsize);
				continue;
			}

			/* Process and print it */
#if ENABLE_UNICODE_SUPPORT
			{
				wchar_t *tmp = xmalloc(bufsize * sizeof(wchar_t));
				/* Convert to wchar_t (might error out!) */
				int len  = mbstowcs(tmp, buf, bufsize);
				if (len >= 0) {
					strrev(tmp, len);
					/* Convert back to char */
					wcstombs(buf, tmp, bufsize);
				}
				free(tmp);
			}
#else
			strrev(buf, strlen(buf));
#endif
			fputs(buf, stdout);
		}
		fclose(fp);
	} while (*argv);

	if (ENABLE_FEATURE_CLEAN_UP)
		free(buf);

	fflush_stdout_and_exit(retval);
}
