/*
 * Kernel-based Virtual Machine driver for Linux
 *
 * This module enables machines with Intel VT-x extensions to run virtual
 * machines without emulation or binary translation.
 *
 * Copyright (C) 2006 Qumranet, Inc.
 *
 * Authors:
 *   Avi Kivity   <avi@qumranet.com>
 *   Yaniv Kamay  <yaniv@qumranet.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#include "kvm.h"
#include "vmx.h"
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/highmem.h>
#include <linux/profile.h>
#include <asm/io.h>
#include <asm/desc.h>

#include "segment_descriptor.h"

MODULE_AUTHOR("Qumranet");
MODULE_LICENSE("GPL");

static DEFINE_PER_CPU(struct vmcs *, vmxarea);
static DEFINE_PER_CPU(struct vmcs *, current_vmcs);

#ifdef CONFIG_X86_64
#define HOST_IS_64 1
#else
#define HOST_IS_64 0
#endif

static struct vmcs_descriptor {
	int size;
	int order;
	u32 revision_id;
} vmcs_descriptor;

#define VMX_SEGMENT_FIELD(seg)					\
	[VCPU_SREG_##seg] = {                                   \
		.selector = GUEST_##seg##_SELECTOR,		\
		.base = GUEST_##seg##_BASE,		   	\
		.limit = GUEST_##seg##_LIMIT,		   	\
		.ar_bytes = GUEST_##seg##_AR_BYTES,	   	\
	}

static struct kvm_vmx_segment_field {
	unsigned selector;
	unsigned base;
	unsigned limit;
	unsigned ar_bytes;
} kvm_vmx_segment_fields[] = {
	VMX_SEGMENT_FIELD(CS),
	VMX_SEGMENT_FIELD(DS),
	VMX_SEGMENT_FIELD(ES),
	VMX_SEGMENT_FIELD(FS),
	VMX_SEGMENT_FIELD(GS),
	VMX_SEGMENT_FIELD(SS),
	VMX_SEGMENT_FIELD(TR),
	VMX_SEGMENT_FIELD(LDTR),
};

/*
 * Keep MSR_K6_STAR at the end, as setup_msrs() will try to optimize it
 * away by decrementing the array size.
 */
static const u32 vmx_msr_index[] = {
#ifdef CONFIG_X86_64
	MSR_SYSCALL_MASK, MSR_LSTAR, MSR_CSTAR, MSR_KERNEL_GS_BASE,
#endif
	MSR_EFER, MSR_K6_STAR,
};
#define NR_VMX_MSR ARRAY_SIZE(vmx_msr_index)

#ifdef CONFIG_X86_64
static unsigned msr_offset_kernel_gs_base;
#define NR_64BIT_MSRS 4
/*
 * avoid save/load MSR_SYSCALL_MASK and MSR_LSTAR by std vt
 * mechanism (cpu bug AA24)
 */
#define NR_BAD_MSRS 2
#else
#define NR_64BIT_MSRS 0
#define NR_BAD_MSRS 0
#endif

static inline int is_page_fault(u32 intr_info)
{
	return (intr_info & (INTR_INFO_INTR_TYPE_MASK | INTR_INFO_VECTOR_MASK |
			     INTR_INFO_VALID_MASK)) ==
		(INTR_TYPE_EXCEPTION | PF_VECTOR | INTR_INFO_VALID_MASK);
}

static inline int is_no_device(u32 intr_info)
{
	return (intr_info & (INTR_INFO_INTR_TYPE_MASK | INTR_INFO_VECTOR_MASK |
			     INTR_INFO_VALID_MASK)) ==
		(INTR_TYPE_EXCEPTION | NM_VECTOR | INTR_INFO_VALID_MASK);
}

static inline int is_external_interrupt(u32 intr_info)
{
	return (intr_info & (INTR_INFO_INTR_TYPE_MASK | INTR_INFO_VALID_MASK))
		== (INTR_TYPE_EXT_INTR | INTR_INFO_VALID_MASK);
}

static struct vmx_msr_entry *find_msr_entry(struct kvm_vcpu *vcpu, u32 msr)
{
	int i;

	for (i = 0; i < vcpu->nmsrs; ++i)
		if (vcpu->guest_msrs[i].index == msr)
			return &vcpu->guest_msrs[i];
	return NULL;
}

static void vmcs_clear(struct vmcs *vmcs)
{
	u64 phys_addr = __pa(vmcs);
	u8 error;

	asm volatile (ASM_VMX_VMCLEAR_RAX "; setna %0"
		      : "=g"(error) : "a"(&phys_addr), "m"(phys_addr)
		      : "cc", "memory");
	if (error)
		printk(KERN_ERR "kvm: vmclear fail: %p/%llx\n",
		       vmcs, phys_addr);
}

static void __vcpu_clear(void *arg)
{
	struct kvm_vcpu *vcpu = arg;
	int cpu = raw_smp_processor_id();

	if (vcpu->cpu == cpu)
		vmcs_clear(vcpu->vmcs);
	if (per_cpu(current_vmcs, cpu) == vcpu->vmcs)
		per_cpu(current_vmcs, cpu) = NULL;
}

static void vcpu_clear(struct kvm_vcpu *vcpu)
{
	if (vcpu->cpu != raw_smp_processor_id() && vcpu->cpu != -1)
		smp_call_function_single(vcpu->cpu, __vcpu_clear, vcpu, 0, 1);
	else
		__vcpu_clear(vcpu);
	vcpu->launched = 0;
}

static unsigned long vmcs_readl(unsigned long field)
{
	unsigned long value;

	asm volatile (ASM_VMX_VMREAD_RDX_RAX
		      : "=a"(value) : "d"(field) : "cc");
	return value;
}

static u16 vmcs_read16(unsigned long field)
{
	return vmcs_readl(field);
}

static u32 vmcs_read32(unsigned long field)
{
	return vmcs_readl(field);
}

static u64 vmcs_read64(unsigned long field)
{
#ifdef CONFIG_X86_64
	return vmcs_readl(field);
#else
	return vmcs_readl(field) | ((u64)vmcs_readl(field+1) << 32);
#endif
}

static noinline void vmwrite_error(unsigned long field, unsigned long value)
{
	printk(KERN_ERR "vmwrite error: reg %lx value %lx (err %d)\n",
	       field, value, vmcs_read32(VM_INSTRUCTION_ERROR));
	dump_stack();
}

static void vmcs_writel(unsigned long field, unsigned long value)
{
	u8 error;

	asm volatile (ASM_VMX_VMWRITE_RAX_RDX "; setna %0"
		       : "=q"(error) : "a"(value), "d"(field) : "cc" );
	if (unlikely(error))
		vmwrite_error(field, value);
}

static void vmcs_write16(unsigned long field, u16 value)
{
	vmcs_writel(field, value);
}

static void vmcs_write32(unsigned long field, u32 value)
{
	vmcs_writel(field, value);
}

static void vmcs_write64(unsigned long field, u64 value)
{
#ifdef CONFIG_X86_64
	vmcs_writel(field, value);
#else
	vmcs_writel(field, value);
	asm volatile ("");
	vmcs_writel(field+1, value >> 32);
#endif
}

static void vmcs_clear_bits(unsigned long field, u32 mask)
{
	vmcs_writel(field, vmcs_readl(field) & ~mask);
}

static void vmcs_set_bits(unsigned long field, u32 mask)
{
	vmcs_writel(field, vmcs_readl(field) | mask);
}

/*
 * Switches to specified vcpu, until a matching vcpu_put(), but assumes
 * vcpu mutex is already taken.
 */
static void vmx_vcpu_load(struct kvm_vcpu *vcpu)
{
	u64 phys_addr = __pa(vcpu->vmcs);
	int cpu;

	cpu = get_cpu();

	if (vcpu->cpu != cpu)
		vcpu_clear(vcpu);

	if (per_cpu(current_vmcs, cpu) != vcpu->vmcs) {
		u8 error;

		per_cpu(current_vmcs, cpu) = vcpu->vmcs;
		asm volatile (ASM_VMX_VMPTRLD_RAX "; setna %0"
			      : "=g"(error) : "a"(&phys_addr), "m"(phys_addr)
			      : "cc");
		if (error)
			printk(KERN_ERR "kvm: vmptrld %p/%llx fail\n",
			       vcpu->vmcs, phys_addr);
	}

	if (vcpu->cpu != cpu) {
		struct descriptor_table dt;
		unsigned long sysenter_esp;

		vcpu->cpu = cpu;
		/*
		 * Linux uses per-cpu TSS and GDT, so set these when switching
		 * processors.
		 */
		vmcs_writel(HOST_TR_BASE, read_tr_base()); /* 22.2.4 */
		get_gdt(&dt);
		vmcs_writel(HOST_GDTR_BASE, dt.base);   /* 22.2.4 */

		rdmsrl(MSR_IA32_SYSENTER_ESP, sysenter_esp);
		vmcs_writel(HOST_IA32_SYSENTER_ESP, sysenter_esp); /* 22.2.3 */
	}
}

static void vmx_vcpu_put(struct kvm_vcpu *vcpu)
{
	put_cpu();
}

static void vmx_vcpu_decache(struct kvm_vcpu *vcpu)
{
	vcpu_clear(vcpu);
}

static unsigned long vmx_get_rflags(struct kvm_vcpu *vcpu)
{
	return vmcs_readl(GUEST_RFLAGS);
}

static void vmx_set_rflags(struct kvm_vcpu *vcpu, unsigned long rflags)
{
	vmcs_writel(GUEST_RFLAGS, rflags);
}

static void skip_emulated_instruction(struct kvm_vcpu *vcpu)
{
	unsigned long rip;
	u32 interruptibility;

	rip = vmcs_readl(GUEST_RIP);
	rip += vmcs_read32(VM_EXIT_INSTRUCTION_LEN);
	vmcs_writel(GUEST_RIP, rip);

	/*
	 * We emulated an instruction, so temporary interrupt blocking
	 * should be removed, if set.
	 */
	interruptibility = vmcs_read32(GUEST_INTERRUPTIBILITY_INFO);
	if (interruptibility & 3)
		vmcs_write32(GUEST_INTERRUPTIBILITY_INFO,
			     interruptibility & ~3);
	vcpu->interrupt_window_open = 1;
}

static void vmx_inject_gp(struct kvm_vcpu *vcpu, unsigned error_code)
{
	printk(KERN_DEBUG "inject_general_protection: rip 0x%lx\n",
	       vmcs_readl(GUEST_RIP));
	vmcs_write32(VM_ENTRY_EXCEPTION_ERROR_CODE, error_code);
	vmcs_write32(VM_ENTRY_INTR_INFO_FIELD,
		     GP_VECTOR |
		     INTR_TYPE_EXCEPTION |
		     INTR_INFO_DELIEVER_CODE_MASK |
		     INTR_INFO_VALID_MASK);
}

