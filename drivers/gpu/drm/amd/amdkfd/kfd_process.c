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
 */

#include <linux/mutex.h>
#include <linux/log2.h>
#include <linux/sched.h>
#include <linux/sched/mm.h>
#include <linux/sched/task.h>
#include <linux/slab.h>
#include <linux/amd-iommu.h>
#include <linux/notifier.h>
#include <linux/compat.h>
#include <linux/mman.h>
#include <linux/file.h>
#include "amdgpu_amdkfd.h"

struct mm_struct;

#include "kfd_priv.h"
#include "kfd_device_queue_manager.h"
#include "kfd_dbgmgr.h"
#include "kfd_iommu.h"

/*
 * List of struct kfd_process (field kfd_process).
 * Unique/indexed by mm_struct*
 */
DEFINE_HASHTABLE(kfd_processes_table, KFD_PROCESS_TABLE_SIZE);
static DEFINE_MUTEX(kfd_processes_mutex);

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

static struct kfd_process *find_process(const struct task_struct *thread);
static void kfd_process_ref_release(struct kref *ref);
static struct kfd_process *create_process(const struct task_struct *thread);
static int kfd_process_init_cwsr_apu(struct kfd_process *p, struct file *filep);

static void evict_process_worker(struct work_struct *work);
static void restore_process_worker(struct work_struct *work);

struct kfd_procfs_tree {
	struct kobject *kobj;
};

static struct kfd_procfs_tree procfs;

static ssize_t kfd_procfs_show(struct kobject *kobj, struct attribute *attr,
			       char *buffer)
{
	int val = 0;

	if (strcmp(attr->name, "pasid") == 0) {
		struct kfd_process *p = container_of(attr, struct kfd_process,
						     attr_pasid);
		val = p->pasid;
	} else {
		pr_err("Invalid attribute");
		return -EINVAL;
	}

	return snprintf(buffer, PAGE_SIZE, "%d\n", val);
}

static void kfd_procfs_kobj_release(struct kobject *kobj)
{
	kfree(kobj);
}

static const struct sysfs_ops kfd_procfs_ops = {
	.show = kfd_procfs_show,
};

static struct kobj_type procfs_type = {
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

int kfd_process_create_wq(void)
{
	if (!kfd_process_wq)
		kfd_process_wq = alloc_workqueue("kfd_process_wq", 0, 0);
	if (!kfd_restore_wq)
		kfd_restore_wq = alloc_ordered_workqueue("kfd_restore_wq", 0);

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
			struct kfd_process_device *pdd)
{
	struct kfd_dev *dev = pdd->dev;

	amdgpu_amdkfd_gpuvm_unmap_memory_from_gpu(dev->kgd, mem, pdd->vm);
	amdgpu_amdkfd_gpuvm_free_memory_of_gpu(dev->kgd, mem);
}

/* kfd_process_alloc_gpuvm - Allocate GPU VM for the KFD process
 *	This function should be only called right after the process
 *	is created and when kfd_processes_mutex is still being held
 *	to avoid concurrency. Because of that exclusiveness, we do
 *	not need to take p->mutex.
 */
static int kfd_process_alloc_gpuvm(struct kfd_process_device *pdd,
				   uint64_t gpu_va, uint32_t size,
				   uint32_t flags, void **kptr)
{
	struct kfd_dev *kdev = pdd->dev;
	struct kgd_mem *mem = NULL;
	int handle;
	int err;

	err = amdgpu_amdkfd_gpuvm_alloc_memory_of_gpu(kdev->kgd, gpu_va, size,
						 pdd->vm, &mem, NULL, flags);
	if (err)
		goto err_alloc_mem;

	err = amdgpu_amdkfd_gpuvm_map_memory_to_gpu(kdev->kgd, mem, pdd->vm);
	if (err)
		goto err_map_mem;

	err = amdgpu_amdkfd_gpuvm_sync_memory(kdev->kgd, mem, true);
	if (err) {
		pr_debug("Sync memory failed, wait interrupted by user signal\n");
		goto sync_memory_failed;
	}

	/* Create an obj handle so kfd_process_device_remove_obj_handle
	 * will take care of the bo removal when the process finishes.
	 * We do not need to take p->mutex, because the process is just
	 * created and the ioctls have not had the chance to run.
	 */
	handle = kfd_process_device_create_obj_handle(pdd, mem);

	if (handle < 0) {
		err = handle;
		goto free_gpuvm;
	}

	if (kptr) {
		err = amdgpu_amdkfd_gpuvm_map_gtt_bo_to_kernel(kdev->kgd,
				(struct kgd_mem *)mem, kptr, NULL);
		if (err) {
			pr_debug("Map GTT BO to kernel failed\n");
			goto free_obj_handle;
		}
	}

	return err;

free_obj_handle:
	kfd_process_device_remove_obj_handle(pdd, handle);
free_gpuvm:
sync_memory_failed:
	kfd_process_free_gpuvm(mem, pdd);
	return err;

err_map_mem:
	amdgpu_amdkfd_gpuvm_free_memory_of_gpu(kdev->kgd, mem);
err_alloc_mem:
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
	uint32_t flags = ALLOC_MEM_FLAGS_GTT |
			 ALLOC_MEM_FLAGS_NO_SUBSTITUTE |
			 ALLOC_MEM_FLAGS_WRITABLE |
			 ALLOC_MEM_FLAGS_EXECUTABLE;
	void *kaddr;
	int ret;

	if (qpd->ib_kaddr || !qpd->ib_base)
		return 0;

	/* ib_base is only set for dGPU */
	ret = kfd_process_alloc_gpuvm(pdd, qpd->ib_base, PAGE_SIZE, flags,
				      &kaddr);
	if (ret)
		return ret;

	qpd->ib_kaddr = kaddr;

	return 0;
}

struct kfd_process *kfd_create_process(struct file *filep)
{
	struct kfd_process *process;
	struct task_struct *thread = current;
	int ret;

