/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2021-2022 Intel Corporation */
#ifndef _ASM_X86_TDX_H
#define _ASM_X86_TDX_H

#include <linux/init.h>
#include <linux/bits.h>
#include <linux/mmzone.h>

#include <asm/errno.h>
#include <asm/ptrace.h>
#include <asm/trapnr.h>
#include <asm/shared/tdx.h>

/*
 * SW-defined error codes.
 *
 * Bits 47:40 == 0xFF indicate Reserved status code class that never used by
 * TDX module.
 */
#define TDX_ERROR			_BITUL(63)
#define TDX_NON_RECOVERABLE		_BITUL(62)
#define TDX_SW_ERROR			(TDX_ERROR | GENMASK_ULL(47, 40))
#define TDX_SEAMCALL_VMFAILINVALID	(TDX_SW_ERROR | _UL(0xFFFF0000))

#define TDX_SEAMCALL_GP			(TDX_SW_ERROR | X86_TRAP_GP)
#define TDX_SEAMCALL_UD			(TDX_SW_ERROR | X86_TRAP_UD)

/*
 * TDX module SEAMCALL leaf function error codes
 */
#define TDX_SUCCESS		0ULL
#define TDX_RND_NO_ENTROPY	0x8000020300000000ULL

#ifndef __ASSEMBLER__

#include <uapi/asm/mce.h>
#include <asm/tdx_global_metadata.h>
#include <linux/pgtable.h>

/*
 * Used by the #VE exception handler to gather the #VE exception
 * info from the TDX module. This is a software only structure
 * and not part of the TDX module/VMM ABI.
 */
struct ve_info {
	u64 exit_reason;
	u64 exit_qual;
	/* Guest Linear (virtual) Address */
	u64 gla;
	/* Guest Physical Address */
	u64 gpa;
	u32 instr_len;
	u32 instr_info;
};

#ifdef CONFIG_INTEL_TDX_GUEST

void __init tdx_early_init(void);

void tdx_get_ve_info(struct ve_info *ve);

bool tdx_handle_virt_exception(struct pt_regs *regs, struct ve_info *ve);

void tdx_halt(void);

bool tdx_early_handle_ve(struct pt_regs *regs);

int tdx_mcall_get_report0(u8 *reportdata, u8 *tdreport);

int tdx_mcall_extend_rtmr(u8 index, u8 *data);

u64 tdx_hcall_get_quote(u8 *buf, size_t size);

void __init tdx_dump_attributes(u64 td_attr);
void __init tdx_dump_td_ctls(u64 td_ctls);

#else

static inline void tdx_early_init(void) { };
static inline void tdx_halt(void) { };

static inline bool tdx_early_handle_ve(struct pt_regs *regs) { return false; }

#endif /* CONFIG_INTEL_TDX_GUEST */

#if defined(CONFIG_KVM_GUEST) && defined(CONFIG_INTEL_TDX_GUEST)
long tdx_kvm_hypercall(unsigned int nr, unsigned long p1, unsigned long p2,
		       unsigned long p3, unsigned long p4);
#else
static inline long tdx_kvm_hypercall(unsigned int nr, unsigned long p1,
				     unsigned long p2, unsigned long p3,
				     unsigned long p4)
{
	return -ENODEV;
}
#endif /* CONFIG_INTEL_TDX_GUEST && CONFIG_KVM_GUEST */

#ifdef CONFIG_INTEL_TDX_HOST
u64 __seamcall(u64 fn, struct tdx_module_args *args);
u64 __seamcall_ret(u64 fn, struct tdx_module_args *args);
u64 __seamcall_saved_ret(u64 fn, struct tdx_module_args *args);
void tdx_init(void);

#include <linux/preempt.h>
#include <asm/archrandom.h>
#include <asm/processor.h>

typedef u64 (*sc_func_t)(u64 fn, struct tdx_module_args *args);

static __always_inline u64 __seamcall_dirty_cache(sc_func_t func, u64 fn,
						  struct tdx_module_args *args)
{
	lockdep_assert_preemption_disabled();

	/*
	 * SEAMCALLs are made to the TDX module and can generate dirty
	 * cachelines of TDX private memory.  Mark cache state incoherent
	 * so that the cache can be flushed during kexec.
	 *
	 * This needs to be done before actually making the SEAMCALL,
	 * because kexec-ing CPU could send NMI to stop remote CPUs,
	 * in which case even disabling IRQ won't help here.
	 */
	this_cpu_write(cache_state_incoherent, true);

	return func(fn, args);
}

static __always_inline u64 sc_retry(sc_func_t func, u64 fn,
			   struct tdx_module_args *args)
{
	int retry = RDRAND_RETRY_LOOPS;
	u64 ret;

	do {
		preempt_disable();
		ret = __seamcall_dirty_cache(func, fn, args);
		preempt_enable();
	} while (ret == TDX_RND_NO_ENTROPY && --retry);

	return ret;
}

