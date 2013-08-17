/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * Author: Rabin Vincent <rabin.vincent@stericsson.com>
 * License terms: GNU General Public License (GPL) version 2
 */

#ifndef __MACH_IRQS_BOARD_MOP500_H
#define __MACH_IRQS_BOARD_MOP500_H

/* Number of AB8500 irqs is taken from header file */
#include <linux/mfd/abx500/ab8500.h>

#define MOP500_AB8500_IRQ_BASE		IRQ_BOARD_START
#define MOP500_AB8500_IRQ_END		(MOP500_AB8500_IRQ_BASE \
					 + AB8500_MAX_NR_IRQS)

/* TC35892 */
#define TC35892_NR_INTERNAL_IRQS	8
#define TC35892_INT_GPIO(x)		(TC35892_NR_INTERNAL_IRQS + (x))
#define TC35892_NR_GPIOS		24
#define TC35892_NR_IRQS			TC35892_INT_GPIO(TC35892_NR_GPIOS)

#define MOP500_EGPIO_NR_IRQS		TC35892_NR_IRQS

#define MOP500_EGPIO_IRQ_BASE		MOP500_AB8500_IRQ_END
#define MOP500_EGPIO_IRQ_END		(MOP500_EGPIO_IRQ_BASE \
					 + MOP500_EGPIO_NR_IRQS)
/* STMPE1601 irqs */
#define STMPE_NR_INTERNAL_IRQS          9
#define STMPE_INT_GPIO(x)               (STMPE_NR_INTERNAL_IRQS + (x))
#define STMPE_NR_GPIOS                  24
#define STMPE_NR_IRQS                   STMPE_INT_GPIO(STMPE_NR_GPIOS)

#define MOP500_STMPE1601_IRQBASE        MOP500_EGPIO_IRQ_END
#define MOP500_STMPE1601_IRQ(x)         (MOP500_STMPE1601_IRQBASE + (x))

#define MOP500_STMPE1601_IRQ_END	\
	MOP500_STMPE1601_IRQ(STMPE_NR_INTERNAL_IRQS)

/* AB8500 virtual gpio IRQ */
#define AB8500_VIR_GPIO_NR_IRQS			16

#define MOP500_AB8500_VIR_GPIO_IRQ_BASE		\
	MOP500_STMPE1601_IRQ_END
#define MOP500_AB8500_VIR_GPIO_IRQ_END		\
	(MOP500_AB8500_VIR_GPIO_IRQ_BASE + AB8500_VIR_GPIO_NR_IRQS)

#define MOP500_NR_IRQS		MOP500_AB8500_VIR_GPIO_IRQ_END

#define MOP500_IRQ_END		MOP500_NR_IRQS

/*
 * We may have several boards, but only one will run at a
 * time, so the one with most IRQs will bump this ahead,
 * but the IRQ_BOARD_START remains the same for either board.
 */
#if MOP500_IRQ_END > IRQ_BOARD_END
#undef IRQ_BOARD_END
#define IRQ_BOARD_END	MOP500_IRQ_END
#endif

#endif
