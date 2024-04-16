/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2016 Texas Instruments Incorporated - http://www.ti.com/
 * Author: Tomi Valkeinen <tomi.valkeinen@ti.com>
 */

#ifndef __OMAP_DRM_DSS_DSI_H
#define __OMAP_DRM_DSS_DSI_H

#include <drm/drm_mipi_dsi.h>

struct dsi_reg {
	u16 module;
	u16 idx;
};

#define DSI_REG(mod, idx)		((const struct dsi_reg) { mod, idx })

/* DSI Protocol Engine */

#define DSI_PROTO			0
#define DSI_PROTO_SZ			0x200

#define DSI_REVISION			DSI_REG(DSI_PROTO, 0x0000)
#define DSI_SYSCONFIG			DSI_REG(DSI_PROTO, 0x0010)
#define DSI_SYSSTATUS			DSI_REG(DSI_PROTO, 0x0014)
#define DSI_IRQSTATUS			DSI_REG(DSI_PROTO, 0x0018)
#define DSI_IRQENABLE			DSI_REG(DSI_PROTO, 0x001C)
#define DSI_CTRL			DSI_REG(DSI_PROTO, 0x0040)
#define DSI_GNQ				DSI_REG(DSI_PROTO, 0x0044)
#define DSI_COMPLEXIO_CFG1		DSI_REG(DSI_PROTO, 0x0048)
#define DSI_COMPLEXIO_IRQ_STATUS	DSI_REG(DSI_PROTO, 0x004C)
#define DSI_COMPLEXIO_IRQ_ENABLE	DSI_REG(DSI_PROTO, 0x0050)
#define DSI_CLK_CTRL			DSI_REG(DSI_PROTO, 0x0054)
#define DSI_TIMING1			DSI_REG(DSI_PROTO, 0x0058)
#define DSI_TIMING2			DSI_REG(DSI_PROTO, 0x005C)
#define DSI_VM_TIMING1			DSI_REG(DSI_PROTO, 0x0060)
#define DSI_VM_TIMING2			DSI_REG(DSI_PROTO, 0x0064)
#define DSI_VM_TIMING3			DSI_REG(DSI_PROTO, 0x0068)
#define DSI_CLK_TIMING			DSI_REG(DSI_PROTO, 0x006C)
#define DSI_TX_FIFO_VC_SIZE		DSI_REG(DSI_PROTO, 0x0070)
#define DSI_RX_FIFO_VC_SIZE		DSI_REG(DSI_PROTO, 0x0074)
#define DSI_COMPLEXIO_CFG2		DSI_REG(DSI_PROTO, 0x0078)
#define DSI_RX_FIFO_VC_FULLNESS		DSI_REG(DSI_PROTO, 0x007C)
#define DSI_VM_TIMING4			DSI_REG(DSI_PROTO, 0x0080)
#define DSI_TX_FIFO_VC_EMPTINESS	DSI_REG(DSI_PROTO, 0x0084)
#define DSI_VM_TIMING5			DSI_REG(DSI_PROTO, 0x0088)
#define DSI_VM_TIMING6			DSI_REG(DSI_PROTO, 0x008C)
#define DSI_VM_TIMING7			DSI_REG(DSI_PROTO, 0x0090)
#define DSI_STOPCLK_TIMING		DSI_REG(DSI_PROTO, 0x0094)
#define DSI_VC_CTRL(n)			DSI_REG(DSI_PROTO, 0x0100 + (n * 0x20))
#define DSI_VC_TE(n)			DSI_REG(DSI_PROTO, 0x0104 + (n * 0x20))
#define DSI_VC_LONG_PACKET_HEADER(n)	DSI_REG(DSI_PROTO, 0x0108 + (n * 0x20))
#define DSI_VC_LONG_PACKET_PAYLOAD(n)	DSI_REG(DSI_PROTO, 0x010C + (n * 0x20))
#define DSI_VC_SHORT_PACKET_HEADER(n)	DSI_REG(DSI_PROTO, 0x0110 + (n * 0x20))
#define DSI_VC_IRQSTATUS(n)		DSI_REG(DSI_PROTO, 0x0118 + (n * 0x20))
#define DSI_VC_IRQENABLE(n)		DSI_REG(DSI_PROTO, 0x011C + (n * 0x20))

