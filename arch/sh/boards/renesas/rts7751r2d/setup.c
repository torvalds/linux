/*
 * Renesas Technology Sales RTS7751R2D Support.
 *
 * Copyright (C) 2002 - 2006 Atom Create Engineering Co., Ltd.
 * Copyright (C) 2004 - 2007 Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/pata_platform.h>
#include <linux/serial_8250.h>
#include <linux/sm501.h>
#include <linux/pm.h>
#include <asm/machvec.h>
#include <asm/rts7751r2d.h>
#include <asm/voyagergx.h>
#include <asm/io.h>

static void __init voyagergx_serial_init(void)
{
	unsigned long val;

	/*
	 * GPIO Control
	 */
	val = readl((void __iomem *)GPIO_MUX_HIGH);
	val |= 0x00001fe0;
	writel(val, (void __iomem *)GPIO_MUX_HIGH);

	/*
	 * Power Mode Gate
	 */
	val = readl((void __iomem *)POWER_MODE0_GATE);
	val |= (POWER_MODE0_GATE_U0 | POWER_MODE0_GATE_U1);
	writel(val, (void __iomem *)POWER_MODE0_GATE);

	val = readl((void __iomem *)POWER_MODE1_GATE);
	val |= (POWER_MODE1_GATE_U0 | POWER_MODE1_GATE_U1);
	writel(val, (void __iomem *)POWER_MODE1_GATE);
}

static struct resource cf_ide_resources[] = {
	[0] = {
		.start	= PA_AREA5_IO + 0x1000,
		.end	= PA_AREA5_IO + 0x1000 + 0x10 - 0x2,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= PA_AREA5_IO + 0x80c,
		.end	= PA_AREA5_IO + 0x80c,
		.flags	= IORESOURCE_MEM,
	},
#ifndef CONFIG_RTS7751R2D_1 /* For R2D-1 polling is preferred */
	[2] = {
		.start	= IRQ_CF_IDE,
		.flags	= IORESOURCE_IRQ,
	},
#endif
};

static struct pata_platform_info pata_info = {
	.ioport_shift	= 1,
};

static struct platform_device cf_ide_device  = {
	.name		= "pata_platform",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(cf_ide_resources),
	.resource	= cf_ide_resources,
	.dev	= {
		.platform_data	= &pata_info,
	},
};

static struct resource heartbeat_resources[] = {
	[0] = {
		.start	= PA_OUTPORT,
		.end	= PA_OUTPORT,
		.flags	= IORESOURCE_MEM,
	},
};

static struct platform_device heartbeat_device = {
	.name		= "heartbeat",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(heartbeat_resources),
	.resource	= heartbeat_resources,
};

#ifdef CONFIG_MFD_SM501
static struct plat_serial8250_port uart_platform_data[] = {
	{
		.membase	= (void __iomem *)VOYAGER_UART_BASE,
		.mapbase	= VOYAGER_UART_BASE,
		.iotype		= UPIO_MEM,
		.irq		= IRQ_SM501_U0,
		.flags		= UPF_BOOT_AUTOCONF | UPF_SKIP_TEST,
		.regshift	= 2,
		.uartclk	= (9600 * 16),
	},
	{ 0 },
};

static struct platform_device uart_device = {
	.name		= "serial8250",
	.id		= PLAT8250_DEV_PLATFORM,
	.dev		= {
		.platform_data	= uart_platform_data,
	},
};

static struct resource sm501_resources[] = {
	[0]	= {
		.start	= 0x10000000,
		.end	= 0x13e00000 - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1]	= {
		.start	= 0x13e00000,
		.end	= 0x13ffffff,
		.flags	= IORESOURCE_MEM,
	},
	[2]	= {
		.start	= IRQ_SM501_CV,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device sm501_device = {
	.name		= "sm501",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(sm501_resources),
	.resource	= sm501_resources,
};

#endif /* CONFIG_MFD_SM501 */

static struct platform_device *rts7751r2d_devices[] __initdata = {
#ifdef CONFIG_MFD_SM501
	&uart_device,
	&sm501_device,
#endif
	&cf_ide_device,
	&heartbeat_device,
};

static int __init rts7751r2d_devices_setup(void)
{
	return platform_add_devices(rts7751r2d_devices,
				    ARRAY_SIZE(rts7751r2d_devices));
}
__initcall(rts7751r2d_devices_setup);

static void rts7751r2d_power_off(void)
{
	ctrl_outw(0x0001, PA_POWOFF);
}

static inline unsigned char is_ide_ioaddr(unsigned long addr)
{
	return ((cf_ide_resources[0].start <= addr &&
		 addr <= cf_ide_resources[0].end) ||
		(cf_ide_resources[1].start <= addr &&
		 addr <= cf_ide_resources[1].end));
}

void rts7751r2d_writeb(u8 b, void __iomem *addr)
{
	unsigned long tmp = (unsigned long __force)addr;

	if (is_ide_ioaddr(tmp))
		ctrl_outw((u16)b, tmp);
	else
		ctrl_outb(b, tmp);
}

u8 rts7751r2d_readb(void __iomem *addr)
{
	unsigned long tmp = (unsigned long __force)addr;

	if (is_ide_ioaddr(tmp))
		return ctrl_inw(tmp) & 0xff;
	else
		return ctrl_inb(tmp);
}

/*
 * Initialize the board
 */
static void __init rts7751r2d_setup(char **cmdline_p)
{
	u16 ver = ctrl_inw(PA_VERREG);

	printk(KERN_INFO "Renesas Technology Sales RTS7751R2D support.\n");

	printk(KERN_INFO "FPGA version:%d (revision:%d)\n",
					(ver >> 4) & 0xf, ver & 0xf);

	ctrl_outw(0x0000, PA_OUTPORT);
	pm_power_off = rts7751r2d_power_off;

	voyagergx_serial_init();
}

/*
 * The Machine Vector
 */
static struct sh_machine_vector mv_rts7751r2d __initmv = {
	.mv_name		= "RTS7751R2D",
	.mv_setup		= rts7751r2d_setup,
	.mv_init_irq		= init_rts7751r2d_IRQ,
	.mv_irq_demux		= rts7751r2d_irq_demux,
	.mv_writeb		= rts7751r2d_writeb,
	.mv_readb		= rts7751r2d_readb,
#if defined(CONFIG_MFD_SM501) && defined(CONFIG_USB_OHCI_HCD)
	.mv_consistent_alloc	= voyagergx_consistent_alloc,
	.mv_consistent_free	= voyagergx_consistent_free,
#endif
};
