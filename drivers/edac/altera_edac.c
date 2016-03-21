/*
 *  Copyright Altera Corporation (C) 2014-2016. All rights reserved.
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

#include <asm/cacheflush.h>
#include <linux/ctype.h>
#include <linux/edac.h>
#include <linux/genalloc.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/mfd/syscon.h>
#include <linux/of_address.h>
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
#define EDAC_DEVICE		"Altera"

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
	if (!IS_ENABLED(CONFIG_EDAC_DEBUG))
		return;

	if (!mci->debugfs)
		return;

	edac_debugfs_create_file("inject_ctrl", S_IWUSR, mci->debugfs, mci,
				 &altr_sdr_mc_debug_inject_fops);
}

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

const struct edac_device_prv_data ocramecc_data;
const struct edac_device_prv_data l2ecc_data;
const struct edac_device_prv_data a10_l2ecc_data;

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

static ssize_t altr_edac_device_trig(struct file *file,
				     const char __user *user_buf,
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
		if (ACCESS_ONCE(ptemp[i]))
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
		if (ACCESS_ONCE(ptemp[i]) != i)
			edac_printk(KERN_ERR, EDAC_DEVICE,
				    "Read doesn't match written data\n");

	if (priv->free_mem)
		priv->free_mem(ptemp, priv->trig_alloc_sz, generic_ptr);

	return count;
}

static const struct file_operations altr_edac_device_inject_fops = {
	.open = simple_open,
	.write = altr_edac_device_trig,
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

	if (!edac_debugfs_create_file(priv->dbgfs_name, S_IWUSR,
				      drvdata->debugfs_dir, edac_dci,
				      &altr_edac_device_inject_fops))
		debugfs_remove_recursive(drvdata->debugfs_dir);
}

static const struct of_device_id altr_edac_device_of_match[] = {
#ifdef CONFIG_EDAC_ALTERA_L2C
	{ .compatible = "altr,socfpga-l2-ecc", .data = (void *)&l2ecc_data },
	{ .compatible = "altr,socfpga-a10-l2-ecc",
	  .data = (void *)&a10_l2ecc_data },
#endif
#ifdef CONFIG_EDAC_ALTERA_OCRAM
	{ .compatible = "altr,socfpga-ocram-ecc",
	  .data = (void *)&ocramecc_data },
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
	if (!drvdata->base)
		goto fail1;

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
	gen_pool_free((struct gen_pool *)other, (u32)p, size);
}

/*
 * altr_ocram_check_deps()
 *	Test for OCRAM cache ECC dependencies upon entry because
 *	platform specific startup should have initialized the
 *	On-Chip RAM memory and enabled the ECC.
 *	Can't turn on ECC here because accessing un-initialized
 *	memory will cause CE/UE errors possibly causing an ABORT.
 */
static int altr_ocram_check_deps(struct altr_edac_device_dev *device)
{
	void __iomem  *base = device->base;
	const struct edac_device_prv_data *prv = device->data;

	if (readl(base) & prv->ecc_enable_mask)
		return 0;

	edac_printk(KERN_ERR, EDAC_DEVICE,
		    "OCRAM: No ECC present or ECC disabled.\n");
	return -ENODEV;
}

