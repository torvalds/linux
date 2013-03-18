/*
 * Freescale MPC85xx Memory Controller kenel module
 *
 * Author: Dave Jiang <djiang@mvista.com>
 *
 * 2006-2007 (c) MontaVista Software, Inc. This file is licensed under
 * the terms of the GNU General Public License version 2. This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 *
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/ctype.h>
#include <linux/io.h>
#include <linux/mod_devicetable.h>
#include <linux/edac.h>
#include <linux/smp.h>
#include <linux/gfp.h>

#include <linux/of_platform.h>
#include <linux/of_device.h>
#include "edac_module.h"
#include "edac_core.h"
#include "mpc85xx_edac.h"

static int edac_dev_idx;
#ifdef CONFIG_PCI
static int edac_pci_idx;
#endif
static int edac_mc_idx;

static u32 orig_ddr_err_disable;
static u32 orig_ddr_err_sbe;

/*
 * PCI Err defines
 */
#ifdef CONFIG_PCI
static u32 orig_pci_err_cap_dr;
static u32 orig_pci_err_en;
#endif

static u32 orig_l2_err_disable;
#ifdef CONFIG_FSL_SOC_BOOKE
static u32 orig_hid1[2];
#endif

/************************ MC SYSFS parts ***********************************/

#define to_mci(k) container_of(k, struct mem_ctl_info, dev)

static ssize_t mpc85xx_mc_inject_data_hi_show(struct device *dev,
					      struct device_attribute *mattr,
					      char *data)
{
	struct mem_ctl_info *mci = to_mci(dev);
	struct mpc85xx_mc_pdata *pdata = mci->pvt_info;
	return sprintf(data, "0x%08x",
		       in_be32(pdata->mc_vbase +
			       MPC85XX_MC_DATA_ERR_INJECT_HI));
}

static ssize_t mpc85xx_mc_inject_data_lo_show(struct device *dev,
					      struct device_attribute *mattr,
					      char *data)
{
	struct mem_ctl_info *mci = to_mci(dev);
	struct mpc85xx_mc_pdata *pdata = mci->pvt_info;
	return sprintf(data, "0x%08x",
		       in_be32(pdata->mc_vbase +
			       MPC85XX_MC_DATA_ERR_INJECT_LO));
}

static ssize_t mpc85xx_mc_inject_ctrl_show(struct device *dev,
					   struct device_attribute *mattr,
					   char *data)
{
	struct mem_ctl_info *mci = to_mci(dev);
	struct mpc85xx_mc_pdata *pdata = mci->pvt_info;
	return sprintf(data, "0x%08x",
		       in_be32(pdata->mc_vbase + MPC85XX_MC_ECC_ERR_INJECT));
}

static ssize_t mpc85xx_mc_inject_data_hi_store(struct device *dev,
					       struct device_attribute *mattr,
					       const char *data, size_t count)
{
	struct mem_ctl_info *mci = to_mci(dev);
	struct mpc85xx_mc_pdata *pdata = mci->pvt_info;
	if (isdigit(*data)) {
		out_be32(pdata->mc_vbase + MPC85XX_MC_DATA_ERR_INJECT_HI,
			 simple_strtoul(data, NULL, 0));
		return count;
	}
	return 0;
}

static ssize_t mpc85xx_mc_inject_data_lo_store(struct device *dev,
					       struct device_attribute *mattr,
					       const char *data, size_t count)
{
	struct mem_ctl_info *mci = to_mci(dev);
	struct mpc85xx_mc_pdata *pdata = mci->pvt_info;
	if (isdigit(*data)) {
		out_be32(pdata->mc_vbase + MPC85XX_MC_DATA_ERR_INJECT_LO,
			 simple_strtoul(data, NULL, 0));
		return count;
	}
	return 0;
}

static ssize_t mpc85xx_mc_inject_ctrl_store(struct device *dev,
					       struct device_attribute *mattr,
					       const char *data, size_t count)
{
	struct mem_ctl_info *mci = to_mci(dev);
	struct mpc85xx_mc_pdata *pdata = mci->pvt_info;
	if (isdigit(*data)) {
		out_be32(pdata->mc_vbase + MPC85XX_MC_ECC_ERR_INJECT,
			 simple_strtoul(data, NULL, 0));
		return count;
	}
	return 0;
}

DEVICE_ATTR(inject_data_hi, S_IRUGO | S_IWUSR,
	    mpc85xx_mc_inject_data_hi_show, mpc85xx_mc_inject_data_hi_store);
DEVICE_ATTR(inject_data_lo, S_IRUGO | S_IWUSR,
	    mpc85xx_mc_inject_data_lo_show, mpc85xx_mc_inject_data_lo_store);
DEVICE_ATTR(inject_ctrl, S_IRUGO | S_IWUSR,
	    mpc85xx_mc_inject_ctrl_show, mpc85xx_mc_inject_ctrl_store);

static int mpc85xx_create_sysfs_attributes(struct mem_ctl_info *mci)
{
	int rc;

	rc = device_create_file(&mci->dev, &dev_attr_inject_data_hi);
	if (rc < 0)
		return rc;
	rc = device_create_file(&mci->dev, &dev_attr_inject_data_lo);
	if (rc < 0)
		return rc;
	rc = device_create_file(&mci->dev, &dev_attr_inject_ctrl);
	if (rc < 0)
		return rc;

	return 0;
}

static void mpc85xx_remove_sysfs_attributes(struct mem_ctl_info *mci)
{
	device_remove_file(&mci->dev, &dev_attr_inject_data_hi);
	device_remove_file(&mci->dev, &dev_attr_inject_data_lo);
	device_remove_file(&mci->dev, &dev_attr_inject_ctrl);
}

/**************************** PCI Err device ***************************/
#ifdef CONFIG_PCI

