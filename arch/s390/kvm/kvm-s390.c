/*
 * hosting zSeries kernel virtual machines
 *
 * Copyright IBM Corp. 2008, 2009
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License (version 2 only)
 * as published by the Free Software Foundation.
 *
 *    Author(s): Carsten Otte <cotte@de.ibm.com>
 *               Christian Borntraeger <borntraeger@de.ibm.com>
 *               Heiko Carstens <heiko.carstens@de.ibm.com>
 *               Christian Ehrhardt <ehrhardt@de.ibm.com>
 */

#include <linux/compiler.h>
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/hrtimer.h>
#include <linux/init.h>
#include <linux/kvm.h>
#include <linux/kvm_host.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/timer.h>
#include <asm/asm-offsets.h>
#include <asm/lowcore.h>
#include <asm/pgtable.h>
#include <asm/nmi.h>
#include <asm/switch_to.h>
#include <asm/sclp.h>
#include "kvm-s390.h"
#include "gaccess.h"

#define CREATE_TRACE_POINTS
#include "trace.h"
#include "trace-s390.h"

#define VCPU_STAT(x) offsetof(struct kvm_vcpu, stat.x), KVM_STAT_VCPU

struct kvm_stats_debugfs_item debugfs_entries[] = {
	{ "userspace_handled", VCPU_STAT(exit_userspace) },
	{ "exit_null", VCPU_STAT(exit_null) },
	{ "exit_validity", VCPU_STAT(exit_validity) },
	{ "exit_stop_request", VCPU_STAT(exit_stop_request) },
	{ "exit_external_request", VCPU_STAT(exit_external_request) },
	{ "exit_external_interrupt", VCPU_STAT(exit_external_interrupt) },
	{ "exit_instruction", VCPU_STAT(exit_instruction) },
	{ "exit_program_interruption", VCPU_STAT(exit_program_interruption) },
	{ "exit_instr_and_program_int", VCPU_STAT(exit_instr_and_program) },
	{ "instruction_lctlg", VCPU_STAT(instruction_lctlg) },
	{ "instruction_lctl", VCPU_STAT(instruction_lctl) },
	{ "deliver_emergency_signal", VCPU_STAT(deliver_emergency_signal) },
	{ "deliver_external_call", VCPU_STAT(deliver_external_call) },
	{ "deliver_service_signal", VCPU_STAT(deliver_service_signal) },
	{ "deliver_virtio_interrupt", VCPU_STAT(deliver_virtio_interrupt) },
	{ "deliver_stop_signal", VCPU_STAT(deliver_stop_signal) },
	{ "deliver_prefix_signal", VCPU_STAT(deliver_prefix_signal) },
	{ "deliver_restart_signal", VCPU_STAT(deliver_restart_signal) },
	{ "deliver_program_interruption", VCPU_STAT(deliver_program_int) },
	{ "exit_wait_state", VCPU_STAT(exit_wait_state) },
	{ "instruction_stidp", VCPU_STAT(instruction_stidp) },
	{ "instruction_spx", VCPU_STAT(instruction_spx) },
	{ "instruction_stpx", VCPU_STAT(instruction_stpx) },
	{ "instruction_stap", VCPU_STAT(instruction_stap) },
	{ "instruction_storage_key", VCPU_STAT(instruction_storage_key) },
	{ "instruction_stsch", VCPU_STAT(instruction_stsch) },
	{ "instruction_chsc", VCPU_STAT(instruction_chsc) },
	{ "instruction_stsi", VCPU_STAT(instruction_stsi) },
	{ "instruction_stfl", VCPU_STAT(instruction_stfl) },
	{ "instruction_tprot", VCPU_STAT(instruction_tprot) },
	{ "instruction_sigp_sense", VCPU_STAT(instruction_sigp_sense) },
	{ "instruction_sigp_sense_running", VCPU_STAT(instruction_sigp_sense_running) },
	{ "instruction_sigp_external_call", VCPU_STAT(instruction_sigp_external_call) },
	{ "instruction_sigp_emergency", VCPU_STAT(instruction_sigp_emergency) },
	{ "instruction_sigp_stop", VCPU_STAT(instruction_sigp_stop) },
	{ "instruction_sigp_set_arch", VCPU_STAT(instruction_sigp_arch) },
	{ "instruction_sigp_set_prefix", VCPU_STAT(instruction_sigp_prefix) },
	{ "instruction_sigp_restart", VCPU_STAT(instruction_sigp_restart) },
	{ "diagnose_10", VCPU_STAT(diagnose_10) },
	{ "diagnose_44", VCPU_STAT(diagnose_44) },
	{ "diagnose_9c", VCPU_STAT(diagnose_9c) },
	{ NULL }
};

static unsigned long long *facilities;

