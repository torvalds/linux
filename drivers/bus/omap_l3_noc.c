/*
 * OMAP L3 Interconnect error handling driver
 *
 * Copyright (C) 2011-2015 Texas Instruments Incorporated - http://www.ti.com/
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
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include "omap_l3_noc.h"

/**
 * l3_handle_target() - Handle Target specific parse and reporting
 * @l3:		pointer to l3 struct
 * @base:	base address of clkdm
 * @flag_mux:	flagmux corresponding to the event
 * @err_src:	error source index of the slave (target)
 *
 * This does the second part of the error interrupt handling:
 *	3) Parse in the slave information
 *	4) Print the logged information.
 *	5) Add dump stack to provide kernel trace.
 *	6) Clear the source if known.
 *
 * This handles two types of errors:
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
static int l3_handle_target(struct omap_l3 *l3, void __iomem *base,
			    struct l3_flagmux_data *flag_mux, int err_src)
{
	int k;
	u32 std_err_main, clear, masterid;
	u8 op_code, m_req_info;
	void __iomem *l3_targ_base;
	void __iomem *l3_targ_stderr, *l3_targ_slvofslsb, *l3_targ_mstaddr;
	void __iomem *l3_targ_hdr, *l3_targ_info;
	struct l3_target_data *l3_targ_inst;
	struct l3_masters_data *master;
	char *target_name, *master_name = "UN IDENTIFIED";
	char *err_description;
	char err_string[30] = { 0 };
	char info_string[60] = { 0 };

	/* We DONOT expect err_src to go out of bounds */
	BUG_ON(err_src > MAX_CLKDM_TARGETS);

	if (err_src < flag_mux->num_targ_data) {
		l3_targ_inst = &flag_mux->l3_targ[err_src];
		target_name = l3_targ_inst->name;
		l3_targ_base = base + l3_targ_inst->offset;
	} else {
		target_name = L3_TARGET_NOT_SUPPORTED;
	}

	if (target_name == L3_TARGET_NOT_SUPPORTED)
		return -ENODEV;

	/* Read the stderrlog_main_source from clk domain */
	l3_targ_stderr = l3_targ_base + L3_TARG_STDERRLOG_MAIN;
	l3_targ_slvofslsb = l3_targ_base + L3_TARG_STDERRLOG_SLVOFSLSB;

	std_err_main = readl_relaxed(l3_targ_stderr);

	switch (std_err_main & CUSTOM_ERROR) {
	case STANDARD_ERROR:
		err_description = "Standard";
		snprintf(err_string, sizeof(err_string),
			 ": At Address: 0x%08X ",
			 readl_relaxed(l3_targ_slvofslsb));

		l3_targ_mstaddr = l3_targ_base + L3_TARG_STDERRLOG_MSTADDR;
		l3_targ_hdr = l3_targ_base + L3_TARG_STDERRLOG_HDR;
		l3_targ_info = l3_targ_base + L3_TARG_STDERRLOG_INFO;
		break;

	case CUSTOM_ERROR:
		err_description = "Custom";

		l3_targ_mstaddr = l3_targ_base +
				  L3_TARG_STDERRLOG_CINFO_MSTADDR;
		l3_targ_hdr = l3_targ_base + L3_TARG_STDERRLOG_CINFO_OPCODE;
		l3_targ_info = l3_targ_base + L3_TARG_STDERRLOG_CINFO_INFO;
		break;

	default:
		/* Nothing to be handled here as of now */
		return 0;
	}

	/* STDERRLOG_MSTADDR Stores the NTTP master address. */
	masterid = (readl_relaxed(l3_targ_mstaddr) &
		    l3->mst_addr_mask) >> __ffs(l3->mst_addr_mask);

	for (k = 0, master = l3->l3_masters; k < l3->num_masters;
	     k++, master++) {
		if (masterid == master->id) {
			master_name = master->name;
			break;
		}
	}

	op_code = readl_relaxed(l3_targ_hdr) & 0x7;

	m_req_info = readl_relaxed(l3_targ_info) & 0xF;
	snprintf(info_string, sizeof(info_string),
		 ": %s in %s mode during %s access",
		 (m_req_info & BIT(0)) ? "Opcode Fetch" : "Data Access",
		 (m_req_info & BIT(1)) ? "Supervisor" : "User",
		 (m_req_info & BIT(3)) ? "Debug" : "Functional");

	WARN(true,
	     "%s:L3 %s Error: MASTER %s TARGET %s (%s)%s%s\n",
	     dev_name(l3->dev),
	     err_description,
	     master_name, target_name,
	     l3_transaction_type[op_code],
	     err_string, info_string);

	/* clear the std error log*/
	clear = std_err_main | CLEAR_STDERR_LOG;
	writel_relaxed(clear, l3_targ_stderr);

	return 0;
}

