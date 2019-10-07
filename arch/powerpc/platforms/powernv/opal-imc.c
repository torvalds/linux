// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * OPAL IMC interface detection driver
 * Supported on POWERNV platform
 *
 * Copyright	(C) 2017 Madhavan Srinivasan, IBM Corporation.
 *		(C) 2017 Anju T Sudhakar, IBM Corporation.
 *		(C) 2017 Hemant K Shaw, IBM Corporation.
 */
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/crash_dump.h>
#include <asm/opal.h>
#include <asm/io.h>
#include <asm/imc-pmu.h>
#include <asm/cputhreads.h>
#include <asm/debugfs.h>

static struct dentry *imc_debugfs_parent;

/* Helpers to export imc command and mode via debugfs */
static int imc_mem_get(void *data, u64 *val)
{
	*val = cpu_to_be64(*(u64 *)data);
	return 0;
}

static int imc_mem_set(void *data, u64 val)
{
	*(u64 *)data = cpu_to_be64(val);
	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(fops_imc_x64, imc_mem_get, imc_mem_set, "0x%016llx\n");

static struct dentry *imc_debugfs_create_x64(const char *name, umode_t mode,
					     struct dentry *parent, u64  *value)
{
	return debugfs_create_file_unsafe(name, mode, parent,
					  value, &fops_imc_x64);
}

/*
 * export_imc_mode_and_cmd: Create a debugfs interface
 *                     for imc_cmd and imc_mode
 *                     for each node in the system.
 *  imc_mode and imc_cmd can be changed by echo into
 *  this interface.
 */
static void export_imc_mode_and_cmd(struct device_node *node,
				    struct imc_pmu *pmu_ptr)
{
	static u64 loc, *imc_mode_addr, *imc_cmd_addr;
	char mode[16], cmd[16];
	u32 cb_offset;
	struct imc_mem_info *ptr = pmu_ptr->mem_info;

	imc_debugfs_parent = debugfs_create_dir("imc", powerpc_debugfs_root);

	/*
	 * Return here, either because 'imc' directory already exists,
	 * Or failed to create a new one.
	 */
	if (!imc_debugfs_parent)
		return;

	if (of_property_read_u32(node, "cb_offset", &cb_offset))
		cb_offset = IMC_CNTL_BLK_OFFSET;

	while (ptr->vbase != NULL) {
		loc = (u64)(ptr->vbase) + cb_offset;
		imc_mode_addr = (u64 *)(loc + IMC_CNTL_BLK_MODE_OFFSET);
		sprintf(mode, "imc_mode_%d", (u32)(ptr->id));
		if (!imc_debugfs_create_x64(mode, 0600, imc_debugfs_parent,
					    imc_mode_addr))
			goto err;

		imc_cmd_addr = (u64 *)(loc + IMC_CNTL_BLK_CMD_OFFSET);
		sprintf(cmd, "imc_cmd_%d", (u32)(ptr->id));
		if (!imc_debugfs_create_x64(cmd, 0600, imc_debugfs_parent,
					    imc_cmd_addr))
			goto err;
		ptr++;
	}
	return;

err:
	debugfs_remove_recursive(imc_debugfs_parent);
}

/*
 * imc_get_mem_addr_nest: Function to get nest counter memory region
 * for each chip
 */
static int imc_get_mem_addr_nest(struct device_node *node,
				 struct imc_pmu *pmu_ptr,
				 u32 offset)
{
	int nr_chips = 0, i;
	u64 *base_addr_arr, baddr;
	u32 *chipid_arr;

	nr_chips = of_property_count_u32_elems(node, "chip-id");
	if (nr_chips <= 0)
		return -ENODEV;

	base_addr_arr = kcalloc(nr_chips, sizeof(*base_addr_arr), GFP_KERNEL);
	if (!base_addr_arr)
		return -ENOMEM;

	chipid_arr = kcalloc(nr_chips, sizeof(*chipid_arr), GFP_KERNEL);
	if (!chipid_arr) {
		kfree(base_addr_arr);
		return -ENOMEM;
	}

	if (of_property_read_u32_array(node, "chip-id", chipid_arr, nr_chips))
		goto error;

	if (of_property_read_u64_array(node, "base-addr", base_addr_arr,
								nr_chips))
		goto error;

	pmu_ptr->mem_info = kcalloc(nr_chips + 1, sizeof(*pmu_ptr->mem_info),
				    GFP_KERNEL);
	if (!pmu_ptr->mem_info)
		goto error;

	for (i = 0; i < nr_chips; i++) {
		pmu_ptr->mem_info[i].id = chipid_arr[i];
		baddr = base_addr_arr[i] + offset;
		pmu_ptr->mem_info[i].vbase = phys_to_virt(baddr);
	}

	pmu_ptr->imc_counter_mmaped = true;
	export_imc_mode_and_cmd(node, pmu_ptr);
	kfree(base_addr_arr);
	kfree(chipid_arr);
	return 0;

error:
	kfree(base_addr_arr);
	kfree(chipid_arr);
	return -1;
}

/*
 * imc_pmu_create : Takes the parent device which is the pmu unit, pmu_index
 *		    and domain as the inputs.
 * Allocates memory for the struct imc_pmu, sets up its domain, size and offsets
 */
static int imc_pmu_create(struct device_node *parent, int pmu_index, int domain)
{
	int ret = 0;
	struct imc_pmu *pmu_ptr;
	u32 offset;

	/* Return for unknown domain */
	if (domain < 0)
		return -EINVAL;

	/* memory for pmu */
	pmu_ptr = kzalloc(sizeof(*pmu_ptr), GFP_KERNEL);
	if (!pmu_ptr)
		return -ENOMEM;

	/* Set the domain */
	pmu_ptr->domain = domain;

	ret = of_property_read_u32(parent, "size", &pmu_ptr->counter_mem_size);
	if (ret) {
		ret = -EINVAL;
		goto free_pmu;
	}

	if (!of_property_read_u32(parent, "offset", &offset)) {
		if (imc_get_mem_addr_nest(parent, pmu_ptr, offset)) {
			ret = -EINVAL;
			goto free_pmu;
		}
	}

	/* Function to register IMC pmu */
	ret = init_imc_pmu(parent, pmu_ptr, pmu_index);
	if (ret) {
		pr_err("IMC PMU %s Register failed\n", pmu_ptr->pmu.name);
		kfree(pmu_ptr->pmu.name);
		if (pmu_ptr->domain == IMC_DOMAIN_NEST)
			kfree(pmu_ptr->mem_info);
		kfree(pmu_ptr);
		return ret;
	}

	return 0;

free_pmu:
	kfree(pmu_ptr);
	return ret;
}

static void disable_nest_pmu_counters(void)
{
	int nid, cpu;
	const struct cpumask *l_cpumask;

	get_online_cpus();
	for_each_node_with_cpus(nid) {
		l_cpumask = cpumask_of_node(nid);
		cpu = cpumask_first_and(l_cpumask, cpu_online_mask);
		if (cpu >= nr_cpu_ids)
			continue;
		opal_imc_counters_stop(OPAL_IMC_COUNTERS_NEST,
				       get_hard_smp_processor_id(cpu));
	}
	put_online_cpus();
}

static void disable_core_pmu_counters(void)
{
	cpumask_t cores_map;
	int cpu, rc;

	get_online_cpus();
	/* Disable the IMC Core functions */
	cores_map = cpu_online_cores_map();
	for_each_cpu(cpu, &cores_map) {
		rc = opal_imc_counters_stop(OPAL_IMC_COUNTERS_CORE,
					    get_hard_smp_processor_id(cpu));
		if (rc)
			pr_err("%s: Failed to stop Core (cpu = %d)\n",
				__FUNCTION__, cpu);
	}
	put_online_cpus();
}

int get_max_nest_dev(void)
{
	struct device_node *node;
	u32 pmu_units = 0, type;

	for_each_compatible_node(node, NULL, IMC_DTB_UNIT_COMPAT) {
		if (of_property_read_u32(node, "type", &type))
			continue;

		if (type == IMC_TYPE_CHIP)
			pmu_units++;
	}

	return pmu_units;
}

static int opal_imc_counters_probe(struct platform_device *pdev)
{
	struct device_node *imc_dev = pdev->dev.of_node;
	int pmu_count = 0, domain;
	bool core_imc_reg = false, thread_imc_reg = false;
	u32 type;

	/*
	 * Check whether this is kdump kernel. If yes, force the engines to
	 * stop and return.
	 */
	if (is_kdump_kernel()) {
		disable_nest_pmu_counters();
		disable_core_pmu_counters();
		return -ENODEV;
	}

	for_each_compatible_node(imc_dev, NULL, IMC_DTB_UNIT_COMPAT) {
		if (of_property_read_u32(imc_dev, "type", &type)) {
			pr_warn("IMC Device without type property\n");
			continue;
		}

		switch (type) {
		case IMC_TYPE_CHIP:
			domain = IMC_DOMAIN_NEST;
			break;
		case IMC_TYPE_CORE:
			domain =IMC_DOMAIN_CORE;
			break;
		case IMC_TYPE_THREAD:
			domain = IMC_DOMAIN_THREAD;
			break;
		case IMC_TYPE_TRACE:
			domain = IMC_DOMAIN_TRACE;
			break;
		default:
			pr_warn("IMC Unknown Device type \n");
			domain = -1;
			break;
		}

		if (!imc_pmu_create(imc_dev, pmu_count, domain)) {
			if (domain == IMC_DOMAIN_NEST)
				pmu_count++;
			if (domain == IMC_DOMAIN_CORE)
				core_imc_reg = true;
			if (domain == IMC_DOMAIN_THREAD)
				thread_imc_reg = true;
		}
	}

	/* If none of the nest units are registered, remove debugfs interface */
	if (pmu_count == 0)
		debugfs_remove_recursive(imc_debugfs_parent);

	/* If core imc is not registered, unregister thread-imc */
	if (!core_imc_reg && thread_imc_reg)
		unregister_thread_imc();

	return 0;
}

static void opal_imc_counters_shutdown(struct platform_device *pdev)
{
	/*
	 * Function only stops the engines which is bare minimum.
	 * TODO: Need to handle proper memory cleanup and pmu
	 * unregister.
	 */
	disable_nest_pmu_counters();
	disable_core_pmu_counters();
}

static const struct of_device_id opal_imc_match[] = {
	{ .compatible = IMC_DTB_COMPAT },
	{},
};

static struct platform_driver opal_imc_driver = {
	.driver = {
		.name = "opal-imc-counters",
		.of_match_table = opal_imc_match,
	},
	.probe = opal_imc_counters_probe,
	.shutdown = opal_imc_counters_shutdown,
};

builtin_platform_driver(opal_imc_driver);
