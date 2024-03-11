// SPDX-License-Identifier: GPL-2.0
/*
 * Bluefield-specific EDAC driver.
 *
 * Copyright (c) 2019 Mellanox Technologies.
 */

#include <linux/acpi.h>
#include <linux/arm-smccc.h>
#include <linux/bitfield.h>
#include <linux/edac.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#include "edac_module.h"

#define DRIVER_NAME		"bluefield-edac"

/*
 * Mellanox BlueField EMI (External Memory Interface) register definitions.
 */

#define MLXBF_ECC_CNT 0x340
#define MLXBF_ECC_CNT__SERR_CNT GENMASK(15, 0)
#define MLXBF_ECC_CNT__DERR_CNT GENMASK(31, 16)

#define MLXBF_ECC_ERR 0x348
#define MLXBF_ECC_ERR__SECC BIT(0)
#define MLXBF_ECC_ERR__DECC BIT(16)

#define MLXBF_ECC_LATCH_SEL 0x354
#define MLXBF_ECC_LATCH_SEL__START BIT(24)

#define MLXBF_ERR_ADDR_0 0x358

#define MLXBF_ERR_ADDR_1 0x37c

#define MLXBF_SYNDROM 0x35c
#define MLXBF_SYNDROM__DERR BIT(0)
#define MLXBF_SYNDROM__SERR BIT(1)
#define MLXBF_SYNDROM__SYN GENMASK(25, 16)

#define MLXBF_ADD_INFO 0x364
#define MLXBF_ADD_INFO__ERR_PRANK GENMASK(9, 8)

#define MLXBF_EDAC_MAX_DIMM_PER_MC	2
#define MLXBF_EDAC_ERROR_GRAIN		8

/*
 * Request MLNX_SIP_GET_DIMM_INFO
 *
 * Retrieve information about DIMM on a certain slot.
 *
 * Call register usage:
 * a0: MLNX_SIP_GET_DIMM_INFO
 * a1: (Memory controller index) << 16 | (Dimm index in memory controller)
 * a2-7: not used.
 *
 * Return status:
 * a0: MLXBF_DIMM_INFO defined below describing the DIMM.
 * a1-3: not used.
 */
#define MLNX_SIP_GET_DIMM_INFO		0x82000008

/* Format for the SMC response about the memory information */
#define MLXBF_DIMM_INFO__SIZE_GB GENMASK_ULL(15, 0)
#define MLXBF_DIMM_INFO__IS_RDIMM BIT(16)
#define MLXBF_DIMM_INFO__IS_LRDIMM BIT(17)
#define MLXBF_DIMM_INFO__IS_NVDIMM BIT(18)
#define MLXBF_DIMM_INFO__RANKS GENMASK_ULL(23, 21)
#define MLXBF_DIMM_INFO__PACKAGE_X GENMASK_ULL(31, 24)

struct bluefield_edac_priv {
	int dimm_ranks[MLXBF_EDAC_MAX_DIMM_PER_MC];
	void __iomem *emi_base;
	int dimm_per_mc;
};

static u64 smc_call1(u64 smc_op, u64 smc_arg)
{
	struct arm_smccc_res res;

	arm_smccc_smc(smc_op, smc_arg, 0, 0, 0, 0, 0, 0, &res);

	return res.a0;
}

/*
 * Gather the ECC information from the External Memory Interface registers
 * and report it to the edac handler.
 */
