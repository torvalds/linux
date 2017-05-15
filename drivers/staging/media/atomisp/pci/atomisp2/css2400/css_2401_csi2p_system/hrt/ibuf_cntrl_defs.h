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

#ifndef _ibuf_cntrl_defs_h_
#define _ibuf_cntrl_defs_h_

#include <stream2mmio_defs.h>
#include <dma_v2_defs.h>

#define _IBUF_CNTRL_REG_ALIGN 4
  /* alignment of register banks, first bank are shared configuration and status registers: */
#define _IBUF_CNTRL_PROC_REG_ALIGN        32

  /* the actual amount of configuration registers per proc: */
#define _IBUF_CNTRL_CONFIG_REGS_PER_PROC 18
  /* the actual amount of shared configuration registers: */
#define _IBUF_CNTRL_CONFIG_REGS_NO_PROC  0

  /* the actual amount of status registers per proc */
#define _IBUF_CNTRL_STATUS_REGS_PER_PROC (_IBUF_CNTRL_CONFIG_REGS_PER_PROC + 10)
  /* the actual amount shared status registers */
#define _IBUF_CNTRL_STATUS_REGS_NO_PROC  (_IBUF_CNTRL_CONFIG_REGS_NO_PROC + 2)

  /* time out bits, maximum time out value is 2^_IBUF_CNTRL_TIME_OUT_BITS - 1 */
#define _IBUF_CNTRL_TIME_OUT_BITS         5

/* command token definition */
#define _IBUF_CNTRL_CMD_TOKEN_LSB          0
#define _IBUF_CNTRL_CMD_TOKEN_MSB          1

/* Str2MMIO defines */
#define _IBUF_CNTRL_STREAM2MMIO_CMD_TOKEN_MSB        _STREAM2MMIO_CMD_TOKEN_CMD_MSB
#define _IBUF_CNTRL_STREAM2MMIO_CMD_TOKEN_LSB        _STREAM2MMIO_CMD_TOKEN_CMD_LSB
#define _IBUF_CNTRL_STREAM2MMIO_NUM_ITEMS_BITS       _STREAM2MMIO_PACK_NUM_ITEMS_BITS
#define _IBUF_CNTRL_STREAM2MMIO_ACK_EOF_BIT          _STREAM2MMIO_PACK_ACK_EOF_BIT
#define _IBUF_CNTRL_STREAM2MMIO_ACK_TOKEN_VALID_BIT  _STREAM2MMIO_ACK_TOKEN_VALID_BIT

/* acknowledge token definition */
#define _IBUF_CNTRL_ACK_TOKEN_STORES_IDX    0
#define _IBUF_CNTRL_ACK_TOKEN_STORES_BITS   15
#define _IBUF_CNTRL_ACK_TOKEN_ITEMS_IDX     (_IBUF_CNTRL_ACK_TOKEN_STORES_BITS + _IBUF_CNTRL_ACK_TOKEN_STORES_IDX)
#define _IBUF_CNTRL_ACK_TOKEN_ITEMS_BITS    _STREAM2MMIO_PACK_NUM_ITEMS_BITS
#define _IBUF_CNTRL_ACK_TOKEN_LSB          _IBUF_CNTRL_ACK_TOKEN_STORES_IDX
#define _IBUF_CNTRL_ACK_TOKEN_MSB          (_IBUF_CNTRL_ACK_TOKEN_ITEMS_BITS + _IBUF_CNTRL_ACK_TOKEN_ITEMS_IDX - 1)
          /* bit 31 indicates a valid ack: */
#define _IBUF_CNTRL_ACK_TOKEN_VALID_BIT    (_IBUF_CNTRL_ACK_TOKEN_ITEMS_BITS + _IBUF_CNTRL_ACK_TOKEN_ITEMS_IDX)


/*shared registers:*/
#define _IBUF_CNTRL_RECALC_WORDS_STATUS     0
#define _IBUF_CNTRL_ARBITERS_STATUS         1

#define _IBUF_CNTRL_SET_CRUN                2 /* NO PHYSICAL REGISTER!! Only used in HSS model */


/*register addresses for each proc: */
#define _IBUF_CNTRL_CMD                   0
#define _IBUF_CNTRL_ACK                   1

        /* number of items (packets or words) per frame: */
#define _IBUF_CNTRL_NUM_ITEMS_PER_STORE   2

        /* number of stores (packets or words) per store/buffer: */
#define _IBUF_CNTRL_NUM_STORES_PER_FRAME  3

        /* the channel and command in the DMA */
#define _IBUF_CNTRL_DMA_CHANNEL           4
#define _IBUF_CNTRL_DMA_CMD               5

        /* the start address and stride of the buffers */
#define _IBUF_CNTRL_BUFFER_START_ADDRESS  6
#define _IBUF_CNTRL_BUFFER_STRIDE         7
#define _IBUF_CNTRL_BUFFER_END_ADDRESS    8

        /* destination start address, stride and end address; should be the same as in the DMA */
#define _IBUF_CNTRL_DEST_START_ADDRESS    9
#define _IBUF_CNTRL_DEST_STRIDE           10
#define _IBUF_CNTRL_DEST_END_ADDRESS      11

        /* send a frame sync or not, default 1 */
#define _IBUF_CNTRL_SYNC_FRAME            12

        /* str2mmio cmds */
#define _IBUF_CNTRL_STR2MMIO_SYNC_CMD     13
#define _IBUF_CNTRL_STR2MMIO_STORE_CMD    14

        /* num elems p word*/
#define _IBUF_CNTRL_SHIFT_ITEMS           15
#define _IBUF_CNTRL_ELEMS_P_WORD_IBUF     16
#define _IBUF_CNTRL_ELEMS_P_WORD_DEST     17


   /* STATUS */
        /* current frame and stores in buffer */
#define _IBUF_CNTRL_CUR_STORES            18
#define _IBUF_CNTRL_CUR_ACKS              19

        /* current buffer and destination address for DMA cmd's */
#define _IBUF_CNTRL_CUR_S2M_IBUF_ADDR     20
#define _IBUF_CNTRL_CUR_DMA_IBUF_ADDR     21
#define _IBUF_CNTRL_CUR_DMA_DEST_ADDR     22
#define _IBUF_CNTRL_CUR_ISP_DEST_ADDR     23

#define _IBUF_CNTRL_CUR_NR_DMA_CMDS_SEND  24

#define _IBUF_CNTRL_MAIN_CNTRL_STATE      25
#define _IBUF_CNTRL_DMA_SYNC_STATE        26
#define _IBUF_CNTRL_ISP_SYNC_STATE        27


/*Commands: */
#define _IBUF_CNTRL_CMD_STORE_FRAME_IDX     0
#define _IBUF_CNTRL_CMD_ONLINE_IDX          1

  /* initialize, copy st_addr to cur_addr etc */
#define _IBUF_CNTRL_CMD_INITIALIZE          0

  /* store an online frame (sync with ISP, use end cfg start, stride and end address: */
#define _IBUF_CNTRL_CMD_STORE_ONLINE_FRAME  ((1<<_IBUF_CNTRL_CMD_STORE_FRAME_IDX) | (1<<_IBUF_CNTRL_CMD_ONLINE_IDX))

  /* store an offline frame (don't sync with ISP, requires start address as 2nd token, no end address: */
#define _IBUF_CNTRL_CMD_STORE_OFFLINE_FRAME  (1<<_IBUF_CNTRL_CMD_STORE_FRAME_IDX)

  /* false command token, should be different then commands. Use online bit, not store frame: */
#define _IBUF_CNTRL_FALSE_ACK               2

#endif
