// SPDX-License-Identifier: GPL-2.0
#include <linux/export.h>
#include <linux/sched.h>
#include <linux/personality.h>
#include <linux/binfmts.h>
#include <linux/elf.h>
#include <linux/elf-fdpic.h>
#include <asm/system_info.h>

int elf_check_arch(const struct elf32_hdr *x)
{
	unsigned int eflags;

	/* Make sure it's an ARM executable */
	if (x->e_machine != EM_ARM)
		return 0;

	/* Make sure the entry address is reasonable */
	if (x->e_entry & 1) {
		if (!(elf_hwcap & HWCAP_THUMB))
			return 0;
	} else if (x->e_entry & 3)
		return 0;

	eflags = x->e_flags;
	if ((eflags & EF_ARM_EABI_MASK) == EF_ARM_EABI_UNKNOWN) {
		unsigned int flt_fmt;

		/* APCS26 is only allowed if the CPU supports it */
		if ((eflags & EF_ARM_APCS_26) && !(elf_hwcap & HWCAP_26BIT))
			return 0;

		flt_fmt = eflags & (EF_ARM_VFP_FLOAT | EF_ARM_SOFT_FLOAT);

		/* VFP requires the supporting code */
		if (flt_fmt == EF_ARM_VFP_FLOAT && !(elf_hwcap & HWCAP_VFP))
			return 0;
	}
	return 1;
}
EXPORT_SYMBOL(elf_check_arch);

void elf_set_personality(const struct elf32_hdr *x)
{
	unsigned int eflags = x->e_flags;
	unsigned int personality = current->personality & ~PER_MASK;

	/*
	 * We only support Linux ELF executables, so always set the
	 * personality to LINUX.
	 */
	personality |= PER_LINUX;

	/*
	 * APCS-26 is only valid for OABI executables
	 */
	if ((eflags & EF_ARM_EABI_MASK) == EF_ARM_EABI_UNKNOWN &&
	    (eflags & EF_ARM_APCS_26))
		personality &= ~ADDR_LIMIT_32BIT;
	else
		personality |= ADDR_LIMIT_32BIT;

	set_personality(personality);

	/*
	 * Since the FPA coprocessor uses CP1 and CP2, and iWMMXt uses CP0
	 * and CP1, we only enable access to the iWMMXt coprocessor if the
	 * binary is EABI or softfloat (and thus, guaranteed not to use
	 * FPA instructions.)
	 */
	if (elf_hwcap & HWCAP_IWMMXT &&
	    eflags & (EF_ARM_EABI_MASK | EF_ARM_SOFT_FLOAT)) {
		set_thread_flag(TIF_USING_IWMMXT);
	} else {
		clear_thread_flag(TIF_USING_IWMMXT);
	}
}
EXPORT_SYMBOL(elf_set_personality);

/*
 * An executable for which elf_read_implies_exec() returns TRUE will
 * have the READ_IMPLIES_EXEC personality flag set automatically.
 *
 * The decision process for determining the results are:
 *
 *                 CPU: | lacks NX*  | has NX     |
 * ELF:                 |            |            |
 * ---------------------|------------|------------|
 * missing PT_GNU_STACK | exec-all   | exec-all   |
 * PT_GNU_STACK == RWX  | exec-all   | exec-stack |
 * PT_GNU_STACK == RW   | exec-all   | exec-none  |
 *
 *  exec-all  : all PROT_READ user mappings are executable, except when
 *              backed by files on a noexec-filesystem.
 *  exec-none : only PROT_EXEC user mappings are executable.
 *  exec-stack: only the stack and PROT_EXEC user mappings are executable.
 *
 *  *this column has no architectural effect: NX markings are ignored by
 *   hardware, but may have behavioral effects when "wants X" collides with
 *   "cannot be X" constraints in memory permission flags, as in
 *   https://lkml.kernel.org/r/20190418055759.GA3155@mellanox.com
 *
 */
int arm_elf_read_implies_exec(int executable_stack)
{
	if (executable_stack == EXSTACK_DEFAULT)
		return 1;
	if (cpu_architecture() < CPU_ARCH_ARMv6)
		return 1;
	return 0;
}
EXPORT_SYMBOL(arm_elf_read_implies_exec);

#if defined(CONFIG_MMU) && defined(CONFIG_BINFMT_ELF_FDPIC)

void elf_fdpic_arch_lay_out_mm(struct elf_fdpic_params *exec_params,
			       struct elf_fdpic_params *interp_params,
			       unsigned long *start_stack,
			       unsigned long *start_brk)
{
	elf_set_personality(&exec_params->hdr);

	exec_params->load_addr = 0x8000;
	interp_params->load_addr = ELF_ET_DYN_BASE;
	*start_stack = TASK_SIZE - SZ_16M;

	if ((exec_params->flags & ELF_FDPIC_FLAG_ARRANGEMENT) == ELF_FDPIC_FLAG_INDEPENDENT) {
		exec_params->flags &= ~ELF_FDPIC_FLAG_ARRANGEMENT;
		exec_params->flags |= ELF_FDPIC_FLAG_CONSTDISP;
	}
}

#endif
