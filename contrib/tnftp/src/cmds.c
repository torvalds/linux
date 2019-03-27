/*	$NetBSD: cmds.c,v 1.17 2010/01/12 06:55:47 lukem Exp $	*/
/*	from	NetBSD: cmds.c,v 1.130 2009/07/13 19:05:41 roy Exp	*/

/*-
 * Copyright (c) 1996-2009 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Luke Mewburn.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jason R. Thorpe of the Numerical Aerospace Simulation Facility,
 * NASA Ames Research Center.
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

#include "tnftp.h"

#if 0	/* tnftp */

#include <sys/cdefs.h>
#ifndef lint
#if 0
static char sccsid[] = "@(#)cmds.c	8.6 (Berkeley) 10/9/94";
#else
__RCSID(" NetBSD: cmds.c,v 1.130 2009/07/13 19:05:41 roy Exp  ");
#endif
#endif /* not lint */

/*
 * FTP User Program -- Command Routines.
 */
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <arpa/ftp.h>

#include <ctype.h>
#include <err.h>
#include <glob.h>
#include <limits.h>
#include <netdb.h>
#include <paths.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#endif	/* tnftp */

#include "ftp_var.h"
#include "version.h"

static struct types {
	const char	*t_name;
	const char	*t_mode;
	int		t_type;
	const char	*t_arg;
} types[] = {
	{ "ascii",	"A",	TYPE_A,	0 },
	{ "binary",	"I",	TYPE_I,	0 },
	{ "image",	"I",	TYPE_I,	0 },
	{ "ebcdic",	"E",	TYPE_E,	0 },
	{ "tenex",	"L",	TYPE_L,	bytename },
	{ NULL,		NULL,	0, NULL }
};

static sigjmp_buf	 jabort;

static int	confirm(const char *, const char *);
static void	mintr(int);
static void	mabort(const char *);
static void	set_type(const char *);

static const char *doprocess(char *, size_t, const char *, int, int, int);
static const char *domap(char *, size_t, const char *);
static const char *docase(char *, size_t, const char *);
static const char *dotrans(char *, size_t, const char *);

/*
 * Confirm if "cmd" is to be performed upon "file".
 * If "file" is NULL, generate a "Continue with" prompt instead.
 */
static int
confirm(const char *cmd, const char *file)
{
	const char *errormsg;
	char cline[BUFSIZ];
	const char *promptleft, *promptright;

	if (!interactive || confirmrest)
		return (1);
	if (file == NULL) {
		promptleft = "Continue with";
		promptright = cmd;
	} else {
		promptleft = cmd;
		promptright = file;
	}
	while (1) {
		fprintf(ttyout, "%s %s [anpqy?]? ", promptleft, promptright);
		(void)fflush(ttyout);
		if (get_line(stdin, cline, sizeof(cline), &errormsg) < 0) {
			mflag = 0;
			fprintf(ttyout, "%s; %s aborted\n", errormsg, cmd);
			return (0);
		}
		switch (tolower((unsigned char)*cline)) {
			case 'a':
				confirmrest = 1;
				fprintf(ttyout,
				    "Prompting off for duration of %s.\n", cmd);
				break;
			case 'p':
				interactive = 0;
				fputs("Interactive mode: off.\n", ttyout);
				break;
			case 'q':
				mflag = 0;
				fprintf(ttyout, "%s aborted.\n", cmd);
				/* FALLTHROUGH */
			case 'n':
				return (0);
			case '?':
				fprintf(ttyout,
				    "  confirmation options:\n"
				    "\ta  answer `yes' for the duration of %s\n"
				    "\tn  answer `no' for this file\n"
				    "\tp  turn off `prompt' mode\n"
				    "\tq  stop the current %s\n"
				    "\ty  answer `yes' for this file\n"
				    "\t?  this help list\n",
				    cmd, cmd);
				continue;	/* back to while(1) */
		}
		return (1);
	}
	/* NOTREACHED */
}

/*
 * Set transfer type.
 */
void
settype(int argc, char *argv[])
{
	struct types *p;

	if (argc == 0 || argc > 2) {
		const char *sep;

		UPRINTF("usage: %s [", argv[0]);
		sep = " ";
		for (p = types; p->t_name; p++) {
			fprintf(ttyout, "%s%s", sep, p->t_name);
			sep = " | ";
		}
		fputs(" ]\n", ttyout);
		code = -1;
		return;
	}
	if (argc < 2) {
		fprintf(ttyout, "Using %s mode to transfer files.\n", typename);
		code = 0;
		return;
	}
	set_type(argv[1]);
}

void
set_type(const char *ttype)
{
	struct types *p;
	int comret;

	for (p = types; p->t_name; p++)
		if (strcmp(ttype, p->t_name) == 0)
			break;
	if (p->t_name == 0) {
		fprintf(ttyout, "%s: unknown mode.\n", ttype);
		code = -1;
		return;
	}
	if ((p->t_arg != NULL) && (*(p->t_arg) != '\0'))
		comret = command("TYPE %s %s", p->t_mode, p->t_arg);
	else
		comret = command("TYPE %s", p->t_mode);
	if (comret == COMPLETE) {
		(void)strlcpy(typename, p->t_name, sizeof(typename));
		curtype = type = p->t_type;
	}
}

/*
 * Internal form of settype; changes current type in use with server
 * without changing our notion of the type for data transfers.
 * Used to change to and from ascii for listings.
 */
void
changetype(int newtype, int show)
{
	struct types *p;
	int comret, oldverbose = verbose;

	if (newtype == 0)
		newtype = TYPE_I;
	if (newtype == curtype)
		return;
	if (ftp_debug == 0 && show == 0)
		verbose = 0;
	for (p = types; p->t_name; p++)
		if (newtype == p->t_type)
			break;
	if (p->t_name == 0) {
		errx(1, "changetype: unknown type %d", newtype);
	}
	if (newtype == TYPE_L && bytename[0] != '\0')
		comret = command("TYPE %s %s", p->t_mode, bytename);
	else
		comret = command("TYPE %s", p->t_mode);
	if (comret == COMPLETE)
		curtype = newtype;
	verbose = oldverbose;
}

/*
 * Set binary transfer type.
 */
/*VARARGS*/
void
setbinary(int argc, char *argv[])
{

	if (argc == 0) {
		UPRINTF("usage: %s\n", argv[0]);
		code = -1;
		return;
	}
	set_type("binary");
}

/*
 * Set ascii transfer type.
 */
/*VARARGS*/
void
setascii(int argc, char *argv[])
{

	if (argc == 0) {
		UPRINTF("usage: %s\n", argv[0]);
		code = -1;
		return;
	}
	set_type("ascii");
}

/*
 * Set tenex transfer type.
 */
/*VARARGS*/
void
settenex(int argc, char *argv[])
{

	if (argc == 0) {
		UPRINTF("usage: %s\n", argv[0]);
		code = -1;
		return;
	}
	set_type("tenex");
}

/*
 * Set file transfer mode.
 */
/*ARGSUSED*/
void
setftmode(int argc, char *argv[])
{

	if (argc != 2) {
		UPRINTF("usage: %s mode-name\n", argv[0]);
		code = -1;
		return;
	}
	fprintf(ttyout, "We only support %s mode, sorry.\n", modename);
	code = -1;
}

/*
 * Set file transfer format.
 */
/*ARGSUSED*/
void
setform(int argc, char *argv[])
{

	if (argc != 2) {
		UPRINTF("usage: %s format\n", argv[0]);
		code = -1;
		return;
	}
	fprintf(ttyout, "We only support %s format, sorry.\n", formname);
	code = -1;
}

/*
 * Set file transfer structure.
 */
/*ARGSUSED*/
void
setstruct(int argc, char *argv[])
{

	if (argc != 2) {
		UPRINTF("usage: %s struct-mode\n", argv[0]);
		code = -1;
		return;
	}
	fprintf(ttyout, "We only support %s structure, sorry.\n", structname);
	code = -1;
}

/*
 * Send a single file.
 */
void
put(int argc, char *argv[])
{
	char buf[MAXPATHLEN];
	const char *cmd;
	int loc = 0;
	char *locfile;
	const char *remfile;

	if (argc == 2) {
		argc++;
		argv[2] = argv[1];
		loc++;
	}
	if (argc == 0 || (argc == 1 && !another(&argc, &argv, "local-file")))
		goto usage;
	if ((argc < 3 && !another(&argc, &argv, "remote-file")) || argc > 3) {
 usage:
		UPRINTF("usage: %s local-file [remote-file]\n", argv[0]);
		code = -1;
		return;
	}
	if ((locfile = globulize(argv[1])) == NULL) {
		code = -1;
		return;
	}
	remfile = argv[2];
	if (loc)	/* If argv[2] is a copy of the old argv[1], update it */
		remfile = locfile;
	cmd = (argv[0][0] == 'a') ? "APPE" : ((sunique) ? "STOU" : "STOR");
	remfile = doprocess(buf, sizeof(buf), remfile,
		0, loc && ntflag, loc && mapflag);
	sendrequest(cmd, locfile, remfile,
	    locfile != argv[1] || remfile != argv[2]);
	free(locfile);
}

