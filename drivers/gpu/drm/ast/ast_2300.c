// SPDX-License-Identifier: MIT
/*
 * Copyright 2012 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 */
/*
 * Authors: Dave Airlie <airlied@redhat.com>
 */

#include <linux/delay.h>

#include "ast_drv.h"
#include "ast_post.h"

/*
 *  POST
 */

void ast_2300_set_def_ext_reg(struct ast_device *ast)
{
	static const u8 extreginfo[] = { 0x0f, 0x04, 0x1f, 0xff };
	u8 i, index, reg;
	const u8 *ext_reg_info;

	/* reset scratch */
	for (i = 0x81; i <= 0x9f; i++)
		ast_set_index_reg(ast, AST_IO_VGACRI, i, 0x00);

	ext_reg_info = extreginfo;
	index = 0xa0;
	while (*ext_reg_info != 0xff) {
		ast_set_index_reg_mask(ast, AST_IO_VGACRI, index, 0x00, *ext_reg_info);
		index++;
		ext_reg_info++;
	}

	/* disable standard IO/MEM decode if secondary */
	/* ast_set_index_reg-mask(ast, AST_IO_VGACRI, 0xa1, 0xff, 0x3); */

	/* Set Ext. Default */
	ast_set_index_reg_mask(ast, AST_IO_VGACRI, 0x8c, 0x00, 0x01);
	ast_set_index_reg_mask(ast, AST_IO_VGACRI, 0xb7, 0x00, 0x00);

	/* Enable RAMDAC for A1 */
	reg = 0x04;
	reg |= 0x20;
	ast_set_index_reg_mask(ast, AST_IO_VGACRI, 0xb6, 0xff, reg);
}

/* AST 2300 DRAM settings */
#define AST_DDR3 0
#define AST_DDR2 1

struct ast2300_dram_param {
	u32 dram_type;
	u32 dram_chipid;
	u32 dram_freq;
	u32 vram_size;
	u32 odt;
	u32 wodt;
	u32 rodt;
	u32 dram_config;
	u32 reg_PERIOD;
	u32 reg_MADJ;
	u32 reg_SADJ;
	u32 reg_MRS;
	u32 reg_EMRS;
	u32 reg_AC1;
	u32 reg_AC2;
	u32 reg_DQSIC;
	u32 reg_DRV;
	u32 reg_IOZ;
	u32 reg_DQIDLY;
	u32 reg_FREQ;
	u32 madj_max;
	u32 dll2_finetune_step;
};

/*
 * DQSI DLL CBR Setting
 */
#define CBR_SIZE0            ((1  << 10) - 1)
#define CBR_SIZE1            ((4  << 10) - 1)
#define CBR_SIZE2            ((64 << 10) - 1)
#define CBR_PASSNUM          5
#define CBR_PASSNUM2         5
#define CBR_THRESHOLD        10
#define CBR_THRESHOLD2       10
#define TIMEOUT              5000000
#define CBR_PATNUM           8

static const u32 pattern[8] = {
	0xFF00FF00,
	0xCC33CC33,
	0xAA55AA55,
	0x88778877,
	0x92CC4D6E,
	0x543D3CDE,
	0xF1E843C7,
	0x7C61D253
};

static u32 mmc_test2(struct ast_device *ast, u32 datagen, u8 test_ctl)
{
	u32 data, timeout;

	ast_moutdwm(ast, 0x1e6e0070, 0x00000000);
	ast_moutdwm(ast, 0x1e6e0070, (datagen << 3) | test_ctl);
	timeout = 0;
	do {
		data = ast_mindwm(ast, 0x1e6e0070) & 0x1000;
		if (++timeout > TIMEOUT) {
			ast_moutdwm(ast, 0x1e6e0070, 0x0);
			return 0xffffffff;
		}
	} while (!data);
	data = ast_mindwm(ast, 0x1e6e0078);
	data = (data | (data >> 16)) & 0xffff;
	ast_moutdwm(ast, 0x1e6e0070, 0x00000000);
	return data;
}

static u32 mmc_test_burst2(struct ast_device *ast, u32 datagen)
{
	return mmc_test2(ast, datagen, 0x41);
}

static bool mmc_test_single(struct ast_device *ast, u32 datagen)
{
	return mmc_test(ast, datagen, 0xc5);
}

static u32 mmc_test_single2(struct ast_device *ast, u32 datagen)
{
	return mmc_test2(ast, datagen, 0x05);
}

static int cbr_test(struct ast_device *ast)
{
	u32 data;
	int i;

	data = mmc_test_single2(ast, 0);
	if ((data & 0xff) && (data & 0xff00))
		return 0;
	for (i = 0; i < 8; i++) {
		data = mmc_test_burst2(ast, i);
		if ((data & 0xff) && (data & 0xff00))
			return 0;
	}
	if (!data)
		return 3;
	else if (data & 0xff)
		return 2;
	return 1;
}

static int cbr_scan(struct ast_device *ast)
{
	u32 data, data2, patcnt, loop;

	data2 = 3;
	for (patcnt = 0; patcnt < CBR_PATNUM; patcnt++) {
		ast_moutdwm(ast, 0x1e6e007c, pattern[patcnt]);
		for (loop = 0; loop < CBR_PASSNUM2; loop++) {
			data = cbr_test(ast);
			if (data != 0) {
				data2 &= data;
				if (!data2)
					return 0;
				break;
			}
		}
		if (loop == CBR_PASSNUM2)
			return 0;
	}
	return data2;
}

static u32 cbr_test2(struct ast_device *ast)
{
	u32 data;

	data = mmc_test_burst2(ast, 0);
	if (data == 0xffff)
		return 0;
	data |= mmc_test_single2(ast, 0);
	if (data == 0xffff)
		return 0;

	return ~data & 0xffff;
}

static u32 cbr_scan2(struct ast_device *ast)
{
	u32 data, data2, patcnt, loop;

	data2 = 0xffff;
	for (patcnt = 0; patcnt < CBR_PATNUM; patcnt++) {
		ast_moutdwm(ast, 0x1e6e007c, pattern[patcnt]);
		for (loop = 0; loop < CBR_PASSNUM2; loop++) {
			data = cbr_test2(ast);
			if (data != 0) {
				data2 &= data;
				if (!data2)
					return 0;
				break;
			}
		}
		if (loop == CBR_PASSNUM2)
			return 0;
	}
	return data2;
}

static bool cbr_test3(struct ast_device *ast)
{
	if (!mmc_test_burst(ast, 0))
		return false;
	if (!mmc_test_single(ast, 0))
		return false;
	return true;
}

