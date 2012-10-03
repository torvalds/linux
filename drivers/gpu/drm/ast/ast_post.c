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

static void ast_init_dram_2300(struct drm_device *dev);

static void
ast_enable_vga(struct drm_device *dev)
{
	struct ast_private *ast = dev->dev_private;

	ast_io_write8(ast, 0x43, 0x01);
	ast_io_write8(ast, 0x42, 0x01);
}

#if 0 /* will use later */
static bool
ast_is_vga_enabled(struct drm_device *dev)
{
	struct ast_private *ast = dev->dev_private;
	u8 ch;

	if (ast->chip == AST1180) {
		/* TODO 1180 */
	} else {
		ch = ast_io_read8(ast, 0x43);
		if (ch) {
			ast_open_key(ast);
			ch = ast_get_index_reg_mask(ast, AST_IO_CRTC_PORT, 0xb6, 0xff);
			return ch & 0x04;
		}
	}
	return 0;
}
#endif

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
	for (i = 0x81; i <= 0x8f; i++)
		ast_set_index_reg(ast, AST_IO_CRTC_PORT, i, 0x00);

	if (ast->chip == AST2300) {
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
	if (ast->chip == AST2300)
		reg |= 0x20;
	ast_set_index_reg_mask(ast, AST_IO_CRTC_PORT, 0xb6, 0xff, reg);
}

static inline u32 mindwm(struct ast_private *ast, u32 r)
{
	ast_write32(ast, 0xf004, r & 0xffff0000);
	ast_write32(ast, 0xf000, 0x1);

	return ast_read32(ast, 0x10000 + (r & 0x0000ffff));
}

static inline void moutdwm(struct ast_private *ast, u32 r, u32 v)
{
	ast_write32(ast, 0xf004, r & 0xffff0000);
	ast_write32(ast, 0xf000, 0x1);
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

	moutdwm(ast, 0x1e6e0070, 0x00000000);
	moutdwm(ast, 0x1e6e0070, 0x00000001 | (datagen << 3));
	timeout = 0;
	do {
		data = mindwm(ast, 0x1e6e0070) & 0x40;
		if (++timeout > TIMEOUT_AST2150) {
			moutdwm(ast, 0x1e6e0070, 0x00000000);
			return 0xffffffff;
		}
	} while (!data);
	moutdwm(ast, 0x1e6e0070, 0x00000000);
	moutdwm(ast, 0x1e6e0070, 0x00000003 | (datagen << 3));
	timeout = 0;
	do {
		data = mindwm(ast, 0x1e6e0070) & 0x40;
		if (++timeout > TIMEOUT_AST2150) {
			moutdwm(ast, 0x1e6e0070, 0x00000000);
			return 0xffffffff;
		}
	} while (!data);
	data = (mindwm(ast, 0x1e6e0070) & 0x80) >> 7;
	moutdwm(ast, 0x1e6e0070, 0x00000000);
	return data;
}

