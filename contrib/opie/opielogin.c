/* opielogin.c: The infamous /bin/login

%%% portions-copyright-cmetz-96
Portions of this software are Copyright 1996-1999 by Craig Metz, All Rights
Reserved. The Inner Net License Version 2 applies to these portions of
the software.
You should have received a copy of the license with this software. If
you didn't get a copy, you may request one from <license@inner.net>.

Portions of this software are Copyright 1995 by Randall Atkinson and Dan
McDonald, All Rights Reserved. All Rights under this copyright are assigned
to the U.S. Naval Research Laboratory (NRL). The NRL Copyright Notice and
License Agreement applies to this software.

	History:

	Modified by cmetz for OPIE 2.4. Omit "/dev/" in lastlog entry.
		Don't chdir for invalid users. Fixed bug where getloginname()
		didn't actually change spaces to underscores. Use struct
		opie_key for key blocks. Do the home directory chdir() after
		doing the setuid() in case we're on superuser-mapped NFS.
		Initialize some variables explicitly. Call opieverify() if
		login times out. Use opiestrncpy().	
	Modified by cmetz for OPIE 2.32. Partially handle environment
		variables on the command line (a better implementation is
		coming soon). Handle failure to issue a challenge more
		gracefully.
	Modified by cmetz for OPIE 2.31. Use _PATH_NOLOGIN. Move Solaris
	        drain bamage kluge after rflag check; it breaks rlogin.
		Use TCSAFLUSH instead of TCSANOW (except where it flushes
		data we need). Sleep before kluging for Solaris.
	Modified by cmetz for OPIE 2.3. Process login environment files.
	        Made logindevperm/fbtab handling more generic. Kluge around
                Solaris drain bamage differently (maybe better?). Maybe
		allow cleartext logins even when opiechallenge() fails.
		Changed the conditions on when time.h and sys/time.h are
		included. Send debug info to syslog. Use opielogin() instead
		of dealing with utmp/setlogin() here.
	Modified by cmetz for OPIE 2.22. Call setlogin(). Decreased default
	        timeout to two minutes. Use opiereadpass() flags to get
		around Solaris drain bamage.
	Modified by cmetz for OPIE 2.21. Took the sizeof() the wrong thing.
        Modified by cmetz for OPIE 2.2. Changed prompts to ask for OTP
                response where appropriate. Simple though small speed-up.
                Don't allow cleartext if echo on. Don't try to clear
                non-blocking I/O. Use opiereadpass(). Don't mess with
                termios (as much, at least) -- that's opiereadpass()'s
                job. Change opiereadpass() calls to add echo arg. Fixed
                CONTROL macro. Don't modify argv (at least, unless
                we have a reason to). Allow user in if ruserok() says
                so. Removed useless strings (I don't think that
                removing the ucb copyright one is a problem -- please
                let me know if I'm wrong). Use FUNCTION declaration et
                al. Moved definition of TRUE here. Ifdef around more
                headers. Make everything static. Removed support for
                omitting domain name if same domain -- it generally
                didn't work and it would be a big portability problem.
                Use opiereadpass() in getloginname() and then post-
                process. Added code to grab hpux time zone from
                /etc/src.sh. Renamed MAIL_DIR to PATH_MAIL. Removed
                dupe catchexit and extraneous closelog. openlog() as
                soon as possible because SunOS syslog is broken.
                Don't print an extra blank line before a new Response
                prompt.
        Modified at NRL for OPIE 2.2. Changed strip_crlf to stripcrlf.
                Do opiebackspace() on entries.
	Modified at NRL for OPIE 2.1. Since we don't seem to use the
	        result of opiechallenge() anymore, discard it. Changed
		BSD4_3 to HAVE_GETTTYNAM. Other symbol changes for
		autoconf. Removed obselete usage comment. Removed
		des_crypt.h. File renamed to opielogin.c. Added bletch
		for setpriority. Added slash between MAIL_DIR and name.
        Modified at NRL for OPIE 2.02. Flush stdio after printing login
                prompt. Fixed Solaris shadow password problem introduced
                in OPIE 2.01 (the shadow password structure is spwd, not
                spasswd).
        Modified at NRL for OPIE 2.01. Changed password lookup handling
                to use a static structure to avoid problems with drain-
                bamaged shadow password packages. Make sure to close
                syslog by function to avoid problems with drain bamaged
                syslog implementations. Log a few interesting errors.
	Modified at NRL for OPIE 2.0.
	Modified at Bellcore for the Bellcore S/Key Version 1 software
		distribution.
	Originally from BSD.
*/
/*
 * Portions of this software are
 * Copyright (c) 1980,1987 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */

#include "opie_cfg.h"	/* OPIE: defines symbols for filenames & pathnames */
#if HAVE_SYS_PARAM_H
#include <sys/param.h>
#endif /* HAVE_SYS_PARAM_H */
#include <sys/stat.h>
#include <sys/types.h>

#if HAVE_SETPRIORITY && HAVE_SYS_RESOURCE_H
#include <sys/resource.h>
#endif /* HAVE_SETPRIORITY && HAVE_SYS_RESOURCE_H */

#if TIME_WITH_SYS_TIME
# include <sys/time.h>
# include <time.h>
#else /* TIME_WITH_SYS_TIME */
#if HAVE_SYS_TIME_H
#include <sys/time.h>
#else /* HAVE_SYS_TIME_H */
#include <time.h>
#endif /* HAVE_SYS_TIME_H */
#endif /* TIME_WITH_SYS_TIME */

