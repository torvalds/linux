/*
 *************************************************************************
 * Ralink Tech Inc.
 * 5F., No.36, Taiyuan St., Jhubei City,
 * Hsinchu County 302,
 * Taiwan, R.O.C.
 *
 * (c) Copyright 2002-2007, Ralink Technology, Inc.
 *
 * This program is free software; you can redistribute it and/or modify  *
 * it under the terms of the GNU General Public License as published by  *
 * the Free Software Foundation; either version 2 of the License, or     *
 * (at your option) any later version.                                   *
 *                                                                       *
 * This program is distributed in the hope that it will be useful,       *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 * GNU General Public License for more details.                          *
 *                                                                       *
 * You should have received a copy of the GNU General Public License     *
 * along with this program; if not, write to the                         *
 * Free Software Foundation, Inc.,                                       *
 * 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 *                                                                       *
 *************************************************************************

    Module Name:
	cmm_profile.c

    Abstract:

    Revision History:
    Who          When          What
    ---------    ----------    ----------------------------------------------
 */

#include "crypt_biginteger.h"

#ifdef __KERNEL__
#define DEBUGPRINT(fmt, args...) printk(KERN_ERR fmt, ## args)
#else
#define DEBUGPRINT(fmt, args...) printf(fmt, ## args)
#endif /* __KERNEL__ */

#define UINT32_HBITS(value)	(((value) >> 0x10) & 0xffff)
#define UINT32_LBITS(value)	((value) & 0xffff)
#define UINT32_GETBYTE(value, index)	(((value) >> ((index)*8)) & 0xff)
#define UINT64_HBITS(value)	(((value) >> 0x20) & 0xffffffff)
#define UINT64_LBITS(value)	((value) & 0xffffffff)

static UINT8 WPS_DH_P_VALUE[192] =
{
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xC9, 0x0F, 0xDA, 0xA2, 0x21, 0x68, 0xC2, 0x34,
    0xC4, 0xC6, 0x62, 0x8B, 0x80, 0xDC, 0x1C, 0xD1,
    0x29, 0x02, 0x4E, 0x08, 0x8A, 0x67, 0xCC, 0x74,
    0x02, 0x0B, 0xBE, 0xA6, 0x3B, 0x13, 0x9B, 0x22,
    0x51, 0x4A, 0x08, 0x79, 0x8E, 0x34, 0x04, 0xDD,
    0xEF, 0x95, 0x19, 0xB3, 0xCD, 0x3A, 0x43, 0x1B,
    0x30, 0x2B, 0x0A, 0x6D, 0xF2, 0x5F, 0x14, 0x37,
    0x4F, 0xE1, 0x35, 0x6D, 0x6D, 0x51, 0xC2, 0x45,
    0xE4, 0x85, 0xB5, 0x76, 0x62, 0x5E, 0x7E, 0xC6,
    0xF4, 0x4C, 0x42, 0xE9, 0xA6, 0x37, 0xED, 0x6B,
    0x0B, 0xFF, 0x5C, 0xB6, 0xF4, 0x06, 0xB7, 0xED,
    0xEE, 0x38, 0x6B, 0xFB, 0x5A, 0x89, 0x9F, 0xA5,
    0xAE, 0x9F, 0x24, 0x11, 0x7C, 0x4B, 0x1F, 0xE6,
    0x49, 0x28, 0x66, 0x51, 0xEC, 0xE4, 0x5B, 0x3D,
    0xC2, 0x00, 0x7C, 0xB8, 0xA1, 0x63, 0xBF, 0x05,
    0x98, 0xDA, 0x48, 0x36, 0x1C, 0x55, 0xD3, 0x9A,
    0x69, 0x16, 0x3F, 0xA8, 0xFD, 0x24, 0xCF, 0x5F,
    0x83, 0x65, 0x5D, 0x23, 0xDC, 0xA3, 0xAD, 0x96,
    0x1C, 0x62, 0xF3, 0x56, 0x20, 0x85, 0x52, 0xBB,
    0x9E, 0xD5, 0x29, 0x07, 0x70, 0x96, 0x96, 0x6D,
    0x67, 0x0C, 0x35, 0x4E, 0x4A, 0xBC, 0x98, 0x04,
    0xF1, 0x74, 0x6C, 0x08, 0xCA, 0x23, 0x73, 0x27,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
};

static UINT8 WPS_DH_R_VALUE[193] =
{
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00,
};

static UINT8 WPS_DH_X_VALUE[184] =
{
    0x36, 0xf0, 0x25, 0x5d, 0xde, 0x97, 0x3d, 0xcb,
    0x3b, 0x39, 0x9d, 0x74, 0x7f, 0x23, 0xe3, 0x2e,
    0xd6, 0xfd, 0xb1, 0xf7, 0x75, 0x98, 0x33, 0x8b,
    0xfd, 0xf4, 0x41, 0x59, 0xc4, 0xec, 0x64, 0xdd,
    0xae, 0xb5, 0xf7, 0x86, 0x71, 0xcb, 0xfb, 0x22,
    0x10, 0x6a, 0xe6, 0x4c, 0x32, 0xc5, 0xbc, 0xe4,
    0xcf, 0xd4, 0xf5, 0x92, 0x0d, 0xa0, 0xeb, 0xc8,
    0xb0, 0x1e, 0xca, 0x92, 0x92, 0xae, 0x3d, 0xba,
    0x1b, 0x7a, 0x4a, 0x89, 0x9d, 0xa1, 0x81, 0x39,
    0x0b, 0xb3, 0xbd, 0x16, 0x59, 0xc8, 0x12, 0x94,
    0xf4, 0x00, 0xa3, 0x49, 0x0b, 0xf9, 0x48, 0x12,
    0x11, 0xc7, 0x94, 0x04, 0xa5, 0x76, 0x60, 0x5a,
    0x51, 0x60, 0xdb, 0xee, 0x83, 0xb4, 0xe0, 0x19,
    0xb6, 0xd7, 0x99, 0xae, 0x13, 0x1b, 0xa4, 0xc2,
    0x3d, 0xff, 0x83, 0x47, 0x5e, 0x9c, 0x40, 0xfa,
    0x67, 0x25, 0xb7, 0xc9, 0xe3, 0xaa, 0x2c, 0x65,
    0x96, 0xe9, 0xc0, 0x57, 0x02, 0xdb, 0x30, 0xa0,
    0x7c, 0x9a, 0xa2, 0xdc, 0x23, 0x5c, 0x52, 0x69,
    0xe3, 0x9d, 0x0c, 0xa9, 0xdf, 0x7a, 0xad, 0x44,
    0x61, 0x2a, 0xd6, 0xf8, 0x8f, 0x69, 0x69, 0x92,
    0x98, 0xf3, 0xca, 0xb1, 0xb5, 0x43, 0x67, 0xfb,
    0x0e, 0x8b, 0x93, 0xf7, 0x35, 0xdc, 0x8c, 0xd8,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
};

