// SPDX-License-Identifier: GPL-2.0-only
/*
 * Generic OPP OF helpers
 *
 * Copyright (C) 2009-2010 Texas Instruments Incorporated.
 *	Nishanth Menon
 *	Romit Dasgupta
 *	Kevin Hilman
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/cpu.h>
#include <linux/errno.h>
#include <linux/device.h>
#include <linux/of_device.h>
#include <linux/pm_domain.h>
#include <linux/slab.h>
#include <linux/export.h>
#include <linux/energy_model.h>

#include "opp.h"

/*
 * Returns opp descriptor node for a device node, caller must
 * do of_node_put().
 */
static struct device_node *_opp_of_get_opp_desc_node(struct device_node *np,
						     int index)
{
	/* "operating-points-v2" can be an array for power domain providers */
	return of_parse_phandle(np, "operating-points-v2", index);
}

/* Returns opp descriptor node for a device, caller must do of_node_put() */
struct device_node *dev_pm_opp_of_get_opp_desc_node(struct device *dev)
{
	return _opp_of_get_opp_desc_node(dev->of_node, 0);
}
EXPORT_SYMBOL_GPL(dev_pm_opp_of_get_opp_desc_node);

struct opp_table *_managed_opp(struct device *dev, int index)
{
	struct opp_table *opp_table, *managed_table = NULL;
	struct device_node *np;

	np = _opp_of_get_opp_desc_node(dev->of_node, index);
	if (!np)
		return NULL;

	list_for_each_entry(opp_table, &opp_tables, node) {
		if (opp_table->np == np) {
			/*
			 * Multiple devices can point to the same OPP table and
			 * so will have same node-pointer, np.
			 *
			 * But the OPPs will be considered as shared only if the
			 * OPP table contains a "opp-shared" property.
			 */
			if (opp_table->shared_opp == OPP_TABLE_ACCESS_SHARED) {
				_get_opp_table_kref(opp_table);
				managed_table = opp_table;
			}

			break;
		}
	}

	of_node_put(np);

	return managed_table;
}

/* The caller must call dev_pm_opp_put() after the OPP is used */
static struct dev_pm_opp *_find_opp_of_np(struct opp_table *opp_table,
					  struct device_node *opp_np)
{
	struct dev_pm_opp *opp;

	mutex_lock(&opp_table->lock);

	list_for_each_entry(opp, &opp_table->opp_list, node) {
		if (opp->np == opp_np) {
			dev_pm_opp_get(opp);
			mutex_unlock(&opp_table->lock);
			return opp;
		}
	}

	mutex_unlock(&opp_table->lock);

	return NULL;
}

static struct device_node *of_parse_required_opp(struct device_node *np,
						 int index)
{
	struct device_node *required_np;

	required_np = of_parse_phandle(np, "required-opps", index);
	if (unlikely(!required_np)) {
		pr_err("%s: Unable to parse required-opps: %pOF, index: %d\n",
		       __func__, np, index);
	}

	return required_np;
}

/* The caller must call dev_pm_opp_put_opp_table() after the table is used */
static struct opp_table *_find_table_of_opp_np(struct device_node *opp_np)
{
	struct opp_table *opp_table;
	struct device_node *opp_table_np;

	lockdep_assert_held(&opp_table_lock);

	opp_table_np = of_get_parent(opp_np);
	if (!opp_table_np)
		goto err;

	/* It is safe to put the node now as all we need now is its address */
	of_node_put(opp_table_np);

	list_for_each_entry(opp_table, &opp_tables, node) {
		if (opp_table_np == opp_table->np) {
			_get_opp_table_kref(opp_table);
			return opp_table;
		}
	}

err:
	return ERR_PTR(-ENODEV);
}

/* Free resources previously acquired by _opp_table_alloc_required_tables() */
static void _opp_table_free_required_tables(struct opp_table *opp_table)
{
	struct opp_table **required_opp_tables = opp_table->required_opp_tables;
	int i;

	if (!required_opp_tables)
		return;

	for (i = 0; i < opp_table->required_opp_count; i++) {
		if (IS_ERR_OR_NULL(required_opp_tables[i]))
			break;

		dev_pm_opp_put_opp_table(required_opp_tables[i]);
	}

	kfree(required_opp_tables);

	opp_table->required_opp_count = 0;
	opp_table->required_opp_tables = NULL;
}

/*
 * Populate all devices and opp tables which are part of "required-opps" list.
 * Checking only the first OPP node should be enough.
 */
static void _opp_table_alloc_required_tables(struct opp_table *opp_table,
					     struct device *dev,
					     struct device_node *opp_np)
{
	struct opp_table **required_opp_tables;
	struct device_node *required_np, *np;
	int count, i;

	/* Traversing the first OPP node is all we need */
	np = of_get_next_available_child(opp_np, NULL);
	if (!np) {
		dev_err(dev, "Empty OPP table\n");
		return;
	}

	count = of_count_phandle_with_args(np, "required-opps", NULL);
	if (!count)
		goto put_np;

	required_opp_tables = kcalloc(count, sizeof(*required_opp_tables),
				      GFP_KERNEL);
	if (!required_opp_tables)
		goto put_np;

