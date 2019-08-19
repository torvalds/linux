// SPDX-License-Identifier: GPL-2.0-only
/*
 * vpd_decode.c
 *
 * Google VPD decoding routines.
 *
 * Copyright 2017 Google Inc.
 */

#include <linux/export.h>

#include "vpd_decode.h"

static int vpd_decode_len(const s32 max_len, const u8 *in,
			  s32 *length, s32 *decoded_len)
{
	u8 more;
	int i = 0;

	if (!length || !decoded_len)
		return VPD_FAIL;

	*length = 0;
	do {
		if (i >= max_len)
			return VPD_FAIL;

		more = in[i] & 0x80;
		*length <<= 7;
		*length |= in[i] & 0x7f;
		++i;
	} while (more);

	*decoded_len = i;

	return VPD_OK;
}

int vpd_decode_string(const s32 max_len, const u8 *input_buf, s32 *consumed,
		      vpd_decode_callback callback, void *callback_arg)
{
	int type;
	int res;
	s32 key_len;
	s32 value_len;
	s32 decoded_len;
	const u8 *key;
	const u8 *value;

	/* type */
	if (*consumed >= max_len)
		return VPD_FAIL;

	type = input_buf[*consumed];

	switch (type) {
	case VPD_TYPE_INFO:
	case VPD_TYPE_STRING:
		(*consumed)++;

		/* key */
		res = vpd_decode_len(max_len - *consumed, &input_buf[*consumed],
				     &key_len, &decoded_len);
		if (res != VPD_OK || *consumed + decoded_len >= max_len)
			return VPD_FAIL;

		*consumed += decoded_len;
		key = &input_buf[*consumed];
		*consumed += key_len;

		/* value */
		res = vpd_decode_len(max_len - *consumed, &input_buf[*consumed],
				     &value_len, &decoded_len);
		if (res != VPD_OK || *consumed + decoded_len > max_len)
			return VPD_FAIL;

		*consumed += decoded_len;
		value = &input_buf[*consumed];
		*consumed += value_len;

		if (type == VPD_TYPE_STRING)
			return callback(key, key_len, value, value_len,
					callback_arg);
		break;

	default:
		return VPD_FAIL;
	}

	return VPD_OK;
}