const struct edac_device_prv_data ocramecc_data = {
	.setup = altr_ocram_check_deps,
	.ce_clear_mask = (ALTR_OCR_ECC_EN | ALTR_OCR_ECC_SERR),
	.ue_clear_mask = (ALTR_OCR_ECC_EN | ALTR_OCR_ECC_DERR),
	.dbgfs_name = "altr_ocram_trigger",
	.alloc_mem = ocram_alloc_mem,
	.free_mem = ocram_free_mem,
	.ecc_enable_mask = ALTR_OCR_ECC_EN,
	.ce_set_mask = (ALTR_OCR_ECC_EN | ALTR_OCR_ECC_INJS),
	.ue_set_mask = (ALTR_OCR_ECC_EN | ALTR_OCR_ECC_INJD),
	.set_err_ofst = ALTR_OCR_ECC_REG_OFFSET,
	.trig_alloc_sz = ALTR_TRIG_OCRAM_BYTE_SIZE,
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

static irqreturn_t altr_edac_a10_l2_irq(struct altr_edac_device_dev *dci,
					bool sberr)
{
	if (sberr) {
		regmap_write(dci->edac->ecc_mgr_map,
			     A10_SYSGMR_MPU_CLEAR_L2_ECC_OFST,
			     A10_SYSGMR_MPU_CLEAR_L2_ECC_SB);
		edac_device_handle_ce(dci->edac_dev, 0, 0, dci->edac_dev_name);
	} else {
		regmap_write(dci->edac->ecc_mgr_map,
			     A10_SYSGMR_MPU_CLEAR_L2_ECC_OFST,
			     A10_SYSGMR_MPU_CLEAR_L2_ECC_MB);
		edac_device_handle_ue(dci->edac_dev, 0, 0, dci->edac_dev_name);
		panic("\nEDAC:ECC_DEVICE[Uncorrectable errors]\n");
	}
	return IRQ_HANDLED;
}

const struct edac_device_prv_data l2ecc_data = {
	.setup = altr_l2_check_deps,
	.ce_clear_mask = 0,
	.ue_clear_mask = 0,
	.dbgfs_name = "altr_l2_trigger",
	.alloc_mem = l2_alloc_mem,
	.free_mem = l2_free_mem,
	.ecc_enable_mask = ALTR_L2_ECC_EN,
	.ce_set_mask = (ALTR_L2_ECC_EN | ALTR_L2_ECC_INJS),
	.ue_set_mask = (ALTR_L2_ECC_EN | ALTR_L2_ECC_INJD),
	.set_err_ofst = ALTR_L2_ECC_REG_OFFSET,
	.trig_alloc_sz = ALTR_TRIG_L2C_BYTE_SIZE,
};

const struct edac_device_prv_data a10_l2ecc_data = {
	.setup = altr_l2_check_deps,
	.ce_clear_mask = ALTR_A10_L2_ECC_SERR_CLR,
	.ue_clear_mask = ALTR_A10_L2_ECC_MERR_CLR,
	.irq_status_mask = A10_SYSMGR_ECC_INTSTAT_L2,
	.dbgfs_name = "altr_l2_trigger",
	.alloc_mem = l2_alloc_mem,
	.free_mem = l2_free_mem,
	.ecc_enable_mask = ALTR_A10_L2_ECC_EN_CTL,
	.ce_set_mask = ALTR_A10_L2_ECC_CE_INJ_MASK,
	.ue_set_mask = ALTR_A10_L2_ECC_UE_INJ_MASK,
	.set_err_ofst = ALTR_A10_L2_ECC_INJ_OFST,
	.ecc_irq_handler = altr_edac_a10_l2_irq,
	.trig_alloc_sz = ALTR_TRIG_L2C_BYTE_SIZE,
};

#endif	/* CONFIG_EDAC_ALTERA_L2C */

/********************* Arria10 EDAC Device Functions *************************/

/*
 * The Arria10 EDAC Device Functions differ from the Cyclone5/Arria5
 * because 2 IRQs are shared among the all ECC peripherals. The ECC
 * manager manages the IRQs and the children.
 * Based on xgene_edac.c peripheral code.
 */

static irqreturn_t altr_edac_a10_irq_handler(int irq, void *dev_id)
{
	irqreturn_t rc = IRQ_NONE;
	struct altr_arria10_edac *edac = dev_id;
	struct altr_edac_device_dev *dci;
	int irq_status;
	bool sberr = (irq == edac->sb_irq) ? 1 : 0;
	int sm_offset = sberr ? A10_SYSMGR_ECC_INTSTAT_SERR_OFST :
				A10_SYSMGR_ECC_INTSTAT_DERR_OFST;

	regmap_read(edac->ecc_mgr_map, sm_offset, &irq_status);

	if ((irq != edac->sb_irq) && (irq != edac->db_irq)) {
		WARN_ON(1);
	} else {
		list_for_each_entry(dci, &edac->a10_ecc_devices, next) {
			if (irq_status & dci->data->irq_status_mask)
				rc = dci->data->ecc_irq_handler(dci, sberr);
		}
	}

	return rc;
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
		of_match_node(altr_edac_device_of_match, np);
	if (IS_ERR_OR_NULL(pdev_id))
		return -ENODEV;

	/* Get driver specific data for this EDAC device */
	prv = pdev_id->data;
	if (IS_ERR_OR_NULL(prv))
		return -ENODEV;

	if (!devres_open_group(edac->dev, altr_edac_a10_device_add, GFP_KERNEL))
		return -ENOMEM;

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
	edac_printk(KERN_ALERT, EDAC_DEVICE, "%s: %d\n", __func__, __LINE__);
	devres_release_group(edac->dev, NULL);
	edac_printk(KERN_ERR, EDAC_DEVICE,
		    "%s:Error setting up EDAC device: %d\n", ecc_name, rc);

	return rc;
}

static int altr_edac_a10_probe(struct platform_device *pdev)
{
	struct altr_arria10_edac *edac;
	struct device_node *child;
	int rc;

	edac = devm_kzalloc(&pdev->dev, sizeof(*edac), GFP_KERNEL);
	if (!edac)
		return -ENOMEM;

	edac->dev = &pdev->dev;
	platform_set_drvdata(pdev, edac);
	INIT_LIST_HEAD(&edac->a10_ecc_devices);

	edac->ecc_mgr_map = syscon_regmap_lookup_by_phandle(pdev->dev.of_node,
							"altr,sysmgr-syscon");
	if (IS_ERR(edac->ecc_mgr_map)) {
		edac_printk(KERN_ERR, EDAC_DEVICE,
			    "Unable to get syscon altr,sysmgr-syscon\n");
		return PTR_ERR(edac->ecc_mgr_map);
	}

	edac->sb_irq = platform_get_irq(pdev, 0);
	rc = devm_request_irq(&pdev->dev, edac->sb_irq,
			      altr_edac_a10_irq_handler,
			      IRQF_SHARED, dev_name(&pdev->dev), edac);
	if (rc) {
		edac_printk(KERN_ERR, EDAC_DEVICE, "No SBERR IRQ resource\n");
		return rc;
	}

	edac->db_irq = platform_get_irq(pdev, 1);
	rc = devm_request_irq(&pdev->dev, edac->db_irq,
			      altr_edac_a10_irq_handler,
			      IRQF_SHARED, dev_name(&pdev->dev), edac);
	if (rc) {
		edac_printk(KERN_ERR, EDAC_DEVICE, "No DBERR IRQ resource\n");
		return rc;
	}

	for_each_child_of_node(pdev->dev.of_node, child) {
		if (!of_device_is_available(child))
			continue;
		if (of_device_is_compatible(child, "altr,socfpga-a10-l2-ecc"))
			altr_edac_a10_device_add(edac, child);
	}

	return 0;
}

static const struct of_device_id altr_edac_a10_of_match[] = {
	{ .compatible = "altr,socfpga-a10-ecc-manager" },
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
