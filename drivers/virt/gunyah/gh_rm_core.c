// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 *
 */

#include <linux/of.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/module.h>
#include <linux/of_irq.h>
#include <linux/kthread.h>
#include <linux/notifier.h>
#include <linux/irqdomain.h>
#include <linux/workqueue.h>
#include <linux/delay.h>
#include <linux/completion.h>
#include <linux/auxiliary_bus.h>
#include <linux/mod_devicetable.h>
#include <linux/sched.h>

#include <linux/gunyah/gh_dbl.h>
#include <linux/gunyah/gh_msgq.h>
#include <linux/gunyah/gh_common.h>
#include <linux/gunyah/gh_rm_drv.h>

#include "gh_rm_drv_private.h"

#define GH_RM_MAX_NUM_FRAGMENTS	62

#define GH_RM_NO_IRQ_ALLOC	-1

#define GH_RM_MAX_MSG_SIZE_BYTES \
	(GH_MSGQ_MAX_MSG_SIZE_BYTES - sizeof(struct gh_rm_rpc_hdr))

/**
 * struct gh_rm_connection - Represents a complete message from resource manager
 * @payload: Combined payload of all the fragments without any RPC headers
 * @size: Size of the payload.
 * @msg_id: Message ID from the header.
 * @ret: Linux return code, set in case there was an error processing the connection.
 * @type: GH_RM_RPC_TYPE_RPLY or GH_RM_RPC_TYPE_NOTIF.
 * @num_fragments: total number of fragments expected to be received for this connection.
 * @fragments_recieved: fragments received so far.
 * @rm_error: For request/reply sequences with standard replies.
 * @seq: Sequence ID for the main message.
 */
struct gh_rm_connection {
	void *payload;
	size_t size;
	u32 msg_id;
	int ret;
	u8 type;

	u8 num_fragments;
	u8 fragments_received;

	/* only for req/reply sequence */
	u32 rm_error;
	u16 seq;
	struct completion seq_done;
};

struct gh_rm_notif_validate {
	struct gh_rm_connection *conn;
	struct work_struct work;
};

const static struct {
	enum gh_vm_names val;
	const char *image_name;
	const char *vm_name;
} vm_name_map[] = {
	{GH_PRIMARY_VM, "pvm", ""},
	{GH_TRUSTED_VM, "trustedvm", "qcom,trustedvm"},
	{GH_CPUSYS_VM, "cpusys_vm", "qcom,cpusysvm"},
	{GH_OEM_VM, "oem_vm", "qcom,oemvm"},
	{GH_AUTO_VM, "autoghgvm", "qcom,autoghgvm"},
	{GH_AUTO_VM_LV, "autoghgvmlv", "qcom,autoghgvmlv"},
};

static gh_virtio_mmio_cb_t gh_virtio_mmio_fn;
static gh_wdog_manage_cb_t gh_wdog_manage_fn;
static gh_vcpu_affinity_set_cb_t gh_vcpu_affinity_set_fn;
static gh_vcpu_affinity_reset_cb_t gh_vcpu_affinity_reset_fn;
static gh_vpm_grp_set_cb_t gh_vpm_grp_set_fn;
static gh_vpm_grp_reset_cb_t gh_vpm_grp_reset_fn;
static gh_all_res_populated_cb_t gh_all_res_populated_fn;

static DEFINE_MUTEX(gh_virtio_mmio_fn_lock);
static DEFINE_IDR(gh_rm_call_idr);

static struct device_node *gh_rm_intc;
static struct irq_domain *gh_rm_irq_domain;
static u32 gh_rm_base_virq;

SRCU_NOTIFIER_HEAD_STATIC(gh_rm_notifier);

/* non-static: used by gh_rm_iface */
bool gh_rm_core_initialized;
struct gh_rm *rm;

static void gh_rm_get_svm_res_work_fn(struct work_struct *work);
static DECLARE_WORK(gh_rm_get_svm_res_work, gh_rm_get_svm_res_work_fn);

enum gh_vm_names gh_get_image_name(const char *str)
{
	int vmid;

	for (vmid = 0; vmid < ARRAY_SIZE(vm_name_map); ++vmid) {
		if (!strcmp(str, vm_name_map[vmid].image_name))
			return vm_name_map[vmid].val;
	}
	pr_err("Can find vm index for image name %s\n", str);
	return GH_VM_MAX;
}
EXPORT_SYMBOL(gh_get_image_name);

enum gh_vm_names gh_get_vm_name(const char *str)
{
	int vmid;

