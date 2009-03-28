/*
 * Marvell MV64x60 Memory Controller kernel module for PPC platforms
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
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/edac.h>

#include "edac_core.h"
#include "edac_module.h"
#include "mv64x60_edac.h"

static const char *mv64x60_ctl_name = "MV64x60";
static int edac_dev_idx;
static int edac_pci_idx;
static int edac_mc_idx;

/*********************** PCI err device **********************************/
#ifdef CONFIG_PCI
static void mv64x60_pci_check(struct edac_pci_ctl_info *pci)
{
	struct mv64x60_pci_pdata *pdata = pci->pvt_info;
	u32 cause;

	cause = in_le32(pdata->pci_vbase + MV64X60_PCI_ERROR_CAUSE);
	if (!cause)
		return;

	printk(KERN_ERR "Error in PCI %d Interface\n", pdata->pci_hose);
	printk(KERN_ERR "Cause register: 0x%08x\n", cause);
	printk(KERN_ERR "Address Low: 0x%08x\n",
	       in_le32(pdata->pci_vbase + MV64X60_PCI_ERROR_ADDR_LO));
	printk(KERN_ERR "Address High: 0x%08x\n",
	       in_le32(pdata->pci_vbase + MV64X60_PCI_ERROR_ADDR_HI));
	printk(KERN_ERR "Attribute: 0x%08x\n",
	       in_le32(pdata->pci_vbase + MV64X60_PCI_ERROR_ATTR));
	printk(KERN_ERR "Command: 0x%08x\n",
	       in_le32(pdata->pci_vbase + MV64X60_PCI_ERROR_CMD));
	out_le32(pdata->pci_vbase + MV64X60_PCI_ERROR_CAUSE, ~cause);

	if (cause & MV64X60_PCI_PE_MASK)
		edac_pci_handle_pe(pci, pci->ctl_name);

	if (!(cause & MV64X60_PCI_PE_MASK))
		edac_pci_handle_npe(pci, pci->ctl_name);
}

static irqreturn_t mv64x60_pci_isr(int irq, void *dev_id)
{
	struct edac_pci_ctl_info *pci = dev_id;
	struct mv64x60_pci_pdata *pdata = pci->pvt_info;
	u32 val;

	val = in_le32(pdata->pci_vbase + MV64X60_PCI_ERROR_CAUSE);
	if (!val)
		return IRQ_NONE;

	mv64x60_pci_check(pci);

	return IRQ_HANDLED;
}

/*
 * Bit 0 of MV64x60_PCIx_ERR_MASK does not exist on the 64360 and because of
 * errata FEr-#11 and FEr-##16 for the 64460, it should be 0 on that chip as
 * well.  IOW, don't set bit 0.
 */

/* Erratum FEr PCI-#16: clear bit 0 of PCI SERRn Mask reg. */
static int __init mv64x60_pci_fixup(struct platform_device *pdev)
{
	struct resource *r;
	void __iomem *pci_serr;

	r = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (!r) {
		printk(KERN_ERR "%s: Unable to get resource for "
		       "PCI err regs\n", __func__);
		return -ENOENT;
	}

	pci_serr = ioremap(r->start, r->end - r->start + 1);
	if (!pci_serr)
		return -ENOMEM;

	out_le32(pci_serr, in_le32(pci_serr) & ~0x1);
	iounmap(pci_serr);

	return 0;
}

