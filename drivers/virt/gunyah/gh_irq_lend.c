// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2023, Qualcomm Innovation Center, Inc. All rights reserved.
 *
 */

#include <linux/irq.h>
#include <linux/module.h>
#include <linux/notifier.h>
#include <linux/spinlock.h>
#include <dt-bindings/interrupt-controller/arm-gic.h>
#include <linux/gunyah/gh_irq_lend.h>
#include <linux/gunyah/gh_rm_drv.h>

#include "gh_rm_drv_private.h"

struct gh_irq_entry {
	gh_vmid_t vmid;
	enum gh_vm_names vm_name;
	gh_irq_handle_fn_v2 v2_handle;
	gh_irq_handle_fn handle;
	void *data;

	enum {
		GH_IRQ_STATE_NONE,

		GH_IRQ_STATE_WAIT_RELEASE_OR_ACCEPT,
		GH_IRQ_STATE_WAIT_LEND,

		/* notification states */
		GH_IRQ_STATE_RELEASED, /* svm -> pvm */
		GH_IRQ_STATE_ACCEPTED, /* svm -> pvm */
		GH_IRQ_STATE_LENT, /* pvm -> svm */
	} state;
	gh_virq_handle_t virq_handle;
};

static struct gh_irq_entry gh_irq_entries[GH_IRQ_LABEL_MAX];
static DEFINE_SPINLOCK(gh_irq_lend_lock);

static int gh_irq_released_accepted_nb_handler(struct notifier_block *this,
				      unsigned long cmd, void *data)
{
	unsigned long flags;
	enum gh_irq_label label;
	struct gh_irq_entry *entry;
	struct gh_rm_notif_vm_irq_released_payload *released;
	struct gh_rm_notif_vm_irq_accepted_payload *accepted;

	if (cmd != GH_RM_NOTIF_VM_IRQ_RELEASED &&
			cmd != GH_RM_NOTIF_VM_IRQ_ACCEPTED)
		return NOTIFY_DONE;

	spin_lock_irqsave(&gh_irq_lend_lock, flags);
	for (label = 0; label < GH_IRQ_LABEL_MAX; label++) {
		entry = &gh_irq_entries[label];

		if (entry->state != GH_IRQ_STATE_WAIT_RELEASE_OR_ACCEPT &&
					entry->state != GH_IRQ_STATE_ACCEPTED)
			continue;

		switch (cmd) {
		case GH_RM_NOTIF_VM_IRQ_RELEASED:
			released = data;
			if (released->virq_handle == entry->virq_handle) {
				entry->state = GH_IRQ_STATE_RELEASED;
				spin_unlock_irqrestore(&gh_irq_lend_lock,
									flags);
				entry->v2_handle(entry->data, cmd, label);
				return NOTIFY_OK;
			}

			break;
		case GH_RM_NOTIF_VM_IRQ_ACCEPTED:
			accepted = data;
			if (accepted->virq_handle == entry->virq_handle) {
				entry->state = GH_IRQ_STATE_ACCEPTED;
				spin_unlock_irqrestore(&gh_irq_lend_lock,
									flags);
				entry->v2_handle(entry->data, cmd, label);
				return NOTIFY_OK;
			}

			break;
		}
	}
	spin_unlock_irqrestore(&gh_irq_lend_lock, flags);

	return NOTIFY_DONE;
}

static struct notifier_block gh_irq_released_accepted_nb = {
	.notifier_call = gh_irq_released_accepted_nb_handler,
};

