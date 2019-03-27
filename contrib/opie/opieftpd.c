/* opieftpd.c: Main program for an FTP daemon.

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

	Modified by cmetz for OPIE 2.4. Add id parameter to opielogwtmp. Use
		opiestrncpy(). Fix incorrect use of setproctitle().
	Modified by cmetz for OPIE 2.32. Remove include of dirent.h here; it's
		done already (and conditionally) in opie_cfg.h.
	Modified by cmetz for OPIE 2.31. Merged in some 4.4BSD-Lite changes.
		Merged in a security fix to BSD-derived ftpds.
	Modified by cmetz for OPIE 2.3. Fixed the filename at the top.
		Moved LS_COMMAND here.
	Modified by cmetz for OPIE 2.2. Use FUNCTION definition et al.
                Removed useless strings (I don't think that removing the
                ucb copyright one is a problem -- please let me know if
                I'm wrong). Changed default CMASK to 077. Removed random
                comments. Use ANSI stdargs for reply/lreply if we can,
                added stdargs version of reply/lreply. Don't declare the
                tos variable unless IP_TOS defined. Include stdargs headers
                early. More headers ifdefed. Made everything static.
                Got rid of gethostname() call and use of hostname. Pared
                down status response for places where header files frequently
                cause trouble. Made logging of user logins (ala -l)
                non-optional. Moved reply()/lrepy(). Fixed some prototypes.
	Modified at NRL for OPIE 2.1. Added declaration of envp. Discard
	        result of opiechallenge (allows access control to work).
		Added patches for AIX. Symbol changes for autoconf.
        Modified at NRL for OPIE 2.01. Changed password lookup handling
                to avoid problems with drain-bamaged shadow password packages.
                Properly handle internal state for anonymous FTP. Unlock
                user accounts properly if login fails because of /etc/shells.
                Make sure to close syslog by function to avoid problems with
                drain bamaged syslog implementations.
	Modified at NRL for OPIE 2.0.
	Originally from BSD Net/2.

	        There is some really, really ugly code in here.

$FreeBSD$
*/
/*
 * Copyright (c) 1985, 1988, 1990 Regents of the University of California.
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

#include "opie_cfg.h"

#if HAVE_ANSISTDARG
#include <stdarg.h>
#endif /* HAVE_ANSISTDARG */

/*
 * FTP server.
 */

#if HAVE_SYS_PARAM_H
#include <sys/param.h>
#endif /* HAVE_SYS_PARAM_H */
#include <sys/stat.h>
/* #include <sys/ioctl.h> */
#include <sys/socket.h>
#include <sys/wait.h>
#ifdef SYS_FCNTL_H
#include <sys/fcntl.h>
#else
#include <fcntl.h>
#endif	/* SYS_FCNTL_H */
#include <sys/types.h>

#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>

#define	FTP_NAMES
#include <arpa/ftp.h>
#include <arpa/inet.h>
#include <arpa/telnet.h>

#include <signal.h>
#include <fcntl.h>
#if HAVE_TIME_H
#include <time.h>
#endif /* HAVE_TIME_H */
#if HAVE_PWD_H
#include <pwd.h>
#endif /* HAVE_PWD_H */
#include <setjmp.h>
#include <netdb.h>
#include <errno.h>
#include <syslog.h>
#if HAVE_UNISTD_H
#include <unistd.h>
#endif /* HAVE_UNISTD_H */
#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <grp.h>

#include "opie.h"

#if HAVE_SHADOW_H
#include <shadow.h>
#endif /* HAVE_SHADOW_H */

#if HAVE_CRYPT_H
#include <crypt.h>
#endif /* HAVE_CRYPT_H */

#if HAVE_SYS_UTSNAME_H
#include <sys/utsname.h>
#endif /* HAVE_SYS_UTSNAME_H */

#ifdef _AIX
#include <sys/id.h>
#include <sys/priv.h>
#endif /* _AIX */

#ifdef IP_TOS
#ifndef IPTOS_THROUGHPUT
#undef IP_TOS
#endif /* !IPTOS_THROUGHPUT */
#ifndef IPTOS_LOWDELAY
#undef IP_TOS
#endif /* !IPTOS_LOWDELAY */
#endif /* IP_TOS */

extern int errno;
extern char *home;	/* pointer to home directory for glob */
extern FILE *ftpd_popen __P((char *, char *));
extern int ftpd_pclose __P((FILE *));
extern char cbuf[];
extern off_t restart_point;

static struct sockaddr_in ctrl_addr;
static struct sockaddr_in data_source;
struct sockaddr_in data_dest;
struct sockaddr_in his_addr;
static struct sockaddr_in pasv_addr;

static int data;
jmp_buf errcatch;
static jmp_buf urgcatch;
int logged_in;
struct passwd *pw;
int debug;
int timeout = 900;	/* timeout after 15 minutes of inactivity */
int maxtimeout = 7200;	/* don't allow idle time to be set beyond 2 hours */

#if DOANONYMOUS
static int guest;
#endif	/* DOANONYMOUS */
int type;
int form;
static int stru;	/* avoid C keyword */
static int mode;
int usedefault = 1;	/* for data transfers */
int pdata = -1;	/* for passive mode */
static int transflag;
static off_t file_size;
static off_t byte_count;

#if (!defined(CMASK) || CMASK == 0)
#undef CMASK
#define CMASK 077
#endif

static int defumask = CMASK;	/* default umask value */
char tmpline[7];
char remotehost[MAXHOSTNAMELEN];

/*
 * Timeout intervals for retrying connections
 * to hosts that don't accept PORT cmds.  This
 * is a kludge, but given the problems with TCP...
 */
#define	SWAITMAX	90	/* wait at most 90 seconds */
#define	SWAITINT	5	/* interval between retries */

static int swaitmax = SWAITMAX;
static int swaitint = SWAITINT;

#if DOTITLE
static char **Argv = NULL;	/* pointer to argument vector */
static char *LastArgv = NULL;	/* end of argv */
static char proctitle[BUFSIZ];	/* initial part of title */
#endif	/* DOTITLE */

static int af_pwok = 0, pwok = 0;
static struct opie opiestate;

VOIDRET perror_reply __P((int, char *));
VOIDRET dologout __P((int));
char *getline __P((char *, int, FILE *));
VOIDRET upper __P((char *));

