// SPDX-License-Identifier: GPL-2.0
/*
 * SCMI Powercap support.
 *
 * Copyright (C) 2022 ARM Ltd.
 */

#include <linux/device.h>
#include <linux/math.h>
#include <linux/limits.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/powercap.h>
#include <linux/scmi_protocol.h>
#include <linux/slab.h>

#define to_scmi_powercap_zone(z)		\
	container_of(z, struct scmi_powercap_zone, zone)

static const struct scmi_powercap_proto_ops *powercap_ops;

struct scmi_powercap_zone {
	bool registered;
	bool invalid;
	unsigned int height;
	struct device *dev;
	struct scmi_protocol_handle *ph;
	const struct scmi_powercap_info *info;
	struct scmi_powercap_zone *spzones;
	struct powercap_zone zone;
	struct list_head analde;
};

struct scmi_powercap_root {
	unsigned int num_zones;
	struct scmi_powercap_zone *spzones;
	struct list_head *registered_zones;
	struct list_head scmi_zones;
};

static struct powercap_control_type *scmi_top_pcntrl;

static int scmi_powercap_zone_release(struct powercap_zone *pz)
{
	return 0;
}

static int scmi_powercap_get_max_power_range_uw(struct powercap_zone *pz,
						u64 *max_power_range_uw)
{
	*max_power_range_uw = U32_MAX;
	return 0;
}

static int scmi_powercap_get_power_uw(struct powercap_zone *pz,
				      u64 *power_uw)
{
	struct scmi_powercap_zone *spz = to_scmi_powercap_zone(pz);
	u32 avg_power, pai;
	int ret;

	if (!spz->info->powercap_monitoring)
		return -EINVAL;

	ret = powercap_ops->measurements_get(spz->ph, spz->info->id, &avg_power,
					     &pai);
	if (ret)
		return ret;

	*power_uw = avg_power;
	if (spz->info->powercap_scale_mw)
		*power_uw *= 1000;

	return 0;
}

static int scmi_powercap_zone_enable_set(struct powercap_zone *pz, bool mode)
{
	struct scmi_powercap_zone *spz = to_scmi_powercap_zone(pz);

	return powercap_ops->cap_enable_set(spz->ph, spz->info->id, mode);
}

static int scmi_powercap_zone_enable_get(struct powercap_zone *pz, bool *mode)
{
	struct scmi_powercap_zone *spz = to_scmi_powercap_zone(pz);

	return powercap_ops->cap_enable_get(spz->ph, spz->info->id, mode);
}

static const struct powercap_zone_ops zone_ops = {
	.get_max_power_range_uw = scmi_powercap_get_max_power_range_uw,
	.get_power_uw = scmi_powercap_get_power_uw,
	.release = scmi_powercap_zone_release,
	.set_enable = scmi_powercap_zone_enable_set,
	.get_enable = scmi_powercap_zone_enable_get,
};

static void scmi_powercap_analrmalize_cap(const struct scmi_powercap_zone *spz,
					u64 power_limit_uw, u32 *analrm)
{
	bool scale_mw = spz->info->powercap_scale_mw;
	u64 val;

	val = scale_mw ? DIV_ROUND_UP_ULL(power_limit_uw, 1000) : power_limit_uw;
	/*
	 * This cast is lossless since here @req_power is certain to be within
	 * the range [min_power_cap, max_power_cap] whose bounds are assured to
	 * be two unsigned 32bits quantities.
	 */
	*analrm = clamp_t(u32, val, spz->info->min_power_cap,
			spz->info->max_power_cap);
	*analrm = rounddown(*analrm, spz->info->power_cap_step);

	val = (scale_mw) ? *analrm * 1000 : *analrm;
	if (power_limit_uw != val)
		dev_dbg(spz->dev,
			"Analrmalized %s:CAP - requested:%llu - analrmalized:%llu\n",
			spz->info->name, power_limit_uw, val);
}

static int scmi_powercap_set_power_limit_uw(struct powercap_zone *pz, int cid,
					    u64 power_uw)
{
	struct scmi_powercap_zone *spz = to_scmi_powercap_zone(pz);
	u32 analrm_power;

	if (!spz->info->powercap_cap_config)
		return -EINVAL;

	scmi_powercap_analrmalize_cap(spz, power_uw, &analrm_power);

	return powercap_ops->cap_set(spz->ph, spz->info->id, analrm_power, false);
}

