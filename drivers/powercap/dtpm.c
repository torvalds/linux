// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2020 Linaro Limited
 *
 * Author: Daniel Lezcano <daniel.lezcano@linaro.org>
 *
 * The powercap based Dynamic Thermal Power Management framework
 * provides to the userspace a consistent API to set the power limit
 * on some devices.
 *
 * DTPM defines the functions to create a tree of constraints. Each
 * parent node is a virtual description of the aggregation of the
 * children. It propagates the constraints set at its level to its
 * children and collect the children power information. The leaves of
 * the tree are the real devices which have the ability to get their
 * current power consumption and set their power limit.
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/dtpm.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/powercap.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/of.h>

#include "dtpm_subsys.h"

#define DTPM_POWER_LIMIT_FLAG 0

static const char *constraint_name[] = {
	"Instantaneous",
};

static DEFINE_MUTEX(dtpm_lock);
static struct powercap_control_type *pct;
static struct dtpm *root;

static int get_time_window_us(struct powercap_zone *pcz, int cid, u64 *window)
{
	return -ENOSYS;
}

static int set_time_window_us(struct powercap_zone *pcz, int cid, u64 window)
{
	return -ENOSYS;
}

static int get_max_power_range_uw(struct powercap_zone *pcz, u64 *max_power_uw)
{
	struct dtpm *dtpm = to_dtpm(pcz);

	*max_power_uw = dtpm->power_max - dtpm->power_min;

	return 0;
}

static int __get_power_uw(struct dtpm *dtpm, u64 *power_uw)
{
	struct dtpm *child;
	u64 power;
	int ret = 0;

	if (dtpm->ops) {
		*power_uw = dtpm->ops->get_power_uw(dtpm);
		return 0;
	}

	*power_uw = 0;

	list_for_each_entry(child, &dtpm->children, sibling) {
		ret = __get_power_uw(child, &power);
		if (ret)
			break;
		*power_uw += power;
	}

	return ret;
}

static int get_power_uw(struct powercap_zone *pcz, u64 *power_uw)
{
	return __get_power_uw(to_dtpm(pcz), power_uw);
}

static void __dtpm_rebalance_weight(struct dtpm *dtpm)
{
	struct dtpm *child;

	list_for_each_entry(child, &dtpm->children, sibling) {

		pr_debug("Setting weight '%d' for '%s'\n",
			 child->weight, child->zone.name);

		child->weight = DIV64_U64_ROUND_CLOSEST(
			child->power_max * 1024, dtpm->power_max);

		__dtpm_rebalance_weight(child);
	}
}

static void __dtpm_sub_power(struct dtpm *dtpm)
{
	struct dtpm *parent = dtpm->parent;

	while (parent) {
		parent->power_min -= dtpm->power_min;
		parent->power_max -= dtpm->power_max;
		parent->power_limit -= dtpm->power_limit;
		parent = parent->parent;
	}
}

static void __dtpm_add_power(struct dtpm *dtpm)
{
	struct dtpm *parent = dtpm->parent;

	while (parent) {
		parent->power_min += dtpm->power_min;
		parent->power_max += dtpm->power_max;
		parent->power_limit += dtpm->power_limit;
		parent = parent->parent;
	}
}

/**
 * dtpm_update_power - Update the power on the dtpm
 * @dtpm: a pointer to a dtpm structure to update
 *
 * Function to update the power values of the dtpm node specified in
 * parameter. These new values will be propagated to the tree.
 *
 * Return: zero on success, -EINVAL if the values are inconsistent
 */
int dtpm_update_power(struct dtpm *dtpm)
{
	int ret;

	__dtpm_sub_power(dtpm);

	ret = dtpm->ops->update_power_uw(dtpm);
	if (ret)
		pr_err("Failed to update power for '%s': %d\n",
		       dtpm->zone.name, ret);

	if (!test_bit(DTPM_POWER_LIMIT_FLAG, &dtpm->flags))
		dtpm->power_limit = dtpm->power_max;

	__dtpm_add_power(dtpm);

	if (root)
		__dtpm_rebalance_weight(root);

	return ret;
}

/**
 * dtpm_release_zone - Cleanup when the node is released
 * @pcz: a pointer to a powercap_zone structure
 *
 * Do some housecleaning and update the weight on the tree. The
 * release will be denied if the node has children. This function must
 * be called by the specific release callback of the different
 * backends.
 *
 * Return: 0 on success, -EBUSY if there are children
 */
int dtpm_release_zone(struct powercap_zone *pcz)
{
	struct dtpm *dtpm = to_dtpm(pcz);
	struct dtpm *parent = dtpm->parent;

	if (!list_empty(&dtpm->children))
		return -EBUSY;

	if (parent)
		list_del(&dtpm->sibling);

	__dtpm_sub_power(dtpm);

	if (dtpm->ops)
		dtpm->ops->release(dtpm);
	else
		kfree(dtpm);

	return 0;
}

