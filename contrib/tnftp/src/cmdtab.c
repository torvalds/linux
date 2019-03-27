/*	$NetBSD: cmdtab.c,v 1.11 2009/05/20 12:53:47 lukem Exp $	*/
/*	from	NetBSD: cmdtab.c,v 1.51 2009/04/12 10:18:52 lukem Exp	*/

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

/*
 * Copyright (c) 1985, 1989, 1993, 1994
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
 */

#include "tnftp.h"

#if 0	/* tnftp */

#include <sys/cdefs.h>
#ifndef lint
#if 0
static char sccsid[] = "@(#)cmdtab.c	8.4 (Berkeley) 10/9/94";
#else
__RCSID(" NetBSD: cmdtab.c,v 1.51 2009/04/12 10:18:52 lukem Exp  ");
#endif
#endif /* not lint */

#include <stdio.h>

#endif	/* tnftp */

#include "ftp_var.h"

/*
 * User FTP -- Command Tables.
 */

#define HSTR	static const char

#ifndef NO_HELP
HSTR	accounthelp[] =	"send account command to remote server";
HSTR	appendhelp[] =	"append to a file";
HSTR	asciihelp[] =	"set ascii transfer type";
HSTR	beephelp[] =	"beep when command completed";
HSTR	binaryhelp[] =	"set binary transfer type";
HSTR	casehelp[] =	"toggle mget upper/lower case id mapping";
HSTR	cdhelp[] =	"change remote working directory";
HSTR	cduphelp[] =	"change remote working directory to parent directory";
HSTR	chmodhelp[] =	"change file permissions of remote file";
HSTR	connecthelp[] =	"connect to remote ftp server";
HSTR	crhelp[] =	"toggle carriage return stripping on ascii gets";
HSTR	debughelp[] =	"toggle/set debugging mode";
HSTR	deletehelp[] =	"delete remote file";
HSTR	disconhelp[] =	"terminate ftp session";
HSTR	domachelp[] =	"execute macro";
HSTR	edithelp[] =	"toggle command line editing";
HSTR	epsvhelp[] =	"toggle use of EPSV/EPRT on both IPv4 and IPV6 ftp";
HSTR	epsv4help[] =	"toggle use of EPSV/EPRT on IPv4 ftp";
HSTR	epsv6help[] =	"toggle use of EPSV/EPRT on IPv6 ftp";
HSTR	feathelp[] =	"show FEATures supported by remote system";
HSTR	formhelp[] =	"set file transfer format";
HSTR	gatehelp[] =	"toggle gate-ftp; specify host[:port] to change proxy";
HSTR	globhelp[] =	"toggle metacharacter expansion of local file names";
HSTR	hashhelp[] =	"toggle printing `#' marks; specify number to set size";
HSTR	helphelp[] =	"print local help information";
HSTR	idlehelp[] =	"get (set) idle timer on remote side";
HSTR	lcdhelp[] =	"change local working directory";
HSTR	lpagehelp[] =	"view a local file through your pager";
HSTR	lpwdhelp[] =	"print local working directory";
HSTR	lshelp[] =	"list contents of remote path";
HSTR	macdefhelp[] =  "define a macro";
HSTR	mdeletehelp[] =	"delete multiple files";
HSTR	mgethelp[] =	"get multiple files";
HSTR	mregethelp[] =	"get multiple files restarting at end of local file";
HSTR	fgethelp[] =	"get files using a localfile as a source of names";
HSTR	mkdirhelp[] =	"make directory on the remote machine";
HSTR	mlshelp[] =	"list contents of multiple remote directories";
HSTR	mlsdhelp[] =	"list contents of remote directory in a machine "
			"parsable form";
HSTR	mlsthelp[] =	"list remote path in a machine parsable form";
HSTR	modehelp[] =	"set file transfer mode";
HSTR	modtimehelp[] = "show last modification time of remote file";
HSTR	mputhelp[] =	"send multiple files";
HSTR	newerhelp[] =	"get file if remote file is newer than local file ";
HSTR	nmaphelp[] =	"set templates for default file name mapping";
HSTR	ntranshelp[] =	"set translation table for default file name mapping";
HSTR	optshelp[] =	"show or set options for remote commands";
HSTR	pagehelp[] =	"view a remote file through your pager";
HSTR	passivehelp[] =	"toggle use of passive transfer mode";
HSTR	plshelp[] =	"list contents of remote path through your pager";
HSTR	pmlsdhelp[] =	"list contents of remote directory in a machine "
			"parsable form through your pager";
