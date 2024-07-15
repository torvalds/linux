// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2014-2021 Broadcom
 */

#include <linux/init.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/panic_notifier.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/sysfs.h>
#include <linux/io.h>
#include <linux/string.h>
#include <linux/device.h>
#include <linux/list.h>
#include <linux/of.h>
#include <linux/bitops.h>
#include <linux/pm.h>
#include <linux/kernel.h>
#include <linux/kdebug.h>
#include <linux/notifier.h>

#ifdef CONFIG_MIPS
#include <asm/traps.h>
#endif

#define  ARB_ERR_CAP_CLEAR		(1 << 0)
#define  ARB_ERR_CAP_STATUS_TIMEOUT	(1 << 12)
#define  ARB_ERR_CAP_STATUS_TEA		(1 << 11)
#define  ARB_ERR_CAP_STATUS_WRITE	(1 << 1)
#define  ARB_ERR_CAP_STATUS_VALID	(1 << 0)

#define  ARB_BP_CAP_CLEAR		(1 << 0)
#define  ARB_BP_CAP_STATUS_PROT_SHIFT	14
#define  ARB_BP_CAP_STATUS_TYPE		(1 << 13)
#define  ARB_BP_CAP_STATUS_RSP_SHIFT	10
#define  ARB_BP_CAP_STATUS_MASK		GENMASK(1, 0)
#define  ARB_BP_CAP_STATUS_BS_SHIFT	2
#define  ARB_BP_CAP_STATUS_WRITE	(1 << 1)
#define  ARB_BP_CAP_STATUS_VALID	(1 << 0)

enum {
	ARB_TIMER,
	ARB_BP_CAP_CLR,
	ARB_BP_CAP_HI_ADDR,
	ARB_BP_CAP_ADDR,
	ARB_BP_CAP_STATUS,
	ARB_BP_CAP_MASTER,
	ARB_ERR_CAP_CLR,
	ARB_ERR_CAP_HI_ADDR,
	ARB_ERR_CAP_ADDR,
	ARB_ERR_CAP_STATUS,
	ARB_ERR_CAP_MASTER,
};

static const int gisb_offsets_bcm7038[] = {
	[ARB_TIMER]		= 0x00c,
	[ARB_BP_CAP_CLR]	= 0x014,
	[ARB_BP_CAP_HI_ADDR]	= -1,
	[ARB_BP_CAP_ADDR]	= 0x0b8,
	[ARB_BP_CAP_STATUS]	= 0x0c0,
	[ARB_BP_CAP_MASTER]	= -1,
	[ARB_ERR_CAP_CLR]	= 0x0c4,
	[ARB_ERR_CAP_HI_ADDR]	= -1,
	[ARB_ERR_CAP_ADDR]	= 0x0c8,
	[ARB_ERR_CAP_STATUS]	= 0x0d0,
	[ARB_ERR_CAP_MASTER]	= -1,
};

static const int gisb_offsets_bcm7278[] = {
	[ARB_TIMER]		= 0x008,
	[ARB_BP_CAP_CLR]	= 0x01c,
	[ARB_BP_CAP_HI_ADDR]	= -1,
	[ARB_BP_CAP_ADDR]	= 0x220,
	[ARB_BP_CAP_STATUS]	= 0x230,
	[ARB_BP_CAP_MASTER]	= 0x234,
	[ARB_ERR_CAP_CLR]	= 0x7f8,
	[ARB_ERR_CAP_HI_ADDR]	= -1,
	[ARB_ERR_CAP_ADDR]	= 0x7e0,
	[ARB_ERR_CAP_STATUS]	= 0x7f0,
	[ARB_ERR_CAP_MASTER]	= 0x7f4,
};

static const int gisb_offsets_bcm7400[] = {
	[ARB_TIMER]		= 0x00c,
	[ARB_BP_CAP_CLR]	= 0x014,
	[ARB_BP_CAP_HI_ADDR]	= -1,
	[ARB_BP_CAP_ADDR]	= 0x0b8,
	[ARB_BP_CAP_STATUS]	= 0x0c0,
	[ARB_BP_CAP_MASTER]	= 0x0c4,
	[ARB_ERR_CAP_CLR]	= 0x0c8,
	[ARB_ERR_CAP_HI_ADDR]	= -1,
	[ARB_ERR_CAP_ADDR]	= 0x0cc,
	[ARB_ERR_CAP_STATUS]	= 0x0d4,
	[ARB_ERR_CAP_MASTER]	= 0x0d8,
};

