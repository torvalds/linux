/*
 * socket.c - low-level socket operations
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <stdio.h>

#include "ntp.h"
#include "ntp_io.h"
#include "ntp_net.h"
#include "ntp_debug.h"

/*
 * Windows C runtime ioctl() can't deal properly with sockets, 
 * map to ioctlsocket for this source file.
 */
#ifdef SYS_WINNT
#define ioctl(fd, opt, val)  ioctlsocket(fd, opt, (u_long *)(val))
#endif

/*
 * on Unix systems the stdio library typically
 * makes use of file descriptors in the lower
 * integer range.  stdio usually will make use
 * of the file descriptors in the range of
 * [0..FOPEN_MAX)
 * in order to keep this range clean, for socket
 * file descriptors we attempt to move them above
 * FOPEN_MAX. This is not as easy as it sounds as
 * FOPEN_MAX changes from implementation to implementation
 * and may exceed to current file decriptor limits.
 * We are using following strategy:
 * - keep a current socket fd boundary initialized with
 *   max(0, min(GETDTABLESIZE() - FD_CHUNK, FOPEN_MAX))
 * - attempt to move the descriptor to the boundary or
 *   above.
 *   - if that fails and boundary > 0 set boundary
 *     to min(0, socket_fd_boundary - FD_CHUNK)
 *     -> retry
 *     if failure and boundary == 0 return old fd
 *   - on success close old fd return new fd
 *
 * effects:
 *   - fds will be moved above the socket fd boundary
 *     if at all possible.
 *   - the socket boundary will be reduced until
 *     allocation is possible or 0 is reached - at this
 *     point the algrithm will be disabled
 */
SOCKET
move_fd(
	SOCKET fd
	)
{
#if !defined(SYS_WINNT) && defined(F_DUPFD)
#ifndef FD_CHUNK
#define FD_CHUNK	10
#endif
#ifndef FOPEN_MAX
#define FOPEN_MAX	20
#endif
/*
 * number of fds we would like to have for
 * stdio FILE* available.
 * we can pick a "low" number as our use of
 * FILE* is limited to log files and temporarily
 * to data and config files. Except for log files
 * we don't keep the other FILE* open beyond the
 * scope of the function that opened it.
 */
#ifndef FD_PREFERRED_SOCKBOUNDARY
#define FD_PREFERRED_SOCKBOUNDARY 48
#endif

	static SOCKET socket_boundary = -1;
	SOCKET newfd;

	REQUIRE((int)fd >= 0);

	/*
	 * check whether boundary has be set up
	 * already
	 */
	if (socket_boundary == -1) {
		socket_boundary = max(0, min(GETDTABLESIZE() - FD_CHUNK,
					     min(FOPEN_MAX, FD_PREFERRED_SOCKBOUNDARY)));
		TRACE(1, ("move_fd: estimated max descriptors: %d, "
			  "initial socket boundary: %d\n",
			  GETDTABLESIZE(), socket_boundary));
	}

	/*
	 * Leave a space for stdio to work in. potentially moving the
	 * socket_boundary lower until allocation succeeds.
	 */
	do {
		if (fd >= 0 && fd < socket_boundary) {
			/* inside reserved range: attempt to move fd */
			newfd = fcntl(fd, F_DUPFD, socket_boundary);

			if (newfd != -1) {
				/* success: drop the old one - return the new one */
				close(fd);
				return newfd;
			}
		} else {
			/* outside reserved range: no work - return the original one */
			return fd;
		}
		socket_boundary = max(0, socket_boundary - FD_CHUNK);
		TRACE(1, ("move_fd: selecting new socket boundary: %d\n",
			  socket_boundary));
	} while (socket_boundary > 0);
#else
	ENSURE((int)fd >= 0);
#endif /* !defined(SYS_WINNT) && defined(F_DUPFD) */
	return fd;
}


/*
 * make_socket_nonblocking() - set up descriptor to be non blocking
 */
void
make_socket_nonblocking(
	SOCKET fd
	)
{
	/*
	 * set non-blocking,
	 */

#ifdef USE_FIONBIO
	/* in vxWorks we use FIONBIO, but the others are defined for old
	 * systems, so all hell breaks loose if we leave them defined
	 */
#undef O_NONBLOCK
#undef FNDELAY
#undef O_NDELAY
#endif

#if defined(O_NONBLOCK) /* POSIX */
	if (fcntl(fd, F_SETFL, O_NONBLOCK) < 0) {
		msyslog(LOG_ERR,
			"fcntl(O_NONBLOCK) fails on fd #%d: %m", fd);
		exit(1);
	}
#elif defined(FNDELAY)
	if (fcntl(fd, F_SETFL, FNDELAY) < 0) {
		msyslog(LOG_ERR, "fcntl(FNDELAY) fails on fd #%d: %m",
			fd);
		exit(1);
	}
#elif defined(O_NDELAY) /* generally the same as FNDELAY */
	if (fcntl(fd, F_SETFL, O_NDELAY) < 0) {
		msyslog(LOG_ERR, "fcntl(O_NDELAY) fails on fd #%d: %m",
			fd);
		exit(1);
	}
#elif defined(FIONBIO)
	{
		int on = 1;

		if (ioctl(fd, FIONBIO, &on) < 0) {
			msyslog(LOG_ERR,
				"ioctl(FIONBIO) fails on fd #%d: %m",
				fd);
			exit(1);
		}
	}
#elif defined(FIOSNBIO)
	if (ioctl(fd, FIOSNBIO, &on) < 0) {
		msyslog(LOG_ERR,
			"ioctl(FIOSNBIO) fails on fd #%d: %m", fd);
		exit(1);
	}
#else
# include "Bletch: Need non-blocking I/O!"
#endif
}

#if 0

/* The following subroutines should probably be moved here */

static SOCKET
open_socket(
	sockaddr_u *	addr,
	int		bcast,
	int		turn_off_reuse,
	endpt *		interf
	)
void
sendpkt(
	sockaddr_u *		dest,
	struct interface *	ep,
	int			ttl,
	struct pkt *		pkt,
	int			len
	)

static inline int
read_refclock_packet(SOCKET fd, struct refclockio *rp, l_fp ts)

static inline int
read_network_packet(
	SOCKET			fd,
	struct interface *	itf,
	l_fp			ts
	)

void
kill_asyncio(int startfd)

#endif /* 0 */