static int __devinit mv64x60_pci_err_probe(struct platform_device *pdev)
{
	struct edac_pci_ctl_info *pci;
	struct mv64x60_pci_pdata *pdata;
	struct resource *r;
	int res = 0;

	if (!devres_open_group(&pdev->dev, mv64x60_pci_err_probe, GFP_KERNEL))
		return -ENOMEM;

	pci = edac_pci_alloc_ctl_info(sizeof(*pdata), "mv64x60_pci_err");
	if (!pci)
		return -ENOMEM;

	pdata = pci->pvt_info;

	pdata->pci_hose = pdev->id;
	pdata->name = "mpc85xx_pci_err";
	pdata->irq = NO_IRQ;
	platform_set_drvdata(pdev, pci);
	pci->dev = &pdev->dev;
	pci->dev_name = dev_name(&pdev->dev);
	pci->mod_name = EDAC_MOD_STR;
	pci->ctl_name = pdata->name;

	if (edac_op_state == EDAC_OPSTATE_POLL)
		pci->edac_check = mv64x60_pci_check;

	pdata->edac_idx = edac_pci_idx++;

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!r) {
		printk(KERN_ERR "%s: Unable to get resource for "
		       "PCI err regs\n", __func__);
		res = -ENOENT;
		goto err;
	}

	if (!devm_request_mem_region(&pdev->dev,
				     r->start,
				     r->end - r->start + 1,
				     pdata->name)) {
		printk(KERN_ERR "%s: Error while requesting mem region\n",
		       __func__);
		res = -EBUSY;
		goto err;
	}

	pdata->pci_vbase = devm_ioremap(&pdev->dev,
					r->start,
					r->end - r->start + 1);
	if (!pdata->pci_vbase) {
		printk(KERN_ERR "%s: Unable to setup PCI err regs\n", __func__);
		res = -ENOMEM;
		goto err;
	}

	res = mv64x60_pci_fixup(pdev);
	if (res < 0) {
		printk(KERN_ERR "%s: PCI fixup failed\n", __func__);
		goto err;
	}

	out_le32(pdata->pci_vbase + MV64X60_PCI_ERROR_CAUSE, 0);
	out_le32(pdata->pci_vbase + MV64X60_PCI_ERROR_MASK, 0);
	out_le32(pdata->pci_vbase + MV64X60_PCI_ERROR_MASK,
		 MV64X60_PCIx_ERR_MASK_VAL);

	if (edac_pci_add_device(pci, pdata->edac_idx) > 0) {
		debugf3("%s(): failed edac_pci_add_device()\n", __func__);
		goto err;
	}

	if (edac_op_state == EDAC_OPSTATE_INT) {
		pdata->irq = platform_get_irq(pdev, 0);
		res = devm_request_irq(&pdev->dev,
				       pdata->irq,
				       mv64x60_pci_isr,
				       IRQF_DISABLED,
				       "[EDAC] PCI err",
				       pci);
		if (res < 0) {
			printk(KERN_ERR "%s: Unable to request irq %d for "
			       "MV64x60 PCI ERR\n", __func__, pdata->irq);
			res = -ENODEV;
			goto err2;
		}
		printk(KERN_INFO EDAC_MOD_STR " acquired irq %d for PCI Err\n",
		       pdata->irq);
	}

	devres_remove_group(&pdev->dev, mv64x60_pci_err_probe);

	/* get this far and it's successful */
	debugf3("%s(): success\n", __func__);

	return 0;

err2:
	edac_pci_del_device(&pdev->dev);
err:
	edac_pci_free_ctl_info(pci);
	devres_release_group(&pdev->dev, mv64x60_pci_err_probe);
	return res;
}

static int mv64x60_pci_err_remove(struct platform_device *pdev)
{
	struct edac_pci_ctl_info *pci = platform_get_drvdata(pdev);

	debugf0("%s()\n", __func__);

	edac_pci_del_device(&pdev->dev);

	edac_pci_free_ctl_info(pci);

	return 0;
}

static struct platform_driver mv64x60_pci_err_driver = {
	.probe = mv64x60_pci_err_probe,
	.remove = __devexit_p(mv64x60_pci_err_remove),
	.driver = {
		   .name = "mv64x60_pci_err",
	}
};

#endif /* CONFIG_PCI */

