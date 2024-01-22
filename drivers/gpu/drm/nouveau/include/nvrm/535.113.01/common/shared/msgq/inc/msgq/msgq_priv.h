#ifndef __src_common_shared_msgq_inc_msgq_msgq_priv_h__
#define __src_common_shared_msgq_inc_msgq_msgq_priv_h__

/* Excerpt of RM headers from https://github.com/NVIDIA/open-gpu-kernel-modules/tree/535.113.01 */

/*
 * SPDX-FileCopyrightText: Copyright (c) 2018-2019 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: MIT
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

/**
 * msgqTxHeader -- TX queue data structure
 * @version: the version of this structure, must be 0
 * @size: the size of the entire queue, including this header
 * @msgSize: the padded size of queue element, 16 is minimum
 * @msgCount: the number of elements in this queue
 * @writePtr: head index of this queue
 * @flags: 1 = swap the RX pointers
 * @rxHdrOff: offset of readPtr in this structure
 * @entryOff: offset of beginning of queue (msgqRxHeader), relative to
 *          beginning of this structure
 *
 * The command queue is a queue of RPCs that are sent from the driver to the
 * GSP.  The status queue is a queue of messages/responses from GSP-RM to the
 * driver.  Although the driver allocates memory for both queues, the command
 * queue is owned by the driver and the status queue is owned by GSP-RM.  In
 * addition, the headers of the two queues must not share the same 4K page.
 *
 * Each queue is prefixed with this data structure.  The idea is that a queue
 * and its header are written to only by their owner.  That is, only the
 * driver writes to the command queue and command queue header, and only the
 * GSP writes to the status (receive) queue and its header.
 *
 * This is enforced by the concept of "swapping" the RX pointers.  This is
 * why the 'flags' field must be set to 1.  'rxHdrOff' is how the GSP knows
 * where the where the tail pointer of its status queue.
 *
 * When the driver writes a new RPC to the command queue, it updates writePtr.
 * When it reads a new message from the status queue, it updates readPtr.  In
 * this way, the GSP knows when a new command is in the queue (it polls
 * writePtr) and it knows how much free space is in the status queue (it
 * checks readPtr).  The driver never cares about how much free space is in
 * the status queue.
 *
 * As usual, producers write to the head pointer, and consumers read from the
 * tail pointer.  When head == tail, the queue is empty.
 *
 * So to summarize:
 * command.writePtr = head of command queue
 * command.readPtr = tail of status queue
 * status.writePtr = head of status queue
 * status.readPtr = tail of command queue
 */
typedef struct
{
    NvU32 version;   // queue version
    NvU32 size;      // bytes, page aligned
    NvU32 msgSize;   // entry size, bytes, must be power-of-2, 16 is minimum
    NvU32 msgCount;  // number of entries in queue
    NvU32 writePtr;  // message id of next slot
    NvU32 flags;     // if set it means "i want to swap RX"
    NvU32 rxHdrOff;  // Offset of msgqRxHeader from start of backing store.
    NvU32 entryOff;  // Offset of entries from start of backing store.
} msgqTxHeader;

/**
 * msgqRxHeader - RX queue data structure
 * @readPtr: tail index of the other queue
 *
 * Although this is a separate struct, it could easily be merged into
 * msgqTxHeader.  msgqTxHeader.rxHdrOff is simply the offset of readPtr
 * from the beginning of msgqTxHeader.
 */
typedef struct
{
    NvU32 readPtr; // message id of last message read
} msgqRxHeader;

#endif
