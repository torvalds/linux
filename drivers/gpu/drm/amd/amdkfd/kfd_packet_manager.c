/*
 * Copyright 2014 Advanced Micro Devices, Inc.
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

#include <linux/slab.h>
#include <linux/mutex.h>
#include "kfd_device_queue_manager.h"
#include "kfd_kernel_queue.h"
#include "kfd_priv.h"

static inline void inc_wptr(unsigned int *wptr, unsigned int increment_bytes,
				unsigned int buffer_size_bytes)
{
	unsigned int temp = *wptr + increment_bytes / sizeof(uint32_t);

	WARN((temp * sizeof(uint32_t)) > buffer_size_bytes,
	     "Runlist IB overflow");
	*wptr = temp;
}

static void pm_calc_rlib_size(struct packet_manager *pm,
				unsigned int *rlib_size,
				bool *over_subscription)
{
	unsigned int process_count, queue_count, compute_queue_count, gws_queue_count;
	unsigned int map_queue_size;
	unsigned int max_proc_per_quantum = 1;
	struct kfd_dev *dev = pm->dqm->dev;

	process_count = pm->dqm->processes_count;
	queue_count = pm->dqm->active_queue_count;
	compute_queue_count = pm->dqm->active_cp_queue_count;
	gws_queue_count = pm->dqm->gws_queue_count;

	/* check if there is over subscription
	 * Note: the arbitration between the number of VMIDs and
	 * hws_max_conc_proc has been done in
	 * kgd2kfd_device_init().
	 */
	*over_subscription = false;

	if (dev->max_proc_per_quantum > 1)
		max_proc_per_quantum = dev->max_proc_per_quantum;

	if ((process_count > max_proc_per_quantum) ||
	    compute_queue_count > get_cp_queues_num(pm->dqm) ||
	    gws_queue_count > 1) {
		*over_subscription = true;
		pr_debug("Over subscribed runlist\n");
	}

	map_queue_size = pm->pmf->map_queues_size;
	/* calculate run list ib allocation size */
	*rlib_size = process_count * pm->pmf->map_process_size +
		     queue_count * map_queue_size;

	/*
	 * Increase the allocation size in case we need a chained run list
	 * when over subscription
	 */
	if (*over_subscription)
		*rlib_size += pm->pmf->runlist_size;

	pr_debug("runlist ib size %d\n", *rlib_size);
}

static int pm_allocate_runlist_ib(struct packet_manager *pm,
				unsigned int **rl_buffer,
				uint64_t *rl_gpu_buffer,
				unsigned int *rl_buffer_size,
				bool *is_over_subscription)
{
	int retval;

	if (WARN_ON(pm->allocated))
		return -EINVAL;

	pm_calc_rlib_size(pm, rl_buffer_size, is_over_subscription);

	mutex_lock(&pm->lock);

	retval = kfd_gtt_sa_allocate(pm->dqm->dev, *rl_buffer_size,
					&pm->ib_buffer_obj);

	if (retval) {
		pr_err("Failed to allocate runlist IB\n");
		goto out;
	}

	*(void **)rl_buffer = pm->ib_buffer_obj->cpu_ptr;
	*rl_gpu_buffer = pm->ib_buffer_obj->gpu_addr;

	memset(*rl_buffer, 0, *rl_buffer_size);
	pm->allocated = true;

out:
	mutex_unlock(&pm->lock);
	return retval;
}

