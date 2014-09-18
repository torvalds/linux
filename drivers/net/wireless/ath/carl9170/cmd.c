/*
 * Atheros CARL9170 driver
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

#include <asm/div64.h>
#include "carl9170.h"
#include "cmd.h"

int carl9170_write_reg(struct ar9170 *ar, const u32 reg, const u32 val)
{
	const __le32 buf[2] = {
		cpu_to_le32(reg),
		cpu_to_le32(val),
	};
	int err;

	err = carl9170_exec_cmd(ar, CARL9170_CMD_WREG, sizeof(buf),
				(u8 *) buf, 0, NULL);
	if (err) {
		if (net_ratelimit()) {
			wiphy_err(ar->hw->wiphy, "writing reg %#x "
				"(val %#x) failed (%d)\n", reg, val, err);
		}
	}
	return err;
}

int carl9170_read_mreg(struct ar9170 *ar, const int nregs,
		       const u32 *regs, u32 *out)
{
	int i, err;
	__le32 *offs, *res;

	/* abuse "out" for the register offsets, must be same length */
	offs = (__le32 *)out;
	for (i = 0; i < nregs; i++)
		offs[i] = cpu_to_le32(regs[i]);

	/* also use the same buffer for the input */
	res = (__le32 *)out;

	err = carl9170_exec_cmd(ar, CARL9170_CMD_RREG,
				4 * nregs, (u8 *)offs,
				4 * nregs, (u8 *)res);
	if (err) {
		if (net_ratelimit()) {
			wiphy_err(ar->hw->wiphy, "reading regs failed (%d)\n",
				  err);
		}
		return err;
	}

	/* convert result to cpu endian */
	for (i = 0; i < nregs; i++)
		out[i] = le32_to_cpu(res[i]);

	return 0;
}

int carl9170_read_reg(struct ar9170 *ar, u32 reg, u32 *val)
{
	return carl9170_read_mreg(ar, 1, &reg, val);
}

int carl9170_echo_test(struct ar9170 *ar, const u32 v)
{
	u32 echores;
	int err;

	err = carl9170_exec_cmd(ar, CARL9170_CMD_ECHO,
				4, (u8 *)&v,
				4, (u8 *)&echores);
	if (err)
		return err;

	if (v != echores) {
		wiphy_info(ar->hw->wiphy, "wrong echo %x != %x", v, echores);
		return -EINVAL;
	}

	return 0;
}

struct carl9170_cmd *carl9170_cmd_buf(struct ar9170 *ar,
	const enum carl9170_cmd_oids cmd, const unsigned int len)
{
	struct carl9170_cmd *tmp;

	tmp = kzalloc(sizeof(struct carl9170_cmd_head) + len, GFP_ATOMIC);
	if (tmp) {
		tmp->hdr.cmd = cmd;
		tmp->hdr.len = len;
	}

	return tmp;
}

int carl9170_reboot(struct ar9170 *ar)
{
	struct carl9170_cmd *cmd;
	int err;

	cmd = carl9170_cmd_buf(ar, CARL9170_CMD_REBOOT_ASYNC, 0);
	if (!cmd)
		return -ENOMEM;

	err = __carl9170_exec_cmd(ar, cmd, true);
	return err;
}

int carl9170_mac_reset(struct ar9170 *ar)
{
	return carl9170_exec_cmd(ar, CARL9170_CMD_SWRST,
				 0, NULL, 0, NULL);
}

int carl9170_bcn_ctrl(struct ar9170 *ar, const unsigned int vif_id,
		       const u32 mode, const u32 addr, const u32 len)
{
	struct carl9170_cmd *cmd;

	cmd = carl9170_cmd_buf(ar, CARL9170_CMD_BCN_CTRL_ASYNC,
			       sizeof(struct carl9170_bcn_ctrl_cmd));
	if (!cmd)
		return -ENOMEM;

	cmd->bcn_ctrl.vif_id = cpu_to_le32(vif_id);
	cmd->bcn_ctrl.mode = cpu_to_le32(mode);
	cmd->bcn_ctrl.bcn_addr = cpu_to_le32(addr);
	cmd->bcn_ctrl.bcn_len = cpu_to_le32(len);

	return __carl9170_exec_cmd(ar, cmd, true);
}

int carl9170_collect_tally(struct ar9170 *ar)
{
	struct carl9170_tally_rsp tally;
	struct survey_info *info;
	unsigned int tick;
	int err;

	err = carl9170_exec_cmd(ar, CARL9170_CMD_TALLY, 0, NULL,
				sizeof(tally), (u8 *)&tally);
	if (err)
		return err;

	tick = le32_to_cpu(tally.tick);
	if (tick) {
		ar->tally.active += le32_to_cpu(tally.active) / tick;
		ar->tally.cca += le32_to_cpu(tally.cca) / tick;
		ar->tally.tx_time += le32_to_cpu(tally.tx_time) / tick;
		ar->tally.rx_total += le32_to_cpu(tally.rx_total);
		ar->tally.rx_overrun += le32_to_cpu(tally.rx_overrun);

		if (ar->channel) {
			info = &ar->survey[ar->channel->hw_value];
			info->channel_time = ar->tally.active;
			info->channel_time_busy = ar->tally.cca;
			info->channel_time_tx = ar->tally.tx_time;
			do_div(info->channel_time, 1000);
			do_div(info->channel_time_busy, 1000);
			do_div(info->channel_time_tx, 1000);
		}
	}
	return 0;
}

int carl9170_powersave(struct ar9170 *ar, const bool ps)
{
	struct carl9170_cmd *cmd;
	u32 state;

	cmd = carl9170_cmd_buf(ar, CARL9170_CMD_PSM_ASYNC,
			       sizeof(struct carl9170_psm));
	if (!cmd)
		return -ENOMEM;

	if (ps) {
		/* Sleep until next TBTT */
		state = CARL9170_PSM_SLEEP | 1;
	} else {
		/* wake up immediately */
		state = 1;
	}

	cmd->psm.state = cpu_to_le32(state);
	return __carl9170_exec_cmd(ar, cmd, true);
}
