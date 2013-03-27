/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 * Copyright IBM Corp. 2007
 *
 * Authors: Hollis Blanchard <hollisb@us.ibm.com>
 *          Christian Ehrhardt <ehrhardt@linux.vnet.ibm.com>
 */

#include <linux/errno.h>
#include <linux/err.h>
#include <linux/kvm_host.h>
#include <linux/vmalloc.h>
#include <linux/hrtimer.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <asm/cputable.h>
#include <asm/uaccess.h>
#include <asm/kvm_ppc.h>
#include <asm/tlbflush.h>
#include <asm/cputhreads.h>
#include <asm/irqflags.h>
#include "timing.h"
#include "../mm/mmu_decl.h"

#define CREATE_TRACE_POINTS
#include "trace.h"

int kvm_arch_vcpu_runnable(struct kvm_vcpu *v)
{
	return !!(v->arch.pending_exceptions) ||
	       v->requests;
}

int kvm_arch_vcpu_should_kick(struct kvm_vcpu *vcpu)
{
	return 1;
}

#ifndef CONFIG_KVM_BOOK3S_64_HV
/*
 * Common checks before entering the guest world.  Call with interrupts
 * disabled.
 *
 * returns:
 *
 * == 1 if we're ready to go into guest state
 * <= 0 if we need to go back to the host with return value
 */
int kvmppc_prepare_to_enter(struct kvm_vcpu *vcpu)
{
	int r = 1;

	WARN_ON_ONCE(!irqs_disabled());
	while (true) {
		if (need_resched()) {
			local_irq_enable();
			cond_resched();
			local_irq_disable();
			continue;
		}

		if (signal_pending(current)) {
			kvmppc_account_exit(vcpu, SIGNAL_EXITS);
			vcpu->run->exit_reason = KVM_EXIT_INTR;
			r = -EINTR;
			break;
		}

		vcpu->mode = IN_GUEST_MODE;

		/*
		 * Reading vcpu->requests must happen after setting vcpu->mode,
		 * so we don't miss a request because the requester sees
		 * OUTSIDE_GUEST_MODE and assumes we'll be checking requests
		 * before next entering the guest (and thus doesn't IPI).
		 */
		smp_mb();

		if (vcpu->requests) {
			/* Make sure we process requests preemptable */
			local_irq_enable();
			trace_kvm_check_requests(vcpu);
			r = kvmppc_core_check_requests(vcpu);
			local_irq_disable();
			if (r > 0)
				continue;
			break;
		}

		if (kvmppc_core_prepare_to_enter(vcpu)) {
			/* interrupts got enabled in between, so we
			   are back at square 1 */
			continue;
		}

#ifdef CONFIG_PPC64
		/* lazy EE magic */
		hard_irq_disable();
		if (lazy_irq_pending()) {
			/* Got an interrupt in between, try again */
			local_irq_enable();
			local_irq_disable();
			kvm_guest_exit();
			continue;
		}

		trace_hardirqs_on();
#endif

		kvm_guest_enter();
		break;
	}

	return r;
}
#endif /* CONFIG_KVM_BOOK3S_64_HV */

int kvmppc_kvm_pv(struct kvm_vcpu *vcpu)
{
	int nr = kvmppc_get_gpr(vcpu, 11);
	int r;
	unsigned long __maybe_unused param1 = kvmppc_get_gpr(vcpu, 3);
	unsigned long __maybe_unused param2 = kvmppc_get_gpr(vcpu, 4);
	unsigned long __maybe_unused param3 = kvmppc_get_gpr(vcpu, 5);
	unsigned long __maybe_unused param4 = kvmppc_get_gpr(vcpu, 6);
	unsigned long r2 = 0;

	if (!(vcpu->arch.shared->msr & MSR_SF)) {
		/* 32 bit mode */
		param1 &= 0xffffffff;
		param2 &= 0xffffffff;
		param3 &= 0xffffffff;
		param4 &= 0xffffffff;
	}

	switch (nr) {
	case KVM_HCALL_TOKEN(KVM_HC_PPC_MAP_MAGIC_PAGE):
	{
		vcpu->arch.magic_page_pa = param1;
		vcpu->arch.magic_page_ea = param2;

		r2 = KVM_MAGIC_FEAT_SR | KVM_MAGIC_FEAT_MAS0_TO_SPRG7;

		r = EV_SUCCESS;
		break;
	}
	case KVM_HCALL_TOKEN(KVM_HC_FEATURES):
		r = EV_SUCCESS;
#if defined(CONFIG_PPC_BOOK3S) || defined(CONFIG_KVM_E500V2)
		/* XXX Missing magic page on 44x */
		r2 |= (1 << KVM_FEATURE_MAGIC_PAGE);
#endif

		/* Second return value is in r4 */
		break;
	case EV_HCALL_TOKEN(EV_IDLE):
		r = EV_SUCCESS;
		kvm_vcpu_block(vcpu);
		clear_bit(KVM_REQ_UNHALT, &vcpu->requests);
		break;
	default:
		r = EV_UNIMPLEMENTED;
		break;
	}

	kvmppc_set_gpr(vcpu, 4, r2);

	return r;
}