/* DSIPHY_SCP */

#define DSI_PHY				1
#define DSI_PHY_OFFSET			0x200
#define DSI_PHY_SZ			0x40

#define DSI_DSIPHY_CFG0			DSI_REG(DSI_PHY, 0x0000)
#define DSI_DSIPHY_CFG1			DSI_REG(DSI_PHY, 0x0004)
#define DSI_DSIPHY_CFG2			DSI_REG(DSI_PHY, 0x0008)
#define DSI_DSIPHY_CFG5			DSI_REG(DSI_PHY, 0x0014)
#define DSI_DSIPHY_CFG10		DSI_REG(DSI_PHY, 0x0028)

/* DSI_PLL_CTRL_SCP */

#define DSI_PLL				2
#define DSI_PLL_OFFSET			0x300
#define DSI_PLL_SZ			0x20

#define DSI_PLL_CONTROL			DSI_REG(DSI_PLL, 0x0000)
#define DSI_PLL_STATUS			DSI_REG(DSI_PLL, 0x0004)
#define DSI_PLL_GO			DSI_REG(DSI_PLL, 0x0008)
#define DSI_PLL_CONFIGURATION1		DSI_REG(DSI_PLL, 0x000C)
#define DSI_PLL_CONFIGURATION2		DSI_REG(DSI_PLL, 0x0010)

/* Global interrupts */
#define DSI_IRQ_VC0		(1 << 0)
#define DSI_IRQ_VC1		(1 << 1)
#define DSI_IRQ_VC2		(1 << 2)
#define DSI_IRQ_VC3		(1 << 3)
#define DSI_IRQ_WAKEUP		(1 << 4)
#define DSI_IRQ_RESYNC		(1 << 5)
#define DSI_IRQ_PLL_LOCK	(1 << 7)
#define DSI_IRQ_PLL_UNLOCK	(1 << 8)
#define DSI_IRQ_PLL_RECALL	(1 << 9)
#define DSI_IRQ_COMPLEXIO_ERR	(1 << 10)
#define DSI_IRQ_HS_TX_TIMEOUT	(1 << 14)
#define DSI_IRQ_LP_RX_TIMEOUT	(1 << 15)
#define DSI_IRQ_TE_TRIGGER	(1 << 16)
#define DSI_IRQ_ACK_TRIGGER	(1 << 17)
#define DSI_IRQ_SYNC_LOST	(1 << 18)
#define DSI_IRQ_LDO_POWER_GOOD	(1 << 19)
#define DSI_IRQ_TA_TIMEOUT	(1 << 20)
#define DSI_IRQ_ERROR_MASK \
	(DSI_IRQ_HS_TX_TIMEOUT | DSI_IRQ_LP_RX_TIMEOUT | DSI_IRQ_SYNC_LOST | \
	DSI_IRQ_TA_TIMEOUT)
#define DSI_IRQ_CHANNEL_MASK	0xf

/* Virtual channel interrupts */
#define DSI_VC_IRQ_CS		(1 << 0)
#define DSI_VC_IRQ_ECC_CORR	(1 << 1)
#define DSI_VC_IRQ_PACKET_SENT	(1 << 2)
#define DSI_VC_IRQ_FIFO_TX_OVF	(1 << 3)
#define DSI_VC_IRQ_FIFO_RX_OVF	(1 << 4)
#define DSI_VC_IRQ_BTA		(1 << 5)
#define DSI_VC_IRQ_ECC_NO_CORR	(1 << 6)
#define DSI_VC_IRQ_FIFO_TX_UDF	(1 << 7)
#define DSI_VC_IRQ_PP_BUSY_CHANGE (1 << 8)
#define DSI_VC_IRQ_ERROR_MASK \
	(DSI_VC_IRQ_CS | DSI_VC_IRQ_ECC_CORR | DSI_VC_IRQ_FIFO_TX_OVF | \
	DSI_VC_IRQ_FIFO_RX_OVF | DSI_VC_IRQ_ECC_NO_CORR | \
	DSI_VC_IRQ_FIFO_TX_UDF)

