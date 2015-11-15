/*
 * QNAP TS-x09 Boards common functions
 *
 * Maintainers: Lennert Buytenhek <buytenh@marvell.com>
 *		Byron Bradley <byron.bbradley@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/mv643xx_eth.h>
#include <linux/timex.h>
#include <linux/serial_reg.h>
#include <mach/orion5x.h>
#include "tsx09-common.h"
#include "common.h"

/*****************************************************************************
 * QNAP TS-x09 specific power off method via UART1-attached PIC
 ****************************************************************************/

#define UART1_REG(x)	(UART1_VIRT_BASE + ((UART_##x) << 2))

void qnap_tsx09_power_off(void)
{
	/* 19200 baud divisor */
	const unsigned divisor = ((orion5x_tclk + (8 * 19200)) / (16 * 19200));

	pr_info("%s: triggering power-off...\n", __func__);

	/* hijack uart1 and reset into sane state (19200,8n1) */
	writel(0x83, UART1_REG(LCR));
	writel(divisor & 0xff, UART1_REG(DLL));
	writel((divisor >> 8) & 0xff, UART1_REG(DLM));
	writel(0x03, UART1_REG(LCR));
	writel(0x00, UART1_REG(IER));
	writel(0x00, UART1_REG(FCR));
	writel(0x00, UART1_REG(MCR));

	/* send the power-off command 'A' to PIC */
	writel('A', UART1_REG(TX));
}

/*****************************************************************************
 * Ethernet
 ****************************************************************************/

struct mv643xx_eth_platform_data qnap_tsx09_eth_data = {
	.phy_addr	= MV643XX_ETH_PHY_ADDR(8),
};

static int __init qnap_tsx09_check_mac_addr(const char *addr_str)
{
	u_int8_t addr[6];

	if (!mac_pton(addr_str, addr))
		return -1;

	printk(KERN_INFO "tsx09: found ethernet mac address %pM\n", addr);

	memcpy(qnap_tsx09_eth_data.mac_addr, addr, 6);

	return 0;
}

/*
 * The 'NAS Config' flash partition has an ext2 filesystem which
 * contains a file that has the ethernet MAC address in plain text
 * (format "xx:xx:xx:xx:xx:xx\n").
 */
void __init qnap_tsx09_find_mac_addr(u32 mem_base, u32 size)
{
	unsigned long addr;

	for (addr = mem_base; addr < (mem_base + size); addr += 1024) {
		void __iomem *nor_page;
		int ret = 0;

		nor_page = ioremap(addr, 1024);
		if (nor_page != NULL) {
			ret = qnap_tsx09_check_mac_addr((__force const char *)nor_page);
			iounmap(nor_page);
		}

		if (ret == 0)
			break;
	}
}
