/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2024 Ventana Micro Systems Inc.
 */

#ifndef __KVM_NACL_H
#define __KVM_NACL_H

#include <linux/jump_label.h>
#include <linux/percpu.h>
#include <asm/byteorder.h>
#include <asm/csr.h>
#include <asm/sbi.h>

struct kvm_vcpu_arch;

DECLARE_STATIC_KEY_FALSE(kvm_riscv_nacl_available);
#define kvm_riscv_nacl_available() \
	static_branch_unlikely(&kvm_riscv_nacl_available)

DECLARE_STATIC_KEY_FALSE(kvm_riscv_nacl_sync_csr_available);
#define kvm_riscv_nacl_sync_csr_available() \
	static_branch_unlikely(&kvm_riscv_nacl_sync_csr_available)

DECLARE_STATIC_KEY_FALSE(kvm_riscv_nacl_sync_hfence_available);
#define kvm_riscv_nacl_sync_hfence_available() \
	static_branch_unlikely(&kvm_riscv_nacl_sync_hfence_available)

DECLARE_STATIC_KEY_FALSE(kvm_riscv_nacl_sync_sret_available);
#define kvm_riscv_nacl_sync_sret_available() \
	static_branch_unlikely(&kvm_riscv_nacl_sync_sret_available)

DECLARE_STATIC_KEY_FALSE(kvm_riscv_nacl_autoswap_csr_available);
#define kvm_riscv_nacl_autoswap_csr_available() \
	static_branch_unlikely(&kvm_riscv_nacl_autoswap_csr_available)

struct kvm_riscv_nacl {
	void *shmem;
	phys_addr_t shmem_phys;
};
DECLARE_PER_CPU(struct kvm_riscv_nacl, kvm_riscv_nacl);

void __kvm_riscv_nacl_hfence(void *shmem,
			     unsigned long control,
			     unsigned long page_num,
			     unsigned long page_count);

void __kvm_riscv_nacl_switch_to(struct kvm_vcpu_arch *vcpu_arch,
				unsigned long sbi_ext_id,
				unsigned long sbi_func_id);

int kvm_riscv_nacl_enable(void);

void kvm_riscv_nacl_disable(void);

void kvm_riscv_nacl_exit(void);

int kvm_riscv_nacl_init(void);

#ifdef CONFIG_32BIT
#define lelong_to_cpu(__x)	le32_to_cpu(__x)
#define cpu_to_lelong(__x)	cpu_to_le32(__x)
#else
#define lelong_to_cpu(__x)	le64_to_cpu(__x)
#define cpu_to_lelong(__x)	cpu_to_le64(__x)
#endif

#define nacl_shmem()							\
	this_cpu_ptr(&kvm_riscv_nacl)->shmem

#define nacl_scratch_read_long(__shmem, __offset)			\
({									\
	unsigned long *__p = (__shmem) +				\
			     SBI_NACL_SHMEM_SCRATCH_OFFSET +		\
			     (__offset);				\
	lelong_to_cpu(*__p);						\
})

#define nacl_scratch_write_long(__shmem, __offset, __val)		\
do {									\
	unsigned long *__p = (__shmem) +				\
			     SBI_NACL_SHMEM_SCRATCH_OFFSET +		\
			     (__offset);				\
	*__p = cpu_to_lelong(__val);					\
} while (0)

#define nacl_scratch_write_longs(__shmem, __offset, __array, __count)	\
do {									\
	unsigned int __i;						\
	unsigned long *__p = (__shmem) +				\
			     SBI_NACL_SHMEM_SCRATCH_OFFSET +		\
			     (__offset);				\
	for (__i = 0; __i < (__count); __i++)				\
		__p[__i] = cpu_to_lelong((__array)[__i]);		\
} while (0)

#define nacl_sync_hfence(__e)						\
	sbi_ecall(SBI_EXT_NACL, SBI_EXT_NACL_SYNC_HFENCE,		\
		  (__e), 0, 0, 0, 0, 0)

