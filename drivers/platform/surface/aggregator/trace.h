/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Trace points for SSAM/SSH.
 *
 * Copyright (C) 2020-2022 Maximilian Luz <luzmaximilian@gmail.com>
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM surface_aggregator

#if !defined(_SURFACE_AGGREGATOR_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _SURFACE_AGGREGATOR_TRACE_H

#include <linux/surface_aggregator/serial_hub.h>

#include <asm/unaligned.h>
#include <linux/tracepoint.h>

TRACE_DEFINE_ENUM(SSH_FRAME_TYPE_DATA_SEQ);
TRACE_DEFINE_ENUM(SSH_FRAME_TYPE_DATA_NSQ);
TRACE_DEFINE_ENUM(SSH_FRAME_TYPE_ACK);
TRACE_DEFINE_ENUM(SSH_FRAME_TYPE_NAK);

TRACE_DEFINE_ENUM(SSH_PACKET_SF_LOCKED_BIT);
TRACE_DEFINE_ENUM(SSH_PACKET_SF_QUEUED_BIT);
TRACE_DEFINE_ENUM(SSH_PACKET_SF_PENDING_BIT);
TRACE_DEFINE_ENUM(SSH_PACKET_SF_TRANSMITTING_BIT);
TRACE_DEFINE_ENUM(SSH_PACKET_SF_TRANSMITTED_BIT);
TRACE_DEFINE_ENUM(SSH_PACKET_SF_ACKED_BIT);
TRACE_DEFINE_ENUM(SSH_PACKET_SF_CANCELED_BIT);
TRACE_DEFINE_ENUM(SSH_PACKET_SF_COMPLETED_BIT);

TRACE_DEFINE_ENUM(SSH_PACKET_TY_FLUSH_BIT);
TRACE_DEFINE_ENUM(SSH_PACKET_TY_SEQUENCED_BIT);
TRACE_DEFINE_ENUM(SSH_PACKET_TY_BLOCKING_BIT);

TRACE_DEFINE_ENUM(SSH_PACKET_FLAGS_SF_MASK);
TRACE_DEFINE_ENUM(SSH_PACKET_FLAGS_TY_MASK);

TRACE_DEFINE_ENUM(SSH_REQUEST_SF_LOCKED_BIT);
TRACE_DEFINE_ENUM(SSH_REQUEST_SF_QUEUED_BIT);
TRACE_DEFINE_ENUM(SSH_REQUEST_SF_PENDING_BIT);
TRACE_DEFINE_ENUM(SSH_REQUEST_SF_TRANSMITTING_BIT);
TRACE_DEFINE_ENUM(SSH_REQUEST_SF_TRANSMITTED_BIT);
TRACE_DEFINE_ENUM(SSH_REQUEST_SF_RSPRCVD_BIT);
TRACE_DEFINE_ENUM(SSH_REQUEST_SF_CANCELED_BIT);
TRACE_DEFINE_ENUM(SSH_REQUEST_SF_COMPLETED_BIT);

TRACE_DEFINE_ENUM(SSH_REQUEST_TY_FLUSH_BIT);
TRACE_DEFINE_ENUM(SSH_REQUEST_TY_HAS_RESPONSE_BIT);

TRACE_DEFINE_ENUM(SSH_REQUEST_FLAGS_SF_MASK);
TRACE_DEFINE_ENUM(SSH_REQUEST_FLAGS_TY_MASK);

