// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx ZynqMP OCM ECC Driver
 *
 * Copyright (C) 2022 Advanced Micro Devices, Inc.
 */

#include <linux/edac.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>

#include "edac_module.h"

#define ZYNQMP_OCM_EDAC_MSG_SIZE	256

#define ZYNQMP_OCM_EDAC_STRING	"zynqmp_ocm"

/* Error/Interrupt registers */
#define ERR_CTRL_OFST		0x0
#define OCM_ISR_OFST		0x04
#define OCM_IMR_OFST		0x08
#define OCM_IEN_OFST		0x0C
#define OCM_IDS_OFST		0x10

/* ECC control register */
#define ECC_CTRL_OFST		0x14

/* Correctable error info registers */
#define CE_FFA_OFST		0x1C
#define CE_FFD0_OFST		0x20
#define CE_FFD1_OFST		0x24
#define CE_FFD2_OFST		0x28
#define CE_FFD3_OFST		0x2C
#define CE_FFE_OFST		0x30

/* Uncorrectable error info registers */
#define UE_FFA_OFST		0x34
#define UE_FFD0_OFST		0x38
#define UE_FFD1_OFST		0x3C
#define UE_FFD2_OFST		0x40
#define UE_FFD3_OFST		0x44
#define UE_FFE_OFST		0x48

/* ECC control register bit field definitions */
#define ECC_CTRL_CLR_CE_ERR	0x40
#define ECC_CTRL_CLR_UE_ERR	0x80

/* Fault injection data and count registers */
#define OCM_FID0_OFST		0x4C
#define OCM_FID1_OFST		0x50
#define OCM_FID2_OFST		0x54
#define OCM_FID3_OFST		0x58
#define OCM_FIC_OFST		0x74

#define UE_MAX_BITPOS_LOWER	31
#define UE_MIN_BITPOS_UPPER	32
#define UE_MAX_BITPOS_UPPER	63

/* Interrupt masks */
#define OCM_CEINTR_MASK		BIT(6)
#define OCM_UEINTR_MASK		BIT(7)
#define OCM_ECC_ENABLE_MASK	BIT(0)

#define OCM_FICOUNT_MASK	GENMASK(23, 0)
#define OCM_NUM_UE_BITPOS	2
#define OCM_BASEVAL		0xFFFC0000
#define EDAC_DEVICE		"ZynqMP-OCM"

/**
 * struct ecc_error_info - ECC error log information
 * @addr:	Fault generated at this address
 * @fault_lo:	Generated fault data (lower 32-bit)
 * @fault_hi:	Generated fault data (upper 32-bit)
 */
struct ecc_error_info {
	u32 addr;
	u32 fault_lo;
	u32 fault_hi;
};

/**
 * struct ecc_status - ECC status information to report
 * @ce_cnt:	Correctable error count
 * @ue_cnt:	Uncorrectable error count
 * @ceinfo:	Correctable error log information
 * @ueinfo:	Uncorrectable error log information
 */
struct ecc_status {
	u32 ce_cnt;
	u32 ue_cnt;
	struct ecc_error_info ceinfo;
	struct ecc_error_info ueinfo;
};

/**
 * struct edac_priv - OCM private instance data
 * @baseaddr:	Base address of the OCM
 * @message:	Buffer for framing the event specific info
 * @stat:	ECC status information
 * @ce_cnt:	Correctable Error count
 * @ue_cnt:	Uncorrectable Error count
 * @debugfs_dir:	Directory entry for debugfs
 * @ce_bitpos:	Bit position for Correctable Error
 * @ue_bitpos:	Array to store UnCorrectable Error bit positions
 * @fault_injection_cnt: Fault Injection Counter value
 */
struct edac_priv {
	void __iomem *baseaddr;
	char message[ZYNQMP_OCM_EDAC_MSG_SIZE];
	struct ecc_status stat;
	u32 ce_cnt;
	u32 ue_cnt;
#ifdef CONFIG_EDAC_DEBUG
	struct dentry *debugfs_dir;
	u8 ce_bitpos;
	u8 ue_bitpos[OCM_NUM_UE_BITPOS];
	u32 fault_injection_cnt;
#endif
};