static void mpc85xx_pci_check(struct edac_pci_ctl_info *pci)
{
	struct mpc85xx_pci_pdata *pdata = pci->pvt_info;
	u32 err_detect;

	err_detect = in_be32(pdata->pci_vbase + MPC85XX_PCI_ERR_DR);

	/* master aborts can happen during PCI config cycles */
	if (!(err_detect & ~(PCI_EDE_MULTI_ERR | PCI_EDE_MST_ABRT))) {
		out_be32(pdata->pci_vbase + MPC85XX_PCI_ERR_DR, err_detect);
		return;
	}

	printk(KERN_ERR "PCI error(s) detected\n");
	printk(KERN_ERR "PCI/X ERR_DR register: %#08x\n", err_detect);

	printk(KERN_ERR "PCI/X ERR_ATTRIB register: %#08x\n",
	       in_be32(pdata->pci_vbase + MPC85XX_PCI_ERR_ATTRIB));
	printk(KERN_ERR "PCI/X ERR_ADDR register: %#08x\n",
	       in_be32(pdata->pci_vbase + MPC85XX_PCI_ERR_ADDR));
	printk(KERN_ERR "PCI/X ERR_EXT_ADDR register: %#08x\n",
	       in_be32(pdata->pci_vbase + MPC85XX_PCI_ERR_EXT_ADDR));
	printk(KERN_ERR "PCI/X ERR_DL register: %#08x\n",
	       in_be32(pdata->pci_vbase + MPC85XX_PCI_ERR_DL));
	printk(KERN_ERR "PCI/X ERR_DH register: %#08x\n",
	       in_be32(pdata->pci_vbase + MPC85XX_PCI_ERR_DH));

	/* clear error bits */
	out_be32(pdata->pci_vbase + MPC85XX_PCI_ERR_DR, err_detect);

	if (err_detect & PCI_EDE_PERR_MASK)
		edac_pci_handle_pe(pci, pci->ctl_name);

	if ((err_detect & ~PCI_EDE_MULTI_ERR) & ~PCI_EDE_PERR_MASK)
		edac_pci_handle_npe(pci, pci->ctl_name);
}

static irqreturn_t mpc85xx_pci_isr(int irq, void *dev_id)
{
	struct edac_pci_ctl_info *pci = dev_id;
	struct mpc85xx_pci_pdata *pdata = pci->pvt_info;
	u32 err_detect;

	err_detect = in_be32(pdata->pci_vbase + MPC85XX_PCI_ERR_DR);

	if (!err_detect)
		return IRQ_NONE;

	mpc85xx_pci_check(pci);

	return IRQ_HANDLED;
}

int mpc85xx_pci_err_probe(struct platform_device *op)
{
	struct edac_pci_ctl_info *pci;
	struct mpc85xx_pci_pdata *pdata;
	struct resource r;
	int res = 0;

	if (!devres_open_group(&op->dev, mpc85xx_pci_err_probe, GFP_KERNEL))
		return -ENOMEM;

	pci = edac_pci_alloc_ctl_info(sizeof(*pdata), "mpc85xx_pci_err");
	if (!pci)
		return -ENOMEM;

	/* make sure error reporting method is sane */
	switch (edac_op_state) {
	case EDAC_OPSTATE_POLL:
	case EDAC_OPSTATE_INT:
		break;
	default:
		edac_op_state = EDAC_OPSTATE_INT;
		break;
	}

	pdata = pci->pvt_info;
	pdata->name = "mpc85xx_pci_err";
	pdata->irq = NO_IRQ;
	dev_set_drvdata(&op->dev, pci);
	pci->dev = &op->dev;
	pci->mod_name = EDAC_MOD_STR;
	pci->ctl_name = pdata->name;
	pci->dev_name = dev_name(&op->dev);

	if (edac_op_state == EDAC_OPSTATE_POLL)
		pci->edac_check = mpc85xx_pci_check;

	pdata->edac_idx = edac_pci_idx++;

	res = of_address_to_resource(op->dev.of_node, 0, &r);
	if (res) {
		printk(KERN_ERR "%s: Unable to get resource for "
		       "PCI err regs\n", __func__);
		goto err;
	}

	/* we only need the error registers */
	r.start += 0xe00;

	if (!devm_request_mem_region(&op->dev, r.start, resource_size(&r),
					pdata->name)) {
		printk(KERN_ERR "%s: Error while requesting mem region\n",
		       __func__);
		res = -EBUSY;
		goto err;
	}

	pdata->pci_vbase = devm_ioremap(&op->dev, r.start, resource_size(&r));
	if (!pdata->pci_vbase) {
		printk(KERN_ERR "%s: Unable to setup PCI err regs\n", __func__);
		res = -ENOMEM;
		goto err;
	}

	orig_pci_err_cap_dr =
	    in_be32(pdata->pci_vbase + MPC85XX_PCI_ERR_CAP_DR);

	/* PCI master abort is expected during config cycles */
	out_be32(pdata->pci_vbase + MPC85XX_PCI_ERR_CAP_DR, 0x40);

	orig_pci_err_en = in_be32(pdata->pci_vbase + MPC85XX_PCI_ERR_EN);

	/* disable master abort reporting */
	out_be32(pdata->pci_vbase + MPC85XX_PCI_ERR_EN, ~0x40);

	/* clear error bits */
	out_be32(pdata->pci_vbase + MPC85XX_PCI_ERR_DR, ~0);

	if (edac_pci_add_device(pci, pdata->edac_idx) > 0) {
		edac_dbg(3, "failed edac_pci_add_device()\n");
		goto err;
	}

	if (edac_op_state == EDAC_OPSTATE_INT) {
		pdata->irq = irq_of_parse_and_map(op->dev.of_node, 0);
		res = devm_request_irq(&op->dev, pdata->irq,
				       mpc85xx_pci_isr, IRQF_DISABLED,
				       "[EDAC] PCI err", pci);
		if (res < 0) {
			printk(KERN_ERR
			       "%s: Unable to request irq %d for "
			       "MPC85xx PCI err\n", __func__, pdata->irq);
			irq_dispose_mapping(pdata->irq);
			res = -ENODEV;
			goto err2;
		}

		printk(KERN_INFO EDAC_MOD_STR " acquired irq %d for PCI Err\n",
		       pdata->irq);
	}

	devres_remove_group(&op->dev, mpc85xx_pci_err_probe);
	edac_dbg(3, "success\n");
	printk(KERN_INFO EDAC_MOD_STR " PCI err registered\n");

	return 0;

err2:
	edac_pci_del_device(&op->dev);
err:
	edac_pci_free_ctl_info(pci);
	devres_release_group(&op->dev, mpc85xx_pci_err_probe);
	return res;
}
EXPORT_SYMBOL(mpc85xx_pci_err_probe);

