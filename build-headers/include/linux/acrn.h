/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Userspace interface for /dev/acrn_hsm - ACRN Hypervisor Service Module
 *
 * This file can be used by applications that need to communicate with the HSM
 * via the ioctl interface.
 *
 * Copyright (C) 2021 Intel Corporation. All rights reserved.
 */

#ifndef _ACRN_H
#define _ACRN_H

#include <linux/types.h>
#include <linux/uuid.h>

#define ACRN_IO_REQUEST_MAX		16

#define ACRN_IOREQ_STATE_PENDING	0
#define ACRN_IOREQ_STATE_COMPLETE	1
#define ACRN_IOREQ_STATE_PROCESSING	2
#define ACRN_IOREQ_STATE_FREE		3

#define ACRN_IOREQ_TYPE_PORTIO		0
#define ACRN_IOREQ_TYPE_MMIO		1
#define ACRN_IOREQ_TYPE_PCICFG		2

#define ACRN_IOREQ_DIR_READ		0
#define ACRN_IOREQ_DIR_WRITE		1

/**
 * struct acrn_mmio_request - Info of a MMIO I/O request
 * @direction:	Access direction of this request (ACRN_IOREQ_DIR_*)
 * @reserved:	Reserved for alignment and should be 0
 * @address:	Access address of this MMIO I/O request
 * @size:	Access size of this MMIO I/O request
 * @value:	Read/write value of this MMIO I/O request
 */
struct acrn_mmio_request {
	__u32	direction;
	__u32	reserved;
	__u64	address;
	__u64	size;
	__u64	value;
};

/**
 * struct acrn_pio_request - Info of a PIO I/O request
 * @direction:	Access direction of this request (ACRN_IOREQ_DIR_*)
 * @reserved:	Reserved for alignment and should be 0
 * @address:	Access address of this PIO I/O request
 * @size:	Access size of this PIO I/O request
 * @value:	Read/write value of this PIO I/O request
 */
struct acrn_pio_request {
	__u32	direction;
	__u32	reserved;
	__u64	address;
	__u64	size;
	__u32	value;
};

/**
 * struct acrn_pci_request - Info of a PCI I/O request
 * @direction:	Access direction of this request (ACRN_IOREQ_DIR_*)
 * @reserved:	Reserved for alignment and should be 0
 * @size:	Access size of this PCI I/O request
 * @value:	Read/write value of this PIO I/O request
 * @bus:	PCI bus value of this PCI I/O request
 * @dev:	PCI device value of this PCI I/O request
 * @func:	PCI function value of this PCI I/O request
 * @reg:	PCI config space offset of this PCI I/O request
 *
 * Need keep same header layout with &struct acrn_pio_request.
 */
struct acrn_pci_request {
	__u32	direction;
	__u32	reserved[3];
	__u64	size;
	__u32	value;
	__u32	bus;
	__u32	dev;
	__u32	func;
	__u32	reg;
};

/**
 * struct acrn_io_request - 256-byte ACRN I/O request
 * @type:		Type of this request (ACRN_IOREQ_TYPE_*).
 * @completion_polling:	Polling flag. Hypervisor will poll completion of the
 *			I/O request if this flag set.
 * @reserved0:		Reserved fields.
 * @reqs:		Union of different types of request. Byte offset: 64.
 * @reqs.pio_request:	PIO request data of the I/O request.
 * @reqs.pci_request:	PCI configuration space request data of the I/O request.
 * @reqs.mmio_request:	MMIO request data of the I/O request.
 * @reqs.data:		Raw data of the I/O request.
 * @reserved1:		Reserved fields.
 * @kernel_handled:	Flag indicates this request need be handled in kernel.
 * @processed:		The status of this request (ACRN_IOREQ_STATE_*).
 *
 * The state transitions of ACRN I/O request:
 *
 *    FREE -> PENDING -> PROCESSING -> COMPLETE -> FREE -> ...
 *
 * An I/O request in COMPLETE or FREE state is owned by the hypervisor. HSM and
 * ACRN userspace are in charge of processing the others.
 *
 * On basis of the states illustrated above, a typical lifecycle of ACRN IO
 * request would look like:
 *
 * Flow                 (assume the initial state is FREE)
 * |
 * |   Service VM vCPU 0     Service VM vCPU x      User vCPU y
 * |
 * |                                             hypervisor:
 * |                                               fills in type, addr, etc.
 * |                                               pauses the User VM vCPU y
 * |                                               sets the state to PENDING (a)
 * |                                               fires an upcall to Service VM
 * |
 * | HSM:
 * |  scans for PENDING requests
 * |  sets the states to PROCESSING (b)
 * |  assigns the requests to clients (c)
 * V
 * |                     client:
 * |                       scans for the assigned requests
 * |                       handles the requests (d)
 * |                     HSM:
 * |                       sets states to COMPLETE
 * |                       notifies the hypervisor
 * |
 * |                     hypervisor:
 * |                       resumes User VM vCPU y (e)
 * |
 * |                                             hypervisor:
 * |                                               post handling (f)
 * V                                               sets states to FREE
 *
 * Note that the procedures (a) to (f) in the illustration above require to be
 * strictly processed in the order.  One vCPU cannot trigger another request of
 * I/O emulation before completing the previous one.
 *
 * Atomic and barriers are required when HSM and hypervisor accessing the state
 * of &struct acrn_io_request.
 *
 */
