/* timskmodutils.h
 *
 * Copyright © 2010 - 2013 UNISYS CORPORATION
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
void *kmalloc_kernel_dma(size_t siz);
void  kfree_kernel(const void *p, size_t siz);
void *vmalloc_kernel(size_t siz);
void  vfree_kernel(const void *p, size_t siz);
void *pgalloc_kernel(size_t siz);
void  pgfree_kernel(const void *p, size_t siz);
void  myprintk(const char *myDrvName, const char *devname,
		const char *template, ...);
void  myprintkx(const char *myDrvName, int devno, const char *template, ...);

/** Print the hexadecimal contents of a data buffer to a supplied print buffer.
 *  @param dest               the print buffer where text characters will be
 *                            written
 *  @param destSize           the maximum number of bytes that can be written
 *                            to #dest
 *  @param src                the buffer that contains the data that is to be
 *                            hex-dumped
 *  @param srcLen             the number of bytes at #src to be hex-dumped
 *  @param bytesToDumpPerLine output will be formatted such that at most this
 *                            many of the input data bytes will be represented
 *                            on each line of output
 *  @return                   the number of text characters written to #dest
 *                            (not including the trailing '\0' byte)
 *  @ingroup internal
 */
int   hexDumpToBuffer(char *dest,
		      int destSize,
		      char *prefix,
		      char *src,
		      int srcLen,
		      int bytesToDumpPerLine);

/** Print the hexadecimal contents of a data buffer to a supplied print buffer.
 *  Assume the data buffer contains 32-bit words in little-endian format,
 *  and dump the words with MSB first and LSB last.
 *  @param dest               the print buffer where text characters will be
 *                            written
 *  @param destSize           the maximum number of bytes that can be written
 *                            to #dest
 *  @param src                the buffer that contains the data that is to be
 *                            hex-dumped
 *  @param srcWords           the number of 32-bit words at #src to be
 &                            hex-dumped
 *  @param wordsToDumpPerLine output will be formatted such that at most this
 *                            many of the input data words will be represented
 *                            on each line of output
 *  @return                   the number of text characters written to #dest
 *                            (not including the trailing '\0' byte)
 *  @ingroup internal
 */
int   hexDumpWordsToBuffer(char *dest,
			   int destSize,
			   char *prefix,
			   uint32_t *src,
			   int srcWords,
			   int wordsToDumpPerLine);


/** Use printk to print the hexadecimal contents of a data buffer.
 *  See #INFOHEXDRV and #INFOHEXDEV for info.
 *  @ingroup internal
 */
int myPrintkHexDump(char *myDrvName,
		    char *devname,
		    char *prefix,
		    char *src,
		    int srcLen,
		    int bytesToDumpPerLine);

/** Given as input a number of seconds in #seconds, creates text describing
 *  the time within #s.  Also breaks down the number of seconds into component
 *  days, hours, minutes, and seconds, and stores to *#days, *#hours,
 *  *#minutes, and *#secondsx.
 *  @param seconds input number of seconds
 *  @param days    points to a long value where the days component for the
 *                 days+hours+minutes+seconds representation of #seconds will
 *                 be stored
 *  @param hours   points to a long value where the hours component for the
 *                 days+hours+minutes+seconds representation of #seconds will
 *                 be stored
 *  @param minutes points to a long value where the minutes component for the
 *                 days+hours+minutes+seconds representation of #seconds will
 *                 be stored
 *  @param secondsx points to a long value where the seconds component for the
 *                 days+hours+minutes+seconds representation of #seconds will
 *                 be stored
 *  @param s       points to a character buffer where a text representation of
 *                 the #seconds value will be stored.  This buffer MUST be
 *                 large enough to hold the resulting string; to be safe it
 *                 should be at least 100 bytes long.
 */
void  expandSeconds(time_t seconds,
		    long *days, long *hours,
		    long *minutes,
		    long *secondsx,
		    char *s);

/*--------------------------------*
 *---  GENERAL MESSAGEQ STUFF  ---*
 *--------------------------------*/

struct MessageQEntry;

/** the data structure used to hold an arbitrary data item that you want
 *  to place on a #MESSAGEQ.  Declare and initialize as follows:
 *  @code
 *      MESSAGEQENTRY myEntry;
 *      initMessageQEntry (&myEntry, pointerToMyDataItem);
 *  @endcode
 *  This structure should be considered opaque; the client using it should
 *  never access the fields directly.
 *  Refer to these functions for more info:
 *  - initMessageQ()
 *  - initMessageQEntry()
 *  - enqueueMessage()
 *  - dequeueMessage()
 *  - dequeueMessageNoBlock()
 *  - getQueueCount()
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
 *      initMessageQ (&myQueue);
 *  @endcode
 *  This structure should be considered opaque; the client using it should
 *  never access the fields directly.
 *  Refer to these functions for more info:
 *  - initMessageQ()
 *  - initMessageQEntry()
 *  - enqueueMessage()
 *  - dequeueMessage()
 *  - dequeueMessageNoBlock()
 *  - getQueueCount()
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
void initMessageQ(MESSAGEQ *q);
void initMessageQEntry(MESSAGEQENTRY *p, void *data);
MESSAGEQENTRY *dequeueMessage(MESSAGEQ *q);
MESSAGEQENTRY *dequeueMessageNoBlock(MESSAGEQ *q);
void enqueueMessage(MESSAGEQ *q, MESSAGEQENTRY *pEntry);
size_t getQueueCount(MESSAGEQ *q);
int waitQueueLen(wait_queue_head_t *q);
void debugWaitQ(wait_queue_head_t *q);
struct seq_file *seq_file_new_buffer(void *buf, size_t buf_size);
void seq_file_done_buffer(struct seq_file *m);
void seq_hexdump(struct seq_file *seq, u8 *pfx, void *buf, ulong nbytes);

#endif
