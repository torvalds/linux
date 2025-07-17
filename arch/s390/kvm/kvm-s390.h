/* SPDX-License-Identifier: GPL-2.0 */
/*
 * definition for kvm on s390
 *
 * Copyright IBM Corp. 2008, 2020
 *
 *    Author(s): Carsten Otte <cotte@de.ibm.com>
 *               Christian Borntraeger <borntraeger@de.ibm.com>
 *               Christian Ehrhardt <ehrhardt@de.ibm.com>
 */

#ifndef ARCH_S390_KVM_S390_H
#define ARCH_S390_KVM_S390_H

#include <linux/hrtimer.h>
#include <linux/kvm.h>
#include <linux/kvm_host.h>
#include <linux/lockdep.h>
#include <asm/facility.h>
#include <asm/processor.h>
#include <asm/sclp.h>

#define KVM_S390_UCONTROL_MEMSLOT (KVM_USER_MEM_SLOTS + 0)

static inline void kvm_s390_fpu_store(struct kvm_run *run)
{
	fpu_stfpc(&run->s.regs.fpc);
	if (cpu_has_vx())
		save_vx_regs((__vector128 *)&run->s.regs.vrs);
	else
		save_fp_regs((freg_t *)&run->s.regs.fprs);
}

static inline void kvm_s390_fpu_load(struct kvm_run *run)
{
	fpu_lfpc_safe(&run->s.regs.fpc);
	if (cpu_has_vx())
		load_vx_regs((__vector128 *)&run->s.regs.vrs);
	else
		load_fp_regs((freg_t *)&run->s.regs.fprs);
}

/* Transactional Memory Execution related macros */
#define IS_TE_ENABLED(vcpu)	((vcpu->arch.sie_block->ecb & ECB_TE))
#define TDB_FORMAT1		1
#define IS_ITDB_VALID(vcpu) \
	((*(char *)phys_to_virt((vcpu)->arch.sie_block->itdba) == TDB_FORMAT1))

extern debug_info_t *kvm_s390_dbf;
extern debug_info_t *kvm_s390_dbf_uv;

#define KVM_UV_EVENT(d_kvm, d_loglevel, d_string, d_args...)\
do { \
	debug_sprintf_event((d_kvm)->arch.dbf, d_loglevel, d_string "\n", \
	  d_args); \
	debug_sprintf_event(kvm_s390_dbf_uv, d_loglevel, \
			    "%d: " d_string "\n", (d_kvm)->userspace_pid, \
			    d_args); \
} while (0)

#define KVM_EVENT(d_loglevel, d_string, d_args...)\
do { \
	debug_sprintf_event(kvm_s390_dbf, d_loglevel, d_string "\n", \
	  d_args); \
} while (0)

#define VM_EVENT(d_kvm, d_loglevel, d_string, d_args...)\
do { \
	debug_sprintf_event(d_kvm->arch.dbf, d_loglevel, d_string "\n", \
	  d_args); \
} while (0)

#define VCPU_EVENT(d_vcpu, d_loglevel, d_string, d_args...)\
do { \
	debug_sprintf_event(d_vcpu->kvm->arch.dbf, d_loglevel, \
	  "%02d[%016lx-%016lx]: " d_string "\n", d_vcpu->vcpu_id, \
	  d_vcpu->arch.sie_block->gpsw.mask, d_vcpu->arch.sie_block->gpsw.addr,\
	  d_args); \
} while (0)

static inline void kvm_s390_set_cpuflags(struct kvm_vcpu *vcpu, u32 flags)
{
	atomic_or(flags, &vcpu->arch.sie_block->cpuflags);
}

static inline void kvm_s390_clear_cpuflags(struct kvm_vcpu *vcpu, u32 flags)
{
	atomic_andnot(flags, &vcpu->arch.sie_block->cpuflags);
}

static inline bool kvm_s390_test_cpuflags(struct kvm_vcpu *vcpu, u32 flags)
{
	return (atomic_read(&vcpu->arch.sie_block->cpuflags) & flags) == flags;
}