#if 0 /* unused in DDX driver - here for completeness */
static u32 mmctestsingle2_ast2150(struct ast_private *ast, u32 datagen)
{
	u32 data, timeout;

	moutdwm(ast, 0x1e6e0070, 0x00000000);
	moutdwm(ast, 0x1e6e0070, 0x00000005 | (datagen << 3));
	timeout = 0;
	do {
		data = mindwm(ast, 0x1e6e0070) & 0x40;
		if (++timeout > TIMEOUT_AST2150) {
			moutdwm(ast, 0x1e6e0070, 0x00000000);
			return 0xffffffff;
		}
	} while (!data);
	data = (mindwm(ast, 0x1e6e0070) & 0x80) >> 7;
	moutdwm(ast, 0x1e6e0070, 0x00000000);
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
		moutdwm(ast, 0x1e6e007c, pattern_AST2150[patcnt]);
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
		moutdwm(ast, 0x1e6e0068, dlli | (dlli << 8) | (dlli << 16) | (dlli << 24));
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
	moutdwm(ast, 0x1e6e0068, dlli | (dlli << 8) | (dlli << 16) | (dlli << 24));
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
	ast_set_def_ext_reg(dev);

	if (ast->chip == AST2300)
		ast_init_dram_2300(dev);
	else
		ast_init_dram_reg(dev);
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

#if 0 /* unused in DDX, included for completeness */
static int mmc_test_burst(struct ast_private *ast, u32 datagen)
{
	u32 data, timeout;

	moutdwm(ast, 0x1e6e0070, 0x00000000);
	moutdwm(ast, 0x1e6e0070, 0x000000c1 | (datagen << 3));
	timeout = 0;
	do {
		data = mindwm(ast, 0x1e6e0070) & 0x3000;
		if (data & 0x2000) {
			return 0;
		}
		if (++timeout > TIMEOUT) {
			moutdwm(ast, 0x1e6e0070, 0x00000000);
			return 0;
		}
	} while (!data);
	moutdwm(ast, 0x1e6e0070, 0x00000000);
	return 1;
}
#endif

static int mmc_test_burst2(struct ast_private *ast, u32 datagen)
{
	u32 data, timeout;

	moutdwm(ast, 0x1e6e0070, 0x00000000);
	moutdwm(ast, 0x1e6e0070, 0x00000041 | (datagen << 3));
	timeout = 0;
	do {
		data = mindwm(ast, 0x1e6e0070) & 0x1000;
		if (++timeout > TIMEOUT) {
			moutdwm(ast, 0x1e6e0070, 0x0);
			return -1;
		}
	} while (!data);
	data = mindwm(ast, 0x1e6e0078);
	data = (data | (data >> 16)) & 0xffff;
	moutdwm(ast, 0x1e6e0070, 0x0);
	return data;
}

#if 0 /* Unused in DDX here for completeness */
static int mmc_test_single(struct ast_private *ast, u32 datagen)
{
	u32 data, timeout;

	moutdwm(ast, 0x1e6e0070, 0x00000000);
	moutdwm(ast, 0x1e6e0070, 0x000000c5 | (datagen << 3));
	timeout = 0;
	do {
		data = mindwm(ast, 0x1e6e0070) & 0x3000;
		if (data & 0x2000)
			return 0;
		if (++timeout > TIMEOUT) {
			moutdwm(ast, 0x1e6e0070, 0x0);
			return 0;
		}
	} while (!data);
	moutdwm(ast, 0x1e6e0070, 0x0);
	return 1;
}
#endif

static int mmc_test_single2(struct ast_private *ast, u32 datagen)
{
	u32 data, timeout;

	moutdwm(ast, 0x1e6e0070, 0x00000000);
	moutdwm(ast, 0x1e6e0070, 0x00000005 | (datagen << 3));
	timeout = 0;
	do {
		data = mindwm(ast, 0x1e6e0070) & 0x1000;
		if (++timeout > TIMEOUT) {
			moutdwm(ast, 0x1e6e0070, 0x0);
			return -1;
		}
	} while (!data);
	data = mindwm(ast, 0x1e6e0078);
	data = (data | (data >> 16)) & 0xffff;
	moutdwm(ast, 0x1e6e0070, 0x0);
	return data;
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
		moutdwm(ast, 0x1e6e007c, pattern[patcnt]);
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
		moutdwm(ast, 0x1e6e007c, pattern[patcnt]);
		for (loop = 0; loop < CBR_PASSNUM2; loop++) {
			if ((data = cbr_test2(ast)) != 0) {
				data2 &= data;
				if (!data)
					return 0;
				break;
			}
		}
		if (loop == CBR_PASSNUM2)
			return 0;
	}
	return data2;
}

#if 0 /* unused in DDX - added for completeness */
static void finetuneDQI(struct ast_private *ast, struct ast2300_dram_param *param)
{
	u32 gold_sadj[2], dllmin[16], dllmax[16], dlli, data, cnt, mask, passcnt;

	gold_sadj[0] = (mindwm(ast, 0x1E6E0024) >> 16) & 0xffff;
	gold_sadj[1] = gold_sadj[0] >> 8;
	gold_sadj[0] = gold_sadj[0] & 0xff;
	gold_sadj[0] = (gold_sadj[0] + gold_sadj[1]) >> 1;
	gold_sadj[1] = gold_sadj[0];

	for (cnt = 0; cnt < 16; cnt++) {
		dllmin[cnt] = 0xff;
		dllmax[cnt] = 0x0;
	}
	passcnt = 0;
	for (dlli = 0; dlli < 76; dlli++) {
		moutdwm(ast, 0x1E6E0068, 0x00001400 | (dlli << 16) | (dlli << 24));
		/* Wait DQSI latch phase calibration */
		moutdwm(ast, 0x1E6E0074, 0x00000010);
		moutdwm(ast, 0x1E6E0070, 0x00000003);
		do {
			data = mindwm(ast, 0x1E6E0070);
		} while (!(data & 0x00001000));
		moutdwm(ast, 0x1E6E0070, 0x00000000);

		moutdwm(ast, 0x1E6E0074, CBR_SIZE1);
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
		} else if (passcnt >= CBR_THRESHOLD) {
			break;
		}
	}
	data = 0;
	for (cnt = 0; cnt < 8; cnt++) {
		data >>= 3;
		if ((dllmax[cnt] > dllmin[cnt]) && ((dllmax[cnt] - dllmin[cnt]) >= CBR_THRESHOLD)) {
			dlli = (dllmin[cnt] + dllmax[cnt]) >> 1;
			if (gold_sadj[0] >= dlli) {
				dlli = (gold_sadj[0] - dlli) >> 1;
				if (dlli > 3) {
					dlli = 3;
				}
			} else {
				dlli = (dlli - gold_sadj[0]) >> 1;
				if (dlli > 4) {
					dlli = 4;
				}
				dlli = (8 - dlli) & 0x7;
			}
			data |= dlli << 21;
		}
	}
	moutdwm(ast, 0x1E6E0080, data);

	data = 0;
	for (cnt = 8; cnt < 16; cnt++) {
		data >>= 3;
		if ((dllmax[cnt] > dllmin[cnt]) && ((dllmax[cnt] - dllmin[cnt]) >= CBR_THRESHOLD)) {
			dlli = (dllmin[cnt] + dllmax[cnt]) >> 1;
			if (gold_sadj[1] >= dlli) {
				dlli = (gold_sadj[1] - dlli) >> 1;
				if (dlli > 3) {
					dlli = 3;
				} else {
					dlli = (dlli - 1) & 0x7;
				}
			} else {
				dlli = (dlli - gold_sadj[1]) >> 1;
				dlli += 1;
				if (dlli > 4) {
					dlli = 4;
				}
				dlli = (8 - dlli) & 0x7;
			}
			data |= dlli << 21;
		}
	}
	moutdwm(ast, 0x1E6E0084, data);

} /* finetuneDQI */
#endif

