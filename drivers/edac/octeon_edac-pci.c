/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2009 Wind River Systems,
 *   written by Ralf Baechle <ralf@linux-mips.org>
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/edac.h>

#include <asm/octeon/cvmx.h>
#include <asm/octeon/cvmx-npi-defs.h>
#include <asm/octeon/cvmx-pci-defs.h>
#include <asm/octeon/octeon.h>

#include "edac_core.h"
#include "edac_module.h"

#define EDAC_MOD_STR "octeon"

static void co_pci_poll(struct edac_pci_ctl_info *pci)
{
	union cvmx_pci_cfg01 cfg01;

	cfg01.u32 = octeon_npi_read32(CVMX_NPI_PCI_CFG01);
	if (cfg01.s.dpe) {		/* Detected parity error */
		edac_pci_handle_pe(pci, pci->ctl_name);
		cfg01.s.dpe = 1;		/* Reset  */
		octeon_npi_write32(CVMX_NPI_PCI_CFG01, cfg01.u32);
	}
	if (cfg01.s.sse) {
		edac_pci_handle_npe(pci, "Signaled System Error");
		cfg01.s.sse = 1;		/* Reset */
		octeon_npi_write32(CVMX_NPI_PCI_CFG01, cfg01.u32);
	}
	if (cfg01.s.rma) {
		edac_pci_handle_npe(pci, "Received Master Abort");
		cfg01.s.rma = 1;		/* Reset */
		octeon_npi_write32(CVMX_NPI_PCI_CFG01, cfg01.u32);
	}
	if (cfg01.s.rta) {
		edac_pci_handle_npe(pci, "Received Target Abort");
		cfg01.s.rta = 1;		/* Reset */
		octeon_npi_write32(CVMX_NPI_PCI_CFG01, cfg01.u32);
	}
	if (cfg01.s.sta) {
		edac_pci_handle_npe(pci, "Signaled Target Abort");
		cfg01.s.sta = 1;		/* Reset */
		octeon_npi_write32(CVMX_NPI_PCI_CFG01, cfg01.u32);
	}
	if (cfg01.s.mdpe) {
		edac_pci_handle_npe(pci, "Master Data Parity Error");
		cfg01.s.mdpe = 1;		/* Reset */
		octeon_npi_write32(CVMX_NPI_PCI_CFG01, cfg01.u32);
	}
	if (cfg01.s.mdpe) {
		edac_pci_handle_npe(pci, "Master Data Parity Error");
		cfg01.s.mdpe = 1;		/* Reset */
		octeon_npi_write32(CVMX_NPI_PCI_CFG01, cfg01.u32);
	}
}

static int __devinit co_pci_probe(struct platform_device *pdev)
{
	struct edac_pci_ctl_info *pci;
	int res = 0;

	pci = edac_pci_alloc_ctl_info(0, "octeon_pci_err");
	if (!pci)
		return -ENOMEM;

	pci->dev = &pdev->dev;
	platform_set_drvdata(pdev, pci);
	pci->dev_name = dev_name(&pdev->dev);

	pci->mod_name = "octeon-pci";
	pci->ctl_name = "octeon_pci_err";
	pci->edac_check = co_pci_poll;

	if (edac_pci_add_device(pci, 0) > 0) {
		pr_err("%s: edac_pci_add_device() failed\n", __func__);
		goto err;
	}

	return 0;

err:
	edac_pci_free_ctl_info(pci);

	return res;
}

static int co_pci_remove(struct platform_device *pdev)
{
	struct edac_pci_ctl_info *pci = platform_get_drvdata(pdev);

	edac_pci_del_device(&pdev->dev);
	edac_pci_free_ctl_info(pci);

	return 0;
}

static struct platform_driver co_pci_driver = {
	.probe = co_pci_probe,
	.remove = co_pci_remove,
	.driver = {
		   .name = "co_pci_edac",
	}
};

static int __init co_edac_init(void)
{
	int ret;

	ret = platform_driver_register(&co_pci_driver);
	if (ret)
		pr_warning(EDAC_MOD_STR " PCI EDAC failed to register\n");

	return ret;
}

static void __exit co_edac_exit(void)
{
	platform_driver_unregister(&co_pci_driver);
}

module_init(co_edac_init);
module_exit(co_edac_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ralf Baechle <ralf@linux-mips.org>");
