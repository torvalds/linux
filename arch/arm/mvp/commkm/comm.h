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
 * @brief Communication functions based on queue pair transport APIs.
 *
 * Comm is a shared memory-based mechanism that facilitates the implementation
 * of kernel components that require host-to-guest, or guest-to-guest
 * communication.
 * This facility assumes the availability of a minimal shared memory queue pair
 * implementation, such as MVP queue pairs or VMCI queue pairs. The latter must
 * provide primitives for queue pair creation and destruction, and reading and
 * writing from/to queue pairs.
 * Comm assumes that the queue pair (transport) layer is not concerned with
 * multi-threading, locking or flow control, and does not require such features.
 */

#ifndef _COMM_H_
#define _COMM_H_

#define INCLUDE_ALLOW_MODULE
#define INCLUDE_ALLOW_PV
#define INCLUDE_ALLOW_GPL
#include "include_check.h"

#include "comm_os.h"
#include "comm_transp.h"


/* Default/maximum Comm timeouts (in milliseconds). */
#define COMM_MAX_TO 60000ULL
#define COMM_MAX_TO_UNINT (COMM_MAX_TO + 1)

#define COMM_OPF_SET_ERR(flags)   ((flags) |= 128)
#define COMM_OPF_CLEAR_ERR(flags) ((flags) &= 127)
#define COMM_OPF_TEST_ERR(flags)  ((flags) &  128)

#define COMM_OPF_SET_VAL(flags, val) ((flags) |= ((val) & 127))
#define COMM_OPF_GET_VAL(flags)      ((flags) & 127)

/**
 * Packet (header) structure.
 * NB: Do not change this structure, especially the first three fields; there
 *     will be consequences. It may be extended, but it's not recommended: all
 *     operations carry this header, so it's better kept in its minimal form.
 */

typedef struct CommPacket {
   unsigned int len;                        // Total length
   unsigned char flags;                     // Operation flags
   unsigned char opCode;                    // Operation to call
   unsigned short data16;                   // Auxiliary data
   unsigned long long data64;
   unsigned long long data64ex;
   union {
      struct {
         unsigned int data32;
         unsigned int data32ex;
      };
      unsigned long long data64ex2;
   };
} CommPacket;


/* Opaque structure representing a communication channel. */

struct CommChannelPriv;
typedef struct CommChannelPriv *CommChannel;


/* Input operations associated with a comm channel. */

typedef void (*CommOperationFunc)(CommChannel channel,
                                  void *state,
                                  CommPacket *packet,
                                  struct kvec *vec,
                                  unsigned int vecLen);


/* Helper macros */

#define COMM_DEFINE_OP(funcName) \
void                             \
funcName(CommChannel channel,    \
         void *state,            \
         CommPacket *packet,     \
         struct kvec *vec,       \
         unsigned int vecLen)


/* Comm-based implementations. */

typedef struct CommImpl {
   struct module *owner;
   int (*checkArgs)(CommTranspInitArgs *transpArgs);
   void *(*stateCtor)(CommChannel channel);
   void (*stateDtor)(void *state);
   void *(*dataAlloc)(unsigned int dataLen);
   void (*dataFree)(void *data);
   const CommOperationFunc *operations;
   void (*closeNtf)(void *closeNtfData,
                    const CommTranspInitArgs *transpArgs,
                    int inBH);
   void *closeNtfData;
   void (*activateNtf)(void *activateNtfData,
                       CommChannel channel);
   void *activateNtfData;
   unsigned long long openAtMillis;
   unsigned long long openTimeoutAtMillis;
   CommTranspID ntfCenterID;
} CommImpl;


int Comm_Init(unsigned int maxChannels);
int Comm_Finish(unsigned long long *timeoutMillis);
int Comm_RegisterImpl(const CommImpl *impl);
void Comm_UnregisterImpl(const CommImpl *impl);
int Comm_IsActive(CommChannel channel);
CommTranspInitArgs Comm_GetTranspInitArgs(CommChannel channel);
void *Comm_GetState(CommChannel channel);
int Comm_Dispatch(CommChannel channel);
unsigned int Comm_DispatchAll(void);
void Comm_Put(CommChannel channel);
void Comm_DispatchUnlock(CommChannel channel);
int Comm_Lock(CommChannel channel);
void Comm_Unlock(CommChannel channel);
int Comm_Zombify(CommChannel channel, int inBH);

int
Comm_Alloc(const CommTranspInitArgs *transpArgs,
           const CommImpl *impl,
           int inBH,
           CommChannel *newChannel);


int
Comm_Write(CommChannel channel,
           const CommPacket *packet,
           unsigned long long *timeoutMillis);

int
Comm_WriteVec(CommChannel channel,
              const CommPacket *packet,
              struct kvec **vec,
              unsigned int *vecLen,
              unsigned long long *timeoutMillis,
              unsigned int *iovOffset);

unsigned int Comm_RequestInlineEvents(CommChannel channel);
unsigned int Comm_ReleaseInlineEvents(CommChannel channel);

#endif // _COMM_H_