int kvmppc_sanity_check(struct kvm_vcpu *vcpu)
{
	int r = false;

	/* We have to know what CPU to virtualize */
	if (!vcpu->arch.pvr)
		goto out;

	/* PAPR only works with book3s_64 */
	if ((vcpu->arch.cpu_type != KVM_CPU_3S_64) && vcpu->arch.papr_enabled)
		goto out;

#ifdef CONFIG_KVM_BOOK3S_64_HV
	/* HV KVM can only do PAPR mode for now */
	if (!vcpu->arch.papr_enabled)
		goto out;
#endif

#ifdef CONFIG_KVM_BOOKE_HV
	if (!cpu_has_feature(CPU_FTR_EMB_HV))
		goto out;
#endif

	r = true;

out:
	vcpu->arch.sane = r;
	return r ? 0 : -EINVAL;
}

int kvmppc_emulate_mmio(struct kvm_run *run, struct kvm_vcpu *vcpu)
{
	enum emulation_result er;
	int r;

	er = kvmppc_emulate_instruction(run, vcpu);
	switch (er) {
	case EMULATE_DONE:
		/* Future optimization: only reload non-volatiles if they were
		 * actually modified. */
		r = RESUME_GUEST_NV;
		break;
	case EMULATE_DO_MMIO:
		run->exit_reason = KVM_EXIT_MMIO;
		/* We must reload nonvolatiles because "update" load/store
		 * instructions modify register state. */
		/* Future optimization: only reload non-volatiles if they were
		 * actually modified. */
		r = RESUME_HOST_NV;
		break;
	case EMULATE_FAIL:
		/* XXX Deliver Program interrupt to guest. */
		printk(KERN_EMERG "%s: emulation failed (%08x)\n", __func__,
		       kvmppc_get_last_inst(vcpu));
		r = RESUME_HOST;
		break;
	default:
		WARN_ON(1);
		r = RESUME_GUEST;
	}

	return r;
}

int kvm_arch_hardware_enable(void *garbage)
{
	return 0;
}

void kvm_arch_hardware_disable(void *garbage)
{
}

int kvm_arch_hardware_setup(void)
{
	return 0;
}

void kvm_arch_hardware_unsetup(void)
{
}

void kvm_arch_check_processor_compat(void *rtn)
{
	*(int *)rtn = kvmppc_core_check_processor_compat();
}

int kvm_arch_init_vm(struct kvm *kvm, unsigned long type)
{
	if (type)
		return -EINVAL;

	return kvmppc_core_init_vm(kvm);
}

void kvm_arch_destroy_vm(struct kvm *kvm)
{
	unsigned int i;
	struct kvm_vcpu *vcpu;

	kvm_for_each_vcpu(i, vcpu, kvm)
		kvm_arch_vcpu_free(vcpu);

	mutex_lock(&kvm->lock);
	for (i = 0; i < atomic_read(&kvm->online_vcpus); i++)
		kvm->vcpus[i] = NULL;

	atomic_set(&kvm->online_vcpus, 0);

	kvmppc_core_destroy_vm(kvm);

	mutex_unlock(&kvm->lock);
}

void kvm_arch_sync_events(struct kvm *kvm)
{
}

