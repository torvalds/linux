/* SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only) */
/* Copyright(c) 2015 - 2021 Intel Corporation */
#ifndef ADF_PFVF_MSG_H
#define ADF_PFVF_MSG_H

#include <linux/bits.h>

/*
 * PF<->VF Messaging
 * The PF has an array of 32-bit PF2VF registers, one for each VF.  The
 * PF can access all these registers; each VF can access only the one
 * register associated with that particular VF.
 *
 * The register functionally is split into two parts:
 * The bottom half is for PF->VF messages. In particular when the first
 * bit of this register (bit 0) gets set an interrupt will be triggered
 * in the respective VF.
 * The top half is for VF->PF messages. In particular when the first bit
 * of this half of register (bit 16) gets set an interrupt will be triggered
 * in the PF.
 *
 * The remaining bits within this register are available to encode messages.
 * and implement a collision control mechanism to prevent concurrent use of
 * the PF2VF register by both the PF and VF.
 *
 *  31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16
 *  _______________________________________________
 * |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
 * +-----------------------------------------------+
 *  \___________________________/ \_________/ ^   ^
 *                ^                    ^      |   |
 *                |                    |      |   VF2PF Int
 *                |                    |      Message Origin
 *                |                    Message Type
 *                Message-specific Data/Reserved
 *
 *  15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0
 *  _______________________________________________
 * |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
 * +-----------------------------------------------+
 *  \___________________________/ \_________/ ^   ^
 *                ^                    ^      |   |
 *                |                    |      |   PF2VF Int
 *                |                    |      Message Origin
 *                |                    Message Type
 *                Message-specific Data/Reserved
 *
 * Message Origin (Should always be 1)
 * A legacy out-of-tree QAT driver allowed for a set of messages not supported
 * by this driver; these had a Msg Origin of 0 and are ignored by this driver.
 *
 * When a PF or VF attempts to send a message in the lower or upper 16 bits,
 * respectively, the other 16 bits are written to first with a defined
 * IN_USE_BY pattern as part of a collision control scheme (see function
 * adf_gen2_pfvf_send() in adf_pf2vf_msg.c).
 */

/* PFVF message common bits */
#define ADF_PFVF_INT				BIT(0)
#define ADF_PFVF_MSGORIGIN_SYSTEM		BIT(1)

/* Different generations have different CSR layouts, use this struct
 * to abstract these differences away
 */
struct pfvf_message {
	u8 type;
	u32 data;
};

/* PF->VF messages */
enum pf2vf_msgtype {
	ADF_PF2VF_MSGTYPE_RESTARTING		= 0x01,
	ADF_PF2VF_MSGTYPE_VERSION_RESP		= 0x02,
	ADF_PF2VF_MSGTYPE_BLKMSG_RESP		= 0x03,
};

/* VF->PF messages */
enum vf2pf_msgtype {
	ADF_VF2PF_MSGTYPE_INIT			= 0x03,
	ADF_VF2PF_MSGTYPE_SHUTDOWN		= 0x04,
	ADF_VF2PF_MSGTYPE_VERSION_REQ		= 0x05,
	ADF_VF2PF_MSGTYPE_COMPAT_VER_REQ	= 0x06,
	ADF_VF2PF_MSGTYPE_LARGE_BLOCK_REQ	= 0x07,
	ADF_VF2PF_MSGTYPE_MEDIUM_BLOCK_REQ	= 0x08,
	ADF_VF2PF_MSGTYPE_SMALL_BLOCK_REQ	= 0x09,
};

/* VF/PF compatibility version. */
enum pfvf_compatibility_version {
	/* Support for extended capabilities */
	ADF_PFVF_COMPAT_CAPABILITIES		= 0x02,
	/* Reference to the latest version */
	ADF_PFVF_COMPAT_THIS_VERSION		= 0x02,
};

/* PF->VF Version Response */
#define ADF_PF2VF_VERSION_RESP_VERS_MASK	GENMASK(7, 0)
#define ADF_PF2VF_VERSION_RESP_RESULT_MASK	GENMASK(9, 8)

enum pf2vf_compat_response {
	ADF_PF2VF_VF_COMPATIBLE			= 0x01,
	ADF_PF2VF_VF_INCOMPATIBLE		= 0x02,
	ADF_PF2VF_VF_COMPAT_UNKNOWN		= 0x03,
};

/* PF->VF Block Responses */
#define ADF_PF2VF_BLKMSG_RESP_TYPE_MASK		GENMASK(1, 0)
#define ADF_PF2VF_BLKMSG_RESP_DATA_MASK		GENMASK(9, 2)

enum pf2vf_blkmsg_resp_type {
	ADF_PF2VF_BLKMSG_RESP_TYPE_DATA		= 0x00,
	ADF_PF2VF_BLKMSG_RESP_TYPE_CRC		= 0x01,
	ADF_PF2VF_BLKMSG_RESP_TYPE_ERROR	= 0x02,
};