/*
 * Set up the vmcs to automatically save and restore system
 * msrs.  Don't touch the 64-bit msrs if the guest is in legacy
 * mode, as fiddling with msrs is very expensive.
 */
static void setup_msrs(struct kvm_vcpu *vcpu)
{
	int nr_skip, nr_good_msrs;

	if (is_long_mode(vcpu))
		nr_skip = NR_BAD_MSRS;
	else
		nr_skip = NR_64BIT_MSRS;
	nr_good_msrs = vcpu->nmsrs - nr_skip;

	/*
	 * MSR_K6_STAR is only needed on long mode guests, and only
	 * if efer.sce is enabled.
	 */
	if (find_msr_entry(vcpu, MSR_K6_STAR)) {
		--nr_good_msrs;
#ifdef CONFIG_X86_64
		if (is_long_mode(vcpu) && (vcpu->shadow_efer & EFER_SCE))
			++nr_good_msrs;
#endif
	}

	vmcs_writel(VM_ENTRY_MSR_LOAD_ADDR,
		    virt_to_phys(vcpu->guest_msrs + nr_skip));
	vmcs_writel(VM_EXIT_MSR_STORE_ADDR,
		    virt_to_phys(vcpu->guest_msrs + nr_skip));
	vmcs_writel(VM_EXIT_MSR_LOAD_ADDR,
		    virt_to_phys(vcpu->host_msrs + nr_skip));
	vmcs_write32(VM_EXIT_MSR_STORE_COUNT, nr_good_msrs); /* 22.2.2 */
	vmcs_write32(VM_EXIT_MSR_LOAD_COUNT, nr_good_msrs);  /* 22.2.2 */
	vmcs_write32(VM_ENTRY_MSR_LOAD_COUNT, nr_good_msrs); /* 22.2.2 */
}

/*
 * reads and returns guest's timestamp counter "register"
 * guest_tsc = host_tsc + tsc_offset    -- 21.3
 */
static u64 guest_read_tsc(void)
{
	u64 host_tsc, tsc_offset;

	rdtscll(host_tsc);
	tsc_offset = vmcs_read64(TSC_OFFSET);
	return host_tsc + tsc_offset;
}

/*
 * writes 'guest_tsc' into guest's timestamp counter "register"
 * guest_tsc = host_tsc + tsc_offset ==> tsc_offset = guest_tsc - host_tsc
 */
static void guest_write_tsc(u64 guest_tsc)
{
	u64 host_tsc;

	rdtscll(host_tsc);
	vmcs_write64(TSC_OFFSET, guest_tsc - host_tsc);
}

static void reload_tss(void)
{
#ifndef CONFIG_X86_64

	/*
	 * VT restores TR but not its size.  Useless.
	 */
	struct descriptor_table gdt;
	struct segment_descriptor *descs;

	get_gdt(&gdt);
	descs = (void *)gdt.base;
	descs[GDT_ENTRY_TSS].type = 9; /* available TSS */
	load_TR_desc();
#endif
}

/*
 * Reads an msr value (of 'msr_index') into 'pdata'.
 * Returns 0 on success, non-0 otherwise.
 * Assumes vcpu_load() was already called.
 */
static int vmx_get_msr(struct kvm_vcpu *vcpu, u32 msr_index, u64 *pdata)
{
	u64 data;
	struct vmx_msr_entry *msr;

	if (!pdata) {
		printk(KERN_ERR "BUG: get_msr called with NULL pdata\n");
		return -EINVAL;
	}

	switch (msr_index) {
#ifdef CONFIG_X86_64
	case MSR_FS_BASE:
		data = vmcs_readl(GUEST_FS_BASE);
		break;
	case MSR_GS_BASE:
		data = vmcs_readl(GUEST_GS_BASE);
		break;
	case MSR_EFER:
		return kvm_get_msr_common(vcpu, msr_index, pdata);
#endif
	case MSR_IA32_TIME_STAMP_COUNTER:
		data = guest_read_tsc();
		break;
	case MSR_IA32_SYSENTER_CS:
		data = vmcs_read32(GUEST_SYSENTER_CS);
		break;
	case MSR_IA32_SYSENTER_EIP:
		data = vmcs_readl(GUEST_SYSENTER_EIP);
		break;
	case MSR_IA32_SYSENTER_ESP:
		data = vmcs_readl(GUEST_SYSENTER_ESP);
		break;
	default:
		msr = find_msr_entry(vcpu, msr_index);
		if (msr) {
			data = msr->data;
			break;
		}
		return kvm_get_msr_common(vcpu, msr_index, pdata);
	}

	*pdata = data;
	return 0;
}

/*
 * Writes msr value into into the appropriate "register".
 * Returns 0 on success, non-0 otherwise.
 * Assumes vcpu_load() was already called.
 */
static int vmx_set_msr(struct kvm_vcpu *vcpu, u32 msr_index, u64 data)
{
	struct vmx_msr_entry *msr;
	switch (msr_index) {
#ifdef CONFIG_X86_64
	case MSR_EFER:
		return kvm_set_msr_common(vcpu, msr_index, data);
	case MSR_FS_BASE:
		vmcs_writel(GUEST_FS_BASE, data);
		break;
	case MSR_GS_BASE:
		vmcs_writel(GUEST_GS_BASE, data);
		break;
#endif
	case MSR_IA32_SYSENTER_CS:
		vmcs_write32(GUEST_SYSENTER_CS, data);
		break;
	case MSR_IA32_SYSENTER_EIP:
		vmcs_writel(GUEST_SYSENTER_EIP, data);
		break;
	case MSR_IA32_SYSENTER_ESP:
		vmcs_writel(GUEST_SYSENTER_ESP, data);
		break;
	case MSR_IA32_TIME_STAMP_COUNTER:
		guest_write_tsc(data);
		break;
	default:
		msr = find_msr_entry(vcpu, msr_index);
		if (msr) {
			msr->data = data;
			break;
		}
		return kvm_set_msr_common(vcpu, msr_index, data);
		msr->data = data;
		break;
	}

	return 0;
}

/*
 * Sync the rsp and rip registers into the vcpu structure.  This allows
 * registers to be accessed by indexing vcpu->regs.
 */
static void vcpu_load_rsp_rip(struct kvm_vcpu *vcpu)
{
	vcpu->regs[VCPU_REGS_RSP] = vmcs_readl(GUEST_RSP);
	vcpu->rip = vmcs_readl(GUEST_RIP);
}

/*
 * Syncs rsp and rip back into the vmcs.  Should be called after possible
 * modification.
 */
static void vcpu_put_rsp_rip(struct kvm_vcpu *vcpu)
{
	vmcs_writel(GUEST_RSP, vcpu->regs[VCPU_REGS_RSP]);
	vmcs_writel(GUEST_RIP, vcpu->rip);
}

static int set_guest_debug(struct kvm_vcpu *vcpu, struct kvm_debug_guest *dbg)
{
	unsigned long dr7 = 0x400;
	u32 exception_bitmap;
	int old_singlestep;

	exception_bitmap = vmcs_read32(EXCEPTION_BITMAP);
	old_singlestep = vcpu->guest_debug.singlestep;

	vcpu->guest_debug.enabled = dbg->enabled;
	if (vcpu->guest_debug.enabled) {
		int i;

		dr7 |= 0x200;  /* exact */
		for (i = 0; i < 4; ++i) {
			if (!dbg->breakpoints[i].enabled)
				continue;
			vcpu->guest_debug.bp[i] = dbg->breakpoints[i].address;
			dr7 |= 2 << (i*2);    /* global enable */
			dr7 |= 0 << (i*4+16); /* execution breakpoint */
		}

		exception_bitmap |= (1u << 1);  /* Trap debug exceptions */

		vcpu->guest_debug.singlestep = dbg->singlestep;
	} else {
		exception_bitmap &= ~(1u << 1); /* Ignore debug exceptions */
		vcpu->guest_debug.singlestep = 0;
	}

	if (old_singlestep && !vcpu->guest_debug.singlestep) {
		unsigned long flags;

		flags = vmcs_readl(GUEST_RFLAGS);
		flags &= ~(X86_EFLAGS_TF | X86_EFLAGS_RF);
		vmcs_writel(GUEST_RFLAGS, flags);
	}

	vmcs_write32(EXCEPTION_BITMAP, exception_bitmap);
	vmcs_writel(GUEST_DR7, dr7);

	return 0;
}

static __init int cpu_has_kvm_support(void)
{
	unsigned long ecx = cpuid_ecx(1);
	return test_bit(5, &ecx); /* CPUID.1:ECX.VMX[bit 5] -> VT */
}

static __init int vmx_disabled_by_bios(void)
{
	u64 msr;

	rdmsrl(MSR_IA32_FEATURE_CONTROL, msr);
	return (msr & 5) == 1; /* locked but not enabled */
}

static void hardware_enable(void *garbage)
{
	int cpu = raw_smp_processor_id();
	u64 phys_addr = __pa(per_cpu(vmxarea, cpu));
	u64 old;

	rdmsrl(MSR_IA32_FEATURE_CONTROL, old);
	if ((old & 5) != 5)
		/* enable and lock */
		wrmsrl(MSR_IA32_FEATURE_CONTROL, old | 5);
	write_cr4(read_cr4() | CR4_VMXE); /* FIXME: not cpu hotplug safe */
	asm volatile (ASM_VMX_VMXON_RAX : : "a"(&phys_addr), "m"(phys_addr)
		      : "memory", "cc");
}

static void hardware_disable(void *garbage)
{
	asm volatile (ASM_VMX_VMXOFF : : : "cc");
}

static __init void setup_vmcs_descriptor(void)
{
	u32 vmx_msr_low, vmx_msr_high;

	rdmsr(MSR_IA32_VMX_BASIC, vmx_msr_low, vmx_msr_high);
	vmcs_descriptor.size = vmx_msr_high & 0x1fff;
	vmcs_descriptor.order = get_order(vmcs_descriptor.size);
	vmcs_descriptor.revision_id = vmx_msr_low;
}

static struct vmcs *alloc_vmcs_cpu(int cpu)
{
	int node = cpu_to_node(cpu);
	struct page *pages;
	struct vmcs *vmcs;

	pages = alloc_pages_node(node, GFP_KERNEL, vmcs_descriptor.order);
	if (!pages)
		return NULL;
	vmcs = page_address(pages);
	memset(vmcs, 0, vmcs_descriptor.size);
	vmcs->revision_id = vmcs_descriptor.revision_id; /* vmcs revision id */
	return vmcs;
}

static struct vmcs *alloc_vmcs(void)
{
	return alloc_vmcs_cpu(raw_smp_processor_id());
}

