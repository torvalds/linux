/*
 * Copyright (C) Fuzhou Rockchip Electronics Co.Ltd
 * Author:
 *      algea cao <algea.cao@rock-chips.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#ifndef __ROCKCHIP_DRM_TVE_H__
#define __ROCKCHIP_DRM_TVE_H__

#define RK3036_GRF_SOC_CON3	0x0154
#define RK312X_GRF_TVE_CON	0x0170

#define TV_CTRL			(0x00)
	#define m_CVBS_MODE			BIT(24)
	#define m_CLK_UPSTREAM_EN		(3 << 18)
	#define m_TIMING_EN			(3 << 16)
	#define m_LUMA_FILTER_GAIN		(3 << 9)
	#define m_LUMA_FILTER_BW		BIT(8)
	#define m_CSC_PATH			(3 << 1)

	#define v_CVBS_MODE(x)			(((x) & 1) << 24)
	#define v_CLK_UPSTREAM_EN(x)		(((x) & 3) << 18)
	#define v_TIMING_EN(x)			(((x) & 3) << 16)
	#define v_LUMA_FILTER_GAIN(x)		(((x) & 3) << 9)
	#define v_LUMA_FILTER_UPSAMPLE(x)	(((x) & 1) << 8)
	#define v_CSC_PATH(x)			(((x) & 3) << 1)

#define TV_SYNC_TIMING		(0x04)
#define TV_ACT_TIMING		(0x08)
#define TV_ADJ_TIMING		(0x0c)
#define TV_FREQ_SC		(0x10)
#define TV_LUMA_FILTER0		(0x14)
#define TV_LUMA_FILTER1		(0x18)
#define TV_LUMA_FILTER2		(0x1C)
#define TV_ACT_ST		(0x34)
#define TV_ROUTING		(0x38)
	#define m_DAC_SENSE_EN		BIT(27)
	#define m_Y_IRE_7_5		BIT(19)
	#define m_Y_AGC_PULSE_ON	BIT(15)
	#define m_Y_VIDEO_ON		BIT(11)
	#define m_Y_SYNC_ON		BIT(7)
	#define m_YPP_MODE		BIT(3)
	#define m_MONO_EN		BIT(2)
	#define m_PIC_MODE		BIT(1)

	#define v_DAC_SENSE_EN(x)	(((x) & 1) << 27)
	#define v_Y_IRE_7_5(x)		(((x) & 1) << 19)
	#define v_Y_AGC_PULSE_ON(x)	(((x) & 1) << 15)
	#define v_Y_VIDEO_ON(x)		(((x) & 1) << 11)
	#define v_Y_SYNC_ON(x)		(((x) & 1) << 7)
	#define v_YPP_MODE(x)		(((x) & 1) << 3)
	#define v_MONO_EN(x)		(((x) & 1) << 2)
	#define v_PIC_MODE(x)		(((x) & 1) << 1)

#define TV_SYNC_ADJUST		(0x50)
#define TV_STATUS		(0x54)
#define TV_RESET		(0x68)
	#define m_RESET			BIT(1)
	#define v_RESET(x)		(((x) & 1) << 1)
#define TV_SATURATION		(0x78)
#define TV_BW_CTRL		(0x8C)
	#define m_CHROMA_BW	(3 << 4)
	#define m_COLOR_DIFF_BW	(0xf)

	enum {
		BP_FILTER_PASS = 0,
		BP_FILTER_NTSC,
		BP_FILTER_PAL,
	};
	enum {
		COLOR_DIFF_FILTER_OFF = 0,
		COLOR_DIFF_FILTER_BW_0_6,
		COLOR_DIFF_FILTER_BW_1_3,
		COLOR_DIFF_FILTER_BW_2_0
	};

	#define v_CHROMA_BW(x)		((3 & (x)) << 4)
	#define v_COLOR_DIFF_BW(x)	(0xF & (x))

#define TV_BRIGHTNESS_CONTRAST	(0x90)

#define m_EXTREF_EN		BIT(0)
#define m_VBG_EN		BIT(1)
#define m_DAC_EN		BIT(2)
#define m_SENSE_EN		BIT(3)
#define m_BIAS_EN		(7 << 4)
#define m_DAC_GAIN		(0x3f << 7)
#define v_DAC_GAIN(x)		(((x) & 0x3f) << 7)

#define VDAC_VDAC0             (0x00)
       #define m_RST_ANA               BIT(7)
       #define m_RST_DIG               BIT(6)

       #define v_RST_ANA(x)            (((x) & 1) << 7)
       #define v_RST_DIG(x)            (((x) & 1) << 6)
#define VDAC_VDAC1             (0x280)
       #define m_CUR_REG               (0xf << 4)
       #define m_DR_PWR_DOWN           BIT(1)
       #define m_BG_PWR_DOWN           BIT(0)

       #define v_CUR_REG(x)            (((x) & 0xf) << 4)
       #define v_DR_PWR_DOWN(x)        (((x) & 1) << 1)
       #define v_BG_PWR_DOWN(x)        (((x) & 1) << 0)
#define VDAC_VDAC2             (0x284)
       #define m_CUR_CTR               (0X3f)

       #define v_CUR_CTR(x)            (((x) & 0x3f))
#define VDAC_VDAC3             (0x288)
       #define m_CAB_EN                BIT(5)
       #define m_CAB_REF               BIT(4)
       #define m_CAB_FLAG              BIT(0)

       #define v_CAB_EN(x)             (((x) & 1) << 5)
       #define v_CAB_REF(x)            (((x) & 1) << 4)
       #define v_CAB_FLAG(x)           (((x) & 1) << 0)

enum {
	TVOUT_CVBS_NTSC = 0,
	TVOUT_CVBS_PAL,
};

enum {
	INPUT_FORMAT_RGB = 0,
	INPUT_FORMAT_YUV
};

enum {
	SOC_RK3036 = 0,
	SOC_RK312X,
	SOC_RK322X,
	SOC_RK3328
};

#define grf_writel(offset, v)	do { \
	writel_relaxed(v, RK_GRF_VIRT + (offset)); \
	dsb(sy); \
	} while (0)

struct rockchip_tve {
	struct device *dev;
	struct drm_device *drm_dev;
	struct drm_connector connector;
	struct drm_encoder encoder;

	u32 tv_format;
	void __iomem			*regbase;
	void __iomem			*vdacbase;
	struct clk			*aclk;
	struct clk			*dac_clk;
	struct regmap			*dac_grf;
	u32				reg_phy_base;
	u32				len;
	int				input_format;
	int				soc_type;
	bool				enable;
	u32 test_mode;
	u32 saturation;
	u32 brightcontrast;
	u32 adjtiming;
	u32 lumafilter0;
	u32 lumafilter1;
	u32 lumafilter2;
	u32 daclevel;
	u32 dac1level;
	u32 preferred_mode;
	struct mutex suspend_lock;	/* mutex for tve resume operation*/
	struct rockchip_drm_sub_dev sub_dev;
};

#endif /* _ROCKCHIP_DRM_TVE_ */
