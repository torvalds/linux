/*
 * Freescale MPC85xx Memory Controller kernel module
 *
 * Parts Copyrighted (c) 2013 by Freescale Semiconductor, Inc.
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
#include <linux/fsl/edac.h>

#include <linux/of_platform.h>
#include <linux/of_device.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include "edac_module.h"
#include "mpc85xx_edac.h"
#include "fsl_ddr_edac.h"

static int edac_dev_idx;
#ifdef CONFIG_PCI
static int edac_pci_idx;
#endif

/*
 * PCI Err defines
 */
#ifdef CONFIG_PCI
static u32 orig_pci_err_cap_dr;
static u32 orig_pci_err_en;
#endif

static u32 orig_l2_err_disable;

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

	pr_err("PCI error(s) detected\n");
	pr_err("PCI/X ERR_DR register: %#08x\n", err_detect);

	pr_err("PCI/X ERR_ATTRIB register: %#08x\n",
	       in_be32(pdata->pci_vbase + MPC85XX_PCI_ERR_ATTRIB));
	pr_err("PCI/X ERR_ADDR register: %#08x\n",
	       in_be32(pdata->pci_vbase + MPC85XX_PCI_ERR_ADDR));
	pr_err("PCI/X ERR_EXT_ADDR register: %#08x\n",
	       in_be32(pdata->pci_vbase + MPC85XX_PCI_ERR_EXT_ADDR));
	pr_err("PCI/X ERR_DL register: %#08x\n",
	       in_be32(pdata->pci_vbase + MPC85XX_PCI_ERR_DL));
	pr_err("PCI/X ERR_DH register: %#08x\n",
	       in_be32(pdata->pci_vbase + MPC85XX_PCI_ERR_DH));

	/* clear error bits */
	out_be32(pdata->pci_vbase + MPC85XX_PCI_ERR_DR, err_detect);

	if (err_detect & PCI_EDE_PERR_MASK)
		edac_pci_handle_pe(pci, pci->ctl_name);

	if ((err_detect & ~PCI_EDE_MULTI_ERR) & ~PCI_EDE_PERR_MASK)
		edac_pci_handle_npe(pci, pci->ctl_name);
}

static void mpc85xx_pcie_check(struct edac_pci_ctl_info *pci)
{
	struct mpc85xx_pci_pdata *pdata = pci->pvt_info;
	u32 err_detect, err_cap_stat;

	err_detect = in_be32(pdata->pci_vbase + MPC85XX_PCI_ERR_DR);
	err_cap_stat = in_be32(pdata->pci_vbase + MPC85XX_PCI_GAS_TIMR);

	pr_err("PCIe error(s) detected\n");
	pr_err("PCIe ERR_DR register: 0x%08x\n", err_detect);
	pr_err("PCIe ERR_CAP_STAT register: 0x%08x\n", err_cap_stat);
	pr_err("PCIe ERR_CAP_R0 register: 0x%08x\n",
			in_be32(pdata->pci_vbase + MPC85XX_PCIE_ERR_CAP_R0));
	pr_err("PCIe ERR_CAP_R1 register: 0x%08x\n",
			in_be32(pdata->pci_vbase + MPC85XX_PCIE_ERR_CAP_R1));
	pr_err("PCIe ERR_CAP_R2 register: 0x%08x\n",
			in_be32(pdata->pci_vbase + MPC85XX_PCIE_ERR_CAP_R2));
	pr_err("PCIe ERR_CAP_R3 register: 0x%08x\n",
			in_be32(pdata->pci_vbase + MPC85XX_PCIE_ERR_CAP_R3));

	/* clear error bits */
	out_be32(pdata->pci_vbase + MPC85XX_PCI_ERR_DR, err_detect);

	/* reset error capture */
	out_be32(pdata->pci_vbase + MPC85XX_PCI_GAS_TIMR, err_cap_stat | 0x1);
}

static int mpc85xx_pcie_find_capability(struct device_node *np)
{
	struct pci_controller *hose;

	if (!np)
		return -EINVAL;

	hose = pci_find_hose_for_OF_device(np);

	return early_find_capability(hose, 0, 0, PCI_CAP_ID_EXP);
}

