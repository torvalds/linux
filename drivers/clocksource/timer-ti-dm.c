/*
 * linux/arch/arm/plat-omap/dmtimer.c
 *
 * OMAP Dual-Mode Timers
 *
 * Copyright (C) 2010 Texas Instruments Incorporated - http://www.ti.com/
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
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN
 * NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * You should have received a copy of the  GNU General Public License along
 * with this program; if not, write  to the Free Software Foundation, Inc.,
 * 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
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
 * omap_dm_timer_read_reg - read timer registers in posted and non-posted mode
 * @timer:      timer pointer over which read operation to perform
 * @reg:        lowest byte holds the register offset
 *
 * The posted mode bit is encoded in reg. Note that in posted mode write
 * pending bit must be checked. Otherwise a read of a non completed write
 * will produce an error.
 */
static inline u32 omap_dm_timer_read_reg(struct omap_dm_timer *timer, u32 reg)
{
	WARN_ON((reg & 0xff) < _OMAP_TIMER_WAKEUP_EN_OFFSET);
	return __omap_dm_timer_read(timer, reg, timer->posted);
}

/**
 * omap_dm_timer_write_reg - write timer registers in posted and non-posted mode
 * @timer:      timer pointer over which write operation is to perform
 * @reg:        lowest byte holds the register offset
 * @value:      data to write into the register
 *
 * The posted mode bit is encoded in reg. Note that in posted mode the write
 * pending bit must be checked. Otherwise a write on a register which has a
 * pending write will be lost.
 */
static void omap_dm_timer_write_reg(struct omap_dm_timer *timer, u32 reg,
						u32 value)
{
	WARN_ON((reg & 0xff) < _OMAP_TIMER_WAKEUP_EN_OFFSET);
	__omap_dm_timer_write(timer, reg, value, timer->posted);
}

static void omap_timer_restore_context(struct omap_dm_timer *timer)
{
	omap_dm_timer_write_reg(timer, OMAP_TIMER_WAKEUP_EN_REG,
				timer->context.twer);
	omap_dm_timer_write_reg(timer, OMAP_TIMER_COUNTER_REG,
				timer->context.tcrr);
	omap_dm_timer_write_reg(timer, OMAP_TIMER_LOAD_REG,
				timer->context.tldr);
	omap_dm_timer_write_reg(timer, OMAP_TIMER_MATCH_REG,
				timer->context.tmar);
	omap_dm_timer_write_reg(timer, OMAP_TIMER_IF_CTRL_REG,
				timer->context.tsicr);
	writel_relaxed(timer->context.tier, timer->irq_ena);
	omap_dm_timer_write_reg(timer, OMAP_TIMER_CTRL_REG,
				timer->context.tclr);
}

static int omap_dm_timer_reset(struct omap_dm_timer *timer)
{
	u32 l, timeout = 100000;

	if (timer->revision != 1)
		return -EINVAL;

	omap_dm_timer_write_reg(timer, OMAP_TIMER_IF_CTRL_REG, 0x06);

	do {
		l = __omap_dm_timer_read(timer,
					 OMAP_TIMER_V1_SYS_STAT_OFFSET, 0);
	} while (!l && timeout--);

	if (!timeout) {
		dev_err(&timer->pdev->dev, "Timer failed to reset\n");
		return -ETIMEDOUT;
	}

	/* Configure timer for smart-idle mode */
	l = __omap_dm_timer_read(timer, OMAP_TIMER_OCP_CFG_OFFSET, 0);
	l |= 0x2 << 0x3;
	__omap_dm_timer_write(timer, OMAP_TIMER_OCP_CFG_OFFSET, l, 0);

	timer->posted = 0;

	return 0;
}

