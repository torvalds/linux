/* timskmodutils.c
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

#include "uniklog.h"
#include "timskmod.h"

#define MYDRVNAME "timskmodutils"

BOOL Debug_Malloc_Enabled = FALSE;



void myprintk(const char *myDrvName, const char *devname,
	       const char *template, ...)
{
	va_list ap;
	char temp[999];
	char *ptemp = temp;
	char pfx[20];
	char msg[sizeof(pfx) + strlen(myDrvName) + 50];

	if (myDrvName == NULL)
		return;
	temp[sizeof(temp)-1] = '\0';
	pfx[0]               = '\0';
	msg[0]               = '\0';
	va_start(ap, template);
	vsprintf(temp, template, ap);
	va_end(ap);
	if (temp[0] == '<') {
		size_t i = 0;
		for (i = 0; i < sizeof(pfx) - 1; i++) {
			pfx[i] = temp[i];
			if (pfx[i] == '>' || pfx[i] == '\0') {
				if (pfx[i] == '>')
					ptemp = temp+i+1;
				i++;
				break;
			}
		}
		pfx[i] = '\0';
	}
	if (devname == NULL)
		sprintf(msg, "%s%s: ", pfx, myDrvName);
	else
		sprintf(msg, "%s%s[%s]: ", pfx, myDrvName, devname);
	printk(KERN_INFO "%s", msg);

	/* The <prefix> applies up until the \n, so we should not include
	 * it in these printks.  That's why we use <ptemp> to point to the
	 * first char after the ">" in the prefix.
	 */
	printk(KERN_INFO "%s", ptemp);
	printk("\n");

}



void myprintkx(const char *myDrvName, int devno, const char *template, ...)
{
	va_list ap;
	char temp[999];
	char *ptemp = temp;
	char pfx[20];
	char msg[sizeof(pfx) + strlen(myDrvName) + 50];

	if (myDrvName == NULL)
		return;
	temp[sizeof(temp)-1] = '\0';
	pfx[0]               = '\0';
	msg[0]               = '\0';
	va_start(ap, template);
	vsprintf(temp, template, ap);
	va_end(ap);
	if (temp[0] == '<') {
		size_t i = 0;
		for (i = 0; i < sizeof(pfx) - 1; i++) {
			pfx[i] = temp[i];
			if (pfx[i] == '>' || pfx[i] == '\0') {
				if (pfx[i] == '>')
					ptemp = temp+i+1;
				i++;
				break;
			}
		}
		pfx[i] = '\0';
	}
	if (devno < 0)
		sprintf(msg, "%s%s: ", pfx, myDrvName);
	else
		sprintf(msg, "%s%s[%d]: ", pfx, myDrvName, devno);
	printk(KERN_INFO "%s", msg);

	/* The <prefix> applies up until the \n, so we should not include
	 * it in these printks.  That's why we use <ptemp> to point to the
	 * first char after the ">" in the prefix.
	 */
	printk(KERN_INFO "%s", ptemp);
	printk("\n");
}



int hexDumpWordsToBuffer(char *dest,
			 int destSize,
			 char *prefix,
			 uint32_t *src,
			 int srcWords,
			 int wordsToDumpPerLine)
{
	int i = 0;
	int pos = 0;
	char hex[(wordsToDumpPerLine * 9) + 1];
	char *line = NULL;
	int linesize = 1000;
	int linelen = 0;
	int currentlen = 0;
	char emptystring[] = "";
	char *pfx = prefix;
	int baseaddr = 0;
	int rc = 0;
	uint8_t b1, b2, b3, b4;

	line = vmalloc(linesize);
	if (line == NULL)
		RETINT(currentlen);

	if (pfx == NULL || (strlen(pfx) > 50))
		pfx = emptystring;
	memset(hex, ' ', wordsToDumpPerLine * 9);
	hex[wordsToDumpPerLine * 9] = '\0';
	if (destSize > 0)
		dest[0] = '\0';

	for (i = 0; i < srcWords; i++) {
		pos = i % wordsToDumpPerLine;
		if ((pos == 0) && (i > 0)) {
			hex[wordsToDumpPerLine * 9] = '\0';
			linelen = sprintf(line, "%s%-6.6x %s\n", pfx,
					  baseaddr, hex);
			if ((currentlen) + (linelen) >= destSize)
				RETINT(currentlen);
			strcat(dest, line);
			currentlen += linelen;
			memset(hex, ' ', wordsToDumpPerLine * 9);
			baseaddr = i * 4;
		}
		b1 = (uint8_t)((src[i] >> 24) & 0xff);
		b2 = (uint8_t)((src[i] >> 16) & 0xff);
		b3 = (uint8_t)((src[i] >>  8) & 0xff);
		b4 = (uint8_t)((src[i]) & 0xff);
		sprintf(hex + (pos * 9), "%-2.2x%-2.2x%-2.2x%-2.2x ",
			b1, b2, b3, b4);
		*(hex + (pos * 9) + 9) = ' ';  /* get rid of null */
	}
	pos = i%wordsToDumpPerLine;
	if (i > 0) {
		hex[wordsToDumpPerLine * 9] = '\0';
		linelen = sprintf(line, "%s%-6.6x %s\n", pfx, baseaddr, hex);
		if ((currentlen) + (linelen) >= destSize)
			RETINT(currentlen);
		strcat(dest, line);
		currentlen += linelen;
	}
	RETINT(currentlen);

Away:
	if (line)
		vfree(line);
	return rc;
}
EXPORT_SYMBOL_GPL(hexDumpWordsToBuffer);



