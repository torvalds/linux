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

EXPORT_SYMBOL(isdnhdlc_rcv_init);
EXPORT_SYMBOL(isdnhdlc_decode);
EXPORT_SYMBOL(isdnhdlc_out_init);
EXPORT_SYMBOL(isdnhdlc_encode);
