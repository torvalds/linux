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

#include <linux/module.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/pm_runtime.h>

#include <plat/dmtimer.h>

#include <mach/hardware.h>

static LIST_HEAD(omap_timer_list);
static DEFINE_SPINLOCK(dm_timer_lock);

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
	__raw_writel(timer->context.tiocp_cfg,
			timer->io_base + OMAP_TIMER_OCP_CFG_OFFSET);
	if (timer->revision == 1)
		__raw_writel(timer->context.tistat, timer->sys_stat);

	__raw_writel(timer->context.tisr, timer->irq_stat);
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
	__raw_writel(timer->context.tier, timer->irq_ena);
	omap_dm_timer_write_reg(timer, OMAP_TIMER_CTRL_REG,
				timer->context.tclr);
}

static void omap_dm_timer_wait_for_reset(struct omap_dm_timer *timer)
{
	int c;

	if (!timer->sys_stat)
		return;

	c = 0;
	while (!(__raw_readl(timer->sys_stat) & 1)) {
		c++;
		if (c > 100000) {
			printk(KERN_ERR "Timer failed to reset\n");
			return;
		}
	}
}

static void omap_dm_timer_reset(struct omap_dm_timer *timer)
{
	omap_dm_timer_enable(timer);
	if (timer->pdev->id != 1) {
		omap_dm_timer_write_reg(timer, OMAP_TIMER_IF_CTRL_REG, 0x06);
		omap_dm_timer_wait_for_reset(timer);
	}

	__omap_dm_timer_reset(timer, 0, 0);
	omap_dm_timer_disable(timer);
	timer->posted = 1;
}

int omap_dm_timer_prepare(struct omap_dm_timer *timer)
{
	struct dmtimer_platform_data *pdata = timer->pdev->dev.platform_data;
	int ret;

	timer->fclk = clk_get(&timer->pdev->dev, "fck");
	if (WARN_ON_ONCE(IS_ERR_OR_NULL(timer->fclk))) {
		timer->fclk = NULL;
		dev_err(&timer->pdev->dev, ": No fclk handle.\n");
		return -EINVAL;
	}

	if (pdata->needs_manual_reset)
		omap_dm_timer_reset(timer);

	ret = omap_dm_timer_set_source(timer, OMAP_TIMER_SRC_32_KHZ);

	timer->posted = 1;
	return ret;
}

struct omap_dm_timer *omap_dm_timer_request(void)
{
	struct omap_dm_timer *timer = NULL, *t;
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&dm_timer_lock, flags);
	list_for_each_entry(t, &omap_timer_list, node) {
		if (t->reserved)
			continue;

		timer = t;
		timer->reserved = 1;
		break;
	}

	if (timer) {
		ret = omap_dm_timer_prepare(timer);
		if (ret) {
			timer->reserved = 0;
			timer = NULL;
		}
	}
	spin_unlock_irqrestore(&dm_timer_lock, flags);

	if (!timer)
		pr_debug("%s: timer request failed!\n", __func__);

	return timer;
}
EXPORT_SYMBOL_GPL(omap_dm_timer_request);

struct omap_dm_timer *omap_dm_timer_request_specific(int id)
{
	struct omap_dm_timer *timer = NULL, *t;
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&dm_timer_lock, flags);
	list_for_each_entry(t, &omap_timer_list, node) {
		if (t->pdev->id == id && !t->reserved) {
			timer = t;
			timer->reserved = 1;
			break;
		}
	}

	if (timer) {
		ret = omap_dm_timer_prepare(timer);
		if (ret) {
			timer->reserved = 0;
			timer = NULL;
		}
	}
	spin_unlock_irqrestore(&dm_timer_lock, flags);

	if (!timer)
		pr_debug("%s: timer%d request failed!\n", __func__, id);

	return timer;
}
EXPORT_SYMBOL_GPL(omap_dm_timer_request_specific);

int omap_dm_timer_free(struct omap_dm_timer *timer)
{
	if (unlikely(!timer))
		return -EINVAL;

	clk_put(timer->fclk);

	WARN_ON(!timer->reserved);
	timer->reserved = 0;
	return 0;
}
EXPORT_SYMBOL_GPL(omap_dm_timer_free);

