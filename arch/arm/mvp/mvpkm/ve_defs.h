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
 * @brief Virtualization extension definitions.
 *
 * See ARM PRD03-GENC-008353  11.0.
 */
#ifndef _VE_DEFS_H_
#define _VE_DEFS_H_

#define INCLUDE_ALLOW_MVPD
#define INCLUDE_ALLOW_VMX
#define INCLUDE_ALLOW_MODULE
#define INCLUDE_ALLOW_MONITOR
#define INCLUDE_ALLOW_PV
#define INCLUDE_ALLOW_GPL
#include "include_check.h"

#define ARM_VE_HSR_EC_BIT_POS          26
#define ARM_VE_HSR_EC_LENGTH            6

#define ARM_VE_HSR_EC_UNKNOWN        0x00
#define ARM_VE_HSR_EC_WFI_WFE        0x01
#define ARM_VE_HSR_EC_MCR_MRC_CP15   0x03
#define ARM_VE_HSR_EC_MCRR_MRRC_CP15 0x04
#define ARM_VE_HSR_EC_MCR_MRC_CP14   0x05
#define ARM_VE_HSR_EC_LDC_STC_CP14   0x06
#define ARM_VE_HSR_EC_HCPTR          0x07
#define ARM_VE_HSR_EC_MRC_CP10       0x08
#define ARM_VE_HSR_EC_JAZELLE        0x09
#define ARM_VE_HSR_EC_BXJ            0x0a
#define ARM_VE_HSR_EC_MRRC_CP14      0x0c
#define ARM_VE_HSR_EC_SVC_HYP        0x11
#define ARM_VE_HSR_EC_HVC            0x12
#define ARM_VE_HSR_EC_SMC            0x13
#define ARM_VE_HSR_EC_IABORT_SND     0x20
#define ARM_VE_HSR_EC_IABORT_HYP     0x21
#define ARM_VE_HSR_EC_DABORT_SND     0x24
#define ARM_VE_HSR_EC_DABORT_HYP     0x25

#define ARM_VE_HSR_FS_BIT_POS           0
#define ARM_VE_HSR_FS_LENGTH            6

#define ARM_VE_HSR_FS_TRANS_L1        0x5
#define ARM_VE_HSR_FS_TRANS_L2        0x6
#define ARM_VE_HSR_FS_TRANS_L3        0x7

#define ARM_VE_HSR_FS_PERM_L1         0xd
#define ARM_VE_HSR_FS_PERM_L2         0xe
#define ARM_VE_HSR_FS_PERM_L3         0xf

#endif /// ifndef _VE_DEFS_H_
