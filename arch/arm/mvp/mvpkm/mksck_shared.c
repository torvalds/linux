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

#include "mvp.h"
#include "mksck_shared.h"

/**
 * @file
 *
 * @brief The mksck shared area functions used by the monitor and the
 * kernel extension.
 *
 */

/**
 * @brief try to locate a socket using an address.
 * @param mksckPage which shared page to look on.
 *          ASSUMED: locked for shared access
 * @param addr address to check
 * @return pointer to mksck page with addr.
 *          NULL if not found
 */
Mksck *
MksckPage_GetFromAddr(MksckPage *mksckPage, Mksck_Address addr)
{
   Mksck *mksck = mksckPage->sockets;
   uint32 ii;

   ASSERT(addr.vmId == mksckPage->vmId);

   for (ii = mksckPage->numAllocSocks; ii--; mksck++) {
      if ((ATOMIC_GETO(mksck->refCount) != 0) &&
          (mksck->addr.addr == addr.addr)) {
         return mksck;
      }
   }
   return NULL;
}

/**
 * @brief Close a monitor socket.
 *
 * @param mksck pointer to the socket control block
 */
void
Mksck_CloseCommon(Mksck *mksck)
{
   /*
    * If a peer was connected, release the peer.
    */
   Mksck_DisconnectPeer(mksck);

   /*
    * Signal senders that this socket won't be read anymore.
    */
   while (Mutex_Lock(&mksck->mutex, MutexModeEX) < 0);
   mksck->shutDown = MKSCK_SHUT_WR | MKSCK_SHUT_RD;
   Mutex_UnlWake(&mksck->mutex, MutexModeEX, MKSCK_CVAR_ROOM, true);

   /*
    * Decrement reference count because it was set to 1 when opened.  It could
    * still be non-zero after this if some other thread is currently sending to
    * this socket.
    */
   Mksck_DecRefc(mksck);
}


/**
 * @brief decrement socket reference count, free if it goes zero.  Also do a
 *        dmb first to make sure all activity on the struct is finished before
 *        decrementing the ref count.
 * @param mksck socket
 */
void
Mksck_DecRefc(Mksck *mksck)
{
   uint32 oldRefc;

   DMB();
   do {
      while ((oldRefc = ATOMIC_GETO(mksck->refCount)) == 1) {

         MksckPage *mksckPage = Mksck_ToSharedPage(mksck);

         /*
          * Socket refcount is going zero on a socket that locks mksckPage in.
          * Lock shared page exclusive to make sure no one is trying to look
          * for this socket, thus preventing socket's refcount from being
          * incremented non-zero once we decrement it to zero.
          */

         /*
          * Lock failed probably because of an interrupt.  Keep trying
          * to lock until we succeed.
          */
          while (Mutex_Lock(&mksckPage->mutex, MutexModeEX) < 0);

         /*
          * No one is doing any lookups, so set refcount zero.
          */
         if (ATOMIC_SETIF(mksck->refCount, 0, 1)) {
#if 0
            /**
             * @knownjira{MVP-1349}
             * The standard Log is not yet implemented in the kernel space.
             */
            KNOWN_BUG(MVP-1349);
            PRINTK(KERN_INFO "Mksck_DecRefc: %08X shutDown %u, foundEmpty %u,"
                   " foundFull %u, blocked %u\n",
                  mksck->addr.addr, mksck->shutDown,
                  mksck->foundEmpty, mksck->foundFull,
                  ATOMIC_GETO(mksck->mutex.blocked));
#endif

            /*
             * Sockets can't have connected peers by the time their
             * refc hits 0.  The owner should have cleaned that up by
             * now.
             */
            ASSERT(mksck->peer == 0);

            /*
             * Successfully set to zero, release mutex and decrement
             * shared page ref count as it was incremented when the
             * socket was opened. This may free the shared page.
             */
            Mutex_Unlock(&mksckPage->mutex, MutexModeEX);
            MksckPage_DecRefc(mksckPage);
            return;
         }

         /*
          * Someone incremented refcount just before we locked the mutex, so
          * try it all again.
          */
         Mutex_Unlock(&mksckPage->mutex, MutexModeEX);
      }

      /*
       * Not going zero or doesn't lock mksckPage, simple decrement.
       */
      ASSERT(oldRefc != 0);
   } while (!ATOMIC_SETIF(mksck->refCount, oldRefc - 1, oldRefc));
}


/**
 * @brief Find an unused port.
 * @param mksckPage which shared page to look in.
 *                    Locked for exclusive access
 * @param port if not MKSCK_PORT_UNDEF test only this port
 * @return port allocated or MKSCK_PORT_UNDEF if none was found
 */
