/*
 * This contain platform specific code for APM PPC460EX based Canyonlands
 * board.
 *
 * Copyright (c) 2010, Applied Micro Circuits Corporation
 * Author: Rupjyoti Sarmah <rsarmah@apm.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 *
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <asm/pci-bridge.h>
#include <asm/ppc4xx.h>
#include <asm/udbg.h>
#include <asm/uic.h>
#include <linux/of_platform.h>
#include <linux/delay.h>
#include "44x.h"

#define BCSR_USB_EN	0x11

static const struct of_device_id ppc460ex_of_bus[] __initconst = {
	{ .compatible = "ibm,plb4", },
	{ .compatible = "ibm,opb", },
	{ .compatible = "ibm,ebc", },
	{ .compatible = "simple-bus", },
	{},
};

static int __init ppc460ex_device_probe(void)
{
	of_platform_bus_probe(NULL, ppc460ex_of_bus, NULL);

	return 0;
}
machine_device_initcall(canyonlands, ppc460ex_device_probe);

/* Using this code only for the Canyonlands board.  */

static int __init ppc460ex_probe(void)
{
	unsigned long root = of_get_flat_dt_root();
	if (of_flat_dt_is_compatible(root, "amcc,canyonlands")) {
		pci_set_flags(PCI_REASSIGN_ALL_RSRC);
		return 1;
		}
	return 0;
}

/* USB PHY fixup code on Canyonlands kit. */

static int __init ppc460ex_canyonlands_fixup(void)
{
	u8 __iomem *bcsr ;
	void __iomem *vaddr;
	struct device_node *np;
	int ret = 0;

	np = of_find_compatible_node(NULL, NULL, "amcc,ppc460ex-bcsr");
	if (!np) {
		printk(KERN_ERR "failed did not find amcc, ppc460ex bcsr node\n");
		return -ENODEV;
	}

	bcsr = of_iomap(np, 0);
	of_node_put(np);

	if (!bcsr) {
		printk(KERN_CRIT "Could not remap bcsr\n");
		ret = -ENODEV;
		goto err_bcsr;
	}

	np = of_find_compatible_node(NULL, NULL, "ibm,ppc4xx-gpio");
	if (!np) {
		printk(KERN_ERR "failed did not find ibm,ppc4xx-gpio node\n");
		return -ENODEV;
	}

	vaddr = of_iomap(np, 0);
	of_node_put(np);

	if (!vaddr) {
		printk(KERN_CRIT "Could not get gpio node address\n");
		ret = -ENODEV;
		goto err_gpio;
	}
	/* Disable USB, through the BCSR7 bits */
	setbits8(&bcsr[7], BCSR_USB_EN);

	/* Wait for a while after reset */
	msleep(100);

	/* Enable USB here */
	clrbits8(&bcsr[7], BCSR_USB_EN);

	/*
	 * Configure multiplexed gpio16 and gpio19 as alternate1 output
	 * source after USB reset. In this configuration gpio16 will be
	 * USB2HStop and gpio19 will be USB2DStop. For more details refer to
	 * table 34-7 of PPC460EX user manual.
	 */
	setbits32((vaddr + GPIO0_OSRH), 0x42000000);
	setbits32((vaddr + GPIO0_TSRH), 0x42000000);
err_gpio:
	iounmap(vaddr);
err_bcsr:
	iounmap(bcsr);
	return ret;
}
machine_device_initcall(canyonlands, ppc460ex_canyonlands_fixup);
define_machine(canyonlands) {
	.name = "Canyonlands",
	.probe = ppc460ex_probe,
	.progress = udbg_progress,
	.init_IRQ = uic_init_tree,
	.get_irq = uic_get_irq,
	.restart = ppc4xx_reset_system,
	.calibrate_decr = generic_calibrate_decr,
};
