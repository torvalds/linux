/*-
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

#ifndef lint
#if 0
static char sccsid[] = "@(#)exec.c	8.4 (Berkeley) 6/8/95";
#endif
#endif /* not lint */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <paths.h>
#include <stdlib.h>

/*
 * When commands are first encountered, they are entered in a hash table.
 * This ensures that a full path search will not have to be done for them
 * on each invocation.
 *
 * We should investigate converting to a linear search, even though that
 * would make the command name "hash" a misnomer.
 */

#include "shell.h"
#include "main.h"
#include "nodes.h"
#include "parser.h"
#include "redir.h"
#include "eval.h"
#include "exec.h"
#include "builtins.h"
#include "var.h"
#include "options.h"
#include "input.h"
#include "output.h"
#include "syntax.h"
#include "memalloc.h"
#include "error.h"
#include "mystring.h"
#include "show.h"
#include "jobs.h"
#include "alias.h"


#define CMDTABLESIZE 31		/* should be prime */



struct tblentry {
	struct tblentry *next;	/* next entry in hash chain */
	union param param;	/* definition of builtin function */
	int special;		/* flag for special builtin commands */
	signed char cmdtype;	/* index identifying command */
	char cmdname[];		/* name of command */
};


static struct tblentry *cmdtable[CMDTABLESIZE];
static int cmdtable_cd = 0;	/* cmdtable contains cd-dependent entries */


static void tryexec(char *, char **, char **);
static void printentry(struct tblentry *, int);
static struct tblentry *cmdlookup(const char *, int);
static void delete_cmd_entry(void);
static void addcmdentry(const char *, struct cmdentry *);



/*
 * Exec a program.  Never returns.  If you change this routine, you may
 * have to change the find_command routine as well.
 *
 * The argv array may be changed and element argv[-1] should be writable.
 */

void
shellexec(char **argv, char **envp, const char *path, int idx)
{
	char *cmdname;
	const char *opt;
	int e;

	if (strchr(argv[0], '/') != NULL) {
		tryexec(argv[0], argv, envp);
		e = errno;
	} else {
		e = ENOENT;
		while ((cmdname = padvance(&path, &opt, argv[0])) != NULL) {
			if (--idx < 0 && opt == NULL) {
				tryexec(cmdname, argv, envp);
				if (errno != ENOENT && errno != ENOTDIR)
					e = errno;
				if (e == ENOEXEC)
					break;
			}
			stunalloc(cmdname);
		}
	}

	/* Map to POSIX errors */
	if (e == ENOENT || e == ENOTDIR)
		errorwithstatus(127, "%s: not found", argv[0]);
	else
		errorwithstatus(126, "%s: %s", argv[0], strerror(e));
}


static void
tryexec(char *cmd, char **argv, char **envp)
{
	int e, in;
	ssize_t n;
	char buf[256];

	execve(cmd, argv, envp);
	e = errno;
	if (e == ENOEXEC) {
		INTOFF;
		in = open(cmd, O_RDONLY | O_NONBLOCK);
		if (in != -1) {
			n = pread(in, buf, sizeof buf, 0);
			close(in);
			if (n > 0 && memchr(buf, '\0', n) != NULL) {
				errno = ENOEXEC;
				return;
			}
		}
		*argv = cmd;
		*--argv = __DECONST(char *, _PATH_BSHELL);
		execve(_PATH_BSHELL, argv, envp);
	}
	errno = e;
}

/*
 * Do a path search.  The variable path (passed by reference) should be
 * set to the start of the path before the first call; padvance will update
 * this value as it proceeds.  Successive calls to padvance will return
 * the possible path expansions in sequence.  If popt is not NULL, options
 * are processed: if an option (indicated by a percent sign) appears in
 * the path entry then *popt will be set to point to it; else *popt will be
 * set to NULL.  If popt is NULL, percent signs are not special.
 */

