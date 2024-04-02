/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012 ARM Ltd.
 */
#ifndef __ASM_FP_H
#define __ASM_FP_H

#include <asm/errno.h>
#include <asm/ptrace.h>
#include <asm/processor.h>
#include <asm/sigcontext.h>
#include <asm/sysreg.h>

#ifndef __ASSEMBLY__

#include <linux/bitmap.h>
#include <linux/build_bug.h>
#include <linux/bug.h>
#include <linux/cache.h>
#include <linux/init.h>
#include <linux/stddef.h>
#include <linux/types.h>

/* Masks for extracting the FPSR and FPCR from the FPSCR */
#define VFP_FPSCR_STAT_MASK	0xf800009f
#define VFP_FPSCR_CTRL_MASK	0x07f79f00
/*
 * The VFP state has 32x64-bit registers and a single 32-bit
 * control/status register.
 */
#define VFP_STATE_SIZE		((32 * 8) + 4)

static inline unsigned long cpacr_save_enable_kernel_sve(void)
{
	unsigned long old = read_sysreg(cpacr_el1);
	unsigned long set = CPACR_EL1_FPEN_EL1EN | CPACR_EL1_ZEN_EL1EN;

	write_sysreg(old | set, cpacr_el1);
	isb();
	return old;
}

static inline unsigned long cpacr_save_enable_kernel_sme(void)
{
	unsigned long old = read_sysreg(cpacr_el1);
	unsigned long set = CPACR_EL1_FPEN_EL1EN | CPACR_EL1_SMEN_EL1EN;

	write_sysreg(old | set, cpacr_el1);
	isb();
	return old;
}

static inline void cpacr_restore(unsigned long cpacr)
{
	write_sysreg(cpacr, cpacr_el1);
	isb();
}

/*
 * When we defined the maximum SVE vector length we defined the ABI so
 * that the maximum vector length included all the reserved for future
 * expansion bits in ZCR rather than those just currently defined by
 * the architecture.  Using this length to allocate worst size buffers
 * results in excessively large allocations, and this effect is even
 * more pronounced for SME due to ZA.  Define more suitable VLs for
 * these situations.
 */
#define ARCH_SVE_VQ_MAX ((ZCR_ELx_LEN_MASK >> ZCR_ELx_LEN_SHIFT) + 1)
#define SME_VQ_MAX	((SMCR_ELx_LEN_MASK >> SMCR_ELx_LEN_SHIFT) + 1)

struct task_struct;

extern void fpsimd_save_state(struct user_fpsimd_state *state);
extern void fpsimd_load_state(struct user_fpsimd_state *state);

extern void fpsimd_thread_switch(struct task_struct *next);
extern void fpsimd_flush_thread(void);

extern void fpsimd_signal_preserve_current_state(void);
extern void fpsimd_preserve_current_state(void);
extern void fpsimd_restore_current_state(void);
extern void fpsimd_update_current_state(struct user_fpsimd_state const *state);
extern void fpsimd_kvm_prepare(void);

struct cpu_fp_state {
	struct user_fpsimd_state *st;
	void *sve_state;
	void *sme_state;
	u64 *svcr;
	u64 *fpmr;
	unsigned int sve_vl;
	unsigned int sme_vl;
	enum fp_type *fp_type;
	enum fp_type to_save;
};

extern void fpsimd_bind_state_to_cpu(struct cpu_fp_state *fp_state);

extern void fpsimd_flush_task_state(struct task_struct *target);
extern void fpsimd_save_and_flush_cpu_state(void);

static inline bool thread_sm_enabled(struct thread_struct *thread)
{
	return system_supports_sme() && (thread->svcr & SVCR_SM_MASK);
}

static inline bool thread_za_enabled(struct thread_struct *thread)
{
	return system_supports_sme() && (thread->svcr & SVCR_ZA_MASK);
}

/* Maximum VL that SVE/SME VL-agnostic software can transparently support */
#define VL_ARCH_MAX 0x100

/* Offset of FFR in the SVE register dump */
static inline size_t sve_ffr_offset(int vl)
{
	return SVE_SIG_FFR_OFFSET(sve_vq_from_vl(vl)) - SVE_SIG_REGS_OFFSET;
}

static inline void *sve_pffr(struct thread_struct *thread)
{
	unsigned int vl;

	if (system_supports_sme() && thread_sm_enabled(thread))
		vl = thread_get_sme_vl(thread);
	else
		vl = thread_get_sve_vl(thread);

	return (char *)thread->sve_state + sve_ffr_offset(vl);
}

static inline void *thread_zt_state(struct thread_struct *thread)
{
	/* The ZT register state is stored immediately after the ZA state */
	unsigned int sme_vq = sve_vq_from_vl(thread_get_sme_vl(thread));
	return thread->sme_state + ZA_SIG_REGS_SIZE(sme_vq);
}