#if HAVE_SYS_FILE_H
#include <sys/file.h>
#endif /* HAVE_SYS_FILE_H */
#include <signal.h>
#if HAVE_PWD_H
#include <pwd.h>	/* POSIX Password routines */
#endif /* HAVE_PWD_H */
#include <stdio.h>
#include <errno.h>
#if HAVE_UNISTD_H
#include <unistd.h>	/* Basic POSIX macros and functions */
#endif /* HAVE_UNISTD_H */
#include <termios.h>	/* POSIX terminal I/O */
#if HAVE_STRING_H
#include <string.h>	/* ANSI C string functions */
#endif /* HAVE_STRING_H */
#include <fcntl.h>	/* File I/O functions */
#include <syslog.h>
#include <grp.h>
#include <netdb.h>
#include <netinet/in.h>	/* contains types needed for next include file */
#include <arpa/inet.h>	/* Inet addr<-->ascii functions */
#if HAVE_STDLIB_H
#include <stdlib.h>
#endif /* HAVE_STDLIB_H */
#if HAVE_SYS_SELECT_H
#include <sys/select.h>
#endif /* HAVE_SYS_SELECT_H */

#ifdef	QUOTA
#include <sys/quota.h>
#endif

#if HAVE_GETTTYNAM
#include <sys/ioctl.h>	/* non-portable routines used only a few places */
#include <ttyent.h>
#endif /* HAVE_GETTTYNAM */

#include "opie.h"

#define TTYGID(gid)	tty_gid(gid)	/* gid that owns all ttys */

#define NMAX	32
#define HMAX	256

#if HAVE_LASTLOG_H
#include <lastlog.h>
#endif /* HAVE_LASTLOG_H */

static int rflag = 0;
static int usererr = -1;
static int stopmotd = 0;
static char rusername[NMAX + 1];
static char name[NMAX + 1] = "";
static char minusnam[16] = "-";
static char *envinit[1];	/* now set by setenv calls */
static char term[64] = "";	/* important to initialise to a NULL string */
static char host[HMAX + 1] = "";
static struct passwd nouser;
static struct passwd thisuser;

#if HAVE_SHADOW_H
#include <shadow.h>
#endif /* HAVE_SHADOW_H */

static char *ttyprompt;

#ifdef PERMSFILE
extern char *home;
#endif	/* PERMSFILE */

static struct termios attr;

extern int errno;

static int ouroptind;
static char *ouroptarg;

#if HAVE_LASTLOG_H
#ifndef _PATH_LASTLOG
#define _PATH_LASTLOG "/var/adm/lastlog"
#endif /* _PATH_LASTLOG */

static char lastlog[] = _PATH_LASTLOG;
#endif /* HAVE_LASTLOG_H */

/*
 * The "timeout" variable bounds the time given to login.
 * We initialize it here for safety and so that it can be
 * patched on machines where the default value is not appropriate.
 */
static int timeout = 120;

static void getstr __P((char *, int, char *));

#if HAVE_CRYPT_H
#include <crypt.h>
#endif /* HAVE_CRYPT_H */

#undef TRUE
#define TRUE -1

static int need_opieverify = 0;
static struct opie opie;

#ifdef TIOCSWINSZ
/* Windowing variable relating to JWINSIZE/TIOCSWINSZ/TIOCGWINSZ. This is
available on BSDish systems and at least Solaris 2.x, but portability to
other systems is questionable. Use within this source code module is
protected by suitable defines.

I'd be interested in hearing about a more portable approach. rja */

static struct winsize win = {0, 0, 0, 0};
#endif


/*------------------ BEGIN REAL CODE --------------------------------*/

/* We allow the malloc()s to potentially leak data out because we can
only call this routine about four times in the lifetime of this process
and the kernel will free all heap memory when we exit or exec. */
static int lookupuser FUNCTION_NOARGS
{
  struct passwd *pwd;
#if HAVE_SHADOW
  struct spwd *spwd;
#endif /* HAVE_SHADOW */

  memcpy(&thisuser, &nouser, sizeof(thisuser));

  if (!(pwd = getpwnam(name)))
    return -1;

  thisuser.pw_uid = pwd->pw_uid;
  thisuser.pw_gid = pwd->pw_gid;

  if (!(thisuser.pw_name = malloc(strlen(pwd->pw_name) + 1)))
    goto lookupuserbad;
  strcpy(thisuser.pw_name, pwd->pw_name);

  if (!(thisuser.pw_dir = malloc(strlen(pwd->pw_dir) + 1)))
    goto lookupuserbad;
  strcpy(thisuser.pw_dir, pwd->pw_dir);

  if (!(thisuser.pw_shell = malloc(strlen(pwd->pw_shell) + 1)))
    goto lookupuserbad;
  strcpy(thisuser.pw_shell, pwd->pw_shell);

#if HAVE_SHADOW
  if (!(spwd = getspnam(name)))
	goto lookupuserbad;

  pwd->pw_passwd = spwd->sp_pwdp;

  endspent();
#endif /* HAVE_SHADOW */

  if (!(thisuser.pw_passwd = malloc(strlen(pwd->pw_passwd) + 1)))
    goto lookupuserbad;
  strcpy(thisuser.pw_passwd, pwd->pw_passwd);

  endpwent();

  return ((thisuser.pw_passwd[0] == '*') || (thisuser.pw_passwd[0] == '#'));

lookupuserbad:
  memcpy(&thisuser, &nouser, sizeof(thisuser));
  return -1;
}

static VOIDRET getloginname FUNCTION_NOARGS
{
  char *namep, d;
  int flags;
  static int first = 1;

  memset(name, 0, sizeof(name));

  d = 0;
  while (name[0] == '\0') {
    flags = 1;
    if (ttyprompt) {
      if (first) {
	flags = 4;
	first--;
      } else
	printf(ttyprompt);
    } else
      printf("login: ");
    fflush(stdout);
    if (++d == 3)
      exit(0);
    if (!opiereadpass(name, sizeof(name)-1, flags)) {
      syslog(LOG_CRIT, "End-of-file (or other error?) on stdin!");
      exit(0);
    }
    for (namep = name; *namep; namep++) {
      if (*namep == ' ')
        *namep = '_'; 
    }
  }
}

