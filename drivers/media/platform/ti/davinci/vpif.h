/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * VPIF header file
 *
 * Copyright (C) 2009 Texas Instruments Incorporated - https://www.ti.com/
 */

#ifndef VPIF_H
#define VPIF_H

#include <linux/io.h>
#include <linux/videodev2.h>
#include <media/davinci/vpif_types.h>

/* Maximum channel allowed */
#define VPIF_NUM_CHANNELS		(4)
#define VPIF_CAPTURE_NUM_CHANNELS	(2)
#define VPIF_DISPLAY_NUM_CHANNELS	(2)

/* Macros to read/write registers */
extern void __iomem *vpif_base;
extern spinlock_t vpif_lock;

#define regr(reg)               readl((reg) + vpif_base)
#define regw(value, reg)        writel(value, (reg + vpif_base))

/* Register Address Offsets */
#define VPIF_PID			(0x0000)
#define VPIF_CH0_CTRL			(0x0004)
#define VPIF_CH1_CTRL			(0x0008)
#define VPIF_CH2_CTRL			(0x000C)
#define VPIF_CH3_CTRL			(0x0010)

#define VPIF_INTEN			(0x0020)
#define VPIF_INTEN_SET			(0x0024)
#define VPIF_INTEN_CLR			(0x0028)
#define VPIF_STATUS			(0x002C)
#define VPIF_STATUS_CLR			(0x0030)
#define VPIF_EMULATION_CTRL		(0x0034)
#define VPIF_REQ_SIZE			(0x0038)

#define VPIF_CH0_TOP_STRT_ADD_LUMA	(0x0040)
#define VPIF_CH0_BTM_STRT_ADD_LUMA	(0x0044)
#define VPIF_CH0_TOP_STRT_ADD_CHROMA	(0x0048)
#define VPIF_CH0_BTM_STRT_ADD_CHROMA	(0x004c)
#define VPIF_CH0_TOP_STRT_ADD_HANC	(0x0050)
#define VPIF_CH0_BTM_STRT_ADD_HANC	(0x0054)
#define VPIF_CH0_TOP_STRT_ADD_VANC	(0x0058)
#define VPIF_CH0_BTM_STRT_ADD_VANC	(0x005c)
#define VPIF_CH0_SP_CFG			(0x0060)
#define VPIF_CH0_IMG_ADD_OFST		(0x0064)
#define VPIF_CH0_HANC_ADD_OFST		(0x0068)
#define VPIF_CH0_H_CFG			(0x006c)
#define VPIF_CH0_V_CFG_00		(0x0070)
#define VPIF_CH0_V_CFG_01		(0x0074)
#define VPIF_CH0_V_CFG_02		(0x0078)
#define VPIF_CH0_V_CFG_03		(0x007c)

#define VPIF_CH1_TOP_STRT_ADD_LUMA	(0x0080)
#define VPIF_CH1_BTM_STRT_ADD_LUMA	(0x0084)
#define VPIF_CH1_TOP_STRT_ADD_CHROMA	(0x0088)
#define VPIF_CH1_BTM_STRT_ADD_CHROMA	(0x008c)
#define VPIF_CH1_TOP_STRT_ADD_HANC	(0x0090)
#define VPIF_CH1_BTM_STRT_ADD_HANC	(0x0094)
#define VPIF_CH1_TOP_STRT_ADD_VANC	(0x0098)
#define VPIF_CH1_BTM_STRT_ADD_VANC	(0x009c)
#define VPIF_CH1_SP_CFG			(0x00a0)
#define VPIF_CH1_IMG_ADD_OFST		(0x00a4)
#define VPIF_CH1_HANC_ADD_OFST		(0x00a8)
#define VPIF_CH1_H_CFG			(0x00ac)
#define VPIF_CH1_V_CFG_00		(0x00b0)
#define VPIF_CH1_V_CFG_01		(0x00b4)
#define VPIF_CH1_V_CFG_02		(0x00b8)
#define VPIF_CH1_V_CFG_03		(0x00bc)

