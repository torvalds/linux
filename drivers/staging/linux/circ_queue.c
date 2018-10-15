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
 * Filename: circ_queue.c
 * Version: 1.0
 * Description: A lock-free single-producer circular queue implementation 
 *   modeled after the more elaborate C++ version from Faustino Frechilla at:
 *   http://www.codeproject.com/Articles/153898/Yet-another-implementation-of-a-lock-free-circular
 * Author: Matthew Jacobsen
 * History: @mattj: Initial release. Version 1.0.
 */

#include <linux/slab.h>
#include "circ_queue.h"

circ_queue * init_circ_queue(int len)
{
	int i;
	circ_queue * q;

	q = kzalloc(sizeof(circ_queue), GFP_KERNEL);
	if (q == NULL) {
		printk(KERN_ERR "Not enough memory to allocate circ_queue");
		return NULL;
	}

	atomic_set(&q->writeIndex, 0);
	atomic_set(&q->readIndex, 0);
	q->len = len;

	q->vals = (unsigned int**) kzalloc(len*sizeof(unsigned int*), GFP_KERNEL);  
	if (q->vals == NULL) {
		printk(KERN_ERR "Not enough memory to allocate circ_queue array");
		return NULL;
	}
	for (i = 0; i < len; i++) {
		q->vals[i] = (unsigned int*) kzalloc(2*sizeof(unsigned int), GFP_KERNEL);
		if (q->vals[i] == NULL) {
			printk(KERN_ERR "Not enough memory to allocate circ_queue array position");
			return NULL;
		}
	}

	return q;
}

/**
 * Internal function to help count. Returns the queue size normalized position.
 */
unsigned int queue_count_to_index(unsigned int count, unsigned int len)
{
	return (count % len);
}

int push_circ_queue(circ_queue * q, unsigned int val1, unsigned int val2)
{
	unsigned int currReadIndex;
	unsigned int currWriteIndex;

	currWriteIndex = atomic_read(&q->writeIndex);
	currReadIndex  = atomic_read(&q->readIndex);
	if (queue_count_to_index(currWriteIndex+1, q->len) == queue_count_to_index(currReadIndex, q->len)) {
		// The queue is full
		return 1;
	}

	// Save the data into the queue
	q->vals[queue_count_to_index(currWriteIndex, q->len)][0] = val1;
	q->vals[queue_count_to_index(currWriteIndex, q->len)][1] = val2;
	// Increment atomically write index. Now a consumer thread can read
	// the piece of data that was just stored.
	atomic_inc(&q->writeIndex);

	return 0;
}

int pop_circ_queue(circ_queue * q, unsigned int * val1, unsigned int * val2)
{
	unsigned int currReadIndex;
	unsigned int currMaxReadIndex;

	do
	{
		currReadIndex = atomic_read(&q->readIndex);
		currMaxReadIndex = atomic_read(&q->writeIndex);
		if (queue_count_to_index(currReadIndex, q->len) == queue_count_to_index(currMaxReadIndex, q->len)) {
			// The queue is empty or a producer thread has allocate space in the queue
			// but is waiting to commit the data into it
			return 1;
		}

		// Retrieve the data from the queue
		*val1 = q->vals[queue_count_to_index(currReadIndex, q->len)][0];
		*val2 = q->vals[queue_count_to_index(currReadIndex, q->len)][1];

		// Try to perfrom now the CAS operation on the read index. If we succeed
		// label & val already contain what q->readIndex pointed to before we 
		// increased it.
		if (atomic_cmpxchg(&q->readIndex, currReadIndex, currReadIndex+1) == currReadIndex) {
			// The lable & val were retrieved from the queue. Note that the
			// data inside the label or value arrays are not deleted.
			return 0;
		}

		// Failed to retrieve the elements off the queue. Someone else must
		// have read the element stored at countToIndex(currReadIndex)
		// before we could perform the CAS operation.       
	} while(1); // keep looping to try again!

	return 1;
}

int circ_queue_empty(circ_queue * q)
{
	unsigned int currReadIndex;
	unsigned int currMaxReadIndex;

	currReadIndex = atomic_read(&q->readIndex);
	currMaxReadIndex = atomic_read(&q->writeIndex);
	if (queue_count_to_index(currReadIndex, q->len) == queue_count_to_index(currMaxReadIndex, q->len)) {
		// The queue is empty or a producer thread has allocate space in the queue
		// but is waiting to commit the data into it
		return 1;
	}
	return 0;
}

int circ_queue_full(circ_queue * q)
{
	unsigned int currReadIndex;
	unsigned int currWriteIndex;

	currWriteIndex = atomic_read(&q->writeIndex);
	currReadIndex  = atomic_read(&q->readIndex);
	if (queue_count_to_index(currWriteIndex+1, q->len) == queue_count_to_index(currReadIndex, q->len)) {
		// The queue is full
		return 1;
	}
	return 0;
}

void free_circ_queue(circ_queue * q)
{
	int i;

	if (q == NULL)
		return;

	for (i = 0; i < q->len; i++) {  
		kfree(q->vals[i]);  
	}
	kfree(q->vals);
	kfree(q);
}

