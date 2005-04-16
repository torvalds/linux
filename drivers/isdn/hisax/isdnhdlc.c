/*
 * isdnhdlc.c  --  General purpose ISDN HDLC decoder.
 *
 *Copyright (C) 2002	Wolfgang Mües      <wolfgang@iksw-muees.de>
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

#include <linux/module.h>
#include <linux/init.h>
#include <linux/crc-ccitt.h>
#include "isdnhdlc.h"

/*-------------------------------------------------------------------*/

MODULE_AUTHOR("Wolfgang Mües <wolfgang@iksw-muees.de>, "
	      "Frode Isaksen <fisaksen@bewan.com>, "
	      "Kai Germaschewski <kai.germaschewski@gmx.de>");
MODULE_DESCRIPTION("General purpose ISDN HDLC decoder");
MODULE_LICENSE("GPL");

/*-------------------------------------------------------------------*/

/* bit swap table.
 * Very handy for devices with different bit order,
 * and neccessary for each transparent B-channel access for all
 * devices which works with this HDLC decoder without bit reversal.
 */
const unsigned char isdnhdlc_bit_rev_tab[256] = {
	0x00,0x80,0x40,0xC0,0x20,0xA0,0x60,0xE0,0x10,0x90,0x50,0xD0,0x30,0xB0,0x70,0xF0,
	0x08,0x88,0x48,0xC8,0x28,0xA8,0x68,0xE8,0x18,0x98,0x58,0xD8,0x38,0xB8,0x78,0xF8,
	0x04,0x84,0x44,0xC4,0x24,0xA4,0x64,0xE4,0x14,0x94,0x54,0xD4,0x34,0xB4,0x74,0xF4,
	0x0C,0x8C,0x4C,0xCC,0x2C,0xAC,0x6C,0xEC,0x1C,0x9C,0x5C,0xDC,0x3C,0xBC,0x7C,0xFC,
	0x02,0x82,0x42,0xC2,0x22,0xA2,0x62,0xE2,0x12,0x92,0x52,0xD2,0x32,0xB2,0x72,0xF2,
	0x0A,0x8A,0x4A,0xCA,0x2A,0xAA,0x6A,0xEA,0x1A,0x9A,0x5A,0xDA,0x3A,0xBA,0x7A,0xFA,
	0x06,0x86,0x46,0xC6,0x26,0xA6,0x66,0xE6,0x16,0x96,0x56,0xD6,0x36,0xB6,0x76,0xF6,
	0x0E,0x8E,0x4E,0xCE,0x2E,0xAE,0x6E,0xEE,0x1E,0x9E,0x5E,0xDE,0x3E,0xBE,0x7E,0xFE,
	0x01,0x81,0x41,0xC1,0x21,0xA1,0x61,0xE1,0x11,0x91,0x51,0xD1,0x31,0xB1,0x71,0xF1,
	0x09,0x89,0x49,0xC9,0x29,0xA9,0x69,0xE9,0x19,0x99,0x59,0xD9,0x39,0xB9,0x79,0xF9,
	0x05,0x85,0x45,0xC5,0x25,0xA5,0x65,0xE5,0x15,0x95,0x55,0xD5,0x35,0xB5,0x75,0xF5,
	0x0D,0x8D,0x4D,0xCD,0x2D,0xAD,0x6D,0xED,0x1D,0x9D,0x5D,0xDD,0x3D,0xBD,0x7D,0xFD,
	0x03,0x83,0x43,0xC3,0x23,0xA3,0x63,0xE3,0x13,0x93,0x53,0xD3,0x33,0xB3,0x73,0xF3,
	0x0B,0x8B,0x4B,0xCB,0x2B,0xAB,0x6B,0xEB,0x1B,0x9B,0x5B,0xDB,0x3B,0xBB,0x7B,0xFB,
	0x07,0x87,0x47,0xC7,0x27,0xA7,0x67,0xE7,0x17,0x97,0x57,0xD7,0x37,0xB7,0x77,0xF7,
	0x0F,0x8F,0x4F,0xCF,0x2F,0xAF,0x6F,0xEF,0x1F,0x9F,0x5F,0xDF,0x3F,0xBF,0x7F,0xFF
};