static bool cbr_scan3(struct ast_device *ast)
{
	u32 patcnt, loop;

	for (patcnt = 0; patcnt < CBR_PATNUM; patcnt++) {
		ast_moutdwm(ast, 0x1e6e007c, pattern[patcnt]);
		for (loop = 0; loop < 2; loop++) {
			if (cbr_test3(ast))
				break;
		}
		if (loop == 2)
			return false;
	}
	return true;
}

static bool finetuneDQI_L(struct ast_device *ast, struct ast2300_dram_param *param)
{
	u32 gold_sadj[2], dllmin[16], dllmax[16], dlli, data, cnt, mask, passcnt, retry = 0;
	bool status = false;
FINETUNE_START:
	for (cnt = 0; cnt < 16; cnt++) {
		dllmin[cnt] = 0xff;
		dllmax[cnt] = 0x0;
	}
	passcnt = 0;
	for (dlli = 0; dlli < 76; dlli++) {
		ast_moutdwm(ast, 0x1E6E0068, 0x00001400 | (dlli << 16) | (dlli << 24));
		ast_moutdwm(ast, 0x1E6E0074, CBR_SIZE1);
		data = cbr_scan2(ast);
		if (data != 0) {
			mask = 0x00010001;
			for (cnt = 0; cnt < 16; cnt++) {
				if (data & mask) {
					if (dllmin[cnt] > dlli)
						dllmin[cnt] = dlli;
					if (dllmax[cnt] < dlli)
						dllmax[cnt] = dlli;
				}
				mask <<= 1;
			}
			passcnt++;
		} else if (passcnt >= CBR_THRESHOLD2) {
			break;
		}
	}
	gold_sadj[0] = 0x0;
	passcnt = 0;
	for (cnt = 0; cnt < 16; cnt++) {
		if ((dllmax[cnt] > dllmin[cnt]) &&
		    ((dllmax[cnt] - dllmin[cnt]) >= CBR_THRESHOLD2)) {
			gold_sadj[0] += dllmin[cnt];
			passcnt++;
		}
	}
	if (retry++ > 10)
		goto FINETUNE_DONE;
	if (passcnt != 16)
		goto FINETUNE_START;
	status = true;
FINETUNE_DONE:
	gold_sadj[0] = gold_sadj[0] >> 4;
	gold_sadj[1] = gold_sadj[0];

	data = 0;
	for (cnt = 0; cnt < 8; cnt++) {
		data >>= 3;
		if ((dllmax[cnt] > dllmin[cnt]) &&
		    ((dllmax[cnt] - dllmin[cnt]) >= CBR_THRESHOLD2)) {
			dlli = dllmin[cnt];
			if (gold_sadj[0] >= dlli) {
				dlli = ((gold_sadj[0] - dlli) * 19) >> 5;
				if (dlli > 3)
					dlli = 3;
			} else {
				dlli = ((dlli - gold_sadj[0]) * 19) >> 5;
				if (dlli > 4)
					dlli = 4;
				dlli = (8 - dlli) & 0x7;
			}
			data |= dlli << 21;
		}
	}
	ast_moutdwm(ast, 0x1E6E0080, data);

	data = 0;
	for (cnt = 8; cnt < 16; cnt++) {
		data >>= 3;
		if ((dllmax[cnt] > dllmin[cnt]) &&
		    ((dllmax[cnt] - dllmin[cnt]) >= CBR_THRESHOLD2)) {
			dlli = dllmin[cnt];
			if (gold_sadj[1] >= dlli) {
				dlli = ((gold_sadj[1] - dlli) * 19) >> 5;
				if (dlli > 3)
					dlli = 3;
				else
					dlli = (dlli - 1) & 0x7;
			} else {
				dlli = ((dlli - gold_sadj[1]) * 19) >> 5;
				dlli += 1;
				if (dlli > 4)
					dlli = 4;
				dlli = (8 - dlli) & 0x7;
			}
			data |= dlli << 21;
		}
	}
	ast_moutdwm(ast, 0x1E6E0084, data);
	return status;
} /* finetuneDQI_L */

static void finetuneDQSI(struct ast_device *ast)
{
	u32 dlli, dqsip, dqidly;
	u32 reg_mcr18, reg_mcr0c, passcnt[2], diff;
	u32 g_dqidly, g_dqsip, g_margin, g_side;
	u16 pass[32][2][2];
	char tag[2][76];

	/* Disable DQI CBR */
	reg_mcr0c  = ast_mindwm(ast, 0x1E6E000C);
	reg_mcr18  = ast_mindwm(ast, 0x1E6E0018);
	reg_mcr18 &= 0x0000ffff;
	ast_moutdwm(ast, 0x1E6E0018, reg_mcr18);

	for (dlli = 0; dlli < 76; dlli++) {
		tag[0][dlli] = 0x0;
		tag[1][dlli] = 0x0;
	}
	for (dqidly = 0; dqidly < 32; dqidly++) {
		pass[dqidly][0][0] = 0xff;
		pass[dqidly][0][1] = 0x0;
		pass[dqidly][1][0] = 0xff;
		pass[dqidly][1][1] = 0x0;
	}
	for (dqidly = 0; dqidly < 32; dqidly++) {
		passcnt[0] = 0;
		passcnt[1] = 0;
		for (dqsip = 0; dqsip < 2; dqsip++) {
			ast_moutdwm(ast, 0x1E6E000C, 0);
			ast_moutdwm(ast, 0x1E6E0018, reg_mcr18 | (dqidly << 16) | (dqsip << 23));
			ast_moutdwm(ast, 0x1E6E000C, reg_mcr0c);
			for (dlli = 0; dlli < 76; dlli++) {
				ast_moutdwm(ast, 0x1E6E0068,
					    0x00001300 | (dlli << 16) | (dlli << 24));
				ast_moutdwm(ast, 0x1E6E0070, 0);
				ast_moutdwm(ast, 0x1E6E0074, CBR_SIZE0);
				if (cbr_scan3(ast)) {
					if (dlli == 0)
						break;
					passcnt[dqsip]++;
					tag[dqsip][dlli] = 'P';
					if (dlli < pass[dqidly][dqsip][0])
						pass[dqidly][dqsip][0] = (u16)dlli;
					if (dlli > pass[dqidly][dqsip][1])
						pass[dqidly][dqsip][1] = (u16)dlli;
				} else if (passcnt[dqsip] >= 5) {
					break;
				} else {
					pass[dqidly][dqsip][0] = 0xff;
					pass[dqidly][dqsip][1] = 0x0;
				}
			}
		}
		if (passcnt[0] == 0 && passcnt[1] == 0)
			dqidly++;
	}
	/* Search margin */
	g_dqidly = 0;
	g_dqsip = 0;
	g_margin = 0;
	g_side = 0;

	for (dqidly = 0; dqidly < 32; dqidly++) {
		for (dqsip = 0; dqsip < 2; dqsip++) {
			if (pass[dqidly][dqsip][0] > pass[dqidly][dqsip][1])
				continue;
			diff = pass[dqidly][dqsip][1] - pass[dqidly][dqsip][0];
			if ((diff + 2) < g_margin)
				continue;
			passcnt[0] = 0;
			passcnt[1] = 0;
			for (dlli = pass[dqidly][dqsip][0];
			     dlli > 0 && tag[dqsip][dlli] != 0;
			     dlli--, passcnt[0]++) {
			}
			for (dlli = pass[dqidly][dqsip][1];
			     dlli < 76 && tag[dqsip][dlli] != 0;
			     dlli++, passcnt[1]++) {
			}
			if (passcnt[0] > passcnt[1])
				passcnt[0] = passcnt[1];
			passcnt[1] = 0;
			if (passcnt[0] > g_side)
				passcnt[1] = passcnt[0] - g_side;
			if (diff > (g_margin + 1) && (passcnt[1] > 0 || passcnt[0] > 8)) {
				g_margin = diff;
				g_dqidly = dqidly;
				g_dqsip  = dqsip;
				g_side   = passcnt[0];
			} else if (passcnt[1] > 1 && g_side < 8) {
				if (diff > g_margin)
					g_margin = diff;
				g_dqidly = dqidly;
				g_dqsip  = dqsip;
				g_side   = passcnt[0];
			}
		}
	}
	reg_mcr18 = reg_mcr18 | (g_dqidly << 16) | (g_dqsip << 23);
	ast_moutdwm(ast, 0x1E6E0018, reg_mcr18);
}

