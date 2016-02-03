/*
 * Copyright 2011 Freescale Semiconductor, Inc
 *
 * Freescale Integrated Flash Controller
 *
 * Author: Dipen Dudhat <Dipen.Dudhat@freescale.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/compiler.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/fsl_ifc.h>
#include <asm/prom.h>

struct fsl_ifc_ctrl *fsl_ifc_ctrl_dev;
EXPORT_SYMBOL(fsl_ifc_ctrl_dev);

/*
 * convert_ifc_address - convert the base address
 * @addr_base:	base address of the memory bank
 */
unsigned int convert_ifc_address(phys_addr_t addr_base)
{
	return addr_base & CSPR_BA;
}
EXPORT_SYMBOL(convert_ifc_address);

/*
 * fsl_ifc_find - find IFC bank
 * @addr_base:	base address of the memory bank
 *
 * This function walks IFC banks comparing "Base address" field of the CSPR
 * registers with the supplied addr_base argument. When bases match this
 * function returns bank number (starting with 0), otherwise it returns
 * appropriate errno value.
 */
int fsl_ifc_find(phys_addr_t addr_base)
{
	int i = 0;

	if (!fsl_ifc_ctrl_dev || !fsl_ifc_ctrl_dev->regs)
		return -ENODEV;

	for (i = 0; i < fsl_ifc_ctrl_dev->banks; i++) {
		u32 cspr = ifc_in32(&fsl_ifc_ctrl_dev->regs->cspr_cs[i].cspr);
		if (cspr & CSPR_V && (cspr & CSPR_BA) ==
				convert_ifc_address(addr_base))
			return i;
	}

	return -ENOENT;
}
EXPORT_SYMBOL(fsl_ifc_find);

static int fsl_ifc_ctrl_init(struct fsl_ifc_ctrl *ctrl)
{
	struct fsl_ifc_regs __iomem *ifc = ctrl->regs;

	/*
	 * Clear all the common status and event registers
	 */
	if (ifc_in32(&ifc->cm_evter_stat) & IFC_CM_EVTER_STAT_CSER)
		ifc_out32(IFC_CM_EVTER_STAT_CSER, &ifc->cm_evter_stat);

	/* enable all error and events */
	ifc_out32(IFC_CM_EVTER_EN_CSEREN, &ifc->cm_evter_en);

	/* enable all error and event interrupts */
	ifc_out32(IFC_CM_EVTER_INTR_EN_CSERIREN, &ifc->cm_evter_intr_en);
	ifc_out32(0x0, &ifc->cm_erattr0);
	ifc_out32(0x0, &ifc->cm_erattr1);

	return 0;
}

static int fsl_ifc_ctrl_remove(struct platform_device *dev)
{
	struct fsl_ifc_ctrl *ctrl = dev_get_drvdata(&dev->dev);

	free_irq(ctrl->nand_irq, ctrl);
	free_irq(ctrl->irq, ctrl);

	irq_dispose_mapping(ctrl->nand_irq);
	irq_dispose_mapping(ctrl->irq);

	iounmap(ctrl->regs);

	dev_set_drvdata(&dev->dev, NULL);
	kfree(ctrl);

	return 0;
}

/*
 * NAND events are split between an operational interrupt which only
 * receives OPC, and an error interrupt that receives everything else,
 * including non-NAND errors.  Whichever interrupt gets to it first
 * records the status and wakes the wait queue.
 */
static DEFINE_SPINLOCK(nand_irq_lock);

static u32 check_nand_stat(struct fsl_ifc_ctrl *ctrl)
{
	struct fsl_ifc_regs __iomem *ifc = ctrl->regs;
	unsigned long flags;
	u32 stat;

	spin_lock_irqsave(&nand_irq_lock, flags);

	stat = ifc_in32(&ifc->ifc_nand.nand_evter_stat);
	if (stat) {
		ifc_out32(stat, &ifc->ifc_nand.nand_evter_stat);
		ctrl->nand_stat = stat;
		wake_up(&ctrl->nand_wait);
	}

	spin_unlock_irqrestore(&nand_irq_lock, flags);

	return stat;
}