void omap_dm_timer_enable(struct omap_dm_timer *timer)
{
	pm_runtime_get_sync(&timer->pdev->dev);
}
EXPORT_SYMBOL_GPL(omap_dm_timer_enable);

void omap_dm_timer_disable(struct omap_dm_timer *timer)
{
	pm_runtime_put(&timer->pdev->dev);
}
EXPORT_SYMBOL_GPL(omap_dm_timer_disable);

int omap_dm_timer_get_irq(struct omap_dm_timer *timer)
{
	if (timer)
		return timer->irq;
	return -EINVAL;
}
EXPORT_SYMBOL_GPL(omap_dm_timer_get_irq);

#if defined(CONFIG_ARCH_OMAP1)

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
EXPORT_SYMBOL_GPL(omap_dm_timer_modify_idlect_mask);

#else

struct clk *omap_dm_timer_get_fclk(struct omap_dm_timer *timer)
{
	if (timer)
		return timer->fclk;
	return NULL;
}
EXPORT_SYMBOL_GPL(omap_dm_timer_get_fclk);

__u32 omap_dm_timer_modify_idlect_mask(__u32 inputmask)
{
	BUG();

	return 0;
}
EXPORT_SYMBOL_GPL(omap_dm_timer_modify_idlect_mask);

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
EXPORT_SYMBOL_GPL(omap_dm_timer_trigger);

int omap_dm_timer_start(struct omap_dm_timer *timer)
{
	u32 l;

	if (unlikely(!timer))
		return -EINVAL;

	omap_dm_timer_enable(timer);

	if (timer->loses_context) {
		u32 ctx_loss_cnt_after =
			timer->get_context_loss_count(&timer->pdev->dev);
		if (ctx_loss_cnt_after != timer->ctx_loss_count)
			omap_timer_restore_context(timer);
	}

	l = omap_dm_timer_read_reg(timer, OMAP_TIMER_CTRL_REG);
	if (!(l & OMAP_TIMER_CTRL_ST)) {
		l |= OMAP_TIMER_CTRL_ST;
		omap_dm_timer_write_reg(timer, OMAP_TIMER_CTRL_REG, l);
	}

	/* Save the context */
	timer->context.tclr = l;
	return 0;
}
EXPORT_SYMBOL_GPL(omap_dm_timer_start);

int omap_dm_timer_stop(struct omap_dm_timer *timer)
{
	unsigned long rate = 0;
	struct dmtimer_platform_data *pdata;

	if (unlikely(!timer))
		return -EINVAL;

	pdata = timer->pdev->dev.platform_data;
	if (!pdata->needs_manual_reset)
		rate = clk_get_rate(timer->fclk);

	__omap_dm_timer_stop(timer, timer->posted, rate);

	if (timer->loses_context && timer->get_context_loss_count)
		timer->ctx_loss_count =
			timer->get_context_loss_count(&timer->pdev->dev);

	/*
	 * Since the register values are computed and written within
	 * __omap_dm_timer_stop, we need to use read to retrieve the
	 * context.
	 */
	timer->context.tclr =
			omap_dm_timer_read_reg(timer, OMAP_TIMER_CTRL_REG);
	timer->context.tisr = __raw_readl(timer->irq_stat);
	omap_dm_timer_disable(timer);
	return 0;
}
EXPORT_SYMBOL_GPL(omap_dm_timer_stop);

int omap_dm_timer_set_source(struct omap_dm_timer *timer, int source)
{
	int ret;
	struct dmtimer_platform_data *pdata;

	if (unlikely(!timer))
		return -EINVAL;

	pdata = timer->pdev->dev.platform_data;

	if (source < 0 || source >= 3)
		return -EINVAL;

	ret = pdata->set_timer_src(timer->pdev, source);

	return ret;
}
EXPORT_SYMBOL_GPL(omap_dm_timer_set_source);

int omap_dm_timer_set_load(struct omap_dm_timer *timer, int autoreload,
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

	omap_dm_timer_write_reg(timer, OMAP_TIMER_TRIGGER_REG, 0);
	/* Save the context */
	timer->context.tclr = l;
	timer->context.tldr = load;
	omap_dm_timer_disable(timer);
	return 0;
}
EXPORT_SYMBOL_GPL(omap_dm_timer_set_load);