static UINT8 WPS_DH_RRModP_VALUE[192] =
{
	0xe3, 0xb3, 0x3c, 0x72, 0x59, 0x54, 0x1c, 0x01,
	0xee, 0x9c, 0x9a, 0x21, 0x6c, 0xc1, 0xeb, 0xd2,
	0xae, 0x59, 0x41, 0x04, 0x79, 0x29, 0xa1, 0xc7,
	0xe9, 0xc3, 0xfa, 0x02, 0xcc, 0x24, 0x56, 0xef,
	0x10, 0x26, 0x30, 0xfa, 0x9a, 0x36, 0xa5, 0x1f,
	0x57, 0xb5, 0x93, 0x48, 0x67, 0x98, 0x44, 0x60,
	0x0b, 0xe4, 0x96, 0x47, 0xa8, 0x7c, 0x7b, 0x37,
	0xf8, 0x05, 0x65, 0x64, 0x96, 0x9b, 0x7f, 0x02,
	0xdc, 0x54, 0x1a, 0x4e, 0xd4, 0x05, 0x3f, 0x54,
	0xd6, 0x2a, 0x0e, 0xea, 0xb2, 0x70, 0x52, 0x1b,
	0x22, 0xc2, 0x96, 0xe9, 0xd4, 0x6f, 0xec, 0x23,
	0x8e, 0x1a, 0xbd, 0x78, 0x02, 0x23, 0xb7, 0x6b,
	0xb8, 0xfe, 0x61, 0x21, 0x19, 0x6b, 0x7e, 0x88,
	0x1c, 0x72, 0x9c, 0x7e, 0x04, 0xb9, 0xf7, 0x96,
	0x07, 0xcd, 0x0a, 0x62, 0x8e, 0x43, 0x41, 0x30,
	0x04, 0xa5, 0x41, 0xff, 0x93, 0xae, 0x1c, 0xeb,
	0xb0, 0x04, 0xa7, 0x50, 0xdb, 0x10, 0x2d, 0x39,
	0xb9, 0x05, 0x2b, 0xb4, 0x7a, 0x58, 0xf1, 0x70,
	0x7e, 0x8c, 0xd2, 0xac, 0x98, 0xb5, 0xfb, 0x62,
	0x8f, 0x23, 0x31, 0xb1, 0x3b, 0x01, 0xe0, 0x18,
	0xf4, 0x66, 0xee, 0x5f, 0xbc, 0xd4, 0x9d, 0x68,
	0xd0, 0xab, 0x92, 0xe1, 0x83, 0x97, 0xf2, 0x45,
	0x8e, 0x0e, 0x3e, 0x21, 0x67, 0x47, 0x8c, 0x73,
	0xf1, 0x15, 0xd2, 0x7d, 0x32, 0xc6, 0x95, 0xe0,
};

static UINT8 Value_0[1] = {0x00};
static UINT8 Value_1[1] = {0x01};
static PBIG_INTEGER pBI_U = NULL, pBI_S = NULL, pBI_O = NULL;
static UINT Bits_Of_R = 0;


VOID BigInteger_Print (
    IN PBIG_INTEGER pBI)
{
    int i = 0, j = 0;

    if ((pBI == NULL) || (pBI->pIntegerArray == NULL))
        return;

    if (strlen(pBI->Name) != 0)
        DEBUGPRINT("Name=%s\n", pBI->Name);
    DEBUGPRINT("AllocSize=%d, ArrayLength=%d, IntegerLength=%d, Signed=%d\n", pBI->AllocSize, pBI->ArrayLength, pBI->IntegerLength, pBI->Signed);
    for (i = (pBI->ArrayLength - 1), j = 0;i >=0;i--,j++) {
        DEBUGPRINT("%08x, ", pBI->pIntegerArray[i]);
        if ((j%8) == 7)
            DEBUGPRINT("\n");
    } /* End od for */
    DEBUGPRINT("\n\n");
} /* End of BigInteger_Print */


VOID BigInteger_Init (
    INOUT PBIG_INTEGER *pBI)
{
    if (*pBI != NULL)
        BigInteger_Free(pBI);

    if ((*pBI = (PBIG_INTEGER) kmalloc(sizeof(BIG_INTEGER), GFP_ATOMIC)) == NULL) {
        DEBUGPRINT("BigInteger_Init: allocate %d bytes memory failure.\n", (sizeof(BIG_INTEGER)));
        return;
    } /* End of if */

    NdisZeroMemory(*pBI, sizeof(BIG_INTEGER));
    (*pBI)->pIntegerArray = NULL;
    (*pBI)->Signed = 1;
} /* End of BigInteger_Init */


VOID BigInteger_Free_AllocSize (
    IN PBIG_INTEGER *pBI)
{
    if ((*pBI != NULL) && ((*pBI)->pIntegerArray != NULL)) {
        kfree((*pBI)->pIntegerArray);
        NdisZeroMemory(*pBI, sizeof(BIG_INTEGER));
        (*pBI)->pIntegerArray = NULL;
        (*pBI)->Signed = 1;
    } /* End of if */
} /* End of BigInteger_Free_AllocSize */


VOID BigInteger_Free (
    IN PBIG_INTEGER *pBI)
{
    if (*pBI != NULL) {
        BigInteger_Free_AllocSize(pBI);
        kfree(*pBI);
    } /* End of if */

    *pBI = NULL;
} /* End of BigInteger_Free */


VOID BigInteger_AllocSize (
    IN PBIG_INTEGER *pBI,
    IN UINT Length)
{
    UINT ArrayLength = 0;

    if (Length == 0)
        return;

    if (*pBI == NULL)
        BigInteger_Init(pBI);

    /* Caculate array size */
    ArrayLength = Length >> 0x2;
    if ((Length & 0x3) != 0)
        ArrayLength++;

    if (((*pBI)->pIntegerArray != NULL) && ((*pBI)->AllocSize < (sizeof(UINT32)*ArrayLength)))
        BigInteger_Free_AllocSize(pBI);

    if ((*pBI)->pIntegerArray == NULL) {
        if (((*pBI)->pIntegerArray = (UINT32 *) kmalloc(sizeof(UINT32)*ArrayLength, GFP_ATOMIC)) == NULL) {
            DEBUGPRINT("BigInteger_AllocSize: allocate %d bytes memory failure.\n", (sizeof(UINT32)*ArrayLength));
            return;
        } /* End of if */
        (*pBI)->AllocSize = sizeof(UINT32)*ArrayLength;
    } /* End of if */

    NdisZeroMemory((*pBI)->pIntegerArray, (*pBI)->AllocSize);
    (*pBI)->ArrayLength = ArrayLength;
    (*pBI)->IntegerLength = Length;
} /* End of BigInteger_AllocSize */


