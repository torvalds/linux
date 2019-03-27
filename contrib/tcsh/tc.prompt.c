/* $Header: /p/tcsh/cvsroot/tcsh/tc.prompt.c,v 3.71 2014/08/23 09:07:57 christos Exp $ */
/*
 * tc.prompt.c: Prompt printing stuff
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

RCSID("$tcsh: tc.prompt.c,v 3.71 2014/08/23 09:07:57 christos Exp $")

#include "ed.h"
#include "tw.h"

/*
 * kfk 21oct1983 -- add @ (time) and / ($cwd) in prompt.
 * PWP 4/27/87 -- rearange for tcsh.
 * mrdch@com.tau.edu.il 6/26/89 - added ~, T and .# - rearanged to switch()
 *                 instead of if/elseif
 * Luke Mewburn, <lukem@cs.rmit.edu.au>
 *	6-Sep-91	changed date format
 *	16-Feb-94	rewrote directory prompt code, added $ellipsis
 *	29-Dec-96	added rprompt support
 */

static const char   *month_list[12];
static const char   *day_list[7];

void
dateinit(void)
{
#ifdef notyet
  int i;

  setlocale(LC_TIME, "");

  for (i = 0; i < 12; i++)
      xfree((ptr_t) month_list[i]);
  month_list[0] = strsave(_time_info->abbrev_month[0]);
  month_list[1] = strsave(_time_info->abbrev_month[1]);
  month_list[2] = strsave(_time_info->abbrev_month[2]);
  month_list[3] = strsave(_time_info->abbrev_month[3]);
  month_list[4] = strsave(_time_info->abbrev_month[4]);
  month_list[5] = strsave(_time_info->abbrev_month[5]);
  month_list[6] = strsave(_time_info->abbrev_month[6]);
  month_list[7] = strsave(_time_info->abbrev_month[7]);
  month_list[8] = strsave(_time_info->abbrev_month[8]);
  month_list[9] = strsave(_time_info->abbrev_month[9]);
  month_list[10] = strsave(_time_info->abbrev_month[10]);
  month_list[11] = strsave(_time_info->abbrev_month[11]);

  for (i = 0; i < 7; i++)
      xfree((ptr_t) day_list[i]);
  day_list[0] = strsave(_time_info->abbrev_wkday[0]);
  day_list[1] = strsave(_time_info->abbrev_wkday[1]);
  day_list[2] = strsave(_time_info->abbrev_wkday[2]);
  day_list[3] = strsave(_time_info->abbrev_wkday[3]);
  day_list[4] = strsave(_time_info->abbrev_wkday[4]);
  day_list[5] = strsave(_time_info->abbrev_wkday[5]);
  day_list[6] = strsave(_time_info->abbrev_wkday[6]);
#else
  month_list[0] = "Jan";
  month_list[1] = "Feb";
  month_list[2] = "Mar";
  month_list[3] = "Apr";
  month_list[4] = "May";
  month_list[5] = "Jun";
  month_list[6] = "Jul";
  month_list[7] = "Aug";
  month_list[8] = "Sep";
  month_list[9] = "Oct";
  month_list[10] = "Nov";
  month_list[11] = "Dec";

  day_list[0] = "Sun";
  day_list[1] = "Mon";
  day_list[2] = "Tue";
  day_list[3] = "Wed";
  day_list[4] = "Thu";
  day_list[5] = "Fri";
  day_list[6] = "Sat";
#endif
}

