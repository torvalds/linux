/* glob.c: The csh et al glob pattern matching routines.

%%% copyright-cmetz-96
This software is Copyright 1996-2001 by Craig Metz, All Rights Reserved.
The Inner Net License Version 3 applies to this software.
You should have received a copy of the license with this software. If
you didn't get a copy, you may request one from <license@inner.net>.

Portions of this software are Copyright 1995 by Randall Atkinson and Dan
McDonald, All Rights Reserved. All Rights under this copyright are assigned
to the U.S. Naval Research Laboratory (NRL). The NRL Copyright Notice and
License Agreement applies to this software.

	History:

	Modified by cmetz for OPIE 2.32. Remove include of dirent.h here; it's
		done already (and conditionally) in opie_cfg.h.
	Modified by cmetz for OPIE 2.2. Use FUNCTION declaration et al.
             Remove useless strings. Prototype right.
	Modified at NRL for OPIE 2.0.
	Originally from BSD.
*/
/*
 * Copyright (c) 1980 Regents of the University of California.
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by the University of
 *      California, Berkeley and its contributors.
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
 * C-shell glob for random programs.
 */

#include "opie_cfg.h"

#if HAVE_SYS_PARAM_H
#include <sys/param.h>
#endif /* HAVE_SYS_PARAM_H */
#include <sys/stat.h>

#if HAVE_PWD_H
#include <pwd.h>
#endif /* HAVE_PWD_H */
#include <errno.h>
#include <stdio.h>
#include <string.h>
#if HAVE_LIMITS_H
#include <limits.h>
#endif /* HAVE_LIMITS_H */

#include "opie.h"

#ifndef NCARGS
#define NCARGS 600
#endif	/* NCARGS */
#define	QUOTE 0200
#define	TRIM 0177
#define	eq(a,b)		(strcmp((a),(b)) == (0))
#define	GAVSIZ		(NCARGS/6)
#define	isdir(d)	(((d.st_mode) & S_IFMT) == S_IFDIR)

static char **gargv;	/* Pointer to the (stack) arglist */
static int gargc;	/* Number args in gargv */
static int gnleft;
static short gflag;

static int letter __P((register char));
static int digit __P((register char));
static int any __P((int, char *));
static int blklen __P((register char **));
VOIDRET blkfree __P((char **));
static char *strspl __P((register char *, register char *));

static int tglob __P((register char c));

extern int errno;
static char *strend __P((char *));

static int globcnt;

static char *globchars = "`{[*?";
char *globerr = NULL;
char *home = NULL;

static char *gpath, *gpathp, *lastgpathp;
static int globbed;
static char *entp;
static char **sortbas;

static int amatch __P((char *p, char *s));
static int execbrc __P((register char *p, register char *s));
VOIDRET opiefatal __P((char *));
char **copyblk __P((char **));

static int match FUNCTION((s, p), char *s AND char *p)
{
  register int c;
  register char *sentp;
  char sglobbed = globbed;

  if (*s == '.' && *p != '.')
    return (0);
  sentp = entp;
  entp = s;
  c = amatch(s, p);
  entp = sentp;
  globbed = sglobbed;
  return (c);
}


static int Gmatch FUNCTION((s, p), register char *s AND register char *p)
{
  register int scc;
  int ok, lc;
  int c, cc;

  for (;;) {
    scc = *s++ & TRIM;
    switch (c = *p++) {

    case '[':
      ok = 0;
      lc = 077777;
      while (cc = *p++) {
	if (cc == ']') {
	  if (ok)
	    break;
	  return (0);
	}
	if (cc == '-') {
	  if (lc <= scc && scc <= *p++)
	    ok++;
	} else
	  if (scc == (lc = cc))
	    ok++;
      }
      if (cc == 0)
	if (ok)
	  p--;
	else
	  return 0;
      continue;

    case '*':
      if (!*p)
	return (1);
      for (s--; *s; s++)
	if (Gmatch(s, p))
	  return (1);
      return (0);

    case 0:
      return (scc == 0);

    default:
      if ((c & TRIM) != scc)
	return (0);
      continue;

    case '?':
      if (scc == 0)
	return (0);
      continue;

    }
  }
}

static VOIDRET Gcat FUNCTION((s1, s2), register char *s1 AND register char *s2)
{
  register int len = strlen(s1) + strlen(s2) + 1;

  if (len >= gnleft || gargc >= GAVSIZ - 1)
    globerr = "Arguments too long";
  else {
    gargc++;
    gnleft -= len;
    gargv[gargc] = 0;
    gargv[gargc - 1] = strspl(s1, s2);
  }
}

static VOIDRET addpath FUNCTION((c), char c)
{

  if (gpathp >= lastgpathp)
    globerr = "Pathname too long";
  else {
    *gpathp++ = c;
    *gpathp = 0;
  }
}

