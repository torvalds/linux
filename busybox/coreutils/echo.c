/* vi: set sw=4 ts=4: */
/*
 * echo implementation for busybox
 *
 * Copyright (c) 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 *
 * Original copyright notice is retained at the end of this file.
 */
/* Mar 16, 2003      Manuel Novoa III   (mjn3@codepoet.org)
 *
 * Because of behavioral differences, implemented configurable SUSv3
 * or 'fancy' gnu-ish behaviors.  Also, reduced size and fixed bugs.
 * 1) In handling '\c' escape, the previous version only suppressed the
 *     trailing newline.  SUSv3 specifies _no_ output after '\c'.
 * 2) SUSv3 specifies that octal escapes are of the form \0{#{#{#}}}.
 *    The previous version did not allow 4-digit octals.
 */
//config:config ECHO
//config:	bool "echo (basic SuSv3 version taking no options)"
//config:	default y
//config:	help
//config:	echo is used to print a specified string to stdout.
//config:
//config:# this entry also appears in shell/Config.in, next to the echo builtin
//config:config FEATURE_FANCY_ECHO
//config:	bool "Enable -n and -e options"
//config:	default y
//config:	depends on ECHO || ASH_ECHO || HUSH_ECHO

//applet:IF_ECHO(APPLET_NOFORK(echo, echo, BB_DIR_BIN, BB_SUID_DROP, echo))

//kbuild:lib-$(CONFIG_ECHO) += echo.o

//kbuild:lib-$(CONFIG_ASH_ECHO)  += echo.o
//kbuild:lib-$(CONFIG_HUSH_ECHO) += echo.o

/* BB_AUDIT SUSv3 compliant -- unless configured as fancy echo. */
/* http://www.opengroup.org/onlinepubs/007904975/utilities/echo.html */

//usage:#define echo_trivial_usage
//usage:	IF_FEATURE_FANCY_ECHO("[-neE] ") "[ARG]..."
//usage:#define echo_full_usage "\n\n"
//usage:       "Print the specified ARGs to stdout"
//usage:	IF_FEATURE_FANCY_ECHO( "\n"
//usage:     "\n	-n	Suppress trailing newline"
//usage:     "\n	-e	Interpret backslash escapes (i.e., \\t=tab)"
//usage:     "\n	-E	Don't interpret backslash escapes (default)"
//usage:	)
//usage:
//usage:#define echo_example_usage
//usage:       "$ echo \"Erik is cool\"\n"
//usage:       "Erik is cool\n"
//usage:	IF_FEATURE_FANCY_ECHO("$ echo -e \"Erik\\nis\\ncool\"\n"
//usage:       "Erik\n"
//usage:       "is\n"
//usage:       "cool\n"
//usage:       "$ echo \"Erik\\nis\\ncool\"\n"
//usage:       "Erik\\nis\\ncool\n")

#include "libbb.h"

/* This is a NOFORK applet. Be very careful! */

/* NB: can be used by shell even if not enabled as applet */

/*
 * NB2: we don't use stdio, we need better error handing.
 * Examples include writing into non-opened stdout and error on write.
 *
 * With stdio, output gets shoveled into stdout buffer, and even
 * fflush cannot clear it out. It seems that even if libc receives
 * EBADF on write attempts, it feels determined to output data no matter what.
 * If echo is called by shell, it will try writing again later, and possibly
 * will clobber future output. Not good.
 *
 * Solaris has fpurge which discards buffered input. glibc has __fpurge.
 * But this function is not standard.
 */

