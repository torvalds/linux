/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
   cx231xx-reg.h - driver for Conexant Cx23100/101/102
	       USB video capture devices

   Copyright (C) 2008 <srinivasa.deevi at conexant dot com>

 */

#ifndef _CX231XX_REG_H
#define _CX231XX_REG_H

/*****************************************************************************
				* VBI codes *
*****************************************************************************/

#define SAV_ACTIVE_VIDEO_FIELD1		0x80
#define EAV_ACTIVE_VIDEO_FIELD1		0x90

#define SAV_ACTIVE_VIDEO_FIELD2		0xc0
#define EAV_ACTIVE_VIDEO_FIELD2		0xd0

#define SAV_VBLANK_FIELD1		0xa0
#define EAV_VBLANK_FIELD1		0xb0

#define SAV_VBLANK_FIELD2		0xe0
#define EAV_VBLANK_FIELD2		0xf0

#define SAV_VBI_FIELD1			0x20
#define EAV_VBI_FIELD1			0x30

#define SAV_VBI_FIELD2			0x60
#define EAV_VBI_FIELD2			0x70

/*****************************************************************************/
/* Audio ADC Registers */
#define CH_PWR_CTRL1			0x0000000e
#define CH_PWR_CTRL2			0x0000000f
/*****************************************************************************/

#define      HOST_REG1                0x000
#define      FLD_FORCE_CHIP_SEL       0x80
#define      FLD_AUTO_INC_DIS         0x20
#define      FLD_PREFETCH_EN          0x10
/* Reserved [2:3] */
#define      FLD_DIGITAL_PWR_DN       0x02
#define      FLD_SLEEP                0x01

/*****************************************************************************/
#define      HOST_REG2                0x001

/*****************************************************************************/
#define      HOST_REG3                0x002

/*****************************************************************************/
/* added for polaris */
#define      GPIO_PIN_CTL0            0x3
#define      GPIO_PIN_CTL1            0x4
#define      GPIO_PIN_CTL2            0x5
#define      GPIO_PIN_CTL3            0x6
#define      TS1_PIN_CTL0             0x7
#define      TS1_PIN_CTL1             0x8
/*****************************************************************************/

#define      FLD_CLK_IN_EN            0x80
#define      FLD_XTAL_CTRL            0x70
#define      FLD_BB_CLK_MODE          0x0C
#define      FLD_REF_DIV_PLL          0x02
#define      FLD_REF_SEL_PLL1         0x01

/*****************************************************************************/
#define      CHIP_CTRL                0x100
/* Reserved [27] */
/* Reserved [31:21] */
#define      FLD_CHIP_ACFG_DIS        0x00100000
/* Reserved [19] */
#define      FLD_DUAL_MODE_ADC2       0x00040000
#define      FLD_SIF_EN               0x00020000
#define      FLD_SOFT_RST             0x00010000
#define      FLD_DEVICE_ID            0x0000ffff

/*****************************************************************************/
#define      AFE_CTRL                 0x104
#define      AFE_CTRL_C2HH_SRC_CTRL   0x104
#define      FLD_DIF_OUT_SEL          0xc0000000
#define      FLD_AUX_PLL_CLK_ALT_SEL  0x3c000000
#define      FLD_UV_ORDER_MODE        0x02000000
#define      FLD_FUNC_MODE            0x01800000
#define      FLD_ROT1_PHASE_CTL       0x007f8000
#define      FLD_AUD_IN_SEL           0x00004000
#define      FLD_LUMA_IN_SEL          0x00002000
#define      FLD_CHROMA_IN_SEL        0x00001000
/* reserve [11:10] */
#define      FLD_INV_SPEC_DIS         0x00000200
#define      FLD_VGA_SEL_CH3          0x00000100
#define      FLD_VGA_SEL_CH2          0x00000080
#define      FLD_VGA_SEL_CH1          0x00000040
#define      FLD_DCR_BYP_CH1          0x00000020
#define      FLD_DCR_BYP_CH2          0x00000010
#define      FLD_DCR_BYP_CH3          0x00000008
#define      FLD_EN_12DB_CH3          0x00000004
#define      FLD_EN_12DB_CH2          0x00000002
#define      FLD_EN_12DB_CH1          0x00000001

/* redefine in Cx231xx */
/*****************************************************************************/
#define      DC_CTRL1                 0x108
/* reserve [31:30] */
#define      FLD_CLAMP_LVL_CH1        0x3fff8000
#define      FLD_CLAMP_LVL_CH2        0x00007fff
/*****************************************************************************/

/*****************************************************************************/
#define      DC_CTRL2                 0x10c
/* reserve [31:28] */
#define      FLD_CLAMP_LVL_CH3        0x00fffe00
#define      FLD_CLAMP_WIND_LENTH     0x000001e0
#define      FLD_C2HH_SAT_MIN         0x0000001e
#define      FLD_FLT_BYP_SEL          0x00000001
/*****************************************************************************/

/*****************************************************************************/
#define      DC_CTRL3                 0x110
/* reserve [31:16] */
#define      FLD_ERR_GAIN_CTL         0x00070000
#define      FLD_LPF_MIN              0x0000ffff
/*****************************************************************************/

/*****************************************************************************/
#define      DC_CTRL4                 0x114
/* reserve [31:31] */
#define      FLD_INTG_CH1             0x7fffffff
/*****************************************************************************/

/*****************************************************************************/
#define      DC_CTRL5                 0x118
/* reserve [31:31] */
#define      FLD_INTG_CH2             0x7fffffff
/*****************************************************************************/

/*****************************************************************************/
#define      DC_CTRL6                 0x11c
/* reserve [31:31] */
#define      FLD_INTG_CH3             0x7fffffff
/*****************************************************************************/

/*****************************************************************************/
#define      PIN_CTRL                 0x120
#define      FLD_OEF_AGC_RF           0x00000001
#define      FLD_OEF_AGC_IFVGA        0x00000002
#define      FLD_OEF_AGC_IF           0x00000004
#define      FLD_REG_BO_PUD           0x80000000
#define      FLD_IR_IRQ_STAT          0x40000000
#define      FLD_AUD_IRQ_STAT         0x20000000
#define      FLD_VID_IRQ_STAT         0x10000000
/* Reserved [27:26] */
#define      FLD_IRQ_N_OUT_EN         0x02000000
#define      FLD_IRQ_N_POLAR          0x01000000
/* Reserved [23:6] */
#define      FLD_OE_AUX_PLL_CLK       0x00000020
#define      FLD_OE_I2S_BCLK          0x00000010
#define      FLD_OE_I2S_WCLK          0x00000008
#define      FLD_OE_AGC_IF            0x00000004
#define      FLD_OE_AGC_IFVGA         0x00000002
#define      FLD_OE_AGC_RF            0x00000001

/*****************************************************************************/
#define      AUD_IO_CTRL              0x124
/* Reserved [31:8] */
#define      FLD_I2S_PORT_DIR         0x00000080
#define      FLD_I2S_OUT_SRC          0x00000040
#define      FLD_AUD_CHAN3_SRC        0x00000030
#define      FLD_AUD_CHAN2_SRC        0x0000000c
#define      FLD_AUD_CHAN1_SRC        0x00000003

/*****************************************************************************/
#define      AUD_LOCK1                0x128
#define      FLD_AUD_LOCK_KI_SHIFT    0xc0000000
#define      FLD_AUD_LOCK_KD_SHIFT    0x30000000
/* Reserved [27:25] */
#define      FLD_EN_AV_LOCK           0x01000000
#define      FLD_VID_COUNT            0x00ffffff

/*****************************************************************************/
#define      AUD_LOCK2                0x12c
#define      FLD_AUD_LOCK_KI_MULT     0xf0000000
#define      FLD_AUD_LOCK_KD_MULT     0x0F000000
/* Reserved [23:22] */
#define      FLD_AUD_LOCK_FREQ_SHIFT  0x00300000
#define      FLD_AUD_COUNT            0x000fffff

/*****************************************************************************/
#define      AFE_DIAG_CTRL1           0x134
/* Reserved [31:16] */
#define      FLD_CUV_DLY_LENGTH       0x0000ff00
#define      FLD_YC_DLY_LENGTH        0x000000ff

/*****************************************************************************/
/* Poalris redefine */
#define      AFE_DIAG_CTRL3           0x138
/* Reserved [31:26] */
#define      FLD_AUD_DUAL_FLAG_POL    0x02000000
#define      FLD_VID_DUAL_FLAG_POL    0x01000000
/* Reserved [23:23] */
#define      FLD_COL_CLAMP_DIS_CH1    0x00400000
#define      FLD_COL_CLAMP_DIS_CH2    0x00200000
#define      FLD_COL_CLAMP_DIS_CH3    0x00100000

#define      TEST_CTRL1               0x144
/* Reserved [31:29] */
#define      FLD_LBIST_EN             0x10000000
/* Reserved [27:10] */
#define      FLD_FI_BIST_INTR_R       0x0000200
#define      FLD_FI_BIST_INTR_L       0x0000100
#define      FLD_BIST_FAIL_AUD_PLL    0x0000080
#define      FLD_BIST_INTR_AUD_PLL    0x0000040
#define      FLD_BIST_FAIL_VID_PLL    0x0000020
#define      FLD_BIST_INTR_VID_PLL    0x0000010
/* Reserved [3:1] */
#define      FLD_CIR_TEST_DIS         0x00000001

