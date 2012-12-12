/*  *********************************************************************
    *  SB1250 Board Support Package
    *
    *  Memory Controller constants              File: sb1250_mc.h
    *
    *  This module contains constants and macros useful for
    *  programming the memory controller.
    *
    *  SB1250 specification level:  User's manual 1/02/02
    *
    *********************************************************************
    *
    *  Copyright 2000, 2001, 2002, 2003
    *  Broadcom Corporation. All rights reserved.
    *
    *  This program is free software; you can redistribute it and/or
    *  modify it under the terms of the GNU General Public License as
    *  published by the Free Software Foundation; either version 2 of
    *  the License, or (at your option) any later version.
    *
    *  This program is distributed in the hope that it will be useful,
    *  but WITHOUT ANY WARRANTY; without even the implied warranty of
    *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    *  GNU General Public License for more details.
    *
    *  You should have received a copy of the GNU General Public License
    *  along with this program; if not, write to the Free Software
    *  Foundation, Inc., 59 Temple Place, Suite 330, Boston,
    *  MA 02111-1307 USA
    ********************************************************************* */


#ifndef _SB1250_MC_H
#define _SB1250_MC_H

#include <asm/sibyte/sb1250_defs.h>

/*
 * Memory Channel Config Register (table 6-14)
 */

#define S_MC_RESERVED0              0
#define M_MC_RESERVED0              _SB_MAKEMASK(8, S_MC_RESERVED0)

#define S_MC_CHANNEL_SEL            8
#define M_MC_CHANNEL_SEL            _SB_MAKEMASK(8, S_MC_CHANNEL_SEL)
#define V_MC_CHANNEL_SEL(x)         _SB_MAKEVALUE(x, S_MC_CHANNEL_SEL)
#define G_MC_CHANNEL_SEL(x)         _SB_GETVALUE(x, S_MC_CHANNEL_SEL, M_MC_CHANNEL_SEL)

#define S_MC_BANK0_MAP              16
#define M_MC_BANK0_MAP              _SB_MAKEMASK(4, S_MC_BANK0_MAP)
#define V_MC_BANK0_MAP(x)           _SB_MAKEVALUE(x, S_MC_BANK0_MAP)
#define G_MC_BANK0_MAP(x)           _SB_GETVALUE(x, S_MC_BANK0_MAP, M_MC_BANK0_MAP)

#define K_MC_BANK0_MAP_DEFAULT      0x00
#define V_MC_BANK0_MAP_DEFAULT      V_MC_BANK0_MAP(K_MC_BANK0_MAP_DEFAULT)

#define S_MC_BANK1_MAP              20
#define M_MC_BANK1_MAP              _SB_MAKEMASK(4, S_MC_BANK1_MAP)
#define V_MC_BANK1_MAP(x)           _SB_MAKEVALUE(x, S_MC_BANK1_MAP)
#define G_MC_BANK1_MAP(x)           _SB_GETVALUE(x, S_MC_BANK1_MAP, M_MC_BANK1_MAP)

#define K_MC_BANK1_MAP_DEFAULT      0x08
#define V_MC_BANK1_MAP_DEFAULT      V_MC_BANK1_MAP(K_MC_BANK1_MAP_DEFAULT)

#define S_MC_BANK2_MAP              24
#define M_MC_BANK2_MAP              _SB_MAKEMASK(4, S_MC_BANK2_MAP)
#define V_MC_BANK2_MAP(x)           _SB_MAKEVALUE(x, S_MC_BANK2_MAP)
#define G_MC_BANK2_MAP(x)           _SB_GETVALUE(x, S_MC_BANK2_MAP, M_MC_BANK2_MAP)

#define K_MC_BANK2_MAP_DEFAULT      0x09
#define V_MC_BANK2_MAP_DEFAULT      V_MC_BANK2_MAP(K_MC_BANK2_MAP_DEFAULT)

#define S_MC_BANK3_MAP              28
#define M_MC_BANK3_MAP              _SB_MAKEMASK(4, S_MC_BANK3_MAP)
#define V_MC_BANK3_MAP(x)           _SB_MAKEVALUE(x, S_MC_BANK3_MAP)
#define G_MC_BANK3_MAP(x)           _SB_GETVALUE(x, S_MC_BANK3_MAP, M_MC_BANK3_MAP)

#define K_MC_BANK3_MAP_DEFAULT      0x0C
#define V_MC_BANK3_MAP_DEFAULT      V_MC_BANK3_MAP(K_MC_BANK3_MAP_DEFAULT)

