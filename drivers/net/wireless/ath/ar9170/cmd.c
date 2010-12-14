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

#include "ar9170.h"
#include "cmd.h"

int ar9170_write_mem(struct ar9170 *ar, const __le32 *data, size_t len)
{
	int err;

	if (unlikely(!IS_ACCEPTING_CMD(ar)))
		return 0;

	err = ar->exec_cmd(ar, AR9170_CMD_WMEM, len, (u8 *) data, 0, NULL);
	if (err)
		wiphy_debug(ar->hw->wiphy, "writing memory failed\n");
	return err;
}

int ar9170_write_reg(struct ar9170 *ar, const u32 reg, const u32 val)
{
	__le32 buf[2] = {
		cpu_to_le32(reg),
		cpu_to_le32(val),
	};
	int err;

	if (unlikely(!IS_ACCEPTING_CMD(ar)))
		return 0;

	err = ar->exec_cmd(ar, AR9170_CMD_WREG, sizeof(buf),
			   (u8 *) buf, 0, NULL);
	if (err)
		wiphy_debug(ar->hw->wiphy, "writing reg %#x (val %#x) failed\n",
			    reg, val);
	return err;
}

int ar9170_read_mreg(struct ar9170 *ar, int nregs, const u32 *regs, u32 *out)
{
	int i, err;
	__le32 *offs, *res;

	if (unlikely(!IS_ACCEPTING_CMD(ar)))
		return 0;

	/* abuse "out" for the register offsets, must be same length */
	offs = (__le32 *)out;
	for (i = 0; i < nregs; i++)
		offs[i] = cpu_to_le32(regs[i]);

	/* also use the same buffer for the input */
	res = (__le32 *)out;

	err = ar->exec_cmd(ar, AR9170_CMD_RREG,
			   4 * nregs, (u8 *)offs,
			   4 * nregs, (u8 *)res);
	if (err)
		return err;

	/* convert result to cpu endian */
	for (i = 0; i < nregs; i++)
		out[i] = le32_to_cpu(res[i]);

	return 0;
}

int ar9170_read_reg(struct ar9170 *ar, u32 reg, u32 *val)
{
	return ar9170_read_mreg(ar, 1, &reg, val);
}

int ar9170_echo_test(struct ar9170 *ar, u32 v)
{
	__le32 echobuf = cpu_to_le32(v);
	__le32 echores;
	int err;

	if (unlikely(!IS_ACCEPTING_CMD(ar)))
		return -ENODEV;

	err = ar->exec_cmd(ar, AR9170_CMD_ECHO,
			   4, (u8 *)&echobuf,
			   4, (u8 *)&echores);
	if (err)
		return err;

	if (echobuf != echores)
		return -EINVAL;

	return 0;
}
