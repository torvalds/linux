// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/gunyah_rsc_mgr.h>
#include "rsc_mgr.h"

/* Message IDs: Memory Management */
#define GH_RM_RPC_MEM_LEND			0x51000012
#define GH_RM_RPC_MEM_SHARE			0x51000013
#define GH_RM_RPC_MEM_RECLAIM			0x51000015
#define GH_RM_RPC_MEM_APPEND			0x51000018

/* Message IDs: VM Management */
#define GH_RM_RPC_VM_ALLOC_VMID			0x56000001
#define GH_RM_RPC_VM_DEALLOC_VMID		0x56000002
#define GH_RM_RPC_VM_START			0x56000004
#define GH_RM_RPC_VM_STOP			0x56000005
#define GH_RM_RPC_VM_RESET			0x56000006
#define GH_RM_RPC_VM_CONFIG_IMAGE		0x56000009
#define GH_RM_RPC_VM_INIT			0x5600000B
#define GH_RM_RPC_VM_GET_HYP_RESOURCES		0x56000020
#define GH_RM_RPC_VM_GET_VMID			0x56000024
#define GH_RM_RPC_VM_SET_FIRMWARE_MEM		0x56000032

struct gh_rm_vm_common_vmid_req {
	__le16 vmid;
	__le16 _padding;
} __packed;

/* Call: MEM_LEND, MEM_SHARE */
#define GH_MEM_SHARE_REQ_FLAGS_APPEND		BIT(1)

struct gh_rm_mem_share_req_header {
	u8 mem_type;
	u8 _padding0;
	u8 flags;
	u8 _padding1;
	__le32 label;
} __packed;

struct gh_rm_mem_share_req_acl_section {
	__le32 n_entries;
	struct gh_rm_mem_acl_entry entries[];
};

struct gh_rm_mem_share_req_mem_section {
	__le16 n_entries;
	__le16 _padding;
	struct gh_rm_mem_entry entries[];
};

/* Call: MEM_RELEASE */
struct gh_rm_mem_release_req {
	__le32 mem_handle;
	u8 flags; /* currently not used */
	u8 _padding0;
	__le16 _padding1;
} __packed;

/* Call: MEM_APPEND */
#define GH_MEM_APPEND_REQ_FLAGS_END		BIT(0)

struct gh_rm_mem_append_req_header {
	__le32 mem_handle;
	u8 flags;
	u8 _padding0;
	__le16 _padding1;
} __packed;

/* Call: VM_ALLOC */
struct gh_rm_vm_alloc_vmid_resp {
	__le16 vmid;
	__le16 _padding;
} __packed;

/* Call: VM_STOP */
#define GH_RM_VM_STOP_FLAG_FORCE_STOP		BIT(0)

#define GH_RM_VM_STOP_REASON_FORCE_STOP		3

struct gh_rm_vm_stop_req {
	__le16 vmid;
	u8 flags;
	u8 _padding;
	__le32 stop_reason;
} __packed;

/* Call: VM_CONFIG_IMAGE */
struct gh_rm_vm_config_image_req {
	__le16 vmid;
	__le16 auth_mech;
	__le32 mem_handle;
	__le64 image_offset;
	__le64 image_size;
	__le64 dtb_offset;
	__le64 dtb_size;
} __packed;

/* Call: VM_SET_FIRMWARE_MEM */
struct gh_vm_set_firmware_mem_req {
	__le16 vmid;
	__le16 reserved;
	__le32 mem_handle;
	__le64 fw_offset;
	__le64 fw_size;
} __packed;

#define GH_RM_MAX_MEM_ENTRIES	512

/*
 * Several RM calls take only a VMID as a parameter and give only standard
 * response back. Deduplicate boilerplate code by using this common call.
 */
static int gh_rm_common_vmid_call(struct gh_rm *rm, u32 message_id, u16 vmid)
{
	struct gh_rm_vm_common_vmid_req req_payload = {
		.vmid = cpu_to_le16(vmid),
	};

	return gh_rm_call(rm, message_id, &req_payload, sizeof(req_payload), NULL, NULL);
}

