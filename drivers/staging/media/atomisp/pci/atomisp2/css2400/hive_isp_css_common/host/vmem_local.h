/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2010-2015, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#ifndef __VMEM_LOCAL_H_INCLUDED__
#define __VMEM_LOCAL_H_INCLUDED__

#include "type_support.h"
#include "vmem_global.h"

typedef uint16_t t_vmem_elem;

#define VMEM_ARRAY(x,s)    t_vmem_elem x[s/ISP_NWAY][ISP_NWAY]

void isp_vmem_load(
	const isp_ID_t		ID,
	const t_vmem_elem	*from,
	t_vmem_elem		*to,
	unsigned		elems); /* In t_vmem_elem */

void isp_vmem_store(
	const isp_ID_t		ID,
	t_vmem_elem		*to,
	const t_vmem_elem	*from,
	unsigned		elems); /* In t_vmem_elem */

void isp_vmem_2d_load (
	const isp_ID_t		ID,
	const t_vmem_elem	*from,
	t_vmem_elem		*to,
	unsigned		height,
	unsigned		width,
	unsigned		stride_to,  /* In t_vmem_elem */
	unsigned		stride_from /* In t_vmem_elem */);

void isp_vmem_2d_store (
	const isp_ID_t		ID,
	t_vmem_elem		*to,
	const t_vmem_elem	*from,
	unsigned		height,
	unsigned		width,
	unsigned		stride_to,  /* In t_vmem_elem */
	unsigned		stride_from /* In t_vmem_elem */);

#endif /* __VMEM_LOCAL_H_INCLUDED__ */