	for (vmid = 0; vmid < ARRAY_SIZE(vm_name_map); ++vmid) {
		if (!strcmp(str, vm_name_map[vmid].vm_name))
			return vm_name_map[vmid].val;
	}
	pr_err("Can find vm index for vm name %s\n", str);
	return GH_VM_MAX;
}
EXPORT_SYMBOL(gh_get_vm_name);

int gh_rm_register_notifier(struct notifier_block *nb)
{
	return srcu_notifier_chain_register(&gh_rm_notifier, nb);
}
EXPORT_SYMBOL(gh_rm_register_notifier);

int gh_rm_unregister_notifier(struct notifier_block *nb)
{
	return srcu_notifier_chain_unregister(&gh_rm_notifier, nb);
}
EXPORT_SYMBOL(gh_rm_unregister_notifier);

static int gh_rm_core_notifier_call(struct notifier_block *nb, unsigned long action,
								void *msg)
{
	return srcu_notifier_call_chain(&gh_rm_notifier, action, msg);
}

/**
 * gh_rm_virq_to_irq: Get a Linux IRQ from a Gunyah-compatible vIRQ
 * @virq: Gunyah-compatible vIRQ
 * @type: IRQ trigger type (IRQ_TYPE_EDGE_RISING)
 *
 * Returns the mapped Linux IRQ# at Gunyah's IRQ domain (i.e. GIC SPI)
 */
int gh_rm_virq_to_irq(u32 virq, u32 type)
{
	return gh_get_irq(virq, type, of_node_to_fwnode(gh_rm_intc));
}
EXPORT_SYMBOL(gh_rm_virq_to_irq);

/**
 * gh_rm_irq_to_virq: Get a Gunyah-compatible vIRQ from a Linux IRQ
 * @irq: Linux-assigned IRQ#
 * @virq: out value where Gunyah-compatible vIRQ is stored
 *
 * Returns 0 upon success, -EINVAL if the Linux IRQ could not be mapped to
 * a Gunyah vIRQ (i.e., the IRQ does not correspond to any GIC-level IRQ)
 */
int gh_rm_irq_to_virq(int irq, u32 *virq)
{
	struct irq_data *irq_data;

	irq_data = irq_domain_get_irq_data(gh_rm_irq_domain, irq);
	if (!irq_data)
		return -EINVAL;

	if (virq)
		*virq = irq_data->hwirq;

	return 0;
}
EXPORT_SYMBOL(gh_rm_irq_to_virq);

static int gh_rm_get_irq(struct gh_vm_get_hyp_res_resp_entry *res_entry)
{
	int ret, virq = res_entry->virq;

	/* For resources, such as DBL source, there's no IRQ. The virq_handle
	 * wouldn't be defined for such cases. Hence ignore such cases
	 */
	if ((!res_entry->virq_handle && !virq) || virq == U32_MAX)
		return 0;

	/* Allocate and bind a new IRQ if RM-VM hasn't already done already */
	if (virq == GH_RM_NO_IRQ_ALLOC) {
		ret = virq = gh_get_virq(gh_rm_base_virq, virq);
		if (ret < 0)
			return ret;

		/* Bind the vIRQ */
		ret = gh_rm_vm_irq_accept(res_entry->virq_handle, virq);
		if (ret < 0) {
			pr_err("%s: IRQ accept failed: %d\n",
				__func__, ret);
			gh_put_virq(virq);
			return ret;
		}
	}

	return gh_rm_virq_to_irq(virq, IRQ_TYPE_EDGE_RISING);
}

/**
 * gh_rm_get_vm_id_info: Query Resource Manager VM to get vm identification info.
 * @vmid: The vmid of VM whose id information needs to be queried.
 *
 * The function encodes the error codes via ERR_PTR. Hence, the caller is
 * responsible to check it with IS_ERR_OR_NULL().
 */
