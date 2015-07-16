/*
 * Atheros CARL9170 driver
 *
 * PHY and RF code
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

#include <linux/bitrev.h>
#include "carl9170.h"
#include "cmd.h"
#include "phy.h"

static int carl9170_init_power_cal(struct ar9170 *ar)
{
	carl9170_regwrite_begin(ar);

	carl9170_regwrite(AR9170_PHY_REG_POWER_TX_RATE_MAX, 0x7f);
	carl9170_regwrite(AR9170_PHY_REG_POWER_TX_RATE1, 0x3f3f3f3f);
	carl9170_regwrite(AR9170_PHY_REG_POWER_TX_RATE2, 0x3f3f3f3f);
	carl9170_regwrite(AR9170_PHY_REG_POWER_TX_RATE3, 0x3f3f3f3f);
	carl9170_regwrite(AR9170_PHY_REG_POWER_TX_RATE4, 0x3f3f3f3f);
	carl9170_regwrite(AR9170_PHY_REG_POWER_TX_RATE5, 0x3f3f3f3f);
	carl9170_regwrite(AR9170_PHY_REG_POWER_TX_RATE6, 0x3f3f3f3f);
	carl9170_regwrite(AR9170_PHY_REG_POWER_TX_RATE7, 0x3f3f3f3f);
	carl9170_regwrite(AR9170_PHY_REG_POWER_TX_RATE8, 0x3f3f3f3f);
	carl9170_regwrite(AR9170_PHY_REG_POWER_TX_RATE9, 0x3f3f3f3f);

	carl9170_regwrite_finish();
	return carl9170_regwrite_result();
}

struct carl9170_phy_init {
	u32 reg, _5ghz_20, _5ghz_40, _2ghz_40, _2ghz_20;
};

static struct carl9170_phy_init ar5416_phy_init[] = {
	{ 0x1c5800, 0x00000007, 0x00000007, 0x00000007, 0x00000007, },
	{ 0x1c5804, 0x00000300, 0x000003c4, 0x000003c4, 0x00000300, },
	{ 0x1c5808, 0x00000000, 0x00000000, 0x00000000, 0x00000000, },
	{ 0x1c580c, 0xad848e19, 0xad848e19, 0xad848e19, 0xad848e19, },
	{ 0x1c5810, 0x7d14e000, 0x7d14e000, 0x7d14e000, 0x7d14e000, },
	{ 0x1c5814, 0x9c0a9f6b, 0x9c0a9f6b, 0x9c0a9f6b, 0x9c0a9f6b, },
	{ 0x1c5818, 0x00000090, 0x00000090, 0x00000090, 0x00000090, },
	{ 0x1c581c, 0x00000000, 0x00000000, 0x00000000, 0x00000000, },
	{ 0x1c5820, 0x02020200, 0x02020200, 0x02020200, 0x02020200, },
	{ 0x1c5824, 0x00000e0e, 0x00000e0e, 0x00000e0e, 0x00000e0e, },
	{ 0x1c5828, 0x0a020001, 0x0a020001, 0x0a020001, 0x0a020001, },
	{ 0x1c582c, 0x0000a000, 0x0000a000, 0x0000a000, 0x0000a000, },
	{ 0x1c5830, 0x00000000, 0x00000000, 0x00000000, 0x00000000, },
	{ 0x1c5834, 0x00000e0e, 0x00000e0e, 0x00000e0e, 0x00000e0e, },
	{ 0x1c5838, 0x00000007, 0x00000007, 0x00000007, 0x00000007, },
	{ 0x1c583c, 0x00200400, 0x00200400, 0x00200400, 0x00200400, },
	{ 0x1c5840, 0x206a002e, 0x206a002e, 0x206a002e, 0x206a002e, },
	{ 0x1c5844, 0x1372161e, 0x13721c1e, 0x13721c24, 0x137216a4, },
	{ 0x1c5848, 0x001a6a65, 0x001a6a65, 0x00197a68, 0x00197a68, },
	{ 0x1c584c, 0x1284233c, 0x1284233c, 0x1284233c, 0x1284233c, },
	{ 0x1c5850, 0x6c48b4e4, 0x6d48b4e4, 0x6d48b0e4, 0x6c48b0e4, },
	{ 0x1c5854, 0x00000859, 0x00000859, 0x00000859, 0x00000859, },
	{ 0x1c5858, 0x7ec80d2e, 0x7ec80d2e, 0x7ec80d2e, 0x7ec80d2e, },
	{ 0x1c585c, 0x31395c5e, 0x3139605e, 0x3139605e, 0x31395c5e, },
	{ 0x1c5860, 0x0004dd10, 0x0004dd10, 0x0004dd20, 0x0004dd20, },
	{ 0x1c5864, 0x0001c600, 0x0001c600, 0x0001c600, 0x0001c600, },
	{ 0x1c5868, 0x409a4190, 0x409a4190, 0x409a4190, 0x409a4190, },
	{ 0x1c586c, 0x050cb081, 0x050cb081, 0x050cb081, 0x050cb081, },
	{ 0x1c5900, 0x00000000, 0x00000000, 0x00000000, 0x00000000, },
	{ 0x1c5904, 0x00000000, 0x00000000, 0x00000000, 0x00000000, },
	{ 0x1c5908, 0x00000000, 0x00000000, 0x00000000, 0x00000000, },
	{ 0x1c590c, 0x00000000, 0x00000000, 0x00000000, 0x00000000, },
	{ 0x1c5914, 0x000007d0, 0x000007d0, 0x00000898, 0x00000898, },
	{ 0x1c5918, 0x00000118, 0x00000230, 0x00000268, 0x00000134, },
	{ 0x1c591c, 0x10000fff, 0x10000fff, 0x10000fff, 0x10000fff, },
	{ 0x1c5920, 0x0510081c, 0x0510081c, 0x0510001c, 0x0510001c, },
	{ 0x1c5924, 0xd0058a15, 0xd0058a15, 0xd0058a15, 0xd0058a15, },
	{ 0x1c5928, 0x00000001, 0x00000001, 0x00000001, 0x00000001, },
	{ 0x1c592c, 0x00000004, 0x00000004, 0x00000004, 0x00000004, },
	{ 0x1c5934, 0x3f3f3f3f, 0x3f3f3f3f, 0x3f3f3f3f, 0x3f3f3f3f, },
	{ 0x1c5938, 0x3f3f3f3f, 0x3f3f3f3f, 0x3f3f3f3f, 0x3f3f3f3f, },
	{ 0x1c593c, 0x0000007f, 0x0000007f, 0x0000007f, 0x0000007f, },
	{ 0x1c5944, 0xdfb81020, 0xdfb81020, 0xdfb81020, 0xdfb81020, },
	{ 0x1c5948, 0x9280b212, 0x9280b212, 0x9280b212, 0x9280b212, },
	{ 0x1c594c, 0x00020028, 0x00020028, 0x00020028, 0x00020028, },
	{ 0x1c5954, 0x5d50e188, 0x5d50e188, 0x5d50e188, 0x5d50e188, },
	{ 0x1c5958, 0x00081fff, 0x00081fff, 0x00081fff, 0x00081fff, },
	{ 0x1c5960, 0x00009b40, 0x00009b40, 0x00009b40, 0x00009b40, },
	{ 0x1c5964, 0x00001120, 0x00001120, 0x00001120, 0x00001120, },
	{ 0x1c5970, 0x190fb515, 0x190fb515, 0x190fb515, 0x190fb515, },
	{ 0x1c5974, 0x00000000, 0x00000000, 0x00000000, 0x00000000, },
	{ 0x1c5978, 0x00000001, 0x00000001, 0x00000001, 0x00000001, },
	{ 0x1c597c, 0x00000000, 0x00000000, 0x00000000, 0x00000000, },
	{ 0x1c5980, 0x00000000, 0x00000000, 0x00000000, 0x00000000, },
	{ 0x1c5984, 0x00000000, 0x00000000, 0x00000000, 0x00000000, },
	{ 0x1c5988, 0x00000000, 0x00000000, 0x00000000, 0x00000000, },
	{ 0x1c598c, 0x00000000, 0x00000000, 0x00000000, 0x00000000, },
	{ 0x1c5990, 0x00000000, 0x00000000, 0x00000000, 0x00000000, },
	{ 0x1c5994, 0x00000000, 0x00000000, 0x00000000, 0x00000000, },
	{ 0x1c5998, 0x00000000, 0x00000000, 0x00000000, 0x00000000, },
	{ 0x1c599c, 0x00000000, 0x00000000, 0x00000000, 0x00000000, },
	{ 0x1c59a0, 0x00000000, 0x00000000, 0x00000000, 0x00000000, },
	{ 0x1c59a4, 0x00000007, 0x00000007, 0x00000007, 0x00000007, },
	{ 0x1c59a8, 0x001fff00, 0x001fff00, 0x001fff00, 0x001fff00, },
	{ 0x1c59ac, 0x006f00c4, 0x006f00c4, 0x006f00c4, 0x006f00c4, },
	{ 0x1c59b0, 0x03051000, 0x03051000, 0x03051000, 0x03051000, },
	{ 0x1c59b4, 0x00000820, 0x00000820, 0x00000820, 0x00000820, },
	{ 0x1c59bc, 0x00181400, 0x00181400, 0x00181400, 0x00181400, },
	{ 0x1c59c0, 0x038919be, 0x038919be, 0x038919be, 0x038919be, },
	{ 0x1c59c4, 0x06336f77, 0x06336f77, 0x06336f77, 0x06336f77, },
	{ 0x1c59c8, 0x6af6532c, 0x6af6532c, 0x6af6532c, 0x6af6532c, },
	{ 0x1c59cc, 0x08f186c8, 0x08f186c8, 0x08f186c8, 0x08f186c8, },
	{ 0x1c59d0, 0x00046384, 0x00046384, 0x00046384, 0x00046384, },
	{ 0x1c59d4, 0x00000000, 0x00000000, 0x00000000, 0x00000000, },
	{ 0x1c59d8, 0x00000000, 0x00000000, 0x00000000, 0x00000000, },
	{ 0x1c59dc, 0x00000000, 0x00000000, 0x00000000, 0x00000000, },
	{ 0x1c59e0, 0x00000200, 0x00000200, 0x00000200, 0x00000200, },
	{ 0x1c59e4, 0x64646464, 0x64646464, 0x64646464, 0x64646464, },
	{ 0x1c59e8, 0x3c787878, 0x3c787878, 0x3c787878, 0x3c787878, },
	{ 0x1c59ec, 0x000000aa, 0x000000aa, 0x000000aa, 0x000000aa, },
	{ 0x1c59f0, 0x00000000, 0x00000000, 0x00000000, 0x00000000, },
	{ 0x1c59fc, 0x00001042, 0x00001042, 0x00001042, 0x00001042, },
	{ 0x1c5a00, 0x00000000, 0x00000000, 0x00000000, 0x00000000, },
	{ 0x1c5a04, 0x00000040, 0x00000040, 0x00000040, 0x00000040, },
	{ 0x1c5a08, 0x00000080, 0x00000080, 0x00000080, 0x00000080, },
	{ 0x1c5a0c, 0x000001a1, 0x000001a1, 0x00000141, 0x00000141, },
	{ 0x1c5a10, 0x000001e1, 0x000001e1, 0x00000181, 0x00000181, },
	{ 0x1c5a14, 0x00000021, 0x00000021, 0x000001c1, 0x000001c1, },
	{ 0x1c5a18, 0x00000061, 0x00000061, 0x00000001, 0x00000001, },
	{ 0x1c5a1c, 0x00000168, 0x00000168, 0x00000041, 0x00000041, },
	{ 0x1c5a20, 0x000001a8, 0x000001a8, 0x000001a8, 0x000001a8, },
	{ 0x1c5a24, 0x000001e8, 0x000001e8, 0x000001e8, 0x000001e8, },
	{ 0x1c5a28, 0x00000028, 0x00000028, 0x00000028, 0x00000028, },
	{ 0x1c5a2c, 0x00000068, 0x00000068, 0x00000068, 0x00000068, },
	{ 0x1c5a30, 0x00000189, 0x00000189, 0x000000a8, 0x000000a8, },
	{ 0x1c5a34, 0x000001c9, 0x000001c9, 0x00000169, 0x00000169, },
	{ 0x1c5a38, 0x00000009, 0x00000009, 0x000001a9, 0x000001a9, },
	{ 0x1c5a3c, 0x00000049, 0x00000049, 0x000001e9, 0x000001e9, },
	{ 0x1c5a40, 0x00000089, 0x00000089, 0x00000029, 0x00000029, },
	{ 0x1c5a44, 0x00000170, 0x00000170, 0x00000069, 0x00000069, },
	{ 0x1c5a48, 0x000001b0, 0x000001b0, 0x00000190, 0x00000190, },
	{ 0x1c5a4c, 0x000001f0, 0x000001f0, 0x000001d0, 0x000001d0, },
	{ 0x1c5a50, 0x00000030, 0x00000030, 0x00000010, 0x00000010, },
	{ 0x1c5a54, 0x00000070, 0x00000070, 0x00000050, 0x00000050, },
	{ 0x1c5a58, 0x00000191, 0x00000191, 0x00000090, 0x00000090, },
	{ 0x1c5a5c, 0x000001d1, 0x000001d1, 0x00000151, 0x00000151, },
	{ 0x1c5a60, 0x00000011, 0x00000011, 0x00000191, 0x00000191, },
	{ 0x1c5a64, 0x00000051, 0x00000051, 0x000001d1, 0x000001d1, },
	{ 0x1c5a68, 0x00000091, 0x00000091, 0x00000011, 0x00000011, },
	{ 0x1c5a6c, 0x000001b8, 0x000001b8, 0x00000051, 0x00000051, },
	{ 0x1c5a70, 0x000001f8, 0x000001f8, 0x00000198, 0x00000198, },
	{ 0x1c5a74, 0x00000038, 0x00000038, 0x000001d8, 0x000001d8, },
	{ 0x1c5a78, 0x00000078, 0x00000078, 0x00000018, 0x00000018, },
	{ 0x1c5a7c, 0x00000199, 0x00000199, 0x00000058, 0x00000058, },
	{ 0x1c5a80, 0x000001d9, 0x000001d9, 0x00000098, 0x00000098, },
	{ 0x1c5a84, 0x00000019, 0x00000019, 0x00000159, 0x00000159, },
	{ 0x1c5a88, 0x00000059, 0x00000059, 0x00000199, 0x00000199, },
	{ 0x1c5a8c, 0x00000099, 0x00000099, 0x000001d9, 0x000001d9, },
	{ 0x1c5a90, 0x000000d9, 0x000000d9, 0x00000019, 0x00000019, },
	{ 0x1c5a94, 0x000000f9, 0x000000f9, 0x00000059, 0x00000059, },
	{ 0x1c5a98, 0x000000f9, 0x000000f9, 0x00000099, 0x00000099, },
	{ 0x1c5a9c, 0x000000f9, 0x000000f9, 0x000000d9, 0x000000d9, },
	{ 0x1c5aa0, 0x000000f9, 0x000000f9, 0x000000f9, 0x000000f9, },
	{ 0x1c5aa4, 0x000000f9, 0x000000f9, 0x000000f9, 0x000000f9, },
	{ 0x1c5aa8, 0x000000f9, 0x000000f9, 0x000000f9, 0x000000f9, },
	{ 0x1c5aac, 0x000000f9, 0x000000f9, 0x000000f9, 0x000000f9, },
	{ 0x1c5ab0, 0x000000f9, 0x000000f9, 0x000000f9, 0x000000f9, },
	{ 0x1c5ab4, 0x000000f9, 0x000000f9, 0x000000f9, 0x000000f9, },
	{ 0x1c5ab8, 0x000000f9, 0x000000f9, 0x000000f9, 0x000000f9, },
	{ 0x1c5abc, 0x000000f9, 0x000000f9, 0x000000f9, 0x000000f9, },
	{ 0x1c5ac0, 0x000000f9, 0x000000f9, 0x000000f9, 0x000000f9, },
	{ 0x1c5ac4, 0x000000f9, 0x000000f9, 0x000000f9, 0x000000f9, },
	{ 0x1c5ac8, 0x000000f9, 0x000000f9, 0x000000f9, 0x000000f9, },
	{ 0x1c5acc, 0x000000f9, 0x000000f9, 0x000000f9, 0x000000f9, },
	{ 0x1c5ad0, 0x000000f9, 0x000000f9, 0x000000f9, 0x000000f9, },
	{ 0x1c5ad4, 0x000000f9, 0x000000f9, 0x000000f9, 0x000000f9, },
	{ 0x1c5ad8, 0x000000f9, 0x000000f9, 0x000000f9, 0x000000f9, },
	{ 0x1c5adc, 0x000000f9, 0x000000f9, 0x000000f9, 0x000000f9, },
	{ 0x1c5ae0, 0x000000f9, 0x000000f9, 0x000000f9, 0x000000f9, },
	{ 0x1c5ae4, 0x000000f9, 0x000000f9, 0x000000f9, 0x000000f9, },
	{ 0x1c5ae8, 0x000000f9, 0x000000f9, 0x000000f9, 0x000000f9, },
	{ 0x1c5aec, 0x000000f9, 0x000000f9, 0x000000f9, 0x000000f9, },
	{ 0x1c5af0, 0x000000f9, 0x000000f9, 0x000000f9, 0x000000f9, },
	{ 0x1c5af4, 0x000000f9, 0x000000f9, 0x000000f9, 0x000000f9, },
	{ 0x1c5af8, 0x000000f9, 0x000000f9, 0x000000f9, 0x000000f9, },
	{ 0x1c5afc, 0x000000f9, 0x000000f9, 0x000000f9, 0x000000f9, },
	{ 0x1c5b00, 0x00000000, 0x00000000, 0x00000000, 0x00000000, },
	{ 0x1c5b04, 0x00000001, 0x00000001, 0x00000001, 0x00000001, },
	{ 0x1c5b08, 0x00000002, 0x00000002, 0x00000002, 0x00000002, },
	{ 0x1c5b0c, 0x00000003, 0x00000003, 0x00000003, 0x00000003, },
	{ 0x1c5b10, 0x00000004, 0x00000004, 0x00000004, 0x00000004, },
	{ 0x1c5b14, 0x00000005, 0x00000005, 0x00000005, 0x00000005, },
	{ 0x1c5b18, 0x00000008, 0x00000008, 0x00000008, 0x00000008, },
	{ 0x1c5b1c, 0x00000009, 0x00000009, 0x00000009, 0x00000009, },
	{ 0x1c5b20, 0x0000000a, 0x0000000a, 0x0000000a, 0x0000000a, },
	{ 0x1c5b24, 0x0000000b, 0x0000000b, 0x0000000b, 0x0000000b, },
	{ 0x1c5b28, 0x0000000c, 0x0000000c, 0x0000000c, 0x0000000c, },
	{ 0x1c5b2c, 0x0000000d, 0x0000000d, 0x0000000d, 0x0000000d, },
	{ 0x1c5b30, 0x00000010, 0x00000010, 0x00000010, 0x00000010, },
	{ 0x1c5b34, 0x00000011, 0x00000011, 0x00000011, 0x00000011, },
	{ 0x1c5b38, 0x00000012, 0x00000012, 0x00000012, 0x00000012, },
	{ 0x1c5b3c, 0x00000013, 0x00000013, 0x00000013, 0x00000013, },
	{ 0x1c5b40, 0x00000014, 0x00000014, 0x00000014, 0x00000014, },
	{ 0x1c5b44, 0x00000015, 0x00000015, 0x00000015, 0x00000015, },
	{ 0x1c5b48, 0x00000018, 0x00000018, 0x00000018, 0x00000018, },
	{ 0x1c5b4c, 0x00000019, 0x00000019, 0x00000019, 0x00000019, },
	{ 0x1c5b50, 0x0000001a, 0x0000001a, 0x0000001a, 0x0000001a, },
	{ 0x1c5b54, 0x0000001b, 0x0000001b, 0x0000001b, 0x0000001b, },
	{ 0x1c5b58, 0x0000001c, 0x0000001c, 0x0000001c, 0x0000001c, },
	{ 0x1c5b5c, 0x0000001d, 0x0000001d, 0x0000001d, 0x0000001d, },
	{ 0x1c5b60, 0x00000020, 0x00000020, 0x00000020, 0x00000020, },
	{ 0x1c5b64, 0x00000021, 0x00000021, 0x00000021, 0x00000021, },
	{ 0x1c5b68, 0x00000022, 0x00000022, 0x00000022, 0x00000022, },
	{ 0x1c5b6c, 0x00000023, 0x00000023, 0x00000023, 0x00000023, },
	{ 0x1c5b70, 0x00000024, 0x00000024, 0x00000024, 0x00000024, },
	{ 0x1c5b74, 0x00000025, 0x00000025, 0x00000025, 0x00000025, },
	{ 0x1c5b78, 0x00000028, 0x00000028, 0x00000028, 0x00000028, },
	{ 0x1c5b7c, 0x00000029, 0x00000029, 0x00000029, 0x00000029, },
	{ 0x1c5b80, 0x0000002a, 0x0000002a, 0x0000002a, 0x0000002a, },
	{ 0x1c5b84, 0x0000002b, 0x0000002b, 0x0000002b, 0x0000002b, },
	{ 0x1c5b88, 0x0000002c, 0x0000002c, 0x0000002c, 0x0000002c, },
	{ 0x1c5b8c, 0x0000002d, 0x0000002d, 0x0000002d, 0x0000002d, },
	{ 0x1c5b90, 0x00000030, 0x00000030, 0x00000030, 0x00000030, },
	{ 0x1c5b94, 0x00000031, 0x00000031, 0x00000031, 0x00000031, },
	{ 0x1c5b98, 0x00000032, 0x00000032, 0x00000032, 0x00000032, },
	{ 0x1c5b9c, 0x00000033, 0x00000033, 0x00000033, 0x00000033, },
	{ 0x1c5ba0, 0x00000034, 0x00000034, 0x00000034, 0x00000034, },
	{ 0x1c5ba4, 0x00000035, 0x00000035, 0x00000035, 0x00000035, },
	{ 0x1c5ba8, 0x00000035, 0x00000035, 0x00000035, 0x00000035, },
	{ 0x1c5bac, 0x00000035, 0x00000035, 0x00000035, 0x00000035, },
	{ 0x1c5bb0, 0x00000035, 0x00000035, 0x00000035, 0x00000035, },
	{ 0x1c5bb4, 0x00000035, 0x00000035, 0x00000035, 0x00000035, },
	{ 0x1c5bb8, 0x00000035, 0x00000035, 0x00000035, 0x00000035, },
	{ 0x1c5bbc, 0x00000035, 0x00000035, 0x00000035, 0x00000035, },
	{ 0x1c5bc0, 0x00000035, 0x00000035, 0x00000035, 0x00000035, },
	{ 0x1c5bc4, 0x00000035, 0x00000035, 0x00000035, 0x00000035, },
	{ 0x1c5bc8, 0x00000035, 0x00000035, 0x00000035, 0x00000035, },
	{ 0x1c5bcc, 0x00000035, 0x00000035, 0x00000035, 0x00000035, },
	{ 0x1c5bd0, 0x00000035, 0x00000035, 0x00000035, 0x00000035, },
	{ 0x1c5bd4, 0x00000035, 0x00000035, 0x00000035, 0x00000035, },
	{ 0x1c5bd8, 0x00000035, 0x00000035, 0x00000035, 0x00000035, },
	{ 0x1c5bdc, 0x00000035, 0x00000035, 0x00000035, 0x00000035, },
	{ 0x1c5be0, 0x00000035, 0x00000035, 0x00000035, 0x00000035, },
	{ 0x1c5be4, 0x00000035, 0x00000035, 0x00000035, 0x00000035, },
	{ 0x1c5be8, 0x00000035, 0x00000035, 0x00000035, 0x00000035, },
	{ 0x1c5bec, 0x00000035, 0x00000035, 0x00000035, 0x00000035, },
	{ 0x1c5bf0, 0x00000035, 0x00000035, 0x00000035, 0x00000035, },
	{ 0x1c5bf4, 0x00000035, 0x00000035, 0x00000035, 0x00000035, },
	{ 0x1c5bf8, 0x00000010, 0x00000010, 0x00000010, 0x00000010, },
	{ 0x1c5bfc, 0x0000001a, 0x0000001a, 0x0000001a, 0x0000001a, },
	{ 0x1c5c00, 0x00000000, 0x00000000, 0x00000000, 0x00000000, },
	{ 0x1c5c0c, 0x00000000, 0x00000000, 0x00000000, 0x00000000, },
	{ 0x1c5c10, 0x00000000, 0x00000000, 0x00000000, 0x00000000, },
	{ 0x1c5c14, 0x00000000, 0x00000000, 0x00000000, 0x00000000, },
	{ 0x1c5c18, 0x00000000, 0x00000000, 0x00000000, 0x00000000, },
	{ 0x1c5c1c, 0x00000000, 0x00000000, 0x00000000, 0x00000000, },
	{ 0x1c5c20, 0x00000000, 0x00000000, 0x00000000, 0x00000000, },
	{ 0x1c5c24, 0x00000000, 0x00000000, 0x00000000, 0x00000000, },
	{ 0x1c5c28, 0x00000000, 0x00000000, 0x00000000, 0x00000000, },
	{ 0x1c5c2c, 0x00000000, 0x00000000, 0x00000000, 0x00000000, },
	{ 0x1c5c30, 0x00000000, 0x00000000, 0x00000000, 0x00000000, },
	{ 0x1c5c34, 0x00000000, 0x00000000, 0x00000000, 0x00000000, },
	{ 0x1c5c38, 0x00000000, 0x00000000, 0x00000000, 0x00000000, },
	{ 0x1c5c3c, 0x00000000, 0x00000000, 0x00000000, 0x00000000, },
	{ 0x1c5cf0, 0x00000000, 0x00000000, 0x00000000, 0x00000000, },
	{ 0x1c5cf4, 0x00000000, 0x00000000, 0x00000000, 0x00000000, },
	{ 0x1c5cf8, 0x00000000, 0x00000000, 0x00000000, 0x00000000, },
	{ 0x1c5cfc, 0x00000000, 0x00000000, 0x00000000, 0x00000000, },
	{ 0x1c6200, 0x00000008, 0x00000008, 0x0000000e, 0x0000000e, },
	{ 0x1c6204, 0x00000440, 0x00000440, 0x00000440, 0x00000440, },
	{ 0x1c6208, 0xd6be4788, 0xd6be4788, 0xd03e4788, 0xd03e4788, },
	{ 0x1c620c, 0x012e8160, 0x012e8160, 0x012a8160, 0x012a8160, },
	{ 0x1c6210, 0x40806333, 0x40806333, 0x40806333, 0x40806333, },
	{ 0x1c6214, 0x00106c10, 0x00106c10, 0x00106c10, 0x00106c10, },
	{ 0x1c6218, 0x009c4060, 0x009c4060, 0x009c4060, 0x009c4060, },
	{ 0x1c621c, 0x1883800a, 0x1883800a, 0x1883800a, 0x1883800a, },
	{ 0x1c6220, 0x018830c6, 0x018830c6, 0x018830c6, 0x018830c6, },
	{ 0x1c6224, 0x00000400, 0x00000400, 0x00000400, 0x00000400, },
	{ 0x1c6228, 0x000009b5, 0x000009b5, 0x000009b5, 0x000009b5, },
	{ 0x1c622c, 0x00000000, 0x00000000, 0x00000000, 0x00000000, },
	{ 0x1c6230, 0x00000108, 0x00000210, 0x00000210, 0x00000108, },
	{ 0x1c6234, 0x3f3f3f3f, 0x3f3f3f3f, 0x3f3f3f3f, 0x3f3f3f3f, },
	{ 0x1c6238, 0x3f3f3f3f, 0x3f3f3f3f, 0x3f3f3f3f, 0x3f3f3f3f, },
	{ 0x1c623c, 0x13c889af, 0x13c889af, 0x13c889af, 0x13c889af, },
	{ 0x1c6240, 0x38490a20, 0x38490a20, 0x38490a20, 0x38490a20, },
	{ 0x1c6244, 0x00007bb6, 0x00007bb6, 0x00007bb6, 0x00007bb6, },
	{ 0x1c6248, 0x0fff3ffc, 0x0fff3ffc, 0x0fff3ffc, 0x0fff3ffc, },
	{ 0x1c624c, 0x00000001, 0x00000001, 0x00000001, 0x00000001, },
	{ 0x1c6250, 0x0000a000, 0x0000a000, 0x0000a000, 0x0000a000, },
	{ 0x1c6254, 0x00000000, 0x00000000, 0x00000000, 0x00000000, },
	{ 0x1c6258, 0x0cc75380, 0x0cc75380, 0x0cc75380, 0x0cc75380, },
	{ 0x1c625c, 0x0f0f0f01, 0x0f0f0f01, 0x0f0f0f01, 0x0f0f0f01, },
	{ 0x1c6260, 0xdfa91f01, 0xdfa91f01, 0xdfa91f01, 0xdfa91f01, },
	{ 0x1c6264, 0x00418a11, 0x00418a11, 0x00418a11, 0x00418a11, },
	{ 0x1c6268, 0x00000000, 0x00000000, 0x00000000, 0x00000000, },
	{ 0x1c626c, 0x09249126, 0x09249126, 0x09249126, 0x09249126, },
	{ 0x1c6274, 0x0a1a9caa, 0x0a1a9caa, 0x0a1a7caa, 0x0a1a7caa, },
	{ 0x1c6278, 0x1ce739ce, 0x1ce739ce, 0x1ce739ce, 0x1ce739ce, },
	{ 0x1c627c, 0x051701ce, 0x051701ce, 0x051701ce, 0x051701ce, },
	{ 0x1c6300, 0x18010000, 0x18010000, 0x18010000, 0x18010000, },
	{ 0x1c6304, 0x30032602, 0x30032602, 0x2e032402, 0x2e032402, },
	{ 0x1c6308, 0x48073e06, 0x48073e06, 0x4a0a3c06, 0x4a0a3c06, },
	{ 0x1c630c, 0x560b4c0a, 0x560b4c0a, 0x621a540b, 0x621a540b, },
	{ 0x1c6310, 0x641a600f, 0x641a600f, 0x764f6c1b, 0x764f6c1b, },
	{ 0x1c6314, 0x7a4f6e1b, 0x7a4f6e1b, 0x845b7a5a, 0x845b7a5a, },
	{ 0x1c6318, 0x8c5b7e5a, 0x8c5b7e5a, 0x950f8ccf, 0x950f8ccf, },
	{ 0x1c631c, 0x9d0f96cf, 0x9d0f96cf, 0xa5cf9b4f, 0xa5cf9b4f, },
	{ 0x1c6320, 0xb51fa69f, 0xb51fa69f, 0xbddfaf1f, 0xbddfaf1f, },
	{ 0x1c6324, 0xcb3fbd07, 0xcb3fbcbf, 0xd1ffc93f, 0xd1ffc93f, },
	{ 0x1c6328, 0x0000d7bf, 0x0000d7bf, 0x00000000, 0x00000000, },
	{ 0x1c632c, 0x00000000, 0x00000000, 0x00000000, 0x00000000, },
	{ 0x1c6330, 0x00000000, 0x00000000, 0x00000000, 0x00000000, },
	{ 0x1c6334, 0x00000000, 0x00000000, 0x00000000, 0x00000000, },
	{ 0x1c6338, 0x00000000, 0x00000000, 0x00000000, 0x00000000, },
	{ 0x1c633c, 0x00000000, 0x00000000, 0x00000000, 0x00000000, },
	{ 0x1c6340, 0x00000000, 0x00000000, 0x00000000, 0x00000000, },
	{ 0x1c6344, 0x00000000, 0x00000000, 0x00000000, 0x00000000, },
	{ 0x1c6348, 0x3fffffff, 0x3fffffff, 0x3fffffff, 0x3fffffff, },
	{ 0x1c634c, 0x3fffffff, 0x3fffffff, 0x3fffffff, 0x3fffffff, },
	{ 0x1c6350, 0x3fffffff, 0x3fffffff, 0x3fffffff, 0x3fffffff, },
	{ 0x1c6354, 0x0003ffff, 0x0003ffff, 0x0003ffff, 0x0003ffff, },
	{ 0x1c6358, 0x79a8aa1f, 0x79a8aa1f, 0x79a8aa1f, 0x79a8aa1f, },
	{ 0x1c6388, 0x08000000, 0x08000000, 0x08000000, 0x08000000, },
	{ 0x1c638c, 0x3f3f3f3f, 0x3f3f3f3f, 0x3f3f3f3f, 0x3f3f3f3f, },
	{ 0x1c6390, 0x3f3f3f3f, 0x3f3f3f3f, 0x3f3f3f3f, 0x3f3f3f3f, },
	{ 0x1c6394, 0x1ce739ce, 0x1ce739ce, 0x1ce739ce, 0x1ce739ce, },
	{ 0x1c6398, 0x000001ce, 0x000001ce, 0x000001ce, 0x000001ce, },
	{ 0x1c639c, 0x00000007, 0x00000007, 0x00000007, 0x00000007, },
	{ 0x1c63a0, 0x00000000, 0x00000000, 0x00000000, 0x00000000, },
	{ 0x1c63a4, 0x00000000, 0x00000000, 0x00000000, 0x00000000, },
	{ 0x1c63a8, 0x00000000, 0x00000000, 0x00000000, 0x00000000, },
	{ 0x1c63ac, 0x00000000, 0x00000000, 0x00000000, 0x00000000, },
	{ 0x1c63b0, 0x00000000, 0x00000000, 0x00000000, 0x00000000, },
	{ 0x1c63b4, 0x00000000, 0x00000000, 0x00000000, 0x00000000, },
	{ 0x1c63b8, 0x00000000, 0x00000000, 0x00000000, 0x00000000, },
	{ 0x1c63bc, 0x00000000, 0x00000000, 0x00000000, 0x00000000, },
	{ 0x1c63c0, 0x00000000, 0x00000000, 0x00000000, 0x00000000, },
	{ 0x1c63c4, 0x00000000, 0x00000000, 0x00000000, 0x00000000, },
	{ 0x1c63c8, 0x00000000, 0x00000000, 0x00000000, 0x00000000, },
	{ 0x1c63cc, 0x3f3f3f3f, 0x3f3f3f3f, 0x3f3f3f3f, 0x3f3f3f3f, },
	{ 0x1c63d0, 0x3f3f3f3f, 0x3f3f3f3f, 0x3f3f3f3f, 0x3f3f3f3f, },
	{ 0x1c63d4, 0x3f3f3f3f, 0x3f3f3f3f, 0x3f3f3f3f, 0x3f3f3f3f, },
	{ 0x1c63d8, 0x00000000, 0x00000000, 0x00000000, 0x00000000, },
	{ 0x1c63dc, 0x1ce739ce, 0x1ce739ce, 0x1ce739ce, 0x1ce739ce, },
	{ 0x1c63e0, 0x000000c0, 0x000000c0, 0x000000c0, 0x000000c0, },
	{ 0x1c6848, 0x00180a65, 0x00180a65, 0x00180a68, 0x00180a68, },
	{ 0x1c6920, 0x0510001c, 0x0510001c, 0x0510001c, 0x0510001c, },
	{ 0x1c6960, 0x00009b40, 0x00009b40, 0x00009b40, 0x00009b40, },
	{ 0x1c720c, 0x012e8160, 0x012e8160, 0x012a8160, 0x012a8160, },
	{ 0x1c726c, 0x09249126, 0x09249126, 0x09249126, 0x09249126, },
	{ 0x1c7848, 0x00180a65, 0x00180a65, 0x00180a68, 0x00180a68, },
	{ 0x1c7920, 0x0510001c, 0x0510001c, 0x0510001c, 0x0510001c, },
	{ 0x1c7960, 0x00009b40, 0x00009b40, 0x00009b40, 0x00009b40, },
	{ 0x1c820c, 0x012e8160, 0x012e8160, 0x012a8160, 0x012a8160, },
	{ 0x1c826c, 0x09249126, 0x09249126, 0x09249126, 0x09249126, },
/*	{ 0x1c8864, 0x0001ce00, 0x0001ce00, 0x0001ce00, 0x0001ce00, }, */
	{ 0x1c8864, 0x0001c600, 0x0001c600, 0x0001c600, 0x0001c600, },
	{ 0x1c895c, 0x004b6a8e, 0x004b6a8e, 0x004b6a8e, 0x004b6a8e, },
	{ 0x1c8968, 0x000003ce, 0x000003ce, 0x000003ce, 0x000003ce, },
	{ 0x1c89bc, 0x00181400, 0x00181400, 0x00181400, 0x00181400, },
	{ 0x1c9270, 0x00820820, 0x00820820, 0x00820820, 0x00820820, },
	{ 0x1c935c, 0x066c420f, 0x066c420f, 0x066c420f, 0x066c420f, },
	{ 0x1c9360, 0x0f282207, 0x0f282207, 0x0f282207, 0x0f282207, },
	{ 0x1c9364, 0x17601685, 0x17601685, 0x17601685, 0x17601685, },
	{ 0x1c9368, 0x1f801104, 0x1f801104, 0x1f801104, 0x1f801104, },
	{ 0x1c936c, 0x37a00c03, 0x37a00c03, 0x37a00c03, 0x37a00c03, },
	{ 0x1c9370, 0x3fc40883, 0x3fc40883, 0x3fc40883, 0x3fc40883, },
	{ 0x1c9374, 0x57c00803, 0x57c00803, 0x57c00803, 0x57c00803, },
	{ 0x1c9378, 0x5fd80682, 0x5fd80682, 0x5fd80682, 0x5fd80682, },
	{ 0x1c937c, 0x7fe00482, 0x7fe00482, 0x7fe00482, 0x7fe00482, },
	{ 0x1c9380, 0x7f3c7bba, 0x7f3c7bba, 0x7f3c7bba, 0x7f3c7bba, },
	{ 0x1c9384, 0xf3307ff0, 0xf3307ff0, 0xf3307ff0, 0xf3307ff0, }
};

