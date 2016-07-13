/*
 * Copyright 2015 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 *
 */
#ifndef _CGS_LINUX_H
#define _CGS_LINUX_H

#include "cgs_common.h"

/**
 * cgs_irq_source_set_func() - Callback for enabling/disabling interrupt sources
 * @private_data:  private data provided to cgs_add_irq_source
 * @src_id:        interrupt source ID
 * @type:          interrupt type
 * @enabled:       0 = disable source, non-0 = enable source
 *
 * Return:  0 on success, -errno otherwise
 */
typedef int (*cgs_irq_source_set_func_t)(void *private_data,
					 unsigned src_id, unsigned type,
					 int enabled);

/**
 * cgs_irq_handler_func() - Interrupt handler callback
 * @private_data:  private data provided to cgs_add_irq_source
 * @src_id:        interrupt source ID
 * @iv_entry:      pointer to raw ih ring entry
 *
 * This callback runs in interrupt context.
 *
 * Return:  0 on success, -errno otherwise
 */
typedef int (*cgs_irq_handler_func_t)(void *private_data,
				      unsigned src_id, const uint32_t *iv_entry);

/**
 * cgs_add_irq_source() - Add an IRQ source
 * @cgs_device:    opaque device handle
 * @src_id:        interrupt source ID
 * @num_types:     number of interrupt types that can be independently enabled
 * @set:           callback function to enable/disable an interrupt type
 * @handler:       interrupt handler callback
 * @private_data:  private data to pass to callback functions
 *
 * The same IRQ source can be added only once. Adding an IRQ source
 * indicates ownership of that IRQ source and all its IRQ types.
 *
 * Return:  0 on success, -errno otherwise
 */
typedef int (*cgs_add_irq_source_t)(struct cgs_device *cgs_device, unsigned src_id,
				    unsigned num_types,
				    cgs_irq_source_set_func_t set,
				    cgs_irq_handler_func_t handler,
				    void *private_data);

/**
 * cgs_irq_get() - Request enabling an IRQ source and type
 * @cgs_device:  opaque device handle
 * @src_id:      interrupt source ID
 * @type:        interrupt type
 *
 * cgs_irq_get and cgs_irq_put calls must be balanced. They count
 * "references" to IRQ sources.
 *
 * Return:  0 on success, -errno otherwise
 */
typedef int (*cgs_irq_get_t)(struct cgs_device *cgs_device, unsigned src_id, unsigned type);

/**
 * cgs_irq_put() - Indicate IRQ source is no longer needed
 * @cgs_device:  opaque device handle
 * @src_id:      interrupt source ID
 * @type:        interrupt type
 *
 * cgs_irq_get and cgs_irq_put calls must be balanced. They count
 * "references" to IRQ sources. Even after cgs_irq_put is called, the
 * IRQ handler may still be called if there are more refecences to
 * the IRQ source.
 *
 * Return:  0 on success, -errno otherwise
 */
typedef int (*cgs_irq_put_t)(struct cgs_device *cgs_device, unsigned src_id, unsigned type);

struct cgs_os_ops {
	/* IRQ handling */
	cgs_add_irq_source_t add_irq_source;
	cgs_irq_get_t irq_get;
	cgs_irq_put_t irq_put;
};

#define cgs_add_irq_source(dev,src_id,num_types,set,handler,private_data) \
	CGS_OS_CALL(add_irq_source,dev,src_id,num_types,set,handler,	\
		    private_data)
#define cgs_irq_get(dev,src_id,type)		\
	CGS_OS_CALL(irq_get,dev,src_id,type)
#define cgs_irq_put(dev,src_id,type)		\
	CGS_OS_CALL(irq_put,dev,src_id,type)

#endif /* _CGS_LINUX_H */
