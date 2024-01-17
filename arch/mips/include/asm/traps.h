/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *	Trap handling definitions.
 *
 *	Copyright (C) 2002, 2003  Maciej W. Rozycki
 */
#ifndef _ASM_TRAPS_H
#define _ASM_TRAPS_H

/*
 * Possible status responses for a board_be_handler backend.
 */
#define MIPS_BE_DISCARD 0		/* return with no action */
#define MIPS_BE_FIXUP	1		/* return to the fixup code */
#define MIPS_BE_FATAL	2		/* treat as an unrecoverable error */

extern void (*board_be_init)(void);
void mips_set_be_handler(int (*handler)(struct pt_regs *reg, int is_fixup));

extern void (*board_nmi_handler_setup)(void);
extern void (*board_ejtag_handler_setup)(void);
extern void (*board_bind_eic_interrupt)(int irq, int regset);
extern void (*board_ebase_setup)(void);
extern void (*board_cache_error_setup)(void);

extern int register_nmi_notifier(struct notifier_block *nb);
extern void reserve_exception_space(phys_addr_t addr, unsigned long size);
extern char except_vec_nmi[];

#define VECTORSPACING 0x100	/* for EI/VI mode */

#define nmi_notifier(fn, pri)						\
({									\
	static struct notifier_block fn##_nb = {			\
		.notifier_call = fn,					\
		.priority = pri						\
	};								\
									\
	register_nmi_notifier(&fn##_nb);				\
})

asmlinkage void do_ade(struct pt_regs *regs);
asmlinkage void do_be(struct pt_regs *regs);
asmlinkage void do_ov(struct pt_regs *regs);
asmlinkage void do_fpe(struct pt_regs *regs, unsigned long fcr31);
asmlinkage void do_bp(struct pt_regs *regs);
asmlinkage void do_tr(struct pt_regs *regs);
asmlinkage void do_ri(struct pt_regs *regs);
asmlinkage void do_cpu(struct pt_regs *regs);
asmlinkage void do_msa_fpe(struct pt_regs *regs, unsigned int msacsr);
asmlinkage void do_msa(struct pt_regs *regs);
asmlinkage void do_mdmx(struct pt_regs *regs);
asmlinkage void do_watch(struct pt_regs *regs);
asmlinkage void do_mcheck(struct pt_regs *regs);
asmlinkage void do_mt(struct pt_regs *regs);
asmlinkage void do_dsp(struct pt_regs *regs);
asmlinkage void do_reserved(struct pt_regs *regs);
asmlinkage void do_ftlb(void);
asmlinkage void do_gsexc(struct pt_regs *regs, u32 diag1);
asmlinkage void do_daddi_ov(struct pt_regs *regs);
asmlinkage void do_page_fault(struct pt_regs *regs,
	unsigned long write, unsigned long address);

asmlinkage void cache_parity_error(void);
asmlinkage void ejtag_exception_handler(struct pt_regs *regs);
asmlinkage void __noreturn nmi_exception_handler(struct pt_regs *regs);

#endif /* _ASM_TRAPS_H */
