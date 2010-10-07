/*
 * Copyright 2004-2009 Analog Devices Inc.
 *                2007 David Rowe
 *                2006 Intratrade Ltd.
 *                     Ivan Danov <idanov@gmail.com>
 *                2005 National ICT Australia (NICTA)
 *                     Aidan Williams <aidan@nicta.com.au>
 *
 * Licensed under the GPL-2 or later.
 */

#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/spi/spi.h>
#include <linux/spi/flash.h>
#if defined(CONFIG_USB_ISP1362_HCD) || defined(CONFIG_USB_ISP1362_HCD_MODULE)
#include <linux/usb/isp1362.h>
#endif
#include <asm/irq.h>
#include <asm/dma.h>
#include <asm/bfin5xx_spi.h>
#include <asm/portmux.h>
#include <mach/fio_flag.h>

/*
 * Name the Board for the /proc/cpuinfo
 */
const char bfin_board_name[] = "IP04/IP08";

/*
 *  Driver needs to know address, irq and flag pin.
 */
#if defined(CONFIG_BFIN532_IP0X)
#if defined(CONFIG_DM9000) || defined(CONFIG_DM9000_MODULE)

#include <linux/dm9000.h>

static struct resource dm9000_resource1[] = {
	{
		.start = 0x20100000,
		.end   = 0x20100000 + 1,
		.flags = IORESOURCE_MEM
	},{
		.start = 0x20100000 + 2,
		.end   = 0x20100000 + 3,
		.flags = IORESOURCE_MEM
	},{
		.start = IRQ_PF15,
		.end   = IRQ_PF15,
		.flags = IORESOURCE_IRQ | IORESOURCE_IRQ_HIGHEDGE
	}
};

static struct resource dm9000_resource2[] = {
	{
		.start = 0x20200000,
		.end   = 0x20200000 + 1,
		.flags = IORESOURCE_MEM
	},{
		.start = 0x20200000 + 2,
		.end   = 0x20200000 + 3,
		.flags = IORESOURCE_MEM
	},{
		.start = IRQ_PF14,
		.end   = IRQ_PF14,
		.flags = IORESOURCE_IRQ | IORESOURCE_IRQ_HIGHEDGE
	}
};

/*
* for the moment we limit ourselves to 16bit IO until some
* better IO routines can be written and tested
*/
static struct dm9000_plat_data dm9000_platdata1 = {
	.flags          = DM9000_PLATF_16BITONLY,
};

static struct platform_device dm9000_device1 = {
	.name           = "dm9000",
	.id             = 0,
	.num_resources  = ARRAY_SIZE(dm9000_resource1),
	.resource       = dm9000_resource1,
	.dev            = {
		.platform_data = &dm9000_platdata1,
	}
};

static struct dm9000_plat_data dm9000_platdata2 = {
	.flags          = DM9000_PLATF_16BITONLY,
};

static struct platform_device dm9000_device2 = {
	.name           = "dm9000",
	.id             = 1,
	.num_resources  = ARRAY_SIZE(dm9000_resource2),
	.resource       = dm9000_resource2,
	.dev            = {
		.platform_data = &dm9000_platdata2,
	}
};

#endif
#endif


#if defined(CONFIG_SPI_BFIN) || defined(CONFIG_SPI_BFIN_MODULE)
/* all SPI peripherals info goes here */

#if defined(CONFIG_MMC_SPI) || defined(CONFIG_MMC_SPI_MODULE)
static struct bfin5xx_spi_chip mmc_spi_chip_info = {
	.enable_dma = 0,		/* if 1 - block!!! */
	.bits_per_word = 8,
};
#endif

/* Notice: for blackfin, the speed_hz is the value of register
 * SPI_BAUD, not the real baudrate */
static struct spi_board_info bfin_spi_board_info[] __initdata = {
#if defined(CONFIG_MMC_SPI) || defined(CONFIG_MMC_SPI_MODULE)
	{
		.modalias = "mmc_spi",
		.max_speed_hz = 2,
		.bus_num = 1,
		.chip_select = 5,
		.controller_data = &mmc_spi_chip_info,
	},
#endif
};

/* SPI controller data */
static struct bfin5xx_spi_master spi_bfin_master_info = {
	.num_chipselect = 8,
	.enable_dma = 1,  /* master has the ability to do dma transfer */
};

static struct platform_device spi_bfin_master_device = {
	.name = "bfin-spi-master",
	.id = 1, /* Bus number */
	.dev = {
		.platform_data = &spi_bfin_master_info, /* Passed to driver */
	},
};
#endif  /* spi master and devices */

