/*
 * Copyright (c) 1996, 2003 VIA Networking Technologies, Inc.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * File: tether.h
 *
 * Purpose:
 *
 * Author: Tevin Chen
 *
 * Date: Jan. 28, 1997
 *
 */

#ifndef __TETHER_H__
#define __TETHER_H__

#include <linux/if_ether.h>
#include "ttype.h"

/*---------------------  Export Definitions -------------------------*/
//
// constants
//
#define U_ETHER_ADDR_STR_LEN (ETH_ALEN * 2 + 1)
                                        // Ethernet address string length
#define U_MULTI_ADDR_LEN    8           // multicast address length

#ifdef __BIG_ENDIAN

#define TYPE_MGMT_PROBE_RSP 0x5000

//
// wFrameCtl field in the S802_11Header
//
// NOTE....
//   in network byte order, high byte is going first
#define FC_TODS             0x0001
#define FC_FROMDS           0x0002
#define FC_MOREFRAG         0x0004
#define FC_RETRY            0x0008
#define FC_POWERMGT         0x0010
#define FC_MOREDATA         0x0020
#define FC_WEP              0x0040
#define TYPE_802_11_ATIM    0x9000

#define TYPE_802_11_DATA    0x0800
#define TYPE_802_11_CTL     0x0400
#define TYPE_802_11_MGMT    0x0000
#define TYPE_802_11_MASK    0x0C00
#define TYPE_SUBTYPE_MASK   0xFC00
#define TYPE_802_11_NODATA  0x4000
#define TYPE_DATE_NULL      0x4800

#define TYPE_CTL_PSPOLL     0xa400
#define TYPE_CTL_RTS        0xb400
#define TYPE_CTL_CTS        0xc400
#define TYPE_CTL_ACK        0xd400



#else //if LITTLE_ENDIAN
//
// wType field in the SEthernetHeader
//
// NOTE....
//   in network byte order, high byte is going first

#define TYPE_MGMT_PROBE_RSP 0x0050

//
// wFrameCtl field in the S802_11Header
//
// NOTE....
//   in network byte order, high byte is going first
#define FC_TODS             0x0100
#define FC_FROMDS           0x0200
#define FC_MOREFRAG         0x0400
#define FC_RETRY            0x0800
#define FC_POWERMGT         0x1000
#define FC_MOREDATA         0x2000
#define FC_WEP              0x4000
#define TYPE_802_11_ATIM    0x0090

#define TYPE_802_11_DATA    0x0008
#define TYPE_802_11_CTL     0x0004
#define TYPE_802_11_MGMT    0x0000
#define TYPE_802_11_MASK    0x000C
#define TYPE_SUBTYPE_MASK   0x00FC
#define TYPE_802_11_NODATA  0x0040
#define TYPE_DATE_NULL      0x0048

#define TYPE_CTL_PSPOLL     0x00a4
#define TYPE_CTL_RTS        0x00b4
#define TYPE_CTL_CTS        0x00c4
#define TYPE_CTL_ACK        0x00d4



#endif //#ifdef __BIG_ENDIAN

#define WEP_IV_MASK         0x00FFFFFF

/*---------------------  Export Types  ------------------------------*/
//
// Ethernet packet
//
typedef struct tagSEthernetHeader {
    u8    abyDstAddr[ETH_ALEN];
    u8    abySrcAddr[ETH_ALEN];
    WORD    wType;
} __attribute__ ((__packed__))
SEthernetHeader, *PSEthernetHeader;


//
// 802_3 packet
//
typedef struct tagS802_3Header {
    u8    abyDstAddr[ETH_ALEN];
    u8    abySrcAddr[ETH_ALEN];
    WORD    wLen;
} __attribute__ ((__packed__))
S802_3Header, *PS802_3Header;

//
// 802_11 packet
//
typedef struct tagS802_11Header {
    WORD    wFrameCtl;
    WORD    wDurationID;
    u8    abyAddr1[ETH_ALEN];
    u8    abyAddr2[ETH_ALEN];
    u8    abyAddr3[ETH_ALEN];
    WORD    wSeqCtl;
    u8    abyAddr4[ETH_ALEN];
} __attribute__ ((__packed__))
S802_11Header, *PS802_11Header;

/*---------------------  Export Macros ------------------------------*/

/*---------------------  Export Classes  ----------------------------*/

/*---------------------  Export Variables  --------------------------*/

/*---------------------  Export Functions  --------------------------*/

u8 ETHbyGetHashIndexByCrc32(u8 * pbyMultiAddr);
//u8 ETHbyGetHashIndexByCrc(u8 * pbyMultiAddr);
bool ETHbIsBufferCrc32Ok(u8 * pbyBuffer, unsigned int cbFrameLength);

#endif /* __TETHER_H__ */
