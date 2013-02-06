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
 * @brief Exit Status values
 */

#ifndef _EXITSTATUS_H
#define _EXITSTATUS_H

#define INCLUDE_ALLOW_MVPD
#define INCLUDE_ALLOW_VMX
#define INCLUDE_ALLOW_MODULE
#define INCLUDE_ALLOW_MONITOR
#define INCLUDE_ALLOW_GPL
#define INCLUDE_ALLOW_HOSTUSER
#include "include_check.h"


#define _EXIT_STATUS_DEF \
   _EXIT_STATUS_ITEM(Success,        0) \
   _EXIT_STATUS_ITEM(ReturnToHost,   1) \
   _EXIT_STATUS_ITEM(GuestExit,      2) \
   _EXIT_STATUS_ITEM(HostRequest,    3) \
   _EXIT_STATUS_ITEM(VMXFatalError,  4) \
   _EXIT_STATUS_ITEM(VMMFatalError,  5) \
   _EXIT_STATUS_ITEM(MVPDFatalError, 6) \
   _EXIT_STATUS_ITEM(VPNFatalError,  7) \
   _EXIT_STATUS_ITEM(VMXFindCause,   8)


enum ExitStatus {
#define _EXIT_STATUS_ITEM(name,num) ExitStatus##name = num,
_EXIT_STATUS_DEF
#undef  _EXIT_STATUS_ITEM
};

typedef enum ExitStatus ExitStatus;

#ifndef __cplusplus
static const char * ExitStatusName[] UNUSED = {
#define _EXIT_STATUS_ITEM(name,num) [ExitStatus##name] = #name,
_EXIT_STATUS_DEF
#undef  _EXIT_STATUS_ITEM
};
#endif

#endif
