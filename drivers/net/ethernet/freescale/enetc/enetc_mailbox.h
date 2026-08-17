/* SPDX-License-Identifier: (GPL-2.0+ OR BSD-3-Clause) */
/*
 * Copyright 2025-2026 NXP
 *
 * The VSI-to-PSI message generic format:
 *
 * OFFSET  0                               16              24            31
 *        +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *  0x0   |       CRC16 (big-endian)      |    CLASS ID   |     CMD ID    |
 *        +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *  0x4   |   PROTO VER   |      LEN      |    RESV       | COOKIE|  RESV |
 *        +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *  0x8   |                              RESV                             |
 *        +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *  0xc   |                              RESV                             |
 *        +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *  0x10  |                                                               |
 *  0x14  |                                                               |
 *  0x18  |                          Message Body                         |
 *  0x1c  |                                                               |
 *        +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *  0x20  |                                                               |
 *    ~   |              Extended Message Body: LEN x 32B                 |
 *  0x3e0 |                                                               |
 *        +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 * Field Descriptions:
 * CRC16 (16-bit): Big endian, CRC16 CCITT-FALSE algorithm, It provides the
 * equivalent data integrity check functionality as the FCS for standard
 * Ethernet frames.
 *
 * CLASS ID (8-bit) and CMD ID (8-bit): These are 8-bit fields identifying
 * the command class and the class-specific operations supported. For more
 * details, please refer to the definitions of the relevant class ID and
 * cmd ID in this document.
 *
 * PROTO VER (8-bit): Supported VSI-PSI command protocol version. Currently
 * only support version 0. To be incremented for future protocol extensions.
 *
 * LEN (8-bit): Extended message body length in increments of 32B. The upper
 * limit is given by the physical implementation of the NETC VSI-PSI Messaging
 * mechanism that supports message sizes of up to 1024B (including headers),
 * that are multiple of 32B.
 *
 * COOKIE (4-bit): Optional parameter, which, if not 0, indicates that the
 * command should be execute asynchronously on PSI side. If COOKIE is not 0
 * and the command cannot be executed instantly on the PSI side (it would
 * take longer time to complete), the PSI may enqueue the request in a command
 * queue of up to 15 entries per VSI and, later after command execution, the
 * PSI returns the COOKIE to VSI as part of an asynchronous notification
 * message that indicates the command completion status. If COOKIE is 0 then
 * the command is considered as blocking, the PSI will wait for the execution
 * of the command to complete before updating the PSIMSGRR[MC] field with the
 * corresponding return code.
 *
 * The PSI-to-VSI message generic format:
 *   0               4               8               12          15
 * +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
 * |       COOKIE      |   CLASS CODE  |          CLASS ID         |
 * +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
 *
 * The PSI to VSI message format is mapped to the following PSI message
 * registers/fields, depending on use case:
 * 1) PSI_RX_control: PSIMSGRR[MC] - for VSI command return code messages
 * (blocking requests), and
 * 2) PSI_TX_control: PSIMSGSR[MC] - for PSI to VSI notification messages
 * (async mode)
 *
 * Note that for some GET messages, there is no COOKIE field, and the CLASS
 * CODE field is expanded to 8 bits.
 */

#ifndef __ENETC_MAILBOX_H
#define __ENETC_MAILBOX_H

#include <linux/crc-itu-t.h>

#define ENETC_CRC_INIT				0xffff
#define ENETC_MSG_ALIGN				32
/* s indicates the size of the message */
#define ENETC_MSG_EXT_BODY_LEN(s)		((s) / ENETC_MSG_ALIGN - 1)
/* l indicates the extended body len (LEN field) of the message */
#define ENETC_MSG_SIZE(l)			(((l) + 1) * ENETC_MSG_ALIGN)

/* The cookie filed of VSI-to-PSI message */
#define ENETC_VF_MSG_COOKIE			GENMASK(3, 0)
/* The fileds of PSI-to-VSI message, the message is only 16-bit */
#define ENETC_PF_MSG_COOKIE			GENMASK(3, 0)
#define ENETC_PF_MSG_CLASS_CODE			GENMASK(7, 4)
/* Extend the class code to 8-bit for GET messages without COOKIE */
#define ENETC_PF_MSG_CLASS_CODE_U8		GENMASK(7, 0)
#define ENETC_PF_MSG_CLASS_ID			GENMASK(15, 8)

enum enetc_msg_class_id {
	/* Class ID for PSI-to-VSI messages */
	ENETC_MSG_CLASS_ID_CMD_SUCCESS		= 1,
	ENETC_MSG_CLASS_ID_PERMISSION_DENY,
	ENETC_MSG_CLASS_ID_CMD_NOT_SUPPORT,
	ENETC_MSG_CLASS_ID_PSI_BUSY,
	ENETC_MSG_CLASS_ID_CRC_ERROR,
	ENETC_MSG_CLASS_ID_PROTO_NOT_SUPPORT,
	ENETC_MSG_CLASS_ID_INVALID_MSG_LEN,
	ENETC_MSG_CLASS_ID_CMD_TIMEOUT,
	ENETC_MSG_CLASS_ID_CMD_NOT_PERMITTED,
	ENETC_MSG_CLASS_ID_CMD_FAIL, /* Generic error code for failure */
	ENETC_MSG_CLASS_ID_CMD_DEFERRED		= 0xf,

	/* Common Class ID for PSI-to-VSI and VSI-to-PSI messages */
	ENETC_MSG_CLASS_ID_MAC_FILTER		= 0x20,
	ENETC_MSG_CLASS_ID_IP_REVISION		= 0xf0,
};

enum enetc_msg_mac_filter_cmd_id {
	ENETC_MSG_SET_PRIMARY_MAC,
};

enum enetc_msg_ip_revision_cmd_id {
	ENETC_MSG_GET_IP_MN			= 1,
};

/* Class-specific error return codes of MAC filter */
enum enetc_mac_filter_class_code {
	ENETC_MF_CLASS_CODE_INVALID_MAC,
};

struct enetc_msg_swbd {
	void *vaddr;
	dma_addr_t dma;
	int size;
};

/* The generic VSI-to-PSI message header */
struct enetc_msg_header {
	__be16 crc16;
	u8 class_id;
	u8 cmd_id;
	u8 proto_ver;
	u8 len;
	u8 resv0;
	u8 cookie;
	u8 resv2[8];
};

struct enetc_mac_addr {
	u8 addr[ETH_ALEN]; /* Network byte order */
};

/* Message format of class_id 0x20 for exact MAC filter.
 * cmd_id 0x0: set primary MAC
 * cmd_id 0x1: Add entries to MAC address filter table
 * cmd_id 0x2: Delete entries from MAC address filter table
 * Note that cmd_id 0x1 and 0x2 are not supported yet.
 */
struct enetc_msg_mac_exact_filter {
	struct enetc_msg_header hdr;
	u8 mac_cnt; /* No need to set for cmd_id 0 */
	u8 resv[3];
	struct enetc_mac_addr mac[];
};

/* The generic message format applies to the following messages:
 * Get IP revision message, class_id 0xf0.
 * cmd_id 1: get IP minor revision
 */
struct enetc_msg_generic {
	struct enetc_msg_header hdr;
	u8 resv[16];
};

#endif
