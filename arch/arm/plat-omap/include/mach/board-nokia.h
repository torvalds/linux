/*
 *  arch/arm/plat-omap/include/mach/board-nokia.h
 *
 *  Information structures for Nokia-specific board config data
 *
 *  Copyright (C) 2005	Nokia Corporation
 */

#ifndef _OMAP_BOARD_NOKIA_H
#define _OMAP_BOARD_NOKIA_H

#include <linux/types.h>

#define OMAP_TAG_NOKIA_BT	0x4e01
#define OMAP_TAG_WLAN_CX3110X	0x4e02
#define OMAP_TAG_CBUS		0x4e03
#define OMAP_TAG_EM_ASIC_BB5	0x4e04


#define BT_CHIP_CSR		1
#define BT_CHIP_TI		2

#define BT_SYSCLK_12		1
#define BT_SYSCLK_38_4		2

struct omap_bluetooth_config {
	u8    chip_type;
	u8    bt_wakeup_gpio;
	u8    host_wakeup_gpio;
	u8    reset_gpio;
	u8    bt_uart;
	u8    bd_addr[6];
	u8    bt_sysclk;
};

struct omap_wlan_cx3110x_config {
	u8  chip_type;
	s16 power_gpio;
	s16 irq_gpio;
	s16 spi_cs_gpio;
};

struct omap_cbus_config {
	s16 clk_gpio;
	s16 dat_gpio;
	s16 sel_gpio;
};

struct omap_em_asic_bb5_config {
	s16 retu_irq_gpio;
	s16 tahvo_irq_gpio;
};

#endif