	if (!thread->mm)
		return ERR_PTR(-EINVAL);

	/* Only the pthreads threading model is supported. */
	if (thread->group_leader->mm != thread->mm)
		return ERR_PTR(-EINVAL);

	/*
	 * take kfd processes mutex before starting of process creation
	 * so there won't be a case where two threads of the same process
	 * create two kfd_process structures
	 */
	mutex_lock(&kfd_processes_mutex);

	/* A prior open of /dev/kfd could have already created the process. */
	process = find_process(thread);
	if (process) {
		pr_debug("Process already found\n");
	} else {
		process = create_process(thread);
		if (IS_ERR(process))
			goto out;

		ret = kfd_process_init_cwsr_apu(process, filep);
		if (ret) {
			process = ERR_PTR(ret);
			goto out;
		}

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
			goto out;
		}

		process->attr_pasid.name = "pasid";
		process->attr_pasid.mode = KFD_SYSFS_FILE_MODE;
		sysfs_attr_init(&process->attr_pasid);
		ret = sysfs_create_file(process->kobj, &process->attr_pasid);
		if (ret)
			pr_warn("Creating pasid for pid %d failed",
					(int)process->lead_thread->pid);
	}
out:
	mutex_unlock(&kfd_processes_mutex);

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

	process = find_process(thread);
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

static struct kfd_process *find_process(const struct task_struct *thread)
{
	struct kfd_process *p;
	int idx;

	idx = srcu_read_lock(&kfd_processes_srcu);
	p = find_process_by_mm(thread->mm);
	srcu_read_unlock(&kfd_processes_srcu, idx);