/*********************** SRAM err device **********************************/
static void mv64x60_sram_check(struct edac_device_ctl_info *edac_dev)
{
	struct mv64x60_sram_pdata *pdata = edac_dev->pvt_info;
	u32 cause;

	cause = in_le32(pdata->sram_vbase + MV64X60_SRAM_ERR_CAUSE);
	if (!cause)
		return;

	printk(KERN_ERR "Error in internal SRAM\n");
	printk(KERN_ERR "Cause register: 0x%08x\n", cause);
	printk(KERN_ERR "Address Low: 0x%08x\n",
	       in_le32(pdata->sram_vbase + MV64X60_SRAM_ERR_ADDR_LO));
	printk(KERN_ERR "Address High: 0x%08x\n",
	       in_le32(pdata->sram_vbase + MV64X60_SRAM_ERR_ADDR_HI));
	printk(KERN_ERR "Data Low: 0x%08x\n",
	       in_le32(pdata->sram_vbase + MV64X60_SRAM_ERR_DATA_LO));
	printk(KERN_ERR "Data High: 0x%08x\n",
	       in_le32(pdata->sram_vbase + MV64X60_SRAM_ERR_DATA_HI));
	printk(KERN_ERR "Parity: 0x%08x\n",
	       in_le32(pdata->sram_vbase + MV64X60_SRAM_ERR_PARITY));
	out_le32(pdata->sram_vbase + MV64X60_SRAM_ERR_CAUSE, 0);

	edac_device_handle_ue(edac_dev, 0, 0, edac_dev->ctl_name);
}

static irqreturn_t mv64x60_sram_isr(int irq, void *dev_id)
{
	struct edac_device_ctl_info *edac_dev = dev_id;
	struct mv64x60_sram_pdata *pdata = edac_dev->pvt_info;
	u32 cause;

	cause = in_le32(pdata->sram_vbase + MV64X60_SRAM_ERR_CAUSE);
	if (!cause)
		return IRQ_NONE;

	mv64x60_sram_check(edac_dev);

	return IRQ_HANDLED;
}

static int __devinit mv64x60_sram_err_probe(struct platform_device *pdev)
{
	struct edac_device_ctl_info *edac_dev;
	struct mv64x60_sram_pdata *pdata;
	struct resource *r;
	int res = 0;

	if (!devres_open_group(&pdev->dev, mv64x60_sram_err_probe, GFP_KERNEL))
		return -ENOMEM;

	edac_dev = edac_device_alloc_ctl_info(sizeof(*pdata),
					      "sram", 1, NULL, 0, 0, NULL, 0,
					      edac_dev_idx);
	if (!edac_dev) {
		devres_release_group(&pdev->dev, mv64x60_sram_err_probe);
		return -ENOMEM;
	}

	pdata = edac_dev->pvt_info;
	pdata->name = "mv64x60_sram_err";
	pdata->irq = NO_IRQ;
	edac_dev->dev = &pdev->dev;
	platform_set_drvdata(pdev, edac_dev);
	edac_dev->dev_name = dev_name(&pdev->dev);

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!r) {
		printk(KERN_ERR "%s: Unable to get resource for "
		       "SRAM err regs\n", __func__);
		res = -ENOENT;
		goto err;
	}

	if (!devm_request_mem_region(&pdev->dev,
				     r->start,
				     r->end - r->start + 1,
				     pdata->name)) {
		printk(KERN_ERR "%s: Error while request mem region\n",
		       __func__);
		res = -EBUSY;
		goto err;
	}

	pdata->sram_vbase = devm_ioremap(&pdev->dev,
					 r->start,
					 r->end - r->start + 1);
	if (!pdata->sram_vbase) {
		printk(KERN_ERR "%s: Unable to setup SRAM err regs\n",
		       __func__);
		res = -ENOMEM;
		goto err;
	}

	/* setup SRAM err registers */
	out_le32(pdata->sram_vbase + MV64X60_SRAM_ERR_CAUSE, 0);

	edac_dev->mod_name = EDAC_MOD_STR;
	edac_dev->ctl_name = pdata->name;

	if (edac_op_state == EDAC_OPSTATE_POLL)
		edac_dev->edac_check = mv64x60_sram_check;

	pdata->edac_idx = edac_dev_idx++;

	if (edac_device_add_device(edac_dev) > 0) {
		debugf3("%s(): failed edac_device_add_device()\n", __func__);
		goto err;
	}

	if (edac_op_state == EDAC_OPSTATE_INT) {
		pdata->irq = platform_get_irq(pdev, 0);
		res = devm_request_irq(&pdev->dev,
				       pdata->irq,
				       mv64x60_sram_isr,
				       IRQF_DISABLED,
				       "[EDAC] SRAM err",
				       edac_dev);
		if (res < 0) {
			printk(KERN_ERR
			       "%s: Unable to request irq %d for "
			       "MV64x60 SRAM ERR\n", __func__, pdata->irq);
			res = -ENODEV;
			goto err2;
		}

		printk(KERN_INFO EDAC_MOD_STR " acquired irq %d for SRAM Err\n",
		       pdata->irq);
	}

	devres_remove_group(&pdev->dev, mv64x60_sram_err_probe);

	/* get this far and it's successful */
	debugf3("%s(): success\n", __func__);

	return 0;