/*
 * look up a certain register in ar5416_phy_init[] and return the init. value
 * for the band and bandwidth given. Return 0 if register address not found.
 */
static u32 carl9170_def_val(u32 reg, bool is_2ghz, bool is_40mhz)
{
	unsigned int i;
	for (i = 0; i < ARRAY_SIZE(ar5416_phy_init); i++) {
		if (ar5416_phy_init[i].reg != reg)
			continue;

		if (is_2ghz) {
			if (is_40mhz)
				return ar5416_phy_init[i]._2ghz_40;
			else
				return ar5416_phy_init[i]._2ghz_20;
		} else {
			if (is_40mhz)
				return ar5416_phy_init[i]._5ghz_40;
			else
				return ar5416_phy_init[i]._5ghz_20;
		}
	}
	return 0;
}

/*
 * initialize some phy regs from eeprom values in modal_header[]
 * acc. to band and bandwidth
 */
static int carl9170_init_phy_from_eeprom(struct ar9170 *ar,
				bool is_2ghz, bool is_40mhz)
{
	static const u8 xpd2pd[16] = {
		0x2, 0x2, 0x2, 0x1, 0x2, 0x2, 0x6, 0x2,
		0x2, 0x3, 0x7, 0x2, 0xb, 0x2, 0x2, 0x2
	};
	/* pointer to the modal_header acc. to band */
	struct ar9170_eeprom_modal *m = &ar->eeprom.modal_header[is_2ghz];
	u32 val;

	carl9170_regwrite_begin(ar);

	/* ant common control (index 0) */
	carl9170_regwrite(AR9170_PHY_REG_SWITCH_COM,
		le32_to_cpu(m->antCtrlCommon));

	/* ant control chain 0 (index 1) */
	carl9170_regwrite(AR9170_PHY_REG_SWITCH_CHAIN_0,
		le32_to_cpu(m->antCtrlChain[0]));

	/* ant control chain 2 (index 2) */
	carl9170_regwrite(AR9170_PHY_REG_SWITCH_CHAIN_2,
		le32_to_cpu(m->antCtrlChain[1]));

	/* SwSettle (index 3) */
	if (!is_40mhz) {
		val = carl9170_def_val(AR9170_PHY_REG_SETTLING,
				     is_2ghz, is_40mhz);
		SET_VAL(AR9170_PHY_SETTLING_SWITCH, val, m->switchSettling);
		carl9170_regwrite(AR9170_PHY_REG_SETTLING, val);
	}

	/* adcDesired, pdaDesired (index 4) */
	val = carl9170_def_val(AR9170_PHY_REG_DESIRED_SZ, is_2ghz, is_40mhz);
	SET_VAL(AR9170_PHY_DESIRED_SZ_PGA, val, m->pgaDesiredSize);
	SET_VAL(AR9170_PHY_DESIRED_SZ_ADC, val, m->adcDesiredSize);
	carl9170_regwrite(AR9170_PHY_REG_DESIRED_SZ, val);

	/* TxEndToXpaOff, TxFrameToXpaOn (index 5) */
	val = carl9170_def_val(AR9170_PHY_REG_RF_CTL4, is_2ghz, is_40mhz);
	SET_VAL(AR9170_PHY_RF_CTL4_TX_END_XPAB_OFF, val, m->txEndToXpaOff);
	SET_VAL(AR9170_PHY_RF_CTL4_TX_END_XPAA_OFF, val, m->txEndToXpaOff);
	SET_VAL(AR9170_PHY_RF_CTL4_FRAME_XPAB_ON, val, m->txFrameToXpaOn);
	SET_VAL(AR9170_PHY_RF_CTL4_FRAME_XPAA_ON, val, m->txFrameToXpaOn);
	carl9170_regwrite(AR9170_PHY_REG_RF_CTL4, val);

	/* TxEndToRxOn (index 6) */
	val = carl9170_def_val(AR9170_PHY_REG_RF_CTL3, is_2ghz, is_40mhz);
	SET_VAL(AR9170_PHY_RF_CTL3_TX_END_TO_A2_RX_ON, val, m->txEndToRxOn);
	carl9170_regwrite(AR9170_PHY_REG_RF_CTL3, val);

	/* thresh62 (index 7) */
	val = carl9170_def_val(0x1c8864, is_2ghz, is_40mhz);
	val = (val & ~0x7f000) | (m->thresh62 << 12);
	carl9170_regwrite(0x1c8864, val);

	/* tx/rx attenuation chain 0 (index 8) */
	val = carl9170_def_val(AR9170_PHY_REG_RXGAIN, is_2ghz, is_40mhz);
	SET_VAL(AR9170_PHY_RXGAIN_TXRX_ATTEN, val, m->txRxAttenCh[0]);
	carl9170_regwrite(AR9170_PHY_REG_RXGAIN, val);

	/* tx/rx attenuation chain 2 (index 9) */
	val = carl9170_def_val(AR9170_PHY_REG_RXGAIN_CHAIN_2,
			       is_2ghz, is_40mhz);
	SET_VAL(AR9170_PHY_RXGAIN_TXRX_ATTEN, val, m->txRxAttenCh[1]);
	carl9170_regwrite(AR9170_PHY_REG_RXGAIN_CHAIN_2, val);

	/* tx/rx margin chain 0 (index 10) */
	val = carl9170_def_val(AR9170_PHY_REG_GAIN_2GHZ, is_2ghz, is_40mhz);
	SET_VAL(AR9170_PHY_GAIN_2GHZ_RXTX_MARGIN, val, m->rxTxMarginCh[0]);
	/* bsw margin chain 0 for 5GHz only */
	if (!is_2ghz)
		SET_VAL(AR9170_PHY_GAIN_2GHZ_BSW_MARGIN, val, m->bswMargin[0]);
	carl9170_regwrite(AR9170_PHY_REG_GAIN_2GHZ, val);

	/* tx/rx margin chain 2 (index 11) */
	val = carl9170_def_val(AR9170_PHY_REG_GAIN_2GHZ_CHAIN_2,
			       is_2ghz, is_40mhz);
	SET_VAL(AR9170_PHY_GAIN_2GHZ_RXTX_MARGIN, val, m->rxTxMarginCh[1]);
	carl9170_regwrite(AR9170_PHY_REG_GAIN_2GHZ_CHAIN_2, val);

	/* iqCall, iqCallq chain 0 (index 12) */
	val = carl9170_def_val(AR9170_PHY_REG_TIMING_CTRL4(0),
			       is_2ghz, is_40mhz);
	SET_VAL(AR9170_PHY_TIMING_CTRL4_IQCORR_Q_I_COFF, val, m->iqCalICh[0]);
	SET_VAL(AR9170_PHY_TIMING_CTRL4_IQCORR_Q_Q_COFF, val, m->iqCalQCh[0]);
	carl9170_regwrite(AR9170_PHY_REG_TIMING_CTRL4(0), val);

	/* iqCall, iqCallq chain 2 (index 13) */
	val = carl9170_def_val(AR9170_PHY_REG_TIMING_CTRL4(2),
			       is_2ghz, is_40mhz);
	SET_VAL(AR9170_PHY_TIMING_CTRL4_IQCORR_Q_I_COFF, val, m->iqCalICh[1]);
	SET_VAL(AR9170_PHY_TIMING_CTRL4_IQCORR_Q_Q_COFF, val, m->iqCalQCh[1]);
	carl9170_regwrite(AR9170_PHY_REG_TIMING_CTRL4(2), val);

	/* xpd gain mask (index 14) */
	val = carl9170_def_val(AR9170_PHY_REG_TPCRG1, is_2ghz, is_40mhz);
	SET_VAL(AR9170_PHY_TPCRG1_PD_GAIN_1, val,
		xpd2pd[m->xpdGain & 0xf] & 3);
	SET_VAL(AR9170_PHY_TPCRG1_PD_GAIN_2, val,
		xpd2pd[m->xpdGain & 0xf] >> 2);
	carl9170_regwrite(AR9170_PHY_REG_TPCRG1, val);

	carl9170_regwrite(AR9170_PHY_REG_RX_CHAINMASK, ar->eeprom.rx_mask);
	carl9170_regwrite(AR9170_PHY_REG_CAL_CHAINMASK, ar->eeprom.rx_mask);

	carl9170_regwrite_finish();
	return carl9170_regwrite_result();
}

