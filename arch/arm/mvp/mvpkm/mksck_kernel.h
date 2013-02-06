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
 * @brief The monitor-kernel socket interface kernel-only definitions.
 */

#ifndef _MKSCK_KERNEL_H
#define _MKSCK_KERNEL_H

#define INCLUDE_ALLOW_MODULE
#define INCLUDE_ALLOW_GPL
#include "include_check.h"

#include "mksck_shared.h"

/*
 * prototypes
 */
int        Mksck_Init(void);
void       Mksck_Exit(void);
void       Mksck_WakeBlockedSockets(MksckPage *mksckPage);
MksckPage *MksckPage_GetFromTgidIncRefc(void);
MksckPage *MksckPage_GetFromVmIdIncRefc(Mksck_VmId vmId);
MksckPage *MksckPage_GetFromIdx(uint32 idx);
void       MksckPageInfo_Init(void);
void       MksckPageInfo_Exit(void);
int        Mksck_WspInitialize(MvpkmVM *vm);
void       Mksck_WspRelease(WorldSwitchPage *wsp);
int        MksckPage_LookupAndInsertPage(struct vm_area_struct *vma,
                                         unsigned long address,
                                         MPN mpn);

/*
 * Mksck open request must come from this uid.
 */
extern uid_t Mvpkm_vmwareUid;

#define MKSCK_DEVEL 0

#if MKSCK_DEVEL
#define PRINTK printk
#else
#define PRINTK if (0) printk
#endif

#define HOST_CPUID_UNDEF (~0)

#endif
