/*	$NetBSD: main.c,v 1.273 2017/10/28 21:54:54 sjg Exp $	*/

/*
 * Copyright (c) 1988, 1989, 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Adam de Boor.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
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
 * Copyright (c) 1989 by Berkeley Softworks
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Adam de Boor.
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

#ifndef MAKE_NATIVE
static char rcsid[] = "$NetBSD: main.c,v 1.273 2017/10/28 21:54:54 sjg Exp $";
#else
#include <sys/cdefs.h>
#ifndef lint
__COPYRIGHT("@(#) Copyright (c) 1988, 1989, 1990, 1993\
 The Regents of the University of California.  All rights reserved.");
#endif /* not lint */

#ifndef lint
#if 0
static char sccsid[] = "@(#)main.c	8.3 (Berkeley) 3/19/94";
#else
__RCSID("$NetBSD: main.c,v 1.273 2017/10/28 21:54:54 sjg Exp $");
#endif
#endif /* not lint */
#endif

/*-
 * main.c --
 *	The main file for this entire program. Exit routines etc
 *	reside here.
 *
 * Utility functions defined in this file:
 *	Main_ParseArgLine	Takes a line of arguments, breaks them and
 *				treats them as if they were given when first
 *				invoked. Used by the parse module to implement
 *				the .MFLAGS target.
 *
 *	Error			Print a tagged error message. The global
 *				MAKE variable must have been defined. This
 *				takes a format string and optional arguments
 *				for it.
 *
 *	Fatal			Print an error message and exit. Also takes
 *				a format string and arguments for it.
 *
 *	Punt			Aborts all jobs and exits with a message. Also
 *				takes a format string and arguments for it.
 *
 *	Finish			Finish things up by printing the number of
 *				errors which occurred, as passed to it, and
 *				exiting.
 */

#include <sys/types.h>
#include <sys/time.h>
#include <sys/param.h>
#include <sys/resource.h>
#include <sys/stat.h>
#if defined(MAKE_NATIVE) && defined(HAVE_SYSCTL)
#include <sys/sysctl.h>
#endif
#include <sys/utsname.h>
#include "wait.h"

#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <ctype.h>

#include "make.h"
#include "hash.h"
#include "dir.h"
#include "job.h"
#include "pathnames.h"
#include "trace.h"

#ifdef USE_IOVEC
#include <sys/uio.h>
#endif

#ifndef	DEFMAXLOCAL
#define	DEFMAXLOCAL DEFMAXJOBS
#endif	/* DEFMAXLOCAL */

#ifndef __arraycount
# define __arraycount(__x)	(sizeof(__x) / sizeof(__x[0]))
#endif

Lst			create;		/* Targets to be made */
time_t			now;		/* Time at start of make */
GNode			*DEFAULT;	/* .DEFAULT node */
Boolean			allPrecious;	/* .PRECIOUS given on line by itself */
Boolean			deleteOnError;	/* .DELETE_ON_ERROR: set */

static Boolean		noBuiltins;	/* -r flag */
static Lst		makefiles;	/* ordered list of makefiles to read */
static int		printVars;	/* -[vV] argument */
#define COMPAT_VARS 1
#define EXPAND_VARS 2
static Lst		variables;	/* list of variables to print */
int			maxJobs;	/* -j argument */
static int		maxJobTokens;	/* -j argument */
Boolean			compatMake;	/* -B argument */
int			debug;		/* -d argument */
Boolean			debugVflag;	/* -dV */
Boolean			noExecute;	/* -n flag */
Boolean			noRecursiveExecute;	/* -N flag */
Boolean			keepgoing;	/* -k flag */
Boolean			queryFlag;	/* -q flag */
Boolean			touchFlag;	/* -t flag */
Boolean			enterFlag;	/* -w flag */
Boolean			enterFlagObj;	/* -w and objdir != srcdir */
Boolean			ignoreErrors;	/* -i flag */
Boolean			beSilent;	/* -s flag */
Boolean			oldVars;	/* variable substitution style */
Boolean			checkEnvFirst;	/* -e flag */
Boolean			parseWarnFatal;	/* -W flag */
Boolean			jobServer; 	/* -J flag */
static int jp_0 = -1, jp_1 = -1;	/* ends of parent job pipe */
Boolean			varNoExportEnv;	/* -X flag */
Boolean			doing_depend;	/* Set while reading .depend */
static Boolean		jobsRunning;	/* TRUE if the jobs might be running */
static const char *	tracefile;
static void		MainParseArgs(int, char **);
static int		ReadMakefile(const void *, const void *);
static void		usage(void) MAKE_ATTR_DEAD;
static void		purge_cached_realpaths(void);

static Boolean		ignorePWD;	/* if we use -C, PWD is meaningless */
static char objdir[MAXPATHLEN + 1];	/* where we chdir'ed to */
char curdir[MAXPATHLEN + 1];		/* Startup directory */
char *progname;				/* the program name */
char *makeDependfile;
pid_t myPid;
int makelevel;

Boolean forceJobs = FALSE;

/*
 * On some systems MACHINE is defined as something other than
 * what we want.
 */
#ifdef FORCE_MACHINE
# undef MACHINE
# define MACHINE FORCE_MACHINE
#endif

extern Lst parseIncPath;

/*
 * For compatibility with the POSIX version of MAKEFLAGS that includes
 * all the options with out -, convert flags to -f -l -a -g -s.
 */
static char *
explode(const char *flags)
{
    size_t len;
    char *nf, *st;
    const char *f;

    if (flags == NULL)
	return NULL;

    for (f = flags; *f; f++)
	if (!isalpha((unsigned char)*f))
	    break;

    if (*f)
	return bmake_strdup(flags);

    len = strlen(flags);
    st = nf = bmake_malloc(len * 3 + 1);
    while (*flags) {
	*nf++ = '-';
	*nf++ = *flags++;
	*nf++ = ' ';
    }
    *nf = '\0';
    return st;
}
	    
static void
parse_debug_options(const char *argvalue)
{
	const char *modules;
	const char *mode;
	char *fname;
	int len;

	for (modules = argvalue; *modules; ++modules) {
		switch (*modules) {
		case 'A':
			debug = ~0;
			break;
		case 'a':
			debug |= DEBUG_ARCH;
			break;
		case 'C':
			debug |= DEBUG_CWD;
			break;
		case 'c':
			debug |= DEBUG_COND;
			break;
		case 'd':
			debug |= DEBUG_DIR;
			break;
		case 'e':
			debug |= DEBUG_ERROR;
			break;
		case 'f':
			debug |= DEBUG_FOR;
			break;
		case 'g':
			if (modules[1] == '1') {
				debug |= DEBUG_GRAPH1;
				++modules;
			}
			else if (modules[1] == '2') {
				debug |= DEBUG_GRAPH2;
				++modules;
			}
			else if (modules[1] == '3') {
				debug |= DEBUG_GRAPH3;
				++modules;
			}
			break;
		case 'j':
			debug |= DEBUG_JOB;
			break;
		case 'l':
			debug |= DEBUG_LOUD;
			break;
		case 'M':
			debug |= DEBUG_META;
			break;
		case 'm':
			debug |= DEBUG_MAKE;
			break;
		case 'n':
			debug |= DEBUG_SCRIPT;
			break;
		case 'p':
			debug |= DEBUG_PARSE;
			break;
		case 's':
			debug |= DEBUG_SUFF;
			break;
		case 't':
			debug |= DEBUG_TARG;
			break;
		case 'V':
			debugVflag = TRUE;
			break;
		case 'v':
			debug |= DEBUG_VAR;
			break;
		case 'x':
			debug |= DEBUG_SHELL;
			break;
		case 'F':
			if (debug_file != stdout && debug_file != stderr)
				fclose(debug_file);
			if (*++modules == '+') {
				modules++;
				mode = "a";
			} else
				mode = "w";
			if (strcmp(modules, "stdout") == 0) {
				debug_file = stdout;
				goto debug_setbuf;
			}
			if (strcmp(modules, "stderr") == 0) {
				debug_file = stderr;
				goto debug_setbuf;
			}
			len = strlen(modules);
			fname = bmake_malloc(len + 20);
			memcpy(fname, modules, len + 1);
			/* Let the filename be modified by the pid */
			if (strcmp(fname + len - 3, ".%d") == 0)
				snprintf(fname + len - 2, 20, "%d", getpid());
			debug_file = fopen(fname, mode);
			if (!debug_file) {
				fprintf(stderr, "Cannot open debug file %s\n",
				    fname);
				usage();
			}
			free(fname);
			goto debug_setbuf;
		default:
			(void)fprintf(stderr,
			    "%s: illegal argument to d option -- %c\n",
			    progname, *modules);
			usage();
		}
	}
debug_setbuf:
	/*
	 * Make the debug_file unbuffered, and make
	 * stdout line buffered (unless debugfile == stdout).
	 */
	setvbuf(debug_file, NULL, _IONBF, 0);
	if (debug_file != stdout) {
		setvbuf(stdout, NULL, _IOLBF, 0);
	}
}