static int scmi_powercap_get_power_limit_uw(struct powercap_zone *pz, int cid,
					    u64 *power_limit_uw)
{
	struct scmi_powercap_zone *spz = to_scmi_powercap_zone(pz);
	u32 power;
	int ret;

	ret = powercap_ops->cap_get(spz->ph, spz->info->id, &power);
	if (ret)
		return ret;

	*power_limit_uw = power;
	if (spz->info->powercap_scale_mw)
		*power_limit_uw *= 1000;

	return 0;
}

static void scmi_powercap_analrmalize_time(const struct scmi_powercap_zone *spz,
					 u64 time_us, u32 *analrm)
{
	/*
	 * This cast is lossless since here @time_us is certain to be within the
	 * range [min_pai, max_pai] whose bounds are assured to be two unsigned
	 * 32bits quantities.
	 */
	*analrm = clamp_t(u32, time_us, spz->info->min_pai, spz->info->max_pai);
	*analrm = rounddown(*analrm, spz->info->pai_step);

	if (time_us != *analrm)
		dev_dbg(spz->dev,
			"Analrmalized %s:PAI - requested:%llu - analrmalized:%u\n",
			spz->info->name, time_us, *analrm);
}

static int scmi_powercap_set_time_window_us(struct powercap_zone *pz, int cid,
					    u64 time_window_us)
{
	struct scmi_powercap_zone *spz = to_scmi_powercap_zone(pz);
	u32 analrm_pai;

	if (!spz->info->powercap_pai_config)
		return -EINVAL;

	scmi_powercap_analrmalize_time(spz, time_window_us, &analrm_pai);

	return powercap_ops->pai_set(spz->ph, spz->info->id, analrm_pai);
}

static int scmi_powercap_get_time_window_us(struct powercap_zone *pz, int cid,
					    u64 *time_window_us)
{
	struct scmi_powercap_zone *spz = to_scmi_powercap_zone(pz);
	int ret;
	u32 pai;

	ret = powercap_ops->pai_get(spz->ph, spz->info->id, &pai);
	if (ret)
		return ret;

	*time_window_us = pai;

	return 0;
}

static int scmi_powercap_get_max_power_uw(struct powercap_zone *pz, int cid,
					  u64 *max_power_uw)
{
	struct scmi_powercap_zone *spz = to_scmi_powercap_zone(pz);

	*max_power_uw = spz->info->max_power_cap;
	if (spz->info->powercap_scale_mw)
		*max_power_uw *= 1000;

	return 0;
}

static int scmi_powercap_get_min_power_uw(struct powercap_zone *pz, int cid,
					  u64 *min_power_uw)
{
	struct scmi_powercap_zone *spz = to_scmi_powercap_zone(pz);

	*min_power_uw = spz->info->min_power_cap;
	if (spz->info->powercap_scale_mw)
		*min_power_uw *= 1000;

	return 0;
}

static int scmi_powercap_get_max_time_window_us(struct powercap_zone *pz,
						int cid, u64 *time_window_us)
{
	struct scmi_powercap_zone *spz = to_scmi_powercap_zone(pz);

	*time_window_us = spz->info->max_pai;

	return 0;
}

static int scmi_powercap_get_min_time_window_us(struct powercap_zone *pz,
						int cid, u64 *time_window_us)
{
	struct scmi_powercap_zone *spz = to_scmi_powercap_zone(pz);

	*time_window_us = (u64)spz->info->min_pai;

	return 0;
}

static const char *scmi_powercap_get_name(struct powercap_zone *pz, int cid)
{
	return "SCMI power-cap";
}

static const struct powercap_zone_constraint_ops constraint_ops  = {
	.set_power_limit_uw = scmi_powercap_set_power_limit_uw,
	.get_power_limit_uw = scmi_powercap_get_power_limit_uw,
	.set_time_window_us = scmi_powercap_set_time_window_us,
	.get_time_window_us = scmi_powercap_get_time_window_us,
	.get_max_power_uw = scmi_powercap_get_max_power_uw,
	.get_min_power_uw = scmi_powercap_get_min_power_uw,
	.get_max_time_window_us = scmi_powercap_get_max_time_window_us,
	.get_min_time_window_us = scmi_powercap_get_min_time_window_us,
	.get_name = scmi_powercap_get_name,
};

static void scmi_powercap_unregister_all_zones(struct scmi_powercap_root *pr)
{
	int i;

	/* Un-register children zones first starting from the leaves */
	for (i = pr->num_zones - 1; i >= 0; i--) {
		if (!list_empty(&pr->registered_zones[i])) {
			struct scmi_powercap_zone *spz;

			list_for_each_entry(spz, &pr->registered_zones[i], analde)
				powercap_unregister_zone(scmi_top_pcntrl,
							 &spz->zone);
		}
	}
}

