// SPDX-License-Identifier: GPL-2.0 OR MIT
/*
 * Copyright 2014-2022 Advanced Micro Devices, Inc.
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
 */

#include <linux/mutex.h>
#include <linux/log2.h>
#include <linux/sched.h>
#include <linux/sched/mm.h>
#include <linux/sched/task.h>
#include <linux/mmu_context.h>
#include <linux/slab.h>
#include <linux/notifier.h>
#include <linux/compat.h>
#include <linux/mman.h>
#include <linux/file.h>
#include <linux/pm_runtime.h>
#include "amdgpu_amdkfd.h"
#include "amdgpu.h"

struct mm_struct;

#include "kfd_priv.h"
#include "kfd_device_queue_manager.h"
#include "kfd_svm.h"
#include "kfd_smi_events.h"
#include "kfd_debug.h"

/*
 * List of struct kfd_process (field kfd_process).
 * Unique/indexed by mm_struct*
 */
DEFINE_HASHTABLE(kfd_processes_table, KFD_PROCESS_TABLE_SIZE);
DEFINE_MUTEX(kfd_processes_mutex);

DEFINE_SRCU(kfd_processes_srcu);

/* For process termination handling */
static struct workqueue_struct *kfd_process_wq;

/* Ordered, single-threaded workqueue for restoring evicted
 * processes. Restoring multiple processes concurrently under memory
 * pressure can lead to processes blocking each other from validating
 * their BOs and result in a live-lock situation where processes
 * remain evicted indefinitely.
 */
static struct workqueue_struct *kfd_restore_wq;

static struct kfd_process *find_process(const struct task_struct *thread,
					bool ref);
static void kfd_process_ref_release(struct kref *ref);
static struct kfd_process *create_process(const struct task_struct *thread);

static void evict_process_worker(struct work_struct *work);
static void restore_process_worker(struct work_struct *work);

static void kfd_process_device_destroy_cwsr_dgpu(struct kfd_process_device *pdd);

struct kfd_procfs_tree {
	struct kobject *kobj;
};

static struct kfd_procfs_tree procfs;

/*
 * Structure for SDMA activity tracking
 */
struct kfd_sdma_activity_handler_workarea {
	struct work_struct sdma_activity_work;
	struct kfd_process_device *pdd;
	uint64_t sdma_activity_counter;
};

struct temp_sdma_queue_list {
	uint64_t __user *rptr;
	uint64_t sdma_val;
	unsigned int queue_id;
	struct list_head list;
};

static void kfd_sdma_activity_worker(struct work_struct *work)
{
	struct kfd_sdma_activity_handler_workarea *workarea;
	struct kfd_process_device *pdd;
	uint64_t val;
	struct mm_struct *mm;
	struct queue *q;
	struct qcm_process_device *qpd;
	struct device_queue_manager *dqm;
	int ret = 0;
	struct temp_sdma_queue_list sdma_q_list;
	struct temp_sdma_queue_list *sdma_q, *next;

	workarea = container_of(work, struct kfd_sdma_activity_handler_workarea,
				sdma_activity_work);

	pdd = workarea->pdd;
	if (!pdd)
		return;
	dqm = pdd->dev->dqm;
	qpd = &pdd->qpd;
	if (!dqm || !qpd)
		return;
	/*
	 * Total SDMA activity is current SDMA activity + past SDMA activity
	 * Past SDMA count is stored in pdd.
	 * To get the current activity counters for all active SDMA queues,
	 * we loop over all SDMA queues and get their counts from user-space.
	 *
	 * We cannot call get_user() with dqm_lock held as it can cause
	 * a circular lock dependency situation. To read the SDMA stats,
	 * we need to do the following:
	 *
	 * 1. Create a temporary list of SDMA queue nodes from the qpd->queues_list,
	 *    with dqm_lock/dqm_unlock().
	 * 2. Call get_user() for each node in temporary list without dqm_lock.
	 *    Save the SDMA count for each node and also add the count to the total
	 *    SDMA count counter.
	 *    Its possible, during this step, a few SDMA queue nodes got deleted
	 *    from the qpd->queues_list.
	 * 3. Do a second pass over qpd->queues_list to check if any nodes got deleted.
	 *    If any node got deleted, its SDMA count would be captured in the sdma
	 *    past activity counter. So subtract the SDMA counter stored in step 2
	 *    for this node from the total SDMA count.
	 */
	INIT_LIST_HEAD(&sdma_q_list.list);

	/*
	 * Create the temp list of all SDMA queues
	 */
	dqm_lock(dqm);

	list_for_each_entry(q, &qpd->queues_list, list) {
		if ((q->properties.type != KFD_QUEUE_TYPE_SDMA) &&
		    (q->properties.type != KFD_QUEUE_TYPE_SDMA_XGMI))
			continue;

		sdma_q = kzalloc(sizeof(struct temp_sdma_queue_list), GFP_KERNEL);
		if (!sdma_q) {
			dqm_unlock(dqm);
			goto cleanup;
		}

		INIT_LIST_HEAD(&sdma_q->list);
		sdma_q->rptr = (uint64_t __user *)q->properties.read_ptr;
		sdma_q->queue_id = q->properties.queue_id;
		list_add_tail(&sdma_q->list, &sdma_q_list.list);
	}

	/*
	 * If the temp list is empty, then no SDMA queues nodes were found in
	 * qpd->queues_list. Return the past activity count as the total sdma
	 * count
	 */
	if (list_empty(&sdma_q_list.list)) {
		workarea->sdma_activity_counter = pdd->sdma_past_activity_counter;
		dqm_unlock(dqm);
		return;
	}

	dqm_unlock(dqm);

	/*
	 * Get the usage count for each SDMA queue in temp_list.
	 */
	mm = get_task_mm(pdd->process->lead_thread);
	if (!mm)
		goto cleanup;

	kthread_use_mm(mm);

	list_for_each_entry(sdma_q, &sdma_q_list.list, list) {
		val = 0;
		ret = read_sdma_queue_counter(sdma_q->rptr, &val);
		if (ret) {
			pr_debug("Failed to read SDMA queue active counter for queue id: %d",
				 sdma_q->queue_id);
		} else {
			sdma_q->sdma_val = val;
			workarea->sdma_activity_counter += val;
		}
	}

	kthread_unuse_mm(mm);
	mmput(mm);

	/*
	 * Do a second iteration over qpd_queues_list to check if any SDMA
	 * nodes got deleted while fetching SDMA counter.
	 */
	dqm_lock(dqm);

	workarea->sdma_activity_counter += pdd->sdma_past_activity_counter;

	list_for_each_entry(q, &qpd->queues_list, list) {
		if (list_empty(&sdma_q_list.list))
			break;

		if ((q->properties.type != KFD_QUEUE_TYPE_SDMA) &&
		    (q->properties.type != KFD_QUEUE_TYPE_SDMA_XGMI))
			continue;

		list_for_each_entry_safe(sdma_q, next, &sdma_q_list.list, list) {
			if (((uint64_t __user *)q->properties.read_ptr == sdma_q->rptr) &&
			     (sdma_q->queue_id == q->properties.queue_id)) {
				list_del(&sdma_q->list);
				kfree(sdma_q);
				break;
			}
		}
	}

	dqm_unlock(dqm);

	/*
	 * If temp list is not empty, it implies some queues got deleted
	 * from qpd->queues_list during SDMA usage read. Subtract the SDMA
	 * count for each node from the total SDMA count.
	 */
	list_for_each_entry_safe(sdma_q, next, &sdma_q_list.list, list) {
		workarea->sdma_activity_counter -= sdma_q->sdma_val;
		list_del(&sdma_q->list);
		kfree(sdma_q);
	}

	return;

cleanup:
	list_for_each_entry_safe(sdma_q, next, &sdma_q_list.list, list) {
		list_del(&sdma_q->list);
		kfree(sdma_q);
	}
}

/**
 * kfd_get_cu_occupancy - Collect number of waves in-flight on this device
 * by current process. Translates acquired wave count into number of compute units
 * that are occupied.
 *
 * @attr: Handle of attribute that allows reporting of wave count. The attribute
 * handle encapsulates GPU device it is associated with, thereby allowing collection
 * of waves in flight, etc
 * @buffer: Handle of user provided buffer updated with wave count
 *
 * Return: Number of bytes written to user buffer or an error value
 */
