/*
 * Auvitek AU8522 QAM/8VSB demodulator driver and video decoder
 *
 * Copyright (C) 2009 Devin Heitmueller <dheitmueller@linuxtv.org>
 * Copyright (C) 2005-2008 Auvitek International, Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * As published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

/* Developer notes:
 *
 * VBI support is not yet working
 * Enough is implemented here for CVBS and S-Video inputs, but the actual
 *  analog demodulator code isn't implemented (not needed for xc5000 since it
 *  has its own demodulator and outputs CVBS)
 *
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/videodev2.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <media/v4l2-common.h>
#include <media/v4l2-chip-ident.h>
#include <media/v4l2-i2c-drv.h>
#include <media/v4l2-device.h>
#include "au8522.h"
#include "au8522_priv.h"

MODULE_AUTHOR("Devin Heitmueller");
MODULE_LICENSE("GPL");

static int au8522_analog_debug;


module_param_named(analog_debug, au8522_analog_debug, int, 0644);

MODULE_PARM_DESC(analog_debug,
		 "Analog debugging messages [0=Off (default) 1=On]");

struct au8522_register_config {
	u16 reg_name;
	u8 reg_val[8];
};


/* Video Decoder Filter Coefficients
   The values are as follows from left to right
   0="ATV RF" 1="ATV RF13" 2="CVBS" 3="S-Video" 4="PAL" 5=CVBS13" 6="SVideo13"
*/
static const struct au8522_register_config filter_coef[] = {
	{AU8522_FILTER_COEF_R410, {0x25, 0x00, 0x25, 0x25, 0x00, 0x00, 0x00} },
	{AU8522_FILTER_COEF_R411, {0x20, 0x00, 0x20, 0x20, 0x00, 0x00, 0x00} },
	{AU8522_FILTER_COEF_R412, {0x03, 0x00, 0x03, 0x03, 0x00, 0x00, 0x00} },
	{AU8522_FILTER_COEF_R413, {0xe6, 0x00, 0xe6, 0xe6, 0x00, 0x00, 0x00} },
	{AU8522_FILTER_COEF_R414, {0x40, 0x00, 0x40, 0x40, 0x00, 0x00, 0x00} },
	{AU8522_FILTER_COEF_R415, {0x1b, 0x00, 0x1b, 0x1b, 0x00, 0x00, 0x00} },
	{AU8522_FILTER_COEF_R416, {0xc0, 0x00, 0xc0, 0x04, 0x00, 0x00, 0x00} },
	{AU8522_FILTER_COEF_R417, {0x04, 0x00, 0x04, 0x04, 0x00, 0x00, 0x00} },
	{AU8522_FILTER_COEF_R418, {0x8c, 0x00, 0x8c, 0x8c, 0x00, 0x00, 0x00} },
	{AU8522_FILTER_COEF_R419, {0xa0, 0x40, 0xa0, 0xa0, 0x40, 0x40, 0x40} },
	{AU8522_FILTER_COEF_R41A, {0x21, 0x09, 0x21, 0x21, 0x09, 0x09, 0x09} },
	{AU8522_FILTER_COEF_R41B, {0x6c, 0x38, 0x6c, 0x6c, 0x38, 0x38, 0x38} },
	{AU8522_FILTER_COEF_R41C, {0x03, 0xff, 0x03, 0x03, 0xff, 0xff, 0xff} },
	{AU8522_FILTER_COEF_R41D, {0xbf, 0xc7, 0xbf, 0xbf, 0xc7, 0xc7, 0xc7} },
	{AU8522_FILTER_COEF_R41E, {0xa0, 0xdf, 0xa0, 0xa0, 0xdf, 0xdf, 0xdf} },
	{AU8522_FILTER_COEF_R41F, {0x10, 0x06, 0x10, 0x10, 0x06, 0x06, 0x06} },
	{AU8522_FILTER_COEF_R420, {0xae, 0x30, 0xae, 0xae, 0x30, 0x30, 0x30} },
	{AU8522_FILTER_COEF_R421, {0xc4, 0x01, 0xc4, 0xc4, 0x01, 0x01, 0x01} },
	{AU8522_FILTER_COEF_R422, {0x54, 0xdd, 0x54, 0x54, 0xdd, 0xdd, 0xdd} },
	{AU8522_FILTER_COEF_R423, {0xd0, 0xaf, 0xd0, 0xd0, 0xaf, 0xaf, 0xaf} },
	{AU8522_FILTER_COEF_R424, {0x1c, 0xf7, 0x1c, 0x1c, 0xf7, 0xf7, 0xf7} },
	{AU8522_FILTER_COEF_R425, {0x76, 0xdb, 0x76, 0x76, 0xdb, 0xdb, 0xdb} },
	{AU8522_FILTER_COEF_R426, {0x61, 0xc0, 0x61, 0x61, 0xc0, 0xc0, 0xc0} },
	{AU8522_FILTER_COEF_R427, {0xd1, 0x2f, 0xd1, 0xd1, 0x2f, 0x2f, 0x2f} },
	{AU8522_FILTER_COEF_R428, {0x84, 0xd8, 0x84, 0x84, 0xd8, 0xd8, 0xd8} },
	{AU8522_FILTER_COEF_R429, {0x06, 0xfb, 0x06, 0x06, 0xfb, 0xfb, 0xfb} },
	{AU8522_FILTER_COEF_R42A, {0x21, 0xd5, 0x21, 0x21, 0xd5, 0xd5, 0xd5} },
	{AU8522_FILTER_COEF_R42B, {0x0a, 0x3e, 0x0a, 0x0a, 0x3e, 0x3e, 0x3e} },
	{AU8522_FILTER_COEF_R42C, {0xe6, 0x15, 0xe6, 0xe6, 0x15, 0x15, 0x15} },
	{AU8522_FILTER_COEF_R42D, {0x01, 0x34, 0x01, 0x01, 0x34, 0x34, 0x34} },

};
#define NUM_FILTER_COEF (sizeof(filter_coef)\
			 / sizeof(struct au8522_register_config))


