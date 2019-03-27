/* $Header: /p/tcsh/cvsroot/tcsh/sh.exec.c,v 3.81 2016/09/12 16:33:54 christos Exp $ */
/*
 * sh.exec.c: Search, find, and execute a command!
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

RCSID("$tcsh: sh.exec.c,v 3.81 2016/09/12 16:33:54 christos Exp $")

#include "tc.h"
#include "tw.h"
#ifdef WINNT_NATIVE
#include <nt.const.h>
#endif /*WINNT_NATIVE*/

/*
 * C shell
 */

#ifndef OLDHASH
# define FASTHASH	/* Fast hashing is the default */
#endif /* OLDHASH */

/*
 * System level search and execute of a command.
 * We look in each directory for the specified command name.
 * If the name contains a '/' then we execute only the full path name.
 * If there is no search path then we execute only full path names.
 */

/*
 * As we search for the command we note the first non-trivial error
 * message for presentation to the user.  This allows us often
 * to show that a file has the wrong mode/no access when the file
 * is not in the last component of the search path, so we must
 * go on after first detecting the error.
 */
static char *exerr;		/* Execution error message */
static Char *expath;		/* Path for exerr */

/*
 * The two part hash function is designed to let texec() call the
 * more expensive hashname() only once and the simple hash() several
 * times (once for each path component checked).
 * Byte size is assumed to be 8.
 */
#define BITS_PER_BYTE	8

#ifdef FASTHASH
/*
 * xhash is an array of hash buckets which are used to hash execs.  If
 * it is allocated (havhash true), then to tell if ``name'' is
 * (possibly) present in the i'th component of the variable path, look
 * at the [hashname(name)] bucket of size [hashwidth] bytes, in the [i
 * mod size*8]'th bit.  The cache size is defaults to a length of 1024
 * buckets, each 1 byte wide.  This implementation guarantees that
 * objects n bytes wide will be aligned on n byte boundaries.
 */
# define HSHMUL		241

static unsigned long *xhash = NULL;
static unsigned int hashlength = 0, uhashlength = 0;
static unsigned int hashwidth = 0, uhashwidth = 0;
static int hashdebug = 0;

# define hash(a, b)	(((a) * HSHMUL + (b)) % (hashlength))
# define widthof(t)	(sizeof(t) * BITS_PER_BYTE)
# define tbit(f, i, t)	(((t *) xhash)[(f)] &  \
			 (1UL << (i & (widthof(t) - 1))))
# define tbis(f, i, t)	(((t *) xhash)[(f)] |= \
			 (1UL << (i & (widthof(t) - 1))))
# define cbit(f, i)	tbit(f, i, unsigned char)
# define cbis(f, i)	tbis(f, i, unsigned char)
# define sbit(f, i)	tbit(f, i, unsigned short)
# define sbis(f, i)	tbis(f, i, unsigned short)
# define ibit(f, i)	tbit(f, i, unsigned int)
# define ibis(f, i)	tbis(f, i, unsigned int)
# define lbit(f, i)	tbit(f, i, unsigned long)
# define lbis(f, i)	tbis(f, i, unsigned long)

# define bit(f, i) (hashwidth==sizeof(unsigned char)  ? cbit(f,i) : \
 		    ((hashwidth==sizeof(unsigned short) ? sbit(f,i) : \
		     ((hashwidth==sizeof(unsigned int)   ? ibit(f,i) : \
		     lbit(f,i))))))
# define bis(f, i) (hashwidth==sizeof(unsigned char)  ? cbis(f,i) : \
 		    ((hashwidth==sizeof(unsigned short) ? sbis(f,i) : \
		     ((hashwidth==sizeof(unsigned int)   ? ibis(f,i) : \
		     lbis(f,i))))))
#else /* OLDHASH */
/*
 * Xhash is an array of HSHSIZ bits (HSHSIZ / 8 chars), which are used
 * to hash execs.  If it is allocated (havhash true), then to tell
 * whether ``name'' is (possibly) present in the i'th component
 * of the variable path, you look at the bit in xhash indexed by
 * hash(hashname("name"), i).  This is setup automatically
 * after .login is executed, and recomputed whenever ``path'' is
 * changed.
 */
# define HSHSIZ		8192	/* 1k bytes */
# define HSHMASK		(HSHSIZ - 1)
# define HSHMUL		243
static char xhash[HSHSIZ / BITS_PER_BYTE];

# define hash(a, b)	(((a) * HSHMUL + (b)) & HSHMASK)
# define bit(h, b)	((h)[(b) >> 3] & 1 << ((b) & 7))	/* bit test */
# define bis(h, b)	((h)[(b) >> 3] |= 1 << ((b) & 7))	/* bit set */

