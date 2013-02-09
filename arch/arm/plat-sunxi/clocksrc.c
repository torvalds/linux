/*
 * arch/arm/plat-sunxi/clocksrc.c
 *
 * (C) Copyright 2007-2012
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 * Kevin Zhang <kevin@allwinnertech.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#include <mach/hardware.h>
#include <mach/platform.h>
#include <linux/init.h>
#include <linux/clocksource.h>
#include <linux/clockchips.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/delay.h>
#include "clocksrc.h"

#undef CLKSRC_DBG
#undef CLKSRC_ERR
#if (0)
    #define CLKSRC_DBG(format,args...)  printk("[CLKSRC] "format,##args)
    #define CLKSRC_ERR(format,args...)  printk("[CLKSRC] "format,##args)
#else
    #define CLKSRC_DBG(...)
    #define CLKSRC_ERR(...)
#endif

static cycle_t aw_clksrc_read(struct clocksource *cs);
#ifdef CONFIG_HIGH_RES_TIMERS
static irqreturn_t aw_clkevt_irq(int irq, void *handle);
static spinlock_t timer1_spin_lock;
static void aw_set_clkevt_mode(enum clock_event_mode mode, struct clock_event_device *dev);
static int aw_set_next_clkevt(unsigned long delta, struct clock_event_device *dev);
#endif

static struct clocksource aw_clocksrc =
{
    .name = "aw 64bits couter",
    .list = {NULL, NULL},
    .rating = 300,                  /* perfect clock source             */
    .read = aw_clksrc_read,         /* read clock counter               */
    .enable = 0,                    /* not define                       */
    .disable = 0,                   /* not define                       */
    .mask = CLOCKSOURCE_MASK(64),   /* 64bits mask                      */
    .mult = 0,                      /* it will be calculated by shift   */
    .shift = 10,                    /* 32bit shift for                  */
    .max_idle_ns = 1000000000000ULL,
    .flags = CLOCK_SOURCE_IS_CONTINUOUS,
};

#ifdef CONFIG_HIGH_RES_TIMERS
static struct clock_event_device aw_clock_event =
{
    .name = "aw clock event device",
    .features = CLOCK_EVT_FEAT_PERIODIC | CLOCK_EVT_FEAT_ONESHOT,
    .max_delta_ns = 100000000000ULL,
    .min_delta_ns = (1000000000 + AW_HPET_CLOCK_EVENT_HZ - 1) / AW_HPET_CLOCK_EVENT_HZ,
    .mult = 100,                    /* will be calculate when init      */
    .shift = 32,
    .rating = 300,                  /* clock event is perfect           */
    .irq = SW_INT_IRQNO_TIMER1,
    .cpumask = 0,                   /* will be set when init            */
    .set_next_event = aw_set_next_clkevt,
    .set_mode = aw_set_clkevt_mode, /* set clock event mode             */
    .event_handler = 0,             /* be alloced by system framework   */
};

static struct irqaction aw_clkevt_irqact =
{
    .handler = aw_clkevt_irq,
    .flags = IRQF_TIMER | IRQF_DISABLED,
    .name = "aw clock event irq",
    .dev_id = &aw_clock_event,
    .irq = SW_INT_IRQNO_TIMER1,
};
#endif


/*
*********************************************************************************************************
*                           aw_clksrc_read
*
*Description: read cycle count of the clock source;
*
*Arguments  : cs    clock source handle.
*
*Return     : cycle count;
*
*Notes      :
*
*********************************************************************************************************
*/
static cycle_t aw_clksrc_read(struct clocksource *cs)
{
    unsigned long   flags;
    __u32           lower, upper;

    /* disable interrupt response */
    raw_local_irq_save(flags);

    /* latch 64bit counter and wait ready for read */
    TMR_REG_CNT64_CTL |= (1<<1);
    while(TMR_REG_CNT64_CTL & (1<<1));

    /* read the 64bits counter */
    lower = TMR_REG_CNT64_LO;
    upper = TMR_REG_CNT64_HI;

    /* restore interrupt response */
    raw_local_irq_restore(flags);

    return (((__u64)upper)<<32) | lower;
}


