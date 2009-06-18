/*
 * s390host.c --  hosting zSeries kernel virtual machines
 *
 * Copyright IBM Corp. 2008
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License (version 2 only)
 * as published by the Free Software Foundation.
 *
 *    Author(s): Carsten Otte <cotte@de.ibm.com>
 *               Christian Borntraeger <borntraeger@de.ibm.com>
 *               Heiko Carstens <heiko.carstens@de.ibm.com>
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
#include <asm/lowcore.h>
#include <asm/pgtable.h>
#include <asm/nmi.h>
#include "kvm-s390.h"
#include "gaccess.h"

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
	{ "instruction_sigp_sense", VCPU_STAT(instruction_sigp_sense) },
	{ "instruction_sigp_emergency", VCPU_STAT(instruction_sigp_emergency) },
	{ "instruction_sigp_stop", VCPU_STAT(instruction_sigp_stop) },
	{ "instruction_sigp_set_arch", VCPU_STAT(instruction_sigp_arch) },
	{ "instruction_sigp_set_prefix", VCPU_STAT(instruction_sigp_prefix) },
	{ "instruction_sigp_restart", VCPU_STAT(instruction_sigp_restart) },
	{ "diagnose_44", VCPU_STAT(diagnose_44) },
	{ NULL }
};


/* Section: not file related */
void kvm_arch_hardware_enable(void *garbage)
{
	/* every s390 is virtualization enabled ;-) */
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
	switch (ext) {
	default:
		return 0;
	}
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
		r = -EINVAL;
	}

	return r;
}

struct kvm *kvm_arch_create_vm(void)
{
	struct kvm *kvm;
	int rc;
	char debug_name[16];

	rc = s390_enable_sie();
	if (rc)
		goto out_nokvm;

	rc = -ENOMEM;
	kvm = kzalloc(sizeof(struct kvm), GFP_KERNEL);
	if (!kvm)
		goto out_nokvm;

	kvm->arch.sca = (struct sca_block *) get_zeroed_page(GFP_KERNEL);
	if (!kvm->arch.sca)
		goto out_nosca;

	sprintf(debug_name, "kvm-%u", current->pid);

	kvm->arch.dbf = debug_register(debug_name, 8, 2, 8 * sizeof(long));
	if (!kvm->arch.dbf)
		goto out_nodbf;

	spin_lock_init(&kvm->arch.float_int.lock);
	INIT_LIST_HEAD(&kvm->arch.float_int.list);

	debug_register_view(kvm->arch.dbf, &debug_sprintf_view);
	VM_EVENT(kvm, 3, "%s", "vm created");

	return kvm;
out_nodbf:
	free_page((unsigned long)(kvm->arch.sca));
out_nosca:
	kfree(kvm);
out_nokvm:
	return ERR_PTR(rc);
}

void kvm_arch_vcpu_destroy(struct kvm_vcpu *vcpu)
{
	VCPU_EVENT(vcpu, 3, "%s", "free cpu");
	if (vcpu->kvm->arch.sca->cpu[vcpu->vcpu_id].sda ==
		(__u64) vcpu->arch.sie_block)
		vcpu->kvm->arch.sca->cpu[vcpu->vcpu_id].sda = 0;
	smp_mb();
	free_page((unsigned long)(vcpu->arch.sie_block));
	kvm_vcpu_uninit(vcpu);
	kfree(vcpu);
}

static void kvm_free_vcpus(struct kvm *kvm)
{
	unsigned int i;

	for (i = 0; i < KVM_MAX_VCPUS; ++i) {
		if (kvm->vcpus[i]) {
			kvm_arch_vcpu_destroy(kvm->vcpus[i]);
			kvm->vcpus[i] = NULL;
		}
	}
}

void kvm_arch_sync_events(struct kvm *kvm)
{
}

void kvm_arch_destroy_vm(struct kvm *kvm)
{
	kvm_free_vcpus(kvm);
	kvm_free_physmem(kvm);
	free_page((unsigned long)(kvm->arch.sca));
	debug_unregister(kvm->arch.dbf);
	kfree(kvm);
}

/* Section: vcpu related */
int kvm_arch_vcpu_init(struct kvm_vcpu *vcpu)
{
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
	restore_access_regs(vcpu->arch.guest_acrs);
}

