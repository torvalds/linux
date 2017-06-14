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

#ifndef __DMA_GLOBAL_H_INCLUDED__
#define __DMA_GLOBAL_H_INCLUDED__

#include <type_support.h>

#define IS_DMA_VERSION_2

#define HIVE_ISP_NUM_DMA_CONNS		3
#define HIVE_ISP_NUM_DMA_CHANNELS	32

#define N_DMA_CHANNEL_ID	HIVE_ISP_NUM_DMA_CHANNELS

#include "dma_v2_defs.h"

/*
 * Command token bit mappings
 *
 * transfer / config
 *    param id[4] channel id[5] cmd id[6]
 *	| b14 .. b11 | b10 ... b6 | b5 ... b0 |
 *
 *
 * fast transfer:
 *    height[5]     width[8]      width[8]  channel id[5] cmd id[6]
 *	| b31 .. b26 | b25 .. b18 | b17 .. b11 | b10 ... b6 | b5 ... b0 |
 *
 */

#define _DMA_PACKING_SETUP_PARAM	_DMA_V2_PACKING_SETUP_PARAM
#define _DMA_HEIGHT_PARAM			_DMA_V2_HEIGHT_PARAM
#define _DMA_STRIDE_A_PARAM			_DMA_V2_STRIDE_A_PARAM
#define _DMA_ELEM_CROPPING_A_PARAM	_DMA_V2_ELEM_CROPPING_A_PARAM
#define _DMA_WIDTH_A_PARAM			_DMA_V2_WIDTH_A_PARAM
#define _DMA_STRIDE_B_PARAM			_DMA_V2_STRIDE_B_PARAM
#define _DMA_ELEM_CROPPING_B_PARAM	_DMA_V2_ELEM_CROPPING_B_PARAM
#define _DMA_WIDTH_B_PARAM			_DMA_V2_WIDTH_B_PARAM

#define _DMA_ZERO_EXTEND     _DMA_V2_ZERO_EXTEND
#define _DMA_SIGN_EXTEND     _DMA_V2_SIGN_EXTEND


typedef unsigned int dma_channel;

typedef enum {
  dma_isp_to_bus_connection = HIVE_DMA_ISP_BUS_CONN,
  dma_isp_to_ddr_connection = HIVE_DMA_ISP_DDR_CONN,
  dma_bus_to_ddr_connection = HIVE_DMA_BUS_DDR_CONN,
} dma_connection;

typedef enum {
  dma_zero_extension = _DMA_ZERO_EXTEND,
  dma_sign_extension = _DMA_SIGN_EXTEND
} dma_extension;


#define DMA_PROP_SHIFT(val, param)       ((val) << _DMA_V2_ ## param ## _IDX)
#define DMA_PROP_MASK(param)             ((1U << _DMA_V2_ ## param ## _BITS)-1)
#define DMA_PACK(val, param)             DMA_PROP_SHIFT((val) & DMA_PROP_MASK(param), param)

#define DMA_PACK_COMMAND(cmd)            DMA_PACK(cmd, CMD)
#define DMA_PACK_CHANNEL(ch)             DMA_PACK(ch,  CHANNEL)
#define DMA_PACK_PARAM(par)              DMA_PACK(par, PARAM)
#define DMA_PACK_EXTENSION(ext)          DMA_PACK(ext, EXTENSION)
#define DMA_PACK_LEFT_CROPPING(lc)       DMA_PACK(lc,  LEFT_CROPPING)
#define DMA_PACK_WIDTH_A(w)              DMA_PACK(w,   SPEC_DEV_A_XB)
#define DMA_PACK_WIDTH_B(w)              DMA_PACK(w,   SPEC_DEV_B_XB)
#define DMA_PACK_HEIGHT(h)               DMA_PACK(h,   SPEC_YB)

#define DMA_PACK_CMD_CHANNEL(cmd, ch)	 (DMA_PACK_COMMAND(cmd) | DMA_PACK_CHANNEL(ch))
#define DMA_PACK_SETUP(conn, ext)        ((conn) | DMA_PACK_EXTENSION(ext))
#define DMA_PACK_CROP_ELEMS(elems, crop) ((elems) | DMA_PACK_LEFT_CROPPING(crop))

