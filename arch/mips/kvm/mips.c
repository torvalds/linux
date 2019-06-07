/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * KVM/MIPS: MIPS specific KVM APIs
 *
 * Copyright (C) 2012  MIPS Technologies, Inc.  All rights reserved.
 * Authors: Sanjay Lal <sanjayl@kymasys.com>
 */

#include <linux/bitops.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/kdebug.h>
#include <linux/module.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>
#include <linux/sched/signal.h>
#include <linux/fs.h>
#include <linux/memblock.h>

#include <asm/fpu.h>
#include <asm/page.h>
#include <asm/cacheflush.h>
#include <asm/mmu_context.h>
#include <asm/pgalloc.h>
#include <asm/pgtable.h>

#include <linux/kvm_host.h>

#include "interrupt.h"
#include "commpage.h"

#define CREATE_TRACE_POINTS
#include "trace.h"

#ifndef VECTORSPACING
#define VECTORSPACING 0x100	/* for EI/VI mode */
#endif

#define VCPU_STAT(x) offsetof(struct kvm_vcpu, stat.x)
struct kvm_stats_debugfs_item debugfs_entries[] = {
	{ "wait",	  VCPU_STAT(wait_exits),	 KVM_STAT_VCPU },
	{ "cache",	  VCPU_STAT(cache_exits),	 KVM_STAT_VCPU },
	{ "signal",	  VCPU_STAT(signal_exits),	 KVM_STAT_VCPU },
	{ "interrupt",	  VCPU_STAT(int_exits),		 KVM_STAT_VCPU },
	{ "cop_unusable", VCPU_STAT(cop_unusable_exits), KVM_STAT_VCPU },
	{ "tlbmod",	  VCPU_STAT(tlbmod_exits),	 KVM_STAT_VCPU },
	{ "tlbmiss_ld",	  VCPU_STAT(tlbmiss_ld_exits),	 KVM_STAT_VCPU },
	{ "tlbmiss_st",	  VCPU_STAT(tlbmiss_st_exits),	 KVM_STAT_VCPU },
	{ "addrerr_st",	  VCPU_STAT(addrerr_st_exits),	 KVM_STAT_VCPU },
	{ "addrerr_ld",	  VCPU_STAT(addrerr_ld_exits),	 KVM_STAT_VCPU },
	{ "syscall",	  VCPU_STAT(syscall_exits),	 KVM_STAT_VCPU },
	{ "resvd_inst",	  VCPU_STAT(resvd_inst_exits),	 KVM_STAT_VCPU },
	{ "break_inst",	  VCPU_STAT(break_inst_exits),	 KVM_STAT_VCPU },
	{ "trap_inst",	  VCPU_STAT(trap_inst_exits),	 KVM_STAT_VCPU },
	{ "msa_fpe",	  VCPU_STAT(msa_fpe_exits),	 KVM_STAT_VCPU },
	{ "fpe",	  VCPU_STAT(fpe_exits),		 KVM_STAT_VCPU },
	{ "msa_disabled", VCPU_STAT(msa_disabled_exits), KVM_STAT_VCPU },
	{ "flush_dcache", VCPU_STAT(flush_dcache_exits), KVM_STAT_VCPU },
#ifdef CONFIG_KVM_MIPS_VZ
	{ "vz_gpsi",	  VCPU_STAT(vz_gpsi_exits),	 KVM_STAT_VCPU },
	{ "vz_gsfc",	  VCPU_STAT(vz_gsfc_exits),	 KVM_STAT_VCPU },
	{ "vz_hc",	  VCPU_STAT(vz_hc_exits),	 KVM_STAT_VCPU },
	{ "vz_grr",	  VCPU_STAT(vz_grr_exits),	 KVM_STAT_VCPU },
	{ "vz_gva",	  VCPU_STAT(vz_gva_exits),	 KVM_STAT_VCPU },
	{ "vz_ghfc",	  VCPU_STAT(vz_ghfc_exits),	 KVM_STAT_VCPU },
	{ "vz_gpa",	  VCPU_STAT(vz_gpa_exits),	 KVM_STAT_VCPU },
	{ "vz_resvd",	  VCPU_STAT(vz_resvd_exits),	 KVM_STAT_VCPU },
#endif
	{ "halt_successful_poll", VCPU_STAT(halt_successful_poll), KVM_STAT_VCPU },
	{ "halt_attempted_poll", VCPU_STAT(halt_attempted_poll), KVM_STAT_VCPU },
	{ "halt_poll_invalid", VCPU_STAT(halt_poll_invalid), KVM_STAT_VCPU },
	{ "halt_wakeup",  VCPU_STAT(halt_wakeup),	 KVM_STAT_VCPU },
	{NULL}
};

bool kvm_trace_guest_mode_change;

int kvm_guest_mode_change_trace_reg(void)
{
	kvm_trace_guest_mode_change = 1;
	return 0;
}

void kvm_guest_mode_change_trace_unreg(void)
{
	kvm_trace_guest_mode_change = 0;
}

/*
 * XXXKYMA: We are simulatoring a processor that has the WII bit set in
 * Config7, so we are "runnable" if interrupts are pending
 */
int kvm_arch_vcpu_runnable(struct kvm_vcpu *vcpu)
{
	return !!(vcpu->arch.pending_exceptions);
}

bool kvm_arch_vcpu_in_kernel(struct kvm_vcpu *vcpu)
{
	return false;
}

int kvm_arch_vcpu_should_kick(struct kvm_vcpu *vcpu)
{
	return 1;
}

int kvm_arch_hardware_enable(void)
{
	return kvm_mips_callbacks->hardware_enable();
}

void kvm_arch_hardware_disable(void)
{
	kvm_mips_callbacks->hardware_disable();
}

int kvm_arch_hardware_setup(void)
{
	return 0;
}

void kvm_arch_check_processor_compat(void *rtn)
{
	*(int *)rtn = 0;
}

int kvm_arch_init_vm(struct kvm *kvm, unsigned long type)
{
	switch (type) {
#ifdef CONFIG_KVM_MIPS_VZ
	case KVM_VM_MIPS_VZ:
#else
	case KVM_VM_MIPS_TE:
#endif
		break;
	default:
		/* Unsupported KVM type */
		return -EINVAL;
	};

	/* Allocate page table to map GPA -> RPA */
	kvm->arch.gpa_mm.pgd = kvm_pgd_alloc();
	if (!kvm->arch.gpa_mm.pgd)
		return -ENOMEM;

	return 0;
}

bool kvm_arch_has_vcpu_debugfs(void)
{
	return false;
}

int kvm_arch_create_vcpu_debugfs(struct kvm_vcpu *vcpu)
{
	return 0;
}

void kvm_mips_free_vcpus(struct kvm *kvm)
{
	unsigned int i;
	struct kvm_vcpu *vcpu;

	kvm_for_each_vcpu(i, vcpu, kvm) {
		kvm_arch_vcpu_free(vcpu);
	}

	mutex_lock(&kvm->lock);

	for (i = 0; i < atomic_read(&kvm->online_vcpus); i++)
		kvm->vcpus[i] = NULL;

	atomic_set(&kvm->online_vcpus, 0);

	mutex_unlock(&kvm->lock);
}

static void kvm_mips_free_gpa_pt(struct kvm *kvm)
{
	/* It should always be safe to remove after flushing the whole range */
	WARN_ON(!kvm_mips_flush_gpa_pt(kvm, 0, ~0));
	pgd_free(NULL, kvm->arch.gpa_mm.pgd);
}

void kvm_arch_destroy_vm(struct kvm *kvm)
{
	kvm_mips_free_vcpus(kvm);
	kvm_mips_free_gpa_pt(kvm);
}

long kvm_arch_dev_ioctl(struct file *filp, unsigned int ioctl,
			unsigned long arg)
{
	return -ENOIOCTLCMD;
}

int kvm_arch_create_memslot(struct kvm *kvm, struct kvm_memory_slot *slot,
			    unsigned long npages)
{
	return 0;
}

void kvm_arch_flush_shadow_all(struct kvm *kvm)
{
	/* Flush whole GPA */
	kvm_mips_flush_gpa_pt(kvm, 0, ~0);

	/* Let implementation do the rest */
	kvm_mips_callbacks->flush_shadow_all(kvm);
}

