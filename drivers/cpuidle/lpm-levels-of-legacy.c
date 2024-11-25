// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2011-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/err.h>
#include <linux/sysfs.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/moduleparam.h>
#include "lpm-levels-legacy.h"

bool use_psci;
enum lpm_type {
	IDLE = 0,
	SUSPEND,
	LPM_TYPE_NR
};

struct lpm_type_str {
	enum lpm_type type;
	char *str;
};

static const struct lpm_type_str lpm_types[] = {
	{IDLE, "idle_enabled"},
	{SUSPEND, "suspend_enabled"},
};

static DEFINE_PER_CPU(uint32_t *, max_residency);
static struct lpm_level_avail *cpu_level_available[NR_CPUS];
static struct platform_device *lpm_pdev;

static void *get_enabled_ptr(struct kobj_attribute *attr,
					struct lpm_level_avail *avail)
{
	void *arg = NULL;

	if (!strcmp(attr->attr.name, lpm_types[IDLE].str))
		arg = (void *) &avail->idle_enabled;
	else if (!strcmp(attr->attr.name, lpm_types[SUSPEND].str))
		arg = (void *) &avail->suspend_enabled;

	return arg;
}

static struct lpm_level_avail *get_avail_ptr(struct kobject *kobj,
					struct kobj_attribute *attr)
{
	struct lpm_level_avail *avail = NULL;

	if (!strcmp(attr->attr.name, lpm_types[IDLE].str))
		avail = container_of(attr, struct lpm_level_avail,
					idle_enabled_attr);
	else if (!strcmp(attr->attr.name, lpm_types[SUSPEND].str))
		avail = container_of(attr, struct lpm_level_avail,
					suspend_enabled_attr);

	return avail;
}

static void set_optimum_cpu_residency(struct lpm_cpu *cpu, int cpu_id,
		bool probe_time)
{
	int i, j;
	bool mode_avail;
	uint32_t *residency = per_cpu(max_residency, cpu_id);

	for (i = 0; i < cpu->nlevels; i++) {
		struct power_params *pwr = &cpu->levels[i].pwr;

		mode_avail = probe_time ||
			lpm_cpu_mode_allow(cpu_id, i, true);

		if (!mode_avail) {
			residency[i] = 0;
			continue;
		}

		residency[i] = ~0;
		for (j = i + 1; j < cpu->nlevels; j++) {
			mode_avail = probe_time ||
					lpm_cpu_mode_allow(cpu_id, j, true);

			if (mode_avail &&
				(residency[i] > pwr->residencies[j]) &&
				(pwr->residencies[j] != 0))
				residency[i] = pwr->residencies[j];
		}
	}
}

static void set_optimum_cluster_residency(struct lpm_cluster *cluster,
		bool probe_time)
{
	int i, j;
	bool mode_avail;

	for (i = 0; i < cluster->nlevels; i++) {
		struct power_params *pwr = &cluster->levels[i].pwr;

		mode_avail = probe_time ||
			lpm_cluster_mode_allow(cluster, i,
					true);

		if (!mode_avail) {
			pwr->max_residency = 0;
			continue;
		}

		pwr->max_residency = ~0;
		for (j = i+1; j < cluster->nlevels; j++) {
			mode_avail = probe_time ||
					lpm_cluster_mode_allow(cluster, j,
							true);
			if (mode_avail &&
				(pwr->max_residency > pwr->residencies[j]) &&
				(pwr->residencies[j] != 0))
				pwr->max_residency = pwr->residencies[j];
		}
	}
}

uint32_t *get_per_cpu_max_residency(int cpu)
{
	return per_cpu(max_residency, cpu);
}

static ssize_t lpm_enable_show(struct kobject *kobj,
			struct kobj_attribute *attr, char *buf)
{
	int ret = 0;
	struct kernel_param kp;

	kp.arg = get_enabled_ptr(attr, get_avail_ptr(kobj, attr));
	ret = param_get_bool(buf, &kp);
	if (ret > 0) {
		strlcat(buf, "\n", PAGE_SIZE);
		ret++;
	}

	return ret;
}