static int mpc85xx_pci_err_remove(struct platform_device *op)
{
	struct edac_pci_ctl_info *pci = dev_get_drvdata(&op->dev);
	struct mpc85xx_pci_pdata *pdata = pci->pvt_info;

	edac_dbg(0, "\n");

	out_be32(pdata->pci_vbase + MPC85XX_PCI_ERR_CAP_DR,
		 orig_pci_err_cap_dr);

	out_be32(pdata->pci_vbase + MPC85XX_PCI_ERR_EN, orig_pci_err_en);

	edac_pci_del_device(pci->dev);

	if (edac_op_state == EDAC_OPSTATE_INT)
		irq_dispose_mapping(pdata->irq);

	edac_pci_free_ctl_info(pci);

	return 0;
}

#endif				/* CONFIG_PCI */

/**************************** L2 Err device ***************************/

/************************ L2 SYSFS parts ***********************************/

static ssize_t mpc85xx_l2_inject_data_hi_show(struct edac_device_ctl_info
					      *edac_dev, char *data)
{
	struct mpc85xx_l2_pdata *pdata = edac_dev->pvt_info;
	return sprintf(data, "0x%08x",
		       in_be32(pdata->l2_vbase + MPC85XX_L2_ERRINJHI));
}

static ssize_t mpc85xx_l2_inject_data_lo_show(struct edac_device_ctl_info
					      *edac_dev, char *data)
{
	struct mpc85xx_l2_pdata *pdata = edac_dev->pvt_info;
	return sprintf(data, "0x%08x",
		       in_be32(pdata->l2_vbase + MPC85XX_L2_ERRINJLO));
}

static ssize_t mpc85xx_l2_inject_ctrl_show(struct edac_device_ctl_info
					   *edac_dev, char *data)
{
	struct mpc85xx_l2_pdata *pdata = edac_dev->pvt_info;
	return sprintf(data, "0x%08x",
		       in_be32(pdata->l2_vbase + MPC85XX_L2_ERRINJCTL));
}

static ssize_t mpc85xx_l2_inject_data_hi_store(struct edac_device_ctl_info
					       *edac_dev, const char *data,
					       size_t count)
{
	struct mpc85xx_l2_pdata *pdata = edac_dev->pvt_info;
	if (isdigit(*data)) {
		out_be32(pdata->l2_vbase + MPC85XX_L2_ERRINJHI,
			 simple_strtoul(data, NULL, 0));
		return count;
	}
	return 0;
}

static ssize_t mpc85xx_l2_inject_data_lo_store(struct edac_device_ctl_info
					       *edac_dev, const char *data,
					       size_t count)
{
	struct mpc85xx_l2_pdata *pdata = edac_dev->pvt_info;
	if (isdigit(*data)) {
		out_be32(pdata->l2_vbase + MPC85XX_L2_ERRINJLO,
			 simple_strtoul(data, NULL, 0));
		return count;
	}
	return 0;
}

static ssize_t mpc85xx_l2_inject_ctrl_store(struct edac_device_ctl_info
					    *edac_dev, const char *data,
					    size_t count)
{
	struct mpc85xx_l2_pdata *pdata = edac_dev->pvt_info;
	if (isdigit(*data)) {
		out_be32(pdata->l2_vbase + MPC85XX_L2_ERRINJCTL,
			 simple_strtoul(data, NULL, 0));
		return count;
	}
	return 0;
}

static struct edac_dev_sysfs_attribute mpc85xx_l2_sysfs_attributes[] = {
	{
	 .attr = {
		  .name = "inject_data_hi",
		  .mode = (S_IRUGO | S_IWUSR)
		  },
	 .show = mpc85xx_l2_inject_data_hi_show,
	 .store = mpc85xx_l2_inject_data_hi_store},
	{
	 .attr = {
		  .name = "inject_data_lo",
		  .mode = (S_IRUGO | S_IWUSR)
		  },
	 .show = mpc85xx_l2_inject_data_lo_show,
	 .store = mpc85xx_l2_inject_data_lo_store},
	{
	 .attr = {
		  .name = "inject_ctrl",
		  .mode = (S_IRUGO | S_IWUSR)
		  },
	 .show = mpc85xx_l2_inject_ctrl_show,
	 .store = mpc85xx_l2_inject_ctrl_store},

	/* End of list */
	{
	 .attr = {.name = NULL}
	 }
};

static void mpc85xx_set_l2_sysfs_attributes(struct edac_device_ctl_info
					    *edac_dev)
{
	edac_dev->sysfs_attributes = mpc85xx_l2_sysfs_attributes;
}

/***************************** L2 ops ***********************************/

static void mpc85xx_l2_check(struct edac_device_ctl_info *edac_dev)
{
	struct mpc85xx_l2_pdata *pdata = edac_dev->pvt_info;
	u32 err_detect;

	err_detect = in_be32(pdata->l2_vbase + MPC85XX_L2_ERRDET);

	if (!(err_detect & L2_EDE_MASK))
		return;

	printk(KERN_ERR "ECC Error in CPU L2 cache\n");
	printk(KERN_ERR "L2 Error Detect Register: 0x%08x\n", err_detect);
	printk(KERN_ERR "L2 Error Capture Data High Register: 0x%08x\n",
	       in_be32(pdata->l2_vbase + MPC85XX_L2_CAPTDATAHI));
	printk(KERN_ERR "L2 Error Capture Data Lo Register: 0x%08x\n",
	       in_be32(pdata->l2_vbase + MPC85XX_L2_CAPTDATALO));
	printk(KERN_ERR "L2 Error Syndrome Register: 0x%08x\n",
	       in_be32(pdata->l2_vbase + MPC85XX_L2_CAPTECC));
	printk(KERN_ERR "L2 Error Attributes Capture Register: 0x%08x\n",
	       in_be32(pdata->l2_vbase + MPC85XX_L2_ERRATTR));
	printk(KERN_ERR "L2 Error Address Capture Register: 0x%08x\n",
	       in_be32(pdata->l2_vbase + MPC85XX_L2_ERRADDR));

	/* clear error detect register */
	out_be32(pdata->l2_vbase + MPC85XX_L2_ERRDET, err_detect);

	if (err_detect & L2_EDE_CE_MASK)
		edac_device_handle_ce(edac_dev, 0, 0, edac_dev->ctl_name);

	if (err_detect & L2_EDE_UE_MASK)
		edac_device_handle_ue(edac_dev, 0, 0, edac_dev->ctl_name);
}