/* Optimized set_load which removes costly spin wait in timer_start */
int omap_dm_timer_set_load_start(struct omap_dm_timer *timer, int autoreload,
                            unsigned int load)
{
	u32 l;

	if (unlikely(!timer))
		return -EINVAL;

	omap_dm_timer_enable(timer);

	if (timer->loses_context) {
		u32 ctx_loss_cnt_after =
			timer->get_context_loss_count(&timer->pdev->dev);
		if (ctx_loss_cnt_after != timer->ctx_loss_count)
			omap_timer_restore_context(timer);
	}

	l = omap_dm_timer_read_reg(timer, OMAP_TIMER_CTRL_REG);
	if (autoreload) {
		l |= OMAP_TIMER_CTRL_AR;
		omap_dm_timer_write_reg(timer, OMAP_TIMER_LOAD_REG, load);
	} else {
		l &= ~OMAP_TIMER_CTRL_AR;
	}
	l |= OMAP_TIMER_CTRL_ST;

	__omap_dm_timer_load_start(timer, l, load, timer->posted);

	/* Save the context */
	timer->context.tclr = l;
	timer->context.tldr = load;
	timer->context.tcrr = load;
	return 0;
}
EXPORT_SYMBOL_GPL(omap_dm_timer_set_load_start);

int omap_dm_timer_set_match(struct omap_dm_timer *timer, int enable,
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
	omap_dm_timer_write_reg(timer, OMAP_TIMER_CTRL_REG, l);
	omap_dm_timer_write_reg(timer, OMAP_TIMER_MATCH_REG, match);

	/* Save the context */
	timer->context.tclr = l;
	timer->context.tmar = match;
	omap_dm_timer_disable(timer);
	return 0;
}
EXPORT_SYMBOL_GPL(omap_dm_timer_set_match);

