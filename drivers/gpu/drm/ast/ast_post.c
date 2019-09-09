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
 *
 */
/*
 * Authors: Dave Airlie <airlied@redhat.com>
 */

#include <drm/drmP.h>
#include "ast_drv.h"

#include "ast_dram_tables.h"

static void ast_post_chip_2300(struct drm_device *dev);
static void ast_post_chip_2500(struct drm_device *dev);

void ast_enable_vga(struct drm_device *dev)
{
	struct ast_private *ast = dev->dev_private;

	ast_io_write8(ast, AST_IO_VGA_ENABLE_PORT, 0x01);
	ast_io_write8(ast, AST_IO_MISC_PORT_WRITE, 0x01);
}

void ast_enable_mmio(struct drm_device *dev)
{
	struct ast_private *ast = dev->dev_private;

	ast_set_index_reg(ast, AST_IO_CRTC_PORT, 0xa1, 0x06);
}


bool ast_is_vga_enabled(struct drm_device *dev)
{
	struct ast_private *ast = dev->dev_private;
	u8 ch;

	if (ast->chip == AST1180) {
		/* TODO 1180 */
	} else {
		ch = ast_io_read8(ast, AST_IO_VGA_ENABLE_PORT);
		return !!(ch & 0x01);
	}
	return false;
}

static const u8 extreginfo[] = { 0x0f, 0x04, 0x1c, 0xff };
static const u8 extreginfo_ast2300a0[] = { 0x0f, 0x04, 0x1c, 0xff };
static const u8 extreginfo_ast2300[] = { 0x0f, 0x04, 0x1f, 0xff };

static void
ast_set_def_ext_reg(struct drm_device *dev)
{
	struct ast_private *ast = dev->dev_private;
	u8 i, index, reg;
	const u8 *ext_reg_info;

	/* reset scratch */
	for (i = 0x81; i <= 0x9f; i++)
		ast_set_index_reg(ast, AST_IO_CRTC_PORT, i, 0x00);

	if (ast->chip == AST2300 || ast->chip == AST2400 ||
	    ast->chip == AST2500) {
		if (dev->pdev->revision >= 0x20)
			ext_reg_info = extreginfo_ast2300;
		else
			ext_reg_info = extreginfo_ast2300a0;
	} else
		ext_reg_info = extreginfo;

	index = 0xa0;
	while (*ext_reg_info != 0xff) {
		ast_set_index_reg_mask(ast, AST_IO_CRTC_PORT, index, 0x00, *ext_reg_info);
		index++;
		ext_reg_info++;
	}

	/* disable standard IO/MEM decode if secondary */
	/* ast_set_index_reg-mask(ast, AST_IO_CRTC_PORT, 0xa1, 0xff, 0x3); */

	/* Set Ext. Default */
	ast_set_index_reg_mask(ast, AST_IO_CRTC_PORT, 0x8c, 0x00, 0x01);
	ast_set_index_reg_mask(ast, AST_IO_CRTC_PORT, 0xb7, 0x00, 0x00);

	/* Enable RAMDAC for A1 */
	reg = 0x04;
	if (ast->chip == AST2300 || ast->chip == AST2400 ||
	    ast->chip == AST2500)
		reg |= 0x20;
	ast_set_index_reg_mask(ast, AST_IO_CRTC_PORT, 0xb6, 0xff, reg);
}

u32 ast_mindwm(struct ast_private *ast, u32 r)
{
	uint32_t data;

	ast_write32(ast, 0xf004, r & 0xffff0000);
	ast_write32(ast, 0xf000, 0x1);

	do {
		data = ast_read32(ast, 0xf004) & 0xffff0000;
	} while (data != (r & 0xffff0000));
	return ast_read32(ast, 0x10000 + (r & 0x0000ffff));
}

void ast_moutdwm(struct ast_private *ast, u32 r, u32 v)
{
	uint32_t data;
	ast_write32(ast, 0xf004, r & 0xffff0000);
	ast_write32(ast, 0xf000, 0x1);
	do {
		data = ast_read32(ast, 0xf004) & 0xffff0000;
	} while (data != (r & 0xffff0000));
	ast_write32(ast, 0x10000 + (r & 0x0000ffff), v);
}

/*
 * AST2100/2150 DLL CBR Setting
 */
#define CBR_SIZE_AST2150	     ((16 << 10) - 1)
#define CBR_PASSNUM_AST2150          5
#define CBR_THRESHOLD_AST2150        10
#define CBR_THRESHOLD2_AST2150       10
#define TIMEOUT_AST2150              5000000

#define CBR_PATNUM_AST2150           8

static const u32 pattern_AST2150[14] = {
	0xFF00FF00,
	0xCC33CC33,
	0xAA55AA55,
	0xFFFE0001,
	0x683501FE,
	0x0F1929B0,
	0x2D0B4346,
	0x60767F02,
	0x6FBE36A6,
	0x3A253035,
	0x3019686D,
	0x41C6167E,
	0x620152BF,
	0x20F050E0
};

static u32 mmctestburst2_ast2150(struct ast_private *ast, u32 datagen)
{
	u32 data, timeout;

	ast_moutdwm(ast, 0x1e6e0070, 0x00000000);
	ast_moutdwm(ast, 0x1e6e0070, 0x00000001 | (datagen << 3));
	timeout = 0;
	do {
		data = ast_mindwm(ast, 0x1e6e0070) & 0x40;
		if (++timeout > TIMEOUT_AST2150) {
			ast_moutdwm(ast, 0x1e6e0070, 0x00000000);
			return 0xffffffff;
		}
	} while (!data);
	ast_moutdwm(ast, 0x1e6e0070, 0x00000000);
	ast_moutdwm(ast, 0x1e6e0070, 0x00000003 | (datagen << 3));
	timeout = 0;
	do {
		data = ast_mindwm(ast, 0x1e6e0070) & 0x40;
		if (++timeout > TIMEOUT_AST2150) {
			ast_moutdwm(ast, 0x1e6e0070, 0x00000000);
			return 0xffffffff;
		}
	} while (!data);
	data = (ast_mindwm(ast, 0x1e6e0070) & 0x80) >> 7;
	ast_moutdwm(ast, 0x1e6e0070, 0x00000000);
	return data;
}