VOID BigInteger_ClearHighBits (
    IN PBIG_INTEGER pBI)
{
    INT BIArrayIndex, ShiftIndex = 0;
    UINT8 value;

    if ((pBI == NULL) || (pBI->pIntegerArray == NULL))
        return;

    BIArrayIndex = pBI->ArrayLength - 1;
    while ((BIArrayIndex >= 0) && (pBI->pIntegerArray[BIArrayIndex] == 0))
	BIArrayIndex--;

    if (BIArrayIndex >= 0) {
        value = 0;
        ShiftIndex = 4;
        while (value == 0) {
            ShiftIndex--;
            value = UINT32_GETBYTE(pBI->pIntegerArray[BIArrayIndex], ShiftIndex);
	} /* End of while */
    } /* End of if */

    if ((BIArrayIndex == -1) && (ShiftIndex == -1)) {
        pBI->IntegerLength = 1;
        pBI->ArrayLength = 1;
        pBI->Signed = 1;
    } else {
        pBI->IntegerLength = (BIArrayIndex*4) + ShiftIndex + 1;
        pBI->ArrayLength = BIArrayIndex + 1;
    } /* End of if */
} /* End of BigInteger_ClearHighBits */


VOID BigInteger_BI2Bin (
    IN PBIG_INTEGER pBI,
    OUT UINT8 *pValue,
    OUT UINT *Length)
{
    INT  ValueIndex, BIArrayIndex, ShiftIndex;
    UINT32  Number;

    if (pBI == NULL) {
        DEBUGPRINT("BigInteger_BI2Bin: pBI is NUll\n");
        *Length = 0;
        return;
    } /* End of if */

    if (*Length < (sizeof(UINT8) * pBI->IntegerLength)) {
        DEBUGPRINT("BigInteger_BI2Bin: length(%d) is not enough.\n", *Length);
        *Length = 0;
        return;
    } /* End of if */

    if (pBI->pIntegerArray == NULL) {
        *Length = 0;
        return;
    } /* End of if */

    BigInteger_ClearHighBits(pBI);
    if ((ShiftIndex = pBI->IntegerLength & 0x3) == 0)
       ShiftIndex = 4;
    BIArrayIndex = pBI->ArrayLength - 1;
    ValueIndex = 0;

    Number = pBI->pIntegerArray[BIArrayIndex];
    while (ValueIndex < pBI->IntegerLength)
    {
        pValue[ValueIndex++] = (UINT8) UINT32_GETBYTE(Number, ShiftIndex - 1);
        if ((--ShiftIndex) == 0) {
            ShiftIndex = 4;
            BIArrayIndex--;
            Number = pBI->pIntegerArray[BIArrayIndex];
        } /* End of if */
    } /* End of while */
    *Length = pBI->IntegerLength;
} /* End of BigInteger_BI2Bin */


VOID BigInteger_Bin2BI (
    IN UINT8 *pValue,
    IN UINT Length,
    OUT PBIG_INTEGER *pBI)
{
    INT  ValueIndex, BIArrayIndex, ShiftIndex;
    UINT32  Number;

    BigInteger_AllocSize(pBI, Length);

    if ((*pBI)->pIntegerArray != NULL) {
        Number = 0;
        if ((ShiftIndex = Length & 0x3) == 0)
            ShiftIndex = 4;
        BIArrayIndex = (*pBI)->ArrayLength - 1;
        ValueIndex = 0;
        while (ValueIndex < Length)
        {
            Number = (Number << 8) | (UINT8) pValue[ValueIndex++];
            if ((--ShiftIndex) == 0) {
                (*pBI)->pIntegerArray[BIArrayIndex] = Number;
                ShiftIndex = 4;
                BIArrayIndex--;
                Number = 0;
            } /* End of if */
        } /* End of while */
    } /* End of if */
} /* End of BigInteger_Bin2BI */


/* Calculate the bits of BigInteger, the highest bit is 1 */
VOID BigInteger_BitsOfBI (
    IN PBIG_INTEGER pBI,
    OUT UINT *Bits_Of_P)
{
    UINT32 Number, Index;

    Number = pBI->pIntegerArray[pBI->ArrayLength - 1];
    Index = 0;
    while ((!(Number & 0x80000000)) && (Index < 32)) {
        Number <<= 1;
        Index++;
    } /* End of while */
    *Bits_Of_P = (pBI->ArrayLength*sizeof(UINT32)) - Index;
} /* End of BigInteger_BitsOfBN */


INT BigInteger_GetBitValue (
    IN PBIG_INTEGER pBI,
    IN UINT Index)
{
    UINT Array = 0;
    UINT Shift = 0;

    if (Index > 0) {
        Array = (Index - 1) >> 0x5;
        Shift = (Index - 1) & 0x1F;
    }
    if (Array > pBI->ArrayLength)
        return 0;

    return ((pBI->pIntegerArray[Array] >> Shift) & 0x1);
} /* End of BigInteger_GetBitValue */


UINT8 BigInteger_GetByteValue (
    IN PBIG_INTEGER pBI,
    IN UINT Index)
{
    UINT Array = 0;
    UINT Shift = 0;

    if (Index > 0) {
        Array = (Index - 1) >> 0x2;
        Shift = (Index - 1) & 0x3;
    }
    if ((Array > pBI->ArrayLength) || (Index > pBI->IntegerLength))
        return 0;


    return (UINT8) UINT32_GETBYTE(pBI->pIntegerArray[Array], Shift - 1);
} /* End of BigInteger_GetByteValue */


VOID BigInteger_Copy (
    IN PBIG_INTEGER pBI_Copied,
    OUT PBIG_INTEGER *pBI_Result)
{
    BigInteger_AllocSize(pBI_Result, pBI_Copied->IntegerLength);
    NdisCopyMemory((*pBI_Result)->pIntegerArray, pBI_Copied->pIntegerArray, (sizeof(UINT32)*(*pBI_Result)->ArrayLength));
    (*pBI_Result)->ArrayLength = pBI_Copied->ArrayLength;
    (*pBI_Result)->IntegerLength = pBI_Copied->IntegerLength;
    (*pBI_Result)->Signed = pBI_Copied->Signed;
} /* End of BigInteger_Copy */


INT BigInteger_UnsignedCompare (
    IN PBIG_INTEGER pFirstOperand,
    IN PBIG_INTEGER pSecondOperand)
{
    INT BIArrayIndex;

    if (pFirstOperand->IntegerLength > pSecondOperand->IntegerLength)
        return 1;

    if (pFirstOperand->IntegerLength < pSecondOperand->IntegerLength)
        return -1;

    if (pFirstOperand->IntegerLength == pSecondOperand->IntegerLength) {
        for(BIArrayIndex = (pFirstOperand->ArrayLength - 1);BIArrayIndex >= 0 ; BIArrayIndex--)
        {
            if (pFirstOperand->pIntegerArray[BIArrayIndex] > pSecondOperand->pIntegerArray[BIArrayIndex])
                return 1;
            else if (pFirstOperand->pIntegerArray[BIArrayIndex] < pSecondOperand->pIntegerArray[BIArrayIndex])
                return -1;
        } /* End of for */
    } /* End of if */

    return 0;
} /* End of BigInteger_Compare */


