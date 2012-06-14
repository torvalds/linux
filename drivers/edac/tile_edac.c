/*
 * Copyright 2011 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 * Tilera-specific EDAC driver.
 *
 * This source code is derived from the following driver:
 *
 * Cell MIC driver for ECC counting
 *
 * Copyright 2007 Benjamin Herrenschmidt, IBM Corp.
 *                <benh@kernel.crashing.org>
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/uaccess.h>
#include <linux/edac.h>
#include <hv/hypervisor.h>
#include <hv/drv_mshim_intf.h>

#include "edac_core.h"

#define DRV_NAME	"tile-edac"

/* Number of cs_rows needed per memory controller on TILEPro. */
#define TILE_EDAC_NR_CSROWS	1

/* Number of channels per memory controller on TILEPro. */
#define TILE_EDAC_NR_CHANS	1

/* Granularity of reported error in bytes on TILEPro. */
#define TILE_EDAC_ERROR_GRAIN	8

/* TILE processor has multiple independent memory controllers. */
struct platform_device *mshim_pdev[TILE_MAX_MSHIMS];

struct tile_edac_priv {
	int		hv_devhdl;	/* Hypervisor device handle. */
	int		node;		/* Memory controller instance #. */
	unsigned int	ce_count;	/*
					 * Correctable-error counter
					 * kept by the driver.
					 */
};

static void tile_edac_check(struct mem_ctl_info *mci)
{
	struct tile_edac_priv	*priv = mci->pvt_info;
	struct mshim_mem_error	mem_error;

	if (hv_dev_pread(priv->hv_devhdl, 0, (HV_VirtAddr)&mem_error,
		sizeof(struct mshim_mem_error), MSHIM_MEM_ERROR_OFF) !=
		sizeof(struct mshim_mem_error)) {
		pr_err(DRV_NAME ": MSHIM_MEM_ERROR_OFF pread failure.\n");
		return;
	}

	/* Check if the current error count is different from the saved one. */
	if (mem_error.sbe_count != priv->ce_count) {
		dev_dbg(mci->dev, "ECC CE err on node %d\n", priv->node);
		priv->ce_count = mem_error.sbe_count;
		edac_mc_handle_error(HW_EVENT_ERR_CORRECTED, mci,
				     0, 0, 0,
				     0, 0, -1,
				     mci->ctl_name, "", NULL);
	}
}

/*
 * Initialize the 'csrows' table within the mci control structure with the
 * addressing of memory.
 */
static int __devinit tile_edac_init_csrows(struct mem_ctl_info *mci)
{
	struct csrow_info	*csrow = &mci->csrows[0];
	struct tile_edac_priv	*priv = mci->pvt_info;
	struct mshim_mem_info	mem_info;
	struct dimm_info *dimm = csrow->channels[0].dimm;

	if (hv_dev_pread(priv->hv_devhdl, 0, (HV_VirtAddr)&mem_info,
		sizeof(struct mshim_mem_info), MSHIM_MEM_INFO_OFF) !=
		sizeof(struct mshim_mem_info)) {
		pr_err(DRV_NAME ": MSHIM_MEM_INFO_OFF pread failure.\n");
		return -1;
	}

	if (mem_info.mem_ecc)
		dimm->edac_mode = EDAC_SECDED;
	else
		dimm->edac_mode = EDAC_NONE;
	switch (mem_info.mem_type) {
	case DDR2:
		dimm->mtype = MEM_DDR2;
		break;

	case DDR3:
		dimm->mtype = MEM_DDR3;
		break;

	default:
		return -1;
	}

	dimm->nr_pages = mem_info.mem_size >> PAGE_SHIFT;
	dimm->grain = TILE_EDAC_ERROR_GRAIN;
	dimm->dtype = DEV_UNKNOWN;

	return 0;
}

