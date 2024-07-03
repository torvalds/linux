/* SPDX-License-Identifier: GPL-2.0 */
/*

 * Copyright (C) 2015 Thomas Meyer (thomas@m3y3r.de)
 * Copyright (C) 2005 Jeff Dike (jdike@karaya.com)
 */

#ifndef __STUB_DATA_H
#define __STUB_DATA_H

#include <linux/compiler_types.h>
#include <as-layout.h>

struct stub_data {
	unsigned long offset;
	int fd;
	long parent_err, child_err;

	/* 128 leaves enough room for additional fields in the struct */
	unsigned char syscall_data[UM_KERN_PAGE_SIZE - 128] __aligned(16);

	/* Stack for our signal handlers and for calling into . */
	unsigned char sigstack[UM_KERN_PAGE_SIZE] __aligned(UM_KERN_PAGE_SIZE);
};

#endif
