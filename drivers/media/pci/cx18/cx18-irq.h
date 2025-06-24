/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *  cx18 interrupt handling
 *
 *  Copyright (C) 2007  Hans Verkuil <hverkuil@kernel.org>
 *  Copyright (C) 2008  Andy Walls <awalls@md.metrocast.net>
 */

#define HW2_I2C1_INT			(1 << 22)
#define HW2_I2C2_INT			(1 << 23)
#define HW2_INT_CLR_STATUS		0xc730c4
#define HW2_INT_MASK5_PCI		0xc730e4
#define SW1_INT_SET                     0xc73100
#define SW1_INT_STATUS                  0xc73104
#define SW1_INT_ENABLE_PCI              0xc7311c
#define SW2_INT_SET                     0xc73140
#define SW2_INT_STATUS                  0xc73144
#define SW2_INT_ENABLE_CPU              0xc73158
#define SW2_INT_ENABLE_PCI              0xc7315c

irqreturn_t cx18_irq_handler(int irq, void *dev_id);
