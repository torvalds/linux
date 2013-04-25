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


#include "crypt_aes.h"


/* The value given by [x^(i-1),{00},{00},{00}], with x^(i-1) being powers of x in the field GF(2^8). */
static const UINT32 aes_rcon[] = {
	0x00000000, 0x01000000, 0x02000000, 0x04000000, 
    0x08000000, 0x10000000, 0x20000000, 0x40000000, 
    0x80000000, 0x1B000000, 0x36000000};

static const UINT8 aes_sbox_enc[] = {
  /*  0     1     2     3     4     5     6     7     8     9     a     b     c     d     e     f    */
    0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5, 0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7 ,0xab, 0x76, /* 0 */
    0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0, 0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4 ,0x72, 0xc0, /* 1 */
    0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f, 0xf7, 0xcc, 0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8 ,0x31, 0x15, /* 2 */
    0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a, 0x07, 0x12, 0x80, 0xe2, 0xeb, 0x27 ,0xb2, 0x75, /* 3 */
    0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0, 0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3 ,0x2f, 0x84, /* 4 */
    0x53, 0xd1, 0x00, 0xed, 0x20, 0xfc, 0xb1, 0x5b, 0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c ,0x58, 0xcf, /* 5 */
    0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85, 0x45, 0xf9, 0x02, 0x7f, 0x50, 0x3c ,0x9f, 0xa8, /* 6 */
    0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5, 0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff ,0xf3, 0xd2, /* 7 */
    0xcd, 0x0c, 0x13, 0xec, 0x5f, 0x97, 0x44, 0x17, 0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d ,0x19, 0x73, /* 8 */
    0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88, 0x46, 0xee, 0xb8, 0x14, 0xde, 0x5e ,0x0b, 0xdb, /* 9 */
    0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c, 0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95 ,0xe4, 0x79, /* a */
    0xe7, 0xc8, 0x37, 0x6d, 0x8d, 0xd5, 0x4e, 0xa9, 0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a ,0xae, 0x08, /* b */
    0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6, 0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd ,0x8b, 0x8a, /* c */
    0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e, 0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1 ,0x1d, 0x9e, /* d */
    0xe1, 0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e, 0x94, 0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55 ,0x28, 0xdf, /* e */
    0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68, 0x41, 0x99, 0x2d, 0x0f, 0xb0, 0x54 ,0xbb, 0x16, /* f */
};

static const UINT8 aes_sbox_dec[] = {
  /*  0     1     2     3     4     5     6     7     8     9     a     b     c     d     e     f    */
    0x52, 0x09, 0x6a, 0xd5, 0x30, 0x36, 0xa5, 0x38, 0xbf, 0x40, 0xa3, 0x9e, 0x81, 0xf3, 0xd7, 0xfb, /* 0 */
    0x7c, 0xe3, 0x39, 0x82, 0x9b, 0x2f, 0xff, 0x87, 0x34, 0x8e, 0x43, 0x44, 0xc4, 0xde, 0xe9, 0xcb, /* 1 */
    0x54, 0x7b, 0x94, 0x32, 0xa6, 0xc2, 0x23, 0x3d, 0xee, 0x4c, 0x95, 0x0b, 0x42, 0xfa, 0xc3, 0x4e, /* 2 */
    0x08, 0x2e, 0xa1, 0x66, 0x28, 0xd9, 0x24, 0xb2, 0x76, 0x5b, 0xa2, 0x49, 0x6d, 0x8b, 0xd1, 0x25, /* 3 */
    0x72, 0xf8, 0xf6, 0x64, 0x86, 0x68, 0x98, 0x16, 0xd4, 0xa4, 0x5c, 0xcc, 0x5d, 0x65, 0xb6, 0x92, /* 4 */
    0x6c, 0x70, 0x48, 0x50, 0xfd, 0xed, 0xb9, 0xda, 0x5e, 0x15, 0x46, 0x57, 0xa7, 0x8d, 0x9d, 0x84, /* 5 */
    0x90, 0xd8, 0xab, 0x00, 0x8c, 0xbc, 0xd3, 0x0a, 0xf7, 0xe4, 0x58, 0x05, 0xb8, 0xb3, 0x45, 0x06, /* 6 */
    0xd0, 0x2c, 0x1e, 0x8f, 0xca, 0x3f, 0x0f, 0x02, 0xc1, 0xaf, 0xbd, 0x03, 0x01, 0x13, 0x8a, 0x6b, /* 7 */
    0x3a, 0x91, 0x11, 0x41, 0x4f, 0x67, 0xdc, 0xea, 0x97, 0xf2, 0xcf, 0xce, 0xf0, 0xb4, 0xe6, 0x73, /* 8 */
    0x96, 0xac, 0x74, 0x22, 0xe7, 0xad, 0x35, 0x85, 0xe2, 0xf9, 0x37, 0xe8, 0x1c, 0x75, 0xdf, 0x6e, /* 9 */
    0x47, 0xf1, 0x1a, 0x71, 0x1d, 0x29, 0xc5, 0x89, 0x6f, 0xb7, 0x62, 0x0e, 0xaa, 0x18, 0xbe, 0x1b, /* a */
    0xfc, 0x56, 0x3e, 0x4b, 0xc6, 0xd2, 0x79, 0x20, 0x9a, 0xdb, 0xc0, 0xfe, 0x78, 0xcd, 0x5a, 0xf4, /* b */
    0x1f, 0xdd, 0xa8, 0x33, 0x88, 0x07, 0xc7, 0x31, 0xb1, 0x12, 0x10, 0x59, 0x27, 0x80, 0xec, 0x5f, /* c */
    0x60, 0x51, 0x7f, 0xa9, 0x19, 0xb5, 0x4a, 0x0d, 0x2d, 0xe5, 0x7a, 0x9f, 0x93, 0xc9, 0x9c, 0xef, /* d */
    0xa0, 0xe0, 0x3b, 0x4d, 0xae, 0x2a, 0xf5, 0xb0, 0xc8, 0xeb, 0xbb, 0x3c, 0x83, 0x53, 0x99, 0x61, /* e */
    0x17, 0x2b, 0x04, 0x7e, 0xba, 0x77, 0xd6, 0x26, 0xe1, 0x69, 0x14, 0x63, 0x55, 0x21, 0x0c, 0x7d, /* f */
};

/* ArrayIndex*{02} */
static const UINT8 aes_mul_2[] = {
  /*  0     1     2     3     4     5     6     7     8     9     a     b     c     d     e     f    */
    0x00, 0x02, 0x04, 0x06, 0x08, 0x0a, 0x0c, 0x0e, 0x10, 0x12, 0x14, 0x16, 0x18, 0x1a, 0x1c, 0x1e, /* 0 */
    0x20, 0x22, 0x24, 0x26, 0x28, 0x2a, 0x2c, 0x2e, 0x30, 0x32, 0x34, 0x36, 0x38, 0x3a, 0x3c, 0x3e, /* 1 */
    0x40, 0x42, 0x44, 0x46, 0x48, 0x4a, 0x4c, 0x4e, 0x50, 0x52, 0x54, 0x56, 0x58, 0x5a, 0x5c, 0x5e, /* 2 */
    0x60, 0x62, 0x64, 0x66, 0x68, 0x6a, 0x6c, 0x6e, 0x70, 0x72, 0x74, 0x76, 0x78, 0x7a, 0x7c, 0x7e, /* 3 */
    0x80, 0x82, 0x84, 0x86, 0x88, 0x8a, 0x8c, 0x8e, 0x90, 0x92, 0x94, 0x96, 0x98, 0x9a, 0x9c, 0x9e, /* 4 */
    0xa0, 0xa2, 0xa4, 0xa6, 0xa8, 0xaa, 0xac, 0xae, 0xb0, 0xb2, 0xb4, 0xb6, 0xb8, 0xba, 0xbc, 0xbe, /* 5 */
    0xc0, 0xc2, 0xc4, 0xc6, 0xc8, 0xca, 0xcc, 0xce, 0xd0, 0xd2, 0xd4, 0xd6, 0xd8, 0xda, 0xdc, 0xde, /* 6 */
    0xe0, 0xe2, 0xe4, 0xe6, 0xe8, 0xea, 0xec, 0xee, 0xf0, 0xf2, 0xf4, 0xf6, 0xf8, 0xfa, 0xfc, 0xfe, /* 7 */
    0x1b, 0x19, 0x1f, 0x1d, 0x13, 0x11, 0x17, 0x15, 0x0b, 0x09, 0x0f, 0x0d, 0x03, 0x01, 0x07, 0x05, /* 8 */
    0x3b, 0x39, 0x3f, 0x3d, 0x33, 0x31, 0x37, 0x35, 0x2b, 0x29, 0x2f, 0x2d, 0x23, 0x21, 0x27, 0x25, /* 9 */
    0x5b, 0x59, 0x5f, 0x5d, 0x53, 0x51, 0x57, 0x55, 0x4b, 0x49, 0x4f, 0x4d, 0x43, 0x41, 0x47, 0x45, /* a */
    0x7b, 0x79, 0x7f, 0x7d, 0x73, 0x71, 0x77, 0x75, 0x6b, 0x69, 0x6f, 0x6d, 0x63, 0x61, 0x67, 0x65, /* b */
    0x9b, 0x99, 0x9f, 0x9d, 0x93, 0x91, 0x97, 0x95, 0x8b, 0x89, 0x8f, 0x8d, 0x83, 0x81, 0x87, 0x85, /* c */
    0xbb, 0xb9, 0xbf, 0xbd, 0xb3, 0xb1, 0xb7, 0xb5, 0xab, 0xa9, 0xaf, 0xad, 0xa3, 0xa1, 0xa7, 0xa5, /* d */
    0xdb, 0xd9, 0xdf, 0xdd, 0xd3, 0xd1, 0xd7, 0xd5, 0xcb, 0xc9, 0xcf, 0xcd, 0xc3, 0xc1, 0xc7, 0xc5, /* e */
    0xfb, 0xf9, 0xff, 0xfd, 0xf3, 0xf1, 0xf7, 0xf5, 0xeb, 0xe9, 0xef, 0xed, 0xe3, 0xe1, 0xe7, 0xe5, /* f */
};