/* ComplexIO interrupts */
#define DSI_CIO_IRQ_ERRSYNCESC1		(1 << 0)
#define DSI_CIO_IRQ_ERRSYNCESC2		(1 << 1)
#define DSI_CIO_IRQ_ERRSYNCESC3		(1 << 2)
#define DSI_CIO_IRQ_ERRSYNCESC4		(1 << 3)
#define DSI_CIO_IRQ_ERRSYNCESC5		(1 << 4)
#define DSI_CIO_IRQ_ERRESC1		(1 << 5)
#define DSI_CIO_IRQ_ERRESC2		(1 << 6)
#define DSI_CIO_IRQ_ERRESC3		(1 << 7)
#define DSI_CIO_IRQ_ERRESC4		(1 << 8)
#define DSI_CIO_IRQ_ERRESC5		(1 << 9)
#define DSI_CIO_IRQ_ERRCONTROL1		(1 << 10)
#define DSI_CIO_IRQ_ERRCONTROL2		(1 << 11)
#define DSI_CIO_IRQ_ERRCONTROL3		(1 << 12)
#define DSI_CIO_IRQ_ERRCONTROL4		(1 << 13)
#define DSI_CIO_IRQ_ERRCONTROL5		(1 << 14)
#define DSI_CIO_IRQ_STATEULPS1		(1 << 15)
#define DSI_CIO_IRQ_STATEULPS2		(1 << 16)
#define DSI_CIO_IRQ_STATEULPS3		(1 << 17)
#define DSI_CIO_IRQ_STATEULPS4		(1 << 18)
#define DSI_CIO_IRQ_STATEULPS5		(1 << 19)
#define DSI_CIO_IRQ_ERRCONTENTIONLP0_1	(1 << 20)
#define DSI_CIO_IRQ_ERRCONTENTIONLP1_1	(1 << 21)
#define DSI_CIO_IRQ_ERRCONTENTIONLP0_2	(1 << 22)
#define DSI_CIO_IRQ_ERRCONTENTIONLP1_2	(1 << 23)
#define DSI_CIO_IRQ_ERRCONTENTIONLP0_3	(1 << 24)
#define DSI_CIO_IRQ_ERRCONTENTIONLP1_3	(1 << 25)
#define DSI_CIO_IRQ_ERRCONTENTIONLP0_4	(1 << 26)
#define DSI_CIO_IRQ_ERRCONTENTIONLP1_4	(1 << 27)
#define DSI_CIO_IRQ_ERRCONTENTIONLP0_5	(1 << 28)
#define DSI_CIO_IRQ_ERRCONTENTIONLP1_5	(1 << 29)
#define DSI_CIO_IRQ_ULPSACTIVENOT_ALL0	(1 << 30)
#define DSI_CIO_IRQ_ULPSACTIVENOT_ALL1	(1 << 31)
#define DSI_CIO_IRQ_ERROR_MASK \
	(DSI_CIO_IRQ_ERRSYNCESC1 | DSI_CIO_IRQ_ERRSYNCESC2 | \
	 DSI_CIO_IRQ_ERRSYNCESC3 | DSI_CIO_IRQ_ERRSYNCESC4 | \
	 DSI_CIO_IRQ_ERRSYNCESC5 | \
	 DSI_CIO_IRQ_ERRESC1 | DSI_CIO_IRQ_ERRESC2 | \
	 DSI_CIO_IRQ_ERRESC3 | DSI_CIO_IRQ_ERRESC4 | \
	 DSI_CIO_IRQ_ERRESC5 | \
	 DSI_CIO_IRQ_ERRCONTROL1 | DSI_CIO_IRQ_ERRCONTROL2 | \
	 DSI_CIO_IRQ_ERRCONTROL3 | DSI_CIO_IRQ_ERRCONTROL4 | \
	 DSI_CIO_IRQ_ERRCONTROL5 | \
	 DSI_CIO_IRQ_ERRCONTENTIONLP0_1 | DSI_CIO_IRQ_ERRCONTENTIONLP1_1 | \
	 DSI_CIO_IRQ_ERRCONTENTIONLP0_2 | DSI_CIO_IRQ_ERRCONTENTIONLP1_2 | \
	 DSI_CIO_IRQ_ERRCONTENTIONLP0_3 | DSI_CIO_IRQ_ERRCONTENTIONLP1_3 | \
	 DSI_CIO_IRQ_ERRCONTENTIONLP0_4 | DSI_CIO_IRQ_ERRCONTENTIONLP1_4 | \
	 DSI_CIO_IRQ_ERRCONTENTIONLP0_5 | DSI_CIO_IRQ_ERRCONTENTIONLP1_5)

