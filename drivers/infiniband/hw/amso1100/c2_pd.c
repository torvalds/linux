/*
 * Copyright (c) 2004 Topspin Communications.  All rights reserved.
 * Copyright (c) 2005 Cisco Systems.  All rights reserved.
 * Copyright (c) 2005 Mellanox Technologies. All rights reserved.
 * Copyright (c) 2005 Open Grid Computing, Inc. All rights reserved.
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

#include <linux/init.h>
#include <linux/slab.h>
#include <linux/errno.h>

#include "c2.h"
#include "c2_provider.h"

int c2_pd_alloc(struct c2_dev *c2dev, int privileged, struct c2_pd *pd)
{
	u32 obj;
	int ret = 0;

	spin_lock(&c2dev->pd_table.lock);
	obj = find_next_zero_bit(c2dev->pd_table.table, c2dev->pd_table.max,
				 c2dev->pd_table.last);
	if (obj >= c2dev->pd_table.max)
		obj = find_first_zero_bit(c2dev->pd_table.table,
					  c2dev->pd_table.max);
	if (obj < c2dev->pd_table.max) {
		pd->pd_id = obj;
		__set_bit(obj, c2dev->pd_table.table);
		c2dev->pd_table.last = obj+1;
		if (c2dev->pd_table.last >= c2dev->pd_table.max)
			c2dev->pd_table.last = 0;
	} else
		ret = -ENOMEM;
	spin_unlock(&c2dev->pd_table.lock);
	return ret;
}

void c2_pd_free(struct c2_dev *c2dev, struct c2_pd *pd)
{
	spin_lock(&c2dev->pd_table.lock);
	__clear_bit(pd->pd_id, c2dev->pd_table.table);
	spin_unlock(&c2dev->pd_table.lock);
}

int __devinit c2_init_pd_table(struct c2_dev *c2dev)
{

	c2dev->pd_table.last = 0;
	c2dev->pd_table.max = c2dev->props.max_pd;
	spin_lock_init(&c2dev->pd_table.lock);
	c2dev->pd_table.table = kmalloc(BITS_TO_LONGS(c2dev->props.max_pd) *
					sizeof(long), GFP_KERNEL);
	if (!c2dev->pd_table.table)
		return -ENOMEM;
	bitmap_zero(c2dev->pd_table.table, c2dev->props.max_pd);
	return 0;
}

void __devexit c2_cleanup_pd_table(struct c2_dev *c2dev)
{
	kfree(c2dev->pd_table.table);
}
