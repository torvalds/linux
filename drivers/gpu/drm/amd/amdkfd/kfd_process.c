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
#include <linux/slab.h>
#include <linux/amd-iommu.h>
#include <linux/notifier.h>
#include <linux/compat.h>

struct mm_struct;

#include "kfd_priv.h"
#include "kfd_dbgmgr.h"

/*
 * Initial size for the array of queues.
 * The allocated size is doubled each time
 * it is exceeded up to MAX_PROCESS_QUEUES.
 */
#define INITIAL_QUEUE_ARRAY_SIZE 16

/*
 * List of struct kfd_process (field kfd_process).
 * Unique/indexed by mm_struct*
 */
#define KFD_PROCESS_TABLE_SIZE 5 /* bits: 32 entries */
static DEFINE_HASHTABLE(kfd_processes_table, KFD_PROCESS_TABLE_SIZE);
static DEFINE_MUTEX(kfd_processes_mutex);

DEFINE_STATIC_SRCU(kfd_processes_srcu);

static struct workqueue_struct *kfd_process_wq;

struct kfd_process_release_work {
	struct work_struct kfd_work;
	struct kfd_process *p;
};

static struct kfd_process *find_process(const struct task_struct *thread);
static struct kfd_process *create_process(const struct task_struct *thread);

void kfd_process_create_wq(void)
{
	if (!kfd_process_wq)
		kfd_process_wq = create_workqueue("kfd_process_wq");
}

void kfd_process_destroy_wq(void)
{
	if (kfd_process_wq) {
		flush_workqueue(kfd_process_wq);
		destroy_workqueue(kfd_process_wq);
		kfd_process_wq = NULL;
	}
}

struct kfd_process *kfd_create_process(const struct task_struct *thread)
{
	struct kfd_process *process;

	BUG_ON(!kfd_process_wq);

	if (thread->mm == NULL)
		return ERR_PTR(-EINVAL);

	/* Only the pthreads threading model is supported. */
	if (thread->group_leader->mm != thread->mm)
		return ERR_PTR(-EINVAL);

	/* Take mmap_sem because we call __mmu_notifier_register inside */
	down_write(&thread->mm->mmap_sem);

	/*
	 * take kfd processes mutex before starting of process creation
	 * so there won't be a case where two threads of the same process
	 * create two kfd_process structures
	 */
	mutex_lock(&kfd_processes_mutex);

	/* A prior open of /dev/kfd could have already created the process. */
	process = find_process(thread);
	if (process)
		pr_debug("kfd: process already found\n");

	if (!process)
		process = create_process(thread);

	mutex_unlock(&kfd_processes_mutex);

	up_write(&thread->mm->mmap_sem);

	return process;
}

struct kfd_process *kfd_get_process(const struct task_struct *thread)
{
	struct kfd_process *process;

