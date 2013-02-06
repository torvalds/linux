/* linux/drivers/media/video/samsung/tvout/hw_if/hdmi.c
 *
 * Copyright (c) 2009 Samsung Electronics
 *		http://www.samsung.com/
 *
 * Functions for HDMI of Samsung TVOUT driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/io.h>
#include <linux/delay.h>

#include <mach/map.h>
#include <mach/regs-hdmi.h>
#include <mach/regs-pmu.h>

#include "../s5p_tvout_common_lib.h"
#include "hw_if.h"

#undef tvout_dbg

#ifdef CONFIG_TVOUT_DEBUG
#define tvout_dbg(fmt, ...)						\
do {									\
	if (unlikely(tvout_dbg_flag & (1 << DBG_FLAG_HDMI))) {		\
		printk(KERN_INFO "\t\t[HDMI] %s(): " fmt,		\
			__func__, ##__VA_ARGS__);			\
	}								\
} while (0)
#else
#define tvout_dbg(fmt, ...)
#endif


/****************************************
 * Definitions for HDMI_PHY
 ***************************************/

#define PHY_I2C_ADDRESS		0x70
#define PHY_REG_MODE_SET_DONE	0x1F

#define I2C_ACK			(1 << 7)
#define I2C_INT			(1 << 5)
#define I2C_PEND		(1 << 4)
#define I2C_INT_CLEAR		(0 << 4)
#define I2C_CLK			(0x41)
#define I2C_CLK_PEND_INT	(I2C_CLK | I2C_INT_CLEAR | I2C_INT)
#define I2C_ENABLE		(1 << 4)
#define I2C_START		(1 << 5)
#define I2C_MODE_MTX		0xC0
#define I2C_MODE_MRX		0x80
#define I2C_IDLE		0

#define STATE_IDLE		0
#define STATE_TX_EDDC_SEGADDR	1
#define STATE_TX_EDDC_SEGNUM	2
#define STATE_TX_DDC_ADDR	3
#define STATE_TX_DDC_OFFSET	4
#define STATE_RX_DDC_ADDR	5
#define STATE_RX_DDC_DATA	6
#define STATE_RX_ADDR		7
#define STATE_RX_DATA		8
#define STATE_TX_ADDR		9
#define STATE_TX_DATA		10
#define STATE_TX_STOP		11
#define STATE_RX_STOP		12




static struct {
	s32	state;
	u8	*buffer;
	s32	bytes;
} i2c_hdmi_phy_context;




/****************************************
 * Definitions for HDMI
 ***************************************/
#define HDMI_IRQ_TOTAL_NUM	6


/* private data area */
void __iomem	*hdmi_base;
void __iomem	*i2c_hdmi_phy_base;

irqreturn_t	(*s5p_hdmi_isr_ftn[HDMI_IRQ_TOTAL_NUM])(int irq, void *);
spinlock_t	lock_hdmi;

