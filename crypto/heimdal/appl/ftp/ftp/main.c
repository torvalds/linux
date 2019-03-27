/*
 * Copyright (c) 1985, 1989, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
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
 */

/*
 * FTP User Program -- Command Interface.
 */

#include "ftp_locl.h"
#include <getarg.h>

RCSID("$Id$");

static int help_flag;
static int version_flag;
static int debug_flag;

struct getargs getargs[] = {
    { NULL,	'd', arg_flag, &debug_flag,
      "debug", NULL },
    { NULL,	'g', arg_negative_flag, &doglob,
      "disables globbing", NULL},
    { NULL,	'i', arg_negative_flag, &interactive,
      "Turn off interactive prompting", NULL},
    { NULL,	'l', arg_negative_flag, &lineedit,
      "Turn off line editing", NULL},
    { NULL,   'n', arg_negative_flag, &autologin,
      "Turn off auto-login", NULL},
    { NULL,	'p', arg_flag, &passivemode,
      "passive mode", NULL},
    { NULL,	't', arg_counter, &trace,
      "Packet tracing", NULL},
#ifdef KRB5
    { "gss-bindings", 0,  arg_negative_flag, &ftp_do_gss_bindings,
      "Don't use GSS-API bindings", NULL},
    { "gss-delegate", 0,  arg_negative_flag, &ftp_do_gss_delegate,
      "Disable delegation of GSS-API credentials", NULL},
#endif
    { NULL,	'v', arg_counter, &verbose,
      "verbosity", NULL},
    { NULL,	'K', arg_negative_flag, &use_kerberos,
      "Disable kerberos authentication", NULL},
    { "encrypt", 'x', arg_flag, &doencrypt,
      "Encrypt command and data channel if possible" },
    { "version", 0,  arg_flag, &version_flag },
    { "help",	'h', arg_flag, &help_flag },
};

static int num_args = sizeof(getargs) / sizeof(getargs[0]);

static void
usage(int ecode)
{
    arg_printusage(getargs, num_args, NULL, "[host [port]]");
    exit(ecode);
}

int
main(int argc, char **argv)
{
	int top;
	struct passwd *pw = NULL;
	char homedir[MaxPathLen];
	struct servent *sp;
	int optind = 0;

	setprogname(argv[0]);

	sp = getservbyname("ftp", "tcp");
	if (sp == 0)
		errx(1, "ftp/tcp: unknown service");
	doglob = 1;
	interactive = 1;
	autologin = 1;
	lineedit = 1;
	passivemode = 0; /* passive mode not active */
        use_kerberos = 1;
#ifdef KRB5
	ftp_do_gss_bindings = 1;
#endif

	if(getarg(getargs, num_args, argc, argv, &optind))
		usage(1);
	if(help_flag)
		usage(0);
	if(version_flag) {
		print_version(NULL);
		exit(0);
	}

	if (debug_flag) {
		options |= SO_DEBUG;
		debug++;
	}

	argc -= optind;
	argv += optind;

	fromatty = isatty(fileno(stdin));
	if (fromatty)
		verbose++;
	cpend = 0;	/* no pending replies */
	proxy = 0;	/* proxy not active */
	crflag = 1;	/* strip c.r. on ascii gets */
	sendport = -1;	/* not using ports */
	/*
	 * Set up the home directory in case we're globbing.
	 */
	pw = k_getpwuid(getuid());
	if (pw != NULL) {
		strlcpy(homedir, pw->pw_dir, sizeof(homedir));
		home = homedir;
	}
	if (argc > 0) {
	    char *xargv[5];

	    if (setjmp(toplevel))
		exit(0);
	    signal(SIGINT, intr);
	    signal(SIGPIPE, lostpeer);
	    xargv[0] = (char*)getprogname();
	    xargv[1] = argv[0];
	    xargv[2] = argv[1];
	    xargv[3] = argv[2];
	    xargv[4] = NULL;
	    setpeer(argc+1, xargv);
	}
	if(setjmp(toplevel) == 0)
	    top = 1;
	else
	    top = 0;
	if (top) {
	    signal(SIGINT, intr);
	    signal(SIGPIPE, lostpeer);
	}
	for (;;) {
	    cmdscanner(top);
	    top = 1;
	}
}

void
intr(int sig)
{

	longjmp(toplevel, 1);
}

#ifndef SHUT_RDWR
#define SHUT_RDWR 2
#endif

