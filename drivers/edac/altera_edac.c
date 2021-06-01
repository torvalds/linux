// SPDX-License-Identifier: GPL-2.0
/*
 *  Copyright (C) 2017-2018, Intel Corporation. All rights reserved
 *  Copyright Altera Corporation (C) 2014-2016. All rights reserved.
 *  Copyright 2011-2012 Calxeda, Inc.
 */

#include <asm/cacheflush.h>
#include <linux/ctype.h>
#include <linux/delay.h>
#include <linux/edac.h>
#include <linux/firmware/intel/stratix10-smc.h>
#include <linux/genalloc.h>
#include <linux/interrupt.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/kernel.h>
#include <linux/mfd/altera-sysmgr.h>
#include <linux/mfd/syscon.h>
#include <linux/notifier.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/panic_notifier.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/types.h>
#include <linux/uaccess.h>

#include "altera_edac.h"
#include "edac_module.h"

#define EDAC_MOD_STR		"altera_edac"
#define EDAC_DEVICE		"Altera"

#ifdef CONFIG_EDAC_ALTERA_SDRAM
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
	.ce_ue_trgr_offset  = CV_CTLCFG_OFST,
	.ce_set_mask        = CV_CTLCFG_GEN_SB_ERR,
	.ue_set_mask        = CV_CTLCFG_GEN_DB_ERR,
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
	.ce_ue_trgr_offset  = A10_DIAGINTTEST_OFST,
	.ce_set_mask        = A10_DIAGINT_TSERRA_MASK,
	.ue_set_mask        = A10_DIAGINT_TDERRA_MASK,
};

/*********************** EDAC Memory Controller Functions ****************/

/* The SDRAM controller uses the EDAC Memory Controller framework.       */

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
		local_irq_disable();
		regmap_write(drvdata->mc_vbase, priv->ce_ue_trgr_offset,
			     (read_reg | priv->ue_set_mask));
		local_irq_enable();
	} else {
		edac_printk(KERN_ALERT, EDAC_MC,
			    "Inject Single bit error\n");
		local_irq_disable();
		regmap_write(drvdata->mc_vbase,	priv->ce_ue_trgr_offset,
			     (read_reg | priv->ce_set_mask));
		local_irq_enable();
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
	 * The READ_ONCE macros and printk are used to prevent the
	 * the compiler optimizing these reads out.
	 */
	reg = READ_ONCE(ptemp[0]);
	read_reg = READ_ONCE(ptemp[1]);
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
	if (!IS_ENABLED(CONFIG_EDAC_DEBUG))
		return;

	if (!mci->debugfs)
		return;

	edac_debugfs_create_file("altr_trigger", S_IWUSR, mci->debugfs, mci,
				 &altr_sdr_mc_debug_inject_fops);
}

/* Get total memory size from Open Firmware DTB */
static unsigned long get_total_mem(void)
{
	struct device_node *np = NULL;
	struct resource res;
	int ret;
	unsigned long total_mem = 0;

	for_each_node_by_type(np, "memory") {
		ret = of_address_to_resource(np, 0, &res);
		if (ret)
			continue;

		total_mem += resource_size(&res);
	}
	edac_dbg(0, "total_mem 0x%lx\n", total_mem);
	return total_mem;
}

