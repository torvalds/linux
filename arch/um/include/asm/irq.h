/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __UM_IRQ_H
#define __UM_IRQ_H

#define TIMER_IRQ		0
#define UMN_IRQ			1
#define CONSOLE_IRQ		2
#define CONSOLE_WRITE_IRQ	3
#define UBD_IRQ			4
#define UM_ETH_IRQ		5
#define SSL_IRQ			6
#define SSL_WRITE_IRQ		7
#define ACCEPT_IRQ		8
#define MCONSOLE_IRQ		9
#define WINCH_IRQ		10
#define SIGIO_WRITE_IRQ 	11
#define TELNETD_IRQ 		12
#define XTERM_IRQ 		13
#define RANDOM_IRQ 		14

#ifdef CONFIG_UML_NET_VECTOR

#define VECTOR_BASE_IRQ		15
#define VECTOR_IRQ_SPACE	8

#define LAST_IRQ (VECTOR_IRQ_SPACE + VECTOR_BASE_IRQ - 1)

#else

#define LAST_IRQ RANDOM_IRQ

#endif

#define NR_IRQS (LAST_IRQ + 1)

#endif