static VOIDRET lostconn __P((int));
static VOIDRET myoob __P((int));
static FILE *getdatasock __P((char *));
static FILE *dataconn __P((char *, off_t, char *));
static int checkuser __P((char *));
static VOIDRET end_login __P((void));
static VOIDRET send_data __P((FILE *, FILE *, off_t));
static int receive_data __P((FILE *, FILE *));
static char *gunique __P((char *));
static char *sgetsave __P((char *));

int opielogwtmp __P((char *, char *, char *, char *));

int fclose __P((FILE *));

#ifdef HAVE_ANSISTDARG
VOIDRET reply FUNCTION((stdarg is ANSI only), int n AND char *fmt AND ...) 
{
  va_list ap;
  char buffer[1024];

  va_start(ap, fmt);
  vsprintf(buffer, fmt, ap);
  va_end(ap);

  printf("%d %s\r\n", n, buffer);
  fflush(stdout);
  if (debug)
    syslog(LOG_DEBUG, "<--- %d %s", n, buffer);
}
#else /* HAVE_ANSISTDARG */
VOIDRET reply FUNCTION((n, fmt, p0, p1, p2, p3, p4, p5), int n AND char *fmt AND int p0 AND int p1 AND int p2 AND int p3 AND int p4 AND int p5)
{
  printf("%d ", n);
  printf(fmt, p0, p1, p2, p3, p4, p5);
  printf("\r\n");
  fflush(stdout);
  if (debug) {
    syslog(LOG_DEBUG, "<--- %d ", n);
    syslog(LOG_DEBUG, fmt, p0, p1, p2, p3, p4, p5);
  }
}
#endif /* HAVE_ANSISTDARG */

#ifdef HAVE_ANSISTDARG
VOIDRET lreply FUNCTION((stdarg is ANSI only), int n AND char *fmt AND ...) 
{
  va_list ap;
  char buffer[1024];

  va_start(ap, fmt);
  vsprintf(buffer, fmt, ap);
  va_end(ap);

  printf("%d- %s\r\n", n, buffer);
  fflush(stdout);
  if (debug)
    syslog(LOG_DEBUG, "<--- %d- %s", n, buffer);
}
#else /* HAVE_ANSISTDARG */
VOIDRET lreply FUNCTION((n, fmt, p0, p1, p2, p3, p4, p5), int n AND char *fmt AND int p0 AND int p1 AND int p2 AND int p3 AND int p4 AND int p5)
{
  printf("%d- ", n);
  printf(fmt, p0, p1, p2, p3, p4, p5);
  printf("\r\n");
  fflush(stdout);
  if (debug) {
    syslog(LOG_DEBUG, "<--- %d- ", n);
    syslog(LOG_DEBUG, fmt, p0, p1, p2, p3, p4, p5);
  }
}
#endif /* HAVE_ANSISTDARG */

VOIDRET enable_signalling FUNCTION_NOARGS
{
	signal(SIGPIPE, lostconn);
	if ((int)signal(SIGURG, myoob) < 0)
		syslog(LOG_ERR, "signal: %m");
}

VOIDRET disable_signalling FUNCTION_NOARGS
{
	signal(SIGPIPE, SIG_IGN);
	if ((int)signal(SIGURG, SIG_IGN) < 0)
		syslog(LOG_ERR, "signal: %m");
}

static VOIDRET lostconn FUNCTION((input), int input)
{
  if (debug)
    syslog(LOG_DEBUG, "lost connection");
  dologout(-1);
}

static char ttyline[20];

/*
 * Helper function for sgetpwnam().
 */
static char *sgetsave FUNCTION((s), char *s)
{
  char *new = malloc((unsigned) strlen(s) + 1);

  if (new == NULL) {
    perror_reply(421, "Local resource failure: malloc");
    dologout(1);
    /* NOTREACHED */
  }
  strcpy(new, s);
  return (new);
}

/*
 * Save the result of a getpwnam.  Used for USER command, since
 * the data returned must not be clobbered by any other command
 * (e.g., globbing).
 */
static struct passwd *sgetpwnam FUNCTION((name), char *name)
{
  static struct passwd save;
  register struct passwd *p;

#if HAVE_SHADOW
  struct spwd *spwd;
#endif /* HAVE_SHADOW */

  if ((p = getpwnam(name)) == NULL)
    return (p);

#if HAVE_SHADOW
  if ((spwd = getspnam(name)) == NULL)
    return NULL;

  endspent();

  p->pw_passwd = spwd->sp_pwdp;
#endif /* HAVE_SHADOW */

  endpwent();

  if (save.pw_name) {
    free(save.pw_name);
    free(save.pw_passwd);
    free(save.pw_gecos);
    free(save.pw_dir);
    free(save.pw_shell);
  }
  save = *p;
  save.pw_name = sgetsave(p->pw_name);
  save.pw_passwd = sgetsave(p->pw_passwd);
  save.pw_gecos = sgetsave(p->pw_gecos);
  save.pw_dir = sgetsave(p->pw_dir);
  save.pw_shell = sgetsave(p->pw_shell);
  return (&save);
}

int login_attempts;	/* number of failed login attempts */
int askpasswd;	/* had user command, ask for passwd */

/*
 * USER command.
 * Sets global passwd pointer pw if named account exists and is acceptable;
 * sets askpasswd if a PASS command is expected.  If logged in previously,
 * need to reset state.  If name is "ftp" or "anonymous", the name is not in
 * _PATH_FTPUSERS, and ftp account exists, set guest and pw, then just return.
 * If account doesn't exist, ask for passwd anyway.  Otherwise, check user
 * requesting login privileges.  Disallow anyone who does not have a standard
 * shell as returned by getusershell().  Disallow anyone mentioned in the file
 * _PATH_FTPUSERS to allow people such as root and uucp to be avoided.
 */