static void free_vmcs(struct vmcs *vmcs)
{
	free_pages((unsigned long)vmcs, vmcs_descriptor.order);
}

static __exit void free_kvm_area(void)
{
	int cpu;

	for_each_online_cpu(cpu)
		free_vmcs(per_cpu(vmxarea, cpu));
}

extern struct vmcs *alloc_vmcs_cpu(int cpu);

static __init int alloc_kvm_area(void)
{
	int cpu;

	for_each_online_cpu(cpu) {
		struct vmcs *vmcs;

		vmcs = alloc_vmcs_cpu(cpu);
		if (!vmcs) {
			free_kvm_area();
			return -ENOMEM;
		}

		per_cpu(vmxarea, cpu) = vmcs;
	}
	return 0;
}

static __init int hardware_setup(void)
{
	setup_vmcs_descriptor();
	return alloc_kvm_area();
}

static __exit void hardware_unsetup(void)
{
	free_kvm_area();
}

static void update_exception_bitmap(struct kvm_vcpu *vcpu)
{
	if (vcpu->rmode.active)
		vmcs_write32(EXCEPTION_BITMAP, ~0);
	else
		vmcs_write32(EXCEPTION_BITMAP, 1 << PF_VECTOR);
}

static void fix_pmode_dataseg(int seg, struct kvm_save_segment *save)
{
	struct kvm_vmx_segment_field *sf = &kvm_vmx_segment_fields[seg];

	if (vmcs_readl(sf->base) == save->base && (save->base & AR_S_MASK)) {
		vmcs_write16(sf->selector, save->selector);
		vmcs_writel(sf->base, save->base);
		vmcs_write32(sf->limit, save->limit);
		vmcs_write32(sf->ar_bytes, save->ar);
	} else {
		u32 dpl = (vmcs_read16(sf->selector) & SELECTOR_RPL_MASK)
			<< AR_DPL_SHIFT;
		vmcs_write32(sf->ar_bytes, 0x93 | dpl);
	}
}

static void enter_pmode(struct kvm_vcpu *vcpu)
{
	unsigned long flags;

	vcpu->rmode.active = 0;

	vmcs_writel(GUEST_TR_BASE, vcpu->rmode.tr.base);
	vmcs_write32(GUEST_TR_LIMIT, vcpu->rmode.tr.limit);
	vmcs_write32(GUEST_TR_AR_BYTES, vcpu->rmode.tr.ar);

	flags = vmcs_readl(GUEST_RFLAGS);
	flags &= ~(IOPL_MASK | X86_EFLAGS_VM);
	flags |= (vcpu->rmode.save_iopl << IOPL_SHIFT);
	vmcs_writel(GUEST_RFLAGS, flags);

	vmcs_writel(GUEST_CR4, (vmcs_readl(GUEST_CR4) & ~CR4_VME_MASK) |
			(vmcs_readl(CR4_READ_SHADOW) & CR4_VME_MASK));

	update_exception_bitmap(vcpu);

	fix_pmode_dataseg(VCPU_SREG_ES, &vcpu->rmode.es);
	fix_pmode_dataseg(VCPU_SREG_DS, &vcpu->rmode.ds);
	fix_pmode_dataseg(VCPU_SREG_GS, &vcpu->rmode.gs);
	fix_pmode_dataseg(VCPU_SREG_FS, &vcpu->rmode.fs);

	vmcs_write16(GUEST_SS_SELECTOR, 0);
	vmcs_write32(GUEST_SS_AR_BYTES, 0x93);

	vmcs_write16(GUEST_CS_SELECTOR,
		     vmcs_read16(GUEST_CS_SELECTOR) & ~SELECTOR_RPL_MASK);
	vmcs_write32(GUEST_CS_AR_BYTES, 0x9b);
}

static int rmode_tss_base(struct kvm* kvm)
{
	gfn_t base_gfn = kvm->memslots[0].base_gfn + kvm->memslots[0].npages - 3;
	return base_gfn << PAGE_SHIFT;
}

static void fix_rmode_seg(int seg, struct kvm_save_segment *save)
{
	struct kvm_vmx_segment_field *sf = &kvm_vmx_segment_fields[seg];

	save->selector = vmcs_read16(sf->selector);
	save->base = vmcs_readl(sf->base);
	save->limit = vmcs_read32(sf->limit);
	save->ar = vmcs_read32(sf->ar_bytes);
	vmcs_write16(sf->selector, vmcs_readl(sf->base) >> 4);
	vmcs_write32(sf->limit, 0xffff);
	vmcs_write32(sf->ar_bytes, 0xf3);
}

static void enter_rmode(struct kvm_vcpu *vcpu)
{
	unsigned long flags;

	vcpu->rmode.active = 1;

	vcpu->rmode.tr.base = vmcs_readl(GUEST_TR_BASE);
	vmcs_writel(GUEST_TR_BASE, rmode_tss_base(vcpu->kvm));

	vcpu->rmode.tr.limit = vmcs_read32(GUEST_TR_LIMIT);
	vmcs_write32(GUEST_TR_LIMIT, RMODE_TSS_SIZE - 1);

	vcpu->rmode.tr.ar = vmcs_read32(GUEST_TR_AR_BYTES);
	vmcs_write32(GUEST_TR_AR_BYTES, 0x008b);

	flags = vmcs_readl(GUEST_RFLAGS);
	vcpu->rmode.save_iopl = (flags & IOPL_MASK) >> IOPL_SHIFT;

	flags |= IOPL_MASK | X86_EFLAGS_VM;

	vmcs_writel(GUEST_RFLAGS, flags);
	vmcs_writel(GUEST_CR4, vmcs_readl(GUEST_CR4) | CR4_VME_MASK);
	update_exception_bitmap(vcpu);

	vmcs_write16(GUEST_SS_SELECTOR, vmcs_readl(GUEST_SS_BASE) >> 4);
	vmcs_write32(GUEST_SS_LIMIT, 0xffff);
	vmcs_write32(GUEST_SS_AR_BYTES, 0xf3);

	vmcs_write32(GUEST_CS_AR_BYTES, 0xf3);
	vmcs_write32(GUEST_CS_LIMIT, 0xffff);
	if (vmcs_readl(GUEST_CS_BASE) == 0xffff0000)
		vmcs_writel(GUEST_CS_BASE, 0xf0000);
	vmcs_write16(GUEST_CS_SELECTOR, vmcs_readl(GUEST_CS_BASE) >> 4);

	fix_rmode_seg(VCPU_SREG_ES, &vcpu->rmode.es);
	fix_rmode_seg(VCPU_SREG_DS, &vcpu->rmode.ds);
	fix_rmode_seg(VCPU_SREG_GS, &vcpu->rmode.gs);
	fix_rmode_seg(VCPU_SREG_FS, &vcpu->rmode.fs);
}

#ifdef CONFIG_X86_64

static void enter_lmode(struct kvm_vcpu *vcpu)
{
	u32 guest_tr_ar;

	guest_tr_ar = vmcs_read32(GUEST_TR_AR_BYTES);
	if ((guest_tr_ar & AR_TYPE_MASK) != AR_TYPE_BUSY_64_TSS) {
		printk(KERN_DEBUG "%s: tss fixup for long mode. \n",
		       __FUNCTION__);
		vmcs_write32(GUEST_TR_AR_BYTES,
			     (guest_tr_ar & ~AR_TYPE_MASK)
			     | AR_TYPE_BUSY_64_TSS);
	}

	vcpu->shadow_efer |= EFER_LMA;

	find_msr_entry(vcpu, MSR_EFER)->data |= EFER_LMA | EFER_LME;
	vmcs_write32(VM_ENTRY_CONTROLS,
		     vmcs_read32(VM_ENTRY_CONTROLS)
		     | VM_ENTRY_CONTROLS_IA32E_MASK);
}

static void exit_lmode(struct kvm_vcpu *vcpu)
{
	vcpu->shadow_efer &= ~EFER_LMA;

	vmcs_write32(VM_ENTRY_CONTROLS,
		     vmcs_read32(VM_ENTRY_CONTROLS)
		     & ~VM_ENTRY_CONTROLS_IA32E_MASK);
}

#endif

static void vmx_decache_cr4_guest_bits(struct kvm_vcpu *vcpu)
{
	vcpu->cr4 &= KVM_GUEST_CR4_MASK;
	vcpu->cr4 |= vmcs_readl(GUEST_CR4) & ~KVM_GUEST_CR4_MASK;
}

static void vmx_set_cr0(struct kvm_vcpu *vcpu, unsigned long cr0)
{
	if (vcpu->rmode.active && (cr0 & CR0_PE_MASK))
		enter_pmode(vcpu);

	if (!vcpu->rmode.active && !(cr0 & CR0_PE_MASK))
		enter_rmode(vcpu);

#ifdef CONFIG_X86_64
	if (vcpu->shadow_efer & EFER_LME) {
		if (!is_paging(vcpu) && (cr0 & CR0_PG_MASK))
			enter_lmode(vcpu);
		if (is_paging(vcpu) && !(cr0 & CR0_PG_MASK))
			exit_lmode(vcpu);
	}
#endif

	if (!(cr0 & CR0_TS_MASK)) {
		vcpu->fpu_active = 1;
		vmcs_clear_bits(EXCEPTION_BITMAP, CR0_TS_MASK);
	}

	vmcs_writel(CR0_READ_SHADOW, cr0);
	vmcs_writel(GUEST_CR0,
		    (cr0 & ~KVM_GUEST_CR0_MASK) | KVM_VM_CR0_ALWAYS_ON);
	vcpu->cr0 = cr0;
}

static void vmx_set_cr3(struct kvm_vcpu *vcpu, unsigned long cr3)
{
	vmcs_writel(GUEST_CR3, cr3);

	if (!(vcpu->cr0 & CR0_TS_MASK)) {
		vcpu->fpu_active = 0;
		vmcs_set_bits(GUEST_CR0, CR0_TS_MASK);
		vmcs_set_bits(EXCEPTION_BITMAP, 1 << NM_VECTOR);
	}
}

static void vmx_set_cr4(struct kvm_vcpu *vcpu, unsigned long cr4)
{
	vmcs_writel(CR4_READ_SHADOW, cr4);
	vmcs_writel(GUEST_CR4, cr4 | (vcpu->rmode.active ?
		    KVM_RMODE_VM_CR4_ALWAYS_ON : KVM_PMODE_VM_CR4_ALWAYS_ON));
	vcpu->cr4 = cr4;
}

#ifdef CONFIG_X86_64