	return p;
}

void kfd_unref_process(struct kfd_process *p)
{
	kref_put(&p->ref, kfd_process_ref_release);
}

static void kfd_process_device_free_bos(struct kfd_process_device *pdd)
{
	struct kfd_process *p = pdd->process;
	void *mem;
	int id;

	/*
	 * Remove all handles from idr and release appropriate
	 * local memory object
	 */
	idr_for_each_entry(&pdd->alloc_idr, mem, id) {
		struct kfd_process_device *peer_pdd;

		list_for_each_entry(peer_pdd, &p->per_device_data,
				    per_device_list) {
			if (!peer_pdd->vm)
				continue;
			amdgpu_amdkfd_gpuvm_unmap_memory_from_gpu(
				peer_pdd->dev->kgd, mem, peer_pdd->vm);
		}

		amdgpu_amdkfd_gpuvm_free_memory_of_gpu(pdd->dev->kgd, mem);
		kfd_process_device_remove_obj_handle(pdd, id);
	}
}

static void kfd_process_free_outstanding_kfd_bos(struct kfd_process *p)
{
	struct kfd_process_device *pdd;

	list_for_each_entry(pdd, &p->per_device_data, per_device_list)
		kfd_process_device_free_bos(pdd);
}

static void kfd_process_destroy_pdds(struct kfd_process *p)
{
	struct kfd_process_device *pdd, *temp;

	list_for_each_entry_safe(pdd, temp, &p->per_device_data,
				 per_device_list) {
		pr_debug("Releasing pdd (topology id %d) for process (pasid 0x%x)\n",
				pdd->dev->id, p->pasid);

		if (pdd->drm_file) {
			amdgpu_amdkfd_gpuvm_release_process_vm(
					pdd->dev->kgd, pdd->vm);
			fput(pdd->drm_file);
		}
		else if (pdd->vm)
			amdgpu_amdkfd_gpuvm_destroy_process_vm(
				pdd->dev->kgd, pdd->vm);

		list_del(&pdd->per_device_list);

		if (pdd->qpd.cwsr_kaddr && !pdd->qpd.cwsr_base)
			free_pages((unsigned long)pdd->qpd.cwsr_kaddr,
				get_order(KFD_CWSR_TBA_TMA_SIZE));

		kfree(pdd->qpd.doorbell_bitmap);
		idr_destroy(&pdd->alloc_idr);

		kfree(pdd);
	}
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

	/* Remove the procfs files */
	if (p->kobj) {
		sysfs_remove_file(p->kobj, &p->attr_pasid);
		kobject_del(p->kobj);
		kobject_put(p->kobj);
		p->kobj = NULL;
	}

	kfd_iommu_unbind_process(p);

	kfd_process_free_outstanding_kfd_bos(p);

	kfd_process_destroy_pdds(p);
	dma_fence_put(p->ef);

	kfd_event_free_process(p);

	kfd_pasid_free(p->pasid);
	kfd_free_process_doorbells(p);

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

static void kfd_process_free_notifier(struct mmu_notifier *mn)
{
	kfd_unref_process(container_of(mn, struct kfd_process, mmu_notifier));
}

static void kfd_process_notifier_release(struct mmu_notifier *mn,
					struct mm_struct *mm)
{
	struct kfd_process *p;
	struct kfd_process_device *pdd = NULL;

	/*
	 * The kfd_process structure can not be free because the
	 * mmu_notifier srcu is read locked
	 */
	p = container_of(mn, struct kfd_process, mmu_notifier);
	if (WARN_ON(p->mm != mm))
		return;

	mutex_lock(&kfd_processes_mutex);
	hash_del_rcu(&p->kfd_processes);
	mutex_unlock(&kfd_processes_mutex);
	synchronize_srcu(&kfd_processes_srcu);

	cancel_delayed_work_sync(&p->eviction_work);
	cancel_delayed_work_sync(&p->restore_work);

	mutex_lock(&p->mutex);

	/* Iterate over all process device data structures and if the
	 * pdd is in debug mode, we should first force unregistration,
	 * then we will be able to destroy the queues
	 */
	list_for_each_entry(pdd, &p->per_device_data, per_device_list) {
		struct kfd_dev *dev = pdd->dev;

		mutex_lock(kfd_get_dbgmgr_mutex());
		if (dev && dev->dbgmgr && dev->dbgmgr->pasid == p->pasid) {
			if (!kfd_dbgmgr_unregister(dev->dbgmgr, p)) {
				kfd_dbgmgr_destroy(dev->dbgmgr);
				dev->dbgmgr = NULL;
			}
		}
		mutex_unlock(kfd_get_dbgmgr_mutex());
	}

	kfd_process_dequeue_from_all_devices(p);
	pqm_uninit(&p->pqm);

	/* Indicate to other users that MM is no longer valid */
	p->mm = NULL;

	mutex_unlock(&p->mutex);

	mmu_notifier_put(&p->mmu_notifier);
}

static const struct mmu_notifier_ops kfd_process_mmu_notifier_ops = {
	.release = kfd_process_notifier_release,
	.free_notifier = kfd_process_free_notifier,
};

static int kfd_process_init_cwsr_apu(struct kfd_process *p, struct file *filep)
{
	unsigned long  offset;
	struct kfd_process_device *pdd;

	list_for_each_entry(pdd, &p->per_device_data, per_device_list) {
		struct kfd_dev *dev = pdd->dev;
		struct qcm_process_device *qpd = &pdd->qpd;

		if (!dev->cwsr_enabled || qpd->cwsr_kaddr || qpd->cwsr_base)
			continue;

		offset = (KFD_MMAP_TYPE_RESERVED_MEM | KFD_MMAP_GPU_ID(dev->id))
			<< PAGE_SHIFT;
		qpd->tba_addr = (int64_t)vm_mmap(filep, 0,
			KFD_CWSR_TBA_TMA_SIZE, PROT_READ | PROT_EXEC,
			MAP_SHARED, offset);

		if (IS_ERR_VALUE(qpd->tba_addr)) {
			int err = qpd->tba_addr;

			pr_err("Failure to set tba address. error %d.\n", err);
			qpd->tba_addr = 0;
			qpd->cwsr_kaddr = NULL;
			return err;
		}

		memcpy(qpd->cwsr_kaddr, dev->cwsr_isa, dev->cwsr_isa_size);

		qpd->tma_addr = qpd->tba_addr + KFD_CWSR_TMA_OFFSET;
		pr_debug("set tba :0x%llx, tma:0x%llx, cwsr_kaddr:%p for pqm.\n",
			qpd->tba_addr, qpd->tma_addr, qpd->cwsr_kaddr);
	}

	return 0;
}

static int kfd_process_device_init_cwsr_dgpu(struct kfd_process_device *pdd)
{
	struct kfd_dev *dev = pdd->dev;
	struct qcm_process_device *qpd = &pdd->qpd;
	uint32_t flags = ALLOC_MEM_FLAGS_GTT |
		ALLOC_MEM_FLAGS_NO_SUBSTITUTE | ALLOC_MEM_FLAGS_EXECUTABLE;
	void *kaddr;
	int ret;

	if (!dev->cwsr_enabled || qpd->cwsr_kaddr || !qpd->cwsr_base)
		return 0;

	/* cwsr_base is only set for dGPU */
	ret = kfd_process_alloc_gpuvm(pdd, qpd->cwsr_base,
				      KFD_CWSR_TBA_TMA_SIZE, flags, &kaddr);
	if (ret)
		return ret;

	qpd->cwsr_kaddr = kaddr;
	qpd->tba_addr = qpd->cwsr_base;

	memcpy(qpd->cwsr_kaddr, dev->cwsr_isa, dev->cwsr_isa_size);

	qpd->tma_addr = qpd->tba_addr + KFD_CWSR_TMA_OFFSET;
	pr_debug("set tba :0x%llx, tma:0x%llx, cwsr_kaddr:%p for pqm.\n",
		 qpd->tba_addr, qpd->tma_addr, qpd->cwsr_kaddr);

	return 0;
}

/*
 * On return the kfd_process is fully operational and will be freed when the
 * mm is released
 */
static struct kfd_process *create_process(const struct task_struct *thread)
{
	struct kfd_process *process;
	int err = -ENOMEM;

	process = kzalloc(sizeof(*process), GFP_KERNEL);
	if (!process)
		goto err_alloc_process;

	kref_init(&process->ref);
	mutex_init(&process->mutex);
	process->mm = thread->mm;
	process->lead_thread = thread->group_leader;
	INIT_LIST_HEAD(&process->per_device_data);
	INIT_DELAYED_WORK(&process->eviction_work, evict_process_worker);
	INIT_DELAYED_WORK(&process->restore_work, restore_process_worker);
	process->last_restore_timestamp = get_jiffies_64();
	kfd_event_init_process(process);
	process->is_32bit_user_mode = in_compat_syscall();

	process->pasid = kfd_pasid_alloc();
	if (process->pasid == 0)
		goto err_alloc_pasid;

	if (kfd_alloc_process_doorbells(process) < 0)
		goto err_alloc_doorbells;

	err = pqm_init(&process->pqm, process);
	if (err != 0)
		goto err_process_pqm_init;

	/* init process apertures*/
	err = kfd_init_apertures(process);
	if (err != 0)
		goto err_init_apertures;

	/* Must be last, have to use release destruction after this */
	process->mmu_notifier.ops = &kfd_process_mmu_notifier_ops;
	err = mmu_notifier_register(&process->mmu_notifier, process->mm);
	if (err)
		goto err_register_notifier;

	get_task_struct(process->lead_thread);
	hash_add_rcu(kfd_processes_table, &process->kfd_processes,
			(uintptr_t)process->mm);

	return process;

err_register_notifier:
	kfd_process_free_outstanding_kfd_bos(process);
	kfd_process_destroy_pdds(process);
err_init_apertures:
	pqm_uninit(&process->pqm);
err_process_pqm_init:
	kfd_free_process_doorbells(process);
err_alloc_doorbells:
	kfd_pasid_free(process->pasid);
err_alloc_pasid:
	mutex_destroy(&process->mutex);
	kfree(process);
err_alloc_process:
	return ERR_PTR(err);
}

static int init_doorbell_bitmap(struct qcm_process_device *qpd,
			struct kfd_dev *dev)
{
	unsigned int i;
	int range_start = dev->shared_resources.non_cp_doorbells_start;
	int range_end = dev->shared_resources.non_cp_doorbells_end;

	if (!KFD_IS_SOC15(dev->device_info->asic_family))
		return 0;

	qpd->doorbell_bitmap =
		kzalloc(DIV_ROUND_UP(KFD_MAX_NUM_OF_QUEUES_PER_PROCESS,
				     BITS_PER_BYTE), GFP_KERNEL);
	if (!qpd->doorbell_bitmap)
		return -ENOMEM;

	/* Mask out doorbells reserved for SDMA, IH, and VCN on SOC15. */
	pr_debug("reserved doorbell 0x%03x - 0x%03x\n", range_start, range_end);
	pr_debug("reserved doorbell 0x%03x - 0x%03x\n",
			range_start + KFD_QUEUE_DOORBELL_MIRROR_OFFSET,
			range_end + KFD_QUEUE_DOORBELL_MIRROR_OFFSET);

	for (i = 0; i < KFD_MAX_NUM_OF_QUEUES_PER_PROCESS / 2; i++) {
		if (i >= range_start && i <= range_end) {
			set_bit(i, qpd->doorbell_bitmap);
			set_bit(i + KFD_QUEUE_DOORBELL_MIRROR_OFFSET,
				qpd->doorbell_bitmap);
		}
	}

	return 0;
}

struct kfd_process_device *kfd_get_process_device_data(struct kfd_dev *dev,
							struct kfd_process *p)
{
	struct kfd_process_device *pdd = NULL;

	list_for_each_entry(pdd, &p->per_device_data, per_device_list)
		if (pdd->dev == dev)
			return pdd;

	return NULL;
}

struct kfd_process_device *kfd_create_process_device_data(struct kfd_dev *dev,
							struct kfd_process *p)
{
	struct kfd_process_device *pdd = NULL;

	pdd = kzalloc(sizeof(*pdd), GFP_KERNEL);
	if (!pdd)
		return NULL;

	if (init_doorbell_bitmap(&pdd->qpd, dev)) {
		pr_err("Failed to init doorbell for process\n");
		kfree(pdd);
		return NULL;
	}

	pdd->dev = dev;
	INIT_LIST_HEAD(&pdd->qpd.queues_list);
	INIT_LIST_HEAD(&pdd->qpd.priv_queue_list);
	pdd->qpd.dqm = dev->dqm;
	pdd->qpd.pqm = &p->pqm;
	pdd->qpd.evicted = 0;
	pdd->process = p;
	pdd->bound = PDD_UNBOUND;
	pdd->already_dequeued = false;
	list_add(&pdd->per_device_list, &p->per_device_data);

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
	struct kfd_process *p;
	struct kfd_dev *dev;
	int ret;

	if (pdd->vm)
		return drm_file ? -EBUSY : 0;

	p = pdd->process;
	dev = pdd->dev;

	if (drm_file)
		ret = amdgpu_amdkfd_gpuvm_acquire_process_vm(
			dev->kgd, drm_file, p->pasid,
			&pdd->vm, &p->kgd_process_info, &p->ef);
	else
		ret = amdgpu_amdkfd_gpuvm_create_process_vm(dev->kgd, p->pasid,
			&pdd->vm, &p->kgd_process_info, &p->ef);
	if (ret) {
		pr_err("Failed to create process VM object\n");
		return ret;
	}

	amdgpu_vm_set_task_info(pdd->vm);

	ret = kfd_process_device_reserve_ib_mem(pdd);
	if (ret)
		goto err_reserve_ib_mem;
	ret = kfd_process_device_init_cwsr_dgpu(pdd);
	if (ret)
		goto err_init_cwsr;

	pdd->drm_file = drm_file;

	return 0;

err_init_cwsr:
err_reserve_ib_mem:
	kfd_process_device_free_bos(pdd);
	if (!drm_file)
		amdgpu_amdkfd_gpuvm_destroy_process_vm(dev->kgd, pdd->vm);
	pdd->vm = NULL;

	return ret;
}

/*
 * Direct the IOMMU to bind the process (specifically the pasid->mm)
 * to the device.
 * Unbinding occurs when the process dies or the device is removed.
 *
 * Assumes that the process lock is held.
 */
struct kfd_process_device *kfd_bind_process_to_device(struct kfd_dev *dev,
							struct kfd_process *p)
{
	struct kfd_process_device *pdd;
	int err;

	pdd = kfd_get_process_device_data(dev, p);
	if (!pdd) {
		pr_err("Process device data doesn't exist\n");
		return ERR_PTR(-ENOMEM);
	}

	err = kfd_iommu_bind_process_to_device(pdd);
	if (err)
		return ERR_PTR(err);

	err = kfd_process_device_init_vm(pdd, NULL);
	if (err)
		return ERR_PTR(err);

	return pdd;
}

struct kfd_process_device *kfd_get_first_process_device_data(
						struct kfd_process *p)
{
	return list_first_entry(&p->per_device_data,
				struct kfd_process_device,
				per_device_list);
}

struct kfd_process_device *kfd_get_next_process_device_data(
						struct kfd_process *p,
						struct kfd_process_device *pdd)
{
	if (list_is_last(&pdd->per_device_list, &p->per_device_data))
		return NULL;
	return list_next_entry(pdd, per_device_list);
}

bool kfd_has_process_device_data(struct kfd_process *p)
{
	return !(list_empty(&p->per_device_data));
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
struct kfd_process *kfd_lookup_process_by_pasid(unsigned int pasid)
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

/* process_evict_queues - Evict all user queues of a process
 *
 * Eviction is reference-counted per process-device. This means multiple
 * evictions from different sources can be nested safely.
 */
int kfd_process_evict_queues(struct kfd_process *p)
{
	struct kfd_process_device *pdd;
	int r = 0;
	unsigned int n_evicted = 0;

	list_for_each_entry(pdd, &p->per_device_data, per_device_list) {
		r = pdd->dev->dqm->ops.evict_process_queues(pdd->dev->dqm,
							    &pdd->qpd);
		if (r) {
			pr_err("Failed to evict process queues\n");
			goto fail;
		}
		n_evicted++;
	}

	return r;

fail:
	/* To keep state consistent, roll back partial eviction by
	 * restoring queues
	 */
	list_for_each_entry(pdd, &p->per_device_data, per_device_list) {
		if (n_evicted == 0)
			break;
		if (pdd->dev->dqm->ops.restore_process_queues(pdd->dev->dqm,
							      &pdd->qpd))
			pr_err("Failed to restore queues\n");

		n_evicted--;
	}

	return r;
}

/* process_restore_queues - Restore all user queues of a process */
int kfd_process_restore_queues(struct kfd_process *p)
{
	struct kfd_process_device *pdd;
	int r, ret = 0;

	list_for_each_entry(pdd, &p->per_device_data, per_device_list) {
		r = pdd->dev->dqm->ops.restore_process_queues(pdd->dev->dqm,
							      &pdd->qpd);
		if (r) {
			pr_err("Failed to restore process queues\n");
			if (!ret)
				ret = r;
		}
	}

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
	WARN_ONCE(p->last_eviction_seqno != p->ef->seqno,
		  "Eviction fence mismatch\n");

	/* Narrow window of overlap between restore and evict work
	 * item is possible. Once amdgpu_amdkfd_gpuvm_restore_process_bos
	 * unreserves KFD BOs, it is possible to evicted again. But
	 * restore has few more steps of finish. So lets wait for any
	 * previous restore work to complete
	 */
	flush_delayed_work(&p->restore_work);

	pr_debug("Started evicting pasid 0x%x\n", p->pasid);
	ret = kfd_process_evict_queues(p);
	if (!ret) {
		dma_fence_signal(p->ef);
		dma_fence_put(p->ef);
		p->ef = NULL;
		queue_delayed_work(kfd_restore_wq, &p->restore_work,
				msecs_to_jiffies(PROCESS_RESTORE_TIME_MS));

		pr_debug("Finished evicting pasid 0x%x\n", p->pasid);
	} else
		pr_err("Failed to evict queues of pasid 0x%x\n", p->pasid);
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
	ret = amdgpu_amdkfd_gpuvm_restore_process_bos(p->kgd_process_info,
						     &p->ef);
	if (ret) {
		pr_debug("Failed to restore BOs of pasid 0x%x, retry after %d ms\n",
			 p->pasid, PROCESS_BACK_OFF_TIME_MS);
		ret = queue_delayed_work(kfd_restore_wq, &p->restore_work,
				msecs_to_jiffies(PROCESS_BACK_OFF_TIME_MS));
		WARN(!ret, "reschedule restore work failed\n");
		return;
	}

	ret = kfd_process_restore_queues(p);
	if (!ret)
		pr_debug("Finished restoring pasid 0x%x\n", p->pasid);
	else
		pr_err("Failed to restore queues of pasid 0x%x\n", p->pasid);
}

void kfd_suspend_all_processes(void)
{
	struct kfd_process *p;
	unsigned int temp;
	int idx = srcu_read_lock(&kfd_processes_srcu);

	hash_for_each_rcu(kfd_processes_table, temp, p, kfd_processes) {
		cancel_delayed_work_sync(&p->eviction_work);
		cancel_delayed_work_sync(&p->restore_work);

		if (kfd_process_evict_queues(p))
			pr_err("Failed to suspend process 0x%x\n", p->pasid);
		dma_fence_signal(p->ef);
		dma_fence_put(p->ef);
		p->ef = NULL;
	}
	srcu_read_unlock(&kfd_processes_srcu, idx);
}

int kfd_resume_all_processes(void)
{
	struct kfd_process *p;
	unsigned int temp;
	int ret = 0, idx = srcu_read_lock(&kfd_processes_srcu);

	hash_for_each_rcu(kfd_processes_table, temp, p, kfd_processes) {
		if (!queue_delayed_work(kfd_restore_wq, &p->restore_work, 0)) {
			pr_err("Restore process %d failed during resume\n",
			       p->pasid);
			ret = -EFAULT;
		}
	}
	srcu_read_unlock(&kfd_processes_srcu, idx);
	return ret;
}

int kfd_reserved_mem_mmap(struct kfd_dev *dev, struct kfd_process *process,
			  struct vm_area_struct *vma)
{
	struct kfd_process_device *pdd;
	struct qcm_process_device *qpd;

	if ((vma->vm_end - vma->vm_start) != KFD_CWSR_TBA_TMA_SIZE) {
		pr_err("Incorrect CWSR mapping size.\n");
		return -EINVAL;
	}

	pdd = kfd_get_process_device_data(dev, process);
	if (!pdd)
		return -EINVAL;
	qpd = &pdd->qpd;

	qpd->cwsr_kaddr = (void *)__get_free_pages(GFP_KERNEL | __GFP_ZERO,
					get_order(KFD_CWSR_TBA_TMA_SIZE));
	if (!qpd->cwsr_kaddr) {
		pr_err("Error allocating per process CWSR buffer.\n");
		return -ENOMEM;
	}

	vma->vm_flags |= VM_IO | VM_DONTCOPY | VM_DONTEXPAND
		| VM_NORESERVE | VM_DONTDUMP | VM_PFNMAP;
	/* Mapping pages to user process */
	return remap_pfn_range(vma, vma->vm_start,
			       PFN_DOWN(__pa(qpd->cwsr_kaddr)),
			       KFD_CWSR_TBA_TMA_SIZE, vma->vm_page_prot);
}

void kfd_flush_tlb(struct kfd_process_device *pdd)
{
	struct kfd_dev *dev = pdd->dev;
	const struct kfd2kgd_calls *f2g = dev->kfd2kgd;

	if (dev->dqm->sched_policy == KFD_SCHED_POLICY_NO_HWS) {
		/* Nothing to flush until a VMID is assigned, which
		 * only happens when the first queue is created.
		 */
		if (pdd->qpd.vmid)
			f2g->invalidate_tlbs_vmid(dev->kgd, pdd->qpd.vmid);
	} else {
		f2g->invalidate_tlbs(dev->kgd, pdd->process->pasid);
	}
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