struct acrn_io_request {
	__u32	type;
	__u32	completion_polling;
	__u32	reserved0[14];
	union {
		struct acrn_pio_request		pio_request;
		struct acrn_pci_request		pci_request;
		struct acrn_mmio_request	mmio_request;
		__u64				data[8];
	} reqs;
	__u32	reserved1;
	__u32	kernel_handled;
	__u32	processed;
} __attribute__((aligned(256)));

struct acrn_io_request_buffer {
	union {
		struct acrn_io_request	req_slot[ACRN_IO_REQUEST_MAX];
		__u8			reserved[4096];
	};
};

/**
 * struct acrn_ioreq_notify - The structure of ioreq completion notification
 * @vmid:	User VM ID
 * @reserved:	Reserved and should be 0
 * @vcpu:	vCPU ID
 */
struct acrn_ioreq_notify {
	__u16	vmid;
	__u16	reserved;
	__u32	vcpu;
};

/**
 * struct acrn_vm_creation - Info to create a User VM
 * @vmid:		User VM ID returned from the hypervisor
 * @reserved0:		Reserved and must be 0
 * @vcpu_num:		Number of vCPU in the VM. Return from hypervisor.
 * @reserved1:		Reserved and must be 0
 * @uuid:		UUID of the VM. Pass to hypervisor directly.
 * @vm_flag:		Flag of the VM creating. Pass to hypervisor directly.
 * @ioreq_buf:		Service VM GPA of I/O request buffer. Pass to
 *			hypervisor directly.
 * @cpu_affinity:	CPU affinity of the VM. Pass to hypervisor directly.
 * 			It's a bitmap which indicates CPUs used by the VM.
 */
struct acrn_vm_creation {
	__u16	vmid;
	__u16	reserved0;
	__u16	vcpu_num;
	__u16	reserved1;
	guid_t	uuid;
	__u64	vm_flag;
	__u64	ioreq_buf;
	__u64	cpu_affinity;
};

/**
 * struct acrn_gp_regs - General registers of a User VM
 * @rax:	Value of register RAX
 * @rcx:	Value of register RCX
 * @rdx:	Value of register RDX
 * @rbx:	Value of register RBX
 * @rsp:	Value of register RSP
 * @rbp:	Value of register RBP
 * @rsi:	Value of register RSI
 * @rdi:	Value of register RDI
 * @r8:		Value of register R8
 * @r9:		Value of register R9
 * @r10:	Value of register R10
 * @r11:	Value of register R11
 * @r12:	Value of register R12
 * @r13:	Value of register R13
 * @r14:	Value of register R14
 * @r15:	Value of register R15
 */
struct acrn_gp_regs {
	__le64	rax;
	__le64	rcx;
	__le64	rdx;
	__le64	rbx;
	__le64	rsp;
	__le64	rbp;
	__le64	rsi;
	__le64	rdi;
	__le64	r8;
	__le64	r9;
	__le64	r10;
	__le64	r11;
	__le64	r12;
	__le64	r13;
	__le64	r14;
	__le64	r15;
};

/**
 * struct acrn_descriptor_ptr - Segment descriptor table of a User VM.
 * @limit:	Limit field.
 * @base:	Base field.
 * @reserved:	Reserved and must be 0.
 */
struct acrn_descriptor_ptr {
	__le16	limit;
	__le64	base;
	__le16	reserved[3];
} __attribute__ ((__packed__));

