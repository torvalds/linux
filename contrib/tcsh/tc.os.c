/* $Header: /p/tcsh/cvsroot/tcsh/tc.os.c,v 3.73 2016/04/16 15:44:18 christos Exp $ */
/*
 * tc.os.c: OS Dependent builtin functions
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

RCSID("$tcsh: tc.os.c,v 3.73 2016/04/16 15:44:18 christos Exp $")

#include "tw.h"
#include "ed.h"
#include "ed.defns.h"		/* for the function names */
#include "sh.decls.h"

#ifdef _UWIN
#define TIOCGPGRP TIOCGETPGRP
#define TIOCSPGRP TIOCSETPGRP
#endif

/***
 *** MACH
 ***/

#ifdef MACH
/* dosetpath -- setpath built-in command
 *
 **********************************************************************
 * HISTORY
 * 08-May-88  Richard Draves (rpd) at Carnegie-Mellon University
 *	Major changes to remove artificial limits on sizes and numbers
 *	of paths.
 *
 **********************************************************************
 */

#ifdef MACH
static Char STRCPATH[] = {'C', 'P', 'A', 'T', 'H', '\0'};
static Char STRLPATH[] = {'L', 'P', 'A', 'T', 'H', '\0'};
static Char STRMPATH[] = {'M', 'P', 'A', 'T', 'H', '\0'};
# if EPATH
static Char STREPATH[] = {'E', 'P', 'A', 'T', 'H', '\0'};
# endif
#endif /* MACH */
static Char *syspaths[] = {STRKPATH, STRCPATH, STRLPATH, STRMPATH, 

#if EPATH
	STREPATH,
#endif
	 0};
#define LOCALSYSPATH	"/usr/local"

/*ARGSUSED*/
void
dosetpath(Char **arglist, struct command *c)
{
    extern char *getenv();
    Char  **pathvars, **cmdargs;
    char  **spaths, **cpaths, **cmds;
    char   *tcp;
    unsigned int npaths, ncmds;
    int     i, sysflag;

    pintr_disabled++;
    cleanup_push(&pintr_disabled, disabled_cleanup);

    /*
     * setpath(3) uses stdio and we want 0, 1, 2 to work...
     */
    if (!didfds) {
	(void) dcopy(SHIN, 0);
	(void) dcopy(SHOUT, 1);
	(void) dcopy(SHDIAG, 2);
	didfds = 1;
    }

    for (i = 1; arglist[i] && (arglist[i][0] != '-'); i++);
    npaths = i - 1;

    cmdargs = &arglist[i];
    for (; arglist[i]; i++);
    ncmds = i - npaths - 1;

    if (npaths) {
	sysflag = 0;
	pathvars = &arglist[1];
    }
    else {
	sysflag = 1;
	npaths = (sizeof syspaths / sizeof *syspaths) - 1;
	pathvars = syspaths;
    }

    /* note that npaths != 0 */

    spaths = xmalloc(npaths * sizeof *spaths);
    setzero(spaths, npaths * sizeof *spaths);
    cpaths = xmalloc((npaths + 1) * sizeof *cpaths);
    setzero(cpaths, (npaths + 1) * sizeof *cpaths);
    cmds = xmalloc((ncmds + 1) * sizeof *cmds);
    setzero(cmds, (ncmds + 1) * sizeof *cmds);
    for (i = 0; i < npaths; i++) {
	char   *val = getenv(short2str(pathvars[i]));

	if (val == NULL)
	    val = "";

	spaths[i] = xmalloc((Strlen(pathvars[i]) + strlen(val) + 2) *
			    sizeof **spaths);
	(void) strcpy(spaths[i], short2str(pathvars[i]));
	(void) strcat(spaths[i], "=");
	(void) strcat(spaths[i], val);
	cpaths[i] = spaths[i];
    }

    for (i = 0; i < ncmds; i++) {
	Char   *val = globone(cmdargs[i], G_ERROR);/*FIXRESET*/

	if (val == NULL)
	    goto abortpath;
	cmds[i] = strsave(short2str(val));
    }


    if (setpath(cpaths, cmds, LOCALSYSPATH, sysflag, 1) < 0) {
abortpath:
	if (spaths) {
	    for (i = 0; i < npaths; i++)
		xfree(spaths[i]);
	    xfree(spaths);
	}
	xfree(cpaths);
	if (cmds) {
	    for (i = 0; i < ncmds; i++)
		xfree(cmds[i]);
	    xfree(cmds);
	}

	cleanup_until(&pintr_disabled);
	donefds();
	return;
    }

    for (i = 0; i < npaths; i++) {
	Char	*val, *name;

	name = str2short(cpaths[i]);
	for (val = str2short(cpaths[i]); val && *val && *val != '='; val++);
	if (val && *val == '=') {
	    *val++ = '\0';

	    tsetenv(name, val);/*FIXRESET*/
	    if (Strcmp(name, STRKPATH) == 0) {
		importpath(val);/*FIXRESET*/
		if (havhash)
		    dohash(NULL, NULL);/*FIXRESET*/
	    }
	    *--val = '=';
	}
    }
    cleanup_until(&pintr_disabled);
    donefds();
}
#endif /* MACH */

/***
 *** AIX
 ***/
#ifdef TCF
/* ARGSUSED */
void
dogetxvers(Char **v, struct command *c)
{
    char    xvers[MAXPATHLEN];

    if (getxvers(xvers, MAXPATHLEN) == -1)
	stderror(ERR_SYSTEM, "getxvers", strerror(errno));
    xprintf("%s\n", xvers);
    flush();
}

/*ARGSUSED*/
void
dosetxvers(Char **v, struct command *c)
{
    char   *xvers;

    ++v;
    if (!*v || *v[0] == '\0')
	xvers = "";
    else
	xvers = short2str(*v);
    if (setxvers(xvers) == -1)
	stderror(ERR_SYSTEM, "setxvers", strerror(errno));
}