int gh_rm_get_vm_id_info(gh_vmid_t vmid)
{
	struct gh_vm_get_id_resp_entry *id_entries = NULL, *entry;
	struct gh_vm_property vm_prop = {0};
	void *info = NULL;
	int ret = 0;
	u32 n_id, i;
	enum gh_vm_names vm_name;

	id_entries = gh_rm_vm_get_id(vmid, &n_id);
	if (IS_ERR_OR_NULL(id_entries))
		return PTR_ERR(id_entries);

	pr_debug("%s: %d Info are associated with vmid %d\n",
		 __func__, n_id, vmid);

	entry = id_entries;
	for (i = 0; i < n_id; i++) {
		pr_debug("%s: idx:%d id_type %d reserved %d id_size %d\n",
			__func__, i,
			entry->id_type,
			entry->reserved,
			entry->id_size);

		info = kzalloc(entry->id_size % 4 ? entry->id_size + 1 :
							entry->id_size,
			GFP_KERNEL);

		if (!info) {
			ret = -ENOMEM;
			break;
		}

		memcpy(info, entry->id_info, entry->id_size);

		pr_debug("%s: idx:%d id_info %s\n", __func__, i, info);
		switch (entry->id_type) {
		case GH_RM_ID_TYPE_GUID:
			vm_prop.guid = info;
		break;
		case GH_RM_ID_TYPE_URI:
			vm_prop.uri = info;
		break;
		case GH_RM_ID_TYPE_NAME:
			vm_prop.name = info;
		break;
		case GH_RM_ID_TYPE_SIGN_AUTH:
			vm_prop.sign_auth = info;
		break;
		default:
			pr_err("%s: Unknown id type: %u\n",
				__func__, entry->id_type);
			ret = -EINVAL;
			kfree(info);
		}
		entry = (void *)entry + sizeof(*entry) +
			     round_up(entry->id_size, 4);
	}

	if (!ret) {
		vm_prop.vmid = vmid;
		if (vm_prop.name)
			vm_name = gh_get_vm_name(vm_prop.name);
		else
			vm_name = GH_VM_MAX;
		if (vm_name == GH_VM_MAX) {
			pr_err("Invalid vm name %s of VMID %d\n", vm_prop.name,
			       vmid);
			ret = -EINVAL;
		} else {
			ret = gh_update_vm_prop_table(vm_name, &vm_prop);
		}
	}

	kfree(id_entries);
	return ret;
}
EXPORT_SYMBOL(gh_rm_get_vm_id_info);

/**
 * gh_rm_populate_hyp_res: Query Resource Manager VM to get hyp resources.
 * @vmid: The vmid of resources to be queried.
 *
 * The function encodes the error codes via ERR_PTR. Hence, the caller is
 * responsible to check it with IS_ERR_OR_NULL().
 */
int gh_rm_populate_hyp_res(gh_vmid_t vmid, const char *vm_name)
{
	struct gh_vm_get_hyp_res_resp_entry *res_entries = NULL;
	int linux_irq, ret = 0;
	gh_capid_t cap_id;
	gh_label_t label;
	u32 n_res, i;
	u64 base = 0, size = 0;

	res_entries = gh_rm_vm_get_hyp_res(vmid, &n_res);
	if (IS_ERR_OR_NULL(res_entries))
		return PTR_ERR(res_entries);

	pr_debug("%s: %d Resources are associated with vmid %d\n",
		 __func__, n_res, vmid);

	/* Need polulate VCPU first to know if VM support proxy scheduling */
	for (i = 0; i < n_res; i++) {
		if (res_entries[i].res_type == GH_RM_RES_TYPE_VCPU) {
			ret = linux_irq = gh_rm_get_irq(&res_entries[i]);
			if (ret < 0)
				goto out;

			cap_id = (u64) res_entries[i].cap_id_high << 32 |
					res_entries[i].cap_id_low;
			label = res_entries[i].resource_label;
			if (gh_vcpu_affinity_set_fn)
				do {
					ret = (*gh_vcpu_affinity_set_fn)(
						vmid, label, cap_id, linux_irq);
				} while (ret == -EAGAIN);
			if (ret < 0)
				goto out;
		}
	}

	for (i = 0; i < n_res; i++) {
		pr_debug("%s: idx:%d res_entries.res_type = 0x%x, res_entries.partner_vmid = 0x%x, res_entries.resource_handle = 0x%x, res_entries.resource_label = 0x%x, res_entries.cap_id_low = 0x%x, res_entries.cap_id_high = 0x%x, res_entries.virq_handle = 0x%x, res_entries.virq = 0x%x res_entries.base_high = 0x%x, res_entries.base_low = 0x%x, res_entries.size_high = 0x%x, res_entries.size_low = 0x%x\n",
			__func__, i,
			res_entries[i].res_type,
			res_entries[i].partner_vmid,
			res_entries[i].resource_handle,
			res_entries[i].resource_label,
			res_entries[i].cap_id_low,
			res_entries[i].cap_id_high,
			res_entries[i].virq_handle,
			res_entries[i].virq,
			res_entries[i].base_high,
			res_entries[i].base_low,
			res_entries[i].size_high,
			res_entries[i].size_low);

		ret = linux_irq = gh_rm_get_irq(&res_entries[i]);
		if (ret < 0)
			goto out;

		cap_id = (u64) res_entries[i].cap_id_high << 32 |
				res_entries[i].cap_id_low;
		base = (u64) res_entries[i].base_high << 32 |
				res_entries[i].base_low;
		size = (u64) res_entries[i].size_high << 32 |
				res_entries[i].size_low;
		label = res_entries[i].resource_label;

		/* Populate MessageQ, DBL and vCPUs cap tables */
		do {
			switch (res_entries[i].res_type) {
			case GH_RM_RES_TYPE_MQ_TX:
				ret = gh_msgq_populate_cap_info(label, cap_id,
					GH_MSGQ_DIRECTION_TX, linux_irq);
				break;
			case GH_RM_RES_TYPE_MQ_RX:
				ret = gh_msgq_populate_cap_info(label, cap_id,
					GH_MSGQ_DIRECTION_RX, linux_irq);
				break;
			case GH_RM_RES_TYPE_VCPU:
			/* Already populate VCPU resource */
				break;

			case GH_RM_RES_TYPE_DB_TX:
				ret = gh_dbl_populate_cap_info(label, cap_id,
					GH_MSGQ_DIRECTION_TX, linux_irq);
				break;
			case GH_RM_RES_TYPE_DB_RX:
				ret = gh_dbl_populate_cap_info(label, cap_id,
					GH_MSGQ_DIRECTION_RX, linux_irq);
				break;
			case GH_RM_RES_TYPE_VPMGRP:
				if (gh_vpm_grp_set_fn)
					ret = (*gh_vpm_grp_set_fn)(vmid, cap_id, linux_irq);
				break;
			case GH_RM_RES_TYPE_VIRTIO_MMIO:
				mutex_lock(&gh_virtio_mmio_fn_lock);
				if (!gh_virtio_mmio_fn) {
					mutex_unlock(&gh_virtio_mmio_fn_lock);
					break;
				}

				ret = (*gh_virtio_mmio_fn)(vmid, vm_name, label,
						cap_id, linux_irq, base, size);
				mutex_unlock(&gh_virtio_mmio_fn_lock);
				break;
			case GH_RM_RES_TYPE_WATCHDOG:
				if (gh_wdog_manage_fn)
					ret = (*gh_wdog_manage_fn)(vmid, cap_id, true);
				break;
			default:
				pr_err("%s: Unknown resource type: %u\n",
					__func__, res_entries[i].res_type);
				ret = -EINVAL;
			}
		} while (ret == -EAGAIN);

		if (ret < 0)
			goto out;
	}

	if (gh_all_res_populated_fn)
		(*gh_all_res_populated_fn)(vmid, true);
out:
	kfree(res_entries);
	return ret;
}
EXPORT_SYMBOL(gh_rm_populate_hyp_res);

