/*
 * Copyright (C) 2004-2008  Internet Systems Consortium, Inc. ("ISC")
 * Copyright (C) 2000-2003  Internet Software Consortium.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND ISC DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS.  IN NO EVENT SHALL ISC BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE
 * OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

/* $Id: entropy.c,v 1.82 2008/12/01 23:47:45 tbox Exp $ */

/* \file unix/entropy.c
 * \brief
 * This is the system dependent part of the ISC entropy API.
 */

#include <config.h>

#include <sys/param.h>	/* Openserver 5.0.6A and FD_SETSIZE */
#include <sys/types.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>

#ifdef HAVE_NANOSLEEP
#include <time.h>
#endif
#include <unistd.h>

#include <isc/platform.h>
#include <isc/strerror.h>

#ifdef ISC_PLATFORM_NEEDSYSSELECTH
#include <sys/select.h>
#endif

#include "errno2result.h"

/*%
 * There is only one variable in the entropy data structures that is not
 * system independent, but pulling the structure that uses it into this file
 * ultimately means pulling several other independent structures here also to
 * resolve their interdependencies.  Thus only the problem variable's type
 * is defined here.
 */
#define FILESOURCE_HANDLE_TYPE	int

typedef struct {
	int	handle;
	enum	{
		isc_usocketsource_disconnected,
		isc_usocketsource_connecting,
		isc_usocketsource_connected,
		isc_usocketsource_ndesired,
		isc_usocketsource_wrote,
		isc_usocketsource_reading
	} status;
	size_t	sz_to_recv;
} isc_entropyusocketsource_t;

#include "../entropy.c"

static unsigned int
get_from_filesource(isc_entropysource_t *source, isc_uint32_t desired) {
	isc_entropy_t *ent = source->ent;
	unsigned char buf[128];
	int fd = source->sources.file.handle;
	ssize_t n, ndesired;
	unsigned int added;

	if (source->bad)
		return (0);

	desired = desired / 8 + (((desired & 0x07) > 0) ? 1 : 0);

	added = 0;
	while (desired > 0) {
		ndesired = ISC_MIN(desired, sizeof(buf));
		n = read(fd, buf, ndesired);
		if (n < 0) {
			if (errno == EAGAIN || errno == EINTR)
				goto out;
			goto err;
		}
		if (n == 0)
			goto err;

		entropypool_adddata(ent, buf, n, n * 8);
		added += n * 8;
		desired -= n;
	}
	goto out;

 err:
	(void)close(fd);
	source->sources.file.handle = -1;
	source->bad = ISC_TRUE;

 out:
	return (added);
}

