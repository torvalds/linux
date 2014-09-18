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

/* This file is machine-generated; DO NOT EDIT! */
#ifndef __GXIO_TRIO_LINUX_RPC_H__
#define __GXIO_TRIO_LINUX_RPC_H__

#include <hv/iorpc.h>

#include <hv/drv_trio_intf.h>
#include <gxio/trio.h>
#include <gxio/kiorpc.h>
#include <linux/string.h>
#include <linux/module.h>
#include <asm/pgtable.h>

#define GXIO_TRIO_OP_DEALLOC_ASID      IORPC_OPCODE(IORPC_FORMAT_NONE, 0x1400)
#define GXIO_TRIO_OP_ALLOC_ASIDS       IORPC_OPCODE(IORPC_FORMAT_NONE, 0x1401)

#define GXIO_TRIO_OP_ALLOC_MEMORY_MAPS IORPC_OPCODE(IORPC_FORMAT_NONE, 0x1404)

#define GXIO_TRIO_OP_ALLOC_SCATTER_QUEUES IORPC_OPCODE(IORPC_FORMAT_NONE, 0x140e)
#define GXIO_TRIO_OP_ALLOC_PIO_REGIONS IORPC_OPCODE(IORPC_FORMAT_NONE, 0x1412)

#define GXIO_TRIO_OP_INIT_PIO_REGION_AUX IORPC_OPCODE(IORPC_FORMAT_NONE, 0x1414)

#define GXIO_TRIO_OP_INIT_MEMORY_MAP_MMU_AUX IORPC_OPCODE(IORPC_FORMAT_NONE_NOUSER, 0x141e)
#define GXIO_TRIO_OP_GET_PORT_PROPERTY IORPC_OPCODE(IORPC_FORMAT_NONE_NOUSER, 0x141f)
#define GXIO_TRIO_OP_CONFIG_LEGACY_INTR IORPC_OPCODE(IORPC_FORMAT_KERNEL_INTERRUPT, 0x1420)
#define GXIO_TRIO_OP_CONFIG_MSI_INTR   IORPC_OPCODE(IORPC_FORMAT_KERNEL_INTERRUPT, 0x1421)

#define GXIO_TRIO_OP_SET_MPS_MRS       IORPC_OPCODE(IORPC_FORMAT_NONE_NOUSER, 0x1423)
#define GXIO_TRIO_OP_FORCE_RC_LINK_UP  IORPC_OPCODE(IORPC_FORMAT_NONE_NOUSER, 0x1424)
#define GXIO_TRIO_OP_FORCE_EP_LINK_UP  IORPC_OPCODE(IORPC_FORMAT_NONE_NOUSER, 0x1425)
#define GXIO_TRIO_OP_GET_MMIO_BASE     IORPC_OPCODE(IORPC_FORMAT_NONE_NOUSER, 0x8000)
#define GXIO_TRIO_OP_CHECK_MMIO_OFFSET IORPC_OPCODE(IORPC_FORMAT_NONE_NOUSER, 0x8001)

int gxio_trio_alloc_asids(gxio_trio_context_t *context, unsigned int count,
			  unsigned int first, unsigned int flags);


int gxio_trio_alloc_memory_maps(gxio_trio_context_t *context,
				unsigned int count, unsigned int first,
				unsigned int flags);


int gxio_trio_alloc_scatter_queues(gxio_trio_context_t *context,
				   unsigned int count, unsigned int first,
				   unsigned int flags);

int gxio_trio_alloc_pio_regions(gxio_trio_context_t *context,
				unsigned int count, unsigned int first,
				unsigned int flags);

int gxio_trio_init_pio_region_aux(gxio_trio_context_t *context,
				  unsigned int pio_region, unsigned int mac,
				  uint32_t bus_address_hi, unsigned int flags);


int gxio_trio_init_memory_map_mmu_aux(gxio_trio_context_t *context,
				      unsigned int map, unsigned long va,
				      uint64_t size, unsigned int asid,
				      unsigned int mac, uint64_t bus_address,
				      unsigned int node,
				      unsigned int order_mode);

int gxio_trio_get_port_property(gxio_trio_context_t *context,
				struct pcie_trio_ports_property *trio_ports);

int gxio_trio_config_legacy_intr(gxio_trio_context_t *context, int inter_x,
				 int inter_y, int inter_ipi, int inter_event,
				 unsigned int mac, unsigned int intx);

int gxio_trio_config_msi_intr(gxio_trio_context_t *context, int inter_x,
			      int inter_y, int inter_ipi, int inter_event,
			      unsigned int mac, unsigned int mem_map,
			      uint64_t mem_map_base, uint64_t mem_map_limit,
			      unsigned int asid);


int gxio_trio_set_mps_mrs(gxio_trio_context_t *context, uint16_t mps,
			  uint16_t mrs, unsigned int mac);

int gxio_trio_force_rc_link_up(gxio_trio_context_t *context, unsigned int mac);

int gxio_trio_force_ep_link_up(gxio_trio_context_t *context, unsigned int mac);

int gxio_trio_get_mmio_base(gxio_trio_context_t *context, HV_PTE *base);

int gxio_trio_check_mmio_offset(gxio_trio_context_t *context,
				unsigned long offset, unsigned long size);

#endif /* !__GXIO_TRIO_LINUX_RPC_H__ */
