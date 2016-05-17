/*
 * linux/drivers/video/omap2/dss/dsi.c
 *
 * Copyright (C) 2009 Nokia Corporation
 * Author: Tomi Valkeinen <tomi.valkeinen@nokia.com>
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

#define DSS_SUBSYS_NAME "DSI"

#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/module.h>
#include <linux/semaphore.h>
#include <linux/seq_file.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/wait.h>
#include <linux/workqueue.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/debugfs.h>
#include <linux/pm_runtime.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/component.h>

#include <video/omapdss.h>
#include <video/mipi_display.h>

#include "dss.h"
#include "dss_features.h"

#define DSI_CATCH_MISSING_TE

struct dsi_reg { u16 module; u16 idx; };

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

#define REG_GET(dsidev, idx, start, end) \
	FLD_GET(dsi_read_reg(dsidev, idx), start, end)

#define REG_FLD_MOD(dsidev, idx, val, start, end) \
	dsi_write_reg(dsidev, idx, FLD_MOD(dsi_read_reg(dsidev, idx), val, start, end))

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

typedef void (*omap_dsi_isr_t) (void *arg, u32 mask);

static int dsi_display_init_dispc(struct platform_device *dsidev,
	enum omap_channel channel);
static void dsi_display_uninit_dispc(struct platform_device *dsidev,
	enum omap_channel channel);

static int dsi_vc_send_null(struct omap_dss_device *dssdev, int channel);

/* DSI PLL HSDIV indices */
#define HSDIV_DISPC	0
#define HSDIV_DSI	1

#define DSI_MAX_NR_ISRS                2
#define DSI_MAX_NR_LANES	5

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
	unsigned irq_count;
	unsigned dsi_irqs[32];
	unsigned vc_irqs[4][32];
	unsigned cio_irqs[32];
};

struct dsi_isr_tables {
	struct dsi_isr_data isr_table[DSI_MAX_NR_ISRS];
	struct dsi_isr_data isr_table_vc[4][DSI_MAX_NR_ISRS];
	struct dsi_isr_data isr_table_cio[DSI_MAX_NR_ISRS];
};

struct dsi_clk_calc_ctx {
	struct platform_device *dsidev;
	struct dss_pll *pll;

	/* inputs */

	const struct omap_dss_dsi_config *config;

	unsigned long req_pck_min, req_pck_nom, req_pck_max;

	/* outputs */

	struct dss_pll_clock_info dsi_cinfo;
	struct dispc_clock_info dispc_cinfo;

	struct omap_video_timings dispc_vm;
	struct omap_dss_dsi_videomode_timings dsi_vm;
};

struct dsi_lp_clock_info {
	unsigned long lp_clk;
	u16 lp_clk_div;
};

struct dsi_data {
	struct platform_device *pdev;
	void __iomem *proto_base;
	void __iomem *phy_base;
	void __iomem *pll_base;

	int module_id;

	int irq;

	bool is_enabled;

	struct clk *dss_clk;

	struct dispc_clock_info user_dispc_cinfo;
	struct dss_pll_clock_info user_dsi_cinfo;

	struct dsi_lp_clock_info user_lp_cinfo;
	struct dsi_lp_clock_info current_lp_cinfo;

	struct dss_pll pll;

	bool vdds_dsi_enabled;
	struct regulator *vdds_dsi_reg;

	struct {
		enum dsi_vc_source source;
		struct omap_dss_device *dssdev;
		enum fifo_size tx_fifo_size;
		enum fifo_size rx_fifo_size;
		int vc_id;
	} vc[4];

	struct mutex lock;
	struct semaphore bus_lock;

	spinlock_t irq_lock;
	struct dsi_isr_tables isr_tables;
	/* space for a copy used by the interrupt handler */
	struct dsi_isr_tables isr_tables_copy;

	int update_channel;
#ifdef DSI_PERF_MEASURE
	unsigned update_bytes;
#endif

	bool te_enabled;
	bool ulps_enabled;

	void (*framedone_callback)(int, void *);
	void *framedone_data;

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

#ifdef CONFIG_OMAP2_DSS_COLLECT_IRQ_STATS
	spinlock_t irq_stats_lock;
	struct dsi_irq_stats irq_stats;
#endif

	unsigned num_lanes_supported;
	unsigned line_buffer_size;

	struct dsi_lane_config lanes[DSI_MAX_NR_LANES];
	unsigned num_lanes_used;

	unsigned scp_clk_refcount;

	struct dss_lcd_mgr_config mgr_config;
	struct omap_video_timings timings;
	enum omap_dss_dsi_pixel_format pix_fmt;
	enum omap_dss_dsi_mode mode;
	struct omap_dss_dsi_videomode_timings vm_timings;

	struct omap_dss_device output;
};

struct dsi_packet_sent_handler_data {
	struct platform_device *dsidev;
	struct completion *completion;
};

struct dsi_module_id_data {
	u32 address;
	int id;
};

static const struct of_device_id dsi_of_match[];

#ifdef DSI_PERF_MEASURE
static bool dsi_perf;
module_param(dsi_perf, bool, 0644);
#endif

static inline struct dsi_data *dsi_get_dsidrv_data(struct platform_device *dsidev)
{
	return dev_get_drvdata(&dsidev->dev);
}

static inline struct platform_device *dsi_get_dsidev_from_dssdev(struct omap_dss_device *dssdev)
{
	return to_platform_device(dssdev->dev);
}

static struct platform_device *dsi_get_dsidev_from_id(int module)
{
	struct omap_dss_device *out;
	enum omap_dss_output_id	id;

	switch (module) {
	case 0:
		id = OMAP_DSS_OUTPUT_DSI1;
		break;
	case 1:
		id = OMAP_DSS_OUTPUT_DSI2;
		break;
	default:
		return NULL;
	}

	out = omap_dss_get_output(id);

	return out ? to_platform_device(out->dev) : NULL;
}

static inline void dsi_write_reg(struct platform_device *dsidev,
		const struct dsi_reg idx, u32 val)
{
	struct dsi_data *dsi = dsi_get_dsidrv_data(dsidev);
	void __iomem *base;

	switch(idx.module) {
		case DSI_PROTO: base = dsi->proto_base; break;
		case DSI_PHY: base = dsi->phy_base; break;
		case DSI_PLL: base = dsi->pll_base; break;
		default: return;
	}

	__raw_writel(val, base + idx.idx);
}

static inline u32 dsi_read_reg(struct platform_device *dsidev,
		const struct dsi_reg idx)
{
	struct dsi_data *dsi = dsi_get_dsidrv_data(dsidev);
	void __iomem *base;

	switch(idx.module) {
		case DSI_PROTO: base = dsi->proto_base; break;
		case DSI_PHY: base = dsi->phy_base; break;
		case DSI_PLL: base = dsi->pll_base; break;
		default: return 0;
	}

	return __raw_readl(base + idx.idx);
}

static void dsi_bus_lock(struct omap_dss_device *dssdev)
{
	struct platform_device *dsidev = dsi_get_dsidev_from_dssdev(dssdev);
	struct dsi_data *dsi = dsi_get_dsidrv_data(dsidev);

	down(&dsi->bus_lock);
}

static void dsi_bus_unlock(struct omap_dss_device *dssdev)
{
	struct platform_device *dsidev = dsi_get_dsidev_from_dssdev(dssdev);
	struct dsi_data *dsi = dsi_get_dsidrv_data(dsidev);

	up(&dsi->bus_lock);
}

static bool dsi_bus_is_locked(struct platform_device *dsidev)
{
	struct dsi_data *dsi = dsi_get_dsidrv_data(dsidev);

	return dsi->bus_lock.count == 0;
}

static void dsi_completion_handler(void *data, u32 mask)
{
	complete((struct completion *)data);
}

static inline int wait_for_bit_change(struct platform_device *dsidev,
		const struct dsi_reg idx, int bitnum, int value)
{
	unsigned long timeout;
	ktime_t wait;
	int t;

	/* first busyloop to see if the bit changes right away */
	t = 100;
	while (t-- > 0) {
		if (REG_GET(dsidev, idx, bitnum, bitnum) == value)
			return value;
	}

	/* then loop for 500ms, sleeping for 1ms in between */
	timeout = jiffies + msecs_to_jiffies(500);
	while (time_before(jiffies, timeout)) {
		if (REG_GET(dsidev, idx, bitnum, bitnum) == value)
			return value;

		wait = ns_to_ktime(1000 * 1000);
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_hrtimeout(&wait, HRTIMER_MODE_REL);
	}

	return !value;
}

u8 dsi_get_pixel_size(enum omap_dss_dsi_pixel_format fmt)
{
	switch (fmt) {
	case OMAP_DSS_DSI_FMT_RGB888:
	case OMAP_DSS_DSI_FMT_RGB666:
		return 24;
	case OMAP_DSS_DSI_FMT_RGB666_PACKED:
		return 18;
	case OMAP_DSS_DSI_FMT_RGB565:
		return 16;
	default:
		BUG();
		return 0;
	}
}

#ifdef DSI_PERF_MEASURE
static void dsi_perf_mark_setup(struct platform_device *dsidev)
{
	struct dsi_data *dsi = dsi_get_dsidrv_data(dsidev);
	dsi->perf_setup_time = ktime_get();
}

static void dsi_perf_mark_start(struct platform_device *dsidev)
{
	struct dsi_data *dsi = dsi_get_dsidrv_data(dsidev);
	dsi->perf_start_time = ktime_get();
}

static void dsi_perf_show(struct platform_device *dsidev, const char *name)
{
	struct dsi_data *dsi = dsi_get_dsidrv_data(dsidev);
	ktime_t t, setup_time, trans_time;
	u32 total_bytes;
	u32 setup_us, trans_us, total_us;

	if (!dsi_perf)
		return;

	t = ktime_get();

	setup_time = ktime_sub(dsi->perf_start_time, dsi->perf_setup_time);
	setup_us = (u32)ktime_to_us(setup_time);
	if (setup_us == 0)
		setup_us = 1;

	trans_time = ktime_sub(t, dsi->perf_start_time);
	trans_us = (u32)ktime_to_us(trans_time);
	if (trans_us == 0)
		trans_us = 1;

	total_us = setup_us + trans_us;

	total_bytes = dsi->update_bytes;

	printk(KERN_INFO "DSI(%s): %u us + %u us = %u us (%uHz), "
			"%u bytes, %u kbytes/sec\n",
			name,
			setup_us,
			trans_us,
			total_us,
			1000*1000 / total_us,
			total_bytes,
			total_bytes * 1000 / total_us);
}
#else
static inline void dsi_perf_mark_setup(struct platform_device *dsidev)
{
}

static inline void dsi_perf_mark_start(struct platform_device *dsidev)
{
}

static inline void dsi_perf_show(struct platform_device *dsidev,
		const char *name)
{
}
#endif

static int verbose_irq;

static void print_irq_status(u32 status)
{
	if (status == 0)
		return;

	if (!verbose_irq && (status & ~DSI_IRQ_CHANNEL_MASK) == 0)
		return;

#define PIS(x) (status & DSI_IRQ_##x) ? (#x " ") : ""

	pr_debug("DSI IRQ: 0x%x: %s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s\n",
		status,
		verbose_irq ? PIS(VC0) : "",
		verbose_irq ? PIS(VC1) : "",
		verbose_irq ? PIS(VC2) : "",
		verbose_irq ? PIS(VC3) : "",
		PIS(WAKEUP),
		PIS(RESYNC),
		PIS(PLL_LOCK),
		PIS(PLL_UNLOCK),
		PIS(PLL_RECALL),
		PIS(COMPLEXIO_ERR),
		PIS(HS_TX_TIMEOUT),
		PIS(LP_RX_TIMEOUT),
		PIS(TE_TRIGGER),
		PIS(ACK_TRIGGER),
		PIS(SYNC_LOST),
		PIS(LDO_POWER_GOOD),
		PIS(TA_TIMEOUT));
#undef PIS
}

static void print_irq_status_vc(int channel, u32 status)
{
	if (status == 0)
		return;

	if (!verbose_irq && (status & ~DSI_VC_IRQ_PACKET_SENT) == 0)
		return;

#define PIS(x) (status & DSI_VC_IRQ_##x) ? (#x " ") : ""

	pr_debug("DSI VC(%d) IRQ 0x%x: %s%s%s%s%s%s%s%s%s\n",
		channel,
		status,
		PIS(CS),
		PIS(ECC_CORR),
		PIS(ECC_NO_CORR),
		verbose_irq ? PIS(PACKET_SENT) : "",
		PIS(BTA),
		PIS(FIFO_TX_OVF),
		PIS(FIFO_RX_OVF),
		PIS(FIFO_TX_UDF),
		PIS(PP_BUSY_CHANGE));
#undef PIS
}

static void print_irq_status_cio(u32 status)
{
	if (status == 0)
		return;

#define PIS(x) (status & DSI_CIO_IRQ_##x) ? (#x " ") : ""

	pr_debug("DSI CIO IRQ 0x%x: %s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s\n",
		status,
		PIS(ERRSYNCESC1),
		PIS(ERRSYNCESC2),
		PIS(ERRSYNCESC3),
		PIS(ERRESC1),
		PIS(ERRESC2),
		PIS(ERRESC3),
		PIS(ERRCONTROL1),
		PIS(ERRCONTROL2),
		PIS(ERRCONTROL3),
		PIS(STATEULPS1),
		PIS(STATEULPS2),
		PIS(STATEULPS3),
		PIS(ERRCONTENTIONLP0_1),
		PIS(ERRCONTENTIONLP1_1),
		PIS(ERRCONTENTIONLP0_2),
		PIS(ERRCONTENTIONLP1_2),
		PIS(ERRCONTENTIONLP0_3),
		PIS(ERRCONTENTIONLP1_3),
		PIS(ULPSACTIVENOT_ALL0),
		PIS(ULPSACTIVENOT_ALL1));
#undef PIS
}

#ifdef CONFIG_OMAP2_DSS_COLLECT_IRQ_STATS
static void dsi_collect_irq_stats(struct platform_device *dsidev, u32 irqstatus,
		u32 *vcstatus, u32 ciostatus)
{
	struct dsi_data *dsi = dsi_get_dsidrv_data(dsidev);
	int i;

	spin_lock(&dsi->irq_stats_lock);

	dsi->irq_stats.irq_count++;
	dss_collect_irq_stats(irqstatus, dsi->irq_stats.dsi_irqs);

	for (i = 0; i < 4; ++i)
		dss_collect_irq_stats(vcstatus[i], dsi->irq_stats.vc_irqs[i]);

	dss_collect_irq_stats(ciostatus, dsi->irq_stats.cio_irqs);

	spin_unlock(&dsi->irq_stats_lock);
}
#else
#define dsi_collect_irq_stats(dsidev, irqstatus, vcstatus, ciostatus)
#endif

static int debug_irq;

static void dsi_handle_irq_errors(struct platform_device *dsidev, u32 irqstatus,
		u32 *vcstatus, u32 ciostatus)
{
	struct dsi_data *dsi = dsi_get_dsidrv_data(dsidev);
	int i;

	if (irqstatus & DSI_IRQ_ERROR_MASK) {
		DSSERR("DSI error, irqstatus %x\n", irqstatus);
		print_irq_status(irqstatus);
		spin_lock(&dsi->errors_lock);
		dsi->errors |= irqstatus & DSI_IRQ_ERROR_MASK;
		spin_unlock(&dsi->errors_lock);
	} else if (debug_irq) {
		print_irq_status(irqstatus);
	}

	for (i = 0; i < 4; ++i) {
		if (vcstatus[i] & DSI_VC_IRQ_ERROR_MASK) {
			DSSERR("DSI VC(%d) error, vc irqstatus %x\n",
				       i, vcstatus[i]);
			print_irq_status_vc(i, vcstatus[i]);
		} else if (debug_irq) {
			print_irq_status_vc(i, vcstatus[i]);
		}
	}

	if (ciostatus & DSI_CIO_IRQ_ERROR_MASK) {
		DSSERR("DSI CIO error, cio irqstatus %x\n", ciostatus);
		print_irq_status_cio(ciostatus);
	} else if (debug_irq) {
		print_irq_status_cio(ciostatus);
	}
}

static void dsi_call_isrs(struct dsi_isr_data *isr_array,
		unsigned isr_array_size, u32 irqstatus)
{
	struct dsi_isr_data *isr_data;
	int i;

	for (i = 0; i < isr_array_size; i++) {
		isr_data = &isr_array[i];
		if (isr_data->isr && isr_data->mask & irqstatus)
			isr_data->isr(isr_data->arg, irqstatus);
	}
}

static void dsi_handle_isrs(struct dsi_isr_tables *isr_tables,
		u32 irqstatus, u32 *vcstatus, u32 ciostatus)
{
	int i;

	dsi_call_isrs(isr_tables->isr_table,
			ARRAY_SIZE(isr_tables->isr_table),
			irqstatus);

	for (i = 0; i < 4; ++i) {
		if (vcstatus[i] == 0)
			continue;
		dsi_call_isrs(isr_tables->isr_table_vc[i],
				ARRAY_SIZE(isr_tables->isr_table_vc[i]),
				vcstatus[i]);
	}

	if (ciostatus != 0)
		dsi_call_isrs(isr_tables->isr_table_cio,
				ARRAY_SIZE(isr_tables->isr_table_cio),
				ciostatus);
}

static irqreturn_t omap_dsi_irq_handler(int irq, void *arg)
{
	struct platform_device *dsidev;
	struct dsi_data *dsi;
	u32 irqstatus, vcstatus[4], ciostatus;
	int i;

	dsidev = (struct platform_device *) arg;
	dsi = dsi_get_dsidrv_data(dsidev);

	if (!dsi->is_enabled)
		return IRQ_NONE;

	spin_lock(&dsi->irq_lock);

	irqstatus = dsi_read_reg(dsidev, DSI_IRQSTATUS);

	/* IRQ is not for us */
	if (!irqstatus) {
		spin_unlock(&dsi->irq_lock);
		return IRQ_NONE;
	}

	dsi_write_reg(dsidev, DSI_IRQSTATUS, irqstatus & ~DSI_IRQ_CHANNEL_MASK);
	/* flush posted write */
	dsi_read_reg(dsidev, DSI_IRQSTATUS);

	for (i = 0; i < 4; ++i) {
		if ((irqstatus & (1 << i)) == 0) {
			vcstatus[i] = 0;
			continue;
		}

		vcstatus[i] = dsi_read_reg(dsidev, DSI_VC_IRQSTATUS(i));

		dsi_write_reg(dsidev, DSI_VC_IRQSTATUS(i), vcstatus[i]);
		/* flush posted write */
		dsi_read_reg(dsidev, DSI_VC_IRQSTATUS(i));
	}

	if (irqstatus & DSI_IRQ_COMPLEXIO_ERR) {
		ciostatus = dsi_read_reg(dsidev, DSI_COMPLEXIO_IRQ_STATUS);

		dsi_write_reg(dsidev, DSI_COMPLEXIO_IRQ_STATUS, ciostatus);
		/* flush posted write */
		dsi_read_reg(dsidev, DSI_COMPLEXIO_IRQ_STATUS);
	} else {
		ciostatus = 0;
	}

#ifdef DSI_CATCH_MISSING_TE
	if (irqstatus & DSI_IRQ_TE_TRIGGER)
		del_timer(&dsi->te_timer);
#endif

	/* make a copy and unlock, so that isrs can unregister
	 * themselves */
	memcpy(&dsi->isr_tables_copy, &dsi->isr_tables,
		sizeof(dsi->isr_tables));

	spin_unlock(&dsi->irq_lock);

	dsi_handle_isrs(&dsi->isr_tables_copy, irqstatus, vcstatus, ciostatus);

	dsi_handle_irq_errors(dsidev, irqstatus, vcstatus, ciostatus);

	dsi_collect_irq_stats(dsidev, irqstatus, vcstatus, ciostatus);

	return IRQ_HANDLED;
}

/* dsi->irq_lock has to be locked by the caller */
static void _omap_dsi_configure_irqs(struct platform_device *dsidev,
		struct dsi_isr_data *isr_array,
		unsigned isr_array_size, u32 default_mask,
		const struct dsi_reg enable_reg,
		const struct dsi_reg status_reg)
{
	struct dsi_isr_data *isr_data;
	u32 mask;
	u32 old_mask;
	int i;

	mask = default_mask;

	for (i = 0; i < isr_array_size; i++) {
		isr_data = &isr_array[i];

		if (isr_data->isr == NULL)
			continue;

		mask |= isr_data->mask;
	}

	old_mask = dsi_read_reg(dsidev, enable_reg);
	/* clear the irqstatus for newly enabled irqs */
	dsi_write_reg(dsidev, status_reg, (mask ^ old_mask) & mask);
	dsi_write_reg(dsidev, enable_reg, mask);

	/* flush posted writes */
	dsi_read_reg(dsidev, enable_reg);
	dsi_read_reg(dsidev, status_reg);
}

/* dsi->irq_lock has to be locked by the caller */
static void _omap_dsi_set_irqs(struct platform_device *dsidev)
{
	struct dsi_data *dsi = dsi_get_dsidrv_data(dsidev);
	u32 mask = DSI_IRQ_ERROR_MASK;
#ifdef DSI_CATCH_MISSING_TE
	mask |= DSI_IRQ_TE_TRIGGER;
#endif
	_omap_dsi_configure_irqs(dsidev, dsi->isr_tables.isr_table,
			ARRAY_SIZE(dsi->isr_tables.isr_table), mask,
			DSI_IRQENABLE, DSI_IRQSTATUS);
}

/* dsi->irq_lock has to be locked by the caller */
static void _omap_dsi_set_irqs_vc(struct platform_device *dsidev, int vc)
{
	struct dsi_data *dsi = dsi_get_dsidrv_data(dsidev);

	_omap_dsi_configure_irqs(dsidev, dsi->isr_tables.isr_table_vc[vc],
			ARRAY_SIZE(dsi->isr_tables.isr_table_vc[vc]),
			DSI_VC_IRQ_ERROR_MASK,
			DSI_VC_IRQENABLE(vc), DSI_VC_IRQSTATUS(vc));
}

/* dsi->irq_lock has to be locked by the caller */
static void _omap_dsi_set_irqs_cio(struct platform_device *dsidev)
{
	struct dsi_data *dsi = dsi_get_dsidrv_data(dsidev);

	_omap_dsi_configure_irqs(dsidev, dsi->isr_tables.isr_table_cio,
			ARRAY_SIZE(dsi->isr_tables.isr_table_cio),
			DSI_CIO_IRQ_ERROR_MASK,
			DSI_COMPLEXIO_IRQ_ENABLE, DSI_COMPLEXIO_IRQ_STATUS);
}

static void _dsi_initialize_irq(struct platform_device *dsidev)
{
	struct dsi_data *dsi = dsi_get_dsidrv_data(dsidev);
	unsigned long flags;
	int vc;

	spin_lock_irqsave(&dsi->irq_lock, flags);

	memset(&dsi->isr_tables, 0, sizeof(dsi->isr_tables));

	_omap_dsi_set_irqs(dsidev);
	for (vc = 0; vc < 4; ++vc)
		_omap_dsi_set_irqs_vc(dsidev, vc);
	_omap_dsi_set_irqs_cio(dsidev);

	spin_unlock_irqrestore(&dsi->irq_lock, flags);
}

static int _dsi_register_isr(omap_dsi_isr_t isr, void *arg, u32 mask,
		struct dsi_isr_data *isr_array, unsigned isr_array_size)
{
	struct dsi_isr_data *isr_data;
	int free_idx;
	int i;

	BUG_ON(isr == NULL);

	/* check for duplicate entry and find a free slot */
	free_idx = -1;
	for (i = 0; i < isr_array_size; i++) {
		isr_data = &isr_array[i];

		if (isr_data->isr == isr && isr_data->arg == arg &&
				isr_data->mask == mask) {
			return -EINVAL;
		}

		if (isr_data->isr == NULL && free_idx == -1)
			free_idx = i;
	}

	if (free_idx == -1)
		return -EBUSY;

	isr_data = &isr_array[free_idx];
	isr_data->isr = isr;
	isr_data->arg = arg;
	isr_data->mask = mask;

	return 0;
}

static int _dsi_unregister_isr(omap_dsi_isr_t isr, void *arg, u32 mask,
		struct dsi_isr_data *isr_array, unsigned isr_array_size)
{
	struct dsi_isr_data *isr_data;
	int i;

