/*
 * Renesas Europe EDOSK7760 Board Support
 *
 * Copyright (C) 2008 SPES Societa' Progettazione Elettronica e Software Ltd.
 * Author: Luca Santini <luca.santini@spesonline.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#include <linux/init.h>
#include <linux/types.h>
#include <linux/platform_device.h>
#include <linux/smc91x.h>
#include <linux/interrupt.h>
#include <linux/sh_intc.h>
#include <linux/i2c.h>
#include <linux/mtd/physmap.h>
#include <asm/machvec.h>
#include <asm/io.h>
#include <asm/addrspace.h>
#include <asm/delay.h>
#include <asm/i2c-sh7760.h>
#include <asm/sizes.h>

/* Bus state controller registers for CS4 area */
#define BSC_CS4BCR	0xA4FD0010
#define BSC_CS4WCR	0xA4FD0030

#define SMC_IOBASE	0xA2000000
#define SMC_IO_OFFSET	0x300
#define SMC_IOADDR	(SMC_IOBASE + SMC_IO_OFFSET)

/* NOR flash */
static struct mtd_partition edosk7760_nor_flash_partitions[] = {
	{
		.name = "bootloader",
		.offset = 0,
		.size = SZ_256K,
		.mask_flags = MTD_WRITEABLE,	/* Read-only */
	}, {
		.name = "kernel",
		.offset = MTDPART_OFS_APPEND,
		.size = SZ_2M,
	}, {
		.name = "fs",
		.offset = MTDPART_OFS_APPEND,
		.size = (26 << 20),
	}, {
		.name = "other",
		.offset = MTDPART_OFS_APPEND,
		.size = MTDPART_SIZ_FULL,
	},
};

static struct physmap_flash_data edosk7760_nor_flash_data = {
	.width		= 4,
	.parts		= edosk7760_nor_flash_partitions,
	.nr_parts	= ARRAY_SIZE(edosk7760_nor_flash_partitions),
};

static struct resource edosk7760_nor_flash_resources[] = {
	[0] = {
		.name	= "NOR Flash",
		.start	= 0x00000000,
		.end	= 0x00000000 + SZ_32M - 1,
		.flags	= IORESOURCE_MEM,
	}
};

static struct platform_device edosk7760_nor_flash_device = {
	.name		= "physmap-flash",
	.resource	= edosk7760_nor_flash_resources,
	.num_resources	= ARRAY_SIZE(edosk7760_nor_flash_resources),
	.dev		= {
		.platform_data = &edosk7760_nor_flash_data,
	},
};

/* i2c initialization functions */
static struct sh7760_i2c_platdata i2c_pd = {
	.speed_khz	= 400,
};

static struct resource sh7760_i2c1_res[] = {
	{
		.start	= SH7760_I2C1_MMIO,
		.end	= SH7760_I2C1_MMIOEND,
		.flags	= IORESOURCE_MEM,
	},{
		.start	= evt2irq(0x9e0),
		.end	= evt2irq(0x9e0),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device sh7760_i2c1_dev = {
	.dev    = {
		.platform_data	= &i2c_pd,
	},

	.name		= SH7760_I2C_DEVNAME,
	.id		= 1,
	.resource	= sh7760_i2c1_res,
	.num_resources	= ARRAY_SIZE(sh7760_i2c1_res),
};

static struct resource sh7760_i2c0_res[] = {
	{
		.start	= SH7760_I2C0_MMIO,
		.end	= SH7760_I2C0_MMIOEND,
		.flags	= IORESOURCE_MEM,
	}, {
		.start	= evt2irq(0x9c0),
		.end	= evt2irq(0x9c0),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device sh7760_i2c0_dev = {
	.dev    = {
		.platform_data	= &i2c_pd,
	},
	.name		= SH7760_I2C_DEVNAME,
	.id		= 0,
	.resource	= sh7760_i2c0_res,
	.num_resources	= ARRAY_SIZE(sh7760_i2c0_res),
};

/* eth initialization functions */
static struct smc91x_platdata smc91x_info = {
	.flags = SMC91X_USE_16BIT | SMC91X_IO_SHIFT_1 | IORESOURCE_IRQ_LOWLEVEL,
};

static struct resource smc91x_res[] = {
	[0] = {
		.start	= SMC_IOADDR,
		.end	= SMC_IOADDR + SZ_32 - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= evt2irq(0x2a0),
		.end	= evt2irq(0x2a0),
		.flags	= IORESOURCE_IRQ ,
	}
};

static struct platform_device smc91x_dev = {
	.name		= "smc91x",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(smc91x_res),
	.resource	= smc91x_res,

	.dev	= {
		.platform_data	= &smc91x_info,
	},
};

/* platform init code */
static struct platform_device *edosk7760_devices[] __initdata = {
	&smc91x_dev,
	&edosk7760_nor_flash_device,
	&sh7760_i2c0_dev,
	&sh7760_i2c1_dev,
};

static int __init init_edosk7760_devices(void)
{
	plat_irq_setup_pins(IRQ_MODE_IRQ);

	return platform_add_devices(edosk7760_devices,
				    ARRAY_SIZE(edosk7760_devices));
}
device_initcall(init_edosk7760_devices);

/*
 * The Machine Vector
 */
struct sh_machine_vector mv_edosk7760 __initmv = {
	.mv_name	= "EDOSK7760",
};
