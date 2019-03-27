/*	$NetBSD: util.c,v 1.21 2009/11/15 10:12:37 lukem Exp $	*/
/*	from	NetBSD: util.c,v 1.152 2009/07/13 19:05:41 roy Exp	*/

/*-
 * Copyright (c) 1997-2009 The NetBSD Foundation, Inc.
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

#include "tnftp.h"

#if 0	/* tnftp */

#include <sys/cdefs.h>
#ifndef lint
__RCSID(" NetBSD: util.c,v 1.152 2009/07/13 19:05:41 roy Exp  ");
#endif /* not lint */

/*
 * FTP User Program -- Misc support routines
 */
#include <sys/param.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/ftp.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <signal.h>
#include <libgen.h>
#include <limits.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <tzfile.h>
#include <unistd.h>

#endif	/* tnftp */

#include "ftp_var.h"

/*
 * Connect to peer server and auto-login, if possible.
 */
void
setpeer(int argc, char *argv[])
{
	char *host;
	const char *port;

	if (argc == 0)
		goto usage;
	if (connected) {
		fprintf(ttyout, "Already connected to %s, use close first.\n",
		    hostname);
		code = -1;
		return;
	}
	if (argc < 2)
		(void)another(&argc, &argv, "to");
	if (argc < 2 || argc > 3) {
 usage:
		UPRINTF("usage: %s host-name [port]\n", argv[0]);
		code = -1;
		return;
	}
	if (gatemode)
		port = gateport;
	else
		port = ftpport;
	if (argc > 2)
		port = argv[2];

	if (gatemode) {
		if (gateserver == NULL || *gateserver == '\0')
			errx(1, "main: gateserver not defined");
		host = hookup(gateserver, port);
	} else
		host = hookup(argv[1], port);

	if (host) {
		if (gatemode && verbose) {
			fprintf(ttyout,
			    "Connecting via pass-through server %s\n",
			    gateserver);
		}

		connected = 1;
		/*
		 * Set up defaults for FTP.
		 */
		(void)strlcpy(typename, "ascii", sizeof(typename));
		type = TYPE_A;
		curtype = TYPE_A;
		(void)strlcpy(formname, "non-print", sizeof(formname));
		form = FORM_N;
		(void)strlcpy(modename, "stream", sizeof(modename));
		mode = MODE_S;
		(void)strlcpy(structname, "file", sizeof(structname));
		stru = STRU_F;
		(void)strlcpy(bytename, "8", sizeof(bytename));
		bytesize = 8;
		if (autologin)
			(void)ftp_login(argv[1], NULL, NULL);
	}
}

static void
parse_feat(const char *fline)
{

			/*
			 * work-around broken ProFTPd servers that can't
			 * even obey RFC2389.
			 */
	while (*fline && isspace((int)*fline))
		fline++;

	if (strcasecmp(fline, "MDTM") == 0)
		features[FEAT_MDTM] = 1;
	else if (strncasecmp(fline, "MLST", sizeof("MLST") - 1) == 0) {
		features[FEAT_MLST] = 1;
	} else if (strcasecmp(fline, "REST STREAM") == 0)
		features[FEAT_REST_STREAM] = 1;
	else if (strcasecmp(fline, "SIZE") == 0)
		features[FEAT_SIZE] = 1;
	else if (strcasecmp(fline, "TVFS") == 0)
		features[FEAT_TVFS] = 1;
}

/*
 * Determine the remote system type (SYST) and features (FEAT).
 * Call after a successful login (i.e, connected = -1)
 */
void
getremoteinfo(void)
{
	int overbose, i;

	overbose = verbose;
	if (ftp_debug == 0)
		verbose = -1;

			/* determine remote system type */
	if (command("SYST") == COMPLETE) {
		if (overbose) {
			char *cp, c;

			c = 0;
			cp = strchr(reply_string + 4, ' ');
			if (cp == NULL)
				cp = strchr(reply_string + 4, '\r');
			if (cp) {
				if (cp[-1] == '.')
					cp--;
				c = *cp;
				*cp = '\0';
			}

			fprintf(ttyout, "Remote system type is %s.\n",
			    reply_string + 4);
			if (cp)
				*cp = c;
		}
		if (!strncmp(reply_string, "215 UNIX Type: L8", 17)) {
			if (proxy)
				unix_proxy = 1;
			else
				unix_server = 1;
			/*
			 * Set type to 0 (not specified by user),
			 * meaning binary by default, but don't bother
			 * telling server.  We can use binary
			 * for text files unless changed by the user.
			 */
			type = 0;
			(void)strlcpy(typename, "binary", sizeof(typename));
			if (overbose)
			    fprintf(ttyout,
				"Using %s mode to transfer files.\n",
				typename);
		} else {
			if (proxy)
				unix_proxy = 0;
			else
				unix_server = 0;
			if (overbose &&
			    !strncmp(reply_string, "215 TOPS20", 10))
				fputs(
"Remember to set tenex mode when transferring binary files from this machine.\n",
				    ttyout);
		}
	}

			/* determine features (if any) */
	for (i = 0; i < FEAT_max; i++)
		features[i] = -1;
	reply_callback = parse_feat;
	if (command("FEAT") == COMPLETE) {
		for (i = 0; i < FEAT_max; i++) {
			if (features[i] == -1)
				features[i] = 0;
		}
		features[FEAT_FEAT] = 1;
	} else
		features[FEAT_FEAT] = 0;
#ifndef NO_DEBUG
	if (ftp_debug) {
#define DEBUG_FEAT(x) fprintf(ttyout, "features[" #x "] = %d\n", features[(x)])
		DEBUG_FEAT(FEAT_FEAT);
		DEBUG_FEAT(FEAT_MDTM);
		DEBUG_FEAT(FEAT_MLST);
		DEBUG_FEAT(FEAT_REST_STREAM);
		DEBUG_FEAT(FEAT_SIZE);
		DEBUG_FEAT(FEAT_TVFS);
#undef DEBUG_FEAT
	}
#endif
	reply_callback = NULL;

	verbose = overbose;
}

