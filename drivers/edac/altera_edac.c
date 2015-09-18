/*
 *  Copyright Altera Corporation (C) 2014-2015. All rights reserved.
 *  Copyright 2011-2012 Calxeda, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Adapted from the highbank_mc_edac driver.
 */

#include <linux/ctype.h>
#include <linux/edac.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/mfd/syscon.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/types.h>
#include <linux/uaccess.h>

#include "altera_edac.h"
#include "edac_core.h"
#include "edac_module.h"

#define EDAC_MOD_STR		"altera_edac"
#define EDAC_VERSION		"1"

static const struct altr_sdram_prv_data c5_data = {
	.ecc_ctrl_offset    = CV_CTLCFG_OFST,
	.ecc_ctl_en_mask    = CV_CTLCFG_ECC_AUTO_EN,
	.ecc_stat_offset    = CV_DRAMSTS_OFST,
	.ecc_stat_ce_mask   = CV_DRAMSTS_SBEERR,
	.ecc_stat_ue_mask   = CV_DRAMSTS_DBEERR,
	.ecc_saddr_offset   = CV_ERRADDR_OFST,
	.ecc_daddr_offset   = CV_ERRADDR_OFST,
	.ecc_cecnt_offset   = CV_SBECOUNT_OFST,
	.ecc_uecnt_offset   = CV_DBECOUNT_OFST,
	.ecc_irq_en_offset  = CV_DRAMINTR_OFST,
	.ecc_irq_en_mask    = CV_DRAMINTR_INTREN,
	.ecc_irq_clr_offset = CV_DRAMINTR_OFST,
	.ecc_irq_clr_mask   = (CV_DRAMINTR_INTRCLR | CV_DRAMINTR_INTREN),
	.ecc_cnt_rst_offset = CV_DRAMINTR_OFST,
	.ecc_cnt_rst_mask   = CV_DRAMINTR_INTRCLR,
#ifdef CONFIG_EDAC_DEBUG
	.ce_ue_trgr_offset  = CV_CTLCFG_OFST,
	.ce_set_mask        = CV_CTLCFG_GEN_SB_ERR,
	.ue_set_mask        = CV_CTLCFG_GEN_DB_ERR,
#endif
};

static const struct altr_sdram_prv_data a10_data = {
	.ecc_ctrl_offset    = A10_ECCCTRL1_OFST,
	.ecc_ctl_en_mask    = A10_ECCCTRL1_ECC_EN,
	.ecc_stat_offset    = A10_INTSTAT_OFST,
	.ecc_stat_ce_mask   = A10_INTSTAT_SBEERR,
	.ecc_stat_ue_mask   = A10_INTSTAT_DBEERR,
	.ecc_saddr_offset   = A10_SERRADDR_OFST,
	.ecc_daddr_offset   = A10_DERRADDR_OFST,
	.ecc_irq_en_offset  = A10_ERRINTEN_OFST,
	.ecc_irq_en_mask    = A10_ECC_IRQ_EN_MASK,
	.ecc_irq_clr_offset = A10_INTSTAT_OFST,
	.ecc_irq_clr_mask   = (A10_INTSTAT_SBEERR | A10_INTSTAT_DBEERR),
	.ecc_cnt_rst_offset = A10_ECCCTRL1_OFST,
	.ecc_cnt_rst_mask   = A10_ECC_CNT_RESET_MASK,
#ifdef CONFIG_EDAC_DEBUG
	.ce_ue_trgr_offset  = A10_DIAGINTTEST_OFST,
	.ce_set_mask        = A10_DIAGINT_TSERRA_MASK,
	.ue_set_mask        = A10_DIAGINT_TDERRA_MASK,
#endif
};

static irqreturn_t altr_sdram_mc_err_handler(int irq, void *dev_id)
{
	struct mem_ctl_info *mci = dev_id;
	struct altr_sdram_mc_data *drvdata = mci->pvt_info;
	const struct altr_sdram_prv_data *priv = drvdata->data;
	u32 status, err_count = 1, err_addr;

	regmap_read(drvdata->mc_vbase, priv->ecc_stat_offset, &status);

	if (status & priv->ecc_stat_ue_mask) {
		regmap_read(drvdata->mc_vbase, priv->ecc_daddr_offset,
			    &err_addr);
		if (priv->ecc_uecnt_offset)
			regmap_read(drvdata->mc_vbase, priv->ecc_uecnt_offset,
				    &err_count);
		panic("\nEDAC: [%d Uncorrectable errors @ 0x%08X]\n",
		      err_count, err_addr);
	}
	if (status & priv->ecc_stat_ce_mask) {
		regmap_read(drvdata->mc_vbase, priv->ecc_saddr_offset,
			    &err_addr);
		if (priv->ecc_uecnt_offset)
			regmap_read(drvdata->mc_vbase,  priv->ecc_cecnt_offset,
				    &err_count);
		edac_mc_handle_error(HW_EVENT_ERR_CORRECTED, mci, err_count,
				     err_addr >> PAGE_SHIFT,
				     err_addr & ~PAGE_MASK, 0,
				     0, 0, -1, mci->ctl_name, "");
		/* Clear IRQ to resume */
		regmap_write(drvdata->mc_vbase,	priv->ecc_irq_clr_offset,
			     priv->ecc_irq_clr_mask);

		return IRQ_HANDLED;
	}
	return IRQ_NONE;
}