	for (i = 0; i < isr_array_size; i++) {
		isr_data = &isr_array[i];
		if (isr_data->isr != isr || isr_data->arg != arg ||
				isr_data->mask != mask)
			continue;

		isr_data->isr = NULL;
		isr_data->arg = NULL;
		isr_data->mask = 0;

		return 0;
	}

	return -EINVAL;
}

static int dsi_register_isr(struct platform_device *dsidev, omap_dsi_isr_t isr,
		void *arg, u32 mask)
{
	struct dsi_data *dsi = dsi_get_dsidrv_data(dsidev);
	unsigned long flags;
	int r;

	spin_lock_irqsave(&dsi->irq_lock, flags);

	r = _dsi_register_isr(isr, arg, mask, dsi->isr_tables.isr_table,
			ARRAY_SIZE(dsi->isr_tables.isr_table));

	if (r == 0)
		_omap_dsi_set_irqs(dsidev);

	spin_unlock_irqrestore(&dsi->irq_lock, flags);

	return r;
}

static int dsi_unregister_isr(struct platform_device *dsidev,
		omap_dsi_isr_t isr, void *arg, u32 mask)
{
	struct dsi_data *dsi = dsi_get_dsidrv_data(dsidev);
	unsigned long flags;
	int r;

	spin_lock_irqsave(&dsi->irq_lock, flags);

	r = _dsi_unregister_isr(isr, arg, mask, dsi->isr_tables.isr_table,
			ARRAY_SIZE(dsi->isr_tables.isr_table));

	if (r == 0)
		_omap_dsi_set_irqs(dsidev);

	spin_unlock_irqrestore(&dsi->irq_lock, flags);

	return r;
}

static int dsi_register_isr_vc(struct platform_device *dsidev, int channel,
		omap_dsi_isr_t isr, void *arg, u32 mask)
{
	struct dsi_data *dsi = dsi_get_dsidrv_data(dsidev);
	unsigned long flags;
	int r;

	spin_lock_irqsave(&dsi->irq_lock, flags);

	r = _dsi_register_isr(isr, arg, mask,
			dsi->isr_tables.isr_table_vc[channel],
			ARRAY_SIZE(dsi->isr_tables.isr_table_vc[channel]));

	if (r == 0)
		_omap_dsi_set_irqs_vc(dsidev, channel);

	spin_unlock_irqrestore(&dsi->irq_lock, flags);

	return r;
}

static int dsi_unregister_isr_vc(struct platform_device *dsidev, int channel,
		omap_dsi_isr_t isr, void *arg, u32 mask)
{
	struct dsi_data *dsi = dsi_get_dsidrv_data(dsidev);
	unsigned long flags;
	int r;

	spin_lock_irqsave(&dsi->irq_lock, flags);

	r = _dsi_unregister_isr(isr, arg, mask,
			dsi->isr_tables.isr_table_vc[channel],
			ARRAY_SIZE(dsi->isr_tables.isr_table_vc[channel]));

	if (r == 0)
		_omap_dsi_set_irqs_vc(dsidev, channel);

	spin_unlock_irqrestore(&dsi->irq_lock, flags);

	return r;
}

static int dsi_register_isr_cio(struct platform_device *dsidev,
		omap_dsi_isr_t isr, void *arg, u32 mask)
{
	struct dsi_data *dsi = dsi_get_dsidrv_data(dsidev);
	unsigned long flags;
	int r;

	spin_lock_irqsave(&dsi->irq_lock, flags);

	r = _dsi_register_isr(isr, arg, mask, dsi->isr_tables.isr_table_cio,
			ARRAY_SIZE(dsi->isr_tables.isr_table_cio));

	if (r == 0)
		_omap_dsi_set_irqs_cio(dsidev);

	spin_unlock_irqrestore(&dsi->irq_lock, flags);

	return r;
}

static int dsi_unregister_isr_cio(struct platform_device *dsidev,
		omap_dsi_isr_t isr, void *arg, u32 mask)
{
	struct dsi_data *dsi = dsi_get_dsidrv_data(dsidev);
	unsigned long flags;
	int r;

	spin_lock_irqsave(&dsi->irq_lock, flags);

	r = _dsi_unregister_isr(isr, arg, mask, dsi->isr_tables.isr_table_cio,
			ARRAY_SIZE(dsi->isr_tables.isr_table_cio));

	if (r == 0)
		_omap_dsi_set_irqs_cio(dsidev);

	spin_unlock_irqrestore(&dsi->irq_lock, flags);

	return r;
}

static u32 dsi_get_errors(struct platform_device *dsidev)
{
	struct dsi_data *dsi = dsi_get_dsidrv_data(dsidev);
	unsigned long flags;
	u32 e;
	spin_lock_irqsave(&dsi->errors_lock, flags);
	e = dsi->errors;
	dsi->errors = 0;
	spin_unlock_irqrestore(&dsi->errors_lock, flags);
	return e;
}

static int dsi_runtime_get(struct platform_device *dsidev)
{
	int r;
	struct dsi_data *dsi = dsi_get_dsidrv_data(dsidev);

	DSSDBG("dsi_runtime_get\n");

	r = pm_runtime_get_sync(&dsi->pdev->dev);
	WARN_ON(r < 0);
	return r < 0 ? r : 0;
}

static void dsi_runtime_put(struct platform_device *dsidev)
{
	struct dsi_data *dsi = dsi_get_dsidrv_data(dsidev);
	int r;

	DSSDBG("dsi_runtime_put\n");

	r = pm_runtime_put_sync(&dsi->pdev->dev);
	WARN_ON(r < 0 && r != -ENOSYS);
}

static int dsi_regulator_init(struct platform_device *dsidev)
{
	struct dsi_data *dsi = dsi_get_dsidrv_data(dsidev);
	struct regulator *vdds_dsi;
	int r;

	if (dsi->vdds_dsi_reg != NULL)
		return 0;

	vdds_dsi = devm_regulator_get(&dsi->pdev->dev, "vdd");

	if (IS_ERR(vdds_dsi)) {
		if (PTR_ERR(vdds_dsi) != -EPROBE_DEFER)
			DSSERR("can't get DSI VDD regulator\n");
		return PTR_ERR(vdds_dsi);
	}

	if (regulator_can_change_voltage(vdds_dsi)) {
		r = regulator_set_voltage(vdds_dsi, 1800000, 1800000);
		if (r) {
			devm_regulator_put(vdds_dsi);
			DSSERR("can't set the DSI regulator voltage\n");
			return r;
		}
	}

	dsi->vdds_dsi_reg = vdds_dsi;

	return 0;
}

