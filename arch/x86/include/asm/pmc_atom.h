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

/* PMC I/O Registers */
#define	ACPI_BASE_ADDR_OFFSET	0x40
#define	ACPI_BASE_ADDR_MASK	0xFFFFFE00
#define	ACPI_MMIO_REG_LEN	0x100

#define	PM1_CNT			0x4
#define	SLEEP_TYPE_MASK		0xFFFFECFF
#define	SLEEP_TYPE_S5		0x1C00
#define	SLEEP_ENABLE		0x2000
#endif /* PMC_ATOM_H */