static void vmx_set_efer(struct kvm_vcpu *vcpu, u64 efer)
{
	struct vmx_msr_entry *msr = find_msr_entry(vcpu, MSR_EFER);

	vcpu->shadow_efer = efer;
	if (efer & EFER_LMA) {
		vmcs_write32(VM_ENTRY_CONTROLS,
				     vmcs_read32(VM_ENTRY_CONTROLS) |
				     VM_ENTRY_CONTROLS_IA32E_MASK);
		msr->data = efer;

	} else {
		vmcs_write32(VM_ENTRY_CONTROLS,
				     vmcs_read32(VM_ENTRY_CONTROLS) &
				     ~VM_ENTRY_CONTROLS_IA32E_MASK);

		msr->data = efer & ~EFER_LME;
	}
	setup_msrs(vcpu);
}

#endif

static u64 vmx_get_segment_base(struct kvm_vcpu *vcpu, int seg)
{
	struct kvm_vmx_segment_field *sf = &kvm_vmx_segment_fields[seg];

	return vmcs_readl(sf->base);
}

static void vmx_get_segment(struct kvm_vcpu *vcpu,
			    struct kvm_segment *var, int seg)
{
	struct kvm_vmx_segment_field *sf = &kvm_vmx_segment_fields[seg];
	u32 ar;

	var->base = vmcs_readl(sf->base);
	var->limit = vmcs_read32(sf->limit);
	var->selector = vmcs_read16(sf->selector);
	ar = vmcs_read32(sf->ar_bytes);
	if (ar & AR_UNUSABLE_MASK)
		ar = 0;
	var->type = ar & 15;
	var->s = (ar >> 4) & 1;
	var->dpl = (ar >> 5) & 3;
	var->present = (ar >> 7) & 1;
	var->avl = (ar >> 12) & 1;
	var->l = (ar >> 13) & 1;
	var->db = (ar >> 14) & 1;
	var->g = (ar >> 15) & 1;
	var->unusable = (ar >> 16) & 1;
}

static void vmx_set_segment(struct kvm_vcpu *vcpu,
			    struct kvm_segment *var, int seg)
{
	struct kvm_vmx_segment_field *sf = &kvm_vmx_segment_fields[seg];
	u32 ar;

	vmcs_writel(sf->base, var->base);
	vmcs_write32(sf->limit, var->limit);
	vmcs_write16(sf->selector, var->selector);
	if (vcpu->rmode.active && var->s) {
		/*
		 * Hack real-mode segments into vm86 compatibility.
		 */
		if (var->base == 0xffff0000 && var->selector == 0xf000)
			vmcs_writel(sf->base, 0xf0000);
		ar = 0xf3;
	} else if (var->unusable)
		ar = 1 << 16;
	else {
		ar = var->type & 15;
		ar |= (var->s & 1) << 4;
		ar |= (var->dpl & 3) << 5;
		ar |= (var->present & 1) << 7;
		ar |= (var->avl & 1) << 12;
		ar |= (var->l & 1) << 13;
		ar |= (var->db & 1) << 14;
		ar |= (var->g & 1) << 15;
	}
	if (ar == 0) /* a 0 value means unusable */
		ar = AR_UNUSABLE_MASK;
	vmcs_write32(sf->ar_bytes, ar);
}

static void vmx_get_cs_db_l_bits(struct kvm_vcpu *vcpu, int *db, int *l)
{
	u32 ar = vmcs_read32(GUEST_CS_AR_BYTES);

	*db = (ar >> 14) & 1;
	*l = (ar >> 13) & 1;
}

static void vmx_get_idt(struct kvm_vcpu *vcpu, struct descriptor_table *dt)
{
	dt->limit = vmcs_read32(GUEST_IDTR_LIMIT);
	dt->base = vmcs_readl(GUEST_IDTR_BASE);
}

static void vmx_set_idt(struct kvm_vcpu *vcpu, struct descriptor_table *dt)
{
	vmcs_write32(GUEST_IDTR_LIMIT, dt->limit);
	vmcs_writel(GUEST_IDTR_BASE, dt->base);
}

static void vmx_get_gdt(struct kvm_vcpu *vcpu, struct descriptor_table *dt)
{
	dt->limit = vmcs_read32(GUEST_GDTR_LIMIT);
	dt->base = vmcs_readl(GUEST_GDTR_BASE);
}

static void vmx_set_gdt(struct kvm_vcpu *vcpu, struct descriptor_table *dt)
{
	vmcs_write32(GUEST_GDTR_LIMIT, dt->limit);
	vmcs_writel(GUEST_GDTR_BASE, dt->base);
}

static int init_rmode_tss(struct kvm* kvm)
{
	struct page *p1, *p2, *p3;
	gfn_t fn = rmode_tss_base(kvm) >> PAGE_SHIFT;
	char *page;

	p1 = gfn_to_page(kvm, fn++);
	p2 = gfn_to_page(kvm, fn++);
	p3 = gfn_to_page(kvm, fn);

	if (!p1 || !p2 || !p3) {
		kvm_printf(kvm,"%s: gfn_to_page failed\n", __FUNCTION__);
		return 0;
	}

	page = kmap_atomic(p1, KM_USER0);
	memset(page, 0, PAGE_SIZE);
	*(u16*)(page + 0x66) = TSS_BASE_SIZE + TSS_REDIRECTION_SIZE;
	kunmap_atomic(page, KM_USER0);

	page = kmap_atomic(p2, KM_USER0);
	memset(page, 0, PAGE_SIZE);
	kunmap_atomic(page, KM_USER0);

	page = kmap_atomic(p3, KM_USER0);
	memset(page, 0, PAGE_SIZE);
	*(page + RMODE_TSS_SIZE - 2 * PAGE_SIZE - 1) = ~0;
	kunmap_atomic(page, KM_USER0);

	return 1;
}

static void vmcs_write32_fixedbits(u32 msr, u32 vmcs_field, u32 val)
{
	u32 msr_high, msr_low;

	rdmsr(msr, msr_low, msr_high);

	val &= msr_high;
	val |= msr_low;
	vmcs_write32(vmcs_field, val);
}

static void seg_setup(int seg)
{
	struct kvm_vmx_segment_field *sf = &kvm_vmx_segment_fields[seg];

	vmcs_write16(sf->selector, 0);
	vmcs_writel(sf->base, 0);
	vmcs_write32(sf->limit, 0xffff);
	vmcs_write32(sf->ar_bytes, 0x93);
}

/*
 * Sets up the vmcs for emulated real mode.
 */