enum {
	HDLC_FAST_IDLE,HDLC_GET_FLAG_B0,HDLC_GETFLAG_B1A6,HDLC_GETFLAG_B7,
	HDLC_GET_DATA,HDLC_FAST_FLAG
};

enum {
	HDLC_SEND_DATA,HDLC_SEND_CRC1,HDLC_SEND_FAST_FLAG,
	HDLC_SEND_FIRST_FLAG,HDLC_SEND_CRC2,HDLC_SEND_CLOSING_FLAG,
	HDLC_SEND_IDLE1,HDLC_SEND_FAST_IDLE,HDLC_SENDFLAG_B0,
	HDLC_SENDFLAG_B1A6,HDLC_SENDFLAG_B7,STOPPED
};

void isdnhdlc_rcv_init (struct isdnhdlc_vars *hdlc, int do_adapt56)
{
   	hdlc->bit_shift = 0;
	hdlc->hdlc_bits1 = 0;
	hdlc->data_bits = 0;
	hdlc->ffbit_shift = 0;
	hdlc->data_received = 0;
	hdlc->state = HDLC_GET_DATA;
	hdlc->do_adapt56 = do_adapt56;
	hdlc->dchannel = 0;
	hdlc->crc = 0;
	hdlc->cbin = 0;
	hdlc->shift_reg = 0;
	hdlc->ffvalue = 0;
	hdlc->dstpos = 0;
}

void isdnhdlc_out_init (struct isdnhdlc_vars *hdlc, int is_d_channel, int do_adapt56)
{
   	hdlc->bit_shift = 0;
	hdlc->hdlc_bits1 = 0;
	hdlc->data_bits = 0;
	hdlc->ffbit_shift = 0;
	hdlc->data_received = 0;
	hdlc->do_closing = 0;
	hdlc->ffvalue = 0;
	if (is_d_channel) {
		hdlc->dchannel = 1;
		hdlc->state = HDLC_SEND_FIRST_FLAG;
	} else {
		hdlc->dchannel = 0;
		hdlc->state = HDLC_SEND_FAST_FLAG;
		hdlc->ffvalue = 0x7e;
	}
	hdlc->cbin = 0x7e;
	hdlc->bit_shift = 0;
	if(do_adapt56){
		hdlc->do_adapt56 = 1;
		hdlc->data_bits = 0;
		hdlc->state = HDLC_SENDFLAG_B0;
	} else {
		hdlc->do_adapt56 = 0;
		hdlc->data_bits = 8;
	}
	hdlc->shift_reg = 0;
}

/*
  isdnhdlc_decode - decodes HDLC frames from a transparent bit stream.

  The source buffer is scanned for valid HDLC frames looking for
  flags (01111110) to indicate the start of a frame. If the start of
  the frame is found, the bit stuffing is removed (0 after 5 1's).
  When a new flag is found, the complete frame has been received
  and the CRC is checked.
  If a valid frame is found, the function returns the frame length
  excluding the CRC with the bit HDLC_END_OF_FRAME set.
  If the beginning of a valid frame is found, the function returns
  the length.
  If a framing error is found (too many 1s and not a flag) the function
  returns the length with the bit HDLC_FRAMING_ERROR set.
  If a CRC error is found the function returns the length with the
  bit HDLC_CRC_ERROR set.
  If the frame length exceeds the destination buffer size, the function
  returns the length with the bit HDLC_LENGTH_ERROR set.

  src - source buffer
  slen - source buffer length
  count - number of bytes removed (decoded) from the source buffer
  dst _ destination buffer
  dsize - destination buffer size
  returns - number of decoded bytes in the destination buffer and status
  flag.
 */
