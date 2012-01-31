/*
 *  Port on Texas Instruments TMS320C6x architecture
 *
 *  Copyright (C) 2004, 2009, 2011 Texas Instruments Incorporated
 *  Author: Aurelien Jacquiot (aurelien.jacquiot@jaluna.com)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */
#ifndef _ASM_C6X_TRAPS_H
#define _ASM_C6X_TRAPS_H

#define EXCEPT_TYPE_NXF   31	   /* NMI */
#define EXCEPT_TYPE_EXC   30	   /* external exception */
#define EXCEPT_TYPE_IXF   1	   /* internal exception */
#define EXCEPT_TYPE_SXF   0	   /* software exception */

#define EXCEPT_CAUSE_LBX  (1 << 7) /* loop buffer exception */
#define EXCEPT_CAUSE_PRX  (1 << 6) /* privilege exception */
#define EXCEPT_CAUSE_RAX  (1 << 5) /* resource access exception */
#define EXCEPT_CAUSE_RCX  (1 << 4) /* resource conflict exception */
#define EXCEPT_CAUSE_OPX  (1 << 3) /* opcode exception */
#define EXCEPT_CAUSE_EPX  (1 << 2) /* execute packet exception */
#define EXCEPT_CAUSE_FPX  (1 << 1) /* fetch packet exception */
#define EXCEPT_CAUSE_IFX  (1 << 0) /* instruction fetch exception */

struct exception_info {
	char *kernel_str;
	int  signo;
	int  code;
};

extern int (*c6x_nmi_handler)(struct pt_regs *regs);

#endif /* _ASM_C6X_TRAPS_H */