/*
 * Reset the various variables that indicate connection state back to
 * disconnected settings.
 * The caller is responsible for issuing any commands to the remote server
 * to perform a clean shutdown before this is invoked.
 */
void
cleanuppeer(void)
{

	if (cout)
		(void)fclose(cout);
	cout = NULL;
	connected = 0;
	unix_server = 0;
	unix_proxy = 0;
			/*
			 * determine if anonftp was specifically set with -a
			 * (1), or implicitly set by auto_fetch() (2). in the
			 * latter case, disable after the current xfer
			 */
	if (anonftp == 2)
		anonftp = 0;
	data = -1;
	epsv4bad = 0;
	epsv6bad = 0;
	if (username)
		free(username);
	username = NULL;
	if (!proxy)
		macnum = 0;
}

/*
 * Top-level signal handler for interrupted commands.
 */
void
intr(int signo)
{

	sigint_raised = 1;
	alarmtimer(0);
	if (fromatty)
		write(fileno(ttyout), "\n", 1);
	siglongjmp(toplevel, 1);
}

/*
 * Signal handler for lost connections; cleanup various elements of
 * the connection state, and call cleanuppeer() to finish it off.
 */
void
lostpeer(int dummy)
{
	int oerrno = errno;

	alarmtimer(0);
	if (connected) {
		if (cout != NULL) {
			(void)shutdown(fileno(cout), 1+1);
			(void)fclose(cout);
			cout = NULL;
		}
		if (data >= 0) {
			(void)shutdown(data, 1+1);
			(void)close(data);
			data = -1;
		}
		connected = 0;
	}
	pswitch(1);
	if (connected) {
		if (cout != NULL) {
			(void)shutdown(fileno(cout), 1+1);
			(void)fclose(cout);
			cout = NULL;
		}
		connected = 0;
	}
	proxflag = 0;
	pswitch(0);
	cleanuppeer();
	errno = oerrno;
}


/*
 * Login to remote host, using given username & password if supplied.
 * Return non-zero if successful.
 */
int
ftp_login(const char *host, const char *luser, const char *lpass)
{
	char tmp[80];
	char *fuser, *pass, *facct, *p;
	char emptypass[] = "";
	const char *errormsg;
	int n, aflag, rval, nlen;

	aflag = rval = 0;
	fuser = pass = facct = NULL;
	if (luser)
		fuser = ftp_strdup(luser);
	if (lpass)
		pass = ftp_strdup(lpass);

	DPRINTF("ftp_login: user `%s' pass `%s' host `%s'\n",
	    STRorNULL(fuser), STRorNULL(pass), STRorNULL(host));

	/*
	 * Set up arguments for an anonymous FTP session, if necessary.
	 */
	if (anonftp) {
		FREEPTR(fuser);
		fuser = ftp_strdup("anonymous");	/* as per RFC1635 */
		FREEPTR(pass);
		pass = ftp_strdup(getoptionvalue("anonpass"));
	}

	if (ruserpass(host, &fuser, &pass, &facct) < 0) {
		code = -1;
		goto cleanup_ftp_login;
	}

	while (fuser == NULL) {
		if (localname)
			fprintf(ttyout, "Name (%s:%s): ", host, localname);
		else
			fprintf(ttyout, "Name (%s): ", host);
		errormsg = NULL;
		nlen = get_line(stdin, tmp, sizeof(tmp), &errormsg);
		if (nlen < 0) {
			fprintf(ttyout, "%s; %s aborted.\n", errormsg, "login");
			code = -1;
			goto cleanup_ftp_login;
		} else if (nlen == 0) {
			fuser = ftp_strdup(localname);
		} else {
			fuser = ftp_strdup(tmp);
		}
	}

	if (gatemode) {
		char *nuser;
		size_t len;

		len = strlen(fuser) + 1 + strlen(host) + 1;
		nuser = ftp_malloc(len);
		(void)strlcpy(nuser, fuser, len);
		(void)strlcat(nuser, "@",  len);
		(void)strlcat(nuser, host, len);
		FREEPTR(fuser);
		fuser = nuser;
	}

	n = command("USER %s", fuser);
	if (n == CONTINUE) {
		if (pass == NULL) {
			p = getpass("Password: ");
			if (p == NULL)
				p = emptypass;
			pass = ftp_strdup(p);
			memset(p, 0, strlen(p));
		}
		n = command("PASS %s", pass);
		memset(pass, 0, strlen(pass));
	}
	if (n == CONTINUE) {
		aflag++;
		if (facct == NULL) {
			p = getpass("Account: ");
			if (p == NULL)
				p = emptypass;
			facct = ftp_strdup(p);
			memset(p, 0, strlen(p));
		}
		if (facct[0] == '\0') {
			warnx("Login failed");
			goto cleanup_ftp_login;
		}
		n = command("ACCT %s", facct);
		memset(facct, 0, strlen(facct));
	}
	if ((n != COMPLETE) ||
	    (!aflag && facct != NULL && command("ACCT %s", facct) != COMPLETE)) {
		warnx("Login failed");
		goto cleanup_ftp_login;
	}
	rval = 1;
	username = ftp_strdup(fuser);
	if (proxy)
		goto cleanup_ftp_login;

	connected = -1;
	getremoteinfo();
	for (n = 0; n < macnum; ++n) {
		if (!strcmp("init", macros[n].mac_name)) {
			(void)strlcpy(line, "$init", sizeof(line));
			makeargv();
			domacro(margc, margv);
			break;
		}
	}
	updatelocalcwd();
	updateremotecwd();

 cleanup_ftp_login:
	FREEPTR(fuser);
	if (pass != NULL)
		memset(pass, 0, strlen(pass));
	FREEPTR(pass);
	if (facct != NULL)
		memset(facct, 0, strlen(facct));
	FREEPTR(facct);
	return (rval);
}

