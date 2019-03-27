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
 * File: am-utils/fsinfo/fsinfo.h
 *
 */

extern FILE *pref_open(char *pref, char *hn, void (*hdr) (FILE *, char *), char *arg);
extern auto_tree *new_auto_tree(char *, qelem *);
extern automount *new_automount(char *);
extern char **g_argv;
extern char *autodir;
extern char *bootparams_pref;
extern char *disk_fs_strings[];
extern char *dumpset_pref;
extern char *ether_if_strings[];
extern char *exportfs_pref;
extern char *fsmount_strings[];
extern char *fstab_pref;
extern char *host_strings[];
extern char *mount_pref;
extern char *mount_strings[];
extern char *progname;
extern char *username;
extern char *xcalloc(int, int);
extern char hostname[];
extern char idvbuf[];
extern dict *dict_of_hosts;
extern dict *dict_of_volnames;
extern dict *new_dict(void);
extern dict_ent *dict_locate(dict *, char *);
extern disk_fs *new_disk_fs(void);
extern ether_if *new_ether_if(void);
extern fsmount *new_fsmount(void);
extern host *new_host(void);
extern int dict_iter(dict *, int (*)(qelem *));
extern int errors;
extern int file_io_errors;
extern int parse_errors;
extern int pref_close(FILE *fp);
extern int verbose;
extern ioloc *current_location(void);
extern fsi_mount *new_mount(void);
extern qelem *new_que(void);
extern void analyze_automounts(qelem *);
extern void analyze_hosts(qelem *);
extern void compute_automount_point(char *, size_t, host *, char *);
extern void dict_add(dict *, char *, char *);
extern void error(char *fmt, ...)
	__attribute__((__format__(__printf__, 1, 2)));
extern void fatal(char *fmt, ...)
	__attribute__((__format__(__printf__, 1, 2)));
extern void gen_hdr(FILE *ef, char *hn);
extern void info_hdr(FILE *ef, char *info);
extern void init_que(qelem *);
extern void ins_que(qelem *, qelem *);
extern void lerror(ioloc *l, char *fmt, ...)
	__attribute__((__format__(__printf__, 2, 3)));
extern void fsi_log(char *fmt, ...)
	__attribute__((__format__(__printf__, 1, 2)));
extern void lwarning(ioloc *l, char *fmt, ...)
	__attribute__((__format__(__printf__, 2, 3)));
extern void rem_que(qelem *);
extern void set_disk_fs(disk_fs *, int, char *);
extern void set_fsmount(fsmount *, int, char *);
extern void set_mount(fsi_mount *, int, char *);
extern void show_area_being_processed(char *area, int n);
extern void show_new(char *msg);
extern void warning(void);

extern int fsi_error(const char *fmt, ...)
	__attribute__((__format__(__printf__, 1, 2)));
extern void domain_strip(char *otherdom, char *localdom);
/*
 * some systems such as DU-4.x have a different GNU flex in /usr/bin
 * which automatically generates yywrap macros and symbols.  So I must
 * distinguish between them and when yywrap is actually needed.
 */
#ifndef yywrap
extern int yywrap(void);
#endif /* not yywrap */
extern int fsi_parse(void);
extern int write_atab(qelem *q);
extern int write_bootparams(qelem *q);
extern int write_dumpset(qelem *q);
extern int write_exportfs(qelem *q);
extern int write_fstab(qelem *q);
extern void col_cleanup(int eoj);
extern void set_host(host *hp, int k, char *v);
extern void set_ether_if(ether_if *ep, int k, char *v);
extern int fsi_lex(void);


#define	BITSET(m,b)	((m) |= (1<<(b)))
#define	AM_FIRST(ty, q)	((ty *) ((q)->q_forw))
#define	HEAD(ty, q)	((ty *) q)
#define	ISSET(m,b)	((m) & (1<<(b)))
#define	ITER(v, ty, q) 	for ((v) = AM_FIRST(ty,(q)); (v) != HEAD(ty,(q)); (v) = NEXT(ty,(v)))
#define	AM_LAST(ty, q)	((ty *) ((q)->q_back))
#define	NEXT(ty, q)	((ty *) (((qelem *) q)->q_forw))