/* Section: not file related */
int kvm_arch_hardware_enable(void *garbage)
{
	/* every s390 is virtualization enabled ;-) */
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
}

int kvm_arch_init(void *opaque)
{
	return 0;
}

void kvm_arch_exit(void)
{
}

/* Section: device related */
long kvm_arch_dev_ioctl(struct file *filp,
			unsigned int ioctl, unsigned long arg)
{
	if (ioctl == KVM_S390_ENABLE_SIE)
		return s390_enable_sie();
	return -EINVAL;
}

int kvm_dev_ioctl_check_extension(long ext)
{
	int r;

	switch (ext) {
	case KVM_CAP_S390_PSW:
	case KVM_CAP_S390_GMAP:
	case KVM_CAP_SYNC_MMU:
#ifdef CONFIG_KVM_S390_UCONTROL
	case KVM_CAP_S390_UCONTROL:
#endif
	case KVM_CAP_SYNC_REGS:
	case KVM_CAP_ONE_REG:
		r = 1;
		break;
	case KVM_CAP_NR_VCPUS:
	case KVM_CAP_MAX_VCPUS:
		r = KVM_MAX_VCPUS;
		break;
	case KVM_CAP_S390_COW:
		r = sclp_get_fac85() & 0x2;
		break;
	default:
		r = 0;
	}
	return r;
}

/* Section: vm related */
/*
 * Get (and clear) the dirty memory log for a memory slot.
 */
int kvm_vm_ioctl_get_dirty_log(struct kvm *kvm,
			       struct kvm_dirty_log *log)
{
	return 0;
}

long kvm_arch_vm_ioctl(struct file *filp,
		       unsigned int ioctl, unsigned long arg)
{
	struct kvm *kvm = filp->private_data;
	void __user *argp = (void __user *)arg;
	int r;

	switch (ioctl) {
	case KVM_S390_INTERRUPT: {
		struct kvm_s390_interrupt s390int;

		r = -EFAULT;
		if (copy_from_user(&s390int, argp, sizeof(s390int)))
			break;
		r = kvm_s390_inject_vm(kvm, &s390int);
		break;
	}
	default:
		r = -ENOTTY;
	}

	return r;
}

int kvm_arch_init_vm(struct kvm *kvm, unsigned long type)
{
	int rc;
	char debug_name[16];

	rc = -EINVAL;
#ifdef CONFIG_KVM_S390_UCONTROL
	if (type & ~KVM_VM_S390_UCONTROL)
		goto out_err;
	if ((type & KVM_VM_S390_UCONTROL) && (!capable(CAP_SYS_ADMIN)))
		goto out_err;
#else
	if (type)
		goto out_err;
#endif

	rc = s390_enable_sie();
	if (rc)
		goto out_err;

	rc = -ENOMEM;

	kvm->arch.sca = (struct sca_block *) get_zeroed_page(GFP_KERNEL);
	if (!kvm->arch.sca)
		goto out_err;

	sprintf(debug_name, "kvm-%u", current->pid);

	kvm->arch.dbf = debug_register(debug_name, 8, 2, 8 * sizeof(long));
	if (!kvm->arch.dbf)
		goto out_nodbf;

	spin_lock_init(&kvm->arch.float_int.lock);
	INIT_LIST_HEAD(&kvm->arch.float_int.list);

	debug_register_view(kvm->arch.dbf, &debug_sprintf_view);
	VM_EVENT(kvm, 3, "%s", "vm created");

	if (type & KVM_VM_S390_UCONTROL) {
		kvm->arch.gmap = NULL;
	} else {
		kvm->arch.gmap = gmap_alloc(current->mm);
		if (!kvm->arch.gmap)
			goto out_nogmap;
	}
	return 0;
out_nogmap:
	debug_unregister(kvm->arch.dbf);
out_nodbf:
	free_page((unsigned long)(kvm->arch.sca));
out_err:
	return rc;
}

void kvm_arch_vcpu_destroy(struct kvm_vcpu *vcpu)
{
	VCPU_EVENT(vcpu, 3, "%s", "free cpu");
	trace_kvm_s390_destroy_vcpu(vcpu->vcpu_id);
	if (!kvm_is_ucontrol(vcpu->kvm)) {
		clear_bit(63 - vcpu->vcpu_id,
			  (unsigned long *) &vcpu->kvm->arch.sca->mcn);
		if (vcpu->kvm->arch.sca->cpu[vcpu->vcpu_id].sda ==
		    (__u64) vcpu->arch.sie_block)
			vcpu->kvm->arch.sca->cpu[vcpu->vcpu_id].sda = 0;
	}
	smp_mb();

	if (kvm_is_ucontrol(vcpu->kvm))
		gmap_free(vcpu->arch.gmap);

	free_page((unsigned long)(vcpu->arch.sie_block));
	kvm_vcpu_uninit(vcpu);
	kfree(vcpu);
}

