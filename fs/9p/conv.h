/*
 * linux/fs/9p/conv.h
 *
 * 9P protocol conversion definitions.
 *
 *  Copyright (C) 2005 by Latchesar Ionkov <lucho@ionkov.net>
 *  Copyright (C) 2004 by Eric Van Hensbergen <ericvh@gmail.com>
 *  Copyright (C) 2002 by Ron Minnich <rminnich@lanl.gov>
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

int v9fs_deserialize_stat(void *buf, u32 buflen, struct v9fs_stat *stat,
	int extended);
int v9fs_deserialize_fcall(void *buf, u32 buflen, struct v9fs_fcall *rcall,
	int extended);

void v9fs_set_tag(struct v9fs_fcall *fc, u16 tag);

struct v9fs_fcall *v9fs_create_tversion(u32 msize, char *version);
struct v9fs_fcall *v9fs_create_tattach(u32 fid, u32 afid, char *uname,
	char *aname);
struct v9fs_fcall *v9fs_create_tflush(u16 oldtag);
struct v9fs_fcall *v9fs_create_twalk(u32 fid, u32 newfid, u16 nwname,
	char **wnames);
struct v9fs_fcall *v9fs_create_topen(u32 fid, u8 mode);
struct v9fs_fcall *v9fs_create_tcreate(u32 fid, char *name, u32 perm, u8 mode,
	char *extension, int extended);
struct v9fs_fcall *v9fs_create_tread(u32 fid, u64 offset, u32 count);
struct v9fs_fcall *v9fs_create_twrite(u32 fid, u64 offset, u32 count,
	const char __user *data);
struct v9fs_fcall *v9fs_create_tclunk(u32 fid);
struct v9fs_fcall *v9fs_create_tremove(u32 fid);
struct v9fs_fcall *v9fs_create_tstat(u32 fid);
struct v9fs_fcall *v9fs_create_twstat(u32 fid, struct v9fs_wstat *wstat,
	int extended);
