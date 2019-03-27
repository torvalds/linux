/* $Header: /p/tcsh/cvsroot/tcsh/sh.dir.c,v 3.85 2016/04/08 16:10:52 christos Exp $ */
/*
 * sh.dir.c: Directory manipulation functions
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
#include "ed.h"

RCSID("$tcsh: sh.dir.c,v 3.85 2016/04/08 16:10:52 christos Exp $")

/*
 * C Shell - directory management
 */

static	Char			*agetcwd	(void);
static	void			 dstart		(const char *);
static	struct directory	*dfind		(Char *);
static	Char 			*dfollow	(Char *, int);
static	void 	 	 	 printdirs	(int);
static	Char 			*dgoto		(Char *);
static	void 	 	 	 dnewcwd	(struct directory *, int);
static	void 	 	 	 dset		(Char *);
static  void 			 dextract	(struct directory *);
static  int 			 skipargs	(Char ***, const char *,
						 const char *);
static	void			 dgetstack	(void);

static struct directory dhead INIT_ZERO_STRUCT;		/* "head" of loop */
static int    printd;			/* force name to be printed */

int     bequiet = 0;		/* do not print dir stack -strike */

static Char *
agetcwd(void)
{
    char *buf;
    Char *cwd;
    size_t len;

    len = MAXPATHLEN;
    buf = xmalloc(len);
    while (getcwd(buf, len) == NULL) {
	int err;

	err = errno;
	if (err != ERANGE) {
	    xfree(buf);
	    errno = err;
	    return NULL;
	}
	len *= 2;
	buf = xrealloc(buf, len);
    }
    if (*buf == '\0') {
	xfree(buf);
	return NULL;
    }
    cwd = SAVE(buf);
    xfree(buf);
    return cwd;
}

static void
dstart(const char *from)
{
    xprintf(CGETS(12, 1, "%s: Trying to start from \"%s\"\n"), progname, from);
}

/*
 * dinit - initialize current working directory
 */
void
dinit(Char *hp)
{
    Char *cp, *tcp;
    struct directory *dp;

    /* Don't believe the login shell home, because it may be a symlink */
    tcp = agetcwd();
    if (tcp == NULL) {
	xprintf("%s: %s\n", progname, strerror(errno));
	if (hp && *hp) {
	    char *xcp = short2str(hp);
	    dstart(xcp);
	    if (chdir(xcp) == -1)
		cp = NULL;
	    else
		cp = Strsave(hp);
	}
	else
	    cp = NULL;
	if (cp == NULL) {
	    dstart("/");
	    if (chdir("/") == -1)
		/* I am not even try to print an error message! */
		xexit(1);
	    cp = SAVE("/");
	}
    }
    else {
#ifdef S_IFLNK
	struct stat swd, shp;
	int swd_ok;

	swd_ok = stat(short2str(tcp), &swd) == 0;
	/*
	 * See if $HOME is the working directory we got and use that
	 */
	if (swd_ok && hp && *hp && stat(short2str(hp), &shp) != -1 &&
	    DEV_DEV_COMPARE(swd.st_dev, shp.st_dev)  &&
		swd.st_ino == shp.st_ino)
	    cp = Strsave(hp);
	else {
	    char   *cwd;

	    /*
	     * use PWD if we have it (for subshells)
	     */
	    if (swd_ok && (cwd = getenv("PWD")) != NULL) {
		if (stat(cwd, &shp) != -1 &&
			DEV_DEV_COMPARE(swd.st_dev, shp.st_dev) &&
		        swd.st_ino == shp.st_ino) {
		    tcp = SAVE(cwd);
		    cleanup_push(tcp, xfree);
		}
	    }
	    cleanup_push(tcp, xfree);
	    cp = dcanon(tcp, STRNULL);
	    cleanup_ignore(tcp);
	    cleanup_until(tcp);
	}
#else /* S_IFLNK */
	cleanup_push(tcp, xfree);
	cp = dcanon(tcp, STRNULL);
	cleanup_ignore(tcp);
	cleanup_until(tcp);
#endif /* S_IFLNK */
    }

    dp = xcalloc(sizeof(struct directory), 1);
    dp->di_name = cp;
    dp->di_count = 0;
    dhead.di_next = dhead.di_prev = dp;
    dp->di_next = dp->di_prev = &dhead;
    printd = 0;
    dnewcwd(dp, 0);
    setcopy(STRdirstack, dp->di_name, VAR_READWRITE|VAR_NOGLOB);
}

static void
dset(Char *dp)
{
    /*
     * Don't call set() directly cause if the directory contains ` or
     * other junk characters glob will fail. 
     */
    setcopy(STRowd, varval(STRcwd), VAR_READWRITE|VAR_NOGLOB);
    setcopy(STRcwd, dp, VAR_READWRITE|VAR_NOGLOB);
    tsetenv(STRPWD, dp);
}

#define DIR_PRINT	0x01	/* -p */
#define DIR_LONG  	0x02	/* -l */
#define DIR_VERT  	0x04	/* -v */
#define DIR_LINE  	0x08	/* -n */
#define DIR_SAVE 	0x10	/* -S */
#define DIR_LOAD	0x20	/* -L */
#define DIR_CLEAR	0x40	/* -c */
#define DIR_OLD	  	0x80	/* - */