static inline int is_vcpu_stopped(struct kvm_vcpu *vcpu)
{
	return kvm_s390_test_cpuflags(vcpu, CPUSTAT_STOPPED);
}

static inline int is_vcpu_idle(struct kvm_vcpu *vcpu)
{
	return test_bit(vcpu->vcpu_idx, vcpu->kvm->arch.idle_mask);
}

static inline int kvm_is_ucontrol(struct kvm *kvm)
{
#ifdef CONFIG_KVM_S390_UCONTROL
	if (kvm->arch.gmap)
		return 0;
	return 1;
#else
	return 0;
#endif
}

#define GUEST_PREFIX_SHIFT 13
static inline u32 kvm_s390_get_prefix(struct kvm_vcpu *vcpu)
{
	return vcpu->arch.sie_block->prefix << GUEST_PREFIX_SHIFT;
}

static inline void kvm_s390_set_prefix(struct kvm_vcpu *vcpu, u32 prefix)
{
	VCPU_EVENT(vcpu, 3, "set prefix of cpu %03u to 0x%x", vcpu->vcpu_id,
		   prefix);
	vcpu->arch.sie_block->prefix = prefix >> GUEST_PREFIX_SHIFT;
	kvm_make_request(KVM_REQ_TLB_FLUSH, vcpu);
	kvm_make_request(KVM_REQ_REFRESH_GUEST_PREFIX, vcpu);
}

static inline u64 kvm_s390_get_base_disp_s(struct kvm_vcpu *vcpu, u8 *ar)
{
	u32 base2 = vcpu->arch.sie_block->ipb >> 28;
	u32 disp2 = ((vcpu->arch.sie_block->ipb & 0x0fff0000) >> 16);

	if (ar)
		*ar = base2;

	return (base2 ? vcpu->run->s.regs.gprs[base2] : 0) + disp2;
}

static inline u64 kvm_s390_get_base_disp_siy(struct kvm_vcpu *vcpu, u8 *ar)
{
	u32 base1 = vcpu->arch.sie_block->ipb >> 28;
	s64 disp1;

	/* The displacement is a 20bit _SIGNED_ value */
	disp1 = sign_extend64(((vcpu->arch.sie_block->ipb & 0x0fff0000) >> 16) +
			      ((vcpu->arch.sie_block->ipb & 0xff00) << 4), 19);

	if (ar)
		*ar = base1;

	return (base1 ? vcpu->run->s.regs.gprs[base1] : 0) + disp1;
}

static inline void kvm_s390_get_base_disp_sse(struct kvm_vcpu *vcpu,
					      u64 *address1, u64 *address2,
					      u8 *ar_b1, u8 *ar_b2)
{
	u32 base1 = (vcpu->arch.sie_block->ipb & 0xf0000000) >> 28;
	u32 disp1 = (vcpu->arch.sie_block->ipb & 0x0fff0000) >> 16;
	u32 base2 = (vcpu->arch.sie_block->ipb & 0xf000) >> 12;
	u32 disp2 = vcpu->arch.sie_block->ipb & 0x0fff;

	*address1 = (base1 ? vcpu->run->s.regs.gprs[base1] : 0) + disp1;
	*address2 = (base2 ? vcpu->run->s.regs.gprs[base2] : 0) + disp2;

	if (ar_b1)
		*ar_b1 = base1;
	if (ar_b2)
		*ar_b2 = base2;
}

static inline void kvm_s390_get_regs_rre(struct kvm_vcpu *vcpu, int *r1, int *r2)
{
	if (r1)
		*r1 = (vcpu->arch.sie_block->ipb & 0x00f00000) >> 20;
	if (r2)
		*r2 = (vcpu->arch.sie_block->ipb & 0x000f0000) >> 16;
}

static inline u64 kvm_s390_get_base_disp_rsy(struct kvm_vcpu *vcpu, u8 *ar)
{
	u32 base2 = vcpu->arch.sie_block->ipb >> 28;
	u32 disp2 = ((vcpu->arch.sie_block->ipb & 0x0fff0000) >> 16) +
			((vcpu->arch.sie_block->ipb & 0xff00) << 4);
	/* The displacement is a 20bit _SIGNED_ value */
	if (disp2 & 0x80000)
		disp2+=0xfff00000;

	if (ar)
		*ar = base2;

	return (base2 ? vcpu->run->s.regs.gprs[base2] : 0) + (long)(int)disp2;
}