enum omap_dss_dsi_mode {
	OMAP_DSS_DSI_CMD_MODE = 0,
	OMAP_DSS_DSI_VIDEO_MODE,
};

enum omap_dss_dsi_trans_mode {
	/* Sync Pulses: both sync start and end packets sent */
	OMAP_DSS_DSI_PULSE_MODE,
	/* Sync Events: only sync start packets sent */
	OMAP_DSS_DSI_EVENT_MODE,
	/* Burst: only sync start packets sent, pixels are time compressed */
	OMAP_DSS_DSI_BURST_MODE,
};

struct omap_dss_dsi_videomode_timings {
	unsigned long hsclk;

	unsigned int ndl;
	unsigned int bitspp;

	/* pixels */
	u16 hact;
	/* lines */
	u16 vact;

	/* DSI video mode blanking data */
	/* Unit: byte clock cycles */
	u16 hss;
	u16 hsa;
	u16 hse;
	u16 hfp;
	u16 hbp;
	/* Unit: line clocks */
	u16 vsa;
	u16 vfp;
	u16 vbp;

	/* DSI blanking modes */
	int blanking_mode;
	int hsa_blanking_mode;
	int hbp_blanking_mode;
	int hfp_blanking_mode;

	enum omap_dss_dsi_trans_mode trans_mode;

	int window_sync;
};

struct omap_dss_dsi_config {
	enum omap_dss_dsi_mode mode;
	enum mipi_dsi_pixel_format pixel_format;
	const struct videomode *vm;

	unsigned long hs_clk_min, hs_clk_max;
	unsigned long lp_clk_min, lp_clk_max;

	enum omap_dss_dsi_trans_mode trans_mode;
};

/* DSI PLL HSDIV indices */
#define HSDIV_DISPC	0
#define HSDIV_DSI	1

#define DSI_MAX_NR_ISRS                2
#define DSI_MAX_NR_LANES	5

enum dsi_model {
	DSI_MODEL_OMAP3,
	DSI_MODEL_OMAP4,
	DSI_MODEL_OMAP5,
};

enum dsi_lane_function {
	DSI_LANE_UNUSED	= 0,
	DSI_LANE_CLK,
	DSI_LANE_DATA1,
	DSI_LANE_DATA2,
	DSI_LANE_DATA3,
	DSI_LANE_DATA4,
};

struct dsi_lane_config {
	enum dsi_lane_function function;
	u8 polarity;
};

typedef void (*omap_dsi_isr_t) (void *arg, u32 mask);

struct dsi_isr_data {
	omap_dsi_isr_t	isr;
	void		*arg;
	u32		mask;
};

enum fifo_size {
	DSI_FIFO_SIZE_0		= 0,
	DSI_FIFO_SIZE_32	= 1,
	DSI_FIFO_SIZE_64	= 2,
	DSI_FIFO_SIZE_96	= 3,
	DSI_FIFO_SIZE_128	= 4,
};

enum dsi_vc_source {
	DSI_VC_SOURCE_L4 = 0,
	DSI_VC_SOURCE_VP,
};

struct dsi_irq_stats {
	unsigned long last_reset;
	unsigned int irq_count;
	unsigned int dsi_irqs[32];
	unsigned int vc_irqs[4][32];
	unsigned int cio_irqs[32];
};

struct dsi_isr_tables {
	struct dsi_isr_data isr_table[DSI_MAX_NR_ISRS];
	struct dsi_isr_data isr_table_vc[4][DSI_MAX_NR_ISRS];
	struct dsi_isr_data isr_table_cio[DSI_MAX_NR_ISRS];
};

struct dsi_lp_clock_info {
	unsigned long lp_clk;
	u16 lp_clk_div;
};

struct dsi_clk_calc_ctx {
	struct dsi_data *dsi;
	struct dss_pll *pll;

	/* inputs */

	const struct omap_dss_dsi_config *config;

	unsigned long req_pck_min, req_pck_nom, req_pck_max;

	/* outputs */

	struct dss_pll_clock_info dsi_cinfo;
	struct dispc_clock_info dispc_cinfo;
	struct dsi_lp_clock_info lp_cinfo;

