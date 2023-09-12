/*
 * IBM PowerPC eBus Infrastructure Support.
 *
 * Copyright (c) 2005 IBM Corporation
 *  Joachim Fenkes <fenkes@de.ibm.com>
 *  Heiko J Schick <schickhj@de.ibm.com>
 *
 * All rights reserved.
 *
 * This source code is distributed under a dual license of GPL v2.0 and OpenIB
 * BSD.
 *
 * OpenIB BSD License
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 *
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials
 * provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _ASM_EBUS_H
#define _ASM_EBUS_H
#ifdef __KERNEL__

#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/mod_devicetable.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>

struct platform_driver;

extern struct bus_type ibmebus_bus_type;

int ibmebus_register_driver(struct platform_driver *drv);
void ibmebus_unregister_driver(struct platform_driver *drv);

int ibmebus_request_irq(u32 ist, irq_handler_t handler,
			unsigned long irq_flags, const char *devname,
			void *dev_id);
void ibmebus_free_irq(u32 ist, void *dev_id);

#endif /* __KERNEL__ */
#endif /* _ASM_IBMEBUS_H */
