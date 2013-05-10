/*
 *	Trap handling definitions.
 *
 *	Copyright (C) 2002, 2003  Maciej W. Rozycki
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 */
#ifndef _ASM_TRAPS_H
#define _ASM_TRAPS_H

/*
 * Possible status responses for a board_be_handler backend.
 */
#define MIPS_BE_DISCARD	0		/* return with no action */
#define MIPS_BE_FIXUP	1		/* return to the fixup code */
#define MIPS_BE_FATAL	2		/* treat as an unrecoverable error */

extern void (*board_be_init)(void);
extern int (*board_be_handler)(struct pt_regs *regs, int is_fixup);

extern void (*board_nmi_handler_setup)(void);
extern void (*board_ejtag_handler_setup)(void);
extern void (*board_bind_eic_interrupt)(int irq, int regset);

#endif /* _ASM_TRAPS_H */
