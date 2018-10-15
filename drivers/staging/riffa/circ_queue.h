// ----------------------------------------------------------------------
// Copyright (c) 2016, The Regents of the University of California All
// rights reserved.
// 
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
// 
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
// 
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided
//       with the distribution.
// 
//     * Neither the name of The Regents of the University of California
//       nor the names of its contributors may be used to endorse or
//       promote products derived from this software without specific
//       prior written permission.
// 
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL REGENTS OF THE
// UNIVERSITY OF CALIFORNIA BE LIABLE FOR ANY DIRECT, INDIRECT,
// INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
// BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
// OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
// ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
// TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
// USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
// DAMAGE.
// ----------------------------------------------------------------------

/*
 * Filename: circ_queue.h
 * Version: 1.0
 * Description: A lock-free single-producer circular queue implementation 
 *   modeled after the more elaborate C++ version from Faustino Frechilla at:
 *   http://www.codeproject.com/Articles/153898/Yet-another-implementation-of-a-lock-free-circular
 * Author: Matthew Jacobsen
 * History: @mattj: Initial release. Version 1.0.
 */
#ifndef CIRC_QUEUE_H
#define CIRC_QUEUE_H

#include <asm/atomic.h>

/* Struct for the circular queue. */
struct circ_queue {
	atomic_t writeIndex;
	atomic_t readIndex;
	unsigned int ** vals;
	unsigned int len;
};
typedef struct circ_queue circ_queue;

/**
 * Initializes a circ_queue with depth/length len. Returns non-NULL on success, 
 * NULL if there was a problem creating the queue.
 */
circ_queue * init_circ_queue(int len);

/**
 * Pushes a pair of unsigned int values into the specified queue at the head. 
 * Returns 0 on success, non-zero if there is no more space in the queue.
 */
int push_circ_queue(circ_queue * q, unsigned int val1, unsigned int val2);

/**
 * Pops a pair of unsigned int values out of the specified queue from the tail.
 * Returns 0 on success, non-zero if the queue is empty.
 */
int pop_circ_queue(circ_queue * q, unsigned int * val1, unsigned int * val2);

/**
 * Returns 1 if the circ_queue is empty, 0 otherwise. Note, this is not a 
 * synchronized function. If another thread is accessing this circ_queue, the
 * return value may not be valid.
 */
int circ_queue_empty(circ_queue * q);

/**
 * Returns 1 if the circ_queue is full, 0 otherwise. Note, this is not a 
 * synchronized function. If another thread is accessing this circ_queue, the
 * return value may not be valid.
 */
int circ_queue_full(circ_queue * q);

/**
 * Frees the resources associated with the specified circ_queue.
 */
void free_circ_queue(circ_queue * q);

#endif
