/*
 * Hardware definitions common to all DaVinci family processors
 *
 * Author: Kevin Hilman, Deep Root Systems, LLC
 *
 * 2007 (c) Deep Root Systems, LLC. This file is licensed under
 * the terms of the GNU General Public License version 2. This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */
#ifndef __ASM_ARCH_HARDWARE_H
#define __ASM_ARCH_HARDWARE_H

/*
 * Before you add anything to ths file:
 *
 * This header is for defines common to ALL DaVinci family chips.
 * Anything that is chip specific should go in <chipname>.h,
 * and the chip/board init code should then explicitly include
 * <chipname>.h
 */
/*
 * I/O mapping
 */
#define IO_PHYS				0x01c00000UL
#define IO_OFFSET			0xfd000000 /* Virtual IO = 0xfec00000 */
#define IO_SIZE				0x00400000
#define IO_VIRT				(IO_PHYS + IO_OFFSET)
#define io_v2p(va)			((va) - IO_OFFSET)
#define __IO_ADDRESS(x)			((x) + IO_OFFSET)
#define IO_ADDRESS(pa)			IOMEM(__IO_ADDRESS(pa))

#endif /* __ASM_ARCH_HARDWARE_H */
