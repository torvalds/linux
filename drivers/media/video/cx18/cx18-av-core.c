/*
 *  cx18 ADEC audio functions
 *
 *  Derived from cx25840-core.c
 *
 *  Copyright (C) 2007  Hans Verkuil <hverkuil@xs4all.nl>
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version 2
 *  of the License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 *  02110-1301, USA.
 */

#include "cx18-driver.h"

int cx18_av_write(struct cx18 *cx, u16 addr, u8 value)
{
	u32 x = readl(cx->reg_mem + 0xc40000 + (addr & ~3));
	u32 mask = 0xff;
	int shift = (addr & 3) * 8;

	x = (x & ~(mask << shift)) | ((u32)value << shift);
	writel(x, cx->reg_mem + 0xc40000 + (addr & ~3));
	return 0;
}

int cx18_av_write4(struct cx18 *cx, u16 addr, u32 value)
{
	writel(value, cx->reg_mem + 0xc40000 + addr);
	return 0;
}

u8 cx18_av_read(struct cx18 *cx, u16 addr)
{
	u32 x = readl(cx->reg_mem + 0xc40000 + (addr & ~3));
	int shift = (addr & 3) * 8;

	return (x >> shift) & 0xff;
}

u32 cx18_av_read4(struct cx18 *cx, u16 addr)
{
	return readl(cx->reg_mem + 0xc40000 + addr);
}

int cx18_av_and_or(struct cx18 *cx, u16 addr, unsigned and_mask,
		   u8 or_value)
{
	return cx18_av_write(cx, addr,
			     (cx18_av_read(cx, addr) & and_mask) |
			     or_value);
}

int cx18_av_and_or4(struct cx18 *cx, u16 addr, u32 and_mask,
		   u32 or_value)
{
	return cx18_av_write4(cx, addr,
			     (cx18_av_read4(cx, addr) & and_mask) |
			     or_value);
}

int cx18_av_write_no_acfg(struct cx18 *cx, u16 addr, u8 value, int no_acfg_mask)
{
	int retval;
	u32 saved_reg[8] = {0};

	if (no_acfg_mask & CXADEC_NO_ACFG_AFE) {
		saved_reg[0] = cx18_av_read4(cx, CXADEC_CHIP_CTRL);
		saved_reg[1] = cx18_av_read4(cx, CXADEC_AFE_CTRL);
	}

	if (no_acfg_mask & CXADEC_NO_ACFG_PLL) {
		saved_reg[2] = cx18_av_read4(cx, CXADEC_PLL_CTRL1);
		saved_reg[3] = cx18_av_read4(cx, CXADEC_VID_PLL_FRAC);
	}

	if (no_acfg_mask & CXADEC_NO_ACFG_VID) {
		saved_reg[4] = cx18_av_read4(cx, CXADEC_HORIZ_TIM_CTRL);
		saved_reg[5] = cx18_av_read4(cx, CXADEC_VERT_TIM_CTRL);
		saved_reg[6] = cx18_av_read4(cx, CXADEC_SRC_COMB_CFG);
		saved_reg[7] = cx18_av_read4(cx, CXADEC_CHROMA_VBIOFF_CFG);
	}

	retval = cx18_av_write(cx, addr, value);

	if (no_acfg_mask & CXADEC_NO_ACFG_AFE) {
		cx18_av_write4(cx, CXADEC_CHIP_CTRL, saved_reg[0]);
		cx18_av_write4(cx, CXADEC_AFE_CTRL,  saved_reg[1]);
	}

	if (no_acfg_mask & CXADEC_NO_ACFG_PLL) {
		cx18_av_write4(cx, CXADEC_PLL_CTRL1,    saved_reg[2]);
		cx18_av_write4(cx, CXADEC_VID_PLL_FRAC, saved_reg[3]);
	}

	if (no_acfg_mask & CXADEC_NO_ACFG_VID) {
		cx18_av_write4(cx, CXADEC_HORIZ_TIM_CTRL,    saved_reg[4]);
		cx18_av_write4(cx, CXADEC_VERT_TIM_CTRL,     saved_reg[5]);
		cx18_av_write4(cx, CXADEC_SRC_COMB_CFG,      saved_reg[6]);
		cx18_av_write4(cx, CXADEC_CHROMA_VBIOFF_CFG, saved_reg[7]);
	}

	return retval;
}

int cx18_av_and_or_no_acfg(struct cx18 *cx, u16 addr, unsigned and_mask,
			   u8 or_value, int no_acfg_mask)
{
	return cx18_av_write_no_acfg(cx, addr,
				     (cx18_av_read(cx, addr) & and_mask) |
				     or_value, no_acfg_mask);
}

/* ----------------------------------------------------------------------- */

static int set_input(struct cx18 *cx, enum cx18_av_video_input vid_input,
					enum cx18_av_audio_input aud_input);
