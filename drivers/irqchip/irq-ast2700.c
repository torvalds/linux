// SPDX-License-Identifier: GPL-2.0-only
/*
 *  Aspeed AST2700 Interrupt Controller.
 *
 *  Copyright (C) 2026 ASPEED Technology Inc.
 */
#include "irq-ast2700.h"

#define ASPEED_INTC_RANGE_FIXED_CELLS	3U
#define ASPEED_INTC_RANGE_OFF_START	0U
#define ASPEED_INTC_RANGE_OFF_COUNT	1U
#define ASPEED_INTC_RANGE_OFF_PHANDLE	2U

/**
 * aspeed_intc_populate_ranges
 * @dev: Device owning the interrupt controller node.
 * @ranges: Destination for parsed range descriptors.
 *
 * Return: 0 on success, negative errno on error.
 */
int aspeed_intc_populate_ranges(struct device *dev,
				struct aspeed_intc_interrupt_ranges *ranges)
{
	struct aspeed_intc_interrupt_range *arr;
	const __be32 *pvs, *pve;
	struct device_node *dn;
	int len;

	if (!dev || !ranges)
		return -EINVAL;

	dn = dev->of_node;

	pvs = of_get_property(dn, "aspeed,interrupt-ranges", &len);
	if (!pvs)
		return -EINVAL;

	if (len % sizeof(__be32))
		return -EINVAL;

	/* Over-estimate the range entry count for now */
	ranges->ranges = devm_kmalloc_array(dev,
					    len / (ASPEED_INTC_RANGE_FIXED_CELLS * sizeof(__be32)),
					    sizeof(*ranges->ranges),
					    GFP_KERNEL);
	if (!ranges->ranges)
		return -ENOMEM;

	pve = pvs + (len / sizeof(__be32));
	for (unsigned int i = 0; pve - pvs >= ASPEED_INTC_RANGE_FIXED_CELLS; i++) {
		struct aspeed_intc_interrupt_range *r;
		struct device_node *target;
		u32 target_cells;

		target = of_find_node_by_phandle(be32_to_cpu(pvs[ASPEED_INTC_RANGE_OFF_PHANDLE]));
		if (!target)
			return -EINVAL;

		if (of_property_read_u32(target, "#interrupt-cells",
					 &target_cells)) {
			of_node_put(target);
			return -EINVAL;
		}

		if (!target_cells || target_cells > IRQ_DOMAIN_IRQ_SPEC_PARAMS) {
			of_node_put(target);
			return -EINVAL;
		}

		if (pve - pvs < ASPEED_INTC_RANGE_FIXED_CELLS + target_cells) {
			of_node_put(target);
			return -EINVAL;
		}

		r = &ranges->ranges[i];
		r->start = be32_to_cpu(pvs[ASPEED_INTC_RANGE_OFF_START]);
		r->count = be32_to_cpu(pvs[ASPEED_INTC_RANGE_OFF_COUNT]);

		{
			struct of_phandle_args args = {
				.np = target,
				.args_count = target_cells,
			};

			for (u32 j = 0; j < target_cells; j++)
				args.args[j] = be32_to_cpu(pvs[ASPEED_INTC_RANGE_FIXED_CELLS + j]);

			of_phandle_args_to_fwspec(target, args.args,
						  args.args_count,
						  &r->upstream);
		}

		of_node_put(target);
		r->domain = irq_find_matching_fwspec(&r->upstream, DOMAIN_BUS_ANY);
		pvs += ASPEED_INTC_RANGE_FIXED_CELLS + target_cells;
		ranges->nranges++;
	}

	/* Re-fit the range array now we know the entry count */
	arr = devm_krealloc_array(dev, ranges->ranges, ranges->nranges,
				  sizeof(*ranges->ranges), GFP_KERNEL);
	if (!arr)
		return -ENOMEM;
	ranges->ranges = arr;

	return 0;
}
