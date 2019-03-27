/* $Header: /p/tcsh/cvsroot/tcsh/sh.c,v 3.189 2016/09/12 16:33:54 christos Exp $ */
/*
 * sh.c: Main shell routines
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
#define EXTERN	/* Intern */
#include "sh.h"

#ifndef lint
char    copyright[] =
"@(#) Copyright (c) 1991 The Regents of the University of California.\n\
 All rights reserved.\n";
#endif /* not lint */

RCSID("$tcsh: sh.c,v 3.189 2016/09/12 16:33:54 christos Exp $")

#include "tc.h"
#include "ed.h"
#include "tw.h"

extern int MapsAreInited;
extern int NLSMapsAreInited;

/*
 * C Shell
 *
 * Bill Joy, UC Berkeley, California, USA
 * October 1978, May 1980
 *
 * Jim Kulp, IIASA, Laxenburg, Austria
 * April 1980
 *
 * Filename recognition added:
 * Ken Greer, Ind. Consultant, Palo Alto CA
 * October 1983.
 *
 * Karl Kleinpaste, Computer Consoles, Inc.
 * Added precmd, periodic/tperiod, prompt changes,
 * directory stack hack, and login watch.
 * Sometime March 1983 - Feb 1984.
 *
 * Added scheduled commands, including the "sched" command,
 * plus the call to sched_run near the precmd et al
 * routines.
 * Upgraded scheduled events for running events while
 * sitting idle at command input.
 *
 * Paul Placeway, Ohio State
 * added stuff for running with twenex/inputl  9 Oct 1984.
 *
 * ported to Apple Unix (TM) (OREO)  26 -- 29 Jun 1987
 */

jmp_buf_t reslab IZERO_STRUCT;
struct wordent paraml IZERO_STRUCT;

static const char tcshstr[] = "tcsh";

struct sigaction parintr;	/* Parents interrupt catch */
struct sigaction parterm;	/* Parents terminate catch */

#ifdef TESLA
int do_logout = 0;
#endif /* TESLA */


int    use_fork = 0;		/* use fork() instead of vfork()? */

/*
 * Magic pointer values. Used to specify other invalid conditions aside
 * from null.
 */
static Char	INVCHAR;
Char    *INVPTR = &INVCHAR;
Char    **INVPPTR = &INVPTR;

static int    fast = 0;
static int    mflag = 0;
static int    prompt = 1;
int     enterhist = 0;
int    tellwhat = 0;
time_t  t_period;
Char  *ffile = NULL;
int	dolzero = 0;
int	insource = 0;
int	exitset = 0;
static time_t  chktim;		/* Time mail last checked */
char *progname;
int tcsh;

/*
 * This preserves the input state of the shell. It is used by
 * st_save and st_restore to manupulate shell state.
 */
struct saved_state {
    int		  insource;
    int		  OLDSTD;
    int		  SHIN;
    int		  SHOUT;
    int		  SHDIAG;
    int		  intty;
    struct whyle *whyles;
    Char 	 *gointr;
    Char 	 *arginp;
    Char	 *evalp;
    Char	**evalvec;
    Char	 *alvecp;
    Char	**alvec;
    int		  onelflg;
    int	  enterhist;
    Char	**argv;
    Char	**av;
    Char	  HIST;
    int	  cantell;
    struct Bin	  B;
    int		  justpr;
};

static	int		  srccat	(Char *, Char *);
#ifndef WINNT_NATIVE
static	int		  srcfile	(const char *, int, int, Char **);
#else
int		  srcfile	(const char *, int, int, Char **);
#endif /*WINNT_NATIVE*/
static	void		  srcunit	(int, int, int, Char **);
static	void		  mailchk	(void);
#ifndef _PATH_DEFPATH
static	Char	 	**defaultpath	(void);
#endif
static	void		  record	(void);
static	void		  st_save	(struct saved_state *, int, int,
					 Char **, Char **);
static	void		  st_restore	(void *);

	int		  main		(int, char **);

#ifndef LOCALEDIR
#define LOCALEDIR "/usr/share/locale"
#endif

#ifdef NLS_CATALOGS
static void
add_localedir_to_nlspath(const char *path)
{
    static const char msgs_LOC[] = "/%L/LC_MESSAGES/%N.cat";
    static const char msgs_lang[] = "/%l/LC_MESSAGES/%N.cat";
    char *old;
    char *new, *new_p;
    size_t len;
    int add_LOC = 1;
    int add_lang = 1;
    char trypath[MAXPATHLEN];
    struct stat st;

    if (path == NULL)
        return;

    (void) xsnprintf(trypath, sizeof(trypath), "%s/en/LC_MESSAGES/tcsh.cat",
	path);
    if (stat(trypath, &st) == -1)
	return;

    if ((old = getenv("NLSPATH")) != NULL)
        len = strlen(old) + 1;	/* don't forget the colon. */
    else
	len = 0;

    len += 2 * strlen(path) +
	   sizeof(msgs_LOC) + sizeof(msgs_lang); /* includes the extra colon */

    new = new_p = xcalloc(len, 1);

    if (old != NULL) {
	size_t pathlen = strlen(path);
	char *old_p;

	(void) xsnprintf(new_p, len, "%s", old);
	new_p += strlen(new_p);
	len -= new_p - new;

	/* Check if the paths we try to add are already present in NLSPATH.
	   If so, note it by setting the appropriate flag to 0. */
	for (old_p = old; old_p; old_p = strchr(old_p, ':'),
				 old_p = old_p ? old_p + 1 : NULL) {
	    if (strncmp(old_p, path, pathlen) != 0)
	    	continue;
	    if (strncmp(old_p + pathlen, msgs_LOC, sizeof(msgs_LOC) - 1) == 0)
		add_LOC = 0;
	    else if (strncmp(old_p + pathlen, msgs_lang,
			      sizeof(msgs_lang) - 1) == 0)
		add_lang = 0;
	}
    }

    /* Add the message catalog paths not already present to NLSPATH. */
    if (add_LOC || add_lang)
	(void) xsnprintf(new_p, len, "%s%s%s%s%s%s",
			 old ? ":" : "",
			 add_LOC ? path : "", add_LOC ? msgs_LOC : "",
			 add_LOC && add_lang ? ":" : "",
			 add_lang ? path : "", add_lang ? msgs_lang : "");

    tsetenv(STRNLSPATH, str2short(new));
    free(new);
}
#endif