static VOIDRET rscan FUNCTION((t, f), register char **t AND int (*f)__P((char)))
{
  register char *p, c;

  while (p = *t++) {
    if (f == tglob)
      if (*p == '~')
	gflag |= 2;
      else
	if (eq(p, "{") || eq(p, "{}"))
	  continue;
    while (c = *p++)
      (*f) (c);
  }
}

static int tglob FUNCTION((c), register char c)
{
  if (any(c, globchars))
    gflag |= c == '{' ? 2 : 1;
  return (c);
}

static int letter FUNCTION((c), register char c)
{
  return (c >= 'a' && c <= 'z' || c >= 'A' && c <= 'Z' || c == '_');
}

static int digit FUNCTION((c), register char c)
{
  return (c >= '0' && c <= '9');
}

static int any FUNCTION((c, s), int c AND char *s)
{
  while (*s)
    if (*s++ == c)
      return (1);
  return (0);
}

static int blklen FUNCTION((av), register char **av)
{
  register int i = 0;

  while (*av++)
    i++;
  return (i);
}

static char **blkcpy FUNCTION((oav, bv), char **oav AND register char **bv)
{
  register char **av = oav;

  while (*av++ = *bv++)
    continue;
  return (oav);
}

VOIDRET blkfree FUNCTION((av0), char **av0)
{
  register char **av = av0;

  while (*av)
    free(*av++);
}

static char *strspl FUNCTION((cp, dp), register char *cp AND register char *dp)
{
  register char *ep = (char *) malloc((unsigned) (strlen(cp) +
						  strlen(dp) + 1));

  if (ep == (char *) 0)
    opiefatal("Out of memory");
  strcpy(ep, cp);
  strcat(ep, dp);
  return (ep);
}

char **copyblk FUNCTION((v), char **v)
{
  register char **nv = (char **) malloc((unsigned) ((blklen(v) + 1) *
						    sizeof(char **)));

  if (nv == (char **) 0)
    opiefatal("Out of memory");

  return (blkcpy(nv, v));
}

static char *strend FUNCTION((cp), register char *cp)
{

  while (*cp)
    cp++;
  return (cp);
}

/*
 * Extract a home directory from the password file
 * The argument points to a buffer where the name of the
 * user whose home directory is sought is currently.
 * We write the home directory of the user back there.
 */
static int gethdir FUNCTION((home), char *home)
{
  register struct passwd *pp = getpwnam(home);

  if (!pp || home + strlen(pp->pw_dir) >= lastgpathp)
    return (1);
  strcpy(home, pp->pw_dir);
  return (0);
}

static VOIDRET ginit FUNCTION((agargv), char **agargv)
{
  agargv[0] = 0;
  gargv = agargv;
  sortbas = agargv;
  gargc = 0;
  gnleft = NCARGS - 4;
}

static VOIDRET sort FUNCTION_NOARGS
{
  register char **p1, **p2, *c;
  char **Gvp = &gargv[gargc];

  p1 = sortbas;
  while (p1 < Gvp - 1) {
    p2 = p1;
    while (++p2 < Gvp)
      if (strcmp(*p1, *p2) > 0)
	c = *p1, *p1 = *p2, *p2 = c;
    p1++;
  }
  sortbas = Gvp;
}

static VOIDRET matchdir FUNCTION((pattern), char *pattern)
{
  struct stat stb;

  register struct dirent *dp;

  DIR *dirp;

  dirp = opendir(*gpath == '\0' ? "." : gpath);
  if (dirp == NULL) {
    if (globbed)
      return;
    goto patherr2;
  }
#if !defined(linux)
  if (fstat(dirp->dd_fd, &stb) < 0)
    goto patherr1;
  if (!isdir(stb)) {
    errno = ENOTDIR;
    goto patherr1;
  }
#endif /* !defined(linux) */
  while ((dp = readdir(dirp)) != NULL) {
    if (dp->d_ino == 0)
      continue;
    if (match(dp->d_name, pattern)) {
      Gcat(gpath, dp->d_name);
      globcnt++;
    }
  }
  closedir(dirp);
  return;

patherr1:
  closedir(dirp);
patherr2:
  globerr = "Bad directory components";
}

static VOIDRET expand FUNCTION((as), char *as)
{
  register char *cs;
  register char *sgpathp, *oldcs;
  struct stat stb;

  sgpathp = gpathp;
  cs = as;
  if (*cs == '~' && gpathp == gpath) {
    addpath('~');
    for (cs++; letter(*cs) || digit(*cs) || *cs == '-';)
      addpath(*cs++);
    if (!*cs || *cs == '/') {
      if (gpathp != gpath + 1) {
	*gpathp = 0;
	if (gethdir(gpath + 1))
	  globerr = "Unknown user name after ~";
	strcpy(gpath, gpath + 1);
      } else
	strcpy(gpath, home);
      gpathp = strend(gpath);
    }
  }
  while (!any(*cs, globchars)) {
    if (*cs == 0) {
      if (!globbed)
	Gcat(gpath, "");
      else
	if (stat(gpath, &stb) >= 0) {
	  Gcat(gpath, "");
	  globcnt++;
	}
      goto endit;
    }
    addpath(*cs++);
  }
  oldcs = cs;
  while (cs > as && *cs != '/')
    cs--, gpathp--;
  if (*cs == '/')
    cs++, gpathp++;
  *gpathp = 0;
  if (*oldcs == '{') {
    execbrc(cs, ((char *) 0));
    return;
  }
  matchdir(cs);
endit:
  gpathp = sgpathp;
  *gpathp = 0;
}

