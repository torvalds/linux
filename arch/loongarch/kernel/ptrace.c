// SPDX-License-Identifier: GPL-2.0
/*
 * Author: Hanlu Li <lihanlu@loongson.cn>
 *         Huacai Chen <chenhuacai@loongson.cn>
 *
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 *
 * Derived from MIPS:
 * Copyright (C) 1992 Ross Biro
 * Copyright (C) Linus Torvalds
 * Copyright (C) 1994, 95, 96, 97, 98, 2000 Ralf Baechle
 * Copyright (C) 1996 David S. Miller
 * Kevin D. Kissell, kevink@mips.com and Carsten Langgaard, carstenl@mips.com
 * Copyright (C) 1999 MIPS Technologies, Inc.
 * Copyright (C) 2000 Ulf Carlsson
 */
#include <linux/kernel.h>
#include <linux/audit.h>
#include <linux/compiler.h>
#include <linux/context_tracking.h>
#include <linux/elf.h>
#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/ptrace.h>
#include <linux/regset.h>
#include <linux/sched.h>
#include <linux/sched/task_stack.h>
#include <linux/security.h>
#include <linux/smp.h>
#include <linux/stddef.h>
#include <linux/seccomp.h>
#include <linux/uaccess.h>

#include <asm/byteorder.h>
#include <asm/cpu.h>
#include <asm/cpu-info.h>
#include <asm/fpu.h>
#include <asm/loongarch.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/processor.h>
#include <asm/reg.h>
#include <asm/syscall.h>

static void init_fp_ctx(struct task_struct *target)
{
	/* The target already has context */
	if (tsk_used_math(target))
		return;

	/* Begin with data registers set to all 1s... */
	memset(&target->thread.fpu.fpr, ~0, sizeof(target->thread.fpu.fpr));
	set_stopped_child_used_math(target);
}

/*
 * Called by kernel/ptrace.c when detaching..
 *
 * Make sure single step bits etc are not set.
 */
void ptrace_disable(struct task_struct *child)
{
	/* Don't load the watchpoint registers for the ex-child. */
	clear_tsk_thread_flag(child, TIF_LOAD_WATCH);
	clear_tsk_thread_flag(child, TIF_SINGLESTEP);
}

/* regset get/set implementations */

static int gpr_get(struct task_struct *target,
		   const struct user_regset *regset,
		   struct membuf to)
{
	int r;
	struct pt_regs *regs = task_pt_regs(target);

	r = membuf_write(&to, &regs->regs, sizeof(u64) * GPR_NUM);
	r = membuf_write(&to, &regs->orig_a0, sizeof(u64));
	r = membuf_write(&to, &regs->csr_era, sizeof(u64));
	r = membuf_write(&to, &regs->csr_badvaddr, sizeof(u64));

	return r;
}

static int gpr_set(struct task_struct *target,
		   const struct user_regset *regset,
		   unsigned int pos, unsigned int count,
		   const void *kbuf, const void __user *ubuf)
{
	int err;
	int a0_start = sizeof(u64) * GPR_NUM;
	int era_start = a0_start + sizeof(u64);
	int badvaddr_start = era_start + sizeof(u64);
	struct pt_regs *regs = task_pt_regs(target);

	err = user_regset_copyin(&pos, &count, &kbuf, &ubuf,
				 &regs->regs,
				 0, a0_start);
	err |= user_regset_copyin(&pos, &count, &kbuf, &ubuf,
				 &regs->orig_a0,
				 a0_start, a0_start + sizeof(u64));
	err |= user_regset_copyin(&pos, &count, &kbuf, &ubuf,
				 &regs->csr_era,
				 era_start, era_start + sizeof(u64));
	err |= user_regset_copyin(&pos, &count, &kbuf, &ubuf,
				 &regs->csr_badvaddr,
				 badvaddr_start, badvaddr_start + sizeof(u64));

	return err;
}


/*
 * Get the general floating-point registers.
 */
static int gfpr_get(struct task_struct *target, struct membuf *to)
{
	return membuf_write(to, &target->thread.fpu.fpr,
			    sizeof(elf_fpreg_t) * NUM_FPU_REGS);
}

static int gfpr_get_simd(struct task_struct *target, struct membuf *to)
{
	int i, r;
	u64 fpr_val;

	BUILD_BUG_ON(sizeof(fpr_val) != sizeof(elf_fpreg_t));
	for (i = 0; i < NUM_FPU_REGS; i++) {
		fpr_val = get_fpr64(&target->thread.fpu.fpr[i], 0);
		r = membuf_write(to, &fpr_val, sizeof(elf_fpreg_t));
	}

	return r;
}

/*
 * Choose the appropriate helper for general registers, and then copy
 * the FCC and FCSR registers separately.
 */
static int fpr_get(struct task_struct *target,
		   const struct user_regset *regset,
		   struct membuf to)
{
	int r;

