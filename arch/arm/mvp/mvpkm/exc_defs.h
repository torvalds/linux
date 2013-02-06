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
 *  @brief Exception-related definitions. See A2.6 ARM DDI 0100I.
 */

#ifndef _EXC_DEFS_H_
#define _EXC_DEFS_H_

#define INCLUDE_ALLOW_MVPD
#define INCLUDE_ALLOW_VMX
#define INCLUDE_ALLOW_MODULE
#define INCLUDE_ALLOW_MONITOR
#define INCLUDE_ALLOW_PV
#define INCLUDE_ALLOW_GPL
#include "include_check.h"

#define EXC_VECTOR_SIZE                   0x20

#define EXC_RESET_VECTOR_OFFSET           0x00
#define EXC_UNDEFINED_VECTOR_OFFSET       0x04
#define EXC_SWI_VECTOR_OFFSET             0x08
#define EXC_PREFETCH_ABORT_VECTOR_OFFSET  0x0c
#define EXC_DATA_ABORT_VECTOR_OFFSET      0x10
#define EXC_HYP_VECTOR_OFFSET             0x14
#define EXC_IRQ_VECTOR_OFFSET             0x18
#define EXC_FIQ_VECTOR_OFFSET             0x1c

#define EXC_ARM_UNDEFINED_SAVED_PC_OFFSET        4
#define EXC_ARM_SWI_SAVED_PC_OFFSET              4
#define EXC_ARM_PREFETCH_ABORT_SAVED_PC_OFFSET   4
#define EXC_ARM_DATA_ABORT_SAVED_PC_OFFSET       8
#define EXC_ARM_IRQ_SAVED_PC_OFFSET              4
#define EXC_ARM_FIQ_SAVED_PC_OFFSET              4

#define EXC_THUMB_UNDEFINED_SAVED_PC_OFFSET        2
#define EXC_THUMB_SWI_SAVED_PC_OFFSET              2
#define EXC_THUMB_PREFETCH_ABORT_SAVED_PC_OFFSET   4
#define EXC_THUMB_DATA_ABORT_SAVED_PC_OFFSET       8
#define EXC_THUMB_IRQ_SAVED_PC_OFFSET              4
#define EXC_THUMB_FIQ_SAVED_PC_OFFSET              4

#define EXC_SAVED_PC_OFFSET(exc, cpsr) \
   (((cpsr) & ARM_PSR_T) ? EXC_THUMB_##exc##_SAVED_PC_OFFSET : \
                           EXC_ARM_##exc##_SAVED_PC_OFFSET)

#endif /// _EXC_DEFS_H_
