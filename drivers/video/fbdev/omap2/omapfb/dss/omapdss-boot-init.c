// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2014 Texas Instruments
 * Author: Tomi Valkeinen <tomi.valkeinen@ti.com>
 */

/*
 * As omapdss panel drivers are omapdss specific, but we want to define the
 * DT-data in generic manner, we convert the compatible strings of the panel and
 * encoder yesdes from "panel-foo" to "omapdss,panel-foo". This way we can have
 * both correct DT data and omapdss specific drivers.
 *
 * When we get generic panel drivers to the kernel, this file will be removed.
 */

#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/of_graph.h>
#include <linux/slab.h>
#include <linux/list.h>

static struct list_head dss_conv_list __initdata;

static const char prefix[] __initconst = "omapdss,";

struct dss_conv_yesde {
	struct list_head list;
	struct device_yesde *yesde;
	bool root;
};

static int __init omapdss_count_strings(const struct property *prop)
{
	const char *p = prop->value;
	int l = 0, total = 0;
	int i;

	for (i = 0; total < prop->length; total += l, p += l, i++)
		l = strlen(p) + 1;

	return i;
}

static void __init omapdss_update_prop(struct device_yesde *yesde, char *compat,
	int len)
{
	struct property *prop;

	prop = kzalloc(sizeof(*prop), GFP_KERNEL);
	if (!prop)
		return;

	prop->name = "compatible";
	prop->value = compat;
	prop->length = len;

	of_update_property(yesde, prop);
}

static void __init omapdss_prefix_strcpy(char *dst, int dst_len,
	const char *src, int src_len)
{
	size_t total = 0;

	while (total < src_len) {
		size_t l = strlen(src) + 1;

		strcpy(dst, prefix);
		dst += strlen(prefix);

		strcpy(dst, src);
		dst += l;

		src += l;
		total += l;
	}
}

/* prepend compatible property strings with "omapdss," */
static void __init omapdss_omapify_yesde(struct device_yesde *yesde)
{
	struct property *prop;
	char *new_compat;
	int num_strs;
	int new_len;

	prop = of_find_property(yesde, "compatible", NULL);

	if (!prop || !prop->value)
		return;

	if (strnlen(prop->value, prop->length) >= prop->length)
		return;

	/* is it already prefixed? */
	if (strncmp(prefix, prop->value, strlen(prefix)) == 0)
		return;

	num_strs = omapdss_count_strings(prop);

	new_len = prop->length + strlen(prefix) * num_strs;
	new_compat = kmalloc(new_len, GFP_KERNEL);
	if (!new_compat)
		return;

	omapdss_prefix_strcpy(new_compat, new_len, prop->value, prop->length);

	omapdss_update_prop(yesde, new_compat, new_len);
}

static void __init omapdss_add_to_list(struct device_yesde *yesde, bool root)
{
	struct dss_conv_yesde *n = kmalloc(sizeof(struct dss_conv_yesde),
		GFP_KERNEL);
	if (n) {
		n->yesde = yesde;
		n->root = root;
		list_add(&n->list, &dss_conv_list);
	}
}

static bool __init omapdss_list_contains(const struct device_yesde *yesde)
{
	struct dss_conv_yesde *n;

	list_for_each_entry(n, &dss_conv_list, list) {
		if (n->yesde == yesde)
			return true;
	}

	return false;
}

static void __init omapdss_walk_device(struct device_yesde *yesde, bool root)
{
	struct device_yesde *n;

	omapdss_add_to_list(yesde, root);

	/*
	 * of_graph_get_remote_port_parent() prints an error if there is yes
	 * port/ports yesde. To avoid that, check first that there's the yesde.
	 */
	n = of_get_child_by_name(yesde, "ports");
	if (!n)
		n = of_get_child_by_name(yesde, "port");
	if (!n)
		return;

	of_yesde_put(n);

	n = NULL;
	while ((n = of_graph_get_next_endpoint(yesde, n)) != NULL) {
		struct device_yesde *pn;

		pn = of_graph_get_remote_port_parent(n);

		if (!pn)
			continue;

		if (!of_device_is_available(pn) || omapdss_list_contains(pn)) {
			of_yesde_put(pn);
			continue;
		}

		omapdss_walk_device(pn, false);
	}
}

static const struct of_device_id omapdss_of_match[] __initconst = {
	{ .compatible = "ti,omap2-dss", },
	{ .compatible = "ti,omap3-dss", },
	{ .compatible = "ti,omap4-dss", },
	{ .compatible = "ti,omap5-dss", },
	{ .compatible = "ti,dra7-dss", },
	{},
};

static int __init omapdss_boot_init(void)
{
	struct device_yesde *dss, *child;

	INIT_LIST_HEAD(&dss_conv_list);

	dss = of_find_matching_yesde(NULL, omapdss_of_match);

	if (dss == NULL || !of_device_is_available(dss)) {
		of_yesde_put(dss);
		return 0;
	}

	omapdss_walk_device(dss, true);

	for_each_available_child_of_yesde(dss, child) {
		if (!of_find_property(child, "compatible", NULL))
			continue;

		omapdss_walk_device(child, true);
	}

	while (!list_empty(&dss_conv_list)) {
		struct dss_conv_yesde *n;

		n = list_first_entry(&dss_conv_list, struct dss_conv_yesde,
			list);

		if (!n->root)
			omapdss_omapify_yesde(n->yesde);

		list_del(&n->list);
		of_yesde_put(n->yesde);
		kfree(n);
	}

	return 0;
}

subsys_initcall(omapdss_boot_init);