	if (thread->mm == NULL)
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

static void kfd_process_wq_release(struct work_struct *work)
{
	struct kfd_process_release_work *my_work;
	struct kfd_process_device *pdd, *temp;
	struct kfd_process *p;

	my_work = (struct kfd_process_release_work *) work;

	p = my_work->p;

	pr_debug("Releasing process (pasid %d) in workqueue\n",
			p->pasid);

	mutex_lock(&p->mutex);

	list_for_each_entry_safe(pdd, temp, &p->per_device_data,
							per_device_list) {
		pr_debug("Releasing pdd (topology id %d) for process (pasid %d) in workqueue\n",
				pdd->dev->id, p->pasid);

		if (pdd->reset_wavefronts)
			dbgdev_wave_reset_wavefronts(pdd->dev, p);

		amd_iommu_unbind_pasid(pdd->dev->pdev, p->pasid);
		list_del(&pdd->per_device_list);

		kfree(pdd);
	}

	kfd_event_free_process(p);

	kfd_pasid_free(p->pasid);

	mutex_unlock(&p->mutex);

	mutex_destroy(&p->mutex);

	kfree(p->queues);

	kfree(p);

	kfree((void *)work);
}

static void kfd_process_destroy_delayed(struct rcu_head *rcu)
{
	struct kfd_process_release_work *work;
	struct kfd_process *p;

	BUG_ON(!kfd_process_wq);

	p = container_of(rcu, struct kfd_process, rcu);
	BUG_ON(atomic_read(&p->mm->mm_count) <= 0);

	mmdrop(p->mm);

	work = kmalloc(sizeof(struct kfd_process_release_work), GFP_ATOMIC);

	if (work) {
		INIT_WORK((struct work_struct *) work, kfd_process_wq_release);
		work->p = p;
		queue_work(kfd_process_wq, (struct work_struct *) work);
	}
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
	BUG_ON(p->mm != mm);

	mutex_lock(&kfd_processes_mutex);
	hash_del_rcu(&p->kfd_processes);
	mutex_unlock(&kfd_processes_mutex);
	synchronize_srcu(&kfd_processes_srcu);

	mutex_lock(&p->mutex);

	/* In case our notifier is called before IOMMU notifier */
	pqm_uninit(&p->pqm);

	/* Iterate over all process device data structure and check
	 * if we should delete debug managers and reset all wavefronts
	 */
	list_for_each_entry(pdd, &p->per_device_data, per_device_list) {
		if ((pdd->dev->dbgmgr) &&
				(pdd->dev->dbgmgr->pasid == p->pasid))
			kfd_dbgmgr_destroy(pdd->dev->dbgmgr);

		if (pdd->reset_wavefronts) {
			pr_warn("amdkfd: Resetting all wave fronts\n");
			dbgdev_wave_reset_wavefronts(pdd->dev, p);
			pdd->reset_wavefronts = false;
		}
	}

	mutex_unlock(&p->mutex);

	/*
	 * Because we drop mm_count inside kfd_process_destroy_delayed
	 * and because the mmu_notifier_unregister function also drop
	 * mm_count we need to take an extra count here.
	 */
	atomic_inc(&p->mm->mm_count);
	mmu_notifier_unregister_no_release(&p->mmu_notifier, p->mm);
	mmu_notifier_call_srcu(&p->rcu, &kfd_process_destroy_delayed);
}

static const struct mmu_notifier_ops kfd_process_mmu_notifier_ops = {
	.release = kfd_process_notifier_release,
};

static struct kfd_process *create_process(const struct task_struct *thread)
{
	struct kfd_process *process;
	int err = -ENOMEM;

	process = kzalloc(sizeof(*process), GFP_KERNEL);

	if (!process)
		goto err_alloc_process;

	process->queues = kmalloc_array(INITIAL_QUEUE_ARRAY_SIZE,
					sizeof(process->queues[0]), GFP_KERNEL);
	if (!process->queues)
		goto err_alloc_queues;

	process->pasid = kfd_pasid_alloc();
	if (process->pasid == 0)
		goto err_alloc_pasid;

	mutex_init(&process->mutex);

	process->mm = thread->mm;

	/* register notifier */
	process->mmu_notifier.ops = &kfd_process_mmu_notifier_ops;
	err = __mmu_notifier_register(&process->mmu_notifier, process->mm);
	if (err)
		goto err_mmu_notifier;

	hash_add_rcu(kfd_processes_table, &process->kfd_processes,
			(uintptr_t)process->mm);

	process->lead_thread = thread->group_leader;

	process->queue_array_size = INITIAL_QUEUE_ARRAY_SIZE;

	INIT_LIST_HEAD(&process->per_device_data);

	kfd_event_init_process(process);

	err = pqm_init(&process->pqm, process);
	if (err != 0)
		goto err_process_pqm_init;

	/* init process apertures*/
	process->is_32bit_user_mode = is_compat_task();
	if (kfd_init_apertures(process) != 0)
		goto err_init_apretures;

