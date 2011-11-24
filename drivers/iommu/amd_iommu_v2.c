/*
 * Copyright (C) 2010-2012 Advanced Micro Devices, Inc.
 * Author: Joerg Roedel <joerg.roedel@amd.com>
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

#include <linux/amd-iommu.h>
#include <linux/mm_types.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/iommu.h>
#include <linux/pci.h>
#include <linux/gfp.h>

#include "amd_iommu_proto.h"

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Joerg Roedel <joerg.roedel@amd.com>");

#define MAX_DEVICES		0x10000
#define PRI_QUEUE_SIZE		512

struct pri_queue {
	atomic_t inflight;
	bool finish;
};

struct pasid_state {
	struct list_head list;			/* For global state-list */
	atomic_t count;				/* Reference count */
	struct task_struct *task;		/* Task bound to this PASID */
	struct mm_struct *mm;			/* mm_struct for the faults */
	struct pri_queue pri[PRI_QUEUE_SIZE];	/* PRI tag states */
	struct device_state *device_state;	/* Link to our device_state */
	int pasid;				/* PASID index */
};

struct device_state {
	atomic_t count;
	struct pci_dev *pdev;
	struct pasid_state **states;
	struct iommu_domain *domain;
	int pasid_levels;
	int max_pasids;
	spinlock_t lock;
};

struct device_state **state_table;
static spinlock_t state_lock;

/* List and lock for all pasid_states */
static LIST_HEAD(pasid_state_list);
static DEFINE_SPINLOCK(ps_lock);

static void free_pasid_states(struct device_state *dev_state);
static void unbind_pasid(struct device_state *dev_state, int pasid);

static u16 device_id(struct pci_dev *pdev)
{
	u16 devid;

	devid = pdev->bus->number;
	devid = (devid << 8) | pdev->devfn;

	return devid;
}

static struct device_state *get_device_state(u16 devid)
{
	struct device_state *dev_state;
	unsigned long flags;

	spin_lock_irqsave(&state_lock, flags);
	dev_state = state_table[devid];
	if (dev_state != NULL)
		atomic_inc(&dev_state->count);
	spin_unlock_irqrestore(&state_lock, flags);

	return dev_state;
}

static void free_device_state(struct device_state *dev_state)
{
	/*
	 * First detach device from domain - No more PRI requests will arrive
	 * from that device after it is unbound from the IOMMUv2 domain.
	 */
	iommu_detach_device(dev_state->domain, &dev_state->pdev->dev);

	/* Everything is down now, free the IOMMUv2 domain */
	iommu_domain_free(dev_state->domain);

	/* Finally get rid of the device-state */
	kfree(dev_state);
}

static void put_device_state(struct device_state *dev_state)
{
	if (atomic_dec_and_test(&dev_state->count))
		free_device_state(dev_state);
}

static void link_pasid_state(struct pasid_state *pasid_state)
{
	spin_lock(&ps_lock);
	list_add_tail(&pasid_state->list, &pasid_state_list);
	spin_unlock(&ps_lock);
}

static void __unlink_pasid_state(struct pasid_state *pasid_state)
{
	list_del(&pasid_state->list);
}

static void unlink_pasid_state(struct pasid_state *pasid_state)
{
	spin_lock(&ps_lock);
	__unlink_pasid_state(pasid_state);
	spin_unlock(&ps_lock);
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
	if (atomic_dec_and_test(&pasid_state->count)) {
		put_device_state(pasid_state->device_state);
		mmput(pasid_state->mm);
		free_pasid_state(pasid_state);
	}
}

