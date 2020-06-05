/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * RDMA Network Block Driver
 *
 * Copyright (c) 2014 - 2018 ProfitBricks GmbH. All rights reserved.
 * Copyright (c) 2018 - 2019 1&1 IONOS Cloud GmbH. All rights reserved.
 * Copyright (c) 2019 - 2020 1&1 IONOS SE. All rights reserved.
 */
#ifndef RNBD_PROTO_H
#define RNBD_PROTO_H

#include <linux/types.h>
#include <linux/blkdev.h>
#include <linux/limits.h>
#include <linux/inet.h>
#include <linux/in.h>
#include <linux/in6.h>
#include <rdma/ib.h>

#define RNBD_PROTO_VER_MAJOR 2
#define RNBD_PROTO_VER_MINOR 0

/* The default port number the RTRS server is listening on. */
#define RTRS_PORT 1234

/**
 * enum rnbd_msg_types - RNBD message types
 * @RNBD_MSG_SESS_INFO:	initial session info from client to server
 * @RNBD_MSG_SESS_INFO_RSP:	initial session info from server to client
 * @RNBD_MSG_OPEN:		open (map) device request
 * @RNBD_MSG_OPEN_RSP:		response to an @RNBD_MSG_OPEN
 * @RNBD_MSG_IO:		block IO request operation
 * @RNBD_MSG_CLOSE:		close (unmap) device request
 */
enum rnbd_msg_type {
	RNBD_MSG_SESS_INFO,
	RNBD_MSG_SESS_INFO_RSP,
	RNBD_MSG_OPEN,
	RNBD_MSG_OPEN_RSP,
	RNBD_MSG_IO,
	RNBD_MSG_CLOSE,
};

/**
 * struct rnbd_msg_hdr - header of RNBD messages
 * @type:	Message type, valid values see: enum rnbd_msg_types
 */
struct rnbd_msg_hdr {
	__le16		type;
	__le16		__padding;
};

/**
 * We allow to map RO many times and RW only once. We allow to map yet another
 * time RW, if MIGRATION is provided (second RW export can be required for
 * example for VM migration)
 */
enum rnbd_access_mode {
	RNBD_ACCESS_RO,
	RNBD_ACCESS_RW,
	RNBD_ACCESS_MIGRATION,
};

/**
 * struct rnbd_msg_sess_info - initial session info from client to server
 * @hdr:		message header
 * @ver:		RNBD protocol version
 */
struct rnbd_msg_sess_info {
	struct rnbd_msg_hdr hdr;
	u8		ver;
	u8		reserved[31];
};

/**
 * struct rnbd_msg_sess_info_rsp - initial session info from server to client
 * @hdr:		message header
 * @ver:		RNBD protocol version
 */
struct rnbd_msg_sess_info_rsp {
	struct rnbd_msg_hdr hdr;
	u8		ver;
	u8		reserved[31];
};

/**
 * struct rnbd_msg_open - request to open a remote device.
 * @hdr:		message header
 * @access_mode:	the mode to open remote device, valid values see:
 *			enum rnbd_access_mode
 * @device_name:	device path on remote side
 */
struct rnbd_msg_open {
	struct rnbd_msg_hdr hdr;
	u8		access_mode;
	u8		resv1;
	s8		dev_name[NAME_MAX];
	u8		reserved[3];
};

/**
 * struct rnbd_msg_close - request to close a remote device.
 * @hdr:	message header
 * @device_id:	device_id on server side to identify the device
 */
struct rnbd_msg_close {
	struct rnbd_msg_hdr hdr;
	__le32		device_id;
};

/**
 * struct rnbd_msg_open_rsp - response message to RNBD_MSG_OPEN
 * @hdr:		message header
 * @device_id:		device_id on server side to identify the device
 * @nsectors:		number of sectors in the usual 512b unit
 * @max_hw_sectors:	max hardware sectors in the usual 512b unit
 * @max_write_same_sectors: max sectors for WRITE SAME in the 512b unit
 * @max_discard_sectors: max. sectors that can be discarded at once in 512b
 * unit.
 * @discard_granularity: size of the internal discard allocation unit in bytes
 * @discard_alignment: offset from internal allocation assignment in bytes
 * @physical_block_size: physical block size device supports in bytes
 * @logical_block_size: logical block size device supports in bytes
 * @max_segments:	max segments hardware support in one transfer
 * @secure_discard:	supports secure discard
 * @rotation:		is a rotational disc?
 */
struct rnbd_msg_open_rsp {
	struct rnbd_msg_hdr	hdr;
	__le32			device_id;
	__le64			nsectors;
	__le32			max_hw_sectors;
	__le32			max_write_same_sectors;
	__le32			max_discard_sectors;
	__le32			discard_granularity;
	__le32			discard_alignment;
	__le16			physical_block_size;
	__le16			logical_block_size;
	__le16			max_segments;
	__le16			secure_discard;
	u8			rotational;
	u8			reserved[11];
};

