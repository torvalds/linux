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
#include <linux/sched/signal.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/file.h>
#include <linux/module.h>
#include <linux/irqbypass.h>
#include <linux/kvm_irqfd.h>
#include <asm/cputable.h>
#include <linux/uaccess.h>
#include <asm/kvm_ppc.h>
#include <asm/tlbflush.h>
#include <asm/cputhreads.h>
#include <asm/irqflags.h>
#include <asm/iommu.h>
#include <asm/switch_to.h>
#include <asm/xive.h>
#ifdef CONFIG_PPC_PSERIES
#include <asm/hvcall.h>
#include <asm/plpar_wrappers.h>
#endif

#include "timing.h"
#include "irq.h"
#include "../mm/mmu_decl.h"

#define CREATE_TRACE_POINTS
#include "trace.h"

struct kvmppc_ops *kvmppc_hv_ops;
EXPORT_SYMBOL_GPL(kvmppc_hv_ops);
struct kvmppc_ops *kvmppc_pr_ops;
EXPORT_SYMBOL_GPL(kvmppc_pr_ops);


int kvm_arch_vcpu_runnable(struct kvm_vcpu *v)
{
	return !!(v->arch.pending_exceptions) || kvm_request_pending(v);
}

bool kvm_arch_vcpu_in_kernel(struct kvm_vcpu *vcpu)
{
	return false;
}

int kvm_arch_vcpu_should_kick(struct kvm_vcpu *vcpu)
{
	return 1;
}

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
	int r;

	WARN_ON(irqs_disabled());
	hard_irq_disable();

	while (true) {
		if (need_resched()) {
			local_irq_enable();
			cond_resched();
			hard_irq_disable();
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
		 * This also orders the write to mode from any reads
		 * to the page tables done while the VCPU is running.
		 * Please see the comment in kvm_flush_remote_tlbs.
		 */
		smp_mb();

		if (kvm_request_pending(vcpu)) {
			/* Make sure we process requests preemptable */
			local_irq_enable();
			trace_kvm_check_requests(vcpu);
			r = kvmppc_core_check_requests(vcpu);
			hard_irq_disable();
			if (r > 0)
				continue;
			break;
		}

		if (kvmppc_core_prepare_to_enter(vcpu)) {
			/* interrupts got enabled in between, so we
			   are back at square 1 */
			continue;
		}

		guest_enter_irqoff();
		return 1;
	}

	/* return to host */
	local_irq_enable();
	return r;
}
EXPORT_SYMBOL_GPL(kvmppc_prepare_to_enter);

#if defined(CONFIG_PPC_BOOK3S_64) && defined(CONFIG_KVM_BOOK3S_PR_POSSIBLE)
static void kvmppc_swab_shared(struct kvm_vcpu *vcpu)
{
	struct kvm_vcpu_arch_shared *shared = vcpu->arch.shared;
	int i;

	shared->sprg0 = swab64(shared->sprg0);
	shared->sprg1 = swab64(shared->sprg1);
	shared->sprg2 = swab64(shared->sprg2);
	shared->sprg3 = swab64(shared->sprg3);
	shared->srr0 = swab64(shared->srr0);
	shared->srr1 = swab64(shared->srr1);
	shared->dar = swab64(shared->dar);
	shared->msr = swab64(shared->msr);
	shared->dsisr = swab32(shared->dsisr);
	shared->int_pending = swab32(shared->int_pending);
	for (i = 0; i < ARRAY_SIZE(shared->sr); i++)
		shared->sr[i] = swab32(shared->sr[i]);
}
#endif

int kvmppc_kvm_pv(struct kvm_vcpu *vcpu)
{
	int nr = kvmppc_get_gpr(vcpu, 11);
	int r;
	unsigned long __maybe_unused param1 = kvmppc_get_gpr(vcpu, 3);
	unsigned long __maybe_unused param2 = kvmppc_get_gpr(vcpu, 4);
	unsigned long __maybe_unused param3 = kvmppc_get_gpr(vcpu, 5);
	unsigned long __maybe_unused param4 = kvmppc_get_gpr(vcpu, 6);
	unsigned long r2 = 0;

	if (!(kvmppc_get_msr(vcpu) & MSR_SF)) {
		/* 32 bit mode */
		param1 &= 0xffffffff;
		param2 &= 0xffffffff;
		param3 &= 0xffffffff;
		param4 &= 0xffffffff;
	}

	switch (nr) {
	case KVM_HCALL_TOKEN(KVM_HC_PPC_MAP_MAGIC_PAGE):
	{
#if defined(CONFIG_PPC_BOOK3S_64) && defined(CONFIG_KVM_BOOK3S_PR_POSSIBLE)
		/* Book3S can be little endian, find it out here */
		int shared_big_endian = true;
		if (vcpu->arch.intr_msr & MSR_LE)
			shared_big_endian = false;
		if (shared_big_endian != vcpu->arch.shared_big_endian)
			kvmppc_swab_shared(vcpu);
		vcpu->arch.shared_big_endian = shared_big_endian;
#endif

		if (!(param2 & MAGIC_PAGE_FLAG_NOT_MAPPED_NX)) {
			/*
			 * Older versions of the Linux magic page code had
			 * a bug where they would map their trampoline code
			 * NX. If that's the case, remove !PR NX capability.
			 */
			vcpu->arch.disable_kernel_nx = true;
			kvm_make_request(KVM_REQ_TLB_FLUSH, vcpu);
		}

		vcpu->arch.magic_page_pa = param1 & ~0xfffULL;
		vcpu->arch.magic_page_ea = param2 & ~0xfffULL;

#ifdef CONFIG_PPC_64K_PAGES
		/*
		 * Make sure our 4k magic page is in the same window of a 64k
		 * page within the guest and within the host's page.
		 */
		if ((vcpu->arch.magic_page_pa & 0xf000) !=
		    ((ulong)vcpu->arch.shared & 0xf000)) {
			void *old_shared = vcpu->arch.shared;
			ulong shared = (ulong)vcpu->arch.shared;
			void *new_shared;

			shared &= PAGE_MASK;
			shared |= vcpu->arch.magic_page_pa & 0xf000;
			new_shared = (void*)shared;
			memcpy(new_shared, old_shared, 0x1000);
			vcpu->arch.shared = new_shared;
		}
#endif

		r2 = KVM_MAGIC_FEAT_SR | KVM_MAGIC_FEAT_MAS0_TO_SPRG7;

		r = EV_SUCCESS;
		break;
	}
	case KVM_HCALL_TOKEN(KVM_HC_FEATURES):
		r = EV_SUCCESS;
#if defined(CONFIG_PPC_BOOK3S) || defined(CONFIG_KVM_E500V2)
		r2 |= (1 << KVM_FEATURE_MAGIC_PAGE);
#endif

		/* Second return value is in r4 */
		break;
	case EV_HCALL_TOKEN(EV_IDLE):
		r = EV_SUCCESS;
		kvm_vcpu_block(vcpu);
		kvm_clear_request(KVM_REQ_UNHALT, vcpu);
		break;
	default:
		r = EV_UNIMPLEMENTED;
		break;
	}

	kvmppc_set_gpr(vcpu, 4, r2);

	return r;
}
EXPORT_SYMBOL_GPL(kvmppc_kvm_pv);

int kvmppc_sanity_check(struct kvm_vcpu *vcpu)
{
	int r = false;

	/* We have to know what CPU to virtualize */
	if (!vcpu->arch.pvr)
		goto out;

	/* PAPR only works with book3s_64 */
	if ((vcpu->arch.cpu_type != KVM_CPU_3S_64) && vcpu->arch.papr_enabled)
		goto out;

	/* HV KVM can only do PAPR mode for now */
	if (!vcpu->arch.papr_enabled && is_kvmppc_hv_enabled(vcpu->kvm))
		goto out;

#ifdef CONFIG_KVM_BOOKE_HV
	if (!cpu_has_feature(CPU_FTR_EMB_HV))
		goto out;
#endif

	r = true;

out:
	vcpu->arch.sane = r;
	return r ? 0 : -EINVAL;
}
EXPORT_SYMBOL_GPL(kvmppc_sanity_check);

