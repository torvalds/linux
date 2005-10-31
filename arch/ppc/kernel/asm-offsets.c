/*
 * This program is used to generate definitions needed by
 * assembly language modules.
 *
 * We use the technique used in the OSF Mach kernel code:
 * generate asm statements containing #defines,
 * compile this file to assembler, and then extract the
 * #defines from the assembly-language output.
 */

#include <linux/config.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/ptrace.h>
#include <linux/suspend.h>
#include <linux/mman.h>
#include <linux/mm.h>
#include <asm/io.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/processor.h>
#include <asm/cputable.h>
#include <asm/thread_info.h>

#define DEFINE(sym, val) \
	asm volatile("\n->" #sym " %0 " #val : : "i" (val))

#define BLANK() asm volatile("\n->" : : )

int
main(void)
{
	DEFINE(THREAD, offsetof(struct task_struct, thread));
	DEFINE(THREAD_INFO, offsetof(struct task_struct, thread_info));
	DEFINE(MM, offsetof(struct task_struct, mm));
	DEFINE(PTRACE, offsetof(struct task_struct, ptrace));
	DEFINE(KSP, offsetof(struct thread_struct, ksp));
	DEFINE(PGDIR, offsetof(struct thread_struct, pgdir));
	DEFINE(LAST_SYSCALL, offsetof(struct thread_struct, last_syscall));
	DEFINE(PT_REGS, offsetof(struct thread_struct, regs));
	DEFINE(THREAD_FPEXC_MODE, offsetof(struct thread_struct, fpexc_mode));
	DEFINE(THREAD_FPR0, offsetof(struct thread_struct, fpr[0]));
	DEFINE(THREAD_FPSCR, offsetof(struct thread_struct, fpscr));
#if defined(CONFIG_4xx) || defined(CONFIG_BOOKE)
	DEFINE(THREAD_DBCR0, offsetof(struct thread_struct, dbcr0));
	DEFINE(PT_PTRACED, PT_PTRACED);
#endif
#ifdef CONFIG_ALTIVEC
	DEFINE(THREAD_VR0, offsetof(struct thread_struct, vr[0]));
	DEFINE(THREAD_VRSAVE, offsetof(struct thread_struct, vrsave));
	DEFINE(THREAD_VSCR, offsetof(struct thread_struct, vscr));
	DEFINE(THREAD_USED_VR, offsetof(struct thread_struct, used_vr));
#endif /* CONFIG_ALTIVEC */
#ifdef CONFIG_SPE
	DEFINE(THREAD_EVR0, offsetof(struct thread_struct, evr[0]));
	DEFINE(THREAD_ACC, offsetof(struct thread_struct, acc));
	DEFINE(THREAD_SPEFSCR, offsetof(struct thread_struct, spefscr));
	DEFINE(THREAD_USED_SPE, offsetof(struct thread_struct, used_spe));
#endif /* CONFIG_SPE */
	/* Interrupt register frame */
	DEFINE(STACK_FRAME_OVERHEAD, STACK_FRAME_OVERHEAD);
	DEFINE(INT_FRAME_SIZE, STACK_FRAME_OVERHEAD + sizeof(struct pt_regs));
	/* in fact we only use gpr0 - gpr9 and gpr20 - gpr23 */
	DEFINE(GPR0, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, gpr[0]));
	DEFINE(GPR1, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, gpr[1]));
	DEFINE(GPR2, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, gpr[2]));
	DEFINE(GPR3, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, gpr[3]));
	DEFINE(GPR4, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, gpr[4]));
	DEFINE(GPR5, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, gpr[5]));
	DEFINE(GPR6, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, gpr[6]));
	DEFINE(GPR7, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, gpr[7]));
	DEFINE(GPR8, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, gpr[8]));
	DEFINE(GPR9, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, gpr[9]));
	DEFINE(GPR10, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, gpr[10]));
	DEFINE(GPR11, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, gpr[11]));
	DEFINE(GPR12, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, gpr[12]));
	DEFINE(GPR13, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, gpr[13]));
	DEFINE(GPR14, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, gpr[14]));
	DEFINE(GPR15, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, gpr[15]));
	DEFINE(GPR16, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, gpr[16]));
	DEFINE(GPR17, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, gpr[17]));
	DEFINE(GPR18, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, gpr[18]));
	DEFINE(GPR19, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, gpr[19]));
	DEFINE(GPR20, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, gpr[20]));
	DEFINE(GPR21, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, gpr[21]));
	DEFINE(GPR22, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, gpr[22]));
	DEFINE(GPR23, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, gpr[23]));
	DEFINE(GPR24, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, gpr[24]));
	DEFINE(GPR25, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, gpr[25]));
	DEFINE(GPR26, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, gpr[26]));
	DEFINE(GPR27, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, gpr[27]));
	DEFINE(GPR28, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, gpr[28]));
	DEFINE(GPR29, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, gpr[29]));
	DEFINE(GPR30, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, gpr[30]));
	DEFINE(GPR31, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, gpr[31]));
	/* Note: these symbols include _ because they overlap with special
	 * register names
	 */
	DEFINE(_NIP, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, nip));
	DEFINE(_MSR, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, msr));
	DEFINE(_CTR, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, ctr));
	DEFINE(_LINK, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, link));
	DEFINE(_CCR, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, ccr));
	DEFINE(_MQ, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, mq));
	DEFINE(_XER, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, xer));
	DEFINE(_DAR, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, dar));
	DEFINE(_DSISR, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, dsisr));
	/* The PowerPC 400-class & Book-E processors have neither the DAR nor the DSISR
	 * SPRs. Hence, we overload them to hold the similar DEAR and ESR SPRs
	 * for such processors.  For critical interrupts we use them to
	 * hold SRR0 and SRR1.
	 */
	DEFINE(_DEAR, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, dar));
	DEFINE(_ESR, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, dsisr));
	DEFINE(ORIG_GPR3, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, orig_gpr3));
	DEFINE(RESULT, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, result));
	DEFINE(TRAP, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, trap));
	DEFINE(CLONE_VM, CLONE_VM);
	DEFINE(CLONE_UNTRACED, CLONE_UNTRACED);
	DEFINE(MM_PGD, offsetof(struct mm_struct, pgd));

	/* About the CPU features table */
	DEFINE(CPU_SPEC_ENTRY_SIZE, sizeof(struct cpu_spec));
	DEFINE(CPU_SPEC_PVR_MASK, offsetof(struct cpu_spec, pvr_mask));
	DEFINE(CPU_SPEC_PVR_VALUE, offsetof(struct cpu_spec, pvr_value));
	DEFINE(CPU_SPEC_FEATURES, offsetof(struct cpu_spec, cpu_features));
	DEFINE(CPU_SPEC_SETUP, offsetof(struct cpu_spec, cpu_setup));

	DEFINE(TI_SC_NOERR, offsetof(struct thread_info, syscall_noerror));
	DEFINE(TI_TASK, offsetof(struct thread_info, task));
	DEFINE(TI_EXECDOMAIN, offsetof(struct thread_info, exec_domain));
	DEFINE(TI_FLAGS, offsetof(struct thread_info, flags));
	DEFINE(TI_CPU, offsetof(struct thread_info, cpu));
	DEFINE(TI_PREEMPT, offsetof(struct thread_info, preempt_count));

	DEFINE(pbe_address, offsetof(struct pbe, address));
	DEFINE(pbe_orig_address, offsetof(struct pbe, orig_address));
	DEFINE(pbe_next, offsetof(struct pbe, next));

	DEFINE(TASK_SIZE, TASK_SIZE);
	DEFINE(NUM_USER_SEGMENTS, TASK_SIZE>>28);
	return 0;
}