#define VPIF_CH2_TOP_STRT_ADD_LUMA	(0x00c0)
#define VPIF_CH2_BTM_STRT_ADD_LUMA	(0x00c4)
#define VPIF_CH2_TOP_STRT_ADD_CHROMA	(0x00c8)
#define VPIF_CH2_BTM_STRT_ADD_CHROMA	(0x00cc)
#define VPIF_CH2_TOP_STRT_ADD_HANC	(0x00d0)
#define VPIF_CH2_BTM_STRT_ADD_HANC	(0x00d4)
#define VPIF_CH2_TOP_STRT_ADD_VANC	(0x00d8)
#define VPIF_CH2_BTM_STRT_ADD_VANC	(0x00dc)
#define VPIF_CH2_SP_CFG			(0x00e0)
#define VPIF_CH2_IMG_ADD_OFST		(0x00e4)
#define VPIF_CH2_HANC_ADD_OFST		(0x00e8)
#define VPIF_CH2_H_CFG			(0x00ec)
#define VPIF_CH2_V_CFG_00		(0x00f0)
#define VPIF_CH2_V_CFG_01		(0x00f4)
#define VPIF_CH2_V_CFG_02		(0x00f8)
#define VPIF_CH2_V_CFG_03		(0x00fc)
#define VPIF_CH2_HANC0_STRT		(0x0100)
#define VPIF_CH2_HANC0_SIZE		(0x0104)
#define VPIF_CH2_HANC1_STRT		(0x0108)
#define VPIF_CH2_HANC1_SIZE		(0x010c)
#define VPIF_CH2_VANC0_STRT		(0x0110)
#define VPIF_CH2_VANC0_SIZE		(0x0114)
#define VPIF_CH2_VANC1_STRT		(0x0118)
#define VPIF_CH2_VANC1_SIZE		(0x011c)

#define VPIF_CH3_TOP_STRT_ADD_LUMA	(0x0140)
#define VPIF_CH3_BTM_STRT_ADD_LUMA	(0x0144)
#define VPIF_CH3_TOP_STRT_ADD_CHROMA	(0x0148)
#define VPIF_CH3_BTM_STRT_ADD_CHROMA	(0x014c)
#define VPIF_CH3_TOP_STRT_ADD_HANC	(0x0150)
#define VPIF_CH3_BTM_STRT_ADD_HANC	(0x0154)
#define VPIF_CH3_TOP_STRT_ADD_VANC	(0x0158)
#define VPIF_CH3_BTM_STRT_ADD_VANC	(0x015c)
#define VPIF_CH3_SP_CFG			(0x0160)
#define VPIF_CH3_IMG_ADD_OFST		(0x0164)
#define VPIF_CH3_HANC_ADD_OFST		(0x0168)
#define VPIF_CH3_H_CFG			(0x016c)
#define VPIF_CH3_V_CFG_00		(0x0170)
#define VPIF_CH3_V_CFG_01		(0x0174)
#define VPIF_CH3_V_CFG_02		(0x0178)
#define VPIF_CH3_V_CFG_03		(0x017c)
#define VPIF_CH3_HANC0_STRT		(0x0180)
#define VPIF_CH3_HANC0_SIZE		(0x0184)
#define VPIF_CH3_HANC1_STRT		(0x0188)
#define VPIF_CH3_HANC1_SIZE		(0x018c)
#define VPIF_CH3_VANC0_STRT		(0x0190)
#define VPIF_CH3_VANC0_SIZE		(0x0194)
#define VPIF_CH3_VANC1_STRT		(0x0198)
#define VPIF_CH3_VANC1_SIZE		(0x019c)

#define VPIF_IODFT_CTRL			(0x01c0)

/* Functions for bit Manipulation */
static inline void vpif_set_bit(u32 reg, u32 bit)
{
	regw((regr(reg)) | (0x01 << bit), reg);
}

static inline void vpif_clr_bit(u32 reg, u32 bit)
{
	regw(((regr(reg)) & ~(0x01 << bit)), reg);
}

/* Macro for Generating mask */
#ifdef GENERATE_MASK
#undef GENERATE_MASK
#endif

#define GENERATE_MASK(bits, pos) \
		((((0xFFFFFFFF) << (32 - bits)) >> (32 - bits)) << pos)