#ifdef	CONFIG_HDMI_PHY_32N
static u8 phy_config[][3][32] = {
	{/* freq = 25.200 MHz */
		{
		    0x01, 0x51, 0x2a, 0x75, 0x40, 0x01, 0x00, 0x08,
		    0x82, 0x80, 0xfc, 0xd8, 0x45, 0xa0, 0xac, 0x80,
		    0x08, 0x80, 0x11, 0x04, 0x02, 0x22, 0x44, 0x86,
		    0x54, 0xf4, 0x24, 0x00, 0x00, 0x00, 0x01, 0x00,
		}, {
		    0x01, 0x52, 0x69, 0x75, 0x57, 0x01, 0x00, 0x08,
		    0x82, 0x80, 0x3b, 0xd9, 0x45, 0xa0, 0xac, 0x80,
		    0x08, 0x80, 0x11, 0x04, 0x02, 0x22, 0x44, 0x86,
		    0x54, 0xc3, 0x24, 0x00, 0x00, 0x00, 0x01, 0x00,
		}, {
		    0x01, 0x52, 0x3f, 0x35, 0x63, 0x01, 0x00, 0x08,
		    0x82, 0x80, 0xbd, 0xd8, 0x45, 0xa0, 0xac, 0x80,
		    0x08, 0x80, 0x11, 0x04, 0x02, 0x22, 0x44, 0x86,
		    0x54, 0xa3, 0x24, 0x00, 0x00, 0x00, 0x01, 0x00,
		},
	}, {/* freq = 25.175 MHz */
		{
		    0x01, 0xd1, 0x1f, 0x50, 0x40, 0x20, 0x1e, 0x08,
		    0x81, 0xa0, 0xbd, 0xd8, 0x45, 0xa0, 0xac, 0x80,
		    0x08, 0x80, 0x11, 0x04, 0x02, 0x22, 0x44, 0x86,
		    0x54, 0xf4, 0x24, 0x00, 0x00, 0x00, 0x01, 0x00,
		}, {
		    0x01, 0xd1, 0x27, 0x51, 0x15, 0x40, 0x2b, 0x08,
		    0x81, 0xa0, 0xec, 0xd8, 0x45, 0xa0, 0x34, 0x80,
		    0x08, 0x80, 0x32, 0x04, 0x02, 0x22, 0x44, 0x86,
		    0x54, 0xc3, 0x24, 0x00, 0x00, 0x00, 0x01, 0x00,
		}, {
		    0x01, 0xd1, 0x1f, 0x30, 0x23, 0x20, 0x1e, 0x08,
		    0x81, 0xa0, 0xbd, 0xd8, 0x45, 0xa0, 0x34, 0x80,
		    0x08, 0x80, 0x32, 0x04, 0x02, 0x22, 0x44, 0x86,
		    0x54, 0xa3, 0x24, 0x00, 0x00, 0x00, 0x01, 0x00,
		},
	}, {/* freq = 27 MHz */
		{
		    0x01, 0x51, 0x2d, 0x75, 0x40, 0x01, 0x00, 0x08,
		    0x82, 0xa0, 0x0e, 0xd9, 0x45, 0xa0, 0xac, 0x80,
		    0x08, 0x80, 0x11, 0x04, 0x02, 0x22, 0x44, 0x86,
		    0x54, 0xe3, 0x24, 0x00, 0x00, 0x00, 0x01, 0x00,
		}, {
		    0x01, 0xd1, 0x38, 0x74, 0x57, 0x08, 0x04, 0x08,
		    0x80, 0x80, 0x52, 0xd9, 0x45, 0xa0, 0xac, 0x80,
		    0x08, 0x80, 0x11, 0x04, 0x02, 0x22, 0x44, 0x86,
		    0x54, 0xb4, 0x24, 0x00, 0x00, 0x00, 0x01, 0x00,
		}, {
		    0x01, 0xd1, 0x22, 0x31, 0x63, 0x08, 0xfc, 0x08,
		    0x86, 0xa0, 0xcb, 0xd8, 0x45, 0xa0, 0xac, 0x80,
		    0x08, 0x80, 0x11, 0x04, 0x02, 0x22, 0x44, 0x86,
		    0x54, 0x98, 0x24, 0x00, 0x00, 0x00, 0x01, 0x00,
		},
	}, {/* freq = 27.027 MHz */
		{
		    0x01, 0xd1, 0x2d, 0x72, 0x40, 0x64, 0x12, 0x08,
		    0x43, 0xa0, 0x0e, 0xd9, 0x45, 0xa0, 0xac, 0x80,
		    0x08, 0x80, 0x11, 0x04, 0x02, 0x22, 0x44, 0x86,
		    0x54, 0xe3, 0x24, 0x00, 0x00, 0x00, 0x01, 0x00,
		}, {
		    0x01, 0xd1, 0x38, 0x74, 0x57, 0x50, 0x31, 0x01,
		    0x80, 0x80, 0x52, 0xd9, 0x45, 0xa0, 0xac, 0x80,
		    0x08, 0x80, 0x11, 0x04, 0x02, 0x22, 0x44, 0x86,
		    0x54, 0xb6, 0x24, 0x00, 0x00, 0x00, 0x01, 0x00,
		}, {
		    0x01, 0xd4, 0x87, 0x31, 0x63, 0x64, 0x1b, 0x20,
		    0x19, 0xa0, 0xcb, 0xd8, 0x45, 0xa0, 0xac, 0x80,
		    0x08, 0x80, 0x11, 0x04, 0x02, 0x22, 0x44, 0x86,
		    0x54, 0x98, 0x24, 0x00, 0x00, 0x00, 0x01, 0x00,
		},
	}, {/* freq = 54 MHz */
		{
		    0x01, 0x51, 0x2d, 0x35, 0x40, 0x01, 0x00, 0x08,
		    0x82, 0x80, 0x0e, 0xd9, 0x45, 0xa0, 0xac, 0x80,
		    0x08, 0x80, 0x11, 0x04, 0x02, 0x22, 0x44, 0x86,
		    0x54, 0xe4, 0x24, 0x01, 0x00, 0x00, 0x01, 0x00,
		}, {
		    0x01, 0xd1, 0x38, 0x35, 0x53, 0x08, 0x04, 0x08,
		    0x88, 0xa0, 0x52, 0xd8, 0x45, 0xa0, 0xac, 0x80,
		    0x08, 0x80, 0x11, 0x04, 0x02, 0x22, 0x44, 0x86,
		    0x54, 0xb6, 0x24, 0x01, 0x00, 0x00, 0x01, 0x00,
		}, {
		    0x01, 0xd1, 0x22, 0x11, 0x61, 0x08, 0xfc, 0x08,
		    0x86, 0xa0, 0xcb, 0xd8, 0x45, 0xa0, 0xac, 0x80,
		    0x08, 0x80, 0x11, 0x04, 0x02, 0x22, 0x44, 0x86,
		    0x54, 0x98, 0x24, 0x01, 0x00, 0x00, 0x01, 0x00,
		},
	}, {/* freq = 54.054 MHz */
		{
		    0x01, 0xd1, 0x2d, 0x32, 0x40, 0x64, 0x12, 0x08,
		    0x43, 0xa0, 0x0e, 0xd9, 0x45, 0xa0, 0xac, 0x80,
		    0x08, 0x80, 0x11, 0x04, 0x02, 0x22, 0x44, 0x86,
		    0x54, 0xe3, 0x24, 0x01, 0x00, 0x00, 0x01, 0x00,
		}, {
		    0x01, 0xd2, 0x70, 0x34, 0x53, 0x50, 0x31, 0x08,
		    0x80, 0x80, 0x52, 0xd9, 0x45, 0xa0, 0xac, 0x80,
		    0x08, 0x80, 0x11, 0x04, 0x02, 0x22, 0x44, 0x86,
		    0x54, 0xb6, 0x24, 0x01, 0x00, 0x00, 0x01, 0x00,
		}, {
		    0x01, 0xd4, 0x87, 0x11, 0x61, 0x64, 0x1b, 0x20,
		    0x19, 0xa0, 0xcb, 0xd8, 0x45, 0xa0, 0xac, 0x80,
		    0x08, 0x80, 0x11, 0x04, 0x02, 0x22, 0x44, 0x86,
		    0x54, 0x98, 0x24, 0x01, 0x00, 0x00, 0x01, 0x00,
		},
	}, {/* freq = 74.250 MHz */
		{
		    0x01, 0xd1, 0x1f, 0x10, 0x40, 0x40, 0xf8, 0x08,
		    0x81, 0xa0, 0xba, 0xd8, 0x45, 0xa0, 0xac, 0x80,
		    0x3c, 0x80, 0x11, 0x04, 0x02, 0x22, 0x44, 0x86,
		    0x54, 0xa5, 0x24, 0x01, 0x00, 0x00, 0x01, 0x00,
		}, {
		    0x01, 0xd1, 0x27, 0x11, 0x51, 0x40, 0xd6, 0x08,
		    0x81, 0xa0, 0xe8, 0xd8, 0x45, 0xa0, 0xac, 0x80,
		    0x5a, 0x80, 0x11, 0x04, 0x02, 0x22, 0x44, 0x86,
		    0x54, 0x84, 0x24, 0x01, 0x00, 0x00, 0x01, 0x00,
		}, {
		    0x01, 0xd1, 0x2e, 0x12, 0x61, 0x40, 0x34, 0x08,
		    0x82, 0xa0, 0x16, 0xd9, 0x45, 0xa0, 0xac, 0x80,
		    0x5a, 0x80, 0x11, 0x04, 0x02, 0x22, 0x44, 0x86,
		    0x54, 0xb9, 0x25, 0x03, 0x00, 0x00, 0x01, 0x00,
		},
	}, {/* freq = 74.176 MHz */
		{
		    0x01, 0xd1, 0x1f, 0x10, 0x40, 0x5b, 0xef, 0x08,
		    0x81, 0xa0, 0xb9, 0xd8, 0x45, 0xa0, 0xac, 0x80,
		    0x5a, 0x80, 0x11, 0x04, 0x02, 0x22, 0x44, 0x86,
		    0x54, 0xa6, 0x24, 0x01, 0x00, 0x00, 0x01, 0x00,
		}, {
		    0x01, 0xd1, 0x27, 0x14, 0x51, 0x5b, 0xa7, 0x08,
		    0x84, 0xa0, 0xe8, 0xd8, 0x45, 0xa0, 0xac, 0x80,
		    0x5a, 0x80, 0x11, 0x04, 0x02, 0x22, 0x44, 0x86,
		    0x54, 0x85, 0x24, 0x01, 0x00, 0x00, 0x01, 0x00,
		}, {
		    0x01, 0xd2, 0x5d, 0x12, 0x61, 0x5b, 0xcd, 0x10,
		    0x43, 0xa0, 0x16, 0xd9, 0x45, 0xa0, 0xac, 0x80,
		    0x5a, 0x80, 0x11, 0x04, 0x02, 0x22, 0x44, 0x86,
		    0x54, 0xba, 0x25, 0x03, 0x00, 0x00, 0x01, 0x00,
		},
	}, {/* freq = 148.500 MHz  - Pre-emph + Higher Tx amp. */
		{
		    0x01, 0xd1, 0x1f, 0x00, 0x40, 0x40, 0xf8, 0x08,
		    0x81, 0xa0, 0xba, 0xd8, 0x45, 0xa0, 0xac, 0x80,
		    0x3c, 0x80, 0x11, 0x04, 0x02, 0x22, 0x44, 0x86,
		    0x54, 0x4b, 0x25, 0x03, 0x00, 0x00, 0x01, 0x00,
		}, {
		    0x01, 0xd1, 0x27, 0x01, 0x50, 0x40, 0xd6, 0x08,
		    0x81, 0xa0, 0xe8, 0xd8, 0x45, 0xa0, 0xac, 0x80,
		    0xad, 0x80, 0x11, 0x04, 0x02, 0x22, 0x44, 0x86,
		    0x54, 0x09, 0x25, 0x03, 0x00, 0x00, 0x01, 0x00,
		}, {
		    0x01, 0xd1, 0x2e, 0x02, 0x60, 0x40, 0x34, 0x08,
		    0x82, 0xa0, 0x16, 0xd9, 0x45, 0xa0, 0xac, 0x80,
		    0xad, 0x80, 0x11, 0x04, 0x02, 0x22, 0x44, 0x86,
		    0x54, 0xdd, 0x24, 0x03, 0x00, 0x00, 0x01, 0x00,
		},
	}, {/* freq = 148.352 MHz */
		{
		    0x01, 0xd2, 0x3e, 0x00, 0x40, 0x5b, 0xef, 0x08,
		    0x81, 0xa0, 0xb9, 0xd8, 0x45, 0xa0, 0xac, 0x80,
		    0x3c, 0x80, 0x11, 0x04, 0x02, 0x22, 0x44, 0x86,
		    0x54, 0x4b, 0x25, 0x03, 0x00, 0x00, 0x01, 0x00,
		}, {
		    0x01, 0xd1, 0x27, 0x04, 0x10, 0x5b, 0xa7, 0x08,
		    0x84, 0xa0, 0xe8, 0xd8, 0x45, 0xa0, 0xac, 0x80,
		    0xad, 0x80, 0x11, 0x04, 0x02, 0x22, 0x44, 0x86,
		    0x54, 0x09, 0x25, 0x03, 0x00, 0x00, 0x01, 0x00,
		}, {
		    0x01, 0xd2, 0x5d, 0x02, 0x20, 0x5b, 0xcd, 0x10,
		    0x43, 0xa0, 0x16, 0xd9, 0x45, 0xa0, 0xac, 0x80,
		    0xad, 0x80, 0x11, 0x04, 0x02, 0x22, 0x44, 0x86,
		    0x54, 0xdd, 0x24, 0x03, 0x00, 0x00, 0x01, 0x00,
		},
	}, {/* freq = 108.108 MHz */
		{
		    0x01, 0xd1, 0x2d, 0x12, 0x40, 0x64, 0x12, 0x08,
		    0x43, 0xa0, 0x0e, 0xd9, 0x45, 0xa0, 0xac, 0x80,
		    0x5a, 0x80, 0x11, 0x84, 0x02, 0x22, 0x44, 0x86,
		    0x54, 0xc7, 0x25, 0x03, 0x00, 0x00, 0x01, 0x00,
		}, {
		    0x01, 0xd2, 0x70, 0x14, 0x51, 0x50, 0x31, 0x08,
		    0x80, 0x80, 0x5e, 0xd9, 0x45, 0xa0, 0xac, 0x80,
		    0x5a, 0x80, 0x11, 0x84, 0x02, 0x22, 0x44, 0x86,
		    0x54, 0x6c, 0x25, 0x03, 0x00, 0x00, 0x01, 0x00,
		}, {
		    0x01, 0xd4, 0x87, 0x01, 0x60, 0x64, 0x1b, 0x20,
		    0x19, 0xa0, 0xcb, 0xd8, 0x45, 0xa0, 0xac, 0x80,
		    0x5a, 0x80, 0x11, 0x84, 0x02, 0x22, 0x44, 0x86,
		    0x54, 0x2f, 0x25, 0x03, 0x00, 0x00, 0x01, 0x00,
		},
	}, {/* freq = 72 MHz */
		{
		    0x01, 0x51, 0x1e, 0x15, 0x40, 0x01, 0x00, 0x08,
		    0x82, 0x80, 0xb4, 0xd8, 0x45, 0xa0, 0xac, 0x80,
		    0x5a, 0x80, 0x11, 0x84, 0x02, 0x22, 0x44, 0x86,
		    0x54, 0xab, 0x24, 0x01, 0x00, 0x00, 0x01, 0x00,
		}, {
		    0x01, 0x52, 0x4b, 0x15, 0x51, 0x01, 0x00, 0x08,
		    0x82, 0x80, 0xe1, 0xd8, 0x45, 0xa0, 0xac, 0x80,
		    0x5a, 0x80, 0x11, 0x84, 0x02, 0x22, 0x44, 0x86,
		    0x54, 0x89, 0x24, 0x01, 0x00, 0x00, 0x01, 0x00,
		}, {
		    0x01, 0x51, 0x2d, 0x15, 0x61, 0x01, 0x00, 0x08,
		    0x82, 0x80, 0x0e, 0xd9, 0x45, 0xa0, 0xac, 0x80,
		    0x5a, 0x80, 0x11, 0x84, 0x02, 0x22, 0x44, 0x86,
		    0x54, 0xc7, 0x25, 0x03, 0x00, 0x00, 0x01, 0x00,
		},
	}, {/* freq = 25 MHz */
		{
		    0x01, 0xd1, 0x2a, 0x72, 0x40, 0x3c, 0xd8, 0x08,
		    0x86, 0xa0, 0xfa, 0xd8, 0x45, 0xa0, 0xac, 0x80,
		    0x08, 0x80, 0x11, 0x84, 0x02, 0x22, 0x44, 0x86,
		    0x54, 0xf6, 0x24, 0x00, 0x00, 0x00, 0x01, 0x00,
		}, {
		    0x01, 0xd1, 0x27, 0x51, 0x55, 0x40, 0x08, 0x08,
		    0x81, 0xa0, 0xea, 0xd8, 0x45, 0xa0, 0xac, 0x80,
		    0x08, 0x80, 0x11, 0x84, 0x02, 0x22, 0x44, 0x86,
		    0x54, 0xc5, 0x24, 0x00, 0x00, 0x00, 0x01, 0x00,
		}, {
		    0x01, 0xd2, 0x1f, 0x30, 0x63, 0x40, 0x20, 0x08,
		    0x81, 0x80, 0xbc, 0xd8, 0x45, 0xa0, 0xac, 0x80,
		    0x08, 0x80, 0x11, 0x84, 0x02, 0x22, 0x44, 0x86,
		    0x54, 0xa4, 0x24, 0x00, 0x00, 0x00, 0x01, 0x00,
		},
	}, {/* freq = 65 MHz */
		{
		    0x01, 0xd1, 0x36, 0x34, 0x40, 0x0c, 0x04, 0x08,
		    0x82, 0xa0, 0x45, 0xd9, 0x45, 0xa0, 0xac, 0x80,
		    0x5a, 0x80, 0x11, 0x84, 0x02, 0x22, 0x44, 0x86,
		    0x54, 0xbd, 0x24, 0x01, 0x00, 0x00, 0x01, 0x00,
		}, {
		    0x01, 0xd1, 0x22, 0x11, 0x51, 0x30, 0xf2, 0x08,
		    0x86, 0xa0, 0xcb, 0xd8, 0x45, 0xa0, 0xac, 0x80,
		    0x5a, 0x80, 0x11, 0x84, 0x02, 0x22, 0x44, 0x86,
		    0x54, 0x97, 0x24, 0x01, 0x00, 0x00, 0x01, 0x00,
		}, {
		    0x01, 0xd1, 0x29, 0x12, 0x61, 0x40, 0xd0, 0x08,
		    0x87, 0xa0, 0xf4, 0xd8, 0x45, 0xa0, 0xac, 0x80,
		    0x5a, 0x80, 0x11, 0x84, 0x02, 0x22, 0x44, 0x86,
		    0x54, 0x7e, 0x24, 0x01, 0x00, 0x00, 0x01, 0x00,
		},
	}, {/* freq = 108 MHz */
		{
		    0x01, 0x51, 0x2d, 0x15, 0x40, 0x01, 0x00, 0x08,
		    0x82, 0x80, 0x0e, 0xd9, 0x45, 0xa0, 0xac, 0x80,
		    0x5a, 0x80, 0x11, 0x84, 0x02, 0x22, 0x44, 0x86,
		    0x54, 0xc7, 0x25, 0x03, 0x00, 0x00, 0x01, 0x00,
		}, {
		    0x01, 0xd1, 0x38, 0x14, 0x51, 0x08, 0x04, 0x08,
		    0x80, 0x80, 0x52, 0xd9, 0x45, 0xa0, 0xac, 0x80,
		    0x5a, 0x80, 0x11, 0x84, 0x02, 0x22, 0x44, 0x86,
		    0x54, 0x6c, 0x25, 0x03, 0x00, 0x00, 0x01, 0x00,
		}, {
		    0x01, 0xd1, 0x22, 0x01, 0x60, 0x08, 0xfc, 0x08,
		    0x86, 0xa0, 0xcb, 0xd8, 0x45, 0xa0, 0xac, 0x80,
		    0x5a, 0x80, 0x11, 0x84, 0x02, 0x22, 0x44, 0x86,
		    0x54, 0x2f, 0x25, 0x03, 0x00, 0x00, 0x01, 0x00,
		},
	}, {/* freq = 162 MHz */
		{
		    0x01, 0x54, 0x87, 0x05, 0x40, 0x01, 0x00, 0x08,
		    0x82, 0x80, 0xcb, 0xd8, 0x45, 0xa0, 0xac, 0x80,
		    0xad, 0x80, 0x11, 0x84, 0x02, 0x22, 0x44, 0x86,
		    0x54, 0x2f, 0x25, 0x03, 0x00, 0x00, 0x01, 0x00,
		}, {
		    0x01, 0xd1, 0x2a, 0x02, 0x50, 0x40, 0x18, 0x08,
		    0x86, 0xa0, 0xfd, 0xd8, 0x45, 0xa0, 0xac, 0x80,
		    0xad, 0x80, 0x11, 0x84, 0x02, 0x22, 0x44, 0x86,
		    0x54, 0xf3, 0x24, 0x03, 0x00, 0x00, 0x01, 0x00,
		}, {
		    0x01, 0xd1, 0x33, 0x04, 0x60, 0x40, 0xd0, 0x08,
		    0x85, 0xa0, 0x32, 0xd9, 0x45, 0xa0, 0xac, 0x80,
		    0xad, 0x80, 0x11, 0x84, 0x02, 0x22, 0x44, 0x86,
		    0x54, 0xca, 0x24, 0x03, 0x00, 0x00, 0x01, 0x00,
		},
	},
};
#else
static const u8 phy_config[][3][32] = {
	{ /* freq = 25.200 MHz */
		{
			0x01, 0x05, 0x00, 0xD8, 0x10, 0x1C, 0x30, 0x40,
			0x6B, 0x10, 0x02, 0x51, 0x5f, 0xF1, 0x54, 0x7e,
			0x84, 0x00, 0x10, 0x38, 0x00, 0x08, 0x10, 0xE0,
			0x22, 0x40, 0xf3, 0x26, 0x00, 0x00, 0x00, 0x80,
		}, {
			0x01, 0x05, 0x00, 0xD8, 0x10, 0x1C, 0x30, 0x40,
			0x6B, 0x10, 0x02, 0x51, 0x9f, 0xF6, 0x54, 0x9e,
			0x84, 0x00, 0x32, 0x38, 0x00, 0xB8, 0x10, 0xE0,
			0x22, 0x40, 0xc2, 0x26, 0x00, 0x00, 0x00, 0x80,
		}, {
			0x01, 0x05, 0x00, 0xD8, 0x10, 0x1C, 0x30, 0x40,
			0x6B, 0x10, 0x02, 0x51, 0xFf, 0xF3, 0x54, 0xbd,
			0x84, 0x00, 0x30, 0x38, 0x00, 0xA4, 0x10, 0xE0,
			0x22, 0x40, 0xa2, 0x26, 0x00, 0x00, 0x00, 0x80,
		},
	}, { /* freq = 25.175 MHz */
		{
			0x01, 0x05, 0x00, 0xD8, 0x10, 0x9C, 0x1e, 0x20,
			0x6B, 0x50, 0x10, 0x51, 0xf1, 0x31, 0x54, 0xbd,
			0x84, 0x00, 0x10, 0x38, 0x00, 0x08, 0x10, 0xE0,
			0x22, 0x40, 0xf3, 0x26, 0x00, 0x00, 0x00, 0x80,
		}, {
			0x01, 0x05, 0x00, 0xD8, 0x10, 0x9C, 0x2b, 0x40,
			0x6B, 0x50, 0x10, 0x51, 0xF2, 0x32, 0x54, 0xec,
			0x84, 0x00, 0x10, 0x38, 0x00, 0xB8, 0x10, 0xE0,
			0x22, 0x40, 0xc2, 0x26, 0x00, 0x00, 0x00, 0x80,
		}, {
			0x01, 0x05, 0x00, 0xD8, 0x10, 0x9C, 0x1e, 0x20,
			0x6B, 0x10, 0x02, 0x51, 0xf1, 0x31, 0x54, 0xbd,
			0x84, 0x00, 0x10, 0x38, 0x00, 0xA4, 0x10, 0xE0,
			0x22, 0x40, 0xa2, 0x26, 0x00, 0x00, 0x00, 0x80,
		},
	}, { /* freq = 27 MHz */
		{
			0x01, 0x05, 0x00, 0xD8, 0x10, 0x1C, 0x30, 0x40,
			0x6B, 0x10, 0x02, 0x51, 0xDf, 0xF2, 0x54, 0x87,
			0x84, 0x00, 0x30, 0x38, 0x00, 0x08, 0x10, 0xE0,
			0x22, 0x40, 0xe3, 0x26, 0x00, 0x00, 0x00, 0x80,
		}, {
			0x01, 0x05, 0x00, 0xD8, 0x10, 0x9C, 0x02, 0x08,
			0x6A, 0x10, 0x02, 0x51, 0xCf, 0xF1, 0x54, 0xa9,
			0x84, 0x00, 0x10, 0x38, 0x00, 0xB8, 0x10, 0xE0,
			0x22, 0x40, 0xb5, 0x26, 0x00, 0x00, 0x00, 0x80,
		}, {
			0x01, 0x05, 0x00, 0xD8, 0x10, 0x9C, 0xfc, 0x08,
			0x6B, 0x10, 0x02, 0x51, 0x2f, 0xF2, 0x54, 0xcb,
			0x84, 0x00, 0x10, 0x38, 0x00, 0xA4, 0x10, 0xE0,
			0x22, 0x40, 0x97, 0x26, 0x00, 0x00, 0x00, 0x80,
		},
	}, { /* freq = 27.027 MHz */
		{
			0x01, 0x05, 0x00, 0xD4, 0x10, 0x9C, 0x09, 0x64,
			0x6B, 0x10, 0x02, 0x51, 0xDf, 0xF2, 0x54, 0x87,
			0x84, 0x00, 0x30, 0x38, 0x00, 0x08, 0x10, 0xE0,
			0x22, 0x40, 0xe2, 0x26, 0x00, 0x00, 0x00, 0x80,
		}, {
			0x01, 0x05, 0x00, 0xD4, 0x10, 0x9C, 0x31, 0x50,
			0x6B, 0x10, 0x02, 0x51, 0x8f, 0xF3, 0x54, 0xa9,
			0x84, 0x00, 0x30, 0x38, 0x00, 0xB8, 0x10, 0xE0,
			0x22, 0x40, 0xb5, 0x26, 0x00, 0x00, 0x00, 0x80,
		}, {
			0x01, 0x05, 0x00, 0x10, 0x10, 0x9C, 0x1b, 0x64,
			0x6F, 0x10, 0x02, 0x51, 0x7f, 0xF8, 0x54, 0xcb,
			0x84, 0x00, 0x32, 0x38, 0x00, 0xA4, 0x10, 0xE0,
			0x22, 0x40, 0x97, 0x26, 0x00, 0x00, 0x00, 0x80,
		},
	}, { /* freq = 54 MHz */
		{
			0x01, 0x05, 0x00, 0xD8, 0x10, 0x1C, 0x30, 0x40,
			0x6B, 0x10, 0x01, 0x51, 0xDf, 0xF2, 0x54, 0x87,
			0x84, 0x00, 0x30, 0x38, 0x00, 0x08, 0x10, 0xE0,
			0x22, 0x40, 0xe3, 0x26, 0x01, 0x00, 0x00, 0x80,
		}, {
			0x01, 0x05, 0x00, 0xD8, 0x10, 0x9C, 0x02, 0x08,
			0x6A, 0x10, 0x01, 0x51, 0xCf, 0xF1, 0x54, 0xa9,
			0x84, 0x00, 0x10, 0x38, 0x00, 0xF8, 0x10, 0xE0,
			0x22, 0x40, 0xb5, 0x26, 0x01, 0x00, 0x00, 0x80,
		}, {
			0x01, 0x05, 0x00, 0xD8, 0x10, 0x9C, 0xfc, 0x08,
			0x6B, 0x10, 0x01, 0x51, 0x2f, 0xF2, 0x54, 0xcb,
			0x84, 0x00, 0x10, 0x38, 0x00, 0xE4, 0x10, 0xE0,
			0x22, 0x40, 0x97, 0x26, 0x01, 0x00, 0x00, 0x80,
		},
	}, { /* freq = 54.054 MHz */
		{
			0x01, 0x05, 0x00, 0xd4, 0x10, 0x9C, 0x09, 0x64,
			0x6B, 0x10, 0x01, 0x51, 0xDf, 0xF2, 0x54, 0x87,
			0x84, 0x00, 0x30, 0x38, 0x00, 0x08, 0x10, 0xE0,
			0x22, 0x40, 0xe2, 0x26, 0x01, 0x00, 0x00, 0x80,
		}, {
			0x01, 0x05, 0x00, 0xd4, 0x10, 0x9C, 0x31, 0x50,
			0x6B, 0x10, 0x01, 0x51, 0x8f, 0xF3, 0x54, 0xa9,
			0x84, 0x00, 0x30, 0x38, 0x00, 0xF8, 0x10, 0xE0,
			0x22, 0x40, 0xb5, 0x26, 0x01, 0x00, 0x00, 0x80,
		}, {
			0x01, 0x05, 0x00, 0x10, 0x10, 0x9C, 0x1b, 0x64,
			0x6F, 0x10, 0x01, 0x51, 0x7f, 0xF8, 0x54, 0xcb,
			0x84, 0x00, 0x32, 0x38, 0x00, 0xE4, 0x10, 0xE0,
			0x22, 0x40, 0x97, 0x26, 0x01, 0x00, 0x00, 0x80,
		},
	}, { /* freq = 74.250 MHz */
		{
			0x01, 0x05, 0x00, 0xD8, 0x10, 0x9C, 0xf8, 0x40,
			0x6A, 0x10, 0x01, 0x51, 0xff, 0xF1, 0x54, 0xba,
			0x84, 0x00, 0x10, 0x38, 0x00, 0x08, 0x10, 0xE0,
			0x22, 0x40, 0xa4, 0x26, 0x01, 0x00, 0x00, 0x80,
		}, {
			0x01, 0x05, 0x00, 0xD8, 0x10, 0x9C, 0xd6, 0x40,
			0x6B, 0x10, 0x01, 0x51, 0x7f, 0xF2, 0x54, 0xe8,
			0x84, 0x00, 0x10, 0x38, 0x00, 0xF8, 0x10, 0xE0,
			0x22, 0x40, 0x83, 0x26, 0x01, 0x00, 0x00, 0x80,
		}, {
			0x01, 0x05, 0x00, 0xD8, 0x10, 0x9C, 0x34, 0x40,
			0x6B, 0x10, 0x01, 0x51, 0xef, 0xF2, 0x54, 0x16,
			0x85, 0x00, 0x10, 0x38, 0x00, 0xE4, 0x10, 0xE0,
			0x22, 0x40, 0xdc, 0x26, 0x02, 0x00, 0x00, 0x80,
		},
	}, { /* freq = 74.176 MHz */
		{
			0x01, 0x05, 0x00, 0xD8, 0x10, 0x9C, 0xef, 0x5B,
			0x6D, 0x10, 0x01, 0x51, 0xef, 0xF3, 0x54, 0xb9,
			0x84, 0x00, 0x30, 0x38, 0x00, 0x08, 0x10, 0xE0,
			0x22, 0x40, 0xa5, 0x26, 0x01, 0x00, 0x00, 0x80,
		}, {
			0x01, 0x05, 0x00, 0x10, 0x10, 0x9C, 0xab, 0x5B,
			0x6F, 0x10, 0x01, 0x51, 0xbf, 0xF9, 0x54, 0xe8,
			0x84, 0x00, 0x32, 0x38, 0x00, 0xF8, 0x10, 0xE0,
			0x22, 0x40, 0x84, 0x26, 0x01, 0x00, 0x00, 0x80,
		}, {
			0x01, 0x05, 0x00, 0xD4, 0x10, 0x9C, 0xcd, 0x5B,
			0x6F, 0x10, 0x01, 0x51, 0xdf, 0xF5, 0x54, 0x16,
			0x85, 0x00, 0x30, 0x38, 0x00, 0xE4, 0x10, 0xE0,
			0x22, 0x40, 0xdc, 0x26, 0x02, 0x00, 0x00, 0x80,
		},
	}, { /* freq = 148.500 MHz  - Pre-emph + Higher Tx amp. */
		{
			0x01, 0x05, 0x00, 0xD8, 0x10, 0x9C, 0xf8, 0x40,
			0x6A, 0x18, 0x00, 0x51, 0xff, 0xF1, 0x54, 0xba,
			0x84, 0x00, 0x10, 0x38, 0x00, 0x08, 0x10, 0xE0,
			0x22, 0x40, 0xa4, 0x26, 0x02, 0x00, 0x00, 0x80,
		}, {
			0x01, 0x05, 0x00, 0xD8, 0x10, 0x9C, 0xd6, 0x40,
			0x6B, 0x18, 0x00, 0x51, 0x7f, 0xF2, 0x54, 0xe8,
			0x84, 0x00, 0x10, 0x38, 0x00, 0xF8, 0x10, 0xE0,
			0x23, 0x41, 0x83, 0x26, 0x02, 0x00, 0x00, 0x80,
		}, {
			0x01, 0x05, 0x00, 0xD8, 0x10, 0x9C, 0x34, 0x40,
			0x6B, 0x18, 0x00, 0x51, 0xef, 0xF2, 0x54, 0x16,
			0x85, 0x00, 0x10, 0x38, 0x00, 0xE4, 0x10, 0xE0,
			0x23, 0x41, 0x6d, 0x26, 0x02, 0x00, 0x00, 0x80,
		},
	}, { /* freq = 148.352 MHz */
		{
			0x01, 0x05, 0x00, 0xD8, 0x10, 0x9C, 0xef, 0x5B,
			0x6D, 0x18, 0x00, 0x51, 0xef, 0xF3, 0x54, 0xb9,
			0x84, 0x00, 0x30, 0x38, 0x00, 0x08, 0x10, 0xE0,
			0x22, 0x40, 0xa5, 0x26, 0x02, 0x00, 0x00, 0x80,
		}, {
			0x01, 0x05, 0x00, 0x10, 0x10, 0x9C, 0xab, 0x5B,
			0x6F, 0x18, 0x00, 0x51, 0xbf, 0xF9, 0x54, 0xe8,
			0x84, 0x00, 0x32, 0x38, 0x00, 0xF8, 0x10, 0xE0,
			0x23, 0x41, 0x84, 0x26, 0x02, 0x00, 0x00, 0x80,
		}, {
			0x01, 0x05, 0x00, 0xD4, 0x10, 0x9C, 0xcd, 0x5B,
			0x6F, 0x18, 0x00, 0x51, 0xdf, 0xF5, 0x54, 0x16,
			0x85, 0x00, 0x30, 0x38, 0x00, 0xE4, 0x10, 0xE0,
			0x23, 0x41, 0x6d, 0x26, 0x02, 0x00, 0x00, 0x80,
		},
	}, { /* freq = 108.108 MHz */
		{
			0x01, 0x05, 0x00, 0xD4, 0x10, 0x9C, 0x09, 0x64,
			0x6B, 0x18, 0x00, 0x51, 0xDf, 0xF2, 0x54, 0x87,
			0x84, 0x00, 0x30, 0x38, 0x00, 0x08, 0x10, 0xE0,
			0x22, 0x40, 0xe2, 0x26, 0x02, 0x00, 0x00, 0x80,
		}, {
			0x01, 0x05, 0x00, 0xD4, 0x10, 0x9C, 0x31, 0x50,
			0x6D, 0x18, 0x00, 0x51, 0x8f, 0xF3, 0x54, 0xa9,
			0x84, 0x00, 0x30, 0x38, 0x00, 0xF8, 0x10, 0xE0,
			0x22, 0x40, 0xb5, 0x26, 0x02, 0x00, 0x00, 0x80,
		}, {
			0x01, 0x05, 0x00, 0x10, 0x10, 0x9C, 0x1b, 0x64,
			0x6F, 0x18, 0x00, 0x51, 0x7f, 0xF8, 0x54, 0xcb,
			0x84, 0x00, 0x32, 0x38, 0x00, 0xE4, 0x10, 0xE0,
			0x22, 0x40, 0x97, 0x26, 0x02, 0x00, 0x00, 0x80,
		},
	}, { /* freq = 72 MHz */
		{
			0x01, 0x05, 0x00, 0xD8, 0x10, 0x1C, 0x30, 0x40,
			0x6B, 0x10, 0x01, 0x51, 0xEf, 0xF1, 0x54, 0xb4,
			0x84, 0x00, 0x10, 0x38, 0x00, 0x08, 0x10, 0xE0,
			0x22, 0x40, 0xaa, 0x26, 0x01, 0x00, 0x00, 0x80,
		}, {
			0x01, 0x05, 0x00, 0xD8, 0x10, 0x1C, 0x30, 0x40,
			0x6F, 0x10, 0x01, 0x51, 0xBf, 0xF4, 0x54, 0xe1,
			0x84, 0x00, 0x30, 0x38, 0x00, 0xF8, 0x10, 0xE0,
			0x22, 0x40, 0x88, 0x26, 0x01, 0x00, 0x00, 0x80,
		}, {
			0x01, 0x05, 0x00, 0xD8, 0x10, 0x1C, 0x30, 0x40,
			0x6B, 0x18, 0x00, 0x51, 0xDf, 0xF2, 0x54, 0x87,
			0x84, 0x00, 0x30, 0x38, 0x00, 0xE4, 0x10, 0xE0,
			0x22, 0x40, 0xe3, 0x26, 0x02, 0x00, 0x00, 0x80,
		},
	}, { /* freq = 25 MHz */
		{
			0x01, 0x05, 0x00, 0xD8, 0x10, 0x9C, 0x20, 0x40,
			0x6B, 0x50, 0x10, 0x51, 0xff, 0xF1, 0x54, 0xbc,
			0x84, 0x00, 0x10, 0x38, 0x00, 0x08, 0x10, 0xE0,
			0x22, 0x40, 0xf5, 0x26, 0x00, 0x00, 0x00, 0x80,
		}, {
			0x01, 0x05, 0x00, 0xD8, 0x10, 0x9C, 0x08, 0x40,
			0x6B, 0x50, 0x10, 0x51, 0x7f, 0xF2, 0x54, 0xea,
			0x84, 0x00, 0x10, 0x38, 0x00, 0xB8, 0x10, 0xE0,
			0x22, 0x40, 0xc4, 0x26, 0x00, 0x00, 0x00, 0x80,
		}, {
			0x01, 0x05, 0x00, 0xD8, 0x10, 0x9C, 0x20, 0x40,
			0x6B, 0x10, 0x02, 0x51, 0xff, 0xF1, 0x54, 0xbc,
			0x84, 0x00, 0x10, 0x38, 0x00, 0xA4, 0x10, 0xE0,
			0x22, 0x40, 0xa3, 0x26, 0x00, 0x00, 0x00, 0x80,
		},
	}, { /* freq = 65 MHz */
		{
			0x01, 0x05, 0x00, 0xD8, 0x10, 0x9C, 0x02, 0x0c,
			0x6B, 0x10, 0x01, 0x51, 0xBf, 0xF1, 0x54, 0xa3,
			0x84, 0x00, 0x10, 0x38, 0x00, 0x08, 0x10, 0xE0,
			0x22, 0x40, 0xbc, 0x26, 0x01, 0x00, 0x00, 0x80,
		}, {
			0x01, 0x05, 0x00, 0xD8, 0x10, 0x9C, 0xf2, 0x30,
			0x6A, 0x10, 0x01, 0x51, 0x2f, 0xF2, 0x54, 0xcb,
			0x84, 0x00, 0x10, 0x38, 0x00, 0xF8, 0x10, 0xE0,
			0x22, 0x40, 0x96, 0x26, 0x01, 0x00, 0x00, 0x80,
		}, {
			0x01, 0x05, 0x00, 0xD8, 0x10, 0x9C, 0xd0, 0x40,
			0x6B, 0x10, 0x01, 0x51, 0x9f, 0xF2, 0x54, 0xf4,
			0x84, 0x00, 0x10, 0x38, 0x00, 0xE4, 0x10, 0xE0,
			0x22, 0x40, 0x7D, 0x26, 0x01, 0x00, 0x00, 0x80,
		},
	}, { /* freq = 108 MHz */
		{
			0x01, 0x05, 0x00, 0xD8, 0x10, 0x1C, 0x30, 0x40,
			0x6D, 0x18, 0x00, 0x51, 0xDf, 0xF2, 0x54, 0x87,
			0x84, 0x00, 0x30, 0x38, 0x00, 0x08, 0x10, 0xE0,
			0x22, 0x40, 0xe3, 0x26, 0x02, 0x00, 0x00, 0x80,
		}, {
			0x01, 0x05, 0x00, 0xD8, 0x10, 0x9C, 0x02, 0x08,
			0x6A, 0x18, 0x00, 0x51, 0xCf, 0xF1, 0x54, 0xa9,
			0x84, 0x00, 0x10, 0x38, 0x00, 0xF8, 0x10, 0xE0,
			0x22, 0x40, 0xb5, 0x26, 0x02, 0x00, 0x00, 0x80,
		}, {
			0x01, 0x05, 0x00, 0xD8, 0x10, 0x9C, 0xfc, 0x08,
			0x6B, 0x18, 0x00, 0x51, 0x2f, 0xF2, 0x54, 0xcb,
			0x84, 0x00, 0x10, 0x38, 0x00, 0xE4, 0x10, 0xE0,
			0x22, 0x40, 0x97, 0x26, 0x02, 0x00, 0x00, 0x80,
		},
	}, { /* freq = 162 MHz */
		{
			0x01, 0x05, 0x00, 0xD8, 0x10, 0x1C, 0x30, 0x40,
			0x6F, 0x18, 0x00, 0x51, 0x7f, 0xF8, 0x54, 0xcb,
			0x84, 0x00, 0x32, 0x38, 0x00, 0x08, 0x10, 0xE0,
			0x22, 0x40, 0x97, 0x26, 0x02, 0x00, 0x00, 0x80,
		}, {
			0x01, 0x05, 0x00, 0xD8, 0x10, 0x9C, 0x18, 0x40,
			0x6B, 0x18, 0x00, 0x51, 0xAf, 0xF2, 0x54, 0xfd,
			0x84, 0x00, 0x10, 0x38, 0x00, 0xF8, 0x10, 0xE0,
			0x23, 0x41, 0x78, 0x26, 0x02, 0x00, 0x00, 0x80,
		}, {
			0x01, 0x05, 0x00, 0xD8, 0x10, 0x9C, 0xd0, 0x40,
			0x6B, 0x18, 0x00, 0x51, 0x3f, 0xF3, 0x54, 0x30,
			0x85, 0x00, 0x10, 0x38, 0x00, 0xE4, 0x10, 0xE0,
			0x23, 0x41, 0x64, 0x26, 0x02, 0x00, 0x00, 0x80,
		},
	},
};
#endif