int kvm_dev_ioctl_check_extension(long ext)
{
	int r;

	switch (ext) {
#ifdef CONFIG_BOOKE
	case KVM_CAP_PPC_BOOKE_SREGS:
	case KVM_CAP_PPC_BOOKE_WATCHDOG:
	case KVM_CAP_PPC_EPR:
#else
	case KVM_CAP_PPC_SEGSTATE:
	case KVM_CAP_PPC_HIOR:
	case KVM_CAP_PPC_PAPR:
#endif
	case KVM_CAP_PPC_UNSET_IRQ:
	case KVM_CAP_PPC_IRQ_LEVEL:
	case KVM_CAP_ENABLE_CAP:
	case KVM_CAP_ONE_REG:
	case KVM_CAP_IOEVENTFD:
		r = 1;
		break;
#ifndef CONFIG_KVM_BOOK3S_64_HV
	case KVM_CAP_PPC_PAIRED_SINGLES:
	case KVM_CAP_PPC_OSI:
	case KVM_CAP_PPC_GET_PVINFO:
#if defined(CONFIG_KVM_E500V2) || defined(CONFIG_KVM_E500MC)
	case KVM_CAP_SW_TLB:
#endif
		r = 1;
		break;
	case KVM_CAP_COALESCED_MMIO:
		r = KVM_COALESCED_MMIO_PAGE_OFFSET;
		break;
#endif
#ifdef CONFIG_PPC_BOOK3S_64
	case KVM_CAP_SPAPR_TCE:
	case KVM_CAP_PPC_ALLOC_HTAB:
		r = 1;
		break;
#endif /* CONFIG_PPC_BOOK3S_64 */
#ifdef CONFIG_KVM_BOOK3S_64_HV
	case KVM_CAP_PPC_SMT:
		r = threads_per_core;
		break;
	case KVM_CAP_PPC_RMA:
		r = 1;
		/* PPC970 requires an RMA */
		if (cpu_has_feature(CPU_FTR_ARCH_201))
			r = 2;
		break;
#endif
	case KVM_CAP_SYNC_MMU:
#ifdef CONFIG_KVM_BOOK3S_64_HV
		r = cpu_has_feature(CPU_FTR_ARCH_206) ? 1 : 0;
#elif defined(KVM_ARCH_WANT_MMU_NOTIFIER)
		r = 1;
#else
		r = 0;
		break;
#endif
#ifdef CONFIG_KVM_BOOK3S_64_HV
	case KVM_CAP_PPC_HTAB_FD:
		r = 1;
		break;
#endif
		break;
	case KVM_CAP_NR_VCPUS:
		/*
		 * Recommending a number of CPUs is somewhat arbitrary; we
		 * return the number of present CPUs for -HV (since a host
		 * will have secondary threads "offline"), and for other KVM
		 * implementations just count online CPUs.
		 */
#ifdef CONFIG_KVM_BOOK3S_64_HV
		r = num_present_cpus();
#else
		r = num_online_cpus();
#endif
		break;
	case KVM_CAP_MAX_VCPUS:
		r = KVM_MAX_VCPUS;
		break;
#ifdef CONFIG_PPC_BOOK3S_64
	case KVM_CAP_PPC_GET_SMMU_INFO:
		r = 1;
		break;
#endif
	default:
		r = 0;
		break;
	}
	return r;

}

long kvm_arch_dev_ioctl(struct file *filp,
                        unsigned int ioctl, unsigned long arg)
{
	return -EINVAL;
}

void kvm_arch_free_memslot(struct kvm_memory_slot *free,
			   struct kvm_memory_slot *dont)
{
	kvmppc_core_free_memslot(free, dont);
}

int kvm_arch_create_memslot(struct kvm_memory_slot *slot, unsigned long npages)
{
	return kvmppc_core_create_memslot(slot, npages);
}

int kvm_arch_prepare_memory_region(struct kvm *kvm,
                                   struct kvm_memory_slot *memslot,
                                   struct kvm_memory_slot old,
                                   struct kvm_userspace_memory_region *mem,
                                   bool user_alloc)
{
	return kvmppc_core_prepare_memory_region(kvm, memslot, mem);
}

void kvm_arch_commit_memory_region(struct kvm *kvm,
               struct kvm_userspace_memory_region *mem,
               struct kvm_memory_slot old,
               bool user_alloc)
{
	kvmppc_core_commit_memory_region(kvm, mem, old);
}

