// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * AMD Hardware Feedback Interface Driver
 *
 * Copyright (C) 2025 Advanced Micro Devices, Inc. All Rights Reserved.
 *
 * Authors: Perry Yuan <Perry.Yuan@amd.com>
 *          Mario Limonciello <mario.limonciello@amd.com>
 */

#define pr_fmt(fmt)  "amd-hfi: " fmt

#include <linux/acpi.h>
#include <linux/cpu.h>
#include <linux/cpumask.h>
#include <linux/debugfs.h>
#include <linux/gfp.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mailbox_client.h>
#include <linux/mutex.h>
#include <linux/percpu-defs.h>
#include <linux/platform_device.h>
#include <linux/smp.h>
#include <linux/topology.h>
#include <linux/workqueue.h>

#include <asm/cpu_device_id.h>

#include <acpi/pcc.h>
#include <acpi/cppc_acpi.h>

#define AMD_HFI_DRIVER		"amd_hfi"
#define AMD_HFI_MAILBOX_COUNT		1
#define AMD_HETERO_RANKING_TABLE_VER	2

#define AMD_HETERO_CPUID_27	0x80000027

static struct platform_device *device;

/**
 * struct amd_shmem_info - Shared memory table for AMD HFI
 *
 * @header:	The PCCT table header including signature, length flags and command.
 * @version_number:		Version number of the table
 * @n_logical_processors:	Number of logical processors
 * @n_capabilities:		Number of ranking dimensions (performance, efficiency, etc)
 * @table_update_context:	Command being sent over the subspace
 * @n_bitmaps:			Number of 32-bit bitmaps to enumerate all the APIC IDs
 *				This is based on the maximum APIC ID enumerated in the system
 * @reserved:			24 bit spare
 * @table_data:			Bit Map(s) of enabled logical processors
 *				Followed by the ranking data for each logical processor
 */
struct amd_shmem_info {
	struct acpi_pcct_ext_pcc_shared_memory header;
	u32	version_number		:8,
		n_logical_processors	:8,
		n_capabilities		:8,
		table_update_context	:8;
	u32	n_bitmaps		:8,
		reserved		:24;
	u32	table_data[];
};

struct amd_hfi_data {
	const char	*name;
	struct device	*dev;

	/* PCCT table related */
	struct pcc_mbox_chan	*pcc_chan;
	void __iomem		*pcc_comm_addr;
	struct acpi_subtable_header	*pcct_entry;
	struct amd_shmem_info	*shmem;

	struct dentry *dbgfs_dir;
};

/**
 * struct amd_hfi_classes - HFI class capabilities per CPU
 * @perf:	Performance capability
 * @eff:	Power efficiency capability
 *
 * Capabilities of a logical processor in the ranking table. These capabilities
 * are unitless and specific to each HFI class.
 */
struct amd_hfi_classes {
	u32	perf;
	u32	eff;
};

/**
 * struct amd_hfi_cpuinfo - HFI workload class info per CPU
 * @cpu:		CPU index
 * @apic_id:		APIC id of the current CPU
 * @cpus:		mask of CPUs associated with amd_hfi_cpuinfo
 * @class_index:	workload class ID index
 * @nr_class:		max number of workload class supported
 * @ipcc_scores:	ipcc scores for each class
 * @amd_hfi_classes:	current CPU workload class ranking data
 *
 * Parameters of a logical processor linked with hardware feedback class.
 */
struct amd_hfi_cpuinfo {
	int		cpu;
	u32		apic_id;
	cpumask_var_t	cpus;
	s16		class_index;
	u8		nr_class;
	int		*ipcc_scores;
	struct amd_hfi_classes	*amd_hfi_classes;
};

static DEFINE_PER_CPU(struct amd_hfi_cpuinfo, amd_hfi_cpuinfo) = {.class_index = -1};

static DEFINE_MUTEX(hfi_cpuinfo_lock);

static void amd_hfi_sched_itmt_work(struct work_struct *work)
{
	sched_set_itmt_support();
}
static DECLARE_WORK(sched_amd_hfi_itmt_work, amd_hfi_sched_itmt_work);

static int find_cpu_index_by_apicid(unsigned int target_apicid)
{
	int cpu_index;

	for_each_possible_cpu(cpu_index) {
		struct cpuinfo_x86 *info = &cpu_data(cpu_index);

		if (info->topo.apicid == target_apicid) {
			pr_debug("match APIC id %u for CPU index: %d\n",
				 info->topo.apicid, cpu_index);
			return cpu_index;
		}
	}

	return -ENODEV;
}