TRACE_DEFINE_ENUM(SSAM_SSH_TC_SAM);
TRACE_DEFINE_ENUM(SSAM_SSH_TC_BAT);
TRACE_DEFINE_ENUM(SSAM_SSH_TC_TMP);
TRACE_DEFINE_ENUM(SSAM_SSH_TC_PMC);
TRACE_DEFINE_ENUM(SSAM_SSH_TC_FAN);
TRACE_DEFINE_ENUM(SSAM_SSH_TC_PoM);
TRACE_DEFINE_ENUM(SSAM_SSH_TC_DBG);
TRACE_DEFINE_ENUM(SSAM_SSH_TC_KBD);
TRACE_DEFINE_ENUM(SSAM_SSH_TC_FWU);
TRACE_DEFINE_ENUM(SSAM_SSH_TC_UNI);
TRACE_DEFINE_ENUM(SSAM_SSH_TC_LPC);
TRACE_DEFINE_ENUM(SSAM_SSH_TC_TCL);
TRACE_DEFINE_ENUM(SSAM_SSH_TC_SFL);
TRACE_DEFINE_ENUM(SSAM_SSH_TC_KIP);
TRACE_DEFINE_ENUM(SSAM_SSH_TC_EXT);
TRACE_DEFINE_ENUM(SSAM_SSH_TC_BLD);
TRACE_DEFINE_ENUM(SSAM_SSH_TC_BAS);
TRACE_DEFINE_ENUM(SSAM_SSH_TC_SEN);
TRACE_DEFINE_ENUM(SSAM_SSH_TC_SRQ);
TRACE_DEFINE_ENUM(SSAM_SSH_TC_MCU);
TRACE_DEFINE_ENUM(SSAM_SSH_TC_HID);
TRACE_DEFINE_ENUM(SSAM_SSH_TC_TCH);
TRACE_DEFINE_ENUM(SSAM_SSH_TC_BKL);
TRACE_DEFINE_ENUM(SSAM_SSH_TC_TAM);
TRACE_DEFINE_ENUM(SSAM_SSH_TC_ACC0);
TRACE_DEFINE_ENUM(SSAM_SSH_TC_UFI);
TRACE_DEFINE_ENUM(SSAM_SSH_TC_USC);
TRACE_DEFINE_ENUM(SSAM_SSH_TC_PEN);
TRACE_DEFINE_ENUM(SSAM_SSH_TC_VID);
TRACE_DEFINE_ENUM(SSAM_SSH_TC_AUD);
TRACE_DEFINE_ENUM(SSAM_SSH_TC_SMC);
TRACE_DEFINE_ENUM(SSAM_SSH_TC_KPD);
TRACE_DEFINE_ENUM(SSAM_SSH_TC_REG);
TRACE_DEFINE_ENUM(SSAM_SSH_TC_SPT);
TRACE_DEFINE_ENUM(SSAM_SSH_TC_SYS);
TRACE_DEFINE_ENUM(SSAM_SSH_TC_ACC1);
TRACE_DEFINE_ENUM(SSAM_SSH_TC_SHB);
TRACE_DEFINE_ENUM(SSAM_SSH_TC_POS);

#define SSAM_PTR_UID_LEN		9
#define SSAM_U8_FIELD_NOT_APPLICABLE	((u16)-1)
#define SSAM_SEQ_NOT_APPLICABLE		((u16)-1)
#define SSAM_RQID_NOT_APPLICABLE	((u32)-1)
#define SSAM_SSH_TC_NOT_APPLICABLE	0
#define SSAM_SSH_TID_NOT_APPLICABLE	((u8)-1)

#ifndef _SURFACE_AGGREGATOR_TRACE_HELPERS
#define _SURFACE_AGGREGATOR_TRACE_HELPERS

/**
 * ssam_trace_ptr_uid() - Convert the pointer to a non-pointer UID string.
 * @ptr: The pointer to convert.
 * @uid_str: A buffer of length SSAM_PTR_UID_LEN where the UID will be stored.
 *
 * Converts the given pointer into a UID string that is safe to be shared
 * with userspace and logs, i.e. doesn't give away the real memory location.
 */
static inline void ssam_trace_ptr_uid(const void *ptr, char *uid_str)
{
	char buf[2 * sizeof(void *) + 1];

	BUILD_BUG_ON(ARRAY_SIZE(buf) < SSAM_PTR_UID_LEN);

	snprintf(buf, ARRAY_SIZE(buf), "%p", ptr);
	memcpy(uid_str, &buf[ARRAY_SIZE(buf) - SSAM_PTR_UID_LEN],
	       SSAM_PTR_UID_LEN);
}

/**
 * ssam_trace_get_packet_seq() - Read the packet's sequence ID.
 * @p: The packet.
 *
 * Return: Returns the packet's sequence ID (SEQ) field if present, or
 * %SSAM_SEQ_NOT_APPLICABLE if not (e.g. flush packet).
 */
