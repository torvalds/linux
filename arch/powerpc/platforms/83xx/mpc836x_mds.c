// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2006 Freescale Semiconductor, Inc. All rights reserved.
 *
 * Author: Li Yang <LeoLi@freescale.com>
 *	   Yin Olivia <Hong-hua.Yin@freescale.com>
 *
 * Description:
 * MPC8360E MDS board specific routines.
 *
 * Changelog:
 * Jun 21, 2006	Initial version
 */

#include <linux/stddef.h>
#include <linux/kernel.h>
#include <linux/compiler.h>
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
#include <linux/of_platform.h>
#include <linux/of_device.h>

#include <linux/atomic.h>
#include <asm/time.h>
#include <asm/io.h>
#include <asm/machdep.h>
#include <asm/ipic.h>
#include <asm/irq.h>
#include <asm/prom.h>
#include <asm/udbg.h>
#include <sysdev/fsl_soc.h>
#include <sysdev/fsl_pci.h>
#include <sysdev/simple_gpio.h>
#include <soc/fsl/qe/qe.h>
#include <soc/fsl/qe/qe_ic.h>

#include "mpc83xx.h"

#undef DEBUG
#ifdef DEBUG
#define DBG(fmt...) udbg_printf(fmt)
#else
#define DBG(fmt...)
#endif

/* ************************************************************************
 *
 * Setup the architecture
 *
 */
static void __init mpc836x_mds_setup_arch(void)
{
	struct device_node *np;
	u8 __iomem *bcsr_regs = NULL;

	mpc83xx_setup_arch();

	/* Map BCSR area */
	np = of_find_node_by_name(NULL, "bcsr");
	if (np) {
		struct resource res;

		of_address_to_resource(np, 0, &res);
		bcsr_regs = ioremap(res.start, resource_size(&res));
		of_node_put(np);
	}

#ifdef CONFIG_QUICC_ENGINE
	if ((np = of_find_node_by_name(NULL, "par_io")) != NULL) {
		par_io_init(np);
		of_node_put(np);

		for_each_node_by_name(np, "ucc")
			par_io_of_config(np);
#ifdef CONFIG_QE_USB
		/* Must fixup Par IO before QE GPIO chips are registered. */
		par_io_config_pin(1,  2, 1, 0, 3, 0); /* USBOE  */
		par_io_config_pin(1,  3, 1, 0, 3, 0); /* USBTP  */
		par_io_config_pin(1,  8, 1, 0, 1, 0); /* USBTN  */
		par_io_config_pin(1, 10, 2, 0, 3, 0); /* USBRXD */
		par_io_config_pin(1,  9, 2, 1, 3, 0); /* USBRP  */
		par_io_config_pin(1, 11, 2, 1, 3, 0); /* USBRN  */
		par_io_config_pin(2, 20, 2, 0, 1, 0); /* CLK21  */
#endif /* CONFIG_QE_USB */
	}

	if ((np = of_find_compatible_node(NULL, "network", "ucc_geth"))
			!= NULL){
		uint svid;

		/* Reset the Ethernet PHY */
#define BCSR9_GETHRST 0x20
		clrbits8(&bcsr_regs[9], BCSR9_GETHRST);
		udelay(1000);
		setbits8(&bcsr_regs[9], BCSR9_GETHRST);

		/* handle mpc8360ea rev.2.1 erratum 2: RGMII Timing */
		svid = mfspr(SPRN_SVR);
		if (svid == 0x80480021) {
			void __iomem *immap;

			immap = ioremap(get_immrbase() + 0x14a8, 8);

			/*
			 * IMMR + 0x14A8[4:5] = 11 (clk delay for UCC 2)
			 * IMMR + 0x14A8[18:19] = 11 (clk delay for UCC 1)
			 */
			setbits32(immap, 0x0c003000);

			/*
			 * IMMR + 0x14AC[20:27] = 10101010
			 * (data delay for both UCC's)
			 */
			clrsetbits_be32(immap + 4, 0xff0, 0xaa0);

			iounmap(immap);
		}

		iounmap(bcsr_regs);
		of_node_put(np);
	}
#endif				/* CONFIG_QUICC_ENGINE */
}

machine_device_initcall(mpc836x_mds, mpc83xx_declare_of_platform_devices);

#ifdef CONFIG_QE_USB
static int __init mpc836x_usb_cfg(void)
{
	u8 __iomem *bcsr;
	struct device_node *np;
	const char *mode;
	int ret = 0;

	np = of_find_compatible_node(NULL, NULL, "fsl,mpc8360mds-bcsr");
	if (!np)
		return -ENODEV;

	bcsr = of_iomap(np, 0);
	of_node_put(np);
	if (!bcsr)
		return -ENOMEM;

	np = of_find_compatible_node(NULL, NULL, "fsl,mpc8323-qe-usb");
	if (!np) {
		ret = -ENODEV;
		goto err;
	}

#define BCSR8_TSEC1M_MASK	(0x3 << 6)
#define BCSR8_TSEC1M_RGMII	(0x0 << 6)
#define BCSR8_TSEC2M_MASK	(0x3 << 4)
#define BCSR8_TSEC2M_RGMII	(0x0 << 4)
	/*
	 * Default is GMII (2), but we should set it to RGMII (0) if we use
	 * USB (Eth PHY is in RGMII mode anyway).
	 */
	clrsetbits_8(&bcsr[8], BCSR8_TSEC1M_MASK | BCSR8_TSEC2M_MASK,
			       BCSR8_TSEC1M_RGMII | BCSR8_TSEC2M_RGMII);

#define BCSR13_USBMASK	0x0f
#define BCSR13_nUSBEN	0x08 /* 1 - Disable, 0 - Enable			*/
#define BCSR13_USBSPEED	0x04 /* 1 - Full, 0 - Low			*/
#define BCSR13_USBMODE	0x02 /* 1 - Host, 0 - Function			*/
#define BCSR13_nUSBVCC	0x01 /* 1 - gets VBUS, 0 - supplies VBUS 	*/

	clrsetbits_8(&bcsr[13], BCSR13_USBMASK, BCSR13_USBSPEED);

	mode = of_get_property(np, "mode", NULL);
	if (mode && !strcmp(mode, "peripheral")) {
		setbits8(&bcsr[13], BCSR13_nUSBVCC);
		qe_usb_clock_set(QE_CLK21, 48000000);
	} else {
		setbits8(&bcsr[13], BCSR13_USBMODE);
		/*
		 * The BCSR GPIOs are used to control power and
		 * speed of the USB transceiver. This is needed for
		 * the USB Host only.
		 */
		simple_gpiochip_init("fsl,mpc8360mds-bcsr-gpio");
	}

	of_node_put(np);
err:
	iounmap(bcsr);
	return ret;
}
machine_arch_initcall(mpc836x_mds, mpc836x_usb_cfg);
#endif /* CONFIG_QE_USB */

/*
 * Called very early, MMU is off, device-tree isn't unflattened
 */
static int __init mpc836x_mds_probe(void)
{
	return of_machine_is_compatible("MPC836xMDS");
}

define_machine(mpc836x_mds) {
	.name		= "MPC836x MDS",
	.probe		= mpc836x_mds_probe,
	.setup_arch	= mpc836x_mds_setup_arch,
	.init_IRQ	= mpc83xx_ipic_and_qe_init_IRQ,
	.get_irq	= ipic_get_irq,
	.restart	= mpc83xx_restart,
	.time_init	= mpc83xx_time_init,
	.calibrate_decr	= generic_calibrate_decr,
	.progress	= udbg_progress,
};