static irqreturn_t mpc85xx_pci_isr(int irq, void *dev_id)
{
	struct edac_pci_ctl_info *pci = dev_id;
	struct mpc85xx_pci_pdata *pdata = pci->pvt_info;
	u32 err_detect;

	err_detect = in_be32(pdata->pci_vbase + MPC85XX_PCI_ERR_DR);

	if (!err_detect)
		return IRQ_NONE;

	if (pdata->is_pcie)
		mpc85xx_pcie_check(pci);
	else
		mpc85xx_pci_check(pci);

	return IRQ_HANDLED;
}

static int mpc85xx_pci_err_probe(struct platform_device *op)
{
	struct edac_pci_ctl_info *pci;
	struct mpc85xx_pci_pdata *pdata;
	struct mpc85xx_edac_pci_plat_data *plat_data;
	struct device_node *of_node;
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

	plat_data = op->dev.platform_data;
	if (!plat_data) {
		dev_err(&op->dev, "no platform data");
		res = -ENXIO;
		goto err;
	}
	of_node = plat_data->of_node;

	if (mpc85xx_pcie_find_capability(of_node) > 0)
		pdata->is_pcie = true;

	dev_set_drvdata(&op->dev, pci);
	pci->dev = &op->dev;
	pci->mod_name = EDAC_MOD_STR;
	pci->ctl_name = pdata->name;
	pci->dev_name = dev_name(&op->dev);

	if (edac_op_state == EDAC_OPSTATE_POLL) {
		if (pdata->is_pcie)
			pci->edac_check = mpc85xx_pcie_check;
		else
			pci->edac_check = mpc85xx_pci_check;
	}

	pdata->edac_idx = edac_pci_idx++;

	res = of_address_to_resource(of_node, 0, &r);
	if (res) {
		pr_err("%s: Unable to get resource for PCI err regs\n", __func__);
		goto err;
	}

	/* we only need the error registers */
	r.start += 0xe00;

	if (!devm_request_mem_region(&op->dev, r.start, resource_size(&r),
					pdata->name)) {
		pr_err("%s: Error while requesting mem region\n", __func__);
		res = -EBUSY;
		goto err;
	}

	pdata->pci_vbase = devm_ioremap(&op->dev, r.start, resource_size(&r));
	if (!pdata->pci_vbase) {
		pr_err("%s: Unable to setup PCI err regs\n", __func__);
		res = -ENOMEM;
		goto err;
	}

	if (pdata->is_pcie) {
		orig_pci_err_cap_dr =
		    in_be32(pdata->pci_vbase + MPC85XX_PCI_ERR_ADDR);
		out_be32(pdata->pci_vbase + MPC85XX_PCI_ERR_ADDR, ~0);
		orig_pci_err_en =
		    in_be32(pdata->pci_vbase + MPC85XX_PCI_ERR_EN);
		out_be32(pdata->pci_vbase + MPC85XX_PCI_ERR_EN, 0);
	} else {
		orig_pci_err_cap_dr =
		    in_be32(pdata->pci_vbase + MPC85XX_PCI_ERR_CAP_DR);

		/* PCI master abort is expected during config cycles */
		out_be32(pdata->pci_vbase + MPC85XX_PCI_ERR_CAP_DR, 0x40);

		orig_pci_err_en =
		    in_be32(pdata->pci_vbase + MPC85XX_PCI_ERR_EN);

		/* disable master abort reporting */
		out_be32(pdata->pci_vbase + MPC85XX_PCI_ERR_EN, ~0x40);
	}

	/* clear error bits */
	out_be32(pdata->pci_vbase + MPC85XX_PCI_ERR_DR, ~0);

	/* reset error capture */
	out_be32(pdata->pci_vbase + MPC85XX_PCI_GAS_TIMR, 0x1);

	if (edac_pci_add_device(pci, pdata->edac_idx) > 0) {
		edac_dbg(3, "failed edac_pci_add_device()\n");
		goto err;
	}

	if (edac_op_state == EDAC_OPSTATE_INT) {
		pdata->irq = irq_of_parse_and_map(of_node, 0);
		res = devm_request_irq(&op->dev, pdata->irq,
				       mpc85xx_pci_isr,
				       IRQF_SHARED,
				       "[EDAC] PCI err", pci);
		if (res < 0) {
			pr_err("%s: Unable to request irq %d for MPC85xx PCI err\n",
				__func__, pdata->irq);
			irq_dispose_mapping(pdata->irq);
			res = -ENODEV;
			goto err2;
		}

		pr_info(EDAC_MOD_STR " acquired irq %d for PCI Err\n",
		       pdata->irq);
	}

	if (pdata->is_pcie) {
		/*
		 * Enable all PCIe error interrupt & error detect except invalid
		 * PEX_CONFIG_ADDR/PEX_CONFIG_DATA access interrupt generation
		 * enable bit and invalid PEX_CONFIG_ADDR/PEX_CONFIG_DATA access
		 * detection enable bit. Because PCIe bus code to initialize and
		 * configure these PCIe devices on booting will use some invalid
		 * PEX_CONFIG_ADDR/PEX_CONFIG_DATA, edac driver prints the much
		 * notice information. So disable this detect to fix ugly print.
		 */
		out_be32(pdata->pci_vbase + MPC85XX_PCI_ERR_EN, ~0
			 & ~PEX_ERR_ICCAIE_EN_BIT);
		out_be32(pdata->pci_vbase + MPC85XX_PCI_ERR_ADDR, 0
			 | PEX_ERR_ICCAD_DISR_BIT);
	}

	devres_remove_group(&op->dev, mpc85xx_pci_err_probe);
	edac_dbg(3, "success\n");
	pr_info(EDAC_MOD_STR " PCI err registered\n");

	return 0;

err2:
	edac_pci_del_device(&op->dev);
err:
	edac_pci_free_ctl_info(pci);
	devres_release_group(&op->dev, mpc85xx_pci_err_probe);
	return res;
}

