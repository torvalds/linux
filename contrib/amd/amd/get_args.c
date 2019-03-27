/*
 * Copyright (c) 1997-2014 Erez Zadok
 * Copyright (c) 1990 Jan-Simon Pendry
 * Copyright (c) 1990 Imperial College of Science, Technology & Medicine
 * Copyright (c) 1990 The Regents of the University of California.
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
 * File: am-utils/amd/get_args.c
 *
 */

/*
 * Argument decode
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif /* HAVE_CONFIG_H */
#include <am_defs.h>
#include <amd.h>

/* include auto-generated version file */
#include <build_version.h>

char *amu_conf_file = "/etc/amd.conf"; /* default amd configuration file */
char *conf_tag = NULL;		/* default conf file tags to use */
int usage = 0;
int use_conf_file = 0;		/* default don't use amd.conf file */
char *mnttab_file_name = NULL;	/* symbol must be available always */


/*
 * Return the version string (dynamic buffer)
 */
char *
get_version_string(void)
{
  char *vers = NULL;
  char tmpbuf[1024];
  char *wire_buf;
  int wire_buf_len = 0;
  size_t len;		  /* max allocated length (to avoid buf overflow) */

  /*
   * First get dynamic string listing all known networks.
   * This could be a long list, if host has lots of interfaces.
   */
  wire_buf = print_wires();
  if (wire_buf)
    wire_buf_len = strlen(wire_buf);

  len = 2048 + wire_buf_len;
  vers = xmalloc(len);
  xsnprintf(vers, len, "%s\n%s\n%s\n%s\n",
	    "Copyright (c) 1997-2014 Erez Zadok",
	    "Copyright (c) 1990 Jan-Simon Pendry",
	    "Copyright (c) 1990 Imperial College of Science, Technology & Medicine",
	    "Copyright (c) 1990 The Regents of the University of California.");
  xsnprintf(tmpbuf, sizeof(tmpbuf), "%s version %s (build %d).\n",
	    PACKAGE_NAME, PACKAGE_VERSION, AMU_BUILD_VERSION);
  xstrlcat(vers, tmpbuf, len);
  xsnprintf(tmpbuf, sizeof(tmpbuf), "Report bugs to %s.\n", PACKAGE_BUGREPORT);
  xstrlcat(vers, tmpbuf, len);
#if 0
  /*
   * XXX  This block (between from the #if 0 to #endif was in the
   * XXX  original was in the original merge however in the interest
   * XXX  of reproduceable builds and the fact that this is redundant
   * XXX  information, it is effectively removed.
   */
  xsnprintf(tmpbuf, sizeof(tmpbuf), "Configured by %s@%s on date %s.\n",
	    USER_NAME, HOST_NAME, CONFIG_DATE);
  xstrlcat(vers, tmpbuf, len);
  xsnprintf(tmpbuf, sizeof(tmpbuf), "Built by %s@%s on date %s.\n",
	    BUILD_USER, BUILD_HOST, BUILD_DATE);
  xstrlcat(vers, tmpbuf, len);
#endif
  xsnprintf(tmpbuf, sizeof(tmpbuf), "cpu=%s (%s-endian), arch=%s, karch=%s.\n",
	    cpu, endian, gopt.arch, gopt.karch);
  xstrlcat(vers, tmpbuf, len);
  xsnprintf(tmpbuf, sizeof(tmpbuf), "full_os=%s, os=%s, osver=%s, vendor=%s, distro=%s.\n",
	    gopt.op_sys_full, gopt.op_sys, gopt.op_sys_ver, gopt.op_sys_vendor, DISTRO_NAME);
  xstrlcat(vers, tmpbuf, len);
  xsnprintf(tmpbuf, sizeof(tmpbuf), "domain=%s, host=%s, hostd=%s.\n",
	    hostdomain, am_get_hostname(), hostd);
  xstrlcat(vers, tmpbuf, len);

  xstrlcat(vers, "Map support for: ", len);
  mapc_showtypes(tmpbuf, sizeof(tmpbuf));
  xstrlcat(vers, tmpbuf, len);
  xstrlcat(vers, ".\nAMFS: ", len);
  ops_showamfstypes(tmpbuf, sizeof(tmpbuf));
  xstrlcat(vers, tmpbuf, len);
  xstrlcat(vers, ", inherit.\nFS: ", len); /* hack: "show" that we support type:=inherit */
  ops_showfstypes(tmpbuf, sizeof(tmpbuf));
  xstrlcat(vers, tmpbuf, len);

  /* append list of networks if available */
  if (wire_buf) {
    xstrlcat(vers, wire_buf, len);
    XFREE(wire_buf);
  }

  return vers;
}


