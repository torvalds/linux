/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_M68K_SERIAL_H
#define _ASM_M68K_SERIAL_H

#include <asm-generic/serial.h>

/* Standard COM flags (except for COM4, because of the 8514 problem) */
#ifdef CONFIG_SERIAL_8250_DETECT_IRQ
#define STD_COM_FLAGS (UPF_BOOT_AUTOCONF | UPF_SKIP_TEST | UPF_AUTO_IRQ)
#define STD_COM4_FLAGS (UPF_BOOT_AUTOCONF | UPF_AUTO_IRQ)
#else
#define STD_COM_FLAGS (UPF_BOOT_AUTOCONF | UPF_SKIP_TEST)
#define STD_COM4_FLAGS UPF_BOOT_AUTOCONF
#endif

#ifdef CONFIG_ISA
#define SERIAL_PORT_DFNS			\
	/* UART CLK   PORT IRQ     FLAGS        */			\
	{ 0, BASE_BAUD, 0x3F8, 4, STD_COM_FLAGS },	/* ttyS0 */	\
	{ 0, BASE_BAUD, 0x2F8, 3, STD_COM_FLAGS },	/* ttyS1 */	\
	{ 0, BASE_BAUD, 0x3E8, 4, STD_COM_FLAGS },	/* ttyS2 */	\
	{ 0, BASE_BAUD, 0x2E8, 3, STD_COM4_FLAGS },	/* ttyS3 */
#endif

#endif /* _ASM_M68K_SERIAL_H */
