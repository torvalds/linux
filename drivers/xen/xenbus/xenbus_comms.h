/*
 * Private include for xenbus communications.
 *
 * Copyright (C) 2005 Rusty Russell, IBM Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation; or, when distributed
 * separately from the Linux kernel or incorporated into other
 * software packages, subject to the following license:
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this source file (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy, modify,
 * merge, publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#ifndef _XENBUS_COMMS_H
#define _XENBUS_COMMS_H

#include <linux/fs.h>

int xs_init(void);
int xb_init_comms(void);
void xb_deinit_comms(void);

/* Low level routines. */
int xb_write(const void *data, unsigned len);
int xb_read(void *data, unsigned len);
int xb_data_to_read(void);
int xb_wait_for_data_to_read(void);
int xs_input_avail(void);
extern struct xenstore_domain_interface *xen_store_interface;
extern int xen_store_evtchn;
extern enum xenstore_init xen_store_domain_type;

extern const struct file_operations xen_xenbus_fops;

#endif /* _XENBUS_COMMS_H */