static inline u16 ssam_trace_get_packet_seq(const struct ssh_packet *p)
{
	if (!p->data.ptr || p->data.len < SSH_MESSAGE_LENGTH(0))
		return SSAM_SEQ_NOT_APPLICABLE;

	return p->data.ptr[SSH_MSGOFFSET_FRAME(seq)];
}

/**
 * ssam_trace_get_request_id() - Read the packet's request ID.
 * @p: The packet.
 *
 * Return: Returns the packet's request ID (RQID) field if the packet
 * represents a request with command data, or %SSAM_RQID_NOT_APPLICABLE if not
 * (e.g. flush request, control packet).
 */
static inline u32 ssam_trace_get_request_id(const struct ssh_packet *p)
{
	if (!p->data.ptr || p->data.len < SSH_COMMAND_MESSAGE_LENGTH(0))
		return SSAM_RQID_NOT_APPLICABLE;

	return get_unaligned_le16(&p->data.ptr[SSH_MSGOFFSET_COMMAND(rqid)]);
}

/**
 * ssam_trace_get_request_tid() - Read the packet's request target ID.
 * @p: The packet.
 *
 * Return: Returns the packet's request target ID (TID) field if the packet
 * represents a request with command data, or %SSAM_SSH_TID_NOT_APPLICABLE
 * if not (e.g. flush request, control packet).
 */
static inline u32 ssam_trace_get_request_tid(const struct ssh_packet *p)
{
	if (!p->data.ptr || p->data.len < SSH_COMMAND_MESSAGE_LENGTH(0))
		return SSAM_SSH_TID_NOT_APPLICABLE;

	return get_unaligned_le16(&p->data.ptr[SSH_MSGOFFSET_COMMAND(tid)]);
}

/**
 * ssam_trace_get_request_sid() - Read the packet's request source ID.
 * @p: The packet.
 *
 * Return: Returns the packet's request source ID (SID) field if the packet
 * represents a request with command data, or %SSAM_SSH_TID_NOT_APPLICABLE
 * if not (e.g. flush request, control packet).
 */
static inline u32 ssam_trace_get_request_sid(const struct ssh_packet *p)
{
	if (!p->data.ptr || p->data.len < SSH_COMMAND_MESSAGE_LENGTH(0))
		return SSAM_SSH_TID_NOT_APPLICABLE;

	return get_unaligned_le16(&p->data.ptr[SSH_MSGOFFSET_COMMAND(sid)]);
}

/**
 * ssam_trace_get_request_tc() - Read the packet's request target category.
 * @p: The packet.
 *
 * Return: Returns the packet's request target category (TC) field if the
 * packet represents a request with command data, or %SSAM_SSH_TC_NOT_APPLICABLE
 * if not (e.g. flush request, control packet).
 */
static inline u32 ssam_trace_get_request_tc(const struct ssh_packet *p)
{
	if (!p->data.ptr || p->data.len < SSH_COMMAND_MESSAGE_LENGTH(0))
		return SSAM_SSH_TC_NOT_APPLICABLE;

	return get_unaligned_le16(&p->data.ptr[SSH_MSGOFFSET_COMMAND(tc)]);
}

#endif /* _SURFACE_AGGREGATOR_TRACE_HELPERS */

#define ssam_trace_get_command_field_u8(packet, field) \
	((!(packet) || (packet)->data.len < SSH_COMMAND_MESSAGE_LENGTH(0)) \
	 ? 0 : (packet)->data.ptr[SSH_MSGOFFSET_COMMAND(field)])

#define ssam_show_generic_u8_field(value)				\
	__print_symbolic(value,						\
		{ SSAM_U8_FIELD_NOT_APPLICABLE,		"N/A" }		\
	)

#define ssam_show_frame_type(ty)					\
	__print_symbolic(ty,						\
		{ SSH_FRAME_TYPE_DATA_SEQ,		"DSEQ" },	\
		{ SSH_FRAME_TYPE_DATA_NSQ,		"DNSQ" },	\
		{ SSH_FRAME_TYPE_ACK,			"ACK"  },	\
		{ SSH_FRAME_TYPE_NAK,			"NAK"  }	\
	)