static int
skipargs(Char ***v, const char *dstr, const char *str)
{
    Char  **n = *v, *s;

    int dflag = 0, loop = 1;
    for (n++; loop && *n != NULL && (*n)[0] == '-'; n++) 
	if (*(s = &((*n)[1])) == '\0')	/* test for bare "-" argument */
	    dflag |= DIR_OLD;
	else if ((*n)[1] == '-' && (*n)[2] == '\0') {   /* test for -- */
	    n++;
	    break;
	} else {
	    char *p;
	    while (*s != '\0')	/* examine flags */ {
		if ((p = strchr(dstr, *s++)) != NULL)
		    dflag |= (1 << (p - dstr));
	        else
		    stderror(ERR_DIRUS, short2str(**v), dstr, str);
	    }
	}
    if (*n && (dflag & DIR_OLD))
	stderror(ERR_DIRUS, short2str(**v), dstr, str);
    *v = n;
    /* make -l, -v, and -n imply -p */
    if (dflag & (DIR_LONG|DIR_VERT|DIR_LINE))
	dflag |= DIR_PRINT;
    return dflag;
}

/*
 * dodirs - list all directories in directory loop
 */
/*ARGSUSED*/
void
dodirs(Char **v, struct command *c)
{
    static const char flags[] = "plvnSLc";
    int dflag = skipargs(&v, flags, "");

    USE(c);
    if ((dflag & DIR_CLEAR) != 0) {
	struct directory *dp, *fdp;
	for (dp = dcwd->di_next; dp != dcwd; ) {
	    fdp = dp;
	    dp = dp->di_next;
	    if (fdp != &dhead)
		dfree(fdp);
	}
	dhead.di_next = dhead.di_prev = dp;
	dp->di_next = dp->di_prev = &dhead;
    }
    if ((dflag & DIR_LOAD) != 0) 
	loaddirs(*v);
    else if ((dflag & DIR_SAVE) != 0)
	recdirs(*v, 1);

    if (*v && (dflag & (DIR_SAVE|DIR_LOAD)))
	v++;

    if (*v != NULL || (dflag & DIR_OLD))
	stderror(ERR_DIRUS, "dirs", flags, "");
    if ((dflag & (DIR_CLEAR|DIR_LOAD|DIR_SAVE)) == 0 || (dflag & DIR_PRINT))
	printdirs(dflag);
}

static void
printdirs(int dflag)
{
    struct directory *dp;
    Char   *s, *user;
    int     idx, len, cur;

    dp = dcwd;
    idx = 0;
    cur = 0;
    do {
	if (dp == &dhead)
	    continue;
	if (dflag & DIR_VERT) {
	    xprintf("%d\t", idx++);
	    cur = 0;
	}
	s = dp->di_name;		
	user = NULL;
	if (!(dflag & DIR_LONG) && (user = getusername(&s)) != NULL)
	    len = (int) (Strlen(user) + Strlen(s) + 2);
	else
	    len = (int) (Strlen(s) + 1);

	cur += len;
	if ((dflag & DIR_LINE) && cur >= TermH - 1 && len < TermH) {
	    xputchar('\n');
	    cur = len;
	}
	if (user) 
	    xprintf("~%S", user);
	xprintf("%-S%c", s, (dflag & DIR_VERT) ? '\n' : ' ');
    } while ((dp = dp->di_prev) != dcwd);
    if (!(dflag & DIR_VERT))
	xputchar('\n');
}

void
dtildepr(Char *dir)
{
    Char* user;
    if ((user = getusername(&dir)) != NULL)
	xprintf("~%-S%S", user, dir);
    else
	xprintf("%S", dir);
}

void
dtilde(void)
{
    struct directory *d = dcwd;

    do {
	if (d == &dhead)
	    continue;
	d->di_name = dcanon(d->di_name, STRNULL);
    } while ((d = d->di_prev) != dcwd);

    dset(dcwd->di_name);
}


/* dnormalize():
 *	The path will be normalized if it
 *	1) is "..",
 *	2) or starts with "../",
 *	3) or ends with "/..",
 *	4) or contains the string "/../",
 *	then it will be normalized, unless those strings are quoted. 
 *	Otherwise, a copy is made and sent back.
 */