static int gh_irq_lent_nb_handler(struct notifier_block *this,
				  unsigned long cmd, void *data)
{
	unsigned long flags;
	enum gh_irq_label label;
	enum gh_vm_names owner_name;
	struct gh_irq_entry *entry;
	struct gh_rm_notif_vm_irq_lent_payload *lent = data;
	int ret;

	if (cmd != GH_RM_NOTIF_VM_IRQ_LENT)
		return NOTIFY_DONE;

	ret = gh_rm_get_vm_name(lent->owner_vmid, &owner_name);
	if (ret) {
		pr_warn_ratelimited("%s: unknown name for vmid: %d\n", __func__,
				    lent->owner_vmid);
		return ret;
	}

	spin_lock_irqsave(&gh_irq_lend_lock, flags);
	for (label = 0; label < GH_IRQ_LABEL_MAX; label++) {
		entry = &gh_irq_entries[label];
		if (entry->state != GH_IRQ_STATE_WAIT_LEND &&
				entry->state != GH_IRQ_STATE_LENT)
			continue;

		if (label == lent->virq_label &&
		    (entry->vm_name == GH_VM_MAX ||
		     entry->vm_name == owner_name)) {
			entry->vmid = lent->owner_vmid;
			entry->virq_handle = lent->virq_handle;

			entry->state = GH_IRQ_STATE_LENT;
			spin_unlock_irqrestore(&gh_irq_lend_lock,
					       flags);

			entry->v2_handle(entry->data, cmd, label);

			return NOTIFY_OK;
		}
	}
	spin_unlock_irqrestore(&gh_irq_lend_lock, flags);

	return NOTIFY_DONE;
}

static struct notifier_block gh_irq_lent_nb = {
	.notifier_call = gh_irq_lent_nb_handler,
};

/**
 * gh_irq_lend_v2: Lend a hardware interrupt to another VM
 * @label: vIRQ high-level label
 * @name: VM name to send interrupt to
 * @irq: Linux IRQ number to lend
 * @cb_handle: callback to invoke when other VM release or accept the interrupt
 * @data: Argument to pass to cb_handle
 *
 * Returns 0 on success also the handle corresponding to Linux IRQ#.
 * Returns < 0 on error
 */
int gh_irq_lend_v2(enum gh_irq_label label, enum gh_vm_names name,
		int irq, gh_irq_handle_fn_v2 cb_handle, void *data)
{
	int ret, virq;
	unsigned long flags;
	struct gh_irq_entry *entry;

	if (label >= GH_IRQ_LABEL_MAX || !cb_handle)
		return -EINVAL;

	entry = &gh_irq_entries[label];

	if (gh_rm_irq_to_virq(irq, &virq))
		return -EINVAL;

	spin_lock_irqsave(&gh_irq_lend_lock, flags);
	if (entry->state != GH_IRQ_STATE_NONE) {
		spin_unlock_irqrestore(&gh_irq_lend_lock, flags);
		return -EINVAL;
	}

	ret = ghd_rm_get_vmid(name, &entry->vmid);
	if (ret) {
		entry->state = GH_IRQ_STATE_NONE;
		spin_unlock_irqrestore(&gh_irq_lend_lock, flags);
		return ret;
	}

	entry->v2_handle = cb_handle;
	entry->data = data;
	entry->state = GH_IRQ_STATE_WAIT_RELEASE_OR_ACCEPT;
	spin_unlock_irqrestore(&gh_irq_lend_lock, flags);

	return gh_rm_vm_irq_lend(entry->vmid, virq, label, &entry->virq_handle);
}
EXPORT_SYMBOL(gh_irq_lend_v2);

/**
 * gh_irq_lend: Lend a hardware interrupt to another VM
 * @label: vIRQ high-level label
 * @name: VM name to send interrupt to
 * @irq: Linux IRQ number to lend
 * @cb_handle: callback to invoke when other VM release or accept the interrupt
 * @data: Argument to pass to cb_handle
 *
 * Returns 0 on success also the handle corresponding to Linux IRQ#.
 * Returns < 0 on error
 */
int gh_irq_lend(enum gh_irq_label label, enum gh_vm_names name,
		int irq, gh_irq_handle_fn cb_handle, void *data)
{
	struct gh_irq_entry *entry;

	if (label >= GH_IRQ_LABEL_MAX || !cb_handle)
		return -EINVAL;

	entry = &gh_irq_entries[label];
	entry->handle = cb_handle;

	return 0;
}
EXPORT_SYMBOL(gh_irq_lend);

