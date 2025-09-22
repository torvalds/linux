/*	$OpenBSD: extern.h,v 1.64 2024/04/17 18:12:12 jca Exp $	*/
/*	$NetBSD: extern.h,v 1.5 1996/03/26 23:54:16 mrg Exp $	*/

/*-
 * Copyright (c) 1992 Keith Muller.
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Keith Muller of the University of California, San Diego.
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
 *	@(#)extern.h	8.2 (Berkeley) 4/18/94
 */

/*
 * External references from each source file
 */

/*
 * ar_io.c
 */
extern const char *arcname;
extern const char *gzip_program;
extern int force_one_volume;
int ar_open(const char *);
void ar_close(int _in_sig);
void ar_drain(void);
int ar_set_wr(void);
int ar_app_ok(void);
int ar_read(char *, int);
int ar_write(char *, int);
int ar_rdsync(void);
int ar_fow(off_t, off_t *);
int ar_rev(off_t );
int ar_next(void);

/*
 * ar_subs.c
 */
extern u_long flcnt;
void list(void);
void extract(void);
void append(void);
void archive(void);
void copy(void);

/*
 * buf_subs.c
 */
extern int blksz;
extern int wrblksz;
extern int maxflt;
extern int rdblksz;
extern off_t wrlimit;
extern off_t rdcnt;
extern off_t wrcnt;
int wr_start(void);
int rd_start(void);
void cp_start(void);
int appnd_start(off_t);
int rd_sync(void);
void pback(char *, int);
int rd_skip(off_t);
void wr_fin(void);
int wr_rdbuf(char *, int);
int rd_wrbuf(char *, int);
int wr_skip(off_t);
int wr_rdfile(ARCHD *, int, off_t *);
int rd_wrfile(ARCHD *, int, off_t *);
void cp_file(ARCHD *, int, int);

/*
 * cpio.c
 */
int cpio_strd(void);
int cpio_trail(ARCHD *, char *, int, int *);
int cpio_endwr(void);
int cpio_id(char *, int);
int cpio_rd(ARCHD *, char *);
off_t cpio_endrd(void);
int cpio_stwr(void);
int cpio_wr(ARCHD *);
int vcpio_id(char *, int);
int crc_id(char *, int);
int crc_strd(void);
int vcpio_rd(ARCHD *, char *);
off_t vcpio_endrd(void);
int crc_stwr(void);
int vcpio_wr(ARCHD *);
int bcpio_id(char *, int);
int bcpio_rd(ARCHD *, char *);
off_t bcpio_endrd(void);
int bcpio_wr(ARCHD *);

/*
 * file_subs.c
 */
int file_creat(ARCHD *);
void file_close(ARCHD *, int);
int lnk_creat(ARCHD *);
int cross_lnk(ARCHD *);
int chk_same(ARCHD *);
int node_creat(ARCHD *);
void set_ftime(const char *, const struct timespec *,
    const struct timespec *, int);
int set_ids(char *, uid_t, gid_t);
void set_pmode(char *, mode_t);
int set_attr(const struct file_times *, int _force_times, mode_t, int _do_mode,
    int _in_sig);
int file_write(int, char *, int, int *, int *, int, char *);
void file_flush(int, char *, int);
void rdfile_close(ARCHD *, int *);
int set_crc(ARCHD *, int);

/*
 * ftree.c
 */
int ftree_start(void);
int ftree_add(char *, int);
void ftree_sel(ARCHD *);
void ftree_skipped_newer(ARCHD *);
void ftree_chk(void);
int next_file(ARCHD *);

/*
 * gen_subs.c
 */
void ls_list(ARCHD *, time_t, FILE *);
void ls_tty(ARCHD *);
void safe_print(const char *, FILE *);
u_long asc_ul(char *, int, int);
int ul_asc(u_long, char *, int, int);
unsigned long long asc_ull(char *, int, int);
int ull_asc(unsigned long long, char *, int, int);
size_t fieldcpy(char *, size_t, const char *, size_t);