static const int gisb_offsets_bcm74165[] = {
	[ARB_TIMER]		= 0x008,
	[ARB_BP_CAP_CLR]	= 0x044,
	[ARB_BP_CAP_HI_ADDR]	= -1,
	[ARB_BP_CAP_ADDR]	= 0x048,
	[ARB_BP_CAP_STATUS]	= 0x058,
	[ARB_BP_CAP_MASTER]	= 0x05c,
	[ARB_ERR_CAP_CLR]	= 0x038,
	[ARB_ERR_CAP_HI_ADDR]	= -1,
	[ARB_ERR_CAP_ADDR]	= 0x020,
	[ARB_ERR_CAP_STATUS]	= 0x030,
	[ARB_ERR_CAP_MASTER]	= 0x034,
};

static const int gisb_offsets_bcm7435[] = {
	[ARB_TIMER]		= 0x00c,
	[ARB_BP_CAP_CLR]	= 0x014,
	[ARB_BP_CAP_HI_ADDR]	= -1,
	[ARB_BP_CAP_ADDR]	= 0x158,
	[ARB_BP_CAP_STATUS]	= 0x160,
	[ARB_BP_CAP_MASTER]	= 0x164,
	[ARB_ERR_CAP_CLR]	= 0x168,
	[ARB_ERR_CAP_HI_ADDR]	= -1,
	[ARB_ERR_CAP_ADDR]	= 0x16c,
	[ARB_ERR_CAP_STATUS]	= 0x174,
	[ARB_ERR_CAP_MASTER]	= 0x178,
};

static const int gisb_offsets_bcm7445[] = {
	[ARB_TIMER]		= 0x008,
	[ARB_BP_CAP_CLR]	= 0x010,
	[ARB_BP_CAP_HI_ADDR]	= -1,
	[ARB_BP_CAP_ADDR]	= 0x1d8,
	[ARB_BP_CAP_STATUS]	= 0x1e0,
	[ARB_BP_CAP_MASTER]	= 0x1e4,
	[ARB_ERR_CAP_CLR]	= 0x7e4,
	[ARB_ERR_CAP_HI_ADDR]	= 0x7e8,
	[ARB_ERR_CAP_ADDR]	= 0x7ec,
	[ARB_ERR_CAP_STATUS]	= 0x7f4,
	[ARB_ERR_CAP_MASTER]	= 0x7f8,
};

struct brcmstb_gisb_arb_device {
	void __iomem	*base;
	const int	*gisb_offsets;
	bool		big_endian;
	struct mutex	lock;
	struct list_head next;
	u32 valid_mask;
	const char *master_names[sizeof(u32) * BITS_PER_BYTE];
	u32 saved_timeout;
};

static LIST_HEAD(brcmstb_gisb_arb_device_list);

static u32 gisb_read(struct brcmstb_gisb_arb_device *gdev, int reg)
{
	int offset = gdev->gisb_offsets[reg];

	if (offset < 0) {
		/* return 1 if the hardware doesn't have ARB_ERR_CAP_MASTER */
		if (reg == ARB_ERR_CAP_MASTER)
			return 1;
		else
			return 0;
	}

	if (gdev->big_endian)
		return ioread32be(gdev->base + offset);
	else
		return ioread32(gdev->base + offset);
}

static u64 gisb_read_address(struct brcmstb_gisb_arb_device *gdev)
{
	u64 value;

	value = gisb_read(gdev, ARB_ERR_CAP_ADDR);
	value |= (u64)gisb_read(gdev, ARB_ERR_CAP_HI_ADDR) << 32;

	return value;
}

static u64 gisb_read_bp_address(struct brcmstb_gisb_arb_device *gdev)
{
	u64 value;

	value = gisb_read(gdev, ARB_BP_CAP_ADDR);
	value |= (u64)gisb_read(gdev, ARB_BP_CAP_HI_ADDR) << 32;

	return value;
}

