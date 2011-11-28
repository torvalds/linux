/* arch/arm/mach-zynq/include/mach/io.h
 *
 *  Copyright (C) 2011 Xilinx
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __MACH_IO_H__
#define __MACH_IO_H__

/* Allow IO space to be anywhere in the memory */

#define IO_SPACE_LIMIT 0xffff

/* IO address mapping macros, nothing special at this time but required */

#ifdef __ASSEMBLER__
#define IOMEM(x)		(x)
#else
#define IOMEM(x)		((void __force __iomem *)(x))
#endif

#define __io(a)			__typesafe_io(a)
#define __mem_pci(a)		(a)

#endif
