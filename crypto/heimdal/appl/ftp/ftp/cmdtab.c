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
 */

#include "ftp_locl.h"

/*
 * User FTP -- Command Tables.
 */

char	accounthelp[] =	"send account command to remote server";
char	appendhelp[] =	"append to a file";
char	asciihelp[] =	"set ascii transfer type";
char	beephelp[] =	"beep when command completed";
char	binaryhelp[] =	"set binary transfer type";
char	casehelp[] =	"toggle mget upper/lower case id mapping";
char	cdhelp[] =	"change remote working directory";
char	cduphelp[] = 	"change remote working directory to parent directory";
char	chmodhelp[] =	"change file permissions of remote file";
char	connecthelp[] =	"connect to remote tftp";
char	crhelp[] =	"toggle carriage return stripping on ascii gets";
char	deletehelp[] =	"delete remote file";
char	debughelp[] =	"toggle/set debugging mode";
char	dirhelp[] =	"list contents of remote directory";
char	disconhelp[] =	"terminate ftp session";
char	domachelp[] = 	"execute macro";
char	formhelp[] =	"set file transfer format";
char	globhelp[] =	"toggle metacharacter expansion of local file names";
char	hashhelp[] =	"toggle printing `#' for each buffer transferred";
char	helphelp[] =	"print local help information";
char	idlehelp[] =	"get (set) idle timer on remote side";
char	lcdhelp[] =	"change local working directory";
char	lshelp[] =	"list contents of remote directory";
char	macdefhelp[] =  "define a macro";
char	mdeletehelp[] =	"delete multiple files";
char	mdirhelp[] =	"list contents of multiple remote directories";
char	mgethelp[] =	"get multiple files";
char	mkdirhelp[] =	"make directory on the remote machine";
char	mlshelp[] =	"list contents of multiple remote directories";
char	modtimehelp[] = "show last modification time of remote file";
char	modehelp[] =	"set file transfer mode";
char	mputhelp[] =	"send multiple files";
char	newerhelp[] =	"get file if remote file is newer than local file ";
char	nlisthelp[] =	"nlist contents of remote directory";
char	nmaphelp[] =	"set templates for default file name mapping";
char	ntranshelp[] =	"set translation table for default file name mapping";
char	porthelp[] =	"toggle use of PORT cmd for each data connection";
char	prompthelp[] =	"force interactive prompting on multiple commands";
char	proxyhelp[] =	"issue command on alternate connection";
char	pwdhelp[] =	"print working directory on remote machine";
char	quithelp[] =	"terminate ftp session and exit";
char	quotehelp[] =	"send arbitrary ftp command";
char	receivehelp[] =	"receive file";
char	regethelp[] =	"get file restarting at end of local file";
char	remotehelp[] =	"get help from remote server";
char	renamehelp[] =	"rename file";
char	restarthelp[]=	"restart file transfer at bytecount";
char	rmdirhelp[] =	"remove directory on the remote machine";
char	rmtstatushelp[]="show status of remote machine";
char	runiquehelp[] = "toggle store unique for local files";
char	resethelp[] =	"clear queued command replies";
char	sendhelp[] =	"send one file";
char	passivehelp[] =	"enter passive transfer mode";
char	sitehelp[] =	"send site specific command to remote server\n\t\tTry \"rhelp site\" or \"site help\" for more information";
char	shellhelp[] =	"escape to the shell";
char	sizecmdhelp[] = "show size of remote file";
char	statushelp[] =	"show current status";
char	structhelp[] =	"set file transfer structure";
char	suniquehelp[] = "toggle store unique on remote machine";
char	systemhelp[] =  "show remote system type";
char	tenexhelp[] =	"set tenex file transfer type";
char	tracehelp[] =	"toggle packet tracing";
char	typehelp[] =	"set file transfer type";
char	umaskhelp[] =	"get (set) umask on remote side";
char	userhelp[] =	"send new user information";
char	verbosehelp[] =	"toggle verbose mode";

char	prothelp[] = 	"set protection level";
char	prothelp_c[] =	"set command protection level";
#if defined(KRB5)
char	klisthelp[] =	"show remote tickets";
#endif
#if defined(KRB5)
char	afsloghelp[] = 	"obtain remote AFS tokens";
#endif