static bool cbr_dll2(struct ast_device *ast, struct ast2300_dram_param *param)
{
	u32 dllmin[2], dllmax[2], dlli, data, passcnt, retry = 0;
	bool status = false;

	finetuneDQSI(ast);
	if (finetuneDQI_L(ast, param) == false)
		return status;

CBR_START2:
	dllmin[0] = 0xff;
	dllmin[1] = 0xff;
	dllmax[0] = 0x0;
	dllmax[1] = 0x0;
	passcnt = 0;
	for (dlli = 0; dlli < 76; dlli++) {
		ast_moutdwm(ast, 0x1E6E0068, 0x00001300 | (dlli << 16) | (dlli << 24));
		ast_moutdwm(ast, 0x1E6E0074, CBR_SIZE2);
		data = cbr_scan(ast);
		if (data != 0) {
			if (data & 0x1) {
				if (dllmin[0] > dlli)
					dllmin[0] = dlli;
				if (dllmax[0] < dlli)
					dllmax[0] = dlli;
			}
			if (data & 0x2) {
				if (dllmin[1] > dlli)
					dllmin[1] = dlli;
				if (dllmax[1] < dlli)
					dllmax[1] = dlli;
			}
			passcnt++;
		} else if (passcnt >= CBR_THRESHOLD) {
			break;
		}
	}
	if (retry++ > 10)
		goto CBR_DONE2;
	if (dllmax[0] == 0 || (dllmax[0] - dllmin[0]) < CBR_THRESHOLD)
		goto CBR_START2;
	if (dllmax[1] == 0 || (dllmax[1] - dllmin[1]) < CBR_THRESHOLD)
		goto CBR_START2;
	status = true;
CBR_DONE2:
	dlli  = (dllmin[1] + dllmax[1]) >> 1;
	dlli <<= 8;
	dlli += (dllmin[0] + dllmax[0]) >> 1;
	ast_moutdwm(ast, 0x1E6E0068, ast_mindwm(ast, 0x1E720058) | (dlli << 16));
	return status;
} /* CBRDLL2 */