/**
 * l3_interrupt_handler() - interrupt handler for l3 events
 * @irq:	irq number
 * @_l3:	pointer to l3 structure
 *
 * Interrupt Handler for L3 error detection.
 *	1) Identify the L3 clockdomain partition to which the error belongs to.
 *	2) Identify the slave where the error information is logged
 *	... handle the slave event..
 *	7) if the slave is unknown, mask out the slave.
 */
static irqreturn_t l3_interrupt_handler(int irq, void *_l3)
{
	struct omap_l3 *l3 = _l3;
	int inttype, i, ret;
	int err_src = 0;
	u32 err_reg, mask_val;
	void __iomem *base, *mask_reg;
	struct l3_flagmux_data *flag_mux;

	/* Get the Type of interrupt */
	inttype = irq == l3->app_irq ? L3_APPLICATION_ERROR : L3_DEBUG_ERROR;

	for (i = 0; i < l3->num_modules; i++) {
		/*
		 * Read the regerr register of the clock domain
		 * to determine the source
		 */
		base = l3->l3_base[i];
		flag_mux = l3->l3_flagmux[i];
		err_reg = readl_relaxed(base + flag_mux->offset +
					L3_FLAGMUX_REGERR0 + (inttype << 3));

		err_reg &= ~(inttype ? flag_mux->mask_app_bits :
				flag_mux->mask_dbg_bits);

		/* Get the corresponding error and analyse */
		if (err_reg) {
			/* Identify the source from control status register */
			err_src = __ffs(err_reg);

			ret = l3_handle_target(l3, base, flag_mux, err_src);

			/*
			 * Certain plaforms may have "undocumented" status
			 * pending on boot. So dont generate a severe warning
			 * here. Just mask it off to prevent the error from
			 * reoccuring and locking up the system.
			 */
			if (ret) {
				dev_err(l3->dev,
					"L3 %s error: target %d mod:%d %s\n",
					inttype ? "debug" : "application",
					err_src, i, "(unclearable)");

				mask_reg = base + flag_mux->offset +
					   L3_FLAGMUX_MASK0 + (inttype << 3);
				mask_val = readl_relaxed(mask_reg);
				mask_val &= ~(1 << err_src);
				writel_relaxed(mask_val, mask_reg);

				/* Mark these bits as to be ignored */
				if (inttype)
					flag_mux->mask_app_bits |= 1 << err_src;
				else
					flag_mux->mask_dbg_bits |= 1 << err_src;
			}

			/* Error found so break the for loop */
			return IRQ_HANDLED;
		}
	}

	dev_err(l3->dev, "L3 %s IRQ not handled!!\n",
		inttype ? "debug" : "application");

	return IRQ_NONE;
}

static const struct of_device_id l3_noc_match[] = {
	{.compatible = "ti,omap4-l3-noc", .data = &omap4_l3_data},
	{.compatible = "ti,omap5-l3-noc", .data = &omap5_l3_data},
	{.compatible = "ti,dra7-l3-noc", .data = &dra_l3_data},
	{.compatible = "ti,am4372-l3-noc", .data = &am4372_l3_data},
	{},
};
MODULE_DEVICE_TABLE(of, l3_noc_match);

