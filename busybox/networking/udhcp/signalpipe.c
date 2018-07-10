/* vi: set sw=4 ts=4: */
/*
 * Signal pipe infrastructure. A reliable way of delivering signals.
 *
 * Russ Dill <Russ.Dill@asu.edu> December 2003
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#include "common.h"

/* Global variable: we access it from signal handler */
static struct fd_pair signal_pipe;

static void signal_handler(int sig)
{
	int sv = errno;
	unsigned char ch = sig; /* use char, avoid dealing with partial writes */
	if (write(signal_pipe.wr, &ch, 1) != 1)
		bb_perror_msg("can't send signal");
	errno = sv;
}

/* Call this before doing anything else. Sets up the socket pair
 * and installs the signal handler */
void FAST_FUNC udhcp_sp_setup(void)
{
	/* was socketpair, but it needs AF_UNIX in kernel */
	xpiped_pair(signal_pipe);
	close_on_exec_on(signal_pipe.rd);
	close_on_exec_on(signal_pipe.wr);
	ndelay_on(signal_pipe.rd);
	ndelay_on(signal_pipe.wr);
	bb_signals(0
		+ (1 << SIGUSR1)
		+ (1 << SIGUSR2)
		+ (1 << SIGTERM)
		, signal_handler);
}

/* Quick little function to setup the pfds.
 * Limited in that you can only pass one extra fd.
 */
void FAST_FUNC udhcp_sp_fd_set(struct pollfd pfds[2], int extra_fd)
{
	pfds[0].fd = signal_pipe.rd;
	pfds[0].events = POLLIN;
	pfds[1].fd = -1;
	if (extra_fd >= 0) {
		close_on_exec_on(extra_fd);
		pfds[1].fd = extra_fd;
		pfds[1].events = POLLIN;
	}
	/* this simplifies "is extra_fd ready?" tests elsewhere: */
	pfds[1].revents = 0;
}

/* Read a signal from the signal pipe. Returns 0 if there is
 * no signal, -1 on error (and sets errno appropriately), and
 * your signal on success */
int FAST_FUNC udhcp_sp_read(void)
{
	unsigned char sig;

	/* Can't block here, fd is in nonblocking mode */
	if (safe_read(signal_pipe.rd, &sig, 1) != 1)
		return 0;

	return sig;
}