static void bluefield_gather_report_ecc(struct mem_ctl_info *mci,
					int error_cnt,
					int is_single_ecc)
{
	struct bluefield_edac_priv *priv = mci->pvt_info;
	u32 dram_additional_info, err_prank, edea0, edea1;
	u32 ecc_latch_select, dram_syndrom, serr, derr, syndrom;
	enum hw_event_mc_err_type ecc_type;
	u64 ecc_dimm_addr;
	int ecc_dimm;

	ecc_type = is_single_ecc ? HW_EVENT_ERR_CORRECTED :
				   HW_EVENT_ERR_UNCORRECTED;

	/*
	 * Tell the External Memory Interface to populate the relevant
	 * registers with information about the last ECC error occurrence.
	 */
	ecc_latch_select = MLXBF_ECC_LATCH_SEL__START;
	writel(ecc_latch_select, priv->emi_base + MLXBF_ECC_LATCH_SEL);

	/*
	 * Verify that the ECC reported info in the registers is of the
	 * same type as the one asked to report. If not, just report the
	 * error without the detailed information.
	 */
	dram_syndrom = readl(priv->emi_base + MLXBF_SYNDROM);
	serr = FIELD_GET(MLXBF_SYNDROM__SERR, dram_syndrom);
	derr = FIELD_GET(MLXBF_SYNDROM__DERR, dram_syndrom);
	syndrom = FIELD_GET(MLXBF_SYNDROM__SYN, dram_syndrom);

	if ((is_single_ecc && !serr) || (!is_single_ecc && !derr)) {
		edac_mc_handle_error(ecc_type, mci, error_cnt, 0, 0, 0,
				     0, 0, -1, mci->ctl_name, "");
		return;
	}

	dram_additional_info = readl(priv->emi_base + MLXBF_ADD_INFO);
	err_prank = FIELD_GET(MLXBF_ADD_INFO__ERR_PRANK, dram_additional_info);

	ecc_dimm = (err_prank >= 2 && priv->dimm_ranks[0] <= 2) ? 1 : 0;

	edea0 = readl(priv->emi_base + MLXBF_ERR_ADDR_0);
	edea1 = readl(priv->emi_base + MLXBF_ERR_ADDR_1);

	ecc_dimm_addr = ((u64)edea1 << 32) | edea0;

	edac_mc_handle_error(ecc_type, mci, error_cnt,
			     PFN_DOWN(ecc_dimm_addr),
			     offset_in_page(ecc_dimm_addr),
			     syndrom, ecc_dimm, 0, 0, mci->ctl_name, "");
}

static void bluefield_edac_check(struct mem_ctl_info *mci)
{
	struct bluefield_edac_priv *priv = mci->pvt_info;
	u32 ecc_count, single_error_count, double_error_count, ecc_error = 0;

	/*
	 * The memory controller might not be initialized by the firmware
	 * when there isn't memory, which may lead to bad register readings.
	 */
	if (mci->edac_cap == EDAC_FLAG_NONE)
		return;

	ecc_count = readl(priv->emi_base + MLXBF_ECC_CNT);
	single_error_count = FIELD_GET(MLXBF_ECC_CNT__SERR_CNT, ecc_count);
	double_error_count = FIELD_GET(MLXBF_ECC_CNT__DERR_CNT, ecc_count);

	if (single_error_count) {
		ecc_error |= MLXBF_ECC_ERR__SECC;

		bluefield_gather_report_ecc(mci, single_error_count, 1);
	}

	if (double_error_count) {
		ecc_error |= MLXBF_ECC_ERR__DECC;

		bluefield_gather_report_ecc(mci, double_error_count, 0);
	}

	/* Write to clear reported errors. */
	if (ecc_count)
		writel(ecc_error, priv->emi_base + MLXBF_ECC_ERR);
}

/* Initialize the DIMMs information for the given memory controller. */
static void bluefield_edac_init_dimms(struct mem_ctl_info *mci)
{
	struct bluefield_edac_priv *priv = mci->pvt_info;
	int mem_ctrl_idx = mci->mc_idx;
	struct dimm_info *dimm;
	u64 smc_info, smc_arg;
	int is_empty = 1, i;

	for (i = 0; i < priv->dimm_per_mc; i++) {
		dimm = mci->dimms[i];

		smc_arg = mem_ctrl_idx << 16 | i;
		smc_info = smc_call1(MLNX_SIP_GET_DIMM_INFO, smc_arg);

		if (!FIELD_GET(MLXBF_DIMM_INFO__SIZE_GB, smc_info)) {
			dimm->mtype = MEM_EMPTY;
			continue;
		}

		is_empty = 0;

		dimm->edac_mode = EDAC_SECDED;

		if (FIELD_GET(MLXBF_DIMM_INFO__IS_NVDIMM, smc_info))
			dimm->mtype = MEM_NVDIMM;
		else if (FIELD_GET(MLXBF_DIMM_INFO__IS_LRDIMM, smc_info))
			dimm->mtype = MEM_LRDDR4;
		else if (FIELD_GET(MLXBF_DIMM_INFO__IS_RDIMM, smc_info))
			dimm->mtype = MEM_RDDR4;
		else
			dimm->mtype = MEM_DDR4;

		dimm->nr_pages =
			FIELD_GET(MLXBF_DIMM_INFO__SIZE_GB, smc_info) *
			(SZ_1G / PAGE_SIZE);
		dimm->grain = MLXBF_EDAC_ERROR_GRAIN;

		/* Mem controller for BlueField only supports x4, x8 and x16 */
		switch (FIELD_GET(MLXBF_DIMM_INFO__PACKAGE_X, smc_info)) {
		case 4:
			dimm->dtype = DEV_X4;
			break;
		case 8:
			dimm->dtype = DEV_X8;
			break;
		case 16:
			dimm->dtype = DEV_X16;
			break;
		default:
			dimm->dtype = DEV_UNKNOWN;
		}

		priv->dimm_ranks[i] =
			FIELD_GET(MLXBF_DIMM_INFO__RANKS, smc_info);
	}

	if (is_empty)
		mci->edac_cap = EDAC_FLAG_NONE;
	else
		mci->edac_cap = EDAC_FLAG_SECDED;
}