static int amd_hfi_fill_metadata(struct amd_hfi_data *amd_hfi_data)
{
	struct acpi_pcct_ext_pcc_slave *pcct_ext =
		(struct acpi_pcct_ext_pcc_slave *)amd_hfi_data->pcct_entry;
	void __iomem *pcc_comm_addr;
	u32 apic_start = 0;

	pcc_comm_addr = acpi_os_ioremap(amd_hfi_data->pcc_chan->shmem_base_addr,
					amd_hfi_data->pcc_chan->shmem_size);
	if (!pcc_comm_addr) {
		dev_err(amd_hfi_data->dev, "failed to ioremap PCC common region mem\n");
		return -ENOMEM;
	}

	memcpy_fromio(amd_hfi_data->shmem, pcc_comm_addr, pcct_ext->length);
	iounmap(pcc_comm_addr);

	if (amd_hfi_data->shmem->header.signature != PCC_SIGNATURE) {
		dev_err(amd_hfi_data->dev, "invalid signature in shared memory\n");
		return -EINVAL;
	}
	if (amd_hfi_data->shmem->version_number != AMD_HETERO_RANKING_TABLE_VER) {
		dev_err(amd_hfi_data->dev, "invalid version %d\n",
			amd_hfi_data->shmem->version_number);
		return -EINVAL;
	}

	for (unsigned int i = 0; i < amd_hfi_data->shmem->n_bitmaps; i++) {
		u32 bitmap = amd_hfi_data->shmem->table_data[i];

		for (unsigned int j = 0; j < BITS_PER_TYPE(u32); j++) {
			u32 apic_id = i * BITS_PER_TYPE(u32) + j;
			struct amd_hfi_cpuinfo *info;
			int cpu_index, apic_index;

			if (!(bitmap & BIT(j)))
				continue;

			cpu_index = find_cpu_index_by_apicid(apic_id);
			if (cpu_index < 0) {
				dev_warn(amd_hfi_data->dev, "APIC ID %u not found\n", apic_id);
				continue;
			}

			info = per_cpu_ptr(&amd_hfi_cpuinfo, cpu_index);
			info->apic_id = apic_id;

			/* Fill the ranking data for each logical processor */
			info = per_cpu_ptr(&amd_hfi_cpuinfo, cpu_index);
			apic_index = apic_start * info->nr_class * 2;
			for (unsigned int k = 0; k < info->nr_class; k++) {
				u32 *table = amd_hfi_data->shmem->table_data +
					     amd_hfi_data->shmem->n_bitmaps +
					     i * info->nr_class;

				info->amd_hfi_classes[k].eff = table[apic_index + 2 * k];
				info->amd_hfi_classes[k].perf = table[apic_index + 2 * k + 1];
			}
			apic_start++;
		}
	}

	return 0;
}

static int amd_hfi_alloc_class_data(struct platform_device *pdev)
{
	struct amd_hfi_cpuinfo *hfi_cpuinfo;
	struct device *dev = &pdev->dev;
	u32 nr_class_id;
	int idx;

	nr_class_id = cpuid_eax(AMD_HETERO_CPUID_27);
	if (nr_class_id > 255) {
		dev_err(dev, "number of supported classes too large: %d\n",
			nr_class_id);
		return -EINVAL;
	}

	for_each_possible_cpu(idx) {
		struct amd_hfi_classes *classes;
		int *ipcc_scores;

		classes = devm_kcalloc(dev,
				       nr_class_id,
				       sizeof(struct amd_hfi_classes),
				       GFP_KERNEL);
		if (!classes)
			return -ENOMEM;
		ipcc_scores = devm_kcalloc(dev, nr_class_id, sizeof(int), GFP_KERNEL);
		if (!ipcc_scores)
			return -ENOMEM;
		hfi_cpuinfo = per_cpu_ptr(&amd_hfi_cpuinfo, idx);
		hfi_cpuinfo->amd_hfi_classes = classes;
		hfi_cpuinfo->ipcc_scores = ipcc_scores;
		hfi_cpuinfo->nr_class = nr_class_id;
	}

	return 0;
}

static void amd_hfi_remove(struct platform_device *pdev)
{
	struct amd_hfi_data *dev = platform_get_drvdata(pdev);

	debugfs_remove_recursive(dev->dbgfs_dir);
}

static int amd_set_hfi_ipcc_score(struct amd_hfi_cpuinfo *hfi_cpuinfo, int cpu)
{
	for (int i = 0; i < hfi_cpuinfo->nr_class; i++)
		WRITE_ONCE(hfi_cpuinfo->ipcc_scores[i],
			   hfi_cpuinfo->amd_hfi_classes[i].perf);

	sched_set_itmt_core_prio(hfi_cpuinfo->ipcc_scores[0], cpu);

	return 0;
}

static int amd_hfi_set_state(unsigned int cpu, bool state)
{
	int ret;

	ret = wrmsrq_on_cpu(cpu, MSR_AMD_WORKLOAD_CLASS_CONFIG, state ? 1 : 0);
	if (ret)
		return ret;

	return wrmsrq_on_cpu(cpu, MSR_AMD_WORKLOAD_HRST, 0x1);
}