HSTR	porthelp[] =	"toggle use of PORT/LPRT cmd for each data connection";
HSTR	preservehelp[] ="toggle preservation of modification time of "
			"retrieved files";
HSTR	progresshelp[] ="toggle transfer progress meter";
HSTR	prompthelp[] =	"force interactive prompting on multiple commands";
HSTR	proxyhelp[] =	"issue command on alternate connection";
HSTR	pwdhelp[] =	"print working directory on remote machine";
HSTR	quithelp[] =	"terminate ftp session and exit";
HSTR	quotehelp[] =	"send arbitrary ftp command";
HSTR	ratehelp[] =	"set transfer rate limit (in bytes/second)";
HSTR	receivehelp[] =	"receive file";
HSTR	regethelp[] =	"get file restarting at end of local file";
HSTR	remotehelp[] =	"get help from remote server";
HSTR	renamehelp[] =	"rename file";
HSTR	resethelp[] =	"clear queued command replies";
HSTR	restarthelp[]=	"restart file transfer at bytecount";
HSTR	rmdirhelp[] =	"remove directory on the remote machine";
HSTR	rmtstatushelp[]="show status of remote machine";
HSTR	runiquehelp[] = "toggle store unique for local files";
HSTR	sendhelp[] =	"send one file";
HSTR	sethelp[] =	"set or display options";
HSTR	shellhelp[] =	"escape to the shell";
HSTR	sitehelp[] =	"send site specific command to remote server\n"
			"\t\tTry \"rhelp site\" or \"site help\" "
			"for more information";
HSTR	sizecmdhelp[] = "show size of remote file";
HSTR	statushelp[] =	"show current status";
HSTR	structhelp[] =	"set file transfer structure";
HSTR	suniquehelp[] = "toggle store unique on remote machine";
HSTR	systemhelp[] =  "show remote system type";
HSTR	tenexhelp[] =	"set tenex file transfer type";
HSTR	tracehelp[] =	"toggle packet tracing";
HSTR	typehelp[] =	"set file transfer type";
HSTR	umaskhelp[] =	"get (set) umask on remote side";
HSTR	unsethelp[] =	"unset an option";
HSTR	usagehelp[] =	"show command usage";
HSTR	userhelp[] =	"send new user information";
HSTR	verbosehelp[] =	"toggle verbose mode";
HSTR	xferbufhelp[] =	"set socket send/receive buffer size";
#endif

HSTR	empty[] = "";

#ifdef NO_HELP
#define H(x)	empty
#else
#define H(x)	x
#endif

#ifdef NO_EDITCOMPLETE
#define	CMPL(x)
#define	CMPL0
#else  /* !NO_EDITCOMPLETE */
#define	CMPL(x)	#x,
#define	CMPL0	empty,
#endif /* !NO_EDITCOMPLETE */

