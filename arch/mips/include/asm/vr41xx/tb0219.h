/*
 *  tb0219.h, Include file for TANBAC TB0219.
 *
 *  Copyright (C) 2002-2004  Yoichi Yuasa <yoichi_yuasa@tripeaks.co.jp>
 *
 *  Modified for TANBAC TB0219:
 *  Copyright (C) 2003 Megasolution Inc.  <matsu@megasolution.jp>
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
#ifndef __TANBAC_TB0219_H
#define __TANBAC_TB0219_H

#include <asm/vr41xx/irq.h>

/*
 * General-Purpose I/O Pin Number
 */
#define TB0219_PCI_SLOT1_PIN		2
#define TB0219_PCI_SLOT2_PIN		3
#define TB0219_PCI_SLOT3_PIN		4

/*
 * Interrupt Number
 */
#define TB0219_PCI_SLOT1_IRQ		GIU_IRQ(TB0219_PCI_SLOT1_PIN)
#define TB0219_PCI_SLOT2_IRQ		GIU_IRQ(TB0219_PCI_SLOT2_PIN)
#define TB0219_PCI_SLOT3_IRQ		GIU_IRQ(TB0219_PCI_SLOT3_PIN)

#endif /* __TANBAC_TB0219_H */