#ifndef CONFIG_HDMI_PHY_32N
static void s5p_hdmi_reg_core_reset(void)
{
	writeb(0x0, hdmi_base + S5P_HDMI_CORE_RSTOUT);

	mdelay(10);

	writeb(0x1, hdmi_base + S5P_HDMI_CORE_RSTOUT);
}
#endif

static s32 s5p_hdmi_i2c_phy_interruptwait(void)
{
	u8 status, reg;
	s32 retval = 0;

	do {
		status = readb(i2c_hdmi_phy_base + HDMI_I2C_CON);

		if (status & I2C_PEND) {
			reg = readb(i2c_hdmi_phy_base + HDMI_I2C_STAT);
			break;
		}

	} while (1);

	return retval;
}

static s32 s5p_hdmi_i2c_phy_read(u8 addr, u8 nbytes, u8 *buffer)
{
	u8 reg;
	s32 ret = 0;
	u32 proc = true;

	i2c_hdmi_phy_context.state = STATE_RX_ADDR;
	i2c_hdmi_phy_context.buffer = buffer;
	i2c_hdmi_phy_context.bytes = nbytes;

	writeb(I2C_CLK | I2C_INT | I2C_ACK, i2c_hdmi_phy_base + HDMI_I2C_CON);
	writeb(I2C_ENABLE | I2C_MODE_MRX, i2c_hdmi_phy_base + HDMI_I2C_STAT);
	writeb(addr & 0xFE, i2c_hdmi_phy_base + HDMI_I2C_DS);
	writeb(I2C_ENABLE | I2C_START | I2C_MODE_MRX,
				i2c_hdmi_phy_base + HDMI_I2C_STAT);

	while (proc) {

		if (i2c_hdmi_phy_context.state != STATE_RX_STOP) {

			if (s5p_hdmi_i2c_phy_interruptwait() != 0) {
				tvout_err("interrupt wait failed!!!\n");
				ret = -1;
				break;
			}

		}

		switch (i2c_hdmi_phy_context.state) {
		case STATE_RX_DATA:
			reg = readb(i2c_hdmi_phy_base + HDMI_I2C_DS);
			*(i2c_hdmi_phy_context.buffer) = reg;

			i2c_hdmi_phy_context.buffer++;
			--(i2c_hdmi_phy_context.bytes);

			if (i2c_hdmi_phy_context.bytes == 1) {
				i2c_hdmi_phy_context.state = STATE_RX_STOP;
				writeb(I2C_CLK_PEND_INT,
					i2c_hdmi_phy_base + HDMI_I2C_CON);
			} else {
				writeb(I2C_CLK_PEND_INT | I2C_ACK,
					i2c_hdmi_phy_base + HDMI_I2C_CON);
			}

			break;

		case STATE_RX_ADDR:
			i2c_hdmi_phy_context.state = STATE_RX_DATA;

			if (i2c_hdmi_phy_context.bytes == 1) {
				i2c_hdmi_phy_context.state = STATE_RX_STOP;
				writeb(I2C_CLK_PEND_INT,
					i2c_hdmi_phy_base + HDMI_I2C_CON);
			} else {
				writeb(I2C_CLK_PEND_INT | I2C_ACK,
					i2c_hdmi_phy_base + HDMI_I2C_CON);
			}

			break;

		case STATE_RX_STOP:
			i2c_hdmi_phy_context.state = STATE_IDLE;

			reg = readb(i2c_hdmi_phy_base + HDMI_I2C_DS);

			*(i2c_hdmi_phy_context.buffer) = reg;

			writeb(I2C_MODE_MRX|I2C_ENABLE,
				i2c_hdmi_phy_base + HDMI_I2C_STAT);
			writeb(I2C_CLK_PEND_INT,
				i2c_hdmi_phy_base + HDMI_I2C_CON);
			writeb(I2C_MODE_MRX,
				i2c_hdmi_phy_base + HDMI_I2C_STAT);

			while (readb(i2c_hdmi_phy_base + HDMI_I2C_STAT) &
					I2C_START)
				usleep_range(1000, 1000);

			proc = false;
			break;

		case STATE_IDLE:
		default:
			tvout_err("error state!!!\n");

			ret = -1;

			proc = false;
			break;
		}

	}

	return ret;
}