static inline u64 kvm_s390_get_base_disp_rs(struct kvm_vcpu *vcpu, u8 *ar)
{
	u32 base2 = vcpu->arch.sie_block->ipb >> 28;
	u32 disp2 = ((vcpu->arch.sie_block->ipb & 0x0fff0000) >> 16);

	if (ar)
		*ar = base2;

	return (base2 ? vcpu->run->s.regs.gprs[base2] : 0) + disp2;
}

/* Set the condition code in the guest program status word */
static inline void kvm_s390_set_psw_cc(struct kvm_vcpu *vcpu, unsigned long cc)
{
	vcpu->arch.sie_block->gpsw.mask &= ~(3UL << 44);
	vcpu->arch.sie_block->gpsw.mask |= cc << 44;
}

/* test availability of facility in a kvm instance */
static inline int test_kvm_facility(struct kvm *kvm, unsigned long nr)
{
	return __test_facility(nr, kvm->arch.model.fac_mask) &&
		__test_facility(nr, kvm->arch.model.fac_list);
}

static inline int set_kvm_facility(u64 *fac_list, unsigned long nr)
{
	unsigned char *ptr;

	if (nr >= MAX_FACILITY_BIT)
		return -EINVAL;
	ptr = (unsigned char *) fac_list + (nr >> 3);
	*ptr |= (0x80UL >> (nr & 7));
	return 0;
}

static inline int test_kvm_cpu_feat(struct kvm *kvm, unsigned long nr)
{
	WARN_ON_ONCE(nr >= KVM_S390_VM_CPU_FEAT_NR_BITS);
	return test_bit_inv(nr, kvm->arch.cpu_feat);
}

/* are cpu states controlled by user space */
static inline int kvm_s390_user_cpu_state_ctrl(struct kvm *kvm)
{
	return kvm->arch.user_cpu_state_ctrl != 0;
}

static inline void kvm_s390_set_user_cpu_state_ctrl(struct kvm *kvm)
{
	if (kvm->arch.user_cpu_state_ctrl)
		return;

	VM_EVENT(kvm, 3, "%s", "ENABLE: Userspace CPU state control");
	kvm->arch.user_cpu_state_ctrl = 1;
}

/* get the end gfn of the last (highest gfn) memslot */
static inline unsigned long kvm_s390_get_gfn_end(struct kvm_memslots *slots)
{
	struct rb_node *node;
	struct kvm_memory_slot *ms;

	if (WARN_ON(kvm_memslots_empty(slots)))
		return 0;

	node = rb_last(&slots->gfn_tree);
	ms = container_of(node, struct kvm_memory_slot, gfn_node[slots->node_idx]);
	return ms->base_gfn + ms->npages;
}

static inline u32 kvm_s390_get_gisa_desc(struct kvm *kvm)
{
	u32 gd;

	if (!kvm->arch.gisa_int.origin)
		return 0;

	gd = virt_to_phys(kvm->arch.gisa_int.origin);

	if (gd && sclp.has_gisaf)
		gd |= GISA_FORMAT1;
	return gd;
}

static inline hva_t gpa_to_hva(struct kvm *kvm, gpa_t gpa)
{
	hva_t hva = gfn_to_hva(kvm, gpa_to_gfn(gpa));

	if (!kvm_is_error_hva(hva))
		hva |= offset_in_page(gpa);
	return hva;
}