static void log_audio_status(struct cx18 *cx);
static void log_video_status(struct cx18 *cx);

/* ----------------------------------------------------------------------- */

static void cx18_av_initialize(struct cx18 *cx)
{
	u32 v;

	cx18_av_loadfw(cx);
	/* Stop 8051 code execution */
	cx18_av_write4(cx, CXADEC_DL_CTL, 0x03000000);

	/* initallize the PLL by toggling sleep bit */
	v = cx18_av_read4(cx, CXADEC_HOST_REG1);
	/* enable sleep mode */
	cx18_av_write4(cx, CXADEC_HOST_REG1, v | 1);
	/* disable sleep mode */
	cx18_av_write4(cx, CXADEC_HOST_REG1, v & 0xfffe);

	/* initialize DLLs */
	v = cx18_av_read4(cx, CXADEC_DLL1_DIAG_CTRL) & 0xE1FFFEFF;
	/* disable FLD */
	cx18_av_write4(cx, CXADEC_DLL1_DIAG_CTRL, v);
	/* enable FLD */
	cx18_av_write4(cx, CXADEC_DLL1_DIAG_CTRL, v | 0x10000100);

	v = cx18_av_read4(cx, CXADEC_DLL2_DIAG_CTRL) & 0xE1FFFEFF;
	/* disable FLD */
	cx18_av_write4(cx, CXADEC_DLL2_DIAG_CTRL, v);
	/* enable FLD */
	cx18_av_write4(cx, CXADEC_DLL2_DIAG_CTRL, v | 0x06000100);

	/* set analog bias currents. Set Vreg to 1.20V. */
	cx18_av_write4(cx, CXADEC_AFE_DIAG_CTRL1, 0x000A1802);

	v = cx18_av_read4(cx, CXADEC_AFE_DIAG_CTRL3) | 1;
	/* enable TUNE_FIL_RST */
	cx18_av_write4(cx, CXADEC_AFE_DIAG_CTRL3, v);
	/* disable TUNE_FIL_RST */
	cx18_av_write4(cx, CXADEC_AFE_DIAG_CTRL3, v & 0xFFFFFFFE);

	/* enable 656 output */
	cx18_av_and_or4(cx, CXADEC_PIN_CTRL1, ~0, 0x040C00);

	/* video output drive strength */
	cx18_av_and_or4(cx, CXADEC_PIN_CTRL2, ~0, 0x2);

	/* reset video */
	cx18_av_write4(cx, CXADEC_SOFT_RST_CTRL, 0x8000);
	cx18_av_write4(cx, CXADEC_SOFT_RST_CTRL, 0);

	/* set video to auto-detect */
	/* Clear bits 11-12 to enable slow locking mode.  Set autodetect mode */
	/* set the comb notch = 1 */
	cx18_av_and_or4(cx, CXADEC_MODE_CTRL, 0xFFF7E7F0, 0x02040800);

	/* Enable wtw_en in CRUSH_CTRL (Set bit 22) */
	/* Enable maj_sel in CRUSH_CTRL (Set bit 20) */
	cx18_av_and_or4(cx, CXADEC_CRUSH_CTRL, ~0, 0x00500000);

	/* Set VGA_TRACK_RANGE to 0x20 */
	cx18_av_and_or4(cx, CXADEC_DFE_CTRL2, 0xFFFF00FF, 0x00002000);

	/* Enable VBI capture */
	cx18_av_write4(cx, CXADEC_OUT_CTRL1, 0x4010253F);
	/* cx18_av_write4(cx, CXADEC_OUT_CTRL1, 0x4010253E); */

	/* Set the video input.
	   The setting in MODE_CTRL gets lost when we do the above setup */
	/* EncSetSignalStd(dwDevNum, pEnc->dwSigStd); */
	/* EncSetVideoInput(dwDevNum, pEnc->VidIndSelection); */

	v = cx18_av_read4(cx, CXADEC_AFE_CTRL);
	v &= 0xFFFBFFFF;            /* turn OFF bit 18 for droop_comp_ch1 */
	v &= 0xFFFF7FFF;            /* turn OFF bit 9 for clamp_sel_ch1 */
	v &= 0xFFFFFFFE;            /* turn OFF bit 0 for 12db_ch1 */
	/* v |= 0x00000001;*/            /* turn ON bit 0 for 12db_ch1 */
	cx18_av_write4(cx, CXADEC_AFE_CTRL, v);

/* 	if(dwEnable && dw3DCombAvailable) { */
/*      	CxDevWrReg(CXADEC_SRC_COMB_CFG, 0x7728021F); */
/*    } else { */
/*      	CxDevWrReg(CXADEC_SRC_COMB_CFG, 0x6628021F); */
/*    } */
	cx18_av_write4(cx, CXADEC_SRC_COMB_CFG, 0x6628021F);
}

