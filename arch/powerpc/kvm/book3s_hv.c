// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2011 Paul Mackerras, IBM Corp. <paulus@au1.ibm.com>
 * Copyright (C) 2009. SUSE Linux Products GmbH. All rights reserved.
 *
 * Authors:
 *    Paul Mackerras <paulus@au1.ibm.com>
 *    Alexander Graf <agraf@suse.de>
 *    Kevin Wolf <mail@kevin-wolf.de>
 *
 * Description: KVM functions specific to running on Book 3S
 * processors in hypervisor mode (specifically POWER7 and later).
 *
 * This file is derived from arch/powerpc/kvm/book3s.c,
 * by Alexander Graf <agraf@suse.de>.
 */

#include <linux/kvm_host.h>
#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/preempt.h>
#include <linux/sched/signal.h>
#include <linux/sched/stat.h>
#include <linux/delay.h>
#include <linux/export.h>
#include <linux/fs.h>
#include <linux/anon_inodes.h>
#include <linux/cpu.h>
#include <linux/cpumask.h>
#include <linux/spinlock.h>
#include <linux/page-flags.h>
#include <linux/srcu.h>
#include <linux/miscdevice.h>
#include <linux/debugfs.h>
#include <linux/gfp.h>
#include <linux/vmalloc.h>
#include <linux/highmem.h>
#include <linux/hugetlb.h>
#include <linux/kvm_irqfd.h>
#include <linux/irqbypass.h>
#include <linux/module.h>
#include <linux/compiler.h>
#include <linux/of.h>

#include <asm/ftrace.h>
#include <asm/reg.h>
#include <asm/ppc-opcode.h>
#include <asm/asm-prototypes.h>
#include <asm/archrandom.h>
#include <asm/debug.h>
#include <asm/disassemble.h>
#include <asm/cputable.h>
#include <asm/cacheflush.h>
#include <linux/uaccess.h>
#include <asm/io.h>
#include <asm/kvm_ppc.h>
#include <asm/kvm_book3s.h>
#include <asm/mmu_context.h>
#include <asm/lppaca.h>
#include <asm/processor.h>
#include <asm/cputhreads.h>
#include <asm/page.h>
#include <asm/hvcall.h>
#include <asm/switch_to.h>
#include <asm/smp.h>
#include <asm/dbell.h>
#include <asm/hmi.h>
#include <asm/pnv-pci.h>
#include <asm/mmu.h>
#include <asm/opal.h>
#include <asm/xics.h>
#include <asm/xive.h>
#include <asm/hw_breakpoint.h>
#include <asm/kvm_host.h>
#include <asm/kvm_book3s_uvmem.h>
#include <asm/ultravisor.h>

#include "book3s.h"

#define CREATE_TRACE_POINTS
#include "trace_hv.h"

/* #define EXIT_DEBUG */
/* #define EXIT_DEBUG_SIMPLE */
/* #define EXIT_DEBUG_INT */

/* Used to indicate that a guest page fault needs to be handled */
#define RESUME_PAGE_FAULT	(RESUME_GUEST | RESUME_FLAG_ARCH1)
/* Used to indicate that a guest passthrough interrupt needs to be handled */
#define RESUME_PASSTHROUGH	(RESUME_GUEST | RESUME_FLAG_ARCH2)

/* Used as a "null" value for timebase values */
#define TB_NIL	(~(u64)0)

static DECLARE_BITMAP(default_enabled_hcalls, MAX_HCALL_OPCODE/4 + 1);

static int dynamic_mt_modes = 6;
module_param(dynamic_mt_modes, int, 0644);
MODULE_PARM_DESC(dynamic_mt_modes, "Set of allowed dynamic micro-threading modes: 0 (= none), 2, 4, or 6 (= 2 or 4)");
static int target_smt_mode;
module_param(target_smt_mode, int, 0644);
MODULE_PARM_DESC(target_smt_mode, "Target threads per core (0 = max)");

