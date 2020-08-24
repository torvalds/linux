/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2016 - 2019 Realtek Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 ******************************************************************************/

#ifndef HALMAC_GPIO_CMD
#define HALMAC_GPIO_CMD

#include "halmac_2_platform.h"

/* GPIO ID */
#define HALMAC_GPIO0		0
#define HALMAC_GPIO1		1
#define HALMAC_GPIO2		2
#define HALMAC_GPIO3		3
#define HALMAC_GPIO4		4
#define HALMAC_GPIO5		5
#define HALMAC_GPIO6		6
#define HALMAC_GPIO7		7
#define HALMAC_GPIO8		8
#define HALMAC_GPIO9		9
#define HALMAC_GPIO10		10
#define HALMAC_GPIO11		11
#define HALMAC_GPIO12		12
#define HALMAC_GPIO13		13
#define HALMAC_GPIO14		14
#define HALMAC_GPIO15		15
#define HALMAC_GPIO_NUM		16

/* GPIO type */
#define HALMAC_GPIO_IN		0
#define HALMAC_GPIO_OUT		1
#define HALMAC_GPIO_IN_OUT	2

/* Function name */
#define HALMAC_WL_HWPDN			0
#define HALMAC_BT_HWPDN			1
#define HALMAC_BT_GPIO			2
#define HALMAC_WL_HW_EXTWOL		3
#define HALMAC_BT_HW_EXTWOL		4
#define HALMAC_BT_SFLASH		5
#define HALMAC_WL_SFLASH		6
#define HALMAC_WL_LED			7
#define HALMAC_SDIO_INT			8
#define HALMAC_UART0			9
#define HALMAC_EEPROM			10
#define HALMAC_JTAG			11
#define HALMAC_LTE_COEX_UART		12
#define HALMAC_3W_LTE_WL_GPIO		13
#define HALMAC_GPIO2_3_WL_CTRL_EN	14
#define HALMAC_GPIO13_14_WL_CTRL_EN	15
#define HALMAC_DBG_GNT_WL_BT		16
#define HALMAC_BT_3DDLS_A		17
#define HALMAC_BT_3DDLS_B		18
#define HALMAC_BT_PTA			19
#define HALMAC_WL_PTA			20
#define HALMAC_WL_UART			21
#define HALMAC_WLMAC_DBG		22
#define HALMAC_WLPHY_DBG		23
#define HALMAC_BT_DBG			24
#define HALMAC_WLPHY_RFE_CTRL2GPIO	25
#define HALMAC_EXT_XTAL			26
#define HALMAC_SW_IO			27
#define HALMAC_BT_SDIO_INT		28
#define HALMAC_BT_JTAG			29
#define HALMAC_WL_JTAG			30
#define HALMAC_BT_RF			31
#define HALMAC_WLPHY_RFE_CTRL2GPIO_2	32
#define HALMAC_MAILBOX_3W		33
#define HALMAC_MAILBOX_1W		34
#define HALMAC_SW_DPDT_SEL		35
#define HALMAC_BT_DPDT_SEL		36
#define HALMAC_WL_DPDT_SEL		37
#define HALMAC_BT_PAPE_SEL		38
#define HALMAC_SW_PAPE_SEL		39
#define HALMAC_WL_PAPE_SEL		40
#define HALMAC_SW_LNAON_SET		41
#define HALMAC_BT_LNAON_SEL		42
#define HALMAC_WL_LNAON_SEL		43
#define HALMAC_SWR_CTRL_EN		44
#define HALMAC_UART_BRIDGE		45
#define HALMAC_BT_I2C			46
#define HALMAC_BTCOEX_CMD		47
#define HALMAC_BT_UART_INTF		48
#define HALMAC_DATA_CPU_JTAG		49
#define HALMAC_DATA_CPU_SFLASH		50
#define HALMAC_DATA_CPU_UART		51

struct halmac_gpio_pimux_list {
	u16 func;
	u8 id;
	u8 type;
	u16 offset;
	u8 msk;
	u8 value;
};

#endif
