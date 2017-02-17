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

#ifndef __ISYS_DMA_GLOBAL_H_INCLUDED__
#define __ISYS_DMA_GLOBAL_H_INCLUDED__

#include <type_support.h>

#define HIVE_ISYS2401_DMA_IBUF_DDR_CONN	0
#define HIVE_ISYS2401_DMA_IBUF_VMEM_CONN	1
#define _DMA_V2_ZERO_EXTEND		0
#define _DMA_V2_SIGN_EXTEND		1

#define _DMA_ZERO_EXTEND     _DMA_V2_ZERO_EXTEND
#define _DMA_SIGN_EXTEND     _DMA_V2_SIGN_EXTEND

/********************************************************
 *
 * DMA Port.
 *
 * The DMA port definition for the input system
 * 2401 DMA is the duplication of the DMA port
 * definition for the CSS system DMA. It is duplicated
 * here just as the temporal step before the device libary
 * is available. The device libary is suppose to provide
 * the capability of reusing the control interface of the
 * same device prototypes. The refactor team will work on
 * this, right?
 *
 ********************************************************/
typedef struct isys2401_dma_port_cfg_s isys2401_dma_port_cfg_t;
struct isys2401_dma_port_cfg_s {
	uint32_t stride;
	uint32_t elements;
	uint32_t cropping;
	uint32_t width;
 };
/** end of DMA Port */

/************************************************
 *
 * DMA Device.
 *
 * The DMA device definition for the input system
 * 2401 DMA is the duplicattion of the DMA device
 * definition for the CSS system DMA. It is duplicated
 * here just as the temporal step before the device libary
 * is available. The device libary is suppose to provide
 * the capability of reusing the control interface of the
 * same device prototypes. The refactor team will work on
 * this, right?
 *
 ************************************************/
typedef enum {
	isys2401_dma_ibuf_to_ddr_connection	= HIVE_ISYS2401_DMA_IBUF_DDR_CONN,
	isys2401_dma_ibuf_to_vmem_connection	= HIVE_ISYS2401_DMA_IBUF_VMEM_CONN
} isys2401_dma_connection;

typedef enum {
  isys2401_dma_zero_extension = _DMA_ZERO_EXTEND,
  isys2401_dma_sign_extension = _DMA_SIGN_EXTEND
} isys2401_dma_extension;

typedef struct isys2401_dma_cfg_s isys2401_dma_cfg_t;
struct isys2401_dma_cfg_s {
	isys2401_dma_channel	channel;
	isys2401_dma_connection	connection;
	isys2401_dma_extension	extension;
	uint32_t		height;
};
/** end of DMA Device */

/* isys2401_dma_channel limits per DMA ID */
extern const isys2401_dma_channel N_ISYS2401_DMA_CHANNEL_PROCS[N_ISYS2401_DMA_ID];

#endif /* __ISYS_DMA_GLOBAL_H_INCLUDED__ */
