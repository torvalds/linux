/*
 * Copyright (C) 2006-2007 Chelsio Communications.  All rights reserved.
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
#ifndef _T3CDEV_H_
#define _T3CDEV_H_

#include <linux/list.h>
#include <asm/atomic.h>
#include <linux/netdevice.h>
#include <linux/proc_fs.h>
#include <linux/skbuff.h>
#include <net/neighbour.h>

#define T3CNAMSIZ 16

struct cxgb3_client;

enum t3ctype {
	T3A = 0,
	T3B,
	T3C,
};

struct t3cdev {
	char name[T3CNAMSIZ];	/* T3C device name */
	enum t3ctype type;
	struct list_head ofld_dev_list;	/* for list linking */
	struct net_device *lldev;	/* LL dev associated with T3C messages */
	struct proc_dir_entry *proc_dir;	/* root of proc dir for this T3C */
	int (*send)(struct t3cdev *dev, struct sk_buff *skb);
	int (*recv)(struct t3cdev *dev, struct sk_buff **skb, int n);
	int (*ctl)(struct t3cdev *dev, unsigned int req, void *data);
	void (*neigh_update)(struct t3cdev *dev, struct neighbour *neigh);
	void *priv;		/* driver private data */
	void *l2opt;		/* optional layer 2 data */
	void *l3opt;		/* optional layer 3 data */
	void *l4opt;		/* optional layer 4 data */
	void *ulp;		/* ulp stuff */
	void *ulp_iscsi;	/* ulp iscsi */
};

#endif				/* _T3CDEV_H_ */
