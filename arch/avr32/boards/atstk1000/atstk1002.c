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
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/types.h>

#include <asm/setup.h>
#include <asm/arch/board.h>
#include <asm/arch/init.h>

static struct eth_platform_data __initdata eth_data[2];
extern struct lcdc_platform_data atstk1000_fb0_data;

static int __init parse_tag_ethernet(struct tag *tag)
{
	int i;

	i = tag->u.ethernet.mac_index;
	if (i < ARRAY_SIZE(eth_data)) {
		eth_data[i].mii_phy_addr = tag->u.ethernet.mii_phy_addr;
		memcpy(&eth_data[i].hw_addr, tag->u.ethernet.hw_address,
		       sizeof(eth_data[i].hw_addr));
		eth_data[i].valid = 1;
	}
	return 0;
}
__tagtable(ATAG_ETHERNET, parse_tag_ethernet);

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

	if (eth_data[0].valid)
		at32_add_device_eth(0, &eth_data[0]);

	at32_add_device_spi(0);
	at32_add_device_lcdc(0, &atstk1000_fb0_data);

	return 0;
}
postcore_initcall(atstk1002_init);