static VOIDRET timedout FUNCTION((i), int i)
{
  /* input variable declared just to keep the compiler quiet */
  printf("Login timed out after %d seconds\n", timeout);
  syslog(LOG_CRIT, "Login timed out after %d seconds!", timeout);

  if (need_opieverify)
    opieverify(&opie, NULL);

  exit(0);
}

#if !HAVE_MOTD_IN_PROFILE
static VOIDRET catch FUNCTION((i), int i)
{
  /* the input variable is declared to keep the compiler quiet */
  signal(SIGINT, SIG_IGN);
  stopmotd++;
}
#endif /* !HAVE_MOTD_IN_PROFILE */

static VOIDRET catchexit FUNCTION_NOARGS
{
  int i;
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &attr);
  putchar('\n');
  closelog();
  for (i = sysconf(_SC_OPEN_MAX); i > 2; i--)
    close(i);
}

static int rootterm FUNCTION((ttyn), char *ttyn)
{
#if HAVE_GETTTYNAM
/* The getttynam() call and the ttyent structure first appeared in 4.3 BSD and
are not portable to System V systems such as Solaris 2.x. or modern versions
of IRIX rja */
  register struct ttyent *t;
  char *tty;

  tty = strrchr(ttyn, '/');

  if (tty == NULL)
    tty = ttyn;
  else
    tty++;

  if ((t = getttynam(tty)) != NULL)
    return (t->ty_status & TTY_SECURE);

  return (1);	/* when in doubt, allow root logins */

#elif HAVE_ETC_DEFAULT_LOGIN

  FILE *filno;
  char line[128];
  char *next, *next2;

/* SVR4 only permits two security modes for root logins: 1) only from CONSOLE,
if the string "CONSOLE=/dev/console" exists and is not commented out with "#"
characters, or 2) from anywhere.

So we open /etc/default/login file grab the file contents one line at a time
verify that the line being tested isn't commented out check for the substring
"CONSOLE" and decide whether to permit this attempted root login/su. */

  if ((filno = fopen("/etc/default/login", "r")) != NULL) {
    while (fgets(line, 128, filno) != NULL) {
      next = line;

      if ((line[0] != '#') && (next = strstr(line, "CONSOLE"))) {
	next += 7;	/* get past the string "CONSOLE" */

	while (*next && (*next == ' ') || (*next == '\t'))
	  next++;

	if (*(next++) != '=')
	  break;	/* some weird character, get next line */

	next2 = next;
	while (*next2 && (*next2 != '\t') && (*next2 != ' ') &&
	       (*next2 != '\n'))
	  next2++;
	*next2 = 0;

	return !strcmp(ttyn, next);	/* Allow the login if and only if the
					   user's terminal line matches the
					   setting for CONSOLE */
      }
    }	/* end while another line could be obtained */
  }	/* end if could open file */
  return (1);	/* when no CONSOLE line exists, root can login from anywhere */
#elif HAVE_SECURETTY
  {
    FILE *f;
    char buffer[1024], *c;
    int rc = 0;

    if (!(f = fopen("/etc/securetty", "r")))
      return 1;

    if (c = strstr(ttyn, "/dev/"))
      ttyn += 5;

    if (c = strrchr(ttyn, '/'))
      ttyn = ++c;

    while (fgets(buffer, sizeof(buffer), f)) {
      if (c = strrchr(buffer, '\n'))
	*c = 0;

      if (!(c = strrchr(buffer, '/')))
	c = buffer;
      else
	c++;

      if (!strcmp(c, ttyn))
	rc = 1;
    };

    fclose(f);
    return rc;
  }
#else
  return (1);	/* when in doubt, allow root logins */
#endif
}

static int doremotelogin FUNCTION((host), char *host)
{
  int rc;

  getstr(rusername, sizeof(rusername), "remuser");
  getstr(name, sizeof(name), "locuser");
  getstr(term, sizeof(term), "Terminal type");
  if (getuid()) {
    memcpy(&thisuser, &nouser, sizeof(thisuser));
    syslog(LOG_ERR, "getuid() failed");
    return (-1);
  }
  if (lookupuser()) {
    syslog(LOG_ERR, "lookup failed for user %s", name);
    return (-1);
  }
  rc = ruserok(host, !thisuser.pw_uid, rusername, name);
  if (rc == -1) {
    syslog(LOG_ERR,
    "ruserok failed, host=%s, uid=%d, remote username=%s, local username=%s",
	   host, thisuser.pw_uid, rusername, name);
  }
  return rc;
}


static VOIDRET getstr FUNCTION((buf, cnt, err), char *buf AND int cnt AND char *err)
{
  char c;

  do {
    if (read(0, &c, 1) != 1)
      exit(1);
    if (--cnt < 0) {
      printf("%s too long\r\n", err);
      syslog(LOG_CRIT, "%s too long", err);
      exit(1);
    }
    *buf++ = c;
  }
  while ((c != 0) && (c != '~'));
}