Mksck_Port
MksckPage_GetFreePort(MksckPage *mksckPage, Mksck_Port port)
{
   Mksck_Address addr = { .addr = Mksck_AddrInit(mksckPage->vmId, port) };
   uint32 ii;

   if (port == MKSCK_PORT_UNDEF) {
      for (ii = 0; ii<MKSCK_SOCKETS_PER_PAGE; ii++) {

         /*
          * Find an unused local socket number.
          */
         addr.port = mksckPage->portStore--;
         if (!addr.port) {

            /*
             * Wrapped around, reset portStore
             */
            mksckPage->portStore = MKSCK_PORT_HIGH;
         }

         if (!MksckPage_GetFromAddr(mksckPage, addr)) {
            return addr.port;
         }
      }

   } else if (!MksckPage_GetFromAddr(mksckPage, addr)) {
      return addr.port;
   }

   return MKSCK_PORT_UNDEF;
}

/**
 * @brief Find an unused slot in the sockets[] array and allocate it.
 * @param mksckPage which shared page to look in.
 *                    Locked for exclusive access
 * @param addr what local address to assign to the socket
 * @return NULL: no slots available <br>
 *         else: pointer to allocated socket
 */
Mksck *
MksckPage_AllocSocket(MksckPage *mksckPage, Mksck_Address addr)
{
   Mksck *mksck;
   uint32 i;

   for (i = 0; (offsetof(MksckPage, sockets[i+1]) <= MKSCKPAGE_SIZE) &&
               (i < 8 * sizeof mksckPage->wakeHostRecv) &&
               (i < 8 * sizeof mksckPage->wakeVMMRecv); i ++) {
      mksck = &mksckPage->sockets[i];
      if (ATOMIC_GETO(mksck->refCount) == 0) {
         ATOMIC_SETV(mksck->refCount, 1);
         mksck->addr          = addr;
         mksck->peerAddr.addr = MKSCK_ADDR_UNDEF;
         mksck->peer          = NULL;
         mksck->index         = i;
         mksck->write         = 0;
         mksck->read          = 0;
         mksck->shutDown      = 0;
         mksck->foundEmpty    = 0;
         mksck->foundFull     = 0;
         ATOMIC_SETV(mksck->mutex.blocked, 0);
         mksck->rcvCBEntryMVA = 0;
         mksck->rcvCBParamMVA = 0;

         if (mksckPage->numAllocSocks < ++ i) {
            mksckPage->numAllocSocks = i;
         }

         return mksck;
      }
   }
   return NULL;
}


/**
 * @brief increment read index over the packet just read
 * @param mksck socket packet was read from.
 *                Locked for exclusive access
 * @param read current value of mksck->read
 * @param dg datagram at current mksck->read
 * @return with mksck->read updated to next packet <br>
 *         false: buffer not empty <br>
 *          true: buffer now empty
 */
_Bool
Mksck_IncReadIndex(Mksck *mksck, uint32 read, Mksck_Datagram *dg)
{
   ASSERT(read == mksck->read);
   ASSERT((void *)dg == (void *)&mksck->buff[read]);

   read += MKSCK_DGSIZE(dg->len);
   if ((read > mksck->write) && (read >= mksck->wrap)) {
      ASSERT(read == mksck->wrap);
      read = 0;
   }
   mksck->read = read;

   return read == mksck->write;
}


/**
 * @brief find index in buffer that has enough room for a packet
 * @param mksck socket message is being sent to.
 *                Locked for exclusive access
 * @param needed room needed, including dg header and rounded up
 * @return MKSCK_FINDSENDROOM_FULL: not enough room available <br>
 *                             else: index in mksck->buff for packet
 */
uint32
Mksck_FindSendRoom(Mksck *mksck, uint32 needed)
{
   uint32 read, write;

   /*
    * We must leave at least one byte unused so receiver can distinguish full
    * from empty.
    */
   read  = mksck->read;
   write = mksck->write;
   if (write == read) {
      if (needed < MKSCK_BUFSIZE) {
         mksck->read  = 0;
         mksck->write = 0;
         return 0;
      }
   } else if (write < read) {
      if (write + needed < read) {
         return write;
      }
   } else {
      if (write + needed < MKSCK_BUFSIZE) {
         return write;
      }
      if ((write + needed == MKSCK_BUFSIZE) && (read > 0)) {
         return write;
      }
      if (needed < read) {
         mksck->wrap  = write;
         mksck->write = 0;
         return 0;
      }
   }

   return MKSCK_FINDSENDROOM_FULL;
}


/**
 * @brief increment read index over the packet just written
 * @param mksck socket packet was written to.
 *                Locked for exclusive access
 * @param write as returned by @ref Mksck_FindSendRoom
 * @param needed as passed to @ref Mksck_FindSendRoom
 * @return with mksck->write updated to next packet
 */
void
Mksck_IncWriteIndex(Mksck *mksck, uint32 write, uint32 needed)
{
   ASSERT(write == mksck->write);
   write += needed;
   if (write >= MKSCK_BUFSIZE) {
      ASSERT(write == MKSCK_BUFSIZE);
      mksck->wrap = MKSCK_BUFSIZE;
      write = 0;
   }
   ASSERT(write != mksck->read);
   mksck->write = write;
}
