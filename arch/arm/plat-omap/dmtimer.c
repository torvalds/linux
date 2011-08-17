/*
 * linux/arch/arm/plat-omap/dmtimer.c
 *
 * OMAP Dual-Mode Timers
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

#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/errno.h>
#include <linux/list.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/module.h>
#include <mach/hardware.h>
#include <plat/dmtimer.h>
#include <mach/irqs.h>

static int dm_timer_count;

#ifdef CONFIG_ARCH_OMAP1
static struct omap_dm_timer omap1_dm_timers[] = {
	{ .phys_base = 0xfffb1400, .irq = INT_1610_GPTIMER1 },
	{ .phys_base = 0xfffb1c00, .irq = INT_1610_GPTIMER2 },
	{ .phys_base = 0xfffb2400, .irq = INT_1610_GPTIMER3 },
	{ .phys_base = 0xfffb2c00, .irq = INT_1610_GPTIMER4 },
	{ .phys_base = 0xfffb3400, .irq = INT_1610_GPTIMER5 },
	{ .phys_base = 0xfffb3c00, .irq = INT_1610_GPTIMER6 },
	{ .phys_base = 0xfffb7400, .irq = INT_1610_GPTIMER7 },
	{ .phys_base = 0xfffbd400, .irq = INT_1610_GPTIMER8 },
};

static const int omap1_dm_timer_count = ARRAY_SIZE(omap1_dm_timers);

#else
#define omap1_dm_timers			NULL
#define omap1_dm_timer_count		0
#endif	/* CONFIG_ARCH_OMAP1 */

#ifdef CONFIG_ARCH_OMAP2
static struct omap_dm_timer omap2_dm_timers[] = {
	{ .phys_base = 0x48028000, .irq = INT_24XX_GPTIMER1 },
	{ .phys_base = 0x4802a000, .irq = INT_24XX_GPTIMER2 },
	{ .phys_base = 0x48078000, .irq = INT_24XX_GPTIMER3 },
	{ .phys_base = 0x4807a000, .irq = INT_24XX_GPTIMER4 },
	{ .phys_base = 0x4807c000, .irq = INT_24XX_GPTIMER5 },
	{ .phys_base = 0x4807e000, .irq = INT_24XX_GPTIMER6 },
	{ .phys_base = 0x48080000, .irq = INT_24XX_GPTIMER7 },
	{ .phys_base = 0x48082000, .irq = INT_24XX_GPTIMER8 },
	{ .phys_base = 0x48084000, .irq = INT_24XX_GPTIMER9 },
	{ .phys_base = 0x48086000, .irq = INT_24XX_GPTIMER10 },
	{ .phys_base = 0x48088000, .irq = INT_24XX_GPTIMER11 },
	{ .phys_base = 0x4808a000, .irq = INT_24XX_GPTIMER12 },
};

static const char *omap2_dm_source_names[] __initdata = {
	"sys_ck",
	"func_32k_ck",
	"alt_ck",
	NULL
};

static struct clk *omap2_dm_source_clocks[3];
static const int omap2_dm_timer_count = ARRAY_SIZE(omap2_dm_timers);

#else
#define omap2_dm_timers			NULL
#define omap2_dm_timer_count		0
#define omap2_dm_source_names		NULL
#define omap2_dm_source_clocks		NULL
#endif	/* CONFIG_ARCH_OMAP2 */

