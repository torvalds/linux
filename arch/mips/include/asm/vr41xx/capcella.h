/*
 *  capcella.h, Include file for ZAO Networks Capcella.
 *
 *  Copyright (C) 2002-2004  Yoichi Yuasa <yoichi_yuasa@tripeaks.co.jp>
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
#ifndef __ZAO_CAPCELLA_H
#define __ZAO_CAPCELLA_H

#include <asm/vr41xx/irq.h>

/*
 * General-Purpose I/O Pin Number
 */
#define PC104PLUS_INTA_PIN		2
#define PC104PLUS_INTB_PIN		3
#define PC104PLUS_INTC_PIN		4
#define PC104PLUS_INTD_PIN		5

/*
 * Interrupt Number
 */
#define RTL8139_1_IRQ			GIU_IRQ(PC104PLUS_INTC_PIN)
#define RTL8139_2_IRQ			GIU_IRQ(PC104PLUS_INTD_PIN)
#define PC104PLUS_INTA_IRQ		GIU_IRQ(PC104PLUS_INTA_PIN)
#define PC104PLUS_INTB_IRQ		GIU_IRQ(PC104PLUS_INTB_PIN)
#define PC104PLUS_INTC_IRQ		GIU_IRQ(PC104PLUS_INTC_PIN)
#define PC104PLUS_INTD_IRQ		GIU_IRQ(PC104PLUS_INTD_PIN)

#endif /* __ZAO_CAPCELLA_H */
