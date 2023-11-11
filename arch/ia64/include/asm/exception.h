/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __ASM_EXCEPTION_H
#define __ASM_EXCEPTION_H

struct pt_regs;
struct exception_table_entry;

extern void ia64_handle_exception(struct pt_regs *regs,
				  const struct exception_table_entry *e);

#define ia64_done_with_exception(regs)					  \
({									  \
	int __ex_ret = 0;						  \
	const struct exception_table_entry *e;				  \
	e = search_exception_tables((regs)->cr_iip + ia64_psr(regs)->ri); \
	if (e) {							  \
		ia64_handle_exception(regs, e);				  \
		__ex_ret = 1;						  \
	}								  \
	__ex_ret;							  \
})

#endif	/* __ASM_EXCEPTION_H */