/**
 * gh_irq_lend_notify: Pass the irq handle to other VM for accept
 * @label: vIRQ high-level label
 *
 * Returns 0 on success, < 0 on error
 */
int gh_irq_lend_notify(enum gh_irq_label label)
{
	struct gh_irq_entry *entry;

	if (label >= GH_IRQ_LABEL_MAX)
		return -EINVAL;

	entry = &gh_irq_entries[label];
	if (entry->state == GH_IRQ_STATE_NONE)
		return -EINVAL;

	return gh_rm_vm_irq_lend_notify(entry->vmid, entry->virq_handle);
}
EXPORT_SYMBOL(gh_irq_lend_notify);

/**
 * gh_irq_reclaim: Reclaim a hardware interrupt after other VM
 * has released.
 * @label: vIRQ high-level label
 *
 * This function should be called inside or after on_release()
 * callback from gh_irq_lend.
 * This function is not thread-safe. Do not race with another gh_irq_reclaim
 * with same label
 */
int gh_irq_reclaim(enum gh_irq_label label)
{
	int ret;
	struct gh_irq_entry *entry;

	if (label >= GH_IRQ_LABEL_MAX)
		return -EINVAL;

	entry = &gh_irq_entries[label];

	if (entry->state != GH_IRQ_STATE_WAIT_RELEASE_OR_ACCEPT &&
			(entry->state != GH_IRQ_STATE_RELEASED))
		return -EINVAL;

	ret = gh_rm_vm_irq_reclaim(entry->virq_handle);
	if (!ret)
		entry->state = GH_IRQ_STATE_NONE;
	return ret;
}
EXPORT_SYMBOL(gh_irq_reclaim);

/**
 * gh_irq_wait_for_lend_v2: Register to claim a lent interrupt from another VM
 * @label: vIRQ high-level label
 * @name: Lender's VM name. If don't care, then use GH_VM_MAX
 * @on_lend: callback to invoke when other VM lends the interrupt
 * @data: Argument to pass to on_lend
 */
int gh_irq_wait_for_lend_v2(enum gh_irq_label label, enum gh_vm_names name,
			 gh_irq_handle_fn_v2 on_lend, void *data)
{
	unsigned long flags;
	struct gh_irq_entry *entry;

	if (label >= GH_IRQ_LABEL_MAX || !on_lend)
		return -EINVAL;

	entry = &gh_irq_entries[label];

	spin_lock_irqsave(&gh_irq_lend_lock, flags);
	if (entry->state != GH_IRQ_STATE_NONE) {
		spin_unlock_irqrestore(&gh_irq_lend_lock, flags);
		return -EINVAL;
	}

	entry->vm_name = name;
	entry->v2_handle = on_lend;
	entry->data = data;
	entry->state = GH_IRQ_STATE_WAIT_LEND;
	spin_unlock_irqrestore(&gh_irq_lend_lock, flags);

	return 0;
}
EXPORT_SYMBOL(gh_irq_wait_for_lend_v2);

/**
 * gh_irq_wait_lend: Register to claim a lent interrupt from another VM
 * @label: vIRQ high-level label
 * @name: Lender's VM name. If don't care, then use GH_VM_MAX
 * @on_lend: callback to invoke when other VM lends the interrupt
 * @data: Argument to pass to on_lend
 */
int gh_irq_wait_for_lend(enum gh_irq_label label, enum gh_vm_names name,
			 gh_irq_handle_fn on_lend, void *data)
{
	return 0;
}
EXPORT_SYMBOL(gh_irq_wait_for_lend);

/**
 * gh_irq_accept: Register to receive interrupts with a lent vIRQ
 * @label: vIRQ high-level label
 * @irq: Linux IRQ# to associate vIRQ with. If don't care, use -1
 * @type: IRQ flags to use when allowing RM to choose the IRQ. If irq parameter
 *        is specified, then type is unused.
 *
 * Returns the Linux IRQ# that vIRQ was registered to on success.
 * Returns <0 on error
 * This function is not thread-safe w.r.t. IRQ lend state. Do not race with
 * gh_irq_release or another gh_irq_accept with same label.
 */
