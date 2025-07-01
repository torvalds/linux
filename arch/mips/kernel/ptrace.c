/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1992 Ross Biro
 * Copyright (C) Linus Torvalds
 * Copyright (C) 1994, 95, 96, 97, 98, 2000 Ralf Baechle
 * Copyright (C) 1996 David S. Miller
 * Kevin D. Kissell, kevink@mips.com and Carsten Langgaard, carstenl@mips.com
 * Copyright (C) 1999 MIPS Technologies, Inc.
 * Copyright (C) 2000 Ulf Carlsson
 *
 * At this time Linux/MIPS64 only supports syscall tracing, even for 32-bit
 * binaries.
 */
#include <linux/compiler.h>
#include <linux/context_tracking.h>
#include <linux/elf.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/sched/task_stack.h>
#include <linux/mm.h>
#include <linux/errno.h>
#include <linux/ptrace.h>
#include <linux/regset.h>
#include <linux/smp.h>
#include <linux/security.h>
#include <linux/stddef.h>
#include <linux/audit.h>
#include <linux/seccomp.h>
#include <linux/ftrace.h>

#include <asm/branch.h>
#include <asm/byteorder.h>
#include <asm/cpu.h>
#include <asm/cpu-info.h>
#include <asm/dsp.h>
#include <asm/fpu.h>
#include <asm/mipsregs.h>
#include <asm/mipsmtregs.h>
#include <asm/page.h>
#include <asm/processor.h>
#include <asm/syscall.h>
#include <linux/uaccess.h>
#include <asm/bootinfo.h>
#include <asm/reg.h>

#define CREATE_TRACE_POINTS
#include <trace/events/syscalls.h>

unsigned long exception_ip(struct pt_regs *regs)
{
	return exception_epc(regs);
}
EXPORT_SYMBOL(exception_ip);

/*
 * Called by kernel/ptrace.c when detaching..
 *
 * Make sure single step bits etc are not set.
 */
void ptrace_disable(struct task_struct *child)
{
	/* Don't load the watchpoint registers for the ex-child. */
	clear_tsk_thread_flag(child, TIF_LOAD_WATCH);
}

/*
 * Read a general register set.	 We always use the 64-bit format, even
 * for 32-bit kernels and for 32-bit processes on a 64-bit kernel.
 * Registers are sign extended to fill the available space.
 */
int ptrace_getregs(struct task_struct *child, struct user_pt_regs __user *data)
{
	struct pt_regs *regs;
	int i;

	if (!access_ok(data, 38 * 8))
		return -EIO;

	regs = task_pt_regs(child);

	for (i = 0; i < 32; i++)
		__put_user((long)regs->regs[i], (__s64 __user *)&data->regs[i]);
	__put_user((long)regs->lo, (__s64 __user *)&data->lo);
	__put_user((long)regs->hi, (__s64 __user *)&data->hi);
	__put_user((long)regs->cp0_epc, (__s64 __user *)&data->cp0_epc);
	__put_user((long)regs->cp0_badvaddr, (__s64 __user *)&data->cp0_badvaddr);
	__put_user((long)regs->cp0_status, (__s64 __user *)&data->cp0_status);
	__put_user((long)regs->cp0_cause, (__s64 __user *)&data->cp0_cause);

	return 0;
}

/*
 * Write a general register set.  As for PTRACE_GETREGS, we always use
 * the 64-bit format.  On a 32-bit kernel only the lower order half
 * (according to endianness) will be used.
 */
int ptrace_setregs(struct task_struct *child, struct user_pt_regs __user *data)
{
	struct pt_regs *regs;
	int i;

	if (!access_ok(data, 38 * 8))
		return -EIO;

	regs = task_pt_regs(child);

	for (i = 0; i < 32; i++)
		__get_user(regs->regs[i], (__s64 __user *)&data->regs[i]);
	__get_user(regs->lo, (__s64 __user *)&data->lo);
	__get_user(regs->hi, (__s64 __user *)&data->hi);
	__get_user(regs->cp0_epc, (__s64 __user *)&data->cp0_epc);

	/* badvaddr, status, and cause may not be written.  */

	/* System call number may have been changed */
	mips_syscall_update_nr(child, regs);

	return 0;
}

int ptrace_get_watch_regs(struct task_struct *child,
			  struct pt_watch_regs __user *addr)
{
	enum pt_watch_style style;
	int i;

	if (!cpu_has_watch || boot_cpu_data.watch_reg_use_cnt == 0)
		return -EIO;
	if (!access_ok(addr, sizeof(struct pt_watch_regs)))
		return -EIO;

#ifdef CONFIG_32BIT
	style = pt_watch_style_mips32;
#define WATCH_STYLE mips32
#else
	style = pt_watch_style_mips64;
#define WATCH_STYLE mips64
#endif

	__put_user(style, &addr->style);
	__put_user(boot_cpu_data.watch_reg_use_cnt,
		   &addr->WATCH_STYLE.num_valid);
	for (i = 0; i < boot_cpu_data.watch_reg_use_cnt; i++) {
		__put_user(child->thread.watch.mips3264.watchlo[i],
			   &addr->WATCH_STYLE.watchlo[i]);
		__put_user(child->thread.watch.mips3264.watchhi[i] &
				(MIPS_WATCHHI_MASK | MIPS_WATCHHI_IRW),
			   &addr->WATCH_STYLE.watchhi[i]);
		__put_user(boot_cpu_data.watch_reg_masks[i],
			   &addr->WATCH_STYLE.watch_masks[i]);
	}
	for (; i < 8; i++) {
		__put_user(0, &addr->WATCH_STYLE.watchlo[i]);
		__put_user(0, &addr->WATCH_STYLE.watchhi[i]);
		__put_user(0, &addr->WATCH_STYLE.watch_masks[i]);
	}

