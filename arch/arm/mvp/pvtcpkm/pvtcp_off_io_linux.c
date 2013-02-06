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
 *  @file
 *
 *  @brief Server (offload) side Linux-specific socket I/O functions.
 */

#include "pvtcp.h"

/*
 * Data.
 */

/* Used to check if OutputAIO()-ing is likely in progress. */

CommOSAtomic PvtcpOutputAIOSection;


/*
 * Large datagram bounce buffer (PVTCP_SOCK_BUF_SIZE < size <= 64K).
 * Only one such buffer is available, shared across cpus via get/put.
 * A preallocated, smaller buffer is used for most over-size 'allocs'.
 * A larger, 64K-buffer may need to be __vmalloc()-ed.
 */

typedef struct LargeDgramBuf {
   unsigned char buf[PVTCP_SOCK_BUF_SIZE << 1]; /* Fast buffer. */
   void *spareBuf;                              /* Dynamically allocated. */
   CommOSMutex lock;
} LargeDgramBuf;

static LargeDgramBuf largeDgramBuf;


/**
 * @brief One time initialization of large datagram buffer.
 */

void
PvtcpOffLargeDgramBufInit(void)
{
   largeDgramBuf.spareBuf = NULL;
   CommOS_MutexInit(&largeDgramBuf.lock);
}


/**
 * @brief Reserves/holds the large datagram buffer.
 * @param size size of buffer.
 * @sizeeffect may sleep until the buffer is available.
 * @return address of buffer, or NULL if size too large or allocation failed.
 */

static inline void *
LargeDgramBufGet(int size)
{
   static const unsigned int maxSize = 64 * 1024;

   /* coverity[alloc_fn] */
   /* coverity[var_assign] */

   CommOS_MutexLockUninterruptible(&largeDgramBuf.lock);

   if (size <= sizeof largeDgramBuf.buf) {
      return largeDgramBuf.buf;
   }

   if (size <= maxSize) {
      if (!largeDgramBuf.spareBuf) {
         largeDgramBuf.spareBuf = __vmalloc(maxSize,
                                            (GFP_ATOMIC | __GFP_HIGHMEM),
                                            PAGE_KERNEL);
      }
      if (largeDgramBuf.spareBuf) {
         return largeDgramBuf.spareBuf;
      }
   }

   CommOS_MutexUnlock(&largeDgramBuf.lock);
   return NULL;
}


/**
 * @brief Releases hold on the large datagram buffer.
 * @param buf buffer to put back.
 */

static inline void
LargeDgramBufPut(void *buf)
{
   static unsigned int spareBufPuts = 0;

   BUG_ON((buf != largeDgramBuf.buf) && (buf != largeDgramBuf.spareBuf));

   if (largeDgramBuf.spareBuf && (++spareBufPuts % 2) == 0) {
      /* Deallocate the spare buffer every now and then. */

      vfree(largeDgramBuf.spareBuf);
      largeDgramBuf.spareBuf = NULL;
   }

   CommOS_MutexUnlock(&largeDgramBuf.lock);
}


/*
 * I/O offload operations.
 */

/**
 * @brief Flow control notification received when more (enough) data was
 *      consumed from a PV socket.
 * @param channel communication channel with offloader
 * @param upperLayerState state associated with this channel
 * @param packet first packet received in reply
 * @param vec payload buffer descriptors
 * @param vecLen payload buffer descriptor count
 * @sideeffect A writer task is scheduled
 */

void
PvtcpFlowOp(CommChannel channel,
            void *upperLayerState,
            CommPacket *packet,
            struct kvec *vec,
            unsigned int vecLen)
{
   PvtcpSock *pvsk = PvtcpGetPvskOrReturn(packet->data64, upperLayerState);

   PvtcpHoldSock(pvsk);
   PVTCP_UNLOCK_DISP_DISCARD_VEC();
   CommOS_SubReturnAtomic(&pvsk->rcvdSize, (int)packet->data32);
   PvtcpSchedSock(pvsk);
   PvtcpPutSock(pvsk);
}


