/*
 * Atheros AR9170 driver
 *
 * Basic HW register/memory/command access functions
 *
 * Copyright 2008, Johannes Berg <johannes@sipsolutions.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, see
 * http://www.gnu.org/licenses/.
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *    Copyright (c) 2007-2008 Atheros Communications, Inc.
 *
 *    Permission to use, copy, modify, and/or distribute this software for any
 *    purpose with or without fee is hereby granted, provided that the above
 *    copyright notice and this permission notice appear in all copies.
 *
 *    THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 *    WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 *    MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 *    ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 *    WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 *    ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 *    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#ifndef __CMD_H
#define __CMD_H

#include "ar9170.h"

/* basic HW access */
int ar9170_write_mem(struct ar9170 *ar, const __le32 *data, size_t len);
int ar9170_write_reg(struct ar9170 *ar, const u32 reg, const u32 val);
int ar9170_read_reg(struct ar9170 *ar, u32 reg, u32 *val);
int ar9170_echo_test(struct ar9170 *ar, u32 v);

/*
 * Macros to facilitate writing multiple registers in a single
 * write-combining USB command. Note that when the first group
 * fails the whole thing will fail without any others attempted,
 * but you won't know which write in the group failed.
 */
#define ar9170_regwrite_begin(ar)					\
do {									\
	int __nreg = 0, __err = 0;					\
	struct ar9170 *__ar = ar;

#define ar9170_regwrite(r, v) do {					\
	__ar->cmdbuf[2 * __nreg + 1] = cpu_to_le32(r);			\
	__ar->cmdbuf[2 * __nreg + 2] = cpu_to_le32(v);			\
	__nreg++;							\
	if ((__nreg >= PAYLOAD_MAX/2)) {				\
		if (IS_ACCEPTING_CMD(__ar))				\
			__err = ar->exec_cmd(__ar, AR9170_CMD_WREG,	\
					     8 * __nreg,		\
					     (u8 *) &__ar->cmdbuf[1],	\
					     0, NULL);			\
		__nreg = 0;						\
		if (__err)						\
			goto __regwrite_out;				\
	}								\
} while (0)

#define ar9170_regwrite_finish()					\
__regwrite_out :							\
	if (__nreg) {							\
		if (IS_ACCEPTING_CMD(__ar))				\
			__err = ar->exec_cmd(__ar, AR9170_CMD_WREG,	\
					     8 * __nreg, 		\
					     (u8 *) &__ar->cmdbuf[1],	\
					     0, NULL);			\
		__nreg = 0;						\
	}

#define ar9170_regwrite_result()					\
	__err;								\
} while (0);

#endif /* __CMD_H */
