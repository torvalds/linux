/*
 * Linux 2.6.32 and later Kernel module for VMware MVP PVTCP Server
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
 * @brief Pvtcp common code.
 */

#include "pvtcp.h"


/*
 * Operation table.
 */

CommOperationFunc pvtcpOperations[] = {
   [PVTCP_OP_FLOW] = PvtcpFlowOp,
   [PVTCP_OP_IO] = PvtcpIoOp,
   [PVTCP_OP_CREATE] = PvtcpCreateOp,
   [PVTCP_OP_RELEASE] = PvtcpReleaseOp,
   [PVTCP_OP_BIND] = PvtcpBindOp,
   [PVTCP_OP_LISTEN] = PvtcpListenOp,
   [PVTCP_OP_ACCEPT] = PvtcpAcceptOp,
   [PVTCP_OP_CONNECT] = PvtcpConnectOp,
   [PVTCP_OP_SHUTDOWN] = PvtcpShutdownOp,
   [PVTCP_OP_SETSOCKOPT] = PvtcpSetSockOptOp,
   [PVTCP_OP_GETSOCKOPT] = PvtcpGetSockOptOp,
   [PVTCP_OP_IOCTL] = PvtcpIoctlOp,
   [PVTCP_OP_INVALID] = NULL
};


/*
 * Implementation block.
 */

CommImpl pvtcpImpl = {
   .owner = NULL,
   .checkArgs = PvtcpCheckArgs,
   .stateCtor = PvtcpStateAlloc,
   .stateDtor = PvtcpStateFree,
   .dataAlloc = PvtcpBufAlloc,
   .dataFree = PvtcpBufFree,
   .operations = pvtcpOperations,
   .closeNtf = PvtcpCloseNtf,
   .closeNtfData = &pvtcpImpl,
   .ntfCenterID = {{
      .d32[0] = 2U    /* x86 host context (vmci, only). */,
      .d32[1] = 10000 /* Default, not yet reserved, resource (vmci, only). */
   }}
};


/*
 * Version array.
 */

const char *pvtcpVersions[] = {
   [PVTCP_VERS_1_1] = PVTCP_COMM_IMPL_VERS_1_1,
   [PVTCP_VERS_1_0] = PVTCP_COMM_IMPL_VERS_1_0
};

const unsigned int pvtcpVersionsSize =
   (sizeof pvtcpVersions / sizeof pvtcpVersions[0]);


/*
 * Client (pv) channel to offload side. We choose to define it here, although
 * it's only applicable to the pv implementation. The reason is that we can
 * share a common close notification function which does the right thing
 * depending on the channel configuration.
 */

CommChannel pvtcpClientChannel;


/*
 * Built-in state interfaces.
 */

static PvtcpIfConf ifUnbound = {
   .family = PVTCP_PF_UNBOUND
};
const PvtcpIfConf *pvtcpIfUnbound = &ifUnbound;

static PvtcpIfConf ifDeathRow = {
   .family = PVTCP_PF_DEATH_ROW
};
const PvtcpIfConf *pvtcpIfDeathRow = &ifDeathRow;

static PvtcpIfConf ifLoopbackInet4 = {
   .family = PVTCP_PF_LOOPBACK_INET4
};
const PvtcpIfConf *pvtcpIfLoopbackInet4 = &ifLoopbackInet4;


/* Functions */

/**
 *  @brief Checks if the IF configuration has reasonable values.
 *  @param conf configuration to check
 *  @return zero if successful, -1 otherwise
 */

static int
IfCheck(const PvtcpIfConf *conf)
{
   if (!conf ||
       ((conf->family != PF_INET) &&
        (conf->family != PF_INET6) &&
        (conf->family != PVTCP_PF_UNBOUND) &&
        (conf->family != PVTCP_PF_DEATH_ROW) &&
        (conf->family != PVTCP_PF_LOOPBACK_INET4))) {
      return -1;
   }

   /** @todo Need more checks for IP/netmask format validity. */
   return 0;
}


/**
 *  @brief Checks if the IF has reasonable values, but restricts types to
 *      AF_INET and AF_INET6
 *  @param conf IF to check
 *  @return zero if successful, -1 otherwise
 */

static int
IfRestrictedCheck(const PvtcpIfConf *conf)
{
   if (IfCheck(conf) ||
       ((conf->family != PF_INET) &&
        (conf->family != PF_INET6))) {
      return -1;
   }
   return 0;
}