static int kfd_get_cu_occupancy(struct attribute *attr, char *buffer)
{
	int cu_cnt;
	int wave_cnt;
	int max_waves_per_cu;
	struct kfd_node *dev = NULL;
	struct kfd_process *proc = NULL;
	struct kfd_process_device *pdd = NULL;
	int i;
	struct kfd_cu_occupancy *cu_occupancy;
	u32 queue_format;

	pdd = container_of(attr, struct kfd_process_device, attr_cu_occupancy);
	dev = pdd->dev;
	if (dev->kfd2kgd->get_cu_occupancy == NULL)
		return -EINVAL;

	cu_cnt = 0;
	proc = pdd->process;
	if (pdd->qpd.queue_count == 0) {
		pr_debug("Gpu-Id: %d has no active queues for process %d\n",
			 dev->id, proc->pasid);
		return snprintf(buffer, PAGE_SIZE, "%d\n", cu_cnt);
	}

	/* Collect wave count from device if it supports */
	wave_cnt = 0;
	max_waves_per_cu = 0;

	cu_occupancy = kcalloc(AMDGPU_MAX_QUEUES, sizeof(*cu_occupancy), GFP_KERNEL);
	if (!cu_occupancy)
		return -ENOMEM;

	/*
	 * For GFX 9.4.3, fetch the CU occupancy from the first XCC in the partition.
	 * For AQL queues, because of cooperative dispatch we multiply the wave count
	 * by number of XCCs in the partition to get the total wave counts across all
	 * XCCs in the partition.
	 * For PM4 queues, there is no cooperative dispatch so wave_cnt stay as it is.
	 */
	dev->kfd2kgd->get_cu_occupancy(dev->adev, cu_occupancy,
			&max_waves_per_cu, ffs(dev->xcc_mask) - 1);

	for (i = 0; i < AMDGPU_MAX_QUEUES; i++) {
		if (cu_occupancy[i].wave_cnt != 0 &&
		    kfd_dqm_is_queue_in_process(dev->dqm, &pdd->qpd,
						cu_occupancy[i].doorbell_off,
						&queue_format)) {
			if (unlikely(queue_format == KFD_QUEUE_FORMAT_PM4))
				wave_cnt += cu_occupancy[i].wave_cnt;
			else
				wave_cnt += (NUM_XCC(dev->xcc_mask) *
						cu_occupancy[i].wave_cnt);
		}
	}

	/* Translate wave count to number of compute units */
	cu_cnt = (wave_cnt + (max_waves_per_cu - 1)) / max_waves_per_cu;
	kfree(cu_occupancy);
	return snprintf(buffer, PAGE_SIZE, "%d\n", cu_cnt);
}

static ssize_t kfd_procfs_show(struct kobject *kobj, struct attribute *attr,
			       char *buffer)
{
	if (strcmp(attr->name, "pasid") == 0) {
		struct kfd_process *p = container_of(attr, struct kfd_process,
						     attr_pasid);

		return snprintf(buffer, PAGE_SIZE, "%d\n", p->pasid);
	} else if (strncmp(attr->name, "vram_", 5) == 0) {
		struct kfd_process_device *pdd = container_of(attr, struct kfd_process_device,
							      attr_vram);
		return snprintf(buffer, PAGE_SIZE, "%llu\n", atomic64_read(&pdd->vram_usage));
	} else if (strncmp(attr->name, "sdma_", 5) == 0) {
		struct kfd_process_device *pdd = container_of(attr, struct kfd_process_device,
							      attr_sdma);
		struct kfd_sdma_activity_handler_workarea sdma_activity_work_handler;

		INIT_WORK_ONSTACK(&sdma_activity_work_handler.sdma_activity_work,
				  kfd_sdma_activity_worker);

		sdma_activity_work_handler.pdd = pdd;
		sdma_activity_work_handler.sdma_activity_counter = 0;

		schedule_work(&sdma_activity_work_handler.sdma_activity_work);

		flush_work(&sdma_activity_work_handler.sdma_activity_work);
		destroy_work_on_stack(&sdma_activity_work_handler.sdma_activity_work);

		return snprintf(buffer, PAGE_SIZE, "%llu\n",
				(sdma_activity_work_handler.sdma_activity_counter)/
				 SDMA_ACTIVITY_DIVISOR);
	} else {
		pr_err("Invalid attribute");
		return -EINVAL;
	}

	return 0;
}

static void kfd_procfs_kobj_release(struct kobject *kobj)
{
	kfree(kobj);
}

static const struct sysfs_ops kfd_procfs_ops = {
	.show = kfd_procfs_show,
};

static const struct kobj_type procfs_type = {
	.release = kfd_procfs_kobj_release,
	.sysfs_ops = &kfd_procfs_ops,
};

void kfd_procfs_init(void)
{
	int ret = 0;

	procfs.kobj = kfd_alloc_struct(procfs.kobj);
	if (!procfs.kobj)
		return;

	ret = kobject_init_and_add(procfs.kobj, &procfs_type,
				   &kfd_device->kobj, "proc");
	if (ret) {
		pr_warn("Could not create procfs proc folder");
		/* If we fail to create the procfs, clean up */
		kfd_procfs_shutdown();
	}
}

void kfd_procfs_shutdown(void)
{
	if (procfs.kobj) {
		kobject_del(procfs.kobj);
		kobject_put(procfs.kobj);
		procfs.kobj = NULL;
	}
}

static ssize_t kfd_procfs_queue_show(struct kobject *kobj,
				     struct attribute *attr, char *buffer)
{
	struct queue *q = container_of(kobj, struct queue, kobj);

	if (!strcmp(attr->name, "size"))
		return snprintf(buffer, PAGE_SIZE, "%llu",
				q->properties.queue_size);
	else if (!strcmp(attr->name, "type"))
		return snprintf(buffer, PAGE_SIZE, "%d", q->properties.type);
	else if (!strcmp(attr->name, "gpuid"))
		return snprintf(buffer, PAGE_SIZE, "%u", q->device->id);
	else
		pr_err("Invalid attribute");

	return 0;
}

static ssize_t kfd_procfs_stats_show(struct kobject *kobj,
				     struct attribute *attr, char *buffer)
{
	if (strcmp(attr->name, "evicted_ms") == 0) {
		struct kfd_process_device *pdd = container_of(attr,
				struct kfd_process_device,
				attr_evict);
		uint64_t evict_jiffies;

		evict_jiffies = atomic64_read(&pdd->evict_duration_counter);

		return snprintf(buffer,
				PAGE_SIZE,
				"%llu\n",
				jiffies64_to_msecs(evict_jiffies));

	/* Sysfs handle that gets CU occupancy is per device */
	} else if (strcmp(attr->name, "cu_occupancy") == 0) {
		return kfd_get_cu_occupancy(attr, buffer);
	} else {
		pr_err("Invalid attribute");
	}

	return 0;
}

static ssize_t kfd_sysfs_counters_show(struct kobject *kobj,
				       struct attribute *attr, char *buf)
{
	struct kfd_process_device *pdd;

	if (!strcmp(attr->name, "faults")) {
		pdd = container_of(attr, struct kfd_process_device,
				   attr_faults);
		return sysfs_emit(buf, "%llu\n", READ_ONCE(pdd->faults));
	}
	if (!strcmp(attr->name, "page_in")) {
		pdd = container_of(attr, struct kfd_process_device,
				   attr_page_in);
		return sysfs_emit(buf, "%llu\n", READ_ONCE(pdd->page_in));
	}
	if (!strcmp(attr->name, "page_out")) {
		pdd = container_of(attr, struct kfd_process_device,
				   attr_page_out);
		return sysfs_emit(buf, "%llu\n", READ_ONCE(pdd->page_out));
	}
	return 0;
}

static struct attribute attr_queue_size = {
	.name = "size",
	.mode = KFD_SYSFS_FILE_MODE
};

static struct attribute attr_queue_type = {
	.name = "type",
	.mode = KFD_SYSFS_FILE_MODE
};

static struct attribute attr_queue_gpuid = {
	.name = "gpuid",
	.mode = KFD_SYSFS_FILE_MODE
};

static struct attribute *procfs_queue_attrs[] = {
	&attr_queue_size,
	&attr_queue_type,
	&attr_queue_gpuid,
	NULL
};
ATTRIBUTE_GROUPS(procfs_queue);

static const struct sysfs_ops procfs_queue_ops = {
	.show = kfd_procfs_queue_show,
};

static const struct kobj_type procfs_queue_type = {
	.sysfs_ops = &procfs_queue_ops,
	.default_groups = procfs_queue_groups,
};

static const struct sysfs_ops procfs_stats_ops = {
	.show = kfd_procfs_stats_show,
};

static const struct kobj_type procfs_stats_type = {
	.sysfs_ops = &procfs_stats_ops,
	.release = kfd_procfs_kobj_release,
};

static const struct sysfs_ops sysfs_counters_ops = {
	.show = kfd_sysfs_counters_show,
};

static const struct kobj_type sysfs_counters_type = {
	.sysfs_ops = &sysfs_counters_ops,
	.release = kfd_procfs_kobj_release,
};

int kfd_procfs_add_queue(struct queue *q)
{
	struct kfd_process *proc;
	int ret;

	if (!q || !q->process)
		return -EINVAL;
	proc = q->process;

	/* Create proc/<pid>/queues/<queue id> folder */
	if (!proc->kobj_queues)
		return -EFAULT;
	ret = kobject_init_and_add(&q->kobj, &procfs_queue_type,
			proc->kobj_queues, "%u", q->properties.queue_id);
	if (ret < 0) {
		pr_warn("Creating proc/<pid>/queues/%u failed",
			q->properties.queue_id);
		kobject_put(&q->kobj);
		return ret;
	}

	return 0;
}

static void kfd_sysfs_create_file(struct kobject *kobj, struct attribute *attr,
				 char *name)
{
	int ret;

	if (!kobj || !attr || !name)
		return;

	attr->name = name;
	attr->mode = KFD_SYSFS_FILE_MODE;
	sysfs_attr_init(attr);

	ret = sysfs_create_file(kobj, attr);
	if (ret)
		pr_warn("Create sysfs %s/%s failed %d", kobj->name, name, ret);
}