Char   *
dnormalize(const Char *cp, int expnd)
{

/* return true if dp is of the form "../xxx" or "/../xxx" */
#define IS_DOTDOT(sp, p) (ISDOTDOT(p) && ((p) == (sp) || *((p) - 1) == '/'))
#define IS_DOT(sp, p) (ISDOT(p) && ((p) == (sp) || *((p) - 1) == '/'))

#ifdef S_IFLNK
    if (expnd) {
	struct Strbuf buf = Strbuf_INIT;
 	int     dotdot = 0;
	Char   *dp, *cwd;
	const Char *start = cp;
# ifdef HAVE_SLASHSLASH
	int slashslash;
# endif /* HAVE_SLASHSLASH */

	/*
	 * count the number of "../xxx" or "xxx/../xxx" in the path
	 */
	for ( ; *cp && *(cp + 1); cp++)
	    if (IS_DOTDOT(start, cp))
	        dotdot++;

	/*
	 * if none, we are done.
	 */
        if (dotdot == 0)
	    return (Strsave(start));
	
# ifdef notdef
	struct stat sb;
	/*
	 * We disable this test because:
	 * cd /tmp; mkdir dir1 dir2; cd dir2; ln -s /tmp/dir1; cd dir1;
	 * echo ../../dir1 does not expand. We had enabled this before
	 * because it was bothering people with expansions in compilation
	 * lines like -I../../foo. Maybe we need some kind of finer grain
	 * control?
	 *
	 * If the path doesn't exist, we are done too.
	 */
	if (lstat(short2str(start), &sb) != 0 && errno == ENOENT)
	    return (Strsave(start));
# endif

	cwd = xmalloc((Strlen(dcwd->di_name) + 3) * sizeof(Char));
	(void) Strcpy(cwd, dcwd->di_name);

	/*
	 * If the path starts with a slash, we are not relative to
	 * the current working directory.
	 */
	if (ABSOLUTEP(start))
	    *cwd = '\0';
# ifdef HAVE_SLASHSLASH
	slashslash = cwd[0] == '/' && cwd[1] == '/';
# endif /* HAVE_SLASHSLASH */

	/*
	 * Ignore . and count ..'s
	 */
	cp = start;
	do {
	    dotdot = 0;
	    buf.len = 0;
	    while (*cp) 
	        if (IS_DOT(start, cp)) {
	            if (*++cp)
	                cp++;
	        }
	        else if (IS_DOTDOT(start, cp)) {
		    if (buf.len != 0)
		        break; /* finish analyzing .././../xxx/[..] */
		    dotdot++;
		    cp += 2;
		    if (*cp)
		        cp++;
	        }
	        else 
		    Strbuf_append1(&buf, *cp++);

	    Strbuf_terminate(&buf);
	    while (dotdot > 0) 
	        if ((dp = Strrchr(cwd, '/')) != NULL) {
# ifdef HAVE_SLASHSLASH
		    if (dp == &cwd[1]) 
		        slashslash = 1;
# endif /* HAVE_SLASHSLASH */
		        *dp = '\0';
		        dotdot--;
	        }
	        else
		    break;

	    if (!*cwd) {	/* too many ..'s, starts with "/" */
	        cwd[0] = '/';
# ifdef HAVE_SLASHSLASH
		/*
		 * Only append another slash, if already the former cwd
		 * was in a double-slash path.
		 */
		cwd[1] = slashslash ? '/' : '\0';
		cwd[2] = '\0';
# else /* !HAVE_SLASHSLASH */
		cwd[1] = '\0';
# endif /* HAVE_SLASHSLASH */
	    }
# ifdef HAVE_SLASHSLASH
	    else if (slashslash && cwd[1] == '\0') {
		cwd[1] = '/';
		cwd[2] = '\0';
	    }
# endif /* HAVE_SLASHSLASH */

	    if (buf.len != 0) {
		size_t i;

		i = Strlen(cwd);
		if (TRM(cwd[i - 1]) != '/') {
		    cwd[i++] = '/';
		    cwd[i] = '\0';
		}
	        dp = Strspl(cwd, TRM(buf.s[0]) == '/' ? &buf.s[1] : buf.s);
	        xfree(cwd);
	        cwd = dp;
		i = Strlen(cwd) - 1;
	        if (TRM(cwd[i]) == '/')
		    cwd[i] = '\0';
	    }
	    /* Reduction of ".." following the stuff we collected in buf
	     * only makes sense if the directory item in buf really exists.
	     * Avoid reduction of "-I../.." (typical compiler call) to ""
	     * or "/usr/nonexistant/../bin" to "/usr/bin":
	     */
	    if (cwd[0]) {
	        struct stat exists;
		if (0 != stat(short2str(cwd), &exists)) {
		    xfree(buf.s);
		    xfree(cwd);
		    return Strsave(start);
		}
	    }
	} while (*cp != '\0');
	xfree(buf.s);
	return cwd;
    }
#endif /* S_IFLNK */
    return Strsave(cp);
}


/*
 * dochngd - implement chdir command.
 */
/*ARGSUSED*/
void
dochngd(Char **v, struct command *c)
{
    Char *cp;
    struct directory *dp;
    int dflag = skipargs(&v, "plvn", "[-|<dir>]");

    USE(c);
    printd = 0;
    cp = (dflag & DIR_OLD) ? varval(STRowd) : *v;

    if (cp == NULL) {
	if (!cdtohome)
	    stderror(ERR_NAME | ERR_TOOFEW);
	else if ((cp = varval(STRhome)) == STRNULL || *cp == 0)
	    stderror(ERR_NAME | ERR_NOHOMEDIR);
	if (chdir(short2str(cp)) < 0)
	    stderror(ERR_NAME | ERR_CANTCHANGE);
	cp = Strsave(cp);
    }
    else if ((dflag & DIR_OLD) == 0 && v[1] != NULL) {
	stderror(ERR_NAME | ERR_TOOMANY);
	/* NOTREACHED */
	return;
    }
    else if ((dp = dfind(cp)) != 0) {
	char   *tmp;

	printd = 1;
	if (chdir(tmp = short2str(dp->di_name)) < 0)
	    stderror(ERR_SYSTEM, tmp, strerror(errno));
	dcwd->di_prev->di_next = dcwd->di_next;
	dcwd->di_next->di_prev = dcwd->di_prev;
	dfree(dcwd);
	dnewcwd(dp, dflag);
	return;
    }
    else
	if ((cp = dfollow(cp, dflag & DIR_OLD)) == NULL)
	    return;
    dp = xcalloc(sizeof(struct directory), 1);
    dp->di_name = cp;
    dp->di_count = 0;
    dp->di_next = dcwd->di_next;
    dp->di_prev = dcwd->di_prev;
    dp->di_prev->di_next = dp;
    dp->di_next->di_prev = dp;
    dfree(dcwd);
    dnewcwd(dp, dflag);
}

