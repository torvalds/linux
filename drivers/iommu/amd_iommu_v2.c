/*
 * Copyright (C) 2010-2012 Advanced Micro Devices, Inc.
 * Author: Joerg Roedel <jroedel@suse.de>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#define pr_fmt(fmt)     "AMD-Vi: " fmt

#include <linux/mmu_notifier.h>
#include <linux/amd-iommu.h>
#include <linux/mm_types.h>
#include <linux/profile.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/sched/mm.h>
#include <linux/iommu.h>
#include <linux/wait.h>
#include <linux/pci.h>
#include <linux/gfp.h>

#include "amd_iommu_types.h"
#include "amd_iommu_proto.h"

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Joerg Roedel <jroedel@suse.de>");

#define MAX_DEVICES		0x10000
#define PRI_QUEUE_SIZE		512

struct pri_queue {
	atomic_t inflight;
	bool finish;
	int status;
};

struct pasid_state {
	struct list_head list;			/* For global state-list */
	atomic_t count;				/* Reference count */
	unsigned mmu_notifier_count;		/* Counting nested mmu_notifier
						   calls */
	struct mm_struct *mm;			/* mm_struct for the faults */
	struct mmu_notifier mn;                 /* mmu_notifier handle */
	struct pri_queue pri[PRI_QUEUE_SIZE];	/* PRI tag states */
	struct device_state *device_state;	/* Link to our device_state */
	int pasid;				/* PASID index */
	bool invalid;				/* Used during setup and
						   teardown of the pasid */
	spinlock_t lock;			/* Protect pri_queues and
						   mmu_notifer_count */
	wait_queue_head_t wq;			/* To wait for count == 0 */
};

struct device_state {
	struct list_head list;
	u16 devid;
	atomic_t count;
	struct pci_dev *pdev;
	struct pasid_state **states;
	struct iommu_domain *domain;
	int pasid_levels;
	int max_pasids;
	amd_iommu_invalid_ppr_cb inv_ppr_cb;
	amd_iommu_invalidate_ctx inv_ctx_cb;
	spinlock_t lock;
	wait_queue_head_t wq;
};

struct fault {
	struct work_struct work;
	struct device_state *dev_state;
	struct pasid_state *state;
	struct mm_struct *mm;
	u64 address;
	u16 devid;
	u16 pasid;
	u16 tag;
	u16 finish;
	u16 flags;
};

static LIST_HEAD(state_list);
static spinlock_t state_lock;

static struct workqueue_struct *iommu_wq;

static void free_pasid_states(struct device_state *dev_state);

static u16 device_id(struct pci_dev *pdev)
{
	u16 devid;

	devid = pdev->bus->number;
	devid = (devid << 8) | pdev->devfn;

	return devid;
}

static struct device_state *__get_device_state(u16 devid)
{
	struct device_state *dev_state;

	list_for_each_entry(dev_state, &state_list, list) {
		if (dev_state->devid == devid)
			return dev_state;
	}

	return NULL;
}

static struct device_state *get_device_state(u16 devid)
{
	struct device_state *dev_state;
	unsigned long flags;

	spin_lock_irqsave(&state_lock, flags);
	dev_state = __get_device_state(devid);
	if (dev_state != NULL)
		atomic_inc(&dev_state->count);
	spin_unlock_irqrestore(&state_lock, flags);

	return dev_state;
}

static void free_device_state(struct device_state *dev_state)
{
	struct iommu_group *group;

	/*
	 * First detach device from domain - No more PRI requests will arrive
	 * from that device after it is unbound from the IOMMUv2 domain.
	 */
	group = iommu_group_get(&dev_state->pdev->dev);
	if (WARN_ON(!group))
		return;

	iommu_detach_group(dev_state->domain, group);

	iommu_group_put(group);

	/* Everything is down now, free the IOMMUv2 domain */
	iommu_domain_free(dev_state->domain);

	/* Finally get rid of the device-state */
	kfree(dev_state);
}

static void put_device_state(struct device_state *dev_state)
{
	if (atomic_dec_and_test(&dev_state->count))
		wake_up(&dev_state->wq);
}

