/**
 * Copyright (c) 2010-2012 Broadcom. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The names of the above-listed copyright holders may not be used
 *    to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * ALTERNATIVELY, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2, as published by the Free
 * Software Foundation.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
 * IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef VCHI_CFG_INTERNAL_H_
#define VCHI_CFG_INTERNAL_H_

/****************************************************************************************
 * Control optimisation attempts.
 ***************************************************************************************/

// Don't use lots of short-term locks - use great long ones, reducing the overall locks-per-second
#define VCHI_COARSE_LOCKING

// Avoid lock then unlock on exit from blocking queue operations (msg tx, bulk rx/tx)
// (only relevant if VCHI_COARSE_LOCKING)
#define VCHI_ELIDE_BLOCK_EXIT_LOCK

// Avoid lock on non-blocking peek
// (only relevant if VCHI_COARSE_LOCKING)
#define VCHI_AVOID_PEEK_LOCK

// Use one slot-handler thread per connection, rather than 1 thread dealing with all connections in rotation.
#define VCHI_MULTIPLE_HANDLER_THREADS

// Put free descriptors onto the head of the free queue, rather than the tail, so that we don't thrash
// our way through the pool of descriptors.
#define VCHI_PUSH_FREE_DESCRIPTORS_ONTO_HEAD

// Don't issue a MSG_AVAILABLE callback for every single message. Possibly only safe if VCHI_COARSE_LOCKING.
#define VCHI_FEWER_MSG_AVAILABLE_CALLBACKS

// Don't use message descriptors for TX messages that don't need them
#define VCHI_MINIMISE_TX_MSG_DESCRIPTORS

// Nano-locks for multiqueue
//#define VCHI_MQUEUE_NANOLOCKS

// Lock-free(er) dequeuing
//#define VCHI_RX_NANOLOCKS

#endif /*VCHI_CFG_INTERNAL_H_*/