/**
 * amd_hfi_online() - Enable workload classification on @cpu
 * @cpu: CPU in which the workload classification will be enabled
 *
 * Return: 0 on success, negative error code on failure.
 */
static int amd_hfi_online(unsigned int cpu)
{
	struct amd_hfi_cpuinfo *hfi_info = per_cpu_ptr(&amd_hfi_cpuinfo, cpu);
	struct amd_hfi_classes *hfi_classes;
	int ret;

	if (WARN_ON_ONCE(!hfi_info))
		return -EINVAL;

	/*
	 * Check if @cpu as an associated, initialized and ranking data must
	 * be filled.
	 */
	hfi_classes = hfi_info->amd_hfi_classes;
	if (!hfi_classes)
		return -EINVAL;

	guard(mutex)(&hfi_cpuinfo_lock);

	if (!zalloc_cpumask_var(&hfi_info->cpus, GFP_KERNEL))
		return -ENOMEM;

	cpumask_set_cpu(cpu, hfi_info->cpus);

	ret = amd_hfi_set_state(cpu, true);
	if (ret)
		pr_err("WCT enable failed for CPU %u\n", cpu);

	return ret;
}

/**
 * amd_hfi_offline() - Disable workload classification on @cpu
 * @cpu: CPU in which the workload classification will be disabled
 *
 * Remove @cpu from those covered by its HFI instance.
 *
 * Return: 0 on success, negative error code on failure
 */
static int amd_hfi_offline(unsigned int cpu)
{
	struct amd_hfi_cpuinfo *hfi_info = &per_cpu(amd_hfi_cpuinfo, cpu);
	int ret;

	if (WARN_ON_ONCE(!hfi_info))
		return -EINVAL;

	guard(mutex)(&hfi_cpuinfo_lock);

	ret = amd_hfi_set_state(cpu, false);
	if (ret)
		pr_err("WCT disable failed for CPU %u\n", cpu);

	free_cpumask_var(hfi_info->cpus);

	return ret;
}

static int update_hfi_ipcc_scores(void)
{
	int cpu;
	int ret;

	for_each_possible_cpu(cpu) {
		struct amd_hfi_cpuinfo *hfi_cpuinfo = per_cpu_ptr(&amd_hfi_cpuinfo, cpu);

		ret = amd_set_hfi_ipcc_score(hfi_cpuinfo, cpu);
		if (ret)
			return ret;
	}

	return 0;
}

static int amd_hfi_metadata_parser(struct platform_device *pdev,
				   struct amd_hfi_data *amd_hfi_data)
{
	struct acpi_pcct_ext_pcc_slave *pcct_ext;
	struct acpi_subtable_header *pcct_entry;
	struct mbox_chan *pcc_mbox_channels;
	struct acpi_table_header *pcct_tbl;
	struct pcc_mbox_chan *pcc_chan;
	acpi_status status;
	int ret;

	pcc_mbox_channels = devm_kcalloc(&pdev->dev, AMD_HFI_MAILBOX_COUNT,
					 sizeof(*pcc_mbox_channels), GFP_KERNEL);
	if (!pcc_mbox_channels)
		return -ENOMEM;

	pcc_chan = devm_kcalloc(&pdev->dev, AMD_HFI_MAILBOX_COUNT,
				sizeof(*pcc_chan), GFP_KERNEL);
	if (!pcc_chan)
		return -ENOMEM;

	status = acpi_get_table(ACPI_SIG_PCCT, 0, &pcct_tbl);
	if (ACPI_FAILURE(status) || !pcct_tbl)
		return -ENODEV;

	/* get pointer to the first PCC subspace entry */
	pcct_entry = (struct acpi_subtable_header *) (
			(unsigned long)pcct_tbl + sizeof(struct acpi_table_pcct));

	pcc_chan->mchan = &pcc_mbox_channels[0];

	amd_hfi_data->pcc_chan = pcc_chan;
	amd_hfi_data->pcct_entry = pcct_entry;
	pcct_ext = (struct acpi_pcct_ext_pcc_slave *)pcct_entry;

	if (pcct_ext->length <= 0) {
		ret = -EINVAL;
		goto out;
	}

	amd_hfi_data->shmem = devm_kzalloc(amd_hfi_data->dev, pcct_ext->length, GFP_KERNEL);
	if (!amd_hfi_data->shmem) {
		ret = -ENOMEM;
		goto out;
	}

	pcc_chan->shmem_base_addr = pcct_ext->base_address;
	pcc_chan->shmem_size = pcct_ext->length;

	/* parse the shared memory info from the PCCT table */
	ret = amd_hfi_fill_metadata(amd_hfi_data);

out:
	/* Don't leak any ACPI memory */
	acpi_put_table(pcct_tbl);