void kvm_arch_flush_shadow_memslot(struct kvm *kvm,
				   struct kvm_memory_slot *slot)
{
	/*
	 * The slot has been made invalid (ready for moving or deletion), so we
	 * need to ensure that it can no longer be accessed by any guest VCPUs.
	 */

	spin_lock(&kvm->mmu_lock);
	/* Flush slot from GPA */
	kvm_mips_flush_gpa_pt(kvm, slot->base_gfn,
			      slot->base_gfn + slot->npages - 1);
	/* Let implementation do the rest */
	kvm_mips_callbacks->flush_shadow_memslot(kvm, slot);
	spin_unlock(&kvm->mmu_lock);
}

int kvm_arch_prepare_memory_region(struct kvm *kvm,
				   struct kvm_memory_slot *memslot,
				   const struct kvm_userspace_memory_region *mem,
				   enum kvm_mr_change change)
{
	return 0;
}

void kvm_arch_commit_memory_region(struct kvm *kvm,
				   const struct kvm_userspace_memory_region *mem,
				   const struct kvm_memory_slot *old,
				   const struct kvm_memory_slot *new,
				   enum kvm_mr_change change)
{
	int needs_flush;

	kvm_debug("%s: kvm: %p slot: %d, GPA: %llx, size: %llx, QVA: %llx\n",
		  __func__, kvm, mem->slot, mem->guest_phys_addr,
		  mem->memory_size, mem->userspace_addr);

	/*
	 * If dirty page logging is enabled, write protect all pages in the slot
	 * ready for dirty logging.
	 *
	 * There is no need to do this in any of the following cases:
	 * CREATE:	No dirty mappings will already exist.
	 * MOVE/DELETE:	The old mappings will already have been cleaned up by
	 *		kvm_arch_flush_shadow_memslot()
	 */
	if (change == KVM_MR_FLAGS_ONLY &&
	    (!(old->flags & KVM_MEM_LOG_DIRTY_PAGES) &&
	     new->flags & KVM_MEM_LOG_DIRTY_PAGES)) {
		spin_lock(&kvm->mmu_lock);
		/* Write protect GPA page table entries */
		needs_flush = kvm_mips_mkclean_gpa_pt(kvm, new->base_gfn,
					new->base_gfn + new->npages - 1);
		/* Let implementation do the rest */
		if (needs_flush)
			kvm_mips_callbacks->flush_shadow_memslot(kvm, new);
		spin_unlock(&kvm->mmu_lock);
	}
}

static inline void dump_handler(const char *symbol, void *start, void *end)
{
	u32 *p;

	pr_debug("LEAF(%s)\n", symbol);

	pr_debug("\t.set push\n");
	pr_debug("\t.set noreorder\n");

	for (p = start; p < (u32 *)end; ++p)
		pr_debug("\t.word\t0x%08x\t\t# %p\n", *p, p);

	pr_debug("\t.set\tpop\n");

	pr_debug("\tEND(%s)\n", symbol);
}

struct kvm_vcpu *kvm_arch_vcpu_create(struct kvm *kvm, unsigned int id)
{
	int err, size;
	void *gebase, *p, *handler, *refill_start, *refill_end;
	int i;

	struct kvm_vcpu *vcpu = kzalloc(sizeof(struct kvm_vcpu), GFP_KERNEL);

	if (!vcpu) {
		err = -ENOMEM;
		goto out;
	}

	err = kvm_vcpu_init(vcpu, kvm, id);

	if (err)
		goto out_free_cpu;

	kvm_debug("kvm @ %p: create cpu %d at %p\n", kvm, id, vcpu);

	/*
	 * Allocate space for host mode exception handlers that handle
	 * guest mode exits
	 */
	if (cpu_has_veic || cpu_has_vint)
		size = 0x200 + VECTORSPACING * 64;
	else
		size = 0x4000;

	gebase = kzalloc(ALIGN(size, PAGE_SIZE), GFP_KERNEL);

	if (!gebase) {
		err = -ENOMEM;
		goto out_uninit_cpu;
	}
	kvm_debug("Allocated %d bytes for KVM Exception Handlers @ %p\n",
		  ALIGN(size, PAGE_SIZE), gebase);

	/*
	 * Check new ebase actually fits in CP0_EBase. The lack of a write gate
	 * limits us to the low 512MB of physical address space. If the memory
	 * we allocate is out of range, just give up now.
	 */
	if (!cpu_has_ebase_wg && virt_to_phys(gebase) >= 0x20000000) {
		kvm_err("CP0_EBase.WG required for guest exception base %pK\n",
			gebase);
		err = -ENOMEM;
		goto out_free_gebase;
	}

	/* Save new ebase */
	vcpu->arch.guest_ebase = gebase;

	/* Build guest exception vectors dynamically in unmapped memory */
	handler = gebase + 0x2000;

	/* TLB refill (or XTLB refill on 64-bit VZ where KX=1) */
	refill_start = gebase;
	if (IS_ENABLED(CONFIG_KVM_MIPS_VZ) && IS_ENABLED(CONFIG_64BIT))
		refill_start += 0x080;
	refill_end = kvm_mips_build_tlb_refill_exception(refill_start, handler);

	/* General Exception Entry point */
	kvm_mips_build_exception(gebase + 0x180, handler);

	/* For vectored interrupts poke the exception code @ all offsets 0-7 */
	for (i = 0; i < 8; i++) {
		kvm_debug("L1 Vectored handler @ %p\n",
			  gebase + 0x200 + (i * VECTORSPACING));
		kvm_mips_build_exception(gebase + 0x200 + i * VECTORSPACING,
					 handler);
	}

	/* General exit handler */
	p = handler;
	p = kvm_mips_build_exit(p);

	/* Guest entry routine */
	vcpu->arch.vcpu_run = p;
	p = kvm_mips_build_vcpu_run(p);

	/* Dump the generated code */
	pr_debug("#include <asm/asm.h>\n");
	pr_debug("#include <asm/regdef.h>\n");
	pr_debug("\n");
	dump_handler("kvm_vcpu_run", vcpu->arch.vcpu_run, p);
	dump_handler("kvm_tlb_refill", refill_start, refill_end);
	dump_handler("kvm_gen_exc", gebase + 0x180, gebase + 0x200);
	dump_handler("kvm_exit", gebase + 0x2000, vcpu->arch.vcpu_run);

	/* Invalidate the icache for these ranges */
	flush_icache_range((unsigned long)gebase,
			   (unsigned long)gebase + ALIGN(size, PAGE_SIZE));

	/*
	 * Allocate comm page for guest kernel, a TLB will be reserved for
	 * mapping GVA @ 0xFFFF8000 to this page
	 */
	vcpu->arch.kseg0_commpage = kzalloc(PAGE_SIZE << 1, GFP_KERNEL);

	if (!vcpu->arch.kseg0_commpage) {
		err = -ENOMEM;
		goto out_free_gebase;
	}

	kvm_debug("Allocated COMM page @ %p\n", vcpu->arch.kseg0_commpage);
	kvm_mips_commpage_init(vcpu);

	/* Init */
	vcpu->arch.last_sched_cpu = -1;
	vcpu->arch.last_exec_cpu = -1;

	return vcpu;

out_free_gebase:
	kfree(gebase);

out_uninit_cpu:
	kvm_vcpu_uninit(vcpu);

out_free_cpu:
	kfree(vcpu);

out:
	return ERR_PTR(err);
}

void kvm_arch_vcpu_free(struct kvm_vcpu *vcpu)
{
	hrtimer_cancel(&vcpu->arch.comparecount_timer);

	kvm_vcpu_uninit(vcpu);

	kvm_mips_dump_stats(vcpu);

	kvm_mmu_free_memory_caches(vcpu);
	kfree(vcpu->arch.guest_ebase);
	kfree(vcpu->arch.kseg0_commpage);
	kfree(vcpu);
}

void kvm_arch_vcpu_destroy(struct kvm_vcpu *vcpu)
{
	kvm_arch_vcpu_free(vcpu);
}