/*****************************************************************************/
#define      TEST_CTRL2               0x148
#define      FLD_TSXCLK_POL_CTL       0x80000000
#define      FLD_ISO_CTL_SEL          0x40000000
#define      FLD_ISO_CTL_EN           0x20000000
#define      FLD_BIST_DEBUGZ          0x10000000
#define      FLD_AUD_BIST_TEST_H      0x0f000000
/* Reserved [23:22] */
#define      FLD_FLTRN_BIST_TEST_H    0x00020000
#define      FLD_VID_BIST_TEST_H      0x00010000
/* Reserved [19:17] */
#define      FLD_BIST_TEST_H          0x00010000
/* Reserved [15:13] */
#define      FLD_TAB_EN               0x00001000
/* Reserved [11:0] */

/*****************************************************************************/
#define      BIST_STAT                0x14c
#define      FLD_AUD_BIST_FAIL_H      0xfff00000
#define      FLD_FLTRN_BIST_FAIL_H    0x00180000
#define      FLD_VID_BIST_FAIL_H      0x00070000
#define      FLD_AUD_BIST_TST_DONE    0x0000fff0
#define      FLD_FLTRN_BIST_TST_DONE  0x00000008
#define      FLD_VID_BIST_TST_DONE    0x00000007

/*****************************************************************************/
/* DirectIF registers definition have been moved to DIF_reg.h                */
/*****************************************************************************/
#define      MODE_CTRL                0x400
#define      FLD_AFD_PAL60_DIS        0x20000000
#define      FLD_AFD_FORCE_SECAM      0x10000000
#define      FLD_AFD_FORCE_PALNC      0x08000000
#define      FLD_AFD_FORCE_PAL        0x04000000
#define      FLD_AFD_PALM_SEL         0x03000000
#define      FLD_CKILL_MODE           0x00300000
#define      FLD_COMB_NOTCH_MODE      0x00c00000       /* bit[19:18] */
#define      FLD_CLR_LOCK_STAT        0x00020000
#define      FLD_FAST_LOCK_MD         0x00010000
#define      FLD_WCEN                 0x00008000
#define      FLD_CAGCEN               0x00004000
#define      FLD_CKILLEN              0x00002000
#define      FLD_AUTO_SC_LOCK         0x00001000
#define      FLD_MAN_SC_FAST_LOCK     0x00000800
#define      FLD_INPUT_MODE           0x00000600
#define      FLD_AFD_ACQUIRE          0x00000100
#define      FLD_AFD_NTSC_SEL         0x00000080
#define      FLD_AFD_PAL_SEL          0x00000040
#define      FLD_ACFG_DIS             0x00000020
#define      FLD_SQ_PIXEL             0x00000010
#define      FLD_VID_FMT_SEL          0x0000000f

/*****************************************************************************/
#define      OUT_CTRL1                0x404
#define      FLD_POLAR                0x7f000000
/* Reserved [23] */
#define      FLD_RND_MODE             0x00600000
#define      FLD_VIPCLAMP_EN          0x00100000
#define      FLD_VIPBLANK_EN          0x00080000
#define      FLD_VIP_OPT_AL           0x00040000
#define      FLD_IDID0_SOURCE         0x00020000
#define      FLD_DCMODE               0x00010000
#define      FLD_CLK_GATING           0x0000c000
#define      FLD_CLK_INVERT           0x00002000
#define      FLD_HSFMT                0x00001000
#define      FLD_VALIDFMT             0x00000800
#define      FLD_ACTFMT               0x00000400
#define      FLD_SWAPRAW              0x00000200
#define      FLD_CLAMPRAW_EN          0x00000100
#define      FLD_BLUE_FIELD_EN        0x00000080
#define      FLD_BLUE_FIELD_ACT       0x00000040
#define      FLD_TASKBIT_VAL          0x00000020
#define      FLD_ANC_DATA_EN          0x00000010
#define      FLD_VBIHACTRAW_EN        0x00000008
#define      FLD_MODE10B              0x00000004
#define      FLD_OUT_MODE             0x00000003

/*****************************************************************************/
#define      OUT_CTRL2                0x408
#define      FLD_AUD_GRP              0xc0000000
#define      FLD_SAMPLE_RATE          0x30000000
#define      FLD_AUD_ANC_EN           0x08000000
#define      FLD_EN_C                 0x04000000
#define      FLD_EN_B                 0x02000000
#define      FLD_EN_A                 0x01000000
/* Reserved [23:20] */
#define      FLD_IDID1_LSB            0x000c0000
#define      FLD_IDID0_LSB            0x00030000
#define      FLD_IDID1_MSB            0x0000ff00
#define      FLD_IDID0_MSB            0x000000ff

/*****************************************************************************/
#define      GEN_STAT                 0x40c
#define      FLD_VCR_DETECT           0x00800000
#define      FLD_SPECIAL_PLAY_N       0x00400000
#define      FLD_VPRES                0x00200000
#define      FLD_AGC_LOCK             0x00100000
#define      FLD_CSC_LOCK             0x00080000
#define      FLD_VLOCK                0x00040000
#define      FLD_SRC_LOCK             0x00020000
#define      FLD_HLOCK                0x00010000
#define      FLD_VSYNC_N              0x00008000
#define      FLD_SRC_FIFO_UFLOW       0x00004000
#define      FLD_SRC_FIFO_OFLOW       0x00002000
#define      FLD_FIELD                0x00001000
#define      FLD_AFD_FMT_STAT         0x00000f00
#define      FLD_MV_TYPE2_PAIR        0x00000080
#define      FLD_MV_T3CS              0x00000040
#define      FLD_MV_CS                0x00000020
#define      FLD_MV_PSP               0x00000010
/* Reserved [3] */
#define      FLD_MV_CDAT              0x00000003

/*****************************************************************************/
#define      INT_STAT_MASK            0x410
#define      FLD_COMB_3D_FIFO_MSK     0x80000000
#define      FLD_WSS_DAT_AVAIL_MSK    0x40000000
#define      FLD_GS2_DAT_AVAIL_MSK    0x20000000
#define      FLD_GS1_DAT_AVAIL_MSK    0x10000000
#define      FLD_CC_DAT_AVAIL_MSK     0x08000000
#define      FLD_VPRES_CHANGE_MSK     0x04000000
#define      FLD_MV_CHANGE_MSK        0x02000000
#define      FLD_END_VBI_EVEN_MSK     0x01000000
#define      FLD_END_VBI_ODD_MSK      0x00800000
#define      FLD_FMT_CHANGE_MSK       0x00400000
#define      FLD_VSYNC_TRAIL_MSK      0x00200000
#define      FLD_HLOCK_CHANGE_MSK     0x00100000
#define      FLD_VLOCK_CHANGE_MSK     0x00080000
#define      FLD_CSC_LOCK_CHANGE_MSK  0x00040000
#define      FLD_SRC_FIFO_UFLOW_MSK   0x00020000
#define      FLD_SRC_FIFO_OFLOW_MSK   0x00010000
#define      FLD_COMB_3D_FIFO_STAT    0x00008000
#define      FLD_WSS_DAT_AVAIL_STAT   0x00004000
#define      FLD_GS2_DAT_AVAIL_STAT   0x00002000
#define      FLD_GS1_DAT_AVAIL_STAT   0x00001000
#define      FLD_CC_DAT_AVAIL_STAT    0x00000800
#define      FLD_VPRES_CHANGE_STAT    0x00000400
#define      FLD_MV_CHANGE_STAT       0x00000200
#define      FLD_END_VBI_EVEN_STAT    0x00000100
#define      FLD_END_VBI_ODD_STAT     0x00000080
#define      FLD_FMT_CHANGE_STAT      0x00000040
#define      FLD_VSYNC_TRAIL_STAT     0x00000020
#define      FLD_HLOCK_CHANGE_STAT    0x00000010
#define      FLD_VLOCK_CHANGE_STAT    0x00000008
#define      FLD_CSC_LOCK_CHANGE_STAT 0x00000004
#define      FLD_SRC_FIFO_UFLOW_STAT  0x00000002
#define      FLD_SRC_FIFO_OFLOW_STAT  0x00000001

/*****************************************************************************/
#define      LUMA_CTRL                0x414
#define      BRIGHTNESS_CTRL_BYTE     0x414
#define      CONTRAST_CTRL_BYTE       0x415
#define      LUMA_CTRL_BYTE_3         0x416
#define      FLD_LUMA_CORE_SEL        0x00c00000
#define      FLD_RANGE                0x00300000
/* Reserved [19] */
#define      FLD_PEAK_EN              0x00040000
#define      FLD_PEAK_SEL             0x00030000
#define      FLD_CNTRST               0x0000ff00
#define      FLD_BRITE                0x000000ff

/*****************************************************************************/
#define      HSCALE_CTRL              0x418
#define      FLD_HFILT                0x03000000
#define      FLD_HSCALE               0x00ffffff