static int mpc85xx_pci_err_remove(struct platform_device *op)
{
	struct edac_pci_ctl_info *pci = dev_get_drvdata(&op->dev);
	struct mpc85xx_pci_pdata *pdata = pci->pvt_info;

	edac_dbg(0, "\n");

	out_be32(pdata->pci_vbase + MPC85XX_PCI_ERR_ADDR, orig_pci_err_cap_dr);
	out_be32(pdata->pci_vbase + MPC85XX_PCI_ERR_EN, orig_pci_err_en);

	edac_pci_del_device(&op->dev);
	edac_pci_free_ctl_info(pci);

	return 0;
}

static const struct platform_device_id mpc85xx_pci_err_match[] = {
	{
		.name = "mpc85xx-pci-edac"
	},
	{}
};

static struct platform_driver mpc85xx_pci_err_driver = {
	.probe = mpc85xx_pci_err_probe,
	.remove = mpc85xx_pci_err_remove,
	.id_table = mpc85xx_pci_err_match,
	.driver = {
		.name = "mpc85xx_pci_err",
		.suppress_bind_attrs = true,
	},
};
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

	pr_err("ECC Error in CPU L2 cache\n");
	pr_err("L2 Error Detect Register: 0x%08x\n", err_detect);
	pr_err("L2 Error Capture Data High Register: 0x%08x\n",
	       in_be32(pdata->l2_vbase + MPC85XX_L2_CAPTDATAHI));
	pr_err("L2 Error Capture Data Lo Register: 0x%08x\n",
	       in_be32(pdata->l2_vbase + MPC85XX_L2_CAPTDATALO));
	pr_err("L2 Error Syndrome Register: 0x%08x\n",
	       in_be32(pdata->l2_vbase + MPC85XX_L2_CAPTECC));
	pr_err("L2 Error Attributes Capture Register: 0x%08x\n",
	       in_be32(pdata->l2_vbase + MPC85XX_L2_ERRATTR));
	pr_err("L2 Error Address Capture Register: 0x%08x\n",
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
	edac_dev->dev = &op->dev;
	dev_set_drvdata(edac_dev->dev, edac_dev);
	edac_dev->ctl_name = pdata->name;
	edac_dev->dev_name = pdata->name;

	res = of_address_to_resource(op->dev.of_node, 0, &r);
	if (res) {
		pr_err("%s: Unable to get resource for L2 err regs\n", __func__);
		goto err;
	}

	/* we only need the error registers */
	r.start += 0xe00;

	if (!devm_request_mem_region(&op->dev, r.start, resource_size(&r),
				     pdata->name)) {
		pr_err("%s: Error while requesting mem region\n", __func__);
		res = -EBUSY;
		goto err;
	}

	pdata->l2_vbase = devm_ioremap(&op->dev, r.start, resource_size(&r));
	if (!pdata->l2_vbase) {
		pr_err("%s: Unable to setup L2 err regs\n", __func__);
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
				       mpc85xx_l2_isr, IRQF_SHARED,
				       "[EDAC] L2 err", edac_dev);
		if (res < 0) {
			pr_err("%s: Unable to request irq %d for MPC85xx L2 err\n",
				__func__, pdata->irq);
			irq_dispose_mapping(pdata->irq);
			res = -ENODEV;
			goto err2;
		}

		pr_info(EDAC_MOD_STR " acquired irq %d for L2 Err\n", pdata->irq);

		edac_dev->op_state = OP_RUNNING_INTERRUPT;

		out_be32(pdata->l2_vbase + MPC85XX_L2_ERRINTEN, L2_EIE_MASK);
	}

	devres_remove_group(&op->dev, mpc85xx_l2_err_probe);

	edac_dbg(3, "success\n");
	pr_info(EDAC_MOD_STR " L2 err registered\n");

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