#define ssam_show_packet_type(type)					\
	__print_flags(flags & SSH_PACKET_FLAGS_TY_MASK, "",		\
		{ BIT(SSH_PACKET_TY_FLUSH_BIT),		"F" },		\
		{ BIT(SSH_PACKET_TY_SEQUENCED_BIT),	"S" },		\
		{ BIT(SSH_PACKET_TY_BLOCKING_BIT),	"B" }		\
	)

#define ssam_show_packet_state(state)					\
	__print_flags(flags & SSH_PACKET_FLAGS_SF_MASK, "",		\
		{ BIT(SSH_PACKET_SF_LOCKED_BIT),	"L" },		\
		{ BIT(SSH_PACKET_SF_QUEUED_BIT),	"Q" },		\
		{ BIT(SSH_PACKET_SF_PENDING_BIT),	"P" },		\
		{ BIT(SSH_PACKET_SF_TRANSMITTING_BIT),	"S" },		\
		{ BIT(SSH_PACKET_SF_TRANSMITTED_BIT),	"T" },		\
		{ BIT(SSH_PACKET_SF_ACKED_BIT),		"A" },		\
		{ BIT(SSH_PACKET_SF_CANCELED_BIT),	"C" },		\
		{ BIT(SSH_PACKET_SF_COMPLETED_BIT),	"F" }		\
	)

#define ssam_show_packet_seq(seq)					\
	__print_symbolic(seq,						\
		{ SSAM_SEQ_NOT_APPLICABLE,		"N/A" }		\
	)

#define ssam_show_request_type(flags)					\
	__print_flags((flags) & SSH_REQUEST_FLAGS_TY_MASK, "",		\
		{ BIT(SSH_REQUEST_TY_FLUSH_BIT),	"F" },		\
		{ BIT(SSH_REQUEST_TY_HAS_RESPONSE_BIT),	"R" }		\
	)

#define ssam_show_request_state(flags)					\
	__print_flags((flags) & SSH_REQUEST_FLAGS_SF_MASK, "",		\
		{ BIT(SSH_REQUEST_SF_LOCKED_BIT),	"L" },		\
		{ BIT(SSH_REQUEST_SF_QUEUED_BIT),	"Q" },		\
		{ BIT(SSH_REQUEST_SF_PENDING_BIT),	"P" },		\
		{ BIT(SSH_REQUEST_SF_TRANSMITTING_BIT),	"S" },		\
		{ BIT(SSH_REQUEST_SF_TRANSMITTED_BIT),	"T" },		\
		{ BIT(SSH_REQUEST_SF_RSPRCVD_BIT),	"A" },		\
		{ BIT(SSH_REQUEST_SF_CANCELED_BIT),	"C" },		\
		{ BIT(SSH_REQUEST_SF_COMPLETED_BIT),	"F" }		\
	)

#define ssam_show_request_id(rqid)					\
	__print_symbolic(rqid,						\
		{ SSAM_RQID_NOT_APPLICABLE,		"N/A" }		\
	)

#define ssam_show_ssh_tid(tid)						\
	__print_symbolic(tid,						\
		{ SSAM_SSH_TID_NOT_APPLICABLE,		"N/A"      },	\
		{ SSAM_SSH_TID_HOST,			"Host"     },	\
		{ SSAM_SSH_TID_SAM,			"SAM"      },	\
		{ SSAM_SSH_TID_KIP,			"KIP"      },	\
		{ SSAM_SSH_TID_DEBUG,			"Debug"    },	\
		{ SSAM_SSH_TID_SURFLINK,		"SurfLink" }	\
	)