int kvm_arch_vcpu_ioctl_set_guest_debug(struct kvm_vcpu *vcpu,
					struct kvm_guest_debug *dbg)
{
	return -ENOIOCTLCMD;
}

int kvm_arch_vcpu_ioctl_run(struct kvm_vcpu *vcpu, struct kvm_run *run)
{
	int r = -EINTR;

	vcpu_load(vcpu);

	kvm_sigset_activate(vcpu);

	if (vcpu->mmio_needed) {
		if (!vcpu->mmio_is_write)
			kvm_mips_complete_mmio_load(vcpu, run);
		vcpu->mmio_needed = 0;
	}

	if (run->immediate_exit)
		goto out;

	lose_fpu(1);

	local_irq_disable();
	guest_enter_irqoff();
	trace_kvm_enter(vcpu);

	/*
	 * Make sure the read of VCPU requests in vcpu_run() callback is not
	 * reordered ahead of the write to vcpu->mode, or we could miss a TLB
	 * flush request while the requester sees the VCPU as outside of guest
	 * mode and not needing an IPI.
	 */
	smp_store_mb(vcpu->mode, IN_GUEST_MODE);

	r = kvm_mips_callbacks->vcpu_run(run, vcpu);

	trace_kvm_out(vcpu);
	guest_exit_irqoff();
	local_irq_enable();

out:
	kvm_sigset_deactivate(vcpu);

	vcpu_put(vcpu);
	return r;
}

int kvm_vcpu_ioctl_interrupt(struct kvm_vcpu *vcpu,
			     struct kvm_mips_interrupt *irq)
{
	int intr = (int)irq->irq;
	struct kvm_vcpu *dvcpu = NULL;

	if (intr == 3 || intr == -3 || intr == 4 || intr == -4)
		kvm_debug("%s: CPU: %d, INTR: %d\n", __func__, irq->cpu,
			  (int)intr);

	if (irq->cpu == -1)
		dvcpu = vcpu;
	else
		dvcpu = vcpu->kvm->vcpus[irq->cpu];

	if (intr == 2 || intr == 3 || intr == 4) {
		kvm_mips_callbacks->queue_io_int(dvcpu, irq);

	} else if (intr == -2 || intr == -3 || intr == -4) {
		kvm_mips_callbacks->dequeue_io_int(dvcpu, irq);
	} else {
		kvm_err("%s: invalid interrupt ioctl (%d:%d)\n", __func__,
			irq->cpu, irq->irq);
		return -EINVAL;
	}

	dvcpu->arch.wait = 0;

	if (swq_has_sleeper(&dvcpu->wq))
		swake_up_one(&dvcpu->wq);

	return 0;
}

int kvm_arch_vcpu_ioctl_get_mpstate(struct kvm_vcpu *vcpu,
				    struct kvm_mp_state *mp_state)
{
	return -ENOIOCTLCMD;
}

int kvm_arch_vcpu_ioctl_set_mpstate(struct kvm_vcpu *vcpu,
				    struct kvm_mp_state *mp_state)
{
	return -ENOIOCTLCMD;
}

static u64 kvm_mips_get_one_regs[] = {
	KVM_REG_MIPS_R0,
	KVM_REG_MIPS_R1,
	KVM_REG_MIPS_R2,
	KVM_REG_MIPS_R3,
	KVM_REG_MIPS_R4,
	KVM_REG_MIPS_R5,
	KVM_REG_MIPS_R6,
	KVM_REG_MIPS_R7,
	KVM_REG_MIPS_R8,
	KVM_REG_MIPS_R9,
	KVM_REG_MIPS_R10,
	KVM_REG_MIPS_R11,
	KVM_REG_MIPS_R12,
	KVM_REG_MIPS_R13,
	KVM_REG_MIPS_R14,
	KVM_REG_MIPS_R15,
	KVM_REG_MIPS_R16,
	KVM_REG_MIPS_R17,
	KVM_REG_MIPS_R18,
	KVM_REG_MIPS_R19,
	KVM_REG_MIPS_R20,
	KVM_REG_MIPS_R21,
	KVM_REG_MIPS_R22,
	KVM_REG_MIPS_R23,
	KVM_REG_MIPS_R24,
	KVM_REG_MIPS_R25,
	KVM_REG_MIPS_R26,
	KVM_REG_MIPS_R27,
	KVM_REG_MIPS_R28,
	KVM_REG_MIPS_R29,
	KVM_REG_MIPS_R30,
	KVM_REG_MIPS_R31,

#ifndef CONFIG_CPU_MIPSR6
	KVM_REG_MIPS_HI,
	KVM_REG_MIPS_LO,
#endif
	KVM_REG_MIPS_PC,
};

static u64 kvm_mips_get_one_regs_fpu[] = {
	KVM_REG_MIPS_FCR_IR,
	KVM_REG_MIPS_FCR_CSR,
};

static u64 kvm_mips_get_one_regs_msa[] = {
	KVM_REG_MIPS_MSA_IR,
	KVM_REG_MIPS_MSA_CSR,
};

static unsigned long kvm_mips_num_regs(struct kvm_vcpu *vcpu)
{
	unsigned long ret;

	ret = ARRAY_SIZE(kvm_mips_get_one_regs);
	if (kvm_mips_guest_can_have_fpu(&vcpu->arch)) {
		ret += ARRAY_SIZE(kvm_mips_get_one_regs_fpu) + 48;
		/* odd doubles */
		if (boot_cpu_data.fpu_id & MIPS_FPIR_F64)
			ret += 16;
	}
	if (kvm_mips_guest_can_have_msa(&vcpu->arch))
		ret += ARRAY_SIZE(kvm_mips_get_one_regs_msa) + 32;
	ret += kvm_mips_callbacks->num_regs(vcpu);

	return ret;
}

static int kvm_mips_copy_reg_indices(struct kvm_vcpu *vcpu, u64 __user *indices)
{
	u64 index;
	unsigned int i;

	if (copy_to_user(indices, kvm_mips_get_one_regs,
			 sizeof(kvm_mips_get_one_regs)))
		return -EFAULT;
	indices += ARRAY_SIZE(kvm_mips_get_one_regs);

	if (kvm_mips_guest_can_have_fpu(&vcpu->arch)) {
		if (copy_to_user(indices, kvm_mips_get_one_regs_fpu,
				 sizeof(kvm_mips_get_one_regs_fpu)))
			return -EFAULT;
		indices += ARRAY_SIZE(kvm_mips_get_one_regs_fpu);

		for (i = 0; i < 32; ++i) {
			index = KVM_REG_MIPS_FPR_32(i);
			if (copy_to_user(indices, &index, sizeof(index)))
				return -EFAULT;
			++indices;

			/* skip odd doubles if no F64 */
			if (i & 1 && !(boot_cpu_data.fpu_id & MIPS_FPIR_F64))
				continue;

			index = KVM_REG_MIPS_FPR_64(i);
			if (copy_to_user(indices, &index, sizeof(index)))
				return -EFAULT;
			++indices;
		}
	}

	if (kvm_mips_guest_can_have_msa(&vcpu->arch)) {
		if (copy_to_user(indices, kvm_mips_get_one_regs_msa,
				 sizeof(kvm_mips_get_one_regs_msa)))
			return -EFAULT;
		indices += ARRAY_SIZE(kvm_mips_get_one_regs_msa);

		for (i = 0; i < 32; ++i) {
			index = KVM_REG_MIPS_VEC_128(i);
			if (copy_to_user(indices, &index, sizeof(index)))
				return -EFAULT;
			++indices;
		}
	}

	return kvm_mips_callbacks->copy_reg_indices(vcpu, indices);
}

