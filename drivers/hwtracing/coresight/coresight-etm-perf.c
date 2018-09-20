// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright(C) 2015 Linaro Limited. All rights reserved.
 * Author: Mathieu Poirier <mathieu.poirier@linaro.org>
 */

#include <linux/coresight.h>
#include <linux/coresight-pmu.h>
#include <linux/cpumask.h>
#include <linux/device.h>
#include <linux/list.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/perf_event.h>
#include <linux/percpu-defs.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/workqueue.h>

#include "coresight-etm-perf.h"
#include "coresight-priv.h"

static struct pmu etm_pmu;
static bool etm_perf_up;

/**
 * struct etm_event_data - Coresight specifics associated to an event
 * @work:		Handle to free allocated memory outside IRQ context.
 * @mask:		Hold the CPU(s) this event was set for.
 * @snk_config:		The sink configuration.
 * @path:		An array of path, each slot for one CPU.
 */
struct etm_event_data {
	struct work_struct work;
	cpumask_t mask;
	void *snk_config;
	struct list_head * __percpu *path;
};

static DEFINE_PER_CPU(struct perf_output_handle, ctx_handle);
static DEFINE_PER_CPU(struct coresight_device *, csdev_src);

/* ETMv3.5/PTM's ETMCR is 'config' */
PMU_FORMAT_ATTR(cycacc,		"config:" __stringify(ETM_OPT_CYCACC));
PMU_FORMAT_ATTR(timestamp,	"config:" __stringify(ETM_OPT_TS));
PMU_FORMAT_ATTR(retstack,	"config:" __stringify(ETM_OPT_RETSTK));

static struct attribute *etm_config_formats_attr[] = {
	&format_attr_cycacc.attr,
	&format_attr_timestamp.attr,
	&format_attr_retstack.attr,
	NULL,
};

static const struct attribute_group etm_pmu_format_group = {
	.name   = "format",
	.attrs  = etm_config_formats_attr,
};

static const struct attribute_group *etm_pmu_attr_groups[] = {
	&etm_pmu_format_group,
	NULL,
};

static inline struct list_head **
etm_event_cpu_path_ptr(struct etm_event_data *data, int cpu)
{
	return per_cpu_ptr(data->path, cpu);
}

static inline struct list_head *
etm_event_cpu_path(struct etm_event_data *data, int cpu)
{
	return *etm_event_cpu_path_ptr(data, cpu);
}

static void etm_event_read(struct perf_event *event) {}

static int etm_addr_filters_alloc(struct perf_event *event)
{
	struct etm_filters *filters;
	int node = event->cpu == -1 ? -1 : cpu_to_node(event->cpu);

	filters = kzalloc_node(sizeof(struct etm_filters), GFP_KERNEL, node);
	if (!filters)
		return -ENOMEM;

	if (event->parent)
		memcpy(filters, event->parent->hw.addr_filters,
		       sizeof(*filters));

	event->hw.addr_filters = filters;

	return 0;
}

static void etm_event_destroy(struct perf_event *event)
{
	kfree(event->hw.addr_filters);
	event->hw.addr_filters = NULL;
}

static int etm_event_init(struct perf_event *event)
{
	int ret = 0;

	if (event->attr.type != etm_pmu.type) {
		ret = -ENOENT;
		goto out;
	}

	ret = etm_addr_filters_alloc(event);
	if (ret)
		goto out;

	event->destroy = etm_event_destroy;
out:
	return ret;
}

static void free_event_data(struct work_struct *work)
{
	int cpu;
	cpumask_t *mask;
	struct etm_event_data *event_data;
	struct coresight_device *sink;

	event_data = container_of(work, struct etm_event_data, work);
	mask = &event_data->mask;

	/* Free the sink buffers, if there are any */
	if (event_data->snk_config && !WARN_ON(cpumask_empty(mask))) {
		cpu = cpumask_first(mask);
		sink = coresight_get_sink(etm_event_cpu_path(event_data, cpu));
		if (sink_ops(sink)->free_buffer)
			sink_ops(sink)->free_buffer(event_data->snk_config);
	}

	for_each_cpu(cpu, mask) {
		struct list_head **ppath;

		ppath = etm_event_cpu_path_ptr(event_data, cpu);
		if (!(IS_ERR_OR_NULL(*ppath)))
			coresight_release_path(*ppath);
		*ppath = NULL;
	}

	free_percpu(event_data->path);
	kfree(event_data);
}