	return 0;
}

int ptrace_set_watch_regs(struct task_struct *child,
			  struct pt_watch_regs __user *addr)
{
	int i;
	int watch_active = 0;
	unsigned long lt[NUM_WATCH_REGS];
	u16 ht[NUM_WATCH_REGS];

	if (!cpu_has_watch || boot_cpu_data.watch_reg_use_cnt == 0)
		return -EIO;
	if (!access_ok(addr, sizeof(struct pt_watch_regs)))
		return -EIO;
	/* Check the values. */
	for (i = 0; i < boot_cpu_data.watch_reg_use_cnt; i++) {
		__get_user(lt[i], &addr->WATCH_STYLE.watchlo[i]);
#ifdef CONFIG_32BIT
		if (lt[i] & __UA_LIMIT)
			return -EINVAL;
#else
		if (test_tsk_thread_flag(child, TIF_32BIT_ADDR)) {
			if (lt[i] & 0xffffffff80000000UL)
				return -EINVAL;
		} else {
			if (lt[i] & __UA_LIMIT)
				return -EINVAL;
		}
#endif
		__get_user(ht[i], &addr->WATCH_STYLE.watchhi[i]);
		if (ht[i] & ~MIPS_WATCHHI_MASK)
			return -EINVAL;
	}
	/* Install them. */
	for (i = 0; i < boot_cpu_data.watch_reg_use_cnt; i++) {
		if (lt[i] & MIPS_WATCHLO_IRW)
			watch_active = 1;
		child->thread.watch.mips3264.watchlo[i] = lt[i];
		/* Set the G bit. */
		child->thread.watch.mips3264.watchhi[i] = ht[i];
	}

	if (watch_active)
		set_tsk_thread_flag(child, TIF_LOAD_WATCH);
	else
		clear_tsk_thread_flag(child, TIF_LOAD_WATCH);

	return 0;
}

/* regset get/set implementations */

#if defined(CONFIG_32BIT) || defined(CONFIG_MIPS32_O32)

static int gpr32_get(struct task_struct *target,
		     const struct user_regset *regset,
		     struct membuf to)
{
	struct pt_regs *regs = task_pt_regs(target);
	u32 uregs[ELF_NGREG] = {};

	mips_dump_regs32(uregs, regs);
	return membuf_write(&to, uregs, sizeof(uregs));
}

static int gpr32_set(struct task_struct *target,
		     const struct user_regset *regset,
		     unsigned int pos, unsigned int count,
		     const void *kbuf, const void __user *ubuf)
{
	struct pt_regs *regs = task_pt_regs(target);
	u32 uregs[ELF_NGREG];
	unsigned start, num_regs, i;
	int err;

	start = pos / sizeof(u32);
	num_regs = count / sizeof(u32);

	if (start + num_regs > ELF_NGREG)
		return -EIO;

	err = user_regset_copyin(&pos, &count, &kbuf, &ubuf, uregs, 0,
				 sizeof(uregs));
	if (err)
		return err;

	for (i = start; i < num_regs; i++) {
		/*
		 * Cast all values to signed here so that if this is a 64-bit
		 * kernel, the supplied 32-bit values will be sign extended.
		 */
		switch (i) {
		case MIPS32_EF_R1 ... MIPS32_EF_R25:
			/* k0/k1 are ignored. */
		case MIPS32_EF_R28 ... MIPS32_EF_R31:
			regs->regs[i - MIPS32_EF_R0] = (s32)uregs[i];
			break;
		case MIPS32_EF_LO:
			regs->lo = (s32)uregs[i];
			break;
		case MIPS32_EF_HI:
			regs->hi = (s32)uregs[i];
			break;
		case MIPS32_EF_CP0_EPC:
			regs->cp0_epc = (s32)uregs[i];
			break;
		}
	}

	/* System call number may have been changed */
	mips_syscall_update_nr(target, regs);

	return 0;
}

#endif /* CONFIG_32BIT || CONFIG_MIPS32_O32 */

#ifdef CONFIG_64BIT

static int gpr64_get(struct task_struct *target,
		     const struct user_regset *regset,
		     struct membuf to)
{
	struct pt_regs *regs = task_pt_regs(target);
	u64 uregs[ELF_NGREG] = {};

	mips_dump_regs64(uregs, regs);
	return membuf_write(&to, uregs, sizeof(uregs));
}