#endif /* FASTHASH */

#ifdef VFORK
static int hits, misses;
#endif /* VFORK */

/* Dummy search path for just absolute search when no path */
static Char *justabs[] = {STRNULL, 0};

static	void	pexerr		(void) __attribute__((__noreturn__));
static	void	texec		(Char *, Char **);
int	hashname	(Char *);
static	int 	iscommand	(Char *);

void
doexec(struct command *t, int do_glob)
{
    Char *dp, **pv, **opv, **av, *sav;
    struct varent *v;
    int slash, gflag, rehashed;
    int hashval, i;
    Char   *blk[2];

    /*
     * Glob the command name. We will search $path even if this does something,
     * as in sh but not in csh.  One special case: if there is no PATH, then we
     * execute only commands which start with '/'.
     */
    blk[0] = t->t_dcom[0];
    blk[1] = 0;
    gflag = 0;
    if (do_glob)
	gflag = tglob(blk);
    if (gflag) {
	pv = globall(blk, gflag);
	if (pv == 0) {
	    setname(short2str(blk[0]));
	    stderror(ERR_NAME | ERR_NOMATCH);
	}
    }
    else
	pv = saveblk(blk);
    cleanup_push(pv, blk_cleanup);

    trim(pv);

    exerr = 0;
    expath = Strsave(pv[0]);
#ifdef VFORK
    Vexpath = expath;
#endif /* VFORK */

    v = adrof(STRpath);
    if (v == 0 && expath[0] != '/' && expath[0] != '.')
	pexerr();
    slash = any(short2str(expath), '/');

    /*
     * Glob the argument list, if necessary. Otherwise trim off the quote bits.
     */
    gflag = 0;
    av = &t->t_dcom[1];
    if (do_glob)
	gflag = tglob(av);
    if (gflag) {
	av = globall(av, gflag);
	if (av == 0) {
	    setname(short2str(expath));
	    stderror(ERR_NAME | ERR_NOMATCH);
	}
    }
    else
	av = saveblk(av);

    blkfree(t->t_dcom);
    cleanup_ignore(pv);
    cleanup_until(pv);
    t->t_dcom = blkspl(pv, av);
    xfree(pv);
    xfree(av);
    av = t->t_dcom;
    trim(av);

    if (*av == NULL || **av == '\0')
	pexerr();

    xechoit(av);		/* Echo command if -x */
#ifdef CLOSE_ON_EXEC
    /*
     * Since all internal file descriptors are set to close on exec, we don't
     * need to close them explicitly here.  Just reorient ourselves for error
     * messages.
     */
    SHIN = 0;
    SHOUT = 1;
    SHDIAG = 2;
    OLDSTD = 0;
    isoutatty = isatty(SHOUT);
    isdiagatty = isatty(SHDIAG);
#else
    closech();			/* Close random fd's */
#endif
    /*
     * We must do this AFTER any possible forking (like `foo` in glob) so that
     * this shell can still do subprocesses.
     */
    {
	sigset_t set;
	sigemptyset(&set);
	sigaddset(&set, SIGINT);
	sigaddset(&set, SIGCHLD);
	sigprocmask(SIG_UNBLOCK, &set, NULL);
    }
    pintr_disabled = 0;
    pchild_disabled = 0;

    /*
     * If no path, no words in path, or a / in the filename then restrict the
     * command search.
     */
    if (v == NULL || v->vec == NULL || v->vec[0] == NULL || slash)
	opv = justabs;
    else
	opv = v->vec;
    sav = Strspl(STRslash, *av);/* / command name for postpending */
#ifndef VFORK
    cleanup_push(sav, xfree);
#else /* VFORK */
    Vsav = sav;
#endif /* VFORK */
    hashval = havhash ? hashname(*av) : 0;

    rehashed = 0;
retry:
    pv = opv;
    i = 0;
#ifdef VFORK
    hits++;
#endif /* VFORK */
    do {
	/*
	 * Try to save time by looking at the hash table for where this command
	 * could be.  If we are doing delayed hashing, then we put the names in
	 * one at a time, as the user enters them.  This is kinda like Korn
	 * Shell's "tracked aliases".
	 */
	if (!slash && ABSOLUTEP(pv[0]) && havhash) {
#ifdef FASTHASH
	    if (!bit(hashval, i))
		goto cont;
#else /* OLDHASH */
	    int hashval1 = hash(hashval, i);
	    if (!bit(xhash, hashval1))
		goto cont;
#endif /* FASTHASH */
	}
	if (pv[0][0] == 0 || eq(pv[0], STRdot))	/* don't make ./xxx */
	    texec(*av, av);
	else {
	    dp = Strspl(*pv, sav);
#ifndef VFORK
	    cleanup_push(dp, xfree);
#else /* VFORK */
	    Vdp = dp;
#endif /* VFORK */

	    texec(dp, av);
#ifndef VFORK
	    cleanup_until(dp);
#else /* VFORK */
	    Vdp = 0;
	    xfree(dp);
#endif /* VFORK */
	}
#ifdef VFORK
	misses++;
#endif /* VFORK */
cont:
	pv++;
	i++;
    } while (*pv);
#ifdef VFORK
    hits--;
#endif /* VFORK */
    if (adrof(STRautorehash) && !rehashed && havhash && opv != justabs) {
	dohash(NULL, NULL);
	rehashed = 1;
	goto retry;
    }
#ifndef VFORK
    cleanup_until(sav);
#else /* VFORK */
    Vsav = 0;
    xfree(sav);
#endif /* VFORK */
    pexerr();
}

