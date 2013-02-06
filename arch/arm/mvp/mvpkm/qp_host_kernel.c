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
 *  @file
 *
 *  @brief MVP host kernel implementation of the queue pairs API
 *
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/highmem.h>
#include <linux/slab.h>

#include "mvp.h"
#include "mvpkm_kernel.h"
#include "qp.h"
#include "qp_host_kernel.h"

static QPHandle   queuePairs[QP_MAX_QUEUE_PAIRS];
static QPListener listeners[QP_MAX_LISTENERS];

/*
 * Protect listeners and queuePairs.
 */
static DEFINE_MUTEX(qpLock);

#define QPLock()    mutex_lock(&qpLock)
#define QPUnlock()  mutex_unlock(&qpLock)

/**
 * @brief Map a vector of pages into virtually contiguous kernel space
 * @param vm this vm's vm struct
 * @param base base machine page number that lists pages to map
 * @param nrPages number of pages to map
 * @param[out] qp handle to qp to set up
 * @param[out] hkva virtual address mapping
 * @return QP_SUCCESS on success, error code otherwise. Mapped address
 *      is returned in hkva
 */

static int32
MapPages(MvpkmVM *vm,
         MPN base,
         uint32 nrPages,
         QPHandle *qp,
         HKVA *hkva)
{
   HKVA *va;
   uint32 i;
   uint32 rc;
   struct page *basepfn = pfn_to_page(base);
   struct page **pages;

   BUG_ON(!vm); // this would be very bad.

   if (!hkva) {
      return QP_ERROR_INVALID_ARGS;
   }

   pages = kmalloc(nrPages * sizeof (MPN), GFP_KERNEL);
   if (!pages) {
      return QP_ERROR_NO_MEM;
   }

   /*
    * Map in the first page, read out the MPN vector
    */
   down_write(&vm->lockedSem);
   va = kmap(basepfn);
   if (!va) {
      rc = QP_ERROR_INVALID_ARGS;
      kfree(pages);
      qp->pages = NULL;
      goto out;
   }

   /*
    * Grab references and translate MPNs->PFNs
    */
   for (i = 0; i < nrPages; i++) {
      pages[i] = pfn_to_page(((MPN*)va)[i]);
      get_page(pages[i]);
   }

   /*
    * Clean up the first mapping and remap the entire vector
    */
   kunmap(basepfn);
   va = vmap(pages, nrPages, VM_MAP, PAGE_KERNEL);
   if (!va) {
      rc = QP_ERROR_NO_MEM;
      for (i = 0; i < nrPages; i++) {
         put_page(pages[i]);
      }
      kfree(pages);
      qp->pages = NULL;
      goto out;
   } else {
      *hkva = (HKVA)va;
      qp->pages = pages;
   }

   /*
    * Let's not leak mpns..
    */
   memset(va, 0x0, nrPages * PAGE_SIZE);

   rc = QP_SUCCESS;

out:
   up_write(&vm->lockedSem);
   return rc;
}

/**
 * @brief Initialize all free queue pair entries and listeners
 */

void
QP_HostInit(void)
{
   uint32 i;

   for (i = 0; i < QP_MAX_QUEUE_PAIRS; i++) {
      QP_MakeInvalidQPHandle(&queuePairs[i]);
   }

   for (i = 0; i < QP_MAX_LISTENERS; i++) {
      listeners[i] = NULL;
   }
}


/**
 * @brief Detaches a guest from a queue pair and notifies
 *      any registered listeners through the detach callback
 * @param id id that guest requested a detach from, detaches all
 *      queue pairs associated with a VM if the resource id == QP_INVALID_ID
 * @return QP_SUCCESS on success, appropriate error code otherwise
 */

int32
QP_GuestDetachRequest(QPId id)
{
   QPHandle *qp;
   uint32 i;

   if (id.resource >= QP_MAX_ID && id.resource != QP_INVALID_ID) {
      return QP_ERROR_INVALID_ARGS;
   }

   QPLock();

   /*
    * Invalidate all queue pairs associated with this VM if
    * resource == QP_INVALID_ID
    */
   if (id.resource == QP_INVALID_ID) {
      for (i = 0; i < QP_MAX_QUEUE_PAIRS; i++) {
         qp = &queuePairs[i];
         if (qp->id.context == id.context && qp->peerDetachCB) {
            qp->peerDetachCB(qp->detachData);
         }
      }
   } else {
      qp = &queuePairs[id.resource];
      if (qp->peerDetachCB) {
         qp->peerDetachCB(qp->detachData);
      }
  }

   QPUnlock();

   return QP_SUCCESS;
}


/**
 * @brief Attaches a guest to shared memory region
 * @param vm guest to attach
 * @param args queue pair args structure:
 *      - args->id: id of the region to attach to, if id.resource == QP_INVALID_ID, then
 *              an id is assigned
 *      - args->capacity: total size of the region in bytes
 *      - args->type: type of queue pair (e.g PVTCP)
 * @param base base machine page number that lists pages to map
 * @param nrPages number of pages to map
 * @return QP_SUCCESS on success, appropriate error code otherwise.
 */

