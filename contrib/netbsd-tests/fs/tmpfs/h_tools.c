/*	$NetBSD: h_tools.c,v 1.4 2011/06/11 18:03:17 christos Exp $	*/

/*
 * Copyright (c) 2005, 2006 The NetBSD Foundation, Inc.
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
 * Helper tools for several tests.  These are kept in a single file due
 * to the limitations of bsd.prog.mk to build a single program in a
 * given directory.
 */

#include <sys/param.h>
#include <sys/types.h>
#include <sys/event.h>
#include <sys/mount.h>
#include <sys/statvfs.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>

#include <assert.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef __FreeBSD__
#include <inttypes.h>
#endif

/* --------------------------------------------------------------------- */

static int getfh_main(int, char **);
static int kqueue_main(int, char **);
static int rename_main(int, char **);
static int sockets_main(int, char **);
static int statvfs_main(int, char **);

/* --------------------------------------------------------------------- */

int
getfh_main(int argc, char **argv)
{
	int error;
	void *fh;
	size_t fh_size;

	if (argc < 2)
		return EXIT_FAILURE;

#ifdef __FreeBSD__
	fh_size = sizeof(fhandle_t);
#else
	fh_size = 0;
#endif

	fh = NULL;
	for (;;) {
		if (fh_size) {
			fh = malloc(fh_size);
			if (fh == NULL) {
				fprintf(stderr, "out of memory");
				return EXIT_FAILURE;
			}
		}
		/*
		 * The kernel provides the necessary size in fh_size -
		 * but it may change if someone moves things around,
		 * so retry untill we have enough memory.
		 */
#ifdef __FreeBSD__
		error = getfh(argv[1], fh);
#else
		error = getfh(argv[1], fh, &fh_size);
#endif
		if (error == 0) {
			break;
		} else {
			if (fh != NULL)
				free(fh);
			if (errno != E2BIG) {
				warn("getfh");
				return EXIT_FAILURE;
			}
		}
	}

	error = write(STDOUT_FILENO, fh, fh_size);
	if (error == -1) {
		warn("write");
		return EXIT_FAILURE;
	}
	free(fh);

	return 0;
}

/* --------------------------------------------------------------------- */

int
kqueue_main(int argc, char **argv)
{
	char *line;
	int i, kq;
	size_t len;
	struct kevent *changes, event;

	if (argc < 2)
		return EXIT_FAILURE;

	argc--;
	argv++;

	changes = malloc(sizeof(struct kevent) * argc);
	if (changes == NULL)
		errx(EXIT_FAILURE, "not enough memory");

	for (i = 0; i < argc; i++) {
		int fd;

		fd = open(argv[i], O_RDONLY);
		if (fd == -1)
			err(EXIT_FAILURE, "cannot open %s", argv[i]);

		EV_SET(&changes[i], fd, EVFILT_VNODE,
		    EV_ADD | EV_ENABLE | EV_ONESHOT,
		    NOTE_ATTRIB | NOTE_DELETE | NOTE_EXTEND | NOTE_LINK |
		    NOTE_RENAME | NOTE_REVOKE | NOTE_WRITE,
		    0, 0);
	}

	kq = kqueue();
	if (kq == -1)
		err(EXIT_FAILURE, "kqueue");

	while ((line = fgetln(stdin, &len)) != NULL) {
		int ec, nev;
		struct timespec to;

		to.tv_sec = 0;
		to.tv_nsec = 100000;

		(void)kevent(kq, changes, argc, &event, 1, &to);

		assert(len > 0);
		assert(line[len - 1] == '\n');
		line[len - 1] = '\0';
		ec = system(line);
		if (ec != EXIT_SUCCESS)
			errx(ec, "%s returned %d", line, ec);

		do {
			nev = kevent(kq, changes, argc, &event, 1, &to);
			if (nev == -1)
				err(EXIT_FAILURE, "kevent");
			else if (nev > 0) {
				for (i = 0; i < argc; i++)
					if (event.ident == changes[i].ident)
						break;

				if (event.fflags & NOTE_ATTRIB)
					printf("%s - NOTE_ATTRIB\n", argv[i]);
				if (event.fflags & NOTE_DELETE)
					printf("%s - NOTE_DELETE\n", argv[i]);
				if (event.fflags & NOTE_EXTEND)
					printf("%s - NOTE_EXTEND\n", argv[i]);
				if (event.fflags & NOTE_LINK)
					printf("%s - NOTE_LINK\n", argv[i]);
				if (event.fflags & NOTE_RENAME)
					printf("%s - NOTE_RENAME\n", argv[i]);
				if (event.fflags & NOTE_REVOKE)
					printf("%s - NOTE_REVOKE\n", argv[i]);
				if (event.fflags & NOTE_WRITE)
					printf("%s - NOTE_WRITE\n", argv[i]);
			}
		} while (nev > 0);
	}

	for (i = 0; i < argc; i++)
		close(changes[i].ident);
	free(changes);

	return EXIT_SUCCESS;
}