VOID BigInteger_Add (
    IN PBIG_INTEGER pFirstOperand,
    IN PBIG_INTEGER pSecondOperand,
    OUT PBIG_INTEGER *pBI_Result)
{
    INT CompareResult;
    UINT32 BIArrayIndex;
    UINT64 Sum, Carry;
    PBIG_INTEGER pTempBI = NULL;

    if  ((pFirstOperand == NULL) || (pFirstOperand->pIntegerArray == NULL)
      || (pSecondOperand == NULL) || (pSecondOperand->pIntegerArray == NULL)) {
        DEBUGPRINT("BigInteger_Add: first or second operand is NULL.\n");
        return;
    } /* End of if */

    if (*pBI_Result == NULL)
        BigInteger_Init(pBI_Result);

    CompareResult = BigInteger_UnsignedCompare(pFirstOperand, pSecondOperand);
    if ((CompareResult == 0) & ((pFirstOperand->Signed * pSecondOperand->Signed) < 0)) {
        BigInteger_AllocSize(pBI_Result, 1);
        return ;
    } /* End of if */

    /*
     *  Singed table
     *  A + B || A > B || A < B
     *  ------------------------
     *  +   + ||   +   ||   +
     *  +   - ||   +   ||   -
     *  -   + ||   -   ||   +
     *  -   - ||   -   ||   -
     */
    if ((pFirstOperand->Signed * pSecondOperand->Signed) > 0) {
        if (pFirstOperand->IntegerLength > pSecondOperand->IntegerLength) {
                BigInteger_AllocSize(pBI_Result, pFirstOperand->IntegerLength + 1);
        } else {
                BigInteger_AllocSize(pBI_Result, pSecondOperand->IntegerLength + 1);
        } /* End of if */

        Carry = 0;
        for (BIArrayIndex=0; BIArrayIndex < (*pBI_Result)->ArrayLength; BIArrayIndex++)
        {

            Sum = 0;
            if (BIArrayIndex < pFirstOperand->ArrayLength)
                Sum += (UINT64) pFirstOperand->pIntegerArray[BIArrayIndex];

            if (BIArrayIndex < pSecondOperand->ArrayLength)
                Sum += (UINT64) pSecondOperand->pIntegerArray[BIArrayIndex];

            Sum += Carry;
            Carry = Sum  >> 32;
            (*pBI_Result)->pIntegerArray[BIArrayIndex] = (UINT32) (Sum & 0xffffffffUL);
        } /* End of for */
        (*pBI_Result)->Signed = pFirstOperand->Signed;
        BigInteger_ClearHighBits(*pBI_Result);
    } else {
        if  ((pFirstOperand->Signed == 1) & (pSecondOperand->Signed == -1)) {
            BigInteger_Copy(pSecondOperand, &pTempBI);
            pTempBI->Signed = 1;
            BigInteger_Sub(pFirstOperand, pTempBI, pBI_Result);
        } else if ((pFirstOperand->Signed == -1) & (pSecondOperand->Signed == 1)) {
            BigInteger_Copy(pFirstOperand, &pTempBI);
            pTempBI->Signed = 1;
            BigInteger_Sub(pSecondOperand, pTempBI, pBI_Result);
        } /* End of if */
    } /* End of if */

    BigInteger_Free(&pTempBI);
} /* End of BigInteger_Add */


VOID BigInteger_Sub (
    IN PBIG_INTEGER pFirstOperand,
    IN PBIG_INTEGER pSecondOperand,
    OUT PBIG_INTEGER *pBI_Result)
{
    INT CompareResult;
    UINT32 BIArrayIndex, Carry;
    PBIG_INTEGER pTempBI = NULL, pTempBI2 = NULL;

    if  ((pFirstOperand == NULL) || (pFirstOperand->pIntegerArray == NULL)
      || (pSecondOperand == NULL) || (pSecondOperand->pIntegerArray == NULL)) {
        DEBUGPRINT("BigInteger_Sub: first or second operand is NULL.\n");
        return;
    } /* End of if */

    if (*pBI_Result == NULL)
        BigInteger_Init(pBI_Result);

    CompareResult = BigInteger_UnsignedCompare(pFirstOperand, pSecondOperand);
    if ((CompareResult == 0) & ((pFirstOperand->Signed * pSecondOperand->Signed) > 0)) {
        BigInteger_AllocSize(pBI_Result, 1);
        return ;
    } /* End of if */

    BigInteger_Init(&pTempBI);
    BigInteger_Init(&pTempBI2);

    /*
     *  Singed table
     *  A - B || A > B || A < B
     *  ------------------------
     *  +   + ||   +   ||   -
     *  +   - ||   +   ||   +
     *  -   + ||   -   ||   -
     *  -   - ||   -   ||   +
     */
    if ((pFirstOperand->Signed * pSecondOperand->Signed) > 0) {
        if (CompareResult == 1) {
            BigInteger_Copy(pFirstOperand, &pTempBI);
            BigInteger_Copy(pSecondOperand, &pTempBI2);
        } else if (CompareResult == -1) {
            BigInteger_Copy(pSecondOperand, &pTempBI);
            BigInteger_Copy(pFirstOperand, &pTempBI2);
        } /* End of if */

        BigInteger_Copy(pTempBI, pBI_Result);
        Carry = 0;
        for (BIArrayIndex=0; BIArrayIndex < (*pBI_Result)->ArrayLength; BIArrayIndex++)
        {
            if (BIArrayIndex < pTempBI2->ArrayLength) {
                if ((*pBI_Result)->pIntegerArray[BIArrayIndex] >= (pTempBI2->pIntegerArray[BIArrayIndex] - Carry)) {
                    (*pBI_Result)->pIntegerArray[BIArrayIndex] = (*pBI_Result)->pIntegerArray[BIArrayIndex] - pTempBI2->pIntegerArray[BIArrayIndex] - Carry;
                    Carry = 0;
                } else {
                    (*pBI_Result)->pIntegerArray[BIArrayIndex] = 0xffffffffUL - pTempBI2->pIntegerArray[BIArrayIndex] - Carry + (*pBI_Result)->pIntegerArray[BIArrayIndex] + 1;
                    Carry = 1;
                } /* End of if */
            } else {
                if ((*pBI_Result)->pIntegerArray[BIArrayIndex] >= Carry) {
                    (*pBI_Result)->pIntegerArray[BIArrayIndex] -= Carry;
                    Carry = 0;
                } else {
                    (*pBI_Result)->pIntegerArray[BIArrayIndex] = 0xffffffffUL - Carry;
                    Carry = 1;
                } /* End of if */
            } /* End of if */
        } /* End of for */

        if  (((pFirstOperand->Signed == 1) & (pSecondOperand->Signed == 1) & (CompareResult == -1))
          || ((pFirstOperand->Signed == -1) & (pSecondOperand->Signed == -1) & (CompareResult == 1)))
            (*pBI_Result)->Signed = -1;

        BigInteger_ClearHighBits(*pBI_Result);
    } else {
        if  ((pFirstOperand->Signed == 1) & (pSecondOperand->Signed == -1)) {
            BigInteger_Copy(pSecondOperand, &pTempBI);
            pTempBI->Signed = 1;
            BigInteger_Add(pFirstOperand, pTempBI, pBI_Result);
        } else if ((pFirstOperand->Signed == -1) & (pSecondOperand->Signed == 1)) {
            BigInteger_Copy(pFirstOperand, &pTempBI);
            pTempBI->Signed = 1;
            BigInteger_Add(pTempBI, pSecondOperand, pBI_Result);
            (*pBI_Result)->Signed = -1;
        } /* End of if */
    } /* End of if */

    BigInteger_Free(&pTempBI);
    BigInteger_Free(&pTempBI2);
} /* End of BigInteger_Sub */


