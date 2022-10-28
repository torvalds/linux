// SPDX-License-Identifier: GPL-2.0+
/*
 * linux/arch/arm/plat-omap/dmtimer.c
 *
 * OMAP Dual-Mode Timers
 *
 * Copyright (C) 2010 Texas Instruments Incorporated - https://www.ti.com/
 * Tarun Kanti DebBarma <tarun.kanti@ti.com>
 * Thara Gopinath <thara@ti.com>
 *
 * dmtimer adaptation to platform_driver.
 *
 * Copyright (C) 2005 Nokia Corporation
 * OMAP2 support by Juha Yrjola
 * API improvements and OMAP2 clock framework support by Timo Teras
 *
 * Copyright (C) 2009 Texas Instruments
 * Added OMAP4 support - Santosh Shilimkar <santosh.shilimkar@ti.com>
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/cpu_pm.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/pm_runtime.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/platform_data/dmtimer-omap.h>

#include <clocksource/timer-ti-dm.h>

/*
 * timer errata flags
 *
 * Errata i103/i767 impacts all OMAP3/4/5 devices including AM33xx. This
 * errata prevents us from using posted mode on these devices, unless the
 * timer counter register is never read. For more details please refer to
 * the OMAP3/4/5 errata documents.
 */
#define OMAP_TIMER_ERRATA_I103_I767			0x80000000

/* posted mode types */
#define OMAP_TIMER_NONPOSTED			0x00
#define OMAP_TIMER_POSTED			0x01

/* register offsets with the write pending bit encoded */
#define	WPSHIFT					16

#define OMAP_TIMER_WAKEUP_EN_REG		(_OMAP_TIMER_WAKEUP_EN_OFFSET \
							| (WP_NONE << WPSHIFT))

#define OMAP_TIMER_CTRL_REG			(_OMAP_TIMER_CTRL_OFFSET \
							| (WP_TCLR << WPSHIFT))

#define OMAP_TIMER_COUNTER_REG			(_OMAP_TIMER_COUNTER_OFFSET \
							| (WP_TCRR << WPSHIFT))

#define OMAP_TIMER_LOAD_REG			(_OMAP_TIMER_LOAD_OFFSET \
							| (WP_TLDR << WPSHIFT))

#define OMAP_TIMER_TRIGGER_REG			(_OMAP_TIMER_TRIGGER_OFFSET \
							| (WP_TTGR << WPSHIFT))

#define OMAP_TIMER_WRITE_PEND_REG		(_OMAP_TIMER_WRITE_PEND_OFFSET \
							| (WP_NONE << WPSHIFT))

#define OMAP_TIMER_MATCH_REG			(_OMAP_TIMER_MATCH_OFFSET \
							| (WP_TMAR << WPSHIFT))

#define OMAP_TIMER_CAPTURE_REG			(_OMAP_TIMER_CAPTURE_OFFSET \
							| (WP_NONE << WPSHIFT))

#define OMAP_TIMER_IF_CTRL_REG			(_OMAP_TIMER_IF_CTRL_OFFSET \
							| (WP_NONE << WPSHIFT))

#define OMAP_TIMER_CAPTURE2_REG			(_OMAP_TIMER_CAPTURE2_OFFSET \
							| (WP_NONE << WPSHIFT))

#define OMAP_TIMER_TICK_POS_REG			(_OMAP_TIMER_TICK_POS_OFFSET \
							| (WP_TPIR << WPSHIFT))

#define OMAP_TIMER_TICK_NEG_REG			(_OMAP_TIMER_TICK_NEG_OFFSET \
							| (WP_TNIR << WPSHIFT))

#define OMAP_TIMER_TICK_COUNT_REG		(_OMAP_TIMER_TICK_COUNT_OFFSET \
							| (WP_TCVR << WPSHIFT))

#define OMAP_TIMER_TICK_INT_MASK_SET_REG				\
		(_OMAP_TIMER_TICK_INT_MASK_SET_OFFSET | (WP_TOCR << WPSHIFT))

#define OMAP_TIMER_TICK_INT_MASK_COUNT_REG				\
		(_OMAP_TIMER_TICK_INT_MASK_COUNT_OFFSET | (WP_TOWR << WPSHIFT))

struct timer_regs {
	u32 ocp_cfg;
	u32 tidr;
	u32 tier;
	u32 twer;
	u32 tclr;
	u32 tcrr;
	u32 tldr;
	u32 ttrg;
	u32 twps;
	u32 tmar;
	u32 tcar1;
	u32 tsicr;
	u32 tcar2;
	u32 tpir;
	u32 tnir;
	u32 tcvr;
	u32 tocr;
	u32 towr;
};

struct dmtimer {
	struct omap_dm_timer cookie;
	int id;
	int irq;
	struct clk *fclk;

	void __iomem	*io_base;
	int		irq_stat;	/* TISR/IRQSTATUS interrupt status */
	int		irq_ena;	/* irq enable */
	int		irq_dis;	/* irq disable, only on v2 ip */
	void __iomem	*pend;		/* write pending */
	void __iomem	*func_base;	/* function register base */