/*
 * `another' gets another argument, and stores the new argc and argv.
 * It reverts to the top level (via intr()) on EOF/error.
 *
 * Returns false if no new arguments have been added.
 */
int
another(int *pargc, char ***pargv, const char *aprompt)
{
	const char	*errormsg;
	int		ret, nlen;
	size_t		len;

	len = strlen(line);
	if (len >= sizeof(line) - 3) {
		fputs("Sorry, arguments too long.\n", ttyout);
		intr(0);
	}
	fprintf(ttyout, "(%s) ", aprompt);
	line[len++] = ' ';
	errormsg = NULL;
	nlen = get_line(stdin, line + len, sizeof(line)-len, &errormsg);
	if (nlen < 0) {
		fprintf(ttyout, "%s; %s aborted.\n", errormsg, "operation");
		intr(0);
	}
	len += nlen;
	makeargv();
	ret = margc > *pargc;
	*pargc = margc;
	*pargv = margv;
	return (ret);
}

/*
 * glob files given in argv[] from the remote server.
 * if errbuf isn't NULL, store error messages there instead
 * of writing to the screen.
 */
char *
remglob(char *argv[], int doswitch, const char **errbuf)
{
	static char buf[MAXPATHLEN];
	static FILE *ftemp = NULL;
	static char **args;
	char temp[MAXPATHLEN];
	int oldverbose, oldhash, oldprogress, fd;
	char *cp;
	const char *rmode;
	size_t len;

	if (!mflag || !connected) {
		if (!doglob)
			args = NULL;
		else {
			if (ftemp) {
				(void)fclose(ftemp);
				ftemp = NULL;
			}
		}
		return (NULL);
	}
	if (!doglob) {
		if (args == NULL)
			args = argv;
		if ((cp = *++args) == NULL)
			args = NULL;
		return (cp);
	}
	if (ftemp == NULL) {
		len = strlcpy(temp, tmpdir, sizeof(temp));
		if (temp[len - 1] != '/')
			(void)strlcat(temp, "/", sizeof(temp));
		(void)strlcat(temp, TMPFILE, sizeof(temp));
		if ((fd = mkstemp(temp)) < 0) {
			warn("Unable to create temporary file `%s'", temp);
			return (NULL);
		}
		close(fd);
		oldverbose = verbose;
		verbose = (errbuf != NULL) ? -1 : 0;
		oldhash = hash;
		oldprogress = progress;
		hash = 0;
		progress = 0;
		if (doswitch)
			pswitch(!proxy);
		for (rmode = "w"; *++argv != NULL; rmode = "a")
			recvrequest("NLST", temp, *argv, rmode, 0, 0);
		if ((code / 100) != COMPLETE) {
			if (errbuf != NULL)
				*errbuf = reply_string;
		}
		if (doswitch)
			pswitch(!proxy);
		verbose = oldverbose;
		hash = oldhash;
		progress = oldprogress;
		ftemp = fopen(temp, "r");
		(void)unlink(temp);
		if (ftemp == NULL) {
			if (errbuf == NULL)
				warnx("Can't find list of remote files");
			else
				*errbuf =
				    "Can't find list of remote files";
			return (NULL);
		}
	}
	if (fgets(buf, sizeof(buf), ftemp) == NULL) {
		(void)fclose(ftemp);
		ftemp = NULL;
		return (NULL);
	}
	if ((cp = strchr(buf, '\n')) != NULL)
		*cp = '\0';
	return (buf);
}

/*
 * Glob a local file name specification with the expectation of a single
 * return value. Can't control multiple values being expanded from the
 * expression, we return only the first.
 * Returns NULL on error, or a pointer to a buffer containing the filename
 * that's the caller's responsiblity to free(3) when finished with.
 */
char *
globulize(const char *pattern)
{
	glob_t gl;
	int flags;
	char *p;

	if (!doglob)
		return (ftp_strdup(pattern));

	flags = GLOB_BRACE|GLOB_NOCHECK|GLOB_TILDE;
	memset(&gl, 0, sizeof(gl));
	if (glob(pattern, flags, NULL, &gl) || gl.gl_pathc == 0) {
		warnx("Glob pattern `%s' not found", pattern);
		globfree(&gl);
		return (NULL);
	}
	p = ftp_strdup(gl.gl_pathv[0]);
	globfree(&gl);
	return (p);
}