static int vmx_vcpu_setup(struct kvm_vcpu *vcpu)
{
	u32 host_sysenter_cs;
	u32 junk;
	unsigned long a;
	struct descriptor_table dt;
	int i;
	int ret = 0;
	extern asmlinkage void kvm_vmx_return(void);

	if (!init_rmode_tss(vcpu->kvm)) {
		ret = -ENOMEM;
		goto out;
	}

	memset(vcpu->regs, 0, sizeof(vcpu->regs));
	vcpu->regs[VCPU_REGS_RDX] = get_rdx_init_val();
	vcpu->cr8 = 0;
	vcpu->apic_base = 0xfee00000 |
			/*for vcpu 0*/ MSR_IA32_APICBASE_BSP |
			MSR_IA32_APICBASE_ENABLE;

	fx_init(vcpu);

	/*
	 * GUEST_CS_BASE should really be 0xffff0000, but VT vm86 mode
	 * insists on having GUEST_CS_BASE == GUEST_CS_SELECTOR << 4.  Sigh.
	 */
	vmcs_write16(GUEST_CS_SELECTOR, 0xf000);
	vmcs_writel(GUEST_CS_BASE, 0x000f0000);
	vmcs_write32(GUEST_CS_LIMIT, 0xffff);
	vmcs_write32(GUEST_CS_AR_BYTES, 0x9b);

	seg_setup(VCPU_SREG_DS);
	seg_setup(VCPU_SREG_ES);
	seg_setup(VCPU_SREG_FS);
	seg_setup(VCPU_SREG_GS);
	seg_setup(VCPU_SREG_SS);

	vmcs_write16(GUEST_TR_SELECTOR, 0);
	vmcs_writel(GUEST_TR_BASE, 0);
	vmcs_write32(GUEST_TR_LIMIT, 0xffff);
	vmcs_write32(GUEST_TR_AR_BYTES, 0x008b);

	vmcs_write16(GUEST_LDTR_SELECTOR, 0);
	vmcs_writel(GUEST_LDTR_BASE, 0);
	vmcs_write32(GUEST_LDTR_LIMIT, 0xffff);
	vmcs_write32(GUEST_LDTR_AR_BYTES, 0x00082);

	vmcs_write32(GUEST_SYSENTER_CS, 0);
	vmcs_writel(GUEST_SYSENTER_ESP, 0);
	vmcs_writel(GUEST_SYSENTER_EIP, 0);

	vmcs_writel(GUEST_RFLAGS, 0x02);
	vmcs_writel(GUEST_RIP, 0xfff0);
	vmcs_writel(GUEST_RSP, 0);

	//todo: dr0 = dr1 = dr2 = dr3 = 0; dr6 = 0xffff0ff0
	vmcs_writel(GUEST_DR7, 0x400);

	vmcs_writel(GUEST_GDTR_BASE, 0);
	vmcs_write32(GUEST_GDTR_LIMIT, 0xffff);

	vmcs_writel(GUEST_IDTR_BASE, 0);
	vmcs_write32(GUEST_IDTR_LIMIT, 0xffff);

	vmcs_write32(GUEST_ACTIVITY_STATE, 0);
	vmcs_write32(GUEST_INTERRUPTIBILITY_INFO, 0);
	vmcs_write32(GUEST_PENDING_DBG_EXCEPTIONS, 0);

	/* I/O */
	vmcs_write64(IO_BITMAP_A, 0);
	vmcs_write64(IO_BITMAP_B, 0);

	guest_write_tsc(0);

	vmcs_write64(VMCS_LINK_POINTER, -1ull); /* 22.3.1.5 */

	/* Special registers */
	vmcs_write64(GUEST_IA32_DEBUGCTL, 0);

	/* Control */
	vmcs_write32_fixedbits(MSR_IA32_VMX_PINBASED_CTLS,
			       PIN_BASED_VM_EXEC_CONTROL,
			       PIN_BASED_EXT_INTR_MASK   /* 20.6.1 */
			       | PIN_BASED_NMI_EXITING   /* 20.6.1 */
			);
	vmcs_write32_fixedbits(MSR_IA32_VMX_PROCBASED_CTLS,
			       CPU_BASED_VM_EXEC_CONTROL,
			       CPU_BASED_HLT_EXITING         /* 20.6.2 */
			       | CPU_BASED_CR8_LOAD_EXITING    /* 20.6.2 */
			       | CPU_BASED_CR8_STORE_EXITING   /* 20.6.2 */
			       | CPU_BASED_UNCOND_IO_EXITING   /* 20.6.2 */
			       | CPU_BASED_MOV_DR_EXITING
			       | CPU_BASED_USE_TSC_OFFSETING   /* 21.3 */
			);

	vmcs_write32(EXCEPTION_BITMAP, 1 << PF_VECTOR);
	vmcs_write32(PAGE_FAULT_ERROR_CODE_MASK, 0);
	vmcs_write32(PAGE_FAULT_ERROR_CODE_MATCH, 0);
	vmcs_write32(CR3_TARGET_COUNT, 0);           /* 22.2.1 */

	vmcs_writel(HOST_CR0, read_cr0());  /* 22.2.3 */
	vmcs_writel(HOST_CR4, read_cr4());  /* 22.2.3, 22.2.5 */
	vmcs_writel(HOST_CR3, read_cr3());  /* 22.2.3  FIXME: shadow tables */

	vmcs_write16(HOST_CS_SELECTOR, __KERNEL_CS);  /* 22.2.4 */
	vmcs_write16(HOST_DS_SELECTOR, __KERNEL_DS);  /* 22.2.4 */
	vmcs_write16(HOST_ES_SELECTOR, __KERNEL_DS);  /* 22.2.4 */
	vmcs_write16(HOST_FS_SELECTOR, read_fs());    /* 22.2.4 */
	vmcs_write16(HOST_GS_SELECTOR, read_gs());    /* 22.2.4 */
	vmcs_write16(HOST_SS_SELECTOR, __KERNEL_DS);  /* 22.2.4 */
#ifdef CONFIG_X86_64
	rdmsrl(MSR_FS_BASE, a);
	vmcs_writel(HOST_FS_BASE, a); /* 22.2.4 */
	rdmsrl(MSR_GS_BASE, a);
	vmcs_writel(HOST_GS_BASE, a); /* 22.2.4 */
#else
	vmcs_writel(HOST_FS_BASE, 0); /* 22.2.4 */
	vmcs_writel(HOST_GS_BASE, 0); /* 22.2.4 */
#endif

	vmcs_write16(HOST_TR_SELECTOR, GDT_ENTRY_TSS*8);  /* 22.2.4 */

	get_idt(&dt);
	vmcs_writel(HOST_IDTR_BASE, dt.base);   /* 22.2.4 */


	vmcs_writel(HOST_RIP, (unsigned long)kvm_vmx_return); /* 22.2.5 */

	rdmsr(MSR_IA32_SYSENTER_CS, host_sysenter_cs, junk);
	vmcs_write32(HOST_IA32_SYSENTER_CS, host_sysenter_cs);
	rdmsrl(MSR_IA32_SYSENTER_ESP, a);
	vmcs_writel(HOST_IA32_SYSENTER_ESP, a);   /* 22.2.3 */
	rdmsrl(MSR_IA32_SYSENTER_EIP, a);
	vmcs_writel(HOST_IA32_SYSENTER_EIP, a);   /* 22.2.3 */

	for (i = 0; i < NR_VMX_MSR; ++i) {
		u32 index = vmx_msr_index[i];
		u32 data_low, data_high;
		u64 data;
		int j = vcpu->nmsrs;

		if (rdmsr_safe(index, &data_low, &data_high) < 0)
			continue;
		if (wrmsr_safe(index, data_low, data_high) < 0)
			continue;
		data = data_low | ((u64)data_high << 32);
		vcpu->host_msrs[j].index = index;
		vcpu->host_msrs[j].reserved = 0;
		vcpu->host_msrs[j].data = data;
		vcpu->guest_msrs[j] = vcpu->host_msrs[j];
#ifdef CONFIG_X86_64
		if (index == MSR_KERNEL_GS_BASE)
			msr_offset_kernel_gs_base = j;
#endif
		++vcpu->nmsrs;
	}

	setup_msrs(vcpu);

	vmcs_write32_fixedbits(MSR_IA32_VMX_EXIT_CTLS, VM_EXIT_CONTROLS,
		     	       (HOST_IS_64 << 9));  /* 22.2,1, 20.7.1 */

	/* 22.2.1, 20.8.1 */
	vmcs_write32_fixedbits(MSR_IA32_VMX_ENTRY_CTLS,
                               VM_ENTRY_CONTROLS, 0);
	vmcs_write32(VM_ENTRY_INTR_INFO_FIELD, 0);  /* 22.2.1 */

#ifdef CONFIG_X86_64
	vmcs_writel(VIRTUAL_APIC_PAGE_ADDR, 0);
	vmcs_writel(TPR_THRESHOLD, 0);
#endif

	vmcs_writel(CR0_GUEST_HOST_MASK, ~0UL);
	vmcs_writel(CR4_GUEST_HOST_MASK, KVM_GUEST_CR4_MASK);

	vcpu->cr0 = 0x60000010;
	vmx_set_cr0(vcpu, vcpu->cr0); // enter rmode
	vmx_set_cr4(vcpu, 0);
#ifdef CONFIG_X86_64
	vmx_set_efer(vcpu, 0);
#endif

	return 0;

out:
	return ret;
}

static void inject_rmode_irq(struct kvm_vcpu *vcpu, int irq)
{
	u16 ent[2];
	u16 cs;
	u16 ip;
	unsigned long flags;
	unsigned long ss_base = vmcs_readl(GUEST_SS_BASE);
	u16 sp =  vmcs_readl(GUEST_RSP);
	u32 ss_limit = vmcs_read32(GUEST_SS_LIMIT);

	if (sp > ss_limit || sp < 6 ) {
		vcpu_printf(vcpu, "%s: #SS, rsp 0x%lx ss 0x%lx limit 0x%x\n",
			    __FUNCTION__,
			    vmcs_readl(GUEST_RSP),
			    vmcs_readl(GUEST_SS_BASE),
			    vmcs_read32(GUEST_SS_LIMIT));
		return;
	}

	if (kvm_read_guest(vcpu, irq * sizeof(ent), sizeof(ent), &ent) !=
								sizeof(ent)) {
		vcpu_printf(vcpu, "%s: read guest err\n", __FUNCTION__);
		return;
	}

	flags =  vmcs_readl(GUEST_RFLAGS);
	cs =  vmcs_readl(GUEST_CS_BASE) >> 4;
	ip =  vmcs_readl(GUEST_RIP);


	if (kvm_write_guest(vcpu, ss_base + sp - 2, 2, &flags) != 2 ||
	    kvm_write_guest(vcpu, ss_base + sp - 4, 2, &cs) != 2 ||
	    kvm_write_guest(vcpu, ss_base + sp - 6, 2, &ip) != 2) {
		vcpu_printf(vcpu, "%s: write guest err\n", __FUNCTION__);
		return;
	}

	vmcs_writel(GUEST_RFLAGS, flags &
		    ~( X86_EFLAGS_IF | X86_EFLAGS_AC | X86_EFLAGS_TF));
	vmcs_write16(GUEST_CS_SELECTOR, ent[1]) ;
	vmcs_writel(GUEST_CS_BASE, ent[1] << 4);
	vmcs_writel(GUEST_RIP, ent[0]);
	vmcs_writel(GUEST_RSP, (vmcs_readl(GUEST_RSP) & ~0xffff) | (sp - 6));
}

static void kvm_do_inject_irq(struct kvm_vcpu *vcpu)
{
	int word_index = __ffs(vcpu->irq_summary);
	int bit_index = __ffs(vcpu->irq_pending[word_index]);
	int irq = word_index * BITS_PER_LONG + bit_index;

	clear_bit(bit_index, &vcpu->irq_pending[word_index]);
	if (!vcpu->irq_pending[word_index])
		clear_bit(word_index, &vcpu->irq_summary);

	if (vcpu->rmode.active) {
		inject_rmode_irq(vcpu, irq);
		return;
	}
	vmcs_write32(VM_ENTRY_INTR_INFO_FIELD,
			irq | INTR_TYPE_EXT_INTR | INTR_INFO_VALID_MASK);
}


static void do_interrupt_requests(struct kvm_vcpu *vcpu,
				       struct kvm_run *kvm_run)
{
	u32 cpu_based_vm_exec_control;

	vcpu->interrupt_window_open =
		((vmcs_readl(GUEST_RFLAGS) & X86_EFLAGS_IF) &&
		 (vmcs_read32(GUEST_INTERRUPTIBILITY_INFO) & 3) == 0);

	if (vcpu->interrupt_window_open &&
	    vcpu->irq_summary &&
	    !(vmcs_read32(VM_ENTRY_INTR_INFO_FIELD) & INTR_INFO_VALID_MASK))
		/*
		 * If interrupts enabled, and not blocked by sti or mov ss. Good.
		 */
		kvm_do_inject_irq(vcpu);

	cpu_based_vm_exec_control = vmcs_read32(CPU_BASED_VM_EXEC_CONTROL);
	if (!vcpu->interrupt_window_open &&
	    (vcpu->irq_summary || kvm_run->request_interrupt_window))
		/*
		 * Interrupts blocked.  Wait for unblock.
		 */
		cpu_based_vm_exec_control |= CPU_BASED_VIRTUAL_INTR_PENDING;
	else
		cpu_based_vm_exec_control &= ~CPU_BASED_VIRTUAL_INTR_PENDING;
	vmcs_write32(CPU_BASED_VM_EXEC_CONTROL, cpu_based_vm_exec_control);
}

static void kvm_guest_debug_pre(struct kvm_vcpu *vcpu)
{
	struct kvm_guest_debug *dbg = &vcpu->guest_debug;

	set_debugreg(dbg->bp[0], 0);
	set_debugreg(dbg->bp[1], 1);
	set_debugreg(dbg->bp[2], 2);
	set_debugreg(dbg->bp[3], 3);

	if (dbg->singlestep) {
		unsigned long flags;

		flags = vmcs_readl(GUEST_RFLAGS);
		flags |= X86_EFLAGS_TF | X86_EFLAGS_RF;
		vmcs_writel(GUEST_RFLAGS, flags);
	}
}

static int handle_rmode_exception(struct kvm_vcpu *vcpu,
				  int vec, u32 err_code)
{
	if (!vcpu->rmode.active)
		return 0;

	if (vec == GP_VECTOR && err_code == 0)
		if (emulate_instruction(vcpu, NULL, 0, 0) == EMULATE_DONE)
			return 1;
	return 0;
}