/* Bit positions in the channel control registers */
#define VPIF_CH_DATA_MODE_BIT	(2)
#define VPIF_CH_YC_MUX_BIT	(3)
#define VPIF_CH_SDR_FMT_BIT	(4)
#define VPIF_CH_HANC_EN_BIT	(8)
#define VPIF_CH_VANC_EN_BIT	(9)

#define VPIF_CAPTURE_CH_NIP	(10)
#define VPIF_DISPLAY_CH_NIP	(11)

#define VPIF_DISPLAY_PIX_EN_BIT	(10)

#define VPIF_CH_INPUT_FIELD_FRAME_BIT	(12)

#define VPIF_CH_FID_POLARITY_BIT	(15)
#define VPIF_CH_V_VALID_POLARITY_BIT	(14)
#define VPIF_CH_H_VALID_POLARITY_BIT	(13)
#define VPIF_CH_DATA_WIDTH_BIT		(28)

#define VPIF_CH_CLK_EDGE_CTRL_BIT	(31)

/* Mask various length */
#define VPIF_CH_EAVSAV_MASK	GENERATE_MASK(13, 0)
#define VPIF_CH_LEN_MASK	GENERATE_MASK(12, 0)
#define VPIF_CH_WIDTH_MASK	GENERATE_MASK(13, 0)
#define VPIF_CH_LEN_SHIFT	(16)

/* VPIF masks for registers */
#define VPIF_REQ_SIZE_MASK	(0x1ff)

/* bit posotion of interrupt vpif_ch_intr register */
#define VPIF_INTEN_FRAME_CH0	(0x00000001)
#define VPIF_INTEN_FRAME_CH1	(0x00000002)
#define VPIF_INTEN_FRAME_CH2	(0x00000004)
#define VPIF_INTEN_FRAME_CH3	(0x00000008)

/* bit position of clock and channel enable in vpif_chn_ctrl register */

#define VPIF_CH0_CLK_EN		(0x00000002)
#define VPIF_CH0_EN		(0x00000001)
#define VPIF_CH1_CLK_EN		(0x00000002)
#define VPIF_CH1_EN		(0x00000001)
#define VPIF_CH2_CLK_EN		(0x00000002)
#define VPIF_CH2_EN		(0x00000001)
#define VPIF_CH3_CLK_EN		(0x00000002)
#define VPIF_CH3_EN		(0x00000001)
#define VPIF_CH_CLK_EN		(0x00000002)
#define VPIF_CH_EN		(0x00000001)

#define VPIF_INT_TOP	(0x00)
#define VPIF_INT_BOTTOM	(0x01)
#define VPIF_INT_BOTH	(0x02)

#define VPIF_CH0_INT_CTRL_SHIFT	(6)
#define VPIF_CH1_INT_CTRL_SHIFT	(6)
#define VPIF_CH2_INT_CTRL_SHIFT	(6)
#define VPIF_CH3_INT_CTRL_SHIFT	(6)
#define VPIF_CH_INT_CTRL_SHIFT	(6)

#define VPIF_CH2_CLIP_ANC_EN	14
#define VPIF_CH2_CLIP_ACTIVE_EN	13

#define VPIF_CH3_CLIP_ANC_EN	14
#define VPIF_CH3_CLIP_ACTIVE_EN	13

/* enabled interrupt on both the fields on vpid_ch0_ctrl register */
#define channel0_intr_assert()	(regw((regr(VPIF_CH0_CTRL)|\
	(VPIF_INT_BOTH << VPIF_CH0_INT_CTRL_SHIFT)), VPIF_CH0_CTRL))

/* enabled interrupt on both the fields on vpid_ch1_ctrl register */
#define channel1_intr_assert()	(regw((regr(VPIF_CH1_CTRL)|\
	(VPIF_INT_BOTH << VPIF_CH1_INT_CTRL_SHIFT)), VPIF_CH1_CTRL))

/* enabled interrupt on both the fields on vpid_ch0_ctrl register */
#define channel2_intr_assert()	(regw((regr(VPIF_CH2_CTRL)|\
	(VPIF_INT_BOTH << VPIF_CH2_INT_CTRL_SHIFT)), VPIF_CH2_CTRL))

