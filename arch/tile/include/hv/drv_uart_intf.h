/*
 * Copyright 2013 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 */

/**
 * Interface definitions for the UART driver.
 */

#ifndef _SYS_HV_DRV_UART_INTF_H
#define _SYS_HV_DRV_UART_INTF_H

#include <arch/uart.h>

/** Number of UART ports supported. */
#define TILEGX_UART_NR        2

/** The mmap file offset (PA) of the UART MMIO region. */
#define HV_UART_MMIO_OFFSET   0

/** The maximum size of the UARTs MMIO region (64K Bytes). */
#define HV_UART_MMIO_SIZE     (1UL << 16)

#endif /* _SYS_HV_DRV_UART_INTF_H */
