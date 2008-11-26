/*
 * Copyright 1998-2008 VIA Technologies, Inc. All Rights Reserved.
 * Copyright 2001-2008 S3 Graphics, Inc. All Rights Reserved.

 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation;
 * either version 2, or (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTIES OR REPRESENTATIONS; without even
 * the implied warranty of MERCHANTABILITY or FITNESS FOR
 * A PARTICULAR PURPOSE.See the GNU General Public License
 * for more details.

 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc.,
 * 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */
#ifndef __LCDTBL_H__
#define __LCDTBL_H__

#include "share.h"

/* CLE266 Software Power Sequence */
/* {Mask}, {Data}, {Delay} */
int PowerSequenceOn[3][3] =
    { {0x10, 0x08, 0x06}, {0x10, 0x08, 0x06}, {0x19, 0x1FE, 0x01} };
int PowerSequenceOff[3][3] =
    { {0x06, 0x08, 0x10}, {0x00, 0x00, 0x00}, {0xD2, 0x19, 0x01} };

/* ++++++ P880 ++++++ */
/*   Panel 1600x1200   */
struct io_reg P880_LCD_RES_6X4_16X12[] = {
	/*IGA2 Horizontal Total */
	{VIACR, CR50, 0xFF, 0x73}, {VIACR, CR55, 0x0F, 0x08},
	/*IGA2 Horizontal Blank End */
	{VIACR, CR53, 0xFF, 0x73}, {VIACR, CR54, 0x38, 0x00},
	{VIACR, CR5D, 0x40, 0x40},
	/*IGA2 Horizontal Total Shadow */
	{VIACR, CR6D, 0xFF, 0x5A}, {VIACR, CR71, 0x08, 0x00},
	/*IGA2 Horizontal Blank End Shadow */
	{VIACR, CR6E, 0xFF, 0x5E},
	/*IGA2 Offset */
	{VIACR, CR66, 0xFF, 0xD6}, {VIACR, CR67, 0x03, 0x00},
	 /*VCLK*/ {VIASR, SR44, 0xFF, 0x7D}, {VIASR, SR45, 0xFF, 0x8C},
	{VIASR, SR46, 0xFF, 0x02}

};

#define NUM_TOTAL_P880_LCD_RES_6X4_16X12 ARRAY_SIZE(P880_LCD_RES_6X4_16X12)

struct io_reg P880_LCD_RES_7X4_16X12[] = {
	/*IGA2 Horizontal Total */
	{VIACR, CR50, 0xFF, 0x67}, {VIACR, CR55, 0x0F, 0x08},
	/*IGA2 Horizontal Blank End */
	{VIACR, CR53, 0xFF, 0x67}, {VIACR, CR54, 0x38, 0x00},
	{VIACR, CR5D, 0x40, 0x40},
	/*IGA2 Horizontal Total Shadow */
	{VIACR, CR6D, 0xFF, 0x74}, {VIACR, CR71, 0x08, 0x00},
	/*IGA2 Horizontal Blank End Shadow */
	{VIACR, CR6E, 0xFF, 0x78},
	/*IGA2 Offset */
	{VIACR, CR66, 0xFF, 0xF5}, {VIACR, CR67, 0x03, 0x00},
	 /*VCLK*/ {VIASR, SR44, 0xFF, 0x78}, {VIASR, SR45, 0xFF, 0x8C},
	{VIASR, SR46, 0xFF, 0x01}

};

#define NUM_TOTAL_P880_LCD_RES_7X4_16X12 ARRAY_SIZE(P880_LCD_RES_7X4_16X12)

struct io_reg P880_LCD_RES_8X6_16X12[] = {
	/*IGA2 Horizontal Total */
	{VIACR, CR50, 0xFF, 0x65}, {VIACR, CR55, 0x0F, 0x08},
	/*IGA2 Horizontal Blank End */
	{VIACR, CR53, 0xFF, 0x65}, {VIACR, CR54, 0x38, 0x00},
	{VIACR, CR5D, 0x40, 0x40},
	/*IGA2 Horizontal Total Shadow */
	{VIACR, CR6D, 0xFF, 0x7F}, {VIACR, CR71, 0x08, 0x00},
	/*IGA2 Horizontal Blank End Shadow */
	{VIACR, CR6E, 0xFF, 0x83},
	/*IGA2 Offset */
	{VIACR, CR66, 0xFF, 0xE1}, {VIACR, CR67, 0x03, 0x00},
	 /*VCLK*/ {VIASR, SR44, 0xFF, 0x6D}, {VIASR, SR45, 0xFF, 0x88},
	{VIASR, SR46, 0xFF, 0x03}

};

#define NUM_TOTAL_P880_LCD_RES_8X6_16X12 ARRAY_SIZE(P880_LCD_RES_8X6_16X12)

