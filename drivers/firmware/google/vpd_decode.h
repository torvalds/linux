/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * vpd_decode.h
 *
 * Google VPD decoding routines.
 *
 * Copyright 2017 Google Inc.
 */

#ifndef __VPD_DECODE_H
#define __VPD_DECODE_H

#include <linux/types.h>

enum {
	VPD_OK = 0,
	VPD_FAIL,
};

enum {
	VPD_TYPE_TERMINATOR = 0,
	VPD_TYPE_STRING,
	VPD_TYPE_INFO                = 0xfe,
	VPD_TYPE_IMPLICIT_TERMINATOR = 0xff,
};

/* Callback for vpd_decode_string to invoke. */
typedef int vpd_decode_callback(const u8 *key, s32 key_len,
				const u8 *value, s32 value_len,
				void *arg);

/*
 * vpd_decode_string
 *
 * Given the encoded string, this function invokes callback with extracted
 * (key, value). The *consumed will be plused the number of bytes consumed in
 * this function.
 *
 * The input_buf points to the first byte of the input buffer.
 *
 * The *consumed starts from 0, which is actually the next byte to be decoded.
 * It can be non-zero to be used in multiple calls.
 *
 * If one entry is successfully decoded, sends it to callback and returns the
 * result.
 */
int vpd_decode_string(const s32 max_len, const u8 *input_buf, s32 *consumed,
		      vpd_decode_callback callback, void *callback_arg);

#endif  /* __VPD_DECODE_H */
