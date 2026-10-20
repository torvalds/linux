// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/sched/task_stack.h>
#include <linux/perf_event.h>
#include <linux/bug.h>
#include <linux/stddef.h>
#include <asm/perf_regs.h>
#include <asm/ptrace.h>

#ifdef CONFIG_X86_32
#define PERF_REG_X86_MAX PERF_REG_X86_32_MAX
#else
#define PERF_REG_X86_MAX PERF_REG_X86_64_MAX
#endif

#define PT_REGS_OFFSET(id, r) [id] = offsetof(struct pt_regs, r)

static unsigned int pt_regs_offset[PERF_REG_X86_MAX] = {
	PT_REGS_OFFSET(PERF_REG_X86_AX, ax),
	PT_REGS_OFFSET(PERF_REG_X86_BX, bx),
	PT_REGS_OFFSET(PERF_REG_X86_CX, cx),
	PT_REGS_OFFSET(PERF_REG_X86_DX, dx),
	PT_REGS_OFFSET(PERF_REG_X86_SI, si),
	PT_REGS_OFFSET(PERF_REG_X86_DI, di),
	PT_REGS_OFFSET(PERF_REG_X86_BP, bp),
	PT_REGS_OFFSET(PERF_REG_X86_SP, sp),
	PT_REGS_OFFSET(PERF_REG_X86_IP, ip),
	PT_REGS_OFFSET(PERF_REG_X86_FLAGS, flags),
	PT_REGS_OFFSET(PERF_REG_X86_CS, cs),
	PT_REGS_OFFSET(PERF_REG_X86_SS, ss),
#ifdef CONFIG_X86_32
	PT_REGS_OFFSET(PERF_REG_X86_DS, ds),
	PT_REGS_OFFSET(PERF_REG_X86_ES, es),
	PT_REGS_OFFSET(PERF_REG_X86_FS, fs),
	PT_REGS_OFFSET(PERF_REG_X86_GS, gs),
#else
	/*
	 * The pt_regs struct does not store
	 * ds, es, fs, gs in 64 bit mode.
	 */
	(unsigned int) -1,
	(unsigned int) -1,
	(unsigned int) -1,
	(unsigned int) -1,
#endif
#ifdef CONFIG_X86_64
	PT_REGS_OFFSET(PERF_REG_X86_R8, r8),
	PT_REGS_OFFSET(PERF_REG_X86_R9, r9),
	PT_REGS_OFFSET(PERF_REG_X86_R10, r10),
	PT_REGS_OFFSET(PERF_REG_X86_R11, r11),
	PT_REGS_OFFSET(PERF_REG_X86_R12, r12),
	PT_REGS_OFFSET(PERF_REG_X86_R13, r13),
	PT_REGS_OFFSET(PERF_REG_X86_R14, r14),
	PT_REGS_OFFSET(PERF_REG_X86_R15, r15),
#endif
};

u64 perf_reg_value(struct pt_regs *regs, int idx)
{
	struct x86_perf_regs *perf_regs;

	if (idx > PERF_REG_X86_R15) {
		perf_regs = container_of(regs, struct x86_perf_regs, regs);
		if (perf_regs->abi == PERF_SAMPLE_REGS_ABI_NONE)
			return 0;

		if (perf_regs->abi & PERF_SAMPLE_REGS_ABI_SIMD) {
			if (idx <= PERF_REG_X86_R31) {
				if (!perf_regs->egpr_regs)
					return 0;
				return perf_regs->egpr_regs[idx - PERF_REG_X86_R16];
			}
			if (idx == PERF_REG_X86_SSP) {
				if (!perf_regs->ssp)
					return 0;
				return *perf_regs->ssp;
			}
		} else {
			if (idx >= PERF_REG_X86_XMM0 && idx < PERF_REG_X86_XMM_MAX) {
				if (!perf_regs->xmm_regs)
					return 0;
				return perf_regs->xmm_regs[idx - PERF_REG_X86_XMM0];
			}
		}
	}

	if (WARN_ON_ONCE(idx >= ARRAY_SIZE(pt_regs_offset)))
		return 0;

	return regs_get_register(regs, pt_regs_offset[idx]);
}

#define PERF_X86_YMMH_QWORDS	(PERF_X86_YMM_QWORDS / 2)
#define PERF_X86_ZMMH_QWORDS	(PERF_X86_ZMM_QWORDS / 2)

