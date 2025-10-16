/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *  cx18 ADEC header
 *
 *  Derived from cx25840-core.h
 *
 *  Copyright (C) 2007  Hans Verkuil <hverkuil@kernel.org>
 *  Copyright (C) 2008  Andy Walls <awalls@md.metrocast.net>
 */

#ifndef _CX18_AV_CORE_H_
#define _CX18_AV_CORE_H_

#include <media/v4l2-device.h>
#include <media/v4l2-ctrls.h>

struct cx18;

enum cx18_av_video_input {
	/* Composite video inputs In1-In8 */
	CX18_AV_COMPOSITE1 = 1,
	CX18_AV_COMPOSITE2,
	CX18_AV_COMPOSITE3,
	CX18_AV_COMPOSITE4,
	CX18_AV_COMPOSITE5,
	CX18_AV_COMPOSITE6,
	CX18_AV_COMPOSITE7,
	CX18_AV_COMPOSITE8,

	/* S-Video inputs consist of one luma input (In1-In8) ORed with one
	   chroma input (In5-In8) */
	CX18_AV_SVIDEO_LUMA1 = 0x10,
	CX18_AV_SVIDEO_LUMA2 = 0x20,
	CX18_AV_SVIDEO_LUMA3 = 0x30,
	CX18_AV_SVIDEO_LUMA4 = 0x40,
	CX18_AV_SVIDEO_LUMA5 = 0x50,
	CX18_AV_SVIDEO_LUMA6 = 0x60,
	CX18_AV_SVIDEO_LUMA7 = 0x70,
	CX18_AV_SVIDEO_LUMA8 = 0x80,
	CX18_AV_SVIDEO_CHROMA4 = 0x400,
	CX18_AV_SVIDEO_CHROMA5 = 0x500,
	CX18_AV_SVIDEO_CHROMA6 = 0x600,
	CX18_AV_SVIDEO_CHROMA7 = 0x700,
	CX18_AV_SVIDEO_CHROMA8 = 0x800,

	/* S-Video aliases for common luma/chroma combinations */
	CX18_AV_SVIDEO1 = 0x510,
	CX18_AV_SVIDEO2 = 0x620,
	CX18_AV_SVIDEO3 = 0x730,
	CX18_AV_SVIDEO4 = 0x840,

	/* Component Video inputs consist of one luma input (In1-In8) ORed
	   with a red chroma (In4-In6) and blue chroma input (In7-In8) */
	CX18_AV_COMPONENT_LUMA1 = 0x1000,
	CX18_AV_COMPONENT_LUMA2 = 0x2000,
	CX18_AV_COMPONENT_LUMA3 = 0x3000,
	CX18_AV_COMPONENT_LUMA4 = 0x4000,
	CX18_AV_COMPONENT_LUMA5 = 0x5000,
	CX18_AV_COMPONENT_LUMA6 = 0x6000,
	CX18_AV_COMPONENT_LUMA7 = 0x7000,
	CX18_AV_COMPONENT_LUMA8 = 0x8000,
	CX18_AV_COMPONENT_R_CHROMA4 = 0x40000,
	CX18_AV_COMPONENT_R_CHROMA5 = 0x50000,
	CX18_AV_COMPONENT_R_CHROMA6 = 0x60000,
	CX18_AV_COMPONENT_B_CHROMA7 = 0x700000,
	CX18_AV_COMPONENT_B_CHROMA8 = 0x800000,

	/* Component Video aliases for common combinations */
	CX18_AV_COMPONENT1 = 0x861000,
};

enum cx18_av_audio_input {
	/* Audio inputs: serial or In4-In8 */
	CX18_AV_AUDIO_SERIAL1,
	CX18_AV_AUDIO_SERIAL2,
	CX18_AV_AUDIO4 = 4,
	CX18_AV_AUDIO5,
	CX18_AV_AUDIO6,
	CX18_AV_AUDIO7,
	CX18_AV_AUDIO8,
};

