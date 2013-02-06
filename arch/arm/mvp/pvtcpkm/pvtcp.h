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
 * @brief Pvtcp common APIs.
 */

#ifndef _PVTCP_H_
#define _PVTCP_H_

/*
 * Pvtcp state store ipv4 and ipv6 address structures.
 * Platform-specific headers where these are defined, must be included here.
 * Implementation-related header files should not be included in this file.
 *
 * NOTE: Pvtcp is not an API and none of its functions are exported.
 */

#if defined(__linux__)
#include <linux/in.h>
#include <linux/in6.h>
#else
#error "Unsupported OS."
#endif

#include "comm_svc.h"

/* Max time to wait for a channel to be created. */
#define PVTCP_CHANNEL_OPEN_TIMEOUT 2000

/* Max payload size. Used to allocate offload per-cpu bounce buffers. */
#define PVTCP_SOCK_BUF_SIZE (8 << 10) /* 8K */

#define PVTCP_SOCK_DGRAM_BUF_SIZE PVTCP_SOCK_BUF_SIZE
#define PVTCP_SOCK_STREAM_BUF_SIZE PVTCP_SOCK_BUF_SIZE

/* Dgram payloads include a pseudo (udp/ip) header. */
typedef struct PvtcpDgramPseudoHeader {
   unsigned long long d0;
   unsigned long long d1;
   unsigned long long d2;
   unsigned long long d3;
} PvtcpDgramPseudoHeader;


/*
 * Flow control constants for pv/offload sockets.
 * We are defining a receive size model: 1) small, 2) medium, 3)large.
 * This seems sufficient in addressing most target environments, but more
 * models may be defined. A smaller minimum model (1) cannot be defined.
 *
 * Short description of socket-level flow control. This applies to both
 * dgram and stream sockets, in both directions. It follows that, with regard
 * to 'comm' writes, dgram and stream writes are: a) lossless and b) ordered.
 *
 * 0. Both sides (offload, pv) of a socket maintain (almost) mirror values
 *    of input/output queue sizes. We say 'almost', because they're allowed
 *    to conservatively converge in time.
 * 1. Senders never write out to the shmem channel, and destined to a socket
 *    (be it offload or pv), more bytes than that socket can hold/enqueue.
 *    This is based on socket fields storing information mentioned above.
 *    The upper limit is PVTCP_SOCK_RCVSIZE and cannot be exceeded under
 *    any circumstances.
 * 2. There is a 'safe' limit value (per socket) which can be tested prior
 *    to writing one more max-sized packet to that socket.
 *    This value is PVTCP_SOCK_SAFE_RCVSIZE.
 * 3. There is also a notion of 'large' acks, which controls the frequency of
 *    reporting socket queue size changes when bytes are consumed from it.
 *    When a sender is about to write out (to the channel, for a given socket)
 *    in excess of PVTCP_SOCK_LARGE_ACK_WM bytes, it sets, in the packet
 *    header flag field, the PVTCP_SOCK_LARGE_ACK_ORDER value. The other end
 *    updates its 'delta ack' value accordingly (1 << flag value).
 * 4. As bytes are consumed (again, at either end), the operation or function,
 *    will send a size ack packet with the consumed size since the last ack,
 *    _iff_ that size is larger than, or equal to the 'delta ack' value.
 *    If an ack was sent, the 'delta ack' is decreased by half, to a minimum
 *    indicated by PVTCP_SOCK_SMALL_ACK_ORDER.
 *    Note that concurrently setting the 'delta ack' to its high value
 *    because of condition 3) above, is fine since the sender already has,
 *    or is about to put pressure on the socket.
 */

#if !defined(PVTCP_SOCK_RCVSIZE_MODEL)
   #define PVTCP_SOCK_RCVSIZE_MODEL 1
#endif

#if PVTCP_SOCK_RCVSIZE_MODEL == 1
   #define PVTCP_SOCK_LARGE_ACK_WM (64 << 10) /* 64K */
   #define PVTCP_SOCK_LARGE_ACK_ORDER 15
   #define PVTCP_SOCK_SMALL_ACK_ORDER 11
   #define PVTCP_SOCK_SAFE_RCVSIZE (128 << 10) /* 128K */
#elif PVTCP_SOCK_RCVSIZE_MODEL == 2
   #define PVTCP_SOCK_LARGE_ACK_WM (128 << 10) /* 128K */
   #define PVTCP_SOCK_LARGE_ACK_ORDER 16
   #define PVTCP_SOCK_SMALL_ACK_ORDER 12
   #define PVTCP_SOCK_SAFE_RCVSIZE (256 << 10) /* 256K */
