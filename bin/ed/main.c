/*	$OpenBSD: main.c,v 1.70 2024/10/12 07:58:40 jsg Exp $	*/
/*	$NetBSD: main.c,v 1.3 1995/03/21 09:04:44 cgd Exp $	*/

/* main.c: This file contains the main control and user-interface routines
   for the ed line editor. */
/*-
 * Copyright (c) 1993 Andrew Moore, Talke Studio.
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
 */

/*
 * CREDITS
 *
 *	This program is based on the editor algorithm described in
 *	Brian W. Kernighan and P. J. Plauger's book "Software Tools
 *	in Pascal," Addison-Wesley, 1981.
 *
 *	The buffering algorithm is attributed to Rodney Ruddock of
 *	the University of Guelph, Guelph, Ontario.
 *
 */

#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <limits.h>
#include <pwd.h>
#include <regex.h>
#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ed.h"

void signal_hup(int);
void signal_int(int);
void handle_winch(int);

static int next_addr(void);
static int check_addr_range(int, int);
static int get_matching_node_addr(regex_t *, int);
static char *get_filename(int);
static int get_shell_command(void);
static int append_lines(int);
static int join_lines(int, int);
static int move_lines(int);
static int copy_lines(int);
static int mark_line_node(line_t *, int);
static int get_marked_node_addr(int);
static line_t *dup_line_node(line_t *);

sigjmp_buf env;

/* static buffers */
static char errmsg[PATH_MAX + 40];	/* error message buffer */
static char *shcmd;		/* shell command buffer */
static int shcmdsz;		/* shell command buffer size */
static int shcmdi;		/* shell command buffer index */
static char old_filename[PATH_MAX];	/* default filename */

/* global buffers */
char *ibuf;			/* ed command-line buffer */
int ibufsz;			/* ed command-line buffer size */
char *ibufp;			/* pointer to ed command-line buffer */

/* global flags */
int garrulous = 0;		/* if set, print all error messages */
int isbinary;			/* if set, buffer contains ASCII NULs */
int isglobal;			/* if set, doing a global command */
int modified;			/* if set, buffer modified since last write */
int scripted = 0;		/* if set, suppress diagnostics */
int interactive = 0;		/* if set, we are in interactive mode */

volatile sig_atomic_t mutex = 0;  /* if set, signals set flags */
volatile sig_atomic_t sighup = 0; /* if set, sighup received while mutex set */
volatile sig_atomic_t sigint = 0; /* if set, sigint received while mutex set */

/* if set, signal handlers are enabled */
volatile sig_atomic_t sigactive = 0;

int current_addr;		/* current address in editor buffer */
int addr_last;			/* last address in editor buffer */
int lineno;			/* script line number */
static char *prompt;		/* command-line prompt */
static char *dps = "*";		/* default command-line prompt */

static const char usage[] = "usage: %s [-] [-s] [-p string] [file]\n";

static char *home;		/* home directory */

void
seterrmsg(char *s)
{
	strlcpy(errmsg, s, sizeof(errmsg));
}

