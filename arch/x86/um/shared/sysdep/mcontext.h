/* 
 * Copyright (C) 2000 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 * Licensed under the GPL
 */

#ifndef __SYS_SIGCONTEXT_X86_H
#define __SYS_SIGCONTEXT_X86_H

#include <stub-data.h>

extern void get_regs_from_mc(struct uml_pt_regs *, mcontext_t *);
extern void get_mc_from_regs(struct uml_pt_regs *regs, mcontext_t *mc,
			     int single_stepping);

extern int get_stub_state(struct uml_pt_regs *regs, struct stub_data *data,
			  unsigned long *fp_size_out);
extern int set_stub_state(struct uml_pt_regs *regs, struct stub_data *data,
			  int single_stepping);

#ifdef __i386__

#define GET_FAULTINFO_FROM_MC(fi, mc) \
	{ \
		(fi).cr2 = (mc)->cr2; \
		(fi).error_code = (mc)->gregs[REG_ERR]; \
		(fi).trap_no = (mc)->gregs[REG_TRAPNO]; \
	}

#else

#define GET_FAULTINFO_FROM_MC(fi, mc) \
	{ \
		(fi).cr2 = (mc)->gregs[REG_CR2]; \
		(fi).error_code = (mc)->gregs[REG_ERR]; \
		(fi).trap_no = (mc)->gregs[REG_TRAPNO]; \
	}

#endif

#endif
