/*
 * Kernel-based Virtual Machine driver for Linux
 *
 * AMD SVM support
 *
 * Copyright (C) 2006 Qumranet, Inc.
 *
 * Authors:
 *   Yaniv Kamay  <yaniv@qumranet.com>
 *   Avi Kivity   <avi@qumranet.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */
#include <linux/kvm_host.h>

#include "kvm_svm.h"
#include "irq.h"
#include "mmu.h"
#include "kvm_cache_regs.h"

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/vmalloc.h>
#include <linux/highmem.h>
#include <linux/sched.h>

#include <asm/desc.h>

#include <asm/virtext.h>

#define __ex(x) __kvm_handle_fault_on_reboot(x)

MODULE_AUTHOR("Qumranet");
MODULE_LICENSE("GPL");

#define IOPM_ALLOC_ORDER 2
#define MSRPM_ALLOC_ORDER 1

#define SEG_TYPE_LDT 2
#define SEG_TYPE_BUSY_TSS16 3

#define SVM_FEATURE_NPT  (1 << 0)
#define SVM_FEATURE_LBRV (1 << 1)
#define SVM_FEATURE_SVML (1 << 2)

#define DEBUGCTL_RESERVED_BITS (~(0x3fULL))

/* Turn on to get debugging output*/
/* #define NESTED_DEBUG */

#ifdef NESTED_DEBUG
#define nsvm_printk(fmt, args...) printk(KERN_INFO fmt, ## args)
#else
#define nsvm_printk(fmt, args...) do {} while(0)
#endif

/* enable NPT for AMD64 and X86 with PAE */
#if defined(CONFIG_X86_64) || defined(CONFIG_X86_PAE)
static bool npt_enabled = true;
#else
static bool npt_enabled = false;
#endif
static int npt = 1;

module_param(npt, int, S_IRUGO);

static int nested = 0;
module_param(nested, int, S_IRUGO);

static void kvm_reput_irq(struct vcpu_svm *svm);
static void svm_flush_tlb(struct kvm_vcpu *vcpu);

static int nested_svm_exit_handled(struct vcpu_svm *svm, bool kvm_override);
static int nested_svm_vmexit(struct vcpu_svm *svm);
static int nested_svm_vmsave(struct vcpu_svm *svm, void *nested_vmcb,
			     void *arg2, void *opaque);
static int nested_svm_check_exception(struct vcpu_svm *svm, unsigned nr,
				      bool has_error_code, u32 error_code);

static inline struct vcpu_svm *to_svm(struct kvm_vcpu *vcpu)
{
	return container_of(vcpu, struct vcpu_svm, vcpu);
}

static inline bool is_nested(struct vcpu_svm *svm)
{
	return svm->nested_vmcb;
}

static unsigned long iopm_base;

struct kvm_ldttss_desc {
	u16 limit0;
	u16 base0;
	unsigned base1 : 8, type : 5, dpl : 2, p : 1;
	unsigned limit1 : 4, zero0 : 3, g : 1, base2 : 8;
	u32 base3;
	u32 zero1;
} __attribute__((packed));

struct svm_cpu_data {
	int cpu;

	u64 asid_generation;
	u32 max_asid;
	u32 next_asid;
	struct kvm_ldttss_desc *tss_desc;

	struct page *save_area;
};

static DEFINE_PER_CPU(struct svm_cpu_data *, svm_data);
static uint32_t svm_features;

struct svm_init_data {
	int cpu;
	int r;
};

static u32 msrpm_ranges[] = {0, 0xc0000000, 0xc0010000};

#define NUM_MSR_MAPS ARRAY_SIZE(msrpm_ranges)
#define MSRS_RANGE_SIZE 2048
#define MSRS_IN_RANGE (MSRS_RANGE_SIZE * 8 / 2)

#define MAX_INST_SIZE 15

static inline u32 svm_has(u32 feat)
{
	return svm_features & feat;
}

static inline u8 pop_irq(struct kvm_vcpu *vcpu)
{
	int word_index = __ffs(vcpu->arch.irq_summary);
	int bit_index = __ffs(vcpu->arch.irq_pending[word_index]);
	int irq = word_index * BITS_PER_LONG + bit_index;

	clear_bit(bit_index, &vcpu->arch.irq_pending[word_index]);
	if (!vcpu->arch.irq_pending[word_index])
		clear_bit(word_index, &vcpu->arch.irq_summary);
	return irq;
}

static inline void push_irq(struct kvm_vcpu *vcpu, u8 irq)
{
	set_bit(irq, vcpu->arch.irq_pending);
	set_bit(irq / BITS_PER_LONG, &vcpu->arch.irq_summary);
}

static inline void clgi(void)
{
	asm volatile (__ex(SVM_CLGI));
}

static inline void stgi(void)
{
	asm volatile (__ex(SVM_STGI));
}

static inline void invlpga(unsigned long addr, u32 asid)
{
	asm volatile (__ex(SVM_INVLPGA) :: "a"(addr), "c"(asid));
}

static inline unsigned long kvm_read_cr2(void)
{
	unsigned long cr2;

	asm volatile ("mov %%cr2, %0" : "=r" (cr2));
	return cr2;
}

static inline void kvm_write_cr2(unsigned long val)
{
	asm volatile ("mov %0, %%cr2" :: "r" (val));
}

static inline void force_new_asid(struct kvm_vcpu *vcpu)
{
	to_svm(vcpu)->asid_generation--;
}

static inline void flush_guest_tlb(struct kvm_vcpu *vcpu)
{
	force_new_asid(vcpu);
}

static void svm_set_efer(struct kvm_vcpu *vcpu, u64 efer)
{
	if (!npt_enabled && !(efer & EFER_LMA))
		efer &= ~EFER_LME;

	to_svm(vcpu)->vmcb->save.efer = efer | EFER_SVME;
	vcpu->arch.shadow_efer = efer;
}

static void svm_queue_exception(struct kvm_vcpu *vcpu, unsigned nr,
				bool has_error_code, u32 error_code)
{
	struct vcpu_svm *svm = to_svm(vcpu);

	/* If we are within a nested VM we'd better #VMEXIT and let the
	   guest handle the exception */
	if (nested_svm_check_exception(svm, nr, has_error_code, error_code))
		return;

	svm->vmcb->control.event_inj = nr
		| SVM_EVTINJ_VALID
		| (has_error_code ? SVM_EVTINJ_VALID_ERR : 0)
		| SVM_EVTINJ_TYPE_EXEPT;
	svm->vmcb->control.event_inj_err = error_code;
}

static bool svm_exception_injected(struct kvm_vcpu *vcpu)
{
	struct vcpu_svm *svm = to_svm(vcpu);

	return !(svm->vmcb->control.exit_int_info & SVM_EXITINTINFO_VALID);
}

static int is_external_interrupt(u32 info)
{
	info &= SVM_EVTINJ_TYPE_MASK | SVM_EVTINJ_VALID;
	return info == (SVM_EVTINJ_VALID | SVM_EVTINJ_TYPE_INTR);
}

static void skip_emulated_instruction(struct kvm_vcpu *vcpu)
{
	struct vcpu_svm *svm = to_svm(vcpu);

	if (!svm->next_rip) {
		printk(KERN_DEBUG "%s: NOP\n", __func__);
		return;
	}
	if (svm->next_rip - kvm_rip_read(vcpu) > MAX_INST_SIZE)
		printk(KERN_ERR "%s: ip 0x%lx next 0x%llx\n",
		       __func__, kvm_rip_read(vcpu), svm->next_rip);

	kvm_rip_write(vcpu, svm->next_rip);
	svm->vmcb->control.int_state &= ~SVM_INTERRUPT_SHADOW_MASK;

	vcpu->arch.interrupt_window_open = (svm->vcpu.arch.hflags & HF_GIF_MASK);
}

static int has_svm(void)
{
	const char *msg;

	if (!cpu_has_svm(&msg)) {
		printk(KERN_INFO "has_svm: %s\n", msg);
		return 0;
	}

	return 1;
}

static void svm_hardware_disable(void *garbage)
{
	cpu_svm_disable();
}

static void svm_hardware_enable(void *garbage)
{

	struct svm_cpu_data *svm_data;
	uint64_t efer;
	struct desc_ptr gdt_descr;
	struct desc_struct *gdt;
	int me = raw_smp_processor_id();

	if (!has_svm()) {
		printk(KERN_ERR "svm_cpu_init: err EOPNOTSUPP on %d\n", me);
		return;
	}
	svm_data = per_cpu(svm_data, me);

	if (!svm_data) {
		printk(KERN_ERR "svm_cpu_init: svm_data is NULL on %d\n",
		       me);
		return;
	}

	svm_data->asid_generation = 1;
	svm_data->max_asid = cpuid_ebx(SVM_CPUID_FUNC) - 1;
	svm_data->next_asid = svm_data->max_asid + 1;

	asm volatile ("sgdt %0" : "=m"(gdt_descr));
	gdt = (struct desc_struct *)gdt_descr.address;
	svm_data->tss_desc = (struct kvm_ldttss_desc *)(gdt + GDT_ENTRY_TSS);

	rdmsrl(MSR_EFER, efer);
	wrmsrl(MSR_EFER, efer | EFER_SVME);

	wrmsrl(MSR_VM_HSAVE_PA,
	       page_to_pfn(svm_data->save_area) << PAGE_SHIFT);
}

static void svm_cpu_uninit(int cpu)
{
	struct svm_cpu_data *svm_data
		= per_cpu(svm_data, raw_smp_processor_id());

	if (!svm_data)
		return;

	per_cpu(svm_data, raw_smp_processor_id()) = NULL;
	__free_page(svm_data->save_area);
	kfree(svm_data);
}

static int svm_cpu_init(int cpu)
{
	struct svm_cpu_data *svm_data;
	int r;

	svm_data = kzalloc(sizeof(struct svm_cpu_data), GFP_KERNEL);
	if (!svm_data)
		return -ENOMEM;
	svm_data->cpu = cpu;
	svm_data->save_area = alloc_page(GFP_KERNEL);
	r = -ENOMEM;
	if (!svm_data->save_area)
		goto err_1;

	per_cpu(svm_data, cpu) = svm_data;

	return 0;

err_1:
	kfree(svm_data);
	return r;

}

static void set_msr_interception(u32 *msrpm, unsigned msr,
				 int read, int write)
{
	int i;

	for (i = 0; i < NUM_MSR_MAPS; i++) {
		if (msr >= msrpm_ranges[i] &&
		    msr < msrpm_ranges[i] + MSRS_IN_RANGE) {
			u32 msr_offset = (i * MSRS_IN_RANGE + msr -
					  msrpm_ranges[i]) * 2;

			u32 *base = msrpm + (msr_offset / 32);
			u32 msr_shift = msr_offset % 32;
			u32 mask = ((write) ? 0 : 2) | ((read) ? 0 : 1);
			*base = (*base & ~(0x3 << msr_shift)) |
				(mask << msr_shift);
			return;
		}
	}
	BUG();
}

static void svm_vcpu_init_msrpm(u32 *msrpm)
{
	memset(msrpm, 0xff, PAGE_SIZE * (1 << MSRPM_ALLOC_ORDER));

#ifdef CONFIG_X86_64
	set_msr_interception(msrpm, MSR_GS_BASE, 1, 1);
	set_msr_interception(msrpm, MSR_FS_BASE, 1, 1);
	set_msr_interception(msrpm, MSR_KERNEL_GS_BASE, 1, 1);
	set_msr_interception(msrpm, MSR_LSTAR, 1, 1);
	set_msr_interception(msrpm, MSR_CSTAR, 1, 1);
	set_msr_interception(msrpm, MSR_SYSCALL_MASK, 1, 1);
#endif
	set_msr_interception(msrpm, MSR_K6_STAR, 1, 1);
	set_msr_interception(msrpm, MSR_IA32_SYSENTER_CS, 1, 1);
	set_msr_interception(msrpm, MSR_IA32_SYSENTER_ESP, 1, 1);
	set_msr_interception(msrpm, MSR_IA32_SYSENTER_EIP, 1, 1);
}

static void svm_enable_lbrv(struct vcpu_svm *svm)
{
	u32 *msrpm = svm->msrpm;

	svm->vmcb->control.lbr_ctl = 1;
	set_msr_interception(msrpm, MSR_IA32_LASTBRANCHFROMIP, 1, 1);
	set_msr_interception(msrpm, MSR_IA32_LASTBRANCHTOIP, 1, 1);
	set_msr_interception(msrpm, MSR_IA32_LASTINTFROMIP, 1, 1);
	set_msr_interception(msrpm, MSR_IA32_LASTINTTOIP, 1, 1);
}

