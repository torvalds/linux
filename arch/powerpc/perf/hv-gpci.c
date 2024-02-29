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

/* Interface attribute array index to store system information */
#define INTERFACE_PROCESSOR_BUS_TOPOLOGY_ATTR	6
#define INTERFACE_PROCESSOR_CONFIG_ATTR		7
#define INTERFACE_AFFINITY_DOMAIN_VIA_VP_ATTR	8
#define INTERFACE_AFFINITY_DOMAIN_VIA_DOM_ATTR	9
#define INTERFACE_AFFINITY_DOMAIN_VIA_PAR_ATTR	10
#define INTERFACE_NULL_ATTR			11

/* Counter request value to retrieve system information */
enum {
	PROCESSOR_BUS_TOPOLOGY,
	PROCESSOR_CONFIG,
	AFFINITY_DOMAIN_VIA_VP, /* affinity domain via virtual processor */
	AFFINITY_DOMAIN_VIA_DOM, /* affinity domain via domain */
	AFFINITY_DOMAIN_VIA_PAR, /* affinity domain via partition */
};

static int sysinfo_counter_request[] = {
	[PROCESSOR_BUS_TOPOLOGY] = 0xD0,
	[PROCESSOR_CONFIG] = 0x90,
	[AFFINITY_DOMAIN_VIA_VP] = 0xA0,
	[AFFINITY_DOMAIN_VIA_DOM] = 0xB0,
	[AFFINITY_DOMAIN_VIA_PAR] = 0xB1,
};

static DEFINE_PER_CPU(char, hv_gpci_reqb[HGPCI_REQ_BUFFER_SIZE]) __aligned(sizeof(uint64_t));

static unsigned long systeminfo_gpci_request(u32 req, u32 starting_index,
			u16 secondary_index, char *buf,
			size_t *n, struct hv_gpci_request_buffer *arg)
{
	unsigned long ret;
	size_t i, j;

	arg->params.counter_request = cpu_to_be32(req);
	arg->params.starting_index = cpu_to_be32(starting_index);
	arg->params.secondary_index = cpu_to_be16(secondary_index);

	ret = plpar_hcall_norets(H_GET_PERF_COUNTER_INFO,
			virt_to_phys(arg), HGPCI_REQ_BUFFER_SIZE);

	/*
	 * ret value as 'H_PARAMETER' corresponds to 'GEN_BUF_TOO_SMALL',
	 * which means that the current buffer size cannot accommodate
	 * all the information and a partial buffer returned.
	 * hcall fails incase of ret value other than H_SUCCESS or H_PARAMETER.
	 *
	 * ret value as H_AUTHORITY implies that partition is not permitted to retrieve
	 * performance information, and required to set
	 * "Enable Performance Information Collection" option.
	 */
	if (ret == H_AUTHORITY)
		return -EPERM;

	/*
	 * hcall can fail with other possible ret value like H_PRIVILEGE/H_HARDWARE
	 * because of invalid buffer-length/address or due to some hardware
	 * error.
	 */
	if (ret && (ret != H_PARAMETER))
		return -EIO;

	/*
	 * hcall H_GET_PERF_COUNTER_INFO populates the 'returned_values'
	 * to show the total number of counter_value array elements
	 * returned via hcall.
	 * hcall also populates 'cv_element_size' corresponds to individual
	 * counter_value array element size. Below loop go through all
	 * counter_value array elements as per their size and add it to
	 * the output buffer.
	 */
	for (i = 0; i < be16_to_cpu(arg->params.returned_values); i++) {
		j = i * be16_to_cpu(arg->params.cv_element_size);

		for (; j < (i + 1) * be16_to_cpu(arg->params.cv_element_size); j++)
			*n += sprintf(buf + *n,  "%02x", (u8)arg->bytes[j]);
		*n += sprintf(buf + *n,  "\n");
	}

	if (*n >= PAGE_SIZE) {
		pr_info("System information exceeds PAGE_SIZE\n");
		return -EFBIG;
	}

	return ret;
}