err2:
	edac_device_del_device(&pdev->dev);
err:
	devres_release_group(&pdev->dev, mv64x60_sram_err_probe);
	edac_device_free_ctl_info(edac_dev);
	return res;
}

static int mv64x60_sram_err_remove(struct platform_device *pdev)
{
	struct edac_device_ctl_info *edac_dev = platform_get_drvdata(pdev);

	debugf0("%s()\n", __func__);

	edac_device_del_device(&pdev->dev);
	edac_device_free_ctl_info(edac_dev);

	return 0;
}

static struct platform_driver mv64x60_sram_err_driver = {
	.probe = mv64x60_sram_err_probe,
	.remove = mv64x60_sram_err_remove,
	.driver = {
		   .name = "mv64x60_sram_err",
	}
};

/*********************** CPU err device **********************************/
static void mv64x60_cpu_check(struct edac_device_ctl_info *edac_dev)
{
	struct mv64x60_cpu_pdata *pdata = edac_dev->pvt_info;
	u32 cause;

	cause = in_le32(pdata->cpu_vbase[1] + MV64x60_CPU_ERR_CAUSE) &
	    MV64x60_CPU_CAUSE_MASK;
	if (!cause)
		return;

	printk(KERN_ERR "Error on CPU interface\n");
	printk(KERN_ERR "Cause register: 0x%08x\n", cause);
	printk(KERN_ERR "Address Low: 0x%08x\n",
	       in_le32(pdata->cpu_vbase[0] + MV64x60_CPU_ERR_ADDR_LO));
	printk(KERN_ERR "Address High: 0x%08x\n",
	       in_le32(pdata->cpu_vbase[0] + MV64x60_CPU_ERR_ADDR_HI));
	printk(KERN_ERR "Data Low: 0x%08x\n",
	       in_le32(pdata->cpu_vbase[1] + MV64x60_CPU_ERR_DATA_LO));
	printk(KERN_ERR "Data High: 0x%08x\n",
	       in_le32(pdata->cpu_vbase[1] + MV64x60_CPU_ERR_DATA_HI));
	printk(KERN_ERR "Parity: 0x%08x\n",
	       in_le32(pdata->cpu_vbase[1] + MV64x60_CPU_ERR_PARITY));
	out_le32(pdata->cpu_vbase[1] + MV64x60_CPU_ERR_CAUSE, 0);

	edac_device_handle_ue(edac_dev, 0, 0, edac_dev->ctl_name);
}

static irqreturn_t mv64x60_cpu_isr(int irq, void *dev_id)
{
	struct edac_device_ctl_info *edac_dev = dev_id;
	struct mv64x60_cpu_pdata *pdata = edac_dev->pvt_info;
	u32 cause;

	cause = in_le32(pdata->cpu_vbase[1] + MV64x60_CPU_ERR_CAUSE) &
	    MV64x60_CPU_CAUSE_MASK;
	if (!cause)
		return IRQ_NONE;

	mv64x60_cpu_check(edac_dev);

	return IRQ_HANDLED;
}

