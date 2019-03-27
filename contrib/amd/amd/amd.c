/*
 * Copyright (c) 1997-2014 Erez Zadok
 * Copyright (c) 1989 Jan-Simon Pendry
 * Copyright (c) 1989 Imperial College of Science, Technology & Medicine
 * Copyright (c) 1989 The Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Jan-Simon Pendry at Imperial College, London.
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
 *
 *
 * $Id: amd.c,v 1.8.2.6 2004/01/06 03:15:16 ezk Exp $
 * File: am-utils/amd/amd.c
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * Automounter
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif /* HAVE_CONFIG_H */
#include <am_defs.h>
#include <amd.h>

struct amu_global_options gopt;	/* where global options are stored */

char pid_fsname[SIZEOF_PID_FSNAME]; /* "kiska.southseas.nz:(pid%d)" */
char *hostdomain = "unknown.domain";
#define SIZEOF_HOSTD (2 * MAXHOSTNAMELEN + 1)	/* Host+domain */
char hostd[SIZEOF_HOSTD];	/* Host+domain */
char *endian = ARCH_ENDIAN;	/* Big or Little endian */
char *cpu = HOST_CPU;		/* CPU type */
char *PrimNetName;		/* name of primary network */
char *PrimNetNum;		/* number of primary network */

int immediate_abort;		/* Should close-down unmounts be retried */
int orig_umask = 022;
int select_intr_valid;

jmp_buf select_intr;
struct amd_stats amd_stats;	/* Server statistics */
struct in_addr myipaddr;	/* (An) IP address of this host */
time_t do_mapc_reload = 0;	/* mapc_reload() call required? */

#ifdef HAVE_FS_AUTOFS
int amd_use_autofs = 0;
#endif /* HAVE_FS_AUTOFS */

#ifdef HAVE_SIGACTION
sigset_t masked_sigs;
#endif /* HAVE_SIGACTION */


/*
 * Signal handler:
 * SIGINT - tells amd to do a full shutdown, including unmounting all
 *       filesystem.
 * SIGTERM - tells amd to shutdown now.  Just unmounts the automount nodes.
 */
static RETSIGTYPE
sigterm(int sig)
{
#ifdef REINSTALL_SIGNAL_HANDLER
  signal(sig, sigterm);
#endif /* REINSTALL_SIGNAL_HANDLER */

  switch (sig) {
  case SIGINT:
    immediate_abort = 15;
    break;

  case SIGTERM:
    immediate_abort = -1;
    /* fall through... */

  default:
    plog(XLOG_WARNING, "WARNING: automounter going down on signal %d", sig);
    break;
  }
  if (select_intr_valid)
    longjmp(select_intr, sig);
}


/*
 * Hook for cache reload.
 * When a SIGHUP arrives it schedules a call to mapc_reload
 */
static RETSIGTYPE
sighup(int sig)
{
#ifdef REINSTALL_SIGNAL_HANDLER
  signal(sig, sighup);
#endif /* REINSTALL_SIGNAL_HANDLER */

  if (sig != SIGHUP)
    dlog("spurious call to sighup");
  /*
   * Force a reload by zero'ing the timer
   */
  if (amd_state == Run)
    do_mapc_reload = 0;
}


static RETSIGTYPE
parent_exit(int sig)
{
  /*
   * This signal handler is called during Amd initialization.  The parent
   * forks a child to do all the hard automounting work, and waits for a
   * SIGQUIT signal from the child.  When the parent gets the signal it's
   * supposed to call this handler and exit(3), thus completing the
   * daemonizing process.  Alas, on some systems, especially Linux 2.4/2.6
   * with Glibc, exit(3) doesn't always terminate the parent process.
   * Worse, the parent process now refuses to accept any more SIGQUIT
   * signals -- they are blocked.  What's really annoying is that this
   * doesn't happen all the time, suggesting a race condition somewhere.
   * (This happens even if I change the logic to use another signal.)  I
   * traced this to something which exit(3) does in addition to exiting the
   * process, probably some atexit() stuff or other side-effects related to
   * signal handling.  Either way, since at this stage the parent process
   * just needs to terminate, I'm simply calling _exit(2).  Note also that
   * the OpenGroup doesn't list exit(3) as a recommended "Base Interface"
   * but they do list _exit(2) as one.  This fix seems to work reliably all
   * the time. -Erez (2/27/2005)
   */
  _exit(0);
}