static void get_ddr3_info(struct ast_device *ast, struct ast2300_dram_param *param)
{
	u32 trap, trap_AC2, trap_MRS;

	ast_moutdwm(ast, 0x1E6E2000, 0x1688A8A8);

	/* Ger trap info */
	trap = (ast_mindwm(ast, 0x1E6E2070) >> 25) & 0x3;
	trap_AC2  = 0x00020000 + (trap << 16);
	trap_AC2 |= 0x00300000 + ((trap & 0x2) << 19);
	trap_MRS  = 0x00000010 + (trap << 4);
	trap_MRS |= ((trap & 0x2) << 18);

	param->reg_MADJ       = 0x00034C4C;
	param->reg_SADJ       = 0x00001800;
	param->reg_DRV        = 0x000000F0;
	param->reg_PERIOD     = param->dram_freq;
	param->rodt           = 0;

	switch (param->dram_freq) {
	case 336:
		ast_moutdwm(ast, 0x1E6E2020, 0x0190);
		param->wodt          = 0;
		param->reg_AC1       = 0x22202725;
		param->reg_AC2       = 0xAA007613 | trap_AC2;
		param->reg_DQSIC     = 0x000000BA;
		param->reg_MRS       = 0x04001400 | trap_MRS;
		param->reg_EMRS      = 0x00000000;
		param->reg_IOZ       = 0x00000023;
		param->reg_DQIDLY    = 0x00000074;
		param->reg_FREQ      = 0x00004DC0;
		param->madj_max      = 96;
		param->dll2_finetune_step = 3;
		switch (param->dram_chipid) {
		default:
		case AST_DRAM_512Mx16:
		case AST_DRAM_1Gx16:
			param->reg_AC2   = 0xAA007613 | trap_AC2;
			break;
		case AST_DRAM_2Gx16:
			param->reg_AC2   = 0xAA00761C | trap_AC2;
			break;
		case AST_DRAM_4Gx16:
			param->reg_AC2   = 0xAA007636 | trap_AC2;
			break;
		}
		break;
	default:
	case 396:
		ast_moutdwm(ast, 0x1E6E2020, 0x03F1);
		param->wodt          = 1;
		param->reg_AC1       = 0x33302825;
		param->reg_AC2       = 0xCC009617 | trap_AC2;
		param->reg_DQSIC     = 0x000000E2;
		param->reg_MRS       = 0x04001600 | trap_MRS;
		param->reg_EMRS      = 0x00000000;
		param->reg_IOZ       = 0x00000034;
		param->reg_DRV       = 0x000000FA;
		param->reg_DQIDLY    = 0x00000089;
		param->reg_FREQ      = 0x00005040;
		param->madj_max      = 96;
		param->dll2_finetune_step = 4;

		switch (param->dram_chipid) {
		default:
		case AST_DRAM_512Mx16:
		case AST_DRAM_1Gx16:
			param->reg_AC2   = 0xCC009617 | trap_AC2;
			break;
		case AST_DRAM_2Gx16:
			param->reg_AC2   = 0xCC009622 | trap_AC2;
			break;
		case AST_DRAM_4Gx16:
			param->reg_AC2   = 0xCC00963F | trap_AC2;
			break;
		}
		break;

	case 408:
		ast_moutdwm(ast, 0x1E6E2020, 0x01F0);
		param->wodt          = 1;
		param->reg_AC1       = 0x33302825;
		param->reg_AC2       = 0xCC009617 | trap_AC2;
		param->reg_DQSIC     = 0x000000E2;
		param->reg_MRS       = 0x04001600 | trap_MRS;
		param->reg_EMRS      = 0x00000000;
		param->reg_IOZ       = 0x00000023;
		param->reg_DRV       = 0x000000FA;
		param->reg_DQIDLY    = 0x00000089;
		param->reg_FREQ      = 0x000050C0;
		param->madj_max      = 96;
		param->dll2_finetune_step = 4;

		switch (param->dram_chipid) {
		default:
		case AST_DRAM_512Mx16:
		case AST_DRAM_1Gx16:
			param->reg_AC2   = 0xCC009617 | trap_AC2;
			break;
		case AST_DRAM_2Gx16:
			param->reg_AC2   = 0xCC009622 | trap_AC2;
			break;
		case AST_DRAM_4Gx16:
			param->reg_AC2   = 0xCC00963F | trap_AC2;
			break;
		}

		break;
	case 456:
		ast_moutdwm(ast, 0x1E6E2020, 0x0230);
		param->wodt          = 0;
		param->reg_AC1       = 0x33302926;
		param->reg_AC2       = 0xCD44961A;
		param->reg_DQSIC     = 0x000000FC;
		param->reg_MRS       = 0x00081830;
		param->reg_EMRS      = 0x00000000;
		param->reg_IOZ       = 0x00000045;
		param->reg_DQIDLY    = 0x00000097;
		param->reg_FREQ      = 0x000052C0;
		param->madj_max      = 88;
		param->dll2_finetune_step = 4;
		break;
	case 504:
		ast_moutdwm(ast, 0x1E6E2020, 0x0270);
		param->wodt          = 1;
		param->reg_AC1       = 0x33302926;
		param->reg_AC2       = 0xDE44A61D;
		param->reg_DQSIC     = 0x00000117;
		param->reg_MRS       = 0x00081A30;
		param->reg_EMRS      = 0x00000000;
		param->reg_IOZ       = 0x070000BB;
		param->reg_DQIDLY    = 0x000000A0;
		param->reg_FREQ      = 0x000054C0;
		param->madj_max      = 79;
		param->dll2_finetune_step = 4;
		break;
	case 528:
		ast_moutdwm(ast, 0x1E6E2020, 0x0290);
		param->wodt          = 1;
		param->rodt          = 1;
		param->reg_AC1       = 0x33302926;
		param->reg_AC2       = 0xEF44B61E;
		param->reg_DQSIC     = 0x00000125;
		param->reg_MRS       = 0x00081A30;
		param->reg_EMRS      = 0x00000040;
		param->reg_DRV       = 0x000000F5;
		param->reg_IOZ       = 0x00000023;
		param->reg_DQIDLY    = 0x00000088;
		param->reg_FREQ      = 0x000055C0;
		param->madj_max      = 76;
		param->dll2_finetune_step = 3;
		break;
	case 576:
		ast_moutdwm(ast, 0x1E6E2020, 0x0140);
		param->reg_MADJ      = 0x00136868;
		param->reg_SADJ      = 0x00004534;
		param->wodt          = 1;
		param->rodt          = 1;
		param->reg_AC1       = 0x33302A37;
		param->reg_AC2       = 0xEF56B61E;
		param->reg_DQSIC     = 0x0000013F;
		param->reg_MRS       = 0x00101A50;
		param->reg_EMRS      = 0x00000040;
		param->reg_DRV       = 0x000000FA;
		param->reg_IOZ       = 0x00000023;
		param->reg_DQIDLY    = 0x00000078;
		param->reg_FREQ      = 0x000057C0;
		param->madj_max      = 136;
		param->dll2_finetune_step = 3;
		break;
	case 600:
		ast_moutdwm(ast, 0x1E6E2020, 0x02E1);
		param->reg_MADJ      = 0x00136868;
		param->reg_SADJ      = 0x00004534;
		param->wodt          = 1;
		param->rodt          = 1;
		param->reg_AC1       = 0x32302A37;
		param->reg_AC2       = 0xDF56B61F;
		param->reg_DQSIC     = 0x0000014D;
		param->reg_MRS       = 0x00101A50;
		param->reg_EMRS      = 0x00000004;
		param->reg_DRV       = 0x000000F5;
		param->reg_IOZ       = 0x00000023;
		param->reg_DQIDLY    = 0x00000078;
		param->reg_FREQ      = 0x000058C0;
		param->madj_max      = 132;
		param->dll2_finetune_step = 3;
		break;
	case 624:
		ast_moutdwm(ast, 0x1E6E2020, 0x0160);
		param->reg_MADJ      = 0x00136868;
		param->reg_SADJ      = 0x00004534;
		param->wodt          = 1;
		param->rodt          = 1;
		param->reg_AC1       = 0x32302A37;
		param->reg_AC2       = 0xEF56B621;
		param->reg_DQSIC     = 0x0000015A;
		param->reg_MRS       = 0x02101A50;
		param->reg_EMRS      = 0x00000004;
		param->reg_DRV       = 0x000000F5;
		param->reg_IOZ       = 0x00000034;
		param->reg_DQIDLY    = 0x00000078;
		param->reg_FREQ      = 0x000059C0;
		param->madj_max      = 128;
		param->dll2_finetune_step = 3;
		break;
	} /* switch freq */

	switch (param->dram_chipid) {
	case AST_DRAM_512Mx16:
		param->dram_config = 0x130;
		break;
	default:
	case AST_DRAM_1Gx16:
		param->dram_config = 0x131;
		break;
	case AST_DRAM_2Gx16:
		param->dram_config = 0x132;
		break;
	case AST_DRAM_4Gx16:
		param->dram_config = 0x133;
		break;
	} /* switch size */

	switch (param->vram_size) {
	default:
	case SZ_8M:
		param->dram_config |= 0x00;
		break;
	case SZ_16M:
		param->dram_config |= 0x04;
		break;
	case SZ_32M:
		param->dram_config |= 0x08;
		break;
	case SZ_64M:
		param->dram_config |= 0x0c;
		break;
	}
}