static int gpr64_set(struct task_struct *target,
		     const struct user_regset *regset,
		     unsigned int pos, unsigned int count,
		     const void *kbuf, const void __user *ubuf)
{
	struct pt_regs *regs = task_pt_regs(target);
	u64 uregs[ELF_NGREG];
	unsigned start, num_regs, i;
	int err;

	start = pos / sizeof(u64);
	num_regs = count / sizeof(u64);

	if (start + num_regs > ELF_NGREG)
		return -EIO;

	err = user_regset_copyin(&pos, &count, &kbuf, &ubuf, uregs, 0,
				 sizeof(uregs));
	if (err)
		return err;

	for (i = start; i < num_regs; i++) {
		switch (i) {
		case MIPS64_EF_R1 ... MIPS64_EF_R25:
			/* k0/k1 are ignored. */
		case MIPS64_EF_R28 ... MIPS64_EF_R31:
			regs->regs[i - MIPS64_EF_R0] = uregs[i];
			break;
		case MIPS64_EF_LO:
			regs->lo = uregs[i];
			break;
		case MIPS64_EF_HI:
			regs->hi = uregs[i];
			break;
		case MIPS64_EF_CP0_EPC:
			regs->cp0_epc = uregs[i];
			break;
		}
	}

	/* System call number may have been changed */
	mips_syscall_update_nr(target, regs);

	return 0;
}

#endif /* CONFIG_64BIT */


#ifdef CONFIG_MIPS_FP_SUPPORT

/*
 * Poke at FCSR according to its mask.  Set the Cause bits even
 * if a corresponding Enable bit is set.  This will be noticed at
 * the time the thread is switched to and SIGFPE thrown accordingly.
 */
static void ptrace_setfcr31(struct task_struct *child, u32 value)
{
	u32 fcr31;
	u32 mask;

	fcr31 = child->thread.fpu.fcr31;
	mask = boot_cpu_data.fpu_msk31;
	child->thread.fpu.fcr31 = (value & ~mask) | (fcr31 & mask);
}

int ptrace_getfpregs(struct task_struct *child, __u32 __user *data)
{
	int i;

	if (!access_ok(data, 33 * 8))
		return -EIO;

	if (tsk_used_math(child)) {
		union fpureg *fregs = get_fpu_regs(child);
		for (i = 0; i < 32; i++)
			__put_user(get_fpr64(&fregs[i], 0),
				   i + (__u64 __user *)data);
	} else {
		for (i = 0; i < 32; i++)
			__put_user((__u64) -1, i + (__u64 __user *) data);
	}

	__put_user(child->thread.fpu.fcr31, data + 64);
	__put_user(boot_cpu_data.fpu_id, data + 65);

	return 0;
}

int ptrace_setfpregs(struct task_struct *child, __u32 __user *data)
{
	union fpureg *fregs;
	u64 fpr_val;
	u32 value;
	int i;

	if (!access_ok(data, 33 * 8))
		return -EIO;

	init_fp_ctx(child);
	fregs = get_fpu_regs(child);

	for (i = 0; i < 32; i++) {
		__get_user(fpr_val, i + (__u64 __user *)data);
		set_fpr64(&fregs[i], 0, fpr_val);
	}

	__get_user(value, data + 64);
	ptrace_setfcr31(child, value);

	/* FIR may not be written.  */

	return 0;
}

/*
 * Copy the floating-point context to the supplied NT_PRFPREG buffer,
 * !CONFIG_CPU_HAS_MSA variant.  FP context's general register slots
 * correspond 1:1 to buffer slots.  Only general registers are copied.
 */
static void fpr_get_fpa(struct task_struct *target,
		       struct membuf *to)
{
	membuf_write(to, &target->thread.fpu,
			NUM_FPU_REGS * sizeof(elf_fpreg_t));
}

/*
 * Copy the floating-point context to the supplied NT_PRFPREG buffer,
 * CONFIG_CPU_HAS_MSA variant.  Only lower 64 bits of FP context's
 * general register slots are copied to buffer slots.  Only general
 * registers are copied.
 */
static void fpr_get_msa(struct task_struct *target, struct membuf *to)
{
	unsigned int i;

	BUILD_BUG_ON(sizeof(u64) != sizeof(elf_fpreg_t));
	for (i = 0; i < NUM_FPU_REGS; i++)
		membuf_store(to, get_fpr64(&target->thread.fpu.fpr[i], 0));
}

/*
 * Copy the floating-point context to the supplied NT_PRFPREG buffer.
 * Choose the appropriate helper for general registers, and then copy
 * the FCSR and FIR registers separately.
 */
static int fpr_get(struct task_struct *target,
		   const struct user_regset *regset,
		   struct membuf to)
{
	if (sizeof(target->thread.fpu.fpr[0]) == sizeof(elf_fpreg_t))
		fpr_get_fpa(target, &to);
	else
		fpr_get_msa(target, &to);

	membuf_write(&to, &target->thread.fpu.fcr31, sizeof(u32));
	membuf_write(&to, &boot_cpu_data.fpu_id, sizeof(u32));
	return 0;
}

/*
 * Copy the supplied NT_PRFPREG buffer to the floating-point context,
 * !CONFIG_CPU_HAS_MSA variant.   Buffer slots correspond 1:1 to FP
 * context's general register slots.  Only general registers are copied.
 */
static int fpr_set_fpa(struct task_struct *target,
		       unsigned int *pos, unsigned int *count,
		       const void **kbuf, const void __user **ubuf)
{
	return user_regset_copyin(pos, count, kbuf, ubuf,
				  &target->thread.fpu,
				  0, NUM_FPU_REGS * sizeof(elf_fpreg_t));
}

/*
 * Copy the supplied NT_PRFPREG buffer to the floating-point context,
 * CONFIG_CPU_HAS_MSA variant.  Buffer slots are copied to lower 64
 * bits only of FP context's general register slots.  Only general
 * registers are copied.
 */