/*
 * determine size of remote file
 */
off_t
remotesize(const char *file, int noisy)
{
	int overbose, r;
	off_t size;

	overbose = verbose;
	size = -1;
	if (ftp_debug == 0)
		verbose = -1;
	if (! features[FEAT_SIZE]) {
		if (noisy)
			fprintf(ttyout,
			    "SIZE is not supported by remote server.\n");
		goto cleanup_remotesize;
	}
	r = command("SIZE %s", file);
	if (r == COMPLETE) {
		char *cp, *ep;

		cp = strchr(reply_string, ' ');
		if (cp != NULL) {
			cp++;
			size = STRTOLL(cp, &ep, 10);
			if (*ep != '\0' && !isspace((unsigned char)*ep))
				size = -1;
		}
	} else {
		if (r == ERROR && code == 500 && features[FEAT_SIZE] == -1)
			features[FEAT_SIZE] = 0;
		if (noisy && ftp_debug == 0) {
			fputs(reply_string, ttyout);
			putc('\n', ttyout);
		}
	}
 cleanup_remotesize:
	verbose = overbose;
	return (size);
}

/*
 * determine last modification time (in GMT) of remote file
 */
time_t
remotemodtime(const char *file, int noisy)
{
	int	overbose, ocode, r;
	time_t	rtime;

	overbose = verbose;
	ocode = code;
	rtime = -1;
	if (ftp_debug == 0)
		verbose = -1;
	if (! features[FEAT_MDTM]) {
		if (noisy)
			fprintf(ttyout,
			    "MDTM is not supported by remote server.\n");
		goto cleanup_parse_time;
	}
	r = command("MDTM %s", file);
	if (r == COMPLETE) {
		struct tm timebuf;
		char *timestr, *frac;

		/*
		 * time-val = 14DIGIT [ "." 1*DIGIT ]
		 *		YYYYMMDDHHMMSS[.sss]
		 * mdtm-response = "213" SP time-val CRLF / error-response
		 */
		timestr = reply_string + 4;

					/*
					 * parse fraction.
					 * XXX: ignored for now
					 */
		frac = strchr(timestr, '\r');
		if (frac != NULL)
			*frac = '\0';
		frac = strchr(timestr, '.');
		if (frac != NULL)
			*frac++ = '\0';
		if (strlen(timestr) == 15 && strncmp(timestr, "191", 3) == 0) {
			/*
			 * XXX:	Workaround for lame ftpd's that return
			 *	`19100' instead of `2000'
			 */
			fprintf(ttyout,
	    "Y2K warning! Incorrect time-val `%s' received from server.\n",
			    timestr);
			timestr++;
			timestr[0] = '2';
			timestr[1] = '0';
			fprintf(ttyout, "Converted to `%s'\n", timestr);
		}
		memset(&timebuf, 0, sizeof(timebuf));
		if (strlen(timestr) != 14 ||
		    (strptime(timestr, "%Y%m%d%H%M%S", &timebuf) == NULL)) {
 bad_parse_time:
			fprintf(ttyout, "Can't parse time `%s'.\n", timestr);
			goto cleanup_parse_time;
		}
		timebuf.tm_isdst = -1;
		rtime = timegm(&timebuf);
		if (rtime == -1) {
			if (noisy || ftp_debug != 0)
				goto bad_parse_time;
			else
				goto cleanup_parse_time;
		} else {
			DPRINTF("remotemodtime: parsed date `%s' as " LLF
			    ", %s",
			    timestr, (LLT)rtime,
			    rfc2822time(localtime(&rtime)));
		}
	} else {
		if (r == ERROR && code == 500 && features[FEAT_MDTM] == -1)
			features[FEAT_MDTM] = 0;
		if (noisy && ftp_debug == 0) {
			fputs(reply_string, ttyout);
			putc('\n', ttyout);
		}
	}
 cleanup_parse_time:
	verbose = overbose;
	if (rtime == -1)
		code = ocode;
	return (rtime);
}

/*
 * Format tm in an RFC2822 compatible manner, with a trailing \n.
 * Returns a pointer to a static string containing the result.
 */
const char *
rfc2822time(const struct tm *tm)
{
	static char result[50];

	if (strftime(result, sizeof(result),
	    "%a, %d %b %Y %H:%M:%S %z\n", tm) == 0)
		errx(1, "Can't convert RFC2822 time: buffer too small");
	return result;
}

/*
 * Update global `localcwd', which contains the state of the local cwd
 */
void
updatelocalcwd(void)
{

	if (getcwd(localcwd, sizeof(localcwd)) == NULL)
		localcwd[0] = '\0';
	DPRINTF("updatelocalcwd: got `%s'\n", localcwd);
}

/*
 * Update global `remotecwd', which contains the state of the remote cwd
 */