struct cx18_av_state {
	struct v4l2_subdev sd;
	struct v4l2_ctrl_handler hdl;
	struct v4l2_ctrl *volume;
	int radio;
	v4l2_std_id std;
	enum cx18_av_video_input vid_input;
	enum cx18_av_audio_input aud_input;
	u32 audclk_freq;
	int audmode;
	u32 rev;
	int is_initialized;

	/*
	 * The VBI slicer starts operating and counting lines, beginning at
	 * slicer line count of 1, at D lines after the deassertion of VRESET.
	 * This staring field line, S, is 6 (& 319) or 10 (& 273) for 625 or 525
	 * line systems respectively.  Sliced ancillary data captured on VBI
	 * slicer line M is inserted after the VBI slicer is done with line M,
	 * when VBI slicer line count is N = M+1.  Thus when the VBI slicer
	 * reports a VBI slicer line number with ancillary data, the IDID0 byte
	 * indicates VBI slicer line N.  The actual field line that the captured
	 * data comes from is
	 *
	 * L = M+(S+D-1) = N-1+(S+D-1) = N + (S+D-2).
	 *
	 * L is the line in the field, not frame, from which the VBI data came.
	 * N is the line reported by the slicer in the ancillary data.
	 * D is the slicer_line_delay value programmed into register 0x47f.
	 * S is 6 for 625 line systems or 10 for 525 line systems
	 * (S+D-2) is the slicer_line_offset used to convert slicer reported
	 * line counts to actual field lines.
	 */
	int slicer_line_delay;
	int slicer_line_offset;
};


/* Registers */
#define CXADEC_CHIP_TYPE_TIGER     0x837
#define CXADEC_CHIP_TYPE_MAKO      0x843

#define CXADEC_HOST_REG1           0x000
#define CXADEC_HOST_REG2           0x001

#define CXADEC_CHIP_CTRL           0x100
#define CXADEC_AFE_CTRL            0x104
#define CXADEC_PLL_CTRL1           0x108
#define CXADEC_VID_PLL_FRAC        0x10C
#define CXADEC_AUX_PLL_FRAC        0x110
#define CXADEC_PIN_CTRL1           0x114
#define CXADEC_PIN_CTRL2           0x118
#define CXADEC_PIN_CFG1            0x11C
#define CXADEC_PIN_CFG2            0x120

#define CXADEC_PIN_CFG3            0x124
#define CXADEC_I2S_MCLK            0x127

#define CXADEC_AUD_LOCK1           0x128
#define CXADEC_AUD_LOCK2           0x12C
#define CXADEC_POWER_CTRL          0x130
#define CXADEC_AFE_DIAG_CTRL1      0x134
#define CXADEC_AFE_DIAG_CTRL2      0x138
#define CXADEC_AFE_DIAG_CTRL3      0x13C
#define CXADEC_PLL_DIAG_CTRL       0x140
#define CXADEC_TEST_CTRL1          0x144
#define CXADEC_TEST_CTRL2          0x148
#define CXADEC_BIST_STAT           0x14C
#define CXADEC_DLL1_DIAG_CTRL      0x158
#define CXADEC_DLL2_DIAG_CTRL      0x15C

/* IR registers */
#define CXADEC_IR_CTRL_REG         0x200
#define CXADEC_IR_TXCLK_REG        0x204
#define CXADEC_IR_RXCLK_REG        0x208
#define CXADEC_IR_CDUTY_REG        0x20C
#define CXADEC_IR_STAT_REG         0x210
#define CXADEC_IR_IRQEN_REG        0x214
#define CXADEC_IR_FILTER_REG       0x218
#define CXADEC_IR_FIFO_REG         0x21C

/* Video Registers */
#define CXADEC_MODE_CTRL           0x400
#define CXADEC_OUT_CTRL1           0x404
#define CXADEC_OUT_CTRL2           0x408
#define CXADEC_GEN_STAT            0x40C
#define CXADEC_INT_STAT_MASK       0x410
#define CXADEC_LUMA_CTRL           0x414

