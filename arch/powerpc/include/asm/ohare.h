/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_POWERPC_OHARE_H
#define _ASM_POWERPC_OHARE_H
#ifdef __KERNEL__
/*
 * ohare.h: definitions for using the "O'Hare" I/O controller chip.
 *
 * Copyright (C) 1997 Paul Mackerras.
 *
 * BenH: Changed to match those of heathrow (but not all of them). Please
 *       check if I didn't break anything (especially the media bay).
 */

/* offset from ohare base for feature control register */
#define OHARE_MBCR	0x34
#define OHARE_FCR	0x38

/*
 * Bits in feature control register.
 * These were mostly derived by experiment on a powerbook 3400
 * and may differ for other machines.
 */
#define OH_SCC_RESET		1
#define OH_BAY_POWER_N		2	/* a guess */
#define OH_BAY_PCI_ENABLE	4	/* a guess */
#define OH_BAY_IDE_ENABLE	8
#define OH_BAY_FLOPPY_ENABLE	0x10
#define OH_IDE0_ENABLE		0x20
#define OH_IDE0_RESET_N		0x40	/* a guess */
#define OH_BAY_DEV_MASK		0x1c
#define OH_BAY_RESET_N		0x80
#define OH_IOBUS_ENABLE		0x100	/* IOBUS seems to be IDE */
#define OH_SCC_ENABLE		0x200
#define OH_MESH_ENABLE		0x400
#define OH_FLOPPY_ENABLE	0x800
#define OH_SCCA_IO		0x4000
#define OH_SCCB_IO		0x8000
#define OH_VIA_ENABLE		0x10000	/* Is apparently wrong, to be verified */
#define OH_IDE1_RESET_N		0x800000

/*
 * Bits to set in the feature control register on PowerBooks.
 */
#define PBOOK_FEATURES		(OH_IDE_ENABLE | OH_SCC_ENABLE | \
				 OH_MESH_ENABLE | OH_SCCA_IO | OH_SCCB_IO)

/*
 * A magic value to put into the feature control register of the
 * "ohare" I/O controller on Starmaxes to enable the IDE CD interface.
 * Contributed by Harry Eaton.
 */
#define STARMAX_FEATURES	0xbeff7a

#endif /* __KERNEL__ */
#endif /* _ASM_POWERPC_OHARE_H */