#ifdef CONFIG_EDAC_DEBUG
static ssize_t altr_sdr_mc_err_inject_write(struct file *file,
					    const char __user *data,
					    size_t count, loff_t *ppos)
{
	struct mem_ctl_info *mci = file->private_data;
	struct altr_sdram_mc_data *drvdata = mci->pvt_info;
	const struct altr_sdram_prv_data *priv = drvdata->data;
	u32 *ptemp;
	dma_addr_t dma_handle;
	u32 reg, read_reg;

	ptemp = dma_alloc_coherent(mci->pdev, 16, &dma_handle, GFP_KERNEL);
	if (!ptemp) {
		dma_free_coherent(mci->pdev, 16, ptemp, dma_handle);
		edac_printk(KERN_ERR, EDAC_MC,
			    "Inject: Buffer Allocation error\n");
		return -ENOMEM;
	}

	regmap_read(drvdata->mc_vbase, priv->ce_ue_trgr_offset,
		    &read_reg);
	read_reg &= ~(priv->ce_set_mask | priv->ue_set_mask);

	/* Error are injected by writing a word while the SBE or DBE
	 * bit in the CTLCFG register is set. Reading the word will
	 * trigger the SBE or DBE error and the corresponding IRQ.
	 */
	if (count == 3) {
		edac_printk(KERN_ALERT, EDAC_MC,
			    "Inject Double bit error\n");
		regmap_write(drvdata->mc_vbase, priv->ce_ue_trgr_offset,
			     (read_reg | priv->ue_set_mask));
	} else {
		edac_printk(KERN_ALERT, EDAC_MC,
			    "Inject Single bit error\n");
		regmap_write(drvdata->mc_vbase,	priv->ce_ue_trgr_offset,
			     (read_reg | priv->ce_set_mask));
	}

	ptemp[0] = 0x5A5A5A5A;
	ptemp[1] = 0xA5A5A5A5;

	/* Clear the error injection bits */
	regmap_write(drvdata->mc_vbase,	priv->ce_ue_trgr_offset, read_reg);
	/* Ensure it has been written out */
	wmb();

	/*
	 * To trigger the error, we need to read the data back
	 * (the data was written with errors above).
	 * The ACCESS_ONCE macros and printk are used to prevent the
	 * the compiler optimizing these reads out.
	 */
	reg = ACCESS_ONCE(ptemp[0]);
	read_reg = ACCESS_ONCE(ptemp[1]);
	/* Force Read */
	rmb();

	edac_printk(KERN_ALERT, EDAC_MC, "Read Data [0x%X, 0x%X]\n",
		    reg, read_reg);

	dma_free_coherent(mci->pdev, 16, ptemp, dma_handle);

	return count;
}

static const struct file_operations altr_sdr_mc_debug_inject_fops = {
	.open = simple_open,
	.write = altr_sdr_mc_err_inject_write,
	.llseek = generic_file_llseek,
};

static void altr_sdr_mc_create_debugfs_nodes(struct mem_ctl_info *mci)
{
	if (mci->debugfs)
		debugfs_create_file("inject_ctrl", S_IWUSR, mci->debugfs, mci,
				    &altr_sdr_mc_debug_inject_fops);
}
#else
static void altr_sdr_mc_create_debugfs_nodes(struct mem_ctl_info *mci)
{}
#endif

/* Get total memory size from Open Firmware DTB */
static unsigned long get_total_mem(void)
{
	struct device_node *np = NULL;
	const unsigned int *reg, *reg_end;
	int len, sw, aw;
	unsigned long start, size, total_mem = 0;

	for_each_node_by_type(np, "memory") {
		aw = of_n_addr_cells(np);
		sw = of_n_size_cells(np);
		reg = (const unsigned int *)of_get_property(np, "reg", &len);
		reg_end = reg + (len / sizeof(u32));

		total_mem = 0;
		do {
			start = of_read_number(reg, aw);
			reg += aw;
			size = of_read_number(reg, sw);
			reg += sw;
			total_mem += size;
		} while (reg < reg_end);
	}
	edac_dbg(0, "total_mem 0x%lx\n", total_mem);
	return total_mem;
}

