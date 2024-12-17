/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 */

#ifndef _ST2MEM_DEFS_H
#define _ST2MEM_DEFS_H

#define _STR2MEM_CRUN_BIT               0x100000
#define _STR2MEM_CMD_BITS               0x0F0000
#define _STR2MEM_COUNT_BITS             0x00FFFF

#define _STR2MEM_BLOCKS_CMD             0xA0000
#define _STR2MEM_PACKETS_CMD            0xB0000
#define _STR2MEM_BYTES_CMD              0xC0000
#define _STR2MEM_BYTES_FROM_PACKET_CMD  0xD0000

#define _STR2MEM_SOFT_RESET_REG_ID                   0
#define _STR2MEM_INPUT_ENDIANNESS_REG_ID             1
#define _STR2MEM_OUTPUT_ENDIANNESS_REG_ID            2
#define _STR2MEM_BIT_SWAPPING_REG_ID                 3
#define _STR2MEM_BLOCK_SYNC_LEVEL_REG_ID             4
#define _STR2MEM_PACKET_SYNC_LEVEL_REG_ID            5
#define _STR2MEM_READ_POST_WRITE_SYNC_ENABLE_REG_ID  6
#define _STR2MEM_DUAL_BYTE_INPUTS_ENABLED_REG_ID     7
#define _STR2MEM_EN_STAT_UPDATE_ID                   8

#define _STR2MEM_REG_ALIGN      4

#endif /* _ST2MEM_DEFS_H */
