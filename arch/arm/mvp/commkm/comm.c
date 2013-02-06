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
 * @brief Communication functions based on transport functionality.
 */

#include "comm.h"
#include "comm_transp_impl.h"


/* Constant and macro definitions */

#if defined(COMM_INSTRUMENT)
static CommOSAtomic commMaxCoalesceSize;
static CommOSAtomic commPacketsReceived;
static CommOSAtomic commCommittedPacketsReceived;
static CommOSAtomic commOpCalls;
#endif

#define COMM_DISPATCH_EXTRA_WRITER_WAKEUP 1

#define COMM_CHANNEL_MAX_CAPACITY 2048
#define COMM_CHANNEL_FREE        0x0
#define COMM_CHANNEL_INITIALIZED 0x1
#define COMM_CHANNEL_OPENED      0x2
#define COMM_CHANNEL_ACTIVE      0x4
#define COMM_CHANNEL_ZOMBIE      0x8

#define CommIsFree(chan)        \
   ((chan)->lifecycleState == COMM_CHANNEL_FREE)
#define CommIsInitialized(chan) \
   ((chan)->lifecycleState == COMM_CHANNEL_INITIALIZED)
#define CommIsOpened(chan)      \
   ((chan)->lifecycleState == COMM_CHANNEL_OPENED)
#define CommIsActive(chan)      \
   ((chan)->lifecycleState == COMM_CHANNEL_ACTIVE)
#define CommIsZombie(chan)      \
   ((chan)->lifecycleState == COMM_CHANNEL_ZOMBIE)

#define CommSetFree(chan)        \
   SetLifecycleState(chan, COMM_CHANNEL_FREE)
#define CommSetInitialized(chan) \
   SetLifecycleState(chan, COMM_CHANNEL_INITIALIZED)
#define CommSetOpened(chan)      \
   SetLifecycleState(chan, COMM_CHANNEL_OPENED)
#define CommSetActive(chan)      \
   SetLifecycleState(chan, COMM_CHANNEL_ACTIVE)
#define CommSetZombie(chan)      \
   SetLifecycleState(chan, COMM_CHANNEL_ZOMBIE)

#define CommGlobalLock() CommOS_SpinLock(&commGlobalLock)
#define CommGlobalUnlock() CommOS_SpinUnlock(&commGlobalLock)
#define CommGlobalLockBH() CommOS_SpinLockBH(&commGlobalLock)
#define CommGlobalUnlockBH() CommOS_SpinUnlockBH(&commGlobalLock)

#define DispatchTrylock(chan) CommOS_MutexTrylock(&(chan)->dispatchMutex)
#define DispatchUnlock(chan) CommOS_MutexUnlock(&(chan)->dispatchMutex)

#define WriteLock(chan) CommOS_MutexLock(&(chan)->writeMutex)
#define WriteTrylock(chan) CommOS_MutexTrylock(&(chan)->writeMutex)
#define WriteUnlock(chan) CommOS_MutexUnlock(&(chan)->writeMutex)

#define StateLock(chan) CommOS_MutexLock(&(chan)->stateMutex)
#define StateTrylock(chan) CommOS_MutexTrylock(&(chan)->stateMutex)
#define StateUnlock(chan) CommOS_MutexUnlock(&(chan)->stateMutex)

#define CommHoldInit(chan) CommOS_WriteAtomic(&(chan)->holds, 0)
#define CommHold(chan) CommOS_AddReturnAtomic(&(chan)->holds, 1)
#define CommRelease(chan) CommOS_SubReturnAtomic(&(chan)->holds, 1)
#define CommIsHeld(chan) (CommOS_ReadAtomic(&(chan)->holds) > 0)

#define PacketLenOverLimit(chan, len) \
   (((len) - sizeof (CommPacket)) > ((chan)->transpArgs.capacity / 4))


/*
 * Data structure describing the offload <-> paravirtualized module
 * communication channel.
 */

struct CommChannelPriv {
   CommOSAtomic holds;                 // Active readers and writers
   CommTranspInitArgs transpArgs;      // Transport initialization arguments
   CommTransp transp;                  // Transport handle
   CommOSMutex dispatchMutex;          // Dispatch mutex
   CommOSMutex writeMutex;             // Non-BH write mutex
   CommOSMutex stateMutex;             // Upper-layer state mutex
   CommOSWaitQueue availableWaitQ;     // Available write space wait data
   unsigned int desiredWriteSpace;     // Size of write space needed
   const CommImpl *impl;               // Implementation
   unsigned int implNmbOps;            // Number of implementation operations
   unsigned int lifecycleState;        // Lifecycle state
   void *state;                        // Upper layer-specific state
};


static volatile int running;              // Initialized and running.
static CommOSWaitQueue exitWaitQ;         // Exit wait queue.
static CommOSSpinlock commGlobalLock;     // Global lock.


/* Communication channel slots. */

static unsigned int commChannelCapacity;     // Maximum number of channels.
static unsigned int commChannelSize;         // Current size of channel array.
static unsigned int commChannelAllocated;    // Nmb. entries currently in use.
static struct CommChannelPriv *commChannels; // Allocated channel array.


/**
 * @brief Callback function called when the other side created a transport
 *     handle to which we need to potentially attach.
 * @param[in,out] transpArgs arguments used when shared memory area was created.
 * @param probeData our callback data, an implementation block.
 * @return 0 if successful, -1 otherwise.
 * @sideeffects May allocate a channel.
 */

