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
 *  @brief Constant definitions for ARM CPSR/SPSR registers. See A2.5
 *         ARM DDI 0100I.
 */

#ifndef _PSR_DEFS_H_
#define _PSR_DEFS_H_

#define INCLUDE_ALLOW_MVPD
#define INCLUDE_ALLOW_VMX
#define INCLUDE_ALLOW_MODULE
#define INCLUDE_ALLOW_MONITOR
#define INCLUDE_ALLOW_PV
#define INCLUDE_ALLOW_GPL
#include "include_check.h"

#define ARM_PSR_MODE_USER          0x10
#define ARM_PSR_MODE_FIQ           0x11
#define ARM_PSR_MODE_IRQ           0x12
#define ARM_PSR_MODE_SUPERVISOR    0x13
#define ARM_PSR_MODE_ABORT         0x17
#define ARM_PSR_MODE_HVC           0x1a
#define ARM_PSR_MODE_UNDEFINED     0x1b
#define ARM_PSR_MODE_SYSTEM        0x1f

/* Bit 31: N */
#define ARM_PSR_N             (1 << 31)

/* Bit 30: Z */
#define ARM_PSR_Z             (1 << 30)

/* Bit 29: C */
#define ARM_PSR_C             (1 << 29)

/* Bit 28: V */
#define ARM_PSR_V             (1 << 28)

/* Bit 27: Q */
#define ARM_PSR_Q             (1 << 27)

#define ARM_PSR_COND_FLAGS \
   (ARM_PSR_N | ARM_PSR_Z | ARM_PSR_C | ARM_PSR_V | ARM_PSR_Q)

/* Bits 26..25: ITSTATE<1..0> */
#define ARM_PSR_ITSTATE_LOW   MVP_MASK(25, 2)

/* Bit 24: J */
#define ARM_PSR_J             (1 << 24)

/* Bits 23..20 are reserved as of ARMv7 */
#define ARM_PSR_RESERVED      MVP_MASK(20, 4)

/* Bits 19..16: GE<3..0> */
#define ARM_PSR_GE             MVP_MASK(16, 4)

/* Bits 15..10: ITSTATE<7..2> */
#define ARM_PSR_ITSTATE_HIGH  MVP_MASK(10, 6)
#define ARM_PSR_ITSTATE       (ARM_PSR_ITSTATE_LOW | ARM_PSR_ITSTATE_HIGH)

/* Bit 9: E */
#define ARM_PSR_E_POS         (9)
#define ARM_PSR_E             (1 << ARM_PSR_E_POS)

/* Bit 8: A */
#define ARM_PSR_A_POS         (8)
#define ARM_PSR_A             (1 << ARM_PSR_A_POS)

/* Bit 7: I */
#define ARM_PSR_I_POS         (7)
#define ARM_PSR_I             (1 << ARM_PSR_I_POS)

/* Bit 6: F */
#define ARM_PSR_F_POS         (6)
#define ARM_PSR_F             (1 << ARM_PSR_F_POS)

/* Bit 5: T */
#define ARM_PSR_T_POS         (5)
#define ARM_PSR_T             (1 << ARM_PSR_T_POS)

/* Bits 4..0: Mode */
#define ARM_PSR_MODE_MASK     0x1f

#define ARM_PSR_MODE(cpsr)        ((cpsr) & ARM_PSR_MODE_MASK)
#define ARM_PSR_USER_MODE(cpsr)   (ARM_PSR_MODE(cpsr) == ARM_PSR_MODE_USER)


/*
 * We shadow the 10 LSBs in the CPSR, with the exception of the T bit, as they
 * are managed by the VMM on behalf of the guest and are potentially different
 * than the physical CPSR during DE.
 */
#define ARM_PSR_MONITOR_BITS 10
#define ARM_PSR_MONITOR_MASK (((1 << ARM_PSR_MONITOR_BITS) - 1) & ~ARM_PSR_T)

#endif /// ifndef _PSR_DEFS_H_
