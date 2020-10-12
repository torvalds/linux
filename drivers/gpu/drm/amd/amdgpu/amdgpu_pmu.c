/*
 * Copyright 2019 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include <linux/perf_event.h>
#include <linux/init.h>
#include "amdgpu.h"
#include "amdgpu_pmu.h"

#define PMU_NAME_SIZE 32
#define NUM_FORMATS_AMDGPU_PMU		4
#define NUM_FORMATS_DF_VEGA20		3
#define NUM_EVENTS_DF_VEGA20		8
#define NUM_EVENT_TYPES_VEGA20		1
#define NUM_EVENTS_VEGA20_XGMI		2
#define NUM_EVENTS_VEGA20_MAX		NUM_EVENTS_VEGA20_XGMI
#define NUM_EVENT_TYPES_ARCTURUS	1
#define NUM_EVENTS_ARCTURUS_XGMI	6
#define NUM_EVENTS_ARCTURUS_MAX		NUM_EVENTS_ARCTURUS_XGMI

struct amdgpu_pmu_event_attribute {
	struct device_attribute attr;
	const char *event_str;
	unsigned int type;
};

/* record to keep track of pmu entry per pmu type per device */
struct amdgpu_pmu_entry {
	struct list_head entry;
	struct amdgpu_device *adev;
	struct pmu pmu;
	unsigned int pmu_perf_type;
	char *pmu_type_name;
	char *pmu_file_prefix;
	struct attribute_group fmt_attr_group;
	struct amdgpu_pmu_event_attribute *fmt_attr;
	struct attribute_group evt_attr_group;
	struct amdgpu_pmu_event_attribute *evt_attr;
};

static ssize_t amdgpu_pmu_event_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct amdgpu_pmu_event_attribute *amdgpu_pmu_attr;

	amdgpu_pmu_attr = container_of(attr, struct amdgpu_pmu_event_attribute,
									attr);

	if (!amdgpu_pmu_attr->type)
		return sprintf(buf, "%s\n", amdgpu_pmu_attr->event_str);

	return sprintf(buf, "%s,type=0x%x\n",
			amdgpu_pmu_attr->event_str, amdgpu_pmu_attr->type);
}

static LIST_HEAD(amdgpu_pmu_list);


struct amdgpu_pmu_attr {
	const char *name;
	const char *config;
};

struct amdgpu_pmu_type {
	const unsigned int type;
	const unsigned int num_of_type;
};

struct amdgpu_pmu_config {
	struct amdgpu_pmu_attr *formats;
	unsigned int num_formats;
	struct amdgpu_pmu_attr *events;
	unsigned int num_events;
	struct amdgpu_pmu_type *types;
	unsigned int num_types;
};

/*
 * Events fall under two categories:
 *  - PMU typed
 *    Events in /sys/bus/event_source/devices/amdgpu_<pmu_type>_<dev_num> have
 *    performance counter operations handled by one IP <pmu_type>.  Formats and
 *    events should be defined by <pmu_type>_<asic_type>_formats and
 *    <pmu_type>_<asic_type>_events respectively.
 *
 *  - Event config typed
 *    Events in /sys/bus/event_source/devices/amdgpu_<dev_num> have performance
 *    counter operations that can be handled by multiple IPs dictated by their
 *    "type" format field.  Formats and events should be defined by
 *    amdgpu_pmu_formats and <asic_type>_events respectively.  Format field
 *    "type" is generated in amdgpu_pmu_event_show and defined in
 *    <asic_type>_event_config_types.
 */

static struct amdgpu_pmu_attr amdgpu_pmu_formats[NUM_FORMATS_AMDGPU_PMU] = {
	{ .name = "event", .config = "config:0-7" },
	{ .name = "instance", .config = "config:8-15" },
	{ .name = "umask", .config = "config:16-23"},
	{ .name = "type", .config = "config:56-63"}
};