int kvmppc_emulate_mmio(struct kvm_run *run, struct kvm_vcpu *vcpu)
{
	enum emulation_result er;
	int r;

	er = kvmppc_emulate_loadstore(vcpu);
	switch (er) {
	case EMULATE_DONE:
		/* Future optimization: only reload non-volatiles if they were
		 * actually modified. */
		r = RESUME_GUEST_NV;
		break;
	case EMULATE_AGAIN:
		r = RESUME_GUEST;
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
	{
		u32 last_inst;

		kvmppc_get_last_inst(vcpu, INST_GENERIC, &last_inst);
		/* XXX Deliver Program interrupt to guest. */
		pr_emerg("%s: emulation failed (%08x)\n", __func__, last_inst);
		r = RESUME_HOST;
		break;
	}
	default:
		WARN_ON(1);
		r = RESUME_GUEST;
	}

	return r;
}
EXPORT_SYMBOL_GPL(kvmppc_emulate_mmio);

int kvmppc_st(struct kvm_vcpu *vcpu, ulong *eaddr, int size, void *ptr,
	      bool data)
{
	ulong mp_pa = vcpu->arch.magic_page_pa & KVM_PAM & PAGE_MASK;
	struct kvmppc_pte pte;
	int r;

	vcpu->stat.st++;

	r = kvmppc_xlate(vcpu, *eaddr, data ? XLATE_DATA : XLATE_INST,
			 XLATE_WRITE, &pte);
	if (r < 0)
		return r;

	*eaddr = pte.raddr;

	if (!pte.may_write)
		return -EPERM;

	/* Magic page override */
	if (kvmppc_supports_magic_page(vcpu) && mp_pa &&
	    ((pte.raddr & KVM_PAM & PAGE_MASK) == mp_pa) &&
	    !(kvmppc_get_msr(vcpu) & MSR_PR)) {
		void *magic = vcpu->arch.shared;
		magic += pte.eaddr & 0xfff;
		memcpy(magic, ptr, size);
		return EMULATE_DONE;
	}

	if (kvm_write_guest(vcpu->kvm, pte.raddr, ptr, size))
		return EMULATE_DO_MMIO;

	return EMULATE_DONE;
}
EXPORT_SYMBOL_GPL(kvmppc_st);

int kvmppc_ld(struct kvm_vcpu *vcpu, ulong *eaddr, int size, void *ptr,
		      bool data)
{
	ulong mp_pa = vcpu->arch.magic_page_pa & KVM_PAM & PAGE_MASK;
	struct kvmppc_pte pte;
	int rc;

	vcpu->stat.ld++;

	rc = kvmppc_xlate(vcpu, *eaddr, data ? XLATE_DATA : XLATE_INST,
			  XLATE_READ, &pte);
	if (rc)
		return rc;

	*eaddr = pte.raddr;

	if (!pte.may_read)
		return -EPERM;

	if (!data && !pte.may_execute)
		return -ENOEXEC;

	/* Magic page override */
	if (kvmppc_supports_magic_page(vcpu) && mp_pa &&
	    ((pte.raddr & KVM_PAM & PAGE_MASK) == mp_pa) &&
	    !(kvmppc_get_msr(vcpu) & MSR_PR)) {
		void *magic = vcpu->arch.shared;
		magic += pte.eaddr & 0xfff;
		memcpy(ptr, magic, size);
		return EMULATE_DONE;
	}

	if (kvm_read_guest(vcpu->kvm, pte.raddr, ptr, size))
		return EMULATE_DO_MMIO;

	return EMULATE_DONE;
}
EXPORT_SYMBOL_GPL(kvmppc_ld);

int kvm_arch_hardware_enable(void)
{
	return 0;
}

int kvm_arch_hardware_setup(void)
{
	return 0;
}

void kvm_arch_check_processor_compat(void *rtn)
{
	*(int *)rtn = kvmppc_core_check_processor_compat();
}

int kvm_arch_init_vm(struct kvm *kvm, unsigned long type)
{
	struct kvmppc_ops *kvm_ops = NULL;
	/*
	 * if we have both HV and PR enabled, default is HV
	 */
	if (type == 0) {
		if (kvmppc_hv_ops)
			kvm_ops = kvmppc_hv_ops;
		else
			kvm_ops = kvmppc_pr_ops;
		if (!kvm_ops)
			goto err_out;
	} else	if (type == KVM_VM_PPC_HV) {
		if (!kvmppc_hv_ops)
			goto err_out;
		kvm_ops = kvmppc_hv_ops;
	} else if (type == KVM_VM_PPC_PR) {
		if (!kvmppc_pr_ops)
			goto err_out;
		kvm_ops = kvmppc_pr_ops;
	} else
		goto err_out;

	if (kvm_ops->owner && !try_module_get(kvm_ops->owner))
		return -ENOENT;

	kvm->arch.kvm_ops = kvm_ops;
	return kvmppc_core_init_vm(kvm);
err_out:
	return -EINVAL;
}

bool kvm_arch_has_vcpu_debugfs(void)
{
	return false;
}

int kvm_arch_create_vcpu_debugfs(struct kvm_vcpu *vcpu)
{
	return 0;
}

void kvm_arch_destroy_vm(struct kvm *kvm)
{
	unsigned int i;
	struct kvm_vcpu *vcpu;

#ifdef CONFIG_KVM_XICS
	/*
	 * We call kick_all_cpus_sync() to ensure that all
	 * CPUs have executed any pending IPIs before we
	 * continue and free VCPUs structures below.
	 */
	if (is_kvmppc_hv_enabled(kvm))
		kick_all_cpus_sync();
#endif

	kvm_for_each_vcpu(i, vcpu, kvm)
		kvm_arch_vcpu_free(vcpu);

	mutex_lock(&kvm->lock);
	for (i = 0; i < atomic_read(&kvm->online_vcpus); i++)
		kvm->vcpus[i] = NULL;

	atomic_set(&kvm->online_vcpus, 0);

	kvmppc_core_destroy_vm(kvm);

	mutex_unlock(&kvm->lock);

	/* drop the module reference */
	module_put(kvm->arch.kvm_ops->owner);
}