	atomic_t enabled;
	unsigned long rate;
	unsigned reserved:1;
	unsigned posted:1;
	unsigned omap1:1;
	struct timer_regs context;
	int revision;
	u32 capability;
	u32 errata;
	struct platform_device *pdev;
	struct list_head node;
	struct notifier_block nb;
};

static u32 omap_reserved_systimers;
static LIST_HEAD(omap_timer_list);
static DEFINE_SPINLOCK(dm_timer_lock);

enum {
	REQUEST_ANY = 0,
	REQUEST_BY_ID,
	REQUEST_BY_CAP,
	REQUEST_BY_NODE,
};

/**
 * dmtimer_read - read timer registers in posted and non-posted mode
 * @timer:	timer pointer over which read operation to perform
 * @reg:	lowest byte holds the register offset
 *
 * The posted mode bit is encoded in reg. Note that in posted mode, write
 * pending bit must be checked. Otherwise a read of a non completed write
 * will produce an error.
 */
static inline u32 dmtimer_read(struct dmtimer *timer, u32 reg)
{
	u16 wp, offset;

	wp = reg >> WPSHIFT;
	offset = reg & 0xff;

	/* Wait for a possible write pending bit in posted mode */
	if (wp && timer->posted)
		while (readl_relaxed(timer->pend) & wp)
			cpu_relax();

	return readl_relaxed(timer->func_base + offset);
}

/**
 * dmtimer_write - write timer registers in posted and non-posted mode
 * @timer:      timer pointer over which write operation is to perform
 * @reg:        lowest byte holds the register offset
 * @value:      data to write into the register
 *
 * The posted mode bit is encoded in reg. Note that in posted mode, the write
 * pending bit must be checked. Otherwise a write on a register which has a
 * pending write will be lost.
 */
static inline void dmtimer_write(struct dmtimer *timer, u32 reg, u32 val)
{
	u16 wp, offset;

	wp = reg >> WPSHIFT;
	offset = reg & 0xff;

	/* Wait for a possible write pending bit in posted mode */
	if (wp && timer->posted)
		while (readl_relaxed(timer->pend) & wp)
			cpu_relax();

	writel_relaxed(val, timer->func_base + offset);
}

static inline void __omap_dm_timer_init_regs(struct dmtimer *timer)
{
	u32 tidr;

	/* Assume v1 ip if bits [31:16] are zero */
	tidr = readl_relaxed(timer->io_base);
	if (!(tidr >> 16)) {
		timer->revision = 1;
		timer->irq_stat = OMAP_TIMER_V1_STAT_OFFSET;
		timer->irq_ena = OMAP_TIMER_V1_INT_EN_OFFSET;
		timer->irq_dis = OMAP_TIMER_V1_INT_EN_OFFSET;
		timer->pend = timer->io_base + _OMAP_TIMER_WRITE_PEND_OFFSET;
		timer->func_base = timer->io_base;
	} else {
		timer->revision = 2;
		timer->irq_stat = OMAP_TIMER_V2_IRQSTATUS - OMAP_TIMER_V2_FUNC_OFFSET;
		timer->irq_ena = OMAP_TIMER_V2_IRQENABLE_SET - OMAP_TIMER_V2_FUNC_OFFSET;
		timer->irq_dis = OMAP_TIMER_V2_IRQENABLE_CLR - OMAP_TIMER_V2_FUNC_OFFSET;
		timer->pend = timer->io_base +
			_OMAP_TIMER_WRITE_PEND_OFFSET +
				OMAP_TIMER_V2_FUNC_OFFSET;
		timer->func_base = timer->io_base + OMAP_TIMER_V2_FUNC_OFFSET;
	}
}

/*
 * __omap_dm_timer_enable_posted - enables write posted mode
 * @timer:      pointer to timer instance handle
 *
 * Enables the write posted mode for the timer. When posted mode is enabled
 * writes to certain timer registers are immediately acknowledged by the
 * internal bus and hence prevents stalling the CPU waiting for the write to
 * complete. Enabling this feature can improve performance for writing to the
 * timer registers.
 */
static inline void __omap_dm_timer_enable_posted(struct dmtimer *timer)
{
	if (timer->posted)
		return;

	if (timer->errata & OMAP_TIMER_ERRATA_I103_I767) {
		timer->posted = OMAP_TIMER_NONPOSTED;
		dmtimer_write(timer, OMAP_TIMER_IF_CTRL_REG, 0);
		return;
	}

	dmtimer_write(timer, OMAP_TIMER_IF_CTRL_REG, OMAP_TIMER_CTRL_POSTED);
	timer->context.tsicr = OMAP_TIMER_CTRL_POSTED;
	timer->posted = OMAP_TIMER_POSTED;
}

