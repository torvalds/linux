/*
 *************************************************************************
 * Ralink Tech Inc.
 * 5F., No.36, Taiyuan St., Jhubei City,
 * Hsinchu County 302,
 * Taiwan, R.O.C.
 *
 * (c) Copyright 2002-2010, Ralink Technology, Inc.
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
 *************************************************************************/


#include "crypt_arc4.h"


/*
========================================================================
Routine Description:
    ARC4 initialize the key block

Arguments:
    pARC4_CTX        Pointer to ARC4 CONTEXT
    Key              Cipher key, it may be 16, 24, or 32 bytes (128, 192, or 256 bits)
    KeyLength        The length of cipher key in bytes

========================================================================
*/
VOID ARC4_INIT (
    IN ARC4_CTX_STRUC *pARC4_CTX,
    IN PUCHAR pKey,
	IN UINT KeyLength)
{
    UINT BlockIndex = 0, SWAPIndex = 0, KeyIndex = 0;
    UINT8 TempValue = 0;

    /*Initialize the block value*/
    pARC4_CTX->BlockIndex1 = 0;
    pARC4_CTX->BlockIndex2 = 0;
    for (BlockIndex = 0; BlockIndex < ARC4_KEY_BLOCK_SIZE; BlockIndex++)
        pARC4_CTX->KeyBlock[BlockIndex] = (UINT8) BlockIndex;

    /*Key schedule*/
    for (BlockIndex = 0; BlockIndex < ARC4_KEY_BLOCK_SIZE; BlockIndex++)
    {
        TempValue = pARC4_CTX->KeyBlock[BlockIndex];
        KeyIndex = BlockIndex % KeyLength;
        SWAPIndex = (SWAPIndex + TempValue + pKey[KeyIndex]) & 0xff;
        pARC4_CTX->KeyBlock[BlockIndex] = pARC4_CTX->KeyBlock[SWAPIndex];
        pARC4_CTX->KeyBlock[SWAPIndex] = TempValue;                
    } /* End of for */

} /* End of ARC4_INIT */


/*
========================================================================
Routine Description:
    ARC4 encryption/decryption

Arguments:
    pARC4_CTX       Pointer to ARC4 CONTEXT
    InputText       Input text
    InputTextLength The length of input text in bytes

Return Value:
    OutputBlock       Return output text
 ========================================================================
*/
VOID ARC4_Compute (
    IN ARC4_CTX_STRUC *pARC4_CTX,
    IN UINT8 InputBlock[],
    IN UINT InputBlockSize,
    OUT UINT8 OutputBlock[])
{
    UINT InputIndex = 0;
    UINT8 TempValue = 0;

    for (InputIndex = 0; InputIndex < InputBlockSize; InputIndex++)
    {
        pARC4_CTX->BlockIndex1 = (pARC4_CTX->BlockIndex1 + 1) & 0xff;
        TempValue = pARC4_CTX->KeyBlock[pARC4_CTX->BlockIndex1];
        pARC4_CTX->BlockIndex2 = (pARC4_CTX->BlockIndex2 + TempValue) & 0xff;
        
        pARC4_CTX->KeyBlock[pARC4_CTX->BlockIndex1] = pARC4_CTX->KeyBlock[pARC4_CTX->BlockIndex2];
        pARC4_CTX->KeyBlock[pARC4_CTX->BlockIndex2] = TempValue;
        
        TempValue = (TempValue + pARC4_CTX->KeyBlock[pARC4_CTX->BlockIndex1]) & 0xff;
        OutputBlock[InputIndex] = InputBlock[InputIndex]^pARC4_CTX->KeyBlock[TempValue];

    } /* End of for */
} /* End of ARC4_Compute */


/*
========================================================================
Routine Description:
    Discard the key length

Arguments:
    pARC4_CTX   Pointer to ARC4 CONTEXT
    Length      Discard the key length

========================================================================
*/
VOID ARC4_Discard_KeyLength (
    IN ARC4_CTX_STRUC *pARC4_CTX,
    IN UINT Length)    
{
    UINT Index = 0;
    UINT8 TempValue = 0;
    
    for (Index = 0; Index < Length; Index++)
    {
        pARC4_CTX->BlockIndex1 = (pARC4_CTX->BlockIndex1 + 1) & 0xff;
        TempValue = pARC4_CTX->KeyBlock[pARC4_CTX->BlockIndex1];
        pARC4_CTX->BlockIndex2 = (pARC4_CTX->BlockIndex2 + TempValue) & 0xff;
        
        pARC4_CTX->KeyBlock[pARC4_CTX->BlockIndex1] = pARC4_CTX->KeyBlock[pARC4_CTX->BlockIndex2];
        pARC4_CTX->KeyBlock[pARC4_CTX->BlockIndex2] = TempValue;
    } /* End of for */

} /* End of ARC4_Discard_KeyLength */