static int __devinit mv64x60_cpu_err_probe(struct platform_device *pdev)
{
	struct edac_device_ctl_info *edac_dev;
	struct resource *r;
	struct mv64x60_cpu_pdata *pdata;
	int res = 0;

	if (!devres_open_group(&pdev->dev, mv64x60_cpu_err_probe, GFP_KERNEL))
		return -ENOMEM;

	edac_dev = edac_device_alloc_ctl_info(sizeof(*pdata),
					      "cpu", 1, NULL, 0, 0, NULL, 0,
					      edac_dev_idx);
	if (!edac_dev) {
		devres_release_group(&pdev->dev, mv64x60_cpu_err_probe);
		return -ENOMEM;
	}

	pdata = edac_dev->pvt_info;
	pdata->name = "mv64x60_cpu_err";
	pdata->irq = NO_IRQ;
	edac_dev->dev = &pdev->dev;
	platform_set_drvdata(pdev, edac_dev);
	edac_dev->dev_name = dev_name(&pdev->dev);

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!r) {
		printk(KERN_ERR "%s: Unable to get resource for "
		       "CPU err regs\n", __func__);
		res = -ENOENT;
		goto err;
	}

	if (!devm_request_mem_region(&pdev->dev,
				     r->start,
				     r->end - r->start + 1,
				     pdata->name)) {
		printk(KERN_ERR "%s: Error while requesting mem region\n",
		       __func__);
		res = -EBUSY;
		goto err;
	}

	pdata->cpu_vbase[0] = devm_ioremap(&pdev->dev,
					   r->start,
					   r->end - r->start + 1);
	if (!pdata->cpu_vbase[0]) {
		printk(KERN_ERR "%s: Unable to setup CPU err regs\n", __func__);
		res = -ENOMEM;
		goto err;
	}

	r = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (!r) {
		printk(KERN_ERR "%s: Unable to get resource for "
		       "CPU err regs\n", __func__);
		res = -ENOENT;
		goto err;
	}

	if (!devm_request_mem_region(&pdev->dev,
				     r->start,
				     r->end - r->start + 1,
				     pdata->name)) {
		printk(KERN_ERR "%s: Error while requesting mem region\n",
		       __func__);
		res = -EBUSY;
		goto err;
	}

	pdata->cpu_vbase[1] = devm_ioremap(&pdev->dev,
					   r->start,
					   r->end - r->start + 1);
	if (!pdata->cpu_vbase[1]) {
		printk(KERN_ERR "%s: Unable to setup CPU err regs\n", __func__);
		res = -ENOMEM;
		goto err;
	}

	/* setup CPU err registers */
	out_le32(pdata->cpu_vbase[1] + MV64x60_CPU_ERR_CAUSE, 0);
	out_le32(pdata->cpu_vbase[1] + MV64x60_CPU_ERR_MASK, 0);
	out_le32(pdata->cpu_vbase[1] + MV64x60_CPU_ERR_MASK, 0x000000ff);

	edac_dev->mod_name = EDAC_MOD_STR;
	edac_dev->ctl_name = pdata->name;
	if (edac_op_state == EDAC_OPSTATE_POLL)
		edac_dev->edac_check = mv64x60_cpu_check;

	pdata->edac_idx = edac_dev_idx++;

	if (edac_device_add_device(edac_dev) > 0) {
		debugf3("%s(): failed edac_device_add_device()\n", __func__);
		goto err;
	}

	if (edac_op_state == EDAC_OPSTATE_INT) {
		pdata->irq = platform_get_irq(pdev, 0);
		res = devm_request_irq(&pdev->dev,
				       pdata->irq,
				       mv64x60_cpu_isr,
				       IRQF_DISABLED,
				       "[EDAC] CPU err",
				       edac_dev);
		if (res < 0) {
			printk(KERN_ERR
			       "%s: Unable to request irq %d for MV64x60 "
			       "CPU ERR\n", __func__, pdata->irq);
			res = -ENODEV;
			goto err2;
		}

		printk(KERN_INFO EDAC_MOD_STR
		       " acquired irq %d for CPU Err\n", pdata->irq);
	}

	devres_remove_group(&pdev->dev, mv64x60_cpu_err_probe);

	/* get this far and it's successful */
	debugf3("%s(): success\n", __func__);

	return 0;

