/*******************************************************************************

  Intel(R) Gigabit Ethernet Linux driver
  Copyright(c) 2007-2013 Intel Corporation.

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

#ifndef _E1000_MBX_H_
#define _E1000_MBX_H_

#include "e1000_hw.h"

#define E1000_P2VMAILBOX_STS   0x00000001 /* Initiate message send to VF */
#define E1000_P2VMAILBOX_ACK   0x00000002 /* Ack message recv'd from VF */
#define E1000_P2VMAILBOX_VFU   0x00000004 /* VF owns the mailbox buffer */
#define E1000_P2VMAILBOX_PFU   0x00000008 /* PF owns the mailbox buffer */
#define E1000_P2VMAILBOX_RVFU  0x00000010 /* Reset VFU - used when VF stuck */

#define E1000_MBVFICR_VFREQ_MASK 0x000000FF /* bits for VF messages */
#define E1000_MBVFICR_VFREQ_VF1  0x00000001 /* bit for VF 1 message */
#define E1000_MBVFICR_VFACK_MASK 0x00FF0000 /* bits for VF acks */
#define E1000_MBVFICR_VFACK_VF1  0x00010000 /* bit for VF 1 ack */

#define E1000_VFMAILBOX_SIZE   16 /* 16 32 bit words - 64 bytes */

/* If it's a E1000_VF_* msg then it originates in the VF and is sent to the
 * PF.  The reverse is true if it is E1000_PF_*.
 * Message ACK's are the value or'd with 0xF0000000
 */
#define E1000_VT_MSGTYPE_ACK      0x80000000  /* Messages below or'd with
                                               * this are the ACK */
#define E1000_VT_MSGTYPE_NACK     0x40000000  /* Messages below or'd with
                                               * this are the NACK */
#define E1000_VT_MSGTYPE_CTS      0x20000000  /* Indicates that VF is still
                                                 clear to send requests */
#define E1000_VT_MSGINFO_SHIFT    16
/* bits 23:16 are used for exra info for certain messages */
#define E1000_VT_MSGINFO_MASK     (0xFF << E1000_VT_MSGINFO_SHIFT)

#define E1000_VF_RESET            0x01 /* VF requests reset */
#define E1000_VF_SET_MAC_ADDR     0x02 /* VF requests to set MAC addr */
#define E1000_VF_SET_MULTICAST    0x03 /* VF requests to set MC addr */
#define E1000_VF_SET_VLAN         0x04 /* VF requests to set VLAN */
#define E1000_VF_SET_LPE          0x05 /* VF requests to set VMOLR.LPE */
#define E1000_VF_SET_PROMISC      0x06 /*VF requests to clear VMOLR.ROPE/MPME*/
#define E1000_VF_SET_PROMISC_MULTICAST    (0x02 << E1000_VT_MSGINFO_SHIFT)

#define E1000_PF_CONTROL_MSG      0x0100 /* PF control message */

s32 igb_read_mbx(struct e1000_hw *, u32 *, u16, u16);
s32 igb_write_mbx(struct e1000_hw *, u32 *, u16, u16);
s32 igb_check_for_msg(struct e1000_hw *, u16);
s32 igb_check_for_ack(struct e1000_hw *, u16);
s32 igb_check_for_rst(struct e1000_hw *, u16);
s32 igb_init_mbx_params_pf(struct e1000_hw *);

#endif /* _E1000_MBX_H_ */