static int handle_exception(struct kvm_vcpu *vcpu, struct kvm_run *kvm_run)
{
	u32 intr_info, error_code;
	unsigned long cr2, rip;
	u32 vect_info;
	enum emulation_result er;
	int r;

	vect_info = vmcs_read32(IDT_VECTORING_INFO_FIELD);
	intr_info = vmcs_read32(VM_EXIT_INTR_INFO);

	if ((vect_info & VECTORING_INFO_VALID_MASK) &&
						!is_page_fault(intr_info)) {
		printk(KERN_ERR "%s: unexpected, vectoring info 0x%x "
		       "intr info 0x%x\n", __FUNCTION__, vect_info, intr_info);
	}

	if (is_external_interrupt(vect_info)) {
		int irq = vect_info & VECTORING_INFO_VECTOR_MASK;
		set_bit(irq, vcpu->irq_pending);
		set_bit(irq / BITS_PER_LONG, &vcpu->irq_summary);
	}

	if ((intr_info & INTR_INFO_INTR_TYPE_MASK) == 0x200) { /* nmi */
		asm ("int $2");
		return 1;
	}

	if (is_no_device(intr_info)) {
		vcpu->fpu_active = 1;
		vmcs_clear_bits(EXCEPTION_BITMAP, 1 << NM_VECTOR);
		if (!(vcpu->cr0 & CR0_TS_MASK))
			vmcs_clear_bits(GUEST_CR0, CR0_TS_MASK);
		return 1;
	}

	error_code = 0;
	rip = vmcs_readl(GUEST_RIP);
	if (intr_info & INTR_INFO_DELIEVER_CODE_MASK)
		error_code = vmcs_read32(VM_EXIT_INTR_ERROR_CODE);
	if (is_page_fault(intr_info)) {
		cr2 = vmcs_readl(EXIT_QUALIFICATION);

		spin_lock(&vcpu->kvm->lock);
		r = kvm_mmu_page_fault(vcpu, cr2, error_code);
		if (r < 0) {
			spin_unlock(&vcpu->kvm->lock);
			return r;
		}
		if (!r) {
			spin_unlock(&vcpu->kvm->lock);
			return 1;
		}

		er = emulate_instruction(vcpu, kvm_run, cr2, error_code);
		spin_unlock(&vcpu->kvm->lock);

		switch (er) {
		case EMULATE_DONE:
			return 1;
		case EMULATE_DO_MMIO:
			++vcpu->stat.mmio_exits;
			kvm_run->exit_reason = KVM_EXIT_MMIO;
			return 0;
		 case EMULATE_FAIL:
			vcpu_printf(vcpu, "%s: emulate fail\n", __FUNCTION__);
			break;
		default:
			BUG();
		}
	}

	if (vcpu->rmode.active &&
	    handle_rmode_exception(vcpu, intr_info & INTR_INFO_VECTOR_MASK,
								error_code))
		return 1;

	if ((intr_info & (INTR_INFO_INTR_TYPE_MASK | INTR_INFO_VECTOR_MASK)) == (INTR_TYPE_EXCEPTION | 1)) {
		kvm_run->exit_reason = KVM_EXIT_DEBUG;
		return 0;
	}
	kvm_run->exit_reason = KVM_EXIT_EXCEPTION;
	kvm_run->ex.exception = intr_info & INTR_INFO_VECTOR_MASK;
	kvm_run->ex.error_code = error_code;
	return 0;
}

static int handle_external_interrupt(struct kvm_vcpu *vcpu,
				     struct kvm_run *kvm_run)
{
	++vcpu->stat.irq_exits;
	return 1;
}

static int handle_triple_fault(struct kvm_vcpu *vcpu, struct kvm_run *kvm_run)
{
	kvm_run->exit_reason = KVM_EXIT_SHUTDOWN;
	return 0;
}

static int get_io_count(struct kvm_vcpu *vcpu, unsigned long *count)
{
	u64 inst;
	gva_t rip;
	int countr_size;
	int i, n;

	if ((vmcs_readl(GUEST_RFLAGS) & X86_EFLAGS_VM)) {
		countr_size = 2;
	} else {
		u32 cs_ar = vmcs_read32(GUEST_CS_AR_BYTES);

		countr_size = (cs_ar & AR_L_MASK) ? 8:
			      (cs_ar & AR_DB_MASK) ? 4: 2;
	}

	rip =  vmcs_readl(GUEST_RIP);
	if (countr_size != 8)
		rip += vmcs_readl(GUEST_CS_BASE);

	n = kvm_read_guest(vcpu, rip, sizeof(inst), &inst);

	for (i = 0; i < n; i++) {
		switch (((u8*)&inst)[i]) {
		case 0xf0:
		case 0xf2:
		case 0xf3:
		case 0x2e:
		case 0x36:
		case 0x3e:
		case 0x26:
		case 0x64:
		case 0x65:
		case 0x66:
			break;
		case 0x67:
			countr_size = (countr_size == 2) ? 4: (countr_size >> 1);
		default:
			goto done;
		}
	}
	return 0;
done:
	countr_size *= 8;
	*count = vcpu->regs[VCPU_REGS_RCX] & (~0ULL >> (64 - countr_size));
	//printk("cx: %lx\n", vcpu->regs[VCPU_REGS_RCX]);
	return 1;
}

static int handle_io(struct kvm_vcpu *vcpu, struct kvm_run *kvm_run)
{
	u64 exit_qualification;
	int size, down, in, string, rep;
	unsigned port;
	unsigned long count;
	gva_t address;

	++vcpu->stat.io_exits;
	exit_qualification = vmcs_read64(EXIT_QUALIFICATION);
	in = (exit_qualification & 8) != 0;
	size = (exit_qualification & 7) + 1;
	string = (exit_qualification & 16) != 0;
	down = (vmcs_readl(GUEST_RFLAGS) & X86_EFLAGS_DF) != 0;
	count = 1;
	rep = (exit_qualification & 32) != 0;
	port = exit_qualification >> 16;
	address = 0;
	if (string) {
		if (rep && !get_io_count(vcpu, &count))
			return 1;
		address = vmcs_readl(GUEST_LINEAR_ADDRESS);
	}
	return kvm_setup_pio(vcpu, kvm_run, in, size, count, string, down,
			     address, rep, port);
}

static void
vmx_patch_hypercall(struct kvm_vcpu *vcpu, unsigned char *hypercall)
{
	/*
	 * Patch in the VMCALL instruction:
	 */
	hypercall[0] = 0x0f;
	hypercall[1] = 0x01;
	hypercall[2] = 0xc1;
	hypercall[3] = 0xc3;
}

static int handle_cr(struct kvm_vcpu *vcpu, struct kvm_run *kvm_run)
{
	u64 exit_qualification;
	int cr;
	int reg;

	exit_qualification = vmcs_read64(EXIT_QUALIFICATION);
	cr = exit_qualification & 15;
	reg = (exit_qualification >> 8) & 15;
	switch ((exit_qualification >> 4) & 3) {
	case 0: /* mov to cr */
		switch (cr) {
		case 0:
			vcpu_load_rsp_rip(vcpu);
			set_cr0(vcpu, vcpu->regs[reg]);
			skip_emulated_instruction(vcpu);
			return 1;
		case 3:
			vcpu_load_rsp_rip(vcpu);
			set_cr3(vcpu, vcpu->regs[reg]);
			skip_emulated_instruction(vcpu);
			return 1;
		case 4:
			vcpu_load_rsp_rip(vcpu);
			set_cr4(vcpu, vcpu->regs[reg]);
			skip_emulated_instruction(vcpu);
			return 1;
		case 8:
			vcpu_load_rsp_rip(vcpu);
			set_cr8(vcpu, vcpu->regs[reg]);
			skip_emulated_instruction(vcpu);
			return 1;
		};
		break;
	case 2: /* clts */
		vcpu_load_rsp_rip(vcpu);
		vcpu->fpu_active = 1;
		vmcs_clear_bits(EXCEPTION_BITMAP, 1 << NM_VECTOR);
		vmcs_clear_bits(GUEST_CR0, CR0_TS_MASK);
		vcpu->cr0 &= ~CR0_TS_MASK;
		vmcs_writel(CR0_READ_SHADOW, vcpu->cr0);
		skip_emulated_instruction(vcpu);
		return 1;
	case 1: /*mov from cr*/
		switch (cr) {
		case 3:
			vcpu_load_rsp_rip(vcpu);
			vcpu->regs[reg] = vcpu->cr3;
			vcpu_put_rsp_rip(vcpu);
			skip_emulated_instruction(vcpu);
			return 1;
		case 8:
			vcpu_load_rsp_rip(vcpu);
			vcpu->regs[reg] = vcpu->cr8;
			vcpu_put_rsp_rip(vcpu);
			skip_emulated_instruction(vcpu);
			return 1;
		}
		break;
	case 3: /* lmsw */
		lmsw(vcpu, (exit_qualification >> LMSW_SOURCE_DATA_SHIFT) & 0x0f);

		skip_emulated_instruction(vcpu);
		return 1;
	default:
		break;
	}
	kvm_run->exit_reason = 0;
	printk(KERN_ERR "kvm: unhandled control register: op %d cr %d\n",
	       (int)(exit_qualification >> 4) & 3, cr);
	return 0;
}

static int handle_dr(struct kvm_vcpu *vcpu, struct kvm_run *kvm_run)
{
	u64 exit_qualification;
	unsigned long val;
	int dr, reg;

	/*
	 * FIXME: this code assumes the host is debugging the guest.
	 *        need to deal with guest debugging itself too.
	 */
	exit_qualification = vmcs_read64(EXIT_QUALIFICATION);
	dr = exit_qualification & 7;
	reg = (exit_qualification >> 8) & 15;
	vcpu_load_rsp_rip(vcpu);
	if (exit_qualification & 16) {
		/* mov from dr */
		switch (dr) {
		case 6:
			val = 0xffff0ff0;
			break;
		case 7:
			val = 0x400;
			break;
		default:
			val = 0;
		}
		vcpu->regs[reg] = val;
	} else {
		/* mov to dr */
	}
	vcpu_put_rsp_rip(vcpu);
	skip_emulated_instruction(vcpu);
	return 1;
}

static int handle_cpuid(struct kvm_vcpu *vcpu, struct kvm_run *kvm_run)
{
	kvm_emulate_cpuid(vcpu);
	return 1;
}

static int handle_rdmsr(struct kvm_vcpu *vcpu, struct kvm_run *kvm_run)
{
	u32 ecx = vcpu->regs[VCPU_REGS_RCX];
	u64 data;

	if (vmx_get_msr(vcpu, ecx, &data)) {
		vmx_inject_gp(vcpu, 0);
		return 1;
	}

	/* FIXME: handling of bits 32:63 of rax, rdx */
	vcpu->regs[VCPU_REGS_RAX] = data & -1u;
	vcpu->regs[VCPU_REGS_RDX] = (data >> 32) & -1u;
	skip_emulated_instruction(vcpu);
	return 1;
}

static int handle_wrmsr(struct kvm_vcpu *vcpu, struct kvm_run *kvm_run)
{
	u32 ecx = vcpu->regs[VCPU_REGS_RCX];
	u64 data = (vcpu->regs[VCPU_REGS_RAX] & -1u)
		| ((u64)(vcpu->regs[VCPU_REGS_RDX] & -1u) << 32);

	if (vmx_set_msr(vcpu, ecx, data) != 0) {
		vmx_inject_gp(vcpu, 0);
		return 1;
	}

	skip_emulated_instruction(vcpu);
	return 1;
}