static unsigned int
get_from_usocketsource(isc_entropysource_t *source, isc_uint32_t desired) {
	isc_entropy_t *ent = source->ent;
	unsigned char buf[128];
	int fd = source->sources.usocket.handle;
	ssize_t n = 0, ndesired;
	unsigned int added;
	size_t sz_to_recv = source->sources.usocket.sz_to_recv;

	if (source->bad)
		return (0);

	desired = desired / 8 + (((desired & 0x07) > 0) ? 1 : 0);

	added = 0;
	while (desired > 0) {
		ndesired = ISC_MIN(desired, sizeof(buf));
 eagain_loop:

		switch ( source->sources.usocket.status ) {
		case isc_usocketsource_ndesired:
			buf[0] = ndesired;
			if ((n = sendto(fd, buf, 1, 0, NULL, 0)) < 0) {
				if (errno == EWOULDBLOCK || errno == EINTR ||
				    errno == ECONNRESET)
					goto out;
				goto err;
			}
			INSIST(n == 1);
			source->sources.usocket.status =
						isc_usocketsource_wrote;
			goto eagain_loop;

		case isc_usocketsource_connecting:
		case isc_usocketsource_connected:
			buf[0] = 1;
			buf[1] = ndesired;
			if ((n = sendto(fd, buf, 2, 0, NULL, 0)) < 0) {
				if (errno == EWOULDBLOCK || errno == EINTR ||
				    errno == ECONNRESET)
					goto out;
				goto err;
			}
			if (n == 1) {
				source->sources.usocket.status =
					isc_usocketsource_ndesired;
				goto eagain_loop;
			}
			INSIST(n == 2);
			source->sources.usocket.status =
						isc_usocketsource_wrote;
			/*FALLTHROUGH*/

		case isc_usocketsource_wrote:
			if (recvfrom(fd, buf, 1, 0, NULL, NULL) != 1) {
				if (errno == EAGAIN) {
					/*
					 * The problem of EAGAIN (try again
					 * later) is a major issue on HP-UX.
					 * Solaris actually tries the recvfrom
					 * call again, while HP-UX just dies.
					 * This code is an attempt to let the
					 * entropy pool fill back up (at least
					 * that's what I think the problem is.)
					 * We go to eagain_loop because if we
					 * just "break", then the "desired"
					 * amount gets borked.
					 */
#ifdef HAVE_NANOSLEEP
					struct timespec ts;

					ts.tv_sec = 0;
					ts.tv_nsec = 1000000;
					nanosleep(&ts, NULL);
#else
					usleep(1000);
#endif
					goto eagain_loop;
				}
				if (errno == EWOULDBLOCK || errno == EINTR)
					goto out;
				goto err;
			}
			source->sources.usocket.status =
					isc_usocketsource_reading;
			sz_to_recv = buf[0];
			source->sources.usocket.sz_to_recv = sz_to_recv;
			if (sz_to_recv > sizeof(buf))
				goto err;
			/*FALLTHROUGH*/

		case isc_usocketsource_reading:
			if (sz_to_recv != 0U) {
				n = recv(fd, buf, sz_to_recv, 0);
				if (n < 0) {
					if (errno == EWOULDBLOCK ||
					    errno == EINTR)
						goto out;
					goto err;
				}
			} else
				n = 0;
			break;

		default:
			goto err;
		}

		if ((size_t)n != sz_to_recv)
			source->sources.usocket.sz_to_recv -= n;
		else
			source->sources.usocket.status =
				isc_usocketsource_connected;

		if (n == 0)
			goto out;

		entropypool_adddata(ent, buf, n, n * 8);
		added += n * 8;
		desired -= n;
	}
	goto out;

 err:
	close(fd);
	source->bad = ISC_TRUE;
	source->sources.usocket.status = isc_usocketsource_disconnected;
	source->sources.usocket.handle = -1;

 out:
	return (added);
}

/*
 * Poll each source, trying to get data from it to stuff into the entropy
 * pool.
 */