static int
daemon_mode(void)
{
  int bgpid;

#ifdef HAVE_SIGACTION
  struct sigaction sa, osa;

  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = parent_exit;
  sa.sa_flags = 0;
  sigemptyset(&(sa.sa_mask));
  sigaddset(&(sa.sa_mask), SIGQUIT);
  sigaction(SIGQUIT, &sa, &osa);
#else /* not HAVE_SIGACTION */
  signal(SIGQUIT, parent_exit);
#endif /* not HAVE_SIGACTION */

  bgpid = background();

  if (bgpid != 0) {
    /*
     * Now wait for the automount points to
     * complete.
     */
    for (;;)
      pause();
    /* should never reach here */
  }
#ifdef HAVE_SIGACTION
  sigaction(SIGQUIT, &osa, NULL);
#else /* not HAVE_SIGACTION */
  signal(SIGQUIT, SIG_DFL);
#endif /* not HAVE_SIGACTION */

  /*
   * Record our pid to make it easier to kill the correct amd.
   */
  if (gopt.flags & CFM_PRINT_PID) {
    if (STREQ(gopt.pid_file, "/dev/stdout")) {
      printf("%ld\n", (long) am_mypid);
      /* flush stdout, just in case */
      fflush(stdout);
    } else {
      FILE *f;
      mode_t prev_umask = umask(0022); /* set secure temporary umask */

      f = fopen(gopt.pid_file, "w");
      if (f) {
	fprintf(f, "%ld\n", (long) am_mypid);
	(void) fclose(f);
      } else {
	fprintf(stderr, "cannot open %s (errno=%d)\n", gopt.pid_file, errno);
      }
      umask(prev_umask);	/* restore umask */
    }
  }

  /*
   * Pretend we are in the foreground again
   */
  foreground = 1;

  /*
   * Dissociate from the controlling terminal
   */
  amu_release_controlling_tty();

  return getppid();
}


/*
 * Initialize global options structure.
 */
static void
init_global_options(void)
{
#if defined(HAVE_SYS_UTSNAME_H) && defined(HAVE_UNAME)
  static struct utsname un;
#endif /* defined(HAVE_SYS_UTSNAME_H) && defined(HAVE_UNAME) */
  int i;

  memset(&gopt, 0, sizeof(struct amu_global_options));

  /* name of current architecture */
  gopt.arch = HOST_ARCH;

  /* automounter temp dir */
  gopt.auto_dir = "/.amd_mnt";

  /* toplevel attribute cache timeout */
  gopt.auto_attrcache = 0;

  /* cluster name */
  gopt.cluster = NULL;

  /* executable map timeout */
  gopt.exec_map_timeout = AMFS_EXEC_MAP_TIMEOUT;

  /*
   * kernel architecture: this you must get from uname() if possible.
   */
#if defined(HAVE_SYS_UTSNAME_H) && defined(HAVE_UNAME)
  if (uname(&un) >= 0)
    gopt.karch = un.machine;
  else
#endif /* defined(HAVE_SYS_UTSNAME_H) && defined(HAVE_UNAME) */
    gopt.karch = HOST_ARCH;

  /* amd log file */
  gopt.logfile = NULL;

  /* operating system name */
  gopt.op_sys = HOST_OS_NAME;

  /* OS version */
  gopt.op_sys_ver = HOST_OS_VERSION;

  /* full OS name and version */
  gopt.op_sys_full = HOST_OS;

  /* OS version */
  gopt.op_sys_vendor = HOST_VENDOR;

  /* pid file */
  gopt.pid_file = "/dev/stdout";

  /* local domain */
  gopt.sub_domain = NULL;

  /* reset NFS (and toplvl) retransmit counter and retry interval */
  for (i=0; i<AMU_TYPE_MAX; ++i) {
    gopt.amfs_auto_retrans[i] = -1; /* -1 means "never set before" */
    gopt.amfs_auto_timeo[i] = -1; /* -1 means "never set before" */
  }

  /* cache duration */
  gopt.am_timeo = AM_TTL;

  /* dismount interval */
  gopt.am_timeo_w = AM_TTL_W;

  /* map reload intervl */
  gopt.map_reload_interval = ONE_HOUR;

  /*
   * various CFM_* flags that are on by default.
   */
  gopt.flags = CFM_DEFAULT_FLAGS;

#ifdef HAVE_MAP_HESIOD
  /* Hesiod rhs zone */
  gopt.hesiod_base = "automount";
#endif /* HAVE_MAP_HESIOD */

#ifdef HAVE_MAP_LDAP
  /* LDAP base */
  gopt.ldap_base = NULL;

  /* LDAP host ports */
  gopt.ldap_hostports = NULL;

  /* LDAP cache */
  gopt.ldap_cache_seconds = 0;
  gopt.ldap_cache_maxmem = 131072;

  /* LDAP protocol version */
  gopt.ldap_proto_version = 2;
#endif /* HAVE_MAP_LDAP */

#ifdef HAVE_MAP_NIS
  /* YP domain name */
  gopt.nis_domain = NULL;
#endif /* HAVE_MAP_NIS */
}


