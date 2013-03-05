/*
 *  Copyright (C) 2005,2008 Imagination Technologies
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */

#ifndef _METAG_TBIVECTORS_H
#define _METAG_TBIVECTORS_H

#ifndef __ASSEMBLY__

#include <asm/tbx.h>

typedef TBIRES (*kick_irq_func_t)(TBIRES, int, int, int, PTBI, int *);

extern TBIRES kick_handler(TBIRES, int, int, int, PTBI);
struct kick_irq_handler {
	struct list_head list;
	kick_irq_func_t func;
};

extern void kick_register_func(struct kick_irq_handler *);
extern void kick_unregister_func(struct kick_irq_handler *);

extern void head_end(TBIRES, unsigned long);
extern void restart_critical_section(TBIRES State);
extern TBIRES tail_end_sys(TBIRES, int, int *);
static inline TBIRES tail_end(TBIRES state)
{
	return tail_end_sys(state, -1, NULL);
}

DECLARE_PER_CPU(PTBI, pTBI);
extern PTBI pTBI_get(unsigned int);

extern int ret_from_fork(TBIRES arg);

extern int do_page_fault(struct pt_regs *regs, unsigned long address,
			 unsigned int write_access, unsigned int trapno);

extern TBIRES __TBIUnExpXXX(TBIRES State, int SigNum, int Triggers, int Inst,
			    PTBI pTBI);

#endif

#endif /* _METAG_TBIVECTORS_H */