static void
pexerr(void)
{
    /* Couldn't find the damn thing */
    if (expath) {
	setname(short2str(expath));
#ifdef VFORK
	Vexpath = 0;
#endif /* VFORK */
	xfree(expath);
	expath = 0;
    }
    else
	setname("");
    if (exerr)
	stderror(ERR_NAME | ERR_STRING, exerr);
    stderror(ERR_NAME | ERR_COMMAND);
}

/*
 * Execute command f, arg list t.
 * Record error message if not found.
 * Also do shell scripts here.
 */
static void
texec(Char *sf, Char **st)
{
    char **t;
    char *f;
    struct varent *v;
    Char  **vp;
    Char   *lastsh[2];
    char    pref[2];
    int     fd;
    Char   *st0, **ost;

    /* The order for the conversions is significant */
    t = short2blk(st);
    f = short2str(sf);
#ifdef VFORK
    Vt = t;
#endif /* VFORK */
    errno = 0;			/* don't use a previous error */
#ifdef apollo
    /*
     * If we try to execute an nfs mounted directory on the apollo, we
     * hang forever. So until apollo fixes that..
     */
    {
	struct stat stb;
	if (stat(f, &stb) == 0 && S_ISDIR(stb.st_mode))
	    errno = EISDIR;
    }
    if (errno == 0)
#endif /* apollo */
    {
#ifdef ISC_POSIX_EXEC_BUG
	__setostype(0);		/* "0" is "__OS_SYSV" in <sys/user.h> */
#endif /* ISC_POSIX_EXEC_BUG */
	(void) execv(f, t);
#ifdef ISC_POSIX_EXEC_BUG
	__setostype(1);		/* "1" is "__OS_POSIX" in <sys/user.h> */
#endif /* ISC_POSIX_EXEC_BUG */
    }
#ifdef VFORK
    Vt = 0;
#endif /* VFORK */
    blkfree((Char **) t);
    switch (errno) {

    case ENOEXEC:
#ifdef WINNT_NATIVE
		nt_feed_to_cmd(f,t);
#endif /* WINNT_NATIVE */
	/*
	 * From: casper@fwi.uva.nl (Casper H.S. Dik) If we could not execute
	 * it, don't feed it to the shell if it looks like a binary!
	 */
	if ((fd = xopen(f, O_RDONLY|O_LARGEFILE)) != -1) {
	    int nread;
	    if ((nread = xread(fd, pref, 2)) == 2) {
		if (!isprint((unsigned char)pref[0]) &&
		    (pref[0] != '\n' && pref[0] != '\t')) {
		    int err;

		    err = errno;
		    xclose(fd);
		    /*
		     * We *know* what ENOEXEC means.
		     */
		    stderror(ERR_ARCH, f, strerror(err));
		}
	    }
	    else if (nread < 0) {
#ifdef convex
		int err;

		err = errno;
		xclose(fd);
		/* need to print error incase the file is migrated */
		stderror(ERR_SYSTEM, f, strerror(err));
#endif
	    }
#ifdef _PATH_BSHELL
	    else {
		pref[0] = '#';
		pref[1] = '\0';
	    }
#endif
	}
#ifdef HASHBANG
	if (fd == -1 ||
	    pref[0] != '#' || pref[1] != '!' || hashbang(fd, &vp) == -1) {
#endif /* HASHBANG */
	/*
	 * If there is an alias for shell, then put the words of the alias in
	 * front of the argument list replacing the command name. Note no
	 * interpretation of the words at this point.
	 */
	    v = adrof1(STRshell, &aliases);
	    if (v == NULL || v->vec == NULL) {
		vp = lastsh;
		vp[0] = adrof(STRshell) ? varval(STRshell) : STR_SHELLPATH;
		vp[1] = NULL;
#ifdef _PATH_BSHELL
		if (fd != -1 
# ifndef ISC	/* Compatible with ISC's /bin/csh */
		    && pref[0] != '#'
# endif /* ISC */
		    )
		    vp[0] = STR_BSHELL;
#endif
		vp = saveblk(vp);
	    }
	    else
		vp = saveblk(v->vec);
#ifdef HASHBANG
	}
#endif /* HASHBANG */
	if (fd != -1)
	    xclose(fd);

	st0 = st[0];
	st[0] = sf;
	ost = st;
	st = blkspl(vp, st);	/* Splice up the new arglst */
	ost[0] = st0;
	sf = *st;
	/* The order for the conversions is significant */
	t = short2blk(st);
	f = short2str(sf);
	xfree(st);
	blkfree((Char **) vp);
#ifdef VFORK
	Vt = t;
#endif /* VFORK */
#ifdef ISC_POSIX_EXEC_BUG
	__setostype(0);		/* "0" is "__OS_SYSV" in <sys/user.h> */
#endif /* ISC_POSIX_EXEC_BUG */
	(void) execv(f, t);
#ifdef ISC_POSIX_EXEC_BUG
	__setostype(1);		/* "1" is "__OS_POSIX" in <sys/user.h> */
#endif /* ISC_POSIX_EXEC_BUG */
#ifdef VFORK
	Vt = 0;
#endif /* VFORK */
	blkfree((Char **) t);
	/* The sky is falling, the sky is falling! */
	stderror(ERR_SYSTEM, f, strerror(errno));
	break;

    case ENOMEM:
	stderror(ERR_SYSTEM, f, strerror(errno));
	break;

#ifdef _IBMR2
    case 0:			/* execv fails and returns 0! */
#endif /* _IBMR2 */
    case ENOENT:
	break;

    default:
	if (exerr == 0) {
	    exerr = strerror(errno);
	    xfree(expath);
	    expath = Strsave(sf);
#ifdef VFORK
	    Vexpath = expath;
#endif /* VFORK */
	}
	break;
    }
}