static const struct of_device_id altr_sdram_ctrl_of_match[] = {
	{ .compatible = "altr,sdram-edac", .data = (void *)&c5_data},
	{ .compatible = "altr,sdram-edac-a10", .data = (void *)&a10_data},
	{},
};
MODULE_DEVICE_TABLE(of, altr_sdram_ctrl_of_match);

static int a10_init(struct regmap *mc_vbase)
{
	if (regmap_update_bits(mc_vbase, A10_INTMODE_OFST,
			       A10_INTMODE_SB_INT, A10_INTMODE_SB_INT)) {
		edac_printk(KERN_ERR, EDAC_MC,
			    "Error setting SB IRQ mode\n");
		return -ENODEV;
	}

	if (regmap_write(mc_vbase, A10_SERRCNTREG_OFST, 1)) {
		edac_printk(KERN_ERR, EDAC_MC,
			    "Error setting trigger count\n");
		return -ENODEV;
	}

	return 0;
}

static int a10_unmask_irq(struct platform_device *pdev, u32 mask)
{
	void __iomem  *sm_base;
	int  ret = 0;

	if (!request_mem_region(A10_SYMAN_INTMASK_CLR, sizeof(u32),
				dev_name(&pdev->dev))) {
		edac_printk(KERN_ERR, EDAC_MC,
			    "Unable to request mem region\n");
		return -EBUSY;
	}

	sm_base = ioremap(A10_SYMAN_INTMASK_CLR, sizeof(u32));
	if (!sm_base) {
		edac_printk(KERN_ERR, EDAC_MC,
			    "Unable to ioremap device\n");

		ret = -ENOMEM;
		goto release;
	}

	iowrite32(mask, sm_base);

	iounmap(sm_base);

release:
	release_mem_region(A10_SYMAN_INTMASK_CLR, sizeof(u32));

	return ret;
}