static irqreturn_t fsl_ifc_nand_irq(int irqno, void *data)
{
	struct fsl_ifc_ctrl *ctrl = data;

	if (check_nand_stat(ctrl))
		return IRQ_HANDLED;

	return IRQ_NONE;
}

/*
 * NOTE: This interrupt is used to report ifc events of various kinds,
 * such as transaction errors on the chipselects.
 */
static irqreturn_t fsl_ifc_ctrl_irq(int irqno, void *data)
{
	struct fsl_ifc_ctrl *ctrl = data;
	struct fsl_ifc_regs __iomem *ifc = ctrl->regs;
	u32 err_axiid, err_srcid, status, cs_err, err_addr;
	irqreturn_t ret = IRQ_NONE;

	/* read for chip select error */
	cs_err = ifc_in32(&ifc->cm_evter_stat);
	if (cs_err) {
		dev_err(ctrl->dev, "transaction sent to IFC is not mapped to"
				"any memory bank 0x%08X\n", cs_err);
		/* clear the chip select error */
		ifc_out32(IFC_CM_EVTER_STAT_CSER, &ifc->cm_evter_stat);

		/* read error attribute registers print the error information */
		status = ifc_in32(&ifc->cm_erattr0);
		err_addr = ifc_in32(&ifc->cm_erattr1);

		if (status & IFC_CM_ERATTR0_ERTYP_READ)
			dev_err(ctrl->dev, "Read transaction error"
				"CM_ERATTR0 0x%08X\n", status);
		else
			dev_err(ctrl->dev, "Write transaction error"
				"CM_ERATTR0 0x%08X\n", status);

		err_axiid = (status & IFC_CM_ERATTR0_ERAID) >>
					IFC_CM_ERATTR0_ERAID_SHIFT;
		dev_err(ctrl->dev, "AXI ID of the error"
					"transaction 0x%08X\n", err_axiid);

		err_srcid = (status & IFC_CM_ERATTR0_ESRCID) >>
					IFC_CM_ERATTR0_ESRCID_SHIFT;
		dev_err(ctrl->dev, "SRC ID of the error"
					"transaction 0x%08X\n", err_srcid);

		dev_err(ctrl->dev, "Transaction Address corresponding to error"
					"ERADDR 0x%08X\n", err_addr);

		ret = IRQ_HANDLED;
	}

	if (check_nand_stat(ctrl))
		ret = IRQ_HANDLED;

	return ret;
}