/* Vega20 events */
static struct amdgpu_pmu_attr vega20_events[NUM_EVENTS_VEGA20_MAX] = {
	{ .name = "xgmi_link0_data_outbound",
			.config = "event=0x7,instance=0x46,umask=0x2" },
	{ .name = "xgmi_link1_data_outbound",
			.config = "event=0x7,instance=0x47,umask=0x2" }
};

static struct amdgpu_pmu_type vega20_types[NUM_EVENT_TYPES_VEGA20] = {
	{ .type = AMDGPU_PMU_EVENT_CONFIG_TYPE_XGMI,
					.num_of_type = NUM_EVENTS_VEGA20_XGMI }
};

static struct amdgpu_pmu_config vega20_config = {
	.formats = amdgpu_pmu_formats,
	.num_formats = ARRAY_SIZE(amdgpu_pmu_formats),
	.events = vega20_events,
	.num_events = ARRAY_SIZE(vega20_events),
	.types = vega20_types,
	.num_types = ARRAY_SIZE(vega20_types)
};

/* Vega20 data fabric (DF) events */
static struct amdgpu_pmu_attr df_vega20_formats[NUM_FORMATS_DF_VEGA20] = {
	{ .name = "event", .config = "config:0-7" },
	{ .name = "instance", .config = "config:8-15" },
	{ .name = "umask", .config = "config:16-23"}
};

static struct amdgpu_pmu_attr df_vega20_events[NUM_EVENTS_DF_VEGA20] = {
	{ .name = "cake0_pcsout_txdata",
			.config = "event=0x7,instance=0x46,umask=0x2" },
	{ .name = "cake1_pcsout_txdata",
			.config = "event=0x7,instance=0x47,umask=0x2" },
	{ .name = "cake0_pcsout_txmeta",
			.config = "event=0x7,instance=0x46,umask=0x4" },
	{ .name = "cake1_pcsout_txmeta",
			.config = "event=0x7,instance=0x47,umask=0x4" },
	{ .name = "cake0_ftiinstat_reqalloc",
			.config = "event=0xb,instance=0x46,umask=0x4" },
	{ .name = "cake1_ftiinstat_reqalloc",
			.config = "event=0xb,instance=0x47,umask=0x4" },
	{ .name = "cake0_ftiinstat_rspalloc",
			.config = "event=0xb,instance=0x46,umask=0x8" },
	{ .name = "cake1_ftiinstat_rspalloc",
			.config = "event=0xb,instance=0x47,umask=0x8" }
};

static struct amdgpu_pmu_config df_vega20_config = {
	.formats = df_vega20_formats,
	.num_formats = ARRAY_SIZE(df_vega20_formats),
	.events = df_vega20_events,
	.num_events = ARRAY_SIZE(df_vega20_events),
	.types = NULL,
	.num_types = 0
};

/* Arcturus events */
static struct amdgpu_pmu_attr arcturus_events[NUM_EVENTS_ARCTURUS_MAX] = {
	{ .name = "xgmi_link0_data_outbound",
			.config = "event=0x7,instance=0x4b,umask=0x2" },
	{ .name = "xgmi_link1_data_outbound",
			.config = "event=0x7,instance=0x4c,umask=0x2" },
	{ .name = "xgmi_link2_data_outbound",
			.config = "event=0x7,instance=0x4d,umask=0x2" },
	{ .name = "xgmi_link3_data_outbound",
			.config = "event=0x7,instance=0x4e,umask=0x2" },
	{ .name = "xgmi_link4_data_outbound",
			.config = "event=0x7,instance=0x4f,umask=0x2" },
	{ .name = "xgmi_link5_data_outbound",
			.config = "event=0x7,instance=0x50,umask=0x2" }
};

static struct amdgpu_pmu_type arcturus_types[NUM_EVENT_TYPES_ARCTURUS] = {
	{ .type = AMDGPU_PMU_EVENT_CONFIG_TYPE_XGMI,
				.num_of_type = NUM_EVENTS_ARCTURUS_XGMI }
};