struct speed_xlat {
  char *c;
  int i;
}          speeds[] = {

#ifdef B0
  {
    "0", B0
  },
#endif	/* B0 */
#ifdef B50
  {
    "50", B50
  },
#endif	/* B50 */
#ifdef B75
  {
    "75", B75
  },
#endif	/* B75 */
#ifdef B110
  {
    "110", B110
  },
#endif	/* B110 */
#ifdef B134
  {
    "134", B134
  },
#endif	/* B134 */
#ifdef B150
  {
    "150", B150
  },
#endif	/* B150 */
#ifdef B200
  {
    "200", B200
  },
#endif	/* B200 */
#ifdef B300
  {
    "300", B300
  },
#endif	/* B300 */
#ifdef B600
  {
    "600", B600
  },
#endif	/* B600 */
#ifdef B1200
  {
    "1200", B1200
  },
#endif	/* B1200 */
#ifdef B1800
  {
    "1800", B1800
  },
#endif	/* B1800 */
#ifdef B2400
  {
    "2400", B2400
  },
#endif	/* B2400 */
#ifdef B4800
  {
    "4800", B4800
  },
#endif	/* B4800 */
#ifdef B7200
  {
    "7200", B7200
  },
#endif	/* B7200 */
#ifdef B9600
  {
    "9600", B9600
  },
#endif	/* B9600 */
#ifdef B14400
  {
    "14400", B14400
  },
#endif	/* B14400 */
#ifdef B19200
  {
    "19200", B19200
  },
#endif	/* B19200 */
#ifdef B28800
  {
    "28800", B28800
  },
#endif	/* B28800 */
#ifdef B38400
  {
    "38400", B38400
  },
#endif	/* B38400 */
#ifdef B57600
  {
    "57600", B57600
  },
#endif	/* B57600 */
#ifdef B115200
  {
    "115200", B115200
  },
#endif	/* B115200 */
#ifdef B230400
  {
    "230400", B230400
  },
#endif	/* 230400 */
  {
    NULL, 0
  }
};

static VOIDRET doremoteterm FUNCTION((term), char *term)
{
  register char *cp = strchr(term, '/');
  char *speed;
  struct speed_xlat *x;

  if (cp) {
    *cp++ = '\0';
    speed = cp;
    cp = strchr(speed, '/');
    if (cp)
      *cp++ = '\0';
    for (x = speeds; x->c != NULL; x++)
      if (strcmp(x->c, speed) == 0) {
	cfsetispeed(&attr, x->i);
	cfsetospeed(&attr, x->i);
	break;
      }
  }
}

static int tty_gid FUNCTION((default_gid), int default_gid)
{
  struct group *gr;
  int gid = default_gid;

  gr = getgrnam(TTYGRPNAME);
  if (gr != (struct group *) 0)
    gid = gr->gr_gid;
  endgrent();
  return (gid);
}

