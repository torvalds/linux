/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// MAC_Structures.h
//
// This file contains the definitions and data structures used by SW-MAC.
//
// Revision Histoy
//=================
// 0.1      2002        UN00
// 0.2      20021004    PD43 CCLiu6
//          20021018    PD43 CCLiu6
//                      Add enum_TxRate type
//                      Modify enum_STAState type
// 0.3      20021023    PE23 CYLiu update MAC session struct
//          20021108
//          20021122    PD43 Austin
//                      Deleted some unused.
//          20021129    PD43 Austin
//			20030617	increase the 802.11g definition
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/

#ifndef _MAC_Structures_H_
#define _MAC_Structures_H_

#define MAC_ADDR_LENGTH                     6

/* ========================================================
// 802.11 Frame define
//----- */
#define DOT_11_MAC_HEADER_SIZE		24
#define DOT_11_SNAP_SIZE			6
#define DOT_11_DURATION_OFFSET		2
/* Sequence control offset */
#define DOT_11_SEQUENCE_OFFSET		22
/* The start offset of 802.11 Frame// */
#define DOT_11_TYPE_OFFSET			30
#define DOT_11_DATA_OFFSET          24
#define DOT_11_DA_OFFSET			4

#define MAX_ETHERNET_PACKET_SIZE		1514

/* -----  management : Type of Bits (2, 3) and Subtype of Bits (4, 5, 6, 7) */
#define MAC_SUBTYPE_MNGMNT_ASSOC_REQUEST    0x00
#define MAC_SUBTYPE_MNGMNT_ASSOC_RESPONSE   0x10
#define MAC_SUBTYPE_MNGMNT_REASSOC_REQUEST  0x20
#define MAC_SUBTYPE_MNGMNT_REASSOC_RESPONSE 0x30
#define MAC_SUBTYPE_MNGMNT_PROBE_REQUEST    0x40
#define MAC_SUBTYPE_MNGMNT_PROBE_RESPONSE   0x50
#define MAC_SUBTYPE_MNGMNT_BEACON           0x80
#define MAC_SUBTYPE_MNGMNT_ATIM             0x90
#define MAC_SUBTYPE_MNGMNT_DISASSOCIATION   0xA0
#define MAC_SUBTYPE_MNGMNT_AUTHENTICATION   0xB0
#define MAC_SUBTYPE_MNGMNT_DEAUTHENTICATION 0xC0

#define RATE_AUTO					0
#define RATE_1M						2
#define RATE_2M						4
#define RATE_5dot5M					11
#define RATE_6M						12
#define RATE_9M						18
#define RATE_11M					22
#define RATE_12M					24
#define RATE_18M					36
#define RATE_22M					44
#define RATE_24M					48
#define RATE_33M					66
#define RATE_36M					72
#define RATE_48M					96
#define RATE_54M					108
#define RATE_MAX					255

#endif /* _MAC_Structure_H_ */