static void kvm_free_vcpus(struct kvm *kvm)
{
	unsigned int i;
	struct kvm_vcpu *vcpu;

	kvm_for_each_vcpu(i, vcpu, kvm)
		kvm_arch_vcpu_destroy(vcpu);

	mutex_lock(&kvm->lock);
	for (i = 0; i < atomic_read(&kvm->online_vcpus); i++)
		kvm->vcpus[i] = NULL;

	atomic_set(&kvm->online_vcpus, 0);
	mutex_unlock(&kvm->lock);
}

void kvm_arch_sync_events(struct kvm *kvm)
{
}

void kvm_arch_destroy_vm(struct kvm *kvm)
{
	kvm_free_vcpus(kvm);
	free_page((unsigned long)(kvm->arch.sca));
	debug_unregister(kvm->arch.dbf);
	if (!kvm_is_ucontrol(kvm))
		gmap_free(kvm->arch.gmap);
}

/* Section: vcpu related */
int kvm_arch_vcpu_init(struct kvm_vcpu *vcpu)
{
	if (kvm_is_ucontrol(vcpu->kvm)) {
		vcpu->arch.gmap = gmap_alloc(current->mm);
		if (!vcpu->arch.gmap)
			return -ENOMEM;
		return 0;
	}

	vcpu->arch.gmap = vcpu->kvm->arch.gmap;
	vcpu->run->kvm_valid_regs = KVM_SYNC_PREFIX |
				    KVM_SYNC_GPRS |
				    KVM_SYNC_ACRS |
				    KVM_SYNC_CRS;
	return 0;
}

void kvm_arch_vcpu_uninit(struct kvm_vcpu *vcpu)
{
	/* Nothing todo */
}

void kvm_arch_vcpu_load(struct kvm_vcpu *vcpu, int cpu)
{
	save_fp_regs(&vcpu->arch.host_fpregs);
	save_access_regs(vcpu->arch.host_acrs);
	vcpu->arch.guest_fpregs.fpc &= FPC_VALID_MASK;
	restore_fp_regs(&vcpu->arch.guest_fpregs);
	restore_access_regs(vcpu->run->s.regs.acrs);
	gmap_enable(vcpu->arch.gmap);
	atomic_set_mask(CPUSTAT_RUNNING, &vcpu->arch.sie_block->cpuflags);
}

void kvm_arch_vcpu_put(struct kvm_vcpu *vcpu)
{
	atomic_clear_mask(CPUSTAT_RUNNING, &vcpu->arch.sie_block->cpuflags);
	gmap_disable(vcpu->arch.gmap);
	save_fp_regs(&vcpu->arch.guest_fpregs);
	save_access_regs(vcpu->run->s.regs.acrs);
	restore_fp_regs(&vcpu->arch.host_fpregs);
	restore_access_regs(vcpu->arch.host_acrs);
}

static void kvm_s390_vcpu_initial_reset(struct kvm_vcpu *vcpu)
{
	/* this equals initial cpu reset in pop, but we don't switch to ESA */
	vcpu->arch.sie_block->gpsw.mask = 0UL;
	vcpu->arch.sie_block->gpsw.addr = 0UL;
	kvm_s390_set_prefix(vcpu, 0);
	vcpu->arch.sie_block->cputm     = 0UL;
	vcpu->arch.sie_block->ckc       = 0UL;
	vcpu->arch.sie_block->todpr     = 0;
	memset(vcpu->arch.sie_block->gcr, 0, 16 * sizeof(__u64));
	vcpu->arch.sie_block->gcr[0]  = 0xE0UL;
	vcpu->arch.sie_block->gcr[14] = 0xC2000000UL;
	vcpu->arch.guest_fpregs.fpc = 0;
	asm volatile("lfpc %0" : : "Q" (vcpu->arch.guest_fpregs.fpc));
	vcpu->arch.sie_block->gbea = 1;
	atomic_set_mask(CPUSTAT_STOPPED, &vcpu->arch.sie_block->cpuflags);
}

int kvm_arch_vcpu_postcreate(struct kvm_vcpu *vcpu)
{
	return 0;
}

int kvm_arch_vcpu_setup(struct kvm_vcpu *vcpu)
{
	atomic_set(&vcpu->arch.sie_block->cpuflags, CPUSTAT_ZARCH |
						    CPUSTAT_SM |
						    CPUSTAT_STOPPED);
	vcpu->arch.sie_block->ecb   = 6;
	vcpu->arch.sie_block->eca   = 0xC1002001U;
	vcpu->arch.sie_block->fac   = (int) (long) facilities;
	hrtimer_init(&vcpu->arch.ckc_timer, CLOCK_REALTIME, HRTIMER_MODE_ABS);
	tasklet_init(&vcpu->arch.tasklet, kvm_s390_tasklet,
		     (unsigned long) vcpu);
	vcpu->arch.ckc_timer.function = kvm_s390_idle_wakeup;
	get_cpu_id(&vcpu->arch.cpu_id);
	vcpu->arch.cpu_id.version = 0xff;
	return 0;
}