int main FUNCTION((argc, argv), int argc AND char *argv[])
{
  extern char **environ;
  register char *namep;

  int invalid, quietlog;
  FILE *nlfd;
  char *tty, host[256];
  int pflag = 0, hflag = 0, fflag = 0;
  int t, c;
  int i;
  char *p;
  char opieprompt[OPIE_CHALLENGE_MAX + 1];
  int af_pwok;
  int authsok = 0;
  char *pp;
  char buf[256];
  int uid;
  int opiepassed;

#ifndef DEBUG
  if (geteuid()) {
    fprintf(stderr, "This program requires super-user privileges.\n");
    exit(1);
  }
#endif /* DEBUG */

  for (t = sysconf(_SC_OPEN_MAX); t > 2; t--)
    close(t);

  openlog("login", LOG_ODELAY, LOG_AUTH);

  /* initialisation */
  host[0] = '\0';
  opieprompt[0] = '\0';

  if (p = getenv("TERM")) {
#ifdef DEBUG
    syslog(LOG_DEBUG, "environment TERM=%s", p);
#endif /* DEBUG */
    opiestrncpy(term, p, sizeof(term));
  };
  
  memset(&nouser, 0, sizeof(nouser));
  nouser.pw_uid = -1;
  nouser.pw_gid = -1;
  nouser.pw_passwd = "#nope";
  nouser.pw_name = nouser.pw_gecos = nouser.pw_dir = nouser.pw_shell = "";

#if HAVE_SETPRIORITY && HAVE_SYS_RESOURCE_H
  setpriority(PRIO_PROCESS, 0, 0);
#endif /* HAVE_SETPRIORITY && HAVE_SYS_RESOURCE_H */

  signal(SIGALRM, timedout);
  alarm(timeout);
  signal(SIGQUIT, SIG_IGN);
  signal(SIGINT, SIG_IGN);

#if DOTTYPROMPT
  ttyprompt = (char *) getenv("TTYPROMPT");
#endif /* TTYPROMPT */

#ifdef	QUOTA
  quota(Q_SETUID, 0, 0, 0);
#endif

#ifdef DEBUG
  syslog(LOG_DEBUG, "my args are: (argc=%d)", i = argc);
  while (--i)
    syslog(LOG_DEBUG, "%d: %s", i, argv[i]);
#endif /* DEBUG */

/* Implement our own getopt()-like functionality, but do so in a much more
   strict manner to prevent security problems. */
  for (ouroptind = 1; ouroptind < argc; ouroptind++) {
    if (!argv[ouroptind])
      continue;

    if (argv[ouroptind][0] == '-') {
      char *c = argv[ouroptind] + 1;

      while(*c) {
	switch(*(c++)) {
	  case 'd':
	    if (*c || (++ouroptind == argc))
	      exit(1);

/*    The '-d' option is apparently a performance hack to get around
   ttyname() being slow. The potential does exist for it to be used
   for malice, and it does not seem to be strictly necessary, so we
   will just eat it. */
	    break;

	  case 'r':
	    if (rflag || hflag || fflag) {
	      fprintf(stderr, "Other options not allowed with -r\n");
	      exit(1);
	    }

	    if (*c || (++ouroptind == argc))
	      exit(1);

	    if (!(ouroptarg = argv[ouroptind]))
	      exit(1);

	    rflag = -1;
	    if (!doremotelogin(ouroptarg))
	      rflag = 1;
	    
	    opiestrncpy(host, ouroptarg, sizeof(host));
	    break;

	  case 'h':
	    if (!getuid()) {
	      if (rflag || hflag || fflag) {
		fprintf(stderr, "Other options not allowed with -h\n");
		exit(1);
	      }
	      hflag = 1;

	      if (*c || (++ouroptind == argc))
		exit(1);

	      if (!(ouroptarg = argv[ouroptind]))
		exit(1);
	      
	      opiestrncpy(host, ouroptarg, sizeof(host));
	    }
	    break;

	  case 'f':
	    if (rflag) {
	      fprintf(stderr, "Only one of -r and -f allowed\n");
	      exit(1);
	    }
	    fflag = 1;

	    if (*c || (++ouroptind == argc))
	      exit(1);

	    if (!(ouroptarg = argv[ouroptind]))
	      exit(1);

	    opiestrncpy(name, ouroptarg, sizeof(name));
	    break;
	  case 'p':
	    pflag = 1;
	    break;
	};
      };
      continue;
    };

    if (strchr(argv[ouroptind], '=')) {
      if (!strncmp(argv[ouroptind], "TERM=", 5)) {
	opiestrncpy(term, &(argv[ouroptind][5]), sizeof(term));

#ifdef DEBUG
	syslog(LOG_DEBUG, "passed TERM=%s, ouroptind = %d", term, ouroptind);
#endif /* DEBUG */
      } else {
#ifdef DEBUG
	syslog(LOG_DEBUG, "eating %s, ouroptind = %d", argv[ouroptind], ouroptind);
#endif /* DEBUG */
      };
      continue;
    };

    opiestrncpy(name, argv[ouroptind], sizeof(name));
  };

#ifdef TIOCNXCL
  /* BSDism:  not sure how to rewrite for POSIX.  rja */
  ioctl(0, TIOCNXCL, 0);	/* set non-exclusive use of tty */
#endif

  /* get original termio attributes */
  if (tcgetattr(STDIN_FILENO, &attr) != 0)
    return (-1);

/* If talking to an rlogin process, propagate the terminal type and baud rate
   across the network. */
  if (rflag)
    doremoteterm(term);
  else {
    struct termios termios;
    fd_set fds;
    struct timeval timeval;
    
    memset(&timeval, 0, sizeof(struct timeval));
    
    FD_ZERO(&fds);
    FD_SET(0, &fds);

#if HAVE_USLEEP
    usleep(1);
#endif /* HAVE_USLEEP */

    if (select(1, &fds, NULL, NULL, &timeval)) {
#ifdef DEBUG
      syslog(LOG_DEBUG, "reading user name from tty buffer");
#endif /* DEBUG */

      if (tcgetattr(0, &termios)) {
#ifdef DEBUG
	syslog(LOG_DEBUG, "tcgetattr(0, &termios) failed");
#endif /* DEBUG */
	exit(1);
      }
      
      termios.c_lflag &= ~ECHO;
   
      if (tcsetattr(0, TCSANOW, &termios)) {
#ifdef DEBUG
	syslog(LOG_DEBUG, "tcsetattr(0, &termios) failed");
#endif /* DEBUG */
	exit(1);
      }

      if ((i = read(0, name, sizeof(name)-1)) > 0)
	name[i] = 0;
      if ((p = strchr(name, '\r')))
        *p = 0;
      if ((p = strchr(name, '\n')))
        *p = 0;
    }
  }

/* Force termios portable control characters to the system default values as
specified in termios.h. This should help the one-time password login feel the
same as the vendor-supplied login. Common extensions are also set for
completeness, but these are set within appropriate defines for portability. */

#define CONTROL(x) (x - 64)

#ifdef VEOF
#ifdef CEOF
  attr.c_cc[VEOF] = CEOF;
#else	/* CEOF */
  attr.c_cc[VEOF] = CONTROL('D');
#endif	/* CEOF */
#endif	/* VEOF */
#ifdef VEOL
#ifdef CEOL
  attr.c_cc[VEOL] = CEOL;
#else	/* CEOL */
  attr.c_cc[VEOL] = CONTROL('J');
#endif	/* CEOL */
#endif	/* VEOL */
#ifdef VERASE
#ifdef CERASE
  attr.c_cc[VERASE] = CERASE;
#else	/* CERASE */
  attr.c_cc[VERASE] = CONTROL('H');
#endif	/* CERASE */
#endif	/* VERASE */
#ifdef VINTR
#ifdef CINTR
  attr.c_cc[VINTR] = CINTR;
#else	/* CINTR */
  attr.c_cc[VINTR] = CONTROL('C');
#endif	/* CINTR */
#endif	/* VINTR */
#ifdef VKILL
#ifdef CKILL
  attr.c_cc[VKILL] = CKILL;
#else	/* CKILL */
  attr.c_cc[VKILL] = CONTROL('U');
#endif	/* CKILL */
#endif	/* VKILL */
#ifdef VQUIT
#ifdef CQUIT
  attr.c_cc[VQUIT] = CQUIT;
#else	/* CQUIT */
  attr.c_cc[VQUIT] = CONTROL('\\');
#endif	/* CQUIT */
#endif	/* VQUIT */
#ifdef VSUSP
#ifdef CSUSP
  attr.c_cc[VSUSP] = CSUSP;
#else	/* CSUSP */
  attr.c_cc[VSUSP] = CONTROL('Z');
#endif	/* CSUSP */
#endif	/* VSUSP */
#ifdef VSTOP
#ifdef CSTOP
  attr.c_cc[VSTOP] = CSTOP;
#else	/* CSTOP */
  attr.c_cc[VSTOP] = CONTROL('S');
#endif	/* CSTOP */
#endif	/* VSTOP */
#ifdef VSTART
#ifdef CSTART
  attr.c_cc[VSTART] = CSTART;
#else	/* CSTART */
  attr.c_cc[VSTART] = CONTROL('Q');
#endif	/* CSTART */
#endif	/* VSTART */
#ifdef VDSUSP
#ifdef CDSUSP
  attr.c_cc[VDSUSP] = CDSUSP;
#else	/* CDSUSP */
  attr.c_cc[VDSUSP] = 0;
#endif	/* CDSUSP */
#endif	/* VDSUSP */
#ifdef VEOL2
#ifdef CEOL2
  attr.c_cc[VEOL2] = CEOL2;
#else	/* CEOL2 */
  attr.c_cc[VEOL2] = 0;
#endif	/* CEOL2 */
#endif	/* VEOL2 */
#ifdef VREPRINT
#ifdef CRPRNT
  attr.c_cc[VREPRINT] = CRPRNT;
#else	/* CRPRNT */
  attr.c_cc[VREPRINT] = 0;
#endif	/* CRPRNT */
#endif	/* VREPRINT */
#ifdef VWERASE
#ifdef CWERASE
  attr.c_cc[VWERASE] = CWERASE;
#else	/* CWERASE */
  attr.c_cc[VWERASE] = 0;
#endif	/* CWERASE */
#endif	/* VWERASE */
#ifdef VLNEXT
#ifdef CLNEXT
  attr.c_cc[VLNEXT] = CLNEXT;
#else	/* CLNEXT */
  attr.c_cc[VLNEXT] = 0;
#endif	/* CLNEXT */
#endif	/* VLNEXT */

  attr.c_lflag |= ICANON;	/* enable canonical input processing */
  attr.c_lflag &= ~ISIG;	/* disable INTR, QUIT,& SUSP signals */
  attr.c_lflag |= (ECHO | ECHOE);	/* enable echo and erase */
#ifdef ONLCR
  /* POSIX does not specify any output processing flags, but the usage below
     is SVID compliant and is generally portable to modern versions of UNIX. */
  attr.c_oflag |= ONLCR;	/* map CR to CRNL on output */
#endif
#ifdef ICRNL
  attr.c_iflag |= ICRNL;
#endif	/* ICRNL */

  attr.c_oflag |= OPOST;
  attr.c_lflag |= ICANON;	/* enable canonical input */
  attr.c_lflag |= ECHO;
  attr.c_lflag |= ECHOE;	/* enable ERASE character */
  attr.c_lflag |= ECHOK;	/* enable KILL to delete line */
  attr.c_cflag |= HUPCL;	/* hangup on close */

  /* Set revised termio attributes */
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &attr))
    return (-1);

  atexit(catchexit);

  tty = ttyname(0);

  if (tty == (char *) 0 || *tty == '\0')
    tty = "UNKNOWN";	/* was: "/dev/tty??" */