static void post_kvm_run_save(struct kvm_vcpu *vcpu,
			      struct kvm_run *kvm_run)
{
	kvm_run->if_flag = (vmcs_readl(GUEST_RFLAGS) & X86_EFLAGS_IF) != 0;
	kvm_run->cr8 = vcpu->cr8;
	kvm_run->apic_base = vcpu->apic_base;
	kvm_run->ready_for_interrupt_injection = (vcpu->interrupt_window_open &&
						  vcpu->irq_summary == 0);
}

static int handle_interrupt_window(struct kvm_vcpu *vcpu,
				   struct kvm_run *kvm_run)
{
	/*
	 * If the user space waits to inject interrupts, exit as soon as
	 * possible
	 */
	if (kvm_run->request_interrupt_window &&
	    !vcpu->irq_summary) {
		kvm_run->exit_reason = KVM_EXIT_IRQ_WINDOW_OPEN;
		++vcpu->stat.irq_window_exits;
		return 0;
	}
	return 1;
}

static int handle_halt(struct kvm_vcpu *vcpu, struct kvm_run *kvm_run)
{
	skip_emulated_instruction(vcpu);
	if (vcpu->irq_summary)
		return 1;

	kvm_run->exit_reason = KVM_EXIT_HLT;
	++vcpu->stat.halt_exits;
	return 0;
}

static int handle_vmcall(struct kvm_vcpu *vcpu, struct kvm_run *kvm_run)
{
	skip_emulated_instruction(vcpu);
	return kvm_hypercall(vcpu, kvm_run);
}

/*
 * The exit handlers return 1 if the exit was handled fully and guest execution
 * may resume.  Otherwise they set the kvm_run parameter to indicate what needs
 * to be done to userspace and return 0.
 */
static int (*kvm_vmx_exit_handlers[])(struct kvm_vcpu *vcpu,
				      struct kvm_run *kvm_run) = {
	[EXIT_REASON_EXCEPTION_NMI]           = handle_exception,
	[EXIT_REASON_EXTERNAL_INTERRUPT]      = handle_external_interrupt,
	[EXIT_REASON_TRIPLE_FAULT]            = handle_triple_fault,
	[EXIT_REASON_IO_INSTRUCTION]          = handle_io,
	[EXIT_REASON_CR_ACCESS]               = handle_cr,
	[EXIT_REASON_DR_ACCESS]               = handle_dr,
	[EXIT_REASON_CPUID]                   = handle_cpuid,
	[EXIT_REASON_MSR_READ]                = handle_rdmsr,
	[EXIT_REASON_MSR_WRITE]               = handle_wrmsr,
	[EXIT_REASON_PENDING_INTERRUPT]       = handle_interrupt_window,
	[EXIT_REASON_HLT]                     = handle_halt,
	[EXIT_REASON_VMCALL]                  = handle_vmcall,
};

static const int kvm_vmx_max_exit_handlers =
	sizeof(kvm_vmx_exit_handlers) / sizeof(*kvm_vmx_exit_handlers);

/*
 * The guest has exited.  See if we can fix it or if we need userspace
 * assistance.
 */
static int kvm_handle_exit(struct kvm_run *kvm_run, struct kvm_vcpu *vcpu)
{
	u32 vectoring_info = vmcs_read32(IDT_VECTORING_INFO_FIELD);
	u32 exit_reason = vmcs_read32(VM_EXIT_REASON);

	if ( (vectoring_info & VECTORING_INFO_VALID_MASK) &&
				exit_reason != EXIT_REASON_EXCEPTION_NMI )
		printk(KERN_WARNING "%s: unexpected, valid vectoring info and "
		       "exit reason is 0x%x\n", __FUNCTION__, exit_reason);
	if (exit_reason < kvm_vmx_max_exit_handlers
	    && kvm_vmx_exit_handlers[exit_reason])
		return kvm_vmx_exit_handlers[exit_reason](vcpu, kvm_run);
	else {
		kvm_run->exit_reason = KVM_EXIT_UNKNOWN;
		kvm_run->hw.hardware_exit_reason = exit_reason;
	}
	return 0;
}

/*
 * Check if userspace requested an interrupt window, and that the
 * interrupt window is open.
 *
 * No need to exit to userspace if we already have an interrupt queued.
 */
static int dm_request_for_irq_injection(struct kvm_vcpu *vcpu,
					  struct kvm_run *kvm_run)
{
	return (!vcpu->irq_summary &&
		kvm_run->request_interrupt_window &&
		vcpu->interrupt_window_open &&
		(vmcs_readl(GUEST_RFLAGS) & X86_EFLAGS_IF));
}

static int vmx_vcpu_run(struct kvm_vcpu *vcpu, struct kvm_run *kvm_run)
{
	u8 fail;
	u16 fs_sel, gs_sel, ldt_sel;
	int fs_gs_ldt_reload_needed;
	int r;

again:
	/*
	 * Set host fs and gs selectors.  Unfortunately, 22.2.3 does not
	 * allow segment selectors with cpl > 0 or ti == 1.
	 */
	fs_sel = read_fs();
	gs_sel = read_gs();
	ldt_sel = read_ldt();
	fs_gs_ldt_reload_needed = (fs_sel & 7) | (gs_sel & 7) | ldt_sel;
	if (!fs_gs_ldt_reload_needed) {
		vmcs_write16(HOST_FS_SELECTOR, fs_sel);
		vmcs_write16(HOST_GS_SELECTOR, gs_sel);
	} else {
		vmcs_write16(HOST_FS_SELECTOR, 0);
		vmcs_write16(HOST_GS_SELECTOR, 0);
	}

#ifdef CONFIG_X86_64
	vmcs_writel(HOST_FS_BASE, read_msr(MSR_FS_BASE));
	vmcs_writel(HOST_GS_BASE, read_msr(MSR_GS_BASE));
#else
	vmcs_writel(HOST_FS_BASE, segment_base(fs_sel));
	vmcs_writel(HOST_GS_BASE, segment_base(gs_sel));
#endif

	if (!vcpu->mmio_read_completed)
		do_interrupt_requests(vcpu, kvm_run);

	if (vcpu->guest_debug.enabled)
		kvm_guest_debug_pre(vcpu);

	if (vcpu->fpu_active) {
		fx_save(vcpu->host_fx_image);
		fx_restore(vcpu->guest_fx_image);
	}
	/*
	 * Loading guest fpu may have cleared host cr0.ts
	 */
	vmcs_writel(HOST_CR0, read_cr0());

#ifdef CONFIG_X86_64
	if (is_long_mode(vcpu)) {
		save_msrs(vcpu->host_msrs + msr_offset_kernel_gs_base, 1);
		load_msrs(vcpu->guest_msrs, NR_BAD_MSRS);
	}
#endif

	asm (
		/* Store host registers */
		"pushf \n\t"
#ifdef CONFIG_X86_64
		"push %%rax; push %%rbx; push %%rdx;"
		"push %%rsi; push %%rdi; push %%rbp;"
		"push %%r8;  push %%r9;  push %%r10; push %%r11;"
		"push %%r12; push %%r13; push %%r14; push %%r15;"
		"push %%rcx \n\t"
		ASM_VMX_VMWRITE_RSP_RDX "\n\t"
#else
		"pusha; push %%ecx \n\t"
		ASM_VMX_VMWRITE_RSP_RDX "\n\t"
#endif
		/* Check if vmlaunch of vmresume is needed */
		"cmp $0, %1 \n\t"
		/* Load guest registers.  Don't clobber flags. */
#ifdef CONFIG_X86_64
		"mov %c[cr2](%3), %%rax \n\t"
		"mov %%rax, %%cr2 \n\t"
		"mov %c[rax](%3), %%rax \n\t"
		"mov %c[rbx](%3), %%rbx \n\t"
		"mov %c[rdx](%3), %%rdx \n\t"
		"mov %c[rsi](%3), %%rsi \n\t"
		"mov %c[rdi](%3), %%rdi \n\t"
		"mov %c[rbp](%3), %%rbp \n\t"
		"mov %c[r8](%3),  %%r8  \n\t"
		"mov %c[r9](%3),  %%r9  \n\t"
		"mov %c[r10](%3), %%r10 \n\t"
		"mov %c[r11](%3), %%r11 \n\t"
		"mov %c[r12](%3), %%r12 \n\t"
		"mov %c[r13](%3), %%r13 \n\t"
		"mov %c[r14](%3), %%r14 \n\t"
		"mov %c[r15](%3), %%r15 \n\t"
		"mov %c[rcx](%3), %%rcx \n\t" /* kills %3 (rcx) */
#else
		"mov %c[cr2](%3), %%eax \n\t"
		"mov %%eax,   %%cr2 \n\t"
		"mov %c[rax](%3), %%eax \n\t"
		"mov %c[rbx](%3), %%ebx \n\t"
		"mov %c[rdx](%3), %%edx \n\t"
		"mov %c[rsi](%3), %%esi \n\t"
		"mov %c[rdi](%3), %%edi \n\t"
		"mov %c[rbp](%3), %%ebp \n\t"
		"mov %c[rcx](%3), %%ecx \n\t" /* kills %3 (ecx) */
#endif
		/* Enter guest mode */
		"jne launched \n\t"
		ASM_VMX_VMLAUNCH "\n\t"
		"jmp kvm_vmx_return \n\t"
		"launched: " ASM_VMX_VMRESUME "\n\t"
		".globl kvm_vmx_return \n\t"
		"kvm_vmx_return: "
		/* Save guest registers, load host registers, keep flags */
#ifdef CONFIG_X86_64
		"xchg %3,     (%%rsp) \n\t"
		"mov %%rax, %c[rax](%3) \n\t"
		"mov %%rbx, %c[rbx](%3) \n\t"
		"pushq (%%rsp); popq %c[rcx](%3) \n\t"
		"mov %%rdx, %c[rdx](%3) \n\t"
		"mov %%rsi, %c[rsi](%3) \n\t"
		"mov %%rdi, %c[rdi](%3) \n\t"
		"mov %%rbp, %c[rbp](%3) \n\t"
		"mov %%r8,  %c[r8](%3) \n\t"
		"mov %%r9,  %c[r9](%3) \n\t"
		"mov %%r10, %c[r10](%3) \n\t"
		"mov %%r11, %c[r11](%3) \n\t"
		"mov %%r12, %c[r12](%3) \n\t"
		"mov %%r13, %c[r13](%3) \n\t"
		"mov %%r14, %c[r14](%3) \n\t"
		"mov %%r15, %c[r15](%3) \n\t"
		"mov %%cr2, %%rax   \n\t"
		"mov %%rax, %c[cr2](%3) \n\t"
		"mov (%%rsp), %3 \n\t"

		"pop  %%rcx; pop  %%r15; pop  %%r14; pop  %%r13; pop  %%r12;"
		"pop  %%r11; pop  %%r10; pop  %%r9;  pop  %%r8;"
		"pop  %%rbp; pop  %%rdi; pop  %%rsi;"
		"pop  %%rdx; pop  %%rbx; pop  %%rax \n\t"
#else
		"xchg %3, (%%esp) \n\t"
		"mov %%eax, %c[rax](%3) \n\t"
		"mov %%ebx, %c[rbx](%3) \n\t"
		"pushl (%%esp); popl %c[rcx](%3) \n\t"
		"mov %%edx, %c[rdx](%3) \n\t"
		"mov %%esi, %c[rsi](%3) \n\t"
		"mov %%edi, %c[rdi](%3) \n\t"
		"mov %%ebp, %c[rbp](%3) \n\t"
		"mov %%cr2, %%eax  \n\t"
		"mov %%eax, %c[cr2](%3) \n\t"
		"mov (%%esp), %3 \n\t"

		"pop %%ecx; popa \n\t"
#endif
		"setbe %0 \n\t"
		"popf \n\t"
	      : "=q" (fail)
	      : "r"(vcpu->launched), "d"((unsigned long)HOST_RSP),
		"c"(vcpu),
		[rax]"i"(offsetof(struct kvm_vcpu, regs[VCPU_REGS_RAX])),
		[rbx]"i"(offsetof(struct kvm_vcpu, regs[VCPU_REGS_RBX])),
		[rcx]"i"(offsetof(struct kvm_vcpu, regs[VCPU_REGS_RCX])),
		[rdx]"i"(offsetof(struct kvm_vcpu, regs[VCPU_REGS_RDX])),
		[rsi]"i"(offsetof(struct kvm_vcpu, regs[VCPU_REGS_RSI])),
		[rdi]"i"(offsetof(struct kvm_vcpu, regs[VCPU_REGS_RDI])),
		[rbp]"i"(offsetof(struct kvm_vcpu, regs[VCPU_REGS_RBP])),
#ifdef CONFIG_X86_64
		[r8 ]"i"(offsetof(struct kvm_vcpu, regs[VCPU_REGS_R8 ])),
		[r9 ]"i"(offsetof(struct kvm_vcpu, regs[VCPU_REGS_R9 ])),
		[r10]"i"(offsetof(struct kvm_vcpu, regs[VCPU_REGS_R10])),
		[r11]"i"(offsetof(struct kvm_vcpu, regs[VCPU_REGS_R11])),
		[r12]"i"(offsetof(struct kvm_vcpu, regs[VCPU_REGS_R12])),
		[r13]"i"(offsetof(struct kvm_vcpu, regs[VCPU_REGS_R13])),
		[r14]"i"(offsetof(struct kvm_vcpu, regs[VCPU_REGS_R14])),
		[r15]"i"(offsetof(struct kvm_vcpu, regs[VCPU_REGS_R15])),
#endif
		[cr2]"i"(offsetof(struct kvm_vcpu, cr2))
	      : "cc", "memory" );

	/*
	 * Reload segment selectors ASAP. (it's needed for a functional
	 * kernel: x86 relies on having __KERNEL_PDA in %fs and x86_64
	 * relies on having 0 in %gs for the CPU PDA to work.)
	 */
	if (fs_gs_ldt_reload_needed) {
		load_ldt(ldt_sel);
		load_fs(fs_sel);
		/*
		 * If we have to reload gs, we must take care to
		 * preserve our gs base.
		 */
		local_irq_disable();
		load_gs(gs_sel);
#ifdef CONFIG_X86_64
		wrmsrl(MSR_GS_BASE, vmcs_readl(HOST_GS_BASE));
#endif
		local_irq_enable();

		reload_tss();
	}
	++vcpu->stat.exits;

#ifdef CONFIG_X86_64
	if (is_long_mode(vcpu)) {
		save_msrs(vcpu->guest_msrs, NR_BAD_MSRS);
		load_msrs(vcpu->host_msrs, NR_BAD_MSRS);
	}
#endif

	if (vcpu->fpu_active) {
		fx_save(vcpu->guest_fx_image);
		fx_restore(vcpu->host_fx_image);
	}

	vcpu->interrupt_window_open = (vmcs_read32(GUEST_INTERRUPTIBILITY_INFO) & 3) == 0;

	asm ("mov %0, %%ds; mov %0, %%es" : : "r"(__USER_DS));

	if (fail) {
		kvm_run->exit_reason = KVM_EXIT_FAIL_ENTRY;
		kvm_run->fail_entry.hardware_entry_failure_reason
			= vmcs_read32(VM_INSTRUCTION_ERROR);
		r = 0;
	} else {
		/*
		 * Profile KVM exit RIPs:
		 */
		if (unlikely(prof_on == KVM_PROFILING))
			profile_hit(KVM_PROFILING, (void *)vmcs_readl(GUEST_RIP));

		vcpu->launched = 1;
		r = kvm_handle_exit(kvm_run, vcpu);
		if (r > 0) {
			/* Give scheduler a change to reschedule. */
			if (signal_pending(current)) {
				++vcpu->stat.signal_exits;
				post_kvm_run_save(vcpu, kvm_run);
				kvm_run->exit_reason = KVM_EXIT_INTR;
				return -EINTR;
			}

			if (dm_request_for_irq_injection(vcpu, kvm_run)) {
				++vcpu->stat.request_irq_exits;
				post_kvm_run_save(vcpu, kvm_run);
				kvm_run->exit_reason = KVM_EXIT_INTR;
				return -EINTR;
			}

			kvm_resched(vcpu);
			goto again;
		}
	}

	post_kvm_run_save(vcpu, kvm_run);
	return r;
}