/* ArrayIndex*{03} */
static const UINT8 aes_mul_3[] = {
  /*  0     1     2     3     4     5     6     7     8     9     a     b     c     d     e     f    */
    0x00, 0x03, 0x06, 0x05, 0x0c, 0x0f, 0x0a, 0x09, 0x18, 0x1b, 0x1e, 0x1d, 0x14, 0x17, 0x12, 0x11, /* 0 */
    0x30, 0x33, 0x36, 0x35, 0x3c, 0x3f, 0x3a, 0x39, 0x28, 0x2b, 0x2e, 0x2d, 0x24, 0x27, 0x22, 0x21, /* 1 */
    0x60, 0x63, 0x66, 0x65, 0x6c, 0x6f, 0x6a, 0x69, 0x78, 0x7b, 0x7e, 0x7d, 0x74, 0x77, 0x72, 0x71, /* 2 */
    0x50, 0x53, 0x56, 0x55, 0x5c, 0x5f, 0x5a, 0x59, 0x48, 0x4b, 0x4e, 0x4d, 0x44, 0x47, 0x42, 0x41, /* 3 */
    0xc0, 0xc3, 0xc6, 0xc5, 0xcc, 0xcf, 0xca, 0xc9, 0xd8, 0xdb, 0xde, 0xdd, 0xd4, 0xd7, 0xd2, 0xd1, /* 4 */
    0xf0, 0xf3, 0xf6, 0xf5, 0xfc, 0xff, 0xfa, 0xf9, 0xe8, 0xeb, 0xee, 0xed, 0xe4, 0xe7, 0xe2, 0xe1, /* 5 */
    0xa0, 0xa3, 0xa6, 0xa5, 0xac, 0xaf, 0xaa, 0xa9, 0xb8, 0xbb, 0xbe, 0xbd, 0xb4, 0xb7, 0xb2, 0xb1, /* 6 */
    0x90, 0x93, 0x96, 0x95, 0x9c, 0x9f, 0x9a, 0x99, 0x88, 0x8b, 0x8e, 0x8d, 0x84, 0x87, 0x82, 0x81, /* 7 */
    0x9b, 0x98, 0x9d, 0x9e, 0x97, 0x94, 0x91, 0x92, 0x83, 0x80, 0x85, 0x86, 0x8f, 0x8c, 0x89, 0x8a, /* 8 */
    0xab, 0xa8, 0xad, 0xae, 0xa7, 0xa4, 0xa1, 0xa2, 0xb3, 0xb0, 0xb5, 0xb6, 0xbf, 0xbc, 0xb9, 0xba, /* 9 */
    0xfb, 0xf8, 0xfd, 0xfe, 0xf7, 0xf4, 0xf1, 0xf2, 0xe3, 0xe0, 0xe5, 0xe6, 0xef, 0xec, 0xe9, 0xea, /* a */
    0xcb, 0xc8, 0xcd, 0xce, 0xc7, 0xc4, 0xc1, 0xc2, 0xd3, 0xd0, 0xd5, 0xd6, 0xdf, 0xdc, 0xd9, 0xda, /* b */
    0x5b, 0x58, 0x5d, 0x5e, 0x57, 0x54, 0x51, 0x52, 0x43, 0x40, 0x45, 0x46, 0x4f, 0x4c, 0x49, 0x4a, /* c */
    0x6b, 0x68, 0x6d, 0x6e, 0x67, 0x64, 0x61, 0x62, 0x73, 0x70, 0x75, 0x76, 0x7f, 0x7c, 0x79, 0x7a, /* d */
    0x3b, 0x38, 0x3d, 0x3e, 0x37, 0x34, 0x31, 0x32, 0x23, 0x20, 0x25, 0x26, 0x2f, 0x2c, 0x29, 0x2a, /* e */
    0x0b, 0x08, 0x0d, 0x0e, 0x07, 0x04, 0x01, 0x02, 0x13, 0x10, 0x15, 0x16, 0x1f, 0x1c, 0x19, 0x1a, /* f */
};

/* ArrayIndex*{09} */
static const UINT8 aes_mul_9[] = {
  /*  0     1     2     3     4     5     6     7     8     9     a     b     c     d     e     f    */
    0x00, 0x09, 0x12, 0x1b, 0x24, 0x2d, 0x36, 0x3f, 0x48, 0x41, 0x5a, 0x53, 0x6c, 0x65, 0x7e, 0x77, /* 0 */
    0x90, 0x99, 0x82, 0x8b, 0xb4, 0xbd, 0xa6, 0xaf, 0xd8, 0xd1, 0xca, 0xc3, 0xfc, 0xf5, 0xee, 0xe7, /* 1 */
    0x3b, 0x32, 0x29, 0x20, 0x1f, 0x16, 0x0d, 0x04, 0x73, 0x7a, 0x61, 0x68, 0x57, 0x5e, 0x45, 0x4c, /* 2 */
    0xab, 0xa2, 0xb9, 0xb0, 0x8f, 0x86, 0x9d, 0x94, 0xe3, 0xea, 0xf1, 0xf8, 0xc7, 0xce, 0xd5, 0xdc, /* 3 */
    0x76, 0x7f, 0x64, 0x6d, 0x52, 0x5b, 0x40, 0x49, 0x3e, 0x37, 0x2c, 0x25, 0x1a, 0x13, 0x08, 0x01, /* 4 */
    0xe6, 0xef, 0xf4, 0xfd, 0xc2, 0xcb, 0xd0, 0xd9, 0xae, 0xa7, 0xbc, 0xb5, 0x8a, 0x83, 0x98, 0x91, /* 5 */
    0x4d, 0x44, 0x5f, 0x56, 0x69, 0x60, 0x7b, 0x72, 0x05, 0x0c, 0x17, 0x1e, 0x21, 0x28, 0x33, 0x3a, /* 6 */
    0xdd, 0xd4, 0xcf, 0xc6, 0xf9, 0xf0, 0xeb, 0xe2, 0x95, 0x9c, 0x87, 0x8e, 0xb1, 0xb8, 0xa3, 0xaa, /* 7 */
    0xec, 0xe5, 0xfe, 0xf7, 0xc8, 0xc1, 0xda, 0xd3, 0xa4, 0xad, 0xb6, 0xbf, 0x80, 0x89, 0x92, 0x9b, /* 8 */
    0x7c, 0x75, 0x6e, 0x67, 0x58, 0x51, 0x4a, 0x43, 0x34, 0x3d, 0x26, 0x2f, 0x10, 0x19, 0x02, 0x0b, /* 9 */
    0xd7, 0xde, 0xc5, 0xcc, 0xf3, 0xfa, 0xe1, 0xe8, 0x9f, 0x96, 0x8d, 0x84, 0xbb, 0xb2, 0xa9, 0xa0, /* a */
    0x47, 0x4e, 0x55, 0x5c, 0x63, 0x6a, 0x71, 0x78, 0x0f, 0x06, 0x1d, 0x14, 0x2b, 0x22, 0x39, 0x30, /* b */
    0x9a, 0x93, 0x88, 0x81, 0xbe, 0xb7, 0xac, 0xa5, 0xd2, 0xdb, 0xc0, 0xc9, 0xf6, 0xff, 0xe4, 0xed, /* c */
    0x0a, 0x03, 0x18, 0x11, 0x2e, 0x27, 0x3c, 0x35, 0x42, 0x4b, 0x50, 0x59, 0x66, 0x6f, 0x74, 0x7d, /* d */
    0xa1, 0xa8, 0xb3, 0xba, 0x85, 0x8c, 0x97, 0x9e, 0xe9, 0xe0, 0xfb, 0xf2, 0xcd, 0xc4, 0xdf, 0xd6, /* e */
    0x31, 0x38, 0x23, 0x2a, 0x15, 0x1c, 0x07, 0x0e, 0x79, 0x70, 0x6b, 0x62, 0x5d, 0x54, 0x4f, 0x46, /* f */
};

/* ArrayIndex*{0b} */
static const UINT8 aes_mul_b[] = {
  /*  0     1     2     3     4     5     6     7     8     9     a     b     c     d     e     f    */
    0x00, 0x0b, 0x16, 0x1d, 0x2c, 0x27, 0x3a, 0x31, 0x58, 0x53, 0x4e, 0x45, 0x74, 0x7f, 0x62, 0x69, /* 0 */
    0xb0, 0xbb, 0xa6, 0xad, 0x9c, 0x97, 0x8a, 0x81, 0xe8, 0xe3, 0xfe, 0xf5, 0xc4, 0xcf, 0xd2, 0xd9, /* 1 */
    0x7b, 0x70, 0x6d, 0x66, 0x57, 0x5c, 0x41, 0x4a, 0x23, 0x28, 0x35, 0x3e, 0x0f, 0x04, 0x19, 0x12, /* 2 */
    0xcb, 0xc0, 0xdd, 0xd6, 0xe7, 0xec, 0xf1, 0xfa, 0x93, 0x98, 0x85, 0x8e, 0xbf, 0xb4, 0xa9, 0xa2, /* 3 */
    0xf6, 0xfd, 0xe0, 0xeb, 0xda, 0xd1, 0xcc, 0xc7, 0xae, 0xa5, 0xb8, 0xb3, 0x82, 0x89, 0x94, 0x9f, /* 4 */
    0x46, 0x4d, 0x50, 0x5b, 0x6a, 0x61, 0x7c, 0x77, 0x1e, 0x15, 0x08, 0x03, 0x32, 0x39, 0x24, 0x2f, /* 5 */
    0x8d, 0x86, 0x9b, 0x90, 0xa1, 0xaa, 0xb7, 0xbc, 0xd5, 0xde, 0xc3, 0xc8, 0xf9, 0xf2, 0xef, 0xe4, /* 6 */
    0x3d, 0x36, 0x2b, 0x20, 0x11, 0x1a, 0x07, 0x0c, 0x65, 0x6e, 0x73, 0x78, 0x49, 0x42, 0x5f, 0x54, /* 7 */
    0xf7, 0xfc, 0xe1, 0xea, 0xdb, 0xd0, 0xcd, 0xc6, 0xaf, 0xa4, 0xb9, 0xb2, 0x83, 0x88, 0x95, 0x9e, /* 8 */
    0x47, 0x4c, 0x51, 0x5a, 0x6b, 0x60, 0x7d, 0x76, 0x1f, 0x14, 0x09, 0x02, 0x33, 0x38, 0x25, 0x2e, /* 9 */
    0x8c, 0x87, 0x9a, 0x91, 0xa0, 0xab, 0xb6, 0xbd, 0xd4, 0xdf, 0xc2, 0xc9, 0xf8, 0xf3, 0xee, 0xe5, /* a */
    0x3c, 0x37, 0x2a, 0x21, 0x10, 0x1b, 0x06, 0x0d, 0x64, 0x6f, 0x72, 0x79, 0x48, 0x43, 0x5e, 0x55, /* b */
    0x01, 0x0a, 0x17, 0x1c, 0x2d, 0x26, 0x3b, 0x30, 0x59, 0x52, 0x4f, 0x44, 0x75, 0x7e, 0x63, 0x68, /* c */
    0xb1, 0xba, 0xa7, 0xac, 0x9d, 0x96, 0x8b, 0x80, 0xe9, 0xe2, 0xff, 0xf4, 0xc5, 0xce, 0xd3, 0xd8, /* d */
    0x7a, 0x71, 0x6c, 0x67, 0x56, 0x5d, 0x40, 0x4b, 0x22, 0x29, 0x34, 0x3f, 0x0e, 0x05, 0x18, 0x13, /* e */
    0xca, 0xc1, 0xdc, 0xd7, 0xe6, 0xed, 0xf0, 0xfb, 0x92, 0x99, 0x84, 0x8f, 0xbe, 0xb5, 0xa8, 0xa3, /* f */
};

/* ArrayIndex*{0d} */
static const UINT8 aes_mul_d[] = {
  /*  0     1     2     3     4     5     6     7     8     9     a     b     c     d     e     f    */
    0x00, 0x0d, 0x1a, 0x17, 0x34, 0x39, 0x2e, 0x23, 0x68, 0x65, 0x72, 0x7f, 0x5c, 0x51, 0x46, 0x4b, /* 0 */
    0xd0, 0xdd, 0xca, 0xc7, 0xe4, 0xe9, 0xfe, 0xf3, 0xb8, 0xb5, 0xa2, 0xaf, 0x8c, 0x81, 0x96, 0x9b, /* 1 */
    0xbb, 0xb6, 0xa1, 0xac, 0x8f, 0x82, 0x95, 0x98, 0xd3, 0xde, 0xc9, 0xc4, 0xe7, 0xea, 0xfd, 0xf0, /* 2 */
    0x6b, 0x66, 0x71, 0x7c, 0x5f, 0x52, 0x45, 0x48, 0x03, 0x0e, 0x19, 0x14, 0x37, 0x3a, 0x2d, 0x20, /* 3 */
    0x6d, 0x60, 0x77, 0x7a, 0x59, 0x54, 0x43, 0x4e, 0x05, 0x08, 0x1f, 0x12, 0x31, 0x3c, 0x2b, 0x26, /* 4 */
    0xbd, 0xb0, 0xa7, 0xaa, 0x89, 0x84, 0x93, 0x9e, 0xd5, 0xd8, 0xcf, 0xc2, 0xe1, 0xec, 0xfb, 0xf6, /* 5 */
    0xd6, 0xdb, 0xcc, 0xc1, 0xe2, 0xef, 0xf8, 0xf5, 0xbe, 0xb3, 0xa4, 0xa9, 0x8a, 0x87, 0x90, 0x9d, /* 6 */
    0x06, 0x0b, 0x1c, 0x11, 0x32, 0x3f, 0x28, 0x25, 0x6e, 0x63, 0x74, 0x79, 0x5a, 0x57, 0x40, 0x4d, /* 7 */
    0xda, 0xd7, 0xc0, 0xcd, 0xee, 0xe3, 0xf4, 0xf9, 0xb2, 0xbf, 0xa8, 0xa5, 0x86, 0x8b, 0x9c, 0x91, /* 8 */
    0x0a, 0x07, 0x10, 0x1d, 0x3e, 0x33, 0x24, 0x29, 0x62, 0x6f, 0x78, 0x75, 0x56, 0x5b, 0x4c, 0x41, /* 9 */
    0x61, 0x6c, 0x7b, 0x76, 0x55, 0x58, 0x4f, 0x42, 0x09, 0x04, 0x13, 0x1e, 0x3d, 0x30, 0x27, 0x2a, /* a */
    0xb1, 0xbc, 0xab, 0xa6, 0x85, 0x88, 0x9f, 0x92, 0xd9, 0xd4, 0xc3, 0xce, 0xed, 0xe0, 0xf7, 0xfa, /* b */
    0xb7, 0xba, 0xad, 0xa0, 0x83, 0x8e, 0x99, 0x94, 0xdf, 0xd2, 0xc5, 0xc8, 0xeb, 0xe6, 0xf1, 0xfc, /* c */
    0x67, 0x6a, 0x7d, 0x70, 0x53, 0x5e, 0x49, 0x44, 0x0f, 0x02, 0x15, 0x18, 0x3b, 0x36, 0x21, 0x2c, /* d */
    0x0c, 0x01, 0x16, 0x1b, 0x38, 0x35, 0x22, 0x2f, 0x64, 0x69, 0x7e, 0x73, 0x50, 0x5d, 0x4a, 0x47, /* e */
    0xdc, 0xd1, 0xc6, 0xcb, 0xe8, 0xe5, 0xf2, 0xff, 0xb4, 0xb9, 0xae, 0xa3, 0x80, 0x8d, 0x9a, 0x97, /* f */
};