static int carl9170_init_phy(struct ar9170 *ar, enum ieee80211_band band)
{
	int i, err;
	u32 val;
	bool is_2ghz = band == IEEE80211_BAND_2GHZ;
	bool is_40mhz = conf_is_ht40(&ar->hw->conf);

	carl9170_regwrite_begin(ar);

	for (i = 0; i < ARRAY_SIZE(ar5416_phy_init); i++) {
		if (is_40mhz) {
			if (is_2ghz)
				val = ar5416_phy_init[i]._2ghz_40;
			else
				val = ar5416_phy_init[i]._5ghz_40;
		} else {
			if (is_2ghz)
				val = ar5416_phy_init[i]._2ghz_20;
			else
				val = ar5416_phy_init[i]._5ghz_20;
		}

		carl9170_regwrite(ar5416_phy_init[i].reg, val);
	}

	carl9170_regwrite_finish();
	err = carl9170_regwrite_result();
	if (err)
		return err;

	err = carl9170_init_phy_from_eeprom(ar, is_2ghz, is_40mhz);
	if (err)
		return err;

	err = carl9170_init_power_cal(ar);
	if (err)
		return err;

	if (!ar->fw.hw_counters) {
		err = carl9170_write_reg(ar, AR9170_PWR_REG_PLL_ADDAC,
					 is_2ghz ? 0x5163 : 0x5143);
	}

	return err;
}

