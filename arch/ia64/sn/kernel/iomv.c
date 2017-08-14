/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2000-2003, 2006 Silicon Graphics, Inc. All rights reserved.
 */

#include <linux/module.h>
#include <linux/acpi.h>
#include <asm/io.h>
#include <asm/delay.h>
#include <asm/vga.h>
#include <asm/sn/nodepda.h>
#include <asm/sn/simulator.h>
#include <asm/sn/pda.h>
#include <asm/sn/sn_cpuid.h>
#include <asm/sn/shub_mmr.h>
#include <asm/sn/acpi.h>

#define IS_LEGACY_VGA_IOPORT(p) \
	(((p) >= 0x3b0 && (p) <= 0x3bb) || ((p) >= 0x3c0 && (p) <= 0x3df))

/**
 * sn_io_addr - convert an in/out port to an i/o address
 * @port: port to convert
 *
 * Legacy in/out instructions are converted to ld/st instructions
 * on IA64.  This routine will convert a port number into a valid
 * SN i/o address.  Used by sn_in*() and sn_out*().
 */

void *sn_io_addr(unsigned long port)
{
	if (!IS_RUNNING_ON_SIMULATOR()) {
		if (IS_LEGACY_VGA_IOPORT(port))
			return (__ia64_mk_io_addr(port));
		/* On sn2, legacy I/O ports don't point at anything */
		if (port < (64 * 1024))
			return NULL;
		if (SN_ACPI_BASE_SUPPORT())
			return (__ia64_mk_io_addr(port));
		else
			return ((void *)(port | __IA64_UNCACHED_OFFSET));
	} else {
		/* but the simulator uses them... */
		unsigned long addr;

		/*
		 * word align port, but need more than 10 bits
		 * for accessing registers in bedrock local block
		 * (so we don't do port&0xfff)
		 */
		addr = (is_shub2() ? 0xc00000028c000000UL : 0xc0000087cc000000UL) | ((port >> 2) << 12);
		if ((port >= 0x1f0 && port <= 0x1f7) || port == 0x3f6 || port == 0x3f7)
			addr |= port;
		return (void *)addr;
	}
}

EXPORT_SYMBOL(sn_io_addr);

/**
 * __sn_mmiowb - I/O space memory barrier
 *
 * See arch/ia64/include/asm/io.h and Documentation/driver-api/device-io.rst
 * for details.
 *
 * On SN2, we wait for the PIO_WRITE_STATUS SHub register to clear.
 * See PV 871084 for details about the WAR about zero value.
 *
 */
void __sn_mmiowb(void)
{
	volatile unsigned long *adr = pda->pio_write_status_addr;
	unsigned long val = pda->pio_write_status_val;

	while ((*adr & SH_PIO_WRITE_STATUS_PENDING_WRITE_COUNT_MASK) != val)
		cpu_relax();
}

EXPORT_SYMBOL(__sn_mmiowb);
