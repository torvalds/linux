/*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*
* Copyright (c) 2009
*
* ChangeLog
*
*
*/
#ifndef _CTP_PLATFORM_OPS_H_
#define _CTP_PLATFORM_OPS_H_
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <asm/irq.h>
#include <asm/io.h>

#include <mach/irqs.h>
#include <linux/i2c.h>

// gpio base address
#define PIO_BASE_ADDRESS	(0x01c20800)
#define PIO_RANGE_SIZE		(0x400)
#define GPIO_ENABLE
#define SYSCONFIG_GPIO_ENABLE

#define PIO_INT_STAT_OFFSET          (0x214)
#define PIO_INT_CTRL_OFFSET          (0x210)

#define PIO_PN_DAT_OFFSET(n)         ((n)*0x24 + 0x10)
//#define PIOI_DATA                    (0x130)
#define PIOH_DATA                    (0x10c)
#define PIOI_CFG3_OFFSET             (0x12c)

#define PRESS_DOWN	(1)
#define FREE_UP		(0)

#define IRQ_EINT0	(0)
#define IRQ_EINT1	(1)
#define IRQ_EINT2	(2)
#define IRQ_EINT3	(3)
#define IRQ_EINT4	(4)
#define IRQ_EINT5	(5)
#define IRQ_EINT6	(6)
#define IRQ_EINT7	(7)
#define IRQ_EINT8	(8)
#define IRQ_EINT9	(9)
#define IRQ_EINT10	(10)
#define IRQ_EINT11	(11)
#define IRQ_EINT12	(12)
#define IRQ_EINT13	(13)
#define IRQ_EINT14	(14)
#define IRQ_EINT15	(15)
#define IRQ_EINT16	(16)
#define IRQ_EINT17	(17)
#define IRQ_EINT18	(18)
#define IRQ_EINT19	(19)
#define IRQ_EINT20	(20)
#define IRQ_EINT21	(21)
#define IRQ_EINT22	(22)
#define IRQ_EINT23	(23)
#define IRQ_EINT24	(24)
#define IRQ_EINT25	(25)
#define IRQ_EINT26	(26)
#define IRQ_EINT27	(27)
#define IRQ_EINT28	(28)
#define IRQ_EINT29	(29)
#define IRQ_EINT30	(30)
#define IRQ_EINT31	(31)

typedef enum {
     PIO_INT_CFG0_OFFSET = 0x200,
     PIO_INT_CFG1_OFFSET = 0x204,
     PIO_INT_CFG2_OFFSET = 0x208,
     PIO_INT_CFG3_OFFSET = 0x20c,
} int_cfg_offset;

typedef enum{
	POSITIVE_EDGE = 0x0,
	NEGATIVE_EDGE = 0x1,
	HIGH_LEVEL = 0x2,
	LOW_LEVEL = 0x3,
	DOUBLE_EDGE = 0x4
} ext_int_mode;

struct ctp_platform_ops{
	int         irq;
	bool        pendown;
	int	(*get_pendown_state)(void);
	void        (*clear_penirq)(void);
	int         (*set_irq_mode)(char *major_key , char *subkey, ext_int_mode int_mode);
	int         (*set_gpio_mode)(void);
	int         (*judge_int_occur)(void);
	int         (*init_platform_resource)(void);
	void        (*free_platform_resource)(void);
	int         (*fetch_sysconfig_para)(void);
	void        (*ts_reset)(void);
	void        (*ts_wakeup)(void);
	int (*ts_detect)(struct i2c_client *client, struct i2c_board_info *info);
};

#endif /*_CTP_PLATFORM_OPS_H_*/

