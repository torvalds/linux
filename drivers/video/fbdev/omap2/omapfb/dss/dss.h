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
	printk(KERN_ERR "omapdss " DSS_SUBSYS_NAME " error: " format, \
	## __VA_ARGS__)
#else
#define DSSERR(format, ...) \
	printk(KERN_ERR "omapdss error: " format, ## __VA_ARGS__)
#endif

#ifdef DSS_SUBSYS_NAME
#define DSSINFO(format, ...) \
	printk(KERN_INFO "omapdss " DSS_SUBSYS_NAME ": " format, \
	## __VA_ARGS__)
#else
#define DSSINFO(format, ...) \
	printk(KERN_INFO "omapdss: " format, ## __VA_ARGS__)
#endif

#ifdef DSS_SUBSYS_NAME
#define DSSWARN(format, ...) \
	printk(KERN_WARNING "omapdss " DSS_SUBSYS_NAME ": " format, \
	## __VA_ARGS__)
#else
#define DSSWARN(format, ...) \
	printk(KERN_WARNING "omapdss: " format, ## __VA_ARGS__)
#endif

/* OMAP TRM gives bitfields as start:end, where start is the higher bit
   number. For example 7:0 */
#define FLD_MASK(start, end)	(((1 << ((start) - (end) + 1)) - 1) << (end))
#define FLD_VAL(val, start, end) (((val) << (end)) & FLD_MASK(start, end))
#define FLD_GET(val, start, end) (((val) & FLD_MASK(start, end)) >> (end))
#define FLD_MOD(orig, val, start, end) \
	(((orig) & ~FLD_MASK(start, end)) | FLD_VAL(val, start, end))

enum omap_dss_clk_source {
	OMAP_DSS_CLK_SRC_FCK = 0,		/* OMAP2/3: DSS1_ALWON_FCLK
						 * OMAP4: DSS_FCLK */
	OMAP_DSS_CLK_SRC_DSI_PLL_HSDIV_DISPC,	/* OMAP3: DSI1_PLL_FCLK
						 * OMAP4: PLL1_CLK1 */
	OMAP_DSS_CLK_SRC_DSI_PLL_HSDIV_DSI,	/* OMAP3: DSI2_PLL_FCLK
						 * OMAP4: PLL1_CLK2 */
	OMAP_DSS_CLK_SRC_DSI2_PLL_HSDIV_DISPC,	/* OMAP4: PLL2_CLK1 */
	OMAP_DSS_CLK_SRC_DSI2_PLL_HSDIV_DSI,	/* OMAP4: PLL2_CLK2 */
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

enum dss_pll_id {
	DSS_PLL_DSI1,
	DSS_PLL_DSI2,
	DSS_PLL_HDMI,
	DSS_PLL_VIDEO1,
	DSS_PLL_VIDEO2,
};

struct dss_pll;

#define DSS_PLL_MAX_HSDIVS 4

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
struct platform_device *dss_get_core_pdev(void);
int dss_dsi_enable_pads(int dsi_id, unsigned lane_mask);
void dss_dsi_disable_pads(int dsi_id, unsigned lane_mask);
int dss_set_min_bus_tput(struct device *dev, unsigned long tput);
void dss_debugfs_create_file(const char *name, void (*write)(struct seq_file *));

/* display */
int dss_suspend_all_devices(void);
int dss_resume_all_devices(void);
void dss_disable_all_devices(void);

int display_init_sysfs(struct platform_device *pdev);
void display_uninit_sysfs(struct platform_device *pdev);

/* manager */
int dss_init_overlay_managers(void);
void dss_uninit_overlay_managers(void);
int dss_init_overlay_managers_sysfs(struct platform_device *pdev);
void dss_uninit_overlay_managers_sysfs(struct platform_device *pdev);
int dss_mgr_simple_check(struct omap_overlay_manager *mgr,
		const struct omap_overlay_manager_info *info);
int dss_mgr_check_timings(struct omap_overlay_manager *mgr,
		const struct omap_video_timings *timings);
int dss_mgr_check(struct omap_overlay_manager *mgr,
		struct omap_overlay_manager_info *info,
		const struct omap_video_timings *mgr_timings,
		const struct dss_lcd_mgr_config *config,
		struct omap_overlay_info **overlay_infos);

static inline bool dss_mgr_is_lcd(enum omap_channel id)
{
	if (id == OMAP_DSS_CHANNEL_LCD || id == OMAP_DSS_CHANNEL_LCD2 ||
			id == OMAP_DSS_CHANNEL_LCD3)
		return true;
	else
		return false;
}

int dss_manager_kobj_init(struct omap_overlay_manager *mgr,
		struct platform_device *pdev);
void dss_manager_kobj_uninit(struct omap_overlay_manager *mgr);

/* overlay */
void dss_init_overlays(struct platform_device *pdev);
void dss_uninit_overlays(struct platform_device *pdev);
void dss_overlay_setup_dispc_manager(struct omap_overlay_manager *mgr);
int dss_ovl_simple_check(struct omap_overlay *ovl,
		const struct omap_overlay_info *info);
int dss_ovl_check(struct omap_overlay *ovl, struct omap_overlay_info *info,
		const struct omap_video_timings *mgr_timings);
bool dss_ovl_use_replication(struct dss_lcd_mgr_config config,
		enum omap_color_mode mode);
int dss_overlay_kobj_init(struct omap_overlay *ovl,
		struct platform_device *pdev);
void dss_overlay_kobj_uninit(struct omap_overlay *ovl);

/* DSS */
int dss_init_platform_driver(void) __init;
void dss_uninit_platform_driver(void);

int dss_runtime_get(void);
void dss_runtime_put(void);

unsigned long dss_get_dispc_clk_rate(void);
int dss_dpi_select_source(int port, enum omap_channel channel);
void dss_select_hdmi_venc_clk_source(enum dss_hdmi_venc_clk_source_select);
enum dss_hdmi_venc_clk_source_select dss_get_hdmi_venc_clk_source(void);
const char *dss_get_generic_clk_source_name(enum omap_dss_clk_source clk_src);
void dss_dump_clocks(struct seq_file *s);

/* DSS VIDEO PLL */
struct dss_pll *dss_video_pll_init(struct platform_device *pdev, int id,
	struct regulator *regulator);
void dss_video_pll_uninit(struct dss_pll *pll);

/* dss-of */
struct device_node *dss_of_port_get_parent_device(struct device_node *port);
u32 dss_of_port_get_port_number(struct device_node *port);

#if defined(CONFIG_FB_OMAP2_DSS_DEBUGFS)
void dss_debug_dump_clocks(struct seq_file *s);
#endif

void dss_ctrl_pll_enable(enum dss_pll_id pll_id, bool enable);
void dss_ctrl_pll_set_control_mux(enum dss_pll_id pll_id,
	enum omap_channel channel);

void dss_sdi_init(int datapairs);
int dss_sdi_enable(void);
void dss_sdi_disable(void);

void dss_select_dsi_clk_source(int dsi_module,
		enum omap_dss_clk_source clk_src);
void dss_select_lcd_clk_source(enum omap_channel channel,
		enum omap_dss_clk_source clk_src);
enum omap_dss_clk_source dss_get_dispc_clk_source(void);
enum omap_dss_clk_source dss_get_dsi_clk_source(int dsi_module);
enum omap_dss_clk_source dss_get_lcd_clk_source(enum omap_channel channel);

void dss_set_venc_output(enum omap_dss_venc_type type);
void dss_set_dac_pwrdn_bgz(bool enable);

int dss_set_fck_rate(unsigned long rate);

typedef bool (*dss_div_calc_func)(unsigned long fck, void *data);
bool dss_div_calc(unsigned long pck, unsigned long fck_min,
		dss_div_calc_func func, void *data);

/* SDI */
int sdi_init_platform_driver(void) __init;
void sdi_uninit_platform_driver(void);

#ifdef CONFIG_FB_OMAP2_DSS_SDI
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

#ifdef CONFIG_FB_OMAP2_DSS_DSI

struct dentry;
struct file_operations;

int dsi_init_platform_driver(void) __init;
void dsi_uninit_platform_driver(void);

void dsi_dump_clocks(struct seq_file *s);

void dsi_irq_handler(void);
u8 dsi_get_pixel_size(enum omap_dss_dsi_pixel_format fmt);

#else
static inline u8 dsi_get_pixel_size(enum omap_dss_dsi_pixel_format fmt)
{
	WARN(1, "%s: DSI not compiled in, returning pixel_size as 0\n",
	     __func__);
	return 0;
}
#endif

/* DPI */
int dpi_init_platform_driver(void) __init;
void dpi_uninit_platform_driver(void);

#ifdef CONFIG_FB_OMAP2_DSS_DPI
int dpi_init_port(struct platform_device *pdev, struct device_node *port);
void dpi_uninit_port(struct device_node *port);
#else
static inline int dpi_init_port(struct platform_device *pdev,
		struct device_node *port)
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

bool dispc_mgr_timings_ok(enum omap_channel channel,
		const struct omap_video_timings *timings);
int dispc_calc_clock_rates(unsigned long dispc_fclk_rate,
		struct dispc_clock_info *cinfo);


void dispc_ovl_set_fifo_threshold(enum omap_plane plane, u32 low, u32 high);
void dispc_ovl_compute_fifo_thresholds(enum omap_plane plane,
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
void dispc_wb_enable(bool enable);
bool dispc_wb_is_enabled(void);
void dispc_wb_set_channel_in(enum dss_writeback_channel channel);
int dispc_wb_setup(const struct omap_dss_writeback_info *wi,
		bool mem_to_mem, const struct omap_video_timings *timings);

u32 dispc_read_irqstatus(void);
void dispc_clear_irqstatus(u32 mask);
u32 dispc_read_irqenable(void);
void dispc_write_irqenable(u32 mask);

int dispc_request_irq(irq_handler_t handler, void *dev_id);
void dispc_free_irq(void *dev_id);

int dispc_runtime_get(void);
void dispc_runtime_put(void);

void dispc_mgr_enable(enum omap_channel channel, bool enable);
bool dispc_mgr_is_enabled(enum omap_channel channel);
u32 dispc_mgr_get_vsync_irq(enum omap_channel channel);
u32 dispc_mgr_get_framedone_irq(enum omap_channel channel);
u32 dispc_mgr_get_sync_lost_irq(enum omap_channel channel);
bool dispc_mgr_go_busy(enum omap_channel channel);
void dispc_mgr_go(enum omap_channel channel);
void dispc_mgr_set_lcd_config(enum omap_channel channel,
		const struct dss_lcd_mgr_config *config);
void dispc_mgr_set_timings(enum omap_channel channel,
		const struct omap_video_timings *timings);
void dispc_mgr_setup(enum omap_channel channel,
		const struct omap_overlay_manager_info *info);

int dispc_ovl_check(enum omap_plane plane, enum omap_channel channel,
		const struct omap_overlay_info *oi,
		const struct omap_video_timings *timings,
		int *x_predecim, int *y_predecim);

int dispc_ovl_enable(enum omap_plane plane, bool enable);
bool dispc_ovl_enabled(enum omap_plane plane);
void dispc_ovl_set_channel_out(enum omap_plane plane,
		enum omap_channel channel);
int dispc_ovl_setup(enum omap_plane plane, const struct omap_overlay_info *oi,
		bool replication, const struct omap_video_timings *mgr_timings,
		bool mem_to_mem);

/* VENC */
int venc_init_platform_driver(void) __init;
void venc_uninit_platform_driver(void);

/* HDMI */
int hdmi4_init_platform_driver(void) __init;
void hdmi4_uninit_platform_driver(void);

int hdmi5_init_platform_driver(void) __init;
void hdmi5_uninit_platform_driver(void);

/* RFBI */
int rfbi_init_platform_driver(void) __init;
void rfbi_uninit_platform_driver(void);


#ifdef CONFIG_FB_OMAP2_DSS_COLLECT_IRQ_STATS
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
int dss_pll_enable(struct dss_pll *pll);
void dss_pll_disable(struct dss_pll *pll);
int dss_pll_set_config(struct dss_pll *pll,
		const struct dss_pll_clock_info *cinfo);

bool dss_pll_hsdiv_calc(const struct dss_pll *pll, unsigned long clkdco,
		unsigned long out_min, unsigned long out_max,
		dss_hsdiv_calc_func func, void *data);
bool dss_pll_calc(const struct dss_pll *pll, unsigned long clkin,
		unsigned long pll_min, unsigned long pll_max,
		dss_pll_calc_func func, void *data);
int dss_pll_write_config_type_a(struct dss_pll *pll,
		const struct dss_pll_clock_info *cinfo);
int dss_pll_write_config_type_b(struct dss_pll *pll,
		const struct dss_pll_clock_info *cinfo);
int dss_pll_wait_reset_done(struct dss_pll *pll);

/* compat */

struct dss_mgr_ops {
	int (*connect)(struct omap_overlay_manager *mgr,
		struct omap_dss_device *dst);
	void (*disconnect)(struct omap_overlay_manager *mgr,
		struct omap_dss_device *dst);

	void (*start_update)(struct omap_overlay_manager *mgr);
	int (*enable)(struct omap_overlay_manager *mgr);
	void (*disable)(struct omap_overlay_manager *mgr);
	void (*set_timings)(struct omap_overlay_manager *mgr,
			const struct omap_video_timings *timings);
	void (*set_lcd_config)(struct omap_overlay_manager *mgr,
			const struct dss_lcd_mgr_config *config);
	int (*register_framedone_handler)(struct omap_overlay_manager *mgr,
			void (*handler)(void *), void *data);
	void (*unregister_framedone_handler)(struct omap_overlay_manager *mgr,
			void (*handler)(void *), void *data);
};

int dss_install_mgr_ops(const struct dss_mgr_ops *mgr_ops);
void dss_uninstall_mgr_ops(void);

int dss_mgr_connect(struct omap_overlay_manager *mgr,
		struct omap_dss_device *dst);
void dss_mgr_disconnect(struct omap_overlay_manager *mgr,
		struct omap_dss_device *dst);
void dss_mgr_set_timings(struct omap_overlay_manager *mgr,
		const struct omap_video_timings *timings);
void dss_mgr_set_lcd_config(struct omap_overlay_manager *mgr,
		const struct dss_lcd_mgr_config *config);
int dss_mgr_enable(struct omap_overlay_manager *mgr);
void dss_mgr_disable(struct omap_overlay_manager *mgr);
void dss_mgr_start_update(struct omap_overlay_manager *mgr);
int dss_mgr_register_framedone_handler(struct omap_overlay_manager *mgr,
		void (*handler)(void *), void *data);
void dss_mgr_unregister_framedone_handler(struct omap_overlay_manager *mgr,
		void (*handler)(void *), void *data);

#endif
