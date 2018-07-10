/* vi: set sw=4 ts=4: */
/*
 * Generic non-forking server infrastructure.
 * Intended to make writing telnetd-type servers easier.
 *
 * Copyright (C) 2007 Denys Vlasenko
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */

#include "libbb.h"
#include "isrv.h"

#define DEBUG 0

#if DEBUG
#define DPRINTF(args...) bb_error_msg(args)
#else
#define DPRINTF(args...) ((void)0)
#endif

/* Helpers */

/* Opaque structure */

struct isrv_state_t {
	short  *fd2peer; /* one per registered fd */
	void  **param_tbl; /* one per registered peer */
	/* one per registered peer; doesn't exist if !timeout */
	time_t *timeo_tbl;
	int   (*new_peer)(isrv_state_t *state, int fd);
	time_t  curtime;
	int     timeout;
	int     fd_count;
	int     peer_count;
	int     wr_count;
	fd_set  rd;
	fd_set  wr;
};
#define FD2PEER    (state->fd2peer)
#define PARAM_TBL  (state->param_tbl)
#define TIMEO_TBL  (state->timeo_tbl)
#define CURTIME    (state->curtime)
#define TIMEOUT    (state->timeout)
#define FD_COUNT   (state->fd_count)
#define PEER_COUNT (state->peer_count)
#define WR_COUNT   (state->wr_count)

/* callback */
void isrv_want_rd(isrv_state_t *state, int fd)
{
	FD_SET(fd, &state->rd);
}

/* callback */
void isrv_want_wr(isrv_state_t *state, int fd)
{
	if (!FD_ISSET(fd, &state->wr)) {
		WR_COUNT++;
		FD_SET(fd, &state->wr);
	}
}

/* callback */
void isrv_dont_want_rd(isrv_state_t *state, int fd)
{
	FD_CLR(fd, &state->rd);
}

/* callback */
void isrv_dont_want_wr(isrv_state_t *state, int fd)
{
	if (FD_ISSET(fd, &state->wr)) {
		WR_COUNT--;
		FD_CLR(fd, &state->wr);
	}
}

/* callback */
int isrv_register_fd(isrv_state_t *state, int peer, int fd)
{
	int n;

	DPRINTF("register_fd(peer:%d,fd:%d)", peer, fd);

	if (FD_COUNT >= FD_SETSIZE) return -1;
	if (FD_COUNT <= fd) {
		n = FD_COUNT;
		FD_COUNT = fd + 1;

		DPRINTF("register_fd: FD_COUNT %d", FD_COUNT);

		FD2PEER = xrealloc(FD2PEER, FD_COUNT * sizeof(FD2PEER[0]));
		while (n < fd) FD2PEER[n++] = -1;
	}

	DPRINTF("register_fd: FD2PEER[%d] = %d", fd, peer);

	FD2PEER[fd] = peer;
	return 0;
}

/* callback */
void isrv_close_fd(isrv_state_t *state, int fd)
{
	DPRINTF("close_fd(%d)", fd);

	close(fd);
	isrv_dont_want_rd(state, fd);
	if (WR_COUNT) isrv_dont_want_wr(state, fd);

	FD2PEER[fd] = -1;
	if (fd == FD_COUNT-1) {
		do fd--; while (fd >= 0 && FD2PEER[fd] == -1);
		FD_COUNT = fd + 1;

		DPRINTF("close_fd: FD_COUNT %d", FD_COUNT);

		FD2PEER = xrealloc(FD2PEER, FD_COUNT * sizeof(FD2PEER[0]));
	}
}

/* callback */
int isrv_register_peer(isrv_state_t *state, void *param)
{
	int n;

	if (PEER_COUNT >= FD_SETSIZE) return -1;
	n = PEER_COUNT++;

	DPRINTF("register_peer: PEER_COUNT %d", PEER_COUNT);

	PARAM_TBL = xrealloc(PARAM_TBL, PEER_COUNT * sizeof(PARAM_TBL[0]));
	PARAM_TBL[n] = param;
	if (TIMEOUT) {
		TIMEO_TBL = xrealloc(TIMEO_TBL, PEER_COUNT * sizeof(TIMEO_TBL[0]));
		TIMEO_TBL[n] = CURTIME;
	}
	return n;
}

static void remove_peer(isrv_state_t *state, int peer)
{
	int movesize;
	int fd;

	DPRINTF("remove_peer(%d)", peer);

	fd = FD_COUNT - 1;
	while (fd >= 0) {
		if (FD2PEER[fd] == peer) {
			isrv_close_fd(state, fd);
			fd--;
			continue;
		}
		if (FD2PEER[fd] > peer)
			FD2PEER[fd]--;
		fd--;
	}

	PEER_COUNT--;
	DPRINTF("remove_peer: PEER_COUNT %d", PEER_COUNT);

	movesize = (PEER_COUNT - peer) * sizeof(void*);
	if (movesize > 0) {
		memcpy(&PARAM_TBL[peer], &PARAM_TBL[peer+1], movesize);
		if (TIMEOUT)
			memcpy(&TIMEO_TBL[peer], &TIMEO_TBL[peer+1], movesize);
	}
	PARAM_TBL = xrealloc(PARAM_TBL, PEER_COUNT * sizeof(PARAM_TBL[0]));
	if (TIMEOUT)
		TIMEO_TBL = xrealloc(TIMEO_TBL, PEER_COUNT * sizeof(TIMEO_TBL[0]));
}