static void gisb_write(struct brcmstb_gisb_arb_device *gdev, u32 val, int reg)
{
	int offset = gdev->gisb_offsets[reg];

	if (offset == -1)
		return;

	if (gdev->big_endian)
		iowrite32be(val, gdev->base + offset);
	else
		iowrite32(val, gdev->base + offset);
}

static ssize_t gisb_arb_get_timeout(struct device *dev,
				    struct device_attribute *attr,
				    char *buf)
{
	struct brcmstb_gisb_arb_device *gdev = dev_get_drvdata(dev);
	u32 timeout;

	mutex_lock(&gdev->lock);
	timeout = gisb_read(gdev, ARB_TIMER);
	mutex_unlock(&gdev->lock);

	return sprintf(buf, "%d", timeout);
}

static ssize_t gisb_arb_set_timeout(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	struct brcmstb_gisb_arb_device *gdev = dev_get_drvdata(dev);
	int val, ret;

	ret = kstrtoint(buf, 10, &val);
	if (ret < 0)
		return ret;

	if (val == 0 || val >= 0xffffffff)
		return -EINVAL;

	mutex_lock(&gdev->lock);
	gisb_write(gdev, val, ARB_TIMER);
	mutex_unlock(&gdev->lock);

	return count;
}

static const char *
brcmstb_gisb_master_to_str(struct brcmstb_gisb_arb_device *gdev,
						u32 masters)
{
	u32 mask = gdev->valid_mask & masters;

	if (hweight_long(mask) != 1)
		return NULL;

	return gdev->master_names[ffs(mask) - 1];
}

static int brcmstb_gisb_arb_decode_addr(struct brcmstb_gisb_arb_device *gdev,
					const char *reason)
{
	u32 cap_status;
	u64 arb_addr;
	u32 master;
	const char *m_name;
	char m_fmt[11];

	cap_status = gisb_read(gdev, ARB_ERR_CAP_STATUS);

	/* Invalid captured address, bail out */
	if (!(cap_status & ARB_ERR_CAP_STATUS_VALID))
		return 1;

	/* Read the address and master */
	arb_addr = gisb_read_address(gdev);
	master = gisb_read(gdev, ARB_ERR_CAP_MASTER);

	m_name = brcmstb_gisb_master_to_str(gdev, master);
	if (!m_name) {
		snprintf(m_fmt, sizeof(m_fmt), "0x%08x", master);
		m_name = m_fmt;
	}

	pr_crit("GISB: %s at 0x%llx [%c %s], core: %s\n",
		reason, arb_addr,
		cap_status & ARB_ERR_CAP_STATUS_WRITE ? 'W' : 'R',
		cap_status & ARB_ERR_CAP_STATUS_TIMEOUT ? "timeout" : "",
		m_name);

	/* clear the GISB error */
	gisb_write(gdev, ARB_ERR_CAP_CLEAR, ARB_ERR_CAP_CLR);

	return 0;
}

#ifdef CONFIG_MIPS
static int brcmstb_bus_error_handler(struct pt_regs *regs, int is_fixup)
{
	int ret = 0;
	struct brcmstb_gisb_arb_device *gdev;
	u32 cap_status;

	list_for_each_entry(gdev, &brcmstb_gisb_arb_device_list, next) {
		cap_status = gisb_read(gdev, ARB_ERR_CAP_STATUS);

		/* Invalid captured address, bail out */
		if (!(cap_status & ARB_ERR_CAP_STATUS_VALID)) {
			is_fixup = 1;
			goto out;
		}

		ret |= brcmstb_gisb_arb_decode_addr(gdev, "bus error");
	}
out:
	return is_fixup ? MIPS_BE_FIXUP : MIPS_BE_FATAL;
}
#endif

static irqreturn_t brcmstb_gisb_timeout_handler(int irq, void *dev_id)
{
	brcmstb_gisb_arb_decode_addr(dev_id, "timeout");

	return IRQ_HANDLED;
}

static irqreturn_t brcmstb_gisb_tea_handler(int irq, void *dev_id)
{
	brcmstb_gisb_arb_decode_addr(dev_id, "target abort");

	return IRQ_HANDLED;
}