static int pm_create_runlist_ib(struct packet_manager *pm,
				struct list_head *queues,
				uint64_t *rl_gpu_addr,
				size_t *rl_size_bytes)
{
	unsigned int alloc_size_bytes;
	unsigned int *rl_buffer, rl_wptr, i;
	int retval, proccesses_mapped;
	struct device_process_node *cur;
	struct qcm_process_device *qpd;
	struct queue *q;
	struct kernel_queue *kq;
	bool is_over_subscription;

	rl_wptr = retval = proccesses_mapped = 0;

	retval = pm_allocate_runlist_ib(pm, &rl_buffer, rl_gpu_addr,
				&alloc_size_bytes, &is_over_subscription);
	if (retval)
		return retval;

	*rl_size_bytes = alloc_size_bytes;
	pm->ib_size_bytes = alloc_size_bytes;

	pr_debug("Building runlist ib process count: %d queues count %d\n",
		pm->dqm->processes_count, pm->dqm->active_queue_count);

	/* build the run list ib packet */
	list_for_each_entry(cur, queues, list) {
		qpd = cur->qpd;
		/* build map process packet */
		if (proccesses_mapped >= pm->dqm->processes_count) {
			pr_debug("Not enough space left in runlist IB\n");
			pm_release_ib(pm);
			return -ENOMEM;
		}

		retval = pm->pmf->map_process(pm, &rl_buffer[rl_wptr], qpd);
		if (retval)
			return retval;

		proccesses_mapped++;
		inc_wptr(&rl_wptr, pm->pmf->map_process_size,
				alloc_size_bytes);

		list_for_each_entry(kq, &qpd->priv_queue_list, list) {
			if (!kq->queue->properties.is_active)
				continue;

			pr_debug("static_queue, mapping kernel q %d, is debug status %d\n",
				kq->queue->queue, qpd->is_debug);

			retval = pm->pmf->map_queues(pm,
						&rl_buffer[rl_wptr],
						kq->queue,
						qpd->is_debug);
			if (retval)
				return retval;

			inc_wptr(&rl_wptr,
				pm->pmf->map_queues_size,
				alloc_size_bytes);
		}

		list_for_each_entry(q, &qpd->queues_list, list) {
			if (!q->properties.is_active)
				continue;

			pr_debug("static_queue, mapping user queue %d, is debug status %d\n",
				q->queue, qpd->is_debug);

			retval = pm->pmf->map_queues(pm,
						&rl_buffer[rl_wptr],
						q,
						qpd->is_debug);

			if (retval)
				return retval;

			inc_wptr(&rl_wptr,
				pm->pmf->map_queues_size,
				alloc_size_bytes);
		}
	}

	pr_debug("Finished map process and queues to runlist\n");

	if (is_over_subscription) {
		if (!pm->is_over_subscription)
			pr_warn("Runlist is getting oversubscribed. Expect reduced ROCm performance.\n");
		retval = pm->pmf->runlist(pm, &rl_buffer[rl_wptr],
					*rl_gpu_addr,
					alloc_size_bytes / sizeof(uint32_t),
					true);
	}
	pm->is_over_subscription = is_over_subscription;

	for (i = 0; i < alloc_size_bytes / sizeof(uint32_t); i++)
		pr_debug("0x%2X ", rl_buffer[i]);
	pr_debug("\n");

	return retval;
}

int pm_init(struct packet_manager *pm, struct device_queue_manager *dqm)
{
	switch (dqm->dev->device_info->asic_family) {
	case CHIP_KAVERI:
	case CHIP_HAWAII:
		/* PM4 packet structures on CIK are the same as on VI */
	case CHIP_CARRIZO:
	case CHIP_TONGA:
	case CHIP_FIJI:
	case CHIP_POLARIS10:
	case CHIP_POLARIS11:
	case CHIP_POLARIS12:
	case CHIP_VEGAM:
		pm->pmf = &kfd_vi_pm_funcs;
		break;
	case CHIP_VEGA10:
	case CHIP_VEGA12:
	case CHIP_VEGA20:
	case CHIP_RAVEN:
	case CHIP_RENOIR:
	case CHIP_ARCTURUS:
	case CHIP_NAVI10:
	case CHIP_NAVI12:
	case CHIP_NAVI14:
	case CHIP_SIENNA_CICHLID:
	case CHIP_NAVY_FLOUNDER:
	case CHIP_VANGOGH:
		pm->pmf = &kfd_v9_pm_funcs;
		break;
	default:
		WARN(1, "Unexpected ASIC family %u",
		     dqm->dev->device_info->asic_family);
		return -EINVAL;
	}

	pm->dqm = dqm;
	mutex_init(&pm->lock);
	pm->priv_queue = kernel_queue_init(dqm->dev, KFD_QUEUE_TYPE_HIQ);
	if (!pm->priv_queue) {
		mutex_destroy(&pm->lock);
		return -ENOMEM;
	}
	pm->allocated = false;

	return 0;
}

void pm_uninit(struct packet_manager *pm, bool hanging)
{
	mutex_destroy(&pm->lock);
	kernel_queue_uninit(pm->priv_queue, hanging);
}

int pm_send_set_resources(struct packet_manager *pm,
				struct scheduling_resources *res)
{
	uint32_t *buffer, size;
	int retval = 0;

	size = pm->pmf->set_resources_size;
	mutex_lock(&pm->lock);
	kq_acquire_packet_buffer(pm->priv_queue,
					size / sizeof(uint32_t),
					(unsigned int **)&buffer);
	if (!buffer) {
		pr_err("Failed to allocate buffer on kernel queue\n");
		retval = -ENOMEM;
		goto out;
	}

	retval = pm->pmf->set_resources(pm, buffer, res);
	if (!retval)
		kq_submit_packet(pm->priv_queue);
	else
		kq_rollback_packet(pm->priv_queue);

out:
	mutex_unlock(&pm->lock);

	return retval;
}