static inline void __omap_dm_timer_stop(struct dmtimer *timer,
					unsigned long rate)
{
	u32 l;

	l = dmtimer_read(timer, OMAP_TIMER_CTRL_REG);
	if (l & OMAP_TIMER_CTRL_ST) {
		l &= ~0x1;
		dmtimer_write(timer, OMAP_TIMER_CTRL_REG, l);
#ifdef CONFIG_ARCH_OMAP2PLUS
		/* Readback to make sure write has completed */
		dmtimer_read(timer, OMAP_TIMER_CTRL_REG);
		/*
		 * Wait for functional clock period x 3.5 to make sure that
		 * timer is stopped
		 */
		udelay(3500000 / rate + 1);
#endif
	}

	/* Ack possibly pending interrupt */
	dmtimer_write(timer, timer->irq_stat, OMAP_TIMER_INT_OVERFLOW);
}

static inline void __omap_dm_timer_int_enable(struct dmtimer *timer,
					      unsigned int value)
{
	dmtimer_write(timer, timer->irq_ena, value);
	dmtimer_write(timer, OMAP_TIMER_WAKEUP_EN_REG, value);
}

static inline unsigned int
__omap_dm_timer_read_counter(struct dmtimer *timer)
{
	return dmtimer_read(timer, OMAP_TIMER_COUNTER_REG);
}

static inline void __omap_dm_timer_write_status(struct dmtimer *timer,
						unsigned int value)
{
	dmtimer_write(timer, timer->irq_stat, value);
}

static void omap_timer_restore_context(struct dmtimer *timer)
{
	dmtimer_write(timer, OMAP_TIMER_OCP_CFG_OFFSET, timer->context.ocp_cfg);

	dmtimer_write(timer, OMAP_TIMER_WAKEUP_EN_REG, timer->context.twer);
	dmtimer_write(timer, OMAP_TIMER_COUNTER_REG, timer->context.tcrr);
	dmtimer_write(timer, OMAP_TIMER_LOAD_REG, timer->context.tldr);
	dmtimer_write(timer, OMAP_TIMER_MATCH_REG, timer->context.tmar);
	dmtimer_write(timer, OMAP_TIMER_IF_CTRL_REG, timer->context.tsicr);
	dmtimer_write(timer, timer->irq_ena, timer->context.tier);
	dmtimer_write(timer, OMAP_TIMER_CTRL_REG, timer->context.tclr);
}

static void omap_timer_save_context(struct dmtimer *timer)
{
	timer->context.ocp_cfg = dmtimer_read(timer, OMAP_TIMER_OCP_CFG_OFFSET);

	timer->context.tclr = dmtimer_read(timer, OMAP_TIMER_CTRL_REG);
	timer->context.twer = dmtimer_read(timer, OMAP_TIMER_WAKEUP_EN_REG);
	timer->context.tldr = dmtimer_read(timer, OMAP_TIMER_LOAD_REG);
	timer->context.tmar = dmtimer_read(timer, OMAP_TIMER_MATCH_REG);
	timer->context.tier = dmtimer_read(timer, timer->irq_ena);
	timer->context.tsicr = dmtimer_read(timer, OMAP_TIMER_IF_CTRL_REG);
}

static int omap_timer_context_notifier(struct notifier_block *nb,
				       unsigned long cmd, void *v)
{
	struct dmtimer *timer;

	timer = container_of(nb, struct dmtimer, nb);

	switch (cmd) {
	case CPU_CLUSTER_PM_ENTER:
		if ((timer->capability & OMAP_TIMER_ALWON) ||
		    !atomic_read(&timer->enabled))
			break;
		omap_timer_save_context(timer);
		break;
	case CPU_CLUSTER_PM_ENTER_FAILED:	/* No need to restore context */
		break;
	case CPU_CLUSTER_PM_EXIT:
		if ((timer->capability & OMAP_TIMER_ALWON) ||
		    !atomic_read(&timer->enabled))
			break;
		omap_timer_restore_context(timer);
		break;
	}

	return NOTIFY_OK;
}

static int omap_dm_timer_reset(struct dmtimer *timer)
{
	u32 l, timeout = 100000;

	if (timer->revision != 1)
		return -EINVAL;

	dmtimer_write(timer, OMAP_TIMER_IF_CTRL_REG, 0x06);

	do {
		l = dmtimer_read(timer, OMAP_TIMER_V1_SYS_STAT_OFFSET);
	} while (!l && timeout--);

	if (!timeout) {
		dev_err(&timer->pdev->dev, "Timer failed to reset\n");
		return -ETIMEDOUT;
	}

	/* Configure timer for smart-idle mode */
	l = dmtimer_read(timer, OMAP_TIMER_OCP_CFG_OFFSET);
	l |= 0x2 << 0x3;
	dmtimer_write(timer, OMAP_TIMER_OCP_CFG_OFFSET, l);

	timer->posted = 0;

	return 0;
}

/*
 * Functions exposed to PWM and remoteproc drivers via platform_data.
 * Do not use these in the driver, these will get deprecated and will
 * will be replaced by Linux generic framework functions such as
 * chained interrupts and clock framework.
 */
static struct dmtimer *to_dmtimer(struct omap_dm_timer *cookie)
{
	if (!cookie)
		return NULL;

	return container_of(cookie, struct dmtimer, cookie);
}

