/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012 ARM Ltd.
 */
#ifndef __ASM_FP_H
#define __ASM_FP_H

#include <asm/errno.h>
#include <asm/percpu.h>
#include <asm/ptrace.h>
#include <asm/processor.h>
#include <asm/sigcontext.h>
#include <asm/sysreg.h>

#ifndef __ASSEMBLER__

#include <linux/bitmap.h>
#include <linux/build_bug.h>
#include <linux/bug.h>
#include <linux/cache.h>
#include <linux/init.h>
#include <linux/stddef.h>
#include <linux/types.h>

#define __FPSIMD_PREAMBLE	".arch_extension fp\n" \
				".arch_extension simd\n"
#define __SVE_PREAMBLE		".arch_extension sve\n"
#define __SME_PREAMBLE		".arch_extension sme\n"

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

static inline void fpsimd_save_common(struct user_fpsimd_state *state)
{
	state->fpsr = read_sysreg_s(SYS_FPSR);
	state->fpcr = read_sysreg_s(SYS_FPCR);
}

static inline void fpsimd_load_common(const struct user_fpsimd_state *state)
{
	write_sysreg_s(state->fpsr, SYS_FPSR);
	write_sysreg_s(state->fpcr, SYS_FPCR);
}

static inline void fpsimd_save_vregs(struct user_fpsimd_state *state)
{
	instrument_write(state->vregs, sizeof(state->vregs));
	asm volatile(
	__FPSIMD_PREAMBLE
	"	stp	q0,  q1,  [%[vregs], #16 * 0]\n"
	"	stp	q2,  q3,  [%[vregs], #16 * 2]\n"
	"	stp	q4,  q5,  [%[vregs], #16 * 4]\n"
	"	stp	q6,  q7,  [%[vregs], #16 * 6]\n"
	"	stp	q8,  q9,  [%[vregs], #16 * 8]\n"
	"	stp	q10, q11, [%[vregs], #16 * 10]\n"
	"	stp	q12, q13, [%[vregs], #16 * 12]\n"
	"	stp	q14, q15, [%[vregs], #16 * 14]\n"
	"	stp	q16, q17, [%[vregs], #16 * 16]\n"
	"	stp	q18, q19, [%[vregs], #16 * 18]\n"
	"	stp	q20, q21, [%[vregs], #16 * 20]\n"
	"	stp	q22, q23, [%[vregs], #16 * 22]\n"
	"	stp	q24, q25, [%[vregs], #16 * 24]\n"
	"	stp	q26, q27, [%[vregs], #16 * 26]\n"
	"	stp	q28, q29, [%[vregs], #16 * 28]\n"
	"	stp	q30, q31, [%[vregs], #16 * 30]\n"
	: "=Q" (state->vregs)
	: [vregs] "r" (state->vregs)
	);
}

static inline void fpsimd_load_vregs(const struct user_fpsimd_state *state)
{
	instrument_read(state->vregs, sizeof(state->vregs));
	asm volatile(
	__FPSIMD_PREAMBLE
	"	ldp	q0,  q1,  [%[vregs], #16 * 0]\n"
	"	ldp	q2,  q3,  [%[vregs], #16 * 2]\n"
	"	ldp	q4,  q5,  [%[vregs], #16 * 4]\n"
	"	ldp	q6,  q7,  [%[vregs], #16 * 6]\n"
	"	ldp	q8,  q9,  [%[vregs], #16 * 8]\n"
	"	ldp	q10, q11, [%[vregs], #16 * 10]\n"
	"	ldp	q12, q13, [%[vregs], #16 * 12]\n"
	"	ldp	q14, q15, [%[vregs], #16 * 14]\n"
	"	ldp	q16, q17, [%[vregs], #16 * 16]\n"
	"	ldp	q18, q19, [%[vregs], #16 * 18]\n"
	"	ldp	q20, q21, [%[vregs], #16 * 20]\n"
	"	ldp	q22, q23, [%[vregs], #16 * 22]\n"
	"	ldp	q24, q25, [%[vregs], #16 * 24]\n"
	"	ldp	q26, q27, [%[vregs], #16 * 26]\n"
	"	ldp	q28, q29, [%[vregs], #16 * 28]\n"
	"	ldp	q30, q31, [%[vregs], #16 * 30]\n"
	:
	: "Q" (state->vregs),
	  [vregs] "r" (state->vregs)
	);
}

static inline void fpsimd_save_state(struct user_fpsimd_state *state)
{
	fpsimd_save_vregs(state);
	fpsimd_save_common(state);
}

