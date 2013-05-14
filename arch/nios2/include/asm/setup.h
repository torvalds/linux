/*
 * Copyright (C) 2011 Tobias Klauser <tklauser@distanz.ch>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef _ASM_NIOS2_SETUP_H
#define _ASM_NIOS2_SETUP_H

#include <asm-generic/setup.h>

#ifndef __ASSEMBLY__
# ifdef __KERNEL__

extern char cmd_line[COMMAND_LINE_SIZE];

extern char exception_handler_hook[];
extern char fast_handler[];
extern char fast_handler_end[];

extern void pagetable_init(void);

extern void setup_early_printk(void);

# endif/* __KERNEL__ */
#endif /* __ASSEMBLY__ */

#endif /* _ASM_NIOS2_SETUP_H */