#ifdef CONFIG_ARCH_OMAP3
static struct omap_dm_timer omap3_dm_timers[] = {
	{ .phys_base = 0x48318000, .irq = INT_24XX_GPTIMER1 },
	{ .phys_base = 0x49032000, .irq = INT_24XX_GPTIMER2 },
	{ .phys_base = 0x49034000, .irq = INT_24XX_GPTIMER3 },
	{ .phys_base = 0x49036000, .irq = INT_24XX_GPTIMER4 },
	{ .phys_base = 0x49038000, .irq = INT_24XX_GPTIMER5 },
	{ .phys_base = 0x4903A000, .irq = INT_24XX_GPTIMER6 },
	{ .phys_base = 0x4903C000, .irq = INT_24XX_GPTIMER7 },
	{ .phys_base = 0x4903E000, .irq = INT_24XX_GPTIMER8 },
	{ .phys_base = 0x49040000, .irq = INT_24XX_GPTIMER9 },
	{ .phys_base = 0x48086000, .irq = INT_24XX_GPTIMER10 },
	{ .phys_base = 0x48088000, .irq = INT_24XX_GPTIMER11 },
	{ .phys_base = 0x48304000, .irq = INT_34XX_GPT12_IRQ },
};

static const char *omap3_dm_source_names[] __initdata = {
	"sys_ck",
	"omap_32k_fck",
	NULL
};

static struct clk *omap3_dm_source_clocks[2];
static const int omap3_dm_timer_count = ARRAY_SIZE(omap3_dm_timers);

#else
#define omap3_dm_timers			NULL
#define omap3_dm_timer_count		0
#define omap3_dm_source_names		NULL
#define omap3_dm_source_clocks		NULL
#endif	/* CONFIG_ARCH_OMAP3 */

#ifdef CONFIG_ARCH_OMAP4
static struct omap_dm_timer omap4_dm_timers[] = {
	{ .phys_base = 0x4a318000, .irq = OMAP44XX_IRQ_GPT1 },
	{ .phys_base = 0x48032000, .irq = OMAP44XX_IRQ_GPT2 },
	{ .phys_base = 0x48034000, .irq = OMAP44XX_IRQ_GPT3 },
	{ .phys_base = 0x48036000, .irq = OMAP44XX_IRQ_GPT4 },
	{ .phys_base = 0x40138000, .irq = OMAP44XX_IRQ_GPT5 },
	{ .phys_base = 0x4013a000, .irq = OMAP44XX_IRQ_GPT6 },
	{ .phys_base = 0x4013a000, .irq = OMAP44XX_IRQ_GPT7 },
	{ .phys_base = 0x4013e000, .irq = OMAP44XX_IRQ_GPT8 },
	{ .phys_base = 0x4803e000, .irq = OMAP44XX_IRQ_GPT9 },
	{ .phys_base = 0x48086000, .irq = OMAP44XX_IRQ_GPT10 },
	{ .phys_base = 0x48088000, .irq = OMAP44XX_IRQ_GPT11 },
	{ .phys_base = 0x4a320000, .irq = OMAP44XX_IRQ_GPT12 },
};
static const char *omap4_dm_source_names[] __initdata = {
	"sys_clkin_ck",
	"sys_32k_ck",
	NULL
};
static struct clk *omap4_dm_source_clocks[2];
static const int omap4_dm_timer_count = ARRAY_SIZE(omap4_dm_timers);

#else
#define omap4_dm_timers			NULL
#define omap4_dm_timer_count		0
#define omap4_dm_source_names		NULL
#define omap4_dm_source_clocks		NULL
#endif	/* CONFIG_ARCH_OMAP4 */

static struct omap_dm_timer *dm_timers;
static const char **dm_source_names;
static struct clk **dm_source_clocks;

static spinlock_t dm_timer_lock;

/*
 * Reads timer registers in posted and non-posted mode. The posted mode bit
 * is encoded in reg. Note that in posted mode write pending bit must be
 * checked. Otherwise a read of a non completed write will produce an error.
 */
static inline u32 omap_dm_timer_read_reg(struct omap_dm_timer *timer, u32 reg)
{
	return __omap_dm_timer_read(timer->io_base, reg, timer->posted);
}

/*
 * Writes timer registers in posted and non-posted mode. The posted mode bit
 * is encoded in reg. Note that in posted mode the write pending bit must be
 * checked. Otherwise a write on a register which has a pending write will be
 * lost.
 */
static void omap_dm_timer_write_reg(struct omap_dm_timer *timer, u32 reg,
						u32 value)
{
	__omap_dm_timer_write(timer->io_base, reg, value, timer->posted);
}

