/*
 * Copyright (c) 1983, 1990, 1992, 1993
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

#include "rcp_locl.h"
#include <getarg.h>

#define RSH_PROGRAM "rsh"

struct  passwd *pwd;
uid_t	userid;
int     errs, remin, remout;
int     pflag, iamremote, iamrecursive, targetshouldbedirectory;
int     doencrypt, noencrypt;
int     usebroken, usekrb4, usekrb5, forwardtkt;
char    *port;
int     eflag = 0;

#define	CMDNEEDS	64
char cmd[CMDNEEDS];		/* must hold "rcp -r -p -d\0" */

int	 response (void);
void	 rsource (char *, struct stat *);
void	 sink (int, char *[]);
void	 source (int, char *[]);
void	 tolocal (int, char *[]);
void	 toremote (char *, int, char *[]);

int      do_cmd(char *host, char *remuser, char *cmd, int *fdin, int *fdout);

static int fflag, tflag;

static int version_flag, help_flag;

struct getargs args[] = {
    { NULL,	'4', arg_flag,		&usekrb4,	"use Kerberos 4 authentication" },
    { NULL,	'5', arg_flag,		&usekrb5,	"use Kerberos 5 authentication" },
    { NULL,	'F', arg_flag,		&forwardtkt,	"forward credentials" },
    { NULL,	'K', arg_flag,		&usebroken,	"use BSD authentication" },
    { NULL,	'P', arg_string,	&port,		"non-default port", "port" },
    { NULL,	'p', arg_flag,		&pflag,	"preserve file permissions" },
    { NULL,	'r', arg_flag,		&iamrecursive,	"recursive mode" },
    { NULL,	'x', arg_flag,		&doencrypt,	"use encryption" },
    { NULL,	'z', arg_flag,		&noencrypt,	"don't encrypt" },
    { NULL,	'd', arg_flag,		&targetshouldbedirectory },
    { NULL,	'e', arg_flag,		&eflag, 	"passed to rsh" },
    { NULL,	'f', arg_flag,		&fflag },
    { NULL,	't', arg_flag,		&tflag },
    { "version", 0,  arg_flag,		&version_flag },
    { "help",	 0,  arg_flag,		&help_flag }
};

static void
usage (int ret)
{
    arg_printusage (args,
		    sizeof(args) / sizeof(args[0]),
		    NULL,
		    "file1 file2|file... directory");
    exit (ret);
}

int
main(int argc, char **argv)
{
	char *targ;
	int optind = 0;

	setprogname(argv[0]);
	if (getarg (args, sizeof(args) / sizeof(args[0]), argc, argv,
		    &optind))
	    usage (1);
	if(help_flag)
	    usage(0);
	if (version_flag) {
	    print_version (NULL);
	    return 0;
	}

	iamremote = (fflag || tflag);

	argc -= optind;
	argv += optind;

	if ((pwd = getpwuid(userid = getuid())) == NULL)
		errx(1, "unknown user %d", (int)userid);

	remin = STDIN_FILENO;		/* XXX */
	remout = STDOUT_FILENO;

	if (fflag) {			/* Follow "protocol", send data. */
		(void)response();
		source(argc, argv);
		exit(errs);
	}

	if (tflag) {			/* Receive data. */
		sink(argc, argv);
		exit(errs);
	}

	if (argc < 2)
	    usage(1);
	if (argc > 2)
		targetshouldbedirectory = 1;

	remin = remout = -1;
	/* Command to be executed on remote system using "rsh". */
	snprintf(cmd, sizeof(cmd),
		 "rcp%s%s%s", iamrecursive ? " -r" : "",
		 pflag ? " -p" : "", targetshouldbedirectory ? " -d" : "");

	signal(SIGPIPE, lostconn);

	if ((targ = colon(argv[argc - 1])))	/* Dest is remote host. */
		toremote(targ, argc, argv);
	else {
		tolocal(argc, argv);		/* Dest is local host. */
		if (targetshouldbedirectory)
			verifydir(argv[argc - 1]);
	}
	exit(errs);
}