extern void sve_save_state(void *state, u32 *pfpsr, int save_ffr);
extern void sve_load_state(void const *state, u32 const *pfpsr,
			   int restore_ffr);
extern void sve_flush_live(bool flush_ffr, unsigned long vq_minus_1);
extern unsigned int sve_get_vl(void);
extern void sve_set_vq(unsigned long vq_minus_1);
extern void sme_set_vq(unsigned long vq_minus_1);
extern void sme_save_state(void *state, int zt);
extern void sme_load_state(void const *state, int zt);

struct arm64_cpu_capabilities;
extern void cpu_enable_fpsimd(const struct arm64_cpu_capabilities *__unused);
extern void cpu_enable_sve(const struct arm64_cpu_capabilities *__unused);
extern void cpu_enable_sme(const struct arm64_cpu_capabilities *__unused);
extern void cpu_enable_sme2(const struct arm64_cpu_capabilities *__unused);
extern void cpu_enable_fa64(const struct arm64_cpu_capabilities *__unused);
extern void cpu_enable_fpmr(const struct arm64_cpu_capabilities *__unused);

extern u64 read_smcr_features(void);

/*
 * Helpers to translate bit indices in sve_vq_map to VQ values (and
 * vice versa).  This allows find_next_bit() to be used to find the
 * _maximum_ VQ not exceeding a certain value.
 */
static inline unsigned int __vq_to_bit(unsigned int vq)
{
	return SVE_VQ_MAX - vq;
}

static inline unsigned int __bit_to_vq(unsigned int bit)
{
	return SVE_VQ_MAX - bit;
}


struct vl_info {
	enum vec_type type;
	const char *name;		/* For display purposes */

	/* Minimum supported vector length across all CPUs */
	int min_vl;

	/* Maximum supported vector length across all CPUs */
	int max_vl;
	int max_virtualisable_vl;

	/*
	 * Set of available vector lengths,
	 * where length vq encoded as bit __vq_to_bit(vq):
	 */
	DECLARE_BITMAP(vq_map, SVE_VQ_MAX);

	/* Set of vector lengths present on at least one cpu: */
	DECLARE_BITMAP(vq_partial_map, SVE_VQ_MAX);
};

#ifdef CONFIG_ARM64_SVE

extern void sve_alloc(struct task_struct *task, bool flush);
extern void fpsimd_release_task(struct task_struct *task);
extern void fpsimd_sync_to_sve(struct task_struct *task);
extern void fpsimd_force_sync_to_sve(struct task_struct *task);
extern void sve_sync_to_fpsimd(struct task_struct *task);
extern void sve_sync_from_fpsimd_zeropad(struct task_struct *task);

extern int vec_set_vector_length(struct task_struct *task, enum vec_type type,
				 unsigned long vl, unsigned long flags);

extern int sve_set_current_vl(unsigned long arg);
extern int sve_get_current_vl(void);

static inline void sve_user_disable(void)
{
	sysreg_clear_set(cpacr_el1, CPACR_EL1_ZEN_EL0EN, 0);
}

static inline void sve_user_enable(void)
{
	sysreg_clear_set(cpacr_el1, 0, CPACR_EL1_ZEN_EL0EN);
}

#define sve_cond_update_zcr_vq(val, reg)		\
	do {						\
		u64 __zcr = read_sysreg_s((reg));	\
		u64 __new = __zcr & ~ZCR_ELx_LEN_MASK;	\
		__new |= (val) & ZCR_ELx_LEN_MASK;	\
		if (__zcr != __new)			\
			write_sysreg_s(__new, (reg));	\
	} while (0)

/*
 * Probing and setup functions.
 * Calls to these functions must be serialised with one another.
 */
enum vec_type;

extern void __init vec_init_vq_map(enum vec_type type);
extern void vec_update_vq_map(enum vec_type type);
extern int vec_verify_vq_map(enum vec_type type);
extern void __init sve_setup(void);

extern __ro_after_init struct vl_info vl_info[ARM64_VEC_MAX];

static inline void write_vl(enum vec_type type, u64 val)
{
	u64 tmp;

	switch (type) {
#ifdef CONFIG_ARM64_SVE
	case ARM64_VEC_SVE:
		tmp = read_sysreg_s(SYS_ZCR_EL1) & ~ZCR_ELx_LEN_MASK;
		write_sysreg_s(tmp | val, SYS_ZCR_EL1);
		break;
#endif
#ifdef CONFIG_ARM64_SME
	case ARM64_VEC_SME:
		tmp = read_sysreg_s(SYS_SMCR_EL1) & ~SMCR_ELx_LEN_MASK;
		write_sysreg_s(tmp | val, SYS_SMCR_EL1);
		break;
#endif
	default:
		WARN_ON_ONCE(1);
		break;
	}
}

static inline int vec_max_vl(enum vec_type type)
{
	return vl_info[type].max_vl;
}

static inline int vec_max_virtualisable_vl(enum vec_type type)
{
	return vl_info[type].max_virtualisable_vl;
}