struct carl9170_rf_initvals {
	u32 reg, _5ghz, _2ghz;
};

static struct carl9170_rf_initvals carl9170_rf_initval[] = {
	/* bank 0 */
	{ 0x1c58b0, 0x1e5795e5, 0x1e5795e5},
	{ 0x1c58e0, 0x02008020, 0x02008020},
	/* bank 1 */
	{ 0x1c58b0, 0x02108421, 0x02108421},
	{ 0x1c58ec, 0x00000008, 0x00000008},
	/* bank 2 */
	{ 0x1c58b0, 0x0e73ff17, 0x0e73ff17},
	{ 0x1c58e0, 0x00000420, 0x00000420},
	/* bank 3 */
	{ 0x1c58f0, 0x01400018, 0x01c00018},
	/* bank 4 */
	{ 0x1c58b0, 0x000001a1, 0x000001a1},
	{ 0x1c58e8, 0x00000001, 0x00000001},
	/* bank 5 */
	{ 0x1c58b0, 0x00000013, 0x00000013},
	{ 0x1c58e4, 0x00000002, 0x00000002},
	/* bank 6 */
	{ 0x1c58b0, 0x00000000, 0x00000000},
	{ 0x1c58b0, 0x00000000, 0x00000000},
	{ 0x1c58b0, 0x00000000, 0x00000000},
	{ 0x1c58b0, 0x00000000, 0x00000000},
	{ 0x1c58b0, 0x00000000, 0x00000000},
	{ 0x1c58b0, 0x00004000, 0x00004000},
	{ 0x1c58b0, 0x00006c00, 0x00006c00},
	{ 0x1c58b0, 0x00002c00, 0x00002c00},
	{ 0x1c58b0, 0x00004800, 0x00004800},
	{ 0x1c58b0, 0x00004000, 0x00004000},
	{ 0x1c58b0, 0x00006000, 0x00006000},
	{ 0x1c58b0, 0x00001000, 0x00001000},
	{ 0x1c58b0, 0x00004000, 0x00004000},
	{ 0x1c58b0, 0x00007c00, 0x00007c00},
	{ 0x1c58b0, 0x00007c00, 0x00007c00},
	{ 0x1c58b0, 0x00007c00, 0x00007c00},
	{ 0x1c58b0, 0x00007c00, 0x00007c00},
	{ 0x1c58b0, 0x00007c00, 0x00007c00},
	{ 0x1c58b0, 0x00087c00, 0x00087c00},
	{ 0x1c58b0, 0x00007c00, 0x00007c00},
	{ 0x1c58b0, 0x00005400, 0x00005400},
	{ 0x1c58b0, 0x00000c00, 0x00000c00},
	{ 0x1c58b0, 0x00001800, 0x00001800},
	{ 0x1c58b0, 0x00007c00, 0x00007c00},
	{ 0x1c58b0, 0x00006c00, 0x00006c00},
	{ 0x1c58b0, 0x00006c00, 0x00006c00},
	{ 0x1c58b0, 0x00007c00, 0x00007c00},
	{ 0x1c58b0, 0x00002c00, 0x00002c00},
	{ 0x1c58b0, 0x00003c00, 0x00003c00},
	{ 0x1c58b0, 0x00003800, 0x00003800},
	{ 0x1c58b0, 0x00001c00, 0x00001c00},
	{ 0x1c58b0, 0x00000800, 0x00000800},
	{ 0x1c58b0, 0x00000408, 0x00000408},
	{ 0x1c58b0, 0x00004c15, 0x00004c15},
	{ 0x1c58b0, 0x00004188, 0x00004188},
	{ 0x1c58b0, 0x0000201e, 0x0000201e},
	{ 0x1c58b0, 0x00010408, 0x00010408},
	{ 0x1c58b0, 0x00000801, 0x00000801},
	{ 0x1c58b0, 0x00000c08, 0x00000c08},
	{ 0x1c58b0, 0x0000181e, 0x0000181e},
	{ 0x1c58b0, 0x00001016, 0x00001016},
	{ 0x1c58b0, 0x00002800, 0x00002800},
	{ 0x1c58b0, 0x00004010, 0x00004010},
	{ 0x1c58b0, 0x0000081c, 0x0000081c},
	{ 0x1c58b0, 0x00000115, 0x00000115},
	{ 0x1c58b0, 0x00000015, 0x00000015},
	{ 0x1c58b0, 0x00000066, 0x00000066},
	{ 0x1c58b0, 0x0000001c, 0x0000001c},
	{ 0x1c58b0, 0x00000000, 0x00000000},
	{ 0x1c58b0, 0x00000004, 0x00000004},
	{ 0x1c58b0, 0x00000015, 0x00000015},
	{ 0x1c58b0, 0x0000001f, 0x0000001f},
	{ 0x1c58e0, 0x00000000, 0x00000400},
	/* bank 7 */
	{ 0x1c58b0, 0x000000a0, 0x000000a0},
	{ 0x1c58b0, 0x00000000, 0x00000000},
	{ 0x1c58b0, 0x00000040, 0x00000040},
	{ 0x1c58f0, 0x0000001c, 0x0000001c},
};