int
main(int argc, char **argv)
{
    int batch = 0;
    volatile int nexececho = 0;
    int nofile = 0;
    volatile int nverbose = 0;
    volatile int rdirs = 0;
    int quitit = 0;
    Char *cp;
#ifdef AUTOLOGOUT
    Char *cp2;
#endif
    char *tcp, *ttyn;
    int f, reenter;
    char **tempv;
    const char *targinp = NULL;
    int osetintr;
    struct sigaction oparintr;

#ifdef WINNT_NATIVE
    nt_init();
#endif /* WINNT_NATIVE */

    (void)memset(&reslab, 0, sizeof(reslab));
#if defined(NLS_CATALOGS) && defined(LC_MESSAGES)
    (void) setlocale(LC_MESSAGES, "");
#endif /* NLS_CATALOGS && LC_MESSAGES */

#ifdef NLS
# ifdef LC_CTYPE
    (void) setlocale(LC_CTYPE, ""); /* for iscntrl */
# endif /* LC_CTYPE */
#endif /* NLS */

    STR_environ = blk2short(environ);
    environ = short2blk(STR_environ);	/* So that we can free it */

#ifdef NLS_CATALOGS
    add_localedir_to_nlspath(LOCALEDIR);
#endif

    nlsinit();
    initlex(&paraml);

#ifdef MALLOC_TRACE
    mal_setstatsfile(fdopen(dmove(xopen("/tmp/tcsh.trace", 
	O_WRONLY|O_CREAT|O_LARGEFILE, 0666), 25), "w"));
    mal_trace(1);
#endif /* MALLOC_TRACE */

#if !(defined(BSDTIMES) || defined(_SEQUENT_)) && defined(POSIX)
# ifdef _SC_CLK_TCK
    clk_tck = (clock_t) sysconf(_SC_CLK_TCK);
# else /* ! _SC_CLK_TCK */
#  ifdef CLK_TCK
    clk_tck = CLK_TCK;
#  else /* !CLK_TCK */
    clk_tck = HZ;
#  endif /* CLK_TCK */
# endif /* _SC_CLK_TCK */
#endif /* !BSDTIMES && POSIX */

    settimes();			/* Immed. estab. timing base */
#ifdef TESLA
    do_logout = 0;
#endif /* TESLA */

    /*
     * Make sure we have 0, 1, 2 open
     * Otherwise `` jobs will not work... (From knaff@poly.polytechnique.fr)
     */
    {
	do 
	    if ((f = xopen(_PATH_DEVNULL, O_RDONLY|O_LARGEFILE)) == -1 &&
		(f = xopen("/", O_RDONLY|O_LARGEFILE)) == -1) 
		exit(1);
	while (f < 3);
	xclose(f);
    }

    osinit();			/* Os dependent initialization */

    
    {
	char *t;

	t = strrchr(argv[0], '/');
#ifdef WINNT_NATIVE
	{
	    char *s = strrchr(argv[0], '\\');
	    if (s)
		t = s;
	}
#endif /* WINNT_NATIVE */
	t = t ? t + 1 : argv[0];
	if (*t == '-') t++;
	progname = strsave((t && *t) ? t : tcshstr);    /* never want a null */
	tcsh = strncmp(progname, tcshstr, sizeof(tcshstr) - 1) == 0;
    }

    /*
     * Initialize non constant strings
     */
#ifdef _PATH_BSHELL
    STR_BSHELL = SAVE(_PATH_BSHELL);
#endif
#ifdef _PATH_TCSHELL
    STR_SHELLPATH = SAVE(_PATH_TCSHELL);
#else
# ifdef _PATH_CSHELL
    STR_SHELLPATH = SAVE(_PATH_CSHELL);
# endif
#endif
    STR_WORD_CHARS = SAVE(WORD_CHARS);
    STR_WORD_CHARS_VI = SAVE(WORD_CHARS_VI);

    HIST = '!';
    HISTSUB = '^';
    PRCH = tcsh ? '>' : '%';	/* to replace %# in $prompt for normal users */
    PRCHROOT = '#';		/* likewise for root */
    word_chars = STR_WORD_CHARS;
    bslash_quote = 0;		/* PWP: do tcsh-style backslash quoting? */
    anyerror = 1;		/* for compatibility */
    setcopy(STRanyerror, STRNULL, VAR_READWRITE);

    /* Default history size to 100 */
    setcopy(STRhistory, str2short("100"), VAR_READWRITE);
    sethistory(100);

    tempv = argv;
    ffile = SAVE(tempv[0]);
    dolzero = 0;
    if (eq(ffile, STRaout))	/* A.out's are quittable */
	quitit = 1;
    uid = getuid();
    gid = getgid();
    euid = geteuid();
    egid = getegid();
    /*
     * We are a login shell if: 1. we were invoked as -<something> with
     * optional arguments 2. or we were invoked only with the -l flag
     */
    loginsh = (**tempv == '-') || (argc == 2 &&
				   tempv[1][0] == '-' && tempv[1][1] == 'l' &&
						tempv[1][2] == '\0');
#ifdef _VMS_POSIX
    /* No better way to find if we are a login shell */
    if (!loginsh) {
	loginsh = (argc == 1 && getppid() == 1);
	**tempv = '-';	/* Avoid giving VMS an acidic stomach */
    }
#endif /* _VMS_POSIX */

    if (loginsh && **tempv != '-') {
	char *argv0;

	/*
	 * Mangle the argv space
	 */
	tempv[1][0] = '\0';
	tempv[1][1] = '\0';
	tempv[1] = NULL;
	argv0 = strspl("-", *tempv);
	*tempv = argv0;
	argc--;
    }
    if (loginsh) {
	(void) time(&chktim);
	setNS(STRloginsh);
    }

    NoNLSRebind = getenv("NOREBIND") != NULL;
#ifdef NLS
# ifdef SETLOCALEBUG
    dont_free = 1;
# endif /* SETLOCALEBUG */
    (void) setlocale(LC_ALL, "");
# ifdef LC_COLLATE
    (void) setlocale(LC_COLLATE, "");
# endif
# ifdef SETLOCALEBUG
    dont_free = 0;
# endif /* SETLOCALEBUG */
# ifdef STRCOLLBUG
    fix_strcoll_bug();
# endif /* STRCOLLBUG */

    /*
     * On solaris ISO8859-1 contains no printable characters in the upper half
     * so we need to test only for MB_CUR_MAX == 1, otherwise for multi-byte
     * locales we are always AsciiOnly == 0.
     */
    if (MB_CUR_MAX == 1) {
	int     k;

	for (k = 0200; k <= 0377 && !isprint(CTL_ESC(k)); k++)
	    continue;
	AsciiOnly = k > 0377;
    } else
	AsciiOnly = 0;
#else
    AsciiOnly = getenv("LANG") == NULL && getenv("LC_CTYPE") == NULL;
#endif				/* NLS */
    if (MapsAreInited && !NLSMapsAreInited)
	ed_InitNLSMaps();
    ResetArrowKeys();

    /*
     * Initialize for periodic command intervals. Also, initialize the dummy
     * tty list for login-watch.
     */
    (void) time(&t_period);
#ifndef HAVENOUTMP
    initwatch();
#endif /* !HAVENOUTMP */

#if defined(alliant)
    /*
     * From:  Jim Pace <jdp@research.att.com>
     * tcsh does not work properly on the alliants through an rlogin session.
     * The shell generally hangs.  Also, reference to the controlling terminal
     * does not work ( ie: echo foo > /dev/tty ).
     *
     * A security feature was added to rlogind affecting FX/80's Concentrix
     * from revision 5.5.xx upwards (through 5.7 where this fix was implemented)
     * This security change also affects the FX/2800 series.
     * The security change to rlogind requires the process group of an rlogin
     * session become disassociated with the tty in rlogind.
     *
     * The changes needed are:
     * 1. set the process group
     * 2. reenable the control terminal
     */
     if (loginsh && isatty(SHIN)) {
	 ttyn = ttyname(SHIN);
	 xclose(SHIN);
	 SHIN = xopen(ttyn, O_RDWR|O_LARGEFILE);
	 shpgrp = getpid();
	 (void) ioctl (SHIN, TIOCSPGRP, (ioctl_t) &shpgrp);
	 (void) setpgid(0, shpgrp);
     }
#endif /* alliant */

    /*
     * Move the descriptors to safe places. The variable didfds is 0 while we
     * have only FSH* to work with. When didfds is true, we have 0,1,2 and
     * prefer to use these.
     */
    initdesc();

    cdtohome = 1;
    setv(STRcdtohome, SAVE(""), VAR_READWRITE);

    /*
     * Get and set the tty now
     */
    if ((ttyn = ttyname(SHIN)) != NULL) {
	/*
	 * Could use rindex to get rid of other possible path components, but
	 * hpux preserves the subdirectory /pty/ when storing the tty name in
	 * utmp, so we keep it too.
	 */
	if (strncmp(ttyn, "/dev/", 5) == 0)
	    setv(STRtty, cp = SAVE(ttyn + 5), VAR_READWRITE);
	else
	    setv(STRtty, cp = SAVE(ttyn), VAR_READWRITE);
    }
    else
	setv(STRtty, cp = SAVE(""), VAR_READWRITE);

    /*
     * Initialize the shell variables. ARGV and PROMPT are initialized later.
     * STATUS is also munged in several places. CHILD is munged when
     * forking/waiting
     */

    /*
     * 7-10-87 Paul Placeway autologout should be set ONLY on login shells and
     * on shells running as root.  Out of these, autologout should NOT be set
     * for any psudo-terminals (this catches most window systems) and not for
     * any terminal running X windows.
     * 
     * At Ohio State, we have had problems with a user having his X session 
     * drop out from under him (on a Sun) because the shell in his master 
     * xterm timed out and exited.
     * 
     * Really, this should be done with a program external to the shell, that
     * watches for no activity (and NO running programs, such as dump) on a
     * terminal for a long peroid of time, and then SIGHUPS the shell on that
     * terminal.
     * 
     * bugfix by Rich Salz <rsalz@PINEAPPLE.BBN.COM>: For root rsh things 
     * allways first check to see if loginsh or really root, then do things 
     * with ttyname()
     * 
     * Also by Jean-Francois Lamy <lamy%ai.toronto.edu@RELAY.CS.NET>: check the
     * value of cp before using it! ("root can rsh too")
     * 
     * PWP: keep the nested ifs; the order of the tests matters and a good 
     * (smart) C compiler might re-arange things wrong.
     */
#ifdef AUTOLOGOUT
# ifdef convex
    if (uid == 0)
	/*  root always has a 15 minute autologout  */
	setcopy(STRautologout, STRrootdefautologout, VAR_READWRITE);
    else
	if (loginsh)
	    /*  users get autologout set to 0  */
	    setcopy(STRautologout, STR0, VAR_READWRITE);
# else /* convex */
    if (loginsh || (uid == 0)) {
	if (*cp) {
	    /* only for login shells or root and we must have a tty */
	    if (((cp2 = Strrchr(cp, (Char) '/')) != NULL) &&
		(Strncmp(cp, STRptssl, 3) != 0)) {
		cp2 = cp2 + 1;
	    }
	    else
		cp2 = cp;
	    if (!(((Strncmp(cp2, STRtty, 3) == 0) && Isalpha(cp2[3])) ||
	          Strstr(cp, STRptssl) != NULL)) {
		if (getenv("DISPLAY") == NULL) {
		    /* NOT on X window shells */
		    setcopy(STRautologout, STRdefautologout, VAR_READWRITE);
		}
	    }
	}
    }
# endif /* convex */
#endif /* AUTOLOGOUT */

    sigset_interrupting(SIGALRM, queue_alrmcatch);

    setcopy(STRstatus, STR0, VAR_READWRITE);

    /*
     * get and set machine specific environment variables
     */
    getmachine();


    /*
     * Publish the selected echo style
     */
#if ECHO_STYLE != BSD_ECHO
    if (tcsh) {
# if ECHO_STYLE == NONE_ECHO
	setcopy(STRecho_style, STRnone, VAR_READWRITE);
# endif /* ECHO_STYLE == NONE_ECHO */
# if ECHO_STYLE == SYSV_ECHO
	setcopy(STRecho_style, STRsysv, VAR_READWRITE);
# endif /* ECHO_STYLE == SYSV_ECHO */
# if ECHO_STYLE == BOTH_ECHO
	setcopy(STRecho_style, STRboth, VAR_READWRITE);
# endif /* ECHO_STYLE == BOTH_ECHO */
    } else
#endif /* ECHO_STYLE != BSD_ECHO */
	setcopy(STRecho_style, STRbsd, VAR_READWRITE);

    /*
     * increment the shell level.
     */
    shlvl(1);

#ifdef __ANDROID__
    /* On Android, $HOME either isn't set or set to /data, a R/O location.
       Check for the environment variable EXTERNAL_STORAGE, which contains
       the mount point of the external storage (SD card, mostly).  If
       EXTERNAL_STORAGE isn't set fall back to "/sdcard".  Eventually
       override $HOME so the environment is on the same page. */
    if (((tcp = getenv("HOME")) != NULL && strcmp (tcp, "/data") != 0)
	|| (tcp = getenv("EXTERNAL_STORAGE")) != NULL) {
	cp = quote(SAVE(tcp));
    } else
	cp = quote(SAVE("/sdcard"));
    tsetenv(STRKHOME, cp);
#else
    if ((tcp = getenv("HOME")) != NULL)
	cp = quote(SAVE(tcp));
    else
	cp = NULL;
#endif

    if (cp == NULL)
	fast = 1;		/* No home -> can't read scripts */
    else
	setv(STRhome, cp, VAR_READWRITE);

    dinit(cp);			/* dinit thinks that HOME == cwd in a login
				 * shell */
    /*
     * Grab other useful things from the environment. Should we grab
     * everything??
     */
    {
	char *cln, *cus, *cgr;
	struct passwd *pw;
	struct group *gr;


#ifdef apollo
	int     oid = getoid();

	setv(STRoid, Itoa(oid, 0, 0), VAR_READWRITE);
#endif /* apollo */

	setv(STReuid, Itoa(euid, 0, 0), VAR_READWRITE);
	if ((pw = xgetpwuid(euid)) == NULL)
	    setcopy(STReuser, STRunknown, VAR_READWRITE);
	else
	    setcopy(STReuser, str2short(pw->pw_name), VAR_READWRITE);

	setv(STRuid, Itoa(uid, 0, 0), VAR_READWRITE);

	setv(STRgid, Itoa(gid, 0, 0), VAR_READWRITE);

	cln = getenv("LOGNAME");
	cus = getenv("USER");
	if (cus != NULL)
	    setv(STRuser, quote(SAVE(cus)), VAR_READWRITE);
	else if (cln != NULL)
	    setv(STRuser, quote(SAVE(cln)), VAR_READWRITE);
	else if ((pw = xgetpwuid(uid)) == NULL)
	    setcopy(STRuser, STRunknown, VAR_READWRITE);
	else
	    setcopy(STRuser, str2short(pw->pw_name), VAR_READWRITE);
	if (cln == NULL)
	    tsetenv(STRLOGNAME, varval(STRuser));
	if (cus == NULL)
	    tsetenv(STRKUSER, varval(STRuser));
	
	cgr = getenv("GROUP");
	if (cgr != NULL)
	    setv(STRgroup, quote(SAVE(cgr)), VAR_READWRITE);
	else if ((gr = xgetgrgid(gid)) == NULL)
	    setcopy(STRgroup, STRunknown, VAR_READWRITE);
	else
	    setcopy(STRgroup, str2short(gr->gr_name), VAR_READWRITE);
	if (cgr == NULL)
	    tsetenv(STRKGROUP, varval(STRgroup));
    }

    /*
     * HOST may be wrong, since rexd transports the entire environment on sun
     * 3.x Just set it again
     */
    {
	char    cbuff[MAXHOSTNAMELEN];

	if (gethostname(cbuff, sizeof(cbuff)) >= 0) {
	    cbuff[sizeof(cbuff) - 1] = '\0';	/* just in case */
	    tsetenv(STRHOST, str2short(cbuff));
	}
	else
	    tsetenv(STRHOST, STRunknown);
    }


#ifdef REMOTEHOST
    /*
     * Try to determine the remote host we were logged in from.
     */
    remotehost();
#endif /* REMOTEHOST */
 
#ifdef apollo
    if ((tcp = getenv("SYSTYPE")) == NULL)
	tcp = "bsd4.3";
    tsetenv(STRSYSTYPE, quote(str2short(tcp)));
#endif /* apollo */

    /*
     * set editing on by default, unless running under Emacs as an inferior
     * shell.
     * We try to do this intelligently. If $TERM is available, then it
     * should determine if we should edit or not. $TERM is preserved
     * across rlogin sessions, so we will not get confused if we rlogin
     * under an emacs shell. Another advantage is that if we run an
     * xterm under an emacs shell, then the $TERM will be set to 
     * xterm, so we are going to want to edit. Unfortunately emacs
     * does not restore all the tty modes, so xterm is not very well
     * set up. But this is not the shell's fault.
     * Also don't edit if $TERM == wm, for when we're running under an ATK app.
     * Finally, emacs compiled under terminfo, sets the terminal to dumb,
     * so disable editing for that too.
     * 
     * Unfortunately, in some cases the initial $TERM setting is "unknown",
     * "dumb", or "network" which is then changed in the user's startup files.
     * We fix this by setting noediting here if $TERM is unknown/dumb and
     * if noediting is set, we switch on editing if $TERM is changed.
     */
    if ((tcp = getenv("TERM")) != NULL) {
	setv(STRterm, quote(SAVE(tcp)), VAR_READWRITE);
	noediting = strcmp(tcp, "unknown") == 0 || strcmp(tcp, "dumb") == 0 ||
		    strcmp(tcp, "network") == 0;
	editing = strcmp(tcp, "emacs") != 0 && strcmp(tcp, "wm") != 0 &&
		  !noediting;
    }
    else {
	noediting = 0;
	editing = ((tcp = getenv("EMACS")) == NULL || strcmp(tcp, "t") != 0);
    }

    /* 
     * The 'edit' variable is either set or unset.  It doesn't 
     * need a value.  Making it 'emacs' might be confusing. 
     */
    if (editing)
	setNS(STRedit);


    /*
     * still more mutability: make the complete routine automatically add the
     * suffix of file names...
     */
    setNS(STRaddsuffix);

    /*
     * Compatibility with tcsh >= 6.12 by default
     */
    setNS(STRcsubstnonl);
    
    /*
     * Random default kill ring size
     */
    setcopy(STRkillring, str2short("30"), VAR_READWRITE);

    /*
     * Re-initialize path if set in environment
     */
    if ((tcp = getenv("PATH")) == NULL)
#ifdef _PATH_DEFPATH
	importpath(str2short(_PATH_DEFPATH));
#else /* !_PATH_DEFPATH */
	setq(STRpath, defaultpath(), &shvhed, VAR_READWRITE);
#endif /* _PATH_DEFPATH */
    else
	/* Importpath() allocates memory for the path, and the
	 * returned pointer from SAVE() was discarded, so
	 * this was a memory leak.. (sg)
	 *
	 * importpath(SAVE(tcp));
	 */
	importpath(str2short(tcp));


    {
	/* If the SHELL environment variable ends with "tcsh", set
	 * STRshell to the same path.  This is to facilitate using
	 * the executable in environments where the compiled-in
	 * default isn't appropriate (sg).
	 */

	size_t sh_len = 0;

	if ((tcp = getenv("SHELL")) != NULL) {
	    sh_len = strlen(tcp);
	    if ((sh_len >= 5 && strcmp(tcp + (sh_len - 5), "/tcsh") == 0) || 
	        (!tcsh && sh_len >= 4 && strcmp(tcp + (sh_len - 4), "/csh") == 0))
		setv(STRshell, quote(SAVE(tcp)), VAR_READWRITE);
	    else
		sh_len = 0;
	}
	if (sh_len == 0)
	    setcopy(STRshell, STR_SHELLPATH, VAR_READWRITE);
    }

#ifdef _OSD_POSIX  /* BS2000 needs this variable set to "SHELL" */
    if ((tcp = getenv("PROGRAM_ENVIRONMENT")) == NULL)
	tcp = "SHELL";
    tsetenv(STRPROGRAM_ENVIRONMENT, quote(str2short(tcp)));
#endif /* _OSD_POSIX */

#ifdef COLOR_LS_F
    if ((tcp = getenv("LS_COLORS")) != NULL)
	parseLS_COLORS(str2short(tcp));
    if ((tcp = getenv("LSCOLORS")) != NULL)
	parseLSCOLORS(str2short(tcp));
#endif /* COLOR_LS_F */

    mainpid = getpid();
    doldol = putn((tcsh_number_t)mainpid);	/* For $$ */
#ifdef WINNT_NATIVE
    {
	char *tmp;
	Char *tmp2;
	if ((tmp = getenv("TMP")) != NULL) {
	    tmp = xasprintf("%s/%s", tmp, "sh");
	    tmp2 = SAVE(tmp);
	    xfree(tmp);
	}
	else {
	    tmp2 = SAVE(""); 
	}
	shtemp = Strspl(tmp2, doldol);	/* For << */
	xfree(tmp2);
    }
#else /* !WINNT_NATIVE */
#ifdef HAVE_MKSTEMP
    {
	const char *tmpdir = getenv ("TMPDIR");
	if (!tmpdir)
	    tmpdir = "/tmp";
	shtemp = Strspl(SAVE(tmpdir), SAVE("/sh" TMP_TEMPLATE)); /* For << */
    }
#else /* !HAVE_MKSTEMP */
    shtemp = Strspl(STRtmpsh, doldol);	/* For << */
#endif /* HAVE_MKSTEMP */
#endif /* WINNT_NATIVE */

    /*
     * Record the interrupt states from the parent process. If the parent is
     * non-interruptible our hand must be forced or we (and our children) won't
     * be either. Our children inherit termination from our parent. We catch it
     * only if we are the login shell.
     */
    sigaction(SIGINT, NULL, &parintr);
    sigaction(SIGTERM, NULL, &parterm);


#ifdef TCF
    /* Enable process migration on ourselves and our progeny */
    (void) signal(SIGMIGRATE, SIG_DFL);
#endif /* TCF */

    /*
     * dspkanji/dspmbyte autosetting
     */
    /* PATCH IDEA FROM Issei.Suzuki VERY THANKS */
#if defined(DSPMBYTE)
#if defined(NLS) && defined(LC_CTYPE)
    if (((tcp = setlocale(LC_CTYPE, NULL)) != NULL || (tcp = getenv("LANG")) != NULL) && !adrof(CHECK_MBYTEVAR))
#else
    if ((tcp = getenv("LANG")) != NULL && !adrof(CHECK_MBYTEVAR))
#endif
    {
	autoset_dspmbyte(str2short(tcp));
    }
#if defined(WINNT_NATIVE)
    else if (!adrof(CHECK_MBYTEVAR))
      nt_autoset_dspmbyte();
#endif /* WINNT_NATIVE */
#endif
#if defined(AUTOSET_KANJI) 
# if defined(NLS) && defined(LC_CTYPE)
    if (setlocale(LC_CTYPE, NULL) != NULL || getenv("LANG") != NULL)
# else
    if (getenv("LANG") != NULL)
# endif
	autoset_kanji();
#endif /* AUTOSET_KANJI */
    fix_version();		/* publish the shell version */

    if (argc > 1 && strcmp(argv[1], "--version") == 0) {
	xprintf("%S\n", varval(STRversion));
	xexit(0);
    }
    if (argc > 1 && strcmp(argv[1], "--help") == 0) {
	xprintf("%S\n\n", varval(STRversion));
	xprintf("%s", CGETS(11, 8, HELP_STRING));
	xexit(0);
    }
    /*
     * Process the arguments.
     * 
     * Note that processing of -v/-x is actually delayed till after script
     * processing.
     * 
     * We set the first character of our name to be '-' if we are a shell 
     * running interruptible commands.  Many programs which examine ps'es 
     * use this to filter such shells out.
     */
    argc--, tempv++;
    while (argc > 0 && (tcp = tempv[0])[0] == '-' &&
	   *++tcp != '\0' && !batch) {
	do
	    switch (*tcp++) {

	    case 0:		/* -	Interruptible, no prompt */
		prompt = 0;
		setintr = 1;
		nofile = 1;
		break;

	    case 'b':		/* -b	Next arg is input file */
		batch = 1;
		break;

	    case 'c':		/* -c	Command input from arg */
		if (argc == 1)
		    xexit(0);
		argc--, tempv++;
#ifdef M_XENIX
		/* Xenix Vi bug:
		   it relies on a 7 bit environment (/bin/sh), so it
		   pass ascii arguments with the 8th bit set */
		if (!strcmp(argv[0], "sh"))
		  {
		    char *p;

		    for (p = tempv[0]; *p; ++p)
		      *p &= ASCII;
		  }
#endif
		targinp = tempv[0];
		prompt = 0;
		nofile = 1;
		break;
	    case 'd':		/* -d	Load directory stack from file */
		rdirs = 1;
		break;

#ifdef apollo
	    case 'D':		/* -D	Define environment variable */
		{
		    Char *dp;

		    cp = str2short(tcp);
		    if (dp = Strchr(cp, '=')) {
			*dp++ = '\0';
			tsetenv(cp, dp);
		    }
		    else
			tsetenv(cp, STRNULL);
		}
		*tcp = '\0'; 	/* done with this argument */
		break;
#endif /* apollo */

	    case 'e':		/* -e	Exit on any error */
		exiterr = 1;
		break;

	    case 'f':		/* -f	Fast start */
		fast = 1;
		break;

	    case 'i':		/* -i	Interactive, even if !intty */
		intact = 1;
		nofile = 1;
		break;

	    case 'm':		/* -m	read .cshrc (from su) */
		mflag = 1;
		break;

	    case 'n':		/* -n	Don't execute */
		noexec = 1;
		break;

	    case 'q':		/* -q	(Undoc'd) ... die on quit */
		quitit = 1;
		break;

	    case 's':		/* -s	Read from std input */
		nofile = 1;
		break;

	    case 't':		/* -t	Read one line from input */
		onelflg = 2;
		prompt = 0;
		nofile = 1;
		break;

	    case 'v':		/* -v	Echo hist expanded input */
		nverbose = 1;	/* ... later */
		break;

	    case 'x':		/* -x	Echo just before execution */
		nexececho = 1;	/* ... later */
		break;

	    case 'V':		/* -V	Echo hist expanded input */
		setNS(STRverbose);	/* NOW! */
		break;

	    case 'X':		/* -X	Echo just before execution */
		setNS(STRecho);	/* NOW! */
		break;

	    case 'F':
		/*
		 * This will cause children to be created using fork instead of
		 * vfork.
		 */
		use_fork = 1;
		break;

	    case ' ':
	    case '\t':
	    case '\r':
	    case '\n':
		/* 
		 * for O/S's that don't do the argument parsing right in 
		 * "#!/foo -f " scripts
		 */
		break;

	    default:		/* Unknown command option */
		exiterr = 1;
		stderror(ERR_TCSHUSAGE, tcp-1, progname);
		break;

	} while (*tcp);
	tempv++, argc--;
    }

    if (quitit)			/* With all due haste, for debugging */
	(void) signal(SIGQUIT, SIG_DFL);

    /*
     * Unless prevented by -, -c, -i, -s, or -t, if there are remaining
     * arguments the first of them is the name of a shell file from which to
     * read commands.
     */
    if (nofile == 0 && argc > 0) {
	nofile = xopen(tempv[0], O_RDONLY|O_LARGEFILE);
	if (nofile < 0) {
	    child = 1;		/* So this ... */
	    /* ... doesn't return */
	    stderror(ERR_SYSTEM, tempv[0], strerror(errno));
	}
	xfree(ffile);
	dolzero = 1;
	ffile = SAVE(tempv[0]);
	/* 
	 * Replace FSHIN. Handle /dev/std{in,out,err} specially
	 * since once they are closed we cannot open them again.
	 * In that case we use our own saved descriptors
	 */
	if ((SHIN = dmove(nofile, FSHIN)) < 0) 
	    switch(nofile) {
	    case 0:
		SHIN = FSHIN;
		break;
	    case 1:
		SHIN = FSHOUT;
		break;
	    case 2:
		SHIN = FSHDIAG;
		break;
	    default:
		stderror(ERR_SYSTEM, tempv[0], strerror(errno));
		break;
	    }
	(void) close_on_exec(SHIN, 1);
	prompt = 0;
	 /* argc not used any more */ tempv++;
    }

    /* 
     * Call to closem() used to be part of initdesc(). Now called below where
     * the script name argument has become stdin. Kernel may have used a file
     * descriptor to hold the name of the script (setuid case) and this name
     * mustn't be lost by closing the fd too soon.
     */
    closem();

    /*
     * Consider input a tty if it really is or we are interactive. but not for
     * editing (christos)
     */
    if (!(intty = isatty(SHIN))) {
	if (adrof(STRedit))
	    unsetv(STRedit);
	editing = 0;
    }
    intty |= intact;
#ifndef convex
    if (intty || (intact && isatty(SHOUT))) {
	if (!batch && (uid != euid || gid != egid)) {
	    errno = EACCES;
	    child = 1;		/* So this ... */
	    /* ... doesn't return */
	    stderror(ERR_SYSTEM, progname, strerror(errno));
	}
    }
#endif /* convex */
    isoutatty = isatty(SHOUT);
    isdiagatty = isatty(SHDIAG);
    /*
     * Decide whether we should play with signals or not. If we are explicitly
     * told (via -i, or -) or we are a login shell (arg0 starts with -) or the
     * input and output are both the ttys("csh", or "csh</dev/ttyx>/dev/ttyx")
     * Note that in only the login shell is it likely that parent may have set
     * signals to be ignored
     */
    if (loginsh || intact || (intty && isatty(SHOUT)))
	setintr = 1;
    settell();
    /*
     * Save the remaining arguments in argv.
     */
    setq(STRargv, blk2short(tempv), &shvhed, VAR_READWRITE);

    /*
     * Set up the prompt.
     */
    if (prompt) {
	setcopy(STRprompt, STRdefprompt, VAR_READWRITE);
	/* that's a meta-questionmark */
	setcopy(STRprompt2, STRmquestion, VAR_READWRITE);
	setcopy(STRprompt3, STRKCORRECT, VAR_READWRITE);
    }

    /*
     * If we are an interactive shell, then start fiddling with the signals;
     * this is a tricky game.
     */
    shpgrp = mygetpgrp();
    opgrp = tpgrp = -1;
    if (setintr) {
	struct sigaction osig;

	**argv = '-';
	if (!quitit)		/* Wary! */
	    (void) signal(SIGQUIT, SIG_IGN);
	pintr_disabled = 1;
	sigset_interrupting(SIGINT, queue_pintr);
	(void) signal(SIGTERM, SIG_IGN);

	/* 
	 * No reason I can see not to save history on all these events..
	 * Most usual occurrence is in a window system, where we're not a login
	 * shell, but might as well be... (sg)
	 * But there might be races when lots of shells exit together...
	 * [this is also incompatible].
	 * We have to be mre careful here. If the parent wants to 
	 * ignore the signals then we leave them untouched...
	 * We also only setup the handlers for shells that are trully
	 * interactive.
	 */
	sigaction(SIGHUP, NULL, &osig);
	if (loginsh || osig.sa_handler != SIG_IGN)
	    /* exit processing on HUP */
	    sigset_interrupting(SIGHUP, queue_phup);
#ifdef SIGXCPU
	sigaction(SIGXCPU, NULL, &osig);
	if (loginsh || osig.sa_handler != SIG_IGN)
	    /* exit processing on XCPU */
	    sigset_interrupting(SIGXCPU, queue_phup);
#endif
#ifdef SIGXFSZ
	sigaction(SIGXFSZ, NULL, &osig);
	if (loginsh || osig.sa_handler != SIG_IGN)
	    /* exit processing on XFSZ */
	    sigset_interrupting(SIGXFSZ, queue_phup);
#endif

	if (quitit == 0 && targinp == 0) {
#ifdef SIGTSTP
	    (void) signal(SIGTSTP, SIG_IGN);
#endif
#ifdef SIGTTIN
	    (void) signal(SIGTTIN, SIG_IGN);
#endif
#ifdef SIGTTOU
	    (void) signal(SIGTTOU, SIG_IGN);
#endif
	    /*
	     * Wait till in foreground, in case someone stupidly runs csh &
	     * dont want to try to grab away the tty.
	     */
	    if (isatty(FSHDIAG))
		f = FSHDIAG;
	    else if (isatty(FSHOUT))
		f = FSHOUT;
	    else if (isatty(OLDSTD))
		f = OLDSTD;
	    else
		f = -1;

#ifdef NeXT
	    /* NeXT 2.0 /usr/etc/rlogind, does not set our process group! */
	    if (f != -1 && shpgrp == 0) {
	        shpgrp = getpid();
		(void) setpgid(0, shpgrp);
	        (void) tcsetpgrp(f, shpgrp);
	    }
#endif /* NeXT */
#ifdef BSDJOBS			/* if we have tty job control */
	    if (f != -1 && grabpgrp(f, shpgrp) != -1) {
		/*
		 * Thanks to Matt Day for the POSIX references, and to
		 * Paul Close for the SGI clarification.
		 */
		if (setdisc(f) != -1) {
		    opgrp = shpgrp;
		    shpgrp = getpid();
		    tpgrp = shpgrp;
		    if (tcsetpgrp(f, shpgrp) == -1) {
			/*
			 * On hpux 7.03 this fails with EPERM. This happens on
			 * the 800 when opgrp != shpgrp at this point. (we were
			 * forked from a non job control shell)
			 * POSIX 7.2.4, says we failed because the process
			 * group specified did not belong to a process
			 * in the same session with the tty. So we set our
			 * process group and try again.
			 */
			if (setpgid(0, shpgrp) == -1) {
			    xprintf("setpgid:");
			    goto notty;
			}
			if (tcsetpgrp(f, shpgrp) == -1) {
			    xprintf("tcsetpgrp:");
			    goto notty;
			}
		    }
		    /*
		     * We check the process group now. If it is the same, then
		     * we don't need to set it again. On hpux 7.0 on the 300's
		     * if we set it again it fails with EPERM. This is the
		     * correct behavior according to POSIX 4.3.3 if the process
		     * was a session leader .
		     */
		    else if (shpgrp != mygetpgrp()) {
			if(setpgid(0, shpgrp) == -1) {
			    xprintf("setpgid:");
			    goto notty;
			}
		    }
#ifdef IRIS4D
		    /*
		     * But on irix 3.3 we need to set it again, even if it is
		     * the same. We do that to tell the system that we
		     * need BSD process group compatibility.
		     */
		    else
			(void) setpgid(0, shpgrp);
#endif
		    (void) close_on_exec(dcopy(f, FSHTTY), 1);
		}
		else
		    tpgrp = -1;
	    }
	    if (tpgrp == -1) {
	notty:
	        xprintf(CGETS(11, 1, "Warning: no access to tty (%s).\n"),
		    strerror(errno));
		xprintf("%s",
		    CGETS(11, 2, "Thus no job control in this shell.\n"));
		/*
		 * Fix from:Sakari Jalovaara <sja@sirius.hut.fi> if we don't
		 * have access to tty, disable editing too
		 */
		if (adrof(STRedit))
		    unsetv(STRedit);
		editing = 0;
	    }
#else	/* BSDJOBS */		/* don't have job control, so frotz it */
	    tpgrp = -1;
#endif				/* BSDJOBS */
	}
    }
    if (setintr == 0 && parintr.sa_handler == SIG_DFL)
	setintr = 1;

/*
 * SVR4 doesn't send a SIGCHLD when a child is stopped or continued if the
 * handler is installed with signal(2) or sigset(2).  sigaction(2) must
 * be used instead.
 *
 * David Dawes (dawes@physics.su.oz.au) Sept 1991
 */
    sigset_interrupting(SIGCHLD, queue_pchild);

    if (intty && !targinp) 	
	(void) ed_Setup(editing);/* Get the tty state, and set defaults */
				 /* Only alter the tty state if editing */
    
    /*
     * Set an exit here in case of an interrupt or error reading the shell
     * start-up scripts.
     */
    osetintr = setintr;
    oparintr = parintr;
    (void)cleanup_push_mark(); /* There is no outer handler */
    if (setexit() != 0) /* PWP */
	reenter = 1;
    else
	reenter = 0;
    exitset++;
    haderr = 0;			/* In case second time through */
    if (!fast && reenter == 0) {
	/* Will have varval(STRhome) here because set fast if don't */
	{
	    pintr_disabled++;
	    cleanup_push(&pintr_disabled, disabled_cleanup);
	    setintr = 0;/*FIXRESET:cleanup*/
	    /* onintr in /etc/ files has no effect */
	    parintr.sa_handler = SIG_IGN;/*FIXRESET: cleanup*/
#ifdef LOGINFIRST
#ifdef _PATH_DOTLOGIN
	    if (loginsh)
		(void) srcfile(_PATH_DOTLOGIN, 0, 0, NULL);
#endif
#endif

#ifdef _PATH_DOTCSHRC
	    (void) srcfile(_PATH_DOTCSHRC, 0, 0, NULL);
#endif
	    if (!targinp && !onelflg && !havhash)
		dohash(NULL,NULL);
#ifndef LOGINFIRST
#ifdef _PATH_DOTLOGIN
	    if (loginsh)
		(void) srcfile(_PATH_DOTLOGIN, 0, 0, NULL);
#endif
#endif
	    cleanup_until(&pintr_disabled);
	    setintr = osetintr;
	    parintr = oparintr;
	}
#ifdef LOGINFIRST
	if (loginsh)
	    (void) srccat(varval(STRhome), STRsldotlogin);
#endif
	/* upward compat. */
	if (!srccat(varval(STRhome), STRsldottcshrc))
	    (void) srccat(varval(STRhome), STRsldotcshrc);

	if (!targinp && !onelflg && !havhash)
	    dohash(NULL,NULL);

	/*
	 * Source history before .login so that it is available in .login
	 */
	loadhist(NULL, 0);
#ifndef LOGINFIRST
	if (loginsh)
	    (void) srccat(varval(STRhome), STRsldotlogin);
#endif
	if (loginsh || rdirs)
	    loaddirs(NULL);
    }
    /* Reset interrupt flag */
    setintr = osetintr;
    parintr = oparintr;
    exitset--;

    /* Initing AFTER .cshrc is the Right Way */
    if (intty && !targinp) {	/* PWP setup stuff */
	ed_Init();		/* init the new line editor */
#ifdef SIG_WINDOW
	check_window_size(1);	/* mung environment */
#endif				/* SIG_WINDOW */
    }

    /*
     * Now are ready for the -v and -x flags
     */
    if (nverbose)
	setNS(STRverbose);
    if (nexececho)
	setNS(STRecho);
    

    if (targinp) {
	arginp = SAVE(targinp);
	/*
	 * we put the command into a variable
	 */
	if (arginp != NULL)
	    setv(STRcommand, quote(Strsave(arginp)), VAR_READWRITE);

	/*
	 * * Give an error on -c arguments that end in * backslash to
	 * ensure that you don't make * nonportable csh scripts.
	 */
	{
	    int count;

	    cp = Strend(arginp);
	    count = 0;
	    while (cp > arginp && *--cp == '\\')
		++count;
	    if ((count & 1) != 0) {
		exiterr = 1;
		stderror(ERR_ARGC);
	    }
	}
    }
    /*
     * All the rest of the world is inside this call. The argument to process
     * indicates whether it should catch "error unwinds".  Thus if we are a
     * interactive shell our call here will never return by being blown past on
     * an error.
     */
    process(setintr);

    /*
     * Mop-up.
     */
    /* Take care of these (especially HUP) here instead of inside flush. */
    handle_pending_signals();
    if (intty) {
	if (loginsh) {
	    xprintf("logout\n");
	    xclose(SHIN);
	    child = 1;
#ifdef TESLA
	    do_logout = 1;
#endif				/* TESLA */
	    goodbye(NULL, NULL);
	}
	else {
	    xprintf("exit\n");
	}
    }
    record();
    exitstat();
    return (0);
}

