/*
 * drivers/input/touchscreen/largenum_ts.c - largenum for rk2818 spi xpt2046 device and console
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
#include <linux/kernel.h>

#include "largenum_ts.h"

unsigned int
LargeNumSignedFormat(
    PLARGENUM   pNum
    );

PLARGENUM
LargeNumSet(
    PLARGENUM   pNum,
    int       n
    )
{
    int i;

    if(n < 0){
        pNum->u.s32.u[0] = -n;
        pNum->fNegative = 1;
    } else{
        pNum->u.s32.u[0] = n;
        pNum->fNegative=0;
    }
    for(i=1; i<SIZE_OF_LARGENUM; i++){
        pNum->u.s32.u[i] = 0;
    }
    return pNum;
}

unsigned char
IsLargeNumNotZero(
    PLARGENUM   pNum
    )
{
    int i;

    for(i=0; i<SIZE_OF_LARGENUM; i++){
        if(pNum->u.s32.u[i]){
            return 1;
        }
    }
    return 0;
}

unsigned char
IsLargeNumNegative(
    PLARGENUM   pNum
    )
{
    return (pNum->fNegative ? 1 : 0);

}

unsigned char
IsLargeNumMagGreaterThan(
    PLARGENUM   pNum1,
    PLARGENUM   pNum2
    )
{
    int i;

    for(i=SIZE_OF_LARGENUM-1; i>=0; i--){
        if(pNum1->u.s32.u[i] > pNum2->u.s32.u[i]){
            return 1;
        } else if(pNum1->u.s32.u[i] < pNum2->u.s32.u[i]){
            return 0;
        }
    }
    return 0;
}

unsigned char
IsLargeNumMagLessThan(
    PLARGENUM   pNum1,
    PLARGENUM   pNum2
    )
{
    int i;

    for(i=SIZE_OF_LARGENUM-1; i>=0; i--){
        if(pNum1->u.s32.u[i] < pNum2->u.s32.u[i]){
            return 1;
        } else if(pNum1->u.s32.u[i] > pNum2->u.s32.u[i]){
            return 0;
        }
    }
    return 0;
}

PLARGENUM
LargeNumMagInc(
    PLARGENUM   pNum
    )
{
    unsigned int  c;
    int     i;

    c = 1;
    for(i=0; i<SIZE_OF_LARGENUM; i++){
        pNum->u.s32.u[i] += c;
        if(pNum->u.s32.u[i]){
            c = 0;
        }
    }
    return pNum;
}

PLARGENUM
LargeNumMagAdd(
    PLARGENUM   pNum1,
    PLARGENUM   pNum2,
    PLARGENUM   pResult
    )
{
    unsigned int      c;
    unsigned int      i;
    unsigned int      a;
    unsigned int      b;

    c = 0;
    for(i=0; i<SIZE_OF_LARGENUM; i++){
        a = pNum1->u.s32.u[i];
        b = pNum2->u.s32.u[i];
        pResult->u.s32.u[i] = a + b + c;
        if(c){
            if(pResult->u.s32.u[i] <= a){
                c = 1;
            } else {
                c = 0;
            }

        } else {
            if(pResult->u.s32.u[i] < a){
                c = 1;
            } else {
                c = 0;
            }

        }
    }
    return pResult;
}

PLARGENUM
LargeNumMagSub(
    PLARGENUM   pNum1,
    PLARGENUM   pNum2,
    PLARGENUM   pResult
    )
{
    unsigned int      c;
    unsigned int      i;
    unsigned int      a;
    unsigned int      b;

    c = 1;
    for(i=0; i<SIZE_OF_LARGENUM; i++){
        a = pNum1->u.s32.u[i];
        b = ~(pNum2->u.s32.u[i]);
        pResult->u.s32.u[i] = a + b + c;
        if(c){
            if(pResult->u.s32.u[i] <= a){
                c = 1;
            } else {
                c = 0;
            }

        } else {
            if(pResult->u.s32.u[i] < a){
                c = 1;
            } else {
                c = 0;
            }

        }
    }
    return pResult;
}

PLARGENUM
LargeNumAdd(
    PLARGENUM   pNum1,
    PLARGENUM   pNum2,
    PLARGENUM   pResult
    )
{
    unsigned char    fNegative1;
    unsigned char    fNegative2;

    fNegative1 = IsLargeNumNegative(pNum1);
    fNegative2 = IsLargeNumNegative(pNum2);

    if(fNegative1 != fNegative2){
        if(IsLargeNumMagGreaterThan(pNum1, pNum2)){
            LargeNumMagSub(pNum1, pNum2, pResult);
        } else {
            LargeNumMagSub(pNum2, pNum1, pResult);
            fNegative1 = !fNegative1;
        }
    } else {
        LargeNumMagAdd(pNum1, pNum2, pResult);
    }
    if(!IsLargeNumNotZero(pResult)){
        pResult->fNegative = 0;
    } else {
        pResult->fNegative = fNegative1;
    }
    return pResult;
}

PLARGENUM
LargeNumSub(
    PLARGENUM   pNum1,
    PLARGENUM   pNum2,
    PLARGENUM   pResult
    )
{
    unsigned char    fNegative1;
    unsigned char    fNegative2;

    fNegative1 = IsLargeNumNegative(pNum1);
    fNegative2 = IsLargeNumNegative(pNum2);

    if(fNegative1 == fNegative2){
        if(IsLargeNumMagGreaterThan(pNum1, pNum2)){
            LargeNumMagSub(pNum1, pNum2, pResult);
        } else {
            LargeNumMagSub(pNum2, pNum1, pResult);
            fNegative1 = !fNegative1;
        }
    } else {
        LargeNumMagAdd(pNum1, pNum2, pResult);
    }
    if(!IsLargeNumNotZero(pResult)){
        pResult->fNegative = 0;
    } else {
        pResult->fNegative = fNegative1;
    }
    return pResult;
}

PLARGENUM
LargeNumMulUint32(
    unsigned int      a,
    unsigned int      b,
    PLARGENUM   pResult
    )
{
    unsigned int  a1, a0;
    unsigned int  b1, b0;
    unsigned int  r0;
    unsigned int  r1;
    unsigned int  r2;
    unsigned int  c;
    int     i;

    a1 = a >> 16;
    a0 = a & 0xffff;
    b1 = b >> 16;
    b0 = b & 0xffff;

    r0 = a0 * b0;
    r1 = a1 * b0 + a0 * b1;
    r2 = a1 * b1;

    pResult->u.s32.u[0] = (r1 << 16) + r0;
    if(pResult->u.s32.u[0] < r0){
        c = 1;
    } else {
        c = 0;
    }
    pResult->u.s32.u[1] = r2 + (r1 >> 16) + c;
    for(i=2; i<SIZE_OF_LARGENUM; i++){
        pResult->u.s32.u[i] = 0;
    }
    pResult->fNegative = 0;

    return pResult;
}

PLARGENUM
LargeNumMulInt32(
    int       a,
    int       b,
    PLARGENUM   pResult
    )
{
    unsigned char        fNegativeA;
    unsigned char        fNegativeB;

    if(a < 0){
        fNegativeA = 1;
        a = -a;
    } else {
        fNegativeA = 0;
    }

    if(b < 0){
        fNegativeB = 1;
        b = -b;
    } else {
        fNegativeB = 0;
    }

    LargeNumMulUint32(a, b, pResult);

    if(!IsLargeNumNotZero(pResult)){
        pResult->fNegative = 0;
    } else {
        if(fNegativeA != fNegativeB){
            pResult->fNegative = 1;
        }
    }
    return pResult;
}

PLARGENUM
LargeNumMult(
    PLARGENUM   pNum1,
    PLARGENUM   pNum2,
    PLARGENUM   pResult
    )
{
    LARGENUM    lNumTemp;
    LARGENUM    lNumSum;
    LARGENUM    lNumCarry;
    int         i;
    int         j;

    LargeNumSet(&lNumCarry, 0);
    for(i=0; i<SIZE_OF_LARGENUM; i++){
        LargeNumSet(&lNumSum, 0);
        for(j=0; j<=i; j++){
            LargeNumMulUint32(pNum1->u.s32.u[j], pNum2->u.s32.u[i-j], &lNumTemp);
            LargeNumMagAdd(&lNumTemp, &lNumSum, &lNumSum);
        }
        LargeNumMagAdd(&lNumCarry, &lNumSum, &lNumSum);
        for(j=0; j<SIZE_OF_LARGENUM-1; j++){
            lNumCarry.u.s32.u[j] = lNumSum.u.s32.u[j+1];
        }
        pResult->u.s32.u[i] = lNumSum.u.s32.u[0];
    }

    if(!IsLargeNumNotZero(pResult)){
        pResult->fNegative = 0;
    } else {
        pResult->fNegative = (pNum1->fNegative != pNum2->fNegative);
    }
    return pResult;
}

unsigned int
LargeNumSignedFormat(
    PLARGENUM   pNum
    )
{
    int     i;
    unsigned int  c;

    if(IsLargeNumNegative(pNum)){
        c = 1;
        for(i=0; i<SIZE_OF_LARGENUM; i++){
            pNum->u.s32.u[i] = ~(pNum->u.s32.u[i]) + c;
            if(pNum->u.s32.u[i]){
                c = 0;
            }
        }
        return 0xffffffff;
    } else {
        return 0;
    }
}

void
LargeNumRAShift(
    PLARGENUM   pNum,
    int       count
    )
{
    int   shift32;
    int   countLeft;
    unsigned int  filler;
    int     i;
    int     j;

    filler = LargeNumSignedFormat(pNum);

    shift32 = count / 32;

    if(shift32 > (SIZE_OF_LARGENUM - 1)){
        for(i=0; i<SIZE_OF_LARGENUM; i++){
            pNum->u.s32.u[i] = filler;
        }
        return;
    }

    count %= 32;
    countLeft = 32 - count;
    for(i=0, j=shift32;;){
        pNum->u.s32.u[i] = (pNum->u.s32.u[j] >> count);
        if(j<(SIZE_OF_LARGENUM-1)){
            j++;            
            if (countLeft < 32) {
                // Shifting by >= 32 is undefined.
                pNum->u.s32.u[i] |= pNum->u.s32.u[j] << countLeft;
            }
            i++;
        } else {
            if (countLeft < 32) {
                // Shifting by >= 32 is undefined.
                pNum->u.s32.u[i] |= filler << countLeft;
            }
            i++;
            break;
        }
    }

    for(; i<SIZE_OF_LARGENUM; i++){
        pNum->u.s32.u[i] = filler;
    }
}

unsigned int
LargeNumDivInt32(
    PLARGENUM   pNum,
    int       divisor,
    PLARGENUM   pResult
    )
{
    unsigned int  s[2*SIZE_OF_LARGENUM];
    unsigned int  r;
    unsigned int  q;
    unsigned int  d;
    unsigned char    sd;
    int     i;

    for(i=0; i<2*SIZE_OF_LARGENUM; i++){
        s[i] = pNum->u.s16.s[i];
    }

    if(divisor < 0){
        divisor = -divisor;
        sd = 1;
    } else if(divisor == 0){
        //
        // This is a divide-by-zero error
        //
        for(i=0; i<SIZE_OF_LARGENUM; i++){
            pResult->u.s32.u[i] = 0xffffffff;
        }
        return 0xffffffff;
    } else {
        sd = 0;
    }

    r = 0;
    for(i=(2*SIZE_OF_LARGENUM-1); i>=0; i--){
        d = (r << 16) + s[i];
        q = d / divisor;
        r = d - q * divisor;
        s[i] = q;
    }

    for(i=0; i<2*SIZE_OF_LARGENUM; i++){
        pResult->u.s16.s[i] = s[i];
    }

    if(pNum->fNegative){
        LargeNumMagInc(pResult);
        r = divisor - r;
        if(sd == 0 && IsLargeNumNotZero(pResult)){
            pResult->fNegative = 1;
        } else {
            pResult->fNegative = 0;
        }

    } else {
        if(sd && IsLargeNumNotZero(pResult)){
            pResult->fNegative = 1;
        } else {
            pResult->fNegative = 0;
        }
    }

    return r;
}

int
LargeNumBits(
    PLARGENUM   pNum
    )
{
    static  unsigned int LargeNumMask[32] = {
        0x00000001,
        0x00000002,
        0x00000004,
        0x00000008,
        0x00000010,
        0x00000020,
        0x00000040,
        0x00000080,
        0x00000100,
        0x00000200,
        0x00000400,
        0x00000800,
        0x00001000,
        0x00002000,
        0x00004000,
        0x00008000,
        0x00010000,
        0x00020000,
        0x00040000,
        0x00080000,
        0x00100000,
        0x00200000,
        0x00400000,
        0x00800000,
        0x01000000,
        0x02000000,
        0x04000000,
        0x08000000,
        0x10000000,
        0x20000000,
        0x40000000,
        0x80000000,
        };

    int     i;
    int     j;
    unsigned int  u;

    for(i=(SIZE_OF_LARGENUM-1); i>=0; i--){
        u = pNum->u.s32.u[i];
        if(u){
            for(j=31; j>=0; j--){
                if(u & (LargeNumMask[j])){
                    return i * 32 + j + 1;
                }
            }
        }
    }
    return 0;
}

char *
LargeNumToAscii(
    PLARGENUM   pNum
    )
{
    static  char    buf[SIZE_OF_LARGENUM * 10 + 2];
    LARGENUM        lNum;
    char            *p;
    char            *q;
    unsigned int          r;
    int             s;

    p = buf + sizeof(buf) - 1;
    *p= 0;

    lNum = *pNum;

    s = pNum->fNegative;
    lNum.fNegative = 0;

    while(IsLargeNumNotZero(&lNum)){
        r = LargeNumDivInt32(&lNum, 10, &lNum);
        p--;
        *p = r + '0';
    }

    q = buf;

    if(s){
        *q++='-';
    }
    while(*p){
        //ASSERT(q <= p);
        //PREFAST_SUPPRESS(394, "q is <= p");
        *q++ = *p++;
    }

    if((q == buf) || (s && q == &(buf[1]))){
        *q++ = '0';
    }
    *q = 0;
    return buf;
}