static void kfd_procfs_add_sysfs_stats(struct kfd_process *p)
{
	int ret;
	int i;
	char stats_dir_filename[MAX_SYSFS_FILENAME_LEN];

	if (!p || !p->kobj)
		return;

	/*
	 * Create sysfs files for each GPU:
	 * - proc/<pid>/stats_<gpuid>/
	 * - proc/<pid>/stats_<gpuid>/evicted_ms
	 * - proc/<pid>/stats_<gpuid>/cu_occupancy
	 */
	for (i = 0; i < p->n_pdds; i++) {
		struct kfd_process_device *pdd = p->pdds[i];

		snprintf(stats_dir_filename, MAX_SYSFS_FILENAME_LEN,
				"stats_%u", pdd->dev->id);
		pdd->kobj_stats = kfd_alloc_struct(pdd->kobj_stats);
		if (!pdd->kobj_stats)
			return;

		ret = kobject_init_and_add(pdd->kobj_stats,
					   &procfs_stats_type,
					   p->kobj,
					   stats_dir_filename);

		if (ret) {
			pr_warn("Creating KFD proc/stats_%s folder failed",
				stats_dir_filename);
			kobject_put(pdd->kobj_stats);
			pdd->kobj_stats = NULL;
			return;
		}

		kfd_sysfs_create_file(pdd->kobj_stats, &pdd->attr_evict,
				      "evicted_ms");
		/* Add sysfs file to report compute unit occupancy */
		if (pdd->dev->kfd2kgd->get_cu_occupancy)
			kfd_sysfs_create_file(pdd->kobj_stats,
					      &pdd->attr_cu_occupancy,
					      "cu_occupancy");
	}
}

static void kfd_procfs_add_sysfs_counters(struct kfd_process *p)
{
	int ret = 0;
	int i;
	char counters_dir_filename[MAX_SYSFS_FILENAME_LEN];

	if (!p || !p->kobj)
		return;

	/*
	 * Create sysfs files for each GPU which supports SVM
	 * - proc/<pid>/counters_<gpuid>/
	 * - proc/<pid>/counters_<gpuid>/faults
	 * - proc/<pid>/counters_<gpuid>/page_in
	 * - proc/<pid>/counters_<gpuid>/page_out
	 */
	for_each_set_bit(i, p->svms.bitmap_supported, p->n_pdds) {
		struct kfd_process_device *pdd = p->pdds[i];
		struct kobject *kobj_counters;

		snprintf(counters_dir_filename, MAX_SYSFS_FILENAME_LEN,
			"counters_%u", pdd->dev->id);
		kobj_counters = kfd_alloc_struct(kobj_counters);
		if (!kobj_counters)
			return;

		ret = kobject_init_and_add(kobj_counters, &sysfs_counters_type,
					   p->kobj, counters_dir_filename);
		if (ret) {
			pr_warn("Creating KFD proc/%s folder failed",
				counters_dir_filename);
			kobject_put(kobj_counters);
			return;
		}

		pdd->kobj_counters = kobj_counters;
		kfd_sysfs_create_file(kobj_counters, &pdd->attr_faults,
				      "faults");
		kfd_sysfs_create_file(kobj_counters, &pdd->attr_page_in,
				      "page_in");
		kfd_sysfs_create_file(kobj_counters, &pdd->attr_page_out,
				      "page_out");
	}
}

static void kfd_procfs_add_sysfs_files(struct kfd_process *p)
{
	int i;

	if (!p || !p->kobj)
		return;

	/*
	 * Create sysfs files for each GPU:
	 * - proc/<pid>/vram_<gpuid>
	 * - proc/<pid>/sdma_<gpuid>
	 */
	for (i = 0; i < p->n_pdds; i++) {
		struct kfd_process_device *pdd = p->pdds[i];

		snprintf(pdd->vram_filename, MAX_SYSFS_FILENAME_LEN, "vram_%u",
			 pdd->dev->id);
		kfd_sysfs_create_file(p->kobj, &pdd->attr_vram,
				      pdd->vram_filename);

		snprintf(pdd->sdma_filename, MAX_SYSFS_FILENAME_LEN, "sdma_%u",
			 pdd->dev->id);
		kfd_sysfs_create_file(p->kobj, &pdd->attr_sdma,
					    pdd->sdma_filename);
	}
}

void kfd_procfs_del_queue(struct queue *q)
{
	if (!q)
		return;

	kobject_del(&q->kobj);
	kobject_put(&q->kobj);
}

int kfd_process_create_wq(void)
{
	if (!kfd_process_wq)
		kfd_process_wq = alloc_workqueue("kfd_process_wq", 0, 0);
	if (!kfd_restore_wq)
		kfd_restore_wq = alloc_ordered_workqueue("kfd_restore_wq",
							 WQ_FREEZABLE);

	if (!kfd_process_wq || !kfd_restore_wq) {
		kfd_process_destroy_wq();
		return -ENOMEM;
	}

	return 0;
}

void kfd_process_destroy_wq(void)
{
	if (kfd_process_wq) {
		destroy_workqueue(kfd_process_wq);
		kfd_process_wq = NULL;
	}
	if (kfd_restore_wq) {
		destroy_workqueue(kfd_restore_wq);
		kfd_restore_wq = NULL;
	}
}

static void kfd_process_free_gpuvm(struct kgd_mem *mem,
			struct kfd_process_device *pdd, void **kptr)
{
	struct kfd_node *dev = pdd->dev;

	if (kptr && *kptr) {
		amdgpu_amdkfd_gpuvm_unmap_gtt_bo_from_kernel(mem);
		*kptr = NULL;
	}

	amdgpu_amdkfd_gpuvm_unmap_memory_from_gpu(dev->adev, mem, pdd->drm_priv);
	amdgpu_amdkfd_gpuvm_free_memory_of_gpu(dev->adev, mem, pdd->drm_priv,
					       NULL);
}

/* kfd_process_alloc_gpuvm - Allocate GPU VM for the KFD process
 *	This function should be only called right after the process
 *	is created and when kfd_processes_mutex is still being held
 *	to avoid concurrency. Because of that exclusiveness, we do
 *	not need to take p->mutex.
 */
static int kfd_process_alloc_gpuvm(struct kfd_process_device *pdd,
				   uint64_t gpu_va, uint32_t size,
				   uint32_t flags, struct kgd_mem **mem, void **kptr)
{
	struct kfd_node *kdev = pdd->dev;
	int err;

	err = amdgpu_amdkfd_gpuvm_alloc_memory_of_gpu(kdev->adev, gpu_va, size,
						 pdd->drm_priv, mem, NULL,
						 flags, false);
	if (err)
		goto err_alloc_mem;

	err = amdgpu_amdkfd_gpuvm_map_memory_to_gpu(kdev->adev, *mem,
			pdd->drm_priv);
	if (err)
		goto err_map_mem;

	err = amdgpu_amdkfd_gpuvm_sync_memory(kdev->adev, *mem, true);
	if (err) {
		pr_debug("Sync memory failed, wait interrupted by user signal\n");
		goto sync_memory_failed;
	}

	if (kptr) {
		err = amdgpu_amdkfd_gpuvm_map_gtt_bo_to_kernel(
				(struct kgd_mem *)*mem, kptr, NULL);
		if (err) {
			pr_debug("Map GTT BO to kernel failed\n");
			goto sync_memory_failed;
		}
	}

	return err;

sync_memory_failed:
	amdgpu_amdkfd_gpuvm_unmap_memory_from_gpu(kdev->adev, *mem, pdd->drm_priv);

err_map_mem:
	amdgpu_amdkfd_gpuvm_free_memory_of_gpu(kdev->adev, *mem, pdd->drm_priv,
					       NULL);
err_alloc_mem:
	*mem = NULL;
	*kptr = NULL;
	return err;
}

/* kfd_process_device_reserve_ib_mem - Reserve memory inside the
 *	process for IB usage The memory reserved is for KFD to submit
 *	IB to AMDGPU from kernel.  If the memory is reserved
 *	successfully, ib_kaddr will have the CPU/kernel
 *	address. Check ib_kaddr before accessing the memory.
 */
static int kfd_process_device_reserve_ib_mem(struct kfd_process_device *pdd)
{
	struct qcm_process_device *qpd = &pdd->qpd;
	uint32_t flags = KFD_IOC_ALLOC_MEM_FLAGS_GTT |
			KFD_IOC_ALLOC_MEM_FLAGS_NO_SUBSTITUTE |
			KFD_IOC_ALLOC_MEM_FLAGS_WRITABLE |
			KFD_IOC_ALLOC_MEM_FLAGS_EXECUTABLE;
	struct kgd_mem *mem;
	void *kaddr;
	int ret;

	if (qpd->ib_kaddr || !qpd->ib_base)
		return 0;

	/* ib_base is only set for dGPU */
	ret = kfd_process_alloc_gpuvm(pdd, qpd->ib_base, PAGE_SIZE, flags,
				      &mem, &kaddr);
	if (ret)
		return ret;

	qpd->ib_mem = mem;
	qpd->ib_kaddr = kaddr;

	return 0;
}

static void kfd_process_device_destroy_ib_mem(struct kfd_process_device *pdd)
{
	struct qcm_process_device *qpd = &pdd->qpd;

	if (!qpd->ib_kaddr || !qpd->ib_base)
		return;

	kfd_process_free_gpuvm(qpd->ib_mem, pdd, &qpd->ib_kaddr);
}

struct kfd_process *kfd_create_process(struct task_struct *thread)
{
	struct kfd_process *process;
	int ret;

	if (!(thread->mm && mmget_not_zero(thread->mm)))
		return ERR_PTR(-EINVAL);

