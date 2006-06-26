/*
 *  arch/mips/emma2rh/markeins/platofrm.c
 *      This file sets up platform devices for EMMA2RH Mark-eins.
 *
 *  Copyright(C) MontaVista Software Inc, 2006
 *
 *  Author: dmitry pervushin <dpervushin@ru.mvista.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include <linux/config.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/ioport.h>
#include <linux/serial_8250.h>
#include <linux/mtd/physmap.h>

#include <asm/cpu.h>
#include <asm/bootinfo.h>
#include <asm/addrspace.h>
#include <asm/time.h>
#include <asm/bcache.h>
#include <asm/irq.h>
#include <asm/reboot.h>
#include <asm/gdb-stub.h>
#include <asm/traps.h>
#include <asm/debug.h>

#include <asm/emma2rh/emma2rh.h>


#define I2C_EMMA2RH "emma2rh-iic" /* must be in sync with IIC driver */

static struct resource i2c_emma_resources_0[] = {
	{ NULL, EMMA2RH_IRQ_PIIC0, EMMA2RH_IRQ_PIIC0, IORESOURCE_IRQ },
	{ NULL, KSEG1ADDR(EMMA2RH_PIIC0_BASE), KSEG1ADDR(EMMA2RH_PIIC0_BASE + 0x1000), 0 },
};

struct resource i2c_emma_resources_1[] = {
	{ NULL, EMMA2RH_IRQ_PIIC1, EMMA2RH_IRQ_PIIC1, IORESOURCE_IRQ },
	{ NULL, KSEG1ADDR(EMMA2RH_PIIC1_BASE), KSEG1ADDR(EMMA2RH_PIIC1_BASE + 0x1000), 0 },
};

struct resource i2c_emma_resources_2[] = {
	{ NULL, EMMA2RH_IRQ_PIIC2, EMMA2RH_IRQ_PIIC2, IORESOURCE_IRQ },
	{ NULL, KSEG1ADDR(EMMA2RH_PIIC2_BASE), KSEG1ADDR(EMMA2RH_PIIC2_BASE + 0x1000), 0 },
};

struct platform_device i2c_emma_devices[] = {
	[0] = {
		.name = I2C_EMMA2RH,
		.id = 0,
		.resource = i2c_emma_resources_0,
		.num_resources = ARRAY_SIZE(i2c_emma_resources_0),
	},
	[1] = {
		.name = I2C_EMMA2RH,
		.id = 1,
		.resource = i2c_emma_resources_1,
		.num_resources = ARRAY_SIZE(i2c_emma_resources_1),
	},
	[2] = {
		.name = I2C_EMMA2RH,
		.id = 2,
		.resource = i2c_emma_resources_2,
		.num_resources = ARRAY_SIZE(i2c_emma_resources_2),
	},
};

#define EMMA2RH_SERIAL_CLOCK 18544000
#define EMMA2RH_SERIAL_FLAGS UPF_BOOT_AUTOCONF | UPF_SKIP_TEST

static struct  plat_serial8250_port platform_serial_ports[] = {
       [0] = {
         .membase = (void __iomem*)KSEG1ADDR(EMMA2RH_PFUR0_BASE + 3),
         .irq = EMMA2RH_IRQ_PFUR0,
         .uartclk = EMMA2RH_SERIAL_CLOCK,
         .regshift = 4,
         .iotype = UPIO_MEM,
         .flags = EMMA2RH_SERIAL_FLAGS,
       },
       [1] = {
         .membase = (void __iomem*)KSEG1ADDR(EMMA2RH_PFUR1_BASE + 3),
         .irq = EMMA2RH_IRQ_PFUR1,
         .uartclk = EMMA2RH_SERIAL_CLOCK,
         .regshift = 4,
         .iotype = UPIO_MEM,
         .flags = EMMA2RH_SERIAL_FLAGS,
       },
       [2] = {
         .membase = (void __iomem*)KSEG1ADDR(EMMA2RH_PFUR2_BASE + 3),
         .irq = EMMA2RH_IRQ_PFUR2,
         .uartclk = EMMA2RH_SERIAL_CLOCK,
         .regshift = 4,
         .iotype = UPIO_MEM,
         .flags = EMMA2RH_SERIAL_FLAGS,
       },
       [3] = {
	 .flags = 0,
       },
};

static struct  platform_device serial_emma = {
	.name = "serial8250",
	.dev = {
		.platform_data = &platform_serial_ports,
	},
};

static struct platform_device *devices[] = {
	&i2c_emma_devices[0],
	&i2c_emma_devices[1],
	&i2c_emma_devices[2],
	&serial_emma,
};

static struct mtd_partition markeins_parts[] = {
	[0] = {
		.name = "RootFS",
		.offset = 0x00000000,
		.size = 0x00c00000,
	},
	[1] = {
		.name = "boot code area",
		.offset = MTDPART_OFS_APPEND,
		.size = 0x00100000,
	},
	[2] = {
		.name = "kernel image",
		.offset = MTDPART_OFS_APPEND,
		.size = 0x00300000,
	},
	[3] = {
		.name = "RootFS2",
		.offset = MTDPART_OFS_APPEND,
		.size = 0x00c00000,
	},
	[4] = {
		.name = "boot code area2",
		.offset = MTDPART_OFS_APPEND,
		.size = 0x00100000,
	},
	[5] = {
		.name = "kernel image2",
		.offset = MTDPART_OFS_APPEND,
		.size = MTDPART_SIZ_FULL,
	},
};

static int __init platform_devices_setup(void)
{
	physmap_set_partitions(markeins_parts, ARRAY_SIZE(markeins_parts));
	return platform_add_devices(devices, ARRAY_SIZE(devices));
}

arch_initcall(platform_devices_setup);