int user FUNCTION((name), char *name)
{
  register char *cp;
  char *shell;

  if (logged_in) {
#if DOANONYMOUS
    if (guest) {
      reply(530, "Can't change user from guest login.");
      return -1;
    }
#endif	/* DOANONMOUS */
    end_login();
  }
  askpasswd = 1;
#if DOANONYMOUS
  guest = 0;
  if (!strcmp(name, "ftp") || !strcmp(name, "anonymous"))
    if (!checkuser("ftp") && !checkuser("anonymous"))
      if ((pw = sgetpwnam("ftp")) != NULL) {
	guest = 1;
	askpasswd = 1;
	reply(331, "Guest login ok, send your e-mail address as your password.");
	syslog(LOG_INFO, "Anonymous FTP connection made from host %s.", remotehost);
        return 0;
      }
#endif	/* DOANONYMOUS */
  if (pw = sgetpwnam(name)) {
    if ((shell = pw->pw_shell) == NULL || *shell == 0)
      shell = _PATH_BSHELL;
    while ((cp = getusershell()) != NULL)
      if (!strcmp(cp, shell))
	break;
    endusershell();
    if (cp == NULL || checkuser(name) || ((pw->pw_passwd[0] == '*') || (pw->pw_passwd[0] == '#'))) {
#if DEBUG
      if (!cp)
        syslog(LOG_DEBUG, "Couldn't find %s in the list of valid shells.", pw->pw_shell);
      if (checkuser(name))
        syslog(LOG_DEBUG, "checkuser failed - user in /etc/ftpusers?");
      if (((pw->pw_passwd[0] == '*') || (pw->pw_passwd[0] == '#')))
        syslog(LOG_DEBUG, "Login disabled: pw_passwd == %s", pw->pw_passwd);
#endif /* DEBUG */
      pw = (struct passwd *) NULL;
      askpasswd = -1;
    }
  } 
  {
    char prompt[OPIE_CHALLENGE_MAX + 1];

    opiechallenge(&opiestate, name, prompt);

    if (askpasswd == -1) {
      syslog(LOG_WARNING, "Invalid FTP user name %s attempted from %s.", name, remotehost);
      pwok = 0;
    } else
      pwok = af_pwok && opiealways(pw->pw_dir);

#if NEW_PROMPTS
    reply(331, "Response to %s %s for %s.", prompt,
#else /* NEW_PROMPTS */
    reply(331, "OTP response %s %s for %s.", prompt,
#endif /* NEW_PROMPTS */
	  pwok ? "requested" : "required", name);
  }
  /* Delay before reading passwd after first failed attempt to slow down
     passwd-guessing programs. */
  if (login_attempts)
    sleep((unsigned) login_attempts);

  return 0;
}

/*
 * Check if a user is in the file _PATH_FTPUSERS
 */
static int checkuser FUNCTION((name), char *name)
{
  register FILE *fd;
  register char *p;
  char line[BUFSIZ];

  if ((fd = fopen(_PATH_FTPUSERS, "r")) != NULL) {
    while (fgets(line, sizeof(line), fd) != NULL)
      if ((p = strchr(line, '\n')) != NULL) {
	*p = '\0';
	if (line[0] == '#')
	  continue;
	if (!strcmp(line, name)) {
          fclose(fd);
	  return (1);
        }
      }
    fclose(fd);
  }
  return (0);
}

/*
 * Terminate login as previous user, if any, resetting state;
 * used when USER command is given or login fails.
 */
static VOIDRET end_login FUNCTION_NOARGS
{
  disable_signalling();
  if (seteuid((uid_t) 0))
    syslog(LOG_ERR, "Can't set euid");
  if (logged_in)
    opielogwtmp(ttyline, "", "", "ftp");
  pw = NULL;
  logged_in = 0;
#if DOANONYMOUS
  guest = 0;
#endif	/* DOANONYMOUS */
  enable_signalling();
}

VOIDRET pass FUNCTION((passwd), char *passwd)
{
  int legit = askpasswd + 1, i;

  if (logged_in || askpasswd == 0) {
    reply(503, "Login with USER first.");
    return;
  }
  askpasswd = 0;

#if DOANONYMOUS
  if (!guest) { /* "ftp" is only account allowed no password */
#endif	/* DOANONYMOUS */
    i = opieverify(&opiestate, passwd);
    if (legit && i && pwok) 
      i = strcmp(crypt(passwd, pw->pw_passwd), pw->pw_passwd);
    if (!legit || i) {
      reply(530, "Login incorrect.");
      pw = NULL;
      if (login_attempts++ >= 5) {
	syslog(LOG_WARNING,
	       "Repeated login failures for user %s from %s",
	       pw->pw_name, remotehost);
	exit(0);
      }
      return;
    }
#if DOANONYMOUS
  } else
    if ((passwd[0] <= ' ') ||  checkuser(passwd)) {
      reply(530, "No identity, no service.");
      syslog(LOG_DEBUG, "Bogus address: %s", passwd);
      exit(0);
    }
#endif	/* DOANONYMOUS */
  login_attempts = 0;	/* this time successful */
  if (setegid((gid_t) pw->pw_gid) < 0) {
    reply(550, "Can't set gid.");
    syslog(LOG_DEBUG, "gid = %d, errno = %s(%d)", pw->pw_gid, strerror(errno), errno);
    return;
  }
  initgroups(pw->pw_name, pw->pw_gid);

  /* open wtmp before chroot */
  sprintf(ttyline, "ftp%d", getpid());
  opielogwtmp(ttyline, pw->pw_name, remotehost, "ftp");
  logged_in = 1;

#if DOANONYMOUS
  if (guest) {
    /* We MUST do a chdir() after the chroot. Otherwise the old current
       directory will be accessible as "." outside the new root! */
    if (chroot(pw->pw_dir) < 0 || chdir("/") < 0) {
      reply(550, "Can't set guest privileges.");
      goto bad;
    }
  } else
#endif	/* DOANONYMOUS */
    if (chdir(pw->pw_dir) < 0) {
      if (chdir("/") < 0) {
	reply(530, "User %s: can't change directory to %s.",
	      pw->pw_name, pw->pw_dir);
	goto bad;
      } else
	lreply(230, "No directory! Logging in with home=/");
    }
/* This patch was contributed by an OPIE user. We don't know what it
   does, exactly. It may or may not work. */
#ifdef _AIX
   {
       priv_t priv;
       priv.pv_priv[0] = 0;
       priv.pv_priv[1] = 0;
       setgroups(NULL, NULL);
       if (setpriv(PRIV_SET|PRIV_INHERITED|PRIV_EFFECTIVE|PRIV_BEQUEATH,
                   &priv, sizeof(priv_t)) < 0 ||
	   setgidx(ID_REAL|ID_EFFECTIVE, (gid_t)pw->pw_gid) < 0 ||
           setuidx(ID_REAL|ID_EFFECTIVE, (uid_t)pw->pw_uid) < 0 ||
           seteuid((uid_t)pw->pw_uid) < 0) {
               reply(550, "Can't set uid (_AIX3).");
               goto bad;
       }
    }
#else /* _AIX */
  if (seteuid((uid_t) pw->pw_uid) < 0) {
    reply(550, "Can't set uid.");
    goto bad;
  }
#endif /* _AIX */
 /*
  * Display a login message, if it exists.
  * N.B. reply(230,) must follow the message.
  */
  {
  FILE *fd;

  if ((fd = fopen(_PATH_FTPLOGINMESG, "r")) != NULL) {
    char *cp, line[128];

    while (fgets(line, sizeof(line), fd) != NULL) {
      if ((cp = strchr(line, '\n')) != NULL)
        *cp = '\0';
      lreply(230, "%s", line);
    }
    (void) fflush(stdout);
    (void) fclose(fd);
  }
  }
#if DOANONYMOUS
  if (guest) {
    reply(230, "Guest login ok, access restrictions apply.");
#if DOTITLE
    setproctitle("%s: anonymous/%.*s", remotehost,
            sizeof(proctitle) - sizeof(remotehost) - sizeof(": anonymous/"),
	    passwd);
#endif /* DOTITLE */
    syslog(LOG_NOTICE, "ANONYMOUS FTP login from %s with ID %s",
            remotehost, passwd);
  } else
#endif	/* DOANONYMOUS */
  {
    reply(230, "User %s logged in.", pw->pw_name);

#if DOTITLE
    setproctitle("%s: %s", remotehost, pw->pw_name);
#endif /* DOTITLE */
    syslog(LOG_INFO, "FTP login from %s with user name %s", remotehost, pw->pw_name);
  }
  home = pw->pw_dir;	/* home dir for globbing */
  umask(defumask);
  return;

bad:
  /* Forget all about it... */
  end_login();
}

VOIDRET retrieve FUNCTION((cmd, name), char *cmd AND char *name)
{
  FILE *fin, *dout;
  struct stat st;
  int (*closefunc) ();

  if (cmd == 0) {
    fin = fopen(name, "r"), closefunc = fclose;
    st.st_size = 0;
  } else {
    char line[BUFSIZ];

    snprintf(line, sizeof(line), cmd, name);
    name = line;
    fin = ftpd_popen(line, "r"), closefunc = ftpd_pclose;
    st.st_size = -1;
#if HAVE_ST_BLKSIZE
    st.st_blksize = BUFSIZ;
#endif /* HAVE_ST_BLKSIZE */
  }
  if (fin == NULL) {
    if (errno != 0)
      perror_reply(550, name);
    return;
  }
  if (cmd == 0 &&
      (fstat(fileno(fin), &st) < 0 || (st.st_mode & S_IFMT) != S_IFREG)) {
    reply(550, "%s: not a plain file.", name);
    goto done;
  }
  if (restart_point) {
    if (type == TYPE_A) {
      register int i, n, c;

      n = restart_point;
      i = 0;
      while (i++ < n) {
	if ((c = getc(fin)) == EOF) {
	  perror_reply(550, name);
	  goto done;
	}
	if (c == '\n')
	  i++;
      }
    } else
      if (lseek(fileno(fin), restart_point, SEEK_SET /* L_SET */ ) < 0) {
	perror_reply(550, name);
	goto done;
      }
  }
  dout = dataconn(name, st.st_size, "w");
  if (dout == NULL)
    goto done;
#if HAVE_ST_BLKSIZE
  send_data(fin, dout, st.st_blksize);
#else /* HAVE_ST_BLKSIZE */
  send_data(fin, dout, BUFSIZ);
#endif /* HAVE_ST_BLKSIZE */
  fclose(dout);
  data = -1;
  pdata = -1;
done:
  (*closefunc) (fin);
}

VOIDRET store FUNCTION((name, mode, unique), char *name AND char *mode AND int unique)
{
  FILE *fout, *din;
  struct stat st;
  int (*closefunc) ();

  if (unique && stat(name, &st) == 0 &&
      (name = gunique(name)) == NULL)
    return;

  if (restart_point)
    mode = "r+w";
  fout = fopen(name, mode);
  closefunc = fclose;
  if (fout == NULL) {
    perror_reply(553, name);
    return;
  }
  if (restart_point) {
    if (type == TYPE_A) {
      register int i, n, c;

      n = restart_point;
      i = 0;
      while (i++ < n) {
	if ((c = getc(fout)) == EOF) {
	  perror_reply(550, name);
	  goto done;
	}
	if (c == '\n')
	  i++;
      }
      /* We must do this seek to "current" position because we are changing
         from reading to writing. */
      if (fseek(fout, 0L, SEEK_CUR /* L_INCR */ ) < 0) {
	perror_reply(550, name);
	goto done;
      }
    } else
      if (lseek(fileno(fout), restart_point, SEEK_SET /* L_SET */ ) < 0) {
	perror_reply(550, name);
	goto done;
      }
  }
  din = dataconn(name, (off_t) - 1, "r");
  if (din == NULL)
    goto done;
  if (receive_data(din, fout) == 0) {
    if (unique)
      reply(226, "Transfer complete (unique file name:%s).",
	    name);
    else
      reply(226, "Transfer complete.");
  }
  fclose(din);
  data = -1;
  pdata = -1;
done:
  (*closefunc) (fout);
}

static FILE *getdatasock FUNCTION((mode), char *mode)
{
  int s, on = 1, tries;

  if (data >= 0)
    return (fdopen(data, mode));
  disable_signalling();
  if (seteuid((uid_t) 0))
    syslog(LOG_ERR, "Can't set euid");
  s = socket(AF_INET, SOCK_STREAM, 0);
  if (s < 0)
    goto bad;
  if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR,
		 (char *) &on, sizeof(on)) < 0)
    goto bad;
  /* anchor socket to avoid multi-homing problems */
  data_source.sin_family = AF_INET;
  data_source.sin_addr = ctrl_addr.sin_addr;
  for (tries = 1;; tries++) {
    if (bind(s, (struct sockaddr *) & data_source,
	     sizeof(data_source)) >= 0)
      break;
    if (errno != EADDRINUSE || tries > 10)
      goto bad;
    sleep(tries);
  }
  if (seteuid((uid_t) pw->pw_uid))
    syslog(LOG_ERR, "Can't set euid");
  enable_signalling();