static void omap_dm_timer_wait_for_reset(struct omap_dm_timer *timer)
{
	int c;

	c = 0;
	while (!(omap_dm_timer_read_reg(timer, OMAP_TIMER_SYS_STAT_REG) & 1)) {
		c++;
		if (c > 100000) {
			printk(KERN_ERR "Timer failed to reset\n");
			return;
		}
	}
}

static void omap_dm_timer_reset(struct omap_dm_timer *timer)
{
	int autoidle = 0, wakeup = 0;

	if (!cpu_class_is_omap2() || timer != &dm_timers[0]) {
		omap_dm_timer_write_reg(timer, OMAP_TIMER_IF_CTRL_REG, 0x06);
		omap_dm_timer_wait_for_reset(timer);
	}
	omap_dm_timer_set_source(timer, OMAP_TIMER_SRC_32_KHZ);

	/* Enable autoidle on OMAP2+ */
	if (cpu_class_is_omap2())
		autoidle = 1;

	/*
	 * Enable wake-up on OMAP2 CPUs.
	 */
	if (cpu_class_is_omap2())
		wakeup = 1;

	__omap_dm_timer_reset(timer->io_base, autoidle, wakeup);
	timer->posted = 1;
}

void omap_dm_timer_prepare(struct omap_dm_timer *timer)
{
	omap_dm_timer_enable(timer);
	omap_dm_timer_reset(timer);
}

struct omap_dm_timer *omap_dm_timer_request(void)
{
	struct omap_dm_timer *timer = NULL;
	unsigned long flags;
	int i;

	spin_lock_irqsave(&dm_timer_lock, flags);
	for (i = 0; i < dm_timer_count; i++) {
		if (dm_timers[i].reserved)
			continue;

		timer = &dm_timers[i];
		timer->reserved = 1;
		break;
	}
	spin_unlock_irqrestore(&dm_timer_lock, flags);

	if (timer != NULL)
		omap_dm_timer_prepare(timer);

	return timer;
}
EXPORT_SYMBOL_GPL(omap_dm_timer_request);

struct omap_dm_timer *omap_dm_timer_request_specific(int id)
{
	struct omap_dm_timer *timer;
	unsigned long flags;

	spin_lock_irqsave(&dm_timer_lock, flags);
	if (id <= 0 || id > dm_timer_count || dm_timers[id-1].reserved) {
		spin_unlock_irqrestore(&dm_timer_lock, flags);
		printk("BUG: warning at %s:%d/%s(): unable to get timer %d\n",
		       __FILE__, __LINE__, __func__, id);
		dump_stack();
		return NULL;
	}

	timer = &dm_timers[id-1];
	timer->reserved = 1;
	spin_unlock_irqrestore(&dm_timer_lock, flags);

	omap_dm_timer_prepare(timer);

	return timer;
}
EXPORT_SYMBOL_GPL(omap_dm_timer_request_specific);

void omap_dm_timer_free(struct omap_dm_timer *timer)
{
	omap_dm_timer_enable(timer);
	omap_dm_timer_reset(timer);
	omap_dm_timer_disable(timer);

	WARN_ON(!timer->reserved);
	timer->reserved = 0;
}
EXPORT_SYMBOL_GPL(omap_dm_timer_free);

void omap_dm_timer_enable(struct omap_dm_timer *timer)
{
	if (timer->enabled)
		return;

#ifdef CONFIG_ARCH_OMAP2PLUS
	if (cpu_class_is_omap2()) {
		clk_enable(timer->fclk);
		clk_enable(timer->iclk);
	}
#endif

	timer->enabled = 1;
}
EXPORT_SYMBOL_GPL(omap_dm_timer_enable);

void omap_dm_timer_disable(struct omap_dm_timer *timer)
{
	if (!timer->enabled)
		return;

#ifdef CONFIG_ARCH_OMAP2PLUS
	if (cpu_class_is_omap2()) {
		clk_disable(timer->iclk);
		clk_disable(timer->fclk);
	}
#endif

	timer->enabled = 0;
}
EXPORT_SYMBOL_GPL(omap_dm_timer_disable);