VOID BigInteger_Mul (
    IN PBIG_INTEGER pFirstOperand,
    IN PBIG_INTEGER pSecondOperand,
    OUT PBIG_INTEGER *pBI_Result)
{

    UINT32 BIFirstIndex, BISecondIndex;
    UINT64 FirstValue, SecondValue, Sum, Carry;

    if  ((pFirstOperand == NULL) || (pFirstOperand->pIntegerArray == NULL)
      || (pSecondOperand == NULL) || (pSecondOperand->pIntegerArray == NULL)) {
        DEBUGPRINT("BigInteger_Mul: first or second operand is NULL.\n");
        return;
    } /* End of if */

    /* The first or second operand is zero */
    if  (((pFirstOperand->IntegerLength  == 1) && (pFirstOperand->pIntegerArray[0]  == 0))
       ||((pSecondOperand->IntegerLength == 1) && (pSecondOperand->pIntegerArray[0] == 0))) {
        BigInteger_AllocSize(pBI_Result, 1);
        goto output;
    } /* End of if */

    /* The first or second operand is one */
    if  ((pFirstOperand->IntegerLength  == 1) && (pFirstOperand->pIntegerArray[0]  == 1)) {
        BigInteger_Copy(pSecondOperand, pBI_Result);
        goto output;
    } /* End of if */
    if  ((pSecondOperand->IntegerLength  == 1) && (pSecondOperand->pIntegerArray[0]  == 1)) {
        BigInteger_Copy(pFirstOperand, pBI_Result);
        goto output;
    } /* End of if */

    BigInteger_AllocSize(pBI_Result, pFirstOperand->IntegerLength + pSecondOperand->IntegerLength);

    for (BIFirstIndex=0; BIFirstIndex < pFirstOperand->ArrayLength; BIFirstIndex++)
    {
        Carry = 0;
        FirstValue = (UINT64) pFirstOperand->pIntegerArray[BIFirstIndex];
        if (FirstValue == 0) {
            continue;
        } else {
            for (BISecondIndex=0; BISecondIndex < pSecondOperand->ArrayLength; BISecondIndex++)
            {
                SecondValue = ((UINT64) pSecondOperand->pIntegerArray[BISecondIndex])*FirstValue;
                Sum = (UINT64) ((*pBI_Result)->pIntegerArray[BIFirstIndex + BISecondIndex] + SecondValue + Carry);
                Carry = Sum >> 32;
                (*pBI_Result)->pIntegerArray[BIFirstIndex + BISecondIndex] = (UINT32) (Sum & 0xffffffffUL);
            } /* End of for */
            while (Carry != 0) {
                Sum = (UINT64) (*pBI_Result)->pIntegerArray[BIFirstIndex + BISecondIndex];
                Sum += Carry;

                Carry = Sum >> 32;
                (*pBI_Result)->pIntegerArray[BIFirstIndex + BISecondIndex] = (UINT32) (Sum & 0xffffffffUL);
                BISecondIndex++;
            } /* End of while */
        } /* End of if */
    } /* End of for */

output:
    (*pBI_Result)->Signed = pFirstOperand->Signed * pSecondOperand->Signed;
    BigInteger_ClearHighBits(*pBI_Result);
} /* End of BigInteger_Mul */