static ssize_t processor_bus_topology_show(struct device *dev, struct device_attribute *attr,
				char *buf)
{
	struct hv_gpci_request_buffer *arg;
	unsigned long ret;
	size_t n = 0;

	arg = (void *)get_cpu_var(hv_gpci_reqb);
	memset(arg, 0, HGPCI_REQ_BUFFER_SIZE);

	/*
	 * Pass the counter request value 0xD0 corresponds to request
	 * type 'Processor_bus_topology', to retrieve
	 * the system topology information.
	 * starting_index value implies the starting hardware
	 * chip id.
	 */
	ret = systeminfo_gpci_request(sysinfo_counter_request[PROCESSOR_BUS_TOPOLOGY],
			0, 0, buf, &n, arg);

	if (!ret)
		return n;

	if (ret != H_PARAMETER)
		goto out;

	/*
	 * ret value as 'H_PARAMETER' corresponds to 'GEN_BUF_TOO_SMALL', which
	 * implies that buffer can't accommodate all information, and a partial buffer
	 * returned. To handle that, we need to make subsequent requests
	 * with next starting index to retrieve additional (missing) data.
	 * Below loop do subsequent hcalls with next starting index and add it
	 * to buffer util we get all the information.
	 */
	while (ret == H_PARAMETER) {
		int returned_values = be16_to_cpu(arg->params.returned_values);
		int elementsize = be16_to_cpu(arg->params.cv_element_size);
		int last_element = (returned_values - 1) * elementsize;

		/*
		 * Since the starting index value is part of counter_value
		 * buffer elements, use the starting index value in the last
		 * element and add 1 to make subsequent hcalls.
		 */
		u32 starting_index = arg->bytes[last_element + 3] +
				(arg->bytes[last_element + 2] << 8) +
				(arg->bytes[last_element + 1] << 16) +
				(arg->bytes[last_element] << 24) + 1;

		memset(arg, 0, HGPCI_REQ_BUFFER_SIZE);

		ret = systeminfo_gpci_request(sysinfo_counter_request[PROCESSOR_BUS_TOPOLOGY],
				starting_index, 0, buf, &n, arg);

		if (!ret)
			return n;

		if (ret != H_PARAMETER)
			goto out;
	}

	return n;

out:
	put_cpu_var(hv_gpci_reqb);
	return ret;
}

static ssize_t processor_config_show(struct device *dev, struct device_attribute *attr,
					char *buf)
{
	struct hv_gpci_request_buffer *arg;
	unsigned long ret;
	size_t n = 0;

	arg = (void *)get_cpu_var(hv_gpci_reqb);
	memset(arg, 0, HGPCI_REQ_BUFFER_SIZE);

	/*
	 * Pass the counter request value 0x90 corresponds to request
	 * type 'Processor_config', to retrieve
	 * the system processor information.
	 * starting_index value implies the starting hardware
	 * processor index.
	 */
	ret = systeminfo_gpci_request(sysinfo_counter_request[PROCESSOR_CONFIG],
			0, 0, buf, &n, arg);

	if (!ret)
		return n;

	if (ret != H_PARAMETER)
		goto out;

	/*
	 * ret value as 'H_PARAMETER' corresponds to 'GEN_BUF_TOO_SMALL', which
	 * implies that buffer can't accommodate all information, and a partial buffer
	 * returned. To handle that, we need to take subsequent requests
	 * with next starting index to retrieve additional (missing) data.
	 * Below loop do subsequent hcalls with next starting index and add it
	 * to buffer util we get all the information.
	 */
	while (ret == H_PARAMETER) {
		int returned_values = be16_to_cpu(arg->params.returned_values);
		int elementsize = be16_to_cpu(arg->params.cv_element_size);
		int last_element = (returned_values - 1) * elementsize;

		/*
		 * Since the starting index is part of counter_value
		 * buffer elements, use the starting index value in the last
		 * element and add 1 to subsequent hcalls.
		 */
		u32 starting_index = arg->bytes[last_element + 3] +
				(arg->bytes[last_element + 2] << 8) +
				(arg->bytes[last_element + 1] << 16) +
				(arg->bytes[last_element] << 24) + 1;

		memset(arg, 0, HGPCI_REQ_BUFFER_SIZE);

		ret = systeminfo_gpci_request(sysinfo_counter_request[PROCESSOR_CONFIG],
				starting_index, 0, buf, &n, arg);

		if (!ret)
			return n;

		if (ret != H_PARAMETER)
			goto out;
	}

	return n;

out:
	put_cpu_var(hv_gpci_reqb);
	return ret;
}

