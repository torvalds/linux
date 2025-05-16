// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023, Microsoft Corporation.
 *
 * Authors: Microsoft Linux virtualization team
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <asm/mshyperv.h>

#include "mshv_eventfd.h"
#include "mshv.h"
#include "mshv_root.h"

/* called from the ioctl code, user wants to update the guest irq table */
int mshv_update_routing_table(struct mshv_partition *partition,
			      const struct mshv_user_irq_entry *ue,
			      unsigned int numents)
{
	struct mshv_girq_routing_table *new = NULL, *old;
	u32 i, nr_rt_entries = 0;
	int r = 0;

	if (numents == 0)
		goto swap_routes;

	for (i = 0; i < numents; i++) {
		if (ue[i].gsi >= MSHV_MAX_GUEST_IRQS)
			return -EINVAL;

		if (ue[i].address_hi)
			return -EINVAL;

		nr_rt_entries = max(nr_rt_entries, ue[i].gsi);
	}
	nr_rt_entries += 1;

	new = kzalloc(struct_size(new, mshv_girq_info_tbl, nr_rt_entries),
		      GFP_KERNEL_ACCOUNT);
	if (!new)
		return -ENOMEM;

	new->num_rt_entries = nr_rt_entries;
	for (i = 0; i < numents; i++) {
		struct mshv_guest_irq_ent *girq;

		girq = &new->mshv_girq_info_tbl[ue[i].gsi];

		/*
		 * Allow only one to one mapping between GSI and MSI routing.
		 */
		if (girq->guest_irq_num != 0) {
			r = -EINVAL;
			goto out;
		}

		girq->guest_irq_num = ue[i].gsi;
		girq->girq_addr_lo = ue[i].address_lo;
		girq->girq_addr_hi = ue[i].address_hi;
		girq->girq_irq_data = ue[i].data;
		girq->girq_entry_valid = true;
	}

swap_routes:
	mutex_lock(&partition->pt_irq_lock);
	old = rcu_dereference_protected(partition->pt_girq_tbl, 1);
	rcu_assign_pointer(partition->pt_girq_tbl, new);
	mshv_irqfd_routing_update(partition);
	mutex_unlock(&partition->pt_irq_lock);

	synchronize_srcu_expedited(&partition->pt_irq_srcu);
	new = old;

out:
	kfree(new);

	return r;
}

/* vm is going away, kfree the irq routing table */
void mshv_free_routing_table(struct mshv_partition *partition)
{
	struct mshv_girq_routing_table *rt =
				   rcu_access_pointer(partition->pt_girq_tbl);

	kfree(rt);
}

struct mshv_guest_irq_ent
mshv_ret_girq_entry(struct mshv_partition *partition, u32 irqnum)
{
	struct mshv_guest_irq_ent entry = { 0 };
	struct mshv_girq_routing_table *girq_tbl;

	girq_tbl = srcu_dereference_check(partition->pt_girq_tbl,
					  &partition->pt_irq_srcu,
					  lockdep_is_held(&partition->pt_irq_lock));
	if (!girq_tbl || irqnum >= girq_tbl->num_rt_entries) {
		/*
		 * Premature register_irqfd, setting valid_entry = 0
		 * would ignore this entry anyway
		 */
		entry.guest_irq_num = irqnum;
		return entry;
	}

	return girq_tbl->mshv_girq_info_tbl[irqnum];
}

void mshv_copy_girq_info(struct mshv_guest_irq_ent *ent,
			 struct mshv_lapic_irq *lirq)
{
	memset(lirq, 0, sizeof(*lirq));
	if (!ent || !ent->girq_entry_valid)
		return;

	lirq->lapic_vector = ent->girq_irq_data & 0xFF;
	lirq->lapic_apic_id = (ent->girq_addr_lo >> 12) & 0xFF;
	lirq->lapic_control.interrupt_type = (ent->girq_irq_data & 0x700) >> 8;
	lirq->lapic_control.level_triggered = (ent->girq_irq_data >> 15) & 0x1;
	lirq->lapic_control.logical_dest_mode = (ent->girq_addr_lo >> 2) & 0x1;
}
