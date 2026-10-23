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
 * Note that for some PSI-to-VSI messages, there is no COOKIE field, and the
 * CLASS CODE field is expanded to 8 bits.
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
/* Extend the class code to 8-bit for PSI-to-VSI messages without COOKIE
 * The class code for the following messages is 8-bit.
 * 1. Get IP revision messages
 * 2. Link status messages
 * 3. Link speed messages
 */
#define ENETC_PF_MSG_CLASS_CODE_U8		GENMASK(7, 0)
#define ENETC_PF_MSG_CLASS_ID			GENMASK(15, 8)

#define ENETC_MAC_HASH_TABLE_SIZE_64		0
#define ENETC_MSG_MAC_HASH_SIZE			GENMASK(5, 0)
#define ENETC_MSG_MAC_TYPE			GENMASK(7, 6)
#define  ENETC_MAC_FILTER_TYPE_UC		BIT(0)
#define  ENETC_MAC_FILTER_TYPE_MC		BIT(1)
#define  ENETC_MAC_FILTER_TYPE_ALL		(ENETC_MAC_FILTER_TYPE_UC | \
						 ENETC_MAC_FILTER_TYPE_MC)

#define ENETC_MSG_MAC_FLUSH_MACS		BIT(0)
#define ENETC_MSG_MAC_PROMISC_MODE		BIT(1)

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
	ENETC_MSG_CLASS_ID_LINK_STATUS		= 0x80,
	ENETC_MSG_CLASS_ID_LINK_SPEED		= 0x81,
	ENETC_MSG_CLASS_ID_IP_REVISION		= 0xf0,
};

enum enetc_msg_mac_filter_cmd_id {
	ENETC_MSG_SET_PRIMARY_MAC,
	ENETC_MSG_SET_MAC_HASH_TABLE		= 3,
	ENETC_MSG_SET_MAC_PROMISC_MODE		= 5,
};

enum enetc_msg_ip_revision_cmd_id {
	ENETC_MSG_GET_IP_MN			= 1,
};

enum enetc_msg_link_status_cmd_id {
	ENETC_MSG_GET_CURRENT_LINK_STATUS,
	ENETC_MSG_REGISTER_LINK_CHANGE_NOTIFIER,
	ENETC_MSG_UNREGISTER_LINK_CHANGE_NOTIFIER,
};

enum enetc_msg_link_speed_cmd_id {
	ENETC_MSG_GET_CURRENT_LINK_SPEED,
	/* The following command IDs are not currently supported */
	ENETC_MSG_REGISTER_SPEED_CHANGE_NOTIFIER,
	ENETC_MSG_UNREGISTER_SPEED_CHANGE_NOTIFIER,
};

/* Class-specific error return codes of MAC filter */
enum enetc_mac_filter_class_code {
	ENETC_MF_CLASS_CODE_INVALID_MAC,
	ENETC_MF_CLASS_CODE_INVALID_TYPE	= 4,
	/* Unicast Filter Is Denied */
	ENETC_MF_CLASS_CODE_UCF_DENY		= 5,
};

/* Class-specific notifications/codes of link status */
#define ENETC_CLASS_CODE_LINK_DOWN		BIT(0)
#define ENETC_CLASS_CODE_TX_PAUSE_EN		BIT(1)

/* Class-specific notifications/codes of link speed */
enum enetc_link_speed_class_code {
	ENETC_MSG_SPEED_UNKNOWN,
	ENETC_MSG_SPEED_10M_HD,
	ENETC_MSG_SPEED_10M_FD,
	ENETC_MSG_SPEED_100M_HD,
	ENETC_MSG_SPEED_100M_FD,
	ENETC_MSG_SPEED_1000M,
	ENETC_MSG_SPEED_2500M,
	ENETC_MSG_SPEED_5G,
	/* Do not add enumeration values for any speed greater than
	 * 5Gbps. For any speed greater than 5Gbps, its speed class
	 * code should follow the formula below.
	 *
	 * SPEED = (link_speed - 5000) / 1000 + ENETC_MSG_SPEED_5G
	 *
	 * The unit of link_speed should be Mbps, the max SPEED
	 * should <= ENETC_MSG_SPEED_MAX.
	 */
	ENETC_MSG_SPEED_MAX = 0xff,
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

/* message format of class_id 0x20 for hash MAC filter.
 * cmd_id 0x3: set MAC hash table
 */
struct enetc_msg_mac_hash_filter {
	struct enetc_msg_header hdr;
	/* bit 0 ~ 5: ENETC_MSG_MAC_HASH_SIZE
	 * bit 6~7: ENETC_MSG_MAC_TYPE
	 */
	u8 sz_type;
	u8 resv[3];
	u32 hash_tbl[];
};

/* message format of class_id 0x20 for MAC promiscuous mode.
 * cmd_id 0x5: set MAC promiscuous mode
 */
struct enetc_msg_mac_promisc_mode {
	struct enetc_msg_header hdr;
	/* bit 0: ENETC_MSG_MAC_FLUSH_MACS
	 * bit 1: ENETC_MSG_MAC_PROMISC_MODE
	 * bit 6~7: ENETC_MSG_MAC_TYPE
	 */
	u8 config;
	u8 resv[15];
};

/* The generic message format applies to the following messages:
 * Get IP revision message, class_id 0xf0.
 * cmd_id 1: get IP minor revision
 *
 * Link status message, class id 0x80.
 * cmd_id 0x0: get the current link status
 * cmd_id 0x1: register link status change notification
 * cmd_id 0x2: unregister link status change notification
 *
 * Link speed message, class_id 0x81.
 * cmd_id 0x0: get the current link speed. Unlike the link status
 *   query (class 0x80), this query is only permitted for trusted VFs;
 *   an untrusted VF receives a permission-deny response. This is
 *   because the PF must take rtnl_lock() to read the link speed, so
 *   restricting it to trusted VFs avoids rtnl_lock contention on the
 *   host from a misbehaving VF.
 * cmd_id 0x1: register link speed change notification, not supported yet
 * cmd_id 0x2: unregister link speed change notification, not supported yet
 */
struct enetc_msg_generic {
	struct enetc_msg_header hdr;
	u8 resv[16];
};

#endif