static int altr_sdram_probe(struct platform_device *pdev)
{
	const struct of_device_id *id;
	struct edac_mc_layer layers[2];
	struct mem_ctl_info *mci;
	struct altr_sdram_mc_data *drvdata;
	const struct altr_sdram_prv_data *priv;
	struct regmap *mc_vbase;
	struct dimm_info *dimm;
	u32 read_reg;
	int irq, irq2, res = 0;
	unsigned long mem_size, irqflags = 0;

	id = of_match_device(altr_sdram_ctrl_of_match, &pdev->dev);
	if (!id)
		return -ENODEV;

	/* Grab the register range from the sdr controller in device tree */
	mc_vbase = syscon_regmap_lookup_by_phandle(pdev->dev.of_node,
						   "altr,sdr-syscon");
	if (IS_ERR(mc_vbase)) {
		edac_printk(KERN_ERR, EDAC_MC,
			    "regmap for altr,sdr-syscon lookup failed.\n");
		return -ENODEV;
	}

	/* Check specific dependencies for the module */
	priv = of_match_node(altr_sdram_ctrl_of_match,
			     pdev->dev.of_node)->data;

	/* Validate the SDRAM controller has ECC enabled */
	if (regmap_read(mc_vbase, priv->ecc_ctrl_offset, &read_reg) ||
	    ((read_reg & priv->ecc_ctl_en_mask) != priv->ecc_ctl_en_mask)) {
		edac_printk(KERN_ERR, EDAC_MC,
			    "No ECC/ECC disabled [0x%08X]\n", read_reg);
		return -ENODEV;
	}

	/* Grab memory size from device tree. */
	mem_size = get_total_mem();
	if (!mem_size) {
		edac_printk(KERN_ERR, EDAC_MC, "Unable to calculate memory size\n");
		return -ENODEV;
	}

	/* Ensure the SDRAM Interrupt is disabled */
	if (regmap_update_bits(mc_vbase, priv->ecc_irq_en_offset,
			       priv->ecc_irq_en_mask, 0)) {
		edac_printk(KERN_ERR, EDAC_MC,
			    "Error disabling SDRAM ECC IRQ\n");
		return -ENODEV;
	}

	/* Toggle to clear the SDRAM Error count */
	if (regmap_update_bits(mc_vbase, priv->ecc_cnt_rst_offset,
			       priv->ecc_cnt_rst_mask,
			       priv->ecc_cnt_rst_mask)) {
		edac_printk(KERN_ERR, EDAC_MC,
			    "Error clearing SDRAM ECC count\n");
		return -ENODEV;
	}

	if (regmap_update_bits(mc_vbase, priv->ecc_cnt_rst_offset,
			       priv->ecc_cnt_rst_mask, 0)) {
		edac_printk(KERN_ERR, EDAC_MC,
			    "Error clearing SDRAM ECC count\n");
		return -ENODEV;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		edac_printk(KERN_ERR, EDAC_MC,
			    "No irq %d in DT\n", irq);
		return -ENODEV;
	}

	/* Arria10 has a 2nd IRQ */
	irq2 = platform_get_irq(pdev, 1);

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
	drvdata->mc_vbase = mc_vbase;
	drvdata->data = priv;
	platform_set_drvdata(pdev, mci);

	if (!devres_open_group(&pdev->dev, NULL, GFP_KERNEL)) {
		edac_printk(KERN_ERR, EDAC_MC,
			    "Unable to get managed device resource\n");
		res = -ENOMEM;
		goto free;
	}

	mci->mtype_cap = MEM_FLAG_DDR3;
	mci->edac_ctl_cap = EDAC_FLAG_NONE | EDAC_FLAG_SECDED;
	mci->edac_cap = EDAC_FLAG_SECDED;
	mci->mod_name = EDAC_MOD_STR;
	mci->mod_ver = EDAC_VERSION;
	mci->ctl_name = dev_name(&pdev->dev);
	mci->scrub_mode = SCRUB_SW_SRC;
	mci->dev_name = dev_name(&pdev->dev);

	dimm = *mci->dimms;
	dimm->nr_pages = ((mem_size - 1) >> PAGE_SHIFT) + 1;
	dimm->grain = 8;
	dimm->dtype = DEV_X8;
	dimm->mtype = MEM_DDR3;
	dimm->edac_mode = EDAC_SECDED;

	res = edac_mc_add_mc(mci);
	if (res < 0)
		goto err;

	/* Only the Arria10 has separate IRQs */
	if (irq2 > 0) {
		/* Arria10 specific initialization */
		res = a10_init(mc_vbase);
		if (res < 0)
			goto err2;

		res = devm_request_irq(&pdev->dev, irq2,
				       altr_sdram_mc_err_handler,
				       IRQF_SHARED, dev_name(&pdev->dev), mci);
		if (res < 0) {
			edac_mc_printk(mci, KERN_ERR,
				       "Unable to request irq %d\n", irq2);
			res = -ENODEV;
			goto err2;
		}

		res = a10_unmask_irq(pdev, A10_DDR0_IRQ_MASK);
		if (res < 0)
			goto err2;

		irqflags = IRQF_SHARED;
	}

	res = devm_request_irq(&pdev->dev, irq, altr_sdram_mc_err_handler,
			       irqflags, dev_name(&pdev->dev), mci);
	if (res < 0) {
		edac_mc_printk(mci, KERN_ERR,
			       "Unable to request irq %d\n", irq);
		res = -ENODEV;
		goto err2;
	}

	/* Infrastructure ready - enable the IRQ */
	if (regmap_update_bits(drvdata->mc_vbase, priv->ecc_irq_en_offset,
			       priv->ecc_irq_en_mask, priv->ecc_irq_en_mask)) {
		edac_mc_printk(mci, KERN_ERR,
			       "Error enabling SDRAM ECC IRQ\n");
		res = -ENODEV;
		goto err2;
	}

	altr_sdr_mc_create_debugfs_nodes(mci);

	devres_close_group(&pdev->dev, NULL);

	return 0;

err2:
	edac_mc_del_mc(&pdev->dev);
err:
	devres_release_group(&pdev->dev, NULL);
free:
	edac_mc_free(mci);
	edac_printk(KERN_ERR, EDAC_MC,
		    "EDAC Probe Failed; Error %d\n", res);

	return res;
}

static int altr_sdram_remove(struct platform_device *pdev)
{
	struct mem_ctl_info *mci = platform_get_drvdata(pdev);

	edac_mc_del_mc(&pdev->dev);
	edac_mc_free(mci);
	platform_set_drvdata(pdev, NULL);

	return 0;
}

/*
 * If you want to suspend, need to disable EDAC by removing it
 * from the device tree or defconfig.
 */
#ifdef CONFIG_PM
static int altr_sdram_prepare(struct device *dev)
{
	pr_err("Suspend not allowed when EDAC is enabled.\n");

	return -EPERM;
}

static const struct dev_pm_ops altr_sdram_pm_ops = {
	.prepare = altr_sdram_prepare,
};
#endif

static struct platform_driver altr_sdram_edac_driver = {
	.probe = altr_sdram_probe,
	.remove = altr_sdram_remove,
	.driver = {
		.name = "altr_sdram_edac",
#ifdef CONFIG_PM
		.pm = &altr_sdram_pm_ops,
#endif
		.of_match_table = altr_sdram_ctrl_of_match,
	},
};

module_platform_driver(altr_sdram_edac_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Thor Thayer");
MODULE_DESCRIPTION("EDAC Driver for Altera SDRAM Controller");