#define hive_dma_snd(dma_id, token) OP_std_snd(dma_id, (unsigned int)(token))

#define DMA_PACK_BLOCK_CMD(cmd, ch, width_a, width_b, height) \
  (DMA_PACK_COMMAND(cmd)     | \
   DMA_PACK_CHANNEL(ch)      | \
   DMA_PACK_WIDTH_A(width_a) | \
   DMA_PACK_WIDTH_B(width_b) | \
   DMA_PACK_HEIGHT(height))

#define hive_dma_move_data(dma_id, read, channel, addr_a, addr_b, to_is_var, from_is_var) \
{ \
  hive_dma_snd(dma_id, DMA_PACK(_DMA_V2_SET_CRUN_COMMAND, CMD)); \
  hive_dma_snd(dma_id, DMA_PACK_CMD_CHANNEL(read?_DMA_V2_MOVE_B2A_COMMAND:_DMA_V2_MOVE_A2B_COMMAND, channel)); \
  hive_dma_snd(dma_id, read?(unsigned)(addr_b):(unsigned)(addr_a)); \
  hive_dma_snd(dma_id, read?(unsigned)(addr_a):(unsigned)(addr_b)); \
  hive_dma_snd(dma_id, to_is_var); \
  hive_dma_snd(dma_id, from_is_var); \
}
#define hive_dma_move_data_no_ack(dma_id, read, channel, addr_a, addr_b, to_is_var, from_is_var) \
{ \
  hive_dma_snd(dma_id, DMA_PACK(_DMA_V2_SET_CRUN_COMMAND, CMD)); \
  hive_dma_snd(dma_id, DMA_PACK_CMD_CHANNEL(read?_DMA_V2_NO_ACK_MOVE_B2A_NO_SYNC_CHK_COMMAND:_DMA_V2_NO_ACK_MOVE_A2B_NO_SYNC_CHK_COMMAND, channel)); \
  hive_dma_snd(dma_id, read?(unsigned)(addr_b):(unsigned)(addr_a)); \
  hive_dma_snd(dma_id, read?(unsigned)(addr_a):(unsigned)(addr_b)); \
  hive_dma_snd(dma_id, to_is_var); \
  hive_dma_snd(dma_id, from_is_var); \
}

#define hive_dma_move_b2a_data(dma_id, channel, to_addr, from_addr, to_is_var, from_is_var) \
{ \
  hive_dma_move_data(dma_id, true, channel, to_addr, from_addr, to_is_var, from_is_var) \
}

#define hive_dma_move_a2b_data(dma_id, channel, from_addr, to_addr, from_is_var, to_is_var) \
{ \
  hive_dma_move_data(dma_id, false, channel, from_addr, to_addr, from_is_var, to_is_var) \
}

#define hive_dma_set_data(dma_id, channel, address, value, is_var) \
{ \
  hive_dma_snd(dma_id, DMA_PACK(_DMA_V2_SET_CRUN_COMMAND, CMD)); \
  hive_dma_snd(dma_id, DMA_PACK_CMD_CHANNEL(_DMA_V2_INIT_A_COMMAND, channel)); \
  hive_dma_snd(dma_id, value); \
  hive_dma_snd(dma_id, address); \
  hive_dma_snd(dma_id, is_var); \
}

#define hive_dma_clear_data(dma_id, channel, address, is_var) hive_dma_set_data(dma_id, channel, address, 0, is_var)

#define hive_dma_configure(dma_id, channel, connection, extension, height, \
	stride_A, elems_A, cropping_A, width_A, \
	stride_B, elems_B, cropping_B, width_B) \
{ \
  hive_dma_snd(dma_id, DMA_PACK_CMD_CHANNEL(_DMA_V2_CONFIG_CHANNEL_COMMAND, channel)); \
  hive_dma_snd(dma_id, DMA_PACK_SETUP(connection, extension)); \
  hive_dma_snd(dma_id, stride_A); \
  hive_dma_snd(dma_id, DMA_PACK_CROP_ELEMS(elems_A, cropping_A)); \
  hive_dma_snd(dma_id, width_A); \
  hive_dma_snd(dma_id, stride_B); \
  hive_dma_snd(dma_id, DMA_PACK_CROP_ELEMS(elems_B, cropping_B)); \
  hive_dma_snd(dma_id, width_B); \
  hive_dma_snd(dma_id, height); \
}