static void
fillpool(isc_entropy_t *ent, unsigned int desired, isc_boolean_t blocking) {
	unsigned int added;
	unsigned int remaining;
	unsigned int needed;
	unsigned int nsource;
	isc_entropysource_t *source;

	REQUIRE(VALID_ENTROPY(ent));

	needed = desired;

	/*
	 * This logic is a little strange, so an explanation is in order.
	 *
	 * If needed is 0, it means we are being asked to "fill to whatever
	 * we think is best."  This means that if we have at least a
	 * partially full pool (say, > 1/4th of the pool) we probably don't
	 * need to add anything.
	 *
	 * Also, we will check to see if the "pseudo" count is too high.
	 * If it is, try to mix in better data.  Too high is currently
	 * defined as 1/4th of the pool.
	 *
	 * Next, if we are asked to add a specific bit of entropy, make
	 * certain that we will do so.  Clamp how much we try to add to
	 * (DIGEST_SIZE * 8 < needed < POOLBITS - entropy).
	 *
	 * Note that if we are in a blocking mode, we will only try to
	 * get as much data as we need, not as much as we might want
	 * to build up.
	 */
	if (needed == 0) {
		REQUIRE(!blocking);

		if ((ent->pool.entropy >= RND_POOLBITS / 4)
		    && (ent->pool.pseudo <= RND_POOLBITS / 4))
			return;

		needed = THRESHOLD_BITS * 4;
	} else {
		needed = ISC_MAX(needed, THRESHOLD_BITS);
		needed = ISC_MIN(needed, RND_POOLBITS);
	}

	/*
	 * In any case, clamp how much we need to how much we can add.
	 */
	needed = ISC_MIN(needed, RND_POOLBITS - ent->pool.entropy);

	/*
	 * But wait!  If we're not yet initialized, we need at least
	 *	THRESHOLD_BITS
	 * of randomness.
	 */
	if (ent->initialized < THRESHOLD_BITS)
		needed = ISC_MAX(needed, THRESHOLD_BITS - ent->initialized);

	/*
	 * Poll each file source to see if we can read anything useful from
	 * it.  XXXMLG When where are multiple sources, we should keep a
	 * record of which one we last used so we can start from it (or the
	 * next one) to avoid letting some sources build up entropy while
	 * others are always drained.
	 */

	added = 0;
	remaining = needed;
	if (ent->nextsource == NULL) {
		ent->nextsource = ISC_LIST_HEAD(ent->sources);
		if (ent->nextsource == NULL)
			return;
	}
	source = ent->nextsource;
 again_file:
	for (nsource = 0; nsource < ent->nsources; nsource++) {
		unsigned int got;

		if (remaining == 0)
			break;

		got = 0;

		switch ( source->type ) {
		case ENTROPY_SOURCETYPE_FILE:
			got = get_from_filesource(source, remaining);
			break;

		case ENTROPY_SOURCETYPE_USOCKET:
			got = get_from_usocketsource(source, remaining);
			break;
		}

		added += got;

		remaining -= ISC_MIN(remaining, got);

		source = ISC_LIST_NEXT(source, link);
		if (source == NULL)
			source = ISC_LIST_HEAD(ent->sources);
	}
	ent->nextsource = source;

	if (blocking && remaining != 0) {
		int fds;

		fds = wait_for_sources(ent);
		if (fds > 0)
			goto again_file;
	}

	/*
	 * Here, if there are bits remaining to be had and we can block,
	 * check to see if we have a callback source.  If so, call them.
	 */
	source = ISC_LIST_HEAD(ent->sources);
	while ((remaining != 0) && (source != NULL)) {
		unsigned int got;

		got = 0;

		if (source->type == ENTROPY_SOURCETYPE_CALLBACK)
			got = get_from_callback(source, remaining, blocking);

		added += got;
		remaining -= ISC_MIN(remaining, got);

		if (added >= needed)
			break;

		source = ISC_LIST_NEXT(source, link);
	}

	/*
	 * Mark as initialized if we've added enough data.
	 */
	if (ent->initialized < THRESHOLD_BITS)
		ent->initialized += added;
}

static int
wait_for_sources(isc_entropy_t *ent) {
	isc_entropysource_t *source;
	int maxfd, fd;
	int cc;
	fd_set reads;
	fd_set writes;

	maxfd = -1;
	FD_ZERO(&reads);
	FD_ZERO(&writes);

	source = ISC_LIST_HEAD(ent->sources);
	while (source != NULL) {
		if (source->type == ENTROPY_SOURCETYPE_FILE) {
			fd = source->sources.file.handle;
			if (fd >= 0) {
				maxfd = ISC_MAX(maxfd, fd);
				FD_SET(fd, &reads);
			}
		}
		if (source->type == ENTROPY_SOURCETYPE_USOCKET) {
			fd = source->sources.usocket.handle;
			if (fd >= 0) {
				switch (source->sources.usocket.status) {
				case isc_usocketsource_disconnected:
					break;
				case isc_usocketsource_connecting:
				case isc_usocketsource_connected:
				case isc_usocketsource_ndesired:
					maxfd = ISC_MAX(maxfd, fd);
					FD_SET(fd, &writes);
					break;
				case isc_usocketsource_wrote:
				case isc_usocketsource_reading:
					maxfd = ISC_MAX(maxfd, fd);
					FD_SET(fd, &reads);
					break;
				}
			}
		}
		source = ISC_LIST_NEXT(source, link);
	}

	if (maxfd < 0)
		return (-1);

	cc = select(maxfd + 1, &reads, &writes, NULL, NULL);
	if (cc < 0)
		return (-1);

	return (cc);
}

static void
destroyfilesource(isc_entropyfilesource_t *source) {
	(void)close(source->handle);
}

static void
destroyusocketsource(isc_entropyusocketsource_t *source) {
	close(source->handle);
}

/*
 * Make a fd non-blocking
 */
