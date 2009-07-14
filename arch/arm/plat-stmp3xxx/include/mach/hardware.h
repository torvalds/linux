/*
 * This file contains the hardware definitions of the Freescale STMP3XXX
 *
 * Copyright (C) 2005 Sigmatel Inc
 *
 * Copyright 2008 Freescale Semiconductor, Inc. All Rights Reserved.
 * Copyright 2008 Embedded Alley Solutions, Inc All Rights Reserved.
 */

/*
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */
#ifndef __ASM_ARCH_HARDWARE_H
#define __ASM_ARCH_HARDWARE_H

/*
 * Where in virtual memory the IO devices (timers, system controllers
 * and so on)
 */
#define IO_BASE			0xF0000000                 /* VA of IO  */
#define IO_SIZE			0x00100000                 /* How much? */
#define IO_START		0x80000000                 /* PA of IO  */

/* macro to get at IO space when running virtually */
#define IO_ADDRESS(x) (((x) & 0x000fffff) | IO_BASE)

#endif
