/*
 * arch/arm/plat-sunxi/clocksrc.h
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

#ifndef __AW_CLOCKSRC_H__
#define __AW_CLOCKSRC_H__

#define __cnt_reg(off) (*(volatile __u32 *)((SW_VA_CPUCFG_IO_BASE) + (off)))
#define __tmr_reg(off) (*(volatile __u32 *)((SW_VA_TIMERC_IO_BASE) + (off)))
#define __tmr_x_reg(x, off) __tmr_reg(0x10 + (x) * 0x10 + (off))

/* define timer io register value */
#define TMR_REG_IRQ_EN		__tmr_reg(0x00)
#define TMR_REG_IRQ_STAT	__tmr_reg(0x04)
#define TMR_REG_TMR_CTL(x)	__tmr_x_reg((x), 0x00)
#define TMR_REG_TMR_INTV(x)	__tmr_x_reg((x), 0x04)
#define TMR_REG_TMR_CUR(x)	__tmr_x_reg((x), 0x08)

#ifndef CONFIG_ARCH_SUN7I
#define TMR_REG_CNT64_CTL       __tmr_reg(0xa0)
#define TMR_REG_CNT64_LO        __tmr_reg(0xa4)
#define TMR_REG_CNT64_HI        __tmr_reg(0xa8)
#endif

/* define timer clock source */
#define TMR_CLK_SRC_32KLOSC     (0)
#define TMR_CLK_SRC_24MHOSC     (1)
#define TMR_CLK_SRC_PLL         (2)


/* config clock frequency   */
#define AW_HPET_CLK_SRC     TMR_CLK_SRC_24MHOSC
#define AW_HPET_CLK_EVT     TMR_CLK_SRC_24MHOSC


/* aw HPET clock source frequency */
#if(AW_HPET_CLK_SRC == TMR_CLK_SRC_24MHOSC)
    #define AW_HPET_CLOCK_SOURCE_HZ         (24000000)
#else
    #error "AW_HPET_CLK_SRC config is invalid!!"
#endif


/* aw HPET clock eventy frequency */
#if(AW_HPET_CLK_EVT == TMR_CLK_SRC_32KLOSC)

#define AW_HPET_CLOCK_EVENT_HZ          (32768)

#ifdef CONFIG_ARCH_SUN7I
/* Make the TMR_REG_CNT64 macros point to the 32KLOSC 64bit counter */
#define TMR_REG_CNT64_CTL       __cnt_reg(0x0290)
#define TMR_REG_CNT64_LO        __cnt_reg(0x0294)
#define TMR_REG_CNT64_HI        __cnt_reg(0x0298)
#endif

#elif(AW_HPET_CLK_EVT == TMR_CLK_SRC_24MHOSC)

#define AW_HPET_CLOCK_EVENT_HZ          (24000000)

#ifdef CONFIG_ARCH_SUN7I
/* Make the TMR_REG_CNT64 macros point to 24MHOSC 64bit counter */
#define TMR_REG_CNT64_CTL       __cnt_reg(0x0280)
#define TMR_REG_CNT64_LO        __cnt_reg(0x0284)
#define TMR_REG_CNT64_HI        __cnt_reg(0x0288)
#endif

#else
#error "AW_HPET_CLK_EVT config is invalid!!"
#endif

u32 aw_sched_clock_read(void);
int aw_clksrc_init(void);
int aw_clkevt_init(void);

#endif  /* #ifndef __AW_CLOCKSRC_H__ */
