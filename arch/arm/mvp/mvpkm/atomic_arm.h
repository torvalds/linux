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
 * @brief bus-atomic operators, ARM implementation.
 * Do not include directly, include 'atomic.h' instead.
 * Memory where the atomic reside must be shared.
 *
 * These operations assume that the exclusive access monitor is cleared during
 * abort entry but they do not assume that cooperative scheduling (e.g. Linux
 * schedule()) clears the monitor and hence the use of "clrex" when required.
 */

#ifndef _ATOMIC_ARM_H
#define _ATOMIC_ARM_H

#define INCLUDE_ALLOW_MVPD
#define INCLUDE_ALLOW_VMX
#define INCLUDE_ALLOW_MODULE
#define INCLUDE_ALLOW_MONITOR
#define INCLUDE_ALLOW_PV
#define INCLUDE_ALLOW_GPL
#define INCLUDE_ALLOW_HOSTUSER
#define INCLUDE_ALLOW_GUESTUSER
#include "include_check.h"

#include "mvp_assert.h"

/**
 * @brief Atomic Add
 * @param atm atomic cell to operate on
 * @param modval value to apply to atomic cell
 * @return the original value of 'atm'
 */
#define ATOMIC_ADDO(atm,modval) ATOMIC_OPO_PRIVATE(atm,modval,add)

/**
 * @brief Atomic Add
 * @param atm atomic cell to operate on
 * @param modval value to apply to atomic cell
 * @return nothing
 */
#define ATOMIC_ADDV(atm,modval) ATOMIC_OPV_PRIVATE(atm,modval,add)

/**
 * @brief Atomic And
 * @param atm atomic cell to operate on
 * @param modval value to apply to atomic cell
 * @return the original value of 'atm'
 */
#define ATOMIC_ANDO(atm,modval) ATOMIC_OPO_PRIVATE(atm,modval,and)

/**
 * @brief Atomic And
 * @param atm atomic cell to operate on
 * @param modval value to apply to atomic cell
 * @return nothing
 */
#define ATOMIC_ANDV(atm,modval) ATOMIC_OPV_PRIVATE(atm,modval,and)

/**
 * @brief Retrieve an atomic value
 * @param atm atomic cell to operate on
 * @return the value of 'atm'
 */
#define ATOMIC_GETO(atm) ({ \
   typeof((atm).atm_Normal) _oldval;                        \
   switch (sizeof _oldval) {                                \
      case 4:                                               \
         asm volatile ("ldrex  %0, [%1]\n"                  \
                       "clrex"                              \
                       : "=&r" (_oldval)                    \
                       : "r"   (&((atm).atm_Volatl)));      \
         break;                                             \
      case 8:                                               \
         asm volatile ("ldrexd %0, %H0, [%1]\n"             \
                       "clrex"                              \
                       : "=&r" (_oldval)                    \
                       : "r"   (&((atm).atm_Volatl)));      \
         break;                                             \
      default:                                              \
         FATAL();                                           \
   }                                                        \
   _oldval;                                                 \
})

/**
 * @brief Atomic Or
 * @param atm atomic cell to operate on
 * @param modval value to apply to atomic cell
 * @return the original value of 'atm'
 */
#define ATOMIC_ORO(atm,modval) ATOMIC_OPO_PRIVATE(atm,modval,orr)

/**
 * @brief Atomic Or
 * @param atm atomic cell to operate on
 * @param modval value to apply to atomic cell
 * @return nothing
 */
#define ATOMIC_ORV(atm,modval) ATOMIC_OPV_PRIVATE(atm,modval,orr)

/**
 * @brief Atomic Conditional Write, ie,
 *        set 'atm' to 'newval' iff it was 'oldval'.
 * @param atm atomic cell to operate on
 * @param newval value to possibly write to atomic cell
 * @param oldval value that atomic cell must equal
 * @return 0 if failed; 1 if successful
 */
#define ATOMIC_SETIF(atm,newval,oldval) ({ \
   int _failed;                                        \
   typeof((atm).atm_Normal) _newval = newval;          \
   typeof((atm).atm_Normal) _oldval = oldval;          \
   ASSERT_ON_COMPILE(sizeof _newval == 4);             \
   asm volatile ("1: ldrex    %0, [%1]      \n"        \
                 "   cmp      %0, %2        \n"        \
                 "   mov      %0, #2        \n"        \
                 "   IT       eq            \n"        \
                 "   strexeq  %0, %3, [%1]  \n"        \
                 "   cmp      %0, #1        \n"        \
                 "   beq      1b            \n"        \
                 "   clrex"                            \
                 : "=&r" (_failed)                     \
                 : "r"   (&((atm).atm_Volatl)),        \
                   "r"   (_oldval),                    \
                   "r"   (_newval)                     \
                 : "cc", "memory");                    \
   !_failed;                                           \
})


/**
 * @brief Atomic Write (unconditional)
 * @param atm atomic cell to operate on
 * @param newval value to write to atomic cell
 * @return the original value of 'atm'
 */
#define ATOMIC_SETO(atm,newval) ({ \
   int _failed;                                        \
   typeof((atm).atm_Normal) _newval = newval;          \
   typeof((atm).atm_Normal) _oldval;                   \
   switch (sizeof _newval) {                           \
      case 4:                                          \
         asm volatile ("1: ldrex   %0, [%2]\n"         \
                       "   strex   %1, %3, [%2]\n"     \
                       "   teq     %1, #0\n"           \
                       "   bne     1b"                 \
                       : "=&r" (_oldval),              \
                         "=&r" (_failed)               \
                       : "r"   (&((atm).atm_Volatl)),  \
                         "r"   (_newval)               \
                       : "cc", "memory");              \
         break;                                        \
      case 8:                                          \
         asm volatile ("1: ldrexd  %0, %H0, [%2]\n"    \
                       "   strexd  %1, %3, %H3, [%2]\n"\
                       "   teq     %1, #0\n"           \
                       "   bne     1b"                 \
                       : "=&r" (_oldval),              \
                         "=&r" (_failed)               \
                       : "r"   (&((atm).atm_Volatl)),  \
                         "r"   (_newval)               \
                       : "cc", "memory");              \
         break;                                        \
      default:                                         \
         FATAL();                                      \
   }                                                   \
   _oldval;                                            \
})