static int omap_l3_probe(struct platform_device *pdev)
{
	const struct of_device_id *of_id;
	static struct omap_l3 *l3;
	int ret, i, res_idx;

	of_id = of_match_device(l3_noc_match, &pdev->dev);
	if (!of_id) {
		dev_err(&pdev->dev, "OF data missing\n");
		return -EINVAL;
	}

	l3 = devm_kzalloc(&pdev->dev, sizeof(*l3), GFP_KERNEL);
	if (!l3)
		return -ENOMEM;

	memcpy(l3, of_id->data, sizeof(*l3));
	l3->dev = &pdev->dev;
	platform_set_drvdata(pdev, l3);

	/* Get mem resources */
	for (i = 0, res_idx = 0; i < l3->num_modules; i++) {
		struct resource	*res;

		if (l3->l3_base[i] == L3_BASE_IS_SUBMODULE) {
			/* First entry cannot be submodule */
			BUG_ON(i == 0);
			l3->l3_base[i] = l3->l3_base[i - 1];
			continue;
		}
		res = platform_get_resource(pdev, IORESOURCE_MEM, res_idx);
		l3->l3_base[i] = devm_ioremap_resource(&pdev->dev, res);
		if (IS_ERR(l3->l3_base[i])) {
			dev_err(l3->dev, "ioremap %d failed\n", i);
			return PTR_ERR(l3->l3_base[i]);
		}
		res_idx++;
	}

	/*
	 * Setup interrupt Handlers
	 */
	l3->debug_irq = platform_get_irq(pdev, 0);
	ret = devm_request_irq(l3->dev, l3->debug_irq, l3_interrupt_handler,
			       0x0, "l3-dbg-irq", l3);
	if (ret) {
		dev_err(l3->dev, "request_irq failed for %d\n",
			l3->debug_irq);
		return ret;
	}

	l3->app_irq = platform_get_irq(pdev, 1);
	ret = devm_request_irq(l3->dev, l3->app_irq, l3_interrupt_handler,
			       0x0, "l3-app-irq", l3);
	if (ret)
		dev_err(l3->dev, "request_irq failed for %d\n", l3->app_irq);

	return ret;
}

#ifdef	CONFIG_PM

/**
 * l3_resume_noirq() - resume function for l3_noc
 * @dev:	pointer to l3_noc device structure
 *
 * We only have the resume handler only since we
 * have already maintained the delta register
 * configuration as part of configuring the system
 */
static int l3_resume_noirq(struct device *dev)
{
	struct omap_l3 *l3 = dev_get_drvdata(dev);
	int i;
	struct l3_flagmux_data *flag_mux;
	void __iomem *base, *mask_regx = NULL;
	u32 mask_val;

	for (i = 0; i < l3->num_modules; i++) {
		base = l3->l3_base[i];
		flag_mux = l3->l3_flagmux[i];
		if (!flag_mux->mask_app_bits && !flag_mux->mask_dbg_bits)
			continue;

		mask_regx = base + flag_mux->offset + L3_FLAGMUX_MASK0 +
			   (L3_APPLICATION_ERROR << 3);
		mask_val = readl_relaxed(mask_regx);
		mask_val &= ~(flag_mux->mask_app_bits);

		writel_relaxed(mask_val, mask_regx);
		mask_regx = base + flag_mux->offset + L3_FLAGMUX_MASK0 +
			   (L3_DEBUG_ERROR << 3);
		mask_val = readl_relaxed(mask_regx);
		mask_val &= ~(flag_mux->mask_dbg_bits);

		writel_relaxed(mask_val, mask_regx);
	}

	/* Dummy read to force OCP barrier */
	if (mask_regx)
		(void)readl(mask_regx);

	return 0;
}

static const struct dev_pm_ops l3_dev_pm_ops = {
	.resume_noirq		= l3_resume_noirq,
};

#define L3_DEV_PM_OPS (&l3_dev_pm_ops)
#else
#define L3_DEV_PM_OPS NULL
#endif

static struct platform_driver omap_l3_driver = {
	.probe		= omap_l3_probe,
	.driver		= {
		.name		= "omap_l3_noc",
		.pm		= L3_DEV_PM_OPS,
		.of_match_table = of_match_ptr(l3_noc_match),
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