/*
 * does path contain any relative components
 */
static int
is_relpath(const char *path)
{
	const char *cp;

	if (path[0] != '/')
		return TRUE;
	cp = path;
	do {
		cp = strstr(cp, "/.");
		if (!cp)
			break;
		cp += 2;
		if (cp[0] == '/' || cp[0] == '\0')
			return TRUE;
		else if (cp[0] == '.') {
			if (cp[1] == '/' || cp[1] == '\0')
				return TRUE;
		}
	} while (cp);
	return FALSE;
}

/*-
 * MainParseArgs --
 *	Parse a given argument vector. Called from main() and from
 *	Main_ParseArgLine() when the .MAKEFLAGS target is used.
 *
 *	XXX: Deal with command line overriding .MAKEFLAGS in makefile
 *
 * Results:
 *	None
 *
 * Side Effects:
 *	Various global and local flags will be set depending on the flags
 *	given
 */
static void
MainParseArgs(int argc, char **argv)
{
	char *p;
	int c = '?';
	int arginc;
	char *argvalue;
	const char *getopt_def;
	struct stat sa, sb;
	char *optscan;
	Boolean inOption, dashDash = FALSE;
	char found_path[MAXPATHLEN + 1];	/* for searching for sys.mk */

#define OPTFLAGS "BC:D:I:J:NST:V:WXd:ef:ij:km:nqrstv:w"
/* Can't actually use getopt(3) because rescanning is not portable */

	getopt_def = OPTFLAGS;
rearg:	
	inOption = FALSE;
	optscan = NULL;
	while(argc > 1) {
		char *getopt_spec;
		if(!inOption)
			optscan = argv[1];
		c = *optscan++;
		arginc = 0;
		if(inOption) {
			if(c == '\0') {
				++argv;
				--argc;
				inOption = FALSE;
				continue;
			}
		} else {
			if (c != '-' || dashDash)
				break;
			inOption = TRUE;
			c = *optscan++;
		}
		/* '-' found at some earlier point */
		getopt_spec = strchr(getopt_def, c);
		if(c != '\0' && getopt_spec != NULL && getopt_spec[1] == ':') {
			/* -<something> found, and <something> should have an arg */
			inOption = FALSE;
			arginc = 1;
			argvalue = optscan;
			if(*argvalue == '\0') {
				if (argc < 3)
					goto noarg;
				argvalue = argv[2];
				arginc = 2;
			}
		} else {
			argvalue = NULL; 
		}
		switch(c) {
		case '\0':
			arginc = 1;
			inOption = FALSE;
			break;
		case 'B':
			compatMake = TRUE;
			Var_Append(MAKEFLAGS, "-B", VAR_GLOBAL);
			Var_Set(MAKE_MODE, "compat", VAR_GLOBAL, 0);
			break;
		case 'C':
			if (chdir(argvalue) == -1) {
				(void)fprintf(stderr,
					      "%s: chdir %s: %s\n",
					      progname, argvalue,
					      strerror(errno));
				exit(1);
			}
			if (getcwd(curdir, MAXPATHLEN) == NULL) {
				(void)fprintf(stderr, "%s: %s.\n", progname, strerror(errno));
				exit(2);
			}
			if (!is_relpath(argvalue) &&
			    stat(argvalue, &sa) != -1 &&
			    stat(curdir, &sb) != -1 &&
			    sa.st_ino == sb.st_ino &&
			    sa.st_dev == sb.st_dev)
				strncpy(curdir, argvalue, MAXPATHLEN);
			ignorePWD = TRUE;
			break;
		case 'D':
			if (argvalue == NULL || argvalue[0] == 0) goto noarg;
			Var_Set(argvalue, "1", VAR_GLOBAL, 0);
			Var_Append(MAKEFLAGS, "-D", VAR_GLOBAL);
			Var_Append(MAKEFLAGS, argvalue, VAR_GLOBAL);
			break;
		case 'I':
			if (argvalue == NULL) goto noarg;
			Parse_AddIncludeDir(argvalue);
			Var_Append(MAKEFLAGS, "-I", VAR_GLOBAL);
			Var_Append(MAKEFLAGS, argvalue, VAR_GLOBAL);
			break;
		case 'J':
			if (argvalue == NULL) goto noarg;
			if (sscanf(argvalue, "%d,%d", &jp_0, &jp_1) != 2) {
			    (void)fprintf(stderr,
				"%s: internal error -- J option malformed (%s)\n",
				progname, argvalue);
				usage();
			}
			if ((fcntl(jp_0, F_GETFD, 0) < 0) ||
			    (fcntl(jp_1, F_GETFD, 0) < 0)) {
#if 0
			    (void)fprintf(stderr,
				"%s: ###### warning -- J descriptors were closed!\n",
				progname);
			    exit(2);
#endif
			    jp_0 = -1;
			    jp_1 = -1;
			    compatMake = TRUE;
			} else {
			    Var_Append(MAKEFLAGS, "-J", VAR_GLOBAL);
			    Var_Append(MAKEFLAGS, argvalue, VAR_GLOBAL);
			    jobServer = TRUE;
			}
			break;
		case 'N':
			noExecute = TRUE;
			noRecursiveExecute = TRUE;
			Var_Append(MAKEFLAGS, "-N", VAR_GLOBAL);
			break;
		case 'S':
			keepgoing = FALSE;
			Var_Append(MAKEFLAGS, "-S", VAR_GLOBAL);
			break;
		case 'T':
			if (argvalue == NULL) goto noarg;
			tracefile = bmake_strdup(argvalue);
			Var_Append(MAKEFLAGS, "-T", VAR_GLOBAL);
			Var_Append(MAKEFLAGS, argvalue, VAR_GLOBAL);
			break;
		case 'V':
		case 'v':
			if (argvalue == NULL) goto noarg;
			printVars = c == 'v' ? EXPAND_VARS : COMPAT_VARS;
			(void)Lst_AtEnd(variables, argvalue);
			Var_Append(MAKEFLAGS, "-V", VAR_GLOBAL);
			Var_Append(MAKEFLAGS, argvalue, VAR_GLOBAL);
			break;
		case 'W':
			parseWarnFatal = TRUE;
			break;
		case 'X':
			varNoExportEnv = TRUE;
			Var_Append(MAKEFLAGS, "-X", VAR_GLOBAL);
			break;
		case 'd':
			if (argvalue == NULL) goto noarg;
			/* If '-d-opts' don't pass to children */
			if (argvalue[0] == '-')
			    argvalue++;
			else {
			    Var_Append(MAKEFLAGS, "-d", VAR_GLOBAL);
			    Var_Append(MAKEFLAGS, argvalue, VAR_GLOBAL);
			}
			parse_debug_options(argvalue);
			break;
		case 'e':
			checkEnvFirst = TRUE;
			Var_Append(MAKEFLAGS, "-e", VAR_GLOBAL);
			break;
		case 'f':
			if (argvalue == NULL) goto noarg;
			(void)Lst_AtEnd(makefiles, argvalue);
			break;
		case 'i':
			ignoreErrors = TRUE;
			Var_Append(MAKEFLAGS, "-i", VAR_GLOBAL);
			break;
		case 'j':
			if (argvalue == NULL) goto noarg;
			forceJobs = TRUE;
			maxJobs = strtol(argvalue, &p, 0);
			if (*p != '\0' || maxJobs < 1) {
				(void)fprintf(stderr, "%s: illegal argument to -j -- must be positive integer!\n",
				    progname);
				exit(1);
			}
			Var_Append(MAKEFLAGS, "-j", VAR_GLOBAL);
			Var_Append(MAKEFLAGS, argvalue, VAR_GLOBAL);
			Var_Set(".MAKE.JOBS", argvalue, VAR_GLOBAL, 0);
			maxJobTokens = maxJobs;
			break;
		case 'k':
			keepgoing = TRUE;
			Var_Append(MAKEFLAGS, "-k", VAR_GLOBAL);
			break;
		case 'm':
			if (argvalue == NULL) goto noarg;
			/* look for magic parent directory search string */
			if (strncmp(".../", argvalue, 4) == 0) {
				if (!Dir_FindHereOrAbove(curdir, argvalue+4,
				    found_path, sizeof(found_path)))
					break;		/* nothing doing */
				(void)Dir_AddDir(sysIncPath, found_path);
			} else {
				(void)Dir_AddDir(sysIncPath, argvalue);
			}
			Var_Append(MAKEFLAGS, "-m", VAR_GLOBAL);
			Var_Append(MAKEFLAGS, argvalue, VAR_GLOBAL);
			break;
		case 'n':
			noExecute = TRUE;
			Var_Append(MAKEFLAGS, "-n", VAR_GLOBAL);
			break;
		case 'q':
			queryFlag = TRUE;
			/* Kind of nonsensical, wot? */
			Var_Append(MAKEFLAGS, "-q", VAR_GLOBAL);
			break;
		case 'r':
			noBuiltins = TRUE;
			Var_Append(MAKEFLAGS, "-r", VAR_GLOBAL);
			break;
		case 's':
			beSilent = TRUE;
			Var_Append(MAKEFLAGS, "-s", VAR_GLOBAL);
			break;
		case 't':
			touchFlag = TRUE;
			Var_Append(MAKEFLAGS, "-t", VAR_GLOBAL);
			break;
		case 'w':
			enterFlag = TRUE;
			Var_Append(MAKEFLAGS, "-w", VAR_GLOBAL);
			break;
		case '-':
			dashDash = TRUE;
			break;
		default:
		case '?':
#ifndef MAKE_NATIVE
			fprintf(stderr, "getopt(%s) -> %d (%c)\n",
				OPTFLAGS, c, c);
#endif
			usage();
		}
		argv += arginc;
		argc -= arginc;
	}

	oldVars = TRUE;

	/*
	 * See if the rest of the arguments are variable assignments and
	 * perform them if so. Else take them to be targets and stuff them
	 * on the end of the "create" list.
	 */
	for (; argc > 1; ++argv, --argc)
		if (Parse_IsVar(argv[1])) {
			Parse_DoVar(argv[1], VAR_CMD);
		} else {
			if (!*argv[1])
				Punt("illegal (null) argument.");
			if (*argv[1] == '-' && !dashDash)
				goto rearg;
			(void)Lst_AtEnd(create, bmake_strdup(argv[1]));
		}

	return;
noarg:
	(void)fprintf(stderr, "%s: option requires an argument -- %c\n",
	    progname, c);
	usage();
}

