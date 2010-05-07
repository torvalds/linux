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

#ifdef CONFIG_OMAP2_DSS_DEBUG_SUPPORT
#define DEBUG
#endif

#ifdef DEBUG
extern unsigned int dss_debug;
#ifdef DSS_SUBSYS_NAME
#define DSSDBG(format, ...) \
	if (dss_debug) \
		printk(KERN_DEBUG "omapdss " DSS_SUBSYS_NAME ": " format, \
		## __VA_ARGS__)
#else
#define DSSDBG(format, ...) \
	if (dss_debug) \
		printk(KERN_DEBUG "omapdss: " format, ## __VA_ARGS__)
#endif

#ifdef DSS_SUBSYS_NAME
#define DSSDBGF(format, ...) \
	if (dss_debug) \
		printk(KERN_DEBUG "omapdss " DSS_SUBSYS_NAME \
				": %s(" format ")\n", \
				__func__, \
				## __VA_ARGS__)
#else
#define DSSDBGF(format, ...) \
	if (dss_debug) \
		printk(KERN_DEBUG "omapdss: " \
				": %s(" format ")\n", \
				__func__, \
				## __VA_ARGS__)
#endif

#else /* DEBUG */
#define DSSDBG(format, ...)
#define DSSDBGF(format, ...)
#endif


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

#define DISPC_MAX_FCK 173000000

enum omap_burst_size {
	OMAP_DSS_BURST_4x32 = 0,
	OMAP_DSS_BURST_8x32 = 1,
	OMAP_DSS_BURST_16x32 = 2,
};

enum omap_parallel_interface_mode {
	OMAP_DSS_PARALLELMODE_BYPASS,		/* MIPI DPI */
	OMAP_DSS_PARALLELMODE_RFBI,		/* MIPI DBI */
	OMAP_DSS_PARALLELMODE_DSI,
};

enum dss_clock {
	DSS_CLK_ICK	= 1 << 0,
	DSS_CLK_FCK1	= 1 << 1,
	DSS_CLK_FCK2	= 1 << 2,
	DSS_CLK_54M	= 1 << 3,
	DSS_CLK_96M	= 1 << 4,
};

enum dss_clk_source {
	DSS_SRC_DSI1_PLL_FCLK,
	DSS_SRC_DSI2_PLL_FCLK,
	DSS_SRC_DSS1_ALWON_FCLK,
};

struct dss_clock_info {
	/* rates that we get with dividers below */
	unsigned long fck;

	/* dividers */
	u16 fck_div;
};

struct dispc_clock_info {
	/* rates that we get with dividers below */
	unsigned long lck;
	unsigned long pck;

	/* dividers */
	u16 lck_div;
	u16 pck_div;
};

struct dsi_clock_info {
	/* rates that we get with dividers below */
	unsigned long fint;
	unsigned long clkin4ddr;
	unsigned long clkin;
	unsigned long dsi1_pll_fclk;
	unsigned long dsi2_pll_fclk;

	unsigned long lp_clk;

	/* dividers */
	u16 regn;
	u16 regm;
	u16 regm3;
	u16 regm4;

	u16 lp_clk_div;

	u8 highfreq;
	bool use_dss2_fck;
};

struct seq_file;
struct platform_device;

/* core */
void dss_clk_enable(enum dss_clock clks);
void dss_clk_disable(enum dss_clock clks);
unsigned long dss_clk_get_rate(enum dss_clock clk);
int dss_need_ctx_restore(void);
void dss_dump_clocks(struct seq_file *s);
struct bus_type *dss_get_bus(void);
struct regulator *dss_get_vdds_dsi(void);
struct regulator *dss_get_vdds_sdi(void);
struct regulator *dss_get_vdda_dac(void);

/* display */
int dss_suspend_all_devices(void);
int dss_resume_all_devices(void);
void dss_disable_all_devices(void);

void dss_init_device(struct platform_device *pdev,
		struct omap_dss_device *dssdev);
void dss_uninit_device(struct platform_device *pdev,
		struct omap_dss_device *dssdev);
bool dss_use_replication(struct omap_dss_device *dssdev,
		enum omap_color_mode mode);
void default_get_overlay_fifo_thresholds(enum omap_plane plane,
		u32 fifo_size, enum omap_burst_size *burst_size,
		u32 *fifo_low, u32 *fifo_high);

/* manager */
int dss_init_overlay_managers(struct platform_device *pdev);
void dss_uninit_overlay_managers(struct platform_device *pdev);
int dss_mgr_wait_for_go_ovl(struct omap_overlay *ovl);
void dss_setup_partial_planes(struct omap_dss_device *dssdev,
				u16 *x, u16 *y, u16 *w, u16 *h);
void dss_start_update(struct omap_dss_device *dssdev);

/* overlay */
void dss_init_overlays(struct platform_device *pdev);
void dss_uninit_overlays(struct platform_device *pdev);
int dss_check_overlay(struct omap_overlay *ovl, struct omap_dss_device *dssdev);
void dss_overlay_setup_dispc_manager(struct omap_overlay_manager *mgr);
#ifdef L4_EXAMPLE
void dss_overlay_setup_l4_manager(struct omap_overlay_manager *mgr);
#endif
void dss_recheck_connections(struct omap_dss_device *dssdev, bool force);

/* DSS */
int dss_init(bool skip_init);
void dss_exit(void);

void dss_save_context(void);
void dss_restore_context(void);

void dss_dump_regs(struct seq_file *s);

void dss_sdi_init(u8 datapairs);
int dss_sdi_enable(void);
void dss_sdi_disable(void);

void dss_select_dispc_clk_source(enum dss_clk_source clk_src);
void dss_select_dsi_clk_source(enum dss_clk_source clk_src);
enum dss_clk_source dss_get_dispc_clk_source(void);
enum dss_clk_source dss_get_dsi_clk_source(void);

void dss_set_venc_output(enum omap_dss_venc_type type);
void dss_set_dac_pwrdn_bgz(bool enable);

unsigned long dss_get_dpll4_rate(void);
int dss_calc_clock_rates(struct dss_clock_info *cinfo);
int dss_set_clock_div(struct dss_clock_info *cinfo);
int dss_get_clock_div(struct dss_clock_info *cinfo);
int dss_calc_clock_div(bool is_tft, unsigned long req_pck,
		struct dss_clock_info *dss_cinfo,
		struct dispc_clock_info *dispc_cinfo);

/* SDI */
#ifdef CONFIG_OMAP2_DSS_SDI
int sdi_init(bool skip_init);
void sdi_exit(void);
int sdi_init_display(struct omap_dss_device *display);
#else
static inline int sdi_init(bool skip_init)
{
	return 0;
}
static inline void sdi_exit(void)
{
}
#endif

/* DSI */
#ifdef CONFIG_OMAP2_DSS_DSI
int dsi_init(struct platform_device *pdev);
void dsi_exit(void);

void dsi_dump_clocks(struct seq_file *s);
void dsi_dump_irqs(struct seq_file *s);
void dsi_dump_regs(struct seq_file *s);

void dsi_save_context(void);
void dsi_restore_context(void);

int dsi_init_display(struct omap_dss_device *display);
void dsi_irq_handler(void);
unsigned long dsi_get_dsi1_pll_rate(void);
int dsi_pll_set_clock_div(struct dsi_clock_info *cinfo);
int dsi_pll_calc_clock_div_pck(bool is_tft, unsigned long req_pck,
		struct dsi_clock_info *cinfo,
		struct dispc_clock_info *dispc_cinfo);
int dsi_pll_init(struct omap_dss_device *dssdev, bool enable_hsclk,
		bool enable_hsdiv);
void dsi_pll_uninit(void);
void dsi_get_overlay_fifo_thresholds(enum omap_plane plane,
		u32 fifo_size, enum omap_burst_size *burst_size,
		u32 *fifo_low, u32 *fifo_high);
#else
static inline int dsi_init(struct platform_device *pdev)
{
	return 0;
}
static inline void dsi_exit(void)
{
}
#endif

/* DPI */
#ifdef CONFIG_OMAP2_DSS_DPI
int dpi_init(struct platform_device *pdev);
void dpi_exit(void);
int dpi_init_display(struct omap_dss_device *dssdev);
#else
static inline int dpi_init(struct platform_device *pdev)
{
	return 0;
}
static inline void dpi_exit(void)
{
}
#endif

/* DISPC */
int dispc_init(void);
void dispc_exit(void);
void dispc_dump_clocks(struct seq_file *s);
void dispc_dump_irqs(struct seq_file *s);
void dispc_dump_regs(struct seq_file *s);
void dispc_irq_handler(void);
void dispc_fake_vsync_irq(void);

void dispc_save_context(void);
void dispc_restore_context(void);

void dispc_enable_sidle(void);
void dispc_disable_sidle(void);

void dispc_lcd_enable_signal_polarity(bool act_high);
void dispc_lcd_enable_signal(bool enable);
void dispc_pck_free_enable(bool enable);
void dispc_enable_fifohandcheck(bool enable);

void dispc_set_lcd_size(u16 width, u16 height);
void dispc_set_digit_size(u16 width, u16 height);
u32 dispc_get_plane_fifo_size(enum omap_plane plane);
void dispc_setup_plane_fifo(enum omap_plane plane, u32 low, u32 high);
void dispc_enable_fifomerge(bool enable);
void dispc_set_burst_size(enum omap_plane plane,
		enum omap_burst_size burst_size);

void dispc_set_plane_ba0(enum omap_plane plane, u32 paddr);
void dispc_set_plane_ba1(enum omap_plane plane, u32 paddr);
void dispc_set_plane_pos(enum omap_plane plane, u16 x, u16 y);
void dispc_set_plane_size(enum omap_plane plane, u16 width, u16 height);
void dispc_set_channel_out(enum omap_plane plane,
		enum omap_channel channel_out);

int dispc_setup_plane(enum omap_plane plane,
		      u32 paddr, u16 screen_width,
		      u16 pos_x, u16 pos_y,
		      u16 width, u16 height,
		      u16 out_width, u16 out_height,
		      enum omap_color_mode color_mode,
		      bool ilace,
		      enum omap_dss_rotation_type rotation_type,
		      u8 rotation, bool mirror,
		      u8 global_alpha);

bool dispc_go_busy(enum omap_channel channel);
void dispc_go(enum omap_channel channel);
void dispc_enable_channel(enum omap_channel channel, bool enable);
bool dispc_is_channel_enabled(enum omap_channel channel);
int dispc_enable_plane(enum omap_plane plane, bool enable);
void dispc_enable_replication(enum omap_plane plane, bool enable);

void dispc_set_parallel_interface_mode(enum omap_parallel_interface_mode mode);
void dispc_set_tft_data_lines(u8 data_lines);
void dispc_set_lcd_display_type(enum omap_lcd_display_type type);
void dispc_set_loadmode(enum omap_dss_load_mode mode);

void dispc_set_default_color(enum omap_channel channel, u32 color);
u32 dispc_get_default_color(enum omap_channel channel);
void dispc_set_trans_key(enum omap_channel ch,
		enum omap_dss_trans_key_type type,
		u32 trans_key);
void dispc_get_trans_key(enum omap_channel ch,
		enum omap_dss_trans_key_type *type,
		u32 *trans_key);
void dispc_enable_trans_key(enum omap_channel ch, bool enable);
void dispc_enable_alpha_blending(enum omap_channel ch, bool enable);
bool dispc_trans_key_enabled(enum omap_channel ch);
bool dispc_alpha_blending_enabled(enum omap_channel ch);

bool dispc_lcd_timings_ok(struct omap_video_timings *timings);
void dispc_set_lcd_timings(struct omap_video_timings *timings);
unsigned long dispc_fclk_rate(void);
unsigned long dispc_lclk_rate(void);
unsigned long dispc_pclk_rate(void);
void dispc_set_pol_freq(enum omap_panel_config config, u8 acbi, u8 acb);
void dispc_find_clk_divs(bool is_tft, unsigned long req_pck, unsigned long fck,
		struct dispc_clock_info *cinfo);
int dispc_calc_clock_rates(unsigned long dispc_fclk_rate,
		struct dispc_clock_info *cinfo);
int dispc_set_clock_div(struct dispc_clock_info *cinfo);
int dispc_get_clock_div(struct dispc_clock_info *cinfo);


/* VENC */
#ifdef CONFIG_OMAP2_DSS_VENC
int venc_init(struct platform_device *pdev);
void venc_exit(void);
void venc_dump_regs(struct seq_file *s);
int venc_init_display(struct omap_dss_device *display);
#else
static inline int venc_init(struct platform_device *pdev)
{
	return 0;
}
static inline void venc_exit(void)
{
}
#endif

/* RFBI */
#ifdef CONFIG_OMAP2_DSS_RFBI
int rfbi_init(void);
void rfbi_exit(void);
void rfbi_dump_regs(struct seq_file *s);

int rfbi_configure(int rfbi_module, int bpp, int lines);
void rfbi_enable_rfbi(bool enable);
void rfbi_transfer_area(u16 width, u16 height,
			     void (callback)(void *data), void *data);
void rfbi_set_timings(int rfbi_module, struct rfbi_timings *t);
unsigned long rfbi_get_max_tx_rate(void);
int rfbi_init_display(struct omap_dss_device *display);
#else
static inline int rfbi_init(void)
{
	return 0;
}
static inline void rfbi_exit(void)
{
}
#endif


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

#endif