static int carl9170_init_rf_banks_0_7(struct ar9170 *ar, bool band5ghz)
{
	int err, i;

	carl9170_regwrite_begin(ar);

	for (i = 0; i < ARRAY_SIZE(carl9170_rf_initval); i++)
		carl9170_regwrite(carl9170_rf_initval[i].reg,
				  band5ghz ? carl9170_rf_initval[i]._5ghz
					   : carl9170_rf_initval[i]._2ghz);

	carl9170_regwrite_finish();
	err = carl9170_regwrite_result();
	if (err)
		wiphy_err(ar->hw->wiphy, "rf init failed\n");

	return err;
}

struct carl9170_phy_freq_params {
	u8 coeff_exp;
	u16 coeff_man;
	u8 coeff_exp_shgi;
	u16 coeff_man_shgi;
};

enum carl9170_bw {
	CARL9170_BW_20,
	CARL9170_BW_40_BELOW,
	CARL9170_BW_40_ABOVE,

	__CARL9170_NUM_BW,
};

struct carl9170_phy_freq_entry {
	u16 freq;
	struct carl9170_phy_freq_params params[__CARL9170_NUM_BW];
};

/* NB: must be in sync with channel tables in main! */
static const struct carl9170_phy_freq_entry carl9170_phy_freq_params[] = {
/*
 *	freq,
 *		20MHz,
 *		40MHz (below),
 *		40Mhz (above),
 */
	{ 2412, {
		{ 3, 21737, 3, 19563, },
		{ 3, 21827, 3, 19644, },
		{ 3, 21647, 3, 19482, },
	} },
	{ 2417, {
		{ 3, 21692, 3, 19523, },
		{ 3, 21782, 3, 19604, },
		{ 3, 21602, 3, 19442, },
	} },
	{ 2422, {
		{ 3, 21647, 3, 19482, },
		{ 3, 21737, 3, 19563, },
		{ 3, 21558, 3, 19402, },
	} },
	{ 2427, {
		{ 3, 21602, 3, 19442, },
		{ 3, 21692, 3, 19523, },
		{ 3, 21514, 3, 19362, },
	} },
	{ 2432, {
		{ 3, 21558, 3, 19402, },
		{ 3, 21647, 3, 19482, },
		{ 3, 21470, 3, 19323, },
	} },
	{ 2437, {
		{ 3, 21514, 3, 19362, },
		{ 3, 21602, 3, 19442, },
		{ 3, 21426, 3, 19283, },
	} },
	{ 2442, {
		{ 3, 21470, 3, 19323, },
		{ 3, 21558, 3, 19402, },
		{ 3, 21382, 3, 19244, },
	} },
	{ 2447, {
		{ 3, 21426, 3, 19283, },
		{ 3, 21514, 3, 19362, },
		{ 3, 21339, 3, 19205, },
	} },
	{ 2452, {
		{ 3, 21382, 3, 19244, },
		{ 3, 21470, 3, 19323, },
		{ 3, 21295, 3, 19166, },
	} },
	{ 2457, {
		{ 3, 21339, 3, 19205, },
		{ 3, 21426, 3, 19283, },
		{ 3, 21252, 3, 19127, },
	} },
	{ 2462, {
		{ 3, 21295, 3, 19166, },
		{ 3, 21382, 3, 19244, },
		{ 3, 21209, 3, 19088, },
	} },
	{ 2467, {
		{ 3, 21252, 3, 19127, },
		{ 3, 21339, 3, 19205, },
		{ 3, 21166, 3, 19050, },
	} },
	{ 2472, {
		{ 3, 21209, 3, 19088, },
		{ 3, 21295, 3, 19166, },
		{ 3, 21124, 3, 19011, },
	} },
	{ 2484, {
		{ 3, 21107, 3, 18996, },
		{ 3, 21192, 3, 19073, },
		{ 3, 21022, 3, 18920, },
	} },
	{ 4920, {
		{ 4, 21313, 4, 19181, },
		{ 4, 21356, 4, 19220, },
		{ 4, 21269, 4, 19142, },
	} },
	{ 4940, {
		{ 4, 21226, 4, 19104, },
		{ 4, 21269, 4, 19142, },
		{ 4, 21183, 4, 19065, },
	} },
	{ 4960, {
		{ 4, 21141, 4, 19027, },
		{ 4, 21183, 4, 19065, },
		{ 4, 21098, 4, 18988, },
	} },
	{ 4980, {
		{ 4, 21056, 4, 18950, },
		{ 4, 21098, 4, 18988, },
		{ 4, 21014, 4, 18912, },
	} },
	{ 5040, {
		{ 4, 20805, 4, 18725, },
		{ 4, 20846, 4, 18762, },
		{ 4, 20764, 4, 18687, },
	} },
	{ 5060, {
		{ 4, 20723, 4, 18651, },
		{ 4, 20764, 4, 18687, },
		{ 4, 20682, 4, 18614, },
	} },
	{ 5080, {
		{ 4, 20641, 4, 18577, },
		{ 4, 20682, 4, 18614, },
		{ 4, 20601, 4, 18541, },
	} },
	{ 5180, {
		{ 4, 20243, 4, 18219, },
		{ 4, 20282, 4, 18254, },
		{ 4, 20204, 4, 18183, },
	} },
	{ 5200, {
		{ 4, 20165, 4, 18148, },
		{ 4, 20204, 4, 18183, },
		{ 4, 20126, 4, 18114, },
	} },
	{ 5220, {
		{ 4, 20088, 4, 18079, },
		{ 4, 20126, 4, 18114, },
		{ 4, 20049, 4, 18044, },
	} },
	{ 5240, {
		{ 4, 20011, 4, 18010, },
		{ 4, 20049, 4, 18044, },
		{ 4, 19973, 4, 17976, },
	} },
	{ 5260, {
		{ 4, 19935, 4, 17941, },
		{ 4, 19973, 4, 17976, },
		{ 4, 19897, 4, 17907, },
	} },
	{ 5280, {
		{ 4, 19859, 4, 17873, },
		{ 4, 19897, 4, 17907, },
		{ 4, 19822, 4, 17840, },
	} },
	{ 5300, {
		{ 4, 19784, 4, 17806, },
		{ 4, 19822, 4, 17840, },
		{ 4, 19747, 4, 17772, },
	} },
	{ 5320, {
		{ 4, 19710, 4, 17739, },
		{ 4, 19747, 4, 17772, },
		{ 4, 19673, 4, 17706, },
	} },
	{ 5500, {
		{ 4, 19065, 4, 17159, },
		{ 4, 19100, 4, 17190, },
		{ 4, 19030, 4, 17127, },
	} },
	{ 5520, {
		{ 4, 18996, 4, 17096, },
		{ 4, 19030, 4, 17127, },
		{ 4, 18962, 4, 17065, },
	} },
	{ 5540, {
		{ 4, 18927, 4, 17035, },
		{ 4, 18962, 4, 17065, },
		{ 4, 18893, 4, 17004, },
	} },
	{ 5560, {
		{ 4, 18859, 4, 16973, },
		{ 4, 18893, 4, 17004, },
		{ 4, 18825, 4, 16943, },
	} },
	{ 5580, {
		{ 4, 18792, 4, 16913, },
		{ 4, 18825, 4, 16943, },
		{ 4, 18758, 4, 16882, },
	} },
	{ 5600, {
		{ 4, 18725, 4, 16852, },
		{ 4, 18758, 4, 16882, },
		{ 4, 18691, 4, 16822, },
	} },
	{ 5620, {
		{ 4, 18658, 4, 16792, },
		{ 4, 18691, 4, 16822, },
		{ 4, 18625, 4, 16762, },
	} },
	{ 5640, {
		{ 4, 18592, 4, 16733, },
		{ 4, 18625, 4, 16762, },
		{ 4, 18559, 4, 16703, },
	} },
	{ 5660, {
		{ 4, 18526, 4, 16673, },
		{ 4, 18559, 4, 16703, },
		{ 4, 18493, 4, 16644, },
	} },
	{ 5680, {
		{ 4, 18461, 4, 16615, },
		{ 4, 18493, 4, 16644, },
		{ 4, 18428, 4, 16586, },
	} },
	{ 5700, {
		{ 4, 18396, 4, 16556, },
		{ 4, 18428, 4, 16586, },
		{ 4, 18364, 4, 16527, },
	} },
	{ 5745, {
		{ 4, 18252, 4, 16427, },
		{ 4, 18284, 4, 16455, },
		{ 4, 18220, 4, 16398, },
	} },
	{ 5765, {
		{ 4, 18189, 5, 32740, },
		{ 4, 18220, 4, 16398, },
		{ 4, 18157, 5, 32683, },
	} },
	{ 5785, {
		{ 4, 18126, 5, 32626, },
		{ 4, 18157, 5, 32683, },
		{ 4, 18094, 5, 32570, },
	} },
	{ 5805, {
		{ 4, 18063, 5, 32514, },
		{ 4, 18094, 5, 32570, },
		{ 4, 18032, 5, 32458, },
	} },
	{ 5825, {
		{ 4, 18001, 5, 32402, },
		{ 4, 18032, 5, 32458, },
		{ 4, 17970, 5, 32347, },
	} },
	{ 5170, {
		{ 4, 20282, 4, 18254, },
		{ 4, 20321, 4, 18289, },
		{ 4, 20243, 4, 18219, },
	} },
	{ 5190, {
		{ 4, 20204, 4, 18183, },
		{ 4, 20243, 4, 18219, },
		{ 4, 20165, 4, 18148, },
	} },
	{ 5210, {
		{ 4, 20126, 4, 18114, },
		{ 4, 20165, 4, 18148, },
		{ 4, 20088, 4, 18079, },
	} },
	{ 5230, {
		{ 4, 20049, 4, 18044, },
		{ 4, 20088, 4, 18079, },
		{ 4, 20011, 4, 18010, },
	} },
};