static void svm_disable_lbrv(struct vcpu_svm *svm)
{
	u32 *msrpm = svm->msrpm;

	svm->vmcb->control.lbr_ctl = 0;
	set_msr_interception(msrpm, MSR_IA32_LASTBRANCHFROMIP, 0, 0);
	set_msr_interception(msrpm, MSR_IA32_LASTBRANCHTOIP, 0, 0);
	set_msr_interception(msrpm, MSR_IA32_LASTINTFROMIP, 0, 0);
	set_msr_interception(msrpm, MSR_IA32_LASTINTTOIP, 0, 0);
}

static __init int svm_hardware_setup(void)
{
	int cpu;
	struct page *iopm_pages;
	void *iopm_va;
	int r;

	iopm_pages = alloc_pages(GFP_KERNEL, IOPM_ALLOC_ORDER);

	if (!iopm_pages)
		return -ENOMEM;

	iopm_va = page_address(iopm_pages);
	memset(iopm_va, 0xff, PAGE_SIZE * (1 << IOPM_ALLOC_ORDER));
	iopm_base = page_to_pfn(iopm_pages) << PAGE_SHIFT;

	if (boot_cpu_has(X86_FEATURE_NX))
		kvm_enable_efer_bits(EFER_NX);

	if (boot_cpu_has(X86_FEATURE_FXSR_OPT))
		kvm_enable_efer_bits(EFER_FFXSR);

	if (nested) {
		printk(KERN_INFO "kvm: Nested Virtualization enabled\n");
		kvm_enable_efer_bits(EFER_SVME);
	}

	for_each_online_cpu(cpu) {
		r = svm_cpu_init(cpu);
		if (r)
			goto err;
	}

	svm_features = cpuid_edx(SVM_CPUID_FUNC);

	if (!svm_has(SVM_FEATURE_NPT))
		npt_enabled = false;

	if (npt_enabled && !npt) {
		printk(KERN_INFO "kvm: Nested Paging disabled\n");
		npt_enabled = false;
	}

	if (npt_enabled) {
		printk(KERN_INFO "kvm: Nested Paging enabled\n");
		kvm_enable_tdp();
	} else
		kvm_disable_tdp();

	return 0;

err:
	__free_pages(iopm_pages, IOPM_ALLOC_ORDER);
	iopm_base = 0;
	return r;
}

static __exit void svm_hardware_unsetup(void)
{
	int cpu;

	for_each_online_cpu(cpu)
		svm_cpu_uninit(cpu);

	__free_pages(pfn_to_page(iopm_base >> PAGE_SHIFT), IOPM_ALLOC_ORDER);
	iopm_base = 0;
}

static void init_seg(struct vmcb_seg *seg)
{
	seg->selector = 0;
	seg->attrib = SVM_SELECTOR_P_MASK | SVM_SELECTOR_S_MASK |
		SVM_SELECTOR_WRITE_MASK; /* Read/Write Data Segment */
	seg->limit = 0xffff;
	seg->base = 0;
}

static void init_sys_seg(struct vmcb_seg *seg, uint32_t type)
{
	seg->selector = 0;
	seg->attrib = SVM_SELECTOR_P_MASK | type;
	seg->limit = 0xffff;
	seg->base = 0;
}

static void init_vmcb(struct vcpu_svm *svm)
{
	struct vmcb_control_area *control = &svm->vmcb->control;
	struct vmcb_save_area *save = &svm->vmcb->save;

	control->intercept_cr_read = 	INTERCEPT_CR0_MASK |
					INTERCEPT_CR3_MASK |
					INTERCEPT_CR4_MASK;

	control->intercept_cr_write = 	INTERCEPT_CR0_MASK |
					INTERCEPT_CR3_MASK |
					INTERCEPT_CR4_MASK |
					INTERCEPT_CR8_MASK;

	control->intercept_dr_read = 	INTERCEPT_DR0_MASK |
					INTERCEPT_DR1_MASK |
					INTERCEPT_DR2_MASK |
					INTERCEPT_DR3_MASK;

	control->intercept_dr_write = 	INTERCEPT_DR0_MASK |
					INTERCEPT_DR1_MASK |
					INTERCEPT_DR2_MASK |
					INTERCEPT_DR3_MASK |
					INTERCEPT_DR5_MASK |
					INTERCEPT_DR7_MASK;

	control->intercept_exceptions = (1 << PF_VECTOR) |
					(1 << UD_VECTOR) |
					(1 << MC_VECTOR);


	control->intercept = 	(1ULL << INTERCEPT_INTR) |
				(1ULL << INTERCEPT_NMI) |
				(1ULL << INTERCEPT_SMI) |
				(1ULL << INTERCEPT_CPUID) |
				(1ULL << INTERCEPT_INVD) |
				(1ULL << INTERCEPT_HLT) |
				(1ULL << INTERCEPT_INVLPG) |
				(1ULL << INTERCEPT_INVLPGA) |
				(1ULL << INTERCEPT_IOIO_PROT) |
				(1ULL << INTERCEPT_MSR_PROT) |
				(1ULL << INTERCEPT_TASK_SWITCH) |
				(1ULL << INTERCEPT_SHUTDOWN) |
				(1ULL << INTERCEPT_VMRUN) |
				(1ULL << INTERCEPT_VMMCALL) |
				(1ULL << INTERCEPT_VMLOAD) |
				(1ULL << INTERCEPT_VMSAVE) |
				(1ULL << INTERCEPT_STGI) |
				(1ULL << INTERCEPT_CLGI) |
				(1ULL << INTERCEPT_SKINIT) |
				(1ULL << INTERCEPT_WBINVD) |
				(1ULL << INTERCEPT_MONITOR) |
				(1ULL << INTERCEPT_MWAIT);

	control->iopm_base_pa = iopm_base;
	control->msrpm_base_pa = __pa(svm->msrpm);
	control->tsc_offset = 0;
	control->int_ctl = V_INTR_MASKING_MASK;

	init_seg(&save->es);
	init_seg(&save->ss);
	init_seg(&save->ds);
	init_seg(&save->fs);
	init_seg(&save->gs);

	save->cs.selector = 0xf000;
	/* Executable/Readable Code Segment */
	save->cs.attrib = SVM_SELECTOR_READ_MASK | SVM_SELECTOR_P_MASK |
		SVM_SELECTOR_S_MASK | SVM_SELECTOR_CODE_MASK;
	save->cs.limit = 0xffff;
	/*
	 * cs.base should really be 0xffff0000, but vmx can't handle that, so
	 * be consistent with it.
	 *
	 * Replace when we have real mode working for vmx.
	 */
	save->cs.base = 0xf0000;

	save->gdtr.limit = 0xffff;
	save->idtr.limit = 0xffff;

	init_sys_seg(&save->ldtr, SEG_TYPE_LDT);
	init_sys_seg(&save->tr, SEG_TYPE_BUSY_TSS16);

	save->efer = EFER_SVME;
	save->dr6 = 0xffff0ff0;
	save->dr7 = 0x400;
	save->rflags = 2;
	save->rip = 0x0000fff0;
	svm->vcpu.arch.regs[VCPU_REGS_RIP] = save->rip;

	/*
	 * cr0 val on cpu init should be 0x60000010, we enable cpu
	 * cache by default. the orderly way is to enable cache in bios.
	 */
	save->cr0 = 0x00000010 | X86_CR0_PG | X86_CR0_WP;
	save->cr4 = X86_CR4_PAE;
	/* rdx = ?? */

	if (npt_enabled) {
		/* Setup VMCB for Nested Paging */
		control->nested_ctl = 1;
		control->intercept &= ~((1ULL << INTERCEPT_TASK_SWITCH) |
					(1ULL << INTERCEPT_INVLPG));
		control->intercept_exceptions &= ~(1 << PF_VECTOR);
		control->intercept_cr_read &= ~(INTERCEPT_CR0_MASK|
						INTERCEPT_CR3_MASK);
		control->intercept_cr_write &= ~(INTERCEPT_CR0_MASK|
						 INTERCEPT_CR3_MASK);
		save->g_pat = 0x0007040600070406ULL;
		/* enable caching because the QEMU Bios doesn't enable it */
		save->cr0 = X86_CR0_ET;
		save->cr3 = 0;
		save->cr4 = 0;
	}
	force_new_asid(&svm->vcpu);

	svm->nested_vmcb = 0;
	svm->vcpu.arch.hflags = HF_GIF_MASK;
}

static int svm_vcpu_reset(struct kvm_vcpu *vcpu)
{
	struct vcpu_svm *svm = to_svm(vcpu);

	init_vmcb(svm);

	if (vcpu->vcpu_id != 0) {
		kvm_rip_write(vcpu, 0);
		svm->vmcb->save.cs.base = svm->vcpu.arch.sipi_vector << 12;
		svm->vmcb->save.cs.selector = svm->vcpu.arch.sipi_vector << 8;
	}
	vcpu->arch.regs_avail = ~0;
	vcpu->arch.regs_dirty = ~0;

	return 0;
}

static struct kvm_vcpu *svm_create_vcpu(struct kvm *kvm, unsigned int id)
{
	struct vcpu_svm *svm;
	struct page *page;
	struct page *msrpm_pages;
	struct page *hsave_page;
	struct page *nested_msrpm_pages;
	int err;

	svm = kmem_cache_zalloc(kvm_vcpu_cache, GFP_KERNEL);
	if (!svm) {
		err = -ENOMEM;
		goto out;
	}

	err = kvm_vcpu_init(&svm->vcpu, kvm, id);
	if (err)
		goto free_svm;

	page = alloc_page(GFP_KERNEL);
	if (!page) {
		err = -ENOMEM;
		goto uninit;
	}

	err = -ENOMEM;
	msrpm_pages = alloc_pages(GFP_KERNEL, MSRPM_ALLOC_ORDER);
	if (!msrpm_pages)
		goto uninit;

	nested_msrpm_pages = alloc_pages(GFP_KERNEL, MSRPM_ALLOC_ORDER);
	if (!nested_msrpm_pages)
		goto uninit;

	svm->msrpm = page_address(msrpm_pages);
	svm_vcpu_init_msrpm(svm->msrpm);

	hsave_page = alloc_page(GFP_KERNEL);
	if (!hsave_page)
		goto uninit;
	svm->hsave = page_address(hsave_page);

	svm->nested_msrpm = page_address(nested_msrpm_pages);

	svm->vmcb = page_address(page);
	clear_page(svm->vmcb);
	svm->vmcb_pa = page_to_pfn(page) << PAGE_SHIFT;
	svm->asid_generation = 0;
	init_vmcb(svm);

	fx_init(&svm->vcpu);
	svm->vcpu.fpu_active = 1;
	svm->vcpu.arch.apic_base = 0xfee00000 | MSR_IA32_APICBASE_ENABLE;
	if (svm->vcpu.vcpu_id == 0)
		svm->vcpu.arch.apic_base |= MSR_IA32_APICBASE_BSP;

	return &svm->vcpu;

uninit:
	kvm_vcpu_uninit(&svm->vcpu);
free_svm:
	kmem_cache_free(kvm_vcpu_cache, svm);
out:
	return ERR_PTR(err);
}

static void svm_free_vcpu(struct kvm_vcpu *vcpu)
{
	struct vcpu_svm *svm = to_svm(vcpu);

	__free_page(pfn_to_page(svm->vmcb_pa >> PAGE_SHIFT));
	__free_pages(virt_to_page(svm->msrpm), MSRPM_ALLOC_ORDER);
	__free_page(virt_to_page(svm->hsave));
	__free_pages(virt_to_page(svm->nested_msrpm), MSRPM_ALLOC_ORDER);
	kvm_vcpu_uninit(vcpu);
	kmem_cache_free(kvm_vcpu_cache, svm);
}

static void svm_vcpu_load(struct kvm_vcpu *vcpu, int cpu)
{
	struct vcpu_svm *svm = to_svm(vcpu);
	int i;

	if (unlikely(cpu != vcpu->cpu)) {
		u64 tsc_this, delta;

		/*
		 * Make sure that the guest sees a monotonically
		 * increasing TSC.
		 */
		rdtscll(tsc_this);
		delta = vcpu->arch.host_tsc - tsc_this;
		svm->vmcb->control.tsc_offset += delta;
		vcpu->cpu = cpu;
		kvm_migrate_timers(vcpu);
	}

	for (i = 0; i < NR_HOST_SAVE_USER_MSRS; i++)
		rdmsrl(host_save_user_msrs[i], svm->host_user_msrs[i]);
}

static void svm_vcpu_put(struct kvm_vcpu *vcpu)
{
	struct vcpu_svm *svm = to_svm(vcpu);
	int i;

	++vcpu->stat.host_state_reload;
	for (i = 0; i < NR_HOST_SAVE_USER_MSRS; i++)
		wrmsrl(host_save_user_msrs[i], svm->host_user_msrs[i]);

	rdtscll(vcpu->arch.host_tsc);
}

static unsigned long svm_get_rflags(struct kvm_vcpu *vcpu)
{
	return to_svm(vcpu)->vmcb->save.rflags;
}

static void svm_set_rflags(struct kvm_vcpu *vcpu, unsigned long rflags)
{
	to_svm(vcpu)->vmcb->save.rflags = rflags;
}

