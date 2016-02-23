/*
 * Copyright 2012 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 */

/* Machine-generated file; do not edit. */

#ifndef __ARCH_TRIO_DEF_H__
#define __ARCH_TRIO_DEF_H__
#define TRIO_CFG_REGION_ADDR__REG_SHIFT 0
#define TRIO_CFG_REGION_ADDR__INTFC_SHIFT 16
#define TRIO_CFG_REGION_ADDR__INTFC_VAL_TRIO 0x0
#define TRIO_CFG_REGION_ADDR__INTFC_VAL_MAC_INTERFACE 0x1
#define TRIO_CFG_REGION_ADDR__INTFC_VAL_MAC_STANDARD 0x2
#define TRIO_CFG_REGION_ADDR__INTFC_VAL_MAC_PROTECTED 0x3
#define TRIO_CFG_REGION_ADDR__MAC_SEL_SHIFT 18
#define TRIO_CFG_REGION_ADDR__PROT_SHIFT 20
#define TRIO_PIO_REGIONS_ADDR__REGION_SHIFT 32
#define TRIO_MAP_MEM_REG_INT0 0x1000000000
#define TRIO_MAP_MEM_REG_INT1 0x1000000008
#define TRIO_MAP_MEM_REG_INT2 0x1000000010
#define TRIO_MAP_MEM_REG_INT3 0x1000000018
#define TRIO_MAP_MEM_REG_INT4 0x1000000020
#define TRIO_MAP_MEM_REG_INT5 0x1000000028
#define TRIO_MAP_MEM_REG_INT6 0x1000000030
#define TRIO_MAP_MEM_REG_INT7 0x1000000038
#define TRIO_MAP_MEM_LIM__ADDR_SHIFT 12
#define TRIO_MAP_MEM_SETUP__ORDER_MODE_VAL_UNORDERED 0x0
#define TRIO_MAP_MEM_SETUP__ORDER_MODE_VAL_STRICT 0x1
#define TRIO_MAP_MEM_SETUP__ORDER_MODE_VAL_REL_ORD 0x2
#define TRIO_TILE_PIO_REGION_SETUP_CFG_ADDR__MAC_SHIFT 30
#endif /* !defined(__ARCH_TRIO_DEF_H__) */
