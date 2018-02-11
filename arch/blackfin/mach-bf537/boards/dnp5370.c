/*
 * This is the configuration for SSV Dil/NetPC DNP/5370 board.
 *
 * DIL module:         http://www.dilnetpc.com/dnp0086.htm
 * SK28 (starter kit): http://www.dilnetpc.com/dnp0088.htm
 *
 * Copyright 2010 3ality Digital Systems
 * Copyright 2005 National ICT Australia (NICTA)
 * Copyright 2004-2006 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#include <linux/device.h>
#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/rawnand.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/plat-ram.h>
#include <linux/mtd/physmap.h>
#include <linux/spi/spi.h>
#include <linux/spi/flash.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/spi/mmc_spi.h>
#include <linux/phy.h>
#include <asm/dma.h>
#include <asm/bfin5xx_spi.h>
#include <asm/reboot.h>
#include <asm/portmux.h>
#include <asm/dpmc.h>

/*
 * Name the Board for the /proc/cpuinfo
 */
const char bfin_board_name[] = "DNP/5370";
#define FLASH_MAC               0x202f0000
#define CONFIG_MTD_PHYSMAP_LEN  0x300000

#if IS_ENABLED(CONFIG_RTC_DRV_BFIN)
static struct platform_device rtc_device = {
	.name = "rtc-bfin",
	.id   = -1,
};
#endif

#if IS_ENABLED(CONFIG_BFIN_MAC)
#include <linux/bfin_mac.h>
static const unsigned short bfin_mac_peripherals[] = P_RMII0;

static struct bfin_phydev_platform_data bfin_phydev_data[] = {
	{
		.addr = 1,
		.irq = PHY_POLL, /* IRQ_MAC_PHYINT */
	},
};

static struct bfin_mii_bus_platform_data bfin_mii_bus_data = {
	.phydev_number   = 1,
	.phydev_data     = bfin_phydev_data,
	.phy_mode        = PHY_INTERFACE_MODE_RMII,
	.mac_peripherals = bfin_mac_peripherals,
};

static struct platform_device bfin_mii_bus = {
	.name = "bfin_mii_bus",
	.dev = {
		.platform_data = &bfin_mii_bus_data,
	}
};

static struct platform_device bfin_mac_device = {
	.name = "bfin_mac",
	.dev = {
		.platform_data = &bfin_mii_bus,
	}
};
#endif

#if IS_ENABLED(CONFIG_MTD_PHYSMAP)
static struct mtd_partition asmb_flash_partitions[] = {
	{
		.name       = "bootloader(nor)",
		.size       = 0x30000,
		.offset     = 0,
	}, {
		.name       = "linux kernel and rootfs(nor)",
		.size       = 0x300000 - 0x30000 - 0x10000,
		.offset     = MTDPART_OFS_APPEND,
	}, {
		.name       = "MAC address(nor)",
		.size       = 0x10000,
		.offset     = MTDPART_OFS_APPEND,
		.mask_flags = MTD_WRITEABLE,
	}
};

static struct physmap_flash_data asmb_flash_data = {
	.width      = 1,
	.parts      = asmb_flash_partitions,
	.nr_parts   = ARRAY_SIZE(asmb_flash_partitions),
};

static struct resource asmb_flash_resource = {
	.start = 0x20000000,
	.end   = 0x202fffff,
	.flags = IORESOURCE_MEM,
};

/* 4 MB NOR flash attached to async memory banks 0-2,
 * therefore only 3 MB visible.
 */
static struct platform_device asmb_flash_device = {
	.name	  = "physmap-flash",
	.id	  = 0,
	.dev = {
		.platform_data = &asmb_flash_data,
	},
	.num_resources = 1,
	.resource      = &asmb_flash_resource,
};
#endif

#if IS_ENABLED(CONFIG_SPI_BFIN5XX)

#if IS_ENABLED(CONFIG_MMC_SPI)

static struct bfin5xx_spi_chip mmc_spi_chip_info = {
	.enable_dma    = 0,	 /* use no dma transfer with this chip*/
};

#endif

#if IS_ENABLED(CONFIG_MTD_DATAFLASH)
/* This mapping is for at45db642 it has 1056 page size,
 * partition size and offset should be page aligned
 */