void
toremote(char *targ, int argc, char **argv)
{
	int i;
	char *bp, *host, *src, *suser, *thost, *tuser;

	*targ++ = 0;
	if (*targ == 0)
		targ = ".";

	if ((thost = strchr(argv[argc - 1], '@')) != NULL) {
		/* user@host */
		*thost++ = 0;
		tuser = argv[argc - 1];
		if (*tuser == '\0')
			tuser = NULL;
		else if (!okname(tuser))
			exit(1);
	} else {
		thost = argv[argc - 1];
		tuser = NULL;
	}
	thost = unbracket(thost);

	for (i = 0; i < argc - 1; i++) {
		src = colon(argv[i]);
		if (src) {			/* remote to remote */
			int ret;
			*src++ = 0;
			if (*src == 0)
				src = ".";
			host = strchr(argv[i], '@');
			if (host) {
				*host++ = '\0';
				host = unbracket(host);
				suser = argv[i];
				if (*suser == '\0')
					suser = pwd->pw_name;
				else if (!okname(suser))
					continue;
				ret = asprintf(&bp,
				    "%s%s %s -l %s -n %s %s '%s%s%s:%s'",
					 _PATH_RSH, eflag ? " -e" : "",
					 host, suser, cmd, src,
				    tuser ? tuser : "", tuser ? "@" : "",
				    thost, targ);
			} else {
				host = unbracket(argv[i]);
				ret = asprintf(&bp,
					 "exec %s%s %s -n %s %s '%s%s%s:%s'",
					 _PATH_RSH, eflag ? " -e" : "",
					 host, cmd, src,
					 tuser ? tuser : "", tuser ? "@" : "",
					 thost, targ);
			}
			if (ret == -1)
				err (1, "malloc");
			susystem(bp);
			free(bp);
		} else {			/* local to remote */
			if (remin == -1) {
				if (asprintf(&bp, "%s -t %s", cmd, targ) == -1)
					err (1, "malloc");
				host = thost;

				if (do_cmd(host, tuser, bp, &remin, &remout) < 0)
					exit(1);

				if (response() < 0)
					exit(1);
				free(bp);
			}
			source(1, argv+i);
		}
	}
}

void
tolocal(int argc, char **argv)
{
	int i;
	char *bp, *host, *src, *suser;

	for (i = 0; i < argc - 1; i++) {
		int ret;

		if (!(src = colon(argv[i]))) {		/* Local to local. */
			ret = asprintf(&bp, "exec %s%s%s %s %s", _PATH_CP,
			    iamrecursive ? " -PR" : "", pflag ? " -p" : "",
			    argv[i], argv[argc - 1]);
			if (ret == -1)
				err (1, "malloc");
			if (susystem(bp))
				++errs;
			free(bp);
			continue;
		}
		*src++ = 0;
		if (*src == 0)
			src = ".";
		if ((host = strchr(argv[i], '@')) == NULL) {
			host = argv[i];
			suser = pwd->pw_name;
		} else {
			*host++ = 0;
			suser = argv[i];
			if (*suser == '\0')
				suser = pwd->pw_name;
			else if (!okname(suser))
				continue;
		}
		ret = asprintf(&bp, "%s -f %s", cmd, src);
		if (ret == -1)
			err (1, "malloc");
		if (do_cmd(host, suser, bp, &remin, &remout) < 0) {
			free(bp);
			++errs;
			continue;
		}
		free(bp);
		sink(1, argv + argc - 1);
		close(remin);
		remin = remout = -1;
	}
}

void
source(int argc, char **argv)
{
	struct stat stb;
	static BUF buffer;
	BUF *bp;
	off_t i;
	off_t amt;
	int fd, haderr, indx, result;
	char *last, *name, buf[BUFSIZ];

	for (indx = 0; indx < argc; ++indx) {
                name = argv[indx];
		if ((fd = open(name, O_RDONLY, 0)) < 0)
			goto syserr;
		if (fstat(fd, &stb)) {
syserr:			run_err("%s: %s", name, strerror(errno));
			goto next;
		}
		if (S_ISDIR(stb.st_mode) && iamrecursive) {
			rsource(name, &stb);
			goto next;
		} else if (!S_ISREG(stb.st_mode)) {
			run_err("%s: not a regular file", name);
			goto next;
		}
		if ((last = strrchr(name, '/')) == NULL)
			last = name;
		else
			++last;
		if (pflag) {
			/*
			 * Make it compatible with possible future
			 * versions expecting microseconds.
			 */
			snprintf(buf, sizeof(buf), "T%ld 0 %ld 0\n",
			    (long)stb.st_mtime,
			    (long)stb.st_atime);
			write(remout, buf, strlen(buf));
			if (response() < 0)
				goto next;
		}
#undef MODEMASK
#define	MODEMASK	(S_ISUID|S_ISGID|S_ISVTX|S_IRWXU|S_IRWXG|S_IRWXO)
		snprintf(buf, sizeof(buf), "C%04o %lu %s\n",
			 (unsigned int)(stb.st_mode & MODEMASK),
			 (unsigned long)stb.st_size,
			 last);
		write(remout, buf, strlen(buf));
		if (response() < 0)
			goto next;
		if ((bp = allocbuf(&buffer, fd, BUFSIZ)) == NULL) {
next:			close(fd);
			continue;
		}

		/* Keep writing after an error so that we stay sync'd up. */
		for (haderr = i = 0; i < stb.st_size; i += bp->cnt) {
			amt = bp->cnt;
			if (i + amt > stb.st_size)
				amt = stb.st_size - i;
			if (!haderr) {
			        result = read(fd, bp->buf, (size_t)amt);
				if (result != amt)
					haderr = result >= 0 ? EIO : errno;
			}
			if (haderr)
				write(remout, bp->buf, amt);
			else {
			        result = write(remout, bp->buf, (size_t)amt);
				if (result != amt)
					haderr = result >= 0 ? EIO : errno;
			}
		}
		if (close(fd) && !haderr)
			haderr = errno;
		if (!haderr)
			write(remout, "", 1);
		else
			run_err("%s: %s", name, strerror(haderr));
		response();
	}
}