/* implemented in pv.c */
int kvm_s390_pv_destroy_cpu(struct kvm_vcpu *vcpu, u16 *rc, u16 *rrc);
int kvm_s390_pv_create_cpu(struct kvm_vcpu *vcpu, u16 *rc, u16 *rrc);
int kvm_s390_pv_set_aside(struct kvm *kvm, u16 *rc, u16 *rrc);
int kvm_s390_pv_deinit_aside_vm(struct kvm *kvm, u16 *rc, u16 *rrc);
int kvm_s390_pv_deinit_cleanup_all(struct kvm *kvm, u16 *rc, u16 *rrc);
int kvm_s390_pv_deinit_vm(struct kvm *kvm, u16 *rc, u16 *rrc);
int kvm_s390_pv_init_vm(struct kvm *kvm, u16 *rc, u16 *rrc);
int kvm_s390_pv_set_sec_parms(struct kvm *kvm, void *hdr, u64 length, u16 *rc,
			      u16 *rrc);
int kvm_s390_pv_unpack(struct kvm *kvm, unsigned long addr, unsigned long size,
		       unsigned long tweak, u16 *rc, u16 *rrc);
int kvm_s390_pv_set_cpu_state(struct kvm_vcpu *vcpu, u8 state);
int kvm_s390_pv_dump_cpu(struct kvm_vcpu *vcpu, void *buff, u16 *rc, u16 *rrc);
int kvm_s390_pv_dump_stor_state(struct kvm *kvm, void __user *buff_user,
				u64 *gaddr, u64 buff_user_len, u16 *rc, u16 *rrc);
int kvm_s390_pv_dump_complete(struct kvm *kvm, void __user *buff_user,
			      u16 *rc, u16 *rrc);
int kvm_s390_pv_destroy_page(struct kvm *kvm, unsigned long gaddr);
int kvm_s390_pv_convert_to_secure(struct kvm *kvm, unsigned long gaddr);
int kvm_s390_pv_make_secure(struct kvm *kvm, unsigned long gaddr, void *uvcb);

static inline u64 kvm_s390_pv_get_handle(struct kvm *kvm)
{
	return kvm->arch.pv.handle;
}

static inline u64 kvm_s390_pv_cpu_get_handle(struct kvm_vcpu *vcpu)
{
	return vcpu->arch.pv.handle;
}

/**
 * __kvm_s390_pv_destroy_page() - Destroy a guest page.
 * @page: the page to destroy
 *
 * An attempt will be made to destroy the given guest page. If the attempt
 * fails, an attempt is made to export the page. If both attempts fail, an
 * appropriate error is returned.
 *
 * Context: must be called holding the mm lock for gmap->mm
 */
static inline int __kvm_s390_pv_destroy_page(struct page *page)
{
	struct folio *folio = page_folio(page);
	int rc;

	/* Large folios cannot be secure. Small folio implies FW_LEVEL_PTE. */
	if (folio_test_large(folio))
		return -EFAULT;

	rc = uv_destroy_folio(folio);
	/*
	 * Fault handlers can race; it is possible that two CPUs will fault
	 * on the same secure page. One CPU can destroy the page, reboot,
	 * re-enter secure mode and import it, while the second CPU was
	 * stuck at the beginning of the handler. At some point the second
	 * CPU will be able to progress, and it will not be able to destroy
	 * the page. In that case we do not want to terminate the process,
	 * we instead try to export the page.
	 */
	if (rc)
		rc = uv_convert_from_secure_folio(folio);

	return rc;
}

/* implemented in interrupt.c */
int kvm_s390_handle_wait(struct kvm_vcpu *vcpu);
void kvm_s390_vcpu_wakeup(struct kvm_vcpu *vcpu);
enum hrtimer_restart kvm_s390_idle_wakeup(struct hrtimer *timer);
int __must_check kvm_s390_deliver_pending_interrupts(struct kvm_vcpu *vcpu);
void kvm_s390_clear_local_irqs(struct kvm_vcpu *vcpu);
void kvm_s390_clear_float_irqs(struct kvm *kvm);
int __must_check kvm_s390_inject_vm(struct kvm *kvm,
				    struct kvm_s390_interrupt *s390int);
int __must_check kvm_s390_inject_vcpu(struct kvm_vcpu *vcpu,
				      struct kvm_s390_irq *irq);
static inline int kvm_s390_inject_prog_irq(struct kvm_vcpu *vcpu,
					   struct kvm_s390_pgm_info *pgm_info)
{
	struct kvm_s390_irq irq = {
		.type = KVM_S390_PROGRAM_INT,
		.u.pgm = *pgm_info,
	};

