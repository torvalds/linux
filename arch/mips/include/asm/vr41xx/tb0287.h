/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *  tb0287.h, Include file for TANBAC TB0287 mini-ITX board.
 *
 *  Copyright (C) 2005	Media Lab Inc. <ito@mlb.co.jp>
 *
 *  This code is largely based on tb0219.h.
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
