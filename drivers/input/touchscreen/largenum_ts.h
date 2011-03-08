/*
 * drivers/input/touchscreen/largenum_ts.h
 *
 * Copyright (C) 2010 ROCKCHIP, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef LARGENUM_TS_H
#define LARGENUM_TS_H

#define SIZE_OF_LARGENUM 3

typedef struct 
{
    unsigned char fNegative;
    union 
    {
        struct 
        {
            unsigned short  s[2 * SIZE_OF_LARGENUM];
        }   
        s16;
        
        struct 
        {
            unsigned int  u[SIZE_OF_LARGENUM];
        }   
        s32;
        
    }   
    u;
} 
LARGENUM, *PLARGENUM;

//
// Function prototypes
//
PLARGENUM 
LargeNumSet(
    PLARGENUM pNum, 
    int n
    );

unsigned char 
IsLargeNumNotZero(
    PLARGENUM pNum
    );

unsigned char
IsLargeNumNegative(
    PLARGENUM   pNum
    );

unsigned char
IsLargeNumMagGreaterThan(
    PLARGENUM   pNum1,
    PLARGENUM   pNum2
    );

unsigned char
IsLargeNumMagLessThan(
    PLARGENUM   pNum1,
    PLARGENUM   pNum2
    );

PLARGENUM
LargeNumMagInc(
    PLARGENUM   pNum
    );

char *
LargeNumToAscii(
    PLARGENUM   pNum
    );


PLARGENUM
LargeNumMagAdd(
    PLARGENUM   pNum1,
    PLARGENUM   pNum2,
    PLARGENUM   pResult
    );

PLARGENUM
LargeNumMagSub(
    PLARGENUM   pNum1,
    PLARGENUM   pNum2,
    PLARGENUM   pResult
    );

PLARGENUM
LargeNumAdd(
    PLARGENUM   pNum1,
    PLARGENUM   pNum2,
    PLARGENUM   pResult
    );

PLARGENUM
LargeNumSub(
    PLARGENUM   pNum1,
    PLARGENUM   pNum2,
    PLARGENUM   pResult
    );

PLARGENUM
LargeNumMulUint32(
    unsigned int      a,
    unsigned int      b,
    PLARGENUM   pResult
    );

PLARGENUM
LargeNumMulInt32(
    int       a,
    int       b,
    PLARGENUM   pResult
    );

PLARGENUM
LargeNumMult(
    PLARGENUM   pNum1,
    PLARGENUM   pNum2,
    PLARGENUM   pResult
    );

void
LargeNumRAShift(
    PLARGENUM   pNum,
    int       count
    );

unsigned int
LargeNumDivInt32(
    PLARGENUM   pNum,
    int       divisor,
    PLARGENUM   pResult
    );

int
LargeNumBits(
    PLARGENUM   pNum
    );

#endif /* LARGENUM_TS_H */