static int
DefaultTranspListener(CommTranspInitArgs *transpArgs,
                      void *probeData)
{
   int rc = -1;
   const int inBH = 1;
   const CommImpl *impl;

   if (!transpArgs || !probeData) {
      CommOS_Debug(("%s: NULL args [0x%p, 0x%p].\n",
                    __FUNCTION__, transpArgs, probeData));
      goto out;
   }

   impl = probeData;
   CommOS_Debug(("%s: Received attach info [%u,%u,%u:%u].\n",
                 __FUNCTION__,
                 transpArgs->capacity, transpArgs->type,
                 transpArgs->id.d32[0], transpArgs->id.d32[1]));

   if (impl->checkArgs(transpArgs)) {
      goto out;
   }
   transpArgs->mode = COMM_TRANSP_INIT_ATTACH; /* Ensure we attach. */

   /* We recognized it, so don't let others waste any time. Even if we fail. */

   rc = 0;
   if (Comm_Alloc(transpArgs, impl, inBH, NULL)) {
      impl->closeNtf(impl->closeNtfData, transpArgs, inBH);
      CommOS_Log(("%s: Can't allocate new channel!\n", __FUNCTION__));
   }

out:
   return rc;
}


/**
 * @brief Sets the lifecycle state of a channel entry
 * @param channel channel to update
 * @param newState state to update to
 */

static inline void
SetLifecycleState(CommChannel channel,
                  unsigned int newState)
{

   channel->lifecycleState = newState;
}


/* Wait conditions: functions returning 1: true, 0: false, < 0: error. */

/**
 * @brief Wait condition function to check whether module can be unloaded.
 * @param arg1 dummy
 * @param arg2 dummy
 * @return 1 if no channels are currently allocated, 0 if there are
 */

static int
ExitCondition(void *arg1,
              void *arg2)
{
   unsigned int i;
   int rc;

   (void)arg1;
   (void)arg2;
   CommOS_Debug(("%s: running [%d] "
                 "commChannelAllocated [%u] commChannelSize [%u].\n",
                 __FUNCTION__, running, commChannelAllocated, commChannelSize));
   rc = !running && (commChannelAllocated == 0);
   if (!rc) {
      for (i = 0; i < commChannelCapacity; i++) {
         CommOS_Debug(("%s: channel[%u] state [0x%x].\n",
                       __FUNCTION__, i, commChannels[i].lifecycleState));
      }
   }
   return rc;
}


/**
 * @brief Wait condition function to check available write space.
 * @param arg1 pointer to CommChannel struct
 * @param arg2 size argument
 * @return 1 if there is enough write space, 0 if not, -ENOMEM if comm down.
 */

static int
WriteSpaceCondition(void *arg1,
                    void *arg2)
{
   CommChannel channel = arg1;

   if (!CommIsActive(channel)) {
      return -ENOMEM;
   }
   return channel->desiredWriteSpace < CommTransp_EnqueueSpace(channel->transp);
}


/**
 * @brief Registers an implementation block used when attaching to channels
 *    in response to transport attach events.
 * @param impl implementation block.
 * @return 0 if successful, non-zero otherwise.
 */

int
Comm_RegisterImpl(const CommImpl *impl)
{
   CommTranspListener listener = {
      .probe = DefaultTranspListener,
      .probeData = (void *)impl
   };

   return CommTransp_Register(&listener);
}


/**
 * @brief Unregisters an implementation block used when attaching to channels
 *    in response to transport attach events.
 * @param impl implementation block.
 */

void
Comm_UnregisterImpl(const CommImpl *impl)
{
   CommTranspListener listener = {
      .probe = DefaultTranspListener,
      .probeData = (void *)impl
   };

   CommTransp_Unregister(&listener);
}


/**
 * @brief Allocates and initializes comm global state. Single-threaded use.
 * @param maxChannels maximum number of channels.
 * @return zero if successful, non-zero otherwise.
 */

int
Comm_Init(unsigned int maxChannels)
{
   int rc = -1;
   unsigned int i;

   if (running || commChannels ||
       (maxChannels == 0) || (maxChannels > COMM_CHANNEL_MAX_CAPACITY)) {
      goto out;
   }

#if defined(COMM_INSTRUMENT)
   CommOS_WriteAtomic(&commMaxCoalesceSize, 0);
   CommOS_WriteAtomic(&commPacketsReceived, 0);
   CommOS_WriteAtomic(&commCommittedPacketsReceived, 0);
   CommOS_WriteAtomic(&commOpCalls, 0);
#endif

   CommOS_WaitQueueInit(&exitWaitQ);
   CommOS_SpinlockInit(&commGlobalLock);
   commChannelCapacity = maxChannels;
   commChannelAllocated = 0;
   commChannels = CommOS_Kmalloc((sizeof *commChannels) * commChannelCapacity);
   if (!commChannels) {
      goto out;
   }

   memset(commChannels, 0, (sizeof *commChannels) * commChannelCapacity);
   for (i = 0; i < commChannelCapacity; i++ ) {
      CommChannel channel;

      channel = &commChannels[i];
      CommHoldInit(channel);
      channel->transp = NULL;
      CommOS_MutexInit(&channel->dispatchMutex);
      CommOS_MutexInit(&channel->writeMutex);
      CommOS_MutexInit(&channel->stateMutex);
      CommOS_WaitQueueInit(&channel->availableWaitQ);
      channel->desiredWriteSpace = -1U;
      channel->state = NULL;
      CommSetFree(channel);
   }

   rc = CommTransp_Init();
   if (!rc) {
      commChannelSize = 0;
      running = 1;
      rc = 0;
   } else {
      CommOS_Kfree(commChannels);
   }

out:
   return rc;
}