static isc_result_t
make_nonblock(int fd) {
	int ret;
	int flags;
	char strbuf[ISC_STRERRORSIZE];
#ifdef USE_FIONBIO_IOCTL
	int on = 1;

	ret = ioctl(fd, FIONBIO, (char *)&on);
#else
	flags = fcntl(fd, F_GETFL, 0);
	flags |= PORT_NONBLOCK;
	ret = fcntl(fd, F_SETFL, flags);
#endif

	if (ret == -1) {
		isc__strerror(errno, strbuf, sizeof(strbuf));
		UNEXPECTED_ERROR(__FILE__, __LINE__,
#ifdef USE_FIONBIO_IOCTL
				 "ioctl(%d, FIONBIO, &on): %s", fd,
#else
				 "fcntl(%d, F_SETFL, %d): %s", fd, flags,
#endif
				 strbuf);

		return (ISC_R_UNEXPECTED);
	}

	return (ISC_R_SUCCESS);
}

isc_result_t
isc_entropy_createfilesource(isc_entropy_t *ent, const char *fname) {
	int fd;
	struct stat _stat;
	isc_boolean_t is_usocket = ISC_FALSE;
	isc_boolean_t is_connected = ISC_FALSE;
	isc_result_t ret;
	isc_entropysource_t *source;

	REQUIRE(VALID_ENTROPY(ent));
	REQUIRE(fname != NULL);

	LOCK(&ent->lock);

	if (stat(fname, &_stat) < 0) {
		ret = isc__errno2result(errno);
		goto errout;
	}
	/*
	 * Solaris 2.5.1 does not have support for sockets (S_IFSOCK),
	 * but it does return type S_IFIFO (the OS believes that
	 * the socket is a fifo).  This may be an issue if we tell
	 * the program to look at an actual FIFO as its source of
	 * entropy.
	 */
#if defined(S_ISSOCK)
	if (S_ISSOCK(_stat.st_mode))
		is_usocket = ISC_TRUE;
#endif
#if defined(S_ISFIFO) && defined(sun)
	if (S_ISFIFO(_stat.st_mode))
		is_usocket = ISC_TRUE;
#endif
	if (is_usocket)
		fd = socket(PF_UNIX, SOCK_STREAM, 0);
	else
		fd = open(fname, O_RDONLY | PORT_NONBLOCK, 0);

	if (fd < 0) {
		ret = isc__errno2result(errno);
		goto errout;
	}

	ret = make_nonblock(fd);
	if (ret != ISC_R_SUCCESS)
		goto closefd;

	if (is_usocket) {
		struct sockaddr_un sname;

		memset(&sname, 0, sizeof(sname));
		sname.sun_family = AF_UNIX;
		strncpy(sname.sun_path, fname, sizeof(sname.sun_path));
		sname.sun_path[sizeof(sname.sun_path)-1] = '0';
#ifdef ISC_PLATFORM_HAVESALEN
#if !defined(SUN_LEN)
#define SUN_LEN(su) \
	(sizeof(*(su)) - sizeof((su)->sun_path) + strlen((su)->sun_path))
#endif
		sname.sun_len = SUN_LEN(&sname);
#endif

		if (connect(fd, (struct sockaddr *) &sname,
			    sizeof(struct sockaddr_un)) < 0) {
			if (errno != EINPROGRESS) {
				ret = isc__errno2result(errno);
				goto closefd;
			}
		} else
			is_connected = ISC_TRUE;
	}

	source = isc_mem_get(ent->mctx, sizeof(isc_entropysource_t));
	if (source == NULL) {
		ret = ISC_R_NOMEMORY;
		goto closefd;
	}

	/*
	 * From here down, no failures can occur.
	 */
	source->magic = SOURCE_MAGIC;
	source->ent = ent;
	source->total = 0;
	source->bad = ISC_FALSE;
	memset(source->name, 0, sizeof(source->name));
	ISC_LINK_INIT(source, link);
	if (is_usocket) {
		source->sources.usocket.handle = fd;
		if (is_connected)
			source->sources.usocket.status =
					isc_usocketsource_connected;
		else
			source->sources.usocket.status =
					isc_usocketsource_connecting;
		source->sources.usocket.sz_to_recv = 0;
		source->type = ENTROPY_SOURCETYPE_USOCKET;
	} else {
		source->sources.file.handle = fd;
		source->type = ENTROPY_SOURCETYPE_FILE;
	}

	/*
	 * Hook it into the entropy system.
	 */
	ISC_LIST_APPEND(ent->sources, source, link);
	ent->nsources++;

	UNLOCK(&ent->lock);
	return (ISC_R_SUCCESS);

 closefd:
	(void)close(fd);

 errout:
	UNLOCK(&ent->lock);

	return (ret);
}
