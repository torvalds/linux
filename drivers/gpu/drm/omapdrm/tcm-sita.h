/*
 * tcm_sita.h
 *
 * SImple Tiler Allocator (SiTA) private structures.
 *
 * Author: Ravi Ramachandra <r.ramachandra@ti.com>
 *
 * Copyright (C) 2009-2011 Texas Instruments, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * * Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 *
 * * Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 *
 * * Neither the name of Texas Instruments Incorporated nor the names of
 *   its contributors may be used to endorse or promote products derived
 *   from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _TCM_SITA_H
#define _TCM_SITA_H

#include "tcm.h"

/* length between two coordinates */
#define LEN(a, b) ((a) > (b) ? (a) - (b) + 1 : (b) - (a) + 1)

enum criteria {
	CR_MAX_NEIGHS		= 0x01,
	CR_FIRST_FOUND		= 0x10,
	CR_BIAS_HORIZONTAL	= 0x20,
	CR_BIAS_VERTICAL	= 0x40,
	CR_DIAGONAL_BALANCE	= 0x80
};

/* nearness to the beginning of the search field from 0 to 1000 */
struct nearness_factor {
	s32 x;
	s32 y;
};

/*
 * Statistics on immediately neighboring slots.  Edge is the number of
 * border segments that are also border segments of the scan field.  Busy
 * refers to the number of neighbors that are occupied.
 */
struct neighbor_stats {
	u16 edge;
	u16 busy;
};

/* structure to keep the score of a potential allocation */
struct score {
	struct nearness_factor	f;
	struct neighbor_stats	n;
	struct tcm_area		a;
	u16    neighs;		/* number of busy neighbors */
};

struct sita_pvt {
	spinlock_t lock;	/* spinlock to protect access */
	struct tcm_pt div_pt;	/* divider point splitting container */
	struct tcm_area ***map;	/* pointers to the parent area for each slot */
};

/* assign coordinates to area */
static inline
void assign(struct tcm_area *a, u16 x0, u16 y0, u16 x1, u16 y1)
{
	a->p0.x = x0;
	a->p0.y = y0;
	a->p1.x = x1;
	a->p1.y = y1;
}

#endif