#define nacl_hfence_mkconfig(__type, __order, __vmid, __asid)		\
({									\
	unsigned long __c = SBI_NACL_SHMEM_HFENCE_CONFIG_PEND;		\
	__c |= ((__type) & SBI_NACL_SHMEM_HFENCE_CONFIG_TYPE_MASK)	\
		<< SBI_NACL_SHMEM_HFENCE_CONFIG_TYPE_SHIFT;		\
	__c |= (((__order) - SBI_NACL_SHMEM_HFENCE_ORDER_BASE) &	\
		SBI_NACL_SHMEM_HFENCE_CONFIG_ORDER_MASK)		\
		<< SBI_NACL_SHMEM_HFENCE_CONFIG_ORDER_SHIFT;		\
	__c |= ((__vmid) & SBI_NACL_SHMEM_HFENCE_CONFIG_VMID_MASK)	\
		<< SBI_NACL_SHMEM_HFENCE_CONFIG_VMID_SHIFT;		\
	__c |= ((__asid) & SBI_NACL_SHMEM_HFENCE_CONFIG_ASID_MASK);	\
	__c;								\
})

#define nacl_hfence_mkpnum(__order, __addr)				\
	((__addr) >> (__order))

#define nacl_hfence_mkpcount(__order, __size)				\
	((__size) >> (__order))

#define nacl_hfence_gvma(__shmem, __gpa, __gpsz, __order)		\
__kvm_riscv_nacl_hfence(__shmem,					\
	nacl_hfence_mkconfig(SBI_NACL_SHMEM_HFENCE_TYPE_GVMA,		\
			   __order, 0, 0),				\
	nacl_hfence_mkpnum(__order, __gpa),				\
	nacl_hfence_mkpcount(__order, __gpsz))

#define nacl_hfence_gvma_all(__shmem)					\
__kvm_riscv_nacl_hfence(__shmem,					\
	nacl_hfence_mkconfig(SBI_NACL_SHMEM_HFENCE_TYPE_GVMA_ALL,	\
			   0, 0, 0), 0, 0)

#define nacl_hfence_gvma_vmid(__shmem, __vmid, __gpa, __gpsz, __order)	\
__kvm_riscv_nacl_hfence(__shmem,					\
	nacl_hfence_mkconfig(SBI_NACL_SHMEM_HFENCE_TYPE_GVMA_VMID,	\
			   __order, __vmid, 0),				\
	nacl_hfence_mkpnum(__order, __gpa),				\
	nacl_hfence_mkpcount(__order, __gpsz))

#define nacl_hfence_gvma_vmid_all(__shmem, __vmid)			\
__kvm_riscv_nacl_hfence(__shmem,					\
	nacl_hfence_mkconfig(SBI_NACL_SHMEM_HFENCE_TYPE_GVMA_VMID_ALL,	\
			   0, __vmid, 0), 0, 0)

#define nacl_hfence_vvma(__shmem, __vmid, __gva, __gvsz, __order)	\
__kvm_riscv_nacl_hfence(__shmem,					\
	nacl_hfence_mkconfig(SBI_NACL_SHMEM_HFENCE_TYPE_VVMA,		\
			   __order, __vmid, 0),				\
	nacl_hfence_mkpnum(__order, __gva),				\
	nacl_hfence_mkpcount(__order, __gvsz))

#define nacl_hfence_vvma_all(__shmem, __vmid)				\
__kvm_riscv_nacl_hfence(__shmem,					\
	nacl_hfence_mkconfig(SBI_NACL_SHMEM_HFENCE_TYPE_VVMA_ALL,	\
			   0, __vmid, 0), 0, 0)