/**
 * @brief Outputs bytes to socket.
 * @param channel communication channel with offloader.
 * @param upperLayerState state associated with this channel.
 * @param packet received packet header.
 * @param vec payload buffer descriptors.
 * @param vecLen payload buffer descriptor count.
 * @sideeffect Changes send size/capacity ratio. May schedule AIO processing
 *   for enqueued bytes, if applicable.
 */

void
PvtcpIoOp(CommChannel channel,
          void *upperLayerState,
          CommPacket *packet,
          struct kvec *vec,
          unsigned int vecLen)
{
   int rc;
   unsigned int vecOff;
   PvtcpOffBuf *internalBuf;
   PvtcpSock *pvsk = PvtcpGetPvskOrReturn(packet->data64, upperLayerState);
   struct sock *sk = SkFromPvsk(pvsk);
   struct socket *sock = sk->sk_socket;
   unsigned int dataLen = packet->len - sizeof *packet;
   struct msghdr msg = {
      .msg_controllen = 0,
      .msg_control = NULL
   };
   int tmpSize;
   int needSched = 0;

   PvtcpHoldSock(pvsk);
   rc = 0;

   if (!pvsk->peerSockSet || PvskTestFlag(pvsk, PVTCP_OFF_PVSKF_SHUT_WR)) {
      PVTCP_UNLOCK_DISP_DISCARD_VEC();
      goto out;
   }

   tmpSize = (int)COMM_OPF_GET_VAL(packet->flags);
   if (tmpSize) {
      /* It was requested that we update deltaAckSize. */

      tmpSize = 1 << tmpSize;
      CommOS_WriteAtomic(&pvsk->deltaAckSize, tmpSize);
   }

   if (sk->sk_type == SOCK_STREAM) {
      unsigned int queueSize = 0;

      if (!SOCK_OUT_TRYLOCK(pvsk)) {
         if (pvsk->peerSockSet &&
             (sk->sk_state == TCP_ESTABLISHED) &&
             (CommOS_ReadAtomic(&pvsk->queueSize) == 0)) {
            /* Attempt to write directly as many bytes as we can. */

            msg.msg_flags = MSG_DONTWAIT | MSG_NOSIGNAL;
            rc = kernel_sendmsg(sock, &msg, vec, vecLen, dataLen);

            if (rc == -EAGAIN) {
               rc = 0;
            }
            if (rc >= 0) {
               dataLen = rc;
               for (vecOff = 0; vecOff < vecLen; vecOff++) {
                  if (rc >= vec[vecOff].iov_len) {
                     /* Dispose of all fully consumed buffers. */

                     PvtcpBufFree(vec[vecOff].iov_base);
                     rc -= vec[vecOff].iov_len;
                  } else {
                     /* Place partly consumed / unconsumed buffers in queue. */

                     internalBuf =
                        PvtcpOffInternalFromBuf(vec[vecOff].iov_base);
                     BUG_ON(internalBuf == NULL);
                     if (rc > 0) {
                        internalBuf->len -= rc;
                        internalBuf->off += rc;
                        rc = 0;
                     }
                     CommOS_ListAddTail(&pvsk->queue, &internalBuf->link);
                     queueSize += internalBuf->len;
                  }
               }
               if (queueSize > 0) {
                  CommOS_AddReturnAtomic(&pvsk->queueSize, queueSize);
                  needSched = 1;
               }
            } else {
               /*
                * We never close offload sockets unless told by the PV side,
                * or when the comm goes down. Getting out of sync with PV
                * sockets is a dangerously bad idea.
                * This is very likely an EPIPE/ECONNRESET.
                */

               dataLen = 0;
               for ( vecOff = 0; vecOff < vecLen; vecOff++) {
                  PvtcpBufFree(vec[vecOff].iov_base);
               }
            }
            SOCK_OUT_UNLOCK(pvsk);
         } else {
            SOCK_OUT_UNLOCK(pvsk);
            goto enqueueBytes;
         }
      } else {
         /*
          * We enqueue the bytes for aio processing. Note that request
          * level ordering is preserved since we're still under the dispatch
          * lock. However, accessing 'queue' must be protected via
          * the state lock to serialize with aio changes.
          * Note that the struct socket *sock may have been released, but here
          * we only access sk which is held (albeit potentially orphaned).
          */

         CommOSList bufList;

enqueueBytes:
         dataLen = 0;
         if (pvsk->peerSockSet && (sk->sk_state == TCP_ESTABLISHED)) {
            queueSize = 0;
            CommOS_ListInit(&bufList);
            for (vecOff = 0; vecOff < vecLen; vecOff++) {
               internalBuf = PvtcpOffInternalFromBuf(vec[vecOff].iov_base);
               BUG_ON(internalBuf == NULL);
               CommOS_ListAddTail(&bufList, &internalBuf->link);
               queueSize += internalBuf->len;
            }

            if (queueSize > 0) {
               SOCK_STATE_LOCK(pvsk);
               CommOS_ListSpliceTail(&pvsk->queue, &bufList);
               SOCK_STATE_UNLOCK(pvsk);
               CommOS_AddReturnAtomic(&pvsk->queueSize, queueSize);
               needSched = 1;
            }
         } else {
            for ( vecOff = 0; vecOff < vecLen; vecOff++) {
               PvtcpBufFree(vec[vecOff].iov_base);
            }
         }
      }
   } else { /* SOCK_DGRAM || SOCK_RAW */
      struct sockaddr *addr;
      struct sockaddr_in sin;
      struct sockaddr_in6 sin6;
      int addrLen;

      /*
       * Non-stream sockets don't use the send queue, packets are sent
       * directly and they must _not_ be merged.
       */

      if (sk->sk_family == AF_INET) {
         sin.sin_family = AF_INET;
         sin.sin_port = packet->data16;
         addr = (struct sockaddr *)&sin;
         addrLen = sizeof sin;
         sin.sin_addr.s_addr = (unsigned int)packet->data64ex;
         PvtcpTestAndBindLoopbackInet4(pvsk, &sin.sin_addr.s_addr, 0);
      } else { /* AF_INET6 */
         sin6.sin6_family = AF_INET6;
         sin6.sin6_port = packet->data16;
         addr = (struct sockaddr *)&sin6;
         addrLen = sizeof sin6;
         PvtcpTestAndBindLoopbackInet6(pvsk, &packet->data64ex,
                                       &packet->data64ex2, 0);
         PvtcpI6AddrUnpack(&sin6.sin6_addr.s6_addr32[0],
                           packet->data64ex, packet->data64ex2);
      }
      msg.msg_flags = packet->data32 | MSG_DONTWAIT | MSG_NOSIGNAL;
      msg.msg_name = addr;
      msg.msg_namelen = addrLen;

      if (pvsk->peerSockSet) {
         /*
          * Flow-control already done, based on PVTCP_SOCK_SAFE_RCVSIZE, just
          * as with stream sockets. Meaning that we block the senders in the
          * guest (if applicable).
          *
          * The send buffer size was set high enough, at socket creation time,
          * to avoid dropping datagrams during the (non-blocking) write.
          */

         if (vecLen == 0) {
            /*
             * Allow zero-sized datagram sending.
             */

            struct kvec dummy = { .iov_base = NULL, .iov_len = 0 };

            rc = kernel_sendmsg(sock, &msg, &dummy, 0, 0);
            if (rc != dummy.iov_len) {
#if defined(PVTCP_FULL_DEBUG)
               CommOS_Debug(("%s: Dgram [0x%p] sent [%d], expected [%d]\n",
                             __FUNCTION__, sk, rc, dummy.iov_len));
#endif
               if (rc == -EAGAIN) { /* As if lost on the wire. */
                  rc = 0;
               }
            }
         }

         for (vecOff = 0; vecOff < vecLen; vecOff++) {
            rc = kernel_sendmsg(sock, &msg, &vec[vecOff], 1,
                                vec[vecOff].iov_len);
            PvtcpBufFree(vec[vecOff].iov_base);
            if (rc != vec[vecOff].iov_len) {
#if defined(PVTCP_FULL_DEBUG)
               CommOS_Debug(("%s: Dgram [0x%p] sent [%d], expected [%d]\n",
                             __FUNCTION__, sk, rc, vec[vecOff].iov_len));
#endif
               if (rc == -EAGAIN) { /* As if lost on the wire. */
                  rc = 0;
               }
            }
         }

         if (COMM_OPF_TEST_ERR(packet->flags)) {
            /* PV client wants an automatic bind. */

            PvskSetOpFlag(pvsk, PVTCP_OP_BIND);
            PvtcpSchedSock(pvsk);
         }
      } else {
         for ( vecOff = 0; vecOff < vecLen; vecOff++) {
            PvtcpBufFree(vec[vecOff].iov_base);
         }
      }
   }
   CommSvc_DispatchUnlock(channel);

out:
   if (rc < 0) {
      pvsk->err = -rc;
   }
   tmpSize = CommOS_AddReturnAtomic(&pvsk->sentSize, dataLen);
   if ((tmpSize >= CommOS_ReadAtomic(&pvsk->deltaAckSize)) ||
       pvsk->err || needSched) {
      if (CommOS_AddReturnAtomic(&PvtcpOutputAIOSection, 1) == 1) {
         /* OutputAIO() (likely) not running. */

         PvtcpSchedSock(pvsk);
      }
      CommOS_SubReturnAtomic(&PvtcpOutputAIOSection, 1);
   }

   PvtcpPutSock(pvsk);
}


