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
 * @brief Offload common definitions.
 * This file is meant to only be included via pvtcp.h.
 */

#ifndef _PVTCP_OFF_H_
#define _PVTCP_OFF_H_


#define PVTCP_OFF_SOCK_COMMON_FIELDS                                \
   volatile unsigned int opFlags; /* Saves op codes as bit mask. */ \
   volatile unsigned int flags    /* General purpose flags.      */


/* General purpose socket flags */

enum PvtcpOffPvskFlags {
   PVTCP_OFF_PVSKF_IPV6_LOOP = 0, /* Used for IPV6 loopback morphing/reset.   */
   PVTCP_OFF_PVSKF_SHUT_RD,       /* Set to initiate socket recv shutdown.    */
   PVTCP_OFF_PVSKF_SHUT_WR,       /* Set to initiate socket send shutdown.    */
   PVTCP_OFF_PVSKF_TCP_NODELAY,   /* Caches the TCP_NODELAY socket option.    */
   PVTCP_OFF_PVSKF_TCP_CORK,      /* Caches the TCP_CORK socket option.       */
   PVTCP_OFF_PVSKF_DISCONNECT,    /* Set do indicate connect()/AF_UNSPEC.     */
   PVTCP_OFF_PVSKF_INVALID = 32
};


/*
 * Include OS-dependent PvtcpSock structure and functions.
 */

#if defined(__linux__)
#include "pvtcp_off_linux.h"
#else
#error "Unsupported OS."
#endif


/*
 * Offload packet payload data structure.
 */

typedef struct PvtcpOffBuf {
   CommOSList link;    // Link in socket queue.
   unsigned short len;
   unsigned short off;
   char data[1];
} PvtcpOffBuf;


/**
 *  @brief Returns net buffer given private data structure pointer and based
 *      on the internal offset pointer
 *  @param arg pointer to PvtcpOffBuf wrapper structure
 *  @return address of buffer or NULL
 */

static inline void *
PvtcpOffBufFromInternalOff(PvtcpOffBuf *arg)
{
   return arg ?
          &arg->data[arg->off] :
          NULL;
}


/**
 *  @brief Returns net buffer given private data structure pointer
 *  @param arg pointer to PvtcpOffBuf wrapper structure
 *  @return address of buffer or NULL
 */

static inline void *
PvtcpOffBufFromInternal(PvtcpOffBuf *arg)
{
   return arg ?
          &arg->data[0] :
          NULL;
}


/**
 *  @brief Returns internal data structure given net buffer pointer
 *  @param arg pointer to PvtcpOffBuf wrapper structure
 *  @return address of internal data structure or NULL
 */

static inline PvtcpOffBuf *
PvtcpOffInternalFromBuf(void *arg)
{
   return arg ?
          (PvtcpOffBuf *)((char *)arg - offsetof(PvtcpOffBuf, data)) :
          NULL;
}


/**
 * @brief Tests operation flag for AIO processing.
 * @param pvsk socket to test operation on.
 * @param op operation to test if set.
 * @return non-zero if operation set, zero otherwise.
 * @sideeffect socket processing by AIO threads affected according to operation.
 */

static inline int
PvskTestOpFlag(struct PvtcpSock *pvsk,
               int op)
{
   return pvsk->opFlags & (1 << op);
}


/**
 * @brief Sets operation flag for AIO processing; acquires the state lock.
 * @param[in,out] pvsk socket to set operation on.
 * @param op operation to set.
 * @sideeffect socket processing by AIO threads affected according to operation.
 */

static inline void
PvskSetOpFlag(struct PvtcpSock *pvsk,
              int op)
{
   unsigned int ops;

   SOCK_STATE_LOCK(pvsk);
   ops = pvsk->opFlags | (1 << op);
   pvsk->opFlags = ops;
   SOCK_STATE_UNLOCK(pvsk);
}


/**
 * @brief Resets operation flag for AIO processing; acquires the state lock.
 * @param[in,out] pvsk socket to reset operation on.
 * @param op operation to reset.
 * @sideeffect socket processing by AIO threads affected according to operation.
 */

static inline void
PvskResetOpFlag(struct PvtcpSock *pvsk,
                int op)
{
   unsigned int ops;

   SOCK_STATE_LOCK(pvsk);
   ops = pvsk->opFlags & ~(1 << op);
   pvsk->opFlags = ops;
   SOCK_STATE_UNLOCK(pvsk);
}


/**
 * @brief Tests general purpose socket flags.
 * @param pvsk socket.
 * @param flag flag to test.
 * @return non-zero if flag set, zero otherwise.
 */

static inline int
PvskTestFlag(struct PvtcpSock *pvsk,
             int flag)
{
   return (flag < PVTCP_OFF_PVSKF_INVALID) && (pvsk->flags & (1 << flag));
}


/**
 * @brief Sets general purpose socket flags; acquires the state lock.
 * @param[in,out] pvsk socket.
 * @param flag flag to set or clear.
 * @param onOff whether to set or clear the flag.
 */

static inline void
PvskSetFlag(struct PvtcpSock *pvsk,
            int flag,
            int onOff)
{
   unsigned int flags;

   SOCK_STATE_LOCK(pvsk);
   if (flag < PVTCP_OFF_PVSKF_INVALID) {
      if (onOff) {
         flags = pvsk->flags | (1 << flag);
      } else {
         flags = pvsk->flags & ~(1 << flag);
      }
      pvsk->flags = flags;
   }
   SOCK_STATE_UNLOCK(pvsk);
}


int PvtcpOffSockInit(PvtcpSock *pvsk, CommChannel channel);

#endif // _PVTCP_OFF_H_