/*
 * Lock process text and data segment in memory (after forking the daemon)
 */
static void
do_memory_locking(void)
{
#if defined(HAVE_PLOCK) || defined(HAVE_MLOCKALL)
  int locked_ok = 0;
#else /* not HAVE_PLOCK and not HAVE_MLOCKALL */
  plog(XLOG_WARNING, "Process memory locking not supported by the OS");
#endif /* not HAVE_PLOCK and not HAVE_MLOCKALL */
#ifdef HAVE_PLOCK
# ifdef _AIX
  /*
   * On AIX you must lower the stack size using ulimit() before calling
   * plock.  Otherwise plock will reserve a lot of memory space based on
   * your maximum stack size limit.  Since it is not easily possible to
   * tell what should the limit be, I print a warning before calling
   * plock().  See the manual pages for ulimit(1,3,4) on your AIX system.
   */
  plog(XLOG_WARNING, "AIX: may need to lower stack size using ulimit(3) before calling plock");
# endif /* _AIX */
  if (!locked_ok && plock(PROCLOCK) != 0)
    plog(XLOG_WARNING, "Couldn't lock process pages in memory using plock(): %m");
  else
    locked_ok = 1;
#endif /* HAVE_PLOCK */
#ifdef HAVE_MLOCKALL
  if (!locked_ok && mlockall(MCL_CURRENT|MCL_FUTURE) != 0)
    plog(XLOG_WARNING, "Couldn't lock process pages in memory using mlockall(): %m");
  else
    locked_ok = 1;
#endif /* HAVE_MLOCKALL */
#if defined(HAVE_PLOCK) || defined(HAVE_MLOCKALL)
  if (locked_ok)
    plog(XLOG_INFO, "Locked process pages in memory");
#endif /* HAVE_PLOCK || HAVE_MLOCKALL */

#if defined(HAVE_MADVISE) && defined(MADV_PROTECT)
    madvise(NULL, 0, MADV_PROTECT); /* may be redundant of the above worked out */
#endif /* defined(HAVE_MADVISE) && defined(MADV_PROTECT) */
}


