/*
 * Freescale Lite5200 board support
 *
 * Written by: Grant Likely <grant.likely@secretlab.ca>
 *
 * Copyright (C) Secret Lab Technologies Ltd. 2006. All rights reserved.
 * Copyright (C) Freescale Semicondutor, Inc. 2006. All rights reserved.
 *
 * Description:
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#undef DEBUG

#include <linux/stddef.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/reboot.h>
#include <linux/pci.h>
#include <linux/kdev_t.h>
#include <linux/major.h>
#include <linux/console.h>
#include <linux/delay.h>
#include <linux/seq_file.h>
#include <linux/root_dev.h>
#include <linux/initrd.h>

#include <asm/system.h>
#include <asm/atomic.h>
#include <asm/time.h>
#include <asm/io.h>
#include <asm/machdep.h>
#include <asm/ipic.h>
#include <asm/bootinfo.h>
#include <asm/irq.h>
#include <asm/prom.h>
#include <asm/udbg.h>
#include <sysdev/fsl_soc.h>
#include <asm/of_platform.h>

#include <asm/mpc52xx.h>

/* ************************************************************************
 *
 * Setup the architecture
 *
 */

static void __init
lite5200_setup_cpu(void)
{
	struct mpc52xx_gpio __iomem *gpio;
	u32 port_config;

	/* Map zones */
	gpio = mpc52xx_find_and_map("mpc5200-gpio");
	if (!gpio) {
		printk(KERN_ERR __FILE__ ": "
			"Error while mapping GPIO register for port config. "
			"Expect some abnormal behavior\n");
		goto error;
	}

	/* Set port config */
	port_config = in_be32(&gpio->port_config);

	port_config &= ~0x00800000;	/* 48Mhz internal, pin is GPIO	*/

	port_config &= ~0x00007000;	/* USB port : Differential mode	*/
	port_config |=  0x00001000;	/*            USB 1 only	*/

	port_config &= ~0x03000000;	/* ATA CS is on csb_4/5		*/
	port_config |=  0x01000000;

	pr_debug("port_config: old:%x new:%x\n",
	         in_be32(&gpio->port_config), port_config);
	out_be32(&gpio->port_config, port_config);

	/* Unmap zone */
error:
	iounmap(gpio);
}

#ifdef CONFIG_PM
static u32 descr_a;
static void lite5200_suspend_prepare(void __iomem *mbar)
{
	u8 pin = 1;	/* GPIO_WKUP_1 (GPIO_PSC2_4) */
	u8 level = 0;	/* wakeup on low level */
	mpc52xx_set_wakeup_gpio(pin, level);

	/*
	 * power down usb port
	 * this needs to be called before of-ohci suspend code
	 */
	descr_a = in_be32(mbar + 0x1048);
	out_be32(mbar + 0x1048, (descr_a & ~0x200) | 0x100);
}

static void lite5200_resume_finish(void __iomem *mbar)
{
	out_be32(mbar + 0x1048, descr_a);
}
#endif

static void __init lite5200_setup_arch(void)
{
	struct device_node *np;

	if (ppc_md.progress)
		ppc_md.progress("lite5200_setup_arch()", 0);

	np = of_find_node_by_type(NULL, "cpu");
	if (np) {
		const unsigned int *fp =
			of_get_property(np, "clock-frequency", NULL);
		if (fp != 0)
			loops_per_jiffy = *fp / HZ;
		else
			loops_per_jiffy = 50000000 / HZ;
		of_node_put(np);
	}

	/* CPU & Port mux setup */
	mpc52xx_setup_cpu();	/* Generic */
	lite5200_setup_cpu();	/* Platorm specific */

#ifdef CONFIG_PM
	mpc52xx_suspend.board_suspend_prepare = lite5200_suspend_prepare;
	mpc52xx_suspend.board_resume_finish = lite5200_resume_finish;
	mpc52xx_pm_init();
#endif

#ifdef CONFIG_PCI
	np = of_find_node_by_type(NULL, "pci");
	if (np) {
		mpc52xx_add_bridge(np);
		of_node_put(np);
	}
#endif

#ifdef CONFIG_BLK_DEV_INITRD
	if (initrd_start)
		ROOT_DEV = Root_RAM0;
	else
#endif
#ifdef  CONFIG_ROOT_NFS
		ROOT_DEV = Root_NFS;
#else
		ROOT_DEV = Root_HDA1;
#endif

}

static void lite5200_show_cpuinfo(struct seq_file *m)
{
	struct device_node* np = of_find_all_nodes(NULL);
	const char *model = NULL;

	if (np)
		model = of_get_property(np, "model", NULL);

	seq_printf(m, "vendor\t\t:	Freescale Semiconductor\n");
	seq_printf(m, "machine\t\t:	%s\n", model ? model : "unknown");

	of_node_put(np);
}

/*
 * Called very early, MMU is off, device-tree isn't unflattened
 */
static int __init lite5200_probe(void)
{
	unsigned long node = of_get_flat_dt_root();
	const char *model = of_get_flat_dt_prop(node, "model", NULL);

	if (!of_flat_dt_is_compatible(node, "fsl,lite5200") &&
	    !of_flat_dt_is_compatible(node, "fsl,lite5200b"))
		return 0;
	pr_debug("%s board found\n", model ? model : "unknown");

	return 1;
}

define_machine(lite5200) {
	.name 		= "lite5200",
	.probe 		= lite5200_probe,
	.setup_arch 	= lite5200_setup_arch,
	.init		= mpc52xx_declare_of_platform_devices,
	.init_IRQ 	= mpc52xx_init_irq,
	.get_irq 	= mpc52xx_get_irq,
	.show_cpuinfo	= lite5200_show_cpuinfo,
	.calibrate_decr	= generic_calibrate_decr,
};