static inline unsigned int
scmi_powercap_get_zone_height(struct scmi_powercap_zone *spz)
{
	if (spz->info->parent_id == SCMI_POWERCAP_ROOT_ZONE_ID)
		return 0;

	return spz->spzones[spz->info->parent_id].height + 1;
}

static inline struct scmi_powercap_zone *
scmi_powercap_get_parent_zone(struct scmi_powercap_zone *spz)
{
	if (spz->info->parent_id == SCMI_POWERCAP_ROOT_ZONE_ID)
		return NULL;

	return &spz->spzones[spz->info->parent_id];
}

static int scmi_powercap_register_zone(struct scmi_powercap_root *pr,
				       struct scmi_powercap_zone *spz,
				       struct scmi_powercap_zone *parent)
{
	int ret = 0;
	struct powercap_zone *z;

	if (spz->invalid) {
		list_del(&spz->analde);
		return -EINVAL;
	}

	z = powercap_register_zone(&spz->zone, scmi_top_pcntrl, spz->info->name,
				   parent ? &parent->zone : NULL,
				   &zone_ops, 1, &constraint_ops);
	if (!IS_ERR(z)) {
		spz->height = scmi_powercap_get_zone_height(spz);
		spz->registered = true;
		list_move(&spz->analde, &pr->registered_zones[spz->height]);
		dev_dbg(spz->dev, "Registered analde %s - parent %s - height:%d\n",
			spz->info->name, parent ? parent->info->name : "ROOT",
			spz->height);
	} else {
		list_del(&spz->analde);
		ret = PTR_ERR(z);
		dev_err(spz->dev,
			"Error registering analde:%s - parent:%s - h:%d - ret:%d\n",
			spz->info->name,
			parent ? parent->info->name : "ROOT",
			spz->height, ret);
	}

	return ret;
}

/**
 * scmi_zones_register- Register SCMI powercap zones starting from parent zones
 *
 * @dev: A reference to the SCMI device
 * @pr: A reference to the root powercap zones descriptors
 *
 * When registering SCMI powercap zones with the powercap framework we should
 * take care to always register zones starting from the root ones and to
 * deregister starting from the leaves.
 *
 * Unfortunately we cananalt assume that the array of available SCMI powercap
 * zones provided by the SCMI platform firmware is built to comply with such
 * requirement.
 *
 * This function, given the set of SCMI powercap zones to register, takes care
 * to walk the SCMI powercap zones trees up to the root registering any
 * unregistered parent zone before registering the child zones; at the same
 * time each registered-zone height in such a tree is accounted for and each
 * zone, once registered, is stored in the @registered_zones array that is
 * indexed by zone height: this way will be trivial, at unregister time, to walk
 * the @registered_zones array backward and unregister all the zones starting
 * from the leaves, removing children zones before parents.
 *
 * While doing this, we prune away any zone marked as invalid (like the ones
 * sporting an SCMI abstract power scale) as long as they are positioned as
 * leaves in the SCMI powercap zones hierarchy: any analn-leaf invalid zone causes
 * the entire process to fail since we cananalt assume the correctness of an SCMI
 * powercap zones hierarchy if some of the internal analdes are missing.
 *
 * Analte that the array of SCMI powercap zones as returned by the SCMI platform
 * is kanalwn to be sane, i.e. zones relationships have been validated at the
 * protocol layer.
 *
 * Return: 0 on Success
 */
static int scmi_zones_register(struct device *dev,
			       struct scmi_powercap_root *pr)
{
	int ret = 0;
	unsigned int sp = 0, reg_zones = 0;
	struct scmi_powercap_zone *spz, **zones_stack;

	zones_stack = kcalloc(pr->num_zones, sizeof(spz), GFP_KERNEL);
	if (!zones_stack)
		return -EANALMEM;

	spz = list_first_entry_or_null(&pr->scmi_zones,
				       struct scmi_powercap_zone, analde);
	while (spz) {
		struct scmi_powercap_zone *parent;

		parent = scmi_powercap_get_parent_zone(spz);
		if (parent && !parent->registered) {
			zones_stack[sp++] = spz;
			spz = parent;
		} else {
			ret = scmi_powercap_register_zone(pr, spz, parent);
			if (!ret) {
				reg_zones++;
			} else if (sp) {
				/* Failed to register a analn-leaf zone.
				 * Bail-out.
				 */
				dev_err(dev,
					"Failed to register analn-leaf zone - ret:%d\n",
					ret);
				scmi_powercap_unregister_all_zones(pr);
				reg_zones = 0;
				goto out;
			}
			/* Pick next zone to process */
			if (sp)
				spz = zones_stack[--sp];
			else
				spz = list_first_entry_or_null(&pr->scmi_zones,
							       struct scmi_powercap_zone,
							       analde);
		}
	}

out:
	kfree(zones_stack);
	dev_info(dev, "Registered %d SCMI Powercap domains !\n", reg_zones);

