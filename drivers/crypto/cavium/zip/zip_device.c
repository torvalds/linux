/***********************license start************************************
 * Copyright (c) 2003-2017 Cavium, Inc.
 * All rights reserved.
 *
 * License: one of 'Cavium License' or 'GNU General Public License Version 2'
 *
 * This file is provided under the terms of the Cavium License (see below)
 * or under the terms of GNU General Public License, Version 2, as
 * published by the Free Software Foundation. When using or redistributing
 * this file, you may do so under either license.
 *
 * Cavium License:  Redistribution and use in source and binary forms, with
 * or without modification, are permitted provided that the following
 * conditions are met:
 *
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 *  * Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials provided
 *    with the distribution.
 *
 *  * Neither the name of Cavium Inc. nor the names of its contributors may be
 *    used to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * This Software, including technical data, may be subject to U.S. export
 * control laws, including the U.S. Export Administration Act and its
 * associated regulations, and may be subject to export or import
 * regulations in other countries.
 *
 * TO THE MAXIMUM EXTENT PERMITTED BY LAW, THE SOFTWARE IS PROVIDED "AS IS"
 * AND WITH ALL FAULTS AND CAVIUM INC. MAKES NO PROMISES, REPRESENTATIONS
 * OR WARRANTIES, EITHER EXPRESS, IMPLIED, STATUTORY, OR OTHERWISE, WITH
 * RESPECT TO THE SOFTWARE, INCLUDING ITS CONDITION, ITS CONFORMITY TO ANY
 * REPRESENTATION OR DESCRIPTION, OR THE EXISTENCE OF ANY LATENT OR PATENT
 * DEFECTS, AND CAVIUM SPECIFICALLY DISCLAIMS ALL IMPLIED (IF ANY)
 * WARRANTIES OF TITLE, MERCHANTABILITY, NONINFRINGEMENT, FITNESS FOR A
 * PARTICULAR PURPOSE, LACK OF VIRUSES, ACCURACY OR COMPLETENESS, QUIET
 * ENJOYMENT, QUIET POSSESSION OR CORRESPONDENCE TO DESCRIPTION. THE
 * ENTIRE  RISK ARISING OUT OF USE OR PERFORMANCE OF THE SOFTWARE LIES
 * WITH YOU.
 ***********************license end**************************************/

#include "common.h"
#include "zip_deflate.h"

/**
 * zip_cmd_queue_consumed - Calculates the space consumed in the command queue.
 *
 * @zip_dev: Pointer to zip device structure
 * @queue:   Queue number
 *
 * Return: Bytes consumed in the command queue buffer.
 */
static inline u32 zip_cmd_queue_consumed(struct zip_device *zip_dev, int queue)
{
	return ((zip_dev->iq[queue].sw_head - zip_dev->iq[queue].sw_tail) *
		sizeof(u64 *));
}

/**
 * zip_load_instr - Submits the instruction into the ZIP command queue
 * @instr:      Pointer to the instruction to be submitted
 * @zip_dev:    Pointer to ZIP device structure to which the instruction is to
 *              be submitted
 *
 * This function copies the ZIP instruction to the command queue and rings the
 * doorbell to notify the engine of the instruction submission. The command
 * queue is maintained in a circular fashion. When there is space for exactly
 * one instruction in the queue, next chunk pointer of the queue is made to
 * point to the head of the queue, thus maintaining a circular queue.
 *
 * Return: Queue number to which the instruction was submitted
 */
u32 zip_load_instr(union zip_inst_s *instr,
		   struct zip_device *zip_dev)
{
	union zip_quex_doorbell dbell;
	u32 queue = 0;
	u32 consumed = 0;
	u64 *ncb_ptr = NULL;
	union zip_nptr_s ncp;

	/*
	 * Distribute the instructions between the enabled queues based on
	 * the CPU id.
	 */
	if (raw_smp_processor_id() % 2 == 0)
		queue = 0;
	else
		queue = 1;

	zip_dbg("CPU Core: %d Queue number:%d", raw_smp_processor_id(), queue);

	/* Take cmd buffer lock */
	spin_lock(&zip_dev->iq[queue].lock);

	/*
	 * Command Queue implementation
	 * 1. If there is place for new instructions, push the cmd at sw_head.
	 * 2. If there is place for exactly one instruction, push the new cmd
	 *    at the sw_head. Make sw_head point to the sw_tail to make it
	 *    circular. Write sw_head's physical address to the "Next-Chunk
	 *    Buffer Ptr" to make it cmd_hw_tail.
	 * 3. Ring the door bell.
	 */
	zip_dbg("sw_head : %lx", zip_dev->iq[queue].sw_head);
	zip_dbg("sw_tail : %lx", zip_dev->iq[queue].sw_tail);