static void
gh_rm_put_irq(struct gh_vm_get_hyp_res_resp_entry *res_entry, int irq)
{
	if (!gh_put_irq(irq))
		gh_rm_vm_irq_release(res_entry->virq_handle);

}

/**
 * gh_rm_unpopulate_hyp_res: Unpopulate the resources that we got from
 *				gh_rm_populate_hyp_res().
 * @vmid: The vmid of resources to be queried.
 * @vm_name: The name of the VM
 *
 * Returns 0 on success and a negative error code upon failure.
 */
int gh_rm_unpopulate_hyp_res(gh_vmid_t vmid, const char *vm_name)
{
	struct gh_vm_get_hyp_res_resp_entry *res_entries = NULL;
	gh_label_t label;
	u32 n_res, i;
	int ret = 0, irq = -1;
	gh_capid_t cap_id;

	res_entries = gh_rm_vm_get_hyp_res(vmid, &n_res);
	if (IS_ERR_OR_NULL(res_entries))
		return PTR_ERR(res_entries);

	for (i = 0; i < n_res; i++) {
		label = res_entries[i].resource_label;
		cap_id = (u64) res_entries[i].cap_id_high << 32 |
				res_entries[i].cap_id_low;

		switch (res_entries[i].res_type) {
		case GH_RM_RES_TYPE_MQ_TX:
			ret = gh_msgq_reset_cap_info(label,
						GH_MSGQ_DIRECTION_TX, &irq);
			break;
		case GH_RM_RES_TYPE_MQ_RX:
			ret = gh_msgq_reset_cap_info(label,
						GH_MSGQ_DIRECTION_RX, &irq);
			break;
		case GH_RM_RES_TYPE_DB_TX:
			ret = gh_dbl_reset_cap_info(label,
						GH_RM_RES_TYPE_DB_TX, &irq);
			break;
		case GH_RM_RES_TYPE_DB_RX:
			ret = gh_dbl_reset_cap_info(label,
						GH_RM_RES_TYPE_DB_RX, &irq);
			break;
		case GH_RM_RES_TYPE_VCPU:
			if (gh_vcpu_affinity_reset_fn)
				ret = (*gh_vcpu_affinity_reset_fn)(vmid,
							label, cap_id, &irq);
			break;
		case GH_RM_RES_TYPE_VIRTIO_MMIO:
			/* Virtio cleanup is handled in gh_virtio_mmio_exit() */
			break;
		case GH_RM_RES_TYPE_VPMGRP:
			if (gh_vpm_grp_reset_fn)
				ret = (*gh_vpm_grp_reset_fn)(vmid, &irq);
			break;
		case GH_RM_RES_TYPE_WATCHDOG:
			if (gh_wdog_manage_fn)
				ret = (*gh_wdog_manage_fn)(vmid, cap_id, false);
			break;
		default:
			pr_err("%s: Unknown resource type: %u\n",
				__func__, res_entries[i].res_type);
			ret = -EINVAL;
		}

		if (ret < 0)
			goto out;

		if (irq >= 0)
			gh_rm_put_irq(&res_entries[i], irq);
	}

	if (gh_all_res_populated_fn)
		(*gh_all_res_populated_fn)(vmid, false);
out:
	kfree(res_entries);
	return ret;
}
EXPORT_SYMBOL(gh_rm_unpopulate_hyp_res);

