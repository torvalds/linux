/****************************************************************************
 *******                                                              *******
 *******            P A C K E T   H E A D E R   F I L E
 *******                                                              *******
 ****************************************************************************

 Author  : Ian Nandhra / Jeremy Rolls
 Date    :

 *
 *  (C) 1990 - 2000 Specialix International Ltd., Byfleet, Surrey, UK.
 *
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2 of the License, or
 *      (at your option) any later version.
 *
 *      This program is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License
 *      along with this program; if not, write to the Free Software
 *      Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

 Version : 0.01


                            Mods
 ----------------------------------------------------------------------------
  Date     By                Description
 ----------------------------------------------------------------------------

 ***************************************************************************/

#ifndef _pkt_h
#define _pkt_h 1


#ifdef SCCS_LABELS
#ifndef lint
/* static char *_rio_pkt_h_sccs = "@(#)pkt.h	1.8"; */
#endif
#endif

#define MAX_TTL         0xf
#define PKT_CMD_BIT     ((ushort) 0x080)
#define PKT_CMD_DATA    ((ushort) 0x080)

#define PKT_ACK         ((ushort) 0x040)

#define PKT_TGL         ((ushort) 0x020)

#define PKT_LEN_MASK    ((ushort) 0x07f)

#define DATA_WNDW       ((ushort) 0x10)
#define PKT_TTL_MASK    ((ushort) 0x0f)

#define PKT_MAX_DATA_LEN   72

#define PKT_LENGTH         sizeof(struct PKT)
#define SYNC_PKT_LENGTH    (PKT_LENGTH + 4)

#define CONTROL_PKT_LEN_MASK PKT_LEN_MASK
#define CONTROL_PKT_CMD_BIT  PKT_CMD_BIT
#define CONTROL_PKT_ACK (PKT_ACK << 8)
#define CONTROL_PKT_TGL (PKT_TGL << 8)
#define CONTROL_PKT_TTL_MASK (PKT_TTL_MASK << 8)
#define CONTROL_DATA_WNDW  (DATA_WNDW << 8)

struct PKT {
#ifdef INKERNEL
	BYTE dest_unit;		/* Destination Unit Id */
	BYTE dest_port;		/* Destination POrt */
	BYTE src_unit;		/* Source Unit Id */
	BYTE src_port;		/* Source POrt */
#else
	union {
		ushort destination;	/* Complete destination */
		struct {
			unsigned char unit;	/* Destination unit */
			unsigned char port;	/* Destination port */
		} s1;
	} u1;
	union {
		ushort source;	/* Complete source */
		struct {
			unsigned char unit;	/* Source unit */
			unsigned char port;	/* Source port */
		} s2;
	} u2;
#endif
#ifdef INKERNEL
	BYTE len;
	BYTE control;
#else
	union {
		ushort control;
		struct {
			unsigned char len;
			unsigned char control;
		} s3;
	} u3;
#endif
	BYTE data[PKT_MAX_DATA_LEN];
	/* Actual data :-) */
	WORD csum;		/* C-SUM */
};
#endif

/*********** end of file ***********/
