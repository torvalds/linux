/*
 * ti_hdmi.h
 *
 * HDMI driver definition for TI OMAP4, DM81xx, DM38xx  Processor.
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

#ifndef _TI_HDMI_H
#define _TI_HDMI_H

struct hdmi_ip_data;

enum hdmi_pll_pwr {
	HDMI_PLLPWRCMD_ALLOFF = 0,
	HDMI_PLLPWRCMD_PLLONLY = 1,
	HDMI_PLLPWRCMD_BOTHON_ALLCLKS = 2,
	HDMI_PLLPWRCMD_BOTHON_NOPHYCLK = 3
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

struct hdmi_video_timings {
	u16 x_res;
	u16 y_res;
	/* Unit: KHz */
	u32 pixel_clock;
	u16 hsw;
	u16 hfp;
	u16 hbp;
	u16 vsw;
	u16 vfp;
	u16 vbp;
};

/* HDMI timing structure */
struct hdmi_timings {
	struct hdmi_video_timings timings;
	int vsync_pol;
	int hsync_pol;
};

struct hdmi_cm {
	int	code;
	int	mode;
};

struct hdmi_config {
	struct hdmi_timings timings;
	u16	interlace;
	struct hdmi_cm cm;
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

struct ti_hdmi_ip_ops {

	void (*video_configure)(struct hdmi_ip_data *ip_data);

	int (*phy_enable)(struct hdmi_ip_data *ip_data);

	void (*phy_disable)(struct hdmi_ip_data *ip_data);

	int (*read_edid)(struct hdmi_ip_data *ip_data, u8 *edid, int len);

	bool (*detect)(struct hdmi_ip_data *ip_data);

	int (*pll_enable)(struct hdmi_ip_data *ip_data);

	void (*pll_disable)(struct hdmi_ip_data *ip_data);

	void (*video_enable)(struct hdmi_ip_data *ip_data, bool start);

	void (*dump_wrapper)(struct hdmi_ip_data *ip_data, struct seq_file *s);

	void (*dump_core)(struct hdmi_ip_data *ip_data, struct seq_file *s);

	void (*dump_pll)(struct hdmi_ip_data *ip_data, struct seq_file *s);

	void (*dump_phy)(struct hdmi_ip_data *ip_data, struct seq_file *s);

#if defined(CONFIG_SND_OMAP_SOC_OMAP4_HDMI) || \
	defined(CONFIG_SND_OMAP_SOC_OMAP4_HDMI_MODULE)
	void (*audio_enable)(struct hdmi_ip_data *ip_data, bool start);
#endif

};

struct hdmi_ip_data {
	void __iomem	*base_wp;	/* HDMI wrapper */
	unsigned long	core_sys_offset;
	unsigned long	core_av_offset;
	unsigned long	pll_offset;
	unsigned long	phy_offset;
	const struct ti_hdmi_ip_ops *ops;
	struct hdmi_config cfg;
	struct hdmi_pll_info pll_data;
};
int ti_hdmi_4xxx_phy_enable(struct hdmi_ip_data *ip_data);
void ti_hdmi_4xxx_phy_disable(struct hdmi_ip_data *ip_data);
int ti_hdmi_4xxx_read_edid(struct hdmi_ip_data *ip_data, u8 *edid, int len);
bool ti_hdmi_4xxx_detect(struct hdmi_ip_data *ip_data);
void ti_hdmi_4xxx_wp_video_start(struct hdmi_ip_data *ip_data, bool start);
int ti_hdmi_4xxx_pll_enable(struct hdmi_ip_data *ip_data);
void ti_hdmi_4xxx_pll_disable(struct hdmi_ip_data *ip_data);
void ti_hdmi_4xxx_basic_configure(struct hdmi_ip_data *ip_data);
void ti_hdmi_4xxx_wp_dump(struct hdmi_ip_data *ip_data, struct seq_file *s);
void ti_hdmi_4xxx_pll_dump(struct hdmi_ip_data *ip_data, struct seq_file *s);
void ti_hdmi_4xxx_core_dump(struct hdmi_ip_data *ip_data, struct seq_file *s);
void ti_hdmi_4xxx_phy_dump(struct hdmi_ip_data *ip_data, struct seq_file *s);
#if defined(CONFIG_SND_OMAP_SOC_OMAP4_HDMI) || \
	defined(CONFIG_SND_OMAP_SOC_OMAP4_HDMI_MODULE)
void ti_hdmi_4xxx_wp_audio_enable(struct hdmi_ip_data *ip_data, bool enable);
#endif
#endif
