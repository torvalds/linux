/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright (c) 2010-2012 Broadcom. All rights reserved. */

#ifndef VCHIQ_PAGELIST_H
#define VCHIQ_PAGELIST_H

#define PAGELIST_WRITE 0
#define PAGELIST_READ 1
#define PAGELIST_READ_WITH_FRAGMENTS 2

struct pagelist {
	u32 length;
	u16 type;
	u16 offset;
	u32 addrs[1];	/* N.B. 12 LSBs hold the number
			 * of following pages at consecutive
			 * addresses.
			 */
};

#endif /* VCHIQ_PAGELIST_H */