static ssize_t affinity_domain_via_virtual_processor_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct hv_gpci_request_buffer *arg;
	unsigned long ret;
	size_t n = 0;

	arg = (void *)get_cpu_var(hv_gpci_reqb);
	memset(arg, 0, HGPCI_REQ_BUFFER_SIZE);

	/*
	 * Pass the counter request 0xA0 corresponds to request
	 * type 'Affinity_domain_information_by_virutal_processor',
	 * to retrieve the system affinity domain information.
	 * starting_index value refers to the starting hardware
	 * processor index.
	 */
	ret = systeminfo_gpci_request(sysinfo_counter_request[AFFINITY_DOMAIN_VIA_VP],
			0, 0, buf, &n, arg);

	if (!ret)
		return n;

	if (ret != H_PARAMETER)
		goto out;

	/*
	 * ret value as 'H_PARAMETER' corresponds to 'GEN_BUF_TOO_SMALL', which
	 * implies that buffer can't accommodate all information, and a partial buffer
	 * returned. To handle that, we need to take subsequent requests
	 * with next secondary index to retrieve additional (missing) data.
	 * Below loop do subsequent hcalls with next secondary index and add it
	 * to buffer util we get all the information.
	 */
	while (ret == H_PARAMETER) {
		int returned_values = be16_to_cpu(arg->params.returned_values);
		int elementsize = be16_to_cpu(arg->params.cv_element_size);
		int last_element = (returned_values - 1) * elementsize;

		/*
		 * Since the starting index and secondary index type is part of the
		 * counter_value buffer elements, use the starting index value in the
		 * last array element as subsequent starting index, and use secondary index
		 * value in the last array element plus 1 as subsequent secondary index.
		 * For counter request '0xA0', starting index points to partition id
		 * and secondary index points to corresponding virtual processor index.
		 */
		u32 starting_index = arg->bytes[last_element + 1] + (arg->bytes[last_element] << 8);
		u16 secondary_index = arg->bytes[last_element + 3] +
				(arg->bytes[last_element + 2] << 8) + 1;

		memset(arg, 0, HGPCI_REQ_BUFFER_SIZE);

		ret = systeminfo_gpci_request(sysinfo_counter_request[AFFINITY_DOMAIN_VIA_VP],
				starting_index, secondary_index, buf, &n, arg);

		if (!ret)
			return n;

		if (ret != H_PARAMETER)
			goto out;
	}

	return n;

out:
	put_cpu_var(hv_gpci_reqb);
	return ret;
}