/* enabled interrupt on both the fields on vpid_ch1_ctrl register */
#define channel3_intr_assert()	(regw((regr(VPIF_CH3_CTRL)|\
	(VPIF_INT_BOTH << VPIF_CH3_INT_CTRL_SHIFT)), VPIF_CH3_CTRL))

#define VPIF_CH_FID_MASK	(0x20)
#define VPIF_CH_FID_SHIFT	(5)

#define VPIF_NTSC_VBI_START_FIELD0	(1)
#define VPIF_NTSC_VBI_START_FIELD1	(263)
#define VPIF_PAL_VBI_START_FIELD0	(624)
#define VPIF_PAL_VBI_START_FIELD1	(311)

#define VPIF_NTSC_HBI_START_FIELD0	(1)
#define VPIF_NTSC_HBI_START_FIELD1	(263)
#define VPIF_PAL_HBI_START_FIELD0	(624)
#define VPIF_PAL_HBI_START_FIELD1	(311)

#define VPIF_NTSC_VBI_COUNT_FIELD0	(20)
#define VPIF_NTSC_VBI_COUNT_FIELD1	(19)
#define VPIF_PAL_VBI_COUNT_FIELD0	(24)
#define VPIF_PAL_VBI_COUNT_FIELD1	(25)

#define VPIF_NTSC_HBI_COUNT_FIELD0	(263)
#define VPIF_NTSC_HBI_COUNT_FIELD1	(262)
#define VPIF_PAL_HBI_COUNT_FIELD0	(312)
#define VPIF_PAL_HBI_COUNT_FIELD1	(313)

#define VPIF_NTSC_VBI_SAMPLES_PER_LINE	(720)
#define VPIF_PAL_VBI_SAMPLES_PER_LINE	(720)
#define VPIF_NTSC_HBI_SAMPLES_PER_LINE	(268)
#define VPIF_PAL_HBI_SAMPLES_PER_LINE	(280)

#define VPIF_CH_VANC_EN			(0x20)
#define VPIF_DMA_REQ_SIZE		(0x080)
#define VPIF_EMULATION_DISABLE		(0x01)

extern u8 irq_vpif_capture_channel[VPIF_NUM_CHANNELS];

/* inline function to enable/disable channel0 */
static inline void enable_channel0(int enable)
{
	if (enable)
		regw((regr(VPIF_CH0_CTRL) | (VPIF_CH0_EN)), VPIF_CH0_CTRL);
	else
		regw((regr(VPIF_CH0_CTRL) & (~VPIF_CH0_EN)), VPIF_CH0_CTRL);
}

/* inline function to enable/disable channel1 */
static inline void enable_channel1(int enable)
{
	if (enable)
		regw((regr(VPIF_CH1_CTRL) | (VPIF_CH1_EN)), VPIF_CH1_CTRL);
	else
		regw((regr(VPIF_CH1_CTRL) & (~VPIF_CH1_EN)), VPIF_CH1_CTRL);
}

/* inline function to enable interrupt for channel0 */
static inline void channel0_intr_enable(int enable)
{
	unsigned long flags;

	spin_lock_irqsave(&vpif_lock, flags);

	if (enable) {
		regw((regr(VPIF_INTEN) | 0x10), VPIF_INTEN);
		regw((regr(VPIF_INTEN_SET) | 0x10), VPIF_INTEN_SET);

		regw((regr(VPIF_INTEN) | VPIF_INTEN_FRAME_CH0), VPIF_INTEN);
		regw((regr(VPIF_INTEN_SET) | VPIF_INTEN_FRAME_CH0),
							VPIF_INTEN_SET);
	} else {
		regw((regr(VPIF_INTEN) & (~VPIF_INTEN_FRAME_CH0)), VPIF_INTEN);
		regw((regr(VPIF_INTEN_SET) | VPIF_INTEN_FRAME_CH0),
							VPIF_INTEN_SET);
	}
	spin_unlock_irqrestore(&vpif_lock, flags);
}

