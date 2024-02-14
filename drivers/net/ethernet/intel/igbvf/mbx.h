/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 1999 - 2018 Intel Corporation. */

#ifndef _E1000_MBX_H_
#define _E1000_MBX_H_

#include "vf.h"

#define E1000_V2PMAILBOX_REQ	0x00000001 /* Request for PF Ready bit */
#define E1000_V2PMAILBOX_ACK	0x00000002 /* Ack PF message received */
#define E1000_V2PMAILBOX_VFU	0x00000004 /* VF owns the mailbox buffer */
#define E1000_V2PMAILBOX_PFU	0x00000008 /* PF owns the mailbox buffer */
#define E1000_V2PMAILBOX_PFSTS	0x00000010 /* PF wrote a message in the MB */
#define E1000_V2PMAILBOX_PFACK	0x00000020 /* PF ack the previous VF msg */
#define E1000_V2PMAILBOX_RSTI	0x00000040 /* PF has reset indication */
#define E1000_V2PMAILBOX_RSTD	0x00000080 /* PF has indicated reset done */
#define E1000_V2PMAILBOX_R2C_BITS 0x000000B0 /* All read to clear bits */

#define E1000_VFMAILBOX_SIZE	16 /* 16 32 bit words - 64 bytes */

/* If it's a E1000_VF_* msg then it originates in the VF and is sent to the
 * PF.  The reverse is true if it is E1000_PF_*.
 * Message ACK's are the value or'd with 0xF0000000
 */
/* Messages below or'd with this are the ACK */
#define E1000_VT_MSGTYPE_ACK	0x80000000
/* Messages below or'd with this are the NACK */
#define E1000_VT_MSGTYPE_NACK	0x40000000
/* Indicates that VF is still clear to send requests */
#define E1000_VT_MSGTYPE_CTS	0x20000000

/* We have a total wait time of 1s for vf mailbox posted messages */
#define E1000_VF_MBX_INIT_TIMEOUT	2000 /* retry count for mbx timeout */
#define E1000_VF_MBX_INIT_DELAY		500  /* usec delay between retries */

#define E1000_VT_MSGINFO_SHIFT	16
/* bits 23:16 are used for exra info for certain messages */
#define E1000_VT_MSGINFO_MASK	(0xFF << E1000_VT_MSGINFO_SHIFT)

#define E1000_VF_RESET		0x01 /* VF requests reset */
#define E1000_VF_SET_MAC_ADDR	0x02 /* VF requests PF to set MAC addr */
/* VF requests PF to clear all unicast MAC filters */
#define E1000_VF_MAC_FILTER_CLR (0x01 << E1000_VT_MSGINFO_SHIFT)
/* VF requests PF to add unicast MAC filter */
#define E1000_VF_MAC_FILTER_ADD (0x02 << E1000_VT_MSGINFO_SHIFT)
#define E1000_VF_SET_MULTICAST	0x03 /* VF requests PF to set MC addr */
#define E1000_VF_SET_VLAN	0x04 /* VF requests PF to set VLAN */
#define E1000_VF_SET_LPE	0x05 /* VF requests PF to set VMOLR.LPE */

#define E1000_PF_CONTROL_MSG	0x0100 /* PF control message */

void e1000_init_mbx_ops_generic(struct e1000_hw *hw);
s32 e1000_init_mbx_params_vf(struct e1000_hw *);

#endif /* _E1000_MBX_H_ */