void kvm_arch_flush_shadow_all(struct kvm *kvm)
{
}

void kvm_arch_flush_shadow_memslot(struct kvm *kvm,
				   struct kvm_memory_slot *slot)
{
	kvmppc_core_flush_memslot(kvm, slot);
}

struct kvm_vcpu *kvm_arch_vcpu_create(struct kvm *kvm, unsigned int id)
{
	struct kvm_vcpu *vcpu;
	vcpu = kvmppc_core_vcpu_create(kvm, id);
	if (!IS_ERR(vcpu)) {
		vcpu->arch.wqp = &vcpu->wq;
		kvmppc_create_vcpu_debugfs(vcpu, id);
	}
	return vcpu;
}

int kvm_arch_vcpu_postcreate(struct kvm_vcpu *vcpu)
{
	return 0;
}

void kvm_arch_vcpu_free(struct kvm_vcpu *vcpu)
{
	/* Make sure we're not using the vcpu anymore */
	hrtimer_cancel(&vcpu->arch.dec_timer);
	tasklet_kill(&vcpu->arch.tasklet);

	kvmppc_remove_vcpu_debugfs(vcpu);
	kvmppc_core_vcpu_free(vcpu);
}

void kvm_arch_vcpu_destroy(struct kvm_vcpu *vcpu)
{
	kvm_arch_vcpu_free(vcpu);
}

int kvm_cpu_has_pending_timer(struct kvm_vcpu *vcpu)
{
	return kvmppc_core_pending_dec(vcpu);
}

/*
 * low level hrtimer wake routine. Because this runs in hardirq context
 * we schedule a tasklet to do the real work.
 */
enum hrtimer_restart kvmppc_decrementer_wakeup(struct hrtimer *timer)
{
	struct kvm_vcpu *vcpu;

	vcpu = container_of(timer, struct kvm_vcpu, arch.dec_timer);
	tasklet_schedule(&vcpu->arch.tasklet);

	return HRTIMER_NORESTART;
}

int kvm_arch_vcpu_init(struct kvm_vcpu *vcpu)
{
	int ret;

	hrtimer_init(&vcpu->arch.dec_timer, CLOCK_REALTIME, HRTIMER_MODE_ABS);
	tasklet_init(&vcpu->arch.tasklet, kvmppc_decrementer_func, (ulong)vcpu);
	vcpu->arch.dec_timer.function = kvmppc_decrementer_wakeup;
	vcpu->arch.dec_expires = ~(u64)0;

#ifdef CONFIG_KVM_EXIT_TIMING
	mutex_init(&vcpu->arch.exit_timing_lock);
#endif
	ret = kvmppc_subarch_vcpu_init(vcpu);
	return ret;
}

void kvm_arch_vcpu_uninit(struct kvm_vcpu *vcpu)
{
	kvmppc_mmu_destroy(vcpu);
	kvmppc_subarch_vcpu_uninit(vcpu);
}

void kvm_arch_vcpu_load(struct kvm_vcpu *vcpu, int cpu)
{
#ifdef CONFIG_BOOKE
	/*
	 * vrsave (formerly usprg0) isn't used by Linux, but may
	 * be used by the guest.
	 *
	 * On non-booke this is associated with Altivec and
	 * is handled by code in book3s.c.
	 */
	mtspr(SPRN_VRSAVE, vcpu->arch.vrsave);
#endif
	kvmppc_core_vcpu_load(vcpu, cpu);
}

void kvm_arch_vcpu_put(struct kvm_vcpu *vcpu)
{
	kvmppc_core_vcpu_put(vcpu);
#ifdef CONFIG_BOOKE
	vcpu->arch.vrsave = mfspr(SPRN_VRSAVE);
#endif
}

int kvm_arch_vcpu_ioctl_set_guest_debug(struct kvm_vcpu *vcpu,
                                        struct kvm_guest_debug *dbg)
{
	return -EINVAL;
}

static void kvmppc_complete_dcr_load(struct kvm_vcpu *vcpu,
                                     struct kvm_run *run)
{
	kvmppc_set_gpr(vcpu, vcpu->arch.io_gpr, run->dcr.data);
}

static void kvmppc_complete_mmio_load(struct kvm_vcpu *vcpu,
                                      struct kvm_run *run)
{
	u64 uninitialized_var(gpr);