/* ArrayIndex*{0e} */
static const UINT8 aes_mul_e[] = {
  /*  0     1     2     3     4     5     6     7     8     9     a     b     c     d     e     f    */
    0x00, 0x0e, 0x1c, 0x12, 0x38, 0x36, 0x24, 0x2a, 0x70, 0x7e, 0x6c, 0x62, 0x48, 0x46, 0x54, 0x5a, /* 0 */
    0xe0, 0xee, 0xfc, 0xf2, 0xd8, 0xd6, 0xc4, 0xca, 0x90, 0x9e, 0x8c, 0x82, 0xa8, 0xa6, 0xb4, 0xba, /* 1 */
    0xdb, 0xd5, 0xc7, 0xc9, 0xe3, 0xed, 0xff, 0xf1, 0xab, 0xa5, 0xb7, 0xb9, 0x93, 0x9d, 0x8f, 0x81, /* 2 */
    0x3b, 0x35, 0x27, 0x29, 0x03, 0x0d, 0x1f, 0x11, 0x4b, 0x45, 0x57, 0x59, 0x73, 0x7d, 0x6f, 0x61, /* 3 */
    0xad, 0xa3, 0xb1, 0xbf, 0x95, 0x9b, 0x89, 0x87, 0xdd, 0xd3, 0xc1, 0xcf, 0xe5, 0xeb, 0xf9, 0xf7, /* 4 */
    0x4d, 0x43, 0x51, 0x5f, 0x75, 0x7b, 0x69, 0x67, 0x3d, 0x33, 0x21, 0x2f, 0x05, 0x0b, 0x19, 0x17, /* 5 */
    0x76, 0x78, 0x6a, 0x64, 0x4e, 0x40, 0x52, 0x5c, 0x06, 0x08, 0x1a, 0x14, 0x3e, 0x30, 0x22, 0x2c, /* 6 */
    0x96, 0x98, 0x8a, 0x84, 0xae, 0xa0, 0xb2, 0xbc, 0xe6, 0xe8, 0xfa, 0xf4, 0xde, 0xd0, 0xc2, 0xcc, /* 7 */
    0x41, 0x4f, 0x5d, 0x53, 0x79, 0x77, 0x65, 0x6b, 0x31, 0x3f, 0x2d, 0x23, 0x09, 0x07, 0x15, 0x1b, /* 8 */
    0xa1, 0xaf, 0xbd, 0xb3, 0x99, 0x97, 0x85, 0x8b, 0xd1, 0xdf, 0xcd, 0xc3, 0xe9, 0xe7, 0xf5, 0xfb, /* 9 */
    0x9a, 0x94, 0x86, 0x88, 0xa2, 0xac, 0xbe, 0xb0, 0xea, 0xe4, 0xf6, 0xf8, 0xd2, 0xdc, 0xce, 0xc0, /* a */
    0x7a, 0x74, 0x66, 0x68, 0x42, 0x4c, 0x5e, 0x50, 0x0a, 0x04, 0x16, 0x18, 0x32, 0x3c, 0x2e, 0x20, /* b */
    0xec, 0xe2, 0xf0, 0xfe, 0xd4, 0xda, 0xc8, 0xc6, 0x9c, 0x92, 0x80, 0x8e, 0xa4, 0xaa, 0xb8, 0xb6, /* c */
    0x0c, 0x02, 0x10, 0x1e, 0x34, 0x3a, 0x28, 0x26, 0x7c, 0x72, 0x60, 0x6e, 0x44, 0x4a, 0x58, 0x56, /* d */
    0x37, 0x39, 0x2b, 0x25, 0x0f, 0x01, 0x13, 0x1d, 0x47, 0x49, 0x5b, 0x55, 0x7f, 0x71, 0x63, 0x6d, /* e */
    0xd7, 0xd9, 0xcb, 0xc5, 0xef, 0xe1, 0xf3, 0xfd, 0xa7, 0xa9, 0xbb, 0xb5, 0x9f, 0x91, 0x83, 0x8d, /* f */
};

/* For AES_CMAC */
#define AES_MAC_LENGTH 16 /* 128-bit string */
static UINT8 Const_Zero[16] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static UINT8 Const_Rb[16] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x87};
   
/*
========================================================================
Routine Description:
    AES key expansion (key schedule)

Arguments:
    Key              Cipher key, it may be 16, 24, or 32 bytes (128, 192, or 256 bits)
    KeyLength        The length of cipher key in bytes
    paes_ctx         Pointer to AES_CTX_STRUC

Return Value:
    paes_ctx         Retrun the KeyWordExpansion of AES_CTX_STRUC

Note:
    Pseudo code for key expansion
    ------------------------------------------
       Nk = (key length/4);
       
       while (i < Nk)
           KeyWordExpansion[i] = word(key[4*i], key[4*i + 1], key[4*i + 2], key[4*i + 3]);
           i++;
       end while

       while (i < ((key length/4 + 6 + 1)*4) )
           temp = KeyWordExpansion[i - 1];
           if (i % Nk ==0)
               temp = SubWord(RotWord(temp)) ^ Rcon[i/Nk];
           else if ((Nk > 6) && (i % 4 == 4))
               temp = SubWord(temp);
           end if

           KeyWordExpansion[i] = KeyWordExpansion[i - Nk]^ temp;
           i++;
       end while
========================================================================
*/
VOID RT_AES_KeyExpansion (
    IN UINT8 Key[],
    IN UINT KeyLength,
    INOUT AES_CTX_STRUC *paes_ctx)
{
    UINT KeyIndex = 0;
    UINT NumberOfWordOfKey, NumberOfWordOfKeyExpansion;
    UINT8  TempWord[AES_KEY_ROWS], Temp;
    UINT32 Temprcon;

    NumberOfWordOfKey = KeyLength >> 2;
    while (KeyIndex < NumberOfWordOfKey)
    {
        paes_ctx->KeyWordExpansion[0][KeyIndex] = Key[4*KeyIndex];
        paes_ctx->KeyWordExpansion[1][KeyIndex] = Key[4*KeyIndex + 1];
        paes_ctx->KeyWordExpansion[2][KeyIndex] = Key[4*KeyIndex + 2];
        paes_ctx->KeyWordExpansion[3][KeyIndex] = Key[4*KeyIndex + 3];
        KeyIndex++;
    } /* End of while */

    NumberOfWordOfKeyExpansion = ((UINT) AES_KEY_ROWS) * ((KeyLength >> 2) + 6 + 1);    
    while (KeyIndex < NumberOfWordOfKeyExpansion)
    {
        TempWord[0] = paes_ctx->KeyWordExpansion[0][KeyIndex - 1];
        TempWord[1] = paes_ctx->KeyWordExpansion[1][KeyIndex - 1];
        TempWord[2] = paes_ctx->KeyWordExpansion[2][KeyIndex - 1];
        TempWord[3] = paes_ctx->KeyWordExpansion[3][KeyIndex - 1];
        if ((KeyIndex % NumberOfWordOfKey) == 0) {
            Temprcon = aes_rcon[KeyIndex/NumberOfWordOfKey];
            Temp = aes_sbox_enc[TempWord[1]]^((Temprcon >> 24) & 0xff);
            TempWord[1] = aes_sbox_enc[TempWord[2]]^((Temprcon >> 16) & 0xff);
            TempWord[2] = aes_sbox_enc[TempWord[3]]^((Temprcon >>  8) & 0xff);
            TempWord[3] = aes_sbox_enc[TempWord[0]]^((Temprcon      ) & 0xff);
            TempWord[0] = Temp;
        } else if ((NumberOfWordOfKey > 6) && ((KeyIndex % NumberOfWordOfKey) == 4)) {
            Temp = aes_sbox_enc[TempWord[0]];
            TempWord[1] = aes_sbox_enc[TempWord[1]];
            TempWord[2] = aes_sbox_enc[TempWord[2]];
            TempWord[3] = aes_sbox_enc[TempWord[3]];
            TempWord[0] = Temp;
        }
        paes_ctx->KeyWordExpansion[0][KeyIndex] = paes_ctx->KeyWordExpansion[0][KeyIndex - NumberOfWordOfKey]^TempWord[0];
        paes_ctx->KeyWordExpansion[1][KeyIndex] = paes_ctx->KeyWordExpansion[1][KeyIndex - NumberOfWordOfKey]^TempWord[1];
        paes_ctx->KeyWordExpansion[2][KeyIndex] = paes_ctx->KeyWordExpansion[2][KeyIndex - NumberOfWordOfKey]^TempWord[2];
        paes_ctx->KeyWordExpansion[3][KeyIndex] = paes_ctx->KeyWordExpansion[3][KeyIndex - NumberOfWordOfKey]^TempWord[3];
        KeyIndex++;
    } /* End of while */
} /* End of RT_AES_KeyExpansion */


