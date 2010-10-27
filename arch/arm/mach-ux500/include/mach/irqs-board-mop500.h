/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * Author: Rabin Vincent <rabin.vincent@stericsson.com>
 * License terms: GNU General Public License (GPL) version 2
 */

#ifndef __MACH_IRQS_BOARD_MOP500_H
#define __MACH_IRQS_BOARD_MOP500_H

#define AB8500_NR_IRQS			104

#define MOP500_AB8500_IRQ_BASE		IRQ_BOARD_START
#define MOP500_AB8500_IRQ_END		(MOP500_AB8500_IRQ_BASE \
					 + AB8500_NR_IRQS)
#define MOP500_IRQ_END			MOP500_AB8500_IRQ_END

#if MOP500_IRQ_END > IRQ_BOARD_END
#undef IRQ_BOARD_END
#define IRQ_BOARD_END	MOP500_IRQ_END
#endif

#endif