#if HAVE_SETVBUF && defined(_IONBF)
#if SETVBUF_REVERSED
  setvbuf(stdout, _IONBF, NULL, 0);
  setvbuf(stderr, _IONBF, NULL, 0);
#else /* SETVBUF_REVERSED */
  setvbuf(stdout, NULL, _IONBF, 0);
  setvbuf(stderr, NULL, _IONBF, 0);
#endif /* SETVBUF_REVERSED */
#endif /* HAVE_SETVBUF && defined(_IONBF) */

#ifdef DEBUG
  syslog(LOG_DEBUG, "tty = %s", tty);
#endif /* DEBUG */

#ifdef HAVE_LOGIN_ENVFILE
  {
    FILE *f;

    if (f = fopen(HAVE_LOGIN_ENVFILE, "r")) {
      char line[128], *c, *c2;

      while(fgets(line, sizeof(line)-1, f)) {
	c = line;
	while(*c && (isalnum(*c) || (*c == '_'))) c++;
	  if (*c == '=') {
	    *(c++) = 0;
	    if (c2 = strchr(c, ';'))
	      *c2 = 0;
	    if (c2 = strchr(c, '\n'))
	      *c2 = 0;
	    if (c2 = strchr(c, ' '))
	      continue;
	    if (c2 = strchr(c, '\t'))
	      continue;
	    if (!strcmp(line, "TZ"))
	      continue;
	    if (setenv(line, c, 1) < 0) {
	      fprintf(stderr, "setenv() failed -- environment full?\n");
	      break;
	    }
	  }
        }
      fclose(f);
    }
  }