/* ----------------------------------------------------------------------- */

static void input_change(struct cx18 *cx)
{
	struct cx18_av_state *state = &cx->av_state;
	v4l2_std_id std = state->std;

	/* Follow step 8c and 8d of section 3.16 in the cx18_av datasheet */
	if (std & V4L2_STD_SECAM)
		cx18_av_write_no_acfg(cx, 0x402, 0, CXADEC_NO_ACFG_ALL);
	else {
		cx18_av_write_no_acfg(cx, 0x402, 0x04, CXADEC_NO_ACFG_ALL);
		cx18_av_write(cx, 0x49f, (std & V4L2_STD_NTSC) ? 0x14 : 0x11);
	}
	cx18_av_and_or_no_acfg(cx, 0x401, ~0x60, 0,
				CXADEC_NO_ACFG_PLL | CXADEC_NO_ACFG_VID);
	cx18_av_and_or_no_acfg(cx, 0x401, ~0x60, 0x60,
				CXADEC_NO_ACFG_PLL | CXADEC_NO_ACFG_VID);

	if (std & V4L2_STD_525_60) {
		if (std == V4L2_STD_NTSC_M_JP) {
			/* Japan uses EIAJ audio standard */
			cx18_av_write(cx, 0x808, 0xf7);
			cx18_av_write(cx, 0x80b, 0x02);
		} else if (std == V4L2_STD_NTSC_M_KR) {
			/* South Korea uses A2 audio standard */
			cx18_av_write(cx, 0x808, 0xf8);
			cx18_av_write(cx, 0x80b, 0x03);
		} else {
			/* Others use the BTSC audio standard */
			cx18_av_write(cx, 0x808, 0xf6);
			cx18_av_write(cx, 0x80b, 0x01);
		}
	} else if (std & V4L2_STD_PAL) {
		/* Follow tuner change procedure for PAL */
		cx18_av_write(cx, 0x808, 0xff);
		cx18_av_write(cx, 0x80b, 0x03);
	} else if (std & V4L2_STD_SECAM) {
		/* Select autodetect for SECAM */
		cx18_av_write(cx, 0x808, 0xff);
		cx18_av_write(cx, 0x80b, 0x03);
	}

	if (cx18_av_read(cx, 0x803) & 0x10) {
		/* restart audio decoder microcontroller */
		cx18_av_and_or(cx, 0x803, ~0x10, 0x00);
		cx18_av_and_or(cx, 0x803, ~0x10, 0x10);
	}
}

static int set_input(struct cx18 *cx, enum cx18_av_video_input vid_input,
					enum cx18_av_audio_input aud_input)
{
	struct cx18_av_state *state = &cx->av_state;
	u8 is_composite = (vid_input >= CX18_AV_COMPOSITE1 &&
			   vid_input <= CX18_AV_COMPOSITE8);
	u8 reg;

	CX18_DEBUG_INFO("decoder set video input %d, audio input %d\n",
			vid_input, aud_input);

	if (is_composite) {
		reg = 0xf0 + (vid_input - CX18_AV_COMPOSITE1);
	} else {
		int luma = vid_input & 0xf0;
		int chroma = vid_input & 0xf00;

		if ((vid_input & ~0xff0) ||
		    luma < CX18_AV_SVIDEO_LUMA1 ||
		    luma > CX18_AV_SVIDEO_LUMA8 ||
		    chroma < CX18_AV_SVIDEO_CHROMA4 ||
		    chroma > CX18_AV_SVIDEO_CHROMA8) {
			CX18_ERR("0x%04x is not a valid video input!\n",
					vid_input);
			return -EINVAL;
		}
		reg = 0xf0 + ((luma - CX18_AV_SVIDEO_LUMA1) >> 4);
		if (chroma >= CX18_AV_SVIDEO_CHROMA7) {
			reg &= 0x3f;
			reg |= (chroma - CX18_AV_SVIDEO_CHROMA7) >> 2;
		} else {
			reg &= 0xcf;
			reg |= (chroma - CX18_AV_SVIDEO_CHROMA4) >> 4;
		}
	}

	switch (aud_input) {
	case CX18_AV_AUDIO_SERIAL:
		/* do nothing, use serial audio input */
		break;
	case CX18_AV_AUDIO4: reg &= ~0x30; break;
	case CX18_AV_AUDIO5: reg &= ~0x30; reg |= 0x10; break;
	case CX18_AV_AUDIO6: reg &= ~0x30; reg |= 0x20; break;
	case CX18_AV_AUDIO7: reg &= ~0xc0; break;
	case CX18_AV_AUDIO8: reg &= ~0xc0; reg |= 0x40; break;

	default:
		CX18_ERR("0x%04x is not a valid audio input!\n", aud_input);
		return -EINVAL;
	}