static s32 s5p_hdmi_i2c_phy_write(u8 addr, u8 nbytes, u8 *buffer)
{
	u8 reg;
	s32 ret = 0;
	u32 proc = true;

	i2c_hdmi_phy_context.state = STATE_TX_ADDR;
	i2c_hdmi_phy_context.buffer = buffer;
	i2c_hdmi_phy_context.bytes = nbytes;

	writeb(I2C_CLK | I2C_INT | I2C_ACK, i2c_hdmi_phy_base + HDMI_I2C_CON);
	writeb(I2C_ENABLE | I2C_MODE_MTX, i2c_hdmi_phy_base + HDMI_I2C_STAT);
	writeb(addr & 0xFE, i2c_hdmi_phy_base + HDMI_I2C_DS);
	writeb(I2C_ENABLE | I2C_START | I2C_MODE_MTX,
				i2c_hdmi_phy_base + HDMI_I2C_STAT);

	while (proc) {

		if (s5p_hdmi_i2c_phy_interruptwait() != 0) {
			tvout_err("interrupt wait failed!!!\n");
			ret = -1;

			break;
		}

		switch (i2c_hdmi_phy_context.state) {
		case STATE_TX_ADDR:
		case STATE_TX_DATA:
			i2c_hdmi_phy_context.state = STATE_TX_DATA;

			reg = *(i2c_hdmi_phy_context.buffer);

			writeb(reg, i2c_hdmi_phy_base + HDMI_I2C_DS);

			i2c_hdmi_phy_context.buffer++;
			--(i2c_hdmi_phy_context.bytes);

			if (i2c_hdmi_phy_context.bytes == 0) {
				i2c_hdmi_phy_context.state = STATE_TX_STOP;
				writeb(I2C_CLK_PEND_INT,
					i2c_hdmi_phy_base + HDMI_I2C_CON);
			} else {
				writeb(I2C_CLK_PEND_INT | I2C_ACK,
					i2c_hdmi_phy_base + HDMI_I2C_CON);
			}

			break;

		case STATE_TX_STOP:
			i2c_hdmi_phy_context.state = STATE_IDLE;

			writeb(I2C_MODE_MTX | I2C_ENABLE,
				i2c_hdmi_phy_base + HDMI_I2C_STAT);
			writeb(I2C_CLK_PEND_INT,
				i2c_hdmi_phy_base + HDMI_I2C_CON);
			writeb(I2C_MODE_MTX,
				i2c_hdmi_phy_base + HDMI_I2C_STAT);

			while (readb(i2c_hdmi_phy_base + HDMI_I2C_STAT) &
					I2C_START)
				usleep_range(1000, 1000);

			proc = false;
			break;

		case STATE_IDLE:
		default:
			tvout_err("error state!!!\n");

			ret = -1;

			proc = false;
			break;
		}

	}

	return ret;
}

#ifdef S5P_HDMI_DEBUG
static void s5p_hdmi_print_phy_config(void)
{
	s32 size;
	int i = 0;
	u8 read_buffer[0x40] = {0, };
	size = sizeof(phy_config[0][0])
		/ sizeof(phy_config[0][0][0]);


	/* read data */
	if (s5p_hdmi_i2c_phy_read(PHY_I2C_ADDRESS, size, read_buffer) != 0) {
		tvout_err("s5p_hdmi_i2c_phy_read failed.\n");
		return;
	}

	printk(KERN_WARNING "read buffer :\n");

	for (i = 0; i < size; i++) {
		printk("0x%02x", read_buffer[i]);

		if ((i+1) % 8)
			printk(" ");
		else
			printk("\n");
	}
	printk(KERN_WARNING "\n");
}
#else
static inline void s5p_hdmi_print_phy_config(void) {}
#endif

#ifdef CONFIG_SND_SAMSUNG_SPDIF
static void s5p_hdmi_audio_set_config(
		enum s5p_tvout_audio_codec_type audio_codec)
{
	u32 data_type = (audio_codec == PCM) ?
			S5P_HDMI_SPDIFIN_CFG_LINEAR_PCM_TYPE :
			(audio_codec == AC3) ?
				S5P_HDMI_SPDIFIN_CFG_NO_LINEAR_PCM_TYPE : 0xff;

	tvout_dbg("audio codec type = %s\n",
		(audio_codec & PCM) ? "PCM" :
		(audio_codec & AC3) ? "AC3" :
		(audio_codec & MP3) ? "MP3" :
		(audio_codec & WMA) ? "WMA" : "Unknown");

	/* open SPDIF path on HDMI_I2S */
	writeb(S5P_HDMI_I2S_CLK_EN, hdmi_base + S5P_HDMI_I2S_CLK_CON);
	writeb(readl(hdmi_base + S5P_HDMI_I2S_MUX_CON) |
		S5P_HDMI_I2S_CUV_I2S_ENABLE |
		S5P_HDMI_I2S_MUX_ENABLE,
		hdmi_base + S5P_HDMI_I2S_MUX_CON);
	writeb(S5P_HDMI_I2S_CH_ALL_EN, hdmi_base + S5P_HDMI_I2S_MUX_CH);
	writeb(S5P_HDMI_I2S_CUV_RL_EN, hdmi_base + S5P_HDMI_I2S_MUX_CUV);

	writeb(S5P_HDMI_SPDIFIN_CFG_FILTER_2_SAMPLE | data_type |
		S5P_HDMI_SPDIFIN_CFG_PCPD_MANUAL_SET |
		S5P_HDMI_SPDIFIN_CFG_WORD_LENGTH_M_SET |
		S5P_HDMI_SPDIFIN_CFG_U_V_C_P_REPORT |
		S5P_HDMI_SPDIFIN_CFG_BURST_SIZE_2 |
		S5P_HDMI_SPDIFIN_CFG_DATA_ALIGN_32BIT,
		hdmi_base + S5P_HDMI_SPDIFIN_CONFIG_1);

	writeb(S5P_HDMI_SPDIFIN_CFG2_NO_CLK_DIV,
		hdmi_base + S5P_HDMI_SPDIFIN_CONFIG_2);
}

static void s5p_hdmi_audio_clock_enable(void)
{
	writeb(S5P_HDMI_SPDIFIN_CLK_ON, hdmi_base + S5P_HDMI_SPDIFIN_CLK_CTRL);
	writeb(S5P_HDMI_SPDIFIN_STATUS_CHK_OP_MODE,
		hdmi_base + S5P_HDMI_SPDIFIN_OP_CTRL);
}

static void s5p_hdmi_audio_set_repetition_time(
				enum s5p_tvout_audio_codec_type audio_codec,
				u32 bits, u32 frame_size_code)
{
	/* Only 4'b1011 24bit */
	u32 wl = 5 << 1 | 1;
	u32 rpt_cnt = (audio_codec == AC3) ? 1536 * 2 - 1 : 0;

	tvout_dbg("repetition count = %d\n", rpt_cnt);

	/* 24bit and manual mode */
	writeb(((rpt_cnt & 0xf) << 4) | wl,
		hdmi_base + S5P_HDMI_SPDIFIN_USER_VALUE_1);
	/* if PCM this value is 0 */
	writeb((rpt_cnt >> 4) & 0xff,
		hdmi_base + S5P_HDMI_SPDIFIN_USER_VALUE_2);
	/* if PCM this value is 0 */
	writeb(frame_size_code & 0xff,
		hdmi_base + S5P_HDMI_SPDIFIN_USER_VALUE_3);
	/* if PCM this value is 0 */
	writeb((frame_size_code >> 8) & 0xff,
		hdmi_base + S5P_HDMI_SPDIFIN_USER_VALUE_4);
}

static void s5p_hdmi_audio_irq_enable(u32 irq_en)
{
	writeb(irq_en, hdmi_base + S5P_HDMI_SPDIFIN_IRQ_MASK);
}
#else
static void s5p_hdmi_audio_i2s_config(
		enum s5p_tvout_audio_codec_type audio_codec,
		u32 sample_rate, u32 bits_per_sample,
		u32 frame_size_code,
		struct s5p_hdmi_audio *audio)
{
	u32 data_num, bit_ch, sample_frq;

	if (bits_per_sample == 20) {
		data_num = 2;
		bit_ch  = 1;
	} else if (bits_per_sample == 24) {
		data_num = 3;
		bit_ch  = 1;
	} else {
		data_num = 1;
		bit_ch  = 0;
	}

	writeb((S5P_HDMI_I2S_IN_DISABLE | S5P_HDMI_I2S_AUD_I2S |
		S5P_HDMI_I2S_CUV_I2S_ENABLE | S5P_HDMI_I2S_MUX_ENABLE),
		hdmi_base + S5P_HDMI_I2S_MUX_CON);

	writeb(S5P_HDMI_I2S_CH0_EN | S5P_HDMI_I2S_CH1_EN | S5P_HDMI_I2S_CH2_EN,
		hdmi_base + S5P_HDMI_I2S_MUX_CH);

	writeb(S5P_HDMI_I2S_CUV_RL_EN, hdmi_base + S5P_HDMI_I2S_MUX_CUV);

	sample_frq = (sample_rate == 44100) ? 0 :
			(sample_rate == 48000) ? 2 :
			(sample_rate == 32000) ? 3 :
			(sample_rate == 96000) ? 0xa : 0x0;

