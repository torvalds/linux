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
 *               Jason J. Herne <jjherne@us.ibm.com>
 */

#include <linux/compiler.h>
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/hrtimer.h>
#include <linux/init.h>
#include <linux/kvm.h>
#include <linux/kvm_host.h>
#include <linux/module.h>
#include <linux/random.h>
#include <linux/slab.h>
#include <linux/timer.h>
#include <linux/vmalloc.h>
#include <asm/asm-offsets.h>
#include <asm/lowcore.h>
#include <asm/etr.h>
#include <asm/pgtable.h>
#include <asm/nmi.h>
#include <asm/switch_to.h>
#include <asm/isc.h>
#include <asm/sclp.h>
#include "kvm-s390.h"
#include "gaccess.h"

#define KMSG_COMPONENT "kvm-s390"
#undef pr_fmt
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#define CREATE_TRACE_POINTS
#include "trace.h"
#include "trace-s390.h"

#define MEM_OP_MAX_SIZE 65536	/* Maximum transfer size for KVM_S390_MEM_OP */
#define LOCAL_IRQS 32
#define VCPU_IRQS_MAX_BUF (sizeof(struct kvm_s390_irq) * \
			   (KVM_MAX_VCPUS + LOCAL_IRQS))

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
	{ "halt_successful_poll", VCPU_STAT(halt_successful_poll) },
	{ "halt_attempted_poll", VCPU_STAT(halt_attempted_poll) },
	{ "halt_wakeup", VCPU_STAT(halt_wakeup) },
	{ "instruction_lctlg", VCPU_STAT(instruction_lctlg) },
	{ "instruction_lctl", VCPU_STAT(instruction_lctl) },
	{ "instruction_stctl", VCPU_STAT(instruction_stctl) },
	{ "instruction_stctg", VCPU_STAT(instruction_stctg) },
	{ "deliver_emergency_signal", VCPU_STAT(deliver_emergency_signal) },
	{ "deliver_external_call", VCPU_STAT(deliver_external_call) },
	{ "deliver_service_signal", VCPU_STAT(deliver_service_signal) },
	{ "deliver_virtio_interrupt", VCPU_STAT(deliver_virtio_interrupt) },
	{ "deliver_stop_signal", VCPU_STAT(deliver_stop_signal) },
	{ "deliver_prefix_signal", VCPU_STAT(deliver_prefix_signal) },
	{ "deliver_restart_signal", VCPU_STAT(deliver_restart_signal) },
	{ "deliver_program_interruption", VCPU_STAT(deliver_program_int) },
	{ "exit_wait_state", VCPU_STAT(exit_wait_state) },
	{ "instruction_pfmf", VCPU_STAT(instruction_pfmf) },
	{ "instruction_stidp", VCPU_STAT(instruction_stidp) },
	{ "instruction_spx", VCPU_STAT(instruction_spx) },
	{ "instruction_stpx", VCPU_STAT(instruction_stpx) },
	{ "instruction_stap", VCPU_STAT(instruction_stap) },
	{ "instruction_storage_key", VCPU_STAT(instruction_storage_key) },
	{ "instruction_ipte_interlock", VCPU_STAT(instruction_ipte_interlock) },
	{ "instruction_stsch", VCPU_STAT(instruction_stsch) },
	{ "instruction_chsc", VCPU_STAT(instruction_chsc) },
	{ "instruction_essa", VCPU_STAT(instruction_essa) },
	{ "instruction_stsi", VCPU_STAT(instruction_stsi) },
	{ "instruction_stfl", VCPU_STAT(instruction_stfl) },
	{ "instruction_tprot", VCPU_STAT(instruction_tprot) },
	{ "instruction_sigp_sense", VCPU_STAT(instruction_sigp_sense) },
	{ "instruction_sigp_sense_running", VCPU_STAT(instruction_sigp_sense_running) },
	{ "instruction_sigp_external_call", VCPU_STAT(instruction_sigp_external_call) },
	{ "instruction_sigp_emergency", VCPU_STAT(instruction_sigp_emergency) },
	{ "instruction_sigp_cond_emergency", VCPU_STAT(instruction_sigp_cond_emergency) },
	{ "instruction_sigp_start", VCPU_STAT(instruction_sigp_start) },
	{ "instruction_sigp_stop", VCPU_STAT(instruction_sigp_stop) },
	{ "instruction_sigp_stop_store_status", VCPU_STAT(instruction_sigp_stop_store_status) },
	{ "instruction_sigp_store_status", VCPU_STAT(instruction_sigp_store_status) },
	{ "instruction_sigp_store_adtl_status", VCPU_STAT(instruction_sigp_store_adtl_status) },
	{ "instruction_sigp_set_arch", VCPU_STAT(instruction_sigp_arch) },
	{ "instruction_sigp_set_prefix", VCPU_STAT(instruction_sigp_prefix) },
	{ "instruction_sigp_restart", VCPU_STAT(instruction_sigp_restart) },
	{ "instruction_sigp_cpu_reset", VCPU_STAT(instruction_sigp_cpu_reset) },
	{ "instruction_sigp_init_cpu_reset", VCPU_STAT(instruction_sigp_init_cpu_reset) },
	{ "instruction_sigp_unknown", VCPU_STAT(instruction_sigp_unknown) },
	{ "diagnose_10", VCPU_STAT(diagnose_10) },
	{ "diagnose_44", VCPU_STAT(diagnose_44) },
	{ "diagnose_9c", VCPU_STAT(diagnose_9c) },
	{ "diagnose_258", VCPU_STAT(diagnose_258) },
	{ "diagnose_308", VCPU_STAT(diagnose_308) },
	{ "diagnose_500", VCPU_STAT(diagnose_500) },
	{ NULL }
};

/* upper facilities limit for kvm */
unsigned long kvm_s390_fac_list_mask[] = {
	0xffe6fffbfcfdfc40UL,
	0x005e800000000000UL,
};

unsigned long kvm_s390_fac_list_mask_size(void)
{
	BUILD_BUG_ON(ARRAY_SIZE(kvm_s390_fac_list_mask) > S390_ARCH_FAC_MASK_SIZE_U64);
	return ARRAY_SIZE(kvm_s390_fac_list_mask);
}

static struct gmap_notifier gmap_notifier;
debug_info_t *kvm_s390_dbf;

/* Section: not file related */
int kvm_arch_hardware_enable(void)
{
	/* every s390 is virtualization enabled ;-) */
	return 0;
}

static void kvm_gmap_notifier(struct gmap *gmap, unsigned long address);

/*
 * This callback is executed during stop_machine(). All CPUs are therefore
 * temporarily stopped. In order not to change guest behavior, we have to
 * disable preemption whenever we touch the epoch of kvm and the VCPUs,
 * so a CPU won't be stopped while calculating with the epoch.
 */
static int kvm_clock_sync(struct notifier_block *notifier, unsigned long val,
			  void *v)
{
	struct kvm *kvm;
	struct kvm_vcpu *vcpu;
	int i;
	unsigned long long *delta = v;

	list_for_each_entry(kvm, &vm_list, vm_list) {
		kvm->arch.epoch -= *delta;
		kvm_for_each_vcpu(i, vcpu, kvm) {
			vcpu->arch.sie_block->epoch -= *delta;
		}
	}
	return NOTIFY_OK;
}

static struct notifier_block kvm_clock_notifier = {
	.notifier_call = kvm_clock_sync,
};

int kvm_arch_hardware_setup(void)
{
	gmap_notifier.notifier_call = kvm_gmap_notifier;
	gmap_register_ipte_notifier(&gmap_notifier);
	atomic_notifier_chain_register(&s390_epoch_delta_notifier,
				       &kvm_clock_notifier);
	return 0;
}

void kvm_arch_hardware_unsetup(void)
{
	gmap_unregister_ipte_notifier(&gmap_notifier);
	atomic_notifier_chain_unregister(&s390_epoch_delta_notifier,
					 &kvm_clock_notifier);
}

int kvm_arch_init(void *opaque)
{
	kvm_s390_dbf = debug_register("kvm-trace", 32, 1, 7 * sizeof(long));
	if (!kvm_s390_dbf)
		return -ENOMEM;

	if (debug_register_view(kvm_s390_dbf, &debug_sprintf_view)) {
		debug_unregister(kvm_s390_dbf);
		return -ENOMEM;
	}

	/* Register floating interrupt controller interface. */
	return kvm_register_device_ops(&kvm_flic_ops, KVM_DEV_TYPE_FLIC);
}

void kvm_arch_exit(void)
{
	debug_unregister(kvm_s390_dbf);
}

/* Section: device related */
long kvm_arch_dev_ioctl(struct file *filp,
			unsigned int ioctl, unsigned long arg)
{
	if (ioctl == KVM_S390_ENABLE_SIE)
		return s390_enable_sie();
	return -EINVAL;
}

int kvm_vm_ioctl_check_extension(struct kvm *kvm, long ext)
{
	int r;

	switch (ext) {
	case KVM_CAP_S390_PSW:
	case KVM_CAP_S390_GMAP:
	case KVM_CAP_SYNC_MMU:
#ifdef CONFIG_KVM_S390_UCONTROL
	case KVM_CAP_S390_UCONTROL:
#endif
	case KVM_CAP_ASYNC_PF:
	case KVM_CAP_SYNC_REGS:
	case KVM_CAP_ONE_REG:
	case KVM_CAP_ENABLE_CAP:
	case KVM_CAP_S390_CSS_SUPPORT:
	case KVM_CAP_IOEVENTFD:
	case KVM_CAP_DEVICE_CTRL:
	case KVM_CAP_ENABLE_CAP_VM:
	case KVM_CAP_S390_IRQCHIP:
	case KVM_CAP_VM_ATTRIBUTES:
	case KVM_CAP_MP_STATE:
	case KVM_CAP_S390_INJECT_IRQ:
	case KVM_CAP_S390_USER_SIGP:
	case KVM_CAP_S390_USER_STSI:
	case KVM_CAP_S390_SKEYS:
	case KVM_CAP_S390_IRQ_STATE:
		r = 1;
		break;
	case KVM_CAP_S390_MEM_OP:
		r = MEM_OP_MAX_SIZE;
		break;
	case KVM_CAP_NR_VCPUS:
	case KVM_CAP_MAX_VCPUS:
		r = sclp.has_esca ? KVM_S390_ESCA_CPU_SLOTS
				  : KVM_S390_BSCA_CPU_SLOTS;
		break;
	case KVM_CAP_NR_MEMSLOTS:
		r = KVM_USER_MEM_SLOTS;
		break;
	case KVM_CAP_S390_COW:
		r = MACHINE_HAS_ESOP;
		break;
	case KVM_CAP_S390_VECTOR_REGISTERS:
		r = MACHINE_HAS_VX;
		break;
	default:
		r = 0;
	}
	return r;
}

static void kvm_s390_sync_dirty_log(struct kvm *kvm,
					struct kvm_memory_slot *memslot)
{
	gfn_t cur_gfn, last_gfn;
	unsigned long address;
	struct gmap *gmap = kvm->arch.gmap;

	down_read(&gmap->mm->mmap_sem);
	/* Loop over all guest pages */
	last_gfn = memslot->base_gfn + memslot->npages;
	for (cur_gfn = memslot->base_gfn; cur_gfn <= last_gfn; cur_gfn++) {
		address = gfn_to_hva_memslot(memslot, cur_gfn);

		if (gmap_test_and_clear_dirty(address, gmap))
			mark_page_dirty(kvm, cur_gfn);
	}
	up_read(&gmap->mm->mmap_sem);
}

/* Section: vm related */
static void sca_del_vcpu(struct kvm_vcpu *vcpu);

/*
 * Get (and clear) the dirty memory log for a memory slot.
 */
int kvm_vm_ioctl_get_dirty_log(struct kvm *kvm,
			       struct kvm_dirty_log *log)
{
	int r;
	unsigned long n;
	struct kvm_memslots *slots;
	struct kvm_memory_slot *memslot;
	int is_dirty = 0;

	mutex_lock(&kvm->slots_lock);

	r = -EINVAL;
	if (log->slot >= KVM_USER_MEM_SLOTS)
		goto out;

