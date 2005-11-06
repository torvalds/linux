/*
 * linux/arch/arm/plat-omap/dmtimer.c
 *
 * OMAP Dual-Mode Timers
 *
 * Copyright (C) 2005 Nokia Corporation
 * Author: Lauri Leukkunen <lauri.leukkunen@nokia.com>
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
#include <asm/arch/hardware.h>
#include <asm/arch/dmtimer.h>
#include <asm/io.h>
#include <asm/arch/irqs.h>
#include <linux/spinlock.h>
#include <linux/list.h>

#define OMAP_TIMER_COUNT		8

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
#define OMAP_TIMER_WRITE_PEND_REG 	0x34
#define OMAP_TIMER_MATCH_REG		0x38
#define OMAP_TIMER_CAPTURE_REG		0x3c
#define OMAP_TIMER_IF_CTRL_REG		0x40


static struct dmtimer_info_struct {
	struct list_head	unused_timers;
	struct list_head	reserved_timers;
} dm_timer_info;

static struct omap_dm_timer dm_timers[] = {
	{ .base=0xfffb1400, .irq=INT_1610_GPTIMER1 },
	{ .base=0xfffb1c00, .irq=INT_1610_GPTIMER2 },
	{ .base=0xfffb2400, .irq=INT_1610_GPTIMER3 },
	{ .base=0xfffb2c00, .irq=INT_1610_GPTIMER4 },
	{ .base=0xfffb3400, .irq=INT_1610_GPTIMER5 },
	{ .base=0xfffb3c00, .irq=INT_1610_GPTIMER6 },
	{ .base=0xfffb4400, .irq=INT_1610_GPTIMER7 },
	{ .base=0xfffb4c00, .irq=INT_1610_GPTIMER8 },
	{ .base=0x0 },
};


static spinlock_t dm_timer_lock;


inline void omap_dm_timer_write_reg(struct omap_dm_timer *timer, int reg, u32 value)
{
	omap_writel(value, timer->base + reg);
	while (omap_dm_timer_read_reg(timer, OMAP_TIMER_WRITE_PEND_REG))
		;
}

u32 omap_dm_timer_read_reg(struct omap_dm_timer *timer, int reg)
{
	return omap_readl(timer->base + reg);
}

int omap_dm_timers_active(void)
{
	struct omap_dm_timer *timer;

	for (timer = &dm_timers[0]; timer->base; ++timer)
		if (omap_dm_timer_read_reg(timer, OMAP_TIMER_CTRL_REG) &
		    OMAP_TIMER_CTRL_ST)
			return 1;

	return 0;
}


void omap_dm_timer_set_source(struct omap_dm_timer *timer, int source)
{
	int n = (timer - dm_timers) << 1;
	u32 l;

	l = omap_readl(MOD_CONF_CTRL_1) & ~(0x03 << n);
	l |= source << n;
	omap_writel(l, MOD_CONF_CTRL_1);
}


static void omap_dm_timer_reset(struct omap_dm_timer *timer)
{
	/* Reset and set posted mode */
	omap_dm_timer_write_reg(timer, OMAP_TIMER_IF_CTRL_REG, 0x06);
	omap_dm_timer_write_reg(timer, OMAP_TIMER_OCP_CFG_REG, 0x02);

	omap_dm_timer_set_source(timer, OMAP_TIMER_SRC_ARMXOR);
}



struct omap_dm_timer * omap_dm_timer_request(void)
{
	struct omap_dm_timer *timer = NULL;
	unsigned long flags;

	spin_lock_irqsave(&dm_timer_lock, flags);
	if (!list_empty(&dm_timer_info.unused_timers)) {
		timer = (struct omap_dm_timer *)
				dm_timer_info.unused_timers.next;
		list_move_tail((struct list_head *)timer,
				&dm_timer_info.reserved_timers);
	}
	spin_unlock_irqrestore(&dm_timer_lock, flags);

	return timer;
}


void omap_dm_timer_free(struct omap_dm_timer *timer)
{
	unsigned long flags;

	omap_dm_timer_reset(timer);

	spin_lock_irqsave(&dm_timer_lock, flags);
	list_move_tail((struct list_head *)timer, &dm_timer_info.unused_timers);
	spin_unlock_irqrestore(&dm_timer_lock, flags);
}