#endif /* HAVE_LOGIN_ENVFILE */

  t = 0;
  invalid = TRUE;
  af_pwok = opieaccessfile(host);

  if (name[0])
    if (name[0] == '-') {
      fprintf(stderr, "User names can't start with '-'.\n");
      syslog(LOG_AUTH, "Attempt to use invalid username: %s.", name);
      exit(1);
    } else
      invalid = lookupuser();

  do {
    /* If remote login take given name, otherwise prompt user for something. */
    if (invalid && !name[0]) {
      getloginname();
      invalid = lookupuser();
      authsok = 0;
    }
#ifdef DEBUG
    syslog(LOG_DEBUG, "login name is +%s+, of length %d, [0] = %d", name, strlen(name), name[0]);
#endif /* DEBUG */

    if (fflag) {
      uid = getuid();

      if (uid != 0 && uid != thisuser.pw_uid)
	fflag = 0;
      /* Disallow automatic login for root. */
      if (thisuser.pw_uid == 0)
	fflag = 0;
    }
    if (feof(stdin))
      exit(0);

    /* If no remote login authentication and a password exists for this user,
       prompt for and verify a password. */
    if (!fflag && (rflag < 1) && *thisuser.pw_passwd) {
#ifdef DEBUG
      syslog(LOG_DEBUG, "login name is +%s+, of length %d, [0] = %d\n", name, strlen(name), name[0]);
#endif /* DEBUG */

      /* Attempt a one-time password challenge */
      i = opiechallenge(&opie, name, opieprompt);
      need_opieverify = TRUE;

      if ((i < 0) || (i > 1)) {
        syslog(LOG_ERR, "error: opiechallenge() returned %d, errno=%d!\n", i, errno);
      } else {
        printf("%s\n", opieprompt);
	authsok |= 1;
      }

      if (!memcmp(&thisuser, &nouser, sizeof(thisuser)))
	if (host[0])
	  syslog(LOG_WARNING, "Invalid login attempt for %s on %s from %s.",
		 name, tty, host);
	else
	  syslog(LOG_WARNING, "Invalid login attempt for %s on %s.",
		 name, tty);

      if (af_pwok && opiealways(thisuser.pw_dir))
	authsok |= 2;

#if DEBUG
      syslog(LOG_DEBUG, "af_pwok = %d, authsok = %d", af_pwok, authsok);
#endif /* DEBUG */

      if (!authsok)
	syslog(LOG_ERR, "no authentication methods are available for %s!", name);

#if NEW_PROMPTS
      if ((authsok & 1) || !authsok)
        printf("Response");
      if (((authsok & 3) == 3) || !authsok)
        printf(" or ");
      if ((authsok & 2) || !authsok)
        printf("Password");
      printf(": ");
      fflush(stdout);
      if (!opiereadpass(buf, sizeof(buf), !(authsok & 2)))
        invalid = TRUE;
#else /* NEW_PROMPTS */
      if ((authsok & 3) == 1)
	printf("(OTP response required)\n");
      printf("Password:");
      fflush(stdout);
      if (!opiereadpass(buf, sizeof(buf), 0))
        invalid = TRUE;
#endif /* NEW_PROMPTS */

      if (!buf[0] && (authsok & 1)) {
        authsok &= ~2;
	/* Null line entered, so display appropriate prompt & flush current
	   data. */
#if NEW_PROMPTS
        printf("Response: ");
#else /* NEW_PROMPTS */
	printf(" (echo on)\nPassword:");
#endif /* NEW_PROMPTS */
        if (!opiereadpass(buf, sizeof(buf), 1))
          invalid = TRUE;
      }

      if (authsok & 1) {
        i = opiegetsequence(&opie);
        opiepassed = !opieverify(&opie, buf);
	need_opieverify = 0;

#ifdef DEBUG
      syslog(LOG_DEBUG, "opiepassed = %d", opiepassed);
#endif /* DEBUG */
      }

      if (!invalid) {
        if ((authsok & 1) && opiepassed) {
	  if (i < 10) {
	    printf("Warning: Re-initialize your OTP information");
            if (i < 5)
              printf(" NOW!");
            printf("\n");
	  }
        } else {
	  if (authsok & 2) {
	    pp = crypt(buf, thisuser.pw_passwd);
	    invalid = strcmp(pp, thisuser.pw_passwd);
	  } else
            invalid = TRUE;
	}
      }
    }

    /* If user not super-user, check for logins disabled. */
    if (thisuser.pw_uid) {
      if (nlfd = fopen(_PATH_NOLOGIN, "r")) {
	while ((c = getc(nlfd)) != EOF)
	  putchar(c);
	fflush(stdout);
	sleep(5);
	exit(0);
      }
    }
    /* If valid so far and root is logging in, see if root logins on this
       terminal are permitted. */
    if (!invalid && !thisuser.pw_uid && !rootterm(tty)) {
      if (host[0])
	syslog(LOG_CRIT, "ROOT LOGIN REFUSED ON %s FROM %.*s",
	       tty, HMAX, host);
      else
	syslog(LOG_CRIT, "ROOT LOGIN REFUSED ON %s", tty);
      invalid = TRUE;
    }
    /* If invalid, then log failure attempt data to appropriate system
       logfiles and close the connection. */
    if (invalid) {
      printf("Login incorrect\n");
      if (host[0])
	  syslog(LOG_ERR, "LOGIN FAILURE ON %s FROM %.*s, %.*s",
		 tty, HMAX, host, sizeof(name), name);
	else
	  syslog(LOG_ERR, "LOGIN FAILURE ON %s, %.*s", 
		 tty, sizeof(name), name);
      if (++t >= 5)
	exit(1);
    }
    if (*thisuser.pw_shell == '\0')
      thisuser.pw_shell = "/bin/sh";
    /* Remote login invalid must have been because of a restriction of some
       sort, no extra chances. */
    if (invalid) {
      if (!usererr)
	exit(1);
      name[0] = 0;
    }
  }
  while (invalid);
  /* Committed to login -- turn off timeout */
  alarm(0);

#ifdef	QUOTA
  if (quota(Q_SETUID, thisuser.pw_uid, 0, 0) < 0 && errno != EINVAL) {
    if (errno == EUSERS)
      printf("%s.\n%s.\n", "Too many users logged on already",
	     "Try again later");
    else
      if (errno == EPROCLIM)
	printf("You have too many processes running.\n");
      else
	perror("quota (Q_SETUID)");
    sleep(5);
    exit(0);
  }
#endif

  if (opielogin(tty, name, host))
    syslog(LOG_ERR, "can't record login: tty %s, name %s, host %s", tty, name, host);

  quietlog = !access(QUIET_LOGIN_FILE, F_OK);

#if HAVE_LASTLOG_H
  {
  int f;

  if ((f = open(lastlog, O_RDWR)) >= 0) {
    struct lastlog ll;

    lseek(f, (long)thisuser.pw_uid * sizeof(struct lastlog), 0);

    if ((sizeof(ll) == read(f, (char *) &ll, sizeof(ll))) &&
	(ll.ll_time != 0) && (!quietlog)) {
      printf("Last login: %.*s ",
	     24 - 5, (char *) ctime(&ll.ll_time));
      if (*ll.ll_host != '\0')
	printf("from %.*s\n", sizeof(ll.ll_host), ll.ll_host);
      else
	printf("on %.*s\n", sizeof(ll.ll_line), ll.ll_line);
    }
    lseek(f, (long)thisuser.pw_uid * sizeof(struct lastlog), 0);

    time(&ll.ll_time);
    if (!strncmp(tty, "/dev/", 5))
      opiestrncpy(ll.ll_line, tty + 5, sizeof(ll.ll_line));
    else
      opiestrncpy(ll.ll_line, tty, sizeof(ll.ll_line));
    opiestrncpy(ll.ll_host, host, sizeof(ll.ll_host));
    write(f, (char *) &ll, sizeof ll);
    close(f);
  }
  }
#endif /* HAVE_LASTLOG_H */

  chown(tty, thisuser.pw_uid, TTYGID(thisuser.pw_gid));

