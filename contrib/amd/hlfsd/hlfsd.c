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
 * File: am-utils/hlfsd/hlfsd.c
 *
 * HLFSD was written at Columbia University Computer Science Department, by
 * Erez Zadok <ezk@cs.columbia.edu> and Alexander Dupuy <dupuy@cs.columbia.edu>
 * It is being distributed under the same terms and conditions as amd does.
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif /* HAVE_CONFIG_H */
#include <am_defs.h>
#include <hlfsd.h>

/*
 * STATIC VARIABLES:
 */
static RETSIGTYPE proceed(int);
static RETSIGTYPE reaper(int);
static RETSIGTYPE reload(int);
static char *hlfs_group = DEFAULT_HLFS_GROUP;
static char default_dir_name[] = DEFAULT_DIRNAME;
static char *dir_name = default_dir_name;
static int printpid = 0;
static int stoplight = 0;
static void hlfsd_init(void);
static void usage(void);

static struct itimerval reloadinterval = {
  {DEFAULT_INTERVAL, 0},
  {DEFAULT_INTERVAL, 0}
};

/*
 * default mount options.
 */
static char default_mntopts[] = "ro,noac";

/*
 * GLOBALS:
 */
SVCXPRT *nfsxprt;
char *alt_spooldir = ALT_SPOOLDIR;
char *home_subdir = HOME_SUBDIR;
char *logfile = DEFAULT_LOGFILE;
char *passwdfile = NULL;	/* alternate passwd file to use */
char *slinkname = NULL;
char hostname[MAXHOSTNAMELEN + 1] = "localhost";
u_int cache_interval = DEFAULT_CACHE_INTERVAL;
gid_t hlfs_gid = (gid_t) INVALIDID;
int masterpid = 0;
int noverify = 0;
int orig_umask = 022;
int serverpid = 0;
nfstime startup;
u_short nfs_port;

/* symbol must be available always */
#ifdef MNTTAB_FILE_NAME
char *mnttab_file_name = MNTTAB_FILE_NAME;
#else /* not MNTTAB_FILE_NAME */
char *mnttab_file_name = NULL;
#endif /* not MNTTAB_FILE_NAME */

/* forward declarations */
void hlfsd_going_down(int rc);
void fatalerror(char *str);


static void
usage(void)
{
  fprintf(stderr,
	  "Usage: %s [-Cfhnpv] [-a altdir] [-c cache-interval] [-g group]\n",
	  am_get_progname());
  fprintf(stderr, "\t[-i interval] [-l logfile] [-o mntopts] [-P passwdfile]\n");
  show_opts('x', xlog_opt);
#ifdef DEBUG
  show_opts('D', dbg_opt);
#endif /* DEBUG */
  fprintf(stderr, "\t[dir_name [subdir]]\n");
  exit(2);
}


void
fatalerror(char *str)
{
#define ERRM ": %m"
  size_t l = strlen(str) + sizeof(ERRM) - 1;
  char *tmp = strnsave(str, l);
  xstrlcat(tmp, ERRM, l);
  fatal(tmp);
}