/* inline function to enable interrupt for channel1 */
static inline void channel1_intr_enable(int enable)
{
	unsigned long flags;

	spin_lock_irqsave(&vpif_lock, flags);

	if (enable) {
		regw((regr(VPIF_INTEN) | 0x10), VPIF_INTEN);
		regw((regr(VPIF_INTEN_SET) | 0x10), VPIF_INTEN_SET);

		regw((regr(VPIF_INTEN) | VPIF_INTEN_FRAME_CH1), VPIF_INTEN);
		regw((regr(VPIF_INTEN_SET) | VPIF_INTEN_FRAME_CH1),
							VPIF_INTEN_SET);
	} else {
		regw((regr(VPIF_INTEN) & (~VPIF_INTEN_FRAME_CH1)), VPIF_INTEN);
		regw((regr(VPIF_INTEN_SET) | VPIF_INTEN_FRAME_CH1),
							VPIF_INTEN_SET);
	}
	spin_unlock_irqrestore(&vpif_lock, flags);
}

/* inline function to set buffer addresses in case of Y/C non mux mode */
static inline void ch0_set_videobuf_addr_yc_nmux(unsigned long top_strt_luma,
						 unsigned long btm_strt_luma,
						 unsigned long top_strt_chroma,
						 unsigned long btm_strt_chroma)
{
	regw(top_strt_luma, VPIF_CH0_TOP_STRT_ADD_LUMA);
	regw(btm_strt_luma, VPIF_CH0_BTM_STRT_ADD_LUMA);
	regw(top_strt_chroma, VPIF_CH1_TOP_STRT_ADD_CHROMA);
	regw(btm_strt_chroma, VPIF_CH1_BTM_STRT_ADD_CHROMA);
}

/* inline function to set buffer addresses in VPIF registers for video data */
static inline void ch0_set_videobuf_addr(unsigned long top_strt_luma,
					 unsigned long btm_strt_luma,
					 unsigned long top_strt_chroma,
					 unsigned long btm_strt_chroma)
{
	regw(top_strt_luma, VPIF_CH0_TOP_STRT_ADD_LUMA);
	regw(btm_strt_luma, VPIF_CH0_BTM_STRT_ADD_LUMA);
	regw(top_strt_chroma, VPIF_CH0_TOP_STRT_ADD_CHROMA);
	regw(btm_strt_chroma, VPIF_CH0_BTM_STRT_ADD_CHROMA);
}

static inline void ch1_set_videobuf_addr(unsigned long top_strt_luma,
					 unsigned long btm_strt_luma,
					 unsigned long top_strt_chroma,
					 unsigned long btm_strt_chroma)
{

	regw(top_strt_luma, VPIF_CH1_TOP_STRT_ADD_LUMA);
	regw(btm_strt_luma, VPIF_CH1_BTM_STRT_ADD_LUMA);
	regw(top_strt_chroma, VPIF_CH1_TOP_STRT_ADD_CHROMA);
	regw(btm_strt_chroma, VPIF_CH1_BTM_STRT_ADD_CHROMA);
}

static inline void ch0_set_vbi_addr(unsigned long top_vbi,
	unsigned long btm_vbi, unsigned long a, unsigned long b)
{
	regw(top_vbi, VPIF_CH0_TOP_STRT_ADD_VANC);
	regw(btm_vbi, VPIF_CH0_BTM_STRT_ADD_VANC);
}

static inline void ch0_set_hbi_addr(unsigned long top_vbi,
	unsigned long btm_vbi, unsigned long a, unsigned long b)
{
	regw(top_vbi, VPIF_CH0_TOP_STRT_ADD_HANC);
	regw(btm_vbi, VPIF_CH0_BTM_STRT_ADD_HANC);
}

static inline void ch1_set_vbi_addr(unsigned long top_vbi,
	unsigned long btm_vbi, unsigned long a, unsigned long b)
{
	regw(top_vbi, VPIF_CH1_TOP_STRT_ADD_VANC);
	regw(btm_vbi, VPIF_CH1_BTM_STRT_ADD_VANC);
}

static inline void ch1_set_hbi_addr(unsigned long top_vbi,
	unsigned long btm_vbi, unsigned long a, unsigned long b)
{
	regw(top_vbi, VPIF_CH1_TOP_STRT_ADD_HANC);
	regw(btm_vbi, VPIF_CH1_BTM_STRT_ADD_HANC);
}