#define CXADEC_BRIGHTNESS_CTRL_BYTE 0x414
#define CXADEC_CONTRAST_CTRL_BYTE  0x415
#define CXADEC_LUMA_CTRL_BYTE_3    0x416

#define CXADEC_HSCALE_CTRL         0x418
#define CXADEC_VSCALE_CTRL         0x41C

#define CXADEC_CHROMA_CTRL         0x420

#define CXADEC_USAT_CTRL_BYTE      0x420
#define CXADEC_VSAT_CTRL_BYTE      0x421
#define CXADEC_HUE_CTRL_BYTE       0x422

#define CXADEC_VBI_LINE_CTRL1      0x424
#define CXADEC_VBI_LINE_CTRL2      0x428
#define CXADEC_VBI_LINE_CTRL3      0x42C
#define CXADEC_VBI_LINE_CTRL4      0x430
#define CXADEC_VBI_LINE_CTRL5      0x434
#define CXADEC_VBI_FC_CFG          0x438
#define CXADEC_VBI_MISC_CFG1       0x43C
#define CXADEC_VBI_MISC_CFG2       0x440
#define CXADEC_VBI_PAY1            0x444
#define CXADEC_VBI_PAY2            0x448
#define CXADEC_VBI_CUST1_CFG1      0x44C
#define CXADEC_VBI_CUST1_CFG2      0x450
#define CXADEC_VBI_CUST1_CFG3      0x454
#define CXADEC_VBI_CUST2_CFG1      0x458
#define CXADEC_VBI_CUST2_CFG2      0x45C
#define CXADEC_VBI_CUST2_CFG3      0x460
#define CXADEC_VBI_CUST3_CFG1      0x464
#define CXADEC_VBI_CUST3_CFG2      0x468
#define CXADEC_VBI_CUST3_CFG3      0x46C
#define CXADEC_HORIZ_TIM_CTRL      0x470
#define CXADEC_VERT_TIM_CTRL       0x474
#define CXADEC_SRC_COMB_CFG        0x478
#define CXADEC_CHROMA_VBIOFF_CFG   0x47C
#define CXADEC_FIELD_COUNT         0x480
#define CXADEC_MISC_TIM_CTRL       0x484
#define CXADEC_DFE_CTRL1           0x488
#define CXADEC_DFE_CTRL2           0x48C
#define CXADEC_DFE_CTRL3           0x490
#define CXADEC_PLL_CTRL2           0x494
#define CXADEC_HTL_CTRL            0x498
#define CXADEC_COMB_CTRL           0x49C
#define CXADEC_CRUSH_CTRL          0x4A0
#define CXADEC_SOFT_RST_CTRL       0x4A4
#define CXADEC_MV_DT_CTRL2         0x4A8
#define CXADEC_MV_DT_CTRL3         0x4AC
#define CXADEC_MISC_DIAG_CTRL      0x4B8

#define CXADEC_DL_CTL              0x800
#define CXADEC_DL_CTL_ADDRESS_LOW  0x800   /* Byte 1 in DL_CTL */
#define CXADEC_DL_CTL_ADDRESS_HIGH 0x801   /* Byte 2 in DL_CTL */
#define CXADEC_DL_CTL_DATA         0x802   /* Byte 3 in DL_CTL */
#define CXADEC_DL_CTL_CONTROL      0x803   /* Byte 4 in DL_CTL */

#define CXADEC_STD_DET_STATUS      0x804

#define CXADEC_STD_DET_CTL         0x808
#define CXADEC_STD_DET_CTL_AUD_CTL   0x808 /* Byte 1 in STD_DET_CTL */
#define CXADEC_STD_DET_CTL_PREF_MODE 0x809 /* Byte 2 in STD_DET_CTL */

