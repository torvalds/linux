/*
 * linux/fs/9p/mux.h
 *
 * Multiplexer Definitions
 *
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

/* structure to manage each RPC transaction */

struct v9fs_rpcreq {
	struct v9fs_fcall *tcall;
	struct v9fs_fcall *rcall;
	int err;	/* error code if response failed */

	/* XXX - could we put scatter/gather buffers here? */

	struct list_head next;
};

int v9fs_mux_init(struct v9fs_session_info *v9ses, const char *dev_name);
long v9fs_mux_rpc(struct v9fs_session_info *v9ses,
		  struct v9fs_fcall *tcall, struct v9fs_fcall **rcall);
void v9fs_mux_cancel_requests(struct v9fs_session_info *v9ses, int err);