	/* Only the pthreads threading model is supported. */
	if (thread->group_leader->mm != thread->mm) {
		mmput(thread->mm);
		return ERR_PTR(-EINVAL);
	}

	/*
	 * take kfd processes mutex before starting of process creation
	 * so there won't be a case where two threads of the same process
	 * create two kfd_process structures
	 */
	mutex_lock(&kfd_processes_mutex);

	if (kfd_is_locked()) {
		pr_debug("KFD is locked! Cannot create process");
		process = ERR_PTR(-EINVAL);
		goto out;
	}

	/* A prior open of /dev/kfd could have already created the process.
	 * find_process will increase process kref in this case
	 */
	process = find_process(thread, true);
	if (process) {
		pr_debug("Process already found\n");
	} else {
		/* If the process just called exec(3), it is possible that the
		 * cleanup of the kfd_process (following the release of the mm
		 * of the old process image) is still in the cleanup work queue.
		 * Make sure to drain any job before trying to recreate any
		 * resource for this process.
		 */
		flush_workqueue(kfd_process_wq);

		process = create_process(thread);
		if (IS_ERR(process))
			goto out;

		if (!procfs.kobj)
			goto out;

		process->kobj = kfd_alloc_struct(process->kobj);
		if (!process->kobj) {
			pr_warn("Creating procfs kobject failed");
			goto out;
		}
		ret = kobject_init_and_add(process->kobj, &procfs_type,
					   procfs.kobj, "%d",
					   (int)process->lead_thread->pid);
		if (ret) {
			pr_warn("Creating procfs pid directory failed");
			kobject_put(process->kobj);
			goto out;
		}

		kfd_sysfs_create_file(process->kobj, &process->attr_pasid,
				      "pasid");

		process->kobj_queues = kobject_create_and_add("queues",
							process->kobj);
		if (!process->kobj_queues)
			pr_warn("Creating KFD proc/queues folder failed");

		kfd_procfs_add_sysfs_stats(process);
		kfd_procfs_add_sysfs_files(process);
		kfd_procfs_add_sysfs_counters(process);

		init_waitqueue_head(&process->wait_irq_drain);
	}
out:
	mutex_unlock(&kfd_processes_mutex);
	mmput(thread->mm);

	return process;
}

struct kfd_process *kfd_get_process(const struct task_struct *thread)
{
	struct kfd_process *process;

	if (!thread->mm)
		return ERR_PTR(-EINVAL);

	/* Only the pthreads threading model is supported. */
	if (thread->group_leader->mm != thread->mm)
		return ERR_PTR(-EINVAL);

	process = find_process(thread, false);
	if (!process)
		return ERR_PTR(-EINVAL);

	return process;
}

static struct kfd_process *find_process_by_mm(const struct mm_struct *mm)
{
	struct kfd_process *process;

	hash_for_each_possible_rcu(kfd_processes_table, process,
					kfd_processes, (uintptr_t)mm)
		if (process->mm == mm)
			return process;

	return NULL;
}

static struct kfd_process *find_process(const struct task_struct *thread,
					bool ref)
{
	struct kfd_process *p;
	int idx;

	idx = srcu_read_lock(&kfd_processes_srcu);
	p = find_process_by_mm(thread->mm);
	if (p && ref)
		kref_get(&p->ref);
	srcu_read_unlock(&kfd_processes_srcu, idx);

	return p;
}

void kfd_unref_process(struct kfd_process *p)
{
	kref_put(&p->ref, kfd_process_ref_release);
}

/* This increments the process->ref counter. */
struct kfd_process *kfd_lookup_process_by_pid(struct pid *pid)
{
	struct task_struct *task = NULL;
	struct kfd_process *p    = NULL;

	if (!pid) {
		task = current;
		get_task_struct(task);
	} else {
		task = get_pid_task(pid, PIDTYPE_PID);
	}

	if (task) {
		p = find_process(task, true);
		put_task_struct(task);
	}

	return p;
}

static void kfd_process_device_free_bos(struct kfd_process_device *pdd)
{
	struct kfd_process *p = pdd->process;
	void *mem;
	int id;
	int i;

	/*
	 * Remove all handles from idr and release appropriate
	 * local memory object
	 */
	idr_for_each_entry(&pdd->alloc_idr, mem, id) {

		for (i = 0; i < p->n_pdds; i++) {
			struct kfd_process_device *peer_pdd = p->pdds[i];

			if (!peer_pdd->drm_priv)
				continue;
			amdgpu_amdkfd_gpuvm_unmap_memory_from_gpu(
				peer_pdd->dev->adev, mem, peer_pdd->drm_priv);
		}

		amdgpu_amdkfd_gpuvm_free_memory_of_gpu(pdd->dev->adev, mem,
						       pdd->drm_priv, NULL);
		kfd_process_device_remove_obj_handle(pdd, id);
	}
}

/*
 * Just kunmap and unpin signal BO here. It will be freed in
 * kfd_process_free_outstanding_kfd_bos()
 */
static void kfd_process_kunmap_signal_bo(struct kfd_process *p)
{
	struct kfd_process_device *pdd;
	struct kfd_node *kdev;
	void *mem;

	kdev = kfd_device_by_id(GET_GPU_ID(p->signal_handle));
	if (!kdev)
		return;

	mutex_lock(&p->mutex);

	pdd = kfd_get_process_device_data(kdev, p);
	if (!pdd)
		goto out;

	mem = kfd_process_device_translate_handle(
		pdd, GET_IDR_HANDLE(p->signal_handle));
	if (!mem)
		goto out;

	amdgpu_amdkfd_gpuvm_unmap_gtt_bo_from_kernel(mem);

out:
	mutex_unlock(&p->mutex);
}

static void kfd_process_free_outstanding_kfd_bos(struct kfd_process *p)
{
	int i;

	for (i = 0; i < p->n_pdds; i++)
		kfd_process_device_free_bos(p->pdds[i]);
}

static void kfd_process_destroy_pdds(struct kfd_process *p)
{
	int i;

	for (i = 0; i < p->n_pdds; i++) {
		struct kfd_process_device *pdd = p->pdds[i];

		pr_debug("Releasing pdd (topology id %d) for process (pasid 0x%x)\n",
				pdd->dev->id, p->pasid);

		kfd_process_device_destroy_cwsr_dgpu(pdd);
		kfd_process_device_destroy_ib_mem(pdd);

		if (pdd->drm_file) {
			amdgpu_amdkfd_gpuvm_release_process_vm(
					pdd->dev->adev, pdd->drm_priv);
			fput(pdd->drm_file);
		}

		if (pdd->qpd.cwsr_kaddr && !pdd->qpd.cwsr_base)
			free_pages((unsigned long)pdd->qpd.cwsr_kaddr,
				get_order(KFD_CWSR_TBA_TMA_SIZE));

		idr_destroy(&pdd->alloc_idr);

		kfd_free_process_doorbells(pdd->dev->kfd, pdd);

		if (pdd->dev->kfd->shared_resources.enable_mes &&
			pdd->proc_ctx_cpu_ptr)
			amdgpu_amdkfd_free_gtt_mem(pdd->dev->adev,
						   &pdd->proc_ctx_bo);
		/*
		 * before destroying pdd, make sure to report availability
		 * for auto suspend
		 */
		if (pdd->runtime_inuse) {
			pm_runtime_mark_last_busy(adev_to_drm(pdd->dev->adev)->dev);
			pm_runtime_put_autosuspend(adev_to_drm(pdd->dev->adev)->dev);
			pdd->runtime_inuse = false;
		}

		kfree(pdd);
		p->pdds[i] = NULL;
	}
	p->n_pdds = 0;
}

static void kfd_process_remove_sysfs(struct kfd_process *p)
{
	struct kfd_process_device *pdd;
	int i;

	if (!p->kobj)
		return;

	sysfs_remove_file(p->kobj, &p->attr_pasid);
	kobject_del(p->kobj_queues);
	kobject_put(p->kobj_queues);
	p->kobj_queues = NULL;

	for (i = 0; i < p->n_pdds; i++) {
		pdd = p->pdds[i];

		sysfs_remove_file(p->kobj, &pdd->attr_vram);
		sysfs_remove_file(p->kobj, &pdd->attr_sdma);

		sysfs_remove_file(pdd->kobj_stats, &pdd->attr_evict);
		if (pdd->dev->kfd2kgd->get_cu_occupancy)
			sysfs_remove_file(pdd->kobj_stats,
					  &pdd->attr_cu_occupancy);
		kobject_del(pdd->kobj_stats);
		kobject_put(pdd->kobj_stats);
		pdd->kobj_stats = NULL;
	}

	for_each_set_bit(i, p->svms.bitmap_supported, p->n_pdds) {
		pdd = p->pdds[i];

		sysfs_remove_file(pdd->kobj_counters, &pdd->attr_faults);
		sysfs_remove_file(pdd->kobj_counters, &pdd->attr_page_in);
		sysfs_remove_file(pdd->kobj_counters, &pdd->attr_page_out);
		kobject_del(pdd->kobj_counters);
		kobject_put(pdd->kobj_counters);
		pdd->kobj_counters = NULL;
	}

	kobject_del(p->kobj);
	kobject_put(p->kobj);
	p->kobj = NULL;
}

/* No process locking is needed in this function, because the process
 * is not findable any more. We must assume that no other thread is
 * using it any more, otherwise we couldn't safely free the process
 * structure in the end.
 */
