/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 1999 - 2018 Intel Corporation. */

#ifndef _IXGBE_MBX_H_
#define _IXGBE_MBX_H_

#include "ixgbe_type.h"

#define IXGBE_VFMAILBOX_SIZE        16 /* 16 32 bit words - 64 bytes */

#define IXGBE_VFMAILBOX             0x002FC
#define IXGBE_VFMBMEM               0x00200

#define IXGBE_PFMAILBOX_STS   0x00000001 /* Initiate message send to VF */
#define IXGBE_PFMAILBOX_ACK   0x00000002 /* Ack message recv'd from VF */
#define IXGBE_PFMAILBOX_VFU   0x00000004 /* VF owns the mailbox buffer */
#define IXGBE_PFMAILBOX_PFU   0x00000008 /* PF owns the mailbox buffer */
#define IXGBE_PFMAILBOX_RVFU  0x00000010 /* Reset VFU - used when VF stuck */

#define IXGBE_MBVFICR_VFREQ_MASK 0x0000FFFF /* bits for VF messages */
#define IXGBE_MBVFICR_VFREQ_VF1  0x00000001 /* bit for VF 1 message */
#define IXGBE_MBVFICR_VFACK_MASK 0xFFFF0000 /* bits for VF acks */
#define IXGBE_MBVFICR_VFACK_VF1  0x00010000 /* bit for VF 1 ack */


/* If it's a IXGBE_VF_* msg then it originates in the VF and is sent to the
 * PF.  The reverse is true if it is IXGBE_PF_*.
 * Message ACK's are the value or'd with 0xF0000000
 */
#define IXGBE_VT_MSGTYPE_ACK      0x80000000  /* Messages below or'd with
					       * this are the ACK */
#define IXGBE_VT_MSGTYPE_NACK     0x40000000  /* Messages below or'd with
					       * this are the NACK */
#define IXGBE_VT_MSGTYPE_CTS      0x20000000  /* Indicates that VF is still
						 clear to send requests */
#define IXGBE_VT_MSGINFO_SHIFT    16
/* bits 23:16 are used for exra info for certain messages */
#define IXGBE_VT_MSGINFO_MASK     (0xFF << IXGBE_VT_MSGINFO_SHIFT)

/* definitions to support mailbox API version negotiation */

/*
 * Each element denotes a version of the API; existing numbers may not
 * change; any additions must go at the end
 */
enum ixgbe_pfvf_api_rev {
	ixgbe_mbox_api_10,	/* API version 1.0, linux/freebsd VF driver */
	ixgbe_mbox_api_20,	/* API version 2.0, solaris Phase1 VF driver */
	ixgbe_mbox_api_11,	/* API version 1.1, linux/freebsd VF driver */
	ixgbe_mbox_api_12,	/* API version 1.2, linux/freebsd VF driver */
	ixgbe_mbox_api_13,	/* API version 1.3, linux/freebsd VF driver */
	ixgbe_mbox_api_14,	/* API version 1.4, linux/freebsd VF driver */
	/* This value should always be last */
	ixgbe_mbox_api_unknown,	/* indicates that API version is not known */
};

/* mailbox API, legacy requests */
#define IXGBE_VF_RESET            0x01 /* VF requests reset */
#define IXGBE_VF_SET_MAC_ADDR     0x02 /* VF requests PF to set MAC addr */
#define IXGBE_VF_SET_MULTICAST    0x03 /* VF requests PF to set MC addr */
#define IXGBE_VF_SET_VLAN         0x04 /* VF requests PF to set VLAN */

/* mailbox API, version 1.0 VF requests */
#define IXGBE_VF_SET_LPE	0x05 /* VF requests PF to set VMOLR.LPE */
#define IXGBE_VF_SET_MACVLAN	0x06 /* VF requests PF for unicast filter */
#define IXGBE_VF_API_NEGOTIATE	0x08 /* negotiate API version */

/* mailbox API, version 1.1 VF requests */
#define IXGBE_VF_GET_QUEUES	0x09 /* get queue configuration */

/* GET_QUEUES return data indices within the mailbox */
#define IXGBE_VF_TX_QUEUES	1	/* number of Tx queues supported */
#define IXGBE_VF_RX_QUEUES	2	/* number of Rx queues supported */
#define IXGBE_VF_TRANS_VLAN	3	/* Indication of port vlan */
#define IXGBE_VF_DEF_QUEUE	4	/* Default queue offset */

/* mailbox API, version 1.2 VF requests */
#define IXGBE_VF_GET_RETA	0x0a	/* VF request for RETA */
#define IXGBE_VF_GET_RSS_KEY	0x0b	/* get RSS key */

#define IXGBE_VF_UPDATE_XCAST_MODE	0x0c

/* mailbox API, version 1.4 VF requests */
#define IXGBE_VF_IPSEC_ADD	0x0d
#define IXGBE_VF_IPSEC_DEL	0x0e

#define IXGBE_VF_GET_LINK_STATE 0x10 /* get vf link state */

/* length of permanent address message returned from PF */
#define IXGBE_VF_PERMADDR_MSG_LEN 4
/* word in permanent address message with the current multicast type */
#define IXGBE_VF_MC_TYPE_WORD     3

#define IXGBE_PF_CONTROL_MSG      0x0100 /* PF control message */

#define IXGBE_VF_MBX_INIT_TIMEOUT 2000 /* number of retries on mailbox */
#define IXGBE_VF_MBX_INIT_DELAY   500  /* microseconds between retries */

int ixgbe_read_mbx(struct ixgbe_hw *, u32 *, u16, u16);
int ixgbe_write_mbx(struct ixgbe_hw *, u32 *, u16, u16);
int ixgbe_check_for_msg(struct ixgbe_hw *, u16);
int ixgbe_check_for_ack(struct ixgbe_hw *, u16);
int ixgbe_check_for_rst(struct ixgbe_hw *, u16);
#ifdef CONFIG_PCI_IOV
void ixgbe_init_mbx_params_pf(struct ixgbe_hw *);
#endif /* CONFIG_PCI_IOV */

extern const struct ixgbe_mbx_operations mbx_ops_generic;

#endif /* _IXGBE_MBX_H_ */