/**
 *  @brief Finds a netif given a state and a configuration. The configuration
 *      must have already been checked. This function doesn't lock, so it
 *      should not be called when the state, or the netif for the passed
 *      configuration may be deleted.
 *  @param state state to look for.
 *  @param conf configuration to look for.
 *  @return netif matching configuration, or NULL.
 */

PvtcpIf *
PvtcpStateFindIf(PvtcpState *state,
                 const PvtcpIfConf *conf)
{
   PvtcpIf *netif;

   if (!state) {
      return NULL;
   }

   if (conf->family == PVTCP_PF_UNBOUND) {
      return &state->ifUnbound;
   }

   if (conf->family == PVTCP_PF_DEATH_ROW) {
      return &state->ifDeathRow;
   }

   if (conf->family == PVTCP_PF_LOOPBACK_INET4) {
      return &state->ifLoopbackInet4;
   }

   CommOS_ListForEach(&state->ifList, netif, stateLink) {
      if (netif->conf.family == conf->family) {
         if ((conf->family == PF_INET &&
              !memcmp(&netif->conf.addr.in, &conf->addr.in,
                      sizeof conf->addr.in)) ||
             (conf->family == PF_INET6 &&
              !memcmp(&netif->conf.addr.in6, &conf->addr.in6,
                      sizeof conf->addr.in6))) {
            return netif;
         }
      }
   }
   return NULL;
}


/**
 *  @brief Creates and initializes a new netif for a given channel and with
 *      the specified configuration. Death row and unbound netifs may not
 *      be added using this function.
 *  @param[in,out] channel channel to make a new netif in
 *  @param conf configuration to set netif to
 *  @return 0 if successful, -1 otherwise
 *  @sideeffect May allocate memory
 */

int
PvtcpStateAddIf(CommChannel channel,
                const PvtcpIfConf *conf)
{
   int rc = -1;
   PvtcpState *state;
   PvtcpIf *netif;

   if (!channel || IfRestrictedCheck(conf)) {
      return rc;
   }

   if (CommSvc_Lock(channel)) {
      return rc; /* channel isn't active. */
   }

   state = CommSvc_GetState(channel);
   if (!state) {
      goto out;
   }

   if (PvtcpStateFindIf(state, conf)) {
      goto out; /* Already configured. */
   }

   netif = CommOS_Kmalloc(sizeof *netif);
   if (!netif) {
      goto out;
   }

   INIT_LIST_HEAD(&netif->stateLink);
   INIT_LIST_HEAD(&netif->sockList);
   netif->state = state;
   netif->conf = *conf;
   CommOS_ListAddTail(&state->ifList, &netif->stateLink);
   rc = 0;

out:
   CommSvc_Unlock(channel);
   return rc;
}


/**
 *  @brief Removes and potentially deallocates all sockets associated with the
 *      given netif and deallocates the latter.
 *  @param[in,out] netif netif to deallocate
 *  @sideeffect Closes sockets, deallocates memory
 */

static void
IfFree(PvtcpIf *netif)
{
   PvtcpSock *pvsk;
   PvtcpSock *tmp;

   if (netif) {
      CommOS_ListForEachSafe(&netif->sockList, pvsk, tmp, ifLink) {
         CommOS_ListDel(&pvsk->ifLink);
         PvtcpReleaseSocket(pvsk);
      }
      if ((netif->conf.family != PVTCP_PF_UNBOUND) &&
          (netif->conf.family != PVTCP_PF_DEATH_ROW) &&
          (netif->conf.family != PVTCP_PF_LOOPBACK_INET4)) {
         CommOS_ListDel(&netif->stateLink);
         CommOS_Kfree(netif);
      }
   }
}


/**
 *  @brief Closes all sockets associated with, and deallocates the netif
 *      in the given channel and with the specified configuration.
 *      Death row and unbound netifs may not be removed using this function.
 *  @param[in,out] channel channel to remove from
 *  @param conf configuration specified
 *  @return zero if successful, error code otherwise
 *  @sideeffect Closes sockets, deallocates memory
 */