/*-
 * Main_ParseArgLine --
 *  	Used by the parse module when a .MFLAGS or .MAKEFLAGS target
 *	is encountered and by main() when reading the .MAKEFLAGS envariable.
 *	Takes a line of arguments and breaks it into its
 * 	component words and passes those words and the number of them to the
 *	MainParseArgs function.
 *	The line should have all its leading whitespace removed.
 *
 * Input:
 *	line		Line to fracture
 *
 * Results:
 *	None
 *
 * Side Effects:
 *	Only those that come from the various arguments.
 */
void
Main_ParseArgLine(const char *line)
{
	char **argv;			/* Manufactured argument vector */
	int argc;			/* Number of arguments in argv */
	char *args;			/* Space used by the args */
	char *buf, *p1;
	char *argv0 = Var_Value(".MAKE", VAR_GLOBAL, &p1);
	size_t len;

	if (line == NULL)
		return;
	for (; *line == ' '; ++line)
		continue;
	if (!*line)
		return;

#ifndef POSIX
	{
		/*
		 * $MAKE may simply be naming the make(1) binary
		 */
		char *cp;

		if (!(cp = strrchr(line, '/')))
			cp = line;
		if ((cp = strstr(cp, "make")) &&
		    strcmp(cp, "make") == 0)
			return;
	}
#endif
	buf = bmake_malloc(len = strlen(line) + strlen(argv0) + 2);
	(void)snprintf(buf, len, "%s %s", argv0, line);
	free(p1);

	argv = brk_string(buf, &argc, TRUE, &args);
	if (argv == NULL) {
		Error("Unterminated quoted string [%s]", buf);
		free(buf);
		return;
	}
	free(buf);
	MainParseArgs(argc, argv);

	free(args);
	free(argv);
}

Boolean
Main_SetObjdir(const char *fmt, ...)
{
	struct stat sb;
	char *path;
	char buf[MAXPATHLEN + 1];
	char buf2[MAXPATHLEN + 1];
	Boolean rc = FALSE;
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(path = buf, MAXPATHLEN, fmt, ap);
	va_end(ap);

	if (path[0] != '/') {
		snprintf(buf2, MAXPATHLEN, "%s/%s", curdir, path);
		path = buf2;
	}

	/* look for the directory and try to chdir there */
	if (stat(path, &sb) == 0 && S_ISDIR(sb.st_mode)) {
		if (chdir(path)) {
			(void)fprintf(stderr, "make warning: %s: %s.\n",
				      path, strerror(errno));
		} else {
			strncpy(objdir, path, MAXPATHLEN);
			Var_Set(".OBJDIR", objdir, VAR_GLOBAL, 0);
			setenv("PWD", objdir, 1);
			Dir_InitDot();
			purge_cached_realpaths();
			rc = TRUE;
			if (enterFlag && strcmp(objdir, curdir) != 0)
				enterFlagObj = TRUE;
		}
	}

	return rc;
}

static Boolean
Main_SetVarObjdir(const char *var, const char *suffix)
{
	char *p, *path, *xpath;

	if ((path = Var_Value(var, VAR_CMD, &p)) == NULL ||
	    *path == '\0')
		return FALSE;

	/* expand variable substitutions */
	if (strchr(path, '$') != 0)
		xpath = Var_Subst(NULL, path, VAR_GLOBAL, VARF_WANTRES);
	else
		xpath = path;

	(void)Main_SetObjdir("%s%s", xpath, suffix);

	if (xpath != path)
		free(xpath);
	free(p);
	return TRUE;
}

/*-
 * ReadAllMakefiles --
 *	wrapper around ReadMakefile() to read all.
 *
 * Results:
 *	TRUE if ok, FALSE on error
 */
static int
ReadAllMakefiles(const void *p, const void *q)
{
	return (ReadMakefile(p, q) == 0);
}

int
str2Lst_Append(Lst lp, char *str, const char *sep)
{
    char *cp;
    int n;

    if (!sep)
	sep = " \t";

    for (n = 0, cp = strtok(str, sep); cp; cp = strtok(NULL, sep)) {
	(void)Lst_AtEnd(lp, cp);
	n++;
    }
    return (n);
}

#ifdef SIGINFO
/*ARGSUSED*/
static void
siginfo(int signo MAKE_ATTR_UNUSED)
{
	char dir[MAXPATHLEN];
	char str[2 * MAXPATHLEN];
	int len;
	if (getcwd(dir, sizeof(dir)) == NULL)
		return;
	len = snprintf(str, sizeof(str), "%s: Working in: %s\n", progname, dir);
	if (len > 0)
		(void)write(STDERR_FILENO, str, (size_t)len);
}
#endif

/*
 * Allow makefiles some control over the mode we run in.
 */