#include <sf.h>
#ifdef _AIXPS2
# define XC_PDP11	0x01
# define XC_23		0x02
# define XC_Z8K		0x03
# define XC_8086	0x04
# define XC_68K		0x05
# define XC_Z80		0x06
# define XC_VAX		0x07
# define XC_16032	0x08
# define XC_286		0x09
# define XC_386		0x0a
# define XC_S370	0x0b
#else
# include <sys/x.out.h>
#endif /* _AIXPS2 */

static struct xc_cpu_t {
    short   xc_id;
    char   *xc_name;
}       xcpu[] =
{
    { XC_PDP11,	"pdp11"   },
    { XC_23,	"i370"    },
    { XC_Z8K,	"z8000"   },
    { XC_8086,	"i86"	  },
    { XC_68K,	"mc68000" },
    { XC_Z80,	"x80"	  },
    { XC_VAX,	"vax"	  },
    { XC_16032,	"ns16032" },
    { XC_286,	"i286"	  },
    { XC_386,	"i386"	  },
    { XC_S370,	"xa370"	  },
    { 0,	NULL      }
};

/*
 * our local hack table, stolen from x.out.h
 */
static char *
getxcode(short xcid)
{
    int     i;

    for (i = 0; xcpu[i].xc_name != NULL; i++)
	if (xcpu[i].xc_id == xcid)
	    return (xcpu[i].xc_name);
    return (NULL);
}

static short
getxid(char *xcname)
{
    int     i;

    for (i = 0; xcpu[i].xc_name != NULL; i++)
	if (strcmp(xcpu[i].xc_name, xcname) == 0)
	    return (xcpu[i].xc_id);
    return ((short) -1);
}


/*ARGSUSED*/
void
dogetspath(Char **v, struct command *c)
{
    int     i, j;
    sitepath_t p[MAXSITE];
    struct sf *st;
    static char *local = "LOCAL ";

    if ((j = getspath(p, MAXSITE)) == -1)
	stderror(ERR_SYSTEM, "getspath", strerror(errno));
    for (i = 0; i < j && (p[i] & SPATH_CPU) != NOSITE; i++) {
	if (p[i] & SPATH_CPU) {
	    if ((p[i] & SPATH_MASK) == NULLSITE)
		xprintf(local);
	    else if ((st = sfxcode((short) (p[i] & SPATH_MASK))) != NULL)
		xprintf("%s ", st->sf_ctype);
	    else {
		char   *xc = getxcode(p[i] & SPATH_MASK);

		if (xc != NULL)
		    xprintf("%s ", xc);
		else
		    xprintf("*cpu %d* ", (int) (p[i] & SPATH_MASK));
		/* 
		 * BUG in the aix code... needs that cause if
		 * sfxcode fails once it fails for ever 
		 */
		endsf();	
	    }
	}
	else {
	    if (p[i] == NULLSITE)
		xprintf(local);
	    else if ((st = sfnum(p[i])) != NULL)
		xprintf("%s ", st->sf_sname);
	    else
		xprintf("*site %d* ", (int) (p[i] & SPATH_MASK));
	}
    }
    xputchar('\n');
    flush();
}

/*ARGSUSED*/
void
dosetspath(Char **v, struct command *c)
{
    int     i;
    short   j;
    char   *s;
    sitepath_t p[MAXSITE];
    struct sf *st;

    /*
     * sfname() on AIX G9.9 at least, mallocs too pointers p, q
     * then does the equivalent of while (*p++ == *q++) continue;
     * and then tries to free(p,q) them! Congrats to the wizard who
     * wrote that one. I bet he tested it really well too.
     * Sooo, we set dont_free :-)
     */
    dont_free = 1;
    for (i = 0, v++; *v && *v[0] != '\0'; v++, i++) {
	s = short2str(*v);
	if (isdigit(*s))
	    p[i] = atoi(s);
	else if (strcmp(s, "LOCAL") == 0)
	    p[i] = NULLSITE;
	else if ((st = sfctype(s)) != NULL)
	    p[i] = SPATH_CPU | st->sf_ccode;
	else if ((j = getxid(s)) != -1)
	    p[i] = SPATH_CPU | j;
	else if ((st = sfname(s)) != NULL)
	    p[i] = st->sf_id;
	else {
	    setname(s);
	    stderror(ERR_NAME | ERR_STRING, CGETS(23, 1, "Bad cpu/site name"));
	}
	if (i == MAXSITE - 1)
	    stderror(ERR_NAME | ERR_STRING, CGETS(23, 2, "Site path too long"));
    }
    if (setspath(p, i) == -1)
	stderror(ERR_SYSTEM, "setspath", strerror(errno));
    dont_free = 0;
}

/* sitename():
 *	Return the site name where the process is running
 */
char   *
sitename(pid_t pid)
{
    siteno_t ss;
    struct sf *st;

    if ((ss = site(pid)) == -1 || (st = sfnum(ss)) == NULL)
	return CGETS(23, 3, "unknown");
    else
	return st->sf_sname;
}

static int
migratepid(pit_t pid, siteno_t new_site)
{
    struct sf *st;
    int     need_local;

    need_local = (pid == 0) || (pid == getpid());

    if (kill3(pid, SIGMIGRATE, new_site) < 0) {
	xprintf("%d: %s\n", pid, strerror(errno));
	return (-1);
    }

    if (need_local) {
	if ((new_site = site(0)) == -1) {
	    xprintf(CGETS(23, 4, "site: %s\n"), strerror(errno));
	    return (-1);
	}
	if ((st = sfnum(new_site)) == NULL) {
	    xprintf(CGETS(23, 5, "%d: Site not found\n"), new_site);
	    return (-1);
	}
	if (setlocal(st->sf_local, strlen(st->sf_local)) == -1) {
	    xprintf(CGETS(23, 6, "setlocal: %s: %s\n"),
			  st->sf_local, strerror(errno));
	    return (-1);
	}
    }
    return (0);
}

