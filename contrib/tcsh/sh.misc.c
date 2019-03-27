/* $Header: /p/tcsh/cvsroot/tcsh/sh.misc.c,v 3.50 2015/06/06 21:19:08 christos Exp $ */
/*
 * sh.misc.c: Miscelaneous functions
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

RCSID("$tcsh: sh.misc.c,v 3.50 2015/06/06 21:19:08 christos Exp $")

static	int	renum	(int, int);
static  Char  **blkend	(Char **);
static  Char  **blkcat	(Char **, Char **);
static	int	xdup2	(int, int);

/*
 * C Shell
 */

int
any(const char *s, Char c)
{
    if (!s)
	return (0);		/* Check for nil pointer */
    while (*s)
	if ((Char)*s++ == c)
	    return (1);
    return (0);
}

void
setzero(void *p, size_t size)
{
    memset(p, 0, size);
}

#ifndef SHORT_STRINGS
char *
strnsave(const char *s, size_t len)
{
    char *r;

    r = xmalloc(len + 1);
    memcpy(r, s, len);
    r[len] = '\0';
    return r;
}
#endif

char   *
strsave(const char *s)
{
    char   *r;
    size_t size;

    if (s == NULL)
	s = "";
    size = strlen(s) + 1;
    r = xmalloc(size);
    memcpy(r, s, size);
    return (r);
}

static Char  **
blkend(Char **up)
{

    while (*up)
	up++;
    return (up);
}


void
blkpr(Char *const *av)
{

    for (; *av; av++) {
	xprintf("%S", *av);
	if (av[1])
	    xprintf(" ");
    }
}

Char *
blkexpand(Char *const *av)
{
    struct Strbuf buf = Strbuf_INIT;

    for (; *av; av++) {
	Strbuf_append(&buf, *av);
	if (av[1])
	    Strbuf_append1(&buf, ' ');
    }
    return Strbuf_finish(&buf);
}

int
blklen(Char **av)
{
    int i = 0;

    while (*av++)
	i++;
    return (i);
}

Char  **
blkcpy(Char **oav, Char **bv)
{
    Char **av = oav;

    while ((*av++ = *bv++) != NULL)
	continue;
    return (oav);
}

static Char  **
blkcat(Char **up, Char **vp)
{

    (void) blkcpy(blkend(up), vp);
    return (up);
}

void
blkfree(Char **av0)
{
    Char **av = av0;

    if (!av0)
	return;
    for (; *av; av++)
	xfree(*av);
    xfree(av0);
}

void
blk_cleanup(void *ptr)
{
    blkfree(ptr);
}

void
blk_indirect_cleanup(void *xptr)
{
    Char ***ptr;

    ptr = xptr;
    blkfree(*ptr);
    xfree(ptr);
}

Char  **
saveblk(Char **v)
{
    Char **newv, **onewv;

    if (v == NULL)
	return NULL;

    onewv = newv = xcalloc(blklen(v) + 1, sizeof(Char **));

    while (*v)
	*newv++ = Strsave(*v++);
    return (onewv);
}

#ifndef HAVE_STRSTR
char   *
strstr(const char *s, const char *t)
{
    do {
	const char *ss = s;
	const char *tt = t;

	do
	    if (*tt == '\0')
		return (s);
	while (*ss++ == *tt++);
    } while (*s++ != '\0');
    return (NULL);
}
#endif /* !HAVE_STRSTR */

char   *
strspl(const char *cp, const char *dp)
{
    char   *ep;
    size_t cl, dl;

    if (!cp)
	cp = "";
    if (!dp)
	dp = "";
    cl = strlen(cp);
    dl = strlen(dp);
    ep = xmalloc((cl + dl + 1) * sizeof(char));
    memcpy(ep, cp, cl);
    memcpy(ep + cl, dp, dl + 1);
    return (ep);
}

Char  **
blkspl(Char **up, Char **vp)
{
    Char **wp = xcalloc(blklen(up) + blklen(vp) + 1, sizeof(Char **));

    (void) blkcpy(wp, up);
    return (blkcat(wp, vp));
}

Char
lastchr(Char *cp)
{

    if (!cp)
	return (0);
    if (!*cp)
	return (0);
    while (cp[1])
	cp++;
    return (*cp);
}

/*
 * This routine is called after an error to close up
 * any units which may have been left open accidentally.
 */