#define nacl_hfence_vvma_asid(__shmem, __vmid, __asid, __gva, __gvsz, __order)\
__kvm_riscv_nacl_hfence(__shmem,					\
	nacl_hfence_mkconfig(SBI_NACL_SHMEM_HFENCE_TYPE_VVMA_ASID,	\
			   __order, __vmid, __asid),			\
	nacl_hfence_mkpnum(__order, __gva),				\
	nacl_hfence_mkpcount(__order, __gvsz))

#define nacl_hfence_vvma_asid_all(__shmem, __vmid, __asid)		\
__kvm_riscv_nacl_hfence(__shmem,					\
	nacl_hfence_mkconfig(SBI_NACL_SHMEM_HFENCE_TYPE_VVMA_ASID_ALL,	\
			   0, __vmid, __asid), 0, 0)

#define nacl_csr_read(__shmem, __csr)					\
({									\
	unsigned long *__a = (__shmem) + SBI_NACL_SHMEM_CSR_OFFSET;	\
	lelong_to_cpu(__a[SBI_NACL_SHMEM_CSR_INDEX(__csr)]);		\
})

#define nacl_csr_write(__shmem, __csr, __val)				\
do {									\
	void *__s = (__shmem);						\
	unsigned int __i = SBI_NACL_SHMEM_CSR_INDEX(__csr);		\
	unsigned long *__a = (__s) + SBI_NACL_SHMEM_CSR_OFFSET;		\
	u8 *__b = (__s) + SBI_NACL_SHMEM_DBITMAP_OFFSET;		\
	__a[__i] = cpu_to_lelong(__val);				\
	__b[__i >> 3] |= 1U << (__i & 0x7);				\
} while (0)

#define nacl_csr_swap(__shmem, __csr, __val)				\
({									\
	void *__s = (__shmem);						\
	unsigned int __i = SBI_NACL_SHMEM_CSR_INDEX(__csr);		\
	unsigned long *__a = (__s) + SBI_NACL_SHMEM_CSR_OFFSET;		\
	u8 *__b = (__s) + SBI_NACL_SHMEM_DBITMAP_OFFSET;		\
	unsigned long __r = lelong_to_cpu(__a[__i]);			\
	__a[__i] = cpu_to_lelong(__val);				\
	__b[__i >> 3] |= 1U << (__i & 0x7);				\
	__r;								\
})

#define nacl_sync_csr(__csr)						\
	sbi_ecall(SBI_EXT_NACL, SBI_EXT_NACL_SYNC_CSR,			\
		  (__csr), 0, 0, 0, 0, 0)

/*
 * Each ncsr_xyz() macro defined below has it's own static-branch so every
 * use of ncsr_xyz() macro emits a patchable direct jump. This means multiple
 * back-to-back ncsr_xyz() macro usage will emit multiple patchable direct
 * jumps which is sub-optimal.
 *
 * Based on the above, it is recommended to avoid multiple back-to-back
 * ncsr_xyz() macro usage.
 */

#define ncsr_read(__csr)						\
({									\
	unsigned long __r;						\
	if (kvm_riscv_nacl_available())					\
		__r = nacl_csr_read(nacl_shmem(), __csr);		\
	else								\
		__r = csr_read(__csr);					\
	__r;								\
})

#define ncsr_write(__csr, __val)					\
do {									\
	if (kvm_riscv_nacl_sync_csr_available())			\
		nacl_csr_write(nacl_shmem(), __csr, __val);		\
	else								\
		csr_write(__csr, __val);				\
} while (0)

#define ncsr_swap(__csr, __val)						\
({									\
	unsigned long __r;						\
	if (kvm_riscv_nacl_sync_csr_available())			\
		__r = nacl_csr_swap(nacl_shmem(), __csr, __val);	\
	else								\
		__r = csr_swap(__csr, __val);				\
	__r;								\
})

#define nsync_csr(__csr)						\
do {									\
	if (kvm_riscv_nacl_sync_csr_available())			\
		nacl_sync_csr(__csr);					\
} while (0)

#endif