char *
padvance(const char **path, const char **popt, const char *name)
{
	const char *p, *start;
	char *q;
	size_t len, namelen;

	if (*path == NULL)
		return NULL;
	start = *path;
	if (popt != NULL)
		for (p = start; *p && *p != ':' && *p != '%'; p++)
			; /* nothing */
	else
		for (p = start; *p && *p != ':'; p++)
			; /* nothing */
	namelen = strlen(name);
	len = p - start + namelen + 2;	/* "2" is for '/' and '\0' */
	STARTSTACKSTR(q);
	CHECKSTRSPACE(len, q);
	if (p != start) {
		memcpy(q, start, p - start);
		q += p - start;
		*q++ = '/';
	}
	memcpy(q, name, namelen + 1);
	if (popt != NULL) {
		if (*p == '%') {
			*popt = ++p;
			while (*p && *p != ':')  p++;
		} else
			*popt = NULL;
	}
	if (*p == ':')
		*path = p + 1;
	else
		*path = NULL;
	return stalloc(len);
}



/*** Command hashing code ***/


int
hashcmd(int argc __unused, char **argv __unused)
{
	struct tblentry **pp;
	struct tblentry *cmdp;
	int c;
	int verbose;
	struct cmdentry entry;
	char *name;
	int errors;

	errors = 0;
	verbose = 0;
	while ((c = nextopt("rv")) != '\0') {
		if (c == 'r') {
			clearcmdentry();
		} else if (c == 'v') {
			verbose++;
		}
	}
	if (*argptr == NULL) {
		for (pp = cmdtable ; pp < &cmdtable[CMDTABLESIZE] ; pp++) {
			for (cmdp = *pp ; cmdp ; cmdp = cmdp->next) {
				if (cmdp->cmdtype == CMDNORMAL)
					printentry(cmdp, verbose);
			}
		}
		return 0;
	}
	while ((name = *argptr) != NULL) {
		if ((cmdp = cmdlookup(name, 0)) != NULL
		 && cmdp->cmdtype == CMDNORMAL)
			delete_cmd_entry();
		find_command(name, &entry, DO_ERR, pathval());
		if (entry.cmdtype == CMDUNKNOWN)
			errors = 1;
		else if (verbose) {
			cmdp = cmdlookup(name, 0);
			if (cmdp != NULL)
				printentry(cmdp, verbose);
			else {
				outfmt(out2, "%s: not found\n", name);
				errors = 1;
			}
			flushall();
		}
		argptr++;
	}
	return errors;
}


static void
printentry(struct tblentry *cmdp, int verbose)
{
	int idx;
	const char *path, *opt;
	char *name;

	if (cmdp->cmdtype == CMDNORMAL) {
		idx = cmdp->param.index;
		path = pathval();
		do {
			name = padvance(&path, &opt, cmdp->cmdname);
			stunalloc(name);
		} while (--idx >= 0);
		out1str(name);
	} else if (cmdp->cmdtype == CMDBUILTIN) {
		out1fmt("builtin %s", cmdp->cmdname);
	} else if (cmdp->cmdtype == CMDFUNCTION) {
		out1fmt("function %s", cmdp->cmdname);
		if (verbose) {
			INTOFF;
			name = commandtext(getfuncnode(cmdp->param.func));
			out1c(' ');
			out1str(name);
			ckfree(name);
			INTON;
		}
#ifdef DEBUG
	} else {
		error("internal error: cmdtype %d", cmdp->cmdtype);
#endif
	}
	out1c('\n');
}



/*
 * Resolve a command name.  If you change this routine, you may have to
 * change the shellexec routine as well.
 */

