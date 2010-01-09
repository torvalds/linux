/*******************************************************************************

  Intel 82599 Virtual Function driver
  Copyright(c) 1999 - 2009 Intel Corporation.

  This program is free software; you can redistribute it and/or modify it
  under the terms and conditions of the GNU General Public License,
  version 2, as published by the Free Software Foundation.

  This program is distributed in the hope it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
  more details.

  You should have received a copy of the GNU General Public License along with
  this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.

  The full GNU General Public License is included in this distribution in
  the file called "COPYING".

  Contact Information:
  e1000-devel Mailing List <e1000-devel@lists.sourceforge.net>
  Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497

*******************************************************************************/

#ifndef _IXGBE_MBX_H_
#define _IXGBE_MBX_H_

#include "vf.h"

#define IXGBE_VFMAILBOX_SIZE        16 /* 16 32 bit words - 64 bytes */
#define IXGBE_ERR_MBX               -100

#define IXGBE_VFMAILBOX             0x002FC
#define IXGBE_VFMBMEM               0x00200

/* Define mailbox register bits */
#define IXGBE_VFMAILBOX_REQ      0x00000001 /* Request for PF Ready bit */
#define IXGBE_VFMAILBOX_ACK      0x00000002 /* Ack PF message received */
#define IXGBE_VFMAILBOX_VFU      0x00000004 /* VF owns the mailbox buffer */
#define IXGBE_VFMAILBOX_PFU      0x00000008 /* PF owns the mailbox buffer */
#define IXGBE_VFMAILBOX_PFSTS    0x00000010 /* PF wrote a message in the MB */
#define IXGBE_VFMAILBOX_PFACK    0x00000020 /* PF ack the previous VF msg */
#define IXGBE_VFMAILBOX_RSTI     0x00000040 /* PF has reset indication */
#define IXGBE_VFMAILBOX_RSTD     0x00000080 /* PF has indicated reset done */
#define IXGBE_VFMAILBOX_R2C_BITS 0x000000B0 /* All read to clear bits */

#define IXGBE_PFMAILBOX(x)          (0x04B00 + (4 * x))
#define IXGBE_PFMBMEM(vfn)          (0x13000 + (64 * vfn))

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
					       *  clear to send requests */
#define IXGBE_VT_MSGINFO_SHIFT    16
/* bits 23:16 are used for exra info for certain messages */
#define IXGBE_VT_MSGINFO_MASK     (0xFF << IXGBE_VT_MSGINFO_SHIFT)

#define IXGBE_VF_RESET            0x01 /* VF requests reset */
#define IXGBE_VF_SET_MAC_ADDR     0x02 /* VF requests PF to set MAC addr */
#define IXGBE_VF_SET_MULTICAST    0x03 /* VF requests PF to set MC addr */
#define IXGBE_VF_SET_VLAN         0x04 /* VF requests PF to set VLAN */
#define IXGBE_VF_SET_LPE          0x05 /* VF requests PF to set VMOLR.LPE */

/* length of permanent address message returned from PF */
#define IXGBE_VF_PERMADDR_MSG_LEN 4
/* word in permanent address message with the current multicast type */
#define IXGBE_VF_MC_TYPE_WORD     3

#define IXGBE_PF_CONTROL_MSG      0x0100 /* PF control message */

#define IXGBE_VF_MBX_INIT_TIMEOUT 2000 /* number of retries on mailbox */
#define IXGBE_VF_MBX_INIT_DELAY   500  /* microseconds between retries */

/* forward declaration of the HW struct */
struct ixgbe_hw;

s32 ixgbevf_init_mbx_params_vf(struct ixgbe_hw *);

#endif /* _IXGBE_MBX_H_ */