static int fpr_set_msa(struct task_struct *target,
		       unsigned int *pos, unsigned int *count,
		       const void **kbuf, const void __user **ubuf)
{
	unsigned int i;
	u64 fpr_val;
	int err;

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
 * Copy the supplied NT_PRFPREG buffer to the floating-point context.
 * Choose the appropriate helper for general registers, and then copy
 * the FCSR register separately.  Ignore the incoming FIR register
 * contents though, as the register is read-only.
 *
 * We optimize for the case where `count % sizeof(elf_fpreg_t) == 0',
 * which is supposed to have been guaranteed by the kernel before
 * calling us, e.g. in `ptrace_regset'.  We enforce that requirement,
 * so that we can safely avoid preinitializing temporaries for
 * partial register writes.
 */
static int fpr_set(struct task_struct *target,
		   const struct user_regset *regset,
		   unsigned int pos, unsigned int count,
		   const void *kbuf, const void __user *ubuf)
{
	const int fcr31_pos = NUM_FPU_REGS * sizeof(elf_fpreg_t);
	const int fir_pos = fcr31_pos + sizeof(u32);
	u32 fcr31;
	int err;

	BUG_ON(count % sizeof(elf_fpreg_t));

	if (pos + count > sizeof(elf_fpregset_t))
		return -EIO;

	init_fp_ctx(target);

	if (sizeof(target->thread.fpu.fpr[0]) == sizeof(elf_fpreg_t))
		err = fpr_set_fpa(target, &pos, &count, &kbuf, &ubuf);
	else
		err = fpr_set_msa(target, &pos, &count, &kbuf, &ubuf);
	if (err)
		return err;

	if (count > 0) {
		err = user_regset_copyin(&pos, &count, &kbuf, &ubuf,
					 &fcr31,
					 fcr31_pos, fcr31_pos + sizeof(u32));
		if (err)
			return err;

		ptrace_setfcr31(target, fcr31);
	}

	if (count > 0) {
		user_regset_copyin_ignore(&pos, &count, &kbuf, &ubuf,
					  fir_pos, fir_pos + sizeof(u32));
		return 0;
	}

	return err;
}

/* Copy the FP mode setting to the supplied NT_MIPS_FP_MODE buffer.  */
static int fp_mode_get(struct task_struct *target,
		       const struct user_regset *regset,
		       struct membuf to)
{
	return membuf_store(&to, (int)mips_get_process_fp_mode(target));
}

/*
 * Copy the supplied NT_MIPS_FP_MODE buffer to the FP mode setting.
 *
 * We optimize for the case where `count % sizeof(int) == 0', which
 * is supposed to have been guaranteed by the kernel before calling
 * us, e.g. in `ptrace_regset'.  We enforce that requirement, so
 * that we can safely avoid preinitializing temporaries for partial
 * mode writes.
 */
static int fp_mode_set(struct task_struct *target,
		       const struct user_regset *regset,
		       unsigned int pos, unsigned int count,
		       const void *kbuf, const void __user *ubuf)
{
	int fp_mode;
	int err;

	BUG_ON(count % sizeof(int));

	if (pos + count > sizeof(fp_mode))
		return -EIO;

	err = user_regset_copyin(&pos, &count, &kbuf, &ubuf, &fp_mode, 0,
				 sizeof(fp_mode));
	if (err)
		return err;

	if (count > 0)
		err = mips_set_process_fp_mode(target, fp_mode);

	return err;
}

#endif /* CONFIG_MIPS_FP_SUPPORT */

#ifdef CONFIG_CPU_HAS_MSA

struct msa_control_regs {
	unsigned int fir;
	unsigned int fcsr;
	unsigned int msair;
	unsigned int msacsr;
};

static void copy_pad_fprs(struct task_struct *target,
			 const struct user_regset *regset,
			 struct membuf *to,
			 unsigned int live_sz)
{
	int i, j;
	unsigned long long fill = ~0ull;
	unsigned int cp_sz, pad_sz;

	cp_sz = min(regset->size, live_sz);
	pad_sz = regset->size - cp_sz;
	WARN_ON(pad_sz % sizeof(fill));

	for (i = 0; i < NUM_FPU_REGS; i++) {
		membuf_write(to, &target->thread.fpu.fpr[i], cp_sz);
		for (j = 0; j < (pad_sz / sizeof(fill)); j++)
			membuf_store(to, fill);
	}
}

static int msa_get(struct task_struct *target,
		   const struct user_regset *regset,
		   struct membuf to)
{
	const unsigned int wr_size = NUM_FPU_REGS * regset->size;
	const struct msa_control_regs ctrl_regs = {
		.fir = boot_cpu_data.fpu_id,
		.fcsr = target->thread.fpu.fcr31,
		.msair = boot_cpu_data.msa_id,
		.msacsr = target->thread.fpu.msacsr,
	};

	if (!tsk_used_math(target)) {
		/* The task hasn't used FP or MSA, fill with 0xff */
		copy_pad_fprs(target, regset, &to, 0);
	} else if (!test_tsk_thread_flag(target, TIF_MSA_CTX_LIVE)) {
		/* Copy scalar FP context, fill the rest with 0xff */
		copy_pad_fprs(target, regset, &to, 8);
	} else if (sizeof(target->thread.fpu.fpr[0]) == regset->size) {
		/* Trivially copy the vector registers */
		membuf_write(&to, &target->thread.fpu.fpr, wr_size);
	} else {
		/* Copy as much context as possible, fill the rest with 0xff */
		copy_pad_fprs(target, regset, &to,
				sizeof(target->thread.fpu.fpr[0]));
	}

	return membuf_write(&to, &ctrl_regs, sizeof(ctrl_regs));
}

static int msa_set(struct task_struct *target,
		   const struct user_regset *regset,
		   unsigned int pos, unsigned int count,
		   const void *kbuf, const void __user *ubuf)
{
	const unsigned int wr_size = NUM_FPU_REGS * regset->size;
	struct msa_control_regs ctrl_regs;
	unsigned int cp_sz;
	int i, err, start;

	init_fp_ctx(target);

	if (sizeof(target->thread.fpu.fpr[0]) == regset->size) {
		/* Trivially copy the vector registers */
		err = user_regset_copyin(&pos, &count, &kbuf, &ubuf,
					 &target->thread.fpu.fpr,
					 0, wr_size);
	} else {
		/* Copy as much context as possible */
		cp_sz = min_t(unsigned int, regset->size,
			      sizeof(target->thread.fpu.fpr[0]));

		i = start = err = 0;
		for (; i < NUM_FPU_REGS; i++, start += regset->size) {
			err |= user_regset_copyin(&pos, &count, &kbuf, &ubuf,
						  &target->thread.fpu.fpr[i],
						  start, start + cp_sz);
		}
	}

	if (!err)
		err = user_regset_copyin(&pos, &count, &kbuf, &ubuf, &ctrl_regs,
					 wr_size, wr_size + sizeof(ctrl_regs));
	if (!err) {
		target->thread.fpu.fcr31 = ctrl_regs.fcsr & ~FPU_CSR_ALL_X;
		target->thread.fpu.msacsr = ctrl_regs.msacsr & ~MSA_CSR_CAUSEF;
	}

	return err;
}

#endif /* CONFIG_CPU_HAS_MSA */

#if defined(CONFIG_32BIT) || defined(CONFIG_MIPS32_O32)

/*
 * Copy the DSP context to the supplied 32-bit NT_MIPS_DSP buffer.
 */
static int dsp32_get(struct task_struct *target,
		     const struct user_regset *regset,
		     struct membuf to)
{
	u32 dspregs[NUM_DSP_REGS + 1];
	unsigned int i;

	BUG_ON(to.left % sizeof(u32));

	if (!cpu_has_dsp)
		return -EIO;

	for (i = 0; i < NUM_DSP_REGS; i++)
		dspregs[i] = target->thread.dsp.dspr[i];
	dspregs[NUM_DSP_REGS] = target->thread.dsp.dspcontrol;
	return membuf_write(&to, dspregs, sizeof(dspregs));
}

/*
 * Copy the supplied 32-bit NT_MIPS_DSP buffer to the DSP context.
 */
static int dsp32_set(struct task_struct *target,
		     const struct user_regset *regset,
		     unsigned int pos, unsigned int count,
		     const void *kbuf, const void __user *ubuf)
{
	unsigned int start, num_regs, i;
	u32 dspregs[NUM_DSP_REGS + 1];
	int err;

	BUG_ON(count % sizeof(u32));

	if (!cpu_has_dsp)
		return -EIO;

	start = pos / sizeof(u32);
	num_regs = count / sizeof(u32);

	if (start + num_regs > NUM_DSP_REGS + 1)
		return -EIO;

	err = user_regset_copyin(&pos, &count, &kbuf, &ubuf, dspregs, 0,
				 sizeof(dspregs));
	if (err)
		return err;

	for (i = start; i < num_regs; i++)
		switch (i) {
		case 0 ... NUM_DSP_REGS - 1:
			target->thread.dsp.dspr[i] = (s32)dspregs[i];
			break;
		case NUM_DSP_REGS:
			target->thread.dsp.dspcontrol = (s32)dspregs[i];
			break;
		}

	return 0;
}

#endif /* CONFIG_32BIT || CONFIG_MIPS32_O32 */

#ifdef CONFIG_64BIT

/*
 * Copy the DSP context to the supplied 64-bit NT_MIPS_DSP buffer.
 */
static int dsp64_get(struct task_struct *target,
		     const struct user_regset *regset,
		     struct membuf to)
{
	u64 dspregs[NUM_DSP_REGS + 1];
	unsigned int i;

	BUG_ON(to.left % sizeof(u64));

	if (!cpu_has_dsp)
		return -EIO;

	for (i = 0; i < NUM_DSP_REGS; i++)
		dspregs[i] = target->thread.dsp.dspr[i];
	dspregs[NUM_DSP_REGS] = target->thread.dsp.dspcontrol;
	return membuf_write(&to, dspregs, sizeof(dspregs));
}

/*
 * Copy the supplied 64-bit NT_MIPS_DSP buffer to the DSP context.
 */
static int dsp64_set(struct task_struct *target,
		     const struct user_regset *regset,
		     unsigned int pos, unsigned int count,
		     const void *kbuf, const void __user *ubuf)
{
	unsigned int start, num_regs, i;
	u64 dspregs[NUM_DSP_REGS + 1];
	int err;

	BUG_ON(count % sizeof(u64));

	if (!cpu_has_dsp)
		return -EIO;

	start = pos / sizeof(u64);
	num_regs = count / sizeof(u64);

	if (start + num_regs > NUM_DSP_REGS + 1)
		return -EIO;

	err = user_regset_copyin(&pos, &count, &kbuf, &ubuf, dspregs, 0,
				 sizeof(dspregs));
	if (err)
		return err;

	for (i = start; i < num_regs; i++)
		switch (i) {
		case 0 ... NUM_DSP_REGS - 1:
			target->thread.dsp.dspr[i] = dspregs[i];
			break;
		case NUM_DSP_REGS:
			target->thread.dsp.dspcontrol = dspregs[i];
			break;
		}

	return 0;
}

#endif /* CONFIG_64BIT */

/*
 * Determine whether the DSP context is present.
 */
static int dsp_active(struct task_struct *target,
		      const struct user_regset *regset)
{
	return cpu_has_dsp ? NUM_DSP_REGS + 1 : -ENODEV;
}

enum mips_regset {
	REGSET_GPR,
	REGSET_DSP,
#ifdef CONFIG_MIPS_FP_SUPPORT
	REGSET_FPR,
	REGSET_FP_MODE,
#endif
#ifdef CONFIG_CPU_HAS_MSA
	REGSET_MSA,
#endif
};

struct pt_regs_offset {
	const char *name;
	int offset;
};

#define REG_OFFSET_NAME(reg, r) {					\
	.name = #reg,							\
	.offset = offsetof(struct pt_regs, r)				\
}