static irqreturn_t brcmstb_gisb_bp_handler(int irq, void *dev_id)
{
	struct brcmstb_gisb_arb_device *gdev = dev_id;
	const char *m_name;
	u32 bp_status;
	u64 arb_addr;
	u32 master;
	char m_fmt[11];

	bp_status = gisb_read(gdev, ARB_BP_CAP_STATUS);

	/* Invalid captured address, bail out */
	if (!(bp_status & ARB_BP_CAP_STATUS_VALID))
		return IRQ_HANDLED;

	/* Read the address and master */
	arb_addr = gisb_read_bp_address(gdev);
	master = gisb_read(gdev, ARB_BP_CAP_MASTER);

	m_name = brcmstb_gisb_master_to_str(gdev, master);
	if (!m_name) {
		snprintf(m_fmt, sizeof(m_fmt), "0x%08x", master);
		m_name = m_fmt;
	}

	pr_crit("GISB: breakpoint at 0x%llx [%c], core: %s\n",
		arb_addr, bp_status & ARB_BP_CAP_STATUS_WRITE ? 'W' : 'R',
		m_name);

	/* clear the GISB error */
	gisb_write(gdev, ARB_ERR_CAP_CLEAR, ARB_ERR_CAP_CLR);

	return IRQ_HANDLED;
}

/*
 * Dump out gisb errors on die or panic.
 */
static int dump_gisb_error(struct notifier_block *self, unsigned long v,
			   void *p);

static struct notifier_block gisb_die_notifier = {
	.notifier_call = dump_gisb_error,
};

static struct notifier_block gisb_panic_notifier = {
	.notifier_call = dump_gisb_error,
};

static int dump_gisb_error(struct notifier_block *self, unsigned long v,
			   void *p)
{
	struct brcmstb_gisb_arb_device *gdev;
	const char *reason = "panic";

	if (self == &gisb_die_notifier)
		reason = "die";

	/* iterate over each GISB arb registered handlers */
	list_for_each_entry(gdev, &brcmstb_gisb_arb_device_list, next)
		brcmstb_gisb_arb_decode_addr(gdev, reason);

	return NOTIFY_DONE;
}

static DEVICE_ATTR(gisb_arb_timeout, S_IWUSR | S_IRUGO,
		gisb_arb_get_timeout, gisb_arb_set_timeout);

static struct attribute *gisb_arb_sysfs_attrs[] = {
	&dev_attr_gisb_arb_timeout.attr,
	NULL,
};

static struct attribute_group gisb_arb_sysfs_attr_group = {
	.attrs = gisb_arb_sysfs_attrs,
};

static const struct of_device_id brcmstb_gisb_arb_of_match[] = {
	{ .compatible = "brcm,gisb-arb",         .data = gisb_offsets_bcm7445 },
	{ .compatible = "brcm,bcm7445-gisb-arb", .data = gisb_offsets_bcm7445 },
	{ .compatible = "brcm,bcm7435-gisb-arb", .data = gisb_offsets_bcm7435 },
	{ .compatible = "brcm,bcm7400-gisb-arb", .data = gisb_offsets_bcm7400 },
	{ .compatible = "brcm,bcm7278-gisb-arb", .data = gisb_offsets_bcm7278 },
	{ .compatible = "brcm,bcm7038-gisb-arb", .data = gisb_offsets_bcm7038 },
	{ .compatible = "brcm,bcm74165-gisb-arb", .data = gisb_offsets_bcm74165 },
	{ },
};