/*
 * getoldopt.c
 */
int getoldopt(int, char **, const char *);

/*
 * options.c
 */
extern FSUB fsub[];
extern int ford[];
void options(int, char **);
OPLIST * opt_next(void);
extern char *chdname;

/*
 * pat_rep.c
 */
int rep_add(char *);
int pat_add(char *, char *);
void pat_chk(void);
int pat_sel(ARCHD *);
int pat_match(ARCHD *);
int mod_name(ARCHD *);
int set_dest(ARCHD *, char *, int);
int has_dotdot(const char *);

/*
 * pax.c
 */
extern int act;
extern FSUB *frmt;
extern int cflag;
extern int cwdfd;
extern int dflag;
extern int iflag;
extern int kflag;
extern int lflag;
extern int nflag;
extern int tflag;
extern int uflag;
extern int vflag;
extern int Dflag;
extern int Hflag;
extern int Lflag;
extern int Nflag;
extern int Xflag;
extern int Yflag;
extern int Zflag;
extern int zeroflag;
extern int vfpart;
extern int patime;
extern int pmtime;
extern int nodirs;
extern int pmode;
extern int pids;
extern int rmleadslash;
extern int exit_val;
extern int docrc;
extern char *dirptr;
extern char *argv0;
extern enum op_mode { OP_PAX, OP_TAR, OP_CPIO } op_mode;
extern FILE *listf;
extern int listfd;
extern char *tempfile;
extern char *tempbase;
extern int havechd;


/*
 * sel_subs.c
 */
int sel_chk(ARCHD *);
int grp_add(char *);
int usr_add(char *);
int trng_add(char *);

/*
 * tables.c
 */
int lnk_start(void);
int chk_lnk(ARCHD *);
void purg_lnk(ARCHD *);
void lnk_end(void);
int ftime_start(void);
int chk_ftime(ARCHD *);
int sltab_start(void);
int sltab_add_sym(const char *_path, const char *_value, mode_t _mode);
int sltab_add_link(const char *, const struct stat *);
void sltab_process(int _in_sig);
int name_start(void);
int add_name(char *, int, char *);
void sub_name(char *, int *, int);
#ifndef NOCPIO
int dev_start(void);
int add_dev(ARCHD *);
int map_dev(ARCHD *, u_long, u_long);
#else
# define dev_start()	0
# define add_dev(x)	0
# define map_dev(x,y,z)	0
#endif /* NOCPIO */
int atdir_start(void);
void atdir_end(void);
void add_atdir(char *, dev_t, ino_t, const struct timespec *,
    const struct timespec *);
int do_atdir(const char *, dev_t, ino_t);
int dir_start(void);
void add_dir(char *, struct stat *, int);
void delete_dir(dev_t, ino_t);
void proc_dir(int _in_sig);

/*
 * tar.c
 */
extern int tar_nodir;
extern char *gnu_name_string, *gnu_link_string;
int tar_endwr(void);
off_t tar_endrd(void);
int tar_trail(ARCHD *, char *, int, int *);
int tar_id(char *, int);
int tar_opt(void);
int tar_rd(ARCHD *, char *);
int tar_wr(ARCHD *);
int ustar_id(char *, int);
int ustar_rd(ARCHD *, char *);
int ustar_wr(ARCHD *);
int pax_id(char *, int);
int pax_opt(void);
int pax_wr(ARCHD *);

/*
 * tty_subs.c
 */
int tty_init(void);
void tty_prnt(const char *, ...)
    __attribute__((nonnull(1), format(printf, 1, 2)));
int tty_read(char *, int);
void paxwarn(int, const char *, ...)
    __attribute__((nonnull(2), format(printf, 2, 3)));
void syswarn(int, int, const char *, ...)
    __attribute__((nonnull(3), format(printf, 3, 4)));