#define REG_OFFSET_END {						\
	.name = NULL,							\
	.offset = 0							\
}

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
	REG_OFFSET_NAME(c0_status, cp0_status),
	REG_OFFSET_NAME(hi, hi),
	REG_OFFSET_NAME(lo, lo),
#ifdef CONFIG_CPU_HAS_SMARTMIPS
	REG_OFFSET_NAME(acx, acx),
#endif
	REG_OFFSET_NAME(c0_badvaddr, cp0_badvaddr),
	REG_OFFSET_NAME(c0_cause, cp0_cause),
	REG_OFFSET_NAME(c0_epc, cp0_epc),
#ifdef CONFIG_CPU_CAVIUM_OCTEON
	REG_OFFSET_NAME(mpl0, mpl[0]),
	REG_OFFSET_NAME(mpl1, mpl[1]),
	REG_OFFSET_NAME(mpl2, mpl[2]),
	REG_OFFSET_NAME(mtp0, mtp[0]),
	REG_OFFSET_NAME(mtp1, mtp[1]),
	REG_OFFSET_NAME(mtp2, mtp[2]),
#endif
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

#if defined(CONFIG_32BIT) || defined(CONFIG_MIPS32_O32)

static const struct user_regset mips_regsets[] = {
	[REGSET_GPR] = {
		USER_REGSET_NOTE_TYPE(PRSTATUS),
		.n		= ELF_NGREG,
		.size		= sizeof(unsigned int),
		.align		= sizeof(unsigned int),
		.regset_get	= gpr32_get,
		.set		= gpr32_set,
	},
	[REGSET_DSP] = {
		USER_REGSET_NOTE_TYPE(MIPS_DSP),
		.n		= NUM_DSP_REGS + 1,
		.size		= sizeof(u32),
		.align		= sizeof(u32),
		.regset_get	= dsp32_get,
		.set		= dsp32_set,
		.active		= dsp_active,
	},
#ifdef CONFIG_MIPS_FP_SUPPORT
	[REGSET_FPR] = {
		USER_REGSET_NOTE_TYPE(PRFPREG),
		.n		= ELF_NFPREG,
		.size		= sizeof(elf_fpreg_t),
		.align		= sizeof(elf_fpreg_t),
		.regset_get	= fpr_get,
		.set		= fpr_set,
	},
	[REGSET_FP_MODE] = {
		USER_REGSET_NOTE_TYPE(MIPS_FP_MODE),
		.n		= 1,
		.size		= sizeof(int),
		.align		= sizeof(int),
		.regset_get	= fp_mode_get,
		.set		= fp_mode_set,
	},
#endif
#ifdef CONFIG_CPU_HAS_MSA
	[REGSET_MSA] = {
		USER_REGSET_NOTE_TYPE(MIPS_MSA),
		.n		= NUM_FPU_REGS + 1,
		.size		= 16,
		.align		= 16,
		.regset_get	= msa_get,
		.set		= msa_set,
	},
#endif
};

