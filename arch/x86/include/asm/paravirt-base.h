/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef _ASM_X86_PARAVIRT_BASE_H
#define _ASM_X86_PARAVIRT_BASE_H

/*
 * Wrapper type for pointers to code which uses the non-standard
 * calling convention.  See PV_CALL_SAVE_REGS_THUNK below.
 */
struct paravirt_callee_save {
	void *func;
};

struct pv_info {
#ifdef CONFIG_PARAVIRT_XXL
	u16 extra_user_64bit_cs;  /* __USER_CS if none */
#endif
	const char *name;
};

void default_banner(void);
extern struct pv_info pv_info;
unsigned long paravirt_ret0(void);
#ifdef CONFIG_PARAVIRT_XXL
u64 _paravirt_ident_64(u64);
#endif
#define paravirt_nop	((void *)nop_func)

#ifdef CONFIG_PARAVIRT_SPINLOCKS
void paravirt_set_cap(void);
#else
static inline void paravirt_set_cap(void) { }
#endif

#endif /* _ASM_X86_PARAVIRT_BASE_H */