void
updateremotecwd(void)
{
	int	 overbose, ocode;
	size_t	 i;
	char	*cp;

	overbose = verbose;
	ocode = code;
	if (ftp_debug == 0)
		verbose = -1;
	if (command("PWD") != COMPLETE)
		goto badremotecwd;
	cp = strchr(reply_string, ' ');
	if (cp == NULL || cp[0] == '\0' || cp[1] != '"')
		goto badremotecwd;
	cp += 2;
	for (i = 0; *cp && i < sizeof(remotecwd) - 1; i++, cp++) {
		if (cp[0] == '"') {
			if (cp[1] == '"')
				cp++;
			else
				break;
		}
		remotecwd[i] = *cp;
	}
	remotecwd[i] = '\0';
	DPRINTF("updateremotecwd: got `%s'\n", remotecwd);
	goto cleanupremotecwd;
 badremotecwd:
	remotecwd[0]='\0';
 cleanupremotecwd:
	verbose = overbose;
	code = ocode;
}

/*
 * Ensure file is in or under dir.
 * Returns 1 if so, 0 if not (or an error occurred).
 */
int
fileindir(const char *file, const char *dir)
{
	char	parentdirbuf[PATH_MAX+1], *parentdir;
	char	realdir[PATH_MAX+1];
	size_t	dirlen;

					/* determine parent directory of file */
	(void)strlcpy(parentdirbuf, file, sizeof(parentdirbuf));
	parentdir = dirname(parentdirbuf);
	if (strcmp(parentdir, ".") == 0)
		return 1;		/* current directory is ok */

					/* find the directory */
	if (realpath(parentdir, realdir) == NULL) {
		warn("Unable to determine real path of `%s'", parentdir);
		return 0;
	}
	if (realdir[0] != '/')		/* relative result is ok */
		return 1;
	dirlen = strlen(dir);
	if (strncmp(realdir, dir, dirlen) == 0 &&
	    (realdir[dirlen] == '/' || realdir[dirlen] == '\0'))
		return 1;
	return 0;
}

/*
 * List words in stringlist, vertically arranged
 */
void
list_vertical(StringList *sl)
{
	size_t i, j;
	size_t columns, lines;
	char *p;
	size_t w, width;

	width = 0;

	for (i = 0 ; i < sl->sl_cur ; i++) {
		w = strlen(sl->sl_str[i]);
		if (w > width)
			width = w;
	}
	width = (width + 8) &~ 7;

	columns = ttywidth / width;
	if (columns == 0)
		columns = 1;
	lines = (sl->sl_cur + columns - 1) / columns;
	for (i = 0; i < lines; i++) {
		for (j = 0; j < columns; j++) {
			p = sl->sl_str[j * lines + i];
			if (p)
				fputs(p, ttyout);
			if (j * lines + i + lines >= sl->sl_cur) {
				putc('\n', ttyout);
				break;
			}
			if (p) {
				w = strlen(p);
				while (w < width) {
					w = (w + 8) &~ 7;
					(void)putc('\t', ttyout);
				}
			}
		}
	}
}

/*
 * Update the global ttywidth value, using TIOCGWINSZ.
 */
void
setttywidth(int a)
{
	struct winsize winsize;
	int oerrno = errno;

	if (ioctl(fileno(ttyout), TIOCGWINSZ, &winsize) != -1 &&
	    winsize.ws_col != 0)
		ttywidth = winsize.ws_col;
	else
		ttywidth = 80;
	errno = oerrno;
}

/*
 * Change the rate limit up (SIGUSR1) or down (SIGUSR2)
 */
void
crankrate(int sig)
{

	switch (sig) {
	case SIGUSR1:
		if (rate_get)
			rate_get += rate_get_incr;
		if (rate_put)
			rate_put += rate_put_incr;
		break;
	case SIGUSR2:
		if (rate_get && rate_get > rate_get_incr)
			rate_get -= rate_get_incr;
		if (rate_put && rate_put > rate_put_incr)
			rate_put -= rate_put_incr;
		break;
	default:
		err(1, "crankrate invoked with unknown signal: %d", sig);
	}
}


/*
 * Setup or cleanup EditLine structures
 */
#ifndef NO_EDITCOMPLETE
void
controlediting(void)
{
	if (editing && el == NULL && hist == NULL) {
		HistEvent ev;
		int editmode;

		el = el_init(getprogname(), stdin, ttyout, stderr);
		/* init editline */
		hist = history_init();		/* init the builtin history */
		history(hist, &ev, H_SETSIZE, 100);/* remember 100 events */
		el_set(el, EL_HIST, history, hist);	/* use history */

		el_set(el, EL_EDITOR, "emacs");	/* default editor is emacs */
		el_set(el, EL_PROMPT, prompt);	/* set the prompt functions */
		el_set(el, EL_RPROMPT, rprompt);

		/* add local file completion, bind to TAB */
		el_set(el, EL_ADDFN, "ftp-complete",
		    "Context sensitive argument completion",
		    complete);
		el_set(el, EL_BIND, "^I", "ftp-complete", NULL);
		el_source(el, NULL);	/* read ~/.editrc */
		if ((el_get(el, EL_EDITMODE, &editmode) != -1) && editmode == 0)
			editing = 0;	/* the user doesn't want editing,
					 * so disable, and let statement
					 * below cleanup */
		else
			el_set(el, EL_SIGNAL, 1);
	}
	if (!editing) {
		if (hist) {
			history_end(hist);
			hist = NULL;
		}
		if (el) {
			el_end(el);
			el = NULL;
		}
	}
}
#endif /* !NO_EDITCOMPLETE */

/*
 * Convert the string `arg' to an int, which may have an optional SI suffix
 * (`b', `k', `m', `g'). Returns the number for success, -1 otherwise.
 */