static const struct user_regset_view user_mips_view = {
	.name		= "mips",
	.e_machine	= ELF_ARCH,
	.ei_osabi	= ELF_OSABI,
	.regsets	= mips_regsets,
	.n		= ARRAY_SIZE(mips_regsets),
};

#endif /* CONFIG_32BIT || CONFIG_MIPS32_O32 */

#ifdef CONFIG_64BIT

static const struct user_regset mips64_regsets[] = {
	[REGSET_GPR] = {
		USER_REGSET_NOTE_TYPE(PRSTATUS),
		.n		= ELF_NGREG,
		.size		= sizeof(unsigned long),
		.align		= sizeof(unsigned long),
		.regset_get	= gpr64_get,
		.set		= gpr64_set,
	},
	[REGSET_DSP] = {
		USER_REGSET_NOTE_TYPE(MIPS_DSP),
		.n		= NUM_DSP_REGS + 1,
		.size		= sizeof(u64),
		.align		= sizeof(u64),
		.regset_get	= dsp64_get,
		.set		= dsp64_set,
		.active		= dsp_active,
	},
#ifdef CONFIG_MIPS_FP_SUPPORT
	[REGSET_FP_MODE] = {
		USER_REGSET_NOTE_TYPE(MIPS_FP_MODE),
		.n		= 1,
		.size		= sizeof(int),
		.align		= sizeof(int),
		.regset_get	= fp_mode_get,
		.set		= fp_mode_set,
	},
	[REGSET_FPR] = {
		USER_REGSET_NOTE_TYPE(PRFPREG),
		.n		= ELF_NFPREG,
		.size		= sizeof(elf_fpreg_t),
		.align		= sizeof(elf_fpreg_t),
		.regset_get	= fpr_get,
		.set		= fpr_set,
	},
#endif
#ifdef CONFIG_CPU_HAS_MSA
	[REGSET_MSA] = {
		USER_REGSET_NOTE_TYPE(MIPS_MSA),
		.n		= NUM_FPU_REGS + 1,
		.size		= 16,
		.align		= 16,
		.regset_get	= msa_get,
		.set		= msa_set,
	},
#endif
};