/* Registers 0x060b through 0x0652 are the LP Filter coefficients
   The values are as follows from left to right
   0="SIF" 1="ATVRF/ATVRF13"
   Note: the "ATVRF/ATVRF13" mode has never been tested
*/
static const struct au8522_register_config lpfilter_coef[] = {
	{0x060b, {0x21, 0x0b} },
	{0x060c, {0xad, 0xad} },
	{0x060d, {0x70, 0xf0} },
	{0x060e, {0xea, 0xe9} },
	{0x060f, {0xdd, 0xdd} },
	{0x0610, {0x08, 0x64} },
	{0x0611, {0x60, 0x60} },
	{0x0612, {0xf8, 0xb2} },
	{0x0613, {0x01, 0x02} },
	{0x0614, {0xe4, 0xb4} },
	{0x0615, {0x19, 0x02} },
	{0x0616, {0xae, 0x2e} },
	{0x0617, {0xee, 0xc5} },
	{0x0618, {0x56, 0x56} },
	{0x0619, {0x30, 0x58} },
	{0x061a, {0xf9, 0xf8} },
	{0x061b, {0x24, 0x64} },
	{0x061c, {0x07, 0x07} },
	{0x061d, {0x30, 0x30} },
	{0x061e, {0xa9, 0xed} },
	{0x061f, {0x09, 0x0b} },
	{0x0620, {0x42, 0xc2} },
	{0x0621, {0x1d, 0x2a} },
	{0x0622, {0xd6, 0x56} },
	{0x0623, {0x95, 0x8b} },
	{0x0624, {0x2b, 0x2b} },
	{0x0625, {0x30, 0x24} },
	{0x0626, {0x3e, 0x3e} },
	{0x0627, {0x62, 0xe2} },
	{0x0628, {0xe9, 0xf5} },
	{0x0629, {0x99, 0x19} },
	{0x062a, {0xd4, 0x11} },
	{0x062b, {0x03, 0x04} },
	{0x062c, {0xb5, 0x85} },
	{0x062d, {0x1e, 0x20} },
	{0x062e, {0x2a, 0xea} },
	{0x062f, {0xd7, 0xd2} },
	{0x0630, {0x15, 0x15} },
	{0x0631, {0xa3, 0xa9} },
	{0x0632, {0x1f, 0x1f} },
	{0x0633, {0xf9, 0xd1} },
	{0x0634, {0xc0, 0xc3} },
	{0x0635, {0x4d, 0x8d} },
	{0x0636, {0x21, 0x31} },
	{0x0637, {0x83, 0x83} },
	{0x0638, {0x08, 0x8c} },
	{0x0639, {0x19, 0x19} },
	{0x063a, {0x45, 0xa5} },
	{0x063b, {0xef, 0xec} },
	{0x063c, {0x8a, 0x8a} },
	{0x063d, {0xf4, 0xf6} },
	{0x063e, {0x8f, 0x8f} },
	{0x063f, {0x44, 0x0c} },
	{0x0640, {0xef, 0xf0} },
	{0x0641, {0x66, 0x66} },
	{0x0642, {0xcc, 0xd2} },
	{0x0643, {0x41, 0x41} },
	{0x0644, {0x63, 0x93} },
	{0x0645, {0x8e, 0x8e} },
	{0x0646, {0xa2, 0x42} },
	{0x0647, {0x7b, 0x7b} },
	{0x0648, {0x04, 0x04} },
	{0x0649, {0x00, 0x00} },
	{0x064a, {0x40, 0x40} },
	{0x064b, {0x8c, 0x98} },
	{0x064c, {0x00, 0x00} },
	{0x064d, {0x63, 0xc3} },
	{0x064e, {0x04, 0x04} },
	{0x064f, {0x20, 0x20} },
	{0x0650, {0x00, 0x00} },
	{0x0651, {0x40, 0x40} },
	{0x0652, {0x01, 0x01} },
};
#define NUM_LPFILTER_COEF (sizeof(lpfilter_coef)\
			   / sizeof(struct au8522_register_config))