#ifdef IP_TOS
  on = IPTOS_THROUGHPUT;
  if (setsockopt(s, IPPROTO_IP, IP_TOS, (char *) &on, sizeof(int)) < 0)
    syslog(LOG_WARNING, "setsockopt (IP_TOS): %m");
#endif
  return (fdopen(s, mode));
bad:
  {
  int t = errno;

  if (seteuid((uid_t) pw->pw_uid))
    syslog(LOG_ERR, "Can't set euid");
  enable_signalling();
  close(s);

  errno = t;
  }
  return (NULL);
}

static FILE *dataconn FUNCTION((name, size, mode), char *name AND off_t size AND char *mode)
{
  char sizebuf[32];
  FILE *file;
  int retry = 0;
#ifdef IP_TOS
  int tos;
#endif /* IP_TOS */

  file_size = size;
  byte_count = 0;
  if (size != (off_t) - 1)
    snprintf(sizebuf, sizeof(sizebuf), " (%ld bytes)", size);
  else
    strcpy(sizebuf, "");
  if (pdata >= 0) {
    struct sockaddr_in from;
    int s, fromlen = sizeof(from);

    s = accept(pdata, (struct sockaddr *) & from, &fromlen);
    if (s < 0) {
      reply(425, "Can't open data connection.");
      close(pdata);
      pdata = -1;
      return (NULL);
    }
    close(pdata);
    pdata = s;
#ifdef IP_TOS
    tos = IPTOS_LOWDELAY;
    setsockopt(s, IPPROTO_IP, IP_TOS, (char *) &tos,
		      sizeof(int));

#endif
    reply(150, "Opening %s mode data connection for %s%s.",
	  type == TYPE_A ? "ASCII" : "BINARY", name, sizebuf);
    return (fdopen(pdata, mode));
  }
  if (data >= 0) {
    reply(125, "Using existing data connection for %s%s.",
	  name, sizebuf);
    usedefault = 1;
    return (fdopen(data, mode));
  }
  if (usedefault)
    data_dest = his_addr;
  usedefault = 1;
  file = getdatasock(mode);
  if (file == NULL) {
    reply(425, "Can't create data socket (%s,%d): %s.",
	  inet_ntoa(data_source.sin_addr),
	  ntohs(data_source.sin_port), strerror(errno));
    return (NULL);
  }
  data = fileno(file);
  while (connect(data, (struct sockaddr *) & data_dest,
		 sizeof(data_dest)) < 0) {
    if (errno == EADDRINUSE && retry < swaitmax) {
      sleep((unsigned) swaitint);
      retry += swaitint;
      continue;
    }
    perror_reply(425, "Can't build data connection");
    fclose(file);
    data = -1;
    return (NULL);
  }
  reply(150, "Opening %s mode data connection for %s%s.",
	type == TYPE_A ? "ASCII" : "BINARY", name, sizebuf);
  return (file);
}