/* Must be called under dev_state->lock */
static struct pasid_state **__get_pasid_state_ptr(struct device_state *dev_state,
						  int pasid, bool alloc)
{
	struct pasid_state **root, **ptr;
	int level, index;

	level = dev_state->pasid_levels;
	root  = dev_state->states;

	while (true) {

		index = (pasid >> (9 * level)) & 0x1ff;
		ptr   = &root[index];

		if (level == 0)
			break;

		if (*ptr == NULL) {
			if (!alloc)
				return NULL;

			*ptr = (void *)get_zeroed_page(GFP_ATOMIC);
			if (*ptr == NULL)
				return NULL;
		}

		root   = (struct pasid_state **)*ptr;
		level -= 1;
	}

	return ptr;
}

static int set_pasid_state(struct device_state *dev_state,
			   struct pasid_state *pasid_state,
			   int pasid)
{
	struct pasid_state **ptr;
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&dev_state->lock, flags);
	ptr = __get_pasid_state_ptr(dev_state, pasid, true);

	ret = -ENOMEM;
	if (ptr == NULL)
		goto out_unlock;

	ret = -ENOMEM;
	if (*ptr != NULL)
		goto out_unlock;

	*ptr = pasid_state;

	ret = 0;

out_unlock:
	spin_unlock_irqrestore(&dev_state->lock, flags);

	return ret;
}

static void clear_pasid_state(struct device_state *dev_state, int pasid)
{
	struct pasid_state **ptr;
	unsigned long flags;

	spin_lock_irqsave(&dev_state->lock, flags);
	ptr = __get_pasid_state_ptr(dev_state, pasid, true);

	if (ptr == NULL)
		goto out_unlock;

	*ptr = NULL;

out_unlock:
	spin_unlock_irqrestore(&dev_state->lock, flags);
}

static struct pasid_state *get_pasid_state(struct device_state *dev_state,
					   int pasid)
{
	struct pasid_state **ptr, *ret = NULL;
	unsigned long flags;

	spin_lock_irqsave(&dev_state->lock, flags);
	ptr = __get_pasid_state_ptr(dev_state, pasid, false);

	if (ptr == NULL)
		goto out_unlock;

	ret = *ptr;
	if (ret)
		atomic_inc(&ret->count);

out_unlock:
	spin_unlock_irqrestore(&dev_state->lock, flags);

	return ret;
}

static void free_pasid_state(struct pasid_state *pasid_state)
{
	kfree(pasid_state);
}

static void put_pasid_state(struct pasid_state *pasid_state)
{
	if (atomic_dec_and_test(&pasid_state->count))
		wake_up(&pasid_state->wq);
}

static void put_pasid_state_wait(struct pasid_state *pasid_state)
{
	atomic_dec(&pasid_state->count);
	wait_event(pasid_state->wq, !atomic_read(&pasid_state->count));
	free_pasid_state(pasid_state);
}

static void unbind_pasid(struct pasid_state *pasid_state)
{
	struct iommu_domain *domain;

	domain = pasid_state->device_state->domain;

	/*
	 * Mark pasid_state as invalid, no more faults will we added to the
	 * work queue after this is visible everywhere.
	 */
	pasid_state->invalid = true;

	/* Make sure this is visible */
	smp_wmb();

	/* After this the device/pasid can't access the mm anymore */
	amd_iommu_domain_clear_gcr3(domain, pasid_state->pasid);

	/* Make sure no more pending faults are in the queue */
	flush_workqueue(iommu_wq);
}

static void free_pasid_states_level1(struct pasid_state **tbl)
{
	int i;

	for (i = 0; i < 512; ++i) {
		if (tbl[i] == NULL)
			continue;

		free_page((unsigned long)tbl[i]);
	}
}

static void free_pasid_states_level2(struct pasid_state **tbl)
{
	struct pasid_state **ptr;
	int i;

	for (i = 0; i < 512; ++i) {
		if (tbl[i] == NULL)
			continue;

		ptr = (struct pasid_state **)tbl[i];
		free_pasid_states_level1(ptr);
	}
}