/*ARGSUSED*/
void
domigrate(Char **v, struct command *c)
{
    struct sf *st;
    char   *s;
    Char   *cp;
    struct process *pp;
    int    err1 = 0;
    int    pid = 0;
    siteno_t new_site = 0;

    pchild_disabled++;
    cleanup_push(&pchild_disabled, disabled_cleanup);
    if (setintr) {
	pintr_disabled++;
	cleanup_push(&pintr_disabled, disabled_cleanup);
    }

    ++v;
    if (*v[0] == '-') {
	/*
	 * Do the -site.
	 */
	s = short2str(&v[0][1]);
	/*
	 * see comment in setspath()
	 */
	dont_free = 1;
	if ((st = sfname(s)) == NULL) {
	    dont_free = 0;
	    setname(s);
	    stderror(ERR_NAME | ERR_STRING, CGETS(23, 7, "Site not found"));
	}
	dont_free = 0;
	new_site = st->sf_id;
	++v;
    }

    if (!*v || *v[0] == '\0') {
	if (migratepid(0, new_site) == -1)
	    err1++;
    }
    else {
	Char **globbed;

	v = glob_all_or_error(v);
	globbed = v;
	cleanup_push(globbed, blk_cleanup);

	while (v && (cp = *v)) {
	    if (*cp == '%') {
		pp = pfind(cp);
		if (kill3(- pp->p_jobid, SIGMIGRATE, new_site) < 0) {
		    xprintf("%S: %s\n", cp, strerror(errno));
		    err1++;
		}
	    }
	    else if (!(Isdigit(*cp) || *cp == '-'))
		stderror(ERR_NAME | ERR_JOBARGS);
	    else {
		pid = atoi(short2str(cp));
		if (migratepid(pid, new_site) == -1)
		    err1++;
	    }
	    v++;
	}
	cleanup_until(globbed);
    }

done:
    cleanup_until(&pchild_disabled);
    if (err1)
	stderror(ERR_SILENT);
}

#endif /* TCF */

/***
 *** CRAY ddmode <velo@sesun3.epfl.ch> (Martin Ouwehand EPFL-SIC/SE)
 ***/
#if defined(_CRAY) && !defined(_CRAYMPP)
void
dodmmode(Char **v, struct command *c)
{
    Char *cp = v[1];

    USE(c);

    if ( !cp ) {
	int mode;

	mode = dmmode(0);
	dmmode(mode);
	xprintf("%d\n",mode);
    }
    else {
	if (cp[1] != '\0')
	    stderror(ERR_NAME | ERR_STRING,
		     CGETS(23, 30, "Too many arguments"));
	else
	    switch(*cp) {
	    case '0':
		dmmode(0);
		break;
	    case '1':
		dmmode(1);
		break;
	    default:
		stderror(ERR_NAME | ERR_STRING,
			 CGETS(23, 31, "Invalid argument"));
	    }
    }
}
#endif /* _CRAY && !_CRAYMPP */


/***
 *** CONVEX Warps.
 ***/

#ifdef WARP
/*
 * handle the funky warping of symlinks
 */
#include <warpdb.h>
#include <sys/warp.h>

static jmp_buf sigsys_buf;

static void
catch_sigsys(void)
{
    sigset_t set;
    sigemptyset(&set, SIGSYS);
    (void)sigprocmask(SIG_UNBLOCK, &set, NULL);
    longjmp(sigsys_buf, 1);
}


/*ARGSUSED*/
void
dowarp(Char **v, struct command *c) 
{
    int     warp, oldwarp;
    struct warpent *we;
    volatile struct sigaction old_sigsys_handler;
    char   *newwarp;

    if (setjmp(sigsys_buf)) {
	sigaction(SIGSYS, &old_sigsys_handler, NULL);
	stderror(ERR_NAME | ERR_STRING,
		 CGETS(23, 8, "You're trapped in a universe you never made"));
	return;
    }
    sigaction(SIGSYS, NULL, &old_sigsys_handler);
    signal(SIGSYS, catch_sigsys);

    warp = getwarp();

    v++;
    if (*v == 0) {		/* display warp value */
	if (warp < 0)
	    stderror(ERR_NAME | ERR_STRING, CGETS(23, 9, "Getwarp failed"));
	we = getwarpbyvalue(warp);
	if (we)
	    printf("%s\n", we->w_name);
	else
	    printf("%d\n", warp);
    }
    else {			/* set warp value */
	oldwarp = warp;
	newwarp = short2str(*v);
	if (Isdigit(*v[0]))
	    warp = atoi(newwarp);
	else {
	    we = getwarpbyname(newwarp);
	    if (we)
		warp = we->w_value;
	    else
		warp = -1;
	}
	if ((warp < 0) || (warp >= WARP_MAXLINK))
	    stderror(ERR_NAME | ERR_STRING, CGETS(23, 10, "Invalid warp"));
	if ((setwarp(warp) < 0) || (getwarp() != warp)) {
	    (void) setwarp(oldwarp);
	    stderror(ERR_NAME | ERR_STRING, CGETS(23, 11, "Setwarp failed"));
	}
    }
    sigaction(SIGSYS, &old_sigsys_handler, NULL);
}
#endif /* WARP */

/***
 *** Masscomp or HCX
 ***/
/* Added, DAS DEC-90. */
#if defined(masscomp) || defined(_CX_UX)
static void
setuniverse_cleanup(void *xbuf)
{
    char *buf;

    buf = xbuf;
    setuniverse(buf);
}

/*ARGSUSED*/
void
douniverse(Char **v, struct command *c) 
{
    Char *cp = v[1];
    Char *cp2;		/* dunno how many elements v comes in with */
    char    ubuf[100];

    if (cp == 0) {
	(void) getuniverse(ubuf);
	xprintf("%s\n", ubuf);
    }
    else {
	cp2 = v[2];
	if (cp2 == 0) {
	    if (*cp == '\0' || setuniverse(short2str(cp)) != 0)
		stderror(ERR_NAME | ERR_STRING, CGETS(23, 12, "Illegal universe"));
	    }
	else {
	    (void) getuniverse(ubuf);
	    if (*cp == '\0' || setuniverse(short2str(cp)) != 0)
		stderror(ERR_NAME | ERR_STRING, CGETS(23, 12, "Illegal universe"));
	    cleanup_push(ubuf, setuniverse_cleanup);
	    if (setintr) {
		pintr_disabled++;
		cleanup_push(&pintr_disabled, disabled_cleanup);
	    }
	    lshift(v, 2);
	    if (setintr)
		cleanup_until(&pintr_disabled);
	    reexecute(c);
	    cleanup_until(ubuf);
	}
    }
}
#endif /* masscomp || _CX_UX */