/**
 * @brief Initiates and finishes, comm global state deallocations.
 * @param timeoutMillis initialization timeout in milliseconds
 * @return zero if deallocations done, non-zero if more calls are needed.
 */

int
Comm_Finish(unsigned long long *timeoutMillis)
{
   int rc;
   unsigned int i;
   unsigned long long timeout;

   for (i = 0; i < commChannelSize; i++) {
      Comm_Zombify(&commChannels[i], 0);
   }

   running = 0;
   timeout = timeoutMillis ? *timeoutMillis : 0;
   /* coverity[var_deref_model] */
   rc = CommOS_Wait(&exitWaitQ, ExitCondition, NULL, NULL, &timeout);
   if (rc == 1) {
      /*
       * Didn't time out, task wasn't interrupted, we can wrap it up..
       */

      CommTransp_Exit();
      CommOS_Kfree(commChannels);
      commChannels = NULL;
      commChannelSize = 0;
#if defined(COMM_INSTRUMENT)
      CommOS_Log(("%s: commMaxCoalesceSize = %lu.\n",
                  __FUNCTION__,
                  CommOS_ReadAtomic(&commMaxCoalesceSize)));
      CommOS_Log(("%s: commPacketsReceived = %lu.\n",
                  __FUNCTION__,
                  CommOS_ReadAtomic(&commPacketsReceived)));
      CommOS_Log(("%s: commCommittedPacketsReceived = %lu.\n",
                  __FUNCTION__,
                  CommOS_ReadAtomic(&commCommittedPacketsReceived)));
      CommOS_Log(("%s: commOpCalls = %lu.\n",
                  __FUNCTION__,
                  CommOS_ReadAtomic)(&commOpCalls)));
#endif
      rc = 0;
   } else {
      rc = -1;
   }
   return rc;
}


/**
 * @brief Finds a free entry and initializes it with the information provided.
 *     May be called from BH. It doesn't call potentially blocking functions.
 *
 * @note Depending on the choice of shared memory transport (VMCI or MVP QP),
 * the 'inBH' distinction is important. VMCI datagrams are received under
 * some circumstances in bottom-half context, so 'inBH' should be set. This
 * is not a restriction on MVP.
 *
 * @param transpArgs transport initialization arguments.
 * @param impl implementation block.
 * @param inBH non-zero if called in bottom half.
 * @param[out] newChannel newly allocated channel.
 * @return zero if successful, non-zero otherwise.
 * @sideeffects Initializes the communications channel with given parameters
 */

int
Comm_Alloc(const CommTranspInitArgs *transpArgs,
           const CommImpl *impl,
           int inBH,
           CommChannel *newChannel)
{
   unsigned int i;
   CommChannel channel = NULL;
   int restoreSize = 0;
   int modHeld = 0;
   int rc = -1;

   if (inBH) {
      CommGlobalLock();
   } else {
      CommGlobalLockBH();
   }

   if (!running || !transpArgs || !impl) {
      goto out;
   }

   if (CommOS_ModuleGet(impl->owner)) {
      goto out;
   }
   modHeld = 1;

   for (i = 0; i < commChannelSize; i++) {
      /*
       * Check if this channel is already allocated. We don't match against
       * ANY because those channels are in the process of being opened; after
       * that happens, they'll get proper IDs.
       */

      if (!CommIsFree(&commChannels[i]) &&
          (transpArgs->id.d64 != COMM_TRANSP_ID_64_ANY) &&
          (transpArgs->id.d64 == commChannels[i].transpArgs.id.d64)) {
         goto out;
      }
      if (!channel && CommIsFree(&commChannels[i])) {
         channel = &commChannels[i];
      }
   }
   if (!channel) {
      if (commChannelSize == commChannelCapacity) {
         goto out;
      }
      channel = &commChannels[commChannelSize];
      commChannelSize++;
      restoreSize = 1;
   }

   if (channel->transp) { /* Inconsistency! */
      if (restoreSize) {
         commChannelSize--;
      }
      goto out;
   }

   channel->transpArgs = *transpArgs;
   channel->impl = impl;
   for (i = 0; impl->operations[i]; i++) {
      ;
   }
   channel->implNmbOps = i;
   channel->desiredWriteSpace = -1U;
   commChannelAllocated++;
   CommSetInitialized(channel);
   if (newChannel) {
      *newChannel = channel;
   }
   rc = 0;
   CommOS_ScheduleDisp();

out:
   if (inBH) {
      CommGlobalUnlock();
   } else {
      CommGlobalUnlockBH();
   }
   if (rc && modHeld) {
      CommOS_ModulePut(impl->owner);
   }
   return rc;
}


/**
 * @brief Zombifies a channel. May fail if channel isn't active.
 * @param[in,out] channel channel to zombify.
 * @param inBH non-zero if called in bottom half.
 * @return zero if channel zombified, non-zero otherwise.
 */

int
Comm_Zombify(CommChannel channel,
             int inBH)
{
   int rc = -1;

   if (!running) {
      goto out;
   }
   if (inBH) {
      CommGlobalLock();
   } else {
      CommGlobalLockBH();
   }
   if (CommIsActive(channel) || CommIsOpened(channel)) {
      CommSetZombie(channel);
      rc = 0;
   }
   if (inBH) {
      CommGlobalUnlock();
   } else {
      CommGlobalUnlockBH();
   }

out:
   if (!rc) {
      CommOS_ScheduleDisp();
   }
   return rc;
}