static int carl9170_init_rf_bank4_pwr(struct ar9170 *ar, bool band5ghz,
				      u32 freq, enum carl9170_bw bw)
{
	int err;
	u32 d0, d1, td0, td1, fd0, fd1;
	u8 chansel;
	u8 refsel0 = 1, refsel1 = 0;
	u8 lf_synth = 0;

	switch (bw) {
	case CARL9170_BW_40_ABOVE:
		freq += 10;
		break;
	case CARL9170_BW_40_BELOW:
		freq -= 10;
		break;
	case CARL9170_BW_20:
		break;
	default:
		BUG();
		return -ENOSYS;
	}

	if (band5ghz) {
		if (freq % 10) {
			chansel = (freq - 4800) / 5;
		} else {
			chansel = ((freq - 4800) / 10) * 2;
			refsel0 = 0;
			refsel1 = 1;
		}
		chansel = bitrev8(chansel);
	} else {
		if (freq == 2484) {
			chansel = 10 + (freq - 2274) / 5;
			lf_synth = 1;
		} else
			chansel = 16 + (freq - 2272) / 5;
		chansel *= 4;
		chansel = bitrev8(chansel);
	}

	d1 =	chansel;
	d0 =	0x21 |
		refsel0 << 3 |
		refsel1 << 2 |
		lf_synth << 1;
	td0 =	d0 & 0x1f;
	td1 =	d1 & 0x1f;
	fd0 =	td1 << 5 | td0;

	td0 =	(d0 >> 5) & 0x7;
	td1 =	(d1 >> 5) & 0x7;
	fd1 =	td1 << 5 | td0;

	carl9170_regwrite_begin(ar);

	carl9170_regwrite(0x1c58b0, fd0);
	carl9170_regwrite(0x1c58e8, fd1);

	carl9170_regwrite_finish();
	err = carl9170_regwrite_result();
	if (err)
		return err;

	return 0;
}

static const struct carl9170_phy_freq_params *
carl9170_get_hw_dyn_params(struct ieee80211_channel *channel,
			   enum carl9170_bw bw)
{
	unsigned int chanidx = 0;
	u16 freq = 2412;

	if (channel) {
		chanidx = channel->hw_value;
		freq = channel->center_freq;
	}

	BUG_ON(chanidx >= ARRAY_SIZE(carl9170_phy_freq_params));

	BUILD_BUG_ON(__CARL9170_NUM_BW != 3);

	WARN_ON(carl9170_phy_freq_params[chanidx].freq != freq);

	return &carl9170_phy_freq_params[chanidx].params[bw];
}

static int carl9170_find_freq_idx(int nfreqs, u8 *freqs, u8 f)
{
	int idx = nfreqs - 2;

	while (idx >= 0) {
		if (f >= freqs[idx])
			return idx;
		idx--;
	}

	return 0;
}

static s32 carl9170_interpolate_s32(s32 x, s32 x1, s32 y1, s32 x2, s32 y2)
{
	/* nothing to interpolate, it's horizontal */
	if (y2 == y1)
		return y1;

	/* check if we hit one of the edges */
	if (x == x1)
		return y1;
	if (x == x2)
		return y2;

	/* x1 == x2 is bad, hopefully == x */
	if (x2 == x1)
		return y1;

	return y1 + (((y2 - y1) * (x - x1)) / (x2 - x1));
}

static u8 carl9170_interpolate_u8(u8 x, u8 x1, u8 y1, u8 x2, u8 y2)
{
#define SHIFT		8
	s32 y;

	y = carl9170_interpolate_s32(x << SHIFT, x1 << SHIFT,
		y1 << SHIFT, x2 << SHIFT, y2 << SHIFT);

	/*
	 * XXX: unwrap this expression
	 *	Isn't it just DIV_ROUND_UP(y, 1<<SHIFT)?
	 *	Can we rely on the compiler to optimise away the div?
	 */
	return (y >> SHIFT) + ((y & (1 << (SHIFT - 1))) >> (SHIFT - 1));
#undef SHIFT
}

