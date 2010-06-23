/*
 * drv_interface.h
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
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

#ifndef	_DRV_INTERFACE_H_
#define _DRV_INTERFACE_H_

/* Prototypes for all functions in this bridge */
static int __init bridge_init(void);	/* Initialize bridge */
static void __exit bridge_exit(void);	/* Opposite of initialize */
static int bridge_open(struct inode *, struct file *);	/* Open */
static int bridge_release(struct inode *, struct file *);	/* Release */
static long bridge_ioctl(struct file *, unsigned int, unsigned long);
static int bridge_mmap(struct file *filp, struct vm_area_struct *vma);
#endif /* ifndef _DRV_INTERFACE_H_ */
