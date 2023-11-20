/*
 * This file is part of the Chelsio T4/T5/T6 Ethernet driver for Linux.
 *
 * Copyright (c) 2017 Chelsio Communications, Inc. All rights reserved.
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

#ifndef __CXGB4_SMT_H
#define __CXGB4_SMT_H

#include <linux/spinlock.h>
#include <linux/if_ether.h>
#include <linux/atomic.h>

struct adapter;
struct cpl_smt_write_rpl;

/* SMT related handling. Heavily adapted based on l2t ops in l2t.h/l2t.c
 */
enum {
	SMT_STATE_SWITCHING,
	SMT_STATE_UNUSED,
	SMT_STATE_ERROR
};

enum {
	SMT_SIZE = 256
};

struct smt_entry {
	u16 state;
	u16 idx;
	u16 pfvf;
	u8 src_mac[ETH_ALEN];
	int refcnt;
	spinlock_t lock;	/* protect smt entry add,removal */
};

struct smt_data {
	unsigned int smt_size;
	rwlock_t lock;
	struct smt_entry smtab[] __counted_by(smt_size);
};

struct smt_data *t4_init_smt(void);
struct smt_entry *cxgb4_smt_alloc_switching(struct net_device *dev, u8 *smac);
void cxgb4_smt_release(struct smt_entry *e);
void do_smt_write_rpl(struct adapter *p, const struct cpl_smt_write_rpl *rpl);
#endif /* __CXGB4_SMT_H */