struct execash_state
{
    int saveIN, saveOUT, saveDIAG, saveSTD;
    int SHIN, SHOUT, SHDIAG, OLDSTD;
    int didfds;
#ifndef CLOSE_ON_EXEC
    int didcch;
#endif
    struct sigaction sigint, sigquit, sigterm;
};

static void
execash_cleanup(void *xstate)
{
    struct execash_state *state;

    state = xstate;
    sigaction(SIGINT, &state->sigint, NULL);
    sigaction(SIGQUIT, &state->sigquit, NULL);
    sigaction(SIGTERM, &state->sigterm, NULL);

    doneinp = 0;
#ifndef CLOSE_ON_EXEC
    didcch = state->didcch;
#endif /* CLOSE_ON_EXEC */
    didfds = state->didfds;
    xclose(SHIN);
    xclose(SHOUT);
    xclose(SHDIAG);
    xclose(OLDSTD);
    close_on_exec(SHIN = dmove(state->saveIN, state->SHIN), 1);
    close_on_exec(SHOUT = dmove(state->saveOUT, state->SHOUT), 1);
    close_on_exec(SHDIAG = dmove(state->saveDIAG, state->SHDIAG), 1);
    close_on_exec(OLDSTD = dmove(state->saveSTD, state->OLDSTD), 1);
}

