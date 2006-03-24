/*
 * linux/fs/9p/mux.h
 *
 * Multiplexer Definitions
 *
 *  Copyright (C) 2005 by Latchesar Ionkov <lucho@ionkov.net>
 *  Copyright (C) 2004 by Eric Van Hensbergen <ericvh@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to:
 *  Free Software Foundation
 *  51 Franklin Street, Fifth Floor
 *  Boston, MA  02111-1301  USA
 *
 */

struct v9fs_mux_data;

/**
 * v9fs_mux_req_callback - callback function that is called when the
 * response of a request is received. The callback is called from
 * a workqueue and shouldn't block.
 *
 * @a - the pointer that was specified when the request was send to be
 *      passed to the callback
 * @tc - request call
 * @rc - response call
 * @err - error code (non-zero if error occured)
 */
typedef void (*v9fs_mux_req_callback)(void *a, struct v9fs_fcall *tc,
	struct v9fs_fcall *rc, int err);

int v9fs_mux_global_init(void);
void v9fs_mux_global_exit(void);

struct v9fs_mux_data *v9fs_mux_init(struct v9fs_transport *trans, int msize,
	unsigned char *extended);
void v9fs_mux_destroy(struct v9fs_mux_data *);

int v9fs_mux_send(struct v9fs_mux_data *m, struct v9fs_fcall *tc);
struct v9fs_fcall *v9fs_mux_recv(struct v9fs_mux_data *m);
int v9fs_mux_rpc(struct v9fs_mux_data *m, struct v9fs_fcall *tc, struct v9fs_fcall **rc);

void v9fs_mux_flush(struct v9fs_mux_data *m, int sendflush);
void v9fs_mux_cancel(struct v9fs_mux_data *m, int err);
int v9fs_errstr2errno(char *errstr, int len);
