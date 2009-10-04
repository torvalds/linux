/*
 * Alchemy Semi Pb1500 Referrence Board
 *
 * Copyright 2001, 2008 MontaVista Software Inc.
 * Author: MontaVista Software, Inc. <source@mvista.com>
 *
 * ########################################################################
 *
 *  This program is free software; you can distribute it and/or modify it
 *  under the terms of the GNU General Public License (Version 2) as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * ########################################################################
 *
 *
 */
#ifndef __ASM_PB1500_H
#define __ASM_PB1500_H

/* PCMCIA Pb1500 specific defines */
#define PCMCIA_MAX_SOCK  0
#define PCMCIA_NUM_SOCKS (PCMCIA_MAX_SOCK + 1)

/* VPP/VCC */
#define SET_VCC_VPP(VCC, VPP) (((VCC) << 2) | ((VPP) << 0))

#endif /* __ASM_PB1500_H */
