/* Standard UART definitions
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */

#ifndef _ASM_SERIAL_H
#define _ASM_SERIAL_H

/* Standard COM flags (except for COM4, because of the 8514 problem) */
#ifdef CONFIG_SERIAL_8250_DETECT_IRQ
#define STD_COM_FLAGS	(UPF_BOOT_AUTOCONF | UPF_SKIP_TEST | UPF_AUTO_IRQ)
#define STD_COM4_FLAGS	(UPF_BOOT_AUTOCONF | UPF_AUTO_IRQ)
#else
#define STD_COM_FLAGS	(UPF_BOOT_AUTOCONF | UPF_SKIP_TEST)
#define STD_COM4_FLAGS	UPF_BOOT_AUTOCONF
#endif

#ifdef CONFIG_SERIAL_8250_MANY_PORTS
#define FOURPORT_FLAGS	UPF_FOURPORT
#define ACCENT_FLAGS	0
#define BOCA_FLAGS	0
#define HUB6_FLAGS	0
#define RS_TABLE_SIZE	64
#else
#define RS_TABLE_SIZE
#endif

#include <unit/serial.h>

#endif /* _ASM_SERIAL_H */
