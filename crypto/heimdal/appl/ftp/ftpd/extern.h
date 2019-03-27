/*-
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
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
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
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
 *
 *	@(#)extern.h	8.2 (Berkeley) 4/4/94
 */

#ifndef _EXTERN_H_
#define _EXTERN_H_

#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif
#ifdef HAVE_NETDB_H
#include <netdb.h>
#endif

#include <stdio.h>
#include <stdarg.h>
#ifdef HAVE_PWD_H
#include <pwd.h>
#endif

#ifdef HAVE_LIMITS_H
#include <limits.h>
#endif

#ifndef NBBY
#define NBBY CHAR_BIT
#endif

void	abor(void);
void	blkfree(char **);
char  **copyblk(char **);
void	cwd(const char *);
void	do_delete(char *);
void	dologout(int);
void	eprt(char *);
void	epsv(char *);
void	fatal(char *);
int	filename_check(char *);
int	ftpd_pclose(FILE *);
FILE   *ftpd_popen(char *, char *, int, int);
char   *ftpd_getline(char *, int);
void	ftpd_logwtmp(char *, char *, char *);
void	lreply(int, const char *, ...)
    __attribute__ ((format (printf, 2, 3)));
void	makedir(char *);
void	nack(char *);
void	nreply(const char *, ...)
    __attribute__ ((format (printf, 1, 2)));
void	pass(char *);
void	pasv(void);
void	perror_reply(int, const char *);
void	pwd(void);
void	removedir(char *);
void	renamecmd(char *, char *);
char   *renamefrom(char *);
void	reply(int, const char *, ...)
    __attribute__ ((format (printf, 2, 3)));
void	retrieve(const char *, char *);
void	send_file_list(char *);
void	setproctitle(const char *, ...)
    __attribute__ ((format (printf, 1, 2)));
void	statcmd(void);
void	statfilecmd(char *);
void	do_store(char *, char *, int);
void	upper(char *);
void	user(char *);
void	yyerror(char *);

void	list_file(char*);

void	kauth(char *, char*);
void	klist(void);
void	cond_kdestroy(void);
void	kdestroy(void);
void	krbtkfile(const char *tkfile);
void	afslog(const char *, int);
void	afsunlog(void);

extern int do_destroy_tickets;
extern char *k5ccname;

int	find(char *);

int	builtin_ls(FILE*, const char*);

int	do_login(int code, char *passwd);
int	klogin(char *name, char *password);

const char *ftp_rooted(const char *path);

extern struct sockaddr *ctrl_addr, *his_addr;
extern char hostname[];

extern	struct sockaddr *data_dest;
extern	int logged_in;
extern	struct passwd *pw;
extern	int guest;
extern  int dochroot;
extern	int logging;
extern	int type;
extern off_t file_size;
extern off_t byte_count;
extern	int ccc_passed;

extern	int form;
extern	int debug;
extern	int ftpd_timeout;
extern	int maxtimeout;
extern  int pdata;
extern	char hostname[], remotehost[];
extern	char proctitle[];
extern	int usedefault;
extern  char tmpline[];
extern  int paranoid;

#endif /* _EXTERN_H_ */