void
untty(void)
{
#ifdef BSDJOBS
    if (tpgrp > 0 && opgrp != shpgrp) {
	(void) setpgid(0, opgrp);
	(void) tcsetpgrp(FSHTTY, opgrp);
	(void) resetdisc(FSHTTY);
    }
#endif /* BSDJOBS */
}

void
importpath(Char *cp)
{
    size_t i = 0;
    Char *dp;
    Char **pv;
    int     c;

    for (dp = cp; *dp; dp++)
	if (*dp == PATHSEP)
	    i++;
    /*
     * i+2 where i is the number of colons in the path. There are i+1
     * directories in the path plus we need room for a zero terminator.
     */
    pv = xcalloc(i + 2, sizeof(Char *));
    dp = cp;
    i = 0;
    if (*dp)
	for (;;) {
	    if ((c = *dp) == PATHSEP || c == 0) {
		*dp = 0;
		pv[i++] = Strsave(*cp ? cp : STRdot);
		if (c) {
		    cp = dp + 1;
		    *dp = PATHSEP;
		}
		else
		    break;
	    }
#ifdef WINNT_NATIVE
	    else if (*dp == '\\')
		*dp = '/';
#endif /* WINNT_NATIVE */
	    dp++;
	}
    pv[i] = 0;
    cleanup_push(pv, blk_cleanup);
    setq(STRpath, pv, &shvhed, VAR_READWRITE);
    cleanup_ignore(pv);
    cleanup_until(pv);
}