static void finetuneDQI_L(struct ast_private *ast, struct ast2300_dram_param *param)
{
	u32 gold_sadj[2], dllmin[16], dllmax[16], dlli, data, cnt, mask, passcnt;

FINETUNE_START:
	for (cnt = 0; cnt < 16; cnt++) {
		dllmin[cnt] = 0xff;
		dllmax[cnt] = 0x0;
	}
	passcnt = 0;
	for (dlli = 0; dlli < 76; dlli++) {
		moutdwm(ast, 0x1E6E0068, 0x00001400 | (dlli << 16) | (dlli << 24));
		/* Wait DQSI latch phase calibration */
		moutdwm(ast, 0x1E6E0074, 0x00000010);
		moutdwm(ast, 0x1E6E0070, 0x00000003);
		do {
			data = mindwm(ast, 0x1E6E0070);
		} while (!(data & 0x00001000));
		moutdwm(ast, 0x1E6E0070, 0x00000000);

		moutdwm(ast, 0x1E6E0074, CBR_SIZE1);
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
	if (passcnt != 16) {
		goto FINETUNE_START;
	}
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
	moutdwm(ast, 0x1E6E0080, data);

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
	moutdwm(ast, 0x1E6E0084, data);

} /* finetuneDQI_L */

static void finetuneDQI_L2(struct ast_private *ast, struct ast2300_dram_param *param)
{
	u32 gold_sadj[2], dllmin[16], dllmax[16], dlli, data, cnt, mask, passcnt, data2;

	for (cnt = 0; cnt < 16; cnt++) {
		dllmin[cnt] = 0xff;
		dllmax[cnt] = 0x0;
	}
	passcnt = 0;
	for (dlli = 0; dlli < 76; dlli++) {
		moutdwm(ast, 0x1E6E0068, 0x00001400 | (dlli << 16) | (dlli << 24));
		/* Wait DQSI latch phase calibration */
		moutdwm(ast, 0x1E6E0074, 0x00000010);
		moutdwm(ast, 0x1E6E0070, 0x00000003);
		do {
			data = mindwm(ast, 0x1E6E0070);
		} while (!(data & 0x00001000));
		moutdwm(ast, 0x1E6E0070, 0x00000000);

		moutdwm(ast, 0x1E6E0074, CBR_SIZE2);
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
	gold_sadj[1] = 0xFF;
	for (cnt = 0; cnt < 8; cnt++) {
		if ((dllmax[cnt] > dllmin[cnt]) && ((dllmax[cnt] - dllmin[cnt]) >= CBR_THRESHOLD2)) {
			if (gold_sadj[0] < dllmin[cnt]) {
				gold_sadj[0] = dllmin[cnt];
			}
			if (gold_sadj[1] > dllmax[cnt]) {
				gold_sadj[1] = dllmax[cnt];
			}
		}
	}
	gold_sadj[0] = (gold_sadj[1] + gold_sadj[0]) >> 1;
	gold_sadj[1] = mindwm(ast, 0x1E6E0080);

	data = 0;
	for (cnt = 0; cnt < 8; cnt++) {
		data >>= 3;
		data2 = gold_sadj[1] & 0x7;
		gold_sadj[1] >>= 3;
		if ((dllmax[cnt] > dllmin[cnt]) && ((dllmax[cnt] - dllmin[cnt]) >= CBR_THRESHOLD2)) {
			dlli = (dllmin[cnt] + dllmax[cnt]) >> 1;
			if (gold_sadj[0] >= dlli) {
				dlli = (gold_sadj[0] - dlli) >> 1;
				if (dlli > 0) {
					dlli = 1;
				}
				if (data2 != 3) {
					data2 = (data2 + dlli) & 0x7;
				}
			} else {
				dlli = (dlli - gold_sadj[0]) >> 1;
				if (dlli > 0) {
					dlli = 1;
				}
				if (data2 != 4) {
					data2 = (data2 - dlli) & 0x7;
				}
			}
		}
		data |= data2 << 21;
	}
	moutdwm(ast, 0x1E6E0080, data);

	gold_sadj[0] = 0x0;
	gold_sadj[1] = 0xFF;
	for (cnt = 8; cnt < 16; cnt++) {
		if ((dllmax[cnt] > dllmin[cnt]) && ((dllmax[cnt] - dllmin[cnt]) >= CBR_THRESHOLD2)) {
			if (gold_sadj[0] < dllmin[cnt]) {
				gold_sadj[0] = dllmin[cnt];
			}
			if (gold_sadj[1] > dllmax[cnt]) {
				gold_sadj[1] = dllmax[cnt];
			}
		}
	}
	gold_sadj[0] = (gold_sadj[1] + gold_sadj[0]) >> 1;
	gold_sadj[1] = mindwm(ast, 0x1E6E0084);

	data = 0;
	for (cnt = 8; cnt < 16; cnt++) {
		data >>= 3;
		data2 = gold_sadj[1] & 0x7;
		gold_sadj[1] >>= 3;
		if ((dllmax[cnt] > dllmin[cnt]) && ((dllmax[cnt] - dllmin[cnt]) >= CBR_THRESHOLD2)) {
			dlli = (dllmin[cnt] + dllmax[cnt]) >> 1;
			if (gold_sadj[0] >= dlli) {
				dlli = (gold_sadj[0] - dlli) >> 1;
				if (dlli > 0) {
					dlli = 1;
				}
				if (data2 != 3) {
					data2 = (data2 + dlli) & 0x7;
				}
			} else {
				dlli = (dlli - gold_sadj[0]) >> 1;
				if (dlli > 0) {
					dlli = 1;
				}
				if (data2 != 4) {
					data2 = (data2 - dlli) & 0x7;
				}
			}
		}
		data |= data2 << 21;
	}
	moutdwm(ast, 0x1E6E0084, data);

} /* finetuneDQI_L2 */

static void cbr_dll2(struct ast_private *ast, struct ast2300_dram_param *param)
{
	u32 dllmin[2], dllmax[2], dlli, data, data2, passcnt;


	finetuneDQI_L(ast, param);
	finetuneDQI_L2(ast, param);

CBR_START2:
	dllmin[0] = dllmin[1] = 0xff;
	dllmax[0] = dllmax[1] = 0x0;
	passcnt = 0;
	for (dlli = 0; dlli < 76; dlli++) {
		moutdwm(ast, 0x1E6E0068, 0x00001300 | (dlli << 16) | (dlli << 24));
		/* Wait DQSI latch phase calibration */
		moutdwm(ast, 0x1E6E0074, 0x00000010);
		moutdwm(ast, 0x1E6E0070, 0x00000003);
		do {
			data = mindwm(ast, 0x1E6E0070);
		} while (!(data & 0x00001000));
		moutdwm(ast, 0x1E6E0070, 0x00000000);

		moutdwm(ast, 0x1E6E0074, CBR_SIZE2);
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
	if (dllmax[0] == 0 || (dllmax[0]-dllmin[0]) < CBR_THRESHOLD) {
		goto CBR_START2;
	}
	if (dllmax[1] == 0 || (dllmax[1]-dllmin[1]) < CBR_THRESHOLD) {
		goto CBR_START2;
	}
	dlli  = (dllmin[1] + dllmax[1]) >> 1;
	dlli <<= 8;
	dlli += (dllmin[0] + dllmax[0]) >> 1;
	moutdwm(ast, 0x1E6E0068, (mindwm(ast, 0x1E6E0068) & 0xFFFF) | (dlli << 16));

	data  = (mindwm(ast, 0x1E6E0080) >> 24) & 0x1F;
	data2 = (mindwm(ast, 0x1E6E0018) & 0xff80ffff) | (data << 16);
	moutdwm(ast, 0x1E6E0018, data2);
	moutdwm(ast, 0x1E6E0024, 0x8001 | (data << 1) | (param->dll2_finetune_step << 8));

	/* Wait DQSI latch phase calibration */
	moutdwm(ast, 0x1E6E0074, 0x00000010);
	moutdwm(ast, 0x1E6E0070, 0x00000003);
	do {
		data = mindwm(ast, 0x1E6E0070);
	} while (!(data & 0x00001000));
	moutdwm(ast, 0x1E6E0070, 0x00000000);
	moutdwm(ast, 0x1E6E0070, 0x00000003);
	do {
		data = mindwm(ast, 0x1E6E0070);
	} while (!(data & 0x00001000));
	moutdwm(ast, 0x1E6E0070, 0x00000000);
} /* CBRDLL2 */

static void get_ddr3_info(struct ast_private *ast, struct ast2300_dram_param *param)
{
	u32 trap, trap_AC2, trap_MRS;

	moutdwm(ast, 0x1E6E2000, 0x1688A8A8);

	/* Ger trap info */
	trap = (mindwm(ast, 0x1E6E2070) >> 25) & 0x3;
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
		moutdwm(ast, 0x1E6E2020, 0x0190);
		param->wodt          = 0;
		param->reg_AC1       = 0x22202725;
		param->reg_AC2       = 0xAA007613 | trap_AC2;
		param->reg_DQSIC     = 0x000000BA;
		param->reg_MRS       = 0x04001400 | trap_MRS;
		param->reg_EMRS      = 0x00000000;
		param->reg_IOZ       = 0x00000034;
		param->reg_DQIDLY    = 0x00000074;
		param->reg_FREQ      = 0x00004DC0;
		param->madj_max      = 96;
		param->dll2_finetune_step = 3;
		break;
	default:
	case 396:
		moutdwm(ast, 0x1E6E2020, 0x03F1);
		param->wodt          = 1;
		param->reg_AC1       = 0x33302825;
		param->reg_AC2       = 0xCC009617 | trap_AC2;
		param->reg_DQSIC     = 0x000000E2;
		param->reg_MRS       = 0x04001600 | trap_MRS;
		param->reg_EMRS      = 0x00000000;
		param->reg_IOZ       = 0x00000034;
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

	case 408:
		moutdwm(ast, 0x1E6E2020, 0x01F0);
		param->wodt          = 1;
		param->reg_AC1       = 0x33302825;
		param->reg_AC2       = 0xCC009617 | trap_AC2;
		param->reg_DQSIC     = 0x000000E2;
		param->reg_MRS       = 0x04001600 | trap_MRS;
		param->reg_EMRS      = 0x00000000;
		param->reg_IOZ       = 0x00000034;
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
		moutdwm(ast, 0x1E6E2020, 0x0230);
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
		moutdwm(ast, 0x1E6E2020, 0x0270);
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
		moutdwm(ast, 0x1E6E2020, 0x0290);
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
		moutdwm(ast, 0x1E6E2020, 0x0140);
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
		moutdwm(ast, 0x1E6E2020, 0x02E1);
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
		moutdwm(ast, 0x1E6E2020, 0x0160);
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
	}; /* switch size */

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
	u32 data, data2;

	moutdwm(ast, 0x1E6E0000, 0xFC600309);
	moutdwm(ast, 0x1E6E0018, 0x00000100);
	moutdwm(ast, 0x1E6E0024, 0x00000000);
	moutdwm(ast, 0x1E6E0034, 0x00000000);
	udelay(10);
	moutdwm(ast, 0x1E6E0064, param->reg_MADJ);
	moutdwm(ast, 0x1E6E0068, param->reg_SADJ);
	udelay(10);
	moutdwm(ast, 0x1E6E0064, param->reg_MADJ | 0xC0000);
	udelay(10);

	moutdwm(ast, 0x1E6E0004, param->dram_config);
	moutdwm(ast, 0x1E6E0008, 0x90040f);
	moutdwm(ast, 0x1E6E0010, param->reg_AC1);
	moutdwm(ast, 0x1E6E0014, param->reg_AC2);
	moutdwm(ast, 0x1E6E0020, param->reg_DQSIC);
	moutdwm(ast, 0x1E6E0080, 0x00000000);
	moutdwm(ast, 0x1E6E0084, 0x00000000);
	moutdwm(ast, 0x1E6E0088, param->reg_DQIDLY);
	moutdwm(ast, 0x1E6E0018, 0x4040A170);
	moutdwm(ast, 0x1E6E0018, 0x20402370);
	moutdwm(ast, 0x1E6E0038, 0x00000000);
	moutdwm(ast, 0x1E6E0040, 0xFF444444);
	moutdwm(ast, 0x1E6E0044, 0x22222222);
	moutdwm(ast, 0x1E6E0048, 0x22222222);
	moutdwm(ast, 0x1E6E004C, 0x00000002);
	moutdwm(ast, 0x1E6E0050, 0x80000000);
	moutdwm(ast, 0x1E6E0050, 0x00000000);
	moutdwm(ast, 0x1E6E0054, 0);
	moutdwm(ast, 0x1E6E0060, param->reg_DRV);
	moutdwm(ast, 0x1E6E006C, param->reg_IOZ);
	moutdwm(ast, 0x1E6E0070, 0x00000000);
	moutdwm(ast, 0x1E6E0074, 0x00000000);
	moutdwm(ast, 0x1E6E0078, 0x00000000);
	moutdwm(ast, 0x1E6E007C, 0x00000000);
	/* Wait MCLK2X lock to MCLK */
	do {
		data = mindwm(ast, 0x1E6E001C);
	} while (!(data & 0x08000000));
	moutdwm(ast, 0x1E6E0034, 0x00000001);
	moutdwm(ast, 0x1E6E000C, 0x00005C04);
	udelay(10);
	moutdwm(ast, 0x1E6E000C, 0x00000000);
	moutdwm(ast, 0x1E6E0034, 0x00000000);
	data = mindwm(ast, 0x1E6E001C);
	data = (data >> 8) & 0xff;
	while ((data & 0x08) || ((data & 0x7) < 2) || (data < 4)) {
		data2 = (mindwm(ast, 0x1E6E0064) & 0xfff3ffff) + 4;
		if ((data2 & 0xff) > param->madj_max) {
			break;
		}
		moutdwm(ast, 0x1E6E0064, data2);
		if (data2 & 0x00100000) {
			data2 = ((data2 & 0xff) >> 3) + 3;
		} else {
			data2 = ((data2 & 0xff) >> 2) + 5;
		}
		data = mindwm(ast, 0x1E6E0068) & 0xffff00ff;
		data2 += data & 0xff;
		data = data | (data2 << 8);
		moutdwm(ast, 0x1E6E0068, data);
		udelay(10);
		moutdwm(ast, 0x1E6E0064, mindwm(ast, 0x1E6E0064) | 0xC0000);
		udelay(10);
		data = mindwm(ast, 0x1E6E0018) & 0xfffff1ff;
		moutdwm(ast, 0x1E6E0018, data);
		data = data | 0x200;
		moutdwm(ast, 0x1E6E0018, data);
		do {
			data = mindwm(ast, 0x1E6E001C);
		} while (!(data & 0x08000000));

		moutdwm(ast, 0x1E6E0034, 0x00000001);
		moutdwm(ast, 0x1E6E000C, 0x00005C04);
		udelay(10);
		moutdwm(ast, 0x1E6E000C, 0x00000000);
		moutdwm(ast, 0x1E6E0034, 0x00000000);
		data = mindwm(ast, 0x1E6E001C);
		data = (data >> 8) & 0xff;
	}
	data = mindwm(ast, 0x1E6E0018) | 0xC00;
	moutdwm(ast, 0x1E6E0018, data);

	moutdwm(ast, 0x1E6E0034, 0x00000001);
	moutdwm(ast, 0x1E6E000C, 0x00000040);
	udelay(50);
	/* Mode Register Setting */
	moutdwm(ast, 0x1E6E002C, param->reg_MRS | 0x100);
	moutdwm(ast, 0x1E6E0030, param->reg_EMRS);
	moutdwm(ast, 0x1E6E0028, 0x00000005);
	moutdwm(ast, 0x1E6E0028, 0x00000007);
	moutdwm(ast, 0x1E6E0028, 0x00000003);
	moutdwm(ast, 0x1E6E0028, 0x00000001);
	moutdwm(ast, 0x1E6E002C, param->reg_MRS);
	moutdwm(ast, 0x1E6E000C, 0x00005C08);
	moutdwm(ast, 0x1E6E0028, 0x00000001);

	moutdwm(ast, 0x1E6E000C, 0x7FFF5C01);
	data = 0;
	if (param->wodt) {
		data = 0x300;
	}
	if (param->rodt) {
		data = data | 0x3000 | ((param->reg_AC2 & 0x60000) >> 3);
	}
	moutdwm(ast, 0x1E6E0034, data | 0x3);

	/* Wait DQI delay lock */
	do {
		data = mindwm(ast, 0x1E6E0080);
	} while (!(data & 0x40000000));
	/* Wait DQSI delay lock */
	do {
		data = mindwm(ast, 0x1E6E0020);
	} while (!(data & 0x00000800));
	/* Calibrate the DQSI delay */
	cbr_dll2(ast, param);

	moutdwm(ast, 0x1E6E0120, param->reg_FREQ);
	/* ECC Memory Initialization */
#ifdef ECC
	moutdwm(ast, 0x1E6E007C, 0x00000000);
	moutdwm(ast, 0x1E6E0070, 0x221);
	do {
		data = mindwm(ast, 0x1E6E0070);
	} while (!(data & 0x00001000));
	moutdwm(ast, 0x1E6E0070, 0x00000000);
	moutdwm(ast, 0x1E6E0050, 0x80000000);
	moutdwm(ast, 0x1E6E0050, 0x00000000);
#endif


}

static void get_ddr2_info(struct ast_private *ast, struct ast2300_dram_param *param)
{
	u32 trap, trap_AC2, trap_MRS;

	moutdwm(ast, 0x1E6E2000, 0x1688A8A8);

	/* Ger trap info */
	trap = (mindwm(ast, 0x1E6E2070) >> 25) & 0x3;
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
		moutdwm(ast, 0x1E6E2020, 0x0130);
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
		moutdwm(ast, 0x1E6E2020, 0x0190);
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
		break;
	default:
	case 396:
		moutdwm(ast, 0x1E6E2020, 0x03F1);
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

	case 408:
		moutdwm(ast, 0x1E6E2020, 0x01F0);
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
		moutdwm(ast, 0x1E6E2020, 0x0230);
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
		moutdwm(ast, 0x1E6E2020, 0x0261);
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
		moutdwm(ast, 0x1E6E2020, 0x0120);
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
		moutdwm(ast, 0x1E6E2020, 0x02A1);
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
		moutdwm(ast, 0x1E6E2020, 0x0140);
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
	}; /* switch size */

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
	u32 data, data2;

	moutdwm(ast, 0x1E6E0000, 0xFC600309);
	moutdwm(ast, 0x1E6E0018, 0x00000100);
	moutdwm(ast, 0x1E6E0024, 0x00000000);
	moutdwm(ast, 0x1E6E0064, param->reg_MADJ);
	moutdwm(ast, 0x1E6E0068, param->reg_SADJ);
	udelay(10);
	moutdwm(ast, 0x1E6E0064, param->reg_MADJ | 0xC0000);
	udelay(10);

	moutdwm(ast, 0x1E6E0004, param->dram_config);
	moutdwm(ast, 0x1E6E0008, 0x90040f);
	moutdwm(ast, 0x1E6E0010, param->reg_AC1);
	moutdwm(ast, 0x1E6E0014, param->reg_AC2);
	moutdwm(ast, 0x1E6E0020, param->reg_DQSIC);
	moutdwm(ast, 0x1E6E0080, 0x00000000);
	moutdwm(ast, 0x1E6E0084, 0x00000000);
	moutdwm(ast, 0x1E6E0088, param->reg_DQIDLY);
	moutdwm(ast, 0x1E6E0018, 0x4040A130);
	moutdwm(ast, 0x1E6E0018, 0x20402330);
	moutdwm(ast, 0x1E6E0038, 0x00000000);
	moutdwm(ast, 0x1E6E0040, 0xFF808000);
	moutdwm(ast, 0x1E6E0044, 0x88848466);
	moutdwm(ast, 0x1E6E0048, 0x44440008);
	moutdwm(ast, 0x1E6E004C, 0x00000000);
	moutdwm(ast, 0x1E6E0050, 0x80000000);
	moutdwm(ast, 0x1E6E0050, 0x00000000);
	moutdwm(ast, 0x1E6E0054, 0);
	moutdwm(ast, 0x1E6E0060, param->reg_DRV);
	moutdwm(ast, 0x1E6E006C, param->reg_IOZ);
	moutdwm(ast, 0x1E6E0070, 0x00000000);
	moutdwm(ast, 0x1E6E0074, 0x00000000);
	moutdwm(ast, 0x1E6E0078, 0x00000000);
	moutdwm(ast, 0x1E6E007C, 0x00000000);

	/* Wait MCLK2X lock to MCLK */
	do {
		data = mindwm(ast, 0x1E6E001C);
	} while (!(data & 0x08000000));
	moutdwm(ast, 0x1E6E0034, 0x00000001);
	moutdwm(ast, 0x1E6E000C, 0x00005C04);
	udelay(10);
	moutdwm(ast, 0x1E6E000C, 0x00000000);
	moutdwm(ast, 0x1E6E0034, 0x00000000);
	data = mindwm(ast, 0x1E6E001C);
	data = (data >> 8) & 0xff;
	while ((data & 0x08) || ((data & 0x7) < 2) || (data < 4)) {
		data2 = (mindwm(ast, 0x1E6E0064) & 0xfff3ffff) + 4;
		if ((data2 & 0xff) > param->madj_max) {
			break;
		}
		moutdwm(ast, 0x1E6E0064, data2);
		if (data2 & 0x00100000) {
			data2 = ((data2 & 0xff) >> 3) + 3;
		} else {
			data2 = ((data2 & 0xff) >> 2) + 5;
		}
		data = mindwm(ast, 0x1E6E0068) & 0xffff00ff;
		data2 += data & 0xff;
		data = data | (data2 << 8);
		moutdwm(ast, 0x1E6E0068, data);
		udelay(10);
		moutdwm(ast, 0x1E6E0064, mindwm(ast, 0x1E6E0064) | 0xC0000);
		udelay(10);
		data = mindwm(ast, 0x1E6E0018) & 0xfffff1ff;
		moutdwm(ast, 0x1E6E0018, data);
		data = data | 0x200;
		moutdwm(ast, 0x1E6E0018, data);
		do {
			data = mindwm(ast, 0x1E6E001C);
		} while (!(data & 0x08000000));

		moutdwm(ast, 0x1E6E0034, 0x00000001);
		moutdwm(ast, 0x1E6E000C, 0x00005C04);
		udelay(10);
		moutdwm(ast, 0x1E6E000C, 0x00000000);
		moutdwm(ast, 0x1E6E0034, 0x00000000);
		data = mindwm(ast, 0x1E6E001C);
		data = (data >> 8) & 0xff;
	}
	data = mindwm(ast, 0x1E6E0018) | 0xC00;
	moutdwm(ast, 0x1E6E0018, data);

	moutdwm(ast, 0x1E6E0034, 0x00000001);
	moutdwm(ast, 0x1E6E000C, 0x00000000);
	udelay(50);
	/* Mode Register Setting */
	moutdwm(ast, 0x1E6E002C, param->reg_MRS | 0x100);
	moutdwm(ast, 0x1E6E0030, param->reg_EMRS);
	moutdwm(ast, 0x1E6E0028, 0x00000005);
	moutdwm(ast, 0x1E6E0028, 0x00000007);
	moutdwm(ast, 0x1E6E0028, 0x00000003);
	moutdwm(ast, 0x1E6E0028, 0x00000001);

	moutdwm(ast, 0x1E6E000C, 0x00005C08);
	moutdwm(ast, 0x1E6E002C, param->reg_MRS);
	moutdwm(ast, 0x1E6E0028, 0x00000001);
	moutdwm(ast, 0x1E6E0030, param->reg_EMRS | 0x380);
	moutdwm(ast, 0x1E6E0028, 0x00000003);
	moutdwm(ast, 0x1E6E0030, param->reg_EMRS);
	moutdwm(ast, 0x1E6E0028, 0x00000003);

	moutdwm(ast, 0x1E6E000C, 0x7FFF5C01);
	data = 0;
	if (param->wodt) {
		data = 0x500;
	}
	if (param->rodt) {
		data = data | 0x3000 | ((param->reg_AC2 & 0x60000) >> 3);
	}
	moutdwm(ast, 0x1E6E0034, data | 0x3);
	moutdwm(ast, 0x1E6E0120, param->reg_FREQ);

	/* Wait DQI delay lock */
	do {
		data = mindwm(ast, 0x1E6E0080);
	} while (!(data & 0x40000000));
	/* Wait DQSI delay lock */
	do {
		data = mindwm(ast, 0x1E6E0020);
	} while (!(data & 0x00000800));
	/* Calibrate the DQSI delay */
	cbr_dll2(ast, param);

	/* ECC Memory Initialization */
#ifdef ECC
	moutdwm(ast, 0x1E6E007C, 0x00000000);
	moutdwm(ast, 0x1E6E0070, 0x221);
	do {
		data = mindwm(ast, 0x1E6E0070);
	} while (!(data & 0x00001000));
	moutdwm(ast, 0x1E6E0070, 0x00000000);
	moutdwm(ast, 0x1E6E0050, 0x80000000);
	moutdwm(ast, 0x1E6E0050, 0x00000000);
#endif

}

static void ast_init_dram_2300(struct drm_device *dev)
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

		param.dram_type = AST_DDR3;
		if (temp & 0x01000000)
			param.dram_type = AST_DDR2;
		param.dram_chipid = ast->dram_type;
		param.dram_freq = ast->mclk;
		param.vram_size = ast->vram_size;

		if (param.dram_type == AST_DDR3) {
			get_ddr3_info(ast, &param);
			ddr3_init(ast, &param);
		} else {
			get_ddr2_info(ast, &param);
			ddr2_init(ast, &param);
		}

		temp = mindwm(ast, 0x1e6e2040);
		moutdwm(ast, 0x1e6e2040, temp | 0x40);
	}

	/* wait ready */
	do {
		reg = ast_get_index_reg_mask(ast, AST_IO_CRTC_PORT, 0xd0, 0xff);
	} while ((reg & 0x40) == 0);
}