#define ssam_show_ssh_tc(tc)						\
	__print_symbolic(tc,						\
		{ SSAM_SSH_TC_NOT_APPLICABLE,		"N/A"  },	\
		{ SSAM_SSH_TC_SAM,			"SAM"  },	\
		{ SSAM_SSH_TC_BAT,			"BAT"  },	\
		{ SSAM_SSH_TC_TMP,			"TMP"  },	\
		{ SSAM_SSH_TC_PMC,			"PMC"  },	\
		{ SSAM_SSH_TC_FAN,			"FAN"  },	\
		{ SSAM_SSH_TC_PoM,			"PoM"  },	\
		{ SSAM_SSH_TC_DBG,			"DBG"  },	\
		{ SSAM_SSH_TC_KBD,			"KBD"  },	\
		{ SSAM_SSH_TC_FWU,			"FWU"  },	\
		{ SSAM_SSH_TC_UNI,			"UNI"  },	\
		{ SSAM_SSH_TC_LPC,			"LPC"  },	\
		{ SSAM_SSH_TC_TCL,			"TCL"  },	\
		{ SSAM_SSH_TC_SFL,			"SFL"  },	\
		{ SSAM_SSH_TC_KIP,			"KIP"  },	\
		{ SSAM_SSH_TC_EXT,			"EXT"  },	\
		{ SSAM_SSH_TC_BLD,			"BLD"  },	\
		{ SSAM_SSH_TC_BAS,			"BAS"  },	\
		{ SSAM_SSH_TC_SEN,			"SEN"  },	\
		{ SSAM_SSH_TC_SRQ,			"SRQ"  },	\
		{ SSAM_SSH_TC_MCU,			"MCU"  },	\
		{ SSAM_SSH_TC_HID,			"HID"  },	\
		{ SSAM_SSH_TC_TCH,			"TCH"  },	\
		{ SSAM_SSH_TC_BKL,			"BKL"  },	\
		{ SSAM_SSH_TC_TAM,			"TAM"  },	\
		{ SSAM_SSH_TC_ACC0,			"ACC0" },	\
		{ SSAM_SSH_TC_UFI,			"UFI"  },	\
		{ SSAM_SSH_TC_USC,			"USC"  },	\
		{ SSAM_SSH_TC_PEN,			"PEN"  },	\
		{ SSAM_SSH_TC_VID,			"VID"  },	\
		{ SSAM_SSH_TC_AUD,			"AUD"  },	\
		{ SSAM_SSH_TC_SMC,			"SMC"  },	\
		{ SSAM_SSH_TC_KPD,			"KPD"  },	\
		{ SSAM_SSH_TC_REG,			"REG"  },	\
		{ SSAM_SSH_TC_SPT,			"SPT"  },	\
		{ SSAM_SSH_TC_SYS,			"SYS"  },	\
		{ SSAM_SSH_TC_ACC1,			"ACC1" },	\
		{ SSAM_SSH_TC_SHB,			"SMB"  },	\
		{ SSAM_SSH_TC_POS,			"POS"  }	\
	)

DECLARE_EVENT_CLASS(ssam_frame_class,
	TP_PROTO(const struct ssh_frame *frame),

	TP_ARGS(frame),

	TP_STRUCT__entry(
		__field(u8, type)
		__field(u8, seq)
		__field(u16, len)
	),

	TP_fast_assign(
		__entry->type = frame->type;
		__entry->seq = frame->seq;
		__entry->len = get_unaligned_le16(&frame->len);
	),

	TP_printk("ty=%s, seq=%#04x, len=%u",
		ssam_show_frame_type(__entry->type),
		__entry->seq,
		__entry->len
	)
);

#define DEFINE_SSAM_FRAME_EVENT(name)				\
	DEFINE_EVENT(ssam_frame_class, ssam_##name,		\
		TP_PROTO(const struct ssh_frame *frame),	\
		TP_ARGS(frame)					\
	)

DECLARE_EVENT_CLASS(ssam_command_class,
	TP_PROTO(const struct ssh_command *cmd, u16 len),

	TP_ARGS(cmd, len),

	TP_STRUCT__entry(
		__field(u16, rqid)
		__field(u16, len)
		__field(u8, tid)
		__field(u8, sid)
		__field(u8, tc)
		__field(u8, cid)
		__field(u8, iid)
	),

	TP_fast_assign(
		__entry->rqid = get_unaligned_le16(&cmd->rqid);
		__entry->tid = cmd->tid;
		__entry->sid = cmd->sid;
		__entry->tc = cmd->tc;
		__entry->cid = cmd->cid;
		__entry->iid = cmd->iid;
		__entry->len = len;
	),

	TP_printk("rqid=%#06x, tid=%s, sid=%s, tc=%s, cid=%#04x, iid=%#04x, len=%u",
		__entry->rqid,
		ssam_show_ssh_tid(__entry->tid),
		ssam_show_ssh_tid(__entry->sid),
		ssam_show_ssh_tc(__entry->tc),
		__entry->cid,
		__entry->iid,
		__entry->len
	)
);

