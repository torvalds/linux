/*
 * isdnhdlc.h  --  General purpose ISDN HDLC decoder.
 *
 * Implementation of a HDLC decoder/encoder in software.
 * Neccessary because some ISDN devices don't have HDLC
 * controllers. Also included: a bit reversal table.
 *
 *Copyright (C) 2002    Wolfgang Mües      <wolfgang@iksw-muees.de>
 *		2001 	Frode Isaksen      <fisaksen@bewan.com>
 *              2001 	Kai Germaschewski  <kai.germaschewski@gmx.de>
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
 */

#ifndef __ISDNHDLC_H__
#define __ISDNHDLC_H__

struct isdnhdlc_vars {
	int bit_shift;
	int hdlc_bits1;
	int data_bits;
	int ffbit_shift; 	// encoding only
	int state;
	int dstpos;

	unsigned short crc;

	unsigned char cbin;
	unsigned char shift_reg;
	unsigned char ffvalue;

	int data_received:1; 	// set if transferring data
	int dchannel:1; 	// set if D channel (send idle instead of flags)
	int do_adapt56:1; 	// set if 56K adaptation
        int do_closing:1; 	// set if in closing phase (need to send CRC + flag
};


/*
  The return value from isdnhdlc_decode is
  the frame length, 0 if no complete frame was decoded,
  or a negative error number
*/
#define HDLC_FRAMING_ERROR     1
#define HDLC_CRC_ERROR         2
#define HDLC_LENGTH_ERROR      3

extern const unsigned char isdnhdlc_bit_rev_tab[256];

extern void isdnhdlc_rcv_init (struct isdnhdlc_vars *hdlc, int do_adapt56);

extern int isdnhdlc_decode (struct isdnhdlc_vars *hdlc, const unsigned char *src, int slen,int *count,
	                    unsigned char *dst, int dsize);

extern void isdnhdlc_out_init (struct isdnhdlc_vars *hdlc,int is_d_channel,int do_adapt56);

extern int isdnhdlc_encode (struct isdnhdlc_vars *hdlc,const unsigned char *src,unsigned short slen,int *count,
	                    unsigned char *dst,int dsize);

#endif /* __ISDNHDLC_H__ */