static inline int sve_max_vl(void)
{
	return vec_max_vl(ARM64_VEC_SVE);
}

static inline int sve_max_virtualisable_vl(void)
{
	return vec_max_virtualisable_vl(ARM64_VEC_SVE);
}

/* Ensure vq >= SVE_VQ_MIN && vq <= SVE_VQ_MAX before calling this function */
static inline bool vq_available(enum vec_type type, unsigned int vq)
{
	return test_bit(__vq_to_bit(vq), vl_info[type].vq_map);
}

static inline bool sve_vq_available(unsigned int vq)
{
	return vq_available(ARM64_VEC_SVE, vq);
}

size_t sve_state_size(struct task_struct const *task);

#else /* ! CONFIG_ARM64_SVE */

static inline void sve_alloc(struct task_struct *task, bool flush) { }
static inline void fpsimd_release_task(struct task_struct *task) { }
static inline void sve_sync_to_fpsimd(struct task_struct *task) { }
static inline void sve_sync_from_fpsimd_zeropad(struct task_struct *task) { }

static inline int sve_max_virtualisable_vl(void)
{
	return 0;
}

static inline int sve_set_current_vl(unsigned long arg)
{
	return -EINVAL;
}

static inline int sve_get_current_vl(void)
{
	return -EINVAL;
}

static inline int sve_max_vl(void)
{
	return -EINVAL;
}

static inline bool sve_vq_available(unsigned int vq) { return false; }

static inline void sve_user_disable(void) { BUILD_BUG(); }
static inline void sve_user_enable(void) { BUILD_BUG(); }

#define sve_cond_update_zcr_vq(val, reg) do { } while (0)

static inline void vec_init_vq_map(enum vec_type t) { }
static inline void vec_update_vq_map(enum vec_type t) { }
static inline int vec_verify_vq_map(enum vec_type t) { return 0; }
static inline void sve_setup(void) { }

static inline size_t sve_state_size(struct task_struct const *task)
{
	return 0;
}

#endif /* ! CONFIG_ARM64_SVE */

#ifdef CONFIG_ARM64_SME

static inline void sme_user_disable(void)
{
	sysreg_clear_set(cpacr_el1, CPACR_EL1_SMEN_EL0EN, 0);
}

static inline void sme_user_enable(void)
{
	sysreg_clear_set(cpacr_el1, 0, CPACR_EL1_SMEN_EL0EN);
}

static inline void sme_smstart_sm(void)
{
	asm volatile(__msr_s(SYS_SVCR_SMSTART_SM_EL0, "xzr"));
}

static inline void sme_smstop_sm(void)
{
	asm volatile(__msr_s(SYS_SVCR_SMSTOP_SM_EL0, "xzr"));
}

static inline void sme_smstop(void)
{
	asm volatile(__msr_s(SYS_SVCR_SMSTOP_SMZA_EL0, "xzr"));
}

extern void __init sme_setup(void);

static inline int sme_max_vl(void)
{
	return vec_max_vl(ARM64_VEC_SME);
}

static inline int sme_max_virtualisable_vl(void)
{
	return vec_max_virtualisable_vl(ARM64_VEC_SME);
}

extern void sme_alloc(struct task_struct *task, bool flush);
extern unsigned int sme_get_vl(void);
extern int sme_set_current_vl(unsigned long arg);
extern int sme_get_current_vl(void);
extern void sme_suspend_exit(void);

/*
 * Return how many bytes of memory are required to store the full SME
 * specific state for task, given task's currently configured vector
 * length.
 */
static inline size_t sme_state_size(struct task_struct const *task)
{
	unsigned int vl = task_get_sme_vl(task);
	size_t size;

	size = ZA_SIG_REGS_SIZE(sve_vq_from_vl(vl));

	if (system_supports_sme2())
		size += ZT_SIG_REG_SIZE;

	return size;
}

#else

static inline void sme_user_disable(void) { BUILD_BUG(); }
static inline void sme_user_enable(void) { BUILD_BUG(); }

static inline void sme_smstart_sm(void) { }
static inline void sme_smstop_sm(void) { }
static inline void sme_smstop(void) { }

static inline void sme_alloc(struct task_struct *task, bool flush) { }
static inline void sme_setup(void) { }
static inline unsigned int sme_get_vl(void) { return 0; }
static inline int sme_max_vl(void) { return 0; }
static inline int sme_max_virtualisable_vl(void) { return 0; }
static inline int sme_set_current_vl(unsigned long arg) { return -EINVAL; }
static inline int sme_get_current_vl(void) { return -EINVAL; }
static inline void sme_suspend_exit(void) { }

static inline size_t sme_state_size(struct task_struct const *task)
{
	return 0;
}

#endif /* ! CONFIG_ARM64_SME */

/* For use by EFI runtime services calls only */
extern void __efi_fpsimd_begin(void);
extern void __efi_fpsimd_end(void);

#endif

#endif