	return ret;
}

static int class_capabilities_show(struct seq_file *s, void *unused)
{
	u32 cpu, idx;

	seq_puts(s, "CPU #\tWLC\tPerf\tEff\n");
	for_each_possible_cpu(cpu) {
		struct amd_hfi_cpuinfo *hfi_cpuinfo = per_cpu_ptr(&amd_hfi_cpuinfo, cpu);

		seq_printf(s, "%d", cpu);
		for (idx = 0; idx < hfi_cpuinfo->nr_class; idx++) {
			seq_printf(s, "\t%u\t%u\t%u\n", idx,
				   hfi_cpuinfo->amd_hfi_classes[idx].perf,
				   hfi_cpuinfo->amd_hfi_classes[idx].eff);
		}
	}

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(class_capabilities);

static int amd_hfi_pm_resume(struct device *dev)
{
	int ret, cpu;

	for_each_online_cpu(cpu) {
		ret = amd_hfi_set_state(cpu, true);
		if (ret < 0) {
			dev_err(dev, "failed to enable workload class config: %d\n", ret);
			return ret;
		}
	}

	return 0;
}

static int amd_hfi_pm_suspend(struct device *dev)
{
	int ret, cpu;

	for_each_online_cpu(cpu) {
		ret = amd_hfi_set_state(cpu, false);
		if (ret < 0) {
			dev_err(dev, "failed to disable workload class config: %d\n", ret);
			return ret;
		}
	}

	return 0;
}

static DEFINE_SIMPLE_DEV_PM_OPS(amd_hfi_pm_ops, amd_hfi_pm_suspend, amd_hfi_pm_resume);

static const struct acpi_device_id amd_hfi_platform_match[] = {
	{"AMDI0104", 0},
	{ }
};
MODULE_DEVICE_TABLE(acpi, amd_hfi_platform_match);

static int amd_hfi_probe(struct platform_device *pdev)
{
	struct amd_hfi_data *amd_hfi_data;
	int ret;

	if (!acpi_match_device(amd_hfi_platform_match, &pdev->dev))
		return -ENODEV;

	amd_hfi_data = devm_kzalloc(&pdev->dev, sizeof(*amd_hfi_data), GFP_KERNEL);
	if (!amd_hfi_data)
		return -ENOMEM;

	amd_hfi_data->dev = &pdev->dev;
	platform_set_drvdata(pdev, amd_hfi_data);

	ret = amd_hfi_alloc_class_data(pdev);
	if (ret)
		return ret;

	ret = amd_hfi_metadata_parser(pdev, amd_hfi_data);
	if (ret)
		return ret;

	ret = update_hfi_ipcc_scores();
	if (ret)
		return ret;

	/*
	 * Tasks will already be running at the time this happens. This is
	 * OK because rankings will be adjusted by the callbacks.
	 */
	ret = cpuhp_setup_state(CPUHP_AP_ONLINE_DYN, "x86/amd_hfi:online",
				amd_hfi_online, amd_hfi_offline);
	if (ret < 0)
		return ret;

	schedule_work(&sched_amd_hfi_itmt_work);

	amd_hfi_data->dbgfs_dir = debugfs_create_dir("amd_hfi", arch_debugfs_dir);
	debugfs_create_file("class_capabilities", 0644, amd_hfi_data->dbgfs_dir, pdev,
			    &class_capabilities_fops);

	return 0;
}

static struct platform_driver amd_hfi_driver = {
	.driver = {
		.name = AMD_HFI_DRIVER,
		.owner = THIS_MODULE,
		.pm = &amd_hfi_pm_ops,
		.acpi_match_table = ACPI_PTR(amd_hfi_platform_match),
	},
	.probe = amd_hfi_probe,
	.remove = amd_hfi_remove,
};

static int __init amd_hfi_init(void)
{
	int ret;

	if (acpi_disabled ||
	    !cpu_feature_enabled(X86_FEATURE_AMD_HTR_CORES) ||
	    !cpu_feature_enabled(X86_FEATURE_AMD_WORKLOAD_CLASS))
		return -ENODEV;

	device = platform_device_register_simple(AMD_HFI_DRIVER, -1, NULL, 0);
	if (IS_ERR(device)) {
		pr_err("unable to register HFI platform device\n");
		return PTR_ERR(device);
	}

	ret = platform_driver_register(&amd_hfi_driver);
	if (ret)
		pr_err("failed to register HFI driver\n");

	return ret;
}

static __exit void amd_hfi_exit(void)
{
	platform_driver_unregister(&amd_hfi_driver);
	platform_device_unregister(device);
}
module_init(amd_hfi_init);
module_exit(amd_hfi_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("AMD Hardware Feedback Interface Driver");
