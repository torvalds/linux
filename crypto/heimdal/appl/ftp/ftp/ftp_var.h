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
 *
 *	@(#)ftp_var.h	8.4 (Berkeley) 10/9/94
 */

/*
 * FTP global variables.
 */

#ifdef HAVE_SYS_PARAM_H
#include <sys/param.h>
#endif
#include <setjmp.h>

/*
 * Options and other state info.
 */
extern int	trace;			/* trace packets exchanged */
extern int	hash;			/* print # for each buffer transferred */
extern int	sendport;		/* use PORT cmd for each data connection */
extern int	verbose;		/* print messages coming back from server */
extern int	connected;		/* connected to server */
extern int	fromatty;		/* input is from a terminal */
extern int	interactive;		/* interactively prompt on m* cmds */
extern int	lineedit;		/* use line-editing */
extern int	debug;			/* debugging level */
extern int	bell;			/* ring bell on cmd completion */
extern int	doglob;			/* glob local file names */
extern int	autologin;		/* establish user account on connection */
extern int	doencrypt;
extern int	proxy;			/* proxy server connection active */
extern int	proxflag;		/* proxy connection exists */
extern int	sunique;		/* store files on server with unique name */
extern int	runique;		/* store local files with unique name */
extern int	mcase;			/* map upper to lower case for mget names */
extern int	ntflag;			/* use ntin ntout tables for name translation */
extern int	mapflag;		/* use mapin mapout templates on file names */
extern int	code;			/* return/reply code for ftp command */
extern int	crflag;			/* if 1, strip car. rets. on ascii gets */
extern char	pasv[64];		/* passive port for proxy data connection */
extern int	passivemode;		/* passive mode enabled */
extern char	*altarg;		/* argv[1] with no shell-like preprocessing  */
extern char	ntin[17];		/* input translation table */
extern char	ntout[17];		/* output translation table */
extern char	mapin[MaxPathLen];	/* input map template */
extern char	mapout[MaxPathLen];	/* output map template */
extern char	typename[32];		/* name of file transfer type */
extern int	type;			/* requested file transfer type */
extern int	curtype;		/* current file transfer type */
extern char	structname[32];		/* name of file transfer structure */
extern int	stru;			/* file transfer structure */
extern char	formname[32];		/* name of file transfer format */
extern int	form;			/* file transfer format */
extern char	modename[32];		/* name of file transfer mode */
extern int	mode;			/* file transfer mode */
extern char	bytename[32];		/* local byte size in ascii */
extern int	bytesize;		/* local byte size in binary */

extern char	*hostname;		/* name of host connected to */
extern int	unix_server;		/* server is unix, can use binary for ascii */
extern int	unix_proxy;		/* proxy is unix, can use binary for ascii */

extern jmp_buf	toplevel;		/* non-local goto stuff for cmd scanner */

extern char	line[200];		/* input line buffer */
extern char	*stringbase;		/* current scan point in line buffer */
extern char	argbuf[200];		/* argument storage buffer */
extern char	*argbase;		/* current storage point in arg buffer */
extern int	margc;			/* count of arguments on input line */
extern char	**margv;		/* args parsed from input line */
extern int	margvlen;		/* how large margv is currently */
extern int     cpend;                  /* flag: if != 0, then pending server reply */
extern int	mflag;			/* flag: if != 0, then active multi command */

extern int	options;		/* used during socket creation */
extern int      use_kerberos;           /* use Kerberos authentication */

/*
 * Format of command table.
 */
struct cmd {
	char	*c_name;	/* name of command */
	char	*c_help;	/* help string */
	char	c_bell;		/* give bell when command completes */
	char	c_conn;		/* must be connected to use command */
	char	c_proxy;	/* proxy server may execute */
	void	(*c_handler) (int, char **); /* function to call */
};

struct macel {
	char mac_name[9];	/* macro name */
	char *mac_start;	/* start of macro in macbuf */
	char *mac_end;		/* end of macro in macbuf */
};

extern int macnum;			/* number of defined macros */
extern struct macel macros[16];
extern char macbuf[4096];