	opp_table->required_opp_tables = required_opp_tables;
	opp_table->required_opp_count = count;

	for (i = 0; i < count; i++) {
		required_np = of_parse_required_opp(np, i);
		if (!required_np)
			goto free_required_tables;

		required_opp_tables[i] = _find_table_of_opp_np(required_np);
		of_node_put(required_np);

		if (IS_ERR(required_opp_tables[i]))
			goto free_required_tables;

		/*
		 * We only support genpd's OPPs in the "required-opps" for now,
		 * as we don't know how much about other cases. Error out if the
		 * required OPP doesn't belong to a genpd.
		 */
		if (!required_opp_tables[i]->is_genpd) {
			dev_err(dev, "required-opp doesn't belong to genpd: %pOF\n",
				required_np);
			goto free_required_tables;
		}
	}

	goto put_np;

free_required_tables:
	_opp_table_free_required_tables(opp_table);
put_np:
	of_node_put(np);
}

void _of_init_opp_table(struct opp_table *opp_table, struct device *dev,
			int index)
{
	struct device_node *np, *opp_np;
	u32 val;

	/*
	 * Only required for backward compatibility with v1 bindings, but isn't
	 * harmful for other cases. And so we do it unconditionally.
	 */
	np = of_node_get(dev->of_node);
	if (!np)
		return;

	if (!of_property_read_u32(np, "clock-latency", &val))
		opp_table->clock_latency_ns_max = val;
	of_property_read_u32(np, "voltage-tolerance",
			     &opp_table->voltage_tolerance_v1);

	if (of_find_property(np, "#power-domain-cells", NULL))
		opp_table->is_genpd = true;

	/* Get OPP table node */
	opp_np = _opp_of_get_opp_desc_node(np, index);
	of_node_put(np);

	if (!opp_np)
		return;

	if (of_property_read_bool(opp_np, "opp-shared"))
		opp_table->shared_opp = OPP_TABLE_ACCESS_SHARED;
	else
		opp_table->shared_opp = OPP_TABLE_ACCESS_EXCLUSIVE;

	opp_table->np = opp_np;

	_opp_table_alloc_required_tables(opp_table, dev, opp_np);
	of_node_put(opp_np);
}

void _of_clear_opp_table(struct opp_table *opp_table)
{
	_opp_table_free_required_tables(opp_table);
}

/*
 * Release all resources previously acquired with a call to
 * _of_opp_alloc_required_opps().
 */
void _of_opp_free_required_opps(struct opp_table *opp_table,
				struct dev_pm_opp *opp)
{
	struct dev_pm_opp **required_opps = opp->required_opps;
	int i;

	if (!required_opps)
		return;

	for (i = 0; i < opp_table->required_opp_count; i++) {
		if (!required_opps[i])
			break;

		/* Put the reference back */
		dev_pm_opp_put(required_opps[i]);
	}

	kfree(required_opps);
	opp->required_opps = NULL;
}

/* Populate all required OPPs which are part of "required-opps" list */
static int _of_opp_alloc_required_opps(struct opp_table *opp_table,
				       struct dev_pm_opp *opp)
{
	struct dev_pm_opp **required_opps;
	struct opp_table *required_table;
	struct device_node *np;
	int i, ret, count = opp_table->required_opp_count;

	if (!count)
		return 0;

	required_opps = kcalloc(count, sizeof(*required_opps), GFP_KERNEL);
	if (!required_opps)
		return -ENOMEM;

	opp->required_opps = required_opps;

	for (i = 0; i < count; i++) {
		required_table = opp_table->required_opp_tables[i];

		np = of_parse_required_opp(opp->np, i);
		if (unlikely(!np)) {
			ret = -ENODEV;
			goto free_required_opps;
		}

		required_opps[i] = _find_opp_of_np(required_table, np);
		of_node_put(np);

		if (!required_opps[i]) {
			pr_err("%s: Unable to find required OPP node: %pOF (%d)\n",
			       __func__, opp->np, i);
			ret = -ENODEV;
			goto free_required_opps;
		}
	}

	return 0;

free_required_opps:
	_of_opp_free_required_opps(opp_table, opp);

	return ret;
}

static int _bandwidth_supported(struct device *dev, struct opp_table *opp_table)
{
	struct device_node *np, *opp_np;
	struct property *prop;

	if (!opp_table) {
		np = of_node_get(dev->of_node);
		if (!np)
			return -ENODEV;

		opp_np = _opp_of_get_opp_desc_node(np, 0);
		of_node_put(np);
	} else {
		opp_np = of_node_get(opp_table->np);
	}

	/* Lets not fail in case we are parsing opp-v1 bindings */
	if (!opp_np)
		return 0;

	/* Checking only first OPP is sufficient */
	np = of_get_next_available_child(opp_np, NULL);
	if (!np) {
		dev_err(dev, "OPP table empty\n");
		return -EINVAL;
	}
	of_node_put(opp_np);

	prop = of_find_property(np, "opp-peak-kBps", NULL);
	of_node_put(np);

	if (!prop || !prop->length)
		return 0;

	return 1;
}