void omap_dm_timer_set_int_enable(struct omap_dm_timer *timer,
				unsigned int value)
{
	omap_dm_timer_write_reg(timer, OMAP_TIMER_INT_EN_REG, value);
}

unsigned int omap_dm_timer_read_status(struct omap_dm_timer *timer)
{
	return omap_dm_timer_read_reg(timer, OMAP_TIMER_STAT_REG);
}

void omap_dm_timer_write_status(struct omap_dm_timer *timer, unsigned int value)
{
	omap_dm_timer_write_reg(timer, OMAP_TIMER_STAT_REG, value);
}

void omap_dm_timer_enable_autoreload(struct omap_dm_timer *timer)
{
	u32 l;
	l = omap_dm_timer_read_reg(timer, OMAP_TIMER_CTRL_REG);
	l |= OMAP_TIMER_CTRL_AR;
	omap_dm_timer_write_reg(timer, OMAP_TIMER_CTRL_REG, l);
}

void omap_dm_timer_trigger(struct omap_dm_timer *timer)
{
	omap_dm_timer_write_reg(timer, OMAP_TIMER_TRIGGER_REG, 1);
}

void omap_dm_timer_set_trigger(struct omap_dm_timer *timer, unsigned int value)
{
	u32 l;

	l = omap_dm_timer_read_reg(timer, OMAP_TIMER_CTRL_REG);
	l |= value & 0x3;
	omap_dm_timer_write_reg(timer, OMAP_TIMER_CTRL_REG, l);
}

void omap_dm_timer_start(struct omap_dm_timer *timer)
{
	u32 l;

	l = omap_dm_timer_read_reg(timer, OMAP_TIMER_CTRL_REG);
	l |= OMAP_TIMER_CTRL_ST;
	omap_dm_timer_write_reg(timer, OMAP_TIMER_CTRL_REG, l);
}

void omap_dm_timer_stop(struct omap_dm_timer *timer)
{
	u32 l;

	l = omap_dm_timer_read_reg(timer, OMAP_TIMER_CTRL_REG);
	l &= ~0x1;
	omap_dm_timer_write_reg(timer, OMAP_TIMER_CTRL_REG, l);
}

unsigned int omap_dm_timer_read_counter(struct omap_dm_timer *timer)
{
	return omap_dm_timer_read_reg(timer, OMAP_TIMER_COUNTER_REG);
}

void omap_dm_timer_reset_counter(struct omap_dm_timer *timer)
{
	omap_dm_timer_write_reg(timer, OMAP_TIMER_COUNTER_REG, 0);
}

void omap_dm_timer_set_load(struct omap_dm_timer *timer, unsigned int load)
{
	omap_dm_timer_write_reg(timer, OMAP_TIMER_LOAD_REG, load);
}

void omap_dm_timer_set_match(struct omap_dm_timer *timer, unsigned int match)
{
	omap_dm_timer_write_reg(timer, OMAP_TIMER_MATCH_REG, match);
}

void omap_dm_timer_enable_compare(struct omap_dm_timer *timer)
{
	u32 l;

	l = omap_dm_timer_read_reg(timer, OMAP_TIMER_CTRL_REG);
	l |= OMAP_TIMER_CTRL_CE;
	omap_dm_timer_write_reg(timer, OMAP_TIMER_CTRL_REG, l);
}


static inline void __dm_timer_init(void)
{
	struct omap_dm_timer *timer;

	spin_lock_init(&dm_timer_lock);
	INIT_LIST_HEAD(&dm_timer_info.unused_timers);
	INIT_LIST_HEAD(&dm_timer_info.reserved_timers);

	timer = &dm_timers[0];
	while (timer->base) {
		list_add_tail((struct list_head *)timer, &dm_timer_info.unused_timers);
		omap_dm_timer_reset(timer);
		timer++;
	}
}

static int __init omap_dm_timer_init(void)
{
	if (cpu_is_omap16xx())
		__dm_timer_init();
	return 0;
}

arch_initcall(omap_dm_timer_init);
