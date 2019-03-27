/*	$Id: catman.c,v 1.21 2017/02/18 12:24:24 schwarze Exp $ */
/*
 * Copyright (c) 2017 Michael Stapelberg <stapelberg@debian.org>
 * Copyright (c) 2017 Ingo Schwarze <schwarze@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHORS DISCLAIM ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#include "config.h"

#if HAVE_CMSG_XPG42
#define _XPG4_2
#endif

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>

#if HAVE_ERR
#include <err.h>
#endif
#include <errno.h>
#include <fcntl.h>
#if HAVE_FTS
#include <fts.h>
#else
#include "compat_fts.h"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

int	 process_manpage(int, int, const char *);
int	 process_tree(int, int);
void	 run_mandocd(int, const char *, const char *)
		__attribute__((__noreturn__));
ssize_t	 sock_fd_write(int, int, int, int);
void	 usage(void) __attribute__((__noreturn__));


void
run_mandocd(int sockfd, const char *outtype, const char* defos)
{
	char	 sockfdstr[10];

	if (snprintf(sockfdstr, sizeof(sockfdstr), "%d", sockfd) == -1)
		err(1, "snprintf");
	if (defos == NULL)
		execlp("mandocd", "mandocd", "-T", outtype,
		    sockfdstr, (char *)NULL);
	else
		execlp("mandocd", "mandocd", "-T", outtype,
		    "-I", defos, sockfdstr, (char *)NULL);
	err(1, "exec");
}

ssize_t
sock_fd_write(int fd, int fd0, int fd1, int fd2)
{
	const struct timespec timeout = { 0, 10000000 };  /* 0.01 s */
	struct msghdr	 msg;
	struct iovec	 iov;
	union {
		struct cmsghdr	 cmsghdr;
		char		 control[CMSG_SPACE(3 * sizeof(int))];
	} cmsgu;
	struct cmsghdr	*cmsg;
	int		*walk;
	ssize_t		 sz;
	unsigned char	 dummy[1] = {'\0'};

	iov.iov_base = dummy;
	iov.iov_len = sizeof(dummy);

	msg.msg_name = NULL;
	msg.msg_namelen = 0;
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;

	msg.msg_control = cmsgu.control;
	msg.msg_controllen = sizeof(cmsgu.control);

	cmsg = CMSG_FIRSTHDR(&msg);
	cmsg->cmsg_len = CMSG_LEN(3 * sizeof(int));
	cmsg->cmsg_level = SOL_SOCKET;
	cmsg->cmsg_type = SCM_RIGHTS;

	walk = (int *)CMSG_DATA(cmsg);
	*(walk++) = fd0;
	*(walk++) = fd1;
	*(walk++) = fd2;

	/*
	 * It appears that on some systems, sendmsg(3)
	 * may return EAGAIN even in blocking mode.
	 * Seen for example on Oracle Solaris 11.2.
	 * The sleeping time was chosen by experimentation,
	 * to neither cause more than a handful of retries
	 * in normal operation nor unnecessary delays.
	 */
	for (;;) {
		if ((sz = sendmsg(fd, &msg, 0)) != -1 ||
		    errno != EAGAIN)
			break;
		nanosleep(&timeout, NULL);
	}
	return sz;
}

int
process_manpage(int srv_fd, int dstdir_fd, const char *path)
{
	int	 in_fd, out_fd;
	int	 irc;

	if ((in_fd = open(path, O_RDONLY)) == -1) {
		warn("open(%s)", path);
		return 0;
	}

	if ((out_fd = openat(dstdir_fd, path,
	    O_WRONLY | O_NOFOLLOW | O_CREAT | O_TRUNC,
	    S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)) == -1) {
		warn("openat(%s)", path);
		close(in_fd);
		return 0;
	}

	irc = sock_fd_write(srv_fd, in_fd, out_fd, STDERR_FILENO);

	close(in_fd);
	close(out_fd);

	if (irc < 0) {
		warn("sendmsg");
		return -1;
	}
	return 0;
}

int
process_tree(int srv_fd, int dstdir_fd)
{
	FTS		*ftsp;
	FTSENT		*entry;
	const char	*argv[2];
	const char	*path;

	argv[0] = ".";
	argv[1] = (char *)NULL;

	if ((ftsp = fts_open((char * const *)argv,
	    FTS_PHYSICAL | FTS_NOCHDIR, NULL)) == NULL) {
		warn("fts_open");
		return -1;
	}

	while ((entry = fts_read(ftsp)) != NULL) {
		path = entry->fts_path + 2;
		switch (entry->fts_info) {
		case FTS_F:
			if (process_manpage(srv_fd, dstdir_fd, path) == -1) {
				fts_close(ftsp);
				return -1;
			}
			break;
		case FTS_D:
			if (*path != '\0' &&
			    mkdirat(dstdir_fd, path, S_IRWXU | S_IRGRP |
			      S_IXGRP | S_IROTH | S_IXOTH) == -1 &&
			    errno != EEXIST) {
				warn("mkdirat(%s)", path);
				(void)fts_set(ftsp, entry, FTS_SKIP);
			}
			break;
		case FTS_DP:
			break;
		default:
			warnx("%s: not a regular file", path);
			break;
		}
	}

	fts_close(ftsp);
	return 0;
}

int
main(int argc, char **argv)
{
	const char	*defos, *outtype;
	int		 srv_fds[2];
	int		 dstdir_fd;
	int		 opt;
	pid_t		 pid;

	defos = NULL;
	outtype = "ascii";
	while ((opt = getopt(argc, argv, "I:T:")) != -1) {
		switch (opt) {
		case 'I':
			defos = optarg;
			break;
		case 'T':
			outtype = optarg;
			break;
		default:
			usage();
		}
	}

	if (argc > 0) {
		argc -= optind;
		argv += optind;
	}
	if (argc != 2)
		usage();

	if (socketpair(AF_LOCAL, SOCK_STREAM, AF_UNSPEC, srv_fds) == -1)
		err(1, "socketpair");

	pid = fork();
	switch (pid) {
	case -1:
		err(1, "fork");
	case 0:
		close(srv_fds[0]);
		run_mandocd(srv_fds[1], outtype, defos);
	default:
		break;
	}
	close(srv_fds[1]);

	if ((dstdir_fd = open(argv[1], O_RDONLY | O_DIRECTORY)) == -1)
		err(1, "open(%s)", argv[1]);

	if (chdir(argv[0]) == -1)
		err(1, "chdir(%s)", argv[0]);

	return process_tree(srv_fds[0], dstdir_fd) == -1 ? 1 : 0;
}

void
usage(void)
{
	fprintf(stderr, "usage: %s [-I os=name] [-T output] "
	    "srcdir dstdir\n", BINM_CATMAN);
	exit(1);
}