err2:
	edac_device_del_device(&pdev->dev);
err:
	devres_release_group(&pdev->dev, mv64x60_cpu_err_probe);
	edac_device_free_ctl_info(edac_dev);
	return res;
}

static int mv64x60_cpu_err_remove(struct platform_device *pdev)
{
	struct edac_device_ctl_info *edac_dev = platform_get_drvdata(pdev);

	debugf0("%s()\n", __func__);

	edac_device_del_device(&pdev->dev);
	edac_device_free_ctl_info(edac_dev);
	return 0;
}

static struct platform_driver mv64x60_cpu_err_driver = {
	.probe = mv64x60_cpu_err_probe,
	.remove = mv64x60_cpu_err_remove,
	.driver = {
		   .name = "mv64x60_cpu_err",
	}
};

/*********************** DRAM err device **********************************/

static void mv64x60_mc_check(struct mem_ctl_info *mci)
{
	struct mv64x60_mc_pdata *pdata = mci->pvt_info;
	u32 reg;
	u32 err_addr;
	u32 sdram_ecc;
	u32 comp_ecc;
	u32 syndrome;

	reg = in_le32(pdata->mc_vbase + MV64X60_SDRAM_ERR_ADDR);
	if (!reg)
		return;

	err_addr = reg & ~0x3;
	sdram_ecc = in_le32(pdata->mc_vbase + MV64X60_SDRAM_ERR_ECC_RCVD);
	comp_ecc = in_le32(pdata->mc_vbase + MV64X60_SDRAM_ERR_ECC_CALC);
	syndrome = sdram_ecc ^ comp_ecc;

	/* first bit clear in ECC Err Reg, 1 bit error, correctable by HW */
	if (!(reg & 0x1))
		edac_mc_handle_ce(mci, err_addr >> PAGE_SHIFT,
				  err_addr & PAGE_MASK, syndrome, 0, 0,
				  mci->ctl_name);
	else	/* 2 bit error, UE */
		edac_mc_handle_ue(mci, err_addr >> PAGE_SHIFT,
				  err_addr & PAGE_MASK, 0, mci->ctl_name);

	/* clear the error */
	out_le32(pdata->mc_vbase + MV64X60_SDRAM_ERR_ADDR, 0);
}

static irqreturn_t mv64x60_mc_isr(int irq, void *dev_id)
{
	struct mem_ctl_info *mci = dev_id;
	struct mv64x60_mc_pdata *pdata = mci->pvt_info;
	u32 reg;

	reg = in_le32(pdata->mc_vbase + MV64X60_SDRAM_ERR_ADDR);
	if (!reg)
		return IRQ_NONE;

	/* writing 0's to the ECC err addr in check function clears irq */
	mv64x60_mc_check(mci);

	return IRQ_HANDLED;
}

static void get_total_mem(struct mv64x60_mc_pdata *pdata)
{
	struct device_node *np = NULL;
	const unsigned int *reg;

	np = of_find_node_by_type(NULL, "memory");
	if (!np)
		return;

	reg = of_get_property(np, "reg", NULL);

	pdata->total_mem = reg[1];
}

static void mv64x60_init_csrows(struct mem_ctl_info *mci,
				struct mv64x60_mc_pdata *pdata)
{
	struct csrow_info *csrow;
	u32 devtype;
	u32 ctl;

	get_total_mem(pdata);

	ctl = in_le32(pdata->mc_vbase + MV64X60_SDRAM_CONFIG);

