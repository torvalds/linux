// SPDX-License-Identifier: GPL-2.0
/*
 * hosting IBM Z kernel virtual machines (s390x)
 *
 * Copyright IBM Corp. 2008, 2017
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
#include <linux/mman.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/random.h>
#include <linux/slab.h>
#include <linux/timer.h>
#include <linux/vmalloc.h>
#include <linux/bitmap.h>
#include <linux/sched/signal.h>
#include <linux/string.h>

#include <asm/asm-offsets.h>
#include <asm/lowcore.h>
#include <asm/stp.h>
#include <asm/pgtable.h>
#include <asm/gmap.h>
#include <asm/nmi.h>
#include <asm/switch_to.h>
#include <asm/isc.h>
#include <asm/sclp.h>
#include <asm/cpacf.h>
#include <asm/timex.h>
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
	{ "exit_pei", VCPU_STAT(exit_pei) },
	{ "exit_program_interruption", VCPU_STAT(exit_program_interruption) },
	{ "exit_instr_and_program_int", VCPU_STAT(exit_instr_and_program) },
	{ "exit_operation_exception", VCPU_STAT(exit_operation_exception) },
	{ "halt_successful_poll", VCPU_STAT(halt_successful_poll) },
	{ "halt_attempted_poll", VCPU_STAT(halt_attempted_poll) },
	{ "halt_poll_invalid", VCPU_STAT(halt_poll_invalid) },
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
	{ "instruction_sthyi", VCPU_STAT(instruction_sthyi) },
	{ "instruction_sie", VCPU_STAT(instruction_sie) },
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

struct kvm_s390_tod_clock_ext {
	__u8 epoch_idx;
	__u64 tod;
	__u8 reserved[7];
} __packed;

/* allow nested virtualization in KVM (if enabled by user space) */
static int nested;
module_param(nested, int, S_IRUGO);
MODULE_PARM_DESC(nested, "Nested virtualization support");

/* upper facilities limit for kvm */
unsigned long kvm_s390_fac_list_mask[16] = { FACILITIES_KVM };

unsigned long kvm_s390_fac_list_mask_size(void)
{
	BUILD_BUG_ON(ARRAY_SIZE(kvm_s390_fac_list_mask) > S390_ARCH_FAC_MASK_SIZE_U64);
	return ARRAY_SIZE(kvm_s390_fac_list_mask);
}

/* available cpu features supported by kvm */
static DECLARE_BITMAP(kvm_s390_available_cpu_feat, KVM_S390_VM_CPU_FEAT_NR_BITS);
/* available subfunctions indicated via query / "test bit" */
static struct kvm_s390_vm_cpu_subfunc kvm_s390_available_subfunc;

static struct gmap_notifier gmap_notifier;
static struct gmap_notifier vsie_gmap_notifier;
debug_info_t *kvm_s390_dbf;

/* Section: not file related */
int kvm_arch_hardware_enable(void)
{
	/* every s390 is virtualization enabled ;-) */
	return 0;
}

static void kvm_gmap_notifier(struct gmap *gmap, unsigned long start,
			      unsigned long end);

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
			if (vcpu->arch.cputm_enabled)
				vcpu->arch.cputm_start += *delta;
			if (vcpu->arch.vsie_block)
				vcpu->arch.vsie_block->epoch -= *delta;
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
	gmap_register_pte_notifier(&gmap_notifier);
	vsie_gmap_notifier.notifier_call = kvm_s390_vsie_gmap_notifier;
	gmap_register_pte_notifier(&vsie_gmap_notifier);
	atomic_notifier_chain_register(&s390_epoch_delta_notifier,
				       &kvm_clock_notifier);
	return 0;
}

void kvm_arch_hardware_unsetup(void)
{
	gmap_unregister_pte_notifier(&gmap_notifier);
	gmap_unregister_pte_notifier(&vsie_gmap_notifier);
	atomic_notifier_chain_unregister(&s390_epoch_delta_notifier,
					 &kvm_clock_notifier);
}

static void allow_cpu_feat(unsigned long nr)
{
	set_bit_inv(nr, kvm_s390_available_cpu_feat);
}

static inline int plo_test_bit(unsigned char nr)
{
	register unsigned long r0 asm("0") = (unsigned long) nr | 0x100;
	int cc;

	asm volatile(
		/* Parameter registers are ignored for "test bit" */
		"	plo	0,0,0,0(0)\n"
		"	ipm	%0\n"
		"	srl	%0,28\n"
		: "=d" (cc)
		: "d" (r0)
		: "cc");
	return cc == 0;
}

