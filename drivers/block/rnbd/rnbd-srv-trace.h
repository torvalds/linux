/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * RDMA Network Block Driver
 *
 * Copyright (c) 2022 1&1 IONOS SE. All rights reserved.
 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM rnbd_srv

#if !defined(_TRACE_RNBD_SRV_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_RNBD_SRV_H

#include <linux/tracepoint.h>

struct rnbd_srv_session;
struct rtrs_srv_op;

DECLARE_EVENT_CLASS(rnbd_srv_link_class,
	TP_PROTO(struct rnbd_srv_session *srv),

	TP_ARGS(srv),

	TP_STRUCT__entry(
		__field(int, qdepth)
		__string(sessname, srv->sessname)
	),

	TP_fast_assign(
		__entry->qdepth = srv->queue_depth;
		__assign_str(sessname);
	),

	TP_printk("sessname: %s qdepth: %d",
		   __get_str(sessname),
		   __entry->qdepth
	)
);

#define DEFINE_LINK_EVENT(name) \
DEFINE_EVENT(rnbd_srv_link_class, name, \
	TP_PROTO(struct rnbd_srv_session *srv), \
	TP_ARGS(srv))

DEFINE_LINK_EVENT(create_sess);
DEFINE_LINK_EVENT(destroy_sess);

TRACE_DEFINE_ENUM(RNBD_OP_READ);
TRACE_DEFINE_ENUM(RNBD_OP_WRITE);
TRACE_DEFINE_ENUM(RNBD_OP_FLUSH);
TRACE_DEFINE_ENUM(RNBD_OP_DISCARD);
TRACE_DEFINE_ENUM(RNBD_OP_SECURE_ERASE);
TRACE_DEFINE_ENUM(RNBD_F_SYNC);
TRACE_DEFINE_ENUM(RNBD_F_FUA);

#define show_rnbd_rw_flags(x) \
	__print_flags(x, "|", \
		{ RNBD_OP_READ,		"READ" }, \
		{ RNBD_OP_WRITE,	"WRITE" }, \
		{ RNBD_OP_FLUSH,	"FLUSH" }, \
		{ RNBD_OP_DISCARD,	"DISCARD" }, \
		{ RNBD_OP_SECURE_ERASE,	"SECURE_ERASE" }, \
		{ RNBD_F_SYNC,		"SYNC" }, \
		{ RNBD_F_FUA,		"FUA" })

TRACE_EVENT(process_rdma,
	TP_PROTO(struct rnbd_srv_session *srv,
		 const struct rnbd_msg_io *msg,
		 struct rtrs_srv_op *id,
		 u32 datalen,
		 size_t usrlen),

	TP_ARGS(srv, msg, id, datalen, usrlen),

	TP_STRUCT__entry(
		__string(sessname, srv->sessname)
		__field(u8, dir)
		__field(u8, ver)
		__field(u32, device_id)
		__field(u64, sector)
		__field(u32, flags)
		__field(u32, bi_size)
		__field(u16, ioprio)
		__field(u32, datalen)
		__field(size_t, usrlen)
	),

	TP_fast_assign(
		__assign_str(sessname);
		__entry->dir = id->dir;
		__entry->ver = srv->ver;
		__entry->device_id = le32_to_cpu(msg->device_id);
		__entry->sector = le64_to_cpu(msg->sector);
		__entry->bi_size = le32_to_cpu(msg->bi_size);
		__entry->flags = le32_to_cpu(msg->rw);
		__entry->ioprio = le16_to_cpu(msg->prio);
		__entry->datalen = datalen;
		__entry->usrlen = usrlen;
	),

	TP_printk("I/O req: sess: %s, type: %s, ver: %d, devid: %u, sector: %llu, bsize: %u, flags: %s, ioprio: %d, datalen: %u, usrlen: %zu",
		   __get_str(sessname),
		   __print_symbolic(__entry->dir,
			 { READ,  "READ" },
			 { WRITE, "WRITE" }),
		   __entry->ver,
		   __entry->device_id,
		   __entry->sector,
		   __entry->bi_size,
		   show_rnbd_rw_flags(__entry->flags),
		   __entry->ioprio,
		   __entry->datalen,
		   __entry->usrlen
	)
);

TRACE_EVENT(process_msg_sess_info,
	TP_PROTO(struct rnbd_srv_session *srv,
		 const struct rnbd_msg_sess_info *msg),

	TP_ARGS(srv, msg),

	TP_STRUCT__entry(
		__field(u8, proto_ver)
		__field(u8, clt_ver)
		__field(u8, srv_ver)
		__string(sessname, srv->sessname)
	),

	TP_fast_assign(
		__entry->proto_ver = srv->ver;
		__entry->clt_ver = msg->ver;
		__entry->srv_ver = RNBD_PROTO_VER_MAJOR;
		__assign_str(sessname);
	),

	TP_printk("Session %s using proto-ver %d (clt-ver: %d, srv-ver: %d)",
		   __get_str(sessname),
		   __entry->proto_ver,
		   __entry->clt_ver,
		   __entry->srv_ver
	)
);

TRACE_DEFINE_ENUM(RNBD_ACCESS_RO);
TRACE_DEFINE_ENUM(RNBD_ACCESS_RW);
TRACE_DEFINE_ENUM(RNBD_ACCESS_MIGRATION);

#define show_rnbd_access_mode(x) \
	__print_symbolic(x, \
		{ RNBD_ACCESS_RO,		"RO" }, \
		{ RNBD_ACCESS_RW,		"RW" }, \
		{ RNBD_ACCESS_MIGRATION,	"MIGRATION" })

TRACE_EVENT(process_msg_open,
	TP_PROTO(struct rnbd_srv_session *srv,
		 const struct rnbd_msg_open *msg),

	TP_ARGS(srv, msg),

	TP_STRUCT__entry(
		__field(u8, access_mode)
		__string(sessname, srv->sessname)
		__string(dev_name, msg->dev_name)
	),

	TP_fast_assign(
		__entry->access_mode = msg->access_mode;
		__assign_str(sessname);
		__assign_str(dev_name);
	),

	TP_printk("Open message received: session='%s' path='%s' access_mode=%s",
		   __get_str(sessname),
		   __get_str(dev_name),
		   show_rnbd_access_mode(__entry->access_mode)
	)
);

TRACE_EVENT(process_msg_close,
	TP_PROTO(struct rnbd_srv_session *srv,
		 const struct rnbd_msg_close *msg),

	TP_ARGS(srv, msg),

	TP_STRUCT__entry(
		__field(u32, device_id)
		__string(sessname, srv->sessname)
	),

	TP_fast_assign(
		__entry->device_id = le32_to_cpu(msg->device_id);
		__assign_str(sessname);
	),

	TP_printk("Close message received: session='%s' device id='%d'",
		   __get_str(sessname),
		   __entry->device_id
	)
);

#endif /* _TRACE_RNBD_SRV_H */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#define TRACE_INCLUDE_FILE rnbd-srv-trace
#include <trace/define_trace.h>

