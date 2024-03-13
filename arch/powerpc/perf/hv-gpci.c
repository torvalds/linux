// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Hypervisor supplied "gpci" ("get performance counter info") performance
 * counter support
 *
 * Author: Cody P Schafer <cody@linux.vnet.ibm.com>
 * Copyright 2014 IBM Corporation.
 */

#define pr_fmt(fmt) "hv-gpci: " fmt

#include <linux/init.h>
#include <linux/perf_event.h>
#include <asm/firmware.h>
#include <asm/hvcall.h>
#include <asm/io.h>

#include "hv-gpci.h"
#include "hv-common.h"

/*
 * Example usage:
 *  perf stat -e 'hv_gpci/counter_info_version=3,offset=0,length=8,
 *		  secondary_index=0,starting_index=0xffffffff,request=0x10/' ...
 */

/* u32 */
EVENT_DEFINE_RANGE_FORMAT(request, config, 0, 31);
/* u32 */
/*
 * Note that starting_index, phys_processor_idx, sibling_part_id,
 * hw_chip_id, partition_id all refer to the same bit range. They
 * are basically aliases for the starting_index. The specific alias
 * used depends on the event. See REQUEST_IDX_KIND in hv-gpci-requests.h
 */
EVENT_DEFINE_RANGE_FORMAT(starting_index, config, 32, 63);
EVENT_DEFINE_RANGE_FORMAT_LITE(phys_processor_idx, config, 32, 63);
EVENT_DEFINE_RANGE_FORMAT_LITE(sibling_part_id, config, 32, 63);
EVENT_DEFINE_RANGE_FORMAT_LITE(hw_chip_id, config, 32, 63);
EVENT_DEFINE_RANGE_FORMAT_LITE(partition_id, config, 32, 63);

/* u16 */
EVENT_DEFINE_RANGE_FORMAT(secondary_index, config1, 0, 15);
/* u8 */
EVENT_DEFINE_RANGE_FORMAT(counter_info_version, config1, 16, 23);
/* u8, bytes of data (1-8) */
EVENT_DEFINE_RANGE_FORMAT(length, config1, 24, 31);
/* u32, byte offset */
EVENT_DEFINE_RANGE_FORMAT(offset, config1, 32, 63);

static cpumask_t hv_gpci_cpumask;

static struct attribute *format_attrs[] = {
	&format_attr_request.attr,
	&format_attr_starting_index.attr,
	&format_attr_phys_processor_idx.attr,
	&format_attr_sibling_part_id.attr,
	&format_attr_hw_chip_id.attr,
	&format_attr_partition_id.attr,
	&format_attr_secondary_index.attr,
	&format_attr_counter_info_version.attr,

	&format_attr_offset.attr,
	&format_attr_length.attr,
	NULL,
};

static const struct attribute_group format_group = {
	.name = "format",
	.attrs = format_attrs,
};

static struct attribute_group event_group = {
	.name  = "events",
	/* .attrs is set in init */
};

#define HV_CAPS_ATTR(_name, _format)				\
static ssize_t _name##_show(struct device *dev,			\
			    struct device_attribute *attr,	\
			    char *page)				\
{								\
	struct hv_perf_caps caps;				\
	unsigned long hret = hv_perf_caps_get(&caps);		\
	if (hret)						\
		return -EIO;					\
								\
	return sprintf(page, _format, caps._name);		\
}								\
static struct device_attribute hv_caps_attr_##_name = __ATTR_RO(_name)

static ssize_t kernel_version_show(struct device *dev,
				   struct device_attribute *attr,
				   char *page)
{
	return sprintf(page, "0x%x\n", COUNTER_INFO_VERSION_CURRENT);
}

static ssize_t cpumask_show(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	return cpumap_print_to_pagebuf(true, buf, &hv_gpci_cpumask);
}

static DEVICE_ATTR_RO(kernel_version);
static DEVICE_ATTR_RO(cpumask);

HV_CAPS_ATTR(version, "0x%x\n");
HV_CAPS_ATTR(ga, "%d\n");
HV_CAPS_ATTR(expanded, "%d\n");
HV_CAPS_ATTR(lab, "%d\n");
HV_CAPS_ATTR(collect_privileged, "%d\n");