/*
*********************************************************************************************************
*                           aw_set_clkevt_mode
*
*Description: set clock event work mode.
*
*Arguments  : mode  mode for clock event work;
*             dev   clock event device;
*
*Return     : none
*
*Notes      :
*
*********************************************************************************************************
*/
#ifdef CONFIG_HIGH_RES_TIMERS
static void aw_set_clkevt_mode(enum clock_event_mode mode, struct clock_event_device *dev)
{
    CLKSRC_DBG("aw_set_clkevt_mode:%u\n", mode);
    switch (mode)
    {
        case CLOCK_EVT_MODE_PERIODIC:
        {
            /* set timer work with continueous mode */
            TMR_REG_TMR1_CTL &= ~(1<<0);
            /* wait hardware synchronization, 2 cycles of the hardware work clock at least  */
            __delay(50);
            TMR_REG_TMR1_CTL &= ~(1<<7);
            TMR_REG_TMR1_CTL |= (1<<0);
            break;
        }

        case CLOCK_EVT_MODE_ONESHOT:
        {
            /* set timer work with onshot mode */
            TMR_REG_TMR1_CTL &= ~(1<<0);
            /* wait hardware synchronization, 2 cycles of the hardware work clock at least  */
            __delay(50);
            TMR_REG_TMR1_CTL |= (1<<7);
            TMR_REG_TMR1_CTL |= (1<<0);
            break;
        }

        default:
        {
            /* disable clock event device */
            TMR_REG_TMR1_CTL &= ~(1<<0);
            /* wait hardware synchronization, 2 cycles of the hardware work clock at least  */
            __delay(50);
            break;
        }
    }
}
#endif


/*
*********************************************************************************************************
*                           aw_set_next_clkevt
*
*Description: set next clock event.
*
*Arguments  : delta     cycle count for next clock event.
*             dev       clock event device.
*
*Return     : result,
*               0,  set next event successed;
*              !0,  set next event failed;
*
*Notes      :
*
*********************************************************************************************************
*/
#ifdef CONFIG_HIGH_RES_TIMERS
static int aw_set_next_clkevt(unsigned long delta, struct clock_event_device *dev)
{
	unsigned long flags;
    CLKSRC_DBG("aw_set_next_clkevt: %u\n", (unsigned int)delta);

	spin_lock_irqsave(&timer1_spin_lock, flags);
    /* disable timer and clear pending first    */
    TMR_REG_TMR1_CTL &= ~(1<<0);
    /* wait hardware synchronization, 2 cycles of the hardware work clock at least  */
    udelay(1);

    /* set timer intervalue         */
    TMR_REG_TMR1_INTV = delta;
    /* reload the timer intervalue  */
    TMR_REG_TMR1_CTL |= (1<<1);

    /* enable timer */
    TMR_REG_TMR1_CTL |= (1<<0);
    spin_unlock_irqrestore(&timer1_spin_lock, flags);
    return 0;
}
#endif

/*
*********************************************************************************************************
*                                   aw_clkevt_irq
*
*Description: clock event interrupt handler.
*
*Arguments  : irq       interrupt number of current processed;
*             handle    device handle registered when setup irq;
*
*Return     : result,
*               IRQ_HANDLED,    irq is processed successed;
*               IRQ_NONE,       irq is not setup by us;
*
*Notes      :
*
*********************************************************************************************************
*/
#ifdef CONFIG_HIGH_RES_TIMERS
static irqreturn_t aw_clkevt_irq(int irq, void *handle)
{
    if(TMR_REG_IRQ_STAT & (1<<1))
    {
        CLKSRC_DBG("aw_clkevt_irq!\n");
        /* clear pending */
        TMR_REG_IRQ_STAT = (1<<1);

        /* clock event interrupt handled */
	if(likely(aw_clock_event.event_handler != NULL))
		aw_clock_event.event_handler(&aw_clock_event);

        return IRQ_HANDLED;
    }

    return IRQ_NONE;
}
#endif


