#include <linux/module.h>
#include <linux/sched.h>
#include <linux/personality.h>
#include <linux/binfmts.h>
#include <linux/elf.h>

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
	unsigned int personality = PER_LINUX_32BIT;

	/*
	 * APCS-26 is only valid for OABI executables
	 */
	if ((eflags & EF_ARM_EABI_MASK) == EF_ARM_EABI_UNKNOWN) {
		if (eflags & EF_ARM_APCS_26)
			personality = PER_LINUX;
	}

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
 * Set READ_IMPLIES_EXEC if:
 *  - the binary requires an executable stack
 *  - we're running on a CPU which doesn't support NX.
 */
int arm_elf_read_implies_exec(const struct elf32_hdr *x, int executable_stack)
{
	if (executable_stack != EXSTACK_DISABLE_X)
		return 1;
	if (cpu_architecture() < CPU_ARCH_ARMv6)
		return 1;
#if !defined(CONFIG_AEABI) || defined(CONFIG_OABI_COMPAT)
	/*
	 * If we have support for OABI programs, we can never allow NX
	 * support - our signal syscall restart mechanism relies upon
	 * being able to execute code placed on the user stack.
	 */
	return 1;
#else
	return 0;
#endif
}
EXPORT_SYMBOL(arm_elf_read_implies_exec);
