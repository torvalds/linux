/* Copyright (c) 2008-2009, Code Aurora Forum. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/crc-ccitt.h>
#include "diagchar_hdlc.h"


MODULE_LICENSE("GPL v2");

#define CRC_16_L_SEED           0xFFFF

#define CRC_16_L_STEP(xx_crc, xx_c) \
	crc_ccitt_byte(xx_crc, xx_c)

void diag_hdlc_encode(struct diag_send_desc_type *src_desc,
		      struct diag_hdlc_dest_type *enc)
{
	uint8_t *dest;
	uint8_t *dest_last;
	const uint8_t *src;
	const uint8_t *src_last;
	uint16_t crc;
	unsigned char src_byte = 0;
	enum diag_send_state_enum_type state;
	unsigned int used = 0;

	if (src_desc && enc) {

		/* Copy parts to local variables. */
		src = src_desc->pkt;
		src_last = src_desc->last;
		state = src_desc->state;
		dest = enc->dest;
		dest_last = enc->dest_last;

		if (state == DIAG_STATE_START) {
			crc = CRC_16_L_SEED;
			state++;
		} else {
			/* Get a local copy of the CRC */
			crc = enc->crc;
		}

		/* dest or dest_last may be NULL to trigger a
		   state transition only */
		if (dest && dest_last) {
			/* This condition needs to include the possibility
			   of 2 dest bytes for an escaped byte */
			while (src <= src_last && dest <= dest_last) {

				src_byte = *src++;

				if ((src_byte == CONTROL_CHAR) ||
				    (src_byte == ESC_CHAR)) {

					/* If the escape character is not the
					   last byte */
					if (dest != dest_last) {
						crc = CRC_16_L_STEP(crc,
								    src_byte);

						*dest++ = ESC_CHAR;
						used++;

						*dest++ = src_byte
							  ^ ESC_MASK;
						used++;
					} else {

						src--;
						break;
					}

				} else {
					crc = CRC_16_L_STEP(crc, src_byte);
					*dest++ = src_byte;
					used++;
				}
			}

			if (src > src_last) {

				if (state == DIAG_STATE_BUSY) {
					if (src_desc->terminate) {
						crc = ~crc;
						state++;
					} else {
						/* Done with fragment */
						state = DIAG_STATE_COMPLETE;
					}
				}

				while (dest <= dest_last &&
				       state >= DIAG_STATE_CRC1 &&
				       state < DIAG_STATE_TERM) {
					/* Encode a byte of the CRC next */
					src_byte = crc & 0xFF;

					if ((src_byte == CONTROL_CHAR)
					    || (src_byte == ESC_CHAR)) {

						if (dest != dest_last) {

							*dest++ = ESC_CHAR;
							used++;
							*dest++ = src_byte ^
								  ESC_MASK;
							used++;

							crc >>= 8;
						} else {

							break;
						}
					} else {

						crc >>= 8;
						*dest++ = src_byte;
						used++;
					}

					state++;
				}

				if (state == DIAG_STATE_TERM) {
					if (dest_last >= dest) {
						*dest++ = CONTROL_CHAR;
						used++;
						state++;	/* Complete */
					}
				}
			}
		}
		/* Copy local variables back into the encode structure. */

		enc->dest = dest;
		enc->dest_last = dest_last;
		enc->crc = crc;
		src_desc->pkt = src;
		src_desc->last = src_last;
		src_desc->state = state;
	}

	return;
}


int diag_hdlc_decode(struct diag_hdlc_decode_type *hdlc)
{
	uint8_t *src_ptr = NULL, *dest_ptr = NULL;
	unsigned int src_length = 0, dest_length = 0;

	unsigned int len = 0;
	unsigned int i;
	uint8_t src_byte;

	int pkt_bnd = 0;

	if (hdlc && hdlc->src_ptr && hdlc->dest_ptr &&
	    (hdlc->src_size - hdlc->src_idx > 0) &&
	    (hdlc->dest_size - hdlc->dest_idx > 0)) {

		src_ptr = hdlc->src_ptr;
		src_ptr = &src_ptr[hdlc->src_idx];
		src_length = hdlc->src_size - hdlc->src_idx;

		dest_ptr = hdlc->dest_ptr;
		dest_ptr = &dest_ptr[hdlc->dest_idx];
		dest_length = hdlc->dest_size - hdlc->dest_idx;

		for (i = 0; i < src_length; i++) {

			src_byte = src_ptr[i];

			if (hdlc->escaping) {
				dest_ptr[len++] = src_byte ^ ESC_MASK;
				hdlc->escaping = 0;
			} else if (src_byte == ESC_CHAR) {
				if (i == (src_length - 1)) {
					hdlc->escaping = 1;
					i++;
					break;
				} else {
					dest_ptr[len++] = src_ptr[++i]
							  ^ ESC_MASK;
				}
			} else if (src_byte == CONTROL_CHAR) {
				dest_ptr[len++] = src_byte;
				pkt_bnd = 1;
				i++;
				break;
			} else {
				dest_ptr[len++] = src_byte;
			}

			if (len >= dest_length) {
				i++;
				break;
			}
		}

		hdlc->src_idx += i;
		hdlc->dest_idx += len;
	}

	return pkt_bnd;
}
