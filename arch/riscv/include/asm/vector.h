/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2020 SiFive
 */

#ifndef __ASM_RISCV_VECTOR_H
#define __ASM_RISCV_VECTOR_H

#include <linux/types.h>
#include <uapi/asm-generic/errno.h>

#ifdef CONFIG_RISCV_ISA_V

#include <linux/stringify.h>
#include <linux/sched.h>
#include <linux/sched/task_stack.h>
#include <asm/ptrace.h>
#include <asm/cpufeature.h>
#include <asm/csr.h>
#include <asm/asm.h>
#include <asm/vendorid_list.h>
#include <asm/vendor_extensions.h>
#include <asm/vendor_extensions/thead.h>

#define __riscv_v_vstate_or(_val, TYPE) ({				\
	typeof(_val) _res = _val;					\
	if (has_xtheadvector()) \
		_res = (_res & ~SR_VS_THEAD) | SR_VS_##TYPE##_THEAD;	\
	else								\
		_res = (_res & ~SR_VS) | SR_VS_##TYPE;			\
	_res;								\
})

#define __riscv_v_vstate_check(_val, TYPE) ({				\
	bool _res;							\
	if (has_xtheadvector()) \
		_res = ((_val) & SR_VS_THEAD) == SR_VS_##TYPE##_THEAD;	\
	else								\
		_res = ((_val) & SR_VS) == SR_VS_##TYPE;		\
	_res;								\
})

extern unsigned long riscv_v_vsize;
int riscv_v_setup_vsize(void);
bool insn_is_vector(u32 insn_buf);
bool riscv_v_first_use_handler(struct pt_regs *regs);
void kernel_vector_begin(void);
void kernel_vector_end(void);
void get_cpu_vector_context(void);
void put_cpu_vector_context(void);
void riscv_v_thread_free(struct task_struct *tsk);
void __init riscv_v_setup_ctx_cache(void);
void riscv_v_thread_alloc(struct task_struct *tsk);

static inline u32 riscv_v_flags(void)
{
	return READ_ONCE(current->thread.riscv_v_flags);
}

static __always_inline bool has_vector(void)
{
	return riscv_has_extension_unlikely(RISCV_ISA_EXT_ZVE32X);
}

static __always_inline bool has_xtheadvector_no_alternatives(void)
{
	if (IS_ENABLED(CONFIG_RISCV_ISA_XTHEADVECTOR))
		return riscv_isa_vendor_extension_available(THEAD_VENDOR_ID, XTHEADVECTOR);
	else
		return false;
}

static __always_inline bool has_xtheadvector(void)
{
	if (IS_ENABLED(CONFIG_RISCV_ISA_XTHEADVECTOR))
		return riscv_has_vendor_extension_unlikely(THEAD_VENDOR_ID,
							   RISCV_ISA_VENDOR_EXT_XTHEADVECTOR);
	else
		return false;
}

static inline void __riscv_v_vstate_clean(struct pt_regs *regs)
{
	regs->status = __riscv_v_vstate_or(regs->status, CLEAN);
}

static inline void __riscv_v_vstate_dirty(struct pt_regs *regs)
{
	regs->status = __riscv_v_vstate_or(regs->status, DIRTY);
}

static inline void riscv_v_vstate_off(struct pt_regs *regs)
{
	regs->status = __riscv_v_vstate_or(regs->status, OFF);
}

static inline void riscv_v_vstate_on(struct pt_regs *regs)
{
	regs->status = __riscv_v_vstate_or(regs->status, INITIAL);
}

static inline bool riscv_v_vstate_query(struct pt_regs *regs)
{
	return !__riscv_v_vstate_check(regs->status, OFF);
}

static __always_inline void riscv_v_enable(void)
{
	if (has_xtheadvector())
		csr_set(CSR_SSTATUS, SR_VS_THEAD);
	else
		csr_set(CSR_SSTATUS, SR_VS);
}

static __always_inline void riscv_v_disable(void)
{
	if (has_xtheadvector())
		csr_clear(CSR_SSTATUS, SR_VS_THEAD);
	else
		csr_clear(CSR_SSTATUS, SR_VS);
}

