// SPDX-License-Identifier: GPL-2.0-only
/*
 * intel_rapl_tpmi: Intel RAPL driver via TPMI interface
 *
 * Copyright (c) 2023, Intel Corporation.
 * All Rights Reserved.
 *
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/auxiliary_bus.h>
#include <linux/io.h>
#include <linux/intel_tpmi.h>
#include <linux/intel_rapl.h>
#include <linux/module.h>
#include <linux/slab.h>

#define TPMI_RAPL_VERSION 1

/* 1 header + 10 registers + 5 reserved. 8 bytes for each. */
#define TPMI_RAPL_DOMAIN_SIZE 128

enum tpmi_rapl_domain_type {
	TPMI_RAPL_DOMAIN_INVALID,
	TPMI_RAPL_DOMAIN_SYSTEM,
	TPMI_RAPL_DOMAIN_PACKAGE,
	TPMI_RAPL_DOMAIN_RESERVED,
	TPMI_RAPL_DOMAIN_MEMORY,
	TPMI_RAPL_DOMAIN_MAX,
};

enum tpmi_rapl_register {
	TPMI_RAPL_REG_HEADER,
	TPMI_RAPL_REG_UNIT,
	TPMI_RAPL_REG_PL1,
	TPMI_RAPL_REG_PL2,
	TPMI_RAPL_REG_PL3,
	TPMI_RAPL_REG_PL4,
	TPMI_RAPL_REG_RESERVED,
	TPMI_RAPL_REG_ENERGY_STATUS,
	TPMI_RAPL_REG_PERF_STATUS,
	TPMI_RAPL_REG_POWER_INFO,
	TPMI_RAPL_REG_INTERRUPT,
	TPMI_RAPL_REG_MAX = 15,
};

struct tpmi_rapl_package {
	struct rapl_if_priv priv;
	struct intel_tpmi_plat_info *tpmi_info;
	struct rapl_package *rp;
	void __iomem *base;
	struct list_head node;
};

static LIST_HEAD(tpmi_rapl_packages);
static DEFINE_MUTEX(tpmi_rapl_lock);

static struct powercap_control_type *tpmi_control_type;

static int tpmi_rapl_read_raw(int id, struct reg_action *ra)
{
	if (!ra->reg)
		return -EINVAL;

	ra->value = readq((void __iomem *)ra->reg);

	ra->value &= ra->mask;
	return 0;
}

static int tpmi_rapl_write_raw(int id, struct reg_action *ra)
{
	u64 val;

	if (!ra->reg)
		return -EINVAL;

	val = readq((void __iomem *)ra->reg);

	val &= ~ra->mask;
	val |= ra->value;

	writeq(val, (void __iomem *)ra->reg);
	return 0;
}

static struct tpmi_rapl_package *trp_alloc(int pkg_id)
{
	struct tpmi_rapl_package *trp;
	int ret;

	mutex_lock(&tpmi_rapl_lock);

	if (list_empty(&tpmi_rapl_packages)) {
		tpmi_control_type = powercap_register_control_type(NULL, "intel-rapl", NULL);
		if (IS_ERR(tpmi_control_type)) {
			ret = PTR_ERR(tpmi_control_type);
			goto err_unlock;
		}
	}

	trp = kzalloc(sizeof(*trp), GFP_KERNEL);
	if (!trp) {
		ret = -ENOMEM;
		goto err_del_powercap;
	}

	list_add(&trp->node, &tpmi_rapl_packages);

	mutex_unlock(&tpmi_rapl_lock);
	return trp;

err_del_powercap:
	if (list_empty(&tpmi_rapl_packages))
		powercap_unregister_control_type(tpmi_control_type);
err_unlock:
	mutex_unlock(&tpmi_rapl_lock);
	return ERR_PTR(ret);
}