static int kvm_mips_get_reg(struct kvm_vcpu *vcpu,
			    const struct kvm_one_reg *reg)
{
	struct mips_coproc *cop0 = vcpu->arch.cop0;
	struct mips_fpu_struct *fpu = &vcpu->arch.fpu;
	int ret;
	s64 v;
	s64 vs[2];
	unsigned int idx;

	switch (reg->id) {
	/* General purpose registers */
	case KVM_REG_MIPS_R0 ... KVM_REG_MIPS_R31:
		v = (long)vcpu->arch.gprs[reg->id - KVM_REG_MIPS_R0];
		break;
#ifndef CONFIG_CPU_MIPSR6
	case KVM_REG_MIPS_HI:
		v = (long)vcpu->arch.hi;
		break;
	case KVM_REG_MIPS_LO:
		v = (long)vcpu->arch.lo;
		break;
#endif
	case KVM_REG_MIPS_PC:
		v = (long)vcpu->arch.pc;
		break;

	/* Floating point registers */
	case KVM_REG_MIPS_FPR_32(0) ... KVM_REG_MIPS_FPR_32(31):
		if (!kvm_mips_guest_has_fpu(&vcpu->arch))
			return -EINVAL;
		idx = reg->id - KVM_REG_MIPS_FPR_32(0);
		/* Odd singles in top of even double when FR=0 */
		if (kvm_read_c0_guest_status(cop0) & ST0_FR)
			v = get_fpr32(&fpu->fpr[idx], 0);
		else
			v = get_fpr32(&fpu->fpr[idx & ~1], idx & 1);
		break;
	case KVM_REG_MIPS_FPR_64(0) ... KVM_REG_MIPS_FPR_64(31):
		if (!kvm_mips_guest_has_fpu(&vcpu->arch))
			return -EINVAL;
		idx = reg->id - KVM_REG_MIPS_FPR_64(0);
		/* Can't access odd doubles in FR=0 mode */
		if (idx & 1 && !(kvm_read_c0_guest_status(cop0) & ST0_FR))
			return -EINVAL;
		v = get_fpr64(&fpu->fpr[idx], 0);
		break;
	case KVM_REG_MIPS_FCR_IR:
		if (!kvm_mips_guest_has_fpu(&vcpu->arch))
			return -EINVAL;
		v = boot_cpu_data.fpu_id;
		break;
	case KVM_REG_MIPS_FCR_CSR:
		if (!kvm_mips_guest_has_fpu(&vcpu->arch))
			return -EINVAL;
		v = fpu->fcr31;
		break;

	/* MIPS SIMD Architecture (MSA) registers */
	case KVM_REG_MIPS_VEC_128(0) ... KVM_REG_MIPS_VEC_128(31):
		if (!kvm_mips_guest_has_msa(&vcpu->arch))
			return -EINVAL;
		/* Can't access MSA registers in FR=0 mode */
		if (!(kvm_read_c0_guest_status(cop0) & ST0_FR))
			return -EINVAL;
		idx = reg->id - KVM_REG_MIPS_VEC_128(0);
#ifdef CONFIG_CPU_LITTLE_ENDIAN
		/* least significant byte first */
		vs[0] = get_fpr64(&fpu->fpr[idx], 0);
		vs[1] = get_fpr64(&fpu->fpr[idx], 1);
#else
		/* most significant byte first */
		vs[0] = get_fpr64(&fpu->fpr[idx], 1);
		vs[1] = get_fpr64(&fpu->fpr[idx], 0);
#endif
		break;
	case KVM_REG_MIPS_MSA_IR:
		if (!kvm_mips_guest_has_msa(&vcpu->arch))
			return -EINVAL;
		v = boot_cpu_data.msa_id;
		break;
	case KVM_REG_MIPS_MSA_CSR:
		if (!kvm_mips_guest_has_msa(&vcpu->arch))
			return -EINVAL;
		v = fpu->msacsr;
		break;

	/* registers to be handled specially */
	default:
		ret = kvm_mips_callbacks->get_one_reg(vcpu, reg, &v);
		if (ret)
			return ret;
		break;
	}
	if ((reg->id & KVM_REG_SIZE_MASK) == KVM_REG_SIZE_U64) {
		u64 __user *uaddr64 = (u64 __user *)(long)reg->addr;

		return put_user(v, uaddr64);
	} else if ((reg->id & KVM_REG_SIZE_MASK) == KVM_REG_SIZE_U32) {
		u32 __user *uaddr32 = (u32 __user *)(long)reg->addr;
		u32 v32 = (u32)v;

		return put_user(v32, uaddr32);
	} else if ((reg->id & KVM_REG_SIZE_MASK) == KVM_REG_SIZE_U128) {
		void __user *uaddr = (void __user *)(long)reg->addr;

		return copy_to_user(uaddr, vs, 16) ? -EFAULT : 0;
	} else {
		return -EINVAL;
	}
}

static int kvm_mips_set_reg(struct kvm_vcpu *vcpu,
			    const struct kvm_one_reg *reg)
{
	struct mips_coproc *cop0 = vcpu->arch.cop0;
	struct mips_fpu_struct *fpu = &vcpu->arch.fpu;
	s64 v;
	s64 vs[2];
	unsigned int idx;

	if ((reg->id & KVM_REG_SIZE_MASK) == KVM_REG_SIZE_U64) {
		u64 __user *uaddr64 = (u64 __user *)(long)reg->addr;

		if (get_user(v, uaddr64) != 0)
			return -EFAULT;
	} else if ((reg->id & KVM_REG_SIZE_MASK) == KVM_REG_SIZE_U32) {
		u32 __user *uaddr32 = (u32 __user *)(long)reg->addr;
		s32 v32;

		if (get_user(v32, uaddr32) != 0)
			return -EFAULT;
		v = (s64)v32;
	} else if ((reg->id & KVM_REG_SIZE_MASK) == KVM_REG_SIZE_U128) {
		void __user *uaddr = (void __user *)(long)reg->addr;

		return copy_from_user(vs, uaddr, 16) ? -EFAULT : 0;
	} else {
		return -EINVAL;
	}

	switch (reg->id) {
	/* General purpose registers */
	case KVM_REG_MIPS_R0:
		/* Silently ignore requests to set $0 */
		break;
	case KVM_REG_MIPS_R1 ... KVM_REG_MIPS_R31:
		vcpu->arch.gprs[reg->id - KVM_REG_MIPS_R0] = v;
		break;
#ifndef CONFIG_CPU_MIPSR6
	case KVM_REG_MIPS_HI:
		vcpu->arch.hi = v;
		break;
	case KVM_REG_MIPS_LO:
		vcpu->arch.lo = v;
		break;
#endif
	case KVM_REG_MIPS_PC:
		vcpu->arch.pc = v;
		break;

	/* Floating point registers */
	case KVM_REG_MIPS_FPR_32(0) ... KVM_REG_MIPS_FPR_32(31):
		if (!kvm_mips_guest_has_fpu(&vcpu->arch))
			return -EINVAL;
		idx = reg->id - KVM_REG_MIPS_FPR_32(0);
		/* Odd singles in top of even double when FR=0 */
		if (kvm_read_c0_guest_status(cop0) & ST0_FR)
			set_fpr32(&fpu->fpr[idx], 0, v);
		else
			set_fpr32(&fpu->fpr[idx & ~1], idx & 1, v);
		break;
	case KVM_REG_MIPS_FPR_64(0) ... KVM_REG_MIPS_FPR_64(31):
		if (!kvm_mips_guest_has_fpu(&vcpu->arch))
			return -EINVAL;
		idx = reg->id - KVM_REG_MIPS_FPR_64(0);
		/* Can't access odd doubles in FR=0 mode */
		if (idx & 1 && !(kvm_read_c0_guest_status(cop0) & ST0_FR))
			return -EINVAL;
		set_fpr64(&fpu->fpr[idx], 0, v);
		break;
	case KVM_REG_MIPS_FCR_IR:
		if (!kvm_mips_guest_has_fpu(&vcpu->arch))
			return -EINVAL;
		/* Read-only */
		break;
	case KVM_REG_MIPS_FCR_CSR:
		if (!kvm_mips_guest_has_fpu(&vcpu->arch))
			return -EINVAL;
		fpu->fcr31 = v;
		break;

	/* MIPS SIMD Architecture (MSA) registers */
	case KVM_REG_MIPS_VEC_128(0) ... KVM_REG_MIPS_VEC_128(31):
		if (!kvm_mips_guest_has_msa(&vcpu->arch))
			return -EINVAL;
		idx = reg->id - KVM_REG_MIPS_VEC_128(0);
#ifdef CONFIG_CPU_LITTLE_ENDIAN
		/* least significant byte first */
		set_fpr64(&fpu->fpr[idx], 0, vs[0]);
		set_fpr64(&fpu->fpr[idx], 1, vs[1]);
#else
		/* most significant byte first */
		set_fpr64(&fpu->fpr[idx], 1, vs[0]);
		set_fpr64(&fpu->fpr[idx], 0, vs[1]);
#endif
		break;
	case KVM_REG_MIPS_MSA_IR:
		if (!kvm_mips_guest_has_msa(&vcpu->arch))
			return -EINVAL;
		/* Read-only */
		break;
	case KVM_REG_MIPS_MSA_CSR:
		if (!kvm_mips_guest_has_msa(&vcpu->arch))
			return -EINVAL;
		fpu->msacsr = v;
		break;

	/* registers to be handled specially */
	default:
		return kvm_mips_callbacks->set_one_reg(vcpu, reg, v);
	}
	return 0;
}

