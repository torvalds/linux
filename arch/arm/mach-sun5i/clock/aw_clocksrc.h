/*
 * arch\arm\mach-sun5i\clock\aw_clocksrc.h
 * (C) Copyright 2010-2015
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 * kevin.z
 *
 * core header file for Lichee Linux BSP
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 */
#ifndef __AW_CLOCKSRC_H__
#define __AW_CLOCKSRC_H__

#ifndef __tmr_reg
    #define __tmr_reg(x)    (*(volatile __u32 *)(x))
#endif  /*#ifndef __tmr_reg */


/* define timer io base on aw chips */
#define AW_TMR_IO_BASE          SW_VA_TIMERC_IO_BASE
/* define timer io register address */
#define TMR_REG_o_IRQ_EN        (AW_TMR_IO_BASE + 0x0000)
#define TMR_REG_o_IRQ_STAT      (AW_TMR_IO_BASE + 0x0004)
#define TMR_REG_o_TMR1_CTL      (AW_TMR_IO_BASE + 0x0020)
#define TMR_REG_o_TMR1_INTV     (AW_TMR_IO_BASE + 0x0024)
#define TMR_REG_o_TMR1_CUR      (AW_TMR_IO_BASE + 0x0028)
#define TMR_REG_o_CNT64_CTL     (AW_TMR_IO_BASE + 0x00A0)
#define TMR_REG_o_CNT64_LO      (AW_TMR_IO_BASE + 0x00A4)
#define TMR_REG_o_CNT64_HI      (AW_TMR_IO_BASE + 0x00A8)
/* define timer io register value */
#define TMR_REG_IRQ_EN          __tmr_reg(TMR_REG_o_IRQ_EN   )
#define TMR_REG_IRQ_STAT        __tmr_reg(TMR_REG_o_IRQ_STAT )
#define TMR_REG_TMR1_CTL        __tmr_reg(TMR_REG_o_TMR1_CTL )
#define TMR_REG_TMR1_INTV       __tmr_reg(TMR_REG_o_TMR1_INTV)
#define TMR_REG_TMR1_CUR        __tmr_reg(TMR_REG_o_TMR1_CUR )
#define TMR_REG_CNT64_CTL       __tmr_reg(TMR_REG_o_CNT64_CTL)
#define TMR_REG_CNT64_LO        __tmr_reg(TMR_REG_o_CNT64_LO )
#define TMR_REG_CNT64_HI        __tmr_reg(TMR_REG_o_CNT64_HI )


/* define timer clock source */
#define TMR_CLK_SRC_32KLOSC     (0)
#define TMR_CLK_SRC_24MHOSC     (1)
#define TMR_CLK_SRC_PLL         (2)


/* config clock frequency   */
#define AW_HPET_CLK_SRC     TMR_CLK_SRC_24MHOSC
#define AW_HPET_CLK_EVT     TMR_CLK_SRC_24MHOSC


/* aw HPET clock source frequency */
#ifndef AW_HPET_CLK_SRC
    #error "AW_HPET_CLK_SRC is not define!!"
#endif
#if(AW_HPET_CLK_SRC == TMR_CLK_SRC_24MHOSC)
    #define AW_HPET_CLOCK_SOURCE_HZ         (24000000)
#else
    #error "AW_HPET_CLK_SRC config is invalid!!"
#endif


/* aw HPET clock eventy frequency */
#ifndef AW_HPET_CLK_EVT
    #error "AW_HPET_CLK_EVT is not define!!"
#endif
#if(AW_HPET_CLK_EVT == TMR_CLK_SRC_32KLOSC)
    #define AW_HPET_CLOCK_EVENT_HZ          (32768)
#elif(AW_HPET_CLK_EVT == TMR_CLK_SRC_24MHOSC)
    #define AW_HPET_CLOCK_EVENT_HZ          (24000000)
#else
    #error "AW_HPET_CLK_EVT config is invalid!!"
#endif


#endif  /* #ifndef __AW_CLOCKSRC_H__ */