static void kfd_process_wq_release(struct work_struct *work)
{
	struct kfd_process *p = container_of(work, struct kfd_process,
					     release_work);
	struct dma_fence *ef;

	kfd_process_dequeue_from_all_devices(p);
	pqm_uninit(&p->pqm);

	/* Signal the eviction fence after user mode queues are
	 * destroyed. This allows any BOs to be freed without
	 * triggering pointless evictions or waiting for fences.
	 */
	synchronize_rcu();
	ef = rcu_access_pointer(p->ef);
	dma_fence_signal(ef);

	kfd_process_remove_sysfs(p);

	kfd_process_kunmap_signal_bo(p);
	kfd_process_free_outstanding_kfd_bos(p);
	svm_range_list_fini(p);

	kfd_process_destroy_pdds(p);
	dma_fence_put(ef);

	kfd_event_free_process(p);

	kfd_pasid_free(p->pasid);
	mutex_destroy(&p->mutex);

	put_task_struct(p->lead_thread);

	kfree(p);
}

static void kfd_process_ref_release(struct kref *ref)
{
	struct kfd_process *p = container_of(ref, struct kfd_process, ref);

	INIT_WORK(&p->release_work, kfd_process_wq_release);
	queue_work(kfd_process_wq, &p->release_work);
}

static struct mmu_notifier *kfd_process_alloc_notifier(struct mm_struct *mm)
{
	/* This increments p->ref counter if kfd process p exists */
	struct kfd_process *p = kfd_lookup_process_by_mm(mm);

	return p ? &p->mmu_notifier : ERR_PTR(-ESRCH);
}

static void kfd_process_free_notifier(struct mmu_notifier *mn)
{
	kfd_unref_process(container_of(mn, struct kfd_process, mmu_notifier));
}

static void kfd_process_notifier_release_internal(struct kfd_process *p)
{
	int i;

	cancel_delayed_work_sync(&p->eviction_work);
	cancel_delayed_work_sync(&p->restore_work);

	for (i = 0; i < p->n_pdds; i++) {
		struct kfd_process_device *pdd = p->pdds[i];

		/* re-enable GFX OFF since runtime enable with ttmp setup disabled it. */
		if (!kfd_dbg_is_rlc_restore_supported(pdd->dev) && p->runtime_info.ttmp_setup)
			amdgpu_gfx_off_ctrl(pdd->dev->adev, true);
	}

	/* Indicate to other users that MM is no longer valid */
	p->mm = NULL;
	kfd_dbg_trap_disable(p);

	if (atomic_read(&p->debugged_process_count) > 0) {
		struct kfd_process *target;
		unsigned int temp;
		int idx = srcu_read_lock(&kfd_processes_srcu);

		hash_for_each_rcu(kfd_processes_table, temp, target, kfd_processes) {
			if (target->debugger_process && target->debugger_process == p) {
				mutex_lock_nested(&target->mutex, 1);
				kfd_dbg_trap_disable(target);
				mutex_unlock(&target->mutex);
				if (atomic_read(&p->debugged_process_count) == 0)
					break;
			}
		}

		srcu_read_unlock(&kfd_processes_srcu, idx);
	}

	mmu_notifier_put(&p->mmu_notifier);
}

static void kfd_process_notifier_release(struct mmu_notifier *mn,
					struct mm_struct *mm)
{
	struct kfd_process *p;

	/*
	 * The kfd_process structure can not be free because the
	 * mmu_notifier srcu is read locked
	 */
	p = container_of(mn, struct kfd_process, mmu_notifier);
	if (WARN_ON(p->mm != mm))
		return;

	mutex_lock(&kfd_processes_mutex);
	/*
	 * Do early return if table is empty.
	 *
	 * This could potentially happen if this function is called concurrently
	 * by mmu_notifier and by kfd_cleanup_pocesses.
	 *
	 */
	if (hash_empty(kfd_processes_table)) {
		mutex_unlock(&kfd_processes_mutex);
		return;
	}
	hash_del_rcu(&p->kfd_processes);
	mutex_unlock(&kfd_processes_mutex);
	synchronize_srcu(&kfd_processes_srcu);

	kfd_process_notifier_release_internal(p);
}

static const struct mmu_notifier_ops kfd_process_mmu_notifier_ops = {
	.release = kfd_process_notifier_release,
	.alloc_notifier = kfd_process_alloc_notifier,
	.free_notifier = kfd_process_free_notifier,
};

/*
 * This code handles the case when driver is being unloaded before all
 * mm_struct are released.  We need to safely free the kfd_process and
 * avoid race conditions with mmu_notifier that might try to free them.
 *
 */
void kfd_cleanup_processes(void)
{
	struct kfd_process *p;
	struct hlist_node *p_temp;
	unsigned int temp;
	HLIST_HEAD(cleanup_list);

	/*
	 * Move all remaining kfd_process from the process table to a
	 * temp list for processing.   Once done, callback from mmu_notifier
	 * release will not see the kfd_process in the table and do early return,
	 * avoiding double free issues.
	 */
	mutex_lock(&kfd_processes_mutex);
	hash_for_each_safe(kfd_processes_table, temp, p_temp, p, kfd_processes) {
		hash_del_rcu(&p->kfd_processes);
		synchronize_srcu(&kfd_processes_srcu);
		hlist_add_head(&p->kfd_processes, &cleanup_list);
	}
	mutex_unlock(&kfd_processes_mutex);

	hlist_for_each_entry_safe(p, p_temp, &cleanup_list, kfd_processes)
		kfd_process_notifier_release_internal(p);

	/*
	 * Ensures that all outstanding free_notifier get called, triggering
	 * the release of the kfd_process struct.
	 */
	mmu_notifier_synchronize();
}

int kfd_process_init_cwsr_apu(struct kfd_process *p, struct file *filep)
{
	unsigned long  offset;
	int i;

	if (p->has_cwsr)
		return 0;

	for (i = 0; i < p->n_pdds; i++) {
		struct kfd_node *dev = p->pdds[i]->dev;
		struct qcm_process_device *qpd = &p->pdds[i]->qpd;

		if (!dev->kfd->cwsr_enabled || qpd->cwsr_kaddr || qpd->cwsr_base)
			continue;

		offset = KFD_MMAP_TYPE_RESERVED_MEM | KFD_MMAP_GPU_ID(dev->id);
		qpd->tba_addr = (int64_t)vm_mmap(filep, 0,
			KFD_CWSR_TBA_TMA_SIZE, PROT_READ | PROT_EXEC,
			MAP_SHARED, offset);

		if (IS_ERR_VALUE(qpd->tba_addr)) {
			int err = qpd->tba_addr;

			dev_err(dev->adev->dev,
				"Failure to set tba address. error %d.\n", err);
			qpd->tba_addr = 0;
			qpd->cwsr_kaddr = NULL;
			return err;
		}

		memcpy(qpd->cwsr_kaddr, dev->kfd->cwsr_isa, dev->kfd->cwsr_isa_size);

		kfd_process_set_trap_debug_flag(qpd, p->debug_trap_enabled);

		qpd->tma_addr = qpd->tba_addr + KFD_CWSR_TMA_OFFSET;
		pr_debug("set tba :0x%llx, tma:0x%llx, cwsr_kaddr:%p for pqm.\n",
			qpd->tba_addr, qpd->tma_addr, qpd->cwsr_kaddr);
	}

	p->has_cwsr = true;

	return 0;
}

static int kfd_process_device_init_cwsr_dgpu(struct kfd_process_device *pdd)
{
	struct kfd_node *dev = pdd->dev;
	struct qcm_process_device *qpd = &pdd->qpd;
	uint32_t flags = KFD_IOC_ALLOC_MEM_FLAGS_GTT
			| KFD_IOC_ALLOC_MEM_FLAGS_NO_SUBSTITUTE
			| KFD_IOC_ALLOC_MEM_FLAGS_EXECUTABLE;
	struct kgd_mem *mem;
	void *kaddr;
	int ret;

	if (!dev->kfd->cwsr_enabled || qpd->cwsr_kaddr || !qpd->cwsr_base)
		return 0;

	/* cwsr_base is only set for dGPU */
	ret = kfd_process_alloc_gpuvm(pdd, qpd->cwsr_base,
				      KFD_CWSR_TBA_TMA_SIZE, flags, &mem, &kaddr);
	if (ret)
		return ret;

	qpd->cwsr_mem = mem;
	qpd->cwsr_kaddr = kaddr;
	qpd->tba_addr = qpd->cwsr_base;

	memcpy(qpd->cwsr_kaddr, dev->kfd->cwsr_isa, dev->kfd->cwsr_isa_size);

	kfd_process_set_trap_debug_flag(&pdd->qpd,
					pdd->process->debug_trap_enabled);

	qpd->tma_addr = qpd->tba_addr + KFD_CWSR_TMA_OFFSET;
	pr_debug("set tba :0x%llx, tma:0x%llx, cwsr_kaddr:%p for pqm.\n",
		 qpd->tba_addr, qpd->tma_addr, qpd->cwsr_kaddr);

	return 0;
}

static void kfd_process_device_destroy_cwsr_dgpu(struct kfd_process_device *pdd)
{
	struct kfd_node *dev = pdd->dev;
	struct qcm_process_device *qpd = &pdd->qpd;

	if (!dev->kfd->cwsr_enabled || !qpd->cwsr_kaddr || !qpd->cwsr_base)
		return;

	kfd_process_free_gpuvm(qpd->cwsr_mem, pdd, &qpd->cwsr_kaddr);
}

