/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * Author: Rabin Vincent <rabin.vincent@stericsson.com>
 * License terms: GNU General Public License (GPL) version 2
 */

#ifndef __MACH_IRQS_BOARD_MOP500_H
#define __MACH_IRQS_BOARD_MOP500_H

/* Number of AB8500 irqs is taken from header file */
#include <linux/mfd/ab8500.h>

#define MOP500_AB8500_IRQ_BASE		IRQ_BOARD_START
#define MOP500_AB8500_IRQ_END		(MOP500_AB8500_IRQ_BASE \
					 + AB8500_NR_IRQS)

#define TC35892_NR_INTERNAL_IRQS	8
#define TC35892_INT_GPIO(x)		(TC35892_NR_INTERNAL_IRQS + (x))
#define TC35892_NR_GPIOS		24
#define TC35892_NR_IRQS			TC35892_INT_GPIO(TC35892_NR_GPIOS)

#define MOP500_EGPIO_NR_IRQS		TC35892_NR_IRQS

#define MOP500_EGPIO_IRQ_BASE		MOP500_AB8500_IRQ_END
#define MOP500_EGPIO_IRQ_END		(MOP500_EGPIO_IRQ_BASE \
					 + MOP500_EGPIO_NR_IRQS)

#define MOP500_IRQ_END			MOP500_EGPIO_IRQ_END

#if MOP500_IRQ_END > IRQ_BOARD_END
#undef IRQ_BOARD_END
#define IRQ_BOARD_END	MOP500_IRQ_END
#endif

#endif