/*
 * Tranfer the contents of "instr" to
 * "outstr" peer using the appropriate
 * encapsulation of the data subject
 * to Mode, Structure, and Type.
 *
 * NB: Form isn't handled.
 */
static VOIDRET send_data FUNCTION((instr, outstr, blksize), FILE *instr AND FILE *outstr AND off_t blksize)
{
  register int c, cnt;
  register char *buf;
  int netfd, filefd;

  transflag++;
  if (setjmp(urgcatch)) {
    transflag = 0;
    return;
  }
  switch (type) {

  case TYPE_A:
    while ((c = getc(instr)) != EOF) {
      byte_count++;
      if (c == '\n') {
	if (ferror(outstr))
	  goto data_err;
	putc('\r', outstr);
      }
      putc(c, outstr);
    }
    fflush(outstr);
    transflag = 0;
    if (ferror(instr))
      goto file_err;
    if (ferror(outstr))
      goto data_err;
    reply(226, "Transfer complete.");
    return;

  case TYPE_I:
  case TYPE_L:
    if ((buf = malloc((u_int) blksize)) == NULL) {
      transflag = 0;
      perror_reply(451, "Local resource failure: malloc");
      return;
    }
    netfd = fileno(outstr);
    filefd = fileno(instr);
    while ((cnt = read(filefd, buf, (u_int) blksize)) > 0 &&
	   write(netfd, buf, cnt) == cnt)
      byte_count += cnt;
    transflag = 0;
    free(buf);
    if (cnt != 0) {
      if (cnt < 0)
	goto file_err;
      goto data_err;
    }
    reply(226, "Transfer complete.");
    return;
  default:
    transflag = 0;
    reply(550, "Unimplemented TYPE %d in send_data", type);
    return;
  }

data_err:
  transflag = 0;
  perror_reply(426, "Data connection");
  return;

file_err:
  transflag = 0;
  perror_reply(551, "Error on input file");
}

/*
 * Transfer data from peer to
 * "outstr" using the appropriate
 * encapulation of the data subject
 * to Mode, Structure, and Type.
 *
 * N.B.: Form isn't handled.
 */
static int receive_data FUNCTION((instr, outstr), FILE *instr AND FILE *outstr)
{
  register int c;
  int cnt, bare_lfs = 0;
  char buf[BUFSIZ];

  transflag++;
  if (setjmp(urgcatch)) {
    transflag = 0;
    return (-1);
  }
  switch (type) {

  case TYPE_I:
  case TYPE_L:
    while ((cnt = read(fileno(instr), buf, sizeof buf)) > 0) {
      if (write(fileno(outstr), buf, cnt) != cnt)
	goto file_err;
      byte_count += cnt;
    }
    if (cnt < 0)
      goto data_err;
    transflag = 0;
    return (0);

  case TYPE_E:
    reply(553, "TYPE E not implemented.");
    transflag = 0;
    return (-1);

  case TYPE_A:
    while ((c = getc(instr)) != EOF) {
      byte_count++;
      if (c == '\n')
	bare_lfs++;
      while (c == '\r') {
	if (ferror(outstr))
	  goto data_err;
	if ((c = getc(instr)) != '\n') {
	  putc('\r', outstr);
	  if (c == '\0' || c == EOF)
	    goto contin2;
	}
      }
      putc(c, outstr);
  contin2:;
    }
    fflush(outstr);
    if (ferror(instr))
      goto data_err;
    if (ferror(outstr))
      goto file_err;
    transflag = 0;
    if (bare_lfs) {
      lreply(230, "WARNING! %d bare linefeeds received in ASCII mode", bare_lfs);
      printf("   File may not have transferred correctly.\r\n");
    }
    return (0);
  default:
    reply(550, "Unimplemented TYPE %d in receive_data", type);
    transflag = 0;
    return (-1);
  }

data_err:
  transflag = 0;
  perror_reply(426, "Data Connection");
  return (-1);

file_err:
  transflag = 0;
  perror_reply(452, "Error writing file");
  return (-1);
}