static ssize_t affinity_domain_via_domain_show(struct device *dev, struct device_attribute *attr,
						char *buf)
{
	struct hv_gpci_request_buffer *arg;
	unsigned long ret;
	size_t n = 0;

	arg = (void *)get_cpu_var(hv_gpci_reqb);
	memset(arg, 0, HGPCI_REQ_BUFFER_SIZE);

	/*
	 * Pass the counter request 0xB0 corresponds to request
	 * type 'Affinity_domain_information_by_domain',
	 * to retrieve the system affinity domain information.
	 * starting_index value refers to the starting hardware
	 * processor index.
	 */
	ret = systeminfo_gpci_request(sysinfo_counter_request[AFFINITY_DOMAIN_VIA_DOM],
			0, 0, buf, &n, arg);

	if (!ret)
		return n;

	if (ret != H_PARAMETER)
		goto out;

	/*
	 * ret value as 'H_PARAMETER' corresponds to 'GEN_BUF_TOO_SMALL', which
	 * implies that buffer can't accommodate all information, and a partial buffer
	 * returned. To handle that, we need to take subsequent requests
	 * with next starting index to retrieve additional (missing) data.
	 * Below loop do subsequent hcalls with next starting index and add it
	 * to buffer util we get all the information.
	 */
	while (ret == H_PARAMETER) {
		int returned_values = be16_to_cpu(arg->params.returned_values);
		int elementsize = be16_to_cpu(arg->params.cv_element_size);
		int last_element = (returned_values - 1) * elementsize;

		/*
		 * Since the starting index value is part of counter_value
		 * buffer elements, use the starting index value in the last
		 * element and add 1 to make subsequent hcalls.
		 */
		u32 starting_index = arg->bytes[last_element + 1] +
			(arg->bytes[last_element] << 8) + 1;

		memset(arg, 0, HGPCI_REQ_BUFFER_SIZE);

		ret = systeminfo_gpci_request(sysinfo_counter_request[AFFINITY_DOMAIN_VIA_DOM],
					starting_index, 0, buf, &n, arg);

		if (!ret)
			return n;

		if (ret != H_PARAMETER)
			goto out;
	}

	return n;

out:
	put_cpu_var(hv_gpci_reqb);
	return ret;
}

static void affinity_domain_via_partition_result_parse(int returned_values,
			int element_size, char *buf, size_t *last_element,
			size_t *n, struct hv_gpci_request_buffer *arg)
{
	size_t i = 0, j = 0;
	size_t k, l, m;
	uint16_t total_affinity_domain_ele, size_of_each_affinity_domain_ele;

	/*
	 * hcall H_GET_PERF_COUNTER_INFO populates the 'returned_values'
	 * to show the total number of counter_value array elements
	 * returned via hcall.
	 * Unlike other request types, the data structure returned by this
	 * request is variable-size. For this counter request type,
	 * hcall populates 'cv_element_size' corresponds to minimum size of
	 * the structure returned i.e; the size of the structure with no domain
	 * information. Below loop go through all counter_value array
	 * to determine the number and size of each domain array element and
	 * add it to the output buffer.
	 */
	while (i < returned_values) {
		k = j;
		for (; k < j + element_size; k++)
			*n += sprintf(buf + *n,  "%02x", (u8)arg->bytes[k]);
		*n += sprintf(buf + *n,  "\n");

		total_affinity_domain_ele = (u8)arg->bytes[k - 2] << 8 | (u8)arg->bytes[k - 3];
		size_of_each_affinity_domain_ele = (u8)arg->bytes[k] << 8 | (u8)arg->bytes[k - 1];

		for (l = 0; l < total_affinity_domain_ele; l++) {
			for (m = 0; m < size_of_each_affinity_domain_ele; m++) {
				*n += sprintf(buf + *n,  "%02x", (u8)arg->bytes[k]);
				k++;
			}
			*n += sprintf(buf + *n,  "\n");
		}

		*n += sprintf(buf + *n,  "\n");
		i++;
		j = k;
	}

	*last_element = k;
}