struct io_reg P880_LCD_RES_10X7_16X12[] = {
	/*IGA2 Horizontal Total */
	{VIACR, CR50, 0xFF, 0x65}, {VIACR, CR55, 0x0F, 0x08},
	/*IGA2 Horizontal Blank End */
	{VIACR, CR53, 0xFF, 0x65}, {VIACR, CR54, 0x38, 0x00},
	{VIACR, CR5D, 0x40, 0x40},
	/*IGA2 Horizontal Total Shadow */
	{VIACR, CR6D, 0xFF, 0xAB}, {VIACR, CR71, 0x08, 0x00},
	/*IGA2 Horizontal Blank End Shadow */
	{VIACR, CR6E, 0xFF, 0xAF},
	/*IGA2 Offset */
	{VIACR, CR66, 0xFF, 0xF0}, {VIACR, CR67, 0x03, 0x00},
	 /*VCLK*/ {VIASR, SR44, 0xFF, 0x92}, {VIASR, SR45, 0xFF, 0x88},
	{VIASR, SR46, 0xFF, 0x03}

};

#define NUM_TOTAL_P880_LCD_RES_10X7_16X12 ARRAY_SIZE(P880_LCD_RES_10X7_16X12)

struct io_reg P880_LCD_RES_12X10_16X12[] = {
	/*IGA2 Horizontal Total */
	{VIACR, CR50, 0xFF, 0x7D}, {VIACR, CR55, 0x0F, 0x08},
	/*IGA2 Horizontal Blank End */
	{VIACR, CR53, 0xFF, 0x7D}, {VIACR, CR54, 0x38, 0x00},
	{VIACR, CR5D, 0x40, 0x40},
	/*IGA2 Horizontal Total Shadow */
	{VIACR, CR6D, 0xFF, 0xD0}, {VIACR, CR71, 0x08, 0x00},
	/*IGA2 Horizontal Blank End Shadow */
	{VIACR, CR6E, 0xFF, 0xD4},
	/*IGA2 Offset */
	{VIACR, CR66, 0xFF, 0xFA}, {VIACR, CR67, 0x03, 0x00},
	 /*VCLK*/ {VIASR, SR44, 0xFF, 0xF6}, {VIASR, SR45, 0xFF, 0x88},
	{VIASR, SR46, 0xFF, 0x05}

};

#define NUM_TOTAL_P880_LCD_RES_12X10_16X12 ARRAY_SIZE(P880_LCD_RES_12X10_16X12)

/*   Panel 1400x1050   */
struct io_reg P880_LCD_RES_6X4_14X10[] = {
	/* 640x480                          */
	/* IGA2 Horizontal Total            */
	{VIACR, CR50, 0xFF, 0x9D}, {VIACR, CR55, 0x0F, 0x56},
	/* IGA2 Horizontal Blank End        */
	{VIACR, CR53, 0xFF, 0x9D}, {VIACR, CR54, 0x38, 0x75},
	{VIACR, CR5D, 0x40, 0x24},
	/* IGA2 Horizontal Total Shadow     */
	{VIACR, CR6D, 0xFF, 0x5F}, {VIACR, CR71, 0x08, 0x44},
	/* IGA2 Horizontal Blank End Shadow */
	{VIACR, CR6E, 0xFF, 0x63},
	/* IGA2 Offset                      */
	{VIACR, CR66, 0xFF, 0xB4}, {VIACR, CR67, 0x03, 0x00},
	/* VCLK                             */
	{VIASR, SR44, 0xFF, 0xC6}, {VIASR, SR45, 0xFF, 0x8C},
	{VIASR, SR46, 0xFF, 0x05}
};

#define NUM_TOTAL_P880_LCD_RES_6X4_14X10 ARRAY_SIZE(P880_LCD_RES_6X4_14X10)

struct io_reg P880_LCD_RES_8X6_14X10[] = {
	/* 800x600                          */
	/* IGA2 Horizontal Total            */
	{VIACR, CR50, 0xFF, 0x9D}, {VIACR, CR55, 0x0F, 0x56},
	/* IGA2 Horizontal Blank End        */
	{VIACR, CR53, 0xFF, 0x9D}, {VIACR, CR54, 0x38, 0x75},
	{VIACR, CR5D, 0x40, 0x24},
	/* IGA2 Horizontal Total Shadow     */
	{VIACR, CR6D, 0xFF, 0x7F}, {VIACR, CR71, 0x08, 0x44},
	/* IGA2 Horizontal Blank End Shadow */
	{VIACR, CR6E, 0xFF, 0x83},
	/* IGA2 Offset                      */
	{VIACR, CR66, 0xFF, 0xBE}, {VIACR, CR67, 0x03, 0x00},
	/* VCLK                             */
	{VIASR, SR44, 0xFF, 0x06}, {VIASR, SR45, 0xFF, 0x8D},
	{VIASR, SR46, 0xFF, 0x05}
};

