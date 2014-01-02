/*
 * Copyright 2012 Tilera Corporation. All Rights Reserved.
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
#ifndef _GXIO_USB_H_
#define _GXIO_USB_H_

#include <gxio/common.h>

#include <hv/drv_usb_host_intf.h>
#include <hv/iorpc.h>

/*
 *
 * An API for manipulating general-purpose I/O pins.
 */

/*
 *
 * The USB shim allows access to the processor's Universal Serial Bus
 * connections.
 */

/* A context object used to manage USB hardware resources. */
typedef struct {

	/* File descriptor for calling up to the hypervisor. */
	int fd;

	/* The VA at which our MMIO registers are mapped. */
	char *mmio_base;
} gxio_usb_host_context_t;

/* Initialize a USB context.
 *
 *  A properly initialized context must be obtained before any of the other
 *  gxio_usb_host routines may be used.
 *
 * @param context Pointer to a gxio_usb_host_context_t, which will be
 *  initialized by this routine, if it succeeds.
 * @param usb_index Index of the USB shim to use.
 * @param is_ehci Nonzero to use the EHCI interface; zero to use the OHCI
 *  intereface.
 * @return Zero if the context was successfully initialized, else a
 *  GXIO_ERR_xxx error code.
 */
extern int gxio_usb_host_init(gxio_usb_host_context_t *context, int usb_index,
			      int is_ehci);

/* Destroy a USB context.
 *
 *  Once destroyed, a context may not be used with any gxio_usb_host routines
 *  other than gxio_usb_host_init().  After this routine returns, no further
 *  interrupts or signals requested on this context will be delivered.  The
 *  state and configuration of the pins which had been attached to this
 *  context are unchanged by this operation.
 *
 * @param context Pointer to a gxio_usb_host_context_t.
 * @return Zero if the context was successfully destroyed, else a
 *  GXIO_ERR_xxx error code.
 */
extern int gxio_usb_host_destroy(gxio_usb_host_context_t *context);

/* Retrieve the address of the shim's MMIO registers.
 *
 * @param context Pointer to a properly initialized gxio_usb_host_context_t.
 * @return The address of the shim's MMIO registers.
 */
extern void *gxio_usb_host_get_reg_start(gxio_usb_host_context_t *context);

/* Retrieve the length of the shim's MMIO registers.
 *
 * @param context Pointer to a properly initialized gxio_usb_host_context_t.
 * @return The length of the shim's MMIO registers.
 */
extern size_t gxio_usb_host_get_reg_len(gxio_usb_host_context_t *context);

#endif /* _GXIO_USB_H_ */
