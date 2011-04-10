/*
 * Copyright (c) 2010 Broadcom Corporation
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef	_h_pcicfg_
#define	_h_pcicfg_

#include <linux/pci_regs.h>

/* PCI configuration address space size */
#define PCI_SZPCR		256

/* Everything below is BRCM HND proprietary */

/* Brcm PCI configuration registers */
#define PCI_BAR0_WIN		0x80	/* backplane address space accessed by BAR0 */
#define PCI_SPROM_CONTROL	0x88	/* sprom property control */
#define PCI_INT_MASK		0x94	/* mask of PCI and other cores interrupts */
#define  PCI_SBIM_SHIFT		8	/* backplane core interrupt mask bits offset */
#define PCI_BAR0_WIN2		0xac	/* backplane address space accessed by second 4KB of BAR0 */
#define PCI_GPIO_IN		0xb0	/* pci config space gpio input (>=rev3) */
#define PCI_GPIO_OUT		0xb4	/* pci config space gpio output (>=rev3) */
#define PCI_GPIO_OUTEN		0xb8	/* pci config space gpio output enable (>=rev3) */

#define PCI_BAR0_SPROM_OFFSET	(4 * 1024)	/* bar0 + 4K accesses external sprom */
#define PCI_BAR0_PCIREGS_OFFSET	(6 * 1024)	/* bar0 + 6K accesses pci core registers */
#define PCI_BAR0_PCISBR_OFFSET	(4 * 1024)	/* pci core SB registers are at the end of the
						 * 8KB window, so their address is the "regular"
						 * address plus 4K
						 */
#define PCI_BAR0_WINSZ		(16 * 1024)	/* bar0 window size Match with corerev 13 */
/* On pci corerev >= 13 and all pcie, the bar0 is now 16KB and it maps: */
#define PCI_16KB0_PCIREGS_OFFSET (8 * 1024)	/* bar0 + 8K accesses pci/pcie core registers */
#define PCI_16KB0_CCREGS_OFFSET	(12 * 1024)	/* bar0 + 12K accesses chipc core registers */

#define PCI_SBIM_STATUS_SERR	0x4	/* backplane SBErr interrupt status */

#endif				/* _h_pcicfg_ */