struct kvm_vcpu *kvm_arch_vcpu_create(struct kvm *kvm,
				      unsigned int id)
{
	struct kvm_vcpu *vcpu;
	int rc = -EINVAL;

	if (id >= KVM_MAX_VCPUS)
		goto out;

	rc = -ENOMEM;

	vcpu = kzalloc(sizeof(struct kvm_vcpu), GFP_KERNEL);
	if (!vcpu)
		goto out;

	vcpu->arch.sie_block = (struct kvm_s390_sie_block *)
					get_zeroed_page(GFP_KERNEL);

	if (!vcpu->arch.sie_block)
		goto out_free_cpu;

	vcpu->arch.sie_block->icpua = id;
	if (!kvm_is_ucontrol(kvm)) {
		if (!kvm->arch.sca) {
			WARN_ON_ONCE(1);
			goto out_free_cpu;
		}
		if (!kvm->arch.sca->cpu[id].sda)
			kvm->arch.sca->cpu[id].sda =
				(__u64) vcpu->arch.sie_block;
		vcpu->arch.sie_block->scaoh =
			(__u32)(((__u64)kvm->arch.sca) >> 32);
		vcpu->arch.sie_block->scaol = (__u32)(__u64)kvm->arch.sca;
		set_bit(63 - id, (unsigned long *) &kvm->arch.sca->mcn);
	}

	spin_lock_init(&vcpu->arch.local_int.lock);
	INIT_LIST_HEAD(&vcpu->arch.local_int.list);
	vcpu->arch.local_int.float_int = &kvm->arch.float_int;
	spin_lock(&kvm->arch.float_int.lock);
	kvm->arch.float_int.local_int[id] = &vcpu->arch.local_int;
	init_waitqueue_head(&vcpu->arch.local_int.wq);
	vcpu->arch.local_int.cpuflags = &vcpu->arch.sie_block->cpuflags;
	spin_unlock(&kvm->arch.float_int.lock);

	rc = kvm_vcpu_init(vcpu, kvm, id);
	if (rc)
		goto out_free_sie_block;
	VM_EVENT(kvm, 3, "create cpu %d at %p, sie block at %p", id, vcpu,
		 vcpu->arch.sie_block);
	trace_kvm_s390_create_vcpu(id, vcpu, vcpu->arch.sie_block);

	return vcpu;
out_free_sie_block:
	free_page((unsigned long)(vcpu->arch.sie_block));
out_free_cpu:
	kfree(vcpu);
out:
	return ERR_PTR(rc);
}

int kvm_arch_vcpu_runnable(struct kvm_vcpu *vcpu)
{
	/* kvm common code refers to this, but never calls it */
	BUG();
	return 0;
}

int kvm_arch_vcpu_should_kick(struct kvm_vcpu *vcpu)
{
	/* kvm common code refers to this, but never calls it */
	BUG();
	return 0;
}

static int kvm_arch_vcpu_ioctl_get_one_reg(struct kvm_vcpu *vcpu,
					   struct kvm_one_reg *reg)
{
	int r = -EINVAL;

	switch (reg->id) {
	case KVM_REG_S390_TODPR:
		r = put_user(vcpu->arch.sie_block->todpr,
			     (u32 __user *)reg->addr);
		break;
	case KVM_REG_S390_EPOCHDIFF:
		r = put_user(vcpu->arch.sie_block->epoch,
			     (u64 __user *)reg->addr);
		break;
	case KVM_REG_S390_CPU_TIMER:
		r = put_user(vcpu->arch.sie_block->cputm,
			     (u64 __user *)reg->addr);
		break;
	case KVM_REG_S390_CLOCK_COMP:
		r = put_user(vcpu->arch.sie_block->ckc,
			     (u64 __user *)reg->addr);
		break;
	default:
		break;
	}

	return r;
}

static int kvm_arch_vcpu_ioctl_set_one_reg(struct kvm_vcpu *vcpu,
					   struct kvm_one_reg *reg)
{
	int r = -EINVAL;

	switch (reg->id) {
	case KVM_REG_S390_TODPR:
		r = get_user(vcpu->arch.sie_block->todpr,
			     (u32 __user *)reg->addr);
		break;
	case KVM_REG_S390_EPOCHDIFF:
		r = get_user(vcpu->arch.sie_block->epoch,
			     (u64 __user *)reg->addr);
		break;
	case KVM_REG_S390_CPU_TIMER:
		r = get_user(vcpu->arch.sie_block->cputm,
			     (u64 __user *)reg->addr);
		break;
	case KVM_REG_S390_CLOCK_COMP:
		r = get_user(vcpu->arch.sie_block->ckc,
			     (u64 __user *)reg->addr);
		break;
	default:
		break;
	}