static struct attribute *interface_attrs[] = {
	&dev_attr_kernel_version.attr,
	&hv_caps_attr_version.attr,
	&hv_caps_attr_ga.attr,
	&hv_caps_attr_expanded.attr,
	&hv_caps_attr_lab.attr,
	&hv_caps_attr_collect_privileged.attr,
	NULL,
};

static struct attribute *cpumask_attrs[] = {
	&dev_attr_cpumask.attr,
	NULL,
};

static const struct attribute_group cpumask_attr_group = {
	.attrs = cpumask_attrs,
};

static const struct attribute_group interface_group = {
	.name = "interface",
	.attrs = interface_attrs,
};

static const struct attribute_group *attr_groups[] = {
	&format_group,
	&event_group,
	&interface_group,
	&cpumask_attr_group,
	NULL,
};

static DEFINE_PER_CPU(char, hv_gpci_reqb[HGPCI_REQ_BUFFER_SIZE]) __aligned(sizeof(uint64_t));

static unsigned long single_gpci_request(u32 req, u32 starting_index,
		u16 secondary_index, u8 version_in, u32 offset, u8 length,
		u64 *value)
{
	unsigned long ret;
	size_t i;
	u64 count;
	struct hv_gpci_request_buffer *arg;

	arg = (void *)get_cpu_var(hv_gpci_reqb);
	memset(arg, 0, HGPCI_REQ_BUFFER_SIZE);

	arg->params.counter_request = cpu_to_be32(req);
	arg->params.starting_index = cpu_to_be32(starting_index);
	arg->params.secondary_index = cpu_to_be16(secondary_index);
	arg->params.counter_info_version_in = version_in;

	ret = plpar_hcall_norets(H_GET_PERF_COUNTER_INFO,
			virt_to_phys(arg), HGPCI_REQ_BUFFER_SIZE);
	if (ret) {
		pr_devel("hcall failed: 0x%lx\n", ret);
		goto out;
	}

	/*
	 * we verify offset and length are within the zeroed buffer at event
	 * init.
	 */
	count = 0;
	for (i = offset; i < offset + length; i++)
		count |= (u64)(arg->bytes[i]) << ((length - 1 - (i - offset)) * 8);

	*value = count;
out:
	put_cpu_var(hv_gpci_reqb);
	return ret;
}

static u64 h_gpci_get_value(struct perf_event *event)
{
	u64 count;
	unsigned long ret = single_gpci_request(event_get_request(event),
					event_get_starting_index(event),
					event_get_secondary_index(event),
					event_get_counter_info_version(event),
					event_get_offset(event),
					event_get_length(event),
					&count);
	if (ret)
		return 0;
	return count;
}

static void h_gpci_event_update(struct perf_event *event)
{
	s64 prev;
	u64 now = h_gpci_get_value(event);
	prev = local64_xchg(&event->hw.prev_count, now);
	local64_add(now - prev, &event->count);
}

static void h_gpci_event_start(struct perf_event *event, int flags)
{
	local64_set(&event->hw.prev_count, h_gpci_get_value(event));
}

static void h_gpci_event_stop(struct perf_event *event, int flags)
{
	h_gpci_event_update(event);
}

static int h_gpci_event_add(struct perf_event *event, int flags)
{
	if (flags & PERF_EF_START)
		h_gpci_event_start(event, flags);

	return 0;
}

static int h_gpci_event_init(struct perf_event *event)
{
	u64 count;
	u8 length;

	/* Not our event */
	if (event->attr.type != event->pmu->type)
		return -ENOENT;

	/* config2 is unused */
	if (event->attr.config2) {
		pr_devel("config2 set when reserved\n");
		return -EINVAL;
	}

	/* no branch sampling */
	if (has_branch_stack(event))
		return -EOPNOTSUPP;

	length = event_get_length(event);
	if (length < 1 || length > 8) {
		pr_devel("length invalid\n");
		return -EINVAL;
	}

	/* last byte within the buffer? */
	if ((event_get_offset(event) + length) > HGPCI_MAX_DATA_BYTES) {
		pr_devel("request outside of buffer: %zu > %zu\n",
				(size_t)event_get_offset(event) + length,
				HGPCI_MAX_DATA_BYTES);
		return -EINVAL;
	}

	/* check if the request works... */
	if (single_gpci_request(event_get_request(event),
				event_get_starting_index(event),
				event_get_secondary_index(event),
				event_get_counter_info_version(event),
				event_get_offset(event),
				length,
				&count)) {
		pr_devel("gpci hcall failed\n");
		return -EINVAL;
	}

	return 0;
}