/*****************************************************************************/
#define      VSCALE_CTRL              0x41c
#define      FLD_LINE_AVG_DIS         0x01000000
/* Reserved [23:20] */
#define      FLD_VS_INTRLACE          0x00080000
#define      FLD_VFILT                0x00070000
/* Reserved [15:13] */
#define      FLD_VSCALE               0x00001fff

/*****************************************************************************/
#define      CHROMA_CTRL              0x420
#define      USAT_CTRL_BYTE           0x420
#define      VSAT_CTRL_BYTE           0x421
#define      HUE_CTRL_BYTE            0x422
#define      FLD_C_LPF_EN             0x20000000
#define      FLD_CHR_DELAY            0x1c000000
#define      FLD_C_CORE_SEL           0x03000000
#define      FLD_HUE                  0x00ff0000
#define      FLD_VSAT                 0x0000ff00
#define      FLD_USAT                 0x000000ff

/*****************************************************************************/
#define      VBI_LINE_CTRL1           0x424
#define      FLD_VBI_MD_LINE4         0xff000000
#define      FLD_VBI_MD_LINE3         0x00ff0000
#define      FLD_VBI_MD_LINE2         0x0000ff00
#define      FLD_VBI_MD_LINE1         0x000000ff

/*****************************************************************************/
#define      VBI_LINE_CTRL2           0x428
#define      FLD_VBI_MD_LINE8         0xff000000
#define      FLD_VBI_MD_LINE7         0x00ff0000
#define      FLD_VBI_MD_LINE6         0x0000ff00
#define      FLD_VBI_MD_LINE5         0x000000ff

/*****************************************************************************/
#define      VBI_LINE_CTRL3           0x42c
#define      FLD_VBI_MD_LINE12        0xff000000
#define      FLD_VBI_MD_LINE11        0x00ff0000
#define      FLD_VBI_MD_LINE10        0x0000ff00
#define      FLD_VBI_MD_LINE9         0x000000ff

/*****************************************************************************/
#define      VBI_LINE_CTRL4           0x430
#define      FLD_VBI_MD_LINE16        0xff000000
#define      FLD_VBI_MD_LINE15        0x00ff0000
#define      FLD_VBI_MD_LINE14        0x0000ff00
#define      FLD_VBI_MD_LINE13        0x000000ff

/*****************************************************************************/
#define      VBI_LINE_CTRL5           0x434
#define      FLD_VBI_MD_LINE17        0x000000ff

/*****************************************************************************/
#define      VBI_FC_CFG               0x438
#define      FLD_FC_ALT2              0xff000000
#define      FLD_FC_ALT1              0x00ff0000
#define      FLD_FC_ALT2_TYPE         0x0000f000
#define      FLD_FC_ALT1_TYPE         0x00000f00
/* Reserved [7:1] */
#define      FLD_FC_SEARCH_MODE       0x00000001

/*****************************************************************************/
#define      VBI_MISC_CFG1            0x43c
#define      FLD_TTX_PKTADRU          0xfff00000
#define      FLD_TTX_PKTADRL          0x000fff00
/* Reserved [7:6] */
#define      FLD_MOJI_PACK_DIS        0x00000020
#define      FLD_VPS_DEC_DIS          0x00000010
#define      FLD_CRI_MARG_SCALE       0x0000000c
#define      FLD_EDGE_RESYNC_EN       0x00000002
#define      FLD_ADAPT_SLICE_DIS      0x00000001

/*****************************************************************************/
#define      VBI_MISC_CFG2            0x440
#define      FLD_HAMMING_TYPE         0x0f000000
/* Reserved [23:20] */
#define      FLD_WSS_FIFO_RST         0x00080000
#define      FLD_GS2_FIFO_RST         0x00040000
#define      FLD_GS1_FIFO_RST         0x00020000
#define      FLD_CC_FIFO_RST          0x00010000
/* Reserved [15:12] */
#define      FLD_VBI3_SDID            0x00000f00
#define      FLD_VBI2_SDID            0x000000f0
#define      FLD_VBI1_SDID            0x0000000f

/*****************************************************************************/
#define      VBI_PAY1                 0x444
#define      FLD_GS1_FIFO_DAT         0xFF000000
#define      FLD_GS1_STAT             0x00FF0000
#define      FLD_CC_FIFO_DAT          0x0000FF00
#define      FLD_CC_STAT              0x000000FF

/*****************************************************************************/
#define      VBI_PAY2                 0x448
#define      FLD_WSS_FIFO_DAT         0xff000000
#define      FLD_WSS_STAT             0x00ff0000
#define      FLD_GS2_FIFO_DAT         0x0000ff00
#define      FLD_GS2_STAT             0x000000ff

/*****************************************************************************/
#define      VBI_CUST1_CFG1           0x44c
/* Reserved [31] */
#define      FLD_VBI1_CRIWIN          0x7f000000
#define      FLD_VBI1_SLICE_DIST      0x00f00000
#define      FLD_VBI1_BITINC          0x000fff00
#define      FLD_VBI1_HDELAY          0x000000ff

/*****************************************************************************/
#define      VBI_CUST1_CFG2           0x450
#define      FLD_VBI1_FC_LENGTH       0x1f000000
#define      FLD_VBI1_FRAME_CODE      0x00ffffff

/*****************************************************************************/
#define      VBI_CUST1_CFG3           0x454
#define      FLD_VBI1_HAM_EN          0x80000000
#define      FLD_VBI1_FIFO_MODE       0x70000000
#define      FLD_VBI1_FORMAT_TYPE     0x0f000000
#define      FLD_VBI1_PAYLD_LENGTH    0x00ff0000
#define      FLD_VBI1_CRI_LENGTH      0x0000f000
#define      FLD_VBI1_CRI_MARGIN      0x00000f00
#define      FLD_VBI1_CRI_TIME        0x000000ff

/*****************************************************************************/
#define      VBI_CUST2_CFG1           0x458
/* Reserved [31] */
#define      FLD_VBI2_CRIWIN          0x7f000000
#define      FLD_VBI2_SLICE_DIST      0x00f00000
#define      FLD_VBI2_BITINC          0x000fff00
#define      FLD_VBI2_HDELAY          0x000000ff

/*****************************************************************************/
#define      VBI_CUST2_CFG2           0x45c
#define      FLD_VBI2_FC_LENGTH       0x1f000000
#define      FLD_VBI2_FRAME_CODE      0x00ffffff

/*****************************************************************************/
#define      VBI_CUST2_CFG3           0x460
#define      FLD_VBI2_HAM_EN          0x80000000
#define      FLD_VBI2_FIFO_MODE       0x70000000
#define      FLD_VBI2_FORMAT_TYPE     0x0f000000
#define      FLD_VBI2_PAYLD_LENGTH    0x00ff0000
#define      FLD_VBI2_CRI_LENGTH      0x0000f000
#define      FLD_VBI2_CRI_MARGIN      0x00000f00
#define      FLD_VBI2_CRI_TIME        0x000000ff

/*****************************************************************************/
#define      VBI_CUST3_CFG1           0x464
/* Reserved [31] */
#define      FLD_VBI3_CRIWIN          0x7f000000
#define      FLD_VBI3_SLICE_DIST      0x00f00000
#define      FLD_VBI3_BITINC          0x000fff00
#define      FLD_VBI3_HDELAY          0x000000ff

/*****************************************************************************/
#define      VBI_CUST3_CFG2           0x468
#define      FLD_VBI3_FC_LENGTH       0x1f000000
#define      FLD_VBI3_FRAME_CODE      0x00ffffff

/*****************************************************************************/
#define      VBI_CUST3_CFG3           0x46c
#define      FLD_VBI3_HAM_EN          0x80000000
#define      FLD_VBI3_FIFO_MODE       0x70000000
#define      FLD_VBI3_FORMAT_TYPE     0x0f000000
#define      FLD_VBI3_PAYLD_LENGTH    0x00ff0000
#define      FLD_VBI3_CRI_LENGTH      0x0000f000
#define      FLD_VBI3_CRI_MARGIN      0x00000f00
#define      FLD_VBI3_CRI_TIME        0x000000ff

/*****************************************************************************/
#define      HORIZ_TIM_CTRL           0x470
#define      FLD_BGDEL_CNT            0xff000000
/* Reserved [23:22] */
#define      FLD_HACTIVE_CNT          0x003ff000
/* Reserved [11:10] */
#define      FLD_HBLANK_CNT           0x000003ff

/*****************************************************************************/
#define      VERT_TIM_CTRL            0x474
#define      FLD_V656BLANK_CNT        0xff000000
/* Reserved [23:22] */
#define      FLD_VACTIVE_CNT          0x003ff000
/* Reserved [11:10] */
#define      FLD_VBLANK_CNT           0x000003ff

/*****************************************************************************/
#define      SRC_COMB_CFG             0x478
#define      FLD_CCOMB_2LN_CHECK      0x80000000
#define      FLD_CCOMB_3LN_EN         0x40000000
#define      FLD_CCOMB_2LN_EN         0x20000000
#define      FLD_CCOMB_3D_EN          0x10000000
/* Reserved [27] */
#define      FLD_LCOMB_3LN_EN         0x04000000
#define      FLD_LCOMB_2LN_EN         0x02000000
#define      FLD_LCOMB_3D_EN          0x01000000
#define      FLD_LUMA_LPF_SEL         0x00c00000
#define      FLD_UV_LPF_SEL           0x00300000
#define      FLD_BLEND_SLOPE          0x000f0000
#define      FLD_CCOMB_REDUCE_EN      0x00008000
/* Reserved [14:10] */
#define      FLD_SRC_DECIM_RATIO      0x000003ff