static u8 carl9170_interpolate_val(u8 x, u8 *x_array, u8 *y_array)
{
	int i;

	for (i = 0; i < 3; i++) {
		if (x <= x_array[i + 1])
			break;
	}

	return carl9170_interpolate_u8(x, x_array[i], y_array[i],
		x_array[i + 1], y_array[i + 1]);
}

static int carl9170_set_freq_cal_data(struct ar9170 *ar,
	struct ieee80211_channel *channel)
{
	u8 *cal_freq_pier;
	u8 vpds[2][AR5416_PD_GAIN_ICEPTS];
	u8 pwrs[2][AR5416_PD_GAIN_ICEPTS];
	int chain, idx, i;
	u32 phy_data = 0;
	u8 f, tmp;

	switch (channel->band) {
	case IEEE80211_BAND_2GHZ:
		f = channel->center_freq - 2300;
		cal_freq_pier = ar->eeprom.cal_freq_pier_2G;
		i = AR5416_NUM_2G_CAL_PIERS - 1;
		break;

	case IEEE80211_BAND_5GHZ:
		f = (channel->center_freq - 4800) / 5;
		cal_freq_pier = ar->eeprom.cal_freq_pier_5G;
		i = AR5416_NUM_5G_CAL_PIERS - 1;
		break;

	default:
		return -EINVAL;
	}

	for (; i >= 0; i--) {
		if (cal_freq_pier[i] != 0xff)
			break;
	}
	if (i < 0)
		return -EINVAL;

	idx = carl9170_find_freq_idx(i, cal_freq_pier, f);

	carl9170_regwrite_begin(ar);

	for (chain = 0; chain < AR5416_MAX_CHAINS; chain++) {
		for (i = 0; i < AR5416_PD_GAIN_ICEPTS; i++) {
			struct ar9170_calibration_data_per_freq *cal_pier_data;
			int j;

			switch (channel->band) {
			case IEEE80211_BAND_2GHZ:
				cal_pier_data = &ar->eeprom.
					cal_pier_data_2G[chain][idx];
				break;

			case IEEE80211_BAND_5GHZ:
				cal_pier_data = &ar->eeprom.
					cal_pier_data_5G[chain][idx];
				break;

			default:
				return -EINVAL;
			}

			for (j = 0; j < 2; j++) {
				vpds[j][i] = carl9170_interpolate_u8(f,
					cal_freq_pier[idx],
					cal_pier_data->vpd_pdg[j][i],
					cal_freq_pier[idx + 1],
					cal_pier_data[1].vpd_pdg[j][i]);

				pwrs[j][i] = carl9170_interpolate_u8(f,
					cal_freq_pier[idx],
					cal_pier_data->pwr_pdg[j][i],
					cal_freq_pier[idx + 1],
					cal_pier_data[1].pwr_pdg[j][i]) / 2;
			}
		}

		for (i = 0; i < 76; i++) {
			if (i < 25) {
				tmp = carl9170_interpolate_val(i, &pwrs[0][0],
							       &vpds[0][0]);
			} else {
				tmp = carl9170_interpolate_val(i - 12,
							       &pwrs[1][0],
							       &vpds[1][0]);
			}

			phy_data |= tmp << ((i & 3) << 3);
			if ((i & 3) == 3) {
				carl9170_regwrite(0x1c6280 + chain * 0x1000 +
						  (i & ~3), phy_data);
				phy_data = 0;
			}
		}

		for (i = 19; i < 32; i++)
			carl9170_regwrite(0x1c6280 + chain * 0x1000 + (i << 2),
					  0x0);
	}

	carl9170_regwrite_finish();
	return carl9170_regwrite_result();
}

static u8 carl9170_get_max_edge_power(struct ar9170 *ar,
	u32 freq, struct ar9170_calctl_edges edges[])
{
	int i;
	u8 rc = AR5416_MAX_RATE_POWER;
	u8 f;
	if (freq < 3000)
		f = freq - 2300;
	else
		f = (freq - 4800) / 5;

	for (i = 0; i < AR5416_NUM_BAND_EDGES; i++) {
		if (edges[i].channel == 0xff)
			break;
		if (f == edges[i].channel) {
			/* exact freq match */
			rc = edges[i].power_flags & ~AR9170_CALCTL_EDGE_FLAGS;
			break;
		}
		if (i > 0 && f < edges[i].channel) {
			if (f > edges[i - 1].channel &&
			    edges[i - 1].power_flags &
			    AR9170_CALCTL_EDGE_FLAGS) {
				/* lower channel has the inband flag set */
				rc = edges[i - 1].power_flags &
					~AR9170_CALCTL_EDGE_FLAGS;
			}
			break;
		}
	}

	if (i == AR5416_NUM_BAND_EDGES) {
		if (f > edges[i - 1].channel &&
		    edges[i - 1].power_flags & AR9170_CALCTL_EDGE_FLAGS) {
			/* lower channel has the inband flag set */
			rc = edges[i - 1].power_flags &
				~AR9170_CALCTL_EDGE_FLAGS;
		}
	}
	return rc;
}

static u8 carl9170_get_heavy_clip(struct ar9170 *ar, u32 freq,
	enum carl9170_bw bw, struct ar9170_calctl_edges edges[])
{
	u8 f;
	int i;
	u8 rc = 0;

	if (freq < 3000)
		f = freq - 2300;
	else
		f = (freq - 4800) / 5;

	if (bw == CARL9170_BW_40_BELOW || bw == CARL9170_BW_40_ABOVE)
		rc |= 0xf0;

	for (i = 0; i < AR5416_NUM_BAND_EDGES; i++) {
		if (edges[i].channel == 0xff)
			break;
		if (f == edges[i].channel) {
			if (!(edges[i].power_flags & AR9170_CALCTL_EDGE_FLAGS))
				rc |= 0x0f;
			break;
		}
	}

	return rc;
}

/*
 * calculate the conformance test limits and the heavy clip parameter
 * and apply them to ar->power* (derived from otus hal/hpmain.c, line 3706)
 */
static void carl9170_calc_ctl(struct ar9170 *ar, u32 freq, enum carl9170_bw bw)
{
	u8 ctl_grp; /* CTL group */
	u8 ctl_idx; /* CTL index */
	int i, j;
	struct ctl_modes {
		u8 ctl_mode;
		u8 max_power;
		u8 *pwr_cal_data;
		int pwr_cal_len;
	} *modes;

	/*
	 * order is relevant in the mode_list_*: we fall back to the
	 * lower indices if any mode is missed in the EEPROM.
	 */
	struct ctl_modes mode_list_2ghz[] = {
		{ CTL_11B, 0, ar->power_2G_cck, 4 },
		{ CTL_11G, 0, ar->power_2G_ofdm, 4 },
		{ CTL_2GHT20, 0, ar->power_2G_ht20, 8 },
		{ CTL_2GHT40, 0, ar->power_2G_ht40, 8 },
	};
	struct ctl_modes mode_list_5ghz[] = {
		{ CTL_11A, 0, ar->power_5G_leg, 4 },
		{ CTL_5GHT20, 0, ar->power_5G_ht20, 8 },
		{ CTL_5GHT40, 0, ar->power_5G_ht40, 8 },
	};
	int nr_modes;

#define EDGES(c, n) (ar->eeprom.ctl_data[c].control_edges[n])

	ar->heavy_clip = 0;

	/*
	 * TODO: investigate the differences between OTUS'
	 * hpreg.c::zfHpGetRegulatoryDomain() and
	 * ath/regd.c::ath_regd_get_band_ctl() -
	 * e.g. for FCC3_WORLD the OTUS procedure
	 * always returns CTL_FCC, while the one in ath/ delivers
	 * CTL_ETSI for 2GHz and CTL_FCC for 5GHz.
	 */
	ctl_grp = ath_regd_get_band_ctl(&ar->common.regulatory,
					ar->hw->conf.chandef.chan->band);

	/* ctl group not found - either invalid band (NO_CTL) or ww roaming */
	if (ctl_grp == NO_CTL || ctl_grp == SD_NO_CTL)
		ctl_grp = CTL_FCC;

	if (ctl_grp != CTL_FCC)
		/* skip CTL and heavy clip for CTL_MKK and CTL_ETSI */
		return;

	if (ar->hw->conf.chandef.chan->band == IEEE80211_BAND_2GHZ) {
		modes = mode_list_2ghz;
		nr_modes = ARRAY_SIZE(mode_list_2ghz);
	} else {
		modes = mode_list_5ghz;
		nr_modes = ARRAY_SIZE(mode_list_5ghz);
	}

	for (i = 0; i < nr_modes; i++) {
		u8 c = ctl_grp | modes[i].ctl_mode;
		for (ctl_idx = 0; ctl_idx < AR5416_NUM_CTLS; ctl_idx++)
			if (c == ar->eeprom.ctl_index[ctl_idx])
				break;
		if (ctl_idx < AR5416_NUM_CTLS) {
			int f_off = 0;

			/*
			 * determine heavy clip parameter
			 * from the 11G edges array
			 */
			if (modes[i].ctl_mode == CTL_11G) {
				ar->heavy_clip =
					carl9170_get_heavy_clip(ar,
						freq, bw, EDGES(ctl_idx, 1));
			}

			/* adjust freq for 40MHz */
			if (modes[i].ctl_mode == CTL_2GHT40 ||
			    modes[i].ctl_mode == CTL_5GHT40) {
				if (bw == CARL9170_BW_40_BELOW)
					f_off = -10;
				else
					f_off = 10;
			}

			modes[i].max_power =
				carl9170_get_max_edge_power(ar,
					freq + f_off, EDGES(ctl_idx, 1));

			/*
			 * TODO: check if the regulatory max. power is
			 * controlled by cfg80211 for DFS.
			 * (hpmain applies it to max_power itself for DFS freq)
			 */

		} else {
			/*
			 * Workaround in otus driver, hpmain.c, line 3906:
			 * if no data for 5GHT20 are found, take the
			 * legacy 5G value. We extend this here to fallback
			 * from any other HT* or 11G, too.
			 */
			int k = i;

			modes[i].max_power = AR5416_MAX_RATE_POWER;
			while (k-- > 0) {
				if (modes[k].max_power !=
				    AR5416_MAX_RATE_POWER) {
					modes[i].max_power = modes[k].max_power;
					break;
				}
			}
		}

		/* apply max power to pwr_cal_data (ar->power_*) */
		for (j = 0; j < modes[i].pwr_cal_len; j++) {
			modes[i].pwr_cal_data[j] = min(modes[i].pwr_cal_data[j],
						       modes[i].max_power);
		}
	}

	if (ar->heavy_clip & 0xf0) {
		ar->power_2G_ht40[0]--;
		ar->power_2G_ht40[1]--;
		ar->power_2G_ht40[2]--;
	}
	if (ar->heavy_clip & 0xf) {
		ar->power_2G_ht20[0]++;
		ar->power_2G_ht20[1]++;
		ar->power_2G_ht20[2]++;
	}

#undef EDGES
}