int
main(int argc, char *argv[])
{
  char *dot;
  char *mntopts = (char *) NULL;
  char hostpid_fs[MAXHOSTNAMELEN + 1 + 16];	/* room for ":(pid###)" */
  char progpid_fs[PROGNAMESZ + 1 + 11];		/* room for ":pid" */
  char preopts[128];
  char *progname;
  int forcecache = 0;
  int forcefast = 0;
  int genflags = 0;
  int opt, ret;
  int opterrs = 0;
  int retry;
  int soNFS;			/* NFS socket */
  int s = -99;
  mntent_t mnt;
  nfs_args_t nfs_args;
  am_nfs_handle_t anh;
  struct dirent *direntry;
  struct group *grp;
  struct stat stmodes;
  DIR *mountdir;
  MTYPE_TYPE type = MOUNT_TYPE_NFS;

#ifdef HAVE_SIGACTION
  struct sigaction sa;
#endif /* not HAVE_SIGACTION */

#ifndef HAVE_TRANSPORT_TYPE_TLI
  struct sockaddr_in localsocket;
#endif /* not HAVE_TRANSPORT_TYPE_TLI */


  /* get program name and truncate so we don't overflow progpid_fs */

  if ((progname = strrchr(argv[0], '/')) != NULL)
    progname++;
  else
    progname = argv[0];
  if ((int) strlen(progname) > PROGNAMESZ) /* truncate to reasonable size */
    progname[PROGNAMESZ] = '\0';
  am_set_progname(progname);

  while ((opt = getopt(argc, argv, "a:c:CD:fg:hi:l:no:pP:x:v")) != -1)
    switch (opt) {

    case 'a':
      if (!optarg || optarg[0] != '/') {
	printf("%s: invalid directory for -a: %s\n",
	       am_get_progname(), optarg);
	exit(3);
      }
      alt_spooldir = optarg;
      break;

    case 'c':
      if (!atoi(optarg)) {
	printf("%s: invalid interval for -c: %s\n",
	       am_get_progname(), optarg);
	exit(3);
      }
      cache_interval = atoi(optarg);
      break;

    case 'C':
      forcecache++;
      break;

    case 'f':
      forcefast++;
      break;

    case 'g':
      hlfs_group = optarg;
      break;

    case 'i':
      if (!atoi(optarg)) {
	printf("%s: invalid interval for -i: %s\n",
	       am_get_progname(), optarg);
	exit(3);
      }
      reloadinterval.it_interval.tv_sec = atoi(optarg);
      reloadinterval.it_value.tv_sec = atoi(optarg);
      break;

    case 'l':
      logfile = optarg;
      break;

    case 'n':
      noverify++;
      break;

    case 'o':
      mntopts = optarg;
      break;

    case 'p':
      printpid++;
      break;

    case 'P':
      passwdfile = optarg;
      break;

    case 'v':
      fprintf(stderr, "%s\n", HLFSD_VERSION);
      exit(0);

    case 'x':
      opterrs += switch_option(optarg);
      break;

    case 'D':
#ifdef DEBUG
      opterrs += debug_option(optarg);
#else /* not DEBUG */
      fprintf(stderr, "%s: not compiled with DEBUG -- sorry.\n", am_get_progname());
#endif /* not DEBUG */
      break;

    case 'h':
    case '?':
      opterrs++;
    }

  /* need my pid before any dlog/plog */
  am_set_mypid();
#ifdef DEBUG
  switch_option("debug");
#endif /* DEBUG */

/*
 * Terminate if did not ask to forcecache (-C) and hlfsd would not be able
 * to set the minimum cache intervals.
 */
#if !defined(MNT2_NFS_OPT_ACREGMIN) && !defined(MNT2_NFS_OPT_NOAC) && !defined(HAVE_NFS_ARGS_T_ACREGMIN)
  if (!forcecache) {
    fprintf(stderr, "%s: will not be able to turn off attribute caches.\n", am_get_progname());
    exit(1);
  }
#endif /* !defined(MNT2_NFS_OPT_ACREGMIN) && !defined(MNT2_NFS_OPT_NOAC) && !defined(HAVE_NFS_ARGS_T_ACREGMIN) */


  switch (argc - optind) {
  case 2:
    home_subdir = argv[optind + 1];
  case 1:
    dir_name = argv[optind];
  case 0:
    break;
  default:
    opterrs++;
  }

  if (opterrs)
    usage();

  /* ensure that only root can run hlfsd */
  if (geteuid()) {
    fprintf(stderr, "hlfsd can only be run as root\n");
    exit(1);
  }
  setbuf(stdout, (char *) NULL);
  umask(0);

  /* find gid for hlfs_group */
  if ((grp = getgrnam(hlfs_group)) == (struct group *) NULL) {
    fprintf(stderr, "%s: cannot get gid for group \"%s\".\n",
	    am_get_progname(), hlfs_group);
  } else {
    hlfs_gid = grp->gr_gid;
  }

  /* get hostname for logging and open log before we reset umask */
  if (gethostname(hostname, sizeof(hostname)) == -1) {
    fprintf(stderr, "%s: gethostname failed \"%s\".\n",
	    am_get_progname(), strerror(errno));
    exit(1);
  }
  hostname[sizeof(hostname) - 1] = '\0';
  if ((dot = strchr(hostname, '.')) != NULL)
    *dot = '\0';
  orig_umask = umask(0);
  if (logfile)
    switch_to_logfile(logfile, orig_umask, 0);

#ifndef MOUNT_TABLE_ON_FILE
  if (amuDebug(D_MTAB))
    dlog("-D mtab option ignored");
#endif /* not MOUNT_TABLE_ON_FILE */

  /* avoid hanging on other NFS servers if started elsewhere */
  if (chdir("/") < 0)
    fatal("cannot chdir to /: %m");

  if (geteuid() != 0)
    fatal("must be root to mount filesystems");

  /*
   * dir_name must match "^(/.*)/([^/]+)$", and is split at last '/' with
   * slinkname = `basename $dir_name` - requires dir_name be writable
   */

  if (dir_name[0] != '/'
      || ((slinkname = strrchr(dir_name, '/')), *slinkname++ = '\0',
	  (dir_name[0] == '\0' || slinkname[0] == '\0'))) {
    if (slinkname)
      *--slinkname = '/';
    printf("%s: invalid mount directory/link %s\n",
	   am_get_progname(), dir_name);
    exit(3);
  }

  if (!forcefast) {
    /* make sure mount point exists and is at least mode 555 */
    if (stat(dir_name, &stmodes) < 0)
      if (errno != ENOENT || mkdirs(dir_name, 0555) < 0
	  || stat(dir_name, &stmodes) < 0)
	fatalerror(dir_name);

    if ((stmodes.st_mode & 0555) != 0555) {
      fprintf(stderr, "%s: directory %s not read/executable\n",
	      am_get_progname(), dir_name);
      plog(XLOG_WARNING, "directory %s not read/executable",
	   dir_name);
    }

    /* warn if extraneous stuff will be hidden by mount */
    if ((mountdir = opendir(dir_name)) == NULL)
      fatalerror(dir_name);

    while ((direntry = readdir(mountdir)) != NULL) {
      if (!NSTREQ(".", direntry->d_name, NAMLEN(direntry)) &&
	  !NSTREQ("..", direntry->d_name, NAMLEN(direntry)) &&
	  !NSTREQ(slinkname, direntry->d_name, NAMLEN(direntry)))
	break;
    }

    if (direntry != NULL) {
      fprintf(stderr, "%s: %s/%s will be hidden by mount\n",
	      am_get_progname(), dir_name, direntry->d_name);
      plog(XLOG_WARNING, "%s/%s will be hidden by mount\n",
	   dir_name, direntry->d_name);
    }
    closedir(mountdir);

    /* make sure alternate spool dir exists */
    if ((errno = mkdirs(alt_spooldir, OPEN_SPOOLMODE))) {
      fprintf(stderr, "%s: cannot create alternate dir ",
	      am_get_progname());
      perror(alt_spooldir);
      plog(XLOG_ERROR, "cannot create alternate dir %s: %m",
	   alt_spooldir);
    }
    chmod(alt_spooldir, OPEN_SPOOLMODE);

    /* create failsafe link to alternate spool directory */
    *(slinkname-1) = '/';	/* unsplit dir_name to include link */
    if (lstat(dir_name, &stmodes) == 0 &&
	(stmodes.st_mode & S_IFMT) != S_IFLNK) {
      fprintf(stderr, "%s: failsafe %s not a symlink\n",
	      am_get_progname(), dir_name);
      plog(XLOG_WARNING, "failsafe %s not a symlink\n",
	   dir_name);
    } else {
      unlink(dir_name);

      if (symlink(alt_spooldir, dir_name) < 0) {
	fprintf(stderr,
		"%s: cannot create failsafe symlink %s -> ",
		am_get_progname(), dir_name);
	perror(alt_spooldir);
	plog(XLOG_WARNING,
	     "cannot create failsafe symlink %s -> %s: %m",
	     dir_name, alt_spooldir);
      }
    }

    *(slinkname-1) = '\0';	/* resplit dir_name */
  } /* end of "if (!forcefast) {" */

  /*
   * Register hlfsd as an nfs service with the portmapper.
   */
  ret = create_nfs_service(&soNFS, &nfs_port, &nfsxprt, nfs_program_2,
    NFS_VERSION);
  if (ret != 0)
    fatal("cannot create NFS service");

#ifdef HAVE_SIGACTION
  sa.sa_handler = proceed;
  sa.sa_flags = 0;
  sigemptyset(&(sa.sa_mask));
  sigaddset(&(sa.sa_mask), SIGUSR2);
  sigaction(SIGUSR2, &sa, NULL);
#else /* not HAVE_SIGACTION */
  signal(SIGUSR2, proceed);
#endif /* not HAVE_SIGACTION */

  plog(XLOG_INFO, "Initializing hlfsd...");
  hlfsd_init();			/* start up child (forking) to run svc_run */

#ifdef HAVE_SIGACTION
  sa.sa_handler = reaper;
  sa.sa_flags = 0;
  sigemptyset(&(sa.sa_mask));
  sigaddset(&(sa.sa_mask), SIGCHLD);
  sigaction(SIGCHLD, &sa, NULL);
#else /* not HAVE_SIGACTION */
  signal(SIGCHLD, reaper);
#endif /* not HAVE_SIGACTION */

  /*
   * In the parent, if -D nodaemon, we don't need to
   * set this signal handler.
   */
  if (amuDebug(D_DAEMON)) {
    s = -99;
    while (stoplight != SIGUSR2) {
      plog(XLOG_INFO, "parent waits for child to setup (stoplight=%d)", stoplight);
#ifdef HAVE_SIGSUSPEND
      {
	sigset_t mask;
	sigemptyset(&mask);
	s = sigsuspend(&mask);	/* wait for child to set up */
      }
#else /* not HAVE_SIGSUSPEND */
      s = sigpause(0);		/* wait for child to set up */
#endif /* not HAVE_SIGSUSPEND */
      sleep(1);
    }
  }

  /*
   * setup options to mount table (/etc/{mtab,mnttab}) entry
   */
  xsnprintf(hostpid_fs, sizeof(hostpid_fs),
	    "%s:(pid%d)", hostname, masterpid);
  memset((char *) &mnt, 0, sizeof(mnt));
  mnt.mnt_dir = dir_name;	/* i.e., "/mail" */
  mnt.mnt_fsname = hostpid_fs;
  if (mntopts) {
    mnt.mnt_opts = mntopts;
  } else {
    xstrlcpy(preopts, default_mntopts, sizeof(preopts));
    /*
     * Turn off all kinds of attribute and symlink caches as
     * much as possible.  Also make sure that mount does not
     * show up to df.
     */
#ifdef MNTTAB_OPT_INTR
    xstrlcat(preopts, ",", sizeof(preopts));
    xstrlcat(preopts, MNTTAB_OPT_INTR, sizeof(preopts));
#endif /* MNTTAB_OPT_INTR */
#ifdef MNTTAB_OPT_IGNORE
    xstrlcat(preopts, ",", sizeof(preopts));
    xstrlcat(preopts, MNTTAB_OPT_IGNORE, sizeof(preopts));
#endif /* MNTTAB_OPT_IGNORE */
#ifdef MNT2_GEN_OPT_CACHE
    xstrlcat(preopts, ",nocache", sizeof(preopts));
#endif /* MNT2_GEN_OPT_CACHE */
#ifdef MNT2_NFS_OPT_SYMTTL
    xstrlcat(preopts, ",symttl=0", sizeof(preopts));
#endif /* MNT2_NFS_OPT_SYMTTL */
    mnt.mnt_opts = preopts;
  }

  /*
   * Make sure that amd's top-level NFS mounts are hidden by default
   * from df.
   * If they don't appear to support the either the "ignore" mnttab
   * option entry, or the "auto" one, set the mount type to "nfs".
   */
#ifdef HIDE_MOUNT_TYPE
  mnt.mnt_type = HIDE_MOUNT_TYPE;
#else /* not HIDE_MOUNT_TYPE */
  mnt.mnt_type = "nfs";
#endif /* not HIDE_MOUNT_TYPE */
  /* some systems don't have a mount type, but a mount flag */

#ifndef HAVE_TRANSPORT_TYPE_TLI
  amu_get_myaddress(&localsocket.sin_addr, NULL);
  localsocket.sin_family = AF_INET;
  localsocket.sin_port = htons(nfsxprt->xp_port);
#endif /* not HAVE_TRANSPORT_TYPE_TLI */

  /*
   * Update hostname field.
   * Make some name prog:pid (i.e., hlfsd:174) for hostname
   */
  xsnprintf(progpid_fs, sizeof(progpid_fs),
	    "%s:%d", am_get_progname(), masterpid);

  /* Most kernels have a name length restriction. */
  if ((int) strlen(progpid_fs) >= (int) MAXHOSTNAMELEN)
    xstrlcpy(progpid_fs + MAXHOSTNAMELEN - 3, "..",
	     sizeof(progpid_fs) - MAXHOSTNAMELEN + 3);

  genflags = compute_mount_flags(&mnt);

  retry = hasmntval(&mnt, MNTTAB_OPT_RETRY);
  if (retry <= 0)
    retry = 1;			/* XXX */

  memmove(&anh.v2, root_fhp, sizeof(*root_fhp));
#ifdef HAVE_TRANSPORT_TYPE_TLI
  compute_nfs_args(&nfs_args,
		   &mnt,
		   genflags,
		   nfsncp,
		   NULL,	/* remote host IP addr is set below */
		   NFS_VERSION,	/* version 2 */
		   "udp",	/* XXX: shouldn't this be "udp"? */
		   &anh,
		   progpid_fs,	/* host name for kernel */
		   hostpid_fs); /* filesystem name for kernel */
  /*
   * IMPORTANT: set the correct IP address AFTERWARDS.  It cannot
   * be done using the normal mechanism of compute_nfs_args(), because
   * that one will allocate a new address and use NFS_SA_DREF() to copy
   * parts to it, while assuming that the ip_addr passed is always
   * a "struct sockaddr_in".  That assumption is incorrect on TLI systems,
   * because they define a special macro HOST_SELF which is DIFFERENT
   * than localhost (127.0.0.1)!
   */
  nfs_args.addr = &nfsxprt->xp_ltaddr;
#else /* not HAVE_TRANSPORT_TYPE_TLI */
  compute_nfs_args(&nfs_args,
		   &mnt,
		   genflags,
		   NULL,
		   &localsocket,
		   NFS_VERSION, /* version 2 */
		   "udp",	/* XXX: shouldn't this be "udp"? */
		   &anh,
		   progpid_fs,	/* host name for kernel */
		   hostpid_fs); /* filesystem name for kernel */
#endif /* not HAVE_TRANSPORT_TYPE_TLI */

  /*************************************************************************
   * NOTE: while compute_nfs_args() works ok for regular NFS mounts	   *
   * the toplvl one is not, and so some options must be corrected by hand  *
   * more carefully, *after* compute_nfs_args() runs.			   *
   *************************************************************************/
  compute_automounter_nfs_args(&nfs_args, &mnt);

/*
 * For some reason, this mount may have to be done in the background, if I am
 * using -D daemon.  I suspect that the actual act of mounting requires
 * calling to hlfsd itself to invoke one or more of its nfs calls, to stat
 * /mail.  That means that even if you say -D daemon, at least the mount
 * of hlfsd itself on top of /mail will be done in the background.
 * The other alternative I have is to run svc_run, but set a special
 * signal handler to perform the mount in N seconds via some alarm.
 *      -Erez Zadok.
 */
  if (!amuDebug(D_DAEMON)) {	/* Normal case */
    plog(XLOG_INFO, "normal NFS mounting hlfsd service points");
    if (mount_fs(&mnt, genflags, (caddr_t) &nfs_args, retry, type, 0, NULL, mnttab_file_name, 0) < 0)
      fatal("nfsmount: %m");
  } else {			/* asked for -D daemon */
    if (fork() == 0) {		/* child runs mount */
      am_set_mypid();
      foreground = 0;
      plog(XLOG_INFO, "child NFS mounting hlfsd service points");
      if (mount_fs(&mnt, genflags, (caddr_t) &nfs_args, retry, type, 0, NULL, mnttab_file_name, 0) < 0) {
	fatal("nfsmount: %m");
      }
      exit(0);			/* all went well */
    } else { /* fork failed or parent running */
      plog(XLOG_INFO, "parent waiting 1sec for mount...");
    }
  }

#ifdef HAVE_TRANSPORT_TYPE_TLI
  /*
   * XXX: this free_knetconfig() was not done for hlfsd before,
   * and apparently there was a reason for it, but why? -Erez
   */
  free_knetconfig(nfs_args.knconf);
  /*
   * local automounter mounts do not allocate a special address, so
   * no need to XFREE(nfs_args.addr) under TLI.
   */
#endif /* HAVE_TRANSPORT_TYPE_TLI */

  if (printpid)
    printf("%d\n", masterpid);

  plog(XLOG_INFO, "hlfsd ready to serve");
  /*
   * If asked not to fork a daemon (-D nodaemon), then hlfsd_init()
   * will not run svc_run.  We must start svc_run here.
   */
  if (!amuDebug(D_DAEMON)) {
    plog(XLOG_DEBUG, "starting no-daemon debugging svc_run");
    svc_run();
  }

  cleanup(0);			/* should never happen here */
  return (0);			/* everything went fine? */
}


