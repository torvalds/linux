/*
 * Intel Atom SOC Power Management Controller Header File
 * Copyright (c) 2014, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 */

#ifndef PMC_ATOM_H
#define PMC_ATOM_H

/* ValleyView Power Control Unit PCI Device ID */
#define	PCI_DEVICE_ID_VLV_PMC	0x0F1C

/* PMC Memory mapped IO registers */
#define	PMC_BASE_ADDR_OFFSET	0x44
#define	PMC_BASE_ADDR_MASK	0xFFFFFE00
#define	PMC_MMIO_REG_LEN	0x100
#define	PMC_REG_BIT_WIDTH	32

/* S0ix wake event control */
#define	PMC_S0IX_WAKE_EN	0x3C

#define	BIT_LPC_CLOCK_RUN		BIT(4)
#define	BIT_SHARED_IRQ_GPSC		BIT(5)
#define	BIT_ORED_DEDICATED_IRQ_GPSS	BIT(18)
#define	BIT_ORED_DEDICATED_IRQ_GPSC	BIT(19)
#define	BIT_SHARED_IRQ_GPSS		BIT(20)

#define	PMC_WAKE_EN_SETTING	~(BIT_LPC_CLOCK_RUN | \
				BIT_SHARED_IRQ_GPSC | \
				BIT_ORED_DEDICATED_IRQ_GPSS | \
				BIT_ORED_DEDICATED_IRQ_GPSC | \
				BIT_SHARED_IRQ_GPSS)

/* PMC I/O Registers */
#define	ACPI_BASE_ADDR_OFFSET	0x40
#define	ACPI_BASE_ADDR_MASK	0xFFFFFE00
#define	ACPI_MMIO_REG_LEN	0x100

#define	PM1_CNT			0x4
#define	SLEEP_TYPE_MASK		0xFFFFECFF
#define	SLEEP_TYPE_S5		0x1C00
#define	SLEEP_ENABLE		0x2000
#endif /* PMC_ATOM_H */