#if defined(CONFIG_SERIAL_BFIN) || defined(CONFIG_SERIAL_BFIN_MODULE)
#ifdef CONFIG_SERIAL_BFIN_UART0
static struct resource bfin_uart0_resources[] = {
	{
		.start = BFIN_UART_THR,
		.end = BFIN_UART_GCTL+2,
		.flags = IORESOURCE_MEM,
	},
	{
		.start = IRQ_UART0_RX,
		.end = IRQ_UART0_RX + 1,
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

unsigned short bfin_uart0_peripherals[] = {
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
#endif

#if defined(CONFIG_BFIN_SIR) || defined(CONFIG_BFIN_SIR_MODULE)
#ifdef CONFIG_BFIN_SIR0
static struct resource bfin_sir0_resources[] = {
	{
		.start = 0xFFC00400,
		.end = 0xFFC004FF,
		.flags = IORESOURCE_MEM,
	},
	{
		.start = IRQ_UART0_RX,
		.end = IRQ_UART0_RX+1,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = CH_UART0_RX,
		.end = CH_UART0_RX+1,
		.flags = IORESOURCE_DMA,
	},
};

static struct platform_device bfin_sir0_device = {
	.name = "bfin_sir",
	.id = 0,
	.num_resources = ARRAY_SIZE(bfin_sir0_resources),
	.resource = bfin_sir0_resources,
};
#endif
#endif

#if defined(CONFIG_USB_ISP1362_HCD) || defined(CONFIG_USB_ISP1362_HCD_MODULE)
static struct resource isp1362_hcd_resources[] = {
	{
		.start = 0x20300000,
		.end   = 0x20300000 + 1,
		.flags = IORESOURCE_MEM,
	},{
		.start = 0x20300000 + 2,
		.end   = 0x20300000 + 3,
		.flags = IORESOURCE_MEM,
	},{
		.start = IRQ_PF11,
		.end   = IRQ_PF11,
		.flags = IORESOURCE_IRQ | IORESOURCE_IRQ_HIGHLEVEL,
	},
};

static struct isp1362_platform_data isp1362_priv = {
	.sel15Kres = 1,
	.clknotstop = 0,
	.oc_enable = 0,		/* external OC */
	.int_act_high = 0,
	.int_edge_triggered = 0,
	.remote_wakeup_connected = 0,
	.no_power_switching = 1,
	.power_switching_mode = 0,
};

static struct platform_device isp1362_hcd_device = {
	.name = "isp1362-hcd",
	.id = 0,
	.dev = {
		.platform_data = &isp1362_priv,
	},
	.num_resources = ARRAY_SIZE(isp1362_hcd_resources),
	.resource = isp1362_hcd_resources,
};
#endif


static struct platform_device *ip0x_devices[] __initdata = {
#if defined(CONFIG_BFIN532_IP0X)
#if defined(CONFIG_DM9000) || defined(CONFIG_DM9000_MODULE)
	&dm9000_device1,
	&dm9000_device2,
#endif
#endif

#if defined(CONFIG_SPI_BFIN) || defined(CONFIG_SPI_BFIN_MODULE)
	&spi_bfin_master_device,
#endif

#if defined(CONFIG_SERIAL_BFIN) || defined(CONFIG_SERIAL_BFIN_MODULE)
#ifdef CONFIG_SERIAL_BFIN_UART0
	&bfin_uart0_device,
#endif
#endif

#if defined(CONFIG_BFIN_SIR) || defined(CONFIG_BFIN_SIR_MODULE)
#ifdef CONFIG_BFIN_SIR0
	&bfin_sir0_device,
#endif
#endif

#if defined(CONFIG_USB_ISP1362_HCD) || defined(CONFIG_USB_ISP1362_HCD_MODULE)
	&isp1362_hcd_device,
#endif
};

static int __init ip0x_init(void)
{
	int i;

	printk(KERN_INFO "%s(): registering device resources\n", __func__);
	platform_add_devices(ip0x_devices, ARRAY_SIZE(ip0x_devices));

#if defined(CONFIG_SPI_BFIN) || defined(CONFIG_SPI_BFIN_MODULE)
	for (i = 0; i < ARRAY_SIZE(bfin_spi_board_info); ++i) {
		int j = 1 << bfin_spi_board_info[i].chip_select;
		/* set spi cs to 1 */
		bfin_write_FIO_DIR(bfin_read_FIO_DIR() | j);
		bfin_write_FIO_FLAG_S(j);
	}
	spi_register_board_info(bfin_spi_board_info, ARRAY_SIZE(bfin_spi_board_info));
#endif

	return 0;
}

arch_initcall(ip0x_init);

static struct platform_device *ip0x_early_devices[] __initdata = {
#if defined(CONFIG_SERIAL_BFIN_CONSOLE) || defined(CONFIG_EARLY_PRINTK)
#ifdef CONFIG_SERIAL_BFIN_UART0
	&bfin_uart0_device,
#endif
#endif
};

void __init native_machine_early_platform_add_devices(void)
{
	printk(KERN_INFO "register early platform devices\n");
	early_platform_add_devices(ip0x_early_devices,
		ARRAY_SIZE(ip0x_early_devices));
}