/*
========================================================================
Routine Description:
    AES encryption

Arguments:
    PlainBlock       The block of plain text, 16 bytes(128 bits) each block
    PlainBlockSize   The length of block of plain text in bytes
    Key              Cipher key, it may be 16, 24, or 32 bytes (128, 192, or 256 bits)
    KeyLength        The length of cipher key in bytes
    CipherBlockSize  The length of allocated cipher block in bytes

Return Value:
    CipherBlock      Return cipher text
    CipherBlockSize  Return the length of real used cipher block in bytes

Note:
    Reference to FIPS-PUB 197
    1. Check if block size is 16 bytes(128 bits) and if key length is 16, 24, or 32 bytes(128, 192, or 256 bits)
    2. Transfer the plain block to state block 
    3. Main encryption rounds
    4. Transfer the state block to cipher block
    ------------------------------------------
       NumberOfRound = (key length / 4) + 6;
       state block = plain block;
       
       AddRoundKey(state block, key);
       for round = 1 to NumberOfRound
           SubBytes(state block)
           ShiftRows(state block)
           MixColumns(state block)
           AddRoundKey(state block, key);
       end for

       SubBytes(state block)
       ShiftRows(state block)
       AddRoundKey(state block, key);

       cipher block = state block;
========================================================================
*/
VOID RT_AES_Encrypt (
    IN UINT8 PlainBlock[],
    IN UINT PlainBlockSize,
    IN UINT8 Key[],
    IN UINT KeyLength,
    OUT UINT8 CipherBlock[],
    INOUT UINT *CipherBlockSize)
{
/*    AES_CTX_STRUC aes_ctx;
*/
	AES_CTX_STRUC *paes_ctx = NULL;
    UINT RowIndex, ColumnIndex;
    UINT RoundIndex, NumberOfRound = 0;
    UINT8 Temp, Row0, Row1, Row2, Row3;

    /*   
     * 1. Check if block size is 16 bytes(128 bits) and if key length is 16, 24, or 32 bytes(128, 192, or 256 bits) 
     */
    if (PlainBlockSize != AES_BLOCK_SIZES) {
    	DBGPRINT(RT_DEBUG_ERROR, ("RT_AES_Encrypt: plain block size is %d bytes, it must be %d bytes(128 bits).\n", 
            PlainBlockSize, AES_BLOCK_SIZES));
        return;
    } /* End of if */
    if ((KeyLength != AES_KEY128_LENGTH) && (KeyLength != AES_KEY192_LENGTH) && (KeyLength != AES_KEY256_LENGTH)) {
    	DBGPRINT(RT_DEBUG_ERROR, ("RT_AES_Encrypt: key length is %d bytes, it must be %d, %d, or %d bytes(128, 192, or 256 bits).\n", 
            KeyLength, AES_KEY128_LENGTH, AES_KEY192_LENGTH, AES_KEY256_LENGTH));
        return;
    } /* End of if */
    if (*CipherBlockSize < AES_BLOCK_SIZES) {
    	DBGPRINT(RT_DEBUG_ERROR, ("RT_AES_Encrypt: cipher block size is %d bytes, it must be %d bytes(128 bits).\n", 
            *CipherBlockSize, AES_BLOCK_SIZES));
        return;
    } /* End of if */

	/* allocate memory */
	os_alloc_mem(NULL, (UCHAR **)&paes_ctx, sizeof(AES_CTX_STRUC));
	if (paes_ctx == NULL)
	{
		DBGPRINT(RT_DEBUG_ERROR, ("%s: Allocate memory fail!!!\n", __FUNCTION__));
		return;
	}

    /* 
     * 2. Transfer the plain block to state block 
     */
    for (RowIndex = 0; RowIndex < AES_STATE_ROWS;RowIndex++)
        for (ColumnIndex = 0; ColumnIndex < AES_STATE_COLUMNS;ColumnIndex++)
            paes_ctx->State[RowIndex][ColumnIndex] = PlainBlock[RowIndex + 4*ColumnIndex];

    /* 
     *  3. Main encryption rounds
     */
    RT_AES_KeyExpansion(Key, KeyLength, paes_ctx);
    NumberOfRound = (KeyLength >> 2) + 6;

    /* AES_AddRoundKey */
    RoundIndex = 0;
    for (RowIndex = 0; RowIndex < AES_STATE_ROWS;RowIndex++)
        for (ColumnIndex = 0; ColumnIndex < AES_STATE_COLUMNS;ColumnIndex++)
            paes_ctx->State[RowIndex][ColumnIndex] ^= paes_ctx->KeyWordExpansion[RowIndex][(RoundIndex*((UINT) AES_STATE_COLUMNS)) + ColumnIndex];

    for (RoundIndex = 1; RoundIndex < NumberOfRound;RoundIndex++)
    {
        /* AES_SubBytes */
        for (RowIndex = 0; RowIndex < AES_STATE_ROWS;RowIndex++)
            for (ColumnIndex = 0; ColumnIndex < AES_STATE_COLUMNS;ColumnIndex++)
                paes_ctx->State[RowIndex][ColumnIndex] = aes_sbox_enc[paes_ctx->State[RowIndex][ColumnIndex]];

        /* AES_ShiftRows */
        Temp = paes_ctx->State[1][0];
        paes_ctx->State[1][0] = paes_ctx->State[1][1];
        paes_ctx->State[1][1] = paes_ctx->State[1][2];
        paes_ctx->State[1][2] = paes_ctx->State[1][3];
        paes_ctx->State[1][3] = Temp;
        Temp = paes_ctx->State[2][0];
        paes_ctx->State[2][0] = paes_ctx->State[2][2];
        paes_ctx->State[2][2] = Temp;
        Temp = paes_ctx->State[2][1];
        paes_ctx->State[2][1] = paes_ctx->State[2][3];
        paes_ctx->State[2][3] = Temp;
        Temp = paes_ctx->State[3][3];
        paes_ctx->State[3][3] = paes_ctx->State[3][2];
        paes_ctx->State[3][2] = paes_ctx->State[3][1];
        paes_ctx->State[3][1] = paes_ctx->State[3][0];
        paes_ctx->State[3][0] = Temp;

        /* AES_MixColumns */
        for (ColumnIndex = 0; ColumnIndex < AES_STATE_COLUMNS;ColumnIndex++)
        {
            Row0 = paes_ctx->State[0][ColumnIndex];
            Row1 = paes_ctx->State[1][ColumnIndex];
            Row2 = paes_ctx->State[2][ColumnIndex];
            Row3 = paes_ctx->State[3][ColumnIndex];
            paes_ctx->State[0][ColumnIndex] = aes_mul_2[Row0]^aes_mul_3[Row1]^Row2^Row3;
            paes_ctx->State[1][ColumnIndex] = Row0^aes_mul_2[Row1]^aes_mul_3[Row2]^Row3;
            paes_ctx->State[2][ColumnIndex] = Row0^Row1^aes_mul_2[Row2]^aes_mul_3[Row3];
            paes_ctx->State[3][ColumnIndex] = aes_mul_3[Row0]^Row1^Row2^aes_mul_2[Row3];
        }

        /* AES_AddRoundKey */
        for (RowIndex = 0; RowIndex < AES_STATE_ROWS;RowIndex++)
            for (ColumnIndex = 0; ColumnIndex < AES_STATE_COLUMNS;ColumnIndex++)
                paes_ctx->State[RowIndex][ColumnIndex] ^= paes_ctx->KeyWordExpansion[RowIndex][(RoundIndex*((UINT) AES_STATE_COLUMNS)) + ColumnIndex];
    } /* End of for */

    /* AES_SubBytes */
    for (RowIndex = 0; RowIndex < AES_STATE_ROWS;RowIndex++)
        for (ColumnIndex = 0; ColumnIndex < AES_STATE_COLUMNS;ColumnIndex++)
            paes_ctx->State[RowIndex][ColumnIndex] = aes_sbox_enc[paes_ctx->State[RowIndex][ColumnIndex]];
    /* AES_ShiftRows */
    Temp = paes_ctx->State[1][0];
    paes_ctx->State[1][0] = paes_ctx->State[1][1];
    paes_ctx->State[1][1] = paes_ctx->State[1][2];
    paes_ctx->State[1][2] = paes_ctx->State[1][3];
    paes_ctx->State[1][3] = Temp;
    Temp = paes_ctx->State[2][0];
    paes_ctx->State[2][0] = paes_ctx->State[2][2];
    paes_ctx->State[2][2] = Temp;
    Temp = paes_ctx->State[2][1];
    paes_ctx->State[2][1] = paes_ctx->State[2][3];
    paes_ctx->State[2][3] = Temp;
    Temp = paes_ctx->State[3][3];
    paes_ctx->State[3][3] = paes_ctx->State[3][2];
    paes_ctx->State[3][2] = paes_ctx->State[3][1];
    paes_ctx->State[3][1] = paes_ctx->State[3][0];
    paes_ctx->State[3][0] = Temp;
    /* AES_AddRoundKey */
    for (RowIndex = 0; RowIndex < AES_STATE_ROWS;RowIndex++)
        for (ColumnIndex = 0; ColumnIndex < AES_STATE_COLUMNS;ColumnIndex++)
            paes_ctx->State[RowIndex][ColumnIndex] ^= paes_ctx->KeyWordExpansion[RowIndex][(RoundIndex*((UINT) AES_STATE_COLUMNS)) + ColumnIndex];

    /* 
     * 4. Transfer the state block to cipher block 
     */
    for (RowIndex = 0; RowIndex < AES_STATE_ROWS;RowIndex++)
        for (ColumnIndex = 0; ColumnIndex < AES_STATE_COLUMNS;ColumnIndex++)
            CipherBlock[RowIndex + 4*ColumnIndex] = paes_ctx->State[RowIndex][ColumnIndex];

    *CipherBlockSize = ((UINT) AES_STATE_ROWS)*((UINT) AES_STATE_COLUMNS);

	if (paes_ctx != NULL)
		os_free_mem(NULL, paes_ctx);
} /* End of RT_AES_Encrypt */