#define DEFINE_SSAM_COMMAND_EVENT(name)					\
	DEFINE_EVENT(ssam_command_class, ssam_##name,			\
		TP_PROTO(const struct ssh_command *cmd, u16 len),	\
		TP_ARGS(cmd, len)					\
	)

DECLARE_EVENT_CLASS(ssam_packet_class,
	TP_PROTO(const struct ssh_packet *packet),

	TP_ARGS(packet),

	TP_STRUCT__entry(
		__field(unsigned long, state)
		__array(char, uid, SSAM_PTR_UID_LEN)
		__field(u8, priority)
		__field(u16, length)
		__field(u16, seq)
	),

	TP_fast_assign(
		__entry->state = READ_ONCE(packet->state);
		ssam_trace_ptr_uid(packet, __entry->uid);
		__entry->priority = READ_ONCE(packet->priority);
		__entry->length = packet->data.len;
		__entry->seq = ssam_trace_get_packet_seq(packet);
	),

	TP_printk("uid=%s, seq=%s, ty=%s, pri=%#04x, len=%u, sta=%s",
		__entry->uid,
		ssam_show_packet_seq(__entry->seq),
		ssam_show_packet_type(__entry->state),
		__entry->priority,
		__entry->length,
		ssam_show_packet_state(__entry->state)
	)
);

#define DEFINE_SSAM_PACKET_EVENT(name)				\
	DEFINE_EVENT(ssam_packet_class, ssam_##name,		\
		TP_PROTO(const struct ssh_packet *packet),	\
		TP_ARGS(packet)					\
	)

DECLARE_EVENT_CLASS(ssam_packet_status_class,
	TP_PROTO(const struct ssh_packet *packet, int status),

	TP_ARGS(packet, status),

	TP_STRUCT__entry(
		__field(unsigned long, state)
		__field(int, status)
		__array(char, uid, SSAM_PTR_UID_LEN)
		__field(u8, priority)
		__field(u16, length)
		__field(u16, seq)
	),

	TP_fast_assign(
		__entry->state = READ_ONCE(packet->state);
		__entry->status = status;
		ssam_trace_ptr_uid(packet, __entry->uid);
		__entry->priority = READ_ONCE(packet->priority);
		__entry->length = packet->data.len;
		__entry->seq = ssam_trace_get_packet_seq(packet);
	),

	TP_printk("uid=%s, seq=%s, ty=%s, pri=%#04x, len=%u, sta=%s, status=%d",
		__entry->uid,
		ssam_show_packet_seq(__entry->seq),
		ssam_show_packet_type(__entry->state),
		__entry->priority,
		__entry->length,
		ssam_show_packet_state(__entry->state),
		__entry->status
	)
);

#define DEFINE_SSAM_PACKET_STATUS_EVENT(name)				\
	DEFINE_EVENT(ssam_packet_status_class, ssam_##name,		\
		TP_PROTO(const struct ssh_packet *packet, int status),	\
		TP_ARGS(packet, status)					\
	)