static int __devinit tile_edac_mc_probe(struct platform_device *pdev)
{
	char			hv_file[32];
	int			hv_devhdl;
	struct mem_ctl_info	*mci;
	struct edac_mc_layer	layers[2];
	struct tile_edac_priv	*priv;
	int			rc;

	sprintf(hv_file, "mshim/%d", pdev->id);
	hv_devhdl = hv_dev_open((HV_VirtAddr)hv_file, 0);
	if (hv_devhdl < 0)
		return -EINVAL;

	/* A TILE MC has a single channel and one chip-select row. */
	layers[0].type = EDAC_MC_LAYER_CHIP_SELECT;
	layers[0].size = TILE_EDAC_NR_CSROWS;
	layers[0].is_virt_csrow = true;
	layers[1].type = EDAC_MC_LAYER_CHANNEL;
	layers[1].size = TILE_EDAC_NR_CHANS;
	layers[1].is_virt_csrow = false;
	mci = edac_mc_alloc(pdev->id, ARRAY_SIZE(layers), layers,
			    sizeof(struct tile_edac_priv));
	if (mci == NULL)
		return -ENOMEM;
	priv = mci->pvt_info;
	priv->node = pdev->id;
	priv->hv_devhdl = hv_devhdl;

	mci->dev = &pdev->dev;
	mci->mtype_cap = MEM_FLAG_DDR2;
	mci->edac_ctl_cap = EDAC_FLAG_SECDED;

	mci->mod_name = DRV_NAME;
#ifdef __tilegx__
	mci->ctl_name = "TILEGx_Memory_Controller";
#else
	mci->ctl_name = "TILEPro_Memory_Controller";
#endif
	mci->dev_name = dev_name(&pdev->dev);
	mci->edac_check = tile_edac_check;

	/*
	 * Initialize the MC control structure 'csrows' table
	 * with the mapping and control information.
	 */
	if (tile_edac_init_csrows(mci)) {
		/* No csrows found. */
		mci->edac_cap = EDAC_FLAG_NONE;
	} else {
		mci->edac_cap = EDAC_FLAG_SECDED;
	}

	platform_set_drvdata(pdev, mci);

	/* Register with EDAC core */
	rc = edac_mc_add_mc(mci);
	if (rc) {
		dev_err(&pdev->dev, "failed to register with EDAC core\n");
		edac_mc_free(mci);
		return rc;
	}

	return 0;
}

static int __devexit tile_edac_mc_remove(struct platform_device *pdev)
{
	struct mem_ctl_info *mci = platform_get_drvdata(pdev);

	edac_mc_del_mc(&pdev->dev);
	if (mci)
		edac_mc_free(mci);
	return 0;
}

static struct platform_driver tile_edac_mc_driver = {
	.driver		= {
		.name	= DRV_NAME,
		.owner	= THIS_MODULE,
	},
	.probe		= tile_edac_mc_probe,
	.remove		= __devexit_p(tile_edac_mc_remove),
};

/*
 * Driver init routine.
 */
static int __init tile_edac_init(void)
{
	char	hv_file[32];
	struct platform_device *pdev;
	int i, err, num = 0;

	/* Only support POLL mode. */
	edac_op_state = EDAC_OPSTATE_POLL;

	err = platform_driver_register(&tile_edac_mc_driver);
	if (err)
		return err;

	for (i = 0; i < TILE_MAX_MSHIMS; i++) {
		/*
		 * Not all memory controllers are configured such as in the
		 * case of a simulator. So we register only those mshims
		 * that are configured by the hypervisor.
		 */
		sprintf(hv_file, "mshim/%d", i);
		if (hv_dev_open((HV_VirtAddr)hv_file, 0) < 0)
			continue;

		pdev = platform_device_register_simple(DRV_NAME, i, NULL, 0);
		if (IS_ERR(pdev))
			continue;
		mshim_pdev[i] = pdev;
		num++;
	}

	if (num == 0) {
		platform_driver_unregister(&tile_edac_mc_driver);
		return -ENODEV;
	}
	return 0;
}

/*
 * Driver cleanup routine.
 */
static void __exit tile_edac_exit(void)
{
	int i;

	for (i = 0; i < TILE_MAX_MSHIMS; i++) {
		struct platform_device *pdev = mshim_pdev[i];
		if (!pdev)
			continue;

		platform_set_drvdata(pdev, NULL);
		platform_device_unregister(pdev);
	}
	platform_driver_unregister(&tile_edac_mc_driver);
}

module_init(tile_edac_init);
module_exit(tile_edac_exit);