void
MakeMode(const char *mode)
{
    char *mp = NULL;

    if (!mode)
	mode = mp = Var_Subst(NULL, "${" MAKE_MODE ":tl}",
			      VAR_GLOBAL, VARF_WANTRES);

    if (mode && *mode) {
	if (strstr(mode, "compat")) {
	    compatMake = TRUE;
	    forceJobs = FALSE;
	}
#if USE_META
	if (strstr(mode, "meta"))
	    meta_mode_init(mode);
#endif
    }

    free(mp);
}

static void
doPrintVars(void)
{
	LstNode ln;
	Boolean expandVars;

	if (printVars == EXPAND_VARS)
		expandVars = TRUE;
	else if (debugVflag)
		expandVars = FALSE;
	else
		expandVars = getBoolean(".MAKE.EXPAND_VARIABLES", FALSE);

	for (ln = Lst_First(variables); ln != NULL;
	    ln = Lst_Succ(ln)) {
		char *var = (char *)Lst_Datum(ln);
		char *value;
		char *p1;
		
		if (strchr(var, '$')) {
			value = p1 = Var_Subst(NULL, var, VAR_GLOBAL,
			    VARF_WANTRES);
		} else if (expandVars) {
			char tmp[128];
			int len = snprintf(tmp, sizeof(tmp), "${%s}", var);
							
			if (len >= (int)sizeof(tmp))
				Fatal("%s: variable name too big: %s",
				    progname, var);
			value = p1 = Var_Subst(NULL, tmp, VAR_GLOBAL,
			    VARF_WANTRES);
		} else {
			value = Var_Value(var, VAR_GLOBAL, &p1);
		}
		printf("%s\n", value ? value : "");
		free(p1);
	}
}

static Boolean
runTargets(void)
{
	Lst targs;	/* target nodes to create -- passed to Make_Init */
	Boolean outOfDate; 	/* FALSE if all targets up to date */

	/*
	 * Have now read the entire graph and need to make a list of
	 * targets to create. If none was given on the command line,
	 * we consult the parsing module to find the main target(s)
	 * to create.
	 */
	if (Lst_IsEmpty(create))
		targs = Parse_MainName();
	else
		targs = Targ_FindList(create, TARG_CREATE);

	if (!compatMake) {
		/*
		 * Initialize job module before traversing the graph
		 * now that any .BEGIN and .END targets have been read.
		 * This is done only if the -q flag wasn't given
		 * (to prevent the .BEGIN from being executed should
		 * it exist).
		 */
		if (!queryFlag) {
			Job_Init();
			jobsRunning = TRUE;
		}

		/* Traverse the graph, checking on all the targets */
		outOfDate = Make_Run(targs);
	} else {
		/*
		 * Compat_Init will take care of creating all the
		 * targets as well as initializing the module.
		 */
		Compat_Run(targs);
		outOfDate = FALSE;
	}
	Lst_Destroy(targs, NULL);
	return outOfDate;
}

/*-
 * main --
 *	The main function, for obvious reasons. Initializes variables
 *	and a few modules, then parses the arguments give it in the
 *	environment and on the command line. Reads the system makefile
 *	followed by either Makefile, makefile or the file given by the
 *	-f argument. Sets the .MAKEFLAGS PMake variable based on all the
 *	flags it has received by then uses either the Make or the Compat
 *	module to create the initial list of targets.
 *
 * Results:
 *	If -q was given, exits -1 if anything was out-of-date. Else it exits
 *	0.
 *
 * Side Effects:
 *	The program exits when done. Targets are created. etc. etc. etc.
 */