	slots = kvm_memslots(kvm);
	memslot = id_to_memslot(slots, log->slot);
	r = -ENOENT;
	if (!memslot->dirty_bitmap)
		goto out;

	kvm_s390_sync_dirty_log(kvm, memslot);
	r = kvm_get_dirty_log(kvm, log, &is_dirty);
	if (r)
		goto out;

	/* Clear the dirty log */
	if (is_dirty) {
		n = kvm_dirty_bitmap_bytes(memslot);
		memset(memslot->dirty_bitmap, 0, n);
	}
	r = 0;
out:
	mutex_unlock(&kvm->slots_lock);
	return r;
}

static int kvm_vm_ioctl_enable_cap(struct kvm *kvm, struct kvm_enable_cap *cap)
{
	int r;

	if (cap->flags)
		return -EINVAL;

	switch (cap->cap) {
	case KVM_CAP_S390_IRQCHIP:
		VM_EVENT(kvm, 3, "%s", "ENABLE: CAP_S390_IRQCHIP");
		kvm->arch.use_irqchip = 1;
		r = 0;
		break;
	case KVM_CAP_S390_USER_SIGP:
		VM_EVENT(kvm, 3, "%s", "ENABLE: CAP_S390_USER_SIGP");
		kvm->arch.user_sigp = 1;
		r = 0;
		break;
	case KVM_CAP_S390_VECTOR_REGISTERS:
		mutex_lock(&kvm->lock);
		if (atomic_read(&kvm->online_vcpus)) {
			r = -EBUSY;
		} else if (MACHINE_HAS_VX) {
			set_kvm_facility(kvm->arch.model.fac->mask, 129);
			set_kvm_facility(kvm->arch.model.fac->list, 129);
			r = 0;
		} else
			r = -EINVAL;
		mutex_unlock(&kvm->lock);
		VM_EVENT(kvm, 3, "ENABLE: CAP_S390_VECTOR_REGISTERS %s",
			 r ? "(not available)" : "(success)");
		break;
	case KVM_CAP_S390_USER_STSI:
		VM_EVENT(kvm, 3, "%s", "ENABLE: CAP_S390_USER_STSI");
		kvm->arch.user_stsi = 1;
		r = 0;
		break;
	default:
		r = -EINVAL;
		break;
	}
	return r;
}

static int kvm_s390_get_mem_control(struct kvm *kvm, struct kvm_device_attr *attr)
{
	int ret;

	switch (attr->attr) {
	case KVM_S390_VM_MEM_LIMIT_SIZE:
		ret = 0;
		VM_EVENT(kvm, 3, "QUERY: max guest memory: %lu bytes",
			 kvm->arch.gmap->asce_end);
		if (put_user(kvm->arch.gmap->asce_end, (u64 __user *)attr->addr))
			ret = -EFAULT;
		break;
	default:
		ret = -ENXIO;
		break;
	}
	return ret;
}

static int kvm_s390_set_mem_control(struct kvm *kvm, struct kvm_device_attr *attr)
{
	int ret;
	unsigned int idx;
	switch (attr->attr) {
	case KVM_S390_VM_MEM_ENABLE_CMMA:
		/* enable CMMA only for z10 and later (EDAT_1) */
		ret = -EINVAL;
		if (!MACHINE_IS_LPAR || !MACHINE_HAS_EDAT1)
			break;

		ret = -EBUSY;
		VM_EVENT(kvm, 3, "%s", "ENABLE: CMMA support");
		mutex_lock(&kvm->lock);
		if (atomic_read(&kvm->online_vcpus) == 0) {
			kvm->arch.use_cmma = 1;
			ret = 0;
		}
		mutex_unlock(&kvm->lock);
		break;
	case KVM_S390_VM_MEM_CLR_CMMA:
		ret = -EINVAL;
		if (!kvm->arch.use_cmma)
			break;

		VM_EVENT(kvm, 3, "%s", "RESET: CMMA states");
		mutex_lock(&kvm->lock);
		idx = srcu_read_lock(&kvm->srcu);
		s390_reset_cmma(kvm->arch.gmap->mm);
		srcu_read_unlock(&kvm->srcu, idx);
		mutex_unlock(&kvm->lock);
		ret = 0;
		break;
	case KVM_S390_VM_MEM_LIMIT_SIZE: {
		unsigned long new_limit;

		if (kvm_is_ucontrol(kvm))
			return -EINVAL;

		if (get_user(new_limit, (u64 __user *)attr->addr))
			return -EFAULT;

		if (new_limit > kvm->arch.gmap->asce_end)
			return -E2BIG;

		ret = -EBUSY;
		mutex_lock(&kvm->lock);
		if (atomic_read(&kvm->online_vcpus) == 0) {
			/* gmap_alloc will round the limit up */
			struct gmap *new = gmap_alloc(current->mm, new_limit);

			if (!new) {
				ret = -ENOMEM;
			} else {
				gmap_free(kvm->arch.gmap);
				new->private = kvm;
				kvm->arch.gmap = new;
				ret = 0;
			}
		}
		mutex_unlock(&kvm->lock);
		VM_EVENT(kvm, 3, "SET: max guest memory: %lu bytes", new_limit);
		break;
	}
	default:
		ret = -ENXIO;
		break;
	}
	return ret;
}

static void kvm_s390_vcpu_crypto_setup(struct kvm_vcpu *vcpu);

static int kvm_s390_vm_set_crypto(struct kvm *kvm, struct kvm_device_attr *attr)
{
	struct kvm_vcpu *vcpu;
	int i;

	if (!test_kvm_facility(kvm, 76))
		return -EINVAL;

	mutex_lock(&kvm->lock);
	switch (attr->attr) {
	case KVM_S390_VM_CRYPTO_ENABLE_AES_KW:
		get_random_bytes(
			kvm->arch.crypto.crycb->aes_wrapping_key_mask,
			sizeof(kvm->arch.crypto.crycb->aes_wrapping_key_mask));
		kvm->arch.crypto.aes_kw = 1;
		VM_EVENT(kvm, 3, "%s", "ENABLE: AES keywrapping support");
		break;
	case KVM_S390_VM_CRYPTO_ENABLE_DEA_KW:
		get_random_bytes(
			kvm->arch.crypto.crycb->dea_wrapping_key_mask,
			sizeof(kvm->arch.crypto.crycb->dea_wrapping_key_mask));
		kvm->arch.crypto.dea_kw = 1;
		VM_EVENT(kvm, 3, "%s", "ENABLE: DEA keywrapping support");
		break;
	case KVM_S390_VM_CRYPTO_DISABLE_AES_KW:
		kvm->arch.crypto.aes_kw = 0;
		memset(kvm->arch.crypto.crycb->aes_wrapping_key_mask, 0,
			sizeof(kvm->arch.crypto.crycb->aes_wrapping_key_mask));
		VM_EVENT(kvm, 3, "%s", "DISABLE: AES keywrapping support");
		break;
	case KVM_S390_VM_CRYPTO_DISABLE_DEA_KW:
		kvm->arch.crypto.dea_kw = 0;
		memset(kvm->arch.crypto.crycb->dea_wrapping_key_mask, 0,
			sizeof(kvm->arch.crypto.crycb->dea_wrapping_key_mask));
		VM_EVENT(kvm, 3, "%s", "DISABLE: DEA keywrapping support");
		break;
	default:
		mutex_unlock(&kvm->lock);
		return -ENXIO;
	}

	kvm_for_each_vcpu(i, vcpu, kvm) {
		kvm_s390_vcpu_crypto_setup(vcpu);
		exit_sie(vcpu);
	}
	mutex_unlock(&kvm->lock);
	return 0;
}

static int kvm_s390_set_tod_high(struct kvm *kvm, struct kvm_device_attr *attr)
{
	u8 gtod_high;

	if (copy_from_user(&gtod_high, (void __user *)attr->addr,
					   sizeof(gtod_high)))
		return -EFAULT;

	if (gtod_high != 0)
		return -EINVAL;
	VM_EVENT(kvm, 3, "SET: TOD extension: 0x%x", gtod_high);

	return 0;
}

static int kvm_s390_set_tod_low(struct kvm *kvm, struct kvm_device_attr *attr)
{
	u64 gtod;

	if (copy_from_user(&gtod, (void __user *)attr->addr, sizeof(gtod)))
		return -EFAULT;

	kvm_s390_set_tod_clock(kvm, gtod);
	VM_EVENT(kvm, 3, "SET: TOD base: 0x%llx", gtod);
	return 0;
}

static int kvm_s390_set_tod(struct kvm *kvm, struct kvm_device_attr *attr)
{
	int ret;

	if (attr->flags)
		return -EINVAL;

	switch (attr->attr) {
	case KVM_S390_VM_TOD_HIGH:
		ret = kvm_s390_set_tod_high(kvm, attr);
		break;
	case KVM_S390_VM_TOD_LOW:
		ret = kvm_s390_set_tod_low(kvm, attr);
		break;
	default:
		ret = -ENXIO;
		break;
	}
	return ret;
}

static int kvm_s390_get_tod_high(struct kvm *kvm, struct kvm_device_attr *attr)
{
	u8 gtod_high = 0;

	if (copy_to_user((void __user *)attr->addr, &gtod_high,
					 sizeof(gtod_high)))
		return -EFAULT;
	VM_EVENT(kvm, 3, "QUERY: TOD extension: 0x%x", gtod_high);

	return 0;
}

static int kvm_s390_get_tod_low(struct kvm *kvm, struct kvm_device_attr *attr)
{
	u64 gtod;

	gtod = kvm_s390_get_tod_clock_fast(kvm);
	if (copy_to_user((void __user *)attr->addr, &gtod, sizeof(gtod)))
		return -EFAULT;
	VM_EVENT(kvm, 3, "QUERY: TOD base: 0x%llx", gtod);

	return 0;
}

static int kvm_s390_get_tod(struct kvm *kvm, struct kvm_device_attr *attr)
{
	int ret;

	if (attr->flags)
		return -EINVAL;

	switch (attr->attr) {
	case KVM_S390_VM_TOD_HIGH:
		ret = kvm_s390_get_tod_high(kvm, attr);
		break;
	case KVM_S390_VM_TOD_LOW:
		ret = kvm_s390_get_tod_low(kvm, attr);
		break;
	default:
		ret = -ENXIO;
		break;
	}
	return ret;
}

static int kvm_s390_set_processor(struct kvm *kvm, struct kvm_device_attr *attr)
{
	struct kvm_s390_vm_cpu_processor *proc;
	int ret = 0;

	mutex_lock(&kvm->lock);
	if (atomic_read(&kvm->online_vcpus)) {
		ret = -EBUSY;
		goto out;
	}
	proc = kzalloc(sizeof(*proc), GFP_KERNEL);
	if (!proc) {
		ret = -ENOMEM;
		goto out;
	}
	if (!copy_from_user(proc, (void __user *)attr->addr,
			    sizeof(*proc))) {
		memcpy(&kvm->arch.model.cpu_id, &proc->cpuid,
		       sizeof(struct cpuid));
		kvm->arch.model.ibc = proc->ibc;
		memcpy(kvm->arch.model.fac->list, proc->fac_list,
		       S390_ARCH_FAC_LIST_SIZE_BYTE);
	} else
		ret = -EFAULT;
	kfree(proc);
out:
	mutex_unlock(&kvm->lock);
	return ret;
}

static int kvm_s390_set_cpu_model(struct kvm *kvm, struct kvm_device_attr *attr)
{
	int ret = -ENXIO;

	switch (attr->attr) {
	case KVM_S390_VM_CPU_PROCESSOR:
		ret = kvm_s390_set_processor(kvm, attr);
		break;
	}
	return ret;
}

static int kvm_s390_get_processor(struct kvm *kvm, struct kvm_device_attr *attr)
{
	struct kvm_s390_vm_cpu_processor *proc;
	int ret = 0;

	proc = kzalloc(sizeof(*proc), GFP_KERNEL);
	if (!proc) {
		ret = -ENOMEM;
		goto out;
	}
	memcpy(&proc->cpuid, &kvm->arch.model.cpu_id, sizeof(struct cpuid));
	proc->ibc = kvm->arch.model.ibc;
	memcpy(&proc->fac_list, kvm->arch.model.fac->list, S390_ARCH_FAC_LIST_SIZE_BYTE);
	if (copy_to_user((void __user *)attr->addr, proc, sizeof(*proc)))
		ret = -EFAULT;
	kfree(proc);
out:
	return ret;
}

