/*
 * Atheros CARL9170 driver
 *
 * Basic HW register/memory/command access functions
 *
 * Copyright 2008, Johannes Berg <johannes@sipsolutions.net>
 * Copyright 2010, Christian Lamparter <chunkeey@googlemail.com>
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

#include "carl9170.h"

/* basic HW access */
int carl9170_write_reg(struct ar9170 *ar, const u32 reg, const u32 val);
int carl9170_read_reg(struct ar9170 *ar, const u32 reg, u32 *val);
int carl9170_read_mreg(struct ar9170 *ar, const int nregs,
		       const u32 *regs, u32 *out);
int carl9170_echo_test(struct ar9170 *ar, u32 v);
int carl9170_reboot(struct ar9170 *ar);
int carl9170_mac_reset(struct ar9170 *ar);
int carl9170_powersave(struct ar9170 *ar, const bool power_on);
int carl9170_bcn_ctrl(struct ar9170 *ar, const unsigned int vif_id,
		       const u32 mode, const u32 addr, const u32 len);

static inline int carl9170_flush_cab(struct ar9170 *ar,
				     const unsigned int vif_id)
{
	return carl9170_bcn_ctrl(ar, vif_id, CARL9170_BCN_CTRL_DRAIN, 0, 0);
}

static inline int carl9170_rx_filter(struct ar9170 *ar,
				     const unsigned int _rx_filter)
{
	__le32 rx_filter = cpu_to_le32(_rx_filter);

	return carl9170_exec_cmd(ar, CARL9170_CMD_RX_FILTER,
				sizeof(rx_filter), (u8 *)&rx_filter,
				0, NULL);
}

struct carl9170_cmd *carl9170_cmd_buf(struct ar9170 *ar,
	const enum carl9170_cmd_oids cmd, const unsigned int len);

/*
 * Macros to facilitate writing multiple registers in a single
 * write-combining USB command. Note that when the first group
 * fails the whole thing will fail without any others attempted,
 * but you won't know which write in the group failed.
 */
#define carl9170_regwrite_begin(ar)					\
do {									\
	int __nreg = 0, __err = 0;					\
	struct ar9170 *__ar = ar;

#define carl9170_regwrite(r, v) do {					\
	__ar->cmd_buf[2 * __nreg + 1] = cpu_to_le32(r);			\
	__ar->cmd_buf[2 * __nreg + 2] = cpu_to_le32(v);			\
	__nreg++;							\
	if ((__nreg >= PAYLOAD_MAX/2)) {				\
		if (IS_ACCEPTING_CMD(__ar))				\
			__err = carl9170_exec_cmd(__ar,			\
				CARL9170_CMD_WREG, 8 * __nreg,		\
				(u8 *) &__ar->cmd_buf[1], 0, NULL);	\
		else							\
			goto __regwrite_out;				\
									\
		__nreg = 0;						\
		if (__err)						\
			goto __regwrite_out;				\
	}								\
} while (0)

#define carl9170_regwrite_finish()					\
__regwrite_out :							\
	if (__err == 0 && __nreg) {					\
		if (IS_ACCEPTING_CMD(__ar))				\
			__err = carl9170_exec_cmd(__ar,			\
				CARL9170_CMD_WREG, 8 * __nreg,		\
				(u8 *) &__ar->cmd_buf[1], 0, NULL);	\
		__nreg = 0;						\
	}

#define carl9170_regwrite_result()					\
	__err;								\
} while (0);


#define carl9170_async_get_buf()					\
do {									\
	__cmd = carl9170_cmd_buf(__carl, CARL9170_CMD_WREG_ASYNC,	\
				 CARL9170_MAX_CMD_PAYLOAD_LEN);		\
	if (__cmd == NULL) {						\
		__err = -ENOMEM;					\
		goto __async_regwrite_out;				\
	}								\
} while (0);

#define carl9170_async_regwrite_begin(carl)				\
do {									\
	int __nreg = 0, __err = 0;					\
	struct ar9170 *__carl = carl;					\
	struct carl9170_cmd *__cmd;					\
	carl9170_async_get_buf();					\

#define carl9170_async_regwrite(r, v) do {				\
	__cmd->wreg.regs[__nreg].addr = cpu_to_le32(r);			\
	__cmd->wreg.regs[__nreg].val = cpu_to_le32(v);			\
	__nreg++;							\
	if ((__nreg >= PAYLOAD_MAX/2)) {				\
		if (IS_ACCEPTING_CMD(__carl)) {				\
			__cmd->hdr.len = 8 * __nreg;			\
			__err = __carl9170_exec_cmd(__carl, __cmd, true);\
			__cmd = NULL;					\
			carl9170_async_get_buf();			\
		} else {						\
			goto __async_regwrite_out;			\
		}							\
		__nreg = 0;						\
		if (__err)						\
			goto __async_regwrite_out;			\
	}								\
} while (0)

#define carl9170_async_regwrite_finish()				\
__async_regwrite_out :							\
	if (__err == 0 && __nreg) {					\
		__cmd->hdr.len = 8 * __nreg;				\
		if (IS_ACCEPTING_CMD(__carl))				\
			__err = __carl9170_exec_cmd(__carl, __cmd, true);\
		__nreg = 0;						\
	}

#define carl9170_async_regwrite_result()				\
	__err;								\
} while (0);

#endif /* __CMD_H */
