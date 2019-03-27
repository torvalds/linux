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
 * File: am-utils/hlfsd/homedir.c
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
 * STATIC VARIABLES AND FUNCTIONS:
 */
static FILE *passwd_fp = NULL;
static char pw_name[16], pw_dir[128];
static int cur_pwtab_num = 0, max_pwtab_num = 0;
static int hlfsd_diskspace(char *);
static int hlfsd_stat(char *, struct stat *);
static int passwd_line = 0;
static int plt_reset(void);
static struct passwd passwd_ent;
static uid2home_t *lastchild;
static uid2home_t *pwtab;
static void delay(uid2home_t *, int);
static void table_add(u_int, const char *, const char *);
static char mboxfile[MAXPATHLEN];
static char *root_home;		/* root's home directory */

/* GLOBAL FUNCTIONS */
char *homeof(char *username);
int uidof(char *username);

/* GLOBALS VARIABLES */
username2uid_t *untab;		/* user name table */

/*
 * Return the home directory pathname for the user with uid "userid".
 */
char *
homedir(int userid, int groupid)
{
  static char linkval[MAXPATHLEN + 1];
  static struct timeval tp;
  uid2home_t *found;
  char *homename;
  struct stat homestat;
  int old_groupid, old_userid;

  if ((found = plt_search(userid)) == (uid2home_t *) NULL) {
    return alt_spooldir;	/* use alt spool for unknown uid */
  }
  homename = found->home;

  if (homename[0] != '/' || homename[1] == '\0') {
    found->last_status = 1;
    return alt_spooldir;	/* use alt spool for / or rel. home */
  }
  if ((int) userid == 0)	/* force all uid 0 to use root's home */
    xsnprintf(linkval, sizeof(linkval), "%s/%s", root_home, home_subdir);
  else
    xsnprintf(linkval, sizeof(linkval), "%s/%s", homename, home_subdir);

  if (noverify) {
    found->last_status = 0;
    return linkval;
  }

  /*
   * To optimize hlfsd, we don't actually check the validity of the
   * symlink if it has been checked in the last N seconds.  It is
   * very likely that the link, machine, and filesystem are still
   * valid, as long as N is small.  But if N is large, that may not be
   * true.  That's why the default N is 5 minutes, but we allow the
   * user to override this value via a command line option.  Note that
   * we do not update the last_access_time each time it is accessed,
   * but only once every N seconds.
   */
  if (gettimeofday(&tp, (struct timezone *) NULL) < 0) {
    tp.tv_sec = 0;
  } else {
    if ((tp.tv_sec - found->last_access_time) < cache_interval) {
      if (found->last_status == 0) {
	return linkval;
      } else {
	return alt_spooldir;
      }
    } else {
      found->last_access_time = tp.tv_sec;
    }
  }

  /*
   * Only run this forking code if ask for -D fork (default).
   * Disable forking using -D nofork.
   */
  if (amuDebug(D_FORK)) {
    /* fork child to process request if none in progress */
    if (found->child && kill(found->child, 0))
      found->child = 0;

    if (found->child)
      delay(found, 5);		/* wait a bit if in progress */
    if (found->child) {		/* better safe than sorry - maybe */
      found->last_status = 1;
      return alt_spooldir;
    }
    if ((found->child = fork()) < 0) {
      found->last_status = 1;
      return alt_spooldir;
    }
    if (found->child) {		/* PARENT */
      if (lastchild)
	dlog("cache spill uid = %ld, pid = %ld, home = %s",
	     (long) lastchild->uid, (long) lastchild->child,
	     lastchild->home);
      lastchild = found;
      return (char *) NULL;	/* return NULL to parent, so it can continue */
    }
  }

  /*
   * CHILD: (or parent if -D fork)
   *
   * Check and create dir if needed.
   * Check disk space and/or quotas too.
   *
   * We don't need to set the _last_status field of found after the fork
   * in the child, b/c that information would be later determined in
   * nfsproc_readlink_2() and the correct exit status would be returned
   * to the parent upon SIGCHLD in interlock().
   *
   */
  am_set_mypid();		/* for logging routines */
  if ((old_groupid = setgid(groupid)) < 0) {
    plog(XLOG_WARNING, "could not setgid to %d: %m", groupid);
    return linkval;
  }
  if ((old_userid = seteuid(userid)) < 0) {
    plog(XLOG_WARNING, "could not seteuid to %d: %m", userid);
    setgid(old_groupid);
    return linkval;
  }
  if (hlfsd_stat(linkval, &homestat) < 0) {
    if (errno == ENOENT) {	/* make the spool dir if possible */
      /* don't use recursive mkdirs here */
      if (mkdir(linkval, PERS_SPOOLMODE) < 0) {
	seteuid(old_userid);
	setgid(old_groupid);
	plog(XLOG_WARNING, "can't make directory %s: %m", linkval);
	return alt_spooldir;
      }
      /* fall through to testing the disk space / quota */
    } else {			/* the home dir itself must not exist then */
      seteuid(old_userid);
      setgid(old_groupid);
      plog(XLOG_WARNING, "bad link to %s: %m", linkval);
      return alt_spooldir;
    }
  }

  /*
   * If gets here, then either the spool dir in the home dir exists,
   * or it was just created.  In either case, we now need to
   * test if we can create a small file and write at least one
   * byte into it.  This will test that we have both enough inodes
   * and disk blocks to spare, or they fall within the user's quotas too.
   * We are still seteuid to the user at this point.
   */
  if (hlfsd_diskspace(linkval) < 0) {
    seteuid(old_userid);
    setgid(old_groupid);
    plog(XLOG_WARNING, "no more space in %s: %m", linkval);
    return alt_spooldir;
  } else {
    seteuid(old_userid);
    setgid(old_groupid);
    return linkval;
  }
}