#if 0 /* unused in DDX driver - here for completeness */
static u32 mmctestsingle2_ast2150(struct ast_private *ast, u32 datagen)
{
	u32 data, timeout;

	ast_moutdwm(ast, 0x1e6e0070, 0x00000000);
	ast_moutdwm(ast, 0x1e6e0070, 0x00000005 | (datagen << 3));
	timeout = 0;
	do {
		data = ast_mindwm(ast, 0x1e6e0070) & 0x40;
		if (++timeout > TIMEOUT_AST2150) {
			ast_moutdwm(ast, 0x1e6e0070, 0x00000000);
			return 0xffffffff;
		}
	} while (!data);
	data = (ast_mindwm(ast, 0x1e6e0070) & 0x80) >> 7;
	ast_moutdwm(ast, 0x1e6e0070, 0x00000000);
	return data;
}
#endif

static int cbrtest_ast2150(struct ast_private *ast)
{
	int i;

	for (i = 0; i < 8; i++)
		if (mmctestburst2_ast2150(ast, i))
			return 0;
	return 1;
}

static int cbrscan_ast2150(struct ast_private *ast, int busw)
{
	u32 patcnt, loop;

	for (patcnt = 0; patcnt < CBR_PATNUM_AST2150; patcnt++) {
		ast_moutdwm(ast, 0x1e6e007c, pattern_AST2150[patcnt]);
		for (loop = 0; loop < CBR_PASSNUM_AST2150; loop++) {
			if (cbrtest_ast2150(ast))
				break;
		}
		if (loop == CBR_PASSNUM_AST2150)
			return 0;
	}
	return 1;
}


static void cbrdlli_ast2150(struct ast_private *ast, int busw)
{
	u32 dll_min[4], dll_max[4], dlli, data, passcnt;

cbr_start:
	dll_min[0] = dll_min[1] = dll_min[2] = dll_min[3] = 0xff;
	dll_max[0] = dll_max[1] = dll_max[2] = dll_max[3] = 0x0;
	passcnt = 0;

	for (dlli = 0; dlli < 100; dlli++) {
		ast_moutdwm(ast, 0x1e6e0068, dlli | (dlli << 8) | (dlli << 16) | (dlli << 24));
		data = cbrscan_ast2150(ast, busw);
		if (data != 0) {
			if (data & 0x1) {
				if (dll_min[0] > dlli)
					dll_min[0] = dlli;
				if (dll_max[0] < dlli)
					dll_max[0] = dlli;
			}
			passcnt++;
		} else if (passcnt >= CBR_THRESHOLD_AST2150)
			goto cbr_start;
	}
	if (dll_max[0] == 0 || (dll_max[0]-dll_min[0]) < CBR_THRESHOLD_AST2150)
		goto cbr_start;

	dlli = dll_min[0] + (((dll_max[0] - dll_min[0]) * 7) >> 4);
	ast_moutdwm(ast, 0x1e6e0068, dlli | (dlli << 8) | (dlli << 16) | (dlli << 24));
}



static void ast_init_dram_reg(struct drm_device *dev)
{
	struct ast_private *ast = dev->dev_private;
	u8 j;
	u32 data, temp, i;
	const struct ast_dramstruct *dram_reg_info;

	j = ast_get_index_reg_mask(ast, AST_IO_CRTC_PORT, 0xd0, 0xff);

	if ((j & 0x80) == 0) { /* VGA only */
		if (ast->chip == AST2000) {
			dram_reg_info = ast2000_dram_table_data;
			ast_write32(ast, 0xf004, 0x1e6e0000);
			ast_write32(ast, 0xf000, 0x1);
			ast_write32(ast, 0x10100, 0xa8);

			do {
				;
			} while (ast_read32(ast, 0x10100) != 0xa8);
		} else {/* AST2100/1100 */
			if (ast->chip == AST2100 || ast->chip == 2200)
				dram_reg_info = ast2100_dram_table_data;
			else
				dram_reg_info = ast1100_dram_table_data;

			ast_write32(ast, 0xf004, 0x1e6e0000);
			ast_write32(ast, 0xf000, 0x1);
			ast_write32(ast, 0x12000, 0x1688A8A8);
			do {
				;
			} while (ast_read32(ast, 0x12000) != 0x01);

			ast_write32(ast, 0x10000, 0xfc600309);
			do {
				;
			} while (ast_read32(ast, 0x10000) != 0x01);
		}

		while (dram_reg_info->index != 0xffff) {
			if (dram_reg_info->index == 0xff00) {/* delay fn */
				for (i = 0; i < 15; i++)
					udelay(dram_reg_info->data);
			} else if (dram_reg_info->index == 0x4 && ast->chip != AST2000) {
				data = dram_reg_info->data;
				if (ast->dram_type == AST_DRAM_1Gx16)
					data = 0x00000d89;
				else if (ast->dram_type == AST_DRAM_1Gx32)
					data = 0x00000c8d;

				temp = ast_read32(ast, 0x12070);
				temp &= 0xc;
				temp <<= 2;
				ast_write32(ast, 0x10000 + dram_reg_info->index, data | temp);
			} else
				ast_write32(ast, 0x10000 + dram_reg_info->index, dram_reg_info->data);
			dram_reg_info++;
		}

		/* AST 2100/2150 DRAM calibration */
		data = ast_read32(ast, 0x10120);
		if (data == 0x5061) { /* 266Mhz */
			data = ast_read32(ast, 0x10004);
			if (data & 0x40)
				cbrdlli_ast2150(ast, 16); /* 16 bits */
			else
				cbrdlli_ast2150(ast, 32); /* 32 bits */
		}

		switch (ast->chip) {
		case AST2000:
			temp = ast_read32(ast, 0x10140);
			ast_write32(ast, 0x10140, temp | 0x40);
			break;
		case AST1100:
		case AST2100:
		case AST2200:
		case AST2150:
			temp = ast_read32(ast, 0x1200c);
			ast_write32(ast, 0x1200c, temp & 0xfffffffd);
			temp = ast_read32(ast, 0x12040);
			ast_write32(ast, 0x12040, temp | 0x40);
			break;
		default:
			break;
		}
	}

	/* wait ready */
	do {
		j = ast_get_index_reg_mask(ast, AST_IO_CRTC_PORT, 0xd0, 0xff);
	} while ((j & 0x40) == 0);
}