#define seamcall(_fn, _args)		sc_retry(__seamcall, (_fn), (_args))
#define seamcall_ret(_fn, _args)	sc_retry(__seamcall_ret, (_fn), (_args))
#define seamcall_saved_ret(_fn, _args)	sc_retry(__seamcall_saved_ret, (_fn), (_args))
int tdx_cpu_enable(void);
int tdx_enable(void);
const char *tdx_dump_mce_info(struct mce *m);
const struct tdx_sys_info *tdx_get_sysinfo(void);

int tdx_guest_keyid_alloc(void);
u32 tdx_get_nr_guest_keyids(void);
void tdx_guest_keyid_free(unsigned int keyid);

void tdx_quirk_reset_page(struct page *page);

struct tdx_td {
	/* TD root structure: */
	struct page *tdr_page;

	int tdcs_nr_pages;
	/* TD control structure: */
	struct page **tdcs_pages;

	/* Size of `tdcx_pages` in struct tdx_vp */
	int tdcx_nr_pages;
};

struct tdx_vp {
	/* TDVP root page */
	struct page *tdvpr_page;
	/* precalculated page_to_phys(tdvpr_page) for use in noinstr code */
	phys_addr_t tdvpr_pa;

	/* TD vCPU control structure: */
	struct page **tdcx_pages;
};

static inline u64 mk_keyed_paddr(u16 hkid, struct page *page)
{
	u64 ret;

	ret = page_to_phys(page);
	/* KeyID bits are just above the physical address bits: */
	ret |= (u64)hkid << boot_cpu_data.x86_phys_bits;

	return ret;
}

static inline int pg_level_to_tdx_sept_level(enum pg_level level)
{
        WARN_ON_ONCE(level == PG_LEVEL_NONE);
        return level - 1;
}

u64 tdh_vp_enter(struct tdx_vp *vp, struct tdx_module_args *args);
u64 tdh_mng_addcx(struct tdx_td *td, struct page *tdcs_page);
u64 tdh_mem_page_add(struct tdx_td *td, u64 gpa, struct page *page, struct page *source, u64 *ext_err1, u64 *ext_err2);
u64 tdh_mem_sept_add(struct tdx_td *td, u64 gpa, int level, struct page *page, u64 *ext_err1, u64 *ext_err2);
u64 tdh_vp_addcx(struct tdx_vp *vp, struct page *tdcx_page);
u64 tdh_mem_page_aug(struct tdx_td *td, u64 gpa, int level, struct page *page, u64 *ext_err1, u64 *ext_err2);
u64 tdh_mem_range_block(struct tdx_td *td, u64 gpa, int level, u64 *ext_err1, u64 *ext_err2);
u64 tdh_mng_key_config(struct tdx_td *td);
u64 tdh_mng_create(struct tdx_td *td, u16 hkid);
u64 tdh_vp_create(struct tdx_td *td, struct tdx_vp *vp);
u64 tdh_mng_rd(struct tdx_td *td, u64 field, u64 *data);
u64 tdh_mr_extend(struct tdx_td *td, u64 gpa, u64 *ext_err1, u64 *ext_err2);
u64 tdh_mr_finalize(struct tdx_td *td);
u64 tdh_vp_flush(struct tdx_vp *vp);
u64 tdh_mng_vpflushdone(struct tdx_td *td);
u64 tdh_mng_key_freeid(struct tdx_td *td);
u64 tdh_mng_init(struct tdx_td *td, u64 td_params, u64 *extended_err);
u64 tdh_vp_init(struct tdx_vp *vp, u64 initial_rcx, u32 x2apicid);
u64 tdh_vp_rd(struct tdx_vp *vp, u64 field, u64 *data);
u64 tdh_vp_wr(struct tdx_vp *vp, u64 field, u64 data, u64 mask);
u64 tdh_phymem_page_reclaim(struct page *page, u64 *tdx_pt, u64 *tdx_owner, u64 *tdx_size);
u64 tdh_mem_track(struct tdx_td *tdr);
u64 tdh_mem_page_remove(struct tdx_td *td, u64 gpa, u64 level, u64 *ext_err1, u64 *ext_err2);
u64 tdh_phymem_cache_wb(bool resume);
u64 tdh_phymem_page_wbinvd_tdr(struct tdx_td *td);
u64 tdh_phymem_page_wbinvd_hkid(u64 hkid, struct page *page);
#else
static inline void tdx_init(void) { }
static inline int tdx_cpu_enable(void) { return -ENODEV; }
static inline int tdx_enable(void)  { return -ENODEV; }
static inline u32 tdx_get_nr_guest_keyids(void) { return 0; }
static inline const char *tdx_dump_mce_info(struct mce *m) { return NULL; }
static inline const struct tdx_sys_info *tdx_get_sysinfo(void) { return NULL; }
#endif	/* CONFIG_INTEL_TDX_HOST */

#ifdef CONFIG_KEXEC_CORE
void tdx_cpu_flush_cache_for_kexec(void);
#else
static inline void tdx_cpu_flush_cache_for_kexec(void) { }
#endif

#endif /* !__ASSEMBLER__ */
#endif /* _ASM_X86_TDX_H */