static int bluefield_edac_mc_probe(struct platform_device *pdev)
{
	struct bluefield_edac_priv *priv;
	struct device *dev = &pdev->dev;
	struct edac_mc_layer layers[1];
	struct mem_ctl_info *mci;
	struct resource *emi_res;
	unsigned int mc_idx, dimm_count;
	int rc, ret;

	/* Read the MSS (Memory SubSystem) index from ACPI table. */
	if (device_property_read_u32(dev, "mss_number", &mc_idx)) {
		dev_warn(dev, "bf_edac: MSS number unknown\n");
		return -EINVAL;
	}

	/* Read the DIMMs per MC from ACPI table. */
	if (device_property_read_u32(dev, "dimm_per_mc", &dimm_count)) {
		dev_warn(dev, "bf_edac: DIMMs per MC unknown\n");
		return -EINVAL;
	}

	if (dimm_count > MLXBF_EDAC_MAX_DIMM_PER_MC) {
		dev_warn(dev, "bf_edac: DIMMs per MC not valid\n");
		return -EINVAL;
	}

	emi_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!emi_res)
		return -EINVAL;

	layers[0].type = EDAC_MC_LAYER_SLOT;
	layers[0].size = dimm_count;
	layers[0].is_virt_csrow = true;

	mci = edac_mc_alloc(mc_idx, ARRAY_SIZE(layers), layers, sizeof(*priv));
	if (!mci)
		return -ENOMEM;

	priv = mci->pvt_info;

	priv->dimm_per_mc = dimm_count;
	priv->emi_base = devm_ioremap_resource(dev, emi_res);
	if (IS_ERR(priv->emi_base)) {
		dev_err(dev, "failed to map EMI IO resource\n");
		ret = PTR_ERR(priv->emi_base);
		goto err;
	}

	mci->pdev = dev;
	mci->mtype_cap = MEM_FLAG_DDR4 | MEM_FLAG_RDDR4 |
			 MEM_FLAG_LRDDR4 | MEM_FLAG_NVDIMM;
	mci->edac_ctl_cap = EDAC_FLAG_SECDED;

	mci->mod_name = DRIVER_NAME;
	mci->ctl_name = "BlueField_Memory_Controller";
	mci->dev_name = dev_name(dev);
	mci->edac_check = bluefield_edac_check;

	/* Initialize mci with the actual populated DIMM information. */
	bluefield_edac_init_dimms(mci);

	platform_set_drvdata(pdev, mci);

	/* Register with EDAC core */
	rc = edac_mc_add_mc(mci);
	if (rc) {
		dev_err(dev, "failed to register with EDAC core\n");
		ret = rc;
		goto err;
	}

	/* Only POLL mode supported so far. */
	edac_op_state = EDAC_OPSTATE_POLL;

	return 0;

err:
	edac_mc_free(mci);

	return ret;

}

static void bluefield_edac_mc_remove(struct platform_device *pdev)
{
	struct mem_ctl_info *mci = platform_get_drvdata(pdev);

	edac_mc_del_mc(&pdev->dev);
	edac_mc_free(mci);
}

static const struct acpi_device_id bluefield_mc_acpi_ids[] = {
	{"MLNXBF08", 0},
	{}
};

MODULE_DEVICE_TABLE(acpi, bluefield_mc_acpi_ids);

static struct platform_driver bluefield_edac_mc_driver = {
	.driver = {
		.name = DRIVER_NAME,
		.acpi_match_table = bluefield_mc_acpi_ids,
	},
	.probe = bluefield_edac_mc_probe,
	.remove_new = bluefield_edac_mc_remove,
};

module_platform_driver(bluefield_edac_mc_driver);

MODULE_DESCRIPTION("Mellanox BlueField memory edac driver");
MODULE_AUTHOR("Mellanox Technologies");
MODULE_LICENSE("GPL v2");
