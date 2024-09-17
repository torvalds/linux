/*
 * This file is part of the Chelsio T4 Ethernet driver for Linux.
 *
 * Copyright (c) 2015 Chelsio Communications, Inc. All rights reserved.
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

#ifndef __CXGB4_FCOE_H__
#define __CXGB4_FCOE_H__

#ifdef CONFIG_CHELSIO_T4_FCOE

#define CXGB_FCOE_TXPKT_CSUM_START	28
#define CXGB_FCOE_TXPKT_CSUM_END	8

/* fcoe flags */
enum {
	CXGB_FCOE_ENABLED     = (1 << 0),
};

struct cxgb_fcoe {
	u8	flags;
};

int cxgb_fcoe_enable(struct net_device *);
int cxgb_fcoe_disable(struct net_device *);
bool cxgb_fcoe_sof_eof_supported(struct adapter *, struct sk_buff *);

#endif /* CONFIG_CHELSIO_T4_FCOE */
#endif /* __CXGB4_FCOE_H__ */