void kfd_process_set_trap_handler(struct qcm_process_device *qpd,
				  uint64_t tba_addr,
				  uint64_t tma_addr)
{
	if (qpd->cwsr_kaddr) {
		/* KFD trap handler is bound, record as second-level TBA/TMA
		 * in first-level TMA. First-level trap will jump to second.
		 */
		uint64_t *tma =
			(uint64_t *)(qpd->cwsr_kaddr + KFD_CWSR_TMA_OFFSET);
		tma[0] = tba_addr;
		tma[1] = tma_addr;
	} else {
		/* No trap handler bound, bind as first-level TBA/TMA. */
		qpd->tba_addr = tba_addr;
		qpd->tma_addr = tma_addr;
	}
}

bool kfd_process_xnack_mode(struct kfd_process *p, bool supported)
{
	int i;

	/* On most GFXv9 GPUs, the retry mode in the SQ must match the
	 * boot time retry setting. Mixing processes with different
	 * XNACK/retry settings can hang the GPU.
	 *
	 * Different GPUs can have different noretry settings depending
	 * on HW bugs or limitations. We need to find at least one
	 * XNACK mode for this process that's compatible with all GPUs.
	 * Fortunately GPUs with retry enabled (noretry=0) can run code
	 * built for XNACK-off. On GFXv9 it may perform slower.
	 *
	 * Therefore applications built for XNACK-off can always be
	 * supported and will be our fallback if any GPU does not
	 * support retry.
	 */
	for (i = 0; i < p->n_pdds; i++) {
		struct kfd_node *dev = p->pdds[i]->dev;

		/* Only consider GFXv9 and higher GPUs. Older GPUs don't
		 * support the SVM APIs and don't need to be considered
		 * for the XNACK mode selection.
		 */
		if (!KFD_IS_SOC15(dev))
			continue;
		/* Aldebaran can always support XNACK because it can support
		 * per-process XNACK mode selection. But let the dev->noretry
		 * setting still influence the default XNACK mode.
		 */
		if (supported && KFD_SUPPORT_XNACK_PER_PROCESS(dev)) {
			if (!amdgpu_sriov_xnack_support(dev->kfd->adev)) {
				pr_debug("SRIOV platform xnack not supported\n");
				return false;
			}
			continue;
		}

		/* GFXv10 and later GPUs do not support shader preemption
		 * during page faults. This can lead to poor QoS for queue
		 * management and memory-manager-related preemptions or
		 * even deadlocks.
		 */
		if (KFD_GC_VERSION(dev) >= IP_VERSION(10, 1, 1))
			return false;

		if (dev->kfd->noretry)
			return false;
	}

	return true;
}

void kfd_process_set_trap_debug_flag(struct qcm_process_device *qpd,
				     bool enabled)
{
	if (qpd->cwsr_kaddr) {
		uint64_t *tma =
			(uint64_t *)(qpd->cwsr_kaddr + KFD_CWSR_TMA_OFFSET);
		tma[2] = enabled;
	}
}

/*
 * On return the kfd_process is fully operational and will be freed when the
 * mm is released
 */
static struct kfd_process *create_process(const struct task_struct *thread)
{
	struct kfd_process *process;
	struct mmu_notifier *mn;
	int err = -ENOMEM;

	process = kzalloc(sizeof(*process), GFP_KERNEL);
	if (!process)
		goto err_alloc_process;

	kref_init(&process->ref);
	mutex_init(&process->mutex);
	process->mm = thread->mm;
	process->lead_thread = thread->group_leader;
	process->n_pdds = 0;
	process->queues_paused = false;
	INIT_DELAYED_WORK(&process->eviction_work, evict_process_worker);
	INIT_DELAYED_WORK(&process->restore_work, restore_process_worker);
	process->last_restore_timestamp = get_jiffies_64();
	err = kfd_event_init_process(process);
	if (err)
		goto err_event_init;
	process->is_32bit_user_mode = in_compat_syscall();
	process->debug_trap_enabled = false;
	process->debugger_process = NULL;
	process->exception_enable_mask = 0;
	atomic_set(&process->debugged_process_count, 0);
	sema_init(&process->runtime_enable_sema, 0);

	process->pasid = kfd_pasid_alloc();
	if (process->pasid == 0) {
		err = -ENOSPC;
		goto err_alloc_pasid;
	}

	err = pqm_init(&process->pqm, process);
	if (err != 0)
		goto err_process_pqm_init;

	/* init process apertures*/
	err = kfd_init_apertures(process);
	if (err != 0)
		goto err_init_apertures;

	/* Check XNACK support after PDDs are created in kfd_init_apertures */
	process->xnack_enabled = kfd_process_xnack_mode(process, false);

	err = svm_range_list_init(process);
	if (err)
		goto err_init_svm_range_list;

	/* alloc_notifier needs to find the process in the hash table */
	hash_add_rcu(kfd_processes_table, &process->kfd_processes,
			(uintptr_t)process->mm);

	/* Avoid free_notifier to start kfd_process_wq_release if
	 * mmu_notifier_get failed because of pending signal.
	 */
	kref_get(&process->ref);

	/* MMU notifier registration must be the last call that can fail
	 * because after this point we cannot unwind the process creation.
	 * After this point, mmu_notifier_put will trigger the cleanup by
	 * dropping the last process reference in the free_notifier.
	 */
	mn = mmu_notifier_get(&kfd_process_mmu_notifier_ops, process->mm);
	if (IS_ERR(mn)) {
		err = PTR_ERR(mn);
		goto err_register_notifier;
	}
	BUG_ON(mn != &process->mmu_notifier);

	kfd_unref_process(process);
	get_task_struct(process->lead_thread);

	INIT_WORK(&process->debug_event_workarea, debug_event_write_work_handler);

	return process;

err_register_notifier:
	hash_del_rcu(&process->kfd_processes);
	svm_range_list_fini(process);
err_init_svm_range_list:
	kfd_process_free_outstanding_kfd_bos(process);
	kfd_process_destroy_pdds(process);
err_init_apertures:
	pqm_uninit(&process->pqm);
err_process_pqm_init:
	kfd_pasid_free(process->pasid);
err_alloc_pasid:
	kfd_event_free_process(process);
err_event_init:
	mutex_destroy(&process->mutex);
	kfree(process);
err_alloc_process:
	return ERR_PTR(err);
}

struct kfd_process_device *kfd_get_process_device_data(struct kfd_node *dev,
							struct kfd_process *p)
{
	int i;

	for (i = 0; i < p->n_pdds; i++)
		if (p->pdds[i]->dev == dev)
			return p->pdds[i];

	return NULL;
}

struct kfd_process_device *kfd_create_process_device_data(struct kfd_node *dev,
							struct kfd_process *p)
{
	struct kfd_process_device *pdd = NULL;

	if (WARN_ON_ONCE(p->n_pdds >= MAX_GPU_INSTANCE))
		return NULL;
	pdd = kzalloc(sizeof(*pdd), GFP_KERNEL);
	if (!pdd)
		return NULL;

	pdd->dev = dev;
	INIT_LIST_HEAD(&pdd->qpd.queues_list);
	INIT_LIST_HEAD(&pdd->qpd.priv_queue_list);
	pdd->qpd.dqm = dev->dqm;
	pdd->qpd.pqm = &p->pqm;
	pdd->qpd.evicted = 0;
	pdd->qpd.mapped_gws_queue = false;
	pdd->process = p;
	pdd->bound = PDD_UNBOUND;
	pdd->already_dequeued = false;
	pdd->runtime_inuse = false;
	atomic64_set(&pdd->vram_usage, 0);
	pdd->sdma_past_activity_counter = 0;
	pdd->user_gpu_id = dev->id;
	atomic64_set(&pdd->evict_duration_counter, 0);

	p->pdds[p->n_pdds++] = pdd;
	if (kfd_dbg_is_per_vmid_supported(pdd->dev))
		pdd->spi_dbg_override = pdd->dev->kfd2kgd->disable_debug_trap(
							pdd->dev->adev,
							false,
							0);

	/* Init idr used for memory handle translation */
	idr_init(&pdd->alloc_idr);

	return pdd;
}

/**
 * kfd_process_device_init_vm - Initialize a VM for a process-device
 *
 * @pdd: The process-device
 * @drm_file: Optional pointer to a DRM file descriptor
 *
 * If @drm_file is specified, it will be used to acquire the VM from
 * that file descriptor. If successful, the @pdd takes ownership of
 * the file descriptor.
 *
 * If @drm_file is NULL, a new VM is created.
 *
 * Returns 0 on success, -errno on failure.
 */
int kfd_process_device_init_vm(struct kfd_process_device *pdd,
			       struct file *drm_file)
{
	struct amdgpu_fpriv *drv_priv;
	struct amdgpu_vm *avm;
	struct kfd_process *p;
	struct dma_fence *ef;
	struct kfd_node *dev;
	int ret;

	if (!drm_file)
		return -EINVAL;

	if (pdd->drm_priv)
		return -EBUSY;

	ret = amdgpu_file_to_fpriv(drm_file, &drv_priv);
	if (ret)
		return ret;
	avm = &drv_priv->vm;

	p = pdd->process;
	dev = pdd->dev;