/**
 * gh_rm_set_virtio_mmio_cb: Set callback that handles virtio MMIO resource
 * @fnptr: Pointer to callback function
 *
 * gh_rm_populate_hyp_res() queries RM-VM for all resources assigned to a VM and
 * as part of that response RM-VM will indicate resources assigned exclusively
 * to handle virtio communication between the two VMs. @fnptr callback is
 * invoked providing details of the virtio resource allocated for a particular
 * virtio device. @fnptr is expected to initialize additional state based on the
 * information provided.
 *
 * This function returns these values:
 *	0	-> indicates success
 *	-EINVAL -> Indicates invalid input argument
 *	-EBUSY	-> Indicates that a callback is already set
 */
int gh_rm_set_virtio_mmio_cb(gh_virtio_mmio_cb_t fnptr)
{
	if (!fnptr)
		return -EINVAL;

	mutex_lock(&gh_virtio_mmio_fn_lock);
	if (gh_virtio_mmio_fn) {
		mutex_unlock(&gh_virtio_mmio_fn_lock);
		return -EBUSY;
	}

	gh_virtio_mmio_fn = fnptr;
	mutex_unlock(&gh_virtio_mmio_fn_lock);

	return 0;
}
EXPORT_SYMBOL(gh_rm_set_virtio_mmio_cb);

/**
 * gh_rm_unset_virtio_mmio_cb: Unset callback that handles virtio MMIO resource
 */
void gh_rm_unset_virtio_mmio_cb(void)
{
	mutex_lock(&gh_virtio_mmio_fn_lock);
	gh_virtio_mmio_fn = NULL;
	mutex_unlock(&gh_virtio_mmio_fn_lock);
}
EXPORT_SYMBOL(gh_rm_unset_virtio_mmio_cb);

/**
 * gh_rm_set_wdog_manage_cb: Set callback that handles wdog resource
 * @fnptr: Pointer to callback function
 *
 * @fnptr callback is invoked providing details of the wdog resource.
 *
 * This function returns these values:
 *	0	-> indicates success
 *	-EINVAL -> Indicates invalid input argument
 *	-EBUSY	-> Indicates that a callback is already set
 */
int gh_rm_set_wdog_manage_cb(gh_wdog_manage_cb_t fnptr)
{
	if (!fnptr)
		return -EINVAL;

	if (gh_wdog_manage_fn)
		return -EBUSY;

	gh_wdog_manage_fn = fnptr;

	return 0;
}
EXPORT_SYMBOL(gh_rm_set_wdog_manage_cb);

/**
 * gh_rm_set_vcpu_affinity_cb: Set callback that handles vcpu affinity
 * @fnptr: Pointer to callback function
 *
 * @fnptr callback is invoked providing details of the vcpu resource.
 *
 * This function returns these values:
 *	0	-> indicates success
 *	-EINVAL -> Indicates invalid input argument
 *	-EBUSY	-> Indicates that a callback is already set
 */
int gh_rm_set_vcpu_affinity_cb(gh_vcpu_affinity_set_cb_t fnptr)
{
	if (!fnptr)
		return -EINVAL;

	if (gh_vcpu_affinity_set_fn)
		return -EBUSY;

	gh_vcpu_affinity_set_fn = fnptr;

	return 0;
}
EXPORT_SYMBOL(gh_rm_set_vcpu_affinity_cb);