static int _gh_rm_mem_append(struct gh_rm *rm, u32 mem_handle, bool end_append,
			struct gh_rm_mem_entry *mem_entries, size_t n_mem_entries)
{
	struct gh_rm_mem_share_req_mem_section *mem_section;
	struct gh_rm_mem_append_req_header *req_header;
	size_t msg_size = 0;
	void *msg;
	int ret;

	msg_size += sizeof(struct gh_rm_mem_append_req_header);
	msg_size += struct_size(mem_section, entries, n_mem_entries);

	msg = kzalloc(msg_size, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	req_header = msg;
	mem_section = (void *)(req_header + 1);

	req_header->mem_handle = cpu_to_le32(mem_handle);
	if (end_append)
		req_header->flags |= GH_MEM_APPEND_REQ_FLAGS_END;

	mem_section->n_entries = cpu_to_le16(n_mem_entries);
	memcpy(mem_section->entries, mem_entries, sizeof(*mem_entries) * n_mem_entries);

	ret = gh_rm_call(rm, GH_RM_RPC_MEM_APPEND, msg, msg_size, NULL, NULL);
	kfree(msg);

	return ret;
}

static int gh_rm_mem_append(struct gh_rm *rm, u32 mem_handle,
			struct gh_rm_mem_entry *mem_entries, size_t n_mem_entries)
{
	bool end_append;
	int ret = 0;
	size_t n;

	while (n_mem_entries) {
		if (n_mem_entries > GH_RM_MAX_MEM_ENTRIES) {
			end_append = false;
			n = GH_RM_MAX_MEM_ENTRIES;
		} else {
			end_append = true;
			n = n_mem_entries;
		}

		ret = _gh_rm_mem_append(rm, mem_handle, end_append, mem_entries, n);
		if (ret)
			break;

		mem_entries += n;
		n_mem_entries -= n;
	}

