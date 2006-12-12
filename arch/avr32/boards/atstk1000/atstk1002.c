/*
 * ATSTK1002 daughterboard-specific init code
 *
 * Copyright (C) 2005-2006 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/clk.h>
#include <linux/etherdevice.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/string.h>
#include <linux/types.h>

#include <asm/io.h>
#include <asm/setup.h>
#include <asm/arch/board.h>
#include <asm/arch/init.h>

struct eth_addr {
	u8 addr[6];
};

static struct eth_addr __initdata hw_addr[2];

static struct eth_platform_data __initdata eth_data[2];
extern struct lcdc_platform_data atstk1000_fb0_data;

/*
 * The next two functions should go away as the boot loader is
 * supposed to initialize the macb address registers with a valid
 * ethernet address. But we need to keep it around for a while until
 * we can be reasonably sure the boot loader does this.
 *
 * The phy_id is ignored as the driver will probe for it.
 */
static int __init parse_tag_ethernet(struct tag *tag)
{
	int i;

	i = tag->u.ethernet.mac_index;
	if (i < ARRAY_SIZE(hw_addr))
		memcpy(hw_addr[i].addr, tag->u.ethernet.hw_address,
		       sizeof(hw_addr[i].addr));

	return 0;
}
__tagtable(ATAG_ETHERNET, parse_tag_ethernet);

static void __init set_hw_addr(struct platform_device *pdev)
{
	struct resource *res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	const u8 *addr;
	void __iomem *regs;
	struct clk *pclk;

	if (!res)
		return;
	if (pdev->id >= ARRAY_SIZE(hw_addr))
		return;

	addr = hw_addr[pdev->id].addr;
	if (!is_valid_ether_addr(addr))
		return;

	/*
	 * Since this is board-specific code, we'll cheat and use the
	 * physical address directly as we happen to know that it's
	 * the same as the virtual address.
	 */
	regs = (void __iomem __force *)res->start;
	pclk = clk_get(&pdev->dev, "pclk");
	if (!pclk)
		return;

	clk_enable(pclk);
	__raw_writel((addr[3] << 24) | (addr[2] << 16)
		     | (addr[1] << 8) | addr[0], regs + 0x98);
	__raw_writel((addr[5] << 8) | addr[4], regs + 0x9c);
	clk_disable(pclk);
	clk_put(pclk);
}

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

	set_hw_addr(at32_add_device_eth(0, &eth_data[0]));

	at32_add_device_spi(0);
	at32_add_device_lcdc(0, &atstk1000_fb0_data);

	return 0;
}
postcore_initcall(atstk1002_init);