static void
show_usage(void)
{
  fprintf(stderr,
	  "Usage: %s [-nprvHS] [-a mount_point] [-c cache_time] [-d domain]\n\
\t[-k kernel_arch] [-l logfile%s\n\
\t[-t timeout.retrans] [-w wait_timeout] [-A arch] [-C cluster_name]\n\
\t[-o op_sys_ver] [-O op_sys_name]\n\
\t[-F conf_file] [-T conf_tag]", am_get_progname(),
#ifdef HAVE_SYSLOG
# ifdef LOG_DAEMON
	  "|\"syslog[:facility]\"]"
# else /* not LOG_DAEMON */
	  "|\"syslog\"]"
# endif /* not LOG_DAEMON */
#else /* not HAVE_SYSLOG */
	  "]"
#endif /* not HAVE_SYSLOG */
	  );

#ifdef HAVE_MAP_NIS
  fputs(" [-y nis-domain]\n", stderr);
#else /* not HAVE_MAP_NIS */
  fputc('\n', stderr);
#endif /* HAVE_MAP_NIS */

  show_opts('x', xlog_opt);
#ifdef DEBUG
  show_opts('D', dbg_opt);
#endif /* DEBUG */
  fprintf(stderr, "\t[directory mapname [-map_options]] ...\n");
}