int
main(int argc, char **argv)
{
	Boolean outOfDate; 	/* FALSE if all targets up to date */
	struct stat sb, sa;
	char *p1, *path;
	char mdpath[MAXPATHLEN];
#ifdef FORCE_MACHINE
	const char *machine = FORCE_MACHINE;
#else
    	const char *machine = getenv("MACHINE");
#endif
	const char *machine_arch = getenv("MACHINE_ARCH");
	char *syspath = getenv("MAKESYSPATH");
	Lst sysMkPath;			/* Path of sys.mk */
	char *cp = NULL, *start;
					/* avoid faults on read-only strings */
	static char defsyspath[] = _PATH_DEFSYSPATH;
	char found_path[MAXPATHLEN + 1];	/* for searching for sys.mk */
	struct timeval rightnow;		/* to initialize random seed */
	struct utsname utsname;

	/* default to writing debug to stderr */
	debug_file = stderr;

#ifdef SIGINFO
	(void)bmake_signal(SIGINFO, siginfo);
#endif
	/*
	 * Set the seed to produce a different random sequence
	 * on each program execution.
	 */
	gettimeofday(&rightnow, NULL);
	srandom(rightnow.tv_sec + rightnow.tv_usec);
	
	if ((progname = strrchr(argv[0], '/')) != NULL)
		progname++;
	else
		progname = argv[0];
#if defined(MAKE_NATIVE) || (defined(HAVE_SETRLIMIT) && defined(RLIMIT_NOFILE))
	/*
	 * get rid of resource limit on file descriptors
	 */
	{
		struct rlimit rl;
		if (getrlimit(RLIMIT_NOFILE, &rl) != -1 &&
		    rl.rlim_cur != rl.rlim_max) {
			rl.rlim_cur = rl.rlim_max;
			(void)setrlimit(RLIMIT_NOFILE, &rl);
		}
	}
#endif

	if (uname(&utsname) == -1) {
	    (void)fprintf(stderr, "%s: uname failed (%s).\n", progname,
		strerror(errno));
	    exit(2);
	}

	/*
	 * Get the name of this type of MACHINE from utsname
	 * so we can share an executable for similar machines.
	 * (i.e. m68k: amiga hp300, mac68k, sun3, ...)
	 *
	 * Note that both MACHINE and MACHINE_ARCH are decided at
	 * run-time.
	 */
	if (!machine) {
#ifdef MAKE_NATIVE
	    machine = utsname.machine;
#else
#ifdef MAKE_MACHINE
	    machine = MAKE_MACHINE;
#else
	    machine = "unknown";
#endif
#endif
	}

	if (!machine_arch) {
#if defined(MAKE_NATIVE) && defined(HAVE_SYSCTL) && defined(CTL_HW) && defined(HW_MACHINE_ARCH)
	    static char machine_arch_buf[sizeof(utsname.machine)];
	    int mib[2] = { CTL_HW, HW_MACHINE_ARCH };
	    size_t len = sizeof(machine_arch_buf);
                
	    if (sysctl(mib, __arraycount(mib), machine_arch_buf,
		    &len, NULL, 0) < 0) {
		(void)fprintf(stderr, "%s: sysctl failed (%s).\n", progname,
		    strerror(errno));
		exit(2);
	    }

	    machine_arch = machine_arch_buf;
#else
#ifndef MACHINE_ARCH
#ifdef MAKE_MACHINE_ARCH
            machine_arch = MAKE_MACHINE_ARCH;
#else
	    machine_arch = "unknown";
#endif
#else
	    machine_arch = MACHINE_ARCH;
#endif
#endif
	}

	myPid = getpid();		/* remember this for vFork() */

	/*
	 * Just in case MAKEOBJDIR wants us to do something tricky.
	 */
	Var_Init();		/* Initialize the lists of variables for
				 * parsing arguments */
	Var_Set(".MAKE.OS", utsname.sysname, VAR_GLOBAL, 0);
	Var_Set("MACHINE", machine, VAR_GLOBAL, 0);
	Var_Set("MACHINE_ARCH", machine_arch, VAR_GLOBAL, 0);
#ifdef MAKE_VERSION
	Var_Set("MAKE_VERSION", MAKE_VERSION, VAR_GLOBAL, 0);
#endif
	Var_Set(".newline", "\n", VAR_GLOBAL, 0); /* handy for :@ loops */
	/*
	 * This is the traditional preference for makefiles.
	 */
#ifndef MAKEFILE_PREFERENCE_LIST
# define MAKEFILE_PREFERENCE_LIST "makefile Makefile"
#endif
	Var_Set(MAKEFILE_PREFERENCE, MAKEFILE_PREFERENCE_LIST,
		VAR_GLOBAL, 0);
	Var_Set(MAKE_DEPENDFILE, ".depend", VAR_GLOBAL, 0);

	create = Lst_Init(FALSE);
	makefiles = Lst_Init(FALSE);
	printVars = 0;
	debugVflag = FALSE;
	variables = Lst_Init(FALSE);
	beSilent = FALSE;		/* Print commands as executed */
	ignoreErrors = FALSE;		/* Pay attention to non-zero returns */
	noExecute = FALSE;		/* Execute all commands */
	noRecursiveExecute = FALSE;	/* Execute all .MAKE targets */
	keepgoing = FALSE;		/* Stop on error */
	allPrecious = FALSE;		/* Remove targets when interrupted */
	deleteOnError = FALSE;		/* Historical default behavior */
	queryFlag = FALSE;		/* This is not just a check-run */
	noBuiltins = FALSE;		/* Read the built-in rules */
	touchFlag = FALSE;		/* Actually update targets */
	debug = 0;			/* No debug verbosity, please. */
	jobsRunning = FALSE;

	maxJobs = DEFMAXLOCAL;		/* Set default local max concurrency */
	maxJobTokens = maxJobs;
	compatMake = FALSE;		/* No compat mode */
	ignorePWD = FALSE;

	/*
	 * Initialize the parsing, directory and variable modules to prepare
	 * for the reading of inclusion paths and variable settings on the
	 * command line
	 */

	/*
	 * Initialize various variables.
	 *	MAKE also gets this name, for compatibility
	 *	.MAKEFLAGS gets set to the empty string just in case.
	 *	MFLAGS also gets initialized empty, for compatibility.
	 */
	Parse_Init();
	if (argv[0][0] == '/' || strchr(argv[0], '/') == NULL) {
	    /*
	     * Leave alone if it is an absolute path, or if it does
	     * not contain a '/' in which case we need to find it in
	     * the path, like execvp(3) and the shells do.
	     */
	    p1 = argv[0];
	} else {
	    /*
	     * A relative path, canonicalize it.
	     */
	    p1 = cached_realpath(argv[0], mdpath);
	    if (!p1 || *p1 != '/' || stat(p1, &sb) < 0) {
		p1 = argv[0];		/* realpath failed */
	    }
	}
	Var_Set("MAKE", p1, VAR_GLOBAL, 0);
	Var_Set(".MAKE", p1, VAR_GLOBAL, 0);
	Var_Set(MAKEFLAGS, "", VAR_GLOBAL, 0);
	Var_Set(MAKEOVERRIDES, "", VAR_GLOBAL, 0);
	Var_Set("MFLAGS", "", VAR_GLOBAL, 0);
	Var_Set(".ALLTARGETS", "", VAR_GLOBAL, 0);
	/* some makefiles need to know this */
	Var_Set(MAKE_LEVEL ".ENV", MAKE_LEVEL_ENV, VAR_CMD, 0);

	/*
	 * Set some other useful macros
	 */
	{
	    char tmp[64], *ep;

	    makelevel = ((ep = getenv(MAKE_LEVEL_ENV)) && *ep) ? atoi(ep) : 0;
	    if (makelevel < 0)
		makelevel = 0;
	    snprintf(tmp, sizeof(tmp), "%d", makelevel);
	    Var_Set(MAKE_LEVEL, tmp, VAR_GLOBAL, 0);
	    snprintf(tmp, sizeof(tmp), "%u", myPid);
	    Var_Set(".MAKE.PID", tmp, VAR_GLOBAL, 0);
	    snprintf(tmp, sizeof(tmp), "%u", getppid());
	    Var_Set(".MAKE.PPID", tmp, VAR_GLOBAL, 0);
	}
	if (makelevel > 0) {
		char pn[1024];
		snprintf(pn, sizeof(pn), "%s[%d]", progname, makelevel);
		progname = bmake_strdup(pn);
	}

#ifdef USE_META
	meta_init();
#endif
	Dir_Init(NULL);		/* Dir_* safe to call from MainParseArgs */

	/*
	 * First snag any flags out of the MAKE environment variable.
	 * (Note this is *not* MAKEFLAGS since /bin/make uses that and it's
	 * in a different format).
	 */
#ifdef POSIX
	p1 = explode(getenv("MAKEFLAGS"));
	Main_ParseArgLine(p1);
	free(p1);
#else
	Main_ParseArgLine(getenv("MAKE"));
#endif

	/*
	 * Find where we are (now).
	 * We take care of PWD for the automounter below...
	 */
	if (getcwd(curdir, MAXPATHLEN) == NULL) {
		(void)fprintf(stderr, "%s: getcwd: %s.\n",
		    progname, strerror(errno));
		exit(2);
	}

	MainParseArgs(argc, argv);

	if (enterFlag)
		printf("%s: Entering directory `%s'\n", progname, curdir);

	/*
	 * Verify that cwd is sane.
	 */
	if (stat(curdir, &sa) == -1) {
	    (void)fprintf(stderr, "%s: %s: %s.\n",
		 progname, curdir, strerror(errno));
	    exit(2);
	}

	/*
	 * All this code is so that we know where we are when we start up
	 * on a different machine with pmake.
	 * Overriding getcwd() with $PWD totally breaks MAKEOBJDIRPREFIX
	 * since the value of curdir can vary depending on how we got
	 * here.  Ie sitting at a shell prompt (shell that provides $PWD)
	 * or via subdir.mk in which case its likely a shell which does
	 * not provide it.
	 * So, to stop it breaking this case only, we ignore PWD if
	 * MAKEOBJDIRPREFIX is set or MAKEOBJDIR contains a transform.
	 */
#ifndef NO_PWD_OVERRIDE
	if (!ignorePWD) {
		char *pwd, *ptmp1 = NULL, *ptmp2 = NULL;

		if ((pwd = getenv("PWD")) != NULL &&
		    Var_Value("MAKEOBJDIRPREFIX", VAR_CMD, &ptmp1) == NULL) {
			const char *makeobjdir = Var_Value("MAKEOBJDIR",
			    VAR_CMD, &ptmp2);

			if (makeobjdir == NULL || !strchr(makeobjdir, '$')) {
				if (stat(pwd, &sb) == 0 &&
				    sa.st_ino == sb.st_ino &&
				    sa.st_dev == sb.st_dev)
					(void)strncpy(curdir, pwd, MAXPATHLEN);
			}
		}
		free(ptmp1);
		free(ptmp2);
	}
#endif
	Var_Set(".CURDIR", curdir, VAR_GLOBAL, 0);

	/*
	 * Find the .OBJDIR.  If MAKEOBJDIRPREFIX, or failing that,
	 * MAKEOBJDIR is set in the environment, try only that value
	 * and fall back to .CURDIR if it does not exist.
	 *
	 * Otherwise, try _PATH_OBJDIR.MACHINE-MACHINE_ARCH, _PATH_OBJDIR.MACHINE,
	 * and * finally _PATH_OBJDIRPREFIX`pwd`, in that order.  If none
	 * of these paths exist, just use .CURDIR.
	 */
	Dir_Init(curdir);
	(void)Main_SetObjdir("%s", curdir);

	if (!Main_SetVarObjdir("MAKEOBJDIRPREFIX", curdir) &&
	    !Main_SetVarObjdir("MAKEOBJDIR", "") &&
	    !Main_SetObjdir("%s.%s-%s", _PATH_OBJDIR, machine, machine_arch) &&
	    !Main_SetObjdir("%s.%s", _PATH_OBJDIR, machine) &&
	    !Main_SetObjdir("%s", _PATH_OBJDIR))
		(void)Main_SetObjdir("%s%s", _PATH_OBJDIRPREFIX, curdir);

	/*
	 * Initialize archive, target and suffix modules in preparation for
	 * parsing the makefile(s)
	 */
	Arch_Init();
	Targ_Init();
	Suff_Init();
	Trace_Init(tracefile);

	DEFAULT = NULL;
	(void)time(&now);

	Trace_Log(MAKESTART, NULL);
	
	/*
	 * Set up the .TARGETS variable to contain the list of targets to be
	 * created. If none specified, make the variable empty -- the parser
	 * will fill the thing in with the default or .MAIN target.
	 */
	if (!Lst_IsEmpty(create)) {
		LstNode ln;

		for (ln = Lst_First(create); ln != NULL;
		    ln = Lst_Succ(ln)) {
			char *name = (char *)Lst_Datum(ln);

			Var_Append(".TARGETS", name, VAR_GLOBAL);
		}
	} else
		Var_Set(".TARGETS", "", VAR_GLOBAL, 0);


	/*
	 * If no user-supplied system path was given (through the -m option)
	 * add the directories from the DEFSYSPATH (more than one may be given
	 * as dir1:...:dirn) to the system include path.
	 */
	if (syspath == NULL || *syspath == '\0')
		syspath = defsyspath;
	else
		syspath = bmake_strdup(syspath);

	for (start = syspath; *start != '\0'; start = cp) {
		for (cp = start; *cp != '\0' && *cp != ':'; cp++)
			continue;
		if (*cp == ':') {
			*cp++ = '\0';
		}
		/* look for magic parent directory search string */
		if (strncmp(".../", start, 4) != 0) {
			(void)Dir_AddDir(defIncPath, start);
		} else {
			if (Dir_FindHereOrAbove(curdir, start+4, 
			    found_path, sizeof(found_path))) {
				(void)Dir_AddDir(defIncPath, found_path);
			}
		}
	}
	if (syspath != defsyspath)
		free(syspath);

	/*
	 * Read in the built-in rules first, followed by the specified
	 * makefile, if it was (makefile != NULL), or the default
	 * makefile and Makefile, in that order, if it wasn't.
	 */
	if (!noBuiltins) {
		LstNode ln;

		sysMkPath = Lst_Init(FALSE);
		Dir_Expand(_PATH_DEFSYSMK,
			   Lst_IsEmpty(sysIncPath) ? defIncPath : sysIncPath,
			   sysMkPath);
		if (Lst_IsEmpty(sysMkPath))
			Fatal("%s: no system rules (%s).", progname,
			    _PATH_DEFSYSMK);
		ln = Lst_Find(sysMkPath, NULL, ReadMakefile);
		if (ln == NULL)
			Fatal("%s: cannot open %s.", progname,
			    (char *)Lst_Datum(ln));
	}

	if (!Lst_IsEmpty(makefiles)) {
		LstNode ln;

		ln = Lst_Find(makefiles, NULL, ReadAllMakefiles);
		if (ln != NULL)
			Fatal("%s: cannot open %s.", progname, 
			    (char *)Lst_Datum(ln));
	} else {
	    p1 = Var_Subst(NULL, "${" MAKEFILE_PREFERENCE "}",
		VAR_CMD, VARF_WANTRES);
	    if (p1) {
		(void)str2Lst_Append(makefiles, p1, NULL);
		(void)Lst_Find(makefiles, NULL, ReadMakefile);
		free(p1);
	    }
	}

	/* In particular suppress .depend for '-r -V .OBJDIR -f /dev/null' */
	if (!noBuiltins || !printVars) {
	    makeDependfile = Var_Subst(NULL, "${.MAKE.DEPENDFILE:T}",
		VAR_CMD, VARF_WANTRES);
	    doing_depend = TRUE;
	    (void)ReadMakefile(makeDependfile, NULL);
	    doing_depend = FALSE;
	}

	if (enterFlagObj)
		printf("%s: Entering directory `%s'\n", progname, objdir);
	
	MakeMode(NULL);

	Var_Append("MFLAGS", Var_Value(MAKEFLAGS, VAR_GLOBAL, &p1), VAR_GLOBAL);
	free(p1);

	if (!forceJobs && !compatMake &&
	    Var_Exists(".MAKE.JOBS", VAR_GLOBAL)) {
	    char *value;
	    int n;

	    value = Var_Subst(NULL, "${.MAKE.JOBS}", VAR_GLOBAL, VARF_WANTRES);
	    n = strtol(value, NULL, 0);
	    if (n < 1) {
		(void)fprintf(stderr, "%s: illegal value for .MAKE.JOBS -- must be positive integer!\n",
		    progname);
		exit(1);
	    }
	    if (n != maxJobs) {
		Var_Append(MAKEFLAGS, "-j", VAR_GLOBAL);
		Var_Append(MAKEFLAGS, value, VAR_GLOBAL);
	    }
	    maxJobs = n;
	    maxJobTokens = maxJobs;
	    forceJobs = TRUE;
	    free(value);
	}

	/*
	 * Be compatible if user did not specify -j and did not explicitly
	 * turned compatibility on
	 */
	if (!compatMake && !forceJobs) {
	    compatMake = TRUE;
	}

	if (!compatMake)
	    Job_ServerStart(maxJobTokens, jp_0, jp_1);
	if (DEBUG(JOB))
	    fprintf(debug_file, "job_pipe %d %d, maxjobs %d, tokens %d, compat %d\n",
		jp_0, jp_1, maxJobs, maxJobTokens, compatMake);

	if (!printVars)
	    Main_ExportMAKEFLAGS(TRUE);	/* initial export */
	
	/*
	 * For compatibility, look at the directories in the VPATH variable
	 * and add them to the search path, if the variable is defined. The
	 * variable's value is in the same format as the PATH envariable, i.e.
	 * <directory>:<directory>:<directory>...
	 */
	if (Var_Exists("VPATH", VAR_CMD)) {
		char *vpath, savec;
		/*
		 * GCC stores string constants in read-only memory, but
		 * Var_Subst will want to write this thing, so store it
		 * in an array
		 */
		static char VPATH[] = "${VPATH}";

		vpath = Var_Subst(NULL, VPATH, VAR_CMD, VARF_WANTRES);
		path = vpath;
		do {
			/* skip to end of directory */
			for (cp = path; *cp != ':' && *cp != '\0'; cp++)
				continue;
			/* Save terminator character so know when to stop */
			savec = *cp;
			*cp = '\0';
			/* Add directory to search path */
			(void)Dir_AddDir(dirSearchPath, path);
			*cp = savec;
			path = cp + 1;
		} while (savec == ':');
		free(vpath);
	}

	/*
	 * Now that all search paths have been read for suffixes et al, it's
	 * time to add the default search path to their lists...
	 */
	Suff_DoPaths();

	/*
	 * Propagate attributes through :: dependency lists.
	 */
	Targ_Propagate();

	/* print the initial graph, if the user requested it */
	if (DEBUG(GRAPH1))
		Targ_PrintGraph(1);

	/* print the values of any variables requested by the user */
	if (printVars) {
		doPrintVars();
		outOfDate = FALSE;
	} else {
		outOfDate = runTargets();
	}

#ifdef CLEANUP
	Lst_Destroy(variables, NULL);
	Lst_Destroy(makefiles, NULL);
	Lst_Destroy(create, (FreeProc *)free);
#endif

	/* print the graph now it's been processed if the user requested it */
	if (DEBUG(GRAPH2))
		Targ_PrintGraph(2);

	Trace_Log(MAKEEND, 0);

	if (enterFlagObj)
		printf("%s: Leaving directory `%s'\n", progname, objdir);
	if (enterFlag)
		printf("%s: Leaving directory `%s'\n", progname, curdir);

#ifdef USE_META
	meta_finish();
#endif
	Suff_End();
        Targ_End();
	Arch_End();
	Var_End();
	Parse_End();
	Dir_End();
	Job_End();
	Trace_End();

	return outOfDate ? 1 : 0;
}

