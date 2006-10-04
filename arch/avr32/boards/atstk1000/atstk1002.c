/*
 * ATSTK1002 daughterboard-specific init code
 *
 * Copyright (C) 2005-2006 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/init.h>

#include <asm/arch/board.h>
#include <asm/arch/init.h>

struct eth_platform_data __initdata eth0_data = {
	.valid		= 1,
	.mii_phy_addr	= 0x10,
	.is_rmii	= 0,
	.hw_addr	= { 0x6a, 0x87, 0x71, 0x14, 0xcd, 0xcb },
};

extern struct lcdc_platform_data atstk1000_fb0_data;

void __init setup_board(void)
{
	at32_map_usart(1, 0);	/* /dev/ttyS0 */
	at32_map_usart(2, 1);	/* /dev/ttyS1 */
	at32_map_usart(3, 2);	/* /dev/ttyS2 */

	at32_setup_serial_console(0);
}

static int __init atstk1002_init(void)
{
	at32_add_system_devices();

	at32_add_device_usart(0);
	at32_add_device_usart(1);
	at32_add_device_usart(2);

	at32_add_device_eth(0, &eth0_data);
	at32_add_device_spi(0);
	at32_add_device_lcdc(0, &atstk1000_fb0_data);

	return 0;
}
postcore_initcall(atstk1002_init);
