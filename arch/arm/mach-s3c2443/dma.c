/* linux/arch/arm/mach-s3c2443/dma.c
 *
 * Copyright (c) 2007 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * S3C2443 DMA selection
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
#include <mach/regs-mem.h>
#include <mach/regs-lcd.h>
#include <mach/regs-sdi.h>
#include <plat/regs-iis.h>
#include <plat/regs-spi.h>

#define MAP(x) { \
		[0]	= (x) | DMA_CH_VALID,	\
		[1]	= (x) | DMA_CH_VALID,	\
		[2]	= (x) | DMA_CH_VALID,	\
		[3]	= (x) | DMA_CH_VALID,	\
		[4]	= (x) | DMA_CH_VALID,	\
		[5]     = (x) | DMA_CH_VALID,	\
	}

static struct s3c24xx_dma_map __initdata s3c2443_dma_mappings[] = {
	[DMACH_XD0] = {
		.name		= "xdreq0",
		.channels	= MAP(S3C2443_DMAREQSEL_XDREQ0),
	},
	[DMACH_XD1] = {
		.name		= "xdreq1",
		.channels	= MAP(S3C2443_DMAREQSEL_XDREQ1),
	},
	[DMACH_SDI] = {
		.name		= "sdi",
		.channels	= MAP(S3C2443_DMAREQSEL_SDI),
	},
	[DMACH_SPI0] = {
		.name		= "spi0",
		.channels	= MAP(S3C2443_DMAREQSEL_SPI0TX),
	},
	[DMACH_SPI1] = {
		.name		= "spi1",
		.channels	= MAP(S3C2443_DMAREQSEL_SPI1TX),
	},
	[DMACH_UART0] = {
		.name		= "uart0",
		.channels	= MAP(S3C2443_DMAREQSEL_UART0_0),
	},
	[DMACH_UART1] = {
		.name		= "uart1",
		.channels	= MAP(S3C2443_DMAREQSEL_UART1_0),
	},
      	[DMACH_UART2] = {
		.name		= "uart2",
		.channels	= MAP(S3C2443_DMAREQSEL_UART2_0),
	},
      	[DMACH_UART3] = {
		.name		= "uart3",
		.channels	= MAP(S3C2443_DMAREQSEL_UART3_0),
	},
	[DMACH_UART0_SRC2] = {
		.name		= "uart0",
		.channels	= MAP(S3C2443_DMAREQSEL_UART0_1),
	},
	[DMACH_UART1_SRC2] = {
		.name		= "uart1",
		.channels	= MAP(S3C2443_DMAREQSEL_UART1_1),
	},
      	[DMACH_UART2_SRC2] = {
		.name		= "uart2",
		.channels	= MAP(S3C2443_DMAREQSEL_UART2_1),
	},
      	[DMACH_UART3_SRC2] = {
		.name		= "uart3",
		.channels	= MAP(S3C2443_DMAREQSEL_UART3_1),
	},
	[DMACH_TIMER] = {
		.name		= "timer",
		.channels	= MAP(S3C2443_DMAREQSEL_TIMER),
	},
	[DMACH_I2S_IN] = {
		.name		= "i2s-sdi",
		.channels	= MAP(S3C2443_DMAREQSEL_I2SRX),
	},
	[DMACH_I2S_OUT] = {
		.name		= "i2s-sdo",
		.channels	= MAP(S3C2443_DMAREQSEL_I2STX),
	},
	[DMACH_PCM_IN] = {
		.name		= "pcm-in",
		.channels	= MAP(S3C2443_DMAREQSEL_PCMIN),
	},
	[DMACH_PCM_OUT] = {
		.name		= "pcm-out",
		.channels	= MAP(S3C2443_DMAREQSEL_PCMOUT),
	},
	[DMACH_MIC_IN] = {
		.name		= "mic-in",
		.channels	= MAP(S3C2443_DMAREQSEL_MICIN),
	},
};

static void s3c2443_dma_select(struct s3c2410_dma_chan *chan,
			       struct s3c24xx_dma_map *map)
{
	writel(map->channels[0] | S3C2443_DMAREQSEL_HW,
	       chan->regs + S3C2443_DMA_DMAREQSEL);
}

static struct s3c24xx_dma_selection __initdata s3c2443_dma_sel = {
	.select		= s3c2443_dma_select,
	.dcon_mask	= 0,
	.map		= s3c2443_dma_mappings,
	.map_size	= ARRAY_SIZE(s3c2443_dma_mappings),
};

static int __init s3c2443_dma_add(struct device *dev,
				  struct subsys_interface *sif)
{
	s3c24xx_dma_init(6, IRQ_S3C2443_DMA0, 0x100);
	return s3c24xx_dma_init_map(&s3c2443_dma_sel);
}

static struct subsys_interface s3c2443_dma_interface = {
	.name		= "s3c2443_dma",
	.subsys		= &s3c2443_subsys,
	.add_dev	= s3c2443_dma_add,
};

static int __init s3c2443_dma_init(void)
{
	return subsys_interface_register(&s3c2443_dma_interface);
}

arch_initcall(s3c2443_dma_init);