/*****************************************************************************/
#define      CHROMA_VBIOFF_CFG        0x47c
#define      FLD_VBI_VOFFSET          0x1f000000
/* Reserved [23:20] */
#define      FLD_SC_STEP              0x000fffff

/*****************************************************************************/
#define      FIELD_COUNT              0x480
#define      FLD_FIELD_COUNT_FLD      0x000003ff

/*****************************************************************************/
#define      MISC_TIM_CTRL            0x484
#define      FLD_DEBOUNCE_COUNT       0xc0000000
#define      FLD_VT_LINE_CNT_HYST     0x30000000
/* Reserved [27] */
#define      FLD_AFD_STAT             0x07ff0000
#define      FLD_VPRES_VERT_EN        0x00008000
/* Reserved [14:12] */
#define      FLD_HR32                 0x00000800
#define      FLD_TDALGN               0x00000400
#define      FLD_TDFIELD              0x00000200
/* Reserved [8:6] */
#define      FLD_TEMPDEC              0x0000003f

/*****************************************************************************/
#define      DFE_CTRL1                0x488
#define      FLD_CLAMP_AUTO_EN        0x80000000
#define      FLD_AGC_AUTO_EN          0x40000000
#define      FLD_VGA_CRUSH_EN         0x20000000
#define      FLD_VGA_AUTO_EN          0x10000000
#define      FLD_VBI_GATE_EN          0x08000000
#define      FLD_CLAMP_LEVEL          0x07000000
/* Reserved [23:22] */
#define      FLD_CLAMP_SKIP_CNT       0x00300000
#define      FLD_AGC_GAIN             0x000fff00
/* Reserved [7:6] */
#define      FLD_VGA_GAIN             0x0000003f

/*****************************************************************************/
#define      DFE_CTRL2                0x48c
#define      FLD_VGA_ACQUIRE_RANGE    0x00ff0000
#define      FLD_VGA_TRACK_RANGE      0x0000ff00
#define      FLD_VGA_SYNC             0x000000ff

/*****************************************************************************/
#define      DFE_CTRL3                0x490
#define      FLD_BP_PERCENT           0xff000000
#define      FLD_DFT_THRESHOLD        0x00ff0000
/* Reserved [15:12] */
#define      FLD_SYNC_WIDTH_SEL       0x00000600
#define      FLD_BP_LOOP_GAIN         0x00000300
#define      FLD_SYNC_LOOP_GAIN       0x000000c0
/* Reserved [5:4] */
#define      FLD_AGC_LOOP_GAIN        0x0000000c
#define      FLD_DCC_LOOP_GAIN        0x00000003

/*****************************************************************************/
#define      PLL_CTRL                 0x494
#define      FLD_PLL_KD               0xff000000
#define      FLD_PLL_KI               0x00ff0000
#define      FLD_PLL_MAX_OFFSET       0x0000ffff

/*****************************************************************************/
#define      HTL_CTRL                 0x498
/* Reserved [31:24] */
#define      FLD_AUTO_LOCK_SPD        0x00080000
#define      FLD_MAN_FAST_LOCK        0x00040000
#define      FLD_HTL_15K_EN           0x00020000
#define      FLD_HTL_500K_EN          0x00010000
#define      FLD_HTL_KD               0x0000ff00
#define      FLD_HTL_KI               0x000000ff

/*****************************************************************************/
#define      COMB_CTRL                0x49c
#define      FLD_COMB_PHASE_LIMIT     0xff000000
#define      FLD_CCOMB_ERR_LIMIT      0x00ff0000
#define      FLD_LUMA_THRESHOLD       0x0000ff00
#define      FLD_LCOMB_ERR_LIMIT      0x000000ff

/*****************************************************************************/
#define      CRUSH_CTRL               0x4a0
#define      FLD_WTW_EN               0x00400000
#define      FLD_CRUSH_FREQ           0x00200000
#define      FLD_MAJ_SEL_EN           0x00100000
#define      FLD_MAJ_SEL              0x000c0000
/* Reserved [17:15] */
#define      FLD_SYNC_TIP_REDUCE      0x00007e00
/* Reserved [8:6] */
#define      FLD_SYNC_TIP_INC         0x0000003f

/*****************************************************************************/
#define      SOFT_RST_CTRL            0x4a4
#define      FLD_VD_SOFT_RST          0x00008000
/* Reserved [14:12] */
#define      FLD_REG_RST_MSK          0x00000800
#define      FLD_VOF_RST_MSK          0x00000400
#define      FLD_MVDET_RST_MSK        0x00000200
#define      FLD_VBI_RST_MSK          0x00000100
#define      FLD_SCALE_RST_MSK        0x00000080
#define      FLD_CHROMA_RST_MSK       0x00000040
#define      FLD_LUMA_RST_MSK         0x00000020
#define      FLD_VTG_RST_MSK          0x00000010
#define      FLD_YCSEP_RST_MSK        0x00000008
#define      FLD_SRC_RST_MSK          0x00000004
#define      FLD_DFE_RST_MSK          0x00000002
/* Reserved [0] */

/*****************************************************************************/
#define      MV_DT_CTRL1              0x4a8
/* Reserved [31:29] */
#define      FLD_PSP_STOP_LINE        0x1f000000
/* Reserved [23:21] */
#define      FLD_PSP_STRT_LINE        0x001f0000
/* Reserved [15] */
#define      FLD_PSP_LLIMW            0x00007f00
/* Reserved [7] */
#define      FLD_PSP_ULIMW            0x0000007f

/*****************************************************************************/
#define      MV_DT_CTRL2              0x4aC
#define      FLD_CS_STOPWIN           0xff000000
#define      FLD_CS_STRTWIN           0x00ff0000
#define      FLD_CS_WIDTH             0x0000ff00
#define      FLD_PSP_SPEC_VAL         0x000000ff

/*****************************************************************************/
#define      MV_DT_CTRL3              0x4B0
#define      FLD_AUTO_RATE_DIS        0x80000000
#define      FLD_HLOCK_DIS            0x40000000
#define      FLD_SEL_FIELD_CNT        0x20000000
#define      FLD_CS_TYPE2_SEL         0x10000000
#define      FLD_CS_LINE_THRSH_SEL    0x08000000
#define      FLD_CS_ATHRESH_SEL       0x04000000
#define      FLD_PSP_SPEC_SEL         0x02000000
#define      FLD_PSP_LINES_SEL        0x01000000
#define      FLD_FIELD_CNT            0x00f00000
#define      FLD_CS_TYPE2_CNT         0x000fc000
#define      FLD_CS_LINE_CNT          0x00003f00
#define      FLD_CS_ATHRESH_LEV       0x000000ff

/*****************************************************************************/
#define      CHIP_VERSION             0x4b4
/* Cx231xx redefine  */
#define      VERSION                  0x4b4
#define      FLD_REV_ID               0x000000ff

/*****************************************************************************/
#define      MISC_DIAG_CTRL           0x4b8
/* Reserved [31:24] */
#define      FLD_SC_CONVERGE_THRESH   0x00ff0000
#define      FLD_CCOMB_ERR_LIMIT_3D   0x0000ff00
#define      FLD_LCOMB_ERR_LIMIT_3D   0x000000ff

/*****************************************************************************/
#define      VBI_PASS_CTRL            0x4bc
#define      FLD_VBI_PASS_MD          0x00200000
#define      FLD_VBI_SETUP_DIS        0x00100000
#define      FLD_PASS_LINE_CTRL       0x000fffff

/*****************************************************************************/
/* Cx231xx redefine */
#define      VCR_DET_CTRL             0x4c0
#define      FLD_EN_FIELD_PHASE_DET   0x80000000
#define      FLD_EN_HEAD_SW_DET       0x40000000
#define      FLD_FIELD_PHASE_LENGTH   0x01ff0000
/* Reserved [29:25] */
#define      FLD_FIELD_PHASE_DELAY    0x0000ff00
#define      FLD_FIELD_PHASE_LIMIT    0x000000f0
#define      FLD_HEAD_SW_DET_LIMIT    0x0000000f

/*****************************************************************************/
#define      DL_CTL                   0x800
#define      DL_CTL_ADDRESS_LOW       0x800    /* Byte 1 in DL_CTL */
#define      DL_CTL_ADDRESS_HIGH      0x801    /* Byte 2 in DL_CTL */
#define      DL_CTL_DATA              0x802    /* Byte 3 in DL_CTL */
#define      DL_CTL_CONTROL           0x803    /* Byte 4 in DL_CTL */
/* Reserved [31:5] */
#define      FLD_START_8051           0x10000000
#define      FLD_DL_ENABLE            0x08000000
#define      FLD_DL_AUTO_INC          0x04000000
#define      FLD_DL_MAP               0x03000000

