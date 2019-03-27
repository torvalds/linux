/* $Header: /p/tcsh/cvsroot/tcsh/tw.help.c,v 3.27 2006/08/24 20:56:31 christos Exp $ */
/* tw.help.c: actually look up and print documentation on a file.
 *	      Look down the path for an appropriate file, then print it.
 *	      Note that the printing is NOT PAGED.  This is because the
 *	      function is NOT meant to look at manual pages, it only does so
 *	      if there is no .help file to look in.
 */
/*-
 * Copyright (c) 1980, 1991 The Regents of the University of California.
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
#include "sh.h"

RCSID("$tcsh: tw.help.c,v 3.27 2006/08/24 20:56:31 christos Exp $")

#include "tw.h"
#include "tc.h"


static int f = -1;
static	void		 cleanf		(int);
static	Char    	*skipslist	(Char *);
static	void		 nextslist 	(const Char *, Char *);

static const char *const h_ext[] = {
    ".help", ".1", ".8", ".6", "", NULL
};

void
do_help(const Char *command)
{
    Char   *name, *cmd_p;

    /* trim off the whitespace at the beginning */
    while (*command == ' ' || *command == '\t')
        command++;

    /* copy the string to a safe place */
    name = Strsave(command);
    cleanup_push(name, xfree);

    /* trim off the whitespace that may be at the end */
    for (cmd_p = name;
	 *cmd_p != ' ' && *cmd_p != '\t' && *cmd_p != '\0'; cmd_p++)
	continue;
    *cmd_p = '\0';

    /* if nothing left, return */
    if (*name == '\0') {
	cleanup_until(name);
	return;
    }

    if (adrof1(STRhelpcommand, &aliases)) {	/* if we have an alias */
	jmp_buf_t osetexit;
	size_t omark;

	getexit(osetexit);	/* make sure to come back here */
	omark = cleanup_push_mark();
	if (setexit() == 0)
	    aliasrun(2, STRhelpcommand, name);	/* then use it. */
	cleanup_pop_mark(omark);
	resexit(osetexit);	/* and finish up */
    }
    else {			/* else cat something to them */
	Char *thpath, *hpath;	/* The environment parameter */
	Char *curdir;	        /* Current directory being looked at */
	struct Strbuf full = Strbuf_INIT;

	/* got is, now "cat" the file based on the path $HPATH */

	hpath = str2short(getenv(SEARCHLIST));
	if (hpath == NULL)
	    hpath = str2short(DEFAULTLIST);
	thpath = hpath = Strsave(hpath);
	cleanup_push(thpath, xfree);
	curdir = xmalloc((Strlen(thpath) + 1) * sizeof (*curdir));
	cleanup_push(curdir, xfree);
	cleanup_push(&full, Strbuf_cleanup);

	for (;;) {
	    const char *const *sp;
	    size_t ep;

	    if (!*hpath) {
		xprintf(CGETS(29, 1, "No help file for %S\n"), name);
		break;
	    }
	    nextslist(hpath, curdir);
	    hpath = skipslist(hpath);

	    /*
	     * now make the full path name - try first /bar/foo.help, then
	     * /bar/foo.1, /bar/foo.8, then finally /bar/foo.6.  This is so
	     * that you don't spit a binary at the tty when $HPATH == $PATH.
	     */
	    full.len = 0;
	    Strbuf_append(&full, curdir);
	    Strbuf_append(&full, STRslash);
	    Strbuf_append(&full, name);
	    ep = full.len;
	    for (sp = h_ext; *sp; sp++) {
		full.len = ep;
		Strbuf_append(&full, str2short(*sp));
		Strbuf_terminate(&full);
		if ((f = xopen(short2str(full.s), O_RDONLY|O_LARGEFILE)) != -1)
		    break;
	    }
	    if (f != -1) {
	        unsigned char buf[512];
		sigset_t oset, set;
		struct sigaction osa, sa;
		ssize_t len;

		/* so cat it to the terminal */
		cleanup_push(&f, open_cleanup);
		sa.sa_handler = cleanf;
		sigemptyset(&sa.sa_mask);
		sa.sa_flags = 0;
		(void)sigaction(SIGINT, &sa, &osa);
		cleanup_push(&osa, sigint_cleanup);
		(void)sigprocmask(SIG_UNBLOCK, &set, &oset);
		cleanup_push(&oset, sigprocmask_cleanup);
		while ((len = xread(f, buf, sizeof(buf))) > 0)
		    (void) xwrite(SHOUT, buf, len);
		cleanup_until(&f);
#ifdef convex
		/* print error in case file is migrated */
		if (len == -1)
		    stderror(ERR_SYSTEM, progname, strerror(errno));
#endif /* convex */
		break;
	    }
	}
    }
    cleanup_until(name);
}

static void
/*ARGSUSED*/
cleanf(int snum)
{
    USE(snum);
    if (f != -1)
	xclose(f);
    f = -1;
}

/* these next two are stolen from CMU's man(1) command for looking down
 * paths.  they are prety straight forward. */

/*
 * nextslist takes a search list and copies the next path in it
 * to np.  A null search list entry is expanded to ".".
 * If there are no entries in the search list, then np will point
 * to a null string.
 */

static void
nextslist(const Char *sl, Char *np)
{
    if (!*sl)
	*np = '\000';
    else if (*sl == ':') {
	*np++ = '.';
	*np = '\000';
    }
    else {
	while (*sl && *sl != ':')
	    *np++ = *sl++;
	*np = '\000';
    }
}

/*
 * skipslist returns the pointer to the next entry in the search list.
 */

static Char *
skipslist(Char *sl)
{
    while (*sl && *sl++ != ':')
	continue;
    return (sl);
}