/*
 * Source to the file which is the catenation of the argument names.
 */
static int
srccat(Char *cp, Char *dp)
{
    if (cp[0] == '/' && cp[1] == '\0') 
	return srcfile(short2str(dp), (mflag ? 0 : 1), 0, NULL);
    else {
	Char *ep;
	char   *ptr;
	int rv;

#ifdef WINNT_NATIVE
	ep = Strend(cp);
	if (ep != cp && ep[-1] == '/' && dp[0] == '/') /* silly win95 */
	    dp++;
#endif /* WINNT_NATIVE */

	ep = Strspl(cp, dp);
	cleanup_push(ep, xfree);
	ptr = short2str(ep);

	rv = srcfile(ptr, (mflag ? 0 : 1), 0, NULL);
	cleanup_until(ep);
	return rv;
    }
}

/*
 * Source to a file putting the file descriptor in a safe place (> 2).
 */
#ifndef WINNT_NATIVE
static int
#else
int
#endif /*WINNT_NATIVE*/
srcfile(const char *f, int onlyown, int flag, Char **av)
{
    int unit;

    if ((unit = xopen(f, O_RDONLY|O_LARGEFILE)) == -1) 
	return 0;
    cleanup_push(&unit, open_cleanup);
    unit = dmove(unit, -1);
    cleanup_ignore(&unit);
    cleanup_until(&unit);

    (void) close_on_exec(unit, 1);
    srcunit(unit, onlyown, flag, av);
    return 1;
}


