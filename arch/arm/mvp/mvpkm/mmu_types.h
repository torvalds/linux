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
 *  @brief MMU-related types.
 */

#ifndef _MMU_TYPES_H_
#define _MMU_TYPES_H_

#define INCLUDE_ALLOW_MVPD
#define INCLUDE_ALLOW_VMX
#define INCLUDE_ALLOW_MODULE
#define INCLUDE_ALLOW_MONITOR
#define INCLUDE_ALLOW_PV
#define INCLUDE_ALLOW_HOSTUSER
#define INCLUDE_ALLOW_GUESTUSER
#define INCLUDE_ALLOW_GPL
#include "include_check.h"

#include "mmu_defs.h"

/**
 * @brief ARM level 1 page table descriptor. See B3-8 ARM DDI 0406B.
 */
typedef union {
   uint32 u;

   struct {
      uint32  type    : 2;
      uint32  xx      : 30;
   } x;

   struct {
      uint32  type    : 2;
      uint32  sbz1    : 1;
      uint32  ns      : 1;
      uint32  sbz2    : 1;
      uint32  domain  : 4;
      uint32  imp     : 1;
      uint32  base    : 22;
   } coarse;

   struct {
      uint32  type    : 2;
      uint32  cb      : 2;
      uint32  xn      : 1;
      uint32  domain  : 4;
      uint32  imp     : 1;
      uint32  ap      : 2;
      uint32  tex     : 3;
      uint32  apx     : 1;
      uint32  s       : 1;
      uint32  ng      : 1;
      uint32  sbz     : 1;
      uint32  ns      : 1;
      uint32  base    : 12;
   } section;

   struct {
      uint32  type    : 2;
      uint32  cb      : 2;
      uint32  xn      : 1;
      uint32  xbase2  : 4;
      uint32  imp     : 1;
      uint32  ap      : 2;
      uint32  tex     : 3;
      uint32  apx     : 1;
      uint32  s       : 1;
      uint32  ng      : 1;
      uint32  sbo     : 1;
      uint32  ns      : 1;
      uint32  xbase1  : 4;
      uint32  base    : 8;
   } supersection;
} ARM_L1D;

/**
 * @brief ARM level 2 page table descriptor. See B3-10 ARM DDI 0406B.
 */
typedef union {
   uint32   u;

   struct {
      uint32  type    : 2;
      uint32  cb      : 2;
      uint32  xx      : 28;
   } x;

   struct {
      uint32  type    : 2;
      uint32  cb      : 2;
      uint32  ap      : 2;
      uint32  sbz     : 3;
      uint32  apx     : 1;
      uint32  s       : 1;
      uint32  ng      : 1;
      uint32  tex     : 3;
      uint32  xn      : 1;
      uint32  base    : 16;
   } large;

   struct {
      uint32  xn      : 1;
      uint32  type    : 1;
      uint32  cb      : 2;
      uint32  ap      : 2;
      uint32  tex     : 3;
      uint32  apx     : 1;
      uint32  s       : 1;
      uint32  ng      : 1;
      uint32  base    : 20;
   } small;
} ARM_L2D;

/**
 * @brief Get the simplified access permissions from a small L2 descriptor.
 *
 * @param l2D value of L2 descriptor.
 *
 * @return Simplified access permissions.
 */
static inline uint8
ARM_L2DSimpleAP(ARM_L2D l2D)
{
   ASSERT(l2D.small.type == ARM_L2D_TYPE_SMALL);
   return (l2D.small.apx << 1) | (l2D.small.ap >> 1);
}

/**
 * @brief Permissions for a page - intermediate format.
 */
typedef struct {
   uint8 ap  : 2;
   uint8 apx : 1;
   uint8 xn  : 1;
} ARM_AccessPerms;

/**
 * @brief ARM domain (0-15).
 */
typedef uint8 ARM_Domain;

/**
 * @brief ARM Domain Access Control Register, see B4.9.4 ARM DDI 0100I.
 */
typedef uint32 ARM_DACR;

/**
 * @brief ARM address space identifier.
 * 8-bits with an "invalid ASID" value
 * representation.
 */
typedef uint32 ARM_ASID;

#define ARM_INVALID_ASID ((uint32)(-1))

/**
 * @brief Page shareability property.
 *
 * LPAE encoding, see p8 ARM PRD03-GENC-008469 11.0.
 */
typedef enum {
   ARM_SHARE_ATTR_NONE,
   ARM_SHARE_ATTR_RESERVED,
   ARM_SHARE_ATTR_OUTER,
   ARM_SHARE_ATTR_INNER,
} PACKED ARM_ShareAttr;

/**
 * @brief Page cacheability property (TEX Remap disabled).
 *
 * ARM C/B bits, see B4.4.1 ARM DDI 0100I.
 */
typedef enum {
   ARM_CB_UNBUFFERED   = 0,
   ARM_CB_UNCACHED     = 1,
   ARM_CB_WRITETHROUGH = 2,
   ARM_CB_WRITEBACK    = 3
} PACKED ARM_CB;

/**
 * @brief Normal page cacheability property (TEX Remap enabled).
 *
 * NMRR encoding, see B3-146 ARM DDI 0406B.
 */
typedef enum {
   ARM_CACHE_ATTR_NORMAL_NONE,
   ARM_CACHE_ATTR_NORMAL_WB_WALLOC,
   ARM_CACHE_ATTR_NORMAL_WT,
   ARM_CACHE_ATTR_NORMAL_WB
} PACKED ARM_CacheAttrNormal;

/**
 * @brief Normal page memory attributes.
 *
 * Captures the general case of distinct inner/outer cacheability/shareability.
 * See A3-30 ARM DDI 0406B for a discussion of shareability domains and
 * cacheability attributes.
 */
typedef struct {
   ARM_ShareAttr share;
   ARM_CacheAttrNormal innerCache;
   ARM_CacheAttrNormal outerCache;
} ARM_MemAttrNormal;

#endif /// _MMU_TYPES_H_