/*
*********************************************************************************************************
*                           aw_clksrc_init
*
*Description: clock source initialise.
*
*Arguments  : none
*
*Return     : result,
*               0,  initiate successed;
*              !0,  initiate failed;
*
*Notes      :
*
*********************************************************************************************************
*/
static int __init aw_clksrc_init(void)
{
    CLKSRC_DBG("all-winners clock source init!\n");
    /* we use 64bits counter as HPET(High Precision Event Timer) */
    TMR_REG_CNT64_CTL  = 0;
    __delay(50);
    /* config clock source for 64bits counter */
    #if(AW_HPET_CLK_SRC == TMR_CLK_SRC_24MHOSC)
        TMR_REG_CNT64_CTL |= (0<<2);
    #else
        TMR_REG_CNT64_CTL |= (1<<2);
    #endif
    __delay(50);
    /* clear 64bits counter */
    TMR_REG_CNT64_CTL |= (1<<0);
    __delay(50);
    CLKSRC_DBG("register all-winners clock source!\n");
    /* calculate the mult by shift  */
    aw_clocksrc.mult = clocksource_hz2mult(AW_HPET_CLOCK_SOURCE_HZ, aw_clocksrc.shift);
    /* register clock source */
    clocksource_register(&aw_clocksrc);

    return 0;
}

/*
*********************************************************************************************************
*                           aw_clkevt_init
*
*Description: clock event initialise.
*
*Arguments  : none
*
*Return     : result,
*               0,  initiate successed;
*              !0,  initiate failed;
*
*Notes      :
*
*********************************************************************************************************
*/
#ifdef CONFIG_HIGH_RES_TIMERS
static int __init aw_clkevt_init(void)
{
    /* register clock event irq     */
    CLKSRC_DBG("set up all-winners clock event irq!\n");
    /* clear timer1 setting */
    TMR_REG_TMR1_CTL = 0;
    /* initialise timer inter value to 1 tick */
    TMR_REG_TMR1_INTV = AW_HPET_CLOCK_EVENT_HZ/HZ;

    /* config clock source for timer1 */
    #if(AW_HPET_CLK_EVT == TMR_CLK_SRC_24MHOSC)
        TMR_REG_TMR1_CTL |= (1<<2);
    #else
        TMR_REG_TMR1_CTL |= (0<<2);
    #endif
    /* reload inter value */
    TMR_REG_TMR1_CTL |= (1<<1);
    /* install timer irq */
    setup_irq(SW_INT_IRQNO_TIMER1, &aw_clkevt_irqact);
    /* enable timer1 irq            */
    TMR_REG_IRQ_EN |= (1<<1);

    /* register clock event device  */
    CLKSRC_DBG("register all-winners clock event device!\n");
	aw_clock_event.mult = div_sc(AW_HPET_CLOCK_EVENT_HZ, NSEC_PER_SEC, aw_clock_event.shift);
	aw_clock_event.max_delta_ns = clockevent_delta2ns((0x80000000), &aw_clock_event);
	/* time value timer must larger than 50 cycles at least, suggested by david 2011-5-25 11:41 */
	aw_clock_event.min_delta_ns = clockevent_delta2ns(1, &aw_clock_event) + 100000;
	aw_clock_event.cpumask = cpumask_of(0);
    clockevents_register_device(&aw_clock_event);

    return 0;
}
#endif

arch_initcall(aw_clksrc_init);
#ifdef CONFIG_HIGH_RES_TIMERS
arch_initcall(aw_clkevt_init);
#endif