	consumed = zip_cmd_queue_consumed(zip_dev, queue);
	/* Check if there is space to push just one cmd */
	if ((consumed + 128) == (ZIP_CMD_QBUF_SIZE - 8)) {
		zip_dbg("Cmd queue space available for single command");
		/* Space for one cmd, pust it and make it circular queue */
		memcpy((u8 *)zip_dev->iq[queue].sw_head, (u8 *)instr,
		       sizeof(union zip_inst_s));
		zip_dev->iq[queue].sw_head += 16; /* 16 64_bit words = 128B */

		/* Now, point the "Next-Chunk Buffer Ptr" to sw_head */
		ncb_ptr = zip_dev->iq[queue].sw_head;

		zip_dbg("ncb addr :0x%lx sw_head addr :0x%lx",
			ncb_ptr, zip_dev->iq[queue].sw_head - 16);

		/* Using Circular command queue */
		zip_dev->iq[queue].sw_head = zip_dev->iq[queue].sw_tail;
		/* Mark this buffer for free */
		zip_dev->iq[queue].free_flag = 1;

		/* Write new chunk buffer address at "Next-Chunk Buffer Ptr" */
		ncp.u_reg64 = 0ull;
		ncp.s.addr = __pa(zip_dev->iq[queue].sw_head);
		*ncb_ptr = ncp.u_reg64;
		zip_dbg("*ncb_ptr :0x%lx sw_head[phys] :0x%lx",
			*ncb_ptr, __pa(zip_dev->iq[queue].sw_head));

		zip_dev->iq[queue].pend_cnt++;

	} else {
		zip_dbg("Enough space is available for commands");
		/* Push this cmd to cmd queue buffer */
		memcpy((u8 *)zip_dev->iq[queue].sw_head, (u8 *)instr,
		       sizeof(union zip_inst_s));
		zip_dev->iq[queue].sw_head += 16; /* 16 64_bit words = 128B */

		zip_dev->iq[queue].pend_cnt++;
	}
	zip_dbg("sw_head :0x%lx sw_tail :0x%lx hw_tail :0x%lx",
		zip_dev->iq[queue].sw_head, zip_dev->iq[queue].sw_tail,
		zip_dev->iq[queue].hw_tail);

	zip_dbg(" Pushed the new cmd : pend_cnt : %d",
		zip_dev->iq[queue].pend_cnt);

	/* Ring the doorbell */
	dbell.u_reg64     = 0ull;
	dbell.s.dbell_cnt = 1;
	zip_reg_write(dbell.u_reg64,
		      (zip_dev->reg_base + ZIP_QUEX_DOORBELL(queue)));

	/* Unlock cmd buffer lock */
	spin_unlock(&zip_dev->iq[queue].lock);

	return queue;
}

/**
 * zip_update_cmd_bufs - Updates the queue statistics after posting the
 *                       instruction
 * @zip_dev: Pointer to zip device structure
 * @queue:   Queue number
 */
void zip_update_cmd_bufs(struct zip_device *zip_dev, u32 queue)
{
	/* Take cmd buffer lock */
	spin_lock(&zip_dev->iq[queue].lock);

	/* Check if the previous buffer can be freed */
	if (zip_dev->iq[queue].free_flag == 1) {
		zip_dbg("Free flag. Free cmd buffer, adjust sw head and tail");
		/* Reset the free flag */
		zip_dev->iq[queue].free_flag = 0;

		/* Point the hw_tail to start of the new chunk buffer */
		zip_dev->iq[queue].hw_tail = zip_dev->iq[queue].sw_head;
	} else {
		zip_dbg("Free flag not set. increment hw tail");
		zip_dev->iq[queue].hw_tail += 16; /* 16 64_bit words = 128B */
	}

	zip_dev->iq[queue].done_cnt++;
	zip_dev->iq[queue].pend_cnt--;

	zip_dbg("sw_head :0x%lx sw_tail :0x%lx hw_tail :0x%lx",
		zip_dev->iq[queue].sw_head, zip_dev->iq[queue].sw_tail,
		zip_dev->iq[queue].hw_tail);
	zip_dbg(" Got CC : pend_cnt : %d\n", zip_dev->iq[queue].pend_cnt);

	spin_unlock(&zip_dev->iq[queue].lock);
}
