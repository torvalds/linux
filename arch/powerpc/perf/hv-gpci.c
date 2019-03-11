/*
 * Hypervisor supplied "gpci" ("get performance counter info") performance
 * counter support
 *
 * Author: Cody P Schafer <cody@linux.vnet.ibm.com>
 * Copyright 2014 IBM Corporation.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
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

static struct attribute_group format_group = {
	.name = "format",
	.attrs = format_attrs,
};

static struct attribute_group event_group = {
	.name  = "events",
	.attrs = hv_gpci_event_attrs,
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

static DEVICE_ATTR_RO(kernel_version);
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

static struct attribute_group interface_group = {
	.name = "interface",
	.attrs = interface_attrs,
};

static const struct attribute_group *attr_groups[] = {
	&format_group,
	&event_group,
	&interface_group,
	NULL,
};

#define HGPCI_REQ_BUFFER_SIZE	4096
#define HGPCI_MAX_DATA_BYTES \
	(HGPCI_REQ_BUFFER_SIZE - sizeof(struct hv_get_perf_counter_info_params))

static DEFINE_PER_CPU(char, hv_gpci_reqb[HGPCI_REQ_BUFFER_SIZE]) __aligned(sizeof(uint64_t));

struct hv_gpci_request_buffer {
	struct hv_get_perf_counter_info_params params;
	uint8_t bytes[HGPCI_MAX_DATA_BYTES];
} __packed;

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
		count |= arg->bytes[i] << (i - offset);

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

static int hv_gpci_init(void)
{
	int r;
	unsigned long hret;
	struct hv_perf_caps caps;

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

	/* sampling not supported */
	h_gpci_pmu.capabilities |= PERF_PMU_CAP_NO_INTERRUPT;

	r = perf_pmu_register(&h_gpci_pmu, h_gpci_pmu.name, -1);
	if (r)
		return r;

	return 0;
}

device_initcall(hv_gpci_init);