#define CXADEC_DW8051_INT          0x80C
#define CXADEC_GENERAL_CTL         0x810
#define CXADEC_AAGC_CTL            0x814
#define CXADEC_IF_SRC_CTL          0x818
#define CXADEC_ANLOG_DEMOD_CTL     0x81C
#define CXADEC_ROT_FREQ_CTL        0x820
#define CXADEC_FM1_CTL             0x824
#define CXADEC_PDF_CTL             0x828
#define CXADEC_DFT1_CTL1           0x82C
#define CXADEC_DFT1_CTL2           0x830
#define CXADEC_DFT_STATUS          0x834
#define CXADEC_DFT2_CTL1           0x838
#define CXADEC_DFT2_CTL2           0x83C
#define CXADEC_DFT2_STATUS         0x840
#define CXADEC_DFT3_CTL1           0x844
#define CXADEC_DFT3_CTL2           0x848
#define CXADEC_DFT3_STATUS         0x84C
#define CXADEC_DFT4_CTL1           0x850
#define CXADEC_DFT4_CTL2           0x854
#define CXADEC_DFT4_STATUS         0x858
#define CXADEC_AM_MTS_DET          0x85C
#define CXADEC_ANALOG_MUX_CTL      0x860
#define CXADEC_DIG_PLL_CTL1        0x864
#define CXADEC_DIG_PLL_CTL2        0x868
#define CXADEC_DIG_PLL_CTL3        0x86C
#define CXADEC_DIG_PLL_CTL4        0x870
#define CXADEC_DIG_PLL_CTL5        0x874
#define CXADEC_DEEMPH_GAIN_CTL     0x878
#define CXADEC_DEEMPH_COEF1        0x87C
#define CXADEC_DEEMPH_COEF2        0x880
#define CXADEC_DBX1_CTL1           0x884
#define CXADEC_DBX1_CTL2           0x888
#define CXADEC_DBX1_STATUS         0x88C
#define CXADEC_DBX2_CTL1           0x890
#define CXADEC_DBX2_CTL2           0x894
#define CXADEC_DBX2_STATUS         0x898
#define CXADEC_AM_FM_DIFF          0x89C

/* NICAM registers go here */
#define CXADEC_NICAM_STATUS        0x8C8
#define CXADEC_DEMATRIX_CTL        0x8CC

#define CXADEC_PATH1_CTL1          0x8D0
#define CXADEC_PATH1_VOL_CTL       0x8D4
#define CXADEC_PATH1_EQ_CTL        0x8D8
#define CXADEC_PATH1_SC_CTL        0x8DC

#define CXADEC_PATH2_CTL1          0x8E0
#define CXADEC_PATH2_VOL_CTL       0x8E4
#define CXADEC_PATH2_EQ_CTL        0x8E8
#define CXADEC_PATH2_SC_CTL        0x8EC

#define CXADEC_SRC_CTL             0x8F0
#define CXADEC_SRC_LF_COEF         0x8F4
#define CXADEC_SRC1_CTL            0x8F8
#define CXADEC_SRC2_CTL            0x8FC
#define CXADEC_SRC3_CTL            0x900
#define CXADEC_SRC4_CTL            0x904
#define CXADEC_SRC5_CTL            0x908
#define CXADEC_SRC6_CTL            0x90C

#define CXADEC_BASEBAND_OUT_SEL    0x910
#define CXADEC_I2S_IN_CTL          0x914
#define CXADEC_I2S_OUT_CTL         0x918
#define CXADEC_AC97_CTL            0x91C
#define CXADEC_QAM_PDF             0x920
#define CXADEC_QAM_CONST_DEC       0x924
#define CXADEC_QAM_ROTATOR_FREQ    0x948

/* Bit definitions / settings used in Mako Audio */
#define CXADEC_PREF_MODE_MONO_LANGA        0
#define CXADEC_PREF_MODE_MONO_LANGB        1
#define CXADEC_PREF_MODE_MONO_LANGC        2
#define CXADEC_PREF_MODE_FALLBACK          3
#define CXADEC_PREF_MODE_STEREO            4
#define CXADEC_PREF_MODE_DUAL_LANG_AC      5
#define CXADEC_PREF_MODE_DUAL_LANG_BC      6
#define CXADEC_PREF_MODE_DUAL_LANG_AB      7


