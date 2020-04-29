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
 * Author: Jonathan Kim <jonathan.kim@amd.com>
 *
 */

#include <linux/perf_event.h>
#include <linux/init.h>
#include "amdgpu.h"
#include "amdgpu_pmu.h"
#include "df_v3_6.h"

#define PMU_NAME_SIZE 32

/* record to keep track of pmu entry per pmu type per device */
struct amdgpu_pmu_entry {
	struct list_head entry;
	struct amdgpu_device *adev;
	struct pmu pmu;
	unsigned int pmu_perf_type;
};

static LIST_HEAD(amdgpu_pmu_list);


/* initialize perf counter */
static int amdgpu_perf_event_init(struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;

	/* test the event attr type check for PMU enumeration */
	if (event->attr.type != event->pmu->type)
		return -ENOENT;

	/* update the hw_perf_event struct with config data */
	hwc->config = event->attr.config;

	return 0;
}

/* start perf counter */
static void amdgpu_perf_start(struct perf_event *event, int flags)
{
	struct hw_perf_event *hwc = &event->hw;
	struct amdgpu_pmu_entry *pe = container_of(event->pmu,
						  struct amdgpu_pmu_entry,
						  pmu);

	if (WARN_ON_ONCE(!(hwc->state & PERF_HES_STOPPED)))
		return;

	WARN_ON_ONCE(!(hwc->state & PERF_HES_UPTODATE));
	hwc->state = 0;

	switch (pe->pmu_perf_type) {
	case PERF_TYPE_AMDGPU_DF:
		if (!(flags & PERF_EF_RELOAD))
			pe->adev->df.funcs->pmc_start(pe->adev, hwc->config, 1);

		pe->adev->df.funcs->pmc_start(pe->adev, hwc->config, 0);
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

		switch (pe->pmu_perf_type) {
		case PERF_TYPE_AMDGPU_DF:
			pe->adev->df.funcs->pmc_get_count(pe->adev, hwc->config,
							  &count);
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

	switch (pe->pmu_perf_type) {
	case PERF_TYPE_AMDGPU_DF:
		pe->adev->df.funcs->pmc_stop(pe->adev, hwc->config, 0);
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

/* add perf counter  */
static int amdgpu_perf_add(struct perf_event *event, int flags)
{
	struct hw_perf_event *hwc = &event->hw;
	int retval;

	struct amdgpu_pmu_entry *pe = container_of(event->pmu,
						  struct amdgpu_pmu_entry,
						  pmu);

	event->hw.state = PERF_HES_UPTODATE | PERF_HES_STOPPED;

	switch (pe->pmu_perf_type) {
	case PERF_TYPE_AMDGPU_DF:
		retval = pe->adev->df.funcs->pmc_start(pe->adev,
						       hwc->config, 1);
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

	switch (pe->pmu_perf_type) {
	case PERF_TYPE_AMDGPU_DF:
		pe->adev->df.funcs->pmc_stop(pe->adev, hwc->config, 1);
		break;
	default:
		break;
	}

	perf_event_update_userpage(event);
}

/* vega20 pmus */

/* init pmu tracking per pmu type */
static int init_pmu_by_type(struct amdgpu_device *adev,
		  const struct attribute_group *attr_groups[],
		  char *pmu_type_name, char *pmu_file_prefix,
		  unsigned int pmu_perf_type,
		  unsigned int num_counters)
{
	char pmu_name[PMU_NAME_SIZE];
	struct amdgpu_pmu_entry *pmu_entry;
	int ret = 0;

	pmu_entry = kzalloc(sizeof(struct amdgpu_pmu_entry), GFP_KERNEL);

	if (!pmu_entry)
		return -ENOMEM;

	pmu_entry->adev = adev;
	pmu_entry->pmu = (struct pmu){
		.event_init = amdgpu_perf_event_init,
		.add = amdgpu_perf_add,
		.del = amdgpu_perf_del,
		.start = amdgpu_perf_start,
		.stop = amdgpu_perf_stop,
		.read = amdgpu_perf_read,
		.task_ctx_nr = perf_invalid_context,
	};

	pmu_entry->pmu.attr_groups = attr_groups;
	pmu_entry->pmu_perf_type = pmu_perf_type;
	snprintf(pmu_name, PMU_NAME_SIZE, "%s_%d",
				pmu_file_prefix, adev->ddev->primary->index);

	ret = perf_pmu_register(&pmu_entry->pmu, pmu_name, -1);

	if (ret) {
		kfree(pmu_entry);
		pr_warn("Error initializing AMDGPU %s PMUs.\n", pmu_type_name);
		return ret;
	}

	pr_info("Detected AMDGPU %s Counters. # of Counters = %d.\n",
			pmu_type_name, num_counters);

	list_add_tail(&pmu_entry->entry, &amdgpu_pmu_list);

	return 0;
}

/* init amdgpu_pmu */
int amdgpu_pmu_init(struct amdgpu_device *adev)
{
	int ret = 0;

	switch (adev->asic_type) {
	case CHIP_VEGA20:
		/* init df */
		ret = init_pmu_by_type(adev, df_v3_6_attr_groups,
				       "DF", "amdgpu_df", PERF_TYPE_AMDGPU_DF,
				       DF_V3_6_MAX_COUNTERS);

		/* other pmu types go here*/
		break;
	default:
		return 0;
	}

	return 0;
}


/* destroy all pmu data associated with target device */
void amdgpu_pmu_fini(struct amdgpu_device *adev)
{
	struct amdgpu_pmu_entry *pe, *temp;

	list_for_each_entry_safe(pe, temp, &amdgpu_pmu_list, entry) {
		if (pe->adev == adev) {
			list_del(&pe->entry);
			perf_pmu_unregister(&pe->pmu);
			kfree(pe);
		}
	}
}
