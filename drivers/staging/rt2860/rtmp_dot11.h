/*
 *************************************************************************
 * Ralink Tech Inc.
 * 5F., No.36, Taiyuan St., Jhubei City,
 * Hsinchu County 302,
 * Taiwan, R.O.C.
 *
 * (c) Copyright 2002-2007, Ralink Technology, Inc.
 *
 * This program is free software; you can redistribute it and/or modify  *
 * it under the terms of the GNU General Public License as published by  *
 * the Free Software Foundation; either version 2 of the License, or     *
 * (at your option) any later version.                                   *
 *                                                                       *
 * This program is distributed in the hope that it will be useful,       *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 * GNU General Public License for more details.                          *
 *                                                                       *
 * You should have received a copy of the GNU General Public License     *
 * along with this program; if not, write to the                         *
 * Free Software Foundation, Inc.,                                       *
 * 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 *                                                                       *
 *************************************************************************
*/

#ifndef __DOT11_BASE_H__
#define __DOT11_BASE_H__

#include "rtmp_type.h"

/* 4-byte HTC field.  maybe included in any frame except non-QOS data frame.  The Order bit must set 1. */
struct PACKED rt_ht_control {
	u32 MA:1;		/*management action payload exist in (QoS Null+HTC) */
	u32 TRQ:1;		/*sounding request */
	u32 MRQ:1;		/*MCS feedback. Request for a MCS feedback */
	u32 MRSorASI:3;	/* MRQ Sequence identifier. unchanged during entire procedure. 0x000-0x110. */
	u32 MFS:3;		/*SET to the received value of MRS. 0x111 for unsolicited MFB. */
	u32 MFBorASC:7;	/*Link adaptation feedback containing recommended MCS. 0x7f for no feedback or not available */
	u32 CalPos:2;	/* calibration position */
	u32 CalSeq:2;	/*calibration sequence */
	u32 FBKReq:2;	/*feedback request */
	u32 CSISTEERING:2;	/*CSI/ STEERING */
	u32 ZLFAnnouce:1;	/* ZLF announcement */
	u32 rsv:5;		/*calibration sequence */
	u32 ACConstraint:1;	/*feedback request */
	u32 RDG:1;		/*RDG / More PPDU */
};

/* 2-byte QOS CONTROL field */
struct PACKED rt_qos_control {
	u16 TID:4;
	u16 EOSP:1;
	u16 AckPolicy:2;	/*0: normal ACK 1:No ACK 2:scheduled under MTBA/PSMP  3: BA */
	u16 AMsduPresent:1;
	u16 Txop_QueueSize:8;
};

/* 2-byte Frame control field */
struct PACKED rt_frame_control {
	u16 Ver:2;		/* Protocol version */
	u16 Type:2;		/* MSDU type */
	u16 SubType:4;	/* MSDU subtype */
	u16 ToDs:1;		/* To DS indication */
	u16 FrDs:1;		/* From DS indication */
	u16 MoreFrag:1;	/* More fragment bit */
	u16 Retry:1;		/* Retry status bit */
	u16 PwrMgmt:1;	/* Power management bit */
	u16 MoreData:1;	/* More data bit */
	u16 Wep:1;		/* Wep data */
	u16 Order:1;		/* Strict order expected */
};

struct PACKED rt_header_802_11 {
	struct rt_frame_control FC;
	u16 Duration;
	u8 Addr1[MAC_ADDR_LEN];
	u8 Addr2[MAC_ADDR_LEN];
	u8 Addr3[MAC_ADDR_LEN];
	u16 Frag:4;
	u16 Sequence:12;
	u8 Octet[0];
};

struct PACKED rt_pspoll_frame {
	struct rt_frame_control FC;
	u16 Aid;
	u8 Bssid[MAC_ADDR_LEN];
	u8 Ta[MAC_ADDR_LEN];
};

struct PACKED rt_rts_frame {
	struct rt_frame_control FC;
	u16 Duration;
	u8 Addr1[MAC_ADDR_LEN];
	u8 Addr2[MAC_ADDR_LEN];
};

#endif /* __DOT11_BASE_H__ // */