/**
 * struct acrn_regs - Registers structure of a User VM
 * @gprs:		General registers
 * @gdt:		Global Descriptor Table
 * @idt:		Interrupt Descriptor Table
 * @rip:		Value of register RIP
 * @cs_base:		Base of code segment selector
 * @cr0:		Value of register CR0
 * @cr4:		Value of register CR4
 * @cr3:		Value of register CR3
 * @ia32_efer:		Value of IA32_EFER MSR
 * @rflags:		Value of regsiter RFLAGS
 * @reserved_64:	Reserved and must be 0
 * @cs_ar:		Attribute field of code segment selector
 * @cs_limit:		Limit field of code segment selector
 * @reserved_32:	Reserved and must be 0
 * @cs_sel:		Value of code segment selector
 * @ss_sel:		Value of stack segment selector
 * @ds_sel:		Value of data segment selector
 * @es_sel:		Value of extra segment selector
 * @fs_sel:		Value of FS selector
 * @gs_sel:		Value of GS selector
 * @ldt_sel:		Value of LDT descriptor selector
 * @tr_sel:		Value of TSS descriptor selector
 */
struct acrn_regs {
	struct acrn_gp_regs		gprs;
	struct acrn_descriptor_ptr	gdt;
	struct acrn_descriptor_ptr	idt;

	__le64				rip;
	__le64				cs_base;
	__le64				cr0;
	__le64				cr4;
	__le64				cr3;
	__le64				ia32_efer;
	__le64				rflags;
	__le64				reserved_64[4];

	__le32				cs_ar;
	__le32				cs_limit;
	__le32				reserved_32[3];

	__le16				cs_sel;
	__le16				ss_sel;
	__le16				ds_sel;
	__le16				es_sel;
	__le16				fs_sel;
	__le16				gs_sel;
	__le16				ldt_sel;
	__le16				tr_sel;
};

/**
 * struct acrn_vcpu_regs - Info of vCPU registers state
 * @vcpu_id:	vCPU ID
 * @reserved:	Reserved and must be 0
 * @vcpu_regs:	vCPU registers state
 *
 * This structure will be passed to hypervisor directly.
 */
struct acrn_vcpu_regs {
	__u16			vcpu_id;
	__u16			reserved[3];
	struct acrn_regs	vcpu_regs;
};

#define	ACRN_MEM_ACCESS_RIGHT_MASK	0x00000007U
#define	ACRN_MEM_ACCESS_READ		0x00000001U
#define	ACRN_MEM_ACCESS_WRITE		0x00000002U
#define	ACRN_MEM_ACCESS_EXEC		0x00000004U
#define	ACRN_MEM_ACCESS_RWX		(ACRN_MEM_ACCESS_READ  | \
					 ACRN_MEM_ACCESS_WRITE | \
					 ACRN_MEM_ACCESS_EXEC)

#define	ACRN_MEM_TYPE_MASK		0x000007C0U
#define	ACRN_MEM_TYPE_WB		0x00000040U
#define	ACRN_MEM_TYPE_WT		0x00000080U
#define	ACRN_MEM_TYPE_UC		0x00000100U
#define	ACRN_MEM_TYPE_WC		0x00000200U
#define	ACRN_MEM_TYPE_WP		0x00000400U

/* Memory mapping types */
#define	ACRN_MEMMAP_RAM			0
#define	ACRN_MEMMAP_MMIO		1

/**
 * struct acrn_vm_memmap - A EPT memory mapping info for a User VM.
 * @type:		Type of the memory mapping (ACRM_MEMMAP_*).
 *			Pass to hypervisor directly.
 * @attr:		Attribute of the memory mapping.
 *			Pass to hypervisor directly.
 * @user_vm_pa:		Physical address of User VM.
 *			Pass to hypervisor directly.
 * @service_vm_pa:	Physical address of Service VM.
 *			Pass to hypervisor directly.
 * @vma_base:		VMA address of Service VM. Pass to hypervisor directly.
 * @len:		Length of the memory mapping.
 *			Pass to hypervisor directly.
 */
struct acrn_vm_memmap {
	__u32	type;
	__u32	attr;
	__u64	user_vm_pa;
	union {
		__u64	service_vm_pa;
		__u64	vma_base;
	};
	__u64	len;
};

/* Type of interrupt of a passthrough device */
#define ACRN_PTDEV_IRQ_INTX	0
#define ACRN_PTDEV_IRQ_MSI	1
#define ACRN_PTDEV_IRQ_MSIX	2
/**
 * struct acrn_ptdev_irq - Interrupt data of a passthrough device.
 * @type:		Type (ACRN_PTDEV_IRQ_*)
 * @virt_bdf:		Virtual Bus/Device/Function
 * @phys_bdf:		Physical Bus/Device/Function
 * @intx:		Info of interrupt
 * @intx.virt_pin:	Virtual IOAPIC pin
 * @intx.phys_pin:	Physical IOAPIC pin
 * @intx.is_pic_pin:	Is PIC pin or not
 *
 * This structure will be passed to hypervisor directly.
 */
