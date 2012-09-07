/*
 * Copyright 2011-2012 Calxeda, Inc.
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
 */
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/ctype.h>
#include <linux/edac.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/of_platform.h>
#include <linux/uaccess.h>

#include "edac_core.h"
#include "edac_module.h"

/* DDR Ctrlr Error Registers */
#define HB_DDR_ECC_OPT			0x128
#define HB_DDR_ECC_U_ERR_ADDR		0x130
#define HB_DDR_ECC_U_ERR_STAT		0x134
#define HB_DDR_ECC_U_ERR_DATAL		0x138
#define HB_DDR_ECC_U_ERR_DATAH		0x13c
#define HB_DDR_ECC_C_ERR_ADDR		0x140
#define HB_DDR_ECC_C_ERR_STAT		0x144
#define HB_DDR_ECC_C_ERR_DATAL		0x148
#define HB_DDR_ECC_C_ERR_DATAH		0x14c
#define HB_DDR_ECC_INT_STATUS		0x180
#define HB_DDR_ECC_INT_ACK		0x184
#define HB_DDR_ECC_U_ERR_ID		0x424
#define HB_DDR_ECC_C_ERR_ID		0x428

#define HB_DDR_ECC_INT_STAT_CE		0x8
#define HB_DDR_ECC_INT_STAT_DOUBLE_CE	0x10
#define HB_DDR_ECC_INT_STAT_UE		0x20
#define HB_DDR_ECC_INT_STAT_DOUBLE_UE	0x40

#define HB_DDR_ECC_OPT_MODE_MASK	0x3
#define HB_DDR_ECC_OPT_FWC		0x100
#define HB_DDR_ECC_OPT_XOR_SHIFT	16

struct hb_mc_drvdata {
	void __iomem *mc_vbase;
};

static irqreturn_t highbank_mc_err_handler(int irq, void *dev_id)
{
	struct mem_ctl_info *mci = dev_id;
	struct hb_mc_drvdata *drvdata = mci->pvt_info;
	u32 status, err_addr;

	/* Read the interrupt status register */
	status = readl(drvdata->mc_vbase + HB_DDR_ECC_INT_STATUS);

	if (status & HB_DDR_ECC_INT_STAT_UE) {
		err_addr = readl(drvdata->mc_vbase + HB_DDR_ECC_U_ERR_ADDR);
		edac_mc_handle_error(HW_EVENT_ERR_UNCORRECTED, mci, 1,
				     err_addr >> PAGE_SHIFT,
				     err_addr & ~PAGE_MASK, 0,
				     0, 0, -1,
				     mci->ctl_name, "");
	}
	if (status & HB_DDR_ECC_INT_STAT_CE) {
		u32 syndrome = readl(drvdata->mc_vbase + HB_DDR_ECC_C_ERR_STAT);
		syndrome = (syndrome >> 8) & 0xff;
		err_addr = readl(drvdata->mc_vbase + HB_DDR_ECC_C_ERR_ADDR);
		edac_mc_handle_error(HW_EVENT_ERR_CORRECTED, mci, 1,
				     err_addr >> PAGE_SHIFT,
				     err_addr & ~PAGE_MASK, syndrome,
				     0, 0, -1,
				     mci->ctl_name, "");
	}

	/* clear the error, clears the interrupt */
	writel(status, drvdata->mc_vbase + HB_DDR_ECC_INT_ACK);
	return IRQ_HANDLED;
}

#ifdef CONFIG_EDAC_DEBUG
static ssize_t highbank_mc_err_inject_write(struct file *file,
				      const char __user *data,
				      size_t count, loff_t *ppos)
{
	struct mem_ctl_info *mci = file->private_data;
	struct hb_mc_drvdata *pdata = mci->pvt_info;
	char buf[32];
	size_t buf_size;
	u32 reg;
	u8 synd;

	buf_size = min(count, (sizeof(buf)-1));
	if (copy_from_user(buf, data, buf_size))
		return -EFAULT;
	buf[buf_size] = 0;

	if (!kstrtou8(buf, 16, &synd)) {
		reg = readl(pdata->mc_vbase + HB_DDR_ECC_OPT);
		reg &= HB_DDR_ECC_OPT_MODE_MASK;
		reg |= (synd << HB_DDR_ECC_OPT_XOR_SHIFT) | HB_DDR_ECC_OPT_FWC;
		writel(reg, pdata->mc_vbase + HB_DDR_ECC_OPT);
	}

	return count;
}

static int debugfs_open(struct inode *inode, struct file *file)
{
	file->private_data = inode->i_private;
	return 0;
}

static const struct file_operations highbank_mc_debug_inject_fops = {
	.open = debugfs_open,
	.write = highbank_mc_err_inject_write,
	.llseek = generic_file_llseek,
};

