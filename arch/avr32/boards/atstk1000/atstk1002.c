/*
 * ATSTK1002/ATSTK1006 daughterboard-specific init code
 *
 * Copyright (C) 2005-2007 Atmel Corporation
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
#include <linux/spi/spi.h>
#include <linux/spi/at73c213.h>
#include <linux/atmel-mci.h>

#include <video/atmel_lcdc.h>

#include <asm/io.h>
#include <asm/setup.h>

#include <mach/at32ap700x.h>
#include <mach/board.h>
#include <mach/init.h>
#include <mach/portmux.h>

#include "atstk1000.h"

/* Oscillator frequencies. These are board specific */
unsigned long at32_board_osc_rates[3] = {
	[0] = 32768,	/* 32.768 kHz on RTC osc */
	[1] = 20000000,	/* 20 MHz on osc0 */
	[2] = 12000000,	/* 12 MHz on osc1 */
};

/*
 * The ATSTK1006 daughterboard is very similar to the ATSTK1002. Both
 * have the AT32AP7000 chip on board; the difference is that the
 * STK1006 has 128 MB SDRAM (the STK1002 uses the 8 MB SDRAM chip on
 * the STK1000 motherboard) and 256 MB NAND flash (the STK1002 has
 * none.)
 *
 * The RAM difference is handled by the boot loader, so the only
 * difference we end up handling here is the NAND flash.
 */
#ifdef CONFIG_BOARD_ATSTK1006
#include <linux/mtd/partitions.h>
#include <mach/smc.h>

static struct smc_timing nand_timing __initdata = {
	.ncs_read_setup		= 0,
	.nrd_setup		= 10,
	.ncs_write_setup	= 0,
	.nwe_setup		= 10,

	.ncs_read_pulse		= 30,
	.nrd_pulse		= 15,
	.ncs_write_pulse	= 30,
	.nwe_pulse		= 15,

	.read_cycle		= 30,
	.write_cycle		= 30,

	.ncs_read_recover	= 0,
	.nrd_recover		= 15,
	.ncs_write_recover	= 0,
	/* WE# high -> RE# low min 60 ns */
	.nwe_recover		= 50,
};

static struct smc_config nand_config __initdata = {
	.bus_width		= 1,
	.nrd_controlled		= 1,
	.nwe_controlled		= 1,
	.nwait_mode		= 0,
	.byte_write		= 0,
	.tdf_cycles		= 2,
	.tdf_mode		= 0,
};

static struct mtd_partition nand_partitions[] = {
	{
		.name		= "main",
		.offset		= 0x00000000,
		.size		= MTDPART_SIZ_FULL,
	},
};

static struct atmel_nand_data atstk1006_nand_data __initdata = {
	.cle		= 21,
	.ale		= 22,
	.rdy_pin	= GPIO_PIN_PB(30),
	.enable_pin	= GPIO_PIN_PB(29),
	.parts		= nand_partitions,
	.num_parts	= ARRAY_SIZE(num_partitions),
};
#endif

struct eth_addr {
	u8 addr[6];
};

static struct eth_addr __initdata hw_addr[2];
static struct macb_platform_data __initdata eth_data[2] = {
	{
		/*
		 * The MDIO pullups on STK1000 are a bit too weak for
		 * the autodetection to work properly, so we have to
		 * mask out everything but the correct address.
		 */
		.phy_mask	= ~(1U << 16),
	},
	{
		.phy_mask	= ~(1U << 17),
	},
};

#ifdef CONFIG_BOARD_ATSTK1000_EXTDAC
static struct at73c213_board_info at73c213_data = {
	.ssc_id		= 0,
	.shortname	= "AVR32 STK1000 external DAC",
};
#endif

#ifndef CONFIG_BOARD_ATSTK100X_SW1_CUSTOM
static struct spi_board_info spi0_board_info[] __initdata = {
#ifdef CONFIG_BOARD_ATSTK1000_EXTDAC
	{
		/* AT73C213 */
		.modalias	= "at73c213",
		.max_speed_hz	= 200000,
		.chip_select	= 0,
		.mode		= SPI_MODE_1,
		.platform_data	= &at73c213_data,
	},
#endif
	{
		/* QVGA display */
		.modalias	= "ltv350qv",
		.max_speed_hz	= 16000000,
		.chip_select	= 1,
		.mode		= SPI_MODE_3,
	},
};
#endif

#ifdef CONFIG_BOARD_ATSTK100X_SPI1
static struct spi_board_info spi1_board_info[] __initdata = { {
	/* patch in custom entries here */
} };
#endif

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
	if (IS_ERR(pclk))
		return;

	clk_enable(pclk);
	__raw_writel((addr[3] << 24) | (addr[2] << 16)
		     | (addr[1] << 8) | addr[0], regs + 0x98);
	__raw_writel((addr[5] << 8) | addr[4], regs + 0x9c);
	clk_disable(pclk);
	clk_put(pclk);
}