#define NUM_TOTAL_P880_LCD_RES_8X6_14X10 ARRAY_SIZE(P880_LCD_RES_8X6_14X10)

/* ++++++ K400 ++++++ */
/*   Panel 1600x1200   */
struct io_reg K400_LCD_RES_6X4_16X12[] = {
	/*IGA2 Horizontal Total */
	{VIACR, CR50, 0xFF, 0x73}, {VIACR, CR55, 0x0F, 0x08},
	/*IGA2 Horizontal Blank End */
	{VIACR, CR53, 0xFF, 0x73}, {VIACR, CR54, 0x38, 0x00},
	{VIACR, CR5D, 0x40, 0x40},
	/*IGA2 Horizontal Total Shadow */
	{VIACR, CR6D, 0xFF, 0x5A}, {VIACR, CR71, 0x08, 0x00},
	/*IGA2 Horizontal Blank End Shadow */
	{VIACR, CR6E, 0xFF, 0x5E},
	/*IGA2 Offset */
	{VIACR, CR66, 0xFF, 0xDA}, {VIACR, CR67, 0x03, 0x00},
	 /*VCLK*/ {VIASR, SR46, 0xFF, 0xC4}, {VIASR, SR47, 0xFF, 0x7F}
};

#define NUM_TOTAL_K400_LCD_RES_6X4_16X12 ARRAY_SIZE(K400_LCD_RES_6X4_16X12)

struct io_reg K400_LCD_RES_7X4_16X12[] = {
	/*IGA2 Horizontal Total */
	{VIACR, CR50, 0xFF, 0x67}, {VIACR, CR55, 0x0F, 0x08},
	/*IGA2 Horizontal Blank End */
	{VIACR, CR53, 0xFF, 0x67}, {VIACR, CR54, 0x38, 0x00},
	{VIACR, CR5D, 0x40, 0x40},
	/*IGA2 Horizontal Total Shadow */
	{VIACR, CR6D, 0xFF, 0x74}, {VIACR, CR71, 0x08, 0x00},
	/*IGA2 Horizontal Blank End Shadow */
	{VIACR, CR6E, 0xFF, 0x78},
	/*IGA2 Offset */
	{VIACR, CR66, 0xFF, 0xF5}, {VIACR, CR67, 0x03, 0x00},
	 /*VCLK*/ {VIASR, SR46, 0xFF, 0x46}, {VIASR, SR47, 0xFF, 0x3D}
};

#define NUM_TOTAL_K400_LCD_RES_7X4_16X12 ARRAY_SIZE(K400_LCD_RES_7X4_16X12)

struct io_reg K400_LCD_RES_8X6_16X12[] = {
	/*IGA2 Horizontal Total */
	{VIACR, CR50, 0xFF, 0x65}, {VIACR, CR55, 0x0F, 0x08},
	/*IGA2 Horizontal Blank End */
	{VIACR, CR53, 0xFF, 0x65}, {VIACR, CR54, 0x38, 0x00},
	{VIACR, CR5D, 0x40, 0x40},
	/*IGA2 Horizontal Total Shadow */
	{VIACR, CR6D, 0xFF, 0x7F}, {VIACR, CR71, 0x08, 0x00},
	/*IGA2 Horizontal Blank End Shadow */
	{VIACR, CR6E, 0xFF, 0x83},
	/*IGA2 Offset */
	{VIACR, CR66, 0xFF, 0xE1}, {VIACR, CR67, 0x03, 0x00},
	 /*VCLK*/ {VIASR, SR46, 0xFF, 0x85}, {VIASR, SR47, 0xFF, 0x6F}
};

#define NUM_TOTAL_K400_LCD_RES_8X6_16X12 ARRAY_SIZE(K400_LCD_RES_8X6_16X12)

struct io_reg K400_LCD_RES_10X7_16X12[] = {
	/*IGA2 Horizontal Total */
	{VIACR, CR50, 0xFF, 0x65}, {VIACR, CR55, 0x0F, 0x08},
	/*IGA2 Horizontal Blank End */
	{VIACR, CR53, 0xFF, 0x65}, {VIACR, CR54, 0x38, 0x00},
	{VIACR, CR5D, 0x40, 0x40},
	/*IGA2 Horizontal Total Shadow */
	{VIACR, CR6D, 0xFF, 0xAB}, {VIACR, CR71, 0x08, 0x00},
	/*IGA2 Horizontal Blank End Shadow */
	{VIACR, CR6E, 0xFF, 0xAF},
	/*IGA2 Offset */
	{VIACR, CR66, 0xFF, 0xF0}, {VIACR, CR67, 0x03, 0x00},
	 /*VCLK*/ {VIASR, SR46, 0xFF, 0x45}, {VIASR, SR47, 0xFF, 0x4A}
};

