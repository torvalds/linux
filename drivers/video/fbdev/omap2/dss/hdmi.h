/*
 * HDMI driver definition for TI OMAP4 Processor.
 *
 * Copyright (C) 2010-2011 Texas Instruments Incorporated - http://www.ti.com/
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _HDMI_H
#define _HDMI_H

#include <linux/delay.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/hdmi.h>
#include <video/omapdss.h>

#include "dss.h"

/* HDMI Wrapper */

#define HDMI_WP_REVISION			0x0
#define HDMI_WP_SYSCONFIG			0x10
#define HDMI_WP_IRQSTATUS_RAW			0x24
#define HDMI_WP_IRQSTATUS			0x28
#define HDMI_WP_IRQENABLE_SET			0x2C
#define HDMI_WP_IRQENABLE_CLR			0x30
#define HDMI_WP_IRQWAKEEN			0x34
#define HDMI_WP_PWR_CTRL			0x40
#define HDMI_WP_DEBOUNCE			0x44
#define HDMI_WP_VIDEO_CFG			0x50
#define HDMI_WP_VIDEO_SIZE			0x60
#define HDMI_WP_VIDEO_TIMING_H			0x68
#define HDMI_WP_VIDEO_TIMING_V			0x6C
#define HDMI_WP_CLK				0x70
#define HDMI_WP_AUDIO_CFG			0x80
#define HDMI_WP_AUDIO_CFG2			0x84
#define HDMI_WP_AUDIO_CTRL			0x88
#define HDMI_WP_AUDIO_DATA			0x8C

/* HDMI WP IRQ flags */
#define HDMI_IRQ_CORE				(1 << 0)
#define HDMI_IRQ_OCP_TIMEOUT			(1 << 4)
#define HDMI_IRQ_AUDIO_FIFO_UNDERFLOW		(1 << 8)
#define HDMI_IRQ_AUDIO_FIFO_OVERFLOW		(1 << 9)
#define HDMI_IRQ_AUDIO_FIFO_SAMPLE_REQ		(1 << 10)
#define HDMI_IRQ_VIDEO_VSYNC			(1 << 16)
#define HDMI_IRQ_VIDEO_FRAME_DONE		(1 << 17)
#define HDMI_IRQ_PHY_LINE5V_ASSERT		(1 << 24)
#define HDMI_IRQ_LINK_CONNECT			(1 << 25)
#define HDMI_IRQ_LINK_DISCONNECT		(1 << 26)
#define HDMI_IRQ_PLL_LOCK			(1 << 29)
#define HDMI_IRQ_PLL_UNLOCK			(1 << 30)
#define HDMI_IRQ_PLL_RECAL			(1 << 31)

/* HDMI PLL */

#define PLLCTRL_PLL_CONTROL			0x0
#define PLLCTRL_PLL_STATUS			0x4
#define PLLCTRL_PLL_GO				0x8
#define PLLCTRL_CFG1				0xC
#define PLLCTRL_CFG2				0x10
#define PLLCTRL_CFG3				0x14
#define PLLCTRL_SSC_CFG1			0x18
#define PLLCTRL_SSC_CFG2			0x1C
#define PLLCTRL_CFG4				0x20

/* HDMI PHY */

#define HDMI_TXPHY_TX_CTRL			0x0
#define HDMI_TXPHY_DIGITAL_CTRL			0x4
#define HDMI_TXPHY_POWER_CTRL			0x8
#define HDMI_TXPHY_PAD_CFG_CTRL			0xC
#define HDMI_TXPHY_BIST_CONTROL			0x1C

enum hdmi_pll_pwr {
	HDMI_PLLPWRCMD_ALLOFF = 0,
	HDMI_PLLPWRCMD_PLLONLY = 1,
	HDMI_PLLPWRCMD_BOTHON_ALLCLKS = 2,
	HDMI_PLLPWRCMD_BOTHON_NOPHYCLK = 3
};

enum hdmi_phy_pwr {
	HDMI_PHYPWRCMD_OFF = 0,
	HDMI_PHYPWRCMD_LDOON = 1,
	HDMI_PHYPWRCMD_TXON = 2
};

enum hdmi_core_hdmi_dvi {
	HDMI_DVI = 0,
	HDMI_HDMI = 1
};

enum hdmi_clk_refsel {
	HDMI_REFSEL_PCLK = 0,
	HDMI_REFSEL_REF1 = 1,
	HDMI_REFSEL_REF2 = 2,
	HDMI_REFSEL_SYSCLK = 3
};

enum hdmi_packing_mode {
	HDMI_PACK_10b_RGB_YUV444 = 0,
	HDMI_PACK_24b_RGB_YUV444_YUV422 = 1,
	HDMI_PACK_20b_YUV422 = 2,
	HDMI_PACK_ALREADYPACKED = 7
};