#define M_MC_RESERVED1              _SB_MAKEMASK(8, 32)

#define S_MC_QUEUE_SIZE		    40
#define M_MC_QUEUE_SIZE             _SB_MAKEMASK(4, S_MC_QUEUE_SIZE)
#define V_MC_QUEUE_SIZE(x)          _SB_MAKEVALUE(x, S_MC_QUEUE_SIZE)
#define G_MC_QUEUE_SIZE(x)          _SB_GETVALUE(x, S_MC_QUEUE_SIZE, M_MC_QUEUE_SIZE)
#define V_MC_QUEUE_SIZE_DEFAULT     V_MC_QUEUE_SIZE(0x0A)

#define S_MC_AGE_LIMIT              44
#define M_MC_AGE_LIMIT              _SB_MAKEMASK(4, S_MC_AGE_LIMIT)
#define V_MC_AGE_LIMIT(x)           _SB_MAKEVALUE(x, S_MC_AGE_LIMIT)
#define G_MC_AGE_LIMIT(x)           _SB_GETVALUE(x, S_MC_AGE_LIMIT, M_MC_AGE_LIMIT)
#define V_MC_AGE_LIMIT_DEFAULT      V_MC_AGE_LIMIT(8)

#define S_MC_WR_LIMIT               48
#define M_MC_WR_LIMIT               _SB_MAKEMASK(4, S_MC_WR_LIMIT)
#define V_MC_WR_LIMIT(x)            _SB_MAKEVALUE(x, S_MC_WR_LIMIT)
#define G_MC_WR_LIMIT(x)            _SB_GETVALUE(x, S_MC_WR_LIMIT, M_MC_WR_LIMIT)
#define V_MC_WR_LIMIT_DEFAULT       V_MC_WR_LIMIT(5)

#define M_MC_IOB1HIGHPRIORITY	    _SB_MAKEMASK1(52)

#define M_MC_RESERVED2              _SB_MAKEMASK(3, 53)

#define S_MC_CS_MODE                56
#define M_MC_CS_MODE                _SB_MAKEMASK(4, S_MC_CS_MODE)
#define V_MC_CS_MODE(x)             _SB_MAKEVALUE(x, S_MC_CS_MODE)
#define G_MC_CS_MODE(x)             _SB_GETVALUE(x, S_MC_CS_MODE, M_MC_CS_MODE)

#define K_MC_CS_MODE_MSB_CS         0
#define K_MC_CS_MODE_INTLV_CS       15
#define K_MC_CS_MODE_MIXED_CS_10    12
#define K_MC_CS_MODE_MIXED_CS_30    6
#define K_MC_CS_MODE_MIXED_CS_32    3

#define V_MC_CS_MODE_MSB_CS         V_MC_CS_MODE(K_MC_CS_MODE_MSB_CS)
#define V_MC_CS_MODE_INTLV_CS       V_MC_CS_MODE(K_MC_CS_MODE_INTLV_CS)
#define V_MC_CS_MODE_MIXED_CS_10    V_MC_CS_MODE(K_MC_CS_MODE_MIXED_CS_10)
#define V_MC_CS_MODE_MIXED_CS_30    V_MC_CS_MODE(K_MC_CS_MODE_MIXED_CS_30)
#define V_MC_CS_MODE_MIXED_CS_32    V_MC_CS_MODE(K_MC_CS_MODE_MIXED_CS_32)

#define M_MC_ECC_DISABLE            _SB_MAKEMASK1(60)
#define M_MC_BERR_DISABLE           _SB_MAKEMASK1(61)
#define M_MC_FORCE_SEQ              _SB_MAKEMASK1(62)
#define M_MC_DEBUG                  _SB_MAKEMASK1(63)

#define V_MC_CONFIG_DEFAULT     V_MC_WR_LIMIT_DEFAULT | V_MC_AGE_LIMIT_DEFAULT | \
				V_MC_BANK0_MAP_DEFAULT | V_MC_BANK1_MAP_DEFAULT | \
				V_MC_BANK2_MAP_DEFAULT | V_MC_BANK3_MAP_DEFAULT | V_MC_CHANNEL_SEL(0) | \
                                M_MC_IOB1HIGHPRIORITY | V_MC_QUEUE_SIZE_DEFAULT


/*
 * Memory clock config register (Table 6-15)
 *
 * Note: this field has been updated to be consistent with the errata to 0.2
 */