#define NUM_TOTAL_K400_LCD_RES_10X7_16X12 ARRAY_SIZE(K400_LCD_RES_10X7_16X12)

struct io_reg K400_LCD_RES_12X10_16X12[] = {
	/*IGA2 Horizontal Total */
	{VIACR, CR50, 0xFF, 0x7D}, {VIACR, CR55, 0x0F, 0x08},
	/*IGA2 Horizontal Blank End */
	{VIACR, CR53, 0xFF, 0x7D}, {VIACR, CR54, 0x38, 0x00},
	{VIACR, CR5D, 0x40, 0x40},
	/*IGA2 Horizontal Total Shadow */
	{VIACR, CR6D, 0xFF, 0xD0}, {VIACR, CR71, 0x08, 0x00},
	/*IGA2 Horizontal Blank End Shadow */
	{VIACR, CR6E, 0xFF, 0xD4},
	/*IGA2 Offset */
	{VIACR, CR66, 0xFF, 0xFA}, {VIACR, CR67, 0x03, 0x00},
	 /*VCLK*/ {VIASR, SR46, 0xFF, 0x47}, {VIASR, SR47, 0xFF, 0x7C}
};

#define NUM_TOTAL_K400_LCD_RES_12X10_16X12 ARRAY_SIZE(K400_LCD_RES_12X10_16X12)

/*   Panel 1400x1050   */
struct io_reg K400_LCD_RES_6X4_14X10[] = {
	/* 640x400                          */
	/* IGA2 Horizontal Total            */
	{VIACR, CR50, 0xFF, 0x9D}, {VIACR, CR55, 0x0F, 0x56},
	/* IGA2 Horizontal Blank End        */
	{VIACR, CR53, 0xFF, 0x9D}, {VIACR, CR54, 0x38, 0x75},
	{VIACR, CR5D, 0x40, 0x24},
	/* IGA2 Horizontal Total Shadow     */
	{VIACR, CR6D, 0xFF, 0x5F}, {VIACR, CR71, 0x08, 0x44},
	/* IGA2 Horizontal Blank End Shadow */
	{VIACR, CR6E, 0xFF, 0x63},
	/* IGA2 Offset                      */
	{VIACR, CR66, 0xFF, 0xB4}, {VIACR, CR67, 0x03, 0x00},
	/* VCLK                             */
	{VIASR, SR46, 0xFF, 0x07}, {VIASR, SR47, 0xFF, 0x19}
};

#define NUM_TOTAL_K400_LCD_RES_6X4_14X10 ARRAY_SIZE(K400_LCD_RES_6X4_14X10)

struct io_reg K400_LCD_RES_8X6_14X10[] = {
	/* 800x600                          */
	/* IGA2 Horizontal Total            */
	{VIACR, CR50, 0xFF, 0x9D}, {VIACR, CR55, 0x0F, 0x56},
	/* IGA2 Horizontal Blank End        */
	{VIACR, CR53, 0xFF, 0x9D}, {VIACR, CR54, 0x38, 0x75},
	{VIACR, CR5D, 0x40, 0x24},
	/* IGA2 Horizontal Total Shadow     */
	{VIACR, CR6D, 0xFF, 0x7F}, {VIACR, CR71, 0x08, 0x44},
	/* IGA2 Horizontal Blank End Shadow */
	{VIACR, CR6E, 0xFF, 0x83},
	/* IGA2 Offset                      */
	{VIACR, CR66, 0xFF, 0xBE}, {VIACR, CR67, 0x03, 0x00},
	/* VCLK                             */
	{VIASR, SR46, 0xFF, 0x07}, {VIASR, SR47, 0xFF, 0x21}
};

#define NUM_TOTAL_K400_LCD_RES_8X6_14X10 ARRAY_SIZE(K400_LCD_RES_8X6_14X10)

struct io_reg K400_LCD_RES_10X7_14X10[] = {
	/* 1024x768                         */
	/* IGA2 Horizontal Total            */
	{VIACR, CR50, 0xFF, 0x9D}, {VIACR, CR55, 0x0F, 0x56},
	/* IGA2 Horizontal Blank End        */
	{VIACR, CR53, 0xFF, 0x9D}, {VIACR, CR54, 0x38, 0x75},
	{VIACR, CR5D, 0x40, 0x24},
	/* IGA2 Horizontal Total Shadow     */
	{VIACR, CR6D, 0xFF, 0xA3}, {VIACR, CR71, 0x08, 0x44},
	/* IGA2 Horizontal Blank End Shadow */
	{VIACR, CR6E, 0xFF, 0xA7},
	/* IGA2 Offset                      */
	{VIACR, CR66, 0xFF, 0xC3}, {VIACR, CR67, 0x03, 0x04},
	/* VCLK                             */
	{VIASR, SR46, 0xFF, 0x05}, {VIASR, SR47, 0xFF, 0x1E}
};

