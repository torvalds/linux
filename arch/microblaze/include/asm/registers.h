/*
 * Copyright (C) 2008 Michal Simek
 * Copyright (C) 2008 PetaLogix
 * Copyright (C) 2006 Atmark Techno, Inc.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#ifndef _ASM_MICROBLAZE_REGISTERS_H
#define _ASM_MICROBLAZE_REGISTERS_H

#define MSR_BE	(1<<0) /* 0x001 */
#define MSR_IE	(1<<1) /* 0x002 */
#define MSR_C	(1<<2) /* 0x004 */
#define MSR_BIP	(1<<3) /* 0x008 */
#define MSR_FSL	(1<<4) /* 0x010 */
#define MSR_ICE	(1<<5) /* 0x020 */
#define MSR_DZ	(1<<6) /* 0x040 */
#define MSR_DCE	(1<<7) /* 0x080 */
#define MSR_EE	(1<<8) /* 0x100 */
#define MSR_EIP	(1<<9) /* 0x200 */
#define MSR_CC	(1<<31)

/* Floating Point Status Register (FSR) Bits */
#define FSR_IO		(1<<4) /* Invalid operation */
#define FSR_DZ		(1<<3) /* Divide-by-zero */
#define FSR_OF		(1<<2) /* Overflow */
#define FSR_UF		(1<<1) /* Underflow */
#define FSR_DO		(1<<0) /* Denormalized operand error */

#endif /* _ASM_MICROBLAZE_REGISTERS_H */
