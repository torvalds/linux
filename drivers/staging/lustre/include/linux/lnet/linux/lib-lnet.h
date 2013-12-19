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
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 */

#ifndef __LNET_LINUX_LIB_LNET_H__
#define __LNET_LINUX_LIB_LNET_H__

#ifndef __LNET_LIB_LNET_H__
#error Do not #include this file directly. #include <linux/lnet/lib-lnet.h> instead
#endif

# include <asm/page.h>
# include <linux/string.h>
# include <asm/io.h>
# include <linux/libcfs/libcfs.h>

static inline __u64
lnet_page2phys (struct page *p)
{
	/* compiler optimizer will elide unused branches */

	switch (sizeof(typeof(page_to_phys(p)))) {
	case 4:
		/* page_to_phys returns a 32 bit physical address.  This must
		 * be a 32 bit machine with <= 4G memory and we must ensure we
		 * don't sign extend when converting to 64 bits. */
		return (unsigned long)page_to_phys(p);

	case 8:
		/* page_to_phys returns a 64 bit physical address :) */
		return page_to_phys(p);

	default:
		LBUG();
		return 0;
	}
}


#define LNET_ROUTER

#endif /* __LNET_LINUX_LIB_LNET_H__ */
