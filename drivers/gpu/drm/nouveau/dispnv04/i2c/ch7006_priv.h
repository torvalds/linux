/*
 * Copyright (C) 2009 Francisco Jerez.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE COPYRIGHT OWNER(S) AND/OR ITS SUPPLIERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#ifndef __NOUVEAU_I2C_CH7006_PRIV_H__
#define __NOUVEAU_I2C_CH7006_PRIV_H__

#include <drm/drm_probe_helper.h>

#include <dispnv04/i2c/encoder_i2c.h>
#include <dispnv04/i2c/ch7006.h>

typedef int64_t fixed;
#define fixed1 (1LL << 32)

enum ch7006_tv_norm {
	TV_NORM_PAL,
	TV_NORM_PAL_M,
	TV_NORM_PAL_N,
	TV_NORM_PAL_NC,
	TV_NORM_PAL_60,
	TV_NORM_NTSC_M,
	TV_NORM_NTSC_J,
	NUM_TV_NORMS
};

struct ch7006_tv_norm_info {
	fixed vrefresh;
	int vdisplay;
	int vtotal;
	int hvirtual;

	fixed subc_freq;
	fixed black_level;

	uint32_t dispmode;
	int voffset;
};

struct ch7006_mode {
	struct drm_display_mode mode;

	int enc_hdisp;
	int enc_vdisp;

	fixed subc_coeff;
	uint32_t dispmode;

	uint32_t valid_scales;
	uint32_t valid_norms;
};

struct ch7006_state {
	uint8_t regs[0x26];
};

struct ch7006_priv {
	struct ch7006_encoder_params params;
	const struct ch7006_mode *mode;

	struct ch7006_state state;
	struct ch7006_state saved_state;

	struct drm_property *scale_property;

	int select_subconnector;
	int subconnector;
	int hmargin;
	int vmargin;
	enum ch7006_tv_norm norm;
	int brightness;
	int contrast;
	int flicker;
	int scale;

	int chip_version;
	int last_dpms;
};

#define to_ch7006_priv(x) \
	((struct ch7006_priv *)to_encoder_i2c(x)->encoder_i2c_priv)

extern int ch7006_debug;
extern char *ch7006_tv_norm;
extern int ch7006_scale;

extern const char * const ch7006_tv_norm_names[];
extern const struct ch7006_tv_norm_info ch7006_tv_norms[];
extern const struct ch7006_mode ch7006_modes[];

const struct ch7006_mode *ch7006_lookup_mode(struct drm_encoder *encoder,
					     const struct drm_display_mode *drm_mode);

void ch7006_setup_levels(struct drm_encoder *encoder);
void ch7006_setup_subcarrier(struct drm_encoder *encoder);
void ch7006_setup_pll(struct drm_encoder *encoder);
void ch7006_setup_power_state(struct drm_encoder *encoder);
void ch7006_setup_properties(struct drm_encoder *encoder);

void ch7006_write(struct i2c_client *client, uint8_t addr, uint8_t val);
uint8_t ch7006_read(struct i2c_client *client, uint8_t addr);

void ch7006_state_load(struct i2c_client *client,
		       struct ch7006_state *state);
void ch7006_state_save(struct i2c_client *client,
		       struct ch7006_state *state);

/* Some helper macros */

#define ch7006_dbg(client, format, ...) do {				\
		if (ch7006_debug)					\
			dev_printk(KERN_DEBUG, &client->dev,		\
				   "%s: " format, __func__, ## __VA_ARGS__); \
	} while (0)
#define ch7006_info(client, format, ...) \
				dev_info(&client->dev, format, __VA_ARGS__)
#define ch7006_err(client, format, ...) \
				dev_err(&client->dev, format, __VA_ARGS__)

#define __mask(src, bitfield) \
		(((2 << (1 ? bitfield)) - 1) & ~((1 << (0 ? bitfield)) - 1))
#define mask(bitfield) __mask(bitfield)

#define __bitf(src, bitfield, x) \
		(((x) >> (src) << (0 ? bitfield)) &  __mask(src, bitfield))