	/* readl(hdmi_base + S5P_HDMI_YMAX) */
	writeb(S5P_HDMI_I2S_CLK_DIS, hdmi_base + S5P_HDMI_I2S_CLK_CON);
	writeb(S5P_HDMI_I2S_CLK_EN, hdmi_base + S5P_HDMI_I2S_CLK_CON);

	writeb(readl(hdmi_base + S5P_HDMI_I2S_DSD_CON) | 0x01,
		hdmi_base + S5P_HDMI_I2S_DSD_CON);

	/* Configuration I2S input ports. Configure I2S_PIN_SEL_0~4 */
	writeb(S5P_HDMI_I2S_SEL_SCLK(5) | S5P_HDMI_I2S_SEL_LRCK(6),
			hdmi_base + S5P_HDMI_I2S_PIN_SEL_0);
	if (audio->channel == 2)
		/* I2S 2 channel */
		writeb(S5P_HDMI_I2S_SEL_SDATA1(1) | S5P_HDMI_I2S_SEL_SDATA2(4),
				hdmi_base + S5P_HDMI_I2S_PIN_SEL_1);
	else
		/* I2S 5.1 channel */
		writeb(S5P_HDMI_I2S_SEL_SDATA1(3) | S5P_HDMI_I2S_SEL_SDATA2(4),
				hdmi_base + S5P_HDMI_I2S_PIN_SEL_1);

	writeb(S5P_HDMI_I2S_SEL_SDATA3(1) | S5P_HDMI_I2S_SEL_SDATA2(2),
			hdmi_base + S5P_HDMI_I2S_PIN_SEL_2);
	writeb(S5P_HDMI_I2S_SEL_DSD(0), hdmi_base + S5P_HDMI_I2S_PIN_SEL_3);

	/* I2S_CON_1 & 2 */
	writeb(S5P_HDMI_I2S_SCLK_FALLING_EDGE | S5P_HDMI_I2S_L_CH_LOW_POL,
		hdmi_base + S5P_HDMI_I2S_CON_1);
	writeb(S5P_HDMI_I2S_MSB_FIRST_MODE |
		S5P_HDMI_I2S_SET_BIT_CH(bit_ch) |
		S5P_HDMI_I2S_SET_SDATA_BIT(data_num) |
		S5P_HDMI_I2S_BASIC_FORMAT,
		hdmi_base + S5P_HDMI_I2S_CON_2);

	/* Configure register related to CUV information */
	writeb(S5P_HDMI_I2S_CH_STATUS_MODE_0 |
		S5P_HDMI_I2S_2AUD_CH_WITHOUT_PREEMPH |
		S5P_HDMI_I2S_COPYRIGHT |
		S5P_HDMI_I2S_LINEAR_PCM |
		S5P_HDMI_I2S_CONSUMER_FORMAT,
		hdmi_base + S5P_HDMI_I2S_CH_ST_0);
	writeb(S5P_HDMI_I2S_CD_PLAYER,
		hdmi_base + S5P_HDMI_I2S_CH_ST_1);

	if (audio->channel == 2)
		/* Audio channel to 5.1 */
		writeb(S5P_HDMI_I2S_SET_SOURCE_NUM(0),
				hdmi_base + S5P_HDMI_I2S_CH_ST_2);
	else
		writeb(S5P_HDMI_I2S_SET_SOURCE_NUM(0) |
				S5P_HDMI_I2S_SET_CHANNEL_NUM(0x6),
				hdmi_base + S5P_HDMI_I2S_CH_ST_2);

	writeb(S5P_HDMI_I2S_CLK_ACCUR_LEVEL_2 |
		S5P_HDMI_I2S_SET_SAMPLING_FREQ(sample_frq),
		hdmi_base + S5P_HDMI_I2S_CH_ST_3);
	writeb(S5P_HDMI_I2S_ORG_SAMPLING_FREQ_44_1 |
		S5P_HDMI_I2S_WORD_LENGTH_MAX24_24BITS |
		S5P_HDMI_I2S_WORD_LENGTH_MAX_24BITS,
		hdmi_base + S5P_HDMI_I2S_CH_ST_4);

	writeb(S5P_HDMI_I2S_CH_STATUS_RELOAD,
		hdmi_base + S5P_HDMI_I2S_CH_ST_CON);
}
#endif

static u8 s5p_hdmi_checksum(int sum, int size, u8 *data)
{
	u32 i;

	for (i = 0; i < size; i++)
		sum += (u32)(data[i]);

	return (u8)(0x100 - (sum & 0xff));
}


static int s5p_hdmi_phy_control(bool on, u8 addr, u8 offset, u8 *read_buffer)
{
	u8 buff[2] = {0};

	buff[0] = addr;
	buff[1] = (on) ? (read_buffer[addr] & (~(1 << offset))) :
			(read_buffer[addr] | (1 << offset));
	read_buffer[addr] = buff[1];

	if (s5p_hdmi_i2c_phy_write(PHY_I2C_ADDRESS, 2, buff) != 0)
		return -EINVAL;

	return 0;
}

static int s5p_hdmi_phy_enable_oscpad(bool on, u8 *read_buffer)
{
	u8 buff[2];

#if defined(CONFIG_CPU_EXYNOS4212) || defined(CONFIG_CPU_EXYNOS4412)
	buff[0] = 0x0b;
	if (on)
		buff[1] = 0xd8;
	else
		buff[1] = 0x18;
	read_buffer[0x0b] = buff[1];
#else
	buff[0] = 0x19;
	if (on)
		buff[1] = (read_buffer[0x19] & (~(3<<6))) | (1<<6);
	else
		buff[1] = (read_buffer[0x19] & (~(3<<6))) | (2<<6);
	read_buffer[0x19] = buff[1];
#endif

	if (s5p_hdmi_i2c_phy_write(PHY_I2C_ADDRESS, 2, buff) != 0)
		return -EINVAL;

	return 0;
}

static bool s5p_hdmi_phy_is_enable(void)
{
	u32 reg;

#ifdef CONFIG_ARCH_EXYNOS4
	reg = readl(S5P_HDMI_PHY_CONTROL);
#endif

	return reg & (1 << 0);
}

static void s5p_hdmi_phy_enable(bool on)
{
	u32 reg;

#ifdef CONFIG_ARCH_EXYNOS4
	reg = readl(S5P_HDMI_PHY_CONTROL);

	if (on)
		reg |= (1 << 0);
	else
		reg &= ~(1 << 0);

	writeb(reg, S5P_HDMI_PHY_CONTROL);
#endif

}

void s5p_hdmi_reg_sw_reset(void)
{
	tvout_dbg("\n");
	s5p_hdmi_ctrl_clock(1);

	writeb(0x1, hdmi_base + S5P_HDMI_PHY_RSTOUT);
	mdelay(10);
	writeb(0x0, hdmi_base + S5P_HDMI_PHY_RSTOUT);

	s5p_hdmi_ctrl_clock(0);
}

#ifdef CONFIG_HDMI_TX_STRENGTH
int s5p_hdmi_phy_set_tx_strength(u8 ch, u8 *value)
{
	u8 buff[2];

	if (ch & TX_EMP_LVL) { /* REG10 BIT7:4 */
		buff[0] = HDMI_PHY_I2C_REG10;
		buff[1] = (value[TX_EMP_LVL_VAL] & 0x0f) << 4;
		if (s5p_hdmi_i2c_phy_write(PHY_I2C_ADDRESS, 2, buff) != 0)
			goto err_exit;
	}

	if (ch & TX_AMP_LVL) { /* REG10 BIT3:0, REG0F BIT7 */
		buff[0] = HDMI_PHY_I2C_REG10;
		buff[1] = (value[TX_AMP_LVL_VAL] & 0x0e) >> 1;
		if (s5p_hdmi_i2c_phy_write(PHY_I2C_ADDRESS, 2, buff) != 0)
			goto err_exit;
		buff[0] = HDMI_PHY_I2C_REG0F;
		buff[1] = (value[TX_AMP_LVL_VAL] & 0x01) << 7;
		if (s5p_hdmi_i2c_phy_write(PHY_I2C_ADDRESS, 2, buff) != 0)
			goto err_exit;
	}

	if (ch & TX_LVL_CH0) { /* REG04 BIT7:6 */
		buff[0] = HDMI_PHY_I2C_REG04;
		buff[1] = (value[TX_LVL_CH0_VAL] & 0x3) << 6;
		if (s5p_hdmi_i2c_phy_write(PHY_I2C_ADDRESS, 2, buff) != 0)
			goto err_exit;
	}

	if (ch & TX_LVL_CH1) { /* REG13 BIT1:0 */
		buff[0] = HDMI_PHY_I2C_REG13;
		buff[1] = (value[TX_LVL_CH1_VAL] & 0x3);
		if (s5p_hdmi_i2c_phy_write(PHY_I2C_ADDRESS, 2, buff) != 0)
			goto err_exit;
	}

	if (ch & TX_LVL_CH2) { /* REG17 BIT1:0 */
		buff[0] = HDMI_PHY_I2C_REG17;
		buff[1] = (value[TX_LVL_CH2_VAL] & 0x3);
		if (s5p_hdmi_i2c_phy_write(PHY_I2C_ADDRESS, 2, buff) != 0)
			goto err_exit;
	}

	return 0;

err_exit:
	pr_err("%s: failed to set tx strength\n", __func__);
	return -1;
}
#endif

int s5p_hdmi_phy_power(bool on)
{
	u32 size;
	u8 *buffer;
	u8 read_buffer[0x40] = {0, };

	size = sizeof(phy_config[0][0])
		/ sizeof(phy_config[0][0][0]);

	buffer = (u8 *) phy_config[0][0];

	tvout_dbg("(on:%d)\n", on);
	if (on) {
		if (!s5p_hdmi_phy_is_enable()) {
			s5p_hdmi_phy_enable(1);
			s5p_hdmi_reg_sw_reset();

			if (s5p_hdmi_i2c_phy_write(
				PHY_I2C_ADDRESS, 1, buffer) != 0)
				goto ret_on_err;

			if (s5p_hdmi_i2c_phy_read(
				PHY_I2C_ADDRESS, size, read_buffer) != 0) {
				tvout_err("s5p_hdmi_i2c_phy_read failed.\n");
				goto ret_on_err;
			}

#if defined(CONFIG_CPU_EXYNOS4212) || defined(CONFIG_CPU_EXYNOS4412)
			s5p_hdmi_phy_control(true, 0x1d, 0x7, read_buffer);
			s5p_hdmi_phy_control(true, 0x1d, 0x0, read_buffer);
			s5p_hdmi_phy_control(true, 0x1d, 0x1, read_buffer);
			s5p_hdmi_phy_control(true, 0x1d, 0x2, read_buffer);
			s5p_hdmi_phy_control(true, 0x1d, 0x4, read_buffer);
			s5p_hdmi_phy_control(true, 0x1d, 0x5, read_buffer);
			s5p_hdmi_phy_control(true, 0x1d, 0x6, read_buffer);
#else
			s5p_hdmi_phy_control(true, 0x1, 0x5, read_buffer);
			s5p_hdmi_phy_control(true, 0x1, 0x7, read_buffer);
			s5p_hdmi_phy_control(true, 0x5, 0x5, read_buffer);
			s5p_hdmi_phy_control(true, 0x17, 0x0, read_buffer);
			s5p_hdmi_phy_control(true, 0x17, 0x1, read_buffer);
#endif

			s5p_hdmi_print_phy_config();
		}
	} else {
		if (s5p_hdmi_phy_is_enable()) {
			if (s5p_hdmi_i2c_phy_write(
				PHY_I2C_ADDRESS, 1, buffer) != 0)
				goto ret_on_err;

			if (s5p_hdmi_i2c_phy_read(
				PHY_I2C_ADDRESS, size, read_buffer) != 0) {
				tvout_err("s5p_hdmi_i2c_phy_read failed.\n");
				goto ret_on_err;
			}
			/* Disable OSC pad */
			s5p_hdmi_phy_enable_oscpad(false, read_buffer);

#if defined(CONFIG_CPU_EXYNOS4212) || defined(CONFIG_CPU_EXYNOS4412)
			s5p_hdmi_phy_control(false, 0x1d, 0x7, read_buffer);
			s5p_hdmi_phy_control(false, 0x1d, 0x0, read_buffer);
			s5p_hdmi_phy_control(false, 0x1d, 0x1, read_buffer);
			s5p_hdmi_phy_control(false, 0x1d, 0x2, read_buffer);
			s5p_hdmi_phy_control(false, 0x1d, 0x4, read_buffer);
			s5p_hdmi_phy_control(false, 0x1d, 0x5, read_buffer);
			s5p_hdmi_phy_control(false, 0x1d, 0x6, read_buffer);
			s5p_hdmi_phy_control(false, 0x4, 0x3, read_buffer);
#else
			s5p_hdmi_phy_control(false, 0x1, 0x5, read_buffer);
			s5p_hdmi_phy_control(false, 0x1, 0x7, read_buffer);
			s5p_hdmi_phy_control(false, 0x5, 0x5, read_buffer);
			s5p_hdmi_phy_control(false, 0x17, 0x0, read_buffer);
			s5p_hdmi_phy_control(false, 0x17, 0x1, read_buffer);
#endif

			s5p_hdmi_print_phy_config();

			s5p_hdmi_phy_enable(0);
		}
	}

	return 0;

ret_on_err:
	return -1;
}

s32 s5p_hdmi_phy_config(
		enum phy_freq freq, enum s5p_hdmi_color_depth cd)
{
	s32 index;
	s32 size;
	u8 buffer[32] = {0, };
	u8 reg;

	switch (cd) {
	case HDMI_CD_24:
		index = 0;
		break;

	case HDMI_CD_30:
		index = 1;
		break;

	case HDMI_CD_36:
		index = 2;
		break;

	default:
		return -1;
	}

	buffer[0] = PHY_REG_MODE_SET_DONE;
	buffer[1] = 0x00;

	if (s5p_hdmi_i2c_phy_write(PHY_I2C_ADDRESS, 2, buffer) != 0) {
		tvout_err("s5p_hdmi_i2c_phy_write failed.\n");
		return -1;
	}

	writeb(0x5, i2c_hdmi_phy_base + HDMI_I2C_LC);

	size = sizeof(phy_config[freq][index])
		/ sizeof(phy_config[freq][index][0]);

	memcpy(buffer, phy_config[freq][index], sizeof(buffer));

	if (s5p_hdmi_i2c_phy_write(PHY_I2C_ADDRESS, size, buffer) != 0)
		return -1;

#ifdef CONFIG_HDMI_PHY_32N
	buffer[0] = PHY_REG_MODE_SET_DONE;
	buffer[1] = 0x80;

	if (s5p_hdmi_i2c_phy_write(PHY_I2C_ADDRESS, 2, buffer) != 0) {
		tvout_err("s5p_hdmi_i2c_phy_write failed.\n");
		return -1;
	}
#else
	buffer[0] = 0x01;

	if (s5p_hdmi_i2c_phy_write(PHY_I2C_ADDRESS, 1, buffer) != 0) {
		tvout_err("s5p_hdmi_i2c_phy_write failed.\n");
		return -1;
	}
#endif