/**
 * get_error_info - Get the current ECC error info
 * @base:	Pointer to the base address of the OCM
 * @p:		Pointer to the OCM ECC status structure
 * @mask:	Status register mask value
 *
 * Determines there is any ECC error or not
 *
 */
static void get_error_info(void __iomem *base, struct ecc_status *p, int mask)
{
	if (mask & OCM_CEINTR_MASK) {
		p->ce_cnt++;
		p->ceinfo.fault_lo = readl(base + CE_FFD0_OFST);
		p->ceinfo.fault_hi = readl(base + CE_FFD1_OFST);
		p->ceinfo.addr = (OCM_BASEVAL | readl(base + CE_FFA_OFST));
		writel(ECC_CTRL_CLR_CE_ERR, base + OCM_ISR_OFST);
	} else if (mask & OCM_UEINTR_MASK) {
		p->ue_cnt++;
		p->ueinfo.fault_lo = readl(base + UE_FFD0_OFST);
		p->ueinfo.fault_hi = readl(base + UE_FFD1_OFST);
		p->ueinfo.addr = (OCM_BASEVAL | readl(base + UE_FFA_OFST));
		writel(ECC_CTRL_CLR_UE_ERR, base + OCM_ISR_OFST);
	}
}

/**
 * handle_error - Handle error types CE and UE
 * @dci:	Pointer to the EDAC device instance
 * @p:		Pointer to the OCM ECC status structure
 *
 * Handles correctable and uncorrectable errors.
 */
static void handle_error(struct edac_device_ctl_info *dci, struct ecc_status *p)
{
	struct edac_priv *priv = dci->pvt_info;
	struct ecc_error_info *pinf;

	if (p->ce_cnt) {
		pinf = &p->ceinfo;
		snprintf(priv->message, ZYNQMP_OCM_EDAC_MSG_SIZE,
			 "\nOCM ECC error type :%s\nAddr: [0x%x]\nFault Data[0x%08x%08x]",
			 "CE", pinf->addr, pinf->fault_hi, pinf->fault_lo);
		edac_device_handle_ce(dci, 0, 0, priv->message);
	}

	if (p->ue_cnt) {
		pinf = &p->ueinfo;
		snprintf(priv->message, ZYNQMP_OCM_EDAC_MSG_SIZE,
			 "\nOCM ECC error type :%s\nAddr: [0x%x]\nFault Data[0x%08x%08x]",
			 "UE", pinf->addr, pinf->fault_hi, pinf->fault_lo);
		edac_device_handle_ue(dci, 0, 0, priv->message);
	}

	memset(p, 0, sizeof(*p));
}

/**
 * intr_handler - ISR routine
 * @irq:        irq number
 * @dev_id:     device id pointer
 *
 * Return: IRQ_NONE, if CE/UE interrupt not set or IRQ_HANDLED otherwise
 */
static irqreturn_t intr_handler(int irq, void *dev_id)
{
	struct edac_device_ctl_info *dci = dev_id;
	struct edac_priv *priv = dci->pvt_info;
	int regval;

	regval = readl(priv->baseaddr + OCM_ISR_OFST);
	if (!(regval & (OCM_CEINTR_MASK | OCM_UEINTR_MASK))) {
		WARN_ONCE(1, "Unhandled IRQ%d, ISR: 0x%x", irq, regval);
		return IRQ_NONE;
	}

	get_error_info(priv->baseaddr, &priv->stat, regval);

	priv->ce_cnt += priv->stat.ce_cnt;
	priv->ue_cnt += priv->stat.ue_cnt;
	handle_error(dci, &priv->stat);

	return IRQ_HANDLED;
}

/**
 * get_eccstate - Return the ECC status
 * @base:	Pointer to the OCM base address
 *
 * Get the ECC enable/disable status
 *
 * Return: ECC status 0/1.
 */
static bool get_eccstate(void __iomem *base)
{
	return readl(base + ECC_CTRL_OFST) & OCM_ECC_ENABLE_MASK;
}

#ifdef CONFIG_EDAC_DEBUG
/**
 * write_fault_count - write fault injection count
 * @priv:	Pointer to the EDAC private struct
 *
 * Update the fault injection count register, once the counter reaches
 * zero, it injects errors
 */
