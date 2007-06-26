/*
 * linux/arch/arm/plat-omap/dmtimer.c
 *
 * OMAP Dual-Mode Timers
 *
 * Copyright (C) 2005 Nokia Corporation
 * OMAP2 support by Juha Yrjola
 * API improvements and OMAP2 clock framework support by Timo Teras
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
#include <asm/hardware.h>
#include <asm/arch/dmtimer.h>
#include <asm/io.h>
#include <asm/arch/irqs.h>

/* register offsets */
#define OMAP_TIMER_ID_REG		0x00
#define OMAP_TIMER_OCP_CFG_REG		0x10
#define OMAP_TIMER_SYS_STAT_REG		0x14
#define OMAP_TIMER_STAT_REG		0x18
#define OMAP_TIMER_INT_EN_REG		0x1c
#define OMAP_TIMER_WAKEUP_EN_REG	0x20
#define OMAP_TIMER_CTRL_REG		0x24
#define OMAP_TIMER_COUNTER_REG		0x28
#define OMAP_TIMER_LOAD_REG		0x2c
#define OMAP_TIMER_TRIGGER_REG		0x30
#define OMAP_TIMER_WRITE_PEND_REG	0x34
#define OMAP_TIMER_MATCH_REG		0x38
#define OMAP_TIMER_CAPTURE_REG		0x3c
#define OMAP_TIMER_IF_CTRL_REG		0x40

/* timer control reg bits */
#define OMAP_TIMER_CTRL_GPOCFG		(1 << 14)
#define OMAP_TIMER_CTRL_CAPTMODE	(1 << 13)
#define OMAP_TIMER_CTRL_PT		(1 << 12)
#define OMAP_TIMER_CTRL_TCM_LOWTOHIGH	(0x1 << 8)
#define OMAP_TIMER_CTRL_TCM_HIGHTOLOW	(0x2 << 8)
#define OMAP_TIMER_CTRL_TCM_BOTHEDGES	(0x3 << 8)
#define OMAP_TIMER_CTRL_SCPWM		(1 << 7)
#define OMAP_TIMER_CTRL_CE		(1 << 6)	/* compare enable */
#define OMAP_TIMER_CTRL_PRE		(1 << 5)	/* prescaler enable */
#define OMAP_TIMER_CTRL_PTV_SHIFT	2		/* how much to shift the prescaler value */
#define OMAP_TIMER_CTRL_AR		(1 << 1)	/* auto-reload enable */
#define OMAP_TIMER_CTRL_ST		(1 << 0)	/* start timer */

struct omap_dm_timer {
	unsigned long phys_base;
	int irq;
#if defined(CONFIG_ARCH_OMAP2) || defined(CONFIG_ARCH_OMAP3)
	struct clk *iclk, *fclk;
#endif
	void __iomem *io_base;
	unsigned reserved:1;
	unsigned enabled:1;
};

#ifdef CONFIG_ARCH_OMAP1

#define omap_dm_clk_enable(x)
#define omap_dm_clk_disable(x)
#define omap2_dm_timers			NULL
#define omap2_dm_source_names		NULL
#define omap2_dm_source_clocks		NULL
#define omap3_dm_timers			NULL
#define omap3_dm_source_names		NULL
#define omap3_dm_source_clocks		NULL

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

static const int dm_timer_count = ARRAY_SIZE(omap1_dm_timers);

#elif defined(CONFIG_ARCH_OMAP2)

#define omap_dm_clk_enable(x)		clk_enable(x)
#define omap_dm_clk_disable(x)		clk_disable(x)
#define omap1_dm_timers			NULL
#define omap3_dm_timers			NULL
#define omap3_dm_source_names		NULL
#define omap3_dm_source_clocks		NULL

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

static struct clk **omap2_dm_source_clocks[3];
static const int dm_timer_count = ARRAY_SIZE(omap2_dm_timers);

#elif defined(CONFIG_ARCH_OMAP3)

#define omap_dm_clk_enable(x)		clk_enable(x)
#define omap_dm_clk_disable(x)		clk_disable(x)
#define omap1_dm_timers			NULL
#define omap2_dm_timers			NULL
#define omap2_dm_source_names		NULL
#define omap2_dm_source_clocks		NULL

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
	{ .phys_base = 0x48304000, .irq = INT_24XX_GPTIMER12 },
};