VOIDRET statfilecmd FUNCTION((filename), char *filename)
{
  char line[BUFSIZ];
  FILE *fin;
  int c;

#if HAVE_LS_G_FLAG
  snprintf(line, sizeof(line), "%s %s", "/bin/ls -lgA", filename);
#else /* HAVE_LS_G_FLAG */
  snprintf(line, sizeof(line), "%s %s", "/bin/ls -lA", filename);
#endif /* HAVE_LS_G_FLAG */
  fin = ftpd_popen(line, "r");
  lreply(211, "status of %s:", filename);
  while ((c = getc(fin)) != EOF) {
    if (c == '\n') {
      if (ferror(stdout)) {
	perror_reply(421, "control connection");
	ftpd_pclose(fin);
	dologout(1);
	/* NOTREACHED */
      }
      if (ferror(fin)) {
	perror_reply(551, filename);
	ftpd_pclose(fin);
	return;
      }
      putc('\r', stdout);
    }
    putc(c, stdout);
  }
  ftpd_pclose(fin);
  reply(211, "End of Status");
}

VOIDRET statcmd FUNCTION_NOARGS
{
/* COMMENTED OUT STUFF BECAUSE THINGS BROKE ON SUNOS. */
  struct sockaddr_in *sin;
  u_char *a, *p;

  lreply(211, "FTP server status:");
  printf("     \r\n");
  printf("     Connected to %s", remotehost);
  if (!isdigit(remotehost[0]))
    printf(" (%s)", inet_ntoa(his_addr.sin_addr));
  printf("\r\n");
  if (logged_in) {
#if DOANONYMOUS
    if (guest)
      printf("     Logged in anonymously\r\n");
    else
#endif	/* DOANONYMOUS */
      printf("     Logged in as %s\r\n", pw->pw_name);
  } else
    if (askpasswd)
      printf("     Waiting for password\r\n");
    else
      printf("     Waiting for user name\r\n");
  if (data != -1)
    printf("     Data connection open\r\n");
  else
    if (pdata != -1) {
      printf("     in Passive mode");
      sin = &pasv_addr;
      goto printaddr;
    } else
      if (usedefault == 0) {
	printf("     PORT");
	sin = &data_dest;
    printaddr:
	a = (u_char *) & sin->sin_addr;
	p = (u_char *) & sin->sin_port;
#define UC(b) (((int) b) & 0xff)
	printf(" (%d,%d,%d,%d,%d,%d)\r\n", UC(a[0]),
	       UC(a[1]), UC(a[2]), UC(a[3]), UC(p[0]), UC(p[1]));
#undef UC
      } else
	printf("     No data connection\r\n");
  reply(211, "End of status");
}

VOIDRET opiefatal FUNCTION((s), char *s)
{
  reply(451, "Error in server: %s\n", s);
  reply(221, "Closing connection due to server error.");
  dologout(0);
  /* NOTREACHED */
}

static VOIDRET ack FUNCTION((s), char *s)
{
  reply(250, "%s command successful.", s);
}

VOIDRET nack FUNCTION((s), char *s)
{
  reply(502, "%s command not implemented.", s);
}

VOIDRET yyerror FUNCTION((s), char *s)
{
  char *cp;

  if (cp = strchr(cbuf, '\n'))
    *cp = '\0';
  reply(500, "'%s': command not understood.", cbuf);
}

VOIDRET delete FUNCTION((name), char *name)
{
  struct stat st;

  if (stat(name, &st) < 0) {
    perror_reply(550, name);
    return;
  }
  if ((st.st_mode & S_IFMT) == S_IFDIR) {
    if (rmdir(name) < 0) {
      perror_reply(550, name);
      return;
    }
    goto done;
  }
  if (unlink(name) < 0) {
    perror_reply(550, name);
    return;
  }
done:
  ack("DELE");
}

VOIDRET cwd FUNCTION((path), char *path)
{
  if (chdir(path) < 0)
    perror_reply(550, path);
  else
    ack("CWD");
}

VOIDRET makedir FUNCTION((name), char *name)
{
  if (mkdir(name, 0777) < 0)
    perror_reply(550, name);
  else
    reply(257, "MKD command successful.");
}

VOIDRET removedir FUNCTION((name), char *name)
{
  if (rmdir(name) < 0)
    perror_reply(550, name);
  else
    ack("RMD");
}

VOIDRET pwd FUNCTION_NOARGS
{
  char path[MAXPATHLEN + 1];

  if (getcwd(path, MAXPATHLEN) == (char *) NULL)
    reply(550, "%s.", path);
  else
    reply(257, "\"%s\" is current directory.", path);
}

char *renamefrom FUNCTION((name), char *name)
{
  struct stat st;

  if (stat(name, &st) < 0) {
    perror_reply(550, name);
    return ((char *) 0);
  }
  reply(350, "File exists, ready for destination name");
  return (name);
}

VOIDRET renamecmd FUNCTION((from, to), char *from AND char *to)
{
  if (rename(from, to) < 0)
    perror_reply(550, "rename");
  else
    ack("RNTO");
}

static VOIDRET dolog FUNCTION((sin), struct sockaddr_in *sin)
{
  struct hostent *hp = gethostbyaddr((char *) &sin->sin_addr,
				     sizeof(struct in_addr), AF_INET);
  time_t t, time();

  if (hp)
    opiestrncpy(remotehost, hp->h_name, sizeof(remotehost));
  else
    opiestrncpy(remotehost, inet_ntoa(sin->sin_addr), sizeof(remotehost));
#if DOTITLE
  setproctitle("%s: connected", remotehost);
#endif	/* DOTITLE */

  t = time((time_t *) 0);
  syslog(LOG_INFO, "connection from %s at %s",
    remotehost, ctime(&t));
}

/*
 * Record logout in wtmp file
 * and exit with supplied status.
 */
VOIDRET dologout FUNCTION((status), int status)
{
  disable_signalling();
  if (logged_in) {
    if (seteuid((uid_t) 0))
      syslog(LOG_ERR, "Can't set euid");
    opielogwtmp(ttyline, "", "", "ftp");
  }
  /* beware of flushing buffers after a SIGPIPE */
  _exit(status);
}