#define bitf(bitfield, x) __bitf(bitfield, x)
#define bitfs(bitfield, s) __bitf(bitfield, bitfield##_##s)
#define setbitf(state, reg, bitfield, x)				\
	state->regs[reg] = (state->regs[reg] & ~mask(reg##_##bitfield))	\
		| bitf(reg##_##bitfield, x)

#define __unbitf(src, bitfield, x) \
		((x & __mask(src, bitfield)) >> (0 ? bitfield) << (src))
#define unbitf(bitfield, x) __unbitf(bitfield, x)

static inline int interpolate(int y0, int y1, int y2, int x)
{
	return y1 + (x < 50 ? y1 - y0 : y2 - y1) * (x - 50) / 50;
}

static inline int32_t round_fixed(fixed x)
{
	return (x + fixed1/2) >> 32;
}

#define ch7006_load_reg(client, state, reg) ch7006_write(client, reg, state->regs[reg])
#define ch7006_save_reg(client, state, reg) state->regs[reg] = ch7006_read(client, reg)

/* Fixed hardware specs */

#define CH7006_FREQ0				14318
#define CH7006_MAXN				650
#define CH7006_MAXM				315

/* Register definitions */

#define CH7006_DISPMODE				0x00
#define CH7006_DISPMODE_INPUT_RES		0, 7:5
#define CH7006_DISPMODE_INPUT_RES_512x384	0x0
#define CH7006_DISPMODE_INPUT_RES_720x400	0x1
#define CH7006_DISPMODE_INPUT_RES_640x400	0x2
#define CH7006_DISPMODE_INPUT_RES_640x480	0x3
#define CH7006_DISPMODE_INPUT_RES_800x600	0x4
#define CH7006_DISPMODE_INPUT_RES_NATIVE	0x5
#define CH7006_DISPMODE_OUTPUT_STD		0, 4:3
#define CH7006_DISPMODE_OUTPUT_STD_PAL		0x0
#define CH7006_DISPMODE_OUTPUT_STD_NTSC		0x1
#define CH7006_DISPMODE_OUTPUT_STD_PAL_M	0x2
#define CH7006_DISPMODE_OUTPUT_STD_NTSC_J	0x3
#define CH7006_DISPMODE_SCALING_RATIO		0, 2:0
#define CH7006_DISPMODE_SCALING_RATIO_5_4	0x0
#define CH7006_DISPMODE_SCALING_RATIO_1_1	0x1
#define CH7006_DISPMODE_SCALING_RATIO_7_8	0x2
#define CH7006_DISPMODE_SCALING_RATIO_5_6	0x3
#define CH7006_DISPMODE_SCALING_RATIO_3_4	0x4
#define CH7006_DISPMODE_SCALING_RATIO_7_10	0x5

#define CH7006_FFILTER				0x01
#define CH7006_FFILTER_TEXT			0, 5:4
#define CH7006_FFILTER_LUMA			0, 3:2
#define CH7006_FFILTER_CHROMA			0, 1:0
#define CH7006_FFILTER_CHROMA_NO_DCRAWL		0x3

#define CH7006_BWIDTH				0x03
#define CH7006_BWIDTH_5L_FFILER			(1 << 7)
#define CH7006_BWIDTH_CVBS_NO_CHROMA		(1 << 6)
#define CH7006_BWIDTH_CHROMA			0, 5:4
#define CH7006_BWIDTH_SVIDEO_YPEAK		(1 << 3)
#define CH7006_BWIDTH_SVIDEO_LUMA		0, 2:1
#define CH7006_BWIDTH_CVBS_LUMA			0, 0:0

#define CH7006_INPUT_FORMAT			0x04
#define CH7006_INPUT_FORMAT_DAC_GAIN		(1 << 6)
#define CH7006_INPUT_FORMAT_RGB_PASS_THROUGH	(1 << 5)
#define CH7006_INPUT_FORMAT_FORMAT		0, 3:0
#define CH7006_INPUT_FORMAT_FORMAT_RGB16	0x0
#define CH7006_INPUT_FORMAT_FORMAT_YCrCb24m16	0x1
#define CH7006_INPUT_FORMAT_FORMAT_RGB24m16	0x2
#define CH7006_INPUT_FORMAT_FORMAT_RGB15	0x3
#define CH7006_INPUT_FORMAT_FORMAT_RGB24m12C	0x4
#define CH7006_INPUT_FORMAT_FORMAT_RGB24m12I	0x5
#define CH7006_INPUT_FORMAT_FORMAT_RGB24m8	0x6
#define CH7006_INPUT_FORMAT_FORMAT_RGB16m8	0x7
#define CH7006_INPUT_FORMAT_FORMAT_RGB15m8	0x8
#define CH7006_INPUT_FORMAT_FORMAT_YCrCb24m8	0x9

#define CH7006_CLKMODE				0x06
#define CH7006_CLKMODE_SUBC_LOCK		(1 << 7)
#define CH7006_CLKMODE_MASTER			(1 << 6)
#define CH7006_CLKMODE_POS_EDGE			(1 << 4)
#define CH7006_CLKMODE_XCM			0, 3:2
#define CH7006_CLKMODE_PCM			0, 1:0

#define CH7006_START_ACTIVE			0x07
#define CH7006_START_ACTIVE_0			0, 7:0

#define CH7006_POV				0x08
#define CH7006_POV_START_ACTIVE_8		8, 2:2
#define CH7006_POV_HPOS_8			8, 1:1
#define CH7006_POV_VPOS_8			8, 0:0

#define CH7006_BLACK_LEVEL			0x09
#define CH7006_BLACK_LEVEL_0			0, 7:0

#define CH7006_HPOS				0x0a
#define CH7006_HPOS_0				0, 7:0

#define CH7006_VPOS				0x0b
#define CH7006_VPOS_0				0, 7:0

#define CH7006_INPUT_SYNC			0x0d
#define CH7006_INPUT_SYNC_EMBEDDED		(1 << 3)
#define CH7006_INPUT_SYNC_OUTPUT		(1 << 2)
#define CH7006_INPUT_SYNC_PVSYNC		(1 << 1)
#define CH7006_INPUT_SYNC_PHSYNC		(1 << 0)

#define CH7006_POWER				0x0e
#define CH7006_POWER_SCART			(1 << 4)
#define CH7006_POWER_RESET			(1 << 3)
#define CH7006_POWER_LEVEL			0, 2:0
#define CH7006_POWER_LEVEL_CVBS_OFF		0x0
#define CH7006_POWER_LEVEL_POWER_OFF		0x1
#define CH7006_POWER_LEVEL_SVIDEO_OFF		0x2
#define CH7006_POWER_LEVEL_NORMAL		0x3
#define CH7006_POWER_LEVEL_FULL_POWER_OFF	0x4

#define CH7006_DETECT				0x10
#define CH7006_DETECT_SVIDEO_Y_TEST		(1 << 3)
#define CH7006_DETECT_SVIDEO_C_TEST		(1 << 2)
#define CH7006_DETECT_CVBS_TEST			(1 << 1)
#define CH7006_DETECT_SENSE			(1 << 0)

#define CH7006_CONTRAST				0x11
#define CH7006_CONTRAST_0			0, 2:0

#define CH7006_PLLOV	 			0x13
#define CH7006_PLLOV_N_8	 		8, 2:1
#define CH7006_PLLOV_M_8	 		8, 0:0

#define CH7006_PLLM	 			0x14
#define CH7006_PLLM_0	 			0, 7:0

#define CH7006_PLLN	 			0x15
#define CH7006_PLLN_0	 			0, 7:0

#define CH7006_BCLKOUT	 			0x17

#define CH7006_SUBC_INC0			0x18
#define CH7006_SUBC_INC0_28			28, 3:0

#define CH7006_SUBC_INC1			0x19
#define CH7006_SUBC_INC1_24			24, 3:0

#define CH7006_SUBC_INC2			0x1a
#define CH7006_SUBC_INC2_20			20, 3:0

#define CH7006_SUBC_INC3			0x1b
#define CH7006_SUBC_INC3_GPIO1_VAL		(1 << 7)
#define CH7006_SUBC_INC3_GPIO0_VAL		(1 << 6)
#define CH7006_SUBC_INC3_POUT_3_3V		(1 << 5)
#define CH7006_SUBC_INC3_POUT_INV		(1 << 4)
#define CH7006_SUBC_INC3_16			16, 3:0

#define CH7006_SUBC_INC4			0x1c
#define CH7006_SUBC_INC4_GPIO1_IN		(1 << 7)
#define CH7006_SUBC_INC4_GPIO0_IN		(1 << 6)
#define CH7006_SUBC_INC4_DS_INPUT		(1 << 4)
#define CH7006_SUBC_INC4_12			12, 3:0

#define CH7006_SUBC_INC5			0x1d
#define CH7006_SUBC_INC5_8			8, 3:0

#define CH7006_SUBC_INC6			0x1e
#define CH7006_SUBC_INC6_4			4, 3:0

#define CH7006_SUBC_INC7			0x1f
#define CH7006_SUBC_INC7_0			0, 3:0

#define CH7006_PLL_CONTROL			0x20
#define CH7006_PLL_CONTROL_CPI			(1 << 5)
#define CH7006_PLL_CONTROL_CAPACITOR		(1 << 4)
#define CH7006_PLL_CONTROL_7STAGES		(1 << 3)
#define CH7006_PLL_CONTROL_DIGITAL_5V		(1 << 2)
#define CH7006_PLL_CONTROL_ANALOG_5V		(1 << 1)
#define CH7006_PLL_CONTROL_MEMORY_5V		(1 << 0)

#define CH7006_CALC_SUBC_INC0			0x21
#define CH7006_CALC_SUBC_INC0_24		24, 4:3
#define CH7006_CALC_SUBC_INC0_HYST		0, 2:1
#define CH7006_CALC_SUBC_INC0_AUTO		(1 << 0)

#define CH7006_CALC_SUBC_INC1			0x22
#define CH7006_CALC_SUBC_INC1_16		16, 7:0

#define CH7006_CALC_SUBC_INC2			0x23
#define CH7006_CALC_SUBC_INC2_8			8, 7:0

#define CH7006_CALC_SUBC_INC3			0x24
#define CH7006_CALC_SUBC_INC3_0			0, 7:0

#define CH7006_VERSION_ID			0x25

#endif