int isdnhdlc_decode (struct isdnhdlc_vars *hdlc, const unsigned char *src,
		     int slen, int *count, unsigned char *dst, int dsize)
{
	int status=0;

	static const unsigned char fast_flag[]={
		0x00,0x00,0x00,0x20,0x30,0x38,0x3c,0x3e,0x3f
	};

	static const unsigned char fast_flag_value[]={
		0x00,0x7e,0xfc,0xf9,0xf3,0xe7,0xcf,0x9f,0x3f
	};

	static const unsigned char fast_abort[]={
		0x00,0x00,0x80,0xc0,0xe0,0xf0,0xf8,0xfc,0xfe,0xff
	};

	*count = slen;

	while(slen > 0){
		if(hdlc->bit_shift==0){
			hdlc->cbin = *src++;
			slen--;
			hdlc->bit_shift = 8;
			if(hdlc->do_adapt56){
				hdlc->bit_shift --;
			}
		}

		switch(hdlc->state){
		case STOPPED:
			return 0;
		case HDLC_FAST_IDLE:
			if(hdlc->cbin == 0xff){
				hdlc->bit_shift = 0;
				break;
			}
			hdlc->state = HDLC_GET_FLAG_B0;
			hdlc->hdlc_bits1 = 0;
			hdlc->bit_shift = 8;
			break;
		case HDLC_GET_FLAG_B0:
			if(!(hdlc->cbin & 0x80)) {
				hdlc->state = HDLC_GETFLAG_B1A6;
				hdlc->hdlc_bits1 = 0;
			} else {
				if(!hdlc->do_adapt56){
					if(++hdlc->hdlc_bits1 >=8 ) if(hdlc->bit_shift==1)
						hdlc->state = HDLC_FAST_IDLE;
				}
			}
			hdlc->cbin<<=1;
			hdlc->bit_shift --;
			break;
		case HDLC_GETFLAG_B1A6:
			if(hdlc->cbin & 0x80){
				hdlc->hdlc_bits1++;
				if(hdlc->hdlc_bits1==6){
					hdlc->state = HDLC_GETFLAG_B7;
				}
			} else {
				hdlc->hdlc_bits1 = 0;
			}
			hdlc->cbin<<=1;
			hdlc->bit_shift --;
			break;
		case HDLC_GETFLAG_B7:
			if(hdlc->cbin & 0x80) {
				hdlc->state = HDLC_GET_FLAG_B0;
			} else {
				hdlc->state = HDLC_GET_DATA;
				hdlc->crc = 0xffff;
				hdlc->shift_reg = 0;
				hdlc->hdlc_bits1 = 0;
				hdlc->data_bits = 0;
				hdlc->data_received = 0;
			}
			hdlc->cbin<<=1;
			hdlc->bit_shift --;
			break;
		case HDLC_GET_DATA:
			if(hdlc->cbin & 0x80){
				hdlc->hdlc_bits1++;
				switch(hdlc->hdlc_bits1){
				case 6:
					break;
				case 7:
					if(hdlc->data_received) {
						// bad frame
						status = -HDLC_FRAMING_ERROR;
					}
					if(!hdlc->do_adapt56){
						if(hdlc->cbin==fast_abort[hdlc->bit_shift+1]){
							hdlc->state = HDLC_FAST_IDLE;
							hdlc->bit_shift=1;
							break;
						}
					} else {
						hdlc->state = HDLC_GET_FLAG_B0;
					}
					break;
				default:
					hdlc->shift_reg>>=1;
					hdlc->shift_reg |= 0x80;
					hdlc->data_bits++;
					break;
				}
			} else {
				switch(hdlc->hdlc_bits1){
				case 5:
					break;
				case 6:
					if(hdlc->data_received){
						if (hdlc->dstpos < 2) {
							status = -HDLC_FRAMING_ERROR;
						} else if (hdlc->crc != 0xf0b8){
							// crc error
							status = -HDLC_CRC_ERROR;
						} else {
							// remove CRC
							hdlc->dstpos -= 2;
							// good frame
							status = hdlc->dstpos;
						}
					}
					hdlc->crc = 0xffff;
					hdlc->shift_reg = 0;
					hdlc->data_bits = 0;
					if(!hdlc->do_adapt56){
						if(hdlc->cbin==fast_flag[hdlc->bit_shift]){
							hdlc->ffvalue = fast_flag_value[hdlc->bit_shift];
							hdlc->state = HDLC_FAST_FLAG;
							hdlc->ffbit_shift = hdlc->bit_shift;
							hdlc->bit_shift = 1;
						} else {
							hdlc->state = HDLC_GET_DATA;
							hdlc->data_received = 0;
						}
					} else {
						hdlc->state = HDLC_GET_DATA;
						hdlc->data_received = 0;
					}
					break;
				default:
					hdlc->shift_reg>>=1;
					hdlc->data_bits++;
					break;
				}
				hdlc->hdlc_bits1 = 0;
			}
			if (status) {
				hdlc->dstpos = 0;
				*count -= slen;
				hdlc->cbin <<= 1;
				hdlc->bit_shift--;
				return status;
			}
			if(hdlc->data_bits==8){
				hdlc->data_bits = 0;
				hdlc->data_received = 1;
				hdlc->crc = crc_ccitt_byte(hdlc->crc, hdlc->shift_reg);

				// good byte received
				if (hdlc->dstpos < dsize) {
					dst[hdlc->dstpos++] = hdlc->shift_reg;
				} else {
					// frame too long
					status = -HDLC_LENGTH_ERROR;
					hdlc->dstpos = 0;
				}
			}
			hdlc->cbin <<= 1;
			hdlc->bit_shift--;
			break;
		case HDLC_FAST_FLAG:
			if(hdlc->cbin==hdlc->ffvalue){
				hdlc->bit_shift = 0;
				break;
			} else {
				if(hdlc->cbin == 0xff){
					hdlc->state = HDLC_FAST_IDLE;
					hdlc->bit_shift=0;
				} else if(hdlc->ffbit_shift==8){
					hdlc->state = HDLC_GETFLAG_B7;
					break;
				} else {
					hdlc->shift_reg = fast_abort[hdlc->ffbit_shift-1];
					hdlc->hdlc_bits1 = hdlc->ffbit_shift-2;
					if(hdlc->hdlc_bits1<0)hdlc->hdlc_bits1 = 0;
					hdlc->data_bits = hdlc->ffbit_shift-1;
					hdlc->state = HDLC_GET_DATA;
					hdlc->data_received = 0;
				}
			}
			break;
		default:
			break;
		}
	}
	*count -= slen;
	return 0;
}