static ssize_t lpm_enable_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t len)
{
	int ret = 0;
	struct kernel_param kp;
	struct lpm_level_avail *avail;

	avail = get_avail_ptr(kobj, attr);
	if (WARN_ON(!avail))
		return -EINVAL;

	kp.arg = get_enabled_ptr(attr, avail);
	ret = param_set_bool(buf, &kp);

	if (avail->cpu_node)
		set_optimum_cpu_residency(avail->data, avail->idx, false);
	else
		set_optimum_cluster_residency(avail->data, false);

	return ret ? ret : len;
}

static int create_lvl_avail_nodes(const char *name,
			struct kobject *parent, struct lpm_level_avail *avail,
			void *data, int index, bool cpu_node)
{
	struct attribute_group *attr_group = NULL;
	struct attribute **attr = NULL;
	struct kobject *kobj = NULL;
	int ret = 0;

	kobj = kobject_create_and_add(name, parent);
	if (!kobj)
		return -ENOMEM;

	attr_group = devm_kzalloc(&lpm_pdev->dev, sizeof(*attr_group),
					GFP_KERNEL);
	if (!attr_group) {
		ret = -ENOMEM;
		goto failed;
	}

	attr = devm_kzalloc(&lpm_pdev->dev,
		sizeof(*attr) * (LPM_TYPE_NR + 1), GFP_KERNEL);
	if (!attr) {
		ret = -ENOMEM;
		goto failed;
	}

	sysfs_attr_init(&avail->idle_enabled_attr.attr);
	avail->idle_enabled_attr.attr.name = lpm_types[IDLE].str;
	avail->idle_enabled_attr.attr.mode = 0644;
	avail->idle_enabled_attr.show = lpm_enable_show;
	avail->idle_enabled_attr.store = lpm_enable_store;

	sysfs_attr_init(&avail->suspend_enabled_attr.attr);
	avail->suspend_enabled_attr.attr.name = lpm_types[SUSPEND].str;
	avail->suspend_enabled_attr.attr.mode = 0644;
	avail->suspend_enabled_attr.show = lpm_enable_show;
	avail->suspend_enabled_attr.store = lpm_enable_store;

	attr[0] = &avail->idle_enabled_attr.attr;
	attr[1] = &avail->suspend_enabled_attr.attr;
	attr[2] = NULL;
	attr_group->attrs = attr;

	ret = sysfs_create_group(kobj, attr_group);
	if (ret) {
		ret = -ENOMEM;
		goto failed;
	}

	avail->idle_enabled = true;
	avail->suspend_enabled = true;
	avail->kobj = kobj;
	avail->data = data;
	avail->idx = index;
	avail->cpu_node = cpu_node;

	return ret;

failed:
	kobject_put(kobj);
	return ret;
}

static int create_cpu_lvl_nodes(struct lpm_cluster *p, struct kobject *parent)
{
	int cpu;
	int i, cpu_idx;
	struct kobject **cpu_kobj = NULL;
	struct lpm_level_avail *level_list = NULL;
	char cpu_name[20] = {0};
	int ret = 0;

	cpu_kobj = devm_kzalloc(&lpm_pdev->dev, sizeof(*cpu_kobj) *
			cpumask_weight(&p->child_cpus), GFP_KERNEL);
	if (!cpu_kobj)
		return -ENOMEM;

	cpu_idx = 0;
	for_each_cpu(cpu, &p->child_cpus) {
		snprintf(cpu_name, sizeof(cpu_name), "cpu%d", cpu);
		cpu_kobj[cpu_idx] = kobject_create_and_add(cpu_name, parent);
		if (!cpu_kobj[cpu_idx]) {
			ret = -ENOMEM;
			goto release_kobj;
		}

		level_list = devm_kzalloc(&lpm_pdev->dev,
				p->cpu->nlevels * sizeof(*level_list),
				GFP_KERNEL);
		if (!level_list) {
			ret = -ENOMEM;
			goto release_kobj;
		}

		for (i = 0; i < p->cpu->nlevels; i++) {

			ret = create_lvl_avail_nodes(p->cpu->levels[i].name,
					cpu_kobj[cpu_idx], &level_list[i],
					(void *)p->cpu, cpu, true);
			if (ret)
				goto release_kobj;
		}

		cpu_level_available[cpu] = level_list;
		cpu_idx++;
	}

	return ret;

release_kobj:
	for (i = 0; i < cpumask_weight(&p->child_cpus); i++)
		kobject_put(cpu_kobj[i]);

	return ret;
}

