/* opiesu.c: main body of code for the su(1m) program

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

	Modified by cmetz for OPIE 2.4. Check euid on startup. Use
		opiestrncpy().
	Modified by cmetz for OPIE 2.32. Set up TERM and PATH correctly.
	Modified by cmetz for OPIE 2.31. Fix sulog(). Replaced Getlogin() with
		currentuser. Fixed fencepost error in month printed by sulog().
	Modified by cmetz for OPIE 2.3. Limit the length of TERM on full login.
		Use HAVE_SULOG instead of DOSULOG.
        Modified by cmetz for OPIE 2.2. Don't try to clear non-blocking I/O.
                Use opiereadpass(). Minor speedup. Removed termios manipulation
                -- that's opiereadpass()'s job. Change opiereadpass() calls
                to add echo arg. Removed useless strings (I don't think that
                removing the ucb copyright one is a problem -- please let me
                know if I'm wrong). Use FUNCTION declaration et al. Ifdef
                around some headers. Make everything static. Removed
                closelog() prototype. Use the same catchexit() trickery as
                opielogin.
        Modified at NRL for OPIE 2.2. Changed opiestrip_crlf to
                opiestripcrlf.
        Modified at NRL for OPIE 2.1. Added struct group declaration.
	        Added Solaris(+others?) sulog capability. Symbol changes
		for autoconf. Removed des_crypt.h. File renamed to
		opiesu.c. Symbol+misc changes for autoconf. Added bletch
		for setpriority.
        Modified at NRL for OPIE 2.02. Added SU_STAR_CHECK (turning a bug
                into a feature ;). Fixed Solaris shadow password problem
                introduced in OPIE 2.01 (the shadow password structure is
                spwd, not spasswd).
        Modified at NRL for OPIE 2.01. Changed password lookup handling
                to use a static structure to avoid problems with drain-
                bamaged shadow password packages. Always log failures.
                Make sure to close syslog by function to avoid problems 
                with drain bamaged syslog implementations. Log a few 
                interesting errors.
	Modified at NRL for OPIE 2.0.
	Modified at Bellcore for the S/Key Version 1 software distribution.
	Originally from BSD.
*/

/*
 * Copyright (c) 1980 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */

#include "opie_cfg.h"

#include <stdio.h>
#if HAVE_PWD_H
#include <pwd.h>
#endif /* HAVE_PWD_H */
#include <grp.h>
#include <syslog.h>
#include <sys/types.h>
#if HAVE_SETPRIORITY && HAVE_SYS_RESOURCE_H
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
#include <sys/resource.h>
#else /* HAVE_SETPRIORITY && HAVE_SYS_RESOURCE_H */
#if TM_IN_SYS_TIME
#include <sys/time.h>
#else /* TM_IN_SYS_TIME */
#include <time.h>
#endif /* TM_IN_SYS_TIME */
#endif /* HAVE_SETPRIORITY && HAVE_SYS_RESOURCE_H */
#if HAVE_STDLIB_H
#include <stdlib.h>
#endif /* HAVE_STDLIB_H */
#if HAVE_UNISTD_H
#include <unistd.h>
#endif /* HAVE_UNISTD_H */
#if HAVE_STRING_H
#include <string.h>
#endif /* HAVE_STRING_H */
#include <errno.h>

#include "opie.h"

static char userbuf[16] = "USER=";
static char homebuf[128] = "HOME=";
static char shellbuf[128] = "SHELL=";
static char pathbuf[sizeof("PATH") + sizeof(DEFAULT_PATH) - 1] = "PATH=";
static char termbuf[32] = "TERM=";
static char *cleanenv[] = {userbuf, homebuf, shellbuf, pathbuf, 0, 0};
static char *user = "root";
static char *shell = "/bin/sh";
static int fulllogin;
#if 0
static int fastlogin;
#else /* 0 */
static int force = 0;
#endif /* 0 */

static char currentuser[65];