static void *alloc_event_data(int cpu)
{
	cpumask_t *mask;
	struct etm_event_data *event_data;

	/* First get memory for the session's data */
	event_data = kzalloc(sizeof(struct etm_event_data), GFP_KERNEL);
	if (!event_data)
		return NULL;


	mask = &event_data->mask;
	if (cpu != -1)
		cpumask_set_cpu(cpu, mask);
	else
		cpumask_copy(mask, cpu_present_mask);

	/*
	 * Each CPU has a single path between source and destination.  As such
	 * allocate an array using CPU numbers as indexes.  That way a path
	 * for any CPU can easily be accessed at any given time.  We proceed
	 * the same way for sessions involving a single CPU.  The cost of
	 * unused memory when dealing with single CPU trace scenarios is small
	 * compared to the cost of searching through an optimized array.
	 */
	event_data->path = alloc_percpu(struct list_head *);

	if (!event_data->path) {
		kfree(event_data);
		return NULL;
	}

	return event_data;
}

static void etm_free_aux(void *data)
{
	struct etm_event_data *event_data = data;

	schedule_work(&event_data->work);
}

static void *etm_setup_aux(int event_cpu, void **pages,
			   int nr_pages, bool overwrite)
{
	int cpu;
	cpumask_t *mask;
	struct coresight_device *sink;
	struct etm_event_data *event_data = NULL;

	event_data = alloc_event_data(event_cpu);
	if (!event_data)
		return NULL;
	INIT_WORK(&event_data->work, free_event_data);

	/*
	 * In theory nothing prevent tracers in a trace session from being
	 * associated with different sinks, nor having a sink per tracer.  But
	 * until we have HW with this kind of topology we need to assume tracers
	 * in a trace session are using the same sink.  Therefore go through
	 * the coresight bus and pick the first enabled sink.
	 *
	 * When operated from sysFS users are responsible to enable the sink
	 * while from perf, the perf tools will do it based on the choice made
	 * on the cmd line.  As such the "enable_sink" flag in sysFS is reset.
	 */
	sink = coresight_get_enabled_sink(true);
	if (!sink || !sink_ops(sink)->alloc_buffer)
		goto err;

	mask = &event_data->mask;

	/*
	 * Setup the path for each CPU in a trace session. We try to build
	 * trace path for each CPU in the mask. If we don't find an ETM
	 * for the CPU or fail to build a path, we clear the CPU from the
	 * mask and continue with the rest. If ever we try to trace on those
	 * CPUs, we can handle it and fail the session.
	 */
	for_each_cpu(cpu, mask) {
		struct list_head *path;
		struct coresight_device *csdev;

		csdev = per_cpu(csdev_src, cpu);
		/*
		 * If there is no ETM associated with this CPU clear it from
		 * the mask and continue with the rest. If ever we try to trace
		 * on this CPU, we handle it accordingly.
		 */
		if (!csdev) {
			cpumask_clear_cpu(cpu, mask);
			continue;
		}

		/*
		 * Building a path doesn't enable it, it simply builds a
		 * list of devices from source to sink that can be
		 * referenced later when the path is actually needed.
		 */
		path = coresight_build_path(csdev, sink);
		if (IS_ERR(path)) {
			cpumask_clear_cpu(cpu, mask);
			continue;
		}

		*etm_event_cpu_path_ptr(event_data, cpu) = path;
	}

	/* If we don't have any CPUs ready for tracing, abort */
	cpu = cpumask_first(mask);
	if (cpu >= nr_cpu_ids)
		goto err;

	/* Allocate the sink buffer for this session */
	event_data->snk_config =
			sink_ops(sink)->alloc_buffer(sink, cpu, pages,
						     nr_pages, overwrite);
	if (!event_data->snk_config)
		goto err;

out:
	return event_data;

err:
	etm_free_aux(event_data);
	event_data = NULL;
	goto out;
}

