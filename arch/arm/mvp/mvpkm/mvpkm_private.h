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
 * @brief Private interface between user level wrappers and kernel module.
 * The communication uses the ioctl linux call. The command operand is one
 * of the MVPKM_xxx macros defined below, the custom operand is a pointer
 * to the respective structure below.
 */


#ifndef _MVPKMPRIVATE_H
#define _MVPKMPRIVATE_H

#define INCLUDE_ALLOW_MVPD
#define INCLUDE_ALLOW_MODULE
#define INCLUDE_ALLOW_HOSTUSER
#define INCLUDE_ALLOW_GPL
#include "include_check.h"

#include <linux/ioctl.h>
/*
 * For details how to create ioctl numbers, see
 * Documentation/ioctl/ioctl-number.txt.  The letter '9' is
 * unused. The 0xa0-0xaf block is even more unused. Note however, that
 * ioctl numbers are desired to be unique for debug purposes, they
 * may conflict.
 */
#define MVP_IOCTL_LETTER '9'
#define MVPKM_DISABLE_FAULT  _IO( MVP_IOCTL_LETTER, 0xa0)
#define MVPKM_LOCK_MPN       _IOW(MVP_IOCTL_LETTER, 0xa1, MvpkmLockMPN)
#define MVPKM_UNLOCK_MPN     _IOW(MVP_IOCTL_LETTER, 0xa2, MvpkmLockMPN)
#define MVPKM_RUN_MONITOR    _IO( MVP_IOCTL_LETTER, 0xa3)
#define MVPKM_CPU_INFO       _IOR(MVP_IOCTL_LETTER, 0xa4, MvpkmCpuInfo)
#define MVPKM_ABORT_MONITOR  _IO( MVP_IOCTL_LETTER, 0xa5)
#define MVPKM_MAP_WSPHKVA    _IOW(MVP_IOCTL_LETTER, 0xa7, MvpkmMapHKVA)

#include "mksck.h"
#include "monva_common.h"
#include "mvpkm_types.h"

/**
 * @brief Operand for the MVPKM_LOCK_MPN call
 */
typedef struct MvpkmLockMPN {
   uint32  order;  /* IN  */
   PhysMem_RegionType forRegion;  /* IN */
   uint32  mpn;    /* OUT */
} MvpkmLockMPN;

/**
 * @brief Operand for the MVPKM_MAP_HKVA call
 */
typedef struct MvpkmMapHKVA {
   HkvaMapInfo *mapInfo;  /* IN */
   PhysMem_RegionType forRegion;  /* IN */
   HKVA hkva;    /* OUT */
} MvpkmMapHKVA;

#define WSP_PAGE_COUNT            2

/**
 * @brief Operand for the MVPKM_CPU_INFO call
 */
typedef struct MvpkmCpuInfo {
   ARM_L2D attribL2D;           /* OUT */
   ARM_MemAttrNormal attribMAN; /* OUT */
   _Bool mpExt;                 /* OUT */
} MvpkmCpuInfo;

/**
 * @brief These magic numbers mark the beginning and end of the
 * special page that is mapped into the virtual address space of MVPD
 * when it's monitor coredumper requests an unavailable page.
 */
#define MVPKM_STUBPAGE_BEG 0x78d10c67
#define MVPKM_STUBPAGE_END 0x8378f3dd
#endif