static void free_pasid_states(struct device_state *dev_state)
{
	struct pasid_state *pasid_state;
	int i;

	for (i = 0; i < dev_state->max_pasids; ++i) {
		pasid_state = get_pasid_state(dev_state, i);
		if (pasid_state == NULL)
			continue;

		put_pasid_state(pasid_state);

		/*
		 * This will call the mn_release function and
		 * unbind the PASID
		 */
		mmu_notifier_unregister(&pasid_state->mn, pasid_state->mm);

		put_pasid_state_wait(pasid_state); /* Reference taken in
						      amd_iommu_bind_pasid */

		/* Drop reference taken in amd_iommu_bind_pasid */
		put_device_state(dev_state);
	}

	if (dev_state->pasid_levels == 2)
		free_pasid_states_level2(dev_state->states);
	else if (dev_state->pasid_levels == 1)
		free_pasid_states_level1(dev_state->states);
	else
		BUG_ON(dev_state->pasid_levels != 0);

	free_page((unsigned long)dev_state->states);
}

static struct pasid_state *mn_to_state(struct mmu_notifier *mn)
{
	return container_of(mn, struct pasid_state, mn);
}

static void __mn_flush_page(struct mmu_notifier *mn,
			    unsigned long address)
{
	struct pasid_state *pasid_state;
	struct device_state *dev_state;

	pasid_state = mn_to_state(mn);
	dev_state   = pasid_state->device_state;

	amd_iommu_flush_page(dev_state->domain, pasid_state->pasid, address);
}

static int mn_clear_flush_young(struct mmu_notifier *mn,
				struct mm_struct *mm,
				unsigned long start,
				unsigned long end)
{
	for (; start < end; start += PAGE_SIZE)
		__mn_flush_page(mn, start);

	return 0;
}

static void mn_invalidate_range(struct mmu_notifier *mn,
				struct mm_struct *mm,
				unsigned long start, unsigned long end)
{
	struct pasid_state *pasid_state;
	struct device_state *dev_state;

	pasid_state = mn_to_state(mn);
	dev_state   = pasid_state->device_state;

	if ((start ^ (end - 1)) < PAGE_SIZE)
		amd_iommu_flush_page(dev_state->domain, pasid_state->pasid,
				     start);
	else
		amd_iommu_flush_tlb(dev_state->domain, pasid_state->pasid);
}

static void mn_release(struct mmu_notifier *mn, struct mm_struct *mm)
{
	struct pasid_state *pasid_state;
	struct device_state *dev_state;
	bool run_inv_ctx_cb;

	might_sleep();

	pasid_state    = mn_to_state(mn);
	dev_state      = pasid_state->device_state;
	run_inv_ctx_cb = !pasid_state->invalid;

	if (run_inv_ctx_cb && dev_state->inv_ctx_cb)
		dev_state->inv_ctx_cb(dev_state->pdev, pasid_state->pasid);

	unbind_pasid(pasid_state);
}

static const struct mmu_notifier_ops iommu_mn = {
	.release		= mn_release,
	.clear_flush_young      = mn_clear_flush_young,
	.invalidate_range       = mn_invalidate_range,
};

static void set_pri_tag_status(struct pasid_state *pasid_state,
			       u16 tag, int status)
{
	unsigned long flags;

	spin_lock_irqsave(&pasid_state->lock, flags);
	pasid_state->pri[tag].status = status;
	spin_unlock_irqrestore(&pasid_state->lock, flags);
}

static void finish_pri_tag(struct device_state *dev_state,
			   struct pasid_state *pasid_state,
			   u16 tag)
{
	unsigned long flags;

	spin_lock_irqsave(&pasid_state->lock, flags);
	if (atomic_dec_and_test(&pasid_state->pri[tag].inflight) &&
	    pasid_state->pri[tag].finish) {
		amd_iommu_complete_ppr(dev_state->pdev, pasid_state->pasid,
				       pasid_state->pri[tag].status, tag);
		pasid_state->pri[tag].finish = false;
		pasid_state->pri[tag].status = PPR_SUCCESS;
	}
	spin_unlock_irqrestore(&pasid_state->lock, flags);
}

