/*
 * l2 cache initialization for CSR SiRFprimaII
 *
 * Copyright (c) 2011 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <asm/hardware/cache-l2x0.h>

struct l2x0_aux
{
	u32 val;
	u32 mask;
};

static struct l2x0_aux prima2_l2x0_aux __initconst = {
	.val = 2 << L2X0_AUX_CTRL_WAY_SIZE_SHIFT,
	.mask =	0,
};

static struct l2x0_aux marco_l2x0_aux __initconst = {
	.val = (2 << L2X0_AUX_CTRL_WAY_SIZE_SHIFT) |
		(1 << L2X0_AUX_CTRL_ASSOCIATIVITY_SHIFT),
	.mask = L2X0_AUX_CTRL_MASK,
};

static struct of_device_id sirf_l2x0_ids[] __initconst = {
	{ .compatible = "sirf,prima2-pl310-cache", .data = &prima2_l2x0_aux, },
	{ .compatible = "sirf,marco-pl310-cache", .data = &marco_l2x0_aux, },
	{},
};

static int __init sirfsoc_l2x0_init(void)
{
	struct device_node *np;
	const struct l2x0_aux *aux;

	np = of_find_matching_node(NULL, sirf_l2x0_ids);
	if (np) {
		aux = of_match_node(sirf_l2x0_ids, np)->data;
		return l2x0_of_init(aux->val, aux->mask);
	}

	return 0;
}
early_initcall(sirfsoc_l2x0_init);