int dev_pm_opp_of_find_icc_paths(struct device *dev,
				 struct opp_table *opp_table)
{
	struct device_node *np;
	int ret, i, count, num_paths;
	struct icc_path **paths;

	ret = _bandwidth_supported(dev, opp_table);
	if (ret <= 0)
		return ret;

	ret = 0;

	np = of_node_get(dev->of_node);
	if (!np)
		return 0;

	count = of_count_phandle_with_args(np, "interconnects",
					   "#interconnect-cells");
	of_node_put(np);
	if (count < 0)
		return 0;

	/* two phandles when #interconnect-cells = <1> */
	if (count % 2) {
		dev_err(dev, "%s: Invalid interconnects values\n", __func__);
		return -EINVAL;
	}

	num_paths = count / 2;
	paths = kcalloc(num_paths, sizeof(*paths), GFP_KERNEL);
	if (!paths)
		return -ENOMEM;

	for (i = 0; i < num_paths; i++) {
		paths[i] = of_icc_get_by_index(dev, i);
		if (IS_ERR(paths[i])) {
			ret = PTR_ERR(paths[i]);
			if (ret != -EPROBE_DEFER) {
				dev_err(dev, "%s: Unable to get path%d: %d\n",
					__func__, i, ret);
			}
			goto err;
		}
	}

	if (opp_table) {
		opp_table->paths = paths;
		opp_table->path_count = num_paths;
		return 0;
	}

err:
	while (i--)
		icc_put(paths[i]);

	kfree(paths);

	return ret;
}
EXPORT_SYMBOL_GPL(dev_pm_opp_of_find_icc_paths);

static bool _opp_is_supported(struct device *dev, struct opp_table *opp_table,
			      struct device_node *np)
{
	unsigned int count = opp_table->supported_hw_count;
	u32 version;
	int ret;

	if (!opp_table->supported_hw) {
		/*
		 * In the case that no supported_hw has been set by the
		 * platform but there is an opp-supported-hw value set for
		 * an OPP then the OPP should not be enabled as there is
		 * no way to see if the hardware supports it.
		 */
		if (of_find_property(np, "opp-supported-hw", NULL))
			return false;
		else
			return true;
	}

	while (count--) {
		ret = of_property_read_u32_index(np, "opp-supported-hw", count,
						 &version);
		if (ret) {
			dev_warn(dev, "%s: failed to read opp-supported-hw property at index %d: %d\n",
				 __func__, count, ret);
			return false;
		}

		/* Both of these are bitwise masks of the versions */
		if (!(version & opp_table->supported_hw[count]))
			return false;
	}

	return true;
}

static int opp_parse_supplies(struct dev_pm_opp *opp, struct device *dev,
			      struct opp_table *opp_table)
{
	u32 *microvolt, *microamp = NULL;
	int supplies = opp_table->regulator_count, vcount, icount, ret, i, j;
	struct property *prop = NULL;
	char name[NAME_MAX];

	/* Search for "opp-microvolt-<name>" */
	if (opp_table->prop_name) {
		snprintf(name, sizeof(name), "opp-microvolt-%s",
			 opp_table->prop_name);
		prop = of_find_property(opp->np, name, NULL);
	}

	if (!prop) {
		/* Search for "opp-microvolt" */
		sprintf(name, "opp-microvolt");
		prop = of_find_property(opp->np, name, NULL);

		/* Missing property isn't a problem, but an invalid entry is */
		if (!prop) {
			if (unlikely(supplies == -1)) {
				/* Initialize regulator_count */
				opp_table->regulator_count = 0;
				return 0;
			}

			if (!supplies)
				return 0;

			dev_err(dev, "%s: opp-microvolt missing although OPP managing regulators\n",
				__func__);
			return -EINVAL;
		}
	}

	if (unlikely(supplies == -1)) {
		/* Initialize regulator_count */
		supplies = opp_table->regulator_count = 1;
	} else if (unlikely(!supplies)) {
		dev_err(dev, "%s: opp-microvolt wasn't expected\n", __func__);
		return -EINVAL;
	}

	vcount = of_property_count_u32_elems(opp->np, name);
	if (vcount < 0) {
		dev_err(dev, "%s: Invalid %s property (%d)\n",
			__func__, name, vcount);
		return vcount;
	}

	/* There can be one or three elements per supply */
	if (vcount != supplies && vcount != supplies * 3) {
		dev_err(dev, "%s: Invalid number of elements in %s property (%d) with supplies (%d)\n",
			__func__, name, vcount, supplies);
		return -EINVAL;
	}

	microvolt = kmalloc_array(vcount, sizeof(*microvolt), GFP_KERNEL);
	if (!microvolt)
		return -ENOMEM;

	ret = of_property_read_u32_array(opp->np, name, microvolt, vcount);
	if (ret) {
		dev_err(dev, "%s: error parsing %s: %d\n", __func__, name, ret);
		ret = -EINVAL;
		goto free_microvolt;
	}

