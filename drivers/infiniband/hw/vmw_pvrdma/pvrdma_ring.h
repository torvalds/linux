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

#ifndef __PVRDMA_RING_H__
#define __PVRDMA_RING_H__

#include <linux/types.h>

#define PVRDMA_INVALID_IDX	-1	/* Invalid index. */

struct pvrdma_ring {
	atomic_t prod_tail;	/* Producer tail. */
	atomic_t cons_head;	/* Consumer head. */
};

struct pvrdma_ring_state {
	struct pvrdma_ring tx;	/* Tx ring. */
	struct pvrdma_ring rx;	/* Rx ring. */
};

static inline int pvrdma_idx_valid(__u32 idx, __u32 max_elems)
{
	/* Generates fewer instructions than a less-than. */
	return (idx & ~((max_elems << 1) - 1)) == 0;
}

static inline __s32 pvrdma_idx(atomic_t *var, __u32 max_elems)
{
	const unsigned int idx = atomic_read(var);

	if (pvrdma_idx_valid(idx, max_elems))
		return idx & (max_elems - 1);
	return PVRDMA_INVALID_IDX;
}

static inline void pvrdma_idx_ring_inc(atomic_t *var, __u32 max_elems)
{
	__u32 idx = atomic_read(var) + 1;	/* Increment. */

	idx &= (max_elems << 1) - 1;		/* Modulo size, flip gen. */
	atomic_set(var, idx);
}

static inline __s32 pvrdma_idx_ring_has_space(const struct pvrdma_ring *r,
					      __u32 max_elems, __u32 *out_tail)
{
	const __u32 tail = atomic_read(&r->prod_tail);
	const __u32 head = atomic_read(&r->cons_head);

	if (pvrdma_idx_valid(tail, max_elems) &&
	    pvrdma_idx_valid(head, max_elems)) {
		*out_tail = tail & (max_elems - 1);
		return tail != (head ^ max_elems);
	}
	return PVRDMA_INVALID_IDX;
}

static inline __s32 pvrdma_idx_ring_has_data(const struct pvrdma_ring *r,
					     __u32 max_elems, __u32 *out_head)
{
	const __u32 tail = atomic_read(&r->prod_tail);
	const __u32 head = atomic_read(&r->cons_head);

	if (pvrdma_idx_valid(tail, max_elems) &&
	    pvrdma_idx_valid(head, max_elems)) {
		*out_head = head & (max_elems - 1);
		return tail != head;
	}
	return PVRDMA_INVALID_IDX;
}

#endif /* __PVRDMA_RING_H__ */