/*
========================================================================
Routine Description:
    AES decryption

Arguments:
    CipherBlock      The block of cipher text, 16 bytes(128 bits) each block
    CipherBlockSize  The length of block of cipher text in bytes
    Key              Cipher key, it may be 16, 24, or 32 bytes (128, 192, or 256 bits)
    KeyLength        The length of cipher key in bytes
    PlainBlockSize   The length of allocated plain block in bytes

Return Value:
    PlainBlock       Return plain text
    PlainBlockSize  Return the length of real used plain block in bytes

Note:
    Reference to FIPS-PUB 197
    1. Check if block size is 16 bytes(128 bits) and if key length is 16, 24, or 32 bytes(128, 192, or 256 bits)
    2. Transfer the cipher block to state block 
    3. Main decryption rounds
    4. Transfer the state block to plain block
    ------------------------------------------
       NumberOfRound = (key length / 4) + 6;
       state block = cipher block;
       
       AddRoundKey(state block, key);
       for round = NumberOfRound to 1
           InvSubBytes(state block)
           InvShiftRows(state block)
           InvMixColumns(state block)
           AddRoundKey(state block, key);
       end for

       InvSubBytes(state block)
       InvShiftRows(state block)
       AddRoundKey(state block, key);

       plain block = state block;
========================================================================
*/
VOID RT_AES_Decrypt (
    IN UINT8 CipherBlock[],
    IN UINT CipherBlockSize,
    IN UINT8 Key[],
    IN UINT KeyLength,
    OUT UINT8 PlainBlock[],
    INOUT UINT *PlainBlockSize)
{
/*    AES_CTX_STRUC aes_ctx;
*/
	AES_CTX_STRUC *paes_ctx = NULL;
    UINT RowIndex, ColumnIndex;
    UINT RoundIndex, NumberOfRound = 0;
    UINT8 Temp, Row0, Row1, Row2, Row3;

    /*   
     * 1. Check if block size is 16 bytes(128 bits) and if key length is 16, 24, or 32 bytes(128, 192, or 256 bits) 
     */
    if (*PlainBlockSize < AES_BLOCK_SIZES) {
    	DBGPRINT(RT_DEBUG_ERROR, ("RT_AES_Decrypt: plain block size is %d bytes, it must be %d bytes(128 bits).\n", 
            *PlainBlockSize, AES_BLOCK_SIZES));
        return;
    } /* End of if */
    if ((KeyLength != AES_KEY128_LENGTH) && (KeyLength != AES_KEY192_LENGTH) && (KeyLength != AES_KEY256_LENGTH)) {
    	DBGPRINT(RT_DEBUG_ERROR, ("RT_AES_Decrypt: key length is %d bytes, it must be %d, %d, or %d bytes(128, 192, or 256 bits).\n", 
            KeyLength, AES_KEY128_LENGTH, AES_KEY192_LENGTH, AES_KEY256_LENGTH));
        return;
    } /* End of if */
    if (CipherBlockSize != AES_BLOCK_SIZES) {
    	DBGPRINT(RT_DEBUG_ERROR, ("RT_AES_Decrypt: cipher block size is %d bytes, it must be %d bytes(128 bits).\n", 
            CipherBlockSize, AES_BLOCK_SIZES));
        return;
    } /* End of if */

	/* allocate memory */
	os_alloc_mem(NULL, (UCHAR **)&paes_ctx, sizeof(AES_CTX_STRUC));
	if (paes_ctx == NULL)
	{
		DBGPRINT(RT_DEBUG_ERROR, ("%s: Allocate memory fail!!!\n", __FUNCTION__));
		return;
	}

    /* 
     * 2. Transfer the cipher block to state block 
     */
    for (RowIndex = 0; RowIndex < AES_STATE_ROWS;RowIndex++)
        for (ColumnIndex = 0; ColumnIndex < AES_STATE_COLUMNS;ColumnIndex++)
            paes_ctx->State[RowIndex][ColumnIndex] = CipherBlock[RowIndex + 4*ColumnIndex];

    /* 
     *  3. Main decryption rounds
     */
    RT_AES_KeyExpansion(Key, KeyLength, paes_ctx);
    NumberOfRound = (KeyLength >> 2) + 6;

    /* AES_AddRoundKey */
    RoundIndex = NumberOfRound;
    for (RowIndex = 0; RowIndex < AES_STATE_ROWS;RowIndex++)
        for (ColumnIndex = 0; ColumnIndex < AES_STATE_COLUMNS;ColumnIndex++)
            paes_ctx->State[RowIndex][ColumnIndex] ^= paes_ctx->KeyWordExpansion[RowIndex][(RoundIndex*((UINT) AES_STATE_COLUMNS)) + ColumnIndex];

    for (RoundIndex = (NumberOfRound - 1); RoundIndex > 0 ;RoundIndex--)
    {
        /* AES_InvShiftRows */
        Temp = paes_ctx->State[1][3];
        paes_ctx->State[1][3] = paes_ctx->State[1][2];
        paes_ctx->State[1][2] = paes_ctx->State[1][1];
        paes_ctx->State[1][1] = paes_ctx->State[1][0];
        paes_ctx->State[1][0] = Temp;
        Temp = paes_ctx->State[2][0];
        paes_ctx->State[2][0] = paes_ctx->State[2][2];
        paes_ctx->State[2][2] = Temp;
        Temp = paes_ctx->State[2][1];
        paes_ctx->State[2][1] = paes_ctx->State[2][3];
        paes_ctx->State[2][3] = Temp;
        Temp = paes_ctx->State[3][0];
        paes_ctx->State[3][0] = paes_ctx->State[3][1];
        paes_ctx->State[3][1] = paes_ctx->State[3][2];
        paes_ctx->State[3][2] = paes_ctx->State[3][3];
        paes_ctx->State[3][3] = Temp;

        /* AES_InvSubBytes */
        for (RowIndex = 0; RowIndex < AES_STATE_ROWS;RowIndex++)
            for (ColumnIndex = 0; ColumnIndex < AES_STATE_COLUMNS;ColumnIndex++)
                paes_ctx->State[RowIndex][ColumnIndex] = aes_sbox_dec[paes_ctx->State[RowIndex][ColumnIndex]];

        /* AES_AddRoundKey */
        for (RowIndex = 0; RowIndex < AES_STATE_ROWS;RowIndex++)
            for (ColumnIndex = 0; ColumnIndex < AES_STATE_COLUMNS;ColumnIndex++)
                paes_ctx->State[RowIndex][ColumnIndex] ^= paes_ctx->KeyWordExpansion[RowIndex][(RoundIndex*((UINT) AES_STATE_COLUMNS)) + ColumnIndex];

        /* AES_InvMixColumns */
        for (ColumnIndex = 0; ColumnIndex < AES_STATE_COLUMNS;ColumnIndex++)
        {
            Row0 = paes_ctx->State[0][ColumnIndex];
            Row1 = paes_ctx->State[1][ColumnIndex];
            Row2 = paes_ctx->State[2][ColumnIndex];
            Row3 = paes_ctx->State[3][ColumnIndex];
            paes_ctx->State[0][ColumnIndex] = aes_mul_e[Row0]^aes_mul_b[Row1]^aes_mul_d[Row2]^aes_mul_9[Row3];
            paes_ctx->State[1][ColumnIndex] = aes_mul_9[Row0]^aes_mul_e[Row1]^aes_mul_b[Row2]^aes_mul_d[Row3];
            paes_ctx->State[2][ColumnIndex] = aes_mul_d[Row0]^aes_mul_9[Row1]^aes_mul_e[Row2]^aes_mul_b[Row3];
            paes_ctx->State[3][ColumnIndex] = aes_mul_b[Row0]^aes_mul_d[Row1]^aes_mul_9[Row2]^aes_mul_e[Row3];
        }
    } /* End of for */

    /* AES_InvShiftRows */
    Temp = paes_ctx->State[1][3];
    paes_ctx->State[1][3] = paes_ctx->State[1][2];
    paes_ctx->State[1][2] = paes_ctx->State[1][1];
    paes_ctx->State[1][1] = paes_ctx->State[1][0];
    paes_ctx->State[1][0] = Temp;
    Temp = paes_ctx->State[2][0];
    paes_ctx->State[2][0] = paes_ctx->State[2][2];
    paes_ctx->State[2][2] = Temp;
    Temp = paes_ctx->State[2][1];
    paes_ctx->State[2][1] = paes_ctx->State[2][3];
    paes_ctx->State[2][3] = Temp;
    Temp = paes_ctx->State[3][0];
    paes_ctx->State[3][0] = paes_ctx->State[3][1];
    paes_ctx->State[3][1] = paes_ctx->State[3][2];
    paes_ctx->State[3][2] = paes_ctx->State[3][3];
    paes_ctx->State[3][3] = Temp;
    /* AES_InvSubBytes */
    for (RowIndex = 0; RowIndex < AES_STATE_ROWS;RowIndex++)
        for (ColumnIndex = 0; ColumnIndex < AES_STATE_COLUMNS;ColumnIndex++)
            paes_ctx->State[RowIndex][ColumnIndex] = aes_sbox_dec[paes_ctx->State[RowIndex][ColumnIndex]];
    /* AES_AddRoundKey */
    for (RowIndex = 0; RowIndex < AES_STATE_ROWS;RowIndex++)
        for (ColumnIndex = 0; ColumnIndex < AES_STATE_COLUMNS;ColumnIndex++)
            paes_ctx->State[RowIndex][ColumnIndex] ^= paes_ctx->KeyWordExpansion[RowIndex][(RoundIndex*((UINT) AES_STATE_COLUMNS)) + ColumnIndex];

    /* 
     * 4. Transfer the state block to plain block 
     */
    for (RowIndex = 0; RowIndex < AES_STATE_ROWS;RowIndex++)
        for (ColumnIndex = 0; ColumnIndex < AES_STATE_COLUMNS;ColumnIndex++)
            PlainBlock[RowIndex + 4*ColumnIndex] = paes_ctx->State[RowIndex][ColumnIndex];

    *PlainBlockSize = ((UINT) AES_STATE_ROWS)*((UINT) AES_STATE_COLUMNS);

	if (paes_ctx != NULL)
		os_free_mem(NULL, paes_ctx);
} /* End of RT_AES_Decrypt */



/*
========================================================================
Routine Description:
    AES-CBCMAC 

Arguments:
    Payload        Data
    PayloadLength  The length of data in bytes
    Key              Cipher key
    KeyLength        The length of cipher key in bytes depend on block cipher (16, 24, or 32 bytes)
    Nonce            Nonce
    NonceLength      The length of nonce in bytes
    AAD              Additional authenticated data
    AADLength        The length of AAD in bytes
    MACLength        The length of MAC in bytes

Return Value:
    MACText       The mac

Note:
    Reference to RFC 3601, and NIST 800-38C.
========================================================================
*/
VOID AES_CCM_MAC (
    IN UINT8 Payload[],
    IN UINT  PayloadLength,
    IN UINT8 Key[],
    IN UINT  KeyLength,
    IN UINT8 Nonce[],
    IN UINT  NonceLength,
    IN UINT8 AAD[],
    IN UINT  AADLength,
    IN UINT  MACLength,
    OUT UINT8 MACText[])
{
    UINT8 Block[AES_BLOCK_SIZES], Block_MAC[AES_BLOCK_SIZES];
    UINT  Block_Index = 0, ADD_Index = 0, Payload_Index = 0;
    UINT  Temp_Value = 0, Temp_Index = 0, Temp_Length = 0, Copy_Length = 0;

    /*   
     * 1. Formatting of the Control Information and the Nonce
     */
    NdisZeroMemory(Block, AES_BLOCK_SIZES);
    if (AADLength > 0)
        Block[0] |= 0x40; /* Set bit 6 to 1 */
    Temp_Value = ((MACLength - 2) >> 1) << 3; /* Set bit 3-5 to (t-2)/2 */
    Block[0] |= Temp_Value;
    Temp_Value = (15 - NonceLength) - 1; /* Set bit 0-2 to (q-1), q = 15 - Nonce Length */
    Block[0] |= Temp_Value;
    for (Temp_Index = 0; Temp_Index < NonceLength; Temp_Index++)
        Block[Temp_Index + 1] = Nonce[Temp_Index];
    if (NonceLength < 12)
        Block[12] = (PayloadLength >> 24) & 0xff;
    if (NonceLength < 13)
        Block[13] = (PayloadLength >> 16) & 0xff;
    Block[14] = (PayloadLength >> 8) & 0xff;
    Block[15] = PayloadLength & 0xff;

    NdisZeroMemory(Block_MAC, AES_BLOCK_SIZES);
    Temp_Length = sizeof(Block_MAC);
    RT_AES_Encrypt(Block, AES_BLOCK_SIZES , Key, KeyLength, Block_MAC, &Temp_Length);

    /*
     * 2. Formatting of the Associated Data
     *      If 0 < AADLength < (2^16 - 2^8), AData_Length = 2
     *      If (2^16 - 2^8) < AADLength < 2^32, AData_Length = 6
     *      If 2^32 < AADLength < 2^64, AData_Length = 10 (not implement)
     */    
    NdisZeroMemory(Block, AES_BLOCK_SIZES);
    if ((AADLength > 0) && (AADLength < 0xFF00)) {
        Block_Index = 2;
        Block[0] = (AADLength >> 8) & 0xff;
        Block[1] = AADLength & 0xff;
    } else {
        Block_Index = 6;
        Block[2] = (AADLength >> 24) & 0xff;
        Block[3] = (AADLength >> 16) & 0xff;
        Block[4] = (AADLength >> 8) & 0xff;
        Block[5] = AADLength & 0xff;
    } /* End of if */

    while (ADD_Index < AADLength) 
    {
        Copy_Length = AADLength - ADD_Index;
        if (Copy_Length > AES_BLOCK_SIZES)
            Copy_Length = AES_BLOCK_SIZES;
        if ((Copy_Length + Block_Index) > AES_BLOCK_SIZES) {
            Copy_Length = AES_BLOCK_SIZES - Block_Index;
        } /* End of if */                    
        for (Temp_Index = 0; Temp_Index < Copy_Length; Temp_Index++)
            Block[Temp_Index + Block_Index] = AAD[ADD_Index + Temp_Index];        
        for (Temp_Index = 0; Temp_Index < AES_BLOCK_SIZES; Temp_Index++)
            Block[Temp_Index] ^= Block_MAC[Temp_Index];
        NdisZeroMemory(Block_MAC, AES_BLOCK_SIZES);
        Temp_Length = sizeof(Block_MAC);        
        RT_AES_Encrypt(Block, AES_BLOCK_SIZES , Key, KeyLength, Block_MAC, &Temp_Length);
        ADD_Index += Copy_Length;
        Block_Index = 0;        
        NdisZeroMemory(Block, AES_BLOCK_SIZES);
    } /* End of while */

    /*
     * 3. Calculate the MAC (MIC)
     */
    while (Payload_Index < PayloadLength) 
    {
        NdisZeroMemory(Block, AES_BLOCK_SIZES);
        Copy_Length = PayloadLength - Payload_Index;
        if (Copy_Length > AES_BLOCK_SIZES)
            Copy_Length = AES_BLOCK_SIZES;
        for (Temp_Index = 0; Temp_Index < Copy_Length; Temp_Index++)
            Block[Temp_Index] = Payload[Payload_Index + Temp_Index];        
        for (Temp_Index = 0; Temp_Index < AES_BLOCK_SIZES; Temp_Index++)
            Block[Temp_Index] ^= Block_MAC[Temp_Index];
        NdisZeroMemory(Block_MAC, AES_BLOCK_SIZES);
        Temp_Length = sizeof(Block_MAC);
        RT_AES_Encrypt(Block, AES_BLOCK_SIZES , Key, KeyLength, Block_MAC, &Temp_Length);
        Payload_Index += Copy_Length;
    } /* End of while */
    for (Temp_Index = 0; Temp_Index < MACLength; Temp_Index++)
        MACText[Temp_Index] = Block_MAC[Temp_Index];
} /* End of AES_CCM_MAC */