int omap_dm_timer_set_pwm(struct omap_dm_timer *timer, int def_on,
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
EXPORT_SYMBOL_GPL(omap_dm_timer_set_pwm);

int omap_dm_timer_set_prescaler(struct omap_dm_timer *timer, int prescaler)
{
	u32 l;

	if (unlikely(!timer))
		return -EINVAL;

	omap_dm_timer_enable(timer);
	l = omap_dm_timer_read_reg(timer, OMAP_TIMER_CTRL_REG);
	l &= ~(OMAP_TIMER_CTRL_PRE | (0x07 << 2));
	if (prescaler >= 0x00 && prescaler <= 0x07) {
		l |= OMAP_TIMER_CTRL_PRE;
		l |= prescaler << 2;
	}
	omap_dm_timer_write_reg(timer, OMAP_TIMER_CTRL_REG, l);

	/* Save the context */
	timer->context.tclr = l;
	omap_dm_timer_disable(timer);
	return 0;
}
EXPORT_SYMBOL_GPL(omap_dm_timer_set_prescaler);

int omap_dm_timer_set_int_enable(struct omap_dm_timer *timer,
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
EXPORT_SYMBOL_GPL(omap_dm_timer_set_int_enable);

unsigned int omap_dm_timer_read_status(struct omap_dm_timer *timer)
{
	unsigned int l;

	if (unlikely(!timer || pm_runtime_suspended(&timer->pdev->dev))) {
		pr_err("%s: timer not available or enabled.\n", __func__);
		return 0;
	}

	l = __raw_readl(timer->irq_stat);

	return l;
}
EXPORT_SYMBOL_GPL(omap_dm_timer_read_status);

int omap_dm_timer_write_status(struct omap_dm_timer *timer, unsigned int value)
{
	if (unlikely(!timer || pm_runtime_suspended(&timer->pdev->dev)))
		return -EINVAL;

	__omap_dm_timer_write_status(timer, value);
	/* Save the context */
	timer->context.tisr = value;
	return 0;
}
EXPORT_SYMBOL_GPL(omap_dm_timer_write_status);

unsigned int omap_dm_timer_read_counter(struct omap_dm_timer *timer)
{
	if (unlikely(!timer || pm_runtime_suspended(&timer->pdev->dev))) {
		pr_err("%s: timer not iavailable or enabled.\n", __func__);
		return 0;
	}

	return __omap_dm_timer_read_counter(timer, timer->posted);
}
EXPORT_SYMBOL_GPL(omap_dm_timer_read_counter);

int omap_dm_timer_write_counter(struct omap_dm_timer *timer, unsigned int value)
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
EXPORT_SYMBOL_GPL(omap_dm_timer_write_counter);

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
EXPORT_SYMBOL_GPL(omap_dm_timers_active);

/**
 * omap_dm_timer_probe - probe function called for every registered device
 * @pdev:	pointer to current timer platform device
 *
 * Called by driver framework at the end of device registration for all
 * timer devices.
 */
static int __devinit omap_dm_timer_probe(struct platform_device *pdev)
{
	int ret;
	unsigned long flags;
	struct omap_dm_timer *timer;
	struct resource *mem, *irq, *ioarea;
	struct dmtimer_platform_data *pdata = pdev->dev.platform_data;

	if (!pdata) {
		dev_err(&pdev->dev, "%s: no platform data.\n", __func__);
		return -ENODEV;
	}

	irq = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (unlikely(!irq)) {
		dev_err(&pdev->dev, "%s: no IRQ resource.\n", __func__);
		return -ENODEV;
	}

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (unlikely(!mem)) {
		dev_err(&pdev->dev, "%s: no memory resource.\n", __func__);
		return -ENODEV;
	}

	ioarea = request_mem_region(mem->start, resource_size(mem),
			pdev->name);
	if (!ioarea) {
		dev_err(&pdev->dev, "%s: region already claimed.\n", __func__);
		return -EBUSY;
	}

	timer = kzalloc(sizeof(struct omap_dm_timer), GFP_KERNEL);
	if (!timer) {
		dev_err(&pdev->dev, "%s: no memory for omap_dm_timer.\n",
			__func__);
		ret = -ENOMEM;
		goto err_free_ioregion;
	}

	timer->io_base = ioremap(mem->start, resource_size(mem));
	if (!timer->io_base) {
		dev_err(&pdev->dev, "%s: ioremap failed.\n", __func__);
		ret = -ENOMEM;
		goto err_free_mem;
	}

	timer->id = pdev->id;
	timer->irq = irq->start;
	timer->reserved = pdata->reserved;
	timer->pdev = pdev;
	timer->loses_context = pdata->loses_context;
	timer->get_context_loss_count = pdata->get_context_loss_count;

	/* Skip pm_runtime_enable for OMAP1 */
	if (!pdata->needs_manual_reset) {
		pm_runtime_enable(&pdev->dev);
		pm_runtime_irq_safe(&pdev->dev);
	}

	if (!timer->reserved) {
		pm_runtime_get_sync(&pdev->dev);
		__omap_dm_timer_init_regs(timer);
		pm_runtime_put(&pdev->dev);
	}

	/* add the timer element to the list */
	spin_lock_irqsave(&dm_timer_lock, flags);
	list_add_tail(&timer->node, &omap_timer_list);
	spin_unlock_irqrestore(&dm_timer_lock, flags);

	dev_dbg(&pdev->dev, "Device Probed.\n");

	return 0;

err_free_mem:
	kfree(timer);

err_free_ioregion:
	release_mem_region(mem->start, resource_size(mem));

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
static int __devexit omap_dm_timer_remove(struct platform_device *pdev)
{
	struct omap_dm_timer *timer;
	unsigned long flags;
	int ret = -EINVAL;

	spin_lock_irqsave(&dm_timer_lock, flags);
	list_for_each_entry(timer, &omap_timer_list, node)
		if (timer->pdev->id == pdev->id) {
			list_del(&timer->node);
			kfree(timer);
			ret = 0;
			break;
		}
	spin_unlock_irqrestore(&dm_timer_lock, flags);

	return ret;
}

static struct platform_driver omap_dm_timer_driver = {
	.probe  = omap_dm_timer_probe,
	.remove = __devexit_p(omap_dm_timer_remove),
	.driver = {
		.name   = "omap_timer",
	},
};

static int __init omap_dm_timer_driver_init(void)
{
	return platform_driver_register(&omap_dm_timer_driver);
}

static void __exit omap_dm_timer_driver_exit(void)
{
	platform_driver_unregister(&omap_dm_timer_driver);
}

early_platform_init("earlytimer", &omap_dm_timer_driver);
module_init(omap_dm_timer_driver_init);
module_exit(omap_dm_timer_driver_exit);

MODULE_DESCRIPTION("OMAP Dual-Mode Timer Driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" DRIVER_NAME);
MODULE_AUTHOR("Texas Instruments Inc");