int myPrintkHexDump(char *myDrvName,
		    char *devname,
		    char *prefix,
		    char *src,
		    int srcLen,
		    int bytesToDumpPerLine)
{
	int i = 0;
	int pos = 0;
	char printable[bytesToDumpPerLine + 1];
	char hex[(bytesToDumpPerLine*3) + 1];
	char *line = NULL;
	int linesize = 1000;
	int linelen = 0;
	int currentlen = 0;
	char emptystring[] = "";
	char *pfx = prefix;
	int baseaddr = 0;
	int rc = 0;
	int linecount = 0;

	line = vmalloc(linesize);
	if (line == NULL)
		RETINT(currentlen);

	if (pfx == NULL || (strlen(pfx) > 50))
		pfx = emptystring;
	memset(hex, ' ', bytesToDumpPerLine * 3);
	hex[bytesToDumpPerLine * 3] = '\0';
	memset(printable, ' ', bytesToDumpPerLine);
	printable[bytesToDumpPerLine] = '\0';

	for (i = 0; i < srcLen; i++) {
		pos = i % bytesToDumpPerLine;
		if ((pos == 0) && (i > 0)) {
			hex[bytesToDumpPerLine*3] = '\0';
			linelen = sprintf(line, "%s%-6.6x %s %s",
					  pfx, baseaddr, hex, printable);
			myprintk(myDrvName, devname, KERN_INFO "%s", line);
			currentlen += linelen;
			linecount++;
			if ((linecount % 50) == 0)
				SLEEPJIFFIES(10);
			memset(hex, ' ', bytesToDumpPerLine*3);
			memset(printable, ' ', bytesToDumpPerLine);
			baseaddr = i;
		}
		sprintf(hex + (pos * 3), "%-2.2x ", (uint8_t)(src[i]));
		*(hex + (pos * 3) + 3) = ' ';  /* get rid of null */
		if (((uint8_t)(src[i]) >= ' ') && (uint8_t)(src[i]) < 127)
			printable[pos] = src[i];
		else
			printable[pos] = '.';
	}
	pos = i%bytesToDumpPerLine;
	if (i > 0) {
		hex[bytesToDumpPerLine*3] = '\0';
		linelen = sprintf(line, "%s%-6.6x %s %s",
				  pfx, baseaddr, hex, printable);
		myprintk(myDrvName, devname, KERN_INFO "%s", line);
		currentlen += linelen;
	}
	RETINT(currentlen);

Away:
	if (line)
		vfree(line);
	return rc;
}



/** Given as input a number of seconds in #seconds, creates text describing
    the time within #s.  Also breaks down the number of seconds into component
    days, hours, minutes, and seconds, and stores to *#days, *#hours,
    *#minutes, and *#secondsx.
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
 *		   the #seconds value will be stored.  This buffer MUST be
 *		   large enough to hold the resulting string; to be safe it
 *		   should be at least 100 bytes long.
 */