	s5p_hdmi_print_phy_config();

#ifndef CONFIG_HDMI_PHY_32N
	s5p_hdmi_reg_core_reset();
#endif

#ifdef CONFIG_HDMI_PHY_32N
	do {
		reg = readb(hdmi_base + S5P_HDMI_PHY_STATUS0);
	} while (!(reg & S5P_HDMI_PHY_STATUS_READY));
#else
	do {
		reg = readb(hdmi_base + S5P_HDMI_PHY_STATUS);
	} while (!(reg & S5P_HDMI_PHY_STATUS_READY));
#endif

	writeb(I2C_CLK_PEND_INT, i2c_hdmi_phy_base + HDMI_I2C_CON);
	writeb(I2C_IDLE, i2c_hdmi_phy_base + HDMI_I2C_STAT);

	return 0;
}

void s5p_hdmi_set_gcp(enum s5p_hdmi_color_depth	depth, u8 *gcp)
{
	switch (depth) {
	case HDMI_CD_48:
		gcp[1] = S5P_HDMI_GCP_48BPP; break;
	case HDMI_CD_36:
		gcp[1] = S5P_HDMI_GCP_36BPP; break;
	case HDMI_CD_30:
		gcp[1] = S5P_HDMI_GCP_30BPP; break;
	case HDMI_CD_24:
		gcp[1] = S5P_HDMI_GCP_24BPP; break;

	default:
		break;
	}
}

void s5p_hdmi_reg_acr(u8 *acr)
{
	u32 n	= acr[4] << 16 | acr[5] << 8 | acr[6];
	u32 cts	= acr[1] << 16 | acr[2] << 8 | acr[3];

	hdmi_write_24(n, hdmi_base + S5P_HDMI_ACR_N0);
	hdmi_write_24(cts, hdmi_base + S5P_HDMI_ACR_MCTS0);
	hdmi_write_24(cts, hdmi_base + S5P_HDMI_ACR_CTS0);

	writeb(4, hdmi_base + S5P_HDMI_ACR_CON);
}

void s5p_hdmi_reg_asp(u8 *asp, struct s5p_hdmi_audio *audio)
{
	if (audio->channel == 2)
		writeb(S5P_HDMI_AUD_NO_DST_DOUBLE | S5P_HDMI_AUD_TYPE_SAMPLE |
			S5P_HDMI_AUD_MODE_TWO_CH | S5P_HDMI_AUD_SP_ALL_DIS,
			hdmi_base + S5P_HDMI_ASP_CON);
	else
		writeb(S5P_HDMI_AUD_MODE_MULTI_CH | S5P_HDMI_AUD_SP_AUD2_EN |
			S5P_HDMI_AUD_SP_AUD1_EN | S5P_HDMI_AUD_SP_AUD0_EN,
			hdmi_base + S5P_HDMI_ASP_CON);

	writeb(S5P_HDMI_ASP_SP_FLAT_AUD_SAMPLE,
		hdmi_base + S5P_HDMI_ASP_SP_FLAT);

	if (audio->channel == 2) {
		writeb(S5P_HDMI_SPK0R_SEL_I_PCM0R | S5P_HDMI_SPK0L_SEL_I_PCM0L,
				hdmi_base + S5P_HDMI_ASP_CHCFG0);
		writeb(S5P_HDMI_SPK0R_SEL_I_PCM0R | S5P_HDMI_SPK0L_SEL_I_PCM0L,
				hdmi_base + S5P_HDMI_ASP_CHCFG1);
		writeb(S5P_HDMI_SPK0R_SEL_I_PCM0R | S5P_HDMI_SPK0L_SEL_I_PCM0L,
				hdmi_base + S5P_HDMI_ASP_CHCFG2);
		writeb(S5P_HDMI_SPK0R_SEL_I_PCM0R | S5P_HDMI_SPK0L_SEL_I_PCM0L,
				hdmi_base + S5P_HDMI_ASP_CHCFG3);
	} else {
		writeb(S5P_HDMI_SPK0R_SEL_I_PCM0R | S5P_HDMI_SPK0L_SEL_I_PCM0L,
				hdmi_base + S5P_HDMI_ASP_CHCFG0);
		writeb(S5P_HDMI_SPK0R_SEL_I_PCM1L | S5P_HDMI_SPK0L_SEL_I_PCM1R,
				hdmi_base + S5P_HDMI_ASP_CHCFG1);
		writeb(S5P_HDMI_SPK0R_SEL_I_PCM2R | S5P_HDMI_SPK0L_SEL_I_PCM2L,
				hdmi_base + S5P_HDMI_ASP_CHCFG2);
		writeb(S5P_HDMI_SPK0R_SEL_I_PCM3R | S5P_HDMI_SPK0L_SEL_I_PCM3L,
				hdmi_base + S5P_HDMI_ASP_CHCFG3);
	}
}

void s5p_hdmi_reg_gcp(u8 i_p, u8 *gcp)
{
	u32 gcp_con;

	writeb(gcp[2], hdmi_base + S5P_HDMI_GCP_BYTE2);

	gcp_con = readb(hdmi_base + S5P_HDMI_GCP_CON);

	if (i_p)
		gcp_con |= S5P_HDMI_GCP_CON_EN_1ST_VSYNC |
				S5P_HDMI_GCP_CON_EN_2ST_VSYNC;
	else
		gcp_con &= (~(S5P_HDMI_GCP_CON_EN_1ST_VSYNC |
				S5P_HDMI_GCP_CON_EN_2ST_VSYNC));

	writeb(gcp_con, hdmi_base + S5P_HDMI_GCP_CON);

}

void s5p_hdmi_reg_acp(u8 *header, u8 *acp)
{
	writeb(header[1], hdmi_base + S5P_HDMI_ACP_TYPE);
}

void s5p_hdmi_reg_isrc(u8 *isrc1, u8 *isrc2)
{
}

void s5p_hdmi_reg_gmp(u8 *gmp)
{
}

#ifdef CONFIG_HDMI_14A_3D

#define VENDOR_HEADER00			0x81
#define VENDOR_HEADER01			0x01
#define VENDOR_HEADER02			0x05
#define VENDOR_INFOFRAME_HEADER		(0x1 + 0x01 + 0x06)
#define VENDOR_PACKET_BYTE_LENGTH 0x06
#define TRANSMIT_EVERY_VSYNC		(1<<1)

void s5p_hdmi_reg_infoframe(struct s5p_hdmi_infoframe *info,
	u8 *data, u8 type_3D)
{
	u32 start_addr = 0, sum_addr = 0;
	u8 sum;
	u32 uSpdCon;
	u8 ucChecksum, i;

	switch (info->type) {
	case HDMI_VSI_INFO:
		writeb((u8)VENDOR_HEADER00, hdmi_base + S5P_HDMI_VSI_HEADER0);
		writeb((u8)VENDOR_HEADER01, hdmi_base + S5P_HDMI_VSI_HEADER1);

		if (type_3D == HDMI_3D_FP_FORMAT) {
			writeb((u8)VENDOR_HEADER02,
				hdmi_base + S5P_HDMI_VSI_HEADER2);
			ucChecksum = VENDOR_HEADER00 +
				VENDOR_HEADER01 + VENDOR_HEADER02;

			for (i = 0; i < VENDOR_PACKET_BYTE_LENGTH; i++)
				ucChecksum += readb(hdmi_base +
					S5P_HDMI_VSI_DATA01+4*i);

			writeb((u8)0x2a, hdmi_base + S5P_HDMI_VSI_DATA00);
			writeb((u8)0x03, hdmi_base + S5P_HDMI_VSI_DATA01);
			writeb((u8)0x0c, hdmi_base + S5P_HDMI_VSI_DATA02);
			writeb((u8)0x00, hdmi_base + S5P_HDMI_VSI_DATA03);
			writeb((u8)0x40, hdmi_base + S5P_HDMI_VSI_DATA04);
			writeb((u8)0x00, hdmi_base + S5P_HDMI_VSI_DATA05);

		} else if (type_3D == HDMI_3D_TB_FORMAT) {
			writeb((u8)VENDOR_HEADER02, hdmi_base +
				S5P_HDMI_VSI_HEADER2);
			ucChecksum = VENDOR_HEADER00 +
				VENDOR_HEADER01 + VENDOR_HEADER02;

			for (i = 0; i < VENDOR_PACKET_BYTE_LENGTH; i++)
				ucChecksum += readb(hdmi_base +
					S5P_HDMI_VSI_DATA01+4*i);

			writeb((u8)0xca, hdmi_base + S5P_HDMI_VSI_DATA00);
			writeb((u8)0x03, hdmi_base + S5P_HDMI_VSI_DATA01);
			writeb((u8)0x0c, hdmi_base + S5P_HDMI_VSI_DATA02);
			writeb((u8)0x00, hdmi_base + S5P_HDMI_VSI_DATA03);
			writeb((u8)0x40, hdmi_base + S5P_HDMI_VSI_DATA04);
			writeb((u8)0x60, hdmi_base + S5P_HDMI_VSI_DATA05);

		} else if (type_3D == HDMI_3D_SSH_FORMAT) {
			writeb((u8)0x06, hdmi_base + S5P_HDMI_VSI_HEADER2);
			ucChecksum = VENDOR_HEADER00 + VENDOR_HEADER01 + 0x06;

			for (i = 0; i < 7; i++)
				ucChecksum += readb(hdmi_base +
					S5P_HDMI_VSI_DATA01+4*i);

			writeb((u8)0x99, hdmi_base + S5P_HDMI_VSI_DATA00);
			writeb((u8)0x03, hdmi_base + S5P_HDMI_VSI_DATA01);
			writeb((u8)0x0c, hdmi_base + S5P_HDMI_VSI_DATA02);
			writeb((u8)0x00, hdmi_base + S5P_HDMI_VSI_DATA03);
			writeb((u8)0x40, hdmi_base + S5P_HDMI_VSI_DATA04);
			writeb((u8)0x80, hdmi_base + S5P_HDMI_VSI_DATA05);
			writeb((u8)0x10, hdmi_base + S5P_HDMI_VSI_DATA06);

		} else {
			writeb((u8)0x0, hdmi_base + S5P_HDMI_VSI_HEADER2);
			ucChecksum = VENDOR_HEADER00 + VENDOR_HEADER01 + 0x06;

			for (i = 0; i < 7; i++)
				ucChecksum += readb(hdmi_base +
					S5P_HDMI_VSI_DATA01+4*i);

			writeb((u8)0x0, hdmi_base + S5P_HDMI_VSI_DATA00);
			writeb((u8)0x0, hdmi_base + S5P_HDMI_VSI_DATA01);
			writeb((u8)0x0, hdmi_base + S5P_HDMI_VSI_DATA02);
			writeb((u8)0x0, hdmi_base + S5P_HDMI_VSI_DATA03);
			writeb((u8)0x0, hdmi_base + S5P_HDMI_VSI_DATA04);
			writeb((u8)0x0, hdmi_base + S5P_HDMI_VSI_DATA05);
			writeb((u8)0x0, hdmi_base + S5P_HDMI_VSI_DATA06);
			tvout_dbg("2D format is supported.\n");
			return ;
		}

		uSpdCon = readb(hdmi_base + S5P_HDMI_VSI_CON);
		uSpdCon = (uSpdCon&(~(3<<0)))|(TRANSMIT_EVERY_VSYNC);
		writeb((u8)uSpdCon, hdmi_base + S5P_HDMI_VSI_CON);
		break;
	case HDMI_AVI_INFO:
		writeb((u8)0x82, hdmi_base + S5P_HDMI_AVI_HEADER0);
		writeb((u8)0x02, hdmi_base + S5P_HDMI_AVI_HEADER1);
		writeb((u8)0x0d, hdmi_base + S5P_HDMI_AVI_HEADER2);

		sum_addr	= S5P_HDMI_AVI_CHECK_SUM;
		start_addr	= S5P_HDMI_AVI_BYTE1;
		break;
	case HDMI_SPD_INFO:
		sum_addr	= S5P_HDMI_SPD_DATA00;
		start_addr	= S5P_HDMI_SPD_DATA01 + 4;
		/* write header */
		writeb((u8)info->type, hdmi_base + S5P_HDMI_SPD_HEADER0);
		writeb((u8)info->version, hdmi_base + S5P_HDMI_SPD_HEADER1);
		writeb((u8)info->length, hdmi_base + S5P_HDMI_SPD_HEADER2);
		break;
	case HDMI_AUI_INFO:
		writeb((u8)0x84, hdmi_base + S5P_HDMI_AUI_HEADER0);
		writeb((u8)0x01, hdmi_base + S5P_HDMI_AUI_HEADER1);
		writeb((u8)0x0a, hdmi_base + S5P_HDMI_AUI_HEADER2);
		sum_addr	= S5P_HDMI_AUI_CHECK_SUM;
		start_addr	= S5P_HDMI_AUI_BYTE1;
		break;
	case HDMI_MPG_INFO:
		sum_addr	= S5P_HDMI_MPG_CHECK_SUM;
		start_addr	= S5P_HDMI_MPG_BYTE1;
		break;
	default:
		tvout_dbg("undefined infoframe\n");
		return;
	}

	/* calculate checksum */
	sum = (u8)info->type + info->version + info->length;
	sum = s5p_hdmi_checksum(sum, info->length, data);

	/* write checksum */
	writeb(sum, hdmi_base + sum_addr);
	/* write data */
	hdmi_write_l(data, hdmi_base, start_addr, info->length);
}

void s5p_hdmi_reg_tg(struct s5p_hdmi_v_format *v)
{
	u8 tg;
	struct s5p_hdmi_v_frame	*frame = &(v->frame);

	hdmi_write_16(v->tg_H_FSZ, hdmi_base + S5P_HDMI_TG_H_FSZ_L);
	hdmi_write_16(v->tg_HACT_ST, hdmi_base + S5P_HDMI_TG_HACT_ST_L);
	hdmi_write_16(v->tg_HACT_SZ, hdmi_base + S5P_HDMI_TG_HACT_SZ_L);

	hdmi_write_16(v->tg_V_FSZ, hdmi_base + S5P_HDMI_TG_V_FSZ_L);
	hdmi_write_16(v->tg_VACT_SZ, hdmi_base + S5P_HDMI_TG_VACT_SZ_L);
	hdmi_write_16(v->tg_VACT_ST, hdmi_base + S5P_HDMI_TG_VACT_ST_L);
	hdmi_write_16(v->tg_VACT_ST2, hdmi_base + S5P_HDMI_TG_VACT_ST2_L);
	hdmi_write_16(v->tg_VACT_ST3, hdmi_base + S5P_HDMI_TG_VACT_ST3_L);
	hdmi_write_16(v->tg_VACT_ST4, hdmi_base + S5P_HDMI_TG_VACT_ST4_L);

	hdmi_write_16(v->tg_VSYNC_BOT_HDMI, hdmi_base +
		S5P_HDMI_TG_VSYNC_BOT_HDMI_L);
	hdmi_write_16(v->tg_VSYNC_TOP_HDMI, hdmi_base +
		S5P_HDMI_TG_VSYNC_TOP_HDMI_L);
	hdmi_write_16(v->tg_FIELD_TOP_HDMI, hdmi_base +
		S5P_HDMI_TG_FIELD_TOP_HDMI_L);
	hdmi_write_16(v->tg_FIELD_BOT_HDMI, hdmi_base +
		S5P_HDMI_TG_FIELD_BOT_HDMI_L);

	/* write reg default value */
	hdmi_write_16(v->tg_VSYNC, hdmi_base + S5P_HDMI_TG_VSYNC_L);
	hdmi_write_16(v->tg_VSYNC2, hdmi_base + S5P_HDMI_TG_VSYNC2_L);
	hdmi_write_16(v->tg_FIELD_CHG, hdmi_base + S5P_HDMI_TG_FIELD_CHG_L);

	tg = readb(hdmi_base + S5P_HDMI_TG_CMD);

	hdmi_bit_set(frame->interlaced, tg, S5P_HDMI_FIELD);

	writeb(tg, hdmi_base + S5P_HDMI_TG_CMD);
}