/*
 * AI/O functions called from the main AIO processing function.
 */

/**
 * @brief Processes socket flow control acks and error notifications in an
 *   AIO thread. This function is called with the socket 'in' lock taken.
 * @param[in,out] pvsk socket to process.
 * @param err non-zero if offload was closed, zero otherwise.
 * @sideeffect May resume PV socket sending or raise errors.
 */

void
PvtcpFlowAIO(PvtcpSock *pvsk,
             int err)
{
   CommPacket packet = { .flags = 0 };
   unsigned long long timeout;
   int tmpSize;

   COMM_OPF_CLEAR_ERR(packet.flags);
   packet.data32 = PVTCP_FLOW_OP_INVALID_SIZE;
   if (pvsk->err || err) {
      COMM_OPF_SET_ERR(packet.flags);
      packet.data32ex = !pvsk->err ? 0 : xchg(&pvsk->err, 0);
      if (!packet.data32ex) {
         packet.data32ex = -err;
      }
#if defined(PVTCP_FULL_DEBUG)
      CommOS_Debug(("%s: Sending socket error [%u] on [0x%p -> 0x%0x].\n",
                    __FUNCTION__, packet.data32ex, pvsk,
                    (unsigned)(pvsk->peerSock)));
#endif
   } else {
      SOCK_STATE_LOCK(pvsk);
      tmpSize = CommOS_ReadAtomic(&pvsk->deltaAckSize);
      if (CommOS_ReadAtomic(&pvsk->sentSize) >= tmpSize) {
         if ((SkFromPvsk(pvsk)->sk_type != SOCK_STREAM) &&
             !sock_writeable(SkFromPvsk(pvsk))) {
            /* Don't send dgram flow op until WriteSpaceCB tells us to do so. */

            packet.data32 = PVTCP_FLOW_OP_INVALID_SIZE;
         } else {
            packet.data32 = CommOS_ReadAtomic(&pvsk->sentSize);
            CommOS_WriteAtomic(&pvsk->sentSize, 0);
            if (tmpSize > (1 << (PVTCP_SOCK_SMALL_ACK_ORDER + 1))) {
               tmpSize >>= 1;
               CommOS_WriteAtomic(&pvsk->deltaAckSize, tmpSize);
            }
         }
      }
      SOCK_STATE_UNLOCK(pvsk);
      packet.data32ex = 0;
   }

   if (((packet.data32 != PVTCP_FLOW_OP_INVALID_SIZE) ||
        COMM_OPF_TEST_ERR(packet.flags)) &&
       pvsk->peerSockSet) {
      packet.len = sizeof packet;
      packet.opCode = PVTCP_OP_FLOW;
      packet.data64 = pvsk->peerSock;
      timeout = COMM_MAX_TO;
      CommSvc_Write(pvsk->channel, &packet, &timeout);
   }
}