#define hive_dma_execute(dma_id, channel, cmd, to_addr, from_addr_value, to_is_var, from_is_var) \
{ \
  hive_dma_snd(dma_id, DMA_PACK(_DMA_V2_SET_CRUN_COMMAND, CMD)); \
  hive_dma_snd(dma_id, DMA_PACK_CMD_CHANNEL(cmd, channel)); \
  hive_dma_snd(dma_id, to_addr); \
  hive_dma_snd(dma_id, from_addr_value); \
  hive_dma_snd(dma_id, to_is_var); \
  if ((cmd & DMA_CLEAR_CMDBIT) == 0) { \
	hive_dma_snd(dma_id, from_is_var); \
  } \
}

#define hive_dma_configure_fast(dma_id, channel, connection, extension, elems_A, elems_B) \
{ \
  hive_dma_snd(dma_id, DMA_PACK_CMD_CHANNEL(_DMA_V2_CONFIG_CHANNEL_COMMAND, channel)); \
  hive_dma_snd(dma_id, DMA_PACK_SETUP(connection, extension)); \
  hive_dma_snd(dma_id, 0); \
  hive_dma_snd(dma_id, DMA_PACK_CROP_ELEMS(elems_A, 0)); \
  hive_dma_snd(dma_id, 0); \
  hive_dma_snd(dma_id, 0); \
  hive_dma_snd(dma_id, DMA_PACK_CROP_ELEMS(elems_B, 0)); \
  hive_dma_snd(dma_id, 0); \
  hive_dma_snd(dma_id, 1); \
}

#define hive_dma_set_parameter(dma_id, channel, param, value) \
{ \
  hive_dma_snd(dma_id, _DMA_V2_SET_CHANNEL_PARAM_COMMAND | DMA_PACK_CHANNEL(channel) | DMA_PACK_PARAM(param)); \
  hive_dma_snd(dma_id, value); \
}

#define	DMA_SPECIFIC_CMDBIT	0x01
#define	DMA_CHECK_CMDBIT	0x02
#define	DMA_RW_CMDBIT		0x04
#define	DMA_CLEAR_CMDBIT	0x08
#define	DMA_ACK_CMDBIT		0x10
#define	DMA_CFG_CMDBIT		0x20
#define	DMA_PARAM_CMDBIT	0x01

/* Write complete check not necessary if there's no ack */
#define	DMA_NOACK_CMD		(DMA_ACK_CMDBIT | DMA_CHECK_CMDBIT)
#define	DMA_CFG_CMD			(DMA_CFG_CMDBIT)
#define	DMA_CFGPARAM_CMD	(DMA_CFG_CMDBIT | DMA_PARAM_CMDBIT)

#define DMA_CMD_NEEDS_ACK(cmd) ((cmd & DMA_NOACK_CMD) == 0)
#define DMA_CMD_IS_TRANSFER(cmd) ((cmd & DMA_CFG_CMDBIT) == 0)
#define DMA_CMD_IS_WR(cmd) ((cmd & DMA_RW_CMDBIT) != 0)
#define DMA_CMD_IS_RD(cmd) ((cmd & DMA_RW_CMDBIT) == 0)
#define DMA_CMD_IS_CLR(cmd) ((cmd & DMA_CLEAR_CMDBIT) != 0)
#define DMA_CMD_IS_CFG(cmd) ((cmd & DMA_CFG_CMDBIT) != 0)
#define DMA_CMD_IS_PARAMCFG(cmd) ((cmd & DMA_CFGPARAM_CMD) == DMA_CFGPARAM_CMD)