struct cmd cmdtab[] = {
	{ "!",		shellhelp,	0,	0,	0,	shell },
	{ "$",		domachelp,	1,	0,	0,	domacro },
	{ "account",	accounthelp,	0,	1,	1,	account},
	{ "append",	appendhelp,	1,	1,	1,	put },
	{ "ascii",	asciihelp,	0,	1,	1,	setascii },
	{ "bell",	beephelp,	0,	0,	0,	setbell },
	{ "binary",	binaryhelp,	0,	1,	1,	setbinary },
	{ "bye",	quithelp,	0,	0,	0,	quit },
	{ "case",	casehelp,	0,	0,	1,	setcase },
	{ "cd",		cdhelp,		0,	1,	1,	cd },
	{ "cdup",	cduphelp,	0,	1,	1,	cdup },
	{ "chmod",	chmodhelp,	0,	1,	1,	do_chmod },
	{ "close",	disconhelp,	0,	1,	1,	disconnect },
	{ "cr",		crhelp,		0,	0,	0,	setcr },
	{ "delete",	deletehelp,	0,	1,	1,	delete },
	{ "debug",	debughelp,	0,	0,	0,	setdebug },
	{ "dir",	dirhelp,	1,	1,	1,	ls },
	{ "disconnect",	disconhelp,	0,	1,	1,	disconnect },
	{ "form",	formhelp,	0,	1,	1,	setform },
	{ "get",	receivehelp,	1,	1,	1,	get },
	{ "glob",	globhelp,	0,	0,	0,	setglob },
	{ "hash",	hashhelp,	0,	0,	0,	sethash },
	{ "help",	helphelp,	0,	0,	1,	help },
	{ "idle",	idlehelp,	0,	1,	1,	ftp_idle },
	{ "image",	binaryhelp,	0,	1,	1,	setbinary },
	{ "lcd",	lcdhelp,	0,	0,	0,	lcd },
	{ "ls",		lshelp,		1,	1,	1,	ls },
	{ "macdef",	macdefhelp,	0,	0,	0,	macdef },
	{ "mdelete",	mdeletehelp,	1,	1,	1,	mdelete },
	{ "mdir",	mdirhelp,	1,	1,	1,	mls },
	{ "mget",	mgethelp,	1,	1,	1,	mget },
	{ "mkdir",	mkdirhelp,	0,	1,	1,	makedir },
	{ "mls",	mlshelp,	1,	1,	1,	mls },
	{ "mode",	modehelp,	0,	1,	1,	setftmode },
	{ "modtime",	modtimehelp,	0,	1,	1,	modtime },
	{ "mput",	mputhelp,	1,	1,	1,	mput },
	{ "newer",	newerhelp,	1,	1,	1,	newer },
	{ "nmap",	nmaphelp,	0,	0,	1,	setnmap },
	{ "nlist",	nlisthelp,	1,	1,	1,	ls },
	{ "ntrans",	ntranshelp,	0,	0,	1,	setntrans },
	{ "open",	connecthelp,	0,	0,	1,	setpeer },
	{ "passive",	passivehelp,	0,	0,	0,	setpassive },
	{ "prompt",	prompthelp,	0,	0,	0,	setprompt },
	{ "proxy",	proxyhelp,	0,	0,	1,	doproxy },
	{ "sendport",	porthelp,	0,	0,	0,	setport },
	{ "put",	sendhelp,	1,	1,	1,	put },
	{ "pwd",	pwdhelp,	0,	1,	1,	pwd },
	{ "quit",	quithelp,	0,	0,	0,	quit },
	{ "quote",	quotehelp,	1,	1,	1,	quote },
	{ "recv",	receivehelp,	1,	1,	1,	get },
	{ "reget",	regethelp,	1,	1,	1,	reget },
	{ "rstatus",	rmtstatushelp,	0,	1,	1,	rmtstatus },
	{ "rhelp",	remotehelp,	0,	1,	1,	rmthelp },
	{ "rename",	renamehelp,	0,	1,	1,	renamefile },
	{ "reset",	resethelp,	0,	1,	1,	reset },
	{ "restart",	restarthelp,	1,	1,	1,	restart },
	{ "rmdir",	rmdirhelp,	0,	1,	1,	removedir },
	{ "runique",	runiquehelp,	0,	0,	1,	setrunique },
	{ "send",	sendhelp,	1,	1,	1,	put },
	{ "site",	sitehelp,	0,	1,	1,	site },
	{ "size",	sizecmdhelp,	1,	1,	1,	sizecmd },
	{ "status",	statushelp,	0,	0,	1,	status },
	{ "struct",	structhelp,	0,	1,	1,	setstruct },
	{ "system",	systemhelp,	0,	1,	1,	syst },
	{ "sunique",	suniquehelp,	0,	0,	1,	setsunique },
	{ "tenex",	tenexhelp,	0,	1,	1,	settenex },
	{ "trace",	tracehelp,	0,	0,	0,	settrace },
	{ "type",	typehelp,	0,	1,	1,	settype },
	{ "user",	userhelp,	0,	1,	1,	user },
	{ "umask",	umaskhelp,	0,	1,	1,	do_umask },
	{ "verbose",	verbosehelp,	0,	0,	0,	setverbose },
	{ "?",		helphelp,	0,	0,	1,	help },

	{ "protect", 	prothelp, 	0, 	1, 	0,	sec_prot },
	/* what MIT uses */
	{ "cprotect",	prothelp_c,	0,	1,	1,	sec_prot_command },
#if defined(KRB5)
	{ "klist", 	klisthelp, 	0, 	1, 	0,	klist },
#endif
#if defined(KRB5)
	{ "afslog",	afsloghelp,	0,	1,	0,	afslog },
#endif

	{ 0 },
};

int	NCMDS = (sizeof (cmdtab) / sizeof (cmdtab[0])) - 1;
