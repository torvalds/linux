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
 * File: am-utils/fsinfo/fsinfo.c
 *
 */

/*
 * fsinfo
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif /* HAVE_CONFIG_H */
#include <am_defs.h>
#include <fsi_data.h>
#include <fsinfo.h>
#include <fsi_gram.h>

/* globals */
char **g_argv;
char *autodir = "/a";
char *progname;
char hostname[MAXHOSTNAMELEN + 1];
char *username;
char idvbuf[1024];
dict *dict_of_hosts;
dict *dict_of_volnames;
int errors;
int file_io_errors;
int parse_errors;
int verbose;
qelem *list_of_automounts;
qelem *list_of_hosts;

/*
 * Output file prefixes
 */
char *bootparams_pref;
char *dumpset_pref;
char *exportfs_pref;
char *fstab_pref;
char *mount_pref;


/*
 * Argument cracking...
 */
static void
fsi_get_args(int c, char *v[])
{
  int ch;
  int usage = 0;
  char *iptr = idvbuf;

  /*
   * Determine program name
   */
  if (v[0]) {
    progname = strrchr(v[0], '/');
    if (progname && progname[1])
      progname++;
    else
      progname = v[0];
  }

  if (!progname)
    progname = "fsinfo";

  while ((ch = getopt(c, v, "a:b:d:e:f:h:m:D:U:I:qv")) != -1)

    switch (ch) {

    case 'a':
      autodir = optarg;
      break;

    case 'b':
      if (bootparams_pref)
	fatal("-b option specified twice");
      bootparams_pref = optarg;
      break;

    case 'd':
      if (dumpset_pref)
	fatal("-d option specified twice");
      dumpset_pref = optarg;
      break;

    case 'h':
      xstrlcpy(hostname, optarg, sizeof(hostname));
      break;

    case 'e':
      if (exportfs_pref)
	fatal("-e option specified twice");
      exportfs_pref = optarg;
      break;

    case 'f':
      if (fstab_pref)
	fatal("-f option specified twice");
      fstab_pref = optarg;
      break;

    case 'm':
      if (mount_pref)
	fatal("-m option specified twice");
      mount_pref = optarg;
      break;

    case 'q':
      verbose = -1;
      break;

    case 'v':
      verbose = 1;
      break;

    case 'I':
    case 'D':
    case 'U':
      /* sizeof(iptr) is actually that of idvbuf.  See declaration above */
      xsnprintf(iptr, sizeof(idvbuf), "-%c%s ", ch, optarg);
      iptr += strlen(iptr);
      break;

    default:
      usage++;
      break;
    }

  if (c != optind) {
    g_argv = v + optind - 1;
#ifdef yywrap
    if (yywrap())
#endif /* yywrap */
      fatal("Cannot read any input files");
  } else {
    usage++;
  }

  if (usage) {
    fprintf(stderr,
	    "\
Usage: %s [-v] [-a autodir] [-h hostname] [-b bootparams] [-d dumpsets]\n\
\t[-e exports] [-f fstabs] [-m automounts]\n\
\t[-I dir] [-D|-U string[=string]] config ...\n", progname);
    exit(1);
  }

  if (g_argv[0])
    fsi_log("g_argv[0] = %s", g_argv[0]);
  else
    fsi_log("g_argv[0] = (nil)");
}


/*
 * Determine username of caller
 */
static char *
find_username(void)
{
  const char *u = getlogin();

  if (!u) {
    struct passwd *pw = getpwuid(getuid());
    if (pw)
      u = pw->pw_name;
  }

  if (!u)
    u = getenv("USER");
  if (!u)
    u = getenv("LOGNAME");
  if (!u)
    u = "root";

  return xstrdup(u);
}


/*
 * MAIN
 */
int
main(int argc, char *argv[])
{
  /*
   * Process arguments
   */
  fsi_get_args(argc, argv);

  /*
   * If no hostname given then use the local name
   */
  if (!*hostname && gethostname(hostname, sizeof(hostname)) < 0) {
    perror("gethostname");
    exit(1);
  }
  hostname[sizeof(hostname) - 1] = '\0';

  /*
   * Get the username
   */
  username = find_username();

  /*
   * New hosts and automounts
   */
  list_of_hosts = new_que();
  list_of_automounts = new_que();

  /*
   * New dictionaries
   */
  dict_of_volnames = new_dict();
  dict_of_hosts = new_dict();

  /*
   * Parse input
   */
  show_area_being_processed("read config", 11);
  if (fsi_parse())
    errors = 1;
  errors += file_io_errors + parse_errors;

  if (errors == 0) {
    /*
     * Do semantic analysis of input
     */
    analyze_hosts(list_of_hosts);
    analyze_automounts(list_of_automounts);
  }

  /*
   * Give up if errors
   */
  if (errors == 0) {
    /*
     * Output data files
     */

    write_atab(list_of_automounts);
    write_bootparams(list_of_hosts);
    write_dumpset(list_of_hosts);
    write_exportfs(list_of_hosts);
    write_fstab(list_of_hosts);
  }
  col_cleanup(1);

  exit(errors);
  return errors; /* should never reach here */
}