static void svm_set_vintr(struct vcpu_svm *svm)
{
	svm->vmcb->control.intercept |= 1ULL << INTERCEPT_VINTR;
}

static void svm_clear_vintr(struct vcpu_svm *svm)
{
	svm->vmcb->control.intercept &= ~(1ULL << INTERCEPT_VINTR);
}

static struct vmcb_seg *svm_seg(struct kvm_vcpu *vcpu, int seg)
{
	struct vmcb_save_area *save = &to_svm(vcpu)->vmcb->save;

	switch (seg) {
	case VCPU_SREG_CS: return &save->cs;
	case VCPU_SREG_DS: return &save->ds;
	case VCPU_SREG_ES: return &save->es;
	case VCPU_SREG_FS: return &save->fs;
	case VCPU_SREG_GS: return &save->gs;
	case VCPU_SREG_SS: return &save->ss;
	case VCPU_SREG_TR: return &save->tr;
	case VCPU_SREG_LDTR: return &save->ldtr;
	}
	BUG();
	return NULL;
}

static u64 svm_get_segment_base(struct kvm_vcpu *vcpu, int seg)
{
	struct vmcb_seg *s = svm_seg(vcpu, seg);

	return s->base;
}

static void svm_get_segment(struct kvm_vcpu *vcpu,
			    struct kvm_segment *var, int seg)
{
	struct vmcb_seg *s = svm_seg(vcpu, seg);

	var->base = s->base;
	var->limit = s->limit;
	var->selector = s->selector;
	var->type = s->attrib & SVM_SELECTOR_TYPE_MASK;
	var->s = (s->attrib >> SVM_SELECTOR_S_SHIFT) & 1;
	var->dpl = (s->attrib >> SVM_SELECTOR_DPL_SHIFT) & 3;
	var->present = (s->attrib >> SVM_SELECTOR_P_SHIFT) & 1;
	var->avl = (s->attrib >> SVM_SELECTOR_AVL_SHIFT) & 1;
	var->l = (s->attrib >> SVM_SELECTOR_L_SHIFT) & 1;
	var->db = (s->attrib >> SVM_SELECTOR_DB_SHIFT) & 1;
	var->g = (s->attrib >> SVM_SELECTOR_G_SHIFT) & 1;

	/* AMD's VMCB does not have an explicit unusable field, so emulate it
	 * for cross vendor migration purposes by "not present"
	 */
	var->unusable = !var->present || (var->type == 0);

	switch (seg) {
	case VCPU_SREG_CS:
		/*
		 * SVM always stores 0 for the 'G' bit in the CS selector in
		 * the VMCB on a VMEXIT. This hurts cross-vendor migration:
		 * Intel's VMENTRY has a check on the 'G' bit.
		 */
		var->g = s->limit > 0xfffff;
		break;
	case VCPU_SREG_TR:
		/*
		 * Work around a bug where the busy flag in the tr selector
		 * isn't exposed
		 */
		var->type |= 0x2;
		break;
	case VCPU_SREG_DS:
	case VCPU_SREG_ES:
	case VCPU_SREG_FS:
	case VCPU_SREG_GS:
		/*
		 * The accessed bit must always be set in the segment
		 * descriptor cache, although it can be cleared in the
		 * descriptor, the cached bit always remains at 1. Since
		 * Intel has a check on this, set it here to support
		 * cross-vendor migration.
		 */
		if (!var->unusable)
			var->type |= 0x1;
		break;
	}
}

static int svm_get_cpl(struct kvm_vcpu *vcpu)
{
	struct vmcb_save_area *save = &to_svm(vcpu)->vmcb->save;

	return save->cpl;
}

static void svm_get_idt(struct kvm_vcpu *vcpu, struct descriptor_table *dt)
{
	struct vcpu_svm *svm = to_svm(vcpu);

	dt->limit = svm->vmcb->save.idtr.limit;
	dt->base = svm->vmcb->save.idtr.base;
}

static void svm_set_idt(struct kvm_vcpu *vcpu, struct descriptor_table *dt)
{
	struct vcpu_svm *svm = to_svm(vcpu);

	svm->vmcb->save.idtr.limit = dt->limit;
	svm->vmcb->save.idtr.base = dt->base ;
}

static void svm_get_gdt(struct kvm_vcpu *vcpu, struct descriptor_table *dt)
{
	struct vcpu_svm *svm = to_svm(vcpu);

	dt->limit = svm->vmcb->save.gdtr.limit;
	dt->base = svm->vmcb->save.gdtr.base;
}

static void svm_set_gdt(struct kvm_vcpu *vcpu, struct descriptor_table *dt)
{
	struct vcpu_svm *svm = to_svm(vcpu);

	svm->vmcb->save.gdtr.limit = dt->limit;
	svm->vmcb->save.gdtr.base = dt->base ;
}

static void svm_decache_cr4_guest_bits(struct kvm_vcpu *vcpu)
{
}

static void svm_set_cr0(struct kvm_vcpu *vcpu, unsigned long cr0)
{
	struct vcpu_svm *svm = to_svm(vcpu);

#ifdef CONFIG_X86_64
	if (vcpu->arch.shadow_efer & EFER_LME) {
		if (!is_paging(vcpu) && (cr0 & X86_CR0_PG)) {
			vcpu->arch.shadow_efer |= EFER_LMA;
			svm->vmcb->save.efer |= EFER_LMA | EFER_LME;
		}

		if (is_paging(vcpu) && !(cr0 & X86_CR0_PG)) {
			vcpu->arch.shadow_efer &= ~EFER_LMA;
			svm->vmcb->save.efer &= ~(EFER_LMA | EFER_LME);
		}
	}
#endif
	if (npt_enabled)
		goto set;

	if ((vcpu->arch.cr0 & X86_CR0_TS) && !(cr0 & X86_CR0_TS)) {
		svm->vmcb->control.intercept_exceptions &= ~(1 << NM_VECTOR);
		vcpu->fpu_active = 1;
	}

	vcpu->arch.cr0 = cr0;
	cr0 |= X86_CR0_PG | X86_CR0_WP;
	if (!vcpu->fpu_active) {
		svm->vmcb->control.intercept_exceptions |= (1 << NM_VECTOR);
		cr0 |= X86_CR0_TS;
	}
set:
	/*
	 * re-enable caching here because the QEMU bios
	 * does not do it - this results in some delay at
	 * reboot
	 */
	cr0 &= ~(X86_CR0_CD | X86_CR0_NW);
	svm->vmcb->save.cr0 = cr0;
}

static void svm_set_cr4(struct kvm_vcpu *vcpu, unsigned long cr4)
{
	unsigned long host_cr4_mce = read_cr4() & X86_CR4_MCE;
	unsigned long old_cr4 = to_svm(vcpu)->vmcb->save.cr4;

	if (npt_enabled && ((old_cr4 ^ cr4) & X86_CR4_PGE))
		force_new_asid(vcpu);

	vcpu->arch.cr4 = cr4;
	if (!npt_enabled)
		cr4 |= X86_CR4_PAE;
	cr4 |= host_cr4_mce;
	to_svm(vcpu)->vmcb->save.cr4 = cr4;
}

static void svm_set_segment(struct kvm_vcpu *vcpu,
			    struct kvm_segment *var, int seg)
{
	struct vcpu_svm *svm = to_svm(vcpu);
	struct vmcb_seg *s = svm_seg(vcpu, seg);

	s->base = var->base;
	s->limit = var->limit;
	s->selector = var->selector;
	if (var->unusable)
		s->attrib = 0;
	else {
		s->attrib = (var->type & SVM_SELECTOR_TYPE_MASK);
		s->attrib |= (var->s & 1) << SVM_SELECTOR_S_SHIFT;
		s->attrib |= (var->dpl & 3) << SVM_SELECTOR_DPL_SHIFT;
		s->attrib |= (var->present & 1) << SVM_SELECTOR_P_SHIFT;
		s->attrib |= (var->avl & 1) << SVM_SELECTOR_AVL_SHIFT;
		s->attrib |= (var->l & 1) << SVM_SELECTOR_L_SHIFT;
		s->attrib |= (var->db & 1) << SVM_SELECTOR_DB_SHIFT;
		s->attrib |= (var->g & 1) << SVM_SELECTOR_G_SHIFT;
	}
	if (seg == VCPU_SREG_CS)
		svm->vmcb->save.cpl
			= (svm->vmcb->save.cs.attrib
			   >> SVM_SELECTOR_DPL_SHIFT) & 3;

}

static int svm_guest_debug(struct kvm_vcpu *vcpu, struct kvm_guest_debug *dbg)
{
	int old_debug = vcpu->guest_debug;
	struct vcpu_svm *svm = to_svm(vcpu);

	vcpu->guest_debug = dbg->control;

	svm->vmcb->control.intercept_exceptions &=
		~((1 << DB_VECTOR) | (1 << BP_VECTOR));
	if (vcpu->guest_debug & KVM_GUESTDBG_ENABLE) {
		if (vcpu->guest_debug &
		    (KVM_GUESTDBG_SINGLESTEP | KVM_GUESTDBG_USE_HW_BP))
			svm->vmcb->control.intercept_exceptions |=
				1 << DB_VECTOR;
		if (vcpu->guest_debug & KVM_GUESTDBG_USE_SW_BP)
			svm->vmcb->control.intercept_exceptions |=
				1 << BP_VECTOR;
	} else
		vcpu->guest_debug = 0;

	if (vcpu->guest_debug & KVM_GUESTDBG_USE_HW_BP)
		svm->vmcb->save.dr7 = dbg->arch.debugreg[7];
	else
		svm->vmcb->save.dr7 = vcpu->arch.dr7;

	if (vcpu->guest_debug & KVM_GUESTDBG_SINGLESTEP)
		svm->vmcb->save.rflags |= X86_EFLAGS_TF | X86_EFLAGS_RF;
	else if (old_debug & KVM_GUESTDBG_SINGLESTEP)
		svm->vmcb->save.rflags &= ~(X86_EFLAGS_TF | X86_EFLAGS_RF);

	return 0;
}

static int svm_get_irq(struct kvm_vcpu *vcpu)
{
	struct vcpu_svm *svm = to_svm(vcpu);
	u32 exit_int_info = svm->vmcb->control.exit_int_info;

	if (is_external_interrupt(exit_int_info))
		return exit_int_info & SVM_EVTINJ_VEC_MASK;
	return -1;
}

static void load_host_msrs(struct kvm_vcpu *vcpu)
{
#ifdef CONFIG_X86_64
	wrmsrl(MSR_GS_BASE, to_svm(vcpu)->host_gs_base);
#endif
}

static void save_host_msrs(struct kvm_vcpu *vcpu)
{
#ifdef CONFIG_X86_64
	rdmsrl(MSR_GS_BASE, to_svm(vcpu)->host_gs_base);
#endif
}

static void new_asid(struct vcpu_svm *svm, struct svm_cpu_data *svm_data)
{
	if (svm_data->next_asid > svm_data->max_asid) {
		++svm_data->asid_generation;
		svm_data->next_asid = 1;
		svm->vmcb->control.tlb_ctl = TLB_CONTROL_FLUSH_ALL_ASID;
	}

	svm->vcpu.cpu = svm_data->cpu;
	svm->asid_generation = svm_data->asid_generation;
	svm->vmcb->control.asid = svm_data->next_asid++;
}

static unsigned long svm_get_dr(struct kvm_vcpu *vcpu, int dr)
{
	struct vcpu_svm *svm = to_svm(vcpu);
	unsigned long val;

	switch (dr) {
	case 0 ... 3:
		val = vcpu->arch.db[dr];
		break;
	case 6:
		if (vcpu->guest_debug & KVM_GUESTDBG_USE_HW_BP)
			val = vcpu->arch.dr6;
		else
			val = svm->vmcb->save.dr6;
		break;
	case 7:
		if (vcpu->guest_debug & KVM_GUESTDBG_USE_HW_BP)
			val = vcpu->arch.dr7;
		else
			val = svm->vmcb->save.dr7;
		break;
	default:
		val = 0;
	}

	KVMTRACE_2D(DR_READ, vcpu, (u32)dr, (u32)val, handler);
	return val;
}