/* ed: line editor */
int
main(volatile int argc, char ** volatile argv)
{
	int c, n;
	int status = 0;

	if (pledge("stdio rpath wpath cpath proc exec tty", NULL) == -1)
		err(1, "pledge");

	home = getenv("HOME");

top:
	while ((c = getopt(argc, argv, "p:sx")) != -1)
		switch (c) {
		case 'p':				/* set prompt */
			dps = prompt = optarg;
			break;
		case 's':				/* run script */
			scripted = 1;
			break;
		case 'x':				/* use crypt */
			fprintf(stderr, "crypt unavailable\n?\n");
			break;
		default:
			fprintf(stderr, usage, argv[0]);
			exit(1);
		}
	argv += optind;
	argc -= optind;
	if (argc && **argv == '-') {
		scripted = 1;
		if (argc > 1) {
			optind = 1;
			goto top;
		}
		argv++;
		argc--;
	}

	if (!(interactive = isatty(0))) {
		struct stat sb;

		/* assert: pipes show up as fifo's when fstat'd */
		if (fstat(STDIN_FILENO, &sb) || !S_ISFIFO(sb.st_mode)) {
			if (lseek(STDIN_FILENO, 0, SEEK_CUR)) {
				interactive = 1;
				setvbuf(stdout, NULL, _IOLBF, 0);
			}
		}
	}

	/* assert: reliable signals! */
	if (isatty(STDIN_FILENO)) {
		handle_winch(SIGWINCH);
		signal(SIGWINCH, handle_winch);
	}
	signal(SIGHUP, signal_hup);
	siginterrupt(SIGHUP, 1);
	signal(SIGQUIT, SIG_IGN);
	signal(SIGINT, signal_int);
	if (sigsetjmp(env, 1)) {
		status = -1;
		fputs("\n?\n", stdout);
		seterrmsg("interrupt");
	} else {
		init_buffers();
		sigactive = 1;			/* enable signal handlers */
		if (argc && **argv) {
			if (read_file(*argv, 0) < 0 && !interactive)
				quit(2);
			else if (**argv != '!')
				strlcpy(old_filename, *argv,
				    sizeof old_filename);
		} else if (argc) {
			fputs("?\n", stdout);
			if (**argv == '\0')
				seterrmsg("invalid filename");
			if (!interactive)
				quit(2);
		}
	}
	for (;;) {
		if (status < 0 && garrulous)
			fprintf(stderr, "%s\n", errmsg);
		if (prompt) {
			fputs(prompt, stdout);
			fflush(stdout);
		}
		if ((n = get_tty_line()) < 0) {
			status = ERR;
			continue;
		} else if (n == 0) {
			if (modified && !scripted) {
				fputs("?\n", stdout);
				seterrmsg("warning: file modified");
				if (!interactive) {
					if (garrulous)
						fprintf(stderr,
						    "script, line %d: %s\n",
						    lineno, errmsg);
					quit(2);
				}
				clearerr(stdin);
				modified = 0;
				status = EMOD;
				continue;
			} else
				quit(0);
		} else if (ibuf[n - 1] != '\n') {
			/* discard line */
			seterrmsg("unexpected end-of-file");
			clearerr(stdin);
			status = ERR;
			continue;
		}
		isglobal = 0;
		if ((status = extract_addr_range()) >= 0 &&
		    (status = exec_command()) >= 0)
			if (!status || (status &&
			    (status = display_lines(current_addr, current_addr,
				status)) >= 0))
				continue;
		switch (status) {
		case EOF:
			quit(0);
			break;
		case EMOD:
			modified = 0;
			fputs("?\n", stdout);		/* give warning */
			seterrmsg("warning: file modified");
			if (!interactive) {
				if (garrulous)
					fprintf(stderr,
					    "script, line %d: %s\n",
					    lineno, errmsg);
				quit(2);
			}
			break;
		case FATAL:
			if (!interactive) {
				if (garrulous)
					fprintf(stderr,
					    "script, line %d: %s\n",
					    lineno, errmsg);
			} else if (garrulous)
				fprintf(stderr, "%s\n", errmsg);
			quit(3);
			break;
		default:
			fputs("?\n", stdout);
			if (!interactive) {
				if (garrulous)
					fprintf(stderr,
					    "script, line %d: %s\n",
					    lineno, errmsg);
				quit(2);
			}
			break;
		}
	}
	/*NOTREACHED*/
}

int first_addr, second_addr, addr_cnt;

/* extract_addr_range: get line addresses from the command buffer until an
   illegal address is seen; return status */
int
extract_addr_range(void)
{
	int addr;

	addr_cnt = 0;
	first_addr = second_addr = current_addr;
	while ((addr = next_addr()) >= 0) {
		addr_cnt++;
		first_addr = second_addr;
		second_addr = addr;
		if (*ibufp != ',' && *ibufp != ';')
			break;
		else if (*ibufp++ == ';')
			current_addr = addr;
	}
	if ((addr_cnt = min(addr_cnt, 2)) == 1 || second_addr != addr)
		first_addr = second_addr;
	return (addr == ERR) ? ERR : 0;
}


#define	SKIP_BLANKS() \
	do { \
		while (isspace((unsigned char)*ibufp) && *ibufp != '\n') \
			ibufp++; \
	} while (0)

#define MUST_BE_FIRST() \
	do { \
		if (!first) { \
			seterrmsg("invalid address"); \
			return ERR; \
		} \
	} while (0)
	