static void vmx_flush_tlb(struct kvm_vcpu *vcpu)
{
	vmcs_writel(GUEST_CR3, vmcs_readl(GUEST_CR3));
}

static void vmx_inject_page_fault(struct kvm_vcpu *vcpu,
				  unsigned long addr,
				  u32 err_code)
{
	u32 vect_info = vmcs_read32(IDT_VECTORING_INFO_FIELD);

	++vcpu->stat.pf_guest;

	if (is_page_fault(vect_info)) {
		printk(KERN_DEBUG "inject_page_fault: "
		       "double fault 0x%lx @ 0x%lx\n",
		       addr, vmcs_readl(GUEST_RIP));
		vmcs_write32(VM_ENTRY_EXCEPTION_ERROR_CODE, 0);
		vmcs_write32(VM_ENTRY_INTR_INFO_FIELD,
			     DF_VECTOR |
			     INTR_TYPE_EXCEPTION |
			     INTR_INFO_DELIEVER_CODE_MASK |
			     INTR_INFO_VALID_MASK);
		return;
	}
	vcpu->cr2 = addr;
	vmcs_write32(VM_ENTRY_EXCEPTION_ERROR_CODE, err_code);
	vmcs_write32(VM_ENTRY_INTR_INFO_FIELD,
		     PF_VECTOR |
		     INTR_TYPE_EXCEPTION |
		     INTR_INFO_DELIEVER_CODE_MASK |
		     INTR_INFO_VALID_MASK);

}

static void vmx_free_vmcs(struct kvm_vcpu *vcpu)
{
	if (vcpu->vmcs) {
		on_each_cpu(__vcpu_clear, vcpu, 0, 1);
		free_vmcs(vcpu->vmcs);
		vcpu->vmcs = NULL;
	}
}

static void vmx_free_vcpu(struct kvm_vcpu *vcpu)
{
	vmx_free_vmcs(vcpu);
}

static int vmx_create_vcpu(struct kvm_vcpu *vcpu)
{
	struct vmcs *vmcs;

	vcpu->guest_msrs = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (!vcpu->guest_msrs)
		return -ENOMEM;

	vcpu->host_msrs = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (!vcpu->host_msrs)
		goto out_free_guest_msrs;

	vmcs = alloc_vmcs();
	if (!vmcs)
		goto out_free_msrs;

	vmcs_clear(vmcs);
	vcpu->vmcs = vmcs;
	vcpu->launched = 0;
	vcpu->fpu_active = 1;

	return 0;

out_free_msrs:
	kfree(vcpu->host_msrs);
	vcpu->host_msrs = NULL;

out_free_guest_msrs:
	kfree(vcpu->guest_msrs);
	vcpu->guest_msrs = NULL;

	return -ENOMEM;
}

static struct kvm_arch_ops vmx_arch_ops = {
	.cpu_has_kvm_support = cpu_has_kvm_support,
	.disabled_by_bios = vmx_disabled_by_bios,
	.hardware_setup = hardware_setup,
	.hardware_unsetup = hardware_unsetup,
	.hardware_enable = hardware_enable,
	.hardware_disable = hardware_disable,

	.vcpu_create = vmx_create_vcpu,
	.vcpu_free = vmx_free_vcpu,

	.vcpu_load = vmx_vcpu_load,
	.vcpu_put = vmx_vcpu_put,
	.vcpu_decache = vmx_vcpu_decache,

	.set_guest_debug = set_guest_debug,
	.get_msr = vmx_get_msr,
	.set_msr = vmx_set_msr,
	.get_segment_base = vmx_get_segment_base,
	.get_segment = vmx_get_segment,
	.set_segment = vmx_set_segment,
	.get_cs_db_l_bits = vmx_get_cs_db_l_bits,
	.decache_cr4_guest_bits = vmx_decache_cr4_guest_bits,
	.set_cr0 = vmx_set_cr0,
	.set_cr3 = vmx_set_cr3,
	.set_cr4 = vmx_set_cr4,
#ifdef CONFIG_X86_64
	.set_efer = vmx_set_efer,
#endif
	.get_idt = vmx_get_idt,
	.set_idt = vmx_set_idt,
	.get_gdt = vmx_get_gdt,
	.set_gdt = vmx_set_gdt,
	.cache_regs = vcpu_load_rsp_rip,
	.decache_regs = vcpu_put_rsp_rip,
	.get_rflags = vmx_get_rflags,
	.set_rflags = vmx_set_rflags,

	.tlb_flush = vmx_flush_tlb,
	.inject_page_fault = vmx_inject_page_fault,

	.inject_gp = vmx_inject_gp,

	.run = vmx_vcpu_run,
	.skip_emulated_instruction = skip_emulated_instruction,
	.vcpu_setup = vmx_vcpu_setup,
	.patch_hypercall = vmx_patch_hypercall,
};

static int __init vmx_init(void)
{
	return kvm_init_arch(&vmx_arch_ops, THIS_MODULE);
}

static void __exit vmx_exit(void)
{
	kvm_exit_arch();
}

module_init(vmx_init)
module_exit(vmx_exit)
