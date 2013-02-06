/*
 * Linux 2.6.32 and later Kernel module for VMware MVP Guest Communications
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
 * @file
 *
 * @brief MVP Queue Pairs function and structure declarations
 *
 * MVP Queue Pairs:
 *
 * Queue pairs are intended to be a generic bulk data transport mechanism
 * between the guest and host kernels. The queue pair abstraction is based
 * on two ring buffers (queues) placed on a shared memory region mapped
 * into both guest and host kernel address spaces.
 *
 * NOTE: Queue pairs are SINGLE-READER, SINGLE-WRITER. Any caller is
 * responsible for multi-reader/writer serialization!!!
 *
 * There are a maximum of QP_MAX_QUEUE_PAIRS in the system, with a maximum
 * size of QP_MAX_CAPACITY per pair. Each queue pair is identified by
 * an ID.
 *
 * Each peer follows a producer-consumer model in which one side is the
 * producer on one queue, and the other side is the consumer on that queue
 * (and vice-versa for its pair).
 *
 * Data is enqueued and dequeued into the pair in transactional stages,
 * meaning each enqueue/dequeue can be followed by zero or more
 * enqueue/dequeues, but the enqueue/dequeue is not visible to the peer
 * until it has been committed with the *Commit() function.
 * In PVTCP, for example, this is used to enqueue a short header, then
 * followed by 'segments' of iovecs, then followed by a commit. This
 * model prevents a peer from reading the header, expecting a payload,
 * but not being able to read the payload because it hasn't been
 * enqueued yet.
 *
 * Queue Pair setup:
 *
 * Before data can be passed, the guest and host kernel must perform
 * the following connection handshake:
 *
 * 1). A host kernel service registers a listener with the queue pair
 *     subsystem with a callback to be called when guests create
 *     and attach to a shared memory region.
 *
 * 2). Guest initiates an QP_Attach() operation to a shared memory region
 *     keyed by ID. This step allocates memory, maps it into the host
 *     address space, and optionally notifies any host services who are
 *     listening for attach requests from the guest (see previous step).
 *     Host listeners are provided with a copy of the initialization
 *     arguments used by the guest (id, size, service type). All registered
 *     listeners are iterated over until one of them handles the attach
 *     request and acknowledges with QP_SUCCESS.
 *
 * 3). The registered host callback is called, notifying the host that
 *     the guest has attached.
 *
 * 4). The host can now QP_Attach() to the shared memory region with the same
 *     arguments as the guest. The queue pair is now well formed and enqueues
 *     and dequeues can proceed on either side.
 *
 * Queue Pair teardown:
 *
 * 1). As before, teardowns are initiated by the guest. Hosts can register
 *     a callback to be called upon detach. Guests initiate a teardown
 *     through a call to QP_Detach().
 *
 * 2). Registered hosts are notified through the aforementioned callback.
 * 3). The host service can call QP_Detach() at its own leisure. Memory
 *     is freed, the queue pair is destroyed.
 *
 * If at any point the guest unexpectedly shuts down, the host will be
 * notified at monitor shutdown time. Memory is freed, and the queue
 * pair is destroyed.
 *
 */

#ifndef _QP_H
#define _QP_H

#define INCLUDE_ALLOW_MODULE
#define INCLUDE_ALLOW_MONITOR
#define INCLUDE_ALLOW_PV
#define INCLUDE_ALLOW_GPL
#include "include_check.h"

//#define QP_DEBUG 1

typedef enum QPState {
   QP_STATE_FREE = 0x1,          ///< No peers, not memory-backed
   QP_STATE_CONNECTED,           ///< Both peers attached , memory backed
   QP_STATE_GUEST_ATTACHED,      ///< Guest allocated memory, host not yet attached
   QP_STATE_MAX                  // leave this at the end!
} QPState;

typedef struct QPId {
   uint32 context;
   uint32 resource;
} QPId;

/*
 * Initialization arguments for each queue pair
 */
typedef struct QPInitArgs {
   QPId id;                  ///< Shared memory region ID
   uint32 capacity;          ///< Total size of shared region in bytes
   uint32 type;              ///< Type of queue pair (PVTCP, other)...
} QPInitArgs;

/*
 * Placed on the shared region, two per region
 */
typedef struct QHandle {
   volatile uint32 head;                    ///< queue head offset
   volatile uint32 tail;                    ///< queue tail offset
   volatile uint32 phantom_head;            ///< queue shadow head offset
   volatile uint32 phantom_tail;            ///< queue shadow tail offset
   uint8 data[0];                           ///< start of data, runs off
                                            //        the struct
} QHandle;

/*
 * Local to each peer
 */
typedef struct QPHandle {
   QPId          id;                        ///< shared memory region ID
   uint32        capacity;                  ///< size of region in bytes
   QHandle      *produceQ;                  ///< producer queue
   QHandle      *consumeQ;                  ///< consumer queue
   uint32        queueSize;                 ///< size of each queue in bytes
   uint32        type;                      ///< type of queue pair

   /*
    * Following fields unused by guest
    */
   QPState       state;
   void        (*peerDetachCB)(void* data); ///< detach notification callback
   void         *detachData;                ///< data for the detach cb
   struct page  **pages;                    ///< page pointers for shared region
} QPHandle;

/*
 * QP Error codes
 */
#define QP_SUCCESS                    0
#define QP_ERROR_NO_MEM             (-1)
#define QP_ERROR_INVALID_HANDLE     (-2)
#define QP_ERROR_INVALID_ARGS       (-3)
#define QP_ERROR_ALREADY_ATTACHED   (-4)

/*
 * Hard-coded limits
 */