	return r;
}

static int kvm_arch_vcpu_ioctl_initial_reset(struct kvm_vcpu *vcpu)
{
	kvm_s390_vcpu_initial_reset(vcpu);
	return 0;
}

int kvm_arch_vcpu_ioctl_set_regs(struct kvm_vcpu *vcpu, struct kvm_regs *regs)
{
	memcpy(&vcpu->run->s.regs.gprs, &regs->gprs, sizeof(regs->gprs));
	return 0;
}

int kvm_arch_vcpu_ioctl_get_regs(struct kvm_vcpu *vcpu, struct kvm_regs *regs)
{
	memcpy(&regs->gprs, &vcpu->run->s.regs.gprs, sizeof(regs->gprs));
	return 0;
}

int kvm_arch_vcpu_ioctl_set_sregs(struct kvm_vcpu *vcpu,
				  struct kvm_sregs *sregs)
{
	memcpy(&vcpu->run->s.regs.acrs, &sregs->acrs, sizeof(sregs->acrs));
	memcpy(&vcpu->arch.sie_block->gcr, &sregs->crs, sizeof(sregs->crs));
	restore_access_regs(vcpu->run->s.regs.acrs);
	return 0;
}

int kvm_arch_vcpu_ioctl_get_sregs(struct kvm_vcpu *vcpu,
				  struct kvm_sregs *sregs)
{
	memcpy(&sregs->acrs, &vcpu->run->s.regs.acrs, sizeof(sregs->acrs));
	memcpy(&sregs->crs, &vcpu->arch.sie_block->gcr, sizeof(sregs->crs));
	return 0;
}

int kvm_arch_vcpu_ioctl_set_fpu(struct kvm_vcpu *vcpu, struct kvm_fpu *fpu)
{
	memcpy(&vcpu->arch.guest_fpregs.fprs, &fpu->fprs, sizeof(fpu->fprs));
	vcpu->arch.guest_fpregs.fpc = fpu->fpc & FPC_VALID_MASK;
	restore_fp_regs(&vcpu->arch.guest_fpregs);
	return 0;
}

int kvm_arch_vcpu_ioctl_get_fpu(struct kvm_vcpu *vcpu, struct kvm_fpu *fpu)
{
	memcpy(&fpu->fprs, &vcpu->arch.guest_fpregs.fprs, sizeof(fpu->fprs));
	fpu->fpc = vcpu->arch.guest_fpregs.fpc;
	return 0;
}

static int kvm_arch_vcpu_ioctl_set_initial_psw(struct kvm_vcpu *vcpu, psw_t psw)
{
	int rc = 0;

	if (!(atomic_read(&vcpu->arch.sie_block->cpuflags) & CPUSTAT_STOPPED))
		rc = -EBUSY;
	else {
		vcpu->run->psw_mask = psw.mask;
		vcpu->run->psw_addr = psw.addr;
	}
	return rc;
}

int kvm_arch_vcpu_ioctl_translate(struct kvm_vcpu *vcpu,
				  struct kvm_translation *tr)
{
	return -EINVAL; /* not implemented yet */
}

int kvm_arch_vcpu_ioctl_set_guest_debug(struct kvm_vcpu *vcpu,
					struct kvm_guest_debug *dbg)
{
	return -EINVAL; /* not implemented yet */
}

int kvm_arch_vcpu_ioctl_get_mpstate(struct kvm_vcpu *vcpu,
				    struct kvm_mp_state *mp_state)
{
	return -EINVAL; /* not implemented yet */
}

int kvm_arch_vcpu_ioctl_set_mpstate(struct kvm_vcpu *vcpu,
				    struct kvm_mp_state *mp_state)
{
	return -EINVAL; /* not implemented yet */
}