static int omap_dm_timer_set_source(struct omap_dm_timer *cookie, int source)
{
	int ret;
	const char *parent_name;
	struct clk *parent;
	struct dmtimer_platform_data *pdata;
	struct dmtimer *timer;

	timer = to_dmtimer(cookie);
	if (unlikely(!timer) || IS_ERR(timer->fclk))
		return -EINVAL;

	switch (source) {
	case OMAP_TIMER_SRC_SYS_CLK:
		parent_name = "timer_sys_ck";
		break;
	case OMAP_TIMER_SRC_32_KHZ:
		parent_name = "timer_32k_ck";
		break;
	case OMAP_TIMER_SRC_EXT_CLK:
		parent_name = "timer_ext_ck";
		break;
	default:
		return -EINVAL;
	}

	pdata = timer->pdev->dev.platform_data;

	/*
	 * FIXME: Used for OMAP1 devices only because they do not currently
	 * use the clock framework to set the parent clock. To be removed
	 * once OMAP1 migrated to using clock framework for dmtimers
	 */
	if (timer->omap1 && pdata && pdata->set_timer_src)
		return pdata->set_timer_src(timer->pdev, source);

#if defined(CONFIG_COMMON_CLK)
	/* Check if the clock has configurable parents */
	if (clk_hw_get_num_parents(__clk_get_hw(timer->fclk)) < 2)
		return 0;
#endif

	parent = clk_get(&timer->pdev->dev, parent_name);
	if (IS_ERR(parent)) {
		pr_err("%s: %s not found\n", __func__, parent_name);
		return -EINVAL;
	}

	ret = clk_set_parent(timer->fclk, parent);
	if (ret < 0)
		pr_err("%s: failed to set %s as parent\n", __func__,
			parent_name);

	clk_put(parent);

	return ret;
}

static void omap_dm_timer_enable(struct omap_dm_timer *cookie)
{
	struct dmtimer *timer = to_dmtimer(cookie);
	struct device *dev = &timer->pdev->dev;
	int rc;

	rc = pm_runtime_resume_and_get(dev);
	if (rc)
		dev_err(dev, "could not enable timer\n");
}

static void omap_dm_timer_disable(struct omap_dm_timer *cookie)
{
	struct dmtimer *timer = to_dmtimer(cookie);
	struct device *dev = &timer->pdev->dev;

	pm_runtime_put_sync(dev);
}

static int omap_dm_timer_prepare(struct dmtimer *timer)
{
	struct device *dev = &timer->pdev->dev;
	int rc;

	rc = pm_runtime_resume_and_get(dev);
	if (rc)
		return rc;

	if (timer->capability & OMAP_TIMER_NEEDS_RESET) {
		rc = omap_dm_timer_reset(timer);
		if (rc) {
			pm_runtime_put_sync(dev);
			return rc;
		}
	}

	__omap_dm_timer_enable_posted(timer);
	pm_runtime_put_sync(dev);

	return 0;
}

static inline u32 omap_dm_timer_reserved_systimer(int id)
{
	return (omap_reserved_systimers & (1 << (id - 1))) ? 1 : 0;
}

static struct dmtimer *_omap_dm_timer_request(int req_type, void *data)
{
	struct dmtimer *timer = NULL, *t;
	struct device_node *np = NULL;
	unsigned long flags;
	u32 cap = 0;
	int id = 0;

	switch (req_type) {
	case REQUEST_BY_ID:
		id = *(int *)data;
		break;
	case REQUEST_BY_CAP:
		cap = *(u32 *)data;
		break;
	case REQUEST_BY_NODE:
		np = (struct device_node *)data;
		break;
	default:
		/* REQUEST_ANY */
		break;
	}

	spin_lock_irqsave(&dm_timer_lock, flags);
	list_for_each_entry(t, &omap_timer_list, node) {
		if (t->reserved)
			continue;

		switch (req_type) {
		case REQUEST_BY_ID:
			if (id == t->pdev->id) {
				timer = t;
				timer->reserved = 1;
				goto found;
			}
			break;
		case REQUEST_BY_CAP:
			if (cap == (t->capability & cap)) {
				/*
				 * If timer is not NULL, we have already found
				 * one timer. But it was not an exact match
				 * because it had more capabilities than what
				 * was required. Therefore, unreserve the last
				 * timer found and see if this one is a better
				 * match.
				 */
				if (timer)
					timer->reserved = 0;
				timer = t;
				timer->reserved = 1;

				/* Exit loop early if we find an exact match */
				if (t->capability == cap)
					goto found;
			}
			break;
		case REQUEST_BY_NODE:
			if (np == t->pdev->dev.of_node) {
				timer = t;
				timer->reserved = 1;
				goto found;
			}
			break;
		default:
			/* REQUEST_ANY */
			timer = t;
			timer->reserved = 1;
			goto found;
		}
	}
found:
	spin_unlock_irqrestore(&dm_timer_lock, flags);

	if (timer && omap_dm_timer_prepare(timer)) {
		timer->reserved = 0;
		timer = NULL;
	}

	if (!timer)
		pr_debug("%s: timer request failed!\n", __func__);

	return timer;
}

static struct omap_dm_timer *omap_dm_timer_request(void)
{
	struct dmtimer *timer;