static void unbind_pasid(struct device_state *dev_state, int pasid)
{
	struct pasid_state *pasid_state;

	pasid_state = get_pasid_state(dev_state, pasid);
	if (pasid_state == NULL)
		return;

	unlink_pasid_state(pasid_state);

	amd_iommu_domain_clear_gcr3(dev_state->domain, pasid);
	clear_pasid_state(dev_state, pasid);

	put_pasid_state(pasid_state); /* Reference taken in this function */
	put_pasid_state(pasid_state); /* Reference taken in bind() function */
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

		unbind_pasid(dev_state, i);
		put_pasid_state(pasid_state);
	}

	if (dev_state->pasid_levels == 2)
		free_pasid_states_level2(dev_state->states);
	else if (dev_state->pasid_levels == 1)
		free_pasid_states_level1(dev_state->states);
	else if (dev_state->pasid_levels != 0)
		BUG();

	free_page((unsigned long)dev_state->states);
}

int amd_iommu_bind_pasid(struct pci_dev *pdev, int pasid,
			 struct task_struct *task)
{
	struct pasid_state *pasid_state;
	struct device_state *dev_state;
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
	pasid_state->task         = task;
	pasid_state->mm           = get_task_mm(task);
	pasid_state->device_state = dev_state;
	pasid_state->pasid        = pasid;

	if (pasid_state->mm == NULL)
		goto out_free;

	ret = set_pasid_state(dev_state, pasid_state, pasid);
	if (ret)
		goto out_free;

	ret = amd_iommu_domain_set_gcr3(dev_state->domain, pasid,
					__pa(pasid_state->mm->pgd));
	if (ret)
		goto out_clear_state;

	link_pasid_state(pasid_state);

	return 0;

out_clear_state:
	clear_pasid_state(dev_state, pasid);

out_free:
	put_pasid_state(pasid_state);

out:
	put_device_state(dev_state);

	return ret;
}
EXPORT_SYMBOL(amd_iommu_bind_pasid);

void amd_iommu_unbind_pasid(struct pci_dev *pdev, int pasid)
{
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

	unbind_pasid(dev_state, pasid);

out:
	put_device_state(dev_state);
}
EXPORT_SYMBOL(amd_iommu_unbind_pasid);

int amd_iommu_init_device(struct pci_dev *pdev, int pasids)
{
	struct device_state *dev_state;
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
	dev_state->pdev = pdev;

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

	ret = iommu_attach_device(dev_state->domain, &pdev->dev);
	if (ret != 0)
		goto out_free_domain;

	spin_lock_irqsave(&state_lock, flags);

	if (state_table[devid] != NULL) {
		spin_unlock_irqrestore(&state_lock, flags);
		ret = -EBUSY;
		goto out_free_domain;
	}

	state_table[devid] = dev_state;

	spin_unlock_irqrestore(&state_lock, flags);

	return 0;

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

	dev_state = state_table[devid];
	if (dev_state == NULL) {
		spin_unlock_irqrestore(&state_lock, flags);
		return;
	}

	state_table[devid] = NULL;

	spin_unlock_irqrestore(&state_lock, flags);

	/* Get rid of any remaining pasid states */
	free_pasid_states(dev_state);

	put_device_state(dev_state);
}
EXPORT_SYMBOL(amd_iommu_free_device);

static int __init amd_iommu_v2_init(void)
{
	size_t state_table_size;

	pr_info("AMD IOMMUv2 driver by Joerg Roedel <joerg.roedel@amd.com>");

	spin_lock_init(&state_lock);

	state_table_size = MAX_DEVICES * sizeof(struct device_state *);
	state_table = (void *)__get_free_pages(GFP_KERNEL | __GFP_ZERO,
					       get_order(state_table_size));
	if (state_table == NULL)
		return -ENOMEM;

	return 0;
}

static void __exit amd_iommu_v2_exit(void)
{
	struct device_state *dev_state;
	size_t state_table_size;
	int i;

	for (i = 0; i < MAX_DEVICES; ++i) {
		dev_state = get_device_state(i);

		if (dev_state == NULL)
			continue;

		WARN_ON_ONCE(1);

		amd_iommu_free_device(dev_state->pdev);
		put_device_state(dev_state);
	}

	state_table_size = MAX_DEVICES * sizeof(struct device_state *);
	free_pages((unsigned long)state_table, get_order(state_table_size));
}

module_init(amd_iommu_v2_init);
module_exit(amd_iommu_v2_exit);
