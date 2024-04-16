/*
 * OpenRISC unwinder.h
 *
 * Architecture API for unwinding stacks.
 *
 * Copyright (C) 2017 Stafford Horne <shorne@gmail.com>
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2.  This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */

#ifndef __ASM_OPENRISC_UNWINDER_H
#define __ASM_OPENRISC_UNWINDER_H

void unwind_stack(void *data, unsigned long *stack,
		  void (*trace)(void *data, unsigned long addr,
				int reliable));

#endif /* __ASM_OPENRISC_UNWINDER_H */