	if (sizeof(target->thread.fpu.fpr[0]) == sizeof(elf_fpreg_t))
		r = gfpr_get(target, &to);
	else
		r = gfpr_get_simd(target, &to);

	r = membuf_write(&to, &target->thread.fpu.fcc, sizeof(target->thread.fpu.fcc));
	r = membuf_write(&to, &target->thread.fpu.fcsr, sizeof(target->thread.fpu.fcsr));

	return r;
}

static int gfpr_set(struct task_struct *target,
		    unsigned int *pos, unsigned int *count,
		    const void **kbuf, const void __user **ubuf)
{
	return user_regset_copyin(pos, count, kbuf, ubuf,
				  &target->thread.fpu.fpr,
				  0, NUM_FPU_REGS * sizeof(elf_fpreg_t));
}

static int gfpr_set_simd(struct task_struct *target,
		       unsigned int *pos, unsigned int *count,
		       const void **kbuf, const void __user **ubuf)
{
	int i, err;
	u64 fpr_val;

	BUILD_BUG_ON(sizeof(fpr_val) != sizeof(elf_fpreg_t));
	for (i = 0; i < NUM_FPU_REGS && *count > 0; i++) {
		err = user_regset_copyin(pos, count, kbuf, ubuf,
					 &fpr_val, i * sizeof(elf_fpreg_t),
					 (i + 1) * sizeof(elf_fpreg_t));
		if (err)
			return err;
		set_fpr64(&target->thread.fpu.fpr[i], 0, fpr_val);
	}

	return 0;
}

/*
 * Choose the appropriate helper for general registers, and then copy
 * the FCC register separately.
 */
static int fpr_set(struct task_struct *target,
		   const struct user_regset *regset,
		   unsigned int pos, unsigned int count,
		   const void *kbuf, const void __user *ubuf)
{
	const int fcc_start = NUM_FPU_REGS * sizeof(elf_fpreg_t);
	const int fcsr_start = fcc_start + sizeof(u64);
	int err;

	BUG_ON(count % sizeof(elf_fpreg_t));
	if (pos + count > sizeof(elf_fpregset_t))
		return -EIO;

	init_fp_ctx(target);

	if (sizeof(target->thread.fpu.fpr[0]) == sizeof(elf_fpreg_t))
		err = gfpr_set(target, &pos, &count, &kbuf, &ubuf);
	else
		err = gfpr_set_simd(target, &pos, &count, &kbuf, &ubuf);
	if (err)
		return err;

	err |= user_regset_copyin(&pos, &count, &kbuf, &ubuf,
				  &target->thread.fpu.fcc, fcc_start,
				  fcc_start + sizeof(u64));
	err |= user_regset_copyin(&pos, &count, &kbuf, &ubuf,
				  &target->thread.fpu.fcsr, fcsr_start,
				  fcsr_start + sizeof(u32));

	return err;
}

static int cfg_get(struct task_struct *target,
		   const struct user_regset *regset,
		   struct membuf to)
{
	int i, r;
	u32 cfg_val;

	i = 0;
	while (to.left > 0) {
		cfg_val = read_cpucfg(i++);
		r = membuf_write(&to, &cfg_val, sizeof(u32));
	}

	return r;
}

/*
 * CFG registers are read-only.
 */
static int cfg_set(struct task_struct *target,
		   const struct user_regset *regset,
		   unsigned int pos, unsigned int count,
		   const void *kbuf, const void __user *ubuf)
{
	return 0;
}

struct pt_regs_offset {
	const char *name;
	int offset;
};

#define REG_OFFSET_NAME(n, r) {.name = #n, .offset = offsetof(struct pt_regs, r)}
#define REG_OFFSET_END {.name = NULL, .offset = 0}