/*****************************************************************************/
#define      STD_DET_STATUS           0x804
#define      FLD_SPARE_STATUS1        0xff000000
#define      FLD_SPARE_STATUS0        0x00ff0000
#define      FLD_MOD_DET_STATUS1      0x0000ff00
#define      FLD_MOD_DET_STATUS0      0x000000ff

/*****************************************************************************/
#define      AUD_BUILD_NUM            0x806
#define      AUD_VER_NUM              0x807
#define      STD_DET_CTL              0x808
#define      STD_DET_CTL_AUD_CTL      0x808    /* Byte 1 in STD_DET_CTL */
#define      STD_DET_CTL_PREF_MODE    0x809    /* Byte 2 in STD_DET_CTL */
#define      FLD_SPARE_CTL0           0xff000000
#define      FLD_DIS_DBX              0x00800000
#define      FLD_DIS_BTSC             0x00400000
#define      FLD_DIS_NICAM_A2         0x00200000
#define      FLD_VIDEO_PRESENT        0x00100000
#define      FLD_DW8051_VIDEO_FORMAT  0x000f0000
#define      FLD_PREF_DEC_MODE        0x0000ff00
#define      FLD_AUD_CONFIG           0x000000ff

/*****************************************************************************/
#define      DW8051_INT               0x80c
#define      FLD_VIDEO_PRESENT_CHANGE 0x80000000
#define      FLD_VIDEO_CHANGE         0x40000000
#define      FLD_RDS_READY            0x20000000
#define      FLD_AC97_INT             0x10000000
#define      FLD_NICAM_BIT_ERROR_TOO_HIGH         0x08000000
#define      FLD_NICAM_LOCK           0x04000000
#define      FLD_NICAM_UNLOCK         0x02000000
#define      FLD_DFT4_TH_CMP          0x01000000
/* Reserved [23:22] */
#define      FLD_LOCK_IND_INT         0x00200000
#define      FLD_DFT3_TH_CMP          0x00100000
#define      FLD_DFT2_TH_CMP          0x00080000
#define      FLD_DFT1_TH_CMP          0x00040000
#define      FLD_FM2_DFT_TH_CMP       0x00020000
#define      FLD_FM1_DFT_TH_CMP       0x00010000
#define      FLD_VIDEO_PRESENT_EN     0x00008000
#define      FLD_VIDEO_CHANGE_EN      0x00004000
#define      FLD_RDS_READY_EN         0x00002000
#define      FLD_AC97_INT_EN          0x00001000
#define      FLD_NICAM_BIT_ERROR_TOO_HIGH_EN      0x00000800
#define      FLD_NICAM_LOCK_EN        0x00000400
#define      FLD_NICAM_UNLOCK_EN      0x00000200
#define      FLD_DFT4_TH_CMP_EN       0x00000100
/* Reserved [7] */
#define      FLD_DW8051_INT6_CTL1     0x00000040
#define      FLD_DW8051_INT5_CTL1     0x00000020
#define      FLD_DW8051_INT4_CTL1     0x00000010
#define      FLD_DW8051_INT3_CTL1     0x00000008
#define      FLD_DW8051_INT2_CTL1     0x00000004
#define      FLD_DW8051_INT1_CTL1     0x00000002
#define      FLD_DW8051_INT0_CTL1     0x00000001

/*****************************************************************************/
#define      GENERAL_CTL              0x810
#define      FLD_RDS_INT              0x80000000
#define      FLD_NBER_INT             0x40000000
#define      FLD_NLL_INT              0x20000000
#define      FLD_IFL_INT              0x10000000
#define      FLD_FDL_INT              0x08000000
#define      FLD_AFC_INT              0x04000000
#define      FLD_AMC_INT              0x02000000
#define      FLD_AC97_INT_CTL         0x01000000
#define      FLD_RDS_INT_DIS          0x00800000
#define      FLD_NBER_INT_DIS         0x00400000
#define      FLD_NLL_INT_DIS          0x00200000
#define      FLD_IFL_INT_DIS          0x00100000
#define      FLD_FDL_INT_DIS          0x00080000
#define      FLD_FC_INT_DIS           0x00040000
#define      FLD_AMC_INT_DIS          0x00020000
#define      FLD_AC97_INT_DIS         0x00010000
#define      FLD_REV_NUM              0x0000ff00
/* Reserved [7:5] */
#define      FLD_DBX_SOFT_RESET_REG   0x00000010
#define      FLD_AD_SOFT_RESET_REG    0x00000008
#define      FLD_SRC_SOFT_RESET_REG   0x00000004
#define      FLD_CDMOD_SOFT_RESET     0x00000002
#define      FLD_8051_SOFT_RESET      0x00000001

/*****************************************************************************/
#define      AAGC_CTL                 0x814
#define      FLD_AFE_12DB_EN          0x80000000
#define      FLD_AAGC_DEFAULT_EN      0x40000000
#define      FLD_AAGC_DEFAULT         0x3f000000
/* Reserved [23] */
#define      FLD_AAGC_GAIN            0x00600000
#define      FLD_AAGC_TH              0x001f0000
/* Reserved [15:14] */
#define      FLD_AAGC_HYST2           0x00003f00
/* Reserved [7:6] */
#define      FLD_AAGC_HYST1           0x0000003f

/*****************************************************************************/
#define      IF_SRC_CTL               0x818
#define      FLD_DBX_BYPASS           0x80000000
/* Reserved [30:25] */
#define      FLD_IF_SRC_MODE          0x01000000
/* Reserved [23:18] */
#define      FLD_IF_SRC_PHASE_INC     0x0001ffff

/*****************************************************************************/
#define      ANALOG_DEMOD_CTL         0x81c
#define      FLD_ROT1_PHACC_PROG      0xffff0000
/* Reserved [15] */
#define      FLD_FM1_DELAY_FIX        0x00007000
#define      FLD_PDF4_SHIFT           0x00000c00
#define      FLD_PDF3_SHIFT           0x00000300
#define      FLD_PDF2_SHIFT           0x000000c0
#define      FLD_PDF1_SHIFT           0x00000030
#define      FLD_FMBYPASS_MODE2       0x00000008
#define      FLD_FMBYPASS_MODE1       0x00000004
#define      FLD_NICAM_MODE           0x00000002
#define      FLD_BTSC_FMRADIO_MODE    0x00000001

/*****************************************************************************/
#define      ROT_FREQ_CTL             0x820
#define      FLD_ROT3_PHACC_PROG      0xffff0000
#define      FLD_ROT2_PHACC_PROG      0x0000ffff

/*****************************************************************************/
#define      FM_CTL                   0x824
#define      FLD_FM2_DC_FB_SHIFT      0xf0000000
#define      FLD_FM2_DC_INT_SHIFT     0x0f000000
#define      FLD_FM2_AFC_RESET        0x00800000
#define      FLD_FM2_DC_PASS_IN       0x00400000
#define      FLD_FM2_DAGC_SHIFT       0x00380000
#define      FLD_FM2_CORDIC_SHIFT     0x00070000
#define      FLD_FM1_DC_FB_SHIFT      0x0000f000
#define      FLD_FM1_DC_INT_SHIFT     0x00000f00
#define      FLD_FM1_AFC_RESET        0x00000080
#define      FLD_FM1_DC_PASS_IN       0x00000040
#define      FLD_FM1_DAGC_SHIFT       0x00000038
#define      FLD_FM1_CORDIC_SHIFT     0x00000007

/*****************************************************************************/
#define      LPF_PDF_CTL              0x828
/* Reserved [31:30] */
#define      FLD_LPF32_SHIFT1         0x30000000
#define      FLD_LPF32_SHIFT2         0x0c000000
#define      FLD_LPF160_SHIFTA        0x03000000
#define      FLD_LPF160_SHIFTB        0x00c00000
#define      FLD_LPF160_SHIFTC        0x00300000
#define      FLD_LPF32_COEF_SEL2      0x000c0000
#define      FLD_LPF32_COEF_SEL1      0x00030000
#define      FLD_LPF160_COEF_SELC     0x0000c000
#define      FLD_LPF160_COEF_SELB     0x00003000
#define      FLD_LPF160_COEF_SELA     0x00000c00
#define      FLD_LPF160_IN_EN_REG     0x00000300
#define      FLD_PDF4_PDF_SEL         0x000000c0
#define      FLD_PDF3_PDF_SEL         0x00000030
#define      FLD_PDF2_PDF_SEL         0x0000000c
#define      FLD_PDF1_PDF_SEL         0x00000003

/*****************************************************************************/
#define      DFT1_CTL1                0x82c
#define      FLD_DFT1_DWELL           0xffff0000
#define      FLD_DFT1_FREQ            0x0000ffff

/*****************************************************************************/
#define      DFT1_CTL2                0x830
#define      FLD_DFT1_THRESHOLD       0xffffff00
#define      FLD_DFT1_CMP_CTL         0x00000080
#define      FLD_DFT1_AVG             0x00000070
/* Reserved [3:1] */
#define      FLD_DFT1_START           0x00000001

/*****************************************************************************/
#define      DFT1_STATUS              0x834
#define      FLD_DFT1_DONE            0x80000000
#define      FLD_DFT1_TH_CMP_STAT     0x40000000
#define      FLD_DFT1_RESULT          0x3fffffff