static void etm_event_start(struct perf_event *event, int flags)
{
	int cpu = smp_processor_id();
	struct etm_event_data *event_data;
	struct perf_output_handle *handle = this_cpu_ptr(&ctx_handle);
	struct coresight_device *sink, *csdev = per_cpu(csdev_src, cpu);
	struct list_head *path;

	if (!csdev)
		goto fail;

	/*
	 * Deal with the ring buffer API and get a handle on the
	 * session's information.
	 */
	event_data = perf_aux_output_begin(handle, event);
	if (!event_data)
		goto fail;

	path = etm_event_cpu_path(event_data, cpu);
	/* We need a sink, no need to continue without one */
	sink = coresight_get_sink(path);
	if (WARN_ON_ONCE(!sink || !sink_ops(sink)->set_buffer))
		goto fail_end_stop;

	/* Configure the sink */
	if (sink_ops(sink)->set_buffer(sink, handle,
				       event_data->snk_config))
		goto fail_end_stop;

	/* Nothing will happen without a path */
	if (coresight_enable_path(path, CS_MODE_PERF))
		goto fail_end_stop;

	/* Tell the perf core the event is alive */
	event->hw.state = 0;

	/* Finally enable the tracer */
	if (source_ops(csdev)->enable(csdev, event, CS_MODE_PERF))
		goto fail_disable_path;

out:
	return;

fail_disable_path:
	coresight_disable_path(path);
fail_end_stop:
	perf_aux_output_flag(handle, PERF_AUX_FLAG_TRUNCATED);
	perf_aux_output_end(handle, 0);
fail:
	event->hw.state = PERF_HES_STOPPED;
	goto out;
}

static void etm_event_stop(struct perf_event *event, int mode)
{
	int cpu = smp_processor_id();
	unsigned long size;
	struct coresight_device *sink, *csdev = per_cpu(csdev_src, cpu);
	struct perf_output_handle *handle = this_cpu_ptr(&ctx_handle);
	struct etm_event_data *event_data = perf_get_aux(handle);
	struct list_head *path;

	if (event->hw.state == PERF_HES_STOPPED)
		return;

	if (!csdev)
		return;

	path = etm_event_cpu_path(event_data, cpu);
	if (!path)
		return;

	sink = coresight_get_sink(path);
	if (!sink)
		return;

	/* stop tracer */
	source_ops(csdev)->disable(csdev, event);

	/* tell the core */
	event->hw.state = PERF_HES_STOPPED;

	if (mode & PERF_EF_UPDATE) {
		if (WARN_ON_ONCE(handle->event != event))
			return;

		/* update trace information */
		if (!sink_ops(sink)->update_buffer)
			return;

		sink_ops(sink)->update_buffer(sink, handle,
					      event_data->snk_config);

		if (!sink_ops(sink)->reset_buffer)
			return;

		size = sink_ops(sink)->reset_buffer(sink, handle,
						    event_data->snk_config);

		perf_aux_output_end(handle, size);
	}

	/* Disabling the path make its elements available to other sessions */
	coresight_disable_path(path);
}

static int etm_event_add(struct perf_event *event, int mode)
{
	int ret = 0;
	struct hw_perf_event *hwc = &event->hw;

	if (mode & PERF_EF_START) {
		etm_event_start(event, 0);
		if (hwc->state & PERF_HES_STOPPED)
			ret = -EINVAL;
	} else {
		hwc->state = PERF_HES_STOPPED;
	}

	return ret;
}

static void etm_event_del(struct perf_event *event, int mode)
{
	etm_event_stop(event, PERF_EF_UPDATE);
}