/**
 * @brief Reports whether a channel is active.
 * @param channel channel to report on.
 * @return non-zero if channel active, zero otherwise.
 */

int
Comm_IsActive(CommChannel channel)
{
   return channel ? CommIsActive(channel) : 0;
}


/**
 * @brief Wakes up potential writer on the channel. This function must be
 *    called on an active channel, with either the dispatch lock taken, or
 *    the channel ref count incremented.
 * @param channel CommChannel structure on which potential writer waits.
 */

static inline void
WakeUpWriter(CommChannel channel)
{
   if (WriteSpaceCondition(channel, NULL)) {
      CommOS_WakeUp(&channel->availableWaitQ);
   }
}


/**
 * @brief Transport event handler for comm channels.
 * @param transp transport handle.
 * @param event type of event.
 * @param data callback data.
 * @sideeffects may put the channel into zombie state, or schedule it for I/O.
 */

static void
TranspEventHandler(CommTransp transp,
                   CommTranspIOEvent event,
                   void *data)
{
   CommChannel channel = (CommChannel)data;

   switch (event) {
   case COMM_TRANSP_IO_DETACH:
      CommOS_Debug(("%s: Detach event. Zombifying channel.\n", __FUNCTION__));
      Comm_Zombify(channel, 1);
      break;

   case COMM_TRANSP_IO_IN:
   case COMM_TRANSP_IO_INOUT:
      /*
       * The dispatch threads may not have been started because either:
       * a) we're not running in the CommSvc service, or
       * b) the Comm client didn't create them explicitly (CommOS_StartIO()).
       *
       * If so, the CommOS_ScheduleDisp() call is ineffective. This is
       * the intended behavior: the client obviously wants to call the Comm
       * dispatch function(s) directly.
       */

      CommOS_ScheduleDisp();
      break;

   case COMM_TRANSP_IO_OUT:
      CommHold(channel);
      if (CommIsActive(channel)) {
         WakeUpWriter(channel);
      }
      CommRelease(channel);
      if (CommIsZombie(channel)) {
         /*
          * After releasing the hold on the channel, we must check if it was
          * set to zombie and the dispatcher was supposed to nuke it. If the
          * dispatcher had made its run while we were holding the channel, it
          * gave up. So schedule it.
          */

         CommOS_ScheduleDisp();
      }
      break;

   default:
      CommOS_Debug(("%s: Unhandled event [%u, %p, %p].\n",
                    __FUNCTION__, event, transp, data));
   }
}


/**
 * @brief Destroys upper layer state, unregisters event handlers and
 *     detaches from or deletes shared memory.
 * @param[in,out] channel CommChannel structure to close.
 */

static void
CommClose(CommChannel channel)
{
   const CommImpl *impl = channel->impl;

   StateLock(channel);
   if (impl->stateDtor && channel->state) {
      impl->stateDtor(channel->state);
   }
   channel->state = NULL;
   StateUnlock(channel);

   CommOS_ModulePut(impl->owner);

   if (channel->transp) {
      CommTransp_Close(channel->transp);
      channel->transp = NULL;
   }

   CommGlobalLockBH();
   CommSetFree(channel);
   commChannelAllocated--;
   if (channel == &commChannels[commChannelSize - 1]) {
      commChannelSize--;
   }
   CommGlobalUnlockBH();
   if (!running && (commChannelAllocated == 0)) {
      CommOS_WakeUp(&exitWaitQ);
   }
}


/**
 * @brief Allocates upper layer state, registers transport event handler
 *     and creates or attaches to shared memory.
 * @param[in,out] channel CommChannel structure to open.
 * @return  zero if successful, -1 otherwise
 * @sideeffects Memory may be allocated, event handlers registered and
 *     QP allocated or attached to.
 */

static int
CommOpen(CommChannel channel)
{
   int rc = -1;
   CommTranspEvent transpEvent = {
      .ioEvent = TranspEventHandler,
      .ioEventData = channel
   };
   const CommImpl *impl;

   if (!channel || !CommIsInitialized(channel)) {
      return rc;
   }

   if (!running) { /* Ok, toggle it back to FREE. */
      goto out;
   }

   impl = channel->impl;
   if (impl->stateCtor) {
      channel->state = impl->stateCtor(channel);
      if (!channel->state) {
         goto out;
      }
   }

   if (!CommTransp_Open(&channel->transp, &channel->transpArgs, &transpEvent)) {
      rc = 0;
   } else {
      channel->transp = NULL;
   }

out:
   if (!rc) {
      CommSetOpened(channel);
   } else {
      CommClose(channel);
   }
   return rc;
}


/**
 * @brief Retrieves a channel's transport initialization arguments.
 *     It doesn't lock, the caller must ensure the channel may be accessed.
 * @param channel CommChannel structure to get initialization arguments from.
 * @return initialization arguments used to allocate/attach to channel.
 */

CommTranspInitArgs
Comm_GetTranspInitArgs(CommChannel channel)
{
   if (!channel) {
      CommTranspInitArgs res = { .capacity = 0 };

      return res;
   }
   return channel->transpArgs;
}


