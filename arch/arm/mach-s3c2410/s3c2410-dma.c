/* linux/arch/arm/mach-s3c2410/s3c2410-dma.c
 *
 * Copyright (c) 2006 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * S3C2410 DMA selection
 *
 * http://armlinux.simtec.co.uk/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/sysdev.h>
#include <linux/serial_core.h>

#include <asm/dma.h>
#include <asm/arch/dma.h>
#include "dma.h"

#include "cpu.h"

#include <asm/arch/regs-serial.h>
#include <asm/arch/regs-gpio.h>
#include <asm/arch/regs-ac97.h>
#include <asm/arch/regs-mem.h>
#include <asm/arch/regs-lcd.h>
#include <asm/arch/regs-sdi.h>
#include <asm/arch/regs-iis.h>
#include <asm/arch/regs-spi.h>

static struct s3c24xx_dma_map __initdata s3c2410_dma_mappings[] = {
	[DMACH_XD0] = {
		.name		= "xdreq0",
		.channels[0]	= S3C2410_DCON_CH0_XDREQ0 | DMA_CH_VALID,
	},
	[DMACH_XD1] = {
		.name		= "xdreq1",
		.channels[1]	= S3C2410_DCON_CH1_XDREQ1 | DMA_CH_VALID,
	},
	[DMACH_SDI] = {
		.name		= "sdi",
		.channels[0]	= S3C2410_DCON_CH0_SDI | DMA_CH_VALID,
		.channels[2]	= S3C2410_DCON_CH2_SDI | DMA_CH_VALID,
		.channels[3]	= S3C2410_DCON_CH3_SDI | DMA_CH_VALID,
		.hw_addr.to	= S3C2410_PA_IIS + S3C2410_IISFIFO,
		.hw_addr.from	= S3C2410_PA_IIS + S3C2410_IISFIFO,
	},
	[DMACH_SPI0] = {
		.name		= "spi0",
		.channels[1]	= S3C2410_DCON_CH1_SPI | DMA_CH_VALID,
		.hw_addr.to	= S3C2410_PA_SPI + S3C2410_SPTDAT,
		.hw_addr.from	= S3C2410_PA_SPI + S3C2410_SPRDAT,
	},
	[DMACH_SPI1] = {
		.name		= "spi1",
		.channels[3]	= S3C2410_DCON_CH3_SPI | DMA_CH_VALID,
		.hw_addr.to	= S3C2410_PA_SPI + 0x20 + S3C2410_SPTDAT,
		.hw_addr.from	= S3C2410_PA_SPI + 0x20 + S3C2410_SPRDAT,
	},
	[DMACH_UART0] = {
		.name		= "uart0",
		.channels[0]	= S3C2410_DCON_CH0_UART0 | DMA_CH_VALID,
		.hw_addr.to	= S3C2410_PA_UART0 + S3C2410_UTXH,
		.hw_addr.from	= S3C2410_PA_UART0 + S3C2410_URXH,
	},
	[DMACH_UART1] = {
		.name		= "uart1",
		.channels[1]	= S3C2410_DCON_CH1_UART1 | DMA_CH_VALID,
		.hw_addr.to	= S3C2410_PA_UART1 + S3C2410_UTXH,
		.hw_addr.from	= S3C2410_PA_UART1 + S3C2410_URXH,
	},
      	[DMACH_UART2] = {
		.name		= "uart2",
		.channels[3]	= S3C2410_DCON_CH3_UART2 | DMA_CH_VALID,
		.hw_addr.to	= S3C2410_PA_UART2 + S3C2410_UTXH,
		.hw_addr.from	= S3C2410_PA_UART2 + S3C2410_URXH,
	},
	[DMACH_TIMER] = {
		.name		= "timer",
		.channels[0]	= S3C2410_DCON_CH0_TIMER | DMA_CH_VALID,
		.channels[2]	= S3C2410_DCON_CH2_TIMER | DMA_CH_VALID,
		.channels[3]	= S3C2410_DCON_CH3_TIMER | DMA_CH_VALID,
	},
	[DMACH_I2S_IN] = {
		.name		= "i2s-sdi",
		.channels[1]	= S3C2410_DCON_CH1_I2SSDI | DMA_CH_VALID,
		.channels[2]	= S3C2410_DCON_CH2_I2SSDI | DMA_CH_VALID,
		.hw_addr.from	= S3C2410_PA_IIS + S3C2410_IISFIFO,
	},
	[DMACH_I2S_OUT] = {
		.name		= "i2s-sdo",
		.channels[2]	= S3C2410_DCON_CH2_I2SSDO | DMA_CH_VALID,
		.hw_addr.to	= S3C2410_PA_IIS + S3C2410_IISFIFO,
	},
	[DMACH_USB_EP1] = {
		.name		= "usb-ep1",
		.channels[0]	= S3C2410_DCON_CH0_USBEP1 | DMA_CH_VALID,
	},
	[DMACH_USB_EP2] = {
		.name		= "usb-ep2",
		.channels[1]	= S3C2410_DCON_CH1_USBEP2 | DMA_CH_VALID,
	},
	[DMACH_USB_EP3] = {
		.name		= "usb-ep3",
		.channels[2]	= S3C2410_DCON_CH2_USBEP3 | DMA_CH_VALID,
	},
	[DMACH_USB_EP4] = {
		.name		= "usb-ep4",
		.channels[3]	=S3C2410_DCON_CH3_USBEP4 | DMA_CH_VALID,
	},
};

static void s3c2410_dma_select(struct s3c2410_dma_chan *chan,
			       struct s3c24xx_dma_map *map)
{
	chan->dcon = map->channels[chan->number] & ~DMA_CH_VALID;
}

static struct s3c24xx_dma_selection __initdata s3c2410_dma_sel = {
	.select		= s3c2410_dma_select,
	.dcon_mask	= 7 << 24,
	.map		= s3c2410_dma_mappings,
	.map_size	= ARRAY_SIZE(s3c2410_dma_mappings),
};

static int s3c2410_dma_add(struct sys_device *sysdev)
{
	return s3c24xx_dma_init_map(&s3c2410_dma_sel);
}

#if defined(CONFIG_CPU_S3C2410)
static struct sysdev_driver s3c2410_dma_driver = {
	.add	= s3c2410_dma_add,
};

static int __init s3c2410_dma_init(void)
{
	return sysdev_driver_register(&s3c2410_sysclass, &s3c2410_dma_driver);
}

arch_initcall(s3c2410_dma_init);
#endif

#if defined(CONFIG_CPU_S3C2442)
/* S3C2442 DMA contains the same selection table as the S3C2410 */
static struct sysdev_driver s3c2442_dma_driver = {
	.add	= s3c2410_dma_add,
};

static int __init s3c2442_dma_init(void)
{
	return sysdev_driver_register(&s3c2442_sysclass, &s3c2442_dma_driver);
}

arch_initcall(s3c2442_dma_init);
#endif

