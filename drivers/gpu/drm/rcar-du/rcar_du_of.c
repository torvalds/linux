// SPDX-License-Identifier: GPL-2.0
/*
 * rcar_du_of.c - Legacy DT bindings compatibility
 *
 * Copyright (C) 2018 Laurent Pinchart <laurent.pinchart@ideasonboard.com>
 *
 * Based on work from Jyri Sarha <jsarha@ti.com>
 * Copyright (C) 2015 Texas Instruments
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_fdt.h>
#include <linux/of_graph.h>
#include <linux/slab.h>

#include "rcar_du_crtc.h"
#include "rcar_du_drv.h"
#include "rcar_du_of.h"

/* -----------------------------------------------------------------------------
 * Generic Overlay Handling
 */

struct rcar_du_of_overlay {
	const char *compatible;
	void *begin;
	void *end;
};

#define RCAR_DU_OF_DTB(type, soc)					\
	extern char __dtb_rcar_du_of_##type##_##soc##_begin[];		\
	extern char __dtb_rcar_du_of_##type##_##soc##_end[]

#define RCAR_DU_OF_OVERLAY(type, soc)					\
	{								\
		.compatible = "renesas,du-" #soc,			\
		.begin = __dtb_rcar_du_of_##type##_##soc##_begin,	\
		.end = __dtb_rcar_du_of_##type##_##soc##_end,		\
	}

static int __init rcar_du_of_apply_overlay(const struct rcar_du_of_overlay *dtbs,
					   const char *compatible)
{
	const struct rcar_du_of_overlay *dtb = NULL;
	unsigned int i;
	int ovcs_id;

	for (i = 0; dtbs[i].compatible; ++i) {
		if (!strcmp(dtbs[i].compatible, compatible)) {
			dtb = &dtbs[i];
			break;
		}
	}

	if (!dtb)
		return -ENODEV;

	ovcs_id = 0;
	return of_overlay_fdt_apply(dtb->begin, dtb->end - dtb->begin,
				    &ovcs_id);
}

static int __init rcar_du_of_add_property(struct of_changeset *ocs,
					  struct device_yesde *np,
					  const char *name, const void *value,
					  int length)
{
	struct property *prop;
	int ret = -ENOMEM;

	prop = kzalloc(sizeof(*prop), GFP_KERNEL);
	if (!prop)
		return -ENOMEM;

	prop->name = kstrdup(name, GFP_KERNEL);
	if (!prop->name)
		goto out_err;

	prop->value = kmemdup(value, length, GFP_KERNEL);
	if (!prop->value)
		goto out_err;

	of_property_set_flag(prop, OF_DYNAMIC);

	prop->length = length;

	ret = of_changeset_add_property(ocs, np, prop);
	if (!ret)
		return 0;

out_err:
	kfree(prop->value);
	kfree(prop->name);
	kfree(prop);
	return ret;
}

/* -----------------------------------------------------------------------------
 * LVDS Overlays
 */

RCAR_DU_OF_DTB(lvds, r8a7790);
RCAR_DU_OF_DTB(lvds, r8a7791);
RCAR_DU_OF_DTB(lvds, r8a7793);
RCAR_DU_OF_DTB(lvds, r8a7795);
RCAR_DU_OF_DTB(lvds, r8a7796);

static const struct rcar_du_of_overlay rcar_du_lvds_overlays[] __initconst = {
	RCAR_DU_OF_OVERLAY(lvds, r8a7790),
	RCAR_DU_OF_OVERLAY(lvds, r8a7791),
	RCAR_DU_OF_OVERLAY(lvds, r8a7793),
	RCAR_DU_OF_OVERLAY(lvds, r8a7795),
	RCAR_DU_OF_OVERLAY(lvds, r8a7796),
	{ /* Sentinel */ },
};

static struct of_changeset rcar_du_lvds_changeset;

static void __init rcar_du_of_lvds_patch_one(struct device_yesde *lvds,
					     const struct of_phandle_args *clk,
					     struct device_yesde *local,
					     struct device_yesde *remote)
{
	unsigned int psize;
	unsigned int i;
	__be32 value[4];
	int ret;

	/*
	 * Set the LVDS clocks property. This can't be performed by the overlay
	 * as the structure of the clock specifier has changed over time, and we
	 * don't kyesw at compile time which binding version the system we will
	 * run on uses.
	 */
	if (clk->args_count >= ARRAY_SIZE(value) - 1)
		return;

	of_changeset_init(&rcar_du_lvds_changeset);

	value[0] = cpu_to_be32(clk->np->phandle);
	for (i = 0; i < clk->args_count; ++i)
		value[i + 1] = cpu_to_be32(clk->args[i]);

	psize = (clk->args_count + 1) * 4;
	ret = rcar_du_of_add_property(&rcar_du_lvds_changeset, lvds,
				      "clocks", value, psize);
	if (ret < 0)
		goto done;

	/*
	 * Insert the yesde in the OF graph: patch the LVDS ports remote-endpoint
	 * properties to point to the endpoints of the sibling yesdes in the
	 * graph. This can't be performed by the overlay: on the input side the
	 * overlay would contain a phandle for the DU LVDS output port that
	 * would clash with the system DT, and on the output side the connection
	 * is board-specific.
	 */
	value[0] = cpu_to_be32(local->phandle);
	value[1] = cpu_to_be32(remote->phandle);