	if (run->mmio.len > sizeof(gpr)) {
		printk(KERN_ERR "bad MMIO length: %d\n", run->mmio.len);
		return;
	}

	if (vcpu->arch.mmio_is_bigendian) {
		switch (run->mmio.len) {
		case 8: gpr = *(u64 *)run->mmio.data; break;
		case 4: gpr = *(u32 *)run->mmio.data; break;
		case 2: gpr = *(u16 *)run->mmio.data; break;
		case 1: gpr = *(u8 *)run->mmio.data; break;
		}
	} else {
		/* Convert BE data from userland back to LE. */
		switch (run->mmio.len) {
		case 4: gpr = ld_le32((u32 *)run->mmio.data); break;
		case 2: gpr = ld_le16((u16 *)run->mmio.data); break;
		case 1: gpr = *(u8 *)run->mmio.data; break;
		}
	}

	if (vcpu->arch.mmio_sign_extend) {
		switch (run->mmio.len) {
#ifdef CONFIG_PPC64
		case 4:
			gpr = (s64)(s32)gpr;
			break;
#endif
		case 2:
			gpr = (s64)(s16)gpr;
			break;
		case 1:
			gpr = (s64)(s8)gpr;
			break;
		}
	}

	kvmppc_set_gpr(vcpu, vcpu->arch.io_gpr, gpr);

	switch (vcpu->arch.io_gpr & KVM_MMIO_REG_EXT_MASK) {
	case KVM_MMIO_REG_GPR:
		kvmppc_set_gpr(vcpu, vcpu->arch.io_gpr, gpr);
		break;
	case KVM_MMIO_REG_FPR:
		vcpu->arch.fpr[vcpu->arch.io_gpr & KVM_MMIO_REG_MASK] = gpr;
		break;
#ifdef CONFIG_PPC_BOOK3S
	case KVM_MMIO_REG_QPR:
		vcpu->arch.qpr[vcpu->arch.io_gpr & KVM_MMIO_REG_MASK] = gpr;
		break;
	case KVM_MMIO_REG_FQPR:
		vcpu->arch.fpr[vcpu->arch.io_gpr & KVM_MMIO_REG_MASK] = gpr;
		vcpu->arch.qpr[vcpu->arch.io_gpr & KVM_MMIO_REG_MASK] = gpr;
		break;
#endif
	default:
		BUG();
	}
}

int kvmppc_handle_load(struct kvm_run *run, struct kvm_vcpu *vcpu,
                       unsigned int rt, unsigned int bytes, int is_bigendian)
{
	if (bytes > sizeof(run->mmio.data)) {
		printk(KERN_ERR "%s: bad MMIO length: %d\n", __func__,
		       run->mmio.len);
	}

	run->mmio.phys_addr = vcpu->arch.paddr_accessed;
	run->mmio.len = bytes;
	run->mmio.is_write = 0;

	vcpu->arch.io_gpr = rt;
	vcpu->arch.mmio_is_bigendian = is_bigendian;
	vcpu->mmio_needed = 1;
	vcpu->mmio_is_write = 0;
	vcpu->arch.mmio_sign_extend = 0;

	if (!kvm_io_bus_read(vcpu->kvm, KVM_MMIO_BUS, run->mmio.phys_addr,
			     bytes, &run->mmio.data)) {
		kvmppc_complete_mmio_load(vcpu, run);
		vcpu->mmio_needed = 0;
		return EMULATE_DONE;
	}

	return EMULATE_DO_MMIO;
}

/* Same as above, but sign extends */
int kvmppc_handle_loads(struct kvm_run *run, struct kvm_vcpu *vcpu,
                        unsigned int rt, unsigned int bytes, int is_bigendian)
{
	int r;

	vcpu->arch.mmio_sign_extend = 1;
	r = kvmppc_handle_load(run, vcpu, rt, bytes, is_bigendian);

	return r;
}