/**
 * @brief Retrieves upper layer state (pointer). It doesn't lock, the caller
 *     must ensure the channel may be accessed.
 * @param channel CommChannel structure to get state from.
 * @return pointer to upper layer state.
 */

void *
Comm_GetState(CommChannel channel)
{
   if (!channel) {
      return NULL;
   }
   return channel->state;
}


/**
 * @brief Main input processing function operating on a given channel.
 * @param channel CommChannel structure to process.
 * @return number of processed channels (0 or 1), or -1 if channel closed.
 * @sideeffects Lifecycle states are transitioned to and from. Channel may
 *     be opened or destroyed, waiting writers may be woken up, and input
 *     may be handed off to operation callbacks.
 */

int
Comm_Dispatch(CommChannel channel)
{
   int rc = 0;
   int zombify = 0;
   CommPacket packet;
   CommPacket firstPacket;
   unsigned int dataLen;
#define VEC_SIZE 32
   struct kvec vec[VEC_SIZE];
   unsigned int vecLen;

   /*
    * Taking the reader mutex is safe in all cases: entries, including
    * free ones, are guaranteed to have initialized mutexes and locks.
    * Locking empty entries may seem wasteful, but those entries are rare.
    */

   if (DispatchTrylock(channel)) {
      return 0;
   }

   /* Process input and writer wake-up. */

   if (CommIsActive(channel)) {
      /*
       * The entry may have transitioned to ZOMBIE, somehow. That's OK
       * since it can't be freed just yet (it's currently locked).
       */

      /* Wake up any waiting writers, if necessary. */

      WakeUpWriter(channel);

      /* Read packets, payloads. */
      CommTransp_DequeueReset(channel->transp);

      for (vecLen = 0; vecLen < VEC_SIZE; vecLen++) {
         if (!running) {
            break;
         }

         /* Read header. */

         rc = CommTransp_DequeueSegment(channel->transp,
                                        &packet, sizeof packet);
         if (rc <= 0) {
            /* No packet (header). */

            rc = vecLen == 0 ? 0 : 1;
            break;
         }
#if defined(COMM_INSTRUMENT)
         CommOS_AddReturnAtomic(commPacketsReceived, 1);
#endif
         if ((rc != sizeof packet) || (packet.len < sizeof packet)) {
            rc = -1; /* Fatal protocol error, close down comm. */
            break;
         }
         rc = 1;

         /* Read payload, if any. */

         dataLen = packet.len - sizeof packet;
         if (vecLen == 0) {
            /* Save header of first packet. */

            firstPacket = packet;
            if (dataLen == 0) {
               /* Commit no-payload packet read and we're done. */

               CommTransp_DequeueCommit(channel->transp);
#if defined(COMM_INSTRUMENT)
               CommOS_AddReturnAtomic(&commCommittedPacketsReceived, 1);
#endif
               break;
            }
         } else {
            /*
             * Check if non-equivalent packet or above coalescing limit.
             * If so, don't commit the read.
             */

            if (memcmp(&packet.opCode, &firstPacket.opCode,
                       sizeof packet - offsetof(CommPacket, opCode)) ||
                PacketLenOverLimit(channel, firstPacket.len + dataLen)) {
               break;
            }
         }

         if (dataLen == 0) {
            /*
             * Received equivalent packet with zero-sized payload. This may
             * happen in certain cases, such as pvtcp forwarding zero-sized
             * datagrams. So don't break the loop, but keep going for as
             * along as we can.
             */

            vec[vecLen].iov_base = NULL;
            goto dequeueCommit;
         }

         /* The packet has a payload (dataLen > 0). */

         if (!(vec[vecLen].iov_base = channel->impl->dataAlloc(dataLen))) {
            /*
             * We treat out-of-(net?-)memory errors as "nothing to read".
             * Memory pressure may either subside, in which case a future
             * read may be successful, or be severe enough for the kernel
             * to oops, anyway. Leave packet uncommitted.
             */

            CommOS_Debug(("%s: COULD NOT ALLOC PAYLOAD BYTES!\n",
                          __FUNCTION__));
            rc = vecLen == 0 ? 0 : 1;
            break;
         }

         /* Read payload and commit (packet and payload). */

         rc = CommTransp_DequeueSegment(channel->transp,
                                        vec[vecLen].iov_base, dataLen);
         if (rc != dataLen) {
            channel->impl->dataFree(vec[vecLen].iov_base);
            CommOS_Log(("%s: BOOG -- COULD NOT DEQUEUE PAYLOAD! [%d != %u]",
                        __FUNCTION__, rc, dataLen));
            rc = -1; /* Fatal protocol error, close down comm. */
            break;
         }
         rc = 1;

dequeueCommit:
         CommTransp_DequeueCommit(channel->transp);
#if defined(COMM_INSTRUMENT)
         CommOS_AddReturnAtomic(&commCommittedPacketsReceived, 1);
#endif
         vec[vecLen].iov_len = dataLen;
         if (vecLen > 0) {
            firstPacket.len += dataLen;
            if (packet.flags) {
               /* Update to latest flags _iff_ latter non-zero. */

               firstPacket.flags = packet.flags;
            }
         }
#if defined(COMM_INSTRUMENT)
         if (firstPacket.len >
             CommOS_ReadAtomic(&commMaxCoalesceSize)) {
            CommOS_WriteAtomic(&commMaxCoalesceSize, firstPacket.len);
         }
#endif
         if (COMM_OPF_TEST_ERR(packet.flags)) {
            /* If error bit is set, we're done (no more coalescing). */

            vecLen++;
            break;
         }
      }

      if (rc <= 0) {
         if (rc < 0) {
            zombify = 1;
            rc = 1;
         }
         goto outUnlockAndFreeIovec;
      }

#if defined(COMM_DISPATCH_EXTRA_WRITER_WAKEUP)
      /* Check again if we need to wake up any writers. */

      WakeUpWriter(channel);
#endif

      if (firstPacket.opCode >= channel->implNmbOps) {
         CommOS_Debug(("%s: Ignoring illegal opCode [%u]!\n",
                       __FUNCTION__, (unsigned int)firstPacket.opCode));
         CommOS_Debug(("%s: Max opCode: %u\n",
                       __FUNCTION__, channel->implNmbOps));
         goto outUnlockAndFreeIovec;
      }

      /*
       * NOTE:
       * DispatchUnlock() _must_ be called from the operation callback.
       * The reason for doing so is that, for better scalability, we want
       * it released as soon as possible, BUT:
       * - releasing it here, before calling into the operation, doesn't
       *   let the latter coordinate its own lock acquisition, such as
       *   potential socket or state locks.
       * - alternatively, always releasing the dispatch lock after the
       *   operation completes, ties up the channel and imposes too much
       *   serialization between sockets.
       * - to prevent the channel from being torn down while an operation
       *   is in flight (and potentially having released the dispatch lock),
       *   we increment the ref count on the channel and then release it
       *   after the function returns.
       */

#if defined(COMM_INSTRUMENT)
      CommOS_AddReturnAtomic(&commOpCalls, 1);
#endif

      CommHold(channel);
      channel->impl->operations[firstPacket.opCode](channel, channel->state,
                                                    &firstPacket, vec, vecLen);
      CommRelease(channel);
      goto out; /* No unlocking, see comment above. */
   }

   /* Process state changes. */

   if (CommIsZombie(channel) && !CommIsHeld(channel)) {
      CommTranspInitArgs transpArgs = channel->transpArgs;
      void (*closeNtf)(void *,
                       const CommTranspInitArgs *,
                       int inBH) = channel->impl->closeNtf;
      void *closeNtfData = channel->impl->closeNtfData;

      while (WriteTrylock(channel)) {
         /* Take the write lock; kick writers out if necessary. */

         CommOS_Debug(("%s: Kicking writers out...\n", __FUNCTION__));
         CommOS_WakeUp(&channel->availableWaitQ);
      }
      WriteUnlock(channel);

      CommOS_Debug(("%s: Nuking zombie channel.\n", __FUNCTION__));
      CommClose(channel);
      if (closeNtf) {
         closeNtf(closeNtfData, &transpArgs, 0);
      }
      rc = -1;
   } else if (CommIsInitialized(channel) &&
              (channel->impl->openAtMillis <=
               CommOS_GetCurrentMillis())) {
      if (!CommOpen(channel)) {
         if (channel->transpArgs.mode == COMM_TRANSP_INIT_CREATE) {
            /*
             * If the attach side doesn't get notified, the entry will
             * time out in OPENED and will be collected.
             * Note that during the CommOpen(Transp_Open) call, the IDs
             * in the transpArgs may have changed. Use those.
             */

            CommTransp_Notify(&channel->impl->ntfCenterID,
                              &channel->transpArgs);
         } else { /* Attach mode */
            packet.len = sizeof packet;
            packet.opCode = 0xff;
            packet.flags = 0x00;

            /*
             * Send out control packet, attach ack, and transition straight
             * to ACTIVE.
             */

            rc = CommTransp_EnqueueAtomic(channel->transp,
                                          &packet, sizeof packet);
            if (rc == sizeof packet) {
               /* Guard against potentially concurrent zombify. */

               CommGlobalLockBH();
               if (CommIsOpened(channel)) {
                  CommOS_Debug(("%s: Sent attach ack. Activating channel.\n",
                                __FUNCTION__));
                  CommSetActive(channel);
               }
               CommGlobalUnlockBH();
            }
         }
         rc = 1;
      }
   } else if (CommIsOpened(channel) &&
              (channel->transpArgs.mode == COMM_TRANSP_INIT_CREATE)) {
      /*
       * Get control packet (opCode == 0xff), attach ack (flags == 0x0),
       * or check whether the channel timed out in OPENED.
       */

      rc = CommTransp_DequeueAtomic(channel->transp,
                                    &packet, sizeof packet);
      if (rc == sizeof packet) {
         void (*activateNtf)(void *activateNtfData, CommChannel) = NULL;
         void *activateNtfData = NULL;

         /* Guard against potentially concurrent zombify. */

         CommGlobalLockBH();
         if (CommIsOpened(channel) &&
             (packet.opCode == 0xff) && (packet.flags == 0x0)) {
            activateNtf = channel->impl->activateNtf;
            activateNtfData = channel->impl->activateNtfData;

            CommSetActive(channel);
            CommOS_Debug(("%s: Received attach ack. Activating channel.\n",
                          __FUNCTION__));
         }
         CommHold(channel);
         CommGlobalUnlockBH();

         if (activateNtf) {
            /* The callback must be short and 'put' the channel when done. */

            activateNtf(activateNtfData, channel);
         } else {
            /* Don't forget to put back the channel if no activate callback. */

            CommRelease(channel);
         }
      } else if ((channel->impl->openTimeoutAtMillis <=
                  CommOS_GetCurrentMillis()) ||
                 !running) {
         zombify = 1;
         CommOS_Debug(("%s: Zombifying expired opened channel.\n",
                       __FUNCTION__));
      }
      rc = 1;
   }
   DispatchUnlock(channel);

out:
   if (zombify) {
      Comm_Zombify(channel, 0);
   }
   return rc;

outUnlockAndFreeIovec:
   DispatchUnlock(channel);
   for ( ; vecLen; ) {
      if (vec[--vecLen].iov_base) {
         channel->impl->dataFree(vec[vecLen].iov_base);
         vec[vecLen].iov_base = NULL;
      }
      vec[vecLen].iov_len = 0;
   }
   goto out;
#undef VEC_SIZE
}