void
find_command(const char *name, struct cmdentry *entry, int act,
    const char *path)
{
	struct tblentry *cmdp, loc_cmd;
	int idx;
	const char *opt;
	char *fullname;
	struct stat statb;
	int e;
	int i;
	int spec;
	int cd;

	/* If name contains a slash, don't use the hash table */
	if (strchr(name, '/') != NULL) {
		entry->cmdtype = CMDNORMAL;
		entry->u.index = 0;
		entry->special = 0;
		return;
	}

	cd = 0;

	/* If name is in the table, we're done */
	if ((cmdp = cmdlookup(name, 0)) != NULL) {
		if (cmdp->cmdtype == CMDFUNCTION && act & DO_NOFUNC)
			cmdp = NULL;
		else
			goto success;
	}

	/* Check for builtin next */
	if ((i = find_builtin(name, &spec)) >= 0) {
		INTOFF;
		cmdp = cmdlookup(name, 1);
		if (cmdp->cmdtype == CMDFUNCTION)
			cmdp = &loc_cmd;
		cmdp->cmdtype = CMDBUILTIN;
		cmdp->param.index = i;
		cmdp->special = spec;
		INTON;
		goto success;
	}

	/* We have to search path. */

	e = ENOENT;
	idx = -1;
	for (;(fullname = padvance(&path, &opt, name)) != NULL;
	    stunalloc(fullname)) {
		idx++;
		if (opt) {
			if (strncmp(opt, "func", 4) == 0) {
				/* handled below */
			} else {
				continue; /* ignore unimplemented options */
			}
		}
		if (fullname[0] != '/')
			cd = 1;
		if (stat(fullname, &statb) < 0) {
			if (errno != ENOENT && errno != ENOTDIR)
				e = errno;
			continue;
		}
		e = EACCES;	/* if we fail, this will be the error */
		if (!S_ISREG(statb.st_mode))
			continue;
		if (opt) {		/* this is a %func directory */
			readcmdfile(fullname);
			if ((cmdp = cmdlookup(name, 0)) == NULL || cmdp->cmdtype != CMDFUNCTION)
				error("%s not defined in %s", name, fullname);
			stunalloc(fullname);
			goto success;
		}
#ifdef notdef
		if (statb.st_uid == geteuid()) {
			if ((statb.st_mode & 0100) == 0)
				goto loop;
		} else if (statb.st_gid == getegid()) {
			if ((statb.st_mode & 010) == 0)
				goto loop;
		} else {
			if ((statb.st_mode & 01) == 0)
				goto loop;
		}
#endif
		TRACE(("searchexec \"%s\" returns \"%s\"\n", name, fullname));
		INTOFF;
		stunalloc(fullname);
		cmdp = cmdlookup(name, 1);
		if (cmdp->cmdtype == CMDFUNCTION)
			cmdp = &loc_cmd;
		cmdp->cmdtype = CMDNORMAL;
		cmdp->param.index = idx;
		cmdp->special = 0;
		INTON;
		goto success;
	}

	if (act & DO_ERR) {
		if (e == ENOENT || e == ENOTDIR)
			outfmt(out2, "%s: not found\n", name);
		else
			outfmt(out2, "%s: %s\n", name, strerror(e));
	}
	entry->cmdtype = CMDUNKNOWN;
	entry->u.index = 0;
	entry->special = 0;
	return;

success:
	if (cd)
		cmdtable_cd = 1;
	entry->cmdtype = cmdp->cmdtype;
	entry->u = cmdp->param;
	entry->special = cmdp->special;
}



/*
 * Search the table of builtin commands.
 */

int
find_builtin(const char *name, int *special)
{
	const unsigned char *bp;
	size_t len;

	len = strlen(name);
	for (bp = builtincmd ; *bp ; bp += 2 + bp[0]) {
		if (bp[0] == len && memcmp(bp + 2, name, len) == 0) {
			*special = (bp[1] & BUILTIN_SPECIAL) != 0;
			return bp[1] & ~BUILTIN_SPECIAL;
		}
	}
	return -1;
}



/*
 * Called when a cd is done.  If any entry in cmdtable depends on the current
 * directory, simply clear cmdtable completely.
 */