void s5p_hdmi_reg_v_timing(struct s5p_hdmi_v_format *v)
{
	u32 uTemp32;

	struct s5p_hdmi_v_frame	*frame = &(v->frame);

	uTemp32 = frame->vH_Line;
	writeb((u8)(uTemp32&0xff), hdmi_base + S5P_HDMI_H_LINE_0);
	writeb((u8)(uTemp32 >> 8), hdmi_base + S5P_HDMI_H_LINE_1);

	uTemp32 = frame->vV_Line;
	writeb((u8)(uTemp32&0xff), hdmi_base + S5P_HDMI_V_LINE_0);
	writeb((u8)(uTemp32 >> 8), hdmi_base + S5P_HDMI_V_LINE_1);

	uTemp32 = frame->vH_SYNC_START;
	writeb((u8)(uTemp32&0xff), hdmi_base + S5P_HDMI_H_SYNC_START_0);
	writeb((u8)(uTemp32 >> 8), hdmi_base + S5P_HDMI_H_SYNC_START_1);

	uTemp32 = frame->vH_SYNC_END;
	writeb((u8)(uTemp32&0xff), hdmi_base + S5P_HDMI_H_SYNC_END_0);
	writeb((u8)(uTemp32 >> 8), hdmi_base + S5P_HDMI_H_SYNC_END_1);

	uTemp32 = frame->vV1_Blank;
	writeb((u8)(uTemp32&0xff), hdmi_base + S5P_HDMI_V1_BLANK_0);
	writeb((u8)(uTemp32 >> 8), hdmi_base + S5P_HDMI_V1_BLANK_1);

	uTemp32 = frame->vV2_Blank;
	writeb((u8)(uTemp32&0xff), hdmi_base + S5P_HDMI_V2_BLANK_0);
	writeb((u8)(uTemp32 >> 8), hdmi_base + S5P_HDMI_V2_BLANK_1);

	uTemp32 = frame->vHBlank;
	writeb((u8)(uTemp32&0xff), hdmi_base + S5P_HDMI_H_BLANK_0);
	writeb((u8)(uTemp32 >> 8), hdmi_base + S5P_HDMI_H_BLANK_1);

	uTemp32 = frame->VBLANK_F0;
	writeb((u8)(uTemp32&0xff), hdmi_base + S5P_HDMI_V_BLANK_F0_0);
	writeb((u8)(uTemp32 >> 8), hdmi_base + S5P_HDMI_V_BLANK_F0_1);

	uTemp32 = frame->VBLANK_F1;
	writeb((u8)(uTemp32&0xff), hdmi_base + S5P_HDMI_V_BLANK_F1_0);
	writeb((u8)(uTemp32 >> 8), hdmi_base + S5P_HDMI_V_BLANK_F1_1);

	uTemp32 = 0xffff;
	writeb((u8)(uTemp32&0xff), hdmi_base + S5P_HDMI_V_BLANK_F2_0);
	writeb((u8)(uTemp32 >> 8), hdmi_base + S5P_HDMI_V_BLANK_F2_1);

	uTemp32 = 0xffff;
	writeb((u8)(uTemp32&0xff), hdmi_base + S5P_HDMI_V_BLANK_F3_0);
	writeb((u8)(uTemp32 >> 8), hdmi_base + S5P_HDMI_V_BLANK_F3_1);

	uTemp32 = 0xffff;
	writeb((u8)(uTemp32&0xff), hdmi_base + S5P_HDMI_V_BLANK_F4_0);
	writeb((u8)(uTemp32 >> 8), hdmi_base + S5P_HDMI_V_BLANK_F4_1);

	uTemp32 = 0xffff;
	writeb((u8)(uTemp32&0xff), hdmi_base + S5P_HDMI_V_BLANK_F5_0);
	writeb((u8)(uTemp32 >> 8), hdmi_base + S5P_HDMI_V_BLANK_F5_1);

	uTemp32 = frame->vVSYNC_LINE_BEF_1;
	writeb((u8)(uTemp32&0xff), hdmi_base + S5P_HDMI_V_SYNC_LINE_BEF_1_0);
	writeb((u8)(uTemp32 >> 8), hdmi_base + S5P_HDMI_V_SYNC_LINE_BEF_1_1);

	uTemp32 = frame->vVSYNC_LINE_BEF_2;
	writeb((u8)(uTemp32&0xff), hdmi_base + S5P_HDMI_V_SYNC_LINE_BEF_2_0);
	writeb((u8)(uTemp32 >> 8), hdmi_base + S5P_HDMI_V_SYNC_LINE_BEF_2_1);

	uTemp32 = frame->vVSYNC_LINE_AFT_1;
	writeb((u8)(uTemp32&0xff), hdmi_base + S5P_HDMI_V_SYNC_LINE_AFT_1_0);
	writeb((u8)(uTemp32 >> 8), hdmi_base + S5P_HDMI_V_SYNC_LINE_AFT_1_1);

	uTemp32 = frame->vVSYNC_LINE_AFT_2;
	writeb((u8)(uTemp32&0xff), hdmi_base + S5P_HDMI_V_SYNC_LINE_AFT_2_0);
	writeb((u8)(uTemp32 >> 8), hdmi_base + S5P_HDMI_V_SYNC_LINE_AFT_2_1);

	uTemp32 = 0xffff;
	writeb((u8)(uTemp32&0xff), hdmi_base + S5P_HDMI_V_SYNC_LINE_AFT_3_0);
	writeb((u8)(uTemp32 >> 8), hdmi_base + S5P_HDMI_V_SYNC_LINE_AFT_3_1);

	uTemp32 = 0xffff;
	writeb((u8)(uTemp32&0xff), hdmi_base + S5P_HDMI_V_SYNC_LINE_AFT_4_0);
	writeb((u8)(uTemp32 >> 8), hdmi_base + S5P_HDMI_V_SYNC_LINE_AFT_4_1);

	uTemp32 = 0xffff;
	writeb((u8)(uTemp32&0xff), hdmi_base + S5P_HDMI_V_SYNC_LINE_AFT_5_0);
	writeb((u8)(uTemp32 >> 8), hdmi_base + S5P_HDMI_V_SYNC_LINE_AFT_5_1);

	uTemp32 = 0xffff;
	writeb((u8)(uTemp32&0xff), hdmi_base + S5P_HDMI_V_SYNC_LINE_AFT_6_0);
	writeb((u8)(uTemp32 >> 8), hdmi_base + S5P_HDMI_V_SYNC_LINE_AFT_6_1);

	uTemp32 = frame->vVSYNC_LINE_AFT_PXL_1;
	writeb((u8)(uTemp32&0xff), hdmi_base +
		S5P_HDMI_V_SYNC_LINE_AFT_PXL_1_0);
	writeb((u8)(uTemp32 >> 8), hdmi_base +
		S5P_HDMI_V_SYNC_LINE_AFT_PXL_1_1);

	uTemp32 = frame->vVSYNC_LINE_AFT_PXL_2;
	writeb((u8)(uTemp32&0xff), hdmi_base +
		S5P_HDMI_V_SYNC_LINE_AFT_PXL_2_0);
	writeb((u8)(uTemp32 >> 8), hdmi_base +
		S5P_HDMI_V_SYNC_LINE_AFT_PXL_2_1);

	uTemp32 = 0xffff;
	writeb((u8)(uTemp32&0xff), hdmi_base +
		S5P_HDMI_V_SYNC_LINE_AFT_PXL_3_0);
	writeb((u8)(uTemp32 >> 8), hdmi_base +
		S5P_HDMI_V_SYNC_LINE_AFT_PXL_3_1);

	uTemp32 = 0xffff;
	writeb((u8)(uTemp32&0xff), hdmi_base +
		S5P_HDMI_V_SYNC_LINE_AFT_PXL_4_0);
	writeb((u8)(uTemp32 >> 8), hdmi_base +
		S5P_HDMI_V_SYNC_LINE_AFT_PXL_4_1);

	uTemp32 = 0xffff;
	writeb((u8)(uTemp32&0xff), hdmi_base +
		S5P_HDMI_V_SYNC_LINE_AFT_PXL_5_0);
	writeb((u8)(uTemp32 >> 8), hdmi_base +
		S5P_HDMI_V_SYNC_LINE_AFT_PXL_5_1);

	uTemp32 = 0xffff;
	writeb((u8)(uTemp32&0xff), hdmi_base +
		S5P_HDMI_V_SYNC_LINE_AFT_PXL_6_0);
	writeb((u8)(uTemp32 >> 8), hdmi_base +
		S5P_HDMI_V_SYNC_LINE_AFT_PXL_6_1);

	uTemp32 = frame->vVACT_SPACE_1;
	writeb((u8)(uTemp32&0xff), hdmi_base + S5P_HDMI_VACT_SPACE_1_0);
	writeb((u8)(uTemp32 >> 8), hdmi_base + S5P_HDMI_VACT_SPACE_1_1);

	uTemp32 = frame->vVACT_SPACE_2;
	writeb((u8)(uTemp32&0xff), hdmi_base + S5P_HDMI_VACT_SPACE_2_0);
	writeb((u8)(uTemp32 >> 8), hdmi_base + S5P_HDMI_VACT_SPACE_2_1);

	uTemp32 = 0xffff;
	writeb((u8)(uTemp32&0xff), hdmi_base + S5P_HDMI_VACT_SPACE_3_0);
	writeb((u8)(uTemp32 >> 8), hdmi_base + S5P_HDMI_VACT_SPACE_3_1);

	uTemp32 = 0xffff;
	writeb((u8)(uTemp32&0xff), hdmi_base + S5P_HDMI_VACT_SPACE_4_0);
	writeb((u8)(uTemp32 >> 8), hdmi_base + S5P_HDMI_VACT_SPACE_4_1);

	uTemp32 = 0xffff;
	writeb((u8)(uTemp32&0xff), hdmi_base + S5P_HDMI_VACT_SPACE_5_0);
	writeb((u8)(uTemp32 >> 8), hdmi_base + S5P_HDMI_VACT_SPACE_5_1);

	uTemp32 = 0xffff;
	writeb((u8)(uTemp32&0xff), hdmi_base + S5P_HDMI_VACT_SPACE_6_0);
	writeb((u8)(uTemp32 >> 8), hdmi_base + S5P_HDMI_VACT_SPACE_6_1);

	writeb(frame->Hsync_polarity, hdmi_base + S5P_HDMI_HSYNC_POL);

	writeb(frame->Vsync_polarity, hdmi_base + S5P_HDMI_VSYNC_POL);

	writeb(frame->interlaced, hdmi_base + S5P_HDMI_INT_PRO_MODE);
}

void s5p_hdmi_reg_bluescreen_clr(u16 b, u16 g, u16 r)
{
	writeb((u8)(b>>8), hdmi_base + S5P_HDMI_BLUE_SCREEN_B_0);
	writeb((u8)(b&0xff), hdmi_base + S5P_HDMI_BLUE_SCREEN_B_0);
	writeb((u8)(g>>8), hdmi_base + S5P_HDMI_BLUE_SCREEN_G_0);
	writeb((u8)(g&0xff), hdmi_base + S5P_HDMI_BLUE_SCREEN_G_1);
	writeb((u8)(r>>8), hdmi_base + S5P_HDMI_BLUE_SCREEN_R_0);
	writeb((u8)(r&0xff), hdmi_base + S5P_HDMI_BLUE_SCREEN_R_1);
}

#else
void s5p_hdmi_reg_infoframe(struct s5p_hdmi_infoframe *info, u8 *data)
{
	u32 start_addr = 0, sum_addr = 0;
	u8 sum;

	switch (info->type) {
	case HDMI_VSI_INFO:
		break;
	case HDMI_AVI_INFO:
		sum_addr	= S5P_HDMI_AVI_CHECK_SUM;
		start_addr	= S5P_HDMI_AVI_DATA;
		break;
	case HDMI_SPD_INFO:
		sum_addr	= S5P_HDMI_SPD_DATA;
		start_addr	= S5P_HDMI_SPD_DATA + 4;
		/* write header */
		writeb((u8)info->type, hdmi_base + S5P_HDMI_SPD_HEADER);
		writeb((u8)info->version, hdmi_base + S5P_HDMI_SPD_HEADER + 4);
		writeb((u8)info->length, hdmi_base + S5P_HDMI_SPD_HEADER + 8);
		break;
	case HDMI_AUI_INFO:
		sum_addr	= S5P_HDMI_AUI_CHECK_SUM;
		start_addr	= S5P_HDMI_AUI_BYTE1;
		break;
	case HDMI_MPG_INFO:
		sum_addr	= S5P_HDMI_MPG_CHECK_SUM;
		start_addr	= S5P_HDMI_MPG_DATA;
		break;
	default:
		tvout_dbg("undefined infoframe\n");
		return;
	}

	/* calculate checksum */
	sum = (u8)info->type + info->version + info->length;
	sum = s5p_hdmi_checksum(sum, info->length, data);

	/* write checksum */
	writeb(sum, hdmi_base + sum_addr);

	/* write data */
	hdmi_write_l(data, hdmi_base, start_addr, info->length);
}