static int kvm_s390_get_machine(struct kvm *kvm, struct kvm_device_attr *attr)
{
	struct kvm_s390_vm_cpu_machine *mach;
	int ret = 0;

	mach = kzalloc(sizeof(*mach), GFP_KERNEL);
	if (!mach) {
		ret = -ENOMEM;
		goto out;
	}
	get_cpu_id((struct cpuid *) &mach->cpuid);
	mach->ibc = sclp.ibc;
	memcpy(&mach->fac_mask, kvm->arch.model.fac->mask,
	       S390_ARCH_FAC_LIST_SIZE_BYTE);
	memcpy((unsigned long *)&mach->fac_list, S390_lowcore.stfle_fac_list,
	       S390_ARCH_FAC_LIST_SIZE_BYTE);
	if (copy_to_user((void __user *)attr->addr, mach, sizeof(*mach)))
		ret = -EFAULT;
	kfree(mach);
out:
	return ret;
}

static int kvm_s390_get_cpu_model(struct kvm *kvm, struct kvm_device_attr *attr)
{
	int ret = -ENXIO;

	switch (attr->attr) {
	case KVM_S390_VM_CPU_PROCESSOR:
		ret = kvm_s390_get_processor(kvm, attr);
		break;
	case KVM_S390_VM_CPU_MACHINE:
		ret = kvm_s390_get_machine(kvm, attr);
		break;
	}
	return ret;
}

static int kvm_s390_vm_set_attr(struct kvm *kvm, struct kvm_device_attr *attr)
{
	int ret;

	switch (attr->group) {
	case KVM_S390_VM_MEM_CTRL:
		ret = kvm_s390_set_mem_control(kvm, attr);
		break;
	case KVM_S390_VM_TOD:
		ret = kvm_s390_set_tod(kvm, attr);
		break;
	case KVM_S390_VM_CPU_MODEL:
		ret = kvm_s390_set_cpu_model(kvm, attr);
		break;
	case KVM_S390_VM_CRYPTO:
		ret = kvm_s390_vm_set_crypto(kvm, attr);
		break;
	default:
		ret = -ENXIO;
		break;
	}

	return ret;
}

static int kvm_s390_vm_get_attr(struct kvm *kvm, struct kvm_device_attr *attr)
{
	int ret;

	switch (attr->group) {
	case KVM_S390_VM_MEM_CTRL:
		ret = kvm_s390_get_mem_control(kvm, attr);
		break;
	case KVM_S390_VM_TOD:
		ret = kvm_s390_get_tod(kvm, attr);
		break;
	case KVM_S390_VM_CPU_MODEL:
		ret = kvm_s390_get_cpu_model(kvm, attr);
		break;
	default:
		ret = -ENXIO;
		break;
	}

	return ret;
}

static int kvm_s390_vm_has_attr(struct kvm *kvm, struct kvm_device_attr *attr)
{
	int ret;

	switch (attr->group) {
	case KVM_S390_VM_MEM_CTRL:
		switch (attr->attr) {
		case KVM_S390_VM_MEM_ENABLE_CMMA:
		case KVM_S390_VM_MEM_CLR_CMMA:
		case KVM_S390_VM_MEM_LIMIT_SIZE:
			ret = 0;
			break;
		default:
			ret = -ENXIO;
			break;
		}
		break;
	case KVM_S390_VM_TOD:
		switch (attr->attr) {
		case KVM_S390_VM_TOD_LOW:
		case KVM_S390_VM_TOD_HIGH:
			ret = 0;
			break;
		default:
			ret = -ENXIO;
			break;
		}
		break;
	case KVM_S390_VM_CPU_MODEL:
		switch (attr->attr) {
		case KVM_S390_VM_CPU_PROCESSOR:
		case KVM_S390_VM_CPU_MACHINE:
			ret = 0;
			break;
		default:
			ret = -ENXIO;
			break;
		}
		break;
	case KVM_S390_VM_CRYPTO:
		switch (attr->attr) {
		case KVM_S390_VM_CRYPTO_ENABLE_AES_KW:
		case KVM_S390_VM_CRYPTO_ENABLE_DEA_KW:
		case KVM_S390_VM_CRYPTO_DISABLE_AES_KW:
		case KVM_S390_VM_CRYPTO_DISABLE_DEA_KW:
			ret = 0;
			break;
		default:
			ret = -ENXIO;
			break;
		}
		break;
	default:
		ret = -ENXIO;
		break;
	}

	return ret;
}

static long kvm_s390_get_skeys(struct kvm *kvm, struct kvm_s390_skeys *args)
{
	uint8_t *keys;
	uint64_t hva;
	unsigned long curkey;
	int i, r = 0;

	if (args->flags != 0)
		return -EINVAL;

	/* Is this guest using storage keys? */
	if (!mm_use_skey(current->mm))
		return KVM_S390_GET_SKEYS_NONE;

	/* Enforce sane limit on memory allocation */
	if (args->count < 1 || args->count > KVM_S390_SKEYS_MAX)
		return -EINVAL;

	keys = kmalloc_array(args->count, sizeof(uint8_t),
			     GFP_KERNEL | __GFP_NOWARN);
	if (!keys)
		keys = vmalloc(sizeof(uint8_t) * args->count);
	if (!keys)
		return -ENOMEM;

	for (i = 0; i < args->count; i++) {
		hva = gfn_to_hva(kvm, args->start_gfn + i);
		if (kvm_is_error_hva(hva)) {
			r = -EFAULT;
			goto out;
		}

		curkey = get_guest_storage_key(current->mm, hva);
		if (IS_ERR_VALUE(curkey)) {
			r = curkey;
			goto out;
		}
		keys[i] = curkey;
	}

	r = copy_to_user((uint8_t __user *)args->skeydata_addr, keys,
			 sizeof(uint8_t) * args->count);
	if (r)
		r = -EFAULT;
out:
	kvfree(keys);
	return r;
}

static long kvm_s390_set_skeys(struct kvm *kvm, struct kvm_s390_skeys *args)
{
	uint8_t *keys;
	uint64_t hva;
	int i, r = 0;

	if (args->flags != 0)
		return -EINVAL;

	/* Enforce sane limit on memory allocation */
	if (args->count < 1 || args->count > KVM_S390_SKEYS_MAX)
		return -EINVAL;

	keys = kmalloc_array(args->count, sizeof(uint8_t),
			     GFP_KERNEL | __GFP_NOWARN);
	if (!keys)
		keys = vmalloc(sizeof(uint8_t) * args->count);
	if (!keys)
		return -ENOMEM;

	r = copy_from_user(keys, (uint8_t __user *)args->skeydata_addr,
			   sizeof(uint8_t) * args->count);
	if (r) {
		r = -EFAULT;
		goto out;
	}

	/* Enable storage key handling for the guest */
	r = s390_enable_skey();
	if (r)
		goto out;

	for (i = 0; i < args->count; i++) {
		hva = gfn_to_hva(kvm, args->start_gfn + i);
		if (kvm_is_error_hva(hva)) {
			r = -EFAULT;
			goto out;
		}

		/* Lowest order bit is reserved */
		if (keys[i] & 0x01) {
			r = -EINVAL;
			goto out;
		}

		r = set_guest_storage_key(current->mm, hva,
					  (unsigned long)keys[i], 0);
		if (r)
			goto out;
	}
out:
	kvfree(keys);
	return r;
}

long kvm_arch_vm_ioctl(struct file *filp,
		       unsigned int ioctl, unsigned long arg)
{
	struct kvm *kvm = filp->private_data;
	void __user *argp = (void __user *)arg;
	struct kvm_device_attr attr;
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
	case KVM_ENABLE_CAP: {
		struct kvm_enable_cap cap;
		r = -EFAULT;
		if (copy_from_user(&cap, argp, sizeof(cap)))
			break;
		r = kvm_vm_ioctl_enable_cap(kvm, &cap);
		break;
	}
	case KVM_CREATE_IRQCHIP: {
		struct kvm_irq_routing_entry routing;

		r = -EINVAL;
		if (kvm->arch.use_irqchip) {
			/* Set up dummy routing. */
			memset(&routing, 0, sizeof(routing));
			r = kvm_set_irq_routing(kvm, &routing, 0, 0);
		}
		break;
	}
	case KVM_SET_DEVICE_ATTR: {
		r = -EFAULT;
		if (copy_from_user(&attr, (void __user *)arg, sizeof(attr)))
			break;
		r = kvm_s390_vm_set_attr(kvm, &attr);
		break;
	}
	case KVM_GET_DEVICE_ATTR: {
		r = -EFAULT;
		if (copy_from_user(&attr, (void __user *)arg, sizeof(attr)))
			break;
		r = kvm_s390_vm_get_attr(kvm, &attr);
		break;
	}
	case KVM_HAS_DEVICE_ATTR: {
		r = -EFAULT;
		if (copy_from_user(&attr, (void __user *)arg, sizeof(attr)))
			break;
		r = kvm_s390_vm_has_attr(kvm, &attr);
		break;
	}
	case KVM_S390_GET_SKEYS: {
		struct kvm_s390_skeys args;

		r = -EFAULT;
		if (copy_from_user(&args, argp,
				   sizeof(struct kvm_s390_skeys)))
			break;
		r = kvm_s390_get_skeys(kvm, &args);
		break;
	}
	case KVM_S390_SET_SKEYS: {
		struct kvm_s390_skeys args;

		r = -EFAULT;
		if (copy_from_user(&args, argp,
				   sizeof(struct kvm_s390_skeys)))
			break;
		r = kvm_s390_set_skeys(kvm, &args);
		break;
	}
	default:
		r = -ENOTTY;
	}

	return r;
}

static int kvm_s390_query_ap_config(u8 *config)
{
	u32 fcn_code = 0x04000000UL;
	u32 cc = 0;

	memset(config, 0, 128);
	asm volatile(
		"lgr 0,%1\n"
		"lgr 2,%2\n"
		".long 0xb2af0000\n"		/* PQAP(QCI) */
		"0: ipm %0\n"
		"srl %0,28\n"
		"1:\n"
		EX_TABLE(0b, 1b)
		: "+r" (cc)
		: "r" (fcn_code), "r" (config)
		: "cc", "0", "2", "memory"
	);

	return cc;
}

static int kvm_s390_apxa_installed(void)
{
	u8 config[128];
	int cc;

	if (test_facility(12)) {
		cc = kvm_s390_query_ap_config(config);

		if (cc)
			pr_err("PQAP(QCI) failed with cc=%d", cc);
		else
			return config[0] & 0x40;
	}

	return 0;
}

static void kvm_s390_set_crycb_format(struct kvm *kvm)
{
	kvm->arch.crypto.crycbd = (__u32)(unsigned long) kvm->arch.crypto.crycb;

	if (kvm_s390_apxa_installed())
		kvm->arch.crypto.crycbd |= CRYCB_FORMAT2;
	else
		kvm->arch.crypto.crycbd |= CRYCB_FORMAT1;
}

static void kvm_s390_get_cpu_id(struct cpuid *cpu_id)
{
	get_cpu_id(cpu_id);
	cpu_id->version = 0xff;
}

static int kvm_s390_crypto_init(struct kvm *kvm)
{
	if (!test_kvm_facility(kvm, 76))
		return 0;

	kvm->arch.crypto.crycb = kzalloc(sizeof(*kvm->arch.crypto.crycb),
					 GFP_KERNEL | GFP_DMA);
	if (!kvm->arch.crypto.crycb)
		return -ENOMEM;

	kvm_s390_set_crycb_format(kvm);

	/* Enable AES/DEA protected key functions by default */
	kvm->arch.crypto.aes_kw = 1;
	kvm->arch.crypto.dea_kw = 1;
	get_random_bytes(kvm->arch.crypto.crycb->aes_wrapping_key_mask,
			 sizeof(kvm->arch.crypto.crycb->aes_wrapping_key_mask));
	get_random_bytes(kvm->arch.crypto.crycb->dea_wrapping_key_mask,
			 sizeof(kvm->arch.crypto.crycb->dea_wrapping_key_mask));

	return 0;
}