/*  next_addr: return the next line address in the command buffer */
static int
next_addr(void)
{
	char *hd;
	int addr = current_addr;
	int n;
	int first = 1;
	int c;

	SKIP_BLANKS();
	for (hd = ibufp;; first = 0)
		switch ((c = (unsigned char)*ibufp)) {
		case '+':
		case '\t':
		case ' ':
		case '-':
		case '^':
			ibufp++;
			SKIP_BLANKS();
			if (isdigit((unsigned char)*ibufp)) {
				STRTOI(n, ibufp);
				addr += (c == '-' || c == '^') ? -n : n;
			} else if (!isspace(c))
				addr += (c == '-' || c == '^') ? -1 : 1;
			break;
		case '0': case '1': case '2':
		case '3': case '4': case '5':
		case '6': case '7': case '8': case '9':
			MUST_BE_FIRST();
			STRTOI(addr, ibufp);
			break;
		case '.':
		case '$':
			MUST_BE_FIRST();
			ibufp++;
			addr = (c == '.') ? current_addr : addr_last;
			break;
		case '/':
		case '?':
			MUST_BE_FIRST();
			if ((addr = get_matching_node_addr(
			    get_compiled_pattern(), c == '/')) < 0)
				return ERR;
			else if (c == *ibufp)
				ibufp++;
			break;
		case '\'':
			MUST_BE_FIRST();
			ibufp++;
			if ((addr = get_marked_node_addr((unsigned char)*ibufp++)) < 0)
				return ERR;
			break;
		case '%':
		case ',':
		case ';':
			if (first) {
				ibufp++;
				addr_cnt++;
				second_addr = (c == ';') ? current_addr : 1;
				if ((addr = next_addr()) < 0)
					addr = addr_last;
				break;
			}
			/* FALLTHROUGH */
		default:
			if (ibufp == hd)
				return EOF;
			else if (addr < 0 || addr_last < addr) {
				seterrmsg("invalid address");
				return ERR;
			} else
				return addr;
		}
	/* NOTREACHED */
}


/* GET_THIRD_ADDR: get a legal address from the command buffer */
#define GET_THIRD_ADDR(addr) \
	do { \
		int ol1, ol2; \
		\
		ol1 = first_addr; \
		ol2 = second_addr; \
		if (extract_addr_range() < 0) \
			return ERR; \
		else if (addr_cnt == 0) { \
			seterrmsg("destination expected"); \
			return ERR; \
		} else if (second_addr < 0 || addr_last < second_addr) { \
			seterrmsg("invalid address"); \
			return ERR; \
		} \
		addr = second_addr; \
		first_addr = ol1; \
		second_addr = ol2; \
	} while (0)


/* GET_COMMAND_SUFFIX: verify the command suffix in the command buffer */
#define GET_COMMAND_SUFFIX() \
	do { \
		int done = 0; \
		do { \
			switch (*ibufp) { \
			case 'p': \
				gflag |= GPR; \
				ibufp++; \
				break; \
			case 'l': \
				gflag |= GLS; \
				ibufp++; \
				break; \
			case 'n': \
				gflag |= GNP; \
				ibufp++; \
				break; \
			default: \
				done++; \
			} \
		} while (!done); \
		if (*ibufp++ != '\n') { \
			seterrmsg("invalid command suffix"); \
			return ERR; \
		} \
	} while (0)

/* sflags */
#define SGG 001		/* complement previous global substitute suffix */
#define SGP 002		/* complement previous print suffix */
#define SGR 004		/* use last regex instead of last pat */
#define SGF 010		/* repeat last substitution */

int patlock = 0;	/* if set, pattern not freed by get_compiled_pattern() */

volatile sig_atomic_t rows = 22;	/* scroll length: ws_row - 2 */
volatile sig_atomic_t cols = 72;	/* wrap column */

/* exec_command: execute the next command in command buffer; return print
   request, if any */