static inline struct au8522_state *to_state(struct v4l2_subdev *sd)
{
	return container_of(sd, struct au8522_state, sd);
}

static void setup_vbi(struct au8522_state *state, int aud_input)
{
	int i;

	/* These are set to zero regardless of what mode we're in */
	au8522_writereg(state, AU8522_TVDEC_VBI_CTRL_H_REG017H, 0x00);
	au8522_writereg(state, AU8522_TVDEC_VBI_CTRL_L_REG018H, 0x00);
	au8522_writereg(state, AU8522_TVDEC_VBI_USER_TOTAL_BITS_REG019H, 0x00);
	au8522_writereg(state, AU8522_TVDEC_VBI_USER_TUNIT_H_REG01AH, 0x00);
	au8522_writereg(state, AU8522_TVDEC_VBI_USER_TUNIT_L_REG01BH, 0x00);
	au8522_writereg(state, AU8522_TVDEC_VBI_USER_THRESH1_REG01CH, 0x00);
	au8522_writereg(state, AU8522_TVDEC_VBI_USER_FRAME_PAT2_REG01EH, 0x00);
	au8522_writereg(state, AU8522_TVDEC_VBI_USER_FRAME_PAT1_REG01FH, 0x00);
	au8522_writereg(state, AU8522_TVDEC_VBI_USER_FRAME_PAT0_REG020H, 0x00);
	au8522_writereg(state, AU8522_TVDEC_VBI_USER_FRAME_MASK2_REG021H,
			0x00);
	au8522_writereg(state, AU8522_TVDEC_VBI_USER_FRAME_MASK1_REG022H,
			0x00);
	au8522_writereg(state, AU8522_TVDEC_VBI_USER_FRAME_MASK0_REG023H,
			0x00);

	/* Setup the VBI registers */
	for (i = 0x30; i < 0x60; i++)
		au8522_writereg(state, i, 0x40);

	/* For some reason, every register is 0x40 except register 0x44
	   (confirmed via the HVR-950q USB capture) */
	au8522_writereg(state, 0x44, 0x60);

	/* Enable VBI (we always do this regardless of whether the user is
	   viewing closed caption info) */
	au8522_writereg(state, AU8522_TVDEC_VBI_CTRL_H_REG017H,
			AU8522_TVDEC_VBI_CTRL_H_REG017H_CCON);

}