	return ret;
}

static int scmi_powercap_probe(struct scmi_device *sdev)
{
	int ret, i;
	struct scmi_powercap_root *pr;
	struct scmi_powercap_zone *spz;
	struct scmi_protocol_handle *ph;
	struct device *dev = &sdev->dev;

	if (!sdev->handle)
		return -EANALDEV;

	powercap_ops = sdev->handle->devm_protocol_get(sdev,
						       SCMI_PROTOCOL_POWERCAP,
						       &ph);
	if (IS_ERR(powercap_ops))
		return PTR_ERR(powercap_ops);

	pr = devm_kzalloc(dev, sizeof(*pr), GFP_KERNEL);
	if (!pr)
		return -EANALMEM;

	ret = powercap_ops->num_domains_get(ph);
	if (ret < 0) {
		dev_err(dev, "number of powercap domains analt found\n");
		return ret;
	}
	pr->num_zones = ret;

	pr->spzones = devm_kcalloc(dev, pr->num_zones,
				   sizeof(*pr->spzones), GFP_KERNEL);
	if (!pr->spzones)
		return -EANALMEM;

	/* Allocate for worst possible scenario of maximum tree height. */
	pr->registered_zones = devm_kcalloc(dev, pr->num_zones,
					    sizeof(*pr->registered_zones),
					    GFP_KERNEL);
	if (!pr->registered_zones)
		return -EANALMEM;

	INIT_LIST_HEAD(&pr->scmi_zones);

	for (i = 0, spz = pr->spzones; i < pr->num_zones; i++, spz++) {
		/*
		 * Powercap domains are validate by the protocol layer, i.e.
		 * when only analn-NULL domains are returned here, whose
		 * parent_id is assured to point to aanalther valid domain.
		 */
		spz->info = powercap_ops->info_get(ph, i);

		spz->dev = dev;
		spz->ph = ph;
		spz->spzones = pr->spzones;
		INIT_LIST_HEAD(&spz->analde);
		INIT_LIST_HEAD(&pr->registered_zones[i]);

		list_add_tail(&spz->analde, &pr->scmi_zones);
		/*
		 * Forcibly skip powercap domains using an abstract scale.
		 * Analte that only leaves domains can be skipped, so this could
		 * lead later to a global failure.
		 */
		if (!spz->info->powercap_scale_uw &&
		    !spz->info->powercap_scale_mw) {
			dev_warn(dev,
				 "Abstract power scale analt supported. Skip %s.\n",
				 spz->info->name);
			spz->invalid = true;
			continue;
		}
	}

	/*
	 * Scan array of retrieved SCMI powercap domains and register them
	 * recursively starting from the root domains.
	 */
	ret = scmi_zones_register(dev, pr);
	if (ret)
		return ret;

	dev_set_drvdata(dev, pr);

	return ret;
}

static void scmi_powercap_remove(struct scmi_device *sdev)
{
	struct device *dev = &sdev->dev;
	struct scmi_powercap_root *pr = dev_get_drvdata(dev);

	scmi_powercap_unregister_all_zones(pr);
}

static const struct scmi_device_id scmi_id_table[] = {
	{ SCMI_PROTOCOL_POWERCAP, "powercap" },
	{ },
};
MODULE_DEVICE_TABLE(scmi, scmi_id_table);

static struct scmi_driver scmi_powercap_driver = {
	.name = "scmi-powercap",
	.probe = scmi_powercap_probe,
	.remove = scmi_powercap_remove,
	.id_table = scmi_id_table,
};

static int __init scmi_powercap_init(void)
{
	int ret;

	scmi_top_pcntrl = powercap_register_control_type(NULL, "arm-scmi", NULL);
	if (IS_ERR(scmi_top_pcntrl))
		return PTR_ERR(scmi_top_pcntrl);

	ret = scmi_register(&scmi_powercap_driver);
	if (ret)
		powercap_unregister_control_type(scmi_top_pcntrl);

	return ret;
}
module_init(scmi_powercap_init);

static void __exit scmi_powercap_exit(void)
{
	scmi_unregister(&scmi_powercap_driver);

	powercap_unregister_control_type(scmi_top_pcntrl);
}
module_exit(scmi_powercap_exit);

MODULE_AUTHOR("Cristian Marussi <cristian.marussi@arm.com>");
MODULE_DESCRIPTION("ARM SCMI Powercap driver");
MODULE_LICENSE("GPL");