struct acrn_ptdev_irq {
	__u32	type;
	__u16	virt_bdf;
	__u16	phys_bdf;

	struct {
		__u32	virt_pin;
		__u32	phys_pin;
		__u32	is_pic_pin;
	} intx;
};

/* Type of PCI device assignment */
#define ACRN_PTDEV_QUIRK_ASSIGN	(1U << 0)

#define ACRN_PCI_NUM_BARS	6
/**
 * struct acrn_pcidev - Info for assigning or de-assigning a PCI device
 * @type:	Type of the assignment
 * @virt_bdf:	Virtual Bus/Device/Function
 * @phys_bdf:	Physical Bus/Device/Function
 * @intr_line:	PCI interrupt line
 * @intr_pin:	PCI interrupt pin
 * @bar:	PCI BARs.
 *
 * This structure will be passed to hypervisor directly.
 */
struct acrn_pcidev {
	__u32	type;
	__u16	virt_bdf;
	__u16	phys_bdf;
	__u8	intr_line;
	__u8	intr_pin;
	__u32	bar[ACRN_PCI_NUM_BARS];
};

/**
 * struct acrn_msi_entry - Info for injecting a MSI interrupt to a VM
 * @msi_addr:	MSI addr[19:12] with dest vCPU ID
 * @msi_data:	MSI data[7:0] with vector
 */
struct acrn_msi_entry {
	__u64	msi_addr;
	__u64	msi_data;
};

struct acrn_acpi_generic_address {
	__u8	space_id;
	__u8	bit_width;
	__u8	bit_offset;
	__u8	access_size;
	__u64	address;
} __attribute__ ((__packed__));

/**
 * struct acrn_cstate_data - A C state package defined in ACPI
 * @cx_reg:	Register of the C state object
 * @type:	Type of the C state object
 * @latency:	The worst-case latency to enter and exit this C state
 * @power:	The average power consumption when in this C state
 */
struct acrn_cstate_data {
	struct acrn_acpi_generic_address	cx_reg;
	__u8					type;
	__u32					latency;
	__u64					power;
};

/**
 * struct acrn_pstate_data - A P state package defined in ACPI
 * @core_frequency:	CPU frequency (in MHz).
 * @power:		Power dissipation (in milliwatts).
 * @transition_latency:	The worst-case latency in microseconds that CPU is
 * 			unavailable during a transition from any P state to
 * 			this P state.
 * @bus_master_latency:	The worst-case latency in microseconds that Bus Masters
 * 			are prevented from accessing memory during a transition
 * 			from any P state to this P state.
 * @control:		The value to be written to Performance Control Register
 * @status:		Transition status.
 */
struct acrn_pstate_data {
	__u64	core_frequency;
	__u64	power;
	__u64	transition_latency;
	__u64	bus_master_latency;
	__u64	control;
	__u64	status;
};

#define PMCMD_TYPE_MASK		0x000000ff
enum acrn_pm_cmd_type {
	ACRN_PMCMD_GET_PX_CNT,
	ACRN_PMCMD_GET_PX_DATA,
	ACRN_PMCMD_GET_CX_CNT,
	ACRN_PMCMD_GET_CX_DATA,
};

#define ACRN_IOEVENTFD_FLAG_PIO		0x01
#define ACRN_IOEVENTFD_FLAG_DATAMATCH	0x02
#define ACRN_IOEVENTFD_FLAG_DEASSIGN	0x04
/**
 * struct acrn_ioeventfd - Data to operate a &struct hsm_ioeventfd
 * @fd:		The fd of eventfd associated with a hsm_ioeventfd
 * @flags:	Logical-OR of ACRN_IOEVENTFD_FLAG_*
 * @addr:	The start address of IO range of ioeventfd
 * @len:	The length of IO range of ioeventfd
 * @reserved:	Reserved and should be 0
 * @data:	Data for data matching
 *
 * Without flag ACRN_IOEVENTFD_FLAG_DEASSIGN, ioctl ACRN_IOCTL_IOEVENTFD
 * creates a &struct hsm_ioeventfd with properties originated from &struct
 * acrn_ioeventfd. With flag ACRN_IOEVENTFD_FLAG_DEASSIGN, ioctl
 * ACRN_IOCTL_IOEVENTFD destroys the &struct hsm_ioeventfd matching the fd.
 */