static void kvm_s390_cpu_feat_init(void)
{
	int i;

	for (i = 0; i < 256; ++i) {
		if (plo_test_bit(i))
			kvm_s390_available_subfunc.plo[i >> 3] |= 0x80 >> (i & 7);
	}

	if (test_facility(28)) /* TOD-clock steering */
		ptff(kvm_s390_available_subfunc.ptff,
		     sizeof(kvm_s390_available_subfunc.ptff),
		     PTFF_QAF);

	if (test_facility(17)) { /* MSA */
		__cpacf_query(CPACF_KMAC, (cpacf_mask_t *)
			      kvm_s390_available_subfunc.kmac);
		__cpacf_query(CPACF_KMC, (cpacf_mask_t *)
			      kvm_s390_available_subfunc.kmc);
		__cpacf_query(CPACF_KM, (cpacf_mask_t *)
			      kvm_s390_available_subfunc.km);
		__cpacf_query(CPACF_KIMD, (cpacf_mask_t *)
			      kvm_s390_available_subfunc.kimd);
		__cpacf_query(CPACF_KLMD, (cpacf_mask_t *)
			      kvm_s390_available_subfunc.klmd);
	}
	if (test_facility(76)) /* MSA3 */
		__cpacf_query(CPACF_PCKMO, (cpacf_mask_t *)
			      kvm_s390_available_subfunc.pckmo);
	if (test_facility(77)) { /* MSA4 */
		__cpacf_query(CPACF_KMCTR, (cpacf_mask_t *)
			      kvm_s390_available_subfunc.kmctr);
		__cpacf_query(CPACF_KMF, (cpacf_mask_t *)
			      kvm_s390_available_subfunc.kmf);
		__cpacf_query(CPACF_KMO, (cpacf_mask_t *)
			      kvm_s390_available_subfunc.kmo);
		__cpacf_query(CPACF_PCC, (cpacf_mask_t *)
			      kvm_s390_available_subfunc.pcc);
	}
	if (test_facility(57)) /* MSA5 */
		__cpacf_query(CPACF_PRNO, (cpacf_mask_t *)
			      kvm_s390_available_subfunc.ppno);

	if (test_facility(146)) /* MSA8 */
		__cpacf_query(CPACF_KMA, (cpacf_mask_t *)
			      kvm_s390_available_subfunc.kma);

	if (MACHINE_HAS_ESOP)
		allow_cpu_feat(KVM_S390_VM_CPU_FEAT_ESOP);
	/*
	 * We need SIE support, ESOP (PROT_READ protection for gmap_shadow),
	 * 64bit SCAO (SCA passthrough) and IDTE (for gmap_shadow unshadowing).
	 */
	if (!sclp.has_sief2 || !MACHINE_HAS_ESOP || !sclp.has_64bscao ||
	    !test_facility(3) || !nested)
		return;
	allow_cpu_feat(KVM_S390_VM_CPU_FEAT_SIEF2);
	if (sclp.has_64bscao)
		allow_cpu_feat(KVM_S390_VM_CPU_FEAT_64BSCAO);
	if (sclp.has_siif)
		allow_cpu_feat(KVM_S390_VM_CPU_FEAT_SIIF);
	if (sclp.has_gpere)
		allow_cpu_feat(KVM_S390_VM_CPU_FEAT_GPERE);
	if (sclp.has_gsls)
		allow_cpu_feat(KVM_S390_VM_CPU_FEAT_GSLS);
	if (sclp.has_ib)
		allow_cpu_feat(KVM_S390_VM_CPU_FEAT_IB);
	if (sclp.has_cei)
		allow_cpu_feat(KVM_S390_VM_CPU_FEAT_CEI);
	if (sclp.has_ibs)
		allow_cpu_feat(KVM_S390_VM_CPU_FEAT_IBS);
	if (sclp.has_kss)
		allow_cpu_feat(KVM_S390_VM_CPU_FEAT_KSS);
	/*
	 * KVM_S390_VM_CPU_FEAT_SKEY: Wrong shadow of PTE.I bits will make
	 * all skey handling functions read/set the skey from the PGSTE
	 * instead of the real storage key.
	 *
	 * KVM_S390_VM_CPU_FEAT_CMMA: Wrong shadow of PTE.I bits will make
	 * pages being detected as preserved although they are resident.
	 *
	 * KVM_S390_VM_CPU_FEAT_PFMFI: Wrong shadow of PTE.I bits will
	 * have the same effect as for KVM_S390_VM_CPU_FEAT_SKEY.
	 *
	 * For KVM_S390_VM_CPU_FEAT_SKEY, KVM_S390_VM_CPU_FEAT_CMMA and
	 * KVM_S390_VM_CPU_FEAT_PFMFI, all PTE.I and PGSTE bits have to be
	 * correctly shadowed. We can do that for the PGSTE but not for PTE.I.
	 *
	 * KVM_S390_VM_CPU_FEAT_SIGPIF: Wrong SCB addresses in the SCA. We
	 * cannot easily shadow the SCA because of the ipte lock.
	 */
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

	kvm_s390_cpu_feat_init();

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
	case KVM_CAP_IMMEDIATE_EXIT:
	case KVM_CAP_S390_INJECT_IRQ:
	case KVM_CAP_S390_USER_SIGP:
	case KVM_CAP_S390_USER_STSI:
	case KVM_CAP_S390_SKEYS:
	case KVM_CAP_S390_IRQ_STATE:
	case KVM_CAP_S390_USER_INSTR0:
	case KVM_CAP_S390_CMMA_MIGRATION:
	case KVM_CAP_S390_AIS:
	case KVM_CAP_S390_AIS_MIGRATION:
		r = 1;
		break;
	case KVM_CAP_S390_MEM_OP:
		r = MEM_OP_MAX_SIZE;
		break;
	case KVM_CAP_NR_VCPUS:
	case KVM_CAP_MAX_VCPUS:
		r = KVM_S390_BSCA_CPU_SLOTS;
		if (!kvm_s390_use_sca_entries())
			r = KVM_MAX_VCPUS;
		else if (sclp.has_esca && sclp.has_64bscao)
			r = KVM_S390_ESCA_CPU_SLOTS;
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
	case KVM_CAP_S390_RI:
		r = test_facility(64);
		break;
	case KVM_CAP_S390_GS:
		r = test_facility(133);
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

	/* Loop over all guest pages */
	last_gfn = memslot->base_gfn + memslot->npages;
	for (cur_gfn = memslot->base_gfn; cur_gfn <= last_gfn; cur_gfn++) {
		address = gfn_to_hva_memslot(memslot, cur_gfn);

		if (test_and_clear_guest_dirty(gmap->mm, address))
			mark_page_dirty(kvm, cur_gfn);
		if (fatal_signal_pending(current))
			return;
		cond_resched();
	}
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

	if (kvm_is_ucontrol(kvm))
		return -EINVAL;

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

static void icpt_operexc_on_all_vcpus(struct kvm *kvm)
{
	unsigned int i;
	struct kvm_vcpu *vcpu;

	kvm_for_each_vcpu(i, vcpu, kvm) {
		kvm_s390_sync_request(KVM_REQ_ICPT_OPEREXC, vcpu);
	}
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
		if (kvm->created_vcpus) {
			r = -EBUSY;
		} else if (MACHINE_HAS_VX) {
			set_kvm_facility(kvm->arch.model.fac_mask, 129);
			set_kvm_facility(kvm->arch.model.fac_list, 129);
			if (test_facility(134)) {
				set_kvm_facility(kvm->arch.model.fac_mask, 134);
				set_kvm_facility(kvm->arch.model.fac_list, 134);
			}
			if (test_facility(135)) {
				set_kvm_facility(kvm->arch.model.fac_mask, 135);
				set_kvm_facility(kvm->arch.model.fac_list, 135);
			}
			r = 0;
		} else
			r = -EINVAL;
		mutex_unlock(&kvm->lock);
		VM_EVENT(kvm, 3, "ENABLE: CAP_S390_VECTOR_REGISTERS %s",
			 r ? "(not available)" : "(success)");
		break;
	case KVM_CAP_S390_RI:
		r = -EINVAL;
		mutex_lock(&kvm->lock);
		if (kvm->created_vcpus) {
			r = -EBUSY;
		} else if (test_facility(64)) {
			set_kvm_facility(kvm->arch.model.fac_mask, 64);
			set_kvm_facility(kvm->arch.model.fac_list, 64);
			r = 0;
		}
		mutex_unlock(&kvm->lock);
		VM_EVENT(kvm, 3, "ENABLE: CAP_S390_RI %s",
			 r ? "(not available)" : "(success)");
		break;
	case KVM_CAP_S390_AIS:
		mutex_lock(&kvm->lock);
		if (kvm->created_vcpus) {
			r = -EBUSY;
		} else {
			set_kvm_facility(kvm->arch.model.fac_mask, 72);
			set_kvm_facility(kvm->arch.model.fac_list, 72);
			r = 0;
		}
		mutex_unlock(&kvm->lock);
		VM_EVENT(kvm, 3, "ENABLE: AIS %s",
			 r ? "(not available)" : "(success)");
		break;
	case KVM_CAP_S390_GS:
		r = -EINVAL;
		mutex_lock(&kvm->lock);
		if (atomic_read(&kvm->online_vcpus)) {
			r = -EBUSY;
		} else if (test_facility(133)) {
			set_kvm_facility(kvm->arch.model.fac_mask, 133);
			set_kvm_facility(kvm->arch.model.fac_list, 133);
			r = 0;
		}
		mutex_unlock(&kvm->lock);
		VM_EVENT(kvm, 3, "ENABLE: CAP_S390_GS %s",
			 r ? "(not available)" : "(success)");
		break;
	case KVM_CAP_S390_USER_STSI:
		VM_EVENT(kvm, 3, "%s", "ENABLE: CAP_S390_USER_STSI");
		kvm->arch.user_stsi = 1;
		r = 0;
		break;
	case KVM_CAP_S390_USER_INSTR0:
		VM_EVENT(kvm, 3, "%s", "ENABLE: CAP_S390_USER_INSTR0");
		kvm->arch.user_instr0 = 1;
		icpt_operexc_on_all_vcpus(kvm);
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
			 kvm->arch.mem_limit);
		if (put_user(kvm->arch.mem_limit, (u64 __user *)attr->addr))
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
		ret = -ENXIO;
		if (!sclp.has_cmma)
			break;

		ret = -EBUSY;
		VM_EVENT(kvm, 3, "%s", "ENABLE: CMMA support");
		mutex_lock(&kvm->lock);
		if (!kvm->created_vcpus) {
			kvm->arch.use_cmma = 1;
			ret = 0;
		}
		mutex_unlock(&kvm->lock);
		break;
	case KVM_S390_VM_MEM_CLR_CMMA:
		ret = -ENXIO;
		if (!sclp.has_cmma)
			break;
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

		if (kvm->arch.mem_limit != KVM_S390_NO_MEM_LIMIT &&
		    new_limit > kvm->arch.mem_limit)
			return -E2BIG;

		if (!new_limit)
			return -EINVAL;

		/* gmap_create takes last usable address */
		if (new_limit != KVM_S390_NO_MEM_LIMIT)
			new_limit -= 1;

		ret = -EBUSY;
		mutex_lock(&kvm->lock);
		if (!kvm->created_vcpus) {
			/* gmap_create will round the limit up */
			struct gmap *new = gmap_create(current->mm, new_limit);

			if (!new) {
				ret = -ENOMEM;
			} else {
				gmap_remove(kvm->arch.gmap);
				new->private = kvm;
				kvm->arch.gmap = new;
				ret = 0;
			}
		}
		mutex_unlock(&kvm->lock);
		VM_EVENT(kvm, 3, "SET: max guest address: %lu", new_limit);
		VM_EVENT(kvm, 3, "New guest asce: 0x%pK",
			 (void *) kvm->arch.gmap->asce);
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

static void kvm_s390_sync_request_broadcast(struct kvm *kvm, int req)
{
	int cx;
	struct kvm_vcpu *vcpu;

	kvm_for_each_vcpu(cx, vcpu, kvm)
		kvm_s390_sync_request(req, vcpu);
}

/*
 * Must be called with kvm->srcu held to avoid races on memslots, and with
 * kvm->lock to avoid races with ourselves and kvm_s390_vm_stop_migration.
 */
static int kvm_s390_vm_start_migration(struct kvm *kvm)
{
	struct kvm_s390_migration_state *mgs;
	struct kvm_memory_slot *ms;
	/* should be the only one */
	struct kvm_memslots *slots;
	unsigned long ram_pages;
	int slotnr;

	/* migration mode already enabled */
	if (kvm->arch.migration_state)
		return 0;

	slots = kvm_memslots(kvm);
	if (!slots || !slots->used_slots)
		return -EINVAL;

	mgs = kzalloc(sizeof(*mgs), GFP_KERNEL);
	if (!mgs)
		return -ENOMEM;
	kvm->arch.migration_state = mgs;

	if (kvm->arch.use_cmma) {
		/*
		 * Get the last slot. They should be sorted by base_gfn, so the
		 * last slot is also the one at the end of the address space.
		 * We have verified above that at least one slot is present.
		 */
		ms = slots->memslots + slots->used_slots - 1;
		/* round up so we only use full longs */
		ram_pages = roundup(ms->base_gfn + ms->npages, BITS_PER_LONG);
		/* allocate enough bytes to store all the bits */
		mgs->pgste_bitmap = vmalloc(ram_pages / 8);
		if (!mgs->pgste_bitmap) {
			kfree(mgs);
			kvm->arch.migration_state = NULL;
			return -ENOMEM;
		}

		mgs->bitmap_size = ram_pages;
		atomic64_set(&mgs->dirty_pages, ram_pages);
		/* mark all the pages in active slots as dirty */
		for (slotnr = 0; slotnr < slots->used_slots; slotnr++) {
			ms = slots->memslots + slotnr;
			bitmap_set(mgs->pgste_bitmap, ms->base_gfn, ms->npages);
		}

		kvm_s390_sync_request_broadcast(kvm, KVM_REQ_START_MIGRATION);
	}
	return 0;
}

/*
 * Must be called with kvm->lock to avoid races with ourselves and
 * kvm_s390_vm_start_migration.
 */
static int kvm_s390_vm_stop_migration(struct kvm *kvm)
{
	struct kvm_s390_migration_state *mgs;

	/* migration mode already disabled */
	if (!kvm->arch.migration_state)
		return 0;
	mgs = kvm->arch.migration_state;
	kvm->arch.migration_state = NULL;

	if (kvm->arch.use_cmma) {
		kvm_s390_sync_request_broadcast(kvm, KVM_REQ_STOP_MIGRATION);
		vfree(mgs->pgste_bitmap);
	}
	kfree(mgs);
	return 0;
}

static int kvm_s390_vm_set_migration(struct kvm *kvm,
				     struct kvm_device_attr *attr)
{
	int idx, res = -ENXIO;

	mutex_lock(&kvm->lock);
	switch (attr->attr) {
	case KVM_S390_VM_MIGRATION_START:
		idx = srcu_read_lock(&kvm->srcu);
		res = kvm_s390_vm_start_migration(kvm);
		srcu_read_unlock(&kvm->srcu, idx);
		break;
	case KVM_S390_VM_MIGRATION_STOP:
		res = kvm_s390_vm_stop_migration(kvm);
		break;
	default:
		break;
	}
	mutex_unlock(&kvm->lock);

	return res;
}

static int kvm_s390_vm_get_migration(struct kvm *kvm,
				     struct kvm_device_attr *attr)
{
	u64 mig = (kvm->arch.migration_state != NULL);

	if (attr->attr != KVM_S390_VM_MIGRATION_STATUS)
		return -ENXIO;

	if (copy_to_user((void __user *)attr->addr, &mig, sizeof(mig)))
		return -EFAULT;
	return 0;
}

static int kvm_s390_set_tod_ext(struct kvm *kvm, struct kvm_device_attr *attr)
{
	struct kvm_s390_vm_tod_clock gtod;

	if (copy_from_user(&gtod, (void __user *)attr->addr, sizeof(gtod)))
		return -EFAULT;

	if (test_kvm_facility(kvm, 139))
		kvm_s390_set_tod_clock_ext(kvm, &gtod);
	else if (gtod.epoch_idx == 0)
		kvm_s390_set_tod_clock(kvm, gtod.tod);
	else
		return -EINVAL;

	VM_EVENT(kvm, 3, "SET: TOD extension: 0x%x, TOD base: 0x%llx",
		gtod.epoch_idx, gtod.tod);

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
	case KVM_S390_VM_TOD_EXT:
		ret = kvm_s390_set_tod_ext(kvm, attr);
		break;
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

static void kvm_s390_get_tod_clock_ext(struct kvm *kvm,
					struct kvm_s390_vm_tod_clock *gtod)
{
	struct kvm_s390_tod_clock_ext htod;

	preempt_disable();

	get_tod_clock_ext((char *)&htod);

	gtod->tod = htod.tod + kvm->arch.epoch;
	gtod->epoch_idx = htod.epoch_idx + kvm->arch.epdx;

	if (gtod->tod < htod.tod)
		gtod->epoch_idx += 1;

	preempt_enable();
}

static int kvm_s390_get_tod_ext(struct kvm *kvm, struct kvm_device_attr *attr)
{
	struct kvm_s390_vm_tod_clock gtod;

	memset(&gtod, 0, sizeof(gtod));

	if (test_kvm_facility(kvm, 139))
		kvm_s390_get_tod_clock_ext(kvm, &gtod);
	else
		gtod.tod = kvm_s390_get_tod_clock_fast(kvm);

	if (copy_to_user((void __user *)attr->addr, &gtod, sizeof(gtod)))
		return -EFAULT;

	VM_EVENT(kvm, 3, "QUERY: TOD extension: 0x%x, TOD base: 0x%llx",
		gtod.epoch_idx, gtod.tod);
	return 0;
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
	case KVM_S390_VM_TOD_EXT:
		ret = kvm_s390_get_tod_ext(kvm, attr);
		break;
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
	u16 lowest_ibc, unblocked_ibc;
	int ret = 0;

	mutex_lock(&kvm->lock);
	if (kvm->created_vcpus) {
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
		kvm->arch.model.cpuid = proc->cpuid;
		lowest_ibc = sclp.ibc >> 16 & 0xfff;
		unblocked_ibc = sclp.ibc & 0xfff;
		if (lowest_ibc && proc->ibc) {
			if (proc->ibc > unblocked_ibc)
				kvm->arch.model.ibc = unblocked_ibc;
			else if (proc->ibc < lowest_ibc)
				kvm->arch.model.ibc = lowest_ibc;
			else
				kvm->arch.model.ibc = proc->ibc;
		}
		memcpy(kvm->arch.model.fac_list, proc->fac_list,
		       S390_ARCH_FAC_LIST_SIZE_BYTE);
		VM_EVENT(kvm, 3, "SET: guest ibc: 0x%4.4x, guest cpuid: 0x%16.16llx",
			 kvm->arch.model.ibc,
			 kvm->arch.model.cpuid);
		VM_EVENT(kvm, 3, "SET: guest faclist: 0x%16.16llx.%16.16llx.%16.16llx",
			 kvm->arch.model.fac_list[0],
			 kvm->arch.model.fac_list[1],
			 kvm->arch.model.fac_list[2]);
	} else
		ret = -EFAULT;
	kfree(proc);
out:
	mutex_unlock(&kvm->lock);
	return ret;
}

static int kvm_s390_set_processor_feat(struct kvm *kvm,
				       struct kvm_device_attr *attr)
{
	struct kvm_s390_vm_cpu_feat data;
	int ret = -EBUSY;

	if (copy_from_user(&data, (void __user *)attr->addr, sizeof(data)))
		return -EFAULT;
	if (!bitmap_subset((unsigned long *) data.feat,
			   kvm_s390_available_cpu_feat,
			   KVM_S390_VM_CPU_FEAT_NR_BITS))
		return -EINVAL;

	mutex_lock(&kvm->lock);
	if (!atomic_read(&kvm->online_vcpus)) {
		bitmap_copy(kvm->arch.cpu_feat, (unsigned long *) data.feat,
			    KVM_S390_VM_CPU_FEAT_NR_BITS);
		ret = 0;
	}
	mutex_unlock(&kvm->lock);
	return ret;
}

static int kvm_s390_set_processor_subfunc(struct kvm *kvm,
					  struct kvm_device_attr *attr)
{
	/*
	 * Once supported by kernel + hw, we have to store the subfunctions
	 * in kvm->arch and remember that user space configured them.
	 */
	return -ENXIO;
}

static int kvm_s390_set_cpu_model(struct kvm *kvm, struct kvm_device_attr *attr)
{
	int ret = -ENXIO;

	switch (attr->attr) {
	case KVM_S390_VM_CPU_PROCESSOR:
		ret = kvm_s390_set_processor(kvm, attr);
		break;
	case KVM_S390_VM_CPU_PROCESSOR_FEAT:
		ret = kvm_s390_set_processor_feat(kvm, attr);
		break;
	case KVM_S390_VM_CPU_PROCESSOR_SUBFUNC:
		ret = kvm_s390_set_processor_subfunc(kvm, attr);
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
	proc->cpuid = kvm->arch.model.cpuid;
	proc->ibc = kvm->arch.model.ibc;
	memcpy(&proc->fac_list, kvm->arch.model.fac_list,
	       S390_ARCH_FAC_LIST_SIZE_BYTE);
	VM_EVENT(kvm, 3, "GET: guest ibc: 0x%4.4x, guest cpuid: 0x%16.16llx",
		 kvm->arch.model.ibc,
		 kvm->arch.model.cpuid);
	VM_EVENT(kvm, 3, "GET: guest faclist: 0x%16.16llx.%16.16llx.%16.16llx",
		 kvm->arch.model.fac_list[0],
		 kvm->arch.model.fac_list[1],
		 kvm->arch.model.fac_list[2]);
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
	memcpy(&mach->fac_mask, kvm->arch.model.fac_mask,
	       S390_ARCH_FAC_LIST_SIZE_BYTE);
	memcpy((unsigned long *)&mach->fac_list, S390_lowcore.stfle_fac_list,
	       sizeof(S390_lowcore.stfle_fac_list));
	VM_EVENT(kvm, 3, "GET: host ibc:  0x%4.4x, host cpuid:  0x%16.16llx",
		 kvm->arch.model.ibc,
		 kvm->arch.model.cpuid);
	VM_EVENT(kvm, 3, "GET: host facmask:  0x%16.16llx.%16.16llx.%16.16llx",
		 mach->fac_mask[0],
		 mach->fac_mask[1],
		 mach->fac_mask[2]);
	VM_EVENT(kvm, 3, "GET: host faclist:  0x%16.16llx.%16.16llx.%16.16llx",
		 mach->fac_list[0],
		 mach->fac_list[1],
		 mach->fac_list[2]);
	if (copy_to_user((void __user *)attr->addr, mach, sizeof(*mach)))
		ret = -EFAULT;
	kfree(mach);
out:
	return ret;
}

static int kvm_s390_get_processor_feat(struct kvm *kvm,
				       struct kvm_device_attr *attr)
{
	struct kvm_s390_vm_cpu_feat data;

	bitmap_copy((unsigned long *) data.feat, kvm->arch.cpu_feat,
		    KVM_S390_VM_CPU_FEAT_NR_BITS);
	if (copy_to_user((void __user *)attr->addr, &data, sizeof(data)))
		return -EFAULT;
	return 0;
}

static int kvm_s390_get_machine_feat(struct kvm *kvm,
				     struct kvm_device_attr *attr)
{
	struct kvm_s390_vm_cpu_feat data;

	bitmap_copy((unsigned long *) data.feat,
		    kvm_s390_available_cpu_feat,
		    KVM_S390_VM_CPU_FEAT_NR_BITS);
	if (copy_to_user((void __user *)attr->addr, &data, sizeof(data)))
		return -EFAULT;
	return 0;
}

static int kvm_s390_get_processor_subfunc(struct kvm *kvm,
					  struct kvm_device_attr *attr)
{
	/*
	 * Once we can actually configure subfunctions (kernel + hw support),
	 * we have to check if they were already set by user space, if so copy
	 * them from kvm->arch.
	 */
	return -ENXIO;
}

static int kvm_s390_get_machine_subfunc(struct kvm *kvm,
					struct kvm_device_attr *attr)
{
	if (copy_to_user((void __user *)attr->addr, &kvm_s390_available_subfunc,
	    sizeof(struct kvm_s390_vm_cpu_subfunc)))
		return -EFAULT;
	return 0;
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
	case KVM_S390_VM_CPU_PROCESSOR_FEAT:
		ret = kvm_s390_get_processor_feat(kvm, attr);
		break;
	case KVM_S390_VM_CPU_MACHINE_FEAT:
		ret = kvm_s390_get_machine_feat(kvm, attr);
		break;
	case KVM_S390_VM_CPU_PROCESSOR_SUBFUNC:
		ret = kvm_s390_get_processor_subfunc(kvm, attr);
		break;
	case KVM_S390_VM_CPU_MACHINE_SUBFUNC:
		ret = kvm_s390_get_machine_subfunc(kvm, attr);
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
	case KVM_S390_VM_MIGRATION:
		ret = kvm_s390_vm_set_migration(kvm, attr);
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
	case KVM_S390_VM_MIGRATION:
		ret = kvm_s390_vm_get_migration(kvm, attr);
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
			ret = sclp.has_cmma ? 0 : -ENXIO;
			break;
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
		case KVM_S390_VM_CPU_PROCESSOR_FEAT:
		case KVM_S390_VM_CPU_MACHINE_FEAT:
		case KVM_S390_VM_CPU_MACHINE_SUBFUNC:
			ret = 0;
			break;
		/* configuring subfunctions is not supported yet */
		case KVM_S390_VM_CPU_PROCESSOR_SUBFUNC:
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
	case KVM_S390_VM_MIGRATION:
		ret = 0;
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
	int srcu_idx, i, r = 0;

	if (args->flags != 0)
		return -EINVAL;

	/* Is this guest using storage keys? */
	if (!mm_use_skey(current->mm))
		return KVM_S390_GET_SKEYS_NONE;

	/* Enforce sane limit on memory allocation */
	if (args->count < 1 || args->count > KVM_S390_SKEYS_MAX)
		return -EINVAL;

	keys = kvmalloc_array(args->count, sizeof(uint8_t), GFP_KERNEL);
	if (!keys)
		return -ENOMEM;

	down_read(&current->mm->mmap_sem);
	srcu_idx = srcu_read_lock(&kvm->srcu);
	for (i = 0; i < args->count; i++) {
		hva = gfn_to_hva(kvm, args->start_gfn + i);
		if (kvm_is_error_hva(hva)) {
			r = -EFAULT;
			break;
		}

		r = get_guest_storage_key(current->mm, hva, &keys[i]);
		if (r)
			break;
	}
	srcu_read_unlock(&kvm->srcu, srcu_idx);
	up_read(&current->mm->mmap_sem);

	if (!r) {
		r = copy_to_user((uint8_t __user *)args->skeydata_addr, keys,
				 sizeof(uint8_t) * args->count);
		if (r)
			r = -EFAULT;
	}

	kvfree(keys);
	return r;
}

static long kvm_s390_set_skeys(struct kvm *kvm, struct kvm_s390_skeys *args)
{
	uint8_t *keys;
	uint64_t hva;
	int srcu_idx, i, r = 0;

	if (args->flags != 0)
		return -EINVAL;

	/* Enforce sane limit on memory allocation */
	if (args->count < 1 || args->count > KVM_S390_SKEYS_MAX)
		return -EINVAL;

	keys = kvmalloc_array(args->count, sizeof(uint8_t), GFP_KERNEL);
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

	down_read(&current->mm->mmap_sem);
	srcu_idx = srcu_read_lock(&kvm->srcu);
	for (i = 0; i < args->count; i++) {
		hva = gfn_to_hva(kvm, args->start_gfn + i);
		if (kvm_is_error_hva(hva)) {
			r = -EFAULT;
			break;
		}

		/* Lowest order bit is reserved */
		if (keys[i] & 0x01) {
			r = -EINVAL;
			break;
		}

		r = set_guest_storage_key(current->mm, hva, keys[i], 0);
		if (r)
			break;
	}
	srcu_read_unlock(&kvm->srcu, srcu_idx);
	up_read(&current->mm->mmap_sem);
out:
	kvfree(keys);
	return r;
}

/*
 * Base address and length must be sent at the start of each block, therefore
 * it's cheaper to send some clean data, as long as it's less than the size of
 * two longs.
 */
#define KVM_S390_MAX_BIT_DISTANCE (2 * sizeof(void *))
/* for consistency */
#define KVM_S390_CMMA_SIZE_MAX ((u32)KVM_S390_SKEYS_MAX)

/*
 * This function searches for the next page with dirty CMMA attributes, and
 * saves the attributes in the buffer up to either the end of the buffer or
 * until a block of at least KVM_S390_MAX_BIT_DISTANCE clean bits is found;
 * no trailing clean bytes are saved.
 * In case no dirty bits were found, or if CMMA was not enabled or used, the
 * output buffer will indicate 0 as length.
 */
static int kvm_s390_get_cmma_bits(struct kvm *kvm,
				  struct kvm_s390_cmma_log *args)
{
	struct kvm_s390_migration_state *s = kvm->arch.migration_state;
	unsigned long bufsize, hva, pgstev, i, next, cur;
	int srcu_idx, peek, r = 0, rr;
	u8 *res;

	cur = args->start_gfn;
	i = next = pgstev = 0;

	if (unlikely(!kvm->arch.use_cmma))
		return -ENXIO;
	/* Invalid/unsupported flags were specified */
	if (args->flags & ~KVM_S390_CMMA_PEEK)
		return -EINVAL;
	/* Migration mode query, and we are not doing a migration */
	peek = !!(args->flags & KVM_S390_CMMA_PEEK);
	if (!peek && !s)
		return -EINVAL;
	/* CMMA is disabled or was not used, or the buffer has length zero */
	bufsize = min(args->count, KVM_S390_CMMA_SIZE_MAX);
	if (!bufsize || !kvm->mm->context.use_cmma) {
		memset(args, 0, sizeof(*args));
		return 0;
	}

	if (!peek) {
		/* We are not peeking, and there are no dirty pages */
		if (!atomic64_read(&s->dirty_pages)) {
			memset(args, 0, sizeof(*args));
			return 0;
		}
		cur = find_next_bit(s->pgste_bitmap, s->bitmap_size,
				    args->start_gfn);
		if (cur >= s->bitmap_size)	/* nothing found, loop back */
			cur = find_next_bit(s->pgste_bitmap, s->bitmap_size, 0);
		if (cur >= s->bitmap_size) {	/* again! (very unlikely) */
			memset(args, 0, sizeof(*args));
			return 0;
		}
		next = find_next_bit(s->pgste_bitmap, s->bitmap_size, cur + 1);
	}

	res = vmalloc(bufsize);
	if (!res)
		return -ENOMEM;

	args->start_gfn = cur;

	down_read(&kvm->mm->mmap_sem);
	srcu_idx = srcu_read_lock(&kvm->srcu);
	while (i < bufsize) {
		hva = gfn_to_hva(kvm, cur);
		if (kvm_is_error_hva(hva)) {
			r = -EFAULT;
			break;
		}
		/* decrement only if we actually flipped the bit to 0 */
		if (!peek && test_and_clear_bit(cur, s->pgste_bitmap))
			atomic64_dec(&s->dirty_pages);
		r = get_pgste(kvm->mm, hva, &pgstev);
		if (r < 0)
			pgstev = 0;
		/* save the value */
		res[i++] = (pgstev >> 24) & 0x43;
		/*
		 * if the next bit is too far away, stop.
		 * if we reached the previous "next", find the next one
		 */
		if (!peek) {
			if (next > cur + KVM_S390_MAX_BIT_DISTANCE)
				break;
			if (cur == next)
				next = find_next_bit(s->pgste_bitmap,
						     s->bitmap_size, cur + 1);
		/* reached the end of the bitmap or of the buffer, stop */
			if ((next >= s->bitmap_size) ||
			    (next >= args->start_gfn + bufsize))
				break;
		}
		cur++;
	}
	srcu_read_unlock(&kvm->srcu, srcu_idx);
	up_read(&kvm->mm->mmap_sem);
	args->count = i;
	args->remaining = s ? atomic64_read(&s->dirty_pages) : 0;

	rr = copy_to_user((void __user *)args->values, res, args->count);
	if (rr)
		r = -EFAULT;

	vfree(res);
	return r;
}

/*
 * This function sets the CMMA attributes for the given pages. If the input
 * buffer has zero length, no action is taken, otherwise the attributes are
 * set and the mm->context.use_cmma flag is set.
 */
static int kvm_s390_set_cmma_bits(struct kvm *kvm,
				  const struct kvm_s390_cmma_log *args)
{
	unsigned long hva, mask, pgstev, i;
	uint8_t *bits;
	int srcu_idx, r = 0;

	mask = args->mask;

	if (!kvm->arch.use_cmma)
		return -ENXIO;
	/* invalid/unsupported flags */
	if (args->flags != 0)
		return -EINVAL;
	/* Enforce sane limit on memory allocation */
	if (args->count > KVM_S390_CMMA_SIZE_MAX)
		return -EINVAL;
	/* Nothing to do */
	if (args->count == 0)
		return 0;

	bits = vmalloc(sizeof(*bits) * args->count);
	if (!bits)
		return -ENOMEM;

	r = copy_from_user(bits, (void __user *)args->values, args->count);
	if (r) {
		r = -EFAULT;
		goto out;
	}

	down_read(&kvm->mm->mmap_sem);
	srcu_idx = srcu_read_lock(&kvm->srcu);
	for (i = 0; i < args->count; i++) {
		hva = gfn_to_hva(kvm, args->start_gfn + i);
		if (kvm_is_error_hva(hva)) {
			r = -EFAULT;
			break;
		}

		pgstev = bits[i];
		pgstev = pgstev << 24;
		mask &= _PGSTE_GPS_USAGE_MASK | _PGSTE_GPS_NODAT;
		set_pgste_bits(kvm->mm, hva, mask, pgstev);
	}
	srcu_read_unlock(&kvm->srcu, srcu_idx);
	up_read(&kvm->mm->mmap_sem);

	if (!kvm->mm->context.use_cmma) {
		down_write(&kvm->mm->mmap_sem);
		kvm->mm->context.use_cmma = 1;
		up_write(&kvm->mm->mmap_sem);
	}
out:
	vfree(bits);
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
	case KVM_S390_GET_CMMA_BITS: {
		struct kvm_s390_cmma_log args;

		r = -EFAULT;
		if (copy_from_user(&args, argp, sizeof(args)))
			break;
		r = kvm_s390_get_cmma_bits(kvm, &args);
		if (!r) {
			r = copy_to_user(argp, &args, sizeof(args));
			if (r)
				r = -EFAULT;
		}
		break;
	}
	case KVM_S390_SET_CMMA_BITS: {
		struct kvm_s390_cmma_log args;

		r = -EFAULT;
		if (copy_from_user(&args, argp, sizeof(args)))
			break;
		r = kvm_s390_set_cmma_bits(kvm, &args);
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

static u64 kvm_s390_get_initial_cpuid(void)
{
	struct cpuid cpuid;

	get_cpu_id(&cpuid);
	cpuid.version = 0xff;
	return *((u64 *) &cpuid);
}

static void kvm_s390_crypto_init(struct kvm *kvm)
{
	if (!test_kvm_facility(kvm, 76))
		return;

	kvm->arch.crypto.crycb = &kvm->arch.sie_page2->crycb;
	kvm_s390_set_crycb_format(kvm);

	/* Enable AES/DEA protected key functions by default */
	kvm->arch.crypto.aes_kw = 1;
	kvm->arch.crypto.dea_kw = 1;
	get_random_bytes(kvm->arch.crypto.crycb->aes_wrapping_key_mask,
			 sizeof(kvm->arch.crypto.crycb->aes_wrapping_key_mask));
	get_random_bytes(kvm->arch.crypto.crycb->dea_wrapping_key_mask,
			 sizeof(kvm->arch.crypto.crycb->dea_wrapping_key_mask));
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
	gfp_t alloc_flags = GFP_KERNEL;
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
	if (!sclp.has_64bscao)
		alloc_flags |= GFP_DMA;
	rwlock_init(&kvm->arch.sca_lock);
	kvm->arch.sca = (struct bsca_block *) get_zeroed_page(alloc_flags);
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

	kvm->arch.sie_page2 =
	     (struct sie_page2 *) get_zeroed_page(GFP_KERNEL | GFP_DMA);
	if (!kvm->arch.sie_page2)
		goto out_err;

	/* Populate the facility mask initially. */
	memcpy(kvm->arch.model.fac_mask, S390_lowcore.stfle_fac_list,
	       sizeof(S390_lowcore.stfle_fac_list));
	for (i = 0; i < S390_ARCH_FAC_LIST_SIZE_U64; i++) {
		if (i < kvm_s390_fac_list_mask_size())
			kvm->arch.model.fac_mask[i] &= kvm_s390_fac_list_mask[i];
		else
			kvm->arch.model.fac_mask[i] = 0UL;
	}

	/* Populate the facility list initially. */
	kvm->arch.model.fac_list = kvm->arch.sie_page2->fac_list;
	memcpy(kvm->arch.model.fac_list, kvm->arch.model.fac_mask,
	       S390_ARCH_FAC_LIST_SIZE_BYTE);

	/* we are always in czam mode - even on pre z14 machines */
	set_kvm_facility(kvm->arch.model.fac_mask, 138);
	set_kvm_facility(kvm->arch.model.fac_list, 138);
	/* we emulate STHYI in kvm */
	set_kvm_facility(kvm->arch.model.fac_mask, 74);
	set_kvm_facility(kvm->arch.model.fac_list, 74);
	if (MACHINE_HAS_TLB_GUEST) {
		set_kvm_facility(kvm->arch.model.fac_mask, 147);
		set_kvm_facility(kvm->arch.model.fac_list, 147);
	}

	kvm->arch.model.cpuid = kvm_s390_get_initial_cpuid();
	kvm->arch.model.ibc = sclp.ibc & 0x0fff;

	kvm_s390_crypto_init(kvm);

	mutex_init(&kvm->arch.float_int.ais_lock);
	kvm->arch.float_int.simm = 0;
	kvm->arch.float_int.nimm = 0;
	spin_lock_init(&kvm->arch.float_int.lock);
	for (i = 0; i < FIRQ_LIST_COUNT; i++)
		INIT_LIST_HEAD(&kvm->arch.float_int.lists[i]);
	init_waitqueue_head(&kvm->arch.ipte_wq);
	mutex_init(&kvm->arch.ipte_mutex);

	debug_register_view(kvm->arch.dbf, &debug_sprintf_view);
	VM_EVENT(kvm, 3, "vm created with type %lu", type);

	if (type & KVM_VM_S390_UCONTROL) {
		kvm->arch.gmap = NULL;
		kvm->arch.mem_limit = KVM_S390_NO_MEM_LIMIT;
	} else {
		if (sclp.hamax == U64_MAX)
			kvm->arch.mem_limit = TASK_SIZE_MAX;
		else
			kvm->arch.mem_limit = min_t(unsigned long, TASK_SIZE_MAX,
						    sclp.hamax + 1);
		kvm->arch.gmap = gmap_create(current->mm, kvm->arch.mem_limit - 1);
		if (!kvm->arch.gmap)
			goto out_err;
		kvm->arch.gmap->private = kvm;
		kvm->arch.gmap->pfault_enabled = 0;
	}

	kvm->arch.css_support = 0;
	kvm->arch.use_irqchip = 0;
	kvm->arch.epoch = 0;

	spin_lock_init(&kvm->arch.start_stop_lock);
	kvm_s390_vsie_init(kvm);
	KVM_EVENT(3, "vm 0x%pK created by pid %u", kvm, current->pid);

	return 0;
out_err:
	free_page((unsigned long)kvm->arch.sie_page2);
	debug_unregister(kvm->arch.dbf);
	sca_dispose(kvm);
	KVM_EVENT(3, "creation of vm failed: %d", rc);
	return rc;
}

bool kvm_arch_has_vcpu_debugfs(void)
{
	return false;
}

int kvm_arch_create_vcpu_debugfs(struct kvm_vcpu *vcpu)
{
	return 0;
}

void kvm_arch_vcpu_destroy(struct kvm_vcpu *vcpu)
{
	VCPU_EVENT(vcpu, 3, "%s", "free cpu");
	trace_kvm_s390_destroy_vcpu(vcpu->vcpu_id);
	kvm_s390_clear_local_irqs(vcpu);
	kvm_clear_async_pf_completion_queue(vcpu);
	if (!kvm_is_ucontrol(vcpu->kvm))
		sca_del_vcpu(vcpu);

	if (kvm_is_ucontrol(vcpu->kvm))
		gmap_remove(vcpu->arch.gmap);

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
	sca_dispose(kvm);
	debug_unregister(kvm->arch.dbf);
	free_page((unsigned long)kvm->arch.sie_page2);
	if (!kvm_is_ucontrol(kvm))
		gmap_remove(kvm->arch.gmap);
	kvm_s390_destroy_adapters(kvm);
	kvm_s390_clear_float_irqs(kvm);
	kvm_s390_vsie_destroy(kvm);
	if (kvm->arch.migration_state) {
		vfree(kvm->arch.migration_state->pgste_bitmap);
		kfree(kvm->arch.migration_state);
	}
	KVM_EVENT(3, "vm 0x%pK destroyed", kvm);
}

/* Section: vcpu related */
static int __kvm_ucontrol_vcpu_init(struct kvm_vcpu *vcpu)
{
	vcpu->arch.gmap = gmap_create(current->mm, -1UL);
	if (!vcpu->arch.gmap)
		return -ENOMEM;
	vcpu->arch.gmap->private = vcpu->kvm;

	return 0;
}

static void sca_del_vcpu(struct kvm_vcpu *vcpu)
{
	if (!kvm_s390_use_sca_entries())
		return;
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
	if (!kvm_s390_use_sca_entries()) {
		struct bsca_block *sca = vcpu->kvm->arch.sca;

		/* we still need the basic sca for the ipte control */
		vcpu->arch.sie_block->scaoh = (__u32)(((__u64)sca) >> 32);
		vcpu->arch.sie_block->scaol = (__u32)(__u64)sca;
	}
	read_lock(&vcpu->kvm->arch.sca_lock);
	if (vcpu->kvm->arch.use_esca) {
		struct esca_block *sca = vcpu->kvm->arch.sca;

		sca->cpu[vcpu->vcpu_id].sda = (__u64) vcpu->arch.sie_block;
		vcpu->arch.sie_block->scaoh = (__u32)(((__u64)sca) >> 32);
		vcpu->arch.sie_block->scaol = (__u32)(__u64)sca & ~0x3fU;
		vcpu->arch.sie_block->ecb2 |= ECB2_ESCA;
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
		vcpu->arch.sie_block->ecb2 |= ECB2_ESCA;
	}
	kvm->arch.sca = new_sca;
	kvm->arch.use_esca = 1;

	write_unlock(&kvm->arch.sca_lock);
	kvm_s390_vcpu_unblock_all(kvm);

	free_page((unsigned long)old_sca);

	VM_EVENT(kvm, 2, "Switched to ESCA (0x%pK -> 0x%pK)",
		 old_sca, kvm->arch.sca);
	return 0;
}

static int sca_can_add_vcpu(struct kvm *kvm, unsigned int id)
{
	int rc;

	if (!kvm_s390_use_sca_entries()) {
		if (id < KVM_MAX_VCPUS)
			return true;
		return false;
	}
	if (id < KVM_S390_BSCA_CPU_SLOTS)
		return true;
	if (!sclp.has_esca || !sclp.has_64bscao)
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
	kvm_s390_set_prefix(vcpu, 0);
	if (test_kvm_facility(vcpu->kvm, 64))
		vcpu->run->kvm_valid_regs |= KVM_SYNC_RICCB;
	if (test_kvm_facility(vcpu->kvm, 133))
		vcpu->run->kvm_valid_regs |= KVM_SYNC_GSCB;
	/* fprs can be synchronized via vrs, even if the guest has no vx. With
	 * MACHINE_HAS_VX, (load|store)_fpu_regs() will work with vrs format.
	 */
	if (MACHINE_HAS_VX)
		vcpu->run->kvm_valid_regs |= KVM_SYNC_VRS;
	else
		vcpu->run->kvm_valid_regs |= KVM_SYNC_FPRS;

	if (kvm_is_ucontrol(vcpu->kvm))
		return __kvm_ucontrol_vcpu_init(vcpu);

	return 0;
}

/* needs disabled preemption to protect from TOD sync and vcpu_load/put */
static void __start_cpu_timer_accounting(struct kvm_vcpu *vcpu)
{
	WARN_ON_ONCE(vcpu->arch.cputm_start != 0);
	raw_write_seqcount_begin(&vcpu->arch.cputm_seqcount);
	vcpu->arch.cputm_start = get_tod_clock_fast();
	raw_write_seqcount_end(&vcpu->arch.cputm_seqcount);
}

/* needs disabled preemption to protect from TOD sync and vcpu_load/put */
static void __stop_cpu_timer_accounting(struct kvm_vcpu *vcpu)
{
	WARN_ON_ONCE(vcpu->arch.cputm_start == 0);
	raw_write_seqcount_begin(&vcpu->arch.cputm_seqcount);
	vcpu->arch.sie_block->cputm -= get_tod_clock_fast() - vcpu->arch.cputm_start;
	vcpu->arch.cputm_start = 0;
	raw_write_seqcount_end(&vcpu->arch.cputm_seqcount);
}

/* needs disabled preemption to protect from TOD sync and vcpu_load/put */
static void __enable_cpu_timer_accounting(struct kvm_vcpu *vcpu)
{
	WARN_ON_ONCE(vcpu->arch.cputm_enabled);
	vcpu->arch.cputm_enabled = true;
	__start_cpu_timer_accounting(vcpu);
}

/* needs disabled preemption to protect from TOD sync and vcpu_load/put */
static void __disable_cpu_timer_accounting(struct kvm_vcpu *vcpu)
{
	WARN_ON_ONCE(!vcpu->arch.cputm_enabled);
	__stop_cpu_timer_accounting(vcpu);
	vcpu->arch.cputm_enabled = false;
}

static void enable_cpu_timer_accounting(struct kvm_vcpu *vcpu)
{
	preempt_disable(); /* protect from TOD sync and vcpu_load/put */
	__enable_cpu_timer_accounting(vcpu);
	preempt_enable();
}

static void disable_cpu_timer_accounting(struct kvm_vcpu *vcpu)
{
	preempt_disable(); /* protect from TOD sync and vcpu_load/put */
	__disable_cpu_timer_accounting(vcpu);
	preempt_enable();
}

/* set the cpu timer - may only be called from the VCPU thread itself */
void kvm_s390_set_cpu_timer(struct kvm_vcpu *vcpu, __u64 cputm)
{
	preempt_disable(); /* protect from TOD sync and vcpu_load/put */
	raw_write_seqcount_begin(&vcpu->arch.cputm_seqcount);
	if (vcpu->arch.cputm_enabled)
		vcpu->arch.cputm_start = get_tod_clock_fast();
	vcpu->arch.sie_block->cputm = cputm;
	raw_write_seqcount_end(&vcpu->arch.cputm_seqcount);
	preempt_enable();
}

/* update and get the cpu timer - can also be called from other VCPU threads */
__u64 kvm_s390_get_cpu_timer(struct kvm_vcpu *vcpu)
{
	unsigned int seq;
	__u64 value;

	if (unlikely(!vcpu->arch.cputm_enabled))
		return vcpu->arch.sie_block->cputm;

	preempt_disable(); /* protect from TOD sync and vcpu_load/put */
	do {
		seq = raw_read_seqcount(&vcpu->arch.cputm_seqcount);
		/*
		 * If the writer would ever execute a read in the critical
		 * section, e.g. in irq context, we have a deadlock.
		 */
		WARN_ON_ONCE((seq & 1) && smp_processor_id() == vcpu->cpu);
		value = vcpu->arch.sie_block->cputm;
		/* if cputm_start is 0, accounting is being started/stopped */
		if (likely(vcpu->arch.cputm_start))
			value -= get_tod_clock_fast() - vcpu->arch.cputm_start;
	} while (read_seqcount_retry(&vcpu->arch.cputm_seqcount, seq & ~1));
	preempt_enable();
	return value;
}

void kvm_arch_vcpu_load(struct kvm_vcpu *vcpu, int cpu)
{

	gmap_enable(vcpu->arch.enabled_gmap);
	atomic_or(CPUSTAT_RUNNING, &vcpu->arch.sie_block->cpuflags);
	if (vcpu->arch.cputm_enabled && !is_vcpu_idle(vcpu))
		__start_cpu_timer_accounting(vcpu);
	vcpu->cpu = cpu;
}

void kvm_arch_vcpu_put(struct kvm_vcpu *vcpu)
{
	vcpu->cpu = -1;
	if (vcpu->arch.cputm_enabled && !is_vcpu_idle(vcpu))
		__stop_cpu_timer_accounting(vcpu);
	atomic_andnot(CPUSTAT_RUNNING, &vcpu->arch.sie_block->cpuflags);
	vcpu->arch.enabled_gmap = gmap_get_enabled();
	gmap_disable(vcpu->arch.enabled_gmap);

}

static void kvm_s390_vcpu_initial_reset(struct kvm_vcpu *vcpu)
{
	/* this equals initial cpu reset in pop, but we don't switch to ESA */
	vcpu->arch.sie_block->gpsw.mask = 0UL;
	vcpu->arch.sie_block->gpsw.addr = 0UL;
	kvm_s390_set_prefix(vcpu, 0);
	kvm_s390_set_cpu_timer(vcpu, 0);
	vcpu->arch.sie_block->ckc       = 0UL;
	vcpu->arch.sie_block->todpr     = 0;
	memset(vcpu->arch.sie_block->gcr, 0, 16 * sizeof(__u64));
	vcpu->arch.sie_block->gcr[0]  = 0xE0UL;
	vcpu->arch.sie_block->gcr[14] = 0xC2000000UL;
	/* make sure the new fpc will be lazily loaded */
	save_fpu_regs();
	current->thread.fpu.fpc = 0;
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
	if (test_kvm_facility(vcpu->kvm, 74) || vcpu->kvm->arch.user_instr0)
		vcpu->arch.sie_block->ictl |= ICTL_OPEREXC;
	/* make vcpu_load load the right gmap on the first trigger */
	vcpu->arch.enabled_gmap = vcpu->arch.gmap;
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

	vcpu->arch.sie_block->ecb2 &= ~ECB2_PFMFI;
	return 0;
}

static void kvm_s390_vcpu_setup_model(struct kvm_vcpu *vcpu)
{
	struct kvm_s390_cpu_model *model = &vcpu->kvm->arch.model;

	vcpu->arch.sie_block->ibc = model->ibc;
	if (test_kvm_facility(vcpu->kvm, 7))
		vcpu->arch.sie_block->fac = (u32)(u64) model->fac_list;
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

	/* pgste_set_pte has special handling for !MACHINE_HAS_ESOP */
	if (MACHINE_HAS_ESOP)
		vcpu->arch.sie_block->ecb |= ECB_HOSTPROTINT;
	if (test_kvm_facility(vcpu->kvm, 9))
		vcpu->arch.sie_block->ecb |= ECB_SRSI;
	if (test_kvm_facility(vcpu->kvm, 73))
		vcpu->arch.sie_block->ecb |= ECB_TE;

	if (test_kvm_facility(vcpu->kvm, 8) && sclp.has_pfmfi)
		vcpu->arch.sie_block->ecb2 |= ECB2_PFMFI;
	if (test_kvm_facility(vcpu->kvm, 130))
		vcpu->arch.sie_block->ecb2 |= ECB2_IEP;
	vcpu->arch.sie_block->eca = ECA_MVPGI | ECA_PROTEXCI;
	if (sclp.has_cei)
		vcpu->arch.sie_block->eca |= ECA_CEI;
	if (sclp.has_ib)
		vcpu->arch.sie_block->eca |= ECA_IB;
	if (sclp.has_siif)
		vcpu->arch.sie_block->eca |= ECA_SII;
	if (sclp.has_sigpif)
		vcpu->arch.sie_block->eca |= ECA_SIGPI;
	if (test_kvm_facility(vcpu->kvm, 129)) {
		vcpu->arch.sie_block->eca |= ECA_VX;
		vcpu->arch.sie_block->ecd |= ECD_HOSTREGMGMT;
	}
	if (test_kvm_facility(vcpu->kvm, 139))
		vcpu->arch.sie_block->ecd |= ECD_MEF;

	vcpu->arch.sie_block->sdnxo = ((unsigned long) &vcpu->run->s.regs.sdnx)
					| SDNXC;
	vcpu->arch.sie_block->riccbd = (unsigned long) &vcpu->run->s.regs.riccb;

	if (sclp.has_kss)
		atomic_or(CPUSTAT_KSS, &vcpu->arch.sie_block->cpuflags);
	else
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

	BUILD_BUG_ON(sizeof(struct sie_page) != 4096);
	sie_page = (struct sie_page *) get_zeroed_page(GFP_KERNEL);
	if (!sie_page)
		goto out_free_cpu;

	vcpu->arch.sie_block = &sie_page->sie_block;
	vcpu->arch.sie_block->itdba = (unsigned long) &sie_page->itdb;

	/* the real guest size will always be smaller than msl */
	vcpu->arch.sie_block->mso = 0;
	vcpu->arch.sie_block->msl = sclp.hamax;

	vcpu->arch.sie_block->icpua = id;
	spin_lock_init(&vcpu->arch.local_int.lock);
	vcpu->arch.local_int.float_int = &kvm->arch.float_int;
	vcpu->arch.local_int.wq = &vcpu->wq;
	vcpu->arch.local_int.cpuflags = &vcpu->arch.sie_block->cpuflags;
	seqcount_init(&vcpu->arch.cputm_seqcount);

	rc = kvm_vcpu_init(vcpu, kvm, id);
	if (rc)
		goto out_free_sie_block;
	VM_EVENT(kvm, 3, "create cpu %d at 0x%pK, sie block at 0x%pK", id, vcpu,
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

bool kvm_arch_vcpu_in_kernel(struct kvm_vcpu *vcpu)
{
	return !(vcpu->arch.sie_block->gpsw.mask & PSW_MASK_PSTATE);
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

static void kvm_gmap_notifier(struct gmap *gmap, unsigned long start,
			      unsigned long end)
{
	struct kvm *kvm = gmap->private;
	struct kvm_vcpu *vcpu;
	unsigned long prefix;
	int i;

	if (gmap_is_shadow(gmap))
		return;
	if (start >= 1UL << 31)
		/* We are only interested in prefix pages */
		return;
	kvm_for_each_vcpu(i, vcpu, kvm) {
		/* match against both prefix pages */
		prefix = kvm_s390_get_prefix(vcpu);
		if (prefix <= end && start <= prefix + 2*PAGE_SIZE - 1) {
			VCPU_EVENT(vcpu, 2, "gmap notifier for %lx-%lx",
				   start, end);
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
		r = put_user(kvm_s390_get_cpu_timer(vcpu),
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
	__u64 val;

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
		r = get_user(val, (u64 __user *)reg->addr);
		if (!r)
			kvm_s390_set_cpu_timer(vcpu, val);
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
	vcpu->run->s.regs.fpc = fpu->fpc;
	if (MACHINE_HAS_VX)
		convert_fp_to_vx((__vector128 *) vcpu->run->s.regs.vrs,
				 (freg_t *) fpu->fprs);
	else
		memcpy(vcpu->run->s.regs.fprs, &fpu->fprs, sizeof(fpu->fprs));
	return 0;
}

int kvm_arch_vcpu_ioctl_get_fpu(struct kvm_vcpu *vcpu, struct kvm_fpu *fpu)
{
	/* make sure we have the latest values */
	save_fpu_regs();
	if (MACHINE_HAS_VX)
		convert_vx_to_fp((freg_t *) fpu->fprs,
				 (__vector128 *) vcpu->run->s.regs.vrs);
	else
		memcpy(fpu->fprs, vcpu->run->s.regs.fprs, sizeof(fpu->fprs));
	fpu->fpc = vcpu->run->s.regs.fpc;
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
	if (!sclp.has_gpere)
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
	if (!kvm_request_pending(vcpu))
		return 0;
	/*
	 * We use MMU_RELOAD just to re-arm the ipte notifier for the
	 * guest prefix page. gmap_mprotect_notify will wait on the ptl lock.
	 * This ensures that the ipte instruction for this request has
	 * already finished. We might race against a second unmapper that
	 * wants to set the blocking bit. Lets just retry the request loop.
	 */
	if (kvm_check_request(KVM_REQ_MMU_RELOAD, vcpu)) {
		int rc;
		rc = gmap_mprotect_notify(vcpu->arch.gmap,
					  kvm_s390_get_prefix(vcpu),
					  PAGE_SIZE * 2, PROT_WRITE);
		if (rc) {
			kvm_make_request(KVM_REQ_MMU_RELOAD, vcpu);
			return rc;
		}
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

	if (kvm_check_request(KVM_REQ_ICPT_OPEREXC, vcpu)) {
		vcpu->arch.sie_block->ictl |= ICTL_OPEREXC;
		goto retry;
	}

	if (kvm_check_request(KVM_REQ_START_MIGRATION, vcpu)) {
		/*
		 * Disable CMMA virtualization; we will emulate the ESSA
		 * instruction manually, in order to provide additional
		 * functionalities needed for live migration.
		 */
		vcpu->arch.sie_block->ecb2 &= ~ECB2_CMMA;
		goto retry;
	}

	if (kvm_check_request(KVM_REQ_STOP_MIGRATION, vcpu)) {
		/*
		 * Re-enable CMMA virtualization if CMMA is available and
		 * was used.
		 */
		if ((vcpu->kvm->arch.use_cmma) &&
		    (vcpu->kvm->mm->context.use_cmma))
			vcpu->arch.sie_block->ecb2 |= ECB2_CMMA;
		goto retry;
	}

	/* nothing to do, just clear the request */
	kvm_clear_request(KVM_REQ_UNHALT, vcpu);

	return 0;
}

void kvm_s390_set_tod_clock_ext(struct kvm *kvm,
				 const struct kvm_s390_vm_tod_clock *gtod)
{
	struct kvm_vcpu *vcpu;
	struct kvm_s390_tod_clock_ext htod;
	int i;

	mutex_lock(&kvm->lock);
	preempt_disable();

	get_tod_clock_ext((char *)&htod);

	kvm->arch.epoch = gtod->tod - htod.tod;
	kvm->arch.epdx = gtod->epoch_idx - htod.epoch_idx;

	if (kvm->arch.epoch > gtod->tod)
		kvm->arch.epdx -= 1;

	kvm_s390_vcpu_block_all(kvm);
	kvm_for_each_vcpu(i, vcpu, kvm) {
		vcpu->arch.sie_block->epoch = kvm->arch.epoch;
		vcpu->arch.sie_block->epdx  = kvm->arch.epdx;
	}

	kvm_s390_vcpu_unblock_all(kvm);
	preempt_enable();
	mutex_unlock(&kvm->lock);
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

	vcpu->arch.sie_block->gg14 = vcpu->run->s.regs.gprs[14];
	vcpu->arch.sie_block->gg15 = vcpu->run->s.regs.gprs[15];

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
	struct kvm_s390_pgm_info pgm_info = {
		.code = PGM_ADDRESSING,
	};
	u8 opcode, ilen;
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
	rc = read_guest_instr(vcpu, vcpu->arch.sie_block->gpsw.addr, &opcode, 1);
	ilen = insn_length(opcode);
	if (rc < 0) {
		return rc;
	} else if (rc) {
		/* Instruction-Fetching Exceptions - we can't detect the ilen.
		 * Forward by arbitrary ilc, injection will take care of
		 * nullification if necessary.
		 */
		pgm_info = vcpu->arch.pgm;
		ilen = 4;
	}
	pgm_info.flags = ilen | KVM_S390_PGM_FLAGS_ILC_VALID;
	kvm_s390_forward_psw(vcpu, ilen);
	return kvm_s390_inject_prog_irq(vcpu, &pgm_info);
}

static int vcpu_post_run(struct kvm_vcpu *vcpu, int exit_reason)
{
	struct mcck_volatile_info *mcck_info;
	struct sie_page *sie_page;

	VCPU_EVENT(vcpu, 6, "exit sie icptcode %d",
		   vcpu->arch.sie_block->icptcode);
	trace_kvm_s390_sie_exit(vcpu, vcpu->arch.sie_block->icptcode);

	if (guestdbg_enabled(vcpu))
		kvm_s390_restore_guest_per_regs(vcpu);

	vcpu->run->s.regs.gprs[14] = vcpu->arch.sie_block->gg14;
	vcpu->run->s.regs.gprs[15] = vcpu->arch.sie_block->gg15;

	if (exit_reason == -EINTR) {
		VCPU_EVENT(vcpu, 3, "%s", "machine check");
		sie_page = container_of(vcpu->arch.sie_block,
					struct sie_page, sie_block);
		mcck_info = &sie_page->mcck_info;
		kvm_s390_reinject_machine_check(vcpu, mcck_info);
		return 0;
	}

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
		guest_enter_irqoff();
		__disable_cpu_timer_accounting(vcpu);
		local_irq_enable();
		exit_reason = sie64a(vcpu->arch.sie_block,
				     vcpu->run->s.regs.gprs);
		local_irq_disable();
		__enable_cpu_timer_accounting(vcpu);
		guest_exit_irqoff();
		local_irq_enable();
		vcpu->srcu_idx = srcu_read_lock(&vcpu->kvm->srcu);

		rc = vcpu_post_run(vcpu, exit_reason);
	} while (!signal_pending(current) && !guestdbg_exit_pending(vcpu) && !rc);

	srcu_read_unlock(&vcpu->kvm->srcu, vcpu->srcu_idx);
	return rc;
}

static void sync_regs(struct kvm_vcpu *vcpu, struct kvm_run *kvm_run)
{
	struct runtime_instr_cb *riccb;
	struct gs_cb *gscb;

	riccb = (struct runtime_instr_cb *) &kvm_run->s.regs.riccb;
	gscb = (struct gs_cb *) &kvm_run->s.regs.gscb;
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
		kvm_s390_set_cpu_timer(vcpu, kvm_run->s.regs.cputm);
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
	/*
	 * If userspace sets the riccb (e.g. after migration) to a valid state,
	 * we should enable RI here instead of doing the lazy enablement.
	 */
	if ((kvm_run->kvm_dirty_regs & KVM_SYNC_RICCB) &&
	    test_kvm_facility(vcpu->kvm, 64) &&
	    riccb->v &&
	    !(vcpu->arch.sie_block->ecb3 & ECB3_RI)) {
		VCPU_EVENT(vcpu, 3, "%s", "ENABLE: RI (sync_regs)");
		vcpu->arch.sie_block->ecb3 |= ECB3_RI;
	}
	/*
	 * If userspace sets the gscb (e.g. after migration) to non-zero,
	 * we should enable GS here instead of doing the lazy enablement.
	 */
	if ((kvm_run->kvm_dirty_regs & KVM_SYNC_GSCB) &&
	    test_kvm_facility(vcpu->kvm, 133) &&
	    gscb->gssm &&
	    !vcpu->arch.gs_enabled) {
		VCPU_EVENT(vcpu, 3, "%s", "ENABLE: GS (sync_regs)");
		vcpu->arch.sie_block->ecb |= ECB_GS;
		vcpu->arch.sie_block->ecd |= ECD_HOSTREGMGMT;
		vcpu->arch.gs_enabled = 1;
	}
	save_access_regs(vcpu->arch.host_acrs);
	restore_access_regs(vcpu->run->s.regs.acrs);
	/* save host (userspace) fprs/vrs */
	save_fpu_regs();
	vcpu->arch.host_fpregs.fpc = current->thread.fpu.fpc;
	vcpu->arch.host_fpregs.regs = current->thread.fpu.regs;
	if (MACHINE_HAS_VX)
		current->thread.fpu.regs = vcpu->run->s.regs.vrs;
	else
		current->thread.fpu.regs = vcpu->run->s.regs.fprs;
	current->thread.fpu.fpc = vcpu->run->s.regs.fpc;
	if (test_fp_ctl(current->thread.fpu.fpc))
		/* User space provided an invalid FPC, let's clear it */
		current->thread.fpu.fpc = 0;
	if (MACHINE_HAS_GS) {
		preempt_disable();
		__ctl_set_bit(2, 4);
		if (current->thread.gs_cb) {
			vcpu->arch.host_gscb = current->thread.gs_cb;
			save_gs_cb(vcpu->arch.host_gscb);
		}
		if (vcpu->arch.gs_enabled) {
			current->thread.gs_cb = (struct gs_cb *)
						&vcpu->run->s.regs.gscb;
			restore_gs_cb(current->thread.gs_cb);
		}
		preempt_enable();
	}

	kvm_run->kvm_dirty_regs = 0;
}

static void store_regs(struct kvm_vcpu *vcpu, struct kvm_run *kvm_run)
{
	kvm_run->psw_mask = vcpu->arch.sie_block->gpsw.mask;
	kvm_run->psw_addr = vcpu->arch.sie_block->gpsw.addr;
	kvm_run->s.regs.prefix = kvm_s390_get_prefix(vcpu);
	memcpy(&kvm_run->s.regs.crs, &vcpu->arch.sie_block->gcr, 128);
	kvm_run->s.regs.cputm = kvm_s390_get_cpu_timer(vcpu);
	kvm_run->s.regs.ckc = vcpu->arch.sie_block->ckc;
	kvm_run->s.regs.todpr = vcpu->arch.sie_block->todpr;
	kvm_run->s.regs.pp = vcpu->arch.sie_block->pp;
	kvm_run->s.regs.gbea = vcpu->arch.sie_block->gbea;
	kvm_run->s.regs.pft = vcpu->arch.pfault_token;
	kvm_run->s.regs.pfs = vcpu->arch.pfault_select;
	kvm_run->s.regs.pfc = vcpu->arch.pfault_compare;
	save_access_regs(vcpu->run->s.regs.acrs);
	restore_access_regs(vcpu->arch.host_acrs);
	/* Save guest register state */
	save_fpu_regs();
	vcpu->run->s.regs.fpc = current->thread.fpu.fpc;
	/* Restore will be done lazily at return */
	current->thread.fpu.fpc = vcpu->arch.host_fpregs.fpc;
	current->thread.fpu.regs = vcpu->arch.host_fpregs.regs;
	if (MACHINE_HAS_GS) {
		__ctl_set_bit(2, 4);
		if (vcpu->arch.gs_enabled)
			save_gs_cb(current->thread.gs_cb);
		preempt_disable();
		current->thread.gs_cb = vcpu->arch.host_gscb;
		restore_gs_cb(vcpu->arch.host_gscb);
		preempt_enable();
		if (!vcpu->arch.host_gscb)
			__ctl_clear_bit(2, 4);
		vcpu->arch.host_gscb = NULL;
	}

}

int kvm_arch_vcpu_ioctl_run(struct kvm_vcpu *vcpu, struct kvm_run *kvm_run)
{
	int rc;

	if (kvm_run->immediate_exit)
		return -EINTR;

	if (guestdbg_exit_pending(vcpu)) {
		kvm_s390_prepare_debug_exit(vcpu);
		return 0;
	}

	kvm_sigset_activate(vcpu);

	if (!kvm_s390_user_cpu_state_ctrl(vcpu->kvm)) {
		kvm_s390_vcpu_start(vcpu);
	} else if (is_vcpu_stopped(vcpu)) {
		pr_err_ratelimited("can't run stopped vcpu %d\n",
				   vcpu->vcpu_id);
		return -EINVAL;
	}

	sync_regs(vcpu, kvm_run);
	enable_cpu_timer_accounting(vcpu);

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

	disable_cpu_timer_accounting(vcpu);
	store_regs(vcpu, kvm_run);

	kvm_sigset_deactivate(vcpu);

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
	freg_t fprs[NUM_FPRS];
	unsigned int px;
	u64 clkcomp, cputm;
	int rc;

	px = kvm_s390_get_prefix(vcpu);
	if (gpa == KVM_S390_STORE_STATUS_NOADDR) {
		if (write_guest_abs(vcpu, 163, &archmode, 1))
			return -EFAULT;
		gpa = 0;
	} else if (gpa == KVM_S390_STORE_STATUS_PREFIXED) {
		if (write_guest_real(vcpu, 163, &archmode, 1))
			return -EFAULT;
		gpa = px;
	} else
		gpa -= __LC_FPREGS_SAVE_AREA;

	/* manually convert vector registers if necessary */
	if (MACHINE_HAS_VX) {
		convert_vx_to_fp(fprs, (__vector128 *) vcpu->run->s.regs.vrs);
		rc = write_guest_abs(vcpu, gpa + __LC_FPREGS_SAVE_AREA,
				     fprs, 128);
	} else {
		rc = write_guest_abs(vcpu, gpa + __LC_FPREGS_SAVE_AREA,
				     vcpu->run->s.regs.fprs, 128);
	}
	rc |= write_guest_abs(vcpu, gpa + __LC_GPREGS_SAVE_AREA,
			      vcpu->run->s.regs.gprs, 128);
	rc |= write_guest_abs(vcpu, gpa + __LC_PSW_SAVE_AREA,
			      &vcpu->arch.sie_block->gpsw, 16);
	rc |= write_guest_abs(vcpu, gpa + __LC_PREFIX_SAVE_AREA,
			      &px, 4);
	rc |= write_guest_abs(vcpu, gpa + __LC_FP_CREG_SAVE_AREA,
			      &vcpu->run->s.regs.fpc, 4);
	rc |= write_guest_abs(vcpu, gpa + __LC_TOD_PROGREG_SAVE_AREA,
			      &vcpu->arch.sie_block->todpr, 4);
	cputm = kvm_s390_get_cpu_timer(vcpu);
	rc |= write_guest_abs(vcpu, gpa + __LC_CPU_TIMER_SAVE_AREA,
			      &cputm, 8);
	clkcomp = vcpu->arch.sie_block->ckc >> 8;
	rc |= write_guest_abs(vcpu, gpa + __LC_CLOCK_COMP_SAVE_AREA,
			      &clkcomp, 8);
	rc |= write_guest_abs(vcpu, gpa + __LC_AREGS_SAVE_AREA,
			      &vcpu->run->s.regs.acrs, 64);
	rc |= write_guest_abs(vcpu, gpa + __LC_CREGS_SAVE_AREA,
			      &vcpu->arch.sie_block->gcr, 128);
	return rc ? -EFAULT : 0;
}

int kvm_s390_vcpu_store_status(struct kvm_vcpu *vcpu, unsigned long addr)
{
	/*
	 * The guest FPRS and ACRS are in the host FPRS/ACRS due to the lazy
	 * switch in the run ioctl. Let's update our copies before we save
	 * it into the save area
	 */
	save_fpu_regs();
	vcpu->run->s.regs.fpc = current->thread.fpu.fpc;
	save_access_regs(vcpu->run->s.regs.acrs);

	return kvm_s390_store_status_unloaded(vcpu, addr);
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
	if (!sclp.has_ibs)
		return;
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
			r = check_gva_range(vcpu, mop->gaddr, mop->ar,
					    mop->size, GACC_FETCH);
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
			r = check_gva_range(vcpu, mop->gaddr, mop->ar,
					    mop->size, GACC_STORE);
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
		/* do not use irq_state.flags, it will break old QEMUs */
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
		/* do not use irq_state.flags, it will break old QEMUs */
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

	if (mem->guest_phys_addr + mem->memory_size > kvm->arch.mem_limit)
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

static inline unsigned long nonhyp_mask(int i)
{
	unsigned int nonhyp_fai = (sclp.hmfai << i * 2) >> 30;

	return 0x0000ffffffffffffUL >> (nonhyp_fai << 4);
}

void kvm_arch_vcpu_block_finish(struct kvm_vcpu *vcpu)
{
	vcpu->valid_wakeup = false;
}

static int __init kvm_s390_init(void)
{
	int i;

	if (!sclp.has_sief2) {
		pr_info("SIE not available\n");
		return -ENODEV;
	}

	for (i = 0; i < 16; i++)
		kvm_s390_fac_list_mask[i] |=
			S390_lowcore.stfle_fac_list[i] & nonhyp_mask(i);

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
