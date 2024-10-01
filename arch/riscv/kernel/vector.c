// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2023 SiFive
 * Author: Andy Chiu <andy.chiu@sifive.com>
 */
#include <linux/export.h>
#include <linux/sched/signal.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/uaccess.h>
#include <linux/prctl.h>

#include <asm/thread_info.h>
#include <asm/processor.h>
#include <asm/insn.h>
#include <asm/vector.h>
#include <asm/csr.h>
#include <asm/elf.h>
#include <asm/ptrace.h>
#include <asm/bug.h>

static bool riscv_v_implicit_uacc = IS_ENABLED(CONFIG_RISCV_ISA_V_DEFAULT_ENABLE);
static struct kmem_cache *riscv_v_user_cachep;
#ifdef CONFIG_RISCV_ISA_V_PREEMPTIVE
static struct kmem_cache *riscv_v_kernel_cachep;
#endif

unsigned long riscv_v_vsize __read_mostly;
EXPORT_SYMBOL_GPL(riscv_v_vsize);

int riscv_v_setup_vsize(void)
{
	unsigned long this_vsize;

	/* There are 32 vector registers with vlenb length. */
	riscv_v_enable();
	this_vsize = csr_read(CSR_VLENB) * 32;
	riscv_v_disable();

	if (!riscv_v_vsize) {
		riscv_v_vsize = this_vsize;
		return 0;
	}

	if (riscv_v_vsize != this_vsize) {
		WARN(1, "RISCV_ISA_V only supports one vlenb on SMP systems");
		return -EOPNOTSUPP;
	}

	return 0;
}

void __init riscv_v_setup_ctx_cache(void)
{
	if (!has_vector())
		return;

	riscv_v_user_cachep = kmem_cache_create_usercopy("riscv_vector_ctx",
							 riscv_v_vsize, 16, SLAB_PANIC,
							 0, riscv_v_vsize, NULL);
#ifdef CONFIG_RISCV_ISA_V_PREEMPTIVE
	riscv_v_kernel_cachep = kmem_cache_create("riscv_vector_kctx",
						  riscv_v_vsize, 16,
						  SLAB_PANIC, NULL);
#endif
}

static bool insn_is_vector(u32 insn_buf)
{
	u32 opcode = insn_buf & __INSN_OPCODE_MASK;
	u32 width, csr;

	/*
	 * All V-related instructions, including CSR operations are 4-Byte. So,
	 * do not handle if the instruction length is not 4-Byte.
	 */
	if (unlikely(GET_INSN_LENGTH(insn_buf) != 4))
		return false;

	switch (opcode) {
	case RVV_OPCODE_VECTOR:
		return true;
	case RVV_OPCODE_VL:
	case RVV_OPCODE_VS:
		width = RVV_EXRACT_VL_VS_WIDTH(insn_buf);
		if (width == RVV_VL_VS_WIDTH_8 || width == RVV_VL_VS_WIDTH_16 ||
		    width == RVV_VL_VS_WIDTH_32 || width == RVV_VL_VS_WIDTH_64)
			return true;

		break;
	case RVG_OPCODE_SYSTEM:
		csr = RVG_EXTRACT_SYSTEM_CSR(insn_buf);
		if ((csr >= CSR_VSTART && csr <= CSR_VCSR) ||
		    (csr >= CSR_VL && csr <= CSR_VLENB))
			return true;
	}

	return false;
}

static int riscv_v_thread_zalloc(struct kmem_cache *cache,
				 struct __riscv_v_ext_state *ctx)
{
	void *datap;

	datap = kmem_cache_zalloc(cache, GFP_KERNEL);
	if (!datap)
		return -ENOMEM;

	ctx->datap = datap;
	memset(ctx, 0, offsetof(struct __riscv_v_ext_state, datap));
	return 0;
}

void riscv_v_thread_alloc(struct task_struct *tsk)
{
#ifdef CONFIG_RISCV_ISA_V_PREEMPTIVE
	riscv_v_thread_zalloc(riscv_v_kernel_cachep, &tsk->thread.kernel_vstate);
#endif
}

void riscv_v_thread_free(struct task_struct *tsk)
{
	if (tsk->thread.vstate.datap)
		kmem_cache_free(riscv_v_user_cachep, tsk->thread.vstate.datap);
#ifdef CONFIG_RISCV_ISA_V_PREEMPTIVE
	if (tsk->thread.kernel_vstate.datap)
		kmem_cache_free(riscv_v_kernel_cachep, tsk->thread.kernel_vstate.datap);
#endif
}

#define VSTATE_CTRL_GET_CUR(x) ((x) & PR_RISCV_V_VSTATE_CTRL_CUR_MASK)
#define VSTATE_CTRL_GET_NEXT(x) (((x) & PR_RISCV_V_VSTATE_CTRL_NEXT_MASK) >> 2)
#define VSTATE_CTRL_MAKE_NEXT(x) (((x) << 2) & PR_RISCV_V_VSTATE_CTRL_NEXT_MASK)
#define VSTATE_CTRL_GET_INHERIT(x) (!!((x) & PR_RISCV_V_VSTATE_CTRL_INHERIT))
static inline int riscv_v_ctrl_get_cur(struct task_struct *tsk)
{
	return VSTATE_CTRL_GET_CUR(tsk->thread.vstate_ctrl);
}

static inline int riscv_v_ctrl_get_next(struct task_struct *tsk)
{
	return VSTATE_CTRL_GET_NEXT(tsk->thread.vstate_ctrl);
}