DECLARE_EVENT_CLASS(ssam_request_class,
	TP_PROTO(const struct ssh_request *request),

	TP_ARGS(request),

	TP_STRUCT__entry(
		__field(unsigned long, state)
		__field(u32, rqid)
		__array(char, uid, SSAM_PTR_UID_LEN)
		__field(u8, tc)
		__field(u16, cid)
		__field(u16, iid)
		__field(u8, tid)
		__field(u8, sid)
	),

	TP_fast_assign(
		const struct ssh_packet *p = &request->packet;

		/* Use packet for UID so we can match requests to packets. */
		__entry->state = READ_ONCE(request->state);
		__entry->rqid = ssam_trace_get_request_id(p);
		ssam_trace_ptr_uid(p, __entry->uid);
		__entry->tid = ssam_trace_get_request_tid(p);
		__entry->sid = ssam_trace_get_request_sid(p);
		__entry->tc = ssam_trace_get_request_tc(p);
		__entry->cid = ssam_trace_get_command_field_u8(p, cid);
		__entry->iid = ssam_trace_get_command_field_u8(p, iid);
	),

	TP_printk("uid=%s, rqid=%s, ty=%s, sta=%s, tid=%s, sid=%s, tc=%s, cid=%s, iid=%s",
		__entry->uid,
		ssam_show_request_id(__entry->rqid),
		ssam_show_request_type(__entry->state),
		ssam_show_request_state(__entry->state),
		ssam_show_ssh_tid(__entry->tid),
		ssam_show_ssh_tid(__entry->sid),
		ssam_show_ssh_tc(__entry->tc),
		ssam_show_generic_u8_field(__entry->cid),
		ssam_show_generic_u8_field(__entry->iid)
	)
);

#define DEFINE_SSAM_REQUEST_EVENT(name)				\
	DEFINE_EVENT(ssam_request_class, ssam_##name,		\
		TP_PROTO(const struct ssh_request *request),	\
		TP_ARGS(request)				\
	)

DECLARE_EVENT_CLASS(ssam_request_status_class,
	TP_PROTO(const struct ssh_request *request, int status),

	TP_ARGS(request, status),

	TP_STRUCT__entry(
		__field(unsigned long, state)
		__field(u32, rqid)
		__field(int, status)
		__array(char, uid, SSAM_PTR_UID_LEN)
		__field(u8, tc)
		__field(u16, cid)
		__field(u16, iid)
		__field(u8, tid)
		__field(u8, sid)
	),

	TP_fast_assign(
		const struct ssh_packet *p = &request->packet;

		/* Use packet for UID so we can match requests to packets. */
		__entry->state = READ_ONCE(request->state);
		__entry->rqid = ssam_trace_get_request_id(p);
		__entry->status = status;
		ssam_trace_ptr_uid(p, __entry->uid);
		__entry->tid = ssam_trace_get_request_tid(p);
		__entry->sid = ssam_trace_get_request_sid(p);
		__entry->tc = ssam_trace_get_request_tc(p);
		__entry->cid = ssam_trace_get_command_field_u8(p, cid);
		__entry->iid = ssam_trace_get_command_field_u8(p, iid);
	),

	TP_printk("uid=%s, rqid=%s, ty=%s, sta=%s, tid=%s, sid=%s, tc=%s, cid=%s, iid=%s, status=%d",
		__entry->uid,
		ssam_show_request_id(__entry->rqid),
		ssam_show_request_type(__entry->state),
		ssam_show_request_state(__entry->state),
		ssam_show_ssh_tid(__entry->tid),
		ssam_show_ssh_tid(__entry->sid),
		ssam_show_ssh_tc(__entry->tc),
		ssam_show_generic_u8_field(__entry->cid),
		ssam_show_generic_u8_field(__entry->iid),
		__entry->status
	)
);

#define DEFINE_SSAM_REQUEST_STATUS_EVENT(name)				\
	DEFINE_EVENT(ssam_request_status_class, ssam_##name,		\
		TP_PROTO(const struct ssh_request *request, int status),\
		TP_ARGS(request, status)				\
	)

DECLARE_EVENT_CLASS(ssam_alloc_class,
	TP_PROTO(void *ptr, size_t len),

	TP_ARGS(ptr, len),

	TP_STRUCT__entry(
		__field(size_t, len)
		__array(char, uid, SSAM_PTR_UID_LEN)
	),

	TP_fast_assign(
		__entry->len = len;
		ssam_trace_ptr_uid(ptr, __entry->uid);
	),

	TP_printk("uid=%s, len=%zu", __entry->uid, __entry->len)
);

#define DEFINE_SSAM_ALLOC_EVENT(name)					\
	DEFINE_EVENT(ssam_alloc_class, ssam_##name,			\
		TP_PROTO(void *ptr, size_t len),			\
		TP_ARGS(ptr, len)					\
	)