static void ddr3_init(struct ast_device *ast, struct ast2300_dram_param *param)
{
	u32 data, data2, retry = 0;

ddr3_init_start:
	ast_moutdwm(ast, 0x1E6E0000, 0xFC600309);
	ast_moutdwm(ast, 0x1E6E0018, 0x00000100);
	ast_moutdwm(ast, 0x1E6E0024, 0x00000000);
	ast_moutdwm(ast, 0x1E6E0034, 0x00000000);
	udelay(10);
	ast_moutdwm(ast, 0x1E6E0064, param->reg_MADJ);
	ast_moutdwm(ast, 0x1E6E0068, param->reg_SADJ);
	udelay(10);
	ast_moutdwm(ast, 0x1E6E0064, param->reg_MADJ | 0xC0000);
	udelay(10);

	ast_moutdwm(ast, 0x1E6E0004, param->dram_config);
	ast_moutdwm(ast, 0x1E6E0008, 0x90040f);
	ast_moutdwm(ast, 0x1E6E0010, param->reg_AC1);
	ast_moutdwm(ast, 0x1E6E0014, param->reg_AC2);
	ast_moutdwm(ast, 0x1E6E0020, param->reg_DQSIC);
	ast_moutdwm(ast, 0x1E6E0080, 0x00000000);
	ast_moutdwm(ast, 0x1E6E0084, 0x00000000);
	ast_moutdwm(ast, 0x1E6E0088, param->reg_DQIDLY);
	ast_moutdwm(ast, 0x1E6E0018, 0x4000A170);
	ast_moutdwm(ast, 0x1E6E0018, 0x00002370);
	ast_moutdwm(ast, 0x1E6E0038, 0x00000000);
	ast_moutdwm(ast, 0x1E6E0040, 0xFF444444);
	ast_moutdwm(ast, 0x1E6E0044, 0x22222222);
	ast_moutdwm(ast, 0x1E6E0048, 0x22222222);
	ast_moutdwm(ast, 0x1E6E004C, 0x00000002);
	ast_moutdwm(ast, 0x1E6E0050, 0x80000000);
	ast_moutdwm(ast, 0x1E6E0050, 0x00000000);
	ast_moutdwm(ast, 0x1E6E0054, 0);
	ast_moutdwm(ast, 0x1E6E0060, param->reg_DRV);
	ast_moutdwm(ast, 0x1E6E006C, param->reg_IOZ);
	ast_moutdwm(ast, 0x1E6E0070, 0x00000000);
	ast_moutdwm(ast, 0x1E6E0074, 0x00000000);
	ast_moutdwm(ast, 0x1E6E0078, 0x00000000);
	ast_moutdwm(ast, 0x1E6E007C, 0x00000000);
	/* Wait MCLK2X lock to MCLK */
	do {
		data = ast_mindwm(ast, 0x1E6E001C);
	} while (!(data & 0x08000000));
	data = ast_mindwm(ast, 0x1E6E001C);
	data = (data >> 8) & 0xff;
	while ((data & 0x08) || ((data & 0x7) < 2) || (data < 4)) {
		data2 = (ast_mindwm(ast, 0x1E6E0064) & 0xfff3ffff) + 4;
		if ((data2 & 0xff) > param->madj_max)
			break;
		ast_moutdwm(ast, 0x1E6E0064, data2);
		if (data2 & 0x00100000)
			data2 = ((data2 & 0xff) >> 3) + 3;
		else
			data2 = ((data2 & 0xff) >> 2) + 5;
		data = ast_mindwm(ast, 0x1E6E0068) & 0xffff00ff;
		data2 += data & 0xff;
		data = data | (data2 << 8);
		ast_moutdwm(ast, 0x1E6E0068, data);
		udelay(10);
		ast_moutdwm(ast, 0x1E6E0064, ast_mindwm(ast, 0x1E6E0064) | 0xC0000);
		udelay(10);
		data = ast_mindwm(ast, 0x1E6E0018) & 0xfffff1ff;
		ast_moutdwm(ast, 0x1E6E0018, data);
		data = data | 0x200;
		ast_moutdwm(ast, 0x1E6E0018, data);
		do {
			data = ast_mindwm(ast, 0x1E6E001C);
		} while (!(data & 0x08000000));

		data = ast_mindwm(ast, 0x1E6E001C);
		data = (data >> 8) & 0xff;
	}
	ast_moutdwm(ast, 0x1E720058, ast_mindwm(ast, 0x1E6E0068) & 0xffff);
	data = ast_mindwm(ast, 0x1E6E0018) | 0xC00;
	ast_moutdwm(ast, 0x1E6E0018, data);

	ast_moutdwm(ast, 0x1E6E0034, 0x00000001);
	ast_moutdwm(ast, 0x1E6E000C, 0x00000040);
	udelay(50);
	/* Mode Register Setting */
	ast_moutdwm(ast, 0x1E6E002C, param->reg_MRS | 0x100);
	ast_moutdwm(ast, 0x1E6E0030, param->reg_EMRS);
	ast_moutdwm(ast, 0x1E6E0028, 0x00000005);
	ast_moutdwm(ast, 0x1E6E0028, 0x00000007);
	ast_moutdwm(ast, 0x1E6E0028, 0x00000003);
	ast_moutdwm(ast, 0x1E6E0028, 0x00000001);
	ast_moutdwm(ast, 0x1E6E002C, param->reg_MRS);
	ast_moutdwm(ast, 0x1E6E000C, 0x00005C08);
	ast_moutdwm(ast, 0x1E6E0028, 0x00000001);

	ast_moutdwm(ast, 0x1E6E000C, 0x00005C01);
	data = 0;
	if (param->wodt)
		data = 0x300;
	if (param->rodt)
		data = data | 0x3000 | ((param->reg_AC2 & 0x60000) >> 3);
	ast_moutdwm(ast, 0x1E6E0034, data | 0x3);

	/* Calibrate the DQSI delay */
	if ((cbr_dll2(ast, param) == false) && (retry++ < 10))
		goto ddr3_init_start;

	ast_moutdwm(ast, 0x1E6E0120, param->reg_FREQ);
	/* ECC Memory Initialization */
#ifdef ECC
	ast_moutdwm(ast, 0x1E6E007C, 0x00000000);
	ast_moutdwm(ast, 0x1E6E0070, 0x221);
	do {
		data = ast_mindwm(ast, 0x1E6E0070);
	} while (!(data & 0x00001000));
	ast_moutdwm(ast, 0x1E6E0070, 0x00000000);
	ast_moutdwm(ast, 0x1E6E0050, 0x80000000);
	ast_moutdwm(ast, 0x1E6E0050, 0x00000000);
#endif
}