static Char *
dgoto(Char *cp)
{
    Char *dp, *ret;

    if (!ABSOLUTEP(cp))
    {
	Char *p, *q;
	size_t cwdlen;

	cwdlen = Strlen(dcwd->di_name);
	if (cwdlen == 1)	/* root */
	    cwdlen = 0;
	dp = xmalloc((cwdlen + Strlen(cp) + 2) * sizeof(Char));
	for (p = dp, q = dcwd->di_name; (*p++ = *q++) != '\0';)
	    continue;
	if (cwdlen)
	    p[-1] = '/';
	else
	    p--;		/* don't add a / after root */
	Strcpy(p, cp);
	xfree(cp);
	cp = dp;
	dp += cwdlen;
    }
    else
	dp = cp;

#if defined(WINNT_NATIVE)
    return agetcwd();
#elif defined(__CYGWIN__)
    if (ABSOLUTEP(cp) && cp[1] == ':') { /* Only DOS paths are treated that way */
	return agetcwd();
    } else {
	cleanup_push(cp, xfree);
    	ret = dcanon(cp, dp);
	cleanup_ignore(cp);
	cleanup_until(cp);
    }
#else /* !WINNT_NATIVE */
    cleanup_push(cp, xfree);
    ret = dcanon(cp, dp);
    cleanup_ignore(cp);
    cleanup_until(cp);
#endif /* WINNT_NATIVE */
    return ret;
}

/*
 * dfollow - change to arg directory; fall back on cdpath if not valid
 */
static Char *
dfollow(Char *cp, int old)
{
    Char *dp;
    struct varent *c;
    int serrno;

    cp = old ? Strsave(cp) : globone(cp, G_ERROR);
    cleanup_push(cp, xfree);
#ifdef apollo
    if (Strchr(cp, '`')) {
	char *dptr;
	if (chdir(dptr = short2str(cp)) < 0) 
	    stderror(ERR_SYSTEM, dptr, strerror(errno));
	dp = agetcwd();
	cleanup_push(dp, xfree);
	if (dp != NULL) {
	    cleanup_until(cp);
	    return dgoto(dp);
	}
	else
	    stderror(ERR_SYSTEM, dptr, strerror(errno));
    }
#endif /* apollo */

    /*
     * if we are ignoring symlinks, try to fix relatives now.
     * if we are expading symlinks, it should be done by now.
     */ 
    dp = dnormalize(cp, symlinks == SYM_IGNORE);
    if (chdir(short2str(dp)) >= 0) {
        cleanup_until(cp);
        return dgoto(dp);
    }
    else {
        xfree(dp);
        if (chdir(short2str(cp)) >= 0) {
	    cleanup_ignore(cp);
	    cleanup_until(cp);
	    return dgoto(cp);
	}
	else if (errno != ENOENT && errno != ENOTDIR) {
	    int err;

	    err = errno;
	    stderror(ERR_SYSTEM, short2str(cp), strerror(err));
	}
	serrno = errno;
    }

    if (cp[0] != '/' && !prefix(STRdotsl, cp) && !prefix(STRdotdotsl, cp)
	&& (c = adrof(STRcdpath)) && c->vec != NULL) {
	struct Strbuf buf = Strbuf_INIT;
	Char  **cdp;

	for (cdp = c->vec; *cdp; cdp++) {
	    size_t len = Strlen(*cdp);
	    buf.len = 0;
	    if (len > 0) {
		Strbuf_append(&buf, *cdp);
		if ((*cdp)[len - 1] != '/')
		    Strbuf_append1(&buf, '/');
	    }
	    Strbuf_append(&buf, cp);
	    Strbuf_terminate(&buf);
	    /*
	     * We always want to fix the directory here
	     * If we are normalizing symlinks
	     */
	    dp = dnormalize(buf.s, symlinks == SYM_IGNORE || 
				   symlinks == SYM_EXPAND);
	    if (chdir(short2str(dp)) >= 0) {
		printd = 1;
		xfree(buf.s);
		cleanup_until(cp);
		return dgoto(dp);
	    }
	    else if (chdir(short2str(cp)) >= 0) {
		printd = 1;
		xfree(dp);
		xfree(buf.s);
		cleanup_ignore(cp);
		cleanup_until(cp);
		return dgoto(cp);
	    }
	    xfree(dp);
	}
	xfree(buf.s);
    }
    dp = varval(cp);
    if ((dp[0] == '/' || dp[0] == '.') && chdir(short2str(dp)) >= 0) {
	cleanup_until(cp);
	cp = Strsave(dp);
	printd = 1;
	return dgoto(cp);
    }
    /*
     * on login source of ~/.cshdirs, errors are eaten. the dir stack is all
     * directories we could get to.
     */
    if (!bequiet)
	stderror(ERR_SYSTEM, short2str(cp), strerror(serrno));
    cleanup_until(cp);
    return (NULL);
}


/*
 * dopushd - push new directory onto directory stack.
 *	with no arguments exchange top and second.
 *	with numeric argument (+n) bring it to top.
 */