/*
 * Save the shell state, and establish new argument vector, and new input
 * fd.
 */
static void
st_save(struct saved_state *st, int unit, int hflg, Char **al, Char **av)
{
    st->insource	= insource;
    st->SHIN		= SHIN;
    /* Want to preserve the meaning of "source file >output".
     * Save old descriptors, move new 0,1,2 to safe places and assign
     * them to SH* and let process() redo 0,1,2 from them.
     *
     * The macro returns true if d1 and d2 are good and they point to
     * different things.  If you don't avoid saving duplicate
     * descriptors, you really limit the depth of "source" recursion
     * you can do because of all the open file descriptors.  -IAN!
     */
#define NEED_SAVE_FD(d1,d2) \
    (fstat(d1, &s1) != -1 && fstat(d2, &s2) != -1 \
	&& (s1.st_ino != s2.st_ino || s1.st_dev != s2.st_dev) )

    st->OLDSTD = st->SHOUT = st->SHDIAG = -1;/* test later to restore these */
    if (didfds) {
	    struct stat s1, s2;
	    if (NEED_SAVE_FD(0,OLDSTD)) {
		    st->OLDSTD = OLDSTD;
		    OLDSTD = dmove(0, -1);
		    (void)close_on_exec(OLDSTD, 1);
	    }
	    if (NEED_SAVE_FD(1,SHOUT)) {
		    st->SHOUT = SHOUT;
		    SHOUT = dmove(1, -1);
		    (void)close_on_exec(SHOUT, 1);
	    }
	    if (NEED_SAVE_FD(2,SHDIAG)) {
		    st->SHDIAG = SHDIAG;
		    SHDIAG = dmove(2, -1);
		    (void)close_on_exec(SHDIAG, 1);
	    }
	    donefds();
    }

    st->intty		= intty;
    st->whyles		= whyles;
    st->gointr		= gointr;
    st->arginp		= arginp;
    st->evalp		= evalp;
    st->evalvec		= evalvec;
    st->alvecp		= alvecp;
    st->alvec		= alvec;
    st->onelflg		= onelflg;
    st->enterhist	= enterhist;
    st->justpr		= justpr;
    if (hflg)
	st->HIST	= HIST;
    else
	st->HIST	= '\0';
    st->cantell		= cantell;
    cpybin(st->B, B);

    /*
     * we can now pass arguments to source. 
     * For compatibility we do that only if arguments were really
     * passed, otherwise we keep the old, global $argv like before.
     */
    if (av != NULL && *av != NULL) {
	struct varent *vp;
	if ((vp = adrof(STRargv)) != NULL && vp->vec != NULL)
	    st->argv = saveblk(vp->vec);
	else
	    st->argv = NULL;
	setq(STRargv, saveblk(av), &shvhed, VAR_READWRITE);
    }
    else
	st->argv = NULL;
    st->av = av;

    SHIN	= unit;	/* Do this first */

    /* Establish new input arena */
    {
	fbuf = NULL;
	fseekp = feobp = fblocks = 0;
	settell();
    }

    arginp	= 0;
    onelflg	= 0;
    intty	= isatty(SHIN);
    whyles	= 0;
    gointr	= 0;
    evalvec	= 0;
    evalp	= 0;
    alvec	= al;
    alvecp	= 0;
    enterhist	= hflg;
    if (enterhist)
	HIST	= '\0';
    insource	= 1;
}