void
closem(void)
{
    int f, num_files;

#ifdef NLS_BUGS
#ifdef NLS_CATALOGS
    nlsclose();
#endif /* NLS_CATALOGS */
#endif /* NLS_BUGS */
#ifdef YPBUGS
    /* suggested by Justin Bur; thanks to Karl Kleinpaste */
    fix_yp_bugs();
#endif /* YPBUGS */
    num_files = NOFILE;
    for (f = 0; f < num_files; f++)
	if (f != SHIN && f != SHOUT && f != SHDIAG && f != OLDSTD &&
	    f != FSHTTY 
#ifdef MALLOC_TRACE
	    && f != 25
#endif /* MALLOC_TRACE */
	    )
	  {
	    xclose(f);
#ifdef NISPLUS
	    if(f < 3)
		(void) xopen(_PATH_DEVNULL, O_RDONLY|O_LARGEFILE);
#endif /* NISPLUS */
	  }
#ifdef NLS_BUGS
#ifdef NLS_CATALOGS
    nlsinit();
#endif /* NLS_CATALOGS */
#endif /* NLS_BUGS */
}

#ifndef CLOSE_ON_EXEC
/*
 * Close files before executing a file.
 * We could be MUCH more intelligent, since (on a version 7 system)
 * we need only close files here during a source, the other
 * shell fd's being in units 16-19 which are closed automatically!
 */
void
closech(void)
{
    int f, num_files;

    if (didcch)
	return;
    didcch = 1;
    SHIN = 0;
    SHOUT = 1;
    SHDIAG = 2;
    OLDSTD = 0;
    isoutatty = isatty(SHOUT);
    isdiagatty = isatty(SHDIAG);
    num_files = NOFILE;
    for (f = 3; f < num_files; f++)
	xclose(f);
}

#endif /* CLOSE_ON_EXEC */

void
donefds(void)
{

    xclose(0);
    xclose(1);
    xclose(2);
    didfds = 0;
#ifdef NISPLUS
    {
	int fd = xopen(_PATH_DEVNULL, O_RDONLY|O_LARGEFILE);
	(void)dcopy(fd, 1);
	(void)dcopy(fd, 2);
	(void)dmove(fd, 0);
    }
#endif /*NISPLUS*/    
}

/*
 * Move descriptor i to j.
 * If j is -1 then we just want to get i to a safe place,
 * i.e. to a unit > FSAFE.  This also happens in dcopy.
 */
int
dmove(int i, int j)
{

    if (i == j || i < 0)
	return (i);
#ifdef HAVE_DUP2
    if (j >= 0) {
	(void) xdup2(i, j);
	if (j != i)
	    xclose(i);
	return (j);
    }
#endif
    j = dcopy(i, j);
    if (j != i)
	xclose(i);
    return (j);
}

int
dcopy(int i, int j)
{

    if (i == j || i < 0 || (j < 0 && i > FSAFE))
	return (i);
    if (j >= 0) {
#ifdef HAVE_DUP2
	(void) xdup2(i, j);
	return (j);
#else
	xclose(j);
#endif
    }
    return (renum(i, j));
}

static int
renum(int i, int j)
{
    int k = dup(i);

    if (k < 0)
	return (-1);
    if (j == -1 && k > FSAFE)
	return (k);
    if (k != j) {
	j = renum(k, j);
	xclose(k);
	return (j);
    }
    return (k);
}

/*
 * Left shift a command argument list, discarding
 * the first c arguments.  Used in "shift" commands
 * as well as by commands like "repeat".
 */
void
lshift(Char **v, int c)
{
    Char **u;

    for (u = v; *u && --c >= 0; u++)
	xfree(*u);
    (void) blkcpy(v, u);
}

int
number(Char *cp)
{
    if (!cp)
	return (0);
    if (*cp == '-') {
	cp++;
	if (!Isdigit(*cp))
	    return (0);
	cp++;
    }
    while (*cp && Isdigit(*cp))
	cp++;
    return (*cp == 0);
}

Char  **
copyblk(Char **v)
{
    Char **nv = xcalloc(blklen(v) + 1, sizeof(Char **));

    return (blkcpy(nv, v));
}

char   *
strend(const char *cp)
{
    if (!cp)
	return ((char *)(intptr_t)cp);
    while (*cp)
	cp++;
    return ((char *)(intptr_t)cp);
}

Char   *
strip(Char *cp)
{
    Char *dp = cp;

    if (!cp)
	return (cp);
    while (*dp != '\0') {
#if INVALID_BYTE != 0
	if ((*dp & INVALID_BYTE) != INVALID_BYTE)    /* *dp < INVALID_BYTE */
#endif
		*dp &= TRIM;
	dp++;
    }
    return (cp);
}

Char   *
quote(Char *cp)
{
    Char *dp = cp;

    if (!cp)
	return (cp);
    while (*dp != '\0') {
#ifdef WIDE_STRINGS
	if ((*dp & 0xffffff80) == 0)	/* *dp < 0x80 */
#elif defined SHORT_STRINGS
	if ((*dp & 0xff80) == 0)	/* *dp < 0x80 */
#else
	if ((*dp & 0x80) == 0)		/* *dp < 0x80 */
#endif
	    *dp |= QUOTE;
	dp++;
    }
    return (cp);
}