static __always_inline void __vstate_csr_save(struct __riscv_v_ext_state *dest)
{
	asm volatile (
		"csrr	%0, " __stringify(CSR_VSTART) "\n\t"
		"csrr	%1, " __stringify(CSR_VTYPE) "\n\t"
		"csrr	%2, " __stringify(CSR_VL) "\n\t"
		: "=r" (dest->vstart), "=r" (dest->vtype), "=r" (dest->vl),
		"=r" (dest->vcsr) : :);

	if (has_xtheadvector()) {
		unsigned long status;

		/*
		 * CSR_VCSR is defined as
		 * [2:1] - vxrm[1:0]
		 * [0] - vxsat
		 * The earlier vector spec implemented by T-Head uses separate
		 * registers for the same bit-elements, so just combine those
		 * into the existing output field.
		 *
		 * Additionally T-Head cores need FS to be enabled when accessing
		 * the VXRM and VXSAT CSRs, otherwise ending in illegal instructions.
		 * Though the cores do not implement the VXRM and VXSAT fields in the
		 * FCSR CSR that vector-0.7.1 specifies.
		 */
		status = csr_read_set(CSR_STATUS, SR_FS_DIRTY);
		dest->vcsr = csr_read(CSR_VXSAT) | csr_read(CSR_VXRM) << CSR_VXRM_SHIFT;

		dest->vlenb = riscv_v_vsize / 32;

		if ((status & SR_FS) != SR_FS_DIRTY)
			csr_write(CSR_STATUS, status);
	} else {
		dest->vcsr = csr_read(CSR_VCSR);
		dest->vlenb = csr_read(CSR_VLENB);
	}
}

static __always_inline void __vstate_csr_restore(struct __riscv_v_ext_state *src)
{
	asm volatile (
		".option push\n\t"
		".option arch, +zve32x\n\t"
		"vsetvl	 x0, %2, %1\n\t"
		".option pop\n\t"
		"csrw	" __stringify(CSR_VSTART) ", %0\n\t"
		: : "r" (src->vstart), "r" (src->vtype), "r" (src->vl));

	if (has_xtheadvector()) {
		unsigned long status = csr_read(CSR_SSTATUS);

		/*
		 * Similar to __vstate_csr_save above, restore values for the
		 * separate VXRM and VXSAT CSRs from the vcsr variable.
		 */
		status = csr_read_set(CSR_STATUS, SR_FS_DIRTY);

		csr_write(CSR_VXRM, (src->vcsr >> CSR_VXRM_SHIFT) & CSR_VXRM_MASK);
		csr_write(CSR_VXSAT, src->vcsr & CSR_VXSAT_MASK);

		if ((status & SR_FS) != SR_FS_DIRTY)
			csr_write(CSR_STATUS, status);
	} else {
		csr_write(CSR_VCSR, src->vcsr);
	}
}

static inline void __riscv_v_vstate_save(struct __riscv_v_ext_state *save_to,
					 void *datap)
{
	unsigned long vl;

	riscv_v_enable();
	__vstate_csr_save(save_to);
	if (has_xtheadvector()) {
		asm volatile (
			"mv t0, %0\n\t"
			THEAD_VSETVLI_T4X0E8M8D1
			THEAD_VSB_V_V0T0
			"add		t0, t0, t4\n\t"
			THEAD_VSB_V_V0T0
			"add		t0, t0, t4\n\t"
			THEAD_VSB_V_V0T0
			"add		t0, t0, t4\n\t"
			THEAD_VSB_V_V0T0
			: : "r" (datap) : "memory", "t0", "t4");
	} else {
		asm volatile (
			".option push\n\t"
			".option arch, +zve32x\n\t"
			"vsetvli	%0, x0, e8, m8, ta, ma\n\t"
			"vse8.v		v0, (%1)\n\t"
			"add		%1, %1, %0\n\t"
			"vse8.v		v8, (%1)\n\t"
			"add		%1, %1, %0\n\t"
			"vse8.v		v16, (%1)\n\t"
			"add		%1, %1, %0\n\t"
			"vse8.v		v24, (%1)\n\t"
			".option pop\n\t"
			: "=&r" (vl) : "r" (datap) : "memory");
	}
	riscv_v_disable();
}