	csrow = &mci->csrows[0];
	csrow->first_page = 0;
	csrow->nr_pages = pdata->total_mem >> PAGE_SHIFT;
	csrow->last_page = csrow->first_page + csrow->nr_pages - 1;
	csrow->grain = 8;

	csrow->mtype = (ctl & MV64X60_SDRAM_REGISTERED) ? MEM_RDDR : MEM_DDR;

	devtype = (ctl >> 20) & 0x3;
	switch (devtype) {
	case 0x0:
		csrow->dtype = DEV_X32;
		break;
	case 0x2:		/* could be X8 too, but no way to tell */
		csrow->dtype = DEV_X16;
		break;
	case 0x3:
		csrow->dtype = DEV_X4;
		break;
	default:
		csrow->dtype = DEV_UNKNOWN;
		break;
	}

	csrow->edac_mode = EDAC_SECDED;
}

static int __devinit mv64x60_mc_err_probe(struct platform_device *pdev)
{
	struct mem_ctl_info *mci;
	struct mv64x60_mc_pdata *pdata;
	struct resource *r;
	u32 ctl;
	int res = 0;

	if (!devres_open_group(&pdev->dev, mv64x60_mc_err_probe, GFP_KERNEL))
		return -ENOMEM;

	mci = edac_mc_alloc(sizeof(struct mv64x60_mc_pdata), 1, 1, edac_mc_idx);
	if (!mci) {
		printk(KERN_ERR "%s: No memory for CPU err\n", __func__);
		devres_release_group(&pdev->dev, mv64x60_mc_err_probe);
		return -ENOMEM;
	}

	pdata = mci->pvt_info;
	mci->dev = &pdev->dev;
	platform_set_drvdata(pdev, mci);
	pdata->name = "mv64x60_mc_err";
	pdata->irq = NO_IRQ;
	mci->dev_name = dev_name(&pdev->dev);
	pdata->edac_idx = edac_mc_idx++;

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!r) {
		printk(KERN_ERR "%s: Unable to get resource for "
		       "MC err regs\n", __func__);
		res = -ENOENT;
		goto err;
	}

	if (!devm_request_mem_region(&pdev->dev,
				     r->start,
				     r->end - r->start + 1,
				     pdata->name)) {
		printk(KERN_ERR "%s: Error while requesting mem region\n",
		       __func__);
		res = -EBUSY;
		goto err;
	}

	pdata->mc_vbase = devm_ioremap(&pdev->dev,
				       r->start,
				       r->end - r->start + 1);
	if (!pdata->mc_vbase) {
		printk(KERN_ERR "%s: Unable to setup MC err regs\n", __func__);
		res = -ENOMEM;
		goto err;
	}

	ctl = in_le32(pdata->mc_vbase + MV64X60_SDRAM_CONFIG);
	if (!(ctl & MV64X60_SDRAM_ECC)) {
		/* Non-ECC RAM? */
		printk(KERN_WARNING "%s: No ECC DIMMs discovered\n", __func__);
		res = -ENODEV;
		goto err2;
	}

	debugf3("%s(): init mci\n", __func__);
	mci->mtype_cap = MEM_FLAG_RDDR | MEM_FLAG_DDR;
	mci->edac_ctl_cap = EDAC_FLAG_NONE | EDAC_FLAG_SECDED;
	mci->edac_cap = EDAC_FLAG_SECDED;
	mci->mod_name = EDAC_MOD_STR;
	mci->mod_ver = MV64x60_REVISION;
	mci->ctl_name = mv64x60_ctl_name;

	if (edac_op_state == EDAC_OPSTATE_POLL)
		mci->edac_check = mv64x60_mc_check;

	mci->ctl_page_to_phys = NULL;

	mci->scrub_mode = SCRUB_SW_SRC;

	mv64x60_init_csrows(mci, pdata);

	/* setup MC registers */
	out_le32(pdata->mc_vbase + MV64X60_SDRAM_ERR_ADDR, 0);
	ctl = in_le32(pdata->mc_vbase + MV64X60_SDRAM_ERR_ECC_CNTL);
	ctl = (ctl & 0xff00ffff) | 0x10000;
	out_le32(pdata->mc_vbase + MV64X60_SDRAM_ERR_ECC_CNTL, ctl);

	if (edac_mc_add_mc(mci)) {
		debugf3("%s(): failed edac_mc_add_mc()\n", __func__);
		goto err;
	}

	if (edac_op_state == EDAC_OPSTATE_INT) {
		/* acquire interrupt that reports errors */
		pdata->irq = platform_get_irq(pdev, 0);
		res = devm_request_irq(&pdev->dev,
				       pdata->irq,
				       mv64x60_mc_isr,
				       IRQF_DISABLED,
				       "[EDAC] MC err",
				       mci);
		if (res < 0) {
			printk(KERN_ERR "%s: Unable to request irq %d for "
			       "MV64x60 DRAM ERR\n", __func__, pdata->irq);
			res = -ENODEV;
			goto err2;
		}

		printk(KERN_INFO EDAC_MOD_STR " acquired irq %d for MC Err\n",
		       pdata->irq);
	}

	/* get this far and it's successful */
	debugf3("%s(): success\n", __func__);

	return 0;

