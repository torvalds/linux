/*
 * Copyright 2016-17 IBM Corp.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#define pr_fmt(fmt) "vas: " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/of_platform.h>
#include <linux/of_address.h>
#include <linux/of.h>
#include <asm/prom.h>

#include "vas.h"

DEFINE_MUTEX(vas_mutex);
static LIST_HEAD(vas_instances);

static DEFINE_PER_CPU(int, cpu_vas_id);

static int init_vas_instance(struct platform_device *pdev)
{
	int rc, cpu, vasid;
	struct resource *res;
	struct vas_instance *vinst;
	struct device_node *dn = pdev->dev.of_node;

	rc = of_property_read_u32(dn, "ibm,vas-id", &vasid);
	if (rc) {
		pr_err("No ibm,vas-id property for %s?\n", pdev->name);
		return -ENODEV;
	}

	if (pdev->num_resources != 4) {
		pr_err("Unexpected DT configuration for [%s, %d]\n",
				pdev->name, vasid);
		return -ENODEV;
	}

	vinst = kzalloc(sizeof(*vinst), GFP_KERNEL);
	if (!vinst)
		return -ENOMEM;

	INIT_LIST_HEAD(&vinst->node);
	ida_init(&vinst->ida);
	mutex_init(&vinst->mutex);
	vinst->vas_id = vasid;
	vinst->pdev = pdev;

	res = &pdev->resource[0];
	vinst->hvwc_bar_start = res->start;

	res = &pdev->resource[1];
	vinst->uwc_bar_start = res->start;

	res = &pdev->resource[2];
	vinst->paste_base_addr = res->start;

	res = &pdev->resource[3];
	if (res->end > 62) {
		pr_err("Bad 'paste_win_id_shift' in DT, %llx\n", res->end);
		goto free_vinst;
	}

	vinst->paste_win_id_shift = 63 - res->end;

	pr_devel("Initialized instance [%s, %d], paste_base 0x%llx, "
			"paste_win_id_shift 0x%llx\n", pdev->name, vasid,
			vinst->paste_base_addr, vinst->paste_win_id_shift);

	for_each_possible_cpu(cpu) {
		if (cpu_to_chip_id(cpu) == of_get_ibm_chip_id(dn))
			per_cpu(cpu_vas_id, cpu) = vasid;
	}

	mutex_lock(&vas_mutex);
	list_add(&vinst->node, &vas_instances);
	mutex_unlock(&vas_mutex);

	vas_instance_init_dbgdir(vinst);

	dev_set_drvdata(&pdev->dev, vinst);

	return 0;

free_vinst:
	kfree(vinst);
	return -ENODEV;

}

/*
 * Although this is read/used multiple times, it is written to only
 * during initialization.
 */
struct vas_instance *find_vas_instance(int vasid)
{
	struct list_head *ent;
	struct vas_instance *vinst;

	mutex_lock(&vas_mutex);

	if (vasid == -1)
		vasid = per_cpu(cpu_vas_id, smp_processor_id());

	list_for_each(ent, &vas_instances) {
		vinst = list_entry(ent, struct vas_instance, node);
		if (vinst->vas_id == vasid) {
			mutex_unlock(&vas_mutex);
			return vinst;
		}
	}
	mutex_unlock(&vas_mutex);

	pr_devel("Instance %d not found\n", vasid);
	return NULL;
}

int chip_to_vas_id(int chipid)
{
	int cpu;

	for_each_possible_cpu(cpu) {
		if (cpu_to_chip_id(cpu) == chipid)
			return per_cpu(cpu_vas_id, cpu);
	}
	return -1;
}
EXPORT_SYMBOL(chip_to_vas_id);

static int vas_probe(struct platform_device *pdev)
{
	return init_vas_instance(pdev);
}

static const struct of_device_id powernv_vas_match[] = {
	{ .compatible = "ibm,vas",},
	{},
};

static struct platform_driver vas_driver = {
	.driver = {
		.name = "vas",
		.of_match_table = powernv_vas_match,
	},
	.probe = vas_probe,
};

static int __init vas_init(void)
{
	int found = 0;
	struct device_node *dn;

	vas_init_dbgdir();

	platform_driver_register(&vas_driver);

	for_each_compatible_node(dn, NULL, "ibm,vas") {
		of_platform_device_create(dn, NULL, NULL);
		found++;
	}

	if (!found)
		return -ENODEV;

	pr_devel("Found %d instances\n", found);

	return 0;
}
device_initcall(vas_init);