static struct mtd_partition bfin_spi_dataflash_partitions[] = {
	{
		.name   = "JFFS2 dataflash(nor)",
#ifdef CONFIG_MTD_PAGESIZE_1024
		.offset = 0x40000,
		.size   = 0x7C0000,
#else
		.offset = 0x0,
		.size   = 0x840000,
#endif
	}
};

static struct flash_platform_data bfin_spi_dataflash_data = {
	.name     = "mtd_dataflash",
	.parts    = bfin_spi_dataflash_partitions,
	.nr_parts = ARRAY_SIZE(bfin_spi_dataflash_partitions),
	.type     = "mtd_dataflash",
};

static struct bfin5xx_spi_chip spi_dataflash_chip_info = {
	.enable_dma    = 0,	 /* use no dma transfer with this chip*/
};
#endif

static struct spi_board_info bfin_spi_board_info[] __initdata = {
/* SD/MMC card reader at SPI bus */
#if IS_ENABLED(CONFIG_MMC_SPI)
	{
		.modalias	 = "mmc_spi",
		.max_speed_hz    = 20000000,
		.bus_num	 = 0,
		.chip_select     = 1,
		.controller_data = &mmc_spi_chip_info,
		.mode	         = SPI_MODE_3,
	},
#endif

/* 8 Megabyte Atmel NOR flash chip at SPI bus */
#if IS_ENABLED(CONFIG_MTD_DATAFLASH)
	{
	.modalias        = "mtd_dataflash",
	.max_speed_hz    = 16700000,
	.bus_num         = 0,
	.chip_select     = 2,
	.platform_data   = &bfin_spi_dataflash_data,
	.controller_data = &spi_dataflash_chip_info,
	.mode            = SPI_MODE_3, /* SPI_CPHA and SPI_CPOL */
	},
#endif
};

/* SPI controller data */
/* SPI (0) */
static struct resource bfin_spi0_resource[] = {
	[0] = {
		.start = SPI0_REGBASE,
		.end   = SPI0_REGBASE + 0xFF,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = CH_SPI,
		.end   = CH_SPI,
		.flags = IORESOURCE_DMA,
	},
	[2] = {
		.start = IRQ_SPI,
		.end   = IRQ_SPI,
		.flags = IORESOURCE_IRQ,
	},
};

static struct bfin5xx_spi_master spi_bfin_master_info = {
	.num_chipselect = 8,
	.enable_dma     = 1,  /* master has the ability to do dma transfer */
	.pin_req        = {P_SPI0_SCK, P_SPI0_MISO, P_SPI0_MOSI, 0},
};

static struct platform_device spi_bfin_master_device = {
	.name          = "bfin-spi",
	.id            = 0, /* Bus number */
	.num_resources = ARRAY_SIZE(bfin_spi0_resource),
	.resource      = bfin_spi0_resource,
	.dev           = {
		.platform_data = &spi_bfin_master_info, /* Passed to driver */
	},
};
#endif

#if IS_ENABLED(CONFIG_SERIAL_BFIN)
#ifdef CONFIG_SERIAL_BFIN_UART0
static struct resource bfin_uart0_resources[] = {
	{
		.start = UART0_THR,
		.end = UART0_GCTL+2,
		.flags = IORESOURCE_MEM,
	},
	{
		.start = IRQ_UART0_TX,
		.end = IRQ_UART0_TX,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = IRQ_UART0_RX,
		.end = IRQ_UART0_RX,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = IRQ_UART0_ERROR,
		.end = IRQ_UART0_ERROR,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = CH_UART0_TX,
		.end = CH_UART0_TX,
		.flags = IORESOURCE_DMA,
	},
	{
		.start = CH_UART0_RX,
		.end = CH_UART0_RX,
		.flags = IORESOURCE_DMA,
	},
};

static unsigned short bfin_uart0_peripherals[] = {
	P_UART0_TX, P_UART0_RX, 0
};

static struct platform_device bfin_uart0_device = {
	.name = "bfin-uart",
	.id = 0,
	.num_resources = ARRAY_SIZE(bfin_uart0_resources),
	.resource = bfin_uart0_resources,
	.dev = {
		.platform_data = &bfin_uart0_peripherals, /* Passed to driver */
	},
};
#endif

