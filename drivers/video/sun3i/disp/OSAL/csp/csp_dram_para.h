/*
 * drivers/video/sun3i/disp/OSAL/csp/csp_dram_para.h
 *
 * (C) Copyright 2007-2012
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#ifndef __CSP_DRAM_PARA_H__
#define __CSP_DRAM_PARA_H__

#define DRAM_PIN_DEV_ID     (0)
#define DRAM_PIN_LIST       ((__u32 *)0)
#define DRAM_PIN_NUMBER     (0)


typedef enum __DRAM_TYPE
{
    DRAM_TYPE_DDR   =1,
    DRAM_TYPE_DDR2  =2,

}__dram_type_e;

typedef enum __DRAM_BNKMOD
{
    DRAM_ACS_INTERLEAVE = 0,
    DRAM_ACS_SEQUENCE   = 1,
} __dram_bnkmod_e;


typedef enum __DRAM_PRIO_LEVEL
{
    DRAM_PRIO_LEVEL_0 = 0,
    DRAM_PRIO_LEVEL_1    ,
    DRAM_PRIO_LEVEL_2    ,
    DRAM_PRIO_LEVEL_3    ,
    DRAM_PRIO_LEVEL_

}__dram_prio_level_e;

typedef enum __DRAM_DEV
{
    DRAM_DEVICE_NULL = 0,
    DRAM_DEVICE_CPUD,
    DRAM_DEVICE_CPUI,
    DRAM_DEVICE_DDMA,
    DRAM_DEVICE_COME,
    DRAM_DEVICE_IMAGE0,
    DRAM_DEVICE_SCALE0,
    DRAM_DEVICE_CSI0,
    DRAM_DEVICE_TS,
    DRAM_DEVICE_VE,
    DRAM_DEVICE_IMAGE1,
    DRAM_DEVICE_SCALE1,
    DRAM_DEVICE_MIX,
    DRAM_DEVICE_CSI1,
    DRAM_DEVICE_ACE,
    DRAM_DEVICE_NDMASDC0,

}__dram_dev_e;


typedef struct __DRAM_PARA
{
    __u32           base;           // dram base address
    __u32           size;           // dram size, based on MByte
    __u32           clk;            // dram work clock
    __u32           access_mode;    // interleave mode or sequence mode
    __u32           cs_num;         // dram chip count
    __u32           ddr8_remap;
    __dram_type_e   type;           // ddr/ddr2/sdr/mddr/lpddr/...
    __u32           bwidth;         // dram buss width
    __u32           col_width;      // column address width
    __u32           row_width;      // row address width
    __u32           bank_size;      // dram bank count
    __u32           cas;            // dram cas
}__dram_para_t;

//==============================================================================
// dram configuration parameter reference value
//==============================================================================

//*****************************************************************************
//DDR SDRAM (x16)
//*****************************************************************************

//DDR 64Mx16 (128M Byte)
#ifdef DRAM_DDR_64Mx16
#define DRAM_DDR_TYPE           1
#define DRAM_COL_WIDTH          10
#define DRAM_ROW_WIDTH          14
#define DRAM_BANK_SIZE          4
#define DRAM_CAS                3
#endif

//DDR 32Mx16 (64M Byte)(H4H511638C-UCB3)
#ifdef DRAM_DDR_32Mx16
#define DRAM_DDR_TYPE           1
#define DRAM_COL_WIDTH          10
#define DRAM_ROW_WIDTH          13
#define DRAM_BANK_SIZE          4
#define DRAM_CAS                3
#endif

//DDR 16Mx16 (32MB) (HY5DU561622ETP-5)
#ifdef DRAM_DDR_16Mx16
#define DRAM_DDR_TYPE           1
#define DRAM_COL_WIDTH          9
#define DRAM_ROW_WIDTH          13
#define DRAM_BANK_SIZE          4
#define DRAM_CAS                3
#endif

//*****************************************************************************
//DDR2 SDRAM (x16)
//*****************************************************************************

//DDR2 64Mx16 (128M Byte)
#ifdef DRAM_DDR2_64Mx16
#define DRAM_DDR_TYPE           2
#define DRAM_COL_WIDTH          10
#define DRAM_ROW_WIDTH          13
#define DRAM_BANK_SIZE          8
#define DRAM_CAS                4
#endif

//DDR2 128Mx16 (256M Byte)
#ifdef DRAM_DDR2_128Mx16
#define DRAM_DDR_TYPE           2
#define DRAM_COL_WIDTH          10
#define DRAM_ROW_WIDTH          14
#define DRAM_BANK_SIZE          8
#define DRAM_CAS                5
#endif


#endif  //__CSP_DRAM_PARA_H__