static VOIDRET myoob FUNCTION((input), int input)
{
  char *cp;

  /* only process if transfer occurring */
  if (!transflag)
    return;
  cp = tmpline;
  if (getline(cp, 7, stdin) == NULL) {
    reply(221, "You could at least say goodbye.");
    dologout(0);
  }
  upper(cp);
  if (strcmp(cp, "ABOR\r\n") == 0) {
    tmpline[0] = '\0';
    reply(426, "Transfer aborted. Data connection closed.");
    reply(226, "Abort successful");
    longjmp(urgcatch, 1);
  }
  if (strcmp(cp, "STAT\r\n") == 0) {
    if (file_size != (off_t) - 1)
      reply(213, "Status: %lu of %lu bytes transferred",
	    byte_count, file_size);
    else
      reply(213, "Status: %lu bytes transferred", byte_count);
  }
}

/*
 * Note: a response of 425 is not mentioned as a possible response to
 *      the PASV command in RFC959. However, it has been blessed as
 *      a legitimate response by Jon Postel in a telephone conversation
 *      with Rick Adams on 25 Jan 89.
 */
VOIDRET passive FUNCTION_NOARGS
{
  int len;
  register char *p, *a;

  pdata = socket(AF_INET, SOCK_STREAM, 0);
  if (pdata < 0) {
    perror_reply(425, "Can't open passive connection");
    return;
  }
  pasv_addr = ctrl_addr;
  pasv_addr.sin_port = 0;
  if (seteuid((uid_t) 0))
    syslog(LOG_ERR, "Can't set euid");
  if (bind(pdata, (struct sockaddr *) & pasv_addr, sizeof(pasv_addr)) < 0) {
    seteuid((uid_t) pw->pw_uid);
    goto pasv_error;
  }
  if (seteuid((uid_t) pw->pw_uid))
    syslog(LOG_ERR, "Can't set euid");
  len = sizeof(pasv_addr);
  if (getsockname(pdata, (struct sockaddr *) & pasv_addr, &len) < 0)
    goto pasv_error;
  if (listen(pdata, 1) < 0)
    goto pasv_error;
  a = (char *) &pasv_addr.sin_addr;
  p = (char *) &pasv_addr.sin_port;

#define UC(b) (((int) b) & 0xff)

  reply(227, "Entering Passive Mode (%d,%d,%d,%d,%d,%d)", UC(a[0]),
	UC(a[1]), UC(a[2]), UC(a[3]), UC(p[0]), UC(p[1]));
  return;

pasv_error:
  close(pdata);
  pdata = -1;
  perror_reply(425, "Can't open passive connection");
  return;
}

/*
 * Generate unique name for file with basename "local".
 * The file named "local" is already known to exist.
 * Generates failure reply on error.
 */
static char *gunique FUNCTION((local), char *local)
{
  static char new[MAXPATHLEN+1];
  struct stat st;
  char *cp = strrchr(local, '/');
  int count = 0;

  if (cp)
    *cp = '\0';
  if (stat(cp ? local : ".", &st) < 0) {
    perror_reply(553, cp ? local : ".");
    return ((char *) 0);
  }
  if (cp)
    *cp = '/';
  strcpy(new, local);
  cp = new + strlen(new);
  *cp++ = '.';
  for (count = 1; count < 100; count++) {
    snprintf(cp, sizeof(new) - (cp - new), "%d", count);
    if (stat(new, &st) < 0)
      return (new);
  }
  reply(452, "Unique file name cannot be created.");
  return ((char *) 0);
}

/*
 * Format and send reply containing system error number.
 */
VOIDRET perror_reply FUNCTION((code, string), int code AND char *string)
{
  reply(code, "%s: %s.", string, strerror(errno));
}

static char *onefile[] =
{
  "",
  0
};

VOIDRET send_file_list FUNCTION((whichfiles), char *whichfiles)
{
  struct stat st;
  DIR *dirp = NULL;
  struct dirent *dir;
  FILE *dout = NULL;
  register char **dirlist, *dirname;
  int simple = 0;

  if (strpbrk(whichfiles, "~{[*?") != NULL) {
    extern char **ftpglob(), *globerr;

    globerr = NULL;
    dirlist = ftpglob(whichfiles);
    if (globerr != NULL) {
      reply(550, globerr);
      return;
    } else
      if (dirlist == NULL) {
	errno = ENOENT;
	perror_reply(550, whichfiles);
	return;
      }
  } else {
    onefile[0] = whichfiles;
    dirlist = onefile;
    simple = 1;
  }

  if (setjmp(urgcatch)) {
    transflag = 0;
    return;
  }
  while (dirname = *dirlist++) {
    if (stat(dirname, &st) < 0) {
      /* If user typed "ls -l", etc, and the client used NLST, do what the
         user meant. */
      if (dirname[0] == '-' && *dirlist == NULL &&
	  transflag == 0) {
	retrieve("/bin/ls %s", dirname);
	return;
      }
      perror_reply(550, whichfiles);
      if (dout != NULL) {
	fclose(dout);
	transflag = 0;
	data = -1;
	pdata = -1;
      }
      return;
    }
    if ((st.st_mode & S_IFMT) == S_IFREG) {
      if (dout == NULL) {
	dout = dataconn("file list", (off_t) - 1, "w");
	if (dout == NULL)
	  return;
	transflag++;
      }
      fprintf(dout, "%s%s\n", dirname,
	      type == TYPE_A ? "\r" : "");
      byte_count += strlen(dirname) + 1;
      continue;
    } else
      if ((st.st_mode & S_IFMT) != S_IFDIR)
	continue;

    if ((dirp = opendir(dirname)) == NULL)
      continue;

    while ((dir = readdir(dirp)) != NULL) {
      char nbuf[MAXPATHLEN+1];

      if (dir->d_name[0] == '.' && (strlen(dir->d_name) == 1))
	continue;
      if (dir->d_name[0] == '.' && dir->d_name[1] == '.' &&
	  (strlen(dir->d_name) == 2))
	continue;

      snprintf(nbuf, sizeof(nbuf), "%s/%s", dirname, dir->d_name);

      /* We have to do a stat to insure it's not a directory or special file. */
      if (simple || (stat(nbuf, &st) == 0 &&
		     (st.st_mode & S_IFMT) == S_IFREG)) {
	if (dout == NULL) {
	  dout = dataconn("file list", (off_t) - 1, "w");
	  if (dout == NULL)
	    return;
	  transflag++;
	}
	if (nbuf[0] == '.' && nbuf[1] == '/')
	  fprintf(dout, "%s%s\n", &nbuf[2],
		  type == TYPE_A ? "\r" : "");
	else
	  fprintf(dout, "%s%s\n", nbuf,
		  type == TYPE_A ? "\r" : "");
	byte_count += strlen(nbuf) + 1;
      }
    }
    closedir(dirp);
  }

  if (dout == NULL)
    reply(550, "No files found.");
  else
    if (ferror(dout) != 0)
      perror_reply(550, "Data connection");
    else
      reply(226, "Transfer complete.");

  transflag = 0;
  if (dout != NULL)
    fclose(dout);
  data = -1;
  pdata = -1;
}