static void carl9170_set_power_cal(struct ar9170 *ar, u32 freq,
				   enum carl9170_bw bw)
{
	struct ar9170_calibration_target_power_legacy *ctpl;
	struct ar9170_calibration_target_power_ht *ctph;
	u8 *ctpres;
	int ntargets;
	int idx, i, n;
	u8 f;
	u8 pwr_freqs[AR5416_MAX_NUM_TGT_PWRS];

	if (freq < 3000)
		f = freq - 2300;
	else
		f = (freq - 4800) / 5;

	/*
	 * cycle through the various modes
	 *
	 * legacy modes first: 5G, 2G CCK, 2G OFDM
	 */
	for (i = 0; i < 3; i++) {
		switch (i) {
		case 0: /* 5 GHz legacy */
			ctpl = &ar->eeprom.cal_tgt_pwr_5G[0];
			ntargets = AR5416_NUM_5G_TARGET_PWRS;
			ctpres = ar->power_5G_leg;
			break;
		case 1: /* 2.4 GHz CCK */
			ctpl = &ar->eeprom.cal_tgt_pwr_2G_cck[0];
			ntargets = AR5416_NUM_2G_CCK_TARGET_PWRS;
			ctpres = ar->power_2G_cck;
			break;
		case 2: /* 2.4 GHz OFDM */
			ctpl = &ar->eeprom.cal_tgt_pwr_2G_ofdm[0];
			ntargets = AR5416_NUM_2G_OFDM_TARGET_PWRS;
			ctpres = ar->power_2G_ofdm;
			break;
		default:
			BUG();
		}

		for (n = 0; n < ntargets; n++) {
			if (ctpl[n].freq == 0xff)
				break;
			pwr_freqs[n] = ctpl[n].freq;
		}
		ntargets = n;
		idx = carl9170_find_freq_idx(ntargets, pwr_freqs, f);
		for (n = 0; n < 4; n++)
			ctpres[n] = carl9170_interpolate_u8(f,
				ctpl[idx + 0].freq, ctpl[idx + 0].power[n],
				ctpl[idx + 1].freq, ctpl[idx + 1].power[n]);
	}

	/* HT modes now: 5G HT20, 5G HT40, 2G CCK, 2G OFDM, 2G HT20, 2G HT40 */
	for (i = 0; i < 4; i++) {
		switch (i) {
		case 0: /* 5 GHz HT 20 */
			ctph = &ar->eeprom.cal_tgt_pwr_5G_ht20[0];
			ntargets = AR5416_NUM_5G_TARGET_PWRS;
			ctpres = ar->power_5G_ht20;
			break;
		case 1: /* 5 GHz HT 40 */
			ctph = &ar->eeprom.cal_tgt_pwr_5G_ht40[0];
			ntargets = AR5416_NUM_5G_TARGET_PWRS;
			ctpres = ar->power_5G_ht40;
			break;
		case 2: /* 2.4 GHz HT 20 */
			ctph = &ar->eeprom.cal_tgt_pwr_2G_ht20[0];
			ntargets = AR5416_NUM_2G_OFDM_TARGET_PWRS;
			ctpres = ar->power_2G_ht20;
			break;
		case 3: /* 2.4 GHz HT 40 */
			ctph = &ar->eeprom.cal_tgt_pwr_2G_ht40[0];
			ntargets = AR5416_NUM_2G_OFDM_TARGET_PWRS;
			ctpres = ar->power_2G_ht40;
			break;
		default:
			BUG();
		}

		for (n = 0; n < ntargets; n++) {
			if (ctph[n].freq == 0xff)
				break;
			pwr_freqs[n] = ctph[n].freq;
		}
		ntargets = n;
		idx = carl9170_find_freq_idx(ntargets, pwr_freqs, f);
		for (n = 0; n < 8; n++)
			ctpres[n] = carl9170_interpolate_u8(f,
				ctph[idx + 0].freq, ctph[idx + 0].power[n],
				ctph[idx + 1].freq, ctph[idx + 1].power[n]);
	}

	/* calc. conformance test limits and apply to ar->power*[] */
	carl9170_calc_ctl(ar, freq, bw);
}

int carl9170_get_noisefloor(struct ar9170 *ar)
{
	static const u32 phy_regs[] = {
		AR9170_PHY_REG_CCA, AR9170_PHY_REG_CH2_CCA,
		AR9170_PHY_REG_EXT_CCA, AR9170_PHY_REG_CH2_EXT_CCA };
	u32 phy_res[ARRAY_SIZE(phy_regs)];
	int err, i;

	BUILD_BUG_ON(ARRAY_SIZE(phy_regs) != ARRAY_SIZE(ar->noise));

	err = carl9170_read_mreg(ar, ARRAY_SIZE(phy_regs), phy_regs, phy_res);
	if (err)
		return err;

	for (i = 0; i < 2; i++) {
		ar->noise[i] = sign_extend32(GET_VAL(
			AR9170_PHY_CCA_MIN_PWR, phy_res[i]), 8);

		ar->noise[i + 2] = sign_extend32(GET_VAL(
			AR9170_PHY_EXT_CCA_MIN_PWR, phy_res[i + 2]), 8);
	}

	if (ar->channel)
		ar->survey[ar->channel->hw_value].noise = ar->noise[0];

	return 0;
}

static enum carl9170_bw nl80211_to_carl(enum nl80211_channel_type type)
{
	switch (type) {
	case NL80211_CHAN_NO_HT:
	case NL80211_CHAN_HT20:
		return CARL9170_BW_20;
	case NL80211_CHAN_HT40MINUS:
		return CARL9170_BW_40_BELOW;
	case NL80211_CHAN_HT40PLUS:
		return CARL9170_BW_40_ABOVE;
	default:
		BUG();
	}
}

int carl9170_set_channel(struct ar9170 *ar, struct ieee80211_channel *channel,
			 enum nl80211_channel_type _bw)
{
	const struct carl9170_phy_freq_params *freqpar;
	struct carl9170_rf_init_result rf_res;
	struct carl9170_rf_init rf;
	u32 tmp, offs = 0, new_ht = 0;
	int err;
	enum carl9170_bw bw;
	struct ieee80211_channel *old_channel = NULL;

	bw = nl80211_to_carl(_bw);

	if (conf_is_ht(&ar->hw->conf))
		new_ht |= CARL9170FW_PHY_HT_ENABLE;

	if (conf_is_ht40(&ar->hw->conf))
		new_ht |= CARL9170FW_PHY_HT_DYN2040;

	/* may be NULL at first setup */
	if (ar->channel) {
		old_channel = ar->channel;
		ar->channel = NULL;
	}

	/* cold reset BB/ADDA */
	err = carl9170_write_reg(ar, AR9170_PWR_REG_RESET,
				 AR9170_PWR_RESET_BB_COLD_RESET);
	if (err)
		return err;

	err = carl9170_write_reg(ar, AR9170_PWR_REG_RESET, 0x0);
	if (err)
		return err;

	err = carl9170_init_phy(ar, channel->band);
	if (err)
		return err;

	err = carl9170_init_rf_banks_0_7(ar,
					 channel->band == IEEE80211_BAND_5GHZ);
	if (err)
		return err;

	err = carl9170_exec_cmd(ar, CARL9170_CMD_FREQ_START, 0, NULL, 0, NULL);
	if (err)
		return err;

	err = carl9170_write_reg(ar, AR9170_PHY_REG_HEAVY_CLIP_ENABLE,
				 0x200);
	if (err)
		return err;

	err = carl9170_init_rf_bank4_pwr(ar,
					 channel->band == IEEE80211_BAND_5GHZ,
					 channel->center_freq, bw);
	if (err)
		return err;

	tmp = AR9170_PHY_TURBO_FC_SINGLE_HT_LTF1 |
	      AR9170_PHY_TURBO_FC_HT_EN;

	switch (bw) {
	case CARL9170_BW_20:
		break;
	case CARL9170_BW_40_BELOW:
		tmp |= AR9170_PHY_TURBO_FC_DYN2040_EN |
		       AR9170_PHY_TURBO_FC_SHORT_GI_40;
		offs = 3;
		break;
	case CARL9170_BW_40_ABOVE:
		tmp |= AR9170_PHY_TURBO_FC_DYN2040_EN |
		       AR9170_PHY_TURBO_FC_SHORT_GI_40 |
		       AR9170_PHY_TURBO_FC_DYN2040_PRI_CH;
		offs = 1;
		break;
	default:
		BUG();
		return -ENOSYS;
	}

	if (ar->eeprom.tx_mask != 1)
		tmp |= AR9170_PHY_TURBO_FC_WALSH;

	err = carl9170_write_reg(ar, AR9170_PHY_REG_TURBO, tmp);
	if (err)
		return err;

	err = carl9170_set_freq_cal_data(ar, channel);
	if (err)
		return err;

	carl9170_set_power_cal(ar, channel->center_freq, bw);

	err = carl9170_set_mac_tpc(ar, channel);
	if (err)
		return err;

	freqpar = carl9170_get_hw_dyn_params(channel, bw);

	rf.ht_settings = new_ht;
	if (conf_is_ht40(&ar->hw->conf))
		SET_VAL(CARL9170FW_PHY_HT_EXT_CHAN_OFF, rf.ht_settings, offs);

	rf.freq = cpu_to_le32(channel->center_freq * 1000);
	rf.delta_slope_coeff_exp = cpu_to_le32(freqpar->coeff_exp);
	rf.delta_slope_coeff_man = cpu_to_le32(freqpar->coeff_man);
	rf.delta_slope_coeff_exp_shgi = cpu_to_le32(freqpar->coeff_exp_shgi);
	rf.delta_slope_coeff_man_shgi = cpu_to_le32(freqpar->coeff_man_shgi);
	rf.finiteLoopCount = cpu_to_le32(2000);
	err = carl9170_exec_cmd(ar, CARL9170_CMD_RF_INIT, sizeof(rf), &rf,
				sizeof(rf_res), &rf_res);
	if (err)
		return err;

	err = le32_to_cpu(rf_res.ret);
	if (err != 0) {
		ar->chan_fail++;
		ar->total_chan_fail++;

		wiphy_err(ar->hw->wiphy, "channel change: %d -> %d "
			  "failed (%d).\n", old_channel ?
			  old_channel->center_freq : -1, channel->center_freq,
			  err);

		if (ar->chan_fail > 3) {
			/* We have tried very hard to change to _another_
			 * channel and we've failed to do so!
			 * Chances are that the PHY/RF is no longer
			 * operable (due to corruptions/fatal events/bugs?)
			 * and we need to reset at a higher level.
			 */
			carl9170_restart(ar, CARL9170_RR_TOO_MANY_PHY_ERRORS);
			return 0;
		}

		err = carl9170_set_channel(ar, channel, _bw);
		if (err)
			return err;
	} else {
		ar->chan_fail = 0;
	}

	if (ar->heavy_clip) {
		err = carl9170_write_reg(ar, AR9170_PHY_REG_HEAVY_CLIP_ENABLE,
					 0x200 | ar->heavy_clip);
		if (err) {
			if (net_ratelimit()) {
				wiphy_err(ar->hw->wiphy, "failed to set "
				       "heavy clip\n");
			}

			return err;
		}
	}

	ar->channel = channel;
	ar->ht_settings = new_ht;
	return 0;
}