static irqreturn_t mpc85xx_l2_isr(int irq, void *dev_id)
{
	struct edac_device_ctl_info *edac_dev = dev_id;
	struct mpc85xx_l2_pdata *pdata = edac_dev->pvt_info;
	u32 err_detect;

	err_detect = in_be32(pdata->l2_vbase + MPC85XX_L2_ERRDET);

	if (!(err_detect & L2_EDE_MASK))
		return IRQ_NONE;

	mpc85xx_l2_check(edac_dev);

	return IRQ_HANDLED;
}

static int mpc85xx_l2_err_probe(struct platform_device *op)
{
	struct edac_device_ctl_info *edac_dev;
	struct mpc85xx_l2_pdata *pdata;
	struct resource r;
	int res;

	if (!devres_open_group(&op->dev, mpc85xx_l2_err_probe, GFP_KERNEL))
		return -ENOMEM;

	edac_dev = edac_device_alloc_ctl_info(sizeof(*pdata),
					      "cpu", 1, "L", 1, 2, NULL, 0,
					      edac_dev_idx);
	if (!edac_dev) {
		devres_release_group(&op->dev, mpc85xx_l2_err_probe);
		return -ENOMEM;
	}

	pdata = edac_dev->pvt_info;
	pdata->name = "mpc85xx_l2_err";
	pdata->irq = NO_IRQ;
	edac_dev->dev = &op->dev;
	dev_set_drvdata(edac_dev->dev, edac_dev);
	edac_dev->ctl_name = pdata->name;
	edac_dev->dev_name = pdata->name;

	res = of_address_to_resource(op->dev.of_node, 0, &r);
	if (res) {
		printk(KERN_ERR "%s: Unable to get resource for "
		       "L2 err regs\n", __func__);
		goto err;
	}

	/* we only need the error registers */
	r.start += 0xe00;

	if (!devm_request_mem_region(&op->dev, r.start, resource_size(&r),
				     pdata->name)) {
		printk(KERN_ERR "%s: Error while requesting mem region\n",
		       __func__);
		res = -EBUSY;
		goto err;
	}

	pdata->l2_vbase = devm_ioremap(&op->dev, r.start, resource_size(&r));
	if (!pdata->l2_vbase) {
		printk(KERN_ERR "%s: Unable to setup L2 err regs\n", __func__);
		res = -ENOMEM;
		goto err;
	}

	out_be32(pdata->l2_vbase + MPC85XX_L2_ERRDET, ~0);

	orig_l2_err_disable = in_be32(pdata->l2_vbase + MPC85XX_L2_ERRDIS);

	/* clear the err_dis */
	out_be32(pdata->l2_vbase + MPC85XX_L2_ERRDIS, 0);

	edac_dev->mod_name = EDAC_MOD_STR;

	if (edac_op_state == EDAC_OPSTATE_POLL)
		edac_dev->edac_check = mpc85xx_l2_check;

	mpc85xx_set_l2_sysfs_attributes(edac_dev);

	pdata->edac_idx = edac_dev_idx++;

	if (edac_device_add_device(edac_dev) > 0) {
		edac_dbg(3, "failed edac_device_add_device()\n");
		goto err;
	}

	if (edac_op_state == EDAC_OPSTATE_INT) {
		pdata->irq = irq_of_parse_and_map(op->dev.of_node, 0);
		res = devm_request_irq(&op->dev, pdata->irq,
				       mpc85xx_l2_isr, IRQF_DISABLED,
				       "[EDAC] L2 err", edac_dev);
		if (res < 0) {
			printk(KERN_ERR
			       "%s: Unable to request irq %d for "
			       "MPC85xx L2 err\n", __func__, pdata->irq);
			irq_dispose_mapping(pdata->irq);
			res = -ENODEV;
			goto err2;
		}

		printk(KERN_INFO EDAC_MOD_STR " acquired irq %d for L2 Err\n",
		       pdata->irq);

		edac_dev->op_state = OP_RUNNING_INTERRUPT;

		out_be32(pdata->l2_vbase + MPC85XX_L2_ERRINTEN, L2_EIE_MASK);
	}

	devres_remove_group(&op->dev, mpc85xx_l2_err_probe);

	edac_dbg(3, "success\n");
	printk(KERN_INFO EDAC_MOD_STR " L2 err registered\n");

	return 0;

err2:
	edac_device_del_device(&op->dev);
err:
	devres_release_group(&op->dev, mpc85xx_l2_err_probe);
	edac_device_free_ctl_info(edac_dev);
	return res;
}

static int mpc85xx_l2_err_remove(struct platform_device *op)
{
	struct edac_device_ctl_info *edac_dev = dev_get_drvdata(&op->dev);
	struct mpc85xx_l2_pdata *pdata = edac_dev->pvt_info;

	edac_dbg(0, "\n");

	if (edac_op_state == EDAC_OPSTATE_INT) {
		out_be32(pdata->l2_vbase + MPC85XX_L2_ERRINTEN, 0);
		irq_dispose_mapping(pdata->irq);
	}

	out_be32(pdata->l2_vbase + MPC85XX_L2_ERRDIS, orig_l2_err_disable);
	edac_device_del_device(&op->dev);
	edac_device_free_ctl_info(edac_dev);
	return 0;
}