static const char *
doprocess(char *dst, size_t dlen, const char *src,
    int casef, int transf, int mapf)
{
	if (casef)
		src = docase(dst, dlen, src);
	if (transf)
		src = dotrans(dst, dlen, src);
	if (mapf)
		src = domap(dst, dlen, src);
	return src;
}

/*
 * Send multiple files.
 */
void
mput(int argc, char *argv[])
{
	int i;
	sigfunc oldintr;
	int ointer;
	const char *tp;

	if (argc == 0 || (argc == 1 && !another(&argc, &argv, "local-files"))) {
		UPRINTF("usage: %s local-files\n", argv[0]);
		code = -1;
		return;
	}
	mflag = 1;
	oldintr = xsignal(SIGINT, mintr);
	if (sigsetjmp(jabort, 1))
		mabort(argv[0]);
	if (proxy) {
		char *cp;

		while ((cp = remglob(argv, 0, NULL)) != NULL) {
			if (*cp == '\0' || !connected) {
				mflag = 0;
				continue;
			}
			if (mflag && confirm(argv[0], cp)) {
				char buf[MAXPATHLEN];
				tp = doprocess(buf, sizeof(buf), cp,
				    mcase, ntflag, mapflag);
				sendrequest((sunique) ? "STOU" : "STOR",
				    cp, tp, cp != tp || !interactive);
				if (!mflag && fromatty) {
					ointer = interactive;
					interactive = 1;
					if (confirm(argv[0], NULL)) {
						mflag++;
					}
					interactive = ointer;
				}
			}
		}
		goto cleanupmput;
	}
	for (i = 1; i < argc && connected; i++) {
		char **cpp;
		glob_t gl;
		int flags;

		if (!doglob) {
			if (mflag && confirm(argv[0], argv[i])) {
				char buf[MAXPATHLEN];
				tp = doprocess(buf, sizeof(buf), argv[i],
					0, ntflag, mapflag);
				sendrequest((sunique) ? "STOU" : "STOR",
				    argv[i], tp, tp != argv[i] || !interactive);
				if (!mflag && fromatty) {
					ointer = interactive;
					interactive = 1;
					if (confirm(argv[0], NULL)) {
						mflag++;
					}
					interactive = ointer;
				}
			}
			continue;
		}

		memset(&gl, 0, sizeof(gl));
		flags = GLOB_BRACE|GLOB_NOCHECK|GLOB_TILDE;
		if (glob(argv[i], flags, NULL, &gl) || gl.gl_pathc == 0) {
			warnx("Glob pattern `%s' not found", argv[i]);
			globfree(&gl);
			continue;
		}
		for (cpp = gl.gl_pathv; cpp && *cpp != NULL && connected;
		    cpp++) {
			if (mflag && confirm(argv[0], *cpp)) {
				char buf[MAXPATHLEN];
				tp = *cpp;
				tp = doprocess(buf, sizeof(buf), *cpp,
				    0, ntflag, mapflag);
				sendrequest((sunique) ? "STOU" : "STOR",
				    *cpp, tp, *cpp != tp || !interactive);
				if (!mflag && fromatty) {
					ointer = interactive;
					interactive = 1;
					if (confirm(argv[0], NULL)) {
						mflag++;
					}
					interactive = ointer;
				}
			}
		}
		globfree(&gl);
	}
 cleanupmput:
	(void)xsignal(SIGINT, oldintr);
	mflag = 0;
}

void
reget(int argc, char *argv[])
{

	(void)getit(argc, argv, 1, "r+");
}

void
get(int argc, char *argv[])
{

	(void)getit(argc, argv, 0, restart_point ? "r+" : "w" );
}

/*
 * Receive one file.
 * If restartit is  1, restart the xfer always.
 * If restartit is -1, restart the xfer only if the remote file is newer.
 */
int
getit(int argc, char *argv[], int restartit, const char *gmode)
{
	int	loc, rval;
	char	*remfile, *olocfile;
	const char *locfile;
	char	buf[MAXPATHLEN];

	loc = rval = 0;
	if (argc == 2) {
		argc++;
		argv[2] = argv[1];
		loc++;
	}
	if (argc == 0 || (argc == 1 && !another(&argc, &argv, "remote-file")))
		goto usage;
	if ((argc < 3 && !another(&argc, &argv, "local-file")) || argc > 3) {
 usage:
		UPRINTF("usage: %s remote-file [local-file]\n", argv[0]);
		code = -1;
		return (0);
	}
	remfile = argv[1];
	if ((olocfile = globulize(argv[2])) == NULL) {
		code = -1;
		return (0);
	}
	locfile = doprocess(buf, sizeof(buf), olocfile,
		loc && mcase, loc && ntflag, loc && mapflag);
	if (restartit) {
		struct stat stbuf;
		int ret;

		if (! features[FEAT_REST_STREAM]) {
			fprintf(ttyout,
			    "Restart is not supported by the remote server.\n");
			return (0);
		}
		ret = stat(locfile, &stbuf);
		if (restartit == 1) {
			if (ret < 0) {
				warn("Can't stat `%s'", locfile);
				goto freegetit;
			}
			restart_point = stbuf.st_size;
		} else {
			if (ret == 0) {
				time_t mtime;

				mtime = remotemodtime(argv[1], 0);
				if (mtime == -1)
					goto freegetit;
				if (stbuf.st_mtime >= mtime) {
					rval = 1;
					goto freegetit;
				}
			}
		}
	}

	recvrequest("RETR", locfile, remfile, gmode,
	    remfile != argv[1] || locfile != argv[2], loc);
	restart_point = 0;
 freegetit:
	(void)free(olocfile);
	return (rval);
}

/* ARGSUSED */
static void
mintr(int signo)
{

	alarmtimer(0);
	if (fromatty)
		write(fileno(ttyout), "\n", 1);
	siglongjmp(jabort, 1);
}

static void
mabort(const char *cmd)
{
	int ointer, oconf;

	if (mflag && fromatty) {
		ointer = interactive;
		oconf = confirmrest;
		interactive = 1;
		confirmrest = 0;
		if (confirm(cmd, NULL)) {
			interactive = ointer;
			confirmrest = oconf;
			return;
		}
		interactive = ointer;
		confirmrest = oconf;
	}
	mflag = 0;
}

/*
 * Get multiple files.
 */
void
mget(int argc, char *argv[])
{
	sigfunc oldintr;
	int ointer;
	char *cp;
	const char *tp;
	int volatile restartit;

	if (argc == 0 ||
	    (argc == 1 && !another(&argc, &argv, "remote-files"))) {
		UPRINTF("usage: %s remote-files\n", argv[0]);
		code = -1;
		return;
	}
	mflag = 1;
	restart_point = 0;
	restartit = 0;
	if (strcmp(argv[0], "mreget") == 0) {
		if (! features[FEAT_REST_STREAM]) {
			fprintf(ttyout,
		    "Restart is not supported by the remote server.\n");
			return;
		}
		restartit = 1;
	}
	oldintr = xsignal(SIGINT, mintr);
	if (sigsetjmp(jabort, 1))
		mabort(argv[0]);
	while ((cp = remglob(argv, proxy, NULL)) != NULL) {
		char buf[MAXPATHLEN];
		if (*cp == '\0' || !connected) {
			mflag = 0;
			continue;
		}
		if (! mflag)
			continue;
		if (! fileindir(cp, localcwd)) {
			fprintf(ttyout, "Skipping non-relative filename `%s'\n",
			    cp);
			continue;
		}
		if (!confirm(argv[0], cp))
			continue;
		tp = doprocess(buf, sizeof(buf), cp, mcase, ntflag, mapflag);
		if (restartit) {
			struct stat stbuf;

			if (stat(tp, &stbuf) == 0)
				restart_point = stbuf.st_size;
			else
				warn("Can't stat `%s'", tp);
		}
		recvrequest("RETR", tp, cp, restart_point ? "r+" : "w",
		    tp != cp || !interactive, 1);
		restart_point = 0;
		if (!mflag && fromatty) {
			ointer = interactive;
			interactive = 1;
			if (confirm(argv[0], NULL))
				mflag++;
			interactive = ointer;
		}
	}
	(void)xsignal(SIGINT, oldintr);
	mflag = 0;
}

/*
 * Read list of filenames from a local file and get those
 */