static void get_ddr2_info(struct ast_device *ast, struct ast2300_dram_param *param)
{
	u32 trap, trap_AC2, trap_MRS;

	ast_moutdwm(ast, 0x1E6E2000, 0x1688A8A8);

	/* Ger trap info */
	trap = (ast_mindwm(ast, 0x1E6E2070) >> 25) & 0x3;
	trap_AC2  = (trap << 20) | (trap << 16);
	trap_AC2 += 0x00110000;
	trap_MRS  = 0x00000040 | (trap << 4);

	param->reg_MADJ       = 0x00034C4C;
	param->reg_SADJ       = 0x00001800;
	param->reg_DRV        = 0x000000F0;
	param->reg_PERIOD     = param->dram_freq;
	param->rodt           = 0;

	switch (param->dram_freq) {
	case 264:
		ast_moutdwm(ast, 0x1E6E2020, 0x0130);
		param->wodt          = 0;
		param->reg_AC1       = 0x11101513;
		param->reg_AC2       = 0x78117011;
		param->reg_DQSIC     = 0x00000092;
		param->reg_MRS       = 0x00000842;
		param->reg_EMRS      = 0x00000000;
		param->reg_DRV       = 0x000000F0;
		param->reg_IOZ       = 0x00000034;
		param->reg_DQIDLY    = 0x0000005A;
		param->reg_FREQ      = 0x00004AC0;
		param->madj_max      = 138;
		param->dll2_finetune_step = 3;
		break;
	case 336:
		ast_moutdwm(ast, 0x1E6E2020, 0x0190);
		param->wodt          = 1;
		param->reg_AC1       = 0x22202613;
		param->reg_AC2       = 0xAA009016 | trap_AC2;
		param->reg_DQSIC     = 0x000000BA;
		param->reg_MRS       = 0x00000A02 | trap_MRS;
		param->reg_EMRS      = 0x00000040;
		param->reg_DRV       = 0x000000FA;
		param->reg_IOZ       = 0x00000034;
		param->reg_DQIDLY    = 0x00000074;
		param->reg_FREQ      = 0x00004DC0;
		param->madj_max      = 96;
		param->dll2_finetune_step = 3;
		switch (param->dram_chipid) {
		default:
		case AST_DRAM_512Mx16:
			param->reg_AC2   = 0xAA009012 | trap_AC2;
			break;
		case AST_DRAM_1Gx16:
			param->reg_AC2   = 0xAA009016 | trap_AC2;
			break;
		case AST_DRAM_2Gx16:
			param->reg_AC2   = 0xAA009023 | trap_AC2;
			break;
		case AST_DRAM_4Gx16:
			param->reg_AC2   = 0xAA00903B | trap_AC2;
			break;
		}
		break;
	default:
	case 396:
		ast_moutdwm(ast, 0x1E6E2020, 0x03F1);
		param->wodt          = 1;
		param->rodt          = 0;
		param->reg_AC1       = 0x33302714;
		param->reg_AC2       = 0xCC00B01B | trap_AC2;
		param->reg_DQSIC     = 0x000000E2;
		param->reg_MRS       = 0x00000C02 | trap_MRS;
		param->reg_EMRS      = 0x00000040;
		param->reg_DRV       = 0x000000FA;
		param->reg_IOZ       = 0x00000034;
		param->reg_DQIDLY    = 0x00000089;
		param->reg_FREQ      = 0x00005040;
		param->madj_max      = 96;
		param->dll2_finetune_step = 4;

		switch (param->dram_chipid) {
		case AST_DRAM_512Mx16:
			param->reg_AC2   = 0xCC00B016 | trap_AC2;
			break;
		default:
		case AST_DRAM_1Gx16:
			param->reg_AC2   = 0xCC00B01B | trap_AC2;
			break;
		case AST_DRAM_2Gx16:
			param->reg_AC2   = 0xCC00B02B | trap_AC2;
			break;
		case AST_DRAM_4Gx16:
			param->reg_AC2   = 0xCC00B03F | trap_AC2;
			break;
		}

		break;

	case 408:
		ast_moutdwm(ast, 0x1E6E2020, 0x01F0);
		param->wodt          = 1;
		param->rodt          = 0;
		param->reg_AC1       = 0x33302714;
		param->reg_AC2       = 0xCC00B01B | trap_AC2;
		param->reg_DQSIC     = 0x000000E2;
		param->reg_MRS       = 0x00000C02 | trap_MRS;
		param->reg_EMRS      = 0x00000040;
		param->reg_DRV       = 0x000000FA;
		param->reg_IOZ       = 0x00000034;
		param->reg_DQIDLY    = 0x00000089;
		param->reg_FREQ      = 0x000050C0;
		param->madj_max      = 96;
		param->dll2_finetune_step = 4;

		switch (param->dram_chipid) {
		case AST_DRAM_512Mx16:
			param->reg_AC2   = 0xCC00B016 | trap_AC2;
			break;
		default:
		case AST_DRAM_1Gx16:
			param->reg_AC2   = 0xCC00B01B | trap_AC2;
			break;
		case AST_DRAM_2Gx16:
			param->reg_AC2   = 0xCC00B02B | trap_AC2;
			break;
		case AST_DRAM_4Gx16:
			param->reg_AC2   = 0xCC00B03F | trap_AC2;
			break;
		}

		break;
	case 456:
		ast_moutdwm(ast, 0x1E6E2020, 0x0230);
		param->wodt          = 0;
		param->reg_AC1       = 0x33302815;
		param->reg_AC2       = 0xCD44B01E;
		param->reg_DQSIC     = 0x000000FC;
		param->reg_MRS       = 0x00000E72;
		param->reg_EMRS      = 0x00000000;
		param->reg_DRV       = 0x00000000;
		param->reg_IOZ       = 0x00000034;
		param->reg_DQIDLY    = 0x00000097;
		param->reg_FREQ      = 0x000052C0;
		param->madj_max      = 88;
		param->dll2_finetune_step = 3;
		break;
	case 504:
		ast_moutdwm(ast, 0x1E6E2020, 0x0261);
		param->wodt          = 1;
		param->rodt          = 1;
		param->reg_AC1       = 0x33302815;
		param->reg_AC2       = 0xDE44C022;
		param->reg_DQSIC     = 0x00000117;
		param->reg_MRS       = 0x00000E72;
		param->reg_EMRS      = 0x00000040;
		param->reg_DRV       = 0x0000000A;
		param->reg_IOZ       = 0x00000045;
		param->reg_DQIDLY    = 0x000000A0;
		param->reg_FREQ      = 0x000054C0;
		param->madj_max      = 79;
		param->dll2_finetune_step = 3;
		break;
	case 528:
		ast_moutdwm(ast, 0x1E6E2020, 0x0120);
		param->wodt          = 1;
		param->rodt          = 1;
		param->reg_AC1       = 0x33302815;
		param->reg_AC2       = 0xEF44D024;
		param->reg_DQSIC     = 0x00000125;
		param->reg_MRS       = 0x00000E72;
		param->reg_EMRS      = 0x00000004;
		param->reg_DRV       = 0x000000F9;
		param->reg_IOZ       = 0x00000045;
		param->reg_DQIDLY    = 0x000000A7;
		param->reg_FREQ      = 0x000055C0;
		param->madj_max      = 76;
		param->dll2_finetune_step = 3;
		break;
	case 552:
		ast_moutdwm(ast, 0x1E6E2020, 0x02A1);
		param->wodt          = 1;
		param->rodt          = 1;
		param->reg_AC1       = 0x43402915;
		param->reg_AC2       = 0xFF44E025;
		param->reg_DQSIC     = 0x00000132;
		param->reg_MRS       = 0x00000E72;
		param->reg_EMRS      = 0x00000040;
		param->reg_DRV       = 0x0000000A;
		param->reg_IOZ       = 0x00000045;
		param->reg_DQIDLY    = 0x000000AD;
		param->reg_FREQ      = 0x000056C0;
		param->madj_max      = 76;
		param->dll2_finetune_step = 3;
		break;
	case 576:
		ast_moutdwm(ast, 0x1E6E2020, 0x0140);
		param->wodt          = 1;
		param->rodt          = 1;
		param->reg_AC1       = 0x43402915;
		param->reg_AC2       = 0xFF44E027;
		param->reg_DQSIC     = 0x0000013F;
		param->reg_MRS       = 0x00000E72;
		param->reg_EMRS      = 0x00000004;
		param->reg_DRV       = 0x000000F5;
		param->reg_IOZ       = 0x00000045;
		param->reg_DQIDLY    = 0x000000B3;
		param->reg_FREQ      = 0x000057C0;
		param->madj_max      = 76;
		param->dll2_finetune_step = 3;
		break;
	}

	switch (param->dram_chipid) {
	case AST_DRAM_512Mx16:
		param->dram_config = 0x100;
		break;
	default:
	case AST_DRAM_1Gx16:
		param->dram_config = 0x121;
		break;
	case AST_DRAM_2Gx16:
		param->dram_config = 0x122;
		break;
	case AST_DRAM_4Gx16:
		param->dram_config = 0x123;
		break;
	} /* switch size */

	switch (param->vram_size) {
	default:
	case SZ_8M:
		param->dram_config |= 0x00;
		break;
	case SZ_16M:
		param->dram_config |= 0x04;
		break;
	case SZ_32M:
		param->dram_config |= 0x08;
		break;
	case SZ_64M:
		param->dram_config |= 0x0c;
		break;
	}
}