void s5p_hdmi_reg_tg(struct s5p_hdmi_v_frame *frame)
{
	u16 reg;
	u8 tg;

	hdmi_write_16(frame->h_total, hdmi_base + S5P_HDMI_TG_H_FSZ_L);
	hdmi_write_16((frame->h_blank)-1, hdmi_base + S5P_HDMI_TG_HACT_ST_L);
	hdmi_write_16((frame->h_active)+1, hdmi_base + S5P_HDMI_TG_HACT_SZ_L);

	hdmi_write_16(frame->v_total, hdmi_base + S5P_HDMI_TG_V_FSZ_L);
	hdmi_write_16(frame->v_active, hdmi_base + S5P_HDMI_TG_VACT_SZ_L);


	reg = (frame->i_p) ? (frame->v_total - frame->v_active*2) / 2 :
				frame->v_total - frame->v_active;
	hdmi_write_16(reg, hdmi_base + S5P_HDMI_TG_VACT_ST_L);

	reg = (frame->i_p) ? 0x249 : 0x248;
	hdmi_write_16(reg, hdmi_base + S5P_HDMI_TG_VACT_ST2_L);

	reg = (frame->i_p) ? 0x233 : 1;
	hdmi_write_16(reg, hdmi_base + S5P_HDMI_TG_VSYNC_BOT_HDMI_L);

	/* write reg default value */
	hdmi_write_16(0x1, hdmi_base + S5P_HDMI_TG_VSYNC_L);
	hdmi_write_16(0x233, hdmi_base + S5P_HDMI_TG_VSYNC2_L);
	hdmi_write_16(0x233, hdmi_base + S5P_HDMI_TG_FIELD_CHG_L);
	hdmi_write_16(0x1, hdmi_base + S5P_HDMI_TG_VSYNC_TOP_HDMI_L);
	hdmi_write_16(0x1, hdmi_base + S5P_HDMI_TG_FIELD_TOP_HDMI_L);
	hdmi_write_16(0x233, hdmi_base + S5P_HDMI_TG_FIELD_BOT_HDMI_L);

	tg = readb(hdmi_base + S5P_HDMI_TG_CMD);

	hdmi_bit_set(frame->i_p, tg, S5P_HDMI_FIELD);

	writeb(tg, hdmi_base + S5P_HDMI_TG_CMD);
}

void s5p_hdmi_reg_v_timing(struct s5p_hdmi_v_format *v)
{
	u32 reg32;

	struct s5p_hdmi_v_frame	*frame = &(v->frame);

	writeb(frame->polarity, hdmi_base + S5P_HDMI_SYNC_MODE);
	writeb(frame->i_p, hdmi_base + S5P_HDMI_INT_PRO_MODE);

	hdmi_write_16(frame->h_blank, hdmi_base + S5P_HDMI_H_BLANK_0);

	reg32 = (frame->v_blank << 11) | (frame->v_blank + frame->v_active);
	hdmi_write_24(reg32, hdmi_base + S5P_HDMI_V_BLANK_0);

	reg32 = (frame->h_total << 12) | frame->v_total;
	hdmi_write_24(reg32, hdmi_base + S5P_HDMI_H_V_LINE_0);

	reg32 = frame->polarity << 20 | v->h_sync.end << 10 | v->h_sync.begin;
	hdmi_write_24(reg32, hdmi_base + S5P_HDMI_H_SYNC_GEN_0);

	reg32 = v->v_sync_top.begin << 12 | v->v_sync_top.end;
	hdmi_write_24(reg32, hdmi_base + S5P_HDMI_V_SYNC_GEN_1_0);

	if (frame->i_p) {
		reg32 = v->v_blank_f.end << 11 | v->v_blank_f.begin;
		hdmi_write_24(reg32, hdmi_base + S5P_HDMI_V_BLANK_F_0);

		reg32 = v->v_sync_bottom.begin << 12 | v->v_sync_bottom.end;
		hdmi_write_24(reg32, hdmi_base + S5P_HDMI_V_SYNC_GEN_2_0);

		reg32 = v->v_sync_h_pos.begin << 12 | v->v_sync_h_pos.end;
		hdmi_write_24(reg32, hdmi_base + S5P_HDMI_V_SYNC_GEN_3_0);
	} else {
		hdmi_write_24(0x0, hdmi_base + S5P_HDMI_V_BLANK_F_0);
		hdmi_write_24(0x1001, hdmi_base + S5P_HDMI_V_SYNC_GEN_2_0);
		hdmi_write_24(0x1001, hdmi_base + S5P_HDMI_V_SYNC_GEN_3_0);
	}
}

void s5p_hdmi_reg_bluescreen_clr(u8 cb_b, u8 y_g, u8 cr_r)
{
	writeb(cb_b, hdmi_base + S5P_HDMI_BLUE_SCREEN_0);
	writeb(y_g, hdmi_base + S5P_HDMI_BLUE_SCREEN_1);
	writeb(cr_r, hdmi_base + S5P_HDMI_BLUE_SCREEN_2);
}
#endif

void s5p_hdmi_reg_bluescreen(bool en)
{
	u8 reg = readl(hdmi_base + S5P_HDMI_CON_0);

	hdmi_bit_set(en, reg, S5P_HDMI_BLUE_SCR_EN);

	writeb(reg, hdmi_base + S5P_HDMI_CON_0);
}

void s5p_hdmi_reg_clr_range(u8 y_min, u8 y_max, u8 c_min, u8 c_max)
{
	writeb(y_max, hdmi_base + S5P_HDMI_YMAX);
	writeb(y_min, hdmi_base + S5P_HDMI_YMIN);
	writeb(c_max, hdmi_base + S5P_HDMI_CMAX);
	writeb(c_min, hdmi_base + S5P_HDMI_CMIN);
}

void s5p_hdmi_reg_tg_cmd(bool time, bool bt656, bool tg)
{
	u8 reg = 0;

	reg = readb(hdmi_base + S5P_HDMI_TG_CMD);

	hdmi_bit_set(time, reg, S5P_HDMI_GETSYNC_TYPE);
	hdmi_bit_set(bt656, reg, S5P_HDMI_GETSYNC);
	hdmi_bit_set(tg, reg, S5P_HDMI_TG);

	writeb(reg, hdmi_base + S5P_HDMI_TG_CMD);
}

void s5p_hdmi_reg_enable(bool en)
{
	u8 reg;

	reg = readb(hdmi_base + S5P_HDMI_CON_0);

	if (en)
		reg |= S5P_HDMI_EN;
	else
		reg &= ~(S5P_HDMI_EN | S5P_HDMI_ASP_EN);

	writeb(reg, hdmi_base + S5P_HDMI_CON_0);

	if (!en) {
		do {
			reg = readb(hdmi_base + S5P_HDMI_CON_0);
		} while (reg & S5P_HDMI_EN);
	}
}

u8 s5p_hdmi_reg_intc_status(void)
{
#ifdef CONFIG_HDMI_14A_3D
	return readb(hdmi_base + S5P_HDMI_INTC_FLAG0);
#else
	return readb(hdmi_base + S5P_HDMI_INTC_FLAG);
#endif
}

u8 s5p_hdmi_reg_intc_get_enabled(void)
{
#ifdef CONFIG_HDMI_14A_3D
	return readb(hdmi_base + S5P_HDMI_INTC_CON0);
#else
	return readb(hdmi_base + S5P_HDMI_INTC_CON);
#endif
}

void s5p_hdmi_reg_intc_clear_pending(enum s5p_hdmi_interrrupt intr)
{
	u8 reg;
#ifdef CONFIG_HDMI_14A_3D
	reg = readb(hdmi_base + S5P_HDMI_INTC_FLAG0);
	writeb(reg | (1 << intr), hdmi_base + S5P_HDMI_INTC_FLAG0);
#else
	reg = readb(hdmi_base + S5P_HDMI_INTC_FLAG);
	writeb(reg | (1 << intr), hdmi_base + S5P_HDMI_INTC_FLAG);
#endif
}


void s5p_hdmi_reg_sw_hpd_enable(bool enable)
{
	u8 reg;

	reg = readb(hdmi_base + S5P_HDMI_HPD);
	reg &= ~S5P_HDMI_HPD_SEL_I_HPD;

	if (enable)
		writeb(reg | S5P_HDMI_HPD_SEL_I_HPD, hdmi_base + S5P_HDMI_HPD);
	else
		writeb(reg, hdmi_base + S5P_HDMI_HPD);
}

void s5p_hdmi_reg_set_hpd_onoff(bool on_off)
{
	u8 reg;

	reg = readb(hdmi_base + S5P_HDMI_HPD);
	reg &= ~S5P_HDMI_SW_HPD_PLUGGED;

	if (on_off)
		writel(reg | S5P_HDMI_SW_HPD_PLUGGED,
			hdmi_base + S5P_HDMI_HPD);
	else
		writel(reg | S5P_HDMI_SW_HPD_UNPLUGGED,
			hdmi_base + S5P_HDMI_HPD);

}

u8 s5p_hdmi_reg_get_hpd_status(void)
{
	return readb(hdmi_base + S5P_HDMI_HPD_STATUS);
}

void s5p_hdmi_reg_hpd_gen(void)
{
#ifdef CONFIG_HDMI_14A_3D
	writeb(0xFF, hdmi_base + S5P_HDMI_HPD_GEN0);
#else
	writeb(0xFF, hdmi_base + S5P_HDMI_HPD_GEN);
#endif
}

int s5p_hdmi_reg_intc_set_isr(irqreturn_t (*isr)(int, void *), u8 num)
{
	if (!isr) {
		tvout_err("invalid irq routine\n");
		return -1;
	}

	if (num >= HDMI_IRQ_TOTAL_NUM) {
		tvout_err("max irq_num exceeded\n");
		return -1;
	}

	if (s5p_hdmi_isr_ftn[num])
		tvout_dbg("irq %d already registered\n", num);

	s5p_hdmi_isr_ftn[num] = isr;

	tvout_dbg("success to register irq : %d\n", num);

	return 0;
}
EXPORT_SYMBOL(s5p_hdmi_reg_intc_set_isr);

void s5p_hdmi_reg_intc_enable(enum s5p_hdmi_interrrupt intr, u8 en)
{
	u8 reg;

	reg = s5p_hdmi_reg_intc_get_enabled();

	if (en) {
		if (!reg)
			reg |= S5P_HDMI_INTC_EN_GLOBAL;

		reg |= (1 << intr);
	} else {
		reg &= ~(1 << intr);

		if (!reg)
			reg &= ~S5P_HDMI_INTC_EN_GLOBAL;
	}
#ifdef CONFIG_HDMI_14A_3D
	writeb(reg, hdmi_base + S5P_HDMI_INTC_CON0);
#else
	writeb(reg, hdmi_base + S5P_HDMI_INTC_CON);
#endif
}

void s5p_hdmi_reg_audio_enable(u8 en)
{
	u8 con, mod;
	con = readb(hdmi_base + S5P_HDMI_CON_0);
	mod = readb(hdmi_base + S5P_HDMI_MODE_SEL);

	if (en) {
#ifndef CONFIG_HDMI_EARJACK_MUTE
		if (mod & S5P_HDMI_DVI_MODE_EN)
#else
		if ((mod & S5P_HDMI_DVI_MODE_EN) || hdmi_audio_ext)
#endif
			return;

		con |= S5P_HDMI_ASP_EN;
		writeb(HDMI_TRANS_EVERY_SYNC, hdmi_base + S5P_HDMI_AUI_CON);
	} else {
		con &= ~S5P_HDMI_ASP_EN;
		writeb(HDMI_DO_NOT_TANS, hdmi_base + S5P_HDMI_AUI_CON);
	}

	writeb(con, hdmi_base + S5P_HDMI_CON_0);
}

int s5p_hdmi_audio_init(
		enum s5p_tvout_audio_codec_type audio_codec,
		u32 sample_rate, u32 bits, u32 frame_size_code,
		struct s5p_hdmi_audio *audio)
{
#ifdef CONFIG_SND_SAMSUNG_SPDIF
	s5p_hdmi_audio_set_config(audio_codec);
	s5p_hdmi_audio_set_repetition_time(audio_codec, bits, frame_size_code);
	s5p_hdmi_audio_irq_enable(S5P_HDMI_SPDIFIN_IRQ_OVERFLOW_EN);
	s5p_hdmi_audio_clock_enable();
#else
	s5p_hdmi_audio_i2s_config(audio_codec, sample_rate, bits,
				frame_size_code, audio);
#endif
	return 0;
}

void s5p_hdmi_reg_mute(bool en)
{
	s5p_hdmi_reg_bluescreen(en);

	s5p_hdmi_reg_audio_enable(!en);
}

irqreturn_t s5p_hdmi_irq(int irq, void *dev_id)
{
	u8 state, num = 0;
	unsigned long spin_flags;

	spin_lock_irqsave(&lock_hdmi, spin_flags);

#ifdef CONFIG_HDMI_14A_3D
	state = readb(hdmi_base + S5P_HDMI_INTC_FLAG0);
#else
	state = readb(hdmi_base + S5P_HDMI_INTC_FLAG);
#endif

	if (!state) {
		tvout_err("undefined irq : %d\n", state);
		goto irq_handled;
	}

	for (num = 0; num < HDMI_IRQ_TOTAL_NUM; num++) {

		if (!(state & (1 << num)))
			continue;

		if (s5p_hdmi_isr_ftn[num]) {
			tvout_dbg("call by s5p_hdmi_isr_ftn num : %d\n", num);
			(s5p_hdmi_isr_ftn[num])(num, NULL);
		} else
			tvout_dbg("unregistered irq : %d\n", num);
	}

irq_handled:
	spin_unlock_irqrestore(&lock_hdmi, spin_flags);

	return IRQ_HANDLED;
}

void s5p_hdmi_init(void __iomem *hdmi_addr)
{
	hdmi_base = hdmi_addr;
	spin_lock_init(&lock_hdmi);
}

void s5p_hdmi_phy_init(void __iomem *hdmi_phy_addr)
{
	i2c_hdmi_phy_base = hdmi_phy_addr;
	if (i2c_hdmi_phy_base != NULL)
		writeb(0x5, i2c_hdmi_phy_base + HDMI_I2C_LC);
}

void s5p_hdmi_reg_output(struct s5p_hdmi_o_reg *reg)
{
	writeb(reg->pxl_limit, hdmi_base + S5P_HDMI_CON_1);
	writeb(reg->preemble, hdmi_base + S5P_HDMI_CON_2);
	writeb(reg->mode, hdmi_base + S5P_HDMI_MODE_SEL);
}

void s5p_hdmi_reg_packet_trans(struct s5p_hdmi_o_trans *trans)
{
	u8 reg;

	writeb(trans->avi, hdmi_base + S5P_HDMI_AVI_CON);
	writeb(trans->mpg, hdmi_base + S5P_HDMI_MPG_CON);
	writeb(trans->spd, hdmi_base + S5P_HDMI_SPD_CON);
	writeb(trans->gmp, hdmi_base + S5P_HDMI_GAMUT_CON);
	writeb(trans->aui, hdmi_base + S5P_HDMI_AUI_CON);

	reg = trans->gcp | readb(hdmi_base + S5P_HDMI_GCP_CON);
	writeb(reg, hdmi_base + S5P_HDMI_GCP_CON);

	reg = trans->isrc | readb(hdmi_base + S5P_HDMI_ISRC_CON);
	writeb(reg, hdmi_base + S5P_HDMI_ISRC_CON);

	reg = trans->acp | readb(hdmi_base + S5P_HDMI_ACP_CON);
	writeb(reg, hdmi_base + S5P_HDMI_ACP_CON);

	reg = trans->acr | readb(hdmi_base + S5P_HDMI_ACP_CON);
	writeb(reg, hdmi_base + S5P_HDMI_ACR_CON);
}