int
exec_command(void)
{
	extern int u_current_addr;
	extern int u_addr_last;

	static regex_t *pat = NULL;
	static int sgflag = 0;
	static int sgnum = 0;

	regex_t *tpat;
	char *fnp;
	int gflag = 0;
	int sflags = 0;
	int addr = 0;
	int n = 0;
	int c;

	SKIP_BLANKS();
	switch ((c = (unsigned char)*ibufp++)) {
	case 'a':
		GET_COMMAND_SUFFIX();
		if (!isglobal) clear_undo_stack();
		if (append_lines(second_addr) < 0)
			return ERR;
		break;
	case 'c':
		if (check_addr_range(current_addr, current_addr) < 0)
			return ERR;
		GET_COMMAND_SUFFIX();
		if (!isglobal) clear_undo_stack();
		if (delete_lines(first_addr, second_addr) < 0 ||
		    append_lines(current_addr) < 0)
			return ERR;
		break;
	case 'd':
		if (check_addr_range(current_addr, current_addr) < 0)
			return ERR;
		GET_COMMAND_SUFFIX();
		if (!isglobal) clear_undo_stack();
		if (delete_lines(first_addr, second_addr) < 0)
			return ERR;
		else if ((addr = INC_MOD(current_addr, addr_last)) != 0)
			current_addr = addr;
		break;
	case 'e':
		if (modified && !scripted)
			return EMOD;
		/* FALLTHROUGH */
	case 'E':
		if (addr_cnt > 0) {
			seterrmsg("unexpected address");
			return ERR;
		} else if (!isspace((unsigned char)*ibufp)) {
			seterrmsg("unexpected command suffix");
			return ERR;
		} else if ((fnp = get_filename(1)) == NULL)
			return ERR;
		GET_COMMAND_SUFFIX();
		if (delete_lines(1, addr_last) < 0)
			return ERR;
		clear_undo_stack();
		if (close_sbuf() < 0)
			return ERR;
		else if (open_sbuf() < 0)
			return FATAL;
		if (read_file(fnp, 0) < 0)
			return ERR;
		clear_undo_stack();
		modified = 0;
		u_current_addr = u_addr_last = -1;
		break;
	case 'f':
		if (addr_cnt > 0) {
			seterrmsg("unexpected address");
			return ERR;
		} else if (!isspace((unsigned char)*ibufp)) {
			seterrmsg("unexpected command suffix");
			return ERR;
		} else if ((fnp = get_filename(1)) == NULL)
			return ERR;
		else if (*fnp == '!') {
			seterrmsg("invalid redirection");
			return ERR;
		}
		GET_COMMAND_SUFFIX();
		puts(strip_escapes(fnp));
		break;
	case 'g':
	case 'v':
	case 'G':
	case 'V':
		if (isglobal) {
			seterrmsg("cannot nest global commands");
			return ERR;
		} else if (check_addr_range(1, addr_last) < 0)
			return ERR;
		else if (build_active_list(c == 'g' || c == 'G') < 0)
			return ERR;
		else if ((n = (c == 'G' || c == 'V')))
			GET_COMMAND_SUFFIX();
		isglobal++;
		if (exec_global(n, gflag) < 0)
			return ERR;
		break;
	case 'h':
		if (addr_cnt > 0) {
			seterrmsg("unexpected address");
			return ERR;
		}
		GET_COMMAND_SUFFIX();
		if (*errmsg) fprintf(stderr, "%s\n", errmsg);
		break;
	case 'H':
		if (addr_cnt > 0) {
			seterrmsg("unexpected address");
			return ERR;
		}
		GET_COMMAND_SUFFIX();
		if ((garrulous = 1 - garrulous) && *errmsg)
			fprintf(stderr, "%s\n", errmsg);
		break;
	case 'i':
		if (second_addr == 0) {
			second_addr = 1;
		}
		GET_COMMAND_SUFFIX();
		if (!isglobal) clear_undo_stack();
		if (append_lines(second_addr - 1) < 0)
			return ERR;
		break;
	case 'j':
		if (check_addr_range(current_addr, current_addr + 1) < 0)
			return ERR;
		GET_COMMAND_SUFFIX();
		if (!isglobal) clear_undo_stack();
		if (first_addr != second_addr &&
		    join_lines(first_addr, second_addr) < 0)
			return ERR;
		break;
	case 'k':
		c = (unsigned char)*ibufp++;
		if (second_addr == 0) {
			seterrmsg("invalid address");
			return ERR;
		}
		GET_COMMAND_SUFFIX();
		if (mark_line_node(get_addressed_line_node(second_addr), c) < 0)
			return ERR;
		break;
	case 'l':
		if (check_addr_range(current_addr, current_addr) < 0)
			return ERR;
		GET_COMMAND_SUFFIX();
		if (display_lines(first_addr, second_addr, gflag | GLS) < 0)
			return ERR;
		gflag = 0;
		break;
	case 'm':
		if (check_addr_range(current_addr, current_addr) < 0)
			return ERR;
		GET_THIRD_ADDR(addr);
		if (first_addr <= addr && addr < second_addr) {
			seterrmsg("invalid destination");
			return ERR;
		}
		GET_COMMAND_SUFFIX();
		if (!isglobal) clear_undo_stack();
		if (move_lines(addr) < 0)
			return ERR;
		break;
	case 'n':
		if (check_addr_range(current_addr, current_addr) < 0)
			return ERR;
		GET_COMMAND_SUFFIX();
		if (display_lines(first_addr, second_addr, gflag | GNP) < 0)
			return ERR;
		gflag = 0;
		break;
	case 'p':
		if (check_addr_range(current_addr, current_addr) < 0)
			return ERR;
		GET_COMMAND_SUFFIX();
		if (display_lines(first_addr, second_addr, gflag | GPR) < 0)
			return ERR;
		gflag = 0;
		break;
	case 'P':
		if (addr_cnt > 0) {
			seterrmsg("unexpected address");
			return ERR;
		}
		GET_COMMAND_SUFFIX();
		prompt = prompt ? NULL : optarg ? optarg : dps;
		break;
	case 'q':
	case 'Q':
		if (addr_cnt > 0) {
			seterrmsg("unexpected address");
			return ERR;
		}
		GET_COMMAND_SUFFIX();
		gflag =  (modified && !scripted && c == 'q') ? EMOD : EOF;
		break;
	case 'r':
		if (!isspace((unsigned char)*ibufp)) {
			seterrmsg("unexpected command suffix");
			return ERR;
		} else if (addr_cnt == 0)
			second_addr = addr_last;
		if ((fnp = get_filename(0)) == NULL)
			return ERR;
		GET_COMMAND_SUFFIX();
		if (!isglobal) clear_undo_stack();
		if ((addr = read_file(fnp, second_addr)) < 0)
			return ERR;
		else if (addr)
			modified = 1;
		break;
	case 's':
		do {
			switch (*ibufp) {
			case '\n':
				sflags |=SGF;
				break;
			case 'g':
				sflags |= SGG;
				ibufp++;
				break;
			case 'p':
				sflags |= SGP;
				ibufp++;
				break;
			case 'r':
				sflags |= SGR;
				ibufp++;
				break;
			case '0': case '1': case '2': case '3': case '4':
			case '5': case '6': case '7': case '8': case '9':
				STRTOI(sgnum, ibufp);
				sflags |= SGF;
				sgflag &= ~GSG;		/* override GSG */
				break;
			default:
				if (sflags) {
					seterrmsg("invalid command suffix");
					return ERR;
				}
			}
		} while (sflags && *ibufp != '\n');
		if (sflags && !pat) {
			seterrmsg("no previous substitution");
			return ERR;
		} else if (sflags & SGG)
			sgnum = 0;		/* override numeric arg */
		if (*ibufp != '\n' && *(ibufp + 1) == '\n') {
			seterrmsg("invalid pattern delimiter");
			return ERR;
		}
		tpat = pat;
		SPL1();
		if ((!sflags || (sflags & SGR)) &&
		    (tpat = get_compiled_pattern()) == NULL) {
		 	SPL0();
			return ERR;
		} else if (tpat != pat) {
			if (pat) {
				regfree(pat);
				free(pat);
			}
			pat = tpat;
			patlock = 1;		/* reserve pattern */
		}
		SPL0();
		if (!sflags && extract_subst_tail(&sgflag, &sgnum) < 0)
			return ERR;
		else if (isglobal)
			sgflag |= GLB;
		else
			sgflag &= ~GLB;
		if (sflags & SGG)
			sgflag ^= GSG;
		if (sflags & SGP) {
			sgflag ^= GPR;
			sgflag &= ~(GLS | GNP);
		}
		do {
			switch (*ibufp) {
			case 'p':
				sgflag |= GPR;
				ibufp++;
				break;
			case 'l':
				sgflag |= GLS;
				ibufp++;
				break;
			case 'n':
				sgflag |= GNP;
				ibufp++;
				break;
			default:
				n++;
			}
		} while (!n);
		if (check_addr_range(current_addr, current_addr) < 0)
			return ERR;
		GET_COMMAND_SUFFIX();
		if (!isglobal) clear_undo_stack();
		if (search_and_replace(pat, sgflag, sgnum) < 0)
			return ERR;
		break;
	case 't':
		if (check_addr_range(current_addr, current_addr) < 0)
			return ERR;
		GET_THIRD_ADDR(addr);
		GET_COMMAND_SUFFIX();
		if (!isglobal) clear_undo_stack();
		if (copy_lines(addr) < 0)
			return ERR;
		break;
	case 'u':
		if (addr_cnt > 0) {
			seterrmsg("unexpected address");
			return ERR;
		}
		GET_COMMAND_SUFFIX();
		if (pop_undo_stack() < 0)
			return ERR;
		break;
	case 'w':
	case 'W':
		if ((n = *ibufp) == 'q' || n == 'Q') {
			gflag = EOF;
			ibufp++;
		}
		if (!isspace((unsigned char)*ibufp)) {
			seterrmsg("unexpected command suffix");
			return ERR;
		} else if ((fnp = get_filename(0)) == NULL)
			return ERR;
		if (addr_cnt == 0 && !addr_last)
			first_addr = second_addr = 0;
		else if (check_addr_range(1, addr_last) < 0)
			return ERR;
		GET_COMMAND_SUFFIX();
		if ((addr = write_file(fnp, (c == 'W') ? "a" : "w",
		    first_addr, second_addr)) < 0)
			return ERR;
		else if (addr == addr_last && *fnp != '!')
			modified = 0;
		else if (modified && !scripted && n == 'q')
			gflag = EMOD;
		break;
	case 'x':
		if (addr_cnt > 0) {
			seterrmsg("unexpected address");
			return ERR;
		}
		GET_COMMAND_SUFFIX();
		seterrmsg("crypt unavailable");
		return ERR;
	case 'z':
		first_addr = 1;
		if (check_addr_range(first_addr, current_addr + 1) < 0)
			return ERR;
		else if ('0' < *ibufp && *ibufp <= '9')
			STRTOI(rows, ibufp);
		GET_COMMAND_SUFFIX();
		if (display_lines(second_addr, min(addr_last,
		    second_addr + rows), gflag) < 0)
			return ERR;
		gflag = 0;
		break;
	case '=':
		GET_COMMAND_SUFFIX();
		printf("%d\n", addr_cnt ? second_addr : addr_last);
		break;
	case '!':
		if (addr_cnt > 0) {
			seterrmsg("unexpected address");
			return ERR;
		} else if ((sflags = get_shell_command()) < 0)
			return ERR;
		GET_COMMAND_SUFFIX();
		if (sflags) printf("%s\n", shcmd + 1);
		fflush(NULL); /* flush any buffered I/O */
		system(shcmd + 1);
		if (!scripted) printf("!\n");
		break;
	case '\n':
		first_addr = 1;
		if (check_addr_range(first_addr, current_addr + 1) < 0
		 || display_lines(second_addr, second_addr, 0) < 0)
			return ERR;
		break;
	default:
		seterrmsg("unknown command");
		return ERR;
	}
	return gflag;
}