void
fget(int argc, char *argv[])
{
	const char *gmode;
	FILE	*fp;
	char	buf[MAXPATHLEN], cmdbuf[MAX_C_NAME];

	if (argc != 2) {
		UPRINTF("usage: %s localfile\n", argv[0]);
		code = -1;
		return;
	}

	fp = fopen(argv[1], "r");
	if (fp == NULL) {
		fprintf(ttyout, "Can't open source file %s\n", argv[1]);
		code = -1;
		return;
	}

	(void)strlcpy(cmdbuf, "get", sizeof(cmdbuf));
	argv[0] = cmdbuf;
	gmode = restart_point ? "r+" : "w";

	while (get_line(fp, buf, sizeof(buf), NULL) >= 0) {
		if (buf[0] == '\0')
			continue;
		argv[1] = buf;
		(void)getit(argc, argv, 0, gmode);
	}
	fclose(fp);
}

const char *
onoff(int val)
{

	return (val ? "on" : "off");
}

/*
 * Show status.
 */
/*ARGSUSED*/
void
status(int argc, char *argv[])
{

	if (argc == 0) {
		UPRINTF("usage: %s\n", argv[0]);
		code = -1;
		return;
	}
#ifndef NO_STATUS
	if (connected)
		fprintf(ttyout, "Connected %sto %s.\n",
		    connected == -1 ? "and logged in" : "", hostname);
	else
		fputs("Not connected.\n", ttyout);
	if (!proxy) {
		pswitch(1);
		if (connected) {
			fprintf(ttyout, "Connected for proxy commands to %s.\n",
			    hostname);
		}
		else {
			fputs("No proxy connection.\n", ttyout);
		}
		pswitch(0);
	}
	fprintf(ttyout, "Gate ftp: %s, server %s, port %s.\n", onoff(gatemode),
	    *gateserver ? gateserver : "(none)", gateport);
	fprintf(ttyout, "Passive mode: %s; fallback to active mode: %s.\n",
	    onoff(passivemode), onoff(activefallback));
	fprintf(ttyout, "Mode: %s; Type: %s; Form: %s; Structure: %s.\n",
	    modename, typename, formname, structname);
	fprintf(ttyout, "Verbose: %s; Bell: %s; Prompting: %s; Globbing: %s.\n",
	    onoff(verbose), onoff(bell), onoff(interactive), onoff(doglob));
	fprintf(ttyout, "Store unique: %s; Receive unique: %s.\n",
	    onoff(sunique), onoff(runique));
	fprintf(ttyout, "Preserve modification times: %s.\n", onoff(preserve));
	fprintf(ttyout, "Case: %s; CR stripping: %s.\n", onoff(mcase),
	    onoff(crflag));
	if (ntflag) {
		fprintf(ttyout, "Ntrans: (in) %s (out) %s\n", ntin, ntout);
	}
	else {
		fputs("Ntrans: off.\n", ttyout);
	}
	if (mapflag) {
		fprintf(ttyout, "Nmap: (in) %s (out) %s\n", mapin, mapout);
	}
	else {
		fputs("Nmap: off.\n", ttyout);
	}
	fprintf(ttyout,
	    "Hash mark printing: %s; Mark count: %d; Progress bar: %s.\n",
	    onoff(hash), mark, onoff(progress));
	fprintf(ttyout,
	    "Get transfer rate throttle: %s; maximum: %d; increment %d.\n",
	    onoff(rate_get), rate_get, rate_get_incr);
	fprintf(ttyout,
	    "Put transfer rate throttle: %s; maximum: %d; increment %d.\n",
	    onoff(rate_put), rate_put, rate_put_incr);
	fprintf(ttyout,
	    "Socket buffer sizes: send %d, receive %d.\n",
	    sndbuf_size, rcvbuf_size);
	fprintf(ttyout, "Use of PORT cmds: %s.\n", onoff(sendport));
	fprintf(ttyout, "Use of EPSV/EPRT cmds for IPv4: %s%s.\n", onoff(epsv4),
	    epsv4bad ? " (disabled for this connection)" : "");
	fprintf(ttyout, "Use of EPSV/EPRT cmds for IPv6: %s%s.\n", onoff(epsv6),
	    epsv6bad ? " (disabled for this connection)" : "");
	fprintf(ttyout, "Command line editing: %s.\n",
#ifdef NO_EDITCOMPLETE
	    "support not compiled in"
#else	/* !def NO_EDITCOMPLETE */
	    onoff(editing)
#endif	/* !def NO_EDITCOMPLETE */
	    );
	if (macnum > 0) {
		int i;

		fputs("Macros:\n", ttyout);
		for (i=0; i<macnum; i++) {
			fprintf(ttyout, "\t%s\n", macros[i].mac_name);
		}
	}
#endif /* !def NO_STATUS */
	fprintf(ttyout, "Version: %s %s\n", FTP_PRODUCT, FTP_VERSION);
	code = 0;
}

/*
 * Toggle a variable
 */
int
togglevar(int argc, char *argv[], int *var, const char *mesg)
{
	if (argc == 1) {
		*var = !*var;
	} else if (argc == 2 && strcasecmp(argv[1], "on") == 0) {
		*var = 1;
	} else if (argc == 2 && strcasecmp(argv[1], "off") == 0) {
		*var = 0;
	} else {
		UPRINTF("usage: %s [ on | off ]\n", argv[0]);
		return (-1);
	}
	if (mesg)
		fprintf(ttyout, "%s %s.\n", mesg, onoff(*var));
	return (*var);
}

/*
 * Set beep on cmd completed mode.
 */
/*VARARGS*/
void
setbell(int argc, char *argv[])
{

	code = togglevar(argc, argv, &bell, "Bell mode");
}

/*
 * Set command line editing
 */
/*VARARGS*/
void
setedit(int argc, char *argv[])
{

#ifdef NO_EDITCOMPLETE
	if (argc == 0) {
		UPRINTF("usage: %s\n", argv[0]);
		code = -1;
		return;
	}
	if (verbose)
		fputs("Editing support not compiled in; ignoring command.\n",
		    ttyout);
#else	/* !def NO_EDITCOMPLETE */
	code = togglevar(argc, argv, &editing, "Editing mode");
	controlediting();
#endif	/* !def NO_EDITCOMPLETE */
}

/*
 * Turn on packet tracing.
 */
/*VARARGS*/
void
settrace(int argc, char *argv[])
{

	code = togglevar(argc, argv, &trace, "Packet tracing");
}

/*
 * Toggle hash mark printing during transfers, or set hash mark bytecount.
 */
/*VARARGS*/
void
sethash(int argc, char *argv[])
{
	if (argc == 1)
		hash = !hash;
	else if (argc != 2) {
		UPRINTF("usage: %s [ on | off | bytecount ]\n",
		    argv[0]);
		code = -1;
		return;
	} else if (strcasecmp(argv[1], "on") == 0)
		hash = 1;
	else if (strcasecmp(argv[1], "off") == 0)
		hash = 0;
	else {
		int nmark;

		nmark = strsuftoi(argv[1]);
		if (nmark < 1) {
			fprintf(ttyout, "mark: bad bytecount value `%s'.\n",
			    argv[1]);
			code = -1;
			return;
		}
		mark = nmark;
		hash = 1;
	}
	fprintf(ttyout, "Hash mark printing %s", onoff(hash));
	if (hash)
		fprintf(ttyout, " (%d bytes/hash mark)", mark);
	fputs(".\n", ttyout);
	if (hash)
		progress = 0;
	code = hash;
}

/*
 * Turn on printing of server echo's.
 */
/*VARARGS*/
void
setverbose(int argc, char *argv[])
{

	code = togglevar(argc, argv, &verbose, "Verbose mode");
}

/*
 * Toggle PORT/LPRT cmd use before each data connection.
 */
/*VARARGS*/
void
setport(int argc, char *argv[])
{

	code = togglevar(argc, argv, &sendport, "Use of PORT/LPRT cmds");
}

/*
 * Toggle transfer progress bar.
 */
/*VARARGS*/
void
setprogress(int argc, char *argv[])
{

	code = togglevar(argc, argv, &progress, "Progress bar");
	if (progress)
		hash = 0;
}

/*
 * Turn on interactive prompting during mget, mput, and mdelete.
 */
/*VARARGS*/
void
setprompt(int argc, char *argv[])
{

	code = togglevar(argc, argv, &interactive, "Interactive mode");
}

/*
 * Toggle gate-ftp mode, or set gate-ftp server
 */
/*VARARGS*/
void
setgate(int argc, char *argv[])
{
	static char gsbuf[MAXHOSTNAMELEN];

	if (argc == 0 || argc > 3) {
		UPRINTF(
		    "usage: %s [ on | off | gateserver [port] ]\n", argv[0]);
		code = -1;
		return;
	} else if (argc < 2) {
		gatemode = !gatemode;
	} else {
		if (argc == 2 && strcasecmp(argv[1], "on") == 0)
			gatemode = 1;
		else if (argc == 2 && strcasecmp(argv[1], "off") == 0)
			gatemode = 0;
		else {
			if (argc == 3)
				gateport = ftp_strdup(argv[2]);
			(void)strlcpy(gsbuf, argv[1], sizeof(gsbuf));
			gateserver = gsbuf;
			gatemode = 1;
		}
	}
	if (gatemode && (gateserver == NULL || *gateserver == '\0')) {
		fprintf(ttyout,
		    "Disabling gate-ftp mode - no gate-ftp server defined.\n");
		gatemode = 0;
	} else {
		fprintf(ttyout, "Gate ftp: %s, server %s, port %s.\n",
		    onoff(gatemode), *gateserver ? gateserver : "(none)",
		    gateport);
	}
	code = gatemode;
}