#elif PVTCP_SOCK_RCVSIZE_MODEL == 3
   #define PVTCP_SOCK_LARGE_ACK_WM (128 << 10) /* 128K */
   #define PVTCP_SOCK_LARGE_ACK_ORDER 16
   #define PVTCP_SOCK_SMALL_ACK_ORDER 12
   #define PVTCP_SOCK_SAFE_RCVSIZE (512 << 10) /* 512K */
#else
   #error "Invalid PVTCP_SOCK_RCVSIZE_MODEL (one of 1, 2, 3)"
#endif

#define PVTCP_SOCK_RCVSIZE    \
   (PVTCP_SOCK_SAFE_RCVSIZE + \
    PVTCP_SOCK_BUF_SIZE + sizeof (PvtcpDgramPseudoHeader))


/*
 * Operation codes
 */

enum PvtcpOpCodes {
   PVTCP_OP_FLOW = 0,
   PVTCP_OP_IO,
   PVTCP_OP_CREATE,
   PVTCP_OP_RELEASE,
   PVTCP_OP_BIND,
   PVTCP_OP_LISTEN,
   PVTCP_OP_ACCEPT,
   PVTCP_OP_CONNECT,
   PVTCP_OP_SHUTDOWN,
   PVTCP_OP_SETSOCKOPT,
   PVTCP_OP_GETSOCKOPT,
   PVTCP_OP_IOCTL,
   PVTCP_OP_INVALID
};

#define PVTCP_FLOW_OP_INVALID_SIZE 0xffffffff


/*
 * Operation functions
 */

COMM_DEFINE_OP(PvtcpFlowOp);
COMM_DEFINE_OP(PvtcpIoOp);
COMM_DEFINE_OP(PvtcpCreateOp);
COMM_DEFINE_OP(PvtcpReleaseOp);
COMM_DEFINE_OP(PvtcpBindOp);
COMM_DEFINE_OP(PvtcpListenOp);
COMM_DEFINE_OP(PvtcpAcceptOp);
COMM_DEFINE_OP(PvtcpConnectOp);
COMM_DEFINE_OP(PvtcpShutdownOp);
COMM_DEFINE_OP(PvtcpSetSockOptOp);
COMM_DEFINE_OP(PvtcpGetSockOptOp);
COMM_DEFINE_OP(PvtcpIoctlOp);


/*
 * Pvtcp/Comm type and supported versions.
 */

#define PVTCP_COMM_IMPL_TYPE "com.vmware.comm.protocol.pvTCP@"

#define PVTCP_COMM_IMPL_VERS_1_0 (PVTCP_COMM_IMPL_TYPE "1.0")
#define PVTCP_COMM_IMPL_VERS_1_1 (PVTCP_COMM_IMPL_TYPE "1.1")

typedef enum {
   PVTCP_VERS_1_0 = 0,
   PVTCP_VERS_1_1
} PvtcpVersion;

extern const char *pvtcpVersions[];
extern const unsigned int pvtcpVersionsSize;


/*
 * State interface markers
 */

#define PVTCP_PF_UNBOUND   0x0
#define PVTCP_PF_DEATH_ROW 0xffffffff
#define PVTCP_PF_LOOPBACK_INET4 (PVTCP_PF_DEATH_ROW - 1)


/*
 * Interface and interface configuration structures.
 */

typedef struct PvtcpIfConf {
   int family;                      // Values:
                                    //    unbound  (PVTCP_PF_UNBOUND)
                                    //    deathRow (PVTCP_PF_DEATH_ROW)
                                    //    loopback (PVTCP_PF_LOOPBACK_INET4)
                                    //    inet4    (PF_INET)
                                    //    inet6    (PF_INET6)
   union {
      struct in_addr in;
      struct in6_addr in6;
   } addr;                          // inet4 or inet6 address.
   union {
      struct in_addr in;
      struct in6_addr in6;
   } mask;                          // inet4 or inet6 netmask.
} PvtcpIfConf;


struct PvtcpState;

typedef struct PvtcpIf {
   CommOSList sockList;       // List of sockets.
   CommOSList stateLink;      // Link in PvtcpState.ifList.
   struct PvtcpState *state;  // Back reference to state.
   PvtcpIfConf conf;          // Interface configuration.
} PvtcpIf;


/*
 * General pvtcp state associated with a channel.
 */

