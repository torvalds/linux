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
 * @brief Communication functions exported by the comm_rt module.
 */

#ifndef _COMM_SVC_H_
#define _COMM_SVC_H_

#define INCLUDE_ALLOW_MODULE
#define INCLUDE_ALLOW_PV
#define INCLUDE_ALLOW_GPL
#include "include_check.h"

#include "comm.h"

int CommSvc_RegisterImpl(const CommImpl *impl);
void CommSvc_UnregisterImpl(const CommImpl *impl);
int CommSvc_Zombify(CommChannel channel, int inBH);
int CommSvc_IsActive(CommChannel channel);
CommTranspInitArgs CommSvc_GetTranspInitArgs(CommChannel channel);
void *CommSvc_GetState(CommChannel channel);
void CommSvc_Put(CommChannel channel);
void CommSvc_DispatchUnlock(CommChannel channel);
int CommSvc_Lock(CommChannel channel);
void CommSvc_Unlock(CommChannel channel);
int CommSvc_ScheduleAIOWork(CommOSWork *work);

int
CommSvc_Alloc(const CommTranspInitArgs *transpArgs,
              const CommImpl *impl,
              int inBH,
              CommChannel *newChannel);

int
CommSvc_Write(CommChannel channel,
              const CommPacket *packet,
              unsigned long long *timeoutMillis);

int
CommSvc_WriteVec(CommChannel channel,
                 const CommPacket *packet,
                 struct kvec **vec,
                 unsigned int *vecLen,
                 unsigned long long *timeoutMillis,
                 unsigned int *iovOffset);

unsigned int CommSvc_RequestInlineEvents(CommChannel channel);
unsigned int CommSvc_ReleaseInlineEvents(CommChannel channel);

#endif // _COMM_SVC_H_