static int execbrc FUNCTION((p, s), char *p AND char *s)
{
  char restbuf[BUFSIZ + 2];
  register char *pe, *pm, *pl;
  int brclev = 0;
  char *lm, savec, *sgpathp;

  for (lm = restbuf; *p != '{'; *lm++ = *p++)
    continue;
  for (pe = ++p; *pe; pe++)
    switch (*pe) {

    case '{':
      brclev++;
      continue;

    case '}':
      if (brclev == 0)
	goto pend;
      brclev--;
      continue;

    case '[':
      for (pe++; *pe && *pe != ']'; pe++)
	continue;
      continue;
    }
pend:
  brclev = 0;
  for (pl = pm = p; pm <= pe; pm++)
    switch (*pm & (QUOTE | TRIM)) {

    case '{':
      brclev++;
      continue;

    case '}':
      if (brclev) {
	brclev--;
	continue;
      }
      goto doit;

    case ',' | QUOTE:
    case ',':
      if (brclev)
	continue;
  doit:
      savec = *pm;
      *pm = 0;
      strcpy(lm, pl);
      strcat(restbuf, pe + 1);
      *pm = savec;
      if (s == 0) {
	sgpathp = gpathp;
	expand(restbuf);
	gpathp = sgpathp;
	*gpathp = 0;
      } else
	if (amatch(s, restbuf))
	  return (1);
      sort();
      pl = pm + 1;
      if (brclev)
	return (0);
      continue;

    case '[':
      for (pm++; *pm && *pm != ']'; pm++)
	continue;
      if (!*pm)
	pm--;
      continue;
    }
  if (brclev)
    goto doit;
  return (0);
}

static VOIDRET acollect FUNCTION((as), register char *as)
{
  register int ogargc = gargc;

  gpathp = gpath;
  *gpathp = 0;
  globbed = 0;
  expand(as);
  if (gargc != ogargc)
    sort();
}

static VOIDRET collect FUNCTION((as), register char *as)
{
  if (eq(as, "{") || eq(as, "{}")) {
    Gcat(as, "");
    sort();
  } else
    acollect(as);
}

static int amatch FUNCTION((s, p), register char *s AND register char *p)
{
  register int scc;
  int ok, lc;
  char *sgpathp;
  struct stat stb;
  int c, cc;

  globbed = 1;
  for (;;) {
    scc = *s++ & TRIM;
    switch (c = *p++) {

    case '{':
      return (execbrc(p - 1, s - 1));

    case '[':
      ok = 0;
      lc = 077777;
      while (cc = *p++) {
	if (cc == ']') {
	  if (ok)
	    break;
	  return (0);
	}
	if (cc == '-') {
	  if (lc <= scc && scc <= *p++)
	    ok++;
	} else
	  if (scc == (lc = cc))
	    ok++;
      }
      if (cc == 0)
	if (ok)
	  p--;
	else
	  return 0;
      continue;

    case '*':
      if (!*p)
	return (1);
      if (*p == '/') {
	p++;
	goto slash;
      }
      s--;
      do {
	if (amatch(s, p))
	  return (1);
      }
      while (*s++);
      return (0);

    case 0:
      return (scc == 0);

    default:
      if (c != scc)
	return (0);
      continue;

    case '?':
      if (scc == 0)
	return (0);
      continue;

    case '/':
      if (scc)
	return (0);
  slash:
      s = entp;
      sgpathp = gpathp;
      while (*s)
	addpath(*s++);
      addpath('/');
      if (stat(gpath, &stb) == 0 && isdir(stb))
	if (*p == 0) {
	  Gcat(gpath, "");
	  globcnt++;
	} else
	  expand(p);
      gpathp = sgpathp;
      *gpathp = 0;
      return (0);
    }
  }
}


char **ftpglob FUNCTION((v), register char *v)
{
  char agpath[BUFSIZ];
  char *agargv[GAVSIZ];
  char *vv[2];

  vv[0] = v;
  vv[1] = 0;
  gflag = 0;
  rscan(vv, tglob);
  if (gflag == 0) {
    vv[0] = strspl(v, "");
    return (copyblk(vv));
  }
  globerr = 0;
  gpath = agpath;
  gpathp = gpath;
  *gpathp = 0;
  lastgpathp = &gpath[sizeof agpath - 2];
  ginit(agargv);
  globcnt = 0;
  collect(v);
  if (globcnt == 0 && (gflag & 1)) {
    blkfree(gargv), gargv = 0;
    return (0);
  } else
    return (gargv = copyblk(gargv));
}