int omap_dm_timer_get_irq(struct omap_dm_timer *timer)
{
	return timer->irq;
}
EXPORT_SYMBOL_GPL(omap_dm_timer_get_irq);

#if defined(CONFIG_ARCH_OMAP1)

/**
 * omap_dm_timer_modify_idlect_mask - Check if any running timers use ARMXOR
 * @inputmask: current value of idlect mask
 */
__u32 omap_dm_timer_modify_idlect_mask(__u32 inputmask)
{
	int i;

	/* If ARMXOR cannot be idled this function call is unnecessary */
	if (!(inputmask & (1 << 1)))
		return inputmask;

	/* If any active timer is using ARMXOR return modified mask */
	for (i = 0; i < dm_timer_count; i++) {
		u32 l;

		l = omap_dm_timer_read_reg(&dm_timers[i], OMAP_TIMER_CTRL_REG);
		if (l & OMAP_TIMER_CTRL_ST) {
			if (((omap_readl(MOD_CONF_CTRL_1) >> (i * 2)) & 0x03) == 0)
				inputmask &= ~(1 << 1);
			else
				inputmask &= ~(1 << 2);
		}
	}

	return inputmask;
}
EXPORT_SYMBOL_GPL(omap_dm_timer_modify_idlect_mask);

#else

struct clk *omap_dm_timer_get_fclk(struct omap_dm_timer *timer)
{
	return timer->fclk;
}
EXPORT_SYMBOL_GPL(omap_dm_timer_get_fclk);

__u32 omap_dm_timer_modify_idlect_mask(__u32 inputmask)
{
	BUG();

	return 0;
}
EXPORT_SYMBOL_GPL(omap_dm_timer_modify_idlect_mask);

#endif

void omap_dm_timer_trigger(struct omap_dm_timer *timer)
{
	omap_dm_timer_write_reg(timer, OMAP_TIMER_TRIGGER_REG, 0);
}
EXPORT_SYMBOL_GPL(omap_dm_timer_trigger);

void omap_dm_timer_start(struct omap_dm_timer *timer)
{
	u32 l;

	l = omap_dm_timer_read_reg(timer, OMAP_TIMER_CTRL_REG);
	if (!(l & OMAP_TIMER_CTRL_ST)) {
		l |= OMAP_TIMER_CTRL_ST;
		omap_dm_timer_write_reg(timer, OMAP_TIMER_CTRL_REG, l);
	}
}
EXPORT_SYMBOL_GPL(omap_dm_timer_start);

void omap_dm_timer_stop(struct omap_dm_timer *timer)
{
	unsigned long rate = 0;

#ifdef CONFIG_ARCH_OMAP2PLUS
	rate = clk_get_rate(timer->fclk);
#endif

	__omap_dm_timer_stop(timer->io_base, timer->posted, rate);
}
EXPORT_SYMBOL_GPL(omap_dm_timer_stop);

#ifdef CONFIG_ARCH_OMAP1

int omap_dm_timer_set_source(struct omap_dm_timer *timer, int source)
{
	int n = (timer - dm_timers) << 1;
	u32 l;

	l = omap_readl(MOD_CONF_CTRL_1) & ~(0x03 << n);
	l |= source << n;
	omap_writel(l, MOD_CONF_CTRL_1);

	return 0;
}
EXPORT_SYMBOL_GPL(omap_dm_timer_set_source);

#else

int omap_dm_timer_set_source(struct omap_dm_timer *timer, int source)
{
	if (source < 0 || source >= 3)
		return -EINVAL;

	return __omap_dm_timer_set_source(timer->fclk,
						dm_source_clocks[source]);
}
EXPORT_SYMBOL_GPL(omap_dm_timer_set_source);

#endif