static void sca_dispose(struct kvm *kvm)
{
	if (kvm->arch.use_esca)
		free_pages_exact(kvm->arch.sca, sizeof(struct esca_block));
	else
		free_page((unsigned long)(kvm->arch.sca));
	kvm->arch.sca = NULL;
}

int kvm_arch_init_vm(struct kvm *kvm, unsigned long type)
{
	int i, rc;
	char debug_name[16];
	static unsigned long sca_offset;

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

	kvm->arch.use_esca = 0; /* start with basic SCA */
	rwlock_init(&kvm->arch.sca_lock);
	kvm->arch.sca = (struct bsca_block *) get_zeroed_page(GFP_KERNEL);
	if (!kvm->arch.sca)
		goto out_err;
	spin_lock(&kvm_lock);
	sca_offset += 16;
	if (sca_offset + sizeof(struct bsca_block) > PAGE_SIZE)
		sca_offset = 0;
	kvm->arch.sca = (struct bsca_block *)
			((char *) kvm->arch.sca + sca_offset);
	spin_unlock(&kvm_lock);

	sprintf(debug_name, "kvm-%u", current->pid);

	kvm->arch.dbf = debug_register(debug_name, 32, 1, 7 * sizeof(long));
	if (!kvm->arch.dbf)
		goto out_err;

	/*
	 * The architectural maximum amount of facilities is 16 kbit. To store
	 * this amount, 2 kbyte of memory is required. Thus we need a full
	 * page to hold the guest facility list (arch.model.fac->list) and the
	 * facility mask (arch.model.fac->mask). Its address size has to be
	 * 31 bits and word aligned.
	 */
	kvm->arch.model.fac =
		(struct kvm_s390_fac *) get_zeroed_page(GFP_KERNEL | GFP_DMA);
	if (!kvm->arch.model.fac)
		goto out_err;

	/* Populate the facility mask initially. */
	memcpy(kvm->arch.model.fac->mask, S390_lowcore.stfle_fac_list,
	       S390_ARCH_FAC_LIST_SIZE_BYTE);
	for (i = 0; i < S390_ARCH_FAC_LIST_SIZE_U64; i++) {
		if (i < kvm_s390_fac_list_mask_size())
			kvm->arch.model.fac->mask[i] &= kvm_s390_fac_list_mask[i];
		else
			kvm->arch.model.fac->mask[i] = 0UL;
	}

	/* Populate the facility list initially. */
	memcpy(kvm->arch.model.fac->list, kvm->arch.model.fac->mask,
	       S390_ARCH_FAC_LIST_SIZE_BYTE);

	kvm_s390_get_cpu_id(&kvm->arch.model.cpu_id);
	kvm->arch.model.ibc = sclp.ibc & 0x0fff;

	if (kvm_s390_crypto_init(kvm) < 0)
		goto out_err;

	spin_lock_init(&kvm->arch.float_int.lock);
	for (i = 0; i < FIRQ_LIST_COUNT; i++)
		INIT_LIST_HEAD(&kvm->arch.float_int.lists[i]);
	init_waitqueue_head(&kvm->arch.ipte_wq);
	mutex_init(&kvm->arch.ipte_mutex);

	debug_register_view(kvm->arch.dbf, &debug_sprintf_view);
	VM_EVENT(kvm, 3, "vm created with type %lu", type);

	if (type & KVM_VM_S390_UCONTROL) {
		kvm->arch.gmap = NULL;
	} else {
		kvm->arch.gmap = gmap_alloc(current->mm, (1UL << 44) - 1);
		if (!kvm->arch.gmap)
			goto out_err;
		kvm->arch.gmap->private = kvm;
		kvm->arch.gmap->pfault_enabled = 0;
	}

	kvm->arch.css_support = 0;
	kvm->arch.use_irqchip = 0;
	kvm->arch.epoch = 0;

	spin_lock_init(&kvm->arch.start_stop_lock);
	KVM_EVENT(3, "vm 0x%p created by pid %u", kvm, current->pid);

	return 0;
out_err:
	kfree(kvm->arch.crypto.crycb);
	free_page((unsigned long)kvm->arch.model.fac);
	debug_unregister(kvm->arch.dbf);
	sca_dispose(kvm);
	KVM_EVENT(3, "creation of vm failed: %d", rc);
	return rc;
}