static int
hlfsd_diskspace(char *path)
{
  char buf[MAXPATHLEN];
  int fd, len;

  xsnprintf(buf, sizeof(buf), "%s/._hlfstmp_%lu", path, (long) getpid());
  if ((fd = open(buf, O_RDWR | O_CREAT, 0600)) < 0) {
    plog(XLOG_ERROR, "cannot open %s: %m", buf);
    return -1;
  }
  len = strlen(buf);
  if (write(fd, buf, len) < len) {
    plog(XLOG_ERROR, "cannot write \"%s\" (%d bytes) to %s : %m", buf, len, buf);
    close(fd);
    unlink(buf);		/* cleanup just in case */
    return -1;
  }
  if (unlink(buf) < 0) {
    plog(XLOG_ERROR, "cannot unlink %s : %m", buf);
  }
  close(fd);
  return 0;
}


static int
hlfsd_stat(char *path, struct stat *statp)
{
  if (stat(path, statp) < 0)
    return -1;
  else if (!S_ISDIR(statp->st_mode)) {
    errno = ENOTDIR;
    return -1;
  }
  return 0;
}


static void
delay(uid2home_t *found, int secs)
{
  struct timeval tv;

  if (found)
    dlog("delaying on child %ld for %d seconds", (long) found->child, secs);

  tv.tv_usec = 0;

  do {
    tv.tv_sec = secs;
    if (select(0, NULL, NULL, NULL, &tv) == 0)
      break;
  } while (--secs && found->child);
}


/*
 * This function is called when a child has terminated after
 * servicing an nfs request.  We need to check the exit status and
 * update the last_status field of the requesting user.
 */
