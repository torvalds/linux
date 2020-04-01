/*
 * This file is part of the Chelsio T4 Ethernet driver for Linux.
 *
 * Copyright (c) 2003-2016 Chelsio Communications, Inc. All rights reserved.
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

#ifndef __CXGB4_FILTER_H
#define __CXGB4_FILTER_H

#include "t4_msg.h"

#define WORD_MASK	0xffffffff

void filter_rpl(struct adapter *adap, const struct cpl_set_tcb_rpl *rpl);
void hash_filter_rpl(struct adapter *adap, const struct cpl_act_open_rpl *rpl);
void hash_del_filter_rpl(struct adapter *adap,
			 const struct cpl_abort_rpl_rss *rpl);
void clear_filter(struct adapter *adap, struct filter_entry *f);

int set_filter_wr(struct adapter *adapter, int fidx);
int delete_filter(struct adapter *adapter, unsigned int fidx);

int writable_filter(struct filter_entry *f);
void clear_all_filters(struct adapter *adapter);
void init_hash_filter(struct adapter *adap);
bool is_filter_exact_match(struct adapter *adap,
			   struct ch_filter_specification *fs);
#endif /* __CXGB4_FILTER_H */