static const char *omap3_dm_source_names[] __initdata = {
	"sys_ck",
	"omap_32k_fck",
	NULL
};

static struct clk **omap3_dm_source_clocks[2];
static const int dm_timer_count = ARRAY_SIZE(omap3_dm_timers);

#else

#error OMAP architecture not supported!

#endif

static struct omap_dm_timer *dm_timers;
static char **dm_source_names;
static struct clk **dm_source_clocks;

static spinlock_t dm_timer_lock;

static inline u32 omap_dm_timer_read_reg(struct omap_dm_timer *timer, int reg)
{
	return readl(timer->io_base + reg);
}

static void omap_dm_timer_write_reg(struct omap_dm_timer *timer, int reg, u32 value)
{
	writel(value, timer->io_base + reg);
	while (omap_dm_timer_read_reg(timer, OMAP_TIMER_WRITE_PEND_REG))
		;
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
	u32 l;

	if (!cpu_class_is_omap2() || timer != &dm_timers[0]) {
		omap_dm_timer_write_reg(timer, OMAP_TIMER_IF_CTRL_REG, 0x06);
		omap_dm_timer_wait_for_reset(timer);
	}
	omap_dm_timer_set_source(timer, OMAP_TIMER_SRC_32_KHZ);

	/* Set to smart-idle mode */
	l = omap_dm_timer_read_reg(timer, OMAP_TIMER_OCP_CFG_REG);
	l |= 0x02 << 3;

	if (cpu_class_is_omap2() && timer == &dm_timers[0]) {
		/* Enable wake-up only for GPT1 on OMAP2 CPUs*/
		l |= 1 << 2;
		/* Non-posted mode */
		omap_dm_timer_write_reg(timer, OMAP_TIMER_IF_CTRL_REG, 0);
	}
	omap_dm_timer_write_reg(timer, OMAP_TIMER_OCP_CFG_REG, l);
}

static void omap_dm_timer_prepare(struct omap_dm_timer *timer)
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

struct omap_dm_timer *omap_dm_timer_request_specific(int id)
{
	struct omap_dm_timer *timer;
	unsigned long flags;

	spin_lock_irqsave(&dm_timer_lock, flags);
	if (id <= 0 || id > dm_timer_count || dm_timers[id-1].reserved) {
		spin_unlock_irqrestore(&dm_timer_lock, flags);
		printk("BUG: warning at %s:%d/%s(): unable to get timer %d\n",
		       __FILE__, __LINE__, __FUNCTION__, id);
		dump_stack();
		return NULL;
	}

	timer = &dm_timers[id-1];
	timer->reserved = 1;
	spin_unlock_irqrestore(&dm_timer_lock, flags);

	omap_dm_timer_prepare(timer);

	return timer;
}

void omap_dm_timer_free(struct omap_dm_timer *timer)
{
	omap_dm_timer_enable(timer);
	omap_dm_timer_reset(timer);
	omap_dm_timer_disable(timer);

	WARN_ON(!timer->reserved);
	timer->reserved = 0;
}

void omap_dm_timer_enable(struct omap_dm_timer *timer)
{
	if (timer->enabled)
		return;

	omap_dm_clk_enable(timer->fclk);
	omap_dm_clk_enable(timer->iclk);

	timer->enabled = 1;
}

void omap_dm_timer_disable(struct omap_dm_timer *timer)
{
	if (!timer->enabled)
		return;

	omap_dm_clk_disable(timer->iclk);
	omap_dm_clk_disable(timer->fclk);

	timer->enabled = 0;
}

int omap_dm_timer_get_irq(struct omap_dm_timer *timer)
{
	return timer->irq;
}

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

#elif defined(CONFIG_ARCH_OMAP2) || defined (CONFIG_ARCH_OMAP3)

struct clk *omap_dm_timer_get_fclk(struct omap_dm_timer *timer)
{
	return timer->fclk;
}

__u32 omap_dm_timer_modify_idlect_mask(__u32 inputmask)
{
	BUG();

	return 0;
}

#endif

void omap_dm_timer_trigger(struct omap_dm_timer *timer)
{
	omap_dm_timer_write_reg(timer, OMAP_TIMER_TRIGGER_REG, 0);
}

