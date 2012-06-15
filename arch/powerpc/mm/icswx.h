#ifndef _ARCH_POWERPC_MM_ICSWX_H_
#define _ARCH_POWERPC_MM_ICSWX_H_

/*
 *  ICSWX and ACOP Management
 *
 *  Copyright (C) 2011 Anton Blanchard, IBM Corp. <anton@samba.org>
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 *
 */

#include <asm/mmu_context.h>

/* also used to denote that PIDs are not used */
#define COP_PID_NONE 0

static inline void sync_cop(void *arg)
{
	struct mm_struct *mm = arg;

	if (mm == current->active_mm)
		switch_cop(current->active_mm);
}

#ifdef CONFIG_PPC_ICSWX_PID
extern int get_cop_pid(struct mm_struct *mm);
extern int disable_cop_pid(struct mm_struct *mm);
extern void free_cop_pid(int free_pid);
#else
#define get_cop_pid(m) (COP_PID_NONE)
#define disable_cop_pid(m) (COP_PID_NONE)
#define free_cop_pid(p)
#endif

/*
 * These are implementation bits for architected registers.  If this
 * ever becomes architecture the should be moved to reg.h et. al.
 */
/* UCT is the same bit for Server and Embedded */
#define ICSWX_DSI_UCT		0x00004000  /* Unavailable Coprocessor Type */

#ifdef CONFIG_PPC_BOOK3E
/* Embedded implementation gives us no hints as to what the CT is */
#define ICSWX_GET_CT_HINT(x) (-1)
#else
/* Server implementation contains the CT value in the DSISR */
#define ICSWX_DSISR_CTMASK	0x00003f00
#define ICSWX_GET_CT_HINT(x)	(((x) & ICSWX_DSISR_CTMASK) >> 8)
#endif

#define ICSWX_RC_STARTED	0x8	/* The request has been started */
#define ICSWX_RC_NOT_IDLE	0x4	/* No coprocessor found idle */
#define ICSWX_RC_NOT_FOUND	0x2	/* No coprocessor found */
#define ICSWX_RC_UNDEFINED	0x1	/* Reserved */

extern int acop_handle_fault(struct pt_regs *regs, unsigned long address,
			     unsigned long error_code);

static inline u64 acop_copro_type_bit(unsigned int type)
{
	return 1ULL << (63 - type);
}

#endif /* !_ARCH_POWERPC_MM_ICSWX_H_ */
