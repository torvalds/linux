/*
 *  Copyright (C) 2013 Altera Corporation <www.altera.com>
 *  Copyright 2011-2012 Calxeda, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Adapted from the highbank_mc_edac driver
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/ctype.h>
#include <linux/edac.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/of_platform.h>
#include <linux/uaccess.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>

#include "edac_core.h"
#include "edac_module.h"

#define ALTR_EDAC_MOD_STR	"altera_edac"

/* SDRAM Controller CtrlCfg Register */
#define ALTR_SDR_CTLCFG                 0x00

/* SDRAM Controller CtrlCfg Register Bit Masks */
#define ALTR_SDR_CTLCFG_ECC_EN          0x400
#define ALTR_SDR_CTLCFG_ECC_CORR_EN     0x800
#define ALTR_SDR_CTLCFG_GEN_SB_ERR      0x2000
#define ALTR_SDR_CTLCFG_GEN_DB_ERR      0x4000

#define ALTR_SDR_CTLCFG_ECC_AUTO_EN     (ALTR_SDR_CTLCFG_ECC_EN | \
					ALTR_SDR_CTLCFG_ECC_CORR_EN)

/* SDRAM Controller DRAM Status Register */
#define ALTR_SDR_DRAMSTS                0x38

/* SDRAM Controller DRAM Status Register Bit Masks */
#define ALTR_SDR_DRAMSTS_SBEERR         0x04
#define ALTR_SDR_DRAMSTS_DBEERR         0x08
#define ALTR_SDR_DRAMSTS_CORR_DROP      0x10

/* SDRAM Controller DRAM IRQ Register */
#define ALTR_SDR_DRAMINTR                0x3C

/* SDRAM Controller DRAM IRQ Register Bit Masks */
#define ALTR_SDR_DRAMINTR_INTREN        0x01
#define ALTR_SDR_DRAMINTR_SBEMASK       0x02
#define ALTR_SDR_DRAMINTR_DBEMASK       0x04
#define ALTR_SDR_DRAMINTR_CORRDROPMASK  0x08
#define ALTR_SDR_DRAMINTR_INTRCLR       0x10

/* SDRAM Controller Single Bit Error Count Register */
#define ALTR_SDR_SBECOUNT               0x40

/* SDRAM Controller Single Bit Error Count Register Bit Masks */
#define ALTR_SDR_SBECOUNT_COUNT         0x0F

/* SDRAM Controller Double Bit Error Count Register */
#define ALTR_SDR_DBECOUNT               0x44

/* SDRAM Controller Double Bit Error Count Register Bit Masks */
#define ALTR_SDR_DBECOUNT_COUNT         0x0F

/* SDRAM Controller ECC Error Address Register */
#define ALTR_SDR_ERRADDR                0x48

/* SDRAM Controller ECC Error Address Register Bit Masks */
#define ALTR_SDR_ERRADDR_ADDR           0xFFFFFFFF

/* SDRAM Controller ECC Autocorrect Drop Count Register */
#define ALTR_SDR_DROPCOUNT              0x4C

/* SDRAM Controller ECC Autocorrect Drop Count Register Bit Masks */
#define ALTR_SDR_DROPCOUNT_CORR         0x0F

/* SDRAM Controller ECC AutoCorrect Address Register */
#define ALTR_SDR_DROPADDR               0x50

/* SDRAM Controller ECC AutoCorrect Error Address Register Bit Masks */
#define ALTR_SDR_DROPADDR_ADDR          0xFFFFFFFF

/* Altera SDRAM Memory Controller data */
struct altr_sdram_mc_data {
	struct regmap *mc_vbase;
};

static irqreturn_t altr_sdram_mc_err_handler(int irq, void *dev_id)
{
	struct mem_ctl_info *mci = dev_id;
	struct altr_sdram_mc_data *drvdata = mci->pvt_info;
	u32 status = 0, err_count = 0, err_addr = 0;

	/* Error Address is the same for both SBE & DBE */
	regmap_read(drvdata->mc_vbase, ALTR_SDR_ERRADDR, &err_addr);

	regmap_read(drvdata->mc_vbase, ALTR_SDR_DRAMSTS, &status);

	if (status & ALTR_SDR_DRAMSTS_DBEERR) {
		regmap_read(drvdata->mc_vbase, ALTR_SDR_DBECOUNT, &err_count);
		panic("\nEDAC: [%d Uncorrectable errors @ 0x%08X]\n",
			err_count, err_addr);
	}
	if (status & ALTR_SDR_DRAMSTS_SBEERR) {
		regmap_read(drvdata->mc_vbase, ALTR_SDR_SBECOUNT, &err_count);
		edac_mc_handle_error(HW_EVENT_ERR_CORRECTED, mci, err_count,
					err_addr >> PAGE_SHIFT,
					err_addr & ~PAGE_MASK, 0,
					0, 0, -1, mci->ctl_name, "");
	}

	regmap_write(drvdata->mc_vbase,	ALTR_SDR_DRAMINTR,
		(ALTR_SDR_DRAMINTR_INTRCLR | ALTR_SDR_DRAMINTR_INTREN));

	return IRQ_HANDLED;
}

/* Get total memory size from Open Firmware DTB */
static u32 altr_sdram_get_total_mem_size(void)
{
	struct device_node *np;
	u32 retcode, reg_array[2];

	np = of_find_node_by_type(NULL, "memory");
	if (!np)
		return 0;

	retcode = of_property_read_u32_array(np, "reg",
		reg_array, ARRAY_SIZE(reg_array));

	of_node_put(np);

	if (retcode)
		return 0;

	return reg_array[1];
}