static void
hlfsd_init(void)
{
  int child = 0;
#ifdef HAVE_SIGACTION
  struct sigaction sa;
#endif /* HAVE_SIGACTION */

  /*
   * Initialize file handles.
   */
  plog(XLOG_INFO, "initializing hlfsd file handles");
  hlfsd_init_filehandles();

  /*
   * If -D daemon then we must fork.
   */
  if (amuDebug(D_DAEMON))
    child = fork();

  if (child < 0)
    fatal("fork: %m");

  if (child != 0) {		/* parent process - save child pid */
    masterpid = child;
    am_set_mypid();		/* for logging routines */
    return;
  }

  /*
   * CHILD CODE:
   * initialize server
   */

  plog(XLOG_INFO, "initializing home directory database");
  plt_init();			/* initialize database */
  plog(XLOG_INFO, "home directory database initialized");

  masterpid = serverpid = am_set_mypid(); /* for logging routines */

  /*
   * SIGALRM/SIGHUP: reload password database if timer expired
   * or user sent HUP signal.
   */
#ifdef HAVE_SIGACTION
  sa.sa_handler = reload;
  sa.sa_flags = 0;
  sigemptyset(&(sa.sa_mask));
  sigaddset(&(sa.sa_mask), SIGALRM);
  sigaddset(&(sa.sa_mask), SIGHUP);
  sigaction(SIGALRM, &sa, NULL);
  sigaction(SIGHUP, &sa, NULL);
#else /* not HAVE_SIGACTION */
  signal(SIGALRM, reload);
  signal(SIGHUP, reload);
#endif /* not HAVE_SIGACTION */

  /*
   * SIGTERM: cleanup and exit.
   */
#ifdef HAVE_SIGACTION
  sa.sa_handler = cleanup;
  sa.sa_flags = 0;
  sigemptyset(&(sa.sa_mask));
  sigaddset(&(sa.sa_mask), SIGTERM);
  sigaction(SIGTERM, &sa, NULL);
#else /* not HAVE_SIGACTION */
  signal(SIGTERM, cleanup);
#endif /* not HAVE_SIGACTION */

  /*
   * SIGCHLD: interlock synchronization and testing
   */
#ifdef HAVE_SIGACTION
  sa.sa_handler = interlock;
  sa.sa_flags = 0;
  sigemptyset(&(sa.sa_mask));
  sigaddset(&(sa.sa_mask), SIGCHLD);
  sigaction(SIGCHLD, &sa, NULL);
#else /* not HAVE_SIGACTION */
  signal(SIGCHLD, interlock);
#endif /* not HAVE_SIGACTION */

  /*
   * SIGUSR1: dump internal hlfsd maps/cache to file
   */
#ifdef HAVE_SIGACTION
# if defined(DEBUG) || defined(DEBUG_PRINT)
  sa.sa_handler = plt_print;
# else /* not defined(DEBUG) || defined(DEBUG_PRINT) */
  sa.sa_handler = SIG_IGN;
# endif /* not defined(DEBUG) || defined(DEBUG_PRINT) */
  sa.sa_flags = 0;
  sigemptyset(&(sa.sa_mask));
  sigaddset(&(sa.sa_mask), SIGUSR1);
  sigaction(SIGUSR1, &sa, NULL);
#else /* not HAVE_SIGACTION */
# if defined(DEBUG) || defined(DEBUG_PRINT)
  signal(SIGUSR1, plt_print);
# else /* not defined(DEBUG) || defined(DEBUG_PRINT) */
  signal(SIGUSR1, SIG_IGN);
# endif /* not defined(DEBUG) || defined(DEBUG_PRINT) */
#endif /* not HAVE_SIGACTION */

  if (setitimer(ITIMER_REAL, &reloadinterval, (struct itimerval *) NULL) < 0)
    fatal("setitimer: %m");

  clocktime(&startup);

  /*
   * If -D daemon, then start serving here in the child,
   * and the parent will exit.  But if -D nodaemon, then
   * skip this code and make sure svc_run is entered elsewhere.
   */
  if (amuDebug(D_DAEMON)) {
    /*
     * Dissociate from the controlling terminal
     */
    amu_release_controlling_tty();

    /*
     * signal parent we are ready. parent should
     * mount(2) and die.
     */
    if (kill(getppid(), SIGUSR2) < 0)
      fatal("kill: %m");
    plog(XLOG_INFO, "starting svc_run");
    svc_run();
    cleanup(0);		/* should never happen, just in case */
  }

}


