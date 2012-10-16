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

#include "edac_core.h"
#include "edac_module.h"
#include "octeon_edac-lmc.h"

#define EDAC_MOD_STR "octeon"

static struct mem_ctl_info *mc_cavium;
static void *lmc_base;

static void co_lmc_poll(struct mem_ctl_info *mci)
{
	union lmc_mem_cfg0 cfg0;
	union lmc_fadr fadr;
	char msg[64];

	fadr.u64 = readq(lmc_base + LMC_FADR);
	cfg0.u64 = readq(lmc_base + LMC_MEM_CFG0);
	snprintf(msg, sizeof(msg), "DIMM %d rank %d bank %d row %d col %d",
		fadr.fdimm, fadr.fbunk, fadr.fbank, fadr.frow, fadr.fcol);

	if (cfg0.sec_err) {
		edac_mc_handle_error(HW_EVENT_ERR_CORRECTED, mci, 1, 0, 0, 0, -1, -1, -1,
				     msg, "");

		cfg0.intr_sec_ena = -1;		/* Done, re-arm */
	}

	if (cfg0.ded_err) {
		edac_mc_handle_error(HW_EVENT_ERR_UNCORRECTED, mci, 1, 0, 0, 0, -1, -1, -1,
				     msg, "");
		cfg0.intr_ded_ena = -1;		/* Done, re-arm */
	}

	writeq(cfg0.u64, lmc_base + LMC_MEM_CFG0);
}

static int __devinit co_lmc_probe(struct platform_device *pdev)
{
	struct mem_ctl_info *mci;
	union lmc_mem_cfg0 cfg0;
	int res = 0;

	mci = edac_mc_alloc(0, 0, 0, 0);
	if (!mci)
		return -ENOMEM;

	mci->pdev = &pdev->dev;
	platform_set_drvdata(pdev, mci);
	mci->dev_name = dev_name(&pdev->dev);

	mci->mod_name = "octeon-lmc";
	mci->ctl_name = "co_lmc_err";
	mci->edac_check = co_lmc_poll;

	if (edac_mc_add_mc(mci) > 0) {
		pr_err("%s: edac_mc_add_mc() failed\n", __func__);
		goto err;
	}

	cfg0.u64 = readq(lmc_base + LMC_MEM_CFG0);	/* We poll */
	cfg0.intr_ded_ena = 0;
	cfg0.intr_sec_ena = 0;
	writeq(cfg0.u64, lmc_base + LMC_MEM_CFG0);

	mc_cavium = mci;

	return 0;

err:
	edac_mc_free(mci);

	return res;
}

static int co_lmc_remove(struct platform_device *pdev)
{
	struct mem_ctl_info *mci = platform_get_drvdata(pdev);

	mc_cavium = NULL;
	edac_mc_del_mc(&pdev->dev);
	edac_mc_free(mci);

	return 0;
}

static struct platform_driver co_lmc_driver = {
	.probe = co_lmc_probe,
	.remove = co_lmc_remove,
	.driver = {
		   .name = "co_lmc_edac",
	}
};

static int __init co_edac_init(void)
{
	union lmc_mem_cfg0 cfg0;
	int ret;

	lmc_base = ioremap_nocache(LMC_BASE, LMC_SIZE);
	if (!lmc_base)
		return -ENOMEM;

	cfg0.u64 = readq(lmc_base + LMC_MEM_CFG0);
	if (!cfg0.ecc_ena) {
		pr_info(EDAC_MOD_STR " LMC EDAC: ECC disabled, good bye\n");
		ret = -ENODEV;
		goto out;
	}

	ret = platform_driver_register(&co_lmc_driver);
	if (ret) {
		pr_warning(EDAC_MOD_STR " LMC EDAC failed to register\n");
		goto out;
	}

	return ret;

out:
	iounmap(lmc_base);

	return ret;
}

static void __exit co_edac_exit(void)
{
	platform_driver_unregister(&co_lmc_driver);
	iounmap(lmc_base);
}

module_init(co_edac_init);
module_exit(co_edac_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ralf Baechle <ralf@linux-mips.org>");