/*
 * Toggle metacharacter interpretation on local file names.
 */
/*VARARGS*/
void
setglob(int argc, char *argv[])
{

	code = togglevar(argc, argv, &doglob, "Globbing");
}

/*
 * Toggle preserving modification times on retrieved files.
 */
/*VARARGS*/
void
setpreserve(int argc, char *argv[])
{

	code = togglevar(argc, argv, &preserve, "Preserve modification times");
}

/*
 * Set debugging mode on/off and/or set level of debugging.
 */
/*VARARGS*/
void
setdebug(int argc, char *argv[])
{
	if (argc == 0 || argc > 2) {
		UPRINTF("usage: %s [ on | off | debuglevel ]\n", argv[0]);
		code = -1;
		return;
	} else if (argc == 2) {
		if (strcasecmp(argv[1], "on") == 0)
			ftp_debug = 1;
		else if (strcasecmp(argv[1], "off") == 0)
			ftp_debug = 0;
		else {
			int val;

			val = strsuftoi(argv[1]);
			if (val < 0) {
				fprintf(ttyout, "%s: bad debugging value.\n",
				    argv[1]);
				code = -1;
				return;
			}
			ftp_debug = val;
		}
	} else
		ftp_debug = !ftp_debug;
	if (ftp_debug)
		options |= SO_DEBUG;
	else
		options &= ~SO_DEBUG;
	fprintf(ttyout, "Debugging %s (ftp_debug=%d).\n", onoff(ftp_debug), ftp_debug);
	code = ftp_debug > 0;
}

/*
 * Set current working directory on remote machine.
 */
void
cd(int argc, char *argv[])
{
	int r;

	if (argc == 0 || argc > 2 ||
	    (argc == 1 && !another(&argc, &argv, "remote-directory"))) {
		UPRINTF("usage: %s remote-directory\n", argv[0]);
		code = -1;
		return;
	}
	r = command("CWD %s", argv[1]);
	if (r == ERROR && code == 500) {
		if (verbose)
			fputs("CWD command not recognized, trying XCWD.\n",
			    ttyout);
		r = command("XCWD %s", argv[1]);
	}
	if (r == COMPLETE) {
		dirchange = 1;
		updateremotecwd();
	}
}

/*
 * Set current working directory on local machine.
 */
void
lcd(int argc, char *argv[])
{
	char *locdir;

	code = -1;
	if (argc == 1) {
		argc++;
		argv[1] = localhome;
	}
	if (argc != 2) {
		UPRINTF("usage: %s [local-directory]\n", argv[0]);
		return;
	}
	if ((locdir = globulize(argv[1])) == NULL)
		return;
	if (chdir(locdir) == -1)
		warn("Can't chdir `%s'", locdir);
	else {
		updatelocalcwd();
		if (localcwd[0]) {
			fprintf(ttyout, "Local directory now: %s\n", localcwd);
			code = 0;
		} else {
			fprintf(ttyout, "Unable to determine local directory\n");
		}
	}
	(void)free(locdir);
}

/*
 * Delete a single file.
 */
void
delete(int argc, char *argv[])
{

	if (argc == 0 || argc > 2 ||
	    (argc == 1 && !another(&argc, &argv, "remote-file"))) {
		UPRINTF("usage: %s remote-file\n", argv[0]);
		code = -1;
		return;
	}
	if (command("DELE %s", argv[1]) == COMPLETE)
		dirchange = 1;
}

/*
 * Delete multiple files.
 */
void
mdelete(int argc, char *argv[])
{
	sigfunc oldintr;
	int ointer;
	char *cp;

	if (argc == 0 ||
	    (argc == 1 && !another(&argc, &argv, "remote-files"))) {
		UPRINTF("usage: %s [remote-files]\n", argv[0]);
		code = -1;
		return;
	}
	mflag = 1;
	oldintr = xsignal(SIGINT, mintr);
	if (sigsetjmp(jabort, 1))
		mabort(argv[0]);
	while ((cp = remglob(argv, 0, NULL)) != NULL) {
		if (*cp == '\0') {
			mflag = 0;
			continue;
		}
		if (mflag && confirm(argv[0], cp)) {
			if (command("DELE %s", cp) == COMPLETE)
				dirchange = 1;
			if (!mflag && fromatty) {
				ointer = interactive;
				interactive = 1;
				if (confirm(argv[0], NULL)) {
					mflag++;
				}
				interactive = ointer;
			}
		}
	}
	(void)xsignal(SIGINT, oldintr);
	mflag = 0;
}

/*
 * Rename a remote file.
 */
void
renamefile(int argc, char *argv[])
{

	if (argc == 0 || (argc == 1 && !another(&argc, &argv, "from-name")))
		goto usage;
	if ((argc < 3 && !another(&argc, &argv, "to-name")) || argc > 3) {
 usage:
		UPRINTF("usage: %s from-name to-name\n", argv[0]);
		code = -1;
		return;
	}
	if (command("RNFR %s", argv[1]) == CONTINUE &&
	    command("RNTO %s", argv[2]) == COMPLETE)
		dirchange = 1;
}

/*
 * Get a directory listing of remote files.
 * Supports being invoked as:
 *	cmd		runs
 *	---		----
 *	dir, ls		LIST
 *	mlsd		MLSD
 *	nlist		NLST
 *	pdir, pls	LIST |$PAGER
 *	pmlsd		MLSD |$PAGER
 */
void
ls(int argc, char *argv[])
{
	const char *cmd;
	char *remdir, *locbuf;
	const char *locfile;
	int pagecmd, mlsdcmd;

	remdir = NULL;
	locbuf = NULL;
	locfile = "-";
	pagecmd = mlsdcmd = 0;
			/*
			 * the only commands that start with `p' are
			 * the `pager' versions.
			 */
	if (argv[0][0] == 'p')
		pagecmd = 1;
	if (strcmp(argv[0] + pagecmd , "mlsd") == 0) {
		if (! features[FEAT_MLST]) {
			fprintf(ttyout,
			   "MLSD is not supported by the remote server.\n");
			return;
		}
		mlsdcmd = 1;
	}
	if (argc == 0)
		goto usage;

	if (mlsdcmd)
		cmd = "MLSD";
	else if (strcmp(argv[0] + pagecmd, "nlist") == 0)
		cmd = "NLST";
	else
		cmd = "LIST";

	if (argc > 1)
		remdir = argv[1];
	if (argc > 2)
		locfile = argv[2];
	if (argc > 3 || ((pagecmd | mlsdcmd) && argc > 2)) {
 usage:
		if (pagecmd || mlsdcmd)
			UPRINTF("usage: %s [remote-path]\n", argv[0]);
		else
			UPRINTF("usage: %s [remote-path [local-file]]\n",
			    argv[0]);
		code = -1;
		goto freels;
	}

	if (pagecmd) {
		const char *p;
		size_t len;

		p = getoptionvalue("pager");
		if (EMPTYSTRING(p))
			p = DEFAULTPAGER;
		len = strlen(p) + 2;
		locbuf = ftp_malloc(len);
		locbuf[0] = '|';
		(void)strlcpy(locbuf + 1, p, len - 1);
		locfile = locbuf;
	} else if ((strcmp(locfile, "-") != 0) && *locfile != '|') {
		if ((locbuf = globulize(locfile)) == NULL ||
		    !confirm("output to local-file:", locbuf)) {
			code = -1;
			goto freels;
		}
		locfile = locbuf;
	}
	recvrequest(cmd, locfile, remdir, "w", 0, 0);
 freels:
	if (locbuf)
		(void)free(locbuf);
}

/*
 * Get a directory listing of multiple remote files.
 */
