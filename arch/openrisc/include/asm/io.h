/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * OpenRISC Linux
 *
 * Linux architectural port borrowing liberally from similar works of
 * others.  All original copyrights apply as per the original source
 * declaration.
 *
 * OpenRISC implementation:
 * Copyright (C) 2010-2011 Jonas Bonn <jonas@southpole.se>
 * et al.
 */

#ifndef __ASM_OPENRISC_IO_H
#define __ASM_OPENRISC_IO_H

#include <linux/types.h>
#include <asm/pgalloc.h>
#include <asm/pgtable.h>

/*
 * PCI: We do not use IO ports in OpenRISC
 */
#define IO_SPACE_LIMIT		0

/* OpenRISC has no port IO */
#define HAVE_ARCH_PIO_SIZE	1
#define PIO_RESERVED		0X0UL
#define PIO_OFFSET		0
#define PIO_MASK		0

/*
 * I/O memory mapping functions.
 */
#define _PAGE_IOREMAP (pgprot_val(PAGE_KERNEL) | _PAGE_CI)

#include <asm-generic/io.h>

#endif