	/* Search for "opp-microamp-<name>" */
	prop = NULL;
	if (opp_table->prop_name) {
		snprintf(name, sizeof(name), "opp-microamp-%s",
			 opp_table->prop_name);
		prop = of_find_property(opp->np, name, NULL);
	}

	if (!prop) {
		/* Search for "opp-microamp" */
		sprintf(name, "opp-microamp");
		prop = of_find_property(opp->np, name, NULL);
	}

	if (prop) {
		icount = of_property_count_u32_elems(opp->np, name);
		if (icount < 0) {
			dev_err(dev, "%s: Invalid %s property (%d)\n", __func__,
				name, icount);
			ret = icount;
			goto free_microvolt;
		}

		if (icount != supplies) {
			dev_err(dev, "%s: Invalid number of elements in %s property (%d) with supplies (%d)\n",
				__func__, name, icount, supplies);
			ret = -EINVAL;
			goto free_microvolt;
		}

		microamp = kmalloc_array(icount, sizeof(*microamp), GFP_KERNEL);
		if (!microamp) {
			ret = -EINVAL;
			goto free_microvolt;
		}

		ret = of_property_read_u32_array(opp->np, name, microamp,
						 icount);
		if (ret) {
			dev_err(dev, "%s: error parsing %s: %d\n", __func__,
				name, ret);
			ret = -EINVAL;
			goto free_microamp;
		}
	}

	for (i = 0, j = 0; i < supplies; i++) {
		opp->supplies[i].u_volt = microvolt[j++];

		if (vcount == supplies) {
			opp->supplies[i].u_volt_min = opp->supplies[i].u_volt;
			opp->supplies[i].u_volt_max = opp->supplies[i].u_volt;
		} else {
			opp->supplies[i].u_volt_min = microvolt[j++];
			opp->supplies[i].u_volt_max = microvolt[j++];
		}

		if (microamp)
			opp->supplies[i].u_amp = microamp[i];
	}

free_microamp:
	kfree(microamp);
free_microvolt:
	kfree(microvolt);

	return ret;
}

/**
 * dev_pm_opp_of_remove_table() - Free OPP table entries created from static DT
 *				  entries
 * @dev:	device pointer used to lookup OPP table.
 *
 * Free OPPs created using static entries present in DT.
 */
void dev_pm_opp_of_remove_table(struct device *dev)
{
	_dev_pm_opp_find_and_remove_table(dev);
}
EXPORT_SYMBOL_GPL(dev_pm_opp_of_remove_table);

static int _read_bw(struct dev_pm_opp *new_opp, struct opp_table *table,
		    struct device_node *np, bool peak)
{
	const char *name = peak ? "opp-peak-kBps" : "opp-avg-kBps";
	struct property *prop;
	int i, count, ret;
	u32 *bw;

	prop = of_find_property(np, name, NULL);
	if (!prop)
		return -ENODEV;

	count = prop->length / sizeof(u32);
	if (table->path_count != count) {
		pr_err("%s: Mismatch between %s and paths (%d %d)\n",
				__func__, name, count, table->path_count);
		return -EINVAL;
	}

	bw = kmalloc_array(count, sizeof(*bw), GFP_KERNEL);
	if (!bw)
		return -ENOMEM;

	ret = of_property_read_u32_array(np, name, bw, count);
	if (ret) {
		pr_err("%s: Error parsing %s: %d\n", __func__, name, ret);
		goto out;
	}

	for (i = 0; i < count; i++) {
		if (peak)
			new_opp->bandwidth[i].peak = kBps_to_icc(bw[i]);
		else
			new_opp->bandwidth[i].avg = kBps_to_icc(bw[i]);
	}

out:
	kfree(bw);
	return ret;
}

static int _read_opp_key(struct dev_pm_opp *new_opp, struct opp_table *table,
			 struct device_node *np, bool *rate_not_available)
{
	bool found = false;
	u64 rate;
	int ret;

	ret = of_property_read_u64(np, "opp-hz", &rate);
	if (!ret) {
		/*
		 * Rate is defined as an unsigned long in clk API, and so
		 * casting explicitly to its type. Must be fixed once rate is 64
		 * bit guaranteed in clk API.
		 */
		new_opp->rate = (unsigned long)rate;
		found = true;
	}
	*rate_not_available = !!ret;

	/*
	 * Bandwidth consists of peak and average (optional) values:
	 * opp-peak-kBps = <path1_value path2_value>;
	 * opp-avg-kBps = <path1_value path2_value>;
	 */
	ret = _read_bw(new_opp, table, np, true);
	if (!ret) {
		found = true;
		ret = _read_bw(new_opp, table, np, false);
	}

	/* The properties were found but we failed to parse them */
	if (ret && ret != -ENODEV)
		return ret;

	if (!of_property_read_u32(np, "opp-level", &new_opp->level))
		found = true;

	if (found)
		return 0;

	return ret;
}