static int get_power_limit_uw(struct powercap_zone *pcz,
			      int cid, u64 *power_limit)
{
	*power_limit = to_dtpm(pcz)->power_limit;
	
	return 0;
}

/*
 * Set the power limit on the nodes, the power limit is distributed
 * given the weight of the children.
 *
 * The dtpm node lock must be held when calling this function.
 */
static int __set_power_limit_uw(struct dtpm *dtpm, int cid, u64 power_limit)
{
	struct dtpm *child;
	int ret = 0;
	u64 power;

	/*
	 * A max power limitation means we remove the power limit,
	 * otherwise we set a constraint and flag the dtpm node.
	 */
	if (power_limit == dtpm->power_max) {
		clear_bit(DTPM_POWER_LIMIT_FLAG, &dtpm->flags);
	} else {
		set_bit(DTPM_POWER_LIMIT_FLAG, &dtpm->flags);
	}

	pr_debug("Setting power limit for '%s': %llu uW\n",
		 dtpm->zone.name, power_limit);

	/*
	 * Only leaves of the dtpm tree has ops to get/set the power
	 */
	if (dtpm->ops) {
		dtpm->power_limit = dtpm->ops->set_power_uw(dtpm, power_limit);
	} else {
		dtpm->power_limit = 0;

		list_for_each_entry(child, &dtpm->children, sibling) {

			/*
			 * Integer division rounding will inevitably
			 * lead to a different min or max value when
			 * set several times. In order to restore the
			 * initial value, we force the child's min or
			 * max power every time if the constraint is
			 * at the boundaries.
			 */
			if (power_limit == dtpm->power_max) {
				power = child->power_max;
			} else if (power_limit == dtpm->power_min) {
				power = child->power_min;
			} else {
				power = DIV_ROUND_CLOSEST_ULL(
					power_limit * child->weight, 1024);
			}

			pr_debug("Setting power limit for '%s': %llu uW\n",
				 child->zone.name, power);

			ret = __set_power_limit_uw(child, cid, power);
			if (!ret)
				ret = get_power_limit_uw(&child->zone, cid, &power);

			if (ret)
				break;

			dtpm->power_limit += power;
		}
	}

	return ret;
}

static int set_power_limit_uw(struct powercap_zone *pcz,
			      int cid, u64 power_limit)
{
	struct dtpm *dtpm = to_dtpm(pcz);
	int ret;

	/*
	 * Don't allow values outside of the power range previously
	 * set when initializing the power numbers.
	 */
	power_limit = clamp_val(power_limit, dtpm->power_min, dtpm->power_max);

	ret = __set_power_limit_uw(dtpm, cid, power_limit);

	pr_debug("%s: power limit: %llu uW, power max: %llu uW\n",
		 dtpm->zone.name, dtpm->power_limit, dtpm->power_max);

	return ret;
}

static const char *get_constraint_name(struct powercap_zone *pcz, int cid)
{
	return constraint_name[cid];
}

static int get_max_power_uw(struct powercap_zone *pcz, int id, u64 *max_power)
{
	*max_power = to_dtpm(pcz)->power_max;

	return 0;
}

static struct powercap_zone_constraint_ops constraint_ops = {
	.set_power_limit_uw = set_power_limit_uw,
	.get_power_limit_uw = get_power_limit_uw,
	.set_time_window_us = set_time_window_us,
	.get_time_window_us = get_time_window_us,
	.get_max_power_uw = get_max_power_uw,
	.get_name = get_constraint_name,
};

static struct powercap_zone_ops zone_ops = {
	.get_max_power_range_uw = get_max_power_range_uw,
	.get_power_uw = get_power_uw,
	.release = dtpm_release_zone,
};

/**
 * dtpm_init - Allocate and initialize a dtpm struct
 * @dtpm: The dtpm struct pointer to be initialized
 * @ops: The dtpm device specific ops, NULL for a virtual node
 */
void dtpm_init(struct dtpm *dtpm, struct dtpm_ops *ops)
{
	if (dtpm) {
		INIT_LIST_HEAD(&dtpm->children);
		INIT_LIST_HEAD(&dtpm->sibling);
		dtpm->weight = 1024;
		dtpm->ops = ops;
	}
}

/**
 * dtpm_unregister - Unregister a dtpm node from the hierarchy tree
 * @dtpm: a pointer to a dtpm structure corresponding to the node to be removed
 *
 * Call the underlying powercap unregister function. That will call
 * the release callback of the powercap zone.
 */
void dtpm_unregister(struct dtpm *dtpm)
{
	powercap_unregister_zone(pct, &dtpm->zone);

	pr_debug("Unregistered dtpm node '%s'\n", dtpm->zone.name);
}