/*ARGSUSED*/
void
execash(Char **t, struct command *kp)
{
    struct execash_state state;

    USE(t);
    if (chkstop == 0 && setintr)
	panystop(0);
    /*
     * Hmm, we don't really want to do that now because we might
     * fail, but what is the choice
     */
    rechist(NULL, adrof(STRsavehist) != NULL);


    sigaction(SIGINT, &parintr, &state.sigint);
    sigaction(SIGQUIT, &parintr, &state.sigquit);
    sigaction(SIGTERM, &parterm, &state.sigterm);

    state.didfds = didfds;
#ifndef CLOSE_ON_EXEC
    state.didcch = didcch;
#endif /* CLOSE_ON_EXEC */
    state.SHIN = SHIN;
    state.SHOUT = SHOUT;
    state.SHDIAG = SHDIAG;
    state.OLDSTD = OLDSTD;

    (void)close_on_exec (state.saveIN = dcopy(SHIN, -1), 1);
    (void)close_on_exec (state.saveOUT = dcopy(SHOUT, -1), 1);
    (void)close_on_exec (state.saveDIAG = dcopy(SHDIAG, -1), 1);
    (void)close_on_exec (state.saveSTD = dcopy(OLDSTD, -1), 1);

    lshift(kp->t_dcom, 1);

    (void)close_on_exec (SHIN = dcopy(0, -1), 1);
    (void)close_on_exec (SHOUT = dcopy(1, -1), 1);
    (void)close_on_exec (SHDIAG = dcopy(2, -1), 1);
#ifndef CLOSE_ON_EXEC
    didcch = 0;
#endif /* CLOSE_ON_EXEC */
    didfds = 0;
    cleanup_push(&state, execash_cleanup);

    /*
     * Decrement the shell level, if not in a subshell
     */
    if (mainpid == getpid())
	shlvl(-1);
#ifdef WINNT_NATIVE
    __nt_really_exec=1;
#endif /* WINNT_NATIVE */
    doexec(kp, 1);

    cleanup_until(&state);
}

void
xechoit(Char **t)
{
    if (adrof(STRecho)) {
	int odidfds = didfds;
	flush();
	haderr = 1;
	didfds = 0;
	blkpr(t), xputchar('\n');
	flush();
	didfds = odidfds;
	haderr = 0;
    }
}

/*ARGSUSED*/
void
dohash(Char **vv, struct command *c)
{
#ifdef COMMENT
    struct stat stb;
#endif
    DIR    *dirp;
    struct dirent *dp;
    int     i = 0;
    struct varent *v = adrof(STRpath);
    Char  **pv;
    int hashval;
#ifdef WINNT_NATIVE
    int is_windir; /* check if it is the windows directory */
    USE(hashval);
#endif /* WINNT_NATIVE */

    USE(c);
#ifdef FASTHASH
    if (vv && vv[1]) {
        uhashlength = atoi(short2str(vv[1]));
        if (vv[2]) {
	    uhashwidth = atoi(short2str(vv[2]));
	    if ((uhashwidth != sizeof(unsigned char)) && 
	        (uhashwidth != sizeof(unsigned short)) && 
	        (uhashwidth != sizeof(unsigned long)))
	        uhashwidth = 0;
	    if (vv[3])
		hashdebug = atoi(short2str(vv[3]));
        }
    }

    if (uhashwidth)
	hashwidth = uhashwidth;
    else {
	hashwidth = 0;
	if (v == NULL)
	    return;
	for (pv = v->vec; pv && *pv; pv++, hashwidth++)
	    continue;
	if (hashwidth <= widthof(unsigned char))
	    hashwidth = sizeof(unsigned char);
	else if (hashwidth <= widthof(unsigned short))
	    hashwidth = sizeof(unsigned short);
	else if (hashwidth <= widthof(unsigned int))
	    hashwidth = sizeof(unsigned int);
	else
	    hashwidth = sizeof(unsigned long);
    }

    if (uhashlength)
	hashlength = uhashlength;
    else
        hashlength = hashwidth * (8*64);/* "average" files per dir in path */

    xfree(xhash);
    xhash = xcalloc(hashlength * hashwidth, 1);
#endif /* FASTHASH */

    (void) getusername(NULL);	/* flush the tilde cashe */
    tw_cmd_free();
    havhash = 1;
    if (v == NULL)
	return;
    for (pv = v->vec; pv && *pv; pv++, i++) {
	if (!ABSOLUTEP(pv[0]))
	    continue;
	dirp = opendir(short2str(*pv));
	if (dirp == NULL)
	    continue;
	cleanup_push(dirp, opendir_cleanup);
#ifdef COMMENT			/* this isn't needed.  opendir won't open
				 * non-dirs */
	if (fstat(dirp->dd_fd, &stb) < 0 || !S_ISDIR(stb.st_mode)) {
	    cleanup_until(dirp);
	    continue;
	}
#endif
#ifdef WINNT_NATIVE
	is_windir = nt_check_if_windir(short2str(*pv));
#endif /* WINNT_NATIVE */
	while ((dp = readdir(dirp)) != NULL) {
	    if (dp->d_ino == 0)
		continue;
	    if (dp->d_name[0] == '.' &&
		(dp->d_name[1] == '\0' ||
		 (dp->d_name[1] == '.' && dp->d_name[2] == '\0')))
		continue;
#ifdef WINNT_NATIVE
	    nt_check_name_and_hash(is_windir, dp->d_name, i);
#else /* !WINNT_NATIVE*/
#if defined(_UWIN) || defined(__CYGWIN__)
	    /* Turn foo.{exe,com,bat} into foo since UWIN's readdir returns
	     * the file with the .exe, .com, .bat extension
	     *
	     * Same for Cygwin, but only for .exe and .com extension.
	     */
	    {
		ssize_t	ext = strlen(dp->d_name) - 4;
		if ((ext > 0) && (strcasecmp(&dp->d_name[ext], ".exe") == 0 ||
#ifndef __CYGWIN__
				  strcasecmp(&dp->d_name[ext], ".bat") == 0 ||
#endif
				  strcasecmp(&dp->d_name[ext], ".com") == 0)) {
#ifdef __CYGWIN__
		    /* Also store the variation with extension. */
		    hashval = hashname(str2short(dp->d_name));
		    bis(hashval, i);
#endif /* __CYGWIN__ */
		    dp->d_name[ext] = '\0';
		}
	    }
#endif /* _UWIN || __CYGWIN__ */
# ifdef FASTHASH
	    hashval = hashname(str2short(dp->d_name));
	    bis(hashval, i);
	    if (hashdebug & 1)
	        xprintf(CGETS(13, 1, "hash=%-4d dir=%-2d prog=%s\n"),
		        hashname(str2short(dp->d_name)), i, dp->d_name);
# else /* OLD HASH */
	    hashval = hash(hashname(str2short(dp->d_name)), i);
	    bis(xhash, hashval);
# endif /* FASTHASH */
	    /* tw_add_comm_name (dp->d_name); */
#endif /* WINNT_NATIVE */
	}
	cleanup_until(dirp);
    }
}