void expandSeconds(time_t seconds, long *days, long *hours, long *minutes,
		   long *secondsx, char *s)
{
	BOOL started = FALSE;
	char buf[99];

	*days = seconds / (60*60*24);
	seconds -= ((*days)*(60*60*24));
	*hours = seconds / (60*60);
	seconds -= ((*hours)*(60*60));
	*minutes = seconds/60;
	seconds -= ((*minutes)*60);
	*secondsx = (long)seconds;
	if (s == NULL)
		RETVOID;
	s[0] = '\0';
	if (*days > 0) {
		sprintf(buf, "%lu day", *days);
		strcat(s, buf);
		if (*days != 1)
			strcat(s, "s");
		started = TRUE;
	}
	if ((*hours > 0) || started) {
		if (started)
			strcat(s, ", ");
		sprintf(buf, "%lu hour", *hours);
		strcat(s, buf);
		if (*hours != 1)
			strcat(s, "s");
		started = TRUE;
	}
	if ((*minutes > 0) || started) {
		if (started)
			strcat(s, ", ");
		sprintf(buf, "%lu minute", *minutes);
		strcat(s, buf);
		if (*minutes != 1)
			strcat(s, "s");
		started = TRUE;
	}
	if (started)
		strcat(s, ", ");
	sprintf(buf, "%lu second", *secondsx);
	strcat(s, buf);
	if (*secondsx != 1)
		strcat(s, "s");

Away:
	return;
}



/** Initialize a #MESSAGEQ for use (initially it will be empty, of course).
 *  @param q               the #MESSAGEQ to initialize
 *  @ingroup messageq
 */
void initMessageQ(MESSAGEQ *q)
{
	q->qHead = NULL;
	q->qTail = NULL;
	sema_init(&q->nQEntries, 0);   /* will block initially */
	spin_lock_init(&q->queueLock);
}



/** Initialize #p with your data structure in #data,
 *  so you can later place #p onto a #MESSAGEQ.
 *  @param p               the queue entry that will house your data structure
 *  @param data            a pointer to your data structure that you want
 *                         to queue
 *  @ingroup messageq
 */
void initMessageQEntry(MESSAGEQENTRY *p, void *data)
{
	p->data = data;
	p->qNext = NULL;
	p->qPrev = NULL;
}



MESSAGEQENTRY *dequeueMessageGuts(MESSAGEQ *q, BOOL canBlock)
{
	MESSAGEQENTRY *pEntry = NULL;
	MESSAGEQENTRY *rc = NULL;
	BOOL locked = FALSE;
	ulong flags = 0;
	int res = 0;

	if (canBlock) {
		/* wait for non-empty q */
		res = down_interruptible(&q->nQEntries);
		if (signal_pending(current)) {
			DEBUGDRV("got signal in dequeueMessage");
			RETPTR(NULL);
		}
	} else if (down_trylock(&q->nQEntries))
		RETPTR(NULL);
	spin_lock_irqsave(&q->queueLock, flags);
	locked = TRUE;
#ifdef PARANOID
	if (q->qHead == NULL) {
		HUHDRV("unexpected empty queue in getQueue");
		RETPTR(NULL);
	}
#endif
	pEntry = q->qHead;
	if (pEntry == q->qTail) {
		/* only 1 item in the queue */
		q->qHead = NULL;
		q->qTail = NULL;
	} else {
		q->qHead = pEntry->qNext;
		q->qHead->qPrev = NULL;
	}
	RETPTR(pEntry);
Away:
	if (locked) {
		spin_unlock_irqrestore(&q->queueLock, flags);
		locked = FALSE;
	}
	return rc;
}



/** Remove the next message at the head of the FIFO queue, and return it.
 *  Wait for the queue to become non-empty if it is empty when this
 *  function is called.
 *  @param q               the queue where the message is to be obtained from
 *  @return                the queue entry obtained from the head of the
 *                         FIFO queue, or NULL iff a signal was received
 *                         while waiting for the queue to become non-empty
 *  @ingroup messageq
 */
MESSAGEQENTRY *dequeueMessage(MESSAGEQ *q)
{
	return dequeueMessageGuts(q, TRUE);
}



/** Remove the next message at the head of the FIFO queue, and return it.
 *  This function will never block (it returns NULL instead).
 *  @param q               the queue where the message is to be obtained from
 *  @return                the queue entry obtained from the head of the
 *                         FIFO queue, or NULL iff the queue is empty.
 *  @ingroup messageq
 */
MESSAGEQENTRY *dequeueMessageNoBlock(MESSAGEQ *q)
{
	return dequeueMessageGuts(q, FALSE);
}