	return process;

err_init_apretures:
	pqm_uninit(&process->pqm);
err_process_pqm_init:
	hash_del_rcu(&process->kfd_processes);
	synchronize_rcu();
	mmu_notifier_unregister_no_release(&process->mmu_notifier, process->mm);
err_mmu_notifier:
	kfd_pasid_free(process->pasid);
err_alloc_pasid:
	kfree(process->queues);
err_alloc_queues:
	kfree(process);
err_alloc_process:
	return ERR_PTR(err);
}

struct kfd_process_device *kfd_get_process_device_data(struct kfd_dev *dev,
							struct kfd_process *p)
{
	struct kfd_process_device *pdd = NULL;

	list_for_each_entry(pdd, &p->per_device_data, per_device_list)
		if (pdd->dev == dev)
			break;

	return pdd;
}

struct kfd_process_device *kfd_create_process_device_data(struct kfd_dev *dev,
							struct kfd_process *p)
{
	struct kfd_process_device *pdd = NULL;

	pdd = kzalloc(sizeof(*pdd), GFP_KERNEL);
	if (pdd != NULL) {
		pdd->dev = dev;
		INIT_LIST_HEAD(&pdd->qpd.queues_list);
		INIT_LIST_HEAD(&pdd->qpd.priv_queue_list);
		pdd->qpd.dqm = dev->dqm;
		pdd->reset_wavefronts = false;
		list_add(&pdd->per_device_list, &p->per_device_data);
	}

	return pdd;
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

	if (pdd->bound)
		return pdd;

	err = amd_iommu_bind_pasid(dev->pdev, p->pasid, p->lead_thread);
	if (err < 0)
		return ERR_PTR(err);

	pdd->bound = true;

	return pdd;
}

void kfd_unbind_process_from_device(struct kfd_dev *dev, unsigned int pasid)
{
	struct kfd_process *p;
	struct kfd_process_device *pdd;
	int idx, i;

	BUG_ON(dev == NULL);

	idx = srcu_read_lock(&kfd_processes_srcu);

	/*
	 * Look for the process that matches the pasid. If there is no such
	 * process, we either released it in amdkfd's own notifier, or there
	 * is a bug. Unfortunately, there is no way to tell...
	 */
	hash_for_each_rcu(kfd_processes_table, i, p, kfd_processes)
		if (p->pasid == pasid) {

			srcu_read_unlock(&kfd_processes_srcu, idx);

			pr_debug("Unbinding process %d from IOMMU\n", pasid);

			mutex_lock(&p->mutex);

			if ((dev->dbgmgr) && (dev->dbgmgr->pasid == p->pasid))
				kfd_dbgmgr_destroy(dev->dbgmgr);

			pqm_uninit(&p->pqm);

			pdd = kfd_get_process_device_data(dev, p);

			if (!pdd) {
				mutex_unlock(&p->mutex);
				return;
			}

			if (pdd->reset_wavefronts) {
				dbgdev_wave_reset_wavefronts(pdd->dev, p);
				pdd->reset_wavefronts = false;
			}

			/*
			 * Just mark pdd as unbound, because we still need it
			 * to call amd_iommu_unbind_pasid() in when the
			 * process exits.
			 * We don't call amd_iommu_unbind_pasid() here
			 * because the IOMMU called us.
			 */
			pdd->bound = false;

			mutex_unlock(&p->mutex);

			return;
		}

	srcu_read_unlock(&kfd_processes_srcu, idx);
}

struct kfd_process_device *kfd_get_first_process_device_data(struct kfd_process *p)
{
	return list_first_entry(&p->per_device_data,
				struct kfd_process_device,
				per_device_list);
}

struct kfd_process_device *kfd_get_next_process_device_data(struct kfd_process *p,
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

/* This returns with process->mutex locked. */
struct kfd_process *kfd_lookup_process_by_pasid(unsigned int pasid)
{
	struct kfd_process *p;
	unsigned int temp;

	int idx = srcu_read_lock(&kfd_processes_srcu);

	hash_for_each_rcu(kfd_processes_table, temp, p, kfd_processes) {
		if (p->pasid == pasid) {
			mutex_lock(&p->mutex);
			break;
		}
	}

	srcu_read_unlock(&kfd_processes_srcu, idx);

	return p;
}