/***
 *** BS2000/OSD POSIX (Fujitsu Siemens Computers)
 ***/
#if defined(_OSD_POSIX)
static int
bs2upcase(char *str)
{
    enum { outside = ' ', singlequote='\'', doublequote='"'} string = outside;

    char *white;

    for (white = str + strlen(str) - 1; isspace(*white) && white > str; --white)
        *white = '\0';

    for (; *str != '\0'; ++str)
    {
        if (string == outside)
        {
            *str = toupper (*str);
        }
        if (*str == '\'')
        {
            if (string == outside)
                string = singlequote;
            else if (string != doublequote)
                string = outside;
        }
        else if (*str == '"')
        {
            if (string == outside)
                string = doublequote;
            else if (string != singlequote)
                string = outside;
        }
    }
    if (string != outside)
    {
        stderror(ERR_NAME | ERR_UNMATCHED, (Char) string);
        return 1;
    }
    return 0;
}
static int
bs2cmdlist(char *str)
{
    char *str_beg = NULL;
    int ret = 0;

    enum { outside = ' ', singlequote='\'', doublequote='"'} string = outside;

    while (*str != '\0')
    {
        while (isspace(*str))
            ++str;

        if (*str == '\0')
            break;

        str_beg = str;
        
        for (; *str != '\0'; ++str)
        {
            if (string == outside && *str == ';') /* End of command */
            {
                *str++ = '\0';
                break;    /* continue with next command */
            }
            if (*str == '\'')
            {
                if (string == outside)
                    string = singlequote;
                else if (string != doublequote)
                    string = outside;
            }
            else if (*str == '"')
            {
                if (string == outside)
                    string = doublequote;
                else if (string != singlequote)
                    string = outside;
            }
        }
        if (strlen(str_beg) != 0)
        {
            ret = bs2system(str_beg);
	    flush();
            if (ret != 0 /*&& !option.err_ignore*/)
                break; /* do not continue after errors */
        }
    }

    if (string != outside)
    {
        stderror(ERR_NAME | ERR_UNMATCHED, (Char) string);
        return -1;
    }

    return ret;
}
/*ARGSUSED*/
void
dobs2cmd(Char **v, struct command *c)
{
    Char *cp, **globbed;
    int  i = 0, len = 0;
    char *cmd = NULL;
    int     pvec[2];
    struct command faket;
    Char   *fakecom[2];
    char    tibuf[BUFSIZE];
    int     icnt, old_pintr_disabled;
    static const Char STRbs2cmd[] = { 'b','s','2','c','m','d','\0' };

    v++;
    if (setintr)
	pintr_push_enable(&old_pintr_disabled);
    v = glob_all_or_error(v);
    if (setintr)
	cleanup_until(&old_pintr_disabled);
    globbed = v;
    cleanup_push(globbed, blk_cleanup);

    /* First round: count the string lengths */
    for (i=0; v[i]; ++i) {
	len += Strlen(v[i]) + (v[i+1] != NULL);
    }

    cmd = xmalloc(len+1); /* 1 for the final '\0' *//* FIXME: memory leak? */

    /* 2nd round: fill cmd buffer */
    i = 0;
    while ((cp = *v++) != 0) {
	int c;
	while (c = *cp++)
	    cmd[i++] = (char)c;
        if (*v)
	    cmd[i++] = ' ';
    }
    cmd[i] = '\0';

    /* Make upper case */
    bs2upcase(cmd);

    faket.t_dtyp = NODE_COMMAND;
    faket.t_dflg = F_BACKQ|F_STDERR;
    faket.t_dlef = 0;
    faket.t_drit = 0;
    faket.t_dspr = 0;
    faket.t_dcom = fakecom;
    fakecom[0] = (Char *)STRbs2cmd;
    fakecom[1] = 0;

    mypipe(pvec);
    cleanup_push(&pvec[0], open_cleanup);
    cleanup_push(&pvec[1], open_cleanup);
    if (pfork(&faket, -1) == 0) {
	sigset_t set;
        /* child */
        xclose(pvec[0]);
        (void) dmove(pvec[1], 1);
        (void) dmove(SHDIAG,  2);
        initdesc();
	sigemptyset(&set);
	sigaddset(&set, SIGINT);
	(void)sigprocmask(SIG_UNBLOCK, &set, NULL);
#ifdef SIGTSTP
        signal(SIGTSTP, SIG_IGN);
#endif
#ifdef SIGTTIN
        signal(SIGTTIN, SIG_IGN);
#endif
#ifdef SIGTTOU
        signal(SIGTTOU, SIG_IGN);
#endif
        xexit(bs2cmdlist(cmd));
    }
    cleanup_until(&pvec[1]);
    for(;;) {
	int old_pintr_disabled;

	if (setintr)
	    pintr_push_enable(&old_pintr_disabled);
	icnt = xread(pvec[0], tibuf, sizeof(tibuf));
	if (setintr)
	    cleanup_until(&old_pintr_disabled);
        if (icnt <= 0)
            break;
        for (i = 0; i < icnt; i++)
            xputchar((unsigned char) tibuf[i]);
    }
    cleanup_until(&pvec[0]);
    pwait();

    flush();

    cleanup_until(globbed);
}
#endif /* _OSD_POSIX */

#if defined(_CX_UX)
static void
setuniverse_cleanup(void *xbuf)
{
    char *buf;

    buf = xbuf;
    setuniverse(buf);
}