/*
========================================================================
Routine Description:
    AES-CBCMAC Encryption

Arguments:
    PlainText        Plain text
    PlainTextLength  The length of plain text in bytes
    Key              Cipher key
    KeyLength        The length of cipher key in bytes depend on block cipher (16, 24, or 32 bytes)
    Nonce            Nonce
    NonceLength      The length of nonce in bytes
    AAD              Additional authenticated data
    AADLength        The length of AAD in bytes
    MACLength        The length of MAC in bytes
    CipherTextLength    The length of allocated memory spaces in bytes

Return Value:
    CipherText       The ciphertext
    CipherTextLength Return the length of the ciphertext in bytes

Function Value:
     0: Success
    -1: The key length must be 16 bytes.
    -2: A valid nonce length is 7-13 bytes.
    -3: The MAC length  must be 4, 6, 8, 10, 12, 14, or 16 bytes.
    -4: The CipherTextLength is not enough.

Note:
    Reference to RFC 3601, and NIST 800-38C.
    Here, the implement of AES_CCM is suitable for WI_FI.
========================================================================
*/
INT AES_CCM_Encrypt (
    IN UINT8 PlainText[],
    IN UINT PlainTextLength,
    IN UINT8 Key[],
    IN UINT KeyLength,
    IN UINT8 Nonce[],
    IN UINT NonceLength,
    IN UINT8 AAD[],
    IN UINT AADLength,
    IN UINT MACLength,
    OUT UINT8 CipherText[],
    INOUT UINT *CipherTextLength)
{
    UINT8 Block_MAC[AES_BLOCK_SIZES];
    UINT8 Block_CTR[AES_BLOCK_SIZES], Block_CTR_Cipher[AES_BLOCK_SIZES];
    UINT  Cipher_Index = 0;
    UINT Temp_Value = 0, Temp_Index = 0, Temp_Length = 0, Copy_Length = 0;

    /*   
     * 1. Check Input Values
     *    - Key length must be 16 bytes
     *    - Nonce length range is form 7 to 13 bytes
     *    - MAC length must be 4, 6, 8, 10, 12, 14, or 16 bytes
     *    - CipherTextLength > PlainTextLength + MACLength
     */
    if (KeyLength != AES_KEY128_LENGTH) {
    	DBGPRINT(RT_DEBUG_ERROR, ("AES_CCM_Encrypt: The key length must be %d bytes\n", AES_KEY128_LENGTH));
        return -1;
    } /* End of if */

    if ((NonceLength < 7) || (NonceLength > 13)) {
    	DBGPRINT(RT_DEBUG_ERROR, ("AES_CCM_Encrypt: A valid nonce length is 7-13 bytes\n"));
        return -2;
    } /* End of if */

    if ((MACLength != 4) && (MACLength != 6) && (MACLength != 8) && (MACLength != 10)
        && (MACLength != 12) && (MACLength != 14) && (MACLength != 16)) {
    	DBGPRINT(RT_DEBUG_ERROR, ("AES_CCM_Encrypt: The MAC length  must be 4, 6, 8, 10, 12, 14, or 16 bytes\n"));
        return -3;
    } /* End of if */

    if (*CipherTextLength < (PlainTextLength + MACLength)) {
    	DBGPRINT(RT_DEBUG_ERROR, ("AES_CCM_Encrypt: The CipherTextLength is not enough.\n"));
        return -4;
    } /* End of if */


    /*   
     * 1. Formatting of the Counter Block
     */
    NdisZeroMemory(Block_CTR, AES_BLOCK_SIZES);
    Temp_Value = (15 - NonceLength) - 1; /* Set bit 0-2 to (q-1), q = 15 - Nonce Length */
    Block_CTR[0] |= Temp_Value;
    for (Temp_Index = 0; Temp_Index < NonceLength; Temp_Index++)
        Block_CTR[Temp_Index + 1] = Nonce[Temp_Index];

    /*
     * 2. Calculate the MAC (MIC)
     */
    AES_CCM_MAC(PlainText, PlainTextLength, Key, KeyLength, Nonce, NonceLength, AAD, AADLength, MACLength, Block_MAC);
    Temp_Length = sizeof(Block_CTR_Cipher);
    RT_AES_Encrypt(Block_CTR, AES_BLOCK_SIZES , Key, KeyLength, Block_CTR_Cipher, &Temp_Length);
    for (Temp_Index = 0; Temp_Index < MACLength; Temp_Index++)
        Block_MAC[Temp_Index] ^= Block_CTR_Cipher[Temp_Index];

    /*   
     * 3. Cipher Payload
     */
    while (Cipher_Index < PlainTextLength) 
    {
        Block_CTR[15] += 1;
        Temp_Length = sizeof(Block_CTR_Cipher);
        RT_AES_Encrypt(Block_CTR, AES_BLOCK_SIZES , Key, KeyLength, Block_CTR_Cipher, &Temp_Length);

        Copy_Length = PlainTextLength - Cipher_Index;
        if (Copy_Length > AES_BLOCK_SIZES)
            Copy_Length = AES_BLOCK_SIZES;                
        for (Temp_Index = 0; Temp_Index < Copy_Length; Temp_Index++)
            CipherText[Cipher_Index + Temp_Index] = PlainText[Cipher_Index + Temp_Index]^Block_CTR_Cipher[Temp_Index];

        Cipher_Index += Copy_Length;
    } /* End of while */
    for (Temp_Index = 0; Temp_Index < MACLength; Temp_Index++)
            CipherText[PlainTextLength + Temp_Index] = Block_MAC[Temp_Index];    
    *CipherTextLength = PlainTextLength + MACLength;

    return 0;
} /* End of AES_CCM_Encrypt */


/*
========================================================================
Routine Description:
    AES-CBCMAC Decryption

Arguments:
    CipherText       The ciphertext
    CipherTextLength The length of cipher text in bytes
    Key              Cipher key
    KeyLength        The length of cipher key in bytes depend on block cipher (16, 24, or 32 bytes)
    Nonce            Nonce
    NonceLength      The length of nonce in bytes
    AAD              Additional authenticated data
    AADLength        The length of AAD in bytes
    CipherTextLength    The length of allocated memory spaces in bytes

Return Value:
    PlainText        Plain text
    PlainTextLength  Return the length of the plain text in bytes

Function Value:
     0: Success
    -1: The key length must be 16 bytes.
    -2: A valid nonce length is 7-13 bytes.
    -3: The MAC length  must be 4, 6, 8, 10, 12, 14, or 16 bytes.
    -4: The PlainTextLength is not enough.
    -5: The MIC does not match.
    
Note:
    Reference to RFC 3601, and NIST 800-38C.
    Here, the implement of AES_CCM is suitable for WI_FI.
========================================================================
*/
INT AES_CCM_Decrypt (
    IN UINT8 CipherText[],
    IN UINT  CipherTextLength,
    IN UINT8 Key[],
    IN UINT  KeyLength,
    IN UINT8 Nonce[],
    IN UINT  NonceLength,
    IN UINT8 AAD[],
    IN UINT  AADLength,
    IN UINT  MACLength,    
    OUT UINT8 PlainText[],
    INOUT UINT *PlainTextLength)
{
    UINT8 Block_MAC[AES_BLOCK_SIZES], Block_MAC_From_Cipher[AES_BLOCK_SIZES];
    UINT8 Block_CTR[AES_BLOCK_SIZES], Block_CTR_Cipher[AES_BLOCK_SIZES];
    UINT  Block_Index = 0, Cipher_Index = 0;
    UINT Temp_Value = 0, Temp_Index = 0, Temp_Length = 0, Copy_Length = 0;


    /*   
     * 1. Check Input Values
     *    - Key length must be 16 bytes
     *    - Nonce length range is form 7 to 13 bytes
     */
    if (KeyLength != AES_KEY128_LENGTH) {
    	DBGPRINT(RT_DEBUG_ERROR, ("AES_CCM_Decrypt: The key length must be %d bytes\n", AES_KEY128_LENGTH));
        return -1;
    } /* End of if */

    if ((NonceLength < 7) || (NonceLength > 13)) {
    	DBGPRINT(RT_DEBUG_ERROR, ("AES_CCM_Decrypt: A valid nonce length is 7-13 bytes\n"));
        return -2;
    } /* End of if */
    
    if ((MACLength != 4) && (MACLength != 6) && (MACLength != 8) && (MACLength != 10)
        && (MACLength != 12) && (MACLength != 14) && (MACLength != 16)) {
    	DBGPRINT(RT_DEBUG_ERROR, ("AES_CCM_Decrypt: The MAC length  must be 4, 6, 8, 10, 12, 14, or 16 bytes\n"));
        return -3;
    } /* End of if */
    
    if (*PlainTextLength < (CipherTextLength - MACLength)) {
    	DBGPRINT(RT_DEBUG_ERROR, ("AES_CCM_Decrypt: The PlainTextLength is not enough.\n"));
        return -4;
    } /* End of if */

    /*   
     * 2. Formatting of the Counter Block
     */
    NdisZeroMemory(Block_CTR, AES_BLOCK_SIZES);
    Temp_Value = (15 - NonceLength) - 1; /* Set bit 0-2 to (q-1), q = 15 - Nonce Length */
    Block_CTR[0] |= Temp_Value;
    for (Temp_Index = 0; Temp_Index < NonceLength; Temp_Index++)
        Block_CTR[Temp_Index + 1] = Nonce[Temp_Index];
    Temp_Length = sizeof(Block_CTR_Cipher);
    RT_AES_Encrypt(Block_CTR, AES_BLOCK_SIZES , Key, KeyLength, Block_CTR_Cipher, &Temp_Length);

    /*
     * 3. Catch the MAC (MIC) from CipherText
     */
    Block_Index = 0;
    for (Temp_Index = (CipherTextLength - MACLength); Temp_Index < CipherTextLength; Temp_Index++, Block_Index++)
        Block_MAC_From_Cipher[Block_Index] = CipherText[Temp_Index]^Block_CTR_Cipher[Block_Index];

    /*
     * 4. Decryption the Payload
     */     
    while (Cipher_Index < (CipherTextLength - MACLength)) 
    {
        Block_CTR[15] += 1;
        Temp_Length = sizeof(Block_CTR_Cipher);
        RT_AES_Encrypt(Block_CTR, AES_BLOCK_SIZES , Key, KeyLength, Block_CTR_Cipher, &Temp_Length);

        Copy_Length = (CipherTextLength - MACLength) - Cipher_Index;
        if (Copy_Length > AES_BLOCK_SIZES)
            Copy_Length = AES_BLOCK_SIZES;                
        for (Temp_Index = 0; Temp_Index < Copy_Length; Temp_Index++)
            PlainText[Cipher_Index + Temp_Index] = CipherText[Cipher_Index + Temp_Index]^Block_CTR_Cipher[Temp_Index];
        Cipher_Index += Copy_Length;
    } /* End of while */
    *PlainTextLength = CipherTextLength - MACLength;
    
    /*
     * 5. Calculate the MAC (MIC) from Payload
     */
    AES_CCM_MAC(PlainText, *PlainTextLength, Key, KeyLength, Nonce, NonceLength, AAD, AADLength, MACLength, Block_MAC);

    /*
     * 6. Check the MIC
     */
    if (NdisCmpMemory(Block_MAC_From_Cipher, Block_MAC, MACLength) != 0) {
    	DBGPRINT(RT_DEBUG_ERROR, ("AES_CCM_Decrypt: The MIC does not match.\n"));
        return -5;
    } /* End of if */

    return 0;
} /* End of AES_CCM_Decrypt */


