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

#ifndef _GXIO_UART_H_
#define _GXIO_UART_H_

#include "common.h"

#include <hv/drv_uart_intf.h>
#include <hv/iorpc.h>

/*
 *
 * An API for manipulating UART interface.
 */

/*
 *
 * The Rshim allows access to the processor's UART interface.
 */

/* A context object used to manage UART resources. */
typedef struct {

	/* File descriptor for calling up to the hypervisor. */
	int fd;

	/* The VA at which our MMIO registers are mapped. */
	char *mmio_base;

} gxio_uart_context_t;

/* Request UART interrupts.
 *
 *  Request that interrupts be delivered to a tile when the UART's
 *  Receive FIFO is written, or the Write FIFO is read.
 *
 * @param context Pointer to a properly initialized gxio_uart_context_t.
 * @param bind_cpu_x X coordinate of CPU to which interrupt will be delivered.
 * @param bind_cpu_y Y coordinate of CPU to which interrupt will be delivered.
 * @param bind_interrupt IPI interrupt number.
 * @param bind_event Sub-interrupt event bit number; a negative value can
 *  disable the interrupt.
 * @return Zero if all of the requested UART events were successfully
 *  configured to interrupt.
 */
extern int gxio_uart_cfg_interrupt(gxio_uart_context_t *context,
				   int bind_cpu_x,
				   int bind_cpu_y,
				   int bind_interrupt, int bind_event);

/* Initialize a UART context.
 *
 *  A properly initialized context must be obtained before any of the other
 *  gxio_uart routines may be used.
 *
 * @param context Pointer to a gxio_uart_context_t, which will be initialized
 *  by this routine, if it succeeds.
 * @param uart_index Index of the UART to use.
 * @return Zero if the context was successfully initialized, else a
 *  GXIO_ERR_xxx error code.
 */
extern int gxio_uart_init(gxio_uart_context_t *context, int uart_index);

/* Destroy a UART context.
 *
 *  Once destroyed, a context may not be used with any gxio_uart routines
 *  other than gxio_uart_init().  After this routine returns, no further
 *  interrupts requested on this context will be delivered.  The state and
 *  configuration of the pins which had been attached to this context are
 *  unchanged by this operation.
 *
 * @param context Pointer to a gxio_uart_context_t.
 * @return Zero if the context was successfully destroyed, else a
 *  GXIO_ERR_xxx error code.
 */
extern int gxio_uart_destroy(gxio_uart_context_t *context);

/* Write UART register.
 * @param context Pointer to a gxio_uart_context_t.
 * @param offset UART register offset.
 * @param word Data will be wrote to UART reigister.
 */
extern void gxio_uart_write(gxio_uart_context_t *context, uint64_t offset,
			    uint64_t word);

/* Read UART register.
 * @param context Pointer to a gxio_uart_context_t.
 * @param offset UART register offset.
 * @return Data read from UART register.
 */
extern uint64_t gxio_uart_read(gxio_uart_context_t *context, uint64_t offset);

#endif /* _GXIO_UART_H_ */