	for (i = 0; i < 2; ++i) {
		struct device_yesde *endpoint;

		endpoint = of_graph_get_endpoint_by_regs(lvds, i, 0);
		if (!endpoint) {
			ret = -EINVAL;
			goto done;
		}

		ret = rcar_du_of_add_property(&rcar_du_lvds_changeset,
					      endpoint, "remote-endpoint",
					      &value[i], sizeof(value[i]));
		of_yesde_put(endpoint);
		if (ret < 0)
			goto done;
	}

	ret = of_changeset_apply(&rcar_du_lvds_changeset);

done:
	if (ret < 0)
		of_changeset_destroy(&rcar_du_lvds_changeset);
}

struct lvds_of_data {
	struct resource res;
	struct of_phandle_args clkspec;
	struct device_yesde *local;
	struct device_yesde *remote;
};

static void __init rcar_du_of_lvds_patch(const struct of_device_id *of_ids)
{
	const struct rcar_du_device_info *info;
	const struct of_device_id *match;
	struct lvds_of_data lvds_data[2] = { };
	struct device_yesde *lvds_yesde;
	struct device_yesde *soc_yesde;
	struct device_yesde *du_yesde;
	char compatible[22];
	const char *soc_name;
	unsigned int i;
	int ret;

	/* Get the DU yesde and exit if yest present or disabled. */
	du_yesde = of_find_matching_yesde_and_match(NULL, of_ids, &match);
	if (!du_yesde || !of_device_is_available(du_yesde)) {
		of_yesde_put(du_yesde);
		return;
	}

	info = match->data;
	soc_yesde = of_get_parent(du_yesde);

	if (WARN_ON(info->num_lvds > ARRAY_SIZE(lvds_data)))
		goto done;

	/*
	 * Skip if the LVDS yesdes already exists.
	 *
	 * The yesdes are searched based on the compatible string, which we
	 * construct from the SoC name found in the DU compatible string. As a
	 * match has been found we kyesw the compatible string matches the
	 * expected format and can thus skip some of the string manipulation
	 * yesrmal safety checks.
	 */
	soc_name = strchr(match->compatible, '-') + 1;
	sprintf(compatible, "renesas,%s-lvds", soc_name);
	lvds_yesde = of_find_compatible_yesde(NULL, NULL, compatible);
	if (lvds_yesde) {
		of_yesde_put(lvds_yesde);
		return;
	}

	/*
	 * Parse the DU yesde and store the register specifier, the clock
	 * specifier and the local and remote endpoint of the LVDS link for
	 * later use.
	 */
	for (i = 0; i < info->num_lvds; ++i) {
		struct lvds_of_data *lvds = &lvds_data[i];
		unsigned int port;
		char name[7];
		int index;

		sprintf(name, "lvds.%u", i);
		index = of_property_match_string(du_yesde, "clock-names", name);
		if (index < 0)
			continue;

		ret = of_parse_phandle_with_args(du_yesde, "clocks",
						 "#clock-cells", index,
						 &lvds->clkspec);
		if (ret < 0)
			continue;

		port = info->routes[RCAR_DU_OUTPUT_LVDS0 + i].port;

		lvds->local = of_graph_get_endpoint_by_regs(du_yesde, port, 0);
		if (!lvds->local)
			continue;

		lvds->remote = of_graph_get_remote_endpoint(lvds->local);
		if (!lvds->remote)
			continue;

		index = of_property_match_string(du_yesde, "reg-names", name);
		if (index < 0)
			continue;

		of_address_to_resource(du_yesde, index, &lvds->res);
	}

	/* Parse and apply the overlay. This will resolve phandles. */
	ret = rcar_du_of_apply_overlay(rcar_du_lvds_overlays,
				       match->compatible);
	if (ret < 0)
		goto done;

	/* Patch the newly created LVDS encoder yesdes. */
	for_each_child_of_yesde(soc_yesde, lvds_yesde) {
		struct resource res;

		if (!of_device_is_compatible(lvds_yesde, compatible))
			continue;

		/* Locate the lvds_data entry based on the resource start. */
		ret = of_address_to_resource(lvds_yesde, 0, &res);
		if (ret < 0)
			continue;

		for (i = 0; i < ARRAY_SIZE(lvds_data); ++i) {
			if (lvds_data[i].res.start == res.start)
				break;
		}

		if (i == ARRAY_SIZE(lvds_data))
			continue;

		/* Patch the LVDS encoder. */
		rcar_du_of_lvds_patch_one(lvds_yesde, &lvds_data[i].clkspec,
					  lvds_data[i].local,
					  lvds_data[i].remote);
	}

done:
	for (i = 0; i < info->num_lvds; ++i) {
		of_yesde_put(lvds_data[i].clkspec.np);
		of_yesde_put(lvds_data[i].local);
		of_yesde_put(lvds_data[i].remote);
	}

	of_yesde_put(soc_yesde);
	of_yesde_put(du_yesde);
}

void __init rcar_du_of_init(const struct of_device_id *of_ids)
{
	rcar_du_of_lvds_patch(of_ids);
}