/**
 * struct rnbd_msg_io - message for I/O read/write
 * @hdr:	message header
 * @device_id:	device_id on server side to find the right device
 * @sector:	bi_sector attribute from struct bio
 * @rw:		valid values are defined in enum rnbd_io_flags
 * @bi_size:    number of bytes for I/O read/write
 * @prio:       priority
 */
struct rnbd_msg_io {
	struct rnbd_msg_hdr hdr;
	__le32		device_id;
	__le64		sector;
	__le32		rw;
	__le32		bi_size;
	__le16		prio;
};

#define RNBD_OP_BITS  8
#define RNBD_OP_MASK  ((1 << RNBD_OP_BITS) - 1)

/**
 * enum rnbd_io_flags - RNBD request types from rq_flag_bits
 * @RNBD_OP_READ:	     read sectors from the device
 * @RNBD_OP_WRITE:	     write sectors to the device
 * @RNBD_OP_FLUSH:	     flush the volatile write cache
 * @RNBD_OP_DISCARD:        discard sectors
 * @RNBD_OP_SECURE_ERASE:   securely erase sectors
 * @RNBD_OP_WRITE_SAME:     write the same sectors many times

 * @RNBD_F_SYNC:	     request is sync (sync write or read)
 * @RNBD_F_FUA:             forced unit access
 */
enum rnbd_io_flags {

	/* Operations */

	RNBD_OP_READ		= 0,
	RNBD_OP_WRITE		= 1,
	RNBD_OP_FLUSH		= 2,
	RNBD_OP_DISCARD	= 3,
	RNBD_OP_SECURE_ERASE	= 4,
	RNBD_OP_WRITE_SAME	= 5,

	RNBD_OP_LAST,

	/* Flags */

	RNBD_F_SYNC  = 1<<(RNBD_OP_BITS + 0),
	RNBD_F_FUA   = 1<<(RNBD_OP_BITS + 1),

	RNBD_F_ALL   = (RNBD_F_SYNC | RNBD_F_FUA)

};

static inline u32 rnbd_op(u32 flags)
{
	return flags & RNBD_OP_MASK;
}

static inline u32 rnbd_flags(u32 flags)
{
	return flags & ~RNBD_OP_MASK;
}

static inline bool rnbd_flags_supported(u32 flags)
{
	u32 op;

	op = rnbd_op(flags);
	flags = rnbd_flags(flags);

	if (op >= RNBD_OP_LAST)
		return false;
	if (flags & ~RNBD_F_ALL)
		return false;

	return true;
}

static inline u32 rnbd_to_bio_flags(u32 rnbd_opf)
{
	u32 bio_opf;

	switch (rnbd_op(rnbd_opf)) {
	case RNBD_OP_READ:
		bio_opf = REQ_OP_READ;
		break;
	case RNBD_OP_WRITE:
		bio_opf = REQ_OP_WRITE;
		break;
	case RNBD_OP_FLUSH:
		bio_opf = REQ_OP_FLUSH | REQ_PREFLUSH;
		break;
	case RNBD_OP_DISCARD:
		bio_opf = REQ_OP_DISCARD;
		break;
	case RNBD_OP_SECURE_ERASE:
		bio_opf = REQ_OP_SECURE_ERASE;
		break;
	case RNBD_OP_WRITE_SAME:
		bio_opf = REQ_OP_WRITE_SAME;
		break;
	default:
		WARN(1, "Unknown RNBD type: %d (flags %d)\n",
		     rnbd_op(rnbd_opf), rnbd_opf);
		bio_opf = 0;
	}

	if (rnbd_opf & RNBD_F_SYNC)
		bio_opf |= REQ_SYNC;

	if (rnbd_opf & RNBD_F_FUA)
		bio_opf |= REQ_FUA;

	return bio_opf;
}

static inline u32 rq_to_rnbd_flags(struct request *rq)
{
	u32 rnbd_opf;

	switch (req_op(rq)) {
	case REQ_OP_READ:
		rnbd_opf = RNBD_OP_READ;
		break;
	case REQ_OP_WRITE:
		rnbd_opf = RNBD_OP_WRITE;
		break;
	case REQ_OP_DISCARD:
		rnbd_opf = RNBD_OP_DISCARD;
		break;
	case REQ_OP_SECURE_ERASE:
		rnbd_opf = RNBD_OP_SECURE_ERASE;
		break;
	case REQ_OP_WRITE_SAME:
		rnbd_opf = RNBD_OP_WRITE_SAME;
		break;
	case REQ_OP_FLUSH:
		rnbd_opf = RNBD_OP_FLUSH;
		break;
	default:
		WARN(1, "Unknown request type %d (flags %llu)\n",
		     req_op(rq), (unsigned long long)rq->cmd_flags);
		rnbd_opf = 0;
	}

	if (op_is_sync(rq->cmd_flags))
		rnbd_opf |= RNBD_F_SYNC;

	if (op_is_flush(rq->cmd_flags))
		rnbd_opf |= RNBD_F_FUA;

	return rnbd_opf;
}

const char *rnbd_access_mode_str(enum rnbd_access_mode mode);

#endif /* RNBD_PROTO_H */