/*ARGSUSED*/
void
dopushd(Char **v, struct command *c)
{
    struct directory *dp;
    Char *cp;
    int dflag = skipargs(&v, "plvn", " [-|<dir>|+<n>]");
    
    USE(c);
    printd = 1;
    cp = (dflag & DIR_OLD) ? varval(STRowd) : *v;

    if (cp == NULL) {
	if (adrof(STRpushdtohome)) {
	    if ((cp = varval(STRhome)) == STRNULL || *cp == 0)
		stderror(ERR_NAME | ERR_NOHOMEDIR);
	    if (chdir(short2str(cp)) < 0)
		stderror(ERR_NAME | ERR_CANTCHANGE);
	    if ((cp = dfollow(cp, dflag & DIR_OLD)) == NULL)
		return;
	    dp = xcalloc(sizeof(struct directory), 1);
	    dp->di_name = cp;
	    dp->di_count = 0;
	    dp->di_prev = dcwd;
	    dp->di_next = dcwd->di_next;
	    dcwd->di_next = dp;
	    dp->di_next->di_prev = dp;
	}
	else {
	    char   *tmp;

	    if ((dp = dcwd->di_prev) == &dhead)
		dp = dhead.di_prev;
	    if (dp == dcwd)
		stderror(ERR_NAME | ERR_NODIR);
	    if (chdir(tmp = short2str(dp->di_name)) < 0)
		stderror(ERR_SYSTEM, tmp, strerror(errno));
	    dp->di_prev->di_next = dp->di_next;
	    dp->di_next->di_prev = dp->di_prev;
	    dp->di_next = dcwd->di_next;
	    dp->di_prev = dcwd;
	    dcwd->di_next->di_prev = dp;
	    dcwd->di_next = dp;
	}
    }
    else if ((dflag & DIR_OLD) == 0 && v[1] != NULL) {
	stderror(ERR_NAME | ERR_TOOMANY);
	/* NOTREACHED */
	return;
    }
    else if ((dp = dfind(cp)) != NULL) {
	char   *tmp;

	if (chdir(tmp = short2str(dp->di_name)) < 0)
	    stderror(ERR_SYSTEM, tmp, strerror(errno));
	/*
	 * kfk - 10 Feb 1984 - added new "extraction style" pushd +n
	 */
	if (adrof(STRdextract))
	    dextract(dp);
    }
    else {
	Char *ccp;

	if ((ccp = dfollow(cp, dflag & DIR_OLD)) == NULL)
	    return;
	dp = xcalloc(sizeof(struct directory), 1);
	dp->di_name = ccp;
	dp->di_count = 0;
	dp->di_prev = dcwd;
	dp->di_next = dcwd->di_next;
	dcwd->di_next = dp;
	dp->di_next->di_prev = dp;
    }
    dnewcwd(dp, dflag);
}

/*
 * dfind - find a directory if specified by numeric (+n) argument
 */
static struct directory *
dfind(Char *cp)
{
    struct directory *dp;
    int i;
    Char *ep;

    if (*cp++ != '+')
	return (0);
    for (ep = cp; Isdigit(*ep); ep++)
	continue;
    if (*ep)
	return (0);
    i = getn(cp);
    if (i <= 0)
	return (0);
    for (dp = dcwd; i != 0; i--) {
	if ((dp = dp->di_prev) == &dhead)
	    dp = dp->di_prev;
	if (dp == dcwd)
	    stderror(ERR_NAME | ERR_DEEP);
    }
    return (dp);
}

/*
 * dopopd - pop a directory out of the directory stack
 *	with a numeric argument just discard it.
 */
/*ARGSUSED*/
void
dopopd(Char **v, struct command *c)
{
    Char *cp;
    struct directory *dp, *p = NULL;
    int dflag = skipargs(&v, "plvn", " [-|+<n>]");

    USE(c);
    printd = 1;
    cp = (dflag & DIR_OLD) ? varval(STRowd) : *v;

    if (cp == NULL)
	dp = dcwd;
    else if ((dflag & DIR_OLD) == 0 && v[1] != NULL) {
	stderror(ERR_NAME | ERR_TOOMANY);
	/* NOTREACHED */
	return;
    }
    else if ((dp = dfind(cp)) == 0)
	stderror(ERR_NAME | ERR_BADDIR);
    if (dp->di_prev == &dhead && dp->di_next == &dhead)
	stderror(ERR_NAME | ERR_EMPTY);
    if (dp == dcwd) {
	char   *tmp;

	if ((p = dp->di_prev) == &dhead)
	    p = dhead.di_prev;
	if (chdir(tmp = short2str(p->di_name)) < 0)
	    stderror(ERR_SYSTEM, tmp, strerror(errno));
    }
    dp->di_prev->di_next = dp->di_next;
    dp->di_next->di_prev = dp->di_prev;
    dfree(dp);
    if (dp == dcwd) {
        dnewcwd(p, dflag);
    }
    else {
	printdirs(dflag);
    }
}

/*
 * dfree - free the directory (or keep it if it still has ref count)
 */
void
dfree(struct directory *dp)
{

    if (dp->di_count != 0) {
	dp->di_next = dp->di_prev = 0;
    }
    else {
	xfree(dp->di_name);
	xfree(dp);
    }
}

/*
 * dcanon - canonicalize the pathname, removing excess ./ and ../ etc.
 *	we are of course assuming that the file system is standardly
 *	constructed (always have ..'s, directories have links)
 */