	return kvm_s390_inject_vcpu(vcpu, &irq);
}
static inline int kvm_s390_inject_program_int(struct kvm_vcpu *vcpu, u16 code)
{
	struct kvm_s390_irq irq = {
		.type = KVM_S390_PROGRAM_INT,
		.u.pgm.code = code,
	};

	return kvm_s390_inject_vcpu(vcpu, &irq);
}
struct kvm_s390_interrupt_info *kvm_s390_get_io_int(struct kvm *kvm,
						    u64 isc_mask, u32 schid);
int kvm_s390_reinject_io_int(struct kvm *kvm,
			     struct kvm_s390_interrupt_info *inti);
int kvm_s390_mask_adapter(struct kvm *kvm, unsigned int id, bool masked);

/* implemented in intercept.c */
u8 kvm_s390_get_ilen(struct kvm_vcpu *vcpu);
int kvm_handle_sie_intercept(struct kvm_vcpu *vcpu);
static inline void kvm_s390_rewind_psw(struct kvm_vcpu *vcpu, int ilen)
{
	struct kvm_s390_sie_block *sie_block = vcpu->arch.sie_block;

	sie_block->gpsw.addr = __rewind_psw(sie_block->gpsw, ilen);
}
static inline void kvm_s390_forward_psw(struct kvm_vcpu *vcpu, int ilen)
{
	kvm_s390_rewind_psw(vcpu, -ilen);
}
static inline void kvm_s390_retry_instr(struct kvm_vcpu *vcpu)
{
	/* don't inject PER events if we re-execute the instruction */
	vcpu->arch.sie_block->icptstatus &= ~0x02;
	kvm_s390_rewind_psw(vcpu, kvm_s390_get_ilen(vcpu));
}

int handle_sthyi(struct kvm_vcpu *vcpu);

/* implemented in priv.c */
int is_valid_psw(psw_t *psw);
int kvm_s390_handle_aa(struct kvm_vcpu *vcpu);
int kvm_s390_handle_b2(struct kvm_vcpu *vcpu);
int kvm_s390_handle_e3(struct kvm_vcpu *vcpu);
int kvm_s390_handle_e5(struct kvm_vcpu *vcpu);
int kvm_s390_handle_01(struct kvm_vcpu *vcpu);
int kvm_s390_handle_b9(struct kvm_vcpu *vcpu);
int kvm_s390_handle_lpsw(struct kvm_vcpu *vcpu);
int kvm_s390_handle_stctl(struct kvm_vcpu *vcpu);
int kvm_s390_handle_lctl(struct kvm_vcpu *vcpu);
int kvm_s390_handle_eb(struct kvm_vcpu *vcpu);
int kvm_s390_skey_check_enable(struct kvm_vcpu *vcpu);

/* implemented in vsie.c */
int kvm_s390_handle_vsie(struct kvm_vcpu *vcpu);
void kvm_s390_vsie_kick(struct kvm_vcpu *vcpu);
void kvm_s390_vsie_gmap_notifier(struct gmap *gmap, unsigned long start,
				 unsigned long end);
void kvm_s390_vsie_init(struct kvm *kvm);
void kvm_s390_vsie_destroy(struct kvm *kvm);
int gmap_shadow_valid(struct gmap *sg, unsigned long asce, int edat_level);

/* implemented in gmap-vsie.c */
struct gmap *gmap_shadow(struct gmap *parent, unsigned long asce, int edat_level);

/* implemented in sigp.c */
int kvm_s390_handle_sigp(struct kvm_vcpu *vcpu);
int kvm_s390_handle_sigp_pei(struct kvm_vcpu *vcpu);