	cx18_av_write(cx, 0x103, reg);
	/* Set INPUT_MODE to Composite (0) or S-Video (1) */
	cx18_av_and_or_no_acfg(cx, 0x401, ~0x6, is_composite ? 0 : 0x02,
				CXADEC_NO_ACFG_PLL | CXADEC_NO_ACFG_VID);
	/* Set CH_SEL_ADC2 to 1 if input comes from CH3 */
	cx18_av_and_or(cx, 0x102, ~0x2, (reg & 0x80) == 0 ? 2 : 0);
	/* Set DUAL_MODE_ADC2 to 1 if input comes from both CH2 and CH3 */
	if ((reg & 0xc0) != 0xc0 && (reg & 0x30) != 0x30)
		cx18_av_and_or(cx, 0x102, ~0x4, 4);
	else
		cx18_av_and_or(cx, 0x102, ~0x4, 0);
	/*cx18_av_and_or4(cx, 0x104, ~0x001b4180, 0x00004180);*/

	state->vid_input = vid_input;
	state->aud_input = aud_input;
	cx18_av_audio_set_path(cx);
	input_change(cx);
	return 0;
}

/* ----------------------------------------------------------------------- */

static int set_v4lstd(struct cx18 *cx)
{
	struct cx18_av_state *state = &cx->av_state;
	u8 fmt = 0; 	/* zero is autodetect */
	u8 pal_m = 0;

	/* First tests should be against specific std */
	if (state->std == V4L2_STD_NTSC_M_JP) {
		fmt = 0x2;
	} else if (state->std == V4L2_STD_NTSC_443) {
		fmt = 0x3;
	} else if (state->std == V4L2_STD_PAL_M) {
		pal_m = 1;
		fmt = 0x5;
	} else if (state->std == V4L2_STD_PAL_N) {
		fmt = 0x6;
	} else if (state->std == V4L2_STD_PAL_Nc) {
		fmt = 0x7;
	} else if (state->std == V4L2_STD_PAL_60) {
		fmt = 0x8;
	} else {
		/* Then, test against generic ones */
		if (state->std & V4L2_STD_NTSC)
			fmt = 0x1;
		else if (state->std & V4L2_STD_PAL)
			fmt = 0x4;
		else if (state->std & V4L2_STD_SECAM)
			fmt = 0xc;
	}

	CX18_DEBUG_INFO("changing video std to fmt %i\n", fmt);

	/* Follow step 9 of section 3.16 in the cx18_av datasheet.
	   Without this PAL may display a vertical ghosting effect.
	   This happens for example with the Yuan MPC622. */
	if (fmt >= 4 && fmt < 8) {
		/* Set format to NTSC-M */
		cx18_av_and_or_no_acfg(cx, 0x400, ~0xf, 1, CXADEC_NO_ACFG_AFE);
		/* Turn off LCOMB */
		cx18_av_and_or(cx, 0x47b, ~6, 0);
	}
	cx18_av_and_or_no_acfg(cx, 0x400, ~0xf, fmt, CXADEC_NO_ACFG_AFE);
	cx18_av_and_or_no_acfg(cx, 0x403, ~0x3, pal_m, CXADEC_NO_ACFG_ALL);
	cx18_av_vbi_setup(cx);
	input_change(cx);
	return 0;
}

/* ----------------------------------------------------------------------- */

static int set_v4lctrl(struct cx18 *cx, struct v4l2_control *ctrl)
{
	switch (ctrl->id) {
	case V4L2_CID_BRIGHTNESS:
		if (ctrl->value < 0 || ctrl->value > 255) {
			CX18_ERR("invalid brightness setting %d\n",
				    ctrl->value);
			return -ERANGE;
		}

		cx18_av_write(cx, 0x414, ctrl->value - 128);
		break;

	case V4L2_CID_CONTRAST:
		if (ctrl->value < 0 || ctrl->value > 127) {
			CX18_ERR("invalid contrast setting %d\n",
				    ctrl->value);
			return -ERANGE;
		}

		cx18_av_write(cx, 0x415, ctrl->value << 1);
		break;

	case V4L2_CID_SATURATION:
		if (ctrl->value < 0 || ctrl->value > 127) {
			CX18_ERR("invalid saturation setting %d\n",
				    ctrl->value);
			return -ERANGE;
		}

		cx18_av_write(cx, 0x420, ctrl->value << 1);
		cx18_av_write(cx, 0x421, ctrl->value << 1);
		break;

	case V4L2_CID_HUE:
		if (ctrl->value < -127 || ctrl->value > 127) {
			CX18_ERR("invalid hue setting %d\n", ctrl->value);
			return -ERANGE;
		}

		cx18_av_write(cx, 0x422, ctrl->value);
		break;

	case V4L2_CID_AUDIO_VOLUME:
	case V4L2_CID_AUDIO_BASS:
	case V4L2_CID_AUDIO_TREBLE:
	case V4L2_CID_AUDIO_BALANCE:
	case V4L2_CID_AUDIO_MUTE:
		return cx18_av_audio(cx, VIDIOC_S_CTRL, ctrl);

	default:
		return -EINVAL;
	}

	return 0;
}

