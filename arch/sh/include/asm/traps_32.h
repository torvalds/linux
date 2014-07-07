#ifndef __ASM_SH_TRAPS_32_H
#define __ASM_SH_TRAPS_32_H

#include <linux/types.h>
#include <asm/mmu.h>

#ifdef CONFIG_CPU_HAS_SR_RB
#define lookup_exception_vector()	\
({					\
	unsigned long _vec;		\
					\
	__asm__ __volatile__ (		\
		"stc r2_bank, %0\n\t"	\
		: "=r" (_vec)		\
	);				\
					\
	_vec;				\
})
#else
#define lookup_exception_vector()	\
({					\
	unsigned long _vec;		\
	__asm__ __volatile__ (		\
		"mov r4, %0\n\t"	\
		: "=r" (_vec)		\
	);				\
					\
	_vec;				\
})
#endif

static inline void trigger_address_error(void)
{
	__asm__ __volatile__ (
		"ldc %0, sr\n\t"
		"mov.l @%1, %0"
		:
		: "r" (0x10000000), "r" (0x80000001)
	);
}

asmlinkage void do_address_error(struct pt_regs *regs,
				 unsigned long writeaccess,
				 unsigned long address);
asmlinkage void do_divide_error(unsigned long r4);
asmlinkage void do_reserved_inst(void);
asmlinkage void do_illegal_slot_inst(void);
asmlinkage void do_exception_error(void);

#define BUILD_TRAP_HANDLER(name)					\
asmlinkage void name##_trap_handler(unsigned long r4, unsigned long r5,	\
				    unsigned long r6, unsigned long r7,	\
				    struct pt_regs __regs)

#define TRAP_HANDLER_DECL				\
	struct pt_regs *regs = RELOC_HIDE(&__regs, 0);	\
	unsigned int vec = regs->tra;			\
	(void)vec;

#endif /* __ASM_SH_TRAPS_32_H */