static void svm_set_dr(struct kvm_vcpu *vcpu, int dr, unsigned long value,
		       int *exception)
{
	struct vcpu_svm *svm = to_svm(vcpu);

	KVMTRACE_2D(DR_WRITE, vcpu, (u32)dr, (u32)value, handler);

	*exception = 0;

	switch (dr) {
	case 0 ... 3:
		vcpu->arch.db[dr] = value;
		if (!(vcpu->guest_debug & KVM_GUESTDBG_USE_HW_BP))
			vcpu->arch.eff_db[dr] = value;
		return;
	case 4 ... 5:
		if (vcpu->arch.cr4 & X86_CR4_DE)
			*exception = UD_VECTOR;
		return;
	case 6:
		if (value & 0xffffffff00000000ULL) {
			*exception = GP_VECTOR;
			return;
		}
		vcpu->arch.dr6 = (value & DR6_VOLATILE) | DR6_FIXED_1;
		return;
	case 7:
		if (value & 0xffffffff00000000ULL) {
			*exception = GP_VECTOR;
			return;
		}
		vcpu->arch.dr7 = (value & DR7_VOLATILE) | DR7_FIXED_1;
		if (!(vcpu->guest_debug & KVM_GUESTDBG_USE_HW_BP)) {
			svm->vmcb->save.dr7 = vcpu->arch.dr7;
			vcpu->arch.switch_db_regs = (value & DR7_BP_EN_MASK);
		}
		return;
	default:
		/* FIXME: Possible case? */
		printk(KERN_DEBUG "%s: unexpected dr %u\n",
		       __func__, dr);
		*exception = UD_VECTOR;
		return;
	}
}

static int pf_interception(struct vcpu_svm *svm, struct kvm_run *kvm_run)
{
	u32 exit_int_info = svm->vmcb->control.exit_int_info;
	struct kvm *kvm = svm->vcpu.kvm;
	u64 fault_address;
	u32 error_code;
	bool event_injection = false;

	if (!irqchip_in_kernel(kvm) &&
	    is_external_interrupt(exit_int_info)) {
		event_injection = true;
		push_irq(&svm->vcpu, exit_int_info & SVM_EVTINJ_VEC_MASK);
	}

	fault_address  = svm->vmcb->control.exit_info_2;
	error_code = svm->vmcb->control.exit_info_1;

	if (!npt_enabled)
		KVMTRACE_3D(PAGE_FAULT, &svm->vcpu, error_code,
			    (u32)fault_address, (u32)(fault_address >> 32),
			    handler);
	else
		KVMTRACE_3D(TDP_FAULT, &svm->vcpu, error_code,
			    (u32)fault_address, (u32)(fault_address >> 32),
			    handler);
	/*
	 * FIXME: Tis shouldn't be necessary here, but there is a flush
	 * missing in the MMU code. Until we find this bug, flush the
	 * complete TLB here on an NPF
	 */
	if (npt_enabled)
		svm_flush_tlb(&svm->vcpu);

	if (!npt_enabled && event_injection)
		kvm_mmu_unprotect_page_virt(&svm->vcpu, fault_address);
	return kvm_mmu_page_fault(&svm->vcpu, fault_address, error_code);
}

static int db_interception(struct vcpu_svm *svm, struct kvm_run *kvm_run)
{
	if (!(svm->vcpu.guest_debug &
	      (KVM_GUESTDBG_SINGLESTEP | KVM_GUESTDBG_USE_HW_BP))) {
		kvm_queue_exception(&svm->vcpu, DB_VECTOR);
		return 1;
	}
	kvm_run->exit_reason = KVM_EXIT_DEBUG;
	kvm_run->debug.arch.pc = svm->vmcb->save.cs.base + svm->vmcb->save.rip;
	kvm_run->debug.arch.exception = DB_VECTOR;
	return 0;
}

static int bp_interception(struct vcpu_svm *svm, struct kvm_run *kvm_run)
{
	kvm_run->exit_reason = KVM_EXIT_DEBUG;
	kvm_run->debug.arch.pc = svm->vmcb->save.cs.base + svm->vmcb->save.rip;
	kvm_run->debug.arch.exception = BP_VECTOR;
	return 0;
}

static int ud_interception(struct vcpu_svm *svm, struct kvm_run *kvm_run)
{
	int er;

	er = emulate_instruction(&svm->vcpu, kvm_run, 0, 0, EMULTYPE_TRAP_UD);
	if (er != EMULATE_DONE)
		kvm_queue_exception(&svm->vcpu, UD_VECTOR);
	return 1;
}

static int nm_interception(struct vcpu_svm *svm, struct kvm_run *kvm_run)
{
	svm->vmcb->control.intercept_exceptions &= ~(1 << NM_VECTOR);
	if (!(svm->vcpu.arch.cr0 & X86_CR0_TS))
		svm->vmcb->save.cr0 &= ~X86_CR0_TS;
	svm->vcpu.fpu_active = 1;

	return 1;
}

static int mc_interception(struct vcpu_svm *svm, struct kvm_run *kvm_run)
{
	/*
	 * On an #MC intercept the MCE handler is not called automatically in
	 * the host. So do it by hand here.
	 */
	asm volatile (
		"int $0x12\n");
	/* not sure if we ever come back to this point */

	return 1;
}

static int shutdown_interception(struct vcpu_svm *svm, struct kvm_run *kvm_run)
{
	/*
	 * VMCB is undefined after a SHUTDOWN intercept
	 * so reinitialize it.
	 */
	clear_page(svm->vmcb);
	init_vmcb(svm);

	kvm_run->exit_reason = KVM_EXIT_SHUTDOWN;
	return 0;
}

static int io_interception(struct vcpu_svm *svm, struct kvm_run *kvm_run)
{
	u32 io_info = svm->vmcb->control.exit_info_1; /* address size bug? */
	int size, in, string;
	unsigned port;

	++svm->vcpu.stat.io_exits;

	svm->next_rip = svm->vmcb->control.exit_info_2;

	string = (io_info & SVM_IOIO_STR_MASK) != 0;

	if (string) {
		if (emulate_instruction(&svm->vcpu,
					kvm_run, 0, 0, 0) == EMULATE_DO_MMIO)
			return 0;
		return 1;
	}

	in = (io_info & SVM_IOIO_TYPE_MASK) != 0;
	port = io_info >> 16;
	size = (io_info & SVM_IOIO_SIZE_MASK) >> SVM_IOIO_SIZE_SHIFT;

	skip_emulated_instruction(&svm->vcpu);
	return kvm_emulate_pio(&svm->vcpu, kvm_run, in, size, port);
}

static int nmi_interception(struct vcpu_svm *svm, struct kvm_run *kvm_run)
{
	KVMTRACE_0D(NMI, &svm->vcpu, handler);
	return 1;
}

static int intr_interception(struct vcpu_svm *svm, struct kvm_run *kvm_run)
{
	++svm->vcpu.stat.irq_exits;
	KVMTRACE_0D(INTR, &svm->vcpu, handler);
	return 1;
}

static int nop_on_interception(struct vcpu_svm *svm, struct kvm_run *kvm_run)
{
	return 1;
}

static int halt_interception(struct vcpu_svm *svm, struct kvm_run *kvm_run)
{
	svm->next_rip = kvm_rip_read(&svm->vcpu) + 1;
	skip_emulated_instruction(&svm->vcpu);
	return kvm_emulate_halt(&svm->vcpu);
}

static int vmmcall_interception(struct vcpu_svm *svm, struct kvm_run *kvm_run)
{
	svm->next_rip = kvm_rip_read(&svm->vcpu) + 3;
	skip_emulated_instruction(&svm->vcpu);
	kvm_emulate_hypercall(&svm->vcpu);
	return 1;
}

static int nested_svm_check_permissions(struct vcpu_svm *svm)
{
	if (!(svm->vcpu.arch.shadow_efer & EFER_SVME)
	    || !is_paging(&svm->vcpu)) {
		kvm_queue_exception(&svm->vcpu, UD_VECTOR);
		return 1;
	}

	if (svm->vmcb->save.cpl) {
		kvm_inject_gp(&svm->vcpu, 0);
		return 1;
	}

       return 0;
}

static int nested_svm_check_exception(struct vcpu_svm *svm, unsigned nr,
				      bool has_error_code, u32 error_code)
{
	if (is_nested(svm)) {
		svm->vmcb->control.exit_code = SVM_EXIT_EXCP_BASE + nr;
		svm->vmcb->control.exit_code_hi = 0;
		svm->vmcb->control.exit_info_1 = error_code;
		svm->vmcb->control.exit_info_2 = svm->vcpu.arch.cr2;
		if (nested_svm_exit_handled(svm, false)) {
			nsvm_printk("VMexit -> EXCP 0x%x\n", nr);

			nested_svm_vmexit(svm);
			return 1;
		}
	}

	return 0;
}

static inline int nested_svm_intr(struct vcpu_svm *svm)
{
	if (is_nested(svm)) {
		if (!(svm->vcpu.arch.hflags & HF_VINTR_MASK))
			return 0;

		if (!(svm->vcpu.arch.hflags & HF_HIF_MASK))
			return 0;

		svm->vmcb->control.exit_code = SVM_EXIT_INTR;

		if (nested_svm_exit_handled(svm, false)) {
			nsvm_printk("VMexit -> INTR\n");
			nested_svm_vmexit(svm);
			return 1;
		}
	}

	return 0;
}

static struct page *nested_svm_get_page(struct vcpu_svm *svm, u64 gpa)
{
	struct page *page;

	down_read(&current->mm->mmap_sem);
	page = gfn_to_page(svm->vcpu.kvm, gpa >> PAGE_SHIFT);
	up_read(&current->mm->mmap_sem);

	if (is_error_page(page)) {
		printk(KERN_INFO "%s: could not find page at 0x%llx\n",
		       __func__, gpa);
		kvm_release_page_clean(page);
		kvm_inject_gp(&svm->vcpu, 0);
		return NULL;
	}
	return page;
}

static int nested_svm_do(struct vcpu_svm *svm,
			 u64 arg1_gpa, u64 arg2_gpa, void *opaque,
			 int (*handler)(struct vcpu_svm *svm,
					void *arg1,
					void *arg2,
					void *opaque))
{
	struct page *arg1_page;
	struct page *arg2_page = NULL;
	void *arg1;
	void *arg2 = NULL;
	int retval;

	arg1_page = nested_svm_get_page(svm, arg1_gpa);
	if(arg1_page == NULL)
		return 1;

	if (arg2_gpa) {
		arg2_page = nested_svm_get_page(svm, arg2_gpa);
		if(arg2_page == NULL) {
			kvm_release_page_clean(arg1_page);
			return 1;
		}
	}

	arg1 = kmap_atomic(arg1_page, KM_USER0);
	if (arg2_gpa)
		arg2 = kmap_atomic(arg2_page, KM_USER1);

	retval = handler(svm, arg1, arg2, opaque);

	kunmap_atomic(arg1, KM_USER0);
	if (arg2_gpa)
		kunmap_atomic(arg2, KM_USER1);

	kvm_release_page_dirty(arg1_page);
	if (arg2_gpa)
		kvm_release_page_dirty(arg2_page);

	return retval;
}

static int nested_svm_exit_handled_real(struct vcpu_svm *svm,
					void *arg1,
					void *arg2,
					void *opaque)
{
	struct vmcb *nested_vmcb = (struct vmcb *)arg1;
	bool kvm_overrides = *(bool *)opaque;
	u32 exit_code = svm->vmcb->control.exit_code;

	if (kvm_overrides) {
		switch (exit_code) {
		case SVM_EXIT_INTR:
		case SVM_EXIT_NMI:
			return 0;
		/* For now we are always handling NPFs when using them */
		case SVM_EXIT_NPF:
			if (npt_enabled)
				return 0;
			break;
		/* When we're shadowing, trap PFs */
		case SVM_EXIT_EXCP_BASE + PF_VECTOR:
			if (!npt_enabled)
				return 0;
			break;
		default:
			break;
		}
	}

	switch (exit_code) {
	case SVM_EXIT_READ_CR0 ... SVM_EXIT_READ_CR8: {
		u32 cr_bits = 1 << (exit_code - SVM_EXIT_READ_CR0);
		if (nested_vmcb->control.intercept_cr_read & cr_bits)
			return 1;
		break;
	}
	case SVM_EXIT_WRITE_CR0 ... SVM_EXIT_WRITE_CR8: {
		u32 cr_bits = 1 << (exit_code - SVM_EXIT_WRITE_CR0);
		if (nested_vmcb->control.intercept_cr_write & cr_bits)
			return 1;
		break;
	}
	case SVM_EXIT_READ_DR0 ... SVM_EXIT_READ_DR7: {
		u32 dr_bits = 1 << (exit_code - SVM_EXIT_READ_DR0);
		if (nested_vmcb->control.intercept_dr_read & dr_bits)
			return 1;
		break;
	}
	case SVM_EXIT_WRITE_DR0 ... SVM_EXIT_WRITE_DR7: {
		u32 dr_bits = 1 << (exit_code - SVM_EXIT_WRITE_DR0);
		if (nested_vmcb->control.intercept_dr_write & dr_bits)
			return 1;
		break;
	}
	case SVM_EXIT_EXCP_BASE ... SVM_EXIT_EXCP_BASE + 0x1f: {
		u32 excp_bits = 1 << (exit_code - SVM_EXIT_EXCP_BASE);
		if (nested_vmcb->control.intercept_exceptions & excp_bits)
			return 1;
		break;
	}
	default: {
		u64 exit_bits = 1ULL << (exit_code - SVM_EXIT_INTR);
		nsvm_printk("exit code: 0x%x\n", exit_code);
		if (nested_vmcb->control.intercept & exit_bits)
			return 1;
	}
	}

	return 0;
}