int
strsuftoi(const char *arg)
{
	char *cp;
	long val;

	if (!isdigit((unsigned char)arg[0]))
		return (-1);

	val = strtol(arg, &cp, 10);
	if (cp != NULL) {
		if (cp[0] != '\0' && cp[1] != '\0')
			 return (-1);
		switch (tolower((unsigned char)cp[0])) {
		case '\0':
		case 'b':
			break;
		case 'k':
			val <<= 10;
			break;
		case 'm':
			val <<= 20;
			break;
		case 'g':
			val <<= 30;
			break;
		default:
			return (-1);
		}
	}
	if (val < 0 || val > INT_MAX)
		return (-1);

	return (val);
}

/*
 * Set up socket buffer sizes before a connection is made.
 */
void
setupsockbufsize(int sock)
{
	socklen_t slen;

	if (0 == rcvbuf_size) {
		slen = sizeof(rcvbuf_size);
		if (getsockopt(sock, SOL_SOCKET, SO_RCVBUF,
		    (void *)&rcvbuf_size, &slen) == -1)
			err(1, "Unable to determine rcvbuf size");
		if (rcvbuf_size <= 0)
			rcvbuf_size = 8 * 1024;
		if (rcvbuf_size > 8 * 1024 * 1024)
			rcvbuf_size = 8 * 1024 * 1024;
		DPRINTF("setupsockbufsize: rcvbuf_size determined as %d\n",
		    rcvbuf_size);
	}
	if (0 == sndbuf_size) {
		slen = sizeof(sndbuf_size);
		if (getsockopt(sock, SOL_SOCKET, SO_SNDBUF,
		    (void *)&sndbuf_size, &slen) == -1)
			err(1, "Unable to determine sndbuf size");
		if (sndbuf_size <= 0)
			sndbuf_size = 8 * 1024;
		if (sndbuf_size > 8 * 1024 * 1024)
			sndbuf_size = 8 * 1024 * 1024;
		DPRINTF("setupsockbufsize: sndbuf_size determined as %d\n",
		    sndbuf_size);
	}

	if (setsockopt(sock, SOL_SOCKET, SO_SNDBUF,
	    (void *)&sndbuf_size, sizeof(sndbuf_size)) == -1)
		warn("Unable to set sndbuf size %d", sndbuf_size);

	if (setsockopt(sock, SOL_SOCKET, SO_RCVBUF,
	    (void *)&rcvbuf_size, sizeof(rcvbuf_size)) == -1)
		warn("Unable to set rcvbuf size %d", rcvbuf_size);
}

/*
 * Copy characters from src into dst, \ quoting characters that require it
 */
void
ftpvis(char *dst, size_t dstlen, const char *src, size_t srclen)
{
	size_t	di, si;

	for (di = si = 0;
	    src[si] != '\0' && di < dstlen && si < srclen;
	    di++, si++) {
		switch (src[si]) {
		case '\\':
		case ' ':
		case '\t':
		case '\r':
		case '\n':
		case '"':
			dst[di++] = '\\';
			if (di >= dstlen)
				break;
			/* FALLTHROUGH */
		default:
			dst[di] = src[si];
		}
	}
	dst[di] = '\0';
}

/*
 * Copy src into buf (which is len bytes long), expanding % sequences.
 */
void
formatbuf(char *buf, size_t len, const char *src)
{
	const char	*p, *p2, *q;
	size_t		 i;
	int		 op, updirs, pdirs;

#define ADDBUF(x) do { \
		if (i >= len - 1) \
			goto endbuf; \
		buf[i++] = (x); \
	} while (0)

	p = src;
	for (i = 0; *p; p++) {
		if (*p != '%') {
			ADDBUF(*p);
			continue;
		}
		p++;

		switch (op = *p) {

		case '/':
		case '.':
		case 'c':
			p2 = connected ? remotecwd : "";
			updirs = pdirs = 0;

			/* option to determine fixed # of dirs from path */
			if (op == '.' || op == 'c') {
				int skip;

				q = p2;
				while (*p2)		/* calc # of /'s */
					if (*p2++ == '/')
						updirs++;
				if (p[1] == '0') {	/* print <x> or ... */
					pdirs = 1;
					p++;
				}
				if (p[1] >= '1' && p[1] <= '9') {
							/* calc # to skip  */
					skip = p[1] - '0';
					p++;
				} else
					skip = 1;

				updirs -= skip;
				while (skip-- > 0) {
					while ((p2 > q) && (*p2 != '/'))
						p2--;	/* back up */
					if (skip && p2 > q)
						p2--;
				}
				if (*p2 == '/' && p2 != q)
					p2++;
			}

			if (updirs > 0 && pdirs) {
				if (i >= len - 5)
					break;
				if (op == '.') {
					ADDBUF('.');
					ADDBUF('.');
					ADDBUF('.');
				} else {
					ADDBUF('/');
					ADDBUF('<');
					if (updirs > 9) {
						ADDBUF('9');
						ADDBUF('+');
					} else
						ADDBUF('0' + updirs);
					ADDBUF('>');
				}
			}
			for (; *p2; p2++)
				ADDBUF(*p2);
			break;

		case 'M':
		case 'm':
			for (p2 = connected && hostname ? hostname : "-";
			    *p2 ; p2++) {
				if (op == 'm' && *p2 == '.')
					break;
				ADDBUF(*p2);
			}
			break;

		case 'n':
			for (p2 = connected ? username : "-"; *p2 ; p2++)
				ADDBUF(*p2);
			break;

		case '%':
			ADDBUF('%');
			break;

		default:		/* display unknown codes literally */
			ADDBUF('%');
			ADDBUF(op);
			break;

		}
	}
 endbuf:
	buf[i] = '\0';
}