VOID BigInteger_Square (
    IN PBIG_INTEGER pBI,
    OUT PBIG_INTEGER *pBI_Result)
{
    INT BIFirstIndex, BISecondIndex;
	UINT32 HBITS_Value, LBITS_Value, Temp1_Value, Temp2_Value, Carry32;
	UINT32 *Point_Of_S, *Point_Of_Result, *Point_Of_BI;
    UINT64 Result64_1, Result64_2, Carry64, TempValue64;

    if ((pBI == NULL) || (pBI->pIntegerArray == NULL)) {
        DEBUGPRINT("\tBigInteger_Square: the operand is NULL.\n");
        return;
    } /* End of if */

    /* The operand is zero */
    if  ((pBI->IntegerLength  == 1) && (pBI->pIntegerArray[0]  ==  0)) {
        BigInteger_AllocSize(pBI_Result, 1);
        goto output;
    } /* End of if */

    BigInteger_AllocSize(pBI_Result, (pBI->IntegerLength*2) + 20);
    BigInteger_AllocSize(&pBI_S, (pBI->IntegerLength*2) + 20);
    BigInteger_AllocSize(&pBI_O, (pBI->IntegerLength*2) + 20);

    /*
     * Input: pBI = {a_0, a_1, a_2, a_3, ..., a_n}
     * Step1. calculate a_0^2, a_1^2, a_2^2, a_3^2 ... a_n^2
     */
	Point_Of_S = pBI_S->pIntegerArray;
    for (BIFirstIndex=0; BIFirstIndex < pBI->ArrayLength; BIFirstIndex++)
    {
	HBITS_Value = UINT32_HBITS(pBI->pIntegerArray[BIFirstIndex]);
		LBITS_Value = UINT32_LBITS(pBI->pIntegerArray[BIFirstIndex]);
		Temp1_Value = HBITS_Value*LBITS_Value;
		Temp2_Value = (Temp1_Value & 0x7fff) << 0x11;
		Point_Of_S[0] = (LBITS_Value*LBITS_Value) + Temp2_Value;
		Point_Of_S[1] = (HBITS_Value*HBITS_Value) + ((Temp1_Value >> 0xf) & 0x1ffff);
		if (Point_Of_S[0] < Temp2_Value)
			Point_Of_S[1] += 1;

		Point_Of_S += 2;
    } /* End of for */

    /*
     * Step2. calculate a_0*{a_1, a_2, a_3, a_4, ..., a_n}
     */
    Point_Of_BI = pBI->pIntegerArray;
    Point_Of_Result = (*pBI_Result)->pIntegerArray;
    Point_Of_Result[0] = 0;
    TempValue64 = (UINT64) Point_Of_BI[0];
    Point_Of_Result++;
    Carry64 = 0;
    for (BIFirstIndex=1; BIFirstIndex < pBI->ArrayLength; BIFirstIndex++)
    {
        Result64_1 =  (UINT64) Point_Of_BI[BIFirstIndex]*TempValue64;
        Result64_1 += Carry64;
        Carry64 = (Result64_1 >> 32);
        Point_Of_Result[0] = (UINT32) (Result64_1 & 0xffffffffUL);
        Point_Of_Result++;
    } /* End of for */
    if (Carry64 > 0)
        Point_Of_Result[0] = (UINT32) (Carry64 & 0xffffffffUL);

    /*
     * Step3. calculate
     *           a_1*{a_2, a_3, a_4, ..., a_n}
     *           a_2*{a_3, a_4, a_5, ..., a_n}
     *           a_3*{a_4, a_5, a_6, ..., a_n}
     *           a_4*{a_5, a_6, a_7, ..., a_n}
     *           ...
     *           a_n-1*{a_n}
     */
    Point_Of_BI = pBI->pIntegerArray;
    for (BIFirstIndex=1; BIFirstIndex < (pBI->ArrayLength - 1); BIFirstIndex++)
    {
        Point_Of_Result = (*pBI_Result)->pIntegerArray;
        Point_Of_Result += (BIFirstIndex*2) + 1;
        TempValue64 = (UINT64) Point_Of_BI[BIFirstIndex];
        Carry64 = 0;
        for (BISecondIndex=(BIFirstIndex + 1); BISecondIndex < pBI->ArrayLength; BISecondIndex++)
        {
            Result64_1 = ((UINT64) Point_Of_Result[0]) + Carry64;
            Result64_2 = (UINT64) Point_Of_BI[BISecondIndex]*TempValue64;
            Carry64 = (Result64_1 >> 32);
            Result64_1 = (Result64_1 & 0xffffffffUL);
            Result64_1 = Result64_1 + Result64_2;
            Carry64 += (Result64_1 >> 32);
            Point_Of_Result[0] = (UINT32) (Result64_1 & 0xffffffffUL);
            Point_Of_Result++;
        } /* End of for */
        if (Carry64 > 0)
            Point_Of_Result[0] += (UINT32) (Carry64 & 0xffffffffUL);
    } /* End of for */

    BigInteger_ClearHighBits(*pBI_Result);
    BigInteger_Copy(*pBI_Result, &pBI_O);

    Carry32 = 0;
	for (BIFirstIndex=0; BIFirstIndex < pBI_O->ArrayLength; BIFirstIndex++) {
        pBI_O->pIntegerArray[BIFirstIndex] = (pBI_O->pIntegerArray[BIFirstIndex] << 1) | Carry32;
        if (pBI_O->pIntegerArray[BIFirstIndex] < (*pBI_Result)->pIntegerArray[BIFirstIndex])
            Carry32 = 1;
        else
            Carry32 = 0;
    } /* End of for */
    pBI_O->pIntegerArray[BIFirstIndex] = Carry32;
    pBI_O->IntegerLength++;
    pBI_O->ArrayLength++;
    BigInteger_ClearHighBits(pBI_O);

    BigInteger_Add(pBI_O, pBI_S, pBI_Result);
output:
    (*pBI_Result)->Signed = 1;
    BigInteger_ClearHighBits(*pBI_Result);
} /* End of BigInteger_Square */


