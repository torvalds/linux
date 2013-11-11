#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/sched.h>
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
	if (WARN_ON_ONCE(idx >= ARRAY_SIZE(pt_regs_offset)))
		return 0;

	return regs_get_register(regs, pt_regs_offset[idx]);
}

#define REG_RESERVED (~((1ULL << PERF_REG_X86_MAX) - 1ULL))

#ifdef CONFIG_X86_32
int perf_reg_validate(u64 mask)
{
	if (!mask || mask & REG_RESERVED)
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

int perf_reg_validate(u64 mask)
{
	if (!mask || mask & REG_RESERVED)
		return -EINVAL;

	if (mask & REG_NOSUPPORT)
		return -EINVAL;

	return 0;
}

u64 perf_reg_abi(struct task_struct *task)
{
	if (test_tsk_thread_flag(task, TIF_IA32))
		return PERF_SAMPLE_REGS_ABI_32;
	else
		return PERF_SAMPLE_REGS_ABI_64;
}
#endif /* CONFIG_X86_32 */