static const struct of_device_id mpc85xx_l2_err_of_match[] = {
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
	{ .compatible = "fsl,t2080-l2-cache-controller", },
	{},
};
MODULE_DEVICE_TABLE(of, mpc85xx_l2_err_of_match);

static struct platform_driver mpc85xx_l2_err_driver = {
	.probe = mpc85xx_l2_err_probe,
	.remove = mpc85xx_l2_err_remove,
	.driver = {
		.name = "mpc85xx_l2_err",
		.of_match_table = mpc85xx_l2_err_of_match,
	},
};

static const struct of_device_id mpc85xx_mc_err_of_match[] = {
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
	.probe = fsl_mc_err_probe,
	.remove = fsl_mc_err_remove,
	.driver = {
		.name = "mpc85xx_mc_err",
		.of_match_table = mpc85xx_mc_err_of_match,
	},
};

static struct platform_driver * const drivers[] = {
	&mpc85xx_mc_err_driver,
	&mpc85xx_l2_err_driver,
#ifdef CONFIG_PCI
	&mpc85xx_pci_err_driver,
#endif
};

static int __init mpc85xx_mc_init(void)
{
	int res = 0;
	u32 __maybe_unused pvr = 0;

	pr_info("Freescale(R) MPC85xx EDAC driver, (C) 2006 Montavista Software\n");

	/* make sure error reporting method is sane */
	switch (edac_op_state) {
	case EDAC_OPSTATE_POLL:
	case EDAC_OPSTATE_INT:
		break;
	default:
		edac_op_state = EDAC_OPSTATE_INT;
		break;
	}

	res = platform_register_drivers(drivers, ARRAY_SIZE(drivers));
	if (res)
		pr_warn(EDAC_MOD_STR "drivers fail to register\n");

	return 0;
}

module_init(mpc85xx_mc_init);

static void __exit mpc85xx_mc_exit(void)
{
	platform_unregister_drivers(drivers, ARRAY_SIZE(drivers));
}

module_exit(mpc85xx_mc_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Montavista Software, Inc.");
module_param(edac_op_state, int, 0444);
MODULE_PARM_DESC(edac_op_state, "EDAC Error Reporting state: 0=Poll, 2=Interrupt");