static struct amdgpu_pmu_config arcturus_config = {
	.formats = amdgpu_pmu_formats,
	.num_formats = ARRAY_SIZE(amdgpu_pmu_formats),
	.events = arcturus_events,
	.num_events = ARRAY_SIZE(arcturus_events),
	.types = arcturus_types,
	.num_types = ARRAY_SIZE(arcturus_types)
};

/* initialize perf counter */
static int amdgpu_perf_event_init(struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;

	/* test the event attr type check for PMU enumeration */
	if (event->attr.type != event->pmu->type)
		return -ENOENT;

	/* update the hw_perf_event struct with config data */
	hwc->config = event->attr.config;
	hwc->config_base = AMDGPU_PMU_PERF_TYPE_NONE;

	return 0;
}

/* start perf counter */
static void amdgpu_perf_start(struct perf_event *event, int flags)
{
	struct hw_perf_event *hwc = &event->hw;
	struct amdgpu_pmu_entry *pe = container_of(event->pmu,
						  struct amdgpu_pmu_entry,
						  pmu);
	int target_cntr = 0;

	if (WARN_ON_ONCE(!(hwc->state & PERF_HES_STOPPED)))
		return;

	WARN_ON_ONCE(!(hwc->state & PERF_HES_UPTODATE));
	hwc->state = 0;

	switch (hwc->config_base) {
	case AMDGPU_PMU_EVENT_CONFIG_TYPE_DF:
	case AMDGPU_PMU_EVENT_CONFIG_TYPE_XGMI:
		if (!(flags & PERF_EF_RELOAD)) {
			target_cntr = pe->adev->df.funcs->pmc_start(pe->adev,
						hwc->config, 0 /* unused */,
						1 /* add counter */);
			if (target_cntr < 0)
				break;

			hwc->idx = target_cntr;
		}

		pe->adev->df.funcs->pmc_start(pe->adev, hwc->config,
								hwc->idx, 0);
		break;
	default:
		break;
	}

	perf_event_update_userpage(event);
}

/* read perf counter */
static void amdgpu_perf_read(struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	struct amdgpu_pmu_entry *pe = container_of(event->pmu,
						  struct amdgpu_pmu_entry,
						  pmu);
	u64 count, prev;

	do {
		prev = local64_read(&hwc->prev_count);

		switch (hwc->config_base) {
		case AMDGPU_PMU_EVENT_CONFIG_TYPE_DF:
		case AMDGPU_PMU_EVENT_CONFIG_TYPE_XGMI:
			pe->adev->df.funcs->pmc_get_count(pe->adev,
						hwc->config, hwc->idx, &count);
			break;
		default:
			count = 0;
			break;
		}
	} while (local64_cmpxchg(&hwc->prev_count, prev, count) != prev);

	local64_add(count - prev, &event->count);
}

/* stop perf counter */
static void amdgpu_perf_stop(struct perf_event *event, int flags)
{
	struct hw_perf_event *hwc = &event->hw;
	struct amdgpu_pmu_entry *pe = container_of(event->pmu,
						  struct amdgpu_pmu_entry,
						  pmu);

	if (hwc->state & PERF_HES_UPTODATE)
		return;

	switch (hwc->config_base) {
	case AMDGPU_PMU_EVENT_CONFIG_TYPE_DF:
	case AMDGPU_PMU_EVENT_CONFIG_TYPE_XGMI:
		pe->adev->df.funcs->pmc_stop(pe->adev, hwc->config, hwc->idx,
									0);
		break;
	default:
		break;
	}

	WARN_ON_ONCE(hwc->state & PERF_HES_STOPPED);
	hwc->state |= PERF_HES_STOPPED;

	if (hwc->state & PERF_HES_UPTODATE)
		return;

	amdgpu_perf_read(event);
	hwc->state |= PERF_HES_UPTODATE;
}