/**
 * @brief Atomic Write (unconditional)
 * @param atm atomic cell to operate on
 * @param newval value to write to atomic cell
 * @return nothing
 */
#define ATOMIC_SETV(atm,newval) do { ATOMIC_SETO((atm),(newval)); } while (0)

/**
 * @brief Atomic Subtract
 * @param atm atomic cell to operate on
 * @param modval value to apply to atomic cell
 * @return the original value of 'atm'
 */
#define ATOMIC_SUBO(atm,modval) ATOMIC_OPO_PRIVATE(atm,modval,sub)

/**
 * @brief Atomic Subtract
 * @param atm atomic cell to operate on
 * @param modval value to apply to atomic cell
 * @return nothing
 */
#define ATOMIC_SUBV(atm,modval) ATOMIC_OPV_PRIVATE(atm,modval,sub)

/**
 * @brief Atomic Generic Binary Operation
 * @param atm atomic cell to operate on
 * @param modval value to apply to atomic cell
 * @param op ARM instruction (add, and, orr, etc)
 * @return the original value of 'atm'
 */
#define ATOMIC_OPO_PRIVATE(atm,modval,op) ({ \
   int _failed;                                        \
   typeof((atm).atm_Normal) _modval = modval;          \
   typeof((atm).atm_Normal) _oldval;                   \
   typeof((atm).atm_Normal) _newval;                   \
   ASSERT_ON_COMPILE(sizeof _modval == 4);             \
   asm volatile ("1: ldrex    %0, [%3]\n"              \
                     #op "    %1, %0, %4\n"            \
                 "   strex    %2, %1, [%3]\n"          \
                 "   teq      %2, #0\n"                \
                 "   bne      1b"                      \
                 : "=&r" (_oldval),                    \
                   "=&r" (_newval),                    \
                   "=&r" (_failed)                     \
                 : "r"   (&((atm).atm_Volatl)),        \
                   "r"   (_modval)                     \
                 : "memory");                          \
   _oldval;                                            \
})

/**
 * @brief Atomic Generic Binary Operation
 * @param atm atomic cell to operate on
 * @param modval value to apply to atomic cell
 * @param op ARM instruction (add, and, orr, etc)
 * @return nothing
 */
#define ATOMIC_OPV_PRIVATE(atm,modval,op) do { \
   int _failed;                                        \
   typeof((atm).atm_Normal) _modval = modval;          \
   typeof((atm).atm_Normal) _sample;                   \
   ASSERT_ON_COMPILE(sizeof _modval == 4);             \
   asm volatile ("1: ldrex    %0, [%2]\n"              \
                     #op "    %0, %3\n"                \
                 "   strex    %1, %0, [%2]\n"          \
                 "   teq      %1, #0\n"                \
                 "   bne      1b"                      \
                 : "=&r" (_sample),                    \
                   "=&r" (_failed)                     \
                 : "r"   (&((atm).atm_Volatl)),        \
                   "r"   (_modval)                     \
                 : "memory");                          \
} while (0)

/**
 * @brief Single-copy atomic word write.
 *
 * ARMv7 defines world-aligned word writes to be single-copy atomic. See
 * A3-26 ARM DDI 0406A.
 *
 * @param p word aligned location to write to
 * @param val word-sized value to write to p
 */
#define ATOMIC_SINGLE_COPY_WRITE32(p,val)        \
   do {                                          \
      ASSERT(sizeof(val) == 4);                  \
      ASSERT((MVA)(p) % sizeof(val) == 0);       \
      asm volatile("str %0, [%1]"                \
                   :                             \
                   : "r" (val), "r" (p)          \
                   : "memory");                  \
   } while (0);


/**
 * @brief Single-copy atomic word read.
 *
 * ARMv7 defines world-aligned word reads to be single-copy atomic. See
 * A3-26 ARM DDI 0406A.
 *
 * @param p word aligned location to read from
 *
 * @return word-sized value from p
 */
#define ATOMIC_SINGLE_COPY_READ32(p) ({          \
   ASSERT((MVA)(p) % sizeof(uint32) == 0);       \
   uint32 _val;                                  \
   asm volatile("ldr %0, [%1]"                   \
                   : "=r" (_val)                 \
                   : "r" (p)                     \
                   );                            \
   _val;                                         \
})

/**
 * @brief Single-copy atomic double word write.
 *
 * LPAE defines double world-aligned double word writes to be single-copy
 * atomic. See 6.7 ARM PRD03-GENC-008469 13.0.
 *
 * @param p double word aligned location to write to
 * @param val double word-sized value to write to p
 */
#define ATOMIC_SINGLE_COPY_WRITE64(p,val)        \
   do {                                          \
      ASSERT(sizeof(val) == 8);                  \
      ASSERT((MVA)(p) % sizeof(val) == 0);       \
      asm volatile("mov  r0, %0        \n"       \
                   "mov  r1, %1        \n"       \
                   "strd r0, r1, [%2]"           \
                   :                             \
                   : "r" ((uint32)(val)),        \
                     "r" (((uint64)(val)) >> 32),\
                     "r" (p)                     \
                   : "r0", "r1", "memory");      \
   } while (0);

#endif