void
mls(int argc, char *argv[])
{
	sigfunc oldintr;
	int ointer, i;
	int volatile dolist;
	char * volatile dest, *odest;
	const char *lmode;

	if (argc == 0)
		goto usage;
	if (argc < 2 && !another(&argc, &argv, "remote-files"))
		goto usage;
	if (argc < 3 && !another(&argc, &argv, "local-file")) {
 usage:
		UPRINTF("usage: %s remote-files local-file\n", argv[0]);
		code = -1;
		return;
	}
	odest = dest = argv[argc - 1];
	argv[argc - 1] = NULL;
	if (strcmp(dest, "-") && *dest != '|')
		if (((dest = globulize(dest)) == NULL) ||
		    !confirm("output to local-file:", dest)) {
			code = -1;
			return;
	}
	dolist = strcmp(argv[0], "mls");
	mflag = 1;
	oldintr = xsignal(SIGINT, mintr);
	if (sigsetjmp(jabort, 1))
		mabort(argv[0]);
	for (i = 1; mflag && i < argc-1 && connected; i++) {
		lmode = (i == 1) ? "w" : "a";
		recvrequest(dolist ? "LIST" : "NLST", dest, argv[i], lmode,
		    0, 0);
		if (!mflag && fromatty) {
			ointer = interactive;
			interactive = 1;
			if (confirm(argv[0], NULL)) {
				mflag++;
			}
			interactive = ointer;
		}
	}
	(void)xsignal(SIGINT, oldintr);
	mflag = 0;
	if (dest != odest)			/* free up after globulize() */
		free(dest);
}

/*
 * Do a shell escape
 */
/*ARGSUSED*/
void
shell(int argc, char *argv[])
{
	pid_t pid;
	sigfunc oldintr;
	char shellnam[MAXPATHLEN];
	const char *shellp, *namep;
	int wait_status;

	if (argc == 0) {
		UPRINTF("usage: %s [command [args]]\n", argv[0]);
		code = -1;
		return;
	}
	oldintr = xsignal(SIGINT, SIG_IGN);
	if ((pid = fork()) == 0) {
		(void)closefrom(3);
		(void)xsignal(SIGINT, SIG_DFL);
		shellp = getenv("SHELL");
		if (shellp == NULL)
			shellp = _PATH_BSHELL;
		namep = strrchr(shellp, '/');
		if (namep == NULL)
			namep = shellp;
		else
			namep++;
		(void)strlcpy(shellnam, namep, sizeof(shellnam));
		if (ftp_debug) {
			fputs(shellp, ttyout);
			putc('\n', ttyout);
		}
		if (argc > 1) {
			execl(shellp, shellnam, "-c", altarg, (char *)0);
		}
		else {
			execl(shellp, shellnam, (char *)0);
		}
		warn("Can't execute `%s'", shellp);
		code = -1;
		exit(1);
	}
	if (pid > 0)
		while (wait(&wait_status) != pid)
			;
	(void)xsignal(SIGINT, oldintr);
	if (pid == -1) {
		warn("Can't fork a subshell; try again later");
		code = -1;
	} else
		code = 0;
}

/*
 * Send new user information (re-login)
 */
void
user(int argc, char *argv[])
{
	char *password;
	char emptypass[] = "";
	int n, aflag = 0;

	if (argc == 0)
		goto usage;
	if (argc < 2)
		(void)another(&argc, &argv, "username");
	if (argc < 2 || argc > 4) {
 usage:
		UPRINTF("usage: %s username [password [account]]\n",
		    argv[0]);
		code = -1;
		return;
	}
	n = command("USER %s", argv[1]);
	if (n == CONTINUE) {
		if (argc < 3) {
			password = getpass("Password: ");
			if (password == NULL)
				password = emptypass;
		} else {
			password = argv[2];
		}
		n = command("PASS %s", password);
		memset(password, 0, strlen(password));
	}
	if (n == CONTINUE) {
		aflag++;
		if (argc < 4) {
			password = getpass("Account: ");
			if (password == NULL)
				password = emptypass;
		} else {
			password = argv[3];
		}
		n = command("ACCT %s", password);
		memset(password, 0, strlen(password));
	}
	if (n != COMPLETE) {
		fputs("Login failed.\n", ttyout);
		return;
	}
	if (!aflag && argc == 4) {
		password = argv[3];
		(void)command("ACCT %s", password);
		memset(password, 0, strlen(password));
	}
	connected = -1;
	getremoteinfo();
}

/*
 * Print working directory on remote machine.
 */
/*VARARGS*/
void
pwd(int argc, char *argv[])
{

	code = -1;
	if (argc != 1) {
		UPRINTF("usage: %s\n", argv[0]);
		return;
	}
	if (! remotecwd[0])
		updateremotecwd();
	if (! remotecwd[0])
		fprintf(ttyout, "Unable to determine remote directory\n");
	else {
		fprintf(ttyout, "Remote directory: %s\n", remotecwd);
		code = 0;
	}
}

/*
 * Print working directory on local machine.
 */
void
lpwd(int argc, char *argv[])
{

	code = -1;
	if (argc != 1) {
		UPRINTF("usage: %s\n", argv[0]);
		return;
	}
	if (! localcwd[0])
		updatelocalcwd();
	if (! localcwd[0])
		fprintf(ttyout, "Unable to determine local directory\n");
	else {
		fprintf(ttyout, "Local directory: %s\n", localcwd);
		code = 0;
	}
}

/*
 * Make a directory.
 */
void
makedir(int argc, char *argv[])
{
	int r;

	if (argc == 0 || argc > 2 ||
	    (argc == 1 && !another(&argc, &argv, "directory-name"))) {
		UPRINTF("usage: %s directory-name\n", argv[0]);
		code = -1;
		return;
	}
	r = command("MKD %s", argv[1]);
	if (r == ERROR && code == 500) {
		if (verbose)
			fputs("MKD command not recognized, trying XMKD.\n",
			    ttyout);
		r = command("XMKD %s", argv[1]);
	}
	if (r == COMPLETE)
		dirchange = 1;
}

/*
 * Remove a directory.
 */
void
removedir(int argc, char *argv[])
{
	int r;

	if (argc == 0 || argc > 2 ||
	    (argc == 1 && !another(&argc, &argv, "directory-name"))) {
		UPRINTF("usage: %s directory-name\n", argv[0]);
		code = -1;
		return;
	}
	r = command("RMD %s", argv[1]);
	if (r == ERROR && code == 500) {
		if (verbose)
			fputs("RMD command not recognized, trying XRMD.\n",
			    ttyout);
		r = command("XRMD %s", argv[1]);
	}
	if (r == COMPLETE)
		dirchange = 1;
}

/*
 * Send a line, verbatim, to the remote machine.
 */
void
quote(int argc, char *argv[])
{

	if (argc == 0 ||
	    (argc == 1 && !another(&argc, &argv, "command line to send"))) {
		UPRINTF("usage: %s line-to-send\n", argv[0]);
		code = -1;
		return;
	}
	quote1("", argc, argv);
}

/*
 * Send a SITE command to the remote machine.  The line
 * is sent verbatim to the remote machine, except that the
 * word "SITE" is added at the front.
 */
void
site(int argc, char *argv[])
{

	if (argc == 0 ||
	    (argc == 1 && !another(&argc, &argv, "arguments to SITE command"))){
		UPRINTF("usage: %s line-to-send\n", argv[0]);
		code = -1;
		return;
	}
	quote1("SITE ", argc, argv);
}

/*
 * Turn argv[1..argc) into a space-separated string, then prepend initial text.
 * Send the result as a one-line command and get response.
 */
void
quote1(const char *initial, int argc, char *argv[])
{
	int i;
	char buf[BUFSIZ];		/* must be >= sizeof(line) */

	(void)strlcpy(buf, initial, sizeof(buf));
	for (i = 1; i < argc; i++) {
		(void)strlcat(buf, argv[i], sizeof(buf));
		if (i < (argc - 1))
			(void)strlcat(buf, " ", sizeof(buf));
	}
	if (command("%s", buf) == PRELIM) {
		while (getreply(0) == PRELIM)
			continue;
	}
	dirchange = 1;
}

void
do_chmod(int argc, char *argv[])
{

	if (argc == 0 || (argc == 1 && !another(&argc, &argv, "mode")))
		goto usage;
	if ((argc < 3 && !another(&argc, &argv, "remote-file")) || argc > 3) {
 usage:
		UPRINTF("usage: %s mode remote-file\n", argv[0]);
		code = -1;
		return;
	}
	(void)command("SITE CHMOD %s %s", argv[1], argv[2]);
}

#define COMMAND_1ARG(argc, argv, cmd)			\
	if (argc == 1)					\
		command(cmd);				\
	else						\
		command(cmd " %s", argv[1])

void
do_umask(int argc, char *argv[])
{
	int oldverbose = verbose;

	if (argc == 0) {
		UPRINTF("usage: %s [umask]\n", argv[0]);
		code = -1;
		return;
	}
	verbose = 1;
	COMMAND_1ARG(argc, argv, "SITE UMASK");
	verbose = oldverbose;
}

void
idlecmd(int argc, char *argv[])
{
	int oldverbose = verbose;

	if (argc < 1 || argc > 2) {
		UPRINTF("usage: %s [seconds]\n", argv[0]);
		code = -1;
		return;
	}
	verbose = 1;
	COMMAND_1ARG(argc, argv, "SITE IDLE");
	verbose = oldverbose;
}

