/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * include/linux/mfd/serdes/gpio.h -- GPIO for different serdes chip
 *
 * Copyright (c) 2023-2028 Rockchip Electronics Co. Ltd.
 *
 * Author: luowei <lw@rock-chips.com>
 *
 */

#ifndef __MFD_SERDES_MAXIM_MAX96772_H__
#define __MFD_SERDES_MAXIM_MAX96772_H__

#define GPIO_A_REG(gpio)	(0x02b0 + ((gpio) * 3))
#define GPIO_B_REG(gpio)	(0x02b1 + ((gpio) * 3))
#define GPIO_C_REG(gpio)	(0x02b2 + ((gpio) * 3))


/* 02b0h */
#define RES_CFG			BIT(7)
#define RSVD			BIT(6)
#define TX_COMP_EN		BIT(5)
#define GPIO_OUT		BIT(4)
#define GPIO_IN			BIT(3)
#define GPIO_RX_EN		BIT(2)
#define GPIO_TX_EN		BIT(1)
#define GPIO_OUT_DIS		BIT(0)

/* 02b1h */
#define PULL_UPDN_SEL		GENMASK(7, 6)
#define OUT_TYPE		BIT(5)
#define GPIO_TX_ID		GENMASK(4, 0)

/* 02b2h */
#define OVR_RES_CFG		BIT(7)
#define GPIO_RX_ID		GENMASK(4, 0)

#endif
