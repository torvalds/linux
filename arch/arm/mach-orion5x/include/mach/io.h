/*
 * arch/arm/mach-orion5x/include/mach/io.h
 *
 * Tzachi Perelstein <tzachi@marvell.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#ifndef __ASM_ARCH_IO_H
#define __ASM_ARCH_IO_H

#include "orion5x.h"

#define IO_SPACE_LIMIT		0xffffffff

#define __io(a)			__typesafe_io(a)
#define __mem_pci(a)		(a)

#endif
