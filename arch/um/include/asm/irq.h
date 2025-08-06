/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __UM_IRQ_H
#define __UM_IRQ_H

#define TIMER_IRQ		0
#define UMN_IRQ			1
#define UBD_IRQ			2
#define UM_ETH_IRQ		3
#define ACCEPT_IRQ		4
#define MCONSOLE_IRQ		5
#define WINCH_IRQ		6
#define SIGIO_WRITE_IRQ 	7
#define TELNETD_IRQ 		8
#define XTERM_IRQ 		9
#define RANDOM_IRQ 		10
#define SIGCHLD_IRQ		11

#ifdef CONFIG_UML_NET_VECTOR

#define VECTOR_BASE_IRQ		(SIGCHLD_IRQ + 1)
#define VECTOR_IRQ_SPACE	8

#define UM_FIRST_DYN_IRQ (VECTOR_IRQ_SPACE + VECTOR_BASE_IRQ)

#else

#define UM_FIRST_DYN_IRQ (SIGCHLD_IRQ + 1)

#endif

#define UM_LAST_SIGNAL_IRQ	64
/* If we have (simulated) PCI MSI, allow 64 more interrupt numbers for it */
#ifdef CONFIG_PCI_MSI
#define NR_IRQS			(UM_LAST_SIGNAL_IRQ + 64)
#else
#define NR_IRQS			UM_LAST_SIGNAL_IRQ
#endif /* CONFIG_PCI_MSI */

#include <asm-generic/irq.h>
#endif