/**
 * gh_rm_reset_vcpu_affinity_cb: Reset callback that handles vcpu affinity
 * @fnptr: Pointer to callback function
 *
 * @fnptr callback is invoked providing details of the vcpu resource.
 *
 * This function returns these values:
 *	0	-> indicates success
 *	-EINVAL -> Indicates invalid input argument
 *	-EBUSY	-> Indicates that a callback is already set
 */
int gh_rm_reset_vcpu_affinity_cb(gh_vcpu_affinity_reset_cb_t fnptr)
{
	if (!fnptr)
		return -EINVAL;

	if (gh_vcpu_affinity_reset_fn)
		return -EBUSY;

	gh_vcpu_affinity_reset_fn = fnptr;

	return 0;
}
EXPORT_SYMBOL(gh_rm_reset_vcpu_affinity_cb);

/**
 * gh_rm_set_vpm_grp_cb: Set callback that handles vpm grp state
 * @fnptr: Pointer to callback function
 *
 * @fnptr callback is invoked providing details of the vcpu grp state IRQ.
 *
 * This function returns these values:
 *	0	-> indicates success
 *	-EINVAL -> Indicates invalid input argument
 *	-EBUSY	-> Indicates that a callback is already set
 */
int gh_rm_set_vpm_grp_cb(gh_vpm_grp_set_cb_t fnptr)
{
	if (!fnptr)
		return -EINVAL;

	if (gh_vpm_grp_set_fn)
		return -EBUSY;

	gh_vpm_grp_set_fn = fnptr;

	return 0;
}
EXPORT_SYMBOL(gh_rm_set_vpm_grp_cb);

/**
 * gh_rm_reset_vpm_grp_cb: Reset callback that handles vpm grp state
 * @fnptr: Pointer to callback function
 *
 * @fnptr callback is invoked providing details of the vcpu grp state IRQ.
 *
 * This function returns these values:
 *	0	-> indicates success
 *	-EINVAL -> Indicates invalid input argument
 *	-EBUSY	-> Indicates that a callback is already set
 */
int gh_rm_reset_vpm_grp_cb(gh_vpm_grp_reset_cb_t fnptr)
{
	if (!fnptr)
		return -EINVAL;

	if (gh_vpm_grp_reset_fn)
		return -EBUSY;

	gh_vpm_grp_reset_fn = fnptr;

	return 0;
}
EXPORT_SYMBOL(gh_rm_reset_vpm_grp_cb);

/**
 * gh_rm_all_res_populated_cb: Set callback that handles all res populated
 * @fnptr: Pointer to callback function
 *
 * @fnptr callback is invoked after all resources are populated/un-pupulated.
 *
 * This function returns these values:
 *	0	-> indicates success
 *	-EINVAL -> Indicates invalid input argument
 *	-EBUSY	-> Indicates that a callback is already set
 */
int gh_rm_all_res_populated_cb(gh_all_res_populated_cb_t fnptr)
{
	if (!fnptr)
		return -EINVAL;

	if (gh_all_res_populated_fn)
		return -EBUSY;

	gh_all_res_populated_fn = fnptr;

	return 0;
}
EXPORT_SYMBOL(gh_rm_all_res_populated_cb);

static void gh_rm_get_svm_res_work_fn(struct work_struct *work)
{
	gh_vmid_t vmid;
	int ret;

	ret = ghd_rm_get_vmid(GH_PRIMARY_VM, &vmid);
	if (ret)
		pr_err("%s: Unable to get VMID for VM label %d\n",
						__func__, GH_PRIMARY_VM);
	else
		gh_rm_populate_hyp_res(vmid, NULL);
}

static int gh_vm_status_nb_handler(struct notifier_block *this,
					unsigned long cmd, void *data)
{
	struct gh_rm_notif_vm_status_payload *vm_status_payload = data;
	struct gh_vminfo vm_info = {0};
	enum gh_vm_names vm_name;
	u8 vm_status = vm_status_payload->vm_status;
	int ret;

	if (cmd != GH_RM_NOTIF_VM_STATUS)
		return NOTIFY_DONE;

	switch (vm_status) {
	case GH_RM_VM_STATUS_READY:
		pr_err("vm(%d) is ready\n", vm_status_payload->vmid);
		ret = gh_rm_get_vm_id_info(vm_status_payload->vmid);
		if (ret < 0) {
			pr_err("Failed to get vmid info for vmid = %d ret = %d\n",
				vm_status_payload->vmid, ret);
			return NOTIFY_DONE;
		}
		ret = gh_rm_get_vm_name(vm_status_payload->vmid, &vm_name);
		if (ret < 0) {
			pr_err("Failed to get vm name for vmid = %d ret = %d\n",
			       vm_status_payload->vmid, ret);
			return NOTIFY_DONE;
		}
		ret = gh_rm_get_vminfo(vm_name, &vm_info);
		if (ret < 0)
			pr_err("Failed to get vminfo of vmname = %s\n", vm_name);
		ret = gh_rm_populate_hyp_res(vm_status_payload->vmid,
					     vm_info.name);
		if (ret < 0) {
			pr_err("Failed to get hyp resources for vmid = %d vmname = %s ret = %d\n",
			       vm_status_payload->vmid, vm_name, ret);
			return NOTIFY_DONE;
		}
		break;
	case GH_RM_VM_STATUS_RUNNING:
		pr_err("vm(%d) started running\n", vm_status_payload->vmid);
		break;
	default:
		pr_err("Unknown notification receieved for vmid = %d vm_status = %d\n",
				vm_status_payload->vmid, vm_status);
	}

	return NOTIFY_DONE;
}