static int omap_dm_timer_set_source(struct omap_dm_timer *timer, int source)
{
	int ret;
	const char *parent_name;
	struct clk *parent;
	struct dmtimer_platform_data *pdata;

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
	if (pdata && pdata->set_timer_src)
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

static void omap_dm_timer_enable(struct omap_dm_timer *timer)
{
	int c;

	pm_runtime_get_sync(&timer->pdev->dev);

	if (!(timer->capability & OMAP_TIMER_ALWON)) {
		if (timer->get_context_loss_count) {
			c = timer->get_context_loss_count(&timer->pdev->dev);
			if (c != timer->ctx_loss_count) {
				omap_timer_restore_context(timer);
				timer->ctx_loss_count = c;
			}
		} else {
			omap_timer_restore_context(timer);
		}
	}
}

static void omap_dm_timer_disable(struct omap_dm_timer *timer)
{
	pm_runtime_put_sync(&timer->pdev->dev);
}

static int omap_dm_timer_prepare(struct omap_dm_timer *timer)
{
	int rc;

	/*
	 * FIXME: OMAP1 devices do not use the clock framework for dmtimers so
	 * do not call clk_get() for these devices.
	 */
	if (!(timer->capability & OMAP_TIMER_NEEDS_RESET)) {
		timer->fclk = clk_get(&timer->pdev->dev, "fck");
		if (WARN_ON_ONCE(IS_ERR(timer->fclk))) {
			dev_err(&timer->pdev->dev, ": No fclk handle.\n");
			return -EINVAL;
		}
	}

	omap_dm_timer_enable(timer);

	if (timer->capability & OMAP_TIMER_NEEDS_RESET) {
		rc = omap_dm_timer_reset(timer);
		if (rc) {
			omap_dm_timer_disable(timer);
			return rc;
		}
	}

	__omap_dm_timer_enable_posted(timer);
	omap_dm_timer_disable(timer);

	rc = omap_dm_timer_set_source(timer, OMAP_TIMER_SRC_32_KHZ);

	return rc;
}

static inline u32 omap_dm_timer_reserved_systimer(int id)
{
	return (omap_reserved_systimers & (1 << (id - 1))) ? 1 : 0;
}

int omap_dm_timer_reserve_systimer(int id)
{
	if (omap_dm_timer_reserved_systimer(id))
		return -ENODEV;

	omap_reserved_systimers |= (1 << (id - 1));

	return 0;
}

static struct omap_dm_timer *_omap_dm_timer_request(int req_type, void *data)
{
	struct omap_dm_timer *timer = NULL, *t;
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
	return _omap_dm_timer_request(REQUEST_ANY, NULL);
}

static struct omap_dm_timer *omap_dm_timer_request_specific(int id)
{
	/* Requesting timer by ID is not supported when device tree is used */
	if (of_have_populated_dt()) {
		pr_warn("%s: Please use omap_dm_timer_request_by_node()\n",
			__func__);
		return NULL;
	}

	return _omap_dm_timer_request(REQUEST_BY_ID, &id);
}

/**
 * omap_dm_timer_request_by_cap - Request a timer by capability
 * @cap:	Bit mask of capabilities to match
 *
 * Find a timer based upon capabilities bit mask. Callers of this function
 * should use the definitions found in the plat/dmtimer.h file under the
 * comment "timer capabilities used in hwmod database". Returns pointer to
 * timer handle on success and a NULL pointer on failure.
 */
struct omap_dm_timer *omap_dm_timer_request_by_cap(u32 cap)
{
	return _omap_dm_timer_request(REQUEST_BY_CAP, &cap);
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
	if (!np)
		return NULL;

	return _omap_dm_timer_request(REQUEST_BY_NODE, np);
}

static int omap_dm_timer_free(struct omap_dm_timer *timer)
{
	if (unlikely(!timer))
		return -EINVAL;

	clk_put(timer->fclk);

	WARN_ON(!timer->reserved);
	timer->reserved = 0;
	return 0;
}

int omap_dm_timer_get_irq(struct omap_dm_timer *timer)
{
	if (timer)
		return timer->irq;
	return -EINVAL;
}

#if defined(CONFIG_ARCH_OMAP1)
#include <mach/hardware.h>

static struct clk *omap_dm_timer_get_fclk(struct omap_dm_timer *timer)
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
	struct omap_dm_timer *timer = NULL;
	unsigned long flags;

	/* If ARMXOR cannot be idled this function call is unnecessary */
	if (!(inputmask & (1 << 1)))
		return inputmask;

	/* If any active timer is using ARMXOR return modified mask */
	spin_lock_irqsave(&dm_timer_lock, flags);
	list_for_each_entry(timer, &omap_timer_list, node) {
		u32 l;

		l = omap_dm_timer_read_reg(timer, OMAP_TIMER_CTRL_REG);
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

static struct clk *omap_dm_timer_get_fclk(struct omap_dm_timer *timer)
{
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

int omap_dm_timer_trigger(struct omap_dm_timer *timer)
{
	if (unlikely(!timer || pm_runtime_suspended(&timer->pdev->dev))) {
		pr_err("%s: timer not available or enabled.\n", __func__);
		return -EINVAL;
	}

	omap_dm_timer_write_reg(timer, OMAP_TIMER_TRIGGER_REG, 0);
	return 0;
}

static int omap_dm_timer_start(struct omap_dm_timer *timer)
{
	u32 l;

	if (unlikely(!timer))
		return -EINVAL;

	omap_dm_timer_enable(timer);

	l = omap_dm_timer_read_reg(timer, OMAP_TIMER_CTRL_REG);
	if (!(l & OMAP_TIMER_CTRL_ST)) {
		l |= OMAP_TIMER_CTRL_ST;
		omap_dm_timer_write_reg(timer, OMAP_TIMER_CTRL_REG, l);
	}

	/* Save the context */
	timer->context.tclr = l;
	return 0;
}

static int omap_dm_timer_stop(struct omap_dm_timer *timer)
{
	unsigned long rate = 0;

	if (unlikely(!timer))
		return -EINVAL;

	if (!(timer->capability & OMAP_TIMER_NEEDS_RESET))
		rate = clk_get_rate(timer->fclk);

	__omap_dm_timer_stop(timer, timer->posted, rate);

	/*
	 * Since the register values are computed and written within
	 * __omap_dm_timer_stop, we need to use read to retrieve the
	 * context.
	 */
	timer->context.tclr =
			omap_dm_timer_read_reg(timer, OMAP_TIMER_CTRL_REG);
	omap_dm_timer_disable(timer);
	return 0;
}

static int omap_dm_timer_set_load(struct omap_dm_timer *timer, int autoreload,
				  unsigned int load)
{
	u32 l;

	if (unlikely(!timer))
		return -EINVAL;

	omap_dm_timer_enable(timer);
	l = omap_dm_timer_read_reg(timer, OMAP_TIMER_CTRL_REG);
	if (autoreload)
		l |= OMAP_TIMER_CTRL_AR;
	else
		l &= ~OMAP_TIMER_CTRL_AR;
	omap_dm_timer_write_reg(timer, OMAP_TIMER_CTRL_REG, l);
	omap_dm_timer_write_reg(timer, OMAP_TIMER_LOAD_REG, load);

	/* Save the context */
	timer->context.tclr = l;
	timer->context.tldr = load;
	omap_dm_timer_disable(timer);
	return 0;
}

static int omap_dm_timer_set_match(struct omap_dm_timer *timer, int enable,
				   unsigned int match)
{
	u32 l;

	if (unlikely(!timer))
		return -EINVAL;

	omap_dm_timer_enable(timer);
	l = omap_dm_timer_read_reg(timer, OMAP_TIMER_CTRL_REG);
	if (enable)
		l |= OMAP_TIMER_CTRL_CE;
	else
		l &= ~OMAP_TIMER_CTRL_CE;
	omap_dm_timer_write_reg(timer, OMAP_TIMER_MATCH_REG, match);
	omap_dm_timer_write_reg(timer, OMAP_TIMER_CTRL_REG, l);

	/* Save the context */
	timer->context.tclr = l;
	timer->context.tmar = match;
	omap_dm_timer_disable(timer);
	return 0;
}

static int omap_dm_timer_set_pwm(struct omap_dm_timer *timer, int def_on,
				 int toggle, int trigger)
{
	u32 l;

	if (unlikely(!timer))
		return -EINVAL;

	omap_dm_timer_enable(timer);
	l = omap_dm_timer_read_reg(timer, OMAP_TIMER_CTRL_REG);
	l &= ~(OMAP_TIMER_CTRL_GPOCFG | OMAP_TIMER_CTRL_SCPWM |
	       OMAP_TIMER_CTRL_PT | (0x03 << 10));
	if (def_on)
		l |= OMAP_TIMER_CTRL_SCPWM;
	if (toggle)
		l |= OMAP_TIMER_CTRL_PT;
	l |= trigger << 10;
	omap_dm_timer_write_reg(timer, OMAP_TIMER_CTRL_REG, l);

	/* Save the context */
	timer->context.tclr = l;
	omap_dm_timer_disable(timer);
	return 0;
}

static int omap_dm_timer_set_prescaler(struct omap_dm_timer *timer,
					int prescaler)
{
	u32 l;

	if (unlikely(!timer) || prescaler < -1 || prescaler > 7)
		return -EINVAL;

	omap_dm_timer_enable(timer);
	l = omap_dm_timer_read_reg(timer, OMAP_TIMER_CTRL_REG);
	l &= ~(OMAP_TIMER_CTRL_PRE | (0x07 << 2));
	if (prescaler >= 0) {
		l |= OMAP_TIMER_CTRL_PRE;
		l |= prescaler << 2;
	}
	omap_dm_timer_write_reg(timer, OMAP_TIMER_CTRL_REG, l);

	/* Save the context */
	timer->context.tclr = l;
	omap_dm_timer_disable(timer);
	return 0;
}

static int omap_dm_timer_set_int_enable(struct omap_dm_timer *timer,
					unsigned int value)
{
	if (unlikely(!timer))
		return -EINVAL;

	omap_dm_timer_enable(timer);
	__omap_dm_timer_int_enable(timer, value);

	/* Save the context */
	timer->context.tier = value;
	timer->context.twer = value;
	omap_dm_timer_disable(timer);
	return 0;
}

/**
 * omap_dm_timer_set_int_disable - disable timer interrupts
 * @timer:	pointer to timer handle
 * @mask:	bit mask of interrupts to be disabled
 *
 * Disables the specified timer interrupts for a timer.
 */
static int omap_dm_timer_set_int_disable(struct omap_dm_timer *timer, u32 mask)
{
	u32 l = mask;

	if (unlikely(!timer))
		return -EINVAL;

	omap_dm_timer_enable(timer);

	if (timer->revision == 1)
		l = readl_relaxed(timer->irq_ena) & ~mask;

	writel_relaxed(l, timer->irq_dis);
	l = omap_dm_timer_read_reg(timer, OMAP_TIMER_WAKEUP_EN_REG) & ~mask;
	omap_dm_timer_write_reg(timer, OMAP_TIMER_WAKEUP_EN_REG, l);

	/* Save the context */
	timer->context.tier &= ~mask;
	timer->context.twer &= ~mask;
	omap_dm_timer_disable(timer);
	return 0;
}

static unsigned int omap_dm_timer_read_status(struct omap_dm_timer *timer)
{
	unsigned int l;

	if (unlikely(!timer || pm_runtime_suspended(&timer->pdev->dev))) {
		pr_err("%s: timer not available or enabled.\n", __func__);
		return 0;
	}

	l = readl_relaxed(timer->irq_stat);

	return l;
}

static int omap_dm_timer_write_status(struct omap_dm_timer *timer, unsigned int value)
{
	if (unlikely(!timer || pm_runtime_suspended(&timer->pdev->dev)))
		return -EINVAL;

	__omap_dm_timer_write_status(timer, value);

	return 0;
}

static unsigned int omap_dm_timer_read_counter(struct omap_dm_timer *timer)
{
	if (unlikely(!timer || pm_runtime_suspended(&timer->pdev->dev))) {
		pr_err("%s: timer not iavailable or enabled.\n", __func__);
		return 0;
	}

	return __omap_dm_timer_read_counter(timer, timer->posted);
}

static int omap_dm_timer_write_counter(struct omap_dm_timer *timer, unsigned int value)
{
	if (unlikely(!timer || pm_runtime_suspended(&timer->pdev->dev))) {
		pr_err("%s: timer not available or enabled.\n", __func__);
		return -EINVAL;
	}

	omap_dm_timer_write_reg(timer, OMAP_TIMER_COUNTER_REG, value);

	/* Save the context */
	timer->context.tcrr = value;
	return 0;
}

int omap_dm_timers_active(void)
{
	struct omap_dm_timer *timer;

	list_for_each_entry(timer, &omap_timer_list, node) {
		if (!timer->reserved)
			continue;

		if (omap_dm_timer_read_reg(timer, OMAP_TIMER_CTRL_REG) &
		    OMAP_TIMER_CTRL_ST) {
			return 1;
		}
	}
	return 0;
}

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
	struct omap_dm_timer *timer;
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

	timer->fclk = ERR_PTR(-ENODEV);
	timer->io_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(timer->io_base))
		return PTR_ERR(timer->io_base);

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
		timer->get_context_loss_count = pdata->get_context_loss_count;
	}

	if (pdata)
		timer->errata = pdata->timer_errata;

	timer->pdev = pdev;

	pm_runtime_enable(dev);

	if (!timer->reserved) {
		ret = pm_runtime_get_sync(dev);
		if (ret < 0) {
			dev_err(dev, "%s: pm_runtime_get_sync failed!\n",
				__func__);
			goto err_get_sync;
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

err_get_sync:
	pm_runtime_put_noidle(dev);
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
	struct omap_dm_timer *timer;
	unsigned long flags;
	int ret = -EINVAL;

	spin_lock_irqsave(&dm_timer_lock, flags);
	list_for_each_entry(timer, &omap_timer_list, node)
		if (!strcmp(dev_name(&timer->pdev->dev),
			    dev_name(&pdev->dev))) {
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
	{},
};
MODULE_DEVICE_TABLE(of, omap_timer_match);

static struct platform_driver omap_dm_timer_driver = {
	.probe  = omap_dm_timer_probe,
	.remove = omap_dm_timer_remove,
	.driver = {
		.name   = "omap_timer",
		.of_match_table = of_match_ptr(omap_timer_match),
	},
};

module_platform_driver(omap_dm_timer_driver);

MODULE_DESCRIPTION("OMAP Dual-Mode Timer Driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Texas Instruments Inc");
