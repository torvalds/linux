// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2014-2025 Etnaviv Project
 */

#ifndef __ETNAVIV_BUFFER_H__
#define __ETNAVIV_BUFFER_H__

#include "etnaviv_cmdbuf.h"
#include "etnaviv_gpu.h"
#include "etnaviv_gem.h"
#include "etnaviv_mmu.h"

#include "common.xml.h"
#include "linux/printk.h"
#include "state.xml.h"
#include "state_blt.xml.h"
#include "state_hi.xml.h"
#include "state_3d.xml.h"
#include "cmdstream.xml.h"

static inline void OUT(struct etnaviv_cmdbuf *buffer, u32 data)
{
	u32 *vaddr = (u32 *)buffer->vaddr;

	BUG_ON(buffer->user_size >= buffer->size);

	vaddr[buffer->user_size / 4] = data;
	buffer->user_size += 4;
}

static inline void CMD_LOAD_STATE(struct etnaviv_cmdbuf *buffer, u32 reg,
				  u32 value)
{
	u32 index = reg >> VIV_FE_LOAD_STATE_HEADER_OFFSET__SHR;

	buffer->user_size = ALIGN(buffer->user_size, 8);

	/* write a register via cmd stream */
	OUT(buffer, VIV_FE_LOAD_STATE_HEADER_OP_LOAD_STATE |
			    VIV_FE_LOAD_STATE_HEADER_COUNT(1) |
			    VIV_FE_LOAD_STATE_HEADER_OFFSET(index));
	OUT(buffer, value);
}

static inline void CMD_LOAD_STATES_START(struct etnaviv_cmdbuf *buffer, u32 reg,
					 u32 nvalues)
{
	u32 index = reg >> VIV_FE_LOAD_STATE_HEADER_OFFSET__SHR;

	buffer->user_size = ALIGN(buffer->user_size, 8);

	/* write a register via cmd stream */
	OUT(buffer, VIV_FE_LOAD_STATE_HEADER_OP_LOAD_STATE |
			    VIV_FE_LOAD_STATE_HEADER_OFFSET(index) |
			    VIV_FE_LOAD_STATE_HEADER_COUNT(nvalues));
}

static inline void CMD_END(struct etnaviv_cmdbuf *buffer)
{
	buffer->user_size = ALIGN(buffer->user_size, 8);

	OUT(buffer, VIV_FE_END_HEADER_OP_END);
}

static inline void CMD_WAIT(struct etnaviv_cmdbuf *buffer,
			    unsigned int waitcycles)
{
	buffer->user_size = ALIGN(buffer->user_size, 8);

	OUT(buffer, VIV_FE_WAIT_HEADER_OP_WAIT | waitcycles);
}

static inline void CMD_LINK(struct etnaviv_cmdbuf *buffer, u16 prefetch,
			    u32 address)
{
	buffer->user_size = ALIGN(buffer->user_size, 8);

	OUT(buffer,
	    VIV_FE_LINK_HEADER_OP_LINK | VIV_FE_LINK_HEADER_PREFETCH(prefetch));
	OUT(buffer, address);
}

static inline void CMD_STALL(struct etnaviv_cmdbuf *buffer, u32 from, u32 to)
{
	buffer->user_size = ALIGN(buffer->user_size, 8);

	OUT(buffer, VIV_FE_STALL_HEADER_OP_STALL);
	OUT(buffer, VIV_FE_STALL_TOKEN_FROM(from) | VIV_FE_STALL_TOKEN_TO(to));
}

static inline void CMD_SEM(struct etnaviv_cmdbuf *buffer, u32 from, u32 to)
{
	CMD_LOAD_STATE(buffer, VIVS_GL_SEMAPHORE_TOKEN,
		       VIVS_GL_SEMAPHORE_TOKEN_FROM(from) |
			       VIVS_GL_SEMAPHORE_TOKEN_TO(to));
}

#endif /* __ETNAVIV_BUFFER_H__ */
