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

#define HC_ID_GEN_BASE			0x0UL
#define HC_SOS_REMOVE_CPU		_HC_ID(HC_ID, HC_ID_GEN_BASE + 0x01)

#define HC_ID_VM_BASE			0x10UL
#define HC_CREATE_VM			_HC_ID(HC_ID, HC_ID_VM_BASE + 0x00)
#define HC_DESTROY_VM			_HC_ID(HC_ID, HC_ID_VM_BASE + 0x01)
#define HC_START_VM			_HC_ID(HC_ID, HC_ID_VM_BASE + 0x02)
#define HC_PAUSE_VM			_HC_ID(HC_ID, HC_ID_VM_BASE + 0x03)
#define HC_RESET_VM			_HC_ID(HC_ID, HC_ID_VM_BASE + 0x05)
#define HC_SET_VCPU_REGS		_HC_ID(HC_ID, HC_ID_VM_BASE + 0x06)

#define HC_ID_IRQ_BASE			0x20UL
#define HC_INJECT_MSI			_HC_ID(HC_ID, HC_ID_IRQ_BASE + 0x03)
#define HC_VM_INTR_MONITOR		_HC_ID(HC_ID, HC_ID_IRQ_BASE + 0x04)
#define HC_SET_IRQLINE			_HC_ID(HC_ID, HC_ID_IRQ_BASE + 0x05)

#define HC_ID_IOREQ_BASE		0x30UL
#define HC_SET_IOREQ_BUFFER		_HC_ID(HC_ID, HC_ID_IOREQ_BASE + 0x00)
#define HC_NOTIFY_REQUEST_FINISH	_HC_ID(HC_ID, HC_ID_IOREQ_BASE + 0x01)

#define HC_ID_MEM_BASE			0x40UL
#define HC_VM_SET_MEMORY_REGIONS	_HC_ID(HC_ID, HC_ID_MEM_BASE + 0x02)

#define HC_ID_PCI_BASE			0x50UL
#define HC_SET_PTDEV_INTR		_HC_ID(HC_ID, HC_ID_PCI_BASE + 0x03)
#define HC_RESET_PTDEV_INTR		_HC_ID(HC_ID, HC_ID_PCI_BASE + 0x04)
#define HC_ASSIGN_PCIDEV		_HC_ID(HC_ID, HC_ID_PCI_BASE + 0x05)
#define HC_DEASSIGN_PCIDEV		_HC_ID(HC_ID, HC_ID_PCI_BASE + 0x06)
#define HC_ASSIGN_MMIODEV		_HC_ID(HC_ID, HC_ID_PCI_BASE + 0x07)
#define HC_DEASSIGN_MMIODEV		_HC_ID(HC_ID, HC_ID_PCI_BASE + 0x08)

#define HC_ID_PM_BASE			0x80UL
#define HC_PM_GET_CPU_STATE		_HC_ID(HC_ID, HC_ID_PM_BASE + 0x00)

/**
 * hcall_sos_remove_cpu() - Remove a vCPU of Service VM
 * @cpu: The vCPU to be removed
 *
 * Return: 0 on success, <0 on failure
 */
static inline long hcall_sos_remove_cpu(u64 cpu)
{
	return acrn_hypercall1(HC_SOS_REMOVE_CPU, cpu);
}

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
 * hcall_inject_msi() - Deliver a MSI interrupt to a User VM
 * @vmid:	User VM ID
 * @msi:	Service VM GPA of MSI message
 *
 * Return: 0 on success, <0 on failure
 */
static inline long hcall_inject_msi(u64 vmid, u64 msi)
{
	return acrn_hypercall2(HC_INJECT_MSI, vmid, msi);
}

/**
 * hcall_vm_intr_monitor() - Set a shared page for User VM interrupt statistics
 * @vmid:	User VM ID
 * @addr:	Service VM GPA of the shared page
 *
 * Return: 0 on success, <0 on failure
 */
static inline long hcall_vm_intr_monitor(u64 vmid, u64 addr)
{
	return acrn_hypercall2(HC_VM_INTR_MONITOR, vmid, addr);
}

/**
 * hcall_set_irqline() - Set or clear an interrupt line
 * @vmid:	User VM ID
 * @op:		Service VM GPA of interrupt line operations
 *
 * Return: 0 on success, <0 on failure
 */