/**
 * @brief Main input processing function operating on all channels.
 * @return number of processed channels.
 * @sideeffects Lifecycle states are transitioned to and from. Channels may
 *     be opened and destroyed, waiting writers may be woken up, and input
 *     may be handed off to operation callbacks.
 */

unsigned int
Comm_DispatchAll(void)
{
   unsigned int i;
   unsigned int hits;

   for (hits = 0, i = 0; running && (i < commChannelSize); i++) {
      hits += !!Comm_Dispatch(&commChannels[i]);
   }
   return hits;
}


/**
 * @brief Writes a fully formatted packet (containing payload data, if
 *    applicable) to the specified channel.
 *
 *    The operation may block until enough write space is available, but no
 *    more than the specified interval.  The operation either writes the full
 *    amount of bytes, or it fails.  Warning: callers must _not_ use the
 *    _Lock/_Unlock functions to bracket calls to this function.
 * @param[in,out] channel channel to write to.
 * @param packet packet to write.
 * @param[in,out] timeoutMillis interval in milliseconds to wait.
 * @return number of bytes written, 0 if it times out, -1 error.
 * @sideeffects Data may be written to the channel.
 */

int
Comm_Write(CommChannel channel,
           const CommPacket *packet,
           unsigned long long *timeoutMillis)
{
   int rc = -1;
   int zombify;

   if (!channel || !timeoutMillis ||
       !packet || (packet->len < sizeof *packet)) {
      return rc;
   }

   zombify = (*timeoutMillis >= COMM_MAX_TO);

   WriteLock(channel);
   if (!CommIsActive(channel)) {
      goto out;
   }

   CommTransp_EnqueueReset(channel->transp);
   channel->desiredWriteSpace = packet->len;
   rc = CommOS_DoWait(&channel->availableWaitQ, WriteSpaceCondition,
                      channel, NULL, timeoutMillis,
                      (*timeoutMillis != COMM_MAX_TO_UNINT));
   channel->desiredWriteSpace = -1U;

   if (rc) { /* Don't zombify, if it didn't time out. */
      zombify = 0;
   }
   if (rc == 1) { /* Enough write space, enqueue the packet. */
      rc = CommTransp_EnqueueAtomic(channel->transp, packet, packet->len);
      if (rc != packet->len) {
         zombify = 1;
         rc = -1; /* Fatal protocol error. */
      }
   }

out:
   WriteUnlock(channel);
   if (zombify) {
      Comm_Zombify(channel, 0);
   }
   return rc;
}


