/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2009 Nokia Corporation
 * Author: Tomi Valkeinen <tomi.valkeinen@ti.com>
 *
 * Some code and ideas taken from drivers/video/omap/ driver
 * by Imre Deak.
 */

#ifndef __OMAP2_DSS_H
#define __OMAP2_DSS_H

#include <linux/interrupt.h>

#include "omapdss.h"

struct dispc_device;
struct dss_debugfs_entry;
struct platform_device;
struct seq_file;

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

	unsigned int n_max;
	unsigned int m_min;
	unsigned int m_max;
	unsigned int mX_max;

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

	/* DRA7 errata i932: retry pll lock on failure */
	bool errata_i932;
};

struct dss_pll {
	const char *name;
	enum dss_pll_id id;
	struct dss_device *dss;

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

#define DSS_SZ_REGS			SZ_512

struct dss_device {
	struct platform_device *pdev;
	void __iomem    *base;
	struct regmap	*syscon_pll_ctrl;
	u32		syscon_pll_ctrl_offset;

	struct platform_device *drm_pdev;

	struct clk	*parent_clk;
	struct clk	*dss_clk;
	unsigned long	dss_clk_rate;

	unsigned long	cache_req_pck;
	unsigned long	cache_prate;
	struct dispc_clock_info cache_dispc_cinfo;

	enum dss_clk_source dsi_clk_source[MAX_NUM_DSI];
	enum dss_clk_source dispc_clk_source;
	enum dss_clk_source lcd_clk_source[MAX_DSS_LCD_MANAGERS];

	bool		ctx_valid;
	u32		ctx[DSS_SZ_REGS / sizeof(u32)];

	const struct dss_features *feat;

	struct {
		struct dentry *root;
		struct dss_debugfs_entry *clk;
		struct dss_debugfs_entry *dss;
	} debugfs;

	struct dss_pll *plls[4];
	struct dss_pll	*video1_pll;
	struct dss_pll	*video2_pll;