void
PvtcpStateRemoveIf(CommChannel channel,
                   const PvtcpIfConf *conf)
{
   PvtcpState *state;
   PvtcpIf *netif;

   if (!channel || IfRestrictedCheck(conf)) {
      return;
   }

   if (CommSvc_Lock(channel)) {
      return; /* channel isn't active. */
   }

   state = CommSvc_GetState(channel);
   if (state && (netif = PvtcpStateFindIf(state, conf))) {
      if (netif->state == state) {
         IfFree(netif);
      }
   }

   CommSvc_Unlock(channel);
}


/**
 *  @brief Adds a socket to an existing netif. If the socket is already on a
 *      different netif, it is removed from that netif.
 *      It locks the must-be-active channel. We use that lock to guard
 *      against concurrent removal of the netif.
 *  @param[in,out] channel channel to add to
 *  @param conf specified configuration
 *  @param[in,out] sock socket to add
 *  @return zero if successful, -1 otherwise
 */

int
PvtcpStateAddSocket(CommChannel channel,
                    const PvtcpIfConf *conf,
                    PvtcpSock *sock)
{
   int rc = -1;
   PvtcpState *state;
   PvtcpIf *netif;

   if (!channel || !sock || (sock->channel != channel) || IfCheck(conf)) {
      return rc;
   }

   if (CommSvc_Lock(channel)) {
      return rc; /* channel isn't active. */
   }

   state = CommSvc_GetState(channel);
   if (!state) {
      goto out;
   }

   netif = PvtcpStateFindIf(state, conf);
   if (!netif) {
      goto out;
   }

   CommOS_ListDel(&sock->ifLink);
   sock->netif = netif;
   CommOS_ListAddTail(&netif->sockList, &sock->ifLink);
   rc = 0;

out:
   CommSvc_Unlock(channel);
   return rc;
}


/**
 *  @brief Removes a socket from its netif.
 *      It locks the must-be-active channel. We use that lock to guard
 *      against concurrent removal of the netif.
 *  @param[in,out] channel channel to remove from
 *  @param[in,out] sock socket to remove
 *  @return zero if successful, -1 otherwise
 */

int
PvtcpStateRemoveSocket(CommChannel channel,
                       PvtcpSock *sock)
{
   if (!channel || !sock ||
       (sock->channel && (sock->channel != channel))) {
      return -1;
   }

   if (CommSvc_Lock(channel)) {
      return -1; /* channel isn't active. */
   }

   CommOS_ListDel(&sock->ifLink);
   sock->channel = NULL;
   sock->state = NULL;
   sock->netif = NULL;

   CommSvc_Unlock(channel);
   return 0;
}


/**
 *  @brief State constructor called when a channel is created. The netifs
 *      'death row' and 'unbound' are always initialized.
 *  @param[in,out] channel channel to initialize
 *  @return pointer to a new state structure or NULL
 *  @sideeffect Allocates memory
 */

void *
PvtcpStateAlloc(CommChannel channel)
{
   PvtcpState *state;

   state = CommOS_Kmalloc(sizeof *state);
   if (state) {
      state->channel = channel;
      INIT_LIST_HEAD(&state->ifList);

      /* Initialize always-present netifs. */
      INIT_LIST_HEAD(&state->ifDeathRow.stateLink); /* Irrelevant */
      INIT_LIST_HEAD(&state->ifDeathRow.sockList);
      state->ifDeathRow.state = state;
      state->ifDeathRow.conf.family = PVTCP_PF_DEATH_ROW;

      INIT_LIST_HEAD(&state->ifUnbound.stateLink); /* Irrelevant */
      INIT_LIST_HEAD(&state->ifUnbound.sockList);
      state->ifUnbound.state = state;
      state->ifUnbound.conf.family = PVTCP_PF_UNBOUND;

      INIT_LIST_HEAD(&state->ifLoopbackInet4.stateLink); /* Irrelevant */
      INIT_LIST_HEAD(&state->ifLoopbackInet4.sockList);
      state->ifLoopbackInet4.state = state;
      state->ifLoopbackInet4.conf.family = PVTCP_PF_LOOPBACK_INET4;

      state->namespace = NULL;
      state->mask = ((unsigned int)channel << 4) ^ (unsigned int)state;
#if defined(__linux__)
      state->id = ((unsigned long long)random32() << 32) |
                  (unsigned long long)random32();
#else
      state->id = (unsigned long long)state;
#endif
   }
   return state;
}


/**
 *  @brief State destructor called when a channel is closed.
 *      The caller (Comm) guarantees proper locking.
 *  @param arg pointer to state structure
 *  @sideeffect Destroys all netifs and their sockets, deallocates memory
 */