/*ARGSUSED*/
void
doatt(Char **v, struct command *c)
{
    Char *cp = v[1];
    char    ubuf[100];

    if (cp == 0)
	(void) setuniverse("att");
    else {
	(void) getuniverse(ubuf);
	(void) setuniverse("att");
	cleanup_push(ubuf, setuniverse_cleanup);
	if (setintr) {
	    pintr_disabled++;
	    cleanup_push(&pintr_disabled, disabled_cleanup);
	}
	lshift(v, 1);
	if (setintr)
	    cleanup_until(&pintr_disabled);
	reexecute(c);
	cleanup_until(ubuf);
    }
}

/*ARGSUSED*/
void
doucb(Char **v, struct command *c)
{
    Char *cp = v[1];
    char    ubuf[100];

    if (cp == 0)
	(void) setuniverse("ucb");
    else {
	(void) getuniverse(ubuf);
	(void) setuniverse("ucb");
	cleanup_push(ubuf, setuniverse_cleanup);
	if (setintr) {
	    pintr_disabled++;
	    cleanup_push(&pintr_disabled, disabled_cleanup);
	}
	lshift(v, 1);
	if (setintr)
	    cleanup_until(&pintr_disabled);
	reexecute(c);
	cleanup_until(ubuf);
    }
}
#endif /* _CX_UX */

#ifdef _SEQUENT_
/*
 * Compute the difference in process stats.
 */
void
pr_stat_sub(struct process_stats *p2, struct process_stats *p1,
	    struct process_stats *pr)
{
    pr->ps_utime.tv_sec = p2->ps_utime.tv_sec - p1->ps_utime.tv_sec;
    pr->ps_utime.tv_usec = p2->ps_utime.tv_usec - p1->ps_utime.tv_usec;
    if (pr->ps_utime.tv_usec < 0) {
	pr->ps_utime.tv_sec -= 1;
	pr->ps_utime.tv_usec += 1000000;
    }
    pr->ps_stime.tv_sec = p2->ps_stime.tv_sec - p1->ps_stime.tv_sec;
    pr->ps_stime.tv_usec = p2->ps_stime.tv_usec - p1->ps_stime.tv_usec;
    if (pr->ps_stime.tv_usec < 0) {
	pr->ps_stime.tv_sec -= 1;
	pr->ps_stime.tv_usec += 1000000;
    }

    pr->ps_maxrss = p2->ps_maxrss - p1->ps_maxrss;
    pr->ps_pagein = p2->ps_pagein - p1->ps_pagein;
    pr->ps_reclaim = p2->ps_reclaim - p1->ps_reclaim;
    pr->ps_zerofill = p2->ps_zerofill - p1->ps_zerofill;
    pr->ps_pffincr = p2->ps_pffincr - p1->ps_pffincr;
    pr->ps_pffdecr = p2->ps_pffdecr - p1->ps_pffdecr;
    pr->ps_swap = p2->ps_swap - p1->ps_swap;
    pr->ps_syscall = p2->ps_syscall - p1->ps_syscall;
    pr->ps_volcsw = p2->ps_volcsw - p1->ps_volcsw;
    pr->ps_involcsw = p2->ps_involcsw - p1->ps_involcsw;
    pr->ps_signal = p2->ps_signal - p1->ps_signal;
    pr->ps_lread = p2->ps_lread - p1->ps_lread;
    pr->ps_lwrite = p2->ps_lwrite - p1->ps_lwrite;
    pr->ps_bread = p2->ps_bread - p1->ps_bread;
    pr->ps_bwrite = p2->ps_bwrite - p1->ps_bwrite;
    pr->ps_phread = p2->ps_phread - p1->ps_phread;
    pr->ps_phwrite = p2->ps_phwrite - p1->ps_phwrite;
}

#endif /* _SEQUENT_ */


#ifndef HAVE_MEMSET
/* This is a replacement for a missing memset function */
void *xmemset(void *loc, int value, size_t len)
{
    char *ptr = loc;

    while (len--)
	*ptr++ = value;
    return loc;
}
#endif /* !HAVE_MEMSET */


#ifndef HAVE_MEMMOVE
/* memmove():
 * 	This is the ANSI form of bcopy() with the arguments backwards...
 *	Unlike memcpy(), it handles overlaps between source and 
 *	destination memory
 */
void *
xmemmove(void *vdst, const void *vsrc, size_t len)
{
    const char *src = vsrc;
    char *dst = vdst;

    if (src == dst)
	return vdst;

    if (src > dst) {
	while (len--) 
	    *dst++ = *src++;
    }
    else {
	src += len;
	dst += len;
	while (len--) 
	    *--dst = *--src;
    }
    return vdst;
}
#endif /* HAVE_MEMMOVE */


#ifndef WINNT_NATIVE
#ifdef NEEDtcgetpgrp
pid_t
xtcgetpgrp(int fd)
{
    int     pgrp;

    /* ioctl will handle setting errno correctly. */
    if (ioctl(fd, TIOCGPGRP, (ioctl_t) & pgrp) < 0)
	return (-1);
    return (pgrp);
}

/*
 * XXX: tcsetpgrp is not a macro any more cause on some systems,
 * pid_t is a short, but the ioctl() takes a pointer to int (pyr)
 * Thanks to Simon Day (simon@pharaoh.cyborg.bt.co.uk) for pointing
 * this out.
 */
int
xtcsetpgrp(int fd, int pgrp)
{
    return ioctl(fd, TIOCSPGRP, (ioctl_t) &pgrp);
}

#endif	/* NEEDtcgetpgrp */
#endif /* WINNT_NATIVE */


#ifdef YPBUGS
void
fix_yp_bugs(void)
{
    char   *mydomain;

    extern int yp_get_default_domain (char **);
    /*
     * PWP: The previous version assumed that yp domain was the same as the
     * internet name domain.  This isn't allways true. (Thanks to Mat Landau
     * <mlandau@bbn.com> for the original version of this.)
     */
    if (yp_get_default_domain(&mydomain) == 0) {	/* if we got a name */
	extern void yp_unbind (const char *);

	yp_unbind(mydomain);
    }
}

