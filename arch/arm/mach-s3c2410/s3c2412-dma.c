/* linux/arch/arm/mach-s3c2410/s3c2412-dma.c
 *
 * (c) 2006 Simtec Electronics
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
#include <linux/sysdev.h>

#include <asm/dma.h>
#include <asm/arch/dma.h>
#include <asm/io.h>

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

#define MAP(x) { (x)| DMA_CH_VALID, (x)| DMA_CH_VALID, (x)| DMA_CH_VALID, (x)| DMA_CH_VALID }

static struct s3c24xx_dma_map __initdata s3c2412_dma_mappings[] = {
	[DMACH_XD0] = {
		.name		= "xdreq0",
		.channels	= MAP(S3C2412_DMAREQSEL_XDREQ0),
	},
	[DMACH_XD1] = {
		.name		= "xdreq1",
		.channels	= MAP(S3C2412_DMAREQSEL_XDREQ1),
	},
	[DMACH_SDI] = {
		.name		= "sdi",
		.channels	= MAP(S3C2412_DMAREQSEL_SDI),
		.hw_addr.to	= S3C2410_PA_IIS + S3C2410_IISFIFO,
		.hw_addr.from	= S3C2410_PA_IIS + S3C2410_IISFIFO,
	},
	[DMACH_SPI0] = {
		.name		= "spi0",
		.channels	= MAP(S3C2412_DMAREQSEL_SPI0TX),
		.hw_addr.to	= S3C2410_PA_SPI + S3C2410_SPTDAT,
		.hw_addr.from	= S3C2410_PA_SPI + S3C2410_SPRDAT,
	},
	[DMACH_SPI1] = {
		.name		= "spi1",
		.channels	= MAP(S3C2412_DMAREQSEL_SPI1TX),
		.hw_addr.to	= S3C2410_PA_SPI + 0x20 + S3C2410_SPTDAT,
		.hw_addr.from	= S3C2410_PA_SPI + 0x20 + S3C2410_SPRDAT,
	},
	[DMACH_UART0] = {
		.name		= "uart0",
		.channels	= MAP(S3C2412_DMAREQSEL_UART0_0),
		.hw_addr.to	= S3C2410_PA_UART0 + S3C2410_UTXH,
		.hw_addr.from	= S3C2410_PA_UART0 + S3C2410_URXH,
	},
	[DMACH_UART1] = {
		.name		= "uart1",
		.channels	= MAP(S3C2412_DMAREQSEL_UART1_0),
		.hw_addr.to	= S3C2410_PA_UART1 + S3C2410_UTXH,
		.hw_addr.from	= S3C2410_PA_UART1 + S3C2410_URXH,
	},
      	[DMACH_UART2] = {
		.name		= "uart2",
		.channels	= MAP(S3C2412_DMAREQSEL_UART2_0),
		.hw_addr.to	= S3C2410_PA_UART2 + S3C2410_UTXH,
		.hw_addr.from	= S3C2410_PA_UART2 + S3C2410_URXH,
	},
	[DMACH_UART0_SRC2] = {
		.name		= "uart0",
		.channels	= MAP(S3C2412_DMAREQSEL_UART0_1),
		.hw_addr.to	= S3C2410_PA_UART0 + S3C2410_UTXH,
		.hw_addr.from	= S3C2410_PA_UART0 + S3C2410_URXH,
	},
	[DMACH_UART1_SRC2] = {
		.name		= "uart1",
		.channels	= MAP(S3C2412_DMAREQSEL_UART1_1),
		.hw_addr.to	= S3C2410_PA_UART1 + S3C2410_UTXH,
		.hw_addr.from	= S3C2410_PA_UART1 + S3C2410_URXH,
	},
      	[DMACH_UART2_SRC2] = {
		.name		= "uart2",
		.channels	= MAP(S3C2412_DMAREQSEL_UART2_1),
		.hw_addr.to	= S3C2410_PA_UART2 + S3C2410_UTXH,
		.hw_addr.from	= S3C2410_PA_UART2 + S3C2410_URXH,
	},
	[DMACH_TIMER] = {
		.name		= "timer",
		.channels	= MAP(S3C2412_DMAREQSEL_TIMER),
	},
	[DMACH_I2S_IN] = {
		.name		= "i2s-sdi",
		.channels	= MAP(S3C2412_DMAREQSEL_I2SRX),
		.hw_addr.from	= S3C2410_PA_IIS + S3C2410_IISFIFO,
	},
	[DMACH_I2S_OUT] = {
		.name		= "i2s-sdo",
		.channels	= MAP(S3C2412_DMAREQSEL_I2STX),
		.hw_addr.to	= S3C2410_PA_IIS + S3C2410_IISFIFO,
	},
	[DMACH_USB_EP1] = {
		.name		= "usb-ep1",
		.channels	= MAP(S3C2412_DMAREQSEL_USBEP1),
	},
	[DMACH_USB_EP2] = {
		.name		= "usb-ep2",
		.channels	= MAP(S3C2412_DMAREQSEL_USBEP2),
	},
	[DMACH_USB_EP3] = {
		.name		= "usb-ep3",
		.channels	= MAP(S3C2412_DMAREQSEL_USBEP3),
	},
	[DMACH_USB_EP4] = {
		.name		= "usb-ep4",
		.channels	= MAP(S3C2412_DMAREQSEL_USBEP4),
	},
};

static void s3c2412_dma_select(struct s3c2410_dma_chan *chan,
			       struct s3c24xx_dma_map *map)
{
	writel(chan->regs + S3C2412_DMA_DMAREQSEL,
	       map->channels[0] | S3C2412_DMAREQSEL_HW);
}

static struct s3c24xx_dma_selection __initdata s3c2412_dma_sel = {
	.select		= s3c2412_dma_select,
	.dcon_mask	= 0,
	.map		= s3c2412_dma_mappings,
	.map_size	= ARRAY_SIZE(s3c2412_dma_mappings),
};

static int s3c2412_dma_add(struct sys_device *sysdev)
{
	return s3c24xx_dma_init_map(&s3c2412_dma_sel);
}

static struct sysdev_driver s3c2412_dma_driver = {
	.add	= s3c2412_dma_add,
};

static int __init s3c2412_dma_init(void)
{
	return sysdev_driver_register(&s3c2412_sysclass, &s3c2412_dma_driver);
}

arch_initcall(s3c2412_dma_init);