/* add perf counter */
static int amdgpu_perf_add(struct perf_event *event, int flags)
{
	struct hw_perf_event *hwc = &event->hw;
	int retval = 0, target_cntr;
	struct amdgpu_pmu_entry *pe = container_of(event->pmu,
						  struct amdgpu_pmu_entry,
						  pmu);

	switch (pe->pmu_perf_type) {
	case AMDGPU_PMU_PERF_TYPE_DF:
		hwc->config_base = AMDGPU_PMU_EVENT_CONFIG_TYPE_DF;
		break;
	case AMDGPU_PMU_PERF_TYPE_ALL:
		hwc->config_base = (hwc->config >>
					AMDGPU_PMU_EVENT_CONFIG_TYPE_SHIFT) &
					AMDGPU_PMU_EVENT_CONFIG_TYPE_MASK;
		break;
	}

	event->hw.state = PERF_HES_UPTODATE | PERF_HES_STOPPED;

	switch (hwc->config_base) {
	case AMDGPU_PMU_EVENT_CONFIG_TYPE_DF:
	case AMDGPU_PMU_EVENT_CONFIG_TYPE_XGMI:
		target_cntr = pe->adev->df.funcs->pmc_start(pe->adev,
						hwc->config, 0 /* unused */,
						1 /* add counter */);
		if (target_cntr < 0)
			retval = target_cntr;
		else
			hwc->idx = target_cntr;

		break;
	default:
		return 0;
	}

	if (retval)
		return retval;

	if (flags & PERF_EF_START)
		amdgpu_perf_start(event, PERF_EF_RELOAD);

	return retval;
}

/* delete perf counter  */
static void amdgpu_perf_del(struct perf_event *event, int flags)
{
	struct hw_perf_event *hwc = &event->hw;
	struct amdgpu_pmu_entry *pe = container_of(event->pmu,
						  struct amdgpu_pmu_entry,
						  pmu);

	amdgpu_perf_stop(event, PERF_EF_UPDATE);

	switch (hwc->config_base) {
	case AMDGPU_PMU_EVENT_CONFIG_TYPE_DF:
	case AMDGPU_PMU_EVENT_CONFIG_TYPE_XGMI:
		pe->adev->df.funcs->pmc_stop(pe->adev, hwc->config, hwc->idx,
									1);
		break;
	default:
		break;
	}

	perf_event_update_userpage(event);
}

static void amdgpu_pmu_create_event_attrs_by_type(
				struct attribute_group *attr_group,
				struct amdgpu_pmu_event_attribute *pmu_attr,
				struct amdgpu_pmu_attr events[],
				int s_offset,
				int e_offset,
				unsigned int type)
{
	int i;

	pmu_attr += s_offset;

	for (i = s_offset; i < e_offset; i++) {
		attr_group->attrs[i] = &pmu_attr->attr.attr;
		sysfs_attr_init(&pmu_attr->attr.attr);
		pmu_attr->attr.attr.name = events[i].name;
		pmu_attr->attr.attr.mode = 0444;
		pmu_attr->attr.show = amdgpu_pmu_event_show;
		pmu_attr->event_str = events[i].config;
		pmu_attr->type = type;
		pmu_attr++;
	}
}

static void amdgpu_pmu_create_attrs(struct attribute_group *attr_group,
				struct amdgpu_pmu_event_attribute *pmu_attr,
				struct amdgpu_pmu_attr events[],
				int num_events)
{
	amdgpu_pmu_create_event_attrs_by_type(attr_group, pmu_attr, events, 0,
				num_events, AMDGPU_PMU_EVENT_CONFIG_TYPE_NONE);
}


static int amdgpu_pmu_alloc_pmu_attrs(
				struct attribute_group *fmt_attr_group,
				struct amdgpu_pmu_event_attribute **fmt_attr,
				struct attribute_group *evt_attr_group,
				struct amdgpu_pmu_event_attribute **evt_attr,
				struct amdgpu_pmu_config *config)
{
	*fmt_attr = kcalloc(config->num_formats, sizeof(**fmt_attr),
								GFP_KERNEL);