/**
 * _opp_add_static_v2() - Allocate static OPPs (As per 'v2' DT bindings)
 * @opp_table:	OPP table
 * @dev:	device for which we do this operation
 * @np:		device node
 *
 * This function adds an opp definition to the opp table and returns status. The
 * opp can be controlled using dev_pm_opp_enable/disable functions and may be
 * removed by dev_pm_opp_remove.
 *
 * Return:
 * Valid OPP pointer:
 *		On success
 * NULL:
 *		Duplicate OPPs (both freq and volt are same) and opp->available
 *		OR if the OPP is not supported by hardware.
 * ERR_PTR(-EEXIST):
 *		Freq are same and volt are different OR
 *		Duplicate OPPs (both freq and volt are same) and !opp->available
 * ERR_PTR(-ENOMEM):
 *		Memory allocation failure
 * ERR_PTR(-EINVAL):
 *		Failed parsing the OPP node
 */
static struct dev_pm_opp *_opp_add_static_v2(struct opp_table *opp_table,
		struct device *dev, struct device_node *np)
{
	struct dev_pm_opp *new_opp;
	u64 rate = 0;
	u32 val;
	int ret;
	bool rate_not_available = false;

	new_opp = _opp_allocate(opp_table);
	if (!new_opp)
		return ERR_PTR(-ENOMEM);

	ret = _read_opp_key(new_opp, opp_table, np, &rate_not_available);
	if (ret < 0 && !opp_table->is_genpd) {
		dev_err(dev, "%s: opp key field not found\n", __func__);
		goto free_opp;
	}

	/* Check if the OPP supports hardware's hierarchy of versions or not */
	if (!_opp_is_supported(dev, opp_table, np)) {
		dev_dbg(dev, "OPP not supported by hardware: %llu\n", rate);
		goto free_opp;
	}

	new_opp->turbo = of_property_read_bool(np, "turbo-mode");

	new_opp->np = np;
	new_opp->dynamic = false;
	new_opp->available = true;

	ret = _of_opp_alloc_required_opps(opp_table, new_opp);
	if (ret)
		goto free_opp;

	if (!of_property_read_u32(np, "clock-latency-ns", &val))
		new_opp->clock_latency_ns = val;

	ret = opp_parse_supplies(new_opp, dev, opp_table);
	if (ret)
		goto free_required_opps;

	if (opp_table->is_genpd)
		new_opp->pstate = pm_genpd_opp_to_performance_state(dev, new_opp);

	ret = _opp_add(dev, new_opp, opp_table, rate_not_available);
	if (ret) {
		/* Don't return error for duplicate OPPs */
		if (ret == -EBUSY)
			ret = 0;
		goto free_required_opps;
	}

	/* OPP to select on device suspend */
	if (of_property_read_bool(np, "opp-suspend")) {
		if (opp_table->suspend_opp) {
			/* Pick the OPP with higher rate as suspend OPP */
			if (new_opp->rate > opp_table->suspend_opp->rate) {
				opp_table->suspend_opp->suspend = false;
				new_opp->suspend = true;
				opp_table->suspend_opp = new_opp;
			}
		} else {
			new_opp->suspend = true;
			opp_table->suspend_opp = new_opp;
		}
	}

	if (new_opp->clock_latency_ns > opp_table->clock_latency_ns_max)
		opp_table->clock_latency_ns_max = new_opp->clock_latency_ns;

	pr_debug("%s: turbo:%d rate:%lu uv:%lu uvmin:%lu uvmax:%lu latency:%lu\n",
		 __func__, new_opp->turbo, new_opp->rate,
		 new_opp->supplies[0].u_volt, new_opp->supplies[0].u_volt_min,
		 new_opp->supplies[0].u_volt_max, new_opp->clock_latency_ns);

	/*
	 * Notify the changes in the availability of the operable
	 * frequency/voltage list.
	 */
	blocking_notifier_call_chain(&opp_table->head, OPP_EVENT_ADD, new_opp);
	return new_opp;

free_required_opps:
	_of_opp_free_required_opps(opp_table, new_opp);
free_opp:
	_opp_free(new_opp);

	return ERR_PTR(ret);
}

/* Initializes OPP tables based on new bindings */
static int _of_add_opp_table_v2(struct device *dev, struct opp_table *opp_table)
{
	struct device_node *np;
	int ret, count = 0, pstate_count = 0;
	struct dev_pm_opp *opp;

	/* OPP table is already initialized for the device */
	mutex_lock(&opp_table->lock);
	if (opp_table->parsed_static_opps) {
		opp_table->parsed_static_opps++;
		mutex_unlock(&opp_table->lock);
		return 0;
	}

	opp_table->parsed_static_opps = 1;
	mutex_unlock(&opp_table->lock);

	/* We have opp-table node now, iterate over it and add OPPs */
	for_each_available_child_of_node(opp_table->np, np) {
		opp = _opp_add_static_v2(opp_table, dev, np);
		if (IS_ERR(opp)) {
			ret = PTR_ERR(opp);
			dev_err(dev, "%s: Failed to add OPP, %d\n", __func__,
				ret);
			of_node_put(np);
			goto remove_static_opp;
		} else if (opp) {
			count++;
		}
	}

	/* There should be one of more OPP defined */
	if (WARN_ON(!count)) {
		ret = -ENOENT;
		goto remove_static_opp;
	}

	list_for_each_entry(opp, &opp_table->opp_list, node)
		pstate_count += !!opp->pstate;

	/* Either all or none of the nodes shall have performance state set */
	if (pstate_count && pstate_count != count) {
		dev_err(dev, "Not all nodes have performance state set (%d: %d)\n",
			count, pstate_count);
		ret = -ENOENT;
		goto remove_static_opp;
	}

	if (pstate_count)
		opp_table->genpd_performance_state = true;

	return 0;

remove_static_opp:
	_opp_remove_all_static(opp_table);

	return ret;
}