int pm_send_runlist(struct packet_manager *pm, struct list_head *dqm_queues)
{
	uint64_t rl_gpu_ib_addr;
	uint32_t *rl_buffer;
	size_t rl_ib_size, packet_size_dwords;
	int retval;

	retval = pm_create_runlist_ib(pm, dqm_queues, &rl_gpu_ib_addr,
					&rl_ib_size);
	if (retval)
		goto fail_create_runlist_ib;

	pr_debug("runlist IB address: 0x%llX\n", rl_gpu_ib_addr);

	packet_size_dwords = pm->pmf->runlist_size / sizeof(uint32_t);
	mutex_lock(&pm->lock);

	retval = kq_acquire_packet_buffer(pm->priv_queue,
					packet_size_dwords, &rl_buffer);
	if (retval)
		goto fail_acquire_packet_buffer;

	retval = pm->pmf->runlist(pm, rl_buffer, rl_gpu_ib_addr,
					rl_ib_size / sizeof(uint32_t), false);
	if (retval)
		goto fail_create_runlist;

	kq_submit_packet(pm->priv_queue);

	mutex_unlock(&pm->lock);

	return retval;

fail_create_runlist:
	kq_rollback_packet(pm->priv_queue);
fail_acquire_packet_buffer:
	mutex_unlock(&pm->lock);
fail_create_runlist_ib:
	pm_release_ib(pm);
	return retval;
}

int pm_send_query_status(struct packet_manager *pm, uint64_t fence_address,
			uint32_t fence_value)
{
	uint32_t *buffer, size;
	int retval = 0;

	if (WARN_ON(!fence_address))
		return -EFAULT;

	size = pm->pmf->query_status_size;
	mutex_lock(&pm->lock);
	kq_acquire_packet_buffer(pm->priv_queue,
			size / sizeof(uint32_t), (unsigned int **)&buffer);
	if (!buffer) {
		pr_err("Failed to allocate buffer on kernel queue\n");
		retval = -ENOMEM;
		goto out;
	}

	retval = pm->pmf->query_status(pm, buffer, fence_address, fence_value);
	if (!retval)
		kq_submit_packet(pm->priv_queue);
	else
		kq_rollback_packet(pm->priv_queue);

out:
	mutex_unlock(&pm->lock);
	return retval;
}

int pm_send_unmap_queue(struct packet_manager *pm, enum kfd_queue_type type,
			enum kfd_unmap_queues_filter filter,
			uint32_t filter_param, bool reset,
			unsigned int sdma_engine)
{
	uint32_t *buffer, size;
	int retval = 0;

	size = pm->pmf->unmap_queues_size;
	mutex_lock(&pm->lock);
	kq_acquire_packet_buffer(pm->priv_queue,
			size / sizeof(uint32_t), (unsigned int **)&buffer);
	if (!buffer) {
		pr_err("Failed to allocate buffer on kernel queue\n");
		retval = -ENOMEM;
		goto out;
	}

	retval = pm->pmf->unmap_queues(pm, buffer, type, filter, filter_param,
				       reset, sdma_engine);
	if (!retval)
		kq_submit_packet(pm->priv_queue);
	else
		kq_rollback_packet(pm->priv_queue);

out:
	mutex_unlock(&pm->lock);
	return retval;
}

void pm_release_ib(struct packet_manager *pm)
{
	mutex_lock(&pm->lock);
	if (pm->allocated) {
		kfd_gtt_sa_free(pm->dqm->dev, pm->ib_buffer_obj);
		pm->allocated = false;
	}
	mutex_unlock(&pm->lock);
}

#if defined(CONFIG_DEBUG_FS)

int pm_debugfs_runlist(struct seq_file *m, void *data)
{
	struct packet_manager *pm = data;

	mutex_lock(&pm->lock);

	if (!pm->allocated) {
		seq_puts(m, "  No active runlist\n");
		goto out;
	}

	seq_hex_dump(m, "  ", DUMP_PREFIX_OFFSET, 32, 4,
		     pm->ib_buffer_obj->cpu_ptr, pm->ib_size_bytes, false);

out:
	mutex_unlock(&pm->lock);
	return 0;
}

int pm_debugfs_hang_hws(struct packet_manager *pm)
{
	uint32_t *buffer, size;
	int r = 0;

	size = pm->pmf->query_status_size;
	mutex_lock(&pm->lock);
	kq_acquire_packet_buffer(pm->priv_queue,
			size / sizeof(uint32_t), (unsigned int **)&buffer);
	if (!buffer) {
		pr_err("Failed to allocate buffer on kernel queue\n");
		r = -ENOMEM;
		goto out;
	}
	memset(buffer, 0x55, size);
	kq_submit_packet(pm->priv_queue);

	pr_info("Submitting %x %x %x %x %x %x %x to HIQ to hang the HWS.",
		buffer[0], buffer[1], buffer[2], buffer[3],
		buffer[4], buffer[5], buffer[6]);
out:
	mutex_unlock(&pm->lock);
	return r;
}


#endif