	return ret;
}

static int gh_rm_mem_lend_common(struct gh_rm *rm, u32 message_id, struct gh_rm_mem_parcel *p)
{
	size_t msg_size = 0, initial_mem_entries = p->n_mem_entries, resp_size;
	size_t acl_section_size, mem_section_size;
	struct gh_rm_mem_share_req_acl_section *acl_section;
	struct gh_rm_mem_share_req_mem_section *mem_section;
	struct gh_rm_mem_share_req_header *req_header;
	u32 *attr_section;
	__le32 *resp;
	void *msg;
	int ret;

	if (!p->acl_entries || !p->n_acl_entries || !p->mem_entries || !p->n_mem_entries ||
	    p->n_acl_entries > U8_MAX || p->mem_handle != GH_MEM_HANDLE_INVAL)
		return -EINVAL;

	if (initial_mem_entries > GH_RM_MAX_MEM_ENTRIES)
		initial_mem_entries = GH_RM_MAX_MEM_ENTRIES;

	acl_section_size = struct_size(acl_section, entries, p->n_acl_entries);
	mem_section_size = struct_size(mem_section, entries, initial_mem_entries);
	/* The format of the message goes:
	 * request header
	 * ACL entries (which VMs get what kind of access to this memory parcel)
	 * Memory entries (list of memory regions to share)
	 * Memory attributes (currently unused, we'll hard-code the size to 0)
	 */
	msg_size += sizeof(struct gh_rm_mem_share_req_header);
	msg_size += acl_section_size;
	msg_size += mem_section_size;
	msg_size += sizeof(u32); /* for memory attributes, currently unused */

	msg = kzalloc(msg_size, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	ret = gh_rm_platform_pre_mem_share(rm, p);
	if (ret) {
		kfree(msg);
		return ret;
	}

	req_header = msg;
	acl_section = (void *)req_header + sizeof(*req_header);
	mem_section = (void *)acl_section + acl_section_size;
	attr_section = (void *)mem_section + mem_section_size;

	req_header->mem_type = p->mem_type;
	if (initial_mem_entries != p->n_mem_entries)
		req_header->flags |= GH_MEM_SHARE_REQ_FLAGS_APPEND;
	req_header->label = cpu_to_le32(p->label);

	acl_section->n_entries = cpu_to_le32(p->n_acl_entries);
	memcpy(acl_section->entries, p->acl_entries,
		flex_array_size(acl_section, entries, p->n_acl_entries));

	mem_section->n_entries = cpu_to_le16(initial_mem_entries);
	memcpy(mem_section->entries, p->mem_entries,
		flex_array_size(mem_section, entries, initial_mem_entries));

	/* Set n_entries for memory attribute section to 0 */
	*attr_section = 0;

	ret = gh_rm_call(rm, message_id, msg, msg_size, (void **)&resp, &resp_size);
	kfree(msg);

	if (ret) {
		gh_rm_platform_post_mem_reclaim(rm, p);
		return ret;
	}

	p->mem_handle = le32_to_cpu(*resp);
	kfree(resp);

	if (initial_mem_entries != p->n_mem_entries) {
		ret = gh_rm_mem_append(rm, p->mem_handle,
					&p->mem_entries[initial_mem_entries],
					p->n_mem_entries - initial_mem_entries);
		if (ret) {
			gh_rm_mem_reclaim(rm, p);
			p->mem_handle = GH_MEM_HANDLE_INVAL;
		}
	}

	return ret;
}

/**
 * gh_rm_mem_lend() - Lend memory to other virtual machines.
 * @rm: Handle to a Gunyah resource manager
 * @parcel: Information about the memory to be lent.
 *
 * Lending removes Linux's access to the memory while the memory parcel is lent.
 */
int gh_rm_mem_lend(struct gh_rm *rm, struct gh_rm_mem_parcel *parcel)
{
	return gh_rm_mem_lend_common(rm, GH_RM_RPC_MEM_LEND, parcel);
}


/**
 * gh_rm_mem_share() - Share memory with other virtual machines.
 * @rm: Handle to a Gunyah resource manager
 * @parcel: Information about the memory to be shared.
 *
 * Sharing keeps Linux's access to the memory while the memory parcel is shared.
 */
int gh_rm_mem_share(struct gh_rm *rm, struct gh_rm_mem_parcel *parcel)
{
	return gh_rm_mem_lend_common(rm, GH_RM_RPC_MEM_SHARE, parcel);
}

/**
 * gh_rm_mem_reclaim() - Reclaim a memory parcel
 * @rm: Handle to a Gunyah resource manager
 * @parcel: Information about the memory to be reclaimed.
 *
 * RM maps the associated memory back into the stage-2 page tables of the owner VM.
 */
int gh_rm_mem_reclaim(struct gh_rm *rm, struct gh_rm_mem_parcel *parcel)
{
	struct gh_rm_mem_release_req req = {
		.mem_handle = cpu_to_le32(parcel->mem_handle),
	};
	int ret;

	ret = gh_rm_call(rm, GH_RM_RPC_MEM_RECLAIM, &req, sizeof(req), NULL, NULL);
	/* Only call the platform mem reclaim hooks if we reclaimed the memory */
	if (ret)
		return ret;

	return gh_rm_platform_post_mem_reclaim(rm, parcel);
}

/**
 * gh_rm_vm_set_firmware_mem() - Set the location of firmware for GH_RM_VM_AUTH_QCOM_ANDROID_PVM VMs
 * @rm: Handle to a Gunyah resource manager.
 * @vmid: VM identifier allocated with gh_rm_alloc_vmid.
 * @parcel: Memory parcel where the firmware should be loaded.
 * @fw_offset: offset into the memory parcel where the firmware should be loaded.
 * @fw_size: Maxmimum size of the fw that can be loaded.
 */
int gh_rm_vm_set_firmware_mem(struct gh_rm *rm, u16 vmid, struct gh_rm_mem_parcel *parcel,
				u64 fw_offset, u64 fw_size)
{
	struct gh_vm_set_firmware_mem_req req = {
		.vmid = cpu_to_le16(vmid),
		.mem_handle = cpu_to_le32(parcel->mem_handle),
		.fw_offset = cpu_to_le64(fw_offset),
		.fw_size = cpu_to_le64(fw_size),
	};

	return gh_rm_call(rm, GH_RM_RPC_VM_SET_FIRMWARE_MEM, &req, sizeof(req), NULL, NULL);
}
EXPORT_SYMBOL_GPL(gh_rm_vm_set_firmware_mem);

/**
 * gh_rm_alloc_vmid() - Allocate a new VM in Gunyah. Returns the VM identifier.
 * @rm: Handle to a Gunyah resource manager
 * @vmid: Use 0 to dynamically allocate a VM. A reserved VMID can be supplied
 *        to request allocation of a platform-defined VM.
 *
 * Returns - the allocated VMID or negative value on error
 */
int gh_rm_alloc_vmid(struct gh_rm *rm, u16 vmid)
{
	struct gh_rm_vm_common_vmid_req req_payload = {
		.vmid = cpu_to_le16(vmid),
	};
	struct gh_rm_vm_alloc_vmid_resp *resp_payload;
	size_t resp_size;
	void *resp;
	int ret;

	ret = gh_rm_call(rm, GH_RM_RPC_VM_ALLOC_VMID, &req_payload, sizeof(req_payload), &resp,
			&resp_size);
	if (ret)
		return ret;

	if (!vmid) {
		resp_payload = resp;
		ret = le16_to_cpu(resp_payload->vmid);
		kfree(resp);
	}

	return ret;
}

/**
 * gh_rm_dealloc_vmid() - Dispose of a VMID
 * @rm: Handle to a Gunyah resource manager
 * @vmid: VM identifier allocated with gh_rm_alloc_vmid
 */
int gh_rm_dealloc_vmid(struct gh_rm *rm, u16 vmid)
{
	return gh_rm_common_vmid_call(rm, GH_RM_RPC_VM_DEALLOC_VMID, vmid);
}

/**
 * gh_rm_vm_reset() - Reset a VM's resources
 * @rm: Handle to a Gunyah resource manager
 * @vmid: VM identifier allocated with gh_rm_alloc_vmid
 *
 * As part of tearing down the VM, request RM to clean up all the VM resources
 * associated with the VM. Only after this, Linux can clean up all the
 * references it maintains to resources.
 */
int gh_rm_vm_reset(struct gh_rm *rm, u16 vmid)
{
	return gh_rm_common_vmid_call(rm, GH_RM_RPC_VM_RESET, vmid);
}

/**
 * gh_rm_vm_start() - Move a VM into "ready to run" state
 * @rm: Handle to a Gunyah resource manager
 * @vmid: VM identifier allocated with gh_rm_alloc_vmid
 *
 * On VMs which use proxy scheduling, vcpu_run is needed to actually run the VM.
 * On VMs which use Gunyah's scheduling, the vCPUs start executing in accordance with Gunyah
 * scheduling policies.
 */
int gh_rm_vm_start(struct gh_rm *rm, u16 vmid)
{
	return gh_rm_common_vmid_call(rm, GH_RM_RPC_VM_START, vmid);
}

/**
 * gh_rm_vm_stop() - Send a request to Resource Manager VM to forcibly stop a VM.
 * @rm: Handle to a Gunyah resource manager
 * @vmid: VM identifier allocated with gh_rm_alloc_vmid
 */
int gh_rm_vm_stop(struct gh_rm *rm, u16 vmid)
{
	struct gh_rm_vm_stop_req req_payload = {
		.vmid = cpu_to_le16(vmid),
		.flags = GH_RM_VM_STOP_FLAG_FORCE_STOP,
		.stop_reason = cpu_to_le32(GH_RM_VM_STOP_REASON_FORCE_STOP),
	};

	return gh_rm_call(rm, GH_RM_RPC_VM_STOP, &req_payload, sizeof(req_payload), NULL, NULL);
}

/**
 * gh_rm_vm_configure() - Prepare a VM to start and provide the common
 *			  configuration needed by RM to configure a VM
 * @rm: Handle to a Gunyah resource manager
 * @vmid: VM identifier allocated with gh_rm_alloc_vmid
 * @auth_mechanism: Authentication mechanism used by resource manager to verify
 *                  the virtual machine
 * @mem_handle: Handle to a previously shared memparcel that contains all parts
 *              of the VM image subject to authentication.
 * @image_offset: Start address of VM image, relative to the start of memparcel
 * @image_size: Size of the VM image
 * @dtb_offset: Start address of the devicetree binary with VM configuration,
 *              relative to start of memparcel.
 * @dtb_size: Maximum size of devicetree binary.
 */
int gh_rm_vm_configure(struct gh_rm *rm, u16 vmid, enum gh_rm_vm_auth_mechanism auth_mechanism,
		u32 mem_handle, u64 image_offset, u64 image_size, u64 dtb_offset, u64 dtb_size)
{
	struct gh_rm_vm_config_image_req req_payload = {
		.vmid = cpu_to_le16(vmid),
		.auth_mech = cpu_to_le16(auth_mechanism),
		.mem_handle = cpu_to_le32(mem_handle),
		.image_offset = cpu_to_le64(image_offset),
		.image_size = cpu_to_le64(image_size),
		.dtb_offset = cpu_to_le64(dtb_offset),
		.dtb_size = cpu_to_le64(dtb_size),
	};

	return gh_rm_call(rm, GH_RM_RPC_VM_CONFIG_IMAGE, &req_payload, sizeof(req_payload),
			  NULL, NULL);
}

/**
 * gh_rm_vm_init() - Move the VM to initialized state.
 * @rm: Handle to a Gunyah resource manager
 * @vmid: VM identifier
 *
 * RM will allocate needed resources for the VM.
 */
int gh_rm_vm_init(struct gh_rm *rm, u16 vmid)
{
	return gh_rm_common_vmid_call(rm, GH_RM_RPC_VM_INIT, vmid);
}

/**
 * gh_rm_get_hyp_resources() - Retrieve hypervisor resources (capabilities) associated with a VM
 * @rm: Handle to a Gunyah resource manager
 * @vmid: VMID of the other VM to get the resources of
 * @resources: Set by gh_rm_get_hyp_resources and contains the returned hypervisor resources.
 *             Caller must free the resources pointer if successful.
 */
int gh_rm_get_hyp_resources(struct gh_rm *rm, u16 vmid,
				struct gh_rm_hyp_resources **resources)
{
	struct gh_rm_vm_common_vmid_req req_payload = {
		.vmid = cpu_to_le16(vmid),
	};
	struct gh_rm_hyp_resources *resp;
	size_t resp_size;
	int ret;

	ret = gh_rm_call(rm, GH_RM_RPC_VM_GET_HYP_RESOURCES,
			 &req_payload, sizeof(req_payload),
			 (void **)&resp, &resp_size);
	if (ret)
		return ret;

	if (!resp_size)
		return -EBADMSG;

	if (resp_size < struct_size(resp, entries, 0) ||
		resp_size != struct_size(resp, entries, le32_to_cpu(resp->n_entries))) {
		kfree(resp);
		return -EBADMSG;
	}

	*resources = resp;
	return 0;
}

/**
 * gh_rm_get_vmid() - Retrieve VMID of this virtual machine
 * @rm: Handle to a Gunyah resource manager
 * @vmid: Filled with the VMID of this VM
 */
int gh_rm_get_vmid(struct gh_rm *rm, u16 *vmid)
{
	static u16 cached_vmid = GH_VMID_INVAL;
	size_t resp_size;
	__le32 *resp;
	int ret;

	if (cached_vmid != GH_VMID_INVAL) {
		*vmid = cached_vmid;
		return 0;
	}

	ret = gh_rm_call(rm, GH_RM_RPC_VM_GET_VMID, NULL, 0, (void **)&resp, &resp_size);
	if (ret)
		return ret;

	*vmid = cached_vmid = lower_16_bits(le32_to_cpu(*resp));
	kfree(resp);

	return ret;
}
EXPORT_SYMBOL_GPL(gh_rm_get_vmid);