static inline bool riscv_v_ctrl_test_inherit(struct task_struct *tsk)
{
	return VSTATE_CTRL_GET_INHERIT(tsk->thread.vstate_ctrl);
}

static inline void riscv_v_ctrl_set(struct task_struct *tsk, int cur, int nxt,
				    bool inherit)
{
	unsigned long ctrl;

	ctrl = cur & PR_RISCV_V_VSTATE_CTRL_CUR_MASK;
	ctrl |= VSTATE_CTRL_MAKE_NEXT(nxt);
	if (inherit)
		ctrl |= PR_RISCV_V_VSTATE_CTRL_INHERIT;
	tsk->thread.vstate_ctrl &= ~PR_RISCV_V_VSTATE_CTRL_MASK;
	tsk->thread.vstate_ctrl |= ctrl;
}

bool riscv_v_vstate_ctrl_user_allowed(void)
{
	return riscv_v_ctrl_get_cur(current) == PR_RISCV_V_VSTATE_CTRL_ON;
}
EXPORT_SYMBOL_GPL(riscv_v_vstate_ctrl_user_allowed);

bool riscv_v_first_use_handler(struct pt_regs *regs)
{
	u32 __user *epc = (u32 __user *)regs->epc;
	u32 insn = (u32)regs->badaddr;

	if (!has_vector())
		return false;

	/* Do not handle if V is not supported, or disabled */
	if (!riscv_v_vstate_ctrl_user_allowed())
		return false;

	/* If V has been enabled then it is not the first-use trap */
	if (riscv_v_vstate_query(regs))
		return false;

	/* Get the instruction */
	if (!insn) {
		if (__get_user(insn, epc))
			return false;
	}

	/* Filter out non-V instructions */
	if (!insn_is_vector(insn))
		return false;

	/* Sanity check. datap should be null by the time of the first-use trap */
	WARN_ON(current->thread.vstate.datap);

	/*
	 * Now we sure that this is a V instruction. And it executes in the
	 * context where VS has been off. So, try to allocate the user's V
	 * context and resume execution.
	 */
	if (riscv_v_thread_zalloc(riscv_v_user_cachep, &current->thread.vstate)) {
		force_sig(SIGBUS);
		return true;
	}
	riscv_v_vstate_on(regs);
	riscv_v_vstate_set_restore(current, regs);
	return true;
}

void riscv_v_vstate_ctrl_init(struct task_struct *tsk)
{
	bool inherit;
	int cur, next;

	if (!has_vector())
		return;

	next = riscv_v_ctrl_get_next(tsk);
	if (!next) {
		if (READ_ONCE(riscv_v_implicit_uacc))
			cur = PR_RISCV_V_VSTATE_CTRL_ON;
		else
			cur = PR_RISCV_V_VSTATE_CTRL_OFF;
	} else {
		cur = next;
	}
	/* Clear next mask if inherit-bit is not set */
	inherit = riscv_v_ctrl_test_inherit(tsk);
	if (!inherit)
		next = PR_RISCV_V_VSTATE_CTRL_DEFAULT;

	riscv_v_ctrl_set(tsk, cur, next, inherit);
}

long riscv_v_vstate_ctrl_get_current(void)
{
	if (!has_vector())
		return -EINVAL;

	return current->thread.vstate_ctrl & PR_RISCV_V_VSTATE_CTRL_MASK;
}

long riscv_v_vstate_ctrl_set_current(unsigned long arg)
{
	bool inherit;
	int cur, next;

	if (!has_vector())
		return -EINVAL;

	if (arg & ~PR_RISCV_V_VSTATE_CTRL_MASK)
		return -EINVAL;

	cur = VSTATE_CTRL_GET_CUR(arg);
	switch (cur) {
	case PR_RISCV_V_VSTATE_CTRL_OFF:
		/* Do not allow user to turn off V if current is not off */
		if (riscv_v_ctrl_get_cur(current) != PR_RISCV_V_VSTATE_CTRL_OFF)
			return -EPERM;

		break;
	case PR_RISCV_V_VSTATE_CTRL_ON:
		break;
	case PR_RISCV_V_VSTATE_CTRL_DEFAULT:
		cur = riscv_v_ctrl_get_cur(current);
		break;
	default:
		return -EINVAL;
	}

	next = VSTATE_CTRL_GET_NEXT(arg);
	inherit = VSTATE_CTRL_GET_INHERIT(arg);
	switch (next) {
	case PR_RISCV_V_VSTATE_CTRL_DEFAULT:
	case PR_RISCV_V_VSTATE_CTRL_OFF:
	case PR_RISCV_V_VSTATE_CTRL_ON:
		riscv_v_ctrl_set(current, cur, next, inherit);
		return 0;
	}

	return -EINVAL;
}

#ifdef CONFIG_SYSCTL

static struct ctl_table riscv_v_default_vstate_table[] = {
	{
		.procname	= "riscv_v_default_allow",
		.data		= &riscv_v_implicit_uacc,
		.maxlen		= sizeof(riscv_v_implicit_uacc),
		.mode		= 0644,
		.proc_handler	= proc_dobool,
	},
};

static int __init riscv_v_sysctl_init(void)
{
	if (has_vector())
		if (!register_sysctl("abi", riscv_v_default_vstate_table))
			return -EINVAL;
	return 0;
}

#else /* ! CONFIG_SYSCTL */
static int __init riscv_v_sysctl_init(void) { return 0; }
#endif /* ! CONFIG_SYSCTL */

static int riscv_v_init(void)
{
	return riscv_v_sysctl_init();
}
core_initcall(riscv_v_init);
