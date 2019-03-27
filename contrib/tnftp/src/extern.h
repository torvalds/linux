/*	$NetBSD: extern.h,v 1.12 2009/11/15 10:12:37 lukem Exp $	*/
/*	from	NetBSD: extern.h,v 1.77 2009/07/13 19:05:41 roy Exp	*/

/*-
 * Copyright (c) 1996-2009 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Luke Mewburn.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*-
 * Copyright (c) 1994 The Regents of the University of California.
 * All rights reserved.
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
 *	@(#)extern.h	8.3 (Berkeley) 10/9/94
 */

/*
 * Copyright (C) 1997 and 1998 WIDE Project.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the project nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE PROJECT AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE PROJECT OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

struct sockaddr;
struct tm;
struct addrinfo;

void	abort_remote(FILE *);
void	abort_squared(int);
void	abortpt(int);
void	abortxfer(int);
void	account(int, char **);
void	ai_unmapped(struct addrinfo *);
int	another(int *, char ***, const char *);
int	auto_fetch(int, char **);
int	auto_put(int, char **, const char *);
void	blkfree(char **);
void	cd(int, char **);
void	cdup(int, char **);
void	changetype(int, int);
void	cleanuppeer(void);
void	cmdabort(int);
void	cmdtimeout(int);
void	cmdscanner(void);
int	command(const char *, ...)
     ;
#ifndef NO_EDITCOMPLETE
unsigned char complete(EditLine *, int);
void	controlediting(void);
#endif /* !NO_EDITCOMPLETE */
void	crankrate(int);
FILE   *dataconn(const char *);
void	delete(int, char **);
void	disconnect(int, char **);
void	do_chmod(int, char **);
void	do_umask(int, char **);
void	domacro(int, char **);
void	doproxy(int, char **);
void	feat(int, char **);
void	fget(int, char **);
int	fileindir(const char *, const char *);
int	foregroundproc(void);
void	formatbuf(char *, size_t, const char *);
void	ftpvis(char *, size_t, const char *, size_t);
int	ftp_login(const char *, const char *, const char *);
void	get(int, char **);
struct cmd *getcmd(const char *);
int	getit(int, char **, int, const char *);
int	get_line(FILE *, char *, size_t, const char **);
struct option *getoption(const char *);
char   *getoptionvalue(const char *);
void	getremoteinfo(void);
int	getreply(int);
char   *globulize(const char *);
char   *gunique(const char *);
void	help(int, char **);
char   *hookup(const char *, const char *);
void	idlecmd(int, char **);
int	initconn(void);
void	intr(int);
int	isipv6addr(const char *);
void	list_vertical(StringList *);
void	lcd(int, char **);
void	lostpeer(int);
void	lpage(int, char **);
void	lpwd(int, char **);
void	ls(int, char **);
void	macdef(int, char **);
void	makeargv(void);
void	makedir(int, char **);
void	mdelete(int, char **);
void	mget(int, char **);
void	mls(int, char **);
void	mlst(int, char **);
void	modtime(int, char **);
void	mput(int, char **);
const char *onoff(int);
void	opts(int, char **);
void	newer(int, char **);
void	page(int, char **);
int	parserate(int, char **, int);
char   *prompt(void);
void	proxabort(int);
void	proxtrans(const char *, const char *, const char *);
void	psabort(int);
void	pswitch(int);
void	put(int, char **);
void	pwd(int, char **);
void	quit(int, char **);
void	quote(int, char **);
void	quote1(const char *, int, char **);
void	recvrequest(const char *, const char *, const char *,
	    const char *, int, int);
void	reget(int, char **);
char   *remglob(char **, int, const char **);
time_t	remotemodtime(const char *, int);
off_t	remotesize(const char *, int);
void	removedir(int, char **);
void	renamefile(int, char **);
void	reset(int, char **);
void	restart(int, char **);
const char *rfc2822time(const struct tm *);
void	rmthelp(int, char **);
void	rmtstatus(int, char **);
char   *rprompt(void);
int	ruserpass(const char *, char **, char **, char **);
void	sendrequest(const char *, const char *, const char *, int);
void	setascii(int, char **);
void	setbell(int, char **);
void	setbinary(int, char **);
void	setcase(int, char **);
void	setcr(int, char **);
void	setdebug(int, char **);
void	setedit(int, char **);
void	setepsv4(int, char **);
void	setepsv6(int, char **);
void	setepsv(int, char **);
void	setform(int, char **);
void	setftmode(int, char **);
void	setgate(int, char **);
void	setglob(int, char **);
void	sethash(int, char **);
void	setnmap(int, char **);
void	setntrans(int, char **);
void	setoption(int, char **);
void	setpassive(int, char **);
void	setpeer(int, char **);
void	setport(int, char **);
void	setpreserve(int, char **);
void	setprogress(int, char **);
void	setprompt(int, char **);
void	setrate(int, char **);
void	setrunique(int, char **);
void	setstruct(int, char **);
void	setsunique(int, char **);
void	settenex(int, char **);
void	settrace(int, char **);
void	setttywidth(int);
void	settype(int, char **);
void	setupsockbufsize(int);
void	setverbose(int, char **);
void	setxferbuf(int, char **);
void	set_option(const char *, const char *, int);
void	shell(int, char **);
void	site(int, char **);
void	sizecmd(int, char **);
char   *slurpstring(void);
void	status(int, char **);
int	strsuftoi(const char *);
void	syst(int, char **);
int	togglevar(int, char **, int *, const char *);
void	unsetoption(int, char **);
void	updatelocalcwd(void);
void	updateremotecwd(void);
void	usage(void);
void	user(int, char **);
int	ftp_connect(int, const struct sockaddr *, socklen_t);
int	ftp_listen(int, int);
int	ftp_poll(struct pollfd *, int, int);
void   *ftp_malloc(size_t);
StringList *ftp_sl_init(void);
void	ftp_sl_add(StringList *, char *);
char   *ftp_strdup(const char *);
