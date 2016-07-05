/*
 * Copyright(C) 2015 Linaro Limited. All rights reserved.
 * Author: Mathieu Poirier <mathieu.poirier@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/coresight.h>
#include <linux/coresight-pmu.h>
#include <linux/cpumask.h>
#include <linux/device.h>
#include <linux/list.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/parser.h>
#include <linux/perf_event.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/workqueue.h>

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
	struct list_head **path;
};

/**
 * struct perf_pmu_drv_config - Driver specific configuration needed
 *				before a session can start.
 * @sink:		The name of the sink this session should use.
 * @entry:		Hook to the event->drv_configs list.
 */
struct perf_pmu_drv_config {
	char *sink;
	struct list_head entry;
};

static DEFINE_PER_CPU(struct perf_output_handle, ctx_handle);
static DEFINE_PER_CPU(struct coresight_device *, csdev_src);

/* ETMv3.5/PTM's ETMCR is 'config' */
PMU_FORMAT_ATTR(cycacc,		"config:" __stringify(ETM_OPT_CYCACC));
PMU_FORMAT_ATTR(timestamp,	"config:" __stringify(ETM_OPT_TS));

static struct attribute *etm_config_formats_attr[] = {
	&format_attr_cycacc.attr,
	&format_attr_timestamp.attr,
	NULL,
};

static struct attribute_group etm_pmu_format_group = {
	.name   = "format",
	.attrs  = etm_config_formats_attr,
};

static const struct attribute_group *etm_pmu_attr_groups[] = {
	&etm_pmu_format_group,
	NULL,
};

static void etm_event_read(struct perf_event *event) {}

static int etm_event_init(struct perf_event *event)
{
	if (event->attr.type != etm_pmu.type)
		return -ENOENT;

	return 0;
}

static void free_event_data(struct work_struct *work)
{
	int cpu;
	cpumask_t *mask;
	struct etm_event_data *event_data;
	struct coresight_device *sink;

	event_data = container_of(work, struct etm_event_data, work);
	mask = &event_data->mask;
	/*
	 * First deal with the sink configuration.  See comment in
	 * etm_setup_aux() about why we take the first available path.
	 */
	if (event_data->snk_config) {
		cpu = cpumask_first(mask);
		sink = coresight_get_sink(event_data->path[cpu]);
		if (sink_ops(sink)->free_buffer)
			sink_ops(sink)->free_buffer(event_data->snk_config);
	}

	for_each_cpu(cpu, mask) {
		if (event_data->path[cpu])
			coresight_release_path(event_data->path[cpu]);
	}

	kfree(event_data->path);
	kfree(event_data);
}

static void *alloc_event_data(int cpu)
{
	int size;
	cpumask_t *mask;
	struct etm_event_data *event_data;

	/* First get memory for the session's data */
	event_data = kzalloc(sizeof(struct etm_event_data), GFP_KERNEL);
	if (!event_data)
		return NULL;

	/* Make sure nothing disappears under us */
	get_online_cpus();
	size = num_online_cpus();

	mask = &event_data->mask;
	if (cpu != -1)
		cpumask_set_cpu(cpu, mask);
	else
		cpumask_copy(mask, cpu_online_mask);
	put_online_cpus();

	/*
	 * Each CPU has a single path between source and destination.  As such
	 * allocate an array using CPU numbers as indexes.  That way a path
	 * for any CPU can easily be accessed at any given time.  We proceed
	 * the same way for sessions involving a single CPU.  The cost of
	 * unused memory when dealing with single CPU trace scenarios is small
	 * compared to the cost of searching through an optimized array.
	 */
	event_data->path = kcalloc(size,
				   sizeof(struct list_head *), GFP_KERNEL);
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

static void *etm_setup_aux(struct perf_event *event, void **pages,
			   int nr_pages, bool overwrite)
{
	int cpu;
	char *sink_def = NULL;
	cpumask_t *mask;
	struct coresight_device *sink;
	struct etm_event_data *event_data = NULL;
	struct perf_pmu_drv_config *drv_config;

	/*
	 * Search the driver configurables looking for a sink.  If more than
	 * one sink was specified the last one is taken.
	 */
	list_for_each_entry(drv_config, &event->drv_configs, entry) {
		if (drv_config && drv_config->sink) {
			sink_def = drv_config->sink;
			break;
		}
	}

	event_data = alloc_event_data(event->cpu);
	if (!event_data)
		return NULL;

	INIT_WORK(&event_data->work, free_event_data);

	mask = &event_data->mask;

	/* Setup the path for each CPU in a trace session */
	for_each_cpu(cpu, mask) {
		struct coresight_device *csdev;

		csdev = per_cpu(csdev_src, cpu);
		if (!csdev)
			goto err;

		/*
		 * Building a path doesn't enable it, it simply builds a
		 * list of devices from source to sink that can be
		 * referenced later when the path is actually needed.
		 */
		event_data->path[cpu] = coresight_build_path(csdev, sink_def);
		if (!event_data->path[cpu])
			goto err;
	}

	/*
	 * In theory nothing prevent tracers in a trace session from being
	 * associated with different sinks, nor having a sink per tracer.  But
	 * until we have HW with this kind of topology and a way to convey
	 * sink assignement from the perf cmd line we need to assume tracers
	 * in a trace session are using the same sink.  Therefore pick the sink
	 * found at the end of the first available path.
	 */
	cpu = cpumask_first(mask);
	/* Grab the sink at the end of the path */
	sink = coresight_get_sink(event_data->path[cpu]);
	if (!sink)
		goto err;

	if (!sink_ops(sink)->alloc_buffer)
		goto err;

	/* Get the AUX specific data from the sink buffer */
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

	if (!csdev)
		goto fail;

	/*
	 * Deal with the ring buffer API and get a handle on the
	 * session's information.
	 */
	event_data = perf_aux_output_begin(handle, event);
	if (!event_data)
		goto fail;

	/* We need a sink, no need to continue without one */
	sink = coresight_get_sink(event_data->path[cpu]);
	if (WARN_ON_ONCE(!sink || !sink_ops(sink)->set_buffer))
		goto fail_end_stop;

	/* Configure the sink */
	if (sink_ops(sink)->set_buffer(sink, handle,
				       event_data->snk_config))
		goto fail_end_stop;

	/* Nothing will happen without a path */
	if (coresight_enable_path(event_data->path[cpu], CS_MODE_PERF))
		goto fail_end_stop;

	/* Tell the perf core the event is alive */
	event->hw.state = 0;

	/* Finally enable the tracer */
	if (source_ops(csdev)->enable(csdev, &event->attr, CS_MODE_PERF))
		goto fail_end_stop;

out:
	return;

fail_end_stop:
	perf_aux_output_end(handle, 0, true);
fail:
	event->hw.state = PERF_HES_STOPPED;
	goto out;
}

static void etm_event_stop(struct perf_event *event, int mode)
{
	bool lost;
	int cpu = smp_processor_id();
	unsigned long size;
	struct coresight_device *sink, *csdev = per_cpu(csdev_src, cpu);
	struct perf_output_handle *handle = this_cpu_ptr(&ctx_handle);
	struct etm_event_data *event_data = perf_get_aux(handle);

	if (event->hw.state == PERF_HES_STOPPED)
		return;

	if (!csdev)
		return;

	sink = coresight_get_sink(event_data->path[cpu]);
	if (!sink)
		return;

	/* stop tracer */
	source_ops(csdev)->disable(csdev);

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
						    event_data->snk_config,
						    &lost);

		perf_aux_output_end(handle, size, lost);
	}

	/* Disabling the path make its elements available to other sessions */
	coresight_disable_path(event_data->path[cpu]);
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

