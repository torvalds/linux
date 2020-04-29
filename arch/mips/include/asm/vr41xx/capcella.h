/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *  capcella.h, Include file for ZAO Networks Capcella.
 *
 *  Copyright (C) 2002-2004  Yoichi Yuasa <yuasa@linux-mips.org>
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