static struct of_device_id mpc85xx_l2_err_of_match[] = {
/* deprecate the fsl,85.. forms in the future, 2.6.30? */
	{ .compatible = "fsl,8540-l2-cache-controller", },
	{ .compatible = "fsl,8541-l2-cache-controller", },
	{ .compatible = "fsl,8544-l2-cache-controller", },
	{ .compatible = "fsl,8548-l2-cache-controller", },
	{ .compatible = "fsl,8555-l2-cache-controller", },
	{ .compatible = "fsl,8568-l2-cache-controller", },
	{ .compatible = "fsl,mpc8536-l2-cache-controller", },
	{ .compatible = "fsl,mpc8540-l2-cache-controller", },
	{ .compatible = "fsl,mpc8541-l2-cache-controller", },
	{ .compatible = "fsl,mpc8544-l2-cache-controller", },
	{ .compatible = "fsl,mpc8548-l2-cache-controller", },
	{ .compatible = "fsl,mpc8555-l2-cache-controller", },
	{ .compatible = "fsl,mpc8560-l2-cache-controller", },
	{ .compatible = "fsl,mpc8568-l2-cache-controller", },
	{ .compatible = "fsl,mpc8569-l2-cache-controller", },
	{ .compatible = "fsl,mpc8572-l2-cache-controller", },
	{ .compatible = "fsl,p1020-l2-cache-controller", },
	{ .compatible = "fsl,p1021-l2-cache-controller", },
	{ .compatible = "fsl,p2020-l2-cache-controller", },
	{},
};
MODULE_DEVICE_TABLE(of, mpc85xx_l2_err_of_match);

static struct platform_driver mpc85xx_l2_err_driver = {
	.probe = mpc85xx_l2_err_probe,
	.remove = mpc85xx_l2_err_remove,
	.driver = {
		.name = "mpc85xx_l2_err",
		.owner = THIS_MODULE,
		.of_match_table = mpc85xx_l2_err_of_match,
	},
};

/**************************** MC Err device ***************************/

/*
 * Taken from table 8-55 in the MPC8641 User's Manual and/or 9-61 in the
 * MPC8572 User's Manual.  Each line represents a syndrome bit column as a
 * 64-bit value, but split into an upper and lower 32-bit chunk.  The labels
 * below correspond to Freescale's manuals.
 */
static unsigned int ecc_table[16] = {
	/* MSB           LSB */
	/* [0:31]    [32:63] */
	0xf00fe11e, 0xc33c0ff7,	/* Syndrome bit 7 */
	0x00ff00ff, 0x00fff0ff,
	0x0f0f0f0f, 0x0f0fff00,
	0x11113333, 0x7777000f,
	0x22224444, 0x8888222f,
	0x44448888, 0xffff4441,
	0x8888ffff, 0x11118882,
	0xffff1111, 0x22221114,	/* Syndrome bit 0 */
};

/*
 * Calculate the correct ECC value for a 64-bit value specified by high:low
 */
static u8 calculate_ecc(u32 high, u32 low)
{
	u32 mask_low;
	u32 mask_high;
	int bit_cnt;
	u8 ecc = 0;
	int i;
	int j;

	for (i = 0; i < 8; i++) {
		mask_high = ecc_table[i * 2];
		mask_low = ecc_table[i * 2 + 1];
		bit_cnt = 0;

		for (j = 0; j < 32; j++) {
			if ((mask_high >> j) & 1)
				bit_cnt ^= (high >> j) & 1;
			if ((mask_low >> j) & 1)
				bit_cnt ^= (low >> j) & 1;
		}

		ecc |= bit_cnt << i;
	}

	return ecc;
}

/*
 * Create the syndrome code which is generated if the data line specified by
 * 'bit' failed.  Eg generate an 8-bit codes seen in Table 8-55 in the MPC8641
 * User's Manual and 9-61 in the MPC8572 User's Manual.
 */
static u8 syndrome_from_bit(unsigned int bit) {
	int i;
	u8 syndrome = 0;

	/*
	 * Cycle through the upper or lower 32-bit portion of each value in
	 * ecc_table depending on if 'bit' is in the upper or lower half of
	 * 64-bit data.
	 */
	for (i = bit < 32; i < 16; i += 2)
		syndrome |= ((ecc_table[i] >> (bit % 32)) & 1) << (i / 2);

	return syndrome;
}

/*
 * Decode data and ecc syndrome to determine what went wrong
 * Note: This can only decode single-bit errors
 */
static void sbe_ecc_decode(u32 cap_high, u32 cap_low, u32 cap_ecc,
		       int *bad_data_bit, int *bad_ecc_bit)
{
	int i;
	u8 syndrome;

	*bad_data_bit = -1;
	*bad_ecc_bit = -1;

	/*
	 * Calculate the ECC of the captured data and XOR it with the captured
	 * ECC to find an ECC syndrome value we can search for
	 */
	syndrome = calculate_ecc(cap_high, cap_low) ^ cap_ecc;

	/* Check if a data line is stuck... */
	for (i = 0; i < 64; i++) {
		if (syndrome == syndrome_from_bit(i)) {
			*bad_data_bit = i;
			return;
		}
	}

	/* If data is correct, check ECC bits for errors... */
	for (i = 0; i < 8; i++) {
		if ((syndrome >> i) & 0x1) {
			*bad_ecc_bit = i;
			return;
		}
	}
}