static int nested_svm_exit_handled_msr(struct vcpu_svm *svm,
				       void *arg1, void *arg2,
				       void *opaque)
{
	struct vmcb *nested_vmcb = (struct vmcb *)arg1;
	u8 *msrpm = (u8 *)arg2;
        u32 t0, t1;
	u32 msr = svm->vcpu.arch.regs[VCPU_REGS_RCX];
	u32 param = svm->vmcb->control.exit_info_1 & 1;

	if (!(nested_vmcb->control.intercept & (1ULL << INTERCEPT_MSR_PROT)))
		return 0;

	switch(msr) {
	case 0 ... 0x1fff:
		t0 = (msr * 2) % 8;
		t1 = msr / 8;
		break;
	case 0xc0000000 ... 0xc0001fff:
		t0 = (8192 + msr - 0xc0000000) * 2;
		t1 = (t0 / 8);
		t0 %= 8;
		break;
	case 0xc0010000 ... 0xc0011fff:
		t0 = (16384 + msr - 0xc0010000) * 2;
		t1 = (t0 / 8);
		t0 %= 8;
		break;
	default:
		return 1;
		break;
	}
	if (msrpm[t1] & ((1 << param) << t0))
		return 1;

	return 0;
}

static int nested_svm_exit_handled(struct vcpu_svm *svm, bool kvm_override)
{
	bool k = kvm_override;

	switch (svm->vmcb->control.exit_code) {
	case SVM_EXIT_MSR:
		return nested_svm_do(svm, svm->nested_vmcb,
				     svm->nested_vmcb_msrpm, NULL,
				     nested_svm_exit_handled_msr);
	default: break;
	}

	return nested_svm_do(svm, svm->nested_vmcb, 0, &k,
			     nested_svm_exit_handled_real);
}

static int nested_svm_vmexit_real(struct vcpu_svm *svm, void *arg1,
				  void *arg2, void *opaque)
{
	struct vmcb *nested_vmcb = (struct vmcb *)arg1;
	struct vmcb *hsave = svm->hsave;
	u64 nested_save[] = { nested_vmcb->save.cr0,
			      nested_vmcb->save.cr3,
			      nested_vmcb->save.cr4,
			      nested_vmcb->save.efer,
			      nested_vmcb->control.intercept_cr_read,
			      nested_vmcb->control.intercept_cr_write,
			      nested_vmcb->control.intercept_dr_read,
			      nested_vmcb->control.intercept_dr_write,
			      nested_vmcb->control.intercept_exceptions,
			      nested_vmcb->control.intercept,
			      nested_vmcb->control.msrpm_base_pa,
			      nested_vmcb->control.iopm_base_pa,
			      nested_vmcb->control.tsc_offset };

	/* Give the current vmcb to the guest */
	memcpy(nested_vmcb, svm->vmcb, sizeof(struct vmcb));
	nested_vmcb->save.cr0 = nested_save[0];
	if (!npt_enabled)
		nested_vmcb->save.cr3 = nested_save[1];
	nested_vmcb->save.cr4 = nested_save[2];
	nested_vmcb->save.efer = nested_save[3];
	nested_vmcb->control.intercept_cr_read = nested_save[4];
	nested_vmcb->control.intercept_cr_write = nested_save[5];
	nested_vmcb->control.intercept_dr_read = nested_save[6];
	nested_vmcb->control.intercept_dr_write = nested_save[7];
	nested_vmcb->control.intercept_exceptions = nested_save[8];
	nested_vmcb->control.intercept = nested_save[9];
	nested_vmcb->control.msrpm_base_pa = nested_save[10];
	nested_vmcb->control.iopm_base_pa = nested_save[11];
	nested_vmcb->control.tsc_offset = nested_save[12];

	/* We always set V_INTR_MASKING and remember the old value in hflags */
	if (!(svm->vcpu.arch.hflags & HF_VINTR_MASK))
		nested_vmcb->control.int_ctl &= ~V_INTR_MASKING_MASK;

	if ((nested_vmcb->control.int_ctl & V_IRQ_MASK) &&
	    (nested_vmcb->control.int_vector)) {
		nsvm_printk("WARNING: IRQ 0x%x still enabled on #VMEXIT\n",
				nested_vmcb->control.int_vector);
	}

	/* Restore the original control entries */
	svm->vmcb->control = hsave->control;

	/* Kill any pending exceptions */
	if (svm->vcpu.arch.exception.pending == true)
		nsvm_printk("WARNING: Pending Exception\n");
	svm->vcpu.arch.exception.pending = false;

	/* Restore selected save entries */
	svm->vmcb->save.es = hsave->save.es;
	svm->vmcb->save.cs = hsave->save.cs;
	svm->vmcb->save.ss = hsave->save.ss;
	svm->vmcb->save.ds = hsave->save.ds;
	svm->vmcb->save.gdtr = hsave->save.gdtr;
	svm->vmcb->save.idtr = hsave->save.idtr;
	svm->vmcb->save.rflags = hsave->save.rflags;
	svm_set_efer(&svm->vcpu, hsave->save.efer);
	svm_set_cr0(&svm->vcpu, hsave->save.cr0 | X86_CR0_PE);
	svm_set_cr4(&svm->vcpu, hsave->save.cr4);
	if (npt_enabled) {
		svm->vmcb->save.cr3 = hsave->save.cr3;
		svm->vcpu.arch.cr3 = hsave->save.cr3;
	} else {
		kvm_set_cr3(&svm->vcpu, hsave->save.cr3);
	}
	kvm_register_write(&svm->vcpu, VCPU_REGS_RAX, hsave->save.rax);
	kvm_register_write(&svm->vcpu, VCPU_REGS_RSP, hsave->save.rsp);
	kvm_register_write(&svm->vcpu, VCPU_REGS_RIP, hsave->save.rip);
	svm->vmcb->save.dr7 = 0;
	svm->vmcb->save.cpl = 0;
	svm->vmcb->control.exit_int_info = 0;

	svm->vcpu.arch.hflags &= ~HF_GIF_MASK;
	/* Exit nested SVM mode */
	svm->nested_vmcb = 0;

	return 0;
}

static int nested_svm_vmexit(struct vcpu_svm *svm)
{
	nsvm_printk("VMexit\n");
	if (nested_svm_do(svm, svm->nested_vmcb, 0,
			  NULL, nested_svm_vmexit_real))
		return 1;

	kvm_mmu_reset_context(&svm->vcpu);
	kvm_mmu_load(&svm->vcpu);

	return 0;
}

static int nested_svm_vmrun_msrpm(struct vcpu_svm *svm, void *arg1,
				  void *arg2, void *opaque)
{
	int i;
	u32 *nested_msrpm = (u32*)arg1;
	for (i=0; i< PAGE_SIZE * (1 << MSRPM_ALLOC_ORDER) / 4; i++)
		svm->nested_msrpm[i] = svm->msrpm[i] | nested_msrpm[i];
	svm->vmcb->control.msrpm_base_pa = __pa(svm->nested_msrpm);

	return 0;
}

static int nested_svm_vmrun(struct vcpu_svm *svm, void *arg1,
			    void *arg2, void *opaque)
{
	struct vmcb *nested_vmcb = (struct vmcb *)arg1;
	struct vmcb *hsave = svm->hsave;

	/* nested_vmcb is our indicator if nested SVM is activated */
	svm->nested_vmcb = svm->vmcb->save.rax;

	/* Clear internal status */
	svm->vcpu.arch.exception.pending = false;

	/* Save the old vmcb, so we don't need to pick what we save, but
	   can restore everything when a VMEXIT occurs */
	memcpy(hsave, svm->vmcb, sizeof(struct vmcb));
	/* We need to remember the original CR3 in the SPT case */
	if (!npt_enabled)
		hsave->save.cr3 = svm->vcpu.arch.cr3;
	hsave->save.cr4 = svm->vcpu.arch.cr4;
	hsave->save.rip = svm->next_rip;

	if (svm->vmcb->save.rflags & X86_EFLAGS_IF)
		svm->vcpu.arch.hflags |= HF_HIF_MASK;
	else
		svm->vcpu.arch.hflags &= ~HF_HIF_MASK;

	/* Load the nested guest state */
	svm->vmcb->save.es = nested_vmcb->save.es;
	svm->vmcb->save.cs = nested_vmcb->save.cs;
	svm->vmcb->save.ss = nested_vmcb->save.ss;
	svm->vmcb->save.ds = nested_vmcb->save.ds;
	svm->vmcb->save.gdtr = nested_vmcb->save.gdtr;
	svm->vmcb->save.idtr = nested_vmcb->save.idtr;
	svm->vmcb->save.rflags = nested_vmcb->save.rflags;
	svm_set_efer(&svm->vcpu, nested_vmcb->save.efer);
	svm_set_cr0(&svm->vcpu, nested_vmcb->save.cr0);
	svm_set_cr4(&svm->vcpu, nested_vmcb->save.cr4);
	if (npt_enabled) {
		svm->vmcb->save.cr3 = nested_vmcb->save.cr3;
		svm->vcpu.arch.cr3 = nested_vmcb->save.cr3;
	} else {
		kvm_set_cr3(&svm->vcpu, nested_vmcb->save.cr3);
		kvm_mmu_reset_context(&svm->vcpu);
	}
	svm->vmcb->save.cr2 = nested_vmcb->save.cr2;
	kvm_register_write(&svm->vcpu, VCPU_REGS_RAX, nested_vmcb->save.rax);
	kvm_register_write(&svm->vcpu, VCPU_REGS_RSP, nested_vmcb->save.rsp);
	kvm_register_write(&svm->vcpu, VCPU_REGS_RIP, nested_vmcb->save.rip);
	/* In case we don't even reach vcpu_run, the fields are not updated */
	svm->vmcb->save.rax = nested_vmcb->save.rax;
	svm->vmcb->save.rsp = nested_vmcb->save.rsp;
	svm->vmcb->save.rip = nested_vmcb->save.rip;
	svm->vmcb->save.dr7 = nested_vmcb->save.dr7;
	svm->vmcb->save.dr6 = nested_vmcb->save.dr6;
	svm->vmcb->save.cpl = nested_vmcb->save.cpl;

	/* We don't want a nested guest to be more powerful than the guest,
	   so all intercepts are ORed */
	svm->vmcb->control.intercept_cr_read |=
		nested_vmcb->control.intercept_cr_read;
	svm->vmcb->control.intercept_cr_write |=
		nested_vmcb->control.intercept_cr_write;
	svm->vmcb->control.intercept_dr_read |=
		nested_vmcb->control.intercept_dr_read;
	svm->vmcb->control.intercept_dr_write |=
		nested_vmcb->control.intercept_dr_write;
	svm->vmcb->control.intercept_exceptions |=
		nested_vmcb->control.intercept_exceptions;

	svm->vmcb->control.intercept |= nested_vmcb->control.intercept;

	svm->nested_vmcb_msrpm = nested_vmcb->control.msrpm_base_pa;

	force_new_asid(&svm->vcpu);
	svm->vmcb->control.exit_int_info = nested_vmcb->control.exit_int_info;
	svm->vmcb->control.exit_int_info_err = nested_vmcb->control.exit_int_info_err;
	svm->vmcb->control.int_ctl = nested_vmcb->control.int_ctl | V_INTR_MASKING_MASK;
	if (nested_vmcb->control.int_ctl & V_IRQ_MASK) {
		nsvm_printk("nSVM Injecting Interrupt: 0x%x\n",
				nested_vmcb->control.int_ctl);
	}
	if (nested_vmcb->control.int_ctl & V_INTR_MASKING_MASK)
		svm->vcpu.arch.hflags |= HF_VINTR_MASK;
	else
		svm->vcpu.arch.hflags &= ~HF_VINTR_MASK;

	nsvm_printk("nSVM exit_int_info: 0x%x | int_state: 0x%x\n",
			nested_vmcb->control.exit_int_info,
			nested_vmcb->control.int_state);

	svm->vmcb->control.int_vector = nested_vmcb->control.int_vector;
	svm->vmcb->control.int_state = nested_vmcb->control.int_state;
	svm->vmcb->control.tsc_offset += nested_vmcb->control.tsc_offset;
	if (nested_vmcb->control.event_inj & SVM_EVTINJ_VALID)
		nsvm_printk("Injecting Event: 0x%x\n",
				nested_vmcb->control.event_inj);
	svm->vmcb->control.event_inj = nested_vmcb->control.event_inj;
	svm->vmcb->control.event_inj_err = nested_vmcb->control.event_inj_err;

	svm->vcpu.arch.hflags |= HF_GIF_MASK;

	return 0;
}