static void setup_decoder_defaults(struct au8522_state *state, u8 input_mode)
{
	int i;
	int filter_coef_type;

	/* Provide reasonable defaults for picture tuning values */
	au8522_writereg(state, AU8522_TVDEC_SHARPNESSREG009H, 0x07);
	au8522_writereg(state, AU8522_TVDEC_BRIGHTNESS_REG00AH, 0xed);
	state->brightness = 0xed - 128;
	au8522_writereg(state, AU8522_TVDEC_CONTRAST_REG00BH, 0x79);
	state->contrast = 0x79;
	au8522_writereg(state, AU8522_TVDEC_SATURATION_CB_REG00CH, 0x80);
	au8522_writereg(state, AU8522_TVDEC_SATURATION_CR_REG00DH, 0x80);
	state->saturation = 0x80;
	au8522_writereg(state, AU8522_TVDEC_HUE_H_REG00EH, 0x00);
	au8522_writereg(state, AU8522_TVDEC_HUE_L_REG00FH, 0x00);
	state->hue = 0x00;

	/* Other decoder registers */
	au8522_writereg(state, AU8522_TVDEC_INT_MASK_REG010H, 0x00);

	if (input_mode == 0x23) {
		/* S-Video input mapping */
		au8522_writereg(state, AU8522_VIDEO_MODE_REG011H, 0x04);
	} else {
		/* All other modes (CVBS/ATVRF etc.) */
		au8522_writereg(state, AU8522_VIDEO_MODE_REG011H, 0x00);
	}

	au8522_writereg(state, AU8522_TVDEC_PGA_REG012H,
			AU8522_TVDEC_PGA_REG012H_CVBS);
	au8522_writereg(state, AU8522_TVDEC_COMB_MODE_REG015H,
			AU8522_TVDEC_COMB_MODE_REG015H_CVBS);
	au8522_writereg(state, AU8522_TVDED_DBG_MODE_REG060H,
			AU8522_TVDED_DBG_MODE_REG060H_CVBS);
	au8522_writereg(state, AU8522_TVDEC_FORMAT_CTRL1_REG061H,
			AU8522_TVDEC_FORMAT_CTRL1_REG061H_CVBS13);
	au8522_writereg(state, AU8522_TVDEC_FORMAT_CTRL2_REG062H,
			AU8522_TVDEC_FORMAT_CTRL2_REG062H_CVBS13);
	au8522_writereg(state, AU8522_TVDEC_VCR_DET_LLIM_REG063H,
			AU8522_TVDEC_VCR_DET_LLIM_REG063H_CVBS);
	au8522_writereg(state, AU8522_TVDEC_VCR_DET_HLIM_REG064H,
			AU8522_TVDEC_VCR_DET_HLIM_REG064H_CVBS);
	au8522_writereg(state, AU8522_TVDEC_COMB_VDIF_THR1_REG065H,
			AU8522_TVDEC_COMB_VDIF_THR1_REG065H_CVBS);
	au8522_writereg(state, AU8522_TVDEC_COMB_VDIF_THR2_REG066H,
			AU8522_TVDEC_COMB_VDIF_THR2_REG066H_CVBS);
	au8522_writereg(state, AU8522_TVDEC_COMB_VDIF_THR3_REG067H,
			AU8522_TVDEC_COMB_VDIF_THR3_REG067H_CVBS);
	au8522_writereg(state, AU8522_TVDEC_COMB_NOTCH_THR_REG068H,
			AU8522_TVDEC_COMB_NOTCH_THR_REG068H_CVBS);
	au8522_writereg(state, AU8522_TVDEC_COMB_HDIF_THR1_REG069H,
			AU8522_TVDEC_COMB_HDIF_THR1_REG069H_CVBS);
	au8522_writereg(state, AU8522_TVDEC_COMB_HDIF_THR2_REG06AH,
			AU8522_TVDEC_COMB_HDIF_THR2_REG06AH_CVBS);
	au8522_writereg(state, AU8522_TVDEC_COMB_HDIF_THR3_REG06BH,
			AU8522_TVDEC_COMB_HDIF_THR3_REG06BH_CVBS);
	au8522_writereg(state, AU8522_TVDEC_COMB_DCDIF_THR1_REG06CH,
			AU8522_TVDEC_COMB_DCDIF_THR1_REG06CH_CVBS);
	au8522_writereg(state, AU8522_TVDEC_COMB_DCDIF_THR2_REG06DH,
			AU8522_TVDEC_COMB_DCDIF_THR2_REG06DH_CVBS);
	au8522_writereg(state, AU8522_TVDEC_COMB_DCDIF_THR3_REG06EH,
			AU8522_TVDEC_COMB_DCDIF_THR3_REG06EH_CVBS);
	au8522_writereg(state, AU8522_TVDEC_UV_SEP_THR_REG06FH,
			AU8522_TVDEC_UV_SEP_THR_REG06FH_CVBS);
	au8522_writereg(state, AU8522_TVDEC_COMB_DC_THR1_NTSC_REG070H,
			AU8522_TVDEC_COMB_DC_THR1_NTSC_REG070H_CVBS);
	au8522_writereg(state, AU8522_REG071H, AU8522_REG071H_CVBS);
	au8522_writereg(state, AU8522_REG072H, AU8522_REG072H_CVBS);
	au8522_writereg(state, AU8522_TVDEC_COMB_DC_THR2_NTSC_REG073H,
			AU8522_TVDEC_COMB_DC_THR2_NTSC_REG073H_CVBS);
	au8522_writereg(state, AU8522_REG074H, AU8522_REG074H_CVBS);
	au8522_writereg(state, AU8522_REG075H, AU8522_REG075H_CVBS);
	au8522_writereg(state, AU8522_TVDEC_DCAGC_CTRL_REG077H,
			AU8522_TVDEC_DCAGC_CTRL_REG077H_CVBS);
	au8522_writereg(state, AU8522_TVDEC_PIC_START_ADJ_REG078H,
			AU8522_TVDEC_PIC_START_ADJ_REG078H_CVBS);
	au8522_writereg(state, AU8522_TVDEC_AGC_HIGH_LIMIT_REG079H,
			AU8522_TVDEC_AGC_HIGH_LIMIT_REG079H_CVBS);
	au8522_writereg(state, AU8522_TVDEC_MACROVISION_SYNC_THR_REG07AH,
			AU8522_TVDEC_MACROVISION_SYNC_THR_REG07AH_CVBS);
	au8522_writereg(state, AU8522_TVDEC_INTRP_CTRL_REG07BH,
			AU8522_TVDEC_INTRP_CTRL_REG07BH_CVBS);
	au8522_writereg(state, AU8522_TVDEC_AGC_LOW_LIMIT_REG0E4H,
			AU8522_TVDEC_AGC_LOW_LIMIT_REG0E4H_CVBS);
	au8522_writereg(state, AU8522_TOREGAAGC_REG0E5H,
			AU8522_TOREGAAGC_REG0E5H_CVBS);
	au8522_writereg(state, AU8522_REG016H, AU8522_REG016H_CVBS);

	setup_vbi(state, 0);

	if (input_mode == AU8522_INPUT_CONTROL_REG081H_SVIDEO_CH13 ||
	    input_mode == AU8522_INPUT_CONTROL_REG081H_SVIDEO_CH24) {
		/* Despite what the table says, for the HVR-950q we still need
		   to be in CVBS mode for the S-Video input (reason unknown). */
		/* filter_coef_type = 3; */
		filter_coef_type = 5;
	} else {
		filter_coef_type = 5;
	}

	/* Load the Video Decoder Filter Coefficients */
	for (i = 0; i < NUM_FILTER_COEF; i++) {
		au8522_writereg(state, filter_coef[i].reg_name,
				filter_coef[i].reg_val[filter_coef_type]);
	}

	/* It's not clear what these registers are for, but they are always
	   set to the same value regardless of what mode we're in */
	au8522_writereg(state, AU8522_REG42EH, 0x87);
	au8522_writereg(state, AU8522_REG42FH, 0xa2);
	au8522_writereg(state, AU8522_REG430H, 0xbf);
	au8522_writereg(state, AU8522_REG431H, 0xcb);
	au8522_writereg(state, AU8522_REG432H, 0xa1);
	au8522_writereg(state, AU8522_REG433H, 0x41);
	au8522_writereg(state, AU8522_REG434H, 0x88);
	au8522_writereg(state, AU8522_REG435H, 0xc2);
	au8522_writereg(state, AU8522_REG436H, 0x3c);
}