int echo_main(int argc UNUSED_PARAM, char **argv)
{
	char **pp;
	const char *arg;
	char *out;
	char *buffer;
	unsigned buflen;
#if !ENABLE_FEATURE_FANCY_ECHO
	enum {
		eflag = 0,  /* 0 -- disable escape sequences */
		nflag = 1,  /* 1 -- print '\n' */
	};

	argv++;
#else
	char nflag = 1;
	char eflag = 0;

	while ((arg = *++argv) != NULL) {
		char n, e;

		if (arg[0] != '-')
			break; /* not an option arg, echo it */

		/* If it appears that we are handling options, then make sure
		 * that all of the options specified are actually valid.
		 * Otherwise, the string should just be echoed.
		 */
		arg++;
		n = nflag;
		e = eflag;
		do {
			if (*arg == 'n')
				n = 0;
			else if (*arg == 'e')
				e = '\\';
			else if (*arg != 'E') {
				/* "-ccc" arg with one of c's invalid, echo it */
				/* arg consisting from just "-" also handled here */
				goto just_echo;
			}
		} while (*++arg);
		nflag = n;
		eflag = e;
	}
 just_echo:
#endif

	buflen = 0;
	pp = argv;
	while ((arg = *pp) != NULL) {
		buflen += strlen(arg) + 1;
		pp++;
	}
	out = buffer = xmalloc(buflen + 1); /* +1 is needed for "no args" case */

	while ((arg = *argv) != NULL) {
		int c;

		if (!eflag) {
			/* optimization for very common case */
			out = stpcpy(out, arg);
		} else
		while ((c = *arg++) != '\0') {
			if (c == eflag) {
				/* This is an "\x" sequence */

				if (*arg == 'c') {
					/* "\c" means cancel newline and
					 * ignore all subsequent chars. */
					goto do_write;
				}
				/* Since SUSv3 mandates a first digit of 0, 4-digit octals
				* of the form \0### are accepted. */
				if (*arg == '0') {
					if ((unsigned char)(arg[1] - '0') < 8) {
						/* 2nd char is 0..7: skip leading '0' */
						arg++;
					}
				}
				/* bb_process_escape_sequence handles NUL correctly
				 * ("...\" case). */
				{
					/* optimization: don't force arg to be on-stack,
					 * use another variable for that. ~30 bytes win */
					const char *z = arg;
					c = bb_process_escape_sequence(&z);
					arg = z;
				}
			}
			*out++ = c;
		}

		if (!*++argv)
			break;
		*out++ = ' ';
	}

	if (nflag) {
		*out++ = '\n';
	}

 do_write:
	/* Careful to error out on partial writes too (think ENOSPC!) */
	errno = 0;
	/*r =*/ full_write(STDOUT_FILENO, buffer, out - buffer);
	free(buffer);
	if (/*WRONG:r < 0*/ errno) {
		bb_perror_msg(bb_msg_write_error);
		return 1;
	}
	return 0;
}

/*
 * Copyright (c) 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Kenneth Almquist.
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
 * 3. <BSD Advertising Clause omitted per the July 22, 1999 licensing change
 *		ftp://ftp.cs.berkeley.edu/pub/4bsd/README.Impt.License.Change>
 *
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ''AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)echo.c	8.1 (Berkeley) 5/31/93
 */

#ifdef VERSION_WITH_WRITEV
/* We can't use stdio.
 * The reason for this is highly non-obvious.
 * echo_main is used from shell. Shell must correctly handle "echo foo"
 * if stdout is closed. With stdio, output gets shoveled into
 * stdout buffer, and even fflush cannot clear it out. It seems that
 * even if libc receives EBADF on write attempts, it feels determined
 * to output data no matter what. So it will try later,
 * and possibly will clobber future output. Not good.
 *
 * Using writev instead, with 'direct' conversion of argv vector.
 */

int echo_main(int argc, char **argv)
{
	struct iovec io[argc];
	struct iovec *cur_io = io;
	char *arg;
	char *p;
#if !ENABLE_FEATURE_FANCY_ECHO
	enum {
		eflag = '\\',
		nflag = 1,  /* 1 -- print '\n' */
	};
	arg = *++argv;
	if (!arg)
		goto newline_ret;
#else
	char nflag = 1;
	char eflag = 0;

	while (1) {
		arg = *++argv;
		if (!arg)
			goto newline_ret;
		if (*arg != '-')
			break;

		/* If it appears that we are handling options, then make sure
		 * that all of the options specified are actually valid.
		 * Otherwise, the string should just be echoed.
		 */
		p = arg + 1;
		if (!*p)	/* A single '-', so echo it. */
			goto just_echo;

		do {
			if (!strchr("neE", *p))
				goto just_echo;
		} while (*++p);

		/* All of the options in this arg are valid, so handle them. */
		p = arg + 1;
		do {
			if (*p == 'n')
				nflag = 0;
			if (*p == 'e')
				eflag = '\\';
		} while (*++p);
	}
 just_echo:
#endif

	while (1) {
		/* arg is already == *argv and isn't NULL */
		int c;

		cur_io->iov_base = p = arg;

		if (!eflag) {
			/* optimization for very common case */
			p += strlen(arg);
		} else while ((c = *arg++)) {
			if (c == eflag) {
				/* This is an "\x" sequence */

				if (*arg == 'c') {
					/* "\c" means cancel newline and
					 * ignore all subsequent chars. */
					cur_io->iov_len = p - (char*)cur_io->iov_base;
					cur_io++;
					goto ret;
				}
				/* Since SUSv3 mandates a first digit of 0, 4-digit octals
				* of the form \0### are accepted. */
				if (*arg == '0' && (unsigned char)(arg[1] - '0') < 8) {
					arg++;
				}
				/* bb_process_escape_sequence can handle nul correctly */
				c = bb_process_escape_sequence( (void*) &arg);
			}
			*p++ = c;
		}

		arg = *++argv;
		if (arg)
			*p++ = ' ';
		cur_io->iov_len = p - (char*)cur_io->iov_base;
		cur_io++;
		if (!arg)
			break;
	}

 newline_ret:
	if (nflag) {
		cur_io->iov_base = (char*)"\n";
		cur_io->iov_len = 1;
		cur_io++;
	}
 ret:
	/* TODO: implement and use full_writev? */
	return writev(1, io, (cur_io - io)) >= 0;
}
#endif
