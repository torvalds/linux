/*
 * GPL HEADER START
 *
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 only,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License version 2 for more details (a copy is included
 * in the LICENSE file that accompanied this code).
 *
 * You should have received a copy of the GNU General Public License
 * version 2 along with this program; If not, see
 * http://www.sun.com/software/products/lustre/docs/GPLv2.pdf
 *
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa Clara,
 * CA 95054 USA or visit www.sun.com if you need additional information or
 * have any questions.
 *
 * GPL HEADER END
 */
/*
 * Copyright (c) 2008, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * libcfs/libcfs/linux/linux-utils.c
 *
 * Author: Phil Schwan <phil@clusterfs.com>
 */

/*
 * miscellaneous libcfs stuff
 */
#define DEBUG_SUBSYSTEM S_LNET
#include <linux/libcfs/libcfs.h>
#include <linux/lnet/lnet.h>

/*
 * Convert server error code to client format. Error codes are from
 * Linux errno.h, so for Linux client---identity.
 */
int convert_server_error(__u64 ecode)
{
	return ecode;
}
EXPORT_SYMBOL(convert_server_error);

/*
 * convert <fcntl.h> flag from client to server.
 */
int convert_client_oflag(int cflag, int *result)
{
	*result = cflag;
	return 0;
}
EXPORT_SYMBOL(convert_client_oflag);

void cfs_stack_trace_fill(struct cfs_stack_trace *trace)
{}

EXPORT_SYMBOL(cfs_stack_trace_fill);

void *cfs_stack_trace_frame(struct cfs_stack_trace *trace, int frame_no)
{
	return NULL;
}
EXPORT_SYMBOL(cfs_stack_trace_frame);