	timer = _omap_dm_timer_request(REQUEST_ANY, NULL);
	if (!timer)
		return NULL;

	return &timer->cookie;
}

static struct omap_dm_timer *omap_dm_timer_request_specific(int id)
{
	struct dmtimer *timer;

	/* Requesting timer by ID is not supported when device tree is used */
	if (of_have_populated_dt()) {
		pr_warn("%s: Please use omap_dm_timer_request_by_node()\n",
			__func__);
		return NULL;
	}

	timer = _omap_dm_timer_request(REQUEST_BY_ID, &id);
	if (!timer)
		return NULL;

	return &timer->cookie;
}

/**
 * omap_dm_timer_request_by_node - Request a timer by device-tree node
 * @np:		Pointer to device-tree timer node
 *
 * Request a timer based upon a device node pointer. Returns pointer to
 * timer handle on success and a NULL pointer on failure.
 */
static struct omap_dm_timer *omap_dm_timer_request_by_node(struct device_node *np)
{
	struct dmtimer *timer;

	if (!np)
		return NULL;

	timer = _omap_dm_timer_request(REQUEST_BY_NODE, np);
	if (!timer)
		return NULL;

	return &timer->cookie;
}

static int omap_dm_timer_free(struct omap_dm_timer *cookie)
{
	struct dmtimer *timer;

	timer = to_dmtimer(cookie);
	if (unlikely(!timer))
		return -EINVAL;

	WARN_ON(!timer->reserved);
	timer->reserved = 0;
	return 0;
}

int omap_dm_timer_get_irq(struct omap_dm_timer *cookie)
{
	struct dmtimer *timer = to_dmtimer(cookie);
	if (timer)
		return timer->irq;
	return -EINVAL;
}

#if defined(CONFIG_ARCH_OMAP1)
#include <linux/soc/ti/omap1-io.h>

static struct clk *omap_dm_timer_get_fclk(struct omap_dm_timer *cookie)
{
	return NULL;
}

/**
 * omap_dm_timer_modify_idlect_mask - Check if any running timers use ARMXOR
 * @inputmask: current value of idlect mask
 */
__u32 omap_dm_timer_modify_idlect_mask(__u32 inputmask)
{
	int i = 0;
	struct dmtimer *timer = NULL;
	unsigned long flags;

	/* If ARMXOR cannot be idled this function call is unnecessary */
	if (!(inputmask & (1 << 1)))
		return inputmask;

	/* If any active timer is using ARMXOR return modified mask */
	spin_lock_irqsave(&dm_timer_lock, flags);
	list_for_each_entry(timer, &omap_timer_list, node) {
		u32 l;

		l = dmtimer_read(timer, OMAP_TIMER_CTRL_REG);
		if (l & OMAP_TIMER_CTRL_ST) {
			if (((omap_readl(MOD_CONF_CTRL_1) >> (i * 2)) & 0x03) == 0)
				inputmask &= ~(1 << 1);
			else
				inputmask &= ~(1 << 2);
		}
		i++;
	}
	spin_unlock_irqrestore(&dm_timer_lock, flags);

	return inputmask;
}

#else

static struct clk *omap_dm_timer_get_fclk(struct omap_dm_timer *cookie)
{
	struct dmtimer *timer = to_dmtimer(cookie);

	if (timer && !IS_ERR(timer->fclk))
		return timer->fclk;
	return NULL;
}

__u32 omap_dm_timer_modify_idlect_mask(__u32 inputmask)
{
	BUG();

	return 0;
}

#endif

static int omap_dm_timer_start(struct omap_dm_timer *cookie)
{
	struct dmtimer *timer;
	struct device *dev;
	int rc;
	u32 l;

	timer = to_dmtimer(cookie);
	if (unlikely(!timer))
		return -EINVAL;

	dev = &timer->pdev->dev;

	rc = pm_runtime_resume_and_get(dev);
	if (rc)
		return rc;

	l = dmtimer_read(timer, OMAP_TIMER_CTRL_REG);
	if (!(l & OMAP_TIMER_CTRL_ST)) {
		l |= OMAP_TIMER_CTRL_ST;
		dmtimer_write(timer, OMAP_TIMER_CTRL_REG, l);
	}

	return 0;
}

static int omap_dm_timer_stop(struct omap_dm_timer *cookie)
{
	struct dmtimer *timer;
	struct device *dev;
	unsigned long rate = 0;

	timer = to_dmtimer(cookie);
	if (unlikely(!timer))
		return -EINVAL;

	dev = &timer->pdev->dev;

	if (!timer->omap1)
		rate = clk_get_rate(timer->fclk);

	__omap_dm_timer_stop(timer, rate);

	pm_runtime_put_sync(dev);

	return 0;
}

static int omap_dm_timer_set_load(struct omap_dm_timer *cookie,
				  unsigned int load)
{
	struct dmtimer *timer;
	struct device *dev;
	int rc;

	timer = to_dmtimer(cookie);
	if (unlikely(!timer))
		return -EINVAL;

	dev = &timer->pdev->dev;
	rc = pm_runtime_resume_and_get(dev);
	if (rc)
		return rc;

	dmtimer_write(timer, OMAP_TIMER_LOAD_REG, load);

	pm_runtime_put_sync(dev);

	return 0;
}