static void handle_accept(isrv_state_t *state, int fd)
{
	int n, newfd;

	/* suppress gcc warning "cast from ptr to int of different size" */
	fcntl(fd, F_SETFL, (int)(ptrdiff_t)(PARAM_TBL[0]) | O_NONBLOCK);
	newfd = accept(fd, NULL, 0);
	fcntl(fd, F_SETFL, (int)(ptrdiff_t)(PARAM_TBL[0]));
	if (newfd < 0) {
		if (errno == EAGAIN) return;
		/* Most probably someone gave us wrong fd type
		 * (for example, non-socket). Don't want
		 * to loop forever. */
		bb_perror_msg_and_die("accept");
	}

	DPRINTF("new_peer(%d)", newfd);
	n = state->new_peer(state, newfd);
	if (n)
		remove_peer(state, n); /* unsuccessful peer start */
}

static void handle_fd_set(isrv_state_t *state, fd_set *fds, int (*h)(int, void **))
{
	enum { LONG_CNT = sizeof(fd_set) / sizeof(long) };
	int fds_pos;
	int fd, peer;
	/* need to know value at _the beginning_ of this routine */
	int fd_cnt = FD_COUNT;

	BUILD_BUG_ON(LONG_CNT * sizeof(long) != sizeof(fd_set));

	fds_pos = 0;
	while (1) {
		/* Find next nonzero bit */
		while (fds_pos < LONG_CNT) {
			if (((long*)fds)[fds_pos] == 0) {
				fds_pos++;
				continue;
			}
			/* Found non-zero word */
			fd = fds_pos * sizeof(long)*8; /* word# -> bit# */
			while (1) {
				if (FD_ISSET(fd, fds)) {
					FD_CLR(fd, fds);
					goto found_fd;
				}
				fd++;
			}
		}
		break; /* all words are zero */
 found_fd:
		if (fd >= fd_cnt) { /* paranoia */
			DPRINTF("handle_fd_set: fd > fd_cnt?? (%d > %d)",
					fd, fd_cnt);
			break;
		}
		DPRINTF("handle_fd_set: fd %d is active", fd);
		peer = FD2PEER[fd];
		if (peer < 0)
			continue; /* peer is already gone */
		if (peer == 0) {
			handle_accept(state, fd);
			continue;
		}
		DPRINTF("h(fd:%d)", fd);
		if (h(fd, &PARAM_TBL[peer])) {
			/* this peer is gone */
			remove_peer(state, peer);
		} else if (TIMEOUT) {
			TIMEO_TBL[peer] = monotonic_sec();
		}
	}
}

static void handle_timeout(isrv_state_t *state, int (*do_timeout)(void **))
{
	int n, peer;
	peer = PEER_COUNT-1;
	/* peer 0 is not checked */
	while (peer > 0) {
		DPRINTF("peer %d: time diff %d", peer,
				(int)(CURTIME - TIMEO_TBL[peer]));
		if ((CURTIME - TIMEO_TBL[peer]) >= TIMEOUT) {
			DPRINTF("peer %d: do_timeout()", peer);
			n = do_timeout(&PARAM_TBL[peer]);
			if (n)
				remove_peer(state, peer);
		}
		peer--;
	}
}

/* Driver */
void isrv_run(
	int listen_fd,
	int (*new_peer)(isrv_state_t *state, int fd),
	int (*do_rd)(int fd, void **),
	int (*do_wr)(int fd, void **),
	int (*do_timeout)(void **),
	int timeout,
	int linger_timeout)
{
	isrv_state_t *state = xzalloc(sizeof(*state));
	state->new_peer = new_peer;
	state->timeout  = timeout;

	/* register "peer" #0 - it will accept new connections */
	isrv_register_peer(state, NULL);
	isrv_register_fd(state, /*peer:*/ 0, listen_fd);
	isrv_want_rd(state, listen_fd);
	/* remember flags to make blocking<->nonblocking switch faster */
	/* (suppress gcc warning "cast from ptr to int of different size") */
	PARAM_TBL[0] = (void*)(ptrdiff_t)(fcntl(listen_fd, F_GETFL));

	while (1) {
		struct timeval tv;
		fd_set rd;
		fd_set wr;
		fd_set *wrp = NULL;
		int n;

		tv.tv_sec = timeout;
		if (PEER_COUNT <= 1)
			tv.tv_sec = linger_timeout;
		tv.tv_usec = 0;
		rd = state->rd;
		if (WR_COUNT) {
			wr = state->wr;
			wrp = &wr;
		}

		DPRINTF("run: select(FD_COUNT:%d,timeout:%d)...",
				FD_COUNT, (int)tv.tv_sec);
		n = select(FD_COUNT, &rd, wrp, NULL, tv.tv_sec ? &tv : NULL);
		DPRINTF("run: ...select:%d", n);

		if (n < 0) {
			if (errno != EINTR)
				bb_perror_msg("select");
			continue;
		}

		if (n == 0 && linger_timeout && PEER_COUNT <= 1)
			break;

		if (timeout) {
			time_t t = monotonic_sec();
			if (t != CURTIME) {
				CURTIME = t;
				handle_timeout(state, do_timeout);
			}
		}
		if (n > 0) {
			handle_fd_set(state, &rd, do_rd);
			if (wrp)
				handle_fd_set(state, wrp, do_wr);
		}
	}
	DPRINTF("run: bailout");
	/* NB: accept socket is not closed. Caller is to decide what to do */
}