static void handle_fault_error(struct fault *fault)
{
	int status;

	if (!fault->dev_state->inv_ppr_cb) {
		set_pri_tag_status(fault->state, fault->tag, PPR_INVALID);
		return;
	}

	status = fault->dev_state->inv_ppr_cb(fault->dev_state->pdev,
					      fault->pasid,
					      fault->address,
					      fault->flags);
	switch (status) {
	case AMD_IOMMU_INV_PRI_RSP_SUCCESS:
		set_pri_tag_status(fault->state, fault->tag, PPR_SUCCESS);
		break;
	case AMD_IOMMU_INV_PRI_RSP_INVALID:
		set_pri_tag_status(fault->state, fault->tag, PPR_INVALID);
		break;
	case AMD_IOMMU_INV_PRI_RSP_FAIL:
		set_pri_tag_status(fault->state, fault->tag, PPR_FAILURE);
		break;
	default:
		BUG();
	}
}

static bool access_error(struct vm_area_struct *vma, struct fault *fault)
{
	unsigned long requested = 0;

	if (fault->flags & PPR_FAULT_EXEC)
		requested |= VM_EXEC;

	if (fault->flags & PPR_FAULT_READ)
		requested |= VM_READ;

	if (fault->flags & PPR_FAULT_WRITE)
		requested |= VM_WRITE;

	return (requested & ~vma->vm_flags) != 0;
}

static void do_fault(struct work_struct *work)
{
	struct fault *fault = container_of(work, struct fault, work);
	struct vm_area_struct *vma;
	vm_fault_t ret = VM_FAULT_ERROR;
	unsigned int flags = 0;
	struct mm_struct *mm;
	u64 address;

	mm = fault->state->mm;
	address = fault->address;

	if (fault->flags & PPR_FAULT_USER)
		flags |= FAULT_FLAG_USER;
	if (fault->flags & PPR_FAULT_WRITE)
		flags |= FAULT_FLAG_WRITE;
	flags |= FAULT_FLAG_REMOTE;

	down_read(&mm->mmap_sem);
	vma = find_extend_vma(mm, address);
	if (!vma || address < vma->vm_start)
		/* failed to get a vma in the right range */
		goto out;

	/* Check if we have the right permissions on the vma */
	if (access_error(vma, fault))
		goto out;

	ret = handle_mm_fault(vma, address, flags);
out:
	up_read(&mm->mmap_sem);

	if (ret & VM_FAULT_ERROR)
		/* failed to service fault */
		handle_fault_error(fault);

	finish_pri_tag(fault->dev_state, fault->state, fault->tag);

	put_pasid_state(fault->state);

	kfree(fault);
}