/**
 * @brief Processes queued socket output in an AIO thread. This function is
 *   called with the socket 'out' lock taken.
 * @param[in,out] pvsk socket to process.
 * @sideeffect Changes send size/capacity ratio.
 */

void
PvtcpOutputAIO(PvtcpSock *pvsk)
{
   struct sock *sk;
   struct socket *sock;
   PvtcpOffBuf *internalBuf;
   PvtcpOffBuf *tmp;
   CommOSList queue;
#define VEC_SIZE 32
   struct kvec vec[VEC_SIZE];
   unsigned int vecLen;
   unsigned int dataLen;
   struct msghdr msg = {
      .msg_controllen = 0,
      .msg_control = NULL,
      .msg_flags = MSG_DONTWAIT | MSG_NOSIGNAL
   };
   int queueDelta = 0;
   int done = 0;
   int rc;

   sk = SkFromPvsk(pvsk);
   if (!sk) {
      /* This is an error socket, we don't process it. */

      return;
   }

   sock = sk->sk_socket;

again:
   CommOS_AddReturnAtomic(&PvtcpOutputAIOSection, 1);
   while (!done && CommOS_ReadAtomic(&pvsk->queueSize) > 0) {
      /* Note: only stream sockets can have a positive send queue size.
       * Similar to PvtcpIoOp: we must check if sock (struct socket *) is
       * still valid.
       */

      /* Take the current queue private. */

      SOCK_STATE_LOCK(pvsk);
      queue = pvsk->queue;
      if (CommOS_ListEmpty(&queue)) {
         SOCK_STATE_UNLOCK(pvsk);
         return;
      }
      queue.next->prev = &queue;
      queue.prev->next = &queue;
      CommOS_ListInit(&pvsk->queue);
      SOCK_STATE_UNLOCK(pvsk);

      vecLen = 0;
      dataLen = 0;

      if (sk->sk_state == TCP_ESTABLISHED) {
         CommOS_ListForEach(&queue, internalBuf, link) {
            if (vecLen == VEC_SIZE) {
               break;
            }
            vec[vecLen].iov_base = PvtcpOffBufFromInternalOff(internalBuf);
            vec[vecLen].iov_len = internalBuf->len;
            dataLen += internalBuf->len;
            vecLen++;
         }

         rc = kernel_sendmsg(sock, &msg, vec, vecLen, dataLen);

         if (rc == -EAGAIN) {
            rc = 0;
         }
         if (rc >= 0) {
            /* If we wrote anything, dispose of the buffers in question. */

            queueDelta = rc;
            if (queueDelta > 0) {
               CommOS_ListForEachSafe(&queue, internalBuf, tmp, link) {
                  if (rc >= internalBuf->len) {
                     rc -= internalBuf->len;
                     CommOS_ListDel(&internalBuf->link);
                     PvtcpBufFree(PvtcpOffBufFromInternal(internalBuf));
                  } else {
                     internalBuf->len -= rc;
                     internalBuf->off += rc;
                     break;
                  }
               }
            }
            if (!CommOS_ListEmpty(&queue)) {
               /* Add the remaining bytes to the beginning of the queue. */

               SOCK_STATE_LOCK(pvsk);
               CommOS_ListSplice(&pvsk->queue, &queue);
               SOCK_STATE_UNLOCK(pvsk);
            }
            if (queueDelta == 0) {
               /* Bail out if no bytes written, WriteSpaceCB() will resched. */

               done = 1;
               break;
            }
            CommOS_AddReturnAtomic(&pvsk->sentSize, queueDelta);
            CommOS_SubReturnAtomic(&pvsk->queueSize, queueDelta);
         } else {
            /*
             * Very likely, this is due to the socket being closed, so fine.
             */

            goto discardOutput;
         }
      } else {
         /* Dispose of all buffers in the queue and mark it empty. */

discardOutput:
         if (!CommOS_ListEmpty(&queue)) {
            CommOS_ListForEachSafe(&queue, internalBuf, tmp, link) {
               CommOS_ListDel(&internalBuf->link);
               PvtcpBufFree(PvtcpOffBufFromInternal(internalBuf));
            }
         }
         CommOS_WriteAtomic(&pvsk->queueSize, 0);
         break;
      }
   }
   if (CommOS_SubReturnAtomic(&PvtcpOutputAIOSection, 1) > 0) {
      if (!done) {
         goto again;
      }
   }

   if (PvskTestFlag(pvsk, PVTCP_OFF_PVSKF_SHUT_WR)) {
      kernel_sock_shutdown(sock, SHUT_WR);
      PvskSetFlag(pvsk, PVTCP_OFF_PVSKF_SHUT_WR, 0);
   }
#undef VEC_SIZE
}


