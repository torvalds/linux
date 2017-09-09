/*
 * linux/drivers/video/omap2/dss/dss.h
 *
 * Copyright (C) 2009 Nokia Corporation
 * Author: Tomi Valkeinen <tomi.valkeinen@nokia.com>
 *
 * Some code and ideas taken from drivers/video/omap/ driver
 * by Imre Deak.
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

#ifndef __OMAP2_DSS_H
#define __OMAP2_DSS_H

#include <linux/interrupt.h>

#include "omapdss.h"

#define MAX_DSS_LCD_MANAGERS	3
#define MAX_NUM_DSI		2

#ifdef pr_fmt
#undef pr_fmt
#endif

#ifdef DSS_SUBSYS_NAME
#define pr_fmt(fmt) DSS_SUBSYS_NAME ": " fmt
#else
#define pr_fmt(fmt) fmt
#endif

#define DSSDBG(format, ...) \
	pr_debug(format, ## __VA_ARGS__)

#ifdef DSS_SUBSYS_NAME
#define DSSERR(format, ...) \
	pr_err("omapdss " DSS_SUBSYS_NAME " error: " format, ##__VA_ARGS__)
#else
#define DSSERR(format, ...) \
	pr_err("omapdss error: " format, ##__VA_ARGS__)
#endif

#ifdef DSS_SUBSYS_NAME
#define DSSINFO(format, ...) \
	pr_info("omapdss " DSS_SUBSYS_NAME ": " format, ##__VA_ARGS__)
#else
#define DSSINFO(format, ...) \
	pr_info("omapdss: " format, ## __VA_ARGS__)
#endif

#ifdef DSS_SUBSYS_NAME
#define DSSWARN(format, ...) \
	pr_warn("omapdss " DSS_SUBSYS_NAME ": " format, ##__VA_ARGS__)
#else
#define DSSWARN(format, ...) \
	pr_warn("omapdss: " format, ##__VA_ARGS__)
#endif

/* OMAP TRM gives bitfields as start:end, where start is the higher bit
   number. For example 7:0 */
#define FLD_MASK(start, end)	(((1 << ((start) - (end) + 1)) - 1) << (end))
#define FLD_VAL(val, start, end) (((val) << (end)) & FLD_MASK(start, end))
#define FLD_GET(val, start, end) (((val) & FLD_MASK(start, end)) >> (end))
#define FLD_MOD(orig, val, start, end) \
	(((orig) & ~FLD_MASK(start, end)) | FLD_VAL(val, start, end))

enum dss_model {
	DSS_MODEL_OMAP2,
	DSS_MODEL_OMAP3,
	DSS_MODEL_OMAP4,
	DSS_MODEL_OMAP5,
	DSS_MODEL_DRA7,
};

enum dss_io_pad_mode {
	DSS_IO_PAD_MODE_RESET,
	DSS_IO_PAD_MODE_RFBI,
	DSS_IO_PAD_MODE_BYPASS,
};

enum dss_hdmi_venc_clk_source_select {
	DSS_VENC_TV_CLK = 0,
	DSS_HDMI_M_PCLK = 1,
};

enum dss_dsi_content_type {
	DSS_DSI_CONTENT_DCS,
	DSS_DSI_CONTENT_GENERIC,
};

enum dss_writeback_channel {
	DSS_WB_LCD1_MGR =	0,
	DSS_WB_LCD2_MGR =	1,
	DSS_WB_TV_MGR =		2,
	DSS_WB_OVL0 =		3,
	DSS_WB_OVL1 =		4,
	DSS_WB_OVL2 =		5,
	DSS_WB_OVL3 =		6,
	DSS_WB_LCD3_MGR =	7,
};

enum dss_clk_source {
	DSS_CLK_SRC_FCK = 0,

	DSS_CLK_SRC_PLL1_1,
	DSS_CLK_SRC_PLL1_2,
	DSS_CLK_SRC_PLL1_3,

	DSS_CLK_SRC_PLL2_1,
	DSS_CLK_SRC_PLL2_2,
	DSS_CLK_SRC_PLL2_3,

	DSS_CLK_SRC_HDMI_PLL,
};

enum dss_pll_id {
	DSS_PLL_DSI1,
	DSS_PLL_DSI2,
	DSS_PLL_HDMI,
	DSS_PLL_VIDEO1,
	DSS_PLL_VIDEO2,
};

struct dss_pll;

#define DSS_PLL_MAX_HSDIVS 4

enum dss_pll_type {
	DSS_PLL_TYPE_A,
	DSS_PLL_TYPE_B,
};

/*
 * Type-A PLLs: clkout[]/mX[] refer to hsdiv outputs m4, m5, m6, m7.
 * Type-B PLLs: clkout[0] refers to m2.
 */
struct dss_pll_clock_info {
	/* rates that we get with dividers below */
	unsigned long fint;
	unsigned long clkdco;
	unsigned long clkout[DSS_PLL_MAX_HSDIVS];

	/* dividers */
	u16 n;
	u16 m;
	u32 mf;
	u16 mX[DSS_PLL_MAX_HSDIVS];
	u16 sd;
};