	if (!(*fmt_attr))
		return -ENOMEM;

	fmt_attr_group->attrs = kcalloc(config->num_formats + 1,
				sizeof(*fmt_attr_group->attrs), GFP_KERNEL);

	if (!fmt_attr_group->attrs)
		goto err_fmt_attr_grp;

	*evt_attr = kcalloc(config->num_events, sizeof(**evt_attr), GFP_KERNEL);

	if (!(*evt_attr))
		goto err_evt_attr;

	evt_attr_group->attrs = kcalloc(config->num_events + 1,
				sizeof(*evt_attr_group->attrs), GFP_KERNEL);

	if (!evt_attr_group->attrs)
		goto err_evt_attr_grp;

	return 0;
err_evt_attr_grp:
	kfree(*evt_attr);
err_evt_attr:
	kfree(fmt_attr_group->attrs);
err_fmt_attr_grp:
	kfree(*fmt_attr);
	return -ENOMEM;
}

/* init pmu tracking per pmu type */
static int init_pmu_entry_by_type_and_add(struct amdgpu_pmu_entry *pmu_entry,
			struct amdgpu_pmu_config *config)
{
	const struct attribute_group *attr_groups[] = {
		&pmu_entry->fmt_attr_group,
		&pmu_entry->evt_attr_group,
		NULL
	};
	char pmu_name[PMU_NAME_SIZE];
	int ret = 0, total_num_events = 0;

	pmu_entry->pmu = (struct pmu){
		.event_init = amdgpu_perf_event_init,
		.add = amdgpu_perf_add,
		.del = amdgpu_perf_del,
		.start = amdgpu_perf_start,
		.stop = amdgpu_perf_stop,
		.read = amdgpu_perf_read,
		.task_ctx_nr = perf_invalid_context,
	};

	ret = amdgpu_pmu_alloc_pmu_attrs(&pmu_entry->fmt_attr_group,
					&pmu_entry->fmt_attr,
					&pmu_entry->evt_attr_group,
					&pmu_entry->evt_attr,
					config);

	if (ret)
		goto err_out;

	amdgpu_pmu_create_attrs(&pmu_entry->fmt_attr_group, pmu_entry->fmt_attr,
					config->formats, config->num_formats);

	if (pmu_entry->pmu_perf_type == AMDGPU_PMU_PERF_TYPE_ALL) {
		int i;

		for (i = 0; i < config->num_types; i++) {
			amdgpu_pmu_create_event_attrs_by_type(
					&pmu_entry->evt_attr_group,
					pmu_entry->evt_attr,
					config->events,
					total_num_events,
					total_num_events +
						config->types[i].num_of_type,
					config->types[i].type);
			total_num_events += config->types[i].num_of_type;
		}
	} else {
		amdgpu_pmu_create_attrs(&pmu_entry->evt_attr_group,
					pmu_entry->evt_attr,
					config->events, config->num_events);
		total_num_events = config->num_events;
	}

	pmu_entry->pmu.attr_groups = kmemdup(attr_groups, sizeof(attr_groups),
								GFP_KERNEL);

	if (!pmu_entry->pmu.attr_groups)
		goto err_attr_group;

	snprintf(pmu_name, PMU_NAME_SIZE, "%s_%d", pmu_entry->pmu_file_prefix,
				adev_to_drm(pmu_entry->adev)->primary->index);

	ret = perf_pmu_register(&pmu_entry->pmu, pmu_name, -1);

	if (ret)
		goto err_register;

	if (pmu_entry->pmu_perf_type != AMDGPU_PMU_PERF_TYPE_ALL)
		pr_info("Detected AMDGPU %s Counters. # of Counters = %d.\n",
				pmu_entry->pmu_type_name, total_num_events);
	else
		pr_info("Detected AMDGPU %d Perf Events.\n", total_num_events);


