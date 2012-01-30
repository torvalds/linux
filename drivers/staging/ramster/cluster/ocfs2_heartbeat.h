/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * ocfs2_heartbeat.h
 *
 * On-disk structures for ocfs2_heartbeat
 *
 * Copyright (C) 2002, 2004 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 */

#ifndef _OCFS2_HEARTBEAT_H
#define _OCFS2_HEARTBEAT_H

struct o2hb_disk_heartbeat_block {
	__le64 hb_seq;
	__u8  hb_node;
	__u8  hb_pad1[3];
	__le32 hb_cksum;
	__le64 hb_generation;
	__le32 hb_dead_ms;
};

#endif /* _OCFS2_HEARTBEAT_H */