static inline void fpsimd_load_state(const struct user_fpsimd_state *state)
{
	fpsimd_load_vregs(state);
	fpsimd_load_common(state);
}

extern void fpsimd_thread_switch(struct task_struct *next);
extern void fpsimd_flush_thread(void);

extern void fpsimd_preserve_current_state(void);
extern void fpsimd_restore_current_state(void);
extern void fpsimd_update_current_state(struct user_fpsimd_state const *state);

struct cpu_fp_state {
	struct user_fpsimd_state *st;
	struct arm64_sve_state *sve_state;
	struct arm64_sme_state *sme_state;
	u64 *svcr;
	u64 *fpmr;
	unsigned int sve_vl;
	unsigned int sme_vl;
	enum fp_type *fp_type;
	enum fp_type to_save;
};

DECLARE_PER_CPU(struct cpu_fp_state, fpsimd_last_state);

extern void fpsimd_bind_state_to_cpu(struct cpu_fp_state *fp_state);

extern void fpsimd_flush_task_state(struct task_struct *target);
extern void fpsimd_save_and_flush_current_state(void);
extern void fpsimd_save_and_flush_cpu_state(void);

static inline bool thread_sm_enabled(struct thread_struct *thread)
{
	return system_supports_sme() && (thread->svcr & SVCR_SM_MASK);
}

static inline bool thread_za_enabled(struct thread_struct *thread)
{
	return system_supports_sme() && (thread->svcr & SVCR_ZA_MASK);
}

extern void task_smstop_sm(struct task_struct *task);

/* Maximum VL that SVE/SME VL-agnostic software can transparently support */
#define VL_ARCH_MAX 0x100

static inline void *thread_zt_state(struct thread_struct *thread)
{
	/* The ZT register state is stored immediately after the ZA state */
	unsigned int sme_vq = sve_vq_from_vl(thread_get_sme_vl(thread));
	return (void *)thread->sme_state + ZA_SIG_REGS_SIZE(sme_vq);
}

static inline unsigned int sve_get_vl(void)
{
	unsigned int vl;

	asm volatile(
	__SVE_PREAMBLE
	"	rdvl %x[vl], #1\n"
	: [vl] "=r" (vl)
	);

	return vl;
}

#define FOR_EACH_Z_REG(idx_str, asm_str)											\
	"	.irp " idx_str ",0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31\n"	\
	asm_str	"\n"														\
	"	.endr\n"

#define FOR_EACH_P_REG(idx_str, asm_str)											\
	"	.irp " idx_str ",0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15\n"	\
	asm_str	"\n"								\
	"	.endr\n"

static inline void __sve_save_z(struct arm64_sve_state *state, unsigned long vl)
{
	instrument_write(state, SVE_NUM_ZREGS * vl);
	asm volatile(
	__SVE_PREAMBLE
	FOR_EACH_Z_REG("n", "str	z\\n, [%[zregs], #\\n, MUL VL]")
	:
	: [zregs] "r" (state)
	: "memory"
	);
}

static inline void __sve_load_z(const struct arm64_sve_state *state, unsigned long vl)
{
	instrument_read(state, SVE_NUM_ZREGS * vl);
	asm volatile(
	__SVE_PREAMBLE
	FOR_EACH_Z_REG("n", "ldr	z\\n, [%[zregs], #\\n, MUL VL]")
	:
	: [zregs] "r" (state)
	: "memory"
	);
}

static inline void __sve_save_p(struct arm64_sve_state *state, unsigned long vl, bool ffr)
{
	void *pregs = (void *)state + SVE_NUM_ZREGS * vl;
	unsigned long pl = vl / 8;
	void *pffr = pregs + SVE_NUM_PREGS * pl;

	instrument_write(pregs, SVE_NUM_PREGS * pl);
	asm volatile(
	__SVE_PREAMBLE
	FOR_EACH_P_REG("n", "str	p\\n, [%[pregs], #\\n, MUL VL]\n")
	:
	: [pregs] "r" (pregs)
	: "memory"
	);

	instrument_write(pffr, pl);
	if (ffr) {
		asm volatile(
		__SVE_PREAMBLE
		"	rdffr	p0.b\n"
		"	str	p0, [%[pffr]]\n"
		"	ldr	p0, [%[pregs]]\n"
		:
		: [pregs] "r" (pregs),
		  [pffr] "r" (pffr)
		: "memory"
		);
	} else {
		asm volatile(
		__SVE_PREAMBLE
		"	pfalse	p0.b\n"
		"	str	p0, [%[pffr]]\n"
		"	ldr	p0, [%[pregs]]\n"
		:
		: [pregs] "r" (pregs),
		  [pffr] "r" (pffr)
		: "memory"
		);
	}
}

