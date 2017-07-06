/*
 * Copyright (C) 2012-2017 ARM Limited or its affiliates.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

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