int kvm_vm_ioctl_check_extension(struct kvm *kvm, long ext)
{
	int r;
	/* Assume we're using HV mode when the HV module is loaded */
	int hv_enabled = kvmppc_hv_ops ? 1 : 0;

	if (kvm) {
		/*
		 * Hooray - we know which VM type we're running on. Depend on
		 * that rather than the guess above.
		 */
		hv_enabled = is_kvmppc_hv_enabled(kvm);
	}

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
	case KVM_CAP_ENABLE_CAP_VM:
	case KVM_CAP_ONE_REG:
	case KVM_CAP_IOEVENTFD:
	case KVM_CAP_DEVICE_CTRL:
	case KVM_CAP_IMMEDIATE_EXIT:
		r = 1;
		break;
	case KVM_CAP_PPC_PAIRED_SINGLES:
	case KVM_CAP_PPC_OSI:
	case KVM_CAP_PPC_GET_PVINFO:
#if defined(CONFIG_KVM_E500V2) || defined(CONFIG_KVM_E500MC)
	case KVM_CAP_SW_TLB:
#endif
		/* We support this only for PR */
		r = !hv_enabled;
		break;
#ifdef CONFIG_KVM_MPIC
	case KVM_CAP_IRQ_MPIC:
		r = 1;
		break;
#endif

#ifdef CONFIG_PPC_BOOK3S_64
	case KVM_CAP_SPAPR_TCE:
	case KVM_CAP_SPAPR_TCE_64:
		/* fallthrough */
	case KVM_CAP_SPAPR_TCE_VFIO:
	case KVM_CAP_PPC_RTAS:
	case KVM_CAP_PPC_FIXUP_HCALL:
	case KVM_CAP_PPC_ENABLE_HCALL:
#ifdef CONFIG_KVM_XICS
	case KVM_CAP_IRQ_XICS:
#endif
	case KVM_CAP_PPC_GET_CPU_CHAR:
		r = 1;
		break;

	case KVM_CAP_PPC_ALLOC_HTAB:
		r = hv_enabled;
		break;
#endif /* CONFIG_PPC_BOOK3S_64 */
#ifdef CONFIG_KVM_BOOK3S_HV_POSSIBLE
	case KVM_CAP_PPC_SMT:
		r = 0;
		if (kvm) {
			if (kvm->arch.emul_smt_mode > 1)
				r = kvm->arch.emul_smt_mode;
			else
				r = kvm->arch.smt_mode;
		} else if (hv_enabled) {
			if (cpu_has_feature(CPU_FTR_ARCH_300))
				r = 1;
			else
				r = threads_per_subcore;
		}
		break;
	case KVM_CAP_PPC_SMT_POSSIBLE:
		r = 1;
		if (hv_enabled) {
			if (!cpu_has_feature(CPU_FTR_ARCH_300))
				r = ((threads_per_subcore << 1) - 1);
			else
				/* P9 can emulate dbells, so allow any mode */
				r = 8 | 4 | 2 | 1;
		}
		break;
	case KVM_CAP_PPC_RMA:
		r = 0;
		break;
	case KVM_CAP_PPC_HWRNG:
		r = kvmppc_hwrng_present();
		break;
	case KVM_CAP_PPC_MMU_RADIX:
		r = !!(hv_enabled && radix_enabled());
		break;
	case KVM_CAP_PPC_MMU_HASH_V3:
		r = !!(hv_enabled && cpu_has_feature(CPU_FTR_ARCH_300));
		break;
#endif
	case KVM_CAP_SYNC_MMU:
#ifdef CONFIG_KVM_BOOK3S_HV_POSSIBLE
		r = hv_enabled;
#elif defined(KVM_ARCH_WANT_MMU_NOTIFIER)
		r = 1;
#else
		r = 0;
#endif
		break;
#ifdef CONFIG_KVM_BOOK3S_HV_POSSIBLE
	case KVM_CAP_PPC_HTAB_FD:
		r = hv_enabled;
		break;
#endif
	case KVM_CAP_NR_VCPUS:
		/*
		 * Recommending a number of CPUs is somewhat arbitrary; we
		 * return the number of present CPUs for -HV (since a host
		 * will have secondary threads "offline"), and for other KVM
		 * implementations just count online CPUs.
		 */
		if (hv_enabled)
			r = num_present_cpus();
		else
			r = num_online_cpus();
		break;
	case KVM_CAP_NR_MEMSLOTS:
		r = KVM_USER_MEM_SLOTS;
		break;
	case KVM_CAP_MAX_VCPUS:
		r = KVM_MAX_VCPUS;
		break;
#ifdef CONFIG_PPC_BOOK3S_64
	case KVM_CAP_PPC_GET_SMMU_INFO:
		r = 1;
		break;
	case KVM_CAP_SPAPR_MULTITCE:
		r = 1;
		break;
	case KVM_CAP_SPAPR_RESIZE_HPT:
		r = !!hv_enabled;
		break;
#endif
#ifdef CONFIG_KVM_BOOK3S_HV_POSSIBLE
	case KVM_CAP_PPC_FWNMI:
		r = hv_enabled;
		break;
#endif
	case KVM_CAP_PPC_HTM:
		r = hv_enabled &&
		    (cur_cpu_spec->cpu_user_features2 & PPC_FEATURE2_HTM_COMP);
		break;
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

void kvm_arch_free_memslot(struct kvm *kvm, struct kvm_memory_slot *free,
			   struct kvm_memory_slot *dont)
{
	kvmppc_core_free_memslot(kvm, free, dont);
}

int kvm_arch_create_memslot(struct kvm *kvm, struct kvm_memory_slot *slot,
			    unsigned long npages)
{
	return kvmppc_core_create_memslot(kvm, slot, npages);
}

int kvm_arch_prepare_memory_region(struct kvm *kvm,
				   struct kvm_memory_slot *memslot,
				   const struct kvm_userspace_memory_region *mem,
				   enum kvm_mr_change change)
{
	return kvmppc_core_prepare_memory_region(kvm, memslot, mem);
}

void kvm_arch_commit_memory_region(struct kvm *kvm,
				   const struct kvm_userspace_memory_region *mem,
				   const struct kvm_memory_slot *old,
				   const struct kvm_memory_slot *new,
				   enum kvm_mr_change change)
{
	kvmppc_core_commit_memory_region(kvm, mem, old, new);
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

void kvm_arch_vcpu_postcreate(struct kvm_vcpu *vcpu)
{
}

void kvm_arch_vcpu_free(struct kvm_vcpu *vcpu)
{
	/* Make sure we're not using the vcpu anymore */
	hrtimer_cancel(&vcpu->arch.dec_timer);

	kvmppc_remove_vcpu_debugfs(vcpu);

	switch (vcpu->arch.irq_type) {
	case KVMPPC_IRQ_MPIC:
		kvmppc_mpic_disconnect_vcpu(vcpu->arch.mpic, vcpu);
		break;
	case KVMPPC_IRQ_XICS:
		if (xive_enabled())
			kvmppc_xive_cleanup_vcpu(vcpu);
		else
			kvmppc_xics_free_icp(vcpu);
		break;
	}

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

static enum hrtimer_restart kvmppc_decrementer_wakeup(struct hrtimer *timer)
{
	struct kvm_vcpu *vcpu;

	vcpu = container_of(timer, struct kvm_vcpu, arch.dec_timer);
	kvmppc_decrementer_func(vcpu);

	return HRTIMER_NORESTART;
}

int kvm_arch_vcpu_init(struct kvm_vcpu *vcpu)
{
	int ret;

	hrtimer_init(&vcpu->arch.dec_timer, CLOCK_REALTIME, HRTIMER_MODE_ABS);
	vcpu->arch.dec_timer.function = kvmppc_decrementer_wakeup;
	vcpu->arch.dec_expires = get_tb();

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

/*
 * irq_bypass_add_producer and irq_bypass_del_producer are only
 * useful if the architecture supports PCI passthrough.
 * irq_bypass_stop and irq_bypass_start are not needed and so
 * kvm_ops are not defined for them.
 */
bool kvm_arch_has_irq_bypass(void)
{
	return ((kvmppc_hv_ops && kvmppc_hv_ops->irq_bypass_add_producer) ||
		(kvmppc_pr_ops && kvmppc_pr_ops->irq_bypass_add_producer));
}

int kvm_arch_irq_bypass_add_producer(struct irq_bypass_consumer *cons,
				     struct irq_bypass_producer *prod)
{
	struct kvm_kernel_irqfd *irqfd =
		container_of(cons, struct kvm_kernel_irqfd, consumer);
	struct kvm *kvm = irqfd->kvm;

	if (kvm->arch.kvm_ops->irq_bypass_add_producer)
		return kvm->arch.kvm_ops->irq_bypass_add_producer(cons, prod);

	return 0;
}

void kvm_arch_irq_bypass_del_producer(struct irq_bypass_consumer *cons,
				      struct irq_bypass_producer *prod)
{
	struct kvm_kernel_irqfd *irqfd =
		container_of(cons, struct kvm_kernel_irqfd, consumer);
	struct kvm *kvm = irqfd->kvm;

	if (kvm->arch.kvm_ops->irq_bypass_del_producer)
		kvm->arch.kvm_ops->irq_bypass_del_producer(cons, prod);
}

#ifdef CONFIG_VSX
static inline int kvmppc_get_vsr_dword_offset(int index)
{
	int offset;

	if ((index != 0) && (index != 1))
		return -1;

#ifdef __BIG_ENDIAN
	offset =  index;
#else
	offset = 1 - index;
#endif

	return offset;
}

static inline int kvmppc_get_vsr_word_offset(int index)
{
	int offset;

	if ((index > 3) || (index < 0))
		return -1;

#ifdef __BIG_ENDIAN
	offset = index;
#else
	offset = 3 - index;
#endif
	return offset;
}

static inline void kvmppc_set_vsr_dword(struct kvm_vcpu *vcpu,
	u64 gpr)
{
	union kvmppc_one_reg val;
	int offset = kvmppc_get_vsr_dword_offset(vcpu->arch.mmio_vsx_offset);
	int index = vcpu->arch.io_gpr & KVM_MMIO_REG_MASK;

	if (offset == -1)
		return;

	if (vcpu->arch.mmio_vsx_tx_sx_enabled) {
		val.vval = VCPU_VSX_VR(vcpu, index);
		val.vsxval[offset] = gpr;
		VCPU_VSX_VR(vcpu, index) = val.vval;
	} else {
		VCPU_VSX_FPR(vcpu, index, offset) = gpr;
	}
}

static inline void kvmppc_set_vsr_dword_dump(struct kvm_vcpu *vcpu,
	u64 gpr)
{
	union kvmppc_one_reg val;
	int index = vcpu->arch.io_gpr & KVM_MMIO_REG_MASK;

	if (vcpu->arch.mmio_vsx_tx_sx_enabled) {
		val.vval = VCPU_VSX_VR(vcpu, index);
		val.vsxval[0] = gpr;
		val.vsxval[1] = gpr;
		VCPU_VSX_VR(vcpu, index) = val.vval;
	} else {
		VCPU_VSX_FPR(vcpu, index, 0) = gpr;
		VCPU_VSX_FPR(vcpu, index, 1) = gpr;
	}
}

static inline void kvmppc_set_vsr_word(struct kvm_vcpu *vcpu,
	u32 gpr32)
{
	union kvmppc_one_reg val;
	int offset = kvmppc_get_vsr_word_offset(vcpu->arch.mmio_vsx_offset);
	int index = vcpu->arch.io_gpr & KVM_MMIO_REG_MASK;
	int dword_offset, word_offset;

	if (offset == -1)
		return;

	if (vcpu->arch.mmio_vsx_tx_sx_enabled) {
		val.vval = VCPU_VSX_VR(vcpu, index);
		val.vsx32val[offset] = gpr32;
		VCPU_VSX_VR(vcpu, index) = val.vval;
	} else {
		dword_offset = offset / 2;
		word_offset = offset % 2;
		val.vsxval[0] = VCPU_VSX_FPR(vcpu, index, dword_offset);
		val.vsx32val[word_offset] = gpr32;
		VCPU_VSX_FPR(vcpu, index, dword_offset) = val.vsxval[0];
	}
}
#endif /* CONFIG_VSX */

#ifdef CONFIG_ALTIVEC
static inline void kvmppc_set_vmx_dword(struct kvm_vcpu *vcpu,
		u64 gpr)
{
	int index = vcpu->arch.io_gpr & KVM_MMIO_REG_MASK;
	u32 hi, lo;
	u32 di;

#ifdef __BIG_ENDIAN
	hi = gpr >> 32;
	lo = gpr & 0xffffffff;
#else
	lo = gpr >> 32;
	hi = gpr & 0xffffffff;
#endif

	di = 2 - vcpu->arch.mmio_vmx_copy_nums;		/* doubleword index */
	if (di > 1)
		return;

	if (vcpu->arch.mmio_host_swabbed)
		di = 1 - di;

	VCPU_VSX_VR(vcpu, index).u[di * 2] = hi;
	VCPU_VSX_VR(vcpu, index).u[di * 2 + 1] = lo;
}
#endif /* CONFIG_ALTIVEC */

#ifdef CONFIG_PPC_FPU
static inline u64 sp_to_dp(u32 fprs)
{
	u64 fprd;

	preempt_disable();
	enable_kernel_fp();
	asm ("lfs%U1%X1 0,%1; stfd%U0%X0 0,%0" : "=m" (fprd) : "m" (fprs)
	     : "fr0");
	preempt_enable();
	return fprd;
}

static inline u32 dp_to_sp(u64 fprd)
{
	u32 fprs;

	preempt_disable();
	enable_kernel_fp();
	asm ("lfd%U1%X1 0,%1; stfs%U0%X0 0,%0" : "=m" (fprs) : "m" (fprd)
	     : "fr0");
	preempt_enable();
	return fprs;
}

#else
#define sp_to_dp(x)	(x)
#define dp_to_sp(x)	(x)
#endif /* CONFIG_PPC_FPU */

static void kvmppc_complete_mmio_load(struct kvm_vcpu *vcpu,
                                      struct kvm_run *run)
{
	u64 uninitialized_var(gpr);

	if (run->mmio.len > sizeof(gpr)) {
		printk(KERN_ERR "bad MMIO length: %d\n", run->mmio.len);
		return;
	}

	if (!vcpu->arch.mmio_host_swabbed) {
		switch (run->mmio.len) {
		case 8: gpr = *(u64 *)run->mmio.data; break;
		case 4: gpr = *(u32 *)run->mmio.data; break;
		case 2: gpr = *(u16 *)run->mmio.data; break;
		case 1: gpr = *(u8 *)run->mmio.data; break;
		}
	} else {
		switch (run->mmio.len) {
		case 8: gpr = swab64(*(u64 *)run->mmio.data); break;
		case 4: gpr = swab32(*(u32 *)run->mmio.data); break;
		case 2: gpr = swab16(*(u16 *)run->mmio.data); break;
		case 1: gpr = *(u8 *)run->mmio.data; break;
		}
	}

	/* conversion between single and double precision */
	if ((vcpu->arch.mmio_sp64_extend) && (run->mmio.len == 4))
		gpr = sp_to_dp(gpr);

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

	switch (vcpu->arch.io_gpr & KVM_MMIO_REG_EXT_MASK) {
	case KVM_MMIO_REG_GPR:
		kvmppc_set_gpr(vcpu, vcpu->arch.io_gpr, gpr);
		break;
	case KVM_MMIO_REG_FPR:
		VCPU_FPR(vcpu, vcpu->arch.io_gpr & KVM_MMIO_REG_MASK) = gpr;
		break;
#ifdef CONFIG_PPC_BOOK3S
	case KVM_MMIO_REG_QPR:
		vcpu->arch.qpr[vcpu->arch.io_gpr & KVM_MMIO_REG_MASK] = gpr;
		break;
	case KVM_MMIO_REG_FQPR:
		VCPU_FPR(vcpu, vcpu->arch.io_gpr & KVM_MMIO_REG_MASK) = gpr;
		vcpu->arch.qpr[vcpu->arch.io_gpr & KVM_MMIO_REG_MASK] = gpr;
		break;
#endif
#ifdef CONFIG_VSX
	case KVM_MMIO_REG_VSX:
		if (vcpu->arch.mmio_vsx_copy_type == KVMPPC_VSX_COPY_DWORD)
			kvmppc_set_vsr_dword(vcpu, gpr);
		else if (vcpu->arch.mmio_vsx_copy_type == KVMPPC_VSX_COPY_WORD)
			kvmppc_set_vsr_word(vcpu, gpr);
		else if (vcpu->arch.mmio_vsx_copy_type ==
				KVMPPC_VSX_COPY_DWORD_LOAD_DUMP)
			kvmppc_set_vsr_dword_dump(vcpu, gpr);
		break;
#endif
#ifdef CONFIG_ALTIVEC
	case KVM_MMIO_REG_VMX:
		kvmppc_set_vmx_dword(vcpu, gpr);
		break;
#endif
	default:
		BUG();
	}
}

static int __kvmppc_handle_load(struct kvm_run *run, struct kvm_vcpu *vcpu,
				unsigned int rt, unsigned int bytes,
				int is_default_endian, int sign_extend)
{
	int idx, ret;
	bool host_swabbed;

	/* Pity C doesn't have a logical XOR operator */
	if (kvmppc_need_byteswap(vcpu)) {
		host_swabbed = is_default_endian;
	} else {
		host_swabbed = !is_default_endian;
	}

	if (bytes > sizeof(run->mmio.data)) {
		printk(KERN_ERR "%s: bad MMIO length: %d\n", __func__,
		       run->mmio.len);
	}

	run->mmio.phys_addr = vcpu->arch.paddr_accessed;
	run->mmio.len = bytes;
	run->mmio.is_write = 0;

	vcpu->arch.io_gpr = rt;
	vcpu->arch.mmio_host_swabbed = host_swabbed;
	vcpu->mmio_needed = 1;
	vcpu->mmio_is_write = 0;
	vcpu->arch.mmio_sign_extend = sign_extend;

	idx = srcu_read_lock(&vcpu->kvm->srcu);

	ret = kvm_io_bus_read(vcpu, KVM_MMIO_BUS, run->mmio.phys_addr,
			      bytes, &run->mmio.data);

	srcu_read_unlock(&vcpu->kvm->srcu, idx);

	if (!ret) {
		kvmppc_complete_mmio_load(vcpu, run);
		vcpu->mmio_needed = 0;
		return EMULATE_DONE;
	}

	return EMULATE_DO_MMIO;
}

int kvmppc_handle_load(struct kvm_run *run, struct kvm_vcpu *vcpu,
		       unsigned int rt, unsigned int bytes,
		       int is_default_endian)
{
	return __kvmppc_handle_load(run, vcpu, rt, bytes, is_default_endian, 0);
}
EXPORT_SYMBOL_GPL(kvmppc_handle_load);

/* Same as above, but sign extends */
int kvmppc_handle_loads(struct kvm_run *run, struct kvm_vcpu *vcpu,
			unsigned int rt, unsigned int bytes,
			int is_default_endian)
{
	return __kvmppc_handle_load(run, vcpu, rt, bytes, is_default_endian, 1);
}

#ifdef CONFIG_VSX
int kvmppc_handle_vsx_load(struct kvm_run *run, struct kvm_vcpu *vcpu,
			unsigned int rt, unsigned int bytes,
			int is_default_endian, int mmio_sign_extend)
{
	enum emulation_result emulated = EMULATE_DONE;

	/* Currently, mmio_vsx_copy_nums only allowed to be 4 or less */
	if (vcpu->arch.mmio_vsx_copy_nums > 4)
		return EMULATE_FAIL;

	while (vcpu->arch.mmio_vsx_copy_nums) {
		emulated = __kvmppc_handle_load(run, vcpu, rt, bytes,
			is_default_endian, mmio_sign_extend);

		if (emulated != EMULATE_DONE)
			break;

		vcpu->arch.paddr_accessed += run->mmio.len;

		vcpu->arch.mmio_vsx_copy_nums--;
		vcpu->arch.mmio_vsx_offset++;
	}
	return emulated;
}
#endif /* CONFIG_VSX */

int kvmppc_handle_store(struct kvm_run *run, struct kvm_vcpu *vcpu,
			u64 val, unsigned int bytes, int is_default_endian)
{
	void *data = run->mmio.data;
	int idx, ret;
	bool host_swabbed;

	/* Pity C doesn't have a logical XOR operator */
	if (kvmppc_need_byteswap(vcpu)) {
		host_swabbed = is_default_endian;
	} else {
		host_swabbed = !is_default_endian;
	}

	if (bytes > sizeof(run->mmio.data)) {
		printk(KERN_ERR "%s: bad MMIO length: %d\n", __func__,
		       run->mmio.len);
	}

	run->mmio.phys_addr = vcpu->arch.paddr_accessed;
	run->mmio.len = bytes;
	run->mmio.is_write = 1;
	vcpu->mmio_needed = 1;
	vcpu->mmio_is_write = 1;

	if ((vcpu->arch.mmio_sp64_extend) && (bytes == 4))
		val = dp_to_sp(val);

	/* Store the value at the lowest bytes in 'data'. */
	if (!host_swabbed) {
		switch (bytes) {
		case 8: *(u64 *)data = val; break;
		case 4: *(u32 *)data = val; break;
		case 2: *(u16 *)data = val; break;
		case 1: *(u8  *)data = val; break;
		}
	} else {
		switch (bytes) {
		case 8: *(u64 *)data = swab64(val); break;
		case 4: *(u32 *)data = swab32(val); break;
		case 2: *(u16 *)data = swab16(val); break;
		case 1: *(u8  *)data = val; break;
		}
	}

	idx = srcu_read_lock(&vcpu->kvm->srcu);

	ret = kvm_io_bus_write(vcpu, KVM_MMIO_BUS, run->mmio.phys_addr,
			       bytes, &run->mmio.data);

	srcu_read_unlock(&vcpu->kvm->srcu, idx);

	if (!ret) {
		vcpu->mmio_needed = 0;
		return EMULATE_DONE;
	}

	return EMULATE_DO_MMIO;
}
EXPORT_SYMBOL_GPL(kvmppc_handle_store);

#ifdef CONFIG_VSX
static inline int kvmppc_get_vsr_data(struct kvm_vcpu *vcpu, int rs, u64 *val)
{
	u32 dword_offset, word_offset;
	union kvmppc_one_reg reg;
	int vsx_offset = 0;
	int copy_type = vcpu->arch.mmio_vsx_copy_type;
	int result = 0;

	switch (copy_type) {
	case KVMPPC_VSX_COPY_DWORD:
		vsx_offset =
			kvmppc_get_vsr_dword_offset(vcpu->arch.mmio_vsx_offset);

		if (vsx_offset == -1) {
			result = -1;
			break;
		}

		if (!vcpu->arch.mmio_vsx_tx_sx_enabled) {
			*val = VCPU_VSX_FPR(vcpu, rs, vsx_offset);
		} else {
			reg.vval = VCPU_VSX_VR(vcpu, rs);
			*val = reg.vsxval[vsx_offset];
		}
		break;

	case KVMPPC_VSX_COPY_WORD:
		vsx_offset =
			kvmppc_get_vsr_word_offset(vcpu->arch.mmio_vsx_offset);

		if (vsx_offset == -1) {
			result = -1;
			break;
		}

		if (!vcpu->arch.mmio_vsx_tx_sx_enabled) {
			dword_offset = vsx_offset / 2;
			word_offset = vsx_offset % 2;
			reg.vsxval[0] = VCPU_VSX_FPR(vcpu, rs, dword_offset);
			*val = reg.vsx32val[word_offset];
		} else {
			reg.vval = VCPU_VSX_VR(vcpu, rs);
			*val = reg.vsx32val[vsx_offset];
		}
		break;

	default:
		result = -1;
		break;
	}

	return result;
}

int kvmppc_handle_vsx_store(struct kvm_run *run, struct kvm_vcpu *vcpu,
			int rs, unsigned int bytes, int is_default_endian)
{
	u64 val;
	enum emulation_result emulated = EMULATE_DONE;

	vcpu->arch.io_gpr = rs;

	/* Currently, mmio_vsx_copy_nums only allowed to be 4 or less */
	if (vcpu->arch.mmio_vsx_copy_nums > 4)
		return EMULATE_FAIL;

	while (vcpu->arch.mmio_vsx_copy_nums) {
		if (kvmppc_get_vsr_data(vcpu, rs, &val) == -1)
			return EMULATE_FAIL;

		emulated = kvmppc_handle_store(run, vcpu,
			 val, bytes, is_default_endian);

		if (emulated != EMULATE_DONE)
			break;

		vcpu->arch.paddr_accessed += run->mmio.len;

		vcpu->arch.mmio_vsx_copy_nums--;
		vcpu->arch.mmio_vsx_offset++;
	}

	return emulated;
}

static int kvmppc_emulate_mmio_vsx_loadstore(struct kvm_vcpu *vcpu,
			struct kvm_run *run)
{
	enum emulation_result emulated = EMULATE_FAIL;
	int r;

	vcpu->arch.paddr_accessed += run->mmio.len;

	if (!vcpu->mmio_is_write) {
		emulated = kvmppc_handle_vsx_load(run, vcpu, vcpu->arch.io_gpr,
			 run->mmio.len, 1, vcpu->arch.mmio_sign_extend);
	} else {
		emulated = kvmppc_handle_vsx_store(run, vcpu,
			 vcpu->arch.io_gpr, run->mmio.len, 1);
	}

	switch (emulated) {
	case EMULATE_DO_MMIO:
		run->exit_reason = KVM_EXIT_MMIO;
		r = RESUME_HOST;
		break;
	case EMULATE_FAIL:
		pr_info("KVM: MMIO emulation failed (VSX repeat)\n");
		run->exit_reason = KVM_EXIT_INTERNAL_ERROR;
		run->internal.suberror = KVM_INTERNAL_ERROR_EMULATION;
		r = RESUME_HOST;
		break;
	default:
		r = RESUME_GUEST;
		break;
	}
	return r;
}
#endif /* CONFIG_VSX */

#ifdef CONFIG_ALTIVEC
/* handle quadword load access in two halves */
int kvmppc_handle_load128_by2x64(struct kvm_run *run, struct kvm_vcpu *vcpu,
		unsigned int rt, int is_default_endian)
{
	enum emulation_result emulated;

	while (vcpu->arch.mmio_vmx_copy_nums) {
		emulated = __kvmppc_handle_load(run, vcpu, rt, 8,
				is_default_endian, 0);

		if (emulated != EMULATE_DONE)
			break;

		vcpu->arch.paddr_accessed += run->mmio.len;
		vcpu->arch.mmio_vmx_copy_nums--;
	}

	return emulated;
}

static inline int kvmppc_get_vmx_data(struct kvm_vcpu *vcpu, int rs, u64 *val)
{
	vector128 vrs = VCPU_VSX_VR(vcpu, rs);
	u32 di;
	u64 w0, w1;

	di = 2 - vcpu->arch.mmio_vmx_copy_nums;		/* doubleword index */
	if (di > 1)
		return -1;

	if (vcpu->arch.mmio_host_swabbed)
		di = 1 - di;

	w0 = vrs.u[di * 2];
	w1 = vrs.u[di * 2 + 1];

#ifdef __BIG_ENDIAN
	*val = (w0 << 32) | w1;
#else
	*val = (w1 << 32) | w0;
#endif
	return 0;
}

/* handle quadword store in two halves */
int kvmppc_handle_store128_by2x64(struct kvm_run *run, struct kvm_vcpu *vcpu,
		unsigned int rs, int is_default_endian)
{
	u64 val = 0;
	enum emulation_result emulated = EMULATE_DONE;

	vcpu->arch.io_gpr = rs;

	while (vcpu->arch.mmio_vmx_copy_nums) {
		if (kvmppc_get_vmx_data(vcpu, rs, &val) == -1)
			return EMULATE_FAIL;

		emulated = kvmppc_handle_store(run, vcpu, val, 8,
				is_default_endian);
		if (emulated != EMULATE_DONE)
			break;

		vcpu->arch.paddr_accessed += run->mmio.len;
		vcpu->arch.mmio_vmx_copy_nums--;
	}

	return emulated;
}

static int kvmppc_emulate_mmio_vmx_loadstore(struct kvm_vcpu *vcpu,
		struct kvm_run *run)
{
	enum emulation_result emulated = EMULATE_FAIL;
	int r;

	vcpu->arch.paddr_accessed += run->mmio.len;

	if (!vcpu->mmio_is_write) {
		emulated = kvmppc_handle_load128_by2x64(run, vcpu,
				vcpu->arch.io_gpr, 1);
	} else {
		emulated = kvmppc_handle_store128_by2x64(run, vcpu,
				vcpu->arch.io_gpr, 1);
	}

	switch (emulated) {
	case EMULATE_DO_MMIO:
		run->exit_reason = KVM_EXIT_MMIO;
		r = RESUME_HOST;
		break;
	case EMULATE_FAIL:
		pr_info("KVM: MMIO emulation failed (VMX repeat)\n");
		run->exit_reason = KVM_EXIT_INTERNAL_ERROR;
		run->internal.suberror = KVM_INTERNAL_ERROR_EMULATION;
		r = RESUME_HOST;
		break;
	default:
		r = RESUME_GUEST;
		break;
	}
	return r;
}
#endif /* CONFIG_ALTIVEC */

int kvm_vcpu_ioctl_get_one_reg(struct kvm_vcpu *vcpu, struct kvm_one_reg *reg)
{
	int r = 0;
	union kvmppc_one_reg val;
	int size;

	size = one_reg_size(reg->id);
	if (size > sizeof(val))
		return -EINVAL;

	r = kvmppc_get_one_reg(vcpu, reg->id, &val);
	if (r == -EINVAL) {
		r = 0;
		switch (reg->id) {
#ifdef CONFIG_ALTIVEC
		case KVM_REG_PPC_VR0 ... KVM_REG_PPC_VR31:
			if (!cpu_has_feature(CPU_FTR_ALTIVEC)) {
				r = -ENXIO;
				break;
			}
			val.vval = vcpu->arch.vr.vr[reg->id - KVM_REG_PPC_VR0];
			break;
		case KVM_REG_PPC_VSCR:
			if (!cpu_has_feature(CPU_FTR_ALTIVEC)) {
				r = -ENXIO;
				break;
			}
			val = get_reg_val(reg->id, vcpu->arch.vr.vscr.u[3]);
			break;
		case KVM_REG_PPC_VRSAVE:
			val = get_reg_val(reg->id, vcpu->arch.vrsave);
			break;
#endif /* CONFIG_ALTIVEC */
		default:
			r = -EINVAL;
			break;
		}
	}

	if (r)
		return r;

	if (copy_to_user((char __user *)(unsigned long)reg->addr, &val, size))
		r = -EFAULT;

	return r;
}

int kvm_vcpu_ioctl_set_one_reg(struct kvm_vcpu *vcpu, struct kvm_one_reg *reg)
{
	int r;
	union kvmppc_one_reg val;
	int size;

	size = one_reg_size(reg->id);
	if (size > sizeof(val))
		return -EINVAL;

	if (copy_from_user(&val, (char __user *)(unsigned long)reg->addr, size))
		return -EFAULT;

	r = kvmppc_set_one_reg(vcpu, reg->id, &val);
	if (r == -EINVAL) {
		r = 0;
		switch (reg->id) {
#ifdef CONFIG_ALTIVEC
		case KVM_REG_PPC_VR0 ... KVM_REG_PPC_VR31:
			if (!cpu_has_feature(CPU_FTR_ALTIVEC)) {
				r = -ENXIO;
				break;
			}
			vcpu->arch.vr.vr[reg->id - KVM_REG_PPC_VR0] = val.vval;
			break;
		case KVM_REG_PPC_VSCR:
			if (!cpu_has_feature(CPU_FTR_ALTIVEC)) {
				r = -ENXIO;
				break;
			}
			vcpu->arch.vr.vscr.u[3] = set_reg_val(reg->id, val);
			break;
		case KVM_REG_PPC_VRSAVE:
			if (!cpu_has_feature(CPU_FTR_ALTIVEC)) {
				r = -ENXIO;
				break;
			}
			vcpu->arch.vrsave = set_reg_val(reg->id, val);
			break;
#endif /* CONFIG_ALTIVEC */
		default:
			r = -EINVAL;
			break;
		}
	}

	return r;
}

int kvm_arch_vcpu_ioctl_run(struct kvm_vcpu *vcpu, struct kvm_run *run)
{
	int r;

	vcpu_load(vcpu);

	if (vcpu->mmio_needed) {
		vcpu->mmio_needed = 0;
		if (!vcpu->mmio_is_write)
			kvmppc_complete_mmio_load(vcpu, run);
#ifdef CONFIG_VSX
		if (vcpu->arch.mmio_vsx_copy_nums > 0) {
			vcpu->arch.mmio_vsx_copy_nums--;
			vcpu->arch.mmio_vsx_offset++;
		}

		if (vcpu->arch.mmio_vsx_copy_nums > 0) {
			r = kvmppc_emulate_mmio_vsx_loadstore(vcpu, run);
			if (r == RESUME_HOST) {
				vcpu->mmio_needed = 1;
				goto out;
			}
		}
#endif
#ifdef CONFIG_ALTIVEC
		if (vcpu->arch.mmio_vmx_copy_nums > 0)
			vcpu->arch.mmio_vmx_copy_nums--;

		if (vcpu->arch.mmio_vmx_copy_nums > 0) {
			r = kvmppc_emulate_mmio_vmx_loadstore(vcpu, run);
			if (r == RESUME_HOST) {
				vcpu->mmio_needed = 1;
				goto out;
			}
		}
#endif
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

	kvm_sigset_activate(vcpu);

	if (run->immediate_exit)
		r = -EINTR;
	else
		r = kvmppc_vcpu_run(run, vcpu);

	kvm_sigset_deactivate(vcpu);

out:
	vcpu_put(vcpu);
	return r;
}

int kvm_vcpu_ioctl_interrupt(struct kvm_vcpu *vcpu, struct kvm_interrupt *irq)
{
	if (irq->irq == KVM_INTERRUPT_UNSET) {
		kvmppc_core_dequeue_external(vcpu);
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
		if (cap->args[0])
			vcpu->arch.epr_flags |= KVMPPC_EPR_USER;
		else
			vcpu->arch.epr_flags &= ~KVMPPC_EPR_USER;
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
#ifdef CONFIG_KVM_MPIC
	case KVM_CAP_IRQ_MPIC: {
		struct fd f;
		struct kvm_device *dev;

		r = -EBADF;
		f = fdget(cap->args[0]);
		if (!f.file)
			break;

		r = -EPERM;
		dev = kvm_device_from_filp(f.file);
		if (dev)
			r = kvmppc_mpic_connect_vcpu(dev, vcpu, cap->args[1]);

		fdput(f);
		break;
	}
#endif
#ifdef CONFIG_KVM_XICS
	case KVM_CAP_IRQ_XICS: {
		struct fd f;
		struct kvm_device *dev;

		r = -EBADF;
		f = fdget(cap->args[0]);
		if (!f.file)
			break;

		r = -EPERM;
		dev = kvm_device_from_filp(f.file);
		if (dev) {
			if (xive_enabled())
				r = kvmppc_xive_connect_vcpu(dev, vcpu, cap->args[1]);
			else
				r = kvmppc_xics_connect_vcpu(dev, vcpu, cap->args[1]);
		}

		fdput(f);
		break;
	}
#endif /* CONFIG_KVM_XICS */
#ifdef CONFIG_KVM_BOOK3S_HV_POSSIBLE
	case KVM_CAP_PPC_FWNMI:
		r = -EINVAL;
		if (!is_kvmppc_hv_enabled(vcpu->kvm))
			break;
		r = 0;
		vcpu->kvm->arch.fwnmi_enabled = true;
		break;
#endif /* CONFIG_KVM_BOOK3S_HV_POSSIBLE */
	default:
		r = -EINVAL;
		break;
	}

	if (!r)
		r = kvmppc_sanity_check(vcpu);

	return r;
}

bool kvm_arch_intc_initialized(struct kvm *kvm)
{
#ifdef CONFIG_KVM_MPIC
	if (kvm->arch.mpic)
		return true;
#endif
#ifdef CONFIG_KVM_XICS
	if (kvm->arch.xics || kvm->arch.xive)
		return true;
#endif
	return false;
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

long kvm_arch_vcpu_async_ioctl(struct file *filp,
			       unsigned int ioctl, unsigned long arg)
{
	struct kvm_vcpu *vcpu = filp->private_data;
	void __user *argp = (void __user *)arg;

	if (ioctl == KVM_INTERRUPT) {
		struct kvm_interrupt irq;
		if (copy_from_user(&irq, argp, sizeof(irq)))
			return -EFAULT;
		return kvm_vcpu_ioctl_interrupt(vcpu, &irq);
	}
	return -ENOIOCTLCMD;
}

long kvm_arch_vcpu_ioctl(struct file *filp,
                         unsigned int ioctl, unsigned long arg)
{
	struct kvm_vcpu *vcpu = filp->private_data;
	void __user *argp = (void __user *)arg;
	long r;

	vcpu_load(vcpu);

	switch (ioctl) {
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
	vcpu_put(vcpu);
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
	pvinfo->hcall[0] = cpu_to_be32(inst_sc1);
	pvinfo->hcall[1] = cpu_to_be32(inst_nop);
	pvinfo->hcall[2] = cpu_to_be32(inst_nop);
	pvinfo->hcall[3] = cpu_to_be32(inst_nop);
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
	pvinfo->hcall[0] = cpu_to_be32(inst_lis | ((KVM_SC_MAGIC_R0 >> 16) & inst_imm_mask));
	pvinfo->hcall[1] = cpu_to_be32(inst_ori | (KVM_SC_MAGIC_R0 & inst_imm_mask));
	pvinfo->hcall[2] = cpu_to_be32(inst_sc);
	pvinfo->hcall[3] = cpu_to_be32(inst_nop);
#endif

	pvinfo->flags = KVM_PPC_PVINFO_FLAGS_EV_IDLE;

	return 0;
}

int kvm_vm_ioctl_irq_line(struct kvm *kvm, struct kvm_irq_level *irq_event,
			  bool line_status)
{
	if (!irqchip_in_kernel(kvm))
		return -ENXIO;

	irq_event->status = kvm_set_irq(kvm, KVM_USERSPACE_IRQ_SOURCE_ID,
					irq_event->irq, irq_event->level,
					line_status);
	return 0;
}


static int kvm_vm_ioctl_enable_cap(struct kvm *kvm,
				   struct kvm_enable_cap *cap)
{
	int r;

	if (cap->flags)
		return -EINVAL;

	switch (cap->cap) {
#ifdef CONFIG_KVM_BOOK3S_64_HANDLER
	case KVM_CAP_PPC_ENABLE_HCALL: {
		unsigned long hcall = cap->args[0];

		r = -EINVAL;
		if (hcall > MAX_HCALL_OPCODE || (hcall & 3) ||
		    cap->args[1] > 1)
			break;
		if (!kvmppc_book3s_hcall_implemented(kvm, hcall))
			break;
		if (cap->args[1])
			set_bit(hcall / 4, kvm->arch.enabled_hcalls);
		else
			clear_bit(hcall / 4, kvm->arch.enabled_hcalls);
		r = 0;
		break;
	}
	case KVM_CAP_PPC_SMT: {
		unsigned long mode = cap->args[0];
		unsigned long flags = cap->args[1];

		r = -EINVAL;
		if (kvm->arch.kvm_ops->set_smt_mode)
			r = kvm->arch.kvm_ops->set_smt_mode(kvm, mode, flags);
		break;
	}
#endif
	default:
		r = -EINVAL;
		break;
	}

	return r;
}

#ifdef CONFIG_PPC_BOOK3S_64
/*
 * These functions check whether the underlying hardware is safe
 * against attacks based on observing the effects of speculatively
 * executed instructions, and whether it supplies instructions for
 * use in workarounds.  The information comes from firmware, either
 * via the device tree on powernv platforms or from an hcall on
 * pseries platforms.
 */
#ifdef CONFIG_PPC_PSERIES
static int pseries_get_cpu_char(struct kvm_ppc_cpu_char *cp)
{
	struct h_cpu_char_result c;
	unsigned long rc;

	if (!machine_is(pseries))
		return -ENOTTY;

	rc = plpar_get_cpu_characteristics(&c);
	if (rc == H_SUCCESS) {
		cp->character = c.character;
		cp->behaviour = c.behaviour;
		cp->character_mask = KVM_PPC_CPU_CHAR_SPEC_BAR_ORI31 |
			KVM_PPC_CPU_CHAR_BCCTRL_SERIALISED |
			KVM_PPC_CPU_CHAR_L1D_FLUSH_ORI30 |
			KVM_PPC_CPU_CHAR_L1D_FLUSH_TRIG2 |
			KVM_PPC_CPU_CHAR_L1D_THREAD_PRIV |
			KVM_PPC_CPU_CHAR_BR_HINT_HONOURED |
			KVM_PPC_CPU_CHAR_MTTRIG_THR_RECONF |
			KVM_PPC_CPU_CHAR_COUNT_CACHE_DIS;
		cp->behaviour_mask = KVM_PPC_CPU_BEHAV_FAVOUR_SECURITY |
			KVM_PPC_CPU_BEHAV_L1D_FLUSH_PR |
			KVM_PPC_CPU_BEHAV_BNDS_CHK_SPEC_BAR;
	}
	return 0;
}
#else
static int pseries_get_cpu_char(struct kvm_ppc_cpu_char *cp)
{
	return -ENOTTY;
}
#endif

static inline bool have_fw_feat(struct device_node *fw_features,
				const char *state, const char *name)
{
	struct device_node *np;
	bool r = false;

	np = of_get_child_by_name(fw_features, name);
	if (np) {
		r = of_property_read_bool(np, state);
		of_node_put(np);
	}
	return r;
}

static int kvmppc_get_cpu_char(struct kvm_ppc_cpu_char *cp)
{
	struct device_node *np, *fw_features;
	int r;

	memset(cp, 0, sizeof(*cp));
	r = pseries_get_cpu_char(cp);
	if (r != -ENOTTY)
		return r;

	np = of_find_node_by_name(NULL, "ibm,opal");
	if (np) {
		fw_features = of_get_child_by_name(np, "fw-features");
		of_node_put(np);
		if (!fw_features)
			return 0;
		if (have_fw_feat(fw_features, "enabled",
				 "inst-spec-barrier-ori31,31,0"))
			cp->character |= KVM_PPC_CPU_CHAR_SPEC_BAR_ORI31;
		if (have_fw_feat(fw_features, "enabled",
				 "fw-bcctrl-serialized"))
			cp->character |= KVM_PPC_CPU_CHAR_BCCTRL_SERIALISED;
		if (have_fw_feat(fw_features, "enabled",
				 "inst-l1d-flush-ori30,30,0"))
			cp->character |= KVM_PPC_CPU_CHAR_L1D_FLUSH_ORI30;
		if (have_fw_feat(fw_features, "enabled",
				 "inst-l1d-flush-trig2"))
			cp->character |= KVM_PPC_CPU_CHAR_L1D_FLUSH_TRIG2;
		if (have_fw_feat(fw_features, "enabled",
				 "fw-l1d-thread-split"))
			cp->character |= KVM_PPC_CPU_CHAR_L1D_THREAD_PRIV;
		if (have_fw_feat(fw_features, "enabled",
				 "fw-count-cache-disabled"))
			cp->character |= KVM_PPC_CPU_CHAR_COUNT_CACHE_DIS;
		cp->character_mask = KVM_PPC_CPU_CHAR_SPEC_BAR_ORI31 |
			KVM_PPC_CPU_CHAR_BCCTRL_SERIALISED |
			KVM_PPC_CPU_CHAR_L1D_FLUSH_ORI30 |
			KVM_PPC_CPU_CHAR_L1D_FLUSH_TRIG2 |
			KVM_PPC_CPU_CHAR_L1D_THREAD_PRIV |
			KVM_PPC_CPU_CHAR_COUNT_CACHE_DIS;

		if (have_fw_feat(fw_features, "enabled",
				 "speculation-policy-favor-security"))
			cp->behaviour |= KVM_PPC_CPU_BEHAV_FAVOUR_SECURITY;
		if (!have_fw_feat(fw_features, "disabled",
				  "needs-l1d-flush-msr-pr-0-to-1"))
			cp->behaviour |= KVM_PPC_CPU_BEHAV_L1D_FLUSH_PR;
		if (!have_fw_feat(fw_features, "disabled",
				  "needs-spec-barrier-for-bound-checks"))
			cp->behaviour |= KVM_PPC_CPU_BEHAV_BNDS_CHK_SPEC_BAR;
		cp->behaviour_mask = KVM_PPC_CPU_BEHAV_FAVOUR_SECURITY |
			KVM_PPC_CPU_BEHAV_L1D_FLUSH_PR |
			KVM_PPC_CPU_BEHAV_BNDS_CHK_SPEC_BAR;

		of_node_put(fw_features);
	}

	return 0;
}
#endif

long kvm_arch_vm_ioctl(struct file *filp,
                       unsigned int ioctl, unsigned long arg)
{
	struct kvm *kvm __maybe_unused = filp->private_data;
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
	case KVM_ENABLE_CAP:
	{
		struct kvm_enable_cap cap;
		r = -EFAULT;
		if (copy_from_user(&cap, argp, sizeof(cap)))
			goto out;
		r = kvm_vm_ioctl_enable_cap(kvm, &cap);
		break;
	}
#ifdef CONFIG_SPAPR_TCE_IOMMU
	case KVM_CREATE_SPAPR_TCE_64: {
		struct kvm_create_spapr_tce_64 create_tce_64;

		r = -EFAULT;
		if (copy_from_user(&create_tce_64, argp, sizeof(create_tce_64)))
			goto out;
		if (create_tce_64.flags) {
			r = -EINVAL;
			goto out;
		}
		r = kvm_vm_ioctl_create_spapr_tce(kvm, &create_tce_64);
		goto out;
	}
	case KVM_CREATE_SPAPR_TCE: {
		struct kvm_create_spapr_tce create_tce;
		struct kvm_create_spapr_tce_64 create_tce_64;

		r = -EFAULT;
		if (copy_from_user(&create_tce, argp, sizeof(create_tce)))
			goto out;

		create_tce_64.liobn = create_tce.liobn;
		create_tce_64.page_shift = IOMMU_PAGE_SHIFT_4K;
		create_tce_64.offset = 0;
		create_tce_64.size = create_tce.window_size >>
				IOMMU_PAGE_SHIFT_4K;
		create_tce_64.flags = 0;
		r = kvm_vm_ioctl_create_spapr_tce(kvm, &create_tce_64);
		goto out;
	}
#endif
#ifdef CONFIG_PPC_BOOK3S_64
	case KVM_PPC_GET_SMMU_INFO: {
		struct kvm_ppc_smmu_info info;
		struct kvm *kvm = filp->private_data;

		memset(&info, 0, sizeof(info));
		r = kvm->arch.kvm_ops->get_smmu_info(kvm, &info);
		if (r >= 0 && copy_to_user(argp, &info, sizeof(info)))
			r = -EFAULT;
		break;
	}
	case KVM_PPC_RTAS_DEFINE_TOKEN: {
		struct kvm *kvm = filp->private_data;

		r = kvm_vm_ioctl_rtas_define_token(kvm, argp);
		break;
	}
	case KVM_PPC_CONFIGURE_V3_MMU: {
		struct kvm *kvm = filp->private_data;
		struct kvm_ppc_mmuv3_cfg cfg;

		r = -EINVAL;
		if (!kvm->arch.kvm_ops->configure_mmu)
			goto out;
		r = -EFAULT;
		if (copy_from_user(&cfg, argp, sizeof(cfg)))
			goto out;
		r = kvm->arch.kvm_ops->configure_mmu(kvm, &cfg);
		break;
	}
	case KVM_PPC_GET_RMMU_INFO: {
		struct kvm *kvm = filp->private_data;
		struct kvm_ppc_rmmu_info info;

		r = -EINVAL;
		if (!kvm->arch.kvm_ops->get_rmmu_info)
			goto out;
		r = kvm->arch.kvm_ops->get_rmmu_info(kvm, &info);
		if (r >= 0 && copy_to_user(argp, &info, sizeof(info)))
			r = -EFAULT;
		break;
	}
	case KVM_PPC_GET_CPU_CHAR: {
		struct kvm_ppc_cpu_char cpuchar;

		r = kvmppc_get_cpu_char(&cpuchar);
		if (r >= 0 && copy_to_user(argp, &cpuchar, sizeof(cpuchar)))
			r = -EFAULT;
		break;
	}
	default: {
		struct kvm *kvm = filp->private_data;
		r = kvm->arch.kvm_ops->arch_vm_ioctl(filp, ioctl, arg);
	}
#else /* CONFIG_PPC_BOOK3S_64 */
	default:
		r = -ENOTTY;
#endif
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
EXPORT_SYMBOL_GPL(kvmppc_alloc_lpid);

void kvmppc_claim_lpid(long lpid)
{
	set_bit(lpid, lpid_inuse);
}
EXPORT_SYMBOL_GPL(kvmppc_claim_lpid);

void kvmppc_free_lpid(long lpid)
{
	clear_bit(lpid, lpid_inuse);
}
EXPORT_SYMBOL_GPL(kvmppc_free_lpid);

void kvmppc_init_lpid(unsigned long nr_lpids_param)
{
	nr_lpids = min_t(unsigned long, KVMPPC_NR_LPIDS, nr_lpids_param);
	memset(lpid_inuse, 0, sizeof(lpid_inuse));
}
EXPORT_SYMBOL_GPL(kvmppc_init_lpid);

int kvm_arch_init(void *opaque)
{
	return 0;
}

EXPORT_TRACEPOINT_SYMBOL_GPL(kvm_ppc_instr);