/** Add an entry to a FIFO queue.
 *  @param q               the queue where the entry is to be added
 *  @param pEntry          the entry you want to add to the queue
 *  @ingroup messageq
 */
void enqueueMessage(MESSAGEQ *q, MESSAGEQENTRY *pEntry)
{
	BOOL locked = FALSE;
	ulong flags = 0;

	spin_lock_irqsave(&q->queueLock, flags);
	locked = TRUE;
	if (q->qHead == NULL) {
#ifdef PARANOID
		if (q->qTail != NULL) {
			HUHDRV("qHead/qTail not consistent");
			RETVOID;
		}
#endif
		q->qHead = pEntry;
		q->qTail = pEntry;
		pEntry->qNext = NULL;
		pEntry->qPrev = NULL;
	} else {
#ifdef PARANOID
		if (q->qTail == NULL) {
			HUHDRV("qTail should not be NULL here");
			RETVOID;
		}
#endif
		q->qTail->qNext = pEntry;
		pEntry->qPrev = q->qTail;
		pEntry->qNext = NULL;
		q->qTail = pEntry;
	}
	spin_unlock_irqrestore(&q->queueLock, flags);
	locked = FALSE;
	up(&q->nQEntries);
	RETVOID;
Away:
	if (locked) {
		spin_unlock_irqrestore(&q->queueLock, flags);
		locked = FALSE;
	}
	return;
}



/** Return the number of entries in the queue.
 *  @param q               the queue to be examined
 *  @return                the number of entries on #q
 *  @ingroup messageq
 */
size_t getQueueCount(MESSAGEQ *q)
{
	return (size_t)__sync_fetch_and_add(&(q->nQEntries.count), 0);
}



/** Return the number of processes waiting in a standard wait queue.
 *  @param q               the pointer to the wait queue to be
 *                         examined
 *  @return                the number of waiters
 *  @ingroup internal
 */
int waitQueueLen(wait_queue_head_t *q)
{
	struct list_head *x;
	int count = 0;
	list_for_each(x, &(q->task_list))
		count++;
	return count;
}



/** Display information about the processes on a standard wait queue.
 *  @param q               the pointer to the wait queue to be
 *                         examined
 *  @ingroup internal
 */
void debugWaitQ(wait_queue_head_t *q)
{
	DEBUGDRV("task_list.next= %-8.8x",
		 ((struct __wait_queue_head *)(q))->task_list.next);
	DEBUGDRV("task_list.prev= %-8.8x",
		 ((struct __wait_queue_head *)(q))->task_list.prev);
}



/** Print the hexadecimal contents of a data buffer to a supplied print buffer.
 *  @param dest               the print buffer where text characters will
 *			      be written
 *  @param destSize           the maximum number of bytes that can be written
 *			      to #dest
 *  @param src                the buffer that contains the data that is to be
 *			      hex-dumped
 *  @param srcLen             the number of bytes at #src to be hex-dumped
 *  @param bytesToDumpPerLine output will be formatted such that at most
 *			      this many of the input data bytes will be
 *			      represented on each line of output
 *  @return                   the number of text characters written to #dest
 *                            (not including the trailing '\0' byte)
 *  @ingroup internal
 */