static void mpc85xx_mc_check(struct mem_ctl_info *mci)
{
	struct mpc85xx_mc_pdata *pdata = mci->pvt_info;
	struct csrow_info *csrow;
	u32 bus_width;
	u32 err_detect;
	u32 syndrome;
	u32 err_addr;
	u32 pfn;
	int row_index;
	u32 cap_high;
	u32 cap_low;
	int bad_data_bit;
	int bad_ecc_bit;

	err_detect = in_be32(pdata->mc_vbase + MPC85XX_MC_ERR_DETECT);
	if (!err_detect)
		return;

	mpc85xx_mc_printk(mci, KERN_ERR, "Err Detect Register: %#8.8x\n",
			  err_detect);

	/* no more processing if not ECC bit errors */
	if (!(err_detect & (DDR_EDE_SBE | DDR_EDE_MBE))) {
		out_be32(pdata->mc_vbase + MPC85XX_MC_ERR_DETECT, err_detect);
		return;
	}

	syndrome = in_be32(pdata->mc_vbase + MPC85XX_MC_CAPTURE_ECC);

	/* Mask off appropriate bits of syndrome based on bus width */
	bus_width = (in_be32(pdata->mc_vbase + MPC85XX_MC_DDR_SDRAM_CFG) &
			DSC_DBW_MASK) ? 32 : 64;
	if (bus_width == 64)
		syndrome &= 0xff;
	else
		syndrome &= 0xffff;

	err_addr = in_be32(pdata->mc_vbase + MPC85XX_MC_CAPTURE_ADDRESS);
	pfn = err_addr >> PAGE_SHIFT;

	for (row_index = 0; row_index < mci->nr_csrows; row_index++) {
		csrow = mci->csrows[row_index];
		if ((pfn >= csrow->first_page) && (pfn <= csrow->last_page))
			break;
	}

	cap_high = in_be32(pdata->mc_vbase + MPC85XX_MC_CAPTURE_DATA_HI);
	cap_low = in_be32(pdata->mc_vbase + MPC85XX_MC_CAPTURE_DATA_LO);

	/*
	 * Analyze single-bit errors on 64-bit wide buses
	 * TODO: Add support for 32-bit wide buses
	 */
	if ((err_detect & DDR_EDE_SBE) && (bus_width == 64)) {
		sbe_ecc_decode(cap_high, cap_low, syndrome,
				&bad_data_bit, &bad_ecc_bit);

		if (bad_data_bit != -1)
			mpc85xx_mc_printk(mci, KERN_ERR,
				"Faulty Data bit: %d\n", bad_data_bit);
		if (bad_ecc_bit != -1)
			mpc85xx_mc_printk(mci, KERN_ERR,
				"Faulty ECC bit: %d\n", bad_ecc_bit);

		mpc85xx_mc_printk(mci, KERN_ERR,
			"Expected Data / ECC:\t%#8.8x_%08x / %#2.2x\n",
			cap_high ^ (1 << (bad_data_bit - 32)),
			cap_low ^ (1 << bad_data_bit),
			syndrome ^ (1 << bad_ecc_bit));
	}

	mpc85xx_mc_printk(mci, KERN_ERR,
			"Captured Data / ECC:\t%#8.8x_%08x / %#2.2x\n",
			cap_high, cap_low, syndrome);
	mpc85xx_mc_printk(mci, KERN_ERR, "Err addr: %#8.8x\n", err_addr);
	mpc85xx_mc_printk(mci, KERN_ERR, "PFN: %#8.8x\n", pfn);

	/* we are out of range */
	if (row_index == mci->nr_csrows)
		mpc85xx_mc_printk(mci, KERN_ERR, "PFN out of range!\n");

	if (err_detect & DDR_EDE_SBE)
		edac_mc_handle_error(HW_EVENT_ERR_CORRECTED, mci, 1,
				     pfn, err_addr & ~PAGE_MASK, syndrome,
				     row_index, 0, -1,
				     mci->ctl_name, "");

	if (err_detect & DDR_EDE_MBE)
		edac_mc_handle_error(HW_EVENT_ERR_UNCORRECTED, mci, 1,
				     pfn, err_addr & ~PAGE_MASK, syndrome,
				     row_index, 0, -1,
				     mci->ctl_name, "");

	out_be32(pdata->mc_vbase + MPC85XX_MC_ERR_DETECT, err_detect);
}

static irqreturn_t mpc85xx_mc_isr(int irq, void *dev_id)
{
	struct mem_ctl_info *mci = dev_id;
	struct mpc85xx_mc_pdata *pdata = mci->pvt_info;
	u32 err_detect;

	err_detect = in_be32(pdata->mc_vbase + MPC85XX_MC_ERR_DETECT);
	if (!err_detect)
		return IRQ_NONE;

	mpc85xx_mc_check(mci);

	return IRQ_HANDLED;
}

static void mpc85xx_init_csrows(struct mem_ctl_info *mci)
{
	struct mpc85xx_mc_pdata *pdata = mci->pvt_info;
	struct csrow_info *csrow;
	struct dimm_info *dimm;
	u32 sdram_ctl;
	u32 sdtype;
	enum mem_type mtype;
	u32 cs_bnds;
	int index;

	sdram_ctl = in_be32(pdata->mc_vbase + MPC85XX_MC_DDR_SDRAM_CFG);

	sdtype = sdram_ctl & DSC_SDTYPE_MASK;
	if (sdram_ctl & DSC_RD_EN) {
		switch (sdtype) {
		case DSC_SDTYPE_DDR:
			mtype = MEM_RDDR;
			break;
		case DSC_SDTYPE_DDR2:
			mtype = MEM_RDDR2;
			break;
		case DSC_SDTYPE_DDR3:
			mtype = MEM_RDDR3;
			break;
		default:
			mtype = MEM_UNKNOWN;
			break;
		}
	} else {
		switch (sdtype) {
		case DSC_SDTYPE_DDR:
			mtype = MEM_DDR;
			break;
		case DSC_SDTYPE_DDR2:
			mtype = MEM_DDR2;
			break;
		case DSC_SDTYPE_DDR3:
			mtype = MEM_DDR3;
			break;
		default:
			mtype = MEM_UNKNOWN;
			break;
		}
	}

	for (index = 0; index < mci->nr_csrows; index++) {
		u32 start;
		u32 end;

		csrow = mci->csrows[index];
		dimm = csrow->channels[0]->dimm;

		cs_bnds = in_be32(pdata->mc_vbase + MPC85XX_MC_CS_BNDS_0 +
				  (index * MPC85XX_MC_CS_BNDS_OFS));

		start = (cs_bnds & 0xffff0000) >> 16;
		end   = (cs_bnds & 0x0000ffff);

		if (start == end)
			continue;	/* not populated */

		start <<= (24 - PAGE_SHIFT);
		end   <<= (24 - PAGE_SHIFT);
		end    |= (1 << (24 - PAGE_SHIFT)) - 1;

		csrow->first_page = start;
		csrow->last_page = end;

		dimm->nr_pages = end + 1 - start;
		dimm->grain = 8;
		dimm->mtype = mtype;
		dimm->dtype = DEV_UNKNOWN;
		if (sdram_ctl & DSC_X32_EN)
			dimm->dtype = DEV_X32;
		dimm->edac_mode = EDAC_SECDED;
	}
}

