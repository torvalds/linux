/* SPDX-License-Identifier: GPL-2.0 */
/*******************************************************************************

  Intel 82599 Virtual Function driver
  Copyright(c) 1999 - 2015 Intel Corporation.

  This program is free software; you can redistribute it and/or modify it
  under the terms and conditions of the GNU General Public License,
  version 2, as published by the Free Software Foundation.

  This program is distributed in the hope it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
  more details.

  You should have received a copy of the GNU General Public License along with
  this program; if not, see <http://www.gnu.org/licenses/>.

  The full GNU General Public License is included in this distribution in
  the file called "COPYING".

  Contact Information:
  e1000-devel Mailing List <e1000-devel@lists.sourceforge.net>
  Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497

*******************************************************************************/

#ifndef _IXGBE_MBX_H_
#define _IXGBE_MBX_H_

#include "vf.h"

#define IXGBE_VFMAILBOX_SIZE	16 /* 16 32 bit words - 64 bytes */
#define IXGBE_ERR_MBX		-100

#define IXGBE_VFMAILBOX		0x002FC
#define IXGBE_VFMBMEM		0x00200

/* Define mailbox register bits */
#define IXGBE_VFMAILBOX_REQ	0x00000001 /* Request for PF Ready bit */
#define IXGBE_VFMAILBOX_ACK	0x00000002 /* Ack PF message received */
#define IXGBE_VFMAILBOX_VFU	0x00000004 /* VF owns the mailbox buffer */
#define IXGBE_VFMAILBOX_PFU	0x00000008 /* PF owns the mailbox buffer */
#define IXGBE_VFMAILBOX_PFSTS	0x00000010 /* PF wrote a message in the MB */
#define IXGBE_VFMAILBOX_PFACK	0x00000020 /* PF ack the previous VF msg */
#define IXGBE_VFMAILBOX_RSTI	0x00000040 /* PF has reset indication */
#define IXGBE_VFMAILBOX_RSTD	0x00000080 /* PF has indicated reset done */
#define IXGBE_VFMAILBOX_R2C_BITS 0x000000B0 /* All read to clear bits */

#define IXGBE_PFMAILBOX(x)	(0x04B00 + (4 * (x)))
#define IXGBE_PFMBMEM(vfn)	(0x13000 + (64 * (vfn)))

#define IXGBE_PFMAILBOX_STS	0x00000001 /* Initiate message send to VF */
#define IXGBE_PFMAILBOX_ACK	0x00000002 /* Ack message recv'd from VF */
#define IXGBE_PFMAILBOX_VFU	0x00000004 /* VF owns the mailbox buffer */
#define IXGBE_PFMAILBOX_PFU	0x00000008 /* PF owns the mailbox buffer */
#define IXGBE_PFMAILBOX_RVFU	0x00000010 /* Reset VFU - used when VF stuck */

#define IXGBE_MBVFICR_VFREQ_MASK 0x0000FFFF /* bits for VF messages */
#define IXGBE_MBVFICR_VFREQ_VF1	0x00000001 /* bit for VF 1 message */
#define IXGBE_MBVFICR_VFACK_MASK 0xFFFF0000 /* bits for VF acks */
#define IXGBE_MBVFICR_VFACK_VF1	0x00010000 /* bit for VF 1 ack */

/* If it's a IXGBE_VF_* msg then it originates in the VF and is sent to the
 * PF.  The reverse is true if it is IXGBE_PF_*.
 * Message ACK's are the value or'd with 0xF0000000
 */
/* Messages below or'd with this are the ACK */
#define IXGBE_VT_MSGTYPE_ACK	0x80000000
/* Messages below or'd with this are the NACK */
#define IXGBE_VT_MSGTYPE_NACK	0x40000000
/* Indicates that VF is still clear to send requests */
#define IXGBE_VT_MSGTYPE_CTS	0x20000000
#define IXGBE_VT_MSGINFO_SHIFT	16
/* bits 23:16 are used for exra info for certain messages */
#define IXGBE_VT_MSGINFO_MASK	(0xFF << IXGBE_VT_MSGINFO_SHIFT)

/* definitions to support mailbox API version negotiation */

/* each element denotes a version of the API; existing numbers may not
 * change; any additions must go at the end
 */
enum ixgbe_pfvf_api_rev {
	ixgbe_mbox_api_10,	/* API version 1.0, linux/freebsd VF driver */
	ixgbe_mbox_api_20,	/* API version 2.0, solaris Phase1 VF driver */
	ixgbe_mbox_api_11,	/* API version 1.1, linux/freebsd VF driver */
	ixgbe_mbox_api_12,	/* API version 1.2, linux/freebsd VF driver */
	ixgbe_mbox_api_13,	/* API version 1.3, linux/freebsd VF driver */
	/* This value should always be last */
	ixgbe_mbox_api_unknown,	/* indicates that API version is not known */
};

/* mailbox API, legacy requests */
#define IXGBE_VF_RESET		0x01 /* VF requests reset */
#define IXGBE_VF_SET_MAC_ADDR	0x02 /* VF requests PF to set MAC addr */
#define IXGBE_VF_SET_MULTICAST	0x03 /* VF requests PF to set MC addr */
#define IXGBE_VF_SET_VLAN	0x04 /* VF requests PF to set VLAN */

/* mailbox API, version 1.0 VF requests */
#define IXGBE_VF_SET_LPE	0x05 /* VF requests PF to set VMOLR.LPE */
#define IXGBE_VF_SET_MACVLAN	0x06 /* VF requests PF for unicast filter */
#define IXGBE_VF_API_NEGOTIATE	0x08 /* negotiate API version */

/* mailbox API, version 1.1 VF requests */
#define IXGBE_VF_GET_QUEUE	0x09 /* get queue configuration */

/* GET_QUEUES return data indices within the mailbox */
#define IXGBE_VF_TX_QUEUES	1 /* number of Tx queues supported */
#define IXGBE_VF_RX_QUEUES	2 /* number of Rx queues supported */
#define IXGBE_VF_TRANS_VLAN	3 /* Indication of port VLAN */
#define IXGBE_VF_DEF_QUEUE	4 /* Default queue offset */

/* mailbox API, version 1.2 VF requests */
#define IXGBE_VF_GET_RETA	0x0a	/* VF request for RETA */
#define IXGBE_VF_GET_RSS_KEY	0x0b	/* get RSS hash key */

#define IXGBE_VF_UPDATE_XCAST_MODE	0x0c

/* length of permanent address message returned from PF */
#define IXGBE_VF_PERMADDR_MSG_LEN	4
/* word in permanent address message with the current multicast type */
#define IXGBE_VF_MC_TYPE_WORD		3

#define IXGBE_PF_CONTROL_MSG		0x0100 /* PF control message */

#define IXGBE_VF_MBX_INIT_TIMEOUT	2000 /* number of retries on mailbox */
#define IXGBE_VF_MBX_INIT_DELAY		500  /* microseconds between retries */

/* forward declaration of the HW struct */
struct ixgbe_hw;

#endif /* _IXGBE_MBX_H_ */