extern char **environ;
static struct passwd thisuser, nouser;

#if HAVE_SHADOW_H
#include <shadow.h>
#endif /* HAVE_SHADOW_H */

#if HAVE_CRYPT_H
#include <crypt.h>
#endif /* HAVE_CRYPT_H */

static VOIDRET catchexit FUNCTION_NOARGS
{
  int i;
  closelog();
  for (i = sysconf(_SC_OPEN_MAX); i > 2; i--)
    close(i);
}

/* We allow the malloc()s to potentially leak data out because we can
only call this routine about four times in the lifetime of this process
and the kernel will free all heap memory when we exit or exec. */
static int lookupuser FUNCTION((name), char *name)
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

#if SU_STAR_CHECK
  return ((thisuser.pw_passwd[0] == '*') || (thisuser.pw_passwd[0] == '#'));
#else /* SU_STAR_CHECK */
  return 0;
#endif /* SU_STAR_CHECK */

lookupuserbad:
  memcpy(&thisuser, &nouser, sizeof(thisuser));
  return -1;
}

static VOIDRET lsetenv FUNCTION((ename, eval, buf), char *ename AND char *eval AND char *buf)
{
  register char *cp, *dp;
  register char **ep = environ;

  /* this assumes an environment variable "ename" already exists */
  while (dp = *ep++) {
    for (cp = ename; *cp == *dp && *cp; cp++, dp++)
      continue;
    if (*cp == 0 && (*dp == '=' || *dp == 0)) {
      strcat(buf, eval);
      *--ep = buf;
      return;
    }
  }
}

#if HAVE_SULOG
static int sulog FUNCTION((status, who), int status AND char *who)
{
  char *from;
  char *ttynam;
  struct tm *tm;
  FILE *f;
  time_t now;

  if (who)
    from = who;
  else
    from = currentuser;

  if (!strncmp(ttynam = ttyname(2), "/dev/", 5))
    ttynam += 5;

  now = time(NULL);
  tm = localtime(&now);
  
  if (!(f = fopen("/var/adm/sulog", "a"))) {
    fprintf(stderr, "Can't update su log!\n");
    exit(1);
  }

  fprintf(f, "SU %02d/%02d %02d:%02d %c %s %s-%s\n",
	  tm->tm_mon + 1, tm->tm_mday, tm->tm_hour, tm->tm_min,
	  status ? '+' : '-', ttynam, from, user);
  fclose(f);
}
#endif /* HAVE_SULOG */

