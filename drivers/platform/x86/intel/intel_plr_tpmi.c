// SPDX-License-Identifier: GPL-2.0-only
/*
 * Performance Limit Reasons via TPMI
 *
 * Copyright (c) 2024, Intel Corporation.
 */

#include <linux/array_size.h>
#include <linux/auxiliary_bus.h>
#include <linux/bitmap.h>
#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/gfp_types.h>
#include <linux/intel_tpmi.h>
#include <linux/io.h>
#include <linux/kstrtox.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/seq_file.h>
#include <linux/sprintf.h>
#include <linux/types.h>

#define PLR_HEADER		0x00
#define PLR_DIE_LEVEL		0x18

#define PLR_INVALID		GENMASK_ULL(63, 0)

struct tpmi_plr_die {
	void __iomem *base;
};

struct tpmi_plr {
	struct dentry *dbgfs_dir;
	struct tpmi_plr_die *die_info;
	int num_dies;
};

static const char * const plr_coarse_reasons[] = {
	"FREQUENCY",
	"CURRENT",
	"POWER",
	"THERMAL",
	"PLATFORM",
	"MCP",
	"RAS",
	"MISC",
	"QOS",
	"DFC",
};

static u64 plr_read(struct tpmi_plr_die *plr_die, int offset)
{
	return readq(plr_die->base + offset);
}

static void plr_write(u64 val, struct tpmi_plr_die *plr_die, int offset)
{
	writeq(val, plr_die->base + offset);
}

static void plr_print_bits(struct seq_file *s, u64 val, int bits)
{
	const unsigned long mask[] = { BITMAP_FROM_U64(val) };
	int bit;

	for_each_set_bit(bit, mask, bits) {
		if (bit >= ARRAY_SIZE(plr_coarse_reasons))
			seq_printf(s, " UNKNOWN(%d)", bit);
		else
			seq_printf(s, " %s", plr_coarse_reasons[bit]);
	}

	if (!val)
		seq_puts(s, " none");

	seq_putc(s, '\n');
}

static int plr_status_show(struct seq_file *s, void *unused)
{
	struct tpmi_plr_die *plr_die = s->private;
	u64 val;

	val = plr_read(plr_die, PLR_DIE_LEVEL);
	seq_puts(s, "cpus");
	plr_print_bits(s, val, 32);

	return 0;
}

static ssize_t plr_status_write(struct file *filp, const char __user *ubuf,
				size_t count, loff_t *ppos)
{
	struct seq_file *s = filp->private_data;
	struct tpmi_plr_die *plr_die = s->private;
	bool val;
	int ret;

	ret = kstrtobool_from_user(ubuf, count, &val);
	if (ret)
		return ret;

	if (val != 0)
		return -EINVAL;

	plr_write(0, plr_die, PLR_DIE_LEVEL);

	return count;
}
DEFINE_SHOW_STORE_ATTRIBUTE(plr_status);

static int intel_plr_probe(struct auxiliary_device *auxdev, const struct auxiliary_device_id *id)
{
	struct intel_tpmi_plat_info *plat_info;
	struct dentry *dentry;
	int i, num_resources;
	struct resource *res;
	struct tpmi_plr *plr;
	void __iomem *base;
	char name[16];
	int err;

	plat_info = tpmi_get_platform_data(auxdev);
	if (!plat_info)
		return dev_err_probe(&auxdev->dev, -EINVAL, "No platform info\n");

	dentry = tpmi_get_debugfs_dir(auxdev);
	if (!dentry)
		return dev_err_probe(&auxdev->dev, -ENODEV, "No TPMI debugfs directory.\n");

	num_resources = tpmi_get_resource_count(auxdev);
	if (!num_resources)
		return -EINVAL;

	plr = devm_kzalloc(&auxdev->dev, sizeof(*plr), GFP_KERNEL);
	if (!plr)
		return -ENOMEM;

	plr->die_info = devm_kcalloc(&auxdev->dev, num_resources, sizeof(*plr->die_info),
				     GFP_KERNEL);
	if (!plr->die_info)
		return -ENOMEM;

	plr->num_dies = num_resources;
	plr->dbgfs_dir = debugfs_create_dir("plr", dentry);

	for (i = 0; i < num_resources; i++) {
		res = tpmi_get_resource_at_index(auxdev, i);
		if (!res) {
			err = dev_err_probe(&auxdev->dev, -EINVAL, "No resource\n");
			goto err;
		}

		base = devm_ioremap_resource(&auxdev->dev, res);
		if (IS_ERR(base)) {
			err = PTR_ERR(base);
			goto err;
		}

		plr->die_info[i].base = base;

		if (plr_read(&plr->die_info[i], PLR_HEADER) == PLR_INVALID)
			continue;

		snprintf(name, sizeof(name), "domain%d", i);

		dentry = debugfs_create_dir(name, plr->dbgfs_dir);
		debugfs_create_file("status", 0444, dentry, &plr->die_info[i],
				    &plr_status_fops);
	}

	auxiliary_set_drvdata(auxdev, plr);

	return 0;

err:
	debugfs_remove_recursive(plr->dbgfs_dir);
	return err;
}

static void intel_plr_remove(struct auxiliary_device *auxdev)
{
	struct tpmi_plr *plr = auxiliary_get_drvdata(auxdev);

	debugfs_remove_recursive(plr->dbgfs_dir);
}

static const struct auxiliary_device_id intel_plr_id_table[] = {
	{ .name = "intel_vsec.tpmi-plr" },
	{}
};
MODULE_DEVICE_TABLE(auxiliary, intel_plr_id_table);

static struct auxiliary_driver intel_plr_aux_driver = {
	.id_table       = intel_plr_id_table,
	.remove         = intel_plr_remove,
	.probe          = intel_plr_probe,
};
module_auxiliary_driver(intel_plr_aux_driver);

MODULE_IMPORT_NS(INTEL_TPMI);
MODULE_DESCRIPTION("Intel TPMI PLR Driver");
MODULE_LICENSE("GPL");
