/*
 * OMAP L3 Interconnect error handling driver
 *
 * Copyright (C) 2011-2014 Texas Instruments Incorporated - http://www.ti.com/
 *	Santosh Shilimkar <santosh.shilimkar@ti.com>
 *	Sricharan <r.sricharan@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/slab.h>

#include "omap_l3_noc.h"

/*
 * Interrupt Handler for L3 error detection.
 *	1) Identify the L3 clockdomain partition to which the error belongs to.
 *	2) Identify the slave where the error information is logged
 *	3) Print the logged information.
 *	4) Add dump stack to provide kernel trace.
 *
 * Two Types of errors :
 *	1) Custom errors in L3 :
 *		Target like DMM/FW/EMIF generates SRESP=ERR error
 *	2) Standard L3 error:
 *		- Unsupported CMD.
 *			L3 tries to access target while it is idle
 *		- OCP disconnect.
 *		- Address hole error:
 *			If DSS/ISS/FDIF/USBHOSTFS access a target where they
 *			do not have connectivity, the error is logged in
 *			their default target which is DMM2.
 *
 *	On High Secure devices, firewall errors are possible and those
 *	can be trapped as well. But the trapping is implemented as part
 *	secure software and hence need not be implemented here.
 */
static irqreturn_t l3_interrupt_handler(int irq, void *_l3)
{

	struct omap_l3 *l3 = _l3;
	int inttype, i, k;
	int err_src = 0;
	u32 std_err_main, err_reg, clear, masterid;
	void __iomem *base, *l3_targ_base;
	void __iomem *l3_targ_stderr, *l3_targ_slvofslsb, *l3_targ_mstaddr;
	char *target_name, *master_name = "UN IDENTIFIED";

	/* Get the Type of interrupt */
	inttype = irq == l3->app_irq ? L3_APPLICATION_ERROR : L3_DEBUG_ERROR;

	for (i = 0; i < L3_MODULES; i++) {
		/*
		 * Read the regerr register of the clock domain
		 * to determine the source
		 */
		base = l3->l3_base[i];
		err_reg = readl_relaxed(base + l3_flagmux[i] +
					L3_FLAGMUX_REGERR0 + (inttype << 3));

		/* Get the corresponding error and analyse */
		if (err_reg) {
			/* Identify the source from control status register */
			err_src = __ffs(err_reg);

			/* Read the stderrlog_main_source from clk domain */
			l3_targ_base = base + *(l3_targ[i] + err_src);
			l3_targ_stderr = l3_targ_base + L3_TARG_STDERRLOG_MAIN;
			l3_targ_slvofslsb = l3_targ_base +
					    L3_TARG_STDERRLOG_SLVOFSLSB;
			l3_targ_mstaddr = l3_targ_base +
					  L3_TARG_STDERRLOG_MSTADDR;

			std_err_main = readl_relaxed(l3_targ_stderr);
			masterid = readl_relaxed(l3_targ_mstaddr);

			switch (std_err_main & CUSTOM_ERROR) {
			case STANDARD_ERROR:
				target_name =
					l3_targ_inst_name[i][err_src];
				WARN(true, "L3 standard error: TARGET:%s at address 0x%x\n",
					target_name,
					readl_relaxed(l3_targ_slvofslsb));
				/* clear the std error log*/
				clear = std_err_main | CLEAR_STDERR_LOG;
				writel_relaxed(clear, l3_targ_stderr);
				break;

			case CUSTOM_ERROR:
				target_name =
					l3_targ_inst_name[i][err_src];
				for (k = 0; k < NUM_OF_L3_MASTERS; k++) {
					if (masterid == l3_masters[k].id)
						master_name =
							l3_masters[k].name;
				}
				WARN(true, "L3 custom error: MASTER:%s TARGET:%s\n",
					master_name, target_name);
				/* clear the std error log*/
				clear = std_err_main | CLEAR_STDERR_LOG;
				writel_relaxed(clear, l3_targ_stderr);
				break;

			default:
				/* Nothing to be handled here as of now */
				break;
			}
		/* Error found so break the for loop */
		break;
		}
	}
	return IRQ_HANDLED;
}

static int omap_l3_probe(struct platform_device *pdev)
{
	static struct omap_l3 *l3;
	int ret, i;

	l3 = devm_kzalloc(&pdev->dev, sizeof(*l3), GFP_KERNEL);
	if (!l3)
		return -ENOMEM;

	l3->dev = &pdev->dev;
	platform_set_drvdata(pdev, l3);

	/* Get mem resources */
	for (i = 0; i < L3_MODULES; i++) {
		struct resource	*res = platform_get_resource(pdev,
							     IORESOURCE_MEM, i);

		l3->l3_base[i] = devm_ioremap_resource(&pdev->dev, res);
		if (IS_ERR(l3->l3_base[i])) {
			dev_err(l3->dev, "ioremap %d failed\n", i);
			return PTR_ERR(l3->l3_base[i]);
		}
	}

	/*
	 * Setup interrupt Handlers
	 */
	l3->debug_irq = platform_get_irq(pdev, 0);
	ret = devm_request_irq(l3->dev, l3->debug_irq, l3_interrupt_handler,
			       IRQF_DISABLED, "l3-dbg-irq", l3);
	if (ret) {
		dev_err(l3->dev, "request_irq failed for %d\n",
			l3->debug_irq);
		return ret;
	}

	l3->app_irq = platform_get_irq(pdev, 1);
	ret = devm_request_irq(l3->dev, l3->app_irq, l3_interrupt_handler,
			       IRQF_DISABLED, "l3-app-irq", l3);
	if (ret)
		dev_err(l3->dev, "request_irq failed for %d\n", l3->app_irq);

	return ret;
}

#if defined(CONFIG_OF)
static const struct of_device_id l3_noc_match[] = {
	{.compatible = "ti,omap4-l3-noc", },
	{},
};
MODULE_DEVICE_TABLE(of, l3_noc_match);
#else
#define l3_noc_match NULL
#endif

static struct platform_driver omap_l3_driver = {
	.probe		= omap_l3_probe,
	.driver		= {
		.name		= "omap_l3_noc",
		.owner		= THIS_MODULE,
		.of_match_table = l3_noc_match,
	},
};

static int __init omap_l3_init(void)
{
	return platform_driver_register(&omap_l3_driver);
}
postcore_initcall_sync(omap_l3_init);

static void __exit omap_l3_exit(void)
{
	platform_driver_unregister(&omap_l3_driver);
}
module_exit(omap_l3_exit);
