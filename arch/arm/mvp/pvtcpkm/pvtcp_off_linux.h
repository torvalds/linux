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
 * @brief Linux Offload definitions.
 * This file is only meant to be included via pvtcp_off.h.
 */

#ifndef _PVTCP_OFF_LINUX_H_
#define _PVTCP_OFF_LINUX_H_

#include <linux/socket.h>
#include <net/sock.h>
#include <net/tcp_states.h>
#include <net/tcp.h>
#include <linux/in.h>
#include <linux/in6.h>
#include <linux/skbuff.h>
#include <linux/random.h>
#include <linux/fs.h>
#include <linux/cred.h>


typedef struct PvtcpSock {
   struct sock *sk;
   PVTCP_SOCK_COMMON_FIELDS;
   PVTCP_OFF_SOCK_COMMON_FIELDS;
   void (*destruct)(struct sock *sk);
   void (*stateChange)(struct sock *sk);
   void (*dataReady)(struct sock *sk, int bytes);
   void (*writeSpace)(struct sock *sk);
   void (*errorReport)(struct sock *sk);
} PvtcpSock;


typedef enum PvtcpSockNamespace {
   PVTCP_SOCK_NAMESPACE_INITIAL,
   PVTCP_SOCK_NAMESPACE_CHANNEL
} PvtcpSockNamespace;


/* Number of large datagram allocations. */
extern unsigned long long pvtcpOffDgramAllocations;

/* Inet4 loopback addresses. */
extern unsigned int pvtcpLoopbackOffAddr;

/* Get the 'struct sock' from a PvtcpSock. */
#define SkFromPvsk(pvsk) ((pvsk)->sk)

/* Get the PvtcpSock from a 'struct sock'. */
#define PvskFromSk(sk) ((PvtcpSock *)(sk)->sk_user_data)

int
PvtcpTestAndBindLoopbackInet4(PvtcpSock *pvsk,
                              unsigned int *addr,
                              unsigned short port);
int
PvtcpTestAndBindLoopbackInet6(PvtcpSock *pvsk,
                              unsigned long long *addr0,
                              unsigned long long *addr1,
                              unsigned short port);

void PvtcpResetLoopbackInet4(PvtcpSock *pvsk, unsigned int *addr);
void PvtcpResetLoopbackInet6(PvtcpSock *pvsk, struct in6_addr *in6);

void PvtcpFlowAIO(PvtcpSock *pvsk, int eof);
void PvtcpOutputAIO(PvtcpSock *pvsk);
int PvtcpInputAIO(PvtcpSock *pvsk, void *perCpuBuf);


/**
 * @brief Switches a socket to the channel, or the initial name space.
 * @param pvsk socket to switch.
 * @param ns which namespace to switch to.
 */

static inline void
PvtcpSwitchSock(PvtcpSock *pvsk,
                PvtcpSockNamespace ns)
{
#if defined(CONFIG_NET_NS) && !defined(PVTCP_NET_NS_DISABLE)
   struct sock *sk;
   struct net *prevNet;

   if (!pvsk) {
      return;
   }
   sk = SkFromPvsk(pvsk);
   if (!sk) {
      /* If this is a phony, create fail reporting pvsk, just return. */

      return;
   }

   prevNet = sock_net(sk);
   switch (ns) {
   case PVTCP_SOCK_NAMESPACE_INITIAL:
      sock_net_set(sk, get_net(&init_net));
      break;
   case PVTCP_SOCK_NAMESPACE_CHANNEL:
      sock_net_set(sk, get_net(pvsk->state->namespace));
      break;
   }
   put_net(prevNet);
#endif
}


/**
 * @brief Tests whether a socket has an explicit namespace.
 * @param pvsk socket to test.
 * @return 1 if the socket has a namespace, 0 otherwise.
 */

static inline int
PvtcpHasSockNamespace(PvtcpSock *pvsk)
{
#if defined(CONFIG_NET_NS) && !defined(PVTCP_NET_NS_DISABLE)
   struct sock *sk;
   int rc = 0;

   if (!pvsk) {
      return rc;
   }
   sk = SkFromPvsk(pvsk);
   if (!sk) {
      /* If this is a phony, create fail reporting pvsk, just return 0. */

      return rc;
   }

   rc = (sock_net(sk) != &init_net);
   return rc;
#else
   return 0;
#endif
}


/**
 * @brief Retains the pvsock's underlying socket.
 * @param pvsk socket to retain.
 */

static inline void
PvtcpHoldSock(PvtcpSock *pvsk)
{
   struct sock *sk = SkFromPvsk(pvsk);

   if (likely(sk)) {
      sock_hold(sk);
   }
}


/**
 * @brief Releases a hold on the pvsock's underlying socket. If the underlying
 *   socket is NULL, this is an error socket and we deallocate it.
 * @param pvsk socket to release hold on.
 */

static inline void
PvtcpPutSock(PvtcpSock *pvsk)
{
   struct sock *sk = SkFromPvsk(pvsk);

   if (likely(sk)) {
      sock_put(sk);
   } else {
      /*
       * This is an error socket, which does _not_ have an underlying socket.
       * We simply need to free it.
       */

      CommOS_Kfree(pvsk);
   }
}


/**
 * @brief Schedules an offload socket for AIO.
 * @param pvsk socket to schedule.
 * @sideeffect the socket will be processed by AIO threads.
 */

static inline void
PvtcpSchedSock(PvtcpSock *pvsk)
{
   /*
    * We must hold the socket before we enqueue it for AIO, such that it may
    * not be released while in the workqueue. If CommSvc_ScheduleAIOWork()
    * returned non-zero, it means the socket had already been enqueued. In
    * that case, we release the hold. Otherwise, the hold is released by the
    * AIO function (PvtcpProcessAIO()).
    * Note that error pv sockets may only originate from synchronized RPCs,
    * or to be more precise, from PvtcpCreateOp(), and not from IO processing;
    * this means that they cannot be attempted to be enqueued more than once.
    */

   PvtcpHoldSock(pvsk);
   if (CommSvc_ScheduleAIOWork(&pvsk->work)) {
      PvtcpPutSock(pvsk);
   }
}

#endif // _PVTCP_OFF_LINUX_H_
