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

#include "comm_os.h"
#include "comm_os_mod_ver.h"
#include "comm_svc.h"


/*
 * Initialization of module entry and exit callbacks expected by module
 * loading/unloading functions in comm_os.
 */

static int Init(void *args);
static void Exit(void);

COMM_OS_MOD_INIT(Init, Exit);

static volatile int running;                 // Initialized and running.


/**
 * @brief Allocates and initializes comm global state.
 *    Starts input dispatch and aio threads.
 * @param argsIn arguments
 * @return zero if successful, non-zero otherwise.
 */

static int
Init(void *argsIn)
{
   int rc = -1;
   unsigned int maxChannels = 8;
   /*
    * Infinite timeout, 1 polling cycle
    * see kernel/time.c: msecs_to_jiffies()
    */
   unsigned int pollingMillis = (unsigned int)-1;
   unsigned int pollingCycles = 1;
   const char *args = argsIn;

   if (args && *args) {
      /* coverity[secure_coding] */
      sscanf(args,
             "max_channels:%u,poll_millis:%u,poll_cycles:%u",
             &maxChannels, &pollingMillis, &pollingCycles);
      CommOS_Debug(("%s: arguments [%s].\n", __FUNCTION__, args));
   }

   rc = Comm_Init(maxChannels);
   if (rc) {
      goto out;
   }

   rc = CommOS_StartIO(COMM_OS_MOD_SHORT_NAME_STRING "-disp",
                       Comm_DispatchAll, pollingMillis, pollingCycles,
                       COMM_OS_MOD_SHORT_NAME_STRING "-aio");
   if (rc) {
      unsigned long long timeout = 0;

      Comm_Finish(&timeout); /* Nothing started, guaranteed to succeed. */
      goto out;
   }
   running = 1;
   rc = 0;

out:
   return rc;
}


/**
 * @brief Attempts to close all channels.
 * @return zero if successful, non-zero otherwise.
 */

static int
Halt(void)
{
   unsigned int maxTries = 10;
   int rc = -1;

   if (!running) {
      rc = 0;
      goto out;
   }

   for ( ; maxTries; maxTries--) {
      unsigned long long timeout = 2000ULL;

      CommOS_Debug(("%s: Attempting to halt...\n", __FUNCTION__));
      if (!Comm_Finish(&timeout)) {
         running = 0;
         rc = 0;
         break;
      }
   }

out:
   return rc;
}


/**
 * @brief Stops the comm_rt module.
 *    If Halt() call successful, stops input dispatch and aio threads.
 */

static void
Exit(void)
{
   if (!Halt()) {
      CommOS_StopIO();
   }
}


/**
 * @brief Registers an implementation block used when attaching to channels
 *    in response to transport attach events.
 * @param impl implementation block.
 * @return 0 if successful, non-zero otherwise.
 */

int
CommSvc_RegisterImpl(const CommImpl *impl)
{
   return Comm_RegisterImpl(impl);
}


/**
 * @brief Unregisters an implementation block used when attaching to channels
 *    in response to transport attach events.
 * @param impl implementation block.
 */

void
CommSvc_UnregisterImpl(const CommImpl *impl)
{
   Comm_UnregisterImpl(impl);
}


/**
 * @brief Finds a free entry and initializes it with the information provided.
 *     May be called from BH. It doesn't call potentially blocking functions.
 * @param transpArgs transport initialization arguments.
 * @param impl implementation block.
 * @param inBH non-zero if called in bottom half.
 * @param[out] newChannel newly allocated channel.
 * @return zero if successful, non-zero otherwise.
 * @sideeffects Initializes the communications channel with given parameters
 */

int
CommSvc_Alloc(const CommTranspInitArgs *transpArgs,
              const CommImpl *impl,
              int inBH,
              CommChannel *newChannel)
{
   return Comm_Alloc(transpArgs, impl, inBH, newChannel);
}


/**
 * @brief Zombifies a channel. May fail if channel isn't active.
 * @param channel channel to zombify.
 * @param inBH non-zero if called in bottom half.
 * @return zero if channel zombified, non-zero otherwise.
 */

int
CommSvc_Zombify(CommChannel channel,
                int inBH)
{
   return Comm_Zombify(channel, inBH);
}


/**
 * @brief Reports whether a channel is active.
 * @param channel channel to report on.
 * @return non-zero if channel active, zero otherwise.
 */

int
CommSvc_IsActive(CommChannel channel)
{
   return Comm_IsActive(channel);
}


/**
 * @brief Retrieves a channel's transport initialization arguments.
 *     It doesn't lock, the caller must ensure the channel may be accessed.
 * @param channel CommChannel structure to get initialization arguments from.
 * @return initialization arguments used to allocate/attach to channel.
 */

CommTranspInitArgs
CommSvc_GetTranspInitArgs(CommChannel channel)
{
   return Comm_GetTranspInitArgs(channel);
}


/**
 * @brief Retrieves upper layer state (pointer). It doesn't lock, the caller
 *     must ensure the channel may be accessed.
 * @param channel CommChannel structure to get state from.
 * @return pointer to upper layer state.
 */