/*
========================================================================
Routine Description:
    AES-CMAC generate subkey

Arguments:
    Key        Cipher key 128 bits
    KeyLength  The length of Cipher key in bytes

Return Value:
    SubKey1    SubKey 1 128 bits
    SubKey2    SubKey 2 128 bits

Note:
    Reference to RFC 4493
    
    Step 1.  L := AES-128(K, const_Zero);
    Step 2.  if MSB(L) is equal to 0
                then    K1 := L << 1;
                else    K1 := (L << 1) XOR const_Rb;
    Step 3.  if MSB(K1) is equal to 0
                then    K2 := K1 << 1;
                else    K2 := (K1 << 1) XOR const_Rb;
    Step 4.  return K1, K2;
========================================================================
*/
VOID AES_CMAC_GenerateSubKey (
    IN UINT8 Key[],
    IN UINT KeyLength,
    OUT UINT8 SubKey1[],
    OUT UINT8 SubKey2[])
{
    UINT8 MSB_L = 0, MSB_K1 = 0, Top_Bit = 0;
    UINT  SubKey1_Length = 0;
    INT   Index = 0;

    if (KeyLength != AES_KEY128_LENGTH) {
    	DBGPRINT(RT_DEBUG_ERROR, ("AES_CMAC_GenerateSubKey: key length is %d bytes, it must be %d bytes(128 bits).\n", 
            KeyLength, AES_KEY128_LENGTH));
        return;
    } /* End of if */

    /* Step 1: L := AES-128(K, const_Zero); */
    SubKey1_Length = 16;
    RT_AES_Encrypt(Const_Zero, sizeof(Const_Zero), Key, KeyLength, SubKey1, &SubKey1_Length);

    /*
     * Step 2.  if MSB(L) is equal to 0
     *           then    K1 := L << 1;
     *           else    K1 := (L << 1) XOR const_Rb;
     */
    MSB_L = SubKey1[0] & 0x80;    
    for(Index = 0; Index < 15; Index++) {
        Top_Bit = (SubKey1[Index + 1] & 0x80)?1:0;
        SubKey1[Index] <<= 1;
        SubKey1[Index] |= Top_Bit;
    }
    SubKey1[15] <<= 1;
    if (MSB_L > 0) {
        for(Index = 0; Index < 16; Index++)
            SubKey1[Index] ^= Const_Rb[Index];
    } /* End of if */

    /*
     * Step 3.  if MSB(K1) is equal to 0
     *           then    K2 := K1 << 1;
     *           else    K2 := (K1 << 1) XOR const_Rb;
     */
    MSB_K1 = SubKey1[0] & 0x80;
    for(Index = 0; Index < 15; Index++) {
        Top_Bit = (SubKey1[Index + 1] & 0x80)?1:0;
        SubKey2[Index] = SubKey1[Index] << 1;
        SubKey2[Index] |= Top_Bit;
    }
    SubKey2[15] = SubKey1[15] << 1;
    if (MSB_K1 > 0) {
        for(Index = 0; Index < 16; Index++)
            SubKey2[Index] ^= Const_Rb[Index];
    } /* End of if */
} /* End of AES_CMAC_GenerateSubKey */


/*
========================================================================
Routine Description:
    AES-CMAC

Arguments:
    PlainText        Plain text
    PlainTextLength  The length of plain text in bytes
    Key              Cipher key, it may be 16, 24, or 32 bytes (128, 192, or 256 bits)
    KeyLength        The length of cipher key in bytes
    MACTextLength    The length of allocated memory spaces in bytes

Return Value:
    MACText       Message authentication code (128-bit string)
    MACTextLength Return the length of Message authentication code in bytes

Note:
    Reference to RFC 4493
========================================================================
*/
VOID AES_CMAC (
    IN UINT8 PlainText[],
    IN UINT PlainTextLength,
    IN UINT8 Key[],
    IN UINT KeyLength,
    OUT UINT8 MACText[],
    INOUT UINT *MACTextLength)
{
    UINT  PlainBlockStart;
    UINT8 X[AES_BLOCK_SIZES], Y[AES_BLOCK_SIZES];
    UINT8 SubKey1[16];
    UINT8 SubKey2[16];
    INT Index;
    UINT X_Length;

    if (*MACTextLength < AES_MAC_LENGTH) {
    	DBGPRINT(RT_DEBUG_ERROR, ("AES_CMAC: MAC text length is less than %d bytes).\n", 
            AES_MAC_LENGTH));
        return;
    } /* End of if */
    if (KeyLength != AES_KEY128_LENGTH) {
    	DBGPRINT(RT_DEBUG_ERROR, ("AES_CMAC: key length is %d bytes, it must be %d bytes(128 bits).\n", 
            KeyLength, AES_KEY128_LENGTH));
        return;
    } /* End of if */

    /* Step 1.  (K1,K2) := Generate_Subkey(K); */
    NdisZeroMemory(SubKey1, 16);
    NdisZeroMemory(SubKey2, 16);   
    AES_CMAC_GenerateSubKey(Key, KeyLength, SubKey1, SubKey2);

    /*   
     * 2. Main algorithm
     *    - Plain text divide into serveral blocks (16 bytes/block)
     *    - If plain text is not divided with no remainder by block, padding size = (block - remainder plain text)
     *    - Execute RT_AES_Encrypt procedure.
     */
    PlainBlockStart = 0;
    NdisMoveMemory(X, Const_Zero, AES_BLOCK_SIZES);
    while ((PlainTextLength - PlainBlockStart) > AES_BLOCK_SIZES)
    {
        for (Index = 0; Index < AES_BLOCK_SIZES; Index++)
                Y[Index] = PlainText[PlainBlockStart + Index]^X[Index];

        X_Length = sizeof(X);
        RT_AES_Encrypt(Y, sizeof(Y) , Key, KeyLength, X, &X_Length);
        PlainBlockStart += ((UINT) AES_BLOCK_SIZES);
    } /* End of while */
    if ((PlainTextLength - PlainBlockStart) == AES_BLOCK_SIZES) {
        for (Index = 0; Index < AES_BLOCK_SIZES; Index++)
                Y[Index] = PlainText[PlainBlockStart + Index]^X[Index]^SubKey1[Index];        
    } else {    
        NdisZeroMemory(Y, AES_BLOCK_SIZES);
        NdisMoveMemory(Y, &PlainText[PlainBlockStart], (PlainTextLength - PlainBlockStart));
        Y[(PlainTextLength - PlainBlockStart)] = 0x80;
        for (Index = 0; Index < AES_BLOCK_SIZES; Index++)
                Y[Index] = Y[Index]^X[Index]^SubKey2[Index];
    } /* End of if */
    RT_AES_Encrypt(Y, sizeof(Y) , Key, KeyLength, MACText, MACTextLength);
} /* End of AES_CMAC */


/* For AES_Key_Wrap */
#define AES_KEY_WRAP_IV_LENGTH 8 /* 64-bit */
#define AES_KEY_WRAP_BLOCK_SIZE 8 /* 64-bit */
static UINT8 Default_IV[8] = {
    0xa6, 0xa6, 0xa6, 0xa6, 0xa6, 0xa6, 0xa6, 0xa6};

/*
========================================================================
Routine Description:
    AES-CBC encryption

Arguments:
    PlainText        Plain text
    PlainTextLength  The length of plain text in bytes
    Key              Cipher key, it may be 16, 24, or 32 bytes (128, 192, or 256 bits)
    KeyLength        The length of cipher key in bytes
    IV               Initialization vector, it may be 16 bytes (128 bits)
    IVLength         The length of initialization vector in bytes
    CipherTextLength The length of allocated cipher text in bytes

Return Value:
    CipherText       Return cipher text
    CipherTextLength Return the length of real used cipher text in bytes

Note:
    Reference to RFC 3602 and NIST 800-38A
========================================================================
*/
VOID AES_CBC_Encrypt (
    IN UINT8 PlainText[],
    IN UINT PlainTextLength,
    IN UINT8 Key[],
    IN UINT KeyLength,
    IN UINT8 IV[],
    IN UINT IVLength,
    OUT UINT8 CipherText[],
    INOUT UINT *CipherTextLength)
{
    UINT PaddingSize, PlainBlockStart, CipherBlockStart, CipherBlockSize;
    UINT Index;
    UINT8 Block[AES_BLOCK_SIZES];

    /*   
     * 1. Check the input parameters
     *    - CipherTextLength > (PlainTextLength + Padding size), Padding size = block size - (PlainTextLength % block size)
     *    - Key length must be 16, 24, or 32 bytes(128, 192, or 256 bits) 
     *    - IV length must be 16 bytes(128 bits) 
     */
    PaddingSize = ((UINT) AES_BLOCK_SIZES) - (PlainTextLength % ((UINT)AES_BLOCK_SIZES));
    if (*CipherTextLength < (PlainTextLength + PaddingSize)) {
    	DBGPRINT(RT_DEBUG_ERROR, ("AES_CBC_Encrypt: cipher text length is %d bytes < (plain text length %d bytes + padding size %d bytes).\n", 
            *CipherTextLength, PlainTextLength, PaddingSize));
        return;
    } /* End of if */    
    if ((KeyLength != AES_KEY128_LENGTH) && (KeyLength != AES_KEY192_LENGTH) && (KeyLength != AES_KEY256_LENGTH)) {
    	DBGPRINT(RT_DEBUG_ERROR, ("AES_CBC_Encrypt: key length is %d bytes, it must be %d, %d, or %d bytes(128, 192, or 256 bits).\n", 
            KeyLength, AES_KEY128_LENGTH, AES_KEY192_LENGTH, AES_KEY256_LENGTH));
        return;
    } /* End of if */
    if (IVLength != AES_CBC_IV_LENGTH) {
    	DBGPRINT(RT_DEBUG_ERROR, ("AES_CBC_Encrypt: IV length is %d bytes, it must be %d bytes(128bits).\n", 
            IVLength, AES_CBC_IV_LENGTH));
        return;
    } /* End of if */


    /*   
     * 2. Main algorithm
     *    - Plain text divide into serveral blocks (16 bytes/block)
     *    - If plain text is divided with no remainder by block, add a new block and padding size = block(16 bytes)
     *    - If plain text is not divided with no remainder by block, padding size = (block - remainder plain text)
     *    - Execute RT_AES_Encrypt procedure.
     *    
     *    - Padding method: The remainder bytes will be filled with padding size (1 byte)
     */
    PlainBlockStart = 0;
    CipherBlockStart = 0;
    while ((PlainTextLength - PlainBlockStart) >= AES_BLOCK_SIZES)
    {
        if (CipherBlockStart == 0) {
            for (Index = 0; Index < AES_BLOCK_SIZES; Index++)
                Block[Index] = PlainText[PlainBlockStart + Index]^IV[Index];                
        } else {
            for (Index = 0; Index < AES_BLOCK_SIZES; Index++)
                Block[Index] = PlainText[PlainBlockStart + Index]^CipherText[CipherBlockStart - ((UINT) AES_BLOCK_SIZES) + Index];
        } /* End of if */
            
        CipherBlockSize = *CipherTextLength - CipherBlockStart;
        RT_AES_Encrypt(Block, AES_BLOCK_SIZES , Key, KeyLength, CipherText + CipherBlockStart, &CipherBlockSize);

        PlainBlockStart += ((UINT) AES_BLOCK_SIZES);
        CipherBlockStart += CipherBlockSize;
    } /* End of while */

    NdisMoveMemory(Block, (&PlainText[0] + PlainBlockStart), (PlainTextLength - PlainBlockStart));
    NdisFillMemory((Block + (((UINT) AES_BLOCK_SIZES) -PaddingSize)), PaddingSize, (UINT8) PaddingSize);
    if (CipherBlockStart == 0) {
       for (Index = 0; Index < AES_BLOCK_SIZES; Index++)
           Block[Index] ^= IV[Index];
    } else {
       for (Index = 0; Index < AES_BLOCK_SIZES; Index++)
           Block[Index] ^= CipherText[CipherBlockStart - ((UINT) AES_BLOCK_SIZES) + Index];
    } /* End of if */
    CipherBlockSize = *CipherTextLength - CipherBlockStart;
    RT_AES_Encrypt(Block, AES_BLOCK_SIZES , Key, KeyLength, CipherText + CipherBlockStart, &CipherBlockSize);
    CipherBlockStart += CipherBlockSize;
    *CipherTextLength = CipherBlockStart;
} /* End of AES_CBC_Encrypt */