#endif /* YPBUGS */

#ifdef STRCOLLBUG
void
fix_strcoll_bug(void)
{
#if defined(NLS) && defined(HAVE_STRCOLL)
    /*
     * SunOS4 checks the file descriptor from openlocale() for <= 0
     * instead of == -1. Someone should tell sun that file descriptor 0
     * is valid! Our portable hack: open one so we call it with 0 used...
     * We have to call this routine every time the locale changes...
     *
     * Of course it also tries to free the constant locale "C" it initially
     * had allocated, with the sequence 
     * > setenv LANG "fr"
     * > ls^D
     * > unsetenv LANG
     * But we are smarter than that and just print a warning message.
     */
    int fd = -1;
    static char *root = "/";

    if (!didfds)
	fd = xopen(root, O_RDONLY|O_LARGEFILE);

    (void) strcoll(root, root);

    if (fd != -1)
	xclose(fd);
#endif
}
#endif /* STRCOLLBUG */


#ifdef OREO
#include <compat.h>
#endif /* OREO */

void
osinit(void)
{
#ifdef OREO
    set42sig();
    setcompat(getcompat() & ~COMPAT_EXEC);
    signal(SIGIO, SIG_IGN);		/* ignore SIGIO */
#endif /* OREO */

#ifdef aiws
    {
	struct sigstack inst;
	inst.ss_sp = xmalloc(4192) + 4192;
	inst.ss_onstack = 0;
	sigstack(&inst, NULL);
    }
#endif /* aiws */

#ifdef apollo
    (void) isapad();
#endif

#ifdef _SX
    /* 
     * kill(SIGCONT) problems, don't know what this syscall does
     * [schott@rzg.mpg.de]
     */
    syscall(151, getpid(), getpid());
#endif /* _SX */
}

#ifndef HAVE_STRERROR
extern int sys_nerr;
extern char *sys_errlist[];
char *
xstrerror(int i)
{
    if (i >= 0 && i < sys_nerr) {
	return sys_errlist[i];
    } else {
	static char *errbuf; /* = NULL; */

	xfree(errbuf);
	errbuf = xasprintf(CGETS(23, 13, "Unknown Error: %d"), i);
	return errbuf;
    }
}
#endif /* !HAVE_STRERROR */
    
#ifndef HAVE_GETHOSTNAME
# if !defined(_MINIX) && !defined(__EMX__) && !defined(WINNT_NATIVE)
#  include <sys/utsname.h>
# endif /* !_MINIX && !__EMX__ && !WINNT_NATIVE */

int
xgethostname(char *name, int namlen)
{
# if !defined(_MINIX) && !defined(__EMX__) && !defined(WINNT_NATIVE)
    int     i, retval;
    struct utsname uts;

    retval = uname(&uts);

#  ifdef DEBUG
    xprintf(CGETS(23, 14, "sysname:  %s\n"), uts.sysname);
    xprintf(CGETS(23, 15, "nodename: %s\n"), uts.nodename);
    xprintf(CGETS(23, 16, "release:  %s\n"), uts.release);
    xprintf(CGETS(23, 17, "version:  %s\n"), uts.version);
    xprintf(CGETS(23, 18, "machine:  %s\n"), uts.machine);
#  endif /* DEBUG */
    i = strlen(uts.nodename) + 1;
    (void) strncpy(name, uts.nodename, i < namlen ? i : namlen);

    return retval;
# else /* !_MINIX && !__EMX__ */
    if (namlen > 0) {
#  ifdef __EMX__
	(void) strncpy(name, "OS/2", namlen);
#  else /* _MINIX */
	(void) strncpy(name, "minix", namlen);
#  endif /* __EMX__ */
	name[namlen-1] = '\0';
    }
    return(0);
#endif /* _MINIX && !__EMX__ */
} /* end xgethostname */
#endif /* !HAVE_GETHOSTNAME */

#ifndef HAVE_NICE
# if defined(_MINIX) && defined(NICE)
#  undef _POSIX_SOURCE	/* redefined in <lib.h> */
#  undef _MINIX		/* redefined in <lib.h> */
#  undef HZ		/* redefined in <minix/const.h> */
#  include <lib.h>
# endif /* _MINIX && NICE */
int 
xnice(int incr)
{
#if defined(_MINIX) && defined(NICE)
    return callm1(MM, NICE, incr, 0, 0, NIL_PTR, NIL_PTR, NIL_PTR);
#else
    return /* incr ? 0 : */ 0;
#endif /* _MINIX && NICE */
} /* end xnice */
#endif /* !HAVE_NICE */

#ifndef HAVE_GETCWD
static char *strnrcpy (char *, char *, size_t);

/* xgetcwd():
 *	Return the pathname of the current directory, or return
 *	an error message in pathname.
 */

# ifdef hp9000s500
/*
 *  From: Bernd Mohr <mohr@faui77.informatik.uni-erlangen.de>
 *  I also ported the tcsh to the HP9000 Series 500. This computer
 *  is a little bit different than the other HP 9000 computer. It has
 *  a HP Chip instead of a Motorola CPU and it is no "real" UNIX. It runs
 *  HP-UX which is emulated in top of a HP operating system. So, the last
 *  supported version of HP-UX is 5.2 on the HP9000s500. This has two
 *  consequences: it supports no job control and it has a filesystem
 *  without "." and ".." !!!
 */