/**
 * @brief Writes a packet and associated payload data to the specified channel.
 *     The operation may block until enough write space is available, but
 *     not more than the specified interval.
 *     The operation either writes the full amount of bytes, or it fails.
 *     If there is not enough data in the vector, padding will be added to
 *     reach the specified packet length, if the flags parameter requires it.
 *     Users may call this function successively to write several packets
 *     from large {io|k}vecs, when the flags parameter indicates it. If this
 *     is the case, the packet header needs to be updated accordingly in
 *     between calls, for the different (total) lengths.
 *     Warning: callers must _not_ use the _Lock/_Unlock functions to bracket
 *              calls to this function.
 * @param[in,out] channel the specified channel.
 * @param packet packet to write.
 * @param[in,out] vec kvec to write from.
 * @param[in,out] vecLen length of kvec.
 * @param[in,out] timeoutMillis interval in milliseconds to wait.
 * @param[in,out] iovOffset must be set to 0 before first call (internal cookie)
 * @return number of bytes written, 0 if it timed out, -1 error.
 * @sideeffects data may be written to the channel.
 */

int
Comm_WriteVec(CommChannel channel,
              const CommPacket *packet,
              struct kvec **vec,
              unsigned int *vecLen,
              unsigned long long *timeoutMillis,
              unsigned int *iovOffset)
{
   int rc;
   int zombify;
   unsigned int dataLen;
   unsigned int vecDataLen;
   unsigned int vecNdx;
   unsigned int iovLen;
   void *iovBase;

   if (!channel || !timeoutMillis || !iovOffset ||
       !packet || (packet->len < sizeof *packet) ||
       (((dataLen = packet->len - sizeof *packet) > 0) &&
        (!*vec || !*vecLen))) {
      return -1;
   }

   zombify = (*timeoutMillis >= COMM_MAX_TO);

   WriteLock(channel);
   if (!CommIsActive(channel)) {
      rc = -1;
      goto out;
   }

   CommTransp_EnqueueReset(channel->transp);
   channel->desiredWriteSpace = packet->len;
   rc = CommOS_DoWait(&channel->availableWaitQ, WriteSpaceCondition,
                      channel, NULL, timeoutMillis,
                      (*timeoutMillis != COMM_MAX_TO_UNINT));
   channel->desiredWriteSpace = -1U;

   if (rc) { /* Don't zombify, if it didn't time out. */
      zombify = 0;
   }
   if (rc == 1) { /* Enough write space, enqueue the packet. */
      iovLen = 0;
      rc = CommTransp_EnqueueSegment(channel->transp, packet, sizeof *packet);
      if (rc != sizeof *packet) {
         zombify = 1;
         rc = -1; /* Fatal protocol error. */
         goto out;
      }

      if (dataLen > 0) {
         int done = 0;

         for (vecDataLen = 0, vecNdx = 0; vecNdx < *vecLen; vecNdx++) {
            if (vecNdx) {
               *iovOffset = 0;
            }
            iovLen = (*vec)[vecNdx].iov_len - *iovOffset;
            iovBase = (*vec)[vecNdx].iov_base + *iovOffset;

            if (!iovLen) {
               continue;
            }

            vecDataLen += iovLen;
            if (vecDataLen >= dataLen) {
               iovLen -= (vecDataLen - dataLen);
               done = 1;
            }

            rc = CommTransp_EnqueueSegment(channel->transp, iovBase, iovLen);
            if (rc != iovLen) {
               zombify = 1;
               rc = -1; /* Fatal protocol error, close down comm. */
               goto out;
            }

            if (done) {
               CommTransp_EnqueueCommit(channel->transp);
               if (vecDataLen == dataLen) {
                  vecNdx++;
                  *iovOffset = 0;
               } else {
                  *iovOffset += iovLen;
               }
               *vecLen -= vecNdx;
               *vec += vecNdx;
               break;
            }
         }

         if (!done) {
            /*
             * We exhausted all the bytes in the given vector, but total length
             * in the packet header is more than we sent (was available).
             * If so, we pad by sending zero bytes to reach length required.
             */

            static char pad[1024];
            unsigned int delta;
            unsigned int toSend;

            while (vecDataLen < dataLen) {
               delta = dataLen - vecDataLen;
               toSend = delta <= sizeof pad ? delta : sizeof pad;
               if (toSend == delta) {
                  done = 1;
               }
               vecDataLen += toSend;

               rc = CommTransp_EnqueueSegment(channel->transp, pad, toSend);
               if (rc != toSend) {
                  zombify = 1;
                  rc = -1; /* Fatal protocol error, close down comm. */
                  goto out;
               }

               if (done) {
                  CommTransp_EnqueueCommit(channel->transp);
                  *vec = NULL;
                  *vecLen = 0;
                  *iovOffset = 0;
                  break;
               }
            }
         }
      } else {
         CommTransp_EnqueueCommit(channel->transp);
      }
      rc = (int)packet->len;
   } else {
      CommOS_Debug(("%s: timed out...\n", __FUNCTION__));
   }

out:
   WriteUnlock(channel);
   if (zombify) {
      Comm_Zombify(channel, 0);
   }
   return rc;
}


