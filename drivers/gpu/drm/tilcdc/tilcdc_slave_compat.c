/*
 * Copyright (C) 2015 Texas Instruments
 * Author: Jyri Sarha <jsarha@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 */

/*
 * To support the old "ti,tilcdc,slave" binding the binding has to be
 * transformed to the new external encoder binding.
 */

#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/of_graph.h>
#include <linux/of_fdt.h>
#include <linux/slab.h>
#include <linux/list.h>

#include "tilcdc_slave_compat.h"

struct kfree_table {
	int total;
	int num;
	void **table;
};

static int __init kfree_table_init(struct kfree_table *kft)
{
	kft->total = 32;
	kft->num = 0;
	kft->table = kmalloc(kft->total * sizeof(*kft->table),
			     GFP_KERNEL);
	if (!kft->table)
		return -ENOMEM;

	return 0;
}

static int __init kfree_table_add(struct kfree_table *kft, void *p)
{
	if (kft->num == kft->total) {
		void **old = kft->table;

		kft->total *= 2;
		kft->table = krealloc(old, kft->total * sizeof(*kft->table),
				      GFP_KERNEL);
		if (!kft->table) {
			kft->table = old;
			kfree(p);
			return -ENOMEM;
		}
	}
	kft->table[kft->num++] = p;
	return 0;
}

static void __init kfree_table_free(struct kfree_table *kft)
{
	int i;

	for (i = 0; i < kft->num; i++)
		kfree(kft->table[i]);

	kfree(kft->table);
}

static
struct property * __init tilcdc_prop_dup(const struct property *prop,
					 struct kfree_table *kft)
{
	struct property *nprop;

	nprop = kzalloc(sizeof(*nprop), GFP_KERNEL);
	if (!nprop || kfree_table_add(kft, nprop))
		return NULL;

	nprop->name = kstrdup(prop->name, GFP_KERNEL);
	if (!nprop->name || kfree_table_add(kft, nprop->name))
		return NULL;

	nprop->value = kmemdup(prop->value, prop->length, GFP_KERNEL);
	if (!nprop->value || kfree_table_add(kft, nprop->value))
		return NULL;

	nprop->length = prop->length;

	return nprop;
}

static void __init tilcdc_copy_props(struct device_node *from,
				     struct device_node *to,
				     const char * const props[],
				     struct kfree_table *kft)
{
	struct property *prop;
	int i;

	for (i = 0; props[i]; i++) {
		prop = of_find_property(from, props[i], NULL);
		if (!prop)
			continue;

		prop = tilcdc_prop_dup(prop, kft);
		if (!prop)
			continue;

		prop->next = to->properties;
		to->properties = prop;
	}
}

static int __init tilcdc_prop_str_update(struct property *prop,
					  const char *str,
					  struct kfree_table *kft)
{
	prop->value = kstrdup(str, GFP_KERNEL);
	if (kfree_table_add(kft, prop->value) || !prop->value)
		return -ENOMEM;
	prop->length = strlen(str)+1;
	return 0;
}

static void __init tilcdc_node_disable(struct device_node *node)
{
	struct property *prop;

	prop = kzalloc(sizeof(*prop), GFP_KERNEL);
	if (!prop)
		return;

	prop->name = "status";
	prop->value = "disabled";
	prop->length = strlen((char *)prop->value)+1;

	of_update_property(node, prop);
}

struct device_node * __init tilcdc_get_overlay(struct kfree_table *kft)
{
	const int size = __dtb_tilcdc_slave_compat_end -
		__dtb_tilcdc_slave_compat_begin;
	static void *overlay_data;
	struct device_node *overlay;
	int ret;

	if (!size) {
		pr_warn("%s: No overlay data\n", __func__);
		return NULL;
	}

	overlay_data = kmemdup(__dtb_tilcdc_slave_compat_begin,
			       size, GFP_KERNEL);
	if (!overlay_data || kfree_table_add(kft, overlay_data))
		return NULL;

	of_fdt_unflatten_tree(overlay_data, &overlay);
	if (!overlay) {
		pr_warn("%s: Unfattening overlay tree failed\n", __func__);
		return NULL;
	}

	of_node_set_flag(overlay, OF_DETACHED);
	ret = of_resolve_phandles(overlay);
	if (ret) {
		pr_err("%s: Failed to resolve phandles: %d\n", __func__, ret);
		return NULL;
	}

	return overlay;
}

static const struct of_device_id tilcdc_slave_of_match[] __initconst = {
	{ .compatible = "ti,tilcdc,slave", },
	{},
};

static const struct of_device_id tilcdc_of_match[] __initconst = {
	{ .compatible = "ti,am33xx-tilcdc", },
	{},
};

static const struct of_device_id tilcdc_tda998x_of_match[] __initconst = {
	{ .compatible = "nxp,tda998x", },
	{},
};

static const char * const tilcdc_slave_props[] __initconst = {
	"pinctrl-names",
	"pinctrl-0",
	"pinctrl-1",
	NULL
};

void __init tilcdc_convert_slave_node(void)
{
	struct device_node *slave = NULL, *lcdc = NULL;
	struct device_node *i2c = NULL, *fragment = NULL;
	struct device_node *overlay, *encoder;
	struct property *prop;
	/* For all memory needed for the overlay tree. This memory can
	   be freed after the overlay has been applied. */
	struct kfree_table kft;
	int ret;

	if (kfree_table_init(&kft))
		goto out;

	lcdc = of_find_matching_node(NULL, tilcdc_of_match);
	slave = of_find_matching_node(NULL, tilcdc_slave_of_match);

	if (!slave || !of_device_is_available(lcdc))
		goto out;

	i2c = of_parse_phandle(slave, "i2c", 0);
	if (!i2c) {
		pr_err("%s: Can't find i2c node trough phandle\n", __func__);
		goto out;
	}

	overlay = tilcdc_get_overlay(&kft);
	if (!overlay)
		goto out;

	encoder = of_find_matching_node(overlay, tilcdc_tda998x_of_match);
	if (!encoder) {
		pr_err("%s: Failed to find tda998x node\n", __func__);
		goto out;
	}

	tilcdc_copy_props(slave, encoder, tilcdc_slave_props, &kft);

	for_each_child_of_node(overlay, fragment) {
		prop = of_find_property(fragment, "target-path", NULL);
		if (!prop)
			continue;
		if (!strncmp("i2c", (char *)prop->value, prop->length))
			if (tilcdc_prop_str_update(prop, i2c->full_name, &kft))
				goto out;
		if (!strncmp("lcdc", (char *)prop->value, prop->length))
			if (tilcdc_prop_str_update(prop, lcdc->full_name, &kft))
				goto out;
	}

	tilcdc_node_disable(slave);

	ret = of_overlay_create(overlay);
	if (ret)
		pr_err("%s: Creating overlay failed: %d\n", __func__, ret);
	else
		pr_info("%s: ti,tilcdc,slave node successfully converted\n",
			__func__);
out:
	kfree_table_free(&kft);
	of_node_put(i2c);
	of_node_put(slave);
	of_node_put(lcdc);
	of_node_put(fragment);
}

int __init tilcdc_slave_compat_init(void)
{
	tilcdc_convert_slave_node();
	return 0;
}

subsys_initcall(tilcdc_slave_compat_init);