void kvm_arch_vcpu_put(struct kvm_vcpu *vcpu)
{
	save_fp_regs(&vcpu->arch.guest_fpregs);
	save_access_regs(vcpu->arch.guest_acrs);
	restore_fp_regs(&vcpu->arch.host_fpregs);
	restore_access_regs(vcpu->arch.host_acrs);
}

static void kvm_s390_vcpu_initial_reset(struct kvm_vcpu *vcpu)
{
	/* this equals initial cpu reset in pop, but we don't switch to ESA */
	vcpu->arch.sie_block->gpsw.mask = 0UL;
	vcpu->arch.sie_block->gpsw.addr = 0UL;
	vcpu->arch.sie_block->prefix    = 0UL;
	vcpu->arch.sie_block->ihcpu     = 0xffff;
	vcpu->arch.sie_block->cputm     = 0UL;
	vcpu->arch.sie_block->ckc       = 0UL;
	vcpu->arch.sie_block->todpr     = 0;
	memset(vcpu->arch.sie_block->gcr, 0, 16 * sizeof(__u64));
	vcpu->arch.sie_block->gcr[0]  = 0xE0UL;
	vcpu->arch.sie_block->gcr[14] = 0xC2000000UL;
	vcpu->arch.guest_fpregs.fpc = 0;
	asm volatile("lfpc %0" : : "Q" (vcpu->arch.guest_fpregs.fpc));
	vcpu->arch.sie_block->gbea = 1;
}

/* The current code can have up to 256 pages for virtio */
#define VIRTIODESCSPACE (256ul * 4096ul)

int kvm_arch_vcpu_setup(struct kvm_vcpu *vcpu)
{
	atomic_set(&vcpu->arch.sie_block->cpuflags, CPUSTAT_ZARCH);
	vcpu->arch.sie_block->gmslm = vcpu->kvm->arch.guest_memsize +
				      vcpu->kvm->arch.guest_origin +
				      VIRTIODESCSPACE - 1ul;
	vcpu->arch.sie_block->gmsor = vcpu->kvm->arch.guest_origin;
	vcpu->arch.sie_block->ecb   = 2;
	vcpu->arch.sie_block->eca   = 0xC1002001U;
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
	struct kvm_vcpu *vcpu = kzalloc(sizeof(struct kvm_vcpu), GFP_KERNEL);
	int rc = -ENOMEM;

	if (!vcpu)
		goto out_nomem;

	vcpu->arch.sie_block = (struct kvm_s390_sie_block *)
					get_zeroed_page(GFP_KERNEL);

	if (!vcpu->arch.sie_block)
		goto out_free_cpu;

	vcpu->arch.sie_block->icpua = id;
	BUG_ON(!kvm->arch.sca);
	if (!kvm->arch.sca->cpu[id].sda)
		kvm->arch.sca->cpu[id].sda = (__u64) vcpu->arch.sie_block;
	else
		BUG_ON(!kvm->vcpus[id]); /* vcpu does already exist */
	vcpu->arch.sie_block->scaoh = (__u32)(((__u64)kvm->arch.sca) >> 32);
	vcpu->arch.sie_block->scaol = (__u32)(__u64)kvm->arch.sca;

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
		goto out_free_cpu;
	VM_EVENT(kvm, 3, "create cpu %d at %p, sie block at %p", id, vcpu,
		 vcpu->arch.sie_block);

	return vcpu;
out_free_cpu:
	kfree(vcpu);
out_nomem:
	return ERR_PTR(rc);
}

int kvm_arch_vcpu_runnable(struct kvm_vcpu *vcpu)
{
	/* kvm common code refers to this, but never calls it */
	BUG();
	return 0;
}

static int kvm_arch_vcpu_ioctl_initial_reset(struct kvm_vcpu *vcpu)
{
	vcpu_load(vcpu);
	kvm_s390_vcpu_initial_reset(vcpu);
	vcpu_put(vcpu);
	return 0;
}

int kvm_arch_vcpu_ioctl_set_regs(struct kvm_vcpu *vcpu, struct kvm_regs *regs)
{
	vcpu_load(vcpu);
	memcpy(&vcpu->arch.guest_gprs, &regs->gprs, sizeof(regs->gprs));
	vcpu_put(vcpu);
	return 0;
}

