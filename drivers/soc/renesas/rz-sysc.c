// SPDX-License-Identifier: GPL-2.0
/*
 * RZ System controller driver
 *
 * Copyright (C) 2024 Renesas Electronics Corp.
 */

#include <linux/io.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/sys_soc.h>

#include "rz-sysc.h"

#define field_get(_mask, _reg) (((_reg) & (_mask)) >> (ffs(_mask) - 1))

/**
 * struct rz_sysc - RZ SYSC private data structure
 * @base: SYSC base address
 * @dev: SYSC device pointer
 */
struct rz_sysc {
	void __iomem *base;
	struct device *dev;
};

static int rz_sysc_soc_init(struct rz_sysc *sysc, const struct of_device_id *match)
{
	const struct rz_sysc_init_data *sysc_data = match->data;
	const struct rz_sysc_soc_id_init_data *soc_data = sysc_data->soc_id_init_data;
	struct soc_device_attribute *soc_dev_attr;
	const char *soc_id_start, *soc_id_end;
	u32 val, revision, specific_id;
	struct soc_device *soc_dev;
	char soc_id[32] = {0};
	size_t size;

	soc_id_start = strchr(match->compatible, ',') + 1;
	soc_id_end = strchr(match->compatible, '-');
	size = soc_id_end - soc_id_start + 1;
	if (size > 32)
		size = sizeof(soc_id);
	strscpy(soc_id, soc_id_start, size);

	soc_dev_attr = devm_kzalloc(sysc->dev, sizeof(*soc_dev_attr), GFP_KERNEL);
	if (!soc_dev_attr)
		return -ENOMEM;

	soc_dev_attr->family = devm_kstrdup(sysc->dev, soc_data->family, GFP_KERNEL);
	if (!soc_dev_attr->family)
		return -ENOMEM;

	soc_dev_attr->soc_id = devm_kstrdup(sysc->dev, soc_id, GFP_KERNEL);
	if (!soc_dev_attr->soc_id)
		return -ENOMEM;

	val = readl(sysc->base + soc_data->devid_offset);
	revision = field_get(soc_data->revision_mask, val);
	specific_id = field_get(soc_data->specific_id_mask, val);
	soc_dev_attr->revision = devm_kasprintf(sysc->dev, GFP_KERNEL, "%u", revision);
	if (!soc_dev_attr->revision)
		return -ENOMEM;

	if (soc_data->id && specific_id != soc_data->id) {
		dev_warn(sysc->dev, "SoC mismatch (product = 0x%x)\n", specific_id);
		return -ENODEV;
	}

	/* Try to call SoC-specific device identification */
	if (soc_data->print_id) {
		soc_data->print_id(sysc->dev, sysc->base, soc_dev_attr);
	} else {
		dev_info(sysc->dev, "Detected Renesas %s %s Rev %s\n",
			 soc_dev_attr->family, soc_dev_attr->soc_id, soc_dev_attr->revision);
	}

	soc_dev = soc_device_register(soc_dev_attr);
	if (IS_ERR(soc_dev))
		return PTR_ERR(soc_dev);

	return 0;
}

static const struct of_device_id rz_sysc_match[] = {
#ifdef CONFIG_SYSC_R9A08G045
	{ .compatible = "renesas,r9a08g045-sysc", .data = &rzg3s_sysc_init_data },
#endif
#ifdef CONFIG_SYS_R9A09G047
	{ .compatible = "renesas,r9a09g047-sys", .data = &rzg3e_sys_init_data },
#endif
#ifdef CONFIG_SYS_R9A09G057
	{ .compatible = "renesas,r9a09g057-sys", .data = &rzv2h_sys_init_data },
#endif
	{ }
};
MODULE_DEVICE_TABLE(of, rz_sysc_match);

static int rz_sysc_probe(struct platform_device *pdev)
{
	const struct of_device_id *match;
	struct device *dev = &pdev->dev;
	struct rz_sysc *sysc;

	match = of_match_node(rz_sysc_match, dev->of_node);
	if (!match)
		return -ENODEV;

	sysc = devm_kzalloc(dev, sizeof(*sysc), GFP_KERNEL);
	if (!sysc)
		return -ENOMEM;

	sysc->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(sysc->base))
		return PTR_ERR(sysc->base);

	sysc->dev = dev;
	return rz_sysc_soc_init(sysc, match);
}

static struct platform_driver rz_sysc_driver = {
	.driver = {
		.name = "renesas-rz-sysc",
		.suppress_bind_attrs = true,
		.of_match_table = rz_sysc_match
	},
	.probe = rz_sysc_probe
};

static int __init rz_sysc_init(void)
{
	return platform_driver_register(&rz_sysc_driver);
}
subsys_initcall(rz_sysc_init);

MODULE_DESCRIPTION("Renesas RZ System Controller Driver");
MODULE_AUTHOR("Claudiu Beznea <claudiu.beznea.uj@bp.renesas.com>");
MODULE_LICENSE("GPL");
