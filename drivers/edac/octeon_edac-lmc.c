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

#include <asm/octeon/octeon.h>
#include <asm/octeon/cvmx-lmcx-defs.h>

#include "edac_core.h"
#include "edac_module.h"

#define OCTEON_MAX_MC 4

static void octeon_lmc_edac_poll(struct mem_ctl_info *mci)
{
	union cvmx_lmcx_mem_cfg0 cfg0;
	bool do_clear = false;
	char msg[64];

	cfg0.u64 = cvmx_read_csr(CVMX_LMCX_MEM_CFG0(mci->mc_idx));
	if (cfg0.s.sec_err || cfg0.s.ded_err) {
		union cvmx_lmcx_fadr fadr;
		fadr.u64 = cvmx_read_csr(CVMX_LMCX_FADR(mci->mc_idx));
		snprintf(msg, sizeof(msg),
			 "DIMM %d rank %d bank %d row %d col %d",
			 fadr.cn30xx.fdimm, fadr.cn30xx.fbunk,
			 fadr.cn30xx.fbank, fadr.cn30xx.frow, fadr.cn30xx.fcol);
	}

	if (cfg0.s.sec_err) {
		edac_mc_handle_error(HW_EVENT_ERR_CORRECTED, mci, 1, 0, 0, 0,
				     -1, -1, -1, msg, "");
		cfg0.s.sec_err = -1;	/* Done, re-arm */
		do_clear = true;
	}

	if (cfg0.s.ded_err) {
		edac_mc_handle_error(HW_EVENT_ERR_UNCORRECTED, mci, 1, 0, 0, 0,
				     -1, -1, -1, msg, "");
		cfg0.s.ded_err = -1;	/* Done, re-arm */
		do_clear = true;
	}
	if (do_clear)
		cvmx_write_csr(CVMX_LMCX_MEM_CFG0(mci->mc_idx), cfg0.u64);
}

static void octeon_lmc_edac_poll_o2(struct mem_ctl_info *mci)
{
	union cvmx_lmcx_int int_reg;
	bool do_clear = false;
	char msg[64];

	int_reg.u64 = cvmx_read_csr(CVMX_LMCX_INT(mci->mc_idx));
	if (int_reg.s.sec_err || int_reg.s.ded_err) {
		union cvmx_lmcx_fadr fadr;
		fadr.u64 = cvmx_read_csr(CVMX_LMCX_FADR(mci->mc_idx));
		snprintf(msg, sizeof(msg),
			 "DIMM %d rank %d bank %d row %d col %d",
			 fadr.cn61xx.fdimm, fadr.cn61xx.fbunk,
			 fadr.cn61xx.fbank, fadr.cn61xx.frow, fadr.cn61xx.fcol);
	}

	if (int_reg.s.sec_err) {
		edac_mc_handle_error(HW_EVENT_ERR_CORRECTED, mci, 1, 0, 0, 0,
				     -1, -1, -1, msg, "");
		int_reg.s.sec_err = -1;	/* Done, re-arm */
		do_clear = true;
	}

	if (int_reg.s.ded_err) {
		edac_mc_handle_error(HW_EVENT_ERR_UNCORRECTED, mci, 1, 0, 0, 0,
				     -1, -1, -1, msg, "");
		int_reg.s.ded_err = -1;	/* Done, re-arm */
		do_clear = true;
	}
	if (do_clear)
		cvmx_write_csr(CVMX_LMCX_INT(mci->mc_idx), int_reg.u64);
}

static int octeon_lmc_edac_probe(struct platform_device *pdev)
{
	struct mem_ctl_info *mci;
	struct edac_mc_layer layers[1];
	int mc = pdev->id;

	layers[0].type = EDAC_MC_LAYER_CHANNEL;
	layers[0].size = 1;
	layers[0].is_virt_csrow = false;

	if (OCTEON_IS_MODEL(OCTEON_FAM_1_PLUS)) {
		union cvmx_lmcx_mem_cfg0 cfg0;

		cfg0.u64 = cvmx_read_csr(CVMX_LMCX_MEM_CFG0(0));
		if (!cfg0.s.ecc_ena) {
			dev_info(&pdev->dev, "Disabled (ECC not enabled)\n");
			return 0;
		}

		mci = edac_mc_alloc(mc, ARRAY_SIZE(layers), layers, 0);
		if (!mci)
			return -ENXIO;

		mci->pdev = &pdev->dev;
		mci->dev_name = dev_name(&pdev->dev);

		mci->mod_name = "octeon-lmc";
		mci->ctl_name = "octeon-lmc-err";
		mci->edac_check = octeon_lmc_edac_poll;

		if (edac_mc_add_mc(mci)) {
			dev_err(&pdev->dev, "edac_mc_add_mc() failed\n");
			edac_mc_free(mci);
			return -ENXIO;
		}

		cfg0.u64 = cvmx_read_csr(CVMX_LMCX_MEM_CFG0(mc));
		cfg0.s.intr_ded_ena = 0;	/* We poll */
		cfg0.s.intr_sec_ena = 0;
		cvmx_write_csr(CVMX_LMCX_MEM_CFG0(mc), cfg0.u64);
	} else {
		/* OCTEON II */
		union cvmx_lmcx_int_en en;
		union cvmx_lmcx_config config;

		config.u64 = cvmx_read_csr(CVMX_LMCX_CONFIG(0));
		if (!config.s.ecc_ena) {
			dev_info(&pdev->dev, "Disabled (ECC not enabled)\n");
			return 0;
		}

		mci = edac_mc_alloc(mc, ARRAY_SIZE(layers), layers, 0);
		if (!mci)
			return -ENXIO;

		mci->pdev = &pdev->dev;
		mci->dev_name = dev_name(&pdev->dev);

		mci->mod_name = "octeon-lmc";
		mci->ctl_name = "co_lmc_err";
		mci->edac_check = octeon_lmc_edac_poll_o2;

		if (edac_mc_add_mc(mci)) {
			dev_err(&pdev->dev, "edac_mc_add_mc() failed\n");
			edac_mc_free(mci);
			return -ENXIO;
		}

		en.u64 = cvmx_read_csr(CVMX_LMCX_MEM_CFG0(mc));
		en.s.intr_ded_ena = 0;	/* We poll */
		en.s.intr_sec_ena = 0;
		cvmx_write_csr(CVMX_LMCX_MEM_CFG0(mc), en.u64);
	}
	platform_set_drvdata(pdev, mci);

	return 0;
}

static int octeon_lmc_edac_remove(struct platform_device *pdev)
{
	struct mem_ctl_info *mci = platform_get_drvdata(pdev);

	edac_mc_del_mc(&pdev->dev);
	edac_mc_free(mci);
	return 0;
}

static struct platform_driver octeon_lmc_edac_driver = {
	.probe = octeon_lmc_edac_probe,
	.remove = octeon_lmc_edac_remove,
	.driver = {
		   .name = "octeon_lmc_edac",
	}
};
module_platform_driver(octeon_lmc_edac_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ralf Baechle <ralf@linux-mips.org>");
