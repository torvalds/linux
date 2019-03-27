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
 * File: am-utils/hlfsd/hlfsd.h
 *
 * HLFSD was written at Columbia University Computer Science Department, by
 * Erez Zadok <ezk@cs.columbia.edu> and Alexander Dupuy <dupuy@cs.columbia.edu>
 * It is being distributed under the same terms and conditions as amd does.
 */

#ifndef _HLFSD_HLFS_H
#define _HLFSD_HLFS_H

/*
 * MACROS AND CONSTANTS:
 */

#define HLFSD_VERSION	"hlfsd 1.2 (1993-2002)"
#define PERS_SPOOLMODE	0755
#define OPEN_SPOOLMODE	01777
#define DOTSTRING	"."

/*
 * ROOTID and SLINKID are the fixed "faked" node IDs (inodes) for
 * the '.' (also '..') and the one symlink within the hlfs.
 * They must always be unique, and should never match what a UID
 * could be.
 * They used to be -1 and -2, respectively.
 *
 * I used to cast these to (uid_t) but it failed to compile
 * with /opt/SUNWspro/bin/cc because uid_t is long, while struct fattr's
 * uid field is u_int.  Then it failed to compile on some linux systems
 * which define uid_t to be unsigned short, so I used the lowest common
 * size which is unsigned short.
 */
/*
 * XXX: this will cause problems to systems with UIDs greater than
 * MAX_UNSIGNED_SHORT-3.
 */
#define ROOTID		(((unsigned short) ~0) - 1)
#define SLINKID		(((unsigned short) ~0) - 2)
#ifndef INVALIDID
/* this is also defined in include/am_utils.h */
# define INVALIDID	(((unsigned short) ~0) - 3)
#endif /* not INVALIDID */

#define DOTCOOKIE	1
#define DOTDOTCOOKIE	2
#define SLINKCOOKIE	3

#define ALT_SPOOLDIR "/var/hlfs" /* symlink to use if others fail */
#define HOME_SUBDIR ".hlfsdir"	/* dirname in user's home dir */
#define DEFAULT_DIRNAME "/hlfs/home"
#define DEFAULT_INTERVAL 900	/* secs b/t re-reads of the password maps */
#define DEFAULT_CACHE_INTERVAL 300 /* secs during which assume a link is up */
#define DEFAULT_HLFS_GROUP	"hlfs"	/* Group name for special hlfs_gid */

#define PROGNAMESZ	(MAXHOSTNAMELEN - 5)

#ifdef HAVE_SYSLOG
# define DEFAULT_LOGFILE "syslog"
#else /* not HAVE)_SYSLOG */
# define DEFAULT_LOGFILE 0
#endif /* not HAVE)_SYSLOG */


/*
 * TYPEDEFS:
 */
typedef struct uid2home_t uid2home_t;
typedef struct username2uid_t username2uid_t;


/*
 * STRUCTURES:
 */
struct uid2home_t {
  uid_t uid;			/* XXX: with or without UID_OFFSET? */
  pid_t child;
  char *home;			/* really allocated */
  char *uname;			/* an xref ptr to username2uid_t->username */
  u_long last_access_time;
  int last_status;		/* 0=used $HOME/.hlfsspool; !0=used alt dir */
};

struct username2uid_t {
  char *username;		/* really allocated */
  uid_t uid;			/* XXX: with or without UID_OFFSET? */
  char *home;			/* an xref ptr to uid2home_t->home */
};

/*
 * EXTERNALS:
 */
extern RETSIGTYPE cleanup(int);
extern RETSIGTYPE interlock(int);
extern SVCXPRT *nfs_program_2_transp;	/* For quick_reply() */
extern SVCXPRT *nfsxprt;
extern char *alt_spooldir;
extern char *home_subdir;
extern char *homedir(int, int);
extern char *mailbox(int, char *);
extern char *passwdfile;
extern char *slinkname;
extern gid_t hlfs_gid;
extern u_int cache_interval;
extern int noverify;
extern int serverpid;
extern int untab_index(char *username);
extern am_nfs_fh *root_fhp;
extern am_nfs_fh root;
extern nfstime startup;
extern uid2home_t *plt_search(u_int);
extern username2uid_t *untab;	/* user name table */
extern void fatal(char *);
extern void plt_init(void);
extern void hlfsd_init_filehandles(void);

#if defined(DEBUG) || defined(DEBUG_PRINT)
extern void plt_dump(uid2home_t *, pid_t);
extern void plt_print(int);
#endif /* defined(DEBUG) || defined(DEBUG_PRINT) */

#endif /* _HLFSD_HLFS_H */