/*****************************************************************************/
#define      DFT2_CTL1                0x838
#define      FLD_DFT2_DWELL           0xffff0000
#define      FLD_DFT2_FREQ            0x0000ffff

/*****************************************************************************/
#define      DFT2_CTL2                0x83C
#define      FLD_DFT2_THRESHOLD       0xffffff00
#define      FLD_DFT2_CMP_CTL         0x00000080
#define      FLD_DFT2_AVG             0x00000070
/* Reserved [3:1] */
#define      FLD_DFT2_START           0x00000001

/*****************************************************************************/
#define      DFT2_STATUS              0x840
#define      FLD_DFT2_DONE            0x80000000
#define      FLD_DFT2_TH_CMP_STAT     0x40000000
#define      FLD_DFT2_RESULT          0x3fffffff

/*****************************************************************************/
#define      DFT3_CTL1                0x844
#define      FLD_DFT3_DWELL           0xffff0000
#define      FLD_DFT3_FREQ            0x0000ffff

/*****************************************************************************/
#define      DFT3_CTL2                0x848
#define      FLD_DFT3_THRESHOLD       0xffffff00
#define      FLD_DFT3_CMP_CTL         0x00000080
#define      FLD_DFT3_AVG             0x00000070
/* Reserved [3:1] */
#define      FLD_DFT3_START           0x00000001

/*****************************************************************************/
#define      DFT3_STATUS              0x84c
#define      FLD_DFT3_DONE            0x80000000
#define      FLD_DFT3_TH_CMP_STAT     0x40000000
#define      FLD_DFT3_RESULT          0x3fffffff

/*****************************************************************************/
#define      DFT4_CTL1                0x850
#define      FLD_DFT4_DWELL           0xffff0000
#define      FLD_DFT4_FREQ            0x0000ffff

/*****************************************************************************/
#define      DFT4_CTL2                0x854
#define      FLD_DFT4_THRESHOLD       0xffffff00
#define      FLD_DFT4_CMP_CTL         0x00000080
#define      FLD_DFT4_AVG             0x00000070
/* Reserved [3:1] */
#define      FLD_DFT4_START           0x00000001

/*****************************************************************************/
#define      DFT4_STATUS              0x858
#define      FLD_DFT4_DONE            0x80000000
#define      FLD_DFT4_TH_CMP_STAT     0x40000000
#define      FLD_DFT4_RESULT          0x3fffffff

/*****************************************************************************/
#define      AM_MTS_DET               0x85c
#define      FLD_AM_MTS_MODE          0x80000000
/* Reserved [30:26] */
#define      FLD_AM_SUB               0x02000000
#define      FLD_AM_GAIN_EN           0x01000000
/* Reserved [23:16] */
#define      FLD_AMMTS_GAIN_SCALE     0x0000e000
#define      FLD_MTS_PDF_SHIFT        0x00001800
#define      FLD_AM_REG_GAIN          0x00000700
#define      FLD_AGC_REF              0x000000ff

/*****************************************************************************/
#define      ANALOG_MUX_CTL           0x860
/* Reserved [31:29] */
#define      FLD_MUX21_SEL            0x10000000
#define      FLD_MUX20_SEL            0x08000000
#define      FLD_MUX19_SEL            0x04000000
#define      FLD_MUX18_SEL            0x02000000
#define      FLD_MUX17_SEL            0x01000000
#define      FLD_MUX16_SEL            0x00800000
#define      FLD_MUX15_SEL            0x00400000
#define      FLD_MUX14_SEL            0x00300000
#define      FLD_MUX13_SEL            0x000C0000
#define      FLD_MUX12_SEL            0x00020000
#define      FLD_MUX11_SEL            0x00018000
#define      FLD_MUX10_SEL            0x00004000
#define      FLD_MUX9_SEL             0x00002000
#define      FLD_MUX8_SEL             0x00001000
#define      FLD_MUX7_SEL             0x00000800
#define      FLD_MUX6_SEL             0x00000600
#define      FLD_MUX5_SEL             0x00000100
#define      FLD_MUX4_SEL             0x000000c0
#define      FLD_MUX3_SEL             0x00000030
#define      FLD_MUX2_SEL             0x0000000c
#define      FLD_MUX1_SEL             0x00000003

/*****************************************************************************/
/* Cx231xx redefine */
#define      DPLL_CTRL1               0x864
#define      DIG_PLL_CTL1             0x864

#define      FLD_PLL_STATUS           0x07000000
#define      FLD_BANDWIDTH_SELECT     0x00030000
#define      FLD_PLL_SHIFT_REG        0x00007000
#define      FLD_PHASE_SHIFT          0x000007ff

/*****************************************************************************/
/* Cx231xx redefine */
#define      DPLL_CTRL2               0x868
#define      DIG_PLL_CTL2             0x868
#define      FLD_PLL_UNLOCK_THR       0xff000000
#define      FLD_PLL_LOCK_THR         0x00ff0000
/* Reserved [15:8] */
#define      FLD_AM_PDF_SEL2          0x000000c0
#define      FLD_AM_PDF_SEL1          0x00000030
#define      FLD_DPLL_FSM_CTRL        0x0000000c
/* Reserved [1] */
#define      FLD_PLL_PILOT_DET        0x00000001

/*****************************************************************************/
/* Cx231xx redefine */
#define      DPLL_CTRL3               0x86c
#define      DIG_PLL_CTL3             0x86c
#define      FLD_DISABLE_LOOP         0x01000000
#define      FLD_A1_DS1_SEL           0x000c0000
#define      FLD_A1_DS2_SEL           0x00030000
#define      FLD_A1_KI                0x0000ff00
#define      FLD_A1_KD                0x000000ff

/*****************************************************************************/
/* Cx231xx redefine */
#define      DPLL_CTRL4               0x870
#define      DIG_PLL_CTL4             0x870
#define      FLD_A2_DS1_SEL           0x000c0000
#define      FLD_A2_DS2_SEL           0x00030000
#define      FLD_A2_KI                0x0000ff00
#define      FLD_A2_KD                0x000000ff

/*****************************************************************************/
/* Cx231xx redefine */
#define      DPLL_CTRL5               0x874
#define      DIG_PLL_CTL5             0x874
#define      FLD_TRK_DS1_SEL          0x000c0000
#define      FLD_TRK_DS2_SEL          0x00030000
#define      FLD_TRK_KI               0x0000ff00
#define      FLD_TRK_KD               0x000000ff

/*****************************************************************************/
#define      DEEMPH_GAIN_CTL          0x878
#define      FLD_DEEMPH2_GAIN         0xFFFF0000
#define      FLD_DEEMPH1_GAIN         0x0000FFFF

/*****************************************************************************/
/* Cx231xx redefine */
#define      DEEMPH_COEFF1            0x87c
#define      DEEMPH_COEF1             0x87c
#define      FLD_DEEMPH_B0            0xffff0000
#define      FLD_DEEMPH_A0            0x0000ffff

/*****************************************************************************/
/* Cx231xx redefine */
#define      DEEMPH_COEFF2            0x880
#define      DEEMPH_COEF2             0x880
#define      FLD_DEEMPH_B1            0xFFFF0000
#define      FLD_DEEMPH_A1            0x0000FFFF

/*****************************************************************************/
#define      DBX1_CTL1                0x884
#define      FLD_DBX1_WBE_GAIN        0xffff0000
#define      FLD_DBX1_IN_GAIN         0x0000ffff

/*****************************************************************************/
#define      DBX1_CTL2                0x888
#define      FLD_DBX1_SE_BYPASS       0xffff0000
#define      FLD_DBX1_SE_GAIN         0x0000ffff

/*****************************************************************************/
#define      DBX1_RMS_SE              0x88C
#define      FLD_DBX1_RMS_WBE         0xffff0000
#define      FLD_DBX1_RMS_SE_FLD      0x0000ffff

/*****************************************************************************/
#define      DBX2_CTL1                0x890
#define      FLD_DBX2_WBE_GAIN        0xffff0000
#define      FLD_DBX2_IN_GAIN         0x0000ffff

/*****************************************************************************/
#define      DBX2_CTL2                0x894
#define      FLD_DBX2_SE_BYPASS       0xffff0000
#define      FLD_DBX2_SE_GAIN         0x0000ffff

/*****************************************************************************/
#define      DBX2_RMS_SE              0x898
#define      FLD_DBX2_RMS_WBE         0xffff0000
#define      FLD_DBX2_RMS_SE_FLD      0x0000ffff

/*****************************************************************************/
#define      AM_FM_DIFF               0x89c
/* Reserved [31] */
#define      FLD_FM_DIFF_OUT          0x7fff0000
/* Reserved [15] */
#define      FLD_AM_DIFF_OUT          0x00007fff

/*****************************************************************************/
#define      NICAM_FAW                0x8a0
#define      FLD_FAWDETWINEND         0xFc000000
#define      FLD_FAWDETWINSTR         0x03ff0000
/* Reserved [15:12] */
#define      FLD_FAWDETTHRSHLD3       0x00000f00
#define      FLD_FAWDETTHRSHLD2       0x000000f0
#define      FLD_FAWDETTHRSHLD1       0x0000000f