/*
 * Ask the other side for help.
 */
void
rmthelp(int argc, char *argv[])
{
	int oldverbose = verbose;

	if (argc == 0) {
		UPRINTF("usage: %s\n", argv[0]);
		code = -1;
		return;
	}
	verbose = 1;
	COMMAND_1ARG(argc, argv, "HELP");
	verbose = oldverbose;
}

/*
 * Terminate session and exit.
 * May be called with 0, NULL.
 */
/*VARARGS*/
void
quit(int argc, char *argv[])
{

			/* this may be called with argc == 0, argv == NULL */
	if (argc == 0 && argv != NULL) {
		UPRINTF("usage: %s\n", argv[0]);
		code = -1;
		return;
	}
	if (connected)
		disconnect(0, NULL);
	pswitch(1);
	if (connected)
		disconnect(0, NULL);
	exit(0);
}

/*
 * Terminate session, but don't exit.
 * May be called with 0, NULL.
 */
void
disconnect(int argc, char *argv[])
{

			/* this may be called with argc == 0, argv == NULL */
	if (argc == 0 && argv != NULL) {
		UPRINTF("usage: %s\n", argv[0]);
		code = -1;
		return;
	}
	if (!connected)
		return;
	(void)command("QUIT");
	cleanuppeer();
}

void
account(int argc, char *argv[])
{
	char *ap;
	char emptypass[] = "";

	if (argc == 0 || argc > 2) {
		UPRINTF("usage: %s [password]\n", argv[0]);
		code = -1;
		return;
	}
	else if (argc == 2)
		ap = argv[1];
	else {
		ap = getpass("Account:");
		if (ap == NULL)
			ap = emptypass;
	}
	(void)command("ACCT %s", ap);
	memset(ap, 0, strlen(ap));
}

sigjmp_buf abortprox;

void
proxabort(int notused)
{

	sigint_raised = 1;
	alarmtimer(0);
	if (!proxy) {
		pswitch(1);
	}
	if (connected) {
		proxflag = 1;
	}
	else {
		proxflag = 0;
	}
	pswitch(0);
	siglongjmp(abortprox, 1);
}

void
doproxy(int argc, char *argv[])
{
	struct cmd *c;
	int cmdpos;
	sigfunc oldintr;
	char cmdbuf[MAX_C_NAME];

	if (argc == 0 || (argc == 1 && !another(&argc, &argv, "command"))) {
		UPRINTF("usage: %s command\n", argv[0]);
		code = -1;
		return;
	}
	c = getcmd(argv[1]);
	if (c == (struct cmd *) -1) {
		fputs("?Ambiguous command.\n", ttyout);
		code = -1;
		return;
	}
	if (c == 0) {
		fputs("?Invalid command.\n", ttyout);
		code = -1;
		return;
	}
	if (!c->c_proxy) {
		fputs("?Invalid proxy command.\n", ttyout);
		code = -1;
		return;
	}
	if (sigsetjmp(abortprox, 1)) {
		code = -1;
		return;
	}
	oldintr = xsignal(SIGINT, proxabort);
	pswitch(1);
	if (c->c_conn && !connected) {
		fputs("Not connected.\n", ttyout);
		pswitch(0);
		(void)xsignal(SIGINT, oldintr);
		code = -1;
		return;
	}
	cmdpos = strcspn(line, " \t");
	if (cmdpos > 0)		/* remove leading "proxy " from input buffer */
		memmove(line, line + cmdpos + 1, strlen(line) - cmdpos + 1);
	(void)strlcpy(cmdbuf, c->c_name, sizeof(cmdbuf));
	argv[1] = cmdbuf;
	(*c->c_handler)(argc-1, argv+1);
	if (connected) {
		proxflag = 1;
	}
	else {
		proxflag = 0;
	}
	pswitch(0);
	(void)xsignal(SIGINT, oldintr);
}

void
setcase(int argc, char *argv[])
{

	code = togglevar(argc, argv, &mcase, "Case mapping");
}

/*
 * convert the given name to lower case if it's all upper case, into
 * a static buffer which is returned to the caller
 */
static const char *
docase(char *dst, size_t dlen, const char *src)
{
	size_t i;
	int dochange = 1;

	for (i = 0; src[i] != '\0' && i < dlen - 1; i++) {
		dst[i] = src[i];
		if (islower((unsigned char)dst[i]))
			dochange = 0;
	}
	dst[i] = '\0';

	if (dochange) {
		for (i = 0; dst[i] != '\0'; i++)
			if (isupper((unsigned char)dst[i]))
				dst[i] = tolower((unsigned char)dst[i]);
	}
	return dst;
}

void
setcr(int argc, char *argv[])
{

	code = togglevar(argc, argv, &crflag, "Carriage Return stripping");
}

void
setntrans(int argc, char *argv[])
{

	if (argc == 0 || argc > 3) {
		UPRINTF("usage: %s [inchars [outchars]]\n", argv[0]);
		code = -1;
		return;
	}
	if (argc == 1) {
		ntflag = 0;
		fputs("Ntrans off.\n", ttyout);
		code = ntflag;
		return;
	}
	ntflag++;
	code = ntflag;
	(void)strlcpy(ntin, argv[1], sizeof(ntin));
	if (argc == 2) {
		ntout[0] = '\0';
		return;
	}
	(void)strlcpy(ntout, argv[2], sizeof(ntout));
}

static const char *
dotrans(char *dst, size_t dlen, const char *src)
{
	const char *cp1;
	char *cp2 = dst;
	size_t i, ostop;

	for (ostop = 0; *(ntout + ostop) && ostop < 16; ostop++)
		continue;
	for (cp1 = src; *cp1; cp1++) {
		int found = 0;
		for (i = 0; *(ntin + i) && i < 16; i++) {
			if (*cp1 == *(ntin + i)) {
				found++;
				if (i < ostop) {
					*cp2++ = *(ntout + i);
					if (cp2 - dst >= (ptrdiff_t)(dlen - 1))
						goto out;
				}
				break;
			}
		}
		if (!found) {
			*cp2++ = *cp1;
		}
	}
out:
	*cp2 = '\0';
	return dst;
}

void
setnmap(int argc, char *argv[])
{
	char *cp;

	if (argc == 1) {
		mapflag = 0;
		fputs("Nmap off.\n", ttyout);
		code = mapflag;
		return;
	}
	if (argc == 0 ||
	    (argc < 3 && !another(&argc, &argv, "mapout")) || argc > 3) {
		UPRINTF("usage: %s [mapin mapout]\n", argv[0]);
		code = -1;
		return;
	}
	mapflag = 1;
	code = 1;
	cp = strchr(altarg, ' ');
	if (proxy) {
		while(*++cp == ' ')
			continue;
		altarg = cp;
		cp = strchr(altarg, ' ');
	}
	*cp = '\0';
	(void)strlcpy(mapin, altarg, MAXPATHLEN);
	while (*++cp == ' ')
		continue;
	(void)strlcpy(mapout, cp, MAXPATHLEN);
}

