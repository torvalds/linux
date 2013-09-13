/*  *********************************************************************
    *  SB1250 Board Support Package
    *
    *  SMBUS Constants				File: sb1250_smbus.h
    *
    *  This module contains constants and macros useful for
    *  manipulating the SB1250's SMbus devices.
    *
    *  SB1250 specification level:  10/21/02
    *  BCM1280 specification level:  11/24/03
    *
    *********************************************************************
    *
    *  Copyright 2000,2001,2002,2003
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


#ifndef _SB1250_SMBUS_H
#define _SB1250_SMBUS_H

#include <asm/sibyte/sb1250_defs.h>

/*
 * SMBus Clock Frequency Register (Table 14-2)
 */

#define S_SMB_FREQ_DIV		    0
#define M_SMB_FREQ_DIV		    _SB_MAKEMASK(13, S_SMB_FREQ_DIV)
#define V_SMB_FREQ_DIV(x)	    _SB_MAKEVALUE(x, S_SMB_FREQ_DIV)

#define K_SMB_FREQ_400KHZ	    0x1F
#define K_SMB_FREQ_100KHZ	    0x7D
#define K_SMB_FREQ_10KHZ	    1250

#define S_SMB_CMD		    0
#define M_SMB_CMD		    _SB_MAKEMASK(8, S_SMB_CMD)
#define V_SMB_CMD(x)		    _SB_MAKEVALUE(x, S_SMB_CMD)

/*
 * SMBus control register (Table 14-4)
 */

#define M_SMB_ERR_INTR		    _SB_MAKEMASK1(0)
#define M_SMB_FINISH_INTR	    _SB_MAKEMASK1(1)

#define S_SMB_DATA_OUT		    4
#define M_SMB_DATA_OUT		    _SB_MAKEMASK1(S_SMB_DATA_OUT)
#define V_SMB_DATA_OUT(x)	    _SB_MAKEVALUE(x, S_SMB_DATA_OUT)

#define M_SMB_DATA_DIR		    _SB_MAKEMASK1(5)
#define M_SMB_DATA_DIR_OUTPUT	    M_SMB_DATA_DIR
#define M_SMB_CLK_OUT		    _SB_MAKEMASK1(6)
#define M_SMB_DIRECT_ENABLE	    _SB_MAKEMASK1(7)

/*
 * SMBus status registers (Table 14-5)
 */

#define M_SMB_BUSY		    _SB_MAKEMASK1(0)
#define M_SMB_ERROR		    _SB_MAKEMASK1(1)
#define M_SMB_ERROR_TYPE	    _SB_MAKEMASK1(2)

#if SIBYTE_HDR_FEATURE(1250, PASS3) || SIBYTE_HDR_FEATURE(112x, PASS1) || SIBYTE_HDR_FEATURE_CHIP(1480)
#define S_SMB_SCL_IN		    5
#define M_SMB_SCL_IN		    _SB_MAKEMASK1(S_SMB_SCL_IN)
#define V_SMB_SCL_IN(x)		    _SB_MAKEVALUE(x, S_SMB_SCL_IN)
#define G_SMB_SCL_IN(x)		    _SB_GETVALUE(x, S_SMB_SCL_IN, M_SMB_SCL_IN)
#endif /* 1250 PASS3 || 112x PASS1 || 1480 */

#define S_SMB_REF		    6
#define M_SMB_REF		    _SB_MAKEMASK1(S_SMB_REF)
#define V_SMB_REF(x)		    _SB_MAKEVALUE(x, S_SMB_REF)
#define G_SMB_REF(x)		    _SB_GETVALUE(x, S_SMB_REF, M_SMB_REF)

#define S_SMB_DATA_IN		    7
#define M_SMB_DATA_IN		    _SB_MAKEMASK1(S_SMB_DATA_IN)
#define V_SMB_DATA_IN(x)	    _SB_MAKEVALUE(x, S_SMB_DATA_IN)
#define G_SMB_DATA_IN(x)	    _SB_GETVALUE(x, S_SMB_DATA_IN, M_SMB_DATA_IN)

/*
 * SMBus Start/Command registers (Table 14-9)
 */

#define S_SMB_ADDR		    0
#define M_SMB_ADDR		    _SB_MAKEMASK(7, S_SMB_ADDR)
#define V_SMB_ADDR(x)		    _SB_MAKEVALUE(x, S_SMB_ADDR)
#define G_SMB_ADDR(x)		    _SB_GETVALUE(x, S_SMB_ADDR, M_SMB_ADDR)

#define M_SMB_QDATA		    _SB_MAKEMASK1(7)

#define S_SMB_TT		    8
#define M_SMB_TT		    _SB_MAKEMASK(3, S_SMB_TT)
#define V_SMB_TT(x)		    _SB_MAKEVALUE(x, S_SMB_TT)
#define G_SMB_TT(x)		    _SB_GETVALUE(x, S_SMB_TT, M_SMB_TT)

#define K_SMB_TT_WR1BYTE	    0
#define K_SMB_TT_WR2BYTE	    1
#define K_SMB_TT_WR3BYTE	    2
#define K_SMB_TT_CMD_RD1BYTE	    3
#define K_SMB_TT_CMD_RD2BYTE	    4
#define K_SMB_TT_RD1BYTE	    5
#define K_SMB_TT_QUICKCMD	    6
#define K_SMB_TT_EEPROMREAD	    7