void
rsource(char *name, struct stat *statp)
{
	DIR *dirp;
	struct dirent *dp;
	char *last, *vect[1], path[MAXPATHLEN];

	if (!(dirp = opendir(name))) {
		run_err("%s: %s", name, strerror(errno));
		return;
	}
	last = strrchr(name, '/');
	if (last == 0)
		last = name;
	else
		last++;
	if (pflag) {
		snprintf(path, sizeof(path), "T%ld 0 %ld 0\n",
		    (long)statp->st_mtime,
		    (long)statp->st_atime);
		write(remout, path, strlen(path));
		if (response() < 0) {
			closedir(dirp);
			return;
		}
	}
	snprintf(path, sizeof(path),
		 "D%04o %d %s\n",
		 (unsigned int)(statp->st_mode & MODEMASK), 0, last);
	write(remout, path, strlen(path));
	if (response() < 0) {
		closedir(dirp);
		return;
	}
	while ((dp = readdir(dirp)) != NULL) {
		if (dp->d_ino == 0)
			continue;
		if (!strcmp(dp->d_name, ".") || !strcmp(dp->d_name, ".."))
			continue;
		if (strlen(name) + 1 + strlen(dp->d_name) >= MAXPATHLEN - 1) {
			run_err("%s/%s: name too long", name, dp->d_name);
			continue;
		}
		snprintf(path, sizeof(path), "%s/%s", name, dp->d_name);
		vect[0] = path;
		source(1, vect);
	}
	closedir(dirp);
	write(remout, "E\n", 2);
	response();
}