static int __vcpu_run(struct kvm_vcpu *vcpu)
{
	int rc;

	memcpy(&vcpu->arch.sie_block->gg14, &vcpu->run->s.regs.gprs[14], 16);

	if (need_resched())
		schedule();

	if (test_thread_flag(TIF_MCCK_PENDING))
		s390_handle_mcck();

	if (!kvm_is_ucontrol(vcpu->kvm))
		kvm_s390_deliver_pending_interrupts(vcpu);

	vcpu->arch.sie_block->icptcode = 0;
	preempt_disable();
	kvm_guest_enter();
	preempt_enable();
	VCPU_EVENT(vcpu, 6, "entering sie flags %x",
		   atomic_read(&vcpu->arch.sie_block->cpuflags));
	trace_kvm_s390_sie_enter(vcpu,
				 atomic_read(&vcpu->arch.sie_block->cpuflags));
	rc = sie64a(vcpu->arch.sie_block, vcpu->run->s.regs.gprs);
	if (rc) {
		if (kvm_is_ucontrol(vcpu->kvm)) {
			rc = SIE_INTERCEPT_UCONTROL;
		} else {
			VCPU_EVENT(vcpu, 3, "%s", "fault in sie instruction");
			trace_kvm_s390_sie_fault(vcpu);
			kvm_s390_inject_program_int(vcpu, PGM_ADDRESSING);
			rc = 0;
		}
	}
	VCPU_EVENT(vcpu, 6, "exit sie icptcode %d",
		   vcpu->arch.sie_block->icptcode);
	trace_kvm_s390_sie_exit(vcpu, vcpu->arch.sie_block->icptcode);
	kvm_guest_exit();

	memcpy(&vcpu->run->s.regs.gprs[14], &vcpu->arch.sie_block->gg14, 16);
	return rc;
}

int kvm_arch_vcpu_ioctl_run(struct kvm_vcpu *vcpu, struct kvm_run *kvm_run)
{
	int rc;
	sigset_t sigsaved;

rerun_vcpu:
	if (vcpu->sigset_active)
		sigprocmask(SIG_SETMASK, &vcpu->sigset, &sigsaved);

	atomic_clear_mask(CPUSTAT_STOPPED, &vcpu->arch.sie_block->cpuflags);

	BUG_ON(vcpu->kvm->arch.float_int.local_int[vcpu->vcpu_id] == NULL);

	switch (kvm_run->exit_reason) {
	case KVM_EXIT_S390_SIEIC:
	case KVM_EXIT_UNKNOWN:
	case KVM_EXIT_INTR:
	case KVM_EXIT_S390_RESET:
	case KVM_EXIT_S390_UCONTROL:
		break;
	default:
		BUG();
	}

	vcpu->arch.sie_block->gpsw.mask = kvm_run->psw_mask;
	vcpu->arch.sie_block->gpsw.addr = kvm_run->psw_addr;
	if (kvm_run->kvm_dirty_regs & KVM_SYNC_PREFIX) {
		kvm_run->kvm_dirty_regs &= ~KVM_SYNC_PREFIX;
		kvm_s390_set_prefix(vcpu, kvm_run->s.regs.prefix);
	}
	if (kvm_run->kvm_dirty_regs & KVM_SYNC_CRS) {
		kvm_run->kvm_dirty_regs &= ~KVM_SYNC_CRS;
		memcpy(&vcpu->arch.sie_block->gcr, &kvm_run->s.regs.crs, 128);
		kvm_s390_set_prefix(vcpu, kvm_run->s.regs.prefix);
	}

	might_fault();

	do {
		rc = __vcpu_run(vcpu);
		if (rc)
			break;
		if (kvm_is_ucontrol(vcpu->kvm))
			rc = -EOPNOTSUPP;
		else
			rc = kvm_handle_sie_intercept(vcpu);
	} while (!signal_pending(current) && !rc);

	if (rc == SIE_INTERCEPT_RERUNVCPU)
		goto rerun_vcpu;

	if (signal_pending(current) && !rc) {
		kvm_run->exit_reason = KVM_EXIT_INTR;
		rc = -EINTR;
	}

#ifdef CONFIG_KVM_S390_UCONTROL
	if (rc == SIE_INTERCEPT_UCONTROL) {
		kvm_run->exit_reason = KVM_EXIT_S390_UCONTROL;
		kvm_run->s390_ucontrol.trans_exc_code =
			current->thread.gmap_addr;
		kvm_run->s390_ucontrol.pgm_code = 0x10;
		rc = 0;
	}
#endif

	if (rc == -EOPNOTSUPP) {
		/* intercept cannot be handled in-kernel, prepare kvm-run */
		kvm_run->exit_reason         = KVM_EXIT_S390_SIEIC;
		kvm_run->s390_sieic.icptcode = vcpu->arch.sie_block->icptcode;
		kvm_run->s390_sieic.ipa      = vcpu->arch.sie_block->ipa;
		kvm_run->s390_sieic.ipb      = vcpu->arch.sie_block->ipb;
		rc = 0;
	}

	if (rc == -EREMOTE) {
		/* intercept was handled, but userspace support is needed
		 * kvm_run has been prepared by the handler */
		rc = 0;
	}

	kvm_run->psw_mask     = vcpu->arch.sie_block->gpsw.mask;
	kvm_run->psw_addr     = vcpu->arch.sie_block->gpsw.addr;
	kvm_run->s.regs.prefix = vcpu->arch.sie_block->prefix;
	memcpy(&kvm_run->s.regs.crs, &vcpu->arch.sie_block->gcr, 128);

	if (vcpu->sigset_active)
		sigprocmask(SIG_SETMASK, &sigsaved, NULL);

	vcpu->stat.exit_userspace++;
	return rc;
}