/* Inline function to enable raw vbi in the given channel */
static inline void disable_raw_feature(u8 channel_id, u8 index)
{
	u32 ctrl_reg;
	if (0 == channel_id)
		ctrl_reg = VPIF_CH0_CTRL;
	else
		ctrl_reg = VPIF_CH1_CTRL;

	if (1 == index)
		vpif_clr_bit(ctrl_reg, VPIF_CH_VANC_EN_BIT);
	else
		vpif_clr_bit(ctrl_reg, VPIF_CH_HANC_EN_BIT);
}

static inline void enable_raw_feature(u8 channel_id, u8 index)
{
	u32 ctrl_reg;
	if (0 == channel_id)
		ctrl_reg = VPIF_CH0_CTRL;
	else
		ctrl_reg = VPIF_CH1_CTRL;

	if (1 == index)
		vpif_set_bit(ctrl_reg, VPIF_CH_VANC_EN_BIT);
	else
		vpif_set_bit(ctrl_reg, VPIF_CH_HANC_EN_BIT);
}

/* inline function to enable/disable channel2 */
static inline void enable_channel2(int enable)
{
	if (enable) {
		regw((regr(VPIF_CH2_CTRL) | (VPIF_CH2_CLK_EN)), VPIF_CH2_CTRL);
		regw((regr(VPIF_CH2_CTRL) | (VPIF_CH2_EN)), VPIF_CH2_CTRL);
	} else {
		regw((regr(VPIF_CH2_CTRL) & (~VPIF_CH2_CLK_EN)), VPIF_CH2_CTRL);
		regw((regr(VPIF_CH2_CTRL) & (~VPIF_CH2_EN)), VPIF_CH2_CTRL);
	}
}

/* inline function to enable/disable channel3 */
static inline void enable_channel3(int enable)
{
	if (enable) {
		regw((regr(VPIF_CH3_CTRL) | (VPIF_CH3_CLK_EN)), VPIF_CH3_CTRL);
		regw((regr(VPIF_CH3_CTRL) | (VPIF_CH3_EN)), VPIF_CH3_CTRL);
	} else {
		regw((regr(VPIF_CH3_CTRL) & (~VPIF_CH3_CLK_EN)), VPIF_CH3_CTRL);
		regw((regr(VPIF_CH3_CTRL) & (~VPIF_CH3_EN)), VPIF_CH3_CTRL);
	}
}

/* inline function to enable interrupt for channel2 */
static inline void channel2_intr_enable(int enable)
{
	unsigned long flags;

	spin_lock_irqsave(&vpif_lock, flags);

	if (enable) {
		regw((regr(VPIF_INTEN) | 0x10), VPIF_INTEN);
		regw((regr(VPIF_INTEN_SET) | 0x10), VPIF_INTEN_SET);
		regw((regr(VPIF_INTEN) | VPIF_INTEN_FRAME_CH2), VPIF_INTEN);
		regw((regr(VPIF_INTEN_SET) | VPIF_INTEN_FRAME_CH2),
							VPIF_INTEN_SET);
	} else {
		regw((regr(VPIF_INTEN) & (~VPIF_INTEN_FRAME_CH2)), VPIF_INTEN);
		regw((regr(VPIF_INTEN_SET) | VPIF_INTEN_FRAME_CH2),
							VPIF_INTEN_SET);
	}
	spin_unlock_irqrestore(&vpif_lock, flags);
}

/* inline function to enable interrupt for channel3 */
static inline void channel3_intr_enable(int enable)
{
	unsigned long flags;

	spin_lock_irqsave(&vpif_lock, flags);

	if (enable) {
		regw((regr(VPIF_INTEN) | 0x10), VPIF_INTEN);
		regw((regr(VPIF_INTEN_SET) | 0x10), VPIF_INTEN_SET);

		regw((regr(VPIF_INTEN) | VPIF_INTEN_FRAME_CH3), VPIF_INTEN);
		regw((regr(VPIF_INTEN_SET) | VPIF_INTEN_FRAME_CH3),
							VPIF_INTEN_SET);
	} else {
		regw((regr(VPIF_INTEN) & (~VPIF_INTEN_FRAME_CH3)), VPIF_INTEN);
		regw((regr(VPIF_INTEN_SET) | VPIF_INTEN_FRAME_CH3),
							VPIF_INTEN_SET);
	}
	spin_unlock_irqrestore(&vpif_lock, flags);
}