static int mpc85xx_mc_err_probe(struct platform_device *op)
{
	struct mem_ctl_info *mci;
	struct edac_mc_layer layers[2];
	struct mpc85xx_mc_pdata *pdata;
	struct resource r;
	u32 sdram_ctl;
	int res;

	if (!devres_open_group(&op->dev, mpc85xx_mc_err_probe, GFP_KERNEL))
		return -ENOMEM;

	layers[0].type = EDAC_MC_LAYER_CHIP_SELECT;
	layers[0].size = 4;
	layers[0].is_virt_csrow = true;
	layers[1].type = EDAC_MC_LAYER_CHANNEL;
	layers[1].size = 1;
	layers[1].is_virt_csrow = false;
	mci = edac_mc_alloc(edac_mc_idx, ARRAY_SIZE(layers), layers,
			    sizeof(*pdata));
	if (!mci) {
		devres_release_group(&op->dev, mpc85xx_mc_err_probe);
		return -ENOMEM;
	}

	pdata = mci->pvt_info;
	pdata->name = "mpc85xx_mc_err";
	pdata->irq = NO_IRQ;
	mci->pdev = &op->dev;
	pdata->edac_idx = edac_mc_idx++;
	dev_set_drvdata(mci->pdev, mci);
	mci->ctl_name = pdata->name;
	mci->dev_name = pdata->name;

	res = of_address_to_resource(op->dev.of_node, 0, &r);
	if (res) {
		printk(KERN_ERR "%s: Unable to get resource for MC err regs\n",
		       __func__);
		goto err;
	}

	if (!devm_request_mem_region(&op->dev, r.start, resource_size(&r),
				     pdata->name)) {
		printk(KERN_ERR "%s: Error while requesting mem region\n",
		       __func__);
		res = -EBUSY;
		goto err;
	}

	pdata->mc_vbase = devm_ioremap(&op->dev, r.start, resource_size(&r));
	if (!pdata->mc_vbase) {
		printk(KERN_ERR "%s: Unable to setup MC err regs\n", __func__);
		res = -ENOMEM;
		goto err;
	}

	sdram_ctl = in_be32(pdata->mc_vbase + MPC85XX_MC_DDR_SDRAM_CFG);
	if (!(sdram_ctl & DSC_ECC_EN)) {
		/* no ECC */
		printk(KERN_WARNING "%s: No ECC DIMMs discovered\n", __func__);
		res = -ENODEV;
		goto err;
	}

	edac_dbg(3, "init mci\n");
	mci->mtype_cap = MEM_FLAG_RDDR | MEM_FLAG_RDDR2 |
	    MEM_FLAG_DDR | MEM_FLAG_DDR2;
	mci->edac_ctl_cap = EDAC_FLAG_NONE | EDAC_FLAG_SECDED;
	mci->edac_cap = EDAC_FLAG_SECDED;
	mci->mod_name = EDAC_MOD_STR;
	mci->mod_ver = MPC85XX_REVISION;

	if (edac_op_state == EDAC_OPSTATE_POLL)
		mci->edac_check = mpc85xx_mc_check;

	mci->ctl_page_to_phys = NULL;

	mci->scrub_mode = SCRUB_SW_SRC;

	mpc85xx_init_csrows(mci);

	/* store the original error disable bits */
	orig_ddr_err_disable =
	    in_be32(pdata->mc_vbase + MPC85XX_MC_ERR_DISABLE);
	out_be32(pdata->mc_vbase + MPC85XX_MC_ERR_DISABLE, 0);

	/* clear all error bits */
	out_be32(pdata->mc_vbase + MPC85XX_MC_ERR_DETECT, ~0);

	if (edac_mc_add_mc(mci)) {
		edac_dbg(3, "failed edac_mc_add_mc()\n");
		goto err;
	}

	if (mpc85xx_create_sysfs_attributes(mci)) {
		edac_mc_del_mc(mci->pdev);
		edac_dbg(3, "failed edac_mc_add_mc()\n");
		goto err;
	}

	if (edac_op_state == EDAC_OPSTATE_INT) {
		out_be32(pdata->mc_vbase + MPC85XX_MC_ERR_INT_EN,
			 DDR_EIE_MBEE | DDR_EIE_SBEE);

		/* store the original error management threshold */
		orig_ddr_err_sbe = in_be32(pdata->mc_vbase +
					   MPC85XX_MC_ERR_SBE) & 0xff0000;

		/* set threshold to 1 error per interrupt */
		out_be32(pdata->mc_vbase + MPC85XX_MC_ERR_SBE, 0x10000);

		/* register interrupts */
		pdata->irq = irq_of_parse_and_map(op->dev.of_node, 0);
		res = devm_request_irq(&op->dev, pdata->irq,
				       mpc85xx_mc_isr,
					IRQF_DISABLED | IRQF_SHARED,
				       "[EDAC] MC err", mci);
		if (res < 0) {
			printk(KERN_ERR "%s: Unable to request irq %d for "
			       "MPC85xx DRAM ERR\n", __func__, pdata->irq);
			irq_dispose_mapping(pdata->irq);
			res = -ENODEV;
			goto err2;
		}

		printk(KERN_INFO EDAC_MOD_STR " acquired irq %d for MC\n",
		       pdata->irq);
	}

	devres_remove_group(&op->dev, mpc85xx_mc_err_probe);
	edac_dbg(3, "success\n");
	printk(KERN_INFO EDAC_MOD_STR " MC err registered\n");

	return 0;

err2:
	edac_mc_del_mc(&op->dev);
err:
	devres_release_group(&op->dev, mpc85xx_mc_err_probe);
	edac_mc_free(mci);
	return res;
}

