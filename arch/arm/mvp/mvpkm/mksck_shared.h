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
 * @file
 *
 * @brief The monitor-kernel socket interface shared area definitions.
 */

#ifndef _MKSCK_SHARED_H
#define _MKSCK_SHARED_H

#define INCLUDE_ALLOW_MVPD
#define INCLUDE_ALLOW_VMX
#define INCLUDE_ALLOW_MODULE
#define INCLUDE_ALLOW_MONITOR
#define INCLUDE_ALLOW_GPL
#include "include_check.h"

/*
 * Allocated MksckPages are stored in an array of size
 * MKSCK_MAX_SHARES. The vmid and the slot index of a shared page is
 * not unrelated: vmid = idx%MKSCK_MAX_SHARES.
 */
#define MKSCK_MAX_SHARES_LOG2  4 // 16: one per VM + one per VCPU
#define MKSCK_MAX_SHARES       (1U << MKSCK_MAX_SHARES_LOG2)
#define MKSCK_VMID2IDX(idx)    ((idx)%MKSCK_MAX_SHARES)
#define MKSCK_TGID2VMID(tgid)  (((((tgid)<<1)^((tgid)>>15))&0xfffe)|1)
/*
 * The size of a shared page determines how many sockets can be open
 * concurrently.
 */
#define MKSCKPAGE_TOTAL        8 // number of shared pages
#define MKSCKPAGE_SIZE         (PAGE_SIZE * MKSCKPAGE_TOTAL)
#define MKSCK_SOCKETS_PER_PAGE ((MKSCKPAGE_SIZE-offsetof(MksckPage, sockets[0])) / \
                                sizeof(Mksck))

/*
 * Individual datagrams are aligned on a MKSCK_ALIGNMENT byte boundary
 * in the data receive area of a socket.
 */
#define MKSCK_ALIGNMENT        8 // data packet alignment
#define MKSCK_ALIGN(x)         MVP_ALIGN(x, MKSCK_ALIGNMENT)
#define MKSCK_DGSIZE(len)      offsetof(Mksck_Datagram, data[MKSCK_ALIGN(len)])
#define MKSCK_BUFSIZE          MKSCK_DGSIZE(MKSCK_XFER_MAX + 1)

/*
 * Conditional variables for sleeping on.
 */
#define MKSCK_CVAR_ROOM        0 // senders waiting for room for message
#define MKSCK_CVAR_FILL        1 // receivers waiting for a message to fetch

#define MKSCK_FINDSENDROOM_FULL 0xFFFFFFFFU

/*
 * Shutdown bits
 */
#define MKSCK_SHUT_WR          (1 << 0)   // socket can't send data anymore
#define MKSCK_SHUT_RD          (1 << 1)   // socket can't receive data anymore

typedef struct Mksck Mksck;
typedef struct Mksck_Datagram Mksck_Datagram;
typedef struct MksckPage MksckPage;

#include "atomic.h"
#include "mksck.h"
#include "mmu_defs.h"
#include "mutex.h"
#include "arm_inline.h"

/**
 * @brief Monitor-kernel socket datagram structure
 */
struct Mksck_Datagram {
   Mksck_Address fromAddr;   ///< source address
   uint32        len   : 16; ///< length of the data
   uint32        pad   : 3;  ///< padding between untyped message and mpn
                             ///< array.
   uint32        pages : 13; ///< number of pages in mpn array
   uint8         data[1]     ///< start of the data
         __attribute__((aligned(MKSCK_ALIGNMENT)));
};

/**
 * @brief one particular socket's shared page data.
 */
struct Mksck {
   AtmUInt32 refCount;         ///< when zero, struct is free
                               ///< ... increment only with mksckPage->mutex
                               ///< ... decrement at any time
   Mksck_Address addr;         ///< this socket's address if open
                               ///< ... MKSCK_ADDR_UNDEF if closed
                               ///< ... open only with mksckPage->mutex
   Mksck_Address peerAddr;     ///< peer's address if connected
                               ///< ... MKSCK_ADDR_UNDEF if not
   struct Mksck *peer;         ///< connected peer's ptr or NULL if not
                               ///< ... ptr is MVA for monitor sockets and
                               ///< ... HKVA for sockets of host processes
                               ///< ... holds ref count on target socket
   uint32 index;               ///< index of this socket in page