/*
 * Restore the shell to a saved state
 */
static void
st_restore(void *xst)
{
    struct saved_state *st;

    st = xst;
    if (st->SHIN == -1)
	return;

    /* Reset input arena */
    {
	int i;
	Char** nfbuf = fbuf;
	int nfblocks = fblocks;

	fblocks = 0;
	fbuf = NULL;
	for (i = 0; i < nfblocks; i++)
	    xfree(nfbuf[i]);
	xfree(nfbuf);
    }
    cpybin(B, st->B);

    xclose(SHIN);

    insource	= st->insource;
    SHIN	= st->SHIN;
    if (st->OLDSTD != -1)
	xclose(OLDSTD), OLDSTD = st->OLDSTD;
    if (st->SHOUT != -1)
	xclose(SHOUT),  SHOUT = st->SHOUT;
    if (st->SHDIAG != -1)
	xclose(SHDIAG), SHDIAG = st->SHDIAG;
    arginp	= st->arginp;
    onelflg	= st->onelflg;
    evalp	= st->evalp;
    evalvec	= st->evalvec;
    alvecp	= st->alvecp;
    alvec	= st->alvec;
    intty	= st->intty;
    whyles	= st->whyles;
    gointr	= st->gointr;
    if (st->HIST != '\0')
	HIST	= st->HIST;
    enterhist	= st->enterhist;
    cantell	= st->cantell;
    justpr	= st->justpr;

    if (st->argv != NULL)
	setq(STRargv, st->argv, &shvhed, VAR_READWRITE);
    else if (st->av != NULL  && *st->av != NULL && adrof(STRargv) != NULL)
	unsetv(STRargv);
}

/*
 * Source to a unit.  If onlyown it must be our file or our group or
 * we don't chance it.	This occurs on ".cshrc"s and the like.
 */
static void
srcunit(int unit, int onlyown, int hflg, Char **av)
{
    struct saved_state st;

    st.SHIN = -1;	/* st_restore checks this */

    if (unit < 0)
	return;

    if (onlyown) {
	struct stat stb;

	if (fstat(unit, &stb) < 0) {
	    xclose(unit);
	    return;
	}
    }

    /* Does nothing before st_save() because st.SHIN == -1 */
    cleanup_push(&st, st_restore);
    if (setintr) {
	pintr_disabled++;
	cleanup_push(&pintr_disabled, disabled_cleanup);
    }

    /* Save the current state and move us to a new state */
    st_save(&st, unit, hflg, NULL, av);

    /*
     * Now if we are allowing commands to be interrupted, we let ourselves be
     * interrupted.
     */
    if (setintr) {
	cleanup_until(&pintr_disabled);
	pintr_disabled++;
	cleanup_push(&pintr_disabled, disabled_cleanup);
    }

    process(0);		/* 0 -> blow away on errors */

    /* Restore the old state */
    cleanup_until(&st);
}


/*ARGSUSED*/
void
goodbye(Char **v, struct command *c)
{
    USE(v);
    USE(c);
    record();

    if (loginsh) {
	size_t omark;
	sigset_t set;

	sigemptyset(&set);
	signal(SIGQUIT, SIG_IGN);
	sigaddset(&set, SIGQUIT);
	sigprocmask(SIG_UNBLOCK, &set, NULL);
	signal(SIGINT, SIG_IGN);
	sigaddset(&set, SIGINT);
	signal(SIGTERM, SIG_IGN);
	sigaddset(&set, SIGTERM);
	signal(SIGHUP, SIG_IGN);
	sigaddset(&set, SIGHUP);
	sigprocmask(SIG_UNBLOCK, &set, NULL);
	phup_disabled = 1;
	setintr = 0;		/* No interrupts after "logout" */
	/* Trap errors inside .logout */
	omark = cleanup_push_mark();
	if (setexit() == 0) {
	    if (!(adrof(STRlogout)))
		setcopy(STRlogout, STRnormal, VAR_READWRITE);
#ifdef _PATH_DOTLOGOUT
	    (void) srcfile(_PATH_DOTLOGOUT, 0, 0, NULL);
#endif
	    if (adrof(STRhome))
		(void) srccat(varval(STRhome), STRsldtlogout);
#ifdef TESLA
	    do_logout = 1;
#endif /* TESLA */
	}
	cleanup_pop_mark(omark);
    }
    exitstat();
}

void
exitstat(void)
{
#ifdef PROF
    _mcleanup();
#endif
    /*
     * Note that if STATUS is corrupted (i.e. getn bombs) then error will exit
     * directly because we poke child here. Otherwise we might continue
     * unwarrantedly (sic).
     */
    child = 1;

    xexit(getn(varval(STRstatus)));
}

/*
 * in the event of a HUP we want to save the history
 */