static const char *
domap(char *dst, size_t dlen, const char *src)
{
	const char *cp1 = src;
	char *cp2 = mapin;
	const char *tp[9], *te[9];
	int i, toks[9], toknum = 0, match = 1;

	for (i=0; i < 9; ++i) {
		toks[i] = 0;
	}
	while (match && *cp1 && *cp2) {
		switch (*cp2) {
			case '\\':
				if (*++cp2 != *cp1) {
					match = 0;
				}
				break;
			case '$':
				if (*(cp2+1) >= '1' && (*cp2+1) <= '9') {
					if (*cp1 != *(++cp2+1)) {
						toks[toknum = *cp2 - '1']++;
						tp[toknum] = cp1;
						while (*++cp1 && *(cp2+1)
							!= *cp1);
						te[toknum] = cp1;
					}
					cp2++;
					break;
				}
				/* FALLTHROUGH */
			default:
				if (*cp2 != *cp1) {
					match = 0;
				}
				break;
		}
		if (match && *cp1) {
			cp1++;
		}
		if (match && *cp2) {
			cp2++;
		}
	}
	if (!match && *cp1) /* last token mismatch */
	{
		toks[toknum] = 0;
	}
	cp2 = dst;
	*cp2 = '\0';
	cp1 = mapout;
	while (*cp1) {
		match = 0;
		switch (*cp1) {
			case '\\':
				if (*(cp1 + 1)) {
					*cp2++ = *++cp1;
				}
				break;
			case '[':
LOOP:
				if (*++cp1 == '$' &&
				    isdigit((unsigned char)*(cp1+1))) {
					if (*++cp1 == '0') {
						const char *cp3 = src;

						while (*cp3) {
							*cp2++ = *cp3++;
						}
						match = 1;
					}
					else if (toks[toknum = *cp1 - '1']) {
						const char *cp3 = tp[toknum];

						while (cp3 != te[toknum]) {
							*cp2++ = *cp3++;
						}
						match = 1;
					}
				}
				else {
					while (*cp1 && *cp1 != ',' &&
					    *cp1 != ']') {
						if (*cp1 == '\\') {
							cp1++;
						}
						else if (*cp1 == '$' &&
						    isdigit((unsigned char)*(cp1+1))) {
							if (*++cp1 == '0') {
							   const char *cp3 = src;

							   while (*cp3) {
								*cp2++ = *cp3++;
							   }
							}
							else if (toks[toknum =
							    *cp1 - '1']) {
							   const char *cp3=tp[toknum];

							   while (cp3 !=
								  te[toknum]) {
								*cp2++ = *cp3++;
							   }
							}
						}
						else if (*cp1) {
							*cp2++ = *cp1++;
						}
					}
					if (!*cp1) {
						fputs(
						"nmap: unbalanced brackets.\n",
						    ttyout);
						return (src);
					}
					match = 1;
					cp1--;
				}
				if (match) {
					while (*++cp1 && *cp1 != ']') {
					      if (*cp1 == '\\' && *(cp1 + 1)) {
							cp1++;
					      }
					}
					if (!*cp1) {
						fputs(
						"nmap: unbalanced brackets.\n",
						    ttyout);
						return (src);
					}
					break;
				}
				switch (*++cp1) {
					case ',':
						goto LOOP;
					case ']':
						break;
					default:
						cp1--;
						goto LOOP;
				}
				break;
			case '$':
				if (isdigit((unsigned char)*(cp1 + 1))) {
					if (*++cp1 == '0') {
						const char *cp3 = src;

						while (*cp3) {
							*cp2++ = *cp3++;
						}
					}
					else if (toks[toknum = *cp1 - '1']) {
						const char *cp3 = tp[toknum];

						while (cp3 != te[toknum]) {
							*cp2++ = *cp3++;
						}
					}
					break;
				}
				/* intentional drop through */
			default:
				*cp2++ = *cp1;
				break;
		}
		cp1++;
	}
	*cp2 = '\0';
	return *dst ? dst : src;
}

void
setpassive(int argc, char *argv[])
{

	if (argc == 1) {
		passivemode = !passivemode;
		activefallback = passivemode;
	} else if (argc != 2) {
 passiveusage:
		UPRINTF("usage: %s [ on | off | auto ]\n", argv[0]);
		code = -1;
		return;
	} else if (strcasecmp(argv[1], "on") == 0) {
		passivemode = 1;
		activefallback = 0;
	} else if (strcasecmp(argv[1], "off") == 0) {
		passivemode = 0;
		activefallback = 0;
	} else if (strcasecmp(argv[1], "auto") == 0) {
		passivemode = 1;
		activefallback = 1;
	} else
		goto passiveusage;
	fprintf(ttyout, "Passive mode: %s; fallback to active mode: %s.\n",
	    onoff(passivemode), onoff(activefallback));
	code = passivemode;
}


void
setepsv4(int argc, char *argv[])
{
	code = togglevar(argc, argv, &epsv4,
	    verbose ? "EPSV/EPRT on IPv4" : NULL);
	epsv4bad = 0;
}

void
setepsv6(int argc, char *argv[])
{
	code = togglevar(argc, argv, &epsv6,
	    verbose ? "EPSV/EPRT on IPv6" : NULL);
	epsv6bad = 0;
}

void
setepsv(int argc, char*argv[])
{
	setepsv4(argc,argv);
	setepsv6(argc,argv);
}

void
setsunique(int argc, char *argv[])
{

	code = togglevar(argc, argv, &sunique, "Store unique");
}

void
setrunique(int argc, char *argv[])
{

	code = togglevar(argc, argv, &runique, "Receive unique");
}

int
parserate(int argc, char *argv[], int cmdlineopt)
{
	int dir, max, incr, showonly;
	sigfunc oldusr1, oldusr2;

	if (argc > 4 || (argc < (cmdlineopt ? 3 : 2))) {
 usage:
		if (cmdlineopt)
			UPRINTF(
	"usage: %s (all|get|put),maximum-bytes[,increment-bytes]]\n",
			    argv[0]);
		else
			UPRINTF(
	"usage: %s (all|get|put) [maximum-bytes [increment-bytes]]\n",
			    argv[0]);
		return -1;
	}
	dir = max = incr = showonly = 0;
#define	RATE_GET	1
#define	RATE_PUT	2
#define	RATE_ALL	(RATE_GET | RATE_PUT)

	if (strcasecmp(argv[1], "all") == 0)
		dir = RATE_ALL;
	else if (strcasecmp(argv[1], "get") == 0)
		dir = RATE_GET;
	else if (strcasecmp(argv[1], "put") == 0)
		dir = RATE_PUT;
	else
		goto usage;

	if (argc >= 3) {
		if ((max = strsuftoi(argv[2])) < 0)
			goto usage;
	} else
		showonly = 1;

	if (argc == 4) {
		if ((incr = strsuftoi(argv[3])) <= 0)
			goto usage;
	} else
		incr = DEFAULTINCR;

	oldusr1 = xsignal(SIGUSR1, SIG_IGN);
	oldusr2 = xsignal(SIGUSR2, SIG_IGN);
	if (dir & RATE_GET) {
		if (!showonly) {
			rate_get = max;
			rate_get_incr = incr;
		}
		if (!cmdlineopt || verbose)
			fprintf(ttyout,
		"Get transfer rate throttle: %s; maximum: %d; increment %d.\n",
			    onoff(rate_get), rate_get, rate_get_incr);
	}
	if (dir & RATE_PUT) {
		if (!showonly) {
			rate_put = max;
			rate_put_incr = incr;
		}
		if (!cmdlineopt || verbose)
			fprintf(ttyout,
		"Put transfer rate throttle: %s; maximum: %d; increment %d.\n",
			    onoff(rate_put), rate_put, rate_put_incr);
	}
	(void)xsignal(SIGUSR1, oldusr1);
	(void)xsignal(SIGUSR2, oldusr2);
	return 0;
}

void
setrate(int argc, char *argv[])
{

	code = parserate(argc, argv, 0);
}

/* change directory to parent directory */
void
cdup(int argc, char *argv[])
{
	int r;

	if (argc == 0) {
		UPRINTF("usage: %s\n", argv[0]);
		code = -1;
		return;
	}
	r = command("CDUP");
	if (r == ERROR && code == 500) {
		if (verbose)
			fputs("CDUP command not recognized, trying XCUP.\n",
			    ttyout);
		r = command("XCUP");
	}
	if (r == COMPLETE) {
		dirchange = 1;
		updateremotecwd();
	}
}

/*
 * Restart transfer at specific point
 */
void
restart(int argc, char *argv[])
{

	if (argc == 0 || argc > 2) {
		UPRINTF("usage: %s [restart-point]\n", argv[0]);
		code = -1;
		return;
	}
	if (! features[FEAT_REST_STREAM]) {
		fprintf(ttyout,
		    "Restart is not supported by the remote server.\n");
		return;
	}
	if (argc == 2) {
		off_t rp;
		char *ep;

		rp = STRTOLL(argv[1], &ep, 10);
		if (rp < 0 || *ep != '\0')
			fprintf(ttyout, "restart: Invalid offset `%s'\n",
			    argv[1]);
		else
			restart_point = rp;
	}
	if (restart_point == 0)
		fputs("No restart point defined.\n", ttyout);
	else
		fprintf(ttyout,
		    "Restarting at " LLF " for next get, put or append\n",
		    (LLT)restart_point);
}

/*
 * Show remote system type
 */
void
syst(int argc, char *argv[])
{
	int oldverbose = verbose;

	if (argc == 0) {
		UPRINTF("usage: %s\n", argv[0]);
		code = -1;
		return;
	}
	verbose = 1;	/* If we aren't verbose, this doesn't do anything! */
	(void)command("SYST");
	verbose = oldverbose;
}

void
macdef(int argc, char *argv[])
{
	char *tmp;
	int c;

	if (argc == 0)
		goto usage;
	if (macnum == 16) {
		fputs("Limit of 16 macros have already been defined.\n",
		    ttyout);
		code = -1;
		return;
	}
	if ((argc < 2 && !another(&argc, &argv, "macro name")) || argc > 2) {
 usage:
		UPRINTF("usage: %s macro_name\n", argv[0]);
		code = -1;
		return;
	}
	if (interactive)
		fputs(
		"Enter macro line by line, terminating it with a null line.\n",
		    ttyout);
	(void)strlcpy(macros[macnum].mac_name, argv[1],
	    sizeof(macros[macnum].mac_name));
	if (macnum == 0)
		macros[macnum].mac_start = macbuf;
	else
		macros[macnum].mac_start = macros[macnum - 1].mac_end + 1;
	tmp = macros[macnum].mac_start;
	while (tmp != macbuf+4096) {
		if ((c = getchar()) == EOF) {
			fputs("macdef: end of file encountered.\n", ttyout);
			code = -1;
			return;
		}
		if ((*tmp = c) == '\n') {
			if (tmp == macros[macnum].mac_start) {
				macros[macnum++].mac_end = tmp;
				code = 0;
				return;
			}
			if (*(tmp-1) == '\0') {
				macros[macnum++].mac_end = tmp - 1;
				code = 0;
				return;
			}
			*tmp = '\0';
		}
		tmp++;
	}
	while (1) {
		while ((c = getchar()) != '\n' && c != EOF)
			/* LOOP */;
		if (c == EOF || getchar() == '\n') {
			fputs("Macro not defined - 4K buffer exceeded.\n",
			    ttyout);
			code = -1;
			return;
		}
	}
}