	struct dispc_device *dispc;
	struct omap_drm_private *mgr_ops_priv;
};

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
struct dss_debugfs_entry *
dss_debugfs_create_file(struct dss_device *dss, const char *name,
			int (*show_fn)(struct seq_file *s, void *data),
			void *data);
void dss_debugfs_remove_file(struct dss_debugfs_entry *entry);
#else
static inline struct dss_debugfs_entry *
dss_debugfs_create_file(struct dss_device *dss, const char *name,
			int (*show_fn)(struct seq_file *s, void *data),
			void *data)
{
	return NULL;
}

static inline void dss_debugfs_remove_file(struct dss_debugfs_entry *entry)
{
}
#endif /* CONFIG_OMAP2_DSS_DEBUGFS */

struct dss_device *dss_get_device(struct device *dev);

int dss_runtime_get(struct dss_device *dss);
void dss_runtime_put(struct dss_device *dss);

unsigned long dss_get_dispc_clk_rate(struct dss_device *dss);
unsigned long dss_get_max_fck_rate(struct dss_device *dss);
int dss_dpi_select_source(struct dss_device *dss, int port,
			  enum omap_channel channel);
void dss_select_hdmi_venc_clk_source(struct dss_device *dss,
				     enum dss_hdmi_venc_clk_source_select src);
const char *dss_get_clk_source_name(enum dss_clk_source clk_src);

/* DSS VIDEO PLL */
struct dss_pll *dss_video_pll_init(struct dss_device *dss,
				   struct platform_device *pdev, int id,
				   struct regulator *regulator);
void dss_video_pll_uninit(struct dss_pll *pll);

void dss_ctrl_pll_enable(struct dss_pll *pll, bool enable);

void dss_sdi_init(struct dss_device *dss, int datapairs);
int dss_sdi_enable(struct dss_device *dss);
void dss_sdi_disable(struct dss_device *dss);

void dss_select_dsi_clk_source(struct dss_device *dss, int dsi_module,
			       enum dss_clk_source clk_src);
void dss_select_lcd_clk_source(struct dss_device *dss,
			       enum omap_channel channel,
			       enum dss_clk_source clk_src);
enum dss_clk_source dss_get_dispc_clk_source(struct dss_device *dss);
enum dss_clk_source dss_get_dsi_clk_source(struct dss_device *dss,
					   int dsi_module);
enum dss_clk_source dss_get_lcd_clk_source(struct dss_device *dss,
					   enum omap_channel channel);

void dss_set_venc_output(struct dss_device *dss, enum omap_dss_venc_type type);
void dss_set_dac_pwrdn_bgz(struct dss_device *dss, bool enable);

int dss_set_fck_rate(struct dss_device *dss, unsigned long rate);

typedef bool (*dss_div_calc_func)(unsigned long fck, void *data);
bool dss_div_calc(struct dss_device *dss, unsigned long pck,
		  unsigned long fck_min, dss_div_calc_func func, void *data);

/* SDI */
#ifdef CONFIG_OMAP2_DSS_SDI
int sdi_init_port(struct dss_device *dss, struct platform_device *pdev,
		  struct device_node *port);
void sdi_uninit_port(struct device_node *port);
#else
static inline int sdi_init_port(struct dss_device *dss,
				struct platform_device *pdev,
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

void dsi_irq_handler(void);

#endif

/* DPI */
#ifdef CONFIG_OMAP2_DSS_DPI
int dpi_init_port(struct dss_device *dss, struct platform_device *pdev,
		  struct device_node *port, enum dss_model dss_model);
void dpi_uninit_port(struct device_node *port);
#else
static inline int dpi_init_port(struct dss_device *dss,
				struct platform_device *pdev,
				struct device_node *port,
				enum dss_model dss_model)
{
	return 0;
}
static inline void dpi_uninit_port(struct device_node *port)
{
}
#endif

/* DISPC */
void dispc_dump_clocks(struct dispc_device *dispc, struct seq_file *s);

int dispc_runtime_get(struct dispc_device *dispc);
void dispc_runtime_put(struct dispc_device *dispc);

int dispc_get_num_ovls(struct dispc_device *dispc);
int dispc_get_num_mgrs(struct dispc_device *dispc);

const u32 *dispc_ovl_get_color_modes(struct dispc_device *dispc,
					    enum omap_plane_id plane);

void dispc_ovl_get_max_size(struct dispc_device *dispc, u16 *width, u16 *height);
bool dispc_ovl_color_mode_supported(struct dispc_device *dispc,
				    enum omap_plane_id plane, u32 fourcc);
enum omap_overlay_caps dispc_ovl_get_caps(struct dispc_device *dispc, enum omap_plane_id plane);

u32 dispc_read_irqstatus(struct dispc_device *dispc);
void dispc_clear_irqstatus(struct dispc_device *dispc, u32 mask);
void dispc_write_irqenable(struct dispc_device *dispc, u32 mask);

int dispc_request_irq(struct dispc_device *dispc, irq_handler_t handler,
			     void *dev_id);
void dispc_free_irq(struct dispc_device *dispc, void *dev_id);

u32 dispc_mgr_get_vsync_irq(struct dispc_device *dispc,
				   enum omap_channel channel);
u32 dispc_mgr_get_framedone_irq(struct dispc_device *dispc,
				       enum omap_channel channel);
u32 dispc_mgr_get_sync_lost_irq(struct dispc_device *dispc,
				       enum omap_channel channel);

u32 dispc_get_memory_bandwidth_limit(struct dispc_device *dispc);

void dispc_mgr_enable(struct dispc_device *dispc,
			     enum omap_channel channel, bool enable);

bool dispc_mgr_go_busy(struct dispc_device *dispc,
			      enum omap_channel channel);

void dispc_mgr_go(struct dispc_device *dispc, enum omap_channel channel);

void dispc_mgr_set_lcd_config(struct dispc_device *dispc,
				     enum omap_channel channel,
				     const struct dss_lcd_mgr_config *config);
void dispc_mgr_set_timings(struct dispc_device *dispc,
				  enum omap_channel channel,
				  const struct videomode *vm);
void dispc_mgr_setup(struct dispc_device *dispc,
			    enum omap_channel channel,
			    const struct omap_overlay_manager_info *info);

int dispc_mgr_check_timings(struct dispc_device *dispc,
				   enum omap_channel channel,
				   const struct videomode *vm);

u32 dispc_mgr_gamma_size(struct dispc_device *dispc,
				enum omap_channel channel);
void dispc_mgr_set_gamma(struct dispc_device *dispc,
				enum omap_channel channel,
				const struct drm_color_lut *lut,
				unsigned int length);

int dispc_ovl_setup(struct dispc_device *dispc,
			   enum omap_plane_id plane,
			   const struct omap_overlay_info *oi,
			   const struct videomode *vm, bool mem_to_mem,
			   enum omap_channel channel);

int dispc_ovl_enable(struct dispc_device *dispc,
			    enum omap_plane_id plane, bool enable);

void dispc_enable_sidle(struct dispc_device *dispc);
void dispc_disable_sidle(struct dispc_device *dispc);

void dispc_lcd_enable_signal(struct dispc_device *dispc, bool enable);
void dispc_pck_free_enable(struct dispc_device *dispc, bool enable);

typedef bool (*dispc_div_calc_func)(int lckd, int pckd, unsigned long lck,
		unsigned long pck, void *data);
bool dispc_div_calc(struct dispc_device *dispc, unsigned long dispc_freq,
		    unsigned long pck_min, unsigned long pck_max,
		    dispc_div_calc_func func, void *data);

int dispc_calc_clock_rates(struct dispc_device *dispc,
			   unsigned long dispc_fclk_rate,
			   struct dispc_clock_info *cinfo);


void dispc_ovl_set_fifo_threshold(struct dispc_device *dispc,
				  enum omap_plane_id plane, u32 low, u32 high);
void dispc_ovl_compute_fifo_thresholds(struct dispc_device *dispc,
				       enum omap_plane_id plane,
				       u32 *fifo_low, u32 *fifo_high,
				       bool use_fifomerge, bool manual_update);

void dispc_mgr_set_clock_div(struct dispc_device *dispc,
			     enum omap_channel channel,
			     const struct dispc_clock_info *cinfo);
void dispc_set_tv_pclk(struct dispc_device *dispc, unsigned long pclk);

#ifdef CONFIG_OMAP2_DSS_COLLECT_IRQ_STATS
static inline void dss_collect_irq_stats(u32 irqstatus, unsigned int *irq_arr)
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

int dss_pll_register(struct dss_device *dss, struct dss_pll *pll);
void dss_pll_unregister(struct dss_pll *pll);
struct dss_pll *dss_pll_find(struct dss_device *dss, const char *name);
struct dss_pll *dss_pll_find_by_src(struct dss_device *dss,
				    enum dss_clk_source src);
unsigned int dss_pll_get_clkout_idx_for_src(enum dss_clk_source src);
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

extern struct platform_driver omap_dsshw_driver;
extern struct platform_driver omap_dispchw_driver;
#ifdef CONFIG_OMAP2_DSS_DSI
extern struct platform_driver omap_dsihw_driver;
#endif
#ifdef CONFIG_OMAP2_DSS_VENC
extern struct platform_driver omap_venchw_driver;
#endif
#ifdef CONFIG_OMAP4_DSS_HDMI
extern struct platform_driver omapdss_hdmi4hw_driver;
#endif
#ifdef CONFIG_OMAP5_DSS_HDMI
extern struct platform_driver omapdss_hdmi5hw_driver;
#endif

#endif