char *
xgetcwd(char *pathname, size_t pathlen)
{
    char pathbuf[MAXPATHLEN];	/* temporary pathname buffer */
    char *pnptr = &pathbuf[(sizeof pathbuf)-1]; /* pathname pointer */
    dev_t rdev;			/* root device number */
    DIR *dirp = NULL;		/* directory stream */
    ino_t rino;			/* root inode number */
    off_t rsize;		/* root size */
    struct direct *dir;		/* directory entry struct */
    struct stat d, dd;		/* file status struct */
    int serrno;

    *pnptr = '\0';
    (void) stat("/.", &d);
    rdev = d.st_dev;
    rino = d.st_ino;
    rsize = d.st_size;
    for (;;) {
	if (stat(".", &d) == -1) {
	    (void) xsnprintf(pathname, pathlen, CGETS(23, 24,
		"getcwd: Cannot stat \".\" (%s)"), strerror(errno));
	    goto fail;
	}
	if (d.st_ino == rino && d.st_dev == rdev && d.st_size == rsize)
	    break;		/* reached root directory */
	if ((dirp = opendir("..")) == NULL) {
	    (void) xsnprintf(pathname, pathlen, CGETS(23, 19,
		"getcwd: Cannot open \"..\" (%s)"), strerror(errno));
	    goto fail;
	}
	if (chdir("..") == -1) {
	    (void) xsnprintf(pathname, pathlen, CGETS(23, 20,
		"getcwd: Cannot chdir to \"..\" (%s)"), strerror(errno));
	    goto fail;
	}
	do {
	    if ((dir = readdir(dirp)) == NULL) {
		(void) xsnprintf(pathname, pathlen, 
		    CGETS(23, 21, "getcwd: Read error in \"..\" (%s)"),
		    strerror(errno));
		goto fail;
	    }
	    if (stat(dir->d_name, &dd) == -1) {
		(void) xsnprintf(pathname, pathlen,
		    CGETS(23, 25, "getcwd: Cannot stat directory \"%s\" (%s)"),
		    dir->d_name, strerror(errno));
		goto fail;
	    }
	} while (dd.st_ino  != d.st_ino  ||
		 dd.st_dev  != d.st_dev  ||
		 dd.st_size != d.st_size);
	closedir(dirp);
	dirp = NULL;
	pnptr = strnrcpy(dirp->d_name, pnptr, pnptr - pathbuf);
	pnptr = strnrcpy("/", pnptr, pnptr - pathbuf);
    }

    if (*pnptr == '\0')		/* current dir == root dir */
	(void) strncpy(pathname, "/", pathlen);
    else {
	(void) strncpy(pathname, pnptr, pathlen);
	pathname[pathlen - 1] = '\0';
	if (chdir(pnptr) == -1) {
	    (void) xsnprintf(pathname, MAXPATHLEN, CGETS(23, 22,
		    "getcwd: Cannot change back to \".\" (%s)"),
		    strerror(errno));
	    return NULL;
	}
    }
    return pathname;

fail:
    serrno = errno;
    (void) chdir(strnrcpy(".", pnptr, pnptr - pathbuf));
    errno = serrno;
    return NULL;
}

# else /* ! hp9000s500 */


char *
xgetcwd(char *pathname, size_t pathlen)
{
    DIR    *dp;
    struct dirent *d;

    struct stat st_root, st_cur, st_next, st_dotdot;
    char    pathbuf[MAXPATHLEN], nextpathbuf[MAXPATHLEN * 2];
    char   *pathptr, *nextpathptr, *cur_name_add;
    int	   save_errno = 0;

    /* find the inode of root */
    if (stat("/", &st_root) == -1) {
	(void) xsnprintf(pathname, pathlen, CGETS(23, 23, 
			"getcwd: Cannot stat \"/\" (%s)"),
			strerror(errno));
	return NULL;
    }
    pathbuf[MAXPATHLEN - 1] = '\0';
    pathptr = &pathbuf[MAXPATHLEN - 1];
    nextpathbuf[MAXPATHLEN - 1] = '\0';
    cur_name_add = nextpathptr = &nextpathbuf[MAXPATHLEN - 1];

    /* find the inode of the current directory */
    if (lstat(".", &st_cur) == -1) {
	(void) xsnprintf(pathname, pathlen, CGETS(23, 24,
			 "getcwd: Cannot stat \".\" (%s)"),
			 strerror(errno));
	return NULL;
    }
    nextpathptr = strnrcpy(nextpathptr, "../", nextpathptr - nextpathbuf);

    /* Descend to root */
    for (;;) {

	/* look if we found root yet */
	if (st_cur.st_ino == st_root.st_ino &&
	    DEV_DEV_COMPARE(st_cur.st_dev, st_root.st_dev)) {
	    (void) strncpy(pathname, *pathptr != '/' ? "/" : pathptr, pathlen);
	    pathname[pathlen - 1] = '\0';
	    return pathname;
	}

	/* open the parent directory */
	if (stat(nextpathptr, &st_dotdot) == -1) {
	    (void) xsnprintf(pathname, pathlen, CGETS(23, 25,
			     "getcwd: Cannot stat directory \"%s\" (%s)"),
			     nextpathptr, strerror(errno));
	    return NULL;
	}
	if ((dp = opendir(nextpathptr)) == NULL) {
	    (void) xsnprintf(pathname, pathlen, CGETS(23, 26,
			     "getcwd: Cannot open directory \"%s\" (%s)"),
			     nextpathptr, strerror(errno));
	    return NULL;
	}

	/* look in the parent for the entry with the same inode */
	if (DEV_DEV_COMPARE(st_dotdot.st_dev, st_cur.st_dev)) {
	    /* Parent has same device. No need to stat every member */
	    for (d = readdir(dp); d != NULL; d = readdir(dp)) {
#ifdef __clipper__
		if (((unsigned long)d->d_ino & 0xffff) == st_cur.st_ino)
		    break;
#else
		if (d->d_ino == st_cur.st_ino)
		    break;
#endif
	    }
	}
	else {
	    /* 
	     * Parent has a different device. This is a mount point so we 
	     * need to stat every member 
	     */
	    for (d = readdir(dp); d != NULL; d = readdir(dp)) {
		if (ISDOT(d->d_name) || ISDOTDOT(d->d_name))
		    continue;
		(void)strncpy(cur_name_add, d->d_name,
		    (size_t) (&nextpathbuf[sizeof(nextpathbuf) - 1] - cur_name_add));
		if (lstat(nextpathptr, &st_next) == -1) {
		    /*
		     * We might not be able to stat() some path components
		     * if we are using afs, but this is not an error as
		     * long as we find the one we need; we also save the
		     * first error to report it if we don't finally succeed.
		     */
		    if (save_errno == 0)
			save_errno = errno;
		    continue;
		}
		/* check if we found it yet */
		if (st_next.st_ino == st_cur.st_ino &&
		    DEV_DEV_COMPARE(st_next.st_dev, st_cur.st_dev)) 
		    break;
	    }
	}
	if (d == NULL) {
	    (void) xsnprintf(pathname, pathlen, CGETS(23, 27,
			     "getcwd: Cannot find \".\" in \"..\" (%s)"),
			     strerror(save_errno ? save_errno : ENOENT));
	    closedir(dp);
	    return NULL;
	}
	else
	    save_errno = 0;
	st_cur = st_dotdot;
	pathptr = strnrcpy(pathptr, d->d_name, pathptr - pathbuf);
	pathptr = strnrcpy(pathptr, "/", pathptr - pathbuf);
	nextpathptr = strnrcpy(nextpathptr, "../", nextpathptr - nextpathbuf);
	*cur_name_add = '\0';
	closedir(dp);
    }
} /* end getcwd */
# endif /* hp9000s500 */