/* As a matter of convention */
#define DMA_TRANSFER_READ		DMA_TRANSFER_B2A
#define DMA_TRANSFER_WRITE		DMA_TRANSFER_A2B
/* store/load from the PoV of the system(memory) */
#define DMA_TRANSFER_STORE		DMA_TRANSFER_B2A
#define DMA_TRANSFER_LOAD		DMA_TRANSFER_A2B
#define DMA_TRANSFER_CLEAR		DMA_TRANSFER_CLEAR_A

typedef enum {
	DMA_TRANSFER_CLEAR_A = DMA_CLEAR_CMDBIT,                                       /* 8 */
	DMA_TRANSFER_CLEAR_B = DMA_CLEAR_CMDBIT | DMA_RW_CMDBIT,                       /* 12 */
	DMA_TRANSFER_A2B = DMA_RW_CMDBIT,                                              /* 4 */
	DMA_TRANSFER_B2A = 0,                                                          /* 0 */
	DMA_TRANSFER_CLEAR_A_NOACK = DMA_CLEAR_CMDBIT | DMA_NOACK_CMD,                 /* 26 */
	DMA_TRANSFER_CLEAR_B_NOACK = DMA_CLEAR_CMDBIT | DMA_RW_CMDBIT | DMA_NOACK_CMD, /* 30 */
	DMA_TRANSFER_A2B_NOACK = DMA_RW_CMDBIT | DMA_NOACK_CMD,                        /* 22 */
	DMA_TRANSFER_B2A_NOACK = DMA_NOACK_CMD,                                        /* 18 */
	DMA_FASTTRANSFER_CLEAR_A = DMA_CLEAR_CMDBIT | DMA_SPECIFIC_CMDBIT,
	DMA_FASTTRANSFER_CLEAR_B = DMA_CLEAR_CMDBIT | DMA_RW_CMDBIT | DMA_SPECIFIC_CMDBIT,
	DMA_FASTTRANSFER_A2B = DMA_RW_CMDBIT | DMA_SPECIFIC_CMDBIT,
	DMA_FASTTRANSFER_B2A = DMA_SPECIFIC_CMDBIT,
	DMA_FASTTRANSFER_CLEAR_A_NOACK = DMA_CLEAR_CMDBIT | DMA_NOACK_CMD | DMA_SPECIFIC_CMDBIT,
	DMA_FASTTRANSFER_CLEAR_B_NOACK = DMA_CLEAR_CMDBIT | DMA_RW_CMDBIT | DMA_NOACK_CMD | DMA_SPECIFIC_CMDBIT,
	DMA_FASTTRANSFER_A2B_NOACK = DMA_RW_CMDBIT | DMA_NOACK_CMD | DMA_SPECIFIC_CMDBIT,
	DMA_FASTTRANSFER_B2A_NOACK = DMA_NOACK_CMD | DMA_SPECIFIC_CMDBIT,
} dma_transfer_type_t;

typedef enum {
	DMA_CONFIG_SETUP = _DMA_V2_PACKING_SETUP_PARAM,
	DMA_CONFIG_HEIGHT = _DMA_V2_HEIGHT_PARAM,
	DMA_CONFIG_STRIDE_A_ = _DMA_V2_STRIDE_A_PARAM,
	DMA_CONFIG_CROP_ELEM_A = _DMA_V2_ELEM_CROPPING_A_PARAM,
	DMA_CONFIG_WIDTH_A = _DMA_V2_WIDTH_A_PARAM,
	DMA_CONFIG_STRIDE_B_ = _DMA_V2_STRIDE_B_PARAM,
	DMA_CONFIG_CROP_ELEM_B = _DMA_V2_ELEM_CROPPING_B_PARAM,
	DMA_CONFIG_WIDTH_B = _DMA_V2_WIDTH_B_PARAM,
} dma_config_type_t;

struct dma_port_config {
	uint8_t  crop, elems;
	uint16_t width;
	uint32_t stride;
};

/* Descriptor for dma configuration */
struct dma_channel_config {
	uint8_t  connection;
	uint8_t  extension;
	uint8_t  height;
	struct dma_port_config a, b;
};

#endif /* __DMA_GLOBAL_H_INCLUDED__ */
