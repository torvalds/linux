/* Copyright © 2010 - 2013 UNISYS CORPORATION
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
SignalInsert(pCHANNEL_HEADER pChannel, U32 Queue, void *pSignal)
{
	void *psignal;
	unsigned int head, tail;
	pSIGNAL_QUEUE_HEADER pqhdr =
	    (pSIGNAL_QUEUE_HEADER) ((char *) pChannel +
				    pChannel->oChannelSpace) + Queue;

	/* capture current head and tail */
	head = pqhdr->Head;
	tail = pqhdr->Tail;

	/* queue is full if (head + 1) % n equals tail */
	if (((head + 1) % pqhdr->MaxSignalSlots) == tail) {
		pqhdr->NumOverflows++;
		return 0;
	}

	/* increment the head index */
	head = (head + 1) % pqhdr->MaxSignalSlots;

	/* copy signal to the head location from the area pointed to
	 * by pSignal
	 */
	psignal =
	    (char *) pqhdr + pqhdr->oSignalBase + (head * pqhdr->SignalSize);
	MEMCPY(psignal, pSignal, pqhdr->SignalSize);

	VolatileBarrier();
	pqhdr->Head = head;

	pqhdr->NumSignalsSent++;
	return 1;
}
EXPORT_SYMBOL_GPL(SignalInsert);

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
SignalRemove(pCHANNEL_HEADER pChannel, U32 Queue, void *pSignal)
{
	void *psource;
	unsigned int head, tail;
	pSIGNAL_QUEUE_HEADER pqhdr =
	    (pSIGNAL_QUEUE_HEADER) ((char *) pChannel +
				    pChannel->oChannelSpace) + Queue;

	/* capture current head and tail */
	head = pqhdr->Head;
	tail = pqhdr->Tail;

	/* queue is empty if the head index equals the tail index */
	if (head == tail) {
		pqhdr->NumEmptyCnt++;
		return 0;
	}

	/* advance past the 'empty' front slot */
	tail = (tail + 1) % pqhdr->MaxSignalSlots;

	/* copy signal from tail location to the area pointed to by pSignal */
	psource =
	    (char *) pqhdr + pqhdr->oSignalBase + (tail * pqhdr->SignalSize);
	MEMCPY(pSignal, psource, pqhdr->SignalSize);

	VolatileBarrier();
	pqhdr->Tail = tail;

	pqhdr->NumSignalsReceived++;
	return 1;
}
EXPORT_SYMBOL_GPL(SignalRemove);

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
 * Copies one signal from channel pChannel's nth Queue at the given position
 * at the time of the call into the memory pointed to by pSignal.
 *
 * Parameters:
 * pChannel: (IN) points to the IO Channel
 * Queue: (IN) nth Queue of the IO Channel
 * Position: (IN) nth entry in Queue of the IO Channel
 * pSignal: (IN) pointer to where the signals are to be copied
 *
 * Assumptions:
 * - pChannel and Queue are valid.
 * - pSignal points to a memory area large enough to hold queue's SignalSize
 *
 * Return value:
 * 1 if the copy succeeds, 0 if the queue was empty or Position was invalid.
 */
unsigned char
SignalPeek(pCHANNEL_HEADER pChannel, U32 Queue, U32 Position, void *pSignal)
{
	void *psignal;
	unsigned int head, tail;
	pSIGNAL_QUEUE_HEADER pqhdr =
	    (pSIGNAL_QUEUE_HEADER) ((char *) pChannel +
				    pChannel->oChannelSpace) + Queue;

	head = pqhdr->Head;
	tail = pqhdr->Tail;

	/* check if Position is out of range or queue is empty */
	if (Position >= pqhdr->MaxSignalSlots || Position == tail
	    || head == tail)
		return 0;

	/* check if Position is between tail and head */
	if (head > tail) {
		if (Position > head || Position < tail)
			return 0;
	} else if ((Position > head) && (Position < tail))
		return 0;

	/* copy signal from Position location to the area pointed to
	 * by pSignal
	 */
	psignal =
	    (char *) pqhdr + pqhdr->oSignalBase +
	    (Position * pqhdr->SignalSize);
	MEMCPY(pSignal, psignal, pqhdr->SignalSize);

	return 1;
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
SignalQueueIsEmpty(pCHANNEL_HEADER pChannel, U32 Queue)
{
	pSIGNAL_QUEUE_HEADER pqhdr =
	    (pSIGNAL_QUEUE_HEADER) ((char *) pChannel +
				    pChannel->oChannelSpace) + Queue;
	return pqhdr->Head == pqhdr->Tail;
}
EXPORT_SYMBOL_GPL(SignalQueueIsEmpty);

/*
 * Routine Description:
 * Determine whether a signal queue is empty.
 *
 * Parameters:
 * pChannel: (IN) points to the IO Channel
 * Queue: (IN) nth Queue of the IO Channel
 *
 * Return value:
 * 1 if the signal queue has 1 element, 0 otherwise.
 */
unsigned char
SignalQueueHasOneElement(pCHANNEL_HEADER pChannel, U32 Queue)
{
	pSIGNAL_QUEUE_HEADER pqhdr =
	    (pSIGNAL_QUEUE_HEADER) ((char *) pChannel +
				    pChannel->oChannelSpace) + Queue;
	return ((pqhdr->Tail + 1) % pqhdr->MaxSignalSlots) == pqhdr->Head;
}

/*
 * Routine Description:
 * Determine whether a signal queue is full.
 *
 * Parameters:
 * pChannel: (IN) points to the IO Channel
 * Queue: (IN) nth Queue of the IO Channel
 *
 * Return value:
 * 1 if the signal queue is full, 0 otherwise.
 */
unsigned char
SignalQueueIsFull(pCHANNEL_HEADER pChannel, U32 Queue)
{
	pSIGNAL_QUEUE_HEADER pqhdr =
	    (pSIGNAL_QUEUE_HEADER) ((char *) pChannel +
				    pChannel->oChannelSpace) + Queue;
	return ((pqhdr->Head + 1) % pqhdr->MaxSignalSlots) == pqhdr->Tail;
}