void ast_post_gpu(struct drm_device *dev)
{
	u32 reg;
	struct ast_private *ast = dev->dev_private;

	pci_read_config_dword(ast->dev->pdev, 0x04, &reg);
	reg |= 0x3;
	pci_write_config_dword(ast->dev->pdev, 0x04, reg);

	ast_enable_vga(dev);
	ast_open_key(ast);
	ast_enable_mmio(dev);
	ast_set_def_ext_reg(dev);

	if (ast->config_mode == ast_use_p2a) {
		if (ast->chip == AST2500)
			ast_post_chip_2500(dev);
		else if (ast->chip == AST2300 || ast->chip == AST2400)
			ast_post_chip_2300(dev);
		else
			ast_init_dram_reg(dev);

		ast_init_3rdtx(dev);
	} else {
		if (ast->tx_chip_type != AST_TX_NONE)
			ast_set_index_reg_mask(ast, AST_IO_CRTC_PORT, 0xa3, 0xcf, 0x80);	/* Enable DVO */
	}
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

static bool mmc_test(struct ast_private *ast, u32 datagen, u8 test_ctl)
{
	u32 data, timeout;

	ast_moutdwm(ast, 0x1e6e0070, 0x00000000);
	ast_moutdwm(ast, 0x1e6e0070, (datagen << 3) | test_ctl);
	timeout = 0;
	do {
		data = ast_mindwm(ast, 0x1e6e0070) & 0x3000;
		if (data & 0x2000)
			return false;
		if (++timeout > TIMEOUT) {
			ast_moutdwm(ast, 0x1e6e0070, 0x00000000);
			return false;
		}
	} while (!data);
	ast_moutdwm(ast, 0x1e6e0070, 0x0);
	return true;
}

static u32 mmc_test2(struct ast_private *ast, u32 datagen, u8 test_ctl)
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


static bool mmc_test_burst(struct ast_private *ast, u32 datagen)
{
	return mmc_test(ast, datagen, 0xc1);
}

static u32 mmc_test_burst2(struct ast_private *ast, u32 datagen)
{
	return mmc_test2(ast, datagen, 0x41);
}

static bool mmc_test_single(struct ast_private *ast, u32 datagen)
{
	return mmc_test(ast, datagen, 0xc5);
}

static u32 mmc_test_single2(struct ast_private *ast, u32 datagen)
{
	return mmc_test2(ast, datagen, 0x05);
}

static bool mmc_test_single_2500(struct ast_private *ast, u32 datagen)
{
	return mmc_test(ast, datagen, 0x85);
}

static int cbr_test(struct ast_private *ast)
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

static int cbr_scan(struct ast_private *ast)
{
	u32 data, data2, patcnt, loop;

	data2 = 3;
	for (patcnt = 0; patcnt < CBR_PATNUM; patcnt++) {
		ast_moutdwm(ast, 0x1e6e007c, pattern[patcnt]);
		for (loop = 0; loop < CBR_PASSNUM2; loop++) {
			if ((data = cbr_test(ast)) != 0) {
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

static u32 cbr_test2(struct ast_private *ast)
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

static u32 cbr_scan2(struct ast_private *ast)
{
	u32 data, data2, patcnt, loop;

	data2 = 0xffff;
	for (patcnt = 0; patcnt < CBR_PATNUM; patcnt++) {
		ast_moutdwm(ast, 0x1e6e007c, pattern[patcnt]);
		for (loop = 0; loop < CBR_PASSNUM2; loop++) {
			if ((data = cbr_test2(ast)) != 0) {
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

static bool cbr_test3(struct ast_private *ast)
{
	if (!mmc_test_burst(ast, 0))
		return false;
	if (!mmc_test_single(ast, 0))
		return false;
	return true;
}

static bool cbr_scan3(struct ast_private *ast)
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

static bool finetuneDQI_L(struct ast_private *ast, struct ast2300_dram_param *param)
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
					if (dllmin[cnt] > dlli) {
						dllmin[cnt] = dlli;
					}
					if (dllmax[cnt] < dlli) {
						dllmax[cnt] = dlli;
					}
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
		if ((dllmax[cnt] > dllmin[cnt]) && ((dllmax[cnt] - dllmin[cnt]) >= CBR_THRESHOLD2)) {
			gold_sadj[0] += dllmin[cnt];
			passcnt++;
		}
	}
	if (retry++ > 10)
		goto FINETUNE_DONE;
	if (passcnt != 16) {
		goto FINETUNE_START;
	}
	status = true;
FINETUNE_DONE:
	gold_sadj[0] = gold_sadj[0] >> 4;
	gold_sadj[1] = gold_sadj[0];

	data = 0;
	for (cnt = 0; cnt < 8; cnt++) {
		data >>= 3;
		if ((dllmax[cnt] > dllmin[cnt]) && ((dllmax[cnt] - dllmin[cnt]) >= CBR_THRESHOLD2)) {
			dlli = dllmin[cnt];
			if (gold_sadj[0] >= dlli) {
				dlli = ((gold_sadj[0] - dlli) * 19) >> 5;
				if (dlli > 3) {
					dlli = 3;
				}
			} else {
				dlli = ((dlli - gold_sadj[0]) * 19) >> 5;
				if (dlli > 4) {
					dlli = 4;
				}
				dlli = (8 - dlli) & 0x7;
			}
			data |= dlli << 21;
		}
	}
	ast_moutdwm(ast, 0x1E6E0080, data);

	data = 0;
	for (cnt = 8; cnt < 16; cnt++) {
		data >>= 3;
		if ((dllmax[cnt] > dllmin[cnt]) && ((dllmax[cnt] - dllmin[cnt]) >= CBR_THRESHOLD2)) {
			dlli = dllmin[cnt];
			if (gold_sadj[1] >= dlli) {
				dlli = ((gold_sadj[1] - dlli) * 19) >> 5;
				if (dlli > 3) {
					dlli = 3;
				} else {
					dlli = (dlli - 1) & 0x7;
				}
			} else {
				dlli = ((dlli - gold_sadj[1]) * 19) >> 5;
				dlli += 1;
				if (dlli > 4) {
					dlli = 4;
				}
				dlli = (8 - dlli) & 0x7;
			}
			data |= dlli << 21;
		}
	}
	ast_moutdwm(ast, 0x1E6E0084, data);
	return status;
} /* finetuneDQI_L */

static void finetuneDQSI(struct ast_private *ast)
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
		passcnt[0] = passcnt[1] = 0;
		for (dqsip = 0; dqsip < 2; dqsip++) {
			ast_moutdwm(ast, 0x1E6E000C, 0);
			ast_moutdwm(ast, 0x1E6E0018, reg_mcr18 | (dqidly << 16) | (dqsip << 23));
			ast_moutdwm(ast, 0x1E6E000C, reg_mcr0c);
			for (dlli = 0; dlli < 76; dlli++) {
				ast_moutdwm(ast, 0x1E6E0068, 0x00001300 | (dlli << 16) | (dlli << 24));
				ast_moutdwm(ast, 0x1E6E0070, 0);
				ast_moutdwm(ast, 0x1E6E0074, CBR_SIZE0);
				if (cbr_scan3(ast)) {
					if (dlli == 0)
						break;
					passcnt[dqsip]++;
					tag[dqsip][dlli] = 'P';
					if (dlli < pass[dqidly][dqsip][0])
						pass[dqidly][dqsip][0] = (u16) dlli;
					if (dlli > pass[dqidly][dqsip][1])
						pass[dqidly][dqsip][1] = (u16) dlli;
				} else if (passcnt[dqsip] >= 5)
					break;
				else {
					pass[dqidly][dqsip][0] = 0xff;
					pass[dqidly][dqsip][1] = 0x0;
				}
			}
		}
		if (passcnt[0] == 0 && passcnt[1] == 0)
			dqidly++;
	}
	/* Search margin */
	g_dqidly = g_dqsip = g_margin = g_side = 0;

	for (dqidly = 0; dqidly < 32; dqidly++) {
		for (dqsip = 0; dqsip < 2; dqsip++) {
			if (pass[dqidly][dqsip][0] > pass[dqidly][dqsip][1])
				continue;
			diff = pass[dqidly][dqsip][1] - pass[dqidly][dqsip][0];
			if ((diff+2) < g_margin)
				continue;
			passcnt[0] = passcnt[1] = 0;
			for (dlli = pass[dqidly][dqsip][0]; dlli > 0  && tag[dqsip][dlli] != 0; dlli--, passcnt[0]++);
			for (dlli = pass[dqidly][dqsip][1]; dlli < 76 && tag[dqsip][dlli] != 0; dlli++, passcnt[1]++);
			if (passcnt[0] > passcnt[1])
				passcnt[0] = passcnt[1];
			passcnt[1] = 0;
			if (passcnt[0] > g_side)
				passcnt[1] = passcnt[0] - g_side;
			if (diff > (g_margin+1) && (passcnt[1] > 0 || passcnt[0] > 8)) {
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
static bool cbr_dll2(struct ast_private *ast, struct ast2300_dram_param *param)
{
	u32 dllmin[2], dllmax[2], dlli, data, passcnt, retry = 0;
	bool status = false;

	finetuneDQSI(ast);
	if (finetuneDQI_L(ast, param) == false)
		return status;

CBR_START2:
	dllmin[0] = dllmin[1] = 0xff;
	dllmax[0] = dllmax[1] = 0x0;
	passcnt = 0;
	for (dlli = 0; dlli < 76; dlli++) {
		ast_moutdwm(ast, 0x1E6E0068, 0x00001300 | (dlli << 16) | (dlli << 24));
		ast_moutdwm(ast, 0x1E6E0074, CBR_SIZE2);
		data = cbr_scan(ast);
		if (data != 0) {
			if (data & 0x1) {
				if (dllmin[0] > dlli) {
					dllmin[0] = dlli;
				}
				if (dllmax[0] < dlli) {
					dllmax[0] = dlli;
				}
			}
			if (data & 0x2) {
				if (dllmin[1] > dlli) {
					dllmin[1] = dlli;
				}
				if (dllmax[1] < dlli) {
					dllmax[1] = dlli;
				}
			}
			passcnt++;
		} else if (passcnt >= CBR_THRESHOLD) {
			break;
		}
	}
	if (retry++ > 10)
		goto CBR_DONE2;
	if (dllmax[0] == 0 || (dllmax[0]-dllmin[0]) < CBR_THRESHOLD) {
		goto CBR_START2;
	}
	if (dllmax[1] == 0 || (dllmax[1]-dllmin[1]) < CBR_THRESHOLD) {
		goto CBR_START2;
	}
	status = true;
CBR_DONE2:
	dlli  = (dllmin[1] + dllmax[1]) >> 1;
	dlli <<= 8;
	dlli += (dllmin[0] + dllmax[0]) >> 1;
	ast_moutdwm(ast, 0x1E6E0068, ast_mindwm(ast, 0x1E720058) | (dlli << 16));
	return status;
} /* CBRDLL2 */

static void get_ddr3_info(struct ast_private *ast, struct ast2300_dram_param *param)
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
	case AST_VIDMEM_SIZE_8M:
		param->dram_config |= 0x00;
		break;
	case AST_VIDMEM_SIZE_16M:
		param->dram_config |= 0x04;
		break;
	case AST_VIDMEM_SIZE_32M:
		param->dram_config |= 0x08;
		break;
	case AST_VIDMEM_SIZE_64M:
		param->dram_config |= 0x0c;
		break;
	}

}

static void ddr3_init(struct ast_private *ast, struct ast2300_dram_param *param)
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
		if ((data2 & 0xff) > param->madj_max) {
			break;
		}
		ast_moutdwm(ast, 0x1E6E0064, data2);
		if (data2 & 0x00100000) {
			data2 = ((data2 & 0xff) >> 3) + 3;
		} else {
			data2 = ((data2 & 0xff) >> 2) + 5;
		}
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
	if (param->wodt) {
		data = 0x300;
	}
	if (param->rodt) {
		data = data | 0x3000 | ((param->reg_AC2 & 0x60000) >> 3);
	}
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

static void get_ddr2_info(struct ast_private *ast, struct ast2300_dram_param *param)
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
	case AST_VIDMEM_SIZE_8M:
		param->dram_config |= 0x00;
		break;
	case AST_VIDMEM_SIZE_16M:
		param->dram_config |= 0x04;
		break;
	case AST_VIDMEM_SIZE_32M:
		param->dram_config |= 0x08;
		break;
	case AST_VIDMEM_SIZE_64M:
		param->dram_config |= 0x0c;
		break;
	}
}

static void ddr2_init(struct ast_private *ast, struct ast2300_dram_param *param)
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
		if ((data2 & 0xff) > param->madj_max) {
			break;
		}
		ast_moutdwm(ast, 0x1E6E0064, data2);
		if (data2 & 0x00100000) {
			data2 = ((data2 & 0xff) >> 3) + 3;
		} else {
			data2 = ((data2 & 0xff) >> 2) + 5;
		}
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
	if (param->wodt) {
		data = 0x500;
	}
	if (param->rodt) {
		data = data | 0x3000 | ((param->reg_AC2 & 0x60000) >> 3);
	}
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

static void ast_post_chip_2300(struct drm_device *dev)
{
	struct ast_private *ast = dev->dev_private;
	struct ast2300_dram_param param;
	u32 temp;
	u8 reg;

	reg = ast_get_index_reg_mask(ast, AST_IO_CRTC_PORT, 0xd0, 0xff);
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
			param.vram_size = AST_VIDMEM_SIZE_8M;
			break;

		case 0x04:
			param.vram_size = AST_VIDMEM_SIZE_16M;
			break;

		case 0x08:
			param.vram_size = AST_VIDMEM_SIZE_32M;
			break;

		case 0x0c:
			param.vram_size = AST_VIDMEM_SIZE_64M;
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
		reg = ast_get_index_reg_mask(ast, AST_IO_CRTC_PORT, 0xd0, 0xff);
	} while ((reg & 0x40) == 0);
}

static bool cbr_test_2500(struct ast_private *ast)
{
	ast_moutdwm(ast, 0x1E6E0074, 0x0000FFFF);
	ast_moutdwm(ast, 0x1E6E007C, 0xFF00FF00);
	if (!mmc_test_burst(ast, 0))
		return false;
	if (!mmc_test_single_2500(ast, 0))
		return false;
	return true;
}

static bool ddr_test_2500(struct ast_private *ast)
{
	ast_moutdwm(ast, 0x1E6E0074, 0x0000FFFF);
	ast_moutdwm(ast, 0x1E6E007C, 0xFF00FF00);
	if (!mmc_test_burst(ast, 0))
		return false;
	if (!mmc_test_burst(ast, 1))
		return false;
	if (!mmc_test_burst(ast, 2))
		return false;
	if (!mmc_test_burst(ast, 3))
		return false;
	if (!mmc_test_single_2500(ast, 0))
		return false;
	return true;
}

static void ddr_init_common_2500(struct ast_private *ast)
{
	ast_moutdwm(ast, 0x1E6E0034, 0x00020080);
	ast_moutdwm(ast, 0x1E6E0008, 0x2003000F);
	ast_moutdwm(ast, 0x1E6E0038, 0x00000FFF);
	ast_moutdwm(ast, 0x1E6E0040, 0x88448844);
	ast_moutdwm(ast, 0x1E6E0044, 0x24422288);
	ast_moutdwm(ast, 0x1E6E0048, 0x22222222);
	ast_moutdwm(ast, 0x1E6E004C, 0x22222222);
	ast_moutdwm(ast, 0x1E6E0050, 0x80000000);
	ast_moutdwm(ast, 0x1E6E0208, 0x00000000);
	ast_moutdwm(ast, 0x1E6E0218, 0x00000000);
	ast_moutdwm(ast, 0x1E6E0220, 0x00000000);
	ast_moutdwm(ast, 0x1E6E0228, 0x00000000);
	ast_moutdwm(ast, 0x1E6E0230, 0x00000000);
	ast_moutdwm(ast, 0x1E6E02A8, 0x00000000);
	ast_moutdwm(ast, 0x1E6E02B0, 0x00000000);
	ast_moutdwm(ast, 0x1E6E0240, 0x86000000);
	ast_moutdwm(ast, 0x1E6E0244, 0x00008600);
	ast_moutdwm(ast, 0x1E6E0248, 0x80000000);
	ast_moutdwm(ast, 0x1E6E024C, 0x80808080);
}

static void ddr_phy_init_2500(struct ast_private *ast)
{
	u32 data, pass, timecnt;

	pass = 0;
	ast_moutdwm(ast, 0x1E6E0060, 0x00000005);
	while (!pass) {
		for (timecnt = 0; timecnt < TIMEOUT; timecnt++) {
			data = ast_mindwm(ast, 0x1E6E0060) & 0x1;
			if (!data)
				break;
		}
		if (timecnt != TIMEOUT) {
			data = ast_mindwm(ast, 0x1E6E0300) & 0x000A0000;
			if (!data)
				pass = 1;
		}
		if (!pass) {
			ast_moutdwm(ast, 0x1E6E0060, 0x00000000);
			udelay(10); /* delay 10 us */
			ast_moutdwm(ast, 0x1E6E0060, 0x00000005);
		}
	}

	ast_moutdwm(ast, 0x1E6E0060, 0x00000006);
}

/*
 * Check DRAM Size
 * 1Gb : 0x80000000 ~ 0x87FFFFFF
 * 2Gb : 0x80000000 ~ 0x8FFFFFFF
 * 4Gb : 0x80000000 ~ 0x9FFFFFFF
 * 8Gb : 0x80000000 ~ 0xBFFFFFFF
 */
static void check_dram_size_2500(struct ast_private *ast, u32 tRFC)
{
	u32 reg_04, reg_14;

	reg_04 = ast_mindwm(ast, 0x1E6E0004) & 0xfffffffc;
	reg_14 = ast_mindwm(ast, 0x1E6E0014) & 0xffffff00;

	ast_moutdwm(ast, 0xA0100000, 0x41424344);
	ast_moutdwm(ast, 0x90100000, 0x35363738);
	ast_moutdwm(ast, 0x88100000, 0x292A2B2C);
	ast_moutdwm(ast, 0x80100000, 0x1D1E1F10);

	/* Check 8Gbit */
	if (ast_mindwm(ast, 0xA0100000) == 0x41424344) {
		reg_04 |= 0x03;
		reg_14 |= (tRFC >> 24) & 0xFF;
		/* Check 4Gbit */
	} else if (ast_mindwm(ast, 0x90100000) == 0x35363738) {
		reg_04 |= 0x02;
		reg_14 |= (tRFC >> 16) & 0xFF;
		/* Check 2Gbit */
	} else if (ast_mindwm(ast, 0x88100000) == 0x292A2B2C) {
		reg_04 |= 0x01;
		reg_14 |= (tRFC >> 8) & 0xFF;
	} else {
		reg_14 |= tRFC & 0xFF;
	}
	ast_moutdwm(ast, 0x1E6E0004, reg_04);
	ast_moutdwm(ast, 0x1E6E0014, reg_14);
}

static void enable_cache_2500(struct ast_private *ast)
{
	u32 reg_04, data;

	reg_04 = ast_mindwm(ast, 0x1E6E0004);
	ast_moutdwm(ast, 0x1E6E0004, reg_04 | 0x1000);

	do
		data = ast_mindwm(ast, 0x1E6E0004);
	while (!(data & 0x80000));
	ast_moutdwm(ast, 0x1E6E0004, reg_04 | 0x400);
}

static void set_mpll_2500(struct ast_private *ast)
{
	u32 addr, data, param;

	/* Reset MMC */
	ast_moutdwm(ast, 0x1E6E0000, 0xFC600309);
	ast_moutdwm(ast, 0x1E6E0034, 0x00020080);
	for (addr = 0x1e6e0004; addr < 0x1e6e0090;) {
		ast_moutdwm(ast, addr, 0x0);
		addr += 4;
	}
	ast_moutdwm(ast, 0x1E6E0034, 0x00020000);

	ast_moutdwm(ast, 0x1E6E2000, 0x1688A8A8);
	data = ast_mindwm(ast, 0x1E6E2070) & 0x00800000;
	if (data) {
		/* CLKIN = 25MHz */
		param = 0x930023E0;
		ast_moutdwm(ast, 0x1E6E2160, 0x00011320);
	} else {
		/* CLKIN = 24MHz */
		param = 0x93002400;
	}
	ast_moutdwm(ast, 0x1E6E2020, param);
	udelay(100);
}

static void reset_mmc_2500(struct ast_private *ast)
{
	ast_moutdwm(ast, 0x1E78505C, 0x00000004);
	ast_moutdwm(ast, 0x1E785044, 0x00000001);
	ast_moutdwm(ast, 0x1E785048, 0x00004755);
	ast_moutdwm(ast, 0x1E78504C, 0x00000013);
	mdelay(100);
	ast_moutdwm(ast, 0x1E785054, 0x00000077);
	ast_moutdwm(ast, 0x1E6E0000, 0xFC600309);
}

static void ddr3_init_2500(struct ast_private *ast, const u32 *ddr_table)
{

	ast_moutdwm(ast, 0x1E6E0004, 0x00000303);
	ast_moutdwm(ast, 0x1E6E0010, ddr_table[REGIDX_010]);
	ast_moutdwm(ast, 0x1E6E0014, ddr_table[REGIDX_014]);
	ast_moutdwm(ast, 0x1E6E0018, ddr_table[REGIDX_018]);
	ast_moutdwm(ast, 0x1E6E0020, ddr_table[REGIDX_020]);	     /* MODEREG4/6 */
	ast_moutdwm(ast, 0x1E6E0024, ddr_table[REGIDX_024]);	     /* MODEREG5 */
	ast_moutdwm(ast, 0x1E6E002C, ddr_table[REGIDX_02C] | 0x100); /* MODEREG0/2 */
	ast_moutdwm(ast, 0x1E6E0030, ddr_table[REGIDX_030]);	     /* MODEREG1/3 */

	/* DDR PHY Setting */
	ast_moutdwm(ast, 0x1E6E0200, 0x02492AAE);
	ast_moutdwm(ast, 0x1E6E0204, 0x00001001);
	ast_moutdwm(ast, 0x1E6E020C, 0x55E00B0B);
	ast_moutdwm(ast, 0x1E6E0210, 0x20000000);
	ast_moutdwm(ast, 0x1E6E0214, ddr_table[REGIDX_214]);
	ast_moutdwm(ast, 0x1E6E02E0, ddr_table[REGIDX_2E0]);
	ast_moutdwm(ast, 0x1E6E02E4, ddr_table[REGIDX_2E4]);
	ast_moutdwm(ast, 0x1E6E02E8, ddr_table[REGIDX_2E8]);
	ast_moutdwm(ast, 0x1E6E02EC, ddr_table[REGIDX_2EC]);
	ast_moutdwm(ast, 0x1E6E02F0, ddr_table[REGIDX_2F0]);
	ast_moutdwm(ast, 0x1E6E02F4, ddr_table[REGIDX_2F4]);
	ast_moutdwm(ast, 0x1E6E02F8, ddr_table[REGIDX_2F8]);
	ast_moutdwm(ast, 0x1E6E0290, 0x00100008);
	ast_moutdwm(ast, 0x1E6E02C0, 0x00000006);

	/* Controller Setting */
	ast_moutdwm(ast, 0x1E6E0034, 0x00020091);

	/* Wait DDR PHY init done */
	ddr_phy_init_2500(ast);

	ast_moutdwm(ast, 0x1E6E0120, ddr_table[REGIDX_PLL]);
	ast_moutdwm(ast, 0x1E6E000C, 0x42AA5C81);
	ast_moutdwm(ast, 0x1E6E0034, 0x0001AF93);

	check_dram_size_2500(ast, ddr_table[REGIDX_RFC]);
	enable_cache_2500(ast);
	ast_moutdwm(ast, 0x1E6E001C, 0x00000008);
	ast_moutdwm(ast, 0x1E6E0038, 0xFFFFFF00);
}

static void ddr4_init_2500(struct ast_private *ast, const u32 *ddr_table)
{
	u32 data, data2, pass, retrycnt;
	u32 ddr_vref, phy_vref;
	u32 min_ddr_vref = 0, min_phy_vref = 0;
	u32 max_ddr_vref = 0, max_phy_vref = 0;

	ast_moutdwm(ast, 0x1E6E0004, 0x00000313);
	ast_moutdwm(ast, 0x1E6E0010, ddr_table[REGIDX_010]);
	ast_moutdwm(ast, 0x1E6E0014, ddr_table[REGIDX_014]);
	ast_moutdwm(ast, 0x1E6E0018, ddr_table[REGIDX_018]);
	ast_moutdwm(ast, 0x1E6E0020, ddr_table[REGIDX_020]);	     /* MODEREG4/6 */
	ast_moutdwm(ast, 0x1E6E0024, ddr_table[REGIDX_024]);	     /* MODEREG5 */
	ast_moutdwm(ast, 0x1E6E002C, ddr_table[REGIDX_02C] | 0x100); /* MODEREG0/2 */
	ast_moutdwm(ast, 0x1E6E0030, ddr_table[REGIDX_030]);	     /* MODEREG1/3 */

	/* DDR PHY Setting */
	ast_moutdwm(ast, 0x1E6E0200, 0x42492AAE);
	ast_moutdwm(ast, 0x1E6E0204, 0x09002000);
	ast_moutdwm(ast, 0x1E6E020C, 0x55E00B0B);
	ast_moutdwm(ast, 0x1E6E0210, 0x20000000);
	ast_moutdwm(ast, 0x1E6E0214, ddr_table[REGIDX_214]);
	ast_moutdwm(ast, 0x1E6E02E0, ddr_table[REGIDX_2E0]);
	ast_moutdwm(ast, 0x1E6E02E4, ddr_table[REGIDX_2E4]);
	ast_moutdwm(ast, 0x1E6E02E8, ddr_table[REGIDX_2E8]);
	ast_moutdwm(ast, 0x1E6E02EC, ddr_table[REGIDX_2EC]);
	ast_moutdwm(ast, 0x1E6E02F0, ddr_table[REGIDX_2F0]);
	ast_moutdwm(ast, 0x1E6E02F4, ddr_table[REGIDX_2F4]);
	ast_moutdwm(ast, 0x1E6E02F8, ddr_table[REGIDX_2F8]);
	ast_moutdwm(ast, 0x1E6E0290, 0x00100008);
	ast_moutdwm(ast, 0x1E6E02C4, 0x3C183C3C);
	ast_moutdwm(ast, 0x1E6E02C8, 0x00631E0E);

	/* Controller Setting */
	ast_moutdwm(ast, 0x1E6E0034, 0x0001A991);

	/* Train PHY Vref first */
	pass = 0;

	for (retrycnt = 0; retrycnt < 4 && pass == 0; retrycnt++) {
		max_phy_vref = 0x0;
		pass = 0;
		ast_moutdwm(ast, 0x1E6E02C0, 0x00001C06);
		for (phy_vref = 0x40; phy_vref < 0x80; phy_vref++) {
			ast_moutdwm(ast, 0x1E6E000C, 0x00000000);
			ast_moutdwm(ast, 0x1E6E0060, 0x00000000);
			ast_moutdwm(ast, 0x1E6E02CC, phy_vref | (phy_vref << 8));
			/* Fire DFI Init */
			ddr_phy_init_2500(ast);
			ast_moutdwm(ast, 0x1E6E000C, 0x00005C01);
			if (cbr_test_2500(ast)) {
				pass++;
				data = ast_mindwm(ast, 0x1E6E03D0);
				data2 = data >> 8;
				data  = data & 0xff;
				if (data > data2)
					data = data2;
				if (max_phy_vref < data) {
					max_phy_vref = data;
					min_phy_vref = phy_vref;
				}
			} else if (pass > 0)
				break;
		}
	}
	ast_moutdwm(ast, 0x1E6E02CC, min_phy_vref | (min_phy_vref << 8));

	/* Train DDR Vref next */
	pass = 0;

	for (retrycnt = 0; retrycnt < 4 && pass == 0; retrycnt++) {
		min_ddr_vref = 0xFF;
		max_ddr_vref = 0x0;
		pass = 0;
		for (ddr_vref = 0x00; ddr_vref < 0x40; ddr_vref++) {
			ast_moutdwm(ast, 0x1E6E000C, 0x00000000);
			ast_moutdwm(ast, 0x1E6E0060, 0x00000000);
			ast_moutdwm(ast, 0x1E6E02C0, 0x00000006 | (ddr_vref << 8));
			/* Fire DFI Init */
			ddr_phy_init_2500(ast);
			ast_moutdwm(ast, 0x1E6E000C, 0x00005C01);
			if (cbr_test_2500(ast)) {
				pass++;
				if (min_ddr_vref > ddr_vref)
					min_ddr_vref = ddr_vref;
				if (max_ddr_vref < ddr_vref)
					max_ddr_vref = ddr_vref;
			} else if (pass != 0)
				break;
		}
	}

	ast_moutdwm(ast, 0x1E6E000C, 0x00000000);
	ast_moutdwm(ast, 0x1E6E0060, 0x00000000);
	ddr_vref = (min_ddr_vref + max_ddr_vref + 1) >> 1;
	ast_moutdwm(ast, 0x1E6E02C0, 0x00000006 | (ddr_vref << 8));

	/* Wait DDR PHY init done */
	ddr_phy_init_2500(ast);

	ast_moutdwm(ast, 0x1E6E0120, ddr_table[REGIDX_PLL]);
	ast_moutdwm(ast, 0x1E6E000C, 0x42AA5C81);
	ast_moutdwm(ast, 0x1E6E0034, 0x0001AF93);

	check_dram_size_2500(ast, ddr_table[REGIDX_RFC]);
	enable_cache_2500(ast);
	ast_moutdwm(ast, 0x1E6E001C, 0x00000008);
	ast_moutdwm(ast, 0x1E6E0038, 0xFFFFFF00);
}

static bool ast_dram_init_2500(struct ast_private *ast)
{
	u32 data;
	u32 max_tries = 5;

	do {
		if (max_tries-- == 0)
			return false;
		set_mpll_2500(ast);
		reset_mmc_2500(ast);
		ddr_init_common_2500(ast);

		data = ast_mindwm(ast, 0x1E6E2070);
		if (data & 0x01000000)
			ddr4_init_2500(ast, ast2500_ddr4_1600_timing_table);
		else
			ddr3_init_2500(ast, ast2500_ddr3_1600_timing_table);
	} while (!ddr_test_2500(ast));

	ast_moutdwm(ast, 0x1E6E2040, ast_mindwm(ast, 0x1E6E2040) | 0x41);

	/* Patch code */
	data = ast_mindwm(ast, 0x1E6E200C) & 0xF9FFFFFF;
	ast_moutdwm(ast, 0x1E6E200C, data | 0x10000000);

	return true;
}

void ast_post_chip_2500(struct drm_device *dev)
{
	struct ast_private *ast = dev->dev_private;
	u32 temp;
	u8 reg;

	reg = ast_get_index_reg_mask(ast, AST_IO_CRTC_PORT, 0xd0, 0xff);
	if ((reg & 0x80) == 0) {/* vga only */
		/* Clear bus lock condition */
		ast_moutdwm(ast, 0x1e600000, 0xAEED1A03);
		ast_moutdwm(ast, 0x1e600084, 0x00010000);
		ast_moutdwm(ast, 0x1e600088, 0x00000000);
		ast_moutdwm(ast, 0x1e6e2000, 0x1688A8A8);
		ast_write32(ast, 0xf004, 0x1e6e0000);
		ast_write32(ast, 0xf000, 0x1);
		ast_write32(ast, 0x12000, 0x1688a8a8);
		while (ast_read32(ast, 0x12000) != 0x1)
			;

		ast_write32(ast, 0x10000, 0xfc600309);
		while (ast_read32(ast, 0x10000) != 0x1)
			;

		/* Slow down CPU/AHB CLK in VGA only mode */
		temp = ast_read32(ast, 0x12008);
		temp |= 0x73;
		ast_write32(ast, 0x12008, temp);

		/* Reset USB port to patch USB unknown device issue */
		ast_moutdwm(ast, 0x1e6e2090, 0x20000000);
		temp  = ast_mindwm(ast, 0x1e6e2094);
		temp |= 0x00004000;
		ast_moutdwm(ast, 0x1e6e2094, temp);
		temp  = ast_mindwm(ast, 0x1e6e2070);
		if (temp & 0x00800000) {
			ast_moutdwm(ast, 0x1e6e207c, 0x00800000);
			mdelay(100);
			ast_moutdwm(ast, 0x1e6e2070, 0x00800000);
		}

		if (!ast_dram_init_2500(ast))
			DRM_ERROR("DRAM init failed !\n");

		temp = ast_mindwm(ast, 0x1e6e2040);
		ast_moutdwm(ast, 0x1e6e2040, temp | 0x40);
	}

	/* wait ready */
	do {
		reg = ast_get_index_reg_mask(ast, AST_IO_CRTC_PORT, 0xd0, 0xff);
	} while ((reg & 0x40) == 0);
}