#define NUM_TOTAL_K400_LCD_RES_10X7_14X10 ARRAY_SIZE(K400_LCD_RES_10X7_14X10)

struct io_reg K400_LCD_RES_12X10_14X10[] = {
	/* 1280x768, 1280x960, 1280x1024    */
	/* IGA2 Horizontal Total            */
	{VIACR, CR50, 0xFF, 0x97}, {VIACR, CR55, 0x0F, 0x56},
	/* IGA2 Horizontal Blank End        */
	{VIACR, CR53, 0xFF, 0x97}, {VIACR, CR54, 0x38, 0x75},
	{VIACR, CR5D, 0x40, 0x24},
	/* IGA2 Horizontal Total Shadow     */
	{VIACR, CR6D, 0xFF, 0xCE}, {VIACR, CR71, 0x08, 0x44},
	/* IGA2 Horizontal Blank End Shadow */
	{VIACR, CR6E, 0xFF, 0xD2},
	/* IGA2 Offset                      */
	{VIACR, CR66, 0xFF, 0xC9}, {VIACR, CR67, 0x03, 0x04},
	/* VCLK                             */
	{VIASR, SR46, 0xFF, 0x84}, {VIASR, SR47, 0xFF, 0x79}
};

#define NUM_TOTAL_K400_LCD_RES_12X10_14X10 ARRAY_SIZE(K400_LCD_RES_12X10_14X10)

/* ++++++ K400 ++++++ */
/*   Panel 1366x768   */
struct io_reg K400_LCD_RES_6X4_1366X7[] = {
	/* 640x400                          */
	/* IGA2 Horizontal Total            */
	{VIACR, CR50, 0xFF, 0x47}, {VIACR, CR55, 0x0F, 0x35},
	/* IGA2 Horizontal Blank End        */
	{VIACR, CR53, 0xFF, 0x47}, {VIACR, CR54, 0x38, 0x2B},
	{VIACR, CR5D, 0x40, 0x13},
	/* IGA2 Horizontal Total Shadow     */
	{VIACR, CR6D, 0xFF, 0x60}, {VIACR, CR71, 0x08, 0x23},
	/* IGA2 Horizontal Blank End Shadow */
	{VIACR, CR6E, 0xFF, 0x64},
	/* IGA2 Offset                      */
	{VIACR, CR66, 0xFF, 0x8C}, {VIACR, CR67, 0x03, 0x00},
	/* VCLK                             */
	{VIASR, SR46, 0xFF, 0x87}, {VIASR, SR47, 0xFF, 0x4C}
};

#define NUM_TOTAL_K400_LCD_RES_6X4_1366X7 ARRAY_SIZE(K400_LCD_RES_6X4_1366X7)

struct io_reg K400_LCD_RES_7X4_1366X7[] = {
	/* IGA2 Horizontal Total            */
	{VIACR, CR50, 0xFF, 0x3B}, {VIACR, CR55, 0x0F, 0x35},
	/* IGA2 Horizontal Blank End        */
	{VIACR, CR53, 0xFF, 0x3B}, {VIACR, CR54, 0x38, 0x2B},
	{VIACR, CR5D, 0x40, 0x13},
	/* IGA2 Horizontal Total Shadow     */
	{VIACR, CR6D, 0xFF, 0x71}, {VIACR, CR71, 0x08, 0x23},
	/* IGA2 Horizontal Blank End Shadow */
	{VIACR, CR6E, 0xFF, 0x75},
	/* IGA2 Offset                      */
	{VIACR, CR66, 0xFF, 0x96}, {VIACR, CR67, 0x03, 0x00},
	/* VCLK                             */
	{VIASR, SR46, 0xFF, 0x05}, {VIASR, SR47, 0xFF, 0x10}
};

#define NUM_TOTAL_K400_LCD_RES_7X4_1366X7 ARRAY_SIZE(K400_LCD_RES_7X4_1366X7)

