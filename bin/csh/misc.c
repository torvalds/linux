/*	$OpenBSD: misc.c,v 1.25 2018/10/24 06:01:03 martijn Exp $	*/
/*	$NetBSD: misc.c,v 1.6 1995/03/21 09:03:09 cgd Exp $	*/

/*-
 * Copyright (c) 1980, 1991, 1993
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

#include <sys/types.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdarg.h>

#include "csh.h"
#include "extern.h"

static int	fdcmp(int);
static int	renum(int, int);

int
any(char *s, int c)
{
    if (!s)
	return (0);		/* Check for nil pointer */
    while (*s)
	if (*s++ == c)
	    return (1);
    return (0);
}

Char  **
blkend(Char **up)
{

    while (*up)
	up++;
    return (up);
}


void
blkpr(FILE *fp, Char **av)
{

    for (; *av; av++) {
	(void) fprintf(fp, "%s", vis_str(*av));
	if (av[1])
	    (void) fprintf(fp, " ");
    }
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

Char  **
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
	free(* av);
    free(av0);
}

Char  **
saveblk(Char **v)
{
    Char **newv = xcalloc(blklen(v) + 1, sizeof(*newv));
    Char  **onewv = newv;

    while (*v)
	*newv++ = Strsave(*v++);
    return (onewv);
}

Char  **
blkspl(Char **up, Char **vp)
{
    Char **wp = xcalloc(blklen(up) + blklen(vp) + 1, sizeof(*wp));

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
 * Returns 0 if fd is in use, 1 if fd is greater than the largest used file
 * descriptor and -1 otherwise.
 */
static int
fdcmp(int fd)
{
    int fds[] = { SHIN, SHOUT, SHERR, OLDSTD, FSHTTY };
    int i, max;

    max = -1;
    for (i = 0; i < sizeof(fds)/sizeof(fds[0]); i++) {
	if (fd == fds[i])
	    return (0);
	if (fds[i] > max)
	    max = fds[i];
    }
    if (fd > max)
	return (1);

    return (-1);
}

/*
 * This routine is called after an error to close up
 * any units which may have been left open accidentally.
 */
void
closem(void)
{
    int f;
    int max = sysconf(_SC_OPEN_MAX);

    for (f = 0; f < max; f++)
	switch (fdcmp(f)) {
	case 0:
	    continue;
	case 1:
	    closefrom(f);
	    return;
	default:
	    close(f);
	}
}

void
donefds(void)
{
    (void) close(0);
    (void) close(1);
    (void) close(2);

    didfds = 0;
}

/*
 * Move descriptor i to j.
 * If j is -1 then we just want to get i to a safe place,
 * i.e. to a unit > 2.  This also happens in dcopy.
 */
int
dmove(int i, int j)
{

    if (i == j || i < 0)
	return (i);
    if (j >= 0) {
	(void) dup2(i, j);
	if (j != i)
	    (void) close(i);
	return (j);
    }
    j = dcopy(i, j);
    if (j != i)
	(void) close(i);
    return (j);
}

int
dcopy(int i, int j)
{

    if (i == j || i < 0 || (j < 0 && i > 2))
	return (i);
    if (j >= 0) {
	(void) dup2(i, j);
	return (j);
    }
    (void) close(j);
    return (renum(i, j));
}

static int
renum(int i, int j)
{
    int k = dup(i);

    if (k < 0)
	return (-1);
    if (j == -1 && k > 2)
	return (k);
    if (k != j) {
	j = renum(k, j);
	(void) close(k);
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
	free(*u);
    (void) blkcpy(v, u);
}

int
number(Char *cp)
{
    if (!cp)
	return(0);
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
    Char  **nv = xcalloc(blklen(v) + 1, sizeof(*nv));

    return (blkcpy(nv, v));
}

Char   *
strip(Char *cp)
{
    Char *dp = cp;

    if (!cp)
	return (cp);
    while ((*dp++ &= TRIM) != '\0')
	continue;
    return (cp);
}

Char   *
quote(Char *cp)
{
    Char *dp = cp;

    if (!cp)
	return (cp);
    while (*dp != '\0')
	*dp++ |= QUOTE;
    return (cp);
}

void
udvar(Char *name)
{

    setname(vis_str(name));
    stderror(ERR_NAME | ERR_UNDVAR);
}

int
prefix(Char *sub, Char *str)
{

    for (;;) {
	if (*sub == 0)
	    return (1);
	if (*str == 0)
	    return (0);
	if (*sub++ != *str++)
	    return (0);
    }
}