int32
QP_GuestAttachRequest(MvpkmVM *vm,
                      QPInitArgs *args,
                      MPN base,
                      uint32 nrPages)
{
   int32 rc;
   HKVA hkva = 0;
   QPHandle *qp;
   uint32 i;

   if ((!QP_CheckArgs(args))                              ||
       (vm->wsp->guestId != (Mksck_VmId)args->id.context) ||
       (args->capacity != (nrPages * PAGE_SIZE))) {
      return QP_ERROR_INVALID_ARGS;
   }

   QP_DBG("%s: Guest requested attach to [%u:%u] capacity: %u type: %x base: %x nrPages: %u\n",
          __FUNCTION__,
          args->id.context,
          args->id.resource,
          args->capacity,
          args->type,
          base,
          nrPages);

   QPLock();

   /*
    * Assign a resource id if id == QP_INVALID_ID
    */
   if (args->id.resource == QP_INVALID_ID) {
      for (i = 0; i < QP_MAX_QUEUE_PAIRS; i++) {
         if (queuePairs[i].state == QP_STATE_FREE) {
            args->id.resource = i;
            QP_DBG("%s: Guest requested anonymous region, assigning resource id %u\n",
                   __FUNCTION__, args->id.resource);
            goto found;
         }
      }

      rc = QP_ERROR_NO_MEM;
      goto out;
   }

found:
   qp = queuePairs + args->id.resource;

   if (qp->state != QP_STATE_FREE) {
      rc = QP_ERROR_ALREADY_ATTACHED;
      goto out;
   }

   /*
    * Brand new queue pair, allocate some memory to back it and
    * initialize the entry
    */
   rc = MapPages(vm, base, nrPages, qp, &hkva);
   if (rc != QP_SUCCESS) {
      goto out;
   }

   /* NB: reversed from the guest  */
   qp->id              = args->id;
   qp->capacity        = args->capacity;
   qp->produceQ        = (QHandle*)hkva;
   qp->consumeQ        = (QHandle*)(hkva + args->capacity/2);
   qp->queueSize       = args->capacity/2 - sizeof(QHandle);
   qp->type            = args->type;
   qp->state           = QP_STATE_GUEST_ATTACHED;

   /*
    * The qp is now assumed to be well-formed
    */
   QP_DBG("%s: Guest attached to region [%u:%u] capacity: %u HKVA: %x\n",
          __FUNCTION__,
          args->id.context,
          args->id.resource,
          args->capacity,
          (uint32)hkva);
   rc = QP_SUCCESS;

out:
   QPUnlock();
   if (rc != QP_SUCCESS) {
      QP_DBG("%s: Failed to attach: %u\n", __FUNCTION__, rc);
   }
   return rc;
}


/**
 * @brief Attaches the host to the shared memory region. The guest
 * MUST have allocated the shmem region already or else this will fail.
 * @param args structure with the shared memory region id to attach to,
 *      total size of the region in bytes, and type of queue pair (e.g PVTCP)
 * @param[in, out] qp handle to the queue pair to return
 * @return QP_SUCCESS on success, appropriate error code otherwise
 */

int32
QP_Attach(QPInitArgs *args,
          QPHandle** qp)
{
   uint32 rc;

   if (!qp || !QP_CheckArgs(args)) {
      return QP_ERROR_INVALID_ARGS;
   }

   QP_DBG("%s: Attaching to id: [%u:%u] capacity: %u\n",
          __FUNCTION__,
          args->id.context,
          args->id.resource,
          args->capacity);

   QPLock();
   *qp = queuePairs + args->id.resource;

   if (!QP_CheckHandle(*qp)) {
      *qp = NULL;
      rc = QP_ERROR_INVALID_HANDLE;
      goto out;
   }

   if ((*qp)->state == QP_STATE_CONNECTED) {
      rc = QP_ERROR_ALREADY_ATTACHED;
      goto out;
   }

   if ((*qp)->state != QP_STATE_GUEST_ATTACHED) {
      rc = QP_ERROR_INVALID_HANDLE;
      goto out;
   }

   (*qp)->state = QP_STATE_CONNECTED;

   QP_DBG("%s: Attached!\n", __FUNCTION__);
   rc = QP_SUCCESS;

out:
   QPUnlock();
   return rc;
}

/**
 * @brief Detaches the host to the shared memory region.
 * @param[in, out] qp handle to the queue pair
 * @return QP_SUCCESS on success, appropriate error code otherwise
 * @sideeffects Frees memory
 */

int32
QP_Detach(QPHandle* qp)
{
   uint32 rc;
   uint32 i;

   QPLock();
   if (!QP_CheckHandle(qp)) {
      rc = QP_ERROR_INVALID_HANDLE;
      goto out;
   }

   QP_DBG("%s: Freeing queue pair [%u:%u]\n",
          __FUNCTION__,
          qp->id.context,
          qp->id.resource);

   BUG_ON(!qp->produceQ);
   BUG_ON(!qp->pages);
   BUG_ON((qp->state != QP_STATE_CONNECTED) &&
          (qp->state != QP_STATE_GUEST_ATTACHED));

   vunmap(qp->produceQ);

   for (i = 0; i < qp->capacity/PAGE_SIZE; i++) {
      put_page(qp->pages[i]);
   }
   kfree(qp->pages);

   QP_DBG("%s: Host detached from [%u:%u]\n",
          __FUNCTION__,
          qp->id.context,
          qp->id.resource);

   QP_MakeInvalidQPHandle(qp);
   rc = QP_SUCCESS;

out:
   QPUnlock();
   return rc;
}