void
PvtcpStateFree(void *arg)
{
   PvtcpState *state = arg;
   PvtcpIf *netif;
   PvtcpIf *tmp;

   if (state) {
      CommOS_ListForEachSafe(&state->ifList, netif, tmp, stateLink) {
         IfFree(netif);
      }
      /* coverity[address_free] */
      IfFree(&state->ifLoopbackInet4);
      /* coverity[address_free] */
      IfFree(&state->ifUnbound);
      /* coverity[address_free] */
      IfFree(&state->ifDeathRow);
      CommOS_Kfree(state);
   }
}


/**
 *  @brief Checks transport arguments.
 *  @param transpArgs transport arguments.
 *  @return zero if successful, < 0 otherwise.
 */

int
PvtcpCheckArgs(CommTranspInitArgs *transpArgs)
{
   int rc = -1;
   const unsigned int minCapacity =
      (PVTCP_SOCK_BUF_SIZE + sizeof(CommPacket)) * 2;
   unsigned int versionIndex = pvtcpVersionsSize;

   if (transpArgs->capacity < minCapacity) {
      return rc;
   }

   while (versionIndex--) {
      if (transpArgs->type == CommTransp_GetType(pvtcpVersions[versionIndex])) {
         /* If a match, overwrite the hash with the actual version (index). */

         transpArgs->type = versionIndex;
         rc = 0;
         break;
      }
   }

   return rc;
}


/**
 *  @brief Called after a channel is freed.
 *  @param ntfData callback data from implementation block.
 *  @param transpArgs transport arguments of closed channel.
 *  @param inBH whether called in bottom half.
 */

void
PvtcpCloseNtf(void *ntfData,
              const CommTranspInitArgs *transpArgs,
              int inBH)
{
   CommImpl *impl = (CommImpl *)ntfData;

   pvtcpClientChannel = NULL;
   CommOS_Log(("%s: Channel was reset!\n", __FUNCTION__));

   /*
    * If the impl. block owner is NULL, we're pv client: we attempt to
    * reopen the channel in a few seconds.
    */

   if (impl && !impl->owner && !inBH) {
      CommOS_Log(("%s: Attempting to re-initialize channel.\n", __FUNCTION__));
      impl->openAtMillis = CommOS_GetCurrentMillis();
      impl->openTimeoutAtMillis =
         CommOS_GetCurrentMillis() + PVTCP_CHANNEL_OPEN_TIMEOUT;
      if (CommSvc_Alloc(transpArgs, impl, inBH, &pvtcpClientChannel)) {
         CommOS_Log(("%s: Failed to initialize channel!\n", __FUNCTION__));
      }
   }
}


/**
 * @brief Initializes the Pvtcp socket common fields.
 * @param pvsk pvtcp socket.
 * @param channel Comm channel this socket is associated with.
 * @return 0 if successful, -1 otherwise.
 */

int
PvtcpSockInit(PvtcpSock *pvsk,
              CommChannel channel)
{
   PvtcpState *state;
   int rc = -1;

   if (pvsk && channel && (state = CommSvc_GetState(channel))) {
      /* Must _not_ zero out pvsk! */

      CommOS_MutexInit(&pvsk->inLock);
      CommOS_MutexInit(&pvsk->outLock);
      CommOS_SpinlockInit(&pvsk->stateLock);
      CommOS_ListInit(&pvsk->ifLink);
      CommOS_InitWork(&pvsk->work, PvtcpProcessAIO);
      pvsk->netif = NULL;
      pvsk->state = state;
      pvsk->stateID = state->id;
      pvsk->channel = channel;
      pvsk->peerSock = PVTCP_PEER_SOCK_NULL;
      pvsk->peerSockSet = 0;
      CommOS_WriteAtomic(&pvsk->deltaAckSize,
                         (1 << PVTCP_SOCK_SMALL_ACK_ORDER));
      CommOS_WriteAtomic(&pvsk->rcvdSize, 0);
      CommOS_WriteAtomic(&pvsk->sentSize, 0);
      CommOS_WriteAtomic(&pvsk->queueSize, 0);
      CommOS_ListInit(&pvsk->queue);
      pvsk->rpcReply = NULL;
      pvsk->rpcStatus = 0;
      pvsk->err = 0;
      rc = 0;
   }
   return rc;
}