static int mpc85xx_mc_err_remove(struct platform_device *op)
{
	struct mem_ctl_info *mci = dev_get_drvdata(&op->dev);
	struct mpc85xx_mc_pdata *pdata = mci->pvt_info;

	edac_dbg(0, "\n");

	if (edac_op_state == EDAC_OPSTATE_INT) {
		out_be32(pdata->mc_vbase + MPC85XX_MC_ERR_INT_EN, 0);
		irq_dispose_mapping(pdata->irq);
	}

	out_be32(pdata->mc_vbase + MPC85XX_MC_ERR_DISABLE,
		 orig_ddr_err_disable);
	out_be32(pdata->mc_vbase + MPC85XX_MC_ERR_SBE, orig_ddr_err_sbe);

	mpc85xx_remove_sysfs_attributes(mci);
	edac_mc_del_mc(&op->dev);
	edac_mc_free(mci);
	return 0;
}

static struct of_device_id mpc85xx_mc_err_of_match[] = {
/* deprecate the fsl,85.. forms in the future, 2.6.30? */
	{ .compatible = "fsl,8540-memory-controller", },
	{ .compatible = "fsl,8541-memory-controller", },
	{ .compatible = "fsl,8544-memory-controller", },
	{ .compatible = "fsl,8548-memory-controller", },
	{ .compatible = "fsl,8555-memory-controller", },
	{ .compatible = "fsl,8568-memory-controller", },
	{ .compatible = "fsl,mpc8536-memory-controller", },
	{ .compatible = "fsl,mpc8540-memory-controller", },
	{ .compatible = "fsl,mpc8541-memory-controller", },
	{ .compatible = "fsl,mpc8544-memory-controller", },
	{ .compatible = "fsl,mpc8548-memory-controller", },
	{ .compatible = "fsl,mpc8555-memory-controller", },
	{ .compatible = "fsl,mpc8560-memory-controller", },
	{ .compatible = "fsl,mpc8568-memory-controller", },
	{ .compatible = "fsl,mpc8569-memory-controller", },
	{ .compatible = "fsl,mpc8572-memory-controller", },
	{ .compatible = "fsl,mpc8349-memory-controller", },
	{ .compatible = "fsl,p1020-memory-controller", },
	{ .compatible = "fsl,p1021-memory-controller", },
	{ .compatible = "fsl,p2020-memory-controller", },
	{ .compatible = "fsl,qoriq-memory-controller", },
	{},
};
MODULE_DEVICE_TABLE(of, mpc85xx_mc_err_of_match);

static struct platform_driver mpc85xx_mc_err_driver = {
	.probe = mpc85xx_mc_err_probe,
	.remove = mpc85xx_mc_err_remove,
	.driver = {
		.name = "mpc85xx_mc_err",
		.owner = THIS_MODULE,
		.of_match_table = mpc85xx_mc_err_of_match,
	},
};

#ifdef CONFIG_FSL_SOC_BOOKE
static void __init mpc85xx_mc_clear_rfxe(void *data)
{
	orig_hid1[smp_processor_id()] = mfspr(SPRN_HID1);
	mtspr(SPRN_HID1, (orig_hid1[smp_processor_id()] & ~HID1_RFXE));
}
#endif

static int __init mpc85xx_mc_init(void)
{
	int res = 0;
	u32 pvr = 0;

	printk(KERN_INFO "Freescale(R) MPC85xx EDAC driver, "
	       "(C) 2006 Montavista Software\n");

	/* make sure error reporting method is sane */
	switch (edac_op_state) {
	case EDAC_OPSTATE_POLL:
	case EDAC_OPSTATE_INT:
		break;
	default:
		edac_op_state = EDAC_OPSTATE_INT;
		break;
	}

	res = platform_driver_register(&mpc85xx_mc_err_driver);
	if (res)
		printk(KERN_WARNING EDAC_MOD_STR "MC fails to register\n");

	res = platform_driver_register(&mpc85xx_l2_err_driver);
	if (res)
		printk(KERN_WARNING EDAC_MOD_STR "L2 fails to register\n");

#ifdef CONFIG_FSL_SOC_BOOKE
	pvr = mfspr(SPRN_PVR);

	if ((PVR_VER(pvr) == PVR_VER_E500V1) ||
	    (PVR_VER(pvr) == PVR_VER_E500V2)) {
		/*
		 * need to clear HID1[RFXE] to disable machine check int
		 * so we can catch it
		 */
		if (edac_op_state == EDAC_OPSTATE_INT)
			on_each_cpu(mpc85xx_mc_clear_rfxe, NULL, 0);
	}
#endif

	return 0;
}

module_init(mpc85xx_mc_init);

#ifdef CONFIG_FSL_SOC_BOOKE
static void __exit mpc85xx_mc_restore_hid1(void *data)
{
	mtspr(SPRN_HID1, orig_hid1[smp_processor_id()]);
}
#endif

static void __exit mpc85xx_mc_exit(void)
{
#ifdef CONFIG_FSL_SOC_BOOKE
	u32 pvr = mfspr(SPRN_PVR);

	if ((PVR_VER(pvr) == PVR_VER_E500V1) ||
	    (PVR_VER(pvr) == PVR_VER_E500V2)) {
		on_each_cpu(mpc85xx_mc_restore_hid1, NULL, 0);
	}
#endif
	platform_driver_unregister(&mpc85xx_l2_err_driver);
	platform_driver_unregister(&mpc85xx_mc_err_driver);
}

module_exit(mpc85xx_mc_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Montavista Software, Inc.");
module_param(edac_op_state, int, 0444);
MODULE_PARM_DESC(edac_op_state,
		 "EDAC Error Reporting state: 0=Poll, 2=Interrupt");
