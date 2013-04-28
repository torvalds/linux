/* linux/arch/arm/mach-s3c2412/dma.c
 *
 * Copyright (c) 2006 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * S3C2412 DMA selection
 *
 * http://armlinux.simtec.co.uk/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/serial_core.h>
#include <linux/io.h>

#include <mach/dma.h>

#include <plat/dma-s3c24xx.h>
#include <plat/cpu.h>

#include <plat/regs-serial.h>
#include <mach/regs-gpio.h>
#include <plat/regs-ac97.h>
#include <plat/regs-dma.h>
#include <mach/regs-lcd.h>
#include <plat/regs-iis.h>
#include <plat/regs-spi.h>

#define MAP(x) { (x)| DMA_CH_VALID, (x)| DMA_CH_VALID, (x)| DMA_CH_VALID, (x)| DMA_CH_VALID }

static struct s3c24xx_dma_map __initdata s3c2412_dma_mappings[] = {
	[DMACH_XD0] = {
		.name		= "xdreq0",
		.channels	= MAP(S3C2412_DMAREQSEL_XDREQ0),
		.channels_rx	= MAP(S3C2412_DMAREQSEL_XDREQ0),
	},
	[DMACH_XD1] = {
		.name		= "xdreq1",
		.channels	= MAP(S3C2412_DMAREQSEL_XDREQ1),
		.channels_rx	= MAP(S3C2412_DMAREQSEL_XDREQ1),
	},
	[DMACH_SDI] = {
		.name		= "sdi",
		.channels	= MAP(S3C2412_DMAREQSEL_SDI),
		.channels_rx	= MAP(S3C2412_DMAREQSEL_SDI),
	},
	[DMACH_SPI0] = {
		.name		= "spi0",
		.channels	= MAP(S3C2412_DMAREQSEL_SPI0TX),
		.channels_rx	= MAP(S3C2412_DMAREQSEL_SPI0RX),
	},
	[DMACH_SPI1] = {
		.name		= "spi1",
		.channels	= MAP(S3C2412_DMAREQSEL_SPI1TX),
		.channels_rx	= MAP(S3C2412_DMAREQSEL_SPI1RX),
	},
	[DMACH_UART0] = {
		.name		= "uart0",
		.channels	= MAP(S3C2412_DMAREQSEL_UART0_0),
		.channels_rx	= MAP(S3C2412_DMAREQSEL_UART0_0),
	},
	[DMACH_UART1] = {
		.name		= "uart1",
		.channels	= MAP(S3C2412_DMAREQSEL_UART1_0),
		.channels_rx	= MAP(S3C2412_DMAREQSEL_UART1_0),
	},
      	[DMACH_UART2] = {
		.name		= "uart2",
		.channels	= MAP(S3C2412_DMAREQSEL_UART2_0),
		.channels_rx	= MAP(S3C2412_DMAREQSEL_UART2_0),
	},
	[DMACH_UART0_SRC2] = {
		.name		= "uart0",
		.channels	= MAP(S3C2412_DMAREQSEL_UART0_1),
		.channels_rx	= MAP(S3C2412_DMAREQSEL_UART0_1),
	},
	[DMACH_UART1_SRC2] = {
		.name		= "uart1",
		.channels	= MAP(S3C2412_DMAREQSEL_UART1_1),
		.channels_rx	= MAP(S3C2412_DMAREQSEL_UART1_1),
	},
      	[DMACH_UART2_SRC2] = {
		.name		= "uart2",
		.channels	= MAP(S3C2412_DMAREQSEL_UART2_1),
		.channels_rx	= MAP(S3C2412_DMAREQSEL_UART2_1),
	},
	[DMACH_TIMER] = {
		.name		= "timer",
		.channels	= MAP(S3C2412_DMAREQSEL_TIMER),
		.channels_rx	= MAP(S3C2412_DMAREQSEL_TIMER),
	},
	[DMACH_I2S_IN] = {
		.name		= "i2s-sdi",
		.channels	= MAP(S3C2412_DMAREQSEL_I2SRX),
		.channels_rx	= MAP(S3C2412_DMAREQSEL_I2SRX),
	},
	[DMACH_I2S_OUT] = {
		.name		= "i2s-sdo",
		.channels	= MAP(S3C2412_DMAREQSEL_I2STX),
		.channels_rx	= MAP(S3C2412_DMAREQSEL_I2STX),
	},
	[DMACH_USB_EP1] = {
		.name		= "usb-ep1",
		.channels	= MAP(S3C2412_DMAREQSEL_USBEP1),
		.channels_rx	= MAP(S3C2412_DMAREQSEL_USBEP1),
	},
	[DMACH_USB_EP2] = {
		.name		= "usb-ep2",
		.channels	= MAP(S3C2412_DMAREQSEL_USBEP2),
		.channels_rx	= MAP(S3C2412_DMAREQSEL_USBEP2),
	},
	[DMACH_USB_EP3] = {
		.name		= "usb-ep3",
		.channels	= MAP(S3C2412_DMAREQSEL_USBEP3),
		.channels_rx	= MAP(S3C2412_DMAREQSEL_USBEP3),
	},
	[DMACH_USB_EP4] = {
		.name		= "usb-ep4",
		.channels	= MAP(S3C2412_DMAREQSEL_USBEP4),
		.channels_rx	= MAP(S3C2412_DMAREQSEL_USBEP4),
	},
};

static void s3c2412_dma_direction(struct s3c2410_dma_chan *chan,
				  struct s3c24xx_dma_map *map,
				  enum dma_data_direction dir)
{
	unsigned long chsel;

	if (dir == DMA_FROM_DEVICE)
		chsel = map->channels_rx[0];
	else
		chsel = map->channels[0];

	chsel &= ~DMA_CH_VALID;
	chsel |= S3C2412_DMAREQSEL_HW;

	writel(chsel, chan->regs + S3C2412_DMA_DMAREQSEL);
}

static void s3c2412_dma_select(struct s3c2410_dma_chan *chan,
			       struct s3c24xx_dma_map *map)
{
	s3c2412_dma_direction(chan, map, chan->source);
}

static struct s3c24xx_dma_selection __initdata s3c2412_dma_sel = {
	.select		= s3c2412_dma_select,
	.direction	= s3c2412_dma_direction,
	.dcon_mask	= 0,
	.map		= s3c2412_dma_mappings,
	.map_size	= ARRAY_SIZE(s3c2412_dma_mappings),
};

static int __init s3c2412_dma_add(struct device *dev,
				  struct subsys_interface *sif)
{
	s3c2410_dma_init();
	return s3c24xx_dma_init_map(&s3c2412_dma_sel);
}

static struct subsys_interface s3c2412_dma_interface = {
	.name		= "s3c2412_dma",
	.subsys		= &s3c2412_subsys,
	.add_dev	= s3c2412_dma_add,
};

static int __init s3c2412_dma_init(void)
{
	return subsys_interface_register(&s3c2412_dma_interface);
}

arch_initcall(s3c2412_dma_init);