int create_cluster_lvl_nodes(struct lpm_cluster *p, struct kobject *kobj)
{
	int ret = 0;
	struct lpm_cluster *child = NULL;
	int i;
	struct kobject *cluster_kobj = NULL;

	if (!p)
		return -ENODEV;

	cluster_kobj = kobject_create_and_add(p->cluster_name, kobj);
	if (!cluster_kobj)
		return -ENOMEM;

	for (i = 0; i < p->nlevels; i++) {
		ret = create_lvl_avail_nodes(p->levels[i].level_name,
				cluster_kobj, &p->levels[i].available,
				(void *)p, 0, false);
		if (ret)
			return ret;
	}

	list_for_each_entry(child, &p->child, list) {
		ret = create_cluster_lvl_nodes(child, cluster_kobj);
		if (ret)
			return ret;
	}

	if (p->cpu) {
		ret = create_cpu_lvl_nodes(p, cluster_kobj);
		if (ret)
			return ret;
	}

	return 0;
}

bool lpm_cpu_mode_allow(unsigned int cpu,
		unsigned int index, bool from_idle)
{
	struct lpm_level_avail *avail = cpu_level_available[cpu];

	if (!lpm_pdev || !avail)
		return !from_idle;

	return !!(from_idle ? avail[index].idle_enabled :
				avail[index].suspend_enabled);
}

bool lpm_cluster_mode_allow(struct lpm_cluster *cluster,
		unsigned int mode, bool from_idle)
{
	struct lpm_level_avail *avail = &cluster->levels[mode].available;

	if (!lpm_pdev || !avail)
		return false;

	return !!(from_idle ? avail->idle_enabled :
				avail->suspend_enabled);
}

static int parse_legacy_cluster_params(struct device_node *node,
		struct lpm_cluster *c)
{
	int i;
	char *key;
	int ret;
	struct lpm_match {
		char *devname;
		int (*set_mode)(struct low_power_ops *ops, int mode,
					struct lpm_cluster_level *level);
	};
	struct lpm_match match_tbl[] = {
		{"l2", set_l2_mode},
		{"cci", set_system_mode},
		{"l3", set_l3_mode},
		{"cbf", set_system_mode},
	};


	key = "qcom,spm-device-names";
	c->ndevices = of_property_count_strings(node, key);

	if (c->ndevices < 0) {
		pr_info("%s(): Ignoring cluster params\n", __func__);
		c->no_saw_devices = true;
		c->ndevices = 0;
		return 0;
	}

	c->name = devm_kzalloc(&lpm_pdev->dev, c->ndevices * sizeof(*c->name),
				GFP_KERNEL);
	c->lpm_dev = devm_kzalloc(&lpm_pdev->dev,
				c->ndevices * sizeof(*c->lpm_dev),
				GFP_KERNEL);
	if (!c->name || !c->lpm_dev) {
		ret = -ENOMEM;
		goto failed;
	}

	for (i = 0; i < c->ndevices; i++) {
		char device_name[20];
		int j;

		ret = of_property_read_string_index(node, key, i, &c->name[i]);
		if (ret)
			goto failed;
		snprintf(device_name, sizeof(device_name), "%s-%s",
				c->cluster_name, c->name[i]);

		c->lpm_dev[i].spm = msm_spm_get_device_by_name(device_name);

		if (IS_ERR_OR_NULL(c->lpm_dev[i].spm)) {
			pr_err("Failed to get spm device by name:%s\n",
					device_name);
			ret = PTR_ERR(c->lpm_dev[i].spm);
			goto failed;
		}
		for (j = 0; j < ARRAY_SIZE(match_tbl); j++) {
			if (!strcmp(c->name[i], match_tbl[j].devname))
				c->lpm_dev[i].set_mode = match_tbl[j].set_mode;
		}

		if (!c->lpm_dev[i].set_mode) {
			ret = -ENODEV;
			goto failed;
		}
	}

	key = "qcom,default-level";
	if (of_property_read_u32(node, key, &c->default_level))
		c->default_level = 0;
	return 0;
failed:
	pr_err("%s(): Failed reading %s\n", __func__, key);
	return ret;
}

static int parse_cluster_params(struct device_node *node,
		struct lpm_cluster *c)
{
	char *key;
	int ret;