static int altr_sdram_mc_probe(struct platform_device *pdev)
{
	struct edac_mc_layer layers[2];
	struct mem_ctl_info *mci;
	struct altr_sdram_mc_data *drvdata;
	struct dimm_info *dimm;
	u32 read_reg = 0, mem_size;
	int irq;
	int res = 0, retcode;

	layers[0].type = EDAC_MC_LAYER_CHIP_SELECT;
	layers[0].size = 1;
	layers[0].is_virt_csrow = true;
	layers[1].type = EDAC_MC_LAYER_CHANNEL;
	layers[1].size = 1;
	layers[1].is_virt_csrow = false;
	mci = edac_mc_alloc(0, ARRAY_SIZE(layers), layers,
			    sizeof(struct altr_sdram_mc_data));
	if (!mci)
		return -ENOMEM;

	mci->pdev = &pdev->dev;
	drvdata = mci->pvt_info;
	platform_set_drvdata(pdev, mci);

	if (!devres_open_group(&pdev->dev, NULL, GFP_KERNEL)) {
		edac_mc_free(mci);
		return -ENOMEM;
	}

	/* Grab the register values from the sdr-ctl in device tree */
	drvdata->mc_vbase = syscon_regmap_lookup_by_compatible("altr,sdr-ctl");
	if (IS_ERR(drvdata->mc_vbase)) {
		dev_err(&pdev->dev,
			"regmap for altr,sdr-ctl lookup failed.\n");
		res = -ENODEV;
		goto err;
	}

	retcode = regmap_read(drvdata->mc_vbase, ALTR_SDR_CTLCFG, &read_reg);
	if (retcode || ((read_reg & ALTR_SDR_CTLCFG_ECC_AUTO_EN) !=
		ALTR_SDR_CTLCFG_ECC_AUTO_EN)) {
		dev_err(&pdev->dev, "No ECC present / ECC disabled - 0x%08X\n",
		      read_reg);
		res = -ENODEV;
		goto err;
	}

	mci->mtype_cap = MEM_FLAG_DDR3;
	mci->edac_ctl_cap = EDAC_FLAG_NONE | EDAC_FLAG_SECDED;
	mci->edac_cap = EDAC_FLAG_SECDED;
	mci->mod_name = ALTR_EDAC_MOD_STR;
	mci->mod_ver = "1";
	mci->ctl_name = dev_name(&pdev->dev);
	mci->scrub_mode = SCRUB_SW_SRC;
	mci->dev_name = dev_name(&pdev->dev);

	/* Grab memory size from device tree. */
	mem_size = altr_sdram_get_total_mem_size();
	dev_dbg(&pdev->dev, "EDAC Memory Size = 0x%08x\n", mem_size);
	dimm = *mci->dimms;
	if (mem_size <= 0) {
		dev_err(&pdev->dev, "Unable to find memory size (dts)\n");
		res = -ENODEV;
		goto err;
	}
	dimm->nr_pages = ((mem_size - 1) >> PAGE_SHIFT) + 1;
	dimm->grain = 8;
	dimm->dtype = DEV_X8;
	dimm->mtype = MEM_DDR3;
	dimm->edac_mode = EDAC_SECDED;

	res = edac_mc_add_mc(mci);
	if (res < 0)
		goto err;

	retcode = regmap_write(drvdata->mc_vbase, ALTR_SDR_DRAMINTR,
			ALTR_SDR_DRAMINTR_INTRCLR);
	if (retcode) {
		dev_err(&pdev->dev, "Error clearning SDRAM ECC IRQ\n");
		res = -ENODEV;
		goto err;
	}

	irq = platform_get_irq(pdev, 0);
	res = devm_request_irq(&pdev->dev, irq, altr_sdram_mc_err_handler,
				0, dev_name(&pdev->dev), mci);
	if (res < 0) {
		dev_err(&pdev->dev, "Unable to request irq %d\n", irq);
		res = -ENODEV;
		goto err;
	}

	retcode = regmap_write(drvdata->mc_vbase, ALTR_SDR_DRAMINTR,
		(ALTR_SDR_DRAMINTR_INTRCLR | ALTR_SDR_DRAMINTR_INTREN));
	if (retcode) {
		dev_err(&pdev->dev, "Error enabling SDRAM ECC IRQ\n");
		res = -ENODEV;
		goto err;
	}

	devres_close_group(&pdev->dev, NULL);

	return 0;

err:
	dev_err(&pdev->dev, "EDAC Probe Failed; Error %d\n", res);
	devres_release_group(&pdev->dev, NULL);
	edac_mc_free(mci);

	return res;
}

static int altr_sdram_mc_remove(struct platform_device *pdev)
{
	struct mem_ctl_info *mci = platform_get_drvdata(pdev);

	edac_mc_del_mc(&pdev->dev);
	edac_mc_free(mci);
	platform_set_drvdata(pdev, NULL);

	return 0;
}

static const struct of_device_id altr_sdram_ctrl_of_match[] = {
	{ .compatible = "altr,sdram-edac", },
	{},
};
MODULE_DEVICE_TABLE(of, altr_sdram_ctrl_of_match);

static struct platform_driver altr_sdram_mc_edac_driver = {
	.probe = altr_sdram_mc_probe,
	.remove = altr_sdram_mc_remove,
	.driver = {
		.name = "altr_sdram_mc_edac",
		.of_match_table = of_match_ptr(altr_sdram_ctrl_of_match),
	},
};

module_platform_driver(altr_sdram_mc_edac_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Altera Corporation");
MODULE_DESCRIPTION("EDAC Driver for Altera SDRAM Controller");