RETSIGTYPE
lostpeer(int sig)
{

    if (connected) {
	if (cout != NULL) {
	    shutdown(fileno(cout), SHUT_RDWR);
	    fclose(cout);
	    cout = NULL;
	}
	if (data >= 0) {
	    shutdown(data, SHUT_RDWR);
	    close(data);
	    data = -1;
	}
	connected = 0;
    }
    pswitch(1);
    if (connected) {
	if (cout != NULL) {
	    shutdown(fileno(cout), SHUT_RDWR);
	    fclose(cout);
	    cout = NULL;
	}
	connected = 0;
    }
    proxflag = 0;
    pswitch(0);
    sec_end();
    SIGRETURN(0);
}

/*
char *
tail(filename)
	char *filename;
{
	char *s;

	while (*filename) {
		s = strrchr(filename, '/');
		if (s == NULL)
			break;
		if (s[1])
			return (s + 1);
		*s = '\0';
	}
	return (filename);
}
*/

static char *
simple_readline(char *prompt)
{
    char buf[BUFSIZ];
    printf ("%s", prompt);
    fflush (stdout);
    if(fgets(buf, sizeof(buf), stdin) == NULL)
	return NULL;
    if (buf[strlen(buf) - 1] == '\n')
	buf[strlen(buf) - 1] = '\0';
    return strdup(buf);
}

#ifndef HAVE_READLINE

static char *
readline(char *prompt)
{
    return simple_readline (prompt);
}

static void
add_history(char *p)
{
}

#else

/* These should not really be here */

char *readline(char *);
void add_history(char *);

#endif

/*
 * Command parser.
 */
void
cmdscanner(int top)
{
    struct cmd *c;
    int l;

    if (!top)
	putchar('\n');
    for (;;) {
	if (fromatty) {
	    char *p;
	    if (lineedit)
		p = readline("ftp> ");
	    else
		p = simple_readline("ftp> ");
	    if(p == NULL) {
		printf("\n");
		quit(0, 0);
	    }
	    strlcpy(line, p, sizeof(line));
	    if (lineedit)
		add_history(p);
	    free(p);
	} else{
	    if (fgets(line, sizeof line, stdin) == NULL)
		quit(0, 0);
	}
	/* XXX will break on long lines */
	l = strlen(line);
	if (l == 0)
	    break;
	if (line[--l] == '\n') {
	    if (l == 0)
		break;
	    line[l] = '\0';
	} else if (l == sizeof(line) - 2) {
	    printf("sorry, input line too long\n");
	    while ((l = getchar()) != '\n' && l != EOF)
		/* void */;
	    break;
	} /* else it was a line without a newline */
	makeargv();
	if (margc == 0) {
	    continue;
	}
	c = getcmd(margv[0]);
	if (c == (struct cmd *)-1) {
	    printf("?Ambiguous command\n");
	    continue;
	}
	if (c == 0) {
	    printf("?Invalid command\n");
	    continue;
	}
	if (c->c_conn && !connected) {
	    printf("Not connected.\n");
	    continue;
	}
	(*c->c_handler)(margc, margv);
	if (bell && c->c_bell)
	    putchar('\007');
	if (c->c_handler != help)
	    break;
    }
    signal(SIGINT, intr);
    signal(SIGPIPE, lostpeer);
}

struct cmd *
getcmd(char *name)
{
	char *p, *q;
	struct cmd *c, *found;
	int nmatches, longest;

	longest = 0;
	nmatches = 0;
	found = 0;
	for (c = cmdtab; (p = c->c_name); c++) {
		for (q = name; *q == *p++; q++)
			if (*q == 0)		/* exact match? */
				return (c);
		if (!*q) {			/* the name was a prefix */
			if (q - name > longest) {
				longest = q - name;
				nmatches = 1;
				found = c;
			} else if (q - name == longest)
				nmatches++;
		}
	}
	if (nmatches > 1)
		return ((struct cmd *)-1);
	return (found);
}

/*
 * Slice a string up into argc/argv.
 */

int slrflag;

