/* SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only) */
/* Copyright(c) 2015 - 2020 Intel Corporation */
#ifndef ADF_PF2VF_MSG_H
#define ADF_PF2VF_MSG_H

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
 * IN_USE_BY pattern as part of a collision control scheme (see adf_iov_putmsg).
 */

#define ADF_PFVF_COMPAT_THIS_VERSION		0x1	/* PF<->VF compat */

/* PF->VF messages */
#define ADF_PF2VF_INT				BIT(0)
#define ADF_PF2VF_MSGORIGIN_SYSTEM		BIT(1)
#define ADF_PF2VF_MSGTYPE_MASK			0x0000003C
#define ADF_PF2VF_MSGTYPE_SHIFT			2
#define ADF_PF2VF_MSGTYPE_RESTARTING		0x01
#define ADF_PF2VF_MSGTYPE_VERSION_RESP		0x02
#define ADF_PF2VF_IN_USE_BY_PF			0x6AC20000
#define ADF_PF2VF_IN_USE_BY_PF_MASK		0xFFFE0000

/* PF->VF Version Response */
#define ADF_PF2VF_VERSION_RESP_VERS_MASK	0x00003FC0
#define ADF_PF2VF_VERSION_RESP_VERS_SHIFT	6
#define ADF_PF2VF_VERSION_RESP_RESULT_MASK	0x0000C000
#define ADF_PF2VF_VERSION_RESP_RESULT_SHIFT	14
#define ADF_PF2VF_MINORVERSION_SHIFT		6
#define ADF_PF2VF_MAJORVERSION_SHIFT		10
#define ADF_PF2VF_VF_COMPATIBLE			1
#define ADF_PF2VF_VF_INCOMPATIBLE		2
#define ADF_PF2VF_VF_COMPAT_UNKNOWN		3

/* VF->PF messages */
#define ADF_VF2PF_IN_USE_BY_VF			0x00006AC2
#define ADF_VF2PF_IN_USE_BY_VF_MASK		0x0000FFFE
#define ADF_VF2PF_INT				BIT(16)
#define ADF_VF2PF_MSGORIGIN_SYSTEM		BIT(17)
#define ADF_VF2PF_MSGTYPE_MASK			0x003C0000
#define ADF_VF2PF_MSGTYPE_SHIFT			18
#define ADF_VF2PF_MSGTYPE_INIT			0x3
#define ADF_VF2PF_MSGTYPE_SHUTDOWN		0x4
#define ADF_VF2PF_MSGTYPE_VERSION_REQ		0x5
#define ADF_VF2PF_MSGTYPE_COMPAT_VER_REQ	0x6

/* VF->PF Compatible Version Request */
#define ADF_VF2PF_COMPAT_VER_REQ_SHIFT		22

#endif /* ADF_IOV_MSG_H */
