/* Copyright (C) 2010 - 2013 UNISYS CORPORATION
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

#include <linux/kernel.h>
#ifdef CONFIG_MODVERSIONS
#include <config/modversions.h>
#endif
#include <linux/module.h>
#include <linux/init.h>		/* for module_init and module_exit */
#include <linux/slab.h>		/* for memcpy */
#include <linux/types.h>

/* Implementation of exported functions for Supervisor channels */
#include "channel.h"

/*
 * Routine Description:
 * Tries to insert the prebuilt signal pointed to by pSignal into the nth
 * Queue of the Channel pointed to by pChannel
 *
 * Parameters:
 * pChannel: (IN) points to the IO Channel
 * Queue: (IN) nth Queue of the IO Channel
 * pSignal: (IN) pointer to the signal
 *
 * Assumptions:
 * - pChannel, Queue and pSignal are valid.
 * - If insertion fails due to a full queue, the caller will determine the
 * retry policy (e.g. wait & try again, report an error, etc.).
 *
 * Return value:
 * 1 if the insertion succeeds, 0 if the queue was full.
 */
unsigned char
visor_signal_insert(CHANNEL_HEADER __iomem *pChannel, U32 Queue, void *pSignal)
{
	void __iomem *psignal;
	unsigned int head, tail, nof;

	SIGNAL_QUEUE_HEADER __iomem *pqhdr =
	    (SIGNAL_QUEUE_HEADER __iomem *)
		((char __iomem *) pChannel + readq(&pChannel->oChannelSpace))
		+ Queue;

	/* capture current head and tail */
	head = readl(&pqhdr->Head);
	tail = readl(&pqhdr->Tail);

	/* queue is full if (head + 1) % n equals tail */
	if (((head + 1) % readl(&pqhdr->MaxSignalSlots)) == tail) {
		nof = readq(&pqhdr->NumOverflows) + 1;
		writeq(nof, &pqhdr->NumOverflows);
		return 0;
	}

	/* increment the head index */
	head = (head + 1) % readl(&pqhdr->MaxSignalSlots);

	/* copy signal to the head location from the area pointed to
	 * by pSignal
	 */
	psignal = (char __iomem *)pqhdr + readq(&pqhdr->oSignalBase) +
		(head * readl(&pqhdr->SignalSize));
	MEMCPY_TOIO(psignal, pSignal, readl(&pqhdr->SignalSize));

	VolatileBarrier();
	writel(head, &pqhdr->Head);

	writeq(readq(&pqhdr->NumSignalsSent) + 1, &pqhdr->NumSignalsSent);
	return 1;
}
EXPORT_SYMBOL_GPL(visor_signal_insert);

/*
 * Routine Description:
 * Removes one signal from Channel pChannel's nth Queue at the
 * time of the call and copies it into the memory pointed to by
 * pSignal.
 *
 * Parameters:
 * pChannel: (IN) points to the IO Channel
 * Queue: (IN) nth Queue of the IO Channel
 * pSignal: (IN) pointer to where the signals are to be copied
 *
 * Assumptions:
 * - pChannel and Queue are valid.
 * - pSignal points to a memory area large enough to hold queue's SignalSize
 *
 * Return value:
 * 1 if the removal succeeds, 0 if the queue was empty.
 */
unsigned char
visor_signal_remove(CHANNEL_HEADER __iomem *pChannel, U32 Queue, void *pSignal)
{
	void __iomem *psource;
	unsigned int head, tail;
	SIGNAL_QUEUE_HEADER __iomem *pqhdr =
	    (SIGNAL_QUEUE_HEADER __iomem *) ((char __iomem *) pChannel +
				    readq(&pChannel->oChannelSpace)) + Queue;

	/* capture current head and tail */
	head = readl(&pqhdr->Head);
	tail = readl(&pqhdr->Tail);

	/* queue is empty if the head index equals the tail index */
	if (head == tail) {
		writeq(readq(&pqhdr->NumEmptyCnt) + 1, &pqhdr->NumEmptyCnt);
		return 0;
	}

	/* advance past the 'empty' front slot */
	tail = (tail + 1) % readl(&pqhdr->MaxSignalSlots);

	/* copy signal from tail location to the area pointed to by pSignal */
	psource = (char __iomem *) pqhdr + readq(&pqhdr->oSignalBase) +
		(tail * readl(&pqhdr->SignalSize));
	MEMCPY_FROMIO(pSignal, psource, readl(&pqhdr->SignalSize));

	VolatileBarrier();
	writel(tail, &pqhdr->Tail);

	writeq(readq(&pqhdr->NumSignalsReceived) + 1,
	       &pqhdr->NumSignalsReceived);
	return 1;
}
EXPORT_SYMBOL_GPL(visor_signal_remove);

/*
 * Routine Description:
 * Removes all signals present in Channel pChannel's nth Queue at the
 * time of the call and copies them into the memory pointed to by
 * pSignal.  Returns the # of signals copied as the value of the routine.
 *
 * Parameters:
 * pChannel: (IN) points to the IO Channel
 * Queue: (IN) nth Queue of the IO Channel
 * pSignal: (IN) pointer to where the signals are to be copied
 *
 * Assumptions:
 * - pChannel and Queue are valid.
 * - pSignal points to a memory area large enough to hold Queue's MaxSignals
 * # of signals, each of which is Queue's SignalSize.
 *
 * Return value:
 * # of signals copied.
 */
unsigned int
SignalRemoveAll(pCHANNEL_HEADER pChannel, U32 Queue, void *pSignal)
{
	void *psource;
	unsigned int head, tail, signalCount = 0;
	pSIGNAL_QUEUE_HEADER pqhdr =
	    (pSIGNAL_QUEUE_HEADER) ((char *) pChannel +
				    pChannel->oChannelSpace) + Queue;

	/* capture current head and tail */
	head = pqhdr->Head;
	tail = pqhdr->Tail;

	/* queue is empty if the head index equals the tail index */
	if (head == tail)
		return 0;

	while (head != tail) {
		/* advance past the 'empty' front slot */
		tail = (tail + 1) % pqhdr->MaxSignalSlots;

		/* copy signal from tail location to the area pointed
		 * to by pSignal
		 */
		psource =
		    (char *) pqhdr + pqhdr->oSignalBase +
		    (tail * pqhdr->SignalSize);
		MEMCPY((char *) pSignal + (pqhdr->SignalSize * signalCount),
		       psource, pqhdr->SignalSize);

		VolatileBarrier();
		pqhdr->Tail = tail;

		signalCount++;
		pqhdr->NumSignalsReceived++;
	}

	return signalCount;
}

/*
 * Routine Description:
 * Determine whether a signal queue is empty.
 *
 * Parameters:
 * pChannel: (IN) points to the IO Channel
 * Queue: (IN) nth Queue of the IO Channel
 *
 * Return value:
 * 1 if the signal queue is empty, 0 otherwise.
 */
unsigned char
visor_signalqueue_empty(CHANNEL_HEADER __iomem *pChannel, U32 Queue)
{
	SIGNAL_QUEUE_HEADER __iomem *pqhdr =
	    (SIGNAL_QUEUE_HEADER __iomem *) ((char __iomem *) pChannel +
				    readq(&pChannel->oChannelSpace)) + Queue;
	return readl(&pqhdr->Head) == readl(&pqhdr->Tail);
}
EXPORT_SYMBOL_GPL(visor_signalqueue_empty);