static void write_fault_count(struct edac_priv *priv)
{
	u32 ficount = priv->fault_injection_cnt;

	if (ficount & ~OCM_FICOUNT_MASK) {
		ficount &= OCM_FICOUNT_MASK;
		edac_printk(KERN_INFO, EDAC_DEVICE,
			    "Fault injection count value truncated to %d\n", ficount);
	}

	writel(ficount, priv->baseaddr + OCM_FIC_OFST);
}

/*
 * To get the Correctable Error injected, the following steps are needed:
 * - Setup the optional Fault Injection Count:
 *	echo <fault_count val> > /sys/kernel/debug/edac/ocm/inject_fault_count
 * - Write the Correctable Error bit position value:
 *	echo <bit_pos val> > /sys/kernel/debug/edac/ocm/inject_ce_bitpos
 */
static ssize_t inject_ce_write(struct file *file, const char __user *data,
			       size_t count, loff_t *ppos)
{
	struct edac_device_ctl_info *edac_dev = file->private_data;
	struct edac_priv *priv = edac_dev->pvt_info;
	int ret;

	if (!data)
		return -EFAULT;

	ret = kstrtou8_from_user(data, count, 0, &priv->ce_bitpos);
	if (ret)
		return ret;

	if (priv->ce_bitpos > UE_MAX_BITPOS_UPPER)
		return -EINVAL;

	if (priv->ce_bitpos <= UE_MAX_BITPOS_LOWER) {
		writel(BIT(priv->ce_bitpos), priv->baseaddr + OCM_FID0_OFST);
		writel(0, priv->baseaddr + OCM_FID1_OFST);
	} else {
		writel(BIT(priv->ce_bitpos - UE_MIN_BITPOS_UPPER),
		       priv->baseaddr + OCM_FID1_OFST);
		writel(0, priv->baseaddr + OCM_FID0_OFST);
	}

	write_fault_count(priv);

	return count;
}

static const struct file_operations inject_ce_fops = {
	.open = simple_open,
	.write = inject_ce_write,
	.llseek = generic_file_llseek,
};

/*
 * To get the Uncorrectable Error injected, the following steps are needed:
 * - Setup the optional Fault Injection Count:
 *      echo <fault_count val> > /sys/kernel/debug/edac/ocm/inject_fault_count
 * - Write the Uncorrectable Error bit position values:
 *      echo <bit_pos0 val>,<bit_pos1 val> > /sys/kernel/debug/edac/ocm/inject_ue_bitpos
 */
static ssize_t inject_ue_write(struct file *file, const char __user *data,
			       size_t count, loff_t *ppos)
{
	struct edac_device_ctl_info *edac_dev = file->private_data;
	struct edac_priv *priv = edac_dev->pvt_info;
	char buf[6], *pbuf, *token[2];
	u64 ue_bitpos;
	int i, ret;
	u8 len;

	if (!data)
		return -EFAULT;

	len = min_t(size_t, count, sizeof(buf));
	if (copy_from_user(buf, data, len))
		return -EFAULT;

	buf[len] = '\0';
	pbuf = &buf[0];
	for (i = 0; i < OCM_NUM_UE_BITPOS; i++)
		token[i] = strsep(&pbuf, ",");

	ret = kstrtou8(token[0], 0, &priv->ue_bitpos[0]);
	if (ret)
		return ret;

	ret = kstrtou8(token[1], 0, &priv->ue_bitpos[1]);
	if (ret)
		return ret;

	if (priv->ue_bitpos[0] > UE_MAX_BITPOS_UPPER ||
	    priv->ue_bitpos[1] > UE_MAX_BITPOS_UPPER)
		return -EINVAL;

	if (priv->ue_bitpos[0] == priv->ue_bitpos[1]) {
		edac_printk(KERN_ERR, EDAC_DEVICE, "Bit positions should not be equal\n");
		return -EINVAL;
	}

	ue_bitpos = BIT(priv->ue_bitpos[0]) | BIT(priv->ue_bitpos[1]);

	writel((u32)ue_bitpos, priv->baseaddr + OCM_FID0_OFST);
	writel((u32)(ue_bitpos >> 32), priv->baseaddr + OCM_FID1_OFST);

	write_fault_count(priv);

	return count;
}

static const struct file_operations inject_ue_fops = {
	.open = simple_open,
	.write = inject_ue_write,
	.llseek = generic_file_llseek,
};