VOID BigInteger_Div (
    IN PBIG_INTEGER pFirstOperand,
    IN PBIG_INTEGER pSecondOperand,
    OUT PBIG_INTEGER *pBI_Result,
    OUT PBIG_INTEGER *pBI_Remainder)
{
    INT CompareResult;
    INT Index, MulIndex, ComputeSize;
    UINT32 MulStart;
    UINT AllocLength, ArrayIndex, ShiftIndex;
    PBIG_INTEGER pTempBI = NULL, pTempBI2 = NULL, pMulBI = NULL;
    UINT8 SecondHighByte;

    if  ((pFirstOperand == NULL) || (pFirstOperand->pIntegerArray == NULL)
      || (pSecondOperand == NULL) || (pSecondOperand->pIntegerArray == NULL)) {
        DEBUGPRINT("BigInteger_Div: first or second operand is NULL.\n");
        return;
    } /* End of if */

    /* The second operand is zero */
    if ((pSecondOperand->IntegerLength == 1) && (pSecondOperand->pIntegerArray[0] == 0)) {
        DEBUGPRINT("BigInteger_Div: second operand is zero.\n");
        return;
    } /* End of if */

    if (*pBI_Result == NULL)
        BigInteger_Init(pBI_Result);
    if (*pBI_Remainder == NULL)
        BigInteger_Init(pBI_Remainder);

    /* The second operand is one */
    if  ((pSecondOperand->IntegerLength  == 1) && (pSecondOperand->pIntegerArray[0]  == 1)) {
        BigInteger_Copy(pFirstOperand, pBI_Result);
        BigInteger_Bin2BI(Value_0, 1, pBI_Remainder);
        goto output;
    } /* End of if */

    CompareResult = BigInteger_UnsignedCompare(pFirstOperand, pSecondOperand);
    if (CompareResult == 0) {
        BigInteger_Bin2BI(Value_1, 1, pBI_Result);
        BigInteger_Bin2BI(Value_0, 1, pBI_Remainder);
        goto output;
    } else if (CompareResult == -1) {
        BigInteger_Bin2BI(Value_0, 1, pBI_Result);
        BigInteger_Copy(pFirstOperand, pBI_Remainder);
        goto output;
    } /* End of if */
    BigInteger_AllocSize(pBI_Result, pFirstOperand->IntegerLength - pSecondOperand->IntegerLength + 1);
    BigInteger_AllocSize(pBI_Remainder, pSecondOperand->IntegerLength);

    AllocLength = (UINT) (pFirstOperand->IntegerLength << 1);
    BigInteger_AllocSize(&pTempBI, AllocLength);
    BigInteger_AllocSize(&pTempBI2, AllocLength);
    BigInteger_AllocSize(&pMulBI, AllocLength);

    BigInteger_Copy(pFirstOperand, pBI_Remainder);
    SecondHighByte = BigInteger_GetByteValue(pSecondOperand, pSecondOperand->IntegerLength);
    ComputeSize = (INT) pFirstOperand->IntegerLength - pSecondOperand->IntegerLength + 1;
    for (Index = (INT) ComputeSize;Index >= 0;Index--) {
        if (BigInteger_UnsignedCompare(*pBI_Remainder, pSecondOperand) == -1)
            break;

        if (((pSecondOperand->IntegerLength + Index) - (*pBI_Remainder)->IntegerLength) <= 1) {
            BigInteger_AllocSize(&pMulBI, Index + 1);
            ArrayIndex = 0;
            if (Index > 0)
                ArrayIndex = (UINT) (Index - 1) >> 2 ;
            ShiftIndex = (Index & 0x03);
            if (ShiftIndex == 0)
                ShiftIndex = 4;
            ShiftIndex--;
            MulStart = 0;
            MulStart = (BigInteger_GetByteValue((*pBI_Remainder), pFirstOperand->IntegerLength + Index - ComputeSize + 1) & 0xFF) << 8;
            MulStart = MulStart | (BigInteger_GetByteValue((*pBI_Remainder), pFirstOperand->IntegerLength + Index - ComputeSize) & 0xFF);
            if (MulStart < (UINT32) SecondHighByte)
                continue;

            MulStart = MulStart / (UINT32) SecondHighByte;

            if (MulStart > 0xFF)
                MulStart = 0x100;

            for (MulIndex = (INT) MulStart;MulIndex <= 0x101;MulIndex++) { /* 0xFFFF / 0xFF = 0x101 */
                if ((MulIndex > 0xFF) && (ShiftIndex == 3))
                        pMulBI->pIntegerArray[ArrayIndex + 1] = 0x01;
                pMulBI->pIntegerArray[ArrayIndex] = ((UINT) MulIndex << (8*ShiftIndex));
                BigInteger_Mul(pSecondOperand, pMulBI , &pTempBI);
                CompareResult = BigInteger_UnsignedCompare(*pBI_Remainder, pTempBI);
                if (CompareResult < 1) {
                    if (MulIndex > 1) {
                        if (CompareResult != 0) {
                            if ((MulIndex == 0x100) && (ShiftIndex == 3))
                                   pMulBI->pIntegerArray[ArrayIndex + 1] = 0;
                            pMulBI->pIntegerArray[ArrayIndex] = ((UINT) (MulIndex - 1) << (8*ShiftIndex));
                        } /* End of if */

                        BigInteger_Mul(pSecondOperand, pMulBI, &pTempBI);
                        BigInteger_Sub(*pBI_Remainder, pTempBI, &pTempBI2);
                        BigInteger_Copy(pTempBI2, pBI_Remainder);
                        BigInteger_Add(*pBI_Result, pMulBI, &pTempBI2);
                        BigInteger_Copy(pTempBI2, pBI_Result);
                    } /* End of if */
                    break;
                } /* End of if */

                if ((MulIndex >= 0x100) && (ShiftIndex == 3))
                   pMulBI->pIntegerArray[ArrayIndex++] = 0;
                pMulBI->pIntegerArray[ArrayIndex] = 0;
            } /* End of for */
        } /* End of if */
    } /* End of for */

    BigInteger_Free(&pTempBI);
    BigInteger_Free(&pTempBI2);
    BigInteger_Free(&pMulBI);
output:
    (*pBI_Result)->Signed = pFirstOperand->Signed * pSecondOperand->Signed;
    (*pBI_Remainder)->Signed = pFirstOperand->Signed * pSecondOperand->Signed;
    BigInteger_ClearHighBits(*pBI_Result);
    BigInteger_ClearHighBits(*pBI_Remainder);
} /* End of BigInteger_Div */


VOID BigInteger_Montgomery_Reduction (
    IN PBIG_INTEGER pBI_A,
    IN PBIG_INTEGER pBI_P,
     IN PBIG_INTEGER pBI_R,
    OUT PBIG_INTEGER *pBI_Result)
{
    UINT32 *Point_P, *Point_Result;
    UINT32 LoopCount;
    UINT64 Result64_1, Result64_2, Carry64, TempValue64;
    INT FirstLoop, SecondLoop;

    BigInteger_AllocSize(pBI_Result, pBI_A->IntegerLength+ pBI_P->IntegerLength + 20);
    BigInteger_Copy(pBI_A, pBI_Result);

    Point_P = pBI_P->pIntegerArray;
    Point_Result = (*pBI_Result)->pIntegerArray;

    LoopCount = Bits_Of_R >> 0x5;
    for (FirstLoop = 0;FirstLoop < LoopCount;FirstLoop++) {
        Carry64 = 0;
        TempValue64 = (UINT64) Point_Result[0];
        for (SecondLoop = 0;SecondLoop < pBI_P->ArrayLength;SecondLoop++) {
            Result64_1 = ((UINT64) Point_Result[SecondLoop]) + Carry64;
            Result64_2 = (UINT64) Point_P[SecondLoop]*TempValue64;
            Carry64 = (Result64_1 >> 32);
            Result64_1 = (Result64_1 & 0xffffffffUL);
            Result64_1 = Result64_1 + Result64_2;
            Carry64 += (Result64_1 >> 32);
            Point_Result[SecondLoop] = (UINT32) (Result64_1 & 0xffffffffUL);
        } /* End of for */
        while (Carry64 != 0) {
          Result64_1 = ((UINT64) Point_Result[SecondLoop]) + Carry64;
          Carry64 = Result64_1 >> 32;
          Point_Result[SecondLoop] = (UINT32) (Result64_1 & 0xffffffffUL);
          SecondLoop++;
        } /* End of while */
        Point_Result++;
    } /* End of for */

    for (FirstLoop = 0;FirstLoop <= LoopCount;FirstLoop++) {
        (*pBI_Result)->pIntegerArray[FirstLoop] = (*pBI_Result)->pIntegerArray[FirstLoop + LoopCount];
    } /* End of for */
    if ((*pBI_Result)->pIntegerArray[LoopCount] != 0)
        (*pBI_Result)->ArrayLength = LoopCount + 1;
    else
        (*pBI_Result)->ArrayLength = LoopCount;

    (*pBI_Result)->IntegerLength = (*pBI_Result)->ArrayLength*4;
    BigInteger_ClearHighBits(*pBI_Result);

    if (BigInteger_UnsignedCompare(*pBI_Result, pBI_P) >= 0) {
        BigInteger_Sub(*pBI_Result, pBI_P, &pBI_U);
        BigInteger_Copy(pBI_U, pBI_Result);
    } /* End of if */
    BigInteger_ClearHighBits(*pBI_Result);
} /* End of BigInteger_Montgomery_Reduction */