static int __guestcopy(struct kvm_vcpu *vcpu, u64 guestdest, void *from,
		       unsigned long n, int prefix)
{
	if (prefix)
		return copy_to_guest(vcpu, guestdest, from, n);
	else
		return copy_to_guest_absolute(vcpu, guestdest, from, n);
}

/*
 * store status at address
 * we use have two special cases:
 * KVM_S390_STORE_STATUS_NOADDR: -> 0x1200 on 64 bit
 * KVM_S390_STORE_STATUS_PREFIXED: -> prefix
 */
int kvm_s390_vcpu_store_status(struct kvm_vcpu *vcpu, unsigned long addr)
{
	unsigned char archmode = 1;
	int prefix;

	if (addr == KVM_S390_STORE_STATUS_NOADDR) {
		if (copy_to_guest_absolute(vcpu, 163ul, &archmode, 1))
			return -EFAULT;
		addr = SAVE_AREA_BASE;
		prefix = 0;
	} else if (addr == KVM_S390_STORE_STATUS_PREFIXED) {
		if (copy_to_guest(vcpu, 163ul, &archmode, 1))
			return -EFAULT;
		addr = SAVE_AREA_BASE;
		prefix = 1;
	} else
		prefix = 0;

	if (__guestcopy(vcpu, addr + offsetof(struct save_area, fp_regs),
			vcpu->arch.guest_fpregs.fprs, 128, prefix))
		return -EFAULT;

	if (__guestcopy(vcpu, addr + offsetof(struct save_area, gp_regs),
			vcpu->run->s.regs.gprs, 128, prefix))
		return -EFAULT;

	if (__guestcopy(vcpu, addr + offsetof(struct save_area, psw),
			&vcpu->arch.sie_block->gpsw, 16, prefix))
		return -EFAULT;

	if (__guestcopy(vcpu, addr + offsetof(struct save_area, pref_reg),
			&vcpu->arch.sie_block->prefix, 4, prefix))
		return -EFAULT;

	if (__guestcopy(vcpu,
			addr + offsetof(struct save_area, fp_ctrl_reg),
			&vcpu->arch.guest_fpregs.fpc, 4, prefix))
		return -EFAULT;

	if (__guestcopy(vcpu, addr + offsetof(struct save_area, tod_reg),
			&vcpu->arch.sie_block->todpr, 4, prefix))
		return -EFAULT;

	if (__guestcopy(vcpu, addr + offsetof(struct save_area, timer),
			&vcpu->arch.sie_block->cputm, 8, prefix))
		return -EFAULT;

	if (__guestcopy(vcpu, addr + offsetof(struct save_area, clk_cmp),
			&vcpu->arch.sie_block->ckc, 8, prefix))
		return -EFAULT;

	if (__guestcopy(vcpu, addr + offsetof(struct save_area, acc_regs),
			&vcpu->run->s.regs.acrs, 64, prefix))
		return -EFAULT;

	if (__guestcopy(vcpu,
			addr + offsetof(struct save_area, ctrl_regs),
			&vcpu->arch.sie_block->gcr, 128, prefix))
		return -EFAULT;
	return 0;
}

long kvm_arch_vcpu_ioctl(struct file *filp,
			 unsigned int ioctl, unsigned long arg)
{
	struct kvm_vcpu *vcpu = filp->private_data;
	void __user *argp = (void __user *)arg;
	long r;

	switch (ioctl) {
	case KVM_S390_INTERRUPT: {
		struct kvm_s390_interrupt s390int;

		r = -EFAULT;
		if (copy_from_user(&s390int, argp, sizeof(s390int)))
			break;
		r = kvm_s390_inject_vcpu(vcpu, &s390int);
		break;
	}
	case KVM_S390_STORE_STATUS:
		r = kvm_s390_vcpu_store_status(vcpu, arg);
		break;
	case KVM_S390_SET_INITIAL_PSW: {
		psw_t psw;

		r = -EFAULT;
		if (copy_from_user(&psw, argp, sizeof(psw)))
			break;
		r = kvm_arch_vcpu_ioctl_set_initial_psw(vcpu, psw);
		break;
	}
	case KVM_S390_INITIAL_RESET:
		r = kvm_arch_vcpu_ioctl_initial_reset(vcpu);
		break;
	case KVM_SET_ONE_REG:
	case KVM_GET_ONE_REG: {
		struct kvm_one_reg reg;
		r = -EFAULT;
		if (copy_from_user(&reg, argp, sizeof(reg)))
			break;
		if (ioctl == KVM_SET_ONE_REG)
			r = kvm_arch_vcpu_ioctl_set_one_reg(vcpu, &reg);
		else
			r = kvm_arch_vcpu_ioctl_get_one_reg(vcpu, &reg);
		break;
	}
#ifdef CONFIG_KVM_S390_UCONTROL
	case KVM_S390_UCAS_MAP: {
		struct kvm_s390_ucas_mapping ucasmap;

		if (copy_from_user(&ucasmap, argp, sizeof(ucasmap))) {
			r = -EFAULT;
			break;
		}

		if (!kvm_is_ucontrol(vcpu->kvm)) {
			r = -EINVAL;
			break;
		}

		r = gmap_map_segment(vcpu->arch.gmap, ucasmap.user_addr,
				     ucasmap.vcpu_addr, ucasmap.length);
		break;
	}
	case KVM_S390_UCAS_UNMAP: {
		struct kvm_s390_ucas_mapping ucasmap;

		if (copy_from_user(&ucasmap, argp, sizeof(ucasmap))) {
			r = -EFAULT;
			break;
		}

		if (!kvm_is_ucontrol(vcpu->kvm)) {
			r = -EINVAL;
			break;
		}

		r = gmap_unmap_segment(vcpu->arch.gmap, ucasmap.vcpu_addr,
			ucasmap.length);
		break;
	}
#endif
	case KVM_S390_VCPU_FAULT: {
		r = gmap_fault(arg, vcpu->arch.gmap);
		if (!IS_ERR_VALUE(r))
			r = 0;
		break;
	}
	default:
		r = -ENOTTY;
	}
	return r;
}