void
phup(void)
{
    if (loginsh) {
	setcopy(STRlogout, STRhangup, VAR_READWRITE);
#ifdef _PATH_DOTLOGOUT
	(void) srcfile(_PATH_DOTLOGOUT, 0, 0, NULL);
#endif
	if (adrof(STRhome))
	    (void) srccat(varval(STRhome), STRsldtlogout);
    }

    record();

#ifdef POSIXJOBS 
    /*
     * We kill the last foreground process group. It then becomes
     * responsible to propagate the SIGHUP to its progeny. 
     */
    {
	struct process *pp, *np;

	for (pp = proclist.p_next; pp; pp = pp->p_next) {
	    np = pp;
	    /* 
	     * Find if this job is in the foreground. It could be that
	     * the process leader has exited and the foreground flag
	     * is cleared for it.
	     */
	    do 
		/*
		 * If a process is in the foreground we try to kill
		 * it's process group. If we succeed, then the 
		 * whole job is gone. Otherwise we keep going...
		 * But avoid sending HUP to the shell again.
		 */
		if (((np->p_flags & PFOREGND) != 0) && np->p_jobid != shpgrp) {
		    np->p_flags &= ~PHUP;
		    if (killpg(np->p_jobid, SIGHUP) != -1) {
			/* In case the job was suspended... */
#ifdef SIGCONT
			(void) killpg(np->p_jobid, SIGCONT);
#endif
			break;
		    }
		}
	    while ((np = np->p_friends) != pp);
	}
    }
#endif /* POSIXJOBS */

    xexit(SIGHUP);
}

static Char   *jobargv[2] = {STRjobs, 0};

/*
 * Catch an interrupt, e.g. during lexical input.
 * If we are an interactive shell, we reset the interrupt catch
 * immediately.  In any case we drain the shell output,
 * and finally go through the normal error mechanism, which
 * gets a chance to make the shell go away.
 */
int just_signaled;		/* bugfix by Michael Bloom (mg@ttidca.TTI.COM) */

void
pintr(void)
{
    just_signaled = 1;
    pintr1(1);
}

void
pintr1(int wantnl)
{
    if (setintr) {
	if (pjobs) {
	    pjobs = 0;
	    xputchar('\n');
	    dojobs(jobargv, NULL);
	    stderror(ERR_NAME | ERR_INTR);
	}
    }
    /* MH - handle interrupted completions specially */
    {
	if (InsideCompletion)
	    stderror(ERR_SILENT);
    }
    /* JV - Make sure we shut off inputl */
    {
	(void) Cookedmode();
	GettingInput = 0;
	if (evalvec)
	    doneinp = 1;
    }
    drainoline();
#ifdef HAVE_GETPWENT
    (void) endpwent();
#endif

    /*
     * If we have an active "onintr" then we search for the label. Note that if
     * one does "onintr -" then we shan't be interruptible so we needn't worry
     * about that here.
     */
    if (gointr) {
	gotolab(gointr);
	reset();
    }
    else if (intty && wantnl) {
	if (editing) {
	    /* 
	     * If we are editing a multi-line input command, and move to
	     * the beginning of the line, we don't want to trash it when
	     * we hit ^C
	     */
	    PastBottom();
	    ClearLines();
	    ClearDisp();
	}
	else {
	    /* xputchar('\n'); *//* Some like this, others don't */
	    (void) putraw('\r');
	    (void) putraw('\n');
	}
    }
    stderror(ERR_SILENT);
}

/*
 * Process is the main driving routine for the shell.
 * It runs all command processing, except for those within { ... }
 * in expressions (which is run by a routine evalav in sh.exp.c which
 * is a stripped down process), and `...` evaluation which is run
 * also by a subset of this code in sh.glob.c in the routine backeval.
 *
 * The code here is a little strange because part of it is interruptible
 * and hence freeing of structures appears to occur when none is necessary
 * if this is ignored.
 *
 * Note that if catch is not set then we will unwind on any error.
 * If an end-of-file occurs, we return.
 */
void
process(int catch)
{
    jmp_buf_t osetexit;
    /* PWP: This might get nuked by longjmp so don't make it a register var */
    size_t omark;
    volatile int didexitset = 0;

    getexit(osetexit);
    omark = cleanup_push_mark();
    for (;;) {
	struct command *t;
	int hadhist, old_pintr_disabled;

	(void)setexit();
	if (didexitset == 0) {
	    exitset++;
	    didexitset++;
	}
	pendjob();

	justpr = enterhist;	/* execute if not entering history */

	if (haderr) {
	    if (!catch) {
		/* unwind */
		doneinp = 0;
		cleanup_pop_mark(omark);
		resexit(osetexit);
		reset();
	    }
	    haderr = 0;
	    /*
	     * Every error is eventually caught here or the shell dies.  It is
	     * at this point that we clean up any left-over open files, by
	     * closing all but a fixed number of pre-defined files.  Thus
	     * routines don't have to worry about leaving files open due to
	     * deeper errors... they will get closed here.
	     */
	    closem();
	    continue;
	}
	if (doneinp) {
	    doneinp = 0;
	    break;
	}
	if (chkstop)
	    chkstop--;
	if (neednote)
	    pnote();
	if (intty && prompt && evalvec == 0) {
	    just_signaled = 0;
	    mailchk();
	    /*
	     * Watch for logins/logouts. Next is scheduled commands stored
	     * previously using "sched." Then execute periodic commands.
	     * Following that, the prompt precmd is run.
	     */
#ifndef HAVENOUTMP
	    watch_login(0);
#endif /* !HAVENOUTMP */
	    sched_run();
	    period_cmd();
	    precmd();
	    /*
	     * If we are at the end of the input buffer then we are going to
	     * read fresh stuff. Otherwise, we are rereading input and don't
	     * need or want to prompt.
	     */
	    if (fseekp == feobp && aret == TCSH_F_SEEK)
		printprompt(0, NULL);
	    flush();
	    setalarm(1);
	}
	if (seterr) {
	    xfree(seterr);
	    seterr = NULL;
	}

	/*
	 * Interruptible during interactive reads
	 */
	if (setintr)
	    pintr_push_enable(&old_pintr_disabled);
	freelex(&paraml);
	hadhist = lex(&paraml);
	if (setintr)
	    cleanup_until(&old_pintr_disabled);
	cleanup_push(&paraml, lex_cleanup);

	/*
	 * Echo not only on VERBOSE, but also with history expansion. If there
	 * is a lexical error then we forego history echo.
	 * Do not echo if we're only entering history (source -h).
	 */
	if ((hadhist && !seterr && intty && !tellwhat && !Expand && !whyles) ||
	    (!enterhist && adrof(STRverbose)))
	{
	    int odidfds = didfds;
	    haderr = 1;
	    didfds = 0;
	    prlex(&paraml);
	    flush();
	    haderr = 0;
	    didfds = odidfds;
	}
	(void) alarm(0);	/* Autologout OFF */
	alrmcatch_disabled = 1;

	/*
	 * Save input text on the history list if reading in old history, or it
	 * is from the terminal at the top level and not in a loop.
	 * 
	 * PWP: entry of items in the history list while in a while loop is done
	 * elsewhere...
	 */
	if (enterhist || (catch && intty && !whyles && !tellwhat && !arun))
	    savehist(&paraml, enterhist > 1);

	if (Expand && seterr)
	    Expand = 0;

	/*
	 * Print lexical error messages, except when sourcing history lists.
	 */
	if (!enterhist && seterr)
	    stderror(ERR_OLD);

	/*
	 * If had a history command :p modifier then this is as far as we
	 * should go
	 */
	if (justpr)
	    goto cmd_done;

	/*
	 * If had a tellwhat from twenex() then do
	 */
	if (tellwhat) {
	    (void) tellmewhat(&paraml, NULL);
	    goto cmd_done;
	}

	alias(&paraml);

#ifdef BSDJOBS
	/*
	 * If we are interactive, try to continue jobs that we have stopped
	 */
	if (prompt)
	    continue_jobs(&paraml);
#endif				/* BSDJOBS */

	/*
	 * Check to see if the user typed "rm * .o" or something
	 */
	if (prompt)
	    rmstar(&paraml);
	/*
	 * Parse the words of the input into a parse tree.
	 */
	t = syntax(paraml.next, &paraml, 0);
	/*
	 * We cannot cleanup push here, because cd /blah; echo foo
	 * would rewind t on the chdir error, and free the rest of the command
	 */
	if (seterr) {
	    freesyn(t);
	    stderror(ERR_OLD);
	}

	postcmd();
	/*
	 * Execute the parse tree From: Michael Schroeder
	 * <mlschroe@immd4.informatik.uni-erlangen.de> was execute(t, tpgrp);
	 */
	execute(t, (tpgrp > 0 ? tpgrp : -1), NULL, NULL, TRUE);
	freesyn(t);

	/*
	 * Made it!
	 */
#ifdef SIG_WINDOW
	if (windowchg || (catch && intty && !whyles && !tellwhat)) {
	    (void) check_window_size(0);	/* for window systems */
	}
#endif /* SIG_WINDOW */
	setcopy(STR_, InputBuf, VAR_READWRITE | VAR_NOGLOB);
    cmd_done:
	if (cleanup_reset())
	    cleanup_until(&paraml);
	else
	    haderr = 1;
    }
    cleanup_pop_mark(omark);
    resexit(osetexit);
    exitset--;
    handle_pending_signals();
}

/*ARGSUSED*/
void
dosource(Char **t, struct command *c)
{
    Char *f;
    int    hflg = 0;
    char *file;

    USE(c);
    t++;
    if (*t && eq(*t, STRmh)) {
	if (*++t == NULL)
	    stderror(ERR_NAME | ERR_HFLAG);
	hflg++;
    }
    else if (*t && eq(*t, STRmm)) {
    	if (*++t == NULL)
	    stderror(ERR_NAME | ERR_MFLAG);
	hflg = 2;
    }

    f = globone(*t++, G_ERROR);
    file = strsave(short2str(f));
    cleanup_push(file, xfree);
    xfree(f);
    t = glob_all_or_error(t);
    cleanup_push(t, blk_cleanup);
    if ((!srcfile(file, 0, hflg, t)) && (!hflg) && (!bequiet))
	stderror(ERR_SYSTEM, file, strerror(errno));
    cleanup_until(file);
}