enum {
	ETM_TOKEN_SINK_CPU,
	ETM_TOKEN_SINK,
	ETM_TOKEN_ERR,
};

static const match_table_t drv_cfg_tokens = {
	{ETM_TOKEN_SINK_CPU, "sink=cpu%d:%s"},
	{ETM_TOKEN_SINK, "sink=%s"},
	{ETM_TOKEN_ERR,	NULL},
};

static int etm_get_drv_configs(struct perf_event *event, void __user *arg)
{
	char *config, *sink = NULL;
	int cpu = -1, token, ret = 0;
	substring_t args[MAX_OPT_ARGS];
	struct perf_pmu_drv_config *drv_config = NULL;

	/* Make user supplied input usable */
	config = strndup_user(arg, PAGE_SIZE);
	if (IS_ERR(config))
		return PTR_ERR(config);

	/* See above declared @drv_cfg_tokens for the usable formats */
	token = match_token(config, drv_cfg_tokens, args);
	switch (token) {
	case ETM_TOKEN_SINK:
		/* Just a sink has been specified */
		sink = match_strdup(&args[0]);
		if (IS_ERR(sink)) {
			ret = PTR_ERR(sink);
			goto err;
		}
		break;
	case ETM_TOKEN_SINK_CPU:
		/* We have a sink and a CPU */
		if (match_int(&args[0], &cpu)) {
			ret = -EINVAL;
			goto err;
		}
		sink = match_strdup(&args[1]);
		if (IS_ERR(sink)) {
			ret = PTR_ERR(sink);
			goto err;
		}
		break;
	default:
		ret = -EINVAL;
		goto err;
	}

	/* If the CPUs don't match the sink is destined to another path */
	if (event->cpu != cpu)
		goto err;

	/*
	 * We have a valid configuration, allocate memory and add to the list
	 * of driver configurables.
	 */
	drv_config = kzalloc(sizeof(*drv_config), GFP_KERNEL);
	if (IS_ERR(drv_config)) {
		ret = PTR_ERR(drv_config);
		goto err;
	}

	drv_config->sink = sink;
	list_add(&drv_config->entry, &event->drv_configs);

out:
	kfree(config);
	return ret;

err:
	kfree(sink);
	goto out;
}

static void etm_free_drv_configs(struct perf_event *event)
{
	struct perf_pmu_drv_config *config, *itr;

	list_for_each_entry_safe(config, itr, &event->drv_configs, entry) {
		list_del(&config->entry);
		kfree(config->sink);
		kfree(config);
	}
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

	etm_pmu.capabilities	= PERF_PMU_CAP_EXCLUSIVE;

	etm_pmu.attr_groups	= etm_pmu_attr_groups;
	etm_pmu.task_ctx_nr	= perf_sw_context;
	etm_pmu.read		= etm_event_read;
	etm_pmu.event_init	= etm_event_init;
	etm_pmu.setup_aux	= etm_setup_aux;
	etm_pmu.free_aux	= etm_free_aux;
	etm_pmu.start		= etm_event_start;
	etm_pmu.stop		= etm_event_stop;
	etm_pmu.add		= etm_event_add;
	etm_pmu.del		= etm_event_del;
	etm_pmu.get_drv_configs	= etm_get_drv_configs;
	etm_pmu.free_drv_configs
				= etm_free_drv_configs;

	ret = perf_pmu_register(&etm_pmu, CORESIGHT_ETM_PMU_NAME, -1);
	if (ret == 0)
		etm_perf_up = true;

	return ret;
}
device_initcall(etm_perf_init);