/**
 * @brief Releases channel ref count. This function is exported for the upper
 *    layer's 'activateNtf' callback which may be run asynchronously. The
 *    callback is protected from concurrent channel releases until it calls
 *    this function.
 * @param[in,out] channel CommChannel structure to release.
 */

void
Comm_Put(CommChannel channel)
{
   if (channel) {
      CommRelease(channel);
   }
}


/**
 * @brief Uses the read lock. This function is exported for the upper layer
 *    such that it can order acquisition of a different lock (socket) with
 *    the release of the dispatch lock.
 * @param[in,out] channel CommChannel structure to unlock.
 */

void
Comm_DispatchUnlock(CommChannel channel)
{
   if (channel) {
      DispatchUnlock(channel);
   }
}


/**
 * @brief Lock the channel for upper layer state.
 *    This function is exported for the upper layer to ensure that channel
 *    isn't closed while updating the layer state. Operations using this
 *    function are expected to be short, since unlike the _Write functions,
 *    these callers cannot be signaled.
 * @param[in,out] channel CommChannel structure to lock.
 * @return zero if successful, -1 otherwise.
 */

int
Comm_Lock(CommChannel channel)
{
   if (!channel) {
      return -1;
   }
   StateLock(channel);
   if (!CommIsActive(channel) && !CommIsZombie(channel)) {
      StateUnlock(channel);
      return -1;
   }
   return 0;
}


/**
 *  @brief Uses the writer lock. This function is exported for the upper layer
 *     to ensure that channel isn't closed while updating the layer state.
 *     See Comm_Lock for details).
 *  @param[in,out] channel CommChannel structure to unlock.
 */

void
Comm_Unlock(CommChannel channel)
{
   if (channel) {
      StateUnlock(channel);
   }
}


/**
 * @brief Requests events be posted in-line after the function completes.
 * @param channel channel object.
 * @return current number of requests for inline event posting, or -1 on error.
 */

unsigned int
Comm_RequestInlineEvents(CommChannel channel)
{
   if (channel->transp) {
      return CommTransp_RequestInlineEvents(channel->transp);
   } else {
      return (unsigned int)-1;
   }
}


/**
 * @brief Requests events be posted out-of-band after the function completes.
 * @param channel channel object.
 * @return current number of requests for inline event posting, or -1 on error.
 */

unsigned int
Comm_ReleaseInlineEvents(CommChannel channel)
{
   if (channel->transp) {
      return CommTransp_ReleaseInlineEvents(channel->transp);
   } else {
      return (unsigned int)-1;
   }
}