/*
  isdnhdlc_encode - encodes HDLC frames to a transparent bit stream.

  The bit stream starts with a beginning flag (01111110). After
  that each byte is added to the bit stream with bit stuffing added
  (0 after 5 1's).
  When the last byte has been removed from the source buffer, the
  CRC (2 bytes is added) and the frame terminates with the ending flag.
  For the dchannel, the idle character (all 1's) is also added at the end.
  If this function is called with empty source buffer (slen=0), flags or
  idle character will be generated.

  src - source buffer
  slen - source buffer length
  count - number of bytes removed (encoded) from source buffer
  dst _ destination buffer
  dsize - destination buffer size
  returns - number of encoded bytes in the destination buffer
*/
int isdnhdlc_encode(struct isdnhdlc_vars *hdlc, const unsigned char *src,
		unsigned short slen, int *count,
		unsigned char *dst, int dsize)
{
	static const unsigned char xfast_flag_value[] = {
		0x7e,0x3f,0x9f,0xcf,0xe7,0xf3,0xf9,0xfc,0x7e
	};

	int len = 0;

	*count = slen;

	while (dsize > 0) {
		if(hdlc->bit_shift==0){
			if(slen && !hdlc->do_closing){
				hdlc->shift_reg = *src++;
				slen--;
				if (slen == 0)
					hdlc->do_closing = 1;  /* closing sequence, CRC + flag(s) */
				hdlc->bit_shift = 8;
			} else {
				if(hdlc->state == HDLC_SEND_DATA){
					if(hdlc->data_received){
						hdlc->state = HDLC_SEND_CRC1;
						hdlc->crc ^= 0xffff;
						hdlc->bit_shift = 8;
						hdlc->shift_reg = hdlc->crc & 0xff;
					} else if(!hdlc->do_adapt56){
						hdlc->state = HDLC_SEND_FAST_FLAG;
					} else {
						hdlc->state = HDLC_SENDFLAG_B0;
					}
				}

			}
		}

		switch(hdlc->state){
		case STOPPED:
			while (dsize--)
				*dst++ = 0xff;

			return dsize;
		case HDLC_SEND_FAST_FLAG:
			hdlc->do_closing = 0;
			if(slen == 0){
				*dst++ = hdlc->ffvalue;
				len++;
				dsize--;
				break;
			}
			if(hdlc->bit_shift==8){
				hdlc->cbin = hdlc->ffvalue>>(8-hdlc->data_bits);
				hdlc->state = HDLC_SEND_DATA;
				hdlc->crc = 0xffff;
				hdlc->hdlc_bits1 = 0;
				hdlc->data_received = 1;
			}
			break;
		case HDLC_SENDFLAG_B0:
			hdlc->do_closing = 0;
			hdlc->cbin <<= 1;
			hdlc->data_bits++;
			hdlc->hdlc_bits1 = 0;
			hdlc->state = HDLC_SENDFLAG_B1A6;
			break;
		case HDLC_SENDFLAG_B1A6:
			hdlc->cbin <<= 1;
			hdlc->data_bits++;
			hdlc->cbin++;
			if(++hdlc->hdlc_bits1 == 6)
				hdlc->state = HDLC_SENDFLAG_B7;
			break;
		case HDLC_SENDFLAG_B7:
			hdlc->cbin <<= 1;
			hdlc->data_bits++;
			if(slen == 0){
				hdlc->state = HDLC_SENDFLAG_B0;
				break;
			}
			if(hdlc->bit_shift==8){
				hdlc->state = HDLC_SEND_DATA;
				hdlc->crc = 0xffff;
				hdlc->hdlc_bits1 = 0;
				hdlc->data_received = 1;
			}
			break;
		case HDLC_SEND_FIRST_FLAG:
			hdlc->data_received = 1;
			if(hdlc->data_bits==8){
				hdlc->state = HDLC_SEND_DATA;
				hdlc->crc = 0xffff;
				hdlc->hdlc_bits1 = 0;
				break;
			}
			hdlc->cbin <<= 1;
			hdlc->data_bits++;
			if(hdlc->shift_reg & 0x01)
				hdlc->cbin++;
			hdlc->shift_reg >>= 1;
			hdlc->bit_shift--;
			if(hdlc->bit_shift==0){
				hdlc->state = HDLC_SEND_DATA;
				hdlc->crc = 0xffff;
				hdlc->hdlc_bits1 = 0;
			}
			break;
		case HDLC_SEND_DATA:
			hdlc->cbin <<= 1;
			hdlc->data_bits++;
			if(hdlc->hdlc_bits1 == 5){
				hdlc->hdlc_bits1 = 0;
				break;
			}
			if(hdlc->bit_shift==8){
				hdlc->crc = crc_ccitt_byte(hdlc->crc, hdlc->shift_reg);
			}
			if(hdlc->shift_reg & 0x01){
				hdlc->hdlc_bits1++;
				hdlc->cbin++;
				hdlc->shift_reg >>= 1;
				hdlc->bit_shift--;
			} else {
				hdlc->hdlc_bits1 = 0;
				hdlc->shift_reg >>= 1;
				hdlc->bit_shift--;
			}
			break;
		case HDLC_SEND_CRC1:
			hdlc->cbin <<= 1;
			hdlc->data_bits++;
			if(hdlc->hdlc_bits1 == 5){
				hdlc->hdlc_bits1 = 0;
				break;
			}
			if(hdlc->shift_reg & 0x01){
				hdlc->hdlc_bits1++;
				hdlc->cbin++;
				hdlc->shift_reg >>= 1;
				hdlc->bit_shift--;
			} else {
				hdlc->hdlc_bits1 = 0;
				hdlc->shift_reg >>= 1;
				hdlc->bit_shift--;
			}
			if(hdlc->bit_shift==0){
				hdlc->shift_reg = (hdlc->crc >> 8);
				hdlc->state = HDLC_SEND_CRC2;
				hdlc->bit_shift = 8;
			}
			break;
		case HDLC_SEND_CRC2:
			hdlc->cbin <<= 1;
			hdlc->data_bits++;
			if(hdlc->hdlc_bits1 == 5){
				hdlc->hdlc_bits1 = 0;
				break;
			}
			if(hdlc->shift_reg & 0x01){
				hdlc->hdlc_bits1++;
				hdlc->cbin++;
				hdlc->shift_reg >>= 1;
				hdlc->bit_shift--;
			} else {
				hdlc->hdlc_bits1 = 0;
				hdlc->shift_reg >>= 1;
				hdlc->bit_shift--;
			}
			if(hdlc->bit_shift==0){
				hdlc->shift_reg = 0x7e;
				hdlc->state = HDLC_SEND_CLOSING_FLAG;
				hdlc->bit_shift = 8;
			}
			break;
		case HDLC_SEND_CLOSING_FLAG:
			hdlc->cbin <<= 1;
			hdlc->data_bits++;
			if(hdlc->hdlc_bits1 == 5){
				hdlc->hdlc_bits1 = 0;
				break;
			}
			if(hdlc->shift_reg & 0x01){
				hdlc->cbin++;
			}
			hdlc->shift_reg >>= 1;
			hdlc->bit_shift--;
			if(hdlc->bit_shift==0){
				hdlc->ffvalue = xfast_flag_value[hdlc->data_bits];
				if(hdlc->dchannel){
					hdlc->ffvalue = 0x7e;
					hdlc->state = HDLC_SEND_IDLE1;
					hdlc->bit_shift = 8-hdlc->data_bits;
					if(hdlc->bit_shift==0)
						hdlc->state = HDLC_SEND_FAST_IDLE;
				} else {
					if(!hdlc->do_adapt56){
						hdlc->state = HDLC_SEND_FAST_FLAG;
						hdlc->data_received = 0;
					} else {
						hdlc->state = HDLC_SENDFLAG_B0;
						hdlc->data_received = 0;
					}
					// Finished with this frame, send flags
					if (dsize > 1) dsize = 1;
				}
			}
			break;
		case HDLC_SEND_IDLE1:
			hdlc->do_closing = 0;
			hdlc->cbin <<= 1;
			hdlc->cbin++;
			hdlc->data_bits++;
			hdlc->bit_shift--;
			if(hdlc->bit_shift==0){
				hdlc->state = HDLC_SEND_FAST_IDLE;
				hdlc->bit_shift = 0;
			}
			break;
		case HDLC_SEND_FAST_IDLE:
			hdlc->do_closing = 0;
			hdlc->cbin = 0xff;
			hdlc->data_bits = 8;
			if(hdlc->bit_shift == 8){
				hdlc->cbin = 0x7e;
				hdlc->state = HDLC_SEND_FIRST_FLAG;
			} else {
				*dst++ = hdlc->cbin;
				hdlc->bit_shift = hdlc->data_bits = 0;
				len++;
				dsize = 0;
			}
			break;
		default:
			break;
		}
		if(hdlc->do_adapt56){
			if(hdlc->data_bits==7){
				hdlc->cbin <<= 1;
				hdlc->cbin++;
				hdlc->data_bits++;
			}
		}
		if(hdlc->data_bits==8){
			*dst++ = hdlc->cbin;
			hdlc->data_bits = 0;
			len++;
			dsize--;
		}
	}
	*count -= slen;

	return len;
}

EXPORT_SYMBOL(isdnhdlc_bit_rev_tab);
EXPORT_SYMBOL(isdnhdlc_rcv_init);
EXPORT_SYMBOL(isdnhdlc_decode);
EXPORT_SYMBOL(isdnhdlc_out_init);
EXPORT_SYMBOL(isdnhdlc_encode);