/*
 * Determine if given string is an IPv6 address or not.
 * Return 1 for yes, 0 for no
 */
int
isipv6addr(const char *addr)
{
	int rv = 0;
#ifdef INET6
	struct addrinfo hints, *res;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET6;
	hints.ai_socktype = SOCK_DGRAM;	/*dummy*/
	hints.ai_flags = AI_NUMERICHOST;
	if (getaddrinfo(addr, "0", &hints, &res) != 0)
		rv = 0;
	else {
		rv = 1;
		freeaddrinfo(res);
	}
	DPRINTF("isipv6addr: got %d for %s\n", rv, addr);
#endif
	return (rv == 1) ? 1 : 0;
}

/*
 * Read a line from the FILE stream into buf/buflen using fgets(), so up
 * to buflen-1 chars will be read and the result will be NUL terminated.
 * If the line has a trailing newline it will be removed.
 * If the line is too long, excess characters will be read until
 * newline/EOF/error.
 * If EOF/error occurs or a too-long line is encountered and errormsg
 * isn't NULL, it will be changed to a description of the problem.
 * (The EOF message has a leading \n for cosmetic purposes).
 * Returns:
 *	>=0	length of line (excluding trailing newline) if all ok
 *	-1	error occurred
 *	-2	EOF encountered
 *	-3	line was too long
 */
int
get_line(FILE *stream, char *buf, size_t buflen, const char **errormsg)
{
	int	rv, ch;
	size_t	len;

	if (fgets(buf, buflen, stream) == NULL) {
		if (feof(stream)) {	/* EOF */
			rv = -2;
			if (errormsg)
				*errormsg = "\nEOF received";
		} else  {		/* error */
			rv = -1;
			if (errormsg)
				*errormsg = "Error encountered";
		}
		clearerr(stream);
		return rv;
	}
	len = strlen(buf);
	if (buf[len-1] == '\n') {	/* clear any trailing newline */
		buf[--len] = '\0';
	} else if (len == buflen-1) {	/* line too long */
		while ((ch = getchar()) != '\n' && ch != EOF)
			continue;
		if (errormsg)
			*errormsg = "Input line is too long";
		clearerr(stream);
		return -3;
	}
	if (errormsg)
		*errormsg = NULL;
	return len;
}

/*
 * Internal version of connect(2); sets socket buffer sizes,
 * binds to a specific local address (if set), and
 * supports a connection timeout using a non-blocking connect(2) with
 * a poll(2).
 * Socket fcntl flags are temporarily updated to include O_NONBLOCK;
 * these will not be reverted on connection failure.
 * Returns 0 on success, or -1 upon failure (with an appropriate
 * error message displayed.)
 */
int
ftp_connect(int sock, const struct sockaddr *name, socklen_t namelen)
{
	int		flags, rv, timeout, error;
	socklen_t	slen;
	struct timeval	endtime, now, td;
	struct pollfd	pfd[1];
	char		hname[NI_MAXHOST];
	char		sname[NI_MAXSERV];

	setupsockbufsize(sock);
	if (getnameinfo(name, namelen,
	    hname, sizeof(hname), sname, sizeof(sname),
	    NI_NUMERICHOST | NI_NUMERICSERV) != 0) {
		strlcpy(hname, "?", sizeof(hname));
		strlcpy(sname, "?", sizeof(sname));
	}

	if (bindai != NULL) {			/* bind to specific addr */
		struct addrinfo *ai;

		for (ai = bindai; ai != NULL; ai = ai->ai_next) {
			if (ai->ai_family == name->sa_family)
				break;
		}
		if (ai == NULL)
			ai = bindai;
		if (bind(sock, ai->ai_addr, ai->ai_addrlen) == -1) {
			char	bname[NI_MAXHOST];
			int	saveerr;

			saveerr = errno;
			if (getnameinfo(ai->ai_addr, ai->ai_addrlen,
			    bname, sizeof(bname), NULL, 0, NI_NUMERICHOST) != 0)
				strlcpy(bname, "?", sizeof(bname));
			errno = saveerr;
			warn("Can't bind to `%s'", bname);
			return -1;
		}
	}

						/* save current socket flags */
	if ((flags = fcntl(sock, F_GETFL, 0)) == -1) {
		warn("Can't %s socket flags for connect to `%s:%s'",
		    "save", hname, sname);
		return -1;
	}
						/* set non-blocking connect */
	if (fcntl(sock, F_SETFL, flags | O_NONBLOCK) == -1) {
		warn("Can't set socket non-blocking for connect to `%s:%s'",
		    hname, sname);
		return -1;
	}

	/* NOTE: we now must restore socket flags on successful exit */

	pfd[0].fd = sock;
	pfd[0].events = POLLIN|POLLOUT;

	if (quit_time > 0) {			/* want a non default timeout */
		(void)gettimeofday(&endtime, NULL);
		endtime.tv_sec += quit_time;	/* determine end time */
	}

	rv = connect(sock, name, namelen);	/* inititate the connection */
	if (rv == -1) {				/* connection error */
		if (errno != EINPROGRESS) {	/* error isn't "please wait" */
 connecterror:
			warn("Can't connect to `%s:%s'", hname, sname);
			return -1;
		}

						/* connect EINPROGRESS; wait */
		do {
			if (quit_time > 0) {	/* determine timeout */
				(void)gettimeofday(&now, NULL);
				timersub(&endtime, &now, &td);
				timeout = td.tv_sec * 1000 + td.tv_usec/1000;
				if (timeout < 0)
					timeout = 0;
			} else {
				timeout = INFTIM;
			}
			pfd[0].revents = 0;
			rv = ftp_poll(pfd, 1, timeout);
						/* loop until poll ! EINTR */
		} while (rv == -1 && errno == EINTR);

		if (rv == 0) {			/* poll (connect) timed out */
			errno = ETIMEDOUT;
			goto connecterror;
		}

		if (rv == -1) {			/* poll error */
			goto connecterror;
		} else if (pfd[0].revents & (POLLIN|POLLOUT)) {
			slen = sizeof(error);	/* OK, or pending error */
			if (getsockopt(sock, SOL_SOCKET, SO_ERROR,
			    &error, &slen) == -1) {
						/* Solaris pending error */
				goto connecterror;
			} else if (error != 0) {
				errno = error;	/* BSD pending error */
				goto connecterror;
			}
		} else {
			errno = EBADF;		/* this shouldn't happen ... */
			goto connecterror;
		}
	}

	if (fcntl(sock, F_SETFL, flags) == -1) {
						/* restore socket flags */
		warn("Can't %s socket flags for connect to `%s:%s'",
		    "restore", hname, sname);
		return -1;
	}
	return 0;
}