void *
CommSvc_GetState(CommChannel channel)
{
   return Comm_GetState(channel);
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
CommSvc_Write(CommChannel channel,
              const CommPacket *packet,
              unsigned long long *timeoutMillis)
{
   return Comm_Write(channel, packet, timeoutMillis);
}


/**
 * @brief Writes a packet and associated payload data to the specified channel.
 *
 *     The operation may block until enough write space is available, but not
 *     more than the specified interval.  The operation either writes the full
 *     amount of bytes, or it fails.  Users may call this function successively
 *     to write several packets from large {io|k}vecs. If that's the case, the
 *     packet header needs to be updated in between calls, for the different
 *     (total) lengths.  Warning: callers must _not_ use the _Lock/_Unlock
 *     functions to bracket calls to this function.
 * @param[in,out] channel the specified channel
 * @param packet packet to write
 * @param[in,out] vec kvec to write from
 * @param[in,out] vecLen length of kvec
 * @param[in,out] timeoutMillis interval in milliseconds to wait
 * @param[in,out] iovOffset must be set to 0 before first call (internal cookie)
 * @return number of bytes written, 0 if it timed out, -1 error
 * @sideeffects data may be written to the channel
 */

int
CommSvc_WriteVec(CommChannel channel,
                 const CommPacket *packet,
                 struct kvec **vec,
                 unsigned int *vecLen,
                 unsigned long long *timeoutMillis,
                 unsigned int *iovOffset)
{
   return Comm_WriteVec(channel, packet, vec, vecLen, timeoutMillis, iovOffset);
}


/**
 * @brief Releases channel ref count. This function is exported for the upper
 *    layer's 'activateNtf' callback which may be run asynchronously. The
 *    callback is protected from concurrent channel releases until it calls
 *    this function.
 * @param[in,out] channel CommChannel structure to release.
 */

void
CommSvc_Put(CommChannel channel)
{
   Comm_Put(channel);
}


/**
 * @brief Uses the read lock. This function is exported for the upper layer
 *    such that it can order acquisition of a different lock (socket) with
 *    the release of the dispatch lock.
 * @param[in,out] channel CommChannel structure to unlock.
 */

void
CommSvc_DispatchUnlock(CommChannel channel)
{
   Comm_DispatchUnlock(channel);
}


/**
 * @brief Lock the channel.
 *
 *    Uses the writer lock. This function is exported for the upper layer
 *    to ensure that channel isn't closed while updating the layer state.
 *    It also guarantees that if the lock is taken, the entry is either ACTIVE
 *    or ZOMBIE. Operations using this function are expected to be short,
 *    since unlike the _Write functions, these callers cannot be signaled.
 * @param[in,out] channel CommChannel structure to lock.
 * @return zero if successful, -1 otherwise.
 */

int
CommSvc_Lock(CommChannel channel)
{
   return Comm_Lock(channel);
}


/**
 * @brief Unlock the channel.
 *
 *    Uses the writer lock. This function is exported for the upper layer
 *    to ensure that channel isn't closed while updating the layer state.
 *    See Comm_WriteLock for details).
 * @param[in,out] channel CommChannel structure to unlock.
 */

void
CommSvc_Unlock(CommChannel channel)
{
   Comm_Unlock(channel);
}


/**
 * @brief Schedules a work item on the AIO thread(s).
 * @param[in,out] work work item to be scheduled.
 * @return zero if successful, -1 otherwise.
 */

int
CommSvc_ScheduleAIOWork(CommOSWork *work)
{
   return CommOS_ScheduleAIOWork(work);
}


/**
 * @brief Requests events be posted in-line after the function completes.
 * @param channel channel object.
 * @return current number of requests for inline event posting, or -1 on error.
 */

unsigned int
CommSvc_RequestInlineEvents(CommChannel channel)
{
   return Comm_RequestInlineEvents(channel);
}


/**
 * @brief Requests events be posted out-of-band after the function completes.
 * @param channel channel object.
 * @return current number of requests for inline event posting, or -1 on error.
 */

unsigned int
CommSvc_ReleaseInlineEvents(CommChannel channel)
{
   return Comm_ReleaseInlineEvents(channel);
}


#if defined(__linux__)
EXPORT_SYMBOL(CommSvc_RegisterImpl);
EXPORT_SYMBOL(CommSvc_UnregisterImpl);
EXPORT_SYMBOL(CommSvc_Alloc);
EXPORT_SYMBOL(CommSvc_Zombify);
EXPORT_SYMBOL(CommSvc_IsActive);
EXPORT_SYMBOL(CommSvc_GetTranspInitArgs);
EXPORT_SYMBOL(CommSvc_GetState);
EXPORT_SYMBOL(CommSvc_Write);
EXPORT_SYMBOL(CommSvc_WriteVec);
EXPORT_SYMBOL(CommSvc_Put);
EXPORT_SYMBOL(CommSvc_DispatchUnlock);
EXPORT_SYMBOL(CommSvc_Lock);
EXPORT_SYMBOL(CommSvc_Unlock);
EXPORT_SYMBOL(CommSvc_ScheduleAIOWork);
EXPORT_SYMBOL(CommSvc_RequestInlineEvents);
EXPORT_SYMBOL(CommSvc_ReleaseInlineEvents);
#endif // defined(__linux__)