/*
 * Check for mail.
 * If we are a login shell, then we don't want to tell
 * about any mail file unless its been modified
 * after the time we started.
 * This prevents us from telling the user things he already
 * knows, since the login program insists on saying
 * "You have mail."
 */

/*
 * The AMS version.
 * This version checks if the file is a directory, and if so,
 * tells you the number of files in it, otherwise do the old thang.
 * The magic "+1" in the time calculation is to compensate for
 * an AFS bug where directory mtimes are set to 1 second in
 * the future.
 */

static void
mailchk(void)
{
    struct varent *v;
    Char **vp;
    time_t  t;
    int     intvl, cnt;
    struct stat stb;
    int    new;

    v = adrof(STRmail);
    if (v == NULL || v->vec == NULL)
	return;
    (void) time(&t);
    vp = v->vec;
    cnt = blklen(vp);
    intvl = (cnt && number(*vp)) ? (--cnt, getn(*vp++)) : MAILINTVL;
    if (intvl < 1)
	intvl = 1;
    if (chktim + intvl > t)
	return;
    for (; *vp; vp++) {
	char *filename = short2str(*vp);
	char *mboxdir = filename;

	if (stat(filename, &stb) < 0)
	    continue;
#if defined(BSDTIMES) || defined(_SEQUENT_)
	new = stb.st_mtime > time0.tv_sec;
#else
	new = stb.st_mtime > seconds0;
#endif
	if (S_ISDIR(stb.st_mode)) {
	    DIR *mailbox;
	    int mailcount = 0;
	    char *tempfilename;
	    struct stat stc;

	    tempfilename = xasprintf("%s/new", filename);

	    if (stat(tempfilename, &stc) != -1 && S_ISDIR(stc.st_mode)) {
		/*
		 * "filename/new" exists and is a directory; you are
		 * using Qmail.
		 */
		stb = stc;
#if defined(BSDTIMES) || defined(_SEQUENT_)
		new = stb.st_mtime > time0.tv_sec;
#else
		new = stb.st_mtime > seconds0;
#endif
		mboxdir = tempfilename;
	    }

	    if (stb.st_mtime <= chktim + 1 || (loginsh && !new)) {
		xfree(tempfilename);
		continue;
	    }

	    mailbox = opendir(mboxdir);
	    xfree(tempfilename);
	    if (mailbox == NULL)
		continue;

	    /* skip . and .. */
	    if (!readdir(mailbox) || !readdir(mailbox)) {
		(void)closedir(mailbox);
		continue;
	    }

	    while (readdir(mailbox))
		mailcount++;

	    (void)closedir(mailbox);
	    if (mailcount == 0)
		continue;

	    if (cnt == 1)
		xprintf(CGETS(11, 3, "You have %d mail messages.\n"),
			mailcount);
	    else
		xprintf(CGETS(11, 4, "You have %d mail messages in %s.\n"),
			mailcount, filename);
	}
	else {
	    char *type;
	    
	    if (stb.st_size == 0 || stb.st_atime >= stb.st_mtime ||
		(stb.st_atime <= chktim && stb.st_mtime <= chktim) ||
		(loginsh && !new))
		continue;
	    type = strsave(new ? CGETS(11, 6, "new ") : "");
	    cleanup_push(type, xfree);
	    if (cnt == 1)
		xprintf(CGETS(11, 5, "You have %smail.\n"), type);
	    else
	        xprintf(CGETS(11, 7, "You have %smail in %s.\n"), type, filename);
	    cleanup_until(type);
	}
    }
    chktim = t;
}

/*
 * Extract a home directory from the password file
 * The argument points to a buffer where the name of the
 * user whose home directory is sought is currently.
 * We return home directory of the user, or NULL.
 */
Char *
gethdir(const Char *home)
{
    Char   *h;

    /*
     * Is it us?
     */
    if (*home == '\0') {
	if ((h = varval(STRhome)) != STRNULL)
	    return Strsave(h);
	else
	    return NULL;
    }

    /*
     * Look in the cache
     */
    if ((h = gettilde(home)) == NULL)
	return NULL;
    else
	return Strsave(h);
}

/*
 * Move the initial descriptors to their eventual
 * resting places, closing all other units.
 */
void
initdesc(void)
{
#ifdef NLS_BUGS
#ifdef NLS_CATALOGS
    nlsclose();
#endif /* NLS_CATALOGS */
#endif /* NLS_BUGS */


    didfds = 0;			/* 0, 1, 2 aren't set up */
    (void) close_on_exec(SHIN = dcopy(0, FSHIN), 1);
    (void) close_on_exec(SHOUT = dcopy(1, FSHOUT), 1);
    (void) close_on_exec(SHDIAG = dcopy(2, FSHDIAG), 1);
    (void) close_on_exec(OLDSTD = dcopy(SHIN, FOLDSTD), 1);
#ifndef CLOSE_ON_EXEC
    didcch = 0;			/* Havent closed for child */
#endif /* CLOSE_ON_EXEC */
    if (SHDIAG >= 0)
	isdiagatty = isatty(SHDIAG);
    else
    	isdiagatty = 0;
    if (SHDIAG >= 0)
	isoutatty = isatty(SHOUT);
    else
    	isoutatty = 0;
#ifdef NLS_BUGS
#ifdef NLS_CATALOGS
    nlsinit();
#endif /* NLS_CATALOGS */
#endif /* NLS_BUGS */
}


void
#ifdef PROF
done(int i)
#else
xexit(int i)
#endif
{
#ifdef TESLA
    if (loginsh && do_logout) {
	/* this is to send hangup signal to the develcon */
	/* we toggle DTR. clear dtr - sleep 1 - set dtr */
	/* ioctl will return ENOTTY for pty's but we ignore it 	 */
	/* exitstat will run after disconnect */
	/* we sleep for 2 seconds to let things happen in */
	/* .logout and rechist() */
#ifdef TIOCCDTR
	(void) sleep(2);
	(void) ioctl(FSHTTY, TIOCCDTR, NULL);
	(void) sleep(1);
	(void) ioctl(FSHTTY, TIOCSDTR, NULL);
#endif /* TIOCCDTR */
    }
#endif /* TESLA */

    {
	struct process *pp, *np;
	pid_t mypid = getpid();
	/* Kill all processes marked for hup'ing */
	for (pp = proclist.p_next; pp; pp = pp->p_next) {
	    np = pp;
	    do
		if ((np->p_flags & PHUP) && np->p_jobid != shpgrp &&
		    np->p_parentid == mypid) {
		    if (killpg(np->p_jobid, SIGHUP) != -1) {
			/* In case the job was suspended... */
#ifdef SIGCONT
			(void) killpg(np->p_jobid, SIGCONT);
#endif
			break;
		    }
		}
	    while ((np = np->p_friends) != pp);
	}
    }
    untty();
#ifdef NLS_CATALOGS
    /*
     * We need to call catclose, because SVR4 leaves symlinks behind otherwise
     * in the catalog directories. We cannot close on a vforked() child,
     * because messages will stop working on the parent too.
     */
    if (child == 0)
	nlsclose();
#endif /* NLS_CATALOGS */
#ifdef WINNT_NATIVE
    nt_cleanup();
#endif /* WINNT_NATIVE */
    _exit(i);
}

#ifndef _PATH_DEFPATH
static Char **
defaultpath(void)
{
    char   *ptr;
    Char  **blk, **blkp;
    struct stat stb;

    blkp = blk = xmalloc(sizeof(Char *) * 10);

#ifndef NODOT
# ifndef DOTLAST
    *blkp++ = Strsave(STRdot);
# endif
#endif

#define DIRAPPEND(a)  \
	if (stat(ptr = a, &stb) == 0 && S_ISDIR(stb.st_mode)) \
		*blkp++ = SAVE(ptr)

#ifdef _PATH_LOCAL
    DIRAPPEND(_PATH_LOCAL);
#endif

#ifdef _PATH_USRUCB
    DIRAPPEND(_PATH_USRUCB);
#endif

#ifdef _PATH_USRBSD
    DIRAPPEND(_PATH_USRBSD);
#endif

#ifdef _PATH_BIN
    DIRAPPEND(_PATH_BIN);
#endif

#ifdef _PATH_USRBIN
    DIRAPPEND(_PATH_USRBIN);
#endif

#undef DIRAPPEND

#ifndef NODOT
# ifdef DOTLAST
    *blkp++ = Strsave(STRdot);
# endif
#endif
    *blkp = NULL;
    return (blk);
}
#endif

static void
record(void)
{
    if (!fast) {
	recdirs(NULL, adrof(STRsavedirs) != NULL);
	rechist(NULL, adrof(STRsavehist) != NULL);
    }
    displayHistStats("Exiting");	/* no-op unless DEBUG_HIST */
}

/*
 * Grab the tty repeatedly, and give up if we are not in the correct
 * tty process group.
 */
int
grabpgrp(int fd, pid_t desired)
{
    struct sigaction old;
    pid_t pgrp;
    size_t i;

    for (i = 0; i < 100; i++) {
	if ((pgrp = tcgetpgrp(fd)) == -1)
	    return -1;
	if (pgrp == desired)
	    return 0;
	(void)sigaction(SIGTTIN, NULL, &old);
	(void)signal(SIGTTIN, SIG_DFL);
	(void)kill(0, SIGTTIN);
	(void)sigaction(SIGTTIN, &old, NULL);
    }
    errno = EPERM;
    return -1;
}