struct dss_pll_ops {
	int (*enable)(struct dss_pll *pll);
	void (*disable)(struct dss_pll *pll);
	int (*set_config)(struct dss_pll *pll,
		const struct dss_pll_clock_info *cinfo);
};

struct dss_pll_hw {
	enum dss_pll_type type;

	unsigned n_max;
	unsigned m_min;
	unsigned m_max;
	unsigned mX_max;

	unsigned long fint_min, fint_max;
	unsigned long clkdco_min, clkdco_low, clkdco_max;

	u8 n_msb, n_lsb;
	u8 m_msb, m_lsb;
	u8 mX_msb[DSS_PLL_MAX_HSDIVS], mX_lsb[DSS_PLL_MAX_HSDIVS];

	bool has_stopmode;
	bool has_freqsel;
	bool has_selfreqdco;
	bool has_refsel;

	/* DRA7 errata i886: use high N & M to avoid jitter */
	bool errata_i886;
};

struct dss_pll {
	const char *name;
	enum dss_pll_id id;

	struct clk *clkin;
	struct regulator *regulator;

	void __iomem *base;

	const struct dss_pll_hw *hw;

	const struct dss_pll_ops *ops;

	struct dss_pll_clock_info cinfo;
};

/* Defines a generic omap register field */
struct dss_reg_field {
	u8 start, end;
};

struct dispc_clock_info {
	/* rates that we get with dividers below */
	unsigned long lck;
	unsigned long pck;

	/* dividers */
	u16 lck_div;
	u16 pck_div;
};

struct dss_lcd_mgr_config {
	enum dss_io_pad_mode io_pad_mode;

	bool stallmode;
	bool fifohandcheck;

	struct dispc_clock_info clock_info;

	int video_port_width;

	int lcden_sig_polarity;
};

struct seq_file;
struct platform_device;

/* core */
static inline int dss_set_min_bus_tput(struct device *dev, unsigned long tput)
{
	/* To be implemented when the OMAP platform will provide this feature */
	return 0;
}

static inline bool dss_mgr_is_lcd(enum omap_channel id)
{
	if (id == OMAP_DSS_CHANNEL_LCD || id == OMAP_DSS_CHANNEL_LCD2 ||
			id == OMAP_DSS_CHANNEL_LCD3)
		return true;
	else
		return false;
}

/* DSS */
#if defined(CONFIG_OMAP2_DSS_DEBUGFS)
int dss_debugfs_create_file(const char *name, void (*write)(struct seq_file *));
#else
static inline int dss_debugfs_create_file(const char *name,
					  void (*write)(struct seq_file *))
{
	return 0;
}
#endif /* CONFIG_OMAP2_DSS_DEBUGFS */

int dss_init_platform_driver(void) __init;
void dss_uninit_platform_driver(void);

int dss_runtime_get(void);
void dss_runtime_put(void);

unsigned long dss_get_dispc_clk_rate(void);
unsigned long dss_get_max_fck_rate(void);
enum omap_dss_output_id dss_get_supported_outputs(enum omap_channel channel);
int dss_dpi_select_source(int port, enum omap_channel channel);
void dss_select_hdmi_venc_clk_source(enum dss_hdmi_venc_clk_source_select);
enum dss_hdmi_venc_clk_source_select dss_get_hdmi_venc_clk_source(void);
const char *dss_get_clk_source_name(enum dss_clk_source clk_src);
void dss_dump_clocks(struct seq_file *s);

/* DSS VIDEO PLL */
struct dss_pll *dss_video_pll_init(struct platform_device *pdev, int id,
	struct regulator *regulator);
void dss_video_pll_uninit(struct dss_pll *pll);

void dss_ctrl_pll_enable(enum dss_pll_id pll_id, bool enable);

void dss_sdi_init(int datapairs);
int dss_sdi_enable(void);
void dss_sdi_disable(void);

void dss_select_dsi_clk_source(int dsi_module,
		enum dss_clk_source clk_src);
void dss_select_lcd_clk_source(enum omap_channel channel,
		enum dss_clk_source clk_src);
enum dss_clk_source dss_get_dispc_clk_source(void);
enum dss_clk_source dss_get_dsi_clk_source(int dsi_module);
enum dss_clk_source dss_get_lcd_clk_source(enum omap_channel channel);

void dss_set_venc_output(enum omap_dss_venc_type type);
void dss_set_dac_pwrdn_bgz(bool enable);

int dss_set_fck_rate(unsigned long rate);

typedef bool (*dss_div_calc_func)(unsigned long fck, void *data);
bool dss_div_calc(unsigned long pck, unsigned long fck_min,
		dss_div_calc_func func, void *data);

/* SDI */
#ifdef CONFIG_OMAP2_DSS_SDI
int sdi_init_port(struct platform_device *pdev, struct device_node *port);
void sdi_uninit_port(struct device_node *port);
#else
static inline int sdi_init_port(struct platform_device *pdev,
		struct device_node *port)
{
	return 0;
}
static inline void sdi_uninit_port(struct device_node *port)
{
}
#endif