const Char *
quote_meta(struct Strbuf *buf, const Char *s)
{
    buf->len = 0;
    while (*s != '\0') {
	if (cmap(*s, _META | _DOL | _QF | _QB | _ESC | _GLOB))
	    Strbuf_append1(buf, '\\');
	Strbuf_append1(buf, *s++);
    }
    Strbuf_terminate(buf);
    return buf->s;
}

void
udvar(Char *name)
{
    setname(short2str(name));
    stderror(ERR_NAME | ERR_UNDVAR);
}

int
prefix(const Char *sub, const Char *str)
{

    for (;;) {
	if (*sub == 0)
	    return (1);
	if (*str == 0)
	    return (0);
	if ((*sub++ & TRIM) != (*str++ & TRIM))
	    return (0);
    }
}
#ifndef WINNT_NATIVE
char *
areadlink(const char *path)
{
    char *buf;
    size_t size;
    ssize_t res;

    size = MAXPATHLEN + 1;
    buf = xmalloc(size);
    while ((size_t)(res = readlink(path, buf, size)) == size) {
	size *= 2;
	buf = xrealloc(buf, size);
    }
    if (res == -1) {
	int err;

	err = errno;
	xfree(buf);
	errno = err;
	return NULL;
    }
    buf[res] = '\0';
    return xrealloc(buf, res + 1);
}
#endif /*!WINNT_NATIVE*/

void
xclose(int fildes)
{
    if (fildes < 0)
	return;
    while (close(fildes) == -1 && errno == EINTR)
	if (handle_pending_signals())
	    break;
}

void
xclosedir(DIR *dirp)
{
    while (closedir(dirp) == -1 && errno == EINTR)
	if (handle_pending_signals())
	    break;
}

int
xcreat(const char *path, mode_t mode)
{
    int res;

    while ((res = creat(path, mode)) == -1 && errno == EINTR)
	if (handle_pending_signals())
	    break;
    return res;
}

#ifdef HAVE_DUP2
static int
xdup2(int fildes, int fildes2)
{
    int res;

    while ((res = dup2(fildes, fildes2)) == -1 && errno == EINTR)
	if (handle_pending_signals())
	    break;
    return res;
}
#endif

struct group *
xgetgrgid(gid_t xgid)
{
    struct group *res;

    errno = 0;
    while ((res = getgrgid(xgid)) == NULL && errno == EINTR) {
	if (handle_pending_signals())
	    break;
	errno = 0;
    }
    return res;
}

struct passwd *
xgetpwnam(const char *name)
{
    struct passwd *res;

    errno = 0;
    while ((res = getpwnam(name)) == NULL && errno == EINTR) {
	if (handle_pending_signals())
	    break;
	errno = 0;
    }
    return res;
}

struct passwd *
xgetpwuid(uid_t xuid)
{
    struct passwd *res;

    errno = 0;
    while ((res = getpwuid(xuid)) == NULL && errno == EINTR) {
	if (handle_pending_signals())
	    break;
	errno = 0;
    }
    return res;
}

int
xopen(const char *path, int oflag, ...)
{
    int res;

    if ((oflag & O_CREAT) == 0) {
	while ((res = open(path, oflag)) == -1 && errno == EINTR)
	    if (handle_pending_signals())
		break;
    } else {
	va_list ap;
	mode_t mode;

	va_start(ap, oflag);
	/* "int" should actually be "mode_t after default argument
	   promotions". "int" is the best guess we have, "mode_t" used to be
	   "unsigned short", which we obviously can't use. */
	mode = va_arg(ap, int);
	va_end(ap);
	while ((res = open(path, oflag, mode)) == -1 && errno == EINTR)
	    if (handle_pending_signals())
		break;
    }
    return res;
}

ssize_t
xread(int fildes, void *buf, size_t nbyte)
{
    ssize_t res;

    /* This is where we will be blocked most of the time, so handle signals
       that didn't interrupt any system call. */
    do
      if (handle_pending_signals())
	  break;
    while ((res = read(fildes, buf, nbyte)) == -1 && errno == EINTR);
    return res;
}

#ifdef POSIX
int
xtcsetattr(int fildes, int optional_actions, const struct termios *termios_p)
{
    int res;

    while ((res = tcsetattr(fildes, optional_actions, termios_p)) == -1 &&
	   errno == EINTR)
	if (handle_pending_signals())
	    break;
    return res;
}
#endif

ssize_t
xwrite(int fildes, const void *buf, size_t nbyte)
{
    ssize_t res;

    /* This is where we will be blocked most of the time, so handle signals
       that didn't interrupt any system call. */
    do
      if (handle_pending_signals())
	  break;
    while ((res = write(fildes, buf, nbyte)) == -1 && errno == EINTR);
    return res;
}