static int kvm_vcpu_ioctl_enable_cap(struct kvm_vcpu *vcpu,
				     struct kvm_enable_cap *cap)
{
	int r = 0;

	if (!kvm_vm_ioctl_check_extension(vcpu->kvm, cap->cap))
		return -EINVAL;
	if (cap->flags)
		return -EINVAL;
	if (cap->args[0])
		return -EINVAL;

	switch (cap->cap) {
	case KVM_CAP_MIPS_FPU:
		vcpu->arch.fpu_enabled = true;
		break;
	case KVM_CAP_MIPS_MSA:
		vcpu->arch.msa_enabled = true;
		break;
	default:
		r = -EINVAL;
		break;
	}

	return r;
}

long kvm_arch_vcpu_async_ioctl(struct file *filp, unsigned int ioctl,
			       unsigned long arg)
{
	struct kvm_vcpu *vcpu = filp->private_data;
	void __user *argp = (void __user *)arg;

	if (ioctl == KVM_INTERRUPT) {
		struct kvm_mips_interrupt irq;

		if (copy_from_user(&irq, argp, sizeof(irq)))
			return -EFAULT;
		kvm_debug("[%d] %s: irq: %d\n", vcpu->vcpu_id, __func__,
			  irq.irq);

		return kvm_vcpu_ioctl_interrupt(vcpu, &irq);
	}

	return -ENOIOCTLCMD;
}

long kvm_arch_vcpu_ioctl(struct file *filp, unsigned int ioctl,
			 unsigned long arg)
{
	struct kvm_vcpu *vcpu = filp->private_data;
	void __user *argp = (void __user *)arg;
	long r;

	vcpu_load(vcpu);

	switch (ioctl) {
	case KVM_SET_ONE_REG:
	case KVM_GET_ONE_REG: {
		struct kvm_one_reg reg;

		r = -EFAULT;
		if (copy_from_user(&reg, argp, sizeof(reg)))
			break;
		if (ioctl == KVM_SET_ONE_REG)
			r = kvm_mips_set_reg(vcpu, &reg);
		else
			r = kvm_mips_get_reg(vcpu, &reg);
		break;
	}
	case KVM_GET_REG_LIST: {
		struct kvm_reg_list __user *user_list = argp;
		struct kvm_reg_list reg_list;
		unsigned n;

		r = -EFAULT;
		if (copy_from_user(&reg_list, user_list, sizeof(reg_list)))
			break;
		n = reg_list.n;
		reg_list.n = kvm_mips_num_regs(vcpu);
		if (copy_to_user(user_list, &reg_list, sizeof(reg_list)))
			break;
		r = -E2BIG;
		if (n < reg_list.n)
			break;
		r = kvm_mips_copy_reg_indices(vcpu, user_list->reg);
		break;
	}
	case KVM_ENABLE_CAP: {
		struct kvm_enable_cap cap;

		r = -EFAULT;
		if (copy_from_user(&cap, argp, sizeof(cap)))
			break;
		r = kvm_vcpu_ioctl_enable_cap(vcpu, &cap);
		break;
	}
	default:
		r = -ENOIOCTLCMD;
	}

	vcpu_put(vcpu);
	return r;
}

/**
 * kvm_vm_ioctl_get_dirty_log - get and clear the log of dirty pages in a slot
 * @kvm: kvm instance
 * @log: slot id and address to which we copy the log
 *
 * Steps 1-4 below provide general overview of dirty page logging. See
 * kvm_get_dirty_log_protect() function description for additional details.
 *
 * We call kvm_get_dirty_log_protect() to handle steps 1-3, upon return we
 * always flush the TLB (step 4) even if previous step failed  and the dirty
 * bitmap may be corrupt. Regardless of previous outcome the KVM logging API
 * does not preclude user space subsequent dirty log read. Flushing TLB ensures
 * writes will be marked dirty for next log read.
 *
 *   1. Take a snapshot of the bit and clear it if needed.
 *   2. Write protect the corresponding page.
 *   3. Copy the snapshot to the userspace.
 *   4. Flush TLB's if needed.
 */
int kvm_vm_ioctl_get_dirty_log(struct kvm *kvm, struct kvm_dirty_log *log)
{
	struct kvm_memslots *slots;
	struct kvm_memory_slot *memslot;
	bool flush = false;
	int r;

	mutex_lock(&kvm->slots_lock);

	r = kvm_get_dirty_log_protect(kvm, log, &flush);

	if (flush) {
		slots = kvm_memslots(kvm);
		memslot = id_to_memslot(slots, log->slot);

		/* Let implementation handle TLB/GVA invalidation */
		kvm_mips_callbacks->flush_shadow_memslot(kvm, memslot);
	}

	mutex_unlock(&kvm->slots_lock);
	return r;
}

int kvm_vm_ioctl_clear_dirty_log(struct kvm *kvm, struct kvm_clear_dirty_log *log)
{
	struct kvm_memslots *slots;
	struct kvm_memory_slot *memslot;
	bool flush = false;
	int r;

	mutex_lock(&kvm->slots_lock);

	r = kvm_clear_dirty_log_protect(kvm, log, &flush);

	if (flush) {
		slots = kvm_memslots(kvm);
		memslot = id_to_memslot(slots, log->slot);

		/* Let implementation handle TLB/GVA invalidation */
		kvm_mips_callbacks->flush_shadow_memslot(kvm, memslot);
	}

	mutex_unlock(&kvm->slots_lock);
	return r;
}

long kvm_arch_vm_ioctl(struct file *filp, unsigned int ioctl, unsigned long arg)
{
	long r;

	switch (ioctl) {
	default:
		r = -ENOIOCTLCMD;
	}

	return r;
}

int kvm_arch_init(void *opaque)
{
	if (kvm_mips_callbacks) {
		kvm_err("kvm: module already exists\n");
		return -EEXIST;
	}

	return kvm_mips_emulation_init(&kvm_mips_callbacks);
}

void kvm_arch_exit(void)
{
	kvm_mips_callbacks = NULL;
}

int kvm_arch_vcpu_ioctl_get_sregs(struct kvm_vcpu *vcpu,
				  struct kvm_sregs *sregs)
{
	return -ENOIOCTLCMD;
}

int kvm_arch_vcpu_ioctl_set_sregs(struct kvm_vcpu *vcpu,
				  struct kvm_sregs *sregs)
{
	return -ENOIOCTLCMD;
}

void kvm_arch_vcpu_postcreate(struct kvm_vcpu *vcpu)
{
}

int kvm_arch_vcpu_ioctl_get_fpu(struct kvm_vcpu *vcpu, struct kvm_fpu *fpu)
{
	return -ENOIOCTLCMD;
}

int kvm_arch_vcpu_ioctl_set_fpu(struct kvm_vcpu *vcpu, struct kvm_fpu *fpu)
{
	return -ENOIOCTLCMD;
}

vm_fault_t kvm_arch_vcpu_fault(struct kvm_vcpu *vcpu, struct vm_fault *vmf)
{
	return VM_FAULT_SIGBUS;
}

