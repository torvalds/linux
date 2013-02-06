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
 * @brief Worldswitch call parameters
 */

#ifndef _WSCALLS_H
#define _WSCALLS_H

#define INCLUDE_ALLOW_MODULE
#define INCLUDE_ALLOW_MONITOR
#define INCLUDE_ALLOW_GPL
#include "include_check.h"

#define WSCALL_ACQUIRE_PAGE           1
#define WSCALL_FLUSH_ALL_DCACHES      2
#define WSCALL_IRQ                    3
#define WSCALL_ABORT                  4
#define WSCALL_LOG                    5
#define WSCALL_WAIT                   6
#define WSCALL_MUTEXLOCK              7
#define WSCALL_MUTEXUNLOCK            8
#define WSCALL_MUTEXUNLSLEEP          9
#define WSCALL_MUTEXUNLWAKE          10
#define WSCALL_GET_PAGE_FROM_VMID    11
#define WSCALL_REMOVE_PAGE_FROM_VMID 12
#define WSCALL_RELEASE_PAGE          13
#define WSCALL_READTOD               14
#define WSCALL_QP_GUEST_ATTACH       15
#define WSCALL_MONITOR_TIMER         16
#define WSCALL_COMM_SIGNAL           17
#define WSCALL_QP_NOTIFY             18
/*
 * MVPKM V0.5.2.0 supports all the calls above. If new API calls are
 * introduced then make sure that the calling function (probably in
 * mkhost.c) checks the mvpkm's version stored in wsp->mvpkmVersion
 * and invokes the wscall only when it is supported.
 */

#define WSCALL_MAX_CALLNO            20

#define WSCALL_LOG_MAX 256

#define WSCALL_MAX_MPNS  16

#include "exitstatus.h"
#include "mutex.h"
#include "mksck_shared.h"
#include "qp.h"
#include "comm_transp.h"
#include "comm_transp_impl.h"

typedef struct WSParams {
   uint32 callno;
   union {
      /**
       * @brief Used for both WSCALL_ACQUIRE_PAGE and WSCALL_RELEASE_PAGE.
       */
      struct {
         uint16 pages;                 ///< IN Number of pages
         uint16 order;                 /**< IN Size of each page -
                                               2^(12+order) sized and aligned
                                               in machine space.
                                               (WSCALL_ACQUIRE_PAGE only) */
         PhysMem_RegionType forRegion; /**< IN Region identifier for pages
                                               (WSCALL_ACQUIRE_PAGE only) */
         MPN mpns[WSCALL_MAX_MPNS];    /**< OUT (on WSCALL_ACQUIRE_PAGE)
                                            IN (on WSCALL_RELEASE_PAGE)
                                               Vector of page base MPNs. */
      } pages;

      union {
         MPN mpn;                  ///< IN MPN to query refcount.
         _Bool referenced;         ///< OUT Do host page tables contain the MPN?
      } refCount;

      struct {
         ExitStatus   status;      ///< IN the final status of the monitor
      } abort;

      struct {
         int level;
         char messg[WSCALL_LOG_MAX];
      } log;

      struct {
         HKVA mtxHKVA;             ///< IN mutex's host kernel virt addr
         MutexMode mode;           ///< IN shared or exclusive
         uint32 cvi;               ///< IN condition variable index
         _Bool all;                ///< IN wake all waiting threads?
         _Bool ok;                 ///< OUT Mutex_Lock completed
      } mutex;

      struct {
         Mksck_VmId  vmId;         ///< IN translate and lock this vmID
         _Bool found;              /**< OUT true if the lookup was successful,
                                            page is found, and refc incremented */
         MPN mpn[MKSCKPAGE_TOTAL]; ///< OUT array of MPNs of the requested vmId
      } pageMgmnt;

      struct {
         unsigned int now;         ///< OUT current time-of-day seconds
         unsigned int nowusec;     ///< OUT current time-of-day microseconds
      } tod;

      struct {
         QPId id;                 ///< IN/OUT shared memory id
         uint32 capacity;         ///< IN size of shared region requested
         uint32 type;             ///< IN type of queue pair
         uint32 base;             ///< IN base MPN of PA vector page
         uint32 nrPages;          ///< IN number of pages to map
         int32 rc;                ///< OUT return code
      } qp;

      struct {
         CommTranspID transpID;
         CommTranspIOEvent event;
      } commEvent;

      struct {
         uint64 when64;           ///< IN timer request
      } timer;

      struct {
         _Bool suspendMode;       ///< Is the guest in suspend mode?
      } wait;

   };                              ///< anonymous union
} WSParams;


/**
 * @brief Cast the opaque param_ member of the wsp to WSParams type
 * @param wsp_ the world switch page structure pointer
 * @return the cast pointer
 */
static inline WSParams* UNUSED
WSP_Params(WorldSwitchPage *wsp_) {
   return (WSParams*)(wsp_->params_);
}

MY_ASSERTS(WSParFn,
   ASSERT_ON_COMPILE(sizeof(WSParams) <= WSP_PARAMS_SIZE);
)
#endif
