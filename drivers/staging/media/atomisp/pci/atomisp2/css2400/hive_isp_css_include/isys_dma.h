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

#ifndef __ISYS_DMA_H_INCLUDED__
#define __ISYS_DMA_H_INCLUDED__


/*
 * This file is included on every cell {SP,ISP,host} and on every system
 * that uses the input system device(s). It defines the API to DLI bridge
 *
 * System and cell specific interfaces and inline code are included
 * conditionally through Makefile path settings.
 *
 * - system and cell agnostic interfaces, constants and identifiers
 * - public:  system agnostic, cell specific interfaces
 * - private: system dependent, cell specific interfaces &
 *   inline implementations
 * - global:  system specific constants and identifiers
 * - local:   system and cell specific constants and identifiers
 */


#include "system_local.h"
#include "isys_dma_local.h"

#ifndef __INLINE_ISYS2401_DMA__
#define STORAGE_CLASS_ISYS2401_DMA_H extern
#define STORAGE_CLASS_ISYS2401_DMA_C
#include "isys_dma_public.h"
#else  /* __INLINE_ISYS2401_DMA__ */
#define STORAGE_CLASS_ISYS2401_DMA_H static inline
#define STORAGE_CLASS_ISYS2401_DMA_C static inline
#include "isys_dma_private.h"
#endif /* __INLINE_ISYS2401_DMA__ */

#endif /* __ISYS_DMA_H_INCLUDED__ */
