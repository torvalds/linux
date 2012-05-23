/*
 * arch/arm/plat-omap/include/plat/dmtimer.h
 *
 * OMAP Dual-Mode Timers
 *
 * Copyright (C) 2010 Texas Instruments Incorporated - http://www.ti.com/
 * Tarun Kanti DebBarma <tarun.kanti@ti.com>
 * Thara Gopinath <thara@ti.com>
 *
 * Platform device conversion and hwmod support.
 *
 * Copyright (C) 2005 Nokia Corporation
 * Author: Lauri Leukkunen <lauri.leukkunen@nokia.com>
 * PWM and clock framwork support by Timo Teras.
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
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/platform_device.h>

#ifndef __ASM_ARCH_DMTIMER_H
#define __ASM_ARCH_DMTIMER_H

/* clock sources */
#define OMAP_TIMER_SRC_SYS_CLK			0x00
#define OMAP_TIMER_SRC_32_KHZ			0x01
#define OMAP_TIMER_SRC_EXT_CLK			0x02

/* timer interrupt enable bits */
#define OMAP_TIMER_INT_CAPTURE			(1 << 2)
#define OMAP_TIMER_INT_OVERFLOW			(1 << 1)
#define OMAP_TIMER_INT_MATCH			(1 << 0)

/* trigger types */
#define OMAP_TIMER_TRIGGER_NONE			0x00
#define OMAP_TIMER_TRIGGER_OVERFLOW		0x01
#define OMAP_TIMER_TRIGGER_OVERFLOW_AND_COMPARE	0x02

/*
 * IP revision identifier so that Highlander IP
 * in OMAP4 can be distinguished.
 */
#define OMAP_TIMER_IP_VERSION_1                        0x1

/* timer capabilities used in hwmod database */
#define OMAP_TIMER_SECURE				0x80000000
#define OMAP_TIMER_ALWON				0x40000000
#define OMAP_TIMER_HAS_PWM				0x20000000

struct omap_timer_capability_dev_attr {
	u32 timer_capability;
};

struct omap_dm_timer;
struct clk;

struct timer_regs {
	u32 tidr;
	u32 tiocp_cfg;
	u32 tistat;
	u32 tisr;
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

struct dmtimer_platform_data {
	int (*set_timer_src)(struct platform_device *pdev, int source);
	int timer_ip_version;
	u32 needs_manual_reset:1;
	bool reserved;

	bool loses_context;