/*ARGSUSED*/
void
dounhash(Char **v, struct command *c)
{
    USE(c);
    USE(v);
    havhash = 0;
#ifdef FASTHASH
    xfree(xhash);
    xhash = NULL;
#endif /* FASTHASH */
}

/*ARGSUSED*/
void
hashstat(Char **v, struct command *c)
{
    USE(c);
    USE(v);
#ifdef FASTHASH 
   if (havhash && hashlength && hashwidth)
      xprintf(CGETS(13, 2, "%d hash buckets of %d bits each\n"),
	      hashlength, hashwidth*8);
   if (hashdebug)
      xprintf(CGETS(13, 3, "debug mask = 0x%08x\n"), hashdebug);
#endif /* FASTHASH */
#ifdef VFORK
   if (hits + misses)
      xprintf(CGETS(13, 4, "%d hits, %d misses, %d%%\n"),
	      hits, misses, 100 * hits / (hits + misses));
#endif
}


/*
 * Hash a command name.
 */
int
hashname(Char *cp)
{
    unsigned long h;

    for (h = 0; *cp; cp++)
	h = hash(h, *cp);
    return ((int) h);
}

static int
iscommand(Char *name)
{
    Char **opv, **pv;
    Char *sav;
    struct varent *v;
    int slash = any(short2str(name), '/');
    int hashval, rehashed, i;

    v = adrof(STRpath);
    if (v == NULL || v->vec == NULL || v->vec[0] == NULL || slash)
	opv = justabs;
    else
	opv = v->vec;
    sav = Strspl(STRslash, name);	/* / command name for postpending */
    hashval = havhash ? hashname(name) : 0;

    rehashed = 0;
retry:
    pv = opv;
    i = 0;
    do {
	if (!slash && ABSOLUTEP(pv[0]) && havhash) {
#ifdef FASTHASH
	    if (!bit(hashval, i))
		goto cont;
#else /* OLDHASH */
	    int hashval1 = hash(hashval, i);
	    if (!bit(xhash, hashval1))
		goto cont;
#endif /* FASTHASH */
	}
	if (pv[0][0] == 0 || eq(pv[0], STRdot)) {	/* don't make ./xxx */
	    if (executable(NULL, name, 0)) {
		xfree(sav);
		return i + 1;
	    }
	}
	else {
	    if (executable(*pv, sav, 0)) {
		xfree(sav);
		return i + 1;
	    }
	}
cont:
	pv++;
	i++;
    } while (*pv);
    if (adrof(STRautorehash) && !rehashed && havhash && opv != justabs) {
	dohash(NULL, NULL);
	rehashed = 1;
	goto retry;
    }
    xfree(sav);
    return 0;
}

/* Also by:
 *  Andreas Luik <luik@isaak.isa.de>
 *  I S A  GmbH - Informationssysteme fuer computerintegrierte Automatisierung
 *  Azenberstr. 35
 *  D-7000 Stuttgart 1
 *  West-Germany
 * is the executable() routine below and changes to iscommand().
 * Thanks again!!
 */