/* check_addr_range: return status of address range check */
static int
check_addr_range(int n, int m)
{
	if (addr_cnt == 0) {
		first_addr = n;
		second_addr = m;
	}
	if (first_addr > second_addr || 1 > first_addr ||
	    second_addr > addr_last) {
		seterrmsg("invalid address");
		return ERR;
	}
	return 0;
}


/* get_matching_node_addr: return the address of the next line matching a
   pattern in a given direction.  wrap around begin/end of editor buffer if
   necessary */
static int
get_matching_node_addr(regex_t *pat, int dir)
{
	char *s;
	int n = current_addr;
	line_t *lp;

	if (!pat) return ERR;
	do {
		if ((n = dir ? INC_MOD(n, addr_last) : DEC_MOD(n, addr_last))) {
			lp = get_addressed_line_node(n);
			if ((s = get_sbuf_line(lp)) == NULL)
				return ERR;
			if (isbinary)
				NUL_TO_NEWLINE(s, lp->len);
			if (!regexec(pat, s, 0, NULL, 0))
				return n;
		}
	} while (n != current_addr);
	seterrmsg("no match");
	return  ERR;
}


/* get_filename: return pointer to copy of filename in the command buffer */
static char *
get_filename(int save)
{
	static char filename[PATH_MAX];
	char *p;
	int n;

	if (*ibufp != '\n') {
		SKIP_BLANKS();
		if (*ibufp == '\n') {
			seterrmsg("invalid filename");
			return NULL;
		} else if ((ibufp = get_extended_line(&n, 1)) == NULL)
			return NULL;
		else if (*ibufp == '!') {
			ibufp++;
			if ((n = get_shell_command()) < 0)
				return NULL;
			if (n) printf("%s\n", shcmd + 1);
			return shcmd;
		} else if (n >= PATH_MAX - 1) {
			seterrmsg("filename too long");
			return NULL;
		}
	} else {
		if (*old_filename == '\0') {
			seterrmsg("no current filename");
			return  NULL;
		}
		return old_filename;
	}

	p = save ? old_filename : *old_filename ? filename : old_filename;
	for (n = 0; *ibufp != '\n';)
		p[n++] = *ibufp++;
	p[n] = '\0';
	return p;
}