static RETSIGTYPE
proceed(int signum)
{
  stoplight = signum;
}


static RETSIGTYPE
reload(int signum)
{
  int child;
  int status;

  if (getpid() != masterpid)
    return;

  /*
   * If received a SIGHUP, close and reopen the log file (so that it
   * can be rotated)
   */
  if (signum == SIGHUP && logfile)
    switch_to_logfile(logfile, orig_umask, 0);

  /*
   * parent performs the reload, while the child continues to serve
   * clients accessing the home dir link.
   */
  if ((child = fork()) > 0) {
    serverpid = child;		/* parent runs here */
    am_set_mypid();

    plt_init();

    if (kill(child, SIGKILL) < 0) {
      plog(XLOG_ERROR, "kill child: %m");
    } else {			/* wait for child to die before continue */
      if (wait(&status) != child) {
	/*
	 * I took out this line because it generates annoying output.  It
	 * indicates a very small bug in hlfsd which is totally harmless.
	 * It causes hlfsd to work a bit harder than it should.
	 * Nevertheless, I intend on fixing it in a future release.
	 * -Erez Zadok <ezk@cs.columbia.edu>
	 */
	/* plog(XLOG_ERROR, "unknown child"); */
      }
    }
    serverpid = masterpid;
  } else if (child < 0) {
    plog(XLOG_ERROR, "unable to fork: %m");
  } else {
    /* let child handle requests while we reload */
    serverpid = getpid();
    am_set_mypid();
  }
}


