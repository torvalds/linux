/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
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

#ifndef __ISYS_DMA_PUBLIC_H_INCLUDED__
#define __ISYS_DMA_PUBLIC_H_INCLUDED__

#ifdef USE_INPUT_SYSTEM_VERSION_2401

#include "system_local.h"
#include "type_support.h"

STORAGE_CLASS_ISYS2401_DMA_H void isys2401_dma_reg_store(
    const isys2401_dma_ID_t dma_id,
    const unsigned int	reg,
    const hrt_data		value);

STORAGE_CLASS_ISYS2401_DMA_H hrt_data isys2401_dma_reg_load(
    const isys2401_dma_ID_t dma_id,
    const unsigned int	reg);

void isys2401_dma_set_max_burst_size(
    const isys2401_dma_ID_t dma_id,
    uint32_t		max_burst_size);

#endif /* USE_INPUT_SYSTEM_VERSION_2401 */

#endif /* __ISYS_DMA_PUBLIC_H_INCLUDED__ */