static inline void __riscv_v_vstate_restore(struct __riscv_v_ext_state *restore_from,
					    void *datap)
{
	unsigned long vl;

	riscv_v_enable();
	if (has_xtheadvector()) {
		asm volatile (
			"mv t0, %0\n\t"
			THEAD_VSETVLI_T4X0E8M8D1
			THEAD_VLB_V_V0T0
			"add		t0, t0, t4\n\t"
			THEAD_VLB_V_V0T0
			"add		t0, t0, t4\n\t"
			THEAD_VLB_V_V0T0
			"add		t0, t0, t4\n\t"
			THEAD_VLB_V_V0T0
			: : "r" (datap) : "memory", "t0", "t4");
	} else {
		asm volatile (
			".option push\n\t"
			".option arch, +zve32x\n\t"
			"vsetvli	%0, x0, e8, m8, ta, ma\n\t"
			"vle8.v		v0, (%1)\n\t"
			"add		%1, %1, %0\n\t"
			"vle8.v		v8, (%1)\n\t"
			"add		%1, %1, %0\n\t"
			"vle8.v		v16, (%1)\n\t"
			"add		%1, %1, %0\n\t"
			"vle8.v		v24, (%1)\n\t"
			".option pop\n\t"
			: "=&r" (vl) : "r" (datap) : "memory");
	}
	__vstate_csr_restore(restore_from);
	riscv_v_disable();
}

static inline void __riscv_v_vstate_discard(void)
{
	unsigned long vl, vtype_inval = 1UL << (BITS_PER_LONG - 1);

	riscv_v_enable();
	if (has_xtheadvector())
		asm volatile (THEAD_VSETVLI_T4X0E8M8D1 : : : "t4");
	else
		asm volatile (
			".option push\n\t"
			".option arch, +zve32x\n\t"
			"vsetvli	%0, x0, e8, m8, ta, ma\n\t"
			".option pop\n\t": "=&r" (vl));

	asm volatile (
		".option push\n\t"
		".option arch, +zve32x\n\t"
		"vmv.v.i	v0, -1\n\t"
		"vmv.v.i	v8, -1\n\t"
		"vmv.v.i	v16, -1\n\t"
		"vmv.v.i	v24, -1\n\t"
		"vsetvl		%0, x0, %1\n\t"
		".option pop\n\t"
		: "=&r" (vl) : "r" (vtype_inval));

	riscv_v_disable();
}

static inline void riscv_v_vstate_discard(struct pt_regs *regs)
{
	if (riscv_v_vstate_query(regs)) {
		__riscv_v_vstate_discard();
		__riscv_v_vstate_dirty(regs);
	}
}

static inline void riscv_v_vstate_save(struct __riscv_v_ext_state *vstate,
				       struct pt_regs *regs)
{
	if (__riscv_v_vstate_check(regs->status, DIRTY)) {
		__riscv_v_vstate_save(vstate, vstate->datap);
		__riscv_v_vstate_clean(regs);
	}
}

static inline void riscv_v_vstate_restore(struct __riscv_v_ext_state *vstate,
					  struct pt_regs *regs)
{
	if (riscv_v_vstate_query(regs)) {
		__riscv_v_vstate_restore(vstate, vstate->datap);
		__riscv_v_vstate_clean(regs);
	}
}

static inline void riscv_v_vstate_set_restore(struct task_struct *task,
					      struct pt_regs *regs)
{
	if (riscv_v_vstate_query(regs)) {
		set_tsk_thread_flag(task, TIF_RISCV_V_DEFER_RESTORE);
		riscv_v_vstate_on(regs);
	}
}

#ifdef CONFIG_RISCV_ISA_V_PREEMPTIVE
static inline bool riscv_preempt_v_dirty(struct task_struct *task)
{
	return !!(task->thread.riscv_v_flags & RISCV_PREEMPT_V_DIRTY);
}