	ret = amdgpu_amdkfd_gpuvm_acquire_process_vm(dev->adev, avm,
						     &p->kgd_process_info,
						     p->ef ? NULL : &ef);
	if (ret) {
		dev_err(dev->adev->dev, "Failed to create process VM object\n");
		return ret;
	}

	if (!p->ef)
		RCU_INIT_POINTER(p->ef, ef);

	pdd->drm_priv = drm_file->private_data;

	ret = kfd_process_device_reserve_ib_mem(pdd);
	if (ret)
		goto err_reserve_ib_mem;
	ret = kfd_process_device_init_cwsr_dgpu(pdd);
	if (ret)
		goto err_init_cwsr;

	ret = amdgpu_amdkfd_gpuvm_set_vm_pasid(dev->adev, avm, p->pasid);
	if (ret)
		goto err_set_pasid;

	pdd->drm_file = drm_file;

	return 0;

err_set_pasid:
	kfd_process_device_destroy_cwsr_dgpu(pdd);
err_init_cwsr:
	kfd_process_device_destroy_ib_mem(pdd);
err_reserve_ib_mem:
	pdd->drm_priv = NULL;
	amdgpu_amdkfd_gpuvm_destroy_cb(dev->adev, avm);

	return ret;
}

/*
 * Direct the IOMMU to bind the process (specifically the pasid->mm)
 * to the device.
 * Unbinding occurs when the process dies or the device is removed.
 *
 * Assumes that the process lock is held.
 */
struct kfd_process_device *kfd_bind_process_to_device(struct kfd_node *dev,
							struct kfd_process *p)
{
	struct kfd_process_device *pdd;
	int err;

	pdd = kfd_get_process_device_data(dev, p);
	if (!pdd) {
		dev_err(dev->adev->dev, "Process device data doesn't exist\n");
		return ERR_PTR(-ENOMEM);
	}

	if (!pdd->drm_priv)
		return ERR_PTR(-ENODEV);

	/*
	 * signal runtime-pm system to auto resume and prevent
	 * further runtime suspend once device pdd is created until
	 * pdd is destroyed.
	 */
	if (!pdd->runtime_inuse) {
		err = pm_runtime_get_sync(adev_to_drm(dev->adev)->dev);
		if (err < 0) {
			pm_runtime_put_autosuspend(adev_to_drm(dev->adev)->dev);
			return ERR_PTR(err);
		}
	}

	/*
	 * make sure that runtime_usage counter is incremented just once
	 * per pdd
	 */
	pdd->runtime_inuse = true;

	return pdd;
}

/* Create specific handle mapped to mem from process local memory idr
 * Assumes that the process lock is held.
 */
int kfd_process_device_create_obj_handle(struct kfd_process_device *pdd,
					void *mem)
{
	return idr_alloc(&pdd->alloc_idr, mem, 0, 0, GFP_KERNEL);
}

/* Translate specific handle from process local memory idr
 * Assumes that the process lock is held.
 */
void *kfd_process_device_translate_handle(struct kfd_process_device *pdd,
					int handle)
{
	if (handle < 0)
		return NULL;

	return idr_find(&pdd->alloc_idr, handle);
}

/* Remove specific handle from process local memory idr
 * Assumes that the process lock is held.
 */
void kfd_process_device_remove_obj_handle(struct kfd_process_device *pdd,
					int handle)
{
	if (handle >= 0)
		idr_remove(&pdd->alloc_idr, handle);
}

/* This increments the process->ref counter. */
struct kfd_process *kfd_lookup_process_by_pasid(u32 pasid)
{
	struct kfd_process *p, *ret_p = NULL;
	unsigned int temp;

	int idx = srcu_read_lock(&kfd_processes_srcu);

	hash_for_each_rcu(kfd_processes_table, temp, p, kfd_processes) {
		if (p->pasid == pasid) {
			kref_get(&p->ref);
			ret_p = p;
			break;
		}
	}

	srcu_read_unlock(&kfd_processes_srcu, idx);

	return ret_p;
}

/* This increments the process->ref counter. */
struct kfd_process *kfd_lookup_process_by_mm(const struct mm_struct *mm)
{
	struct kfd_process *p;

	int idx = srcu_read_lock(&kfd_processes_srcu);

	p = find_process_by_mm(mm);
	if (p)
		kref_get(&p->ref);

	srcu_read_unlock(&kfd_processes_srcu, idx);

	return p;
}

/* kfd_process_evict_queues - Evict all user queues of a process
 *
 * Eviction is reference-counted per process-device. This means multiple
 * evictions from different sources can be nested safely.
 */
int kfd_process_evict_queues(struct kfd_process *p, uint32_t trigger)
{
	int r = 0;
	int i;
	unsigned int n_evicted = 0;

	for (i = 0; i < p->n_pdds; i++) {
		struct kfd_process_device *pdd = p->pdds[i];
		struct device *dev = pdd->dev->adev->dev;

		kfd_smi_event_queue_eviction(pdd->dev, p->lead_thread->pid,
					     trigger);

		r = pdd->dev->dqm->ops.evict_process_queues(pdd->dev->dqm,
							    &pdd->qpd);
		/* evict return -EIO if HWS is hang or asic is resetting, in this case
		 * we would like to set all the queues to be in evicted state to prevent
		 * them been add back since they actually not be saved right now.
		 */
		if (r && r != -EIO) {
			dev_err(dev, "Failed to evict process queues\n");
			goto fail;
		}
		n_evicted++;

		pdd->dev->dqm->is_hws_hang = false;
	}

	return r;

fail:
	/* To keep state consistent, roll back partial eviction by
	 * restoring queues
	 */
	for (i = 0; i < p->n_pdds; i++) {
		struct kfd_process_device *pdd = p->pdds[i];

		if (n_evicted == 0)
			break;

		kfd_smi_event_queue_restore(pdd->dev, p->lead_thread->pid);

		if (pdd->dev->dqm->ops.restore_process_queues(pdd->dev->dqm,
							      &pdd->qpd))
			dev_err(pdd->dev->adev->dev,
				"Failed to restore queues\n");

		n_evicted--;
	}

	return r;
}

/* kfd_process_restore_queues - Restore all user queues of a process */
int kfd_process_restore_queues(struct kfd_process *p)
{
	int r, ret = 0;
	int i;

	for (i = 0; i < p->n_pdds; i++) {
		struct kfd_process_device *pdd = p->pdds[i];
		struct device *dev = pdd->dev->adev->dev;

		kfd_smi_event_queue_restore(pdd->dev, p->lead_thread->pid);

		r = pdd->dev->dqm->ops.restore_process_queues(pdd->dev->dqm,
							      &pdd->qpd);
		if (r) {
			dev_err(dev, "Failed to restore process queues\n");
			if (!ret)
				ret = r;
		}
	}

	return ret;
}

int kfd_process_gpuidx_from_gpuid(struct kfd_process *p, uint32_t gpu_id)
{
	int i;

	for (i = 0; i < p->n_pdds; i++)
		if (p->pdds[i] && gpu_id == p->pdds[i]->user_gpu_id)
			return i;
	return -EINVAL;
}

int
kfd_process_gpuid_from_node(struct kfd_process *p, struct kfd_node *node,
			    uint32_t *gpuid, uint32_t *gpuidx)
{
	int i;

	for (i = 0; i < p->n_pdds; i++)
		if (p->pdds[i] && p->pdds[i]->dev == node) {
			*gpuid = p->pdds[i]->user_gpu_id;
			*gpuidx = i;
			return 0;
		}
	return -EINVAL;
}

static int signal_eviction_fence(struct kfd_process *p)
{
	struct dma_fence *ef;
	int ret;

	rcu_read_lock();
	ef = dma_fence_get_rcu_safe(&p->ef);
	rcu_read_unlock();
	if (!ef)
		return -EINVAL;

	ret = dma_fence_signal(ef);
	dma_fence_put(ef);

	return ret;
}

static void evict_process_worker(struct work_struct *work)
{
	int ret;
	struct kfd_process *p;
	struct delayed_work *dwork;

	dwork = to_delayed_work(work);

	/* Process termination destroys this worker thread. So during the
	 * lifetime of this thread, kfd_process p will be valid
	 */
	p = container_of(dwork, struct kfd_process, eviction_work);

	pr_debug("Started evicting pasid 0x%x\n", p->pasid);
	ret = kfd_process_evict_queues(p, KFD_QUEUE_EVICTION_TRIGGER_TTM);
	if (!ret) {
		/* If another thread already signaled the eviction fence,
		 * they are responsible stopping the queues and scheduling
		 * the restore work.
		 */
		if (signal_eviction_fence(p) ||
		    mod_delayed_work(kfd_restore_wq, &p->restore_work,
				     msecs_to_jiffies(PROCESS_RESTORE_TIME_MS)))
			kfd_process_restore_queues(p);

		pr_debug("Finished evicting pasid 0x%x\n", p->pasid);
	} else
		pr_err("Failed to evict queues of pasid 0x%x\n", p->pasid);
}

static int restore_process_helper(struct kfd_process *p)
{
	int ret = 0;

	/* VMs may not have been acquired yet during debugging. */
	if (p->kgd_process_info) {
		ret = amdgpu_amdkfd_gpuvm_restore_process_bos(
			p->kgd_process_info, &p->ef);
		if (ret)
			return ret;
	}

	ret = kfd_process_restore_queues(p);
	if (!ret)
		pr_debug("Finished restoring pasid 0x%x\n", p->pasid);
	else
		pr_err("Failed to restore queues of pasid 0x%x\n", p->pasid);

	return ret;
}

