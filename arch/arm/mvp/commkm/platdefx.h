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
 * @brief Basic platform definitions needed various places.
 */

#ifndef _PLATDEFX_H
#define _PLATDEFX_H

#define INCLUDE_ALLOW_MVPD
#define INCLUDE_ALLOW_VMX
#define INCLUDE_ALLOW_MODULE
#define INCLUDE_ALLOW_MONITOR
#define INCLUDE_ALLOW_PV
#define INCLUDE_ALLOW_HOSTUSER
#define INCLUDE_ALLOW_GUESTUSER
#define INCLUDE_ALLOW_WORKSTATION
#define INCLUDE_ALLOW_GPL
#include "include_check.h"

#define PAGE_ORDER              12

#ifndef PAGE_SIZE
#define PAGE_SIZE               (1UL << PAGE_ORDER)
#endif
#if PAGE_SIZE != 4096
#error bad page size PAGE_SIZE
#endif

#define PA_2_PPN(_pa)   ((_pa) / PAGE_SIZE)
#define PPN_2_PA(_ppn)  ((_ppn) * PAGE_SIZE)

#define VMM_DOMAIN                0x0
#define VMM_DOMAIN_NO_ACCESS      0x3
#define VMM_DOMAIN_CLIENT         0x1
#define VMM_DOMAIN_MANAGER        0x4

#define INVALID_CVA  (-(CVA)1)
#define INVALID_GVA  (-(GVA)1)
#define INVALID_MVA  (-(MVA)1)
#define INVALID_HKVA (-(HKVA)1)
#define INVALID_HUVA (-(HUVA)1)

#define INVALID_MPN  (((MPN)-1) >> ARM_L2D_SMALL_ORDER)
#define INVALID_PPN  (((PPN)-1) >> ARM_L2D_SMALL_ORDER)

#endif