VOID BigInteger_Montgomery_ExpMod (
    IN PBIG_INTEGER pBI_G,
    IN PBIG_INTEGER pBI_E,
    IN PBIG_INTEGER pBI_P,
    OUT PBIG_INTEGER *pBI_Result)
{
    UINT Bits_Of_P;
    UINT32 Index, Index2, AllocLength;
	UINT32 Sliding_Value , Sliding_HighValue, Sliding_LowValue;
    PBIG_INTEGER pBI_Temp1 = NULL, pBI_Temp2 = NULL;
    PBIG_INTEGER pBI_X = NULL, pBI_R = NULL, pBI_RR = NULL, pBI_1 = NULL;
    BIG_INTEGER *pBI_A[SLIDING_WINDOW];
    UINT8 *pRValue = NULL;

    AllocLength = (pBI_G->IntegerLength + pBI_E->IntegerLength + pBI_P->IntegerLength + 300);
    BigInteger_AllocSize(&pBI_Temp1, AllocLength);
    BigInteger_AllocSize(&pBI_Temp2, AllocLength);

    /* Calculate the bits of P and E, the highest bit is 1 */
    BigInteger_BitsOfBI(pBI_P, &Bits_Of_P);

    if ((pBI_E->IntegerLength == 1) && (pBI_E->pIntegerArray[0] == 1)) {
        BigInteger_Div(pBI_G, pBI_P, &pBI_Temp1, pBI_Result);
        goto memory_free;
    } /* End of if */

    if ((pBI_E->IntegerLength == 1) && (pBI_E->pIntegerArray[0] == 2)) {
        BigInteger_Mul(pBI_G, pBI_G, &pBI_Temp1);
        BigInteger_Div(pBI_Temp1, pBI_P, &pBI_Temp2, pBI_Result);
        goto memory_free;
    } /* End of if */

    /*
     * Main algorithm
     */
    BigInteger_Init(&pBI_R);
    BigInteger_Init(&pBI_RR);
    BigInteger_Bin2BI(Value_1, 1, &pBI_1);
    BigInteger_AllocSize(&pBI_X, AllocLength);
    BigInteger_AllocSize(&pBI_U, AllocLength); // for BigInteger_Montgomery_Reduction
    BigInteger_AllocSize(&pBI_S, AllocLength); // for BigInteger_Square
    BigInteger_AllocSize(&pBI_O, AllocLength); // for BigInteger_Square

    for (Index = 0; Index < SLIDING_WINDOW; Index++) {
        pBI_A[Index] = NULL;
		BigInteger_AllocSize(&pBI_A[Index], 193);
    } /* End of for */
    BigInteger_Bin2BI(WPS_DH_P_VALUE, 192, &pBI_Temp1);
    if (NdisCmpMemory(pBI_P->pIntegerArray, pBI_Temp1->pIntegerArray, pBI_P->IntegerLength) == 0) {
        BigInteger_Bin2BI(WPS_DH_X_VALUE, 184, &pBI_X);
        BigInteger_Bin2BI(WPS_DH_R_VALUE, 193, &pBI_R);
        BigInteger_Bin2BI(WPS_DH_RRModP_VALUE, 192, &pBI_RR);
        Bits_Of_R = 1537;
    } else {
        if ((Bits_Of_P % 8) == 0) {
            AllocLength = pBI_P->IntegerLength + 1;
        } else {
            AllocLength = pBI_P->IntegerLength;
        } /* End of if */
        pRValue = (UINT8 *) kmalloc(sizeof(UINT8)*AllocLength, GFP_ATOMIC);
	if (pRValue == NULL)
	{
		DBGPRINT(RT_DEBUG_ERROR, ("%s():Alloc memory failed\n", __FUNCTION__));
		goto memory_free;
	}
        NdisZeroMemory(pRValue, sizeof(UINT8)*AllocLength);
        pRValue[0] = (UINT8) (1 << (Bits_Of_P & 0x7));
        BigInteger_Bin2BI(pRValue, AllocLength , &pBI_R);

        BigInteger_Mul(pBI_R, pBI_R, &pBI_Temp1);
        BigInteger_Div(pBI_Temp1, pBI_P, &pBI_A[1], &pBI_RR);

        /* X = 1*R (mod P) */
        BigInteger_Div(pBI_R, pBI_P, &pBI_Temp2, &pBI_X);
    } /* End of if */

    /* A = G*R (mod P) => A = MonMod(G, R^2 mod P) */
    BigInteger_Mul(pBI_G, pBI_RR, &pBI_Temp1);
    BigInteger_Montgomery_Reduction(pBI_Temp1, pBI_P , pBI_R, &pBI_A[1]);
    for (Index = 2; Index < SLIDING_WINDOW; Index++) {
        BigInteger_Mul(pBI_A[Index - 1], pBI_A[1], &pBI_Temp1);
	    BigInteger_Montgomery_Reduction(pBI_Temp1, pBI_P, pBI_R, &pBI_A[Index]);
    } /* End of for */

    for (Index = pBI_E->IntegerLength ; Index > 0 ; Index--) {
        for (Index2 = 0; Index2 < 4 ; Index2++) {
            BigInteger_Square(pBI_X, &pBI_Temp1);
			BigInteger_Montgomery_Reduction(pBI_Temp1, pBI_P, pBI_R, &pBI_X);
	    } /* End of for */

		Sliding_Value = BigInteger_GetByteValue(pBI_E, Index);
		Sliding_HighValue = (Sliding_Value >> 4);
		if (Sliding_HighValue != 0) {
            BigInteger_Mul(pBI_A[Sliding_HighValue], pBI_X, &pBI_Temp1);
			BigInteger_Montgomery_Reduction(pBI_Temp1, pBI_P, pBI_R, &pBI_X);
		} /* End of if */

        for (Index2 = 0; Index2 < 4 ; Index2++) {
            BigInteger_Square(pBI_X, &pBI_Temp1);
			BigInteger_Montgomery_Reduction(pBI_Temp1, pBI_P, pBI_R, &pBI_X);
	    } /* End of for */

		Sliding_LowValue = Sliding_Value & 0x0f;
		if (Sliding_LowValue != 0) {
            BigInteger_Mul(pBI_A[Sliding_LowValue], pBI_X, &pBI_Temp1);
			BigInteger_Montgomery_Reduction(pBI_Temp1, pBI_P, pBI_R, &pBI_X);
		} /* End of if */
    } /* End of for */
    BigInteger_Montgomery_Reduction(pBI_X, pBI_P , pBI_R, pBI_Result);

    BigInteger_Free(&pBI_X);
    BigInteger_Free(&pBI_R);
    BigInteger_Free(&pBI_RR);
    BigInteger_Free(&pBI_1);
    BigInteger_Free(&pBI_U);
    BigInteger_Free(&pBI_S);
    BigInteger_Free(&pBI_O);
    for(Index = 0; Index < SLIDING_WINDOW; Index++)
			BigInteger_Free(&pBI_A[Index]);
    if (pRValue != NULL)
        kfree(pRValue);

memory_free:
    BigInteger_Free(&pBI_Temp1);
    BigInteger_Free(&pBI_Temp2);
} /* End of BigInteger_Montgomery_ExpMod */

/* End of crypt_biginteger.c */