#define S_MC_CLK_RATIO              0
#define M_MC_CLK_RATIO              _SB_MAKEMASK(4, S_MC_CLK_RATIO)
#define V_MC_CLK_RATIO(x)           _SB_MAKEVALUE(x, S_MC_CLK_RATIO)
#define G_MC_CLK_RATIO(x)           _SB_GETVALUE(x, S_MC_CLK_RATIO, M_MC_CLK_RATIO)

#define K_MC_CLK_RATIO_2X           4
#define K_MC_CLK_RATIO_25X          5
#define K_MC_CLK_RATIO_3X           6
#define K_MC_CLK_RATIO_35X          7
#define K_MC_CLK_RATIO_4X           8
#define K_MC_CLK_RATIO_45X	    9

#define V_MC_CLK_RATIO_2X	    V_MC_CLK_RATIO(K_MC_CLK_RATIO_2X)
#define V_MC_CLK_RATIO_25X          V_MC_CLK_RATIO(K_MC_CLK_RATIO_25X)
#define V_MC_CLK_RATIO_3X           V_MC_CLK_RATIO(K_MC_CLK_RATIO_3X)
#define V_MC_CLK_RATIO_35X          V_MC_CLK_RATIO(K_MC_CLK_RATIO_35X)
#define V_MC_CLK_RATIO_4X           V_MC_CLK_RATIO(K_MC_CLK_RATIO_4X)
#define V_MC_CLK_RATIO_45X          V_MC_CLK_RATIO(K_MC_CLK_RATIO_45X)
#define V_MC_CLK_RATIO_DEFAULT      V_MC_CLK_RATIO_25X

#define S_MC_REF_RATE                8
#define M_MC_REF_RATE                _SB_MAKEMASK(8, S_MC_REF_RATE)
#define V_MC_REF_RATE(x)             _SB_MAKEVALUE(x, S_MC_REF_RATE)
#define G_MC_REF_RATE(x)             _SB_GETVALUE(x, S_MC_REF_RATE, M_MC_REF_RATE)

#define K_MC_REF_RATE_100MHz         0x62
#define K_MC_REF_RATE_133MHz         0x81
#define K_MC_REF_RATE_200MHz         0xC4

#define V_MC_REF_RATE_100MHz         V_MC_REF_RATE(K_MC_REF_RATE_100MHz)
#define V_MC_REF_RATE_133MHz         V_MC_REF_RATE(K_MC_REF_RATE_133MHz)
#define V_MC_REF_RATE_200MHz         V_MC_REF_RATE(K_MC_REF_RATE_200MHz)
#define V_MC_REF_RATE_DEFAULT        V_MC_REF_RATE_100MHz

#define S_MC_CLOCK_DRIVE             16
#define M_MC_CLOCK_DRIVE             _SB_MAKEMASK(4, S_MC_CLOCK_DRIVE)
#define V_MC_CLOCK_DRIVE(x)          _SB_MAKEVALUE(x, S_MC_CLOCK_DRIVE)
#define G_MC_CLOCK_DRIVE(x)          _SB_GETVALUE(x, S_MC_CLOCK_DRIVE, M_MC_CLOCK_DRIVE)
#define V_MC_CLOCK_DRIVE_DEFAULT     V_MC_CLOCK_DRIVE(0xF)

#define S_MC_DATA_DRIVE              20
#define M_MC_DATA_DRIVE              _SB_MAKEMASK(4, S_MC_DATA_DRIVE)
#define V_MC_DATA_DRIVE(x)           _SB_MAKEVALUE(x, S_MC_DATA_DRIVE)
#define G_MC_DATA_DRIVE(x)           _SB_GETVALUE(x, S_MC_DATA_DRIVE, M_MC_DATA_DRIVE)
#define V_MC_DATA_DRIVE_DEFAULT      V_MC_DATA_DRIVE(0x0)

#define S_MC_ADDR_DRIVE              24
#define M_MC_ADDR_DRIVE              _SB_MAKEMASK(4, S_MC_ADDR_DRIVE)
#define V_MC_ADDR_DRIVE(x)           _SB_MAKEVALUE(x, S_MC_ADDR_DRIVE)
#define G_MC_ADDR_DRIVE(x)           _SB_GETVALUE(x, S_MC_ADDR_DRIVE, M_MC_ADDR_DRIVE)
#define V_MC_ADDR_DRIVE_DEFAULT      V_MC_ADDR_DRIVE(0x0)

#if SIBYTE_HDR_FEATURE(1250, PASS3) || SIBYTE_HDR_FEATURE(112x, PASS1)
#define M_MC_REF_DISABLE             _SB_MAKEMASK1(30)
#endif /* 1250 PASS3 || 112x PASS1 */