static void setup_debugfs(struct edac_device_ctl_info *edac_dev)
{
	struct edac_priv *priv = edac_dev->pvt_info;

	priv->debugfs_dir = edac_debugfs_create_dir("ocm");
	if (!priv->debugfs_dir)
		return;

	edac_debugfs_create_x32("inject_fault_count", 0644, priv->debugfs_dir,
				&priv->fault_injection_cnt);
	edac_debugfs_create_file("inject_ue_bitpos", 0644, priv->debugfs_dir,
				 edac_dev, &inject_ue_fops);
	edac_debugfs_create_file("inject_ce_bitpos", 0644, priv->debugfs_dir,
				 edac_dev, &inject_ce_fops);
}
#endif

static int edac_probe(struct platform_device *pdev)
{
	struct edac_device_ctl_info *dci;
	struct edac_priv *priv;
	void __iomem *baseaddr;
	struct resource *res;
	int irq, ret;

	baseaddr = devm_platform_get_and_ioremap_resource(pdev, 0, &res);
	if (IS_ERR(baseaddr))
		return PTR_ERR(baseaddr);

	if (!get_eccstate(baseaddr)) {
		edac_printk(KERN_INFO, EDAC_DEVICE, "ECC not enabled\n");
		return -ENXIO;
	}

	dci = edac_device_alloc_ctl_info(sizeof(*priv), ZYNQMP_OCM_EDAC_STRING,
					 1, ZYNQMP_OCM_EDAC_STRING, 1, 0, NULL, 0,
					 edac_device_alloc_index());
	if (!dci)
		return -ENOMEM;

	priv = dci->pvt_info;
	platform_set_drvdata(pdev, dci);
	dci->dev = &pdev->dev;
	priv->baseaddr = baseaddr;
	dci->mod_name = pdev->dev.driver->name;
	dci->ctl_name = ZYNQMP_OCM_EDAC_STRING;
	dci->dev_name = dev_name(&pdev->dev);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		ret = irq;
		goto free_dev_ctl;
	}

	ret = devm_request_irq(&pdev->dev, irq, intr_handler, 0,
			       dev_name(&pdev->dev), dci);
	if (ret) {
		edac_printk(KERN_ERR, EDAC_DEVICE, "Failed to request Irq\n");
		goto free_dev_ctl;
	}

	/* Enable UE, CE interrupts */
	writel((OCM_CEINTR_MASK | OCM_UEINTR_MASK), priv->baseaddr + OCM_IEN_OFST);

#ifdef CONFIG_EDAC_DEBUG
	setup_debugfs(dci);
#endif

	ret = edac_device_add_device(dci);
	if (ret)
		goto free_dev_ctl;

	return 0;

free_dev_ctl:
	edac_device_free_ctl_info(dci);

	return ret;
}

static int edac_remove(struct platform_device *pdev)
{
	struct edac_device_ctl_info *dci = platform_get_drvdata(pdev);
	struct edac_priv *priv = dci->pvt_info;

	/* Disable UE, CE interrupts */
	writel((OCM_CEINTR_MASK | OCM_UEINTR_MASK), priv->baseaddr + OCM_IDS_OFST);

#ifdef CONFIG_EDAC_DEBUG
	debugfs_remove_recursive(priv->debugfs_dir);
#endif

	edac_device_del_device(&pdev->dev);
	edac_device_free_ctl_info(dci);

	return 0;
}

static const struct of_device_id zynqmp_ocm_edac_match[] = {
	{ .compatible = "xlnx,zynqmp-ocmc-1.0"},
	{ /* end of table */ }
};

MODULE_DEVICE_TABLE(of, zynqmp_ocm_edac_match);

static struct platform_driver zynqmp_ocm_edac_driver = {
	.driver = {
		   .name = "zynqmp-ocm-edac",
		   .of_match_table = zynqmp_ocm_edac_match,
		   },
	.probe = edac_probe,
	.remove = edac_remove,
};

module_platform_driver(zynqmp_ocm_edac_driver);

MODULE_AUTHOR("Advanced Micro Devices, Inc");
MODULE_DESCRIPTION("Xilinx ZynqMP OCM ECC driver");
MODULE_LICENSE("GPL");