/*****************************************************************************/
/* Cx231xx redefine */
#define      DEEMPH_GAIN              0x8a4
#define      NICAM_DEEMPHGAIN         0x8a4
/* Reserved [31:18] */
#define      FLD_DEEMPHGAIN           0x0003ffff

/*****************************************************************************/
/* Cx231xx redefine */
#define      DEEMPH_NUMER1            0x8a8
#define      NICAM_DEEMPHNUMER1       0x8a8
/* Reserved [31:18] */
#define      FLD_DEEMPHNUMER1         0x0003ffff

/*****************************************************************************/
/* Cx231xx redefine */
#define      DEEMPH_NUMER2            0x8ac
#define      NICAM_DEEMPHNUMER2       0x8ac
/* Reserved [31:18] */
#define      FLD_DEEMPHNUMER2         0x0003ffff

/*****************************************************************************/
/* Cx231xx redefine */
#define      DEEMPH_DENOM1            0x8b0
#define      NICAM_DEEMPHDENOM1       0x8b0
/* Reserved [31:18] */
#define      FLD_DEEMPHDENOM1         0x0003ffff

/*****************************************************************************/
/* Cx231xx redefine */
#define      DEEMPH_DENOM2            0x8b4
#define      NICAM_DEEMPHDENOM2       0x8b4
/* Reserved [31:18] */
#define      FLD_DEEMPHDENOM2         0x0003ffff

/*****************************************************************************/
#define      NICAM_ERRLOG_CTL1        0x8B8
/* Reserved [31:28] */
#define      FLD_ERRINTRPTTHSHLD1     0x0fff0000
/* Reserved [15:12] */
#define      FLD_ERRLOGPERIOD         0x00000fff

/*****************************************************************************/
#define      NICAM_ERRLOG_CTL2        0x8bc
/* Reserved [31:28] */
#define      FLD_ERRINTRPTTHSHLD3     0x0fff0000
/* Reserved [15:12] */
#define      FLD_ERRINTRPTTHSHLD2     0x00000fff

/*****************************************************************************/
#define      NICAM_ERRLOG_STS1        0x8c0
/* Reserved [31:28] */
#define      FLD_ERRLOG2              0x0fff0000
/* Reserved [15:12] */
#define      FLD_ERRLOG1              0x00000fff

/*****************************************************************************/
#define      NICAM_ERRLOG_STS2        0x8c4
/* Reserved [31:12] */
#define      FLD_ERRLOG3              0x00000fff

/*****************************************************************************/
#define      NICAM_STATUS             0x8c8
/* Reserved [31:20] */
#define      FLD_NICAM_CIB            0x000c0000
#define      FLD_NICAM_LOCK_STAT      0x00020000
#define      FLD_NICAM_MUTE           0x00010000
#define      FLD_NICAMADDIT_DATA      0x0000ffe0
#define      FLD_NICAMCNTRL           0x0000001f

/*****************************************************************************/
#define      DEMATRIX_CTL             0x8cc
#define      FLD_AC97_IN_SHIFT        0xf0000000
#define      FLD_I2S_IN_SHIFT         0x0f000000
#define      FLD_DEMATRIX_SEL_CTL     0x00ff0000
/* Reserved [15:11] */
#define      FLD_DMTRX_BYPASS         0x00000400
#define      FLD_DEMATRIX_MODE        0x00000300
/* Reserved [7:6] */
#define      FLD_PH_DBX_SEL           0x00000020
#define      FLD_PH_CH_SEL            0x00000010
#define      FLD_PHASE_FIX            0x0000000f

/*****************************************************************************/
#define      PATH1_CTL1               0x8d0
/* Reserved [31:29] */
#define      FLD_PATH1_MUTE_CTL       0x1f000000
/* Reserved [23:22] */
#define      FLD_PATH1_AVC_CG         0x00300000
#define      FLD_PATH1_AVC_RT         0x000f0000
#define      FLD_PATH1_AVC_AT         0x0000f000
#define      FLD_PATH1_AVC_STEREO     0x00000800
#define      FLD_PATH1_AVC_CR         0x00000700
#define      FLD_PATH1_AVC_RMS_CON    0x000000f0
#define      FLD_PATH1_SEL_CTL        0x0000000f

/*****************************************************************************/
#define      PATH1_VOL_CTL            0x8d4
#define      FLD_PATH1_AVC_THRESHOLD  0x7fff0000
#define      FLD_PATH1_BAL_LEFT       0x00008000
#define      FLD_PATH1_BAL_LEVEL      0x00007f00
#define      FLD_PATH1_VOLUME         0x000000ff

/*****************************************************************************/
#define      PATH1_EQ_CTL             0x8d8
/* Reserved [31:30] */
#define      FLD_PATH1_EQ_TREBLE_VOL  0x3f000000
/* Reserved [23:22] */
#define      FLD_PATH1_EQ_MID_VOL     0x003f0000
/* Reserved [15:14] */
#define      FLD_PATH1_EQ_BASS_VOL    0x00003f00
/* Reserved [7:1] */
#define      FLD_PATH1_EQ_BAND_SEL    0x00000001

/*****************************************************************************/
#define      PATH1_SC_CTL             0x8dc
#define      FLD_PATH1_SC_THRESHOLD   0x7fff0000
#define      FLD_PATH1_SC_RT          0x0000f000
#define      FLD_PATH1_SC_AT          0x00000f00
#define      FLD_PATH1_SC_STEREO      0x00000080
#define      FLD_PATH1_SC_CR          0x00000070
#define      FLD_PATH1_SC_RMS_CON     0x0000000f

/*****************************************************************************/
#define      PATH2_CTL1               0x8e0
/* Reserved [31:26] */
#define      FLD_PATH2_MUTE_CTL       0x03000000
/* Reserved [23:22] */
#define      FLD_PATH2_AVC_CG         0x00300000
#define      FLD_PATH2_AVC_RT         0x000f0000
#define      FLD_PATH2_AVC_AT         0x0000f000
#define      FLD_PATH2_AVC_STEREO     0x00000800
#define      FLD_PATH2_AVC_CR         0x00000700
#define      FLD_PATH2_AVC_RMS_CON    0x000000f0
#define      FLD_PATH2_SEL_CTL        0x0000000f

/*****************************************************************************/
#define      PATH2_VOL_CTL            0x8e4
#define      FLD_PATH2_AVC_THRESHOLD  0xffff0000
#define      FLD_PATH2_BAL_LEFT       0x00008000
#define      FLD_PATH2_BAL_LEVEL      0x00007f00
#define      FLD_PATH2_VOLUME         0x000000ff

/*****************************************************************************/
#define      PATH2_EQ_CTL             0x8e8
/* Reserved [31:30] */
#define      FLD_PATH2_EQ_TREBLE_VOL  0x3f000000
/* Reserved [23:22] */
#define      FLD_PATH2_EQ_MID_VOL     0x003f0000
/* Reserved [15:14] */
#define      FLD_PATH2_EQ_BASS_VOL    0x00003f00
/* Reserved [7:1] */
#define      FLD_PATH2_EQ_BAND_SEL    0x00000001

/*****************************************************************************/
#define      PATH2_SC_CTL             0x8eC
#define      FLD_PATH2_SC_THRESHOLD   0xffff0000
#define      FLD_PATH2_SC_RT          0x0000f000
#define      FLD_PATH2_SC_AT          0x00000f00
#define      FLD_PATH2_SC_STEREO      0x00000080
#define      FLD_PATH2_SC_CR          0x00000070
#define      FLD_PATH2_SC_RMS_CON     0x0000000f

/*****************************************************************************/
#define      SRC_CTL                  0x8f0
#define      FLD_SRC_STATUS           0xffffff00
#define      FLD_FIFO_LF_EN           0x000000fc
#define      FLD_BYPASS_LI            0x00000002
#define      FLD_BYPASS_PF            0x00000001

/*****************************************************************************/
#define      SRC_LF_COEF              0x8f4
#define      FLD_LOOP_FILTER_COEF2    0xffff0000
#define      FLD_LOOP_FILTER_COEF1    0x0000ffff

/*****************************************************************************/
#define      SRC1_CTL                 0x8f8
/* Reserved [31:28] */
#define      FLD_SRC1_FIFO_RD_TH      0x0f000000
/* Reserved [23:18] */
#define      FLD_SRC1_PHASE_INC       0x0003ffff

/*****************************************************************************/
#define      SRC2_CTL                 0x8fc
/* Reserved [31:28] */
#define      FLD_SRC2_FIFO_RD_TH      0x0f000000
/* Reserved [23:18] */
#define      FLD_SRC2_PHASE_INC       0x0003ffff

/*****************************************************************************/
#define      SRC3_CTL                 0x900
/* Reserved [31:28] */
#define      FLD_SRC3_FIFO_RD_TH      0x0f000000
/* Reserved [23:18] */
#define      FLD_SRC3_PHASE_INC       0x0003ffff

/*****************************************************************************/
#define      SRC4_CTL                 0x904
/* Reserved [31:28] */
#define      FLD_SRC4_FIFO_RD_TH      0x0f000000
/* Reserved [23:18] */
#define      FLD_SRC4_PHASE_INC       0x0003ffff