int main FUNCTION((argc, argv),	int argc AND char *argv[])
{
  char *p;
  struct opie opie;
  int i;
  char pbuf[256];
  char opieprompt[80];
  int console = 0;
  char *argvbuf;

  for (i = sysconf(_SC_OPEN_MAX); i > 2; i--)
    close(i);

  openlog("su", LOG_ODELAY, LOG_AUTH);
  atexit(catchexit);

  {
  int argvsize = 0;
  for (i = 0; i < argc; argvsize += strlen(argv[i++]));
  argvsize += argc;
  if (!(argvbuf = malloc(argvsize))) {
    syslog(LOG_ERR, "can't allocate memory to store command line");
    exit(1);
  };
  for (i = 0, *argvbuf = 0; i < argc;) {
    strcat(argvbuf, argv[i]);
    if (++i < argc)
      strcat(argvbuf, " ");
  };
  };

  strcat(pathbuf, DEFAULT_PATH);

again:
  if (argc > 1 && strcmp(argv[1], "-f") == 0) {
#if 0
    fastlogin++;
#else /* 0 */
#if INSECURE_OVERRIDE
    force = 1;
#else /* INSECURE_OVERRIDE */
    fprintf(stderr, "Sorry, but the -f option is not supported by this build of OPIE.\n");
#endif /* INSECURE_OVERRIDE */
#endif /* 0 */
    argc--, argv++;
    goto again;
  }
  if (argc > 1 && strcmp(argv[1], "-c") == 0) {
    console++;
    argc--, argv++;
    goto again;
  }
  if (argc > 1 && strcmp(argv[1], "-") == 0) {
    fulllogin++;
    argc--;
    argv++;
    goto again;
  }
  if (argc > 1 && argv[1][0] != '-') {
    user = argv[1];
    argc--;
    argv++;
  }


  {
  struct passwd *pwd;
  char *p = getlogin();
  char buf[32];

  if ((pwd = getpwuid(getuid())) == NULL) {
    syslog(LOG_CRIT, "'%s' failed for unknown uid %d on %s", argvbuf, getuid(), ttyname(2));
#if HAVE_SULOG
    sulog(0, "unknown");
#endif /* HAVE_SULOG */
    exit(1);
  }
  opiestrncpy(buf, pwd->pw_name, sizeof(buf));

  if (!p)
    p = "unknown";

  opiestrncpy(currentuser, p, 31);

  if (p && *p && strcmp(currentuser, buf)) {
    strcat(currentuser, "(");
    strcat(currentuser, buf);
    strcat(currentuser, ")");
  };

  if (lookupuser(user)) {
    syslog(LOG_CRIT, "'%s' failed for %s on %s", argvbuf, currentuser, ttyname(2));
#if HAVE_SULOG
    sulog(0, NULL);
#endif /* HAVE_SULOG */
    fprintf(stderr, "Unknown user: %s\n", user);
    exit(1);
  }

  if (geteuid()) {
    syslog(LOG_CRIT, "'%s' failed for %s on %s: not running with superuser priveleges", argvbuf, currentuser, ttyname(2));
#if HAVE_SULOG
    sulog(0, NULL);
#endif /* HAVE_SULOG */
    fprintf(stderr, "You do not have permission to su %s\n", user);
    exit(1);
  };

/* Implement the BSD "wheel group" su restriction. */
#if DOWHEEL
  /* Only allow those in group zero to su to root? */
  if (thisuser.pw_uid == 0) {
    struct group *gr;
    if ((gr = getgrgid(0)) != NULL) {
      for (i = 0; gr->gr_mem[i] != NULL; i++)
	if (strcmp(buf, gr->gr_mem[i]) == 0)
	  goto userok;
      fprintf(stderr, "You do not have permission to su %s\n", user);
      exit(1);
    }
userok:
    ;
#if HAVE_SETPRIORITY && HAVE_SYS_RESOURCE_H
    setpriority(PRIO_PROCESS, 0, -2);
#endif /* HAVE_SETPRIORITY && HAVE_SYS_RESOURCE_H */
  }
#endif	/* DOWHEEL */
  };

  if (!thisuser.pw_passwd[0] || getuid() == 0)
    goto ok;

  if (console) {
    if (!opiealways(thisuser.pw_dir)) {
      fprintf(stderr, "That account requires OTP responses.\n");
      exit(1);
    };
    /* Get user's secret password */
    fprintf(stderr, "Reminder - Only use this method from the console; NEVER from remote. If you\n");
    fprintf(stderr, "are using telnet, xterm, or a dial-in, type ^C now or exit with no password.\n");
    fprintf(stderr, "Then run su without the -c parameter.\n");
    if (opieinsecure()) {
      fprintf(stderr, "Sorry, but you don't seem to be on the console or a secure terminal.\n");
#if INSECURE_OVERRIDE
    if (force)
      fprintf(stderr, "Warning: Continuing could disclose your secret pass phrase to an attacker!\n");
    else
#endif /* INSECURE_OVERRIDE */
      exit(1);
    };
#if NEW_PROMPTS
    printf("%s's system password: ", thisuser.pw_name);
    if (!opiereadpass(pbuf, sizeof(pbuf), 0))
      goto error;
#endif /* NEW_PROMPTS */
  } else {
    /* Attempt an OTP challenge */
    i = opiechallenge(&opie, user, opieprompt);
    printf("%s\n", opieprompt);
#if NEW_PROMPTS
    printf("%s's response: ", thisuser.pw_name);
    if (!opiereadpass(pbuf, sizeof(pbuf), 1))
      goto error;
#else /* NEW_PROMPTS */
    printf("(OTP response required)\n");
#endif /* NEW_PROMPTS */
    fflush(stdout);
  };
#if !NEW_PROMPTS
  printf("%s's password: ", thisuser.pw_name);
  if (!opiereadpass(pbuf, sizeof(pbuf), 0))
    goto error;
#endif /* !NEW_PROMPTS */

#if !NEW_PROMPTS
  if (!pbuf[0] && !console) {
    /* Null line entered; turn echoing back on and read again */
    printf(" (echo on)\n%s's password: ", thisuser.pw_name);
    if (!opiereadpass(pbuf, sizeof(pbuf), 1))
      goto error;
  }
#endif /* !NEW_PROMPTS */

  if (console) {
    /* Try regular password check, if allowed */
    if (!strcmp(crypt(pbuf, thisuser.pw_passwd), thisuser.pw_passwd))
      goto ok;
  } else {
    int i = opiegetsequence(&opie);
    if (!opieverify(&opie, pbuf)) {
      /* OPIE authentication succeeded */
      if (i < 5)
	fprintf(stderr, "Warning: Change %s's OTP secret pass phrase NOW!\n", user);
      else
	if (i < 10)
	  fprintf(stderr, "Warning: Change %s's OTP secret pass phrase soon.\n", user);
      goto ok;
    };
  };
error:
  if (!console)
    opieverify(&opie, "");
  fprintf(stderr, "Sorry\n");
  syslog(LOG_CRIT, "'%s' failed for %s on %s", argvbuf, currentuser, ttyname(2));
#if HAVE_SULOG
  sulog(0, NULL);
#endif /* HAVE_SULOG */
  exit(2);

ok:
  syslog(LOG_NOTICE, "'%s' by %s on %s", argvbuf, currentuser, ttyname(2));
#if HAVE_SULOG
  sulog(1, NULL);
#endif /* HAVE_SULOG */
  
  if (setgid(thisuser.pw_gid) < 0) {
    perror("su: setgid");
    exit(3);
  }
  if (initgroups(user, thisuser.pw_gid)) {
    fprintf(stderr, "su: initgroups failed (errno=%d)\n", errno);
    exit(4);
  }
  if (setuid(thisuser.pw_uid) < 0) {
    perror("su: setuid");
    exit(5);
  }
  if (thisuser.pw_shell && *thisuser.pw_shell)
    shell = thisuser.pw_shell;
  if (fulllogin) {
    if ((p = getenv("TERM")) && (strlen(termbuf) + strlen(p) - 1 < sizeof(termbuf))) {
      strcat(termbuf, p);
      cleanenv[4] = termbuf;
    }
    environ = cleanenv;
  }
  if (fulllogin || strcmp(user, "root") != 0)
    lsetenv("USER", thisuser.pw_name, userbuf);
  lsetenv("SHELL", shell, shellbuf);
  lsetenv("HOME", thisuser.pw_dir, homebuf);

#if HAVE_SETPRIORITY && HAVE_SYS_RESOURCE_H
  setpriority(PRIO_PROCESS, 0, 0);
#endif /* HAVE_SETPRIORITY && HAVE_SYS_RESOURCE_H */

#if 0
  if (fastlogin) {
    *argv-- = "-f";
    *argv = "su";
  } else
#endif /* 0 */
    if (fulllogin) {
      if (chdir(thisuser.pw_dir) < 0) {
	fprintf(stderr, "No directory\n");
	exit(6);
      }
      *argv = "-su";
    } else {
      *argv = "su";
    }

  catchexit();

#if DEBUG
  syslog(LOG_DEBUG, "execing %s", shell);
#endif /* DEBUG */
  execv(shell, argv);
  fprintf(stderr, "No shell\n");
  exit(7);
}