err2:
	edac_mc_del_mc(&pdev->dev);
err:
	devres_release_group(&pdev->dev, mv64x60_mc_err_probe);
	edac_mc_free(mci);
	return res;
}

static int mv64x60_mc_err_remove(struct platform_device *pdev)
{
	struct mem_ctl_info *mci = platform_get_drvdata(pdev);

	debugf0("%s()\n", __func__);

	edac_mc_del_mc(&pdev->dev);
	edac_mc_free(mci);
	return 0;
}

static struct platform_driver mv64x60_mc_err_driver = {
	.probe = mv64x60_mc_err_probe,
	.remove = mv64x60_mc_err_remove,
	.driver = {
		   .name = "mv64x60_mc_err",
	}
};

static int __init mv64x60_edac_init(void)
{
	int ret = 0;

	printk(KERN_INFO "Marvell MV64x60 EDAC driver " MV64x60_REVISION "\n");
	printk(KERN_INFO "\t(C) 2006-2007 MontaVista Software\n");
	/* make sure error reporting method is sane */
	switch (edac_op_state) {
	case EDAC_OPSTATE_POLL:
	case EDAC_OPSTATE_INT:
		break;
	default:
		edac_op_state = EDAC_OPSTATE_INT;
		break;
	}

	ret = platform_driver_register(&mv64x60_mc_err_driver);
	if (ret)
		printk(KERN_WARNING EDAC_MOD_STR "MC err failed to register\n");

	ret = platform_driver_register(&mv64x60_cpu_err_driver);
	if (ret)
		printk(KERN_WARNING EDAC_MOD_STR
			"CPU err failed to register\n");

	ret = platform_driver_register(&mv64x60_sram_err_driver);
	if (ret)
		printk(KERN_WARNING EDAC_MOD_STR
			"SRAM err failed to register\n");

#ifdef CONFIG_PCI
	ret = platform_driver_register(&mv64x60_pci_err_driver);
	if (ret)
		printk(KERN_WARNING EDAC_MOD_STR
			"PCI err failed to register\n");
#endif

	return ret;
}
module_init(mv64x60_edac_init);

static void __exit mv64x60_edac_exit(void)
{
#ifdef CONFIG_PCI
	platform_driver_unregister(&mv64x60_pci_err_driver);
#endif
	platform_driver_unregister(&mv64x60_sram_err_driver);
	platform_driver_unregister(&mv64x60_cpu_err_driver);
	platform_driver_unregister(&mv64x60_mc_err_driver);
}
module_exit(mv64x60_edac_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Montavista Software, Inc.");
module_param(edac_op_state, int, 0444);
MODULE_PARM_DESC(edac_op_state,
		 "EDAC Error Reporting state: 0=Poll, 2=Interrupt");