/**
 * dtpm_register - Register a dtpm node in the hierarchy tree
 * @name: a string specifying the name of the node
 * @dtpm: a pointer to a dtpm structure corresponding to the new node
 * @parent: a pointer to a dtpm structure corresponding to the parent node
 *
 * Create a dtpm node in the tree. If no parent is specified, the node
 * is the root node of the hierarchy. If the root node already exists,
 * then the registration will fail. The powercap controller must be
 * initialized before calling this function.
 *
 * The dtpm structure must be initialized with the power numbers
 * before calling this function.
 *
 * Return: zero on success, a negative value in case of error:
 *  -EAGAIN: the function is called before the framework is initialized.
 *  -EBUSY: the root node is already inserted
 *  -EINVAL: * there is no root node yet and @parent is specified
 *           * no all ops are defined
 *           * parent have ops which are reserved for leaves
 *   Other negative values are reported back from the powercap framework
 */
int dtpm_register(const char *name, struct dtpm *dtpm, struct dtpm *parent)
{
	struct powercap_zone *pcz;

	if (!pct)
		return -EAGAIN;

	if (root && !parent)
		return -EBUSY;

	if (!root && parent)
		return -EINVAL;

	if (parent && parent->ops)
		return -EINVAL;

	if (!dtpm)
		return -EINVAL;

	if (dtpm->ops && !(dtpm->ops->set_power_uw &&
			   dtpm->ops->get_power_uw &&
			   dtpm->ops->update_power_uw &&
			   dtpm->ops->release))
		return -EINVAL;

	pcz = powercap_register_zone(&dtpm->zone, pct, name,
				     parent ? &parent->zone : NULL,
				     &zone_ops, MAX_DTPM_CONSTRAINTS,
				     &constraint_ops);
	if (IS_ERR(pcz))
		return PTR_ERR(pcz);

	if (parent) {
		list_add_tail(&dtpm->sibling, &parent->children);
		dtpm->parent = parent;
	} else {
		root = dtpm;
	}

	if (dtpm->ops && !dtpm->ops->update_power_uw(dtpm)) {
		__dtpm_add_power(dtpm);
		dtpm->power_limit = dtpm->power_max;
	}

	pr_debug("Registered dtpm node '%s' / %llu-%llu uW, \n",
		 dtpm->zone.name, dtpm->power_min, dtpm->power_max);

	return 0;
}

static struct dtpm *dtpm_setup_virtual(const struct dtpm_node *hierarchy,
				       struct dtpm *parent)
{
	struct dtpm *dtpm;
	int ret;

	dtpm = kzalloc(sizeof(*dtpm), GFP_KERNEL);
	if (!dtpm)
		return ERR_PTR(-ENOMEM);
	dtpm_init(dtpm, NULL);

	ret = dtpm_register(hierarchy->name, dtpm, parent);
	if (ret) {
		pr_err("Failed to register dtpm node '%s': %d\n",
		       hierarchy->name, ret);
		kfree(dtpm);
		return ERR_PTR(ret);
	}

	return dtpm;
}

static struct dtpm *dtpm_setup_dt(const struct dtpm_node *hierarchy,
				  struct dtpm *parent)
{
	struct device_node *np;
	int i, ret;

	np = of_find_node_by_path(hierarchy->name);
	if (!np) {
		pr_err("Failed to find '%s'\n", hierarchy->name);
		return ERR_PTR(-ENXIO);
	}

	for (i = 0; i < ARRAY_SIZE(dtpm_subsys); i++) {

		if (!dtpm_subsys[i]->setup)
			continue;

		ret = dtpm_subsys[i]->setup(parent, np);
		if (ret) {
			pr_err("Failed to setup '%s': %d\n", dtpm_subsys[i]->name, ret);
			of_node_put(np);
			return ERR_PTR(ret);
		}
	}

	of_node_put(np);

	/*
	 * By returning a NULL pointer, we let know the caller there
	 * is no child for us as we are a leaf of the tree
	 */
	return NULL;
}

typedef struct dtpm * (*dtpm_node_callback_t)(const struct dtpm_node *, struct dtpm *);

static dtpm_node_callback_t dtpm_node_callback[] = {
	[DTPM_NODE_VIRTUAL] = dtpm_setup_virtual,
	[DTPM_NODE_DT] = dtpm_setup_dt,
};

