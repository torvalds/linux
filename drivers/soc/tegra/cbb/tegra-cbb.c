// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2021-2022, NVIDIA CORPORATION. All rights reserved
 */

#include <linux/clk.h>
#include <linux/cpufeature.h>
#include <linux/debugfs.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/version.h>
#include <soc/tegra/fuse.h>
#include <soc/tegra/tegra-cbb.h>

void tegra_cbb_print_err(struct seq_file *file, const char *fmt, ...)
{
	struct va_format vaf;
	va_list args;

	va_start(args, fmt);

	if (file) {
		seq_vprintf(file, fmt, args);
	} else {
		vaf.fmt = fmt;
		vaf.va = &args;
		pr_crit("%pV", &vaf);
	}

	va_end(args);
}

void tegra_cbb_print_cache(struct seq_file *file, u32 cache)
{
	const char *buff_str, *mod_str, *rd_str, *wr_str;

	buff_str = (cache & BIT(0)) ? "Bufferable " : "";
	mod_str = (cache & BIT(1)) ? "Modifiable " : "";
	rd_str = (cache & BIT(2)) ? "Read-Allocate " : "";
	wr_str = (cache & BIT(3)) ? "Write-Allocate" : "";

	if (cache == 0x0)
		buff_str = "Device Non-Bufferable";

	tegra_cbb_print_err(file, "\t  Cache\t\t\t: 0x%x -- %s%s%s%s\n",
			    cache, buff_str, mod_str, rd_str, wr_str);
}

void tegra_cbb_print_prot(struct seq_file *file, u32 prot)
{
	const char *data_str, *secure_str, *priv_str;

	data_str = (prot & 0x4) ? "Instruction" : "Data";
	secure_str = (prot & 0x2) ? "Non-Secure" : "Secure";
	priv_str = (prot & 0x1) ? "Privileged" : "Unprivileged";

	tegra_cbb_print_err(file, "\t  Protection\t\t: 0x%x -- %s, %s, %s Access\n",
			    prot, priv_str, secure_str, data_str);
}

static int tegra_cbb_err_show(struct seq_file *file, void *data)
{
	struct tegra_cbb *cbb = file->private;

	return cbb->ops->debugfs_show(cbb, file, data);
}

static int tegra_cbb_err_open(struct inode *inode, struct file *file)
{
	return single_open(file, tegra_cbb_err_show, inode->i_private);
}

static const struct file_operations tegra_cbb_err_fops = {
	.open = tegra_cbb_err_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release
};

static int tegra_cbb_err_debugfs_init(struct tegra_cbb *cbb)
{
	static struct dentry *root;

	if (!root) {
		root = debugfs_create_file("tegra_cbb_err", 0444, NULL, cbb, &tegra_cbb_err_fops);
		if (IS_ERR_OR_NULL(root)) {
			pr_err("%s(): could not create debugfs node\n", __func__);
			return PTR_ERR(root);
		}
	}

	return 0;
}

void tegra_cbb_stall_enable(struct tegra_cbb *cbb)
{
	if (cbb->ops->stall_enable)
		cbb->ops->stall_enable(cbb);
}

void tegra_cbb_fault_enable(struct tegra_cbb *cbb)
{
	if (cbb->ops->fault_enable)
		cbb->ops->fault_enable(cbb);
}

void tegra_cbb_error_clear(struct tegra_cbb *cbb)
{
	if (cbb->ops->error_clear)
		cbb->ops->error_clear(cbb);
}

u32 tegra_cbb_get_status(struct tegra_cbb *cbb)
{
	if (cbb->ops->get_status)
		return cbb->ops->get_status(cbb);

	return 0;
}

int tegra_cbb_get_irq(struct platform_device *pdev, unsigned int *nonsec_irq,
		      unsigned int *sec_irq)
{
	unsigned int index = 0;
	int num_intr = 0, irq;

	num_intr = platform_irq_count(pdev);
	if (!num_intr)
		return -EINVAL;

	if (num_intr == 2) {
		irq = platform_get_irq(pdev, index);
		if (irq <= 0) {
			dev_err(&pdev->dev, "failed to get non-secure IRQ: %d\n", irq);
			return -ENOENT;
		}

		*nonsec_irq = irq;
		index++;
	}

	irq = platform_get_irq(pdev, index);
	if (irq <= 0) {
		dev_err(&pdev->dev, "failed to get secure IRQ: %d\n", irq);
		return -ENOENT;
	}

	*sec_irq = irq;

	if (num_intr == 1)
		dev_dbg(&pdev->dev, "secure IRQ: %u\n", *sec_irq);

	if (num_intr == 2)
		dev_dbg(&pdev->dev, "secure IRQ: %u, non-secure IRQ: %u\n", *sec_irq, *nonsec_irq);

	return 0;
}

int tegra_cbb_register(struct tegra_cbb *cbb)
{
	int ret;

	if (IS_ENABLED(CONFIG_DEBUG_FS)) {
		ret = tegra_cbb_err_debugfs_init(cbb);
		if (ret) {
			dev_err(cbb->dev, "failed to create debugfs\n");
			return ret;
		}
	}

	/* register interrupt handler for errors due to different initiators */
	ret = cbb->ops->interrupt_enable(cbb);
	if (ret < 0) {
		dev_err(cbb->dev, "Failed to register CBB Interrupt ISR");
		return ret;
	}

	cbb->ops->error_enable(cbb);
	dsb(sy);

	return 0;
}
