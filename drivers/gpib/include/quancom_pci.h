/* SPDX-License-Identifier: GPL-2.0 */

/***************************************************************************
 * Quancom pci stuff
 * copyright (C) 2005 by Frank Mori Hess
 ***************************************************************************/

#ifndef _QUANCOM_PCI_H
#define _QUANCOM_PCI_H

/* quancom registers */
enum quancom_regs {
	QUANCOM_IRQ_CONTROL_STATUS_REG = 0xfc,
};

enum quancom_irq_control_status_bits {
	QUANCOM_IRQ_ASSERTED_BIT = 0x1, /* readable */
	/* (any write to the register clears the interrupt)*/
	QUANCOM_IRQ_ENABLE_BIT = 0x4, /* writeable */
};

#endif	// _QUANCOM_PCI_H