static const struct of_device_id altr_sdram_ctrl_of_match[] = {
	{ .compatible = "altr,sdram-edac", .data = &c5_data},
	{ .compatible = "altr,sdram-edac-a10", .data = &a10_data},
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
	if (of_machine_is_compatible("altr,socfpga-arria10")) {
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

#endif	/* CONFIG_EDAC_ALTERA_SDRAM */

/************************* EDAC Parent Probe *************************/

static const struct of_device_id altr_edac_device_of_match[];

static const struct of_device_id altr_edac_of_match[] = {
	{ .compatible = "altr,socfpga-ecc-manager" },
	{},
};
MODULE_DEVICE_TABLE(of, altr_edac_of_match);

static int altr_edac_probe(struct platform_device *pdev)
{
	of_platform_populate(pdev->dev.of_node, altr_edac_device_of_match,
			     NULL, &pdev->dev);
	return 0;
}

static struct platform_driver altr_edac_driver = {
	.probe =  altr_edac_probe,
	.driver = {
		.name = "socfpga_ecc_manager",
		.of_match_table = altr_edac_of_match,
	},
};
module_platform_driver(altr_edac_driver);

/************************* EDAC Device Functions *************************/

/*
 * EDAC Device Functions (shared between various IPs).
 * The discrete memories use the EDAC Device framework. The probe
 * and error handling functions are very similar between memories
 * so they are shared. The memory allocation and freeing for EDAC
 * trigger testing are different for each memory.
 */

#ifdef CONFIG_EDAC_ALTERA_OCRAM
static const struct edac_device_prv_data ocramecc_data;
#endif
#ifdef CONFIG_EDAC_ALTERA_L2C
static const struct edac_device_prv_data l2ecc_data;
#endif
#ifdef CONFIG_EDAC_ALTERA_OCRAM
static const struct edac_device_prv_data a10_ocramecc_data;
#endif
#ifdef CONFIG_EDAC_ALTERA_L2C
static const struct edac_device_prv_data a10_l2ecc_data;
#endif

static irqreturn_t altr_edac_device_handler(int irq, void *dev_id)
{
	irqreturn_t ret_value = IRQ_NONE;
	struct edac_device_ctl_info *dci = dev_id;
	struct altr_edac_device_dev *drvdata = dci->pvt_info;
	const struct edac_device_prv_data *priv = drvdata->data;

	if (irq == drvdata->sb_irq) {
		if (priv->ce_clear_mask)
			writel(priv->ce_clear_mask, drvdata->base);
		edac_device_handle_ce(dci, 0, 0, drvdata->edac_dev_name);
		ret_value = IRQ_HANDLED;
	} else if (irq == drvdata->db_irq) {
		if (priv->ue_clear_mask)
			writel(priv->ue_clear_mask, drvdata->base);
		edac_device_handle_ue(dci, 0, 0, drvdata->edac_dev_name);
		panic("\nEDAC:ECC_DEVICE[Uncorrectable errors]\n");
		ret_value = IRQ_HANDLED;
	} else {
		WARN_ON(1);
	}

	return ret_value;
}

static ssize_t __maybe_unused
altr_edac_device_trig(struct file *file, const char __user *user_buf,
		      size_t count, loff_t *ppos)

{
	u32 *ptemp, i, error_mask;
	int result = 0;
	u8 trig_type;
	unsigned long flags;
	struct edac_device_ctl_info *edac_dci = file->private_data;
	struct altr_edac_device_dev *drvdata = edac_dci->pvt_info;
	const struct edac_device_prv_data *priv = drvdata->data;
	void *generic_ptr = edac_dci->dev;

	if (!user_buf || get_user(trig_type, user_buf))
		return -EFAULT;

	if (!priv->alloc_mem)
		return -ENOMEM;

	/*
	 * Note that generic_ptr is initialized to the device * but in
	 * some alloc_functions, this is overridden and returns data.
	 */
	ptemp = priv->alloc_mem(priv->trig_alloc_sz, &generic_ptr);
	if (!ptemp) {
		edac_printk(KERN_ERR, EDAC_DEVICE,
			    "Inject: Buffer Allocation error\n");
		return -ENOMEM;
	}

	if (trig_type == ALTR_UE_TRIGGER_CHAR)
		error_mask = priv->ue_set_mask;
	else
		error_mask = priv->ce_set_mask;

	edac_printk(KERN_ALERT, EDAC_DEVICE,
		    "Trigger Error Mask (0x%X)\n", error_mask);

	local_irq_save(flags);
	/* write ECC corrupted data out. */
	for (i = 0; i < (priv->trig_alloc_sz / sizeof(*ptemp)); i++) {
		/* Read data so we're in the correct state */
		rmb();
		if (READ_ONCE(ptemp[i]))
			result = -1;
		/* Toggle Error bit (it is latched), leave ECC enabled */
		writel(error_mask, (drvdata->base + priv->set_err_ofst));
		writel(priv->ecc_enable_mask, (drvdata->base +
					       priv->set_err_ofst));
		ptemp[i] = i;
	}
	/* Ensure it has been written out */
	wmb();
	local_irq_restore(flags);

	if (result)
		edac_printk(KERN_ERR, EDAC_DEVICE, "Mem Not Cleared\n");

	/* Read out written data. ECC error caused here */
	for (i = 0; i < ALTR_TRIGGER_READ_WRD_CNT; i++)
		if (READ_ONCE(ptemp[i]) != i)
			edac_printk(KERN_ERR, EDAC_DEVICE,
				    "Read doesn't match written data\n");

	if (priv->free_mem)
		priv->free_mem(ptemp, priv->trig_alloc_sz, generic_ptr);

	return count;
}

static const struct file_operations altr_edac_device_inject_fops __maybe_unused = {
	.open = simple_open,
	.write = altr_edac_device_trig,
	.llseek = generic_file_llseek,
};

static ssize_t __maybe_unused
altr_edac_a10_device_trig(struct file *file, const char __user *user_buf,
			  size_t count, loff_t *ppos);

static const struct file_operations altr_edac_a10_device_inject_fops __maybe_unused = {
	.open = simple_open,
	.write = altr_edac_a10_device_trig,
	.llseek = generic_file_llseek,
};

static ssize_t __maybe_unused
altr_edac_a10_device_trig2(struct file *file, const char __user *user_buf,
			   size_t count, loff_t *ppos);

static const struct file_operations altr_edac_a10_device_inject2_fops __maybe_unused = {
	.open = simple_open,
	.write = altr_edac_a10_device_trig2,
	.llseek = generic_file_llseek,
};

static void altr_create_edacdev_dbgfs(struct edac_device_ctl_info *edac_dci,
				      const struct edac_device_prv_data *priv)
{
	struct altr_edac_device_dev *drvdata = edac_dci->pvt_info;

	if (!IS_ENABLED(CONFIG_EDAC_DEBUG))
		return;

	drvdata->debugfs_dir = edac_debugfs_create_dir(drvdata->edac_dev_name);
	if (!drvdata->debugfs_dir)
		return;

	if (!edac_debugfs_create_file("altr_trigger", S_IWUSR,
				      drvdata->debugfs_dir, edac_dci,
				      priv->inject_fops))
		debugfs_remove_recursive(drvdata->debugfs_dir);
}

static const struct of_device_id altr_edac_device_of_match[] = {
#ifdef CONFIG_EDAC_ALTERA_L2C
	{ .compatible = "altr,socfpga-l2-ecc", .data = &l2ecc_data },
#endif
#ifdef CONFIG_EDAC_ALTERA_OCRAM
	{ .compatible = "altr,socfpga-ocram-ecc", .data = &ocramecc_data },
#endif
	{},
};
MODULE_DEVICE_TABLE(of, altr_edac_device_of_match);

/*
 * altr_edac_device_probe()
 *	This is a generic EDAC device driver that will support
 *	various Altera memory devices such as the L2 cache ECC and
 *	OCRAM ECC as well as the memories for other peripherals.
 *	Module specific initialization is done by passing the
 *	function index in the device tree.
 */
static int altr_edac_device_probe(struct platform_device *pdev)
{
	struct edac_device_ctl_info *dci;
	struct altr_edac_device_dev *drvdata;
	struct resource *r;
	int res = 0;
	struct device_node *np = pdev->dev.of_node;
	char *ecc_name = (char *)np->name;
	static int dev_instance;

	if (!devres_open_group(&pdev->dev, NULL, GFP_KERNEL)) {
		edac_printk(KERN_ERR, EDAC_DEVICE,
			    "Unable to open devm\n");
		return -ENOMEM;
	}

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!r) {
		edac_printk(KERN_ERR, EDAC_DEVICE,
			    "Unable to get mem resource\n");
		res = -ENODEV;
		goto fail;
	}

	if (!devm_request_mem_region(&pdev->dev, r->start, resource_size(r),
				     dev_name(&pdev->dev))) {
		edac_printk(KERN_ERR, EDAC_DEVICE,
			    "%s:Error requesting mem region\n", ecc_name);
		res = -EBUSY;
		goto fail;
	}

	dci = edac_device_alloc_ctl_info(sizeof(*drvdata), ecc_name,
					 1, ecc_name, 1, 0, NULL, 0,
					 dev_instance++);

	if (!dci) {
		edac_printk(KERN_ERR, EDAC_DEVICE,
			    "%s: Unable to allocate EDAC device\n", ecc_name);
		res = -ENOMEM;
		goto fail;
	}

	drvdata = dci->pvt_info;
	dci->dev = &pdev->dev;
	platform_set_drvdata(pdev, dci);
	drvdata->edac_dev_name = ecc_name;

	drvdata->base = devm_ioremap(&pdev->dev, r->start, resource_size(r));
	if (!drvdata->base) {
		res = -ENOMEM;
		goto fail1;
	}

	/* Get driver specific data for this EDAC device */
	drvdata->data = of_match_node(altr_edac_device_of_match, np)->data;

	/* Check specific dependencies for the module */
	if (drvdata->data->setup) {
		res = drvdata->data->setup(drvdata);
		if (res)
			goto fail1;
	}

	drvdata->sb_irq = platform_get_irq(pdev, 0);
	res = devm_request_irq(&pdev->dev, drvdata->sb_irq,
			       altr_edac_device_handler,
			       0, dev_name(&pdev->dev), dci);
	if (res)
		goto fail1;

	drvdata->db_irq = platform_get_irq(pdev, 1);
	res = devm_request_irq(&pdev->dev, drvdata->db_irq,
			       altr_edac_device_handler,
			       0, dev_name(&pdev->dev), dci);
	if (res)
		goto fail1;

	dci->mod_name = "Altera ECC Manager";
	dci->dev_name = drvdata->edac_dev_name;

	res = edac_device_add_device(dci);
	if (res)
		goto fail1;

	altr_create_edacdev_dbgfs(dci, drvdata->data);

	devres_close_group(&pdev->dev, NULL);

	return 0;

fail1:
	edac_device_free_ctl_info(dci);
fail:
	devres_release_group(&pdev->dev, NULL);
	edac_printk(KERN_ERR, EDAC_DEVICE,
		    "%s:Error setting up EDAC device: %d\n", ecc_name, res);

	return res;
}

static int altr_edac_device_remove(struct platform_device *pdev)
{
	struct edac_device_ctl_info *dci = platform_get_drvdata(pdev);
	struct altr_edac_device_dev *drvdata = dci->pvt_info;

	debugfs_remove_recursive(drvdata->debugfs_dir);
	edac_device_del_device(&pdev->dev);
	edac_device_free_ctl_info(dci);

	return 0;
}

static struct platform_driver altr_edac_device_driver = {
	.probe =  altr_edac_device_probe,
	.remove = altr_edac_device_remove,
	.driver = {
		.name = "altr_edac_device",
		.of_match_table = altr_edac_device_of_match,
	},
};
module_platform_driver(altr_edac_device_driver);

/******************* Arria10 Device ECC Shared Functions *****************/

/*
 *  Test for memory's ECC dependencies upon entry because platform specific
 *  startup should have initialized the memory and enabled the ECC.
 *  Can't turn on ECC here because accessing un-initialized memory will
 *  cause CE/UE errors possibly causing an ABORT.
 */
static int __maybe_unused
altr_check_ecc_deps(struct altr_edac_device_dev *device)
{
	void __iomem  *base = device->base;
	const struct edac_device_prv_data *prv = device->data;

	if (readl(base + prv->ecc_en_ofst) & prv->ecc_enable_mask)
		return 0;

	edac_printk(KERN_ERR, EDAC_DEVICE,
		    "%s: No ECC present or ECC disabled.\n",
		    device->edac_dev_name);
	return -ENODEV;
}

static irqreturn_t __maybe_unused altr_edac_a10_ecc_irq(int irq, void *dev_id)
{
	struct altr_edac_device_dev *dci = dev_id;
	void __iomem  *base = dci->base;

	if (irq == dci->sb_irq) {
		writel(ALTR_A10_ECC_SERRPENA,
		       base + ALTR_A10_ECC_INTSTAT_OFST);
		edac_device_handle_ce(dci->edac_dev, 0, 0, dci->edac_dev_name);

		return IRQ_HANDLED;
	} else if (irq == dci->db_irq) {
		writel(ALTR_A10_ECC_DERRPENA,
		       base + ALTR_A10_ECC_INTSTAT_OFST);
		edac_device_handle_ue(dci->edac_dev, 0, 0, dci->edac_dev_name);
		if (dci->data->panic)
			panic("\nEDAC:ECC_DEVICE[Uncorrectable errors]\n");

		return IRQ_HANDLED;
	}

	WARN_ON(1);

	return IRQ_NONE;
}

/******************* Arria10 Memory Buffer Functions *********************/

static inline int a10_get_irq_mask(struct device_node *np)
{
	int irq;
	const u32 *handle = of_get_property(np, "interrupts", NULL);

	if (!handle)
		return -ENODEV;
	irq = be32_to_cpup(handle);
	return irq;
}

static inline void ecc_set_bits(u32 bit_mask, void __iomem *ioaddr)
{
	u32 value = readl(ioaddr);

	value |= bit_mask;
	writel(value, ioaddr);
}

static inline void ecc_clear_bits(u32 bit_mask, void __iomem *ioaddr)
{
	u32 value = readl(ioaddr);

	value &= ~bit_mask;
	writel(value, ioaddr);
}

static inline int ecc_test_bits(u32 bit_mask, void __iomem *ioaddr)
{
	u32 value = readl(ioaddr);

	return (value & bit_mask) ? 1 : 0;
}

/*
 * This function uses the memory initialization block in the Arria10 ECC
 * controller to initialize/clear the entire memory data and ECC data.
 */
static int __maybe_unused altr_init_memory_port(void __iomem *ioaddr, int port)
{
	int limit = ALTR_A10_ECC_INIT_WATCHDOG_10US;
	u32 init_mask, stat_mask, clear_mask;
	int ret = 0;

	if (port) {
		init_mask = ALTR_A10_ECC_INITB;
		stat_mask = ALTR_A10_ECC_INITCOMPLETEB;
		clear_mask = ALTR_A10_ECC_ERRPENB_MASK;
	} else {
		init_mask = ALTR_A10_ECC_INITA;
		stat_mask = ALTR_A10_ECC_INITCOMPLETEA;
		clear_mask = ALTR_A10_ECC_ERRPENA_MASK;
	}

	ecc_set_bits(init_mask, (ioaddr + ALTR_A10_ECC_CTRL_OFST));
	while (limit--) {
		if (ecc_test_bits(stat_mask,
				  (ioaddr + ALTR_A10_ECC_INITSTAT_OFST)))
			break;
		udelay(1);
	}
	if (limit < 0)
		ret = -EBUSY;

	/* Clear any pending ECC interrupts */
	writel(clear_mask, (ioaddr + ALTR_A10_ECC_INTSTAT_OFST));

	return ret;
}

static __init int __maybe_unused
altr_init_a10_ecc_block(struct device_node *np, u32 irq_mask,
			u32 ecc_ctrl_en_mask, bool dual_port)
{
	int ret = 0;
	void __iomem *ecc_block_base;
	struct regmap *ecc_mgr_map;
	char *ecc_name;
	struct device_node *np_eccmgr;

	ecc_name = (char *)np->name;

	/* Get the ECC Manager - parent of the device EDACs */
	np_eccmgr = of_get_parent(np);

	ecc_mgr_map =
		altr_sysmgr_regmap_lookup_by_phandle(np_eccmgr,
						     "altr,sysmgr-syscon");

	of_node_put(np_eccmgr);
	if (IS_ERR(ecc_mgr_map)) {
		edac_printk(KERN_ERR, EDAC_DEVICE,
			    "Unable to get syscon altr,sysmgr-syscon\n");
		return -ENODEV;
	}

	/* Map the ECC Block */
	ecc_block_base = of_iomap(np, 0);
	if (!ecc_block_base) {
		edac_printk(KERN_ERR, EDAC_DEVICE,
			    "Unable to map %s ECC block\n", ecc_name);
		return -ENODEV;
	}

	/* Disable ECC */
	regmap_write(ecc_mgr_map, A10_SYSMGR_ECC_INTMASK_SET_OFST, irq_mask);
	writel(ALTR_A10_ECC_SERRINTEN,
	       (ecc_block_base + ALTR_A10_ECC_ERRINTENR_OFST));
	ecc_clear_bits(ecc_ctrl_en_mask,
		       (ecc_block_base + ALTR_A10_ECC_CTRL_OFST));
	/* Ensure all writes complete */
	wmb();
	/* Use HW initialization block to initialize memory for ECC */
	ret = altr_init_memory_port(ecc_block_base, 0);
	if (ret) {
		edac_printk(KERN_ERR, EDAC_DEVICE,
			    "ECC: cannot init %s PORTA memory\n", ecc_name);
		goto out;
	}

	if (dual_port) {
		ret = altr_init_memory_port(ecc_block_base, 1);
		if (ret) {
			edac_printk(KERN_ERR, EDAC_DEVICE,
				    "ECC: cannot init %s PORTB memory\n",
				    ecc_name);
			goto out;
		}
	}

	/* Interrupt mode set to every SBERR */
	regmap_write(ecc_mgr_map, ALTR_A10_ECC_INTMODE_OFST,
		     ALTR_A10_ECC_INTMODE);
	/* Enable ECC */
	ecc_set_bits(ecc_ctrl_en_mask, (ecc_block_base +
					ALTR_A10_ECC_CTRL_OFST));
	writel(ALTR_A10_ECC_SERRINTEN,
	       (ecc_block_base + ALTR_A10_ECC_ERRINTENS_OFST));
	regmap_write(ecc_mgr_map, A10_SYSMGR_ECC_INTMASK_CLR_OFST, irq_mask);
	/* Ensure all writes complete */
	wmb();
out:
	iounmap(ecc_block_base);
	return ret;
}

static int validate_parent_available(struct device_node *np);
static const struct of_device_id altr_edac_a10_device_of_match[];
static int __init __maybe_unused altr_init_a10_ecc_device_type(char *compat)
{
	int irq;
	struct device_node *child, *np;

	np = of_find_compatible_node(NULL, NULL,
				     "altr,socfpga-a10-ecc-manager");
	if (!np) {
		edac_printk(KERN_ERR, EDAC_DEVICE, "ECC Manager not found\n");
		return -ENODEV;
	}

	for_each_child_of_node(np, child) {
		const struct of_device_id *pdev_id;
		const struct edac_device_prv_data *prv;

		if (!of_device_is_available(child))
			continue;
		if (!of_device_is_compatible(child, compat))
			continue;

		if (validate_parent_available(child))
			continue;

		irq = a10_get_irq_mask(child);
		if (irq < 0)
			continue;

		/* Get matching node and check for valid result */
		pdev_id = of_match_node(altr_edac_a10_device_of_match, child);
		if (IS_ERR_OR_NULL(pdev_id))
			continue;

		/* Validate private data pointer before dereferencing */
		prv = pdev_id->data;
		if (!prv)
			continue;

		altr_init_a10_ecc_block(child, BIT(irq),
					prv->ecc_enable_mask, 0);
	}

	of_node_put(np);
	return 0;
}

/*********************** SDRAM EDAC Device Functions *********************/

#ifdef CONFIG_EDAC_ALTERA_SDRAM

static const struct edac_device_prv_data s10_sdramecc_data = {
	.setup = altr_check_ecc_deps,
	.ce_clear_mask = ALTR_S10_ECC_SERRPENA,
	.ue_clear_mask = ALTR_S10_ECC_DERRPENA,
	.ecc_enable_mask = ALTR_S10_ECC_EN,
	.ecc_en_ofst = ALTR_S10_ECC_CTRL_SDRAM_OFST,
	.ce_set_mask = ALTR_S10_ECC_TSERRA,
	.ue_set_mask = ALTR_S10_ECC_TDERRA,
	.set_err_ofst = ALTR_S10_ECC_INTTEST_OFST,
	.ecc_irq_handler = altr_edac_a10_ecc_irq,
	.inject_fops = &altr_edac_a10_device_inject_fops,
};
#endif /* CONFIG_EDAC_ALTERA_SDRAM */

/*********************** OCRAM EDAC Device Functions *********************/

#ifdef CONFIG_EDAC_ALTERA_OCRAM

static void *ocram_alloc_mem(size_t size, void **other)
{
	struct device_node *np;
	struct gen_pool *gp;
	void *sram_addr;

	np = of_find_compatible_node(NULL, NULL, "altr,socfpga-ocram-ecc");
	if (!np)
		return NULL;

	gp = of_gen_pool_get(np, "iram", 0);
	of_node_put(np);
	if (!gp)
		return NULL;

	sram_addr = (void *)gen_pool_alloc(gp, size);
	if (!sram_addr)
		return NULL;

	memset(sram_addr, 0, size);
	/* Ensure data is written out */
	wmb();

	/* Remember this handle for freeing  later */
	*other = gp;

	return sram_addr;
}

static void ocram_free_mem(void *p, size_t size, void *other)
{
	gen_pool_free((struct gen_pool *)other, (unsigned long)p, size);
}

static const struct edac_device_prv_data ocramecc_data = {
	.setup = altr_check_ecc_deps,
	.ce_clear_mask = (ALTR_OCR_ECC_EN | ALTR_OCR_ECC_SERR),
	.ue_clear_mask = (ALTR_OCR_ECC_EN | ALTR_OCR_ECC_DERR),
	.alloc_mem = ocram_alloc_mem,
	.free_mem = ocram_free_mem,
	.ecc_enable_mask = ALTR_OCR_ECC_EN,
	.ecc_en_ofst = ALTR_OCR_ECC_REG_OFFSET,
	.ce_set_mask = (ALTR_OCR_ECC_EN | ALTR_OCR_ECC_INJS),
	.ue_set_mask = (ALTR_OCR_ECC_EN | ALTR_OCR_ECC_INJD),
	.set_err_ofst = ALTR_OCR_ECC_REG_OFFSET,
	.trig_alloc_sz = ALTR_TRIG_OCRAM_BYTE_SIZE,
	.inject_fops = &altr_edac_device_inject_fops,
};

static int __maybe_unused
altr_check_ocram_deps_init(struct altr_edac_device_dev *device)
{
	void __iomem  *base = device->base;
	int ret;

	ret = altr_check_ecc_deps(device);
	if (ret)
		return ret;

	/* Verify OCRAM has been initialized */
	if (!ecc_test_bits(ALTR_A10_ECC_INITCOMPLETEA,
			   (base + ALTR_A10_ECC_INITSTAT_OFST)))
		return -ENODEV;

	/* Enable IRQ on Single Bit Error */
	writel(ALTR_A10_ECC_SERRINTEN, (base + ALTR_A10_ECC_ERRINTENS_OFST));
	/* Ensure all writes complete */
	wmb();

	return 0;
}

static const struct edac_device_prv_data a10_ocramecc_data = {
	.setup = altr_check_ocram_deps_init,
	.ce_clear_mask = ALTR_A10_ECC_SERRPENA,
	.ue_clear_mask = ALTR_A10_ECC_DERRPENA,
	.irq_status_mask = A10_SYSMGR_ECC_INTSTAT_OCRAM,
	.ecc_enable_mask = ALTR_A10_OCRAM_ECC_EN_CTL,
	.ecc_en_ofst = ALTR_A10_ECC_CTRL_OFST,
	.ce_set_mask = ALTR_A10_ECC_TSERRA,
	.ue_set_mask = ALTR_A10_ECC_TDERRA,
	.set_err_ofst = ALTR_A10_ECC_INTTEST_OFST,
	.ecc_irq_handler = altr_edac_a10_ecc_irq,
	.inject_fops = &altr_edac_a10_device_inject2_fops,
	/*
	 * OCRAM panic on uncorrectable error because sleep/resume
	 * functions and FPGA contents are stored in OCRAM. Prefer
	 * a kernel panic over executing/loading corrupted data.
	 */
	.panic = true,
};

#endif	/* CONFIG_EDAC_ALTERA_OCRAM */

/********************* L2 Cache EDAC Device Functions ********************/

#ifdef CONFIG_EDAC_ALTERA_L2C

static void *l2_alloc_mem(size_t size, void **other)
{
	struct device *dev = *other;
	void *ptemp = devm_kzalloc(dev, size, GFP_KERNEL);

	if (!ptemp)
		return NULL;

	/* Make sure everything is written out */
	wmb();

	/*
	 * Clean all cache levels up to LoC (includes L2)
	 * This ensures the corrupted data is written into
	 * L2 cache for readback test (which causes ECC error).
	 */
	flush_cache_all();

	return ptemp;
}

static void l2_free_mem(void *p, size_t size, void *other)
{
	struct device *dev = other;

	if (dev && p)
		devm_kfree(dev, p);
}

/*
 * altr_l2_check_deps()
 *	Test for L2 cache ECC dependencies upon entry because
 *	platform specific startup should have initialized the L2
 *	memory and enabled the ECC.
 *	Bail if ECC is not enabled.
 *	Note that L2 Cache Enable is forced at build time.
 */
static int altr_l2_check_deps(struct altr_edac_device_dev *device)
{
	void __iomem *base = device->base;
	const struct edac_device_prv_data *prv = device->data;

	if ((readl(base) & prv->ecc_enable_mask) ==
	     prv->ecc_enable_mask)
		return 0;

	edac_printk(KERN_ERR, EDAC_DEVICE,
		    "L2: No ECC present, or ECC disabled\n");
	return -ENODEV;
}

static irqreturn_t altr_edac_a10_l2_irq(int irq, void *dev_id)
{
	struct altr_edac_device_dev *dci = dev_id;

	if (irq == dci->sb_irq) {
		regmap_write(dci->edac->ecc_mgr_map,
			     A10_SYSGMR_MPU_CLEAR_L2_ECC_OFST,
			     A10_SYSGMR_MPU_CLEAR_L2_ECC_SB);
		edac_device_handle_ce(dci->edac_dev, 0, 0, dci->edac_dev_name);

		return IRQ_HANDLED;
	} else if (irq == dci->db_irq) {
		regmap_write(dci->edac->ecc_mgr_map,
			     A10_SYSGMR_MPU_CLEAR_L2_ECC_OFST,
			     A10_SYSGMR_MPU_CLEAR_L2_ECC_MB);
		edac_device_handle_ue(dci->edac_dev, 0, 0, dci->edac_dev_name);
		panic("\nEDAC:ECC_DEVICE[Uncorrectable errors]\n");

		return IRQ_HANDLED;
	}

	WARN_ON(1);

	return IRQ_NONE;
}

static const struct edac_device_prv_data l2ecc_data = {
	.setup = altr_l2_check_deps,
	.ce_clear_mask = 0,
	.ue_clear_mask = 0,
	.alloc_mem = l2_alloc_mem,
	.free_mem = l2_free_mem,
	.ecc_enable_mask = ALTR_L2_ECC_EN,
	.ce_set_mask = (ALTR_L2_ECC_EN | ALTR_L2_ECC_INJS),
	.ue_set_mask = (ALTR_L2_ECC_EN | ALTR_L2_ECC_INJD),
	.set_err_ofst = ALTR_L2_ECC_REG_OFFSET,
	.trig_alloc_sz = ALTR_TRIG_L2C_BYTE_SIZE,
	.inject_fops = &altr_edac_device_inject_fops,
};

static const struct edac_device_prv_data a10_l2ecc_data = {
	.setup = altr_l2_check_deps,
	.ce_clear_mask = ALTR_A10_L2_ECC_SERR_CLR,
	.ue_clear_mask = ALTR_A10_L2_ECC_MERR_CLR,
	.irq_status_mask = A10_SYSMGR_ECC_INTSTAT_L2,
	.alloc_mem = l2_alloc_mem,
	.free_mem = l2_free_mem,
	.ecc_enable_mask = ALTR_A10_L2_ECC_EN_CTL,
	.ce_set_mask = ALTR_A10_L2_ECC_CE_INJ_MASK,
	.ue_set_mask = ALTR_A10_L2_ECC_UE_INJ_MASK,
	.set_err_ofst = ALTR_A10_L2_ECC_INJ_OFST,
	.ecc_irq_handler = altr_edac_a10_l2_irq,
	.trig_alloc_sz = ALTR_TRIG_L2C_BYTE_SIZE,
	.inject_fops = &altr_edac_device_inject_fops,
};

#endif	/* CONFIG_EDAC_ALTERA_L2C */

/********************* Ethernet Device Functions ********************/

#ifdef CONFIG_EDAC_ALTERA_ETHERNET

static int __init socfpga_init_ethernet_ecc(struct altr_edac_device_dev *dev)
{
	int ret;

	ret = altr_init_a10_ecc_device_type("altr,socfpga-eth-mac-ecc");
	if (ret)
		return ret;

	return altr_check_ecc_deps(dev);
}

static const struct edac_device_prv_data a10_enetecc_data = {
	.setup = socfpga_init_ethernet_ecc,
	.ce_clear_mask = ALTR_A10_ECC_SERRPENA,
	.ue_clear_mask = ALTR_A10_ECC_DERRPENA,
	.ecc_enable_mask = ALTR_A10_COMMON_ECC_EN_CTL,
	.ecc_en_ofst = ALTR_A10_ECC_CTRL_OFST,
	.ce_set_mask = ALTR_A10_ECC_TSERRA,
	.ue_set_mask = ALTR_A10_ECC_TDERRA,
	.set_err_ofst = ALTR_A10_ECC_INTTEST_OFST,
	.ecc_irq_handler = altr_edac_a10_ecc_irq,
	.inject_fops = &altr_edac_a10_device_inject2_fops,
};

#endif	/* CONFIG_EDAC_ALTERA_ETHERNET */

/********************** NAND Device Functions **********************/

#ifdef CONFIG_EDAC_ALTERA_NAND

static int __init socfpga_init_nand_ecc(struct altr_edac_device_dev *device)
{
	int ret;

	ret = altr_init_a10_ecc_device_type("altr,socfpga-nand-ecc");
	if (ret)
		return ret;

	return altr_check_ecc_deps(device);
}

static const struct edac_device_prv_data a10_nandecc_data = {
	.setup = socfpga_init_nand_ecc,
	.ce_clear_mask = ALTR_A10_ECC_SERRPENA,
	.ue_clear_mask = ALTR_A10_ECC_DERRPENA,
	.ecc_enable_mask = ALTR_A10_COMMON_ECC_EN_CTL,
	.ecc_en_ofst = ALTR_A10_ECC_CTRL_OFST,
	.ce_set_mask = ALTR_A10_ECC_TSERRA,
	.ue_set_mask = ALTR_A10_ECC_TDERRA,
	.set_err_ofst = ALTR_A10_ECC_INTTEST_OFST,
	.ecc_irq_handler = altr_edac_a10_ecc_irq,
	.inject_fops = &altr_edac_a10_device_inject_fops,
};

#endif	/* CONFIG_EDAC_ALTERA_NAND */

/********************** DMA Device Functions **********************/

#ifdef CONFIG_EDAC_ALTERA_DMA

static int __init socfpga_init_dma_ecc(struct altr_edac_device_dev *device)
{
	int ret;

	ret = altr_init_a10_ecc_device_type("altr,socfpga-dma-ecc");
	if (ret)
		return ret;

	return altr_check_ecc_deps(device);
}

static const struct edac_device_prv_data a10_dmaecc_data = {
	.setup = socfpga_init_dma_ecc,
	.ce_clear_mask = ALTR_A10_ECC_SERRPENA,
	.ue_clear_mask = ALTR_A10_ECC_DERRPENA,
	.ecc_enable_mask = ALTR_A10_COMMON_ECC_EN_CTL,
	.ecc_en_ofst = ALTR_A10_ECC_CTRL_OFST,
	.ce_set_mask = ALTR_A10_ECC_TSERRA,
	.ue_set_mask = ALTR_A10_ECC_TDERRA,
	.set_err_ofst = ALTR_A10_ECC_INTTEST_OFST,
	.ecc_irq_handler = altr_edac_a10_ecc_irq,
	.inject_fops = &altr_edac_a10_device_inject_fops,
};

#endif	/* CONFIG_EDAC_ALTERA_DMA */

/********************** USB Device Functions **********************/

#ifdef CONFIG_EDAC_ALTERA_USB

static int __init socfpga_init_usb_ecc(struct altr_edac_device_dev *device)
{
	int ret;

	ret = altr_init_a10_ecc_device_type("altr,socfpga-usb-ecc");
	if (ret)
		return ret;

	return altr_check_ecc_deps(device);
}

static const struct edac_device_prv_data a10_usbecc_data = {
	.setup = socfpga_init_usb_ecc,
	.ce_clear_mask = ALTR_A10_ECC_SERRPENA,
	.ue_clear_mask = ALTR_A10_ECC_DERRPENA,
	.ecc_enable_mask = ALTR_A10_COMMON_ECC_EN_CTL,
	.ecc_en_ofst = ALTR_A10_ECC_CTRL_OFST,
	.ce_set_mask = ALTR_A10_ECC_TSERRA,
	.ue_set_mask = ALTR_A10_ECC_TDERRA,
	.set_err_ofst = ALTR_A10_ECC_INTTEST_OFST,
	.ecc_irq_handler = altr_edac_a10_ecc_irq,
	.inject_fops = &altr_edac_a10_device_inject2_fops,
};

#endif	/* CONFIG_EDAC_ALTERA_USB */

/********************** QSPI Device Functions **********************/

#ifdef CONFIG_EDAC_ALTERA_QSPI

static int __init socfpga_init_qspi_ecc(struct altr_edac_device_dev *device)
{
	int ret;

	ret = altr_init_a10_ecc_device_type("altr,socfpga-qspi-ecc");
	if (ret)
		return ret;

	return altr_check_ecc_deps(device);
}

static const struct edac_device_prv_data a10_qspiecc_data = {
	.setup = socfpga_init_qspi_ecc,
	.ce_clear_mask = ALTR_A10_ECC_SERRPENA,
	.ue_clear_mask = ALTR_A10_ECC_DERRPENA,
	.ecc_enable_mask = ALTR_A10_COMMON_ECC_EN_CTL,
	.ecc_en_ofst = ALTR_A10_ECC_CTRL_OFST,
	.ce_set_mask = ALTR_A10_ECC_TSERRA,
	.ue_set_mask = ALTR_A10_ECC_TDERRA,
	.set_err_ofst = ALTR_A10_ECC_INTTEST_OFST,
	.ecc_irq_handler = altr_edac_a10_ecc_irq,
	.inject_fops = &altr_edac_a10_device_inject_fops,
};

#endif	/* CONFIG_EDAC_ALTERA_QSPI */

/********************* SDMMC Device Functions **********************/

#ifdef CONFIG_EDAC_ALTERA_SDMMC

static const struct edac_device_prv_data a10_sdmmceccb_data;
static int altr_portb_setup(struct altr_edac_device_dev *device)
{
	struct edac_device_ctl_info *dci;
	struct altr_edac_device_dev *altdev;
	char *ecc_name = "sdmmcb-ecc";
	int edac_idx, rc;
	struct device_node *np;
	const struct edac_device_prv_data *prv = &a10_sdmmceccb_data;

	rc = altr_check_ecc_deps(device);
	if (rc)
		return rc;

	np = of_find_compatible_node(NULL, NULL, "altr,socfpga-sdmmc-ecc");
	if (!np) {
		edac_printk(KERN_WARNING, EDAC_DEVICE, "SDMMC node not found\n");
		return -ENODEV;
	}

	/* Create the PortB EDAC device */
	edac_idx = edac_device_alloc_index();
	dci = edac_device_alloc_ctl_info(sizeof(*altdev), ecc_name, 1,
					 ecc_name, 1, 0, NULL, 0, edac_idx);
	if (!dci) {
		edac_printk(KERN_ERR, EDAC_DEVICE,
			    "%s: Unable to allocate PortB EDAC device\n",
			    ecc_name);
		return -ENOMEM;
	}

	/* Initialize the PortB EDAC device structure from PortA structure */
	altdev = dci->pvt_info;
	*altdev = *device;

	if (!devres_open_group(&altdev->ddev, altr_portb_setup, GFP_KERNEL))
		return -ENOMEM;

	/* Update PortB specific values */
	altdev->edac_dev_name = ecc_name;
	altdev->edac_idx = edac_idx;
	altdev->edac_dev = dci;
	altdev->data = prv;
	dci->dev = &altdev->ddev;
	dci->ctl_name = "Altera ECC Manager";
	dci->mod_name = ecc_name;
	dci->dev_name = ecc_name;

	/*
	 * Update the PortB IRQs - A10 has 4, S10 has 2, Index accordingly
	 *
	 * FIXME: Instead of ifdefs with different architectures the driver
	 *        should properly use compatibles.
	 */
#ifdef CONFIG_64BIT
	altdev->sb_irq = irq_of_parse_and_map(np, 1);
#else
	altdev->sb_irq = irq_of_parse_and_map(np, 2);
#endif
	if (!altdev->sb_irq) {
		edac_printk(KERN_ERR, EDAC_DEVICE, "Error PortB SBIRQ alloc\n");
		rc = -ENODEV;
		goto err_release_group_1;
	}
	rc = devm_request_irq(&altdev->ddev, altdev->sb_irq,
			      prv->ecc_irq_handler,
			      IRQF_ONESHOT | IRQF_TRIGGER_HIGH,
			      ecc_name, altdev);
	if (rc) {
		edac_printk(KERN_ERR, EDAC_DEVICE, "PortB SBERR IRQ error\n");
		goto err_release_group_1;
	}

#ifdef CONFIG_64BIT
	/* Use IRQ to determine SError origin instead of assigning IRQ */
	rc = of_property_read_u32_index(np, "interrupts", 1, &altdev->db_irq);
	if (rc) {
		edac_printk(KERN_ERR, EDAC_DEVICE,
			    "Error PortB DBIRQ alloc\n");
		goto err_release_group_1;
	}
#else
	altdev->db_irq = irq_of_parse_and_map(np, 3);
	if (!altdev->db_irq) {
		edac_printk(KERN_ERR, EDAC_DEVICE, "Error PortB DBIRQ alloc\n");
		rc = -ENODEV;
		goto err_release_group_1;
	}
	rc = devm_request_irq(&altdev->ddev, altdev->db_irq,
			      prv->ecc_irq_handler,
			      IRQF_ONESHOT | IRQF_TRIGGER_HIGH,
			      ecc_name, altdev);
	if (rc) {
		edac_printk(KERN_ERR, EDAC_DEVICE, "PortB DBERR IRQ error\n");
		goto err_release_group_1;
	}
#endif

	rc = edac_device_add_device(dci);
	if (rc) {
		edac_printk(KERN_ERR, EDAC_DEVICE,
			    "edac_device_add_device portB failed\n");
		rc = -ENOMEM;
		goto err_release_group_1;
	}
	altr_create_edacdev_dbgfs(dci, prv);

	list_add(&altdev->next, &altdev->edac->a10_ecc_devices);

	devres_remove_group(&altdev->ddev, altr_portb_setup);

	return 0;

err_release_group_1:
	edac_device_free_ctl_info(dci);
	devres_release_group(&altdev->ddev, altr_portb_setup);
	edac_printk(KERN_ERR, EDAC_DEVICE,
		    "%s:Error setting up EDAC device: %d\n", ecc_name, rc);
	return rc;
}

static int __init socfpga_init_sdmmc_ecc(struct altr_edac_device_dev *device)
{
	int rc = -ENODEV;
	struct device_node *child;

	child = of_find_compatible_node(NULL, NULL, "altr,socfpga-sdmmc-ecc");
	if (!child)
		return -ENODEV;

	if (!of_device_is_available(child))
		goto exit;

	if (validate_parent_available(child))
		goto exit;

	/* Init portB */
	rc = altr_init_a10_ecc_block(child, ALTR_A10_SDMMC_IRQ_MASK,
				     a10_sdmmceccb_data.ecc_enable_mask, 1);
	if (rc)
		goto exit;

	/* Setup portB */
	return altr_portb_setup(device);

exit:
	of_node_put(child);
	return rc;
}

static irqreturn_t altr_edac_a10_ecc_irq_portb(int irq, void *dev_id)
{
	struct altr_edac_device_dev *ad = dev_id;
	void __iomem  *base = ad->base;
	const struct edac_device_prv_data *priv = ad->data;

	if (irq == ad->sb_irq) {
		writel(priv->ce_clear_mask,
		       base + ALTR_A10_ECC_INTSTAT_OFST);
		edac_device_handle_ce(ad->edac_dev, 0, 0, ad->edac_dev_name);
		return IRQ_HANDLED;
	} else if (irq == ad->db_irq) {
		writel(priv->ue_clear_mask,
		       base + ALTR_A10_ECC_INTSTAT_OFST);
		edac_device_handle_ue(ad->edac_dev, 0, 0, ad->edac_dev_name);
		return IRQ_HANDLED;
	}

	WARN_ONCE(1, "Unhandled IRQ%d on Port B.", irq);

	return IRQ_NONE;
}

static const struct edac_device_prv_data a10_sdmmcecca_data = {
	.setup = socfpga_init_sdmmc_ecc,
	.ce_clear_mask = ALTR_A10_ECC_SERRPENA,
	.ue_clear_mask = ALTR_A10_ECC_DERRPENA,
	.ecc_enable_mask = ALTR_A10_COMMON_ECC_EN_CTL,
	.ecc_en_ofst = ALTR_A10_ECC_CTRL_OFST,
	.ce_set_mask = ALTR_A10_ECC_SERRPENA,
	.ue_set_mask = ALTR_A10_ECC_DERRPENA,
	.set_err_ofst = ALTR_A10_ECC_INTTEST_OFST,
	.ecc_irq_handler = altr_edac_a10_ecc_irq,
	.inject_fops = &altr_edac_a10_device_inject_fops,
};

static const struct edac_device_prv_data a10_sdmmceccb_data = {
	.setup = socfpga_init_sdmmc_ecc,
	.ce_clear_mask = ALTR_A10_ECC_SERRPENB,
	.ue_clear_mask = ALTR_A10_ECC_DERRPENB,
	.ecc_enable_mask = ALTR_A10_COMMON_ECC_EN_CTL,
	.ecc_en_ofst = ALTR_A10_ECC_CTRL_OFST,
	.ce_set_mask = ALTR_A10_ECC_TSERRB,
	.ue_set_mask = ALTR_A10_ECC_TDERRB,
	.set_err_ofst = ALTR_A10_ECC_INTTEST_OFST,
	.ecc_irq_handler = altr_edac_a10_ecc_irq_portb,
	.inject_fops = &altr_edac_a10_device_inject_fops,
};

#endif	/* CONFIG_EDAC_ALTERA_SDMMC */

/********************* Arria10 EDAC Device Functions *************************/
static const struct of_device_id altr_edac_a10_device_of_match[] = {
#ifdef CONFIG_EDAC_ALTERA_L2C
	{ .compatible = "altr,socfpga-a10-l2-ecc", .data = &a10_l2ecc_data },
#endif
#ifdef CONFIG_EDAC_ALTERA_OCRAM
	{ .compatible = "altr,socfpga-a10-ocram-ecc",
	  .data = &a10_ocramecc_data },
#endif
#ifdef CONFIG_EDAC_ALTERA_ETHERNET
	{ .compatible = "altr,socfpga-eth-mac-ecc",
	  .data = &a10_enetecc_data },
#endif
#ifdef CONFIG_EDAC_ALTERA_NAND
	{ .compatible = "altr,socfpga-nand-ecc", .data = &a10_nandecc_data },
#endif
#ifdef CONFIG_EDAC_ALTERA_DMA
	{ .compatible = "altr,socfpga-dma-ecc", .data = &a10_dmaecc_data },
#endif
#ifdef CONFIG_EDAC_ALTERA_USB
	{ .compatible = "altr,socfpga-usb-ecc", .data = &a10_usbecc_data },
#endif
#ifdef CONFIG_EDAC_ALTERA_QSPI
	{ .compatible = "altr,socfpga-qspi-ecc", .data = &a10_qspiecc_data },
#endif
#ifdef CONFIG_EDAC_ALTERA_SDMMC
	{ .compatible = "altr,socfpga-sdmmc-ecc", .data = &a10_sdmmcecca_data },
#endif
#ifdef CONFIG_EDAC_ALTERA_SDRAM
	{ .compatible = "altr,sdram-edac-s10", .data = &s10_sdramecc_data },
#endif
	{},
};
MODULE_DEVICE_TABLE(of, altr_edac_a10_device_of_match);

/*
 * The Arria10 EDAC Device Functions differ from the Cyclone5/Arria5
 * because 2 IRQs are shared among the all ECC peripherals. The ECC
 * manager manages the IRQs and the children.
 * Based on xgene_edac.c peripheral code.
 */

static ssize_t __maybe_unused
altr_edac_a10_device_trig(struct file *file, const char __user *user_buf,
			  size_t count, loff_t *ppos)
{
	struct edac_device_ctl_info *edac_dci = file->private_data;
	struct altr_edac_device_dev *drvdata = edac_dci->pvt_info;
	const struct edac_device_prv_data *priv = drvdata->data;
	void __iomem *set_addr = (drvdata->base + priv->set_err_ofst);
	unsigned long flags;
	u8 trig_type;

	if (!user_buf || get_user(trig_type, user_buf))
		return -EFAULT;

	local_irq_save(flags);
	if (trig_type == ALTR_UE_TRIGGER_CHAR)
		writel(priv->ue_set_mask, set_addr);
	else
		writel(priv->ce_set_mask, set_addr);

	/* Ensure the interrupt test bits are set */
	wmb();
	local_irq_restore(flags);

	return count;
}

/*
 * The Stratix10 EDAC Error Injection Functions differ from Arria10
 * slightly. A few Arria10 peripherals can use this injection function.
 * Inject the error into the memory and then readback to trigger the IRQ.
 */
static ssize_t __maybe_unused
altr_edac_a10_device_trig2(struct file *file, const char __user *user_buf,
			   size_t count, loff_t *ppos)
{
	struct edac_device_ctl_info *edac_dci = file->private_data;
	struct altr_edac_device_dev *drvdata = edac_dci->pvt_info;
	const struct edac_device_prv_data *priv = drvdata->data;
	void __iomem *set_addr = (drvdata->base + priv->set_err_ofst);
	unsigned long flags;
	u8 trig_type;

	if (!user_buf || get_user(trig_type, user_buf))
		return -EFAULT;

	local_irq_save(flags);
	if (trig_type == ALTR_UE_TRIGGER_CHAR) {
		writel(priv->ue_set_mask, set_addr);
	} else {
		/* Setup read/write of 4 bytes */
		writel(ECC_WORD_WRITE, drvdata->base + ECC_BLK_DBYTECTRL_OFST);
		/* Setup Address to 0 */
		writel(0, drvdata->base + ECC_BLK_ADDRESS_OFST);
		/* Setup accctrl to read & ecc & data override */
		writel(ECC_READ_EDOVR, drvdata->base + ECC_BLK_ACCCTRL_OFST);
		/* Kick it. */
		writel(ECC_XACT_KICK, drvdata->base + ECC_BLK_STARTACC_OFST);
		/* Setup write for single bit change */
		writel(readl(drvdata->base + ECC_BLK_RDATA0_OFST) ^ 0x1,
		       drvdata->base + ECC_BLK_WDATA0_OFST);
		writel(readl(drvdata->base + ECC_BLK_RDATA1_OFST),
		       drvdata->base + ECC_BLK_WDATA1_OFST);
		writel(readl(drvdata->base + ECC_BLK_RDATA2_OFST),
		       drvdata->base + ECC_BLK_WDATA2_OFST);
		writel(readl(drvdata->base + ECC_BLK_RDATA3_OFST),
		       drvdata->base + ECC_BLK_WDATA3_OFST);

		/* Copy Read ECC to Write ECC */
		writel(readl(drvdata->base + ECC_BLK_RECC0_OFST),
		       drvdata->base + ECC_BLK_WECC0_OFST);
		writel(readl(drvdata->base + ECC_BLK_RECC1_OFST),
		       drvdata->base + ECC_BLK_WECC1_OFST);
		/* Setup accctrl to write & ecc override & data override */
		writel(ECC_WRITE_EDOVR, drvdata->base + ECC_BLK_ACCCTRL_OFST);
		/* Kick it. */
		writel(ECC_XACT_KICK, drvdata->base + ECC_BLK_STARTACC_OFST);
		/* Setup accctrl to read & ecc overwrite & data overwrite */
		writel(ECC_READ_EDOVR, drvdata->base + ECC_BLK_ACCCTRL_OFST);
		/* Kick it. */
		writel(ECC_XACT_KICK, drvdata->base + ECC_BLK_STARTACC_OFST);
	}

	/* Ensure the interrupt test bits are set */
	wmb();
	local_irq_restore(flags);

	return count;
}

static void altr_edac_a10_irq_handler(struct irq_desc *desc)
{
	int dberr, bit, sm_offset, irq_status;
	struct altr_arria10_edac *edac = irq_desc_get_handler_data(desc);
	struct irq_chip *chip = irq_desc_get_chip(desc);
	int irq = irq_desc_get_irq(desc);
	unsigned long bits;

	dberr = (irq == edac->db_irq) ? 1 : 0;
	sm_offset = dberr ? A10_SYSMGR_ECC_INTSTAT_DERR_OFST :
			    A10_SYSMGR_ECC_INTSTAT_SERR_OFST;

	chained_irq_enter(chip, desc);

	regmap_read(edac->ecc_mgr_map, sm_offset, &irq_status);

	bits = irq_status;
	for_each_set_bit(bit, &bits, 32) {
		irq = irq_linear_revmap(edac->domain, dberr * 32 + bit);
		if (irq)
			generic_handle_irq(irq);
	}

	chained_irq_exit(chip, desc);
}

static int validate_parent_available(struct device_node *np)
{
	struct device_node *parent;
	int ret = 0;

	/* SDRAM must be present for Linux (implied parent) */
	if (of_device_is_compatible(np, "altr,sdram-edac-s10"))
		return 0;

	/* Ensure parent device is enabled if parent node exists */
	parent = of_parse_phandle(np, "altr,ecc-parent", 0);
	if (parent && !of_device_is_available(parent))
		ret = -ENODEV;

	of_node_put(parent);
	return ret;
}

static int get_s10_sdram_edac_resource(struct device_node *np,
				       struct resource *res)
{
	struct device_node *parent;
	int ret;

	parent = of_parse_phandle(np, "altr,sdr-syscon", 0);
	if (!parent)
		return -ENODEV;

	ret = of_address_to_resource(parent, 0, res);
	of_node_put(parent);

	return ret;
}

static int altr_edac_a10_device_add(struct altr_arria10_edac *edac,
				    struct device_node *np)
{
	struct edac_device_ctl_info *dci;
	struct altr_edac_device_dev *altdev;
	char *ecc_name = (char *)np->name;
	struct resource res;
	int edac_idx;
	int rc = 0;
	const struct edac_device_prv_data *prv;
	/* Get matching node and check for valid result */
	const struct of_device_id *pdev_id =
		of_match_node(altr_edac_a10_device_of_match, np);
	if (IS_ERR_OR_NULL(pdev_id))
		return -ENODEV;

	/* Get driver specific data for this EDAC device */
	prv = pdev_id->data;
	if (IS_ERR_OR_NULL(prv))
		return -ENODEV;

	if (validate_parent_available(np))
		return -ENODEV;

	if (!devres_open_group(edac->dev, altr_edac_a10_device_add, GFP_KERNEL))
		return -ENOMEM;

	if (of_device_is_compatible(np, "altr,sdram-edac-s10"))
		rc = get_s10_sdram_edac_resource(np, &res);
	else
		rc = of_address_to_resource(np, 0, &res);

	if (rc < 0) {
		edac_printk(KERN_ERR, EDAC_DEVICE,
			    "%s: no resource address\n", ecc_name);
		goto err_release_group;
	}

	edac_idx = edac_device_alloc_index();
	dci = edac_device_alloc_ctl_info(sizeof(*altdev), ecc_name,
					 1, ecc_name, 1, 0, NULL, 0,
					 edac_idx);

	if (!dci) {
		edac_printk(KERN_ERR, EDAC_DEVICE,
			    "%s: Unable to allocate EDAC device\n", ecc_name);
		rc = -ENOMEM;
		goto err_release_group;
	}

	altdev = dci->pvt_info;
	dci->dev = edac->dev;
	altdev->edac_dev_name = ecc_name;
	altdev->edac_idx = edac_idx;
	altdev->edac = edac;
	altdev->edac_dev = dci;
	altdev->data = prv;
	altdev->ddev = *edac->dev;
	dci->dev = &altdev->ddev;
	dci->ctl_name = "Altera ECC Manager";
	dci->mod_name = ecc_name;
	dci->dev_name = ecc_name;

	altdev->base = devm_ioremap_resource(edac->dev, &res);
	if (IS_ERR(altdev->base)) {
		rc = PTR_ERR(altdev->base);
		goto err_release_group1;
	}

	/* Check specific dependencies for the module */
	if (altdev->data->setup) {
		rc = altdev->data->setup(altdev);
		if (rc)
			goto err_release_group1;
	}

	altdev->sb_irq = irq_of_parse_and_map(np, 0);
	if (!altdev->sb_irq) {
		edac_printk(KERN_ERR, EDAC_DEVICE, "Error allocating SBIRQ\n");
		rc = -ENODEV;
		goto err_release_group1;
	}
	rc = devm_request_irq(edac->dev, altdev->sb_irq, prv->ecc_irq_handler,
			      IRQF_ONESHOT | IRQF_TRIGGER_HIGH,
			      ecc_name, altdev);
	if (rc) {
		edac_printk(KERN_ERR, EDAC_DEVICE, "No SBERR IRQ resource\n");
		goto err_release_group1;
	}

#ifdef CONFIG_64BIT
	/* Use IRQ to determine SError origin instead of assigning IRQ */
	rc = of_property_read_u32_index(np, "interrupts", 0, &altdev->db_irq);
	if (rc) {
		edac_printk(KERN_ERR, EDAC_DEVICE,
			    "Unable to parse DB IRQ index\n");
		goto err_release_group1;
	}
#else
	altdev->db_irq = irq_of_parse_and_map(np, 1);
	if (!altdev->db_irq) {
		edac_printk(KERN_ERR, EDAC_DEVICE, "Error allocating DBIRQ\n");
		rc = -ENODEV;
		goto err_release_group1;
	}
	rc = devm_request_irq(edac->dev, altdev->db_irq, prv->ecc_irq_handler,
			      IRQF_ONESHOT | IRQF_TRIGGER_HIGH,
			      ecc_name, altdev);
	if (rc) {
		edac_printk(KERN_ERR, EDAC_DEVICE, "No DBERR IRQ resource\n");
		goto err_release_group1;
	}
#endif

	rc = edac_device_add_device(dci);
	if (rc) {
		dev_err(edac->dev, "edac_device_add_device failed\n");
		rc = -ENOMEM;
		goto err_release_group1;
	}

	altr_create_edacdev_dbgfs(dci, prv);

	list_add(&altdev->next, &edac->a10_ecc_devices);

	devres_remove_group(edac->dev, altr_edac_a10_device_add);

	return 0;

err_release_group1:
	edac_device_free_ctl_info(dci);
err_release_group:
	devres_release_group(edac->dev, NULL);
	edac_printk(KERN_ERR, EDAC_DEVICE,
		    "%s:Error setting up EDAC device: %d\n", ecc_name, rc);

	return rc;
}

static void a10_eccmgr_irq_mask(struct irq_data *d)
{
	struct altr_arria10_edac *edac = irq_data_get_irq_chip_data(d);

	regmap_write(edac->ecc_mgr_map,	A10_SYSMGR_ECC_INTMASK_SET_OFST,
		     BIT(d->hwirq));
}

static void a10_eccmgr_irq_unmask(struct irq_data *d)
{
	struct altr_arria10_edac *edac = irq_data_get_irq_chip_data(d);

	regmap_write(edac->ecc_mgr_map,	A10_SYSMGR_ECC_INTMASK_CLR_OFST,
		     BIT(d->hwirq));
}

static int a10_eccmgr_irqdomain_map(struct irq_domain *d, unsigned int irq,
				    irq_hw_number_t hwirq)
{
	struct altr_arria10_edac *edac = d->host_data;

	irq_set_chip_and_handler(irq, &edac->irq_chip, handle_simple_irq);
	irq_set_chip_data(irq, edac);
	irq_set_noprobe(irq);

	return 0;
}

static const struct irq_domain_ops a10_eccmgr_ic_ops = {
	.map = a10_eccmgr_irqdomain_map,
	.xlate = irq_domain_xlate_twocell,
};

/************** Stratix 10 EDAC Double Bit Error Handler ************/
#define to_a10edac(p, m) container_of(p, struct altr_arria10_edac, m)

#ifdef CONFIG_64BIT
/* panic routine issues reboot on non-zero panic_timeout */
extern int panic_timeout;

/*
 * The double bit error is handled through SError which is fatal. This is
 * called as a panic notifier to printout ECC error info as part of the panic.
 */
static int s10_edac_dberr_handler(struct notifier_block *this,
				  unsigned long event, void *ptr)
{
	struct altr_arria10_edac *edac = to_a10edac(this, panic_notifier);
	int err_addr, dberror;

	regmap_read(edac->ecc_mgr_map, S10_SYSMGR_ECC_INTSTAT_DERR_OFST,
		    &dberror);
	regmap_write(edac->ecc_mgr_map, S10_SYSMGR_UE_VAL_OFST, dberror);
	if (dberror & S10_DBE_IRQ_MASK) {
		struct list_head *position;
		struct altr_edac_device_dev *ed;
		struct arm_smccc_res result;

		/* Find the matching DBE in the list of devices */
		list_for_each(position, &edac->a10_ecc_devices) {
			ed = list_entry(position, struct altr_edac_device_dev,
					next);
			if (!(BIT(ed->db_irq) & dberror))
				continue;

			writel(ALTR_A10_ECC_DERRPENA,
			       ed->base + ALTR_A10_ECC_INTSTAT_OFST);
			err_addr = readl(ed->base + ALTR_S10_DERR_ADDRA_OFST);
			regmap_write(edac->ecc_mgr_map,
				     S10_SYSMGR_UE_ADDR_OFST, err_addr);
			edac_printk(KERN_ERR, EDAC_DEVICE,
				    "EDAC: [Fatal DBE on %s @ 0x%08X]\n",
				    ed->edac_dev_name, err_addr);
			break;
		}
		/* Notify the System through SMC. Reboot delay = 1 second */
		panic_timeout = 1;
		arm_smccc_smc(INTEL_SIP_SMC_ECC_DBE, dberror, 0, 0, 0, 0,
			      0, 0, &result);
	}

	return NOTIFY_DONE;
}
#endif

/****************** Arria 10 EDAC Probe Function *********************/
static int altr_edac_a10_probe(struct platform_device *pdev)
{
	struct altr_arria10_edac *edac;
	struct device_node *child;

	edac = devm_kzalloc(&pdev->dev, sizeof(*edac), GFP_KERNEL);
	if (!edac)
		return -ENOMEM;

	edac->dev = &pdev->dev;
	platform_set_drvdata(pdev, edac);
	INIT_LIST_HEAD(&edac->a10_ecc_devices);

	edac->ecc_mgr_map =
		altr_sysmgr_regmap_lookup_by_phandle(pdev->dev.of_node,
						     "altr,sysmgr-syscon");

	if (IS_ERR(edac->ecc_mgr_map)) {
		edac_printk(KERN_ERR, EDAC_DEVICE,
			    "Unable to get syscon altr,sysmgr-syscon\n");
		return PTR_ERR(edac->ecc_mgr_map);
	}

	edac->irq_chip.name = pdev->dev.of_node->name;
	edac->irq_chip.irq_mask = a10_eccmgr_irq_mask;
	edac->irq_chip.irq_unmask = a10_eccmgr_irq_unmask;
	edac->domain = irq_domain_add_linear(pdev->dev.of_node, 64,
					     &a10_eccmgr_ic_ops, edac);
	if (!edac->domain) {
		dev_err(&pdev->dev, "Error adding IRQ domain\n");
		return -ENOMEM;
	}

	edac->sb_irq = platform_get_irq(pdev, 0);
	if (edac->sb_irq < 0) {
		dev_err(&pdev->dev, "No SBERR IRQ resource\n");
		return edac->sb_irq;
	}

	irq_set_chained_handler_and_data(edac->sb_irq,
					 altr_edac_a10_irq_handler,
					 edac);

#ifdef CONFIG_64BIT
	{
		int dberror, err_addr;

		edac->panic_notifier.notifier_call = s10_edac_dberr_handler;
		atomic_notifier_chain_register(&panic_notifier_list,
					       &edac->panic_notifier);

		/* Printout a message if uncorrectable error previously. */
		regmap_read(edac->ecc_mgr_map, S10_SYSMGR_UE_VAL_OFST,
			    &dberror);
		if (dberror) {
			regmap_read(edac->ecc_mgr_map, S10_SYSMGR_UE_ADDR_OFST,
				    &err_addr);
			edac_printk(KERN_ERR, EDAC_DEVICE,
				    "Previous Boot UE detected[0x%X] @ 0x%X\n",
				    dberror, err_addr);
			/* Reset the sticky registers */
			regmap_write(edac->ecc_mgr_map,
				     S10_SYSMGR_UE_VAL_OFST, 0);
			regmap_write(edac->ecc_mgr_map,
				     S10_SYSMGR_UE_ADDR_OFST, 0);
		}
	}
#else
	edac->db_irq = platform_get_irq(pdev, 1);
	if (edac->db_irq < 0) {
		dev_err(&pdev->dev, "No DBERR IRQ resource\n");
		return edac->db_irq;
	}
	irq_set_chained_handler_and_data(edac->db_irq,
					 altr_edac_a10_irq_handler, edac);
#endif

	for_each_child_of_node(pdev->dev.of_node, child) {
		if (!of_device_is_available(child))
			continue;

		if (of_match_node(altr_edac_a10_device_of_match, child))
			altr_edac_a10_device_add(edac, child);

#ifdef CONFIG_EDAC_ALTERA_SDRAM
		else if (of_device_is_compatible(child, "altr,sdram-edac-a10"))
			of_platform_populate(pdev->dev.of_node,
					     altr_sdram_ctrl_of_match,
					     NULL, &pdev->dev);
#endif
	}

	return 0;
}

static const struct of_device_id altr_edac_a10_of_match[] = {
	{ .compatible = "altr,socfpga-a10-ecc-manager" },
	{ .compatible = "altr,socfpga-s10-ecc-manager" },
	{},
};
MODULE_DEVICE_TABLE(of, altr_edac_a10_of_match);

static struct platform_driver altr_edac_a10_driver = {
	.probe =  altr_edac_a10_probe,
	.driver = {
		.name = "socfpga_a10_ecc_manager",
		.of_match_table = altr_edac_a10_of_match,
	},
};
module_platform_driver(altr_edac_a10_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Thor Thayer");
MODULE_DESCRIPTION("EDAC Driver for Altera Memories");