static struct pmu h_gpci_pmu = {
	.task_ctx_nr = perf_invalid_context,

	.name = "hv_gpci",
	.attr_groups = attr_groups,
	.event_init  = h_gpci_event_init,
	.add         = h_gpci_event_add,
	.del         = h_gpci_event_stop,
	.start       = h_gpci_event_start,
	.stop        = h_gpci_event_stop,
	.read        = h_gpci_event_update,
	.capabilities = PERF_PMU_CAP_NO_EXCLUDE,
};

static int ppc_hv_gpci_cpu_online(unsigned int cpu)
{
	if (cpumask_empty(&hv_gpci_cpumask))
		cpumask_set_cpu(cpu, &hv_gpci_cpumask);

	return 0;
}

static int ppc_hv_gpci_cpu_offline(unsigned int cpu)
{
	int target;

	/* Check if exiting cpu is used for collecting gpci events */
	if (!cpumask_test_and_clear_cpu(cpu, &hv_gpci_cpumask))
		return 0;

	/* Find a new cpu to collect gpci events */
	target = cpumask_last(cpu_active_mask);

	if (target < 0 || target >= nr_cpu_ids) {
		pr_err("hv_gpci: CPU hotplug init failed\n");
		return -1;
	}

	/* Migrate gpci events to the new target */
	cpumask_set_cpu(target, &hv_gpci_cpumask);
	perf_pmu_migrate_context(&h_gpci_pmu, cpu, target);

	return 0;
}

static int hv_gpci_cpu_hotplug_init(void)
{
	return cpuhp_setup_state(CPUHP_AP_PERF_POWERPC_HV_GPCI_ONLINE,
			  "perf/powerpc/hv_gcpi:online",
			  ppc_hv_gpci_cpu_online,
			  ppc_hv_gpci_cpu_offline);
}

static int hv_gpci_init(void)
{
	int r;
	unsigned long hret;
	struct hv_perf_caps caps;
	struct hv_gpci_request_buffer *arg;

	hv_gpci_assert_offsets_correct();

	if (!firmware_has_feature(FW_FEATURE_LPAR)) {
		pr_debug("not a virtualized system, not enabling\n");
		return -ENODEV;
	}

	hret = hv_perf_caps_get(&caps);
	if (hret) {
		pr_debug("could not obtain capabilities, not enabling, rc=%ld\n",
				hret);
		return -ENODEV;
	}

	/* init cpuhotplug */
	r = hv_gpci_cpu_hotplug_init();
	if (r)
		return r;

	/* sampling not supported */
	h_gpci_pmu.capabilities |= PERF_PMU_CAP_NO_INTERRUPT;

	arg = (void *)get_cpu_var(hv_gpci_reqb);
	memset(arg, 0, HGPCI_REQ_BUFFER_SIZE);

	/*
	 * hcall H_GET_PERF_COUNTER_INFO populates the output
	 * counter_info_version value based on the system hypervisor.
	 * Pass the counter request 0x10 corresponds to request type
	 * 'Dispatch_timebase_by_processor', to get the supported
	 * counter_info_version.
	 */
	arg->params.counter_request = cpu_to_be32(0x10);

	r = plpar_hcall_norets(H_GET_PERF_COUNTER_INFO,
			virt_to_phys(arg), HGPCI_REQ_BUFFER_SIZE);
	if (r) {
		pr_devel("hcall failed, can't get supported counter_info_version: 0x%x\n", r);
		arg->params.counter_info_version_out = 0x8;
	}

	/*
	 * Use counter_info_version_out value to assign
	 * required hv-gpci event list.
	 */
	if (arg->params.counter_info_version_out >= 0x8)
		event_group.attrs = hv_gpci_event_attrs;
	else
		event_group.attrs = hv_gpci_event_attrs_v6;

	put_cpu_var(hv_gpci_reqb);

	r = perf_pmu_register(&h_gpci_pmu, h_gpci_pmu.name, -1);
	if (r)
		return r;

	return 0;
}

device_initcall(hv_gpci_init);