struct io_reg K400_LCD_RES_8X6_1366X7[] = {
	/* 800x600                          */
	/* IGA2 Horizontal Total            */
	{VIACR, CR50, 0xFF, 0x37}, {VIACR, CR55, 0x0F, 0x35},
	/* IGA2 Horizontal Blank End        */
	{VIACR, CR53, 0xFF, 0x37}, {VIACR, CR54, 0x38, 0x2B},
	{VIACR, CR5D, 0x40, 0x13},
	/* IGA2 Horizontal Total Shadow     */
	{VIACR, CR6D, 0xFF, 0x7E}, {VIACR, CR71, 0x08, 0x23},
	/* IGA2 Horizontal Blank End Shadow */
	{VIACR, CR6E, 0xFF, 0x82},
	/* IGA2 Offset                      */
	{VIACR, CR66, 0xFF, 0x8C}, {VIACR, CR67, 0x03, 0x00},
	/* VCLK                             */
	{VIASR, SR46, 0xFF, 0x84}, {VIASR, SR47, 0xFF, 0xB9}
};

#define NUM_TOTAL_K400_LCD_RES_8X6_1366X7 ARRAY_SIZE(K400_LCD_RES_8X6_1366X7)

struct io_reg K400_LCD_RES_10X7_1366X7[] = {
	/* 1024x768                         */
	/* IGA2 Horizontal Total            */
	{VIACR, CR50, 0xFF, 0x9D}, {VIACR, CR55, 0x0F, 0x56},
	/* IGA2 Horizontal Blank End        */
	{VIACR, CR53, 0xFF, 0x9D}, {VIACR, CR54, 0x38, 0x75},
	{VIACR, CR5D, 0x40, 0x24},
	/* IGA2 Horizontal Total Shadow     */
	{VIACR, CR6D, 0xFF, 0xA3}, {VIACR, CR71, 0x08, 0x44},
	/* IGA2 Horizontal Blank End Shadow */
	{VIACR, CR6E, 0xFF, 0xA7},
	/* IGA2 Offset                      */
	{VIACR, CR66, 0xFF, 0xC3}, {VIACR, CR67, 0x03, 0x04},
	/* VCLK                             */
	{VIASR, SR46, 0xFF, 0x05}, {VIASR, SR47, 0xFF, 0x1E}
};

#define NUM_TOTAL_K400_LCD_RES_10X7_1366X7 ARRAY_SIZE(K400_LCD_RES_10X7_1366X7)

struct io_reg K400_LCD_RES_12X10_1366X7[] = {
	/* 1280x768, 1280x960, 1280x1024    */
	/* IGA2 Horizontal Total            */
	{VIACR, CR50, 0xFF, 0x97}, {VIACR, CR55, 0x0F, 0x56},
	/* IGA2 Horizontal Blank End        */
	{VIACR, CR53, 0xFF, 0x97}, {VIACR, CR54, 0x38, 0x75},
	{VIACR, CR5D, 0x40, 0x24},
	/* IGA2 Horizontal Total Shadow     */
	{VIACR, CR6D, 0xFF, 0xCE}, {VIACR, CR71, 0x08, 0x44},
	/* IGA2 Horizontal Blank End Shadow */
	{VIACR, CR6E, 0xFF, 0xD2},
	/* IGA2 Offset                      */
	{VIACR, CR66, 0xFF, 0xC9}, {VIACR, CR67, 0x03, 0x04},
	/* VCLK                             */
	{VIASR, SR46, 0xFF, 0x84}, {VIASR, SR47, 0xFF, 0x79}
};

#define NUM_TOTAL_K400_LCD_RES_12X10_1366X7\
			ARRAY_SIZE(K400_LCD_RES_12X10_1366X7)

/* ++++++ K400 ++++++ */
/*   Panel 1280x1024   */
struct io_reg K400_LCD_RES_6X4_12X10[] = {
	/*IGA2 Horizontal Total */
	{VIACR, CR50, 0xFF, 0x9D}, {VIACR, CR55, 0x0F, 0x46},
	/*IGA2 Horizontal Blank End */
	{VIACR, CR53, 0xFF, 0x9D}, {VIACR, CR54, 0x38, 0x74},
	{VIACR, CR5D, 0x40, 0x1C},
	/*IGA2 Horizontal Total Shadow */
	{VIACR, CR6D, 0xFF, 0x5F}, {VIACR, CR71, 0x08, 0x34},
	/*IGA2 Horizontal Blank End Shadow */
	{VIACR, CR6E, 0xFF, 0x63},
	/*IGA2 Offset */
	{VIACR, CR66, 0xFF, 0xAA}, {VIACR, CR67, 0x03, 0x00},
	 /*VCLK*/ {VIASR, SR46, 0xFF, 0x07}, {VIASR, SR47, 0xFF, 0x19}
};

#define NUM_TOTAL_K400_LCD_RES_6X4_12X10 ARRAY_SIZE(K400_LCD_RES_6X4_12X10)