static inline bool riscv_preempt_v_restore(struct task_struct *task)
{
	return !!(task->thread.riscv_v_flags & RISCV_PREEMPT_V_NEED_RESTORE);
}

static inline void riscv_preempt_v_clear_dirty(struct task_struct *task)
{
	barrier();
	task->thread.riscv_v_flags &= ~RISCV_PREEMPT_V_DIRTY;
}

static inline void riscv_preempt_v_set_restore(struct task_struct *task)
{
	barrier();
	task->thread.riscv_v_flags |= RISCV_PREEMPT_V_NEED_RESTORE;
}

static inline bool riscv_preempt_v_started(struct task_struct *task)
{
	return !!(task->thread.riscv_v_flags & RISCV_PREEMPT_V);
}

#else /* !CONFIG_RISCV_ISA_V_PREEMPTIVE */
static inline bool riscv_preempt_v_dirty(struct task_struct *task) { return false; }
static inline bool riscv_preempt_v_restore(struct task_struct *task) { return false; }
static inline bool riscv_preempt_v_started(struct task_struct *task) { return false; }
#define riscv_preempt_v_clear_dirty(tsk)	do {} while (0)
#define riscv_preempt_v_set_restore(tsk)	do {} while (0)
#endif /* CONFIG_RISCV_ISA_V_PREEMPTIVE */

static inline void __switch_to_vector(struct task_struct *prev,
				      struct task_struct *next)
{
	struct pt_regs *regs;

	if (riscv_preempt_v_started(prev)) {
		if (riscv_preempt_v_dirty(prev)) {
			__riscv_v_vstate_save(&prev->thread.kernel_vstate,
					      prev->thread.kernel_vstate.datap);
			riscv_preempt_v_clear_dirty(prev);
		}
	} else {
		regs = task_pt_regs(prev);
		riscv_v_vstate_save(&prev->thread.vstate, regs);
	}

	if (riscv_preempt_v_started(next))
		riscv_preempt_v_set_restore(next);
	else
		riscv_v_vstate_set_restore(next, task_pt_regs(next));
}

void riscv_v_vstate_ctrl_init(struct task_struct *tsk);
bool riscv_v_vstate_ctrl_user_allowed(void);

#else /* ! CONFIG_RISCV_ISA_V  */

struct pt_regs;

static inline int riscv_v_setup_vsize(void) { return -EOPNOTSUPP; }
static __always_inline bool has_vector(void) { return false; }
static __always_inline bool insn_is_vector(u32 insn_buf) { return false; }
static __always_inline bool has_xtheadvector_no_alternatives(void) { return false; }
static __always_inline bool has_xtheadvector(void) { return false; }
static inline bool riscv_v_first_use_handler(struct pt_regs *regs) { return false; }
static inline bool riscv_v_vstate_query(struct pt_regs *regs) { return false; }
static inline bool riscv_v_vstate_ctrl_user_allowed(void) { return false; }
#define riscv_v_vsize (0)
#define riscv_v_vstate_discard(regs)		do {} while (0)
#define riscv_v_vstate_save(vstate, regs)	do {} while (0)
#define riscv_v_vstate_restore(vstate, regs)	do {} while (0)
#define __switch_to_vector(__prev, __next)	do {} while (0)
#define riscv_v_vstate_off(regs)		do {} while (0)
#define riscv_v_vstate_on(regs)			do {} while (0)
#define riscv_v_thread_free(tsk)		do {} while (0)
#define  riscv_v_setup_ctx_cache()		do {} while (0)
#define riscv_v_thread_alloc(tsk)		do {} while (0)

#endif /* CONFIG_RISCV_ISA_V */

/*
 * Return the implementation's vlen value.
 *
 * riscv_v_vsize contains the value of "32 vector registers with vlenb length"
 * so rebuild the vlen value in bits from it.
 */
static inline int riscv_vector_vlen(void)
{
	return riscv_v_vsize / 32 * 8;
}

#endif /* ! __ASM_RISCV_VECTOR_H */
