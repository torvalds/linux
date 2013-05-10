/*
 * drivers/input/touchscreen/zt8031.h
 *
 * (C) Copyright 2007-2012
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
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

#ifndef __LINUX_ZT_TS_H__
#define __LINUX_ZT_TS_H__

#include <plat/sys_config.h>
#include <mach/irqs.h>
#include <linux/i2c.h>
#include <linux/i2c/tsc2007.h>

// gpio base address
#define PIO_BASE_ADDRESS             (0x01c20800)
#define PIO_RANGE_SIZE               (0x400)

#define IRQ_EINT21                   (21) 
#define IRQ_EINT29                   (29) 
#define IRQ_EINT25                   (25)

#define TS_POLL_DELAY			    10/* ms delay between samples */
#define TS_POLL_PERIOD			    10 /* ms delay between samples */

#define GPIO_ENABLE
#define SYSCONFIG_GPIO_ENABLE

#define PIO_INT_STAT_OFFSET          (0x214)
#define PIO_INT_CTRL_OFFSET          (0x210)
#define PIO_INT_CFG2_OFFSET          (0x208)
#define PIO_INT_CFG3_OFFSET          (0x20c)
#define PIO_PN_DAT_OFFSET(n)         ((n)*0x24 + 0x10) 
//#define PIOI_DATA                    (0x130)
#define PIOH_DATA                    (0x10c)
#define PIOI_CFG3_OFFSET             (0x12c)

#define PRESS_DOWN                   1
#define FREE_UP                      0

#define TS_RESET_LOW_PERIOD       (1)
#define TS_INITIAL_HIGH_PERIOD   (100)
#define TS_WAKEUP_LOW_PERIOD  (10)
#define TS_WAKEUP_HIGH_PERIOD (10)
#define IRQ_NO                           (IRQ_EINT25)

struct aw_platform_ops{
    int         irq;
	bool        pendown;
	int	        (*get_pendown_state)(void);
	void        (*clear_penirq)(void);
	int         (*set_irq_mode)(void);
	int         (*set_gpio_mode)(void);
	int         (*judge_int_occur)(void);
    int         (*init_platform_resource)(void);
    void        (*free_platform_resource)(void);
	int         (*fetch_sysconfig_para)(void);
	void        (*ts_reset)(void);
	void        (*ts_wakeup)(void);
};



#define ZT_NAME	"zt8031"

struct zt_ts_platform_data{
	u16	intr;		/* irq number	*/
};

#define PIOA_CFG1_REG    (gpio_addr+0x4)
#define PIOA_DATA             (gpio_addr+0x10) 
#define PIOI_DATA              (gpio_addr+0x130) 

#define POINT_DELAY      (1)
#define ZT8031_ADDR                     (0x90>>1)
#define ZT8031_MEASURE_TEMP0		(0x0 << 4)
#define ZT8031_MEASURE_AUX		(0x2 << 4)
#define ZT8031_MEASURE_TEMP1		(0x4 << 4)
#define ZT8031_ACTIVATE_XN		(0x8 << 4)
#define ZT8031_ACTIVATE_YN		(0x9 << 4)
#define ZT8031_ACTIVATE_YP_XN		(0xa << 4)
#define ZT8031_SETUP			(0xb << 4)
#define ZT8031_MEASURE_X		(0xc << 4)
#define ZT8031_MEASURE_Y		(0xd << 4)
#define ZT8031_MEASURE_Z1		(0xe << 4)
#define ZT8031_MEASURE_Z2		(0xf << 4)

#define ZT8031_POWER_OFF_IRQ_EN	        (0x0 << 2)
#define ZT8031_ADC_ON_IRQ_DIS0		(0x1 << 2)
#define ZT8031_ADC_OFF_IRQ_EN		(0x2 << 2)
#define ZT8031_ADC_ON_IRQ_DIS1		(0x3 << 2)

#define ZT8031_12BIT			(0x0 << 1)
#define ZT8031_8BIT			(0x1 << 1)

#define	MAX_12BIT			((1 << 12) - 1)

#define ADC_ON_12BIT	(ZT8031_12BIT | ZT8031_ADC_ON_IRQ_DIS1)

#define READ_Y		(ADC_ON_12BIT | ZT8031_MEASURE_Y)
#define READ_Z1		(ADC_ON_12BIT | ZT8031_MEASURE_Z1)
#define READ_Z2		(ADC_ON_12BIT | ZT8031_MEASURE_Z2)
#define READ_X		(ADC_ON_12BIT | ZT8031_MEASURE_X)
#define PWRDOWN		(ZT8031_12BIT | ZT8031_POWER_OFF_IRQ_EN)
#define PWRUP           (ZT8031_12BIT|ZT8031_ADC_ON_IRQ_DIS1)

#define POINT_DELAY                  (1)

#endif

