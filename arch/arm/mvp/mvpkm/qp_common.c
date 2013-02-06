/*
 * Linux 2.6.32 and later Kernel module for VMware MVP Hypervisor Support
 *
 * Copyright (C) 2010-2012 VMware, Inc. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; see the file COPYING.  If not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */
#line 5

/**
 *  @file
 *
 *  @brief MVP Queue Pairs common enqueue and dequeue functions.
 *  Does not include Attach(), and Detach(), as this will be specific
 *  to host/guest
 *  implementations.
 */

#include <linux/module.h>

#include "mvp_types.h"
#include "comm_os.h"
#include "qp.h"


/**
 *  @brief Calculate free space in the queue, convenience function
 *  @param head queue head offset
 *  @param tail  queue tail offset
 *  @param queueSize size of queue
 *  @return free space in the queue
 */
static inline int32
FreeSpace(uint32 head, uint32 tail, uint32 queueSize) {
   /* Leave 1 byte free to resolve ambiguity between empty
    * and full conditions */
   return (tail >= head) ? (queueSize - (tail - head) - 1) :
                           (head - tail - 1);
}


/**
 *  @brief Returns available space for enqueue, in bytes
 *  @param qp handle to the queue pair
 *  @return available space in bytes in the queue for enqueue operations,
 *      QP_ERROR_INVALID_HANDLE if the handle is malformed
 */
int32
QP_EnqueueSpace(QPHandle *qp)
{
   uint32 head;
   uint32 phantom;
   if (!QP_CheckHandle(qp)) {
      return QP_ERROR_INVALID_HANDLE;
   }

   head    = qp->produceQ->head;
   phantom = qp->produceQ->phantom_tail;

   if (head    >= qp->queueSize ||
       phantom >= qp->queueSize) {
      return QP_ERROR_INVALID_HANDLE;
   }

   return FreeSpace(head, phantom, qp->queueSize);
}


/**
 *  @brief Enqueues a segment of data into the producer queue
 *  @param qp handle to the queue pair
 *  @param buf data to enqueue
 *  @param bufSize size in bytes to enqueue
 *  @return number of bytes enqueued on success, appropriate error
 *      code otherwise
 *  @sideeffects May move phantom tail pointer
 */
int32
QP_EnqueueSegment(QPHandle *qp, const void *buf, size_t bufSize)
{
   int32 freeSpace;
   uint32 head;
   uint32 phantom;

   if (!QP_CheckHandle(qp)) {
      return QP_ERROR_INVALID_HANDLE;
   }

   head = qp->produceQ->head;
   phantom = qp->produceQ->phantom_tail;

   /*
    * This check must go after the assignment above,
    * otherwise a malicious guest could write bogus
    * offsets to the queue and cause the memcpy to
    * copy into unpleasant places.
    */
   if (head    >= qp->queueSize ||
       phantom >= qp->queueSize) {
      return QP_ERROR_INVALID_HANDLE;
   }

   freeSpace = FreeSpace(head, phantom, qp->queueSize);

   if (bufSize <= freeSpace) {
      if (bufSize + phantom < qp->queueSize) {
         memcpy(qp->produceQ->data + phantom, buf, bufSize);
         phantom += bufSize;
      } else {
         uint32 written = qp->queueSize - phantom;
         memcpy(qp->produceQ->data + phantom, buf, written);
         memcpy(qp->produceQ->data, (uint8*)buf + written, bufSize - written);
         phantom = bufSize - written;
      }
   } else {
      return QP_ERROR_NO_MEM;
   }

   qp->produceQ->phantom_tail = phantom;

   return bufSize;
}


/**
 *  @brief Commits any previous EnqueueSegment operations to the queue
 *         pair
 *  @param qp handle to the queue pair.
 *  @return QP_SUCCESS on success, appropriate error code otherwise.
 *  @sideeffects May move tail pointer
 */
int32
QP_EnqueueCommit(QPHandle *qp)
{
   uint32 phantom;
   if (!QP_CheckHandle(qp)) {
      return QP_ERROR_INVALID_HANDLE;
   }

   phantom = qp->produceQ->phantom_tail;
   if (phantom >= qp->queueSize) {
      return QP_ERROR_INVALID_HANDLE;
   }

   qp->produceQ->tail = phantom;
   return QP_SUCCESS;
}


/**
 *  @brief Returns any available bytes for dequeue
 *  @param qp handle to the queue pair
 *  @return available bytes for dequeue, appropriate error code
 *      otherwise
 */
int32
QP_DequeueSpace(QPHandle *qp)
{
   uint32 tail;
   uint32 phantom;
   int32 bytesAvailable;

   if (!QP_CheckHandle(qp)) {
      return QP_ERROR_INVALID_HANDLE;
   }

   tail    = qp->consumeQ->tail;
   phantom = qp->consumeQ->phantom_head;

   if (tail    >= qp->queueSize ||
       phantom >= qp->queueSize) {
      return QP_ERROR_INVALID_HANDLE;
   }

   bytesAvailable = (tail - phantom);
   if ((int32)bytesAvailable < 0) {
      bytesAvailable += qp->queueSize;
   }
   return bytesAvailable;
}


