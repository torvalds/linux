/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *  tb0226.h, Include file for TANBAC TB0226.
 *
 *  Copyright (C) 2002-2004  Yoichi Yuasa <yuasa@linux-mips.org>
 */
#ifndef __TANBAC_TB0226_H
#define __TANBAC_TB0226_H

#include <asm/vr41xx/irq.h>

/*
 * General-Purpose I/O Pin Number
 */
#define GD82559_1_PIN			2
#define GD82559_2_PIN			3
#define UPD720100_INTA_PIN		4
#define UPD720100_INTB_PIN		8
#define UPD720100_INTC_PIN		13

/*
 * Interrupt Number
 */
#define GD82559_1_IRQ			GIU_IRQ(GD82559_1_PIN)
#define GD82559_2_IRQ			GIU_IRQ(GD82559_2_PIN)
#define UPD720100_INTA_IRQ		GIU_IRQ(UPD720100_INTA_PIN)
#define UPD720100_INTB_IRQ		GIU_IRQ(UPD720100_INTB_PIN)
#define UPD720100_INTC_IRQ		GIU_IRQ(UPD720100_INTC_PIN)

#endif /* __TANBAC_TB0226_H */