static bool indep_threads_mode = true;
module_param(indep_threads_mode, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(indep_threads_mode, "Independent-threads mode (only on POWER9)");

static bool one_vm_per_core;
module_param(one_vm_per_core, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(one_vm_per_core, "Only run vCPUs from the same VM on a core (requires indep_threads_mode=N)");

#ifdef CONFIG_KVM_XICS
static struct kernel_param_ops module_param_ops = {
	.set = param_set_int,
	.get = param_get_int,
};

module_param_cb(kvm_irq_bypass, &module_param_ops, &kvm_irq_bypass, 0644);
MODULE_PARM_DESC(kvm_irq_bypass, "Bypass passthrough interrupt optimization");

module_param_cb(h_ipi_redirect, &module_param_ops, &h_ipi_redirect, 0644);
MODULE_PARM_DESC(h_ipi_redirect, "Redirect H_IPI wakeup to a free host core");
#endif

/* If set, guests are allowed to create and control nested guests */
static bool nested = true;
module_param(nested, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(nested, "Enable nested virtualization (only on POWER9)");

static inline bool nesting_enabled(struct kvm *kvm)
{
	return kvm->arch.nested_enable && kvm_is_radix(kvm);
}

/* If set, the threads on each CPU core have to be in the same MMU mode */
static bool no_mixing_hpt_and_radix;

static int kvmppc_hv_setup_htab_rma(struct kvm_vcpu *vcpu);

/*
 * RWMR values for POWER8.  These control the rate at which PURR
 * and SPURR count and should be set according to the number of
 * online threads in the vcore being run.
 */
#define RWMR_RPA_P8_1THREAD	0x164520C62609AECAUL
#define RWMR_RPA_P8_2THREAD	0x7FFF2908450D8DA9UL
#define RWMR_RPA_P8_3THREAD	0x164520C62609AECAUL
#define RWMR_RPA_P8_4THREAD	0x199A421245058DA9UL
#define RWMR_RPA_P8_5THREAD	0x164520C62609AECAUL
#define RWMR_RPA_P8_6THREAD	0x164520C62609AECAUL
#define RWMR_RPA_P8_7THREAD	0x164520C62609AECAUL
#define RWMR_RPA_P8_8THREAD	0x164520C62609AECAUL

static unsigned long p8_rwmr_values[MAX_SMT_THREADS + 1] = {
	RWMR_RPA_P8_1THREAD,
	RWMR_RPA_P8_1THREAD,
	RWMR_RPA_P8_2THREAD,
	RWMR_RPA_P8_3THREAD,
	RWMR_RPA_P8_4THREAD,
	RWMR_RPA_P8_5THREAD,
	RWMR_RPA_P8_6THREAD,
	RWMR_RPA_P8_7THREAD,
	RWMR_RPA_P8_8THREAD,
};

static inline struct kvm_vcpu *next_runnable_thread(struct kvmppc_vcore *vc,
		int *ip)
{
	int i = *ip;
	struct kvm_vcpu *vcpu;

	while (++i < MAX_SMT_THREADS) {
		vcpu = READ_ONCE(vc->runnable_threads[i]);
		if (vcpu) {
			*ip = i;
			return vcpu;
		}
	}
	return NULL;
}

/* Used to traverse the list of runnable threads for a given vcore */
#define for_each_runnable_thread(i, vcpu, vc) \
	for (i = -1; (vcpu = next_runnable_thread(vc, &i)); )

static bool kvmppc_ipi_thread(int cpu)
{
	unsigned long msg = PPC_DBELL_TYPE(PPC_DBELL_SERVER);

	/* If we're a nested hypervisor, fall back to ordinary IPIs for now */
	if (kvmhv_on_pseries())
		return false;

	/* On POWER9 we can use msgsnd to IPI any cpu */
	if (cpu_has_feature(CPU_FTR_ARCH_300)) {
		msg |= get_hard_smp_processor_id(cpu);
		smp_mb();
		__asm__ __volatile__ (PPC_MSGSND(%0) : : "r" (msg));
		return true;
	}

	/* On POWER8 for IPIs to threads in the same core, use msgsnd */
	if (cpu_has_feature(CPU_FTR_ARCH_207S)) {
		preempt_disable();
		if (cpu_first_thread_sibling(cpu) ==
		    cpu_first_thread_sibling(smp_processor_id())) {
			msg |= cpu_thread_in_core(cpu);
			smp_mb();
			__asm__ __volatile__ (PPC_MSGSND(%0) : : "r" (msg));
			preempt_enable();
			return true;
		}
		preempt_enable();
	}

#if defined(CONFIG_PPC_ICP_NATIVE) && defined(CONFIG_SMP)
	if (cpu >= 0 && cpu < nr_cpu_ids) {
		if (paca_ptrs[cpu]->kvm_hstate.xics_phys) {
			xics_wake_cpu(cpu);
			return true;
		}
		opal_int_set_mfrr(get_hard_smp_processor_id(cpu), IPI_PRIORITY);
		return true;
	}
#endif

	return false;
}

static void kvmppc_fast_vcpu_kick_hv(struct kvm_vcpu *vcpu)
{
	int cpu;
	struct swait_queue_head *wqp;

	wqp = kvm_arch_vcpu_wq(vcpu);
	if (swq_has_sleeper(wqp)) {
		swake_up_one(wqp);
		++vcpu->stat.halt_wakeup;
	}

	cpu = READ_ONCE(vcpu->arch.thread_cpu);
	if (cpu >= 0 && kvmppc_ipi_thread(cpu))
		return;

	/* CPU points to the first thread of the core */
	cpu = vcpu->cpu;
	if (cpu >= 0 && cpu < nr_cpu_ids && cpu_online(cpu))
		smp_send_reschedule(cpu);
}

/*
 * We use the vcpu_load/put functions to measure stolen time.
 * Stolen time is counted as time when either the vcpu is able to
 * run as part of a virtual core, but the task running the vcore
 * is preempted or sleeping, or when the vcpu needs something done
 * in the kernel by the task running the vcpu, but that task is
 * preempted or sleeping.  Those two things have to be counted
 * separately, since one of the vcpu tasks will take on the job
 * of running the core, and the other vcpu tasks in the vcore will
 * sleep waiting for it to do that, but that sleep shouldn't count
 * as stolen time.
 *
 * Hence we accumulate stolen time when the vcpu can run as part of
 * a vcore using vc->stolen_tb, and the stolen time when the vcpu
 * needs its task to do other things in the kernel (for example,
 * service a page fault) in busy_stolen.  We don't accumulate
 * stolen time for a vcore when it is inactive, or for a vcpu
 * when it is in state RUNNING or NOTREADY.  NOTREADY is a bit of
 * a misnomer; it means that the vcpu task is not executing in
 * the KVM_VCPU_RUN ioctl, i.e. it is in userspace or elsewhere in
 * the kernel.  We don't have any way of dividing up that time
 * between time that the vcpu is genuinely stopped, time that
 * the task is actively working on behalf of the vcpu, and time
 * that the task is preempted, so we don't count any of it as
 * stolen.
 *
 * Updates to busy_stolen are protected by arch.tbacct_lock;
 * updates to vc->stolen_tb are protected by the vcore->stoltb_lock
 * lock.  The stolen times are measured in units of timebase ticks.
 * (Note that the != TB_NIL checks below are purely defensive;
 * they should never fail.)
 */

static void kvmppc_core_start_stolen(struct kvmppc_vcore *vc)
{
	unsigned long flags;

	spin_lock_irqsave(&vc->stoltb_lock, flags);
	vc->preempt_tb = mftb();
	spin_unlock_irqrestore(&vc->stoltb_lock, flags);
}

static void kvmppc_core_end_stolen(struct kvmppc_vcore *vc)
{
	unsigned long flags;

	spin_lock_irqsave(&vc->stoltb_lock, flags);
	if (vc->preempt_tb != TB_NIL) {
		vc->stolen_tb += mftb() - vc->preempt_tb;
		vc->preempt_tb = TB_NIL;
	}
	spin_unlock_irqrestore(&vc->stoltb_lock, flags);
}

static void kvmppc_core_vcpu_load_hv(struct kvm_vcpu *vcpu, int cpu)
{
	struct kvmppc_vcore *vc = vcpu->arch.vcore;
	unsigned long flags;

	/*
	 * We can test vc->runner without taking the vcore lock,
	 * because only this task ever sets vc->runner to this
	 * vcpu, and once it is set to this vcpu, only this task
	 * ever sets it to NULL.
	 */
	if (vc->runner == vcpu && vc->vcore_state >= VCORE_SLEEPING)
		kvmppc_core_end_stolen(vc);

	spin_lock_irqsave(&vcpu->arch.tbacct_lock, flags);
	if (vcpu->arch.state == KVMPPC_VCPU_BUSY_IN_HOST &&
	    vcpu->arch.busy_preempt != TB_NIL) {
		vcpu->arch.busy_stolen += mftb() - vcpu->arch.busy_preempt;
		vcpu->arch.busy_preempt = TB_NIL;
	}
	spin_unlock_irqrestore(&vcpu->arch.tbacct_lock, flags);
}

static void kvmppc_core_vcpu_put_hv(struct kvm_vcpu *vcpu)
{
	struct kvmppc_vcore *vc = vcpu->arch.vcore;
	unsigned long flags;

	if (vc->runner == vcpu && vc->vcore_state >= VCORE_SLEEPING)
		kvmppc_core_start_stolen(vc);

	spin_lock_irqsave(&vcpu->arch.tbacct_lock, flags);
	if (vcpu->arch.state == KVMPPC_VCPU_BUSY_IN_HOST)
		vcpu->arch.busy_preempt = mftb();
	spin_unlock_irqrestore(&vcpu->arch.tbacct_lock, flags);
}

static void kvmppc_set_pvr_hv(struct kvm_vcpu *vcpu, u32 pvr)
{
	vcpu->arch.pvr = pvr;
}

/* Dummy value used in computing PCR value below */
#define PCR_ARCH_300	(PCR_ARCH_207 << 1)

static int kvmppc_set_arch_compat(struct kvm_vcpu *vcpu, u32 arch_compat)
{
	unsigned long host_pcr_bit = 0, guest_pcr_bit = 0;
	struct kvmppc_vcore *vc = vcpu->arch.vcore;

	/* We can (emulate) our own architecture version and anything older */
	if (cpu_has_feature(CPU_FTR_ARCH_300))
		host_pcr_bit = PCR_ARCH_300;
	else if (cpu_has_feature(CPU_FTR_ARCH_207S))
		host_pcr_bit = PCR_ARCH_207;
	else if (cpu_has_feature(CPU_FTR_ARCH_206))
		host_pcr_bit = PCR_ARCH_206;
	else
		host_pcr_bit = PCR_ARCH_205;

	/* Determine lowest PCR bit needed to run guest in given PVR level */
	guest_pcr_bit = host_pcr_bit;
	if (arch_compat) {
		switch (arch_compat) {
		case PVR_ARCH_205:
			guest_pcr_bit = PCR_ARCH_205;
			break;
		case PVR_ARCH_206:
		case PVR_ARCH_206p:
			guest_pcr_bit = PCR_ARCH_206;
			break;
		case PVR_ARCH_207:
			guest_pcr_bit = PCR_ARCH_207;
			break;
		case PVR_ARCH_300:
			guest_pcr_bit = PCR_ARCH_300;
			break;
		default:
			return -EINVAL;
		}
	}

	/* Check requested PCR bits don't exceed our capabilities */
	if (guest_pcr_bit > host_pcr_bit)
		return -EINVAL;

	spin_lock(&vc->lock);
	vc->arch_compat = arch_compat;
	/*
	 * Set all PCR bits for which guest_pcr_bit <= bit < host_pcr_bit
	 * Also set all reserved PCR bits
	 */
	vc->pcr = (host_pcr_bit - guest_pcr_bit) | PCR_MASK;
	spin_unlock(&vc->lock);

	return 0;
}

static void kvmppc_dump_regs(struct kvm_vcpu *vcpu)
{
	int r;

	pr_err("vcpu %p (%d):\n", vcpu, vcpu->vcpu_id);
	pr_err("pc  = %.16lx  msr = %.16llx  trap = %x\n",
	       vcpu->arch.regs.nip, vcpu->arch.shregs.msr, vcpu->arch.trap);
	for (r = 0; r < 16; ++r)
		pr_err("r%2d = %.16lx  r%d = %.16lx\n",
		       r, kvmppc_get_gpr(vcpu, r),
		       r+16, kvmppc_get_gpr(vcpu, r+16));
	pr_err("ctr = %.16lx  lr  = %.16lx\n",
	       vcpu->arch.regs.ctr, vcpu->arch.regs.link);
	pr_err("srr0 = %.16llx srr1 = %.16llx\n",
	       vcpu->arch.shregs.srr0, vcpu->arch.shregs.srr1);
	pr_err("sprg0 = %.16llx sprg1 = %.16llx\n",
	       vcpu->arch.shregs.sprg0, vcpu->arch.shregs.sprg1);
	pr_err("sprg2 = %.16llx sprg3 = %.16llx\n",
	       vcpu->arch.shregs.sprg2, vcpu->arch.shregs.sprg3);
	pr_err("cr = %.8lx  xer = %.16lx  dsisr = %.8x\n",
	       vcpu->arch.regs.ccr, vcpu->arch.regs.xer, vcpu->arch.shregs.dsisr);
	pr_err("dar = %.16llx\n", vcpu->arch.shregs.dar);
	pr_err("fault dar = %.16lx dsisr = %.8x\n",
	       vcpu->arch.fault_dar, vcpu->arch.fault_dsisr);
	pr_err("SLB (%d entries):\n", vcpu->arch.slb_max);
	for (r = 0; r < vcpu->arch.slb_max; ++r)
		pr_err("  ESID = %.16llx VSID = %.16llx\n",
		       vcpu->arch.slb[r].orige, vcpu->arch.slb[r].origv);
	pr_err("lpcr = %.16lx sdr1 = %.16lx last_inst = %.8x\n",
	       vcpu->arch.vcore->lpcr, vcpu->kvm->arch.sdr1,
	       vcpu->arch.last_inst);
}

static struct kvm_vcpu *kvmppc_find_vcpu(struct kvm *kvm, int id)
{
	return kvm_get_vcpu_by_id(kvm, id);
}

static void init_vpa(struct kvm_vcpu *vcpu, struct lppaca *vpa)
{
	vpa->__old_status |= LPPACA_OLD_SHARED_PROC;
	vpa->yield_count = cpu_to_be32(1);
}

static int set_vpa(struct kvm_vcpu *vcpu, struct kvmppc_vpa *v,
		   unsigned long addr, unsigned long len)
{
	/* check address is cacheline aligned */
	if (addr & (L1_CACHE_BYTES - 1))
		return -EINVAL;
	spin_lock(&vcpu->arch.vpa_update_lock);
	if (v->next_gpa != addr || v->len != len) {
		v->next_gpa = addr;
		v->len = addr ? len : 0;
		v->update_pending = 1;
	}
	spin_unlock(&vcpu->arch.vpa_update_lock);
	return 0;
}

/* Length for a per-processor buffer is passed in at offset 4 in the buffer */
struct reg_vpa {
	u32 dummy;
	union {
		__be16 hword;
		__be32 word;
	} length;
};

static int vpa_is_registered(struct kvmppc_vpa *vpap)
{
	if (vpap->update_pending)
		return vpap->next_gpa != 0;
	return vpap->pinned_addr != NULL;
}

static unsigned long do_h_register_vpa(struct kvm_vcpu *vcpu,
				       unsigned long flags,
				       unsigned long vcpuid, unsigned long vpa)
{
	struct kvm *kvm = vcpu->kvm;
	unsigned long len, nb;
	void *va;
	struct kvm_vcpu *tvcpu;
	int err;
	int subfunc;
	struct kvmppc_vpa *vpap;

	tvcpu = kvmppc_find_vcpu(kvm, vcpuid);
	if (!tvcpu)
		return H_PARAMETER;

	subfunc = (flags >> H_VPA_FUNC_SHIFT) & H_VPA_FUNC_MASK;
	if (subfunc == H_VPA_REG_VPA || subfunc == H_VPA_REG_DTL ||
	    subfunc == H_VPA_REG_SLB) {
		/* Registering new area - address must be cache-line aligned */
		if ((vpa & (L1_CACHE_BYTES - 1)) || !vpa)
			return H_PARAMETER;

		/* convert logical addr to kernel addr and read length */
		va = kvmppc_pin_guest_page(kvm, vpa, &nb);
		if (va == NULL)
			return H_PARAMETER;
		if (subfunc == H_VPA_REG_VPA)
			len = be16_to_cpu(((struct reg_vpa *)va)->length.hword);
		else
			len = be32_to_cpu(((struct reg_vpa *)va)->length.word);
		kvmppc_unpin_guest_page(kvm, va, vpa, false);

		/* Check length */
		if (len > nb || len < sizeof(struct reg_vpa))
			return H_PARAMETER;
	} else {
		vpa = 0;
		len = 0;
	}

	err = H_PARAMETER;
	vpap = NULL;
	spin_lock(&tvcpu->arch.vpa_update_lock);

	switch (subfunc) {
	case H_VPA_REG_VPA:		/* register VPA */
		/*
		 * The size of our lppaca is 1kB because of the way we align
		 * it for the guest to avoid crossing a 4kB boundary. We only
		 * use 640 bytes of the structure though, so we should accept
		 * clients that set a size of 640.
		 */
		BUILD_BUG_ON(sizeof(struct lppaca) != 640);
		if (len < sizeof(struct lppaca))
			break;
		vpap = &tvcpu->arch.vpa;
		err = 0;
		break;

	case H_VPA_REG_DTL:		/* register DTL */
		if (len < sizeof(struct dtl_entry))
			break;
		len -= len % sizeof(struct dtl_entry);

		/* Check that they have previously registered a VPA */
		err = H_RESOURCE;
		if (!vpa_is_registered(&tvcpu->arch.vpa))
			break;

		vpap = &tvcpu->arch.dtl;
		err = 0;
		break;

	case H_VPA_REG_SLB:		/* register SLB shadow buffer */
		/* Check that they have previously registered a VPA */
		err = H_RESOURCE;
		if (!vpa_is_registered(&tvcpu->arch.vpa))
			break;

		vpap = &tvcpu->arch.slb_shadow;
		err = 0;
		break;

	case H_VPA_DEREG_VPA:		/* deregister VPA */
		/* Check they don't still have a DTL or SLB buf registered */
		err = H_RESOURCE;
		if (vpa_is_registered(&tvcpu->arch.dtl) ||
		    vpa_is_registered(&tvcpu->arch.slb_shadow))
			break;

		vpap = &tvcpu->arch.vpa;
		err = 0;
		break;

	case H_VPA_DEREG_DTL:		/* deregister DTL */
		vpap = &tvcpu->arch.dtl;
		err = 0;
		break;

	case H_VPA_DEREG_SLB:		/* deregister SLB shadow buffer */
		vpap = &tvcpu->arch.slb_shadow;
		err = 0;
		break;
	}

	if (vpap) {
		vpap->next_gpa = vpa;
		vpap->len = len;
		vpap->update_pending = 1;
	}

	spin_unlock(&tvcpu->arch.vpa_update_lock);

	return err;
}

static void kvmppc_update_vpa(struct kvm_vcpu *vcpu, struct kvmppc_vpa *vpap)
{
	struct kvm *kvm = vcpu->kvm;
	void *va;
	unsigned long nb;
	unsigned long gpa;

	/*
	 * We need to pin the page pointed to by vpap->next_gpa,
	 * but we can't call kvmppc_pin_guest_page under the lock
	 * as it does get_user_pages() and down_read().  So we
	 * have to drop the lock, pin the page, then get the lock
	 * again and check that a new area didn't get registered
	 * in the meantime.
	 */
	for (;;) {
		gpa = vpap->next_gpa;
		spin_unlock(&vcpu->arch.vpa_update_lock);
		va = NULL;
		nb = 0;
		if (gpa)
			va = kvmppc_pin_guest_page(kvm, gpa, &nb);
		spin_lock(&vcpu->arch.vpa_update_lock);
		if (gpa == vpap->next_gpa)
			break;
		/* sigh... unpin that one and try again */
		if (va)
			kvmppc_unpin_guest_page(kvm, va, gpa, false);
	}

	vpap->update_pending = 0;
	if (va && nb < vpap->len) {
		/*
		 * If it's now too short, it must be that userspace
		 * has changed the mappings underlying guest memory,
		 * so unregister the region.
		 */
		kvmppc_unpin_guest_page(kvm, va, gpa, false);
		va = NULL;
	}
	if (vpap->pinned_addr)
		kvmppc_unpin_guest_page(kvm, vpap->pinned_addr, vpap->gpa,
					vpap->dirty);
	vpap->gpa = gpa;
	vpap->pinned_addr = va;
	vpap->dirty = false;
	if (va)
		vpap->pinned_end = va + vpap->len;
}

static void kvmppc_update_vpas(struct kvm_vcpu *vcpu)
{
	if (!(vcpu->arch.vpa.update_pending ||
	      vcpu->arch.slb_shadow.update_pending ||
	      vcpu->arch.dtl.update_pending))
		return;

	spin_lock(&vcpu->arch.vpa_update_lock);
	if (vcpu->arch.vpa.update_pending) {
		kvmppc_update_vpa(vcpu, &vcpu->arch.vpa);
		if (vcpu->arch.vpa.pinned_addr)
			init_vpa(vcpu, vcpu->arch.vpa.pinned_addr);
	}
	if (vcpu->arch.dtl.update_pending) {
		kvmppc_update_vpa(vcpu, &vcpu->arch.dtl);
		vcpu->arch.dtl_ptr = vcpu->arch.dtl.pinned_addr;
		vcpu->arch.dtl_index = 0;
	}
	if (vcpu->arch.slb_shadow.update_pending)
		kvmppc_update_vpa(vcpu, &vcpu->arch.slb_shadow);
	spin_unlock(&vcpu->arch.vpa_update_lock);
}

/*
 * Return the accumulated stolen time for the vcore up until `now'.
 * The caller should hold the vcore lock.
 */
static u64 vcore_stolen_time(struct kvmppc_vcore *vc, u64 now)
{
	u64 p;
	unsigned long flags;

	spin_lock_irqsave(&vc->stoltb_lock, flags);
	p = vc->stolen_tb;
	if (vc->vcore_state != VCORE_INACTIVE &&
	    vc->preempt_tb != TB_NIL)
		p += now - vc->preempt_tb;
	spin_unlock_irqrestore(&vc->stoltb_lock, flags);
	return p;
}

static void kvmppc_create_dtl_entry(struct kvm_vcpu *vcpu,
				    struct kvmppc_vcore *vc)
{
	struct dtl_entry *dt;
	struct lppaca *vpa;
	unsigned long stolen;
	unsigned long core_stolen;
	u64 now;
	unsigned long flags;

	dt = vcpu->arch.dtl_ptr;
	vpa = vcpu->arch.vpa.pinned_addr;
	now = mftb();
	core_stolen = vcore_stolen_time(vc, now);
	stolen = core_stolen - vcpu->arch.stolen_logged;
	vcpu->arch.stolen_logged = core_stolen;
	spin_lock_irqsave(&vcpu->arch.tbacct_lock, flags);
	stolen += vcpu->arch.busy_stolen;
	vcpu->arch.busy_stolen = 0;
	spin_unlock_irqrestore(&vcpu->arch.tbacct_lock, flags);
	if (!dt || !vpa)
		return;
	memset(dt, 0, sizeof(struct dtl_entry));
	dt->dispatch_reason = 7;
	dt->processor_id = cpu_to_be16(vc->pcpu + vcpu->arch.ptid);
	dt->timebase = cpu_to_be64(now + vc->tb_offset);
	dt->enqueue_to_dispatch_time = cpu_to_be32(stolen);
	dt->srr0 = cpu_to_be64(kvmppc_get_pc(vcpu));
	dt->srr1 = cpu_to_be64(vcpu->arch.shregs.msr);
	++dt;
	if (dt == vcpu->arch.dtl.pinned_end)
		dt = vcpu->arch.dtl.pinned_addr;
	vcpu->arch.dtl_ptr = dt;
	/* order writing *dt vs. writing vpa->dtl_idx */
	smp_wmb();
	vpa->dtl_idx = cpu_to_be64(++vcpu->arch.dtl_index);
	vcpu->arch.dtl.dirty = true;
}

/* See if there is a doorbell interrupt pending for a vcpu */
static bool kvmppc_doorbell_pending(struct kvm_vcpu *vcpu)
{
	int thr;
	struct kvmppc_vcore *vc;

	if (vcpu->arch.doorbell_request)
		return true;
	/*
	 * Ensure that the read of vcore->dpdes comes after the read
	 * of vcpu->doorbell_request.  This barrier matches the
	 * smp_wmb() in kvmppc_guest_entry_inject().
	 */
	smp_rmb();
	vc = vcpu->arch.vcore;
	thr = vcpu->vcpu_id - vc->first_vcpuid;
	return !!(vc->dpdes & (1 << thr));
}

static bool kvmppc_power8_compatible(struct kvm_vcpu *vcpu)
{
	if (vcpu->arch.vcore->arch_compat >= PVR_ARCH_207)
		return true;
	if ((!vcpu->arch.vcore->arch_compat) &&
	    cpu_has_feature(CPU_FTR_ARCH_207S))
		return true;
	return false;
}

static int kvmppc_h_set_mode(struct kvm_vcpu *vcpu, unsigned long mflags,
			     unsigned long resource, unsigned long value1,
			     unsigned long value2)
{
	switch (resource) {
	case H_SET_MODE_RESOURCE_SET_CIABR:
		if (!kvmppc_power8_compatible(vcpu))
			return H_P2;
		if (value2)
			return H_P4;
		if (mflags)
			return H_UNSUPPORTED_FLAG_START;
		/* Guests can't breakpoint the hypervisor */
		if ((value1 & CIABR_PRIV) == CIABR_PRIV_HYPER)
			return H_P3;
		vcpu->arch.ciabr  = value1;
		return H_SUCCESS;
	case H_SET_MODE_RESOURCE_SET_DAWR:
		if (!kvmppc_power8_compatible(vcpu))
			return H_P2;
		if (!ppc_breakpoint_available())
			return H_P2;
		if (mflags)
			return H_UNSUPPORTED_FLAG_START;
		if (value2 & DABRX_HYP)
			return H_P4;
		vcpu->arch.dawr  = value1;
		vcpu->arch.dawrx = value2;
		return H_SUCCESS;
	case H_SET_MODE_RESOURCE_ADDR_TRANS_MODE:
		/* KVM does not support mflags=2 (AIL=2) */
		if (mflags != 0 && mflags != 3)
			return H_UNSUPPORTED_FLAG_START;
		return H_TOO_HARD;
	default:
		return H_TOO_HARD;
	}
}

/* Copy guest memory in place - must reside within a single memslot */
static int kvmppc_copy_guest(struct kvm *kvm, gpa_t to, gpa_t from,
				  unsigned long len)
{
	struct kvm_memory_slot *to_memslot = NULL;
	struct kvm_memory_slot *from_memslot = NULL;
	unsigned long to_addr, from_addr;
	int r;

	/* Get HPA for from address */
	from_memslot = gfn_to_memslot(kvm, from >> PAGE_SHIFT);
	if (!from_memslot)
		return -EFAULT;
	if ((from + len) >= ((from_memslot->base_gfn + from_memslot->npages)
			     << PAGE_SHIFT))
		return -EINVAL;
	from_addr = gfn_to_hva_memslot(from_memslot, from >> PAGE_SHIFT);
	if (kvm_is_error_hva(from_addr))
		return -EFAULT;
	from_addr |= (from & (PAGE_SIZE - 1));

	/* Get HPA for to address */
	to_memslot = gfn_to_memslot(kvm, to >> PAGE_SHIFT);
	if (!to_memslot)
		return -EFAULT;
	if ((to + len) >= ((to_memslot->base_gfn + to_memslot->npages)
			   << PAGE_SHIFT))
		return -EINVAL;
	to_addr = gfn_to_hva_memslot(to_memslot, to >> PAGE_SHIFT);
	if (kvm_is_error_hva(to_addr))
		return -EFAULT;
	to_addr |= (to & (PAGE_SIZE - 1));

	/* Perform copy */
	r = raw_copy_in_user((void __user *)to_addr, (void __user *)from_addr,
			     len);
	if (r)
		return -EFAULT;
	mark_page_dirty(kvm, to >> PAGE_SHIFT);
	return 0;
}

static long kvmppc_h_page_init(struct kvm_vcpu *vcpu, unsigned long flags,
			       unsigned long dest, unsigned long src)
{
	u64 pg_sz = SZ_4K;		/* 4K page size */
	u64 pg_mask = SZ_4K - 1;
	int ret;

	/* Check for invalid flags (H_PAGE_SET_LOANED covers all CMO flags) */
	if (flags & ~(H_ICACHE_INVALIDATE | H_ICACHE_SYNCHRONIZE |
		      H_ZERO_PAGE | H_COPY_PAGE | H_PAGE_SET_LOANED))
		return H_PARAMETER;

	/* dest (and src if copy_page flag set) must be page aligned */
	if ((dest & pg_mask) || ((flags & H_COPY_PAGE) && (src & pg_mask)))
		return H_PARAMETER;

	/* zero and/or copy the page as determined by the flags */
	if (flags & H_COPY_PAGE) {
		ret = kvmppc_copy_guest(vcpu->kvm, dest, src, pg_sz);
		if (ret < 0)
			return H_PARAMETER;
	} else if (flags & H_ZERO_PAGE) {
		ret = kvm_clear_guest(vcpu->kvm, dest, pg_sz);
		if (ret < 0)
			return H_PARAMETER;
	}

	/* We can ignore the remaining flags */

	return H_SUCCESS;
}

static int kvm_arch_vcpu_yield_to(struct kvm_vcpu *target)
{
	struct kvmppc_vcore *vcore = target->arch.vcore;

	/*
	 * We expect to have been called by the real mode handler
	 * (kvmppc_rm_h_confer()) which would have directly returned
	 * H_SUCCESS if the source vcore wasn't idle (e.g. if it may
	 * have useful work to do and should not confer) so we don't
	 * recheck that here.
	 */

	spin_lock(&vcore->lock);
	if (target->arch.state == KVMPPC_VCPU_RUNNABLE &&
	    vcore->vcore_state != VCORE_INACTIVE &&
	    vcore->runner)
		target = vcore->runner;
	spin_unlock(&vcore->lock);

	return kvm_vcpu_yield_to(target);
}

static int kvmppc_get_yield_count(struct kvm_vcpu *vcpu)
{
	int yield_count = 0;
	struct lppaca *lppaca;

	spin_lock(&vcpu->arch.vpa_update_lock);
	lppaca = (struct lppaca *)vcpu->arch.vpa.pinned_addr;
	if (lppaca)
		yield_count = be32_to_cpu(lppaca->yield_count);
	spin_unlock(&vcpu->arch.vpa_update_lock);
	return yield_count;
}

int kvmppc_pseries_do_hcall(struct kvm_vcpu *vcpu)
{
	unsigned long req = kvmppc_get_gpr(vcpu, 3);
	unsigned long target, ret = H_SUCCESS;
	int yield_count;
	struct kvm_vcpu *tvcpu;
	int idx, rc;

	if (req <= MAX_HCALL_OPCODE &&
	    !test_bit(req/4, vcpu->kvm->arch.enabled_hcalls))
		return RESUME_HOST;

	switch (req) {
	case H_CEDE:
		break;
	case H_PROD:
		target = kvmppc_get_gpr(vcpu, 4);
		tvcpu = kvmppc_find_vcpu(vcpu->kvm, target);
		if (!tvcpu) {
			ret = H_PARAMETER;
			break;
		}
		tvcpu->arch.prodded = 1;
		smp_mb();
		if (tvcpu->arch.ceded)
			kvmppc_fast_vcpu_kick_hv(tvcpu);
		break;
	case H_CONFER:
		target = kvmppc_get_gpr(vcpu, 4);
		if (target == -1)
			break;
		tvcpu = kvmppc_find_vcpu(vcpu->kvm, target);
		if (!tvcpu) {
			ret = H_PARAMETER;
			break;
		}
		yield_count = kvmppc_get_gpr(vcpu, 5);
		if (kvmppc_get_yield_count(tvcpu) != yield_count)
			break;
		kvm_arch_vcpu_yield_to(tvcpu);
		break;
	case H_REGISTER_VPA:
		ret = do_h_register_vpa(vcpu, kvmppc_get_gpr(vcpu, 4),
					kvmppc_get_gpr(vcpu, 5),
					kvmppc_get_gpr(vcpu, 6));
		break;
	case H_RTAS:
		if (list_empty(&vcpu->kvm->arch.rtas_tokens))
			return RESUME_HOST;

		idx = srcu_read_lock(&vcpu->kvm->srcu);
		rc = kvmppc_rtas_hcall(vcpu);
		srcu_read_unlock(&vcpu->kvm->srcu, idx);

		if (rc == -ENOENT)
			return RESUME_HOST;
		else if (rc == 0)
			break;

		/* Send the error out to userspace via KVM_RUN */
		return rc;
	case H_LOGICAL_CI_LOAD:
		ret = kvmppc_h_logical_ci_load(vcpu);
		if (ret == H_TOO_HARD)
			return RESUME_HOST;
		break;
	case H_LOGICAL_CI_STORE:
		ret = kvmppc_h_logical_ci_store(vcpu);
		if (ret == H_TOO_HARD)
			return RESUME_HOST;
		break;
	case H_SET_MODE:
		ret = kvmppc_h_set_mode(vcpu, kvmppc_get_gpr(vcpu, 4),
					kvmppc_get_gpr(vcpu, 5),
					kvmppc_get_gpr(vcpu, 6),
					kvmppc_get_gpr(vcpu, 7));
		if (ret == H_TOO_HARD)
			return RESUME_HOST;
		break;
	case H_XIRR:
	case H_CPPR:
	case H_EOI:
	case H_IPI:
	case H_IPOLL:
	case H_XIRR_X:
		if (kvmppc_xics_enabled(vcpu)) {
			if (xics_on_xive()) {
				ret = H_NOT_AVAILABLE;
				return RESUME_GUEST;
			}
			ret = kvmppc_xics_hcall(vcpu, req);
			break;
		}
		return RESUME_HOST;
	case H_SET_DABR:
		ret = kvmppc_h_set_dabr(vcpu, kvmppc_get_gpr(vcpu, 4));
		break;
	case H_SET_XDABR:
		ret = kvmppc_h_set_xdabr(vcpu, kvmppc_get_gpr(vcpu, 4),
						kvmppc_get_gpr(vcpu, 5));
		break;
#ifdef CONFIG_SPAPR_TCE_IOMMU
	case H_GET_TCE:
		ret = kvmppc_h_get_tce(vcpu, kvmppc_get_gpr(vcpu, 4),
						kvmppc_get_gpr(vcpu, 5));
		if (ret == H_TOO_HARD)
			return RESUME_HOST;
		break;
	case H_PUT_TCE:
		ret = kvmppc_h_put_tce(vcpu, kvmppc_get_gpr(vcpu, 4),
						kvmppc_get_gpr(vcpu, 5),
						kvmppc_get_gpr(vcpu, 6));
		if (ret == H_TOO_HARD)
			return RESUME_HOST;
		break;
	case H_PUT_TCE_INDIRECT:
		ret = kvmppc_h_put_tce_indirect(vcpu, kvmppc_get_gpr(vcpu, 4),
						kvmppc_get_gpr(vcpu, 5),
						kvmppc_get_gpr(vcpu, 6),
						kvmppc_get_gpr(vcpu, 7));
		if (ret == H_TOO_HARD)
			return RESUME_HOST;
		break;
	case H_STUFF_TCE:
		ret = kvmppc_h_stuff_tce(vcpu, kvmppc_get_gpr(vcpu, 4),
						kvmppc_get_gpr(vcpu, 5),
						kvmppc_get_gpr(vcpu, 6),
						kvmppc_get_gpr(vcpu, 7));
		if (ret == H_TOO_HARD)
			return RESUME_HOST;
		break;
#endif
	case H_RANDOM:
		if (!powernv_get_random_long(&vcpu->arch.regs.gpr[4]))
			ret = H_HARDWARE;
		break;

	case H_SET_PARTITION_TABLE:
		ret = H_FUNCTION;
		if (nesting_enabled(vcpu->kvm))
			ret = kvmhv_set_partition_table(vcpu);
		break;
	case H_ENTER_NESTED:
		ret = H_FUNCTION;
		if (!nesting_enabled(vcpu->kvm))
			break;
		ret = kvmhv_enter_nested_guest(vcpu);
		if (ret == H_INTERRUPT) {
			kvmppc_set_gpr(vcpu, 3, 0);
			vcpu->arch.hcall_needed = 0;
			return -EINTR;
		} else if (ret == H_TOO_HARD) {
			kvmppc_set_gpr(vcpu, 3, 0);
			vcpu->arch.hcall_needed = 0;
			return RESUME_HOST;
		}
		break;
	case H_TLB_INVALIDATE:
		ret = H_FUNCTION;
		if (nesting_enabled(vcpu->kvm))
			ret = kvmhv_do_nested_tlbie(vcpu);
		break;
	case H_COPY_TOFROM_GUEST:
		ret = H_FUNCTION;
		if (nesting_enabled(vcpu->kvm))
			ret = kvmhv_copy_tofrom_guest_nested(vcpu);
		break;
	case H_PAGE_INIT:
		ret = kvmppc_h_page_init(vcpu, kvmppc_get_gpr(vcpu, 4),
					 kvmppc_get_gpr(vcpu, 5),
					 kvmppc_get_gpr(vcpu, 6));
		break;
	case H_SVM_PAGE_IN:
		ret = kvmppc_h_svm_page_in(vcpu->kvm,
					   kvmppc_get_gpr(vcpu, 4),
					   kvmppc_get_gpr(vcpu, 5),
					   kvmppc_get_gpr(vcpu, 6));
		break;
	case H_SVM_PAGE_OUT:
		ret = kvmppc_h_svm_page_out(vcpu->kvm,
					    kvmppc_get_gpr(vcpu, 4),
					    kvmppc_get_gpr(vcpu, 5),
					    kvmppc_get_gpr(vcpu, 6));
		break;
	case H_SVM_INIT_START:
		ret = kvmppc_h_svm_init_start(vcpu->kvm);
		break;
	case H_SVM_INIT_DONE:
		ret = kvmppc_h_svm_init_done(vcpu->kvm);
		break;

	default:
		return RESUME_HOST;
	}
	kvmppc_set_gpr(vcpu, 3, ret);
	vcpu->arch.hcall_needed = 0;
	return RESUME_GUEST;
}

/*
 * Handle H_CEDE in the nested virtualization case where we haven't
 * called the real-mode hcall handlers in book3s_hv_rmhandlers.S.
 * This has to be done early, not in kvmppc_pseries_do_hcall(), so
 * that the cede logic in kvmppc_run_single_vcpu() works properly.
 */
static void kvmppc_nested_cede(struct kvm_vcpu *vcpu)
{
	vcpu->arch.shregs.msr |= MSR_EE;
	vcpu->arch.ceded = 1;
	smp_mb();
	if (vcpu->arch.prodded) {
		vcpu->arch.prodded = 0;
		smp_mb();
		vcpu->arch.ceded = 0;
	}
}

static int kvmppc_hcall_impl_hv(unsigned long cmd)
{
	switch (cmd) {
	case H_CEDE:
	case H_PROD:
	case H_CONFER:
	case H_REGISTER_VPA:
	case H_SET_MODE:
	case H_LOGICAL_CI_LOAD:
	case H_LOGICAL_CI_STORE:
#ifdef CONFIG_KVM_XICS
	case H_XIRR:
	case H_CPPR:
	case H_EOI:
	case H_IPI:
	case H_IPOLL:
	case H_XIRR_X:
#endif
	case H_PAGE_INIT:
		return 1;
	}

	/* See if it's in the real-mode table */
	return kvmppc_hcall_impl_hv_realmode(cmd);
}

static int kvmppc_emulate_debug_inst(struct kvm_run *run,
					struct kvm_vcpu *vcpu)
{
	u32 last_inst;

	if (kvmppc_get_last_inst(vcpu, INST_GENERIC, &last_inst) !=
					EMULATE_DONE) {
		/*
		 * Fetch failed, so return to guest and
		 * try executing it again.
		 */
		return RESUME_GUEST;
	}

	if (last_inst == KVMPPC_INST_SW_BREAKPOINT) {
		run->exit_reason = KVM_EXIT_DEBUG;
		run->debug.arch.address = kvmppc_get_pc(vcpu);
		return RESUME_HOST;
	} else {
		kvmppc_core_queue_program(vcpu, SRR1_PROGILL);
		return RESUME_GUEST;
	}
}

static void do_nothing(void *x)
{
}

static unsigned long kvmppc_read_dpdes(struct kvm_vcpu *vcpu)
{
	int thr, cpu, pcpu, nthreads;
	struct kvm_vcpu *v;
	unsigned long dpdes;

	nthreads = vcpu->kvm->arch.emul_smt_mode;
	dpdes = 0;
	cpu = vcpu->vcpu_id & ~(nthreads - 1);
	for (thr = 0; thr < nthreads; ++thr, ++cpu) {
		v = kvmppc_find_vcpu(vcpu->kvm, cpu);
		if (!v)
			continue;
		/*
		 * If the vcpu is currently running on a physical cpu thread,
		 * interrupt it in order to pull it out of the guest briefly,
		 * which will update its vcore->dpdes value.
		 */
		pcpu = READ_ONCE(v->cpu);
		if (pcpu >= 0)
			smp_call_function_single(pcpu, do_nothing, NULL, 1);
		if (kvmppc_doorbell_pending(v))
			dpdes |= 1 << thr;
	}
	return dpdes;
}

/*
 * On POWER9, emulate doorbell-related instructions in order to
 * give the guest the illusion of running on a multi-threaded core.
 * The instructions emulated are msgsndp, msgclrp, mfspr TIR,
 * and mfspr DPDES.
 */
static int kvmppc_emulate_doorbell_instr(struct kvm_vcpu *vcpu)
{
	u32 inst, rb, thr;
	unsigned long arg;
	struct kvm *kvm = vcpu->kvm;
	struct kvm_vcpu *tvcpu;

	if (kvmppc_get_last_inst(vcpu, INST_GENERIC, &inst) != EMULATE_DONE)
		return RESUME_GUEST;
	if (get_op(inst) != 31)
		return EMULATE_FAIL;
	rb = get_rb(inst);
	thr = vcpu->vcpu_id & (kvm->arch.emul_smt_mode - 1);
	switch (get_xop(inst)) {
	case OP_31_XOP_MSGSNDP:
		arg = kvmppc_get_gpr(vcpu, rb);
		if (((arg >> 27) & 0xf) != PPC_DBELL_SERVER)
			break;
		arg &= 0x3f;
		if (arg >= kvm->arch.emul_smt_mode)
			break;
		tvcpu = kvmppc_find_vcpu(kvm, vcpu->vcpu_id - thr + arg);
		if (!tvcpu)
			break;
		if (!tvcpu->arch.doorbell_request) {
			tvcpu->arch.doorbell_request = 1;
			kvmppc_fast_vcpu_kick_hv(tvcpu);
		}
		break;
	case OP_31_XOP_MSGCLRP:
		arg = kvmppc_get_gpr(vcpu, rb);
		if (((arg >> 27) & 0xf) != PPC_DBELL_SERVER)
			break;
		vcpu->arch.vcore->dpdes = 0;
		vcpu->arch.doorbell_request = 0;
		break;
	case OP_31_XOP_MFSPR:
		switch (get_sprn(inst)) {
		case SPRN_TIR:
			arg = thr;
			break;
		case SPRN_DPDES:
			arg = kvmppc_read_dpdes(vcpu);
			break;
		default:
			return EMULATE_FAIL;
		}
		kvmppc_set_gpr(vcpu, get_rt(inst), arg);
		break;
	default:
		return EMULATE_FAIL;
	}
	kvmppc_set_pc(vcpu, kvmppc_get_pc(vcpu) + 4);
	return RESUME_GUEST;
}

static int kvmppc_handle_exit_hv(struct kvm_run *run, struct kvm_vcpu *vcpu,
				 struct task_struct *tsk)
{
	int r = RESUME_HOST;

	vcpu->stat.sum_exits++;

	/*
	 * This can happen if an interrupt occurs in the last stages
	 * of guest entry or the first stages of guest exit (i.e. after
	 * setting paca->kvm_hstate.in_guest to KVM_GUEST_MODE_GUEST_HV
	 * and before setting it to KVM_GUEST_MODE_HOST_HV).
	 * That can happen due to a bug, or due to a machine check
	 * occurring at just the wrong time.
	 */
	if (vcpu->arch.shregs.msr & MSR_HV) {
		printk(KERN_EMERG "KVM trap in HV mode!\n");
		printk(KERN_EMERG "trap=0x%x | pc=0x%lx | msr=0x%llx\n",
			vcpu->arch.trap, kvmppc_get_pc(vcpu),
			vcpu->arch.shregs.msr);
		kvmppc_dump_regs(vcpu);
		run->exit_reason = KVM_EXIT_INTERNAL_ERROR;
		run->hw.hardware_exit_reason = vcpu->arch.trap;
		return RESUME_HOST;
	}
	run->exit_reason = KVM_EXIT_UNKNOWN;
	run->ready_for_interrupt_injection = 1;
	switch (vcpu->arch.trap) {
	/* We're good on these - the host merely wanted to get our attention */
	case BOOK3S_INTERRUPT_HV_DECREMENTER:
		vcpu->stat.dec_exits++;
		r = RESUME_GUEST;
		break;
	case BOOK3S_INTERRUPT_EXTERNAL:
	case BOOK3S_INTERRUPT_H_DOORBELL:
	case BOOK3S_INTERRUPT_H_VIRT:
		vcpu->stat.ext_intr_exits++;
		r = RESUME_GUEST;
		break;
	/* SR/HMI/PMI are HV interrupts that host has handled. Resume guest.*/
	case BOOK3S_INTERRUPT_HMI:
	case BOOK3S_INTERRUPT_PERFMON:
	case BOOK3S_INTERRUPT_SYSTEM_RESET:
		r = RESUME_GUEST;
		break;
	case BOOK3S_INTERRUPT_MACHINE_CHECK:
		/* Print the MCE event to host console. */
		machine_check_print_event_info(&vcpu->arch.mce_evt, false, true);

		/*
		 * If the guest can do FWNMI, exit to userspace so it can
		 * deliver a FWNMI to the guest.
		 * Otherwise we synthesize a machine check for the guest
		 * so that it knows that the machine check occurred.
		 */
		if (!vcpu->kvm->arch.fwnmi_enabled) {
			ulong flags = vcpu->arch.shregs.msr & 0x083c0000;
			kvmppc_core_queue_machine_check(vcpu, flags);
			r = RESUME_GUEST;
			break;
		}

		/* Exit to guest with KVM_EXIT_NMI as exit reason */
		run->exit_reason = KVM_EXIT_NMI;
		run->hw.hardware_exit_reason = vcpu->arch.trap;
		/* Clear out the old NMI status from run->flags */
		run->flags &= ~KVM_RUN_PPC_NMI_DISP_MASK;
		/* Now set the NMI status */
		if (vcpu->arch.mce_evt.disposition == MCE_DISPOSITION_RECOVERED)
			run->flags |= KVM_RUN_PPC_NMI_DISP_FULLY_RECOV;
		else
			run->flags |= KVM_RUN_PPC_NMI_DISP_NOT_RECOV;

		r = RESUME_HOST;
		break;
	case BOOK3S_INTERRUPT_PROGRAM:
	{
		ulong flags;
		/*
		 * Normally program interrupts are delivered directly
		 * to the guest by the hardware, but we can get here
		 * as a result of a hypervisor emulation interrupt
		 * (e40) getting turned into a 700 by BML RTAS.
		 */
		flags = vcpu->arch.shregs.msr & 0x1f0000ull;
		kvmppc_core_queue_program(vcpu, flags);
		r = RESUME_GUEST;
		break;
	}
	case BOOK3S_INTERRUPT_SYSCALL:
	{
		/* hcall - punt to userspace */
		int i;

		/* hypercall with MSR_PR has already been handled in rmode,
		 * and never reaches here.
		 */

		run->papr_hcall.nr = kvmppc_get_gpr(vcpu, 3);
		for (i = 0; i < 9; ++i)
			run->papr_hcall.args[i] = kvmppc_get_gpr(vcpu, 4 + i);
		run->exit_reason = KVM_EXIT_PAPR_HCALL;
		vcpu->arch.hcall_needed = 1;
		r = RESUME_HOST;
		break;
	}
	/*
	 * We get these next two if the guest accesses a page which it thinks
	 * it has mapped but which is not actually present, either because
	 * it is for an emulated I/O device or because the corresonding
	 * host page has been paged out.  Any other HDSI/HISI interrupts
	 * have been handled already.
	 */
	case BOOK3S_INTERRUPT_H_DATA_STORAGE:
		r = RESUME_PAGE_FAULT;
		break;
	case BOOK3S_INTERRUPT_H_INST_STORAGE:
		vcpu->arch.fault_dar = kvmppc_get_pc(vcpu);
		vcpu->arch.fault_dsisr = vcpu->arch.shregs.msr &
			DSISR_SRR1_MATCH_64S;
		if (vcpu->arch.shregs.msr & HSRR1_HISI_WRITE)
			vcpu->arch.fault_dsisr |= DSISR_ISSTORE;
		r = RESUME_PAGE_FAULT;
		break;
	/*
	 * This occurs if the guest executes an illegal instruction.
	 * If the guest debug is disabled, generate a program interrupt
	 * to the guest. If guest debug is enabled, we need to check
	 * whether the instruction is a software breakpoint instruction.
	 * Accordingly return to Guest or Host.
	 */
	case BOOK3S_INTERRUPT_H_EMUL_ASSIST:
		if (vcpu->arch.emul_inst != KVM_INST_FETCH_FAILED)
			vcpu->arch.last_inst = kvmppc_need_byteswap(vcpu) ?
				swab32(vcpu->arch.emul_inst) :
				vcpu->arch.emul_inst;
		if (vcpu->guest_debug & KVM_GUESTDBG_USE_SW_BP) {
			r = kvmppc_emulate_debug_inst(run, vcpu);
		} else {
			kvmppc_core_queue_program(vcpu, SRR1_PROGILL);
			r = RESUME_GUEST;
		}
		break;
	/*
	 * This occurs if the guest (kernel or userspace), does something that
	 * is prohibited by HFSCR.
	 * On POWER9, this could be a doorbell instruction that we need
	 * to emulate.
	 * Otherwise, we just generate a program interrupt to the guest.
	 */
	case BOOK3S_INTERRUPT_H_FAC_UNAVAIL:
		r = EMULATE_FAIL;
		if (((vcpu->arch.hfscr >> 56) == FSCR_MSGP_LG) &&
		    cpu_has_feature(CPU_FTR_ARCH_300))
			r = kvmppc_emulate_doorbell_instr(vcpu);
		if (r == EMULATE_FAIL) {
			kvmppc_core_queue_program(vcpu, SRR1_PROGILL);
			r = RESUME_GUEST;
		}
		break;

#ifdef CONFIG_PPC_TRANSACTIONAL_MEM
	case BOOK3S_INTERRUPT_HV_SOFTPATCH:
		/*
		 * This occurs for various TM-related instructions that
		 * we need to emulate on POWER9 DD2.2.  We have already
		 * handled the cases where the guest was in real-suspend
		 * mode and was transitioning to transactional state.
		 */
		r = kvmhv_p9_tm_emulation(vcpu);
		break;
#endif

	case BOOK3S_INTERRUPT_HV_RM_HARD:
		r = RESUME_PASSTHROUGH;
		break;
	default:
		kvmppc_dump_regs(vcpu);
		printk(KERN_EMERG "trap=0x%x | pc=0x%lx | msr=0x%llx\n",
			vcpu->arch.trap, kvmppc_get_pc(vcpu),
			vcpu->arch.shregs.msr);
		run->hw.hardware_exit_reason = vcpu->arch.trap;
		r = RESUME_HOST;
		break;
	}

	return r;
}

static int kvmppc_handle_nested_exit(struct kvm_run *run, struct kvm_vcpu *vcpu)
{
	int r;
	int srcu_idx;

	vcpu->stat.sum_exits++;

	/*
	 * This can happen if an interrupt occurs in the last stages
	 * of guest entry or the first stages of guest exit (i.e. after
	 * setting paca->kvm_hstate.in_guest to KVM_GUEST_MODE_GUEST_HV
	 * and before setting it to KVM_GUEST_MODE_HOST_HV).
	 * That can happen due to a bug, or due to a machine check
	 * occurring at just the wrong time.
	 */
	if (vcpu->arch.shregs.msr & MSR_HV) {
		pr_emerg("KVM trap in HV mode while nested!\n");
		pr_emerg("trap=0x%x | pc=0x%lx | msr=0x%llx\n",
			 vcpu->arch.trap, kvmppc_get_pc(vcpu),
			 vcpu->arch.shregs.msr);
		kvmppc_dump_regs(vcpu);
		return RESUME_HOST;
	}
	switch (vcpu->arch.trap) {
	/* We're good on these - the host merely wanted to get our attention */
	case BOOK3S_INTERRUPT_HV_DECREMENTER:
		vcpu->stat.dec_exits++;
		r = RESUME_GUEST;
		break;
	case BOOK3S_INTERRUPT_EXTERNAL:
		vcpu->stat.ext_intr_exits++;
		r = RESUME_HOST;
		break;
	case BOOK3S_INTERRUPT_H_DOORBELL:
	case BOOK3S_INTERRUPT_H_VIRT:
		vcpu->stat.ext_intr_exits++;
		r = RESUME_GUEST;
		break;
	/* SR/HMI/PMI are HV interrupts that host has handled. Resume guest.*/
	case BOOK3S_INTERRUPT_HMI:
	case BOOK3S_INTERRUPT_PERFMON:
	case BOOK3S_INTERRUPT_SYSTEM_RESET:
		r = RESUME_GUEST;
		break;
	case BOOK3S_INTERRUPT_MACHINE_CHECK:
		/* Pass the machine check to the L1 guest */
		r = RESUME_HOST;
		/* Print the MCE event to host console. */
		machine_check_print_event_info(&vcpu->arch.mce_evt, false, true);
		break;
	/*
	 * We get these next two if the guest accesses a page which it thinks
	 * it has mapped but which is not actually present, either because
	 * it is for an emulated I/O device or because the corresonding
	 * host page has been paged out.
	 */
	case BOOK3S_INTERRUPT_H_DATA_STORAGE:
		srcu_idx = srcu_read_lock(&vcpu->kvm->srcu);
		r = kvmhv_nested_page_fault(run, vcpu);
		srcu_read_unlock(&vcpu->kvm->srcu, srcu_idx);
		break;
	case BOOK3S_INTERRUPT_H_INST_STORAGE:
		vcpu->arch.fault_dar = kvmppc_get_pc(vcpu);
		vcpu->arch.fault_dsisr = kvmppc_get_msr(vcpu) &
					 DSISR_SRR1_MATCH_64S;
		if (vcpu->arch.shregs.msr & HSRR1_HISI_WRITE)
			vcpu->arch.fault_dsisr |= DSISR_ISSTORE;
		srcu_idx = srcu_read_lock(&vcpu->kvm->srcu);
		r = kvmhv_nested_page_fault(run, vcpu);
		srcu_read_unlock(&vcpu->kvm->srcu, srcu_idx);
		break;

#ifdef CONFIG_PPC_TRANSACTIONAL_MEM
	case BOOK3S_INTERRUPT_HV_SOFTPATCH:
		/*
		 * This occurs for various TM-related instructions that
		 * we need to emulate on POWER9 DD2.2.  We have already
		 * handled the cases where the guest was in real-suspend
		 * mode and was transitioning to transactional state.
		 */
		r = kvmhv_p9_tm_emulation(vcpu);
		break;
#endif

	case BOOK3S_INTERRUPT_HV_RM_HARD:
		vcpu->arch.trap = 0;
		r = RESUME_GUEST;
		if (!xics_on_xive())
			kvmppc_xics_rm_complete(vcpu, 0);
		break;
	default:
		r = RESUME_HOST;
		break;
	}

	return r;
}

static int kvm_arch_vcpu_ioctl_get_sregs_hv(struct kvm_vcpu *vcpu,
					    struct kvm_sregs *sregs)
{
	int i;

	memset(sregs, 0, sizeof(struct kvm_sregs));
	sregs->pvr = vcpu->arch.pvr;
	for (i = 0; i < vcpu->arch.slb_max; i++) {
		sregs->u.s.ppc64.slb[i].slbe = vcpu->arch.slb[i].orige;
		sregs->u.s.ppc64.slb[i].slbv = vcpu->arch.slb[i].origv;
	}

	return 0;
}

static int kvm_arch_vcpu_ioctl_set_sregs_hv(struct kvm_vcpu *vcpu,
					    struct kvm_sregs *sregs)
{
	int i, j;

	/* Only accept the same PVR as the host's, since we can't spoof it */
	if (sregs->pvr != vcpu->arch.pvr)
		return -EINVAL;

	j = 0;
	for (i = 0; i < vcpu->arch.slb_nr; i++) {
		if (sregs->u.s.ppc64.slb[i].slbe & SLB_ESID_V) {
			vcpu->arch.slb[j].orige = sregs->u.s.ppc64.slb[i].slbe;
			vcpu->arch.slb[j].origv = sregs->u.s.ppc64.slb[i].slbv;
			++j;
		}
	}
	vcpu->arch.slb_max = j;

	return 0;
}

static void kvmppc_set_lpcr(struct kvm_vcpu *vcpu, u64 new_lpcr,
		bool preserve_top32)
{
	struct kvm *kvm = vcpu->kvm;
	struct kvmppc_vcore *vc = vcpu->arch.vcore;
	u64 mask;

	spin_lock(&vc->lock);
	/*
	 * If ILE (interrupt little-endian) has changed, update the
	 * MSR_LE bit in the intr_msr for each vcpu in this vcore.
	 */
	if ((new_lpcr & LPCR_ILE) != (vc->lpcr & LPCR_ILE)) {
		struct kvm_vcpu *vcpu;
		int i;

		kvm_for_each_vcpu(i, vcpu, kvm) {
			if (vcpu->arch.vcore != vc)
				continue;
			if (new_lpcr & LPCR_ILE)
				vcpu->arch.intr_msr |= MSR_LE;
			else
				vcpu->arch.intr_msr &= ~MSR_LE;
		}
	}

	/*
	 * Userspace can only modify DPFD (default prefetch depth),
	 * ILE (interrupt little-endian) and TC (translation control).
	 * On POWER8 and POWER9 userspace can also modify AIL (alt. interrupt loc.).
	 */
	mask = LPCR_DPFD | LPCR_ILE | LPCR_TC;
	if (cpu_has_feature(CPU_FTR_ARCH_207S))
		mask |= LPCR_AIL;
	/*
	 * On POWER9, allow userspace to enable large decrementer for the
	 * guest, whether or not the host has it enabled.
	 */
	if (cpu_has_feature(CPU_FTR_ARCH_300))
		mask |= LPCR_LD;

	/* Broken 32-bit version of LPCR must not clear top bits */
	if (preserve_top32)
		mask &= 0xFFFFFFFF;
	vc->lpcr = (vc->lpcr & ~mask) | (new_lpcr & mask);
	spin_unlock(&vc->lock);
}

static int kvmppc_get_one_reg_hv(struct kvm_vcpu *vcpu, u64 id,
				 union kvmppc_one_reg *val)
{
	int r = 0;
	long int i;

	switch (id) {
	case KVM_REG_PPC_DEBUG_INST:
		*val = get_reg_val(id, KVMPPC_INST_SW_BREAKPOINT);
		break;
	case KVM_REG_PPC_HIOR:
		*val = get_reg_val(id, 0);
		break;
	case KVM_REG_PPC_DABR:
		*val = get_reg_val(id, vcpu->arch.dabr);
		break;
	case KVM_REG_PPC_DABRX:
		*val = get_reg_val(id, vcpu->arch.dabrx);
		break;
	case KVM_REG_PPC_DSCR:
		*val = get_reg_val(id, vcpu->arch.dscr);
		break;
	case KVM_REG_PPC_PURR:
		*val = get_reg_val(id, vcpu->arch.purr);
		break;
	case KVM_REG_PPC_SPURR:
		*val = get_reg_val(id, vcpu->arch.spurr);
		break;
	case KVM_REG_PPC_AMR:
		*val = get_reg_val(id, vcpu->arch.amr);
		break;
	case KVM_REG_PPC_UAMOR:
		*val = get_reg_val(id, vcpu->arch.uamor);
		break;
	case KVM_REG_PPC_MMCR0 ... KVM_REG_PPC_MMCRS:
		i = id - KVM_REG_PPC_MMCR0;
		*val = get_reg_val(id, vcpu->arch.mmcr[i]);
		break;
	case KVM_REG_PPC_PMC1 ... KVM_REG_PPC_PMC8:
		i = id - KVM_REG_PPC_PMC1;
		*val = get_reg_val(id, vcpu->arch.pmc[i]);
		break;
	case KVM_REG_PPC_SPMC1 ... KVM_REG_PPC_SPMC2:
		i = id - KVM_REG_PPC_SPMC1;
		*val = get_reg_val(id, vcpu->arch.spmc[i]);
		break;
	case KVM_REG_PPC_SIAR:
		*val = get_reg_val(id, vcpu->arch.siar);
		break;
	case KVM_REG_PPC_SDAR:
		*val = get_reg_val(id, vcpu->arch.sdar);
		break;
	case KVM_REG_PPC_SIER:
		*val = get_reg_val(id, vcpu->arch.sier);
		break;
	case KVM_REG_PPC_IAMR:
		*val = get_reg_val(id, vcpu->arch.iamr);
		break;
	case KVM_REG_PPC_PSPB:
		*val = get_reg_val(id, vcpu->arch.pspb);
		break;
	case KVM_REG_PPC_DPDES:
		/*
		 * On POWER9, where we are emulating msgsndp etc.,
		 * we return 1 bit for each vcpu, which can come from
		 * either vcore->dpdes or doorbell_request.
		 * On POWER8, doorbell_request is 0.
		 */
		*val = get_reg_val(id, vcpu->arch.vcore->dpdes |
				   vcpu->arch.doorbell_request);
		break;
	case KVM_REG_PPC_VTB:
		*val = get_reg_val(id, vcpu->arch.vcore->vtb);
		break;
	case KVM_REG_PPC_DAWR:
		*val = get_reg_val(id, vcpu->arch.dawr);
		break;
	case KVM_REG_PPC_DAWRX:
		*val = get_reg_val(id, vcpu->arch.dawrx);
		break;
	case KVM_REG_PPC_CIABR:
		*val = get_reg_val(id, vcpu->arch.ciabr);
		break;
	case KVM_REG_PPC_CSIGR:
		*val = get_reg_val(id, vcpu->arch.csigr);
		break;
	case KVM_REG_PPC_TACR:
		*val = get_reg_val(id, vcpu->arch.tacr);
		break;
	case KVM_REG_PPC_TCSCR:
		*val = get_reg_val(id, vcpu->arch.tcscr);
		break;
	case KVM_REG_PPC_PID:
		*val = get_reg_val(id, vcpu->arch.pid);
		break;
	case KVM_REG_PPC_ACOP:
		*val = get_reg_val(id, vcpu->arch.acop);
		break;
	case KVM_REG_PPC_WORT:
		*val = get_reg_val(id, vcpu->arch.wort);
		break;
	case KVM_REG_PPC_TIDR:
		*val = get_reg_val(id, vcpu->arch.tid);
		break;
	case KVM_REG_PPC_PSSCR:
		*val = get_reg_val(id, vcpu->arch.psscr);
		break;
	case KVM_REG_PPC_VPA_ADDR:
		spin_lock(&vcpu->arch.vpa_update_lock);
		*val = get_reg_val(id, vcpu->arch.vpa.next_gpa);
		spin_unlock(&vcpu->arch.vpa_update_lock);
		break;
	case KVM_REG_PPC_VPA_SLB:
		spin_lock(&vcpu->arch.vpa_update_lock);
		val->vpaval.addr = vcpu->arch.slb_shadow.next_gpa;
		val->vpaval.length = vcpu->arch.slb_shadow.len;
		spin_unlock(&vcpu->arch.vpa_update_lock);
		break;
	case KVM_REG_PPC_VPA_DTL:
		spin_lock(&vcpu->arch.vpa_update_lock);
		val->vpaval.addr = vcpu->arch.dtl.next_gpa;
		val->vpaval.length = vcpu->arch.dtl.len;
		spin_unlock(&vcpu->arch.vpa_update_lock);
		break;
	case KVM_REG_PPC_TB_OFFSET:
		*val = get_reg_val(id, vcpu->arch.vcore->tb_offset);
		break;
	case KVM_REG_PPC_LPCR:
	case KVM_REG_PPC_LPCR_64:
		*val = get_reg_val(id, vcpu->arch.vcore->lpcr);
		break;
	case KVM_REG_PPC_PPR:
		*val = get_reg_val(id, vcpu->arch.ppr);
		break;
#ifdef CONFIG_PPC_TRANSACTIONAL_MEM
	case KVM_REG_PPC_TFHAR:
		*val = get_reg_val(id, vcpu->arch.tfhar);
		break;
	case KVM_REG_PPC_TFIAR:
		*val = get_reg_val(id, vcpu->arch.tfiar);
		break;
	case KVM_REG_PPC_TEXASR:
		*val = get_reg_val(id, vcpu->arch.texasr);
		break;
	case KVM_REG_PPC_TM_GPR0 ... KVM_REG_PPC_TM_GPR31:
		i = id - KVM_REG_PPC_TM_GPR0;
		*val = get_reg_val(id, vcpu->arch.gpr_tm[i]);
		break;
	case KVM_REG_PPC_TM_VSR0 ... KVM_REG_PPC_TM_VSR63:
	{
		int j;
		i = id - KVM_REG_PPC_TM_VSR0;
		if (i < 32)
			for (j = 0; j < TS_FPRWIDTH; j++)
				val->vsxval[j] = vcpu->arch.fp_tm.fpr[i][j];
		else {
			if (cpu_has_feature(CPU_FTR_ALTIVEC))
				val->vval = vcpu->arch.vr_tm.vr[i-32];
			else
				r = -ENXIO;
		}
		break;
	}
	case KVM_REG_PPC_TM_CR:
		*val = get_reg_val(id, vcpu->arch.cr_tm);
		break;
	case KVM_REG_PPC_TM_XER:
		*val = get_reg_val(id, vcpu->arch.xer_tm);
		break;
	case KVM_REG_PPC_TM_LR:
		*val = get_reg_val(id, vcpu->arch.lr_tm);
		break;
	case KVM_REG_PPC_TM_CTR:
		*val = get_reg_val(id, vcpu->arch.ctr_tm);
		break;
	case KVM_REG_PPC_TM_FPSCR:
		*val = get_reg_val(id, vcpu->arch.fp_tm.fpscr);
		break;
	case KVM_REG_PPC_TM_AMR:
		*val = get_reg_val(id, vcpu->arch.amr_tm);
		break;
	case KVM_REG_PPC_TM_PPR:
		*val = get_reg_val(id, vcpu->arch.ppr_tm);
		break;
	case KVM_REG_PPC_TM_VRSAVE:
		*val = get_reg_val(id, vcpu->arch.vrsave_tm);
		break;
	case KVM_REG_PPC_TM_VSCR:
		if (cpu_has_feature(CPU_FTR_ALTIVEC))
			*val = get_reg_val(id, vcpu->arch.vr_tm.vscr.u[3]);
		else
			r = -ENXIO;
		break;
	case KVM_REG_PPC_TM_DSCR:
		*val = get_reg_val(id, vcpu->arch.dscr_tm);
		break;
	case KVM_REG_PPC_TM_TAR:
		*val = get_reg_val(id, vcpu->arch.tar_tm);
		break;
#endif
	case KVM_REG_PPC_ARCH_COMPAT:
		*val = get_reg_val(id, vcpu->arch.vcore->arch_compat);
		break;
	case KVM_REG_PPC_DEC_EXPIRY:
		*val = get_reg_val(id, vcpu->arch.dec_expires +
				   vcpu->arch.vcore->tb_offset);
		break;
	case KVM_REG_PPC_ONLINE:
		*val = get_reg_val(id, vcpu->arch.online);
		break;
	case KVM_REG_PPC_PTCR:
		*val = get_reg_val(id, vcpu->kvm->arch.l1_ptcr);
		break;
	default:
		r = -EINVAL;
		break;
	}

	return r;
}

static int kvmppc_set_one_reg_hv(struct kvm_vcpu *vcpu, u64 id,
				 union kvmppc_one_reg *val)
{
	int r = 0;
	long int i;
	unsigned long addr, len;

	switch (id) {
	case KVM_REG_PPC_HIOR:
		/* Only allow this to be set to zero */
		if (set_reg_val(id, *val))
			r = -EINVAL;
		break;
	case KVM_REG_PPC_DABR:
		vcpu->arch.dabr = set_reg_val(id, *val);
		break;
	case KVM_REG_PPC_DABRX:
		vcpu->arch.dabrx = set_reg_val(id, *val) & ~DABRX_HYP;
		break;
	case KVM_REG_PPC_DSCR:
		vcpu->arch.dscr = set_reg_val(id, *val);
		break;
	case KVM_REG_PPC_PURR:
		vcpu->arch.purr = set_reg_val(id, *val);
		break;
	case KVM_REG_PPC_SPURR:
		vcpu->arch.spurr = set_reg_val(id, *val);
		break;
	case KVM_REG_PPC_AMR:
		vcpu->arch.amr = set_reg_val(id, *val);
		break;
	case KVM_REG_PPC_UAMOR:
		vcpu->arch.uamor = set_reg_val(id, *val);
		break;
	case KVM_REG_PPC_MMCR0 ... KVM_REG_PPC_MMCRS:
		i = id - KVM_REG_PPC_MMCR0;
		vcpu->arch.mmcr[i] = set_reg_val(id, *val);
		break;
	case KVM_REG_PPC_PMC1 ... KVM_REG_PPC_PMC8:
		i = id - KVM_REG_PPC_PMC1;
		vcpu->arch.pmc[i] = set_reg_val(id, *val);
		break;
	case KVM_REG_PPC_SPMC1 ... KVM_REG_PPC_SPMC2:
		i = id - KVM_REG_PPC_SPMC1;
		vcpu->arch.spmc[i] = set_reg_val(id, *val);
		break;
	case KVM_REG_PPC_SIAR:
		vcpu->arch.siar = set_reg_val(id, *val);
		break;
	case KVM_REG_PPC_SDAR:
		vcpu->arch.sdar = set_reg_val(id, *val);
		break;
	case KVM_REG_PPC_SIER:
		vcpu->arch.sier = set_reg_val(id, *val);
		break;
	case KVM_REG_PPC_IAMR:
		vcpu->arch.iamr = set_reg_val(id, *val);
		break;
	case KVM_REG_PPC_PSPB:
		vcpu->arch.pspb = set_reg_val(id, *val);
		break;
	case KVM_REG_PPC_DPDES:
		vcpu->arch.vcore->dpdes = set_reg_val(id, *val);
		break;
	case KVM_REG_PPC_VTB:
		vcpu->arch.vcore->vtb = set_reg_val(id, *val);
		break;
	case KVM_REG_PPC_DAWR:
		vcpu->arch.dawr = set_reg_val(id, *val);
		break;
	case KVM_REG_PPC_DAWRX:
		vcpu->arch.dawrx = set_reg_val(id, *val) & ~DAWRX_HYP;
		break;
	case KVM_REG_PPC_CIABR:
		vcpu->arch.ciabr = set_reg_val(id, *val);
		/* Don't allow setting breakpoints in hypervisor code */
		if ((vcpu->arch.ciabr & CIABR_PRIV) == CIABR_PRIV_HYPER)
			vcpu->arch.ciabr &= ~CIABR_PRIV;	/* disable */
		break;
	case KVM_REG_PPC_CSIGR:
		vcpu->arch.csigr = set_reg_val(id, *val);
		break;
	case KVM_REG_PPC_TACR:
		vcpu->arch.tacr = set_reg_val(id, *val);
		break;
	case KVM_REG_PPC_TCSCR:
		vcpu->arch.tcscr = set_reg_val(id, *val);
		break;
	case KVM_REG_PPC_PID:
		vcpu->arch.pid = set_reg_val(id, *val);
		break;
	case KVM_REG_PPC_ACOP:
		vcpu->arch.acop = set_reg_val(id, *val);
		break;
	case KVM_REG_PPC_WORT:
		vcpu->arch.wort = set_reg_val(id, *val);
		break;
	case KVM_REG_PPC_TIDR:
		vcpu->arch.tid = set_reg_val(id, *val);
		break;
	case KVM_REG_PPC_PSSCR:
		vcpu->arch.psscr = set_reg_val(id, *val) & PSSCR_GUEST_VIS;
		break;
	case KVM_REG_PPC_VPA_ADDR:
		addr = set_reg_val(id, *val);
		r = -EINVAL;
		if (!addr && (vcpu->arch.slb_shadow.next_gpa ||
			      vcpu->arch.dtl.next_gpa))
			break;
		r = set_vpa(vcpu, &vcpu->arch.vpa, addr, sizeof(struct lppaca));
		break;
	case KVM_REG_PPC_VPA_SLB:
		addr = val->vpaval.addr;
		len = val->vpaval.length;
		r = -EINVAL;
		if (addr && !vcpu->arch.vpa.next_gpa)
			break;
		r = set_vpa(vcpu, &vcpu->arch.slb_shadow, addr, len);
		break;
	case KVM_REG_PPC_VPA_DTL:
		addr = val->vpaval.addr;
		len = val->vpaval.length;
		r = -EINVAL;
		if (addr && (len < sizeof(struct dtl_entry) ||
			     !vcpu->arch.vpa.next_gpa))
			break;
		len -= len % sizeof(struct dtl_entry);
		r = set_vpa(vcpu, &vcpu->arch.dtl, addr, len);
		break;
	case KVM_REG_PPC_TB_OFFSET:
		/* round up to multiple of 2^24 */
		vcpu->arch.vcore->tb_offset =
			ALIGN(set_reg_val(id, *val), 1UL << 24);
		break;
	case KVM_REG_PPC_LPCR:
		kvmppc_set_lpcr(vcpu, set_reg_val(id, *val), true);
		break;
	case KVM_REG_PPC_LPCR_64:
		kvmppc_set_lpcr(vcpu, set_reg_val(id, *val), false);
		break;
	case KVM_REG_PPC_PPR:
		vcpu->arch.ppr = set_reg_val(id, *val);
		break;
#ifdef CONFIG_PPC_TRANSACTIONAL_MEM
	case KVM_REG_PPC_TFHAR:
		vcpu->arch.tfhar = set_reg_val(id, *val);
		break;
	case KVM_REG_PPC_TFIAR:
		vcpu->arch.tfiar = set_reg_val(id, *val);
		break;
	case KVM_REG_PPC_TEXASR:
		vcpu->arch.texasr = set_reg_val(id, *val);
		break;
	case KVM_REG_PPC_TM_GPR0 ... KVM_REG_PPC_TM_GPR31:
		i = id - KVM_REG_PPC_TM_GPR0;
		vcpu->arch.gpr_tm[i] = set_reg_val(id, *val);
		break;
	case KVM_REG_PPC_TM_VSR0 ... KVM_REG_PPC_TM_VSR63:
	{
		int j;
		i = id - KVM_REG_PPC_TM_VSR0;
		if (i < 32)
			for (j = 0; j < TS_FPRWIDTH; j++)
				vcpu->arch.fp_tm.fpr[i][j] = val->vsxval[j];
		else
			if (cpu_has_feature(CPU_FTR_ALTIVEC))
				vcpu->arch.vr_tm.vr[i-32] = val->vval;
			else
				r = -ENXIO;
		break;
	}
	case KVM_REG_PPC_TM_CR:
		vcpu->arch.cr_tm = set_reg_val(id, *val);
		break;
	case KVM_REG_PPC_TM_XER:
		vcpu->arch.xer_tm = set_reg_val(id, *val);
		break;
	case KVM_REG_PPC_TM_LR:
		vcpu->arch.lr_tm = set_reg_val(id, *val);
		break;
	case KVM_REG_PPC_TM_CTR:
		vcpu->arch.ctr_tm = set_reg_val(id, *val);
		break;
	case KVM_REG_PPC_TM_FPSCR:
		vcpu->arch.fp_tm.fpscr = set_reg_val(id, *val);
		break;
	case KVM_REG_PPC_TM_AMR:
		vcpu->arch.amr_tm = set_reg_val(id, *val);
		break;
	case KVM_REG_PPC_TM_PPR:
		vcpu->arch.ppr_tm = set_reg_val(id, *val);
		break;
	case KVM_REG_PPC_TM_VRSAVE:
		vcpu->arch.vrsave_tm = set_reg_val(id, *val);
		break;
	case KVM_REG_PPC_TM_VSCR:
		if (cpu_has_feature(CPU_FTR_ALTIVEC))
			vcpu->arch.vr.vscr.u[3] = set_reg_val(id, *val);
		else
			r = - ENXIO;
		break;
	case KVM_REG_PPC_TM_DSCR:
		vcpu->arch.dscr_tm = set_reg_val(id, *val);
		break;
	case KVM_REG_PPC_TM_TAR:
		vcpu->arch.tar_tm = set_reg_val(id, *val);
		break;
#endif
	case KVM_REG_PPC_ARCH_COMPAT:
		r = kvmppc_set_arch_compat(vcpu, set_reg_val(id, *val));
		break;
	case KVM_REG_PPC_DEC_EXPIRY:
		vcpu->arch.dec_expires = set_reg_val(id, *val) -
			vcpu->arch.vcore->tb_offset;
		break;
	case KVM_REG_PPC_ONLINE:
		i = set_reg_val(id, *val);
		if (i && !vcpu->arch.online)
			atomic_inc(&vcpu->arch.vcore->online_count);
		else if (!i && vcpu->arch.online)
			atomic_dec(&vcpu->arch.vcore->online_count);
		vcpu->arch.online = i;
		break;
	case KVM_REG_PPC_PTCR:
		vcpu->kvm->arch.l1_ptcr = set_reg_val(id, *val);
		break;
	default:
		r = -EINVAL;
		break;
	}

	return r;
}

/*
 * On POWER9, threads are independent and can be in different partitions.
 * Therefore we consider each thread to be a subcore.
 * There is a restriction that all threads have to be in the same
 * MMU mode (radix or HPT), unfortunately, but since we only support
 * HPT guests on a HPT host so far, that isn't an impediment yet.
 */
static int threads_per_vcore(struct kvm *kvm)
{
	if (kvm->arch.threads_indep)
		return 1;
	return threads_per_subcore;
}

static struct kvmppc_vcore *kvmppc_vcore_create(struct kvm *kvm, int id)
{
	struct kvmppc_vcore *vcore;

	vcore = kzalloc(sizeof(struct kvmppc_vcore), GFP_KERNEL);

	if (vcore == NULL)
		return NULL;

	spin_lock_init(&vcore->lock);
	spin_lock_init(&vcore->stoltb_lock);
	init_swait_queue_head(&vcore->wq);
	vcore->preempt_tb = TB_NIL;
	vcore->lpcr = kvm->arch.lpcr;
	vcore->first_vcpuid = id;
	vcore->kvm = kvm;
	INIT_LIST_HEAD(&vcore->preempt_list);

	return vcore;
}

#ifdef CONFIG_KVM_BOOK3S_HV_EXIT_TIMING
static struct debugfs_timings_element {
	const char *name;
	size_t offset;
} timings[] = {
	{"rm_entry",	offsetof(struct kvm_vcpu, arch.rm_entry)},
	{"rm_intr",	offsetof(struct kvm_vcpu, arch.rm_intr)},
	{"rm_exit",	offsetof(struct kvm_vcpu, arch.rm_exit)},
	{"guest",	offsetof(struct kvm_vcpu, arch.guest_time)},
	{"cede",	offsetof(struct kvm_vcpu, arch.cede_time)},
};

#define N_TIMINGS	(ARRAY_SIZE(timings))

struct debugfs_timings_state {
	struct kvm_vcpu	*vcpu;
	unsigned int	buflen;
	char		buf[N_TIMINGS * 100];
};

static int debugfs_timings_open(struct inode *inode, struct file *file)
{
	struct kvm_vcpu *vcpu = inode->i_private;
	struct debugfs_timings_state *p;

	p = kzalloc(sizeof(*p), GFP_KERNEL);
	if (!p)
		return -ENOMEM;

	kvm_get_kvm(vcpu->kvm);
	p->vcpu = vcpu;
	file->private_data = p;

	return nonseekable_open(inode, file);
}

static int debugfs_timings_release(struct inode *inode, struct file *file)
{
	struct debugfs_timings_state *p = file->private_data;

	kvm_put_kvm(p->vcpu->kvm);
	kfree(p);
	return 0;
}

static ssize_t debugfs_timings_read(struct file *file, char __user *buf,
				    size_t len, loff_t *ppos)
{
	struct debugfs_timings_state *p = file->private_data;
	struct kvm_vcpu *vcpu = p->vcpu;
	char *s, *buf_end;
	struct kvmhv_tb_accumulator tb;
	u64 count;
	loff_t pos;
	ssize_t n;
	int i, loops;
	bool ok;

	if (!p->buflen) {
		s = p->buf;
		buf_end = s + sizeof(p->buf);
		for (i = 0; i < N_TIMINGS; ++i) {
			struct kvmhv_tb_accumulator *acc;

			acc = (struct kvmhv_tb_accumulator *)
				((unsigned long)vcpu + timings[i].offset);
			ok = false;
			for (loops = 0; loops < 1000; ++loops) {
				count = acc->seqcount;
				if (!(count & 1)) {
					smp_rmb();
					tb = *acc;
					smp_rmb();
					if (count == acc->seqcount) {
						ok = true;
						break;
					}
				}
				udelay(1);
			}
			if (!ok)
				snprintf(s, buf_end - s, "%s: stuck\n",
					timings[i].name);
			else
				snprintf(s, buf_end - s,
					"%s: %llu %llu %llu %llu\n",
					timings[i].name, count / 2,
					tb_to_ns(tb.tb_total),
					tb_to_ns(tb.tb_min),
					tb_to_ns(tb.tb_max));
			s += strlen(s);
		}
		p->buflen = s - p->buf;
	}

	pos = *ppos;
	if (pos >= p->buflen)
		return 0;
	if (len > p->buflen - pos)
		len = p->buflen - pos;
	n = copy_to_user(buf, p->buf + pos, len);
	if (n) {
		if (n == len)
			return -EFAULT;
		len -= n;
	}
	*ppos = pos + len;
	return len;
}

static ssize_t debugfs_timings_write(struct file *file, const char __user *buf,
				     size_t len, loff_t *ppos)
{
	return -EACCES;
}

static const struct file_operations debugfs_timings_ops = {
	.owner	 = THIS_MODULE,
	.open	 = debugfs_timings_open,
	.release = debugfs_timings_release,
	.read	 = debugfs_timings_read,
	.write	 = debugfs_timings_write,
	.llseek	 = generic_file_llseek,
};

/* Create a debugfs directory for the vcpu */
static void debugfs_vcpu_init(struct kvm_vcpu *vcpu, unsigned int id)
{
	char buf[16];
	struct kvm *kvm = vcpu->kvm;

	snprintf(buf, sizeof(buf), "vcpu%u", id);
	if (IS_ERR_OR_NULL(kvm->arch.debugfs_dir))
		return;
	vcpu->arch.debugfs_dir = debugfs_create_dir(buf, kvm->arch.debugfs_dir);
	if (IS_ERR_OR_NULL(vcpu->arch.debugfs_dir))
		return;
	vcpu->arch.debugfs_timings =
		debugfs_create_file("timings", 0444, vcpu->arch.debugfs_dir,
				    vcpu, &debugfs_timings_ops);
}

#else /* CONFIG_KVM_BOOK3S_HV_EXIT_TIMING */
static void debugfs_vcpu_init(struct kvm_vcpu *vcpu, unsigned int id)
{
}
#endif /* CONFIG_KVM_BOOK3S_HV_EXIT_TIMING */

static struct kvm_vcpu *kvmppc_core_vcpu_create_hv(struct kvm *kvm,
						   unsigned int id)
{
	struct kvm_vcpu *vcpu;
	int err;
	int core;
	struct kvmppc_vcore *vcore;

	err = -ENOMEM;
	vcpu = kmem_cache_zalloc(kvm_vcpu_cache, GFP_KERNEL);
	if (!vcpu)
		goto out;

	err = kvm_vcpu_init(vcpu, kvm, id);
	if (err)
		goto free_vcpu;

	vcpu->arch.shared = &vcpu->arch.shregs;
#ifdef CONFIG_KVM_BOOK3S_PR_POSSIBLE
	/*
	 * The shared struct is never shared on HV,
	 * so we can always use host endianness
	 */
#ifdef __BIG_ENDIAN__
	vcpu->arch.shared_big_endian = true;
#else
	vcpu->arch.shared_big_endian = false;
#endif
#endif
	vcpu->arch.mmcr[0] = MMCR0_FC;
	vcpu->arch.ctrl = CTRL_RUNLATCH;
	/* default to host PVR, since we can't spoof it */
	kvmppc_set_pvr_hv(vcpu, mfspr(SPRN_PVR));
	spin_lock_init(&vcpu->arch.vpa_update_lock);
	spin_lock_init(&vcpu->arch.tbacct_lock);
	vcpu->arch.busy_preempt = TB_NIL;
	vcpu->arch.intr_msr = MSR_SF | MSR_ME;

	/*
	 * Set the default HFSCR for the guest from the host value.
	 * This value is only used on POWER9.
	 * On POWER9, we want to virtualize the doorbell facility, so we
	 * don't set the HFSCR_MSGP bit, and that causes those instructions
	 * to trap and then we emulate them.
	 */
	vcpu->arch.hfscr = HFSCR_TAR | HFSCR_EBB | HFSCR_PM | HFSCR_BHRB |
		HFSCR_DSCR | HFSCR_VECVSX | HFSCR_FP;
	if (cpu_has_feature(CPU_FTR_HVMODE)) {
		vcpu->arch.hfscr &= mfspr(SPRN_HFSCR);
		if (cpu_has_feature(CPU_FTR_P9_TM_HV_ASSIST))
			vcpu->arch.hfscr |= HFSCR_TM;
	}
	if (cpu_has_feature(CPU_FTR_TM_COMP))
		vcpu->arch.hfscr |= HFSCR_TM;

	kvmppc_mmu_book3s_hv_init(vcpu);

	vcpu->arch.state = KVMPPC_VCPU_NOTREADY;

	init_waitqueue_head(&vcpu->arch.cpu_run);

	mutex_lock(&kvm->lock);
	vcore = NULL;
	err = -EINVAL;
	if (cpu_has_feature(CPU_FTR_ARCH_300)) {
		if (id >= (KVM_MAX_VCPUS * kvm->arch.emul_smt_mode)) {
			pr_devel("KVM: VCPU ID too high\n");
			core = KVM_MAX_VCORES;
		} else {
			BUG_ON(kvm->arch.smt_mode != 1);
			core = kvmppc_pack_vcpu_id(kvm, id);
		}
	} else {
		core = id / kvm->arch.smt_mode;
	}
	if (core < KVM_MAX_VCORES) {
		vcore = kvm->arch.vcores[core];
		if (vcore && cpu_has_feature(CPU_FTR_ARCH_300)) {
			pr_devel("KVM: collision on id %u", id);
			vcore = NULL;
		} else if (!vcore) {
			/*
			 * Take mmu_setup_lock for mutual exclusion
			 * with kvmppc_update_lpcr().
			 */
			err = -ENOMEM;
			vcore = kvmppc_vcore_create(kvm,
					id & ~(kvm->arch.smt_mode - 1));
			mutex_lock(&kvm->arch.mmu_setup_lock);
			kvm->arch.vcores[core] = vcore;
			kvm->arch.online_vcores++;
			mutex_unlock(&kvm->arch.mmu_setup_lock);
		}
	}
	mutex_unlock(&kvm->lock);

	if (!vcore)
		goto uninit_vcpu;

	spin_lock(&vcore->lock);
	++vcore->num_threads;
	spin_unlock(&vcore->lock);
	vcpu->arch.vcore = vcore;
	vcpu->arch.ptid = vcpu->vcpu_id - vcore->first_vcpuid;
	vcpu->arch.thread_cpu = -1;
	vcpu->arch.prev_cpu = -1;

	vcpu->arch.cpu_type = KVM_CPU_3S_64;
	kvmppc_sanity_check(vcpu);

	debugfs_vcpu_init(vcpu, id);

	return vcpu;

uninit_vcpu:
	kvm_vcpu_uninit(vcpu);
free_vcpu:
	kmem_cache_free(kvm_vcpu_cache, vcpu);
out:
	return ERR_PTR(err);
}

static int kvmhv_set_smt_mode(struct kvm *kvm, unsigned long smt_mode,
			      unsigned long flags)
{
	int err;
	int esmt = 0;

	if (flags)
		return -EINVAL;
	if (smt_mode > MAX_SMT_THREADS || !is_power_of_2(smt_mode))
		return -EINVAL;
	if (!cpu_has_feature(CPU_FTR_ARCH_300)) {
		/*
		 * On POWER8 (or POWER7), the threading mode is "strict",
		 * so we pack smt_mode vcpus per vcore.
		 */
		if (smt_mode > threads_per_subcore)
			return -EINVAL;
	} else {
		/*
		 * On POWER9, the threading mode is "loose",
		 * so each vcpu gets its own vcore.
		 */
		esmt = smt_mode;
		smt_mode = 1;
	}
	mutex_lock(&kvm->lock);
	err = -EBUSY;
	if (!kvm->arch.online_vcores) {
		kvm->arch.smt_mode = smt_mode;
		kvm->arch.emul_smt_mode = esmt;
		err = 0;
	}
	mutex_unlock(&kvm->lock);

	return err;
}

static void unpin_vpa(struct kvm *kvm, struct kvmppc_vpa *vpa)
{
	if (vpa->pinned_addr)
		kvmppc_unpin_guest_page(kvm, vpa->pinned_addr, vpa->gpa,
					vpa->dirty);
}

static void kvmppc_core_vcpu_free_hv(struct kvm_vcpu *vcpu)
{
	spin_lock(&vcpu->arch.vpa_update_lock);
	unpin_vpa(vcpu->kvm, &vcpu->arch.dtl);
	unpin_vpa(vcpu->kvm, &vcpu->arch.slb_shadow);
	unpin_vpa(vcpu->kvm, &vcpu->arch.vpa);
	spin_unlock(&vcpu->arch.vpa_update_lock);
	kvm_vcpu_uninit(vcpu);
	kmem_cache_free(kvm_vcpu_cache, vcpu);
}

static int kvmppc_core_check_requests_hv(struct kvm_vcpu *vcpu)
{
	/* Indicate we want to get back into the guest */
	return 1;
}

static void kvmppc_set_timer(struct kvm_vcpu *vcpu)
{
	unsigned long dec_nsec, now;

	now = get_tb();
	if (now > vcpu->arch.dec_expires) {
		/* decrementer has already gone negative */
		kvmppc_core_queue_dec(vcpu);
		kvmppc_core_prepare_to_enter(vcpu);
		return;
	}
	dec_nsec = tb_to_ns(vcpu->arch.dec_expires - now);
	hrtimer_start(&vcpu->arch.dec_timer, dec_nsec, HRTIMER_MODE_REL);
	vcpu->arch.timer_running = 1;
}

extern int __kvmppc_vcore_entry(void);

static void kvmppc_remove_runnable(struct kvmppc_vcore *vc,
				   struct kvm_vcpu *vcpu)
{
	u64 now;

	if (vcpu->arch.state != KVMPPC_VCPU_RUNNABLE)
		return;
	spin_lock_irq(&vcpu->arch.tbacct_lock);
	now = mftb();
	vcpu->arch.busy_stolen += vcore_stolen_time(vc, now) -
		vcpu->arch.stolen_logged;
	vcpu->arch.busy_preempt = now;
	vcpu->arch.state = KVMPPC_VCPU_BUSY_IN_HOST;
	spin_unlock_irq(&vcpu->arch.tbacct_lock);
	--vc->n_runnable;
	WRITE_ONCE(vc->runnable_threads[vcpu->arch.ptid], NULL);
}

static int kvmppc_grab_hwthread(int cpu)
{
	struct paca_struct *tpaca;
	long timeout = 10000;

	tpaca = paca_ptrs[cpu];

	/* Ensure the thread won't go into the kernel if it wakes */
	tpaca->kvm_hstate.kvm_vcpu = NULL;
	tpaca->kvm_hstate.kvm_vcore = NULL;
	tpaca->kvm_hstate.napping = 0;
	smp_wmb();
	tpaca->kvm_hstate.hwthread_req = 1;

	/*
	 * If the thread is already executing in the kernel (e.g. handling
	 * a stray interrupt), wait for it to get back to nap mode.
	 * The smp_mb() is to ensure that our setting of hwthread_req
	 * is visible before we look at hwthread_state, so if this
	 * races with the code at system_reset_pSeries and the thread
	 * misses our setting of hwthread_req, we are sure to see its
	 * setting of hwthread_state, and vice versa.
	 */
	smp_mb();
	while (tpaca->kvm_hstate.hwthread_state == KVM_HWTHREAD_IN_KERNEL) {
		if (--timeout <= 0) {
			pr_err("KVM: couldn't grab cpu %d\n", cpu);
			return -EBUSY;
		}
		udelay(1);
	}
	return 0;
}

static void kvmppc_release_hwthread(int cpu)
{
	struct paca_struct *tpaca;

	tpaca = paca_ptrs[cpu];
	tpaca->kvm_hstate.hwthread_req = 0;
	tpaca->kvm_hstate.kvm_vcpu = NULL;
	tpaca->kvm_hstate.kvm_vcore = NULL;
	tpaca->kvm_hstate.kvm_split_mode = NULL;
}

static void radix_flush_cpu(struct kvm *kvm, int cpu, struct kvm_vcpu *vcpu)
{
	struct kvm_nested_guest *nested = vcpu->arch.nested;
	cpumask_t *cpu_in_guest;
	int i;

	cpu = cpu_first_thread_sibling(cpu);
	if (nested) {
		cpumask_set_cpu(cpu, &nested->need_tlb_flush);
		cpu_in_guest = &nested->cpu_in_guest;
	} else {
		cpumask_set_cpu(cpu, &kvm->arch.need_tlb_flush);
		cpu_in_guest = &kvm->arch.cpu_in_guest;
	}
	/*
	 * Make sure setting of bit in need_tlb_flush precedes
	 * testing of cpu_in_guest bits.  The matching barrier on
	 * the other side is the first smp_mb() in kvmppc_run_core().
	 */
	smp_mb();
	for (i = 0; i < threads_per_core; ++i)
		if (cpumask_test_cpu(cpu + i, cpu_in_guest))
			smp_call_function_single(cpu + i, do_nothing, NULL, 1);
}

static void kvmppc_prepare_radix_vcpu(struct kvm_vcpu *vcpu, int pcpu)
{
	struct kvm_nested_guest *nested = vcpu->arch.nested;
	struct kvm *kvm = vcpu->kvm;
	int prev_cpu;

	if (!cpu_has_feature(CPU_FTR_HVMODE))
		return;

	if (nested)
		prev_cpu = nested->prev_cpu[vcpu->arch.nested_vcpu_id];
	else
		prev_cpu = vcpu->arch.prev_cpu;

	/*
	 * With radix, the guest can do TLB invalidations itself,
	 * and it could choose to use the local form (tlbiel) if
	 * it is invalidating a translation that has only ever been
	 * used on one vcpu.  However, that doesn't mean it has
	 * only ever been used on one physical cpu, since vcpus
	 * can move around between pcpus.  To cope with this, when
	 * a vcpu moves from one pcpu to another, we need to tell
	 * any vcpus running on the same core as this vcpu previously
	 * ran to flush the TLB.  The TLB is shared between threads,
	 * so we use a single bit in .need_tlb_flush for all 4 threads.
	 */
	if (prev_cpu != pcpu) {
		if (prev_cpu >= 0 &&
		    cpu_first_thread_sibling(prev_cpu) !=
		    cpu_first_thread_sibling(pcpu))
			radix_flush_cpu(kvm, prev_cpu, vcpu);
		if (nested)
			nested->prev_cpu[vcpu->arch.nested_vcpu_id] = pcpu;
		else
			vcpu->arch.prev_cpu = pcpu;
	}
}

static void kvmppc_start_thread(struct kvm_vcpu *vcpu, struct kvmppc_vcore *vc)
{
	int cpu;
	struct paca_struct *tpaca;
	struct kvm *kvm = vc->kvm;

	cpu = vc->pcpu;
	if (vcpu) {
		if (vcpu->arch.timer_running) {
			hrtimer_try_to_cancel(&vcpu->arch.dec_timer);
			vcpu->arch.timer_running = 0;
		}
		cpu += vcpu->arch.ptid;
		vcpu->cpu = vc->pcpu;
		vcpu->arch.thread_cpu = cpu;
		cpumask_set_cpu(cpu, &kvm->arch.cpu_in_guest);
	}
	tpaca = paca_ptrs[cpu];
	tpaca->kvm_hstate.kvm_vcpu = vcpu;
	tpaca->kvm_hstate.ptid = cpu - vc->pcpu;
	tpaca->kvm_hstate.fake_suspend = 0;
	/* Order stores to hstate.kvm_vcpu etc. before store to kvm_vcore */
	smp_wmb();
	tpaca->kvm_hstate.kvm_vcore = vc;
	if (cpu != smp_processor_id())
		kvmppc_ipi_thread(cpu);
}

static void kvmppc_wait_for_nap(int n_threads)
{
	int cpu = smp_processor_id();
	int i, loops;

	if (n_threads <= 1)
		return;
	for (loops = 0; loops < 1000000; ++loops) {
		/*
		 * Check if all threads are finished.
		 * We set the vcore pointer when starting a thread
		 * and the thread clears it when finished, so we look
		 * for any threads that still have a non-NULL vcore ptr.
		 */
		for (i = 1; i < n_threads; ++i)
			if (paca_ptrs[cpu + i]->kvm_hstate.kvm_vcore)
				break;
		if (i == n_threads) {
			HMT_medium();
			return;
		}
		HMT_low();
	}
	HMT_medium();
	for (i = 1; i < n_threads; ++i)
		if (paca_ptrs[cpu + i]->kvm_hstate.kvm_vcore)
			pr_err("KVM: CPU %d seems to be stuck\n", cpu + i);
}

/*
 * Check that we are on thread 0 and that any other threads in
 * this core are off-line.  Then grab the threads so they can't
 * enter the kernel.
 */
static int on_primary_thread(void)
{
	int cpu = smp_processor_id();
	int thr;

	/* Are we on a primary subcore? */
	if (cpu_thread_in_subcore(cpu))
		return 0;

	thr = 0;
	while (++thr < threads_per_subcore)
		if (cpu_online(cpu + thr))
			return 0;

	/* Grab all hw threads so they can't go into the kernel */
	for (thr = 1; thr < threads_per_subcore; ++thr) {
		if (kvmppc_grab_hwthread(cpu + thr)) {
			/* Couldn't grab one; let the others go */
			do {
				kvmppc_release_hwthread(cpu + thr);
			} while (--thr > 0);
			return 0;
		}
	}
	return 1;
}

/*
 * A list of virtual cores for each physical CPU.
 * These are vcores that could run but their runner VCPU tasks are
 * (or may be) preempted.
 */
struct preempted_vcore_list {
	struct list_head	list;
	spinlock_t		lock;
};

static DEFINE_PER_CPU(struct preempted_vcore_list, preempted_vcores);

static void init_vcore_lists(void)
{
	int cpu;

	for_each_possible_cpu(cpu) {
		struct preempted_vcore_list *lp = &per_cpu(preempted_vcores, cpu);
		spin_lock_init(&lp->lock);
		INIT_LIST_HEAD(&lp->list);
	}
}

static void kvmppc_vcore_preempt(struct kvmppc_vcore *vc)
{
	struct preempted_vcore_list *lp = this_cpu_ptr(&preempted_vcores);

	vc->vcore_state = VCORE_PREEMPT;
	vc->pcpu = smp_processor_id();
	if (vc->num_threads < threads_per_vcore(vc->kvm)) {
		spin_lock(&lp->lock);
		list_add_tail(&vc->preempt_list, &lp->list);
		spin_unlock(&lp->lock);
	}

	/* Start accumulating stolen time */
	kvmppc_core_start_stolen(vc);
}

static void kvmppc_vcore_end_preempt(struct kvmppc_vcore *vc)
{
	struct preempted_vcore_list *lp;

	kvmppc_core_end_stolen(vc);
	if (!list_empty(&vc->preempt_list)) {
		lp = &per_cpu(preempted_vcores, vc->pcpu);
		spin_lock(&lp->lock);
		list_del_init(&vc->preempt_list);
		spin_unlock(&lp->lock);
	}
	vc->vcore_state = VCORE_INACTIVE;
}

/*
 * This stores information about the virtual cores currently
 * assigned to a physical core.
 */
struct core_info {
	int		n_subcores;
	int		max_subcore_threads;
	int		total_threads;
	int		subcore_threads[MAX_SUBCORES];
	struct kvmppc_vcore *vc[MAX_SUBCORES];
};

/*
 * This mapping means subcores 0 and 1 can use threads 0-3 and 4-7
 * respectively in 2-way micro-threading (split-core) mode on POWER8.
 */
static int subcore_thread_map[MAX_SUBCORES] = { 0, 4, 2, 6 };

static void init_core_info(struct core_info *cip, struct kvmppc_vcore *vc)
{
	memset(cip, 0, sizeof(*cip));
	cip->n_subcores = 1;
	cip->max_subcore_threads = vc->num_threads;
	cip->total_threads = vc->num_threads;
	cip->subcore_threads[0] = vc->num_threads;
	cip->vc[0] = vc;
}

static bool subcore_config_ok(int n_subcores, int n_threads)
{
	/*
	 * POWER9 "SMT4" cores are permanently in what is effectively a 4-way
	 * split-core mode, with one thread per subcore.
	 */
	if (cpu_has_feature(CPU_FTR_ARCH_300))
		return n_subcores <= 4 && n_threads == 1;

	/* On POWER8, can only dynamically split if unsplit to begin with */
	if (n_subcores > 1 && threads_per_subcore < MAX_SMT_THREADS)
		return false;
	if (n_subcores > MAX_SUBCORES)
		return false;
	if (n_subcores > 1) {
		if (!(dynamic_mt_modes & 2))
			n_subcores = 4;
		if (n_subcores > 2 && !(dynamic_mt_modes & 4))
			return false;
	}

	return n_subcores * roundup_pow_of_two(n_threads) <= MAX_SMT_THREADS;
}

static void init_vcore_to_run(struct kvmppc_vcore *vc)
{
	vc->entry_exit_map = 0;
	vc->in_guest = 0;
	vc->napping_threads = 0;
	vc->conferring_threads = 0;
	vc->tb_offset_applied = 0;
}

static bool can_dynamic_split(struct kvmppc_vcore *vc, struct core_info *cip)
{
	int n_threads = vc->num_threads;
	int sub;

	if (!cpu_has_feature(CPU_FTR_ARCH_207S))
		return false;

	/* In one_vm_per_core mode, require all vcores to be from the same vm */
	if (one_vm_per_core && vc->kvm != cip->vc[0]->kvm)
		return false;

	/* Some POWER9 chips require all threads to be in the same MMU mode */
	if (no_mixing_hpt_and_radix &&
	    kvm_is_radix(vc->kvm) != kvm_is_radix(cip->vc[0]->kvm))
		return false;

	if (n_threads < cip->max_subcore_threads)
		n_threads = cip->max_subcore_threads;
	if (!subcore_config_ok(cip->n_subcores + 1, n_threads))
		return false;
	cip->max_subcore_threads = n_threads;

	sub = cip->n_subcores;
	++cip->n_subcores;
	cip->total_threads += vc->num_threads;
	cip->subcore_threads[sub] = vc->num_threads;
	cip->vc[sub] = vc;
	init_vcore_to_run(vc);
	list_del_init(&vc->preempt_list);

	return true;
}

/*
 * Work out whether it is possible to piggyback the execution of
 * vcore *pvc onto the execution of the other vcores described in *cip.
 */
static bool can_piggyback(struct kvmppc_vcore *pvc, struct core_info *cip,
			  int target_threads)
{
	if (cip->total_threads + pvc->num_threads > target_threads)
		return false;

	return can_dynamic_split(pvc, cip);
}

static void prepare_threads(struct kvmppc_vcore *vc)
{
	int i;
	struct kvm_vcpu *vcpu;

	for_each_runnable_thread(i, vcpu, vc) {
		if (signal_pending(vcpu->arch.run_task))
			vcpu->arch.ret = -EINTR;
		else if (vcpu->arch.vpa.update_pending ||
			 vcpu->arch.slb_shadow.update_pending ||
			 vcpu->arch.dtl.update_pending)
			vcpu->arch.ret = RESUME_GUEST;
		else
			continue;
		kvmppc_remove_runnable(vc, vcpu);
		wake_up(&vcpu->arch.cpu_run);
	}
}

static void collect_piggybacks(struct core_info *cip, int target_threads)
{
	struct preempted_vcore_list *lp = this_cpu_ptr(&preempted_vcores);
	struct kvmppc_vcore *pvc, *vcnext;

	spin_lock(&lp->lock);
	list_for_each_entry_safe(pvc, vcnext, &lp->list, preempt_list) {
		if (!spin_trylock(&pvc->lock))
			continue;
		prepare_threads(pvc);
		if (!pvc->n_runnable || !pvc->kvm->arch.mmu_ready) {
			list_del_init(&pvc->preempt_list);
			if (pvc->runner == NULL) {
				pvc->vcore_state = VCORE_INACTIVE;
				kvmppc_core_end_stolen(pvc);
			}
			spin_unlock(&pvc->lock);
			continue;
		}
		if (!can_piggyback(pvc, cip, target_threads)) {
			spin_unlock(&pvc->lock);
			continue;
		}
		kvmppc_core_end_stolen(pvc);
		pvc->vcore_state = VCORE_PIGGYBACK;
		if (cip->total_threads >= target_threads)
			break;
	}
	spin_unlock(&lp->lock);
}

static bool recheck_signals_and_mmu(struct core_info *cip)
{
	int sub, i;
	struct kvm_vcpu *vcpu;
	struct kvmppc_vcore *vc;

	for (sub = 0; sub < cip->n_subcores; ++sub) {
		vc = cip->vc[sub];
		if (!vc->kvm->arch.mmu_ready)
			return true;
		for_each_runnable_thread(i, vcpu, vc)
			if (signal_pending(vcpu->arch.run_task))
				return true;
	}
	return false;
}

static void post_guest_process(struct kvmppc_vcore *vc, bool is_master)
{
	int still_running = 0, i;
	u64 now;
	long ret;
	struct kvm_vcpu *vcpu;

	spin_lock(&vc->lock);
	now = get_tb();
	for_each_runnable_thread(i, vcpu, vc) {
		/*
		 * It's safe to unlock the vcore in the loop here, because
		 * for_each_runnable_thread() is safe against removal of
		 * the vcpu, and the vcore state is VCORE_EXITING here,
		 * so any vcpus becoming runnable will have their arch.trap
		 * set to zero and can't actually run in the guest.
		 */
		spin_unlock(&vc->lock);
		/* cancel pending dec exception if dec is positive */
		if (now < vcpu->arch.dec_expires &&
		    kvmppc_core_pending_dec(vcpu))
			kvmppc_core_dequeue_dec(vcpu);

		trace_kvm_guest_exit(vcpu);

		ret = RESUME_GUEST;
		if (vcpu->arch.trap)
			ret = kvmppc_handle_exit_hv(vcpu->arch.kvm_run, vcpu,
						    vcpu->arch.run_task);

		vcpu->arch.ret = ret;
		vcpu->arch.trap = 0;

		spin_lock(&vc->lock);
		if (is_kvmppc_resume_guest(vcpu->arch.ret)) {
			if (vcpu->arch.pending_exceptions)
				kvmppc_core_prepare_to_enter(vcpu);
			if (vcpu->arch.ceded)
				kvmppc_set_timer(vcpu);
			else
				++still_running;
		} else {
			kvmppc_remove_runnable(vc, vcpu);
			wake_up(&vcpu->arch.cpu_run);
		}
	}
	if (!is_master) {
		if (still_running > 0) {
			kvmppc_vcore_preempt(vc);
		} else if (vc->runner) {
			vc->vcore_state = VCORE_PREEMPT;
			kvmppc_core_start_stolen(vc);
		} else {
			vc->vcore_state = VCORE_INACTIVE;
		}
		if (vc->n_runnable > 0 && vc->runner == NULL) {
			/* make sure there's a candidate runner awake */
			i = -1;
			vcpu = next_runnable_thread(vc, &i);
			wake_up(&vcpu->arch.cpu_run);
		}
	}
	spin_unlock(&vc->lock);
}

/*
 * Clear core from the list of active host cores as we are about to
 * enter the guest. Only do this if it is the primary thread of the
 * core (not if a subcore) that is entering the guest.
 */
static inline int kvmppc_clear_host_core(unsigned int cpu)
{
	int core;

	if (!kvmppc_host_rm_ops_hv || cpu_thread_in_core(cpu))
		return 0;
	/*
	 * Memory barrier can be omitted here as we will do a smp_wmb()
	 * later in kvmppc_start_thread and we need ensure that state is
	 * visible to other CPUs only after we enter guest.
	 */
	core = cpu >> threads_shift;
	kvmppc_host_rm_ops_hv->rm_core[core].rm_state.in_host = 0;
	return 0;
}

/*
 * Advertise this core as an active host core since we exited the guest
 * Only need to do this if it is the primary thread of the core that is
 * exiting.
 */
static inline int kvmppc_set_host_core(unsigned int cpu)
{
	int core;

	if (!kvmppc_host_rm_ops_hv || cpu_thread_in_core(cpu))
		return 0;

	/*
	 * Memory barrier can be omitted here because we do a spin_unlock
	 * immediately after this which provides the memory barrier.
	 */
	core = cpu >> threads_shift;
	kvmppc_host_rm_ops_hv->rm_core[core].rm_state.in_host = 1;
	return 0;
}

static void set_irq_happened(int trap)
{
	switch (trap) {
	case BOOK3S_INTERRUPT_EXTERNAL:
		local_paca->irq_happened |= PACA_IRQ_EE;
		break;
	case BOOK3S_INTERRUPT_H_DOORBELL:
		local_paca->irq_happened |= PACA_IRQ_DBELL;
		break;
	case BOOK3S_INTERRUPT_HMI:
		local_paca->irq_happened |= PACA_IRQ_HMI;
		break;
	case BOOK3S_INTERRUPT_SYSTEM_RESET:
		replay_system_reset();
		break;
	}
}

/*
 * Run a set of guest threads on a physical core.
 * Called with vc->lock held.
 */
static noinline void kvmppc_run_core(struct kvmppc_vcore *vc)
{
	struct kvm_vcpu *vcpu;
	int i;
	int srcu_idx;
	struct core_info core_info;
	struct kvmppc_vcore *pvc;
	struct kvm_split_mode split_info, *sip;
	int split, subcore_size, active;
	int sub;
	bool thr0_done;
	unsigned long cmd_bit, stat_bit;
	int pcpu, thr;
	int target_threads;
	int controlled_threads;
	int trap;
	bool is_power8;
	bool hpt_on_radix;

	/*
	 * Remove from the list any threads that have a signal pending
	 * or need a VPA update done
	 */
	prepare_threads(vc);

	/* if the runner is no longer runnable, let the caller pick a new one */
	if (vc->runner->arch.state != KVMPPC_VCPU_RUNNABLE)
		return;

	/*
	 * Initialize *vc.
	 */
	init_vcore_to_run(vc);
	vc->preempt_tb = TB_NIL;

	/*
	 * Number of threads that we will be controlling: the same as
	 * the number of threads per subcore, except on POWER9,
	 * where it's 1 because the threads are (mostly) independent.
	 */
	controlled_threads = threads_per_vcore(vc->kvm);

	/*
	 * Make sure we are running on primary threads, and that secondary
	 * threads are offline.  Also check if the number of threads in this
	 * guest are greater than the current system threads per guest.
	 * On POWER9, we need to be not in independent-threads mode if
	 * this is a HPT guest on a radix host machine where the
	 * CPU threads may not be in different MMU modes.
	 */
	hpt_on_radix = no_mixing_hpt_and_radix && radix_enabled() &&
		!kvm_is_radix(vc->kvm);
	if (((controlled_threads > 1) &&
	     ((vc->num_threads > threads_per_subcore) || !on_primary_thread())) ||
	    (hpt_on_radix && vc->kvm->arch.threads_indep)) {
		for_each_runnable_thread(i, vcpu, vc) {
			vcpu->arch.ret = -EBUSY;
			kvmppc_remove_runnable(vc, vcpu);
			wake_up(&vcpu->arch.cpu_run);
		}
		goto out;
	}

	/*
	 * See if we could run any other vcores on the physical core
	 * along with this one.
	 */
	init_core_info(&core_info, vc);
	pcpu = smp_processor_id();
	target_threads = controlled_threads;
	if (target_smt_mode && target_smt_mode < target_threads)
		target_threads = target_smt_mode;
	if (vc->num_threads < target_threads)
		collect_piggybacks(&core_info, target_threads);

	/*
	 * On radix, arrange for TLB flushing if necessary.
	 * This has to be done before disabling interrupts since
	 * it uses smp_call_function().
	 */
	pcpu = smp_processor_id();
	if (kvm_is_radix(vc->kvm)) {
		for (sub = 0; sub < core_info.n_subcores; ++sub)
			for_each_runnable_thread(i, vcpu, core_info.vc[sub])
				kvmppc_prepare_radix_vcpu(vcpu, pcpu);
	}

	/*
	 * Hard-disable interrupts, and check resched flag and signals.
	 * If we need to reschedule or deliver a signal, clean up
	 * and return without going into the guest(s).
	 * If the mmu_ready flag has been cleared, don't go into the
	 * guest because that means a HPT resize operation is in progress.
	 */
	local_irq_disable();
	hard_irq_disable();
	if (lazy_irq_pending() || need_resched() ||
	    recheck_signals_and_mmu(&core_info)) {
		local_irq_enable();
		vc->vcore_state = VCORE_INACTIVE;
		/* Unlock all except the primary vcore */
		for (sub = 1; sub < core_info.n_subcores; ++sub) {
			pvc = core_info.vc[sub];
			/* Put back on to the preempted vcores list */
			kvmppc_vcore_preempt(pvc);
			spin_unlock(&pvc->lock);
		}
		for (i = 0; i < controlled_threads; ++i)
			kvmppc_release_hwthread(pcpu + i);
		return;
	}

	kvmppc_clear_host_core(pcpu);

	/* Decide on micro-threading (split-core) mode */
	subcore_size = threads_per_subcore;
	cmd_bit = stat_bit = 0;
	split = core_info.n_subcores;
	sip = NULL;
	is_power8 = cpu_has_feature(CPU_FTR_ARCH_207S)
		&& !cpu_has_feature(CPU_FTR_ARCH_300);

	if (split > 1 || hpt_on_radix) {
		sip = &split_info;
		memset(&split_info, 0, sizeof(split_info));
		for (sub = 0; sub < core_info.n_subcores; ++sub)
			split_info.vc[sub] = core_info.vc[sub];

		if (is_power8) {
			if (split == 2 && (dynamic_mt_modes & 2)) {
				cmd_bit = HID0_POWER8_1TO2LPAR;
				stat_bit = HID0_POWER8_2LPARMODE;
			} else {
				split = 4;
				cmd_bit = HID0_POWER8_1TO4LPAR;
				stat_bit = HID0_POWER8_4LPARMODE;
			}
			subcore_size = MAX_SMT_THREADS / split;
			split_info.rpr = mfspr(SPRN_RPR);
			split_info.pmmar = mfspr(SPRN_PMMAR);
			split_info.ldbar = mfspr(SPRN_LDBAR);
			split_info.subcore_size = subcore_size;
		} else {
			split_info.subcore_size = 1;
			if (hpt_on_radix) {
				/* Use the split_info for LPCR/LPIDR changes */
				split_info.lpcr_req = vc->lpcr;
				split_info.lpidr_req = vc->kvm->arch.lpid;
				split_info.host_lpcr = vc->kvm->arch.host_lpcr;
				split_info.do_set = 1;
			}
		}

		/* order writes to split_info before kvm_split_mode pointer */
		smp_wmb();
	}

	for (thr = 0; thr < controlled_threads; ++thr) {
		struct paca_struct *paca = paca_ptrs[pcpu + thr];

		paca->kvm_hstate.tid = thr;
		paca->kvm_hstate.napping = 0;
		paca->kvm_hstate.kvm_split_mode = sip;
	}

	/* Initiate micro-threading (split-core) on POWER8 if required */
	if (cmd_bit) {
		unsigned long hid0 = mfspr(SPRN_HID0);

		hid0 |= cmd_bit | HID0_POWER8_DYNLPARDIS;
		mb();
		mtspr(SPRN_HID0, hid0);
		isync();
		for (;;) {
			hid0 = mfspr(SPRN_HID0);
			if (hid0 & stat_bit)
				break;
			cpu_relax();
		}
	}

	/*
	 * On POWER8, set RWMR register.
	 * Since it only affects PURR and SPURR, it doesn't affect
	 * the host, so we don't save/restore the host value.
	 */
	if (is_power8) {
		unsigned long rwmr_val = RWMR_RPA_P8_8THREAD;
		int n_online = atomic_read(&vc->online_count);

		/*
		 * Use the 8-thread value if we're doing split-core
		 * or if the vcore's online count looks bogus.
		 */
		if (split == 1 && threads_per_subcore == MAX_SMT_THREADS &&
		    n_online >= 1 && n_online <= MAX_SMT_THREADS)
			rwmr_val = p8_rwmr_values[n_online];
		mtspr(SPRN_RWMR, rwmr_val);
	}

	/* Start all the threads */
	active = 0;
	for (sub = 0; sub < core_info.n_subcores; ++sub) {
		thr = is_power8 ? subcore_thread_map[sub] : sub;
		thr0_done = false;
		active |= 1 << thr;
		pvc = core_info.vc[sub];
		pvc->pcpu = pcpu + thr;
		for_each_runnable_thread(i, vcpu, pvc) {
			kvmppc_start_thread(vcpu, pvc);
			kvmppc_create_dtl_entry(vcpu, pvc);
			trace_kvm_guest_enter(vcpu);
			if (!vcpu->arch.ptid)
				thr0_done = true;
			active |= 1 << (thr + vcpu->arch.ptid);
		}
		/*
		 * We need to start the first thread of each subcore
		 * even if it doesn't have a vcpu.
		 */
		if (!thr0_done)
			kvmppc_start_thread(NULL, pvc);
	}

	/*
	 * Ensure that split_info.do_nap is set after setting
	 * the vcore pointer in the PACA of the secondaries.
	 */
	smp_mb();

	/*
	 * When doing micro-threading, poke the inactive threads as well.
	 * This gets them to the nap instruction after kvm_do_nap,
	 * which reduces the time taken to unsplit later.
	 * For POWER9 HPT guest on radix host, we need all the secondary
	 * threads woken up so they can do the LPCR/LPIDR change.
	 */
	if (cmd_bit || hpt_on_radix) {
		split_info.do_nap = 1;	/* ask secondaries to nap when done */
		for (thr = 1; thr < threads_per_subcore; ++thr)
			if (!(active & (1 << thr)))
				kvmppc_ipi_thread(pcpu + thr);
	}

	vc->vcore_state = VCORE_RUNNING;
	preempt_disable();

	trace_kvmppc_run_core(vc, 0);

	for (sub = 0; sub < core_info.n_subcores; ++sub)
		spin_unlock(&core_info.vc[sub]->lock);

	guest_enter_irqoff();

	srcu_idx = srcu_read_lock(&vc->kvm->srcu);

	this_cpu_disable_ftrace();

	/*
	 * Interrupts will be enabled once we get into the guest,
	 * so tell lockdep that we're about to enable interrupts.
	 */
	trace_hardirqs_on();

	trap = __kvmppc_vcore_entry();

	trace_hardirqs_off();

	this_cpu_enable_ftrace();

	srcu_read_unlock(&vc->kvm->srcu, srcu_idx);

	set_irq_happened(trap);

	spin_lock(&vc->lock);
	/* prevent other vcpu threads from doing kvmppc_start_thread() now */
	vc->vcore_state = VCORE_EXITING;

	/* wait for secondary threads to finish writing their state to memory */
	kvmppc_wait_for_nap(controlled_threads);

	/* Return to whole-core mode if we split the core earlier */
	if (cmd_bit) {
		unsigned long hid0 = mfspr(SPRN_HID0);
		unsigned long loops = 0;

		hid0 &= ~HID0_POWER8_DYNLPARDIS;
		stat_bit = HID0_POWER8_2LPARMODE | HID0_POWER8_4LPARMODE;
		mb();
		mtspr(SPRN_HID0, hid0);
		isync();
		for (;;) {
			hid0 = mfspr(SPRN_HID0);
			if (!(hid0 & stat_bit))
				break;
			cpu_relax();
			++loops;
		}
	} else if (hpt_on_radix) {
		/* Wait for all threads to have seen final sync */
		for (thr = 1; thr < controlled_threads; ++thr) {
			struct paca_struct *paca = paca_ptrs[pcpu + thr];

			while (paca->kvm_hstate.kvm_split_mode) {
				HMT_low();
				barrier();
			}
			HMT_medium();
		}
	}
	split_info.do_nap = 0;

	kvmppc_set_host_core(pcpu);

	local_irq_enable();
	guest_exit();

	/* Let secondaries go back to the offline loop */
	for (i = 0; i < controlled_threads; ++i) {
		kvmppc_release_hwthread(pcpu + i);
		if (sip && sip->napped[i])
			kvmppc_ipi_thread(pcpu + i);
		cpumask_clear_cpu(pcpu + i, &vc->kvm->arch.cpu_in_guest);
	}

	spin_unlock(&vc->lock);

	/* make sure updates to secondary vcpu structs are visible now */
	smp_mb();

	preempt_enable();

	for (sub = 0; sub < core_info.n_subcores; ++sub) {
		pvc = core_info.vc[sub];
		post_guest_process(pvc, pvc == vc);
	}

	spin_lock(&vc->lock);

 out:
	vc->vcore_state = VCORE_INACTIVE;
	trace_kvmppc_run_core(vc, 1);
}

/*
 * Load up hypervisor-mode registers on P9.
 */
static int kvmhv_load_hv_regs_and_go(struct kvm_vcpu *vcpu, u64 time_limit,
				     unsigned long lpcr)
{
	struct kvmppc_vcore *vc = vcpu->arch.vcore;
	s64 hdec;
	u64 tb, purr, spurr;
	int trap;
	unsigned long host_hfscr = mfspr(SPRN_HFSCR);
	unsigned long host_ciabr = mfspr(SPRN_CIABR);
	unsigned long host_dawr = mfspr(SPRN_DAWR);
	unsigned long host_dawrx = mfspr(SPRN_DAWRX);
	unsigned long host_psscr = mfspr(SPRN_PSSCR);
	unsigned long host_pidr = mfspr(SPRN_PID);

	hdec = time_limit - mftb();
	if (hdec < 0)
		return BOOK3S_INTERRUPT_HV_DECREMENTER;
	mtspr(SPRN_HDEC, hdec);

	if (vc->tb_offset) {
		u64 new_tb = mftb() + vc->tb_offset;
		mtspr(SPRN_TBU40, new_tb);
		tb = mftb();
		if ((tb & 0xffffff) < (new_tb & 0xffffff))
			mtspr(SPRN_TBU40, new_tb + 0x1000000);
		vc->tb_offset_applied = vc->tb_offset;
	}

	if (vc->pcr)
		mtspr(SPRN_PCR, vc->pcr | PCR_MASK);
	mtspr(SPRN_DPDES, vc->dpdes);
	mtspr(SPRN_VTB, vc->vtb);

	local_paca->kvm_hstate.host_purr = mfspr(SPRN_PURR);
	local_paca->kvm_hstate.host_spurr = mfspr(SPRN_SPURR);
	mtspr(SPRN_PURR, vcpu->arch.purr);
	mtspr(SPRN_SPURR, vcpu->arch.spurr);

	if (dawr_enabled()) {
		mtspr(SPRN_DAWR, vcpu->arch.dawr);
		mtspr(SPRN_DAWRX, vcpu->arch.dawrx);
	}
	mtspr(SPRN_CIABR, vcpu->arch.ciabr);
	mtspr(SPRN_IC, vcpu->arch.ic);
	mtspr(SPRN_PID, vcpu->arch.pid);

	mtspr(SPRN_PSSCR, vcpu->arch.psscr | PSSCR_EC |
	      (local_paca->kvm_hstate.fake_suspend << PSSCR_FAKE_SUSPEND_LG));

	mtspr(SPRN_HFSCR, vcpu->arch.hfscr);

	mtspr(SPRN_SPRG0, vcpu->arch.shregs.sprg0);
	mtspr(SPRN_SPRG1, vcpu->arch.shregs.sprg1);
	mtspr(SPRN_SPRG2, vcpu->arch.shregs.sprg2);
	mtspr(SPRN_SPRG3, vcpu->arch.shregs.sprg3);

	mtspr(SPRN_AMOR, ~0UL);

	mtspr(SPRN_LPCR, lpcr);
	isync();

	kvmppc_xive_push_vcpu(vcpu);

	mtspr(SPRN_SRR0, vcpu->arch.shregs.srr0);
	mtspr(SPRN_SRR1, vcpu->arch.shregs.srr1);

	trap = __kvmhv_vcpu_entry_p9(vcpu);

	/* Advance host PURR/SPURR by the amount used by guest */
	purr = mfspr(SPRN_PURR);
	spurr = mfspr(SPRN_SPURR);
	mtspr(SPRN_PURR, local_paca->kvm_hstate.host_purr +
	      purr - vcpu->arch.purr);
	mtspr(SPRN_SPURR, local_paca->kvm_hstate.host_spurr +
	      spurr - vcpu->arch.spurr);
	vcpu->arch.purr = purr;
	vcpu->arch.spurr = spurr;

	vcpu->arch.ic = mfspr(SPRN_IC);
	vcpu->arch.pid = mfspr(SPRN_PID);
	vcpu->arch.psscr = mfspr(SPRN_PSSCR) & PSSCR_GUEST_VIS;

	vcpu->arch.shregs.sprg0 = mfspr(SPRN_SPRG0);
	vcpu->arch.shregs.sprg1 = mfspr(SPRN_SPRG1);
	vcpu->arch.shregs.sprg2 = mfspr(SPRN_SPRG2);
	vcpu->arch.shregs.sprg3 = mfspr(SPRN_SPRG3);

	/* Preserve PSSCR[FAKE_SUSPEND] until we've called kvmppc_save_tm_hv */
	mtspr(SPRN_PSSCR, host_psscr |
	      (local_paca->kvm_hstate.fake_suspend << PSSCR_FAKE_SUSPEND_LG));
	mtspr(SPRN_HFSCR, host_hfscr);
	mtspr(SPRN_CIABR, host_ciabr);
	mtspr(SPRN_DAWR, host_dawr);
	mtspr(SPRN_DAWRX, host_dawrx);
	mtspr(SPRN_PID, host_pidr);

	/*
	 * Since this is radix, do a eieio; tlbsync; ptesync sequence in
	 * case we interrupted the guest between a tlbie and a ptesync.
	 */
	asm volatile("eieio; tlbsync; ptesync");

	mtspr(SPRN_LPID, vcpu->kvm->arch.host_lpid);	/* restore host LPID */
	isync();

	vc->dpdes = mfspr(SPRN_DPDES);
	vc->vtb = mfspr(SPRN_VTB);
	mtspr(SPRN_DPDES, 0);
	if (vc->pcr)
		mtspr(SPRN_PCR, PCR_MASK);

	if (vc->tb_offset_applied) {
		u64 new_tb = mftb() - vc->tb_offset_applied;
		mtspr(SPRN_TBU40, new_tb);
		tb = mftb();
		if ((tb & 0xffffff) < (new_tb & 0xffffff))
			mtspr(SPRN_TBU40, new_tb + 0x1000000);
		vc->tb_offset_applied = 0;
	}

	mtspr(SPRN_HDEC, 0x7fffffff);
	mtspr(SPRN_LPCR, vcpu->kvm->arch.host_lpcr);

	return trap;
}

/*
 * Virtual-mode guest entry for POWER9 and later when the host and
 * guest are both using the radix MMU.  The LPIDR has already been set.
 */
int kvmhv_p9_guest_entry(struct kvm_vcpu *vcpu, u64 time_limit,
			 unsigned long lpcr)
{
	struct kvmppc_vcore *vc = vcpu->arch.vcore;
	unsigned long host_dscr = mfspr(SPRN_DSCR);
	unsigned long host_tidr = mfspr(SPRN_TIDR);
	unsigned long host_iamr = mfspr(SPRN_IAMR);
	unsigned long host_amr = mfspr(SPRN_AMR);
	s64 dec;
	u64 tb;
	int trap, save_pmu;

	dec = mfspr(SPRN_DEC);
	tb = mftb();
	if (dec < 512)
		return BOOK3S_INTERRUPT_HV_DECREMENTER;
	local_paca->kvm_hstate.dec_expires = dec + tb;
	if (local_paca->kvm_hstate.dec_expires < time_limit)
		time_limit = local_paca->kvm_hstate.dec_expires;

	vcpu->arch.ceded = 0;

	kvmhv_save_host_pmu();		/* saves it to PACA kvm_hstate */

	kvmppc_subcore_enter_guest();

	vc->entry_exit_map = 1;
	vc->in_guest = 1;

	if (vcpu->arch.vpa.pinned_addr) {
		struct lppaca *lp = vcpu->arch.vpa.pinned_addr;
		u32 yield_count = be32_to_cpu(lp->yield_count) + 1;
		lp->yield_count = cpu_to_be32(yield_count);
		vcpu->arch.vpa.dirty = 1;
	}

	if (cpu_has_feature(CPU_FTR_TM) ||
	    cpu_has_feature(CPU_FTR_P9_TM_HV_ASSIST))
		kvmppc_restore_tm_hv(vcpu, vcpu->arch.shregs.msr, true);

	kvmhv_load_guest_pmu(vcpu);

	msr_check_and_set(MSR_FP | MSR_VEC | MSR_VSX);
	load_fp_state(&vcpu->arch.fp);
#ifdef CONFIG_ALTIVEC
	load_vr_state(&vcpu->arch.vr);
#endif
	mtspr(SPRN_VRSAVE, vcpu->arch.vrsave);

	mtspr(SPRN_DSCR, vcpu->arch.dscr);
	mtspr(SPRN_IAMR, vcpu->arch.iamr);
	mtspr(SPRN_PSPB, vcpu->arch.pspb);
	mtspr(SPRN_FSCR, vcpu->arch.fscr);
	mtspr(SPRN_TAR, vcpu->arch.tar);
	mtspr(SPRN_EBBHR, vcpu->arch.ebbhr);
	mtspr(SPRN_EBBRR, vcpu->arch.ebbrr);
	mtspr(SPRN_BESCR, vcpu->arch.bescr);
	mtspr(SPRN_WORT, vcpu->arch.wort);
	mtspr(SPRN_TIDR, vcpu->arch.tid);
	mtspr(SPRN_DAR, vcpu->arch.shregs.dar);
	mtspr(SPRN_DSISR, vcpu->arch.shregs.dsisr);
	mtspr(SPRN_AMR, vcpu->arch.amr);
	mtspr(SPRN_UAMOR, vcpu->arch.uamor);

	if (!(vcpu->arch.ctrl & 1))
		mtspr(SPRN_CTRLT, mfspr(SPRN_CTRLF) & ~1);

	mtspr(SPRN_DEC, vcpu->arch.dec_expires - mftb());

	if (kvmhv_on_pseries()) {
		/*
		 * We need to save and restore the guest visible part of the
		 * psscr (i.e. using SPRN_PSSCR_PR) since the hypervisor
		 * doesn't do this for us. Note only required if pseries since
		 * this is done in kvmhv_load_hv_regs_and_go() below otherwise.
		 */
		unsigned long host_psscr;
		/* call our hypervisor to load up HV regs and go */
		struct hv_guest_state hvregs;

		host_psscr = mfspr(SPRN_PSSCR_PR);
		mtspr(SPRN_PSSCR_PR, vcpu->arch.psscr);
		kvmhv_save_hv_regs(vcpu, &hvregs);
		hvregs.lpcr = lpcr;
		vcpu->arch.regs.msr = vcpu->arch.shregs.msr;
		hvregs.version = HV_GUEST_STATE_VERSION;
		if (vcpu->arch.nested) {
			hvregs.lpid = vcpu->arch.nested->shadow_lpid;
			hvregs.vcpu_token = vcpu->arch.nested_vcpu_id;
		} else {
			hvregs.lpid = vcpu->kvm->arch.lpid;
			hvregs.vcpu_token = vcpu->vcpu_id;
		}
		hvregs.hdec_expiry = time_limit;
		trap = plpar_hcall_norets(H_ENTER_NESTED, __pa(&hvregs),
					  __pa(&vcpu->arch.regs));
		kvmhv_restore_hv_return_state(vcpu, &hvregs);
		vcpu->arch.shregs.msr = vcpu->arch.regs.msr;
		vcpu->arch.shregs.dar = mfspr(SPRN_DAR);
		vcpu->arch.shregs.dsisr = mfspr(SPRN_DSISR);
		vcpu->arch.psscr = mfspr(SPRN_PSSCR_PR);
		mtspr(SPRN_PSSCR_PR, host_psscr);

		/* H_CEDE has to be handled now, not later */
		if (trap == BOOK3S_INTERRUPT_SYSCALL && !vcpu->arch.nested &&
		    kvmppc_get_gpr(vcpu, 3) == H_CEDE) {
			kvmppc_nested_cede(vcpu);
			trap = 0;
		}
	} else {
		trap = kvmhv_load_hv_regs_and_go(vcpu, time_limit, lpcr);
	}

	vcpu->arch.slb_max = 0;
	dec = mfspr(SPRN_DEC);
	if (!(lpcr & LPCR_LD)) /* Sign extend if not using large decrementer */
		dec = (s32) dec;
	tb = mftb();
	vcpu->arch.dec_expires = dec + tb;
	vcpu->cpu = -1;
	vcpu->arch.thread_cpu = -1;
	vcpu->arch.ctrl = mfspr(SPRN_CTRLF);

	vcpu->arch.iamr = mfspr(SPRN_IAMR);
	vcpu->arch.pspb = mfspr(SPRN_PSPB);
	vcpu->arch.fscr = mfspr(SPRN_FSCR);
	vcpu->arch.tar = mfspr(SPRN_TAR);
	vcpu->arch.ebbhr = mfspr(SPRN_EBBHR);
	vcpu->arch.ebbrr = mfspr(SPRN_EBBRR);
	vcpu->arch.bescr = mfspr(SPRN_BESCR);
	vcpu->arch.wort = mfspr(SPRN_WORT);
	vcpu->arch.tid = mfspr(SPRN_TIDR);
	vcpu->arch.amr = mfspr(SPRN_AMR);
	vcpu->arch.uamor = mfspr(SPRN_UAMOR);
	vcpu->arch.dscr = mfspr(SPRN_DSCR);

	mtspr(SPRN_PSPB, 0);
	mtspr(SPRN_WORT, 0);
	mtspr(SPRN_UAMOR, 0);
	mtspr(SPRN_DSCR, host_dscr);
	mtspr(SPRN_TIDR, host_tidr);
	mtspr(SPRN_IAMR, host_iamr);
	mtspr(SPRN_PSPB, 0);

	if (host_amr != vcpu->arch.amr)
		mtspr(SPRN_AMR, host_amr);

	msr_check_and_set(MSR_FP | MSR_VEC | MSR_VSX);
	store_fp_state(&vcpu->arch.fp);
#ifdef CONFIG_ALTIVEC
	store_vr_state(&vcpu->arch.vr);
#endif
	vcpu->arch.vrsave = mfspr(SPRN_VRSAVE);

	if (cpu_has_feature(CPU_FTR_TM) ||
	    cpu_has_feature(CPU_FTR_P9_TM_HV_ASSIST))
		kvmppc_save_tm_hv(vcpu, vcpu->arch.shregs.msr, true);

	save_pmu = 1;
	if (vcpu->arch.vpa.pinned_addr) {
		struct lppaca *lp = vcpu->arch.vpa.pinned_addr;
		u32 yield_count = be32_to_cpu(lp->yield_count) + 1;
		lp->yield_count = cpu_to_be32(yield_count);
		vcpu->arch.vpa.dirty = 1;
		save_pmu = lp->pmcregs_in_use;
	}
	/* Must save pmu if this guest is capable of running nested guests */
	save_pmu |= nesting_enabled(vcpu->kvm);

	kvmhv_save_guest_pmu(vcpu, save_pmu);

	vc->entry_exit_map = 0x101;
	vc->in_guest = 0;

	mtspr(SPRN_DEC, local_paca->kvm_hstate.dec_expires - mftb());
	mtspr(SPRN_SPRG_VDSO_WRITE, local_paca->sprg_vdso);

	kvmhv_load_host_pmu();

	kvmppc_subcore_exit_guest();

	return trap;
}

/*
 * Wait for some other vcpu thread to execute us, and
 * wake us up when we need to handle something in the host.
 */
static void kvmppc_wait_for_exec(struct kvmppc_vcore *vc,
				 struct kvm_vcpu *vcpu, int wait_state)
{
	DEFINE_WAIT(wait);

	prepare_to_wait(&vcpu->arch.cpu_run, &wait, wait_state);
	if (vcpu->arch.state == KVMPPC_VCPU_RUNNABLE) {
		spin_unlock(&vc->lock);
		schedule();
		spin_lock(&vc->lock);
	}
	finish_wait(&vcpu->arch.cpu_run, &wait);
}

static void grow_halt_poll_ns(struct kvmppc_vcore *vc)
{
	if (!halt_poll_ns_grow)
		return;

	vc->halt_poll_ns *= halt_poll_ns_grow;
	if (vc->halt_poll_ns < halt_poll_ns_grow_start)
		vc->halt_poll_ns = halt_poll_ns_grow_start;
}

static void shrink_halt_poll_ns(struct kvmppc_vcore *vc)
{
	if (halt_poll_ns_shrink == 0)
		vc->halt_poll_ns = 0;
	else
		vc->halt_poll_ns /= halt_poll_ns_shrink;
}

#ifdef CONFIG_KVM_XICS
static inline bool xive_interrupt_pending(struct kvm_vcpu *vcpu)
{
	if (!xics_on_xive())
		return false;
	return vcpu->arch.irq_pending || vcpu->arch.xive_saved_state.pipr <
		vcpu->arch.xive_saved_state.cppr;
}
#else
static inline bool xive_interrupt_pending(struct kvm_vcpu *vcpu)
{
	return false;
}
#endif /* CONFIG_KVM_XICS */

static bool kvmppc_vcpu_woken(struct kvm_vcpu *vcpu)
{
	if (vcpu->arch.pending_exceptions || vcpu->arch.prodded ||
	    kvmppc_doorbell_pending(vcpu) || xive_interrupt_pending(vcpu))
		return true;

	return false;
}

/*
 * Check to see if any of the runnable vcpus on the vcore have pending
 * exceptions or are no longer ceded
 */
static int kvmppc_vcore_check_block(struct kvmppc_vcore *vc)
{
	struct kvm_vcpu *vcpu;
	int i;

	for_each_runnable_thread(i, vcpu, vc) {
		if (!vcpu->arch.ceded || kvmppc_vcpu_woken(vcpu))
			return 1;
	}

	return 0;
}

/*
 * All the vcpus in this vcore are idle, so wait for a decrementer
 * or external interrupt to one of the vcpus.  vc->lock is held.
 */
static void kvmppc_vcore_blocked(struct kvmppc_vcore *vc)
{
	ktime_t cur, start_poll, start_wait;
	int do_sleep = 1;
	u64 block_ns;
	DECLARE_SWAITQUEUE(wait);

	/* Poll for pending exceptions and ceded state */
	cur = start_poll = ktime_get();
	if (vc->halt_poll_ns) {
		ktime_t stop = ktime_add_ns(start_poll, vc->halt_poll_ns);
		++vc->runner->stat.halt_attempted_poll;

		vc->vcore_state = VCORE_POLLING;
		spin_unlock(&vc->lock);

		do {
			if (kvmppc_vcore_check_block(vc)) {
				do_sleep = 0;
				break;
			}
			cur = ktime_get();
		} while (single_task_running() && ktime_before(cur, stop));

		spin_lock(&vc->lock);
		vc->vcore_state = VCORE_INACTIVE;

		if (!do_sleep) {
			++vc->runner->stat.halt_successful_poll;
			goto out;
		}
	}

	prepare_to_swait_exclusive(&vc->wq, &wait, TASK_INTERRUPTIBLE);

	if (kvmppc_vcore_check_block(vc)) {
		finish_swait(&vc->wq, &wait);
		do_sleep = 0;
		/* If we polled, count this as a successful poll */
		if (vc->halt_poll_ns)
			++vc->runner->stat.halt_successful_poll;
		goto out;
	}

	start_wait = ktime_get();

	vc->vcore_state = VCORE_SLEEPING;
	trace_kvmppc_vcore_blocked(vc, 0);
	spin_unlock(&vc->lock);
	schedule();
	finish_swait(&vc->wq, &wait);
	spin_lock(&vc->lock);
	vc->vcore_state = VCORE_INACTIVE;
	trace_kvmppc_vcore_blocked(vc, 1);
	++vc->runner->stat.halt_successful_wait;

	cur = ktime_get();

out:
	block_ns = ktime_to_ns(cur) - ktime_to_ns(start_poll);

	/* Attribute wait time */
	if (do_sleep) {
		vc->runner->stat.halt_wait_ns +=
			ktime_to_ns(cur) - ktime_to_ns(start_wait);
		/* Attribute failed poll time */
		if (vc->halt_poll_ns)
			vc->runner->stat.halt_poll_fail_ns +=
				ktime_to_ns(start_wait) -
				ktime_to_ns(start_poll);
	} else {
		/* Attribute successful poll time */
		if (vc->halt_poll_ns)
			vc->runner->stat.halt_poll_success_ns +=
				ktime_to_ns(cur) -
				ktime_to_ns(start_poll);
	}

	/* Adjust poll time */
	if (halt_poll_ns) {
		if (block_ns <= vc->halt_poll_ns)
			;
		/* We slept and blocked for longer than the max halt time */
		else if (vc->halt_poll_ns && block_ns > halt_poll_ns)
			shrink_halt_poll_ns(vc);
		/* We slept and our poll time is too small */
		else if (vc->halt_poll_ns < halt_poll_ns &&
				block_ns < halt_poll_ns)
			grow_halt_poll_ns(vc);
		if (vc->halt_poll_ns > halt_poll_ns)
			vc->halt_poll_ns = halt_poll_ns;
	} else
		vc->halt_poll_ns = 0;

	trace_kvmppc_vcore_wakeup(do_sleep, block_ns);
}

/*
 * This never fails for a radix guest, as none of the operations it does
 * for a radix guest can fail or have a way to report failure.
 * kvmhv_run_single_vcpu() relies on this fact.
 */
static int kvmhv_setup_mmu(struct kvm_vcpu *vcpu)
{
	int r = 0;
	struct kvm *kvm = vcpu->kvm;

	mutex_lock(&kvm->arch.mmu_setup_lock);
	if (!kvm->arch.mmu_ready) {
		if (!kvm_is_radix(kvm))
			r = kvmppc_hv_setup_htab_rma(vcpu);
		if (!r) {
			if (cpu_has_feature(CPU_FTR_ARCH_300))
				kvmppc_setup_partition_table(kvm);
			kvm->arch.mmu_ready = 1;
		}
	}
	mutex_unlock(&kvm->arch.mmu_setup_lock);
	return r;
}

static int kvmppc_run_vcpu(struct kvm_run *kvm_run, struct kvm_vcpu *vcpu)
{
	int n_ceded, i, r;
	struct kvmppc_vcore *vc;
	struct kvm_vcpu *v;

	trace_kvmppc_run_vcpu_enter(vcpu);

	kvm_run->exit_reason = 0;
	vcpu->arch.ret = RESUME_GUEST;
	vcpu->arch.trap = 0;
	kvmppc_update_vpas(vcpu);

	/*
	 * Synchronize with other threads in this virtual core
	 */
	vc = vcpu->arch.vcore;
	spin_lock(&vc->lock);
	vcpu->arch.ceded = 0;
	vcpu->arch.run_task = current;
	vcpu->arch.kvm_run = kvm_run;
	vcpu->arch.stolen_logged = vcore_stolen_time(vc, mftb());
	vcpu->arch.state = KVMPPC_VCPU_RUNNABLE;
	vcpu->arch.busy_preempt = TB_NIL;
	WRITE_ONCE(vc->runnable_threads[vcpu->arch.ptid], vcpu);
	++vc->n_runnable;

	/*
	 * This happens the first time this is called for a vcpu.
	 * If the vcore is already running, we may be able to start
	 * this thread straight away and have it join in.
	 */
	if (!signal_pending(current)) {
		if ((vc->vcore_state == VCORE_PIGGYBACK ||
		     vc->vcore_state == VCORE_RUNNING) &&
			   !VCORE_IS_EXITING(vc)) {
			kvmppc_create_dtl_entry(vcpu, vc);
			kvmppc_start_thread(vcpu, vc);
			trace_kvm_guest_enter(vcpu);
		} else if (vc->vcore_state == VCORE_SLEEPING) {
			swake_up_one(&vc->wq);
		}

	}

	while (vcpu->arch.state == KVMPPC_VCPU_RUNNABLE &&
	       !signal_pending(current)) {
		/* See if the MMU is ready to go */
		if (!vcpu->kvm->arch.mmu_ready) {
			spin_unlock(&vc->lock);
			r = kvmhv_setup_mmu(vcpu);
			spin_lock(&vc->lock);
			if (r) {
				kvm_run->exit_reason = KVM_EXIT_FAIL_ENTRY;
				kvm_run->fail_entry.
					hardware_entry_failure_reason = 0;
				vcpu->arch.ret = r;
				break;
			}
		}

		if (vc->vcore_state == VCORE_PREEMPT && vc->runner == NULL)
			kvmppc_vcore_end_preempt(vc);

		if (vc->vcore_state != VCORE_INACTIVE) {
			kvmppc_wait_for_exec(vc, vcpu, TASK_INTERRUPTIBLE);
			continue;
		}
		for_each_runnable_thread(i, v, vc) {
			kvmppc_core_prepare_to_enter(v);
			if (signal_pending(v->arch.run_task)) {
				kvmppc_remove_runnable(vc, v);
				v->stat.signal_exits++;
				v->arch.kvm_run->exit_reason = KVM_EXIT_INTR;
				v->arch.ret = -EINTR;
				wake_up(&v->arch.cpu_run);
			}
		}
		if (!vc->n_runnable || vcpu->arch.state != KVMPPC_VCPU_RUNNABLE)
			break;
		n_ceded = 0;
		for_each_runnable_thread(i, v, vc) {
			if (!kvmppc_vcpu_woken(v))
				n_ceded += v->arch.ceded;
			else
				v->arch.ceded = 0;
		}
		vc->runner = vcpu;
		if (n_ceded == vc->n_runnable) {
			kvmppc_vcore_blocked(vc);
		} else if (need_resched()) {
			kvmppc_vcore_preempt(vc);
			/* Let something else run */
			cond_resched_lock(&vc->lock);
			if (vc->vcore_state == VCORE_PREEMPT)
				kvmppc_vcore_end_preempt(vc);
		} else {
			kvmppc_run_core(vc);
		}
		vc->runner = NULL;
	}

	while (vcpu->arch.state == KVMPPC_VCPU_RUNNABLE &&
	       (vc->vcore_state == VCORE_RUNNING ||
		vc->vcore_state == VCORE_EXITING ||
		vc->vcore_state == VCORE_PIGGYBACK))
		kvmppc_wait_for_exec(vc, vcpu, TASK_UNINTERRUPTIBLE);

	if (vc->vcore_state == VCORE_PREEMPT && vc->runner == NULL)
		kvmppc_vcore_end_preempt(vc);

	if (vcpu->arch.state == KVMPPC_VCPU_RUNNABLE) {
		kvmppc_remove_runnable(vc, vcpu);
		vcpu->stat.signal_exits++;
		kvm_run->exit_reason = KVM_EXIT_INTR;
		vcpu->arch.ret = -EINTR;
	}

	if (vc->n_runnable && vc->vcore_state == VCORE_INACTIVE) {
		/* Wake up some vcpu to run the core */
		i = -1;
		v = next_runnable_thread(vc, &i);
		wake_up(&v->arch.cpu_run);
	}

	trace_kvmppc_run_vcpu_exit(vcpu, kvm_run);
	spin_unlock(&vc->lock);
	return vcpu->arch.ret;
}

int kvmhv_run_single_vcpu(struct kvm_run *kvm_run,
			  struct kvm_vcpu *vcpu, u64 time_limit,
			  unsigned long lpcr)
{
	int trap, r, pcpu;
	int srcu_idx, lpid;
	struct kvmppc_vcore *vc;
	struct kvm *kvm = vcpu->kvm;
	struct kvm_nested_guest *nested = vcpu->arch.nested;

	trace_kvmppc_run_vcpu_enter(vcpu);

	kvm_run->exit_reason = 0;
	vcpu->arch.ret = RESUME_GUEST;
	vcpu->arch.trap = 0;

	vc = vcpu->arch.vcore;
	vcpu->arch.ceded = 0;
	vcpu->arch.run_task = current;
	vcpu->arch.kvm_run = kvm_run;
	vcpu->arch.stolen_logged = vcore_stolen_time(vc, mftb());
	vcpu->arch.state = KVMPPC_VCPU_RUNNABLE;
	vcpu->arch.busy_preempt = TB_NIL;
	vcpu->arch.last_inst = KVM_INST_FETCH_FAILED;
	vc->runnable_threads[0] = vcpu;
	vc->n_runnable = 1;
	vc->runner = vcpu;

	/* See if the MMU is ready to go */
	if (!kvm->arch.mmu_ready)
		kvmhv_setup_mmu(vcpu);

	if (need_resched())
		cond_resched();

	kvmppc_update_vpas(vcpu);

	init_vcore_to_run(vc);
	vc->preempt_tb = TB_NIL;

	preempt_disable();
	pcpu = smp_processor_id();
	vc->pcpu = pcpu;
	kvmppc_prepare_radix_vcpu(vcpu, pcpu);

	local_irq_disable();
	hard_irq_disable();
	if (signal_pending(current))
		goto sigpend;
	if (lazy_irq_pending() || need_resched() || !kvm->arch.mmu_ready)
		goto out;

	if (!nested) {
		kvmppc_core_prepare_to_enter(vcpu);
		if (vcpu->arch.doorbell_request) {
			vc->dpdes = 1;
			smp_wmb();
			vcpu->arch.doorbell_request = 0;
		}
		if (test_bit(BOOK3S_IRQPRIO_EXTERNAL,
			     &vcpu->arch.pending_exceptions))
			lpcr |= LPCR_MER;
	} else if (vcpu->arch.pending_exceptions ||
		   vcpu->arch.doorbell_request ||
		   xive_interrupt_pending(vcpu)) {
		vcpu->arch.ret = RESUME_HOST;
		goto out;
	}

	kvmppc_clear_host_core(pcpu);

	local_paca->kvm_hstate.tid = 0;
	local_paca->kvm_hstate.napping = 0;
	local_paca->kvm_hstate.kvm_split_mode = NULL;
	kvmppc_start_thread(vcpu, vc);
	kvmppc_create_dtl_entry(vcpu, vc);
	trace_kvm_guest_enter(vcpu);

	vc->vcore_state = VCORE_RUNNING;
	trace_kvmppc_run_core(vc, 0);

	if (cpu_has_feature(CPU_FTR_HVMODE)) {
		lpid = nested ? nested->shadow_lpid : kvm->arch.lpid;
		mtspr(SPRN_LPID, lpid);
		isync();
		kvmppc_check_need_tlb_flush(kvm, pcpu, nested);
	}

	guest_enter_irqoff();

	srcu_idx = srcu_read_lock(&kvm->srcu);

	this_cpu_disable_ftrace();

	/* Tell lockdep that we're about to enable interrupts */
	trace_hardirqs_on();

	trap = kvmhv_p9_guest_entry(vcpu, time_limit, lpcr);
	vcpu->arch.trap = trap;

	trace_hardirqs_off();

	this_cpu_enable_ftrace();

	srcu_read_unlock(&kvm->srcu, srcu_idx);

	if (cpu_has_feature(CPU_FTR_HVMODE)) {
		mtspr(SPRN_LPID, kvm->arch.host_lpid);
		isync();
	}

	set_irq_happened(trap);

	kvmppc_set_host_core(pcpu);

	local_irq_enable();
	guest_exit();

	cpumask_clear_cpu(pcpu, &kvm->arch.cpu_in_guest);

	preempt_enable();

	/*
	 * cancel pending decrementer exception if DEC is now positive, or if
	 * entering a nested guest in which case the decrementer is now owned
	 * by L2 and the L1 decrementer is provided in hdec_expires
	 */
	if (kvmppc_core_pending_dec(vcpu) &&
			((get_tb() < vcpu->arch.dec_expires) ||
			 (trap == BOOK3S_INTERRUPT_SYSCALL &&
			  kvmppc_get_gpr(vcpu, 3) == H_ENTER_NESTED)))
		kvmppc_core_dequeue_dec(vcpu);

	trace_kvm_guest_exit(vcpu);
	r = RESUME_GUEST;
	if (trap) {
		if (!nested)
			r = kvmppc_handle_exit_hv(kvm_run, vcpu, current);
		else
			r = kvmppc_handle_nested_exit(kvm_run, vcpu);
	}
	vcpu->arch.ret = r;

	if (is_kvmppc_resume_guest(r) && vcpu->arch.ceded &&
	    !kvmppc_vcpu_woken(vcpu)) {
		kvmppc_set_timer(vcpu);
		while (vcpu->arch.ceded && !kvmppc_vcpu_woken(vcpu)) {
			if (signal_pending(current)) {
				vcpu->stat.signal_exits++;
				kvm_run->exit_reason = KVM_EXIT_INTR;
				vcpu->arch.ret = -EINTR;
				break;
			}
			spin_lock(&vc->lock);
			kvmppc_vcore_blocked(vc);
			spin_unlock(&vc->lock);
		}
	}
	vcpu->arch.ceded = 0;

	vc->vcore_state = VCORE_INACTIVE;
	trace_kvmppc_run_core(vc, 1);

 done:
	kvmppc_remove_runnable(vc, vcpu);
	trace_kvmppc_run_vcpu_exit(vcpu, kvm_run);

	return vcpu->arch.ret;

 sigpend:
	vcpu->stat.signal_exits++;
	kvm_run->exit_reason = KVM_EXIT_INTR;
	vcpu->arch.ret = -EINTR;
 out:
	local_irq_enable();
	preempt_enable();
	goto done;
}

static int kvmppc_vcpu_run_hv(struct kvm_run *run, struct kvm_vcpu *vcpu)
{
	int r;
	int srcu_idx;
	unsigned long ebb_regs[3] = {};	/* shut up GCC */
	unsigned long user_tar = 0;
	unsigned int user_vrsave;
	struct kvm *kvm;

	if (!vcpu->arch.sane) {
		run->exit_reason = KVM_EXIT_INTERNAL_ERROR;
		return -EINVAL;
	}

	/*
	 * Don't allow entry with a suspended transaction, because
	 * the guest entry/exit code will lose it.
	 * If the guest has TM enabled, save away their TM-related SPRs
	 * (they will get restored by the TM unavailable interrupt).
	 */
#ifdef CONFIG_PPC_TRANSACTIONAL_MEM
	if (cpu_has_feature(CPU_FTR_TM) && current->thread.regs &&
	    (current->thread.regs->msr & MSR_TM)) {
		if (MSR_TM_ACTIVE(current->thread.regs->msr)) {
			run->exit_reason = KVM_EXIT_FAIL_ENTRY;
			run->fail_entry.hardware_entry_failure_reason = 0;
			return -EINVAL;
		}
		/* Enable TM so we can read the TM SPRs */
		mtmsr(mfmsr() | MSR_TM);
		current->thread.tm_tfhar = mfspr(SPRN_TFHAR);
		current->thread.tm_tfiar = mfspr(SPRN_TFIAR);
		current->thread.tm_texasr = mfspr(SPRN_TEXASR);
		current->thread.regs->msr &= ~MSR_TM;
	}
#endif

	/*
	 * Force online to 1 for the sake of old userspace which doesn't
	 * set it.
	 */
	if (!vcpu->arch.online) {
		atomic_inc(&vcpu->arch.vcore->online_count);
		vcpu->arch.online = 1;
	}

	kvmppc_core_prepare_to_enter(vcpu);

	/* No need to go into the guest when all we'll do is come back out */
	if (signal_pending(current)) {
		run->exit_reason = KVM_EXIT_INTR;
		return -EINTR;
	}

	kvm = vcpu->kvm;
	atomic_inc(&kvm->arch.vcpus_running);
	/* Order vcpus_running vs. mmu_ready, see kvmppc_alloc_reset_hpt */
	smp_mb();

	flush_all_to_thread(current);

	/* Save userspace EBB and other register values */
	if (cpu_has_feature(CPU_FTR_ARCH_207S)) {
		ebb_regs[0] = mfspr(SPRN_EBBHR);
		ebb_regs[1] = mfspr(SPRN_EBBRR);
		ebb_regs[2] = mfspr(SPRN_BESCR);
		user_tar = mfspr(SPRN_TAR);
	}
	user_vrsave = mfspr(SPRN_VRSAVE);

	vcpu->arch.wqp = &vcpu->arch.vcore->wq;
	vcpu->arch.pgdir = current->mm->pgd;
	vcpu->arch.state = KVMPPC_VCPU_BUSY_IN_HOST;

	do {
		/*
		 * The early POWER9 chips that can't mix radix and HPT threads
		 * on the same core also need the workaround for the problem
		 * where the TLB would prefetch entries in the guest exit path
		 * for radix guests using the guest PIDR value and LPID 0.
		 * The workaround is in the old path (kvmppc_run_vcpu())
		 * but not the new path (kvmhv_run_single_vcpu()).
		 */
		if (kvm->arch.threads_indep && kvm_is_radix(kvm) &&
		    !no_mixing_hpt_and_radix)
			r = kvmhv_run_single_vcpu(run, vcpu, ~(u64)0,
						  vcpu->arch.vcore->lpcr);
		else
			r = kvmppc_run_vcpu(run, vcpu);

		if (run->exit_reason == KVM_EXIT_PAPR_HCALL &&
		    !(vcpu->arch.shregs.msr & MSR_PR)) {
			trace_kvm_hcall_enter(vcpu);
			r = kvmppc_pseries_do_hcall(vcpu);
			trace_kvm_hcall_exit(vcpu, r);
			kvmppc_core_prepare_to_enter(vcpu);
		} else if (r == RESUME_PAGE_FAULT) {
			srcu_idx = srcu_read_lock(&kvm->srcu);
			r = kvmppc_book3s_hv_page_fault(run, vcpu,
				vcpu->arch.fault_dar, vcpu->arch.fault_dsisr);
			srcu_read_unlock(&kvm->srcu, srcu_idx);
		} else if (r == RESUME_PASSTHROUGH) {
			if (WARN_ON(xics_on_xive()))
				r = H_SUCCESS;
			else
				r = kvmppc_xics_rm_complete(vcpu, 0);
		}
	} while (is_kvmppc_resume_guest(r));

	/* Restore userspace EBB and other register values */
	if (cpu_has_feature(CPU_FTR_ARCH_207S)) {
		mtspr(SPRN_EBBHR, ebb_regs[0]);
		mtspr(SPRN_EBBRR, ebb_regs[1]);
		mtspr(SPRN_BESCR, ebb_regs[2]);
		mtspr(SPRN_TAR, user_tar);
		mtspr(SPRN_FSCR, current->thread.fscr);
	}
	mtspr(SPRN_VRSAVE, user_vrsave);

	vcpu->arch.state = KVMPPC_VCPU_NOTREADY;
	atomic_dec(&kvm->arch.vcpus_running);
	return r;
}

static void kvmppc_add_seg_page_size(struct kvm_ppc_one_seg_page_size **sps,
				     int shift, int sllp)
{
	(*sps)->page_shift = shift;
	(*sps)->slb_enc = sllp;
	(*sps)->enc[0].page_shift = shift;
	(*sps)->enc[0].pte_enc = kvmppc_pgsize_lp_encoding(shift, shift);
	/*
	 * Add 16MB MPSS support (may get filtered out by userspace)
	 */
	if (shift != 24) {
		int penc = kvmppc_pgsize_lp_encoding(shift, 24);
		if (penc != -1) {
			(*sps)->enc[1].page_shift = 24;
			(*sps)->enc[1].pte_enc = penc;
		}
	}
	(*sps)++;
}

static int kvm_vm_ioctl_get_smmu_info_hv(struct kvm *kvm,
					 struct kvm_ppc_smmu_info *info)
{
	struct kvm_ppc_one_seg_page_size *sps;

	/*
	 * POWER7, POWER8 and POWER9 all support 32 storage keys for data.
	 * POWER7 doesn't support keys for instruction accesses,
	 * POWER8 and POWER9 do.
	 */
	info->data_keys = 32;
	info->instr_keys = cpu_has_feature(CPU_FTR_ARCH_207S) ? 32 : 0;

	/* POWER7, 8 and 9 all have 1T segments and 32-entry SLB */
	info->flags = KVM_PPC_PAGE_SIZES_REAL | KVM_PPC_1T_SEGMENTS;
	info->slb_size = 32;

	/* We only support these sizes for now, and no muti-size segments */
	sps = &info->sps[0];
	kvmppc_add_seg_page_size(&sps, 12, 0);
	kvmppc_add_seg_page_size(&sps, 16, SLB_VSID_L | SLB_VSID_LP_01);
	kvmppc_add_seg_page_size(&sps, 24, SLB_VSID_L);

	/* If running as a nested hypervisor, we don't support HPT guests */
	if (kvmhv_on_pseries())
		info->flags |= KVM_PPC_NO_HASH;

	return 0;
}

/*
 * Get (and clear) the dirty memory log for a memory slot.
 */
static int kvm_vm_ioctl_get_dirty_log_hv(struct kvm *kvm,
					 struct kvm_dirty_log *log)
{
	struct kvm_memslots *slots;
	struct kvm_memory_slot *memslot;
	int i, r;
	unsigned long n;
	unsigned long *buf, *p;
	struct kvm_vcpu *vcpu;

	mutex_lock(&kvm->slots_lock);

	r = -EINVAL;
	if (log->slot >= KVM_USER_MEM_SLOTS)
		goto out;

	slots = kvm_memslots(kvm);
	memslot = id_to_memslot(slots, log->slot);
	r = -ENOENT;
	if (!memslot->dirty_bitmap)
		goto out;

	/*
	 * Use second half of bitmap area because both HPT and radix
	 * accumulate bits in the first half.
	 */
	n = kvm_dirty_bitmap_bytes(memslot);
	buf = memslot->dirty_bitmap + n / sizeof(long);
	memset(buf, 0, n);

	if (kvm_is_radix(kvm))
		r = kvmppc_hv_get_dirty_log_radix(kvm, memslot, buf);
	else
		r = kvmppc_hv_get_dirty_log_hpt(kvm, memslot, buf);
	if (r)
		goto out;

	/*
	 * We accumulate dirty bits in the first half of the
	 * memslot's dirty_bitmap area, for when pages are paged
	 * out or modified by the host directly.  Pick up these
	 * bits and add them to the map.
	 */
	p = memslot->dirty_bitmap;
	for (i = 0; i < n / sizeof(long); ++i)
		buf[i] |= xchg(&p[i], 0);

	/* Harvest dirty bits from VPA and DTL updates */
	/* Note: we never modify the SLB shadow buffer areas */
	kvm_for_each_vcpu(i, vcpu, kvm) {
		spin_lock(&vcpu->arch.vpa_update_lock);
		kvmppc_harvest_vpa_dirty(&vcpu->arch.vpa, memslot, buf);
		kvmppc_harvest_vpa_dirty(&vcpu->arch.dtl, memslot, buf);
		spin_unlock(&vcpu->arch.vpa_update_lock);
	}

	r = -EFAULT;
	if (copy_to_user(log->dirty_bitmap, buf, n))
		goto out;

	r = 0;
out:
	mutex_unlock(&kvm->slots_lock);
	return r;
}

static void kvmppc_core_free_memslot_hv(struct kvm_memory_slot *free,
					struct kvm_memory_slot *dont)
{
	if (!dont || free->arch.rmap != dont->arch.rmap) {
		vfree(free->arch.rmap);
		free->arch.rmap = NULL;
	}
}

static int kvmppc_core_create_memslot_hv(struct kvm_memory_slot *slot,
					 unsigned long npages)
{
	slot->arch.rmap = vzalloc(array_size(npages, sizeof(*slot->arch.rmap)));
	if (!slot->arch.rmap)
		return -ENOMEM;

	return 0;
}

static int kvmppc_core_prepare_memory_region_hv(struct kvm *kvm,
					struct kvm_memory_slot *memslot,
					const struct kvm_userspace_memory_region *mem)
{
	return 0;
}

static void kvmppc_core_commit_memory_region_hv(struct kvm *kvm,
				const struct kvm_userspace_memory_region *mem,
				const struct kvm_memory_slot *old,
				const struct kvm_memory_slot *new,
				enum kvm_mr_change change)
{
	unsigned long npages = mem->memory_size >> PAGE_SHIFT;

	/*
	 * If we are making a new memslot, it might make
	 * some address that was previously cached as emulated
	 * MMIO be no longer emulated MMIO, so invalidate
	 * all the caches of emulated MMIO translations.
	 */
	if (npages)
		atomic64_inc(&kvm->arch.mmio_update);

	/*
	 * For change == KVM_MR_MOVE or KVM_MR_DELETE, higher levels
	 * have already called kvm_arch_flush_shadow_memslot() to
	 * flush shadow mappings.  For KVM_MR_CREATE we have no
	 * previous mappings.  So the only case to handle is
	 * KVM_MR_FLAGS_ONLY when the KVM_MEM_LOG_DIRTY_PAGES bit
	 * has been changed.
	 * For radix guests, we flush on setting KVM_MEM_LOG_DIRTY_PAGES
	 * to get rid of any THP PTEs in the partition-scoped page tables
	 * so we can track dirtiness at the page level; we flush when
	 * clearing KVM_MEM_LOG_DIRTY_PAGES so that we can go back to
	 * using THP PTEs.
	 */
	if (change == KVM_MR_FLAGS_ONLY && kvm_is_radix(kvm) &&
	    ((new->flags ^ old->flags) & KVM_MEM_LOG_DIRTY_PAGES))
		kvmppc_radix_flush_memslot(kvm, old);
	/*
	 * If UV hasn't yet called H_SVM_INIT_START, don't register memslots.
	 */
	if (!kvm->arch.secure_guest)
		return;

	switch (change) {
	case KVM_MR_CREATE:
		if (kvmppc_uvmem_slot_init(kvm, new))
			return;
		uv_register_mem_slot(kvm->arch.lpid,
				     new->base_gfn << PAGE_SHIFT,
				     new->npages * PAGE_SIZE,
				     0, new->id);
		break;
	case KVM_MR_DELETE:
		uv_unregister_mem_slot(kvm->arch.lpid, old->id);
		kvmppc_uvmem_slot_free(kvm, old);
		break;
	default:
		/* TODO: Handle KVM_MR_MOVE */
		break;
	}
}

/*
 * Update LPCR values in kvm->arch and in vcores.
 * Caller must hold kvm->arch.mmu_setup_lock (for mutual exclusion
 * of kvm->arch.lpcr update).
 */
void kvmppc_update_lpcr(struct kvm *kvm, unsigned long lpcr, unsigned long mask)
{
	long int i;
	u32 cores_done = 0;

	if ((kvm->arch.lpcr & mask) == lpcr)
		return;

	kvm->arch.lpcr = (kvm->arch.lpcr & ~mask) | lpcr;

	for (i = 0; i < KVM_MAX_VCORES; ++i) {
		struct kvmppc_vcore *vc = kvm->arch.vcores[i];
		if (!vc)
			continue;
		spin_lock(&vc->lock);
		vc->lpcr = (vc->lpcr & ~mask) | lpcr;
		spin_unlock(&vc->lock);
		if (++cores_done >= kvm->arch.online_vcores)
			break;
	}
}

static void kvmppc_mmu_destroy_hv(struct kvm_vcpu *vcpu)
{
	return;
}

void kvmppc_setup_partition_table(struct kvm *kvm)
{
	unsigned long dw0, dw1;

	if (!kvm_is_radix(kvm)) {
		/* PS field - page size for VRMA */
		dw0 = ((kvm->arch.vrma_slb_v & SLB_VSID_L) >> 1) |
			((kvm->arch.vrma_slb_v & SLB_VSID_LP) << 1);
		/* HTABSIZE and HTABORG fields */
		dw0 |= kvm->arch.sdr1;

		/* Second dword as set by userspace */
		dw1 = kvm->arch.process_table;
	} else {
		dw0 = PATB_HR | radix__get_tree_size() |
			__pa(kvm->arch.pgtable) | RADIX_PGD_INDEX_SIZE;
		dw1 = PATB_GR | kvm->arch.process_table;
	}
	kvmhv_set_ptbl_entry(kvm->arch.lpid, dw0, dw1);
}

/*
 * Set up HPT (hashed page table) and RMA (real-mode area).
 * Must be called with kvm->arch.mmu_setup_lock held.
 */
static int kvmppc_hv_setup_htab_rma(struct kvm_vcpu *vcpu)
{
	int err = 0;
	struct kvm *kvm = vcpu->kvm;
	unsigned long hva;
	struct kvm_memory_slot *memslot;
	struct vm_area_struct *vma;
	unsigned long lpcr = 0, senc;
	unsigned long psize, porder;
	int srcu_idx;

	/* Allocate hashed page table (if not done already) and reset it */
	if (!kvm->arch.hpt.virt) {
		int order = KVM_DEFAULT_HPT_ORDER;
		struct kvm_hpt_info info;

		err = kvmppc_allocate_hpt(&info, order);
		/* If we get here, it means userspace didn't specify a
		 * size explicitly.  So, try successively smaller
		 * sizes if the default failed. */
		while ((err == -ENOMEM) && --order >= PPC_MIN_HPT_ORDER)
			err  = kvmppc_allocate_hpt(&info, order);

		if (err < 0) {
			pr_err("KVM: Couldn't alloc HPT\n");
			goto out;
		}

		kvmppc_set_hpt(kvm, &info);
	}

	/* Look up the memslot for guest physical address 0 */
	srcu_idx = srcu_read_lock(&kvm->srcu);
	memslot = gfn_to_memslot(kvm, 0);

	/* We must have some memory at 0 by now */
	err = -EINVAL;
	if (!memslot || (memslot->flags & KVM_MEMSLOT_INVALID))
		goto out_srcu;

	/* Look up the VMA for the start of this memory slot */
	hva = memslot->userspace_addr;
	down_read(&current->mm->mmap_sem);
	vma = find_vma(current->mm, hva);
	if (!vma || vma->vm_start > hva || (vma->vm_flags & VM_IO))
		goto up_out;

	psize = vma_kernel_pagesize(vma);

	up_read(&current->mm->mmap_sem);

	/* We can handle 4k, 64k or 16M pages in the VRMA */
	if (psize >= 0x1000000)
		psize = 0x1000000;
	else if (psize >= 0x10000)
		psize = 0x10000;
	else
		psize = 0x1000;
	porder = __ilog2(psize);

	senc = slb_pgsize_encoding(psize);
	kvm->arch.vrma_slb_v = senc | SLB_VSID_B_1T |
		(VRMA_VSID << SLB_VSID_SHIFT_1T);
	/* Create HPTEs in the hash page table for the VRMA */
	kvmppc_map_vrma(vcpu, memslot, porder);

	/* Update VRMASD field in the LPCR */
	if (!cpu_has_feature(CPU_FTR_ARCH_300)) {
		/* the -4 is to account for senc values starting at 0x10 */
		lpcr = senc << (LPCR_VRMASD_SH - 4);
		kvmppc_update_lpcr(kvm, lpcr, LPCR_VRMASD);
	}

	/* Order updates to kvm->arch.lpcr etc. vs. mmu_ready */
	smp_wmb();
	err = 0;
 out_srcu:
	srcu_read_unlock(&kvm->srcu, srcu_idx);
 out:
	return err;

 up_out:
	up_read(&current->mm->mmap_sem);
	goto out_srcu;
}

/*
 * Must be called with kvm->arch.mmu_setup_lock held and
 * mmu_ready = 0 and no vcpus running.
 */
int kvmppc_switch_mmu_to_hpt(struct kvm *kvm)
{
	if (nesting_enabled(kvm))
		kvmhv_release_all_nested(kvm);
	kvmppc_rmap_reset(kvm);
	kvm->arch.process_table = 0;
	/* Mutual exclusion with kvm_unmap_hva_range etc. */
	spin_lock(&kvm->mmu_lock);
	kvm->arch.radix = 0;
	spin_unlock(&kvm->mmu_lock);
	kvmppc_free_radix(kvm);
	kvmppc_update_lpcr(kvm, LPCR_VPM1,
			   LPCR_VPM1 | LPCR_UPRT | LPCR_GTSE | LPCR_HR);
	return 0;
}

/*
 * Must be called with kvm->arch.mmu_setup_lock held and
 * mmu_ready = 0 and no vcpus running.
 */
int kvmppc_switch_mmu_to_radix(struct kvm *kvm)
{
	int err;

	err = kvmppc_init_vm_radix(kvm);
	if (err)
		return err;
	kvmppc_rmap_reset(kvm);
	/* Mutual exclusion with kvm_unmap_hva_range etc. */
	spin_lock(&kvm->mmu_lock);
	kvm->arch.radix = 1;
	spin_unlock(&kvm->mmu_lock);
	kvmppc_free_hpt(&kvm->arch.hpt);
	kvmppc_update_lpcr(kvm, LPCR_UPRT | LPCR_GTSE | LPCR_HR,
			   LPCR_VPM1 | LPCR_UPRT | LPCR_GTSE | LPCR_HR);
	return 0;
}

#ifdef CONFIG_KVM_XICS
/*
 * Allocate a per-core structure for managing state about which cores are
 * running in the host versus the guest and for exchanging data between
 * real mode KVM and CPU running in the host.
 * This is only done for the first VM.
 * The allocated structure stays even if all VMs have stopped.
 * It is only freed when the kvm-hv module is unloaded.
 * It's OK for this routine to fail, we just don't support host
 * core operations like redirecting H_IPI wakeups.
 */
void kvmppc_alloc_host_rm_ops(void)
{
	struct kvmppc_host_rm_ops *ops;
	unsigned long l_ops;
	int cpu, core;
	int size;

	/* Not the first time here ? */
	if (kvmppc_host_rm_ops_hv != NULL)
		return;

	ops = kzalloc(sizeof(struct kvmppc_host_rm_ops), GFP_KERNEL);
	if (!ops)
		return;

	size = cpu_nr_cores() * sizeof(struct kvmppc_host_rm_core);
	ops->rm_core = kzalloc(size, GFP_KERNEL);

	if (!ops->rm_core) {
		kfree(ops);
		return;
	}

	cpus_read_lock();

	for (cpu = 0; cpu < nr_cpu_ids; cpu += threads_per_core) {
		if (!cpu_online(cpu))
			continue;

		core = cpu >> threads_shift;
		ops->rm_core[core].rm_state.in_host = 1;
	}

	ops->vcpu_kick = kvmppc_fast_vcpu_kick_hv;

	/*
	 * Make the contents of the kvmppc_host_rm_ops structure visible
	 * to other CPUs before we assign it to the global variable.
	 * Do an atomic assignment (no locks used here), but if someone
	 * beats us to it, just free our copy and return.
	 */
	smp_wmb();
	l_ops = (unsigned long) ops;

	if (cmpxchg64((unsigned long *)&kvmppc_host_rm_ops_hv, 0, l_ops)) {
		cpus_read_unlock();
		kfree(ops->rm_core);
		kfree(ops);
		return;
	}

	cpuhp_setup_state_nocalls_cpuslocked(CPUHP_KVM_PPC_BOOK3S_PREPARE,
					     "ppc/kvm_book3s:prepare",
					     kvmppc_set_host_core,
					     kvmppc_clear_host_core);
	cpus_read_unlock();
}

void kvmppc_free_host_rm_ops(void)
{
	if (kvmppc_host_rm_ops_hv) {
		cpuhp_remove_state_nocalls(CPUHP_KVM_PPC_BOOK3S_PREPARE);
		kfree(kvmppc_host_rm_ops_hv->rm_core);
		kfree(kvmppc_host_rm_ops_hv);
		kvmppc_host_rm_ops_hv = NULL;
	}
}
#endif

static int kvmppc_core_init_vm_hv(struct kvm *kvm)
{
	unsigned long lpcr, lpid;
	char buf[32];
	int ret;

	mutex_init(&kvm->arch.uvmem_lock);
	INIT_LIST_HEAD(&kvm->arch.uvmem_pfns);
	mutex_init(&kvm->arch.mmu_setup_lock);

	/* Allocate the guest's logical partition ID */

	lpid = kvmppc_alloc_lpid();
	if ((long)lpid < 0)
		return -ENOMEM;
	kvm->arch.lpid = lpid;

	kvmppc_alloc_host_rm_ops();

	kvmhv_vm_nested_init(kvm);

	/*
	 * Since we don't flush the TLB when tearing down a VM,
	 * and this lpid might have previously been used,
	 * make sure we flush on each core before running the new VM.
	 * On POWER9, the tlbie in mmu_partition_table_set_entry()
	 * does this flush for us.
	 */
	if (!cpu_has_feature(CPU_FTR_ARCH_300))
		cpumask_setall(&kvm->arch.need_tlb_flush);

	/* Start out with the default set of hcalls enabled */
	memcpy(kvm->arch.enabled_hcalls, default_enabled_hcalls,
	       sizeof(kvm->arch.enabled_hcalls));

	if (!cpu_has_feature(CPU_FTR_ARCH_300))
		kvm->arch.host_sdr1 = mfspr(SPRN_SDR1);

	/* Init LPCR for virtual RMA mode */
	if (cpu_has_feature(CPU_FTR_HVMODE)) {
		kvm->arch.host_lpid = mfspr(SPRN_LPID);
		kvm->arch.host_lpcr = lpcr = mfspr(SPRN_LPCR);
		lpcr &= LPCR_PECE | LPCR_LPES;
	} else {
		lpcr = 0;
	}
	lpcr |= (4UL << LPCR_DPFD_SH) | LPCR_HDICE |
		LPCR_VPM0 | LPCR_VPM1;
	kvm->arch.vrma_slb_v = SLB_VSID_B_1T |
		(VRMA_VSID << SLB_VSID_SHIFT_1T);
	/* On POWER8 turn on online bit to enable PURR/SPURR */
	if (cpu_has_feature(CPU_FTR_ARCH_207S))
		lpcr |= LPCR_ONL;
	/*
	 * On POWER9, VPM0 bit is reserved (VPM0=1 behaviour is assumed)
	 * Set HVICE bit to enable hypervisor virtualization interrupts.
	 * Set HEIC to prevent OS interrupts to go to hypervisor (should
	 * be unnecessary but better safe than sorry in case we re-enable
	 * EE in HV mode with this LPCR still set)
	 */
	if (cpu_has_feature(CPU_FTR_ARCH_300)) {
		lpcr &= ~LPCR_VPM0;
		lpcr |= LPCR_HVICE | LPCR_HEIC;

		/*
		 * If xive is enabled, we route 0x500 interrupts directly
		 * to the guest.
		 */
		if (xics_on_xive())
			lpcr |= LPCR_LPES;
	}

	/*
	 * If the host uses radix, the guest starts out as radix.
	 */
	if (radix_enabled()) {
		kvm->arch.radix = 1;
		kvm->arch.mmu_ready = 1;
		lpcr &= ~LPCR_VPM1;
		lpcr |= LPCR_UPRT | LPCR_GTSE | LPCR_HR;
		ret = kvmppc_init_vm_radix(kvm);
		if (ret) {
			kvmppc_free_lpid(kvm->arch.lpid);
			return ret;
		}
		kvmppc_setup_partition_table(kvm);
	}

	kvm->arch.lpcr = lpcr;

	/* Initialization for future HPT resizes */
	kvm->arch.resize_hpt = NULL;

	/*
	 * Work out how many sets the TLB has, for the use of
	 * the TLB invalidation loop in book3s_hv_rmhandlers.S.
	 */
	if (radix_enabled())
		kvm->arch.tlb_sets = POWER9_TLB_SETS_RADIX;	/* 128 */
	else if (cpu_has_feature(CPU_FTR_ARCH_300))
		kvm->arch.tlb_sets = POWER9_TLB_SETS_HASH;	/* 256 */
	else if (cpu_has_feature(CPU_FTR_ARCH_207S))
		kvm->arch.tlb_sets = POWER8_TLB_SETS;		/* 512 */
	else
		kvm->arch.tlb_sets = POWER7_TLB_SETS;		/* 128 */

	/*
	 * Track that we now have a HV mode VM active. This blocks secondary
	 * CPU threads from coming online.
	 * On POWER9, we only need to do this if the "indep_threads_mode"
	 * module parameter has been set to N.
	 */
	if (cpu_has_feature(CPU_FTR_ARCH_300)) {
		if (!indep_threads_mode && !cpu_has_feature(CPU_FTR_HVMODE)) {
			pr_warn("KVM: Ignoring indep_threads_mode=N in nested hypervisor\n");
			kvm->arch.threads_indep = true;
		} else {
			kvm->arch.threads_indep = indep_threads_mode;
		}
	}
	if (!kvm->arch.threads_indep)
		kvm_hv_vm_activated();

	/*
	 * Initialize smt_mode depending on processor.
	 * POWER8 and earlier have to use "strict" threading, where
	 * all vCPUs in a vcore have to run on the same (sub)core,
	 * whereas on POWER9 the threads can each run a different
	 * guest.
	 */
	if (!cpu_has_feature(CPU_FTR_ARCH_300))
		kvm->arch.smt_mode = threads_per_subcore;
	else
		kvm->arch.smt_mode = 1;
	kvm->arch.emul_smt_mode = 1;

	/*
	 * Create a debugfs directory for the VM
	 */
	snprintf(buf, sizeof(buf), "vm%d", current->pid);
	kvm->arch.debugfs_dir = debugfs_create_dir(buf, kvm_debugfs_dir);
	kvmppc_mmu_debugfs_init(kvm);
	if (radix_enabled())
		kvmhv_radix_debugfs_init(kvm);

	return 0;
}

static void kvmppc_free_vcores(struct kvm *kvm)
{
	long int i;

	for (i = 0; i < KVM_MAX_VCORES; ++i)
		kfree(kvm->arch.vcores[i]);
	kvm->arch.online_vcores = 0;
}

static void kvmppc_core_destroy_vm_hv(struct kvm *kvm)
{
	debugfs_remove_recursive(kvm->arch.debugfs_dir);

	if (!kvm->arch.threads_indep)
		kvm_hv_vm_deactivated();

	kvmppc_free_vcores(kvm);


	if (kvm_is_radix(kvm))
		kvmppc_free_radix(kvm);
	else
		kvmppc_free_hpt(&kvm->arch.hpt);

	/* Perform global invalidation and return lpid to the pool */
	if (cpu_has_feature(CPU_FTR_ARCH_300)) {
		if (nesting_enabled(kvm))
			kvmhv_release_all_nested(kvm);
		kvm->arch.process_table = 0;
		if (kvm->arch.secure_guest)
			uv_svm_terminate(kvm->arch.lpid);
		kvmhv_set_ptbl_entry(kvm->arch.lpid, 0, 0);
	}

	kvmppc_free_lpid(kvm->arch.lpid);

	kvmppc_free_pimap(kvm);
}

/* We don't need to emulate any privileged instructions or dcbz */
static int kvmppc_core_emulate_op_hv(struct kvm_run *run, struct kvm_vcpu *vcpu,
				     unsigned int inst, int *advance)
{
	return EMULATE_FAIL;
}

static int kvmppc_core_emulate_mtspr_hv(struct kvm_vcpu *vcpu, int sprn,
					ulong spr_val)
{
	return EMULATE_FAIL;
}

static int kvmppc_core_emulate_mfspr_hv(struct kvm_vcpu *vcpu, int sprn,
					ulong *spr_val)
{
	return EMULATE_FAIL;
}

static int kvmppc_core_check_processor_compat_hv(void)
{
	if (cpu_has_feature(CPU_FTR_HVMODE) &&
	    cpu_has_feature(CPU_FTR_ARCH_206))
		return 0;

	/* POWER9 in radix mode is capable of being a nested hypervisor. */
	if (cpu_has_feature(CPU_FTR_ARCH_300) && radix_enabled())
		return 0;

	return -EIO;
}

#ifdef CONFIG_KVM_XICS

void kvmppc_free_pimap(struct kvm *kvm)
{
	kfree(kvm->arch.pimap);
}

static struct kvmppc_passthru_irqmap *kvmppc_alloc_pimap(void)
{
	return kzalloc(sizeof(struct kvmppc_passthru_irqmap), GFP_KERNEL);
}

static int kvmppc_set_passthru_irq(struct kvm *kvm, int host_irq, int guest_gsi)
{
	struct irq_desc *desc;
	struct kvmppc_irq_map *irq_map;
	struct kvmppc_passthru_irqmap *pimap;
	struct irq_chip *chip;
	int i, rc = 0;

	if (!kvm_irq_bypass)
		return 1;

	desc = irq_to_desc(host_irq);
	if (!desc)
		return -EIO;

	mutex_lock(&kvm->lock);

	pimap = kvm->arch.pimap;
	if (pimap == NULL) {
		/* First call, allocate structure to hold IRQ map */
		pimap = kvmppc_alloc_pimap();
		if (pimap == NULL) {
			mutex_unlock(&kvm->lock);
			return -ENOMEM;
		}
		kvm->arch.pimap = pimap;
	}

	/*
	 * For now, we only support interrupts for which the EOI operation
	 * is an OPAL call followed by a write to XIRR, since that's
	 * what our real-mode EOI code does, or a XIVE interrupt
	 */
	chip = irq_data_get_irq_chip(&desc->irq_data);
	if (!chip || !(is_pnv_opal_msi(chip) || is_xive_irq(chip))) {
		pr_warn("kvmppc_set_passthru_irq_hv: Could not assign IRQ map for (%d,%d)\n",
			host_irq, guest_gsi);
		mutex_unlock(&kvm->lock);
		return -ENOENT;
	}

	/*
	 * See if we already have an entry for this guest IRQ number.
	 * If it's mapped to a hardware IRQ number, that's an error,
	 * otherwise re-use this entry.
	 */
	for (i = 0; i < pimap->n_mapped; i++) {
		if (guest_gsi == pimap->mapped[i].v_hwirq) {
			if (pimap->mapped[i].r_hwirq) {
				mutex_unlock(&kvm->lock);
				return -EINVAL;
			}
			break;
		}
	}

	if (i == KVMPPC_PIRQ_MAPPED) {
		mutex_unlock(&kvm->lock);
		return -EAGAIN;		/* table is full */
	}

	irq_map = &pimap->mapped[i];

	irq_map->v_hwirq = guest_gsi;
	irq_map->desc = desc;

	/*
	 * Order the above two stores before the next to serialize with
	 * the KVM real mode handler.
	 */
	smp_wmb();
	irq_map->r_hwirq = desc->irq_data.hwirq;

	if (i == pimap->n_mapped)
		pimap->n_mapped++;

	if (xics_on_xive())
		rc = kvmppc_xive_set_mapped(kvm, guest_gsi, desc);
	else
		kvmppc_xics_set_mapped(kvm, guest_gsi, desc->irq_data.hwirq);
	if (rc)
		irq_map->r_hwirq = 0;

	mutex_unlock(&kvm->lock);

	return 0;
}

static int kvmppc_clr_passthru_irq(struct kvm *kvm, int host_irq, int guest_gsi)
{
	struct irq_desc *desc;
	struct kvmppc_passthru_irqmap *pimap;
	int i, rc = 0;

	if (!kvm_irq_bypass)
		return 0;

	desc = irq_to_desc(host_irq);
	if (!desc)
		return -EIO;

	mutex_lock(&kvm->lock);
	if (!kvm->arch.pimap)
		goto unlock;

	pimap = kvm->arch.pimap;

	for (i = 0; i < pimap->n_mapped; i++) {
		if (guest_gsi == pimap->mapped[i].v_hwirq)
			break;
	}

	if (i == pimap->n_mapped) {
		mutex_unlock(&kvm->lock);
		return -ENODEV;
	}

	if (xics_on_xive())
		rc = kvmppc_xive_clr_mapped(kvm, guest_gsi, pimap->mapped[i].desc);
	else
		kvmppc_xics_clr_mapped(kvm, guest_gsi, pimap->mapped[i].r_hwirq);

	/* invalidate the entry (what do do on error from the above ?) */
	pimap->mapped[i].r_hwirq = 0;

	/*
	 * We don't free this structure even when the count goes to
	 * zero. The structure is freed when we destroy the VM.
	 */
 unlock:
	mutex_unlock(&kvm->lock);
	return rc;
}

static int kvmppc_irq_bypass_add_producer_hv(struct irq_bypass_consumer *cons,
					     struct irq_bypass_producer *prod)
{
	int ret = 0;
	struct kvm_kernel_irqfd *irqfd =
		container_of(cons, struct kvm_kernel_irqfd, consumer);

	irqfd->producer = prod;

	ret = kvmppc_set_passthru_irq(irqfd->kvm, prod->irq, irqfd->gsi);
	if (ret)
		pr_info("kvmppc_set_passthru_irq (irq %d, gsi %d) fails: %d\n",
			prod->irq, irqfd->gsi, ret);

	return ret;
}

static void kvmppc_irq_bypass_del_producer_hv(struct irq_bypass_consumer *cons,
					      struct irq_bypass_producer *prod)
{
	int ret;
	struct kvm_kernel_irqfd *irqfd =
		container_of(cons, struct kvm_kernel_irqfd, consumer);

	irqfd->producer = NULL;

	/*
	 * When producer of consumer is unregistered, we change back to
	 * default external interrupt handling mode - KVM real mode
	 * will switch back to host.
	 */
	ret = kvmppc_clr_passthru_irq(irqfd->kvm, prod->irq, irqfd->gsi);
	if (ret)
		pr_warn("kvmppc_clr_passthru_irq (irq %d, gsi %d) fails: %d\n",
			prod->irq, irqfd->gsi, ret);
}
#endif

static long kvm_arch_vm_ioctl_hv(struct file *filp,
				 unsigned int ioctl, unsigned long arg)
{
	struct kvm *kvm __maybe_unused = filp->private_data;
	void __user *argp = (void __user *)arg;
	long r;

	switch (ioctl) {

	case KVM_PPC_ALLOCATE_HTAB: {
		u32 htab_order;

		r = -EFAULT;
		if (get_user(htab_order, (u32 __user *)argp))
			break;
		r = kvmppc_alloc_reset_hpt(kvm, htab_order);
		if (r)
			break;
		r = 0;
		break;
	}

	case KVM_PPC_GET_HTAB_FD: {
		struct kvm_get_htab_fd ghf;

		r = -EFAULT;
		if (copy_from_user(&ghf, argp, sizeof(ghf)))
			break;
		r = kvm_vm_ioctl_get_htab_fd(kvm, &ghf);
		break;
	}

	case KVM_PPC_RESIZE_HPT_PREPARE: {
		struct kvm_ppc_resize_hpt rhpt;

		r = -EFAULT;
		if (copy_from_user(&rhpt, argp, sizeof(rhpt)))
			break;

		r = kvm_vm_ioctl_resize_hpt_prepare(kvm, &rhpt);
		break;
	}

	case KVM_PPC_RESIZE_HPT_COMMIT: {
		struct kvm_ppc_resize_hpt rhpt;

		r = -EFAULT;
		if (copy_from_user(&rhpt, argp, sizeof(rhpt)))
			break;

		r = kvm_vm_ioctl_resize_hpt_commit(kvm, &rhpt);
		break;
	}

	default:
		r = -ENOTTY;
	}

	return r;
}

/*
 * List of hcall numbers to enable by default.
 * For compatibility with old userspace, we enable by default
 * all hcalls that were implemented before the hcall-enabling
 * facility was added.  Note this list should not include H_RTAS.
 */
static unsigned int default_hcall_list[] = {
	H_REMOVE,
	H_ENTER,
	H_READ,
	H_PROTECT,
	H_BULK_REMOVE,
	H_GET_TCE,
	H_PUT_TCE,
	H_SET_DABR,
	H_SET_XDABR,
	H_CEDE,
	H_PROD,
	H_CONFER,
	H_REGISTER_VPA,
#ifdef CONFIG_KVM_XICS
	H_EOI,
	H_CPPR,
	H_IPI,
	H_IPOLL,
	H_XIRR,
	H_XIRR_X,
#endif
	0
};

static void init_default_hcalls(void)
{
	int i;
	unsigned int hcall;

	for (i = 0; default_hcall_list[i]; ++i) {
		hcall = default_hcall_list[i];
		WARN_ON(!kvmppc_hcall_impl_hv(hcall));
		__set_bit(hcall / 4, default_enabled_hcalls);
	}
}

static int kvmhv_configure_mmu(struct kvm *kvm, struct kvm_ppc_mmuv3_cfg *cfg)
{
	unsigned long lpcr;
	int radix;
	int err;

	/* If not on a POWER9, reject it */
	if (!cpu_has_feature(CPU_FTR_ARCH_300))
		return -ENODEV;

	/* If any unknown flags set, reject it */
	if (cfg->flags & ~(KVM_PPC_MMUV3_RADIX | KVM_PPC_MMUV3_GTSE))
		return -EINVAL;

	/* GR (guest radix) bit in process_table field must match */
	radix = !!(cfg->flags & KVM_PPC_MMUV3_RADIX);
	if (!!(cfg->process_table & PATB_GR) != radix)
		return -EINVAL;

	/* Process table size field must be reasonable, i.e. <= 24 */
	if ((cfg->process_table & PRTS_MASK) > 24)
		return -EINVAL;

	/* We can change a guest to/from radix now, if the host is radix */
	if (radix && !radix_enabled())
		return -EINVAL;

	/* If we're a nested hypervisor, we currently only support radix */
	if (kvmhv_on_pseries() && !radix)
		return -EINVAL;

	mutex_lock(&kvm->arch.mmu_setup_lock);
	if (radix != kvm_is_radix(kvm)) {
		if (kvm->arch.mmu_ready) {
			kvm->arch.mmu_ready = 0;
			/* order mmu_ready vs. vcpus_running */
			smp_mb();
			if (atomic_read(&kvm->arch.vcpus_running)) {
				kvm->arch.mmu_ready = 1;
				err = -EBUSY;
				goto out_unlock;
			}
		}
		if (radix)
			err = kvmppc_switch_mmu_to_radix(kvm);
		else
			err = kvmppc_switch_mmu_to_hpt(kvm);
		if (err)
			goto out_unlock;
	}

	kvm->arch.process_table = cfg->process_table;
	kvmppc_setup_partition_table(kvm);

	lpcr = (cfg->flags & KVM_PPC_MMUV3_GTSE) ? LPCR_GTSE : 0;
	kvmppc_update_lpcr(kvm, lpcr, LPCR_GTSE);
	err = 0;

 out_unlock:
	mutex_unlock(&kvm->arch.mmu_setup_lock);
	return err;
}

static int kvmhv_enable_nested(struct kvm *kvm)
{
	if (!nested)
		return -EPERM;
	if (!cpu_has_feature(CPU_FTR_ARCH_300) || no_mixing_hpt_and_radix)
		return -ENODEV;

	/* kvm == NULL means the caller is testing if the capability exists */
	if (kvm)
		kvm->arch.nested_enable = true;
	return 0;
}

static int kvmhv_load_from_eaddr(struct kvm_vcpu *vcpu, ulong *eaddr, void *ptr,
				 int size)
{
	int rc = -EINVAL;

	if (kvmhv_vcpu_is_radix(vcpu)) {
		rc = kvmhv_copy_from_guest_radix(vcpu, *eaddr, ptr, size);

		if (rc > 0)
			rc = -EINVAL;
	}

	/* For now quadrants are the only way to access nested guest memory */
	if (rc && vcpu->arch.nested)
		rc = -EAGAIN;

	return rc;
}

static int kvmhv_store_to_eaddr(struct kvm_vcpu *vcpu, ulong *eaddr, void *ptr,
				int size)
{
	int rc = -EINVAL;

	if (kvmhv_vcpu_is_radix(vcpu)) {
		rc = kvmhv_copy_to_guest_radix(vcpu, *eaddr, ptr, size);

		if (rc > 0)
			rc = -EINVAL;
	}

	/* For now quadrants are the only way to access nested guest memory */
	if (rc && vcpu->arch.nested)
		rc = -EAGAIN;

	return rc;
}

static void unpin_vpa_reset(struct kvm *kvm, struct kvmppc_vpa *vpa)
{
	unpin_vpa(kvm, vpa);
	vpa->gpa = 0;
	vpa->pinned_addr = NULL;
	vpa->dirty = false;
	vpa->update_pending = 0;
}

/*
 *  IOCTL handler to turn off secure mode of guest
 *
 * - Release all device pages
 * - Issue ucall to terminate the guest on the UV side
 * - Unpin the VPA pages.
 * - Reinit the partition scoped page tables
 */
static int kvmhv_svm_off(struct kvm *kvm)
{
	struct kvm_vcpu *vcpu;
	int mmu_was_ready;
	int srcu_idx;
	int ret = 0;
	int i;

	if (!(kvm->arch.secure_guest & KVMPPC_SECURE_INIT_START))
		return ret;

	mutex_lock(&kvm->arch.mmu_setup_lock);
	mmu_was_ready = kvm->arch.mmu_ready;
	if (kvm->arch.mmu_ready) {
		kvm->arch.mmu_ready = 0;
		/* order mmu_ready vs. vcpus_running */
		smp_mb();
		if (atomic_read(&kvm->arch.vcpus_running)) {
			kvm->arch.mmu_ready = 1;
			ret = -EBUSY;
			goto out;
		}
	}

	srcu_idx = srcu_read_lock(&kvm->srcu);
	for (i = 0; i < KVM_ADDRESS_SPACE_NUM; i++) {
		struct kvm_memory_slot *memslot;
		struct kvm_memslots *slots = __kvm_memslots(kvm, i);

		if (!slots)
			continue;

		kvm_for_each_memslot(memslot, slots) {
			kvmppc_uvmem_drop_pages(memslot, kvm);
			uv_unregister_mem_slot(kvm->arch.lpid, memslot->id);
		}
	}
	srcu_read_unlock(&kvm->srcu, srcu_idx);

	ret = uv_svm_terminate(kvm->arch.lpid);
	if (ret != U_SUCCESS) {
		ret = -EINVAL;
		goto out;
	}

	/*
	 * When secure guest is reset, all the guest pages are sent
	 * to UV via UV_PAGE_IN before the non-boot vcpus get a
	 * chance to run and unpin their VPA pages. Unpinning of all
	 * VPA pages is done here explicitly so that VPA pages
	 * can be migrated to the secure side.
	 *
	 * This is required to for the secure SMP guest to reboot
	 * correctly.
	 */
	kvm_for_each_vcpu(i, vcpu, kvm) {
		spin_lock(&vcpu->arch.vpa_update_lock);
		unpin_vpa_reset(kvm, &vcpu->arch.dtl);
		unpin_vpa_reset(kvm, &vcpu->arch.slb_shadow);
		unpin_vpa_reset(kvm, &vcpu->arch.vpa);
		spin_unlock(&vcpu->arch.vpa_update_lock);
	}

	kvmppc_setup_partition_table(kvm);
	kvm->arch.secure_guest = 0;
	kvm->arch.mmu_ready = mmu_was_ready;
out:
	mutex_unlock(&kvm->arch.mmu_setup_lock);
	return ret;
}

static struct kvmppc_ops kvm_ops_hv = {
	.get_sregs = kvm_arch_vcpu_ioctl_get_sregs_hv,
	.set_sregs = kvm_arch_vcpu_ioctl_set_sregs_hv,
	.get_one_reg = kvmppc_get_one_reg_hv,
	.set_one_reg = kvmppc_set_one_reg_hv,
	.vcpu_load   = kvmppc_core_vcpu_load_hv,
	.vcpu_put    = kvmppc_core_vcpu_put_hv,
	.inject_interrupt = kvmppc_inject_interrupt_hv,
	.set_msr     = kvmppc_set_msr_hv,
	.vcpu_run    = kvmppc_vcpu_run_hv,
	.vcpu_create = kvmppc_core_vcpu_create_hv,
	.vcpu_free   = kvmppc_core_vcpu_free_hv,
	.check_requests = kvmppc_core_check_requests_hv,
	.get_dirty_log  = kvm_vm_ioctl_get_dirty_log_hv,
	.flush_memslot  = kvmppc_core_flush_memslot_hv,
	.prepare_memory_region = kvmppc_core_prepare_memory_region_hv,
	.commit_memory_region  = kvmppc_core_commit_memory_region_hv,
	.unmap_hva_range = kvm_unmap_hva_range_hv,
	.age_hva  = kvm_age_hva_hv,
	.test_age_hva = kvm_test_age_hva_hv,
	.set_spte_hva = kvm_set_spte_hva_hv,
	.mmu_destroy  = kvmppc_mmu_destroy_hv,
	.free_memslot = kvmppc_core_free_memslot_hv,
	.create_memslot = kvmppc_core_create_memslot_hv,
	.init_vm =  kvmppc_core_init_vm_hv,
	.destroy_vm = kvmppc_core_destroy_vm_hv,
	.get_smmu_info = kvm_vm_ioctl_get_smmu_info_hv,
	.emulate_op = kvmppc_core_emulate_op_hv,
	.emulate_mtspr = kvmppc_core_emulate_mtspr_hv,
	.emulate_mfspr = kvmppc_core_emulate_mfspr_hv,
	.fast_vcpu_kick = kvmppc_fast_vcpu_kick_hv,
	.arch_vm_ioctl  = kvm_arch_vm_ioctl_hv,
	.hcall_implemented = kvmppc_hcall_impl_hv,
#ifdef CONFIG_KVM_XICS
	.irq_bypass_add_producer = kvmppc_irq_bypass_add_producer_hv,
	.irq_bypass_del_producer = kvmppc_irq_bypass_del_producer_hv,
#endif
	.configure_mmu = kvmhv_configure_mmu,
	.get_rmmu_info = kvmhv_get_rmmu_info,
	.set_smt_mode = kvmhv_set_smt_mode,
	.enable_nested = kvmhv_enable_nested,
	.load_from_eaddr = kvmhv_load_from_eaddr,
	.store_to_eaddr = kvmhv_store_to_eaddr,
	.svm_off = kvmhv_svm_off,
};

static int kvm_init_subcore_bitmap(void)
{
	int i, j;
	int nr_cores = cpu_nr_cores();
	struct sibling_subcore_state *sibling_subcore_state;

	for (i = 0; i < nr_cores; i++) {
		int first_cpu = i * threads_per_core;
		int node = cpu_to_node(first_cpu);

		/* Ignore if it is already allocated. */
		if (paca_ptrs[first_cpu]->sibling_subcore_state)
			continue;

		sibling_subcore_state =
			kzalloc_node(sizeof(struct sibling_subcore_state),
							GFP_KERNEL, node);
		if (!sibling_subcore_state)
			return -ENOMEM;


		for (j = 0; j < threads_per_core; j++) {
			int cpu = first_cpu + j;

			paca_ptrs[cpu]->sibling_subcore_state =
						sibling_subcore_state;
		}
	}
	return 0;
}

static int kvmppc_radix_possible(void)
{
	return cpu_has_feature(CPU_FTR_ARCH_300) && radix_enabled();
}

static int kvmppc_book3s_init_hv(void)
{
	int r;

	if (!tlbie_capable) {
		pr_err("KVM-HV: Host does not support TLBIE\n");
		return -ENODEV;
	}

	/*
	 * FIXME!! Do we need to check on all cpus ?
	 */
	r = kvmppc_core_check_processor_compat_hv();
	if (r < 0)
		return -ENODEV;

	r = kvmhv_nested_init();
	if (r)
		return r;

	r = kvm_init_subcore_bitmap();
	if (r)
		return r;

	/*
	 * We need a way of accessing the XICS interrupt controller,
	 * either directly, via paca_ptrs[cpu]->kvm_hstate.xics_phys, or
	 * indirectly, via OPAL.
	 */
#ifdef CONFIG_SMP
	if (!xics_on_xive() && !kvmhv_on_pseries() &&
	    !local_paca->kvm_hstate.xics_phys) {
		struct device_node *np;

		np = of_find_compatible_node(NULL, NULL, "ibm,opal-intc");
		if (!np) {
			pr_err("KVM-HV: Cannot determine method for accessing XICS\n");
			return -ENODEV;
		}
		/* presence of intc confirmed - node can be dropped again */
		of_node_put(np);
	}
#endif

	kvm_ops_hv.owner = THIS_MODULE;
	kvmppc_hv_ops = &kvm_ops_hv;

	init_default_hcalls();

	init_vcore_lists();

	r = kvmppc_mmu_hv_init();
	if (r)
		return r;

	if (kvmppc_radix_possible())
		r = kvmppc_radix_init();

	/*
	 * POWER9 chips before version 2.02 can't have some threads in
	 * HPT mode and some in radix mode on the same core.
	 */
	if (cpu_has_feature(CPU_FTR_ARCH_300)) {
		unsigned int pvr = mfspr(SPRN_PVR);
		if ((pvr >> 16) == PVR_POWER9 &&
		    (((pvr & 0xe000) == 0 && (pvr & 0xfff) < 0x202) ||
		     ((pvr & 0xe000) == 0x2000 && (pvr & 0xfff) < 0x101)))
			no_mixing_hpt_and_radix = true;
	}

	r = kvmppc_uvmem_init();
	if (r < 0)
		pr_err("KVM-HV: kvmppc_uvmem_init failed %d\n", r);

	return r;
}

static void kvmppc_book3s_exit_hv(void)
{
	kvmppc_uvmem_free();
	kvmppc_free_host_rm_ops();
	if (kvmppc_radix_possible())
		kvmppc_radix_exit();
	kvmppc_hv_ops = NULL;
	kvmhv_nested_exit();
}

module_init(kvmppc_book3s_init_hv);
module_exit(kvmppc_book3s_exit_hv);
MODULE_LICENSE("GPL");
MODULE_ALIAS_MISCDEV(KVM_MINOR);
MODULE_ALIAS("devname:kvm");