static inline void __sve_load_p(const struct arm64_sve_state *state, unsigned long vl, bool ffr)
{
	const void *pregs = (const void *)state + SVE_NUM_ZREGS * vl;
	unsigned long pl = vl / 8;
	const void *pffr = pregs + SVE_NUM_PREGS * pl;

	if (ffr) {
		instrument_read(pffr, pl);
		asm volatile(
		__SVE_PREAMBLE
		"	ldr	p0, [%[pffr]]\n"
		"	wrffr	p0.b\n"
		:
		: [pffr] "r" (pffr)
		: "memory"
		);
	}

	instrument_read(pregs, SVE_NUM_PREGS * pl);
	asm volatile(
	__SVE_PREAMBLE
	FOR_EACH_P_REG("n", "ldr	p\\n, [%[pregs], #\\n, MUL VL]\n")
	:
	: [pregs] "r" (pregs)
	: "memory"
	);
}

static inline void sve_save_state(struct arm64_sve_state *state, bool ffr)
{
	unsigned long vl = sve_get_vl();
	__sve_save_z(state, vl);
	__sve_save_p(state, vl, ffr);
}

static inline void sve_load_state(const struct arm64_sve_state *state, bool ffr)
{
	unsigned long vl = sve_get_vl();
	__sve_load_z(state, vl);
	__sve_load_p(state, vl, ffr);
}

/*
 * Zero all SVE registers except for the first 128 bits of each vector.
 *
 * The caller must ensure that the VL has been configured and the CPU must be
 * in non-streaming mode.
 */
static inline void sve_flush_live(void)
{
	unsigned long vl = sve_get_vl();

	if (vl > sizeof(__uint128_t)) {
		asm volatile(
		__FPSIMD_PREAMBLE
		FOR_EACH_Z_REG("n", "mov	v\\n\\().16b, v\\n\\().16b")
		);
	}

	asm volatile(
	__SVE_PREAMBLE
	FOR_EACH_P_REG("n", "pfalse	p\\n\\().b")
	"	wrffr	p0.b\n"
	);
}

struct arm64_cpu_capabilities;
extern void cpu_enable_fpsimd(const struct arm64_cpu_capabilities *__unused);
extern void cpu_enable_sve(const struct arm64_cpu_capabilities *__unused);
extern void cpu_enable_sme(const struct arm64_cpu_capabilities *__unused);
extern void cpu_enable_sme2(const struct arm64_cpu_capabilities *__unused);
extern void cpu_enable_fa64(const struct arm64_cpu_capabilities *__unused);
extern void cpu_enable_fpmr(const struct arm64_cpu_capabilities *__unused);

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
extern void fpsimd_sync_from_effective_state(struct task_struct *task);
extern void fpsimd_sync_to_effective_state_zeropad(struct task_struct *task);

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

static inline size_t __sve_state_size(unsigned int sve_vl, unsigned int sme_vl)
{
	unsigned int vl = max(sve_vl, sme_vl);
	return SVE_SIG_REGS_SIZE(sve_vq_from_vl(vl));
}

/*
 * Return how many bytes of memory are required to store the full SVE
 * state for task, given task's currently configured vector length.
 */
static inline size_t sve_state_size(struct task_struct const *task)
{
	unsigned int sve_vl = task_get_sve_vl(task);
	unsigned int sme_vl = task_get_sme_vl(task);
	return __sve_state_size(sve_vl, sme_vl);
}

#else /* ! CONFIG_ARM64_SVE */

static inline void sve_alloc(struct task_struct *task, bool flush) { }
static inline void fpsimd_release_task(struct task_struct *task) { }
static inline void fpsimd_sync_from_effective_state(struct task_struct *task) { }
static inline void fpsimd_sync_to_effective_state_zeropad(struct task_struct *task) { }

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

static inline size_t __sve_state_size(unsigned int sve_vl, unsigned int sme_vl)
{
	return 0;
}

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

static inline unsigned int sme_get_vl(void)
{
	unsigned int vl;

	asm volatile(
	__SME_PREAMBLE
	"	rdsvl %x[vl], #1\n"
	: [vl] "=r" (vl)
	);

	return vl;
}

extern void sme_alloc(struct task_struct *task, bool flush);
extern int sme_set_current_vl(unsigned long arg);
extern int sme_get_current_vl(void);
extern void sme_suspend_exit(void);