	list_add_tail(&pmu_entry->entry, &amdgpu_pmu_list);

	return 0;
err_register:
	kfree(pmu_entry->pmu.attr_groups);
err_attr_group:
	kfree(pmu_entry->fmt_attr_group.attrs);
	kfree(pmu_entry->fmt_attr);
	kfree(pmu_entry->evt_attr_group.attrs);
	kfree(pmu_entry->evt_attr);
err_out:
	pr_warn("Error initializing AMDGPU %s PMUs.\n",
						pmu_entry->pmu_type_name);
	return ret;
}

/* destroy all pmu data associated with target device */
void amdgpu_pmu_fini(struct amdgpu_device *adev)
{
	struct amdgpu_pmu_entry *pe, *temp;

	list_for_each_entry_safe(pe, temp, &amdgpu_pmu_list, entry) {
		if (pe->adev != adev)
			continue;
		list_del(&pe->entry);
		perf_pmu_unregister(&pe->pmu);
		kfree(pe->pmu.attr_groups);
		kfree(pe->fmt_attr_group.attrs);
		kfree(pe->fmt_attr);
		kfree(pe->evt_attr_group.attrs);
		kfree(pe->evt_attr);
		kfree(pe);
	}
}

static struct amdgpu_pmu_entry *create_pmu_entry(struct amdgpu_device *adev,
						unsigned int pmu_type,
						char *pmu_type_name,
						char *pmu_file_prefix)
{
	struct amdgpu_pmu_entry *pmu_entry;

	pmu_entry = kzalloc(sizeof(struct amdgpu_pmu_entry), GFP_KERNEL);

	if (!pmu_entry)
		return pmu_entry;

	pmu_entry->adev = adev;
	pmu_entry->fmt_attr_group.name = "format";
	pmu_entry->fmt_attr_group.attrs = NULL;
	pmu_entry->evt_attr_group.name = "events";
	pmu_entry->evt_attr_group.attrs = NULL;
	pmu_entry->pmu_perf_type = pmu_type;
	pmu_entry->pmu_type_name = pmu_type_name;
	pmu_entry->pmu_file_prefix = pmu_file_prefix;

	return pmu_entry;
}

/* init amdgpu_pmu */
int amdgpu_pmu_init(struct amdgpu_device *adev)
{
	int ret = 0;
	struct amdgpu_pmu_entry *pmu_entry, *pmu_entry_df;

	switch (adev->asic_type) {
	case CHIP_VEGA20:
		pmu_entry_df = create_pmu_entry(adev, AMDGPU_PMU_PERF_TYPE_DF,
						"DF", "amdgpu_df");

		if (!pmu_entry_df)
			return -ENOMEM;

		ret = init_pmu_entry_by_type_and_add(pmu_entry_df,
							&df_vega20_config);

		if (ret) {
			kfree(pmu_entry_df);
			return ret;
		}

		pmu_entry = create_pmu_entry(adev, AMDGPU_PMU_PERF_TYPE_ALL,
						"", "amdgpu");

		if (!pmu_entry) {
			amdgpu_pmu_fini(adev);
			return -ENOMEM;
		}

		ret = init_pmu_entry_by_type_and_add(pmu_entry,
							&vega20_config);

		if (ret) {
			kfree(pmu_entry);
			amdgpu_pmu_fini(adev);
			return ret;
		}

		break;
	case CHIP_ARCTURUS:
		pmu_entry = create_pmu_entry(adev, AMDGPU_PMU_PERF_TYPE_ALL,
						"", "amdgpu");
		if (!pmu_entry)
			return -ENOMEM;

		ret = init_pmu_entry_by_type_and_add(pmu_entry,
							&arcturus_config);

		if (ret) {
			kfree(pmu_entry);
			return -ENOMEM;
		}

		break;

	default:
		return 0;
	}

	return ret;
}