int gh_irq_accept(enum gh_irq_label label, int irq, int type)
{
	struct gh_irq_entry *entry;
	int virq;

	if (label >= GH_IRQ_LABEL_MAX)
		return -EINVAL;

	entry = &gh_irq_entries[label];

	if (entry->state != GH_IRQ_STATE_LENT)
		return -EINVAL;

	if (irq != -1) {
		if (gh_rm_irq_to_virq(irq, &virq))
			return -EINVAL;
	} else
		virq = -1;

	virq = gh_rm_vm_irq_accept(entry->virq_handle, virq);
	if (virq < 0)
		return virq;

	if (irq == -1)
		irq = gh_rm_virq_to_irq(virq, type);

	entry->state = GH_IRQ_STATE_ACCEPTED;
	return irq;
}
EXPORT_SYMBOL(gh_irq_accept);

/**
 * gh_irq_accept_notify: Notify the lend vm (pvm) that IRQ is accepted
 * @label: vIRQ high-level label
 * @irq: Linux IRQ# to associate vIRQ with. If don't care, use -1
 *
 * Returns the Linux IRQ# that vIRQ was registered to on success.
 * Returns <0 on error
 * This function is not thread-safe w.r.t. IRQ lend state. Do not race with
 * gh_irq_release or another gh_irq_accept with same label.
 */
int gh_irq_accept_notify(enum gh_irq_label label)
{
	struct gh_irq_entry *entry;

	if (label >= GH_IRQ_LABEL_MAX)
		return -EINVAL;

	entry = &gh_irq_entries[label];

	if (entry->state != GH_IRQ_STATE_ACCEPTED)
		return -EINVAL;

	return gh_rm_vm_irq_accept_notify(entry->vmid,
					  entry->virq_handle);
}
EXPORT_SYMBOL(gh_irq_accept_notify);

/**
 * gh_irq_release: Release a lent interrupt
 * @label: vIRQ high-level label
 * This function is not thread-safe w.r.t. IRQ lend state. Do not race with
 * gh_irq_accept or another gh_irq_release with same label.
 */
int gh_irq_release(enum gh_irq_label label)
{
	int ret;
	struct gh_irq_entry *entry;

	if (label >= GH_IRQ_LABEL_MAX)
		return -EINVAL;

	entry = &gh_irq_entries[label];

	if (entry->state != GH_IRQ_STATE_ACCEPTED)
		return -EINVAL;

	ret = gh_rm_vm_irq_release(entry->virq_handle);
	if (!ret)
		entry->state = GH_IRQ_STATE_WAIT_LEND;
	return ret;
}
EXPORT_SYMBOL(gh_irq_release);

int gh_irq_release_notify(enum gh_irq_label label)
{
	struct gh_irq_entry *entry;

	if (label >= GH_IRQ_LABEL_MAX)
		return -EINVAL;

	entry = &gh_irq_entries[label];

	if (entry->state != GH_IRQ_STATE_ACCEPTED &&
			entry->state != GH_IRQ_STATE_WAIT_LEND)
		return -EINVAL;

	return gh_rm_vm_irq_release_notify(entry->vmid,
					  entry->virq_handle);
}
EXPORT_SYMBOL(gh_irq_release_notify);

static int __init gh_irq_lend_init(void)
{
	int ret;

	ret = gh_rm_register_notifier(&gh_irq_lent_nb);
	if (ret)
		return ret;

	return gh_rm_register_notifier(&gh_irq_released_accepted_nb);
}
module_init(gh_irq_lend_init);

static void gh_irq_lend_exit(void)
{
	gh_rm_unregister_notifier(&gh_irq_lent_nb);
	gh_rm_unregister_notifier(&gh_irq_released_accepted_nb);
}
module_exit(gh_irq_lend_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Qualcomm Technologies, Inc. Gunyah IRQ Lending Library");