static int ppr_notifier(struct notifier_block *nb, unsigned long e, void *data)
{
	struct amd_iommu_fault *iommu_fault;
	struct pasid_state *pasid_state;
	struct device_state *dev_state;
	unsigned long flags;
	struct fault *fault;
	bool finish;
	u16 tag, devid;
	int ret;
	struct iommu_dev_data *dev_data;
	struct pci_dev *pdev = NULL;

	iommu_fault = data;
	tag         = iommu_fault->tag & 0x1ff;
	finish      = (iommu_fault->tag >> 9) & 1;

	devid = iommu_fault->device_id;
	pdev = pci_get_domain_bus_and_slot(0, PCI_BUS_NUM(devid),
					   devid & 0xff);
	if (!pdev)
		return -ENODEV;
	dev_data = get_dev_data(&pdev->dev);

	/* In kdump kernel pci dev is not initialized yet -> send INVALID */
	ret = NOTIFY_DONE;
	if (translation_pre_enabled(amd_iommu_rlookup_table[devid])
		&& dev_data->defer_attach) {
		amd_iommu_complete_ppr(pdev, iommu_fault->pasid,
				       PPR_INVALID, tag);
		goto out;
	}

	dev_state = get_device_state(iommu_fault->device_id);
	if (dev_state == NULL)
		goto out;

	pasid_state = get_pasid_state(dev_state, iommu_fault->pasid);
	if (pasid_state == NULL || pasid_state->invalid) {
		/* We know the device but not the PASID -> send INVALID */
		amd_iommu_complete_ppr(dev_state->pdev, iommu_fault->pasid,
				       PPR_INVALID, tag);
		goto out_drop_state;
	}

	spin_lock_irqsave(&pasid_state->lock, flags);
	atomic_inc(&pasid_state->pri[tag].inflight);
	if (finish)
		pasid_state->pri[tag].finish = true;
	spin_unlock_irqrestore(&pasid_state->lock, flags);

	fault = kzalloc(sizeof(*fault), GFP_ATOMIC);
	if (fault == NULL) {
		/* We are OOM - send success and let the device re-fault */
		finish_pri_tag(dev_state, pasid_state, tag);
		goto out_drop_state;
	}

	fault->dev_state = dev_state;
	fault->address   = iommu_fault->address;
	fault->state     = pasid_state;
	fault->tag       = tag;
	fault->finish    = finish;
	fault->pasid     = iommu_fault->pasid;
	fault->flags     = iommu_fault->flags;
	INIT_WORK(&fault->work, do_fault);

	queue_work(iommu_wq, &fault->work);

	ret = NOTIFY_OK;

out_drop_state:

	if (ret != NOTIFY_OK && pasid_state)
		put_pasid_state(pasid_state);

	put_device_state(dev_state);

out:
	return ret;
}

static struct notifier_block ppr_nb = {
	.notifier_call = ppr_notifier,
};

int amd_iommu_bind_pasid(struct pci_dev *pdev, int pasid,
			 struct task_struct *task)
{
	struct pasid_state *pasid_state;
	struct device_state *dev_state;
	struct mm_struct *mm;
	u16 devid;
	int ret;

	might_sleep();

	if (!amd_iommu_v2_supported())
		return -ENODEV;

	devid     = device_id(pdev);
	dev_state = get_device_state(devid);

	if (dev_state == NULL)
		return -EINVAL;

	ret = -EINVAL;
	if (pasid < 0 || pasid >= dev_state->max_pasids)
		goto out;

	ret = -ENOMEM;
	pasid_state = kzalloc(sizeof(*pasid_state), GFP_KERNEL);
	if (pasid_state == NULL)
		goto out;


	atomic_set(&pasid_state->count, 1);
	init_waitqueue_head(&pasid_state->wq);
	spin_lock_init(&pasid_state->lock);

	mm                        = get_task_mm(task);
	pasid_state->mm           = mm;
	pasid_state->device_state = dev_state;
	pasid_state->pasid        = pasid;
	pasid_state->invalid      = true; /* Mark as valid only if we are
					     done with setting up the pasid */
	pasid_state->mn.ops       = &iommu_mn;

	if (pasid_state->mm == NULL)
		goto out_free;

	mmu_notifier_register(&pasid_state->mn, mm);

	ret = set_pasid_state(dev_state, pasid_state, pasid);
	if (ret)
		goto out_unregister;

	ret = amd_iommu_domain_set_gcr3(dev_state->domain, pasid,
					__pa(pasid_state->mm->pgd));
	if (ret)
		goto out_clear_state;

	/* Now we are ready to handle faults */
	pasid_state->invalid = false;

	/*
	 * Drop the reference to the mm_struct here. We rely on the
	 * mmu_notifier release call-back to inform us when the mm
	 * is going away.
	 */
	mmput(mm);

	return 0;

out_clear_state:
	clear_pasid_state(dev_state, pasid);

out_unregister:
	mmu_notifier_unregister(&pasid_state->mn, mm);
	mmput(mm);

out_free:
	free_pasid_state(pasid_state);

out:
	put_device_state(dev_state);

	return ret;
}
EXPORT_SYMBOL(amd_iommu_bind_pasid);

