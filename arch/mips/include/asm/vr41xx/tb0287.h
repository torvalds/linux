/*
 *  tb0287.h, Include file for TANBAC TB0287 mini-ITX board.
 *
 *  Copyright (C) 2005	Media Lab Inc. <ito@mlb.co.jp>
 *
 *  This code is largely based on tb0219.h.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#ifndef __TANBAC_TB0287_H
#define __TANBAC_TB0287_H

#include <asm/vr41xx/irq.h>

/*
 * General-Purpose I/O Pin Number
 */
#define TB0287_PCI_SLOT_PIN		2
#define TB0287_SM501_PIN		3
#define TB0287_SIL680A_PIN		8
#define TB0287_RTL8110_PIN		13

/*
 * Interrupt Number
 */
#define TB0287_PCI_SLOT_IRQ		GIU_IRQ(TB0287_PCI_SLOT_PIN)
#define TB0287_SM501_IRQ		GIU_IRQ(TB0287_SM501_PIN)
#define TB0287_SIL680A_IRQ		GIU_IRQ(TB0287_SIL680A_PIN)
#define TB0287_RTL8110_IRQ		GIU_IRQ(TB0287_RTL8110_PIN)

#endif /* __TANBAC_TB0287_H */
