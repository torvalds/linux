// SPDX-License-Identifier: ISC
/*
 * Copyright (c) 2014 Broadcom Corporation
 */
#include <linux/init.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_net.h>
#include <linux/clk.h>

#include <defs.h>
#include "debug.h"
#include "core.h"
#include "common.h"
#include "of.h"

static int brcmf_of_get_country_codes(struct device *dev,
				      struct brcmf_mp_device *settings)
{
	struct device_node *np = dev->of_node;
	struct brcmfmac_pd_cc_entry *cce;
	struct brcmfmac_pd_cc *cc;
	int count;
	int i;

	count = of_property_count_strings(np, "brcm,ccode-map");
	if (count < 0) {
		/* If no explicit country code map is specified, check whether
		 * the trivial map should be used.
		 */
		settings->trivial_ccode_map =
			of_property_read_bool(np, "brcm,ccode-map-trivial");

		/* The property is optional, so return success if it doesn't
		 * exist. Otherwise propagate the error code.
		 */
		return (count == -EINVAL) ? 0 : count;
	}

	cc = devm_kzalloc(dev, struct_size(cc, table, count), GFP_KERNEL);
	if (!cc)
		return -ENOMEM;

	cc->table_size = count;

	for (i = 0; i < count; i++) {
		const char *map;

		cce = &cc->table[i];

		if (of_property_read_string_index(np, "brcm,ccode-map",
						  i, &map))
			continue;

		/* String format e.g. US-Q2-86 */
		if (sscanf(map, "%2c-%2c-%d", cce->iso3166, cce->cc,
			   &cce->rev) != 3)
			brcmf_err("failed to read country map %s\n", map);
		else
			brcmf_dbg(INFO, "%s-%s-%d\n", cce->iso3166, cce->cc,
				  cce->rev);
	}

	settings->country_codes = cc;

	return 0;
}

int brcmf_of_probe(struct device *dev, enum brcmf_bus_type bus_type,
		   struct brcmf_mp_device *settings)
{
	struct brcmfmac_sdio_pd *sdio = &settings->bus.sdio;
	struct device_node *root, *np = dev->of_node;
	struct of_phandle_args oirq;
	struct clk *clk;
	const char *prop;
	int irq;
	int err;
	u32 irqf;
	u32 val;

	/* Apple ARM64 platforms have their own idea of board type, passed in
	 * via the device tree. They also have an antenna SKU parameter
	 */
	err = of_property_read_string(np, "brcm,board-type", &prop);
	if (!err)
		settings->board_type = prop;

	if (!of_property_read_string(np, "apple,antenna-sku", &prop))
		settings->antenna_sku = prop;

	/* The WLAN calibration blob is normally stored in SROM, but Apple
	 * ARM64 platforms pass it via the DT instead.
	 */
	prop = of_get_property(np, "brcm,cal-blob", &settings->cal_size);
	if (prop && settings->cal_size)
		settings->cal_blob = prop;

	/* Set board-type to the first string of the machine compatible prop */
	root = of_find_node_by_path("/");
	if (root && err) {
		char *board_type;
		const char *tmp;

		of_property_read_string_index(root, "compatible", 0, &tmp);

		/* get rid of '/' in the compatible string to be able to find the FW */
		board_type = devm_kstrdup(dev, tmp, GFP_KERNEL);
		if (!board_type) {
			of_node_put(root);
			return 0;
		}
		strreplace(board_type, '/', '-');
		settings->board_type = board_type;
	}
	of_node_put(root);

	clk = devm_clk_get_optional_enabled_with_rate(dev, "lpo", 32768);
	if (IS_ERR(clk))
		return PTR_ERR(clk);

	brcmf_dbg(INFO, "%s LPO clock\n", clk ? "enable" : "no");

	if (!np || !of_device_is_compatible(np, "brcm,bcm4329-fmac"))
		return 0;

	err = brcmf_of_get_country_codes(dev, settings);
	if (err)
		brcmf_err("failed to get OF country code map (err=%d)\n", err);

	of_get_mac_address(np, settings->mac);

	if (bus_type != BRCMF_BUSTYPE_SDIO)
		return 0;

	if (of_property_read_u32(np, "brcm,drive-strength", &val) == 0)
		sdio->drive_strength = val;

	/* make sure there are interrupts defined in the node */
	if (of_irq_parse_one(np, 0, &oirq))
		return 0;

	irq = irq_create_of_mapping(&oirq);
	if (!irq) {
		brcmf_err("interrupt could not be mapped\n");
		return 0;
	}
	irqf = irq_get_trigger_type(irq);

	sdio->oob_irq_supported = true;
	sdio->oob_irq_nr = irq;
	sdio->oob_irq_flags = irqf;

	return 0;
}
