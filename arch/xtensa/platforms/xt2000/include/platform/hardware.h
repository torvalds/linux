/*
 * platform/hardware.h
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001 Tensilica Inc.
 */

/*
 * This file contains the hardware configuration of the XT2000 board.
 */

#ifndef _XTENSA_XT2000_HARDWARE_H
#define _XTENSA_XT2000_HARDWARE_H

#include <variant/core.h>

/*
 * On-board components.
 */

#define SONIC83934_INTNUM	XCHAL_EXTINT3_NUM
#define SONIC83934_ADDR		IOADDR(0x0d030000)

/*
 * V3-PCI
 */

/* The XT2000 uses the V3 as a cascaded interrupt controller for the PCI bus */

#define IRQ_PCI_A		(XCHAL_NUM_INTERRUPTS + 0)
#define IRQ_PCI_B		(XCHAL_NUM_INTERRUPTS + 1)
#define IRQ_PCI_C		(XCHAL_NUM_INTERRUPTS + 2)

/*
 * Various other components.
 */

#define XT2000_LED_ADDR		IOADDR(0x0d040000)

#endif /* _XTENSA_XT2000_HARDWARE_H */