static int nested_svm_vmloadsave(struct vmcb *from_vmcb, struct vmcb *to_vmcb)
{
	to_vmcb->save.fs = from_vmcb->save.fs;
	to_vmcb->save.gs = from_vmcb->save.gs;
	to_vmcb->save.tr = from_vmcb->save.tr;
	to_vmcb->save.ldtr = from_vmcb->save.ldtr;
	to_vmcb->save.kernel_gs_base = from_vmcb->save.kernel_gs_base;
	to_vmcb->save.star = from_vmcb->save.star;
	to_vmcb->save.lstar = from_vmcb->save.lstar;
	to_vmcb->save.cstar = from_vmcb->save.cstar;
	to_vmcb->save.sfmask = from_vmcb->save.sfmask;
	to_vmcb->save.sysenter_cs = from_vmcb->save.sysenter_cs;
	to_vmcb->save.sysenter_esp = from_vmcb->save.sysenter_esp;
	to_vmcb->save.sysenter_eip = from_vmcb->save.sysenter_eip;

	return 1;
}

static int nested_svm_vmload(struct vcpu_svm *svm, void *nested_vmcb,
			     void *arg2, void *opaque)
{
	return nested_svm_vmloadsave((struct vmcb *)nested_vmcb, svm->vmcb);
}

static int nested_svm_vmsave(struct vcpu_svm *svm, void *nested_vmcb,
			     void *arg2, void *opaque)
{
	return nested_svm_vmloadsave(svm->vmcb, (struct vmcb *)nested_vmcb);
}

static int vmload_interception(struct vcpu_svm *svm, struct kvm_run *kvm_run)
{
	if (nested_svm_check_permissions(svm))
		return 1;

	svm->next_rip = kvm_rip_read(&svm->vcpu) + 3;
	skip_emulated_instruction(&svm->vcpu);

	nested_svm_do(svm, svm->vmcb->save.rax, 0, NULL, nested_svm_vmload);

	return 1;
}

static int vmsave_interception(struct vcpu_svm *svm, struct kvm_run *kvm_run)
{
	if (nested_svm_check_permissions(svm))
		return 1;

	svm->next_rip = kvm_rip_read(&svm->vcpu) + 3;
	skip_emulated_instruction(&svm->vcpu);

	nested_svm_do(svm, svm->vmcb->save.rax, 0, NULL, nested_svm_vmsave);

	return 1;
}

static int vmrun_interception(struct vcpu_svm *svm, struct kvm_run *kvm_run)
{
	nsvm_printk("VMrun\n");
	if (nested_svm_check_permissions(svm))
		return 1;

	svm->next_rip = kvm_rip_read(&svm->vcpu) + 3;
	skip_emulated_instruction(&svm->vcpu);

	if (nested_svm_do(svm, svm->vmcb->save.rax, 0,
			  NULL, nested_svm_vmrun))
		return 1;

	if (nested_svm_do(svm, svm->nested_vmcb_msrpm, 0,
		      NULL, nested_svm_vmrun_msrpm))
		return 1;

	return 1;
}

static int stgi_interception(struct vcpu_svm *svm, struct kvm_run *kvm_run)
{
	if (nested_svm_check_permissions(svm))
		return 1;

	svm->next_rip = kvm_rip_read(&svm->vcpu) + 3;
	skip_emulated_instruction(&svm->vcpu);

	svm->vcpu.arch.hflags |= HF_GIF_MASK;

	return 1;
}

static int clgi_interception(struct vcpu_svm *svm, struct kvm_run *kvm_run)
{
	if (nested_svm_check_permissions(svm))
		return 1;

	svm->next_rip = kvm_rip_read(&svm->vcpu) + 3;
	skip_emulated_instruction(&svm->vcpu);

	svm->vcpu.arch.hflags &= ~HF_GIF_MASK;

	/* After a CLGI no interrupts should come */
	svm_clear_vintr(svm);
	svm->vmcb->control.int_ctl &= ~V_IRQ_MASK;

	return 1;
}

static int invalid_op_interception(struct vcpu_svm *svm,
				   struct kvm_run *kvm_run)
{
	kvm_queue_exception(&svm->vcpu, UD_VECTOR);
	return 1;
}

static int task_switch_interception(struct vcpu_svm *svm,
				    struct kvm_run *kvm_run)
{
	u16 tss_selector;

	tss_selector = (u16)svm->vmcb->control.exit_info_1;
	if (svm->vmcb->control.exit_info_2 &
	    (1ULL << SVM_EXITINFOSHIFT_TS_REASON_IRET))
		return kvm_task_switch(&svm->vcpu, tss_selector,
				       TASK_SWITCH_IRET);
	if (svm->vmcb->control.exit_info_2 &
	    (1ULL << SVM_EXITINFOSHIFT_TS_REASON_JMP))
		return kvm_task_switch(&svm->vcpu, tss_selector,
				       TASK_SWITCH_JMP);
	return kvm_task_switch(&svm->vcpu, tss_selector, TASK_SWITCH_CALL);
}

static int cpuid_interception(struct vcpu_svm *svm, struct kvm_run *kvm_run)
{
	svm->next_rip = kvm_rip_read(&svm->vcpu) + 2;
	kvm_emulate_cpuid(&svm->vcpu);
	return 1;
}

static int invlpg_interception(struct vcpu_svm *svm, struct kvm_run *kvm_run)
{
	if (emulate_instruction(&svm->vcpu, kvm_run, 0, 0, 0) != EMULATE_DONE)
		pr_unimpl(&svm->vcpu, "%s: failed\n", __func__);
	return 1;
}

static int emulate_on_interception(struct vcpu_svm *svm,
				   struct kvm_run *kvm_run)
{
	if (emulate_instruction(&svm->vcpu, NULL, 0, 0, 0) != EMULATE_DONE)
		pr_unimpl(&svm->vcpu, "%s: failed\n", __func__);
	return 1;
}

static int cr8_write_interception(struct vcpu_svm *svm, struct kvm_run *kvm_run)
{
	emulate_instruction(&svm->vcpu, NULL, 0, 0, 0);
	if (irqchip_in_kernel(svm->vcpu.kvm))
		return 1;
	kvm_run->exit_reason = KVM_EXIT_SET_TPR;
	return 0;
}

static int svm_get_msr(struct kvm_vcpu *vcpu, unsigned ecx, u64 *data)
{
	struct vcpu_svm *svm = to_svm(vcpu);

	switch (ecx) {
	case MSR_IA32_TIME_STAMP_COUNTER: {
		u64 tsc;

		rdtscll(tsc);
		*data = svm->vmcb->control.tsc_offset + tsc;
		break;
	}
	case MSR_K6_STAR:
		*data = svm->vmcb->save.star;
		break;
#ifdef CONFIG_X86_64
	case MSR_LSTAR:
		*data = svm->vmcb->save.lstar;
		break;
	case MSR_CSTAR:
		*data = svm->vmcb->save.cstar;
		break;
	case MSR_KERNEL_GS_BASE:
		*data = svm->vmcb->save.kernel_gs_base;
		break;
	case MSR_SYSCALL_MASK:
		*data = svm->vmcb->save.sfmask;
		break;
#endif
	case MSR_IA32_SYSENTER_CS:
		*data = svm->vmcb->save.sysenter_cs;
		break;
	case MSR_IA32_SYSENTER_EIP:
		*data = svm->vmcb->save.sysenter_eip;
		break;
	case MSR_IA32_SYSENTER_ESP:
		*data = svm->vmcb->save.sysenter_esp;
		break;
	/* Nobody will change the following 5 values in the VMCB so
	   we can safely return them on rdmsr. They will always be 0
	   until LBRV is implemented. */
	case MSR_IA32_DEBUGCTLMSR:
		*data = svm->vmcb->save.dbgctl;
		break;
	case MSR_IA32_LASTBRANCHFROMIP:
		*data = svm->vmcb->save.br_from;
		break;
	case MSR_IA32_LASTBRANCHTOIP:
		*data = svm->vmcb->save.br_to;
		break;
	case MSR_IA32_LASTINTFROMIP:
		*data = svm->vmcb->save.last_excp_from;
		break;
	case MSR_IA32_LASTINTTOIP:
		*data = svm->vmcb->save.last_excp_to;
		break;
	case MSR_VM_HSAVE_PA:
		*data = svm->hsave_msr;
		break;
	case MSR_VM_CR:
		*data = 0;
		break;
	case MSR_IA32_UCODE_REV:
		*data = 0x01000065;
		break;
	default:
		return kvm_get_msr_common(vcpu, ecx, data);
	}
	return 0;
}

static int rdmsr_interception(struct vcpu_svm *svm, struct kvm_run *kvm_run)
{
	u32 ecx = svm->vcpu.arch.regs[VCPU_REGS_RCX];
	u64 data;

	if (svm_get_msr(&svm->vcpu, ecx, &data))
		kvm_inject_gp(&svm->vcpu, 0);
	else {
		KVMTRACE_3D(MSR_READ, &svm->vcpu, ecx, (u32)data,
			    (u32)(data >> 32), handler);

		svm->vcpu.arch.regs[VCPU_REGS_RAX] = data & 0xffffffff;
		svm->vcpu.arch.regs[VCPU_REGS_RDX] = data >> 32;
		svm->next_rip = kvm_rip_read(&svm->vcpu) + 2;
		skip_emulated_instruction(&svm->vcpu);
	}
	return 1;
}

static int svm_set_msr(struct kvm_vcpu *vcpu, unsigned ecx, u64 data)
{
	struct vcpu_svm *svm = to_svm(vcpu);

	switch (ecx) {
	case MSR_IA32_TIME_STAMP_COUNTER: {
		u64 tsc;

		rdtscll(tsc);
		svm->vmcb->control.tsc_offset = data - tsc;
		break;
	}
	case MSR_K6_STAR:
		svm->vmcb->save.star = data;
		break;
#ifdef CONFIG_X86_64
	case MSR_LSTAR:
		svm->vmcb->save.lstar = data;
		break;
	case MSR_CSTAR:
		svm->vmcb->save.cstar = data;
		break;
	case MSR_KERNEL_GS_BASE:
		svm->vmcb->save.kernel_gs_base = data;
		break;
	case MSR_SYSCALL_MASK:
		svm->vmcb->save.sfmask = data;
		break;
#endif
	case MSR_IA32_SYSENTER_CS:
		svm->vmcb->save.sysenter_cs = data;
		break;
	case MSR_IA32_SYSENTER_EIP:
		svm->vmcb->save.sysenter_eip = data;
		break;
	case MSR_IA32_SYSENTER_ESP:
		svm->vmcb->save.sysenter_esp = data;
		break;
	case MSR_IA32_DEBUGCTLMSR:
		if (!svm_has(SVM_FEATURE_LBRV)) {
			pr_unimpl(vcpu, "%s: MSR_IA32_DEBUGCTL 0x%llx, nop\n",
					__func__, data);
			break;
		}
		if (data & DEBUGCTL_RESERVED_BITS)
			return 1;

		svm->vmcb->save.dbgctl = data;
		if (data & (1ULL<<0))
			svm_enable_lbrv(svm);
		else
			svm_disable_lbrv(svm);
		break;
	case MSR_K7_EVNTSEL0:
	case MSR_K7_EVNTSEL1:
	case MSR_K7_EVNTSEL2:
	case MSR_K7_EVNTSEL3:
	case MSR_K7_PERFCTR0:
	case MSR_K7_PERFCTR1:
	case MSR_K7_PERFCTR2:
	case MSR_K7_PERFCTR3:
		/*
		 * Just discard all writes to the performance counters; this
		 * should keep both older linux and windows 64-bit guests
		 * happy
		 */
		pr_unimpl(vcpu, "unimplemented perfctr wrmsr: 0x%x data 0x%llx\n", ecx, data);

		break;
	case MSR_VM_HSAVE_PA:
		svm->hsave_msr = data;
		break;
	default:
		return kvm_set_msr_common(vcpu, ecx, data);
	}
	return 0;
}

static int wrmsr_interception(struct vcpu_svm *svm, struct kvm_run *kvm_run)
{
	u32 ecx = svm->vcpu.arch.regs[VCPU_REGS_RCX];
	u64 data = (svm->vcpu.arch.regs[VCPU_REGS_RAX] & -1u)
		| ((u64)(svm->vcpu.arch.regs[VCPU_REGS_RDX] & -1u) << 32);

	KVMTRACE_3D(MSR_WRITE, &svm->vcpu, ecx, (u32)data, (u32)(data >> 32),
		    handler);

	svm->next_rip = kvm_rip_read(&svm->vcpu) + 2;
	if (svm_set_msr(&svm->vcpu, ecx, data))
		kvm_inject_gp(&svm->vcpu, 0);
	else
		skip_emulated_instruction(&svm->vcpu);
	return 1;
}

static int msr_interception(struct vcpu_svm *svm, struct kvm_run *kvm_run)
{
	if (svm->vmcb->control.exit_info_1)
		return wrmsr_interception(svm, kvm_run);
	else
		return rdmsr_interception(svm, kvm_run);
}