                               ///< empty ring indicated by read == write
                               ///< ring never completely fills, always at
                               ///< least room for one more byte so we can tell
                               ///< empty from full

   uint32 write;               ///< index within buff to insert next data
                               ///< ... always < MKSCK_BUFSIZE
   uint32 read;                ///< index within buff to remove next data
                               ///< ... always < MKSCK_BUFSIZE
   uint32 wrap;                ///< current wrapping point
                               ///< ... valid only whenever write < read
   uint32 shutDown;            ///< MKSCK_SHUT_RD, MKSCK_SHUT_WR bitfield
   uint32 foundEmpty;          ///< number of times a receive has blocked
   uint32 foundFull;           ///< number of times a send has blocked
   Mutex mutex;                ///< locks the ring buffer
   MVA rcvCBEntryMVA;          ///< monitor's receive callback entrypoint
   MVA rcvCBParamMVA;          ///< monitor's receive callback parameter
   uint8 buff[MKSCK_BUFSIZE]   ///< data going TO this socket
         __attribute__((aligned(MKSCK_ALIGNMENT)));
};


/**
 * @brief the shared page of an address domain (vmId)
 */
struct MksckPage {
   _Bool isGuest;         ///< the page belongs to a monitor/guest
   uint32 tgid;           ///< thread group id if isGuest=true
                          ///< undefined otherwise
   volatile HKVA vmHKVA;  ///< host side local data structure for vm
   AtmUInt32 refCount;    ///< page cannot be freed unless this is zero
                          ///< ... increment only with mksckPageListLock
                          ///< ... decrement at any time
                          ///< ... initialized to 1 for wsp->mksckPage* pointers
   uint32 wakeHostRecv;   ///< bitmask of sockets[] to be woken for receive
                          ///< ... access from VCPU thread only
   AtmUInt32 wakeVMMRecv; ///< likewise for monitor receive callbacks
   Mutex mutex;           ///< locks list of open sockets
   Mksck_VmId vmId;       ///< hostId or guestId these sockets are for
   Mksck_Port portStore;  ///< used to assign ephemeral port numbers
   uint32 numAllocSocks;  ///< number of elements in sockets[] array
   Mksck sockets[1];      ///< array of sockets (to fill MKSCKPAGE_SIZE)
};

MksckPage *MksckPage_GetFromVmId(Mksck_VmId vmId);
Mksck_Port MksckPage_GetFreePort(MksckPage *mksckPage, Mksck_Port port);
Mksck     *MksckPage_GetFromAddr(MksckPage *mksckPage, Mksck_Address addr);
Mksck     *MksckPage_AllocSocket(MksckPage *mksckPage, Mksck_Address addr);
void       MksckPage_DecRefc(MksckPage *mksckPage);

void       Mksck_DecRefc(Mksck *mksck);
void       Mksck_CloseCommon(Mksck *mksck);
_Bool      Mksck_IncReadIndex(Mksck *mksck, uint32 read, Mksck_Datagram *dg);
uint32     Mksck_FindSendRoom(Mksck *mksck, uint32 needed);
void       Mksck_IncWriteIndex(Mksck *mksck, uint32 write, uint32 needed);
void       Mksck_DisconnectPeer(Mksck *mksck);


/**
 * @brief determine which shared page a given socket is on
 *        Note that this process does not rely on any directory.
 * @param mksck pointer to socket
 * @return pointer to shared page
 */
static inline MksckPage *
Mksck_ToSharedPage(Mksck *mksck)
{
   return (MksckPage*)((char*)(mksck - mksck->index)
                       - offsetof(MksckPage, sockets));
}
#endif