RETSIGTYPE
cleanup(int signum)
{
  struct stat stbuf;
  int umount_result;

  if (amuDebug(D_DAEMON)) {
    if (getpid() != masterpid)
      return;

    if (fork() != 0) {
      masterpid = 0;
      am_set_mypid();
      return;
    }
  }
  am_set_mypid();

  for (;;) {
    while ((umount_result = UMOUNT_FS(dir_name, mnttab_file_name, 0)) == EBUSY) {
      dlog("cleanup(): umount delaying for 10 seconds");
      sleep(10);
    }
    if (stat(dir_name, &stbuf) == 0 && stbuf.st_ino == ROOTID) {
      plog(XLOG_ERROR, "unable to unmount %s", dir_name);
      plog(XLOG_ERROR, "suspending, unmount before terminating");
      kill(am_mypid, SIGSTOP);
      continue;			/* retry unmount */
    }
    break;
  }

  if (amuDebug(D_DAEMON)) {
    plog(XLOG_INFO, "cleanup(): killing processes and terminating");
    kill(masterpid, SIGKILL);
    kill(serverpid, SIGKILL);
  }

  plog(XLOG_INFO, "hlfsd terminating with status 0\n");
  _exit(0);
}


static RETSIGTYPE
reaper(int signum)
{
  int result;

  if (wait(&result) == masterpid) {
    _exit(4);
  }
}


void
hlfsd_going_down(int rc)
{
  int mypid = getpid();		/* XXX: should this be the global am_mypid */

  if (mypid == masterpid)
    cleanup(0);
  else if (mypid == serverpid)
    kill(masterpid, SIGTERM);

  exit(rc);
}


void
fatal(char *mess)
{
  if (logfile && !STREQ(logfile, "stderr")) {
    char lessmess[128];
    int messlen;

    messlen = strlen(mess);

    if (!STREQ(&mess[messlen + 1 - sizeof(ERRM)], ERRM))
      fprintf(stderr, "%s: %s\n", am_get_progname(), mess);
    else {
      xstrlcpy(lessmess, mess, sizeof(lessmess));
      lessmess[messlen - 4] = '\0';

      fprintf(stderr, "%s: %s: %s\n",
	      am_get_progname(), lessmess, strerror(errno));
    }
  }
  plog(XLOG_FATAL, "%s", mess);

  hlfsd_going_down(1);
}
