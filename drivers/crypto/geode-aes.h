/* Copyright (C) 2003-2006, Advanced Micro Devices, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef _GEODE_AES_H_
#define _GEODE_AES_H_

#define AES_KEY_LENGTH 16
#define AES_IV_LENGTH  16

#define AES_MIN_BLOCK_SIZE 16

#define AES_MODE_ECB 0
#define AES_MODE_CBC 1

#define AES_DIR_DECRYPT 0
#define AES_DIR_ENCRYPT 1

#define AES_FLAGS_USRKEY   (1 << 0)
#define AES_FLAGS_COHERENT (1 << 1)

struct geode_aes_op {

	void *src;
	void *dst;

	u32 mode;
	u32 dir;
	u32 flags;
	int len;

	u8 key[AES_KEY_LENGTH];
	u8 iv[AES_IV_LENGTH];
};

#endif
