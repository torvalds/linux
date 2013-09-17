/*
 * drivers/block/sunxi_nand/src/include/nand_scan.h
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

#ifndef __NAND_SCAN_H__
#define __NAND_SCAN_H__

#include "nand_type.h"
#include "nand_physic.h"

//==============================================================================
//  define nand flash manufacture ID number
//==============================================================================

#define TOSHIBA_NAND            0x98                //Toshiba nand flash manufacture number
#define SAMSUNG_NAND            0xec                //Samsunt nand flash manufacture number
#define HYNIX_NAND              0xad                //Hynix nand flash manufacture number
#define MICRON_NAND             0x2c                //Micron nand flash manufacture number
#define ST_NAND                 0x20                //ST nand flash manufacture number
#define INTEL_NAND              0x89                //Intel nand flash manufacture number
#define SPANSION_NAND           0x01                //spansion nand flash manufacture number
#define POWER_NAND              0x92                //power nand flash manufacture number
#define SANDISK                 0x45                //sandisk nand flash manufacture number

//==============================================================================
//  define the function __s32erface for nand storage scan module
//==============================================================================

/*
************************************************************************************************************************
*                           ANALYZE NAND FLASH STORAGE SYSTEM
*
*Description: Analyze nand flash storage system, generate the nand flash physical
*             architecture parameter and connect information.
*
*Arguments  : none
*
*Return     : analyze result;
*               = 0     analyze successful;
*               < 0     analyze failed, can't recognize or some other error.
************************************************************************************************************************
*/
__s32  SCN_AnalyzeNandSystem(void);


#endif  //ifndef __NAND_SCAN_H__