#ifdef CONFIG_BOARD_ATSTK1000_EXTDAC
static void __init atstk1002_setup_extdac(void)
{
	struct clk *gclk;
	struct clk *pll;

	gclk = clk_get(NULL, "gclk0");
	if (IS_ERR(gclk))
		goto err_gclk;
	pll = clk_get(NULL, "pll0");
	if (IS_ERR(pll))
		goto err_pll;

	if (clk_set_parent(gclk, pll)) {
		pr_debug("STK1000: failed to set pll0 as parent for DAC clock\n");
		goto err_set_clk;
	}

	at32_select_periph(GPIO_PIOA_BASE, (1 << 30), GPIO_PERIPH_A, 0);
	at73c213_data.dac_clk = gclk;

err_set_clk:
	clk_put(pll);
err_pll:
	clk_put(gclk);
err_gclk:
	return;
}
#else
static void __init atstk1002_setup_extdac(void)
{

}
#endif /* CONFIG_BOARD_ATSTK1000_EXTDAC */

void __init setup_board(void)
{
#ifdef	CONFIG_BOARD_ATSTK100X_SW2_CUSTOM
	at32_map_usart(0, 1, 0);	/* USART 0/B: /dev/ttyS1, IRDA */
#else
	at32_map_usart(1, 0, 0);	/* USART 1/A: /dev/ttyS0, DB9 */
#endif
	/* USART 2/unused: expansion connector */
	at32_map_usart(3, 2, 0);	/* USART 3/C: /dev/ttyS2, DB9 */

	at32_setup_serial_console(0);
}

#ifndef CONFIG_BOARD_ATSTK100X_SW2_CUSTOM

static struct mci_platform_data __initdata mci0_data = {
	.slot[0] = {
		.bus_width	= 4,

/* MMC card detect requires MACB0 *NOT* be used */
#ifdef CONFIG_BOARD_ATSTK1002_SW6_CUSTOM
		.detect_pin	= GPIO_PIN_PC(14), /* gpio30/sdcd */
		.wp_pin		= GPIO_PIN_PC(15), /* gpio31/sdwp */
#else
		.detect_pin	= -ENODEV,
		.wp_pin		= -ENODEV,
#endif	/* SW6 for sd{cd,wp} routing */
	},
};

#endif	/* SW2 for MMC signal routing */

static int __init atstk1002_init(void)
{
	/*
	 * ATSTK1000 uses 32-bit SDRAM interface. Reserve the
	 * SDRAM-specific pins so that nobody messes with them.
	 */
	at32_reserve_pin(GPIO_PIOE_BASE, ATMEL_EBI_PE_DATA_ALL);

#ifdef CONFIG_BOARD_ATSTK1006
	smc_set_timing(&nand_config, &nand_timing);
	smc_set_configuration(3, &nand_config);
	at32_add_device_nand(0, &atstk1006_nand_data);
#endif

#ifdef	CONFIG_BOARD_ATSTK100X_SW2_CUSTOM
	at32_add_device_usart(1);
#else
	at32_add_device_usart(0);
#endif
	at32_add_device_usart(2);

#ifndef CONFIG_BOARD_ATSTK1002_SW6_CUSTOM
	set_hw_addr(at32_add_device_eth(0, &eth_data[0]));
#endif
#ifndef CONFIG_BOARD_ATSTK100X_SW1_CUSTOM
	at32_add_device_spi(0, spi0_board_info, ARRAY_SIZE(spi0_board_info));
#endif
#ifdef CONFIG_BOARD_ATSTK100X_SPI1
	at32_add_device_spi(1, spi1_board_info, ARRAY_SIZE(spi1_board_info));
#endif
#ifndef CONFIG_BOARD_ATSTK100X_SW2_CUSTOM
	at32_add_device_mci(0, &mci0_data);
#endif
#ifdef CONFIG_BOARD_ATSTK1002_SW5_CUSTOM
	set_hw_addr(at32_add_device_eth(1, &eth_data[1]));
#else
	at32_add_device_lcdc(0, &atstk1000_lcdc_data,
			     fbmem_start, fbmem_size,
			     ATMEL_LCDC_PRI_24BIT | ATMEL_LCDC_PRI_CONTROL);
#endif
	at32_add_device_usba(0, NULL);
#ifndef CONFIG_BOARD_ATSTK100X_SW3_CUSTOM
	at32_add_device_ssc(0, ATMEL_SSC_TX);
#endif

	atstk1000_setup_j2_leds();
	atstk1002_setup_extdac();

	return 0;
}
postcore_initcall(atstk1002_init);