/* get_shell_command: read a shell command from stdin; return substitution
   status */
static int
get_shell_command(void)
{
	static char *buf = NULL;
	static int n = 0;

	char *s;			/* substitution char pointer */
	int i = 0;
	int j = 0;

	if ((s = ibufp = get_extended_line(&j, 1)) == NULL)
		return ERR;
	REALLOC(buf, n, j + 1, ERR);
	buf[i++] = '!';			/* prefix command w/ bang */
	while (*ibufp != '\n')
		switch (*ibufp) {
		default:
			REALLOC(buf, n, i + 2, ERR);
			buf[i++] = *ibufp;
			if (*ibufp++ == '\\')
				buf[i++] = *ibufp++;
			break;
		case '!':
			if (s != ibufp) {
				REALLOC(buf, n, i + 1, ERR);
				buf[i++] = *ibufp++;
			}
			else if (shcmd == NULL)
			{
				seterrmsg("no previous command");
				return ERR;
			} else {
				REALLOC(buf, n, i + shcmdi, ERR);
				for (s = shcmd + 1; s < shcmd + shcmdi;)
					buf[i++] = *s++;
				s = ibufp++;
			}
			break;
		case '%':
			if (*old_filename  == '\0') {
				seterrmsg("no current filename");
				return ERR;
			}
			j = strlen(s = strip_escapes(old_filename));
			REALLOC(buf, n, i + j, ERR);
			while (j--)
				buf[i++] = *s++;
			s = ibufp++;
			break;
		}
	if (i == 1) {
		seterrmsg("no command");
		return ERR;
	}
	REALLOC(shcmd, shcmdsz, i + 1, ERR);
	memcpy(shcmd, buf, i);
	shcmd[shcmdi = i] = '\0';
	return *s == '!' || *s == '%';
}