enum hdmi_stereo_channels {
	HDMI_AUDIO_STEREO_NOCHANNELS = 0,
	HDMI_AUDIO_STEREO_ONECHANNEL = 1,
	HDMI_AUDIO_STEREO_TWOCHANNELS = 2,
	HDMI_AUDIO_STEREO_THREECHANNELS = 3,
	HDMI_AUDIO_STEREO_FOURCHANNELS = 4
};

enum hdmi_audio_type {
	HDMI_AUDIO_TYPE_LPCM = 0,
	HDMI_AUDIO_TYPE_IEC = 1
};

enum hdmi_audio_justify {
	HDMI_AUDIO_JUSTIFY_LEFT = 0,
	HDMI_AUDIO_JUSTIFY_RIGHT = 1
};

enum hdmi_audio_sample_order {
	HDMI_AUDIO_SAMPLE_RIGHT_FIRST = 0,
	HDMI_AUDIO_SAMPLE_LEFT_FIRST = 1
};

enum hdmi_audio_samples_perword {
	HDMI_AUDIO_ONEWORD_ONESAMPLE = 0,
	HDMI_AUDIO_ONEWORD_TWOSAMPLES = 1
};

enum hdmi_audio_sample_size_omap {
	HDMI_AUDIO_SAMPLE_16BITS = 0,
	HDMI_AUDIO_SAMPLE_24BITS = 1
};

enum hdmi_audio_transf_mode {
	HDMI_AUDIO_TRANSF_DMA = 0,
	HDMI_AUDIO_TRANSF_IRQ = 1
};

enum hdmi_audio_blk_strt_end_sig {
	HDMI_AUDIO_BLOCK_SIG_STARTEND_ON = 0,
	HDMI_AUDIO_BLOCK_SIG_STARTEND_OFF = 1
};

enum hdmi_core_audio_layout {
	HDMI_AUDIO_LAYOUT_2CH = 0,
	HDMI_AUDIO_LAYOUT_8CH = 1
};

enum hdmi_core_cts_mode {
	HDMI_AUDIO_CTS_MODE_HW = 0,
	HDMI_AUDIO_CTS_MODE_SW = 1
};

enum hdmi_audio_mclk_mode {
	HDMI_AUDIO_MCLK_128FS = 0,
	HDMI_AUDIO_MCLK_256FS = 1,
	HDMI_AUDIO_MCLK_384FS = 2,
	HDMI_AUDIO_MCLK_512FS = 3,
	HDMI_AUDIO_MCLK_768FS = 4,
	HDMI_AUDIO_MCLK_1024FS = 5,
	HDMI_AUDIO_MCLK_1152FS = 6,
	HDMI_AUDIO_MCLK_192FS = 7
};

struct hdmi_video_format {
	enum hdmi_packing_mode	packing_mode;
	u32			y_res;	/* Line per panel */
	u32			x_res;	/* pixel per line */
};

struct hdmi_config {
	struct omap_video_timings timings;
	struct hdmi_avi_infoframe infoframe;
	enum hdmi_core_hdmi_dvi hdmi_dvi_mode;
};

/* HDMI PLL structure */
struct hdmi_pll_info {
	u16 regn;
	u16 regm;
	u32 regmf;
	u16 regm2;
	u16 regsd;
	u16 dcofreq;
	enum hdmi_clk_refsel refsel;
};

struct hdmi_audio_format {
	enum hdmi_stereo_channels		stereo_channels;
	u8					active_chnnls_msk;
	enum hdmi_audio_type			type;
	enum hdmi_audio_justify			justification;
	enum hdmi_audio_sample_order		sample_order;
	enum hdmi_audio_samples_perword		samples_per_word;
	enum hdmi_audio_sample_size_omap	sample_size;
	enum hdmi_audio_blk_strt_end_sig	en_sig_blk_strt_end;
};

struct hdmi_audio_dma {
	u8				transfer_size;
	u8				block_size;
	enum hdmi_audio_transf_mode	mode;
	u16				fifo_threshold;
};

struct hdmi_core_audio_i2s_config {
	u8 in_length_bits;
	u8 justification;
	u8 sck_edge_mode;
	u8 vbit;
	u8 direction;
	u8 shift;
	u8 active_sds;
};

struct hdmi_core_audio_config {
	struct hdmi_core_audio_i2s_config	i2s_cfg;
	struct snd_aes_iec958			*iec60958_cfg;
	bool					fs_override;
	u32					n;
	u32					cts;
	u32					aud_par_busclk;
	enum hdmi_core_audio_layout		layout;
	enum hdmi_core_cts_mode			cts_mode;
	bool					use_mclk;
	enum hdmi_audio_mclk_mode		mclk_mode;
	bool					en_acr_pkt;
	bool					en_dsd_audio;
	bool					en_parallel_aud_input;
	bool					en_spdif;
};

