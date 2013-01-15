/*
 * drivers/block/sunxi_nand/src/scan/nand_id.c
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

#include "../include/nand_scan.h"

//==============================================================================
// define the optional operation parameter for different kindes of nand flash
//==============================================================================

//the physical architecture parameter for Samsung 2K page SLC nand flash
static struct __OptionalPhyOpPar_t PhysicArchiPara0 =
{
    {0x00, 0x30},           //multi-plane read command
    {0x11, 0x81},           //multi-plane program command
    {0x00, 0x00, 0x35},     //multi-plane page copy-back read command
    {0x85, 0x11, 0x81},     //multi-plane page copy-back program command
    0x70,                   //multi-plane operation status read command
    0xf1,                   //inter-leave bank0 operation status read command
    0xf2,                   //inter-leave bank1 operation status read command
    0x01,                   //bad block flag position, in the fist 2 page
    1                       //multi-plane block address offset
};

//the physical architecture parameter for Samsung 4K page SLC nand flash
static struct __OptionalPhyOpPar_t PhysicArchiPara1 =
{
    {0x60, 0x30},           //multi-plane read command
    {0x11, 0x81},           //multi-plane program command
    {0x60, 0x60, 0x35},     //multi-plane page copy-back read command
    {0x85, 0x11, 0x81},     //multi-plane page copy-back program command
    0x70,                   //multi-plane operation status read command
    0xf1,                   //inter-leave bank0 operation status read command
    0xf2,                   //inter-leave bank1 operation status read command
    0x00,                   //bad block flag position, in the fist page
    1                       //multi-plane block address offset
};

//the physical architecture parameter for Samsung 2K page MLC nand flash
static struct __OptionalPhyOpPar_t PhysicArchiPara2 =
{
    {0x00, 0x30},           //multi-plane read command
    {0x11, 0x81},           //multi-plane program command
    {0x00, 0x00, 0x35},     //multi-plane page copy-back read command
    {0x85, 0x11, 0x81},     //multi-plane page copy-back program command
    0x70,                   //multi-plane operation status read command
    0xf1,                   //inter-leave bank0 operation status read command
    0xf2,                   //inter-leave bank1 operation status read command
    0x02,                   //bad block flag position, in the last page
    1                       //multi-plane block address offset
};

//the physical architecture parameter for Samsung 4K page MLC nand flash
static struct __OptionalPhyOpPar_t PhysicArchiPara3 =
{
    {0x60, 0x60},           //multi-plane read command
    {0x11, 0x81},           //multi-plane program command
    {0x60, 0x60, 0x35},     //multi-plane page copy-back read command
    {0x85, 0x11, 0x81},     //multi-plane page copy-back program command
    0x70,                   //multi-plane operation status read command
    0xf1,                   //inter-leave bank0 operation status read command
    0xf2,                   //inter-leave bank1 operation status read command
    0x02,                   //bad block flag position, in the last page
    1                       //multi-plane block address offset
};

//the physical architecture parameter for Micon nand flash
static struct __OptionalPhyOpPar_t PhysicArchiPara4 =
{
    {0x00, 0x30},           //multi-plane read command
    {0x11, 0x80},           //multi-plane program command
    {0x00, 0x00, 0x35},     //multi-plane page copy-back read command
    {0x85, 0x11, 0x80},     //multi-plane page copy-back program command
    0x70,                   //multi-plane operation status read command
    0x78,                   //inter-leave bank0 operation status read command
    0x78,                   //inter-leave bank1 operation status read command
    0x01,                   //bad block flag position, in the fist 2 page
    1                       //multi-plane block address offset
};

//the physical architecture parameter for Toshiba SLC nand flash
static struct __OptionalPhyOpPar_t PhysicArchiPara5 =
{
    {0x00, 0x30},           //multi-plane read command
    {0x11, 0x80},           //multi-plane program command
    {0x00, 0x00, 0x30},     //multi-plane page copy-back read command
    {0x8c, 0x11, 0x8c},     //multi-plane page copy-back program command
    0x71,                   //multi-plane operation status read command
    0x70,                   //inter-leave bank0 operation status read command
    0x70,                   //inter-leave bank1 operation status read command
    0x00,                   //bad block flag position, in the fist page
    0                       //multi-plane block address offset
};

//the physical architecture parameter for Toshiba MLC nand flash which multi-plane offset is 1024
static struct __OptionalPhyOpPar_t PhysicArchiPara6 =
{
    {0x00, 0x30},           //multi-plane read command
    {0x11, 0x80},           //multi-plane program command
    {0x00, 0x00, 0x30},     //multi-plane page copy-back read command
    {0x8c, 0x11, 0x8c},     //multi-plane page copy-back program command
    0x71,                   //multi-plane operation status read command
    0x70,                   //inter-leave bank0 operation status read command
    0x70,                   //inter-leave bank1 operation status read command
    0x00,                   //bad block flag position, in the fist page
    1024                    //multi-plane block address offset
};

//the physical architecture parameter for Toshiba MLC nand flash which multi-plane offset is 2048
static struct __OptionalPhyOpPar_t PhysicArchiPara7 =
{
    {0x00, 0x30},           //multi-plane read command
    {0x11, 0x80},           //multi-plane program command
    {0x00, 0x00, 0x30},     //multi-plane page copy-back read command
    {0x8c, 0x11, 0x8c},     //multi-plane page copy-back program command
    0x71,                   //multi-plane operation status read command
    0x70,                   //inter-leave bank0 operation status read command
    0x70,                   //inter-leave bank1 operation status read command
    0x00,                   //bad block flag position, in the fist page
    2048                    //multi-plane block address offset
};

static struct __OptionalPhyOpPar_t PhysicArchiPara8 =
{
    {0x00, 0x30},           //multi-plane read command
    {0x11, 0x80},           //multi-plane program command
    {0x00, 0x00, 0x30},     //multi-plane page copy-back read command
    {0x8c, 0x11, 0x8c},     //multi-plane page copy-back program command
    0x71,                   //multi-plane operation status read command
    0x70,                   //inter-leave bank0 operation status read command
    0x70,                   //inter-leave bank1 operation status read command
    0x02,                   //bad block flag position, in the last page
    1                       //multi-plane block address offset
};

static struct __OptionalPhyOpPar_t PhysicArchiPara9 =
{
    {0x00, 0x30},           //multi-plane read command
    {0x11, 0x81},           //multi-plane program command
    {0x00, 0x00, 0x30},     //multi-plane page copy-back read command
    {0x8c, 0x11, 0x8c},     //multi-plane page copy-back program command
    0x71,                   //multi-plane operation status read command
    0x70,                   //inter-leave bank0 operation status read command
    0x70,                   //inter-leave bank1 operation status read command
    0x02,                   //bad block flag position, in the last page
    1                       //multi-plane block address offset
};

static struct __OptionalPhyOpPar_t DefualtPhysicArchiPara =
{
    {0x00, 0x30},           //multi-plane read command
    {0x11, 0x81},           //multi-plane program command
    {0x00, 0x00, 0x35},     //multi-plane page copy-back read command
    {0x85, 0x11, 0x81},     //multi-plane page copy-back program command
    0x70,                   //multi-plane operation status read command
    0xf1,                   //inter-leave bank0 operation status read command
    0xf2,                   //inter-leave bank1 operation status read command
    0x00,                   //bad block flag position, in the fist 2 page
    1                       //multi-plane block address offset
};


//==============================================================================
// define the physical architecture parameter for all kinds of nand flash
//==============================================================================

//==============================================================================
//============================ SAMSUNG NAND FLASH ==============================
//==============================================================================
struct __NandPhyInfoPar_t SamsungNandTbl[] =
{
    //                NAND_CHIP_ID                     DieCnt SecCnt  PagCnt   BlkCnt    OpOpt   DatBlk  Freq   EccMode ReadRetry DDRType OperationPar
    //--------------------------------------------------------------------------------------------------------------------------------
    { {0xec, 0xf1, 0xff, 0x15, 0xff, 0xff, 0xff, 0xff }, 1,     4,      64,     1024,   0x0000,   974,    15,     0,       0,        0,     &PhysicArchiPara0 },   // K9F1G08
    { {0xec, 0xf1, 0x00, 0x95, 0xff, 0xff, 0xff, 0xff }, 1,     4,      64,     1024,   0x0000,   974,    15,     0,       0,        0,     &PhysicArchiPara0 },   // K9F1G08
    { {0xec, 0xda, 0xff, 0x15, 0xff, 0xff, 0xff, 0xff }, 2,     4,      64,     1024,   0x0000,   974,    15,     0,       0,        0,     &PhysicArchiPara0 },   // K9K2G08
    { {0xec, 0xda, 0x10, 0x95, 0xff, 0xff, 0xff, 0xff }, 1,     4,      64,     2048,   0x0008,   974,    15,     0,       0,        0,     &PhysicArchiPara0 },   // K9F2G08
    { {0xec, 0xdc, 0xc1, 0x15, 0xff, 0xff, 0xff, 0xff }, 2,     4,      64,     2048,   0x0000,   974,    15,     0,       0,        0,     &PhysicArchiPara0 },   // K9K4G08
    { {0xec, 0xdc, 0x10, 0x95, 0xff, 0xff, 0xff, 0xff }, 1,     4,      64,     4096,   0x0018,   974,    15,     0,       0,        0,     &PhysicArchiPara0 },   // K9F4G08
    { {0xec, 0xd3, 0x51, 0x95, 0xff, 0xff, 0xff, 0xff }, 2,     4,      64,     4096,   0x0018,   974,    30,     0,       0,        0,     &PhysicArchiPara0 },   // K9K8G08
    //-----------------------------------------------------------------------------------------------------------------------------------
    { {0xec, 0xd3, 0x50, 0xa6, 0xff, 0xff, 0xff, 0xff }, 1,     4,     128,     4096,   0x0018,   974,    30,     0,       0,        0,     &PhysicArchiPara1 },   // K9F8G08
    { {0xec, 0xd5, 0x51, 0xa6, 0xff, 0xff, 0xff, 0xff }, 2,     4,     128,     4096,   0x0038,   974,    30,     0,       0,        0,     &PhysicArchiPara1 },   // K9KAG08
    //-----------------------------------------------------------------------------------------------------------------------------------
    { {0xec, 0xdc, 0x14, 0x25, 0xff, 0xff, 0xff, 0xff }, 1,     4,     128,     2048,   0x0008,   974,    20,     0,       0,        0,     &PhysicArchiPara2 },   // K9G4G08
    { {0xec, 0xdc, 0x14, 0xa5, 0xff, 0xff, 0xff, 0xff }, 1,     4,     128,     2048,   0x0008,   974,    30,     0,       0,        0,     &PhysicArchiPara2 },   // K9G4G08
    { {0xec, 0xd3, 0x55, 0x25, 0xff, 0xff, 0xff, 0xff }, 2,     4,     128,     2048,   0x0008,   974,    20,     0,       0,        0,     &PhysicArchiPara2 },   // K9L8G08
    { {0xec, 0xd3, 0x55, 0xa5, 0xff, 0xff, 0xff, 0xff }, 2,     4,     128,     2048,   0x0008,   974,    30,     0,       0,        0,     &PhysicArchiPara2 },   // K9L8G08
    { {0xec, 0xd3, 0x14, 0x25, 0xff, 0xff, 0xff, 0xff }, 1,     4,     128,     4096,   0x0008,   974,    20,     0,       0,        0,     &PhysicArchiPara2 },   // K9G8G08
    { {0xec, 0xd3, 0x14, 0xa5, 0xff, 0xff, 0xff, 0xff }, 1,     4,     128,     4096,   0x0008,   974,    30,     0,       0,        0,     &PhysicArchiPara2 },   // K9G8G08
    { {0xec, 0xd5, 0x55, 0x25, 0xff, 0xff, 0xff, 0xff }, 2,     4,     128,     4096,   0x0028,   974,    30,     0,       0,        0,     &PhysicArchiPara2 },   // K9LAG08
    { {0xec, 0xd5, 0x55, 0xa5, 0xff, 0xff, 0xff, 0xff }, 2,     4,     128,     4096,   0x0028,   974,    30,     0,       0,        0,     &PhysicArchiPara2 },   // K9LAG08
    //-----------------------------------------------------------------------------------------------------------------------------------
    { {0xec, 0xd5, 0x14, 0xb6, 0xff, 0xff, 0xff, 0xff }, 1,     8,     128,     4096,   0x0008,   974,    30,     0,       0,        0,     &PhysicArchiPara3 },   // K9GAG08
    { {0xec, 0xd7, 0x55, 0xb6, 0xff, 0xff, 0xff, 0xff }, 2,     8,     128,     4096,   0x0028,   974,    30,     0,       0,        0,     &PhysicArchiPara3 },   // K9LBG08
    { {0xec, 0xd7, 0xd5, 0x29, 0xff, 0xff, 0xff, 0xff }, 2,     8,     128,     4096,   0x0028,   974,    30,     0,       0,        0,     &PhysicArchiPara3 },   // K9LBG08
    { {0xec, 0xd7, 0x94, 0x72, 0xff, 0xff, 0xff, 0xff }, 1,    16,     128,     4096,   0x0008,   974,    30,     2,       0,        0,     &PhysicArchiPara3 },   // K9GBG08
    { {0xec, 0xd5, 0x98, 0x71, 0xff, 0xff, 0xff, 0xff }, 1,     8,     256,     2048,   0x0008,   950,    30,     3,       0,        0,     &PhysicArchiPara3 },   // K9AAG08
                                                                                                                                            
    { {0xec, 0xd5, 0x94, 0x29, 0xff, 0xff, 0xff, 0xff }, 1,     8,     128,     4096,   0x0008,   974,    30,     0,       0,        0,     &PhysicArchiPara3 },   // K9GAG08U0D
    { {0xec, 0xd5, 0x84, 0x72, 0xff, 0xff, 0xff, 0xff }, 1,    16,     128,     2048,   0x0000,   950,    24,     2,       0,        0,     &PhysicArchiPara3 },   // K9GAG08U0E
    { {0xec, 0xd5, 0x94, 0x76, 0x54, 0xff, 0xff, 0xff }, 1,    16,     128,     2048,   0x0408,   950,    30,     2,       0,        0,     &PhysicArchiPara3 },   // K9GAG08U0E
    { {0xec, 0xd3, 0x84, 0x72, 0xff, 0xff, 0xff, 0xff }, 1,    16,     128,     1024,   0x0000,   950,    24,     2,       0,        0,     &PhysicArchiPara3 },   // K9G8G08U0C
	{ {0xec, 0xd7, 0x94, 0x76, 0xff, 0xff, 0xff, 0xff }, 1,    16,     128,     4096,   0x0088,   974,    30,     3,       0,        0,     &PhysicArchiPara3 },   // K9GBG08U0A
	{ {0xec, 0xd7, 0x94, 0x7A, 0xff, 0xff, 0xff, 0xff }, 1,    16,     128,     4096,   0x0088,   974,    30,     3,       0,        0,     &PhysicArchiPara3 },   // K9GBG08U0A
	{ {0xec, 0xde, 0xd5, 0x7A, 0x58, 0xff, 0xff, 0xff }, 2,    16,	   128, 	4096,	0x0888,   974,	  30,	  3,	   0,		 0, 	&PhysicArchiPara3 },   // K9LCG08U0A
	                                                                                                                                                                                                                                                                                
	{ {0xec, 0xd7, 0x94, 0x7A, 0x54, 0xc3, 0xff, 0xff }, 1,    16,     128,     4096,   0x0088,   974,    60,     1,       0,        3,     &PhysicArchiPara3 },   // toogle nand 1.0 
	{ {0xec, 0xde, 0xa4, 0x7a, 0x68, 0xc4, 0xff, 0xff }, 1,    16,     128,     8192,   0x0588,   974,    60,     4,   0x200e04,     3,     &PhysicArchiPara3 },   // toogle nand 2.0 K9GCGD8U0A
	{ {0xec, 0xd7, 0x94, 0x7E, 0x64, 0xc4, 0xff, 0xff }, 1,    16,     128,     4096,   0x0588,   974,    60,     4,   0x200e04,     3,     &PhysicArchiPara3 },   // toogle nand 2.0 K9GBGD8U0B
    { {0xec, 0xd7, 0x94, 0x7e, 0x64, 0x44, 0xff, 0xff }, 1,    16,     128,     4096,   0x0588,   974,    40,     4,   0x200e04,     0,     &PhysicArchiPara3 },   // 21nm sdr K9GBG08U0B

    //-----------------------------------------------------------------------------------------------------------------------------------
    { {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff }, 0,     0,       0,        0,   0x0000,     0,     0,     0,       0,        0,      0                 },   // NULL
};


//==============================================================================
//============================= HYNIX NAND FLASH ===============================
//==============================================================================
struct __NandPhyInfoPar_t HynixNandTbl[] =
{
    //                  NAND_CHIP_ID                  DieCnt SecCnt  PagCnt   BlkCnt    OpOpt   DatBlk  Freq  EccMode  ReadRetry  OperationPar
    //---------------------------------------------------------------------------------------------------------------------------
    { {0xad, 0xf1, 0x80, 0x15, 0xff, 0xff, 0xff, 0xff }, 1,     4,      64,     1024,   0x0000,   974,    15,     0,      0,         0,    &PhysicArchiPara0 },   // HY27UF081G2M
    { {0xad, 0xf1, 0x80, 0x1d, 0xff, 0xff, 0xff, 0xff }, 1,     4,      64,     1024,   0x0000,   974,    20,     0,      0,         0,    &PhysicArchiPara0 },   // HY27UF081G2A
    { {0xad, 0xf1, 0x00, 0x1d, 0xff, 0xff, 0xff, 0xff }, 1,     4,      64,     1024,   0x0000,   974,    20,     0,      0,         0,    &PhysicArchiPara0 },   // H27U1G8F2B
    { {0xad, 0xda, 0x80, 0x15, 0xff, 0xff, 0xff, 0xff }, 1,     4,      64,     2048,   0x0000,   974,    15,     0,      0,         0,    &PhysicArchiPara0 },   // HY27UF082G2M
    { {0xad, 0xda, 0x80, 0x1d, 0xff, 0xff, 0xff, 0xff }, 1,     4,      64,     2048,   0x0000,   974,    20,     0,      0,         0,    &PhysicArchiPara0 },   // HY27UF082G2A
    { {0xad, 0xda, 0x10, 0x95, 0xff, 0xff, 0xff, 0xff }, 1,     4,      64,     2048,   0x0000,   974,    20,     0,      0,         0,    &PhysicArchiPara0 },   // HY27UF082G2B
    { {0xad, 0xdc, 0x80, 0x15, 0xff, 0xff, 0xff, 0xff }, 4,     4,      64,     1024,   0x0000,   974,    15,     0,      0,         0,    &PhysicArchiPara0 },   // HY27UH084G2M
    { {0xad, 0xdc, 0x80, 0x95, 0xff, 0xff, 0xff, 0xff }, 1,     4,      64,     4096,   0x0000,   974,    20,     0,      0,         0,    &PhysicArchiPara0 },   // HY27UF084G2M, HY27UG088G5M
    { {0xad, 0xdc, 0x10, 0x95, 0xff, 0xff, 0xff, 0xff }, 1,     4,      64,     4096,   0x0008,   974,    20,     0,      0,         0,    &PhysicArchiPara0 },   // HY27UF084G2B, HY27UG088G5B
    { {0xad, 0xd3, 0x80, 0x15, 0xff, 0xff, 0xff, 0xff }, 4,     4,      64,     2048,   0x0000,   974,    20,     0,      0,         0,    &PhysicArchiPara0 },   // HY27UG084G2M, HY27H088G2M
    { {0xad, 0xd3, 0xc1, 0x95, 0xff, 0xff, 0xff, 0xff }, 2,     4,      64,     4096,   0x0000,   974,    20,     0,      0,         0,    &PhysicArchiPara0 },   // HY27UG088G2M, HY27UH08AG5M
    //---------------------------------------------------------------------------------------------------------------------------
    { {0xad, 0xdc, 0x84, 0x25, 0xff, 0xff, 0xff, 0xff }, 1,     4,     128,     2048,   0x0000,   974,    12,     0,      0,         0,    &PhysicArchiPara2 },   // HY27UT084G2M, HY27UU088G5M
    { {0xad, 0xdc, 0x14, 0xa5, 0xff, 0xff, 0xff, 0xff }, 1,     4,     128,     2048,   0x0008,   974,    15,     0,      0,         0,    &PhysicArchiPara2 },   // HY27U4G8T2BTR
    { {0xad, 0xd3, 0x85, 0x25, 0xff, 0xff, 0xff, 0xff }, 2,     4,     128,     2048,   0x0000,   974,    10,     0,      0,         0,    &PhysicArchiPara2 },   // HY27UV08AG5M, HY27UW08BGFM
    { {0xad, 0xd3, 0x14, 0x25, 0xff, 0xff, 0xff, 0xff }, 1,     4,     128,     4096,   0x0008,   974,    12,     0,      0,         0,    &PhysicArchiPara2 },   // HY27UT088G2M, HY27UU08AG5M
    { {0xad, 0xd3, 0x14, 0x2d, 0xff, 0xff, 0xff, 0xff }, 1,     4,     128,     4096,   0x0008,   974,    25,     0,      0,         0,    &PhysicArchiPara2 },   // HY27UT088G2M, HY27UU08AG5M
    { {0xad, 0xd3, 0x14, 0xa5, 0xff, 0xff, 0xff, 0xff }, 1,     4,     128,     4096,   0x0008,   974,    15,     0,      0,         0,    &PhysicArchiPara2 },   // HY27UT088G2M, HY27UU08AG5M
    { {0xad, 0xd5, 0x55, 0x25, 0xff, 0xff, 0xff, 0xff }, 2,     4,     128,     4096,   0x0008,   974,    15,     0,      0,         0,    &PhysicArchiPara2 },   // HY27UV08BG5M, HY27UW08CGFM
    { {0xad, 0xd5, 0x55, 0x2d, 0xff, 0xff, 0xff, 0xff }, 2,     4,     128,     4096,   0x0008,   974,    25,     0,      0,         0,    &PhysicArchiPara2 },   // HY27UV08BG5M, HY27UW08CGFM
    { {0xad, 0xd5, 0x55, 0xa5, 0xff, 0xff, 0xff, 0xff }, 2,     4,     128,     8192,   0x0008,   974,    30,     0,      0,         0,    &PhysicArchiPara2 },   // HY27UV08BG5M, HY27UW08CGFM
    //---------------------------------------------------------------------------------------------------------------------------
    { {0xad, 0xd3, 0x14, 0xb6, 0xff, 0xff, 0xff, 0xff }, 1,     8,     128,     2048,   0x0008,   974,    30,     0,      0,         0,    &PhysicArchiPara3 },   // H27U8G8T2B
    { {0xad, 0xd5, 0x14, 0xb6, 0xff, 0xff, 0xff, 0xff }, 1,     8,     128,     4096,   0x0008,   974,    30,     0,      0,         0,    &PhysicArchiPara3 },   // H27UAG8T2M, H27UBG8U5M
    { {0xad, 0xd7, 0x55, 0xb6, 0xff, 0xff, 0xff, 0xff }, 2,     8,     128,     4096,   0x0008,   974,    30,     0,      0,         0,    &PhysicArchiPara3 },   // H27UCG8V5M
    //---------------------------------------------------------------------------------------------------------------------------
    { {0xad, 0xd5, 0x94, 0x25, 0xff, 0xff, 0xff, 0xff }, 1,     8,     128,     4096,   0x0008,   974,    30,     2,      0,         0,    &PhysicArchiPara3 },   // H27UBG8U5A
    { {0xad, 0xd7, 0x95, 0x25, 0xff, 0xff, 0xff, 0xff }, 2,     8,     128,     4096,   0x0008,   974,    30,     2,      0,         0,    &PhysicArchiPara3 },   // H27UCG8V5A
    { {0xad, 0xd5, 0x95, 0x25, 0xff, 0xff, 0xff, 0xff }, 2,     8,     128,     4096,   0x0008,   974,    30,     2,      0,         0,    &PhysicArchiPara3 },   // H27UCG8VFA
    { {0xad, 0xd5, 0x94, 0x9A, 0xff, 0xff, 0xff, 0xff }, 1,    16,     256,     1024,   0x0000,   950,    30,     2,      0,         0,    &PhysicArchiPara3 },   // H27UAG8T2B
    { {0xad, 0xd7, 0x94, 0x9A, 0xff, 0xff, 0xff, 0xff }, 1,    16,     256,     2048,   0x0008,   950,    30,     2,      0,         0,    &PhysicArchiPara3 },   // H27UBG8T2A H27UCG8U5(D)A H27UDG8VF(D)A
    { {0xad, 0xde, 0xd5, 0x9A, 0xff, 0xff, 0xff, 0xff }, 2,    16,     256,     2048,   0x0008,   950,    30,     2,      0,         0,    &PhysicArchiPara3 },   // H27UDG8V5A
    { {0xad, 0xd7, 0x94, 0x25, 0xff, 0xff, 0xff, 0xff }, 1,     8,     128,     8192,   0x0008,   974,    30,     2,      0,         0,    &PhysicArchiPara3 },   // H27UBG8T2M
    { {0xad, 0xde, 0x94, 0xd2, 0xff, 0xff, 0xff, 0xff }, 1,    16,     256,     4096,   0x0188,   950,    15,     2,  0x000604,      0,    &PhysicArchiPara3 },   // H27UCG8T2M
    { {0xad, 0xd7, 0x18, 0x8d, 0xff, 0xff, 0xff, 0xff }, 1,     8,     256,     4096,   0x0188,   950,    15,     3,  0x000604,      0,    &PhysicArchiPara3 },   // H27UBG8M2A
    { {0xad, 0xd7, 0x94, 0xda, 0xff, 0xff, 0xff, 0xff }, 1,    16,     256,     2048,   0x0188,   950,    15,     3,  0x010604,      0,    &PhysicArchiPara3 },   // H27UBG8M2A


    { {0xad, 0xde, 0x94, 0xda, 0x74, 0xff, 0xff, 0xff }, 1,    16,     256,     4096,   0x0188,   960,    40,     4,  0x020708,      0,    &PhysicArchiPara3 },   // H27UCG8T2A
    { {0xad, 0xd7, 0x94, 0x91, 0x60, 0xff, 0xff, 0xff }, 1,    16,     256,     2048,   0x0188,   960,    40,     4,  0x030708,      0,    &PhysicArchiPara3 },   // H27UBG8T2C
    //---------------------------------------------------------------------------------------------------------------------------
    { {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff }, 0,     0,       0,        0,   0x0000,     0,     0,     0,      0,         0,   0                 },   // NULL
};


//==============================================================================
//============================= TOSHIBA NAND FLASH =============================
//==============================================================================
struct __NandPhyInfoPar_t ToshibaNandTbl[] =
{
    //                    NAND_CHIP_ID                 DieCnt SecCnt  PagCnt   BlkCnt    OpOpt   DatBlk  Freq   EccMode ReadRetry   OperationPar
    //-------------------------------------------------------------------------------------------------------------------------
    { {0x98, 0xf1, 0x80, 0x95, 0xff, 0xff, 0xff, 0xff }, 1,     4,      64,     1024,   0x0000,   974,    20,     0,     0,      0,   &PhysicArchiPara5 },   // TC58NVG0S3B
    { {0x98, 0xda, 0xff, 0x95, 0xff, 0xff, 0xff, 0xff }, 1,     4,      64,     2048,   0x0000,   974,    20,     0,     0,      0,   &PhysicArchiPara5 },   // TC58NVG1S3B
    { {0x98, 0xdc, 0x81, 0x95, 0xff, 0xff, 0xff, 0xff }, 1,     4,      64,     4096,   0x0000,   974,    20,     0,     0,      0,   &PhysicArchiPara5 },   // TC58NVG2S3B
    { {0x98, 0xd1, 0x90, 0x15, 0xff, 0xff, 0xff, 0xff }, 1,     4,      64,     1024,   0x0000,   974,    20,     0,     0,      0,   &PhysicArchiPara5 },   // TC58NVG0S3E
    //-------------------------------------------------------------------------------------------------------------------------
    { {0x98, 0xda, 0x84, 0xa5, 0xff, 0xff, 0xff, 0xff }, 1,     4,     128,     1024,   0x0000,   974,    20,     0,     0,      0,   &PhysicArchiPara6 },   // TC58NVG1D4B
    { {0x98, 0xdc, 0x84, 0xa5, 0xff, 0xff, 0xff, 0xff }, 1,     4,     128,     2048,   0x0008,   974,    20,     0,     0,      0,   &PhysicArchiPara6 },   // TC58NVG2D4B
    { {0x98, 0xd3, 0x84, 0xa5, 0xff, 0xff, 0xff, 0xff }, 1,     4,     128,     4096,   0x0008,   974,    20,     0,     0,      0,   &PhysicArchiPara7 },   // TC58NVG3D4C
    { {0x98, 0xd5, 0x85, 0xa5, 0xff, 0xff, 0xff, 0xff }, 2,     4,     128,     4096,   0x0008,   974,    20,     0,     0,      0,   &PhysicArchiPara7 },   // TC58NVG4D4C, TC58NVG5D4C
    //-------------------------------------------------------------------------------------------------------------------------
    { {0x98, 0xd3, 0x94, 0xba, 0xff, 0xff, 0xff, 0xff }, 1,     8,     128,     2048,   0x0008,   974,    20,     0,     0,      0,   &PhysicArchiPara6 },   // TC58NVG3D1DTG00
    { {0x98, 0xd7, 0x95, 0xba, 0xff, 0xff, 0xff, 0xff }, 1,     8,     128,     8192,   0x0008,   918,    30,     2,     0,      0,   &PhysicArchiPara7 },   // TC58NVG6D1DTG20
    { {0x98, 0xd5, 0x94, 0xba, 0xff, 0xff, 0xff, 0xff }, 1,     8,     128,     4096,   0x0008,   918,    30,     2,     0,      0,   &PhysicArchiPara7},    // TH58NVG5D1DTG20
    { {0x98, 0xd5, 0x94, 0x32, 0xff, 0xff, 0xff, 0xff }, 1,    16,     128,     2048,   0x0008,   918,    25,     1,     0,      0,   &PhysicArchiPara8},    // TH58NVG4D2ETA20 TH58NVG4D2FTA20 TH58NVG5D2ETA00
    { {0x98, 0xd7, 0x94, 0x32, 0xff, 0xff, 0xff, 0xff }, 1,    16,     128,     4096,   0x0008,   918,    25,     2,     0,      0,   &PhysicArchiPara8},    // TH58NVG5D2FTA00 TH58NVG6D2FTA20
    { {0x98, 0xd7, 0x95, 0x32, 0xff, 0xff, 0xff, 0xff }, 2,    16,     128,     4096,   0x0008,   454,    25,     1,     0,      0,   &PhysicArchiPara8},    // TH58NVG6D2ETA20
    //-------------------------------------------------------------------------------------------------------------------------
    { {0x98, 0xde, 0x94, 0x82, 0x76, 0xff, 0xff, 0xff }, 1,    16,     256,     4096,   0x0588,   918,    25,     4, 0x100504,   0,   &PhysicArchiPara9},    // TH58NVG6D2ETA20
    { {0x98, 0xd7, 0x94, 0x32, 0x76, 0x56, 0xff, 0xff }, 1,    16,     128,     4096,   0x0588,   918,    25,     4, 0x100504,   0,   &PhysicArchiPara9},    // TH58NVG5D2HTA20
    { {0x98, 0xd5, 0x84, 0x32, 0x72, 0x56, 0xff, 0xff }, 1,    16,     128,     2048,   0x0580,   918,    25,     4, 0x100504,   0,   &PhysicArchiPara9},    // TH58NVG4D2HTA20
    //-------------------------------------------------------------------------------------------------------------------------
    { {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff }, 0,     0,       0,        0,   0x0000,     0,     0,     0,     0,      0,        0         },   // NULL
};


//==============================================================================
//============================= MICON NAND FLASH ===============================
//==============================================================================
struct __NandPhyInfoPar_t MicronNandTbl[] =
{
    //                   NAND_CHIP_ID                 DieCnt SecCnt  PagCnt   BlkCnt    OpOpt   DatBlk  Freq  EccMode  ReadRetry  OperationPar
    //-------------------------------------------------------------------------------------------------------------------------
    { {0x2c, 0xda, 0xff, 0x15, 0xff, 0xff, 0xff, 0xff }, 1,     4,      64,     2048,   0x0010,   974,    25,     0,     0,     0,   &PhysicArchiPara4 },   // MT29F2G08AAC, JS29F02G08AAN
    { {0x2c, 0xdc, 0xff, 0x15, 0xff, 0xff, 0xff, 0xff }, 2,     4,      64,     2048,   0x0010,   974,    25,     0,     0,     0,   &PhysicArchiPara4 },   // MT29F4G08BAB, MT29F8G08FAB, JS29F04G08BAN, JS29F08G08FAN
    { {0x2c, 0xdc, 0x90, 0x95, 0xff, 0xff, 0xff, 0xff }, 1,     4,      64,     4096,   0x0018,   974,    25,     0,     0,     0,   &PhysicArchiPara4 },   // MT29F4G08AAA, MT29F8G08DAA, JS29F04G08AAN
    { {0x2c, 0xd3, 0xd1, 0x95, 0xff, 0xff, 0xff, 0xff }, 2,     4,      64,     4096,   0x0018,   974,    25,     0,     0,     0,   &PhysicArchiPara4 },   // MT29F8G08BAB, MT29F16G08FAB, JS29F08G08BAN, JS29F16G08FAN
    //-------------------------------------------------------------------------------------------------------------------------
    { {0x2c, 0xdc, 0x84, 0x25, 0xff, 0xff, 0xff, 0xff }, 1,     4,     128,     2048,   0x0000,   974,    20,     0,     0,     0,   &PhysicArchiPara4 },   // MT29F4G08MAA, MT29F8G08QAA
    { {0x2c, 0xd3, 0x85, 0x25, 0xff, 0xff, 0xff, 0xff }, 2,     4,     128,     2048,   0x0000,   974,    20,     0,     0,     0,   &PhysicArchiPara4 },   // MT29F16GTAA
    { {0x2c, 0xd3, 0x94, 0xa5, 0xff, 0xff, 0xff, 0xff }, 1,     4,     128,     4096,   0x0008,   974,    30,     0,     0,     0,   &PhysicArchiPara4 },   // MT29F8G08MAA, MT29F16G08QAA, JS29F08G08AAM, JS29F16G08CAM
    { {0x2c, 0xd5, 0x95, 0xa5, 0xff, 0xff, 0xff, 0xff }, 2,     4,     128,     4096,   0x0008,   974,    20,     0,     0,     0,   &PhysicArchiPara4 },   // MT29F32G08TAA, JS29F32G08FAM
    { {0x2c, 0xd5, 0xd5, 0xa5, 0xff, 0xff, 0xff, 0xff }, 2,     4,     128,     4096,   0x0028,   974,    20,     0,     0,     0,   &PhysicArchiPara4 },   // MT29F32G08TAA, JS29F32G08FAM
    //-------------------------------------------------------------------------------------------------------------------------
    { {0x2c, 0xd5, 0x94, 0x3e, 0xff, 0xff, 0xff, 0xff }, 1,     8,     128,     4096,   0x0008,   974,    30,     0,     0,     0,   &PhysicArchiPara4 },   // MT29F16G08MAA, MT29F32G08QAA, JS29F32G08AAM, JS29F32G08CAM
    { {0x2c, 0xd5, 0xd5, 0x3e, 0xff, 0xff, 0xff, 0xff }, 2,     8,     128,     4096,   0x0008,   974,    30,     0,     0,     0,   &PhysicArchiPara4 },   // MT29F64G08TAA, JS29F64G08FAM
    //-------------------------------------------------------------------------------------------------------------------------
    { {0x2c, 0xd7, 0x94, 0x3e, 0xff, 0xff, 0xff, 0xff }, 1,     8,     128,     8192,   0x0208,   950,    30,     2,     0,     0,   &PhysicArchiPara4 },   // MT29F32G08CBAAA,MT29F64G08CFAAA
    { {0x2c, 0xd7, 0xd5, 0x3e, 0xff, 0xff, 0xff, 0xff }, 2,     8,     128,     4096,   0x0008,   950,    30,     2,     0,     0,   &PhysicArchiPara4 },   // MT29F64G08CTAA
    { {0x2c, 0xd9, 0xd5, 0x3e, 0xff, 0xff, 0xff, 0xff }, 2,     8,     128,     8192,   0x0008,   950,    30,     2,     0,     0,   &PhysicArchiPara4 },   // MT29F128G08,
    { {0x2c, 0x68, 0x04, 0x46, 0xff, 0xff, 0xff, 0xff }, 1,     8,     256,     4096,   0x0208,   950,    30,     2,     0,     0,   &PhysicArchiPara4 },   // MT29F32G08CBABA
    { {0x2c, 0x88, 0x05, 0xC6, 0xff, 0xff, 0xff, 0xff }, 2,     8,     256,     4096,   0x0208,   950,    30,     2,     0,     0,   &PhysicArchiPara4 },   // MT29F128G08CJABA
    { {0x2c, 0x88, 0x04, 0x4B, 0xff, 0xff, 0xff, 0xff }, 1,    16,     256,     4096,   0x0208,   950,    30,     2,     0,     0,   &PhysicArchiPara4 },   // MT29F64G08CBAAA
    { {0x2c, 0x68, 0x04, 0x4A, 0xff, 0xff, 0xff, 0xff }, 1,     8,     256,     4096,   0x0208,   950,    30,     2,     0,     0,   &PhysicArchiPara4 },   // MT29F32G08CBACA
    { {0x2c, 0x48, 0x04, 0x4A, 0xff, 0xff, 0xff, 0xff }, 1,     8,     256,     2048,   0x0208,   950,    30,     2,     0,     0,   &PhysicArchiPara4 },   // MT29F16G08CBACA
    { {0x2c, 0x48, 0x04, 0x46, 0xff, 0xff, 0xff, 0xff }, 1,     8,     256,     2048,   0x0208,   950,    30,     2,     0,     0,   &PhysicArchiPara4 },   // MT29F16G08CBABA
    { {0x2c, 0x64, 0x44, 0x4B, 0xA9, 0xff, 0xff, 0xff }, 1,    16,     256,     4096,   0x0208,   950,    40,     4,     0,     0,   &PhysicArchiPara4 },   // MT29F64G08CBABA
	//-------------------------------------------------------------------------------------------------------------------------
    { {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff }, 0,     0,       0,        0,   0x0000,     0,     0,    0,     0,      0,      0              },   // NULL
};


//==============================================================================
//============================= INTEL NAND FLASH ===============================
//==============================================================================
struct __NandPhyInfoPar_t IntelNandTbl[] =
{
    //                 NAND_CHIP_ID                   DieCnt SecCnt  PagCnt   BlkCnt    OpOpt   DatBlk  Freq  EccMode  ReadRetry  OperationPar
    //-------------------------------------------------------------------------------------------------------------------------
    { {0x89, 0xd3, 0x94, 0xa5, 0xff, 0xff, 0xff, 0xff }, 1,     4,     128,     4096,   0x0008,   974,    30,     0,     0,    0,   &PhysicArchiPara4 },   // 29F08G08AAMB2, 29F16G08CAMB2
    { {0x89, 0xd5, 0xd5, 0xa5, 0xff, 0xff, 0xff, 0xff }, 2,     4,     128,     4096,   0x0028,   974,    20,     0,     0,    0,   &PhysicArchiPara4 },   // 29F32G08FAMB2
    //-------------------------------------------------------------------------------------------------------------------------
	{ {0x89, 0xd7, 0x94, 0x3e, 0xff, 0xff, 0xff, 0xff }, 1,     8,     128,     8192,   0x0008,   918,    30,     2,     0,    0,   &PhysicArchiPara4 },   // MLC32GW8IMA,MLC64GW8IMA, 29F32G08AAMD2, 29F64G08CAMD2
	{ {0x89, 0xd5, 0x94, 0x3e, 0xff, 0xff, 0xff, 0xff }, 1,     8,     128,     4096,   0x0008,   918,    30,     2,     0,    0,   &PhysicArchiPara4 },   // 29F32G08CAMC1
	{ {0x89, 0xd7, 0xd5, 0x3e, 0xff, 0xff, 0xff, 0xff }, 1,     8,     128,     8192,   0x0008,   918,    30,     2,     0,    0,   &PhysicArchiPara4 },   // 29F64G08FAMC1
	{ {0x89, 0x68, 0x04, 0x46, 0xff, 0xff, 0xff, 0xff }, 1,     8,     256,     4096,   0x0208,   918,    30,     2,     0,    0,   &PhysicArchiPara4 },   // 29F32G08AAMDB
	{ {0x89, 0x88, 0x24, 0x4B, 0xff, 0xff, 0xff, 0xff }, 1,    16,     256,     4096,   0x0208,   918,    30,     2,     0,    0,   &PhysicArchiPara4 },    //  29F64G08CBAAA 29F64G083AME1
	{ {0x89, 0xA8, 0x25, 0xCB, 0xff, 0xff, 0xff, 0xff }, 2,    16,     256,     4096,   0x0208,   918,    30,     2,     0,    0,   &PhysicArchiPara4 },    //  29F64G08CBAAA 29F64G083AME1
	//-------------------------------------------------------------------------------------------------------------------------
	{ {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff }, 0,     0,       0,        0,   0x0000,     0,     0,     0,     0,    0,          0        },   // NULL
};


//==============================================================================
//=============================== ST NAND FLASH ================================
//==============================================================================
struct __NandPhyInfoPar_t StNandTbl[] =
{
    //              NAND_CHIP_ID                       DieCnt SecCnt  PagCnt   BlkCnt    OpOpt   DatBlk  Freq  EccMode  ReadRetry  OperationPar
    //-------------------------------------------------------------------------------------------------------------------------
    { {0x20, 0xf1, 0x80, 0x15, 0xff, 0xff, 0xff, 0xff }, 1,     4,      64,     1024,   0x0010,   974,    15,    0,       0,   0,   &PhysicArchiPara0 },  // NAND01GW3B
    { {0x20, 0xf1, 0x00, 0x1d, 0xff, 0xff, 0xff, 0xff }, 1,     4,      64,     1024,   0x0000,   974,    15,    0,       0,   0,   &PhysicArchiPara0 },  // NAND01G001
    { {0x20, 0xda, 0x80, 0x15, 0xff, 0xff, 0xff, 0xff }, 1,     4,      64,     2048,   0x0000,   974,    15,    0,       0,   0,   &PhysicArchiPara0 },  // NAND02GW3B
    { {0x20, 0xda, 0x10, 0x95, 0xff, 0xff, 0xff, 0xff }, 1,     4,      64,     2048,   0x0000,   974,    15,    0,       0,   0,   &PhysicArchiPara0 },  // NAND02GW3B2DN6
    { {0x20, 0xdc, 0x80, 0x95, 0xff, 0xff, 0xff, 0xff }, 1,     4,      64,     4096,   0x0000,   974,    15,    0,       0,   0,   &PhysicArchiPara0 },  // NAND04GW3B
    { {0x20, 0xd3, 0xc1, 0x95, 0xff, 0xff, 0xff, 0xff }, 2,     4,      64,     4096,   0x0000,   974,    15,    0,       0,   0,   &PhysicArchiPara0 },  // NAND08GW3B
    //-------------------------------------------------------------------------------------------------------------------------
    { {0x20, 0xdc, 0x84, 0x25, 0xff, 0xff, 0xff, 0xff }, 1,     4,     128,     2048,   0x0000,   974,    15,    0,       0,   0,   &PhysicArchiPara2 },  // NAND04GW3C
    { {0x20, 0xd3, 0x85, 0x25, 0xff, 0xff, 0xff, 0xff }, 2,     4,     128,     2048,   0x0000,   974,    15,    0,       0,   0,   &PhysicArchiPara2 },  // NAND08GW3C
    { {0x20, 0xd3, 0x85, 0x25, 0xff, 0xff, 0xff, 0xff }, 4,     4,     128,     2048,   0x0000,   974,    15,    0,       0,   0,   &PhysicArchiPara2 },  // NAND16GW3C
    //-------------------------------------------------------------------------------------------------------------------------
    { {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff }, 0,     0,       0,        0,   0x0000,     0,     0,    0,       0,   0,           0          },   // NULL
};

//==============================================================================
//============================ SPANSION NAND FLASH ==============================
//==============================================================================
struct __NandPhyInfoPar_t SpansionNandTbl[] =
{
    //                   NAND_CHIP_ID                 DieCnt SecCnt  PagCnt   BlkCnt    OpOpt   DatBlk  Freq  EccMode   ReadRetry OperationPar
    //------------------------------------------------------------------------------------------------------------------------
    { {0x01, 0xaa, 0x10, 0x00, 0xff, 0xff, 0xff, 0xff }, 1,     4,      64,     2048,   0x0000,   974,    30,     0,      0,   0,   &PhysicArchiPara0 },   // S39MS02G
    { {0x01, 0xa1, 0x10, 0x00, 0xff, 0xff, 0xff, 0xff }, 1,     4,      64,     1024,   0x0000,   974,    30,     0,      0,   0,   &PhysicArchiPara0 },   // S39MS01G
    { {0x01, 0xff, 0xff, 0x00, 0xff, 0xff, 0xff, 0xff }, 1,     4,      64,     1024,   0x0000,   974,    30,     0,      0,   0,   &PhysicArchiPara0 },   // DFT01GR08P1PM0
    //------------------------------------------------------------------------------------------------------------------------
    { {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff }, 0,     0,       0,        0,   0x0000,     0,     0,     0,      0,   0,          0        },   // NULL
};

//==============================================================================
//============================ POWER NAND FLASH ==============================
//==============================================================================
struct __NandPhyInfoPar_t PowerNandTbl[] =
{
    //                   NAND_CHIP_ID                 DieCnt SecCnt  PagCnt   BlkCnt    OpOpt   DatBlk  Freq  EccMode   ReadRetry OperationPar
    //------------------------------------------------------------------------------------------------------------------------
    { {0x92, 0xf1, 0x80, 0x95, 0x40, 0xff, 0xff, 0xff }, 1,     4,      64,     1024,   0x0000,   974,    30,     0,      0,   0,   &PhysicArchiPara0 },   // ASU1GA
    //------------------------------------------------------------------------------------------------------------------------
    { {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff }, 0,     0,       0,        0,   0x0000,     0,     0,     0,      0,   0,          0        },   // NULL
};

//==============================================================================
//============================= DEFAULT NAND FLASH =============================
//==============================================================================
struct __NandPhyInfoPar_t DefaultNandTbl[] =
{
    //                    NAND_CHIP_ID                DieCnt SecCnt  PagCnt   BlkCnt    OpOpt   DatBlk  Freq  EccMode  ReadRetry  OperationPar
    //-----------------------------------------------------------------------------------------------------------------------
    { {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff }, 0,     0,       0,        0,   0x0000,     0,     0,     0,     0,    0,  &DefualtPhysicArchiPara }, //default
};