#define M_MC_DLL_BYPASS              _SB_MAKEMASK1(31)

#define S_MC_DQI_SKEW               32
#define M_MC_DQI_SKEW               _SB_MAKEMASK(8, S_MC_DQI_SKEW)
#define V_MC_DQI_SKEW(x)            _SB_MAKEVALUE(x, S_MC_DQI_SKEW)
#define G_MC_DQI_SKEW(x)            _SB_GETVALUE(x, S_MC_DQI_SKEW, M_MC_DQI_SKEW)
#define V_MC_DQI_SKEW_DEFAULT       V_MC_DQI_SKEW(0)

#define S_MC_DQO_SKEW               40
#define M_MC_DQO_SKEW               _SB_MAKEMASK(8, S_MC_DQO_SKEW)
#define V_MC_DQO_SKEW(x)            _SB_MAKEVALUE(x, S_MC_DQO_SKEW)
#define G_MC_DQO_SKEW(x)            _SB_GETVALUE(x, S_MC_DQO_SKEW, M_MC_DQO_SKEW)
#define V_MC_DQO_SKEW_DEFAULT       V_MC_DQO_SKEW(0)

#define S_MC_ADDR_SKEW               48
#define M_MC_ADDR_SKEW               _SB_MAKEMASK(8, S_MC_ADDR_SKEW)
#define V_MC_ADDR_SKEW(x)            _SB_MAKEVALUE(x, S_MC_ADDR_SKEW)
#define G_MC_ADDR_SKEW(x)            _SB_GETVALUE(x, S_MC_ADDR_SKEW, M_MC_ADDR_SKEW)
#define V_MC_ADDR_SKEW_DEFAULT       V_MC_ADDR_SKEW(0x0F)

#define S_MC_DLL_DEFAULT             56
#define M_MC_DLL_DEFAULT             _SB_MAKEMASK(8, S_MC_DLL_DEFAULT)
#define V_MC_DLL_DEFAULT(x)          _SB_MAKEVALUE(x, S_MC_DLL_DEFAULT)
#define G_MC_DLL_DEFAULT(x)          _SB_GETVALUE(x, S_MC_DLL_DEFAULT, M_MC_DLL_DEFAULT)
#define V_MC_DLL_DEFAULT_DEFAULT     V_MC_DLL_DEFAULT(0x10)

#define V_MC_CLKCONFIG_DEFAULT       V_MC_DLL_DEFAULT_DEFAULT |  \
                                     V_MC_ADDR_SKEW_DEFAULT | \
                                     V_MC_DQO_SKEW_DEFAULT | \
                                     V_MC_DQI_SKEW_DEFAULT | \
                                     V_MC_ADDR_DRIVE_DEFAULT | \
                                     V_MC_DATA_DRIVE_DEFAULT | \
                                     V_MC_CLOCK_DRIVE_DEFAULT | \
                                     V_MC_REF_RATE_DEFAULT



/*
 * DRAM Command Register (Table 6-13)
 */

#define S_MC_COMMAND                0
#define M_MC_COMMAND                _SB_MAKEMASK(4, S_MC_COMMAND)
#define V_MC_COMMAND(x)             _SB_MAKEVALUE(x, S_MC_COMMAND)
#define G_MC_COMMAND(x)             _SB_GETVALUE(x, S_MC_COMMAND, M_MC_COMMAND)

#define K_MC_COMMAND_EMRS           0
#define K_MC_COMMAND_MRS            1
#define K_MC_COMMAND_PRE            2
#define K_MC_COMMAND_AR             3
#define K_MC_COMMAND_SETRFSH        4
#define K_MC_COMMAND_CLRRFSH        5
#define K_MC_COMMAND_SETPWRDN       6
#define K_MC_COMMAND_CLRPWRDN       7

#define V_MC_COMMAND_EMRS           V_MC_COMMAND(K_MC_COMMAND_EMRS)
#define V_MC_COMMAND_MRS            V_MC_COMMAND(K_MC_COMMAND_MRS)
#define V_MC_COMMAND_PRE            V_MC_COMMAND(K_MC_COMMAND_PRE)
#define V_MC_COMMAND_AR             V_MC_COMMAND(K_MC_COMMAND_AR)
#define V_MC_COMMAND_SETRFSH        V_MC_COMMAND(K_MC_COMMAND_SETRFSH)
#define V_MC_COMMAND_CLRRFSH        V_MC_COMMAND(K_MC_COMMAND_CLRRFSH)
#define V_MC_COMMAND_SETPWRDN       V_MC_COMMAND(K_MC_COMMAND_SETPWRDN)
#define V_MC_COMMAND_CLRPWRDN       V_MC_COMMAND(K_MC_COMMAND_CLRPWRDN)