static void au8522_setup_cvbs_mode(struct au8522_state *state)
{
	/* here we're going to try the pre-programmed route */
	au8522_writereg(state, AU8522_MODULE_CLOCK_CONTROL_REG0A3H,
			AU8522_MODULE_CLOCK_CONTROL_REG0A3H_CVBS);

	au8522_writereg(state, AU8522_PGA_CONTROL_REG082H, 0x00);
	au8522_writereg(state, AU8522_CLAMPING_CONTROL_REG083H, 0x0e);
	au8522_writereg(state, AU8522_PGA_CONTROL_REG082H, 0x10);

	au8522_writereg(state, AU8522_INPUT_CONTROL_REG081H,
			AU8522_INPUT_CONTROL_REG081H_CVBS_CH1);

	setup_decoder_defaults(state, AU8522_INPUT_CONTROL_REG081H_CVBS_CH1);

	au8522_writereg(state, AU8522_SYSTEM_MODULE_CONTROL_0_REG0A4H,
			AU8522_SYSTEM_MODULE_CONTROL_0_REG0A4H_CVBS);
}

static void au8522_setup_cvbs_tuner_mode(struct au8522_state *state)
{
	/* here we're going to try the pre-programmed route */
	au8522_writereg(state, AU8522_MODULE_CLOCK_CONTROL_REG0A3H,
			AU8522_MODULE_CLOCK_CONTROL_REG0A3H_CVBS);

	/* It's not clear why they turn off the PGA before enabling the clamp
	   control, but the Windows trace does it so we will too... */
	au8522_writereg(state, AU8522_PGA_CONTROL_REG082H, 0x00);

	/* Enable clamping control */
	au8522_writereg(state, AU8522_CLAMPING_CONTROL_REG083H, 0x0e);

	/* Turn on the PGA */
	au8522_writereg(state, AU8522_PGA_CONTROL_REG082H, 0x10);

	/* Set input mode to CVBS on channel 4 with SIF audio input enabled */
	au8522_writereg(state, AU8522_INPUT_CONTROL_REG081H,
			AU8522_INPUT_CONTROL_REG081H_CVBS_CH4_SIF);

	setup_decoder_defaults(state,
			       AU8522_INPUT_CONTROL_REG081H_CVBS_CH4_SIF);

	au8522_writereg(state, AU8522_SYSTEM_MODULE_CONTROL_0_REG0A4H,
			AU8522_SYSTEM_MODULE_CONTROL_0_REG0A4H_CVBS);
}

static void au8522_setup_svideo_mode(struct au8522_state *state)
{
	au8522_writereg(state, AU8522_MODULE_CLOCK_CONTROL_REG0A3H,
			AU8522_MODULE_CLOCK_CONTROL_REG0A3H_SVIDEO);

	/* Set input to Y on Channe1, C on Channel 3 */
	au8522_writereg(state, AU8522_INPUT_CONTROL_REG081H,
			AU8522_INPUT_CONTROL_REG081H_SVIDEO_CH13);

	/* Disable clamping control (required for S-video) */
	au8522_writereg(state, AU8522_CLAMPING_CONTROL_REG083H, 0x00);

	setup_decoder_defaults(state,
			       AU8522_INPUT_CONTROL_REG081H_SVIDEO_CH13);

	au8522_writereg(state, AU8522_SYSTEM_MODULE_CONTROL_0_REG0A4H,
			AU8522_SYSTEM_MODULE_CONTROL_0_REG0A4H_CVBS);
}

/* ----------------------------------------------------------------------- */