/* strnrcpy():
 *	Like strncpy, going backwards and returning the new pointer
 */
static char *
strnrcpy(char *ptr, char *str, size_t siz)
{
    int len = strlen(str);
    if (siz == 0)
	return ptr;

    while (len && siz--)
	*--ptr = str[--len];

    return (ptr);
} /* end strnrcpy */
#endif /* !HAVE_GETCWD */

#ifdef apollo
/***
 *** Domain/OS
 ***/
#include <apollo/base.h>
#include <apollo/loader.h>
#include <apollo/error.h>


static char *
apperr(status_$t *st)
{
    static char *buf; /* = NULL */
    short e_subl, e_modl, e_codel;
    error_$string_t e_sub, e_mod, e_code;

    error_$get_text(*st, e_sub, &e_subl, e_mod, &e_modl, e_code, &e_codel);
    e_sub[e_subl] = '\0';
    e_code[e_codel] = '\0';
    e_mod[e_modl] = '\0';
    xfree(buf);
    buf = xasprintf("%s (%s/%s)", e_code, e_sub, e_mod);

    return(buf);
}

static int
llib(Char *s)
{
    short len = Strlen(s);
    status_$t st;
    char *t;

    loader_$inlib(t = short2str(s), len, &st);
    if (st.all != status_$ok) 
	stderror(ERR_SYSTEM, t, apperr(&st));
}

/*ARGSUSED*/
void
doinlib(Char **v, struct command *c)
{
    Char **globbed;

    setname(short2str(*v++));
    v = glob_all_or_error(v);
    globbed = v;
    cleanup_push(globbed, blk_cleanup);

    while (v && *v) 
	llib(*v++);
    cleanup_until(globbed);
}

int
getv(Char *v)
{
    if (eq(v, STRbsd43))
	return(1);
    else if (eq(v, STRsys53))
	return(0);
    else 
	stderror(ERR_NAME | ERR_SYSTEM, short2str(v),
		 CGETS(23, 28, "Invalid system type"));
    /*NOTREACHED*/
    return(0);
}

/*ARGSUSED*/
void
dover(Char **v, struct command *c)
{
    Char *p;

    setname(short2str(*v++));
    if (!*v) {
	if (!(p = tgetenv(STRSYSTYPE)))
	    stderror(ERR_NAME | ERR_STRING,
		     CGETS(23, 29, "System type is not set"));
	xprintf("%S\n", p);
    }
    else {
	tsetenv(STRSYSTYPE, getv(*v) ? STRbsd43 : STRsys53);
	dohash(NULL, NULL);
    }
}

/*
 * Many thanks to rees@citi.umich.edu (Jim Rees) and
 *                mathys@ssdt-tempe.sps.mot.com (Yves Mathys)
 * For figuring out how to do this... I could have never done
 * it without their help.
 */
typedef short enum {
	name_$wdir_type,
	name_$ndir_type,
	name_$node_dir_type,
} name_$dir_type_t;

/*ARGSUSED*/
void
dorootnode(Char **v, struct command *c)
{
    name_$dir_type_t dirtype = name_$node_dir_type;
    uid_$t uid;
    status_$t st;
    char *name;
    short namelen;

    setname(short2str(*v++));

    name = short2str(*v);
    namelen = strlen(name);

    name_$resolve(name, &namelen, &uid, &st);
    if (st.all != status_$ok) 
	stderror(ERR_SYSTEM, name, apperr(&st));
    namelen = 0;
    name_$set_diru(&uid, "", &namelen, &dirtype, &st);
    if (st.all != status_$ok) 
	stderror(ERR_SYSTEM, name, apperr(&st));
    dohash(NULL, NULL);
}

int
isapad(void)
{
    static int res = -1;
    static status_$t st;

    if (res == -1) {
	int strm;
	if (isatty(0))
	    strm = 0;
	if (isatty(1))
	    strm = 1;
	if (isatty(2))
	    strm = 2;
	else {
	    res = 0;
	    st.all = status_$ok;
	    return(res);
	}
	res = stream_$isavt(&strm, &st);
	res = res ? 1 : 0;
    }
    else {
	if (st.all != status_$ok) 
	    stderror(ERR_SYSTEM, "stream_$isavt", apperr(&st));
    }
    return(res);
}
#endif

#if defined(__CYGWIN__) && !defined(NO_CRYPT)
#undef CHAR		/* Collides with Win32 API */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <sys/cygwin.h>
char *
cygwin_xcrypt(struct passwd *pw, const char *password, const char *expected_pwd)
{
    static char invalid_password[] = "\377";
    HANDLE token = cygwin_logon_user(pw, password);
    if (token == INVALID_HANDLE_VALUE)
	return invalid_password;
    CloseHandle(token);
    return (char *) expected_pwd;
}
#endif /* __CYGWIN__ && !NO_CRYPT */
