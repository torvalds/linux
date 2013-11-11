#ifndef __ASM_MACH_REGS_OST_H
#define __ASM_MACH_REGS_OST_H

#include <mach/hardware.h>

/*
 * OS Timer & Match Registers
 */

#define OSMR0		io_p2v(0x40A00000)  /* */
#define OSMR1		io_p2v(0x40A00004)  /* */
#define OSMR2		io_p2v(0x40A00008)  /* */
#define OSMR3		io_p2v(0x40A0000C)  /* */
#define OSMR4		io_p2v(0x40A00080)  /* */
#define OSCR		io_p2v(0x40A00010)  /* OS Timer Counter Register */
#define OSCR4		io_p2v(0x40A00040)  /* OS Timer Counter Register */
#define OMCR4		io_p2v(0x40A000C0)  /* */
#define OSSR		io_p2v(0x40A00014)  /* OS Timer Status Register */
#define OWER		io_p2v(0x40A00018)  /* OS Timer Watchdog Enable Register */
#define OIER		io_p2v(0x40A0001C)  /* OS Timer Interrupt Enable Register */

#define OSSR_M3		(1 << 3)	/* Match status channel 3 */
#define OSSR_M2		(1 << 2)	/* Match status channel 2 */
#define OSSR_M1		(1 << 1)	/* Match status channel 1 */
#define OSSR_M0		(1 << 0)	/* Match status channel 0 */

#define OWER_WME	(1 << 0)	/* Watchdog Match Enable */

#define OIER_E3		(1 << 3)	/* Interrupt enable channel 3 */
#define OIER_E2		(1 << 2)	/* Interrupt enable channel 2 */
#define OIER_E1		(1 << 1)	/* Interrupt enable channel 1 */
#define OIER_E0		(1 << 0)	/* Interrupt enable channel 0 */

#endif /* __ASM_MACH_REGS_OST_H */