typedef struct PvtcpState {
   unsigned long long id;     // Randomly generated state ID.
   CommOSList ifList;         // List of active interfaces.
   CommChannel channel;       // Comm channel back reference.
   PvtcpIf ifDeathRow;        // Always-present netif.
   PvtcpIf ifUnbound;         // Ditto.
   PvtcpIf ifLoopbackInet4;   // Ditto.
   void *namespace;           // Name space, where supported.
   void *extra;               // Used by upper layer to extend state as needed.
   unsigned int mask;         // Mask used to obfuscate socket pointers.
} PvtcpState;


/*
 * Define pvtcp socket common fields and include the pv or offload header
 * to get the right PvtcpSock definition.
 */

#define PVTCP_SOCK_COMMON_FIELDS                                           \
   CommOSMutex inLock;          /* Input lock.                          */ \
   CommOSMutex outLock;         /* Output lock.                         */ \
   CommOSSpinlock stateLock;    /* State update lock.                   */ \
   CommOSList ifLink;           /* Link in PvtcpIf.sockList.            */ \
   CommOSWork work;             /* Work item for AIO processing.        */ \
   PvtcpIf *netif;              /* Netif reference.                     */ \
   PvtcpState *state;           /* State reference.                     */ \
   unsigned long long stateID;  /* State ID.                            */ \
   CommChannel channel;         /* Comm channel reference.              */ \
   unsigned long long peerSock; /* Peer socket, opaque.                 */ \
   volatile int peerSockSet;    /* Peer socket valid.                   */ \
   CommOSAtomic deltaAckSize;   /* Recv size updates required by peer.  */ \
   CommOSAtomic rcvdSize;       /* Bytes received since last ack.       */ \
   CommOSAtomic sentSize;       /* Bytes sent; also updated by peer.    */ \
   CommOSAtomic queueSize;      /* Queue size.                          */ \
   CommOSList queue;            /* Send queue (off) or recv queue (pv). */ \
   void *rpcReply;              /* RPC reply.                           */ \
   int rpcStatus;               /* RPC completion status.               */ \
   int err                      /* Socket error.                        */

#define PVTCP_PEER_SOCK_NULL ((unsigned long long)0)


/*
 * Helper macros
 */

#define SOCK_STATE_LOCK(pvsk)   CommOS_SpinLock(&(pvsk)->stateLock)
#define SOCK_STATE_UNLOCK(pvsk) CommOS_SpinUnlock(&(pvsk)->stateLock)

#define SOCK_IN_TRYLOCK(pvsk)   CommOS_MutexTrylock(&(pvsk)->inLock)
#define SOCK_IN_LOCK(pvsk)      CommOS_MutexLock(&(pvsk)->inLock)
#define SOCK_IN_UNLOCK(pvsk)    CommOS_MutexUnlock(&(pvsk)->inLock)

#define SOCK_OUT_TRYLOCK(pvsk)  CommOS_MutexTrylock(&(pvsk)->outLock)
#define SOCK_OUT_LOCK(pvsk)     CommOS_MutexLock(&(pvsk)->outLock)
#define SOCK_OUT_LOCK_UNINT(pvsk) \
   CommOS_MutexLockUninterruptible(&(pvsk)->outLock)
#define SOCK_OUT_UNLOCK(pvsk)   CommOS_MutexUnlock(&(pvsk)->outLock)

#define PVTCP_UNLOCK_DISP_DISCARD_VEC()      \
   CommSvc_DispatchUnlock(channel);          \
   while (vecLen) {                          \
      PvtcpBufFree(vec[--vecLen].iov_base);  \
   }


#if defined(PVTCP_BUILDING_SERVER)
#include "pvtcp_off.h"
#else
#include "pvtcp_pv.h"
#endif // defined(PVTCP_BUILDING_SERVER)


/*
 * Data declarations
 */

extern const PvtcpIfConf *pvtcpIfUnbound;
extern const PvtcpIfConf *pvtcpIfDeathRow;
extern const PvtcpIfConf *pvtcpIfLoopbackInet4;

extern CommImpl pvtcpImpl;
extern CommOperationFunc pvtcpOperations[];

extern CommChannel pvtcpClientChannel;


/*
 * Common state manipulation functions.
 */

void *PvtcpStateAlloc(CommChannel channel);
void PvtcpStateFree(void *arg);

int PvtcpStateAddIf(CommChannel channel, const PvtcpIfConf *conf);
void PvtcpStateRemoveIf(CommChannel channel, const PvtcpIfConf *conf);
PvtcpIf *PvtcpStateFindIf(PvtcpState *state, const PvtcpIfConf *conf);