void omap_dm_timer_set_load(struct omap_dm_timer *timer, int autoreload,
			    unsigned int load)
{
	u32 l;

	l = omap_dm_timer_read_reg(timer, OMAP_TIMER_CTRL_REG);
	if (autoreload)
		l |= OMAP_TIMER_CTRL_AR;
	else
		l &= ~OMAP_TIMER_CTRL_AR;
	omap_dm_timer_write_reg(timer, OMAP_TIMER_CTRL_REG, l);
	omap_dm_timer_write_reg(timer, OMAP_TIMER_LOAD_REG, load);

	omap_dm_timer_write_reg(timer, OMAP_TIMER_TRIGGER_REG, 0);
}
EXPORT_SYMBOL_GPL(omap_dm_timer_set_load);

/* Optimized set_load which removes costly spin wait in timer_start */
void omap_dm_timer_set_load_start(struct omap_dm_timer *timer, int autoreload,
                            unsigned int load)
{
	u32 l;

	l = omap_dm_timer_read_reg(timer, OMAP_TIMER_CTRL_REG);
	if (autoreload) {
		l |= OMAP_TIMER_CTRL_AR;
		omap_dm_timer_write_reg(timer, OMAP_TIMER_LOAD_REG, load);
	} else {
		l &= ~OMAP_TIMER_CTRL_AR;
	}
	l |= OMAP_TIMER_CTRL_ST;

	__omap_dm_timer_load_start(timer->io_base, l, load, timer->posted);
}
EXPORT_SYMBOL_GPL(omap_dm_timer_set_load_start);

void omap_dm_timer_set_match(struct omap_dm_timer *timer, int enable,
			     unsigned int match)
{
	u32 l;

	l = omap_dm_timer_read_reg(timer, OMAP_TIMER_CTRL_REG);
	if (enable)
		l |= OMAP_TIMER_CTRL_CE;
	else
		l &= ~OMAP_TIMER_CTRL_CE;
	omap_dm_timer_write_reg(timer, OMAP_TIMER_CTRL_REG, l);
	omap_dm_timer_write_reg(timer, OMAP_TIMER_MATCH_REG, match);
}
EXPORT_SYMBOL_GPL(omap_dm_timer_set_match);

void omap_dm_timer_set_pwm(struct omap_dm_timer *timer, int def_on,
			   int toggle, int trigger)
{
	u32 l;

	l = omap_dm_timer_read_reg(timer, OMAP_TIMER_CTRL_REG);
	l &= ~(OMAP_TIMER_CTRL_GPOCFG | OMAP_TIMER_CTRL_SCPWM |
	       OMAP_TIMER_CTRL_PT | (0x03 << 10));
	if (def_on)
		l |= OMAP_TIMER_CTRL_SCPWM;
	if (toggle)
		l |= OMAP_TIMER_CTRL_PT;
	l |= trigger << 10;
	omap_dm_timer_write_reg(timer, OMAP_TIMER_CTRL_REG, l);
}
EXPORT_SYMBOL_GPL(omap_dm_timer_set_pwm);

void omap_dm_timer_set_prescaler(struct omap_dm_timer *timer, int prescaler)
{
	u32 l;

	l = omap_dm_timer_read_reg(timer, OMAP_TIMER_CTRL_REG);
	l &= ~(OMAP_TIMER_CTRL_PRE | (0x07 << 2));
	if (prescaler >= 0x00 && prescaler <= 0x07) {
		l |= OMAP_TIMER_CTRL_PRE;
		l |= prescaler << 2;
	}
	omap_dm_timer_write_reg(timer, OMAP_TIMER_CTRL_REG, l);
}
EXPORT_SYMBOL_GPL(omap_dm_timer_set_prescaler);

void omap_dm_timer_set_int_enable(struct omap_dm_timer *timer,
				  unsigned int value)
{
	__omap_dm_timer_int_enable(timer->io_base, value);
}
EXPORT_SYMBOL_GPL(omap_dm_timer_set_int_enable);

