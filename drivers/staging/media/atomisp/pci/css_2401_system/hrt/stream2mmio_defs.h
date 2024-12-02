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

#ifndef _STREAM2MMMIO_DEFS_H
#define _STREAM2MMMIO_DEFS_H

#include <mipi_backend_defs.h>

#define _STREAM2MMIO_REG_ALIGN                  4

#define _STREAM2MMIO_COMMAND_REG_ID             0
#define _STREAM2MMIO_ACKNOWLEDGE_REG_ID         1
#define _STREAM2MMIO_PIX_WIDTH_ID_REG_ID        2
#define _STREAM2MMIO_START_ADDR_REG_ID          3      /* master port address,NOT Byte */
#define _STREAM2MMIO_END_ADDR_REG_ID            4      /* master port address,NOT Byte */
#define _STREAM2MMIO_STRIDE_REG_ID              5      /* stride in master port words, increment is per packet for long sids, stride is not used for short sid's*/
#define _STREAM2MMIO_NUM_ITEMS_REG_ID           6      /* number of packets for store packets cmd, number of words for store_words cmd */
#define _STREAM2MMIO_BLOCK_WHEN_NO_CMD_REG_ID   7      /* if this register is 1, input will be stalled if there is no pending command for this sid */
#define _STREAM2MMIO_REGS_PER_SID               8

#define _STREAM2MMIO_SID_REG_OFFSET             8
#define _STREAM2MMIO_MAX_NOF_SIDS              64      /* value used in hss model */

/* command token definition     */
#define _STREAM2MMIO_CMD_TOKEN_CMD_LSB          0      /* bits 1-0 is for the command field */
#define _STREAM2MMIO_CMD_TOKEN_CMD_MSB          1

#define _STREAM2MMIO_CMD_TOKEN_WIDTH           (_STREAM2MMIO_CMD_TOKEN_CMD_MSB + 1 - _STREAM2MMIO_CMD_TOKEN_CMD_LSB)

#define _STREAM2MMIO_CMD_TOKEN_STORE_WORDS              0      /* command for storing a number of output words indicated by reg _STREAM2MMIO_NUM_ITEMS */
#define _STREAM2MMIO_CMD_TOKEN_STORE_PACKETS            1      /* command for storing a number of packets indicated by reg _STREAM2MMIO_NUM_ITEMS      */
#define _STREAM2MMIO_CMD_TOKEN_SYNC_FRAME               2      /* command for waiting for a frame start                                                */

/* acknowledges from packer module */
/* fields: eof   - indicates whether last (short) packet received was an eof packet */
/*         eop   - indicates whether command has ended due to packet end or due to no of words requested has been received */
/*         count - indicates number of words stored */
#define _STREAM2MMIO_PACK_NUM_ITEMS_BITS        16
#define _STREAM2MMIO_PACK_ACK_EOP_BIT           _STREAM2MMIO_PACK_NUM_ITEMS_BITS
#define _STREAM2MMIO_PACK_ACK_EOF_BIT           (_STREAM2MMIO_PACK_ACK_EOP_BIT + 1)

/* acknowledge token definition */
#define _STREAM2MMIO_ACK_TOKEN_NUM_ITEMS_LSB    0      /* bits 3-0 is for the command field */
#define _STREAM2MMIO_ACK_TOKEN_NUM_ITEMS_MSB   (_STREAM2MMIO_PACK_NUM_ITEMS_BITS - 1)
#define _STREAM2MMIO_ACK_TOKEN_EOP_BIT         _STREAM2MMIO_PACK_ACK_EOP_BIT
#define _STREAM2MMIO_ACK_TOKEN_EOF_BIT         _STREAM2MMIO_PACK_ACK_EOF_BIT
#define _STREAM2MMIO_ACK_TOKEN_VALID_BIT       (_STREAM2MMIO_ACK_TOKEN_EOF_BIT + 1)      /* this bit indicates a valid ack    */
/* if there is no valid ack, a read  */
/* on the ack register returns 0     */
#define _STREAM2MMIO_ACK_TOKEN_WIDTH           (_STREAM2MMIO_ACK_TOKEN_VALID_BIT + 1)

/* commands for packer module */
#define _STREAM2MMIO_PACK_CMD_STORE_WORDS        0
#define _STREAM2MMIO_PACK_CMD_STORE_LONG_PACKET  1
#define _STREAM2MMIO_PACK_CMD_STORE_SHORT_PACKET 2

#endif /* _STREAM2MMIO_DEFS_H */