int kvm_vm_ioctl_check_extension(struct kvm *kvm, long ext)
{
	int r;

	switch (ext) {
	case KVM_CAP_ONE_REG:
	case KVM_CAP_ENABLE_CAP:
	case KVM_CAP_READONLY_MEM:
	case KVM_CAP_SYNC_MMU:
	case KVM_CAP_IMMEDIATE_EXIT:
		r = 1;
		break;
	case KVM_CAP_NR_VCPUS:
		r = num_online_cpus();
		break;
	case KVM_CAP_MAX_VCPUS:
		r = KVM_MAX_VCPUS;
		break;
	case KVM_CAP_MAX_VCPU_ID:
		r = KVM_MAX_VCPU_ID;
		break;
	case KVM_CAP_MIPS_FPU:
		/* We don't handle systems with inconsistent cpu_has_fpu */
		r = !!raw_cpu_has_fpu;
		break;
	case KVM_CAP_MIPS_MSA:
		/*
		 * We don't support MSA vector partitioning yet:
		 * 1) It would require explicit support which can't be tested
		 *    yet due to lack of support in current hardware.
		 * 2) It extends the state that would need to be saved/restored
		 *    by e.g. QEMU for migration.
		 *
		 * When vector partitioning hardware becomes available, support
		 * could be added by requiring a flag when enabling
		 * KVM_CAP_MIPS_MSA capability to indicate that userland knows
		 * to save/restore the appropriate extra state.
		 */
		r = cpu_has_msa && !(boot_cpu_data.msa_id & MSA_IR_WRPF);
		break;
	default:
		r = kvm_mips_callbacks->check_extension(kvm, ext);
		break;
	}
	return r;
}

int kvm_cpu_has_pending_timer(struct kvm_vcpu *vcpu)
{
	return kvm_mips_pending_timer(vcpu) ||
		kvm_read_c0_guest_cause(vcpu->arch.cop0) & C_TI;
}

int kvm_arch_vcpu_dump_regs(struct kvm_vcpu *vcpu)
{
	int i;
	struct mips_coproc *cop0;

	if (!vcpu)
		return -1;

	kvm_debug("VCPU Register Dump:\n");
	kvm_debug("\tpc = 0x%08lx\n", vcpu->arch.pc);
	kvm_debug("\texceptions: %08lx\n", vcpu->arch.pending_exceptions);

	for (i = 0; i < 32; i += 4) {
		kvm_debug("\tgpr%02d: %08lx %08lx %08lx %08lx\n", i,
		       vcpu->arch.gprs[i],
		       vcpu->arch.gprs[i + 1],
		       vcpu->arch.gprs[i + 2], vcpu->arch.gprs[i + 3]);
	}
	kvm_debug("\thi: 0x%08lx\n", vcpu->arch.hi);
	kvm_debug("\tlo: 0x%08lx\n", vcpu->arch.lo);

	cop0 = vcpu->arch.cop0;
	kvm_debug("\tStatus: 0x%08x, Cause: 0x%08x\n",
		  kvm_read_c0_guest_status(cop0),
		  kvm_read_c0_guest_cause(cop0));

	kvm_debug("\tEPC: 0x%08lx\n", kvm_read_c0_guest_epc(cop0));

	return 0;
}

int kvm_arch_vcpu_ioctl_set_regs(struct kvm_vcpu *vcpu, struct kvm_regs *regs)
{
	int i;

	vcpu_load(vcpu);

	for (i = 1; i < ARRAY_SIZE(vcpu->arch.gprs); i++)
		vcpu->arch.gprs[i] = regs->gpr[i];
	vcpu->arch.gprs[0] = 0; /* zero is special, and cannot be set. */
	vcpu->arch.hi = regs->hi;
	vcpu->arch.lo = regs->lo;
	vcpu->arch.pc = regs->pc;

	vcpu_put(vcpu);
	return 0;
}

int kvm_arch_vcpu_ioctl_get_regs(struct kvm_vcpu *vcpu, struct kvm_regs *regs)
{
	int i;

	vcpu_load(vcpu);

	for (i = 0; i < ARRAY_SIZE(vcpu->arch.gprs); i++)
		regs->gpr[i] = vcpu->arch.gprs[i];

	regs->hi = vcpu->arch.hi;
	regs->lo = vcpu->arch.lo;
	regs->pc = vcpu->arch.pc;

	vcpu_put(vcpu);
	return 0;
}

static void kvm_mips_comparecount_func(unsigned long data)
{
	struct kvm_vcpu *vcpu = (struct kvm_vcpu *)data;

	kvm_mips_callbacks->queue_timer_int(vcpu);

	vcpu->arch.wait = 0;
	if (swq_has_sleeper(&vcpu->wq))
		swake_up_one(&vcpu->wq);
}

/* low level hrtimer wake routine */
static enum hrtimer_restart kvm_mips_comparecount_wakeup(struct hrtimer *timer)
{
	struct kvm_vcpu *vcpu;

	vcpu = container_of(timer, struct kvm_vcpu, arch.comparecount_timer);
	kvm_mips_comparecount_func((unsigned long) vcpu);
	return kvm_mips_count_timeout(vcpu);
}

int kvm_arch_vcpu_init(struct kvm_vcpu *vcpu)
{
	int err;

	err = kvm_mips_callbacks->vcpu_init(vcpu);
	if (err)
		return err;

	hrtimer_init(&vcpu->arch.comparecount_timer, CLOCK_MONOTONIC,
		     HRTIMER_MODE_REL);
	vcpu->arch.comparecount_timer.function = kvm_mips_comparecount_wakeup;
	return 0;
}

void kvm_arch_vcpu_uninit(struct kvm_vcpu *vcpu)
{
	kvm_mips_callbacks->vcpu_uninit(vcpu);
}

int kvm_arch_vcpu_ioctl_translate(struct kvm_vcpu *vcpu,
				  struct kvm_translation *tr)
{
	return 0;
}

/* Initial guest state */
int kvm_arch_vcpu_setup(struct kvm_vcpu *vcpu)
{
	return kvm_mips_callbacks->vcpu_setup(vcpu);
}

static void kvm_mips_set_c0_status(void)
{
	u32 status = read_c0_status();

	if (cpu_has_dsp)
		status |= (ST0_MX);

	write_c0_status(status);
	ehb();
}

/*
 * Return value is in the form (errcode<<2 | RESUME_FLAG_HOST | RESUME_FLAG_NV)
 */