struct cmd cmdtab[] = {
	{ "!",		H(shellhelp),	0, 0, 0, CMPL0		shell },
	{ "$",		H(domachelp),	1, 0, 0, CMPL0		domacro },
	{ "account",	H(accounthelp),	0, 1, 1, CMPL0		account},
	{ "append",	H(appendhelp),	1, 1, 1, CMPL(lr)	put },
	{ "ascii",	H(asciihelp),	0, 1, 1, CMPL0		setascii },
	{ "bell",	H(beephelp),	0, 0, 0, CMPL0		setbell },
	{ "binary",	H(binaryhelp),	0, 1, 1, CMPL0		setbinary },
	{ "bye",	H(quithelp),	0, 0, 0, CMPL0		quit },
	{ "case",	H(casehelp),	0, 0, 1, CMPL0		setcase },
	{ "cd",		H(cdhelp),	0, 1, 1, CMPL(r)	cd },
	{ "cdup",	H(cduphelp),	0, 1, 1, CMPL0		cdup },
	{ "chmod",	H(chmodhelp),	0, 1, 1, CMPL(nr)	do_chmod },
	{ "close",	H(disconhelp),	0, 1, 1, CMPL0		disconnect },
	{ "cr",		H(crhelp),	0, 0, 0, CMPL0		setcr },
	{ "debug",	H(debughelp),	0, 0, 0, CMPL0		setdebug },
	{ "delete",	H(deletehelp),	0, 1, 1, CMPL(r)	delete },
	{ "dir",	H(lshelp),	1, 1, 1, CMPL(rl)	ls },
	{ "disconnect",	H(disconhelp),	0, 1, 1, CMPL0		disconnect },
	{ "edit",	H(edithelp),	0, 0, 0, CMPL0		setedit },
	{ "epsv",	H(epsvhelp),	0, 0, 0, CMPL0		setepsv },
	{ "epsv4",	H(epsv4help),	0, 0, 0, CMPL0		setepsv4 },
	{ "epsv6",	H(epsv6help),	0, 0, 0, CMPL0		setepsv6 },
	{ "exit",	H(quithelp),	0, 0, 0, CMPL0		quit },
	{ "features",	H(feathelp),	0, 1, 1, CMPL0		feat },
	{ "fget",	H(fgethelp),	1, 1, 1, CMPL(l)	fget },
	{ "form",	H(formhelp),	0, 1, 1, CMPL0		setform },
	{ "ftp",	H(connecthelp),	0, 0, 1, CMPL0		setpeer },
	{ "gate",	H(gatehelp),	0, 0, 0, CMPL0		setgate },
	{ "get",	H(receivehelp),	1, 1, 1, CMPL(rl)	get },
	{ "glob",	H(globhelp),	0, 0, 0, CMPL0		setglob },
	{ "hash",	H(hashhelp),	0, 0, 0, CMPL0		sethash },
	{ "help",	H(helphelp),	0, 0, 1, CMPL(C)	help },
	{ "idle",	H(idlehelp),	0, 1, 1, CMPL0		idlecmd },
	{ "image",	H(binaryhelp),	0, 1, 1, CMPL0		setbinary },
	{ "lcd",	H(lcdhelp),	0, 0, 0, CMPL(l)	lcd },
	{ "less",	H(pagehelp),	1, 1, 1, CMPL(r)	page },
	{ "lpage",	H(lpagehelp),	0, 0, 0, CMPL(l)	lpage },
	{ "lpwd",	H(lpwdhelp),	0, 0, 0, CMPL0		lpwd },
	{ "ls",		H(lshelp),	1, 1, 1, CMPL(rl)	ls },
	{ "macdef",	H(macdefhelp),	0, 0, 0, CMPL0		macdef },
	{ "mdelete",	H(mdeletehelp),	1, 1, 1, CMPL(R)	mdelete },
	{ "mdir",	H(mlshelp),	1, 1, 1, CMPL(R)	mls },
	{ "mget",	H(mgethelp),	1, 1, 1, CMPL(R)	mget },
	{ "mkdir",	H(mkdirhelp),	0, 1, 1, CMPL(r)	makedir },
	{ "mls",	H(mlshelp),	1, 1, 1, CMPL(R)	mls },
	{ "mlsd",	H(mlsdhelp),	1, 1, 1, CMPL(r)	ls },
	{ "mlst",	H(mlsthelp),	1, 1, 1, CMPL(r)	mlst },
	{ "mode",	H(modehelp),	0, 1, 1, CMPL0		setftmode },
	{ "modtime",	H(modtimehelp),	0, 1, 1, CMPL(r)	modtime },
	{ "more",	H(pagehelp),	1, 1, 1, CMPL(r)	page },
	{ "mput",	H(mputhelp),	1, 1, 1, CMPL(L)	mput },
	{ "mreget",	H(mregethelp),	1, 1, 1, CMPL(R)	mget },
	{ "msend",	H(mputhelp),	1, 1, 1, CMPL(L)	mput },
	{ "newer",	H(newerhelp),	1, 1, 1, CMPL(r)	newer },
	{ "nlist",	H(lshelp),	1, 1, 1, CMPL(rl)	ls },
	{ "nmap",	H(nmaphelp),	0, 0, 1, CMPL0		setnmap },
	{ "ntrans",	H(ntranshelp),	0, 0, 1, CMPL0		setntrans },
	{ "open",	H(connecthelp),	0, 0, 1, CMPL0		setpeer },
	{ "page",	H(pagehelp),	1, 1, 1, CMPL(r)	page },
	{ "passive",	H(passivehelp),	0, 0, 0, CMPL0		setpassive },
	{ "pdir",	H(plshelp),	1, 1, 1, CMPL(r)	ls },
	{ "pls",	H(plshelp),	1, 1, 1, CMPL(r)	ls },
	{ "pmlsd",	H(pmlsdhelp),	1, 1, 1, CMPL(r)	ls },
	{ "preserve",	H(preservehelp),0, 0, 0, CMPL0		setpreserve },
	{ "progress",	H(progresshelp),0, 0, 0, CMPL0		setprogress },
	{ "prompt",	H(prompthelp),	0, 0, 0, CMPL0		setprompt },
	{ "proxy",	H(proxyhelp),	0, 0, 1, CMPL(c)	doproxy },
	{ "put",	H(sendhelp),	1, 1, 1, CMPL(lr)	put },
	{ "pwd",	H(pwdhelp),	0, 1, 1, CMPL0		pwd },
	{ "quit",	H(quithelp),	0, 0, 0, CMPL0		quit },
	{ "quote",	H(quotehelp),	1, 1, 1, CMPL0		quote },
	{ "rate",	H(ratehelp),	0, 0, 0, CMPL0		setrate },
	{ "rcvbuf",	H(xferbufhelp),	0, 0, 0, CMPL0		setxferbuf },
	{ "recv",	H(receivehelp),	1, 1, 1, CMPL(rl)	get },
	{ "reget",	H(regethelp),	1, 1, 1, CMPL(rl)	reget },
	{ "remopts",	H(optshelp),	0, 1, 1, CMPL0		opts },
	{ "rename",	H(renamehelp),	0, 1, 1, CMPL(rr)	renamefile },
	{ "reset",	H(resethelp),	0, 1, 1, CMPL0		reset },
	{ "restart",	H(restarthelp),	1, 1, 1, CMPL0		restart },
	{ "rhelp",	H(remotehelp),	0, 1, 1, CMPL0		rmthelp },
	{ "rmdir",	H(rmdirhelp),	0, 1, 1, CMPL(r)	removedir },
	{ "rstatus",	H(rmtstatushelp),0, 1, 1, CMPL(r)	rmtstatus },
	{ "runique",	H(runiquehelp),	0, 0, 1, CMPL0		setrunique },
	{ "send",	H(sendhelp),	1, 1, 1, CMPL(lr)	put },
	{ "sendport",	H(porthelp),	0, 0, 0, CMPL0		setport },
	{ "set",	H(sethelp),	0, 0, 0, CMPL(o)	setoption },
	{ "site",	H(sitehelp),	0, 1, 1, CMPL0		site },
	{ "size",	H(sizecmdhelp),	1, 1, 1, CMPL(r)	sizecmd },
	{ "sndbuf",	H(xferbufhelp),	0, 0, 0, CMPL0		setxferbuf },
	{ "status",	H(statushelp),	0, 0, 1, CMPL0		status },
	{ "struct",	H(structhelp),	0, 1, 1, CMPL0		setstruct },
	{ "sunique",	H(suniquehelp),	0, 0, 1, CMPL0		setsunique },
	{ "system",	H(systemhelp),	0, 1, 1, CMPL0		syst },
	{ "tenex",	H(tenexhelp),	0, 1, 1, CMPL0		settenex },
	{ "throttle",	H(ratehelp),	0, 0, 0, CMPL0		setrate },
	{ "trace",	H(tracehelp),	0, 0, 0, CMPL0		settrace },
	{ "type",	H(typehelp),	0, 1, 1, CMPL0		settype },
	{ "umask",	H(umaskhelp),	0, 1, 1, CMPL0		do_umask },
	{ "unset",	H(unsethelp),	0, 0, 0, CMPL(o)	unsetoption },
	{ "usage",	H(usagehelp),	0, 0, 1, CMPL(C)	help },
	{ "user",	H(userhelp),	0, 1, 1, CMPL0		user },
	{ "verbose",	H(verbosehelp),	0, 0, 0, CMPL0		setverbose },
	{ "xferbuf",	H(xferbufhelp),	0, 0, 0, CMPL0		setxferbuf },
	{ "?",		H(helphelp),	0, 0, 1, CMPL(C)	help },
	{ NULL,		NULL,		0, 0, 0, CMPL0		NULL },
};

struct option optiontab[] = {
	{ "anonpass",	NULL },
	{ "ftp_proxy",	NULL },
	{ "http_proxy",	NULL },
	{ "no_proxy",	NULL },
	{ "pager",	NULL },
	{ "prompt",	NULL },
	{ "rprompt",	NULL },
	{ NULL,		NULL },
};