static int dtpm_for_each_child(const struct dtpm_node *hierarchy,
			       const struct dtpm_node *it, struct dtpm *parent)
{
	struct dtpm *dtpm;
	int i, ret;

	for (i = 0; hierarchy[i].name; i++) {

		if (hierarchy[i].parent != it)
			continue;

		dtpm = dtpm_node_callback[hierarchy[i].type](&hierarchy[i], parent);

		/*
		 * A NULL pointer means there is no children, hence we
		 * continue without going deeper in the recursivity.
		 */
		if (!dtpm)
			continue;

		/*
		 * There are multiple reasons why the callback could
		 * fail. The generic glue is abstracting the backend
		 * and therefore it is not possible to report back or
		 * take a decision based on the error.  In any case,
		 * if this call fails, it is not critical in the
		 * hierarchy creation, we can assume the underlying
		 * service is not found, so we continue without this
		 * branch in the tree but with a warning to log the
		 * information the node was not created.
		 */
		if (IS_ERR(dtpm)) {
			pr_warn("Failed to create '%s' in the hierarchy\n",
				hierarchy[i].name);
			continue;
		}

		ret = dtpm_for_each_child(hierarchy, &hierarchy[i], dtpm);
		if (ret)
			return ret;
	}

	return 0;
}

/**
 * dtpm_create_hierarchy - Create the dtpm hierarchy
 * @hierarchy: An array of struct dtpm_node describing the hierarchy
 *
 * The function is called by the platform specific code with the
 * description of the different node in the hierarchy. It creates the
 * tree in the sysfs filesystem under the powercap dtpm entry.
 *
 * The expected tree has the format:
 *
 * struct dtpm_node hierarchy[] = {
 *	[0] { .name = "topmost", type =  DTPM_NODE_VIRTUAL },
 *	[1] { .name = "package", .type = DTPM_NODE_VIRTUAL, .parent = &hierarchy[0] },
 *	[2] { .name = "/cpus/cpu0", .type = DTPM_NODE_DT, .parent = &hierarchy[1] },
 *	[3] { .name = "/cpus/cpu1", .type = DTPM_NODE_DT, .parent = &hierarchy[1] },
 *	[4] { .name = "/cpus/cpu2", .type = DTPM_NODE_DT, .parent = &hierarchy[1] },
 *	[5] { .name = "/cpus/cpu3", .type = DTPM_NODE_DT, .parent = &hierarchy[1] },
 *	[6] { }
 * };
 *
 * The last element is always an empty one and marks the end of the
 * array.
 *
 * Return: zero on success, a negative value in case of error. Errors
 * are reported back from the underlying functions.
 */
int dtpm_create_hierarchy(struct of_device_id *dtpm_match_table)
{
	const struct of_device_id *match;
	const struct dtpm_node *hierarchy;
	struct device_node *np;
	int i, ret;

	mutex_lock(&dtpm_lock);

	if (pct) {
		ret = -EBUSY;
		goto out_unlock;
	}

	pct = powercap_register_control_type(NULL, "dtpm", NULL);
	if (IS_ERR(pct)) {
		pr_err("Failed to register control type\n");
		ret = PTR_ERR(pct);
		goto out_pct;
	}

	ret = -ENODEV;
	np = of_find_node_by_path("/");
	if (!np)
		goto out_err;

	match = of_match_node(dtpm_match_table, np);

	of_node_put(np);

	if (!match)
		goto out_err;

	hierarchy = match->data;
	if (!hierarchy) {
		ret = -EFAULT;
		goto out_err;
	}

	ret = dtpm_for_each_child(hierarchy, NULL, NULL);
	if (ret)
		goto out_err;
	
	for (i = 0; i < ARRAY_SIZE(dtpm_subsys); i++) {

		if (!dtpm_subsys[i]->init)
			continue;

		ret = dtpm_subsys[i]->init();
		if (ret)
			pr_info("Failed to initialize '%s': %d",
				dtpm_subsys[i]->name, ret);
	}

	mutex_unlock(&dtpm_lock);

	return 0;

out_err:
	powercap_unregister_control_type(pct);
out_pct:
	pct = NULL;
out_unlock:
	mutex_unlock(&dtpm_lock);
	
	return ret;
}
EXPORT_SYMBOL_GPL(dtpm_create_hierarchy);

static void __dtpm_destroy_hierarchy(struct dtpm *dtpm)
{
	struct dtpm *child, *aux;

	list_for_each_entry_safe(child, aux, &dtpm->children, sibling)
		__dtpm_destroy_hierarchy(child);

	/*
	 * At this point, we know all children were removed from the
	 * recursive call before
	 */
	dtpm_unregister(dtpm);
}

void dtpm_destroy_hierarchy(void)
{
	int i;

	mutex_lock(&dtpm_lock);

	if (!pct)
		goto out_unlock;

	__dtpm_destroy_hierarchy(root);
	

	for (i = 0; i < ARRAY_SIZE(dtpm_subsys); i++) {

		if (!dtpm_subsys[i]->exit)
			continue;

		dtpm_subsys[i]->exit();
	}

	powercap_unregister_control_type(pct);

	pct = NULL;

	root = NULL;

out_unlock:
	mutex_unlock(&dtpm_lock);
}
EXPORT_SYMBOL_GPL(dtpm_destroy_hierarchy);