Char   *
dcanon(Char *cp, Char *p)
{
    Char *sp;
    Char *p1, *p2;	/* general purpose */
    int    slash;
#ifdef HAVE_SLASHSLASH
    int    slashslash;
#endif /* HAVE_SLASHSLASH */
    size_t  clen;

#ifdef S_IFLNK			/* if we have symlinks */
    Char *mlink, *newcp;
    char *tlink;
    size_t cc;
#endif /* S_IFLNK */

    clen = Strlen(cp);

    /*
     * christos: if the path given does not start with a slash prepend cwd. If
     * cwd does not start with a slash or the result would be too long try to
     * correct it.
     */
    if (!ABSOLUTEP(cp)) {
	Char *tmpdir;
	size_t	len;

	p1 = varval(STRcwd);
	if (p1 == STRNULL || !ABSOLUTEP(p1)) {
	    Char *new_cwd = agetcwd();

	    if (new_cwd == NULL) {
		xprintf("%s: %s\n", progname, strerror(errno));
		setcopy(STRcwd, str2short("/"), VAR_READWRITE|VAR_NOGLOB);
	    }
	    else
		setv(STRcwd, new_cwd, VAR_READWRITE|VAR_NOGLOB);
	    p1 = varval(STRcwd);
	}
	len = Strlen(p1);
	tmpdir = xmalloc((len + clen + 2) * sizeof (*tmpdir));
	(void) Strcpy(tmpdir, p1);
	(void) Strcat(tmpdir, STRslash);
	(void) Strcat(tmpdir, cp);
	xfree(cp);
	cp = p = tmpdir;
    }

#ifdef HAVE_SLASHSLASH
    slashslash = (cp[0] == '/' && cp[1] == '/');
#endif /* HAVE_SLASHSLASH */

    while (*p) {		/* for each component */
	sp = p;			/* save slash address */
	while (*++p == '/')	/* flush extra slashes */
	    continue;
	if (p != ++sp)
	    for (p1 = sp, p2 = p; (*p1++ = *p2++) != '\0';)
		continue;
	p = sp;			/* save start of component */
	slash = 0;
	if (*p) 
	    while (*++p)	/* find next slash or end of path */
		if (*p == '/') {
		    slash = 1;
		    *p = 0;
		    break;
		}

#ifdef HAVE_SLASHSLASH
	if (&cp[1] == sp && sp[0] == '.' && sp[1] == '.' && sp[2] == '\0')
	    slashslash = 1;
#endif /* HAVE_SLASHSLASH */
	if (*sp == '\0') {	/* if component is null */
	    if (--sp == cp)	/* if path is one char (i.e. /) */ 
		break;
	    else
		*sp = '\0';
	}
	else if (sp[0] == '.' && sp[1] == 0) {
	    if (slash) {
		for (p1 = sp, p2 = p + 1; (*p1++ = *p2++) != '\0';)
		    continue;
		p = --sp;
	    }
	    else if (--sp != cp)
		*sp = '\0';
	    else
		sp[1] = '\0';
	}
	else if (sp[0] == '.' && sp[1] == '.' && sp[2] == 0) {
	    /*
	     * We have something like "yyy/xxx/..", where "yyy" can be null or
	     * a path starting at /, and "xxx" is a single component. Before
	     * compressing "xxx/..", we want to expand "yyy/xxx", if it is a
	     * symbolic link.
	     */
	    *--sp = 0;		/* form the pathname for readlink */
#ifdef S_IFLNK			/* if we have symlinks */
	    if (sp != cp && /* symlinks != SYM_IGNORE && */
		(tlink = areadlink(short2str(cp))) != NULL) {
		mlink = str2short(tlink);
		xfree(tlink);

		if (slash)
		    *p = '/';
		/*
		 * Point p to the '/' in "/..", and restore the '/'.
		 */
		*(p = sp) = '/';
		if (*mlink != '/') {
		    /*
		     * Relative path, expand it between the "yyy/" and the
		     * "/..". First, back sp up to the character past "yyy/".
		     */
		    while (*--sp != '/')
			continue;
		    sp++;
		    *sp = 0;
		    /*
		     * New length is "yyy/" + mlink + "/.." and rest
		     */
		    p1 = newcp = xmalloc(((sp - cp) + Strlen(mlink) +
					  Strlen(p) + 1) * sizeof(Char));
		    /*
		     * Copy new path into newcp
		     */
		    for (p2 = cp; (*p1++ = *p2++) != '\0';)
			continue;
		    for (p1--, p2 = mlink; (*p1++ = *p2++) != '\0';)
			continue;
		    for (p1--, p2 = p; (*p1++ = *p2++) != '\0';)
			continue;
		    /*
		     * Restart canonicalization at expanded "/xxx".
		     */
		    p = sp - cp - 1 + newcp;
		}
		else {
		    newcp = Strspl(mlink, p);
		    /*
		     * Restart canonicalization at beginning
		     */
		    p = newcp;
		}
		xfree(cp);
		cp = newcp;
#ifdef HAVE_SLASHSLASH
                slashslash = (cp[0] == '/' && cp[1] == '/');
#endif /* HAVE_SLASHSLASH */
		continue;	/* canonicalize the link */
	    }
#endif /* S_IFLNK */
	    *sp = '/';
	    if (sp != cp)
		while (*--sp != '/')
		    continue;
	    if (slash) {
		for (p1 = sp + 1, p2 = p + 1; (*p1++ = *p2++) != '\0';)
		    continue;
		p = sp;
	    }
	    else if (cp == sp)
		*++sp = '\0';
	    else
		*sp = '\0';
	}
	else {			/* normal dir name (not . or .. or nothing) */

#ifdef S_IFLNK			/* if we have symlinks */
	    if (sp != cp && symlinks == SYM_CHASE &&
		(tlink = areadlink(short2str(cp))) != NULL) {
		mlink = str2short(tlink);
		xfree(tlink);

		/*
		 * restore the '/'.
		 */
		if (slash)
		    *p = '/';

		/*
		 * point sp to p (rather than backing up).
		 */
		sp = p;

		if (*mlink != '/') {
		    /*
		     * Relative path, expand it between the "yyy/" and the
		     * remainder. First, back sp up to the character past
		     * "yyy/".
		     */
		    while (*--sp != '/')
			continue;
		    sp++;
		    *sp = 0;
		    /*
		     * New length is "yyy/" + mlink + "/.." and rest
		     */
		    p1 = newcp = xmalloc(((sp - cp) + Strlen(mlink) +
					  Strlen(p) + 1) * sizeof(Char));
		    /*
		     * Copy new path into newcp
		     */
		    for (p2 = cp; (*p1++ = *p2++) != '\0';)
			continue;
		    for (p1--, p2 = mlink; (*p1++ = *p2++) != '\0';)
			continue;
		    for (p1--, p2 = p; (*p1++ = *p2++) != '\0';)
			continue;
		    /*
		     * Restart canonicalization at expanded "/xxx".
		     */
		    p = sp - cp - 1 + newcp;
		}
		else {
		    newcp = Strspl(mlink, p);
		    /*
		     * Restart canonicalization at beginning
		     */
		    p = newcp;
		}
		xfree(cp);
		cp = newcp;
#ifdef HAVE_SLASHSLASH
                slashslash = (cp[0] == '/' && cp[1] == '/');
#endif /* HAVE_SLASHSLASH */
		continue;	/* canonicalize the mlink */
	    }
#endif /* S_IFLNK */
	    if (slash)
		*p = '/';
	}
    }

    /*
     * fix home...
     */
#ifdef S_IFLNK
    p1 = varval(STRhome);
    cc = Strlen(p1);
    /*
     * See if we're not in a subdir of STRhome
     */
    if (p1 && *p1 == '/' && (Strncmp(p1, cp, cc) != 0 ||
	(cp[cc] != '/' && cp[cc] != '\0'))) {
	static ino_t home_ino = (ino_t) -1;
	static dev_t home_dev = (dev_t) -1;
	static Char *home_ptr = NULL;
	struct stat statbuf;
	int found;
	Char *copy;

	/*
	 * Get dev and ino of STRhome
	 */
	if (home_ptr != p1 &&
	    stat(short2str(p1), &statbuf) != -1) {
	    home_dev = statbuf.st_dev;
	    home_ino = statbuf.st_ino;
	    home_ptr = p1;
	}
	/*
	 * Start comparing dev & ino backwards
	 */
	p2 = copy = Strsave(cp);
	found = 0;
	while (*p2 && stat(short2str(p2), &statbuf) != -1) {
	    if (DEV_DEV_COMPARE(statbuf.st_dev, home_dev) &&
			statbuf.st_ino == home_ino) {
			found = 1;
			break;
	    }
	    if ((sp = Strrchr(p2, '/')) != NULL)
		*sp = '\0';
	}
	/*
	 * See if we found it
	 */
	if (*p2 && found) {
	    /*
	     * Use STRhome to make '~' work
	     */
	    newcp = Strspl(p1, cp + Strlen(p2));
	    xfree(cp);
	    cp = newcp;
	}
	xfree(copy);
    }
#endif /* S_IFLNK */

#ifdef HAVE_SLASHSLASH
    if (slashslash) {
	if (cp[1] != '/') {
	    p = xmalloc((Strlen(cp) + 2) * sizeof(Char));
	    *p = '/';
	    (void) Strcpy(&p[1], cp);
	    xfree(cp);
	    cp = p;
	}
    }
    if (cp[1] == '/' && cp[2] == '/') {
	for (p1 = &cp[1], p2 = &cp[2]; (*p1++ = *p2++) != '\0';)
	    continue;
    }
#endif /* HAVE_SLASHSLASH */
    return cp;
}


