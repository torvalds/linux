/*
 * Copyright (c) 2004, 2005, 2007 Darren Tucker (dtucker at zip com au).
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "includes.h"
#if !defined(HAVE_POLL)

#include <sys/types.h>
#include <sys/time.h>
#ifdef HAVE_SYS_SELECT_H
# include <sys/select.h>
#endif

#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include "bsd-poll.h"

/*
 * A minimal implementation of poll(2), built on top of select(2).
 *
 * Only supports POLLIN and POLLOUT flags in pfd.events, and POLLIN, POLLOUT
 * and POLLERR flags in revents.
 *
 * Supports pfd.fd = -1 meaning "unused" although it's not standard.
 */

int
poll(struct pollfd *fds, nfds_t nfds, int timeout)
{
	nfds_t i;
	int saved_errno, ret, fd, maxfd = 0;
	fd_set *readfds = NULL, *writefds = NULL, *exceptfds = NULL;
	size_t nmemb;
	struct timeval tv, *tvp = NULL;

	for (i = 0; i < nfds; i++) {
		fd = fds[i].fd;
		if (fd >= FD_SETSIZE) {
			errno = EINVAL;
			return -1;
		}
		maxfd = MAX(maxfd, fd);
	}

	nmemb = howmany(maxfd + 1 , NFDBITS);
	if ((readfds = calloc(nmemb, sizeof(fd_mask))) == NULL ||
	    (writefds = calloc(nmemb, sizeof(fd_mask))) == NULL ||
	    (exceptfds = calloc(nmemb, sizeof(fd_mask))) == NULL) {
		saved_errno = ENOMEM;
		ret = -1;
		goto out;
	}

	/* populate event bit vectors for the events we're interested in */
	for (i = 0; i < nfds; i++) {
		fd = fds[i].fd;
		if (fd == -1)
			continue;
		if (fds[i].events & POLLIN) {
			FD_SET(fd, readfds);
			FD_SET(fd, exceptfds);
		}
		if (fds[i].events & POLLOUT) {
			FD_SET(fd, writefds);
			FD_SET(fd, exceptfds);
		}
	}

	/* poll timeout is msec, select is timeval (sec + usec) */
	if (timeout >= 0) {
		tv.tv_sec = timeout / 1000;
		tv.tv_usec = (timeout % 1000) * 1000;
		tvp = &tv;
	}

	ret = select(maxfd + 1, readfds, writefds, exceptfds, tvp);
	saved_errno = errno;

	/* scan through select results and set poll() flags */
	for (i = 0; i < nfds; i++) {
		fd = fds[i].fd;
		fds[i].revents = 0;
		if (fd == -1)
			continue;
		if (FD_ISSET(fd, readfds)) {
			fds[i].revents |= POLLIN;
		}
		if (FD_ISSET(fd, writefds)) {
			fds[i].revents |= POLLOUT;
		}
		if (FD_ISSET(fd, exceptfds)) {
			fds[i].revents |= POLLERR;
		}
	}

out:
	free(readfds);
	free(writefds);
	free(exceptfds);
	if (ret == -1)
		errno = saved_errno;
	return ret;
}
#endif