static inline size_t __sme_state_size(unsigned int sme_vl)
{
	size_t size = ZA_SIG_REGS_SIZE(sve_vq_from_vl(sme_vl));

	if (system_supports_sme2())
		size += ZT_SIG_REG_SIZE;

	return size;
}

static inline void __sme_save_za(struct arm64_sme_state *state, unsigned long svl)
{
	/*
	 * The <Wv> argument to LDR/STR (array vector) can only encode W12-W15.
	 * The "Ucj" constraint exists for this, but is only supported by GCC
	 * 14.1.0+ and LLVM 18.1.0+.
	 */
	register unsigned int v asm ("w12");

	instrument_write(state, svl * svl);
	for (v = 0; v < svl; v++) {
		void *pav = (void *)state + v * svl;

		asm volatile(
		__SME_PREAMBLE
		"	str	za[%w[v], #0], [%[pav]]\n"
		:
		: [v] "r" (v),
		  [pav] "r" (pav)
		: "memory"
		);
	}
}

static inline void __sme_load_za(const struct arm64_sme_state *state, unsigned long svl)
{
	/* See comment in __sme_save_za */
	register unsigned int v asm ("w12");

	instrument_read(state, svl * svl);
	for (v = 0; v < svl; v++) {
		void *pav = (void *)state + v * svl;

		asm volatile(
		__SME_PREAMBLE
		"	ldr	za[%w[v], #0], [%[pav]]\n"
		:
		: [v] "r" (v),
		  [pav] "r" (pav)
		: "memory"
		);
	}
}

static inline void __sme_save_zt(struct arm64_sme_state *state, unsigned long svl)
{
	void *pzt = (void *)state + svl * svl;

	instrument_write(pzt, 64);
	asm volatile(
	__DEFINE_ASM_GPR_NUMS
	/*
	 * STR ZT0, [<Xn|SP>]
	 * Supported by binutils 2.41+.
	 * Supported by LLVM 16+
	 */
	"	.inst	0xe13f8000 | ((.L__gpr_num_%[pzt]) << 5)\n"
	:
	: [pzt] "r" (pzt)
	: "memory"
	);
}

static inline void __sme_load_zt(const struct arm64_sme_state *state, unsigned long svl)
{
	void *pzt = (void *)state + svl * svl;

	instrument_read(pzt, 64);
	asm volatile(
	__DEFINE_ASM_GPR_NUMS
	/*
	 * LDR ZT0, [<Xn|SP>]
	 * Supported by binutils 2.41+.
	 * Supported by LLVM 16+
	 */
	"	.inst	0xe11f8000 | ((.L__gpr_num_%[pzt]) << 5)\n"
	:
	: [pzt] "r" (pzt)
	: "memory"
	);
}

static inline void sme_save_state(struct arm64_sme_state *state, bool zt)
{
	unsigned long svl = sme_get_vl();

	__sme_save_za(state, svl);
	if (zt)
		__sme_save_zt(state, svl);
}

static inline void sme_load_state(const struct arm64_sme_state *state, bool zt)
{
	unsigned long svl = sme_get_vl();

	__sme_load_za(state, svl);
	if (zt)
		__sme_load_zt(state, svl);
}

/*
 * Return how many bytes of memory are required to store the full SME
 * specific state for task, given task's currently configured vector
 * length.
 */
static inline size_t sme_state_size(struct task_struct const *task)
{
	return __sme_state_size(task_get_sme_vl(task));
}

void sme_enable_dvmsync(void);
void sme_set_active(void);
void sme_clear_active(void);

static inline void sme_enter_from_user_mode(void)
{
	if (alternative_has_cap_unlikely(ARM64_WORKAROUND_4193714) &&
	    test_thread_flag(TIF_SME))
		sme_clear_active();
}

static inline void sme_exit_to_user_mode(void)
{
	if (alternative_has_cap_unlikely(ARM64_WORKAROUND_4193714) &&
	    test_thread_flag(TIF_SME))
		sme_set_active();
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

static inline size_t __sme_state_size(unsigned int sme_vl)
{
	return 0;
}

static inline size_t sme_state_size(struct task_struct const *task)
{
	return 0;
}

static inline void sme_save_state(struct arm64_sme_state *state, bool zt) { BUILD_BUG(); }
static inline void sme_load_state(const struct arm64_sme_state *state, bool zt) { BUILD_BUG(); }

static inline void sme_enter_from_user_mode(void) { }
static inline void sme_exit_to_user_mode(void) { }

#endif /* ! CONFIG_ARM64_SME */

/* For use by EFI runtime services calls only */
extern void __efi_fpsimd_begin(void);
extern void __efi_fpsimd_end(void);

#endif

#endif
