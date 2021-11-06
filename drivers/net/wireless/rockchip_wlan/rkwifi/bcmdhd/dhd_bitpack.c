/*
 * Bit packing and Base64 utils for EWP
 *
 * Copyright (C) 2020, Broadcom.
 *
 *      Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 *
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 *
 *
 * <<Broadcom-WL-IPTag/Open:>>
 *
 * $Id$
 */

#include <dhd_bitpack.h>

#define BIT_PACK_OVERFLOW 0xFFFFFFFFu

const char* base64_table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

#define BASE64_MAX_VALUE 63u

#define BASE64_UNIT_LEN 6u

#define BASE64_OFFSET0 0u
#define BASE64_OFFSET1 6u
#define BASE64_OFFSET2 12u

#define MASK_UPPER_6BIT 0xfc
#define MASK_LOWER_6BIT 0x3f

#define MASK_UPPER_4BIT 0xf0
#define MASK_LOWER_4BIT 0x0f

#define MASK_UPPER_2BIT 0xc0
#define MASK_LOWER_2BIT 0x03

#define SHIFT_2BIT 2u
#define SHIFT_4BIT 4u
#define SHIFT_6BIT 6u

#define BASE64_PADDING_MARGIN 4u

/*
 * Function:	dhd_bit_pack
 *
 * Purpose:	bit data packing to given buffer
 *
 * Input Parameters:
 *      buf		buffer to pack bit data
 *      buf_len		total buffer length
 *	bit_offset	offset in buffer (bitwise)
 *	data		data to pack (max 32 bit)
 *	bit_length	bit length to pack
 *
 * Output:
 *	Updated bit offset in buf
 */
int32
dhd_bit_pack(char *buf, int buf_len, int bit_offset, uint32 data, int32 bit_length)
{

	int32 byte_shift = (bit_offset / 8);
	int32 local_bit_offset = bit_offset % 8;
	int32 available_bit = 8 - local_bit_offset;
	int32 remain_bit = bit_length;
	uint32 cropped_data;
	int32 idx;
	int32 total_byte = BYTE_SIZE(local_bit_offset + bit_length);

	if (bit_length > 32) {
		/* exceeded max bit length, do nothing */
		return bit_offset;
	}
	if (BYTE_SIZE(bit_offset + bit_length) > buf_len) {
		/* can't pack more bits if expected offset is
		 * exceeded then buffer size
		 */
		return bit_offset;
	}
	if (bit_length < 32 && data >= 1<<bit_length) {
		cropped_data = BIT_PACK_OVERFLOW << (32 - bit_length);
		cropped_data = cropped_data >> (32 - bit_length);
	} else {
		cropped_data = data << (32 - bit_length);
		cropped_data = cropped_data >> (32 - bit_length);
	}

	buf += byte_shift;

	remain_bit = bit_length;
	if (total_byte > 10) {
		return bit_offset;
	}
	for (idx = 0; idx < total_byte; idx++) {
		char temp_byte = 0x00;
		if (idx == 0) {
			local_bit_offset = bit_offset % 8;
		} else {
			local_bit_offset = 0;
		}

		available_bit = 8 - local_bit_offset;
		remain_bit -= available_bit;
		if (remain_bit >= 0) {
			temp_byte = cropped_data >> remain_bit;
		} else {
			temp_byte = cropped_data << (-1*remain_bit);
		}
		*buf = *buf | temp_byte;
		buf ++;
	}
	bit_offset += bit_length;

	return bit_offset;
}

static char
dhd_base64_get_code(char input)
{
	if (input > BASE64_MAX_VALUE) {
		return '=';
	}
	return base64_table[(int)input];
}

/*
 * Function:	dhd_base64_encode
 *
 * Purpose:	base64 encoding module which converts from 8 bits to
 *		6 bit based, base64 format using base64_table
 *		eg:	input:	hex-123456
 *				bin-0001|0010|0011|0100|0101|0110
 *			encode every 6 bit :
 *				bin-000100|100011|010001|010110
 *			base64 code :
 *				base64-EjRW
 *
 * Input Parameters:
 *      in_buf		input buffer
 *      in_buf_len	length of input buffer
 *	out_buf		output buffer
 *	out_buf_len	length_ of output buffer
 *
 * Output:
 *	length of encoded base64 string
 */
int32
dhd_base64_encode(char* in_buf, int32 in_buf_len, char* out_buf, int32 out_buf_len)
{
	char* input_pos;
	char* input_end;
	char* base64_out;
	char* base64_out_pos;
	char* base64_output_end;
	char current_byte = 0;
	char masked_byte = 0;
	int32 estimated_out_len = 0;
	int32 offset = 0;

	if (!in_buf || !out_buf || in_buf_len == 0 || out_buf_len == 0) {
		/* wrong input parameters */
		return 0;
	}

	input_pos = in_buf;
	input_end = in_buf + in_buf_len;
	base64_out = out_buf;
	base64_out_pos = base64_out;
	base64_output_end = out_buf + out_buf_len - BASE64_PADDING_MARGIN;
	estimated_out_len = in_buf_len / 3 * 4;

	if (estimated_out_len > out_buf_len) {
		/* estimated output length is
		 * larger than output buffer size
		 */
		return 0;
	}

	while (input_pos != input_end) {
		if (base64_out_pos > base64_output_end) {
			/* outbuf buffer size exceeded, finish encoding */
			break;
		}
		if (offset == BASE64_OFFSET0) {
			current_byte = *input_pos++;
			masked_byte = (current_byte & MASK_UPPER_6BIT) >> SHIFT_2BIT;
			*base64_out_pos++ = dhd_base64_get_code(masked_byte);
			masked_byte = (current_byte & MASK_LOWER_2BIT) << SHIFT_4BIT;
			offset += BASE64_UNIT_LEN;
		} else if (offset == BASE64_OFFSET1) {
			current_byte = *input_pos++;
			masked_byte |= (current_byte & MASK_UPPER_4BIT) >> SHIFT_4BIT;
			*base64_out_pos++ = dhd_base64_get_code(masked_byte);
			masked_byte = (current_byte & MASK_LOWER_4BIT) << SHIFT_2BIT;
			offset += BASE64_UNIT_LEN;
		} else if (offset == BASE64_OFFSET2) {
			current_byte = *input_pos++;
			masked_byte |= (current_byte & MASK_UPPER_2BIT) >> SHIFT_6BIT;
			*base64_out_pos++ = dhd_base64_get_code(masked_byte);
			offset += BASE64_UNIT_LEN;
			masked_byte = (current_byte & MASK_LOWER_6BIT);
			*base64_out_pos++ = dhd_base64_get_code(masked_byte);
			offset = BASE64_OFFSET0;
		}
	}
	if (offset == BASE64_OFFSET1) {
		*base64_out_pos++ = dhd_base64_get_code(masked_byte);
		*base64_out_pos++ = '=';
		*base64_out_pos++ = '=';
	} else if (offset == BASE64_OFFSET2) {
		*base64_out_pos++ = dhd_base64_get_code(masked_byte);
		*base64_out_pos++ = '=';
	}

	return base64_out_pos - base64_out;
}