int kvm_arch_vcpu_ioctl_get_regs(struct kvm_vcpu *vcpu, struct kvm_regs *regs)
{
	vcpu_load(vcpu);
	memcpy(&regs->gprs, &vcpu->arch.guest_gprs, sizeof(regs->gprs));
	vcpu_put(vcpu);
	return 0;
}

int kvm_arch_vcpu_ioctl_set_sregs(struct kvm_vcpu *vcpu,
				  struct kvm_sregs *sregs)
{
	vcpu_load(vcpu);
	memcpy(&vcpu->arch.guest_acrs, &sregs->acrs, sizeof(sregs->acrs));
	memcpy(&vcpu->arch.sie_block->gcr, &sregs->crs, sizeof(sregs->crs));
	vcpu_put(vcpu);
	return 0;
}

int kvm_arch_vcpu_ioctl_get_sregs(struct kvm_vcpu *vcpu,
				  struct kvm_sregs *sregs)
{
	vcpu_load(vcpu);
	memcpy(&sregs->acrs, &vcpu->arch.guest_acrs, sizeof(sregs->acrs));
	memcpy(&sregs->crs, &vcpu->arch.sie_block->gcr, sizeof(sregs->crs));
	vcpu_put(vcpu);
	return 0;
}

int kvm_arch_vcpu_ioctl_set_fpu(struct kvm_vcpu *vcpu, struct kvm_fpu *fpu)
{
	vcpu_load(vcpu);
	memcpy(&vcpu->arch.guest_fpregs.fprs, &fpu->fprs, sizeof(fpu->fprs));
	vcpu->arch.guest_fpregs.fpc = fpu->fpc;
	vcpu_put(vcpu);
	return 0;
}

int kvm_arch_vcpu_ioctl_get_fpu(struct kvm_vcpu *vcpu, struct kvm_fpu *fpu)
{
	vcpu_load(vcpu);
	memcpy(&fpu->fprs, &vcpu->arch.guest_fpregs.fprs, sizeof(fpu->fprs));
	fpu->fpc = vcpu->arch.guest_fpregs.fpc;
	vcpu_put(vcpu);
	return 0;
}

