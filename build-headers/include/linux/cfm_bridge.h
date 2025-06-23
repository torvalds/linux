/* SPDX-License-Identifier: GPL-2.0+ WITH Linux-syscall-note */

#ifndef _LINUX_CFM_BRIDGE_H_
#define _LINUX_CFM_BRIDGE_H_

#include <linux/types.h>
#include <linux/if_ether.h>

#define ETHER_HEADER_LENGTH		(6+6+4+2)
#define CFM_MAID_LENGTH			48
#define CFM_CCM_PDU_LENGTH		75
#define CFM_PORT_STATUS_TLV_LENGTH	4
#define CFM_IF_STATUS_TLV_LENGTH	4
#define CFM_IF_STATUS_TLV_TYPE		4
#define CFM_PORT_STATUS_TLV_TYPE	2
#define CFM_ENDE_TLV_TYPE		0
#define CFM_CCM_MAX_FRAME_LENGTH	(ETHER_HEADER_LENGTH+\
					 CFM_CCM_PDU_LENGTH+\
					 CFM_PORT_STATUS_TLV_LENGTH+\
					 CFM_IF_STATUS_TLV_LENGTH)
#define CFM_FRAME_PRIO			7
#define CFM_CCM_TLV_OFFSET		70
#define CFM_CCM_PDU_MAID_OFFSET		10
#define CFM_CCM_PDU_MEPID_OFFSET	8
#define CFM_CCM_PDU_SEQNR_OFFSET	4
#define CFM_CCM_PDU_TLV_OFFSET		74
#define CFM_CCM_ITU_RESERVED_SIZE	16

struct br_cfm_common_hdr {
	__u8 mdlevel_version;
	__u8 opcode;
	__u8 flags;
	__u8 tlv_offset;
};

enum br_cfm_opcodes {
	BR_CFM_OPCODE_CCM = 0x1,
};

/* MEP domain */
enum br_cfm_domain {
	BR_CFM_PORT,
	BR_CFM_VLAN,
};

/* MEP direction */
enum br_cfm_mep_direction {
	BR_CFM_MEP_DIRECTION_DOWN,
	BR_CFM_MEP_DIRECTION_UP,
};

/* CCM interval supported. */
enum br_cfm_ccm_interval {
	BR_CFM_CCM_INTERVAL_NONE,
	BR_CFM_CCM_INTERVAL_3_3_MS,
	BR_CFM_CCM_INTERVAL_10_MS,
	BR_CFM_CCM_INTERVAL_100_MS,
	BR_CFM_CCM_INTERVAL_1_SEC,
	BR_CFM_CCM_INTERVAL_10_SEC,
	BR_CFM_CCM_INTERVAL_1_MIN,
	BR_CFM_CCM_INTERVAL_10_MIN,
};

#endif