static void disable_audio_input(struct au8522_state *state)
{
	/* This can probably be optimized */
	au8522_writereg(state, AU8522_AUDIO_VOLUME_L_REG0F2H, 0x00);
	au8522_writereg(state, AU8522_AUDIO_VOLUME_R_REG0F3H, 0x00);
	au8522_writereg(state, AU8522_AUDIO_VOLUME_REG0F4H, 0x00);
	au8522_writereg(state, AU8522_I2C_CONTROL_REG1_REG091H, 0x80);
	au8522_writereg(state, AU8522_I2C_CONTROL_REG0_REG090H, 0x84);

	au8522_writereg(state, AU8522_ENA_USB_REG101H, 0x00);
	au8522_writereg(state, AU8522_AUDIO_VOLUME_L_REG0F2H, 0x7F);
	au8522_writereg(state, AU8522_AUDIO_VOLUME_R_REG0F3H, 0x7F);
	au8522_writereg(state, AU8522_REG0F9H, AU8522_REG0F9H_AUDIO);
	au8522_writereg(state, AU8522_AUDIO_MODE_REG0F1H, 0x40);

	au8522_writereg(state, AU8522_GPIO_DATA_REG0E2H, 0x11);
	msleep(5);
	au8522_writereg(state, AU8522_GPIO_DATA_REG0E2H, 0x00);

	au8522_writereg(state, AU8522_SYSTEM_MODULE_CONTROL_1_REG0A5H, 0x04);
	au8522_writereg(state, AU8522_AUDIOFREQ_REG606H, 0x03);
	au8522_writereg(state, AU8522_I2S_CTRL_2_REG112H, 0x02);

	au8522_writereg(state, AU8522_SYSTEM_MODULE_CONTROL_0_REG0A4H,
			AU8522_SYSTEM_MODULE_CONTROL_0_REG0A4H_CVBS);
}

/* 0=disable, 1=SIF */
static void set_audio_input(struct au8522_state *state, int aud_input)
{
	int i;

	/* Note that this function needs to be used in conjunction with setting
	   the input routing via register 0x81 */

	if (aud_input == AU8522_AUDIO_NONE) {
		disable_audio_input(state);
		return;
	}

	if (aud_input != AU8522_AUDIO_SIF) {
		/* The caller asked for a mode we don't currently support */
		printk(KERN_ERR "Unsupported audio mode requested! mode=%d\n",
		       aud_input);
		return;
	}

	/* Load the Audio Decoder Filter Coefficients */
	for (i = 0; i < NUM_LPFILTER_COEF; i++) {
		au8522_writereg(state, lpfilter_coef[i].reg_name,
				lpfilter_coef[i].reg_val[0]);
	}

	/* Setup audio */
	au8522_writereg(state, AU8522_AUDIO_VOLUME_L_REG0F2H, 0x00);
	au8522_writereg(state, AU8522_AUDIO_VOLUME_R_REG0F3H, 0x00);
	au8522_writereg(state, AU8522_AUDIO_VOLUME_REG0F4H, 0x00);
	au8522_writereg(state, AU8522_I2C_CONTROL_REG1_REG091H, 0x80);
	au8522_writereg(state, AU8522_I2C_CONTROL_REG0_REG090H, 0x84);
	msleep(150);
	au8522_writereg(state, AU8522_SYSTEM_MODULE_CONTROL_0_REG0A4H, 0x00);
	msleep(1);
	au8522_writereg(state, AU8522_SYSTEM_MODULE_CONTROL_0_REG0A4H, 0x9d);
	msleep(50);
	au8522_writereg(state, AU8522_AUDIO_VOLUME_L_REG0F2H, 0x7F);
	au8522_writereg(state, AU8522_AUDIO_VOLUME_R_REG0F3H, 0x7F);
	au8522_writereg(state, AU8522_AUDIO_VOLUME_REG0F4H, 0xff);
	msleep(80);
	au8522_writereg(state, AU8522_AUDIO_VOLUME_L_REG0F2H, 0x7F);
	au8522_writereg(state, AU8522_AUDIO_VOLUME_R_REG0F3H, 0x7F);
	au8522_writereg(state, AU8522_REG0F9H, AU8522_REG0F9H_AUDIO);
	au8522_writereg(state, AU8522_AUDIO_MODE_REG0F1H, 0x82);
	msleep(70);
	au8522_writereg(state, AU8522_SYSTEM_MODULE_CONTROL_1_REG0A5H, 0x09);
	au8522_writereg(state, AU8522_AUDIOFREQ_REG606H, 0x03);
	au8522_writereg(state, AU8522_I2S_CTRL_2_REG112H, 0xc2);
}

/* ----------------------------------------------------------------------- */

static int au8522_s_ctrl(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	struct au8522_state *state = to_state(sd);

	switch (ctrl->id) {
	case V4L2_CID_BRIGHTNESS:
		state->brightness = ctrl->value;
		au8522_writereg(state, AU8522_TVDEC_BRIGHTNESS_REG00AH,
				ctrl->value - 128);
		break;
	case V4L2_CID_CONTRAST:
		state->contrast = ctrl->value;
		au8522_writereg(state, AU8522_TVDEC_CONTRAST_REG00BH,
				ctrl->value);
		break;
	case V4L2_CID_SATURATION:
		state->saturation = ctrl->value;
		au8522_writereg(state, AU8522_TVDEC_SATURATION_CB_REG00CH,
				ctrl->value);
		au8522_writereg(state, AU8522_TVDEC_SATURATION_CR_REG00DH,
				ctrl->value);
		break;
	case V4L2_CID_HUE:
		state->hue = ctrl->value;
		au8522_writereg(state, AU8522_TVDEC_HUE_H_REG00EH,
				ctrl->value >> 8);
		au8522_writereg(state, AU8522_TVDEC_HUE_L_REG00FH,
				ctrl->value & 0xFF);
		break;
	case V4L2_CID_AUDIO_VOLUME:
	case V4L2_CID_AUDIO_BASS:
	case V4L2_CID_AUDIO_TREBLE:
	case V4L2_CID_AUDIO_BALANCE:
	case V4L2_CID_AUDIO_MUTE:
		/* Not yet implemented */
	default:
		return -EINVAL;
	}

	return 0;
}