static int kvm_arch_vcpu_ioctl_set_initial_psw(struct kvm_vcpu *vcpu, psw_t psw)
{
	int rc = 0;

	vcpu_load(vcpu);
	if (atomic_read(&vcpu->arch.sie_block->cpuflags) & CPUSTAT_RUNNING)
		rc = -EBUSY;
	else
		vcpu->arch.sie_block->gpsw = psw;
	vcpu_put(vcpu);
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

static void __vcpu_run(struct kvm_vcpu *vcpu)
{
	memcpy(&vcpu->arch.sie_block->gg14, &vcpu->arch.guest_gprs[14], 16);

	if (need_resched())
		schedule();

	if (test_thread_flag(TIF_MCCK_PENDING))
		s390_handle_mcck();

	kvm_s390_deliver_pending_interrupts(vcpu);

	vcpu->arch.sie_block->icptcode = 0;
	local_irq_disable();
	kvm_guest_enter();
	local_irq_enable();
	VCPU_EVENT(vcpu, 6, "entering sie flags %x",
		   atomic_read(&vcpu->arch.sie_block->cpuflags));
	if (sie64a(vcpu->arch.sie_block, vcpu->arch.guest_gprs)) {
		VCPU_EVENT(vcpu, 3, "%s", "fault in sie instruction");
		kvm_s390_inject_program_int(vcpu, PGM_ADDRESSING);
	}
	VCPU_EVENT(vcpu, 6, "exit sie icptcode %d",
		   vcpu->arch.sie_block->icptcode);
	local_irq_disable();
	kvm_guest_exit();
	local_irq_enable();

	memcpy(&vcpu->arch.guest_gprs[14], &vcpu->arch.sie_block->gg14, 16);
}

int kvm_arch_vcpu_ioctl_run(struct kvm_vcpu *vcpu, struct kvm_run *kvm_run)
{
	int rc;
	sigset_t sigsaved;

	vcpu_load(vcpu);

	/* verify, that memory has been registered */
	if (!vcpu->kvm->arch.guest_memsize) {
		vcpu_put(vcpu);
		return -EINVAL;
	}

	if (vcpu->sigset_active)
		sigprocmask(SIG_SETMASK, &vcpu->sigset, &sigsaved);

	atomic_set_mask(CPUSTAT_RUNNING, &vcpu->arch.sie_block->cpuflags);

	BUG_ON(vcpu->kvm->arch.float_int.local_int[vcpu->vcpu_id] == NULL);

	switch (kvm_run->exit_reason) {
	case KVM_EXIT_S390_SIEIC:
		vcpu->arch.sie_block->gpsw.mask = kvm_run->s390_sieic.mask;
		vcpu->arch.sie_block->gpsw.addr = kvm_run->s390_sieic.addr;
		break;
	case KVM_EXIT_UNKNOWN:
	case KVM_EXIT_S390_RESET:
		break;
	default:
		BUG();
	}

	might_fault();

	do {
		__vcpu_run(vcpu);
		rc = kvm_handle_sie_intercept(vcpu);
	} while (!signal_pending(current) && !rc);

	if (signal_pending(current) && !rc)
		rc = -EINTR;

	if (rc == -ENOTSUPP) {
		/* intercept cannot be handled in-kernel, prepare kvm-run */
		kvm_run->exit_reason         = KVM_EXIT_S390_SIEIC;
		kvm_run->s390_sieic.icptcode = vcpu->arch.sie_block->icptcode;
		kvm_run->s390_sieic.mask     = vcpu->arch.sie_block->gpsw.mask;
		kvm_run->s390_sieic.addr     = vcpu->arch.sie_block->gpsw.addr;
		kvm_run->s390_sieic.ipa      = vcpu->arch.sie_block->ipa;
		kvm_run->s390_sieic.ipb      = vcpu->arch.sie_block->ipb;
		rc = 0;
	}

	if (rc == -EREMOTE) {
		/* intercept was handled, but userspace support is needed
		 * kvm_run has been prepared by the handler */
		rc = 0;
	}

	if (vcpu->sigset_active)
		sigprocmask(SIG_SETMASK, &sigsaved, NULL);

	vcpu_put(vcpu);

	vcpu->stat.exit_userspace++;
	return rc;
}

static int __guestcopy(struct kvm_vcpu *vcpu, u64 guestdest, const void *from,
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
int __kvm_s390_vcpu_store_status(struct kvm_vcpu *vcpu, unsigned long addr)
{
	const unsigned char archmode = 1;
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

	if (__guestcopy(vcpu, addr + offsetof(struct save_area_s390x, fp_regs),
			vcpu->arch.guest_fpregs.fprs, 128, prefix))
		return -EFAULT;

	if (__guestcopy(vcpu, addr + offsetof(struct save_area_s390x, gp_regs),
			vcpu->arch.guest_gprs, 128, prefix))
		return -EFAULT;

	if (__guestcopy(vcpu, addr + offsetof(struct save_area_s390x, psw),
			&vcpu->arch.sie_block->gpsw, 16, prefix))
		return -EFAULT;

	if (__guestcopy(vcpu, addr + offsetof(struct save_area_s390x, pref_reg),
			&vcpu->arch.sie_block->prefix, 4, prefix))
		return -EFAULT;

	if (__guestcopy(vcpu,
			addr + offsetof(struct save_area_s390x, fp_ctrl_reg),
			&vcpu->arch.guest_fpregs.fpc, 4, prefix))
		return -EFAULT;

	if (__guestcopy(vcpu, addr + offsetof(struct save_area_s390x, tod_reg),
			&vcpu->arch.sie_block->todpr, 4, prefix))
		return -EFAULT;

	if (__guestcopy(vcpu, addr + offsetof(struct save_area_s390x, timer),
			&vcpu->arch.sie_block->cputm, 8, prefix))
		return -EFAULT;

	if (__guestcopy(vcpu, addr + offsetof(struct save_area_s390x, clk_cmp),
			&vcpu->arch.sie_block->ckc, 8, prefix))
		return -EFAULT;

	if (__guestcopy(vcpu, addr + offsetof(struct save_area_s390x, acc_regs),
			&vcpu->arch.guest_acrs, 64, prefix))
		return -EFAULT;

	if (__guestcopy(vcpu,
			addr + offsetof(struct save_area_s390x, ctrl_regs),
			&vcpu->arch.sie_block->gcr, 128, prefix))
		return -EFAULT;
	return 0;
}

static int kvm_s390_vcpu_store_status(struct kvm_vcpu *vcpu, unsigned long addr)
{
	int rc;

	vcpu_load(vcpu);
	rc = __kvm_s390_vcpu_store_status(vcpu, addr);
	vcpu_put(vcpu);
	return rc;
}

long kvm_arch_vcpu_ioctl(struct file *filp,
			 unsigned int ioctl, unsigned long arg)
{
	struct kvm_vcpu *vcpu = filp->private_data;
	void __user *argp = (void __user *)arg;

	switch (ioctl) {
	case KVM_S390_INTERRUPT: {
		struct kvm_s390_interrupt s390int;

		if (copy_from_user(&s390int, argp, sizeof(s390int)))
			return -EFAULT;
		return kvm_s390_inject_vcpu(vcpu, &s390int);
	}
	case KVM_S390_STORE_STATUS:
		return kvm_s390_vcpu_store_status(vcpu, arg);
	case KVM_S390_SET_INITIAL_PSW: {
		psw_t psw;

		if (copy_from_user(&psw, argp, sizeof(psw)))
			return -EFAULT;
		return kvm_arch_vcpu_ioctl_set_initial_psw(vcpu, psw);
	}
	case KVM_S390_INITIAL_RESET:
		return kvm_arch_vcpu_ioctl_initial_reset(vcpu);
	default:
		;
	}
	return -EINVAL;
}

/* Section: memory related */
int kvm_arch_set_memory_region(struct kvm *kvm,
				struct kvm_userspace_memory_region *mem,
				struct kvm_memory_slot old,
				int user_alloc)
{
	int i;

	/* A few sanity checks. We can have exactly one memory slot which has
	   to start at guest virtual zero and which has to be located at a
	   page boundary in userland and which has to end at a page boundary.
	   The memory in userland is ok to be fragmented into various different
	   vmas. It is okay to mmap() and munmap() stuff in this slot after
	   doing this call at any time */

	if (mem->slot || kvm->arch.guest_memsize)
		return -EINVAL;

	if (mem->guest_phys_addr)
		return -EINVAL;

	if (mem->userspace_addr & (PAGE_SIZE - 1))
		return -EINVAL;

	if (mem->memory_size & (PAGE_SIZE - 1))
		return -EINVAL;

	if (!user_alloc)
		return -EINVAL;

	/* lock all vcpus */
	for (i = 0; i < KVM_MAX_VCPUS; ++i) {
		if (!kvm->vcpus[i])
			continue;
		if (!mutex_trylock(&kvm->vcpus[i]->mutex))
			goto fail_out;
	}

	kvm->arch.guest_origin = mem->userspace_addr;
	kvm->arch.guest_memsize = mem->memory_size;

	/* update sie control blocks, and unlock all vcpus */
	for (i = 0; i < KVM_MAX_VCPUS; ++i) {
		if (kvm->vcpus[i]) {
			kvm->vcpus[i]->arch.sie_block->gmsor =
				kvm->arch.guest_origin;
			kvm->vcpus[i]->arch.sie_block->gmslm =
				kvm->arch.guest_memsize +
				kvm->arch.guest_origin +
				VIRTIODESCSPACE - 1ul;
			mutex_unlock(&kvm->vcpus[i]->mutex);
		}
	}

	return 0;

fail_out:
	for (; i >= 0; i--)
		mutex_unlock(&kvm->vcpus[i]->mutex);
	return -EINVAL;
}

void kvm_arch_flush_shadow(struct kvm *kvm)
{
}

gfn_t unalias_gfn(struct kvm *kvm, gfn_t gfn)
{
	return gfn;
}

static int __init kvm_s390_init(void)
{
	return kvm_init(NULL, sizeof(struct kvm_vcpu), THIS_MODULE);
}

static void __exit kvm_s390_exit(void)
{
	kvm_exit();
}

module_init(kvm_s390_init);
module_exit(kvm_s390_exit);