static int get_v4lctrl(struct cx18 *cx, struct v4l2_control *ctrl)
{
	switch (ctrl->id) {
	case V4L2_CID_BRIGHTNESS:
		ctrl->value = (s8)cx18_av_read(cx, 0x414) + 128;
		break;
	case V4L2_CID_CONTRAST:
		ctrl->value = cx18_av_read(cx, 0x415) >> 1;
		break;
	case V4L2_CID_SATURATION:
		ctrl->value = cx18_av_read(cx, 0x420) >> 1;
		break;
	case V4L2_CID_HUE:
		ctrl->value = (s8)cx18_av_read(cx, 0x422);
		break;
	case V4L2_CID_AUDIO_VOLUME:
	case V4L2_CID_AUDIO_BASS:
	case V4L2_CID_AUDIO_TREBLE:
	case V4L2_CID_AUDIO_BALANCE:
	case V4L2_CID_AUDIO_MUTE:
		return cx18_av_audio(cx, VIDIOC_G_CTRL, ctrl);
	default:
		return -EINVAL;
	}

	return 0;
}

/* ----------------------------------------------------------------------- */

static int get_v4lfmt(struct cx18 *cx, struct v4l2_format *fmt)
{
	switch (fmt->type) {
	case V4L2_BUF_TYPE_SLICED_VBI_CAPTURE:
		return cx18_av_vbi(cx, VIDIOC_G_FMT, fmt);
	default:
		return -EINVAL;
	}

	return 0;
}

static int set_v4lfmt(struct cx18 *cx, struct v4l2_format *fmt)
{
	struct cx18_av_state *state = &cx->av_state;
	struct v4l2_pix_format *pix;
	int HSC, VSC, Vsrc, Hsrc, filter, Vlines;
	int is_50Hz = !(state->std & V4L2_STD_525_60);

	switch (fmt->type) {
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
		pix = &(fmt->fmt.pix);

		Vsrc = (cx18_av_read(cx, 0x476) & 0x3f) << 4;
		Vsrc |= (cx18_av_read(cx, 0x475) & 0xf0) >> 4;

		Hsrc = (cx18_av_read(cx, 0x472) & 0x3f) << 4;
		Hsrc |= (cx18_av_read(cx, 0x471) & 0xf0) >> 4;

		Vlines = pix->height + (is_50Hz ? 4 : 7);

		if ((pix->width * 16 < Hsrc) || (Hsrc < pix->width) ||
		    (Vlines * 8 < Vsrc) || (Vsrc < Vlines)) {
			CX18_ERR("%dx%d is not a valid size!\n",
				    pix->width, pix->height);
			return -ERANGE;
		}

		HSC = (Hsrc * (1 << 20)) / pix->width - (1 << 20);
		VSC = (1 << 16) - (Vsrc * (1 << 9) / Vlines - (1 << 9));
		VSC &= 0x1fff;

		if (pix->width >= 385)
			filter = 0;
		else if (pix->width > 192)
			filter = 1;
		else if (pix->width > 96)
			filter = 2;
		else
			filter = 3;

		CX18_DEBUG_INFO("decoder set size %dx%d -> scale  %ux%u\n",
			    pix->width, pix->height, HSC, VSC);

		/* HSCALE=HSC */
		cx18_av_write(cx, 0x418, HSC & 0xff);
		cx18_av_write(cx, 0x419, (HSC >> 8) & 0xff);
		cx18_av_write(cx, 0x41a, HSC >> 16);
		/* VSCALE=VSC */
		cx18_av_write(cx, 0x41c, VSC & 0xff);
		cx18_av_write(cx, 0x41d, VSC >> 8);
		/* VS_INTRLACE=1 VFILT=filter */
		cx18_av_write(cx, 0x41e, 0x8 | filter);
		break;

	case V4L2_BUF_TYPE_SLICED_VBI_CAPTURE:
		return cx18_av_vbi(cx, VIDIOC_S_FMT, fmt);

	case V4L2_BUF_TYPE_VBI_CAPTURE:
		return cx18_av_vbi(cx, VIDIOC_S_FMT, fmt);

	default:
		return -EINVAL;
	}

	return 0;
}

/* ----------------------------------------------------------------------- */