#ifndef WINNT_NATIVE
/*
 * executable() examines the pathname obtained by concatenating dir and name
 * (dir may be NULL), and returns 1 either if it is executable by us, or
 * if dir_ok is set and the pathname refers to a directory.
 * This is a bit kludgy, but in the name of optimization...
 */
int
executable(const Char *dir, const Char *name, int dir_ok)
{
    struct stat stbuf;
    char   *strname;

    if (dir && *dir) {
	Char *path;

	path = Strspl(dir, name);
	strname = short2str(path);
	xfree(path);
    }
    else
	strname = short2str(name);

    return (stat(strname, &stbuf) != -1 &&
	    ((dir_ok && S_ISDIR(stbuf.st_mode)) ||
	     (S_ISREG(stbuf.st_mode) &&
    /* save time by not calling access() in the hopeless case */
	      (stbuf.st_mode & (S_IXOTH | S_IXGRP | S_IXUSR)) &&
	      access(strname, X_OK) == 0
	)));
}
#endif /*!WINNT_NATIVE*/

struct tellmewhat_s0_cleanup
{
    Char **dest, *val;
};

static void
tellmewhat_s0_cleanup(void *xstate)
{
    struct tellmewhat_s0_cleanup *state;

    state = xstate;
    *state->dest = state->val;
}

int
tellmewhat(struct wordent *lexp, Char **str)
{
    struct tellmewhat_s0_cleanup s0;
    int i;
    const struct biltins *bptr;
    struct wordent *sp = lexp->next;
    int    aliased = 0, found;
    Char   *s1, *s2, *cmd;
    Char    qc;

    if (adrof1(sp->word, &aliases)) {
	alias(lexp);
	sp = lexp->next;
	aliased = 1;
    }

    s0.dest = &sp->word;	/* to get the memory freeing right... */
    s0.val = sp->word;
    cleanup_push(&s0, tellmewhat_s0_cleanup);

    /* handle quoted alias hack */
    if ((*(sp->word) & (QUOTE | TRIM)) == QUOTE)
	(sp->word)++;

    /* do quoting, if it hasn't been done */
    s1 = s2 = sp->word;
    while (*s2)
	switch (*s2) {
	case '\'':
	case '"':
	    qc = *s2++;
	    while (*s2 && *s2 != qc)
		*s1++ = *s2++ | QUOTE;
	    if (*s2)
		s2++;
	    break;
	case '\\':
	    if (*++s2)
		*s1++ = *s2++ | QUOTE;
	    break;
	default:
	    *s1++ = *s2++;
	}
    *s1 = '\0';

    for (bptr = bfunc; bptr < &bfunc[nbfunc]; bptr++) {
	if (eq(sp->word, str2short(bptr->bname))) {
	    if (str == NULL) {
		if (aliased)
		    prlex(lexp);
		xprintf(CGETS(13, 5, "%S: shell built-in command.\n"),
			      sp->word);
		flush();
	    }
	    else
		*str = Strsave(sp->word);
	    cleanup_until(&s0);
	    return TRUE;
	}
    }
#ifdef WINNT_NATIVE
    for (bptr = nt_bfunc; bptr < &nt_bfunc[nt_nbfunc]; bptr++) {
	if (eq(sp->word, str2short(bptr->bname))) {
	    if (str == NULL) {
		if (aliased)
		    prlex(lexp);
		xprintf(CGETS(13, 5, "%S: shell built-in command.\n"),
			      sp->word);
		flush();
	    }
	    else
		*str = Strsave(sp->word);
	    cleanup_until(&s0);
	    return TRUE;
	}
    }
#endif /* WINNT_NATIVE*/

    sp->word = cmd = globone(sp->word, G_IGNORE);
    cleanup_push(cmd, xfree);

    if ((i = iscommand(sp->word)) != 0) {
	Char **pv;
	struct varent *v;
	int    slash = any(short2str(sp->word), '/');

	v = adrof(STRpath);
	if (v == NULL || v->vec == NULL || v->vec[0] == NULL || slash)
	    pv = justabs;
	else
	    pv = v->vec;

	pv += i - 1;
	if (pv[0][0] == 0 || eq(pv[0], STRdot)) {
	    if (!slash) {
		sp->word = Strspl(STRdotsl, sp->word);
		cleanup_push(sp->word, xfree);
		prlex(lexp);
		cleanup_until(sp->word);
	    }
	    else
		prlex(lexp);
	}
	else {
	    s1 = Strspl(*pv, STRslash);
	    sp->word = Strspl(s1, sp->word);
	    xfree(s1);
	    cleanup_push(sp->word, xfree);
	    if (str == NULL)
		prlex(lexp);
	    else
		*str = Strsave(sp->word);
	    cleanup_until(sp->word);
	}
	found = 1;
    }
    else {
	if (str == NULL) {
	    if (aliased)
		prlex(lexp);
	    xprintf(CGETS(13, 6, "%S: Command not found.\n"), sp->word);
	    flush();
	}
	else
	    *str = Strsave(sp->word);
	found = 0;
    }
    cleanup_until(&s0);
    return found;
}

