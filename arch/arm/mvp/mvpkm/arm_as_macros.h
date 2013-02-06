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
 *  @brief Macro definitions meta-ops to be used in assembler files
 *
 *  This header contains asm macro definitions to be used in asm
 *  files only. This is intended to be the equivalent of arm_gcc_inline.h
 */

#ifndef _ARM_AS_MACROS_H_
#define _ARM_AS_MACROS_H_

#define INCLUDE_ALLOW_MVPD
#define INCLUDE_ALLOW_VMX
#define INCLUDE_ALLOW_MODULE
#define INCLUDE_ALLOW_MONITOR
#define INCLUDE_ALLOW_PV
#define INCLUDE_ALLOW_GPL
#include "include_check.h"

#include "coproc_defs.h"

/**
 * @name The following macros re-arrange the order of the mcr/mrc operands
 * making it suitable to be used with the macros defined in coproc_defs.h
 *
 * @par For example
 *      mcr_p15 DOMAIN_CONTROL, r3
 * @par replaces
 *      mcr p15, 0, r3, c3, c0, 0
 * @{
 */
.macro mcr_p15 op1, op2, op3, op4, reg, cond=al
   mcr\cond p15, \op1, \reg, \op2, \op3, \op4
.endm

.macro mrc_p15 op1, op2, op3, op4, reg, cond=al
   mrc\cond p15, \op1, \reg, \op2, \op3, \op4
.endm

.macro mcrr_p15 op1, op2, reg1, reg2
   mcrr p15, \op1, \reg1, \reg2, \op2
.endm

.macro mrrc_p15 op1, op2, reg1, reg2
   mrrc p15, \op1, \reg1, \reg2, \op2
.endm
/*@}*/

/**
 * @name Our toolchain does not include support for the VE instructions yet.
 * @{
 */
.macro hvc imm16
   .word ARM_INSTR_HVC_A1_ENC(\imm16)
.endm

.macro eret
   .word ARM_INSTR_ERET_A1_ENC(ARM_INSTR_COND_AL)
.endm

.macro msr_ext rm, rn
   .word ARM_INSTR_MSR_EXT_A1_ENC(ARM_INSTR_COND_AL, \rm, \rn)
.endm

.macro mrs_ext rd, rm
   .word ARM_INSTR_MRS_EXT_A1_ENC(ARM_INSTR_COND_AL, \rd, \rm)
.endm
/*@}*/

#endif /// ifndef _ARM_AS_MACROS_H_