#define M_MC_CS0                    _SB_MAKEMASK1(4)
#define M_MC_CS1                    _SB_MAKEMASK1(5)
#define M_MC_CS2                    _SB_MAKEMASK1(6)
#define M_MC_CS3                    _SB_MAKEMASK1(7)

/*
 * DRAM Mode Register (Table 6-14)
 */

#define S_MC_EMODE                  0
#define M_MC_EMODE                  _SB_MAKEMASK(15, S_MC_EMODE)
#define V_MC_EMODE(x)               _SB_MAKEVALUE(x, S_MC_EMODE)
#define G_MC_EMODE(x)               _SB_GETVALUE(x, S_MC_EMODE, M_MC_EMODE)
#define V_MC_EMODE_DEFAULT          V_MC_EMODE(0)

#define S_MC_MODE                   16
#define M_MC_MODE                   _SB_MAKEMASK(15, S_MC_MODE)
#define V_MC_MODE(x)                _SB_MAKEVALUE(x, S_MC_MODE)
#define G_MC_MODE(x)                _SB_GETVALUE(x, S_MC_MODE, M_MC_MODE)
#define V_MC_MODE_DEFAULT           V_MC_MODE(0x22)

#define S_MC_DRAM_TYPE              32
#define M_MC_DRAM_TYPE              _SB_MAKEMASK(3, S_MC_DRAM_TYPE)
#define V_MC_DRAM_TYPE(x)           _SB_MAKEVALUE(x, S_MC_DRAM_TYPE)
#define G_MC_DRAM_TYPE(x)           _SB_GETVALUE(x, S_MC_DRAM_TYPE, M_MC_DRAM_TYPE)

#define K_MC_DRAM_TYPE_JEDEC        0
#define K_MC_DRAM_TYPE_FCRAM        1
#define K_MC_DRAM_TYPE_SGRAM	    2

#define V_MC_DRAM_TYPE_JEDEC        V_MC_DRAM_TYPE(K_MC_DRAM_TYPE_JEDEC)
#define V_MC_DRAM_TYPE_FCRAM        V_MC_DRAM_TYPE(K_MC_DRAM_TYPE_FCRAM)
#define V_MC_DRAM_TYPE_SGRAM        V_MC_DRAM_TYPE(K_MC_DRAM_TYPE_SGRAM)

#define M_MC_EXTERNALDECODE	    _SB_MAKEMASK1(35)

#if SIBYTE_HDR_FEATURE(1250, PASS3) || SIBYTE_HDR_FEATURE(112x, PASS1)
#define M_MC_PRE_ON_A8              _SB_MAKEMASK1(36)
#define M_MC_RAM_WITH_A13           _SB_MAKEMASK1(37)
#endif /* 1250 PASS3 || 112x PASS1 */



/*
 * SDRAM Timing Register  (Table 6-15)
 */

#define M_MC_w2rIDLE_TWOCYCLES	  _SB_MAKEMASK1(60)
#define M_MC_r2wIDLE_TWOCYCLES	  _SB_MAKEMASK1(61)
#define M_MC_r2rIDLE_TWOCYCLES	  _SB_MAKEMASK1(62)

#define S_MC_tFIFO                56
#define M_MC_tFIFO                _SB_MAKEMASK(4, S_MC_tFIFO)
#define V_MC_tFIFO(x)             _SB_MAKEVALUE(x, S_MC_tFIFO)
#define G_MC_tFIFO(x)             _SB_GETVALUE(x, S_MC_tFIFO, M_MC_tFIFO)
#define K_MC_tFIFO_DEFAULT        1
#define V_MC_tFIFO_DEFAULT        V_MC_tFIFO(K_MC_tFIFO_DEFAULT)

#define S_MC_tRFC                 52
#define M_MC_tRFC                 _SB_MAKEMASK(4, S_MC_tRFC)
#define V_MC_tRFC(x)              _SB_MAKEVALUE(x, S_MC_tRFC)
#define G_MC_tRFC(x)              _SB_GETVALUE(x, S_MC_tRFC, M_MC_tRFC)
#define K_MC_tRFC_DEFAULT         12
#define V_MC_tRFC_DEFAULT         V_MC_tRFC(K_MC_tRFC_DEFAULT)

#if SIBYTE_HDR_FEATURE(1250, PASS3)
#define M_MC_tRFC_PLUS16          _SB_MAKEMASK1(51)	/* 1250C3 and later.  */
#endif

