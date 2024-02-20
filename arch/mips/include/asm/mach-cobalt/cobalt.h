/*
 * The Cobalt board ID information.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1997 Cobalt Microserver
 * Copyright (C) 1997, 2003 Ralf Baechle
 * Copyright (C) 2001, 2002, 2003 Liam Davies (ldavies@agile.tv)
 */
#ifndef __ASM_COBALT_H
#define __ASM_COBALT_H

extern int cobalt_board_id;

#define COBALT_BRD_ID_QUBE1    0x3
#define COBALT_BRD_ID_RAQ1     0x4
#define COBALT_BRD_ID_QUBE2    0x5
#define COBALT_BRD_ID_RAQ2     0x6

void cobalt_machine_halt(void);
void cobalt_machine_restart(char *command);

#endif /* __ASM_COBALT_H */