	struct videomode vm;
	struct omap_dss_dsi_videomode_timings dsi_vm;
};

struct dsi_module_id_data {
	u32 address;
	int id;
};

enum dsi_quirks {
	DSI_QUIRK_PLL_PWR_BUG = (1 << 0),	/* DSI-PLL power command 0x3 is not working */
	DSI_QUIRK_DCS_CMD_CONFIG_VC = (1 << 1),
	DSI_QUIRK_VC_OCP_WIDTH = (1 << 2),
	DSI_QUIRK_REVERSE_TXCLKESC = (1 << 3),
	DSI_QUIRK_GNQ = (1 << 4),
	DSI_QUIRK_PHY_DCC = (1 << 5),
};

struct dsi_of_data {
	enum dsi_model model;
	const struct dss_pll_hw *pll_hw;
	const struct dsi_module_id_data *modules;
	unsigned int max_fck_freq;
	unsigned int max_pll_lpdiv;
	enum dsi_quirks quirks;
};

struct dsi_data {
	struct device *dev;
	void __iomem *proto_base;
	void __iomem *phy_base;
	void __iomem *pll_base;

	const struct dsi_of_data *data;
	int module_id;

	int irq;

	bool is_enabled;

	struct clk *dss_clk;
	struct regmap *syscon;
	struct dss_device *dss;

	struct mipi_dsi_host host;

	struct dispc_clock_info user_dispc_cinfo;
	struct dss_pll_clock_info user_dsi_cinfo;

	struct dsi_lp_clock_info user_lp_cinfo;
	struct dsi_lp_clock_info current_lp_cinfo;

	struct dss_pll pll;

	bool vdds_dsi_enabled;
	struct regulator *vdds_dsi_reg;

	struct mipi_dsi_device *dsidev;

	struct {
		enum dsi_vc_source source;
		enum fifo_size tx_fifo_size;
		enum fifo_size rx_fifo_size;
	} vc[4];

	struct mutex lock;
	struct semaphore bus_lock;

	spinlock_t irq_lock;
	struct dsi_isr_tables isr_tables;
	/* space for a copy used by the interrupt handler */
	struct dsi_isr_tables isr_tables_copy;

	int update_vc;
#ifdef DSI_PERF_MEASURE
	unsigned int update_bytes;
#endif

	/* external TE GPIO */
	struct gpio_desc *te_gpio;
	int te_irq;
	struct delayed_work te_timeout_work;
	atomic_t do_ext_te_update;

	bool te_enabled;
	bool iface_enabled;
	bool video_enabled;

	struct delayed_work framedone_timeout_work;

#ifdef DSI_CATCH_MISSING_TE
	struct timer_list te_timer;
#endif

	unsigned long cache_req_pck;
	unsigned long cache_clk_freq;
	struct dss_pll_clock_info cache_cinfo;

	u32		errors;
	spinlock_t	errors_lock;
#ifdef DSI_PERF_MEASURE
	ktime_t perf_setup_time;
	ktime_t perf_start_time;
#endif
	int debug_read;
	int debug_write;
	struct {
		struct dss_debugfs_entry *irqs;
		struct dss_debugfs_entry *regs;
		struct dss_debugfs_entry *clks;
	} debugfs;

#ifdef CONFIG_OMAP2_DSS_COLLECT_IRQ_STATS
	spinlock_t irq_stats_lock;
	struct dsi_irq_stats irq_stats;
#endif

	unsigned int num_lanes_supported;
	unsigned int line_buffer_size;

	struct dsi_lane_config lanes[DSI_MAX_NR_LANES];
	unsigned int num_lanes_used;

	unsigned int scp_clk_refcount;

	struct omap_dss_dsi_config config;

	struct dss_lcd_mgr_config mgr_config;
	struct videomode vm;
	enum mipi_dsi_pixel_format pix_fmt;
	enum omap_dss_dsi_mode mode;
	struct omap_dss_dsi_videomode_timings vm_timings;

	struct omap_dss_device output;
	struct drm_bridge bridge;

	struct delayed_work dsi_disable_work;
};

struct dsi_packet_sent_handler_data {
	struct dsi_data *dsi;
	struct completion *completion;
};

#endif /* __OMAP_DRM_DSS_DSI_H */