	key = "label";
	ret = of_property_read_string(node, key, &c->cluster_name);
	if (ret) {
		pr_err("%s(): Cannot read required param %s\n", __func__, key);
		return ret;
	}

	if (use_psci) {
		key = "qcom,psci-mode-shift";
		ret = of_property_read_u32(node, key,
				&c->psci_mode_shift);
		if (ret) {
			pr_err("%s(): Failed to read param: %s\n",
							__func__, key);
			return ret;
		}

		key = "qcom,psci-mode-mask";
		ret = of_property_read_u32(node, key,
				&c->psci_mode_mask);
		if (ret) {
			pr_err("%s(): Failed to read param: %s\n",
							__func__, key);
			return ret;
		}

		/* Set ndevice to 1 as default */
		c->ndevices = 1;

		return 0;
	} else
		return parse_legacy_cluster_params(node, c);
}

static int parse_lpm_mode(const char *str)
{
	int i;
	struct lpm_lookup_table mode_lookup[] = {
		{MSM_SPM_MODE_POWER_COLLAPSE, "pc"},
		{MSM_SPM_MODE_STANDALONE_POWER_COLLAPSE, "spc"},
		{MSM_SPM_MODE_FASTPC, "fpc"},
		{MSM_SPM_MODE_GDHS, "gdhs"},
		{MSM_SPM_MODE_RETENTION, "retention"},
		{MSM_SPM_MODE_CLOCK_GATING, "wfi"},
		{MSM_SPM_MODE_DISABLED, "active"}
	};

	for (i = 0; i < ARRAY_SIZE(mode_lookup); i++)
		if (!strcmp(str, mode_lookup[i].mode_name))
			return  mode_lookup[i].modes;
	return -EINVAL;
}

static int parse_power_params(struct device_node *node,
		struct power_params *pwr)
{
	char *key;
	int ret;

	key = "qcom,latency-us";
	ret  = of_property_read_u32(node, key, &pwr->latency_us);
	if (ret)
		goto fail;

	key = "qcom,ss-power";
	ret = of_property_read_u32(node, key, &pwr->ss_power);
	if (ret)
		goto fail;

	key = "qcom,energy-overhead";
	ret = of_property_read_u32(node, key, &pwr->energy_overhead);
	if (ret)
		goto fail;

	key = "qcom,time-overhead";
	ret = of_property_read_u32(node, key, &pwr->time_overhead_us);
	if (ret)
		goto fail;

fail:
	if (ret)
		pr_err("%s(): %s Error reading %s\n", __func__, node->name,
				key);
	return ret;
}

static int parse_cluster_level(struct device_node *node,
		struct lpm_cluster *cluster)
{
	int i = 0;
	struct lpm_cluster_level *level = &cluster->levels[cluster->nlevels];
	int ret = -ENOMEM;
	char *key;

	key = "label";
	ret = of_property_read_string(node, key, &level->level_name);
	if (ret)
		goto failed;

	if (use_psci) {
		char *k = "qcom,psci-mode";

		ret = of_property_read_u32(node, k, &level->psci_id);
		if (ret)
			goto failed;

		level->is_reset = of_property_read_bool(node, "qcom,is-reset");
	} else if (!cluster->no_saw_devices) {
		key  = "no saw-devices";

		level->mode = devm_kzalloc(&lpm_pdev->dev,
				cluster->ndevices * sizeof(*level->mode),
				GFP_KERNEL);
		if (!level->mode) {
			pr_err("Memory allocation failed\n");
			goto failed;
		}

		for (i = 0; i < cluster->ndevices; i++) {
			const char *spm_mode;
			char key[25] = {0};

			snprintf(key, 25, "qcom,spm-%s-mode", cluster->name[i]);
			ret = of_property_read_string(node, key, &spm_mode);
			if (ret)
				goto failed;

			level->mode[i] = parse_lpm_mode(spm_mode);

			if (level->mode[i] < 0)
				goto failed;

			if (level->mode[i] == MSM_SPM_MODE_POWER_COLLAPSE
				|| level->mode[i] ==
				MSM_SPM_MODE_STANDALONE_POWER_COLLAPSE)
				level->is_reset |= true;
		}
	}

	key = "label";
	ret = of_property_read_string(node, key, &level->level_name);
	if (ret)
		goto failed;

