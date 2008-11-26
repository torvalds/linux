/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1997, 1999, 2001, 06 by Ralf Baechle
 * Copyright (C) 2001 MIPS Technologies, Inc.
 */
#ifndef _ASM_REBOOT_H
#define _ASM_REBOOT_H

extern void (*_machine_restart)(char *command);
extern void (*_machine_halt)(void);

#endif /* _ASM_REBOOT_H */