#define QP_MIN_CAPACITY            (PAGE_SIZE * 2)
#define QP_MAX_CAPACITY            (1024*1024)                 // 1M
#define QP_MAX_QUEUE_PAIRS         32
#define QP_MAX_ID                  QP_MAX_QUEUE_PAIRS
#define QP_MAX_LISTENERS           QP_MAX_QUEUE_PAIRS
#define QP_MAX_PAGES               (QP_MAX_CAPACITY/PAGE_SIZE) // 256 pages

#define QP_INVALID_ID              0xFFFFFFFF
#define QP_INVALID_SIZE            0xFFFFFFFF
#define QP_INVALID_REGION          0xFFFFFFFF
#define QP_INVALID_TYPE            0xFFFFFFFF

#ifdef __KERNEL__
/**
 *  @brief Utility function to sanity check arguments
 *  @param args argument structure to check
 *  @return true if arguments are sane, false otherwise
 */
static inline
_Bool QP_CheckArgs(QPInitArgs *args)
{
   if (!args                                                                  ||
       !is_power_of_2(args->capacity)                                         ||
        (args->capacity < QP_MIN_CAPACITY)                                    ||
        (args->capacity > QP_MAX_CAPACITY)                                    ||
       !(args->id.resource < QP_MAX_ID || args->id.resource == QP_INVALID_ID) ||
        (args->type == QP_INVALID_TYPE)) {
      return false;
   } else {
      return true;
   }
}
#endif


/**
 *  @brief Utility function to sanity check a queue pair handle
 *  @param qp handle to the queue pair
 *  @return true if the handle is sane, false otherwise
 */
static inline
_Bool QP_CheckHandle(QPHandle *qp)
{
#ifdef MVP_DEBUG
   if (!(qp)                                            ||
       !(qp->produceQ)                                  ||
       !(qp->consumeQ)                                  ||
        (qp->state >= (uint32)QP_STATE_MAX)             ||
       !(qp->queueSize < (QP_MAX_CAPACITY/2))) {
      return false;
   } else {
      return true;
   }
#else
   return true;
#endif
}


/**
 *  @brief Initializes an invalid handle
 *  @param[in, out] qp handle to the queue pair
 */
static inline void
QP_MakeInvalidQPHandle(QPHandle *qp)
{
   if (!qp) {
      return;
   }

   qp->id.context       = QP_INVALID_ID;
   qp->id.resource      = QP_INVALID_ID;
   qp->capacity         = QP_INVALID_SIZE;
   qp->produceQ         = NULL;
   qp->consumeQ         = NULL;
   qp->queueSize        = QP_INVALID_SIZE;
   qp->type             = QP_INVALID_TYPE;
   qp->state            = QP_STATE_FREE;
   qp->peerDetachCB     = NULL;
   qp->detachData       = NULL;
}

/*
 * Host only
 */
typedef int32 (*QPListener)(const QPInitArgs*);
int32 QP_RegisterListener(const QPListener);
int32 QP_UnregisterListener(const QPListener);
int32 QP_RegisterDetachCB(QPHandle *qp, void (*callback)(void*), void *data);


/*
 * Host and guest specific implementations, see qp_host.c and qp_guest.c
 */
int32 QP_Attach(QPInitArgs *args, QPHandle** qp);
int32 QP_Detach(QPHandle* qp);
int32 QP_Notify(QPInitArgs *args);

/*
 * Common implementation, see qp_common.c
 */
int32 QP_EnqueueSpace(QPHandle *qp);
int32 QP_EnqueueSegment(QPHandle *qp, const void *buf, size_t length);
int32 QP_EnqueueCommit(QPHandle *qp);
int32 QP_EnqueueReset(QPHandle *qp);

static inline int32
QP_EnqueueAtomic(QPHandle *qp, const void *buf, size_t length)
{
   int32 rc;
   QP_EnqueueReset(qp);
   rc = QP_EnqueueSegment(qp, buf, length);
   if (rc < 0) {
      return rc;
   } else {
      QP_EnqueueCommit(qp);
   }
   return rc;
}

int32 QP_DequeueSpace(QPHandle *qp);
int32 QP_DequeueSegment(QPHandle *qp, const void *buf, size_t length);
int32 QP_DequeueReset(QPHandle *qp);
int32 QP_DequeueCommit(QPHandle *qp);

static inline int32
QP_DequeueAtomic(QPHandle *qp, const void *buf, size_t length)
{
   int32 rc;
   QP_DequeueReset(qp);
   rc = QP_DequeueSegment(qp, buf, length);
   if (rc < 0) {
      return rc;
   } else {
      QP_DequeueCommit(qp);
   }
   return rc;
}

/*
 * HVC methods and signatures
 */
#define MVP_QP_SIGNATURE       0x53525051                   ///< 'QPRS'
#define MVP_QP_ATTACH          (MVP_OBJECT_CUSTOM_BASE + 0) ///< attach to a queue pair
#define MVP_QP_DETACH          (MVP_OBJECT_CUSTOM_BASE + 1) ///< detach from a queue pair
#define MVP_QP_NOTIFY          (MVP_OBJECT_CUSTOM_BASE + 2) ///< notify host of attach
#define MVP_QP_LAST            (MVP_OBJECT_CUSTOM_BASE + 3) ///< Number of methods

/*
 * Debug macros
 */
#ifdef QP_DEBUG
   #ifdef IN_MONITOR
      #define QP_DBG(...) Log(__VA_ARGS__)
   #else
      #define QP_DBG(...) printk(KERN_INFO __VA_ARGS__)
   #endif
#else
   #define QP_DBG(...)
#endif

#endif