	int (*get_context_loss_count)(struct device *dev);
};

struct omap_dm_timer *omap_dm_timer_request(void);
struct omap_dm_timer *omap_dm_timer_request_specific(int timer_id);
int omap_dm_timer_free(struct omap_dm_timer *timer);
void omap_dm_timer_enable(struct omap_dm_timer *timer);
void omap_dm_timer_disable(struct omap_dm_timer *timer);

int omap_dm_timer_get_irq(struct omap_dm_timer *timer);

u32 omap_dm_timer_modify_idlect_mask(u32 inputmask);
struct clk *omap_dm_timer_get_fclk(struct omap_dm_timer *timer);

int omap_dm_timer_trigger(struct omap_dm_timer *timer);
int omap_dm_timer_start(struct omap_dm_timer *timer);
int omap_dm_timer_stop(struct omap_dm_timer *timer);

int omap_dm_timer_set_source(struct omap_dm_timer *timer, int source);
int omap_dm_timer_set_load(struct omap_dm_timer *timer, int autoreload, unsigned int value);
int omap_dm_timer_set_load_start(struct omap_dm_timer *timer, int autoreload, unsigned int value);
int omap_dm_timer_set_match(struct omap_dm_timer *timer, int enable, unsigned int match);
int omap_dm_timer_set_pwm(struct omap_dm_timer *timer, int def_on, int toggle, int trigger);
int omap_dm_timer_set_prescaler(struct omap_dm_timer *timer, int prescaler);

int omap_dm_timer_set_int_enable(struct omap_dm_timer *timer, unsigned int value);

unsigned int omap_dm_timer_read_status(struct omap_dm_timer *timer);
int omap_dm_timer_write_status(struct omap_dm_timer *timer, unsigned int value);
unsigned int omap_dm_timer_read_counter(struct omap_dm_timer *timer);
int omap_dm_timer_write_counter(struct omap_dm_timer *timer, unsigned int value);

int omap_dm_timers_active(void);

/*
 * Do not use the defines below, they are not needed. They should be only
 * used by dmtimer.c and sys_timer related code.
 */

/*
 * The interrupt registers are different between v1 and v2 ip.
 * These registers are offsets from timer->iobase.
 */
#define OMAP_TIMER_ID_OFFSET		0x00
#define OMAP_TIMER_OCP_CFG_OFFSET	0x10

#define OMAP_TIMER_V1_SYS_STAT_OFFSET	0x14
#define OMAP_TIMER_V1_STAT_OFFSET	0x18
#define OMAP_TIMER_V1_INT_EN_OFFSET	0x1c

#define OMAP_TIMER_V2_IRQSTATUS_RAW	0x24
#define OMAP_TIMER_V2_IRQSTATUS		0x28
#define OMAP_TIMER_V2_IRQENABLE_SET	0x2c
#define OMAP_TIMER_V2_IRQENABLE_CLR	0x30

/*
 * The functional registers have a different base on v1 and v2 ip.
 * These registers are offsets from timer->func_base. The func_base
 * is samae as io_base for v1 and io_base + 0x14 for v2 ip.
 *
 */
#define OMAP_TIMER_V2_FUNC_OFFSET		0x14

#define _OMAP_TIMER_WAKEUP_EN_OFFSET	0x20
#define _OMAP_TIMER_CTRL_OFFSET		0x24
#define		OMAP_TIMER_CTRL_GPOCFG		(1 << 14)
#define		OMAP_TIMER_CTRL_CAPTMODE	(1 << 13)
#define		OMAP_TIMER_CTRL_PT		(1 << 12)
#define		OMAP_TIMER_CTRL_TCM_LOWTOHIGH	(0x1 << 8)
#define		OMAP_TIMER_CTRL_TCM_HIGHTOLOW	(0x2 << 8)
#define		OMAP_TIMER_CTRL_TCM_BOTHEDGES	(0x3 << 8)
#define		OMAP_TIMER_CTRL_SCPWM		(1 << 7)
#define		OMAP_TIMER_CTRL_CE		(1 << 6) /* compare enable */
#define		OMAP_TIMER_CTRL_PRE		(1 << 5) /* prescaler enable */
#define		OMAP_TIMER_CTRL_PTV_SHIFT	2 /* prescaler value shift */
#define		OMAP_TIMER_CTRL_POSTED		(1 << 2)
#define		OMAP_TIMER_CTRL_AR		(1 << 1) /* auto-reload enable */
#define		OMAP_TIMER_CTRL_ST		(1 << 0) /* start timer */
#define _OMAP_TIMER_COUNTER_OFFSET	0x28
#define _OMAP_TIMER_LOAD_OFFSET		0x2c
#define _OMAP_TIMER_TRIGGER_OFFSET	0x30
#define _OMAP_TIMER_WRITE_PEND_OFFSET	0x34
#define		WP_NONE			0	/* no write pending bit */
#define		WP_TCLR			(1 << 0)
#define		WP_TCRR			(1 << 1)
#define		WP_TLDR			(1 << 2)
#define		WP_TTGR			(1 << 3)
#define		WP_TMAR			(1 << 4)
#define		WP_TPIR			(1 << 5)
#define		WP_TNIR			(1 << 6)
#define		WP_TCVR			(1 << 7)
#define		WP_TOCR			(1 << 8)
#define		WP_TOWR			(1 << 9)
#define _OMAP_TIMER_MATCH_OFFSET	0x38
#define _OMAP_TIMER_CAPTURE_OFFSET	0x3c
#define _OMAP_TIMER_IF_CTRL_OFFSET	0x40
#define _OMAP_TIMER_CAPTURE2_OFFSET		0x44	/* TCAR2, 34xx only */
#define _OMAP_TIMER_TICK_POS_OFFSET		0x48	/* TPIR, 34xx only */
#define _OMAP_TIMER_TICK_NEG_OFFSET		0x4c	/* TNIR, 34xx only */
#define _OMAP_TIMER_TICK_COUNT_OFFSET		0x50	/* TCVR, 34xx only */
#define _OMAP_TIMER_TICK_INT_MASK_SET_OFFSET	0x54	/* TOCR, 34xx only */
#define _OMAP_TIMER_TICK_INT_MASK_COUNT_OFFSET	0x58	/* TOWR, 34xx only */

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

struct omap_dm_timer {
	unsigned long phys_base;
	int id;
	int irq;
	struct clk *fclk;

	void __iomem	*io_base;
	void __iomem	*sys_stat;	/* TISTAT timer status */
	void __iomem	*irq_stat;	/* TISR/IRQSTATUS interrupt status */
	void __iomem	*irq_ena;	/* irq enable */
	void __iomem	*irq_dis;	/* irq disable, only on v2 ip */
	void __iomem	*pend;		/* write pending */
	void __iomem	*func_base;	/* function register base */

	unsigned long rate;
	unsigned reserved:1;
	unsigned posted:1;
	struct timer_regs context;
	bool loses_context;
	int ctx_loss_count;
	int revision;
	struct platform_device *pdev;
	struct list_head node;

