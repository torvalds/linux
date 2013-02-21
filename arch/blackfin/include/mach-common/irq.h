/*
 * Common Blackfin IRQ definitions (i.e. the CEC)
 *
 * Copyright 2005-2011 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later
 */

#ifndef _MACH_COMMON_IRQ_H_
#define _MACH_COMMON_IRQ_H_

/*
 * Core events interrupt source definitions
 *
 *  Event Source       Event Name
 *  Emulation          EMU            0  (highest priority)
 *  Reset              RST            1
 *  NMI                NMI            2
 *  Exception          EVX            3
 *  Reserved           --             4
 *  Hardware Error     IVHW           5
 *  Core Timer         IVTMR          6
 *  Peripherals        IVG7           7
 *  Peripherals        IVG8           8
 *  Peripherals        IVG9           9
 *  Peripherals        IVG10         10
 *  Peripherals        IVG11         11
 *  Peripherals        IVG12         12
 *  Peripherals        IVG13         13
 *  Softirq            IVG14         14
 *  System Call        IVG15         15  (lowest priority)
 */

/* The ABSTRACT IRQ definitions */
#define IRQ_EMU			0	/* Emulation */
#define IRQ_RST			1	/* reset */
#define IRQ_NMI			2	/* Non Maskable */
#define IRQ_EVX			3	/* Exception */
#define IRQ_UNUSED		4	/* - unused interrupt */
#define IRQ_HWERR		5	/* Hardware Error */
#define IRQ_CORETMR		6	/* Core timer */

#define IVG7			7
#define IVG8			8
#define IVG9			9
#define IVG10			10
#define IVG11			11
#define IVG12			12
#define IVG13			13
#define IVG14			14
#define IVG15			15

#define BFIN_IRQ(x)		((x) + IVG7)
#define BFIN_SYSIRQ(x)		((x) - IVG7)

#define NR_IRQS			(NR_MACH_IRQS + NR_SPARE_IRQS)

#endif
