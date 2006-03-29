/*
 * linux/fs/9p/transport.h
 *
 * Transport Definition
 *
 *  Copyright (C) 2005 by Latchesar Ionkov <lucho@ionkov.net>
 *  Copyright (C) 2004 by Eric Van Hensbergen <ericvh@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2
 *  as published by the Free Software Foundation.
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

enum v9fs_transport_status {
	Connected,
	Disconnected,
	Hung,
};

struct v9fs_transport {
	enum v9fs_transport_status status;
	void *priv;

	int (*init) (struct v9fs_session_info *, const char *, char *);
	int (*write) (struct v9fs_transport *, void *, int);
	int (*read) (struct v9fs_transport *, void *, int);
	void (*close) (struct v9fs_transport *);
	unsigned int (*poll)(struct v9fs_transport *, struct poll_table_struct *);
};

extern struct v9fs_transport v9fs_trans_tcp;
extern struct v9fs_transport v9fs_trans_unix;
extern struct v9fs_transport v9fs_trans_fd;
