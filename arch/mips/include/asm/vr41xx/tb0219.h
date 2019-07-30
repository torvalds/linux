/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *  tb0219.h, Include file for TANBAC TB0219.
 *
 *  Copyright (C) 2002-2004  Yoichi Yuasa <yuasa@linux-mips.org>
 *
 *  Modified for TANBAC TB0219:
 *  Copyright (C) 2003 Megasolution Inc.  <matsu@megasolution.jp>
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
