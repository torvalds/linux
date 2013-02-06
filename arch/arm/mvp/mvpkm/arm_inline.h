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
 * @brief Inline stubs for ARM assembler instructions.
 */

#ifndef _ARM_INLINE_H_
#define _ARM_INLINE_H_

#define INCLUDE_ALLOW_MVPD
#define INCLUDE_ALLOW_VMX
#define INCLUDE_ALLOW_MODULE
#define INCLUDE_ALLOW_MONITOR
#define INCLUDE_ALLOW_PV
#define INCLUDE_ALLOW_GPL
#include "include_check.h"

#include "arm_types.h"
#include "arm_defs.h"

/*
 * Compiler specific include - we get the actual inline assembler macros here.
 */
#include "arm_gcc_inline.h"

/*
 * Some non-compiler specific helper functions for inline assembler macros
 * included above.
 */

/**
 * @brief Predicate giving whether interrupts are currently enabled
 *
 * @return TRUE if enabled, FALSE otherwise
 */
static inline _Bool
ARM_InterruptsEnabled(void)
{
   return !(ARM_ReadCPSR() & ARM_PSR_I);
}

/**
 * @brief Read current TTBR0 base machine address
 *
 * @return machine address given by translation table base register 0
 */
static inline MA
ARM_ReadTTBase0(void)
{
   MA ttbase;

   ARM_MRC_CP15(TTBASE0_POINTER, ttbase);

   return ttbase & ARM_CP15_TTBASE_MASK;
}

/**
 * @brief Read VFP/Adv.SIMD Extension System Register
 *
 * @param specReg which VFP/Adv. SIMD Extension System Register
 *
 * @return Read value
 */
static inline uint32
ARM_ReadVFPSystemRegister(uint8 specReg)
{
   uint32 value = 0;

   /*
    * VMRS is the instruction used to read VFP System Registers.
    * VMRS is the new UAL-syntax equivalent for the FMRX instruction.
    * At the end of the day, all these are just synonyms for MRC
    * instructions on CP10, as the VFP system registers sit in CP10
    * and MRC is the Co-processor register read instruction.
    * We use the primitive MRC synonym for VMRS here as VMRS/FMRX
    * don't seem to be working when used inside asm volatile blocks,
    * as, for some reason, the inline assembler seems to be setting
    * the VFP mode to soft-float. Moreover, we WANT the monitor code
    * to be compiled with soft-float so that the compiler doesn't use
    * VFP instructions for the monitor's own use, such as for 64-bit
    * integer operations, etc., since we pass-through the use of the
    * underlying hardware's VFP/SIMD state to the guest.
    */

   switch (specReg) {
      case ARM_VFP_SYSTEM_REG_FPSID:
         ARM_MRC_CP10(VFP_FPSID, value);
         break;
      case ARM_VFP_SYSTEM_REG_MVFR0:
         ARM_MRC_CP10(VFP_MVFR0, value);
         break;
      case ARM_VFP_SYSTEM_REG_MVFR1:
         ARM_MRC_CP10(VFP_MVFR1, value);
         break;
      case ARM_VFP_SYSTEM_REG_FPEXC:
         ARM_MRC_CP10(VFP_FPEXC, value);
         break;
      case ARM_VFP_SYSTEM_REG_FPSCR:
         ARM_MRC_CP10(VFP_FPSCR, value);
         break;
      case ARM_VFP_SYSTEM_REG_FPINST:
         ARM_MRC_CP10(VFP_FPINST, value);
         break;
      case ARM_VFP_SYSTEM_REG_FPINST2:
         ARM_MRC_CP10(VFP_FPINST2, value);
         break;
      default:
         NOT_IMPLEMENTED_JIRA(1849);
         break;
   }

   return value;
}

/**
 * @brief Write to VFP/Adv.SIMD Extension System Register
 *
 * @param specReg which VFP/Adv. SIMD Extension System Register
 * @param value desired value to be written to the System Register
 */
static inline void
ARM_WriteVFPSystemRegister(uint8 specReg, uint32 value)
{
   /*
    * VMSR is the instruction used to write to VFP System Registers.
    * VMSR is the new UAL-syntax equivalent for the FMXR instruction.
    * At the end of the day, all these are just synonyms for MCR
    * instructions on CP10, as the VFP system registers sit in CP10
    * and MCR is the Co-processor register write instruction.
    * We use the primitive MCR synonym for VMSR here as VMSR/FMXR
    * don't seem to be working when used inside asm volatile blocks,
    * as, for some reason, the inline assembler seems to be setting
    * the VFP mode to soft-float. Moreover, we WANT the monitor code
    * to be compiled with soft-float so that the compiler doesn't use
    * VFP instructions for the monitor's own use, such as for 64-bit
    * integer operations, etc., since we pass-through the use of the
    * underlying hardware's VFP/SIMD state to the guest.
    */

   switch (specReg) {
      case ARM_VFP_SYSTEM_REG_FPEXC:
         ARM_MCR_CP10(VFP_FPEXC, value);
         break;
      case ARM_VFP_SYSTEM_REG_FPSCR:
         ARM_MCR_CP10(VFP_FPSCR, value);
         break;
      case ARM_VFP_SYSTEM_REG_FPINST:
         ARM_MCR_CP10(VFP_FPINST, value);
         break;
      case ARM_VFP_SYSTEM_REG_FPINST2:
         ARM_MCR_CP10(VFP_FPINST2, value);
         break;
      default:
         NOT_IMPLEMENTED_JIRA(1849);
         break;
   }
}

#endif /// ifndef _ARM_INLINE_H_