/* Initializes OPP tables based on old-deprecated bindings */
static int _of_add_opp_table_v1(struct device *dev, struct opp_table *opp_table)
{
	const struct property *prop;
	const __be32 *val;
	int nr, ret = 0;

	mutex_lock(&opp_table->lock);
	if (opp_table->parsed_static_opps) {
		opp_table->parsed_static_opps++;
		mutex_unlock(&opp_table->lock);
		return 0;
	}

	opp_table->parsed_static_opps = 1;
	mutex_unlock(&opp_table->lock);

	prop = of_find_property(dev->of_node, "operating-points", NULL);
	if (!prop) {
		ret = -ENODEV;
		goto remove_static_opp;
	}
	if (!prop->value) {
		ret = -ENODATA;
		goto remove_static_opp;
	}

	/*
	 * Each OPP is a set of tuples consisting of frequency and
	 * voltage like <freq-kHz vol-uV>.
	 */
	nr = prop->length / sizeof(u32);
	if (nr % 2) {
		dev_err(dev, "%s: Invalid OPP table\n", __func__);
		ret = -EINVAL;
		goto remove_static_opp;
	}

	val = prop->value;
	while (nr) {
		unsigned long freq = be32_to_cpup(val++) * 1000;
		unsigned long volt = be32_to_cpup(val++);

		ret = _opp_add_v1(opp_table, dev, freq, volt, false);
		if (ret) {
			dev_err(dev, "%s: Failed to add OPP %ld (%d)\n",
				__func__, freq, ret);
			goto remove_static_opp;
		}
		nr -= 2;
	}

remove_static_opp:
	_opp_remove_all_static(opp_table);

	return ret;
}

/**
 * dev_pm_opp_of_add_table() - Initialize opp table from device tree
 * @dev:	device pointer used to lookup OPP table.
 *
 * Register the initial OPP table with the OPP library for given device.
 *
 * Return:
 * 0		On success OR
 *		Duplicate OPPs (both freq and volt are same) and opp->available
 * -EEXIST	Freq are same and volt are different OR
 *		Duplicate OPPs (both freq and volt are same) and !opp->available
 * -ENOMEM	Memory allocation failure
 * -ENODEV	when 'operating-points' property is not found or is invalid data
 *		in device node.
 * -ENODATA	when empty 'operating-points' property is found
 * -EINVAL	when invalid entries are found in opp-v2 table
 */
int dev_pm_opp_of_add_table(struct device *dev)
{
	struct opp_table *opp_table;
	int ret;

	opp_table = dev_pm_opp_get_opp_table_indexed(dev, 0);
	if (IS_ERR(opp_table))
		return PTR_ERR(opp_table);

	/*
	 * OPPs have two version of bindings now. Also try the old (v1)
	 * bindings for backward compatibility with older dtbs.
	 */
	if (opp_table->np)
		ret = _of_add_opp_table_v2(dev, opp_table);
	else
		ret = _of_add_opp_table_v1(dev, opp_table);

	if (ret)
		dev_pm_opp_put_opp_table(opp_table);

	return ret;
}
EXPORT_SYMBOL_GPL(dev_pm_opp_of_add_table);

/**
 * dev_pm_opp_of_add_table_indexed() - Initialize indexed opp table from device tree
 * @dev:	device pointer used to lookup OPP table.
 * @index:	Index number.
 *
 * Register the initial OPP table with the OPP library for given device only
 * using the "operating-points-v2" property.
 *
 * Return:
 * 0		On success OR
 *		Duplicate OPPs (both freq and volt are same) and opp->available
 * -EEXIST	Freq are same and volt are different OR
 *		Duplicate OPPs (both freq and volt are same) and !opp->available
 * -ENOMEM	Memory allocation failure
 * -ENODEV	when 'operating-points' property is not found or is invalid data
 *		in device node.
 * -ENODATA	when empty 'operating-points' property is found
 * -EINVAL	when invalid entries are found in opp-v2 table
 */
int dev_pm_opp_of_add_table_indexed(struct device *dev, int index)
{
	struct opp_table *opp_table;
	int ret, count;

	if (index) {
		/*
		 * If only one phandle is present, then the same OPP table
		 * applies for all index requests.
		 */
		count = of_count_phandle_with_args(dev->of_node,
						   "operating-points-v2", NULL);
		if (count == 1)
			index = 0;
	}

	opp_table = dev_pm_opp_get_opp_table_indexed(dev, index);
	if (IS_ERR(opp_table))
		return PTR_ERR(opp_table);

	ret = _of_add_opp_table_v2(dev, opp_table);
	if (ret)
		dev_pm_opp_put_opp_table(opp_table);

	return ret;
}
EXPORT_SYMBOL_GPL(dev_pm_opp_of_add_table_indexed);