/* append_lines: insert text from stdin to after line n; stop when either a
   single period is read or EOF; return status */
static int
append_lines(int n)
{
	int l;
	char *lp = ibuf;
	char *eot;
	undo_t *up = NULL;

	for (current_addr = n;;) {
		if (!isglobal) {
			if ((l = get_tty_line()) < 0)
				return ERR;
			else if (l == 0 || ibuf[l - 1] != '\n') {
				clearerr(stdin);
				return  l ? EOF : 0;
			}
			lp = ibuf;
		} else if (*(lp = ibufp) == '\0')
			return 0;
		else {
			while (*ibufp++ != '\n')
				;
			l = ibufp - lp;
		}
		if (l == 2 && lp[0] == '.' && lp[1] == '\n') {
			return 0;
		}
		eot = lp + l;
		SPL1();
		do {
			if ((lp = put_sbuf_line(lp)) == NULL) {
				SPL0();
				return ERR;
			} else if (up)
				up->t = get_addressed_line_node(current_addr);
			else if ((up = push_undo_stack(UADD, current_addr,
			    current_addr)) == NULL) {
				SPL0();
				return ERR;
			}
		} while (lp != eot);
		modified = 1;
		SPL0();
	}
	/* NOTREACHED */
}


/* join_lines: replace a range of lines with the joined text of those lines */
static int
join_lines(int from, int to)
{
	static char *buf = NULL;
	static int n;

	char *s;
	int size = 0;
	line_t *bp, *ep;

	ep = get_addressed_line_node(INC_MOD(to, addr_last));
	bp = get_addressed_line_node(from);
	for (; bp != ep; bp = bp->q_forw) {
		if ((s = get_sbuf_line(bp)) == NULL)
			return ERR;
		REALLOC(buf, n, size + bp->len, ERR);
		memcpy(buf + size, s, bp->len);
		size += bp->len;
	}
	REALLOC(buf, n, size + 2, ERR);
	memcpy(buf + size, "\n", 2);
	if (delete_lines(from, to) < 0)
		return ERR;
	current_addr = from - 1;
	SPL1();
	if (put_sbuf_line(buf) == NULL ||
	    push_undo_stack(UADD, current_addr, current_addr) == NULL) {
		SPL0();
		return ERR;
	}
	modified = 1;
	SPL0();
	return 0;
}


/* move_lines: move a range of lines */
static int
move_lines(int addr)
{
	line_t *b1, *a1, *b2, *a2;
	int n = INC_MOD(second_addr, addr_last);
	int p = first_addr - 1;
	int done = (addr == first_addr - 1 || addr == second_addr);

	SPL1();
	if (done) {
		a2 = get_addressed_line_node(n);
		b2 = get_addressed_line_node(p);
		current_addr = second_addr;
	} else if (push_undo_stack(UMOV, p, n) == NULL ||
	    push_undo_stack(UMOV, addr, INC_MOD(addr, addr_last)) == NULL) {
		SPL0();
		return ERR;
	} else {
		a1 = get_addressed_line_node(n);
		if (addr < first_addr) {
			b1 = get_addressed_line_node(p);
			b2 = get_addressed_line_node(addr);
					/* this get_addressed_line_node last! */
		} else {
			b2 = get_addressed_line_node(addr);
			b1 = get_addressed_line_node(p);
					/* this get_addressed_line_node last! */
		}
		a2 = b2->q_forw;
		REQUE(b2, b1->q_forw);
		REQUE(a1->q_back, a2);
		REQUE(b1, a1);
		current_addr = addr + ((addr < first_addr) ?
		    second_addr - first_addr + 1 : 0);
	}
	if (isglobal)
		unset_active_nodes(b2->q_forw, a2);
	modified = 1;
	SPL0();
	return 0;
}


/* copy_lines: copy a range of lines; return status */
static int
copy_lines(int addr)
{
	line_t *lp, *np = get_addressed_line_node(first_addr);
	undo_t *up = NULL;
	int n = second_addr - first_addr + 1;
	int m = 0;

	current_addr = addr;
	if (first_addr <= addr && addr < second_addr) {
		n =  addr - first_addr + 1;
		m = second_addr - addr;
	}
	for (; n > 0; n=m, m=0, np = get_addressed_line_node(current_addr + 1))
		for (; n-- > 0; np = np->q_forw) {
			SPL1();
			if ((lp = dup_line_node(np)) == NULL) {
				SPL0();
				return ERR;
			}
			add_line_node(lp);
			if (up)
				up->t = lp;
			else if ((up = push_undo_stack(UADD, current_addr,
			    current_addr)) == NULL) {
				SPL0();
				return ERR;
			}
			modified = 1;
			SPL0();
		}
	return 0;
}