int cx18_av_cmd(struct cx18 *cx, unsigned int cmd, void *arg)
{
	struct cx18_av_state *state = &cx->av_state;
	struct v4l2_tuner *vt = arg;
	struct v4l2_routing *route = arg;

	/* ignore these commands */
	switch (cmd) {
	case TUNER_SET_TYPE_ADDR:
		return 0;
	}

	if (!state->is_initialized) {
		CX18_DEBUG_INFO("cmd %08x triggered fw load\n", cmd);
		/* initialize on first use */
		state->is_initialized = 1;
		cx18_av_initialize(cx);
	}

	switch (cmd) {
	case VIDIOC_INT_DECODE_VBI_LINE:
		return cx18_av_vbi(cx, cmd, arg);

	case VIDIOC_INT_AUDIO_CLOCK_FREQ:
		return cx18_av_audio(cx, cmd, arg);

	case VIDIOC_STREAMON:
		CX18_DEBUG_INFO("enable output\n");
		cx18_av_write(cx, 0x115, 0x8c);
		cx18_av_write(cx, 0x116, 0x07);
		break;

	case VIDIOC_STREAMOFF:
		CX18_DEBUG_INFO("disable output\n");
		cx18_av_write(cx, 0x115, 0x00);
		cx18_av_write(cx, 0x116, 0x00);
		break;

	case VIDIOC_LOG_STATUS:
		log_video_status(cx);
		log_audio_status(cx);
		break;

	case VIDIOC_G_CTRL:
		return get_v4lctrl(cx, (struct v4l2_control *)arg);

	case VIDIOC_S_CTRL:
		return set_v4lctrl(cx, (struct v4l2_control *)arg);

	case VIDIOC_QUERYCTRL:
	{
		struct v4l2_queryctrl *qc = arg;

		switch (qc->id) {
		case V4L2_CID_BRIGHTNESS:
		case V4L2_CID_CONTRAST:
		case V4L2_CID_SATURATION:
		case V4L2_CID_HUE:
			return v4l2_ctrl_query_fill_std(qc);
		default:
			break;
		}

		switch (qc->id) {
		case V4L2_CID_AUDIO_VOLUME:
		case V4L2_CID_AUDIO_MUTE:
		case V4L2_CID_AUDIO_BALANCE:
		case V4L2_CID_AUDIO_BASS:
		case V4L2_CID_AUDIO_TREBLE:
			return v4l2_ctrl_query_fill_std(qc);
		default:
			return -EINVAL;
		}
		return -EINVAL;
	}

	case VIDIOC_G_STD:
		*(v4l2_std_id *)arg = state->std;
		break;

	case VIDIOC_S_STD:
		if (state->radio == 0 && state->std == *(v4l2_std_id *)arg)
			return 0;
		state->radio = 0;
		state->std = *(v4l2_std_id *)arg;
		return set_v4lstd(cx);

	case AUDC_SET_RADIO:
		state->radio = 1;
		break;

	case VIDIOC_INT_G_VIDEO_ROUTING:
		route->input = state->vid_input;
		route->output = 0;
		break;

	case VIDIOC_INT_S_VIDEO_ROUTING:
		return set_input(cx, route->input, state->aud_input);

	case VIDIOC_INT_G_AUDIO_ROUTING:
		route->input = state->aud_input;
		route->output = 0;
		break;

	case VIDIOC_INT_S_AUDIO_ROUTING:
		return set_input(cx, state->vid_input, route->input);

	case VIDIOC_S_FREQUENCY:
		input_change(cx);
		break;

	case VIDIOC_G_TUNER:
	{
		u8 vpres = cx18_av_read(cx, 0x40e) & 0x20;
		u8 mode;
		int val = 0;

		if (state->radio)
			break;

		vt->signal = vpres ? 0xffff : 0x0;

		vt->capability |=
		    V4L2_TUNER_CAP_STEREO | V4L2_TUNER_CAP_LANG1 |
		    V4L2_TUNER_CAP_LANG2 | V4L2_TUNER_CAP_SAP;

		mode = cx18_av_read(cx, 0x804);

		/* get rxsubchans and audmode */
		if ((mode & 0xf) == 1)
			val |= V4L2_TUNER_SUB_STEREO;
		else
			val |= V4L2_TUNER_SUB_MONO;

		if (mode == 2 || mode == 4)
			val = V4L2_TUNER_SUB_LANG1 | V4L2_TUNER_SUB_LANG2;

		if (mode & 0x10)
			val |= V4L2_TUNER_SUB_SAP;

		vt->rxsubchans = val;
		vt->audmode = state->audmode;
		break;
	}

	case VIDIOC_S_TUNER:
		if (state->radio)
			break;

		switch (vt->audmode) {
		case V4L2_TUNER_MODE_MONO:
			/* mono      -> mono
			   stereo    -> mono
			   bilingual -> lang1 */
			cx18_av_and_or(cx, 0x809, ~0xf, 0x00);
			break;
		case V4L2_TUNER_MODE_STEREO:
		case V4L2_TUNER_MODE_LANG1:
			/* mono      -> mono
			   stereo    -> stereo
			   bilingual -> lang1 */
			cx18_av_and_or(cx, 0x809, ~0xf, 0x04);
			break;
		case V4L2_TUNER_MODE_LANG1_LANG2:
			/* mono      -> mono
			   stereo    -> stereo
			   bilingual -> lang1/lang2 */
			cx18_av_and_or(cx, 0x809, ~0xf, 0x07);
			break;
		case V4L2_TUNER_MODE_LANG2:
			/* mono      -> mono
			   stereo    -> stereo
			   bilingual -> lang2 */
			cx18_av_and_or(cx, 0x809, ~0xf, 0x01);
			break;
		default:
			return -EINVAL;
		}
		state->audmode = vt->audmode;
		break;

	case VIDIOC_G_FMT:
		return get_v4lfmt(cx, (struct v4l2_format *)arg);

	case VIDIOC_S_FMT:
		return set_v4lfmt(cx, (struct v4l2_format *)arg);

	case VIDIOC_INT_RESET:
		cx18_av_initialize(cx);
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

/* ----------------------------------------------------------------------- */

/* ----------------------------------------------------------------------- */

static void log_video_status(struct cx18 *cx)
{
	static const char *const fmt_strs[] = {
		"0x0",
		"NTSC-M", "NTSC-J", "NTSC-4.43",
		"PAL-BDGHI", "PAL-M", "PAL-N", "PAL-Nc", "PAL-60",
		"0x9", "0xA", "0xB",
		"SECAM",
		"0xD", "0xE", "0xF"
	};

	struct cx18_av_state *state = &cx->av_state;
	u8 vidfmt_sel = cx18_av_read(cx, 0x400) & 0xf;
	u8 gen_stat1 = cx18_av_read(cx, 0x40d);
	u8 gen_stat2 = cx18_av_read(cx, 0x40e);
	int vid_input = state->vid_input;

	CX18_INFO("Video signal:              %spresent\n",
		    (gen_stat2 & 0x20) ? "" : "not ");
	CX18_INFO("Detected format:           %s\n",
		    fmt_strs[gen_stat1 & 0xf]);

	CX18_INFO("Specified standard:        %s\n",
		    vidfmt_sel ? fmt_strs[vidfmt_sel] : "automatic detection");

	if (vid_input >= CX18_AV_COMPOSITE1 &&
	    vid_input <= CX18_AV_COMPOSITE8) {
		CX18_INFO("Specified video input:     Composite %d\n",
			vid_input - CX18_AV_COMPOSITE1 + 1);
	} else {
		CX18_INFO("Specified video input:     S-Video (Luma In%d, Chroma In%d)\n",
			(vid_input & 0xf0) >> 4, (vid_input & 0xf00) >> 8);
	}

	CX18_INFO("Specified audioclock freq: %d Hz\n", state->audclk_freq);
}

/* ----------------------------------------------------------------------- */

static void log_audio_status(struct cx18 *cx)
{
	struct cx18_av_state *state = &cx->av_state;
	u8 download_ctl = cx18_av_read(cx, 0x803);
	u8 mod_det_stat0 = cx18_av_read(cx, 0x804);
	u8 mod_det_stat1 = cx18_av_read(cx, 0x805);
	u8 audio_config = cx18_av_read(cx, 0x808);
	u8 pref_mode = cx18_av_read(cx, 0x809);
	u8 afc0 = cx18_av_read(cx, 0x80b);
	u8 mute_ctl = cx18_av_read(cx, 0x8d3);
	int aud_input = state->aud_input;
	char *p;

	switch (mod_det_stat0) {
	case 0x00: p = "mono"; break;
	case 0x01: p = "stereo"; break;
	case 0x02: p = "dual"; break;
	case 0x04: p = "tri"; break;
	case 0x10: p = "mono with SAP"; break;
	case 0x11: p = "stereo with SAP"; break;
	case 0x12: p = "dual with SAP"; break;
	case 0x14: p = "tri with SAP"; break;
	case 0xfe: p = "forced mode"; break;
	default: p = "not defined"; break;
	}
	CX18_INFO("Detected audio mode:       %s\n", p);

	switch (mod_det_stat1) {
	case 0x00: p = "not defined"; break;
	case 0x01: p = "EIAJ"; break;
	case 0x02: p = "A2-M"; break;
	case 0x03: p = "A2-BG"; break;
	case 0x04: p = "A2-DK1"; break;
	case 0x05: p = "A2-DK2"; break;
	case 0x06: p = "A2-DK3"; break;
	case 0x07: p = "A1 (6.0 MHz FM Mono)"; break;
	case 0x08: p = "AM-L"; break;
	case 0x09: p = "NICAM-BG"; break;
	case 0x0a: p = "NICAM-DK"; break;
	case 0x0b: p = "NICAM-I"; break;
	case 0x0c: p = "NICAM-L"; break;
	case 0x0d: p = "BTSC/EIAJ/A2-M Mono (4.5 MHz FMMono)"; break;
	case 0x0e: p = "IF FM Radio"; break;
	case 0x0f: p = "BTSC"; break;
	case 0x10: p = "detected chrominance"; break;
	case 0xfd: p = "unknown audio standard"; break;
	case 0xfe: p = "forced audio standard"; break;
	case 0xff: p = "no detected audio standard"; break;
	default: p = "not defined"; break;
	}
	CX18_INFO("Detected audio standard:   %s\n", p);
	CX18_INFO("Audio muted:               %s\n",
		    (mute_ctl & 0x2) ? "yes" : "no");
	CX18_INFO("Audio microcontroller:     %s\n",
		    (download_ctl & 0x10) ? "running" : "stopped");

	switch (audio_config >> 4) {
	case 0x00: p = "undefined"; break;
	case 0x01: p = "BTSC"; break;
	case 0x02: p = "EIAJ"; break;
	case 0x03: p = "A2-M"; break;
	case 0x04: p = "A2-BG"; break;
	case 0x05: p = "A2-DK1"; break;
	case 0x06: p = "A2-DK2"; break;
	case 0x07: p = "A2-DK3"; break;
	case 0x08: p = "A1 (6.0 MHz FM Mono)"; break;
	case 0x09: p = "AM-L"; break;
	case 0x0a: p = "NICAM-BG"; break;
	case 0x0b: p = "NICAM-DK"; break;
	case 0x0c: p = "NICAM-I"; break;
	case 0x0d: p = "NICAM-L"; break;
	case 0x0e: p = "FM radio"; break;
	case 0x0f: p = "automatic detection"; break;
	default: p = "undefined"; break;
	}
	CX18_INFO("Configured audio standard: %s\n", p);

	if ((audio_config >> 4) < 0xF) {
		switch (audio_config & 0xF) {
		case 0x00: p = "MONO1 (LANGUAGE A/Mono L+R channel for BTSC, EIAJ, A2)"; break;
		case 0x01: p = "MONO2 (LANGUAGE B)"; break;
		case 0x02: p = "MONO3 (STEREO forced MONO)"; break;
		case 0x03: p = "MONO4 (NICAM ANALOG-Language C/Analog Fallback)"; break;
		case 0x04: p = "STEREO"; break;
		case 0x05: p = "DUAL1 (AC)"; break;
		case 0x06: p = "DUAL2 (BC)"; break;
		case 0x07: p = "DUAL3 (AB)"; break;
		default: p = "undefined";
		}
		CX18_INFO("Configured audio mode:     %s\n", p);
	} else {
		switch (audio_config & 0xF) {
		case 0x00: p = "BG"; break;
		case 0x01: p = "DK1"; break;
		case 0x02: p = "DK2"; break;
		case 0x03: p = "DK3"; break;
		case 0x04: p = "I"; break;
		case 0x05: p = "L"; break;
		case 0x06: p = "BTSC"; break;
		case 0x07: p = "EIAJ"; break;
		case 0x08: p = "A2-M"; break;
		case 0x09: p = "FM Radio (4.5 MHz)"; break;
		case 0x0a: p = "FM Radio (5.5 MHz)"; break;
		case 0x0b: p = "S-Video"; break;
		case 0x0f: p = "automatic standard and mode detection"; break;
		default: p = "undefined"; break;
		}
		CX18_INFO("Configured audio system:   %s\n", p);
	}

	if (aud_input)
		CX18_INFO("Specified audio input:     Tuner (In%d)\n",
				aud_input);
	else
		CX18_INFO("Specified audio input:     External\n");

	switch (pref_mode & 0xf) {
	case 0: p = "mono/language A"; break;
	case 1: p = "language B"; break;
	case 2: p = "language C"; break;
	case 3: p = "analog fallback"; break;
	case 4: p = "stereo"; break;
	case 5: p = "language AC"; break;
	case 6: p = "language BC"; break;
	case 7: p = "language AB"; break;
	default: p = "undefined"; break;
	}
	CX18_INFO("Preferred audio mode:      %s\n", p);

	if ((audio_config & 0xf) == 0xf) {
		switch ((afc0 >> 3) & 0x1) {
		case 0: p = "system DK"; break;
		case 1: p = "system L"; break;
		}
		CX18_INFO("Selected 65 MHz format:    %s\n", p);

		switch (afc0 & 0x7) {
		case 0: p = "Chroma"; break;
		case 1: p = "BTSC"; break;
		case 2: p = "EIAJ"; break;
		case 3: p = "A2-M"; break;
		case 4: p = "autodetect"; break;
		default: p = "undefined"; break;
		}
		CX18_INFO("Selected 45 MHz format:    %s\n", p);
	}
}