unsigned int omap_dm_timer_read_status(struct omap_dm_timer *timer)
{
	unsigned int l;

	l = omap_dm_timer_read_reg(timer, OMAP_TIMER_STAT_REG);

	return l;
}
EXPORT_SYMBOL_GPL(omap_dm_timer_read_status);

void omap_dm_timer_write_status(struct omap_dm_timer *timer, unsigned int value)
{
	__omap_dm_timer_write_status(timer->io_base, value);
}
EXPORT_SYMBOL_GPL(omap_dm_timer_write_status);

unsigned int omap_dm_timer_read_counter(struct omap_dm_timer *timer)
{
	return __omap_dm_timer_read_counter(timer->io_base, timer->posted);
}
EXPORT_SYMBOL_GPL(omap_dm_timer_read_counter);

void omap_dm_timer_write_counter(struct omap_dm_timer *timer, unsigned int value)
{
	omap_dm_timer_write_reg(timer, OMAP_TIMER_COUNTER_REG, value);
}
EXPORT_SYMBOL_GPL(omap_dm_timer_write_counter);

int omap_dm_timers_active(void)
{
	int i;

	for (i = 0; i < dm_timer_count; i++) {
		struct omap_dm_timer *timer;

		timer = &dm_timers[i];

		if (!timer->enabled)
			continue;

		if (omap_dm_timer_read_reg(timer, OMAP_TIMER_CTRL_REG) &
		    OMAP_TIMER_CTRL_ST) {
			return 1;
		}
	}
	return 0;
}
EXPORT_SYMBOL_GPL(omap_dm_timers_active);

static int __init omap_dm_timer_init(void)
{
	struct omap_dm_timer *timer;
	int i, map_size = SZ_8K;	/* Module 4KB + L4 4KB except on omap1 */

	if (!(cpu_is_omap16xx() || cpu_class_is_omap2()))
		return -ENODEV;

	spin_lock_init(&dm_timer_lock);

	if (cpu_class_is_omap1()) {
		dm_timers = omap1_dm_timers;
		dm_timer_count = omap1_dm_timer_count;
		map_size = SZ_2K;
	} else if (cpu_is_omap24xx()) {
		dm_timers = omap2_dm_timers;
		dm_timer_count = omap2_dm_timer_count;
		dm_source_names = omap2_dm_source_names;
		dm_source_clocks = omap2_dm_source_clocks;
	} else if (cpu_is_omap34xx()) {
		dm_timers = omap3_dm_timers;
		dm_timer_count = omap3_dm_timer_count;
		dm_source_names = omap3_dm_source_names;
		dm_source_clocks = omap3_dm_source_clocks;
	} else if (cpu_is_omap44xx()) {
		dm_timers = omap4_dm_timers;
		dm_timer_count = omap4_dm_timer_count;
		dm_source_names = omap4_dm_source_names;
		dm_source_clocks = omap4_dm_source_clocks;
	}

	if (cpu_class_is_omap2())
		for (i = 0; dm_source_names[i] != NULL; i++)
			dm_source_clocks[i] = clk_get(NULL, dm_source_names[i]);

	if (cpu_is_omap243x())
		dm_timers[0].phys_base = 0x49018000;

	for (i = 0; i < dm_timer_count; i++) {
		timer = &dm_timers[i];

		/* Static mapping, never released */
		timer->io_base = ioremap(timer->phys_base, map_size);
		BUG_ON(!timer->io_base);

#ifdef CONFIG_ARCH_OMAP2PLUS
		if (cpu_class_is_omap2()) {
			char clk_name[16];
			sprintf(clk_name, "gpt%d_ick", i + 1);
			timer->iclk = clk_get(NULL, clk_name);
			sprintf(clk_name, "gpt%d_fck", i + 1);
			timer->fclk = clk_get(NULL, clk_name);
		}

		/* One or two timers may be set up early for sys_timer */
		if (sys_timer_reserved & (1  << i)) {
			timer->reserved = 1;
			timer->posted = 1;
		}
#endif
	}

	return 0;
}

arch_initcall(omap_dm_timer_init);