void
printprompt(int promptno, const char *str)
{
    static  const Char *ocp = NULL;
    static  const char *ostr = NULL;
    time_t  lclock = time(NULL);
    const Char *cp;

    switch (promptno) {
    default:
    case 0:
	cp = varval(STRprompt);
	break;
    case 1:
	cp = varval(STRprompt2);
	break;
    case 2:
	cp = varval(STRprompt3);
	break;
    case 3:
	if (ocp != NULL) {
	    cp = ocp;
	    str = ostr;
	}
	else 
	    cp = varval(STRprompt);
	break;
    }

    if (promptno < 2) {
	ocp = cp;
	ostr = str;
    }

    xfree(Prompt);
    Prompt = NULL;
    Prompt = tprintf(FMT_PROMPT, cp, str, lclock, NULL);
    if (!editing) {
	for (cp = Prompt; *cp ; )
	    (void) putwraw(*cp++);
	SetAttributes(0);
	flush();
    }

    xfree(RPrompt);
    RPrompt = NULL;
    if (promptno == 0) {	/* determine rprompt if using main prompt */
	cp = varval(STRrprompt);
	RPrompt = tprintf(FMT_PROMPT, cp, NULL, lclock, NULL);
				/* if not editing, put rprompt after prompt */
	if (!editing && RPrompt[0] != '\0') {
	    for (cp = RPrompt; *cp ; )
		(void) putwraw(*cp++);
	    SetAttributes(0);
	    putraw(' ');
	    flush();
	}
    }
}

static void
tprintf_append_mbs(struct Strbuf *buf, const char *mbs, Char attributes)
{
    while (*mbs != 0) {
	Char wc;

	mbs += one_mbtowc(&wc, mbs, MB_LEN_MAX);
	Strbuf_append1(buf, wc | attributes);
    }
}