	if (cluster->nlevels != cluster->default_level) {
		key = "min child idx";
		ret = of_property_read_u32(node, "qcom,min-child-idx",
				&level->min_child_level);
		if (ret)
			goto failed;

		if (cluster->min_child_level > level->min_child_level)
			cluster->min_child_level = level->min_child_level;
	}

	level->notify_rpm = of_property_read_bool(node, "qcom,notify-rpm");
	level->disable_dynamic_routing = of_property_read_bool(node,
					"qcom,disable-dynamic-int-routing");
	level->last_core_only = of_property_read_bool(node,
					"qcom,last-core-only");
	level->no_cache_flush = of_property_read_bool(node,
					"qcom,no-cache-flush");

	key = "parse_power_params";
	ret = parse_power_params(node, &level->pwr);
	if (ret)
		goto failed;

	key = "qcom,reset-level";
	ret = of_property_read_u32(node, key, &level->reset_level);
	if (ret == -EINVAL)
		level->reset_level = LPM_RESET_LVL_NONE;
	else if (ret)
		goto failed;

	cluster->nlevels++;
	return 0;
failed:
	pr_err("Failed %s() key = %s ret = %d\n", __func__, key, ret);
	return ret;
}

static int parse_cpu_spm_mode(const char *mode_name)
{
	struct lpm_lookup_table pm_sm_lookup[] = {
		{MSM_PM_SLEEP_MODE_WAIT_FOR_INTERRUPT,
			"wfi"},
		{MSM_PM_SLEEP_MODE_POWER_COLLAPSE_STANDALONE,
			"standalone_pc"},
		{MSM_PM_SLEEP_MODE_POWER_COLLAPSE,
			"pc"},
		{MSM_PM_SLEEP_MODE_RETENTION,
			"retention"},
		{MSM_PM_SLEEP_MODE_FASTPC,
			"fpc"},
	};
	int i;
	int ret = -EINVAL;

	for (i = 0; i < ARRAY_SIZE(pm_sm_lookup); i++) {
		if (!strcmp(mode_name, pm_sm_lookup[i].mode_name)) {
			ret = pm_sm_lookup[i].modes;
			break;
		}
	}
	return ret;
}

static int parse_cpu_mode(struct device_node *n, struct lpm_cpu_level *l)
{
	char *key;
	int ret;

	key = "qcom,spm-cpu-mode";
	ret  =  of_property_read_string(n, key, &l->name);
	if (ret) {
		pr_err("Failed %s %d\n", n->name, __LINE__);
		return ret;
	}

	if (use_psci) {
		key = "qcom,psci-cpu-mode";

		ret = of_property_read_u32(n, key, &l->psci_id);
		if (ret) {
			pr_err("Failed reading %s on device %s\n", key,
					n->name);
			return ret;
		}
		key = "qcom,hyp-psci";

		l->hyp_psci = of_property_read_bool(n, key);
	} else {
		l->mode = parse_cpu_spm_mode(l->name);

		if (l->mode < 0)
			return l->mode;
	}
	return 0;

}

static int get_cpumask_for_node(struct device_node *node, struct cpumask *mask)
{
	struct device_node *cpu_node;
	int cpu;
	int idx = 0;

	cpu_node = of_parse_phandle(node, "qcom,cpu", idx++);
	if (!cpu_node) {
		pr_info("%s: No CPU phandle, assuming single cluster\n",
				node->full_name);
		/*
		 * Not all targets have the cpu node populated in the device
		 * tree. If cpu node is not populated assume all possible
		 * nodes belong to this cluster
		 */
		cpumask_copy(mask, cpu_possible_mask);
		return 0;
	}

	while (cpu_node) {
		for_each_possible_cpu(cpu) {
			if (of_get_cpu_node(cpu, NULL) == cpu_node) {
				cpumask_set_cpu(cpu, mask);
				break;
			}
		}
		of_node_put(cpu_node);
		cpu_node = of_parse_phandle(node, "qcom,cpu", idx++);
	}

	return 0;
}

static int calculate_residency(struct power_params *base_pwr,
					struct power_params *next_pwr)
{
	int32_t residency = (int32_t)(next_pwr->energy_overhead -
						base_pwr->energy_overhead) -
		((int32_t)(next_pwr->ss_power * next_pwr->time_overhead_us)
		- (int32_t)(base_pwr->ss_power * base_pwr->time_overhead_us));