static int interrupt_window_interception(struct vcpu_svm *svm,
				   struct kvm_run *kvm_run)
{
	KVMTRACE_0D(PEND_INTR, &svm->vcpu, handler);

	svm_clear_vintr(svm);
	svm->vmcb->control.int_ctl &= ~V_IRQ_MASK;
	/*
	 * If the user space waits to inject interrupts, exit as soon as
	 * possible
	 */
	if (kvm_run->request_interrupt_window &&
	    !svm->vcpu.arch.irq_summary) {
		++svm->vcpu.stat.irq_window_exits;
		kvm_run->exit_reason = KVM_EXIT_IRQ_WINDOW_OPEN;
		return 0;
	}

	return 1;
}

static int (*svm_exit_handlers[])(struct vcpu_svm *svm,
				      struct kvm_run *kvm_run) = {
	[SVM_EXIT_READ_CR0]           		= emulate_on_interception,
	[SVM_EXIT_READ_CR3]           		= emulate_on_interception,
	[SVM_EXIT_READ_CR4]           		= emulate_on_interception,
	[SVM_EXIT_READ_CR8]           		= emulate_on_interception,
	/* for now: */
	[SVM_EXIT_WRITE_CR0]          		= emulate_on_interception,
	[SVM_EXIT_WRITE_CR3]          		= emulate_on_interception,
	[SVM_EXIT_WRITE_CR4]          		= emulate_on_interception,
	[SVM_EXIT_WRITE_CR8]          		= cr8_write_interception,
	[SVM_EXIT_READ_DR0] 			= emulate_on_interception,
	[SVM_EXIT_READ_DR1]			= emulate_on_interception,
	[SVM_EXIT_READ_DR2]			= emulate_on_interception,
	[SVM_EXIT_READ_DR3]			= emulate_on_interception,
	[SVM_EXIT_WRITE_DR0]			= emulate_on_interception,
	[SVM_EXIT_WRITE_DR1]			= emulate_on_interception,
	[SVM_EXIT_WRITE_DR2]			= emulate_on_interception,
	[SVM_EXIT_WRITE_DR3]			= emulate_on_interception,
	[SVM_EXIT_WRITE_DR5]			= emulate_on_interception,
	[SVM_EXIT_WRITE_DR7]			= emulate_on_interception,
	[SVM_EXIT_EXCP_BASE + DB_VECTOR]	= db_interception,
	[SVM_EXIT_EXCP_BASE + BP_VECTOR]	= bp_interception,
	[SVM_EXIT_EXCP_BASE + UD_VECTOR]	= ud_interception,
	[SVM_EXIT_EXCP_BASE + PF_VECTOR] 	= pf_interception,
	[SVM_EXIT_EXCP_BASE + NM_VECTOR] 	= nm_interception,
	[SVM_EXIT_EXCP_BASE + MC_VECTOR] 	= mc_interception,
	[SVM_EXIT_INTR] 			= intr_interception,
	[SVM_EXIT_NMI]				= nmi_interception,
	[SVM_EXIT_SMI]				= nop_on_interception,
	[SVM_EXIT_INIT]				= nop_on_interception,
	[SVM_EXIT_VINTR]			= interrupt_window_interception,
	/* [SVM_EXIT_CR0_SEL_WRITE]		= emulate_on_interception, */
	[SVM_EXIT_CPUID]			= cpuid_interception,
	[SVM_EXIT_INVD]                         = emulate_on_interception,
	[SVM_EXIT_HLT]				= halt_interception,
	[SVM_EXIT_INVLPG]			= invlpg_interception,
	[SVM_EXIT_INVLPGA]			= invalid_op_interception,
	[SVM_EXIT_IOIO] 		  	= io_interception,
	[SVM_EXIT_MSR]				= msr_interception,
	[SVM_EXIT_TASK_SWITCH]			= task_switch_interception,
	[SVM_EXIT_SHUTDOWN]			= shutdown_interception,
	[SVM_EXIT_VMRUN]			= vmrun_interception,
	[SVM_EXIT_VMMCALL]			= vmmcall_interception,
	[SVM_EXIT_VMLOAD]			= vmload_interception,
	[SVM_EXIT_VMSAVE]			= vmsave_interception,
	[SVM_EXIT_STGI]				= stgi_interception,
	[SVM_EXIT_CLGI]				= clgi_interception,
	[SVM_EXIT_SKINIT]			= invalid_op_interception,
	[SVM_EXIT_WBINVD]                       = emulate_on_interception,
	[SVM_EXIT_MONITOR]			= invalid_op_interception,
	[SVM_EXIT_MWAIT]			= invalid_op_interception,
	[SVM_EXIT_NPF]				= pf_interception,
};

static int handle_exit(struct kvm_run *kvm_run, struct kvm_vcpu *vcpu)
{
	struct vcpu_svm *svm = to_svm(vcpu);
	u32 exit_code = svm->vmcb->control.exit_code;

	KVMTRACE_3D(VMEXIT, vcpu, exit_code, (u32)svm->vmcb->save.rip,
		    (u32)((u64)svm->vmcb->save.rip >> 32), entryexit);

	if (is_nested(svm)) {
		nsvm_printk("nested handle_exit: 0x%x | 0x%lx | 0x%lx | 0x%lx\n",
			    exit_code, svm->vmcb->control.exit_info_1,
			    svm->vmcb->control.exit_info_2, svm->vmcb->save.rip);
		if (nested_svm_exit_handled(svm, true)) {
			nested_svm_vmexit(svm);
			nsvm_printk("-> #VMEXIT\n");
			return 1;
		}
	}

	if (npt_enabled) {
		int mmu_reload = 0;
		if ((vcpu->arch.cr0 ^ svm->vmcb->save.cr0) & X86_CR0_PG) {
			svm_set_cr0(vcpu, svm->vmcb->save.cr0);
			mmu_reload = 1;
		}
		vcpu->arch.cr0 = svm->vmcb->save.cr0;
		vcpu->arch.cr3 = svm->vmcb->save.cr3;
		if (is_paging(vcpu) && is_pae(vcpu) && !is_long_mode(vcpu)) {
			if (!load_pdptrs(vcpu, vcpu->arch.cr3)) {
				kvm_inject_gp(vcpu, 0);
				return 1;
			}
		}
		if (mmu_reload) {
			kvm_mmu_reset_context(vcpu);
			kvm_mmu_load(vcpu);
		}
	}

	kvm_reput_irq(svm);

	if (svm->vmcb->control.exit_code == SVM_EXIT_ERR) {
		kvm_run->exit_reason = KVM_EXIT_FAIL_ENTRY;
		kvm_run->fail_entry.hardware_entry_failure_reason
			= svm->vmcb->control.exit_code;
		return 0;
	}

	if (is_external_interrupt(svm->vmcb->control.exit_int_info) &&
	    exit_code != SVM_EXIT_EXCP_BASE + PF_VECTOR &&
	    exit_code != SVM_EXIT_NPF)
		printk(KERN_ERR "%s: unexpected exit_ini_info 0x%x "
		       "exit_code 0x%x\n",
		       __func__, svm->vmcb->control.exit_int_info,
		       exit_code);

	if (exit_code >= ARRAY_SIZE(svm_exit_handlers)
	    || !svm_exit_handlers[exit_code]) {
		kvm_run->exit_reason = KVM_EXIT_UNKNOWN;
		kvm_run->hw.hardware_exit_reason = exit_code;
		return 0;
	}

	return svm_exit_handlers[exit_code](svm, kvm_run);
}

static void reload_tss(struct kvm_vcpu *vcpu)
{
	int cpu = raw_smp_processor_id();

	struct svm_cpu_data *svm_data = per_cpu(svm_data, cpu);
	svm_data->tss_desc->type = 9; /* available 32/64-bit TSS */
	load_TR_desc();
}

static void pre_svm_run(struct vcpu_svm *svm)
{
	int cpu = raw_smp_processor_id();

	struct svm_cpu_data *svm_data = per_cpu(svm_data, cpu);

	svm->vmcb->control.tlb_ctl = TLB_CONTROL_DO_NOTHING;
	if (svm->vcpu.cpu != cpu ||
	    svm->asid_generation != svm_data->asid_generation)
		new_asid(svm, svm_data);
}


static inline void svm_inject_irq(struct vcpu_svm *svm, int irq)
{
	struct vmcb_control_area *control;

	KVMTRACE_1D(INJ_VIRQ, &svm->vcpu, (u32)irq, handler);

	++svm->vcpu.stat.irq_injections;
	control = &svm->vmcb->control;
	control->int_vector = irq;
	control->int_ctl &= ~V_INTR_PRIO_MASK;
	control->int_ctl |= V_IRQ_MASK |
		((/*control->int_vector >> 4*/ 0xf) << V_INTR_PRIO_SHIFT);
}

static void svm_set_irq(struct kvm_vcpu *vcpu, int irq)
{
	struct vcpu_svm *svm = to_svm(vcpu);

	nested_svm_intr(svm);

	svm_inject_irq(svm, irq);
}

static void update_cr8_intercept(struct kvm_vcpu *vcpu)
{
	struct vcpu_svm *svm = to_svm(vcpu);
	struct vmcb *vmcb = svm->vmcb;
	int max_irr, tpr;

	if (!irqchip_in_kernel(vcpu->kvm) || vcpu->arch.apic->vapic_addr)
		return;

	vmcb->control.intercept_cr_write &= ~INTERCEPT_CR8_MASK;

	max_irr = kvm_lapic_find_highest_irr(vcpu);
	if (max_irr == -1)
		return;

	tpr = kvm_lapic_get_cr8(vcpu) << 4;

	if (tpr >= (max_irr & 0xf0))
		vmcb->control.intercept_cr_write |= INTERCEPT_CR8_MASK;
}

static void svm_intr_assist(struct kvm_vcpu *vcpu)
{
	struct vcpu_svm *svm = to_svm(vcpu);
	struct vmcb *vmcb = svm->vmcb;
	int intr_vector = -1;

	if ((vmcb->control.exit_int_info & SVM_EVTINJ_VALID) &&
	    ((vmcb->control.exit_int_info & SVM_EVTINJ_TYPE_MASK) == 0)) {
		intr_vector = vmcb->control.exit_int_info &
			      SVM_EVTINJ_VEC_MASK;
		vmcb->control.exit_int_info = 0;
		svm_inject_irq(svm, intr_vector);
		goto out;
	}

	if (vmcb->control.int_ctl & V_IRQ_MASK)
		goto out;

	if (!kvm_cpu_has_interrupt(vcpu))
		goto out;

	if (nested_svm_intr(svm))
		goto out;

	if (!(svm->vcpu.arch.hflags & HF_GIF_MASK))
		goto out;

	if (!(vmcb->save.rflags & X86_EFLAGS_IF) ||
	    (vmcb->control.int_state & SVM_INTERRUPT_SHADOW_MASK) ||
	    (vmcb->control.event_inj & SVM_EVTINJ_VALID)) {
		/* unable to deliver irq, set pending irq */
		svm_set_vintr(svm);
		svm_inject_irq(svm, 0x0);
		goto out;
	}
	/* Okay, we can deliver the interrupt: grab it and update PIC state. */
	intr_vector = kvm_cpu_get_interrupt(vcpu);
	svm_inject_irq(svm, intr_vector);
out:
	update_cr8_intercept(vcpu);
}

static void kvm_reput_irq(struct vcpu_svm *svm)
{
	struct vmcb_control_area *control = &svm->vmcb->control;

	if ((control->int_ctl & V_IRQ_MASK)
	    && !irqchip_in_kernel(svm->vcpu.kvm)) {
		control->int_ctl &= ~V_IRQ_MASK;
		push_irq(&svm->vcpu, control->int_vector);
	}

	svm->vcpu.arch.interrupt_window_open =
		!(control->int_state & SVM_INTERRUPT_SHADOW_MASK) &&
		 (svm->vcpu.arch.hflags & HF_GIF_MASK);
}

static void svm_do_inject_vector(struct vcpu_svm *svm)
{
	struct kvm_vcpu *vcpu = &svm->vcpu;
	int word_index = __ffs(vcpu->arch.irq_summary);
	int bit_index = __ffs(vcpu->arch.irq_pending[word_index]);
	int irq = word_index * BITS_PER_LONG + bit_index;

	clear_bit(bit_index, &vcpu->arch.irq_pending[word_index]);
	if (!vcpu->arch.irq_pending[word_index])
		clear_bit(word_index, &vcpu->arch.irq_summary);
	svm_inject_irq(svm, irq);
}

static void do_interrupt_requests(struct kvm_vcpu *vcpu,
				       struct kvm_run *kvm_run)
{
	struct vcpu_svm *svm = to_svm(vcpu);
	struct vmcb_control_area *control = &svm->vmcb->control;

	if (nested_svm_intr(svm))
		return;

