/*
 * arch\arm\mach-aw163x\timer.c
 * (C) Copyright 2010-2016
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 * chenpailin <huangxin@allwinnertech.com>
 *
 * some simple description for this code
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 */

#ifndef __AW_TIMER_H__
#define __AW_TIMER_H__

#include "platform.h"

/* timer reg offset */
#define TMR_IRQ_EN_REG_OFF               0x0000
#define TMR_IRQ_STA_REG_OFF              0x0004

#define TMR0_CTRL_REG_OFF                0x0010
#define TMR0_INTV_VALUE_REG_OFF          0x0014
#define TMR0_CUR_VALUE_REG_OFF           0x0018
#define TMR1_CTRL_REG_OFF                0x0020
#define TMR1_INTV_VALUE_REG_OFF          0x0024
#define TMR1_CUR_VALUE_REG_OFF           0x0028
#define TMR2_CTRL_REG_OFF                0x0030
#define TMR2_INTV_VALUE_REG_OFF          0x0034
#define TMR2_CUR_VALUE_REG_OFF           0x0038
#define TMR3_CTRL_REG_OFF                0x0040
#define TMR3_INTV_VALUE_REG_OFF          0x0044
#define TMR4_CTRL_REG_OFF                0x0050
#define TMR4_INTV_VALUE_REG_OFF          0x0054
#define TMR4_CUR_VALUE_REG_OFF           0x0058
#define TMR5_CTRL_REG_OFF                0x0060
#define TMR5_INTV_VALUE_REG_OFF          0x0064
#define TMR5_CUR_VALUE_REG_OFF           0x0068

#define AVS_CNT_CTRL_REG_OFF             0x0080
#define AVS_CNT0_REG_OFF                 0x0084
#define AVS_CNT1_REG_OFF                 0x0088
#define AVS_CNT_DIV_REG_OFF              0x008c
#define WDOG_CTRL_REG_OFF                0x0090
#define WDOG_MODE_REG_OFF                0x0094

#define LOSC_CTRL_REG_OFF                0x0100
#define RTC_YY_MM_DD_REG_OFF             0x0104
#define RTC_HH_MM_SS_REG_OFF             0x0108
#define DD_HH_MM_SS_REG_OFF              0x010c
#define ALARM_WK_HH_MM_SS_REG_OFF        0x0110
#define ALARM_EN_REG_OFF                 0x0114
#define ALARM_IRQ_REG_OFF                0x0118
#define ALARM_IRQ_STA_REG_OFF            0x011c

#define TMR_GP_DATA_REG_OFF(n)           (0x0120 + (n)<<2) /* n:0~15 */
#define ALARM_CONFIG_REG_OFF             0x0170

/* timer reg define */
#define TMR_IRQ_EN_REG                   (SW_VA_TIMERC_IO_BASE + TMR_IRQ_EN_REG_OFF       )
#define TMR_IRQ_STA_REG                  (SW_VA_TIMERC_IO_BASE + TMR_IRQ_STA_REG_OFF      )

#define TMR0_CTRL_REG                    (SW_VA_TIMERC_IO_BASE + TMR0_CTRL_REG_OFF        )
#define TMR0_INTV_VALUE_REG              (SW_VA_TIMERC_IO_BASE + TMR0_INTV_VALUE_REG_OFF  )
#define TMR0_CUR_VALUE_REG               (SW_VA_TIMERC_IO_BASE + TMR0_CUR_VALUE_REG_OFF   )
#define TMR1_CTRL_REG                    (SW_VA_TIMERC_IO_BASE + TMR1_CTRL_REG_OFF        )
#define TMR1_INTV_VALUE_REG              (SW_VA_TIMERC_IO_BASE + TMR1_INTV_VALUE_REG_OFF  )
#define TMR1_CUR_VALUE_REG               (SW_VA_TIMERC_IO_BASE + TMR1_CUR_VALUE_REG_OFF   )
#define TMR2_CTRL_REG                    (SW_VA_TIMERC_IO_BASE + TMR2_CTRL_REG_OFF        )
#define TMR2_INTV_VALUE_REG              (SW_VA_TIMERC_IO_BASE + TMR2_INTV_VALUE_REG_OFF  )
#define TMR2_CUR_VALUE_REG               (SW_VA_TIMERC_IO_BASE + TMR2_CUR_VALUE_REG_OFF   )
#define TMR3_CTRL_REG                    (SW_VA_TIMERC_IO_BASE + TMR3_CTRL_REG_OFF        )
#define TMR3_INTV_VALUE_REG              (SW_VA_TIMERC_IO_BASE + TMR3_INTV_VALUE_REG_OFF  )
#define TMR4_CTRL_REG                    (SW_VA_TIMERC_IO_BASE + TMR4_CTRL_REG_OFF        )
#define TMR4_INTV_VALUE_REG              (SW_VA_TIMERC_IO_BASE + TMR4_INTV_VALUE_REG_OFF  )
#define TMR4_CUR_VALUE_REG               (SW_VA_TIMERC_IO_BASE + TMR4_CUR_VALUE_REG_OFF   )
#define TMR5_CTRL_REG                    (SW_VA_TIMERC_IO_BASE + TMR5_CTRL_REG_OFF        )
#define TMR5_INTV_VALUE_REG              (SW_VA_TIMERC_IO_BASE + TMR5_INTV_VALUE_REG_OFF  )
#define TMR5_CUR_VALUE_REG               (SW_VA_TIMERC_IO_BASE + TMR5_CUR_VALUE_REG_OFF   )

