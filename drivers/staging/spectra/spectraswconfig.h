/*
 * NAND Flash Controller Device Driver
 * Copyright (c) 2009, Intel Corporation and its suppliers.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

#ifndef _SPECTRASWCONFIG_
#define _SPECTRASWCONFIG_

/* NAND driver version */
#define GLOB_VERSION          "driver version 20100311"


/***** Common Parameters *****/
#define RETRY_TIMES                   3

#define READ_BADBLOCK_INFO            1
#define READBACK_VERIFY               0
#define AUTO_FORMAT_FLASH             0

/***** Cache Parameters *****/
#define CACHE_ITEM_NUM            128
#define BLK_NUM_FOR_L2_CACHE        16

/***** Block Table Parameters *****/
#define BLOCK_TABLE_INDEX             0

/***** Wear Leveling Parameters *****/
#define WEAR_LEVELING_GATE         0x10
#define WEAR_LEVELING_BLOCK_NUM      10

#define DEBUG_BNDRY             0

/***** Product Feature Support *****/
#define FLASH_EMU               defined(CONFIG_SPECTRA_EMU)
#define FLASH_NAND              defined(CONFIG_SPECTRA_MRST_HW)
#define FLASH_MTD               defined(CONFIG_SPECTRA_MTD)
#define CMD_DMA                 defined(CONFIG_SPECTRA_MRST_HW_DMA)

#define SPECTRA_PARTITION_ID    0

/* Enable this macro if the number of flash blocks is larger than 16K. */
#define SUPPORT_LARGE_BLOCKNUM  1

/**** Block Table and Reserved Block Parameters *****/
#define SPECTRA_START_BLOCK     3
//#define NUM_FREE_BLOCKS_GATE    30
#define NUM_FREE_BLOCKS_GATE    60

/**** Hardware Parameters ****/
#define GLOB_HWCTL_REG_BASE     0xFFA40000
#define GLOB_HWCTL_REG_SIZE     4096

#define GLOB_HWCTL_MEM_BASE     0xFFA48000
#define GLOB_HWCTL_MEM_SIZE     4096

/* KBV - Updated to LNW scratch register address */
#define SCRATCH_REG_ADDR    0xFF108018
#define SCRATCH_REG_SIZE    64

#define GLOB_HWCTL_DEFAULT_BLKS    2048

#define SUPPORT_15BITECC        1
#define SUPPORT_8BITECC         1

#define ONFI_BLOOM_TIME         0
#define MODE5_WORKAROUND        1

#endif /*_SPECTRASWCONFIG_*/
