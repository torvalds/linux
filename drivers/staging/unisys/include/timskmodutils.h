/* timskmodutils.h
 *
 * Copyright (C) 2010 - 2013 UNISYS CORPORATION
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 */

#ifndef __TIMSKMODUTILS_H__
#define __TIMSKMODUTILS_H__

#include "timskmod.h"

void *kmalloc_kernel(size_t siz);
void  myprintk(const char *myDrvName, const char *devname,
		const char *template, ...);

/*--------------------------------*
 *---  GENERAL MESSAGEQ STUFF  ---*
 *--------------------------------*/

struct MessageQEntry;

/** the data structure used to hold an arbitrary data item that you want
 *  to place on a #MESSAGEQ.  Declare and initialize as follows:
 *
 *  This structure should be considered opaque; the client using it should
 *  never access the fields directly.
 *  Refer to these functions for more info:
 *
 *  @ingroup messageq
 */
typedef struct MessageQEntry {
	void *data;
	struct MessageQEntry *qNext;
	struct MessageQEntry *qPrev;
} MESSAGEQENTRY;

/** the data structure used to hold a FIFO queue of #MESSAGEQENTRY<b></b>s.
 *  Declare and initialize as follows:
 *  @code
 *      MESSAGEQ myQueue;
 *  @endcode
 *  This structure should be considered opaque; the client using it should
 *  never access the fields directly.
 *  Refer to these functions for more info:
 *
 *  @ingroup messageq
 */
typedef struct MessageQ {
	MESSAGEQENTRY *qHead;
	MESSAGEQENTRY *qTail;
	struct semaphore nQEntries;
	spinlock_t       queueLock;
} MESSAGEQ;

char *cyclesToSeconds(u64 cycles, u64 cyclesPerSecond,
		      char *buf, size_t bufsize);
char *cyclesToIterationSeconds(u64 cycles, u64 cyclesPerSecond,
			       u64 iterations, char *buf, size_t bufsize);
char *cyclesToSomethingsPerSecond(u64 cycles, u64 cyclesPerSecond,
				  u64 somethings, char *buf, size_t bufsize);
struct seq_file *visor_seq_file_new_buffer(void *buf, size_t buf_size);
void visor_seq_file_done_buffer(struct seq_file *m);

extern int unisys_spar_platform;

#endif