static inline long hcall_set_irqline(u64 vmid, u64 op)
{
	return acrn_hypercall2(HC_SET_IRQLINE, vmid, op);
}

/**
 * hcall_set_ioreq_buffer() - Set up the shared buffer for I/O Requests.
 * @vmid:	User VM ID
 * @buffer:	Service VM GPA of the shared buffer
 *
 * Return: 0 on success, <0 on failure
 */
static inline long hcall_set_ioreq_buffer(u64 vmid, u64 buffer)
{
	return acrn_hypercall2(HC_SET_IOREQ_BUFFER, vmid, buffer);
}

/**
 * hcall_notify_req_finish() - Notify ACRN Hypervisor of I/O request completion.
 * @vmid:	User VM ID
 * @vcpu:	The vCPU which initiated the I/O request
 *
 * Return: 0 on success, <0 on failure
 */
static inline long hcall_notify_req_finish(u64 vmid, u64 vcpu)
{
	return acrn_hypercall2(HC_NOTIFY_REQUEST_FINISH, vmid, vcpu);
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

/**
 * hcall_assign_mmiodev() - Assign a MMIO device to a User VM
 * @vmid:	User VM ID
 * @addr:	Service VM GPA of the &struct acrn_mmiodev
 *
 * Return: 0 on success, <0 on failure
 */
static inline long hcall_assign_mmiodev(u64 vmid, u64 addr)
{
	return acrn_hypercall2(HC_ASSIGN_MMIODEV, vmid, addr);
}

/**
 * hcall_deassign_mmiodev() - De-assign a PCI device from a User VM
 * @vmid:	User VM ID
 * @addr:	Service VM GPA of the &struct acrn_mmiodev
 *
 * Return: 0 on success, <0 on failure
 */
static inline long hcall_deassign_mmiodev(u64 vmid, u64 addr)
{
	return acrn_hypercall2(HC_DEASSIGN_MMIODEV, vmid, addr);
}

/**
 * hcall_assign_pcidev() - Assign a PCI device to a User VM
 * @vmid:	User VM ID
 * @addr:	Service VM GPA of the &struct acrn_pcidev
 *
 * Return: 0 on success, <0 on failure
 */
static inline long hcall_assign_pcidev(u64 vmid, u64 addr)
{
	return acrn_hypercall2(HC_ASSIGN_PCIDEV, vmid, addr);
}

/**
 * hcall_deassign_pcidev() - De-assign a PCI device from a User VM
 * @vmid:	User VM ID
 * @addr:	Service VM GPA of the &struct acrn_pcidev
 *
 * Return: 0 on success, <0 on failure
 */
static inline long hcall_deassign_pcidev(u64 vmid, u64 addr)
{
	return acrn_hypercall2(HC_DEASSIGN_PCIDEV, vmid, addr);
}

/**
 * hcall_set_ptdev_intr() - Configure an interrupt for an assigned PCI device.
 * @vmid:	User VM ID
 * @irq:	Service VM GPA of the &struct acrn_ptdev_irq
 *
 * Return: 0 on success, <0 on failure
 */
static inline long hcall_set_ptdev_intr(u64 vmid, u64 irq)
{
	return acrn_hypercall2(HC_SET_PTDEV_INTR, vmid, irq);
}

/**
 * hcall_reset_ptdev_intr() - Reset an interrupt for an assigned PCI device.
 * @vmid:	User VM ID
 * @irq:	Service VM GPA of the &struct acrn_ptdev_irq
 *
 * Return: 0 on success, <0 on failure
 */
static inline long hcall_reset_ptdev_intr(u64 vmid, u64 irq)
{
	return acrn_hypercall2(HC_RESET_PTDEV_INTR, vmid, irq);
}

/*
 * hcall_get_cpu_state() - Get P-states and C-states info from the hypervisor
 * @state:	Service VM GPA of buffer of P-states and C-states
 */
static inline long hcall_get_cpu_state(u64 cmd, u64 state)
{
	return acrn_hypercall2(HC_PM_GET_CPU_STATE, cmd, state);
}

#endif /* __ACRN_HSM_HYPERCALL_H */