/**
 *  @brief Dequeues a segment of data from the consumer queue into
 *      a buffer
 *  @param qp handle to the queue pair
 *  @param[out] buf buffer to copy to
 *  @param bytesDesired number of bytes to dequeue
 *  @return number of bytes dequeued on success, appropriate error
 *      code otherwise
 *  @sideeffects May move phantom head pointer
 */
int32
QP_DequeueSegment(QPHandle *qp, const void *buf, size_t bytesDesired)
{
   uint32 tail;
   uint32 phantom;
   int32 bytesAvailable = 0;

   if (!QP_CheckHandle(qp)) {
      return QP_ERROR_INVALID_HANDLE;
   }

   tail = qp->consumeQ->tail;
   phantom = qp->consumeQ->phantom_head;

   /*
    * This check must go after the assignment above,
    * otherwise a malicious guest could write bogus
    * offsets to the queue and cause the memcpy to
    * copy into unpleasant places.
    */
   if (tail    >= qp->queueSize  ||
       phantom >= qp->queueSize) {
      return QP_ERROR_INVALID_HANDLE;
   }

   bytesAvailable = (tail - phantom);
   if ((int32)bytesAvailable < 0) {
      bytesAvailable += qp->queueSize;
   }

   if (bytesDesired <= bytesAvailable) {
      if (bytesDesired + phantom < qp->queueSize) {
         memcpy((void*)buf, qp->consumeQ->data + phantom, bytesDesired);
         phantom += bytesDesired;
      } else {
         uint32 written = qp->queueSize - phantom;
         memcpy((void*)buf, qp->consumeQ->data + phantom, written);
         memcpy((uint8*)buf + written, qp->consumeQ->data, bytesDesired - written);
         phantom = bytesDesired - written;
      }
   } else {
      return QP_ERROR_NO_MEM;
   }

   qp->consumeQ->phantom_head  = phantom;

   return bytesDesired;
}


/**
 *  @brief Commits any previous DequeueSegment operations to the queue
 *      pair
 *  @param qp handle to the queue pair
 *  @return QP_SUCCESS on success, QP_ERROR_INVALID_HANDLE if the handle
 *      is malformed
 *  @sideeffects Moves the head pointer
 */
int32
QP_DequeueCommit(QPHandle *qp)
{
   uint32 phantom;
   if (!QP_CheckHandle(qp)) {
      return QP_ERROR_INVALID_HANDLE;
   }

   phantom = qp->consumeQ->phantom_head;
   if (phantom >= qp->queueSize) {
      return QP_ERROR_INVALID_HANDLE;
   }

   qp->consumeQ->head = phantom;
   return QP_SUCCESS;
}


/**
 *  @brief Resets the phantom tail pointer and discards any pending
 *      enqueues
 *  @param qp handle to the queue pair
 *  @return QP_SUCCESS on success, QP_ERROR_INVALID_HANDLE if the handle
 *      is malformed
 *  @sideeffects Resets the phantom tail pointer
 */
int32
QP_EnqueueReset(QPHandle *qp)
{
   uint32 tail;
   if (!QP_CheckHandle(qp)) {
      return QP_ERROR_INVALID_HANDLE;
   }

   tail = qp->produceQ->tail;
   if (tail >= qp->queueSize) {
      return QP_ERROR_INVALID_HANDLE;
   }

   qp->produceQ->phantom_tail = tail;
   return QP_SUCCESS;
}

/**
 *  @brief Resets the phantom head pointer and discards any pending
 *      dequeues
 *  @param qp handle to the queue pair
 *  @return QP_SUCCESS on success, QP_ERROR_INVALID_HANDLE if the handle
 *      is malformed
 *  @sideeffects Resets the phantom head pointer
 */
int32
QP_DequeueReset(QPHandle *qp)
{
   uint32 head;
   if (!QP_CheckHandle(qp)) {
      return QP_ERROR_INVALID_HANDLE;
   }

   head = qp->consumeQ->head;
   if (head >= qp->queueSize) {
      return QP_ERROR_INVALID_HANDLE;
   }

   qp->consumeQ->phantom_head = head;
   return QP_SUCCESS;
}

EXPORT_SYMBOL(QP_EnqueueSpace);
EXPORT_SYMBOL(QP_EnqueueSegment);
EXPORT_SYMBOL(QP_EnqueueCommit);
EXPORT_SYMBOL(QP_DequeueSpace);
EXPORT_SYMBOL(QP_DequeueSegment);
EXPORT_SYMBOL(QP_DequeueCommit);
EXPORT_SYMBOL(QP_EnqueueReset);
EXPORT_SYMBOL(QP_DequeueReset);
