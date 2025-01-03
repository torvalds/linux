/*
 * This file is part of the Chelsio T6 Ethernet driver for Linux.
 *
 * Copyright (c) 2017-2018 Chelsio Communications, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef __CXGB4_SRQ_H
#define __CXGB4_SRQ_H

struct adapter;
struct cpl_srq_table_rpl;

#define SRQ_WAIT_TO	(HZ * 5)

struct srq_entry {
	u8 valid;
	u8 idx;
	u8 qlen;
	u16 pdid;
	u16 cur_msn;
	u16 max_msn;
	u32 qbase;
};

struct srq_data {
	unsigned int srq_size;
	struct srq_entry *entryp;
	struct completion comp;
	struct mutex lock; /* generic mutex for srq data */
};

struct srq_data *t4_init_srq(int srq_size);
void do_srq_table_rpl(struct adapter *adap,
		      const struct cpl_srq_table_rpl *rpl);
#endif  /* __CXGB4_SRQ_H */