	int (*get_context_loss_count)(struct device *dev);
};

int omap_dm_timer_prepare(struct omap_dm_timer *timer);

static inline u32 __omap_dm_timer_read(struct omap_dm_timer *timer, u32 reg,
						int posted)
{
	if (posted)
		while (__raw_readl(timer->pend) & (reg >> WPSHIFT))
			cpu_relax();

	return __raw_readl(timer->func_base + (reg & 0xff));
}

static inline void __omap_dm_timer_write(struct omap_dm_timer *timer,
					u32 reg, u32 val, int posted)
{
	if (posted)
		while (__raw_readl(timer->pend) & (reg >> WPSHIFT))
			cpu_relax();

	__raw_writel(val, timer->func_base + (reg & 0xff));
}

static inline void __omap_dm_timer_init_regs(struct omap_dm_timer *timer)
{
	u32 tidr;

	/* Assume v1 ip if bits [31:16] are zero */
	tidr = __raw_readl(timer->io_base);
	if (!(tidr >> 16)) {
		timer->revision = 1;
		timer->sys_stat = timer->io_base +
				OMAP_TIMER_V1_SYS_STAT_OFFSET;
		timer->irq_stat = timer->io_base + OMAP_TIMER_V1_STAT_OFFSET;
		timer->irq_ena = timer->io_base + OMAP_TIMER_V1_INT_EN_OFFSET;
		timer->irq_dis = NULL;
		timer->pend = timer->io_base + _OMAP_TIMER_WRITE_PEND_OFFSET;
		timer->func_base = timer->io_base;
	} else {
		timer->revision = 2;
		timer->sys_stat = NULL;
		timer->irq_stat = timer->io_base + OMAP_TIMER_V2_IRQSTATUS;
		timer->irq_ena = timer->io_base + OMAP_TIMER_V2_IRQENABLE_SET;
		timer->irq_dis = timer->io_base + OMAP_TIMER_V2_IRQENABLE_CLR;
		timer->pend = timer->io_base +
			_OMAP_TIMER_WRITE_PEND_OFFSET +
				OMAP_TIMER_V2_FUNC_OFFSET;
		timer->func_base = timer->io_base + OMAP_TIMER_V2_FUNC_OFFSET;
	}
}

/* Assumes the source clock has been set by caller */
static inline void __omap_dm_timer_reset(struct omap_dm_timer *timer,
					int autoidle, int wakeup)
{
	u32 l;

	l = __raw_readl(timer->io_base + OMAP_TIMER_OCP_CFG_OFFSET);
	l |= 0x02 << 3;  /* Set to smart-idle mode */
	l |= 0x2 << 8;   /* Set clock activity to perserve f-clock on idle */

	if (autoidle)
		l |= 0x1 << 0;

	if (wakeup)
		l |= 1 << 2;

	__raw_writel(l, timer->io_base + OMAP_TIMER_OCP_CFG_OFFSET);

	/* Match hardware reset default of posted mode */
	__omap_dm_timer_write(timer, OMAP_TIMER_IF_CTRL_REG,
					OMAP_TIMER_CTRL_POSTED, 0);
}

static inline int __omap_dm_timer_set_source(struct clk *timer_fck,
						struct clk *parent)
{
	int ret;

	clk_disable(timer_fck);
	ret = clk_set_parent(timer_fck, parent);
	clk_enable(timer_fck);

	/*
	 * When the functional clock disappears, too quick writes seem
	 * to cause an abort. XXX Is this still necessary?
	 */
	__delay(300000);

	return ret;
}

static inline void __omap_dm_timer_stop(struct omap_dm_timer *timer,
					int posted, unsigned long rate)
{
	u32 l;

	l = __omap_dm_timer_read(timer, OMAP_TIMER_CTRL_REG, posted);
	if (l & OMAP_TIMER_CTRL_ST) {
		l &= ~0x1;
		__omap_dm_timer_write(timer, OMAP_TIMER_CTRL_REG, l, posted);
#ifdef CONFIG_ARCH_OMAP2PLUS
		/* Readback to make sure write has completed */
		__omap_dm_timer_read(timer, OMAP_TIMER_CTRL_REG, posted);
		/*
		 * Wait for functional clock period x 3.5 to make sure that
		 * timer is stopped
		 */
		udelay(3500000 / rate + 1);
#endif
	}

	/* Ack possibly pending interrupt */
	__raw_writel(OMAP_TIMER_INT_OVERFLOW, timer->irq_stat);
}

static inline void __omap_dm_timer_load_start(struct omap_dm_timer *timer,
						u32 ctrl, unsigned int load,
						int posted)
{
	__omap_dm_timer_write(timer, OMAP_TIMER_COUNTER_REG, load, posted);
	__omap_dm_timer_write(timer, OMAP_TIMER_CTRL_REG, ctrl, posted);
}

static inline void __omap_dm_timer_int_enable(struct omap_dm_timer *timer,
						unsigned int value)
{
	__raw_writel(value, timer->irq_ena);
	__omap_dm_timer_write(timer, OMAP_TIMER_WAKEUP_EN_REG, value, 0);
}

static inline unsigned int
__omap_dm_timer_read_counter(struct omap_dm_timer *timer, int posted)
{
	return __omap_dm_timer_read(timer, OMAP_TIMER_COUNTER_REG, posted);
}

static inline void __omap_dm_timer_write_status(struct omap_dm_timer *timer,
						unsigned int value)
{
	__raw_writel(value, timer->irq_stat);
}

#endif /* __ASM_ARCH_DMTIMER_H */