int kvm_arch_vcpu_fault(struct kvm_vcpu *vcpu, struct vm_fault *vmf)
{
#ifdef CONFIG_KVM_S390_UCONTROL
	if ((vmf->pgoff == KVM_S390_SIE_PAGE_OFFSET)
		 && (kvm_is_ucontrol(vcpu->kvm))) {
		vmf->page = virt_to_page(vcpu->arch.sie_block);
		get_page(vmf->page);
		return 0;
	}
#endif
	return VM_FAULT_SIGBUS;
}

void kvm_arch_free_memslot(struct kvm_memory_slot *free,
			   struct kvm_memory_slot *dont)
{
}

int kvm_arch_create_memslot(struct kvm_memory_slot *slot, unsigned long npages)
{
	return 0;
}

/* Section: memory related */
int kvm_arch_prepare_memory_region(struct kvm *kvm,
				   struct kvm_memory_slot *memslot,
				   struct kvm_memory_slot old,
				   struct kvm_userspace_memory_region *mem,
				   int user_alloc)
{
	/* A few sanity checks. We can have exactly one memory slot which has
	   to start at guest virtual zero and which has to be located at a
	   page boundary in userland and which has to end at a page boundary.
	   The memory in userland is ok to be fragmented into various different
	   vmas. It is okay to mmap() and munmap() stuff in this slot after
	   doing this call at any time */

	if (mem->slot)
		return -EINVAL;

	if (mem->guest_phys_addr)
		return -EINVAL;

	if (mem->userspace_addr & 0xffffful)
		return -EINVAL;

	if (mem->memory_size & 0xffffful)
		return -EINVAL;

	if (!user_alloc)
		return -EINVAL;

	return 0;
}

void kvm_arch_commit_memory_region(struct kvm *kvm,
				struct kvm_userspace_memory_region *mem,
				struct kvm_memory_slot old,
				int user_alloc)
{
	int rc;


	rc = gmap_map_segment(kvm->arch.gmap, mem->userspace_addr,
		mem->guest_phys_addr, mem->memory_size);
	if (rc)
		printk(KERN_WARNING "kvm-s390: failed to commit memory region\n");
	return;
}

void kvm_arch_flush_shadow_all(struct kvm *kvm)
{
}

void kvm_arch_flush_shadow_memslot(struct kvm *kvm,
				   struct kvm_memory_slot *slot)
{
}

static int __init kvm_s390_init(void)
{
	int ret;
	ret = kvm_init(NULL, sizeof(struct kvm_vcpu), 0, THIS_MODULE);
	if (ret)
		return ret;

	/*
	 * guests can ask for up to 255+1 double words, we need a full page
	 * to hold the maximum amount of facilities. On the other hand, we
	 * only set facilities that are known to work in KVM.
	 */
	facilities = (unsigned long long *) get_zeroed_page(GFP_KERNEL|GFP_DMA);
	if (!facilities) {
		kvm_exit();
		return -ENOMEM;
	}
	memcpy(facilities, S390_lowcore.stfle_fac_list, 16);
	facilities[0] &= 0xff00fff3f47c0000ULL;
	facilities[1] &= 0x001c000000000000ULL;
	return 0;
}

static void __exit kvm_s390_exit(void)
{
	free_page((unsigned long) facilities);
	kvm_exit();
}

module_init(kvm_s390_init);
module_exit(kvm_s390_exit);