#if DOTITLE
/*
 * clobber argv so ps will show what we're doing.
 * (stolen from sendmail)
 * warning, since this is usually started from inetd.conf, it
 * often doesn't have much of an environment or arglist to overwrite.
 */
VOIDRET setproctitle FUNCTION((fmt, a, b, c), char *fmt AND int a AND int b AND int c)
{
  register char *p, *bp, ch;
  register int i;
  char buf[BUFSIZ];

  snprintf(buf, sizeof(buf), fmt, a, b, c);

  /* make ps print our process name */
  p = Argv[0];
  *p++ = '-';

  i = strlen(buf);
  if (i > LastArgv - p - 2) {
    i = LastArgv - p - 2;
    buf[i] = '\0';
  }
  bp = buf;
  while (ch = *bp++)
    if (ch != '\n' && ch != '\r')
      *p++ = ch;
  while (p < LastArgv)
    *p++ = ' ';
}
#endif	/* DOTITLE */

VOIDRET catchexit FUNCTION_NOARGS
{
  closelog();
}

int main FUNCTION((argc, argv, envp), int argc AND char *argv[] AND char *envp[])
{
  int addrlen, on = 1;
  char *cp;
#ifdef IP_TOS
  int tos;
#endif /* IP_TOS */

  {
  int i;

  for (i = sysconf(_SC_OPEN_MAX); i > 2; i--)
    close(i);
  }
  
  /* LOG_NDELAY sets up the logging connection immediately, necessary for
     anonymous ftp's that chroot and can't do it later. */
  openlog("ftpd", LOG_PID | LOG_NDELAY, LOG_DAEMON);
  atexit(catchexit);
  addrlen = sizeof(his_addr);
  if (getpeername(0, (struct sockaddr *) & his_addr, &addrlen) < 0) {
    syslog(LOG_ERR, "getpeername (%s): %m", argv[0]);
    exit(1);
  }
  addrlen = sizeof(ctrl_addr);
  if (getsockname(0, (struct sockaddr *) & ctrl_addr, &addrlen) < 0) {
    syslog(LOG_ERR, "getsockname (%s): %m", argv[0]);
    exit(1);
  }
#ifdef IP_TOS
  tos = IPTOS_LOWDELAY;
  if (setsockopt(0, IPPROTO_IP, IP_TOS, (char *) &tos, sizeof(int)) < 0)
    syslog(LOG_WARNING, "setsockopt (IP_TOS): %m");
#endif
  data_source.sin_port = htons(ntohs(ctrl_addr.sin_port) - 1);
  debug = 0;
#if DOTITLE
  /* Save start and extent of argv for setproctitle. */
  Argv = argv;
  while (*envp)
    envp++;
  LastArgv = envp[-1] + strlen(envp[-1]);
#endif	/* DOTITLE */

  argc--, argv++;
  while (argc > 0 && *argv[0] == '-') {
    for (cp = &argv[0][1]; *cp; cp++)
      switch (*cp) {

      case 'v':
	debug = 1;
	break;

      case 'd':
	debug = 1;
	break;

      case 'l':
	break;

      case 't':
	timeout = atoi(++cp);
	if (maxtimeout < timeout)
	  maxtimeout = timeout;
	goto nextopt;

      case 'T':
	maxtimeout = atoi(++cp);
	if (timeout > maxtimeout)
	  timeout = maxtimeout;
	goto nextopt;

      case 'u':
	{
	  int val = 0;

	  while (*++cp && *cp >= '0' && *cp <= '9')
	    val = val * 8 + *cp - '0';
	  if (*cp)
	    fprintf(stderr, "ftpd: Bad value for -u\n");
	  else
	    defumask = val;
	  goto nextopt;
	}

      default:
	fprintf(stderr, "ftpd: Unknown flag -%c ignored.\n",
		*cp);
	break;
      }
nextopt:
    argc--, argv++;
  }
  freopen(_PATH_DEVNULL, "w", stderr);
  signal(SIGCHLD, SIG_IGN);
  enable_signalling();

  /* Try to handle urgent data inline */
#ifdef SO_OOBINLINE
  if (setsockopt(0, SOL_SOCKET, SO_OOBINLINE, (char *) &on, sizeof(on)) < 0)
    syslog(LOG_ERR, "setsockopt: %m");
#endif

#ifdef	F_SETOWN
  if (fcntl(fileno(stdin), F_SETOWN, getpid()) == -1)
    syslog(LOG_ERR, "fcntl F_SETOWN: %m");
#endif
  dolog(&his_addr);
  /* Set up default state */
  data = -1;
  type = TYPE_A;
  form = FORM_N;
  stru = STRU_F;
  mode = MODE_S;
  tmpline[0] = '\0';
  af_pwok = opieaccessfile(remotehost);

  {
  FILE *fd;
  char line[128];

  /* If logins are disabled, print out the message. */
  if ((fd = fopen(_PATH_NOLOGIN,"r")) != NULL) {
    while (fgets(line, sizeof(line), fd) != NULL) {
      if ((cp = strchr(line, '\n')) != NULL)
        *cp = '\0';
      lreply(530, "%s", line);
    }
    (void) fflush(stdout);
    (void) fclose(fd);
    reply(530, "System not available.");
    exit(0);
  }
  if ((fd = fopen(_PATH_FTPWELCOME, "r")) != NULL) {
    while (fgets(line, sizeof(line), fd) != NULL) {
      if ((cp = strchr(line, '\n')) != NULL)
        *cp = '\0';
      lreply(220, "%s", line);
    }
    (void) fflush(stdout);
    (void) fclose(fd);
    /* reply(220,) must follow */
  }
  };

  reply(220, "FTP server ready.");

  setjmp(errcatch);
  for (;;)
    yyparse();
  /* NOTREACHED */
  return 0;
}