int kvm_mips_handle_exit(struct kvm_run *run, struct kvm_vcpu *vcpu)
{
	u32 cause = vcpu->arch.host_cp0_cause;
	u32 exccode = (cause >> CAUSEB_EXCCODE) & 0x1f;
	u32 __user *opc = (u32 __user *) vcpu->arch.pc;
	unsigned long badvaddr = vcpu->arch.host_cp0_badvaddr;
	enum emulation_result er = EMULATE_DONE;
	u32 inst;
	int ret = RESUME_GUEST;

	vcpu->mode = OUTSIDE_GUEST_MODE;

	/* re-enable HTW before enabling interrupts */
	if (!IS_ENABLED(CONFIG_KVM_MIPS_VZ))
		htw_start();

	/* Set a default exit reason */
	run->exit_reason = KVM_EXIT_UNKNOWN;
	run->ready_for_interrupt_injection = 1;

	/*
	 * Set the appropriate status bits based on host CPU features,
	 * before we hit the scheduler
	 */
	kvm_mips_set_c0_status();

	local_irq_enable();

	kvm_debug("kvm_mips_handle_exit: cause: %#x, PC: %p, kvm_run: %p, kvm_vcpu: %p\n",
			cause, opc, run, vcpu);
	trace_kvm_exit(vcpu, exccode);

	if (!IS_ENABLED(CONFIG_KVM_MIPS_VZ)) {
		/*
		 * Do a privilege check, if in UM most of these exit conditions
		 * end up causing an exception to be delivered to the Guest
		 * Kernel
		 */
		er = kvm_mips_check_privilege(cause, opc, run, vcpu);
		if (er == EMULATE_PRIV_FAIL) {
			goto skip_emul;
		} else if (er == EMULATE_FAIL) {
			run->exit_reason = KVM_EXIT_INTERNAL_ERROR;
			ret = RESUME_HOST;
			goto skip_emul;
		}
	}

	switch (exccode) {
	case EXCCODE_INT:
		kvm_debug("[%d]EXCCODE_INT @ %p\n", vcpu->vcpu_id, opc);

		++vcpu->stat.int_exits;

		if (need_resched())
			cond_resched();

		ret = RESUME_GUEST;
		break;

	case EXCCODE_CPU:
		kvm_debug("EXCCODE_CPU: @ PC: %p\n", opc);

		++vcpu->stat.cop_unusable_exits;
		ret = kvm_mips_callbacks->handle_cop_unusable(vcpu);
		/* XXXKYMA: Might need to return to user space */
		if (run->exit_reason == KVM_EXIT_IRQ_WINDOW_OPEN)
			ret = RESUME_HOST;
		break;

	case EXCCODE_MOD:
		++vcpu->stat.tlbmod_exits;
		ret = kvm_mips_callbacks->handle_tlb_mod(vcpu);
		break;

	case EXCCODE_TLBS:
		kvm_debug("TLB ST fault:  cause %#x, status %#x, PC: %p, BadVaddr: %#lx\n",
			  cause, kvm_read_c0_guest_status(vcpu->arch.cop0), opc,
			  badvaddr);

		++vcpu->stat.tlbmiss_st_exits;
		ret = kvm_mips_callbacks->handle_tlb_st_miss(vcpu);
		break;

	case EXCCODE_TLBL:
		kvm_debug("TLB LD fault: cause %#x, PC: %p, BadVaddr: %#lx\n",
			  cause, opc, badvaddr);

		++vcpu->stat.tlbmiss_ld_exits;
		ret = kvm_mips_callbacks->handle_tlb_ld_miss(vcpu);
		break;

	case EXCCODE_ADES:
		++vcpu->stat.addrerr_st_exits;
		ret = kvm_mips_callbacks->handle_addr_err_st(vcpu);
		break;

	case EXCCODE_ADEL:
		++vcpu->stat.addrerr_ld_exits;
		ret = kvm_mips_callbacks->handle_addr_err_ld(vcpu);
		break;

	case EXCCODE_SYS:
		++vcpu->stat.syscall_exits;
		ret = kvm_mips_callbacks->handle_syscall(vcpu);
		break;

	case EXCCODE_RI:
		++vcpu->stat.resvd_inst_exits;
		ret = kvm_mips_callbacks->handle_res_inst(vcpu);
		break;

	case EXCCODE_BP:
		++vcpu->stat.break_inst_exits;
		ret = kvm_mips_callbacks->handle_break(vcpu);
		break;

	case EXCCODE_TR:
		++vcpu->stat.trap_inst_exits;
		ret = kvm_mips_callbacks->handle_trap(vcpu);
		break;

	case EXCCODE_MSAFPE:
		++vcpu->stat.msa_fpe_exits;
		ret = kvm_mips_callbacks->handle_msa_fpe(vcpu);
		break;

	case EXCCODE_FPE:
		++vcpu->stat.fpe_exits;
		ret = kvm_mips_callbacks->handle_fpe(vcpu);
		break;

	case EXCCODE_MSADIS:
		++vcpu->stat.msa_disabled_exits;
		ret = kvm_mips_callbacks->handle_msa_disabled(vcpu);
		break;

	case EXCCODE_GE:
		/* defer exit accounting to handler */
		ret = kvm_mips_callbacks->handle_guest_exit(vcpu);
		break;

	default:
		if (cause & CAUSEF_BD)
			opc += 1;
		inst = 0;
		kvm_get_badinstr(opc, vcpu, &inst);
		kvm_err("Exception Code: %d, not yet handled, @ PC: %p, inst: 0x%08x  BadVaddr: %#lx Status: %#x\n",
			exccode, opc, inst, badvaddr,
			kvm_read_c0_guest_status(vcpu->arch.cop0));
		kvm_arch_vcpu_dump_regs(vcpu);
		run->exit_reason = KVM_EXIT_INTERNAL_ERROR;
		ret = RESUME_HOST;
		break;

	}

skip_emul:
	local_irq_disable();

	if (ret == RESUME_GUEST)
		kvm_vz_acquire_htimer(vcpu);

	if (er == EMULATE_DONE && !(ret & RESUME_HOST))
		kvm_mips_deliver_interrupts(vcpu, cause);

	if (!(ret & RESUME_HOST)) {
		/* Only check for signals if not already exiting to userspace */
		if (signal_pending(current)) {
			run->exit_reason = KVM_EXIT_INTR;
			ret = (-EINTR << 2) | RESUME_HOST;
			++vcpu->stat.signal_exits;
			trace_kvm_exit(vcpu, KVM_TRACE_EXIT_SIGNAL);
		}
	}

	if (ret == RESUME_GUEST) {
		trace_kvm_reenter(vcpu);

		/*
		 * Make sure the read of VCPU requests in vcpu_reenter()
		 * callback is not reordered ahead of the write to vcpu->mode,
		 * or we could miss a TLB flush request while the requester sees
		 * the VCPU as outside of guest mode and not needing an IPI.
		 */
		smp_store_mb(vcpu->mode, IN_GUEST_MODE);

		kvm_mips_callbacks->vcpu_reenter(run, vcpu);

		/*
		 * If FPU / MSA are enabled (i.e. the guest's FPU / MSA context
		 * is live), restore FCR31 / MSACSR.
		 *
		 * This should be before returning to the guest exception
		 * vector, as it may well cause an [MSA] FP exception if there
		 * are pending exception bits unmasked. (see
		 * kvm_mips_csr_die_notifier() for how that is handled).
		 */
		if (kvm_mips_guest_has_fpu(&vcpu->arch) &&
		    read_c0_status() & ST0_CU1)
			__kvm_restore_fcsr(&vcpu->arch);

		if (kvm_mips_guest_has_msa(&vcpu->arch) &&
		    read_c0_config5() & MIPS_CONF5_MSAEN)
			__kvm_restore_msacsr(&vcpu->arch);
	}

	/* Disable HTW before returning to guest or host */
	if (!IS_ENABLED(CONFIG_KVM_MIPS_VZ))
		htw_stop();

	return ret;
}

/* Enable FPU for guest and restore context */
void kvm_own_fpu(struct kvm_vcpu *vcpu)
{
	struct mips_coproc *cop0 = vcpu->arch.cop0;
	unsigned int sr, cfg5;

	preempt_disable();

	sr = kvm_read_c0_guest_status(cop0);

	/*
	 * If MSA state is already live, it is undefined how it interacts with
	 * FR=0 FPU state, and we don't want to hit reserved instruction
	 * exceptions trying to save the MSA state later when CU=1 && FR=1, so
	 * play it safe and save it first.
	 *
	 * In theory we shouldn't ever hit this case since kvm_lose_fpu() should
	 * get called when guest CU1 is set, however we can't trust the guest
	 * not to clobber the status register directly via the commpage.
	 */
	if (cpu_has_msa && sr & ST0_CU1 && !(sr & ST0_FR) &&
	    vcpu->arch.aux_inuse & KVM_MIPS_AUX_MSA)
		kvm_lose_fpu(vcpu);

	/*
	 * Enable FPU for guest
	 * We set FR and FRE according to guest context
	 */
	change_c0_status(ST0_CU1 | ST0_FR, sr);
	if (cpu_has_fre) {
		cfg5 = kvm_read_c0_guest_config5(cop0);
		change_c0_config5(MIPS_CONF5_FRE, cfg5);
	}
	enable_fpu_hazard();

	/* If guest FPU state not active, restore it now */
	if (!(vcpu->arch.aux_inuse & KVM_MIPS_AUX_FPU)) {
		__kvm_restore_fpu(&vcpu->arch);
		vcpu->arch.aux_inuse |= KVM_MIPS_AUX_FPU;
		trace_kvm_aux(vcpu, KVM_TRACE_AUX_RESTORE, KVM_TRACE_AUX_FPU);
	} else {
		trace_kvm_aux(vcpu, KVM_TRACE_AUX_ENABLE, KVM_TRACE_AUX_FPU);
	}

	preempt_enable();
}