u64 perf_simd_reg_value(struct pt_regs *regs, int idx,
			u16 qwords_idx, bool pred)
{
	struct x86_perf_regs *perf_regs =
			container_of(regs, struct x86_perf_regs, regs);

	if (!(perf_regs->abi & PERF_SAMPLE_REGS_ABI_SIMD))
		return 0;

	if (pred) {
		if (WARN_ON_ONCE(idx >= PERF_X86_SIMD_PRED_REGS_MAX ||
				 qwords_idx >= PERF_X86_OPMASK_QWORDS))
			return 0;
		if (!perf_regs->opmask_regs)
			return 0;
		return perf_regs->opmask_regs[idx];
	}

	if (WARN_ON_ONCE(idx >= PERF_X86_SIMD_VEC_REGS_MAX ||
			 qwords_idx >= PERF_X86_SIMD_QWORDS_MAX))
		return 0;

	if (idx >= PERF_X86_H16ZMM_BASE) {
		if (!perf_regs->h16zmm_regs)
			return 0;
		return perf_regs->h16zmm_regs[(idx - PERF_X86_H16ZMM_BASE) *
					PERF_X86_ZMM_QWORDS + qwords_idx];
	}

	if (qwords_idx < PERF_X86_XMM_QWORDS) {
		if (!perf_regs->xmm_regs)
			return 0;
		return perf_regs->xmm_regs[idx * PERF_X86_XMM_QWORDS +
					   qwords_idx];
	} else if (qwords_idx < PERF_X86_YMM_QWORDS) {
		if (!perf_regs->ymmh_regs)
			return 0;
		return perf_regs->ymmh_regs[idx * PERF_X86_YMMH_QWORDS +
					    qwords_idx - PERF_X86_XMM_QWORDS];
	} else if (qwords_idx < PERF_X86_ZMM_QWORDS) {
		if (!perf_regs->zmmh_regs)
			return 0;
		return perf_regs->zmmh_regs[idx * PERF_X86_ZMMH_QWORDS +
					    qwords_idx - PERF_X86_YMM_QWORDS];
	}

	return 0;
}

int perf_simd_reg_validate(u16 vec_qwords, u64 vec_mask,
			   u16 pred_qwords, u32 pred_mask)
{
	unsigned long mask;
	u64 size;

	if (!vec_qwords && !pred_qwords) {
		if (vec_mask || pred_mask)
			return -EINVAL;
	}

	if (vec_qwords) {
		if (vec_qwords != PERF_X86_XMM_QWORDS &&
		    vec_qwords != PERF_X86_YMM_QWORDS &&
		    vec_qwords != PERF_X86_ZMM_QWORDS)
			return -EINVAL;
		if (vec_mask & ~PERF_X86_SIMD_VEC_MASK)
			return -EINVAL;
		/* Only full-register sampling is allowed. */
		mask = vec_mask;
		if (vec_qwords == PERF_X86_XMM_QWORDS && mask &&
		    !bitmap_full(&mask, PERF_X86_SIMD_XMM_REGS))
			return -EINVAL;
		if (vec_qwords == PERF_X86_YMM_QWORDS && mask &&
		    !bitmap_full(&mask, PERF_X86_SIMD_YMM_REGS))
			return -EINVAL;
		if (vec_qwords == PERF_X86_ZMM_QWORDS && mask &&
		    !bitmap_full(&mask, PERF_X86_SIMD_ZMM_REGS))
			return -EINVAL;
	}

	if (pred_qwords) {
		if (pred_qwords != PERF_X86_OPMASK_QWORDS)
			return -EINVAL;
		if (pred_mask & ~PERF_X86_SIMD_PRED_MASK)
			return -EINVAL;
		/* Only full-register sampling is allowed. */
		mask = pred_mask;
		if (pred_qwords == PERF_X86_OPMASK_QWORDS && mask &&
		    !bitmap_full(&mask, PERF_X86_SIMD_OPMASK_REGS))
			return -EINVAL;
	}

	size = sizeof(u64) * 4;
	size += (hweight64(vec_mask) * vec_qwords +
		 hweight32(pred_mask) * pred_qwords) * sizeof(u64);
	/*
	 * INTR_REGS and USR_REGS could be sampled simultaneously,
	 * so roughly restrict the size to half of U16_MAX.
	 */
	if (size >= U16_MAX / 2)
		return -EINVAL;

	return 0;
}

#define PERF_REG_X86_RESERVED	(GENMASK_ULL(PERF_REG_X86_XMM0 - 1, PERF_REG_X86_AX) & \
				 ~GENMASK_ULL(PERF_REG_X86_R15, PERF_REG_X86_AX))
#define PERF_REG_X86_EXT_RESERVED	(~GENMASK_ULL(PERF_REG_MISC_MAX - 1, PERF_REG_X86_AX))

#ifdef CONFIG_X86_32
#define REG_NOSUPPORT GENMASK_ULL(PERF_REG_X86_R15, PERF_REG_X86_R8)