/*
 * Internal version of listen(2); sets socket buffer sizes first.
 */
int
ftp_listen(int sock, int backlog)
{

	setupsockbufsize(sock);
	return (listen(sock, backlog));
}

/*
 * Internal version of poll(2), to allow reimplementation by select(2)
 * on platforms without the former.
 */
int
ftp_poll(struct pollfd *fds, int nfds, int timeout)
{
#if defined(HAVE_POLL)
	return poll(fds, nfds, timeout);

#elif defined(HAVE_SELECT)
		/* implement poll(2) using select(2) */
	fd_set		rset, wset, xset;
	const int	rsetflags = POLLIN | POLLRDNORM;
	const int	wsetflags = POLLOUT | POLLWRNORM;
	const int	xsetflags = POLLRDBAND;
	struct timeval	tv, *ptv;
	int		i, max, rv;

	FD_ZERO(&rset);			/* build list of read & write events */
	FD_ZERO(&wset);
	FD_ZERO(&xset);
	max = 0;
	for (i = 0; i < nfds; i++) {
		if (fds[i].fd > FD_SETSIZE) {
			warnx("can't select fd %d", fds[i].fd);
			errno = EINVAL;
			return -1;
		} else if (fds[i].fd > max)
			max = fds[i].fd;
		if (fds[i].events & rsetflags)
			FD_SET(fds[i].fd, &rset);
		if (fds[i].events & wsetflags)
			FD_SET(fds[i].fd, &wset);
		if (fds[i].events & xsetflags)
			FD_SET(fds[i].fd, &xset);
	}

	ptv = &tv;			/* determine timeout */
	if (timeout == -1) {		/* wait forever */
		ptv = NULL;
	} else if (timeout == 0) {	/* poll once */
		ptv->tv_sec = 0;
		ptv->tv_usec = 0;
	}
	else if (timeout != 0) {	/* wait timeout milliseconds */
		ptv->tv_sec = timeout / 1000;
		ptv->tv_usec = (timeout % 1000) * 1000;
	}
	rv = select(max + 1, &rset, &wset, &xset, ptv);
	if (rv <= 0)			/* -1 == error, 0 == timeout */
		return rv;

	for (i = 0; i < nfds; i++) {	/* determine results */
		if (FD_ISSET(fds[i].fd, &rset))
			fds[i].revents |= (fds[i].events & rsetflags);
		if (FD_ISSET(fds[i].fd, &wset))
			fds[i].revents |= (fds[i].events & wsetflags);
		if (FD_ISSET(fds[i].fd, &xset))
			fds[i].revents |= (fds[i].events & xsetflags);
	}
	return rv;

#else
# error no way to implement xpoll
#endif
}

/*
 * malloc() with inbuilt error checking
 */
void *
ftp_malloc(size_t size)
{
	void *p;

	p = malloc(size);
	if (p == NULL)
		err(1, "Unable to allocate %ld bytes of memory", (long)size);
	return (p);
}

/*
 * sl_init() with inbuilt error checking
 */
StringList *
ftp_sl_init(void)
{
	StringList *p;

	p = sl_init();
	if (p == NULL)
		err(1, "Unable to allocate memory for stringlist");
	return (p);
}

/*
 * sl_add() with inbuilt error checking
 */
void
ftp_sl_add(StringList *sl, char *i)
{

	if (sl_add(sl, i) == -1)
		err(1, "Unable to add `%s' to stringlist", i);
}

/*
 * strdup() with inbuilt error checking
 */
char *
ftp_strdup(const char *str)
{
	char *s;

	if (str == NULL)
		errx(1, "ftp_strdup: called with NULL argument");
	s = strdup(str);
	if (s == NULL)
		err(1, "Unable to allocate memory for string copy");
	return (s);
}