#ifdef CONFIG_SERIAL_BFIN_UART1
static struct resource bfin_uart1_resources[] = {
	{
		.start = UART1_THR,
		.end   = UART1_GCTL+2,
		.flags = IORESOURCE_MEM,
	},
	{
		.start = IRQ_UART1_TX,
		.end   = IRQ_UART1_TX,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = IRQ_UART1_RX,
		.end   = IRQ_UART1_RX,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = IRQ_UART1_ERROR,
		.end   = IRQ_UART1_ERROR,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = CH_UART1_TX,
		.end   = CH_UART1_TX,
		.flags = IORESOURCE_DMA,
	},
	{
		.start = CH_UART1_RX,
		.end   = CH_UART1_RX,
		.flags = IORESOURCE_DMA,
	},
};

static unsigned short bfin_uart1_peripherals[] = {
	P_UART1_TX, P_UART1_RX, 0
};

static struct platform_device bfin_uart1_device = {
	.name          = "bfin-uart",
	.id            = 1,
	.num_resources = ARRAY_SIZE(bfin_uart1_resources),
	.resource      = bfin_uart1_resources,
	.dev = {
		.platform_data = &bfin_uart1_peripherals, /* Passed to driver */
	},
};
#endif
#endif

#if IS_ENABLED(CONFIG_I2C_BLACKFIN_TWI)
static const u16 bfin_twi0_pins[] = {P_TWI0_SCL, P_TWI0_SDA, 0};

static struct resource bfin_twi0_resource[] = {
	[0] = {
		.start = TWI0_REGBASE,
		.end   = TWI0_REGBASE + 0xff,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = IRQ_TWI,
		.end   = IRQ_TWI,
		.flags = IORESOURCE_IRQ,
	},
};

static struct platform_device i2c_bfin_twi_device = {
	.name          = "i2c-bfin-twi",
	.id            = 0,
	.num_resources = ARRAY_SIZE(bfin_twi0_resource),
	.resource      = bfin_twi0_resource,
	.dev = {
		.platform_data = &bfin_twi0_pins,
	},
};
#endif

static struct platform_device *dnp5370_devices[] __initdata = {

#if IS_ENABLED(CONFIG_SERIAL_BFIN)
#ifdef CONFIG_SERIAL_BFIN_UART0
	&bfin_uart0_device,
#endif
#ifdef CONFIG_SERIAL_BFIN_UART1
	&bfin_uart1_device,
#endif
#endif

#if IS_ENABLED(CONFIG_MTD_PHYSMAP)
	&asmb_flash_device,
#endif

#if IS_ENABLED(CONFIG_BFIN_MAC)
	&bfin_mii_bus,
	&bfin_mac_device,
#endif

#if IS_ENABLED(CONFIG_SPI_BFIN5XX)
	&spi_bfin_master_device,
#endif

#if IS_ENABLED(CONFIG_I2C_BLACKFIN_TWI)
	&i2c_bfin_twi_device,
#endif

#if IS_ENABLED(CONFIG_RTC_DRV_BFIN)
	&rtc_device,
#endif

};

static int __init dnp5370_init(void)
{
	printk(KERN_INFO "DNP/5370: registering device resources\n");
	platform_add_devices(dnp5370_devices, ARRAY_SIZE(dnp5370_devices));
	printk(KERN_INFO "DNP/5370: registering %zu SPI slave devices\n",
	       ARRAY_SIZE(bfin_spi_board_info));
	spi_register_board_info(bfin_spi_board_info, ARRAY_SIZE(bfin_spi_board_info));
	printk(KERN_INFO "DNP/5370: MAC %pM\n", (void *)FLASH_MAC);
	return 0;
}
arch_initcall(dnp5370_init);

/*
 * Currently the MAC address is saved in Flash by U-Boot
 */
int bfin_get_ether_addr(char *addr)
{
	*(u32 *)(&(addr[0])) = bfin_read32(FLASH_MAC);
	*(u16 *)(&(addr[4])) = bfin_read16(FLASH_MAC + 4);
	return 0;
}
EXPORT_SYMBOL(bfin_get_ether_addr);