#ifdef TIOCSWINSZ
/* POSIX does not specify any interface to set/get window sizes, so this is
not portable.  It should work on most recent BSDish systems and the defines
should protect it on older System Vish systems.  It does work under Solaris
2.4, though it isn't clear how many other SVR4 systems support it. I'd be
interested in hearing of a more portable approach. rja */
  if (!hflag && !rflag)
    ioctl(0, TIOCSWINSZ, &win);	/* set window size to 0,0,0,0 */
#endif

  chmod(tty, 0622);
  setgid(thisuser.pw_gid);
  initgroups(name, thisuser.pw_gid);

#ifdef	QUOTA
  quota(Q_DOWARN, thisuser.pw_uid, (dev_t) - 1, 0);
#endif

#ifdef PERMSFILE
  home = thisuser.pw_dir;
  permsfile(name, tty, thisuser.pw_uid, thisuser.pw_gid);
  fflush(stderr);
#endif	/* PERMSFILE */

  setuid(thisuser.pw_uid);

  /* destroy environment unless user has asked to preserve it */
  if (!pflag)
    environ = envinit;
  setenv("HOME", thisuser.pw_dir, 1);
  setenv("SHELL", thisuser.pw_shell, 1);

  if (chdir(thisuser.pw_dir) < 0) {
#if DEBUG
    syslog(LOG_DEBUG, "chdir(%s): %s(%d)", thisuser.pw_dir, strerror(errno),
	   errno);
#endif /* DEBUG */
    if (chdir("/") < 0) {
      printf("No directory!\n");
      invalid = TRUE;
    } else {
      printf("No directory! %s\n", "Logging in with HOME=/");
      strcpy(thisuser.pw_dir, "/");
    }
  }

  if (!term[0]) {
#if HAVE_GETTTYNAM
/*
 * The getttynam() call and the ttyent structure first appeared in 4.3 BSD.
 * They are not portable to System V systems such as Solaris 2.x.
 *         rja
 */
  register struct ttyent *t;
  register char *c;

  if (c = strrchr(tty, '/'))
    c++;
  else
    c = tty;

  if (t = getttynam(c))
    opiestrncpy(term, t->ty_type, sizeof(term));
  else
#endif /* HAVE_GETTTYNAM */
    strcpy(term, "unknown");
  }

  setenv("USER", name, 1);
  setenv("LOGNAME", name, 1);
  setenv("PATH", DEFAULT_PATH, 0);
  if (term[0]) {
#ifdef DEBUG
    syslog(LOG_DEBUG, "setting TERM=%s", term);
#endif	/* DEBUG */
    setenv("TERM", term, 1);
  }

#ifdef HAVE_LOGIN_ENVFILE
  {
    FILE *f;

    if (f = fopen(HAVE_LOGIN_ENVFILE, "r")) {
      char line[128], *c, *c2;

      while(fgets(line, sizeof(line)-1, f)) {
	c = line;
	while(*c && (isalnum(*c) || (*c == '_'))) c++;
	  if (*c == '=') {
	    *(c++) = 0;
	    if (c2 = strchr(c, ';'))
	      *c2 = 0;
	    if (c2 = strchr(c, '\n'))
	      *c2 = 0;
	    if (c2 = strchr(c, ' '))
	      continue;
	    if (c2 = strchr(c, '\t'))
	      continue;
	    if (setenv(line, c, 0) < 0) {
	      fprintf(stderr, "setenv() failed -- environment full?\n");
	      break;
	    }
	  }
        }
      fclose(f);
    }
  }
#endif /* HAVE_LOGIN_ENVFILE */

  if ((namep = strrchr(thisuser.pw_shell, '/')) == NULL)
    namep = thisuser.pw_shell;
  else
    namep++;
  strcat(minusnam, namep);
  if (tty[sizeof("tty") - 1] == 'd')
    syslog(LOG_INFO, "DIALUP %s, %s", tty, name);
  if (!thisuser.pw_uid)
    if (host[0])
      syslog(LOG_NOTICE, "ROOT LOGIN %s FROM %.*s", tty, HMAX, host);
    else
      syslog(LOG_NOTICE, "ROOT LOGIN %s", tty);
#if !HAVE_MOTD_IN_PROFILE
  if (!quietlog) {
    FILE *mf;
    register c;

    signal(SIGINT, catch);
    if ((mf = fopen(MOTD_FILE, "r")) != NULL) {
      while ((c = getc(mf)) != EOF && !stopmotd)
	putchar(c);
      fclose(mf);
    }
    signal(SIGINT, SIG_IGN);
  }
#endif /* !HAVE_MOTD_IN_PROFILE */
#if !HAVE_MAILCHECK_IN_PROFILE
  if (!quietlog) {
    struct stat st;
    char buf[128];
    int len;

    opiestrncpy(buf, PATH_MAIL, sizeof(buf) - 2);

    len = strlen(buf);
    if (*(buf + len - 1) != '/') {
	*(buf + len) = '/';
	*(buf + len + 1) = 0;
    }

    strcat(buf, name);
#if DEBUG
    syslog(LOG_DEBUG, "statting %s", buf);
#endif /* DEBUG */
    if (!stat(buf, &st) && st.st_size)
      printf("You have %smail.\n",
	     (st.st_mtime > st.st_atime) ? "new " : "");
  }
#endif /* !HAVE_MAILCHECK_IN_PROFILE */
  signal(SIGALRM, SIG_DFL);
  signal(SIGQUIT, SIG_DFL);
  signal(SIGINT, SIG_DFL);
  signal(SIGTSTP, SIG_IGN);

  attr.c_lflag |= (ISIG | IEXTEN);

  catchexit();
  execlp(thisuser.pw_shell, minusnam, 0);
  perror(thisuser.pw_shell);
  printf("No shell\n");
  exit(0);
}
