// SPDX-License-Identifier: GPL-2.0-only
/*
 * PM domains for CPUs via genpd.
 *
 * Copyright (C) 2019 Linaro Ltd.
 * Author: Ulf Hansson <ulf.hansson@linaro.org>
 *
 * Copyright (c) 2021 Western Digital Corporation or its affiliates.
 * Copyright (c) 2022 Ventana Micro Systems Inc.
 */

#define pr_fmt(fmt) "dt-idle-genpd: " fmt

#include <linux/cpu.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/pm_domain.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/string.h>

#include "dt_idle_genpd.h"

static int pd_parse_state_nodes(
			int (*parse_state)(struct device_node *, u32 *),
			struct genpd_power_state *states, int state_count)
{
	int i, ret;
	u32 state, *state_buf;

	for (i = 0; i < state_count; i++) {
		ret = parse_state(to_of_node(states[i].fwnode), &state);
		if (ret)
			goto free_state;

		state_buf = kmalloc(sizeof(u32), GFP_KERNEL);
		if (!state_buf) {
			ret = -ENOMEM;
			goto free_state;
		}
		*state_buf = state;
		states[i].data = state_buf;
	}

	return 0;

free_state:
	i--;
	for (; i >= 0; i--)
		kfree(states[i].data);
	return ret;
}

static int pd_parse_states(struct device_node *np,
			   int (*parse_state)(struct device_node *, u32 *),
			   struct genpd_power_state **states,
			   int *state_count)
{
	int ret;

	/* Parse the domain idle states. */
	ret = of_genpd_parse_idle_states(np, states, state_count);
	if (ret)
		return ret;

	/* Fill out the dt specifics for each found state. */
	ret = pd_parse_state_nodes(parse_state, *states, *state_count);
	if (ret)
		kfree(*states);

	return ret;
}

static void pd_free_states(struct genpd_power_state *states,
			    unsigned int state_count)
{
	int i;

	for (i = 0; i < state_count; i++)
		kfree(states[i].data);
	kfree(states);
}

void dt_idle_pd_free(struct generic_pm_domain *pd)
{
	pd_free_states(pd->states, pd->state_count);
	kfree(pd->name);
	kfree(pd);
}

struct generic_pm_domain *dt_idle_pd_alloc(struct device_node *np,
			int (*parse_state)(struct device_node *, u32 *))
{
	struct generic_pm_domain *pd;
	struct genpd_power_state *states = NULL;
	int ret, state_count = 0;

	pd = kzalloc(sizeof(*pd), GFP_KERNEL);
	if (!pd)
		goto out;

	pd->name = kasprintf(GFP_KERNEL, "%pOF", np);
	if (!pd->name)
		goto free_pd;

	/*
	 * Parse the domain idle states and let genpd manage the state selection
	 * for those being compatible with "domain-idle-state".
	 */
	ret = pd_parse_states(np, parse_state, &states, &state_count);
	if (ret)
		goto free_name;

	pd->free_states = pd_free_states;
	pd->name = kbasename(pd->name);
	pd->states = states;
	pd->state_count = state_count;

	pr_debug("alloc PM domain %s\n", pd->name);
	return pd;

free_name:
	kfree(pd->name);
free_pd:
	kfree(pd);
out:
	pr_err("failed to alloc PM domain %pOF\n", np);
	return NULL;
}

int dt_idle_pd_init_topology(struct device_node *np)
{
	struct device_node *node;
	struct of_phandle_args child, parent;
	int ret;

	for_each_child_of_node(np, node) {
		if (of_parse_phandle_with_args(node, "power-domains",
					"#power-domain-cells", 0, &parent))
			continue;

		child.np = node;
		child.args_count = 0;
		ret = of_genpd_add_subdomain(&parent, &child);
		of_node_put(parent.np);
		if (ret) {
			of_node_put(node);
			return ret;
		}
	}

	return 0;
}

int dt_idle_pd_remove_topology(struct device_node *np)
{
	struct device_node *node;
	struct of_phandle_args child, parent;
	int ret;

	for_each_child_of_node(np, node) {
		if (of_parse_phandle_with_args(node, "power-domains",
					"#power-domain-cells", 0, &parent))
			continue;

		child.np = node;
		child.args_count = 0;
		ret = of_genpd_remove_subdomain(&parent, &child);
		of_node_put(parent.np);
		if (ret) {
			of_node_put(node);
			return ret;
		}
	}

	return 0;
}

struct device *dt_idle_attach_cpu(int cpu, const char *name)
{
	struct device *dev;

	dev = dev_pm_domain_attach_by_name(get_cpu_device(cpu), name);
	if (IS_ERR_OR_NULL(dev))
		return dev;

	pm_runtime_irq_safe(dev);
	if (cpu_online(cpu))
		pm_runtime_get_sync(dev);

	dev_pm_syscore_device(dev, true);

	return dev;
}

void dt_idle_detach_cpu(struct device *dev)
{
	if (IS_ERR_OR_NULL(dev))
		return;

	dev_pm_domain_detach(dev, false);
}