static const struct user_regset_view user_mips64_view = {
	.name		= "mips64",
	.e_machine	= ELF_ARCH,
	.ei_osabi	= ELF_OSABI,
	.regsets	= mips64_regsets,
	.n		= ARRAY_SIZE(mips64_regsets),
};

#ifdef CONFIG_MIPS32_N32

static const struct user_regset_view user_mipsn32_view = {
	.name		= "mipsn32",
	.e_flags	= EF_MIPS_ABI2,
	.e_machine	= ELF_ARCH,
	.ei_osabi	= ELF_OSABI,
	.regsets	= mips64_regsets,
	.n		= ARRAY_SIZE(mips64_regsets),
};

#endif /* CONFIG_MIPS32_N32 */

#endif /* CONFIG_64BIT */

const struct user_regset_view *task_user_regset_view(struct task_struct *task)
{
#ifdef CONFIG_32BIT
	return &user_mips_view;
#else
#ifdef CONFIG_MIPS32_O32
	if (test_tsk_thread_flag(task, TIF_32BIT_REGS))
		return &user_mips_view;
#endif
#ifdef CONFIG_MIPS32_N32
	if (test_tsk_thread_flag(task, TIF_32BIT_ADDR))
		return &user_mipsn32_view;
#endif
	return &user_mips64_view;
#endif
}