void omap_dm_timer_start(struct omap_dm_timer *timer)
{
	u32 l;

	l = omap_dm_timer_read_reg(timer, OMAP_TIMER_CTRL_REG);
	if (!(l & OMAP_TIMER_CTRL_ST)) {
		l |= OMAP_TIMER_CTRL_ST;
		omap_dm_timer_write_reg(timer, OMAP_TIMER_CTRL_REG, l);
	}
}

void omap_dm_timer_stop(struct omap_dm_timer *timer)
{
	u32 l;

	l = omap_dm_timer_read_reg(timer, OMAP_TIMER_CTRL_REG);
	if (l & OMAP_TIMER_CTRL_ST) {
		l &= ~0x1;
		omap_dm_timer_write_reg(timer, OMAP_TIMER_CTRL_REG, l);
	}
}

#ifdef CONFIG_ARCH_OMAP1

void omap_dm_timer_set_source(struct omap_dm_timer *timer, int source)
{
	int n = (timer - dm_timers) << 1;
	u32 l;

	l = omap_readl(MOD_CONF_CTRL_1) & ~(0x03 << n);
	l |= source << n;
	omap_writel(l, MOD_CONF_CTRL_1);
}

#else

void omap_dm_timer_set_source(struct omap_dm_timer *timer, int source)
{
	if (source < 0 || source >= 3)
		return;

	clk_disable(timer->fclk);
	clk_set_parent(timer->fclk, dm_source_clocks[source]);
	clk_enable(timer->fclk);

	/* When the functional clock disappears, too quick writes seem to
	 * cause an abort. */
	__delay(150000);
}

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

void omap_dm_timer_set_int_enable(struct omap_dm_timer *timer,
				  unsigned int value)
{
	omap_dm_timer_write_reg(timer, OMAP_TIMER_INT_EN_REG, value);
	omap_dm_timer_write_reg(timer, OMAP_TIMER_WAKEUP_EN_REG, value);
}

unsigned int omap_dm_timer_read_status(struct omap_dm_timer *timer)
{
	unsigned int l;

	l = omap_dm_timer_read_reg(timer, OMAP_TIMER_STAT_REG);

	return l;
}

void omap_dm_timer_write_status(struct omap_dm_timer *timer, unsigned int value)
{
	omap_dm_timer_write_reg(timer, OMAP_TIMER_STAT_REG, value);
}

unsigned int omap_dm_timer_read_counter(struct omap_dm_timer *timer)
{
	unsigned int l;

	l = omap_dm_timer_read_reg(timer, OMAP_TIMER_COUNTER_REG);

	return l;
}

void omap_dm_timer_write_counter(struct omap_dm_timer *timer, unsigned int value)
{
	omap_dm_timer_write_reg(timer, OMAP_TIMER_COUNTER_REG, value);
}

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

int __init omap_dm_timer_init(void)
{
	struct omap_dm_timer *timer;
	int i;

	if (!(cpu_is_omap16xx() || cpu_class_is_omap2()))
		return -ENODEV;

	spin_lock_init(&dm_timer_lock);

	if (cpu_class_is_omap1())
		dm_timers = omap1_dm_timers;
	else if (cpu_is_omap24xx()) {
		dm_timers = omap2_dm_timers;
		dm_source_names = (char **)omap2_dm_source_names;
		dm_source_clocks = (struct clk **)omap2_dm_source_clocks;
	} else if (cpu_is_omap34xx()) {
		dm_timers = omap3_dm_timers;
		dm_source_names = (char **)omap3_dm_source_names;
		dm_source_clocks = (struct clk **)omap3_dm_source_clocks;
	}

	if (cpu_class_is_omap2())
		for (i = 0; dm_source_names[i] != NULL; i++)
			dm_source_clocks[i] = clk_get(NULL, dm_source_names[i]);

	if (cpu_is_omap243x())
		dm_timers[0].phys_base = 0x49018000;

	for (i = 0; i < dm_timer_count; i++) {
		timer = &dm_timers[i];
		timer->io_base = (void __iomem *)io_p2v(timer->phys_base);
#if defined(CONFIG_ARCH_OMAP2) || defined(CONFIG_ARCH_OMAP3)
		if (cpu_class_is_omap2()) {
			char clk_name[16];
			sprintf(clk_name, "gpt%d_ick", i + 1);
			timer->iclk = clk_get(NULL, clk_name);
			sprintf(clk_name, "gpt%d_fck", i + 1);
			timer->fclk = clk_get(NULL, clk_name);
		}
#endif
	}

	return 0;
}