/* implemented in kvm-s390.c */
int kvm_s390_try_set_tod_clock(struct kvm *kvm, const struct kvm_s390_vm_tod_clock *gtod);
int kvm_s390_store_status_unloaded(struct kvm_vcpu *vcpu, unsigned long addr);
int kvm_s390_vcpu_store_status(struct kvm_vcpu *vcpu, unsigned long addr);
int kvm_s390_vcpu_start(struct kvm_vcpu *vcpu);
int kvm_s390_vcpu_stop(struct kvm_vcpu *vcpu);
void kvm_s390_vcpu_block(struct kvm_vcpu *vcpu);
void kvm_s390_vcpu_unblock(struct kvm_vcpu *vcpu);
bool kvm_s390_vcpu_sie_inhibited(struct kvm_vcpu *vcpu);
void exit_sie(struct kvm_vcpu *vcpu);
void kvm_s390_sync_request(int req, struct kvm_vcpu *vcpu);
int kvm_s390_vcpu_setup_cmma(struct kvm_vcpu *vcpu);
void kvm_s390_vcpu_unsetup_cmma(struct kvm_vcpu *vcpu);
void kvm_s390_set_cpu_timer(struct kvm_vcpu *vcpu, __u64 cputm);
__u64 kvm_s390_get_cpu_timer(struct kvm_vcpu *vcpu);
int kvm_s390_cpus_from_pv(struct kvm *kvm, u16 *rc, u16 *rrc);
int __kvm_s390_handle_dat_fault(struct kvm_vcpu *vcpu, gfn_t gfn, gpa_t gaddr, unsigned int flags);
int __kvm_s390_mprotect_many(struct gmap *gmap, gpa_t gpa, u8 npages, unsigned int prot,
			     unsigned long bits);

static inline int kvm_s390_handle_dat_fault(struct kvm_vcpu *vcpu, gpa_t gaddr, unsigned int flags)
{
	return __kvm_s390_handle_dat_fault(vcpu, gpa_to_gfn(gaddr), gaddr, flags);
}

/* implemented in diag.c */
int kvm_s390_handle_diag(struct kvm_vcpu *vcpu);

static inline void kvm_s390_vcpu_block_all(struct kvm *kvm)
{
	unsigned long i;
	struct kvm_vcpu *vcpu;

	WARN_ON(!mutex_is_locked(&kvm->lock));
	kvm_for_each_vcpu(i, vcpu, kvm)
		kvm_s390_vcpu_block(vcpu);
}

static inline void kvm_s390_vcpu_unblock_all(struct kvm *kvm)
{
	unsigned long i;
	struct kvm_vcpu *vcpu;

	kvm_for_each_vcpu(i, vcpu, kvm)
		kvm_s390_vcpu_unblock(vcpu);
}

static inline u64 kvm_s390_get_tod_clock_fast(struct kvm *kvm)
{
	u64 rc;

	preempt_disable();
	rc = get_tod_clock_fast() + kvm->arch.epoch;
	preempt_enable();
	return rc;
}

/**
 * kvm_s390_inject_prog_cond - conditionally inject a program check
 * @vcpu: virtual cpu
 * @rc: original return/error code
 *
 * This function is supposed to be used after regular guest access functions
 * failed, to conditionally inject a program check to a vcpu. The typical
 * pattern would look like
 *
 * rc = write_guest(vcpu, addr, data, len);
 * if (rc)
 *	return kvm_s390_inject_prog_cond(vcpu, rc);
 *
 * A negative return code from guest access functions implies an internal error
 * like e.g. out of memory. In these cases no program check should be injected
 * to the guest.
 * A positive value implies that an exception happened while accessing a guest's
 * memory. In this case all data belonging to the corresponding program check
 * has been stored in vcpu->arch.pgm and can be injected with
 * kvm_s390_inject_prog_irq().
 *
 * Returns: - the original @rc value if @rc was negative (internal error)
 *	    - zero if @rc was already zero
 *	    - zero or error code from injecting if @rc was positive
 *	      (program check injected to @vcpu)
 */
static inline int kvm_s390_inject_prog_cond(struct kvm_vcpu *vcpu, int rc)
{
	if (rc <= 0)
		return rc;
	return kvm_s390_inject_prog_irq(vcpu, &vcpu->arch.pgm);
}

int s390int_to_s390irq(struct kvm_s390_interrupt *s390int,
			struct kvm_s390_irq *s390irq);