#define S_MC_tCwCr                40
#define M_MC_tCwCr                _SB_MAKEMASK(4, S_MC_tCwCr)
#define V_MC_tCwCr(x)             _SB_MAKEVALUE(x, S_MC_tCwCr)
#define G_MC_tCwCr(x)             _SB_GETVALUE(x, S_MC_tCwCr, M_MC_tCwCr)
#define K_MC_tCwCr_DEFAULT        4
#define V_MC_tCwCr_DEFAULT        V_MC_tCwCr(K_MC_tCwCr_DEFAULT)

#define S_MC_tRCr                 28
#define M_MC_tRCr                 _SB_MAKEMASK(4, S_MC_tRCr)
#define V_MC_tRCr(x)              _SB_MAKEVALUE(x, S_MC_tRCr)
#define G_MC_tRCr(x)              _SB_GETVALUE(x, S_MC_tRCr, M_MC_tRCr)
#define K_MC_tRCr_DEFAULT         9
#define V_MC_tRCr_DEFAULT         V_MC_tRCr(K_MC_tRCr_DEFAULT)

#define S_MC_tRCw                 24
#define M_MC_tRCw                 _SB_MAKEMASK(4, S_MC_tRCw)
#define V_MC_tRCw(x)              _SB_MAKEVALUE(x, S_MC_tRCw)
#define G_MC_tRCw(x)              _SB_GETVALUE(x, S_MC_tRCw, M_MC_tRCw)
#define K_MC_tRCw_DEFAULT         10
#define V_MC_tRCw_DEFAULT         V_MC_tRCw(K_MC_tRCw_DEFAULT)

#define S_MC_tRRD                 20
#define M_MC_tRRD                 _SB_MAKEMASK(4, S_MC_tRRD)
#define V_MC_tRRD(x)              _SB_MAKEVALUE(x, S_MC_tRRD)
#define G_MC_tRRD(x)              _SB_GETVALUE(x, S_MC_tRRD, M_MC_tRRD)
#define K_MC_tRRD_DEFAULT         2
#define V_MC_tRRD_DEFAULT         V_MC_tRRD(K_MC_tRRD_DEFAULT)

#define S_MC_tRP                  16
#define M_MC_tRP                  _SB_MAKEMASK(4, S_MC_tRP)
#define V_MC_tRP(x)               _SB_MAKEVALUE(x, S_MC_tRP)
#define G_MC_tRP(x)               _SB_GETVALUE(x, S_MC_tRP, M_MC_tRP)
#define K_MC_tRP_DEFAULT          4
#define V_MC_tRP_DEFAULT          V_MC_tRP(K_MC_tRP_DEFAULT)

#define S_MC_tCwD                 8
#define M_MC_tCwD                 _SB_MAKEMASK(4, S_MC_tCwD)
#define V_MC_tCwD(x)              _SB_MAKEVALUE(x, S_MC_tCwD)
#define G_MC_tCwD(x)              _SB_GETVALUE(x, S_MC_tCwD, M_MC_tCwD)
#define K_MC_tCwD_DEFAULT         1
#define V_MC_tCwD_DEFAULT         V_MC_tCwD(K_MC_tCwD_DEFAULT)

#define M_tCrDh                   _SB_MAKEMASK1(7)
#define M_MC_tCrDh		  M_tCrDh

#define S_MC_tCrD                 4
#define M_MC_tCrD                 _SB_MAKEMASK(3, S_MC_tCrD)
#define V_MC_tCrD(x)              _SB_MAKEVALUE(x, S_MC_tCrD)
#define G_MC_tCrD(x)              _SB_GETVALUE(x, S_MC_tCrD, M_MC_tCrD)
#define K_MC_tCrD_DEFAULT         2
#define V_MC_tCrD_DEFAULT         V_MC_tCrD(K_MC_tCrD_DEFAULT)

#define S_MC_tRCD                 0
#define M_MC_tRCD                 _SB_MAKEMASK(4, S_MC_tRCD)
#define V_MC_tRCD(x)              _SB_MAKEVALUE(x, S_MC_tRCD)
#define G_MC_tRCD(x)              _SB_GETVALUE(x, S_MC_tRCD, M_MC_tRCD)
#define K_MC_tRCD_DEFAULT         3
#define V_MC_tRCD_DEFAULT         V_MC_tRCD(K_MC_tRCD_DEFAULT)