#define CXADEC_DETECT_STEREO               1
#define CXADEC_DETECT_DUAL                 2
#define CXADEC_DETECT_TRI                  4
#define CXADEC_DETECT_SAP                  0x10
#define CXADEC_DETECT_NO_SIGNAL            0xFF

#define CXADEC_SELECT_AUDIO_STANDARD_BG    0xF0  /* NICAM BG and A2 BG */
#define CXADEC_SELECT_AUDIO_STANDARD_DK1   0xF1  /* NICAM DK and A2 DK */
#define CXADEC_SELECT_AUDIO_STANDARD_DK2   0xF2
#define CXADEC_SELECT_AUDIO_STANDARD_DK3   0xF3
#define CXADEC_SELECT_AUDIO_STANDARD_I     0xF4  /* NICAM I and A1 */
#define CXADEC_SELECT_AUDIO_STANDARD_L     0xF5  /* NICAM L and System L AM */
#define CXADEC_SELECT_AUDIO_STANDARD_BTSC  0xF6
#define CXADEC_SELECT_AUDIO_STANDARD_EIAJ  0xF7
#define CXADEC_SELECT_AUDIO_STANDARD_A2_M  0xF8  /* A2 M */
#define CXADEC_SELECT_AUDIO_STANDARD_FM    0xF9  /* FM radio */
#define CXADEC_SELECT_AUDIO_STANDARD_AUTO  0xFF  /* Auto detect */

static inline struct cx18_av_state *to_cx18_av_state(struct v4l2_subdev *sd)
{
	return container_of(sd, struct cx18_av_state, sd);
}

static inline struct v4l2_subdev *to_sd(struct v4l2_ctrl *ctrl)
{
	return &container_of(ctrl->handler, struct cx18_av_state, hdl)->sd;
}

/* ----------------------------------------------------------------------- */
/* cx18_av-core.c							   */
int cx18_av_write(struct cx18 *cx, u16 addr, u8 value);
int cx18_av_write4(struct cx18 *cx, u16 addr, u32 value);
int cx18_av_write4_noretry(struct cx18 *cx, u16 addr, u32 value);
int cx18_av_write_expect(struct cx18 *cx, u16 addr, u8 value, u8 eval, u8 mask);
int cx18_av_write4_expect(struct cx18 *cx, u16 addr, u32 value, u32 eval,
			  u32 mask);
u8 cx18_av_read(struct cx18 *cx, u16 addr);
u32 cx18_av_read4(struct cx18 *cx, u16 addr);
int cx18_av_and_or(struct cx18 *cx, u16 addr, unsigned mask, u8 value);
int cx18_av_and_or4(struct cx18 *cx, u16 addr, u32 mask, u32 value);
void cx18_av_std_setup(struct cx18 *cx);

int cx18_av_probe(struct cx18 *cx);

/* ----------------------------------------------------------------------- */
/* cx18_av-firmware.c                                                      */
int cx18_av_loadfw(struct cx18 *cx);

/* ----------------------------------------------------------------------- */
/* cx18_av-audio.c                                                         */
int cx18_av_s_clock_freq(struct v4l2_subdev *sd, u32 freq);
void cx18_av_audio_set_path(struct cx18 *cx);
extern const struct v4l2_ctrl_ops cx18_av_audio_ctrl_ops;

/* ----------------------------------------------------------------------- */
/* cx18_av-vbi.c                                                           */
int cx18_av_decode_vbi_line(struct v4l2_subdev *sd,
			   struct v4l2_decode_vbi_line *vbi);
int cx18_av_s_raw_fmt(struct v4l2_subdev *sd, struct v4l2_vbi_format *fmt);
int cx18_av_g_sliced_fmt(struct v4l2_subdev *sd, struct v4l2_sliced_vbi_format *fmt);
int cx18_av_s_sliced_fmt(struct v4l2_subdev *sd, struct v4l2_sliced_vbi_format *fmt);

#endif
