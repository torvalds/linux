/*
 * $Id: sbe_promformat.h,v 2.2 2005/09/28 00:10:09 rickd PMCC4_3_1B $
 */

#ifndef _INC_SBE_PROMFORMAT_H_
#define _INC_SBE_PROMFORMAT_H_

/*-----------------------------------------------------------------------------
 * sbe_promformat.h - Contents of seeprom used by dvt and manufacturing tests
 *
 * Copyright (C) 2002-2005  SBE, Inc.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 * For further information, contact via email: support@sbei.com
 * SBE, Inc.  San Ramon, California  U.S.A.
 *
 *-----------------------------------------------------------------------------
 * RCS info:
 * RCS revision: $Revision: 2.2 $
 * Last changed on $Date: 2005/09/28 00:10:09 $
 * Changed by $Author: rickd $
 *-----------------------------------------------------------------------------
 * $Log: sbe_promformat.h,v $
 * Revision 2.2  2005/09/28 00:10:09  rickd
 * Add EEPROM sample from C4T1E1 board.
 *
 * Revision 2.1  2005/05/04 17:18:24  rickd
 * Initial CI.
 *
 *-----------------------------------------------------------------------------
 */


/***
 *  PMCC4 SAMPLE EEPROM IMAGE
 *
 *  eeprom[00]:  01 11 76 07  01 00 a0 d6
 *  eeprom[08]:  22 34 56 3e  5b c1 1c 3e
 *  eeprom[16]:  5b e1 b6 00  00 00 01 00
 *  eeprom[24]:  00 08 46 d3  7b 5e a8 fb
 *  eeprom[32]:  f7 ef df bf  7f 55 00 01
 *  eeprom[40]:  02 04 08 10  20 40 80 ff
 *  eeprom[48]:  fe fd fb f7  ef df bf 7f
 *
 ***/


/*------------------------------------------------------------------------
 *          Type 1 Format
 * byte:
 * 0    1  2    3  4      5   6   7    8  9  10    11 12 13 14    15 16 17 18
 * -------------------------------------------------------------------------
 * 01   11 76   SS SS     00 0A D6 <SERIAL NUM>    <Create TIME>  <Heatrun TIME>
 *       SBE    SUB       SERIAL #    (BCD)         (time_t)       (time_t)
 *       ID     VENDOR                              (format)       (format)
 *
 *  19 20 21 22    23 24 25 26
 *  Heat Run        Heat Run
 *  Iterations      Errors
 *------------------------------------------------------------------------
 *
 *
 *
 *           Type 2 Format  - Added length, CRC in fixed position
 * byte:
 * 0    1  2       3  4  5  6      7  8        9  10     11 12 13 14 15 16
 * -------------------------------------------------------------------------
 * 02   00 1A      CC CC CC CC    11  76       07 03    00 0A D6 <SERIAL NUM>
 *      Payload    SBE Crc32      SUB System   System    SERIAL/MAC
 *      Length                    VENDOR ID    ID
 *
 *  17 18 19 20     21 22 23 24     25 26 27 28    29 39 31 32
 * --------------------------------------------------------------------------
 *  <Create TIME>   <Heatrun TIME>   Heat Run      Heat Run
 *  (time_t)         (time_t)        Iterations    Errors
 *
 */

#ifdef __cplusplus
extern      "C"
{
#endif


#define STRUCT_OFFSET(type, symbol)  ((long)&(((type *)0)->symbol))

/*------------------------------------------------------------------------
 *  Historically different Prom format types.
 *
 *  For diagnostic and failure purposes, do not create a type 0x00 or a
 *  type 0xff
 *------------------------------------------------------------------------
 */
#define PROM_FORMAT_Unk   (-1)
#define PROM_FORMAT_TYPE1   1
#define PROM_FORMAT_TYPE2   2


/****** bit fields  for a type 1 formatted seeprom **************************/
    typedef struct
    {
        char        type;       /* 0x00 */
        char        Id[2];      /* 0x01-0x02 */
        char        SubId[2];   /* 0x03-0x04 */
        char        Serial[6];  /* 0x05-0x0a */
        char        CreateTime[4];      /* 0x0b-0x0e */
        char        HeatRunTime[4];     /* 0x0f-0x12 */
        char        HeatRunIterations[4];       /* 0x13-0x16 */
        char        HeatRunErrors[4];   /* 0x17-0x1a */
        char        Crc32[4];   /* 0x1b-0x1e */
    }           FLD_TYPE1;


/****** bit fields  for a type 2 formatted seeprom **************************/
    typedef struct
    {
        char        type;       /* 0x00 */
        char        length[2];  /* 0x01-0x02 */
        char        Crc32[4];   /* 0x03-0x06 */
        char        Id[2];      /* 0x07-0x08 */
        char        SubId[2];   /* 0x09-0x0a */
        char        Serial[6];  /* 0x0b-0x10 */
        char        CreateTime[4];      /* 0x11-0x14 */
        char        HeatRunTime[4];     /* 0x15-0x18 */
        char        HeatRunIterations[4];       /* 0x19-0x1c */
        char        HeatRunErrors[4];   /* 0x1d-0x20 */
    }           FLD_TYPE2;



/***** this union allows us to access the seeprom as an array of bytes ***/
/***** or as individual fields                                         ***/

#define SBE_EEPROM_SIZE    128
#define SBE_MFG_INFO_SIZE  sizeof(FLD_TYPE2)

    typedef union
    {
        char        bytes[128];
        FLD_TYPE1   fldType1;
        FLD_TYPE2   fldType2;
    }           PROMFORMAT;

#ifdef __cplusplus
}
#endif

#endif                          /*** _INC_SBE_PROMFORMAT_H_ ***/