#define V_MC_TIMING_DEFAULT     V_MC_tFIFO(K_MC_tFIFO_DEFAULT) | \
                                V_MC_tRFC(K_MC_tRFC_DEFAULT) | \
                                V_MC_tCwCr(K_MC_tCwCr_DEFAULT) | \
                                V_MC_tRCr(K_MC_tRCr_DEFAULT) | \
                                V_MC_tRCw(K_MC_tRCw_DEFAULT) | \
                                V_MC_tRRD(K_MC_tRRD_DEFAULT) | \
                                V_MC_tRP(K_MC_tRP_DEFAULT) | \
                                V_MC_tCwD(K_MC_tCwD_DEFAULT) | \
                                V_MC_tCrD(K_MC_tCrD_DEFAULT) | \
                                V_MC_tRCD(K_MC_tRCD_DEFAULT) | \
                                M_MC_r2rIDLE_TWOCYCLES

/*
 * Errata says these are not the default
 *                               M_MC_w2rIDLE_TWOCYCLES | \
 *                               M_MC_r2wIDLE_TWOCYCLES | \
 */


/*
 * Chip Select Start Address Register (Table 6-17)
 */

#define S_MC_CS0_START              0
#define M_MC_CS0_START              _SB_MAKEMASK(16, S_MC_CS0_START)
#define V_MC_CS0_START(x)           _SB_MAKEVALUE(x, S_MC_CS0_START)
#define G_MC_CS0_START(x)           _SB_GETVALUE(x, S_MC_CS0_START, M_MC_CS0_START)

#define S_MC_CS1_START              16
#define M_MC_CS1_START              _SB_MAKEMASK(16, S_MC_CS1_START)
#define V_MC_CS1_START(x)           _SB_MAKEVALUE(x, S_MC_CS1_START)
#define G_MC_CS1_START(x)           _SB_GETVALUE(x, S_MC_CS1_START, M_MC_CS1_START)

#define S_MC_CS2_START              32
#define M_MC_CS2_START              _SB_MAKEMASK(16, S_MC_CS2_START)
#define V_MC_CS2_START(x)           _SB_MAKEVALUE(x, S_MC_CS2_START)
#define G_MC_CS2_START(x)           _SB_GETVALUE(x, S_MC_CS2_START, M_MC_CS2_START)

#define S_MC_CS3_START              48
#define M_MC_CS3_START              _SB_MAKEMASK(16, S_MC_CS3_START)
#define V_MC_CS3_START(x)           _SB_MAKEVALUE(x, S_MC_CS3_START)
#define G_MC_CS3_START(x)           _SB_GETVALUE(x, S_MC_CS3_START, M_MC_CS3_START)

/*
 * Chip Select End Address Register (Table 6-18)
 */

#define S_MC_CS0_END                0
#define M_MC_CS0_END                _SB_MAKEMASK(16, S_MC_CS0_END)
#define V_MC_CS0_END(x)             _SB_MAKEVALUE(x, S_MC_CS0_END)
#define G_MC_CS0_END(x)             _SB_GETVALUE(x, S_MC_CS0_END, M_MC_CS0_END)

#define S_MC_CS1_END                16
#define M_MC_CS1_END                _SB_MAKEMASK(16, S_MC_CS1_END)
#define V_MC_CS1_END(x)             _SB_MAKEVALUE(x, S_MC_CS1_END)
#define G_MC_CS1_END(x)             _SB_GETVALUE(x, S_MC_CS1_END, M_MC_CS1_END)

#define S_MC_CS2_END                32
#define M_MC_CS2_END                _SB_MAKEMASK(16, S_MC_CS2_END)
#define V_MC_CS2_END(x)             _SB_MAKEVALUE(x, S_MC_CS2_END)
#define G_MC_CS2_END(x)             _SB_GETVALUE(x, S_MC_CS2_END, M_MC_CS2_END)

#define S_MC_CS3_END                48
#define M_MC_CS3_END                _SB_MAKEMASK(16, S_MC_CS3_END)
#define V_MC_CS3_END(x)             _SB_MAKEVALUE(x, S_MC_CS3_END)
#define G_MC_CS3_END(x)             _SB_GETVALUE(x, S_MC_CS3_END, M_MC_CS3_END)

/*
 * Chip Select Interleave Register (Table 6-19)
 */

#define S_MC_INTLV_RESERVED         0
#define M_MC_INTLV_RESERVED         _SB_MAKEMASK(5, S_MC_INTLV_RESERVED)

#define S_MC_INTERLEAVE             7
#define M_MC_INTERLEAVE             _SB_MAKEMASK(18, S_MC_INTERLEAVE)
#define V_MC_INTERLEAVE(x)          _SB_MAKEVALUE(x, S_MC_INTERLEAVE)