/*
 * Get size of file on remote machine
 */
void
sizecmd(int argc, char *argv[])
{
	off_t size;

	if (argc == 0 || argc > 2 ||
	    (argc == 1 && !another(&argc, &argv, "remote-file"))) {
		UPRINTF("usage: %s remote-file\n", argv[0]);
		code = -1;
		return;
	}
	size = remotesize(argv[1], 1);
	if (size != -1)
		fprintf(ttyout,
		    "%s\t" LLF "\n", argv[1], (LLT)size);
	code = (size > 0);
}

/*
 * Get last modification time of file on remote machine
 */
void
modtime(int argc, char *argv[])
{
	time_t mtime;

	if (argc == 0 || argc > 2 ||
	    (argc == 1 && !another(&argc, &argv, "remote-file"))) {
		UPRINTF("usage: %s remote-file\n", argv[0]);
		code = -1;
		return;
	}
	mtime = remotemodtime(argv[1], 1);
	if (mtime != -1)
		fprintf(ttyout, "%s\t%s", argv[1],
		    rfc2822time(localtime(&mtime)));
	code = (mtime > 0);
}

/*
 * Show status on remote machine
 */
void
rmtstatus(int argc, char *argv[])
{

	if (argc == 0) {
		UPRINTF("usage: %s [remote-file]\n", argv[0]);
		code = -1;
		return;
	}
	COMMAND_1ARG(argc, argv, "STAT");
}

/*
 * Get file if modtime is more recent than current file
 */
void
newer(int argc, char *argv[])
{

	if (getit(argc, argv, -1, "w"))
		fprintf(ttyout,
		    "Local file \"%s\" is newer than remote file \"%s\".\n",
		    argv[2], argv[1]);
}

/*
 * Display one local file through $PAGER.
 */
void
lpage(int argc, char *argv[])
{
	size_t len;
	const char *p;
	char *pager, *locfile;

	if (argc == 0 || argc > 2 ||
	    (argc == 1 && !another(&argc, &argv, "local-file"))) {
		UPRINTF("usage: %s local-file\n", argv[0]);
		code = -1;
		return;
	}
	if ((locfile = globulize(argv[1])) == NULL) {
		code = -1;
		return;
	}
	p = getoptionvalue("pager");
	if (EMPTYSTRING(p))
		p = DEFAULTPAGER;
	len = strlen(p) + strlen(locfile) + 2;
	pager = ftp_malloc(len);
	(void)strlcpy(pager, p,		len);
	(void)strlcat(pager, " ",	len);
	(void)strlcat(pager, locfile,	len);
	system(pager);
	code = 0;
	(void)free(pager);
	(void)free(locfile);
}

/*
 * Display one remote file through $PAGER.
 */
void
page(int argc, char *argv[])
{
	int ohash, orestart_point, overbose;
	size_t len;
	const char *p;
	char *pager;

	if (argc == 0 || argc > 2 ||
	    (argc == 1 && !another(&argc, &argv, "remote-file"))) {
		UPRINTF("usage: %s remote-file\n", argv[0]);
		code = -1;
		return;
	}
	p = getoptionvalue("pager");
	if (EMPTYSTRING(p))
		p = DEFAULTPAGER;
	len = strlen(p) + 2;
	pager = ftp_malloc(len);
	pager[0] = '|';
	(void)strlcpy(pager + 1, p, len - 1);

	ohash = hash;
	orestart_point = restart_point;
	overbose = verbose;
	hash = restart_point = verbose = 0;
	recvrequest("RETR", pager, argv[1], "r+", 1, 0);
	hash = ohash;
	restart_point = orestart_point;
	verbose = overbose;
	(void)free(pager);
}

/*
 * Set the socket send or receive buffer size.
 */
void
setxferbuf(int argc, char *argv[])
{
	int size, dir;

	if (argc != 2) {
 usage:
		UPRINTF("usage: %s size\n", argv[0]);
		code = -1;
		return;
	}
	if (strcasecmp(argv[0], "sndbuf") == 0)
		dir = RATE_PUT;
	else if (strcasecmp(argv[0], "rcvbuf") == 0)
		dir = RATE_GET;
	else if (strcasecmp(argv[0], "xferbuf") == 0)
		dir = RATE_ALL;
	else
		goto usage;

	if ((size = strsuftoi(argv[1])) == -1)
		goto usage;

	if (size == 0) {
		fprintf(ttyout, "%s: size must be positive.\n", argv[0]);
		goto usage;
	}

	if (dir & RATE_PUT)
		sndbuf_size = size;
	if (dir & RATE_GET)
		rcvbuf_size = size;
	fprintf(ttyout, "Socket buffer sizes: send %d, receive %d.\n",
	    sndbuf_size, rcvbuf_size);
	code = 0;
}

/*
 * Set or display options (defaults are provided by various env vars)
 */
void
setoption(int argc, char *argv[])
{
	struct option *o;

	code = -1;
	if (argc == 0 || (argc != 1 && argc != 3)) {
		UPRINTF("usage: %s [option value]\n", argv[0]);
		return;
	}

#define	OPTIONINDENT ((int) sizeof("http_proxy"))
	if (argc == 1) {
		for (o = optiontab; o->name != NULL; o++) {
			fprintf(ttyout, "%-*s\t%s\n", OPTIONINDENT,
			    o->name, o->value ? o->value : "");
		}
	} else {
		set_option(argv[1], argv[2], 1);
	}
	code = 0;
}

void
set_option(const char * option, const char * value, int doverbose)
{
	struct option *o;

	o = getoption(option);
	if (o == NULL) {
		fprintf(ttyout, "No such option `%s'.\n", option);
		return;
	}
	FREEPTR(o->value);
	o->value = ftp_strdup(value);
	if (verbose && doverbose)
		fprintf(ttyout, "Setting `%s' to `%s'.\n",
		    o->name, o->value);
}

/*
 * Unset an option
 */
void
unsetoption(int argc, char *argv[])
{
	struct option *o;

	code = -1;
	if (argc == 0 || argc != 2) {
		UPRINTF("usage: %s option\n", argv[0]);
		return;
	}

	o = getoption(argv[1]);
	if (o == NULL) {
		fprintf(ttyout, "No such option `%s'.\n", argv[1]);
		return;
	}
	FREEPTR(o->value);
	fprintf(ttyout, "Unsetting `%s'.\n", o->name);
	code = 0;
}

/*
 * Display features supported by the remote host.
 */
void
feat(int argc, char *argv[])
{
	int oldverbose = verbose;

	if (argc == 0) {
		UPRINTF("usage: %s\n", argv[0]);
		code = -1;
		return;
	}
	if (! features[FEAT_FEAT]) {
		fprintf(ttyout,
		    "FEAT is not supported by the remote server.\n");
		return;
	}
	verbose = 1;	/* If we aren't verbose, this doesn't do anything! */
	(void)command("FEAT");
	verbose = oldverbose;
}

void
mlst(int argc, char *argv[])
{
	int oldverbose = verbose;

	if (argc < 1 || argc > 2) {
		UPRINTF("usage: %s [remote-path]\n", argv[0]);
		code = -1;
		return;
	}
	if (! features[FEAT_MLST]) {
		fprintf(ttyout,
		    "MLST is not supported by the remote server.\n");
		return;
	}
	verbose = 1;	/* If we aren't verbose, this doesn't do anything! */
	COMMAND_1ARG(argc, argv, "MLST");
	verbose = oldverbose;
}

void
opts(int argc, char *argv[])
{
	int oldverbose = verbose;

	if (argc < 2 || argc > 3) {
		UPRINTF("usage: %s command [options]\n", argv[0]);
		code = -1;
		return;
	}
	if (! features[FEAT_FEAT]) {
		fprintf(ttyout,
		    "OPTS is not supported by the remote server.\n");
		return;
	}
	verbose = 1;	/* If we aren't verbose, this doesn't do anything! */
	if (argc == 2)
		command("OPTS %s", argv[1]);
	else
		command("OPTS %s %s", argv[1], argv[2]);
	verbose = oldverbose;
}
