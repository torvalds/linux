/*
 * Copyright (c) 2012-2016 VMware, Inc.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of EITHER the GNU General Public License
 * version 2 as published by the Free Software Foundation or the BSD
 * 2-Clause License. This program is distributed in the hope that it
 * will be useful, but WITHOUT ANY WARRANTY; WITHOUT EVEN THE IMPLIED
 * WARRANTY OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License version 2 for more details at
 * http://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program available in the file COPYING in the main
 * directory of this source tree.
 *
 * The BSD 2-Clause License
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
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <linux/bitmap.h>
#include <linux/errno.h>
#include <linux/slab.h>

#include "pvrdma.h"

int pvrdma_uar_table_init(struct pvrdma_dev *dev)
{
	u32 num = dev->dsr->caps.max_uar;
	u32 mask = num - 1;
	struct pvrdma_id_table *tbl = &dev->uar_table.tbl;

	if (!is_power_of_2(num))
		return -EINVAL;

	tbl->last = 0;
	tbl->top = 0;
	tbl->max = num;
	tbl->mask = mask;
	spin_lock_init(&tbl->lock);
	tbl->table = bitmap_zalloc(num, GFP_KERNEL);
	if (!tbl->table)
		return -ENOMEM;

	/* 0th UAR is taken by the device. */
	__set_bit(0, tbl->table);

	return 0;
}

void pvrdma_uar_table_cleanup(struct pvrdma_dev *dev)
{
	struct pvrdma_id_table *tbl = &dev->uar_table.tbl;

	bitmap_free(tbl->table);
}

int pvrdma_uar_alloc(struct pvrdma_dev *dev, struct pvrdma_uar_map *uar)
{
	struct pvrdma_id_table *tbl;
	unsigned long flags;
	u32 obj;

	tbl = &dev->uar_table.tbl;

	spin_lock_irqsave(&tbl->lock, flags);
	obj = find_next_zero_bit(tbl->table, tbl->max, tbl->last);
	if (obj >= tbl->max) {
		tbl->top = (tbl->top + tbl->max) & tbl->mask;
		obj = find_first_zero_bit(tbl->table, tbl->max);
	}

	if (obj >= tbl->max) {
		spin_unlock_irqrestore(&tbl->lock, flags);
		return -ENOMEM;
	}

	__set_bit(obj, tbl->table);
	obj |= tbl->top;

	spin_unlock_irqrestore(&tbl->lock, flags);

	uar->index = obj;
	uar->pfn = (pci_resource_start(dev->pdev, PVRDMA_PCI_RESOURCE_UAR) >>
		    PAGE_SHIFT) + uar->index;

	return 0;
}

void pvrdma_uar_free(struct pvrdma_dev *dev, struct pvrdma_uar_map *uar)
{
	struct pvrdma_id_table *tbl = &dev->uar_table.tbl;
	unsigned long flags;
	u32 obj;

	obj = uar->index & (tbl->max - 1);
	spin_lock_irqsave(&tbl->lock, flags);
	__clear_bit(obj, tbl->table);
	tbl->last = min(tbl->last, obj);
	tbl->top = (tbl->top + tbl->max) & tbl->mask;
	spin_unlock_irqrestore(&tbl->lock, flags);
}