static void ddr2_init(struct ast_device *ast, struct ast2300_dram_param *param)
{
	u32 data, data2, retry = 0;

ddr2_init_start:
	ast_moutdwm(ast, 0x1E6E0000, 0xFC600309);
	ast_moutdwm(ast, 0x1E6E0018, 0x00000100);
	ast_moutdwm(ast, 0x1E6E0024, 0x00000000);
	ast_moutdwm(ast, 0x1E6E0064, param->reg_MADJ);
	ast_moutdwm(ast, 0x1E6E0068, param->reg_SADJ);
	udelay(10);
	ast_moutdwm(ast, 0x1E6E0064, param->reg_MADJ | 0xC0000);
	udelay(10);

	ast_moutdwm(ast, 0x1E6E0004, param->dram_config);
	ast_moutdwm(ast, 0x1E6E0008, 0x90040f);
	ast_moutdwm(ast, 0x1E6E0010, param->reg_AC1);
	ast_moutdwm(ast, 0x1E6E0014, param->reg_AC2);
	ast_moutdwm(ast, 0x1E6E0020, param->reg_DQSIC);
	ast_moutdwm(ast, 0x1E6E0080, 0x00000000);
	ast_moutdwm(ast, 0x1E6E0084, 0x00000000);
	ast_moutdwm(ast, 0x1E6E0088, param->reg_DQIDLY);
	ast_moutdwm(ast, 0x1E6E0018, 0x4000A130);
	ast_moutdwm(ast, 0x1E6E0018, 0x00002330);
	ast_moutdwm(ast, 0x1E6E0038, 0x00000000);
	ast_moutdwm(ast, 0x1E6E0040, 0xFF808000);
	ast_moutdwm(ast, 0x1E6E0044, 0x88848466);
	ast_moutdwm(ast, 0x1E6E0048, 0x44440008);
	ast_moutdwm(ast, 0x1E6E004C, 0x00000000);
	ast_moutdwm(ast, 0x1E6E0050, 0x80000000);
	ast_moutdwm(ast, 0x1E6E0050, 0x00000000);
	ast_moutdwm(ast, 0x1E6E0054, 0);
	ast_moutdwm(ast, 0x1E6E0060, param->reg_DRV);
	ast_moutdwm(ast, 0x1E6E006C, param->reg_IOZ);
	ast_moutdwm(ast, 0x1E6E0070, 0x00000000);
	ast_moutdwm(ast, 0x1E6E0074, 0x00000000);
	ast_moutdwm(ast, 0x1E6E0078, 0x00000000);
	ast_moutdwm(ast, 0x1E6E007C, 0x00000000);

	/* Wait MCLK2X lock to MCLK */
	do {
		data = ast_mindwm(ast, 0x1E6E001C);
	} while (!(data & 0x08000000));
	data = ast_mindwm(ast, 0x1E6E001C);
	data = (data >> 8) & 0xff;
	while ((data & 0x08) || ((data & 0x7) < 2) || (data < 4)) {
		data2 = (ast_mindwm(ast, 0x1E6E0064) & 0xfff3ffff) + 4;
		if ((data2 & 0xff) > param->madj_max)
			break;
		ast_moutdwm(ast, 0x1E6E0064, data2);
		if (data2 & 0x00100000)
			data2 = ((data2 & 0xff) >> 3) + 3;
		else
			data2 = ((data2 & 0xff) >> 2) + 5;
		data = ast_mindwm(ast, 0x1E6E0068) & 0xffff00ff;
		data2 += data & 0xff;
		data = data | (data2 << 8);
		ast_moutdwm(ast, 0x1E6E0068, data);
		udelay(10);
		ast_moutdwm(ast, 0x1E6E0064, ast_mindwm(ast, 0x1E6E0064) | 0xC0000);
		udelay(10);
		data = ast_mindwm(ast, 0x1E6E0018) & 0xfffff1ff;
		ast_moutdwm(ast, 0x1E6E0018, data);
		data = data | 0x200;
		ast_moutdwm(ast, 0x1E6E0018, data);
		do {
			data = ast_mindwm(ast, 0x1E6E001C);
		} while (!(data & 0x08000000));

		data = ast_mindwm(ast, 0x1E6E001C);
		data = (data >> 8) & 0xff;
	}
	ast_moutdwm(ast, 0x1E720058, ast_mindwm(ast, 0x1E6E0008) & 0xffff);
	data = ast_mindwm(ast, 0x1E6E0018) | 0xC00;
	ast_moutdwm(ast, 0x1E6E0018, data);

	ast_moutdwm(ast, 0x1E6E0034, 0x00000001);
	ast_moutdwm(ast, 0x1E6E000C, 0x00000000);
	udelay(50);
	/* Mode Register Setting */
	ast_moutdwm(ast, 0x1E6E002C, param->reg_MRS | 0x100);
	ast_moutdwm(ast, 0x1E6E0030, param->reg_EMRS);
	ast_moutdwm(ast, 0x1E6E0028, 0x00000005);
	ast_moutdwm(ast, 0x1E6E0028, 0x00000007);
	ast_moutdwm(ast, 0x1E6E0028, 0x00000003);
	ast_moutdwm(ast, 0x1E6E0028, 0x00000001);

	ast_moutdwm(ast, 0x1E6E000C, 0x00005C08);
	ast_moutdwm(ast, 0x1E6E002C, param->reg_MRS);
	ast_moutdwm(ast, 0x1E6E0028, 0x00000001);
	ast_moutdwm(ast, 0x1E6E0030, param->reg_EMRS | 0x380);
	ast_moutdwm(ast, 0x1E6E0028, 0x00000003);
	ast_moutdwm(ast, 0x1E6E0030, param->reg_EMRS);
	ast_moutdwm(ast, 0x1E6E0028, 0x00000003);

	ast_moutdwm(ast, 0x1E6E000C, 0x7FFF5C01);
	data = 0;
	if (param->wodt)
		data = 0x500;
	if (param->rodt)
		data = data | 0x3000 | ((param->reg_AC2 & 0x60000) >> 3);
	ast_moutdwm(ast, 0x1E6E0034, data | 0x3);
	ast_moutdwm(ast, 0x1E6E0120, param->reg_FREQ);

	/* Calibrate the DQSI delay */
	if ((cbr_dll2(ast, param) == false) && (retry++ < 10))
		goto ddr2_init_start;

	/* ECC Memory Initialization */
#ifdef ECC
	ast_moutdwm(ast, 0x1E6E007C, 0x00000000);
	ast_moutdwm(ast, 0x1E6E0070, 0x221);
	do {
		data = ast_mindwm(ast, 0x1E6E0070);
	} while (!(data & 0x00001000));
	ast_moutdwm(ast, 0x1E6E0070, 0x00000000);
	ast_moutdwm(ast, 0x1E6E0050, 0x80000000);
	ast_moutdwm(ast, 0x1E6E0050, 0x00000000);
#endif
}