/*
 * Builtin to look at and list all places a command may be defined:
 * aliases, shell builtins, and the path.
 *
 * Marc Horowitz <marc@mit.edu>
 * MIT Student Information Processing Board
 */

/*ARGSUSED*/
void
dowhere(Char **v, struct command *c)
{
    int found = 1;
    USE(c);

    if (adrof(STRautorehash))
	dohash(NULL, NULL);
    for (v++; *v; v++)
	found &= find_cmd(*v, 1);
    /* Make status nonzero if any command is not found. */
    if (!found)
	setcopy(STRstatus, STR1, VAR_READWRITE);
}

int
find_cmd(Char *cmd, int prt)
{
    struct varent *var;
    const struct biltins *bptr;
    Char **pv;
    Char *sv;
    int hashval, rehashed, i, ex, rval = 0;

    if (prt && any(short2str(cmd), '/')) {
	xprintf("%s", CGETS(13, 7, "where: / in command makes no sense\n"));
	return rval;
    }

    /* first, look for an alias */

    if (prt && adrof1(cmd, &aliases)) {
	if ((var = adrof1(cmd, &aliases)) != NULL) {
	    xprintf(CGETS(13, 8, "%S is aliased to "), cmd);
	    if (var->vec != NULL)
		blkpr(var->vec);
	    xputchar('\n');
	    rval = 1;
	}
    }

    /* next, look for a shell builtin */

    for (bptr = bfunc; bptr < &bfunc[nbfunc]; bptr++) {
	if (eq(cmd, str2short(bptr->bname))) {
	    rval = 1;
	    if (prt)
		xprintf(CGETS(13, 9, "%S is a shell built-in\n"), cmd);
	    else
		return rval;
	}
    }
#ifdef WINNT_NATIVE
    for (bptr = nt_bfunc; bptr < &nt_bfunc[nt_nbfunc]; bptr++) {
	if (eq(cmd, str2short(bptr->bname))) {
	    rval = 1;
	    if (prt)
		xprintf(CGETS(13, 9, "%S is a shell built-in\n"), cmd);
	    else
		return rval;
	}
    }
#endif /* WINNT_NATIVE*/

    /* last, look through the path for the command */

    if ((var = adrof(STRpath)) == NULL)
	return rval;

    hashval = havhash ? hashname(cmd) : 0;

    sv = Strspl(STRslash, cmd);
    cleanup_push(sv, xfree);

    rehashed = 0;
retry:
    for (pv = var->vec, i = 0; pv && *pv; pv++, i++) {
	if (havhash && !eq(*pv, STRdot)) {
#ifdef FASTHASH
	    if (!bit(hashval, i))
		continue;
#else /* OLDHASH */
	    int hashval1 = hash(hashval, i);
	    if (!bit(xhash, hashval1))
		continue;
#endif /* FASTHASH */
	}
	ex = executable(*pv, sv, 0);
#ifdef FASTHASH
	if (!ex && (hashdebug & 2)) {
	    xprintf("%s", CGETS(13, 10, "hash miss: "));
	    ex = 1;	/* Force printing */
	}
#endif /* FASTHASH */
	if (ex) {
	    rval = 1;
	    if (prt) {
		xprintf("%S/", *pv);
		xprintf("%S\n", cmd);
	    }
	    else
		return rval;
	}
    }
    /*
     * If we are printing, we are being called from dowhere() which it 
     * has rehashed already
     */
    if (!prt && adrof(STRautorehash) && !rehashed && havhash) {
	dohash(NULL, NULL);
	rehashed = 1;
	goto retry;
    }
    cleanup_until(sv);
    return rval;
}
#ifdef WINNT_NATIVE
int hashval_extern(cp)
	Char *cp;
{
	return havhash?hashname(cp):0;
}
int bit_extern(val,i)
	int val;
	int i;
{
	return bit(val,i);
}
void bis_extern(val,i)
	int val;
	int i;
{
	bis(val,i);
}
#endif /* WINNT_NATIVE */