static void __devinit highbank_mc_create_debugfs_nodes(struct mem_ctl_info *mci)
{
	if (mci->debugfs)
		debugfs_create_file("inject_ctrl", S_IWUSR, mci->debugfs, mci,
				    &highbank_mc_debug_inject_fops);
;
}
#else
static void __devinit highbank_mc_create_debugfs_nodes(struct mem_ctl_info *mci)
{}
#endif

static int __devinit highbank_mc_probe(struct platform_device *pdev)
{
	struct edac_mc_layer layers[2];
	struct mem_ctl_info *mci;
	struct hb_mc_drvdata *drvdata;
	struct dimm_info *dimm;
	struct resource *r;
	u32 control;
	int irq;
	int res = 0;

	layers[0].type = EDAC_MC_LAYER_CHIP_SELECT;
	layers[0].size = 1;
	layers[0].is_virt_csrow = true;
	layers[1].type = EDAC_MC_LAYER_CHANNEL;
	layers[1].size = 1;
	layers[1].is_virt_csrow = false;
	mci = edac_mc_alloc(0, ARRAY_SIZE(layers), layers,
			    sizeof(struct hb_mc_drvdata));
	if (!mci)
		return -ENOMEM;

	mci->pdev = &pdev->dev;
	drvdata = mci->pvt_info;
	platform_set_drvdata(pdev, mci);

	if (!devres_open_group(&pdev->dev, NULL, GFP_KERNEL))
		return -ENOMEM;

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!r) {
		dev_err(&pdev->dev, "Unable to get mem resource\n");
		res = -ENODEV;
		goto err;
	}

	if (!devm_request_mem_region(&pdev->dev, r->start,
				     resource_size(r), dev_name(&pdev->dev))) {
		dev_err(&pdev->dev, "Error while requesting mem region\n");
		res = -EBUSY;
		goto err;
	}

	drvdata->mc_vbase = devm_ioremap(&pdev->dev,
					  r->start, resource_size(r));
	if (!drvdata->mc_vbase) {
		dev_err(&pdev->dev, "Unable to map regs\n");
		res = -ENOMEM;
		goto err;
	}

	control = readl(drvdata->mc_vbase + HB_DDR_ECC_OPT) & 0x3;
	if (!control || (control == 0x2)) {
		dev_err(&pdev->dev, "No ECC present, or ECC disabled\n");
		res = -ENODEV;
		goto err;
	}

	irq = platform_get_irq(pdev, 0);
	res = devm_request_irq(&pdev->dev, irq, highbank_mc_err_handler,
			       0, dev_name(&pdev->dev), mci);
	if (res < 0) {
		dev_err(&pdev->dev, "Unable to request irq %d\n", irq);
		goto err;
	}

	mci->mtype_cap = MEM_FLAG_DDR3;
	mci->edac_ctl_cap = EDAC_FLAG_NONE | EDAC_FLAG_SECDED;
	mci->edac_cap = EDAC_FLAG_SECDED;
	mci->mod_name = dev_name(&pdev->dev);
	mci->mod_ver = "1";
	mci->ctl_name = dev_name(&pdev->dev);
	mci->scrub_mode = SCRUB_SW_SRC;

	/* Only a single 4GB DIMM is supported */
	dimm = *mci->dimms;
	dimm->nr_pages = (~0UL >> PAGE_SHIFT) + 1;
	dimm->grain = 8;
	dimm->dtype = DEV_X8;
	dimm->mtype = MEM_DDR3;
	dimm->edac_mode = EDAC_SECDED;

	res = edac_mc_add_mc(mci);
	if (res < 0)
		goto err;

	highbank_mc_create_debugfs_nodes(mci);

	devres_close_group(&pdev->dev, NULL);
	return 0;
err:
	devres_release_group(&pdev->dev, NULL);
	edac_mc_free(mci);
	return res;
}

static int highbank_mc_remove(struct platform_device *pdev)
{
	struct mem_ctl_info *mci = platform_get_drvdata(pdev);

	edac_mc_del_mc(&pdev->dev);
	edac_mc_free(mci);
	return 0;
}

static const struct of_device_id hb_ddr_ctrl_of_match[] = {
	{ .compatible = "calxeda,hb-ddr-ctrl", },
	{},
};
MODULE_DEVICE_TABLE(of, hb_ddr_ctrl_of_match);

static struct platform_driver highbank_mc_edac_driver = {
	.probe = highbank_mc_probe,
	.remove = highbank_mc_remove,
	.driver = {
		.name = "hb_mc_edac",
		.of_match_table = hb_ddr_ctrl_of_match,
	},
};

module_platform_driver(highbank_mc_edac_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Calxeda, Inc.");
MODULE_DESCRIPTION("EDAC Driver for Calxeda Highbank");
