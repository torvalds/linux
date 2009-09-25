/*
   drbd_tracing.h

   This file is part of DRBD by Philipp Reisner and Lars Ellenberg.

   Copyright (C) 2003-2008, LINBIT Information Technologies GmbH.
   Copyright (C) 2003-2008, Philipp Reisner <philipp.reisner@linbit.com>.
   Copyright (C) 2003-2008, Lars Ellenberg <lars.ellenberg@linbit.com>.

   drbd is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   drbd is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with drbd; see the file COPYING.  If not, write to
   the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.

 */

#ifndef DRBD_TRACING_H
#define DRBD_TRACING_H

#include <linux/tracepoint.h>
#include "drbd_int.h"
#include "drbd_req.h"

enum {
	TRACE_LVL_ALWAYS = 0,
	TRACE_LVL_SUMMARY,
	TRACE_LVL_METRICS,
	TRACE_LVL_ALL,
	TRACE_LVL_MAX
};

DECLARE_TRACE(drbd_unplug,
	TP_PROTO(struct drbd_conf *mdev, char* msg),
	TP_ARGS(mdev, msg));

DECLARE_TRACE(drbd_uuid,
	TP_PROTO(struct drbd_conf *mdev, enum drbd_uuid_index index),
	TP_ARGS(mdev, index));

DECLARE_TRACE(drbd_ee,
	TP_PROTO(struct drbd_conf *mdev, struct drbd_epoch_entry *e, char* msg),
	TP_ARGS(mdev, e, msg));

DECLARE_TRACE(drbd_md_io,
	TP_PROTO(struct drbd_conf *mdev, int rw, struct drbd_backing_dev *bdev),
	TP_ARGS(mdev, rw, bdev));

DECLARE_TRACE(drbd_epoch,
	TP_PROTO(struct drbd_conf *mdev, struct drbd_epoch *epoch, enum epoch_event ev),
	TP_ARGS(mdev, epoch, ev));

DECLARE_TRACE(drbd_netlink,
	TP_PROTO(void *data, int is_req),
	TP_ARGS(data, is_req));

DECLARE_TRACE(drbd_actlog,
	TP_PROTO(struct drbd_conf *mdev, sector_t sector, char* msg),
	TP_ARGS(mdev, sector, msg));

DECLARE_TRACE(drbd_bio,
	TP_PROTO(struct drbd_conf *mdev, const char *pfx, struct bio *bio, int complete,
		 struct drbd_request *r),
	TP_ARGS(mdev, pfx, bio, complete, r));

DECLARE_TRACE(drbd_req,
	TP_PROTO(struct drbd_request *req, enum drbd_req_event what, char *msg),
	      TP_ARGS(req, what, msg));

DECLARE_TRACE(drbd_packet,
	TP_PROTO(struct drbd_conf *mdev, struct socket *sock,
		 int recv, union p_polymorph *p, char *file, int line),
	TP_ARGS(mdev, sock, recv, p, file, line));

DECLARE_TRACE(_drbd_resync,
	TP_PROTO(struct drbd_conf *mdev, int level, const char *fmt, va_list args),
	TP_ARGS(mdev, level, fmt, args));

#endif