/* PF->VF Block Error Code */
enum pf2vf_blkmsg_error {
	ADF_PF2VF_INVALID_BLOCK_TYPE		= 0x00,
	ADF_PF2VF_INVALID_BYTE_NUM_REQ		= 0x01,
	ADF_PF2VF_PAYLOAD_TRUNCATED		= 0x02,
	ADF_PF2VF_UNSPECIFIED_ERROR		= 0x03,
};

/* VF->PF Block Requests */
#define ADF_VF2PF_LARGE_BLOCK_TYPE_MASK		GENMASK(1, 0)
#define ADF_VF2PF_LARGE_BLOCK_BYTE_MASK		GENMASK(8, 2)
#define ADF_VF2PF_MEDIUM_BLOCK_TYPE_MASK	GENMASK(2, 0)
#define ADF_VF2PF_MEDIUM_BLOCK_BYTE_MASK	GENMASK(8, 3)
#define ADF_VF2PF_SMALL_BLOCK_TYPE_MASK		GENMASK(3, 0)
#define ADF_VF2PF_SMALL_BLOCK_BYTE_MASK		GENMASK(8, 4)
#define ADF_VF2PF_BLOCK_CRC_REQ_MASK		BIT(9)

/* PF->VF Block Request Types
 *  0..15 - 32 byte message
 * 16..23 - 64 byte message
 * 24..27 - 128 byte message
 */
enum vf2pf_blkmsg_req_type {
	ADF_VF2PF_BLKMSG_REQ_CAP_SUMMARY	= 0x02,
};

#define ADF_VF2PF_SMALL_BLOCK_TYPE_MAX \
		(FIELD_MAX(ADF_VF2PF_SMALL_BLOCK_TYPE_MASK))

#define ADF_VF2PF_MEDIUM_BLOCK_TYPE_MAX \
		(FIELD_MAX(ADF_VF2PF_MEDIUM_BLOCK_TYPE_MASK) + \
		ADF_VF2PF_SMALL_BLOCK_TYPE_MAX + 1)

#define ADF_VF2PF_LARGE_BLOCK_TYPE_MAX \
		(FIELD_MAX(ADF_VF2PF_LARGE_BLOCK_TYPE_MASK) + \
		ADF_VF2PF_MEDIUM_BLOCK_TYPE_MAX)

#define ADF_VF2PF_SMALL_BLOCK_BYTE_MAX \
		FIELD_MAX(ADF_VF2PF_SMALL_BLOCK_BYTE_MASK)

#define ADF_VF2PF_MEDIUM_BLOCK_BYTE_MAX \
		FIELD_MAX(ADF_VF2PF_MEDIUM_BLOCK_BYTE_MASK)

#define ADF_VF2PF_LARGE_BLOCK_BYTE_MAX \
		FIELD_MAX(ADF_VF2PF_LARGE_BLOCK_BYTE_MASK)

struct pfvf_blkmsg_header {
	u8 version;
	u8 payload_size;
} __packed;

#define ADF_PFVF_BLKMSG_HEADER_SIZE		(sizeof(struct pfvf_blkmsg_header))
#define ADF_PFVF_BLKMSG_PAYLOAD_SIZE(blkmsg)	(sizeof(blkmsg) - \
							ADF_PFVF_BLKMSG_HEADER_SIZE)
#define ADF_PFVF_BLKMSG_MSG_SIZE(blkmsg)	(ADF_PFVF_BLKMSG_HEADER_SIZE + \
							(blkmsg)->hdr.payload_size)
#define ADF_PFVF_BLKMSG_MSG_MAX_SIZE		128

/* PF->VF Block message header bytes */
#define ADF_PFVF_BLKMSG_VER_BYTE		0
#define ADF_PFVF_BLKMSG_LEN_BYTE		1

/* PF/VF Capabilities message values */
enum blkmsg_capabilities_versions {
	ADF_PFVF_CAPABILITIES_V1_VERSION	= 0x01,
	ADF_PFVF_CAPABILITIES_V2_VERSION	= 0x02,
	ADF_PFVF_CAPABILITIES_V3_VERSION	= 0x03,
};

struct capabilities_v1 {
	struct pfvf_blkmsg_header hdr;
	u32 ext_dc_caps;
} __packed;

struct capabilities_v2 {
	struct pfvf_blkmsg_header hdr;
	u32 ext_dc_caps;
	u32 capabilities;
} __packed;

struct capabilities_v3 {
	struct pfvf_blkmsg_header hdr;
	u32 ext_dc_caps;
	u32 capabilities;
	u32 frequency;
} __packed;

#endif /* ADF_PFVF_MSG_H */
