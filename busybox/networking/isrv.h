/* vi: set sw=4 ts=4: */
/*
 * Generic non-forking server infrastructure.
 * Intended to make writing telnetd-type servers easier.
 *
 * Copyright (C) 2007 Denys Vlasenko
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */

PUSH_AND_SET_FUNCTION_VISIBILITY_TO_HIDDEN

/* opaque structure */
struct isrv_state_t;
typedef struct isrv_state_t isrv_state_t;

/* callbacks */
void isrv_want_rd(isrv_state_t *state, int fd);
void isrv_want_wr(isrv_state_t *state, int fd);
void isrv_dont_want_rd(isrv_state_t *state, int fd);
void isrv_dont_want_wr(isrv_state_t *state, int fd);
int isrv_register_fd(isrv_state_t *state, int peer, int fd);
void isrv_close_fd(isrv_state_t *state, int fd);
int isrv_register_peer(isrv_state_t *state, void *param);

/* Driver:
 *
 * Select on listen_fd for <linger_timeout> (or forever if 0).
 *
 * If we time out and we have no peers, exit.
 * If we have peers, call do_timeout(peer_param),
 * if it returns !0, peer is removed.
 *
 * If listen_fd is active, accept new connection ("peer"),
 * call new_peer() on it, and if it returns 0,
 * add it to fds to select on.
 * Now, select will wait for <timeout>, not <linger_timeout>
 * (as long as we have more than zero peers).
 *
 * If a peer's fd is active, we call do_rd() on it if read
 * bit was set, and then do_wr() if write bit was also set.
 * If either returns !0, peer is removed.
 * Reaching this place also resets timeout counter for this peer.
 *
 * Note that peer must indicate that he wants to be selected
 * for read and/or write using isrv_want_rd()/isrv_want_wr()
 * [can be called in new_peer() or in do_rd()/do_wr()].
 * If it never wants to be selected for write, do_wr()
 * will never be called (can be NULL).
 */
void isrv_run(
	int listen_fd,
	int (*new_peer)(isrv_state_t *state, int fd),
	int (*do_rd)(int fd, void **),
	int (*do_wr)(int fd, void **),
	int (*do_timeout)(void **),
	int timeout,
	int linger_timeout
);

POP_SAVED_FUNCTION_VISIBILITY