long arch_ptrace(struct task_struct *child, long request,
		 unsigned long addr, unsigned long data)
{
	int ret;
	void __user *addrp = (void __user *) addr;
	void __user *datavp = (void __user *) data;
	unsigned long __user *datalp = (void __user *) data;

	switch (request) {
	/* when I and D space are separate, these will need to be fixed. */
	case PTRACE_PEEKTEXT: /* read word at location addr. */
	case PTRACE_PEEKDATA:
		ret = generic_ptrace_peekdata(child, addr, data);
		break;

	/* Read the word at location addr in the USER area. */
	case PTRACE_PEEKUSR: {
		struct pt_regs *regs;
		unsigned long tmp = 0;

		regs = task_pt_regs(child);
		ret = 0;  /* Default return value. */

		switch (addr) {
		case 0 ... 31:
			tmp = regs->regs[addr];
			break;
#ifdef CONFIG_MIPS_FP_SUPPORT
		case FPR_BASE ... FPR_BASE + 31: {
			union fpureg *fregs;

			if (!tsk_used_math(child)) {
				/* FP not yet used */
				tmp = -1;
				break;
			}
			fregs = get_fpu_regs(child);

#ifdef CONFIG_32BIT
			if (test_tsk_thread_flag(child, TIF_32BIT_FPREGS)) {
				/*
				 * The odd registers are actually the high
				 * order bits of the values stored in the even
				 * registers.
				 */
				tmp = get_fpr32(&fregs[(addr & ~1) - FPR_BASE],
						addr & 1);
				break;
			}
#endif
			tmp = get_fpr64(&fregs[addr - FPR_BASE], 0);
			break;
		}
		case FPC_CSR:
			tmp = child->thread.fpu.fcr31;
			break;
		case FPC_EIR:
			/* implementation / version register */
			tmp = boot_cpu_data.fpu_id;
			break;
#endif
		case PC:
			tmp = regs->cp0_epc;
			break;
		case CAUSE:
			tmp = regs->cp0_cause;
			break;
		case BADVADDR:
			tmp = regs->cp0_badvaddr;
			break;
		case MMHI:
			tmp = regs->hi;
			break;
		case MMLO:
			tmp = regs->lo;
			break;
#ifdef CONFIG_CPU_HAS_SMARTMIPS
		case ACX:
			tmp = regs->acx;
			break;
#endif
		case DSP_BASE ... DSP_BASE + 5: {
			dspreg_t *dregs;

			if (!cpu_has_dsp) {
				tmp = 0;
				ret = -EIO;
				goto out;
			}
			dregs = __get_dsp_regs(child);
			tmp = dregs[addr - DSP_BASE];
			break;
		}
		case DSP_CONTROL:
			if (!cpu_has_dsp) {
				tmp = 0;
				ret = -EIO;
				goto out;
			}
			tmp = child->thread.dsp.dspcontrol;
			break;
		default:
			tmp = 0;
			ret = -EIO;
			goto out;
		}
		ret = put_user(tmp, datalp);
		break;
	}

	/* when I and D space are separate, this will have to be fixed. */
	case PTRACE_POKETEXT: /* write the word at location addr. */
	case PTRACE_POKEDATA:
		ret = generic_ptrace_pokedata(child, addr, data);
		break;

	case PTRACE_POKEUSR: {
		struct pt_regs *regs;
		ret = 0;
		regs = task_pt_regs(child);

		switch (addr) {
		case 0 ... 31:
			regs->regs[addr] = data;
			/* System call number may have been changed */
			if (addr == 2)
				mips_syscall_update_nr(child, regs);
			else if (addr == 4 &&
				 mips_syscall_is_indirect(child, regs))
				mips_syscall_update_nr(child, regs);
			break;
#ifdef CONFIG_MIPS_FP_SUPPORT
		case FPR_BASE ... FPR_BASE + 31: {
			union fpureg *fregs = get_fpu_regs(child);

			init_fp_ctx(child);
#ifdef CONFIG_32BIT
			if (test_tsk_thread_flag(child, TIF_32BIT_FPREGS)) {
				/*
				 * The odd registers are actually the high
				 * order bits of the values stored in the even
				 * registers.
				 */
				set_fpr32(&fregs[(addr & ~1) - FPR_BASE],
					  addr & 1, data);
				break;
			}
#endif
			set_fpr64(&fregs[addr - FPR_BASE], 0, data);
			break;
		}
		case FPC_CSR:
			init_fp_ctx(child);
			ptrace_setfcr31(child, data);
			break;
#endif
		case PC:
			regs->cp0_epc = data;
			break;
		case MMHI:
			regs->hi = data;
			break;
		case MMLO:
			regs->lo = data;
			break;
#ifdef CONFIG_CPU_HAS_SMARTMIPS
		case ACX:
			regs->acx = data;
			break;
#endif
		case DSP_BASE ... DSP_BASE + 5: {
			dspreg_t *dregs;

			if (!cpu_has_dsp) {
				ret = -EIO;
				break;
			}

			dregs = __get_dsp_regs(child);
			dregs[addr - DSP_BASE] = data;
			break;
		}
		case DSP_CONTROL:
			if (!cpu_has_dsp) {
				ret = -EIO;
				break;
			}
			child->thread.dsp.dspcontrol = data;
			break;
		default:
			/* The rest are not allowed. */
			ret = -EIO;
			break;
		}
		break;
		}

	case PTRACE_GETREGS:
		ret = ptrace_getregs(child, datavp);
		break;

	case PTRACE_SETREGS:
		ret = ptrace_setregs(child, datavp);
		break;

#ifdef CONFIG_MIPS_FP_SUPPORT
	case PTRACE_GETFPREGS:
		ret = ptrace_getfpregs(child, datavp);
		break;

	case PTRACE_SETFPREGS:
		ret = ptrace_setfpregs(child, datavp);
		break;
#endif
	case PTRACE_GET_THREAD_AREA:
		ret = put_user(task_thread_info(child)->tp_value, datalp);
		break;

	case PTRACE_GET_WATCH_REGS:
		ret = ptrace_get_watch_regs(child, addrp);
		break;

	case PTRACE_SET_WATCH_REGS:
		ret = ptrace_set_watch_regs(child, addrp);
		break;

	default:
		ret = ptrace_request(child, request, addr, data);
		break;
	}
 out:
	return ret;
}

/*
 * Notification of system call entry/exit
 * - triggered by current->work.syscall_trace
 */
asmlinkage long syscall_trace_enter(struct pt_regs *regs)
{
	user_exit();

	if (test_thread_flag(TIF_SYSCALL_TRACE)) {
		if (ptrace_report_syscall_entry(regs))
			return -1;
	}

	if (secure_computing())
		return -1;

	if (unlikely(test_thread_flag(TIF_SYSCALL_TRACEPOINT)))
		trace_sys_enter(regs, regs->regs[2]);

	audit_syscall_entry(current_thread_info()->syscall,
			    regs->regs[4], regs->regs[5],
			    regs->regs[6], regs->regs[7]);

	/*
	 * Negative syscall numbers are mistaken for rejected syscalls, but
	 * won't have had the return value set appropriately, so we do so now.
	 */
	if (current_thread_info()->syscall < 0)
		syscall_set_return_value(current, regs, -ENOSYS, 0);
	return current_thread_info()->syscall;
}

/*
 * Notification of system call entry/exit
 * - triggered by current->work.syscall_trace
 */
asmlinkage void syscall_trace_leave(struct pt_regs *regs)
{
	/*
	 * We may come here right after calling schedule_user()
	 * or do_notify_resume(), in which case we can be in RCU
	 * user mode.
	 */
	user_exit();

	audit_syscall_exit(regs);

	if (unlikely(test_thread_flag(TIF_SYSCALL_TRACEPOINT)))
		trace_sys_exit(regs, regs_return_value(regs));

	if (test_thread_flag(TIF_SYSCALL_TRACE))
		ptrace_report_syscall_exit(regs, 0);

	user_enter();
}