void kvm_arch_vcpu_destroy(struct kvm_vcpu *vcpu)
{
	VCPU_EVENT(vcpu, 3, "%s", "free cpu");
	trace_kvm_s390_destroy_vcpu(vcpu->vcpu_id);
	kvm_s390_clear_local_irqs(vcpu);
	kvm_clear_async_pf_completion_queue(vcpu);
	if (!kvm_is_ucontrol(vcpu->kvm))
		sca_del_vcpu(vcpu);
	smp_mb();

	if (kvm_is_ucontrol(vcpu->kvm))
		gmap_free(vcpu->arch.gmap);

	if (vcpu->kvm->arch.use_cmma)
		kvm_s390_vcpu_unsetup_cmma(vcpu);
	free_page((unsigned long)(vcpu->arch.sie_block));

	kvm_vcpu_uninit(vcpu);
	kmem_cache_free(kvm_vcpu_cache, vcpu);
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

void kvm_arch_destroy_vm(struct kvm *kvm)
{
	kvm_free_vcpus(kvm);
	free_page((unsigned long)kvm->arch.model.fac);
	sca_dispose(kvm);
	debug_unregister(kvm->arch.dbf);
	kfree(kvm->arch.crypto.crycb);
	if (!kvm_is_ucontrol(kvm))
		gmap_free(kvm->arch.gmap);
	kvm_s390_destroy_adapters(kvm);
	kvm_s390_clear_float_irqs(kvm);
	KVM_EVENT(3, "vm 0x%p destroyed", kvm);
}

/* Section: vcpu related */
static int __kvm_ucontrol_vcpu_init(struct kvm_vcpu *vcpu)
{
	vcpu->arch.gmap = gmap_alloc(current->mm, -1UL);
	if (!vcpu->arch.gmap)
		return -ENOMEM;
	vcpu->arch.gmap->private = vcpu->kvm;

	return 0;
}

static void sca_del_vcpu(struct kvm_vcpu *vcpu)
{
	read_lock(&vcpu->kvm->arch.sca_lock);
	if (vcpu->kvm->arch.use_esca) {
		struct esca_block *sca = vcpu->kvm->arch.sca;

		clear_bit_inv(vcpu->vcpu_id, (unsigned long *) sca->mcn);
		sca->cpu[vcpu->vcpu_id].sda = 0;
	} else {
		struct bsca_block *sca = vcpu->kvm->arch.sca;

		clear_bit_inv(vcpu->vcpu_id, (unsigned long *) &sca->mcn);
		sca->cpu[vcpu->vcpu_id].sda = 0;
	}
	read_unlock(&vcpu->kvm->arch.sca_lock);
}

static void sca_add_vcpu(struct kvm_vcpu *vcpu)
{
	read_lock(&vcpu->kvm->arch.sca_lock);
	if (vcpu->kvm->arch.use_esca) {
		struct esca_block *sca = vcpu->kvm->arch.sca;

		sca->cpu[vcpu->vcpu_id].sda = (__u64) vcpu->arch.sie_block;
		vcpu->arch.sie_block->scaoh = (__u32)(((__u64)sca) >> 32);
		vcpu->arch.sie_block->scaol = (__u32)(__u64)sca & ~0x3fU;
		vcpu->arch.sie_block->ecb2 |= 0x04U;
		set_bit_inv(vcpu->vcpu_id, (unsigned long *) sca->mcn);
	} else {
		struct bsca_block *sca = vcpu->kvm->arch.sca;

		sca->cpu[vcpu->vcpu_id].sda = (__u64) vcpu->arch.sie_block;
		vcpu->arch.sie_block->scaoh = (__u32)(((__u64)sca) >> 32);
		vcpu->arch.sie_block->scaol = (__u32)(__u64)sca;
		set_bit_inv(vcpu->vcpu_id, (unsigned long *) &sca->mcn);
	}
	read_unlock(&vcpu->kvm->arch.sca_lock);
}

/* Basic SCA to Extended SCA data copy routines */
static inline void sca_copy_entry(struct esca_entry *d, struct bsca_entry *s)
{
	d->sda = s->sda;
	d->sigp_ctrl.c = s->sigp_ctrl.c;
	d->sigp_ctrl.scn = s->sigp_ctrl.scn;
}

static void sca_copy_b_to_e(struct esca_block *d, struct bsca_block *s)
{
	int i;

	d->ipte_control = s->ipte_control;
	d->mcn[0] = s->mcn;
	for (i = 0; i < KVM_S390_BSCA_CPU_SLOTS; i++)
		sca_copy_entry(&d->cpu[i], &s->cpu[i]);
}

static int sca_switch_to_extended(struct kvm *kvm)
{
	struct bsca_block *old_sca = kvm->arch.sca;
	struct esca_block *new_sca;
	struct kvm_vcpu *vcpu;
	unsigned int vcpu_idx;
	u32 scaol, scaoh;

	new_sca = alloc_pages_exact(sizeof(*new_sca), GFP_KERNEL|__GFP_ZERO);
	if (!new_sca)
		return -ENOMEM;

	scaoh = (u32)((u64)(new_sca) >> 32);
	scaol = (u32)(u64)(new_sca) & ~0x3fU;

	kvm_s390_vcpu_block_all(kvm);
	write_lock(&kvm->arch.sca_lock);

	sca_copy_b_to_e(new_sca, old_sca);

	kvm_for_each_vcpu(vcpu_idx, vcpu, kvm) {
		vcpu->arch.sie_block->scaoh = scaoh;
		vcpu->arch.sie_block->scaol = scaol;
		vcpu->arch.sie_block->ecb2 |= 0x04U;
	}
	kvm->arch.sca = new_sca;
	kvm->arch.use_esca = 1;

	write_unlock(&kvm->arch.sca_lock);
	kvm_s390_vcpu_unblock_all(kvm);

	free_page((unsigned long)old_sca);

	VM_EVENT(kvm, 2, "Switched to ESCA (%p -> %p)", old_sca, kvm->arch.sca);
	return 0;
}

static int sca_can_add_vcpu(struct kvm *kvm, unsigned int id)
{
	int rc;

	if (id < KVM_S390_BSCA_CPU_SLOTS)
		return true;
	if (!sclp.has_esca)
		return false;

	mutex_lock(&kvm->lock);
	rc = kvm->arch.use_esca ? 0 : sca_switch_to_extended(kvm);
	mutex_unlock(&kvm->lock);

	return rc == 0 && id < KVM_S390_ESCA_CPU_SLOTS;
}

int kvm_arch_vcpu_init(struct kvm_vcpu *vcpu)
{
	vcpu->arch.pfault_token = KVM_S390_PFAULT_TOKEN_INVALID;
	kvm_clear_async_pf_completion_queue(vcpu);
	vcpu->run->kvm_valid_regs = KVM_SYNC_PREFIX |
				    KVM_SYNC_GPRS |
				    KVM_SYNC_ACRS |
				    KVM_SYNC_CRS |
				    KVM_SYNC_ARCH0 |
				    KVM_SYNC_PFAULT;
	if (test_kvm_facility(vcpu->kvm, 129))
		vcpu->run->kvm_valid_regs |= KVM_SYNC_VRS;

	if (kvm_is_ucontrol(vcpu->kvm))
		return __kvm_ucontrol_vcpu_init(vcpu);

	return 0;
}

/*
 * Backs up the current FP/VX register save area on a particular
 * destination.  Used to switch between different register save
 * areas.
 */
static inline void save_fpu_to(struct fpu *dst)
{
	dst->fpc = current->thread.fpu.fpc;
	dst->regs = current->thread.fpu.regs;
}

/*
 * Switches the FP/VX register save area from which to lazy
 * restore register contents.
 */
static inline void load_fpu_from(struct fpu *from)
{
	current->thread.fpu.fpc = from->fpc;
	current->thread.fpu.regs = from->regs;
}

void kvm_arch_vcpu_load(struct kvm_vcpu *vcpu, int cpu)
{
	/* Save host register state */
	save_fpu_regs();
	save_fpu_to(&vcpu->arch.host_fpregs);

	if (test_kvm_facility(vcpu->kvm, 129)) {
		current->thread.fpu.fpc = vcpu->run->s.regs.fpc;
		/*
		 * Use the register save area in the SIE-control block
		 * for register restore and save in kvm_arch_vcpu_put()
		 */
		current->thread.fpu.vxrs =
			(__vector128 *)&vcpu->run->s.regs.vrs;
	} else
		load_fpu_from(&vcpu->arch.guest_fpregs);

	if (test_fp_ctl(current->thread.fpu.fpc))
		/* User space provided an invalid FPC, let's clear it */
		current->thread.fpu.fpc = 0;

	save_access_regs(vcpu->arch.host_acrs);
	restore_access_regs(vcpu->run->s.regs.acrs);
	gmap_enable(vcpu->arch.gmap);
	atomic_or(CPUSTAT_RUNNING, &vcpu->arch.sie_block->cpuflags);
}

void kvm_arch_vcpu_put(struct kvm_vcpu *vcpu)
{
	atomic_andnot(CPUSTAT_RUNNING, &vcpu->arch.sie_block->cpuflags);
	gmap_disable(vcpu->arch.gmap);

	save_fpu_regs();

	if (test_kvm_facility(vcpu->kvm, 129))
		/*
		 * kvm_arch_vcpu_load() set up the register save area to
		 * the &vcpu->run->s.regs.vrs and, thus, the vector registers
		 * are already saved.  Only the floating-point control must be
		 * copied.
		 */
		vcpu->run->s.regs.fpc = current->thread.fpu.fpc;
	else
		save_fpu_to(&vcpu->arch.guest_fpregs);
	load_fpu_from(&vcpu->arch.host_fpregs);

	save_access_regs(vcpu->run->s.regs.acrs);
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
	vcpu->arch.sie_block->pp = 0;
	vcpu->arch.pfault_token = KVM_S390_PFAULT_TOKEN_INVALID;
	kvm_clear_async_pf_completion_queue(vcpu);
	if (!kvm_s390_user_cpu_state_ctrl(vcpu->kvm))
		kvm_s390_vcpu_stop(vcpu);
	kvm_s390_clear_local_irqs(vcpu);
}

void kvm_arch_vcpu_postcreate(struct kvm_vcpu *vcpu)
{
	mutex_lock(&vcpu->kvm->lock);
	preempt_disable();
	vcpu->arch.sie_block->epoch = vcpu->kvm->arch.epoch;
	preempt_enable();
	mutex_unlock(&vcpu->kvm->lock);
	if (!kvm_is_ucontrol(vcpu->kvm)) {
		vcpu->arch.gmap = vcpu->kvm->arch.gmap;
		sca_add_vcpu(vcpu);
	}

}

static void kvm_s390_vcpu_crypto_setup(struct kvm_vcpu *vcpu)
{
	if (!test_kvm_facility(vcpu->kvm, 76))
		return;

	vcpu->arch.sie_block->ecb3 &= ~(ECB3_AES | ECB3_DEA);

	if (vcpu->kvm->arch.crypto.aes_kw)
		vcpu->arch.sie_block->ecb3 |= ECB3_AES;
	if (vcpu->kvm->arch.crypto.dea_kw)
		vcpu->arch.sie_block->ecb3 |= ECB3_DEA;

	vcpu->arch.sie_block->crycbd = vcpu->kvm->arch.crypto.crycbd;
}

void kvm_s390_vcpu_unsetup_cmma(struct kvm_vcpu *vcpu)
{
	free_page(vcpu->arch.sie_block->cbrlo);
	vcpu->arch.sie_block->cbrlo = 0;
}

int kvm_s390_vcpu_setup_cmma(struct kvm_vcpu *vcpu)
{
	vcpu->arch.sie_block->cbrlo = get_zeroed_page(GFP_KERNEL);
	if (!vcpu->arch.sie_block->cbrlo)
		return -ENOMEM;

	vcpu->arch.sie_block->ecb2 |= 0x80;
	vcpu->arch.sie_block->ecb2 &= ~0x08;
	return 0;
}

static void kvm_s390_vcpu_setup_model(struct kvm_vcpu *vcpu)
{
	struct kvm_s390_cpu_model *model = &vcpu->kvm->arch.model;

	vcpu->arch.cpu_id = model->cpu_id;
	vcpu->arch.sie_block->ibc = model->ibc;
	vcpu->arch.sie_block->fac = (int) (long) model->fac->list;
}

int kvm_arch_vcpu_setup(struct kvm_vcpu *vcpu)
{
	int rc = 0;

	atomic_set(&vcpu->arch.sie_block->cpuflags, CPUSTAT_ZARCH |
						    CPUSTAT_SM |
						    CPUSTAT_STOPPED);

	if (test_kvm_facility(vcpu->kvm, 78))
		atomic_or(CPUSTAT_GED2, &vcpu->arch.sie_block->cpuflags);
	else if (test_kvm_facility(vcpu->kvm, 8))
		atomic_or(CPUSTAT_GED, &vcpu->arch.sie_block->cpuflags);

	kvm_s390_vcpu_setup_model(vcpu);

	vcpu->arch.sie_block->ecb   = 6;
	if (test_kvm_facility(vcpu->kvm, 50) && test_kvm_facility(vcpu->kvm, 73))
		vcpu->arch.sie_block->ecb |= 0x10;

	vcpu->arch.sie_block->ecb2  = 8;
	vcpu->arch.sie_block->eca   = 0xC1002000U;
	if (sclp.has_siif)
		vcpu->arch.sie_block->eca |= 1;
	if (sclp.has_sigpif)
		vcpu->arch.sie_block->eca |= 0x10000000U;
	if (test_kvm_facility(vcpu->kvm, 129)) {
		vcpu->arch.sie_block->eca |= 0x00020000;
		vcpu->arch.sie_block->ecd |= 0x20000000;
	}
	vcpu->arch.sie_block->ictl |= ICTL_ISKE | ICTL_SSKE | ICTL_RRBE;

	if (vcpu->kvm->arch.use_cmma) {
		rc = kvm_s390_vcpu_setup_cmma(vcpu);
		if (rc)
			return rc;
	}
	hrtimer_init(&vcpu->arch.ckc_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	vcpu->arch.ckc_timer.function = kvm_s390_idle_wakeup;

	kvm_s390_vcpu_crypto_setup(vcpu);

	return rc;
}

struct kvm_vcpu *kvm_arch_vcpu_create(struct kvm *kvm,
				      unsigned int id)
{
	struct kvm_vcpu *vcpu;
	struct sie_page *sie_page;
	int rc = -EINVAL;

	if (!kvm_is_ucontrol(kvm) && !sca_can_add_vcpu(kvm, id))
		goto out;

	rc = -ENOMEM;

	vcpu = kmem_cache_zalloc(kvm_vcpu_cache, GFP_KERNEL);
	if (!vcpu)
		goto out;

	sie_page = (struct sie_page *) get_zeroed_page(GFP_KERNEL);
	if (!sie_page)
		goto out_free_cpu;

	vcpu->arch.sie_block = &sie_page->sie_block;
	vcpu->arch.sie_block->itdba = (unsigned long) &sie_page->itdb;

	vcpu->arch.sie_block->icpua = id;
	spin_lock_init(&vcpu->arch.local_int.lock);
	vcpu->arch.local_int.float_int = &kvm->arch.float_int;
	vcpu->arch.local_int.wq = &vcpu->wq;
	vcpu->arch.local_int.cpuflags = &vcpu->arch.sie_block->cpuflags;

	/*
	 * Allocate a save area for floating-point registers.  If the vector
	 * extension is available, register contents are saved in the SIE
	 * control block.  The allocated save area is still required in
	 * particular places, for example, in kvm_s390_vcpu_store_status().
	 */
	vcpu->arch.guest_fpregs.fprs = kzalloc(sizeof(freg_t) * __NUM_FPRS,
					       GFP_KERNEL);
	if (!vcpu->arch.guest_fpregs.fprs)
		goto out_free_sie_block;

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
	kmem_cache_free(kvm_vcpu_cache, vcpu);
out:
	return ERR_PTR(rc);
}

int kvm_arch_vcpu_runnable(struct kvm_vcpu *vcpu)
{
	return kvm_s390_vcpu_has_irq(vcpu, 0);
}

void kvm_s390_vcpu_block(struct kvm_vcpu *vcpu)
{
	atomic_or(PROG_BLOCK_SIE, &vcpu->arch.sie_block->prog20);
	exit_sie(vcpu);
}

void kvm_s390_vcpu_unblock(struct kvm_vcpu *vcpu)
{
	atomic_andnot(PROG_BLOCK_SIE, &vcpu->arch.sie_block->prog20);
}

static void kvm_s390_vcpu_request(struct kvm_vcpu *vcpu)
{
	atomic_or(PROG_REQUEST, &vcpu->arch.sie_block->prog20);
	exit_sie(vcpu);
}

static void kvm_s390_vcpu_request_handled(struct kvm_vcpu *vcpu)
{
	atomic_andnot(PROG_REQUEST, &vcpu->arch.sie_block->prog20);
}

/*
 * Kick a guest cpu out of SIE and wait until SIE is not running.
 * If the CPU is not running (e.g. waiting as idle) the function will
 * return immediately. */
void exit_sie(struct kvm_vcpu *vcpu)
{
	atomic_or(CPUSTAT_STOP_INT, &vcpu->arch.sie_block->cpuflags);
	while (vcpu->arch.sie_block->prog0c & PROG_IN_SIE)
		cpu_relax();
}

/* Kick a guest cpu out of SIE to process a request synchronously */
void kvm_s390_sync_request(int req, struct kvm_vcpu *vcpu)
{
	kvm_make_request(req, vcpu);
	kvm_s390_vcpu_request(vcpu);
}

static void kvm_gmap_notifier(struct gmap *gmap, unsigned long address)
{
	int i;
	struct kvm *kvm = gmap->private;
	struct kvm_vcpu *vcpu;

	kvm_for_each_vcpu(i, vcpu, kvm) {
		/* match against both prefix pages */
		if (kvm_s390_get_prefix(vcpu) == (address & ~0x1000UL)) {
			VCPU_EVENT(vcpu, 2, "gmap notifier for %lx", address);
			kvm_s390_sync_request(KVM_REQ_MMU_RELOAD, vcpu);
		}
	}
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
	case KVM_REG_S390_PFTOKEN:
		r = put_user(vcpu->arch.pfault_token,
			     (u64 __user *)reg->addr);
		break;
	case KVM_REG_S390_PFCOMPARE:
		r = put_user(vcpu->arch.pfault_compare,
			     (u64 __user *)reg->addr);
		break;
	case KVM_REG_S390_PFSELECT:
		r = put_user(vcpu->arch.pfault_select,
			     (u64 __user *)reg->addr);
		break;
	case KVM_REG_S390_PP:
		r = put_user(vcpu->arch.sie_block->pp,
			     (u64 __user *)reg->addr);
		break;
	case KVM_REG_S390_GBEA:
		r = put_user(vcpu->arch.sie_block->gbea,
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
	case KVM_REG_S390_PFTOKEN:
		r = get_user(vcpu->arch.pfault_token,
			     (u64 __user *)reg->addr);
		if (vcpu->arch.pfault_token == KVM_S390_PFAULT_TOKEN_INVALID)
			kvm_clear_async_pf_completion_queue(vcpu);
		break;
	case KVM_REG_S390_PFCOMPARE:
		r = get_user(vcpu->arch.pfault_compare,
			     (u64 __user *)reg->addr);
		break;
	case KVM_REG_S390_PFSELECT:
		r = get_user(vcpu->arch.pfault_select,
			     (u64 __user *)reg->addr);
		break;
	case KVM_REG_S390_PP:
		r = get_user(vcpu->arch.sie_block->pp,
			     (u64 __user *)reg->addr);
		break;
	case KVM_REG_S390_GBEA:
		r = get_user(vcpu->arch.sie_block->gbea,
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
	if (test_fp_ctl(fpu->fpc))
		return -EINVAL;
	memcpy(vcpu->arch.guest_fpregs.fprs, &fpu->fprs, sizeof(fpu->fprs));
	vcpu->arch.guest_fpregs.fpc = fpu->fpc;
	save_fpu_regs();
	load_fpu_from(&vcpu->arch.guest_fpregs);
	return 0;
}

int kvm_arch_vcpu_ioctl_get_fpu(struct kvm_vcpu *vcpu, struct kvm_fpu *fpu)
{
	memcpy(&fpu->fprs, vcpu->arch.guest_fpregs.fprs, sizeof(fpu->fprs));
	fpu->fpc = vcpu->arch.guest_fpregs.fpc;
	return 0;
}

static int kvm_arch_vcpu_ioctl_set_initial_psw(struct kvm_vcpu *vcpu, psw_t psw)
{
	int rc = 0;

	if (!is_vcpu_stopped(vcpu))
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

#define VALID_GUESTDBG_FLAGS (KVM_GUESTDBG_SINGLESTEP | \
			      KVM_GUESTDBG_USE_HW_BP | \
			      KVM_GUESTDBG_ENABLE)

int kvm_arch_vcpu_ioctl_set_guest_debug(struct kvm_vcpu *vcpu,
					struct kvm_guest_debug *dbg)
{
	int rc = 0;

	vcpu->guest_debug = 0;
	kvm_s390_clear_bp_data(vcpu);

	if (dbg->control & ~VALID_GUESTDBG_FLAGS)
		return -EINVAL;

	if (dbg->control & KVM_GUESTDBG_ENABLE) {
		vcpu->guest_debug = dbg->control;
		/* enforce guest PER */
		atomic_or(CPUSTAT_P, &vcpu->arch.sie_block->cpuflags);

		if (dbg->control & KVM_GUESTDBG_USE_HW_BP)
			rc = kvm_s390_import_bp_data(vcpu, dbg);
	} else {
		atomic_andnot(CPUSTAT_P, &vcpu->arch.sie_block->cpuflags);
		vcpu->arch.guestdbg.last_bp = 0;
	}

	if (rc) {
		vcpu->guest_debug = 0;
		kvm_s390_clear_bp_data(vcpu);
		atomic_andnot(CPUSTAT_P, &vcpu->arch.sie_block->cpuflags);
	}

	return rc;
}

int kvm_arch_vcpu_ioctl_get_mpstate(struct kvm_vcpu *vcpu,
				    struct kvm_mp_state *mp_state)
{
	/* CHECK_STOP and LOAD are not supported yet */
	return is_vcpu_stopped(vcpu) ? KVM_MP_STATE_STOPPED :
				       KVM_MP_STATE_OPERATING;
}

int kvm_arch_vcpu_ioctl_set_mpstate(struct kvm_vcpu *vcpu,
				    struct kvm_mp_state *mp_state)
{
	int rc = 0;

	/* user space knows about this interface - let it control the state */
	vcpu->kvm->arch.user_cpu_state_ctrl = 1;

	switch (mp_state->mp_state) {
	case KVM_MP_STATE_STOPPED:
		kvm_s390_vcpu_stop(vcpu);
		break;
	case KVM_MP_STATE_OPERATING:
		kvm_s390_vcpu_start(vcpu);
		break;
	case KVM_MP_STATE_LOAD:
	case KVM_MP_STATE_CHECK_STOP:
		/* fall through - CHECK_STOP and LOAD are not supported yet */
	default:
		rc = -ENXIO;
	}

	return rc;
}

static bool ibs_enabled(struct kvm_vcpu *vcpu)
{
	return atomic_read(&vcpu->arch.sie_block->cpuflags) & CPUSTAT_IBS;
}

static int kvm_s390_handle_requests(struct kvm_vcpu *vcpu)
{
retry:
	kvm_s390_vcpu_request_handled(vcpu);
	if (!vcpu->requests)
		return 0;
	/*
	 * We use MMU_RELOAD just to re-arm the ipte notifier for the
	 * guest prefix page. gmap_ipte_notify will wait on the ptl lock.
	 * This ensures that the ipte instruction for this request has
	 * already finished. We might race against a second unmapper that
	 * wants to set the blocking bit. Lets just retry the request loop.
	 */
	if (kvm_check_request(KVM_REQ_MMU_RELOAD, vcpu)) {
		int rc;
		rc = gmap_ipte_notify(vcpu->arch.gmap,
				      kvm_s390_get_prefix(vcpu),
				      PAGE_SIZE * 2);
		if (rc)
			return rc;
		goto retry;
	}

	if (kvm_check_request(KVM_REQ_TLB_FLUSH, vcpu)) {
		vcpu->arch.sie_block->ihcpu = 0xffff;
		goto retry;
	}

	if (kvm_check_request(KVM_REQ_ENABLE_IBS, vcpu)) {
		if (!ibs_enabled(vcpu)) {
			trace_kvm_s390_enable_disable_ibs(vcpu->vcpu_id, 1);
			atomic_or(CPUSTAT_IBS,
					&vcpu->arch.sie_block->cpuflags);
		}
		goto retry;
	}

	if (kvm_check_request(KVM_REQ_DISABLE_IBS, vcpu)) {
		if (ibs_enabled(vcpu)) {
			trace_kvm_s390_enable_disable_ibs(vcpu->vcpu_id, 0);
			atomic_andnot(CPUSTAT_IBS,
					  &vcpu->arch.sie_block->cpuflags);
		}
		goto retry;
	}

	/* nothing to do, just clear the request */
	clear_bit(KVM_REQ_UNHALT, &vcpu->requests);

	return 0;
}

void kvm_s390_set_tod_clock(struct kvm *kvm, u64 tod)
{
	struct kvm_vcpu *vcpu;
	int i;

	mutex_lock(&kvm->lock);
	preempt_disable();
	kvm->arch.epoch = tod - get_tod_clock();
	kvm_s390_vcpu_block_all(kvm);
	kvm_for_each_vcpu(i, vcpu, kvm)
		vcpu->arch.sie_block->epoch = kvm->arch.epoch;
	kvm_s390_vcpu_unblock_all(kvm);
	preempt_enable();
	mutex_unlock(&kvm->lock);
}

/**
 * kvm_arch_fault_in_page - fault-in guest page if necessary
 * @vcpu: The corresponding virtual cpu
 * @gpa: Guest physical address
 * @writable: Whether the page should be writable or not
 *
 * Make sure that a guest page has been faulted-in on the host.
 *
 * Return: Zero on success, negative error code otherwise.
 */
long kvm_arch_fault_in_page(struct kvm_vcpu *vcpu, gpa_t gpa, int writable)
{
	return gmap_fault(vcpu->arch.gmap, gpa,
			  writable ? FAULT_FLAG_WRITE : 0);
}

static void __kvm_inject_pfault_token(struct kvm_vcpu *vcpu, bool start_token,
				      unsigned long token)
{
	struct kvm_s390_interrupt inti;
	struct kvm_s390_irq irq;

	if (start_token) {
		irq.u.ext.ext_params2 = token;
		irq.type = KVM_S390_INT_PFAULT_INIT;
		WARN_ON_ONCE(kvm_s390_inject_vcpu(vcpu, &irq));
	} else {
		inti.type = KVM_S390_INT_PFAULT_DONE;
		inti.parm64 = token;
		WARN_ON_ONCE(kvm_s390_inject_vm(vcpu->kvm, &inti));
	}
}

void kvm_arch_async_page_not_present(struct kvm_vcpu *vcpu,
				     struct kvm_async_pf *work)
{
	trace_kvm_s390_pfault_init(vcpu, work->arch.pfault_token);
	__kvm_inject_pfault_token(vcpu, true, work->arch.pfault_token);
}

void kvm_arch_async_page_present(struct kvm_vcpu *vcpu,
				 struct kvm_async_pf *work)
{
	trace_kvm_s390_pfault_done(vcpu, work->arch.pfault_token);
	__kvm_inject_pfault_token(vcpu, false, work->arch.pfault_token);
}

void kvm_arch_async_page_ready(struct kvm_vcpu *vcpu,
			       struct kvm_async_pf *work)
{
	/* s390 will always inject the page directly */
}

bool kvm_arch_can_inject_async_page_present(struct kvm_vcpu *vcpu)
{
	/*
	 * s390 will always inject the page directly,
	 * but we still want check_async_completion to cleanup
	 */
	return true;
}

static int kvm_arch_setup_async_pf(struct kvm_vcpu *vcpu)
{
	hva_t hva;
	struct kvm_arch_async_pf arch;
	int rc;

	if (vcpu->arch.pfault_token == KVM_S390_PFAULT_TOKEN_INVALID)
		return 0;
	if ((vcpu->arch.sie_block->gpsw.mask & vcpu->arch.pfault_select) !=
	    vcpu->arch.pfault_compare)
		return 0;
	if (psw_extint_disabled(vcpu))
		return 0;
	if (kvm_s390_vcpu_has_irq(vcpu, 0))
		return 0;
	if (!(vcpu->arch.sie_block->gcr[0] & 0x200ul))
		return 0;
	if (!vcpu->arch.gmap->pfault_enabled)
		return 0;

	hva = gfn_to_hva(vcpu->kvm, gpa_to_gfn(current->thread.gmap_addr));
	hva += current->thread.gmap_addr & ~PAGE_MASK;
	if (read_guest_real(vcpu, vcpu->arch.pfault_token, &arch.pfault_token, 8))
		return 0;

	rc = kvm_setup_async_pf(vcpu, current->thread.gmap_addr, hva, &arch);
	return rc;
}

static int vcpu_pre_run(struct kvm_vcpu *vcpu)
{
	int rc, cpuflags;

	/*
	 * On s390 notifications for arriving pages will be delivered directly
	 * to the guest but the house keeping for completed pfaults is
	 * handled outside the worker.
	 */
	kvm_check_async_pf_completion(vcpu);

	memcpy(&vcpu->arch.sie_block->gg14, &vcpu->run->s.regs.gprs[14], 16);

	if (need_resched())
		schedule();

	if (test_cpu_flag(CIF_MCCK_PENDING))
		s390_handle_mcck();

	if (!kvm_is_ucontrol(vcpu->kvm)) {
		rc = kvm_s390_deliver_pending_interrupts(vcpu);
		if (rc)
			return rc;
	}

	rc = kvm_s390_handle_requests(vcpu);
	if (rc)
		return rc;

	if (guestdbg_enabled(vcpu)) {
		kvm_s390_backup_guest_per_regs(vcpu);
		kvm_s390_patch_guest_per_regs(vcpu);
	}

	vcpu->arch.sie_block->icptcode = 0;
	cpuflags = atomic_read(&vcpu->arch.sie_block->cpuflags);
	VCPU_EVENT(vcpu, 6, "entering sie flags %x", cpuflags);
	trace_kvm_s390_sie_enter(vcpu, cpuflags);

	return 0;
}

static int vcpu_post_run_fault_in_sie(struct kvm_vcpu *vcpu)
{
	psw_t *psw = &vcpu->arch.sie_block->gpsw;
	u8 opcode;
	int rc;

	VCPU_EVENT(vcpu, 3, "%s", "fault in sie instruction");
	trace_kvm_s390_sie_fault(vcpu);

	/*
	 * We want to inject an addressing exception, which is defined as a
	 * suppressing or terminating exception. However, since we came here
	 * by a DAT access exception, the PSW still points to the faulting
	 * instruction since DAT exceptions are nullifying. So we've got
	 * to look up the current opcode to get the length of the instruction
	 * to be able to forward the PSW.
	 */
	rc = read_guest(vcpu, psw->addr, 0, &opcode, 1);
	if (rc)
		return kvm_s390_inject_prog_cond(vcpu, rc);
	psw->addr = __rewind_psw(*psw, -insn_length(opcode));

	return kvm_s390_inject_program_int(vcpu, PGM_ADDRESSING);
}

static int vcpu_post_run(struct kvm_vcpu *vcpu, int exit_reason)
{
	VCPU_EVENT(vcpu, 6, "exit sie icptcode %d",
		   vcpu->arch.sie_block->icptcode);
	trace_kvm_s390_sie_exit(vcpu, vcpu->arch.sie_block->icptcode);

	if (guestdbg_enabled(vcpu))
		kvm_s390_restore_guest_per_regs(vcpu);

	memcpy(&vcpu->run->s.regs.gprs[14], &vcpu->arch.sie_block->gg14, 16);

	if (vcpu->arch.sie_block->icptcode > 0) {
		int rc = kvm_handle_sie_intercept(vcpu);

		if (rc != -EOPNOTSUPP)
			return rc;
		vcpu->run->exit_reason = KVM_EXIT_S390_SIEIC;
		vcpu->run->s390_sieic.icptcode = vcpu->arch.sie_block->icptcode;
		vcpu->run->s390_sieic.ipa = vcpu->arch.sie_block->ipa;
		vcpu->run->s390_sieic.ipb = vcpu->arch.sie_block->ipb;
		return -EREMOTE;
	} else if (exit_reason != -EFAULT) {
		vcpu->stat.exit_null++;
		return 0;
	} else if (kvm_is_ucontrol(vcpu->kvm)) {
		vcpu->run->exit_reason = KVM_EXIT_S390_UCONTROL;
		vcpu->run->s390_ucontrol.trans_exc_code =
						current->thread.gmap_addr;
		vcpu->run->s390_ucontrol.pgm_code = 0x10;
		return -EREMOTE;
	} else if (current->thread.gmap_pfault) {
		trace_kvm_s390_major_guest_pfault(vcpu);
		current->thread.gmap_pfault = 0;
		if (kvm_arch_setup_async_pf(vcpu))
			return 0;
		return kvm_arch_fault_in_page(vcpu, current->thread.gmap_addr, 1);
	}
	return vcpu_post_run_fault_in_sie(vcpu);
}

static int __vcpu_run(struct kvm_vcpu *vcpu)
{
	int rc, exit_reason;

	/*
	 * We try to hold kvm->srcu during most of vcpu_run (except when run-
	 * ning the guest), so that memslots (and other stuff) are protected
	 */
	vcpu->srcu_idx = srcu_read_lock(&vcpu->kvm->srcu);

	do {
		rc = vcpu_pre_run(vcpu);
		if (rc)
			break;

		srcu_read_unlock(&vcpu->kvm->srcu, vcpu->srcu_idx);
		/*
		 * As PF_VCPU will be used in fault handler, between
		 * guest_enter and guest_exit should be no uaccess.
		 */
		local_irq_disable();
		__kvm_guest_enter();
		local_irq_enable();
		exit_reason = sie64a(vcpu->arch.sie_block,
				     vcpu->run->s.regs.gprs);
		local_irq_disable();
		__kvm_guest_exit();
		local_irq_enable();
		vcpu->srcu_idx = srcu_read_lock(&vcpu->kvm->srcu);

		rc = vcpu_post_run(vcpu, exit_reason);
	} while (!signal_pending(current) && !guestdbg_exit_pending(vcpu) && !rc);

	srcu_read_unlock(&vcpu->kvm->srcu, vcpu->srcu_idx);
	return rc;
}

static void sync_regs(struct kvm_vcpu *vcpu, struct kvm_run *kvm_run)
{
	vcpu->arch.sie_block->gpsw.mask = kvm_run->psw_mask;
	vcpu->arch.sie_block->gpsw.addr = kvm_run->psw_addr;
	if (kvm_run->kvm_dirty_regs & KVM_SYNC_PREFIX)
		kvm_s390_set_prefix(vcpu, kvm_run->s.regs.prefix);
	if (kvm_run->kvm_dirty_regs & KVM_SYNC_CRS) {
		memcpy(&vcpu->arch.sie_block->gcr, &kvm_run->s.regs.crs, 128);
		/* some control register changes require a tlb flush */
		kvm_make_request(KVM_REQ_TLB_FLUSH, vcpu);
	}
	if (kvm_run->kvm_dirty_regs & KVM_SYNC_ARCH0) {
		vcpu->arch.sie_block->cputm = kvm_run->s.regs.cputm;
		vcpu->arch.sie_block->ckc = kvm_run->s.regs.ckc;
		vcpu->arch.sie_block->todpr = kvm_run->s.regs.todpr;
		vcpu->arch.sie_block->pp = kvm_run->s.regs.pp;
		vcpu->arch.sie_block->gbea = kvm_run->s.regs.gbea;
	}
	if (kvm_run->kvm_dirty_regs & KVM_SYNC_PFAULT) {
		vcpu->arch.pfault_token = kvm_run->s.regs.pft;
		vcpu->arch.pfault_select = kvm_run->s.regs.pfs;
		vcpu->arch.pfault_compare = kvm_run->s.regs.pfc;
		if (vcpu->arch.pfault_token == KVM_S390_PFAULT_TOKEN_INVALID)
			kvm_clear_async_pf_completion_queue(vcpu);
	}
	kvm_run->kvm_dirty_regs = 0;
}

static void store_regs(struct kvm_vcpu *vcpu, struct kvm_run *kvm_run)
{
	kvm_run->psw_mask = vcpu->arch.sie_block->gpsw.mask;
	kvm_run->psw_addr = vcpu->arch.sie_block->gpsw.addr;
	kvm_run->s.regs.prefix = kvm_s390_get_prefix(vcpu);
	memcpy(&kvm_run->s.regs.crs, &vcpu->arch.sie_block->gcr, 128);
	kvm_run->s.regs.cputm = vcpu->arch.sie_block->cputm;
	kvm_run->s.regs.ckc = vcpu->arch.sie_block->ckc;
	kvm_run->s.regs.todpr = vcpu->arch.sie_block->todpr;
	kvm_run->s.regs.pp = vcpu->arch.sie_block->pp;
	kvm_run->s.regs.gbea = vcpu->arch.sie_block->gbea;
	kvm_run->s.regs.pft = vcpu->arch.pfault_token;
	kvm_run->s.regs.pfs = vcpu->arch.pfault_select;
	kvm_run->s.regs.pfc = vcpu->arch.pfault_compare;
}

int kvm_arch_vcpu_ioctl_run(struct kvm_vcpu *vcpu, struct kvm_run *kvm_run)
{
	int rc;
	sigset_t sigsaved;

	if (guestdbg_exit_pending(vcpu)) {
		kvm_s390_prepare_debug_exit(vcpu);
		return 0;
	}

	if (vcpu->sigset_active)
		sigprocmask(SIG_SETMASK, &vcpu->sigset, &sigsaved);

	if (!kvm_s390_user_cpu_state_ctrl(vcpu->kvm)) {
		kvm_s390_vcpu_start(vcpu);
	} else if (is_vcpu_stopped(vcpu)) {
		pr_err_ratelimited("can't run stopped vcpu %d\n",
				   vcpu->vcpu_id);
		return -EINVAL;
	}

	sync_regs(vcpu, kvm_run);

	might_fault();
	rc = __vcpu_run(vcpu);

	if (signal_pending(current) && !rc) {
		kvm_run->exit_reason = KVM_EXIT_INTR;
		rc = -EINTR;
	}

	if (guestdbg_exit_pending(vcpu) && !rc)  {
		kvm_s390_prepare_debug_exit(vcpu);
		rc = 0;
	}

	if (rc == -EREMOTE) {
		/* userspace support is needed, kvm_run has been prepared */
		rc = 0;
	}

	store_regs(vcpu, kvm_run);

	if (vcpu->sigset_active)
		sigprocmask(SIG_SETMASK, &sigsaved, NULL);

	vcpu->stat.exit_userspace++;
	return rc;
}

/*
 * store status at address
 * we use have two special cases:
 * KVM_S390_STORE_STATUS_NOADDR: -> 0x1200 on 64 bit
 * KVM_S390_STORE_STATUS_PREFIXED: -> prefix
 */
int kvm_s390_store_status_unloaded(struct kvm_vcpu *vcpu, unsigned long gpa)
{
	unsigned char archmode = 1;
	unsigned int px;
	u64 clkcomp;
	int rc;

	if (gpa == KVM_S390_STORE_STATUS_NOADDR) {
		if (write_guest_abs(vcpu, 163, &archmode, 1))
			return -EFAULT;
		gpa = SAVE_AREA_BASE;
	} else if (gpa == KVM_S390_STORE_STATUS_PREFIXED) {
		if (write_guest_real(vcpu, 163, &archmode, 1))
			return -EFAULT;
		gpa = kvm_s390_real_to_abs(vcpu, SAVE_AREA_BASE);
	}
	rc = write_guest_abs(vcpu, gpa + offsetof(struct save_area, fp_regs),
			     vcpu->arch.guest_fpregs.fprs, 128);
	rc |= write_guest_abs(vcpu, gpa + offsetof(struct save_area, gp_regs),
			      vcpu->run->s.regs.gprs, 128);
	rc |= write_guest_abs(vcpu, gpa + offsetof(struct save_area, psw),
			      &vcpu->arch.sie_block->gpsw, 16);
	px = kvm_s390_get_prefix(vcpu);
	rc |= write_guest_abs(vcpu, gpa + offsetof(struct save_area, pref_reg),
			      &px, 4);
	rc |= write_guest_abs(vcpu,
			      gpa + offsetof(struct save_area, fp_ctrl_reg),
			      &vcpu->arch.guest_fpregs.fpc, 4);
	rc |= write_guest_abs(vcpu, gpa + offsetof(struct save_area, tod_reg),
			      &vcpu->arch.sie_block->todpr, 4);
	rc |= write_guest_abs(vcpu, gpa + offsetof(struct save_area, timer),
			      &vcpu->arch.sie_block->cputm, 8);
	clkcomp = vcpu->arch.sie_block->ckc >> 8;
	rc |= write_guest_abs(vcpu, gpa + offsetof(struct save_area, clk_cmp),
			      &clkcomp, 8);
	rc |= write_guest_abs(vcpu, gpa + offsetof(struct save_area, acc_regs),
			      &vcpu->run->s.regs.acrs, 64);
	rc |= write_guest_abs(vcpu, gpa + offsetof(struct save_area, ctrl_regs),
			      &vcpu->arch.sie_block->gcr, 128);
	return rc ? -EFAULT : 0;
}

int kvm_s390_vcpu_store_status(struct kvm_vcpu *vcpu, unsigned long addr)
{
	/*
	 * The guest FPRS and ACRS are in the host FPRS/ACRS due to the lazy
	 * copying in vcpu load/put. Lets update our copies before we save
	 * it into the save area
	 */
	save_fpu_regs();
	if (test_kvm_facility(vcpu->kvm, 129)) {
		/*
		 * If the vector extension is available, the vector registers
		 * which overlaps with floating-point registers are saved in
		 * the SIE-control block.  Hence, extract the floating-point
		 * registers and the FPC value and store them in the
		 * guest_fpregs structure.
		 */
		vcpu->arch.guest_fpregs.fpc = current->thread.fpu.fpc;
		convert_vx_to_fp(vcpu->arch.guest_fpregs.fprs,
				 current->thread.fpu.vxrs);
	} else
		save_fpu_to(&vcpu->arch.guest_fpregs);
	save_access_regs(vcpu->run->s.regs.acrs);

	return kvm_s390_store_status_unloaded(vcpu, addr);
}

/*
 * store additional status at address
 */
int kvm_s390_store_adtl_status_unloaded(struct kvm_vcpu *vcpu,
					unsigned long gpa)
{
	/* Only bits 0-53 are used for address formation */
	if (!(gpa & ~0x3ff))
		return 0;

	return write_guest_abs(vcpu, gpa & ~0x3ff,
			       (void *)&vcpu->run->s.regs.vrs, 512);
}

int kvm_s390_vcpu_store_adtl_status(struct kvm_vcpu *vcpu, unsigned long addr)
{
	if (!test_kvm_facility(vcpu->kvm, 129))
		return 0;

	/*
	 * The guest VXRS are in the host VXRs due to the lazy
	 * copying in vcpu load/put. We can simply call save_fpu_regs()
	 * to save the current register state because we are in the
	 * middle of a load/put cycle.
	 *
	 * Let's update our copies before we save it into the save area.
	 */
	save_fpu_regs();

	return kvm_s390_store_adtl_status_unloaded(vcpu, addr);
}

static void __disable_ibs_on_vcpu(struct kvm_vcpu *vcpu)
{
	kvm_check_request(KVM_REQ_ENABLE_IBS, vcpu);
	kvm_s390_sync_request(KVM_REQ_DISABLE_IBS, vcpu);
}

static void __disable_ibs_on_all_vcpus(struct kvm *kvm)
{
	unsigned int i;
	struct kvm_vcpu *vcpu;

	kvm_for_each_vcpu(i, vcpu, kvm) {
		__disable_ibs_on_vcpu(vcpu);
	}
}

static void __enable_ibs_on_vcpu(struct kvm_vcpu *vcpu)
{
	kvm_check_request(KVM_REQ_DISABLE_IBS, vcpu);
	kvm_s390_sync_request(KVM_REQ_ENABLE_IBS, vcpu);
}

void kvm_s390_vcpu_start(struct kvm_vcpu *vcpu)
{
	int i, online_vcpus, started_vcpus = 0;

	if (!is_vcpu_stopped(vcpu))
		return;

	trace_kvm_s390_vcpu_start_stop(vcpu->vcpu_id, 1);
	/* Only one cpu at a time may enter/leave the STOPPED state. */
	spin_lock(&vcpu->kvm->arch.start_stop_lock);
	online_vcpus = atomic_read(&vcpu->kvm->online_vcpus);

	for (i = 0; i < online_vcpus; i++) {
		if (!is_vcpu_stopped(vcpu->kvm->vcpus[i]))
			started_vcpus++;
	}

	if (started_vcpus == 0) {
		/* we're the only active VCPU -> speed it up */
		__enable_ibs_on_vcpu(vcpu);
	} else if (started_vcpus == 1) {
		/*
		 * As we are starting a second VCPU, we have to disable
		 * the IBS facility on all VCPUs to remove potentially
		 * oustanding ENABLE requests.
		 */
		__disable_ibs_on_all_vcpus(vcpu->kvm);
	}

	atomic_andnot(CPUSTAT_STOPPED, &vcpu->arch.sie_block->cpuflags);
	/*
	 * Another VCPU might have used IBS while we were offline.
	 * Let's play safe and flush the VCPU at startup.
	 */
	kvm_make_request(KVM_REQ_TLB_FLUSH, vcpu);
	spin_unlock(&vcpu->kvm->arch.start_stop_lock);
	return;
}

void kvm_s390_vcpu_stop(struct kvm_vcpu *vcpu)
{
	int i, online_vcpus, started_vcpus = 0;
	struct kvm_vcpu *started_vcpu = NULL;

	if (is_vcpu_stopped(vcpu))
		return;

	trace_kvm_s390_vcpu_start_stop(vcpu->vcpu_id, 0);
	/* Only one cpu at a time may enter/leave the STOPPED state. */
	spin_lock(&vcpu->kvm->arch.start_stop_lock);
	online_vcpus = atomic_read(&vcpu->kvm->online_vcpus);

	/* SIGP STOP and SIGP STOP AND STORE STATUS has been fully processed */
	kvm_s390_clear_stop_irq(vcpu);

	atomic_or(CPUSTAT_STOPPED, &vcpu->arch.sie_block->cpuflags);
	__disable_ibs_on_vcpu(vcpu);

	for (i = 0; i < online_vcpus; i++) {
		if (!is_vcpu_stopped(vcpu->kvm->vcpus[i])) {
			started_vcpus++;
			started_vcpu = vcpu->kvm->vcpus[i];
		}
	}

	if (started_vcpus == 1) {
		/*
		 * As we only have one VCPU left, we want to enable the
		 * IBS facility for that VCPU to speed it up.
		 */
		__enable_ibs_on_vcpu(started_vcpu);
	}

	spin_unlock(&vcpu->kvm->arch.start_stop_lock);
	return;
}

static int kvm_vcpu_ioctl_enable_cap(struct kvm_vcpu *vcpu,
				     struct kvm_enable_cap *cap)
{
	int r;

	if (cap->flags)
		return -EINVAL;

	switch (cap->cap) {
	case KVM_CAP_S390_CSS_SUPPORT:
		if (!vcpu->kvm->arch.css_support) {
			vcpu->kvm->arch.css_support = 1;
			VM_EVENT(vcpu->kvm, 3, "%s", "ENABLE: CSS support");
			trace_kvm_s390_enable_css(vcpu->kvm);
		}
		r = 0;
		break;
	default:
		r = -EINVAL;
		break;
	}
	return r;
}

static long kvm_s390_guest_mem_op(struct kvm_vcpu *vcpu,
				  struct kvm_s390_mem_op *mop)
{
	void __user *uaddr = (void __user *)mop->buf;
	void *tmpbuf = NULL;
	int r, srcu_idx;
	const u64 supported_flags = KVM_S390_MEMOP_F_INJECT_EXCEPTION
				    | KVM_S390_MEMOP_F_CHECK_ONLY;

	if (mop->flags & ~supported_flags)
		return -EINVAL;

	if (mop->size > MEM_OP_MAX_SIZE)
		return -E2BIG;

	if (!(mop->flags & KVM_S390_MEMOP_F_CHECK_ONLY)) {
		tmpbuf = vmalloc(mop->size);
		if (!tmpbuf)
			return -ENOMEM;
	}

	srcu_idx = srcu_read_lock(&vcpu->kvm->srcu);

	switch (mop->op) {
	case KVM_S390_MEMOP_LOGICAL_READ:
		if (mop->flags & KVM_S390_MEMOP_F_CHECK_ONLY) {
			r = check_gva_range(vcpu, mop->gaddr, mop->ar, mop->size, false);
			break;
		}
		r = read_guest(vcpu, mop->gaddr, mop->ar, tmpbuf, mop->size);
		if (r == 0) {
			if (copy_to_user(uaddr, tmpbuf, mop->size))
				r = -EFAULT;
		}
		break;
	case KVM_S390_MEMOP_LOGICAL_WRITE:
		if (mop->flags & KVM_S390_MEMOP_F_CHECK_ONLY) {
			r = check_gva_range(vcpu, mop->gaddr, mop->ar, mop->size, true);
			break;
		}
		if (copy_from_user(tmpbuf, uaddr, mop->size)) {
			r = -EFAULT;
			break;
		}
		r = write_guest(vcpu, mop->gaddr, mop->ar, tmpbuf, mop->size);
		break;
	default:
		r = -EINVAL;
	}

	srcu_read_unlock(&vcpu->kvm->srcu, srcu_idx);

	if (r > 0 && (mop->flags & KVM_S390_MEMOP_F_INJECT_EXCEPTION) != 0)
		kvm_s390_inject_prog_irq(vcpu, &vcpu->arch.pgm);

	vfree(tmpbuf);
	return r;
}

long kvm_arch_vcpu_ioctl(struct file *filp,
			 unsigned int ioctl, unsigned long arg)
{
	struct kvm_vcpu *vcpu = filp->private_data;
	void __user *argp = (void __user *)arg;
	int idx;
	long r;

	switch (ioctl) {
	case KVM_S390_IRQ: {
		struct kvm_s390_irq s390irq;

		r = -EFAULT;
		if (copy_from_user(&s390irq, argp, sizeof(s390irq)))
			break;
		r = kvm_s390_inject_vcpu(vcpu, &s390irq);
		break;
	}
	case KVM_S390_INTERRUPT: {
		struct kvm_s390_interrupt s390int;
		struct kvm_s390_irq s390irq;

		r = -EFAULT;
		if (copy_from_user(&s390int, argp, sizeof(s390int)))
			break;
		if (s390int_to_s390irq(&s390int, &s390irq))
			return -EINVAL;
		r = kvm_s390_inject_vcpu(vcpu, &s390irq);
		break;
	}
	case KVM_S390_STORE_STATUS:
		idx = srcu_read_lock(&vcpu->kvm->srcu);
		r = kvm_s390_vcpu_store_status(vcpu, arg);
		srcu_read_unlock(&vcpu->kvm->srcu, idx);
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
		r = gmap_fault(vcpu->arch.gmap, arg, 0);
		break;
	}
	case KVM_ENABLE_CAP:
	{
		struct kvm_enable_cap cap;
		r = -EFAULT;
		if (copy_from_user(&cap, argp, sizeof(cap)))
			break;
		r = kvm_vcpu_ioctl_enable_cap(vcpu, &cap);
		break;
	}
	case KVM_S390_MEM_OP: {
		struct kvm_s390_mem_op mem_op;

		if (copy_from_user(&mem_op, argp, sizeof(mem_op)) == 0)
			r = kvm_s390_guest_mem_op(vcpu, &mem_op);
		else
			r = -EFAULT;
		break;
	}
	case KVM_S390_SET_IRQ_STATE: {
		struct kvm_s390_irq_state irq_state;

		r = -EFAULT;
		if (copy_from_user(&irq_state, argp, sizeof(irq_state)))
			break;
		if (irq_state.len > VCPU_IRQS_MAX_BUF ||
		    irq_state.len == 0 ||
		    irq_state.len % sizeof(struct kvm_s390_irq) > 0) {
			r = -EINVAL;
			break;
		}
		r = kvm_s390_set_irq_state(vcpu,
					   (void __user *) irq_state.buf,
					   irq_state.len);
		break;
	}
	case KVM_S390_GET_IRQ_STATE: {
		struct kvm_s390_irq_state irq_state;

		r = -EFAULT;
		if (copy_from_user(&irq_state, argp, sizeof(irq_state)))
			break;
		if (irq_state.len == 0) {
			r = -EINVAL;
			break;
		}
		r = kvm_s390_get_irq_state(vcpu,
					   (__u8 __user *)  irq_state.buf,
					   irq_state.len);
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

int kvm_arch_create_memslot(struct kvm *kvm, struct kvm_memory_slot *slot,
			    unsigned long npages)
{
	return 0;
}

/* Section: memory related */
int kvm_arch_prepare_memory_region(struct kvm *kvm,
				   struct kvm_memory_slot *memslot,
				   const struct kvm_userspace_memory_region *mem,
				   enum kvm_mr_change change)
{
	/* A few sanity checks. We can have memory slots which have to be
	   located/ended at a segment boundary (1MB). The memory in userland is
	   ok to be fragmented into various different vmas. It is okay to mmap()
	   and munmap() stuff in this slot after doing this call at any time */

	if (mem->userspace_addr & 0xffffful)
		return -EINVAL;

	if (mem->memory_size & 0xffffful)
		return -EINVAL;

	return 0;
}

void kvm_arch_commit_memory_region(struct kvm *kvm,
				const struct kvm_userspace_memory_region *mem,
				const struct kvm_memory_slot *old,
				const struct kvm_memory_slot *new,
				enum kvm_mr_change change)
{
	int rc;

	/* If the basics of the memslot do not change, we do not want
	 * to update the gmap. Every update causes several unnecessary
	 * segment translation exceptions. This is usually handled just
	 * fine by the normal fault handler + gmap, but it will also
	 * cause faults on the prefix page of running guest CPUs.
	 */
	if (old->userspace_addr == mem->userspace_addr &&
	    old->base_gfn * PAGE_SIZE == mem->guest_phys_addr &&
	    old->npages * PAGE_SIZE == mem->memory_size)
		return;

	rc = gmap_map_segment(kvm->arch.gmap, mem->userspace_addr,
		mem->guest_phys_addr, mem->memory_size);
	if (rc)
		pr_warn("failed to commit memory region\n");
	return;
}

static int __init kvm_s390_init(void)
{
	if (!sclp.has_sief2) {
		pr_info("SIE not available\n");
		return -ENODEV;
	}

	return kvm_init(NULL, sizeof(struct kvm_vcpu), 0, THIS_MODULE);
}

static void __exit kvm_s390_exit(void)
{
	kvm_exit();
}

module_init(kvm_s390_init);
module_exit(kvm_s390_exit);

/*
 * Enable autoloading of the kvm module.
 * Note that we add the module alias here instead of virt/kvm/kvm_main.c
 * since x86 takes a different approach.
 */
#include <linux/miscdevice.h>
MODULE_ALIAS_MISCDEV(KVM_MINOR);
MODULE_ALIAS("devname:kvm");