void
sink(int argc, char **argv)
{
	static BUF buffer;
	struct stat stb;
	struct timeval tv[2];
	enum { YES, NO, DISPLAYED } wrerr;
	BUF *bp;
	off_t i, j, size;
	int amt, count, exists, first, mask, mode, ofd, omode;
	int setimes, targisdir, wrerrno = 0;
	char ch, *cp, *np, *targ, *why, *vect[1], buf[BUFSIZ];

#define	atime	tv[0]
#define	mtime	tv[1]
#define	SCREWUP(str)	{ why = str; goto screwup; }

	setimes = targisdir = 0;
	mask = umask(0);
	if (!pflag)
		umask(mask);
	if (argc != 1) {
		run_err("ambiguous target");
		exit(1);
	}
	targ = *argv;
	if (targetshouldbedirectory)
		verifydir(targ);
	write(remout, "", 1);
	if (stat(targ, &stb) == 0 && S_ISDIR(stb.st_mode))
		targisdir = 1;
	for (first = 1;; first = 0) {
		cp = buf;
		if (read(remin, cp, 1) <= 0)
			return;
		if (*cp++ == '\n')
			SCREWUP("unexpected <newline>");
		do {
			if (read(remin, &ch, sizeof(ch)) != sizeof(ch))
				SCREWUP("lost connection");
			*cp++ = ch;
		} while (cp < &buf[BUFSIZ - 1] && ch != '\n');
		*cp = 0;

		if (buf[0] == '\01' || buf[0] == '\02') {
			if (iamremote == 0)
				write(STDERR_FILENO,
				    buf + 1, strlen(buf + 1));
			if (buf[0] == '\02')
				exit(1);
			++errs;
			continue;
		}
		if (buf[0] == 'E') {
			write(remout, "", 1);
			return;
		}

		if (ch == '\n')
			*--cp = 0;

		cp = buf;
		if (*cp == 'T') {
			setimes++;
			cp++;
			mtime.tv_sec = strtol(cp, &cp, 10);
			if (!cp || *cp++ != ' ')
				SCREWUP("mtime.sec not delimited");
			mtime.tv_usec = strtol(cp, &cp, 10);
			if (!cp || *cp++ != ' ')
				SCREWUP("mtime.usec not delimited");
			atime.tv_sec = strtol(cp, &cp, 10);
			if (!cp || *cp++ != ' ')
				SCREWUP("atime.sec not delimited");
			atime.tv_usec = strtol(cp, &cp, 10);
			if (!cp || *cp++ != '\0')
				SCREWUP("atime.usec not delimited");
			write(remout, "", 1);
			continue;
		}
		if (*cp != 'C' && *cp != 'D') {
			/*
			 * Check for the case "rcp remote:foo\* local:bar".
			 * In this case, the line "No match." can be returned
			 * by the shell before the rcp command on the remote is
			 * executed so the ^Aerror_message convention isn't
			 * followed.
			 */
			if (first) {
				run_err("%s", cp);
				exit(1);
			}
			SCREWUP("expected control record");
		}
		mode = 0;
		for (++cp; cp < buf + 5; cp++) {
			if (*cp < '0' || *cp > '7')
				SCREWUP("bad mode");
			mode = (mode << 3) | (*cp - '0');
		}
		if (*cp++ != ' ')
			SCREWUP("mode not delimited");

		for (size = 0; isdigit((unsigned char)*cp);)
			size = size * 10 + (*cp++ - '0');
		if (*cp++ != ' ')
			SCREWUP("size not delimited");
		if (targisdir) {
			static char *namebuf;
			static int cursize;
			size_t need;

			need = strlen(targ) + strlen(cp) + 250;
			if (need > cursize) {
				if (!(namebuf = malloc(need)))
					run_err("%s", strerror(errno));
			}
			snprintf(namebuf, need, "%s%s%s", targ,
			    *targ ? "/" : "", cp);
			np = namebuf;
		} else
			np = targ;
		exists = stat(np, &stb) == 0;
		if (buf[0] == 'D') {
			int mod_flag = pflag;
			if (exists) {
				if (!S_ISDIR(stb.st_mode)) {
					errno = ENOTDIR;
					goto bad;
				}
				if (pflag)
					chmod(np, mode);
			} else {
				/* Handle copying from a read-only directory */
				mod_flag = 1;
				if (mkdir(np, mode | S_IRWXU) < 0)
					goto bad;
			}
			vect[0] = np;
			sink(1, vect);
			if (setimes) {
				setimes = 0;
				if (utimes(np, tv) < 0)
				    run_err("%s: set times: %s",
					np, strerror(errno));
			}
			if (mod_flag)
				chmod(np, mode);
			continue;
		}
		omode = mode;
		mode |= S_IWRITE;
		if ((ofd = open(np, O_WRONLY|O_CREAT, mode)) < 0) {
bad:			run_err("%s: %s", np, strerror(errno));
			continue;
		}
		write(remout, "", 1);
		if ((bp = allocbuf(&buffer, ofd, BUFSIZ)) == NULL) {
			close(ofd);
			continue;
		}
		cp = bp->buf;
		wrerr = NO;
		for (count = i = 0; i < size; i += BUFSIZ) {
			amt = BUFSIZ;
			if (i + amt > size)
				amt = size - i;
			count += amt;
			if((j = net_read(remin, cp, amt)) != amt) {
			    run_err("%s", j ? strerror(errno) :
				    "dropped connection");
			    exit(1);
			}
			amt -= j;
			cp += j;
			if (count == bp->cnt) {
				/* Keep reading so we stay sync'd up. */
				if (wrerr == NO) {
					j = write(ofd, bp->buf, (size_t)count);
					if (j != count) {
						wrerr = YES;
						wrerrno = j >= 0 ? EIO : errno;
					}
				}
				count = 0;
				cp = bp->buf;
			}
		}
		if (count != 0 && wrerr == NO &&
		    (j = write(ofd, bp->buf, (size_t)count)) != count) {
			wrerr = YES;
			wrerrno = j >= 0 ? EIO : errno;
		}
		if (ftruncate(ofd, size)) {
			run_err("%s: truncate: %s", np, strerror(errno));
			wrerr = DISPLAYED;
		}
		if (pflag) {
			if (exists || omode != mode)
				if (fchmod(ofd, omode))
					run_err("%s: set mode: %s",
					    np, strerror(errno));
		} else {
			if (!exists && omode != mode)
				if (fchmod(ofd, omode & ~mask))
					run_err("%s: set mode: %s",
					    np, strerror(errno));
		}
		close(ofd);
		response();
		if (setimes && wrerr == NO) {
			setimes = 0;
			if (utimes(np, tv) < 0) {
				run_err("%s: set times: %s",
				    np, strerror(errno));
				wrerr = DISPLAYED;
			}
		}
		switch(wrerr) {
		case YES:
			run_err("%s: %s", np, strerror(wrerrno));
			break;
		case NO:
			write(remout, "", 1);
			break;
		case DISPLAYED:
			break;
		}
	}
screwup:
	run_err("protocol error: %s", why);
	exit(1);
}