/* CPU device specific helpers */

/**
 * dev_pm_opp_of_cpumask_remove_table() - Removes OPP table for @cpumask
 * @cpumask:	cpumask for which OPP table needs to be removed
 *
 * This removes the OPP tables for CPUs present in the @cpumask.
 * This should be used only to remove static entries created from DT.
 */
void dev_pm_opp_of_cpumask_remove_table(const struct cpumask *cpumask)
{
	_dev_pm_opp_cpumask_remove_table(cpumask, -1);
}
EXPORT_SYMBOL_GPL(dev_pm_opp_of_cpumask_remove_table);

/**
 * dev_pm_opp_of_cpumask_add_table() - Adds OPP table for @cpumask
 * @cpumask:	cpumask for which OPP table needs to be added.
 *
 * This adds the OPP tables for CPUs present in the @cpumask.
 */
int dev_pm_opp_of_cpumask_add_table(const struct cpumask *cpumask)
{
	struct device *cpu_dev;
	int cpu, ret;

	if (WARN_ON(cpumask_empty(cpumask)))
		return -ENODEV;

	for_each_cpu(cpu, cpumask) {
		cpu_dev = get_cpu_device(cpu);
		if (!cpu_dev) {
			pr_err("%s: failed to get cpu%d device\n", __func__,
			       cpu);
			ret = -ENODEV;
			goto remove_table;
		}

		ret = dev_pm_opp_of_add_table(cpu_dev);
		if (ret) {
			/*
			 * OPP may get registered dynamically, don't print error
			 * message here.
			 */
			pr_debug("%s: couldn't find opp table for cpu:%d, %d\n",
				 __func__, cpu, ret);

			goto remove_table;
		}
	}

	return 0;

remove_table:
	/* Free all other OPPs */
	_dev_pm_opp_cpumask_remove_table(cpumask, cpu);

	return ret;
}
EXPORT_SYMBOL_GPL(dev_pm_opp_of_cpumask_add_table);

/*
 * Works only for OPP v2 bindings.
 *
 * Returns -ENOENT if operating-points-v2 bindings aren't supported.
 */
/**
 * dev_pm_opp_of_get_sharing_cpus() - Get cpumask of CPUs sharing OPPs with
 *				      @cpu_dev using operating-points-v2
 *				      bindings.
 *
 * @cpu_dev:	CPU device for which we do this operation
 * @cpumask:	cpumask to update with information of sharing CPUs
 *
 * This updates the @cpumask with CPUs that are sharing OPPs with @cpu_dev.
 *
 * Returns -ENOENT if operating-points-v2 isn't present for @cpu_dev.
 */
int dev_pm_opp_of_get_sharing_cpus(struct device *cpu_dev,
				   struct cpumask *cpumask)
{
	struct device_node *np, *tmp_np, *cpu_np;
	int cpu, ret = 0;

	/* Get OPP descriptor node */
	np = dev_pm_opp_of_get_opp_desc_node(cpu_dev);
	if (!np) {
		dev_dbg(cpu_dev, "%s: Couldn't find opp node.\n", __func__);
		return -ENOENT;
	}

	cpumask_set_cpu(cpu_dev->id, cpumask);

	/* OPPs are shared ? */
	if (!of_property_read_bool(np, "opp-shared"))
		goto put_cpu_node;

	for_each_possible_cpu(cpu) {
		if (cpu == cpu_dev->id)
			continue;

		cpu_np = of_cpu_device_node_get(cpu);
		if (!cpu_np) {
			dev_err(cpu_dev, "%s: failed to get cpu%d node\n",
				__func__, cpu);
			ret = -ENOENT;
			goto put_cpu_node;
		}

		/* Get OPP descriptor node */
		tmp_np = _opp_of_get_opp_desc_node(cpu_np, 0);
		of_node_put(cpu_np);
		if (!tmp_np) {
			pr_err("%pOF: Couldn't find opp node\n", cpu_np);
			ret = -ENOENT;
			goto put_cpu_node;
		}

		/* CPUs are sharing opp node */
		if (np == tmp_np)
			cpumask_set_cpu(cpu, cpumask);

		of_node_put(tmp_np);
	}

put_cpu_node:
	of_node_put(np);
	return ret;
}
EXPORT_SYMBOL_GPL(dev_pm_opp_of_get_sharing_cpus);

/**
 * of_get_required_opp_performance_state() - Search for required OPP and return its performance state.
 * @np: Node that contains the "required-opps" property.
 * @index: Index of the phandle to parse.
 *
 * Returns the performance state of the OPP pointed out by the "required-opps"
 * property at @index in @np.
 *
 * Return: Zero or positive performance state on success, otherwise negative
 * value on errors.
 */