static void trp_release(struct tpmi_rapl_package *trp)
{
	mutex_lock(&tpmi_rapl_lock);
	list_del(&trp->node);

	if (list_empty(&tpmi_rapl_packages))
		powercap_unregister_control_type(tpmi_control_type);

	kfree(trp);
	mutex_unlock(&tpmi_rapl_lock);
}

static int parse_one_domain(struct tpmi_rapl_package *trp, u32 offset)
{
	u8 tpmi_domain_version;
	enum rapl_domain_type domain_type;
	enum tpmi_rapl_domain_type tpmi_domain_type;
	enum tpmi_rapl_register reg_index;
	enum rapl_domain_reg_id reg_id;
	int tpmi_domain_size, tpmi_domain_flags;
	u64 *tpmi_rapl_regs = trp->base + offset;
	u64 tpmi_domain_header = readq((void __iomem *)tpmi_rapl_regs);

	/* Domain Parent bits are ignored for now */
	tpmi_domain_version = tpmi_domain_header & 0xff;
	tpmi_domain_type = tpmi_domain_header >> 8 & 0xff;
	tpmi_domain_size = tpmi_domain_header >> 16 & 0xff;
	tpmi_domain_flags = tpmi_domain_header >> 32 & 0xffff;

	if (tpmi_domain_version != TPMI_RAPL_VERSION) {
		pr_warn(FW_BUG "Unsupported version:%d\n", tpmi_domain_version);
		return -ENODEV;
	}

	/* Domain size: in unit of 128 Bytes */
	if (tpmi_domain_size != 1) {
		pr_warn(FW_BUG "Invalid Domain size %d\n", tpmi_domain_size);
		return -EINVAL;
	}

	/* Unit register and Energy Status register are mandatory for each domain */
	if (!(tpmi_domain_flags & BIT(TPMI_RAPL_REG_UNIT)) ||
	    !(tpmi_domain_flags & BIT(TPMI_RAPL_REG_ENERGY_STATUS))) {
		pr_warn(FW_BUG "Invalid Domain flag 0x%x\n", tpmi_domain_flags);
		return -EINVAL;
	}

	switch (tpmi_domain_type) {
	case TPMI_RAPL_DOMAIN_PACKAGE:
		domain_type = RAPL_DOMAIN_PACKAGE;
		break;
	case TPMI_RAPL_DOMAIN_SYSTEM:
		domain_type = RAPL_DOMAIN_PLATFORM;
		break;
	case TPMI_RAPL_DOMAIN_MEMORY:
		domain_type = RAPL_DOMAIN_DRAM;
		break;
	default:
		pr_warn(FW_BUG "Unsupported Domain type %d\n", tpmi_domain_type);
		return -EINVAL;
	}

	if (trp->priv.regs[domain_type][RAPL_DOMAIN_REG_UNIT]) {
		pr_warn(FW_BUG "Duplicate Domain type %d\n", tpmi_domain_type);
		return -EINVAL;
	}

	reg_index = TPMI_RAPL_REG_HEADER;
	while (++reg_index != TPMI_RAPL_REG_MAX) {
		if (!(tpmi_domain_flags & BIT(reg_index)))
			continue;

		switch (reg_index) {
		case TPMI_RAPL_REG_UNIT:
			reg_id = RAPL_DOMAIN_REG_UNIT;
			break;
		case TPMI_RAPL_REG_PL1:
			reg_id = RAPL_DOMAIN_REG_LIMIT;
			trp->priv.limits[domain_type] |= BIT(POWER_LIMIT1);
			break;
		case TPMI_RAPL_REG_PL2:
			reg_id = RAPL_DOMAIN_REG_PL2;
			trp->priv.limits[domain_type] |= BIT(POWER_LIMIT2);
			break;
		case TPMI_RAPL_REG_PL4:
			reg_id = RAPL_DOMAIN_REG_PL4;
			trp->priv.limits[domain_type] |= BIT(POWER_LIMIT4);
			break;
		case TPMI_RAPL_REG_ENERGY_STATUS:
			reg_id = RAPL_DOMAIN_REG_STATUS;
			break;
		case TPMI_RAPL_REG_PERF_STATUS:
			reg_id = RAPL_DOMAIN_REG_PERF;
			break;
		case TPMI_RAPL_REG_POWER_INFO:
			reg_id = RAPL_DOMAIN_REG_INFO;
			break;
		default:
			continue;
		}
		trp->priv.regs[domain_type][reg_id] = (u64)&tpmi_rapl_regs[reg_index];
	}

	return 0;
}

