/*
 * io.h
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * The io module manages IO between CHNL and msg_ctrl.
 *
 * Copyright (C) 2005-2006 Texas Instruments, Inc.
 *
 * This package is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * THIS PACKAGE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#ifndef IO_
#define IO_

#include <dspbridge/cfgdefs.h>
#include <dspbridge/devdefs.h>

/* IO Objects: */
struct io_mgr;

/* IO manager attributes: */
struct io_attrs {
	u8 birq;		/* Channel's I/O IRQ number. */
	bool irq_shared;	/* TRUE if the IRQ is shareable. */
	u32 word_size;		/* DSP Word size. */
	u32 shm_base;		/* Physical base address of shared memory. */
	u32 sm_length;		/* Size (in bytes) of shared memory. */
};


/*
 *  ======== io_create ========
 *  Purpose:
 *      Create an IO manager object, responsible for managing IO between
 *      CHNL and msg_ctrl.
 *  Parameters:
 *      channel_mgr:            Location to store a channel manager object on
 *                              output.
 *      hdev_obj:             Handle to a device object.
 *      mgr_attrts:              IO manager attributes.
 *      mgr_attrts->birq:        I/O IRQ number.
 *      mgr_attrts->irq_shared:     TRUE if the IRQ is shareable.
 *      mgr_attrts->word_size:   DSP Word size in equivalent PC bytes..
 *  Returns:
 *      0:                Success;
 *      -ENOMEM:            Insufficient memory for requested resources.
 *      -EIO:             Unable to plug channel ISR for configured IRQ.
 *      -EINVAL: Invalid DSP word size (must be > 0).
 *               Invalid base address for DSP communications.
 *  Requires:
 *      io_man != NULL.
 *      mgr_attrts != NULL.
 *  Ensures:
 */
extern int io_create(struct io_mgr **io_man,
			    struct dev_object *hdev_obj,
			    const struct io_attrs *mgr_attrts);

/*
 *  ======== io_destroy ========
 *  Purpose:
 *      Destroy the IO manager.
 *  Parameters:
 *      hio_mgr:         IOmanager object.
 *  Returns:
 *      0:        Success.
 *      -EFAULT:    hio_mgr was invalid.
 *  Requires:
 *  Ensures:
 */
extern int io_destroy(struct io_mgr *hio_mgr);

#endif /* CHNL_ */