static int omap_dm_timer_set_match(struct omap_dm_timer *cookie, int enable,
				   unsigned int match)
{
	struct dmtimer *timer;
	struct device *dev;
	int rc;
	u32 l;

	timer = to_dmtimer(cookie);
	if (unlikely(!timer))
		return -EINVAL;

	dev = &timer->pdev->dev;
	rc = pm_runtime_resume_and_get(dev);
	if (rc)
		return rc;

	l = dmtimer_read(timer, OMAP_TIMER_CTRL_REG);
	if (enable)
		l |= OMAP_TIMER_CTRL_CE;
	else
		l &= ~OMAP_TIMER_CTRL_CE;
	dmtimer_write(timer, OMAP_TIMER_MATCH_REG, match);
	dmtimer_write(timer, OMAP_TIMER_CTRL_REG, l);

	pm_runtime_put_sync(dev);

	return 0;
}

static int omap_dm_timer_set_pwm(struct omap_dm_timer *cookie, int def_on,
				 int toggle, int trigger, int autoreload)
{
	struct dmtimer *timer;
	struct device *dev;
	int rc;
	u32 l;

	timer = to_dmtimer(cookie);
	if (unlikely(!timer))
		return -EINVAL;

	dev = &timer->pdev->dev;
	rc = pm_runtime_resume_and_get(dev);
	if (rc)
		return rc;

	l = dmtimer_read(timer, OMAP_TIMER_CTRL_REG);
	l &= ~(OMAP_TIMER_CTRL_GPOCFG | OMAP_TIMER_CTRL_SCPWM |
	       OMAP_TIMER_CTRL_PT | (0x03 << 10) | OMAP_TIMER_CTRL_AR);
	if (def_on)
		l |= OMAP_TIMER_CTRL_SCPWM;
	if (toggle)
		l |= OMAP_TIMER_CTRL_PT;
	l |= trigger << 10;
	if (autoreload)
		l |= OMAP_TIMER_CTRL_AR;
	dmtimer_write(timer, OMAP_TIMER_CTRL_REG, l);

	pm_runtime_put_sync(dev);

	return 0;
}

static int omap_dm_timer_get_pwm_status(struct omap_dm_timer *cookie)
{
	struct dmtimer *timer;
	struct device *dev;
	int rc;
	u32 l;

	timer = to_dmtimer(cookie);
	if (unlikely(!timer))
		return -EINVAL;

	dev = &timer->pdev->dev;
	rc = pm_runtime_resume_and_get(dev);
	if (rc)
		return rc;

	l = dmtimer_read(timer, OMAP_TIMER_CTRL_REG);

	pm_runtime_put_sync(dev);

	return l;
}

static int omap_dm_timer_set_prescaler(struct omap_dm_timer *cookie,
				       int prescaler)
{
	struct dmtimer *timer;
	struct device *dev;
	int rc;
	u32 l;

	timer = to_dmtimer(cookie);
	if (unlikely(!timer) || prescaler < -1 || prescaler > 7)
		return -EINVAL;

	dev = &timer->pdev->dev;
	rc = pm_runtime_resume_and_get(dev);
	if (rc)
		return rc;

	l = dmtimer_read(timer, OMAP_TIMER_CTRL_REG);
	l &= ~(OMAP_TIMER_CTRL_PRE | (0x07 << 2));
	if (prescaler >= 0) {
		l |= OMAP_TIMER_CTRL_PRE;
		l |= prescaler << 2;
	}
	dmtimer_write(timer, OMAP_TIMER_CTRL_REG, l);

	pm_runtime_put_sync(dev);

	return 0;
}

static int omap_dm_timer_set_int_enable(struct omap_dm_timer *cookie,
					unsigned int value)
{
	struct dmtimer *timer;
	struct device *dev;
	int rc;

	timer = to_dmtimer(cookie);
	if (unlikely(!timer))
		return -EINVAL;

	dev = &timer->pdev->dev;
	rc = pm_runtime_resume_and_get(dev);
	if (rc)
		return rc;

	__omap_dm_timer_int_enable(timer, value);

	pm_runtime_put_sync(dev);

	return 0;
}

/**
 * omap_dm_timer_set_int_disable - disable timer interrupts
 * @timer:	pointer to timer handle
 * @mask:	bit mask of interrupts to be disabled
 *
 * Disables the specified timer interrupts for a timer.
 */
static int omap_dm_timer_set_int_disable(struct omap_dm_timer *cookie, u32 mask)
{
	struct dmtimer *timer;
	struct device *dev;
	u32 l = mask;
	int rc;

	timer = to_dmtimer(cookie);
	if (unlikely(!timer))
		return -EINVAL;

	dev = &timer->pdev->dev;
	rc = pm_runtime_resume_and_get(dev);
	if (rc)
		return rc;

	if (timer->revision == 1)
		l = dmtimer_read(timer, timer->irq_ena) & ~mask;

	dmtimer_write(timer, timer->irq_dis, l);
	l = dmtimer_read(timer, OMAP_TIMER_WAKEUP_EN_REG) & ~mask;
	dmtimer_write(timer, OMAP_TIMER_WAKEUP_EN_REG, l);

	pm_runtime_put_sync(dev);

	return 0;
}