/* implemented in interrupt.c */
int kvm_s390_vcpu_has_irq(struct kvm_vcpu *vcpu, int exclude_stop);
int psw_extint_disabled(struct kvm_vcpu *vcpu);
void kvm_s390_destroy_adapters(struct kvm *kvm);
int kvm_s390_ext_call_pending(struct kvm_vcpu *vcpu);
extern struct kvm_device_ops kvm_flic_ops;
int kvm_s390_is_stop_irq_pending(struct kvm_vcpu *vcpu);
int kvm_s390_is_restart_irq_pending(struct kvm_vcpu *vcpu);
void kvm_s390_clear_stop_irq(struct kvm_vcpu *vcpu);
int kvm_s390_set_irq_state(struct kvm_vcpu *vcpu,
			   void __user *buf, int len);
int kvm_s390_get_irq_state(struct kvm_vcpu *vcpu,
			   __u8 __user *buf, int len);
void kvm_s390_gisa_init(struct kvm *kvm);
void kvm_s390_gisa_clear(struct kvm *kvm);
void kvm_s390_gisa_destroy(struct kvm *kvm);
void kvm_s390_gisa_disable(struct kvm *kvm);
void kvm_s390_gisa_enable(struct kvm *kvm);
int __init kvm_s390_gib_init(u8 nisc);
void kvm_s390_gib_destroy(void);

/* implemented in guestdbg.c */
void kvm_s390_backup_guest_per_regs(struct kvm_vcpu *vcpu);
void kvm_s390_restore_guest_per_regs(struct kvm_vcpu *vcpu);
void kvm_s390_patch_guest_per_regs(struct kvm_vcpu *vcpu);
int kvm_s390_import_bp_data(struct kvm_vcpu *vcpu,
			    struct kvm_guest_debug *dbg);
void kvm_s390_clear_bp_data(struct kvm_vcpu *vcpu);
void kvm_s390_prepare_debug_exit(struct kvm_vcpu *vcpu);
int kvm_s390_handle_per_ifetch_icpt(struct kvm_vcpu *vcpu);
int kvm_s390_handle_per_event(struct kvm_vcpu *vcpu);

/* support for Basic/Extended SCA handling */
static inline union ipte_control *kvm_s390_get_ipte_control(struct kvm *kvm)
{
	struct bsca_block *sca = kvm->arch.sca; /* SCA version doesn't matter */

	return &sca->ipte_control;
}
static inline int kvm_s390_use_sca_entries(void)
{
	/*
	 * Without SIGP interpretation, only SRS interpretation (if available)
	 * might use the entries. By not setting the entries and keeping them
	 * invalid, hardware will not access them but intercept.
	 */
	return sclp.has_sigpif;
}
void kvm_s390_reinject_machine_check(struct kvm_vcpu *vcpu,
				     struct mcck_volatile_info *mcck_info);

static inline bool kvm_s390_cur_gmap_fault_is_write(void)
{
	if (current->thread.gmap_int_code == PGM_PROTECTION)
		return true;
	return test_facility(75) && (current->thread.gmap_teid.fsi == TEID_FSI_STORE);
}

/**
 * kvm_s390_vcpu_crypto_reset_all
 *
 * Reset the crypto attributes for each vcpu. This can be done while the vcpus
 * are running as each vcpu will be removed from SIE before resetting the crypt
 * attributes and restored to SIE afterward.
 *
 * Note: The kvm->lock must be held while calling this function
 *
 * @kvm: the KVM guest
 */
void kvm_s390_vcpu_crypto_reset_all(struct kvm *kvm);

/**
 * kvm_s390_vcpu_pci_enable_interp
 *
 * Set the associated PCI attributes for each vcpu to allow for zPCI Load/Store
 * interpretation as well as adapter interruption forwarding.
 *
 * @kvm: the KVM guest
 */
void kvm_s390_vcpu_pci_enable_interp(struct kvm *kvm);

/**
 * diag9c_forwarding_hz
 *
 * Set the maximum number of diag9c forwarding per second
 */
extern unsigned int diag9c_forwarding_hz;

#endif