int kvmppc_handle_store(struct kvm_run *run, struct kvm_vcpu *vcpu,
                        u64 val, unsigned int bytes, int is_bigendian)
{
	void *data = run->mmio.data;

	if (bytes > sizeof(run->mmio.data)) {
		printk(KERN_ERR "%s: bad MMIO length: %d\n", __func__,
		       run->mmio.len);
	}

	run->mmio.phys_addr = vcpu->arch.paddr_accessed;
	run->mmio.len = bytes;
	run->mmio.is_write = 1;
	vcpu->mmio_needed = 1;
	vcpu->mmio_is_write = 1;

	/* Store the value at the lowest bytes in 'data'. */
	if (is_bigendian) {
		switch (bytes) {
		case 8: *(u64 *)data = val; break;
		case 4: *(u32 *)data = val; break;
		case 2: *(u16 *)data = val; break;
		case 1: *(u8  *)data = val; break;
		}
	} else {
		/* Store LE value into 'data'. */
		switch (bytes) {
		case 4: st_le32(data, val); break;
		case 2: st_le16(data, val); break;
		case 1: *(u8 *)data = val; break;
		}
	}

	if (!kvm_io_bus_write(vcpu->kvm, KVM_MMIO_BUS, run->mmio.phys_addr,
			      bytes, &run->mmio.data)) {
		kvmppc_complete_mmio_load(vcpu, run);
		vcpu->mmio_needed = 0;
		return EMULATE_DONE;
	}

	return EMULATE_DO_MMIO;
}

int kvm_arch_vcpu_ioctl_run(struct kvm_vcpu *vcpu, struct kvm_run *run)
{
	int r;
	sigset_t sigsaved;

	if (vcpu->sigset_active)
		sigprocmask(SIG_SETMASK, &vcpu->sigset, &sigsaved);

	if (vcpu->mmio_needed) {
		if (!vcpu->mmio_is_write)
			kvmppc_complete_mmio_load(vcpu, run);
		vcpu->mmio_needed = 0;
	} else if (vcpu->arch.dcr_needed) {
		if (!vcpu->arch.dcr_is_write)
			kvmppc_complete_dcr_load(vcpu, run);
		vcpu->arch.dcr_needed = 0;
	} else if (vcpu->arch.osi_needed) {
		u64 *gprs = run->osi.gprs;
		int i;

		for (i = 0; i < 32; i++)
			kvmppc_set_gpr(vcpu, i, gprs[i]);
		vcpu->arch.osi_needed = 0;
	} else if (vcpu->arch.hcall_needed) {
		int i;

		kvmppc_set_gpr(vcpu, 3, run->papr_hcall.ret);
		for (i = 0; i < 9; ++i)
			kvmppc_set_gpr(vcpu, 4 + i, run->papr_hcall.args[i]);
		vcpu->arch.hcall_needed = 0;
#ifdef CONFIG_BOOKE
	} else if (vcpu->arch.epr_needed) {
		kvmppc_set_epr(vcpu, run->epr.epr);
		vcpu->arch.epr_needed = 0;
#endif
	}

	r = kvmppc_vcpu_run(run, vcpu);

	if (vcpu->sigset_active)
		sigprocmask(SIG_SETMASK, &sigsaved, NULL);

	return r;
}

int kvm_vcpu_ioctl_interrupt(struct kvm_vcpu *vcpu, struct kvm_interrupt *irq)
{
	if (irq->irq == KVM_INTERRUPT_UNSET) {
		kvmppc_core_dequeue_external(vcpu, irq);
		return 0;
	}

	kvmppc_core_queue_external(vcpu, irq);

	kvm_vcpu_kick(vcpu);

	return 0;
}

static int kvm_vcpu_ioctl_enable_cap(struct kvm_vcpu *vcpu,
				     struct kvm_enable_cap *cap)
{
	int r;

	if (cap->flags)
		return -EINVAL;

	switch (cap->cap) {
	case KVM_CAP_PPC_OSI:
		r = 0;
		vcpu->arch.osi_enabled = true;
		break;
	case KVM_CAP_PPC_PAPR:
		r = 0;
		vcpu->arch.papr_enabled = true;
		break;
	case KVM_CAP_PPC_EPR:
		r = 0;
		vcpu->arch.epr_enabled = cap->args[0];
		break;
#ifdef CONFIG_BOOKE
	case KVM_CAP_PPC_BOOKE_WATCHDOG:
		r = 0;
		vcpu->arch.watchdog_enabled = true;
		break;
#endif
#if defined(CONFIG_KVM_E500V2) || defined(CONFIG_KVM_E500MC)
	case KVM_CAP_SW_TLB: {
		struct kvm_config_tlb cfg;
		void __user *user_ptr = (void __user *)(uintptr_t)cap->args[0];

		r = -EFAULT;
		if (copy_from_user(&cfg, user_ptr, sizeof(cfg)))
			break;

		r = kvm_vcpu_ioctl_config_tlb(vcpu, &cfg);
		break;
	}
#endif
	default:
		r = -EINVAL;
		break;
	}