static void restore_process_worker(struct work_struct *work)
{
	struct delayed_work *dwork;
	struct kfd_process *p;
	int ret = 0;

	dwork = to_delayed_work(work);

	/* Process termination destroys this worker thread. So during the
	 * lifetime of this thread, kfd_process p will be valid
	 */
	p = container_of(dwork, struct kfd_process, restore_work);
	pr_debug("Started restoring pasid 0x%x\n", p->pasid);

	/* Setting last_restore_timestamp before successful restoration.
	 * Otherwise this would have to be set by KGD (restore_process_bos)
	 * before KFD BOs are unreserved. If not, the process can be evicted
	 * again before the timestamp is set.
	 * If restore fails, the timestamp will be set again in the next
	 * attempt. This would mean that the minimum GPU quanta would be
	 * PROCESS_ACTIVE_TIME_MS - (time to execute the following two
	 * functions)
	 */

	p->last_restore_timestamp = get_jiffies_64();

	ret = restore_process_helper(p);
	if (ret) {
		pr_debug("Failed to restore BOs of pasid 0x%x, retry after %d ms\n",
			 p->pasid, PROCESS_BACK_OFF_TIME_MS);
		if (mod_delayed_work(kfd_restore_wq, &p->restore_work,
				     msecs_to_jiffies(PROCESS_RESTORE_TIME_MS)))
			kfd_process_restore_queues(p);
	}
}

void kfd_suspend_all_processes(void)
{
	struct kfd_process *p;
	unsigned int temp;
	int idx = srcu_read_lock(&kfd_processes_srcu);

	WARN(debug_evictions, "Evicting all processes");
	hash_for_each_rcu(kfd_processes_table, temp, p, kfd_processes) {
		if (kfd_process_evict_queues(p, KFD_QUEUE_EVICTION_TRIGGER_SUSPEND))
			pr_err("Failed to suspend process 0x%x\n", p->pasid);
		signal_eviction_fence(p);
	}
	srcu_read_unlock(&kfd_processes_srcu, idx);
}

int kfd_resume_all_processes(void)
{
	struct kfd_process *p;
	unsigned int temp;
	int ret = 0, idx = srcu_read_lock(&kfd_processes_srcu);

	hash_for_each_rcu(kfd_processes_table, temp, p, kfd_processes) {
		if (restore_process_helper(p)) {
			pr_err("Restore process %d failed during resume\n",
			       p->pasid);
			ret = -EFAULT;
		}
	}
	srcu_read_unlock(&kfd_processes_srcu, idx);
	return ret;
}

int kfd_reserved_mem_mmap(struct kfd_node *dev, struct kfd_process *process,
			  struct vm_area_struct *vma)
{
	struct kfd_process_device *pdd;
	struct qcm_process_device *qpd;

	if ((vma->vm_end - vma->vm_start) != KFD_CWSR_TBA_TMA_SIZE) {
		dev_err(dev->adev->dev, "Incorrect CWSR mapping size.\n");
		return -EINVAL;
	}

	pdd = kfd_get_process_device_data(dev, process);
	if (!pdd)
		return -EINVAL;
	qpd = &pdd->qpd;

	qpd->cwsr_kaddr = (void *)__get_free_pages(GFP_KERNEL | __GFP_ZERO,
					get_order(KFD_CWSR_TBA_TMA_SIZE));
	if (!qpd->cwsr_kaddr) {
		dev_err(dev->adev->dev,
			"Error allocating per process CWSR buffer.\n");
		return -ENOMEM;
	}

	vm_flags_set(vma, VM_IO | VM_DONTCOPY | VM_DONTEXPAND
		| VM_NORESERVE | VM_DONTDUMP | VM_PFNMAP);
	/* Mapping pages to user process */
	return remap_pfn_range(vma, vma->vm_start,
			       PFN_DOWN(__pa(qpd->cwsr_kaddr)),
			       KFD_CWSR_TBA_TMA_SIZE, vma->vm_page_prot);
}

/* assumes caller holds process lock. */
int kfd_process_drain_interrupts(struct kfd_process_device *pdd)
{
	uint32_t irq_drain_fence[8];
	uint8_t node_id = 0;
	int r = 0;

	if (!KFD_IS_SOC15(pdd->dev))
		return 0;

	pdd->process->irq_drain_is_open = true;

	memset(irq_drain_fence, 0, sizeof(irq_drain_fence));
	irq_drain_fence[0] = (KFD_IRQ_FENCE_SOURCEID << 8) |
							KFD_IRQ_FENCE_CLIENTID;
	irq_drain_fence[3] = pdd->process->pasid;

	/*
	 * For GFX 9.4.3/9.5.0, send the NodeId also in IH cookie DW[3]
	 */
	if (KFD_GC_VERSION(pdd->dev->kfd) == IP_VERSION(9, 4, 3) ||
	    KFD_GC_VERSION(pdd->dev->kfd) == IP_VERSION(9, 4, 4) ||
	    KFD_GC_VERSION(pdd->dev->kfd) == IP_VERSION(9, 5, 0)) {
		node_id = ffs(pdd->dev->interrupt_bitmap) - 1;
		irq_drain_fence[3] |= node_id << 16;
	}

	/* ensure stale irqs scheduled KFD interrupts and send drain fence. */
	if (amdgpu_amdkfd_send_close_event_drain_irq(pdd->dev->adev,
						     irq_drain_fence)) {
		pdd->process->irq_drain_is_open = false;
		return 0;
	}

	r = wait_event_interruptible(pdd->process->wait_irq_drain,
				     !READ_ONCE(pdd->process->irq_drain_is_open));
	if (r)
		pdd->process->irq_drain_is_open = false;

	return r;
}

void kfd_process_close_interrupt_drain(unsigned int pasid)
{
	struct kfd_process *p;

	p = kfd_lookup_process_by_pasid(pasid);

	if (!p)
		return;

	WRITE_ONCE(p->irq_drain_is_open, false);
	wake_up_all(&p->wait_irq_drain);
	kfd_unref_process(p);
}

struct send_exception_work_handler_workarea {
	struct work_struct work;
	struct kfd_process *p;
	unsigned int queue_id;
	uint64_t error_reason;
};

static void send_exception_work_handler(struct work_struct *work)
{
	struct send_exception_work_handler_workarea *workarea;
	struct kfd_process *p;
	struct queue *q;
	struct mm_struct *mm;
	struct kfd_context_save_area_header __user *csa_header;
	uint64_t __user *err_payload_ptr;
	uint64_t cur_err;
	uint32_t ev_id;

	workarea = container_of(work,
				struct send_exception_work_handler_workarea,
				work);
	p = workarea->p;

	mm = get_task_mm(p->lead_thread);

	if (!mm)
		return;

	kthread_use_mm(mm);

	q = pqm_get_user_queue(&p->pqm, workarea->queue_id);

	if (!q)
		goto out;

	csa_header = (void __user *)q->properties.ctx_save_restore_area_address;

	get_user(err_payload_ptr, (uint64_t __user **)&csa_header->err_payload_addr);
	get_user(cur_err, err_payload_ptr);
	cur_err |= workarea->error_reason;
	put_user(cur_err, err_payload_ptr);
	get_user(ev_id, &csa_header->err_event_id);

	kfd_set_event(p, ev_id);

out:
	kthread_unuse_mm(mm);
	mmput(mm);
}

int kfd_send_exception_to_runtime(struct kfd_process *p,
			unsigned int queue_id,
			uint64_t error_reason)
{
	struct send_exception_work_handler_workarea worker;

	INIT_WORK_ONSTACK(&worker.work, send_exception_work_handler);

	worker.p = p;
	worker.queue_id = queue_id;
	worker.error_reason = error_reason;

	schedule_work(&worker.work);
	flush_work(&worker.work);
	destroy_work_on_stack(&worker.work);

	return 0;
}

struct kfd_process_device *kfd_process_device_data_by_id(struct kfd_process *p, uint32_t gpu_id)
{
	int i;

	if (gpu_id) {
		for (i = 0; i < p->n_pdds; i++) {
			struct kfd_process_device *pdd = p->pdds[i];

			if (pdd->user_gpu_id == gpu_id)
				return pdd;
		}
	}
	return NULL;
}

int kfd_process_get_user_gpu_id(struct kfd_process *p, uint32_t actual_gpu_id)
{
	int i;

	if (!actual_gpu_id)
		return 0;

	for (i = 0; i < p->n_pdds; i++) {
		struct kfd_process_device *pdd = p->pdds[i];

		if (pdd->dev->id == actual_gpu_id)
			return pdd->user_gpu_id;
	}
	return -EINVAL;
}

#if defined(CONFIG_DEBUG_FS)

int kfd_debugfs_mqds_by_process(struct seq_file *m, void *data)
{
	struct kfd_process *p;
	unsigned int temp;
	int r = 0;

	int idx = srcu_read_lock(&kfd_processes_srcu);

	hash_for_each_rcu(kfd_processes_table, temp, p, kfd_processes) {
		seq_printf(m, "Process %d PASID 0x%x:\n",
			   p->lead_thread->tgid, p->pasid);

		mutex_lock(&p->mutex);
		r = pqm_debugfs_mqds(m, &p->pqm);
		mutex_unlock(&p->mutex);

		if (r)
			break;
	}

	srcu_read_unlock(&kfd_processes_srcu, idx);

	return r;
}

#endif