int of_get_required_opp_performance_state(struct device_node *np, int index)
{
	struct dev_pm_opp *opp;
	struct device_node *required_np;
	struct opp_table *opp_table;
	int pstate = -EINVAL;

	required_np = of_parse_required_opp(np, index);
	if (!required_np)
		return -EINVAL;

	opp_table = _find_table_of_opp_np(required_np);
	if (IS_ERR(opp_table)) {
		pr_err("%s: Failed to find required OPP table %pOF: %ld\n",
		       __func__, np, PTR_ERR(opp_table));
		goto put_required_np;
	}

	opp = _find_opp_of_np(opp_table, required_np);
	if (opp) {
		pstate = opp->pstate;
		dev_pm_opp_put(opp);
	}

	dev_pm_opp_put_opp_table(opp_table);

put_required_np:
	of_node_put(required_np);

	return pstate;
}
EXPORT_SYMBOL_GPL(of_get_required_opp_performance_state);

/**
 * dev_pm_opp_get_of_node() - Gets the DT node corresponding to an opp
 * @opp:	opp for which DT node has to be returned for
 *
 * Return: DT node corresponding to the opp, else 0 on success.
 *
 * The caller needs to put the node with of_node_put() after using it.
 */
struct device_node *dev_pm_opp_get_of_node(struct dev_pm_opp *opp)
{
	if (IS_ERR_OR_NULL(opp)) {
		pr_err("%s: Invalid parameters\n", __func__);
		return NULL;
	}

	return of_node_get(opp->np);
}
EXPORT_SYMBOL_GPL(dev_pm_opp_get_of_node);

/*
 * Callback function provided to the Energy Model framework upon registration.
 * This computes the power estimated by @dev at @kHz if it is the frequency
 * of an existing OPP, or at the frequency of the first OPP above @kHz otherwise
 * (see dev_pm_opp_find_freq_ceil()). This function updates @kHz to the ceiled
 * frequency and @mW to the associated power. The power is estimated as
 * P = C * V^2 * f with C being the device's capacitance and V and f
 * respectively the voltage and frequency of the OPP.
 *
 * Returns -EINVAL if the power calculation failed because of missing
 * parameters, 0 otherwise.
 */
static int __maybe_unused _get_power(unsigned long *mW, unsigned long *kHz,
				     struct device *dev)
{
	struct dev_pm_opp *opp;
	struct device_node *np;
	unsigned long mV, Hz;
	u32 cap;
	u64 tmp;
	int ret;

	np = of_node_get(dev->of_node);
	if (!np)
		return -EINVAL;

	ret = of_property_read_u32(np, "dynamic-power-coefficient", &cap);
	of_node_put(np);
	if (ret)
		return -EINVAL;

	Hz = *kHz * 1000;
	opp = dev_pm_opp_find_freq_ceil(dev, &Hz);
	if (IS_ERR(opp))
		return -EINVAL;

	mV = dev_pm_opp_get_voltage(opp) / 1000;
	dev_pm_opp_put(opp);
	if (!mV)
		return -EINVAL;

	tmp = (u64)cap * mV * mV * (Hz / 1000000);
	do_div(tmp, 1000000000);

	*mW = (unsigned long)tmp;
	*kHz = Hz / 1000;

	return 0;
}

/**
 * dev_pm_opp_of_register_em() - Attempt to register an Energy Model
 * @dev		: Device for which an Energy Model has to be registered
 * @cpus	: CPUs for which an Energy Model has to be registered. For
 *		other type of devices it should be set to NULL.
 *
 * This checks whether the "dynamic-power-coefficient" devicetree property has
 * been specified, and tries to register an Energy Model with it if it has.
 * Having this property means the voltages are known for OPPs and the EM
 * might be calculated.
 */
int dev_pm_opp_of_register_em(struct device *dev, struct cpumask *cpus)
{
	struct em_data_callback em_cb = EM_DATA_CB(_get_power);
	struct device_node *np;
	int ret, nr_opp;
	u32 cap;

	if (IS_ERR_OR_NULL(dev)) {
		ret = -EINVAL;
		goto failed;
	}

	nr_opp = dev_pm_opp_get_opp_count(dev);
	if (nr_opp <= 0) {
		ret = -EINVAL;
		goto failed;
	}

	np = of_node_get(dev->of_node);
	if (!np) {
		ret = -EINVAL;
		goto failed;
	}

	/*
	 * Register an EM only if the 'dynamic-power-coefficient' property is
	 * set in devicetree. It is assumed the voltage values are known if that
	 * property is set since it is useless otherwise. If voltages are not
	 * known, just let the EM registration fail with an error to alert the
	 * user about the inconsistent configuration.
	 */
	ret = of_property_read_u32(np, "dynamic-power-coefficient", &cap);
	of_node_put(np);
	if (ret || !cap) {
		dev_dbg(dev, "Couldn't find proper 'dynamic-power-coefficient' in DT\n");
		ret = -EINVAL;
		goto failed;
	}

	ret = em_dev_register_perf_domain(dev, nr_opp, &em_cb, cpus);
	if (ret)
		goto failed;

	return 0;

failed:
	dev_dbg(dev, "Couldn't register Energy Model %d\n", ret);
	return ret;
}
EXPORT_SYMBOL_GPL(dev_pm_opp_of_register_em);