/*-
 * ReadMakefile  --
 *	Open and parse the given makefile.
 *
 * Results:
 *	0 if ok. -1 if couldn't open file.
 *
 * Side Effects:
 *	lots
 */
static int
ReadMakefile(const void *p, const void *q MAKE_ATTR_UNUSED)
{
	const char *fname = p;		/* makefile to read */
	int fd;
	size_t len = MAXPATHLEN;
	char *name, *path = bmake_malloc(len);

	if (!strcmp(fname, "-")) {
		Parse_File(NULL /*stdin*/, -1);
		Var_Set("MAKEFILE", "", VAR_INTERNAL, 0);
	} else {
		/* if we've chdir'd, rebuild the path name */
		if (strcmp(curdir, objdir) && *fname != '/') {
			size_t plen = strlen(curdir) + strlen(fname) + 2;
			if (len < plen)
				path = bmake_realloc(path, len = 2 * plen);
			
			(void)snprintf(path, len, "%s/%s", curdir, fname);
			fd = open(path, O_RDONLY);
			if (fd != -1) {
				fname = path;
				goto found;
			}
			
			/* If curdir failed, try objdir (ala .depend) */
			plen = strlen(objdir) + strlen(fname) + 2;
			if (len < plen)
				path = bmake_realloc(path, len = 2 * plen);
			(void)snprintf(path, len, "%s/%s", objdir, fname);
			fd = open(path, O_RDONLY);
			if (fd != -1) {
				fname = path;
				goto found;
			}
		} else {
			fd = open(fname, O_RDONLY);
			if (fd != -1)
				goto found;
		}
		/* look in -I and system include directories. */
		name = Dir_FindFile(fname, parseIncPath);
		if (!name)
			name = Dir_FindFile(fname,
				Lst_IsEmpty(sysIncPath) ? defIncPath : sysIncPath);
		if (!name || (fd = open(name, O_RDONLY)) == -1) {
			free(name);
			free(path);
			return(-1);
		}
		fname = name;
		/*
		 * set the MAKEFILE variable desired by System V fans -- the
		 * placement of the setting here means it gets set to the last
		 * makefile specified, as it is set by SysV make.
		 */
found:
		if (!doing_depend)
			Var_Set("MAKEFILE", fname, VAR_INTERNAL, 0);
		Parse_File(fname, fd);
	}
	free(path);
	return(0);
}



