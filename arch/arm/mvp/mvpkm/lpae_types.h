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
 * @brief Large physical address extension types.
 *
 * See ARM PRD03-GENC-008469 11.0.
 */
#ifndef _LPAE_TYPES_H_
#define _LPAE_TYPES_H_

#define INCLUDE_ALLOW_MVPD
#define INCLUDE_ALLOW_VMX
#define INCLUDE_ALLOW_MODULE
#define INCLUDE_ALLOW_MONITOR
#define INCLUDE_ALLOW_PV
#define INCLUDE_ALLOW_GPL
#include "include_check.h"

#include "lpae_defs.h"

/**
 * @name ARM LPAE page table descriptors. See p7-8 ARM PRD03-GENC-008469 11.0.
 * @{
 */

#define LOWER_PAGE_ATTRIBUTES_STAGE1 \
   uint64 attrIndx : 3; \
   uint64 ns       : 1; \
   uint64 ap       : 2; \
   uint64 sh       : 2; \
   uint64 af       : 1; \
   uint64 ng       : 1;

#define LOWER_PAGE_ATTRIBUTES_STAGE2 \
   uint64 memAttr  : 4; \
   uint64 hap      : 2; \
   uint64 sh       : 2; \
   uint64 af       : 1; \
   uint64 sbzL     : 1;

#define UPPER_PAGE_ATTRIBUTES_STAGE1 \
   uint64 contig : 1; \
   uint64 pxn    : 1; \
   uint64 xn     : 1; \
   uint64 sw     : 4; \
   uint64 ignU   : 5;

#define UPPER_PAGE_ATTRIBUTES_STAGE2 \
   uint64 contig : 1; \
   uint64 sbzU   : 1; \
   uint64 xn     : 1; \
   uint64 sw     : 4; \
   uint64 ignU   : 5;


#define ARM_LPAE_DESC_TYPE(lvl,blen,sbzpad) \
   typedef union { \
      uint64 u; \
 \
      struct { \
         uint64 type  : 2;\
         uint64 ign   : 62; \
      } x; \
 \
      struct { \
         uint64 type  : 2;\
         LOWER_PAGE_ATTRIBUTES_STAGE1 \
         sbzpad \
         uint64 base  : blen; \
         uint64 sbz   : 12; \
         UPPER_PAGE_ATTRIBUTES_STAGE1 \
      } blockS1; \
 \
      struct { \
         uint64 type  : 2;\
         LOWER_PAGE_ATTRIBUTES_STAGE2 \
         sbzpad \
         uint64 base  : blen; \
         uint64 sbz   : 12; \
         UPPER_PAGE_ATTRIBUTES_STAGE2 \
      } blockS2; \
 \
      struct { \
         uint64 type : 2;\
         uint64 ign0 : 10; \
         uint64 base : 28; \
         uint64 sbz  : 12; \
         uint64 ign1 : 7; \
         uint64 pxn  : 1; \
         uint64 xn   : 1; \
         uint64 ap   : 2; \
         uint64 ns   : 1; \
      } table; \
 \
   } ARM_LPAE_L##lvl##D;


ARM_LPAE_DESC_TYPE(1, ARM_LPAE_L1D_BLOCK_BITS, uint64 sbzP : 18;)
ARM_LPAE_DESC_TYPE(2, ARM_LPAE_L2D_BLOCK_BITS, uint64 sbzP : 9;)
ARM_LPAE_DESC_TYPE(3, ARM_LPAE_L3D_BLOCK_BITS, )

/*@}*/

#endif /// ifndef _LPAE_TYPES_H_