void
get_args(int argc, char *argv[])
{
  int opt_ch, i;
  FILE *fp = stdin;
  char getopt_arguments[] = "+nprvSa:c:d:k:l:o:t:w:x:y:C:D:F:T:O:HA:";
  char *getopt_args;
  int print_version = 0;	/* 1 means we should print version info */

#ifdef HAVE_GNU_GETOPT
  getopt_args = getopt_arguments;
#else /* ! HAVE_GNU_GETOPT */
  getopt_args = &getopt_arguments[1];
#endif /* HAVE_GNU_GETOPT */

  /* if no arguments were passed, try to use /etc/amd.conf file */
  if (argc <= 1)
    use_conf_file = 1;

  while ((opt_ch = getopt(argc, argv, getopt_args)) != -1)
    switch (opt_ch) {

    case 'a':
      if (*optarg != '/') {
	fprintf(stderr, "%s: -a option must begin with a '/'\n",
		am_get_progname());
	exit(1);
      }
      gopt.auto_dir = optarg;
      break;

    case 'c':
      gopt.am_timeo = atoi(optarg);
      if (gopt.am_timeo <= 0)
	gopt.am_timeo = AM_TTL;
      break;

    case 'd':
      gopt.sub_domain = optarg;
      break;

    case 'k':
      gopt.karch = optarg;
      break;

    case 'l':
      gopt.logfile = optarg;
      break;

    case 'n':
      gopt.flags |= CFM_NORMALIZE_HOSTNAMES;
      break;

    case 'o':
      gopt.op_sys_ver = optarg;
      break;

    case 'p':
     gopt.flags |= CFM_PRINT_PID;
      break;

    case 'r':
      gopt.flags |= CFM_RESTART_EXISTING_MOUNTS;
      break;

    case 't':
      /* timeo.retrans (also affects toplvl mounts) */
      {
	char *dot = strchr(optarg, '.');
	int i;
	if (dot)
	  *dot = '\0';
	if (*optarg) {
	  for (i=0; i<AMU_TYPE_MAX; ++i)
	    gopt.amfs_auto_timeo[i] = atoi(optarg);
	}
	if (dot) {
	  for (i=0; i<AMU_TYPE_MAX; ++i)
	    gopt.amfs_auto_retrans[i] = atoi(dot + 1);
	  *dot = '.';
	}
      }
      break;

    case 'v':
      /*
       * defer to print version info after every variable had been
       * initialized.
       */
      print_version++;
      break;

    case 'w':
      gopt.am_timeo_w = atoi(optarg);
      if (gopt.am_timeo_w <= 0)
	gopt.am_timeo_w = AM_TTL_W;
      break;

    case 'x':
      usage += switch_option(optarg);
      break;

    case 'y':
#ifdef HAVE_MAP_NIS
      gopt.nis_domain = optarg;
#else /* not HAVE_MAP_NIS */
      plog(XLOG_USER, "-y: option ignored.  No NIS support available.");
#endif /* not HAVE_MAP_NIS */
      break;

    case 'A':
      gopt.arch = optarg;
      break;

    case 'C':
      gopt.cluster = optarg;
      break;

    case 'D':
#ifdef DEBUG
      usage += debug_option(optarg);
#else /* not DEBUG */
      fprintf(stderr, "%s: not compiled with DEBUG option -- sorry.\n",
	      am_get_progname());
#endif /* not DEBUG */
      break;

    case 'F':
      amu_conf_file = optarg;
      use_conf_file = 1;
      break;

    case 'H':
      show_usage();
      exit(1);
      break;

    case 'O':
      gopt.op_sys = optarg;
      break;

    case 'S':
      gopt.flags &= ~CFM_PROCESS_LOCK; /* turn process locking off */
      break;

    case 'T':
      conf_tag = optarg;
      break;

    default:
      usage = 1;
      break;
    }

  /*
   * amd.conf file: if not command-line arguments were used, or if -F was
   * specified, then use that amd.conf file.  If the file cannot be opened,
   * abort amd.  If it can be found, open it, parse it, and then close it.
   */
  if (use_conf_file && amu_conf_file) {
    fp = fopen(amu_conf_file, "r");
    if (!fp) {
      char buf[128];
      xsnprintf(buf, sizeof(buf), "Amd configuration file (%s)",
		amu_conf_file);
      perror(buf);
      exit(1);
    }
    conf_in = fp;
    conf_parse();
    fclose(fp);
    if (process_all_regular_maps() != 0)
      exit(1);
  }

#ifdef DEBUG
  usage += switch_option("debug");
  /* initialize debug options */
  if (!debug_flags)
    debug_flags = D_CONTROL;	/* CONTROL = "daemon,amq,fork" */
#endif /* DEBUG */

  /* log information regarding amd.conf file */
  if (use_conf_file && amu_conf_file)
    plog(XLOG_INFO, "using configuration file %s", amu_conf_file);

#ifdef HAVE_MAP_LDAP
  /* ensure that if ldap_base is specified, that also ldap_hostports is */
  if (gopt.ldap_hostports && !gopt.ldap_base) {
    fprintf(stderr, "must specify both ldap_hostports and ldap_base\n");
    exit(1);
  }
#endif /* HAVE_MAP_LDAP */

  if (usage) {
    show_usage();
    exit(1);
  }

  while (optind <= argc - 2) {
    char *dir = argv[optind++];
    char *map = argv[optind++];
    char *opts = "";
    if (argv[optind] && *argv[optind] == '-')
      opts = &argv[optind++][1];

    root_newmap(dir, opts, map, NULL);
  }

  if (optind == argc) {
    /*
     * Append domain name to hostname.
     * sub_domain overrides hostdomain
     * if given.
     */
    if (gopt.sub_domain)
      hostdomain = gopt.sub_domain;
    if (*hostdomain == '.')
      hostdomain++;
    xstrlcat(hostd, ".", sizeof(hostd));
    xstrlcat(hostd, hostdomain, sizeof(hostd));

#ifdef MOUNT_TABLE_ON_FILE
    if (amuDebug(D_MTAB))
      if (gopt.debug_mtab_file)
        mnttab_file_name = gopt.debug_mtab_file; /* user supplied debug mtab path */
      else
	mnttab_file_name = DEBUG_MNTTAB_FILE; /* default debug mtab path */
    else
      mnttab_file_name = MNTTAB_FILE_NAME;
#else /* not MOUNT_TABLE_ON_FILE */
    if (amuDebug(D_MTAB))
      dlog("-D mtab option ignored");
# ifdef MNTTAB_FILE_NAME
    mnttab_file_name = MNTTAB_FILE_NAME;
# endif /* MNTTAB_FILE_NAME */
#endif /* not MOUNT_TABLE_ON_FILE */

    /*
     * If the kernel architecture was not specified
     * then use the machine architecture.
     */
    if (gopt.karch == NULL)
      gopt.karch = gopt.arch;

    if (gopt.cluster == NULL)
      gopt.cluster = hostdomain;

    /* sanity checking, normalize values just in case (toplvl too) */
    for (i=0; i<AMU_TYPE_MAX; ++i) {
      if (gopt.amfs_auto_timeo[i] == 0)
	gopt.amfs_auto_timeo[i] = AMFS_AUTO_TIMEO;
      if (gopt.amfs_auto_retrans[i] == 0)
	gopt.amfs_auto_retrans[i] = AMFS_AUTO_RETRANS(i);
      if (gopt.amfs_auto_retrans[i] == 0)
	gopt.amfs_auto_retrans[i] = 3;	/* under very unusual circumstances, could be zero */
    }
  }

  /* finally print version string and exit, if asked for */
  if (print_version) {
    fputs(get_version_string(), stderr);
    exit(0);
  }

  if (switch_to_logfile(gopt.logfile, orig_umask,
			(gopt.flags & CFM_TRUNCATE_LOG)) != 0)
    plog(XLOG_USER, "Cannot switch logfile");

  return;
}