#define V_SMB_TT_WR1BYTE	    V_SMB_TT(K_SMB_TT_WR1BYTE)
#define V_SMB_TT_WR2BYTE	    V_SMB_TT(K_SMB_TT_WR2BYTE)
#define V_SMB_TT_WR3BYTE	    V_SMB_TT(K_SMB_TT_WR3BYTE)
#define V_SMB_TT_CMD_RD1BYTE	    V_SMB_TT(K_SMB_TT_CMD_RD1BYTE)
#define V_SMB_TT_CMD_RD2BYTE	    V_SMB_TT(K_SMB_TT_CMD_RD2BYTE)
#define V_SMB_TT_RD1BYTE	    V_SMB_TT(K_SMB_TT_RD1BYTE)
#define V_SMB_TT_QUICKCMD	    V_SMB_TT(K_SMB_TT_QUICKCMD)
#define V_SMB_TT_EEPROMREAD	    V_SMB_TT(K_SMB_TT_EEPROMREAD)

#define M_SMB_PEC		    _SB_MAKEMASK1(15)

/*
 * SMBus Data Register (Table 14-6) and SMBus Extra Register (Table 14-7)
 */

#define S_SMB_LB		    0
#define M_SMB_LB		    _SB_MAKEMASK(8, S_SMB_LB)
#define V_SMB_LB(x)		    _SB_MAKEVALUE(x, S_SMB_LB)

#define S_SMB_MB		    8
#define M_SMB_MB		    _SB_MAKEMASK(8, S_SMB_MB)
#define V_SMB_MB(x)		    _SB_MAKEVALUE(x, S_SMB_MB)


/*
 * SMBus Packet Error Check register (Table 14-8)
 */

#define S_SPEC_PEC		    0
#define M_SPEC_PEC		    _SB_MAKEMASK(8, S_SPEC_PEC)
#define V_SPEC_MB(x)		    _SB_MAKEVALUE(x, S_SPEC_PEC)


#if SIBYTE_HDR_FEATURE(1250, PASS2) || SIBYTE_HDR_FEATURE(112x, PASS1) || SIBYTE_HDR_FEATURE_CHIP(1480)

#define S_SMB_CMDH		    8
#define M_SMB_CMDH		    _SB_MAKEMASK(8, S_SMB_CMDH)
#define V_SMB_CMDH(x)		    _SB_MAKEVALUE(x, S_SMB_CMDH)

#define M_SMB_EXTEND		    _SB_MAKEMASK1(14)

#define S_SMB_DFMT		    8
#define M_SMB_DFMT		    _SB_MAKEMASK(3, S_SMB_DFMT)
#define V_SMB_DFMT(x)		    _SB_MAKEVALUE(x, S_SMB_DFMT)
#define G_SMB_DFMT(x)		    _SB_GETVALUE(x, S_SMB_DFMT, M_SMB_DFMT)

#define K_SMB_DFMT_1BYTE	    0
#define K_SMB_DFMT_2BYTE	    1
#define K_SMB_DFMT_3BYTE	    2
#define K_SMB_DFMT_4BYTE	    3
#define K_SMB_DFMT_NODATA	    4
#define K_SMB_DFMT_CMD4BYTE	    5
#define K_SMB_DFMT_CMD5BYTE	    6
#define K_SMB_DFMT_RESERVED	    7

#define V_SMB_DFMT_1BYTE	    V_SMB_DFMT(K_SMB_DFMT_1BYTE)
#define V_SMB_DFMT_2BYTE	    V_SMB_DFMT(K_SMB_DFMT_2BYTE)
#define V_SMB_DFMT_3BYTE	    V_SMB_DFMT(K_SMB_DFMT_3BYTE)
#define V_SMB_DFMT_4BYTE	    V_SMB_DFMT(K_SMB_DFMT_4BYTE)
#define V_SMB_DFMT_NODATA	    V_SMB_DFMT(K_SMB_DFMT_NODATA)
#define V_SMB_DFMT_CMD4BYTE	    V_SMB_DFMT(K_SMB_DFMT_CMD4BYTE)
#define V_SMB_DFMT_CMD5BYTE	    V_SMB_DFMT(K_SMB_DFMT_CMD5BYTE)
#define V_SMB_DFMT_RESERVED	    V_SMB_DFMT(K_SMB_DFMT_RESERVED)

#define S_SMB_AFMT		    11
#define M_SMB_AFMT		    _SB_MAKEMASK(2, S_SMB_AFMT)
#define V_SMB_AFMT(x)		    _SB_MAKEVALUE(x, S_SMB_AFMT)
#define G_SMB_AFMT(x)		    _SB_GETVALUE(x, S_SMB_AFMT, M_SMB_AFMT)

#define K_SMB_AFMT_NONE		    0
#define K_SMB_AFMT_ADDR		    1
#define K_SMB_AFMT_ADDR_CMD1BYTE    2
#define K_SMB_AFMT_ADDR_CMD2BYTE    3

#define V_SMB_AFMT_NONE		    V_SMB_AFMT(K_SMB_AFMT_NONE)
#define V_SMB_AFMT_ADDR		    V_SMB_AFMT(K_SMB_AFMT_ADDR)
#define V_SMB_AFMT_ADDR_CMD1BYTE    V_SMB_AFMT(K_SMB_AFMT_ADDR_CMD1BYTE)
#define V_SMB_AFMT_ADDR_CMD2BYTE    V_SMB_AFMT(K_SMB_AFMT_ADDR_CMD2BYTE)

#define M_SMB_DIR		    _SB_MAKEMASK1(13)

#endif /* 1250 PASS2 || 112x PASS1 || 1480 */

#endif