static void _dsi_print_reset_status(struct platform_device *dsidev)
{
	u32 l;
	int b0, b1, b2;

	/* A dummy read using the SCP interface to any DSIPHY register is
	 * required after DSIPHY reset to complete the reset of the DSI complex
	 * I/O. */
	l = dsi_read_reg(dsidev, DSI_DSIPHY_CFG5);

	if (dss_has_feature(FEAT_DSI_REVERSE_TXCLKESC)) {
		b0 = 28;
		b1 = 27;
		b2 = 26;
	} else {
		b0 = 24;
		b1 = 25;
		b2 = 26;
	}

#define DSI_FLD_GET(fld, start, end)\
	FLD_GET(dsi_read_reg(dsidev, DSI_##fld), start, end)

	pr_debug("DSI resets: PLL (%d) CIO (%d) PHY (%x%x%x, %d, %d, %d)\n",
		DSI_FLD_GET(PLL_STATUS, 0, 0),
		DSI_FLD_GET(COMPLEXIO_CFG1, 29, 29),
		DSI_FLD_GET(DSIPHY_CFG5, b0, b0),
		DSI_FLD_GET(DSIPHY_CFG5, b1, b1),
		DSI_FLD_GET(DSIPHY_CFG5, b2, b2),
		DSI_FLD_GET(DSIPHY_CFG5, 29, 29),
		DSI_FLD_GET(DSIPHY_CFG5, 30, 30),
		DSI_FLD_GET(DSIPHY_CFG5, 31, 31));

#undef DSI_FLD_GET
}

static inline int dsi_if_enable(struct platform_device *dsidev, bool enable)
{
	DSSDBG("dsi_if_enable(%d)\n", enable);

	enable = enable ? 1 : 0;
	REG_FLD_MOD(dsidev, DSI_CTRL, enable, 0, 0); /* IF_EN */

	if (wait_for_bit_change(dsidev, DSI_CTRL, 0, enable) != enable) {
			DSSERR("Failed to set dsi_if_enable to %d\n", enable);
			return -EIO;
	}

	return 0;
}

static unsigned long dsi_get_pll_hsdiv_dispc_rate(struct platform_device *dsidev)
{
	struct dsi_data *dsi = dsi_get_dsidrv_data(dsidev);

	return dsi->pll.cinfo.clkout[HSDIV_DISPC];
}

static unsigned long dsi_get_pll_hsdiv_dsi_rate(struct platform_device *dsidev)
{
	struct dsi_data *dsi = dsi_get_dsidrv_data(dsidev);

	return dsi->pll.cinfo.clkout[HSDIV_DSI];
}

static unsigned long dsi_get_txbyteclkhs(struct platform_device *dsidev)
{
	struct dsi_data *dsi = dsi_get_dsidrv_data(dsidev);

	return dsi->pll.cinfo.clkdco / 16;
}

static unsigned long dsi_fclk_rate(struct platform_device *dsidev)
{
	unsigned long r;
	struct dsi_data *dsi = dsi_get_dsidrv_data(dsidev);

	if (dss_get_dsi_clk_source(dsi->module_id) == OMAP_DSS_CLK_SRC_FCK) {
		/* DSI FCLK source is DSS_CLK_FCK */
		r = clk_get_rate(dsi->dss_clk);
	} else {
		/* DSI FCLK source is dsi_pll_hsdiv_dsi_clk */
		r = dsi_get_pll_hsdiv_dsi_rate(dsidev);
	}

	return r;
}

static int dsi_lp_clock_calc(unsigned long dsi_fclk,
		unsigned long lp_clk_min, unsigned long lp_clk_max,
		struct dsi_lp_clock_info *lp_cinfo)
{
	unsigned lp_clk_div;
	unsigned long lp_clk;

	lp_clk_div = DIV_ROUND_UP(dsi_fclk, lp_clk_max * 2);
	lp_clk = dsi_fclk / 2 / lp_clk_div;

	if (lp_clk < lp_clk_min || lp_clk > lp_clk_max)
		return -EINVAL;

	lp_cinfo->lp_clk_div = lp_clk_div;
	lp_cinfo->lp_clk = lp_clk;

	return 0;
}

static int dsi_set_lp_clk_divisor(struct platform_device *dsidev)
{
	struct dsi_data *dsi = dsi_get_dsidrv_data(dsidev);
	unsigned long dsi_fclk;
	unsigned lp_clk_div;
	unsigned long lp_clk;
	unsigned lpdiv_max = dss_feat_get_param_max(FEAT_PARAM_DSIPLL_LPDIV);


	lp_clk_div = dsi->user_lp_cinfo.lp_clk_div;

	if (lp_clk_div == 0 || lp_clk_div > lpdiv_max)
		return -EINVAL;

	dsi_fclk = dsi_fclk_rate(dsidev);

	lp_clk = dsi_fclk / 2 / lp_clk_div;

	DSSDBG("LP_CLK_DIV %u, LP_CLK %lu\n", lp_clk_div, lp_clk);
	dsi->current_lp_cinfo.lp_clk = lp_clk;
	dsi->current_lp_cinfo.lp_clk_div = lp_clk_div;

	/* LP_CLK_DIVISOR */
	REG_FLD_MOD(dsidev, DSI_CLK_CTRL, lp_clk_div, 12, 0);

	/* LP_RX_SYNCHRO_ENABLE */
	REG_FLD_MOD(dsidev, DSI_CLK_CTRL, dsi_fclk > 30000000 ? 1 : 0, 21, 21);

	return 0;
}

static void dsi_enable_scp_clk(struct platform_device *dsidev)
{
	struct dsi_data *dsi = dsi_get_dsidrv_data(dsidev);

	if (dsi->scp_clk_refcount++ == 0)
		REG_FLD_MOD(dsidev, DSI_CLK_CTRL, 1, 14, 14); /* CIO_CLK_ICG */
}

static void dsi_disable_scp_clk(struct platform_device *dsidev)
{
	struct dsi_data *dsi = dsi_get_dsidrv_data(dsidev);

	WARN_ON(dsi->scp_clk_refcount == 0);
	if (--dsi->scp_clk_refcount == 0)
		REG_FLD_MOD(dsidev, DSI_CLK_CTRL, 0, 14, 14); /* CIO_CLK_ICG */
}

enum dsi_pll_power_state {
	DSI_PLL_POWER_OFF	= 0x0,
	DSI_PLL_POWER_ON_HSCLK	= 0x1,
	DSI_PLL_POWER_ON_ALL	= 0x2,
	DSI_PLL_POWER_ON_DIV	= 0x3,
};

static int dsi_pll_power(struct platform_device *dsidev,
		enum dsi_pll_power_state state)
{
	int t = 0;

	/* DSI-PLL power command 0x3 is not working */
	if (dss_has_feature(FEAT_DSI_PLL_PWR_BUG) &&
			state == DSI_PLL_POWER_ON_DIV)
		state = DSI_PLL_POWER_ON_ALL;

	/* PLL_PWR_CMD */
	REG_FLD_MOD(dsidev, DSI_CLK_CTRL, state, 31, 30);

	/* PLL_PWR_STATUS */
	while (FLD_GET(dsi_read_reg(dsidev, DSI_CLK_CTRL), 29, 28) != state) {
		if (++t > 1000) {
			DSSERR("Failed to set DSI PLL power mode to %d\n",
					state);
			return -ENODEV;
		}
		udelay(1);
	}

	return 0;
}


static void dsi_pll_calc_dsi_fck(struct dss_pll_clock_info *cinfo)
{
	unsigned long max_dsi_fck;

	max_dsi_fck = dss_feat_get_param_max(FEAT_PARAM_DSI_FCK);

	cinfo->mX[HSDIV_DSI] = DIV_ROUND_UP(cinfo->clkdco, max_dsi_fck);
	cinfo->clkout[HSDIV_DSI] = cinfo->clkdco / cinfo->mX[HSDIV_DSI];
}

static int dsi_pll_enable(struct dss_pll *pll)
{
	struct dsi_data *dsi = container_of(pll, struct dsi_data, pll);
	struct platform_device *dsidev = dsi->pdev;
	int r = 0;

	DSSDBG("PLL init\n");

	r = dsi_regulator_init(dsidev);
	if (r)
		return r;

	r = dsi_runtime_get(dsidev);
	if (r)
		return r;

	/*
	 * Note: SCP CLK is not required on OMAP3, but it is required on OMAP4.
	 */
	dsi_enable_scp_clk(dsidev);

	if (!dsi->vdds_dsi_enabled) {
		r = regulator_enable(dsi->vdds_dsi_reg);
		if (r)
			goto err0;
		dsi->vdds_dsi_enabled = true;
	}

	/* XXX PLL does not come out of reset without this... */
	dispc_pck_free_enable(1);

	if (wait_for_bit_change(dsidev, DSI_PLL_STATUS, 0, 1) != 1) {
		DSSERR("PLL not coming out of reset.\n");
		r = -ENODEV;
		dispc_pck_free_enable(0);
		goto err1;
	}

	/* XXX ... but if left on, we get problems when planes do not
	 * fill the whole display. No idea about this */
	dispc_pck_free_enable(0);

	r = dsi_pll_power(dsidev, DSI_PLL_POWER_ON_ALL);

	if (r)
		goto err1;

	DSSDBG("PLL init done\n");

	return 0;
err1:
	if (dsi->vdds_dsi_enabled) {
		regulator_disable(dsi->vdds_dsi_reg);
		dsi->vdds_dsi_enabled = false;
	}
err0:
	dsi_disable_scp_clk(dsidev);
	dsi_runtime_put(dsidev);
	return r;
}

static void dsi_pll_uninit(struct platform_device *dsidev, bool disconnect_lanes)
{
	struct dsi_data *dsi = dsi_get_dsidrv_data(dsidev);

	dsi_pll_power(dsidev, DSI_PLL_POWER_OFF);
	if (disconnect_lanes) {
		WARN_ON(!dsi->vdds_dsi_enabled);
		regulator_disable(dsi->vdds_dsi_reg);
		dsi->vdds_dsi_enabled = false;
	}

	dsi_disable_scp_clk(dsidev);
	dsi_runtime_put(dsidev);

	DSSDBG("PLL uninit done\n");
}

static void dsi_pll_disable(struct dss_pll *pll)
{
	struct dsi_data *dsi = container_of(pll, struct dsi_data, pll);
	struct platform_device *dsidev = dsi->pdev;

	dsi_pll_uninit(dsidev, true);
}

static void dsi_dump_dsidev_clocks(struct platform_device *dsidev,
		struct seq_file *s)
{
	struct dsi_data *dsi = dsi_get_dsidrv_data(dsidev);
	struct dss_pll_clock_info *cinfo = &dsi->pll.cinfo;
	enum dss_clk_source dispc_clk_src, dsi_clk_src;
	int dsi_module = dsi->module_id;
	struct dss_pll *pll = &dsi->pll;

	dispc_clk_src = dss_get_dispc_clk_source();
	dsi_clk_src = dss_get_dsi_clk_source(dsi_module);

	if (dsi_runtime_get(dsidev))
		return;

	seq_printf(s,	"- DSI%d PLL -\n", dsi_module + 1);

	seq_printf(s,	"dsi pll clkin\t%lu\n", clk_get_rate(pll->clkin));

	seq_printf(s,	"Fint\t\t%-16lun %u\n", cinfo->fint, cinfo->n);

	seq_printf(s,	"CLKIN4DDR\t%-16lum %u\n",
			cinfo->clkdco, cinfo->m);

	seq_printf(s,	"DSI_PLL_HSDIV_DISPC (%s)\t%-16lum_dispc %u\t(%s)\n",
			dss_get_generic_clk_source_name(dsi_module == 0 ?
				OMAP_DSS_CLK_SRC_DSI_PLL_HSDIV_DISPC :
				OMAP_DSS_CLK_SRC_DSI2_PLL_HSDIV_DISPC),
			cinfo->clkout[HSDIV_DISPC],
			cinfo->mX[HSDIV_DISPC],
			dispc_clk_src == OMAP_DSS_CLK_SRC_FCK ?
			"off" : "on");

	seq_printf(s,	"DSI_PLL_HSDIV_DSI (%s)\t%-16lum_dsi %u\t(%s)\n",
			dss_get_generic_clk_source_name(dsi_module == 0 ?
				OMAP_DSS_CLK_SRC_DSI_PLL_HSDIV_DSI :
				OMAP_DSS_CLK_SRC_DSI2_PLL_HSDIV_DSI),
			cinfo->clkout[HSDIV_DSI],
			cinfo->mX[HSDIV_DSI],
			dsi_clk_src == OMAP_DSS_CLK_SRC_FCK ?
			"off" : "on");

	seq_printf(s,	"- DSI%d -\n", dsi_module + 1);

	seq_printf(s,	"dsi fclk source = %s\n",
			dss_get_generic_clk_source_name(dsi_clk_src));

	seq_printf(s,	"DSI_FCLK\t%lu\n", dsi_fclk_rate(dsidev));

	seq_printf(s,	"DDR_CLK\t\t%lu\n",
			cinfo->clkdco / 4);

	seq_printf(s,	"TxByteClkHS\t%lu\n", dsi_get_txbyteclkhs(dsidev));

	seq_printf(s,	"LP_CLK\t\t%lu\n", dsi->current_lp_cinfo.lp_clk);

	dsi_runtime_put(dsidev);
}

void dsi_dump_clocks(struct seq_file *s)
{
	struct platform_device *dsidev;
	int i;

	for  (i = 0; i < MAX_NUM_DSI; i++) {
		dsidev = dsi_get_dsidev_from_id(i);
		if (dsidev)
			dsi_dump_dsidev_clocks(dsidev, s);
	}
}

#ifdef CONFIG_OMAP2_DSS_COLLECT_IRQ_STATS
static void dsi_dump_dsidev_irqs(struct platform_device *dsidev,
		struct seq_file *s)
{
	struct dsi_data *dsi = dsi_get_dsidrv_data(dsidev);
	unsigned long flags;
	struct dsi_irq_stats stats;

	spin_lock_irqsave(&dsi->irq_stats_lock, flags);

	stats = dsi->irq_stats;
	memset(&dsi->irq_stats, 0, sizeof(dsi->irq_stats));
	dsi->irq_stats.last_reset = jiffies;

	spin_unlock_irqrestore(&dsi->irq_stats_lock, flags);

	seq_printf(s, "period %u ms\n",
			jiffies_to_msecs(jiffies - stats.last_reset));

	seq_printf(s, "irqs %d\n", stats.irq_count);
#define PIS(x) \
	seq_printf(s, "%-20s %10d\n", #x, stats.dsi_irqs[ffs(DSI_IRQ_##x)-1]);

	seq_printf(s, "-- DSI%d interrupts --\n", dsi->module_id + 1);
	PIS(VC0);
	PIS(VC1);
	PIS(VC2);
	PIS(VC3);
	PIS(WAKEUP);
	PIS(RESYNC);
	PIS(PLL_LOCK);
	PIS(PLL_UNLOCK);
	PIS(PLL_RECALL);
	PIS(COMPLEXIO_ERR);
	PIS(HS_TX_TIMEOUT);
	PIS(LP_RX_TIMEOUT);
	PIS(TE_TRIGGER);
	PIS(ACK_TRIGGER);
	PIS(SYNC_LOST);
	PIS(LDO_POWER_GOOD);
	PIS(TA_TIMEOUT);
#undef PIS

#define PIS(x) \
	seq_printf(s, "%-20s %10d %10d %10d %10d\n", #x, \
			stats.vc_irqs[0][ffs(DSI_VC_IRQ_##x)-1], \
			stats.vc_irqs[1][ffs(DSI_VC_IRQ_##x)-1], \
			stats.vc_irqs[2][ffs(DSI_VC_IRQ_##x)-1], \
			stats.vc_irqs[3][ffs(DSI_VC_IRQ_##x)-1]);

	seq_printf(s, "-- VC interrupts --\n");
	PIS(CS);
	PIS(ECC_CORR);
	PIS(PACKET_SENT);
	PIS(FIFO_TX_OVF);
	PIS(FIFO_RX_OVF);
	PIS(BTA);
	PIS(ECC_NO_CORR);
	PIS(FIFO_TX_UDF);
	PIS(PP_BUSY_CHANGE);
#undef PIS

#define PIS(x) \
	seq_printf(s, "%-20s %10d\n", #x, \
			stats.cio_irqs[ffs(DSI_CIO_IRQ_##x)-1]);

	seq_printf(s, "-- CIO interrupts --\n");
	PIS(ERRSYNCESC1);
	PIS(ERRSYNCESC2);
	PIS(ERRSYNCESC3);
	PIS(ERRESC1);
	PIS(ERRESC2);
	PIS(ERRESC3);
	PIS(ERRCONTROL1);
	PIS(ERRCONTROL2);
	PIS(ERRCONTROL3);
	PIS(STATEULPS1);
	PIS(STATEULPS2);
	PIS(STATEULPS3);
	PIS(ERRCONTENTIONLP0_1);
	PIS(ERRCONTENTIONLP1_1);
	PIS(ERRCONTENTIONLP0_2);
	PIS(ERRCONTENTIONLP1_2);
	PIS(ERRCONTENTIONLP0_3);
	PIS(ERRCONTENTIONLP1_3);
	PIS(ULPSACTIVENOT_ALL0);
	PIS(ULPSACTIVENOT_ALL1);
#undef PIS
}

static void dsi1_dump_irqs(struct seq_file *s)
{
	struct platform_device *dsidev = dsi_get_dsidev_from_id(0);

	dsi_dump_dsidev_irqs(dsidev, s);
}

static void dsi2_dump_irqs(struct seq_file *s)
{
	struct platform_device *dsidev = dsi_get_dsidev_from_id(1);

	dsi_dump_dsidev_irqs(dsidev, s);
}
#endif

static void dsi_dump_dsidev_regs(struct platform_device *dsidev,
		struct seq_file *s)
{
#define DUMPREG(r) seq_printf(s, "%-35s %08x\n", #r, dsi_read_reg(dsidev, r))

	if (dsi_runtime_get(dsidev))
		return;
	dsi_enable_scp_clk(dsidev);

	DUMPREG(DSI_REVISION);
	DUMPREG(DSI_SYSCONFIG);
	DUMPREG(DSI_SYSSTATUS);
	DUMPREG(DSI_IRQSTATUS);
	DUMPREG(DSI_IRQENABLE);
	DUMPREG(DSI_CTRL);
	DUMPREG(DSI_COMPLEXIO_CFG1);
	DUMPREG(DSI_COMPLEXIO_IRQ_STATUS);
	DUMPREG(DSI_COMPLEXIO_IRQ_ENABLE);
	DUMPREG(DSI_CLK_CTRL);
	DUMPREG(DSI_TIMING1);
	DUMPREG(DSI_TIMING2);
	DUMPREG(DSI_VM_TIMING1);
	DUMPREG(DSI_VM_TIMING2);
	DUMPREG(DSI_VM_TIMING3);
	DUMPREG(DSI_CLK_TIMING);
	DUMPREG(DSI_TX_FIFO_VC_SIZE);
	DUMPREG(DSI_RX_FIFO_VC_SIZE);
	DUMPREG(DSI_COMPLEXIO_CFG2);
	DUMPREG(DSI_RX_FIFO_VC_FULLNESS);
	DUMPREG(DSI_VM_TIMING4);
	DUMPREG(DSI_TX_FIFO_VC_EMPTINESS);
	DUMPREG(DSI_VM_TIMING5);
	DUMPREG(DSI_VM_TIMING6);
	DUMPREG(DSI_VM_TIMING7);
	DUMPREG(DSI_STOPCLK_TIMING);

	DUMPREG(DSI_VC_CTRL(0));
	DUMPREG(DSI_VC_TE(0));
	DUMPREG(DSI_VC_LONG_PACKET_HEADER(0));
	DUMPREG(DSI_VC_LONG_PACKET_PAYLOAD(0));
	DUMPREG(DSI_VC_SHORT_PACKET_HEADER(0));
	DUMPREG(DSI_VC_IRQSTATUS(0));
	DUMPREG(DSI_VC_IRQENABLE(0));

	DUMPREG(DSI_VC_CTRL(1));
	DUMPREG(DSI_VC_TE(1));
	DUMPREG(DSI_VC_LONG_PACKET_HEADER(1));
	DUMPREG(DSI_VC_LONG_PACKET_PAYLOAD(1));
	DUMPREG(DSI_VC_SHORT_PACKET_HEADER(1));
	DUMPREG(DSI_VC_IRQSTATUS(1));
	DUMPREG(DSI_VC_IRQENABLE(1));

	DUMPREG(DSI_VC_CTRL(2));
	DUMPREG(DSI_VC_TE(2));
	DUMPREG(DSI_VC_LONG_PACKET_HEADER(2));
	DUMPREG(DSI_VC_LONG_PACKET_PAYLOAD(2));
	DUMPREG(DSI_VC_SHORT_PACKET_HEADER(2));
	DUMPREG(DSI_VC_IRQSTATUS(2));
	DUMPREG(DSI_VC_IRQENABLE(2));

	DUMPREG(DSI_VC_CTRL(3));
	DUMPREG(DSI_VC_TE(3));
	DUMPREG(DSI_VC_LONG_PACKET_HEADER(3));
	DUMPREG(DSI_VC_LONG_PACKET_PAYLOAD(3));
	DUMPREG(DSI_VC_SHORT_PACKET_HEADER(3));
	DUMPREG(DSI_VC_IRQSTATUS(3));
	DUMPREG(DSI_VC_IRQENABLE(3));

	DUMPREG(DSI_DSIPHY_CFG0);
	DUMPREG(DSI_DSIPHY_CFG1);
	DUMPREG(DSI_DSIPHY_CFG2);
	DUMPREG(DSI_DSIPHY_CFG5);

	DUMPREG(DSI_PLL_CONTROL);
	DUMPREG(DSI_PLL_STATUS);
	DUMPREG(DSI_PLL_GO);
	DUMPREG(DSI_PLL_CONFIGURATION1);
	DUMPREG(DSI_PLL_CONFIGURATION2);

	dsi_disable_scp_clk(dsidev);
	dsi_runtime_put(dsidev);
#undef DUMPREG
}

static void dsi1_dump_regs(struct seq_file *s)
{
	struct platform_device *dsidev = dsi_get_dsidev_from_id(0);

	dsi_dump_dsidev_regs(dsidev, s);
}

static void dsi2_dump_regs(struct seq_file *s)
{
	struct platform_device *dsidev = dsi_get_dsidev_from_id(1);

	dsi_dump_dsidev_regs(dsidev, s);
}

enum dsi_cio_power_state {
	DSI_COMPLEXIO_POWER_OFF		= 0x0,
	DSI_COMPLEXIO_POWER_ON		= 0x1,
	DSI_COMPLEXIO_POWER_ULPS	= 0x2,
};

static int dsi_cio_power(struct platform_device *dsidev,
		enum dsi_cio_power_state state)
{
	int t = 0;

	/* PWR_CMD */
	REG_FLD_MOD(dsidev, DSI_COMPLEXIO_CFG1, state, 28, 27);

	/* PWR_STATUS */
	while (FLD_GET(dsi_read_reg(dsidev, DSI_COMPLEXIO_CFG1),
			26, 25) != state) {
		if (++t > 1000) {
			DSSERR("failed to set complexio power state to "
					"%d\n", state);
			return -ENODEV;
		}
		udelay(1);
	}

	return 0;
}

static unsigned dsi_get_line_buf_size(struct platform_device *dsidev)
{
	int val;

	/* line buffer on OMAP3 is 1024 x 24bits */
	/* XXX: for some reason using full buffer size causes
	 * considerable TX slowdown with update sizes that fill the
	 * whole buffer */
	if (!dss_has_feature(FEAT_DSI_GNQ))
		return 1023 * 3;

	val = REG_GET(dsidev, DSI_GNQ, 14, 12); /* VP1_LINE_BUFFER_SIZE */

	switch (val) {
	case 1:
		return 512 * 3;		/* 512x24 bits */
	case 2:
		return 682 * 3;		/* 682x24 bits */
	case 3:
		return 853 * 3;		/* 853x24 bits */
	case 4:
		return 1024 * 3;	/* 1024x24 bits */
	case 5:
		return 1194 * 3;	/* 1194x24 bits */
	case 6:
		return 1365 * 3;	/* 1365x24 bits */
	case 7:
		return 1920 * 3;	/* 1920x24 bits */
	default:
		BUG();
		return 0;
	}
}

static int dsi_set_lane_config(struct platform_device *dsidev)
{
	struct dsi_data *dsi = dsi_get_dsidrv_data(dsidev);
	static const u8 offsets[] = { 0, 4, 8, 12, 16 };
	static const enum dsi_lane_function functions[] = {
		DSI_LANE_CLK,
		DSI_LANE_DATA1,
		DSI_LANE_DATA2,
		DSI_LANE_DATA3,
		DSI_LANE_DATA4,
	};
	u32 r;
	int i;

	r = dsi_read_reg(dsidev, DSI_COMPLEXIO_CFG1);

	for (i = 0; i < dsi->num_lanes_used; ++i) {
		unsigned offset = offsets[i];
		unsigned polarity, lane_number;
		unsigned t;

		for (t = 0; t < dsi->num_lanes_supported; ++t)
			if (dsi->lanes[t].function == functions[i])
				break;

		if (t == dsi->num_lanes_supported)
			return -EINVAL;

		lane_number = t;
		polarity = dsi->lanes[t].polarity;

		r = FLD_MOD(r, lane_number + 1, offset + 2, offset);
		r = FLD_MOD(r, polarity, offset + 3, offset + 3);
	}

	/* clear the unused lanes */
	for (; i < dsi->num_lanes_supported; ++i) {
		unsigned offset = offsets[i];

		r = FLD_MOD(r, 0, offset + 2, offset);
		r = FLD_MOD(r, 0, offset + 3, offset + 3);
	}

	dsi_write_reg(dsidev, DSI_COMPLEXIO_CFG1, r);

	return 0;
}

static inline unsigned ns2ddr(struct platform_device *dsidev, unsigned ns)
{
	struct dsi_data *dsi = dsi_get_dsidrv_data(dsidev);

	/* convert time in ns to ddr ticks, rounding up */
	unsigned long ddr_clk = dsi->pll.cinfo.clkdco / 4;
	return (ns * (ddr_clk / 1000 / 1000) + 999) / 1000;
}

static inline unsigned ddr2ns(struct platform_device *dsidev, unsigned ddr)
{
	struct dsi_data *dsi = dsi_get_dsidrv_data(dsidev);

	unsigned long ddr_clk = dsi->pll.cinfo.clkdco / 4;
	return ddr * 1000 * 1000 / (ddr_clk / 1000);
}

static void dsi_cio_timings(struct platform_device *dsidev)
{
	u32 r;
	u32 ths_prepare, ths_prepare_ths_zero, ths_trail, ths_exit;
	u32 tlpx_half, tclk_trail, tclk_zero;
	u32 tclk_prepare;

	/* calculate timings */

	/* 1 * DDR_CLK = 2 * UI */

	/* min 40ns + 4*UI	max 85ns + 6*UI */
	ths_prepare = ns2ddr(dsidev, 70) + 2;

	/* min 145ns + 10*UI */
	ths_prepare_ths_zero = ns2ddr(dsidev, 175) + 2;

	/* min max(8*UI, 60ns+4*UI) */
	ths_trail = ns2ddr(dsidev, 60) + 5;

	/* min 100ns */
	ths_exit = ns2ddr(dsidev, 145);

	/* tlpx min 50n */
	tlpx_half = ns2ddr(dsidev, 25);

	/* min 60ns */
	tclk_trail = ns2ddr(dsidev, 60) + 2;

	/* min 38ns, max 95ns */
	tclk_prepare = ns2ddr(dsidev, 65);

	/* min tclk-prepare + tclk-zero = 300ns */
	tclk_zero = ns2ddr(dsidev, 260);

	DSSDBG("ths_prepare %u (%uns), ths_prepare_ths_zero %u (%uns)\n",
		ths_prepare, ddr2ns(dsidev, ths_prepare),
		ths_prepare_ths_zero, ddr2ns(dsidev, ths_prepare_ths_zero));
	DSSDBG("ths_trail %u (%uns), ths_exit %u (%uns)\n",
			ths_trail, ddr2ns(dsidev, ths_trail),
			ths_exit, ddr2ns(dsidev, ths_exit));

	DSSDBG("tlpx_half %u (%uns), tclk_trail %u (%uns), "
			"tclk_zero %u (%uns)\n",
			tlpx_half, ddr2ns(dsidev, tlpx_half),
			tclk_trail, ddr2ns(dsidev, tclk_trail),
			tclk_zero, ddr2ns(dsidev, tclk_zero));
	DSSDBG("tclk_prepare %u (%uns)\n",
			tclk_prepare, ddr2ns(dsidev, tclk_prepare));

	/* program timings */

	r = dsi_read_reg(dsidev, DSI_DSIPHY_CFG0);
	r = FLD_MOD(r, ths_prepare, 31, 24);
	r = FLD_MOD(r, ths_prepare_ths_zero, 23, 16);
	r = FLD_MOD(r, ths_trail, 15, 8);
	r = FLD_MOD(r, ths_exit, 7, 0);
	dsi_write_reg(dsidev, DSI_DSIPHY_CFG0, r);

	r = dsi_read_reg(dsidev, DSI_DSIPHY_CFG1);
	r = FLD_MOD(r, tlpx_half, 20, 16);
	r = FLD_MOD(r, tclk_trail, 15, 8);
	r = FLD_MOD(r, tclk_zero, 7, 0);

	if (dss_has_feature(FEAT_DSI_PHY_DCC)) {
		r = FLD_MOD(r, 0, 21, 21);	/* DCCEN = disable */
		r = FLD_MOD(r, 1, 22, 22);	/* CLKINP_DIVBY2EN = enable */
		r = FLD_MOD(r, 1, 23, 23);	/* CLKINP_SEL = enable */
	}

	dsi_write_reg(dsidev, DSI_DSIPHY_CFG1, r);

	r = dsi_read_reg(dsidev, DSI_DSIPHY_CFG2);
	r = FLD_MOD(r, tclk_prepare, 7, 0);
	dsi_write_reg(dsidev, DSI_DSIPHY_CFG2, r);
}

/* lane masks have lane 0 at lsb. mask_p for positive lines, n for negative */
static void dsi_cio_enable_lane_override(struct platform_device *dsidev,
		unsigned mask_p, unsigned mask_n)
{
	struct dsi_data *dsi = dsi_get_dsidrv_data(dsidev);
	int i;
	u32 l;
	u8 lptxscp_start = dsi->num_lanes_supported == 3 ? 22 : 26;

	l = 0;

	for (i = 0; i < dsi->num_lanes_supported; ++i) {
		unsigned p = dsi->lanes[i].polarity;

		if (mask_p & (1 << i))
			l |= 1 << (i * 2 + (p ? 0 : 1));

		if (mask_n & (1 << i))
			l |= 1 << (i * 2 + (p ? 1 : 0));
	}

	/*
	 * Bits in REGLPTXSCPDAT4TO0DXDY:
	 * 17: DY0 18: DX0
	 * 19: DY1 20: DX1
	 * 21: DY2 22: DX2
	 * 23: DY3 24: DX3
	 * 25: DY4 26: DX4
	 */

	/* Set the lane override configuration */

	/* REGLPTXSCPDAT4TO0DXDY */
	REG_FLD_MOD(dsidev, DSI_DSIPHY_CFG10, l, lptxscp_start, 17);

	/* Enable lane override */

	/* ENLPTXSCPDAT */
	REG_FLD_MOD(dsidev, DSI_DSIPHY_CFG10, 1, 27, 27);
}

static void dsi_cio_disable_lane_override(struct platform_device *dsidev)
{
	/* Disable lane override */
	REG_FLD_MOD(dsidev, DSI_DSIPHY_CFG10, 0, 27, 27); /* ENLPTXSCPDAT */
	/* Reset the lane override configuration */
	/* REGLPTXSCPDAT4TO0DXDY */
	REG_FLD_MOD(dsidev, DSI_DSIPHY_CFG10, 0, 22, 17);
}

static int dsi_cio_wait_tx_clk_esc_reset(struct platform_device *dsidev)
{
	struct dsi_data *dsi = dsi_get_dsidrv_data(dsidev);
	int t, i;
	bool in_use[DSI_MAX_NR_LANES];
	static const u8 offsets_old[] = { 28, 27, 26 };
	static const u8 offsets_new[] = { 24, 25, 26, 27, 28 };
	const u8 *offsets;

	if (dss_has_feature(FEAT_DSI_REVERSE_TXCLKESC))
		offsets = offsets_old;
	else
		offsets = offsets_new;

	for (i = 0; i < dsi->num_lanes_supported; ++i)
		in_use[i] = dsi->lanes[i].function != DSI_LANE_UNUSED;

	t = 100000;
	while (true) {
		u32 l;
		int ok;

		l = dsi_read_reg(dsidev, DSI_DSIPHY_CFG5);

		ok = 0;
		for (i = 0; i < dsi->num_lanes_supported; ++i) {
			if (!in_use[i] || (l & (1 << offsets[i])))
				ok++;
		}

		if (ok == dsi->num_lanes_supported)
			break;

		if (--t == 0) {
			for (i = 0; i < dsi->num_lanes_supported; ++i) {
				if (!in_use[i] || (l & (1 << offsets[i])))
					continue;

				DSSERR("CIO TXCLKESC%d domain not coming " \
						"out of reset\n", i);
			}
			return -EIO;
		}
	}

	return 0;
}

/* return bitmask of enabled lanes, lane0 being the lsb */
static unsigned dsi_get_lane_mask(struct platform_device *dsidev)
{
	struct dsi_data *dsi = dsi_get_dsidrv_data(dsidev);
	unsigned mask = 0;
	int i;

	for (i = 0; i < dsi->num_lanes_supported; ++i) {
		if (dsi->lanes[i].function != DSI_LANE_UNUSED)
			mask |= 1 << i;
	}

	return mask;
}

static int dsi_cio_init(struct platform_device *dsidev)
{
	struct dsi_data *dsi = dsi_get_dsidrv_data(dsidev);
	int r;
	u32 l;

	DSSDBG("DSI CIO init starts");

	r = dss_dsi_enable_pads(dsi->module_id, dsi_get_lane_mask(dsidev));
	if (r)
		return r;

	dsi_enable_scp_clk(dsidev);

	/* A dummy read using the SCP interface to any DSIPHY register is
	 * required after DSIPHY reset to complete the reset of the DSI complex
	 * I/O. */
	dsi_read_reg(dsidev, DSI_DSIPHY_CFG5);

	if (wait_for_bit_change(dsidev, DSI_DSIPHY_CFG5, 30, 1) != 1) {
		DSSERR("CIO SCP Clock domain not coming out of reset.\n");
		r = -EIO;
		goto err_scp_clk_dom;
	}

	r = dsi_set_lane_config(dsidev);
	if (r)
		goto err_scp_clk_dom;

	/* set TX STOP MODE timer to maximum for this operation */
	l = dsi_read_reg(dsidev, DSI_TIMING1);
	l = FLD_MOD(l, 1, 15, 15);	/* FORCE_TX_STOP_MODE_IO */
	l = FLD_MOD(l, 1, 14, 14);	/* STOP_STATE_X16_IO */
	l = FLD_MOD(l, 1, 13, 13);	/* STOP_STATE_X4_IO */
	l = FLD_MOD(l, 0x1fff, 12, 0);	/* STOP_STATE_COUNTER_IO */
	dsi_write_reg(dsidev, DSI_TIMING1, l);

	if (dsi->ulps_enabled) {
		unsigned mask_p;
		int i;

		DSSDBG("manual ulps exit\n");

		/* ULPS is exited by Mark-1 state for 1ms, followed by
		 * stop state. DSS HW cannot do this via the normal
		 * ULPS exit sequence, as after reset the DSS HW thinks
		 * that we are not in ULPS mode, and refuses to send the
		 * sequence. So we need to send the ULPS exit sequence
		 * manually by setting positive lines high and negative lines
		 * low for 1ms.
		 */

		mask_p = 0;

		for (i = 0; i < dsi->num_lanes_supported; ++i) {
			if (dsi->lanes[i].function == DSI_LANE_UNUSED)
				continue;
			mask_p |= 1 << i;
		}

		dsi_cio_enable_lane_override(dsidev, mask_p, 0);
	}

	r = dsi_cio_power(dsidev, DSI_COMPLEXIO_POWER_ON);
	if (r)
		goto err_cio_pwr;

	if (wait_for_bit_change(dsidev, DSI_COMPLEXIO_CFG1, 29, 1) != 1) {
		DSSERR("CIO PWR clock domain not coming out of reset.\n");
		r = -ENODEV;
		goto err_cio_pwr_dom;
	}

	dsi_if_enable(dsidev, true);
	dsi_if_enable(dsidev, false);
	REG_FLD_MOD(dsidev, DSI_CLK_CTRL, 1, 20, 20); /* LP_CLK_ENABLE */

	r = dsi_cio_wait_tx_clk_esc_reset(dsidev);
	if (r)
		goto err_tx_clk_esc_rst;

	if (dsi->ulps_enabled) {
		/* Keep Mark-1 state for 1ms (as per DSI spec) */
		ktime_t wait = ns_to_ktime(1000 * 1000);
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_hrtimeout(&wait, HRTIMER_MODE_REL);

		/* Disable the override. The lanes should be set to Mark-11
		 * state by the HW */
		dsi_cio_disable_lane_override(dsidev);
	}

	/* FORCE_TX_STOP_MODE_IO */
	REG_FLD_MOD(dsidev, DSI_TIMING1, 0, 15, 15);

	dsi_cio_timings(dsidev);

	if (dsi->mode == OMAP_DSS_DSI_VIDEO_MODE) {
		/* DDR_CLK_ALWAYS_ON */
		REG_FLD_MOD(dsidev, DSI_CLK_CTRL,
			dsi->vm_timings.ddr_clk_always_on, 13, 13);
	}

	dsi->ulps_enabled = false;

	DSSDBG("CIO init done\n");

	return 0;

err_tx_clk_esc_rst:
	REG_FLD_MOD(dsidev, DSI_CLK_CTRL, 0, 20, 20); /* LP_CLK_ENABLE */
err_cio_pwr_dom:
	dsi_cio_power(dsidev, DSI_COMPLEXIO_POWER_OFF);
err_cio_pwr:
	if (dsi->ulps_enabled)
		dsi_cio_disable_lane_override(dsidev);
err_scp_clk_dom:
	dsi_disable_scp_clk(dsidev);
	dss_dsi_disable_pads(dsi->module_id, dsi_get_lane_mask(dsidev));
	return r;
}

static void dsi_cio_uninit(struct platform_device *dsidev)
{
	struct dsi_data *dsi = dsi_get_dsidrv_data(dsidev);

	/* DDR_CLK_ALWAYS_ON */
	REG_FLD_MOD(dsidev, DSI_CLK_CTRL, 0, 13, 13);

	dsi_cio_power(dsidev, DSI_COMPLEXIO_POWER_OFF);
	dsi_disable_scp_clk(dsidev);
	dss_dsi_disable_pads(dsi->module_id, dsi_get_lane_mask(dsidev));
}

static void dsi_config_tx_fifo(struct platform_device *dsidev,
		enum fifo_size size1, enum fifo_size size2,
		enum fifo_size size3, enum fifo_size size4)
{
	struct dsi_data *dsi = dsi_get_dsidrv_data(dsidev);
	u32 r = 0;
	int add = 0;
	int i;

	dsi->vc[0].tx_fifo_size = size1;
	dsi->vc[1].tx_fifo_size = size2;
	dsi->vc[2].tx_fifo_size = size3;
	dsi->vc[3].tx_fifo_size = size4;

	for (i = 0; i < 4; i++) {
		u8 v;
		int size = dsi->vc[i].tx_fifo_size;

		if (add + size > 4) {
			DSSERR("Illegal FIFO configuration\n");
			BUG();
			return;
		}

		v = FLD_VAL(add, 2, 0) | FLD_VAL(size, 7, 4);
		r |= v << (8 * i);
		/*DSSDBG("TX FIFO vc %d: size %d, add %d\n", i, size, add); */
		add += size;
	}

	dsi_write_reg(dsidev, DSI_TX_FIFO_VC_SIZE, r);
}

static void dsi_config_rx_fifo(struct platform_device *dsidev,
		enum fifo_size size1, enum fifo_size size2,
		enum fifo_size size3, enum fifo_size size4)
{
	struct dsi_data *dsi = dsi_get_dsidrv_data(dsidev);
	u32 r = 0;
	int add = 0;
	int i;

	dsi->vc[0].rx_fifo_size = size1;
	dsi->vc[1].rx_fifo_size = size2;
	dsi->vc[2].rx_fifo_size = size3;
	dsi->vc[3].rx_fifo_size = size4;

	for (i = 0; i < 4; i++) {
		u8 v;
		int size = dsi->vc[i].rx_fifo_size;

		if (add + size > 4) {
			DSSERR("Illegal FIFO configuration\n");
			BUG();
			return;
		}

		v = FLD_VAL(add, 2, 0) | FLD_VAL(size, 7, 4);
		r |= v << (8 * i);
		/*DSSDBG("RX FIFO vc %d: size %d, add %d\n", i, size, add); */
		add += size;
	}

	dsi_write_reg(dsidev, DSI_RX_FIFO_VC_SIZE, r);
}

static int dsi_force_tx_stop_mode_io(struct platform_device *dsidev)
{
	u32 r;

	r = dsi_read_reg(dsidev, DSI_TIMING1);
	r = FLD_MOD(r, 1, 15, 15);	/* FORCE_TX_STOP_MODE_IO */
	dsi_write_reg(dsidev, DSI_TIMING1, r);

	if (wait_for_bit_change(dsidev, DSI_TIMING1, 15, 0) != 0) {
		DSSERR("TX_STOP bit not going down\n");
		return -EIO;
	}

	return 0;
}

static bool dsi_vc_is_enabled(struct platform_device *dsidev, int channel)
{
	return REG_GET(dsidev, DSI_VC_CTRL(channel), 0, 0);
}

static void dsi_packet_sent_handler_vp(void *data, u32 mask)
{
	struct dsi_packet_sent_handler_data *vp_data =
		(struct dsi_packet_sent_handler_data *) data;
	struct dsi_data *dsi = dsi_get_dsidrv_data(vp_data->dsidev);
	const int channel = dsi->update_channel;
	u8 bit = dsi->te_enabled ? 30 : 31;

	if (REG_GET(vp_data->dsidev, DSI_VC_TE(channel), bit, bit) == 0)
		complete(vp_data->completion);
}

static int dsi_sync_vc_vp(struct platform_device *dsidev, int channel)
{
	struct dsi_data *dsi = dsi_get_dsidrv_data(dsidev);
	DECLARE_COMPLETION_ONSTACK(completion);
	struct dsi_packet_sent_handler_data vp_data = {
		.dsidev = dsidev,
		.completion = &completion
	};
	int r = 0;
	u8 bit;

	bit = dsi->te_enabled ? 30 : 31;

	r = dsi_register_isr_vc(dsidev, channel, dsi_packet_sent_handler_vp,
		&vp_data, DSI_VC_IRQ_PACKET_SENT);
	if (r)
		goto err0;

	/* Wait for completion only if TE_EN/TE_START is still set */
	if (REG_GET(dsidev, DSI_VC_TE(channel), bit, bit)) {
		if (wait_for_completion_timeout(&completion,
				msecs_to_jiffies(10)) == 0) {
			DSSERR("Failed to complete previous frame transfer\n");
			r = -EIO;
			goto err1;
		}
	}

	dsi_unregister_isr_vc(dsidev, channel, dsi_packet_sent_handler_vp,
		&vp_data, DSI_VC_IRQ_PACKET_SENT);

	return 0;
err1:
	dsi_unregister_isr_vc(dsidev, channel, dsi_packet_sent_handler_vp,
		&vp_data, DSI_VC_IRQ_PACKET_SENT);
err0:
	return r;
}

static void dsi_packet_sent_handler_l4(void *data, u32 mask)
{
	struct dsi_packet_sent_handler_data *l4_data =
		(struct dsi_packet_sent_handler_data *) data;
	struct dsi_data *dsi = dsi_get_dsidrv_data(l4_data->dsidev);
	const int channel = dsi->update_channel;

	if (REG_GET(l4_data->dsidev, DSI_VC_CTRL(channel), 5, 5) == 0)
		complete(l4_data->completion);
}

static int dsi_sync_vc_l4(struct platform_device *dsidev, int channel)
{
	DECLARE_COMPLETION_ONSTACK(completion);
	struct dsi_packet_sent_handler_data l4_data = {
		.dsidev = dsidev,
		.completion = &completion
	};
	int r = 0;

	r = dsi_register_isr_vc(dsidev, channel, dsi_packet_sent_handler_l4,
		&l4_data, DSI_VC_IRQ_PACKET_SENT);
	if (r)
		goto err0;

	/* Wait for completion only if TX_FIFO_NOT_EMPTY is still set */
	if (REG_GET(dsidev, DSI_VC_CTRL(channel), 5, 5)) {
		if (wait_for_completion_timeout(&completion,
				msecs_to_jiffies(10)) == 0) {
			DSSERR("Failed to complete previous l4 transfer\n");
			r = -EIO;
			goto err1;
		}
	}

	dsi_unregister_isr_vc(dsidev, channel, dsi_packet_sent_handler_l4,
		&l4_data, DSI_VC_IRQ_PACKET_SENT);

	return 0;
err1:
	dsi_unregister_isr_vc(dsidev, channel, dsi_packet_sent_handler_l4,
		&l4_data, DSI_VC_IRQ_PACKET_SENT);
err0:
	return r;
}

static int dsi_sync_vc(struct platform_device *dsidev, int channel)
{
	struct dsi_data *dsi = dsi_get_dsidrv_data(dsidev);

	WARN_ON(!dsi_bus_is_locked(dsidev));

	WARN_ON(in_interrupt());

	if (!dsi_vc_is_enabled(dsidev, channel))
		return 0;

	switch (dsi->vc[channel].source) {
	case DSI_VC_SOURCE_VP:
		return dsi_sync_vc_vp(dsidev, channel);
	case DSI_VC_SOURCE_L4:
		return dsi_sync_vc_l4(dsidev, channel);
	default:
		BUG();
		return -EINVAL;
	}
}

static int dsi_vc_enable(struct platform_device *dsidev, int channel,
		bool enable)
{
	DSSDBG("dsi_vc_enable channel %d, enable %d\n",
			channel, enable);

	enable = enable ? 1 : 0;

	REG_FLD_MOD(dsidev, DSI_VC_CTRL(channel), enable, 0, 0);

	if (wait_for_bit_change(dsidev, DSI_VC_CTRL(channel),
		0, enable) != enable) {
			DSSERR("Failed to set dsi_vc_enable to %d\n", enable);
			return -EIO;
	}

	return 0;
}

static void dsi_vc_initial_config(struct platform_device *dsidev, int channel)
{
	struct dsi_data *dsi = dsi_get_dsidrv_data(dsidev);
	u32 r;

	DSSDBG("Initial config of virtual channel %d", channel);

	r = dsi_read_reg(dsidev, DSI_VC_CTRL(channel));

	if (FLD_GET(r, 15, 15)) /* VC_BUSY */
		DSSERR("VC(%d) busy when trying to configure it!\n",
				channel);

	r = FLD_MOD(r, 0, 1, 1); /* SOURCE, 0 = L4 */
	r = FLD_MOD(r, 0, 2, 2); /* BTA_SHORT_EN  */
	r = FLD_MOD(r, 0, 3, 3); /* BTA_LONG_EN */
	r = FLD_MOD(r, 0, 4, 4); /* MODE, 0 = command */
	r = FLD_MOD(r, 1, 7, 7); /* CS_TX_EN */
	r = FLD_MOD(r, 1, 8, 8); /* ECC_TX_EN */
	r = FLD_MOD(r, 0, 9, 9); /* MODE_SPEED, high speed on/off */
	if (dss_has_feature(FEAT_DSI_VC_OCP_WIDTH))
		r = FLD_MOD(r, 3, 11, 10);	/* OCP_WIDTH = 32 bit */

	r = FLD_MOD(r, 4, 29, 27); /* DMA_RX_REQ_NB = no dma */
	r = FLD_MOD(r, 4, 23, 21); /* DMA_TX_REQ_NB = no dma */

	dsi_write_reg(dsidev, DSI_VC_CTRL(channel), r);

	dsi->vc[channel].source = DSI_VC_SOURCE_L4;
}

static int dsi_vc_config_source(struct platform_device *dsidev, int channel,
		enum dsi_vc_source source)
{
	struct dsi_data *dsi = dsi_get_dsidrv_data(dsidev);

	if (dsi->vc[channel].source == source)
		return 0;

	DSSDBG("Source config of virtual channel %d", channel);

	dsi_sync_vc(dsidev, channel);

	dsi_vc_enable(dsidev, channel, 0);

	/* VC_BUSY */
	if (wait_for_bit_change(dsidev, DSI_VC_CTRL(channel), 15, 0) != 0) {
		DSSERR("vc(%d) busy when trying to config for VP\n", channel);
		return -EIO;
	}

	/* SOURCE, 0 = L4, 1 = video port */
	REG_FLD_MOD(dsidev, DSI_VC_CTRL(channel), source, 1, 1);

	/* DCS_CMD_ENABLE */
	if (dss_has_feature(FEAT_DSI_DCS_CMD_CONFIG_VC)) {
		bool enable = source == DSI_VC_SOURCE_VP;
		REG_FLD_MOD(dsidev, DSI_VC_CTRL(channel), enable, 30, 30);
	}

	dsi_vc_enable(dsidev, channel, 1);

	dsi->vc[channel].source = source;

	return 0;
}

static void dsi_vc_enable_hs(struct omap_dss_device *dssdev, int channel,
		bool enable)
{
	struct platform_device *dsidev = dsi_get_dsidev_from_dssdev(dssdev);
	struct dsi_data *dsi = dsi_get_dsidrv_data(dsidev);

	DSSDBG("dsi_vc_enable_hs(%d, %d)\n", channel, enable);

	WARN_ON(!dsi_bus_is_locked(dsidev));

	dsi_vc_enable(dsidev, channel, 0);
	dsi_if_enable(dsidev, 0);

	REG_FLD_MOD(dsidev, DSI_VC_CTRL(channel), enable, 9, 9);

	dsi_vc_enable(dsidev, channel, 1);
	dsi_if_enable(dsidev, 1);

	dsi_force_tx_stop_mode_io(dsidev);

	/* start the DDR clock by sending a NULL packet */
	if (dsi->vm_timings.ddr_clk_always_on && enable)
		dsi_vc_send_null(dssdev, channel);
}

static void dsi_vc_flush_long_data(struct platform_device *dsidev, int channel)
{
	while (REG_GET(dsidev, DSI_VC_CTRL(channel), 20, 20)) {
		u32 val;
		val = dsi_read_reg(dsidev, DSI_VC_SHORT_PACKET_HEADER(channel));
		DSSDBG("\t\tb1 %#02x b2 %#02x b3 %#02x b4 %#02x\n",
				(val >> 0) & 0xff,
				(val >> 8) & 0xff,
				(val >> 16) & 0xff,
				(val >> 24) & 0xff);
	}
}

static void dsi_show_rx_ack_with_err(u16 err)
{
	DSSERR("\tACK with ERROR (%#x):\n", err);
	if (err & (1 << 0))
		DSSERR("\t\tSoT Error\n");
	if (err & (1 << 1))
		DSSERR("\t\tSoT Sync Error\n");
	if (err & (1 << 2))
		DSSERR("\t\tEoT Sync Error\n");
	if (err & (1 << 3))
		DSSERR("\t\tEscape Mode Entry Command Error\n");
	if (err & (1 << 4))
		DSSERR("\t\tLP Transmit Sync Error\n");
	if (err & (1 << 5))
		DSSERR("\t\tHS Receive Timeout Error\n");
	if (err & (1 << 6))
		DSSERR("\t\tFalse Control Error\n");
	if (err & (1 << 7))
		DSSERR("\t\t(reserved7)\n");
	if (err & (1 << 8))
		DSSERR("\t\tECC Error, single-bit (corrected)\n");
	if (err & (1 << 9))
		DSSERR("\t\tECC Error, multi-bit (not corrected)\n");
	if (err & (1 << 10))
		DSSERR("\t\tChecksum Error\n");
	if (err & (1 << 11))
		DSSERR("\t\tData type not recognized\n");
	if (err & (1 << 12))
		DSSERR("\t\tInvalid VC ID\n");
	if (err & (1 << 13))
		DSSERR("\t\tInvalid Transmission Length\n");
	if (err & (1 << 14))
		DSSERR("\t\t(reserved14)\n");
	if (err & (1 << 15))
		DSSERR("\t\tDSI Protocol Violation\n");
}

static u16 dsi_vc_flush_receive_data(struct platform_device *dsidev,
		int channel)
{
	/* RX_FIFO_NOT_EMPTY */
	while (REG_GET(dsidev, DSI_VC_CTRL(channel), 20, 20)) {
		u32 val;
		u8 dt;
		val = dsi_read_reg(dsidev, DSI_VC_SHORT_PACKET_HEADER(channel));
		DSSERR("\trawval %#08x\n", val);
		dt = FLD_GET(val, 5, 0);
		if (dt == MIPI_DSI_RX_ACKNOWLEDGE_AND_ERROR_REPORT) {
			u16 err = FLD_GET(val, 23, 8);
			dsi_show_rx_ack_with_err(err);
		} else if (dt == MIPI_DSI_RX_DCS_SHORT_READ_RESPONSE_1BYTE) {
			DSSERR("\tDCS short response, 1 byte: %#x\n",
					FLD_GET(val, 23, 8));
		} else if (dt == MIPI_DSI_RX_DCS_SHORT_READ_RESPONSE_2BYTE) {
			DSSERR("\tDCS short response, 2 byte: %#x\n",
					FLD_GET(val, 23, 8));
		} else if (dt == MIPI_DSI_RX_DCS_LONG_READ_RESPONSE) {
			DSSERR("\tDCS long response, len %d\n",
					FLD_GET(val, 23, 8));
			dsi_vc_flush_long_data(dsidev, channel);
		} else {
			DSSERR("\tunknown datatype 0x%02x\n", dt);
		}
	}
	return 0;
}

static int dsi_vc_send_bta(struct platform_device *dsidev, int channel)
{
	struct dsi_data *dsi = dsi_get_dsidrv_data(dsidev);

	if (dsi->debug_write || dsi->debug_read)
		DSSDBG("dsi_vc_send_bta %d\n", channel);

	WARN_ON(!dsi_bus_is_locked(dsidev));

	/* RX_FIFO_NOT_EMPTY */
	if (REG_GET(dsidev, DSI_VC_CTRL(channel), 20, 20)) {
		DSSERR("rx fifo not empty when sending BTA, dumping data:\n");
		dsi_vc_flush_receive_data(dsidev, channel);
	}

	REG_FLD_MOD(dsidev, DSI_VC_CTRL(channel), 1, 6, 6); /* BTA_EN */

	/* flush posted write */
	dsi_read_reg(dsidev, DSI_VC_CTRL(channel));

	return 0;
}

static int dsi_vc_send_bta_sync(struct omap_dss_device *dssdev, int channel)
{
	struct platform_device *dsidev = dsi_get_dsidev_from_dssdev(dssdev);
	DECLARE_COMPLETION_ONSTACK(completion);
	int r = 0;
	u32 err;

	r = dsi_register_isr_vc(dsidev, channel, dsi_completion_handler,
			&completion, DSI_VC_IRQ_BTA);
	if (r)
		goto err0;

	r = dsi_register_isr(dsidev, dsi_completion_handler, &completion,
			DSI_IRQ_ERROR_MASK);
	if (r)
		goto err1;

	r = dsi_vc_send_bta(dsidev, channel);
	if (r)
		goto err2;

	if (wait_for_completion_timeout(&completion,
				msecs_to_jiffies(500)) == 0) {
		DSSERR("Failed to receive BTA\n");
		r = -EIO;
		goto err2;
	}

	err = dsi_get_errors(dsidev);
	if (err) {
		DSSERR("Error while sending BTA: %x\n", err);
		r = -EIO;
		goto err2;
	}
err2:
	dsi_unregister_isr(dsidev, dsi_completion_handler, &completion,
			DSI_IRQ_ERROR_MASK);
err1:
	dsi_unregister_isr_vc(dsidev, channel, dsi_completion_handler,
			&completion, DSI_VC_IRQ_BTA);
err0:
	return r;
}

static inline void dsi_vc_write_long_header(struct platform_device *dsidev,
		int channel, u8 data_type, u16 len, u8 ecc)
{
	struct dsi_data *dsi = dsi_get_dsidrv_data(dsidev);
	u32 val;
	u8 data_id;

	WARN_ON(!dsi_bus_is_locked(dsidev));

	data_id = data_type | dsi->vc[channel].vc_id << 6;

	val = FLD_VAL(data_id, 7, 0) | FLD_VAL(len, 23, 8) |
		FLD_VAL(ecc, 31, 24);

	dsi_write_reg(dsidev, DSI_VC_LONG_PACKET_HEADER(channel), val);
}

static inline void dsi_vc_write_long_payload(struct platform_device *dsidev,
		int channel, u8 b1, u8 b2, u8 b3, u8 b4)
{
	u32 val;

	val = b4 << 24 | b3 << 16 | b2 << 8  | b1 << 0;

/*	DSSDBG("\twriting %02x, %02x, %02x, %02x (%#010x)\n",
			b1, b2, b3, b4, val); */

	dsi_write_reg(dsidev, DSI_VC_LONG_PACKET_PAYLOAD(channel), val);
}

static int dsi_vc_send_long(struct platform_device *dsidev, int channel,
		u8 data_type, u8 *data, u16 len, u8 ecc)
{
	/*u32 val; */
	struct dsi_data *dsi = dsi_get_dsidrv_data(dsidev);
	int i;
	u8 *p;
	int r = 0;
	u8 b1, b2, b3, b4;

	if (dsi->debug_write)
		DSSDBG("dsi_vc_send_long, %d bytes\n", len);

	/* len + header */
	if (dsi->vc[channel].tx_fifo_size * 32 * 4 < len + 4) {
		DSSERR("unable to send long packet: packet too long.\n");
		return -EINVAL;
	}

	dsi_vc_config_source(dsidev, channel, DSI_VC_SOURCE_L4);

	dsi_vc_write_long_header(dsidev, channel, data_type, len, ecc);

	p = data;
	for (i = 0; i < len >> 2; i++) {
		if (dsi->debug_write)
			DSSDBG("\tsending full packet %d\n", i);

		b1 = *p++;
		b2 = *p++;
		b3 = *p++;
		b4 = *p++;

		dsi_vc_write_long_payload(dsidev, channel, b1, b2, b3, b4);
	}

	i = len % 4;
	if (i) {
		b1 = 0; b2 = 0; b3 = 0;

		if (dsi->debug_write)
			DSSDBG("\tsending remainder bytes %d\n", i);

		switch (i) {
		case 3:
			b1 = *p++;
			b2 = *p++;
			b3 = *p++;
			break;
		case 2:
			b1 = *p++;
			b2 = *p++;
			break;
		case 1:
			b1 = *p++;
			break;
		}

		dsi_vc_write_long_payload(dsidev, channel, b1, b2, b3, 0);
	}

	return r;
}

static int dsi_vc_send_short(struct platform_device *dsidev, int channel,
		u8 data_type, u16 data, u8 ecc)
{
	struct dsi_data *dsi = dsi_get_dsidrv_data(dsidev);
	u32 r;
	u8 data_id;

	WARN_ON(!dsi_bus_is_locked(dsidev));

	if (dsi->debug_write)
		DSSDBG("dsi_vc_send_short(ch%d, dt %#x, b1 %#x, b2 %#x)\n",
				channel,
				data_type, data & 0xff, (data >> 8) & 0xff);

	dsi_vc_config_source(dsidev, channel, DSI_VC_SOURCE_L4);

	if (FLD_GET(dsi_read_reg(dsidev, DSI_VC_CTRL(channel)), 16, 16)) {
		DSSERR("ERROR FIFO FULL, aborting transfer\n");
		return -EINVAL;
	}

	data_id = data_type | dsi->vc[channel].vc_id << 6;

	r = (data_id << 0) | (data << 8) | (ecc << 24);

	dsi_write_reg(dsidev, DSI_VC_SHORT_PACKET_HEADER(channel), r);

	return 0;
}

static int dsi_vc_send_null(struct omap_dss_device *dssdev, int channel)
{
	struct platform_device *dsidev = dsi_get_dsidev_from_dssdev(dssdev);

	return dsi_vc_send_long(dsidev, channel, MIPI_DSI_NULL_PACKET, NULL,
		0, 0);
}

static int dsi_vc_write_nosync_common(struct platform_device *dsidev,
		int channel, u8 *data, int len, enum dss_dsi_content_type type)
{
	int r;

	if (len == 0) {
		BUG_ON(type == DSS_DSI_CONTENT_DCS);
		r = dsi_vc_send_short(dsidev, channel,
				MIPI_DSI_GENERIC_SHORT_WRITE_0_PARAM, 0, 0);
	} else if (len == 1) {
		r = dsi_vc_send_short(dsidev, channel,
				type == DSS_DSI_CONTENT_GENERIC ?
				MIPI_DSI_GENERIC_SHORT_WRITE_1_PARAM :
				MIPI_DSI_DCS_SHORT_WRITE, data[0], 0);
	} else if (len == 2) {
		r = dsi_vc_send_short(dsidev, channel,
				type == DSS_DSI_CONTENT_GENERIC ?
				MIPI_DSI_GENERIC_SHORT_WRITE_2_PARAM :
				MIPI_DSI_DCS_SHORT_WRITE_PARAM,
				data[0] | (data[1] << 8), 0);
	} else {
		r = dsi_vc_send_long(dsidev, channel,
				type == DSS_DSI_CONTENT_GENERIC ?
				MIPI_DSI_GENERIC_LONG_WRITE :
				MIPI_DSI_DCS_LONG_WRITE, data, len, 0);
	}

	return r;
}

static int dsi_vc_dcs_write_nosync(struct omap_dss_device *dssdev, int channel,
		u8 *data, int len)
{
	struct platform_device *dsidev = dsi_get_dsidev_from_dssdev(dssdev);

	return dsi_vc_write_nosync_common(dsidev, channel, data, len,
			DSS_DSI_CONTENT_DCS);
}

static int dsi_vc_generic_write_nosync(struct omap_dss_device *dssdev, int channel,
		u8 *data, int len)
{
	struct platform_device *dsidev = dsi_get_dsidev_from_dssdev(dssdev);

	return dsi_vc_write_nosync_common(dsidev, channel, data, len,
			DSS_DSI_CONTENT_GENERIC);
}

static int dsi_vc_write_common(struct omap_dss_device *dssdev, int channel,
		u8 *data, int len, enum dss_dsi_content_type type)
{
	struct platform_device *dsidev = dsi_get_dsidev_from_dssdev(dssdev);
	int r;

	r = dsi_vc_write_nosync_common(dsidev, channel, data, len, type);
	if (r)
		goto err;

	r = dsi_vc_send_bta_sync(dssdev, channel);
	if (r)
		goto err;

	/* RX_FIFO_NOT_EMPTY */
	if (REG_GET(dsidev, DSI_VC_CTRL(channel), 20, 20)) {
		DSSERR("rx fifo not empty after write, dumping data:\n");
		dsi_vc_flush_receive_data(dsidev, channel);
		r = -EIO;
		goto err;
	}

	return 0;
err:
	DSSERR("dsi_vc_write_common(ch %d, cmd 0x%02x, len %d) failed\n",
			channel, data[0], len);
	return r;
}

static int dsi_vc_dcs_write(struct omap_dss_device *dssdev, int channel, u8 *data,
		int len)
{
	return dsi_vc_write_common(dssdev, channel, data, len,
			DSS_DSI_CONTENT_DCS);
}

static int dsi_vc_generic_write(struct omap_dss_device *dssdev, int channel, u8 *data,
		int len)
{
	return dsi_vc_write_common(dssdev, channel, data, len,
			DSS_DSI_CONTENT_GENERIC);
}

static int dsi_vc_dcs_send_read_request(struct platform_device *dsidev,
		int channel, u8 dcs_cmd)
{
	struct dsi_data *dsi = dsi_get_dsidrv_data(dsidev);
	int r;

	if (dsi->debug_read)
		DSSDBG("dsi_vc_dcs_send_read_request(ch%d, dcs_cmd %x)\n",
			channel, dcs_cmd);

	r = dsi_vc_send_short(dsidev, channel, MIPI_DSI_DCS_READ, dcs_cmd, 0);
	if (r) {
		DSSERR("dsi_vc_dcs_send_read_request(ch %d, cmd 0x%02x)"
			" failed\n", channel, dcs_cmd);
		return r;
	}

	return 0;
}

static int dsi_vc_generic_send_read_request(struct platform_device *dsidev,
		int channel, u8 *reqdata, int reqlen)
{
	struct dsi_data *dsi = dsi_get_dsidrv_data(dsidev);
	u16 data;
	u8 data_type;
	int r;

	if (dsi->debug_read)
		DSSDBG("dsi_vc_generic_send_read_request(ch %d, reqlen %d)\n",
			channel, reqlen);

	if (reqlen == 0) {
		data_type = MIPI_DSI_GENERIC_READ_REQUEST_0_PARAM;
		data = 0;
	} else if (reqlen == 1) {
		data_type = MIPI_DSI_GENERIC_READ_REQUEST_1_PARAM;
		data = reqdata[0];
	} else if (reqlen == 2) {
		data_type = MIPI_DSI_GENERIC_READ_REQUEST_2_PARAM;
		data = reqdata[0] | (reqdata[1] << 8);
	} else {
		BUG();
		return -EINVAL;
	}

	r = dsi_vc_send_short(dsidev, channel, data_type, data, 0);
	if (r) {
		DSSERR("dsi_vc_generic_send_read_request(ch %d, reqlen %d)"
			" failed\n", channel, reqlen);
		return r;
	}

	return 0;
}

static int dsi_vc_read_rx_fifo(struct platform_device *dsidev, int channel,
		u8 *buf, int buflen, enum dss_dsi_content_type type)
{
	struct dsi_data *dsi = dsi_get_dsidrv_data(dsidev);
	u32 val;
	u8 dt;
	int r;

	/* RX_FIFO_NOT_EMPTY */
	if (REG_GET(dsidev, DSI_VC_CTRL(channel), 20, 20) == 0) {
		DSSERR("RX fifo empty when trying to read.\n");
		r = -EIO;
		goto err;
	}

	val = dsi_read_reg(dsidev, DSI_VC_SHORT_PACKET_HEADER(channel));
	if (dsi->debug_read)
		DSSDBG("\theader: %08x\n", val);
	dt = FLD_GET(val, 5, 0);
	if (dt == MIPI_DSI_RX_ACKNOWLEDGE_AND_ERROR_REPORT) {
		u16 err = FLD_GET(val, 23, 8);
		dsi_show_rx_ack_with_err(err);
		r = -EIO;
		goto err;

	} else if (dt == (type == DSS_DSI_CONTENT_GENERIC ?
			MIPI_DSI_RX_GENERIC_SHORT_READ_RESPONSE_1BYTE :
			MIPI_DSI_RX_DCS_SHORT_READ_RESPONSE_1BYTE)) {
		u8 data = FLD_GET(val, 15, 8);
		if (dsi->debug_read)
			DSSDBG("\t%s short response, 1 byte: %02x\n",
				type == DSS_DSI_CONTENT_GENERIC ? "GENERIC" :
				"DCS", data);

		if (buflen < 1) {
			r = -EIO;
			goto err;
		}

		buf[0] = data;

		return 1;
	} else if (dt == (type == DSS_DSI_CONTENT_GENERIC ?
			MIPI_DSI_RX_GENERIC_SHORT_READ_RESPONSE_2BYTE :
			MIPI_DSI_RX_DCS_SHORT_READ_RESPONSE_2BYTE)) {
		u16 data = FLD_GET(val, 23, 8);
		if (dsi->debug_read)
			DSSDBG("\t%s short response, 2 byte: %04x\n",
				type == DSS_DSI_CONTENT_GENERIC ? "GENERIC" :
				"DCS", data);

		if (buflen < 2) {
			r = -EIO;
			goto err;
		}

		buf[0] = data & 0xff;
		buf[1] = (data >> 8) & 0xff;

		return 2;
	} else if (dt == (type == DSS_DSI_CONTENT_GENERIC ?
			MIPI_DSI_RX_GENERIC_LONG_READ_RESPONSE :
			MIPI_DSI_RX_DCS_LONG_READ_RESPONSE)) {
		int w;
		int len = FLD_GET(val, 23, 8);
		if (dsi->debug_read)
			DSSDBG("\t%s long response, len %d\n",
				type == DSS_DSI_CONTENT_GENERIC ? "GENERIC" :
				"DCS", len);

		if (len > buflen) {
			r = -EIO;
			goto err;
		}

		/* two byte checksum ends the packet, not included in len */
		for (w = 0; w < len + 2;) {
			int b;
			val = dsi_read_reg(dsidev,
				DSI_VC_SHORT_PACKET_HEADER(channel));
			if (dsi->debug_read)
				DSSDBG("\t\t%02x %02x %02x %02x\n",
						(val >> 0) & 0xff,
						(val >> 8) & 0xff,
						(val >> 16) & 0xff,
						(val >> 24) & 0xff);

			for (b = 0; b < 4; ++b) {
				if (w < len)
					buf[w] = (val >> (b * 8)) & 0xff;
				/* we discard the 2 byte checksum */
				++w;
			}
		}

		return len;
	} else {
		DSSERR("\tunknown datatype 0x%02x\n", dt);
		r = -EIO;
		goto err;
	}

err:
	DSSERR("dsi_vc_read_rx_fifo(ch %d type %s) failed\n", channel,
		type == DSS_DSI_CONTENT_GENERIC ? "GENERIC" : "DCS");

	return r;
}

static int dsi_vc_dcs_read(struct omap_dss_device *dssdev, int channel, u8 dcs_cmd,
		u8 *buf, int buflen)
{
	struct platform_device *dsidev = dsi_get_dsidev_from_dssdev(dssdev);
	int r;

	r = dsi_vc_dcs_send_read_request(dsidev, channel, dcs_cmd);
	if (r)
		goto err;

	r = dsi_vc_send_bta_sync(dssdev, channel);
	if (r)
		goto err;

	r = dsi_vc_read_rx_fifo(dsidev, channel, buf, buflen,
		DSS_DSI_CONTENT_DCS);
	if (r < 0)
		goto err;

	if (r != buflen) {
		r = -EIO;
		goto err;
	}

	return 0;
err:
	DSSERR("dsi_vc_dcs_read(ch %d, cmd 0x%02x) failed\n", channel, dcs_cmd);
	return r;
}

static int dsi_vc_generic_read(struct omap_dss_device *dssdev, int channel,
		u8 *reqdata, int reqlen, u8 *buf, int buflen)
{
	struct platform_device *dsidev = dsi_get_dsidev_from_dssdev(dssdev);
	int r;

	r = dsi_vc_generic_send_read_request(dsidev, channel, reqdata, reqlen);
	if (r)
		return r;

	r = dsi_vc_send_bta_sync(dssdev, channel);
	if (r)
		return r;

	r = dsi_vc_read_rx_fifo(dsidev, channel, buf, buflen,
		DSS_DSI_CONTENT_GENERIC);
	if (r < 0)
		return r;

	if (r != buflen) {
		r = -EIO;
		return r;
	}

	return 0;
}

static int dsi_vc_set_max_rx_packet_size(struct omap_dss_device *dssdev, int channel,
		u16 len)
{
	struct platform_device *dsidev = dsi_get_dsidev_from_dssdev(dssdev);

	return dsi_vc_send_short(dsidev, channel,
			MIPI_DSI_SET_MAXIMUM_RETURN_PACKET_SIZE, len, 0);
}

static int dsi_enter_ulps(struct platform_device *dsidev)
{
	struct dsi_data *dsi = dsi_get_dsidrv_data(dsidev);
	DECLARE_COMPLETION_ONSTACK(completion);
	int r, i;
	unsigned mask;

	DSSDBG("Entering ULPS");

	WARN_ON(!dsi_bus_is_locked(dsidev));

	WARN_ON(dsi->ulps_enabled);

	if (dsi->ulps_enabled)
		return 0;

	/* DDR_CLK_ALWAYS_ON */
	if (REG_GET(dsidev, DSI_CLK_CTRL, 13, 13)) {
		dsi_if_enable(dsidev, 0);
		REG_FLD_MOD(dsidev, DSI_CLK_CTRL, 0, 13, 13);
		dsi_if_enable(dsidev, 1);
	}

	dsi_sync_vc(dsidev, 0);
	dsi_sync_vc(dsidev, 1);
	dsi_sync_vc(dsidev, 2);
	dsi_sync_vc(dsidev, 3);

	dsi_force_tx_stop_mode_io(dsidev);

	dsi_vc_enable(dsidev, 0, false);
	dsi_vc_enable(dsidev, 1, false);
	dsi_vc_enable(dsidev, 2, false);
	dsi_vc_enable(dsidev, 3, false);

	if (REG_GET(dsidev, DSI_COMPLEXIO_CFG2, 16, 16)) {	/* HS_BUSY */
		DSSERR("HS busy when enabling ULPS\n");
		return -EIO;
	}

	if (REG_GET(dsidev, DSI_COMPLEXIO_CFG2, 17, 17)) {	/* LP_BUSY */
		DSSERR("LP busy when enabling ULPS\n");
		return -EIO;
	}

	r = dsi_register_isr_cio(dsidev, dsi_completion_handler, &completion,
			DSI_CIO_IRQ_ULPSACTIVENOT_ALL0);
	if (r)
		return r;

	mask = 0;

	for (i = 0; i < dsi->num_lanes_supported; ++i) {
		if (dsi->lanes[i].function == DSI_LANE_UNUSED)
			continue;
		mask |= 1 << i;
	}
	/* Assert TxRequestEsc for data lanes and TxUlpsClk for clk lane */
	/* LANEx_ULPS_SIG2 */
	REG_FLD_MOD(dsidev, DSI_COMPLEXIO_CFG2, mask, 9, 5);

	/* flush posted write and wait for SCP interface to finish the write */
	dsi_read_reg(dsidev, DSI_COMPLEXIO_CFG2);

	if (wait_for_completion_timeout(&completion,
				msecs_to_jiffies(1000)) == 0) {
		DSSERR("ULPS enable timeout\n");
		r = -EIO;
		goto err;
	}

	dsi_unregister_isr_cio(dsidev, dsi_completion_handler, &completion,
			DSI_CIO_IRQ_ULPSACTIVENOT_ALL0);

	/* Reset LANEx_ULPS_SIG2 */
	REG_FLD_MOD(dsidev, DSI_COMPLEXIO_CFG2, 0, 9, 5);

	/* flush posted write and wait for SCP interface to finish the write */
	dsi_read_reg(dsidev, DSI_COMPLEXIO_CFG2);

	dsi_cio_power(dsidev, DSI_COMPLEXIO_POWER_ULPS);

	dsi_if_enable(dsidev, false);

	dsi->ulps_enabled = true;

	return 0;

err:
	dsi_unregister_isr_cio(dsidev, dsi_completion_handler, &completion,
			DSI_CIO_IRQ_ULPSACTIVENOT_ALL0);
	return r;
}

static void dsi_set_lp_rx_timeout(struct platform_device *dsidev,
		unsigned ticks, bool x4, bool x16)
{
	unsigned long fck;
	unsigned long total_ticks;
	u32 r;

	BUG_ON(ticks > 0x1fff);

	/* ticks in DSI_FCK */
	fck = dsi_fclk_rate(dsidev);

	r = dsi_read_reg(dsidev, DSI_TIMING2);
	r = FLD_MOD(r, 1, 15, 15);	/* LP_RX_TO */
	r = FLD_MOD(r, x16 ? 1 : 0, 14, 14);	/* LP_RX_TO_X16 */
	r = FLD_MOD(r, x4 ? 1 : 0, 13, 13);	/* LP_RX_TO_X4 */
	r = FLD_MOD(r, ticks, 12, 0);	/* LP_RX_COUNTER */
	dsi_write_reg(dsidev, DSI_TIMING2, r);

	total_ticks = ticks * (x16 ? 16 : 1) * (x4 ? 4 : 1);

	DSSDBG("LP_RX_TO %lu ticks (%#x%s%s) = %lu ns\n",
			total_ticks,
			ticks, x4 ? " x4" : "", x16 ? " x16" : "",
			(total_ticks * 1000) / (fck / 1000 / 1000));
}

static void dsi_set_ta_timeout(struct platform_device *dsidev, unsigned ticks,
		bool x8, bool x16)
{
	unsigned long fck;
	unsigned long total_ticks;
	u32 r;

	BUG_ON(ticks > 0x1fff);

	/* ticks in DSI_FCK */
	fck = dsi_fclk_rate(dsidev);

	r = dsi_read_reg(dsidev, DSI_TIMING1);
	r = FLD_MOD(r, 1, 31, 31);	/* TA_TO */
	r = FLD_MOD(r, x16 ? 1 : 0, 30, 30);	/* TA_TO_X16 */
	r = FLD_MOD(r, x8 ? 1 : 0, 29, 29);	/* TA_TO_X8 */
	r = FLD_MOD(r, ticks, 28, 16);	/* TA_TO_COUNTER */
	dsi_write_reg(dsidev, DSI_TIMING1, r);

	total_ticks = ticks * (x16 ? 16 : 1) * (x8 ? 8 : 1);

	DSSDBG("TA_TO %lu ticks (%#x%s%s) = %lu ns\n",
			total_ticks,
			ticks, x8 ? " x8" : "", x16 ? " x16" : "",
			(total_ticks * 1000) / (fck / 1000 / 1000));
}

static void dsi_set_stop_state_counter(struct platform_device *dsidev,
		unsigned ticks, bool x4, bool x16)
{
	unsigned long fck;
	unsigned long total_ticks;
	u32 r;

	BUG_ON(ticks > 0x1fff);

	/* ticks in DSI_FCK */
	fck = dsi_fclk_rate(dsidev);

	r = dsi_read_reg(dsidev, DSI_TIMING1);
	r = FLD_MOD(r, 1, 15, 15);	/* FORCE_TX_STOP_MODE_IO */
	r = FLD_MOD(r, x16 ? 1 : 0, 14, 14);	/* STOP_STATE_X16_IO */
	r = FLD_MOD(r, x4 ? 1 : 0, 13, 13);	/* STOP_STATE_X4_IO */
	r = FLD_MOD(r, ticks, 12, 0);	/* STOP_STATE_COUNTER_IO */
	dsi_write_reg(dsidev, DSI_TIMING1, r);

	total_ticks = ticks * (x16 ? 16 : 1) * (x4 ? 4 : 1);

	DSSDBG("STOP_STATE_COUNTER %lu ticks (%#x%s%s) = %lu ns\n",
			total_ticks,
			ticks, x4 ? " x4" : "", x16 ? " x16" : "",
			(total_ticks * 1000) / (fck / 1000 / 1000));
}

static void dsi_set_hs_tx_timeout(struct platform_device *dsidev,
		unsigned ticks, bool x4, bool x16)
{
	unsigned long fck;
	unsigned long total_ticks;
	u32 r;

	BUG_ON(ticks > 0x1fff);

	/* ticks in TxByteClkHS */
	fck = dsi_get_txbyteclkhs(dsidev);

	r = dsi_read_reg(dsidev, DSI_TIMING2);
	r = FLD_MOD(r, 1, 31, 31);	/* HS_TX_TO */
	r = FLD_MOD(r, x16 ? 1 : 0, 30, 30);	/* HS_TX_TO_X16 */
	r = FLD_MOD(r, x4 ? 1 : 0, 29, 29);	/* HS_TX_TO_X8 (4 really) */
	r = FLD_MOD(r, ticks, 28, 16);	/* HS_TX_TO_COUNTER */
	dsi_write_reg(dsidev, DSI_TIMING2, r);

	total_ticks = ticks * (x16 ? 16 : 1) * (x4 ? 4 : 1);

	DSSDBG("HS_TX_TO %lu ticks (%#x%s%s) = %lu ns\n",
			total_ticks,
			ticks, x4 ? " x4" : "", x16 ? " x16" : "",
			(total_ticks * 1000) / (fck / 1000 / 1000));
}

static void dsi_config_vp_num_line_buffers(struct platform_device *dsidev)
{
	struct dsi_data *dsi = dsi_get_dsidrv_data(dsidev);
	int num_line_buffers;

	if (dsi->mode == OMAP_DSS_DSI_VIDEO_MODE) {
		int bpp = dsi_get_pixel_size(dsi->pix_fmt);
		struct omap_video_timings *timings = &dsi->timings;
		/*
		 * Don't use line buffers if width is greater than the video
		 * port's line buffer size
		 */
		if (dsi->line_buffer_size <= timings->x_res * bpp / 8)
			num_line_buffers = 0;
		else
			num_line_buffers = 2;
	} else {
		/* Use maximum number of line buffers in command mode */
		num_line_buffers = 2;
	}

	/* LINE_BUFFER */
	REG_FLD_MOD(dsidev, DSI_CTRL, num_line_buffers, 13, 12);
}

static void dsi_config_vp_sync_events(struct platform_device *dsidev)
{
	struct dsi_data *dsi = dsi_get_dsidrv_data(dsidev);
	bool sync_end;
	u32 r;

	if (dsi->vm_timings.trans_mode == OMAP_DSS_DSI_PULSE_MODE)
		sync_end = true;
	else
		sync_end = false;

	r = dsi_read_reg(dsidev, DSI_CTRL);
	r = FLD_MOD(r, 1, 9, 9);		/* VP_DE_POL */
	r = FLD_MOD(r, 1, 10, 10);		/* VP_HSYNC_POL */
	r = FLD_MOD(r, 1, 11, 11);		/* VP_VSYNC_POL */
	r = FLD_MOD(r, 1, 15, 15);		/* VP_VSYNC_START */
	r = FLD_MOD(r, sync_end, 16, 16);	/* VP_VSYNC_END */
	r = FLD_MOD(r, 1, 17, 17);		/* VP_HSYNC_START */
	r = FLD_MOD(r, sync_end, 18, 18);	/* VP_HSYNC_END */
	dsi_write_reg(dsidev, DSI_CTRL, r);
}

static void dsi_config_blanking_modes(struct platform_device *dsidev)
{
	struct dsi_data *dsi = dsi_get_dsidrv_data(dsidev);
	int blanking_mode = dsi->vm_timings.blanking_mode;
	int hfp_blanking_mode = dsi->vm_timings.hfp_blanking_mode;
	int hbp_blanking_mode = dsi->vm_timings.hbp_blanking_mode;
	int hsa_blanking_mode = dsi->vm_timings.hsa_blanking_mode;
	u32 r;

	/*
	 * 0 = TX FIFO packets sent or LPS in corresponding blanking periods
	 * 1 = Long blanking packets are sent in corresponding blanking periods
	 */
	r = dsi_read_reg(dsidev, DSI_CTRL);
	r = FLD_MOD(r, blanking_mode, 20, 20);		/* BLANKING_MODE */
	r = FLD_MOD(r, hfp_blanking_mode, 21, 21);	/* HFP_BLANKING */
	r = FLD_MOD(r, hbp_blanking_mode, 22, 22);	/* HBP_BLANKING */
	r = FLD_MOD(r, hsa_blanking_mode, 23, 23);	/* HSA_BLANKING */
	dsi_write_reg(dsidev, DSI_CTRL, r);
}

/*
 * According to section 'HS Command Mode Interleaving' in OMAP TRM, Scenario 3
 * results in maximum transition time for data and clock lanes to enter and
 * exit HS mode. Hence, this is the scenario where the least amount of command
 * mode data can be interleaved. We program the minimum amount of TXBYTECLKHS
 * clock cycles that can be used to interleave command mode data in HS so that
 * all scenarios are satisfied.
 */
static int dsi_compute_interleave_hs(int blank, bool ddr_alwon, int enter_hs,
		int exit_hs, int exiths_clk, int ddr_pre, int ddr_post)
{
	int transition;

	/*
	 * If DDR_CLK_ALWAYS_ON is set, we need to consider HS mode transition
	 * time of data lanes only, if it isn't set, we need to consider HS
	 * transition time of both data and clock lanes. HS transition time
	 * of Scenario 3 is considered.
	 */
	if (ddr_alwon) {
		transition = enter_hs + exit_hs + max(enter_hs, 2) + 1;
	} else {
		int trans1, trans2;
		trans1 = ddr_pre + enter_hs + exit_hs + max(enter_hs, 2) + 1;
		trans2 = ddr_pre + enter_hs + exiths_clk + ddr_post + ddr_pre +
				enter_hs + 1;
		transition = max(trans1, trans2);
	}

	return blank > transition ? blank - transition : 0;
}

/*
 * According to section 'LP Command Mode Interleaving' in OMAP TRM, Scenario 1
 * results in maximum transition time for data lanes to enter and exit LP mode.
 * Hence, this is the scenario where the least amount of command mode data can
 * be interleaved. We program the minimum amount of bytes that can be
 * interleaved in LP so that all scenarios are satisfied.
 */
static int dsi_compute_interleave_lp(int blank, int enter_hs, int exit_hs,
		int lp_clk_div, int tdsi_fclk)
{
	int trans_lp;	/* time required for a LP transition, in TXBYTECLKHS */
	int tlp_avail;	/* time left for interleaving commands, in CLKIN4DDR */
	int ttxclkesc;	/* period of LP transmit escape clock, in CLKIN4DDR */
	int thsbyte_clk = 16;	/* Period of TXBYTECLKHS clock, in CLKIN4DDR */
	int lp_inter;	/* cmd mode data that can be interleaved, in bytes */

	/* maximum LP transition time according to Scenario 1 */
	trans_lp = exit_hs + max(enter_hs, 2) + 1;

	/* CLKIN4DDR = 16 * TXBYTECLKHS */
	tlp_avail = thsbyte_clk * (blank - trans_lp);

	ttxclkesc = tdsi_fclk * lp_clk_div;

	lp_inter = ((tlp_avail - 8 * thsbyte_clk - 5 * tdsi_fclk) / ttxclkesc -
			26) / 16;

	return max(lp_inter, 0);
}

static void dsi_config_cmd_mode_interleaving(struct platform_device *dsidev)
{
	struct dsi_data *dsi = dsi_get_dsidrv_data(dsidev);
	int blanking_mode;
	int hfp_blanking_mode, hbp_blanking_mode, hsa_blanking_mode;
	int hsa, hfp, hbp, width_bytes, bllp, lp_clk_div;
	int ddr_clk_pre, ddr_clk_post, enter_hs_mode_lat, exit_hs_mode_lat;
	int tclk_trail, ths_exit, exiths_clk;
	bool ddr_alwon;
	struct omap_video_timings *timings = &dsi->timings;
	int bpp = dsi_get_pixel_size(dsi->pix_fmt);
	int ndl = dsi->num_lanes_used - 1;
	int dsi_fclk_hsdiv = dsi->user_dsi_cinfo.mX[HSDIV_DSI] + 1;
	int hsa_interleave_hs = 0, hsa_interleave_lp = 0;
	int hfp_interleave_hs = 0, hfp_interleave_lp = 0;
	int hbp_interleave_hs = 0, hbp_interleave_lp = 0;
	int bl_interleave_hs = 0, bl_interleave_lp = 0;
	u32 r;

	r = dsi_read_reg(dsidev, DSI_CTRL);
	blanking_mode = FLD_GET(r, 20, 20);
	hfp_blanking_mode = FLD_GET(r, 21, 21);
	hbp_blanking_mode = FLD_GET(r, 22, 22);
	hsa_blanking_mode = FLD_GET(r, 23, 23);

	r = dsi_read_reg(dsidev, DSI_VM_TIMING1);
	hbp = FLD_GET(r, 11, 0);
	hfp = FLD_GET(r, 23, 12);
	hsa = FLD_GET(r, 31, 24);

	r = dsi_read_reg(dsidev, DSI_CLK_TIMING);
	ddr_clk_post = FLD_GET(r, 7, 0);
	ddr_clk_pre = FLD_GET(r, 15, 8);

	r = dsi_read_reg(dsidev, DSI_VM_TIMING7);
	exit_hs_mode_lat = FLD_GET(r, 15, 0);
	enter_hs_mode_lat = FLD_GET(r, 31, 16);

	r = dsi_read_reg(dsidev, DSI_CLK_CTRL);
	lp_clk_div = FLD_GET(r, 12, 0);
	ddr_alwon = FLD_GET(r, 13, 13);

	r = dsi_read_reg(dsidev, DSI_DSIPHY_CFG0);
	ths_exit = FLD_GET(r, 7, 0);

	r = dsi_read_reg(dsidev, DSI_DSIPHY_CFG1);
	tclk_trail = FLD_GET(r, 15, 8);

	exiths_clk = ths_exit + tclk_trail;

	width_bytes = DIV_ROUND_UP(timings->x_res * bpp, 8);
	bllp = hbp + hfp + hsa + DIV_ROUND_UP(width_bytes + 6, ndl);

	if (!hsa_blanking_mode) {
		hsa_interleave_hs = dsi_compute_interleave_hs(hsa, ddr_alwon,
					enter_hs_mode_lat, exit_hs_mode_lat,
					exiths_clk, ddr_clk_pre, ddr_clk_post);
		hsa_interleave_lp = dsi_compute_interleave_lp(hsa,
					enter_hs_mode_lat, exit_hs_mode_lat,
					lp_clk_div, dsi_fclk_hsdiv);
	}

	if (!hfp_blanking_mode) {
		hfp_interleave_hs = dsi_compute_interleave_hs(hfp, ddr_alwon,
					enter_hs_mode_lat, exit_hs_mode_lat,
					exiths_clk, ddr_clk_pre, ddr_clk_post);
		hfp_interleave_lp = dsi_compute_interleave_lp(hfp,
					enter_hs_mode_lat, exit_hs_mode_lat,
					lp_clk_div, dsi_fclk_hsdiv);
	}

	if (!hbp_blanking_mode) {
		hbp_interleave_hs = dsi_compute_interleave_hs(hbp, ddr_alwon,
					enter_hs_mode_lat, exit_hs_mode_lat,
					exiths_clk, ddr_clk_pre, ddr_clk_post);

		hbp_interleave_lp = dsi_compute_interleave_lp(hbp,
					enter_hs_mode_lat, exit_hs_mode_lat,
					lp_clk_div, dsi_fclk_hsdiv);
	}

	if (!blanking_mode) {
		bl_interleave_hs = dsi_compute_interleave_hs(bllp, ddr_alwon,
					enter_hs_mode_lat, exit_hs_mode_lat,
					exiths_clk, ddr_clk_pre, ddr_clk_post);

		bl_interleave_lp = dsi_compute_interleave_lp(bllp,
					enter_hs_mode_lat, exit_hs_mode_lat,
					lp_clk_div, dsi_fclk_hsdiv);
	}

	DSSDBG("DSI HS interleaving(TXBYTECLKHS) HSA %d, HFP %d, HBP %d, BLLP %d\n",
		hsa_interleave_hs, hfp_interleave_hs, hbp_interleave_hs,
		bl_interleave_hs);

	DSSDBG("DSI LP interleaving(bytes) HSA %d, HFP %d, HBP %d, BLLP %d\n",
		hsa_interleave_lp, hfp_interleave_lp, hbp_interleave_lp,
		bl_interleave_lp);

	r = dsi_read_reg(dsidev, DSI_VM_TIMING4);
	r = FLD_MOD(r, hsa_interleave_hs, 23, 16);
	r = FLD_MOD(r, hfp_interleave_hs, 15, 8);
	r = FLD_MOD(r, hbp_interleave_hs, 7, 0);
	dsi_write_reg(dsidev, DSI_VM_TIMING4, r);

	r = dsi_read_reg(dsidev, DSI_VM_TIMING5);
	r = FLD_MOD(r, hsa_interleave_lp, 23, 16);
	r = FLD_MOD(r, hfp_interleave_lp, 15, 8);
	r = FLD_MOD(r, hbp_interleave_lp, 7, 0);
	dsi_write_reg(dsidev, DSI_VM_TIMING5, r);

	r = dsi_read_reg(dsidev, DSI_VM_TIMING6);
	r = FLD_MOD(r, bl_interleave_hs, 31, 15);
	r = FLD_MOD(r, bl_interleave_lp, 16, 0);
	dsi_write_reg(dsidev, DSI_VM_TIMING6, r);
}

static int dsi_proto_config(struct platform_device *dsidev)
{
	struct dsi_data *dsi = dsi_get_dsidrv_data(dsidev);
	u32 r;
	int buswidth = 0;

	dsi_config_tx_fifo(dsidev, DSI_FIFO_SIZE_32,
			DSI_FIFO_SIZE_32,
			DSI_FIFO_SIZE_32,
			DSI_FIFO_SIZE_32);

	dsi_config_rx_fifo(dsidev, DSI_FIFO_SIZE_32,
			DSI_FIFO_SIZE_32,
			DSI_FIFO_SIZE_32,
			DSI_FIFO_SIZE_32);

	/* XXX what values for the timeouts? */
	dsi_set_stop_state_counter(dsidev, 0x1000, false, false);
	dsi_set_ta_timeout(dsidev, 0x1fff, true, true);
	dsi_set_lp_rx_timeout(dsidev, 0x1fff, true, true);
	dsi_set_hs_tx_timeout(dsidev, 0x1fff, true, true);

	switch (dsi_get_pixel_size(dsi->pix_fmt)) {
	case 16:
		buswidth = 0;
		break;
	case 18:
		buswidth = 1;
		break;
	case 24:
		buswidth = 2;
		break;
	default:
		BUG();
		return -EINVAL;
	}

	r = dsi_read_reg(dsidev, DSI_CTRL);
	r = FLD_MOD(r, 1, 1, 1);	/* CS_RX_EN */
	r = FLD_MOD(r, 1, 2, 2);	/* ECC_RX_EN */
	r = FLD_MOD(r, 1, 3, 3);	/* TX_FIFO_ARBITRATION */
	r = FLD_MOD(r, 1, 4, 4);	/* VP_CLK_RATIO, always 1, see errata*/
	r = FLD_MOD(r, buswidth, 7, 6); /* VP_DATA_BUS_WIDTH */
	r = FLD_MOD(r, 0, 8, 8);	/* VP_CLK_POL */
	r = FLD_MOD(r, 1, 14, 14);	/* TRIGGER_RESET_MODE */
	r = FLD_MOD(r, 1, 19, 19);	/* EOT_ENABLE */
	if (!dss_has_feature(FEAT_DSI_DCS_CMD_CONFIG_VC)) {
		r = FLD_MOD(r, 1, 24, 24);	/* DCS_CMD_ENABLE */
		/* DCS_CMD_CODE, 1=start, 0=continue */
		r = FLD_MOD(r, 0, 25, 25);
	}

	dsi_write_reg(dsidev, DSI_CTRL, r);

	dsi_config_vp_num_line_buffers(dsidev);

	if (dsi->mode == OMAP_DSS_DSI_VIDEO_MODE) {
		dsi_config_vp_sync_events(dsidev);
		dsi_config_blanking_modes(dsidev);
		dsi_config_cmd_mode_interleaving(dsidev);
	}

	dsi_vc_initial_config(dsidev, 0);
	dsi_vc_initial_config(dsidev, 1);
	dsi_vc_initial_config(dsidev, 2);
	dsi_vc_initial_config(dsidev, 3);

	return 0;
}

static void dsi_proto_timings(struct platform_device *dsidev)
{
	struct dsi_data *dsi = dsi_get_dsidrv_data(dsidev);
	unsigned tlpx, tclk_zero, tclk_prepare, tclk_trail;
	unsigned tclk_pre, tclk_post;
	unsigned ths_prepare, ths_prepare_ths_zero, ths_zero;
	unsigned ths_trail, ths_exit;
	unsigned ddr_clk_pre, ddr_clk_post;
	unsigned enter_hs_mode_lat, exit_hs_mode_lat;
	unsigned ths_eot;
	int ndl = dsi->num_lanes_used - 1;
	u32 r;

	r = dsi_read_reg(dsidev, DSI_DSIPHY_CFG0);
	ths_prepare = FLD_GET(r, 31, 24);
	ths_prepare_ths_zero = FLD_GET(r, 23, 16);
	ths_zero = ths_prepare_ths_zero - ths_prepare;
	ths_trail = FLD_GET(r, 15, 8);
	ths_exit = FLD_GET(r, 7, 0);

	r = dsi_read_reg(dsidev, DSI_DSIPHY_CFG1);
	tlpx = FLD_GET(r, 20, 16) * 2;
	tclk_trail = FLD_GET(r, 15, 8);
	tclk_zero = FLD_GET(r, 7, 0);

	r = dsi_read_reg(dsidev, DSI_DSIPHY_CFG2);
	tclk_prepare = FLD_GET(r, 7, 0);

	/* min 8*UI */
	tclk_pre = 20;
	/* min 60ns + 52*UI */
	tclk_post = ns2ddr(dsidev, 60) + 26;

	ths_eot = DIV_ROUND_UP(4, ndl);

	ddr_clk_pre = DIV_ROUND_UP(tclk_pre + tlpx + tclk_zero + tclk_prepare,
			4);
	ddr_clk_post = DIV_ROUND_UP(tclk_post + ths_trail, 4) + ths_eot;

	BUG_ON(ddr_clk_pre == 0 || ddr_clk_pre > 255);
	BUG_ON(ddr_clk_post == 0 || ddr_clk_post > 255);

	r = dsi_read_reg(dsidev, DSI_CLK_TIMING);
	r = FLD_MOD(r, ddr_clk_pre, 15, 8);
	r = FLD_MOD(r, ddr_clk_post, 7, 0);
	dsi_write_reg(dsidev, DSI_CLK_TIMING, r);

	DSSDBG("ddr_clk_pre %u, ddr_clk_post %u\n",
			ddr_clk_pre,
			ddr_clk_post);

	enter_hs_mode_lat = 1 + DIV_ROUND_UP(tlpx, 4) +
		DIV_ROUND_UP(ths_prepare, 4) +
		DIV_ROUND_UP(ths_zero + 3, 4);

	exit_hs_mode_lat = DIV_ROUND_UP(ths_trail + ths_exit, 4) + 1 + ths_eot;

	r = FLD_VAL(enter_hs_mode_lat, 31, 16) |
		FLD_VAL(exit_hs_mode_lat, 15, 0);
	dsi_write_reg(dsidev, DSI_VM_TIMING7, r);

	DSSDBG("enter_hs_mode_lat %u, exit_hs_mode_lat %u\n",
			enter_hs_mode_lat, exit_hs_mode_lat);

	 if (dsi->mode == OMAP_DSS_DSI_VIDEO_MODE) {
		/* TODO: Implement a video mode check_timings function */
		int hsa = dsi->vm_timings.hsa;
		int hfp = dsi->vm_timings.hfp;
		int hbp = dsi->vm_timings.hbp;
		int vsa = dsi->vm_timings.vsa;
		int vfp = dsi->vm_timings.vfp;
		int vbp = dsi->vm_timings.vbp;
		int window_sync = dsi->vm_timings.window_sync;
		bool hsync_end;
		struct omap_video_timings *timings = &dsi->timings;
		int bpp = dsi_get_pixel_size(dsi->pix_fmt);
		int tl, t_he, width_bytes;

		hsync_end = dsi->vm_timings.trans_mode == OMAP_DSS_DSI_PULSE_MODE;
		t_he = hsync_end ?
			((hsa == 0 && ndl == 3) ? 1 : DIV_ROUND_UP(4, ndl)) : 0;

		width_bytes = DIV_ROUND_UP(timings->x_res * bpp, 8);

		/* TL = t_HS + HSA + t_HE + HFP + ceil((WC + 6) / NDL) + HBP */
		tl = DIV_ROUND_UP(4, ndl) + (hsync_end ? hsa : 0) + t_he + hfp +
			DIV_ROUND_UP(width_bytes + 6, ndl) + hbp;

		DSSDBG("HBP: %d, HFP: %d, HSA: %d, TL: %d TXBYTECLKHS\n", hbp,
			hfp, hsync_end ? hsa : 0, tl);
		DSSDBG("VBP: %d, VFP: %d, VSA: %d, VACT: %d lines\n", vbp, vfp,
			vsa, timings->y_res);

		r = dsi_read_reg(dsidev, DSI_VM_TIMING1);
		r = FLD_MOD(r, hbp, 11, 0);	/* HBP */
		r = FLD_MOD(r, hfp, 23, 12);	/* HFP */
		r = FLD_MOD(r, hsync_end ? hsa : 0, 31, 24);	/* HSA */
		dsi_write_reg(dsidev, DSI_VM_TIMING1, r);

		r = dsi_read_reg(dsidev, DSI_VM_TIMING2);
		r = FLD_MOD(r, vbp, 7, 0);	/* VBP */
		r = FLD_MOD(r, vfp, 15, 8);	/* VFP */
		r = FLD_MOD(r, vsa, 23, 16);	/* VSA */
		r = FLD_MOD(r, window_sync, 27, 24);	/* WINDOW_SYNC */
		dsi_write_reg(dsidev, DSI_VM_TIMING2, r);

		r = dsi_read_reg(dsidev, DSI_VM_TIMING3);
		r = FLD_MOD(r, timings->y_res, 14, 0);	/* VACT */
		r = FLD_MOD(r, tl, 31, 16);		/* TL */
		dsi_write_reg(dsidev, DSI_VM_TIMING3, r);
	}
}

static int dsi_configure_pins(struct omap_dss_device *dssdev,
		const struct omap_dsi_pin_config *pin_cfg)
{
	struct platform_device *dsidev = dsi_get_dsidev_from_dssdev(dssdev);
	struct dsi_data *dsi = dsi_get_dsidrv_data(dsidev);
	int num_pins;
	const int *pins;
	struct dsi_lane_config lanes[DSI_MAX_NR_LANES];
	int num_lanes;
	int i;

	static const enum dsi_lane_function functions[] = {
		DSI_LANE_CLK,
		DSI_LANE_DATA1,
		DSI_LANE_DATA2,
		DSI_LANE_DATA3,
		DSI_LANE_DATA4,
	};

	num_pins = pin_cfg->num_pins;
	pins = pin_cfg->pins;

	if (num_pins < 4 || num_pins > dsi->num_lanes_supported * 2
			|| num_pins % 2 != 0)
		return -EINVAL;

	for (i = 0; i < DSI_MAX_NR_LANES; ++i)
		lanes[i].function = DSI_LANE_UNUSED;

	num_lanes = 0;

	for (i = 0; i < num_pins; i += 2) {
		u8 lane, pol;
		int dx, dy;

		dx = pins[i];
		dy = pins[i + 1];

		if (dx < 0 || dx >= dsi->num_lanes_supported * 2)
			return -EINVAL;

		if (dy < 0 || dy >= dsi->num_lanes_supported * 2)
			return -EINVAL;

		if (dx & 1) {
			if (dy != dx - 1)
				return -EINVAL;
			pol = 1;
		} else {
			if (dy != dx + 1)
				return -EINVAL;
			pol = 0;
		}

		lane = dx / 2;

		lanes[lane].function = functions[i / 2];
		lanes[lane].polarity = pol;
		num_lanes++;
	}

	memcpy(dsi->lanes, lanes, sizeof(dsi->lanes));
	dsi->num_lanes_used = num_lanes;

	return 0;
}

static int dsi_enable_video_output(struct omap_dss_device *dssdev, int channel)
{
	struct platform_device *dsidev = dsi_get_dsidev_from_dssdev(dssdev);
	struct dsi_data *dsi = dsi_get_dsidrv_data(dsidev);
	enum omap_channel dispc_channel = dssdev->dispc_channel;
	int bpp = dsi_get_pixel_size(dsi->pix_fmt);
	struct omap_dss_device *out = &dsi->output;
	u8 data_type;
	u16 word_count;
	int r;

	if (!out->dispc_channel_connected) {
		DSSERR("failed to enable display: no output/manager\n");
		return -ENODEV;
	}

	r = dsi_display_init_dispc(dsidev, dispc_channel);
	if (r)
		goto err_init_dispc;

	if (dsi->mode == OMAP_DSS_DSI_VIDEO_MODE) {
		switch (dsi->pix_fmt) {
		case OMAP_DSS_DSI_FMT_RGB888:
			data_type = MIPI_DSI_PACKED_PIXEL_STREAM_24;
			break;
		case OMAP_DSS_DSI_FMT_RGB666:
			data_type = MIPI_DSI_PIXEL_STREAM_3BYTE_18;
			break;
		case OMAP_DSS_DSI_FMT_RGB666_PACKED:
			data_type = MIPI_DSI_PACKED_PIXEL_STREAM_18;
			break;
		case OMAP_DSS_DSI_FMT_RGB565:
			data_type = MIPI_DSI_PACKED_PIXEL_STREAM_16;
			break;
		default:
			r = -EINVAL;
			goto err_pix_fmt;
		}

		dsi_if_enable(dsidev, false);
		dsi_vc_enable(dsidev, channel, false);

		/* MODE, 1 = video mode */
		REG_FLD_MOD(dsidev, DSI_VC_CTRL(channel), 1, 4, 4);

		word_count = DIV_ROUND_UP(dsi->timings.x_res * bpp, 8);

		dsi_vc_write_long_header(dsidev, channel, data_type,
				word_count, 0);

		dsi_vc_enable(dsidev, channel, true);
		dsi_if_enable(dsidev, true);
	}

	r = dss_mgr_enable(dispc_channel);
	if (r)
		goto err_mgr_enable;

	return 0;

err_mgr_enable:
	if (dsi->mode == OMAP_DSS_DSI_VIDEO_MODE) {
		dsi_if_enable(dsidev, false);
		dsi_vc_enable(dsidev, channel, false);
	}
err_pix_fmt:
	dsi_display_uninit_dispc(dsidev, dispc_channel);
err_init_dispc:
	return r;
}

static void dsi_disable_video_output(struct omap_dss_device *dssdev, int channel)
{
	struct platform_device *dsidev = dsi_get_dsidev_from_dssdev(dssdev);
	struct dsi_data *dsi = dsi_get_dsidrv_data(dsidev);
	enum omap_channel dispc_channel = dssdev->dispc_channel;

	if (dsi->mode == OMAP_DSS_DSI_VIDEO_MODE) {
		dsi_if_enable(dsidev, false);
		dsi_vc_enable(dsidev, channel, false);

		/* MODE, 0 = command mode */
		REG_FLD_MOD(dsidev, DSI_VC_CTRL(channel), 0, 4, 4);

		dsi_vc_enable(dsidev, channel, true);
		dsi_if_enable(dsidev, true);
	}

	dss_mgr_disable(dispc_channel);

	dsi_display_uninit_dispc(dsidev, dispc_channel);
}

static void dsi_update_screen_dispc(struct platform_device *dsidev)
{
	struct dsi_data *dsi = dsi_get_dsidrv_data(dsidev);
	enum omap_channel dispc_channel = dsi->output.dispc_channel;
	unsigned bytespp;
	unsigned bytespl;
	unsigned bytespf;
	unsigned total_len;
	unsigned packet_payload;
	unsigned packet_len;
	u32 l;
	int r;
	const unsigned channel = dsi->update_channel;
	const unsigned line_buf_size = dsi->line_buffer_size;
	u16 w = dsi->timings.x_res;
	u16 h = dsi->timings.y_res;

	DSSDBG("dsi_update_screen_dispc(%dx%d)\n", w, h);

	dsi_vc_config_source(dsidev, channel, DSI_VC_SOURCE_VP);

	bytespp	= dsi_get_pixel_size(dsi->pix_fmt) / 8;
	bytespl = w * bytespp;
	bytespf = bytespl * h;

	/* NOTE: packet_payload has to be equal to N * bytespl, where N is
	 * number of lines in a packet.  See errata about VP_CLK_RATIO */

	if (bytespf < line_buf_size)
		packet_payload = bytespf;
	else
		packet_payload = (line_buf_size) / bytespl * bytespl;

	packet_len = packet_payload + 1;	/* 1 byte for DCS cmd */
	total_len = (bytespf / packet_payload) * packet_len;

	if (bytespf % packet_payload)
		total_len += (bytespf % packet_payload) + 1;

	l = FLD_VAL(total_len, 23, 0); /* TE_SIZE */
	dsi_write_reg(dsidev, DSI_VC_TE(channel), l);

	dsi_vc_write_long_header(dsidev, channel, MIPI_DSI_DCS_LONG_WRITE,
		packet_len, 0);

	if (dsi->te_enabled)
		l = FLD_MOD(l, 1, 30, 30); /* TE_EN */
	else
		l = FLD_MOD(l, 1, 31, 31); /* TE_START */
	dsi_write_reg(dsidev, DSI_VC_TE(channel), l);

	/* We put SIDLEMODE to no-idle for the duration of the transfer,
	 * because DSS interrupts are not capable of waking up the CPU and the
	 * framedone interrupt could be delayed for quite a long time. I think
	 * the same goes for any DSS interrupts, but for some reason I have not
	 * seen the problem anywhere else than here.
	 */
	dispc_disable_sidle();

	dsi_perf_mark_start(dsidev);

	r = schedule_delayed_work(&dsi->framedone_timeout_work,
		msecs_to_jiffies(250));
	BUG_ON(r == 0);

	dss_mgr_set_timings(dispc_channel, &dsi->timings);

	dss_mgr_start_update(dispc_channel);

	if (dsi->te_enabled) {
		/* disable LP_RX_TO, so that we can receive TE.  Time to wait
		 * for TE is longer than the timer allows */
		REG_FLD_MOD(dsidev, DSI_TIMING2, 0, 15, 15); /* LP_RX_TO */

		dsi_vc_send_bta(dsidev, channel);

#ifdef DSI_CATCH_MISSING_TE
		mod_timer(&dsi->te_timer, jiffies + msecs_to_jiffies(250));
#endif
	}
}

#ifdef DSI_CATCH_MISSING_TE
static void dsi_te_timeout(unsigned long arg)
{
	DSSERR("TE not received for 250ms!\n");
}
#endif

static void dsi_handle_framedone(struct platform_device *dsidev, int error)
{
	struct dsi_data *dsi = dsi_get_dsidrv_data(dsidev);

	/* SIDLEMODE back to smart-idle */
	dispc_enable_sidle();

	if (dsi->te_enabled) {
		/* enable LP_RX_TO again after the TE */
		REG_FLD_MOD(dsidev, DSI_TIMING2, 1, 15, 15); /* LP_RX_TO */
	}

	dsi->framedone_callback(error, dsi->framedone_data);

	if (!error)
		dsi_perf_show(dsidev, "DISPC");
}

static void dsi_framedone_timeout_work_callback(struct work_struct *work)
{
	struct dsi_data *dsi = container_of(work, struct dsi_data,
			framedone_timeout_work.work);
	/* XXX While extremely unlikely, we could get FRAMEDONE interrupt after
	 * 250ms which would conflict with this timeout work. What should be
	 * done is first cancel the transfer on the HW, and then cancel the
	 * possibly scheduled framedone work. However, cancelling the transfer
	 * on the HW is buggy, and would probably require resetting the whole
	 * DSI */

	DSSERR("Framedone not received for 250ms!\n");

	dsi_handle_framedone(dsi->pdev, -ETIMEDOUT);
}

static void dsi_framedone_irq_callback(void *data)
{
	struct platform_device *dsidev = (struct platform_device *) data;
	struct dsi_data *dsi = dsi_get_dsidrv_data(dsidev);

	/* Note: We get FRAMEDONE when DISPC has finished sending pixels and
	 * turns itself off. However, DSI still has the pixels in its buffers,
	 * and is sending the data.
	 */

	cancel_delayed_work(&dsi->framedone_timeout_work);

	dsi_handle_framedone(dsidev, 0);
}

static int dsi_update(struct omap_dss_device *dssdev, int channel,
		void (*callback)(int, void *), void *data)
{
	struct platform_device *dsidev = dsi_get_dsidev_from_dssdev(dssdev);
	struct dsi_data *dsi = dsi_get_dsidrv_data(dsidev);
	u16 dw, dh;

	dsi_perf_mark_setup(dsidev);

	dsi->update_channel = channel;

	dsi->framedone_callback = callback;
	dsi->framedone_data = data;

	dw = dsi->timings.x_res;
	dh = dsi->timings.y_res;

#ifdef DSI_PERF_MEASURE
	dsi->update_bytes = dw * dh *
		dsi_get_pixel_size(dsi->pix_fmt) / 8;
#endif
	dsi_update_screen_dispc(dsidev);

	return 0;
}

/* Display funcs */

static int dsi_configure_dispc_clocks(struct platform_device *dsidev)
{
	struct dsi_data *dsi = dsi_get_dsidrv_data(dsidev);
	struct dispc_clock_info dispc_cinfo;
	int r;
	unsigned long fck;

	fck = dsi_get_pll_hsdiv_dispc_rate(dsidev);

	dispc_cinfo.lck_div = dsi->user_dispc_cinfo.lck_div;
	dispc_cinfo.pck_div = dsi->user_dispc_cinfo.pck_div;

	r = dispc_calc_clock_rates(fck, &dispc_cinfo);
	if (r) {
		DSSERR("Failed to calc dispc clocks\n");
		return r;
	}

	dsi->mgr_config.clock_info = dispc_cinfo;

	return 0;
}

static int dsi_display_init_dispc(struct platform_device *dsidev,
		enum omap_channel channel)
{
	struct dsi_data *dsi = dsi_get_dsidrv_data(dsidev);
	int r;

	dss_select_lcd_clk_source(channel, dsi->module_id == 0 ?
			OMAP_DSS_CLK_SRC_DSI_PLL_HSDIV_DISPC :
			OMAP_DSS_CLK_SRC_DSI2_PLL_HSDIV_DISPC);

	if (dsi->mode == OMAP_DSS_DSI_CMD_MODE) {
		r = dss_mgr_register_framedone_handler(channel,
				dsi_framedone_irq_callback, dsidev);
		if (r) {
			DSSERR("can't register FRAMEDONE handler\n");
			goto err;
		}

		dsi->mgr_config.stallmode = true;
		dsi->mgr_config.fifohandcheck = true;
	} else {
		dsi->mgr_config.stallmode = false;
		dsi->mgr_config.fifohandcheck = false;
	}

	/*
	 * override interlace, logic level and edge related parameters in
	 * omap_video_timings with default values
	 */
	dsi->timings.interlace = false;
	dsi->timings.hsync_level = OMAPDSS_SIG_ACTIVE_HIGH;
	dsi->timings.vsync_level = OMAPDSS_SIG_ACTIVE_HIGH;
	dsi->timings.data_pclk_edge = OMAPDSS_DRIVE_SIG_RISING_EDGE;
	dsi->timings.de_level = OMAPDSS_SIG_ACTIVE_HIGH;
	dsi->timings.sync_pclk_edge = OMAPDSS_DRIVE_SIG_FALLING_EDGE;

	dss_mgr_set_timings(channel, &dsi->timings);

	r = dsi_configure_dispc_clocks(dsidev);
	if (r)
		goto err1;

	dsi->mgr_config.io_pad_mode = DSS_IO_PAD_MODE_BYPASS;
	dsi->mgr_config.video_port_width =
			dsi_get_pixel_size(dsi->pix_fmt);
	dsi->mgr_config.lcden_sig_polarity = 0;

	dss_mgr_set_lcd_config(channel, &dsi->mgr_config);

	return 0;
err1:
	if (dsi->mode == OMAP_DSS_DSI_CMD_MODE)
		dss_mgr_unregister_framedone_handler(channel,
				dsi_framedone_irq_callback, dsidev);
err:
	dss_select_lcd_clk_source(channel, OMAP_DSS_CLK_SRC_FCK);
	return r;
}

static void dsi_display_uninit_dispc(struct platform_device *dsidev,
		enum omap_channel channel)
{
	struct dsi_data *dsi = dsi_get_dsidrv_data(dsidev);

	if (dsi->mode == OMAP_DSS_DSI_CMD_MODE)
		dss_mgr_unregister_framedone_handler(channel,
				dsi_framedone_irq_callback, dsidev);

	dss_select_lcd_clk_source(channel, OMAP_DSS_CLK_SRC_FCK);
}

static int dsi_configure_dsi_clocks(struct platform_device *dsidev)
{
	struct dsi_data *dsi = dsi_get_dsidrv_data(dsidev);
	struct dss_pll_clock_info cinfo;
	int r;

	cinfo = dsi->user_dsi_cinfo;

	r = dss_pll_set_config(&dsi->pll, &cinfo);
	if (r) {
		DSSERR("Failed to set dsi clocks\n");
		return r;
	}

	return 0;
}

static int dsi_display_init_dsi(struct platform_device *dsidev)
{
	struct dsi_data *dsi = dsi_get_dsidrv_data(dsidev);
	int r;

	r = dss_pll_enable(&dsi->pll);
	if (r)
		goto err0;

	r = dsi_configure_dsi_clocks(dsidev);
	if (r)
		goto err1;

	dss_select_dsi_clk_source(dsi->module_id, dsi->module_id == 0 ?
			OMAP_DSS_CLK_SRC_DSI_PLL_HSDIV_DSI :
			OMAP_DSS_CLK_SRC_DSI2_PLL_HSDIV_DSI);

	DSSDBG("PLL OK\n");

	r = dsi_cio_init(dsidev);
	if (r)
		goto err2;

	_dsi_print_reset_status(dsidev);

	dsi_proto_timings(dsidev);
	dsi_set_lp_clk_divisor(dsidev);

	if (1)
		_dsi_print_reset_status(dsidev);

	r = dsi_proto_config(dsidev);
	if (r)
		goto err3;

	/* enable interface */
	dsi_vc_enable(dsidev, 0, 1);
	dsi_vc_enable(dsidev, 1, 1);
	dsi_vc_enable(dsidev, 2, 1);
	dsi_vc_enable(dsidev, 3, 1);
	dsi_if_enable(dsidev, 1);
	dsi_force_tx_stop_mode_io(dsidev);

	return 0;
err3:
	dsi_cio_uninit(dsidev);
err2:
	dss_select_dsi_clk_source(dsi->module_id, OMAP_DSS_CLK_SRC_FCK);
err1:
	dss_pll_disable(&dsi->pll);
err0:
	return r;
}

static void dsi_display_uninit_dsi(struct platform_device *dsidev,
		bool disconnect_lanes, bool enter_ulps)
{
	struct dsi_data *dsi = dsi_get_dsidrv_data(dsidev);

	if (enter_ulps && !dsi->ulps_enabled)
		dsi_enter_ulps(dsidev);

	/* disable interface */
	dsi_if_enable(dsidev, 0);
	dsi_vc_enable(dsidev, 0, 0);
	dsi_vc_enable(dsidev, 1, 0);
	dsi_vc_enable(dsidev, 2, 0);
	dsi_vc_enable(dsidev, 3, 0);

	dss_select_dsi_clk_source(dsi->module_id, OMAP_DSS_CLK_SRC_FCK);
	dsi_cio_uninit(dsidev);
	dsi_pll_uninit(dsidev, disconnect_lanes);
}

static int dsi_display_enable(struct omap_dss_device *dssdev)
{
	struct platform_device *dsidev = dsi_get_dsidev_from_dssdev(dssdev);
	struct dsi_data *dsi = dsi_get_dsidrv_data(dsidev);
	int r = 0;

	DSSDBG("dsi_display_enable\n");

	WARN_ON(!dsi_bus_is_locked(dsidev));

	mutex_lock(&dsi->lock);

	r = dsi_runtime_get(dsidev);
	if (r)
		goto err_get_dsi;

	_dsi_initialize_irq(dsidev);

	r = dsi_display_init_dsi(dsidev);
	if (r)
		goto err_init_dsi;

	mutex_unlock(&dsi->lock);

	return 0;

err_init_dsi:
	dsi_runtime_put(dsidev);
err_get_dsi:
	mutex_unlock(&dsi->lock);
	DSSDBG("dsi_display_enable FAILED\n");
	return r;
}

static void dsi_display_disable(struct omap_dss_device *dssdev,
		bool disconnect_lanes, bool enter_ulps)
{
	struct platform_device *dsidev = dsi_get_dsidev_from_dssdev(dssdev);
	struct dsi_data *dsi = dsi_get_dsidrv_data(dsidev);

	DSSDBG("dsi_display_disable\n");

	WARN_ON(!dsi_bus_is_locked(dsidev));

	mutex_lock(&dsi->lock);

	dsi_sync_vc(dsidev, 0);
	dsi_sync_vc(dsidev, 1);
	dsi_sync_vc(dsidev, 2);
	dsi_sync_vc(dsidev, 3);

	dsi_display_uninit_dsi(dsidev, disconnect_lanes, enter_ulps);

	dsi_runtime_put(dsidev);

	mutex_unlock(&dsi->lock);
}

static int dsi_enable_te(struct omap_dss_device *dssdev, bool enable)
{
	struct platform_device *dsidev = dsi_get_dsidev_from_dssdev(dssdev);
	struct dsi_data *dsi = dsi_get_dsidrv_data(dsidev);

	dsi->te_enabled = enable;
	return 0;
}

#ifdef PRINT_VERBOSE_VM_TIMINGS
static void print_dsi_vm(const char *str,
		const struct omap_dss_dsi_videomode_timings *t)
{
	unsigned long byteclk = t->hsclk / 4;
	int bl, wc, pps, tot;

	wc = DIV_ROUND_UP(t->hact * t->bitspp, 8);
	pps = DIV_ROUND_UP(wc + 6, t->ndl); /* pixel packet size */
	bl = t->hss + t->hsa + t->hse + t->hbp + t->hfp;
	tot = bl + pps;

#define TO_DSI_T(x) ((u32)div64_u64((u64)x * 1000000000llu, byteclk))

	pr_debug("%s bck %lu, %u/%u/%u/%u/%u/%u = %u+%u = %u, "
			"%u/%u/%u/%u/%u/%u = %u + %u = %u\n",
			str,
			byteclk,
			t->hss, t->hsa, t->hse, t->hbp, pps, t->hfp,
			bl, pps, tot,
			TO_DSI_T(t->hss),
			TO_DSI_T(t->hsa),
			TO_DSI_T(t->hse),
			TO_DSI_T(t->hbp),
			TO_DSI_T(pps),
			TO_DSI_T(t->hfp),

			TO_DSI_T(bl),
			TO_DSI_T(pps),

			TO_DSI_T(tot));
#undef TO_DSI_T
}

static void print_dispc_vm(const char *str, const struct omap_video_timings *t)
{
	unsigned long pck = t->pixelclock;
	int hact, bl, tot;

	hact = t->x_res;
	bl = t->hsw + t->hbp + t->hfp;
	tot = hact + bl;

#define TO_DISPC_T(x) ((u32)div64_u64((u64)x * 1000000000llu, pck))

	pr_debug("%s pck %lu, %u/%u/%u/%u = %u+%u = %u, "
			"%u/%u/%u/%u = %u + %u = %u\n",
			str,
			pck,
			t->hsw, t->hbp, hact, t->hfp,
			bl, hact, tot,
			TO_DISPC_T(t->hsw),
			TO_DISPC_T(t->hbp),
			TO_DISPC_T(hact),
			TO_DISPC_T(t->hfp),
			TO_DISPC_T(bl),
			TO_DISPC_T(hact),
			TO_DISPC_T(tot));
#undef TO_DISPC_T
}

/* note: this is not quite accurate */
static void print_dsi_dispc_vm(const char *str,
		const struct omap_dss_dsi_videomode_timings *t)
{
	struct omap_video_timings vm = { 0 };
	unsigned long byteclk = t->hsclk / 4;
	unsigned long pck;
	u64 dsi_tput;
	int dsi_hact, dsi_htot;

	dsi_tput = (u64)byteclk * t->ndl * 8;
	pck = (u32)div64_u64(dsi_tput, t->bitspp);
	dsi_hact = DIV_ROUND_UP(DIV_ROUND_UP(t->hact * t->bitspp, 8) + 6, t->ndl);
	dsi_htot = t->hss + t->hsa + t->hse + t->hbp + dsi_hact + t->hfp;

	vm.pixelclock = pck;
	vm.hsw = div64_u64((u64)(t->hsa + t->hse) * pck, byteclk);
	vm.hbp = div64_u64((u64)t->hbp * pck, byteclk);
	vm.hfp = div64_u64((u64)t->hfp * pck, byteclk);
	vm.x_res = t->hact;

	print_dispc_vm(str, &vm);
}
#endif /* PRINT_VERBOSE_VM_TIMINGS */

static bool dsi_cm_calc_dispc_cb(int lckd, int pckd, unsigned long lck,
		unsigned long pck, void *data)
{
	struct dsi_clk_calc_ctx *ctx = data;
	struct omap_video_timings *t = &ctx->dispc_vm;

	ctx->dispc_cinfo.lck_div = lckd;
	ctx->dispc_cinfo.pck_div = pckd;
	ctx->dispc_cinfo.lck = lck;
	ctx->dispc_cinfo.pck = pck;

	*t = *ctx->config->timings;
	t->pixelclock = pck;
	t->x_res = ctx->config->timings->x_res;
	t->y_res = ctx->config->timings->y_res;
	t->hsw = t->hfp = t->hbp = t->vsw = 1;
	t->vfp = t->vbp = 0;

	return true;
}

static bool dsi_cm_calc_hsdiv_cb(int m_dispc, unsigned long dispc,
		void *data)
{
	struct dsi_clk_calc_ctx *ctx = data;

	ctx->dsi_cinfo.mX[HSDIV_DISPC] = m_dispc;
	ctx->dsi_cinfo.clkout[HSDIV_DISPC] = dispc;

	return dispc_div_calc(dispc, ctx->req_pck_min, ctx->req_pck_max,
			dsi_cm_calc_dispc_cb, ctx);
}

static bool dsi_cm_calc_pll_cb(int n, int m, unsigned long fint,
		unsigned long clkdco, void *data)
{
	struct dsi_clk_calc_ctx *ctx = data;

	ctx->dsi_cinfo.n = n;
	ctx->dsi_cinfo.m = m;
	ctx->dsi_cinfo.fint = fint;
	ctx->dsi_cinfo.clkdco = clkdco;

	return dss_pll_hsdiv_calc(ctx->pll, clkdco, ctx->req_pck_min,
			dss_feat_get_param_max(FEAT_PARAM_DSS_FCK),
			dsi_cm_calc_hsdiv_cb, ctx);
}

static bool dsi_cm_calc(struct dsi_data *dsi,
		const struct omap_dss_dsi_config *cfg,
		struct dsi_clk_calc_ctx *ctx)
{
	unsigned long clkin;
	int bitspp, ndl;
	unsigned long pll_min, pll_max;
	unsigned long pck, txbyteclk;

	clkin = clk_get_rate(dsi->pll.clkin);
	bitspp = dsi_get_pixel_size(cfg->pixel_format);
	ndl = dsi->num_lanes_used - 1;

	/*
	 * Here we should calculate minimum txbyteclk to be able to send the
	 * frame in time, and also to handle TE. That's not very simple, though,
	 * especially as we go to LP between each pixel packet due to HW
	 * "feature". So let's just estimate very roughly and multiply by 1.5.
	 */
	pck = cfg->timings->pixelclock;
	pck = pck * 3 / 2;
	txbyteclk = pck * bitspp / 8 / ndl;

	memset(ctx, 0, sizeof(*ctx));
	ctx->dsidev = dsi->pdev;
	ctx->pll = &dsi->pll;
	ctx->config = cfg;
	ctx->req_pck_min = pck;
	ctx->req_pck_nom = pck;
	ctx->req_pck_max = pck * 3 / 2;

	pll_min = max(cfg->hs_clk_min * 4, txbyteclk * 4 * 4);
	pll_max = cfg->hs_clk_max * 4;

	return dss_pll_calc(ctx->pll, clkin,
			pll_min, pll_max,
			dsi_cm_calc_pll_cb, ctx);
}

static bool dsi_vm_calc_blanking(struct dsi_clk_calc_ctx *ctx)
{
	struct dsi_data *dsi = dsi_get_dsidrv_data(ctx->dsidev);
	const struct omap_dss_dsi_config *cfg = ctx->config;
	int bitspp = dsi_get_pixel_size(cfg->pixel_format);
	int ndl = dsi->num_lanes_used - 1;
	unsigned long hsclk = ctx->dsi_cinfo.clkdco / 4;
	unsigned long byteclk = hsclk / 4;

	unsigned long dispc_pck, req_pck_min, req_pck_nom, req_pck_max;
	int xres;
	int panel_htot, panel_hbl; /* pixels */
	int dispc_htot, dispc_hbl; /* pixels */
	int dsi_htot, dsi_hact, dsi_hbl, hss, hse; /* byteclks */
	int hfp, hsa, hbp;
	const struct omap_video_timings *req_vm;
	struct omap_video_timings *dispc_vm;
	struct omap_dss_dsi_videomode_timings *dsi_vm;
	u64 dsi_tput, dispc_tput;

	dsi_tput = (u64)byteclk * ndl * 8;

	req_vm = cfg->timings;
	req_pck_min = ctx->req_pck_min;
	req_pck_max = ctx->req_pck_max;
	req_pck_nom = ctx->req_pck_nom;

	dispc_pck = ctx->dispc_cinfo.pck;
	dispc_tput = (u64)dispc_pck * bitspp;

	xres = req_vm->x_res;

	panel_hbl = req_vm->hfp + req_vm->hbp + req_vm->hsw;
	panel_htot = xres + panel_hbl;

	dsi_hact = DIV_ROUND_UP(DIV_ROUND_UP(xres * bitspp, 8) + 6, ndl);

	/*
	 * When there are no line buffers, DISPC and DSI must have the
	 * same tput. Otherwise DISPC tput needs to be higher than DSI's.
	 */
	if (dsi->line_buffer_size < xres * bitspp / 8) {
		if (dispc_tput != dsi_tput)
			return false;
	} else {
		if (dispc_tput < dsi_tput)
			return false;
	}

	/* DSI tput must be over the min requirement */
	if (dsi_tput < (u64)bitspp * req_pck_min)
		return false;

	/* When non-burst mode, DSI tput must be below max requirement. */
	if (cfg->trans_mode != OMAP_DSS_DSI_BURST_MODE) {
		if (dsi_tput > (u64)bitspp * req_pck_max)
			return false;
	}

	hss = DIV_ROUND_UP(4, ndl);

	if (cfg->trans_mode == OMAP_DSS_DSI_PULSE_MODE) {
		if (ndl == 3 && req_vm->hsw == 0)
			hse = 1;
		else
			hse = DIV_ROUND_UP(4, ndl);
	} else {
		hse = 0;
	}

	/* DSI htot to match the panel's nominal pck */
	dsi_htot = div64_u64((u64)panel_htot * byteclk, req_pck_nom);

	/* fail if there would be no time for blanking */
	if (dsi_htot < hss + hse + dsi_hact)
		return false;

	/* total DSI blanking needed to achieve panel's TL */
	dsi_hbl = dsi_htot - dsi_hact;

	/* DISPC htot to match the DSI TL */
	dispc_htot = div64_u64((u64)dsi_htot * dispc_pck, byteclk);

	/* verify that the DSI and DISPC TLs are the same */
	if ((u64)dsi_htot * dispc_pck != (u64)dispc_htot * byteclk)
		return false;

	dispc_hbl = dispc_htot - xres;

	/* setup DSI videomode */

	dsi_vm = &ctx->dsi_vm;
	memset(dsi_vm, 0, sizeof(*dsi_vm));

	dsi_vm->hsclk = hsclk;

	dsi_vm->ndl = ndl;
	dsi_vm->bitspp = bitspp;

	if (cfg->trans_mode != OMAP_DSS_DSI_PULSE_MODE) {
		hsa = 0;
	} else if (ndl == 3 && req_vm->hsw == 0) {
		hsa = 0;
	} else {
		hsa = div64_u64((u64)req_vm->hsw * byteclk, req_pck_nom);
		hsa = max(hsa - hse, 1);
	}

	hbp = div64_u64((u64)req_vm->hbp * byteclk, req_pck_nom);
	hbp = max(hbp, 1);

	hfp = dsi_hbl - (hss + hsa + hse + hbp);
	if (hfp < 1) {
		int t;
		/* we need to take cycles from hbp */

		t = 1 - hfp;
		hbp = max(hbp - t, 1);
		hfp = dsi_hbl - (hss + hsa + hse + hbp);

		if (hfp < 1 && hsa > 0) {
			/* we need to take cycles from hsa */
			t = 1 - hfp;
			hsa = max(hsa - t, 1);
			hfp = dsi_hbl - (hss + hsa + hse + hbp);
		}
	}

	if (hfp < 1)
		return false;

	dsi_vm->hss = hss;
	dsi_vm->hsa = hsa;
	dsi_vm->hse = hse;
	dsi_vm->hbp = hbp;
	dsi_vm->hact = xres;
	dsi_vm->hfp = hfp;

	dsi_vm->vsa = req_vm->vsw;
	dsi_vm->vbp = req_vm->vbp;
	dsi_vm->vact = req_vm->y_res;
	dsi_vm->vfp = req_vm->vfp;

	dsi_vm->trans_mode = cfg->trans_mode;

	dsi_vm->blanking_mode = 0;
	dsi_vm->hsa_blanking_mode = 1;
	dsi_vm->hfp_blanking_mode = 1;
	dsi_vm->hbp_blanking_mode = 1;

	dsi_vm->ddr_clk_always_on = cfg->ddr_clk_always_on;
	dsi_vm->window_sync = 4;

	/* setup DISPC videomode */

	dispc_vm = &ctx->dispc_vm;
	*dispc_vm = *req_vm;
	dispc_vm->pixelclock = dispc_pck;

	if (cfg->trans_mode == OMAP_DSS_DSI_PULSE_MODE) {
		hsa = div64_u64((u64)req_vm->hsw * dispc_pck,
				req_pck_nom);
		hsa = max(hsa, 1);
	} else {
		hsa = 1;
	}

	hbp = div64_u64((u64)req_vm->hbp * dispc_pck, req_pck_nom);
	hbp = max(hbp, 1);

	hfp = dispc_hbl - hsa - hbp;
	if (hfp < 1) {
		int t;
		/* we need to take cycles from hbp */

		t = 1 - hfp;
		hbp = max(hbp - t, 1);
		hfp = dispc_hbl - hsa - hbp;

		if (hfp < 1) {
			/* we need to take cycles from hsa */
			t = 1 - hfp;
			hsa = max(hsa - t, 1);
			hfp = dispc_hbl - hsa - hbp;
		}
	}

	if (hfp < 1)
		return false;

	dispc_vm->hfp = hfp;
	dispc_vm->hsw = hsa;
	dispc_vm->hbp = hbp;

	return true;
}


static bool dsi_vm_calc_dispc_cb(int lckd, int pckd, unsigned long lck,
		unsigned long pck, void *data)
{
	struct dsi_clk_calc_ctx *ctx = data;

	ctx->dispc_cinfo.lck_div = lckd;
	ctx->dispc_cinfo.pck_div = pckd;
	ctx->dispc_cinfo.lck = lck;
	ctx->dispc_cinfo.pck = pck;

	if (dsi_vm_calc_blanking(ctx) == false)
		return false;

#ifdef PRINT_VERBOSE_VM_TIMINGS
	print_dispc_vm("dispc", &ctx->dispc_vm);
	print_dsi_vm("dsi  ", &ctx->dsi_vm);
	print_dispc_vm("req  ", ctx->config->timings);
	print_dsi_dispc_vm("act  ", &ctx->dsi_vm);
#endif

	return true;
}

static bool dsi_vm_calc_hsdiv_cb(int m_dispc, unsigned long dispc,
		void *data)
{
	struct dsi_clk_calc_ctx *ctx = data;
	unsigned long pck_max;

	ctx->dsi_cinfo.mX[HSDIV_DISPC] = m_dispc;
	ctx->dsi_cinfo.clkout[HSDIV_DISPC] = dispc;

	/*
	 * In burst mode we can let the dispc pck be arbitrarily high, but it
	 * limits our scaling abilities. So for now, don't aim too high.
	 */

	if (ctx->config->trans_mode == OMAP_DSS_DSI_BURST_MODE)
		pck_max = ctx->req_pck_max + 10000000;
	else
		pck_max = ctx->req_pck_max;

	return dispc_div_calc(dispc, ctx->req_pck_min, pck_max,
			dsi_vm_calc_dispc_cb, ctx);
}

static bool dsi_vm_calc_pll_cb(int n, int m, unsigned long fint,
		unsigned long clkdco, void *data)
{
	struct dsi_clk_calc_ctx *ctx = data;

	ctx->dsi_cinfo.n = n;
	ctx->dsi_cinfo.m = m;
	ctx->dsi_cinfo.fint = fint;
	ctx->dsi_cinfo.clkdco = clkdco;

	return dss_pll_hsdiv_calc(ctx->pll, clkdco, ctx->req_pck_min,
			dss_feat_get_param_max(FEAT_PARAM_DSS_FCK),
			dsi_vm_calc_hsdiv_cb, ctx);
}

static bool dsi_vm_calc(struct dsi_data *dsi,
		const struct omap_dss_dsi_config *cfg,
		struct dsi_clk_calc_ctx *ctx)
{
	const struct omap_video_timings *t = cfg->timings;
	unsigned long clkin;
	unsigned long pll_min;
	unsigned long pll_max;
	int ndl = dsi->num_lanes_used - 1;
	int bitspp = dsi_get_pixel_size(cfg->pixel_format);
	unsigned long byteclk_min;

	clkin = clk_get_rate(dsi->pll.clkin);

	memset(ctx, 0, sizeof(*ctx));
	ctx->dsidev = dsi->pdev;
	ctx->pll = &dsi->pll;
	ctx->config = cfg;

	/* these limits should come from the panel driver */
	ctx->req_pck_min = t->pixelclock - 1000;
	ctx->req_pck_nom = t->pixelclock;
	ctx->req_pck_max = t->pixelclock + 1000;

	byteclk_min = div64_u64((u64)ctx->req_pck_min * bitspp, ndl * 8);
	pll_min = max(cfg->hs_clk_min * 4, byteclk_min * 4 * 4);

	if (cfg->trans_mode == OMAP_DSS_DSI_BURST_MODE) {
		pll_max = cfg->hs_clk_max * 4;
	} else {
		unsigned long byteclk_max;
		byteclk_max = div64_u64((u64)ctx->req_pck_max * bitspp,
				ndl * 8);

		pll_max = byteclk_max * 4 * 4;
	}

	return dss_pll_calc(ctx->pll, clkin,
			pll_min, pll_max,
			dsi_vm_calc_pll_cb, ctx);
}

static int dsi_set_config(struct omap_dss_device *dssdev,
		const struct omap_dss_dsi_config *config)
{
	struct platform_device *dsidev = dsi_get_dsidev_from_dssdev(dssdev);
	struct dsi_data *dsi = dsi_get_dsidrv_data(dsidev);
	struct dsi_clk_calc_ctx ctx;
	bool ok;
	int r;

	mutex_lock(&dsi->lock);

	dsi->pix_fmt = config->pixel_format;
	dsi->mode = config->mode;

	if (config->mode == OMAP_DSS_DSI_VIDEO_MODE)
		ok = dsi_vm_calc(dsi, config, &ctx);
	else
		ok = dsi_cm_calc(dsi, config, &ctx);

	if (!ok) {
		DSSERR("failed to find suitable DSI clock settings\n");
		r = -EINVAL;
		goto err;
	}

	dsi_pll_calc_dsi_fck(&ctx.dsi_cinfo);

	r = dsi_lp_clock_calc(ctx.dsi_cinfo.clkout[HSDIV_DSI],
		config->lp_clk_min, config->lp_clk_max, &dsi->user_lp_cinfo);
	if (r) {
		DSSERR("failed to find suitable DSI LP clock settings\n");
		goto err;
	}

	dsi->user_dsi_cinfo = ctx.dsi_cinfo;
	dsi->user_dispc_cinfo = ctx.dispc_cinfo;

	dsi->timings = ctx.dispc_vm;
	dsi->vm_timings = ctx.dsi_vm;

	mutex_unlock(&dsi->lock);

	return 0;
err:
	mutex_unlock(&dsi->lock);

	return r;
}

/*
 * Return a hardcoded channel for the DSI output. This should work for
 * current use cases, but this can be later expanded to either resolve
 * the channel in some more dynamic manner, or get the channel as a user
 * parameter.
 */
static enum omap_channel dsi_get_channel(int module_id)
{
	switch (omapdss_get_version()) {
	case OMAPDSS_VER_OMAP24xx:
	case OMAPDSS_VER_AM43xx:
		DSSWARN("DSI not supported\n");
		return OMAP_DSS_CHANNEL_LCD;

	case OMAPDSS_VER_OMAP34xx_ES1:
	case OMAPDSS_VER_OMAP34xx_ES3:
	case OMAPDSS_VER_OMAP3630:
	case OMAPDSS_VER_AM35xx:
		return OMAP_DSS_CHANNEL_LCD;

	case OMAPDSS_VER_OMAP4430_ES1:
	case OMAPDSS_VER_OMAP4430_ES2:
	case OMAPDSS_VER_OMAP4:
		switch (module_id) {
		case 0:
			return OMAP_DSS_CHANNEL_LCD;
		case 1:
			return OMAP_DSS_CHANNEL_LCD2;
		default:
			DSSWARN("unsupported module id\n");
			return OMAP_DSS_CHANNEL_LCD;
		}

	case OMAPDSS_VER_OMAP5:
		switch (module_id) {
		case 0:
			return OMAP_DSS_CHANNEL_LCD;
		case 1:
			return OMAP_DSS_CHANNEL_LCD3;
		default:
			DSSWARN("unsupported module id\n");
			return OMAP_DSS_CHANNEL_LCD;
		}

	default:
		DSSWARN("unsupported DSS version\n");
		return OMAP_DSS_CHANNEL_LCD;
	}
}

static int dsi_request_vc(struct omap_dss_device *dssdev, int *channel)
{
	struct platform_device *dsidev = dsi_get_dsidev_from_dssdev(dssdev);
	struct dsi_data *dsi = dsi_get_dsidrv_data(dsidev);
	int i;

	for (i = 0; i < ARRAY_SIZE(dsi->vc); i++) {
		if (!dsi->vc[i].dssdev) {
			dsi->vc[i].dssdev = dssdev;
			*channel = i;
			return 0;
		}
	}

	DSSERR("cannot get VC for display %s", dssdev->name);
	return -ENOSPC;
}

static int dsi_set_vc_id(struct omap_dss_device *dssdev, int channel, int vc_id)
{
	struct platform_device *dsidev = dsi_get_dsidev_from_dssdev(dssdev);
	struct dsi_data *dsi = dsi_get_dsidrv_data(dsidev);

	if (vc_id < 0 || vc_id > 3) {
		DSSERR("VC ID out of range\n");
		return -EINVAL;
	}

	if (channel < 0 || channel > 3) {
		DSSERR("Virtual Channel out of range\n");
		return -EINVAL;
	}

	if (dsi->vc[channel].dssdev != dssdev) {
		DSSERR("Virtual Channel not allocated to display %s\n",
			dssdev->name);
		return -EINVAL;
	}

	dsi->vc[channel].vc_id = vc_id;

	return 0;
}

static void dsi_release_vc(struct omap_dss_device *dssdev, int channel)
{
	struct platform_device *dsidev = dsi_get_dsidev_from_dssdev(dssdev);
	struct dsi_data *dsi = dsi_get_dsidrv_data(dsidev);

	if ((channel >= 0 && channel <= 3) &&
		dsi->vc[channel].dssdev == dssdev) {
		dsi->vc[channel].dssdev = NULL;
		dsi->vc[channel].vc_id = 0;
	}
}


static int dsi_get_clocks(struct platform_device *dsidev)
{
	struct dsi_data *dsi = dsi_get_dsidrv_data(dsidev);
	struct clk *clk;

	clk = devm_clk_get(&dsidev->dev, "fck");
	if (IS_ERR(clk)) {
		DSSERR("can't get fck\n");
		return PTR_ERR(clk);
	}

	dsi->dss_clk = clk;

	return 0;
}

static int dsi_connect(struct omap_dss_device *dssdev,
		struct omap_dss_device *dst)
{
	struct platform_device *dsidev = dsi_get_dsidev_from_dssdev(dssdev);
	enum omap_channel dispc_channel = dssdev->dispc_channel;
	int r;

	r = dsi_regulator_init(dsidev);
	if (r)
		return r;

	r = dss_mgr_connect(dispc_channel, dssdev);
	if (r)
		return r;

	r = omapdss_output_set_device(dssdev, dst);
	if (r) {
		DSSERR("failed to connect output to new device: %s\n",
				dssdev->name);
		dss_mgr_disconnect(dispc_channel, dssdev);
		return r;
	}

	return 0;
}

static void dsi_disconnect(struct omap_dss_device *dssdev,
		struct omap_dss_device *dst)
{
	enum omap_channel dispc_channel = dssdev->dispc_channel;

	WARN_ON(dst != dssdev->dst);

	if (dst != dssdev->dst)
		return;

	omapdss_output_unset_device(dssdev);

	dss_mgr_disconnect(dispc_channel, dssdev);
}

static const struct omapdss_dsi_ops dsi_ops = {
	.connect = dsi_connect,
	.disconnect = dsi_disconnect,

	.bus_lock = dsi_bus_lock,
	.bus_unlock = dsi_bus_unlock,

	.enable = dsi_display_enable,
	.disable = dsi_display_disable,

	.enable_hs = dsi_vc_enable_hs,

	.configure_pins = dsi_configure_pins,
	.set_config = dsi_set_config,

	.enable_video_output = dsi_enable_video_output,
	.disable_video_output = dsi_disable_video_output,

	.update = dsi_update,

	.enable_te = dsi_enable_te,

	.request_vc = dsi_request_vc,
	.set_vc_id = dsi_set_vc_id,
	.release_vc = dsi_release_vc,

	.dcs_write = dsi_vc_dcs_write,
	.dcs_write_nosync = dsi_vc_dcs_write_nosync,
	.dcs_read = dsi_vc_dcs_read,

	.gen_write = dsi_vc_generic_write,
	.gen_write_nosync = dsi_vc_generic_write_nosync,
	.gen_read = dsi_vc_generic_read,

	.bta_sync = dsi_vc_send_bta_sync,

	.set_max_rx_packet_size = dsi_vc_set_max_rx_packet_size,
};

static void dsi_init_output(struct platform_device *dsidev)
{
	struct dsi_data *dsi = dsi_get_dsidrv_data(dsidev);
	struct omap_dss_device *out = &dsi->output;

	out->dev = &dsidev->dev;
	out->id = dsi->module_id == 0 ?
			OMAP_DSS_OUTPUT_DSI1 : OMAP_DSS_OUTPUT_DSI2;

	out->output_type = OMAP_DISPLAY_TYPE_DSI;
	out->name = dsi->module_id == 0 ? "dsi.0" : "dsi.1";
	out->dispc_channel = dsi_get_channel(dsi->module_id);
	out->ops.dsi = &dsi_ops;
	out->owner = THIS_MODULE;

	omapdss_register_output(out);
}

static void dsi_uninit_output(struct platform_device *dsidev)
{
	struct dsi_data *dsi = dsi_get_dsidrv_data(dsidev);
	struct omap_dss_device *out = &dsi->output;

	omapdss_unregister_output(out);
}

static int dsi_probe_of(struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;
	struct dsi_data *dsi = dsi_get_dsidrv_data(pdev);
	struct property *prop;
	u32 lane_arr[10];
	int len, num_pins;
	int r, i;
	struct device_node *ep;
	struct omap_dsi_pin_config pin_cfg;

	ep = omapdss_of_get_first_endpoint(node);
	if (!ep)
		return 0;

	prop = of_find_property(ep, "lanes", &len);
	if (prop == NULL) {
		dev_err(&pdev->dev, "failed to find lane data\n");
		r = -EINVAL;
		goto err;
	}

	num_pins = len / sizeof(u32);

	if (num_pins < 4 || num_pins % 2 != 0 ||
		num_pins > dsi->num_lanes_supported * 2) {
		dev_err(&pdev->dev, "bad number of lanes\n");
		r = -EINVAL;
		goto err;
	}

	r = of_property_read_u32_array(ep, "lanes", lane_arr, num_pins);
	if (r) {
		dev_err(&pdev->dev, "failed to read lane data\n");
		goto err;
	}

	pin_cfg.num_pins = num_pins;
	for (i = 0; i < num_pins; ++i)
		pin_cfg.pins[i] = (int)lane_arr[i];

	r = dsi_configure_pins(&dsi->output, &pin_cfg);
	if (r) {
		dev_err(&pdev->dev, "failed to configure pins");
		goto err;
	}

	of_node_put(ep);

	return 0;

err:
	of_node_put(ep);
	return r;
}

static const struct dss_pll_ops dsi_pll_ops = {
	.enable = dsi_pll_enable,
	.disable = dsi_pll_disable,
	.set_config = dss_pll_write_config_type_a,
};

static const struct dss_pll_hw dss_omap3_dsi_pll_hw = {
	.n_max = (1 << 7) - 1,
	.m_max = (1 << 11) - 1,
	.mX_max = (1 << 4) - 1,
	.fint_min = 750000,
	.fint_max = 2100000,
	.clkdco_low = 1000000000,
	.clkdco_max = 1800000000,

	.n_msb = 7,
	.n_lsb = 1,
	.m_msb = 18,
	.m_lsb = 8,

	.mX_msb[0] = 22,
	.mX_lsb[0] = 19,
	.mX_msb[1] = 26,
	.mX_lsb[1] = 23,

	.has_stopmode = true,
	.has_freqsel = true,
	.has_selfreqdco = false,
	.has_refsel = false,
};

static const struct dss_pll_hw dss_omap4_dsi_pll_hw = {
	.n_max = (1 << 8) - 1,
	.m_max = (1 << 12) - 1,
	.mX_max = (1 << 5) - 1,
	.fint_min = 500000,
	.fint_max = 2500000,
	.clkdco_low = 1000000000,
	.clkdco_max = 1800000000,

	.n_msb = 8,
	.n_lsb = 1,
	.m_msb = 20,
	.m_lsb = 9,

	.mX_msb[0] = 25,
	.mX_lsb[0] = 21,
	.mX_msb[1] = 30,
	.mX_lsb[1] = 26,

	.has_stopmode = true,
	.has_freqsel = false,
	.has_selfreqdco = false,
	.has_refsel = false,
};

static const struct dss_pll_hw dss_omap5_dsi_pll_hw = {
	.n_max = (1 << 8) - 1,
	.m_max = (1 << 12) - 1,
	.mX_max = (1 << 5) - 1,
	.fint_min = 150000,
	.fint_max = 52000000,
	.clkdco_low = 1000000000,
	.clkdco_max = 1800000000,

	.n_msb = 8,
	.n_lsb = 1,
	.m_msb = 20,
	.m_lsb = 9,

	.mX_msb[0] = 25,
	.mX_lsb[0] = 21,
	.mX_msb[1] = 30,
	.mX_lsb[1] = 26,

	.has_stopmode = true,
	.has_freqsel = false,
	.has_selfreqdco = true,
	.has_refsel = true,
};

static int dsi_init_pll_data(struct platform_device *dsidev)
{
	struct dsi_data *dsi = dsi_get_dsidrv_data(dsidev);
	struct dss_pll *pll = &dsi->pll;
	struct clk *clk;
	int r;

	clk = devm_clk_get(&dsidev->dev, "sys_clk");
	if (IS_ERR(clk)) {
		DSSERR("can't get sys_clk\n");
		return PTR_ERR(clk);
	}

	pll->name = dsi->module_id == 0 ? "dsi0" : "dsi1";
	pll->id = dsi->module_id == 0 ? DSS_PLL_DSI1 : DSS_PLL_DSI2;
	pll->clkin = clk;
	pll->base = dsi->pll_base;

	switch (omapdss_get_version()) {
	case OMAPDSS_VER_OMAP34xx_ES1:
	case OMAPDSS_VER_OMAP34xx_ES3:
	case OMAPDSS_VER_OMAP3630:
	case OMAPDSS_VER_AM35xx:
		pll->hw = &dss_omap3_dsi_pll_hw;
		break;

	case OMAPDSS_VER_OMAP4430_ES1:
	case OMAPDSS_VER_OMAP4430_ES2:
	case OMAPDSS_VER_OMAP4:
		pll->hw = &dss_omap4_dsi_pll_hw;
		break;

	case OMAPDSS_VER_OMAP5:
		pll->hw = &dss_omap5_dsi_pll_hw;
		break;

	default:
		return -ENODEV;
	}

	pll->ops = &dsi_pll_ops;

	r = dss_pll_register(pll);
	if (r)
		return r;

	return 0;
}

/* DSI1 HW IP initialisation */
static int dsi_bind(struct device *dev, struct device *master, void *data)
{
	struct platform_device *dsidev = to_platform_device(dev);
	u32 rev;
	int r, i;
	struct dsi_data *dsi;
	struct resource *dsi_mem;
	struct resource *res;
	struct resource temp_res;

	dsi = devm_kzalloc(&dsidev->dev, sizeof(*dsi), GFP_KERNEL);
	if (!dsi)
		return -ENOMEM;

	dsi->pdev = dsidev;
	dev_set_drvdata(&dsidev->dev, dsi);

	spin_lock_init(&dsi->irq_lock);
	spin_lock_init(&dsi->errors_lock);
	dsi->errors = 0;

#ifdef CONFIG_OMAP2_DSS_COLLECT_IRQ_STATS
	spin_lock_init(&dsi->irq_stats_lock);
	dsi->irq_stats.last_reset = jiffies;
#endif

	mutex_init(&dsi->lock);
	sema_init(&dsi->bus_lock, 1);

	INIT_DEFERRABLE_WORK(&dsi->framedone_timeout_work,
			     dsi_framedone_timeout_work_callback);

#ifdef DSI_CATCH_MISSING_TE
	init_timer(&dsi->te_timer);
	dsi->te_timer.function = dsi_te_timeout;
	dsi->te_timer.data = 0;
#endif

	res = platform_get_resource_byname(dsidev, IORESOURCE_MEM, "proto");
	if (!res) {
		res = platform_get_resource(dsidev, IORESOURCE_MEM, 0);
		if (!res) {
			DSSERR("can't get IORESOURCE_MEM DSI\n");
			return -EINVAL;
		}

		temp_res.start = res->start;
		temp_res.end = temp_res.start + DSI_PROTO_SZ - 1;
		res = &temp_res;
	}

	dsi_mem = res;

	dsi->proto_base = devm_ioremap(&dsidev->dev, res->start,
		resource_size(res));
	if (!dsi->proto_base) {
		DSSERR("can't ioremap DSI protocol engine\n");
		return -ENOMEM;
	}

	res = platform_get_resource_byname(dsidev, IORESOURCE_MEM, "phy");
	if (!res) {
		res = platform_get_resource(dsidev, IORESOURCE_MEM, 0);
		if (!res) {
			DSSERR("can't get IORESOURCE_MEM DSI\n");
			return -EINVAL;
		}

		temp_res.start = res->start + DSI_PHY_OFFSET;
		temp_res.end = temp_res.start + DSI_PHY_SZ - 1;
		res = &temp_res;
	}

	dsi->phy_base = devm_ioremap(&dsidev->dev, res->start,
		resource_size(res));
	if (!dsi->proto_base) {
		DSSERR("can't ioremap DSI PHY\n");
		return -ENOMEM;
	}

	res = platform_get_resource_byname(dsidev, IORESOURCE_MEM, "pll");
	if (!res) {
		res = platform_get_resource(dsidev, IORESOURCE_MEM, 0);
		if (!res) {
			DSSERR("can't get IORESOURCE_MEM DSI\n");
			return -EINVAL;
		}

		temp_res.start = res->start + DSI_PLL_OFFSET;
		temp_res.end = temp_res.start + DSI_PLL_SZ - 1;
		res = &temp_res;
	}

	dsi->pll_base = devm_ioremap(&dsidev->dev, res->start,
		resource_size(res));
	if (!dsi->proto_base) {
		DSSERR("can't ioremap DSI PLL\n");
		return -ENOMEM;
	}

	dsi->irq = platform_get_irq(dsi->pdev, 0);
	if (dsi->irq < 0) {
		DSSERR("platform_get_irq failed\n");
		return -ENODEV;
	}

	r = devm_request_irq(&dsidev->dev, dsi->irq, omap_dsi_irq_handler,
			     IRQF_SHARED, dev_name(&dsidev->dev), dsi->pdev);
	if (r < 0) {
		DSSERR("request_irq failed\n");
		return r;
	}

	if (dsidev->dev.of_node) {
		const struct of_device_id *match;
		const struct dsi_module_id_data *d;

		match = of_match_node(dsi_of_match, dsidev->dev.of_node);
		if (!match) {
			DSSERR("unsupported DSI module\n");
			return -ENODEV;
		}

		d = match->data;

		while (d->address != 0 && d->address != dsi_mem->start)
			d++;

		if (d->address == 0) {
			DSSERR("unsupported DSI module\n");
			return -ENODEV;
		}

		dsi->module_id = d->id;
	} else {
		dsi->module_id = dsidev->id;
	}

	/* DSI VCs initialization */
	for (i = 0; i < ARRAY_SIZE(dsi->vc); i++) {
		dsi->vc[i].source = DSI_VC_SOURCE_L4;
		dsi->vc[i].dssdev = NULL;
		dsi->vc[i].vc_id = 0;
	}

	r = dsi_get_clocks(dsidev);
	if (r)
		return r;

	dsi_init_pll_data(dsidev);

	pm_runtime_enable(&dsidev->dev);

	r = dsi_runtime_get(dsidev);
	if (r)
		goto err_runtime_get;

	rev = dsi_read_reg(dsidev, DSI_REVISION);
	dev_dbg(&dsidev->dev, "OMAP DSI rev %d.%d\n",
	       FLD_GET(rev, 7, 4), FLD_GET(rev, 3, 0));

	/* DSI on OMAP3 doesn't have register DSI_GNQ, set number
	 * of data to 3 by default */
	if (dss_has_feature(FEAT_DSI_GNQ))
		/* NB_DATA_LANES */
		dsi->num_lanes_supported = 1 + REG_GET(dsidev, DSI_GNQ, 11, 9);
	else
		dsi->num_lanes_supported = 3;

	dsi->line_buffer_size = dsi_get_line_buf_size(dsidev);

	dsi_init_output(dsidev);

	if (dsidev->dev.of_node) {
		r = dsi_probe_of(dsidev);
		if (r) {
			DSSERR("Invalid DSI DT data\n");
			goto err_probe_of;
		}

		r = of_platform_populate(dsidev->dev.of_node, NULL, NULL,
			&dsidev->dev);
		if (r)
			DSSERR("Failed to populate DSI child devices: %d\n", r);
	}

	dsi_runtime_put(dsidev);

	if (dsi->module_id == 0)
		dss_debugfs_create_file("dsi1_regs", dsi1_dump_regs);
	else if (dsi->module_id == 1)
		dss_debugfs_create_file("dsi2_regs", dsi2_dump_regs);

#ifdef CONFIG_OMAP2_DSS_COLLECT_IRQ_STATS
	if (dsi->module_id == 0)
		dss_debugfs_create_file("dsi1_irqs", dsi1_dump_irqs);
	else if (dsi->module_id == 1)
		dss_debugfs_create_file("dsi2_irqs", dsi2_dump_irqs);
#endif

	return 0;

err_probe_of:
	dsi_uninit_output(dsidev);
	dsi_runtime_put(dsidev);

err_runtime_get:
	pm_runtime_disable(&dsidev->dev);
	return r;
}

static void dsi_unbind(struct device *dev, struct device *master, void *data)
{
	struct platform_device *dsidev = to_platform_device(dev);
	struct dsi_data *dsi = dsi_get_dsidrv_data(dsidev);

	of_platform_depopulate(&dsidev->dev);

	WARN_ON(dsi->scp_clk_refcount > 0);

	dss_pll_unregister(&dsi->pll);

	dsi_uninit_output(dsidev);

	pm_runtime_disable(&dsidev->dev);

	if (dsi->vdds_dsi_reg != NULL && dsi->vdds_dsi_enabled) {
		regulator_disable(dsi->vdds_dsi_reg);
		dsi->vdds_dsi_enabled = false;
	}
}

static const struct component_ops dsi_component_ops = {
	.bind	= dsi_bind,
	.unbind	= dsi_unbind,
};

static int dsi_probe(struct platform_device *pdev)
{
	return component_add(&pdev->dev, &dsi_component_ops);
}

static int dsi_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &dsi_component_ops);
	return 0;
}

static int dsi_runtime_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct dsi_data *dsi = dsi_get_dsidrv_data(pdev);

	dsi->is_enabled = false;
	/* ensure the irq handler sees the is_enabled value */
	smp_wmb();
	/* wait for current handler to finish before turning the DSI off */
	synchronize_irq(dsi->irq);

	dispc_runtime_put();

	return 0;
}

static int dsi_runtime_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct dsi_data *dsi = dsi_get_dsidrv_data(pdev);
	int r;

	r = dispc_runtime_get();
	if (r)
		return r;

	dsi->is_enabled = true;
	/* ensure the irq handler sees the is_enabled value */
	smp_wmb();

	return 0;
}

static const struct dev_pm_ops dsi_pm_ops = {
	.runtime_suspend = dsi_runtime_suspend,
	.runtime_resume = dsi_runtime_resume,
};

static const struct dsi_module_id_data dsi_of_data_omap3[] = {
	{ .address = 0x4804fc00, .id = 0, },
	{ },
};

static const struct dsi_module_id_data dsi_of_data_omap4[] = {
	{ .address = 0x58004000, .id = 0, },
	{ .address = 0x58005000, .id = 1, },
	{ },
};

static const struct dsi_module_id_data dsi_of_data_omap5[] = {
	{ .address = 0x58004000, .id = 0, },
	{ .address = 0x58009000, .id = 1, },
	{ },
};

static const struct of_device_id dsi_of_match[] = {
	{ .compatible = "ti,omap3-dsi", .data = dsi_of_data_omap3, },
	{ .compatible = "ti,omap4-dsi", .data = dsi_of_data_omap4, },
	{ .compatible = "ti,omap5-dsi", .data = dsi_of_data_omap5, },
	{},
};

static struct platform_driver omap_dsihw_driver = {
	.probe		= dsi_probe,
	.remove		= dsi_remove,
	.driver         = {
		.name   = "omapdss_dsi",
		.pm	= &dsi_pm_ops,
		.of_match_table = dsi_of_match,
		.suppress_bind_attrs = true,
	},
};

int __init dsi_init_platform_driver(void)
{
	return platform_driver_register(&omap_dsihw_driver);
}

void dsi_uninit_platform_driver(void)
{
	platform_driver_unregister(&omap_dsihw_driver);
}