/* --------------------------------------------------------------------- */

int
rename_main(int argc, char **argv)
{

	if (argc < 3)
		return EXIT_FAILURE;

	if (rename(argv[1], argv[2]) == -1) {
		warn("rename");
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}

/* --------------------------------------------------------------------- */

int
sockets_main(int argc, char **argv)
{
	int error, fd;
	struct sockaddr_un addr;

	if (argc < 2)
		return EXIT_FAILURE;

	fd = socket(PF_LOCAL, SOCK_STREAM, 0);
	if (fd == -1) {
		warn("socket");
		return EXIT_FAILURE;
	}

#ifdef	__FreeBSD__
	memset(&addr, 0, sizeof(addr));
#endif
	(void)strlcpy(addr.sun_path, argv[1], sizeof(addr.sun_path));
	addr.sun_family = PF_UNIX;
#ifdef	__FreeBSD__
	error = bind(fd, (struct sockaddr *)&addr, SUN_LEN(&addr));
#else
	error = bind(fd, (struct sockaddr *)&addr, sizeof(addr));
#endif
	if (error == -1) {
		warn("connect");
#ifdef	__FreeBSD__
		(void)close(fd);
#endif
		return EXIT_FAILURE;
	}

	close(fd);

	return EXIT_SUCCESS;
}

/* --------------------------------------------------------------------- */

int
statvfs_main(int argc, char **argv)
{
	int error;
	struct statvfs buf;

	if (argc < 2)
		return EXIT_FAILURE;

	error = statvfs(argv[1], &buf);
	if (error != 0) {
		warn("statvfs");
		return EXIT_FAILURE;
	}

	(void)printf("f_bsize=%lu\n", buf.f_bsize);
	(void)printf("f_blocks=%" PRId64 "\n", buf.f_blocks);
	(void)printf("f_bfree=%" PRId64 "\n", buf.f_bfree);
	(void)printf("f_files=%" PRId64 "\n", buf.f_files);

	return EXIT_SUCCESS;
}

/* --------------------------------------------------------------------- */

int
main(int argc, char **argv)
{
	int error;

	if (argc < 2)
		return EXIT_FAILURE;

	argc -= 1;
	argv += 1;

	if (strcmp(argv[0], "getfh") == 0)
		error = getfh_main(argc, argv);
	else if (strcmp(argv[0], "kqueue") == 0)
		error = kqueue_main(argc, argv);
	else if (strcmp(argv[0], "rename") == 0)
		error = rename_main(argc, argv);
	else if (strcmp(argv[0], "sockets") == 0)
		error = sockets_main(argc, argv);
	else if (strcmp(argv[0], "statvfs") == 0)
		error = statvfs_main(argc, argv);
	else
		error = EXIT_FAILURE;

	return error;
}