struct hdmi_wp_data {
	void __iomem *base;
};

struct hdmi_pll_data {
	void __iomem *base;

	struct hdmi_pll_info info;
};

struct hdmi_phy_data {
	void __iomem *base;

	u8 lane_function[4];
	u8 lane_polarity[4];
};

struct hdmi_core_data {
	void __iomem *base;
};

static inline void hdmi_write_reg(void __iomem *base_addr, const u32 idx,
		u32 val)
{
	__raw_writel(val, base_addr + idx);
}

static inline u32 hdmi_read_reg(void __iomem *base_addr, const u32 idx)
{
	return __raw_readl(base_addr + idx);
}

#define REG_FLD_MOD(base, idx, val, start, end) \
	hdmi_write_reg(base, idx, FLD_MOD(hdmi_read_reg(base, idx),\
							val, start, end))
#define REG_GET(base, idx, start, end) \
	FLD_GET(hdmi_read_reg(base, idx), start, end)

static inline int hdmi_wait_for_bit_change(void __iomem *base_addr,
		const u32 idx, int b2, int b1, u32 val)
{
	u32 t = 0, v;
	while (val != (v = REG_GET(base_addr, idx, b2, b1))) {
		if (t++ > 10000)
			return v;
		udelay(1);
	}
	return v;
}

/* HDMI wrapper funcs */
int hdmi_wp_video_start(struct hdmi_wp_data *wp);
void hdmi_wp_video_stop(struct hdmi_wp_data *wp);
void hdmi_wp_dump(struct hdmi_wp_data *wp, struct seq_file *s);
u32 hdmi_wp_get_irqstatus(struct hdmi_wp_data *wp);
void hdmi_wp_set_irqstatus(struct hdmi_wp_data *wp, u32 irqstatus);
void hdmi_wp_set_irqenable(struct hdmi_wp_data *wp, u32 mask);
void hdmi_wp_clear_irqenable(struct hdmi_wp_data *wp, u32 mask);
int hdmi_wp_set_phy_pwr(struct hdmi_wp_data *wp, enum hdmi_phy_pwr val);
int hdmi_wp_set_pll_pwr(struct hdmi_wp_data *wp, enum hdmi_pll_pwr val);
void hdmi_wp_video_config_format(struct hdmi_wp_data *wp,
		struct hdmi_video_format *video_fmt);
void hdmi_wp_video_config_interface(struct hdmi_wp_data *wp,
		struct omap_video_timings *timings);
void hdmi_wp_video_config_timing(struct hdmi_wp_data *wp,
		struct omap_video_timings *timings);
void hdmi_wp_init_vid_fmt_timings(struct hdmi_video_format *video_fmt,
		struct omap_video_timings *timings, struct hdmi_config *param);
int hdmi_wp_init(struct platform_device *pdev, struct hdmi_wp_data *wp);

/* HDMI PLL funcs */
int hdmi_pll_enable(struct hdmi_pll_data *pll, struct hdmi_wp_data *wp);
void hdmi_pll_disable(struct hdmi_pll_data *pll, struct hdmi_wp_data *wp);
void hdmi_pll_dump(struct hdmi_pll_data *pll, struct seq_file *s);
void hdmi_pll_compute(struct hdmi_pll_data *pll, unsigned long clkin, int phy);
int hdmi_pll_init(struct platform_device *pdev, struct hdmi_pll_data *pll);

/* HDMI PHY funcs */
int hdmi_phy_configure(struct hdmi_phy_data *phy, struct hdmi_config *cfg);
void hdmi_phy_dump(struct hdmi_phy_data *phy, struct seq_file *s);
int hdmi_phy_init(struct platform_device *pdev, struct hdmi_phy_data *phy);
int hdmi_phy_parse_lanes(struct hdmi_phy_data *phy, const u32 *lanes);

/* HDMI common funcs */
int hdmi_parse_lanes_of(struct platform_device *pdev, struct device_node *ep,
	struct hdmi_phy_data *phy);

#if defined(CONFIG_OMAP4_DSS_HDMI_AUDIO) || defined(CONFIG_OMAP5_DSS_HDMI_AUDIO)
int hdmi_compute_acr(u32 pclk, u32 sample_freq, u32 *n, u32 *cts);
int hdmi_wp_audio_enable(struct hdmi_wp_data *wp, bool enable);
int hdmi_wp_audio_core_req_enable(struct hdmi_wp_data *wp, bool enable);
void hdmi_wp_audio_config_format(struct hdmi_wp_data *wp,
		struct hdmi_audio_format *aud_fmt);
void hdmi_wp_audio_config_dma(struct hdmi_wp_data *wp,
		struct hdmi_audio_dma *aud_dma);
static inline bool hdmi_mode_has_audio(int mode)
{
	return mode == HDMI_HDMI ? true : false;
}
#endif
#endif
