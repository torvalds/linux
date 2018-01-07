/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2012-2018 ARM Limited or its affiliates. */

#ifndef _HASH_DEFS_H_
#define _HASH_DEFS_H_

#include "cc_crypto_ctx.h"

enum cc_hash_conf_pad {
	HASH_PADDING_DISABLED = 0,
	HASH_PADDING_ENABLED = 1,
	HASH_DIGEST_RESULT_LITTLE_ENDIAN = 2,
	HASH_CONFIG1_PADDING_RESERVE32 = S32_MAX,
};

enum cc_hash_cipher_pad {
	DO_NOT_PAD = 0,
	DO_PAD = 1,
	HASH_CIPHER_DO_PADDING_RESERVE32 = S32_MAX,
};

#endif /*_HASH_DEFS_H_*/