	svm->vcpu.arch.interrupt_window_open =
		(!(control->int_state & SVM_INTERRUPT_SHADOW_MASK) &&
		 (svm->vmcb->save.rflags & X86_EFLAGS_IF) &&
		 (svm->vcpu.arch.hflags & HF_GIF_MASK));

	if (svm->vcpu.arch.interrupt_window_open && svm->vcpu.arch.irq_summary)
		/*
		 * If interrupts enabled, and not blocked by sti or mov ss. Good.
		 */
		svm_do_inject_vector(svm);

	/*
	 * Interrupts blocked.  Wait for unblock.
	 */
	if (!svm->vcpu.arch.interrupt_window_open &&
	    (svm->vcpu.arch.irq_summary || kvm_run->request_interrupt_window))
		svm_set_vintr(svm);
	else
		svm_clear_vintr(svm);
}

static int svm_set_tss_addr(struct kvm *kvm, unsigned int addr)
{
	return 0;
}

static void svm_flush_tlb(struct kvm_vcpu *vcpu)
{
	force_new_asid(vcpu);
}

static void svm_prepare_guest_switch(struct kvm_vcpu *vcpu)
{
}

static inline void sync_cr8_to_lapic(struct kvm_vcpu *vcpu)
{
	struct vcpu_svm *svm = to_svm(vcpu);

	if (!(svm->vmcb->control.intercept_cr_write & INTERCEPT_CR8_MASK)) {
		int cr8 = svm->vmcb->control.int_ctl & V_TPR_MASK;
		kvm_lapic_set_tpr(vcpu, cr8);
	}
}

static inline void sync_lapic_to_cr8(struct kvm_vcpu *vcpu)
{
	struct vcpu_svm *svm = to_svm(vcpu);
	u64 cr8;

	if (!irqchip_in_kernel(vcpu->kvm))
		return;

	cr8 = kvm_get_cr8(vcpu);
	svm->vmcb->control.int_ctl &= ~V_TPR_MASK;
	svm->vmcb->control.int_ctl |= cr8 & V_TPR_MASK;
}

#ifdef CONFIG_X86_64
#define R "r"
#else
#define R "e"
#endif

static void svm_vcpu_run(struct kvm_vcpu *vcpu, struct kvm_run *kvm_run)
{
	struct vcpu_svm *svm = to_svm(vcpu);
	u16 fs_selector;
	u16 gs_selector;
	u16 ldt_selector;

	svm->vmcb->save.rax = vcpu->arch.regs[VCPU_REGS_RAX];
	svm->vmcb->save.rsp = vcpu->arch.regs[VCPU_REGS_RSP];
	svm->vmcb->save.rip = vcpu->arch.regs[VCPU_REGS_RIP];

	pre_svm_run(svm);

	sync_lapic_to_cr8(vcpu);

	save_host_msrs(vcpu);
	fs_selector = kvm_read_fs();
	gs_selector = kvm_read_gs();
	ldt_selector = kvm_read_ldt();
	svm->host_cr2 = kvm_read_cr2();
	if (!is_nested(svm))
		svm->vmcb->save.cr2 = vcpu->arch.cr2;
	/* required for live migration with NPT */
	if (npt_enabled)
		svm->vmcb->save.cr3 = vcpu->arch.cr3;

	clgi();

	local_irq_enable();

	asm volatile (
		"push %%"R"bp; \n\t"
		"mov %c[rbx](%[svm]), %%"R"bx \n\t"
		"mov %c[rcx](%[svm]), %%"R"cx \n\t"
		"mov %c[rdx](%[svm]), %%"R"dx \n\t"
		"mov %c[rsi](%[svm]), %%"R"si \n\t"
		"mov %c[rdi](%[svm]), %%"R"di \n\t"
		"mov %c[rbp](%[svm]), %%"R"bp \n\t"
#ifdef CONFIG_X86_64
		"mov %c[r8](%[svm]),  %%r8  \n\t"
		"mov %c[r9](%[svm]),  %%r9  \n\t"
		"mov %c[r10](%[svm]), %%r10 \n\t"
		"mov %c[r11](%[svm]), %%r11 \n\t"
		"mov %c[r12](%[svm]), %%r12 \n\t"
		"mov %c[r13](%[svm]), %%r13 \n\t"
		"mov %c[r14](%[svm]), %%r14 \n\t"
		"mov %c[r15](%[svm]), %%r15 \n\t"
#endif

		/* Enter guest mode */
		"push %%"R"ax \n\t"
		"mov %c[vmcb](%[svm]), %%"R"ax \n\t"
		__ex(SVM_VMLOAD) "\n\t"
		__ex(SVM_VMRUN) "\n\t"
		__ex(SVM_VMSAVE) "\n\t"
		"pop %%"R"ax \n\t"

		/* Save guest registers, load host registers */
		"mov %%"R"bx, %c[rbx](%[svm]) \n\t"
		"mov %%"R"cx, %c[rcx](%[svm]) \n\t"
		"mov %%"R"dx, %c[rdx](%[svm]) \n\t"
		"mov %%"R"si, %c[rsi](%[svm]) \n\t"
		"mov %%"R"di, %c[rdi](%[svm]) \n\t"
		"mov %%"R"bp, %c[rbp](%[svm]) \n\t"
#ifdef CONFIG_X86_64
		"mov %%r8,  %c[r8](%[svm]) \n\t"
		"mov %%r9,  %c[r9](%[svm]) \n\t"
		"mov %%r10, %c[r10](%[svm]) \n\t"
		"mov %%r11, %c[r11](%[svm]) \n\t"
		"mov %%r12, %c[r12](%[svm]) \n\t"
		"mov %%r13, %c[r13](%[svm]) \n\t"
		"mov %%r14, %c[r14](%[svm]) \n\t"
		"mov %%r15, %c[r15](%[svm]) \n\t"
#endif
		"pop %%"R"bp"
		:
		: [svm]"a"(svm),
		  [vmcb]"i"(offsetof(struct vcpu_svm, vmcb_pa)),
		  [rbx]"i"(offsetof(struct vcpu_svm, vcpu.arch.regs[VCPU_REGS_RBX])),
		  [rcx]"i"(offsetof(struct vcpu_svm, vcpu.arch.regs[VCPU_REGS_RCX])),
		  [rdx]"i"(offsetof(struct vcpu_svm, vcpu.arch.regs[VCPU_REGS_RDX])),
		  [rsi]"i"(offsetof(struct vcpu_svm, vcpu.arch.regs[VCPU_REGS_RSI])),
		  [rdi]"i"(offsetof(struct vcpu_svm, vcpu.arch.regs[VCPU_REGS_RDI])),
		  [rbp]"i"(offsetof(struct vcpu_svm, vcpu.arch.regs[VCPU_REGS_RBP]))
#ifdef CONFIG_X86_64
		  , [r8]"i"(offsetof(struct vcpu_svm, vcpu.arch.regs[VCPU_REGS_R8])),
		  [r9]"i"(offsetof(struct vcpu_svm, vcpu.arch.regs[VCPU_REGS_R9])),
		  [r10]"i"(offsetof(struct vcpu_svm, vcpu.arch.regs[VCPU_REGS_R10])),
		  [r11]"i"(offsetof(struct vcpu_svm, vcpu.arch.regs[VCPU_REGS_R11])),
		  [r12]"i"(offsetof(struct vcpu_svm, vcpu.arch.regs[VCPU_REGS_R12])),
		  [r13]"i"(offsetof(struct vcpu_svm, vcpu.arch.regs[VCPU_REGS_R13])),
		  [r14]"i"(offsetof(struct vcpu_svm, vcpu.arch.regs[VCPU_REGS_R14])),
		  [r15]"i"(offsetof(struct vcpu_svm, vcpu.arch.regs[VCPU_REGS_R15]))
#endif
		: "cc", "memory"
		, R"bx", R"cx", R"dx", R"si", R"di"
#ifdef CONFIG_X86_64
		, "r8", "r9", "r10", "r11" , "r12", "r13", "r14", "r15"
#endif
		);

	vcpu->arch.cr2 = svm->vmcb->save.cr2;
	vcpu->arch.regs[VCPU_REGS_RAX] = svm->vmcb->save.rax;
	vcpu->arch.regs[VCPU_REGS_RSP] = svm->vmcb->save.rsp;
	vcpu->arch.regs[VCPU_REGS_RIP] = svm->vmcb->save.rip;

	kvm_write_cr2(svm->host_cr2);

	kvm_load_fs(fs_selector);
	kvm_load_gs(gs_selector);
	kvm_load_ldt(ldt_selector);
	load_host_msrs(vcpu);

	reload_tss(vcpu);

	local_irq_disable();

	stgi();

	sync_cr8_to_lapic(vcpu);

	svm->next_rip = 0;
}

#undef R

static void svm_set_cr3(struct kvm_vcpu *vcpu, unsigned long root)
{
	struct vcpu_svm *svm = to_svm(vcpu);

	if (npt_enabled) {
		svm->vmcb->control.nested_cr3 = root;
		force_new_asid(vcpu);
		return;
	}

	svm->vmcb->save.cr3 = root;
	force_new_asid(vcpu);

	if (vcpu->fpu_active) {
		svm->vmcb->control.intercept_exceptions |= (1 << NM_VECTOR);
		svm->vmcb->save.cr0 |= X86_CR0_TS;
		vcpu->fpu_active = 0;
	}
}

static int is_disabled(void)
{
	u64 vm_cr;

	rdmsrl(MSR_VM_CR, vm_cr);
	if (vm_cr & (1 << SVM_VM_CR_SVM_DISABLE))
		return 1;

	return 0;
}

static void
svm_patch_hypercall(struct kvm_vcpu *vcpu, unsigned char *hypercall)
{
	/*
	 * Patch in the VMMCALL instruction:
	 */
	hypercall[0] = 0x0f;
	hypercall[1] = 0x01;
	hypercall[2] = 0xd9;
}

static void svm_check_processor_compat(void *rtn)
{
	*(int *)rtn = 0;
}

static bool svm_cpu_has_accelerated_tpr(void)
{
	return false;
}

static int get_npt_level(void)
{
#ifdef CONFIG_X86_64
	return PT64_ROOT_LEVEL;
#else
	return PT32E_ROOT_LEVEL;
#endif
}

static int svm_get_mt_mask_shift(void)
{
	return 0;
}

static struct kvm_x86_ops svm_x86_ops = {
	.cpu_has_kvm_support = has_svm,
	.disabled_by_bios = is_disabled,
	.hardware_setup = svm_hardware_setup,
	.hardware_unsetup = svm_hardware_unsetup,
	.check_processor_compatibility = svm_check_processor_compat,
	.hardware_enable = svm_hardware_enable,
	.hardware_disable = svm_hardware_disable,
	.cpu_has_accelerated_tpr = svm_cpu_has_accelerated_tpr,

	.vcpu_create = svm_create_vcpu,
	.vcpu_free = svm_free_vcpu,
	.vcpu_reset = svm_vcpu_reset,

	.prepare_guest_switch = svm_prepare_guest_switch,
	.vcpu_load = svm_vcpu_load,
	.vcpu_put = svm_vcpu_put,

	.set_guest_debug = svm_guest_debug,
	.get_msr = svm_get_msr,
	.set_msr = svm_set_msr,
	.get_segment_base = svm_get_segment_base,
	.get_segment = svm_get_segment,
	.set_segment = svm_set_segment,
	.get_cpl = svm_get_cpl,
	.get_cs_db_l_bits = kvm_get_cs_db_l_bits,
	.decache_cr4_guest_bits = svm_decache_cr4_guest_bits,
	.set_cr0 = svm_set_cr0,
	.set_cr3 = svm_set_cr3,
	.set_cr4 = svm_set_cr4,
	.set_efer = svm_set_efer,
	.get_idt = svm_get_idt,
	.set_idt = svm_set_idt,
	.get_gdt = svm_get_gdt,
	.set_gdt = svm_set_gdt,
	.get_dr = svm_get_dr,
	.set_dr = svm_set_dr,
	.get_rflags = svm_get_rflags,
	.set_rflags = svm_set_rflags,

	.tlb_flush = svm_flush_tlb,

	.run = svm_vcpu_run,
	.handle_exit = handle_exit,
	.skip_emulated_instruction = skip_emulated_instruction,
	.patch_hypercall = svm_patch_hypercall,
	.get_irq = svm_get_irq,
	.set_irq = svm_set_irq,
	.queue_exception = svm_queue_exception,
	.exception_injected = svm_exception_injected,
	.inject_pending_irq = svm_intr_assist,
	.inject_pending_vectors = do_interrupt_requests,

	.set_tss_addr = svm_set_tss_addr,
	.get_tdp_level = get_npt_level,
	.get_mt_mask_shift = svm_get_mt_mask_shift,
};

static int __init svm_init(void)
{
	return kvm_init(&svm_x86_ops, sizeof(struct vcpu_svm),
			      THIS_MODULE);
}

static void __exit svm_exit(void)
{
	kvm_exit();
}

module_init(svm_init)
module_exit(svm_exit)