/* inline function to enable raw vbi data for channel2 */
static inline void channel2_raw_enable(int enable, u8 index)
{
	u32 mask;

	if (1 == index)
		mask = VPIF_CH_VANC_EN_BIT;
	else
		mask = VPIF_CH_HANC_EN_BIT;

	if (enable)
		vpif_set_bit(VPIF_CH2_CTRL, mask);
	else
		vpif_clr_bit(VPIF_CH2_CTRL, mask);
}

/* inline function to enable raw vbi data for channel3*/
static inline void channel3_raw_enable(int enable, u8 index)
{
	u32 mask;

	if (1 == index)
		mask = VPIF_CH_VANC_EN_BIT;
	else
		mask = VPIF_CH_HANC_EN_BIT;

	if (enable)
		vpif_set_bit(VPIF_CH3_CTRL, mask);
	else
		vpif_clr_bit(VPIF_CH3_CTRL, mask);
}

/* function to enable clipping (for both active and blanking regions) on ch 2 */
static inline void channel2_clipping_enable(int enable)
{
	if (enable) {
		vpif_set_bit(VPIF_CH2_CTRL, VPIF_CH2_CLIP_ANC_EN);
		vpif_set_bit(VPIF_CH2_CTRL, VPIF_CH2_CLIP_ACTIVE_EN);
	} else {
		vpif_clr_bit(VPIF_CH2_CTRL, VPIF_CH2_CLIP_ANC_EN);
		vpif_clr_bit(VPIF_CH2_CTRL, VPIF_CH2_CLIP_ACTIVE_EN);
	}
}

/* function to enable clipping (for both active and blanking regions) on ch 3 */
static inline void channel3_clipping_enable(int enable)
{
	if (enable) {
		vpif_set_bit(VPIF_CH3_CTRL, VPIF_CH3_CLIP_ANC_EN);
		vpif_set_bit(VPIF_CH3_CTRL, VPIF_CH3_CLIP_ACTIVE_EN);
	} else {
		vpif_clr_bit(VPIF_CH3_CTRL, VPIF_CH3_CLIP_ANC_EN);
		vpif_clr_bit(VPIF_CH3_CTRL, VPIF_CH3_CLIP_ACTIVE_EN);
	}
}

/* inline function to set buffer addresses in case of Y/C non mux mode */
static inline void ch2_set_videobuf_addr_yc_nmux(unsigned long top_strt_luma,
						 unsigned long btm_strt_luma,
						 unsigned long top_strt_chroma,
						 unsigned long btm_strt_chroma)
{
	regw(top_strt_luma, VPIF_CH2_TOP_STRT_ADD_LUMA);
	regw(btm_strt_luma, VPIF_CH2_BTM_STRT_ADD_LUMA);
	regw(top_strt_chroma, VPIF_CH3_TOP_STRT_ADD_CHROMA);
	regw(btm_strt_chroma, VPIF_CH3_BTM_STRT_ADD_CHROMA);
}

/* inline function to set buffer addresses in VPIF registers for video data */
static inline void ch2_set_videobuf_addr(unsigned long top_strt_luma,
					 unsigned long btm_strt_luma,
					 unsigned long top_strt_chroma,
					 unsigned long btm_strt_chroma)
{
	regw(top_strt_luma, VPIF_CH2_TOP_STRT_ADD_LUMA);
	regw(btm_strt_luma, VPIF_CH2_BTM_STRT_ADD_LUMA);
	regw(top_strt_chroma, VPIF_CH2_TOP_STRT_ADD_CHROMA);
	regw(btm_strt_chroma, VPIF_CH2_BTM_STRT_ADD_CHROMA);
}

static inline void ch3_set_videobuf_addr(unsigned long top_strt_luma,
					 unsigned long btm_strt_luma,
					 unsigned long top_strt_chroma,
					 unsigned long btm_strt_chroma)
{
	regw(top_strt_luma, VPIF_CH3_TOP_STRT_ADD_LUMA);
	regw(btm_strt_luma, VPIF_CH3_BTM_STRT_ADD_LUMA);
	regw(top_strt_chroma, VPIF_CH3_TOP_STRT_ADD_CHROMA);
	regw(btm_strt_chroma, VPIF_CH3_BTM_STRT_ADD_CHROMA);
}