#define AVS_CNT_CTRL_REG                 (SW_VA_TIMERC_IO_BASE + AVS_CNT_CTRL_REG_OFF     )
#define AVS_CNT0_REG                     (SW_VA_TIMERC_IO_BASE + AVS_CNT0_REG_OFF         )
#define AVS_CNT1_REG                     (SW_VA_TIMERC_IO_BASE + AVS_CNT1_REG_OFF         )
#define AVS_CNT_DIV_REG                  (SW_VA_TIMERC_IO_BASE + AVS_CNT_DIV_REG_OFF      )
#define WDOG_CTRL_REG                    (SW_VA_TIMERC_IO_BASE + WDOG_CTRL_REG_OFF        )
#define WDOG_MODE_REG                    (SW_VA_TIMERC_IO_BASE + WDOG_MODE_REG_OFF        )

#define LOSC_CTRL_REG                    (SW_VA_TIMERC_IO_BASE + LOSC_CTRL_REG_OFF        )
#define RTC_YY_MM_DD_REG                 (SW_VA_TIMERC_IO_BASE + RTC_YY_MM_DD_REG_OFF     )
#define RTC_HH_MM_SS_REG                 (SW_VA_TIMERC_IO_BASE + RTC_HH_MM_SS_REG_OFF     )
#define DD_HH_MM_SS_REG                  (SW_VA_TIMERC_IO_BASE + DD_HH_MM_SS_REG_OFF      )
#define ALARM_WK_HH_MM_SS_REG            (SW_VA_TIMERC_IO_BASE + ALARM_WK_HH_MM_SS_REG_OFF)
#define ALARM_EN_REG                     (SW_VA_TIMERC_IO_BASE + ALARM_EN_REG_OFF         )
#define ALARM_IRQ_REG                    (SW_VA_TIMERC_IO_BASE + ALARM_IRQ_REG_OFF        )
#define ALARM_IRQ_STA_REG                (SW_VA_TIMERC_IO_BASE + ALARM_IRQ_STA_REG_OFF    )

#define TMR_GP_DATA_REG(n)               (SW_VA_TIMERC_IO_BASE + TMR_GP_DATA_REG_OFF(n)   )
#define ALARM_CONFIG_REG                 (SW_VA_TIMERC_IO_BASE + ALARM_CONFIG_REG_OFF     )

/* 64bit timer reg off */
#define OSC24M_CNT64_CTRL_REG_OFF       0x0280
#define OSC24M_CNT64_LOW_REG_OFF        0x0284
#define OSC24M_CNT64_HIGH_REG_OFF       0x0288
#define LOSC_CNT64_CTRL_REG_OFF         0x0290
#define LOSC_CNT64_LOW_REG_OFF          0x0294
#define LOSC_CNT64_HIGH_REG_OFF         0x0298

/* 64bit timer reg */
#define OSC24M_CNT64_CTRL_REG          (SW_VA_CPUCFG_IO_BASE + OSC24M_CNT64_CTRL_REG_OFF)
#define OSC24M_CNT64_LOW_REG           (SW_VA_CPUCFG_IO_BASE + OSC24M_CNT64_LOW_REG_OFF)
#define OSC24M_CNT64_HIGH_REG          (SW_VA_CPUCFG_IO_BASE + OSC24M_CNT64_HIGH_REG_OFF)
#define LOSC_CNT64_CTRL_REG            (SW_VA_CPUCFG_IO_BASE + LOSC_CNT64_CTRL_REG_OFF)
#define LOSC_CNT64_LOW_REG             (SW_VA_CPUCFG_IO_BASE + LOSC_CNT64_LOW_REG_OFF)
#define LOSC_CNT64_HIGH_REG            (SW_VA_CPUCFG_IO_BASE + LOSC_CNT64_HIGH_REG_OFF)

/* aw HPET clock source frequency */
#define AW_HPET_CLOCK_SOURCE_HZ		(24000000)

#if AW_HPET_CLOCK_SOURCE_HZ == 24000000
#define SW_HSTMR_CTRL_REG	       OSC24M_CNT64_CTRL_REG
#define SW_HSTMR_LOW_REG	       OSC24M_CNT64_LOW_REG
#define SW_HSTMR_HIGH_REG	       OSC24M_CNT64_HIGH_REG
#elif AW_HPET_CLOCK_SOURCE_HZ == 32000
#define SW_HSTMR_CTRL_REG	       LOSC_CNT64_CTRL_REG
#define SW_HSTMR_LOW_REG	       LOSC_CNT64_LOW_REG
#define SW_HSTMR_HIGH_REG	       LOSC_CNT64_HIGH_REG
#else
#error "AW_HPET_CLOCK_SOURCE_HZ invalid! please set it!"
#endif

void __init aw_clksrc_init(void);
void aw_clkevt_init(void);

#endif  /* #ifndef __AW_CLOCKSRC_H__ */