int
main(int argc, char *argv[])
{
  char *domdot, *verstr, *vertmp;
  int ppid = 0;
  int error;
  char *progname = NULL;		/* "amd" */
  char hostname[MAXHOSTNAMELEN + 1] = "localhost"; /* Hostname */

  /*
   * Make sure some built-in assumptions are true before we start
   */
  assert(sizeof(nfscookie) >= sizeof(u_int));
  assert(sizeof(int) >= 4);

  /*
   * Set processing status.
   */
  amd_state = Start;

  /*
   * Determine program name
   */
  if (argv[0]) {
    progname = strrchr(argv[0], '/');
    if (progname && progname[1])
      progname++;
    else
      progname = argv[0];
  }
  if (!progname)
    progname = "amd";
  am_set_progname(progname);

  /*
   * Initialize process id.  This is kept
   * cached since it is used for generating
   * and using file handles.
   */
  am_set_mypid();

  /*
   * Get local machine name
   */
  if (gethostname(hostname, sizeof(hostname)) < 0) {
    plog(XLOG_FATAL, "gethostname: %m");
    going_down(1);
    return 1;
  }
  hostname[sizeof(hostname) - 1] = '\0';

  /*
   * Check it makes sense
   */
  if (!*hostname) {
    plog(XLOG_FATAL, "host name is not set");
    going_down(1);
    return 1;
  }

  /*
   * Initialize global options structure.
   */
  init_global_options();

  /*
   * Partially initialize hostd[].  This
   * is completed in get_args().
   */
  if ((domdot = strchr(hostname, '.'))) {
    /*
     * Hostname already contains domainname.
     * Split out hostname and domainname
     * components
     */
    *domdot++ = '\0';
    hostdomain = domdot;
  }
  xstrlcpy(hostd, hostname, sizeof(hostd));
  am_set_hostname(hostname);

  /*
   * Setup signal handlers
   */
  /* SIGINT: trap interrupts for shutdowns */
  setup_sighandler(SIGINT, sigterm);
  /* SIGTERM: trap terminate so we can shutdown cleanly (some chance) */
  setup_sighandler(SIGTERM, sigterm);
  /* SIGHUP: hangups tell us to reload the cache */
  setup_sighandler(SIGHUP, sighup);
  /*
   * SIGCHLD: trap Death-of-a-child.  These allow us to pick up the exit
   * status of backgrounded mounts.  See "sched.c".
   */
  setup_sighandler(SIGCHLD, sigchld);
#ifdef HAVE_SIGACTION
  /* construct global "masked_sigs" used in nfs_start.c */
  sigemptyset(&masked_sigs);
  sigaddset(&masked_sigs, SIGINT);
  sigaddset(&masked_sigs, SIGTERM);
  sigaddset(&masked_sigs, SIGHUP);
  sigaddset(&masked_sigs, SIGCHLD);
#endif /* HAVE_SIGACTION */

  /*
   * Fix-up any umask problems.  Most systems default
   * to 002 which is not too convenient for our purposes
   */
  orig_umask = umask(0);

  /*
   * Figure out primary network name
   */
  getwire(&PrimNetName, &PrimNetNum);

  /*
   * Determine command-line arguments.
   * (Also initialize amd.conf parameters, maps, and more.)
   */
  get_args(argc, argv);

  /*
   * Log version information.
   */
  vertmp = get_version_string();
  verstr = strtok(vertmp, "\n");
  plog(XLOG_INFO, "AM-UTILS VERSION INFORMATION:");
  while (verstr) {
    plog(XLOG_INFO, "%s", verstr);
    verstr = strtok(NULL, "\n");
  }
  XFREE(vertmp);

  /*
   * Get our own IP address so that we can mount the automounter.  We pass
   * localhost_address which could be used as the default localhost
   * name/address in amu_get_myaddress().
   */
  amu_get_myaddress(&myipaddr, gopt.localhost_address);
  plog(XLOG_INFO, "My ip addr is %s", inet_ntoa(myipaddr));

  /* avoid hanging on other NFS servers if started elsewhere */
  if (chdir("/") < 0)
    plog(XLOG_INFO, "cannot chdir to /: %m");

  /*
   * Now check we are root.
   */
  if (geteuid() != 0) {
    plog(XLOG_FATAL, "Must be root to mount filesystems (euid = %ld)", (long) geteuid());
    going_down(1);
    return 1;
  }

#ifdef HAVE_MAP_NIS
  /*
   * If the domain was specified then bind it here
   * to circumvent any default bindings that may
   * be done in the C library.
   */
  if (gopt.nis_domain && yp_bind(gopt.nis_domain)) {
    plog(XLOG_FATAL, "Can't bind to NIS domain \"%s\"", gopt.nis_domain);
    going_down(1);
    return 1;
  }
#endif /* HAVE_MAP_NIS */

  if (amuDebug(D_DAEMON))
    ppid = daemon_mode();

  /*
   * Lock process text and data segment in memory.
   */
  if (gopt.flags & CFM_PROCESS_LOCK) {
    do_memory_locking();
  }

  do_mapc_reload = clocktime(NULL) + gopt.map_reload_interval;

  /*
   * Register automounter with system.
   */
  error = mount_automounter(ppid);
  if (error && ppid)
    kill(ppid, SIGALRM);

#ifdef HAVE_FS_AUTOFS
  /*
   * XXX this should be part of going_down(), but I can't move it there
   * because it would be calling non-library code from the library... ugh
   */
  if (amd_use_autofs)
    destroy_autofs_service();
#endif /* HAVE_FS_AUTOFS */

  going_down(error);

  abort();
  return 1; /* should never get here */
}