/* delete_lines: delete a range of lines */
int
delete_lines(int from, int to)
{
	line_t *n, *p;

	SPL1();
	if (push_undo_stack(UDEL, from, to) == NULL) {
		SPL0();
		return ERR;
	}
	n = get_addressed_line_node(INC_MOD(to, addr_last));
	p = get_addressed_line_node(from - 1);
					/* this get_addressed_line_node last! */
	if (isglobal)
		unset_active_nodes(p->q_forw, n);
	REQUE(p, n);
	addr_last -= to - from + 1;
	current_addr = from - 1;
	modified = 1;
	SPL0();
	return 0;
}


/* display_lines: print a range of lines to stdout */
int
display_lines(int from, int to, int gflag)
{
	line_t *bp;
	line_t *ep;
	char *s;

	if (!from) {
		seterrmsg("invalid address");
		return ERR;
	}
	ep = get_addressed_line_node(INC_MOD(to, addr_last));
	bp = get_addressed_line_node(from);
	for (; bp != ep; bp = bp->q_forw) {
		if ((s = get_sbuf_line(bp)) == NULL)
			return ERR;
		if (put_tty_line(s, bp->len, current_addr = from++, gflag) < 0)
			return ERR;
	}
	return 0;
}


#define MAXMARK 26			/* max number of marks */

static line_t *mark[MAXMARK];		/* line markers */
static int markno;			/* line marker count */

/* mark_line_node: set a line node mark */
static int
mark_line_node(line_t *lp, int n)
{
	if (!islower(n)) {
		seterrmsg("invalid mark character");
		return ERR;
	} else if (mark[n - 'a'] == NULL)
		markno++;
	mark[n - 'a'] = lp;
	return 0;
}


/* get_marked_node_addr: return address of a marked line */
static int
get_marked_node_addr(int n)
{
	if (!islower(n)) {
		seterrmsg("invalid mark character");
		return ERR;
	}
	return get_line_node_addr(mark[n - 'a']);
}


/* unmark_line_node: clear line node mark */
void
unmark_line_node(line_t *lp)
{
	int i;

	for (i = 0; markno && i < MAXMARK; i++)
		if (mark[i] == lp) {
			mark[i] = NULL;
			markno--;
		}
}


/* dup_line_node: return a pointer to a copy of a line node */
static line_t *
dup_line_node(line_t *lp)
{
	line_t *np;

	if ((np = malloc(sizeof(line_t))) == NULL) {
		perror(NULL);
		seterrmsg("out of memory");
		return NULL;
	}
	np->seek = lp->seek;
	np->len = lp->len;
	return np;
}


/* has_trailing_escape:  return the parity of escapes preceding a character
   in a string */
int
has_trailing_escape(char *s, char *t)
{
    return (s == t || *(t - 1) != '\\') ? 0 : !has_trailing_escape(s, t - 1);
}


/* strip_escapes: return copy of escaped string of at most length PATH_MAX */
char *
strip_escapes(char *s)
{
	static char *file = NULL;
	static int filesz = 0;

	int i = 0;

	REALLOC(file, filesz, PATH_MAX, NULL);
	/* assert: no trailing escape */
	while ((file[i++] = (*s == '\\') ? *++s : *s) != '\0' &&
	       i < PATH_MAX-1)
		s++;
	file[PATH_MAX-1] = '\0';
	return file;
}


void
signal_hup(int signo)
{
	sighup = 1;
}


void
signal_int(int signo)
{
	if (mutex)
		sigint = 1;
	else
		handle_int(signo);	/* XXX quite unsafe */
}


void
handle_hup(void)
{
	char hup[PATH_MAX];

	signal(SIGHUP, SIG_IGN);
	sighup = 0;
	if (addr_last && write_file("ed.hup", "w", 1, addr_last) < 0 &&
	    home != NULL && home[0] == '/') {
		if (strlcpy(hup, home, sizeof(hup)) < sizeof(hup) &&
		    strlcat(hup, "/ed.hup", sizeof(hup)) < sizeof(hup))
			write_file(hup, "w", 1, addr_last);
	}
	exit(2);
}


void
handle_int(int signo)
{
	if (!sigactive)
		_exit(1);
	sigint = 0;
	siglongjmp(env, -1);
}


void
handle_winch(int signo)
{
	int save_errno = errno;
	struct winsize ws;		/* window size structure */

	if (ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) == 0) {
		if (ws.ws_row > 2)
			rows = ws.ws_row - 2;
		if (ws.ws_col > 8)
			cols = ws.ws_col - 8;
	}
	errno = save_errno;
}