void
hashcd(void)
{
	if (cmdtable_cd)
		clearcmdentry();
}



/*
 * Called before PATH is changed.  The argument is the new value of PATH;
 * pathval() still returns the old value at this point.  Called with
 * interrupts off.
 */

void
changepath(const char *newval __unused)
{
	clearcmdentry();
}


/*
 * Clear out cached utility locations.
 */

void
clearcmdentry(void)
{
	struct tblentry **tblp;
	struct tblentry **pp;
	struct tblentry *cmdp;

	INTOFF;
	for (tblp = cmdtable ; tblp < &cmdtable[CMDTABLESIZE] ; tblp++) {
		pp = tblp;
		while ((cmdp = *pp) != NULL) {
			if (cmdp->cmdtype == CMDNORMAL) {
				*pp = cmdp->next;
				ckfree(cmdp);
			} else {
				pp = &cmdp->next;
			}
		}
	}
	cmdtable_cd = 0;
	INTON;
}


/*
 * Locate a command in the command hash table.  If "add" is nonzero,
 * add the command to the table if it is not already present.  The
 * variable "lastcmdentry" is set to point to the address of the link
 * pointing to the entry, so that delete_cmd_entry can delete the
 * entry.
 */

static struct tblentry **lastcmdentry;


static struct tblentry *
cmdlookup(const char *name, int add)
{
	unsigned int hashval;
	const char *p;
	struct tblentry *cmdp;
	struct tblentry **pp;
	size_t len;

	p = name;
	hashval = (unsigned char)*p << 4;
	while (*p)
		hashval += *p++;
	pp = &cmdtable[hashval % CMDTABLESIZE];
	for (cmdp = *pp ; cmdp ; cmdp = cmdp->next) {
		if (equal(cmdp->cmdname, name))
			break;
		pp = &cmdp->next;
	}
	if (add && cmdp == NULL) {
		INTOFF;
		len = strlen(name);
		cmdp = *pp = ckmalloc(sizeof (struct tblentry) + len + 1);
		cmdp->next = NULL;
		cmdp->cmdtype = CMDUNKNOWN;
		memcpy(cmdp->cmdname, name, len + 1);
		INTON;
	}
	lastcmdentry = pp;
	return cmdp;
}

/*
 * Delete the command entry returned on the last lookup.
 */

static void
delete_cmd_entry(void)
{
	struct tblentry *cmdp;

	INTOFF;
	cmdp = *lastcmdentry;
	*lastcmdentry = cmdp->next;
	ckfree(cmdp);
	INTON;
}



/*
 * Add a new command entry, replacing any existing command entry for
 * the same name.
 */

static void
addcmdentry(const char *name, struct cmdentry *entry)
{
	struct tblentry *cmdp;

	INTOFF;
	cmdp = cmdlookup(name, 1);
	if (cmdp->cmdtype == CMDFUNCTION) {
		unreffunc(cmdp->param.func);
	}
	cmdp->cmdtype = entry->cmdtype;
	cmdp->param = entry->u;
	cmdp->special = entry->special;
	INTON;
}


/*
 * Define a shell function.
 */

void
defun(const char *name, union node *func)
{
	struct cmdentry entry;

	INTOFF;
	entry.cmdtype = CMDFUNCTION;
	entry.u.func = copyfunc(func);
	entry.special = 0;
	addcmdentry(name, &entry);
	INTON;
}


/*
 * Delete a function if it exists.
 * Called with interrupts off.
 */

int
unsetfunc(const char *name)
{
	struct tblentry *cmdp;

	if ((cmdp = cmdlookup(name, 0)) != NULL && cmdp->cmdtype == CMDFUNCTION) {
		unreffunc(cmdp->param.func);
		delete_cmd_entry();
		return (0);
	}
	return (0);
}


/*
 * Check if a function by a certain name exists.
 */