#ifdef CONFIG_CPU_HAS_MSA
/* Enable MSA for guest and restore context */
void kvm_own_msa(struct kvm_vcpu *vcpu)
{
	struct mips_coproc *cop0 = vcpu->arch.cop0;
	unsigned int sr, cfg5;

	preempt_disable();

	/*
	 * Enable FPU if enabled in guest, since we're restoring FPU context
	 * anyway. We set FR and FRE according to guest context.
	 */
	if (kvm_mips_guest_has_fpu(&vcpu->arch)) {
		sr = kvm_read_c0_guest_status(cop0);

		/*
		 * If FR=0 FPU state is already live, it is undefined how it
		 * interacts with MSA state, so play it safe and save it first.
		 */
		if (!(sr & ST0_FR) &&
		    (vcpu->arch.aux_inuse & (KVM_MIPS_AUX_FPU |
				KVM_MIPS_AUX_MSA)) == KVM_MIPS_AUX_FPU)
			kvm_lose_fpu(vcpu);

		change_c0_status(ST0_CU1 | ST0_FR, sr);
		if (sr & ST0_CU1 && cpu_has_fre) {
			cfg5 = kvm_read_c0_guest_config5(cop0);
			change_c0_config5(MIPS_CONF5_FRE, cfg5);
		}
	}

	/* Enable MSA for guest */
	set_c0_config5(MIPS_CONF5_MSAEN);
	enable_fpu_hazard();

	switch (vcpu->arch.aux_inuse & (KVM_MIPS_AUX_FPU | KVM_MIPS_AUX_MSA)) {
	case KVM_MIPS_AUX_FPU:
		/*
		 * Guest FPU state already loaded, only restore upper MSA state
		 */
		__kvm_restore_msa_upper(&vcpu->arch);
		vcpu->arch.aux_inuse |= KVM_MIPS_AUX_MSA;
		trace_kvm_aux(vcpu, KVM_TRACE_AUX_RESTORE, KVM_TRACE_AUX_MSA);
		break;
	case 0:
		/* Neither FPU or MSA already active, restore full MSA state */
		__kvm_restore_msa(&vcpu->arch);
		vcpu->arch.aux_inuse |= KVM_MIPS_AUX_MSA;
		if (kvm_mips_guest_has_fpu(&vcpu->arch))
			vcpu->arch.aux_inuse |= KVM_MIPS_AUX_FPU;
		trace_kvm_aux(vcpu, KVM_TRACE_AUX_RESTORE,
			      KVM_TRACE_AUX_FPU_MSA);
		break;
	default:
		trace_kvm_aux(vcpu, KVM_TRACE_AUX_ENABLE, KVM_TRACE_AUX_MSA);
		break;
	}

	preempt_enable();
}
#endif

/* Drop FPU & MSA without saving it */
void kvm_drop_fpu(struct kvm_vcpu *vcpu)
{
	preempt_disable();
	if (cpu_has_msa && vcpu->arch.aux_inuse & KVM_MIPS_AUX_MSA) {
		disable_msa();
		trace_kvm_aux(vcpu, KVM_TRACE_AUX_DISCARD, KVM_TRACE_AUX_MSA);
		vcpu->arch.aux_inuse &= ~KVM_MIPS_AUX_MSA;
	}
	if (vcpu->arch.aux_inuse & KVM_MIPS_AUX_FPU) {
		clear_c0_status(ST0_CU1 | ST0_FR);
		trace_kvm_aux(vcpu, KVM_TRACE_AUX_DISCARD, KVM_TRACE_AUX_FPU);
		vcpu->arch.aux_inuse &= ~KVM_MIPS_AUX_FPU;
	}
	preempt_enable();
}

/* Save and disable FPU & MSA */
void kvm_lose_fpu(struct kvm_vcpu *vcpu)
{
	/*
	 * With T&E, FPU & MSA get disabled in root context (hardware) when it
	 * is disabled in guest context (software), but the register state in
	 * the hardware may still be in use.
	 * This is why we explicitly re-enable the hardware before saving.
	 */

	preempt_disable();
	if (cpu_has_msa && vcpu->arch.aux_inuse & KVM_MIPS_AUX_MSA) {
		if (!IS_ENABLED(CONFIG_KVM_MIPS_VZ)) {
			set_c0_config5(MIPS_CONF5_MSAEN);
			enable_fpu_hazard();
		}

		__kvm_save_msa(&vcpu->arch);
		trace_kvm_aux(vcpu, KVM_TRACE_AUX_SAVE, KVM_TRACE_AUX_FPU_MSA);

		/* Disable MSA & FPU */
		disable_msa();
		if (vcpu->arch.aux_inuse & KVM_MIPS_AUX_FPU) {
			clear_c0_status(ST0_CU1 | ST0_FR);
			disable_fpu_hazard();
		}
		vcpu->arch.aux_inuse &= ~(KVM_MIPS_AUX_FPU | KVM_MIPS_AUX_MSA);
	} else if (vcpu->arch.aux_inuse & KVM_MIPS_AUX_FPU) {
		if (!IS_ENABLED(CONFIG_KVM_MIPS_VZ)) {
			set_c0_status(ST0_CU1);
			enable_fpu_hazard();
		}

		__kvm_save_fpu(&vcpu->arch);
		vcpu->arch.aux_inuse &= ~KVM_MIPS_AUX_FPU;
		trace_kvm_aux(vcpu, KVM_TRACE_AUX_SAVE, KVM_TRACE_AUX_FPU);

		/* Disable FPU */
		clear_c0_status(ST0_CU1 | ST0_FR);
		disable_fpu_hazard();
	}
	preempt_enable();
}

/*
 * Step over a specific ctc1 to FCSR and a specific ctcmsa to MSACSR which are
 * used to restore guest FCSR/MSACSR state and may trigger a "harmless" FP/MSAFP
 * exception if cause bits are set in the value being written.
 */
static int kvm_mips_csr_die_notify(struct notifier_block *self,
				   unsigned long cmd, void *ptr)
{
	struct die_args *args = (struct die_args *)ptr;
	struct pt_regs *regs = args->regs;
	unsigned long pc;

	/* Only interested in FPE and MSAFPE */
	if (cmd != DIE_FP && cmd != DIE_MSAFP)
		return NOTIFY_DONE;

	/* Return immediately if guest context isn't active */
	if (!(current->flags & PF_VCPU))
		return NOTIFY_DONE;

	/* Should never get here from user mode */
	BUG_ON(user_mode(regs));

	pc = instruction_pointer(regs);
	switch (cmd) {
	case DIE_FP:
		/* match 2nd instruction in __kvm_restore_fcsr */
		if (pc != (unsigned long)&__kvm_restore_fcsr + 4)
			return NOTIFY_DONE;
		break;
	case DIE_MSAFP:
		/* match 2nd/3rd instruction in __kvm_restore_msacsr */
		if (!cpu_has_msa ||
		    pc < (unsigned long)&__kvm_restore_msacsr + 4 ||
		    pc > (unsigned long)&__kvm_restore_msacsr + 8)
			return NOTIFY_DONE;
		break;
	}

	/* Move PC forward a little and continue executing */
	instruction_pointer(regs) += 4;

	return NOTIFY_STOP;
}

static struct notifier_block kvm_mips_csr_die_notifier = {
	.notifier_call = kvm_mips_csr_die_notify,
};

static int __init kvm_mips_init(void)
{
	int ret;

	if (cpu_has_mmid) {
		pr_warn("KVM does not yet support MMIDs. KVM Disabled\n");
		return -EOPNOTSUPP;
	}

	ret = kvm_mips_entry_setup();
	if (ret)
		return ret;

	ret = kvm_init(NULL, sizeof(struct kvm_vcpu), 0, THIS_MODULE);

	if (ret)
		return ret;

	register_die_notifier(&kvm_mips_csr_die_notifier);

	return 0;
}

static void __exit kvm_mips_exit(void)
{
	kvm_exit();

	unregister_die_notifier(&kvm_mips_csr_die_notifier);
}

module_init(kvm_mips_init);
module_exit(kvm_mips_exit);

EXPORT_TRACEPOINT_SYMBOL(kvm_exit);