/* DSI */

#ifdef CONFIG_OMAP2_DSS_DSI

struct dentry;
struct file_operations;

int dsi_init_platform_driver(void) __init;
void dsi_uninit_platform_driver(void);

void dsi_dump_clocks(struct seq_file *s);

void dsi_irq_handler(void);

#endif

/* DPI */
#ifdef CONFIG_OMAP2_DSS_DPI
int dpi_init_port(struct platform_device *pdev, struct device_node *port,
		  enum dss_model dss_model);
void dpi_uninit_port(struct device_node *port);
#else
static inline int dpi_init_port(struct platform_device *pdev,
		struct device_node *port, enum dss_model dss_model)
{
	return 0;
}
static inline void dpi_uninit_port(struct device_node *port)
{
}
#endif

/* DISPC */
int dispc_init_platform_driver(void) __init;
void dispc_uninit_platform_driver(void);
void dispc_dump_clocks(struct seq_file *s);

int dispc_runtime_get(void);
void dispc_runtime_put(void);

void dispc_enable_sidle(void);
void dispc_disable_sidle(void);

void dispc_lcd_enable_signal(bool enable);
void dispc_pck_free_enable(bool enable);
void dispc_enable_fifomerge(bool enable);
void dispc_enable_gamma_table(bool enable);

typedef bool (*dispc_div_calc_func)(int lckd, int pckd, unsigned long lck,
		unsigned long pck, void *data);
bool dispc_div_calc(unsigned long dispc,
		unsigned long pck_min, unsigned long pck_max,
		dispc_div_calc_func func, void *data);

bool dispc_mgr_timings_ok(enum omap_channel channel, const struct videomode *vm);
int dispc_calc_clock_rates(unsigned long dispc_fclk_rate,
		struct dispc_clock_info *cinfo);


void dispc_ovl_set_fifo_threshold(enum omap_plane_id plane, u32 low,
				  u32 high);
void dispc_ovl_compute_fifo_thresholds(enum omap_plane_id plane,
		u32 *fifo_low, u32 *fifo_high, bool use_fifomerge,
		bool manual_update);

void dispc_mgr_set_clock_div(enum omap_channel channel,
		const struct dispc_clock_info *cinfo);
int dispc_mgr_get_clock_div(enum omap_channel channel,
		struct dispc_clock_info *cinfo);
void dispc_set_tv_pclk(unsigned long pclk);

u32 dispc_wb_get_framedone_irq(void);
bool dispc_wb_go_busy(void);
void dispc_wb_go(void);
void dispc_wb_set_channel_in(enum dss_writeback_channel channel);
int dispc_wb_setup(const struct omap_dss_writeback_info *wi,
		bool mem_to_mem, const struct videomode *vm);

/* VENC */
int venc_init_platform_driver(void) __init;
void venc_uninit_platform_driver(void);

/* HDMI */
int hdmi4_init_platform_driver(void) __init;
void hdmi4_uninit_platform_driver(void);

int hdmi5_init_platform_driver(void) __init;
void hdmi5_uninit_platform_driver(void);


#ifdef CONFIG_OMAP2_DSS_COLLECT_IRQ_STATS
static inline void dss_collect_irq_stats(u32 irqstatus, unsigned *irq_arr)
{
	int b;
	for (b = 0; b < 32; ++b) {
		if (irqstatus & (1 << b))
			irq_arr[b]++;
	}
}
#endif

/* PLL */
typedef bool (*dss_pll_calc_func)(int n, int m, unsigned long fint,
		unsigned long clkdco, void *data);
typedef bool (*dss_hsdiv_calc_func)(int m_dispc, unsigned long dispc,
		void *data);

int dss_pll_register(struct dss_pll *pll);
void dss_pll_unregister(struct dss_pll *pll);
struct dss_pll *dss_pll_find(const char *name);
struct dss_pll *dss_pll_find_by_src(enum dss_clk_source src);
unsigned dss_pll_get_clkout_idx_for_src(enum dss_clk_source src);
int dss_pll_enable(struct dss_pll *pll);
void dss_pll_disable(struct dss_pll *pll);
int dss_pll_set_config(struct dss_pll *pll,
		const struct dss_pll_clock_info *cinfo);

bool dss_pll_hsdiv_calc_a(const struct dss_pll *pll, unsigned long clkdco,
		unsigned long out_min, unsigned long out_max,
		dss_hsdiv_calc_func func, void *data);
bool dss_pll_calc_a(const struct dss_pll *pll, unsigned long clkin,
		unsigned long pll_min, unsigned long pll_max,
		dss_pll_calc_func func, void *data);

bool dss_pll_calc_b(const struct dss_pll *pll, unsigned long clkin,
	unsigned long target_clkout, struct dss_pll_clock_info *cinfo);

int dss_pll_write_config_type_a(struct dss_pll *pll,
		const struct dss_pll_clock_info *cinfo);
int dss_pll_write_config_type_b(struct dss_pll *pll,
		const struct dss_pll_clock_info *cinfo);
int dss_pll_wait_reset_done(struct dss_pll *pll);

#endif
