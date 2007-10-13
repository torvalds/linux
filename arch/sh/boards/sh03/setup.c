/*
 * linux/arch/sh/boards/sh03/setup.c
 *
 * Copyright (C) 2004  Interface Co.,Ltd. Saito.K
 *
 */

#include <linux/init.h>
#include <linux/irq.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <asm/io.h>
#include <asm/rtc.h>
#include <asm/sh03/io.h>
#include <asm/sh03/sh03.h>
#include <asm/addrspace.h>

static void __init init_sh03_IRQ(void)
{
	plat_irq_setup_pins(IRQ_MODE_IRQ);
}

extern void *cf_io_base;

static void __iomem *sh03_ioport_map(unsigned long port, unsigned int size)
{
	if (PXSEG(port))
		return (void __iomem *)port;
	/* CompactFlash (IDE) */
	if (((port >= 0x1f0) && (port <= 0x1f7)) || (port == 0x3f6))
		return (void __iomem *)((unsigned long)cf_io_base + port);

        return (void __iomem *)(port + PCI_IO_BASE);
}

/* arch/sh/boards/sh03/rtc.c */
void sh03_time_init(void);

static void __init sh03_setup(char **cmdline_p)
{
	board_time_init = sh03_time_init;
}

static struct resource heartbeat_resources[] = {
	[0] = {
		.start	= 0xa0800000,
		.end	= 0xa0800000,
		.flags	= IORESOURCE_MEM,
	},
};

static struct platform_device heartbeat_device = {
	.name		= "heartbeat",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(heartbeat_resources),
	.resource	= heartbeat_resources,
};

static struct platform_device *sh03_devices[] __initdata = {
	&heartbeat_device,
};

static int __init sh03_devices_setup(void)
{
	return platform_add_devices(sh03_devices, ARRAY_SIZE(sh03_devices));
}
__initcall(sh03_devices_setup);

static struct sh_machine_vector mv_sh03 __initmv = {
	.mv_name		= "Interface (CTP/PCI-SH03)",
	.mv_setup		= sh03_setup,
	.mv_nr_irqs		= 48,
	.mv_ioport_map		= sh03_ioport_map,
	.mv_init_irq		= init_sh03_IRQ,
};