static struct notifier_block gh_vm_status_nb = {
	.notifier_call = gh_vm_status_nb_handler
};


static void gh_vm_check_peer(struct device *dev, struct device_node *rm_root)
{
	int peers_cnt, ret, i;
	const char **peers_array = NULL;
	const char *peer, *peer_data;
	gh_vmid_t vmid;
	enum gh_vm_names vm_name_index;
	struct gh_vminfo vm_info;
	uuid_t vm_guid;

	peers_cnt = of_property_count_strings(rm_root, "qcom,peers");
	if (!peers_cnt)
		return;

	peers_array = kcalloc(peers_cnt, sizeof(char *), GFP_KERNEL);
	if (!peers_array) {
		dev_err(dev, "Failed to allocate memory\n");
		return;
	}

	ret = of_property_read_string_array(rm_root, "qcom,peers", peers_array,
					    peers_cnt);
	if (ret < 0) {
		dev_err(dev, "Failed to find qcom,peers\n");
		goto out;
	}

	for (i = 0; i < peers_cnt; i++) {
		peer = peers_array[i];
		if (peer == NULL)
			continue;
		if (strnstr(peer, "vm-name:", strlen("vm-name:")) != NULL) {
			peer_data = peer + strlen("vm-name:");
			dev_dbg(dev, "Trying to lookup name %s\n", peer_data);
			ret = gh_rm_vm_lookup(GH_VM_LOOKUP_NAME, peer_data,
					      strlen(peer_data), &vmid);
		} else if (strnstr(peer, "vm-uri:", strlen("vm-uri:")) !=
			   NULL) {
			peer_data = peer + strlen("vm-uri:");
			dev_dbg(dev, "Trying to lookup uri %s\n", peer_data);
			ret = gh_rm_vm_lookup(GH_VM_LOOKUP_URI, peer_data,
					      strlen(peer_data), &vmid);
		} else if (strnstr(peer, "vm-guid:", strlen("vm-guid:")) !=
			   NULL) {
			peer_data = peer + strlen("vm-guid:");
			dev_dbg(dev, "Trying to lookup guid %s\n", peer_data);
			ret = uuid_parse(peer_data, &vm_guid);
			if (ret != 0)
				dev_err(dev, "Invalid GUID:%s\n",
					peer + strlen("vm-guid:"));
			else
				ret = gh_rm_vm_lookup(GH_VM_LOOKUP_GUID,
						      (char *)&vm_guid,
						      sizeof(vm_guid), &vmid);
		} else {
			dev_err(dev, "Unknown peer type:%s\n", peer);
			continue;
		}
		if (ret < 0) {
			dev_err(dev,
				"lookup %s failed, VM is not running ret=%d\n",
				peer, ret);
			continue;
		}
		ret = gh_rm_get_vm_id_info(vmid);
		if (ret < 0) {
			dev_err(dev,
				"Failed to get vmid info for vmid = %d ret = %d\n",
				vmid, ret);
			continue;
		}
		ret = gh_rm_get_vm_name(vmid, &vm_name_index);
		if (ret < 0) {
			dev_err(dev,
				"Failed to get vmid info for vmid = %d ret = %d\n",
				vmid, ret);
			continue;
		}
		gh_rm_get_vminfo(vm_name_index, &vm_info);
		ret = gh_rm_populate_hyp_res(vmid, vm_info.name);
		if (ret < 0) {
			dev_err(dev,
				"Failed to get hyp resources for vmid = %d ret = %d\n",
				vmid, ret);
			continue;
		}
	}
out:
	kfree(peers_array);
}