/*-
 * Cmd_Exec --
 *	Execute the command in cmd, and return the output of that command
 *	in a string.
 *
 * Results:
 *	A string containing the output of the command, or the empty string
 *	If errnum is not NULL, it contains the reason for the command failure
 *
 * Side Effects:
 *	The string must be freed by the caller.
 */
char *
Cmd_Exec(const char *cmd, const char **errnum)
{
    const char	*args[4];   	/* Args for invoking the shell */
    int 	fds[2];	    	/* Pipe streams */
    int 	cpid;	    	/* Child PID */
    int 	pid;	    	/* PID from wait() */
    char	*res;		/* result */
    WAIT_T	status;		/* command exit status */
    Buffer	buf;		/* buffer to store the result */
    char	*cp;
    int		cc;		/* bytes read, or -1 */
    int		savederr;	/* saved errno */


    *errnum = NULL;

    if (!shellName)
	Shell_Init();
    /*
     * Set up arguments for shell
     */
    args[0] = shellName;
    args[1] = "-c";
    args[2] = cmd;
    args[3] = NULL;

    /*
     * Open a pipe for fetching its output
     */
    if (pipe(fds) == -1) {
	*errnum = "Couldn't create pipe for \"%s\"";
	goto bad;
    }

    /*
     * Fork
     */
    switch (cpid = vFork()) {
    case 0:
	/*
	 * Close input side of pipe
	 */
	(void)close(fds[0]);

	/*
	 * Duplicate the output stream to the shell's output, then
	 * shut the extra thing down. Note we don't fetch the error
	 * stream...why not? Why?
	 */
	(void)dup2(fds[1], 1);
	(void)close(fds[1]);

	Var_ExportVars();

	(void)execv(shellPath, UNCONST(args));
	_exit(1);
	/*NOTREACHED*/

    case -1:
	*errnum = "Couldn't exec \"%s\"";
	goto bad;

    default:
	/*
	 * No need for the writing half
	 */
	(void)close(fds[1]);

	savederr = 0;
	Buf_Init(&buf, 0);

	do {
	    char   result[BUFSIZ];
	    cc = read(fds[0], result, sizeof(result));
	    if (cc > 0)
		Buf_AddBytes(&buf, cc, result);
	}
	while (cc > 0 || (cc == -1 && errno == EINTR));
	if (cc == -1)
	    savederr = errno;

	/*
	 * Close the input side of the pipe.
	 */
	(void)close(fds[0]);

	/*
	 * Wait for the process to exit.
	 */
	while(((pid = waitpid(cpid, &status, 0)) != cpid) && (pid >= 0)) {
	    JobReapChild(pid, status, FALSE);
	    continue;
	}
	cc = Buf_Size(&buf);
	res = Buf_Destroy(&buf, FALSE);

	if (savederr != 0)
	    *errnum = "Couldn't read shell's output for \"%s\"";

	if (WIFSIGNALED(status))
	    *errnum = "\"%s\" exited on a signal";
	else if (WEXITSTATUS(status) != 0)
	    *errnum = "\"%s\" returned non-zero status";

	/*
	 * Null-terminate the result, convert newlines to spaces and
	 * install it in the variable.
	 */
	res[cc] = '\0';
	cp = &res[cc];

	if (cc > 0 && *--cp == '\n') {
	    /*
	     * A final newline is just stripped
	     */
	    *cp-- = '\0';
	}
	while (cp >= res) {
	    if (*cp == '\n') {
		*cp = ' ';
	    }
	    cp--;
	}
	break;
    }
    return res;
bad:
    res = bmake_malloc(1);
    *res = '\0';
    return res;
}

/*-
 * Error --
 *	Print an error message given its format.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	The message is printed.
 */
/* VARARGS */
void
Error(const char *fmt, ...)
{
	va_list ap;
	FILE *err_file;

	err_file = debug_file;
	if (err_file == stdout)
		err_file = stderr;
	(void)fflush(stdout);
	for (;;) {
		va_start(ap, fmt);
		fprintf(err_file, "%s: ", progname);
		(void)vfprintf(err_file, fmt, ap);
		va_end(ap);
		(void)fprintf(err_file, "\n");
		(void)fflush(err_file);
		if (err_file == stderr)
			break;
		err_file = stderr;
	}
}

/*-
 * Fatal --
 *	Produce a Fatal error message. If jobs are running, waits for them
 *	to finish.
 *
 * Results:
 *	None
 *
 * Side Effects:
 *	The program exits
 */
/* VARARGS */
void
Fatal(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	if (jobsRunning)
		Job_Wait();

	(void)fflush(stdout);
	(void)vfprintf(stderr, fmt, ap);
	va_end(ap);
	(void)fprintf(stderr, "\n");
	(void)fflush(stderr);

	PrintOnError(NULL, NULL);

	if (DEBUG(GRAPH2) || DEBUG(GRAPH3))
		Targ_PrintGraph(2);
	Trace_Log(MAKEERROR, 0);
	exit(2);		/* Not 1 so -q can distinguish error */
}

/*
 * Punt --
 *	Major exception once jobs are being created. Kills all jobs, prints
 *	a message and exits.
 *
 * Results:
 *	None
 *
 * Side Effects:
 *	All children are killed indiscriminately and the program Lib_Exits
 */
/* VARARGS */
void
Punt(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	(void)fflush(stdout);
	(void)fprintf(stderr, "%s: ", progname);
	(void)vfprintf(stderr, fmt, ap);
	va_end(ap);
	(void)fprintf(stderr, "\n");
	(void)fflush(stderr);

	PrintOnError(NULL, NULL);

	DieHorribly();
}

/*-
 * DieHorribly --
 *	Exit without giving a message.
 *
 * Results:
 *	None
 *
 * Side Effects:
 *	A big one...
 */
void
DieHorribly(void)
{
	if (jobsRunning)
		Job_AbortAll();
	if (DEBUG(GRAPH2))
		Targ_PrintGraph(2);
	Trace_Log(MAKEERROR, 0);
	exit(2);		/* Not 1, so -q can distinguish error */
}

/*
 * Finish --
 *	Called when aborting due to errors in child shell to signal
 *	abnormal exit.
 *
 * Results:
 *	None
 *
 * Side Effects:
 *	The program exits
 */
void
Finish(int errors)
	           	/* number of errors encountered in Make_Make */
{
	Fatal("%d error%s", errors, errors == 1 ? "" : "s");
}

/*
 * eunlink --
 *	Remove a file carefully, avoiding directories.
 */
int
eunlink(const char *file)
{
	struct stat st;

	if (lstat(file, &st) == -1)
		return -1;

	if (S_ISDIR(st.st_mode)) {
		errno = EISDIR;
		return -1;
	}
	return unlink(file);
}

/*
 * execError --
 *	Print why exec failed, avoiding stdio.
 */
