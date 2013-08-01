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
#ifndef __GXIO_MPIPE_INFO_LINUX_RPC_H__
#define __GXIO_MPIPE_INFO_LINUX_RPC_H__

#include <hv/iorpc.h>

#include <hv/drv_mpipe_intf.h>
#include <asm/page.h>
#include <gxio/kiorpc.h>
#include <gxio/mpipe.h>
#include <linux/string.h>
#include <linux/module.h>
#include <asm/pgtable.h>


#define GXIO_MPIPE_INFO_OP_INSTANCE_AUX IORPC_OPCODE(IORPC_FORMAT_NONE, 0x1250)
#define GXIO_MPIPE_INFO_OP_ENUMERATE_AUX IORPC_OPCODE(IORPC_FORMAT_NONE, 0x1251)
#define GXIO_MPIPE_INFO_OP_GET_MMIO_BASE IORPC_OPCODE(IORPC_FORMAT_NONE_NOUSER, 0x8000)
#define GXIO_MPIPE_INFO_OP_CHECK_MMIO_OFFSET IORPC_OPCODE(IORPC_FORMAT_NONE_NOUSER, 0x8001)


int gxio_mpipe_info_instance_aux(gxio_mpipe_info_context_t * context,
				 _gxio_mpipe_link_name_t name);

int gxio_mpipe_info_enumerate_aux(gxio_mpipe_info_context_t * context,
				  unsigned int idx,
				  _gxio_mpipe_link_name_t * name,
				  _gxio_mpipe_link_mac_t * mac);

int gxio_mpipe_info_get_mmio_base(gxio_mpipe_info_context_t * context,
				  HV_PTE *base);

int gxio_mpipe_info_check_mmio_offset(gxio_mpipe_info_context_t * context,
				      unsigned long offset, unsigned long size);

#endif /* !__GXIO_MPIPE_INFO_LINUX_RPC_H__ */