static unsigned int omap_dm_timer_read_status(struct omap_dm_timer *cookie)
{
	struct dmtimer *timer;
	unsigned int l;

	timer = to_dmtimer(cookie);
	if (unlikely(!timer || !atomic_read(&timer->enabled))) {
		pr_err("%s: timer not available or enabled.\n", __func__);
		return 0;
	}

	l = dmtimer_read(timer, timer->irq_stat);

	return l;
}

static int omap_dm_timer_write_status(struct omap_dm_timer *cookie, unsigned int value)
{
	struct dmtimer *timer;

	timer = to_dmtimer(cookie);
	if (unlikely(!timer || !atomic_read(&timer->enabled)))
		return -EINVAL;

	__omap_dm_timer_write_status(timer, value);

	return 0;
}

static unsigned int omap_dm_timer_read_counter(struct omap_dm_timer *cookie)
{
	struct dmtimer *timer;

	timer = to_dmtimer(cookie);
	if (unlikely(!timer || !atomic_read(&timer->enabled))) {
		pr_err("%s: timer not iavailable or enabled.\n", __func__);
		return 0;
	}

	return __omap_dm_timer_read_counter(timer);
}

static int omap_dm_timer_write_counter(struct omap_dm_timer *cookie, unsigned int value)
{
	struct dmtimer *timer;

	timer = to_dmtimer(cookie);
	if (unlikely(!timer || !atomic_read(&timer->enabled))) {
		pr_err("%s: timer not available or enabled.\n", __func__);
		return -EINVAL;
	}

	dmtimer_write(timer, OMAP_TIMER_COUNTER_REG, value);

	/* Save the context */
	timer->context.tcrr = value;
	return 0;
}

static int __maybe_unused omap_dm_timer_runtime_suspend(struct device *dev)
{
	struct dmtimer *timer = dev_get_drvdata(dev);

	atomic_set(&timer->enabled, 0);

	if (timer->capability & OMAP_TIMER_ALWON || !timer->func_base)
		return 0;

	omap_timer_save_context(timer);

	return 0;
}

static int __maybe_unused omap_dm_timer_runtime_resume(struct device *dev)
{
	struct dmtimer *timer = dev_get_drvdata(dev);

	if (!(timer->capability & OMAP_TIMER_ALWON) && timer->func_base)
		omap_timer_restore_context(timer);

	atomic_set(&timer->enabled, 1);

	return 0;
}

static const struct dev_pm_ops omap_dm_timer_pm_ops = {
	SET_RUNTIME_PM_OPS(omap_dm_timer_runtime_suspend,
			   omap_dm_timer_runtime_resume, NULL)
};

static const struct of_device_id omap_timer_match[];

/**
 * omap_dm_timer_probe - probe function called for every registered device
 * @pdev:	pointer to current timer platform device
 *
 * Called by driver framework at the end of device registration for all
 * timer devices.
 */
static int omap_dm_timer_probe(struct platform_device *pdev)
{
	unsigned long flags;
	struct dmtimer *timer;
	struct device *dev = &pdev->dev;
	const struct dmtimer_platform_data *pdata;
	int ret;

	pdata = of_device_get_match_data(dev);
	if (!pdata)
		pdata = dev_get_platdata(dev);
	else
		dev->platform_data = (void *)pdata;

	if (!pdata) {
		dev_err(dev, "%s: no platform data.\n", __func__);
		return -ENODEV;
	}

	timer = devm_kzalloc(dev, sizeof(*timer), GFP_KERNEL);
	if (!timer)
		return  -ENOMEM;

	timer->irq = platform_get_irq(pdev, 0);
	if (timer->irq < 0)
		return timer->irq;

	timer->io_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(timer->io_base))
		return PTR_ERR(timer->io_base);

	platform_set_drvdata(pdev, timer);

	if (dev->of_node) {
		if (of_find_property(dev->of_node, "ti,timer-alwon", NULL))
			timer->capability |= OMAP_TIMER_ALWON;
		if (of_find_property(dev->of_node, "ti,timer-dsp", NULL))
			timer->capability |= OMAP_TIMER_HAS_DSP_IRQ;
		if (of_find_property(dev->of_node, "ti,timer-pwm", NULL))
			timer->capability |= OMAP_TIMER_HAS_PWM;
		if (of_find_property(dev->of_node, "ti,timer-secure", NULL))
			timer->capability |= OMAP_TIMER_SECURE;
	} else {
		timer->id = pdev->id;
		timer->capability = pdata->timer_capability;
		timer->reserved = omap_dm_timer_reserved_systimer(timer->id);
	}

	timer->omap1 = timer->capability & OMAP_TIMER_NEEDS_RESET;

	/* OMAP1 devices do not yet use the clock framework for dmtimers */
	if (!timer->omap1) {
		timer->fclk = devm_clk_get(dev, "fck");
		if (IS_ERR(timer->fclk))
			return PTR_ERR(timer->fclk);
	} else {
		timer->fclk = ERR_PTR(-ENODEV);
	}

	if (!(timer->capability & OMAP_TIMER_ALWON)) {
		timer->nb.notifier_call = omap_timer_context_notifier;
		cpu_pm_register_notifier(&timer->nb);
	}

	timer->errata = pdata->timer_errata;

	timer->pdev = pdev;

	pm_runtime_enable(dev);

	if (!timer->reserved) {
		ret = pm_runtime_resume_and_get(dev);
		if (ret) {
			dev_err(dev, "%s: pm_runtime_get_sync failed!\n",
				__func__);
			goto err_disable;
		}
		__omap_dm_timer_init_regs(timer);
		pm_runtime_put(dev);
	}

	/* add the timer element to the list */
	spin_lock_irqsave(&dm_timer_lock, flags);
	list_add_tail(&timer->node, &omap_timer_list);
	spin_unlock_irqrestore(&dm_timer_lock, flags);

	dev_dbg(dev, "Device Probed.\n");

	return 0;