static int au8522_g_ctrl(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	struct au8522_state *state = to_state(sd);

	/* Note that we are using values cached in the state structure instead
	   of reading the registers due to issues with i2c reads not working
	   properly/consistently yet on the HVR-950q */

	switch (ctrl->id) {
	case V4L2_CID_BRIGHTNESS:
		ctrl->value = state->brightness;
		break;
	case V4L2_CID_CONTRAST:
		ctrl->value = state->contrast;
		break;
	case V4L2_CID_SATURATION:
		ctrl->value = state->saturation;
		break;
	case V4L2_CID_HUE:
		ctrl->value = state->hue;
		break;
	case V4L2_CID_AUDIO_VOLUME:
	case V4L2_CID_AUDIO_BASS:
	case V4L2_CID_AUDIO_TREBLE:
	case V4L2_CID_AUDIO_BALANCE:
	case V4L2_CID_AUDIO_MUTE:
		/* Not yet supported */
	default:
		return -EINVAL;
	}

	return 0;
}

/* ----------------------------------------------------------------------- */

#ifdef CONFIG_VIDEO_ADV_DEBUG
static int au8522_g_register(struct v4l2_subdev *sd,
			     struct v4l2_dbg_register *reg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct au8522_state *state = to_state(sd);

	if (!v4l2_chip_match_i2c_client(client, &reg->match))
		return -EINVAL;
	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;
	reg->val = au8522_readreg(state, reg->reg & 0xffff);
	return 0;
}

static int au8522_s_register(struct v4l2_subdev *sd,
			     struct v4l2_dbg_register *reg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct au8522_state *state = to_state(sd);

	if (!v4l2_chip_match_i2c_client(client, &reg->match))
		return -EINVAL;
	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;
	au8522_writereg(state, reg->reg, reg->val & 0xff);
	return 0;
}
#endif

static int au8522_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct au8522_state *state = to_state(sd);

	if (enable) {
		au8522_writereg(state, AU8522_SYSTEM_MODULE_CONTROL_0_REG0A4H,
				0x01);
		msleep(1);
		au8522_writereg(state, AU8522_SYSTEM_MODULE_CONTROL_0_REG0A4H,
				AU8522_SYSTEM_MODULE_CONTROL_0_REG0A4H_CVBS);
	} else {
		/* This does not completely power down the device
		   (it only reduces it from around 140ma to 80ma) */
		au8522_writereg(state, AU8522_SYSTEM_MODULE_CONTROL_0_REG0A4H,
				1 << 5);
	}
	return 0;
}

static int au8522_queryctrl(struct v4l2_subdev *sd, struct v4l2_queryctrl *qc)
{
	switch (qc->id) {
	case V4L2_CID_CONTRAST:
		return v4l2_ctrl_query_fill(qc, 0, 255, 1,
					    AU8522_TVDEC_CONTRAST_REG00BH_CVBS);
	case V4L2_CID_BRIGHTNESS:
		return v4l2_ctrl_query_fill(qc, 0, 255, 1, 128);
	case V4L2_CID_SATURATION:
		return v4l2_ctrl_query_fill(qc, 0, 255, 1, 128);
	case V4L2_CID_HUE:
		return v4l2_ctrl_query_fill(qc, -32768, 32768, 1, 0);
	default:
		break;
	}

	qc->type = 0;
	return -EINVAL;
}

static int au8522_reset(struct v4l2_subdev *sd, u32 val)
{
	struct au8522_state *state = to_state(sd);

	state->operational_mode = AU8522_ANALOG_MODE;

	/* Clear out any state associated with the digital side of the
	   chip, so that when it gets powered back up it won't think
	   that it is already tuned */
	state->current_frequency = 0;

	au8522_writereg(state, 0xa4, 1 << 5);

	return 0;
}

static int au8522_s_video_routing(struct v4l2_subdev *sd,
					u32 input, u32 output, u32 config)
{
	struct au8522_state *state = to_state(sd);

	au8522_reset(sd, 0);

	/* Jam open the i2c gate to the tuner.  We do this here to handle the
	   case where the user went into digital mode (causing the gate to be
	   closed), and then came back to analog mode */
	au8522_writereg(state, 0x106, 1);

	if (input == AU8522_COMPOSITE_CH1) {
		au8522_setup_cvbs_mode(state);
	} else if (input == AU8522_SVIDEO_CH13) {
		au8522_setup_svideo_mode(state);
	} else if (input == AU8522_COMPOSITE_CH4_SIF) {
		au8522_setup_cvbs_tuner_mode(state);
	} else {
		printk(KERN_ERR "au8522 mode not currently supported\n");
		return -EINVAL;
	}
	return 0;
}