static int gh_vm_probe(struct device *dev, struct device_node *hyp_root)
{
	struct device_node *node;
	struct gh_vm_property temp_property = {0};
	int vmid, owner_vmid, ret;
	const char *vm_name;
	enum gh_vm_names vm_name_index;


	gh_init_vm_prop_table();

	node = of_find_compatible_node(hyp_root, NULL, "qcom,gunyah-vm-id-1.0");
	if (IS_ERR_OR_NULL(node)) {
		node = of_find_compatible_node(hyp_root, NULL, "qcom,haven-vm-id-1.0");
		if (IS_ERR_OR_NULL(node)) {
			dev_err(dev, "Could not find vm-id node\n");
			return -ENODEV;
		}
	}

	ret = of_property_read_u32(node, "qcom,vmid", &vmid);
	if (ret) {
		dev_err(dev, "Could not read vmid: %d\n", ret);
		return ret;
	}

	ret = of_property_read_u32(node, "qcom,owner-vmid", &owner_vmid);
	if (ret) {
		/* We must be GH_PRIMARY_VM */
		temp_property.vmid = vmid;
		gh_update_vm_prop_table(GH_PRIMARY_VM, &temp_property);
		gh_rm_core_initialized = true;
	} else {
		ret = of_property_read_string(node, "qcom,image-name",
					      &vm_name);
		if (ret) {
			/* Just for compatible, if image-name cannot be found */
			/* Assume we are trusted VM */
			dev_dbg(dev,
				"Could not find qcom,image-name assume we are trustedvm\n");
			vm_name_index = GH_TRUSTED_VM;
		} else {
			vm_name_index = gh_get_vm_name(vm_name);
			if (vm_name_index == GH_VM_MAX) {
				dev_dbg(dev,
					"Could not find vm_name:%s assume we are trustedvm\n",
					vm_name);
				vm_name_index = GH_TRUSTED_VM;
			} else {
				dev_dbg(dev, "VM name index is %d\n",
					vm_name_index);
			}
		}
		temp_property.vmid = vmid;
		gh_update_vm_prop_table(vm_name_index, &temp_property);
		temp_property.vmid = owner_vmid;
		gh_update_vm_prop_table(GH_PRIMARY_VM, &temp_property);

		/* check peer to see if any VM has been bootup */
		gh_vm_check_peer(dev, node);
		gh_rm_register_notifier(&gh_vm_status_nb);
		gh_rm_core_initialized = true;
		/* Query RM for available resources */
		schedule_work(&gh_rm_get_svm_res_work);
	}

	return 0;
}

static struct notifier_block gh_rm_core_notifier_blk = {
	.notifier_call = gh_rm_core_notifier_call,
};

static const struct auxiliary_device_id gh_rm_drv_id_table[] = {
	{ .name = "gunyah_rsc_mgr.gh_rm_core" },
	{ .name = "gunyah.gh_rm_core" },
	{ }
};

static int gh_rm_drv_probe(struct auxiliary_device *adev,
				const struct auxiliary_device_id *adev_id)
{
	struct device *dev = &adev->dev;
	struct device *rm_dev = adev->dev.parent;
	struct device_node *node = rm_dev->of_node;
	int ret;

	rm = rm_dev->driver_data;
	if (!rm)
		dev_err(dev, "Failed to get the rm pointer\n");

	if (of_property_read_u32(node, "qcom,free-irq-start",
				 &gh_rm_base_virq)) {
		dev_err(dev, "Failed to get the vIRQ base\n");
		return -ENXIO;
	}

	gh_rm_intc = of_irq_find_parent(node);
	if (!gh_rm_intc) {
		dev_err(dev, "Failed to get the IRQ parent node\n");
		return -ENXIO;
	}
	gh_rm_irq_domain = irq_find_host(gh_rm_intc);
	if (!gh_rm_irq_domain) {
		dev_err(dev, "Failed to get IRQ domain associated with RM\n");
		return -ENXIO;
	}

	ret = gh_rm_notifier_register(rm, &gh_rm_core_notifier_blk);
	if (ret) {
		dev_err(dev, "Failed to register to RM notifier %d\n", ret);
		return ret;
	}

	/* Probe the vmid */
	ret = gh_vm_probe(dev, node->parent);
	if (ret < 0 && ret != -ENODEV)
		return ret;

	return 0;

}

static void gh_rm_drv_remove(struct auxiliary_device *adev)
{
	gh_rm_notifier_unregister(rm, &gh_rm_core_notifier_blk);
	idr_destroy(&gh_rm_call_idr);
}

static struct auxiliary_driver gh_rm_driver = {
	.name = "gh_rm_driver",
	.probe = gh_rm_drv_probe,
	.remove = gh_rm_drv_remove,
	.id_table = gh_rm_drv_id_table
};

module_auxiliary_driver(gh_rm_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Qualcomm Technologies, Inc. Gunyah Resource Mgr. Driver");