	residency /= (int32_t)(base_pwr->ss_power  - next_pwr->ss_power);

	if (residency < 0) {
		pr_err("%s: residency < 0 for LPM\n",
				__func__);
		return next_pwr->time_overhead_us;
	}

	return residency < next_pwr->time_overhead_us ?
				next_pwr->time_overhead_us : residency;
}

static int parse_cpu_levels(struct device_node *node, struct lpm_cluster *c)
{
	struct device_node *n;
	int ret = -ENOMEM;
	int i, j;
	char *key;

	c->cpu = devm_kzalloc(&lpm_pdev->dev, sizeof(*c->cpu), GFP_KERNEL);
	if (!c->cpu)
		return ret;

	c->cpu->parent = c;
	if (use_psci) {

		key = "qcom,psci-mode-shift";

		ret = of_property_read_u32(node, key, &c->cpu->psci_mode_shift);
		if (ret) {
			pr_err("Failed reading %s on device %s\n", key,
					node->name);
			return ret;
		}
		key = "qcom,psci-mode-mask";

		ret = of_property_read_u32(node, key, &c->cpu->psci_mode_mask);
		if (ret) {
			pr_err("Failed reading %s on device %s\n", key,
					node->name);
			return ret;
		}
	}
	for_each_child_of_node(node, n) {
		struct lpm_cpu_level *l = &c->cpu->levels[c->cpu->nlevels];

		c->cpu->nlevels++;

		ret = parse_cpu_mode(n, l);
		if (ret < 0) {
			pr_info("Failed %s\n", l->name);
			goto failed;
		}

		ret = parse_power_params(n, &l->pwr);
		if (ret)
			goto failed;

		key = "qcom,use-broadcast-timer";
		l->use_bc_timer = of_property_read_bool(n, key);

		l->is_reset = of_property_read_bool(n, "qcom,is-reset");

		key = "qcom,jtag-save-restore";
		l->jtag_save_restore = of_property_read_bool(n, key);

		key = "qcom,reset-level";
		ret = of_property_read_u32(n, key, &l->reset_level);
		if (ret == -EINVAL)
			l->reset_level = LPM_RESET_LVL_NONE;
		else if (ret)
			goto failed;
	}
	for (i = 0; i < c->cpu->nlevels; i++) {
		for (j = 0; j < c->cpu->nlevels; j++) {
			if (i >= j) {
				c->cpu->levels[i].pwr.residencies[j] = 0;
				continue;
			}

			c->cpu->levels[i].pwr.residencies[j] =
				calculate_residency(&c->cpu->levels[i].pwr,
					&c->cpu->levels[j].pwr);

			pr_err("%s: idx %d %u\n", __func__, j,
					c->cpu->levels[i].pwr.residencies[j]);
		}
	}

	return 0;
failed:
	of_node_put(n);
	pr_err("%s(): Failed with error code:%d\n", __func__, ret);
	return ret;
}

void free_cluster_node(struct lpm_cluster *cluster)
{
	struct lpm_cluster *cl, *m;

	list_for_each_entry_safe(cl, m, &cluster->child, list) {
		list_del(&cl->list);
		free_cluster_node(cl);
	}

	cluster->ndevices = 0;
}

/*
 * TODO:
 * Expects a CPU or a cluster only. This ensures that affinity
 * level of a cluster is consistent with reference to its
 * child nodes.
 */
static struct lpm_cluster *parse_cluster(struct device_node *node,
		struct lpm_cluster *parent)
{
	struct lpm_cluster *c;
	struct device_node *n;
	char *key;
	int ret = 0;
	int i, j;

	c = devm_kzalloc(&lpm_pdev->dev, sizeof(*c), GFP_KERNEL);
	if (!c)
		return ERR_PTR(-ENOMEM);

	ret = parse_cluster_params(node, c);

	if (ret)
		goto failed_parse_params;

	INIT_LIST_HEAD(&c->child);
	c->parent = parent;
	spin_lock_init(&c->sync_lock);
	c->min_child_level = NR_LPM_LEVELS;