int hexDumpToBuffer(char *dest, int destSize, char *prefix, char *src,
		    int srcLen, int bytesToDumpPerLine)
{
	int i = 0;
	int pos = 0;
	char printable[bytesToDumpPerLine + 1];
	char hex[(bytesToDumpPerLine * 3) + 1];
	char *line = NULL;
	int linesize = 1000;
	int linelen = 0;
	int currentlen = 0;
	char emptystring[] = "";
	char *pfx = prefix;
	int baseaddr = 0;
	int rc = 0;

	line = vmalloc(linesize);
	if (line == NULL)
		RETINT(currentlen);

	if (pfx == NULL || (strlen(pfx) > 50))
		pfx = emptystring;
	memset(hex, ' ', bytesToDumpPerLine * 3);
	hex[bytesToDumpPerLine * 3] = '\0';
	memset(printable, ' ', bytesToDumpPerLine);
	printable[bytesToDumpPerLine] = '\0';
	if (destSize > 0)
		dest[0] = '\0';

	for (i = 0; i < srcLen; i++) {
		pos = i % bytesToDumpPerLine;
		if ((pos == 0) && (i > 0)) {
			hex[bytesToDumpPerLine*3] = '\0';
			linelen = sprintf(line, "%s%-6.6x %s %s\n", pfx,
					  baseaddr, hex, printable);
			if ((currentlen) + (linelen) >= destSize)
				RETINT(currentlen);
			strcat(dest, line);
			currentlen += linelen;
			memset(hex, ' ', bytesToDumpPerLine * 3);
			memset(printable, ' ', bytesToDumpPerLine);
			baseaddr = i;
		}
		sprintf(hex + (pos * 3), "%-2.2x ", (uint8_t)(src[i]));
		*(hex + (pos * 3) + 3) = ' ';  /* get rid of null */
		if (((uint8_t)(src[i]) >= ' ') && (uint8_t)(src[i]) < 127)
			printable[pos] = src[i];
		else
			printable[pos] = '.';
	}
	pos = i%bytesToDumpPerLine;
	if (i > 0) {
		hex[bytesToDumpPerLine * 3] = '\0';
		linelen = sprintf(line, "%s%-6.6x %s %s\n",
				  pfx, baseaddr, hex, printable);
		if ((currentlen) + (linelen) >= destSize)
			RETINT(currentlen);
		strcat(dest, line);
		currentlen += linelen;
	}
	RETINT(currentlen);

Away:
	if (line)
		vfree(line);
	return rc;
}
EXPORT_SYMBOL_GPL(hexDumpToBuffer);


/** Callers to interfaces that set __GFP_NORETRY flag below
 *  must check for a NULL (error) result as we are telling the
 *  kernel interface that it is okay to fail.
 */

void *kmalloc_kernel(size_t siz)
{
	return kmalloc(siz, GFP_KERNEL | __GFP_NORETRY);
}

void *kmalloc_kernel_dma(size_t siz)
{
	return kmalloc(siz, GFP_KERNEL | __GFP_NORETRY|GFP_DMA);
}

void kfree_kernel(const void *p, size_t siz)
{
	kfree(p);
}

void *vmalloc_kernel(size_t siz)
{
	return vmalloc((unsigned long)(siz));
}

void vfree_kernel(const void *p, size_t siz)
{
	vfree((void *)(p));
}

void *pgalloc_kernel(size_t siz)
{
	return (void *)__get_free_pages(GFP_KERNEL|__GFP_NORETRY,
				       get_order(siz));
}

void pgfree_kernel(const void *p, size_t siz)
{
	free_pages((ulong)(p), get_order(siz));
}



/*  Use these handy-dandy seq_file_xxx functions if you want to call some
 *  functions that write stuff into a seq_file, but you actually just want
 *  to dump that output into a buffer.  Use them as follows:
 *  - call seq_file_new_buffer to create the seq_file (you supply the buf)
 *  - call whatever functions you want that take a seq_file as an argument
 *    (the buf you supplied will get the output data)
 *  - call seq_file_done_buffer to dispose of your seq_file
 */
struct seq_file *seq_file_new_buffer(void *buf, size_t buf_size)
{
	struct seq_file *rc = NULL;
	struct seq_file *m = kmalloc_kernel(sizeof(struct seq_file));

	if (m == NULL)
		RETPTR(NULL);
	memset(m, 0, sizeof(struct seq_file));
	m->buf = buf;
	m->size = buf_size;
	RETPTR(m);
Away:
	if (rc == NULL) {
		seq_file_done_buffer(m);
		m = NULL;
	}
	return rc;
}
EXPORT_SYMBOL_GPL(seq_file_new_buffer);



void seq_file_done_buffer(struct seq_file *m)
{
	if (!m)
		return;
	kfree(m);
}
EXPORT_SYMBOL_GPL(seq_file_done_buffer);



void seq_hexdump(struct seq_file *seq, u8 *pfx, void *buf, ulong nbytes)
{
	int fmtbufsize = 100 * COVQ(nbytes, 16);
	char *fmtbuf = NULL;
	int i = 0;
	if (buf == NULL) {
		seq_printf(seq, "%s<NULL>\n", pfx);
		return;
	}
	fmtbuf = kmalloc_kernel(fmtbufsize);
	if (fmtbuf == NULL)
		return;
	hexDumpToBuffer(fmtbuf, fmtbufsize, pfx, (char *)(buf), nbytes, 16);
	for (i = 0; fmtbuf[i] != '\0'; i++)
		seq_printf(seq, "%c", fmtbuf[i]);
	kfree(fmtbuf);
}
