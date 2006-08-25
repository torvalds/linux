/*
 * Copyright (c) 2006 QLogic, Inc. All rights reserved.
 * Copyright (c) 2003, 2004, 2005, 2006 PathScale, Inc. All rights reserved.
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

#ifndef _IPATH_LAYER_H
#define _IPATH_LAYER_H

/*
 * This header file is for symbols shared between the infinipath driver
 * and drivers layered upon it (such as ipath).
 */

struct sk_buff;
struct ipath_devdata;
struct ether_header;

int ipath_layer_register(void *(*l_add)(int, struct ipath_devdata *),
			 void (*l_remove)(void *),
			 int (*l_intr)(void *, u32),
			 int (*l_rcv)(void *, void *,
				      struct sk_buff *),
			 u16 rcv_opcode,
			 int (*l_rcv_lid)(void *, void *));
void ipath_layer_unregister(void);
int ipath_layer_open(struct ipath_devdata *, u32 * pktmax);
u16 ipath_layer_get_lid(struct ipath_devdata *dd);
int ipath_layer_get_mac(struct ipath_devdata *dd, u8 *);
u16 ipath_layer_get_bcast(struct ipath_devdata *dd);
int ipath_layer_send_hdr(struct ipath_devdata *dd,
			 struct ether_header *hdr);
int ipath_layer_set_piointbufavail_int(struct ipath_devdata *dd);

/* ipath_ether interrupt values */
#define IPATH_LAYER_INT_IF_UP 0x2
#define IPATH_LAYER_INT_IF_DOWN 0x4
#define IPATH_LAYER_INT_LID 0x8
#define IPATH_LAYER_INT_SEND_CONTINUE 0x10
#define IPATH_LAYER_INT_BCAST 0x40

extern unsigned ipath_debug; /* debugging bit mask */

#endif				/* _IPATH_LAYER_H */