struct io_reg K400_LCD_RES_7X4_12X10[] = {
	/*IGA2 Horizontal Total */
	{VIACR, CR50, 0xFF, 0x9D}, {VIACR, CR55, 0x0F, 0x46},
	/*IGA2 Horizontal Blank End */
	{VIACR, CR53, 0xFF, 0x9D}, {VIACR, CR54, 0x38, 0x74},
	{VIACR, CR5D, 0x40, 0x1C},
	/*IGA2 Horizontal Total Shadow */
	{VIACR, CR6D, 0xFF, 0x68}, {VIACR, CR71, 0x08, 0x34},
	/*IGA2 Horizontal Blank End Shadow */
	{VIACR, CR6E, 0xFF, 0x6C},
	/*IGA2 Offset */
	{VIACR, CR66, 0xFF, 0xA8}, {VIACR, CR67, 0x03, 0x00},
	 /*VCLK*/ {VIASR, SR46, 0xFF, 0x87}, {VIASR, SR47, 0xFF, 0xED}
};

#define NUM_TOTAL_K400_LCD_RES_7X4_12X10 ARRAY_SIZE(K400_LCD_RES_7X4_12X10)

struct io_reg K400_LCD_RES_8X6_12X10[] = {
	/*IGA2 Horizontal Total */
	{VIACR, CR50, 0xFF, 0x9D}, {VIACR, CR55, 0x0F, 0x46},
	/*IGA2 Horizontal Blank End */
	{VIACR, CR53, 0xFF, 0x9D}, {VIACR, CR54, 0x38, 0x74},
	{VIACR, CR5D, 0x40, 0x1C},
	/*IGA2 Horizontal Total Shadow */
	{VIACR, CR6D, 0xFF, 0x7F}, {VIACR, CR71, 0x08, 0x34},
	/*IGA2 Horizontal Blank End Shadow */
	{VIACR, CR6E, 0xFF, 0x83},
	/*IGA2 Offset */
	{VIACR, CR66, 0xFF, 0xBE}, {VIACR, CR67, 0x03, 0x00},
	 /*VCLK*/ {VIASR, SR46, 0xFF, 0x07}, {VIASR, SR47, 0xFF, 0x21}
};

#define NUM_TOTAL_K400_LCD_RES_8X6_12X10 ARRAY_SIZE(K400_LCD_RES_8X6_12X10)

struct io_reg K400_LCD_RES_10X7_12X10[] = {
	/*IGA2 Horizontal Total */
	{VIACR, CR50, 0xFF, 0x9D}, {VIACR, CR55, 0x0F, 0x46},
	/*IGA2 Horizontal Blank End */
	{VIACR, CR53, 0xFF, 0x9D}, {VIACR, CR54, 0x38, 0x74},
	{VIACR, CR5D, 0x40, 0x1C},
	/*IGA2 Horizontal Total Shadow */
	{VIACR, CR6D, 0xFF, 0xA3}, {VIACR, CR71, 0x08, 0x34},
	/*IGA2 Horizontal Blank End Shadow */
	{VIACR, CR6E, 0xFF, 0xA7},
	/*IGA2 Offset */
	{VIACR, CR66, 0xFF, 0xBE}, {VIACR, CR67, 0x03, 0x04},
	 /*VCLK*/ {VIASR, SR46, 0xFF, 0x05}, {VIASR, SR47, 0xFF, 0x1E}
};

#define NUM_TOTAL_K400_LCD_RES_10X7_12X10 ARRAY_SIZE(K400_LCD_RES_10X7_12X10)

/* ++++++ K400 ++++++ */
/*   Panel 1024x768    */
struct io_reg K400_LCD_RES_6X4_10X7[] = {
	/*IGA2 Horizontal Total */
	{VIACR, CR50, 0xFF, 0x47}, {VIACR, CR55, 0x0F, 0x35},
	/*IGA2 Horizontal Blank End */
	{VIACR, CR53, 0xFF, 0x47}, {VIACR, CR54, 0x38, 0x2B},
	{VIACR, CR5D, 0x40, 0x13},
	/*IGA2 Horizontal Total Shadow */
	{VIACR, CR6D, 0xFF, 0x60}, {VIACR, CR71, 0x08, 0x23},
	/*IGA2 Horizontal Blank End Shadow */
	{VIACR, CR6E, 0xFF, 0x64},
	/*IGA2 Offset */
	{VIACR, CR66, 0xFF, 0x8C}, {VIACR, CR67, 0x03, 0x00},
	 /*VCLK*/ {VIASR, SR46, 0xFF, 0x87}, {VIASR, SR47, 0xFF, 0x4C}
};

#define NUM_TOTAL_K400_LCD_RES_6X4_10X7 ARRAY_SIZE(K400_LCD_RES_6X4_10X7)