int
response(void)
{
	char ch, *cp, resp, rbuf[BUFSIZ];

	if (read(remin, &resp, sizeof(resp)) != sizeof(resp))
		lostconn(0);

	cp = rbuf;
	switch(resp) {
	case 0:				/* ok */
		return (0);
	default:
		*cp++ = resp;
		/* FALLTHROUGH */
	case 1:				/* error, followed by error msg */
	case 2:				/* fatal error, "" */
		do {
			if (read(remin, &ch, sizeof(ch)) != sizeof(ch))
				lostconn(0);
			*cp++ = ch;
		} while (cp < &rbuf[BUFSIZ] && ch != '\n');

		if (!iamremote)
			write(STDERR_FILENO, rbuf, cp - rbuf);
		++errs;
		if (resp == 1)
			return (-1);
		exit(1);
	}
	/* NOTREACHED */
}

#include <stdarg.h>

void
run_err(const char *fmt, ...)
{
	static FILE *fp;
	va_list ap;

	++errs;
	if (fp == NULL && !(fp = fdopen(remout, "w")))
		return;
	va_start(ap, fmt);
	fprintf(fp, "%c", 0x01);
	fprintf(fp, "rcp: ");
	vfprintf(fp, fmt, ap);
	fprintf(fp, "\n");
	fflush(fp);
	va_end(ap);

	if (!iamremote) {
	    va_start(ap, fmt);
	    vwarnx(fmt, ap);
	    va_end(ap);
	}
}

/*
 * This function executes the given command as the specified user on the
 * given host.  This returns < 0 if execution fails, and >= 0 otherwise. This
 * assigns the input and output file descriptors on success.
 *
 * If it cannot create necessary pipes it exits with error message.
 */

int
do_cmd(char *host, char *remuser, char *cmd, int *fdin, int *fdout)
{
	int pin[2], pout[2], reserved[2];

	/*
	 * Reserve two descriptors so that the real pipes won't get
	 * descriptors 0 and 1 because that will screw up dup2 below.
	 */
	pipe(reserved);

	/* Create a socket pair for communicating with rsh. */
	if (pipe(pin) < 0) {
		perror("pipe");
		exit(255);
	}
	if (pipe(pout) < 0) {
		perror("pipe");
		exit(255);
	}

	/* Free the reserved descriptors. */
	close(reserved[0]);
	close(reserved[1]);

	/* For a child to execute the command on the remote host using rsh. */
	if (fork() == 0) {
		char *args[100];
		unsigned int i;

		/* Child. */
		close(pin[1]);
		close(pout[0]);
		dup2(pin[0], 0);
		dup2(pout[1], 1);
		close(pin[0]);
		close(pout[1]);

		i = 0;
		args[i++] = RSH_PROGRAM;
		if (usekrb4)
			args[i++] = "-4";
		if (usekrb5)
			args[i++] = "-5";
		if (usebroken)
			args[i++] = "-K";
		if (doencrypt)
			args[i++] = "-x";
		if (forwardtkt)
			args[i++] = "-F";
		if (noencrypt)
			args[i++] = "-z";
		if (port != NULL) {
			args[i++] = "-p";
			args[i++] = port;
		}
		if (eflag)
		    args[i++] = "-e";
		if (remuser != NULL) {
			args[i++] = "-l";
			args[i++] = remuser;
		}
		args[i++] = host;
		args[i++] = cmd;
		args[i++] = NULL;

		execvp(RSH_PROGRAM, args);
		perror(RSH_PROGRAM);
		exit(1);
	}
	/* Parent.  Close the other side, and return the local side. */
	close(pin[0]);
	*fdout = pin[1];
	close(pout[1]);
	*fdin = pout[0];
	return 0;
}