Char *
tprintf(int what, const Char *fmt, const char *str, time_t tim, ptr_t info)
{
    struct Strbuf buf = Strbuf_INIT;
    Char   *z, *q;
    Char    attributes = 0;
    static int print_prompt_did_ding = 0;
    char *cz;

    Char *p;
    const Char *cp = fmt;
    Char Scp;
    struct tm *t = localtime(&tim);

			/* prompt stuff */
    static Char *olduser = NULL;
    int updirs;
    size_t pdirs;

    cleanup_push(&buf, Strbuf_cleanup);
    for (; *cp; cp++) {
	if ((*cp == '%') && ! (cp[1] == '\0')) {
	    cp++;
	    switch (*cp) {
	    case 'R':
		if (what == FMT_HISTORY) {
		    cz = fmthist('R', info);
		    tprintf_append_mbs(&buf, cz, attributes);
		    xfree(cz);
		} else {
		    if (str != NULL)
			tprintf_append_mbs(&buf, str, attributes);
		}
		break;
	    case '#':
#ifdef __CYGWIN__
		/* Check for being member of the Administrators group */
		{
			gid_t grps[NGROUPS_MAX];
			int grp, gcnt;

			gcnt = getgroups(NGROUPS_MAX, grps);
# define DOMAIN_GROUP_RID_ADMINS 544
			for (grp = 0; grp < gcnt; ++grp)
				if (grps[grp] == DOMAIN_GROUP_RID_ADMINS)
					break;
			Scp = (grp < gcnt) ? PRCHROOT : PRCH;
		}
#else
		Scp = (uid == 0 || euid == 0) ? PRCHROOT : PRCH;
#endif
		if (Scp != '\0')
		    Strbuf_append1(&buf, attributes | Scp);
		break;
	    case '!':
	    case 'h':
		switch (what) {
		case FMT_HISTORY:
		    cz = fmthist('h', info);
		    break;
		case FMT_SCHED:
		    cz = xasprintf("%d", *(int *)info);
		    break;
		default:
		    cz = xasprintf("%d", eventno + 1);
		    break;
		}
		tprintf_append_mbs(&buf, cz, attributes);
		xfree(cz);
		break;
	    case 'T':		/* 24 hour format	 */
	    case '@':
	    case 't':		/* 12 hour am/pm format */
	    case 'p':		/* With seconds	*/
	    case 'P':
		{
		    char    ampm = 'a';
		    int     hr = t->tm_hour;

		    /* addition by Hans J. Albertsson */
		    /* and another adapted from Justin Bur */
		    if (adrof(STRampm) || (*cp != 'T' && *cp != 'P')) {
			if (hr >= 12) {
			    if (hr > 12)
				hr -= 12;
			    ampm = 'p';
			}
			else if (hr == 0)
			    hr = 12;
		    }		/* else do a 24 hour clock */

		    /* "DING!" stuff by Hans also */
		    if (t->tm_min || print_prompt_did_ding || 
			what != FMT_PROMPT || adrof(STRnoding)) {
			if (t->tm_min)
			    print_prompt_did_ding = 0;
			/*
			 * Pad hour to 2 characters if padhour is set,
			 * by ADAM David Alan Martin
			 */
			p = Itoa(hr, adrof(STRpadhour) ? 2 : 0, attributes);
			Strbuf_append(&buf, p);
			xfree(p);
			Strbuf_append1(&buf, attributes | ':');
			p = Itoa(t->tm_min, 2, attributes);
			Strbuf_append(&buf, p);
			xfree(p);
			if (*cp == 'p' || *cp == 'P') {
			    Strbuf_append1(&buf, attributes | ':');
			    p = Itoa(t->tm_sec, 2, attributes);
			    Strbuf_append(&buf, p);
			    xfree(p);
			}
			if (adrof(STRampm) || (*cp != 'T' && *cp != 'P')) {
			    Strbuf_append1(&buf, attributes | ampm);
			    Strbuf_append1(&buf, attributes | 'm');
			}
		    }
		    else {	/* we need to ding */
			size_t i;

			for (i = 0; STRDING[i] != 0; i++)
			    Strbuf_append1(&buf, attributes | STRDING[i]);
			print_prompt_did_ding = 1;
		    }
		}
		break;

	    case 'M':
#ifndef HAVENOUTMP
		if (what == FMT_WHO)
		    cz = who_info(info, 'M');
		else 
#endif /* HAVENOUTMP */
		    cz = getenv("HOST");
		/*
		 * Bug pointed out by Laurent Dami <dami@cui.unige.ch>: don't
		 * derefrence that NULL (if HOST is not set)...
		 */
		if (cz != NULL)
		    tprintf_append_mbs(&buf, cz, attributes);
		if (what == FMT_WHO)
		    xfree(cz);
		break;

	    case 'm': {
		char *scz = NULL;
#ifndef HAVENOUTMP
		if (what == FMT_WHO)
		    scz = cz = who_info(info, 'm');
		else
#endif /* HAVENOUTMP */
		    cz = getenv("HOST");

		if (cz != NULL)
		    while (*cz != 0 && (what == FMT_WHO || *cz != '.')) {
			Char wc;

			cz += one_mbtowc(&wc, cz, MB_LEN_MAX);
			Strbuf_append1(&buf, wc | attributes);
		    }
		if (scz)
		    xfree(scz);
		break;
	    }

			/* lukem: new directory prompt code */
	    case '~':
	    case '/':
	    case '.':
	    case 'c':
	    case 'C':
		Scp = *cp;
		if (Scp == 'c')		/* store format type (c == .) */
		    Scp = '.';
		if ((z = varval(STRcwd)) == STRNULL)
		    break;		/* no cwd, so don't do anything */

			/* show ~ whenever possible - a la dirs */
		if (Scp == '~' || Scp == '.' ) {
		    static Char *olddir = NULL;

		    if (tlength == 0 || olddir != z) {
			olddir = z;		/* have we changed dir? */
			olduser = getusername(&olddir);
		    }
		    if (olduser)
			z = olddir;
		}
		updirs = pdirs = 0;

			/* option to determine fixed # of dirs from path */
		if (Scp == '.' || Scp == 'C') {
		    int skip;
#ifdef WINNT_NATIVE
		    Char *oldz = z;
		    if (z[1] == ':') {
			Strbuf_append1(&buf, attributes | *z++);
			Strbuf_append1(&buf, attributes | *z++);
		    }
		    if (*z == '/' && z[1] == '/') {
			Strbuf_append1(&buf, attributes | *z++);
			Strbuf_append1(&buf, attributes | *z++);
			do {
			    Strbuf_append1(&buf, attributes | *z++);
			} while(*z != '/');
		    }
#endif /* WINNT_NATIVE */
		    q = z;
		    while (*z)				/* calc # of /'s */
			if (*z++ == '/')
			    updirs++;

#ifdef WINNT_NATIVE
		    /*
		     * for format type c, prompt will be following...
		     * c:/path                => c:/path
		     * c:/path/to             => c:to
		     * //machine/share        => //machine/share
		     * //machine/share/folder => //machine:folder
		     */
		    if (oldz[0] == '/' && oldz[1] == '/' && updirs > 1)
			Strbuf_append1(&buf, attributes | ':');
#endif /* WINNT_NATIVE */
		    if ((Scp == 'C' && *q != '/'))
			updirs++;

		    if (cp[1] == '0') {			/* print <x> or ...  */
			pdirs = 1;
			cp++;
		    }
		    if (cp[1] >= '1' && cp[1] <= '9') {	/* calc # to skip  */
			skip = cp[1] - '0';
			cp++;
		    }
		    else
			skip = 1;

		    updirs -= skip;
		    while (skip-- > 0) {
			while ((z > q) && (*z != '/'))
			    z--;			/* back up */
			if (skip && z > q)
			    z--;
		    }
		    if (*z == '/' && z != q)
			z++;
		} /* . || C */

							/* print ~[user] */
		if ((olduser) && ((Scp == '~') ||
		     (Scp == '.' && (pdirs || (!pdirs && updirs <= 0))) )) {
		    Strbuf_append1(&buf, attributes | '~');
		    for (q = olduser; *q; q++)
			Strbuf_append1(&buf, attributes | *q);
		}

			/* RWM - tell you how many dirs we've ignored */
			/*       and add '/' at front of this         */
		if (updirs > 0 && pdirs) {
		    if (adrof(STRellipsis)) {
			Strbuf_append1(&buf, attributes | '.');
			Strbuf_append1(&buf, attributes | '.');
			Strbuf_append1(&buf, attributes | '.');
		    } else {
			Strbuf_append1(&buf, attributes | '/');
			Strbuf_append1(&buf, attributes | '<');
			if (updirs > 9) {
			    Strbuf_append1(&buf, attributes | '9');
			    Strbuf_append1(&buf, attributes | '+');
			} else
			    Strbuf_append1(&buf, attributes | ('0' + updirs));
			Strbuf_append1(&buf, attributes | '>');
		    }
		}

		while (*z)
		    Strbuf_append1(&buf, attributes | *z++);
		break;
			/* lukem: end of new directory prompt code */

	    case 'n':
#ifndef HAVENOUTMP
		if (what == FMT_WHO) {
		    cz = who_info(info, 'n');
		    tprintf_append_mbs(&buf, cz, attributes);
		    xfree(cz);
		}
		else  
#endif /* HAVENOUTMP */
		{
		    if ((z = varval(STRuser)) != STRNULL)
			while (*z)
			    Strbuf_append1(&buf, attributes | *z++);
		}
		break;
	    case 'N':
		if ((z = varval(STReuser)) != STRNULL)
		    while (*z)
			Strbuf_append1(&buf, attributes | *z++);
		break;
	    case 'l':
#ifndef HAVENOUTMP
		if (what == FMT_WHO) {
		    cz = who_info(info, 'l');
		    tprintf_append_mbs(&buf, cz, attributes);
		    xfree(cz);
		}
		else  
#endif /* HAVENOUTMP */
		{
		    if ((z = varval(STRtty)) != STRNULL)
			while (*z)
			    Strbuf_append1(&buf, attributes | *z++);
		}
		break;
	    case 'd':
		tprintf_append_mbs(&buf, day_list[t->tm_wday], attributes);
		break;
	    case 'D':
		p = Itoa(t->tm_mday, 2, attributes);
		Strbuf_append(&buf, p);
		xfree(p);
		break;
	    case 'w':
		tprintf_append_mbs(&buf, month_list[t->tm_mon], attributes);
		break;
	    case 'W':
		p = Itoa(t->tm_mon + 1, 2, attributes);
		Strbuf_append(&buf, p);
		xfree(p);
		break;
	    case 'y':
		p = Itoa(t->tm_year % 100, 2, attributes);
		Strbuf_append(&buf, p);
		xfree(p);
		break;
	    case 'Y':
		p = Itoa(t->tm_year + 1900, 4, attributes);
		Strbuf_append(&buf, p);
		xfree(p);
		break;
	    case 'S':		/* start standout */
		attributes |= STANDOUT;
		break;
	    case 'B':		/* start bold */
		attributes |= BOLD;
		break;
	    case 'U':		/* start underline */
		attributes |= UNDER;
		break;
	    case 's':		/* end standout */
		attributes &= ~STANDOUT;
		break;
	    case 'b':		/* end bold */
		attributes &= ~BOLD;
		break;
	    case 'u':		/* end underline */
		attributes &= ~UNDER;
		break;
	    case 'L':
		ClearToBottom();
		break;

	    case 'j':
		{
		    int njobs = -1;
		    struct process *pp;

		    for (pp = proclist.p_next; pp; pp = pp->p_next)
			njobs++;
		    if (njobs == -1)
			njobs++;
		    p = Itoa(njobs, 1, attributes);
		    Strbuf_append(&buf, p);
		    xfree(p);
		    break;
		}
	    case '?':
		if ((z = varval(STRstatus)) != STRNULL)
		    while (*z)
			Strbuf_append1(&buf, attributes | *z++);
		break;
	    case '$':
		expdollar(&buf, &cp, attributes);
		/* cp should point the last char of current % sequence */
		cp--;
		break;
	    case '%':
		Strbuf_append1(&buf, attributes | '%');
		break;
	    case '{':		/* literal characters start */
#if LITERAL == 0
		/*
		 * No literal capability, so skip all chars in the literal
		 * string
		 */
		while (*cp != '\0' && (cp[-1] != '%' || *cp != '}'))
		    cp++;
#endif				/* LITERAL == 0 */
		attributes |= LITERAL;
		break;
	    case '}':		/* literal characters end */
		attributes &= ~LITERAL;
		break;
	    default:
#ifndef HAVENOUTMP
		if (*cp == 'a' && what == FMT_WHO) {
		    cz = who_info(info, 'a');
		    tprintf_append_mbs(&buf, cz, attributes);
		    xfree(cz);
		}
		else
#endif /* HAVENOUTMP */
		{
		    Strbuf_append1(&buf, attributes | '%');
		    Strbuf_append1(&buf, attributes | *cp);
		}
		break;
	    }
	}
	else if (*cp == '\\' || *cp == '^')
	    Strbuf_append1(&buf, attributes | parseescape(&cp));
	else if (*cp == HIST) {	/* EGS: handle '!'s in prompts */
	    if (what == FMT_HISTORY)
		cz = fmthist('h', info);
	    else
		cz = xasprintf("%d", eventno + 1);
	    tprintf_append_mbs(&buf, cz, attributes);
	    xfree(cz);
	}
	else
	    Strbuf_append1(&buf, attributes | *cp); /* normal character */
    }
    cleanup_ignore(&buf);
    cleanup_until(&buf);
    return Strbuf_finish(&buf);
}