RETSIGTYPE
interlock(int signum)
{
  int child;
  uid2home_t *lostchild;
  int status;

#ifdef HAVE_WAITPID
  while ((child = waitpid((pid_t) -1, &status, WNOHANG)) > 0) {
#else /* not HAVE_WAITPID */
  while ((child = wait3(&status, WNOHANG, (struct rusage *) NULL)) > 0) {
#endif /* not HAVE_WAITPID */

    /* high chances this was the last child forked */
    if (lastchild && lastchild->child == child) {
      lastchild->child = 0;

      if (WIFEXITED(status))
	lastchild->last_status = WEXITSTATUS(status);
      lastchild = (uid2home_t *) NULL;
    } else {
      /* and if not, we have to search for it... */
      for (lostchild = pwtab; lostchild < &pwtab[cur_pwtab_num]; lostchild++) {
	if (lostchild->child == child) {
	  if (WIFEXITED(status))
	    lostchild->last_status = WEXITSTATUS(status);
	  lostchild->child = 0;
	  break;
	}
      }
    }
  }
}


/*
 * PASSWORD AND USERNAME LOOKUP TABLES FUNCTIONS
 */

/*
 * get index of UserName table entry which matches username.
 * must not return uid_t because we want to return a negative number.
 */
int
untab_index(char *username)
{
  int max, min, mid, cmp;

  max = cur_pwtab_num - 1;
  min = 0;

  do {
    mid = (max + min) / 2;
    cmp = strcmp(untab[mid].username, username);
    if (cmp == 0)		/* record found! */
      return mid;
    if (cmp > 0)
      max = mid;
    else
      min = mid;
  } while (max > min + 1);

  if (STREQ(untab[max].username, username))
    return max;
  if (STREQ(untab[min].username, username))
    return min;

  /* if gets here then record was not found */
  return -1;
}


/*
 * Don't make this return a uid_t, because we need to return negative
 * numbers as well (error codes.)
 */
int
uidof(char *username)
{
  int idx;

  if ((idx = untab_index(username)) < 0)	/* not found */
    return INVALIDID;			/* an invalid user id */
  return untab[idx].uid;
}


/*
 * Don't make this return a uid_t, because we need to return negative
 * numbers as well (error codes.)
 */
char *
homeof(char *username)
{
  int idx;

  if ((idx = untab_index(username)) < 0)	/* not found */
    return (char *) NULL;	/* an invalid user id */
  return untab[idx].home;
}


char *
mailbox(int uid, char *username)
{
  char *home;

  if (uid < 0)
    return (char *) NULL;	/* not found */

  if ((home = homeof(username)) == (char *) NULL)
    return (char *) NULL;
  if (STREQ(home, "/"))
    xsnprintf(mboxfile, sizeof(mboxfile),
	      "/%s/%s", home_subdir, username);
  else
    xsnprintf(mboxfile, sizeof(mboxfile),
	      "%s/%s/%s", home, home_subdir, username);
  return mboxfile;
}


static int
plt_compare_fxn(const voidp x, const voidp y)

{
  uid2home_t *i = (uid2home_t *) x;
  uid2home_t *j = (uid2home_t *) y;

  return i->uid - j->uid;
}


static int
unt_compare_fxn(const voidp x, const voidp y)
{
  username2uid_t *i = (username2uid_t *) x;
  username2uid_t *j = (username2uid_t *) y;

  return strcmp(i->username, j->username);
}


/* perform initialization of user passwd database */
static void
hlfsd_setpwent(void)
{
  if (!passwdfile) {
    setpwent();
    return;
  }

  passwd_fp = fopen(passwdfile, "r");
  if (!passwd_fp) {
    plog(XLOG_ERROR, "unable to read passwd file %s: %m", passwdfile);
    return;
  }
  plog(XLOG_INFO, "reading password entries from file %s", passwdfile);

  passwd_line = 0;
  memset((char *) &passwd_ent, 0, sizeof(struct passwd));
  passwd_ent.pw_name = (char *) &pw_name;
  passwd_ent.pw_dir = (char *) &pw_dir;
}


/* perform de-initialization of user passwd database */
static void
hlfsd_endpwent(void)
{
  if (!passwdfile) {
    /*
     * Don't actually run this because we will be making more passwd calls
     * afterwards.  On Solaris 2.5.1, making getpwent() calls after calling
     * endpwent() results in a memory leak! (and no, even Purify didn't
     * detect it...)
     *
     endpwent();
     */
    return;
  }

  if (passwd_fp) {
    fclose(passwd_fp);
  }
}


/* perform record reading/parsing of individual passwd database records */
static struct passwd *
hlfsd_getpwent(void)
{
  char buf[256], *cp;

  /* check if to perform standard unix function */
  if (!passwdfile) {
    return getpwent();
  }

  /* return here to read another entry */
readent:

  /* return NULL if reached end of file */
  if (feof(passwd_fp))
    return NULL;

  pw_name[0] = pw_dir[0] = '\0';

  /* read records */
  buf[0] = '\0';
  if (fgets(buf, 256, passwd_fp) == NULL)
    return NULL;
  passwd_line++;
  if (buf[0] == '\0')
    goto readent;

  /* read user name */
  cp = strtok(buf, ":");
  if (!cp || cp[0] == '\0') {
    plog(XLOG_ERROR, "no user name on line %d of %s", passwd_line, passwdfile);
    goto readent;
  }
  /* pw_name will show up in passwd_ent.pw_name */
  xstrlcpy(pw_name, cp, sizeof(pw_name));

  /* skip passwd */
  strtok(NULL, ":");

  /* read uid */
  cp = strtok(NULL, ":");
  if (!cp || cp[0] == '\0') {
    plog(XLOG_ERROR, "no uid on line %d of %s", passwd_line, passwdfile);
    goto readent;
  }
  passwd_ent.pw_uid = atoi(cp);

  /* skip gid and gcos */
  strtok(NULL, ":");
  strtok(NULL, ":");

  /* read home dir */
  cp = strtok(NULL, ":");
  if (!cp || cp[0] == '\0') {
    plog(XLOG_ERROR, "no home dir on line %d of %s", passwd_line,  passwdfile);
    goto readent;
  }
  /* pw_dir will show up in passwd_ent.pw_dir */
  xstrlcpy(pw_dir, cp, sizeof(pw_dir));

  /* the rest of the fields are unimportant and not being considered */

  plog(XLOG_USER, "hlfsd_getpwent: name=%s, uid=%ld, dir=%s",
       passwd_ent.pw_name, (long) passwd_ent.pw_uid, passwd_ent.pw_dir);

  return &passwd_ent;
}


/*
 * read and hash the passwd file or NIS map
 */
void
plt_init(void)
{
  struct passwd *pent_p;

  if (plt_reset() < 0)		/* could not reset table. skip. */
    return;

  plog(XLOG_INFO, "reading password map");

  hlfsd_setpwent();			/* prepare to read passwd entries */
  while ((pent_p = hlfsd_getpwent()) != (struct passwd *) NULL) {
    table_add(pent_p->pw_uid, pent_p->pw_dir, pent_p->pw_name);
    if (STREQ("root", pent_p->pw_name)) {
      int len;
      if (root_home)
	XFREE(root_home);
      root_home = xstrdup(pent_p->pw_dir);
      len = strlen(root_home);
      /* remove any trailing '/' chars from root's home (even if just one) */
      while (len > 0 && root_home[len - 1] == '/') {
	len--;
	root_home[len] = '\0';
      }
    }
  }
  hlfsd_endpwent();

  qsort((char *) pwtab, cur_pwtab_num, sizeof(uid2home_t),
	plt_compare_fxn);
  qsort((char *) untab, cur_pwtab_num, sizeof(username2uid_t),
	unt_compare_fxn);

  if (!root_home)
    root_home = xstrdup("");

  plog(XLOG_INFO, "password map read and sorted");
}


/*
 * This is essentially so that we don't reset known good lookup tables when a
 * YP server goes down.
 */
static int
plt_reset(void)
{
  int i;

  hlfsd_setpwent();
  if (hlfsd_getpwent() == (struct passwd *) NULL) {
    hlfsd_endpwent();
    return -1;			/* did not reset table */
  }
  hlfsd_endpwent();

  lastchild = (uid2home_t *) NULL;

  if (max_pwtab_num > 0)	/* was used already. cleanup old table */
    for (i = 0; i < cur_pwtab_num; ++i) {
      if (pwtab[i].home) {
	XFREE(pwtab[i].home);
	pwtab[i].home = (char *) NULL;
      }
      pwtab[i].uid = INVALIDID;	/* not a valid uid (yet...) */
      pwtab[i].child = (pid_t) 0;
      pwtab[i].uname = (char *) NULL;	/* only a ptr to untab[i].username */
      if (untab[i].username) {
	XFREE(untab[i].username);
	untab[i].username = (char *) NULL;
      }
      untab[i].uid = INVALIDID;	/* invalid uid */
      untab[i].home = (char *) NULL;	/* only a ptr to pwtab[i].home  */
    }
  cur_pwtab_num = 0;		/* zero current size */

  if (root_home)
    XFREE(root_home);

  return 0;			/* resetting ok */
}


/*
 * u: uid number
 * h: home directory
 * n: user ID name
 */
static void
table_add(u_int u, const char *h, const char *n)
{
  int i;

  if (max_pwtab_num <= 0) {	/* was never initialized */
    max_pwtab_num = 1;
    pwtab = (uid2home_t *) xmalloc(max_pwtab_num *
				   sizeof(uid2home_t));
    memset((char *) &pwtab[0], 0, max_pwtab_num * sizeof(uid2home_t));
    untab = (username2uid_t *) xmalloc(max_pwtab_num *
				       sizeof(username2uid_t));
    memset((char *) &untab[0], 0, max_pwtab_num * sizeof(username2uid_t));
  }

  /* check if need more space. */
  if (cur_pwtab_num + 1 > max_pwtab_num) {
    /* need more space in table */
    max_pwtab_num *= 2;
    plog(XLOG_INFO, "reallocating table spaces to %d entries", max_pwtab_num);
    pwtab = (uid2home_t *) xrealloc(pwtab,
				    sizeof(uid2home_t) * max_pwtab_num);
    untab = (username2uid_t *) xrealloc(untab,
					sizeof(username2uid_t) *
					max_pwtab_num);
    /* zero out newly added entries */
    for (i=cur_pwtab_num; i<max_pwtab_num; ++i) {
      memset((char *) &pwtab[i], 0, sizeof(uid2home_t));
      memset((char *) &untab[i], 0, sizeof(username2uid_t));
    }
  }

  /* do NOT add duplicate entries (this is an O(N^2) algorithm... */
  for (i=0; i<cur_pwtab_num; ++i)
    if (u == pwtab[i].uid  &&  u != 0 ) {
      dlog("ignoring duplicate home %s for uid %d (already %s)",
	   h, u, pwtab[i].home);
      return;
    }

  /* add new password entry */
  pwtab[cur_pwtab_num].home = xstrdup(h);
  pwtab[cur_pwtab_num].child = 0;
  pwtab[cur_pwtab_num].last_access_time = 0;
  pwtab[cur_pwtab_num].last_status = 0;	/* assume best: used homedir */
  pwtab[cur_pwtab_num].uid = u;

  /* add new userhome entry */
  untab[cur_pwtab_num].username = xstrdup(n);

  /* just a second pointer */
  pwtab[cur_pwtab_num].uname = untab[cur_pwtab_num].username;
  untab[cur_pwtab_num].uid = u;
  untab[cur_pwtab_num].home = pwtab[cur_pwtab_num].home;	/* a ptr */

  /* increment counter */
  ++cur_pwtab_num;
}


/*
 * return entry in lookup table
 */
uid2home_t *
plt_search(u_int u)
{
  int max, min, mid;

  /*
   * empty table should not happen,
   * but I have a bug with signals to trace...
   */
  if (pwtab == (uid2home_t *) NULL)
    return (uid2home_t *) NULL;

  max = cur_pwtab_num - 1;
  min = 0;

  do {
    mid = (max + min) / 2;
    if (pwtab[mid].uid == u)	/* record found! */
      return &pwtab[mid];
    if (pwtab[mid].uid > u)
      max = mid;
    else
      min = mid;
  } while (max > min + 1);

  if (pwtab[max].uid == u)
    return &pwtab[max];
  if (pwtab[min].uid == u)
    return &pwtab[min];

  /* if gets here then record was not found */
  return (uid2home_t *) NULL;
}


#if defined(DEBUG) || defined(DEBUG_PRINT)
void
plt_print(int signum)
{
  FILE *dumpfile;
  int dumpfd;
  char dumptmp[] = "/usr/tmp/hlfsd.dump.XXXXXX";
  int i;

#ifdef HAVE_MKSTEMP
  dumpfd = mkstemp(dumptmp);
#else /* not HAVE_MKSTEMP */
  mktemp(dumptmp);
  if (!dumptmp) {
    plog(XLOG_ERROR, "cannot create temporary dump file");
    return;
  }
  dumpfd = open(dumptmp, O_RDONLY);
#endif /* not HAVE_MKSTEMP */
  if (dumpfd < 0) {
    plog(XLOG_ERROR, "cannot open temporary dump file");
    return;
  }
  if ((dumpfile = fdopen(dumpfd, "a")) != NULL) {
    plog(XLOG_INFO, "dumping internal state to file %s", dumptmp);
    fprintf(dumpfile, "\n\nNew plt_dump():\n");
    for (i = 0; i < cur_pwtab_num; ++i)
      fprintf(dumpfile,
	      "%4d %5lu %10lu %1d %4lu \"%s\" uname=\"%s\"\n",
	      i,
	      (long) pwtab[i].child,
	      pwtab[i].last_access_time,
	      pwtab[i].last_status,
	      (long) pwtab[i].uid,
	      pwtab[i].home,
	      pwtab[i].uname);
    fprintf(dumpfile, "\nUserName table by plt_print():\n");
    for (i = 0; i < cur_pwtab_num; ++i)
      fprintf(dumpfile, "%4d : \"%s\" %4lu \"%s\"\n", i,
	      untab[i].username, (long) untab[i].uid, untab[i].home);
    close(dumpfd);
    fclose(dumpfile);
  }
}


void
plt_dump(uid2home_t *lastc, pid_t this)
{
  FILE *dumpfile;
  int i;

  if ((dumpfile = fopen("/var/tmp/hlfsdump", "a")) != NULL) {
    fprintf(dumpfile, "\n\nNEW PLT_DUMP -- ");
    fprintf(dumpfile, "lastchild->child=%d ",
	    (int) (lastc ? lastc->child : -999));
    fprintf(dumpfile, ", child from wait3=%lu:\n", (long) this);
    for (i = 0; i < cur_pwtab_num; ++i)
      fprintf(dumpfile, "%4d %5lu: %4lu \"%s\" uname=\"%s\"\n", i,
	      (long) pwtab[i].child, (long) pwtab[i].uid,
	      pwtab[i].home, pwtab[i].uname);
    fprintf(dumpfile, "\nUserName table by plt_dump():\n");
    for (i = 0; i < cur_pwtab_num; ++i)
      fprintf(dumpfile, "%4d : \"%s\" %4lu \"%s\"\n", i,
	      untab[i].username, (long) untab[i].uid, untab[i].home);
    fprintf(dumpfile, "ezk: ent=%d, uid=%lu, home=\"%s\"\n",
	    untab_index("ezk"),
	    (long) untab[untab_index("ezk")].uid,
	    pwtab[untab[untab_index("ezk")].uid].home);
    fprintf(dumpfile, "rezk: ent=%d, uid=%lu, home=\"%s\"\n",
	    untab_index("rezk"),
	    (long) untab[untab_index("rezk")].uid,
	    pwtab[untab[untab_index("rezk")].uid].home);
    fclose(dumpfile);
  }
}
#endif /* defined(DEBUG) || defined(DEBUG_PRINT) */