struct io_reg K400_LCD_RES_7X4_10X7[] = {
	/*IGA2 Horizontal Total */
	{VIACR, CR50, 0xFF, 0x3B}, {VIACR, CR55, 0x0F, 0x35},
	/*IGA2 Horizontal Blank End */
	{VIACR, CR53, 0xFF, 0x3B}, {VIACR, CR54, 0x38, 0x2B},
	{VIACR, CR5D, 0x40, 0x13},
	/*IGA2 Horizontal Total Shadow */
	{VIACR, CR6D, 0xFF, 0x71}, {VIACR, CR71, 0x08, 0x23},
	/*IGA2 Horizontal Blank End Shadow */
	{VIACR, CR6E, 0xFF, 0x75},
	/*IGA2 Offset */
	{VIACR, CR66, 0xFF, 0x96}, {VIACR, CR67, 0x03, 0x00},
	 /*VCLK*/ {VIASR, SR46, 0xFF, 0x05}, {VIASR, SR47, 0xFF, 0x10}
};

#define NUM_TOTAL_K400_LCD_RES_7X4_10X7 ARRAY_SIZE(K400_LCD_RES_7X4_10X7)

struct io_reg K400_LCD_RES_8X6_10X7[] = {
	/*IGA2 Horizontal Total */
	{VIACR, CR50, 0xFF, 0x37}, {VIACR, CR55, 0x0F, 0x35},
	/*IGA2 Horizontal Blank End */
	{VIACR, CR53, 0xFF, 0x37}, {VIACR, CR54, 0x38, 0x2B},
	{VIACR, CR5D, 0x40, 0x13},
	/*IGA2 Horizontal Total Shadow */
	{VIACR, CR6D, 0xFF, 0x7E}, {VIACR, CR71, 0x08, 0x23},
	/*IGA2 Horizontal Blank End Shadow */
	{VIACR, CR6E, 0xFF, 0x82},
	/*IGA2 Offset */
	{VIACR, CR66, 0xFF, 0x8C}, {VIACR, CR67, 0x03, 0x00},
	 /*VCLK*/ {VIASR, SR46, 0xFF, 0x84}, {VIASR, SR47, 0xFF, 0xB9}
};

#define NUM_TOTAL_K400_LCD_RES_8X6_10X7 ARRAY_SIZE(K400_LCD_RES_8X6_10X7)

/* ++++++ K400 ++++++ */
/*   Panel 800x600     */
struct io_reg K400_LCD_RES_6X4_8X6[] = {
	/*IGA2 Horizontal Total */
	{VIACR, CR50, 0xFF, 0x1A}, {VIACR, CR55, 0x0F, 0x34},
	/*IGA2 Horizontal Blank End */
	{VIACR, CR53, 0xFF, 0x1A}, {VIACR, CR54, 0x38, 0xE3},
	{VIACR, CR5D, 0x40, 0x12},
	/*IGA2 Horizontal Total Shadow */
	{VIACR, CR6D, 0xFF, 0x5F}, {VIACR, CR71, 0x08, 0x22},
	/*IGA2 Horizontal Blank End Shadow */
	{VIACR, CR6E, 0xFF, 0x63},
	/*IGA2 Offset */
	{VIACR, CR66, 0xFF, 0x6E}, {VIACR, CR67, 0x03, 0x00},
	 /*VCLK*/ {VIASR, SR46, 0xFF, 0x86}, {VIASR, SR47, 0xFF, 0xB3}
};

#define NUM_TOTAL_K400_LCD_RES_6X4_8X6 ARRAY_SIZE(K400_LCD_RES_6X4_8X6)

struct io_reg K400_LCD_RES_7X4_8X6[] = {
	/*IGA2 Horizontal Total */
	{VIACR, CR50, 0xFF, 0x1F}, {VIACR, CR55, 0x0F, 0x34},
	/*IGA2 Horizontal Blank End */
	{VIACR, CR53, 0xFF, 0x1F}, {VIACR, CR54, 0x38, 0xE3},
	{VIACR, CR5D, 0x40, 0x12},
	/*IGA2 Horizontal Total Shadow */
	{VIACR, CR6D, 0xFF, 0x7F}, {VIACR, CR71, 0x08, 0x22},
	/*IGA2 Horizontal Blank End Shadow */
	{VIACR, CR6E, 0xFF, 0x83},
	/*IGA2 Offset */
	{VIACR, CR66, 0xFF, 0x78}, {VIACR, CR67, 0x03, 0x00},
	 /*VCLK*/ {VIASR, SR46, 0xFF, 0xC4}, {VIASR, SR47, 0xFF, 0x59}
};

#define NUM_TOTAL_K400_LCD_RES_7X4_8X6 ARRAY_SIZE(K400_LCD_RES_7X4_8X6)

#endif /* __LCDTBL_H__ */