	if (!r)
		r = kvmppc_sanity_check(vcpu);

	return r;
}

int kvm_arch_vcpu_ioctl_get_mpstate(struct kvm_vcpu *vcpu,
                                    struct kvm_mp_state *mp_state)
{
	return -EINVAL;
}

int kvm_arch_vcpu_ioctl_set_mpstate(struct kvm_vcpu *vcpu,
                                    struct kvm_mp_state *mp_state)
{
	return -EINVAL;
}

long kvm_arch_vcpu_ioctl(struct file *filp,
                         unsigned int ioctl, unsigned long arg)
{
	struct kvm_vcpu *vcpu = filp->private_data;
	void __user *argp = (void __user *)arg;
	long r;

	switch (ioctl) {
	case KVM_INTERRUPT: {
		struct kvm_interrupt irq;
		r = -EFAULT;
		if (copy_from_user(&irq, argp, sizeof(irq)))
			goto out;
		r = kvm_vcpu_ioctl_interrupt(vcpu, &irq);
		goto out;
	}

	case KVM_ENABLE_CAP:
	{
		struct kvm_enable_cap cap;
		r = -EFAULT;
		if (copy_from_user(&cap, argp, sizeof(cap)))
			goto out;
		r = kvm_vcpu_ioctl_enable_cap(vcpu, &cap);
		break;
	}

	case KVM_SET_ONE_REG:
	case KVM_GET_ONE_REG:
	{
		struct kvm_one_reg reg;
		r = -EFAULT;
		if (copy_from_user(&reg, argp, sizeof(reg)))
			goto out;
		if (ioctl == KVM_SET_ONE_REG)
			r = kvm_vcpu_ioctl_set_one_reg(vcpu, &reg);
		else
			r = kvm_vcpu_ioctl_get_one_reg(vcpu, &reg);
		break;
	}

#if defined(CONFIG_KVM_E500V2) || defined(CONFIG_KVM_E500MC)
	case KVM_DIRTY_TLB: {
		struct kvm_dirty_tlb dirty;
		r = -EFAULT;
		if (copy_from_user(&dirty, argp, sizeof(dirty)))
			goto out;
		r = kvm_vcpu_ioctl_dirty_tlb(vcpu, &dirty);
		break;
	}
#endif
	default:
		r = -EINVAL;
	}

out:
	return r;
}

int kvm_arch_vcpu_fault(struct kvm_vcpu *vcpu, struct vm_fault *vmf)
{
	return VM_FAULT_SIGBUS;
}

static int kvm_vm_ioctl_get_pvinfo(struct kvm_ppc_pvinfo *pvinfo)
{
	u32 inst_nop = 0x60000000;
#ifdef CONFIG_KVM_BOOKE_HV
	u32 inst_sc1 = 0x44000022;
	pvinfo->hcall[0] = inst_sc1;
	pvinfo->hcall[1] = inst_nop;
	pvinfo->hcall[2] = inst_nop;
	pvinfo->hcall[3] = inst_nop;
#else
	u32 inst_lis = 0x3c000000;
	u32 inst_ori = 0x60000000;
	u32 inst_sc = 0x44000002;
	u32 inst_imm_mask = 0xffff;

	/*
	 * The hypercall to get into KVM from within guest context is as
	 * follows:
	 *
	 *    lis r0, r0, KVM_SC_MAGIC_R0@h
	 *    ori r0, KVM_SC_MAGIC_R0@l
	 *    sc
	 *    nop
	 */
	pvinfo->hcall[0] = inst_lis | ((KVM_SC_MAGIC_R0 >> 16) & inst_imm_mask);
	pvinfo->hcall[1] = inst_ori | (KVM_SC_MAGIC_R0 & inst_imm_mask);
	pvinfo->hcall[2] = inst_sc;
	pvinfo->hcall[3] = inst_nop;
#endif

	pvinfo->flags = KVM_PPC_PVINFO_FLAGS_EV_IDLE;

	return 0;
}

