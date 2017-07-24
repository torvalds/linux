#ifndef _HFI1_EXP_RCV_H
#define _HFI1_EXP_RCV_H
/*
 * Copyright(c) 2017 Intel Corporation.
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * BSD LICENSE
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  - Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  - Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  - Neither the name of Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include "hfi.h"

#define EXP_TID_SET_EMPTY(set) (set.count == 0 && list_empty(&set.list))

#define EXP_TID_TIDLEN_MASK   0x7FFULL
#define EXP_TID_TIDLEN_SHIFT  0
#define EXP_TID_TIDCTRL_MASK  0x3ULL
#define EXP_TID_TIDCTRL_SHIFT 20
#define EXP_TID_TIDIDX_MASK   0x3FFULL
#define EXP_TID_TIDIDX_SHIFT  22
#define EXP_TID_GET(tid, field)	\
	(((tid) >> EXP_TID_TID##field##_SHIFT) & EXP_TID_TID##field##_MASK)

#define EXP_TID_SET(field, value)			\
	(((value) & EXP_TID_TID##field##_MASK) <<	\
	 EXP_TID_TID##field##_SHIFT)
#define EXP_TID_CLEAR(tid, field) ({					\
		(tid) &= ~(EXP_TID_TID##field##_MASK <<			\
			   EXP_TID_TID##field##_SHIFT);			\
		})
#define EXP_TID_RESET(tid, field, value) do {				\
		EXP_TID_CLEAR(tid, field);				\
		(tid) |= EXP_TID_SET(field, (value));			\
	} while (0)

/*
 * Define fields in the KDETH header so we can update the header
 * template.
 */
#define KDETH_OFFSET_SHIFT        0
#define KDETH_OFFSET_MASK         0x7fff
#define KDETH_OM_SHIFT            15
#define KDETH_OM_MASK             0x1
#define KDETH_TID_SHIFT           16
#define KDETH_TID_MASK            0x3ff
#define KDETH_TIDCTRL_SHIFT       26
#define KDETH_TIDCTRL_MASK        0x3
#define KDETH_INTR_SHIFT          28
#define KDETH_INTR_MASK           0x1
#define KDETH_SH_SHIFT            29
#define KDETH_SH_MASK             0x1
#define KDETH_KVER_SHIFT          30
#define KDETH_KVER_MASK           0x3
#define KDETH_JKEY_SHIFT          0x0
#define KDETH_JKEY_MASK           0xff
#define KDETH_HCRC_UPPER_SHIFT    16
#define KDETH_HCRC_UPPER_MASK     0xff
#define KDETH_HCRC_LOWER_SHIFT    24
#define KDETH_HCRC_LOWER_MASK     0xff

#define KDETH_GET(val, field)						\
	(((le32_to_cpu((val))) >> KDETH_##field##_SHIFT) & KDETH_##field##_MASK)
#define KDETH_SET(dw, field, val) do {					\
		u32 dwval = le32_to_cpu(dw);				\
		dwval &= ~(KDETH_##field##_MASK << KDETH_##field##_SHIFT); \
		dwval |= (((val) & KDETH_##field##_MASK) << \
			  KDETH_##field##_SHIFT);			\
		dw = cpu_to_le32(dwval);				\
	} while (0)

#define KDETH_RESET(dw, field, val) ({ dw = 0; KDETH_SET(dw, field, val); })

/* KDETH OM multipliers and switch over point */
#define KDETH_OM_SMALL     4
#define KDETH_OM_SMALL_SHIFT     2
#define KDETH_OM_LARGE     64
#define KDETH_OM_LARGE_SHIFT     6
#define KDETH_OM_MAX_SIZE  (1 << ((KDETH_OM_LARGE / KDETH_OM_SMALL) + 1))

struct tid_group {
	struct list_head list;
	u32 base;
	u8 size;
	u8 used;
	u8 map;
};

/*
 * Write an "empty" RcvArray entry.
 * This function exists so the TID registaration code can use it
 * to write to unused/unneeded entries and still take advantage
 * of the WC performance improvements. The HFI will ignore this
 * write to the RcvArray entry.
 */
static inline void rcv_array_wc_fill(struct hfi1_devdata *dd, u32 index)
{
	/*
	 * Doing the WC fill writes only makes sense if the device is
	 * present and the RcvArray has been mapped as WC memory.
	 */
	if ((dd->flags & HFI1_PRESENT) && dd->rcvarray_wc)
		writeq(0, dd->rcvarray_wc + (index * 8));
}

static inline void tid_group_add_tail(struct tid_group *grp,
				      struct exp_tid_set *set)
{
	list_add_tail(&grp->list, &set->list);
	set->count++;
}

static inline void tid_group_remove(struct tid_group *grp,
				    struct exp_tid_set *set)
{
	list_del_init(&grp->list);
	set->count--;
}

static inline void tid_group_move(struct tid_group *group,
				  struct exp_tid_set *s1,
				  struct exp_tid_set *s2)
{
	tid_group_remove(group, s1);
	tid_group_add_tail(group, s2);
}

static inline struct tid_group *tid_group_pop(struct exp_tid_set *set)
{
	struct tid_group *grp =
		list_first_entry(&set->list, struct tid_group, list);
	list_del_init(&grp->list);
	set->count--;
	return grp;
}

static inline u32 rcventry2tidinfo(u32 rcventry)
{
	u32 pair = rcventry & ~0x1;

	return EXP_TID_SET(IDX, pair >> 1) |
		EXP_TID_SET(CTRL, 1 << (rcventry - pair));
}

int hfi1_alloc_ctxt_rcv_groups(struct hfi1_ctxtdata *rcd);
void hfi1_free_ctxt_rcv_groups(struct hfi1_ctxtdata *rcd);
void hfi1_exp_tid_group_init(struct exp_tid_set *set);

#endif /* _HFI1_EXP_RCV_H */
