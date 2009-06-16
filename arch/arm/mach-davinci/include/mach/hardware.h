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
#define DAVINCI_SYSTEM_MODULE_BASE        0x01C40000

/* System control register offsets */
#define DM64XX_VDD3P3V_PWDN	0x48

#endif /* __ASM_ARCH_HARDWARE_H */