err_disable:
	pm_runtime_disable(dev);
	return ret;
}

/**
 * omap_dm_timer_remove - cleanup a registered timer device
 * @pdev:	pointer to current timer platform device
 *
 * Called by driver framework whenever a timer device is unregistered.
 * In addition to freeing platform resources it also deletes the timer
 * entry from the local list.
 */
static int omap_dm_timer_remove(struct platform_device *pdev)
{
	struct dmtimer *timer;
	unsigned long flags;
	int ret = -EINVAL;

	spin_lock_irqsave(&dm_timer_lock, flags);
	list_for_each_entry(timer, &omap_timer_list, node)
		if (!strcmp(dev_name(&timer->pdev->dev),
			    dev_name(&pdev->dev))) {
			if (!(timer->capability & OMAP_TIMER_ALWON))
				cpu_pm_unregister_notifier(&timer->nb);
			list_del(&timer->node);
			ret = 0;
			break;
		}
	spin_unlock_irqrestore(&dm_timer_lock, flags);

	pm_runtime_disable(&pdev->dev);

	return ret;
}

static const struct omap_dm_timer_ops dmtimer_ops = {
	.request_by_node = omap_dm_timer_request_by_node,
	.request_specific = omap_dm_timer_request_specific,
	.request = omap_dm_timer_request,
	.set_source = omap_dm_timer_set_source,
	.get_irq = omap_dm_timer_get_irq,
	.set_int_enable = omap_dm_timer_set_int_enable,
	.set_int_disable = omap_dm_timer_set_int_disable,
	.free = omap_dm_timer_free,
	.enable = omap_dm_timer_enable,
	.disable = omap_dm_timer_disable,
	.get_fclk = omap_dm_timer_get_fclk,
	.start = omap_dm_timer_start,
	.stop = omap_dm_timer_stop,
	.set_load = omap_dm_timer_set_load,
	.set_match = omap_dm_timer_set_match,
	.set_pwm = omap_dm_timer_set_pwm,
	.get_pwm_status = omap_dm_timer_get_pwm_status,
	.set_prescaler = omap_dm_timer_set_prescaler,
	.read_counter = omap_dm_timer_read_counter,
	.write_counter = omap_dm_timer_write_counter,
	.read_status = omap_dm_timer_read_status,
	.write_status = omap_dm_timer_write_status,
};

static const struct dmtimer_platform_data omap3plus_pdata = {
	.timer_errata = OMAP_TIMER_ERRATA_I103_I767,
	.timer_ops = &dmtimer_ops,
};

static const struct dmtimer_platform_data am6_pdata = {
	.timer_ops = &dmtimer_ops,
};

static const struct of_device_id omap_timer_match[] = {
	{
		.compatible = "ti,omap2420-timer",
	},
	{
		.compatible = "ti,omap3430-timer",
		.data = &omap3plus_pdata,
	},
	{
		.compatible = "ti,omap4430-timer",
		.data = &omap3plus_pdata,
	},
	{
		.compatible = "ti,omap5430-timer",
		.data = &omap3plus_pdata,
	},
	{
		.compatible = "ti,am335x-timer",
		.data = &omap3plus_pdata,
	},
	{
		.compatible = "ti,am335x-timer-1ms",
		.data = &omap3plus_pdata,
	},
	{
		.compatible = "ti,dm816-timer",
		.data = &omap3plus_pdata,
	},
	{
		.compatible = "ti,am654-timer",
		.data = &am6_pdata,
	},
	{},
};
MODULE_DEVICE_TABLE(of, omap_timer_match);

static struct platform_driver omap_dm_timer_driver = {
	.probe  = omap_dm_timer_probe,
	.remove = omap_dm_timer_remove,
	.driver = {
		.name   = "omap_timer",
		.of_match_table = omap_timer_match,
		.pm = &omap_dm_timer_pm_ops,
	},
};

module_platform_driver(omap_dm_timer_driver);

MODULE_DESCRIPTION("OMAP Dual-Mode Timer Driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Texas Instruments Inc");