/*
 * fsl_ifc_ctrl_probe
 *
 * called by device layer when it finds a device matching
 * one our driver can handled. This code allocates all of
 * the resources needed for the controller only.  The
 * resources for the NAND banks themselves are allocated
 * in the chip probe function.
*/
static int fsl_ifc_ctrl_probe(struct platform_device *dev)
{
	int ret = 0;
	int version, banks;

	dev_info(&dev->dev, "Freescale Integrated Flash Controller\n");

	fsl_ifc_ctrl_dev = kzalloc(sizeof(*fsl_ifc_ctrl_dev), GFP_KERNEL);
	if (!fsl_ifc_ctrl_dev)
		return -ENOMEM;

	dev_set_drvdata(&dev->dev, fsl_ifc_ctrl_dev);

	/* IOMAP the entire IFC region */
	fsl_ifc_ctrl_dev->regs = of_iomap(dev->dev.of_node, 0);
	if (!fsl_ifc_ctrl_dev->regs) {
		dev_err(&dev->dev, "failed to get memory region\n");
		ret = -ENODEV;
		goto err;
	}

	version = ifc_in32(&fsl_ifc_ctrl_dev->regs->ifc_rev) &
			FSL_IFC_VERSION_MASK;
	banks = (version == FSL_IFC_VERSION_1_0_0) ? 4 : 8;
	dev_info(&dev->dev, "IFC version %d.%d, %d banks\n",
		version >> 24, (version >> 16) & 0xf, banks);

	fsl_ifc_ctrl_dev->version = version;
	fsl_ifc_ctrl_dev->banks = banks;

	if (of_property_read_bool(dev->dev.of_node, "little-endian")) {
		fsl_ifc_ctrl_dev->little_endian = true;
		dev_dbg(&dev->dev, "IFC REGISTERS are LITTLE endian\n");
	} else {
		fsl_ifc_ctrl_dev->little_endian = false;
		dev_dbg(&dev->dev, "IFC REGISTERS are BIG endian\n");
	}

	version = ioread32be(&fsl_ifc_ctrl_dev->regs->ifc_rev) &
			FSL_IFC_VERSION_MASK;
	banks = (version == FSL_IFC_VERSION_1_0_0) ? 4 : 8;
	dev_info(&dev->dev, "IFC version %d.%d, %d banks\n",
		version >> 24, (version >> 16) & 0xf, banks);

	fsl_ifc_ctrl_dev->version = version;
	fsl_ifc_ctrl_dev->banks = banks;

	/* get the Controller level irq */
	fsl_ifc_ctrl_dev->irq = irq_of_parse_and_map(dev->dev.of_node, 0);
	if (fsl_ifc_ctrl_dev->irq == NO_IRQ) {
		dev_err(&dev->dev, "failed to get irq resource "
							"for IFC\n");
		ret = -ENODEV;
		goto err;
	}

	/* get the nand machine irq */
	fsl_ifc_ctrl_dev->nand_irq =
			irq_of_parse_and_map(dev->dev.of_node, 1);

	fsl_ifc_ctrl_dev->dev = &dev->dev;

	ret = fsl_ifc_ctrl_init(fsl_ifc_ctrl_dev);
	if (ret < 0)
		goto err;

	init_waitqueue_head(&fsl_ifc_ctrl_dev->nand_wait);

	ret = request_irq(fsl_ifc_ctrl_dev->irq, fsl_ifc_ctrl_irq, IRQF_SHARED,
			  "fsl-ifc", fsl_ifc_ctrl_dev);
	if (ret != 0) {
		dev_err(&dev->dev, "failed to install irq (%d)\n",
			fsl_ifc_ctrl_dev->irq);
		goto err_irq;
	}

	if (fsl_ifc_ctrl_dev->nand_irq) {
		ret = request_irq(fsl_ifc_ctrl_dev->nand_irq, fsl_ifc_nand_irq,
				0, "fsl-ifc-nand", fsl_ifc_ctrl_dev);
		if (ret != 0) {
			dev_err(&dev->dev, "failed to install irq (%d)\n",
				fsl_ifc_ctrl_dev->nand_irq);
			goto err_nandirq;
		}
	}

	return 0;

err_nandirq:
	free_irq(fsl_ifc_ctrl_dev->nand_irq, fsl_ifc_ctrl_dev);
	irq_dispose_mapping(fsl_ifc_ctrl_dev->nand_irq);
err_irq:
	free_irq(fsl_ifc_ctrl_dev->irq, fsl_ifc_ctrl_dev);
	irq_dispose_mapping(fsl_ifc_ctrl_dev->irq);
err:
	return ret;
}

static const struct of_device_id fsl_ifc_match[] = {
	{
		.compatible = "fsl,ifc",
	},
	{},
};

static struct platform_driver fsl_ifc_ctrl_driver = {
	.driver = {
		.name	= "fsl-ifc",
		.of_match_table = fsl_ifc_match,
	},
	.probe       = fsl_ifc_ctrl_probe,
	.remove      = fsl_ifc_ctrl_remove,
};

static int __init fsl_ifc_init(void)
{
	return platform_driver_register(&fsl_ifc_ctrl_driver);
}
subsys_initcall(fsl_ifc_init);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Freescale Semiconductor");
MODULE_DESCRIPTION("Freescale Integrated Flash Controller driver");