	for_each_child_of_node(node, n) {

		if (!n->name)
			continue;
		key = "qcom,pm-cluster-level";
		if (!of_node_cmp(n->name, key)) {
			if (parse_cluster_level(n, c))
				goto failed_parse_cluster;

			continue;
		}

		key = "qcom,pm-cluster";
		if (!of_node_cmp(n->name, key)) {
			struct lpm_cluster *child;

			if (c->no_saw_devices)
				pr_info("%s: SAW device not provided.\n",
					__func__);

			child = parse_cluster(n, c);
			if (!child)
				goto failed_parse_cluster;

			list_add(&child->list, &c->child);
			cpumask_or(&c->child_cpus, &c->child_cpus,
					&child->child_cpus);
			c->aff_level = child->aff_level + 1;
			continue;
		}

		key = "qcom,pm-cpu";
		if (!of_node_cmp(n->name, key)) {
			/*
			 * Parse the cpu node only if a pm-cpu node
			 * is available, though the mask is defined @ the
			 * cluster level
			 */
			if (get_cpumask_for_node(node, &c->child_cpus))
				goto failed_parse_cluster;

			if (parse_cpu_levels(n, c))
				goto failed_parse_cluster;

			c->aff_level = 1;

			for_each_cpu(i, &c->child_cpus) {
				per_cpu(max_residency, i) = devm_kzalloc(
					&lpm_pdev->dev,
					sizeof(uint32_t) * c->cpu->nlevels,
					GFP_KERNEL);
				if (!per_cpu(max_residency, i))
					return ERR_PTR(-ENOMEM);
				set_optimum_cpu_residency(c->cpu, i, true);
			}
		}
	}

	if (cpumask_intersects(&c->child_cpus, cpu_online_mask))
		c->last_level = c->default_level;
	else
		c->last_level = c->nlevels-1;

	for (i = 0; i < c->nlevels; i++) {
		for (j = 0; j < c->nlevels; j++) {
			if (i >= j) {
				c->levels[i].pwr.residencies[j] = 0;
				continue;
			}
			c->levels[i].pwr.residencies[j] = calculate_residency(
				&c->levels[i].pwr, &c->levels[j].pwr);
		}
	}
	set_optimum_cluster_residency(c, true);
	return c;

failed_parse_cluster:
	pr_err("Failed parse cluster:%s\n", key);
	of_node_put(n);
	if (parent)
		list_del(&c->list);
	free_cluster_node(c);
failed_parse_params:
	pr_err("Failed parse params\n");
	return NULL;
}
struct lpm_cluster *lpm_of_parse_cluster(struct platform_device *pdev)
{
	struct device_node *top = NULL;
	struct lpm_cluster *c;

	use_psci = of_property_read_bool(pdev->dev.of_node, "qcom,use-psci");

	top = of_find_node_by_name(pdev->dev.of_node, "qcom,pm-cluster");
	if (!top) {
		pr_err("Failed to find root node\n");
		return ERR_PTR(-ENODEV);
	}

	lpm_pdev = pdev;
	c = parse_cluster(top, NULL);
	of_node_put(top);
	return c;
}

void cluster_dt_walkthrough(struct lpm_cluster *cluster)
{
	struct list_head *list;
	int i, j;
	static int id;
	char str[10] = {0};

	if (!cluster)
		return;

	for (i = 0; i < id; i++)
		snprintf(str+i, 10 - i, "\t");
	pr_info("%d\n", __LINE__);

	for (i = 0; i < cluster->nlevels; i++) {
		struct lpm_cluster_level *l = &cluster->levels[i];

		pr_info("%d ndevices:%d\n", __LINE__, cluster->ndevices);
		for (j = 0; j < cluster->ndevices; j++)
			pr_info("%sDevice: %pK id:%pK\n", str,
					&cluster->name[j], &l->mode[i]);
	}

	if (cluster->cpu) {
		pr_info("%d\n", __LINE__);
		for (j = 0; j < cluster->cpu->nlevels; j++)
			pr_info("%s\tCPU mode: %s id:%d\n", str,
					cluster->cpu->levels[j].name,
					cluster->cpu->levels[j].mode);
	}

	id++;


	list_for_each(list, &cluster->child) {
		struct lpm_cluster *n;

		pr_info("%d\n", __LINE__);
		n = list_entry(list, typeof(*n), list);
		cluster_dt_walkthrough(n);
	}
	id--;
}
