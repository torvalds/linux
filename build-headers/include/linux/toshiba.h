/* SPDX-License-Identifier: GPL-2.0+ WITH Linux-syscall-note */
/* toshiba.h -- Linux driver for accessing the SMM on Toshiba laptops 
 *
 * Copyright (c) 1996-2000  Jonathan A. Buzzard (jonathan@buzzard.org.uk)
 * Copyright (c) 2015  Azael Avalos <coproscefalo@gmail.com>
 *
 * Thanks to Juergen Heinzl <juergen@monocerus.demon.co.uk> for the pointers
 * on making sure the structure is aligned and packed.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 */

#ifndef _LINUX_TOSHIBA_H
#define _LINUX_TOSHIBA_H

/*
 * Toshiba modules paths
 */

#define TOSH_PROC		"/proc/toshiba"
#define TOSH_DEVICE		"/dev/toshiba"
#define TOSHIBA_ACPI_PROC	"/proc/acpi/toshiba"
#define TOSHIBA_ACPI_DEVICE	"/dev/toshiba_acpi"

/*
 * Toshiba SMM structure
 */

typedef struct {
	unsigned int eax;
	unsigned int ebx __attribute__ ((packed));
	unsigned int ecx __attribute__ ((packed));
	unsigned int edx __attribute__ ((packed));
	unsigned int esi __attribute__ ((packed));
	unsigned int edi __attribute__ ((packed));
} SMMRegisters;

/*
 * IOCTLs (0x90 - 0x91)
 */

#define TOSH_SMM		_IOWR('t', 0x90, SMMRegisters)
/*
 * Convenience toshiba_acpi command.
 *
 * The System Configuration Interface (SCI) is opened/closed internally
 * to avoid userspace of buggy BIOSes.
 *
 * The toshiba_acpi module checks whether the eax register is set with
 * SCI_GET (0xf300) or SCI_SET (0xf400), returning -EINVAL if not.
 */
#define TOSHIBA_ACPI_SCI	_IOWR('t', 0x91, SMMRegisters)


#endif /* _LINUX_TOSHIBA_H */