int
expdollar(struct Strbuf *buf, const Char **srcp, Char attr)
{
    struct varent *vp;
    const Char *src = *srcp;
    Char *var, *val;
    size_t i;
    int curly = 0;

    /* found a variable, expand it */
    var = xmalloc((Strlen(src) + 1) * sizeof (*var));
    for (i = 0; ; i++) {
	var[i] = *++src & TRIM;
	if (i == 0 && var[i] == '{') {
	    curly = 1;
	    var[i] = *++src & TRIM;
	}
	if (!alnum(var[i]) && var[i] != '_') {

	    var[i] = '\0';
	    break;
	}
    }
    if (curly && (*src & TRIM) == '}')
	src++;

    vp = adrof(var);
    if (vp && vp->vec) {
	for (i = 0; vp->vec[i] != NULL; i++) {
	    for (val = vp->vec[i]; *val; val++)
		if (*val != '\n' && *val != '\r')
		    Strbuf_append1(buf, *val | attr);
	    if (vp->vec[i+1])
		Strbuf_append1(buf, ' ' | attr);
	}
    }
    else {
	val = (!vp) ? tgetenv(var) : NULL;
	if (val) {
	    for (; *val; val++)
		if (*val != '\n' && *val != '\r')
		    Strbuf_append1(buf, *val | attr);
	} else {
	    *srcp = src;
	    xfree(var);
	    return 0;
	}
    }

    *srcp = src;
    xfree(var);
    return 1;
}