int
PvtcpStateAddSocket(CommChannel channel,
                    const PvtcpIfConf *conf,
                    PvtcpSock *sock);
int PvtcpStateRemoveSocket(CommChannel channel, PvtcpSock *sock);


/*
 * Common Pvtcp functions.
 */

int PvtcpCheckArgs(CommTranspInitArgs *transpArgs);

void
PvtcpCloseNtf(void *ntfData,
              const CommTranspInitArgs *transpArgs,
              int inBH);

void *PvtcpBufAlloc(unsigned int size);
void PvtcpBufFree(void *buf);

void PvtcpReleaseSocket(PvtcpSock *pvsk);
int PvtcpSockInit(PvtcpSock *pvsk, CommChannel channel);

void PvtcpProcessAIO(CommOSWork *work);


/**
 * @brief Packs an IPV6 address stored in an array of four 32-bit elements,
 *    into two 64-bit variables.
 * @param addr IPV6 address as an array of 32-bit elements.
 * @param[out] d64_0 pointer to 64-bit variable.
 * @param[out] d64_1 pointer to 64-bit variable.
 */

static inline void
PvtcpI6AddrPack(const unsigned int addr[4],
                unsigned long long *d64_0,
                unsigned long long *d64_1)
{
   *d64_0 = *(unsigned long long *)&addr[0];
   *d64_1 = *(unsigned long long *)&addr[2];
}


/**
 * @brief Unpacks two 64-bit values into an IPV6 address-storing array of
 *    four 32-bit elements,
 * @param[out] addr IPV6 address as an array of 32-bit elements.
 * @param d64_0 64-bit value.
 * @param d64_1 64-bit value.
 */

static inline void
PvtcpI6AddrUnpack(unsigned int addr[4],
                  unsigned long long d64_0,
                  unsigned long long d64_1)
{
   *(unsigned long long *)&addr[0] = d64_0;
   *(unsigned long long *)&addr[2] = d64_1;
}


/**
 * @brief Verifies whether the argument is a valid socket. If yes, it returns
 *    the actual pointer. Otherwise, it returns from the calling function.
 *    WARNING: This macro must ONLY be used in operation functions, as its
 *             implementation assumes.
 * @param handle socket handle to verify.
 * @param container state supposed to contain the socket handle.
 * @return 32-bit or 64-bit PvtcpSock*, depending on __LP64__ or __LLP64__.
 */

#if defined(__LP64__) || defined(__LLP64__)

#define PvtcpGetPvskOrReturn(handle, container)                                \
   ({                                                                          \
      PvtcpState *__state = (PvtcpState *)(container);                         \
      PvtcpSock *__pvsk =                                                      \
         (PvtcpSock *)((handle) ^ (unsigned long long)__state->mask);          \
                                                                               \
      if (__pvsk->stateID != __state->id) {                                    \
         PVTCP_UNLOCK_DISP_DISCARD_VEC();                                      \
         CommSvc_Zombify(__state->channel, 0);                                 \
         return;                                                               \
      }                                                                        \
      (__pvsk);                                                                \
   })

#else // __LP64__ || __LLP64__

#define PvtcpGetPvskOrReturn(handle, container)                                \
   ({                                                                          \
      PvtcpState *__state = (PvtcpState *)(container);                         \
      PvtcpSock *__pvsk =                                                      \
         (PvtcpSock *)((unsigned int)(handle) ^ __state->mask);                \
                                                                               \
      if (__pvsk->stateID != __state->id) {                                    \
         PVTCP_UNLOCK_DISP_DISCARD_VEC();                                      \
         CommSvc_Zombify(__state->channel, 0);                                 \
         return;                                                               \
      }                                                                        \
      (__pvsk);                                                                \
   })

#endif // __LP64__ || __LLP64__


/**
 * @brief Masks a socket pointer to be passed to the peer module.
 * @param pvsk socket pointer to mask.
 * @return 64-bit pvtcp socket handle.
 */

#if defined(__LP64__) || defined(__LLP64__)

#define PvtcpGetHandle(pvsk)                                                   \
   ((unsigned long long)(pvsk) ^ (unsigned long long)(pvsk)->state->mask)

#else // __LP64__ || __LLP64__

#define PvtcpGetHandle(pvsk)                                                   \
   ((unsigned int)(pvsk) ^ (pvsk)->state->mask)

#endif // __LP64__ || __LLP64__

#endif // _PVTCP_H_
