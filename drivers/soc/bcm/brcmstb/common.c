// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright © 2014 NVIDIA Corporation
 * Copyright © 2015 Broadcom Corporation
 */

#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/slab.h>
#include <linux/soc/brcmstb/brcmstb.h>
#include <linux/sys_soc.h>

struct brcmstb_soc_info {
	u32 family_id;
	u32 product_id;
};

static struct brcmstb_soc_info *soc_info;

u32 brcmstb_get_family_id(void)
{
	return soc_info ? soc_info->family_id : 0;
}
EXPORT_SYMBOL(brcmstb_get_family_id);

u32 brcmstb_get_product_id(void)
{
	return soc_info ? soc_info->product_id : 0;
}
EXPORT_SYMBOL(brcmstb_get_product_id);

static const struct of_device_id sun_top_ctrl_match[] = {
	{ .compatible = "brcm,bcm7125-sun-top-ctrl", },
	{ .compatible = "brcm,bcm7346-sun-top-ctrl", },
	{ .compatible = "brcm,bcm7358-sun-top-ctrl", },
	{ .compatible = "brcm,bcm7360-sun-top-ctrl", },
	{ .compatible = "brcm,bcm7362-sun-top-ctrl", },
	{ .compatible = "brcm,bcm7420-sun-top-ctrl", },
	{ .compatible = "brcm,bcm7425-sun-top-ctrl", },
	{ .compatible = "brcm,bcm7429-sun-top-ctrl", },
	{ .compatible = "brcm,bcm7435-sun-top-ctrl", },
	{ .compatible = "brcm,brcmstb-sun-top-ctrl", },
	{ }
};

static int __init brcmstb_soc_device_init(void)
{
	struct soc_device_attribute *soc_dev_attr;
	struct device_node *sun_top_ctrl;
	void __iomem *sun_top_ctrl_base;
	struct soc_device *soc_dev;
	int ret = 0;

	/* We could be on a multi-platform kernel, don't make this fatal but
	 * bail out early
	 */
	sun_top_ctrl = of_find_matching_node(NULL, sun_top_ctrl_match);
	if (!sun_top_ctrl)
		return 0;

	sun_top_ctrl_base = of_iomap(sun_top_ctrl, 0);
	if (!sun_top_ctrl_base) {
		ret = -ENODEV;
		goto out_put_node;
	}

	soc_info = kzalloc(sizeof(*soc_info), GFP_KERNEL);
	if (!soc_info) {
		ret = -ENOMEM;
		goto out_unmap;
	}

	soc_info->family_id = readl(sun_top_ctrl_base);
	soc_info->product_id = readl(sun_top_ctrl_base + 0x4);

	soc_dev_attr = kzalloc_obj(*soc_dev_attr);
	if (!soc_dev_attr) {
		ret = -ENOMEM;
		goto out_free_info;
	}

	soc_dev_attr->family = kasprintf(GFP_KERNEL, "%x",
					 soc_info->family_id >> 28 ?
					 soc_info->family_id >> 16 : soc_info->family_id >> 8);
	soc_dev_attr->soc_id = kasprintf(GFP_KERNEL, "%x",
					 soc_info->product_id >> 28 ?
					 soc_info->product_id >> 16 : soc_info->product_id >> 8);
	soc_dev_attr->revision = kasprintf(GFP_KERNEL, "%c%d",
					 ((soc_info->product_id & 0xf0) >> 4) + 'A',
					   soc_info->product_id & 0xf);

	soc_dev = soc_device_register(soc_dev_attr);
	if (IS_ERR(soc_dev)) {
		ret = PTR_ERR(soc_dev);
		goto out_free_attr;
	}

	iounmap(sun_top_ctrl_base);
	of_node_put(sun_top_ctrl);
	return 0;

out_free_attr:
	kfree(soc_dev_attr->revision);
	kfree(soc_dev_attr->soc_id);
	kfree(soc_dev_attr->family);
	kfree(soc_dev_attr);
out_free_info:
	kfree(soc_info);
	soc_info = NULL;
out_unmap:
	iounmap(sun_top_ctrl_base);
out_put_node:
	of_node_put(sun_top_ctrl);
	return ret;
}
early_initcall(brcmstb_soc_device_init);
