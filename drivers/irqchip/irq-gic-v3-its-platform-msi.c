/*
 * Copyright (C) 2013-2015 ARM Limited, All Rights Reserved.
 * Author: Marc Zyngier <marc.zyngier@arm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/device.h>
#include <linux/msi.h>
#include <linux/of.h>
#include <linux/of_irq.h>

static struct irq_chip its_pmsi_irq_chip = {
	.name			= "ITS-pMSI",
};

static int of_pmsi_get_dev_id(struct irq_domain *domain, struct device *dev,
				  u32 *dev_id)
{
	int ret, index = 0;

	/* Suck the DeviceID out of the msi-parent property */
	do {
		struct of_phandle_args args;

		ret = of_parse_phandle_with_args(dev->of_node,
						 "msi-parent", "#msi-cells",
						 index, &args);
		if (args.np == irq_domain_get_of_node(domain)) {
			if (WARN_ON(args.args_count != 1))
				return -EINVAL;
			*dev_id = args.args[0];
			break;
		}
	} while (!ret);

	return ret;
}

static int its_pmsi_prepare(struct irq_domain *domain, struct device *dev,
			    int nvec, msi_alloc_info_t *info)
{
	struct msi_domain_info *msi_info;
	u32 dev_id;
	int ret;

	msi_info = msi_get_domain_info(domain->parent);

	ret = of_pmsi_get_dev_id(domain, dev, &dev_id);
	if (ret)
		return ret;

	/* ITS specific DeviceID, as the core ITS ignores dev. */
	info->scratchpad[0].ul = dev_id;

	return msi_info->ops->msi_prepare(domain->parent,
					  dev, nvec, info);
}

static struct msi_domain_ops its_pmsi_ops = {
	.msi_prepare	= its_pmsi_prepare,
};

static struct msi_domain_info its_pmsi_domain_info = {
	.flags	= (MSI_FLAG_USE_DEF_DOM_OPS | MSI_FLAG_USE_DEF_CHIP_OPS),
	.ops	= &its_pmsi_ops,
	.chip	= &its_pmsi_irq_chip,
};

static struct of_device_id its_device_id[] = {
	{	.compatible	= "arm,gic-v3-its",	},
	{},
};

static int __init its_pmsi_init(void)
{
	struct device_node *np;
	struct irq_domain *parent;

	for (np = of_find_matching_node(NULL, its_device_id); np;
	     np = of_find_matching_node(np, its_device_id)) {
		if (!of_property_read_bool(np, "msi-controller"))
			continue;

		parent = irq_find_matching_host(np, DOMAIN_BUS_NEXUS);
		if (!parent || !msi_get_domain_info(parent)) {
			pr_err("%s: unable to locate ITS domain\n",
			       np->full_name);
			continue;
		}

		if (!platform_msi_create_irq_domain(of_node_to_fwnode(np),
						    &its_pmsi_domain_info,
						    parent)) {
			pr_err("%s: unable to create platform domain\n",
			       np->full_name);
			continue;
		}

		pr_info("Platform MSI: %s domain created\n", np->full_name);
	}

	return 0;
}
early_initcall(its_pmsi_init);
