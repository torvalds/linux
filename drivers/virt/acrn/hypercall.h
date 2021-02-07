/* SPDX-License-Identifier: GPL-2.0 */
/*
 * ACRN HSM: hypercalls of ACRN Hypervisor
 */
#ifndef __ACRN_HSM_HYPERCALL_H
#define __ACRN_HSM_HYPERCALL_H
#include <asm/acrn.h>

/*
 * Hypercall IDs of the ACRN Hypervisor
 */
#define _HC_ID(x, y) (((x) << 24) | (y))

#define HC_ID 0x80UL

#define HC_ID_VM_BASE			0x10UL
#define HC_CREATE_VM			_HC_ID(HC_ID, HC_ID_VM_BASE + 0x00)
#define HC_DESTROY_VM			_HC_ID(HC_ID, HC_ID_VM_BASE + 0x01)
#define HC_START_VM			_HC_ID(HC_ID, HC_ID_VM_BASE + 0x02)
#define HC_PAUSE_VM			_HC_ID(HC_ID, HC_ID_VM_BASE + 0x03)
#define HC_RESET_VM			_HC_ID(HC_ID, HC_ID_VM_BASE + 0x05)
#define HC_SET_VCPU_REGS		_HC_ID(HC_ID, HC_ID_VM_BASE + 0x06)

#define HC_ID_MEM_BASE			0x40UL
#define HC_VM_SET_MEMORY_REGIONS	_HC_ID(HC_ID, HC_ID_MEM_BASE + 0x02)

/**
 * hcall_create_vm() - Create a User VM
 * @vminfo:	Service VM GPA of info of User VM creation
 *
 * Return: 0 on success, <0 on failure
 */
static inline long hcall_create_vm(u64 vminfo)
{
	return acrn_hypercall1(HC_CREATE_VM, vminfo);
}

/**
 * hcall_start_vm() - Start a User VM
 * @vmid:	User VM ID
 *
 * Return: 0 on success, <0 on failure
 */
static inline long hcall_start_vm(u64 vmid)
{
	return acrn_hypercall1(HC_START_VM, vmid);
}

/**
 * hcall_pause_vm() - Pause a User VM
 * @vmid:	User VM ID
 *
 * Return: 0 on success, <0 on failure
 */
static inline long hcall_pause_vm(u64 vmid)
{
	return acrn_hypercall1(HC_PAUSE_VM, vmid);
}

/**
 * hcall_destroy_vm() - Destroy a User VM
 * @vmid:	User VM ID
 *
 * Return: 0 on success, <0 on failure
 */
static inline long hcall_destroy_vm(u64 vmid)
{
	return acrn_hypercall1(HC_DESTROY_VM, vmid);
}

/**
 * hcall_reset_vm() - Reset a User VM
 * @vmid:	User VM ID
 *
 * Return: 0 on success, <0 on failure
 */
static inline long hcall_reset_vm(u64 vmid)
{
	return acrn_hypercall1(HC_RESET_VM, vmid);
}

/**
 * hcall_set_vcpu_regs() - Set up registers of virtual CPU of a User VM
 * @vmid:	User VM ID
 * @regs_state:	Service VM GPA of registers state
 *
 * Return: 0 on success, <0 on failure
 */
static inline long hcall_set_vcpu_regs(u64 vmid, u64 regs_state)
{
	return acrn_hypercall2(HC_SET_VCPU_REGS, vmid, regs_state);
}

/**
 * hcall_set_memory_regions() - Inform the hypervisor to set up EPT mappings
 * @regions_pa:	Service VM GPA of &struct vm_memory_region_batch
 *
 * Return: 0 on success, <0 on failure
 */
static inline long hcall_set_memory_regions(u64 regions_pa)
{
	return acrn_hypercall1(HC_VM_SET_MEMORY_REGIONS, regions_pa);
}

#endif /* __ACRN_HSM_HYPERCALL_H */