/**
 * @brief Detaches and destroys all queue pairs associated with a given guest
 * @param vmID which VM to clean up
 * @sideeffects Destroys all queue pairs for guest vmID
 */

void QP_DetachAll(Mksck_VmId vmID) {
   QPId id = {
      .context = (uint32)vmID,
      .resource = QP_INVALID_ID
   };

   QP_DBG("%s: Detaching all queue pairs from vmId context %u\n", __FUNCTION__, vmID);
   QP_GuestDetachRequest(id);
}

/**
 * @brief Registers a listener into the queue pair system. Callbacks are
 *      called with interrupts disabled and must not sleep.
 * @param listener listener to be called
 * @return QP_SUCCESS on success, QP_ERROR_NO_MEM if no more
 *         listeners can be registered
 */

int32
QP_RegisterListener(const QPListener listener)
{
   uint32 i;
   int32 rc = QP_ERROR_NO_MEM;

   QPLock();
   for (i = 0; i < QP_MAX_LISTENERS; i++) {
      if (!listeners[i]) {
         listeners[i] = listener;
         QP_DBG("%s: Registered listener\n", __FUNCTION__);
         rc = QP_SUCCESS;
         break;
      }
   }
   QPUnlock();

   return rc;
}


/**
 * @brief Unregister a listener service from the queue pair system.
 * @param listener listener to unregister
 * @return QP_SUCCESS on success, appropriate error code otherwise
 */

int32
QP_UnregisterListener(const QPListener listener)
{
   uint32 i;
   int32 rc = QP_ERROR_INVALID_HANDLE;

   QPLock();
   for (i = 0; i < QP_MAX_LISTENERS; i++) {
      if (listeners[i] == listener) {
         listeners[i] = NULL;
         QP_DBG("%s: Unregistered listener\n", __FUNCTION__);
         rc = QP_SUCCESS;
         break;
      }
   }
   QPUnlock();

   return rc;
}


/**
 * @brief Registers a callback to be called when the guest detaches
 *      from a queue pair. Callbacks are called with interrupts and
 *      must not sleep.
 * @param qp handle to the queue pair
 * @param callback callback to be called
 * @param data data to deliver to the callback
 * @return QP_SUCCESS on success, appropriate error code otherwise
 */

int32
QP_RegisterDetachCB(QPHandle *qp,
                    void (*callback)(void*),
                    void *data)
{
   if (!QP_CheckHandle(qp)) {
      return QP_ERROR_INVALID_HANDLE;
   }

   if (!callback) {
      return QP_ERROR_INVALID_ARGS;
   }

   qp->peerDetachCB   = callback;
   qp->detachData = data;
   QP_DBG("%s: Registered detach callback\n", __FUNCTION__);
   return QP_SUCCESS;
}


/**
 * @brief Noop on the host, only guests can initiate a notify
 * @param args noop
 * @return QP_SUCCESS
 */


int32 QP_Notify(QPInitArgs *args) {
   return QP_SUCCESS;
}


/**
 * @brief Notify any registered listeners for the given queue pair
 * @param args initialization arguments used by the guest
 * @return QP_SUCCESS on success, error otherwise
 */

int32 QP_NotifyListener(QPInitArgs *args) {
   int32 i;
   QPHandle *qp = NULL;

   if (!QP_CheckArgs(args)) {
      return QP_ERROR_INVALID_ARGS;
   }

   /*
    * Iterate over listeners until one of them reports they handled it
    */
   QPLock();
   for (i = 0; i < QP_MAX_LISTENERS; i++) {
      if (listeners[i]) {
         QP_DBG("Delivering attach event to listener...\n");
         if (listeners[i](args) == QP_SUCCESS) {
            break;
         }
      }
   }

   if (i == QP_MAX_LISTENERS) {
      /*
       * No listener successfully probed this QP.
       * The guest DETACH HVC isn't implemented; we need compensate for it
       * by deallocating the QP here.
       * This is a workaround which assumes, more-or-less correctly, that
       * unsuccessful QP probes never lead to subsequent host-attaching.
       */

      qp = &queuePairs[args->id.resource];
   }

   QPUnlock();

   if (qp) {
      QP_Detach(qp);
   }
   return QP_SUCCESS;
}


EXPORT_SYMBOL(QP_Attach);
EXPORT_SYMBOL(QP_Detach);
EXPORT_SYMBOL(QP_RegisterListener);
EXPORT_SYMBOL(QP_UnregisterListener);
EXPORT_SYMBOL(QP_RegisterDetachCB);
EXPORT_SYMBOL(QP_Notify);