/*
========================================================================
Routine Description:
    AES-CBC decryption

Arguments:
    CipherText       Cipher text
    CipherTextLength The length of cipher text in bytes
    Key              Cipher key, it may be 16, 24, or 32 bytes (128, 192, or 256 bits)
    KeyLength        The length of cipher key in bytes
    IV               Initialization vector, it may be 16 bytes (128 bits)
    IVLength         The length of initialization vector in bytes
    PlainTextLength  The length of allocated plain text in bytes

Return Value:
    PlainText        Return plain text
    PlainTextLength  Return the length of real used plain text in bytes

Note:
    Reference to RFC 3602 and NIST 800-38A
========================================================================
*/
VOID AES_CBC_Decrypt (
    IN UINT8 CipherText[],
    IN UINT CipherTextLength,
    IN UINT8 Key[],
    IN UINT KeyLength,
    IN UINT8 IV[],
    IN UINT IVLength,
    OUT UINT8 PlainText[],
    INOUT UINT *PlainTextLength)
{
    UINT PaddingSize, PlainBlockStart, CipherBlockStart, PlainBlockSize;
    UINT Index;

    /*   
     * 1. Check the input parameters
     *    - CipherTextLength must be divided with no remainder by block
     *    - Key length must be 16, 24, or 32 bytes(128, 192, or 256 bits) 
     *    - IV length must be 16 bytes(128 bits) 
     */
    if ((CipherTextLength % AES_BLOCK_SIZES) != 0) {
    	DBGPRINT(RT_DEBUG_ERROR, ("AES_CBC_Decrypt: cipher text length is %d bytes, it can't be divided with no remainder by block size(%d).\n", 
            CipherTextLength, AES_BLOCK_SIZES));
        return;
    } /* End of if */    
    if ((KeyLength != AES_KEY128_LENGTH) && (KeyLength != AES_KEY192_LENGTH) && (KeyLength != AES_KEY256_LENGTH)) {
    	DBGPRINT(RT_DEBUG_ERROR, ("AES_CBC_Decrypt: key length is %d bytes, it must be %d, %d, or %d bytes(128, 192, or 256 bits).\n", 
            KeyLength, AES_KEY128_LENGTH, AES_KEY192_LENGTH, AES_KEY256_LENGTH));
        return;
    } /* End of if */
    if (IVLength != AES_CBC_IV_LENGTH) {
    	DBGPRINT(RT_DEBUG_ERROR, ("AES_CBC_Decrypt: IV length is %d bytes, it must be %d bytes(128bits).\n", 
            IVLength, AES_CBC_IV_LENGTH));
        return;
    } /* End of if */


    /*   
     * 2. Main algorithm
     *    - Cypher text divide into serveral blocks (16 bytes/block)
     *    - Execute RT_AES_Decrypt procedure.
     *    - Remove padding bytes, padding size is the last byte of plain text
     */
    CipherBlockStart = 0;
    PlainBlockStart = 0;
    while ((CipherTextLength - CipherBlockStart) >= AES_BLOCK_SIZES)
    {
        PlainBlockSize = *PlainTextLength - PlainBlockStart;
        RT_AES_Decrypt(CipherText + CipherBlockStart, AES_BLOCK_SIZES , Key, KeyLength, PlainText + PlainBlockStart, &PlainBlockSize);

        if (PlainBlockStart == 0) {
            for (Index = 0; Index < AES_BLOCK_SIZES; Index++)
                PlainText[PlainBlockStart + Index] ^= IV[Index];                
        } else {
            for (Index = 0; Index < AES_BLOCK_SIZES; Index++)
                PlainText[PlainBlockStart + Index] ^= CipherText[CipherBlockStart + Index - ((UINT) AES_BLOCK_SIZES)];
        } /* End of if */

        CipherBlockStart += AES_BLOCK_SIZES;
        PlainBlockStart += PlainBlockSize;
    } /* End of while */

    PaddingSize = (UINT8) PlainText[PlainBlockStart -1];   
    *PlainTextLength = PlainBlockStart - PaddingSize;

} /* End of AES_CBC_Encrypt */


/*
========================================================================
Routine Description:
    AES key wrap algorithm

Arguments:
    PlainText        Plain text
    PlainTextLength  The length of plain text in bytes
    Key              Cipher key
    KeyLength        The length of cipher key in bytes depend on block cipher (16, 24, or 32 bytes)

Return Value:
    CipherText       The ciphertext
    CipherTextLength Return the length of the ciphertext in bytes
    
Function Value:
     0: Success
    -1: The key length must be 16, 24, or 32 bytes
    -2: Not enough memory
    
Note:
    Reference to RFC 3394
========================================================================
*/
INT AES_Key_Wrap (
    IN UINT8 PlainText[],
    IN UINT  PlainTextLength,
    IN UINT8 Key[],
    IN UINT  KeyLength,
    OUT UINT8 CipherText[],
    OUT UINT *CipherTextLength)
{
    UINT8 IV[8], Block_B[16], Block_Input[16];
    UINT8 *pResult;
    UINT  Temp_Length = 0, Number_Of_Block = 0;
    INT   Index_i = 0, Index_j = 0;
    
    /*   
     * 0. Check input parameter
     */
    if ((KeyLength != AES_KEY128_LENGTH) && (KeyLength != AES_KEY192_LENGTH) && (KeyLength != AES_KEY256_LENGTH)) {
    	DBGPRINT(RT_DEBUG_ERROR, ("AES_Key_Wrap: key length is %d bytes, it must be %d, %d, or %d bytes(128, 192, or 256 bits).\n", 
            KeyLength, AES_KEY128_LENGTH, AES_KEY192_LENGTH, AES_KEY256_LENGTH));
        return -1;
    } /* End of if */    
	os_alloc_mem(NULL, (UCHAR **)&pResult, sizeof(UINT8)*PlainTextLength);
/*    if ((pResult = (UINT8 *) kmalloc(sizeof(UINT8)*PlainTextLength, GFP_ATOMIC)) == NULL) {
*/
    if (pResult == NULL) {
    	DBGPRINT(RT_DEBUG_ERROR, ("AES_Key_Wrap: allocate %d bytes memory failure.\n", sizeof(UINT8)*PlainTextLength));
        return -2;
    } /* End of if */


    /*
     * 1. Initialize variables
     */
    Number_Of_Block = PlainTextLength / AES_KEY_WRAP_BLOCK_SIZE; /* 64 bits each block
*/
    NdisMoveMemory(IV, Default_IV, AES_KEY_WRAP_IV_LENGTH);
    NdisMoveMemory(pResult, PlainText, PlainTextLength);


    /*
     * 2. Calculate intermediate values
     */
    for (Index_j = 0;Index_j < 6 ;Index_j++)
    {   
        for (Index_i = 0;Index_i < Number_Of_Block;Index_i++)
        {
            NdisMoveMemory(Block_Input, IV, 8);
            NdisMoveMemory(Block_Input + 8, pResult + (Index_i*8), 8);
            Temp_Length = sizeof(Block_B);            
            RT_AES_Encrypt(Block_Input, AES_BLOCK_SIZES , Key, KeyLength, Block_B, &Temp_Length);

            NdisMoveMemory(IV, Block_B, 8);
            IV[7] = Block_B[7] ^ ((Number_Of_Block * Index_j) + Index_i + 1);            
            NdisMoveMemory(pResult + (Index_i*8), (Block_B + 8), 8);
        } /* End of for */
    } /* End of for */


    /*
     * 3. Output the results
     */
    *CipherTextLength = PlainTextLength + AES_KEY_WRAP_IV_LENGTH;
    NdisMoveMemory(CipherText, IV, AES_KEY_WRAP_IV_LENGTH);
    NdisMoveMemory(CipherText + AES_KEY_WRAP_IV_LENGTH, pResult, PlainTextLength);

/*    kfree(pResult);
*/
	os_free_mem(NULL, pResult);
    return 0;
} /* End of AES_Key_Wrap */


/*
========================================================================
Routine Description:
    AES key unwrap algorithm

Arguments:
    CipherText       The ciphertext
    CipherTextLength The length of cipher text in bytes
    Key              Cipher key
    KeyLength        The length of cipher key in bytes depend on block cipher (16, 24, or 32 bytes)

Return Value:
    PlainText        Plain text
    PlainTextLength  Return the length of the plain text in bytes    

Function Value:
     0: Success

Note:
    Reference to RFC 3394
========================================================================
*/
INT AES_Key_Unwrap (
    IN UINT8 CipherText[],
    IN UINT  CipherTextLength,
    IN UINT8 Key[],
    IN UINT  KeyLength,
    OUT UINT8 PlainText[],
    OUT UINT *PlainTextLength)
{
    UINT8 IV[8], Block_B[16], Block_Input[16];
    UINT8 *pResult;
    UINT  Temp_Length = 0, Number_Of_Block = 0, PlainLength;
    INT   Index_i = 0, Index_j = 0;
    
    /*   
     * 0. Check input parameter
     */
    PlainLength = CipherTextLength - AES_KEY_WRAP_IV_LENGTH;
    if ((KeyLength != AES_KEY128_LENGTH) && (KeyLength != AES_KEY192_LENGTH) && (KeyLength != AES_KEY256_LENGTH)) {
    	DBGPRINT(RT_DEBUG_ERROR, ("AES_Key_Unwrap: key length is %d bytes, it must be %d, %d, or %d bytes(128, 192, or 256 bits).\n", 
            KeyLength, AES_KEY128_LENGTH, AES_KEY192_LENGTH, AES_KEY256_LENGTH));
        return -1;
    } /* End of if */    
	os_alloc_mem(NULL, (UCHAR **)&pResult, sizeof(UINT8)*PlainLength);
/*    if ((pResult = (UINT8 *) kmalloc(sizeof(UINT8)*PlainLength, GFP_ATOMIC)) == NULL) {
*/
    if (pResult == NULL) {
    	DBGPRINT(RT_DEBUG_ERROR, ("AES_Key_Unwrap: allocate %d bytes memory failure.\n", sizeof(UINT8)*PlainLength));
        return -2;
    } /* End of if */


    /*
     * 1. Initialize variables
     */
    Number_Of_Block = PlainLength / AES_KEY_WRAP_BLOCK_SIZE; /* 64 bits each block
*/
    NdisMoveMemory(IV, CipherText, AES_KEY_WRAP_IV_LENGTH);
    NdisMoveMemory(pResult, CipherText + AES_KEY_WRAP_IV_LENGTH, PlainLength);


    /*
     * 2. Calculate intermediate values
     */
    for (Index_j = 5;Index_j >= 0 ;Index_j--)
    {   
        for (Index_i = (Number_Of_Block - 1);Index_i >= 0;Index_i--)
        {
            IV[7] = IV[7] ^ ((Number_Of_Block * Index_j) + Index_i + 1);
            NdisMoveMemory(Block_Input, IV, 8);
            NdisMoveMemory(Block_Input + 8, pResult + (Index_i*8), 8);
            Temp_Length = sizeof(Block_B);
            RT_AES_Decrypt(Block_Input, AES_BLOCK_SIZES , Key, KeyLength, Block_B, &Temp_Length);
            
            NdisMoveMemory(IV, Block_B, 8);
            NdisMoveMemory(pResult + (Index_i*8), (Block_B + 8), 8);
        } /* End of for */
    } /* End of for */

    /*
     * 3. Output the results
     */
    *PlainTextLength = PlainLength;
    NdisMoveMemory(PlainText, pResult, PlainLength);

/*    kfree(pResult);    
*/
	os_free_mem(NULL, pResult);
    return 0;
} /* End of AES_Key_Unwrap */