static const struct pt_regs_offset regoffset_table[] = {
	REG_OFFSET_NAME(r0, regs[0]),
	REG_OFFSET_NAME(r1, regs[1]),
	REG_OFFSET_NAME(r2, regs[2]),
	REG_OFFSET_NAME(r3, regs[3]),
	REG_OFFSET_NAME(r4, regs[4]),
	REG_OFFSET_NAME(r5, regs[5]),
	REG_OFFSET_NAME(r6, regs[6]),
	REG_OFFSET_NAME(r7, regs[7]),
	REG_OFFSET_NAME(r8, regs[8]),
	REG_OFFSET_NAME(r9, regs[9]),
	REG_OFFSET_NAME(r10, regs[10]),
	REG_OFFSET_NAME(r11, regs[11]),
	REG_OFFSET_NAME(r12, regs[12]),
	REG_OFFSET_NAME(r13, regs[13]),
	REG_OFFSET_NAME(r14, regs[14]),
	REG_OFFSET_NAME(r15, regs[15]),
	REG_OFFSET_NAME(r16, regs[16]),
	REG_OFFSET_NAME(r17, regs[17]),
	REG_OFFSET_NAME(r18, regs[18]),
	REG_OFFSET_NAME(r19, regs[19]),
	REG_OFFSET_NAME(r20, regs[20]),
	REG_OFFSET_NAME(r21, regs[21]),
	REG_OFFSET_NAME(r22, regs[22]),
	REG_OFFSET_NAME(r23, regs[23]),
	REG_OFFSET_NAME(r24, regs[24]),
	REG_OFFSET_NAME(r25, regs[25]),
	REG_OFFSET_NAME(r26, regs[26]),
	REG_OFFSET_NAME(r27, regs[27]),
	REG_OFFSET_NAME(r28, regs[28]),
	REG_OFFSET_NAME(r29, regs[29]),
	REG_OFFSET_NAME(r30, regs[30]),
	REG_OFFSET_NAME(r31, regs[31]),
	REG_OFFSET_NAME(orig_a0, orig_a0),
	REG_OFFSET_NAME(csr_era, csr_era),
	REG_OFFSET_NAME(csr_badvaddr, csr_badvaddr),
	REG_OFFSET_NAME(csr_crmd, csr_crmd),
	REG_OFFSET_NAME(csr_prmd, csr_prmd),
	REG_OFFSET_NAME(csr_euen, csr_euen),
	REG_OFFSET_NAME(csr_ecfg, csr_ecfg),
	REG_OFFSET_NAME(csr_estat, csr_estat),
	REG_OFFSET_END,
};

/**
 * regs_query_register_offset() - query register offset from its name
 * @name:       the name of a register
 *
 * regs_query_register_offset() returns the offset of a register in struct
 * pt_regs from its name. If the name is invalid, this returns -EINVAL;
 */
int regs_query_register_offset(const char *name)
{
	const struct pt_regs_offset *roff;

	for (roff = regoffset_table; roff->name != NULL; roff++)
		if (!strcmp(roff->name, name))
			return roff->offset;
	return -EINVAL;
}

enum loongarch_regset {
	REGSET_GPR,
	REGSET_FPR,
	REGSET_CPUCFG,
};

static const struct user_regset loongarch64_regsets[] = {
	[REGSET_GPR] = {
		.core_note_type	= NT_PRSTATUS,
		.n		= ELF_NGREG,
		.size		= sizeof(elf_greg_t),
		.align		= sizeof(elf_greg_t),
		.regset_get	= gpr_get,
		.set		= gpr_set,
	},
	[REGSET_FPR] = {
		.core_note_type	= NT_PRFPREG,
		.n		= ELF_NFPREG,
		.size		= sizeof(elf_fpreg_t),
		.align		= sizeof(elf_fpreg_t),
		.regset_get	= fpr_get,
		.set		= fpr_set,
	},
	[REGSET_CPUCFG] = {
		.core_note_type	= NT_LOONGARCH_CPUCFG,
		.n		= 64,
		.size		= sizeof(u32),
		.align		= sizeof(u32),
		.regset_get	= cfg_get,
		.set		= cfg_set,
	},
};

static const struct user_regset_view user_loongarch64_view = {
	.name		= "loongarch64",
	.e_machine	= ELF_ARCH,
	.regsets	= loongarch64_regsets,
	.n		= ARRAY_SIZE(loongarch64_regsets),
};


const struct user_regset_view *task_user_regset_view(struct task_struct *task)
{
	return &user_loongarch64_view;
}

static inline int read_user(struct task_struct *target, unsigned long addr,
			    unsigned long __user *data)
{
	unsigned long tmp = 0;

	switch (addr) {
	case 0 ... 31:
		tmp = task_pt_regs(target)->regs[addr];
		break;
	case ARG0:
		tmp = task_pt_regs(target)->orig_a0;
		break;
	case PC:
		tmp = task_pt_regs(target)->csr_era;
		break;
	case BADVADDR:
		tmp = task_pt_regs(target)->csr_badvaddr;
		break;
	default:
		return -EIO;
	}

	return put_user(tmp, data);
}

static inline int write_user(struct task_struct *target, unsigned long addr,
			    unsigned long data)
{
	switch (addr) {
	case 0 ... 31:
		task_pt_regs(target)->regs[addr] = data;
		break;
	case ARG0:
		task_pt_regs(target)->orig_a0 = data;
		break;
	case PC:
		task_pt_regs(target)->csr_era = data;
		break;
	case BADVADDR:
		task_pt_regs(target)->csr_badvaddr = data;
		break;
	default:
		return -EIO;
	}

	return 0;
}

long arch_ptrace(struct task_struct *child, long request,
		 unsigned long addr, unsigned long data)
{
	int ret;
	unsigned long __user *datap = (void __user *) data;

	switch (request) {
	case PTRACE_PEEKUSR:
		ret = read_user(child, addr, datap);
		break;

	case PTRACE_POKEUSR:
		ret = write_user(child, addr, data);
		break;

	default:
		ret = ptrace_request(child, request, addr, data);
		break;
	}

	return ret;
}
