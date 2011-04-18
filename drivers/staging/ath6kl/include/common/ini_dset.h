//------------------------------------------------------------------------------
// Copyright (c) 2004-2010 Atheros Corporation.  All rights reserved.
//
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
//
//
//
// Author(s): ="Atheros"
//------------------------------------------------------------------------------

#ifndef _INI_DSET_H_
#define _INI_DSET_H_

/*
 * Each of these represents a WHAL INI table, which consists
 * of an "address column" followed by 1 or more "value columns".
 *
 * Software uses the base WHAL_INI_DATA_ID+column to access a
 * DataSet that holds a particular column of data.
 */
typedef enum {
#if defined(AR6002_REV4) || defined(AR6003)
/* Add these definitions for compatibility  */
#define WHAL_INI_DATA_ID_BB_RFGAIN_LNA1 WHAL_INI_DATA_ID_BB_RFGAIN
#define WHAL_INI_DATA_ID_BB_RFGAIN_LNA2 WHAL_INI_DATA_ID_BB_RFGAIN
    WHAL_INI_DATA_ID_NULL               =0,
    WHAL_INI_DATA_ID_MODE_SPECIFIC      =1,  /* 2,3,4,5 */
    WHAL_INI_DATA_ID_COMMON             =6,  /* 7 */
    WHAL_INI_DATA_ID_BB_RFGAIN          =8,  /* 9,10 */
#ifdef FPGA
    WHAL_INI_DATA_ID_ANALOG_BANK0       =11, /* 12 */
    WHAL_INI_DATA_ID_ANALOG_BANK1       =13, /* 14 */
    WHAL_INI_DATA_ID_ANALOG_BANK2       =15, /* 16 */
    WHAL_INI_DATA_ID_ANALOG_BANK3       =17, /* 18, 19 */
    WHAL_INI_DATA_ID_ANALOG_BANK6       =20, /* 21,22 */
    WHAL_INI_DATA_ID_ANALOG_BANK7       =23, /* 24 */
    WHAL_INI_DATA_ID_ADDAC              =25, /* 26 */
#else
    WHAL_INI_DATA_ID_ANALOG_COMMON      =11, /* 12 */ 
    WHAL_INI_DATA_ID_ANALOG_MODE_SPECIFIC=13, /* 14,15 */ 
    WHAL_INI_DATA_ID_ANALOG_BANK6       =16, /* 17,18 */
    WHAL_INI_DATA_ID_MODE_OVERRIDES     =19, /* 20,21,22,23 */
    WHAL_INI_DATA_ID_COMMON_OVERRIDES   =24, /* 25 */
    WHAL_INI_DATA_ID_ANALOG_OVERRIDES   =26, /* 27,28 */
#endif /* FPGA */
#else
    WHAL_INI_DATA_ID_NULL               =0,
    WHAL_INI_DATA_ID_MODE_SPECIFIC      =1,  /* 2,3 */
    WHAL_INI_DATA_ID_COMMON             =4,  /* 5 */
    WHAL_INI_DATA_ID_BB_RFGAIN          =6,  /* 7,8 */
#define WHAL_INI_DATA_ID_BB_RFGAIN_LNA1 WHAL_INI_DATA_ID_BB_RFGAIN
    WHAL_INI_DATA_ID_ANALOG_BANK1       =9,  /* 10 */
    WHAL_INI_DATA_ID_ANALOG_BANK2       =11, /* 12 */
    WHAL_INI_DATA_ID_ANALOG_BANK3       =13, /* 14, 15 */
    WHAL_INI_DATA_ID_ANALOG_BANK6       =16, /* 17, 18 */
    WHAL_INI_DATA_ID_ANALOG_BANK7       =19, /* 20 */
    WHAL_INI_DATA_ID_MODE_OVERRIDES     =21, /* 22,23 */
    WHAL_INI_DATA_ID_COMMON_OVERRIDES   =24, /* 25 */
    WHAL_INI_DATA_ID_ANALOG_OVERRIDES   =26, /* 27,28 */
    WHAL_INI_DATA_ID_BB_RFGAIN_LNA2     =29, /* 30,31 */
#endif
    WHAL_INI_DATA_ID_MAX                =31
} WHAL_INI_DATA_ID;

typedef PREPACK struct {
    u16 freqIndex; // 1 - A mode 2 - B or G mode 0 - common
    u16 offset;
    u32 newValue;
} POSTPACK INI_DSET_REG_OVERRIDE;

#endif