long kvm_arch_vm_ioctl(struct file *filp,
                       unsigned int ioctl, unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	long r;

	switch (ioctl) {
	case KVM_PPC_GET_PVINFO: {
		struct kvm_ppc_pvinfo pvinfo;
		memset(&pvinfo, 0, sizeof(pvinfo));
		r = kvm_vm_ioctl_get_pvinfo(&pvinfo);
		if (copy_to_user(argp, &pvinfo, sizeof(pvinfo))) {
			r = -EFAULT;
			goto out;
		}

		break;
	}
#ifdef CONFIG_PPC_BOOK3S_64
	case KVM_CREATE_SPAPR_TCE: {
		struct kvm_create_spapr_tce create_tce;
		struct kvm *kvm = filp->private_data;

		r = -EFAULT;
		if (copy_from_user(&create_tce, argp, sizeof(create_tce)))
			goto out;
		r = kvm_vm_ioctl_create_spapr_tce(kvm, &create_tce);
		goto out;
	}
#endif /* CONFIG_PPC_BOOK3S_64 */

#ifdef CONFIG_KVM_BOOK3S_64_HV
	case KVM_ALLOCATE_RMA: {
		struct kvm *kvm = filp->private_data;
		struct kvm_allocate_rma rma;

		r = kvm_vm_ioctl_allocate_rma(kvm, &rma);
		if (r >= 0 && copy_to_user(argp, &rma, sizeof(rma)))
			r = -EFAULT;
		break;
	}

	case KVM_PPC_ALLOCATE_HTAB: {
		struct kvm *kvm = filp->private_data;
		u32 htab_order;

		r = -EFAULT;
		if (get_user(htab_order, (u32 __user *)argp))
			break;
		r = kvmppc_alloc_reset_hpt(kvm, &htab_order);
		if (r)
			break;
		r = -EFAULT;
		if (put_user(htab_order, (u32 __user *)argp))
			break;
		r = 0;
		break;
	}

	case KVM_PPC_GET_HTAB_FD: {
		struct kvm *kvm = filp->private_data;
		struct kvm_get_htab_fd ghf;

		r = -EFAULT;
		if (copy_from_user(&ghf, argp, sizeof(ghf)))
			break;
		r = kvm_vm_ioctl_get_htab_fd(kvm, &ghf);
		break;
	}
#endif /* CONFIG_KVM_BOOK3S_64_HV */

#ifdef CONFIG_PPC_BOOK3S_64
	case KVM_PPC_GET_SMMU_INFO: {
		struct kvm *kvm = filp->private_data;
		struct kvm_ppc_smmu_info info;

		memset(&info, 0, sizeof(info));
		r = kvm_vm_ioctl_get_smmu_info(kvm, &info);
		if (r >= 0 && copy_to_user(argp, &info, sizeof(info)))
			r = -EFAULT;
		break;
	}
#endif /* CONFIG_PPC_BOOK3S_64 */
	default:
		r = -ENOTTY;
	}

out:
	return r;
}

static unsigned long lpid_inuse[BITS_TO_LONGS(KVMPPC_NR_LPIDS)];
static unsigned long nr_lpids;

long kvmppc_alloc_lpid(void)
{
	long lpid;

	do {
		lpid = find_first_zero_bit(lpid_inuse, KVMPPC_NR_LPIDS);
		if (lpid >= nr_lpids) {
			pr_err("%s: No LPIDs free\n", __func__);
			return -ENOMEM;
		}
	} while (test_and_set_bit(lpid, lpid_inuse));

	return lpid;
}

void kvmppc_claim_lpid(long lpid)
{
	set_bit(lpid, lpid_inuse);
}

void kvmppc_free_lpid(long lpid)
{
	clear_bit(lpid, lpid_inuse);
}

void kvmppc_init_lpid(unsigned long nr_lpids_param)
{
	nr_lpids = min_t(unsigned long, KVMPPC_NR_LPIDS, nr_lpids_param);
	memset(lpid_inuse, 0, sizeof(lpid_inuse));
}

int kvm_arch_init(void *opaque)
{
	return 0;
}

void kvm_arch_exit(void)
{
}