/*****************************************************************************/
#define      SRC5_CTL                 0x908
/* Reserved [31:28] */
#define      FLD_SRC5_FIFO_RD_TH      0x0f000000
/* Reserved [23:18] */
#define      FLD_SRC5_PHASE_INC       0x0003ffff

/*****************************************************************************/
#define      SRC6_CTL                 0x90c
/* Reserved [31:28] */
#define      FLD_SRC6_FIFO_RD_TH      0x0f000000
/* Reserved [23:18] */
#define      FLD_SRC6_PHASE_INC       0x0003ffff

/*****************************************************************************/
#define      BAND_OUT_SEL             0x910
#define      FLD_SRC6_IN_SEL          0xc0000000
#define      FLD_SRC6_CLK_SEL         0x30000000
#define      FLD_SRC5_IN_SEL          0x0c000000
#define      FLD_SRC5_CLK_SEL         0x03000000
#define      FLD_SRC4_IN_SEL          0x00c00000
#define      FLD_SRC4_CLK_SEL         0x00300000
#define      FLD_SRC3_IN_SEL          0x000c0000
#define      FLD_SRC3_CLK_SEL         0x00030000
#define      FLD_BASEBAND_BYPASS_CTL  0x0000ff00
#define      FLD_AC97_SRC_SEL         0x000000c0
#define      FLD_I2S_SRC_SEL          0x00000030
#define      FLD_PARALLEL2_SRC_SEL    0x0000000c
#define      FLD_PARALLEL1_SRC_SEL    0x00000003

/*****************************************************************************/
#define      I2S_IN_CTL               0x914
/* Reserved [31:11] */
#define      FLD_I2S_UP2X_BW20K       0x00000400
#define      FLD_I2S_UP2X_BYPASS      0x00000200
#define      FLD_I2S_IN_MASTER_MODE   0x00000100
#define      FLD_I2S_IN_SONY_MODE     0x00000080
#define      FLD_I2S_IN_RIGHT_JUST    0x00000040
#define      FLD_I2S_IN_WS_SEL        0x00000020
#define      FLD_I2S_IN_BCN_DEL       0x0000001f

/*****************************************************************************/
#define      I2S_OUT_CTL              0x918
/* Reserved [31:17] */
#define      FLD_I2S_OUT_SOFT_RESET_EN  0x00010000
/* Reserved [15:9] */
#define      FLD_I2S_OUT_MASTER_MODE  0x00000100
#define      FLD_I2S_OUT_SONY_MODE    0x00000080
#define      FLD_I2S_OUT_RIGHT_JUST   0x00000040
#define      FLD_I2S_OUT_WS_SEL       0x00000020
#define      FLD_I2S_OUT_BCN_DEL      0x0000001f

/*****************************************************************************/
#define      AC97_CTL                 0x91c
/* Reserved [31:26] */
#define      FLD_AC97_UP2X_BW20K      0x02000000
#define      FLD_AC97_UP2X_BYPASS     0x01000000
/* Reserved [23:17] */
#define      FLD_AC97_RST_ACL         0x00010000
/* Reserved [15:9] */
#define      FLD_AC97_WAKE_UP_SYNC    0x00000100
/* Reserved [7:1] */
#define      FLD_AC97_SHUTDOWN        0x00000001

/* Cx231xx redefine */
#define      QPSK_IAGC_CTL1		0x94c
#define      QPSK_IAGC_CTL2		0x950
#define      QPSK_FEPR_FREQ		0x954
#define      QPSK_BTL_CTL1		0x958
#define      QPSK_BTL_CTL2		0x95c
#define      QPSK_CTL_CTL1		0x960
#define      QPSK_CTL_CTL2		0x964
#define      QPSK_MF_FAGC_CTL		0x968
#define      QPSK_EQ_CTL		0x96c
#define      QPSK_LOCK_CTL		0x970

/*****************************************************************************/
#define      FM1_DFT_CTL              0x9a8
#define      FLD_FM1_DFT_THRESHOLD    0xffff0000
/* Reserved [15:8] */
#define      FLD_FM1_DFT_CMP_CTL      0x00000080
#define      FLD_FM1_DFT_AVG          0x00000070
/* Reserved [3:1] */
#define      FLD_FM1_DFT_START        0x00000001

/*****************************************************************************/
#define      FM1_DFT_STATUS           0x9ac
#define      FLD_FM1_DFT_DONE         0x80000000
/* Reserved [30:19] */
#define      FLD_FM_DFT_TH_CMP        0x00040000
#define      FLD_FM1_DFT              0x0003ffff

/*****************************************************************************/
#define      FM2_DFT_CTL              0x9b0
#define      FLD_FM2_DFT_THRESHOLD    0xffff0000
/* Reserved [15:8] */
#define      FLD_FM2_DFT_CMP_CTL      0x00000080
#define      FLD_FM2_DFT_AVG          0x00000070
/* Reserved [3:1] */
#define      FLD_FM2_DFT_START        0x00000001

/*****************************************************************************/
#define      FM2_DFT_STATUS           0x9b4
#define      FLD_FM2_DFT_DONE         0x80000000
/* Reserved [30:19] */
#define      FLD_FM2_DFT_TH_CMP_STAT  0x00040000
#define      FLD_FM2_DFT              0x0003ffff

/*****************************************************************************/
/* Cx231xx redefine */
#define      AAGC_STATUS_REG          0x9b8
#define      AAGC_STATUS              0x9b8
/* Reserved [31:27] */
#define      FLD_FM2_DAGC_OUT         0x07000000
/* Reserved [23:19] */
#define      FLD_FM1_DAGC_OUT         0x00070000
/* Reserved [15:6] */
#define      FLD_AFE_VGA_OUT          0x0000003f

/*****************************************************************************/
#define      MTS_GAIN_STATUS          0x9bc
/* Reserved [31:14] */
#define      FLD_MTS_GAIN             0x00003fff

#define      RDS_OUT                  0x9c0
#define      FLD_RDS_Q                0xffff0000
#define      FLD_RDS_I                0x0000ffff

/*****************************************************************************/
#define      AUTOCONFIG_REG           0x9c4
/* Reserved [31:4] */
#define      FLD_AUTOCONFIG_MODE      0x0000000f

#define      FM_AFC                   0x9c8
#define      FLD_FM2_AFC              0xffff0000
#define      FLD_FM1_AFC              0x0000ffff

/*****************************************************************************/
/* Cx231xx redefine */
#define      NEW_SPARE                0x9cc
#define      NEW_SPARE_REG            0x9cc

/*****************************************************************************/
#define      DBX_ADJ                  0x9d0
/* Reserved [31:28] */
#define      FLD_DBX2_ADJ             0x0fff0000
/* Reserved [15:12] */
#define      FLD_DBX1_ADJ             0x00000fff

#define      VID_FMT_AUTO              0
#define      VID_FMT_NTSC_M            1
#define      VID_FMT_NTSC_J            2
#define      VID_FMT_NTSC_443          3
#define      VID_FMT_PAL_BDGHI         4
#define      VID_FMT_PAL_M             5
#define      VID_FMT_PAL_N             6
#define      VID_FMT_PAL_NC            7
#define      VID_FMT_PAL_60            8
#define      VID_FMT_SECAM             12
#define      VID_FMT_SECAM_60          13

#define      INPUT_MODE_CVBS_0         0       /* INPUT_MODE_VALUE(0) */
#define      INPUT_MODE_YC_1           1       /* INPUT_MODE_VALUE(1) */
#define      INPUT_MODE_YC2_2          2       /* INPUT_MODE_VALUE(2) */
#define      INPUT_MODE_YUV_3          3       /* INPUT_MODE_VALUE(3) */

#define      LUMA_LPF_LOW_BANDPASS     0       /* 0.6Mhz LPF BW */
#define      LUMA_LPF_MEDIUM_BANDPASS  1       /* 1.0Mhz LPF BW */
#define      LUMA_LPF_HIGH_BANDPASS    2       /* 1.5Mhz LPF BW */

#define      UV_LPF_LOW_BANDPASS       0       /* 0.6Mhz LPF BW */
#define      UV_LPF_MEDIUM_BANDPASS    1       /* 1.0Mhz LPF BW */
#define      UV_LPF_HIGH_BANDPASS      2       /* 1.5Mhz LPF BW */

#define      TWO_TAP_FILT              0
#define      THREE_TAP_FILT            1
#define      FOUR_TAP_FILT             2
#define      FIVE_TAP_FILT             3

#define      AUD_CHAN_SRC_PARALLEL     0
#define      AUD_CHAN_SRC_I2S_INPUT    1
#define      AUD_CHAN_SRC_FLATIRON     2
#define      AUD_CHAN_SRC_PARALLEL3    3

#define      OUT_MODE_601              0
#define      OUT_MODE_656              1
#define      OUT_MODE_VIP11            2
#define      OUT_MODE_VIP20            3

#define      PHASE_INC_49MHZ          0x0df22
#define      PHASE_INC_56MHZ          0x0fa5b
#define      PHASE_INC_28MHZ          0x010000

#endif
