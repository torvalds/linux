/*
 * linux/fs/9p/conv.h
 *
 * 9P protocol conversion definitions
 *
 *  Copyright (C) 2004 by Eric Van Hensbergen <ericvh@gmail.com>
 *  Copyright (C) 2002 by Ron Minnich <rminnich@lanl.gov>
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

int v9fs_deserialize_stat(struct v9fs_session_info *, void *buf,
			  u32 buflen, struct v9fs_stat *stat, u32 statlen);
int v9fs_serialize_fcall(struct v9fs_session_info *, struct v9fs_fcall *tcall,
			 void *buf, u32 buflen);
int v9fs_deserialize_fcall(struct v9fs_session_info *, u32 msglen,
			   void *buf, u32 buflen, struct v9fs_fcall *rcall,
			   int rcalllen);

/* this one is actually in error.c right now */
int v9fs_errstr2errno(char *errstr);