int perf_reg_validate(u64 mask, bool simd_enabled)
{
	if (!simd_enabled &&
	    (!mask || (mask & (REG_NOSUPPORT | PERF_REG_X86_RESERVED))))
		return -EINVAL;

	/* The mask could be 0 if only the SIMD registers are interested */
	if (simd_enabled &&
	    (mask & ~GENMASK_ULL(PERF_REG_X86_GS, PERF_REG_X86_AX)))
		return -EINVAL;

	return 0;
}

u64 perf_reg_abi(struct task_struct *task)
{
	return PERF_SAMPLE_REGS_ABI_32;
}

#else /* CONFIG_X86_64 */
#define REG_NOSUPPORT ((1ULL << PERF_REG_X86_DS) | \
		       (1ULL << PERF_REG_X86_ES) | \
		       (1ULL << PERF_REG_X86_FS) | \
		       (1ULL << PERF_REG_X86_GS))

int perf_reg_validate(u64 mask, bool simd_enabled)
{
	if (!simd_enabled &&
	    (!mask || (mask & (REG_NOSUPPORT | PERF_REG_X86_RESERVED))))
		return -EINVAL;

	/* The mask could be 0 if only the SIMD registers are interested */
	if (simd_enabled &&
	    (mask & (REG_NOSUPPORT | PERF_REG_X86_EXT_RESERVED)))
		return -EINVAL;

	return 0;
}

u64 perf_reg_abi(struct task_struct *task)
{
	if (!user_64bit_mode(task_pt_regs(task)))
		return PERF_SAMPLE_REGS_ABI_32;
	else
		return PERF_SAMPLE_REGS_ABI_64;
}

static DEFINE_PER_CPU(struct pt_regs, nmi_user_regs);

void perf_get_regs_user(struct perf_regs *regs_user,
			struct pt_regs *regs)
{
	struct pt_regs *regs_user_copy = this_cpu_ptr(&nmi_user_regs);
	struct pt_regs *user_regs = task_pt_regs(current);

	if (!in_nmi()) {
		regs_user->regs = user_regs;
		regs_user->abi = perf_reg_abi(current);
		return;
	}

	/*
	 * If we're in an NMI that interrupted task_pt_regs setup, then
	 * we can't sample user regs at all.  This check isn't really
	 * sufficient, though, as we could be in an NMI inside an interrupt
	 * that happened during task_pt_regs setup.
	 */
	if (regs->sp > (unsigned long)&user_regs->r11 &&
	    regs->sp <= (unsigned long)(user_regs + 1)) {
		regs_user->abi = PERF_SAMPLE_REGS_ABI_NONE;
		regs_user->regs = NULL;
		return;
	}

	/*
	 * These registers are always saved on 64-bit syscall entry.
	 * On 32-bit entry points, they are saved too except r8..r11.
	 */
	regs_user_copy->ip = user_regs->ip;
	regs_user_copy->ax = user_regs->ax;
	regs_user_copy->cx = user_regs->cx;
	regs_user_copy->dx = user_regs->dx;
	regs_user_copy->si = user_regs->si;
	regs_user_copy->di = user_regs->di;
	regs_user_copy->r8 = user_regs->r8;
	regs_user_copy->r9 = user_regs->r9;
	regs_user_copy->r10 = user_regs->r10;
	regs_user_copy->r11 = user_regs->r11;
	regs_user_copy->orig_ax = user_regs->orig_ax;
	regs_user_copy->flags = user_regs->flags;
	regs_user_copy->sp = user_regs->sp;
	regs_user_copy->cs = user_regs->cs;
	regs_user_copy->ss = user_regs->ss;
	/*
	 * Store user space frame-pointer value on sample
	 * to facilitate stack unwinding for cases when
	 * user space executable code has such support
	 * enabled at compile time:
	 */
	regs_user_copy->bp = user_regs->bp;

	regs_user_copy->bx = -1;
	regs_user_copy->r12 = -1;
	regs_user_copy->r13 = -1;
	regs_user_copy->r14 = -1;
	regs_user_copy->r15 = -1;
	/*
	 * For this to be at all useful, we need a reasonable guess for
	 * the ABI.  Be careful: we're in NMI context, and we're
	 * considering current to be the current task, so we should
	 * be careful not to look at any other percpu variables that might
	 * change during context switches.
	 */
	regs_user->abi = user_64bit_mode(user_regs) ?
		PERF_SAMPLE_REGS_ABI_64 : PERF_SAMPLE_REGS_ABI_32;

	regs_user->regs = regs_user_copy;
}
#endif /* CONFIG_X86_32 */
