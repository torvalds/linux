/*****************************************************************************
 *                                                                           *
 * File: cpl5_cmd.h                                                          *
 * $Revision: 1.4 $                                                          *
 * $Date: 2005/03/23 07:15:58 $                                              *
 * Description:                                                              *
 *  part of the Chelsio 10Gb Ethernet Driver.                                *
 *                                                                           *
 * This program is free software; you can redistribute it and/or modify      *
 * it under the terms of the GNU General Public License, version 2, as       *
 * published by the Free Software Foundation.                                *
 *                                                                           *
 * You should have received a copy of the GNU General Public License along   *
 * with this program; if not, write to the Free Software Foundation, Inc.,   *
 * 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.                 *
 *                                                                           *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR IMPLIED    *
 * WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED WARRANTIES OF      *
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.                     *
 *                                                                           *
 * http://www.chelsio.com                                                    *
 *                                                                           *
 * Copyright (c) 2003 - 2005 Chelsio Communications, Inc.                    *
 * All rights reserved.                                                      *
 *                                                                           *
 * Maintainers: maintainers@chelsio.com                                      *
 *                                                                           *
 * Authors: Dimitrios Michailidis   <dm@chelsio.com>                         *
 *          Tina Yang               <tainay@chelsio.com>                     *
 *          Felix Marti             <felix@chelsio.com>                      *
 *          Scott Bardone           <sbardone@chelsio.com>                   *
 *          Kurt Ottaway            <kottaway@chelsio.com>                   *
 *          Frank DiMambro          <frank@chelsio.com>                      *
 *                                                                           *
 * History:                                                                  *
 *                                                                           *
 ****************************************************************************/

#ifndef _CPL5_CMD_H
#define _CPL5_CMD_H

#include <asm/byteorder.h>

#if !defined(__LITTLE_ENDIAN_BITFIELD) && !defined(__BIG_ENDIAN_BITFIELD)
#error "Adjust your <asm/byteorder.h> defines"
#endif

enum CPL_opcode {
	CPL_RX_PKT            = 0xAD,
	CPL_TX_PKT            = 0xB2,
	CPL_TX_PKT_LSO        = 0xB6,
};

enum {                /* TX_PKT_LSO ethernet types */
	CPL_ETH_II,
	CPL_ETH_II_VLAN,
	CPL_ETH_802_3,
	CPL_ETH_802_3_VLAN
};

struct cpl_rx_data {
	__u32 rsvd0;
	__u32 len;
	__u32 seq;
	__u16 urg;
	__u8  rsvd1;
	__u8  status;
};

/*
 * We want this header's alignment to be no more stringent than 2-byte aligned.
 * All fields are u8 or u16 except for the length.  However that field is not
 * used so we break it into 2 16-bit parts to easily meet our alignment needs.
 */
struct cpl_tx_pkt {
	__u8 opcode;
#if defined(__LITTLE_ENDIAN_BITFIELD)
	__u8 iff:4;
	__u8 ip_csum_dis:1;
	__u8 l4_csum_dis:1;
	__u8 vlan_valid:1;
	__u8 rsvd:1;
#else
	__u8 rsvd:1;
	__u8 vlan_valid:1;
	__u8 l4_csum_dis:1;
	__u8 ip_csum_dis:1;
	__u8 iff:4;
#endif
	__u16 vlan;
	__u16 len_hi;
	__u16 len_lo;
};

struct cpl_tx_pkt_lso {
	__u8 opcode;
#if defined(__LITTLE_ENDIAN_BITFIELD)
	__u8 iff:4;
	__u8 ip_csum_dis:1;
	__u8 l4_csum_dis:1;
	__u8 vlan_valid:1;
	__u8 rsvd:1;
#else
	__u8 rsvd:1;
	__u8 vlan_valid:1;
	__u8 l4_csum_dis:1;
	__u8 ip_csum_dis:1;
	__u8 iff:4;
#endif
	__u16 vlan;
	__u32 len;

	__u32 rsvd2;
	__u8 rsvd3;
#if defined(__LITTLE_ENDIAN_BITFIELD)
	__u8 tcp_hdr_words:4;
	__u8 ip_hdr_words:4;
#else
	__u8 ip_hdr_words:4;
	__u8 tcp_hdr_words:4;
#endif
	__u16 eth_type_mss;
};

struct cpl_rx_pkt {
	__u8 opcode;
#if defined(__LITTLE_ENDIAN_BITFIELD)
	__u8 iff:4;
	__u8 csum_valid:1;
	__u8 bad_pkt:1;
	__u8 vlan_valid:1;
	__u8 rsvd:1;
#else
	__u8 rsvd:1;
	__u8 vlan_valid:1;
	__u8 bad_pkt:1;
	__u8 csum_valid:1;
	__u8 iff:4;
#endif
	__u16 csum;
	__u16 vlan;
	__u16 len;
};

#endif