static ssize_t affinity_domain_via_partition_show(struct device *dev, struct device_attribute *attr,
							char *buf)
{
	struct hv_gpci_request_buffer *arg;
	unsigned long ret;
	size_t n = 0;
	size_t last_element = 0;
	u32 starting_index;

	arg = (void *)get_cpu_var(hv_gpci_reqb);
	memset(arg, 0, HGPCI_REQ_BUFFER_SIZE);

	/*
	 * Pass the counter request value 0xB1 corresponds to counter request
	 * type 'Affinity_domain_information_by_partition',
	 * to retrieve the system affinity domain by partition information.
	 * starting_index value refers to the starting hardware
	 * processor index.
	 */
	arg->params.counter_request = cpu_to_be32(sysinfo_counter_request[AFFINITY_DOMAIN_VIA_PAR]);
	arg->params.starting_index = cpu_to_be32(0);

	ret = plpar_hcall_norets(H_GET_PERF_COUNTER_INFO,
			virt_to_phys(arg), HGPCI_REQ_BUFFER_SIZE);

	if (!ret)
		goto parse_result;

	if (ret && (ret != H_PARAMETER))
		goto out;

	/*
	 * ret value as 'H_PARAMETER' implies that the current buffer size
	 * can't accommodate all the information, and a partial buffer
	 * returned. To handle that, we need to make subsequent requests
	 * with next starting index to retrieve additional (missing) data.
	 * Below loop do subsequent hcalls with next starting index and add it
	 * to buffer util we get all the information.
	 */
	while (ret == H_PARAMETER) {
		affinity_domain_via_partition_result_parse(
			be16_to_cpu(arg->params.returned_values) - 1,
			be16_to_cpu(arg->params.cv_element_size), buf,
			&last_element, &n, arg);

		if (n >= PAGE_SIZE) {
			put_cpu_var(hv_gpci_reqb);
			pr_debug("System information exceeds PAGE_SIZE\n");
			return -EFBIG;
		}

		/*
		 * Since the starting index value is part of counter_value
		 * buffer elements, use the starting_index value in the last
		 * element and add 1 to make subsequent hcalls.
		 */
		starting_index = (u8)arg->bytes[last_element] << 8 |
				(u8)arg->bytes[last_element + 1];

		memset(arg, 0, HGPCI_REQ_BUFFER_SIZE);
		arg->params.counter_request = cpu_to_be32(
				sysinfo_counter_request[AFFINITY_DOMAIN_VIA_PAR]);
		arg->params.starting_index = cpu_to_be32(starting_index);

		ret = plpar_hcall_norets(H_GET_PERF_COUNTER_INFO,
				virt_to_phys(arg), HGPCI_REQ_BUFFER_SIZE);

		if (ret && (ret != H_PARAMETER))
			goto out;
	}

parse_result:
	affinity_domain_via_partition_result_parse(
		be16_to_cpu(arg->params.returned_values),
		be16_to_cpu(arg->params.cv_element_size),
		buf, &last_element, &n, arg);

	put_cpu_var(hv_gpci_reqb);
	return n;

out:
	put_cpu_var(hv_gpci_reqb);

	/*
	 * ret value as 'H_PARAMETER' corresponds to 'GEN_BUF_TOO_SMALL',
	 * which means that the current buffer size cannot accommodate
	 * all the information and a partial buffer returned.
	 * hcall fails incase of ret value other than H_SUCCESS or H_PARAMETER.
	 *
	 * ret value as H_AUTHORITY implies that partition is not permitted to retrieve
	 * performance information, and required to set
	 * "Enable Performance Information Collection" option.
	 */
	if (ret == H_AUTHORITY)
		return -EPERM;

	/*
	 * hcall can fail with other possible ret value like H_PRIVILEGE/H_HARDWARE
	 * because of invalid buffer-length/address or due to some hardware
	 * error.
	 */
	return -EIO;
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
	/*
	 * This NULL is a placeholder for the processor_bus_topology
	 * attribute, set in init function if applicable.
	 */
	NULL,
	/*
	 * This NULL is a placeholder for the processor_config
	 * attribute, set in init function if applicable.
	 */
	NULL,
	/*
	 * This NULL is a placeholder for the affinity_domain_via_virtual_processor
	 * attribute, set in init function if applicable.
	 */
	NULL,
	/*
	 * This NULL is a placeholder for the affinity_domain_via_domain
	 * attribute, set in init function if applicable.
	 */
	NULL,
	/*
	 * This NULL is a placeholder for the affinity_domain_via_partition
	 * attribute, set in init function if applicable.
	 */
	NULL,
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

static struct device_attribute *sysinfo_device_attr_create(int
		sysinfo_interface_group_index, u32 req)
{
	struct device_attribute *attr = NULL;
	unsigned long ret;
	struct hv_gpci_request_buffer *arg;

	if (sysinfo_interface_group_index < INTERFACE_PROCESSOR_BUS_TOPOLOGY_ATTR ||
			sysinfo_interface_group_index >= INTERFACE_NULL_ATTR) {
		pr_info("Wrong interface group index for system information\n");
		return NULL;
	}

	/* Check for given counter request value support */
	arg = (void *)get_cpu_var(hv_gpci_reqb);
	memset(arg, 0, HGPCI_REQ_BUFFER_SIZE);

	arg->params.counter_request = cpu_to_be32(req);

	ret = plpar_hcall_norets(H_GET_PERF_COUNTER_INFO,
			virt_to_phys(arg), HGPCI_REQ_BUFFER_SIZE);

	put_cpu_var(hv_gpci_reqb);

	/*
	 * Add given counter request value attribute in the interface_attrs
	 * attribute array, only for valid return types.
	 */
	if (!ret || ret == H_AUTHORITY || ret == H_PARAMETER) {
		attr = kzalloc(sizeof(*attr), GFP_KERNEL);
		if (!attr)
			return NULL;

		sysfs_attr_init(&attr->attr);
		attr->attr.mode = 0444;

		switch (sysinfo_interface_group_index) {
		case INTERFACE_PROCESSOR_BUS_TOPOLOGY_ATTR:
			attr->attr.name = "processor_bus_topology";
			attr->show = processor_bus_topology_show;
		break;
		case INTERFACE_PROCESSOR_CONFIG_ATTR:
			attr->attr.name = "processor_config";
			attr->show = processor_config_show;
		break;
		case INTERFACE_AFFINITY_DOMAIN_VIA_VP_ATTR:
			attr->attr.name = "affinity_domain_via_virtual_processor";
			attr->show = affinity_domain_via_virtual_processor_show;
		break;
		case INTERFACE_AFFINITY_DOMAIN_VIA_DOM_ATTR:
			attr->attr.name = "affinity_domain_via_domain";
			attr->show = affinity_domain_via_domain_show;
		break;
		case INTERFACE_AFFINITY_DOMAIN_VIA_PAR_ATTR:
			attr->attr.name = "affinity_domain_via_partition";
			attr->show = affinity_domain_via_partition_show;
		break;
		}
	} else
		pr_devel("hcall failed, with error: 0x%lx\n", ret);

	return attr;
}

static void add_sysinfo_interface_files(void)
{
	int sysfs_count;
	struct device_attribute *attr[INTERFACE_NULL_ATTR - INTERFACE_PROCESSOR_BUS_TOPOLOGY_ATTR];
	int i;

	sysfs_count = INTERFACE_NULL_ATTR - INTERFACE_PROCESSOR_BUS_TOPOLOGY_ATTR;

	/* Get device attribute for a given counter request value */
	for (i = 0; i < sysfs_count; i++) {
		attr[i] = sysinfo_device_attr_create(i + INTERFACE_PROCESSOR_BUS_TOPOLOGY_ATTR,
				sysinfo_counter_request[i]);

		if (!attr[i])
			goto out;
	}

	/* Add sysinfo interface attributes in the interface_attrs attribute array */
	for (i = 0; i < sysfs_count; i++)
		interface_attrs[i + INTERFACE_PROCESSOR_BUS_TOPOLOGY_ATTR] = &attr[i]->attr;

	return;

out:
	/*
	 * The sysinfo interface attributes will be added, only if hcall passed for
	 * all the counter request values. Free the device attribute array incase
	 * of any hcall failure.
	 */
	if (i > 0) {
		while (i >= 0) {
			kfree(attr[i]);
			i--;
		}
	}
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

	/* sysinfo interface files are only available for power10 and above platforms */
	if (PVR_VER(mfspr(SPRN_PVR)) >= PVR_POWER10)
		add_sysinfo_interface_files();

	return 0;
}

device_initcall(hv_gpci_init);
