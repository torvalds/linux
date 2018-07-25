/*
 * Copyright (c) 2015 South Silicon Valley Microelectronics Inc.
 * Copyright (c) 2015 iComm Corporation
 *
 * This program is free software: you can redistribute it and/or modify 
 * it under the terms of the GNU General Public License as published by 
 * the Free Software Foundation, either version 3 of the License, or 
 * (at your option) any later version.
 * This program is distributed in the hope that it will be useful, but 
 * WITHOUT ANY WARRANTY; without even the implied warranty of 
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  
 * See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License 
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/kernel.h>
#include <linux/string.h>
#define SHA256_BLOCK_SIZE 64
#define SHA256_DIGEST_SIZE 32
typedef enum{
    FALSE = 0,
    TRUE = 1
}CsrBool;
#define BYTES_PER_WORD 4
#define BYTE_LEN 8
#define WORD_LEN (BYTE_LEN * BYTES_PER_WORD)
#define TEXT_LEN 128
#define MK_LEN (TEXT_LEN / WORD_LEN)
#define RK_LEN 32
#define TEXT_BYTES (TEXT_LEN / BYTE_LEN)
#define CK_INCREMENT 7
#define KEY_MULTIPLIER 0x80040100
#define TEXT_MULTIPLIER 0xa0202080
#define FK_PARAMETER_0 0xa3b1bac6
#define FK_PARAMETER_1 0x56aa3350
#define FK_PARAMETER_2 0x677d9197
#define FK_PARAMETER_3 0xb27022dc
static const u8 S_Box[] = {
       0xd6, 0x90, 0xe9, 0xfe, 0xcc, 0xe1, 0x3d, 0xb7, 0x16, 0xb6, 0x14, 0xc2, 0x28, 0xfb, 0x2c, 0x05,
       0x2b, 0x67, 0x9a, 0x76, 0x2a, 0xbe, 0x04, 0xc3, 0xaa, 0x44, 0x13, 0x26, 0x49, 0x86, 0x06, 0x99,
       0x9c, 0x42, 0x50, 0xf4, 0x91, 0xef, 0x98, 0x7a, 0x33, 0x54, 0x0b, 0x43, 0xed, 0xcf, 0xac, 0x62,
       0xe4, 0xb3, 0x1c, 0xa9, 0xc9, 0x08, 0xe8, 0x95, 0x80, 0xdf, 0x94, 0xfa, 0x75, 0x8f, 0x3f, 0xa6,
       0x47, 0x07, 0xa7, 0xfc, 0xf3, 0x73, 0x17, 0xba, 0x83, 0x59, 0x3c, 0x19, 0xe6, 0x85, 0x4f, 0xa8,
       0x68, 0x6b, 0x81, 0xb2, 0x71, 0x64, 0xda, 0x8b, 0xf8, 0xeb, 0x0f, 0x4b, 0x70, 0x56, 0x9d, 0x35,
       0x1e, 0x24, 0x0e, 0x5e, 0x63, 0x58, 0xd1, 0xa2, 0x25, 0x22, 0x7c, 0x3b, 0x01, 0x21, 0x78, 0x87,
       0xd4, 0x00, 0x46, 0x57, 0x9f, 0xd3, 0x27, 0x52, 0x4c, 0x36, 0x02, 0xe7, 0xa0, 0xc4, 0xc8, 0x9e,
       0xea, 0xbf, 0x8a, 0xd2, 0x40, 0xc7, 0x38, 0xb5, 0xa3, 0xf7, 0xf2, 0xce, 0xf9, 0x61, 0x15, 0xa1,
       0xe0, 0xae, 0x5d, 0xa4, 0x9b, 0x34, 0x1a, 0x55, 0xad, 0x93, 0x32, 0x30, 0xf5, 0x8c, 0xb1, 0xe3,
       0x1d, 0xf6, 0xe2, 0x2e, 0x82, 0x66, 0xca, 0x60, 0xc0, 0x29, 0x23, 0xab, 0x0d, 0x53, 0x4e, 0x6f,
       0xd5, 0xdb, 0x37, 0x45, 0xde, 0xfd, 0x8e, 0x2f, 0x03, 0xff, 0x6a, 0x72, 0x6d, 0x6c, 0x5b, 0x51,
       0x8d, 0x1b, 0xaf, 0x92, 0xbb, 0xdd, 0xbc, 0x7f, 0x11, 0xd9, 0x5c, 0x41, 0x1f, 0x10, 0x5a, 0xd8,
       0x0a, 0xc1, 0x31, 0x88, 0xa5, 0xcd, 0x7b, 0xbd, 0x2d, 0x74, 0xd0, 0x12, 0xb8, 0xe5, 0xb4, 0xb0,
       0x89, 0x69, 0x97, 0x4a, 0x0c, 0x96, 0x77, 0x7e, 0x65, 0xb9, 0xf1, 0x09, 0xc5, 0x6e, 0xc6, 0x84,
       0x18, 0xf0, 0x7d, 0xec, 0x3a, 0xdc, 0x4d, 0x20, 0x79, 0xee, 0x5f, 0x3e, 0xd7, 0xcb, 0x39, 0x48 };
static const u32 FK_Parameter[] = { FK_PARAMETER_0, FK_PARAMETER_1, FK_PARAMETER_2, FK_PARAMETER_3 };
static const u8 S_XState[] = {
          0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,
          1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,
          1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,
          0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,
          1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,
          0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,
          0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,
          1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,
          1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,
          0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,
          0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,
          1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,
          0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,
          1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,
          1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,
          0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0
};
static const u32 g_NextInputTable[RK_LEN] =
{
    0x00070e15, 0x1c232a31, 0x383f464d, 0x545b6269,
    0x70777e85, 0x8c939aa1, 0xa8afb6bd, 0xc4cbd2d9,
    0xe0e7eef5, 0xfc030a11, 0x181f262d, 0x343b4249,
    0x50575e65, 0x6c737a81, 0x888f969d, 0xa4abb2b9,
    0xc0c7ced5, 0xdce3eaf1, 0xf8ff060d, 0x141b2229,
    0x30373e45, 0x4c535a61, 0x686f767d, 0x848b9299,
    0xa0a7aeb5, 0xbcc3cad1, 0xd8dfe6ed, 0xf4fb0209,
    0x10171e25, 0x2c333a41, 0x484f565d, 0x646b7279
};
static const u32 CipherDataIdx[MK_LEN][MK_LEN] =
{
    {3, 2, 1, 0},
    {0, 3, 2, 1},
    {1, 0 ,3, 2},
    {2, 1, 0, 3}
};
#define PARITY_MACRO(Value) (S_XState[(Value) >> 24] ^ S_XState[((Value) >> 16) & 0xFF] ^ S_XState[((Value) >> 8) & 0xFF] ^ S_XState[(Value) & 0xFF])
#define XOR_MACRO(A,B) ((A) ^ (B))
#define L_TRANSFORM_MACRO(Word,Key) MultiplyCircular(Word, Key ? KEY_MULTIPLIER : TEXT_MULTIPLIER )
static u32 T_Transform(u32 Word)
{
    u32 j;
    u32 New_Word;
 int offset = 0;
    New_Word = 0;
    for (j = 0; j < MK_LEN; j++)
    {
        New_Word = (New_Word << BYTE_LEN);
  offset = ((u32)(Word >> (WORD_LEN - BYTE_LEN))) & ((u32)((1 << BYTE_LEN) - 1));
  New_Word = New_Word | (u32)S_Box[offset];
  Word = (Word << BYTE_LEN);
    }
    return (New_Word);
}
static u32 MultiplyCircular(u32 Word, u32 Basis)
{
    u32 New_Word;
    u32 i;
    New_Word = 0;
    for (i = 0; i < WORD_LEN; i++)
    {
  New_Word = (New_Word << 1) | PARITY_MACRO(Word & Basis);
        Basis = (Basis >> 1) | ((Basis & 1) << (WORD_LEN - 1));
    }
    return (New_Word);
}
static u32 Iterate(CsrBool Key, u32 Next_Input, u32 *Cipher_Text, u32 curIdx)
{
    u32 New_State;
    New_State = Next_Input;
    New_State = XOR_MACRO(New_State, Cipher_Text[CipherDataIdx[curIdx][0]]);
    New_State = XOR_MACRO(New_State, Cipher_Text[CipherDataIdx[curIdx][1]]);
    New_State = XOR_MACRO(New_State, Cipher_Text[CipherDataIdx[curIdx][2]]);
    New_State = L_TRANSFORM_MACRO(T_Transform(New_State), Key);
    New_State = XOR_MACRO(New_State, Cipher_Text[CipherDataIdx[curIdx][3]]);
    Cipher_Text[curIdx] = New_State;
    return (New_State);
}
static void CalculateEnKey(u8 *Key, u32 *Key_Store)
{
    u32 Cipher_Text[MK_LEN];
    u32 Next, i, j, Next_Input;
    for (j = 0; j < MK_LEN; j++)
    {
        Next = 0;
        for (i = 0; i < BYTES_PER_WORD; i++)
        {
      Next = (Next << BYTE_LEN);
      Next = Next | Key[(j <<2) + i];
        }
        Cipher_Text[j] = XOR_MACRO(Next, FK_Parameter[j]);
    }
    for (i = 0; i < RK_LEN; i++)
    {
        Next_Input = g_NextInputTable[i];
        Key_Store[i] = Iterate(TRUE, Next_Input, Cipher_Text, i & (MK_LEN - 1));
    }
}
static void SMS4_Run(u32 *Key_Store, u8 *PlainText, u8 *CipherText)
{
    u32 i, j;
    u32 Next;
    u32 Next_Input;
    u32 Plain_Text[MK_LEN];
    for (j = 0; j < MK_LEN; j++)
    {
        Next = 0;
        for (i = 0; i < BYTES_PER_WORD; i++)
        {
   Next = (Next << BYTE_LEN);
            Next = Next | PlainText[(j<<2) + i];
        }
        Plain_Text[j] = Next;
    }
    for (i = 0; i < RK_LEN; i++)
    {
        Next_Input = Key_Store[i];
        (void)Iterate(FALSE, Next_Input, Plain_Text, i & (MK_LEN - 1));
    }
    for (j = 0; j < MK_LEN; j++)
    {
        Next = Plain_Text[(MK_LEN - 1) - j];
        for (i = 0; i < BYTES_PER_WORD; i++)
        {
            CipherText[(j << 2) + i] = (u8)((Next >> (WORD_LEN - BYTE_LEN)) & ((1 << BYTE_LEN) - 1));
   Next = (Next << BYTE_LEN);
        }
    }
}
void WapiCryptoSms4(u8 *iv, u8 *key, u8 *input, u32 length, u8 *output)
{
    u32 i;
    u8 sms4Output[TEXT_BYTES];
 u8 tmp_data[TEXT_BYTES];
    u32 Key_Store[RK_LEN];
    u32 j = 0;
    u8 * p[2];
    p[0] = sms4Output;
    p[1] = tmp_data;
    memcpy(tmp_data, iv, TEXT_BYTES);
    CalculateEnKey(key, Key_Store);
    for (i = 0; i < length; i++)
    {
        if ((i & (TEXT_BYTES - 1)) == 0)
        {
            SMS4_Run(Key_Store, p[1-j], p[j]);
            j = 1 - j;
        }
        output[i] = input[i] ^ p[1-j][i & (TEXT_BYTES - 1)];
    }
}
void WapiCryptoSms4Mic(u8 *iv, u8 *key, u8 *header, u32 headerLength,
                      const u8 *input, u32 dataLength, u8 *mic)
{
    u32 i, j = 0, totalLength;
    u8 sms4Output[TEXT_BYTES], sms4Input[TEXT_BYTES];
 u32 tmp_headerLength = 0;
 u32 tmp_dataLength = 0;
 u32 header_cnt = 0 ;
 u32 header0_cnt = 0;
 u32 data_cnt = 0;
 u32 data0_cnt = 0;
    u32 Key_Store[RK_LEN];
    memcpy(sms4Input, iv, TEXT_BYTES);
    totalLength = headerLength + dataLength;
 tmp_headerLength = ((headerLength & (TEXT_BYTES-1)) == 0) ? 0 : (TEXT_BYTES - (headerLength & (TEXT_BYTES-1)));
 tmp_dataLength = ((dataLength & (TEXT_BYTES-1)) == 0) ? 0 : (TEXT_BYTES - (dataLength & (TEXT_BYTES-1)));
    totalLength += tmp_headerLength;
 totalLength += tmp_dataLength;
    CalculateEnKey(key, Key_Store);
    for (i = 0; i < totalLength; i++)
    {
        if ((i & (TEXT_BYTES-1)) == 0)
        {
            SMS4_Run(Key_Store, sms4Input, sms4Output);
        }
        if ((dataLength == 0) && (headerLength == 0))
        {
            sms4Input[i & (TEXT_BYTES-1)] = 0 ^ sms4Output[i & (TEXT_BYTES-1)];
   data0_cnt++;
        }
        else if ( (headerLength == 0) && (tmp_headerLength == 0) )
        {
   sms4Input[i & (TEXT_BYTES-1)] = input[j] ^ sms4Output[i & (TEXT_BYTES-1)];
   j++;
            dataLength--;
   data_cnt++;
        }
  else if( headerLength == 0 )
  {
   sms4Input[i & (TEXT_BYTES-1)] = 0 ^ sms4Output[i & (TEXT_BYTES-1)];
   tmp_headerLength--;
   header0_cnt++;
  }
        else
        {
            sms4Input[i & (TEXT_BYTES-1)] = header[i] ^ sms4Output[i & (TEXT_BYTES-1)];
            headerLength--;
   header_cnt++;
        }
    }
    SMS4_Run(Key_Store, sms4Input, mic);
}
