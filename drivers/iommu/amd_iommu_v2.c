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
	iommu_detach_device(dev_state->domain, &dev_state->pdev->dev);
	iommu_domain_free(dev_state->domain);
	kfree(dev_state);
}

static void put_device_state(struct device_state *dev_state)
{
	if (atomic_dec_and_test(&dev_state->count))
		free_device_state(dev_state);
}

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