DECLARE_EVENT_CLASS(ssam_free_class,
	TP_PROTO(void *ptr),

	TP_ARGS(ptr),

	TP_STRUCT__entry(
		__array(char, uid, SSAM_PTR_UID_LEN)
	),

	TP_fast_assign(
		ssam_trace_ptr_uid(ptr, __entry->uid);
	),

	TP_printk("uid=%s", __entry->uid)
);

#define DEFINE_SSAM_FREE_EVENT(name)					\
	DEFINE_EVENT(ssam_free_class, ssam_##name,			\
		TP_PROTO(void *ptr),					\
		TP_ARGS(ptr)						\
	)

DECLARE_EVENT_CLASS(ssam_pending_class,
	TP_PROTO(unsigned int pending),

	TP_ARGS(pending),

	TP_STRUCT__entry(
		__field(unsigned int, pending)
	),

	TP_fast_assign(
		__entry->pending = pending;
	),

	TP_printk("pending=%u", __entry->pending)
);

#define DEFINE_SSAM_PENDING_EVENT(name)					\
	DEFINE_EVENT(ssam_pending_class, ssam_##name,			\
		TP_PROTO(unsigned int pending),				\
		TP_ARGS(pending)					\
	)

DECLARE_EVENT_CLASS(ssam_data_class,
	TP_PROTO(size_t length),

	TP_ARGS(length),

	TP_STRUCT__entry(
		__field(size_t, length)
	),

	TP_fast_assign(
		__entry->length = length;
	),

	TP_printk("length=%zu", __entry->length)
);

#define DEFINE_SSAM_DATA_EVENT(name)					\
	DEFINE_EVENT(ssam_data_class, ssam_##name,			\
		TP_PROTO(size_t length),				\
		TP_ARGS(length)						\
	)

DEFINE_SSAM_FRAME_EVENT(rx_frame_received);
DEFINE_SSAM_COMMAND_EVENT(rx_response_received);
DEFINE_SSAM_COMMAND_EVENT(rx_event_received);

DEFINE_SSAM_PACKET_EVENT(packet_release);
DEFINE_SSAM_PACKET_EVENT(packet_submit);
DEFINE_SSAM_PACKET_EVENT(packet_resubmit);
DEFINE_SSAM_PACKET_EVENT(packet_timeout);
DEFINE_SSAM_PACKET_EVENT(packet_cancel);
DEFINE_SSAM_PACKET_STATUS_EVENT(packet_complete);
DEFINE_SSAM_PENDING_EVENT(ptl_timeout_reap);

DEFINE_SSAM_REQUEST_EVENT(request_submit);
DEFINE_SSAM_REQUEST_EVENT(request_timeout);
DEFINE_SSAM_REQUEST_EVENT(request_cancel);
DEFINE_SSAM_REQUEST_STATUS_EVENT(request_complete);
DEFINE_SSAM_PENDING_EVENT(rtl_timeout_reap);

DEFINE_SSAM_PACKET_EVENT(ei_tx_drop_ack_packet);
DEFINE_SSAM_PACKET_EVENT(ei_tx_drop_nak_packet);
DEFINE_SSAM_PACKET_EVENT(ei_tx_drop_dsq_packet);
DEFINE_SSAM_PACKET_STATUS_EVENT(ei_tx_fail_write);
DEFINE_SSAM_PACKET_EVENT(ei_tx_corrupt_data);
DEFINE_SSAM_DATA_EVENT(ei_rx_corrupt_syn);
DEFINE_SSAM_FRAME_EVENT(ei_rx_corrupt_data);
DEFINE_SSAM_REQUEST_EVENT(ei_rx_drop_response);

DEFINE_SSAM_ALLOC_EVENT(ctrl_packet_alloc);
DEFINE_SSAM_FREE_EVENT(ctrl_packet_free);

DEFINE_SSAM_ALLOC_EVENT(event_item_alloc);
DEFINE_SSAM_FREE_EVENT(event_item_free);

#endif /* _SURFACE_AGGREGATOR_TRACE_H */

/* This part must be outside protection */
#undef TRACE_INCLUDE_PATH
#undef TRACE_INCLUDE_FILE

#define TRACE_INCLUDE_PATH .
#define TRACE_INCLUDE_FILE trace

#include <trace/define_trace.h>