static int intel_rapl_tpmi_probe(struct auxiliary_device *auxdev,
				 const struct auxiliary_device_id *id)
{
	struct tpmi_rapl_package *trp;
	struct intel_tpmi_plat_info *info;
	struct resource *res;
	u32 offset;
	int ret;

	info = tpmi_get_platform_data(auxdev);
	if (!info)
		return -ENODEV;

	trp = trp_alloc(info->package_id);
	if (IS_ERR(trp))
		return PTR_ERR(trp);

	if (tpmi_get_resource_count(auxdev) > 1) {
		dev_err(&auxdev->dev, "does not support multiple resources\n");
		ret = -EINVAL;
		goto err;
	}

	res = tpmi_get_resource_at_index(auxdev, 0);
	if (!res) {
		dev_err(&auxdev->dev, "can't fetch device resource info\n");
		ret = -EIO;
		goto err;
	}

	trp->base = devm_ioremap_resource(&auxdev->dev, res);
	if (IS_ERR(trp->base)) {
		ret = PTR_ERR(trp->base);
		goto err;
	}

	for (offset = 0; offset < resource_size(res); offset += TPMI_RAPL_DOMAIN_SIZE) {
		ret = parse_one_domain(trp, offset);
		if (ret)
			goto err;
	}

	trp->tpmi_info = info;
	trp->priv.type = RAPL_IF_TPMI;
	trp->priv.read_raw = tpmi_rapl_read_raw;
	trp->priv.write_raw = tpmi_rapl_write_raw;
	trp->priv.control_type = tpmi_control_type;

	/* RAPL TPMI I/F is per physical package */
	trp->rp = rapl_find_package_domain(info->package_id, &trp->priv, false);
	if (trp->rp) {
		dev_err(&auxdev->dev, "Domain for Package%d already exists\n", info->package_id);
		ret = -EEXIST;
		goto err;
	}

	trp->rp = rapl_add_package(info->package_id, &trp->priv, false);
	if (IS_ERR(trp->rp)) {
		dev_err(&auxdev->dev, "Failed to add RAPL Domain for Package%d, %ld\n",
			info->package_id, PTR_ERR(trp->rp));
		ret = PTR_ERR(trp->rp);
		goto err;
	}

	auxiliary_set_drvdata(auxdev, trp);

	return 0;
err:
	trp_release(trp);
	return ret;
}

static void intel_rapl_tpmi_remove(struct auxiliary_device *auxdev)
{
	struct tpmi_rapl_package *trp = auxiliary_get_drvdata(auxdev);

	rapl_remove_package(trp->rp);
	trp_release(trp);
}

static const struct auxiliary_device_id intel_rapl_tpmi_ids[] = {
	{.name = "intel_vsec.tpmi-rapl" },
	{ }
};

MODULE_DEVICE_TABLE(auxiliary, intel_rapl_tpmi_ids);

static struct auxiliary_driver intel_rapl_tpmi_driver = {
	.probe = intel_rapl_tpmi_probe,
	.remove = intel_rapl_tpmi_remove,
	.id_table = intel_rapl_tpmi_ids,
};

module_auxiliary_driver(intel_rapl_tpmi_driver)

MODULE_IMPORT_NS(INTEL_TPMI);

MODULE_DESCRIPTION("Intel RAPL TPMI Driver");
MODULE_LICENSE("GPL");