static int __init brcmstb_gisb_arb_probe(struct platform_device *pdev)
{
	struct device_node *dn = pdev->dev.of_node;
	struct brcmstb_gisb_arb_device *gdev;
	const struct of_device_id *of_id;
	int err, timeout_irq, tea_irq, bp_irq;
	unsigned int num_masters, j = 0;
	int i, first, last;

	timeout_irq = platform_get_irq(pdev, 0);
	tea_irq = platform_get_irq(pdev, 1);
	bp_irq = platform_get_irq(pdev, 2);

	gdev = devm_kzalloc(&pdev->dev, sizeof(*gdev), GFP_KERNEL);
	if (!gdev)
		return -ENOMEM;

	mutex_init(&gdev->lock);
	INIT_LIST_HEAD(&gdev->next);

	gdev->base = devm_platform_get_and_ioremap_resource(pdev, 0, NULL);
	if (IS_ERR(gdev->base))
		return PTR_ERR(gdev->base);

	of_id = of_match_node(brcmstb_gisb_arb_of_match, dn);
	if (!of_id) {
		pr_err("failed to look up compatible string\n");
		return -EINVAL;
	}
	gdev->gisb_offsets = of_id->data;
	gdev->big_endian = of_device_is_big_endian(dn);

	err = devm_request_irq(&pdev->dev, timeout_irq,
				brcmstb_gisb_timeout_handler, 0, pdev->name,
				gdev);
	if (err < 0)
		return err;

	err = devm_request_irq(&pdev->dev, tea_irq,
				brcmstb_gisb_tea_handler, 0, pdev->name,
				gdev);
	if (err < 0)
		return err;

	/* Interrupt is optional */
	if (bp_irq > 0) {
		err = devm_request_irq(&pdev->dev, bp_irq,
				       brcmstb_gisb_bp_handler, 0, pdev->name,
				       gdev);
		if (err < 0)
			return err;
	}

	/* If we do not have a valid mask, assume all masters are enabled */
	if (of_property_read_u32(dn, "brcm,gisb-arb-master-mask",
				&gdev->valid_mask))
		gdev->valid_mask = 0xffffffff;

	/* Proceed with reading the litteral names if we agree on the
	 * number of masters
	 */
	num_masters = of_property_count_strings(dn,
			"brcm,gisb-arb-master-names");
	if (hweight_long(gdev->valid_mask) == num_masters) {
		first = ffs(gdev->valid_mask) - 1;
		last = fls(gdev->valid_mask) - 1;

		for (i = first; i < last; i++) {
			if (!(gdev->valid_mask & BIT(i)))
				continue;

			of_property_read_string_index(dn,
					"brcm,gisb-arb-master-names", j,
					&gdev->master_names[i]);
			j++;
		}
	}

	err = sysfs_create_group(&pdev->dev.kobj, &gisb_arb_sysfs_attr_group);
	if (err)
		return err;

	platform_set_drvdata(pdev, gdev);

	list_add_tail(&gdev->next, &brcmstb_gisb_arb_device_list);

#ifdef CONFIG_MIPS
	mips_set_be_handler(brcmstb_bus_error_handler);
#endif

	if (list_is_singular(&brcmstb_gisb_arb_device_list)) {
		register_die_notifier(&gisb_die_notifier);
		atomic_notifier_chain_register(&panic_notifier_list,
					       &gisb_panic_notifier);
	}

	dev_info(&pdev->dev, "registered irqs: %d, %d\n",
		 timeout_irq, tea_irq);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int brcmstb_gisb_arb_suspend(struct device *dev)
{
	struct brcmstb_gisb_arb_device *gdev = dev_get_drvdata(dev);

	gdev->saved_timeout = gisb_read(gdev, ARB_TIMER);

	return 0;
}

/* Make sure we provide the same timeout value that was configured before, and
 * do this before the GISB timeout interrupt handler has any chance to run.
 */
static int brcmstb_gisb_arb_resume_noirq(struct device *dev)
{
	struct brcmstb_gisb_arb_device *gdev = dev_get_drvdata(dev);

	gisb_write(gdev, gdev->saved_timeout, ARB_TIMER);

	return 0;
}
#else
#define brcmstb_gisb_arb_suspend       NULL
#define brcmstb_gisb_arb_resume_noirq  NULL
#endif

static const struct dev_pm_ops brcmstb_gisb_arb_pm_ops = {
	.suspend	= brcmstb_gisb_arb_suspend,
	.resume_noirq	= brcmstb_gisb_arb_resume_noirq,
};

static struct platform_driver brcmstb_gisb_arb_driver = {
	.driver = {
		.name	= "brcm-gisb-arb",
		.of_match_table = brcmstb_gisb_arb_of_match,
		.pm	= &brcmstb_gisb_arb_pm_ops,
	},
};

static int __init brcm_gisb_driver_init(void)
{
	return platform_driver_probe(&brcmstb_gisb_arb_driver,
				     brcmstb_gisb_arb_probe);
}

module_init(brcm_gisb_driver_init);

MODULE_AUTHOR("Broadcom");
MODULE_DESCRIPTION("Broadcom STB GISB arbiter driver");
MODULE_LICENSE("GPL v2");