static int au8522_s_audio_routing(struct v4l2_subdev *sd,
					u32 input, u32 output, u32 config)
{
	struct au8522_state *state = to_state(sd);
	set_audio_input(state, input);
	return 0;
}

static int au8522_g_tuner(struct v4l2_subdev *sd, struct v4l2_tuner *vt)
{
	int val = 0;
	struct au8522_state *state = to_state(sd);
	u8 lock_status;

	/* Interrogate the decoder to see if we are getting a real signal */
	lock_status = au8522_readreg(state, 0x00);
	if (lock_status == 0xa2)
		vt->signal = 0x01;
	else
		vt->signal = 0x00;

	vt->capability |=
		V4L2_TUNER_CAP_STEREO | V4L2_TUNER_CAP_LANG1 |
		V4L2_TUNER_CAP_LANG2 | V4L2_TUNER_CAP_SAP;

	val = V4L2_TUNER_SUB_MONO;
	vt->rxsubchans = val;
	vt->audmode = V4L2_TUNER_MODE_STEREO;
	return 0;
}

static int au8522_g_chip_ident(struct v4l2_subdev *sd,
			       struct v4l2_dbg_chip_ident *chip)
{
	struct au8522_state *state = to_state(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	return v4l2_chip_ident_i2c_client(client, chip, state->id, state->rev);
}

static int au8522_log_status(struct v4l2_subdev *sd)
{
	/* FIXME: Add some status info here */
	return 0;
}

/* ----------------------------------------------------------------------- */

static const struct v4l2_subdev_core_ops au8522_core_ops = {
	.log_status = au8522_log_status,
	.g_chip_ident = au8522_g_chip_ident,
	.g_ctrl = au8522_g_ctrl,
	.s_ctrl = au8522_s_ctrl,
	.queryctrl = au8522_queryctrl,
	.reset = au8522_reset,
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.g_register = au8522_g_register,
	.s_register = au8522_s_register,
#endif
};

static const struct v4l2_subdev_tuner_ops au8522_tuner_ops = {
	.g_tuner = au8522_g_tuner,
};

static const struct v4l2_subdev_audio_ops au8522_audio_ops = {
	.s_routing = au8522_s_audio_routing,
};

static const struct v4l2_subdev_video_ops au8522_video_ops = {
	.s_routing = au8522_s_video_routing,
	.s_stream = au8522_s_stream,
};

static const struct v4l2_subdev_ops au8522_ops = {
	.core = &au8522_core_ops,
	.tuner = &au8522_tuner_ops,
	.audio = &au8522_audio_ops,
	.video = &au8522_video_ops,
};

/* ----------------------------------------------------------------------- */

static int au8522_probe(struct i2c_client *client,
			const struct i2c_device_id *did)
{
	struct au8522_state *state;
	struct v4l2_subdev *sd;
	int instance;
	struct au8522_config *demod_config;

	/* Check if the adapter supports the needed features */
	if (!i2c_check_functionality(client->adapter,
				     I2C_FUNC_SMBUS_BYTE_DATA)) {
		return -EIO;
	}

	/* allocate memory for the internal state */
	instance = au8522_get_state(&state, client->adapter, client->addr);
	switch (instance) {
	case 0:
		printk(KERN_ERR "au8522_decoder allocation failed\n");
		return -EIO;
	case 1:
		/* new demod instance */
		printk(KERN_INFO "au8522_decoder creating new instance...\n");
		break;
	default:
		/* existing demod instance */
		printk(KERN_INFO "au8522_decoder attach existing instance.\n");
		break;
	}

	demod_config = kzalloc(sizeof(struct au8522_config), GFP_KERNEL);
	if (demod_config == NULL) {
		if (instance == 1)
			kfree(state);
		return -ENOMEM;
	}
	demod_config->demod_address = 0x8e >> 1;

	state->config = demod_config;
	state->i2c = client->adapter;

	sd = &state->sd;
	v4l2_i2c_subdev_init(sd, client, &au8522_ops);

	state->c = client;
	state->vid_input = AU8522_COMPOSITE_CH1;
	state->aud_input = AU8522_AUDIO_NONE;
	state->id = 8522;
	state->rev = 0;

	/* Jam open the i2c gate to the tuner */
	au8522_writereg(state, 0x106, 1);

	return 0;
}

static int au8522_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	v4l2_device_unregister_subdev(sd);
	au8522_release_state(to_state(sd));
	return 0;
}

static const struct i2c_device_id au8522_id[] = {
	{"au8522", 0},
	{}
};

MODULE_DEVICE_TABLE(i2c, au8522_id);

static struct v4l2_i2c_driver_data v4l2_i2c_data = {
	.name = "au8522",
	.probe = au8522_probe,
	.remove = au8522_remove,
	.id_table = au8522_id,
};