#define S_MC_INTLV_MBZ              25
#define M_MC_INTLV_MBZ              _SB_MAKEMASK(39, S_MC_INTLV_MBZ)

/*
 * Row Address Bits Register (Table 6-20)
 */

#define S_MC_RAS_RESERVED           0
#define M_MC_RAS_RESERVED           _SB_MAKEMASK(5, S_MC_RAS_RESERVED)

#define S_MC_RAS_SELECT             12
#define M_MC_RAS_SELECT             _SB_MAKEMASK(25, S_MC_RAS_SELECT)
#define V_MC_RAS_SELECT(x)          _SB_MAKEVALUE(x, S_MC_RAS_SELECT)

#define S_MC_RAS_MBZ                37
#define M_MC_RAS_MBZ                _SB_MAKEMASK(27, S_MC_RAS_MBZ)


/*
 * Column Address Bits Register (Table 6-21)
 */

#define S_MC_CAS_RESERVED           0
#define M_MC_CAS_RESERVED           _SB_MAKEMASK(5, S_MC_CAS_RESERVED)

#define S_MC_CAS_SELECT             5
#define M_MC_CAS_SELECT             _SB_MAKEMASK(18, S_MC_CAS_SELECT)
#define V_MC_CAS_SELECT(x)          _SB_MAKEVALUE(x, S_MC_CAS_SELECT)

#define S_MC_CAS_MBZ                23
#define M_MC_CAS_MBZ                _SB_MAKEMASK(41, S_MC_CAS_MBZ)


/*
 * Bank Address Address Bits Register (Table 6-22)
 */

#define S_MC_BA_RESERVED            0
#define M_MC_BA_RESERVED            _SB_MAKEMASK(5, S_MC_BA_RESERVED)

#define S_MC_BA_SELECT              5
#define M_MC_BA_SELECT              _SB_MAKEMASK(20, S_MC_BA_SELECT)
#define V_MC_BA_SELECT(x)           _SB_MAKEVALUE(x, S_MC_BA_SELECT)

#define S_MC_BA_MBZ                 25
#define M_MC_BA_MBZ                 _SB_MAKEMASK(39, S_MC_BA_MBZ)

/*
 * Chip Select Attribute Register (Table 6-23)
 */

#define K_MC_CS_ATTR_CLOSED         0
#define K_MC_CS_ATTR_CASCHECK       1
#define K_MC_CS_ATTR_HINT           2
#define K_MC_CS_ATTR_OPEN           3

#define S_MC_CS0_PAGE               0
#define M_MC_CS0_PAGE               _SB_MAKEMASK(2, S_MC_CS0_PAGE)
#define V_MC_CS0_PAGE(x)            _SB_MAKEVALUE(x, S_MC_CS0_PAGE)
#define G_MC_CS0_PAGE(x)            _SB_GETVALUE(x, S_MC_CS0_PAGE, M_MC_CS0_PAGE)

#define S_MC_CS1_PAGE               16
#define M_MC_CS1_PAGE               _SB_MAKEMASK(2, S_MC_CS1_PAGE)
#define V_MC_CS1_PAGE(x)            _SB_MAKEVALUE(x, S_MC_CS1_PAGE)
#define G_MC_CS1_PAGE(x)            _SB_GETVALUE(x, S_MC_CS1_PAGE, M_MC_CS1_PAGE)

#define S_MC_CS2_PAGE               32
#define M_MC_CS2_PAGE               _SB_MAKEMASK(2, S_MC_CS2_PAGE)
#define V_MC_CS2_PAGE(x)            _SB_MAKEVALUE(x, S_MC_CS2_PAGE)
#define G_MC_CS2_PAGE(x)            _SB_GETVALUE(x, S_MC_CS2_PAGE, M_MC_CS2_PAGE)

#define S_MC_CS3_PAGE               48
#define M_MC_CS3_PAGE               _SB_MAKEMASK(2, S_MC_CS3_PAGE)
#define V_MC_CS3_PAGE(x)            _SB_MAKEVALUE(x, S_MC_CS3_PAGE)
#define G_MC_CS3_PAGE(x)            _SB_GETVALUE(x, S_MC_CS3_PAGE, M_MC_CS3_PAGE)

/*
 * ECC Test ECC Register (Table 6-25)
 */

#define S_MC_ECC_INVERT             0
#define M_MC_ECC_INVERT             _SB_MAKEMASK(8, S_MC_ECC_INVERT)


#endif