static void ast_post_chip_2300(struct ast_device *ast)
{
	struct ast2300_dram_param param;
	u32 temp;
	u8 reg;

	reg = ast_get_index_reg_mask(ast, AST_IO_VGACRI, 0xd0, 0xff);
	if ((reg & 0x80) == 0) {/* vga only */
		ast_write32(ast, 0xf004, 0x1e6e0000);
		ast_write32(ast, 0xf000, 0x1);
		ast_write32(ast, 0x12000, 0x1688a8a8);
		do {
			;
		} while (ast_read32(ast, 0x12000) != 0x1);

		ast_write32(ast, 0x10000, 0xfc600309);
		do {
			;
		} while (ast_read32(ast, 0x10000) != 0x1);

		/* Slow down CPU/AHB CLK in VGA only mode */
		temp = ast_read32(ast, 0x12008);
		temp |= 0x73;
		ast_write32(ast, 0x12008, temp);

		param.dram_freq = 396;
		param.dram_type = AST_DDR3;
		temp = ast_mindwm(ast, 0x1e6e2070);
		if (temp & 0x01000000)
			param.dram_type = AST_DDR2;
		switch (temp & 0x18000000) {
		case 0:
			param.dram_chipid = AST_DRAM_512Mx16;
			break;
		default:
		case 0x08000000:
			param.dram_chipid = AST_DRAM_1Gx16;
			break;
		case 0x10000000:
			param.dram_chipid = AST_DRAM_2Gx16;
			break;
		case 0x18000000:
			param.dram_chipid = AST_DRAM_4Gx16;
			break;
		}
		switch (temp & 0x0c) {
		default:
		case 0x00:
			param.vram_size = SZ_8M;
			break;
		case 0x04:
			param.vram_size = SZ_16M;
			break;
		case 0x08:
			param.vram_size = SZ_32M;
			break;
		case 0x0c:
			param.vram_size = SZ_64M;
			break;
		}

		if (param.dram_type == AST_DDR3) {
			get_ddr3_info(ast, &param);
			ddr3_init(ast, &param);
		} else {
			get_ddr2_info(ast, &param);
			ddr2_init(ast, &param);
		}

		temp = ast_mindwm(ast, 0x1e6e2040);
		ast_moutdwm(ast, 0x1e6e2040, temp | 0x40);
	}

	/* wait ready */
	do {
		reg = ast_get_index_reg_mask(ast, AST_IO_VGACRI, 0xd0, 0xff);
	} while ((reg & 0x40) == 0);
}

int ast_2300_post(struct ast_device *ast)
{
	ast_2300_set_def_ext_reg(ast);

	if (ast->config_mode == ast_use_p2a) {
		ast_post_chip_2300(ast);
		ast_init_3rdtx(ast);
	} else {
		if (ast->tx_chip == AST_TX_SIL164) {
			/* Enable DVO */
			ast_set_index_reg_mask(ast, AST_IO_VGACRI, 0xa3, 0xcf, 0x80);
		}
	}

	return 0;
}
