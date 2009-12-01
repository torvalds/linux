#ifndef __ASM_MACH_REGS_INTC_H
#define __ASM_MACH_REGS_INTC_H

#include <mach/hardware.h>

/*
 * Interrupt Controller
 */

#define ICIP		__REG(0x40D00000)  /* Interrupt Controller IRQ Pending Register */
#define ICMR		__REG(0x40D00004)  /* Interrupt Controller Mask Register */
#define ICLR		__REG(0x40D00008)  /* Interrupt Controller Level Register */
#define ICFP		__REG(0x40D0000C)  /* Interrupt Controller FIQ Pending Register */
#define ICPR		__REG(0x40D00010)  /* Interrupt Controller Pending Register */
#define ICCR		__REG(0x40D00014)  /* Interrupt Controller Control Register */
#define ICHP		__REG(0x40D00018)  /* Interrupt Controller Highest Priority Register */

#define ICIP2		__REG(0x40D0009C)  /* Interrupt Controller IRQ Pending Register 2 */
#define ICMR2		__REG(0x40D000A0)  /* Interrupt Controller Mask Register 2 */
#define ICLR2		__REG(0x40D000A4)  /* Interrupt Controller Level Register 2 */
#define ICFP2		__REG(0x40D000A8)  /* Interrupt Controller FIQ Pending Register 2 */
#define ICPR2		__REG(0x40D000AC)  /* Interrupt Controller Pending Register 2 */

#define ICIP3		__REG(0x40D00130)  /* Interrupt Controller IRQ Pending Register 3 */
#define ICMR3		__REG(0x40D00134)  /* Interrupt Controller Mask Register 3 */
#define ICLR3		__REG(0x40D00138)  /* Interrupt Controller Level Register 3 */
#define ICFP3		__REG(0x40D0013C)  /* Interrupt Controller FIQ Pending Register 3 */
#define ICPR3		__REG(0x40D00140)  /* Interrupt Controller Pending Register 3 */

#define IPR(x)		__REG(0x40D0001C + (x < 32 ? (x << 2)		\
				: (x < 64 ? (0x94 + ((x - 32) << 2))	\
				: (0x128 + ((x - 64) << 2)))))

#endif /* __ASM_MACH_REGS_INTC_H */