void amd_iommu_unbind_pasid(struct pci_dev *pdev, int pasid)
{
	struct pasid_state *pasid_state;
	struct device_state *dev_state;
	u16 devid;

	might_sleep();

	if (!amd_iommu_v2_supported())
		return;

	devid = device_id(pdev);
	dev_state = get_device_state(devid);
	if (dev_state == NULL)
		return;

	if (pasid < 0 || pasid >= dev_state->max_pasids)
		goto out;

	pasid_state = get_pasid_state(dev_state, pasid);
	if (pasid_state == NULL)
		goto out;
	/*
	 * Drop reference taken here. We are safe because we still hold
	 * the reference taken in the amd_iommu_bind_pasid function.
	 */
	put_pasid_state(pasid_state);

	/* Clear the pasid state so that the pasid can be re-used */
	clear_pasid_state(dev_state, pasid_state->pasid);

	/*
	 * Call mmu_notifier_unregister to drop our reference
	 * to pasid_state->mm
	 */
	mmu_notifier_unregister(&pasid_state->mn, pasid_state->mm);

	put_pasid_state_wait(pasid_state); /* Reference taken in
					      amd_iommu_bind_pasid */
out:
	/* Drop reference taken in this function */
	put_device_state(dev_state);

	/* Drop reference taken in amd_iommu_bind_pasid */
	put_device_state(dev_state);
}
EXPORT_SYMBOL(amd_iommu_unbind_pasid);

int amd_iommu_init_device(struct pci_dev *pdev, int pasids)
{
	struct device_state *dev_state;
	struct iommu_group *group;
	unsigned long flags;
	int ret, tmp;
	u16 devid;

	might_sleep();

	if (!amd_iommu_v2_supported())
		return -ENODEV;

	if (pasids <= 0 || pasids > (PASID_MASK + 1))
		return -EINVAL;

	devid = device_id(pdev);

	dev_state = kzalloc(sizeof(*dev_state), GFP_KERNEL);
	if (dev_state == NULL)
		return -ENOMEM;

	spin_lock_init(&dev_state->lock);
	init_waitqueue_head(&dev_state->wq);
	dev_state->pdev  = pdev;
	dev_state->devid = devid;

	tmp = pasids;
	for (dev_state->pasid_levels = 0; (tmp - 1) & ~0x1ff; tmp >>= 9)
		dev_state->pasid_levels += 1;

	atomic_set(&dev_state->count, 1);
	dev_state->max_pasids = pasids;

	ret = -ENOMEM;
	dev_state->states = (void *)get_zeroed_page(GFP_KERNEL);
	if (dev_state->states == NULL)
		goto out_free_dev_state;

	dev_state->domain = iommu_domain_alloc(&pci_bus_type);
	if (dev_state->domain == NULL)
		goto out_free_states;

	amd_iommu_domain_direct_map(dev_state->domain);

	ret = amd_iommu_domain_enable_v2(dev_state->domain, pasids);
	if (ret)
		goto out_free_domain;

	group = iommu_group_get(&pdev->dev);
	if (!group) {
		ret = -EINVAL;
		goto out_free_domain;
	}

	ret = iommu_attach_group(dev_state->domain, group);
	if (ret != 0)
		goto out_drop_group;

	iommu_group_put(group);

	spin_lock_irqsave(&state_lock, flags);

	if (__get_device_state(devid) != NULL) {
		spin_unlock_irqrestore(&state_lock, flags);
		ret = -EBUSY;
		goto out_free_domain;
	}

	list_add_tail(&dev_state->list, &state_list);

	spin_unlock_irqrestore(&state_lock, flags);

	return 0;

out_drop_group:
	iommu_group_put(group);

out_free_domain:
	iommu_domain_free(dev_state->domain);

out_free_states:
	free_page((unsigned long)dev_state->states);

out_free_dev_state:
	kfree(dev_state);

	return ret;
}
EXPORT_SYMBOL(amd_iommu_init_device);

