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
 * @brief GCC inline stubs for ARM assembler instructions.
 */

#ifndef _ARM_GCC_INLINE_H_
#define _ARM_GCC_INLINE_H_

#define INCLUDE_ALLOW_MVPD
#define INCLUDE_ALLOW_VMX
#define INCLUDE_ALLOW_MODULE
#define INCLUDE_ALLOW_MONITOR
#define INCLUDE_ALLOW_PV
#define INCLUDE_ALLOW_GPL
#include "include_check.h"

#include "coproc_defs.h"

/*
 * Macros for accessing CP10.
 */
#define _ARM_CP10_MRCMCR_STR(_op1,_cr1,_cr2,_op2,_var) \
   " p10, " #_op1 ","#_var"," #_cr1 "," #_cr2 "," #_op2 "\n\t"

#define _ARM_MRC_CP10(_op1,_cr1,_cr2,_op2,_var) \
   asm volatile ("mrc" _ARM_CP10_MRCMCR_STR(_op1,_cr1,_cr2,_op2,%0) \
                 : "=r" (_var) )

#define ARM_MRC_CP10(_cp_reg,_var) _ARM_MRC_CP10(_cp_reg,_var)

#define _ARM_MCR_CP10(_op1,_cr1,_cr2,_op2,_val) \
   asm volatile ("mcr" _ARM_CP10_MRCMCR_STR(_op1,_cr1,_cr2,_op2,%0) \
                 : \
                 : "r" (_val) )

#define ARM_MCR_CP10(_cp_reg,_val) _ARM_MCR_CP10(_cp_reg,_val)


/*
 * Macros for accessing CP15.
 */
#define _ARM_CP15_MRCMCR_STR(_op1,_cr1,_cr2,_op2,_var) \
   " p15, " #_op1 ","#_var"," #_cr1 "," #_cr2 "," #_op2 "\n\t"

#define ARM_CP15_MRCMCR_STR(_cp_reg,_var) _ARM_CP15_MRCMCR_STR(_cp_reg,_var)

#ifdef __COVERITY__
static uint32 __cp15;
#define _ARM_MRC_CP15(_op1,_cr1,_cr2,_op2,_var) \
    (_var) = (uint32)__cp15
#else
#define _ARM_MRC_CP15(_op1,_cr1,_cr2,_op2,_var) \
   asm volatile ("mrc" _ARM_CP15_MRCMCR_STR(_op1,_cr1,_cr2,_op2,%0) \
                 : "=r" (_var) \
                 : \
                 : "memory")
#endif

#define ARM_MRC_CP15(_cp_reg,_var) _ARM_MRC_CP15(_cp_reg,_var)


#ifdef __COVERITY__
#define _ARM_MCR_CP15(_op1,_cr1,_cr2,_op2,_val) \
   __cp15 = (_val)
#else
#define _ARM_MCR_CP15(_op1,_cr1,_cr2,_op2,_val) \
   asm volatile ("mcr" _ARM_CP15_MRCMCR_STR(_op1,_cr1,_cr2,_op2,%0) \
                 : \
                 : "r" (_val)\
                 : "memory")
#endif

#define ARM_MCR_CP15(_cp_reg,_val) _ARM_MCR_CP15(_cp_reg,_val)

#define _ARM_MRRC_CP15(_op,_cr,_val1,_val2) \
   asm volatile ("mrrc p15, " #_op ",%0,%1," #_cr "\n\t" \
                 : "=r" (_val1), "=r" (_val2) \
                 : \
                 : "memory")

#define ARM_MRRC_CP15(_cp_reg,_val1,_val2) _ARM_MRRC_CP15(_cp_reg,_val1,_val2)

#define ARM_MRRC64_CP15(_cp_reg,_val) \
   _ARM_MRRC_CP15(_cp_reg,_val,*((uint8 *)&(_val) + 4))

#define _ARM_MCRR_CP15(_op,_cr,_val1,_val2) \
   asm volatile ("mcrr p15, " #_op ",%0,%1," #_cr "\n\t" \
                 : \
                 : "r" (_val1), "r" (_val2) \
                 : "memory")

#define ARM_MCRR_CP15(_cp_reg,_val1,_val2) _ARM_MCRR_CP15(_cp_reg,_val1,_val2)

#define ARM_MCRR64_CP15(_cp_reg,_val) \
   _ARM_MCRR_CP15(_cp_reg,_val,*((uint8 *)&(_val) + 4))

#define DMB() asm volatile ("dmb" : : : "memory")
#define DSB() asm volatile ("dsb" : : : "memory")
#define ISB() asm volatile ("isb" : : : "memory")

/**
 * @name 64-bit multiplies
 * @{
 */

// rdhi:rdlo = rm * rs + rdhi + rdlo
#define ARM_UMAAL(rdlo,rdhi,rm,rs) asm ("umaal %0,%1,%2,%3" \
                                        : "+r" (rdlo), "+r" (rdhi) \
                                        :  "r" (rm),    "r" (rs))

// rdhi:rdlo += rm * rs
#define ARM_UMLAL(rdlo,rdhi,rm,rs) asm ("umlal %0,%1,%2,%3" \
                                        : "+r" (rdlo), "+r" (rdhi) \
                                        :  "r" (rm),    "r" (rs))

// rdhi:rdlo = rm * rs
#define ARM_UMULL(rdlo,rdhi,rm,rs) asm ("umull %0,%1,%2,%3" \
                                        : "=r" (rdlo), "=r" (rdhi) \
                                        :  "r" (rm),    "r" (rs))
/*@}*/

/**
 * @brief Disable interrupts (IRQ + FIQ)
 *
 * @return CPSR status prior to disabling - suitable for passing to
 *         ARM_RestoreInterrupts() to restore IRQ/FIQ levels to
 *         pre-call values
 */
static inline uint32
ARM_DisableInterrupts(void)
{
   register uint32 status;

   asm volatile ("mrs    %0,     cpsr            \n\t"
                 "orr    r1,     %0,     %1      \n\t"
                 "msr    cpsr_c, r1              \n\t"
                 : "=&r" (status)
                 : "i" (ARM_PSR_I | ARM_PSR_F)
                 : "r1", "memory");

   return status;
}

/**
 * @brief Restore interrupts
 *
 * @param status return value from a previous call to ARM_DisableInterrupts()
 */
static inline void
ARM_RestoreInterrupts(uint32 status)
{
   asm volatile ("msr cpsr_c, %0 \n\t" : : "r" (status) : "memory");
}

/**
 * @brief Read current CPSR value
 *
 * @return current CPSR value
 */
static inline uint32
ARM_ReadCPSR(void)
{
   uint32 status;

   asm volatile ("mrs %0, cpsr \n\t" : "=r" (status));

   return status;
}

/**
 * @brief Read current stack pointer
 *
 * @return stack pointer value
 */
static inline uint32
ARM_ReadSP(void)
{
   uint32 sp;

   asm volatile ("mov %0, sp \n\t" : "=r" (sp));

   return sp;
}

#endif /// ifndef _ARM_GCC_INLINE_H_