void
makeargv(void)
{
	char **argp;

	argp = margv;
	stringbase = line;		/* scan from first of buffer */
	argbase = argbuf;		/* store from first of buffer */
	slrflag = 0;
	for (margc = 0; ; margc++) {
		/* Expand array if necessary */
		if (margc == margvlen) {
			int i;

			margv = (margvlen == 0)
				? (char **)malloc(20 * sizeof(char *))
				: (char **)realloc(margv,
					(margvlen + 20)*sizeof(char *));
			if (margv == NULL)
				errx(1, "cannot realloc argv array");
			for(i = margvlen; i < margvlen + 20; ++i)
				margv[i] = NULL;
			margvlen += 20;
			argp = margv + margc;
		}

		if ((*argp++ = slurpstring()) == NULL)
			break;
	}

}

/*
 * Parse string into argbuf;
 * implemented with FSM to
 * handle quoting and strings
 */
char *
slurpstring(void)
{
	int got_one = 0;
	char *sb = stringbase;
	char *ap = argbase;
	char *tmp = argbase;		/* will return this if token found */

	if (*sb == '!' || *sb == '$') {	/* recognize ! as a token for shell */
		switch (slrflag) {	/* and $ as token for macro invoke */
			case 0:
				slrflag++;
				stringbase++;
				return ((*sb == '!') ? "!" : "$");
				/* NOTREACHED */
			case 1:
				slrflag++;
				altarg = stringbase;
				break;
			default:
				break;
		}
	}

S0:
	switch (*sb) {

	case '\0':
		goto OUT;

	case ' ':
	case '\t':
		sb++; goto S0;

	default:
		switch (slrflag) {
			case 0:
				slrflag++;
				break;
			case 1:
				slrflag++;
				altarg = sb;
				break;
			default:
				break;
		}
		goto S1;
	}

S1:
	switch (*sb) {

	case ' ':
	case '\t':
	case '\0':
		goto OUT;	/* end of token */

	case '\\':
		sb++; goto S2;	/* slurp next character */

	case '"':
		sb++; goto S3;	/* slurp quoted string */

	default:
		*ap++ = *sb++;	/* add character to token */
		got_one = 1;
		goto S1;
	}

S2:
	switch (*sb) {

	case '\0':
		goto OUT;

	default:
		*ap++ = *sb++;
		got_one = 1;
		goto S1;
	}

S3:
	switch (*sb) {

	case '\0':
		goto OUT;

	case '"':
		sb++; goto S1;

	default:
		*ap++ = *sb++;
		got_one = 1;
		goto S3;
	}

OUT:
	if (got_one)
		*ap++ = '\0';
	argbase = ap;			/* update storage pointer */
	stringbase = sb;		/* update scan pointer */
	if (got_one) {
		return (tmp);
	}
	switch (slrflag) {
		case 0:
			slrflag++;
			break;
		case 1:
			slrflag++;
			altarg = (char *) 0;
			break;
		default:
			break;
	}
	return NULL;
}

#define HELPINDENT ((int) sizeof ("directory"))

/*
 * Help command.
 * Call each command handler with argc == 0 and argv[0] == name.
 */
void
help(int argc, char **argv)
{
	struct cmd *c;

	if (argc == 1) {
		int i, j, w, k;
		int columns, width = 0, lines;

		printf("Commands may be abbreviated.  Commands are:\n\n");
		for (c = cmdtab; c < &cmdtab[NCMDS]; c++) {
			int len = strlen(c->c_name);

			if (len > width)
				width = len;
		}
		width = (width + 8) &~ 7;
		columns = 80 / width;
		if (columns == 0)
			columns = 1;
		lines = (NCMDS + columns - 1) / columns;
		for (i = 0; i < lines; i++) {
			for (j = 0; j < columns; j++) {
				c = cmdtab + j * lines + i;
				if ((!proxy || c->c_proxy)) {
					printf("%s", c->c_name);
				} else {
					for (k=0; k < strlen(c->c_name); k++) {
						putchar(' ');
					}
				}
				if (c + lines >= &cmdtab[NCMDS]) {
					printf("\n");
					break;
				}
				w = strlen(c->c_name);
				while (w < width) {
					w = (w + 8) &~ 7;
					putchar('\t');
				}
			}
		}
		return;
	}
	while (--argc > 0) {
		char *arg;
		arg = *++argv;
		c = getcmd(arg);
		if (c == (struct cmd *)-1)
			printf("?Ambiguous help command %s\n", arg);
		else if (c == (struct cmd *)0)
			printf("?Invalid help command %s\n", arg);
		else
			printf("%-*s\t%s\n", HELPINDENT,
				c->c_name, c->c_help);
	}
}