/*
 * dnewcwd - make a new directory in the loop the current one
 */
static void
dnewcwd(struct directory *dp, int dflag)
{
    int print;

    if (adrof(STRdunique)) {
	struct directory *dn;

	for (dn = dhead.di_prev; dn != &dhead; dn = dn->di_prev) 
	    if (dn != dp && Strcmp(dn->di_name, dp->di_name) == 0) {
		dn->di_next->di_prev = dn->di_prev;
		dn->di_prev->di_next = dn->di_next;
		dfree(dn);
		break;
	    }
    }
    dcwd = dp;
    dset(dcwd->di_name);
    dgetstack();
    print = printd;		/* if printd is set, print dirstack... */
    if (adrof(STRpushdsilent))	/* but pushdsilent overrides printd... */
	print = 0;
    if (dflag & DIR_PRINT)	/* but DIR_PRINT overrides pushdsilent... */
	print = 1;
    if (bequiet)		/* and bequiet overrides everything */
	print = 0;
    if (print)
	printdirs(dflag);
    cwd_cmd();			/* PWP: run the defined cwd command */
}

void
dsetstack(void)
{
    Char **cp;
    struct varent *vp;
    struct directory *dn, *dp;

    if ((vp = adrof(STRdirstack)) == NULL || vp->vec == NULL)
	return;

    /* Free the whole stack */
    while ((dn = dhead.di_prev) != &dhead) {
	dn->di_next->di_prev = dn->di_prev;
	dn->di_prev->di_next = dn->di_next;
	if (dn != dcwd)
	    dfree(dn);
    }

    /* thread the current working directory */
    dhead.di_prev = dhead.di_next = dcwd;
    dcwd->di_next = dcwd->di_prev = &dhead;

    /* put back the stack */
    for (cp = vp->vec; cp && *cp && **cp; cp++) {
	dp = xcalloc(sizeof(struct directory), 1);
	dp->di_name = Strsave(*cp);
	dp->di_count = 0;
	dp->di_prev = dcwd;
	dp->di_next = dcwd->di_next;
	dcwd->di_next = dp;
	dp->di_next->di_prev = dp;
    }
    dgetstack();	/* Make $dirstack reflect the current state */
}