/**
 * @brief Processes socket input in an AIO thread. This function is
 *   called with the socket 'in' lock taken.
 * @param[in,out] pvsk socket to process.
 * @param[in,out] perCpuBuf per-cpu socket read buffer.
 * @return zero if eof was not detected, non-zero otherwise.
 * @sideeffect Changes receive size/capacity ratio.
 */

int
PvtcpInputAIO(PvtcpSock *pvsk,
              void *perCpuBuf)
{
   struct sock *sk;
   struct socket *sock;
   int err = 0;
   CommPacket packet = {
      .opCode = PVTCP_OP_IO
   };
   unsigned long long timeout;

   sk = SkFromPvsk(pvsk);
   if (!sk) {
      /* IO processing is skipped on socket create-error sockets. */

      return -1;
   }
   if (!perCpuBuf) {
      /* No read buffer. */

      return -1;
   }

   sock = sk->sk_socket;
   packet.data64 = pvsk->peerSock;
   COMM_OPF_CLEAR_ERR(packet.flags);

   if (sk->sk_state == TCP_LISTEN) {
      /* Process stream listen 'input'. */

      packet.len = sizeof packet;
      packet.data16 = sk->sk_ack_backlog;
      timeout = COMM_MAX_TO;
      if (pvsk->peerSockSet) {
         CommSvc_Write(pvsk->channel, &packet, &timeout);
         CommOS_Debug(("%s: Listen sock [0x%p] 'ack_backlog' [%hu].\n",
                       __FUNCTION__, sk, packet.data16));
      }
   } else {
      /* Common path for both stream and datagram sockets. */

      int rc;
      int tmpSize;
      struct kvec vec[2];
      void *ioBuf = perCpuBuf;
      struct kvec *inVec;
      unsigned int inVecLen;
      unsigned int iovOffset = 0;
      unsigned int inputSize = 0;
      unsigned int coalescingSize = PVTCP_SOCK_RCVSIZE >> 2;
      struct sockaddr_in sin = { .sin_family = AF_INET };
      struct sockaddr_in6 sin6 = { .sin6_family = AF_INET6 };
      struct msghdr msg = {
         .msg_controllen = 0,
         .msg_control = NULL,
         .msg_flags = MSG_DONTWAIT
      };
      int tmpFlags = msg.msg_flags;
      PvtcpDgramPseudoHeader dgramHeader;

      tmpSize = CommOS_ReadAtomic(&pvsk->rcvdSize);
      while ((tmpSize < PVTCP_SOCK_SAFE_RCVSIZE) && pvsk->peerSockSet) {
         if (ioBuf != perCpuBuf) {
            LargeDgramBufPut(ioBuf);
            ioBuf = perCpuBuf;
         }
         vec[0].iov_base = (char *)ioBuf;

         if (sk->sk_type == SOCK_STREAM) {
            if (PvskTestFlag(pvsk, PVTCP_OFF_PVSKF_SHUT_RD)) {
               break;
            }

            msg.msg_name = NULL;
            msg.msg_namelen = 0;
            vec[0].iov_len = PVTCP_SOCK_STREAM_BUF_SIZE;
         } else { /* SOCK_DGRAM || SOCK_RAW */
            if (sk->sk_family == AF_INET) {
               msg.msg_name = &sin;
               msg.msg_namelen = sizeof sin;
            } else {
               msg.msg_name = &sin6;
               msg.msg_namelen = sizeof sin6;
            }

            /*
             * Check if datagram larger than the per cpu buffer; if so,
             * allocate a large enough buffer. This should happen quite
             * rarely, as well-behaved applications don't rely on IP
             * fragmentation to accommodate large sizes.
             */

            vec[0].iov_len = 1;
            msg.msg_flags |= (MSG_PEEK | MSG_TRUNC);
            rc = kernel_recvmsg(sock, &msg, vec, 1, 1, msg.msg_flags);
            if (rc < 0) {
               break;
            }
            msg.msg_flags = tmpFlags;
            if (rc > PVTCP_SOCK_DGRAM_BUF_SIZE) {
               /*
                * Track large datagram allocations, whether allocation succeeds
                * or not. No need for atomic overhead, approximating is OK.
                */

               pvtcpOffDgramAllocations++;
               ioBuf = LargeDgramBufGet(rc);
               if (!ioBuf) {
                  /*
                   * We reset it to the per-cpu buffer such that we can still
                   * consume the datagram in the next recvmsg, which will set
                   * MSG_TRUNC so we won't put it on the channel.
                   */

                  CommOS_Debug(("%s: Dropping datagram (alloc failure)!\n",
                                __FUNCTION__));
                  ioBuf = perCpuBuf;
                  vec[0].iov_len = PVTCP_SOCK_DGRAM_BUF_SIZE;
               } else {
                  vec[0].iov_len = rc;
               }
            } else {
               vec[0].iov_len = PVTCP_SOCK_DGRAM_BUF_SIZE;
            }
            vec[0].iov_base = (char *)ioBuf;
         }

         rc = kernel_recvmsg(sock, &msg, vec, 1, vec[0].iov_len, msg.msg_flags);
         if (rc < 0) {
            break;
         }

         if ((rc == 0) && (sk->sk_type == SOCK_STREAM)) {
            PvskSetFlag(pvsk, PVTCP_OFF_PVSKF_SHUT_RD, 1);
            err = -ECONNRESET;
            break;
         }

         if (msg.msg_flags & MSG_TRUNC) {
            continue;
         }

         inputSize += rc;
         tmpSize = CommOS_AddReturnAtomic(&pvsk->rcvdSize, rc);
         if (tmpSize >= PVTCP_SOCK_LARGE_ACK_WM) {
            COMM_OPF_SET_VAL(packet.flags, PVTCP_SOCK_LARGE_ACK_ORDER);
         } else {
            COMM_OPF_SET_VAL(packet.flags, 0);
         }

         if (sk->sk_type == SOCK_STREAM) {
            vec[0].iov_base = ioBuf;
            vec[0].iov_len = rc;
            inVecLen = 1;
            packet.len = sizeof packet + rc;
         } else { /* SOCK_DGRAM || SOCK_RAW */
            if (sk->sk_family == AF_INET) {
               dgramHeader.d0 = (unsigned long long)sin.sin_port;
               PvtcpResetLoopbackInet4(pvsk, &sin.sin_addr.s_addr);
               dgramHeader.d1 = (unsigned long long)sin.sin_addr.s_addr;
            } else { /* AF_INET6 */
               dgramHeader.d0 = (unsigned long long)sin6.sin6_port;
               PvtcpResetLoopbackInet6(pvsk, &sin6.sin6_addr);
               PvtcpI6AddrPack(&sin6.sin6_addr.s6_addr32[0],
                               &dgramHeader.d1, &dgramHeader.d2);
            }
            vec[0].iov_base = &dgramHeader;
            vec[0].iov_len = sizeof dgramHeader;
            vec[1].iov_base = ioBuf;
            vec[1].iov_len = rc;
            inVecLen = 2;
            packet.len = sizeof packet + sizeof dgramHeader + rc;
         }

         inVec = vec;
         timeout = COMM_MAX_TO;
         rc = CommSvc_WriteVec(pvsk->channel, &packet,
                               &inVec, &inVecLen, &timeout, &iovOffset);
         if (rc != packet.len) {
            CommOS_Log(("%s: BOOG -- WROTE INCOMPLETE PACKET [%u->%d]!\n",
                        __FUNCTION__, packet.len, rc));
            break;
         }

         /*
          * If the write failed, we could print a warning. But if this
          * happened, the comm channel went down.
          */
         if (inputSize >= coalescingSize) {
            PvtcpSchedSock(pvsk); /* We must schedule ourselves back in. */
            break;
         }
      }
      if (ioBuf != perCpuBuf) {
         LargeDgramBufPut(ioBuf);
      }
   }
   return err;
}