struct acrn_ioeventfd {
	__u32	fd;
	__u32	flags;
	__u64	addr;
	__u32	len;
	__u32	reserved;
	__u64	data;
};

#define ACRN_IRQFD_FLAG_DEASSIGN	0x01
/**
 * struct acrn_irqfd - Data to operate a &struct hsm_irqfd
 * @fd:		The fd of eventfd associated with a hsm_irqfd
 * @flags:	Logical-OR of ACRN_IRQFD_FLAG_*
 * @msi:	Info of MSI associated with the irqfd
 */
struct acrn_irqfd {
	__s32			fd;
	__u32			flags;
	struct acrn_msi_entry	msi;
};

/* The ioctl type, documented in ioctl-number.rst */
#define ACRN_IOCTL_TYPE			0xA2

/*
 * Common IOCTL IDs definition for ACRN userspace
 */
#define ACRN_IOCTL_CREATE_VM		\
	_IOWR(ACRN_IOCTL_TYPE, 0x10, struct acrn_vm_creation)
#define ACRN_IOCTL_DESTROY_VM		\
	_IO(ACRN_IOCTL_TYPE, 0x11)
#define ACRN_IOCTL_START_VM		\
	_IO(ACRN_IOCTL_TYPE, 0x12)
#define ACRN_IOCTL_PAUSE_VM		\
	_IO(ACRN_IOCTL_TYPE, 0x13)
#define ACRN_IOCTL_RESET_VM		\
	_IO(ACRN_IOCTL_TYPE, 0x15)
#define ACRN_IOCTL_SET_VCPU_REGS	\
	_IOW(ACRN_IOCTL_TYPE, 0x16, struct acrn_vcpu_regs)

#define ACRN_IOCTL_INJECT_MSI		\
	_IOW(ACRN_IOCTL_TYPE, 0x23, struct acrn_msi_entry)
#define ACRN_IOCTL_VM_INTR_MONITOR	\
	_IOW(ACRN_IOCTL_TYPE, 0x24, unsigned long)
#define ACRN_IOCTL_SET_IRQLINE		\
	_IOW(ACRN_IOCTL_TYPE, 0x25, __u64)

#define ACRN_IOCTL_NOTIFY_REQUEST_FINISH \
	_IOW(ACRN_IOCTL_TYPE, 0x31, struct acrn_ioreq_notify)
#define ACRN_IOCTL_CREATE_IOREQ_CLIENT	\
	_IO(ACRN_IOCTL_TYPE, 0x32)
#define ACRN_IOCTL_ATTACH_IOREQ_CLIENT	\
	_IO(ACRN_IOCTL_TYPE, 0x33)
#define ACRN_IOCTL_DESTROY_IOREQ_CLIENT	\
	_IO(ACRN_IOCTL_TYPE, 0x34)
#define ACRN_IOCTL_CLEAR_VM_IOREQ	\
	_IO(ACRN_IOCTL_TYPE, 0x35)

#define ACRN_IOCTL_SET_MEMSEG		\
	_IOW(ACRN_IOCTL_TYPE, 0x41, struct acrn_vm_memmap)
#define ACRN_IOCTL_UNSET_MEMSEG		\
	_IOW(ACRN_IOCTL_TYPE, 0x42, struct acrn_vm_memmap)

#define ACRN_IOCTL_SET_PTDEV_INTR	\
	_IOW(ACRN_IOCTL_TYPE, 0x53, struct acrn_ptdev_irq)
#define ACRN_IOCTL_RESET_PTDEV_INTR	\
	_IOW(ACRN_IOCTL_TYPE, 0x54, struct acrn_ptdev_irq)
#define ACRN_IOCTL_ASSIGN_PCIDEV	\
	_IOW(ACRN_IOCTL_TYPE, 0x55, struct acrn_pcidev)
#define ACRN_IOCTL_DEASSIGN_PCIDEV	\
	_IOW(ACRN_IOCTL_TYPE, 0x56, struct acrn_pcidev)

#define ACRN_IOCTL_PM_GET_CPU_STATE	\
	_IOWR(ACRN_IOCTL_TYPE, 0x60, __u64)

#define ACRN_IOCTL_IOEVENTFD		\
	_IOW(ACRN_IOCTL_TYPE, 0x70, struct acrn_ioeventfd)
#define ACRN_IOCTL_IRQFD		\
	_IOW(ACRN_IOCTL_TYPE, 0x71, struct acrn_irqfd)

#endif /* _ACRN_H */