/* inline function to set buffer addresses in VPIF registers for vbi data */
static inline void ch2_set_vbi_addr(unsigned long top_strt_luma,
					 unsigned long btm_strt_luma,
					 unsigned long top_strt_chroma,
					 unsigned long btm_strt_chroma)
{
	regw(top_strt_luma, VPIF_CH2_TOP_STRT_ADD_VANC);
	regw(btm_strt_luma, VPIF_CH2_BTM_STRT_ADD_VANC);
}

static inline void ch3_set_vbi_addr(unsigned long top_strt_luma,
					 unsigned long btm_strt_luma,
					 unsigned long top_strt_chroma,
					 unsigned long btm_strt_chroma)
{
	regw(top_strt_luma, VPIF_CH3_TOP_STRT_ADD_VANC);
	regw(btm_strt_luma, VPIF_CH3_BTM_STRT_ADD_VANC);
}

static inline int vpif_intr_status(int channel)
{
	int status = 0;
	int mask;

	if (channel < 0 || channel > 3)
		return 0;

	mask = 1 << channel;
	status = regr(VPIF_STATUS) & mask;
	regw(status, VPIF_STATUS_CLR);

	return status;
}

#define VPIF_MAX_NAME	(30)

/* This structure will store size parameters as per the mode selected by user */
struct vpif_channel_config_params {
	char name[VPIF_MAX_NAME];	/* Name of the mode */
	u16 width;			/* Indicates width of the image */
	u16 height;			/* Indicates height of the image */
	u8 frm_fmt;			/* Interlaced (0) or progressive (1) */
	u8 ycmux_mode;			/* This mode requires one (0) or two (1)
					   channels */
	u16 eav2sav;			/* length of eav 2 sav */
	u16 sav2eav;			/* length of sav 2 eav */
	u16 l1, l3, l5, l7, l9, l11;	/* Other parameter configurations */
	u16 vsize;			/* Vertical size of the image */
	u8 capture_format;		/* Indicates whether capture format
					 * is in BT or in CCD/CMOS */
	u8  vbi_supported;		/* Indicates whether this mode
					 * supports capturing vbi or not */
	u8 hd_sd;			/* HDTV (1) or SDTV (0) format */
	v4l2_std_id stdid;		/* SDTV format */
	struct v4l2_dv_timings dv_timings;	/* HDTV format */
};

extern const unsigned int vpif_ch_params_count;
extern const struct vpif_channel_config_params vpif_ch_params[];

struct vpif_video_params;
struct vpif_params;
struct vpif_vbi_params;

int vpif_set_video_params(struct vpif_params *vpifparams, u8 channel_id);
void vpif_set_vbi_display_params(struct vpif_vbi_params *vbiparams,
							u8 channel_id);
int vpif_channel_getfid(u8 channel_id);

enum data_size {
	_8BITS = 0,
	_10BITS,
	_12BITS,
};

/* Structure for vpif parameters for raw vbi data */
struct vpif_vbi_params {
	__u32 hstart0;  /* Horizontal start of raw vbi data for first field */
	__u32 vstart0;  /* Vertical start of raw vbi data for first field */
	__u32 hsize0;   /* Horizontal size of raw vbi data for first field */
	__u32 vsize0;   /* Vertical size of raw vbi data for first field */
	__u32 hstart1;  /* Horizontal start of raw vbi data for second field */
	__u32 vstart1;  /* Vertical start of raw vbi data for second field */
	__u32 hsize1;   /* Horizontal size of raw vbi data for second field */
	__u32 vsize1;   /* Vertical size of raw vbi data for second field */
};

/* structure for vpif parameters */
struct vpif_video_params {
	__u8 storage_mode;	/* Indicates field or frame mode */
	unsigned long hpitch;
	v4l2_std_id stdid;
};

struct vpif_params {
	struct vpif_interface iface;
	struct vpif_video_params video_params;
	struct vpif_channel_config_params std_info;
	union param {
		struct vpif_vbi_params	vbi_params;
		enum data_size data_sz;
	} params;
};

#endif				/* End of #ifndef VPIF_H */