static int etm_addr_filters_validate(struct list_head *filters)
{
	bool range = false, address = false;
	int index = 0;
	struct perf_addr_filter *filter;

	list_for_each_entry(filter, filters, entry) {
		/*
		 * No need to go further if there's no more
		 * room for filters.
		 */
		if (++index > ETM_ADDR_CMP_MAX)
			return -EOPNOTSUPP;

		/* filter::size==0 means single address trigger */
		if (filter->size) {
			/*
			 * The existing code relies on START/STOP filters
			 * being address filters.
			 */
			if (filter->action == PERF_ADDR_FILTER_ACTION_START ||
			    filter->action == PERF_ADDR_FILTER_ACTION_STOP)
				return -EOPNOTSUPP;

			range = true;
		} else
			address = true;

		/*
		 * At this time we don't allow range and start/stop filtering
		 * to cohabitate, they have to be mutually exclusive.
		 */
		if (range && address)
			return -EOPNOTSUPP;
	}

	return 0;
}

static void etm_addr_filters_sync(struct perf_event *event)
{
	struct perf_addr_filters_head *head = perf_event_addr_filters(event);
	unsigned long start, stop, *offs = event->addr_filters_offs;
	struct etm_filters *filters = event->hw.addr_filters;
	struct etm_filter *etm_filter;
	struct perf_addr_filter *filter;
	int i = 0;

	list_for_each_entry(filter, &head->list, entry) {
		start = filter->offset + offs[i];
		stop = start + filter->size;
		etm_filter = &filters->etm_filter[i];

		switch (filter->action) {
		case PERF_ADDR_FILTER_ACTION_FILTER:
			etm_filter->start_addr = start;
			etm_filter->stop_addr = stop;
			etm_filter->type = ETM_ADDR_TYPE_RANGE;
			break;
		case PERF_ADDR_FILTER_ACTION_START:
			etm_filter->start_addr = start;
			etm_filter->type = ETM_ADDR_TYPE_START;
			break;
		case PERF_ADDR_FILTER_ACTION_STOP:
			etm_filter->stop_addr = stop;
			etm_filter->type = ETM_ADDR_TYPE_STOP;
			break;
		}
		i++;
	}

	filters->nr_filters = i;
}

int etm_perf_symlink(struct coresight_device *csdev, bool link)
{
	char entry[sizeof("cpu9999999")];
	int ret = 0, cpu = source_ops(csdev)->cpu_id(csdev);
	struct device *pmu_dev = etm_pmu.dev;
	struct device *cs_dev = &csdev->dev;

	sprintf(entry, "cpu%d", cpu);

	if (!etm_perf_up)
		return -EPROBE_DEFER;

	if (link) {
		ret = sysfs_create_link(&pmu_dev->kobj, &cs_dev->kobj, entry);
		if (ret)
			return ret;
		per_cpu(csdev_src, cpu) = csdev;
	} else {
		sysfs_remove_link(&pmu_dev->kobj, entry);
		per_cpu(csdev_src, cpu) = NULL;
	}

	return 0;
}

static int __init etm_perf_init(void)
{
	int ret;

	etm_pmu.capabilities		= PERF_PMU_CAP_EXCLUSIVE;

	etm_pmu.attr_groups		= etm_pmu_attr_groups;
	etm_pmu.task_ctx_nr		= perf_sw_context;
	etm_pmu.read			= etm_event_read;
	etm_pmu.event_init		= etm_event_init;
	etm_pmu.setup_aux		= etm_setup_aux;
	etm_pmu.free_aux		= etm_free_aux;
	etm_pmu.start			= etm_event_start;
	etm_pmu.stop			= etm_event_stop;
	etm_pmu.add			= etm_event_add;
	etm_pmu.del			= etm_event_del;
	etm_pmu.addr_filters_sync	= etm_addr_filters_sync;
	etm_pmu.addr_filters_validate	= etm_addr_filters_validate;
	etm_pmu.nr_addr_filters		= ETM_ADDR_CMP_MAX;

	ret = perf_pmu_register(&etm_pmu, CORESIGHT_ETM_PMU_NAME, -1);
	if (ret == 0)
		etm_perf_up = true;

	return ret;
}
device_initcall(etm_perf_init);