static void
dgetstack(void)
{
    int i = 0;
    Char **dblk, **dbp;
    struct directory *dn;

    if (adrof(STRdirstack) == NULL) 
    	return;

    for (dn = dhead.di_prev; dn != &dhead; dn = dn->di_prev, i++)
	continue;
    dbp = dblk = xmalloc((i + 1) * sizeof(Char *));
    for (dn = dhead.di_prev; dn != &dhead; dn = dn->di_prev, dbp++)
	 *dbp = Strsave(dn->di_name);
    *dbp = NULL;
    cleanup_push(dblk, blk_cleanup);
    setq(STRdirstack, dblk, &shvhed, VAR_READWRITE);
    cleanup_ignore(dblk);
    cleanup_until(dblk);
}

/*
 * getstakd - added by kfk 17 Jan 1984
 * Support routine for the stack hack.  Finds nth directory in
 * the directory stack, or finds last directory in stack.
 */
const Char *
getstakd(int cnt)
{
    struct directory *dp;

    dp = dcwd;
    if (cnt < 0) {		/* < 0 ==> last dir requested. */
	dp = dp->di_next;
	if (dp == &dhead)
	    dp = dp->di_next;
    }
    else {
	while (cnt-- > 0) {
	    dp = dp->di_prev;
	    if (dp == &dhead)
		dp = dp->di_prev;
	    if (dp == dcwd)
		return NULL;
	}
    }
    return dp->di_name;
}

/*
 * Karl Kleinpaste - 10 Feb 1984
 * Added dextract(), which is used in pushd +n.
 * Instead of just rotating the entire stack around, dextract()
 * lets the user have the nth dir extracted from its current
 * position, and pushes it onto the top.
 */
static void
dextract(struct directory *dp)
{
    if (dp == dcwd)
	return;
    dp->di_next->di_prev = dp->di_prev;
    dp->di_prev->di_next = dp->di_next;
    dp->di_next = dcwd->di_next;
    dp->di_prev = dcwd;
    dp->di_next->di_prev = dp;
    dcwd->di_next = dp;
}

static void
bequiet_cleanup(void *dummy)
{
    USE(dummy);
    bequiet = 0;
}

void
loaddirs(Char *fname)
{
    static Char *loaddirs_cmd[] = { STRsource, NULL, NULL };

    bequiet = 1;
    cleanup_push(&bequiet, bequiet_cleanup);
    if (fname)
	loaddirs_cmd[1] = fname;
    else if ((fname = varval(STRdirsfile)) != STRNULL)
	loaddirs_cmd[1] = fname;
    else
	loaddirs_cmd[1] = STRtildotdirs;
    dosource(loaddirs_cmd, NULL);
    cleanup_until(&bequiet);
}

/*
 * create a file called ~/.cshdirs which has a sequence
 * of pushd commands which will restore the dir stack to
 * its state before exit/logout. remember that the order
 * is reversed in the file because we are pushing.
 * -strike
 */
void
recdirs(Char *fname, int def)
{
    int     fp, ftmp, oldidfds;
    int     cdflag = 0;
    struct directory *dp;
    unsigned int    num;
    Char   *snum;
    struct Strbuf qname = Strbuf_INIT;

    if (fname == NULL && !def) 
	return;

    if (fname == NULL) {
	if ((fname = varval(STRdirsfile)) == STRNULL)
	    fname = Strspl(varval(STRhome), &STRtildotdirs[1]);
	else
	    fname = Strsave(fname);
    }
    else 
	fname = globone(fname, G_ERROR);
    cleanup_push(fname, xfree);

    if ((fp = xcreat(short2str(fname), 0600)) == -1) {
	cleanup_until(fname);
	return;
    }

    if ((snum = varval(STRsavedirs)) == STRNULL || snum[0] == '\0') 
	num = (unsigned int) ~0;
    else
	num = (unsigned int) atoi(short2str(snum));

    oldidfds = didfds;
    didfds = 0;
    ftmp = SHOUT;
    SHOUT = fp;

    cleanup_push(&qname, Strbuf_cleanup);
    dp = dcwd->di_next;
    do {
	if (dp == &dhead)
	    continue;

	if (cdflag == 0) {
	    cdflag = 1;
	    xprintf("cd %S\n", quote_meta(&qname, dp->di_name));
	}
	else
	    xprintf("pushd %S\n", quote_meta(&qname, dp->di_name));

	if (num-- == 0)
	    break;

    } while ((dp = dp->di_next) != dcwd->di_next);

    xclose(fp);
    SHOUT = ftmp;
    didfds = oldidfds;
    cleanup_until(fname);
}