void
execError(const char *af, const char *av)
{
#ifdef USE_IOVEC
	int i = 0;
	struct iovec iov[8];
#define IOADD(s) \
	(void)(iov[i].iov_base = UNCONST(s), \
	    iov[i].iov_len = strlen(iov[i].iov_base), \
	    i++)
#else
#define	IOADD(s) (void)write(2, s, strlen(s))
#endif

	IOADD(progname);
	IOADD(": ");
	IOADD(af);
	IOADD("(");
	IOADD(av);
	IOADD(") failed (");
	IOADD(strerror(errno));
	IOADD(")\n");

#ifdef USE_IOVEC
	while (writev(2, iov, 8) == -1 && errno == EAGAIN)
	    continue;
#endif
}

/*
 * usage --
 *	exit with usage message
 */
static void
usage(void)
{
	char *p;
	if ((p = strchr(progname, '[')) != NULL)
	    *p = '\0';

	(void)fprintf(stderr,
"usage: %s [-BeikNnqrstWwX] \n\
            [-C directory] [-D variable] [-d flags] [-f makefile]\n\
            [-I directory] [-J private] [-j max_jobs] [-m directory] [-T file]\n\
            [-V variable] [-v variable] [variable=value] [target ...]\n",
	    progname);
	exit(2);
}

/*
 * realpath(3) can get expensive, cache results...
 */
static GNode *cached_realpaths = NULL;

static GNode *
get_cached_realpaths(void)
{

    if (!cached_realpaths) {
	cached_realpaths = Targ_NewGN("Realpath");
#ifndef DEBUG_REALPATH_CACHE
	cached_realpaths->flags = INTERNAL;
#endif
    }

    return cached_realpaths;
}

/* purge any relative paths */
static void
purge_cached_realpaths(void)
{
    GNode *cache = get_cached_realpaths();
    Hash_Entry *he, *nhe;
    Hash_Search hs;

    he = Hash_EnumFirst(&cache->context, &hs);
    while (he) {
	nhe = Hash_EnumNext(&hs);
	if (he->name[0] != '/') {
	    if (DEBUG(DIR))
		fprintf(stderr, "cached_realpath: purging %s\n", he->name);
	    Hash_DeleteEntry(&cache->context, he);
	}
	he = nhe;
    }
}

char *
cached_realpath(const char *pathname, char *resolved)
{
    GNode *cache;
    char *rp, *cp;

    if (!pathname || !pathname[0])
	return NULL;

    cache = get_cached_realpaths();

    if ((rp = Var_Value(pathname, cache, &cp)) != NULL) {
	/* a hit */
	strlcpy(resolved, rp, MAXPATHLEN);
    } else if ((rp = realpath(pathname, resolved)) != NULL) {
	Var_Set(pathname, rp, cache, 0);
    }
    free(cp);
    return rp ? resolved : NULL;
}

int
PrintAddr(void *a, void *b)
{
    printf("%lx ", (unsigned long) a);
    return b ? 0 : 0;
}


static int
addErrorCMD(void *cmdp, void *gnp MAKE_ATTR_UNUSED)
{
    if (cmdp == NULL)
	return 1;			/* stop */
    Var_Append(".ERROR_CMD", cmdp, VAR_GLOBAL);
    return 0;
}

void
PrintOnError(GNode *gn, const char *s)
{
    static GNode *en = NULL;
    char tmp[64];
    char *cp;

    if (s)
	printf("%s", s);
	
    printf("\n%s: stopped in %s\n", progname, curdir);

    if (en)
	return;				/* we've been here! */
    if (gn) {
	/*
	 * We can print this even if there is no .ERROR target.
	 */
	Var_Set(".ERROR_TARGET", gn->name, VAR_GLOBAL, 0);
	Var_Delete(".ERROR_CMD", VAR_GLOBAL);
	Lst_ForEach(gn->commands, addErrorCMD, gn);
    }
    strncpy(tmp, "${MAKE_PRINT_VAR_ON_ERROR:@v@$v='${$v}'\n@}",
	    sizeof(tmp) - 1);
    cp = Var_Subst(NULL, tmp, VAR_GLOBAL, VARF_WANTRES);
    if (cp) {
	if (*cp)
	    printf("%s", cp);
	free(cp);
    }
    fflush(stdout);

    /*
     * Finally, see if there is a .ERROR target, and run it if so.
     */
    en = Targ_FindNode(".ERROR", TARG_NOCREATE);
    if (en) {
	en->type |= OP_SPECIAL;
	Compat_Make(en, en);
    }
}

void
Main_ExportMAKEFLAGS(Boolean first)
{
    static int once = 1;
    char tmp[64];
    char *s;

    if (once != first)
	return;
    once = 0;
    
    strncpy(tmp, "${.MAKEFLAGS} ${.MAKEOVERRIDES:O:u:@v@$v=${$v:Q}@}",
	    sizeof(tmp));
    s = Var_Subst(NULL, tmp, VAR_CMD, VARF_WANTRES);
    if (s && *s) {
#ifdef POSIX
	setenv("MAKEFLAGS", s, 1);
#else
	setenv("MAKE", s, 1);
#endif
    }
}

char *
getTmpdir(void)
{
    static char *tmpdir = NULL;

    if (!tmpdir) {
	struct stat st;

	/*
	 * Honor $TMPDIR but only if it is valid.
	 * Ensure it ends with /.
	 */
	tmpdir = Var_Subst(NULL, "${TMPDIR:tA:U" _PATH_TMP "}/", VAR_GLOBAL,
			   VARF_WANTRES);
	if (stat(tmpdir, &st) < 0 || !S_ISDIR(st.st_mode)) {
	    free(tmpdir);
	    tmpdir = bmake_strdup(_PATH_TMP);
	}
    }
    return tmpdir;
}

/*
 * Create and open a temp file using "pattern".
 * If "fnamep" is provided set it to a copy of the filename created.
 * Otherwise unlink the file once open.
 */
int
mkTempFile(const char *pattern, char **fnamep)
{
    static char *tmpdir = NULL;
    char tfile[MAXPATHLEN];
    int fd;
    
    if (!pattern)
	pattern = TMPPAT;
    if (!tmpdir)
	tmpdir = getTmpdir();
    if (pattern[0] == '/') {
	snprintf(tfile, sizeof(tfile), "%s", pattern);
    } else {
	snprintf(tfile, sizeof(tfile), "%s%s", tmpdir, pattern);
    }
    if ((fd = mkstemp(tfile)) < 0)
	Punt("Could not create temporary file %s: %s", tfile, strerror(errno));
    if (fnamep) {
	*fnamep = bmake_strdup(tfile);
    } else {
	unlink(tfile);			/* we just want the descriptor */
    }
    return fd;
}

/*
 * Convert a string representation of a boolean.
 * Anything that looks like "No", "False", "Off", "0" etc,
 * is FALSE, otherwise TRUE.
 */
Boolean
s2Boolean(const char *s, Boolean bf)
{
    if (s) {
	switch(*s) {
	case '\0':			/* not set - the default wins */
	    break;
	case '0':
	case 'F':
	case 'f':
	case 'N':
	case 'n':
	    bf = FALSE;
	    break;
	case 'O':
	case 'o':
	    switch (s[1]) {
	    case 'F':
	    case 'f':
		bf = FALSE;
		break;
	    default:
		bf = TRUE;
		break;
	    }
	    break;
	default:
	    bf = TRUE;
	    break;
	}
    }
    return (bf);
}

/*
 * Return a Boolean based on setting of a knob.
 *
 * If the knob is not set, the supplied default is the return value.
 * If set, anything that looks or smells like "No", "False", "Off", "0" etc,
 * is FALSE, otherwise TRUE.
 */
Boolean
getBoolean(const char *name, Boolean bf)
{
    char tmp[64];
    char *cp;

    if (snprintf(tmp, sizeof(tmp), "${%s:U:tl}", name) < (int)(sizeof(tmp))) {
	cp = Var_Subst(NULL, tmp, VAR_GLOBAL, VARF_WANTRES);

	if (cp) {
	    bf = s2Boolean(cp, bf);
	    free(cp);
	}
    }
    return (bf);
}
