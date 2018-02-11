/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_DEBUGREG_H
#define _ASM_X86_DEBUGREG_H


#include <linux/bug.h>
#include <uapi/asm/debugreg.h>

DECLARE_PER_CPU(unsigned long, cpu_dr7);

#ifndef CONFIG_PARAVIRT
/*
 * These special macros can be used to get or set a debugging register
 */
#define get_debugreg(var, register)				\
	(var) = native_get_debugreg(register)
#define set_debugreg(value, register)				\
	native_set_debugreg(register, value)
#endif

static inline unsigned long native_get_debugreg(int regno)
{
	unsigned long val = 0;	/* Damn you, gcc! */

	switch (regno) {
	case 0:
		asm("mov %%db0, %0" :"=r" (val));
		break;
	case 1:
		asm("mov %%db1, %0" :"=r" (val));
		break;
	case 2:
		asm("mov %%db2, %0" :"=r" (val));
		break;
	case 3:
		asm("mov %%db3, %0" :"=r" (val));
		break;
	case 6:
		asm("mov %%db6, %0" :"=r" (val));
		break;
	case 7:
		asm("mov %%db7, %0" :"=r" (val));
		break;
	default:
		BUG();
	}
	return val;
}

static inline void native_set_debugreg(int regno, unsigned long value)
{
	switch (regno) {
	case 0:
		asm("mov %0, %%db0"	::"r" (value));
		break;
	case 1:
		asm("mov %0, %%db1"	::"r" (value));
		break;
	case 2:
		asm("mov %0, %%db2"	::"r" (value));
		break;
	case 3:
		asm("mov %0, %%db3"	::"r" (value));
		break;
	case 6:
		asm("mov %0, %%db6"	::"r" (value));
		break;
	case 7:
		asm("mov %0, %%db7"	::"r" (value));
		break;
	default:
		BUG();
	}
}

static inline void hw_breakpoint_disable(void)
{
	/* Zero the control register for HW Breakpoint */
	set_debugreg(0UL, 7);

	/* Zero-out the individual HW breakpoint address registers */
	set_debugreg(0UL, 0);
	set_debugreg(0UL, 1);
	set_debugreg(0UL, 2);
	set_debugreg(0UL, 3);
}

static inline int hw_breakpoint_active(void)
{
	return __this_cpu_read(cpu_dr7) & DR_GLOBAL_ENABLE_MASK;
}

extern void aout_dump_debugregs(struct user *dump);

extern void hw_breakpoint_restore(void);

#ifdef CONFIG_X86_64
DECLARE_PER_CPU(int, debug_stack_usage);
static inline void debug_stack_usage_inc(void)
{
	__this_cpu_inc(debug_stack_usage);
}
static inline void debug_stack_usage_dec(void)
{
	__this_cpu_dec(debug_stack_usage);
}
int is_debug_stack(unsigned long addr);
void debug_stack_set_zero(void);
void debug_stack_reset(void);
#else /* !X86_64 */
static inline int is_debug_stack(unsigned long addr) { return 0; }
static inline void debug_stack_set_zero(void) { }
static inline void debug_stack_reset(void) { }
static inline void debug_stack_usage_inc(void) { }
static inline void debug_stack_usage_dec(void) { }
#endif /* X86_64 */

#ifdef CONFIG_CPU_SUP_AMD
extern void set_dr_addr_mask(unsigned long mask, int dr);
#else
static inline void set_dr_addr_mask(unsigned long mask, int dr) { }
#endif

#endif /* _ASM_X86_DEBUGREG_H */