void amd_iommu_free_device(struct pci_dev *pdev)
{
	struct device_state *dev_state;
	unsigned long flags;
	u16 devid;

	if (!amd_iommu_v2_supported())
		return;

	devid = device_id(pdev);

	spin_lock_irqsave(&state_lock, flags);

	dev_state = __get_device_state(devid);
	if (dev_state == NULL) {
		spin_unlock_irqrestore(&state_lock, flags);
		return;
	}

	list_del(&dev_state->list);

	spin_unlock_irqrestore(&state_lock, flags);

	/* Get rid of any remaining pasid states */
	free_pasid_states(dev_state);

	put_device_state(dev_state);
	/*
	 * Wait until the last reference is dropped before freeing
	 * the device state.
	 */
	wait_event(dev_state->wq, !atomic_read(&dev_state->count));
	free_device_state(dev_state);
}
EXPORT_SYMBOL(amd_iommu_free_device);

int amd_iommu_set_invalid_ppr_cb(struct pci_dev *pdev,
				 amd_iommu_invalid_ppr_cb cb)
{
	struct device_state *dev_state;
	unsigned long flags;
	u16 devid;
	int ret;

	if (!amd_iommu_v2_supported())
		return -ENODEV;

	devid = device_id(pdev);

	spin_lock_irqsave(&state_lock, flags);

	ret = -EINVAL;
	dev_state = __get_device_state(devid);
	if (dev_state == NULL)
		goto out_unlock;

	dev_state->inv_ppr_cb = cb;

	ret = 0;

out_unlock:
	spin_unlock_irqrestore(&state_lock, flags);

	return ret;
}
EXPORT_SYMBOL(amd_iommu_set_invalid_ppr_cb);

int amd_iommu_set_invalidate_ctx_cb(struct pci_dev *pdev,
				    amd_iommu_invalidate_ctx cb)
{
	struct device_state *dev_state;
	unsigned long flags;
	u16 devid;
	int ret;

	if (!amd_iommu_v2_supported())
		return -ENODEV;

	devid = device_id(pdev);

	spin_lock_irqsave(&state_lock, flags);

	ret = -EINVAL;
	dev_state = __get_device_state(devid);
	if (dev_state == NULL)
		goto out_unlock;

	dev_state->inv_ctx_cb = cb;

	ret = 0;

out_unlock:
	spin_unlock_irqrestore(&state_lock, flags);

	return ret;
}
EXPORT_SYMBOL(amd_iommu_set_invalidate_ctx_cb);

static int __init amd_iommu_v2_init(void)
{
	int ret;

	pr_info("AMD IOMMUv2 driver by Joerg Roedel <jroedel@suse.de>\n");

	if (!amd_iommu_v2_supported()) {
		pr_info("AMD IOMMUv2 functionality not available on this system\n");
		/*
		 * Load anyway to provide the symbols to other modules
		 * which may use AMD IOMMUv2 optionally.
		 */
		return 0;
	}

	spin_lock_init(&state_lock);

	ret = -ENOMEM;
	iommu_wq = alloc_workqueue("amd_iommu_v2", WQ_MEM_RECLAIM, 0);
	if (iommu_wq == NULL)
		goto out;

	amd_iommu_register_ppr_notifier(&ppr_nb);

	return 0;

out:
	return ret;
}

static void __exit amd_iommu_v2_exit(void)
{
	struct device_state *dev_state;
	int i;

	if (!amd_iommu_v2_supported())
		return;

	amd_iommu_unregister_ppr_notifier(&ppr_nb);

	flush_workqueue(iommu_wq);

	/*
	 * The loop below might call flush_workqueue(), so call
	 * destroy_workqueue() after it
	 */
	for (i = 0; i < MAX_DEVICES; ++i) {
		dev_state = get_device_state(i);

		if (dev_state == NULL)
			continue;

		WARN_ON_ONCE(1);

		put_device_state(dev_state);
		amd_iommu_free_device(dev_state->pdev);
	}

	destroy_workqueue(iommu_wq);
}

module_init(amd_iommu_v2_init);
module_exit(amd_iommu_v2_exit);