int
isfunc(const char *name)
{
	struct tblentry *cmdp;
	cmdp = cmdlookup(name, 0);
	return (cmdp != NULL && cmdp->cmdtype == CMDFUNCTION);
}


/*
 * Shared code for the following builtin commands:
 *    type, command -v, command -V
 */

int
typecmd_impl(int argc, char **argv, int cmd, const char *path)
{
	struct cmdentry entry;
	struct tblentry *cmdp;
	const char *const *pp;
	struct alias *ap;
	int i;
	int error1 = 0;

	if (path != pathval())
		clearcmdentry();

	for (i = 1; i < argc; i++) {
		/* First look at the keywords */
		for (pp = parsekwd; *pp; pp++)
			if (**pp == *argv[i] && equal(*pp, argv[i]))
				break;

		if (*pp) {
			if (cmd == TYPECMD_SMALLV)
				out1fmt("%s\n", argv[i]);
			else
				out1fmt("%s is a shell keyword\n", argv[i]);
			continue;
		}

		/* Then look at the aliases */
		if ((ap = lookupalias(argv[i], 1)) != NULL) {
			if (cmd == TYPECMD_SMALLV) {
				out1fmt("alias %s=", argv[i]);
				out1qstr(ap->val);
				outcslow('\n', out1);
			} else
				out1fmt("%s is an alias for %s\n", argv[i],
				    ap->val);
			continue;
		}

		/* Then check if it is a tracked alias */
		if ((cmdp = cmdlookup(argv[i], 0)) != NULL) {
			entry.cmdtype = cmdp->cmdtype;
			entry.u = cmdp->param;
			entry.special = cmdp->special;
		}
		else {
			/* Finally use brute force */
			find_command(argv[i], &entry, 0, path);
		}

		switch (entry.cmdtype) {
		case CMDNORMAL: {
			if (strchr(argv[i], '/') == NULL) {
				const char *path2 = path;
				const char *opt2;
				char *name;
				int j = entry.u.index;
				do {
					name = padvance(&path2, &opt2, argv[i]);
					stunalloc(name);
				} while (--j >= 0);
				if (cmd == TYPECMD_SMALLV)
					out1fmt("%s\n", name);
				else
					out1fmt("%s is%s %s\n", argv[i],
					    (cmdp && cmd == TYPECMD_TYPE) ?
						" a tracked alias for" : "",
					    name);
			} else {
				if (eaccess(argv[i], X_OK) == 0) {
					if (cmd == TYPECMD_SMALLV)
						out1fmt("%s\n", argv[i]);
					else
						out1fmt("%s is %s\n", argv[i],
						    argv[i]);
				} else {
					if (cmd != TYPECMD_SMALLV)
						outfmt(out2, "%s: %s\n",
						    argv[i], strerror(errno));
					error1 |= 127;
				}
			}
			break;
		}
		case CMDFUNCTION:
			if (cmd == TYPECMD_SMALLV)
				out1fmt("%s\n", argv[i]);
			else
				out1fmt("%s is a shell function\n", argv[i]);
			break;

		case CMDBUILTIN:
			if (cmd == TYPECMD_SMALLV)
				out1fmt("%s\n", argv[i]);
			else if (entry.special)
				out1fmt("%s is a special shell builtin\n",
				    argv[i]);
			else
				out1fmt("%s is a shell builtin\n", argv[i]);
			break;

		default:
			if (cmd != TYPECMD_SMALLV)
				outfmt(out2, "%s: not found\n", argv[i]);
			error1 |= 127;
			break;
		}
	}

	if (path != pathval())
		clearcmdentry();

	return error1;
}

/*
 * Locate and print what a word is...
 */

int
typecmd(int argc, char **argv)
{
	if (argc > 2 && strcmp(argv[1], "--") == 0)
		argc--, argv++;
	return typecmd_impl(argc, argv, TYPECMD_TYPE, bltinlookup("PATH", 1));
}
