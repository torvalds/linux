// SPDX-License-Identifier: GPL-2.0
/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 ******************************************************************************/

#include "odm_precomp.h"

static const u16 dB_Invert_Table[8][12] = {
	{1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 4, 4},
	{4, 5, 6, 6, 7, 8, 9, 10, 11, 13, 14, 16},
	{18, 20, 22, 25, 28, 32, 35, 40, 45, 50, 56, 63},
	{71, 79, 89, 100, 112, 126, 141, 158, 178, 200, 224, 251},
	{282, 316, 355, 398, 447, 501, 562, 631, 708, 794, 891, 1000},
	{1122, 1259, 1413, 1585, 1778, 1995, 2239, 2512, 2818, 3162, 3548, 3981},
	{4467, 5012, 5623, 6310, 7079, 7943, 8913, 10000, 11220, 12589, 14125,
	 15849},
	{17783, 19953, 22387, 25119, 28184, 31623, 35481, 39811, 44668, 50119,
	 56234, 65535}
 };

/*  Global var */

u32 OFDMSwingTable[OFDM_TABLE_SIZE] = {
	0x7f8001fe, /*  0, +6.0dB */
	0x788001e2, /*  1, +5.5dB */
	0x71c001c7, /*  2, +5.0dB */
	0x6b8001ae, /*  3, +4.5dB */
	0x65400195, /*  4, +4.0dB */
	0x5fc0017f, /*  5, +3.5dB */
	0x5a400169, /*  6, +3.0dB */
	0x55400155, /*  7, +2.5dB */
	0x50800142, /*  8, +2.0dB */
	0x4c000130, /*  9, +1.5dB */
	0x47c0011f, /*  10, +1.0dB */
	0x43c0010f, /*  11, +0.5dB */
	0x40000100, /*  12, +0dB */
	0x3c8000f2, /*  13, -0.5dB */
	0x390000e4, /*  14, -1.0dB */
	0x35c000d7, /*  15, -1.5dB */
	0x32c000cb, /*  16, -2.0dB */
	0x300000c0, /*  17, -2.5dB */
	0x2d4000b5, /*  18, -3.0dB */
	0x2ac000ab, /*  19, -3.5dB */
	0x288000a2, /*  20, -4.0dB */
	0x26000098, /*  21, -4.5dB */
	0x24000090, /*  22, -5.0dB */
	0x22000088, /*  23, -5.5dB */
	0x20000080, /*  24, -6.0dB */
	0x1e400079, /*  25, -6.5dB */
	0x1c800072, /*  26, -7.0dB */
	0x1b00006c, /*  27. -7.5dB */
	0x19800066, /*  28, -8.0dB */
	0x18000060, /*  29, -8.5dB */
	0x16c0005b, /*  30, -9.0dB */
	0x15800056, /*  31, -9.5dB */
	0x14400051, /*  32, -10.0dB */
	0x1300004c, /*  33, -10.5dB */
	0x12000048, /*  34, -11.0dB */
	0x11000044, /*  35, -11.5dB */
	0x10000040, /*  36, -12.0dB */
};

u8 CCKSwingTable_Ch1_Ch13[CCK_TABLE_SIZE][8] = {
	{0x36, 0x35, 0x2e, 0x25, 0x1c, 0x12, 0x09, 0x04}, /*  0, +0dB */
	{0x33, 0x32, 0x2b, 0x23, 0x1a, 0x11, 0x08, 0x04}, /*  1, -0.5dB */
	{0x30, 0x2f, 0x29, 0x21, 0x19, 0x10, 0x08, 0x03}, /*  2, -1.0dB */
	{0x2d, 0x2d, 0x27, 0x1f, 0x18, 0x0f, 0x08, 0x03}, /*  3, -1.5dB */
	{0x2b, 0x2a, 0x25, 0x1e, 0x16, 0x0e, 0x07, 0x03}, /*  4, -2.0dB */
	{0x28, 0x28, 0x22, 0x1c, 0x15, 0x0d, 0x07, 0x03}, /*  5, -2.5dB */
	{0x26, 0x25, 0x21, 0x1b, 0x14, 0x0d, 0x06, 0x03}, /*  6, -3.0dB */
	{0x24, 0x23, 0x1f, 0x19, 0x13, 0x0c, 0x06, 0x03}, /*  7, -3.5dB */
	{0x22, 0x21, 0x1d, 0x18, 0x11, 0x0b, 0x06, 0x02}, /*  8, -4.0dB */
	{0x20, 0x20, 0x1b, 0x16, 0x11, 0x08, 0x05, 0x02}, /*  9, -4.5dB */
	{0x1f, 0x1e, 0x1a, 0x15, 0x10, 0x0a, 0x05, 0x02}, /*  10, -5.0dB */
	{0x1d, 0x1c, 0x18, 0x14, 0x0f, 0x0a, 0x05, 0x02}, /*  11, -5.5dB */
	{0x1b, 0x1a, 0x17, 0x13, 0x0e, 0x09, 0x04, 0x02}, /*  12, -6.0dB <== default */
	{0x1a, 0x19, 0x16, 0x12, 0x0d, 0x09, 0x04, 0x02}, /*  13, -6.5dB */
	{0x18, 0x17, 0x15, 0x11, 0x0c, 0x08, 0x04, 0x02}, /*  14, -7.0dB */
	{0x17, 0x16, 0x13, 0x10, 0x0c, 0x08, 0x04, 0x02}, /*  15, -7.5dB */
	{0x16, 0x15, 0x12, 0x0f, 0x0b, 0x07, 0x04, 0x01}, /*  16, -8.0dB */
	{0x14, 0x14, 0x11, 0x0e, 0x0b, 0x07, 0x03, 0x02}, /*  17, -8.5dB */
	{0x13, 0x13, 0x10, 0x0d, 0x0a, 0x06, 0x03, 0x01}, /*  18, -9.0dB */
	{0x12, 0x12, 0x0f, 0x0c, 0x09, 0x06, 0x03, 0x01}, /*  19, -9.5dB */
	{0x11, 0x11, 0x0f, 0x0c, 0x09, 0x06, 0x03, 0x01}, /*  20, -10.0dB */
	{0x10, 0x10, 0x0e, 0x0b, 0x08, 0x05, 0x03, 0x01}, /*  21, -10.5dB */
	{0x0f, 0x0f, 0x0d, 0x0b, 0x08, 0x05, 0x03, 0x01}, /*  22, -11.0dB */
	{0x0e, 0x0e, 0x0c, 0x0a, 0x08, 0x05, 0x02, 0x01}, /*  23, -11.5dB */
	{0x0d, 0x0d, 0x0c, 0x0a, 0x07, 0x05, 0x02, 0x01}, /*  24, -12.0dB */
	{0x0d, 0x0c, 0x0b, 0x09, 0x07, 0x04, 0x02, 0x01}, /*  25, -12.5dB */
	{0x0c, 0x0c, 0x0a, 0x09, 0x06, 0x04, 0x02, 0x01}, /*  26, -13.0dB */
	{0x0b, 0x0b, 0x0a, 0x08, 0x06, 0x04, 0x02, 0x01}, /*  27, -13.5dB */
	{0x0b, 0x0a, 0x09, 0x08, 0x06, 0x04, 0x02, 0x01}, /*  28, -14.0dB */
	{0x0a, 0x0a, 0x09, 0x07, 0x05, 0x03, 0x02, 0x01}, /*  29, -14.5dB */
	{0x0a, 0x09, 0x08, 0x07, 0x05, 0x03, 0x02, 0x01}, /*  30, -15.0dB */
	{0x09, 0x09, 0x08, 0x06, 0x05, 0x03, 0x01, 0x01}, /*  31, -15.5dB */
	{0x09, 0x08, 0x07, 0x06, 0x04, 0x03, 0x01, 0x01}	/*  32, -16.0dB */
};

u8 CCKSwingTable_Ch14[CCK_TABLE_SIZE][8] = {
	{0x36, 0x35, 0x2e, 0x1b, 0x00, 0x00, 0x00, 0x00}, /*  0, +0dB */
	{0x33, 0x32, 0x2b, 0x19, 0x00, 0x00, 0x00, 0x00}, /*  1, -0.5dB */
	{0x30, 0x2f, 0x29, 0x18, 0x00, 0x00, 0x00, 0x00}, /*  2, -1.0dB */
	{0x2d, 0x2d, 0x17, 0x17, 0x00, 0x00, 0x00, 0x00}, /*  3, -1.5dB */
	{0x2b, 0x2a, 0x25, 0x15, 0x00, 0x00, 0x00, 0x00}, /*  4, -2.0dB */
	{0x28, 0x28, 0x24, 0x14, 0x00, 0x00, 0x00, 0x00}, /*  5, -2.5dB */
	{0x26, 0x25, 0x21, 0x13, 0x00, 0x00, 0x00, 0x00}, /*  6, -3.0dB */
	{0x24, 0x23, 0x1f, 0x12, 0x00, 0x00, 0x00, 0x00}, /*  7, -3.5dB */
	{0x22, 0x21, 0x1d, 0x11, 0x00, 0x00, 0x00, 0x00}, /*  8, -4.0dB */
	{0x20, 0x20, 0x1b, 0x10, 0x00, 0x00, 0x00, 0x00}, /*  9, -4.5dB */
	{0x1f, 0x1e, 0x1a, 0x0f, 0x00, 0x00, 0x00, 0x00}, /*  10, -5.0dB */
	{0x1d, 0x1c, 0x18, 0x0e, 0x00, 0x00, 0x00, 0x00}, /*  11, -5.5dB */
	{0x1b, 0x1a, 0x17, 0x0e, 0x00, 0x00, 0x00, 0x00}, /*  12, -6.0dB  <== default */
	{0x1a, 0x19, 0x16, 0x0d, 0x00, 0x00, 0x00, 0x00}, /*  13, -6.5dB */
	{0x18, 0x17, 0x15, 0x0c, 0x00, 0x00, 0x00, 0x00}, /*  14, -7.0dB */
	{0x17, 0x16, 0x13, 0x0b, 0x00, 0x00, 0x00, 0x00}, /*  15, -7.5dB */
	{0x16, 0x15, 0x12, 0x0b, 0x00, 0x00, 0x00, 0x00}, /*  16, -8.0dB */
	{0x14, 0x14, 0x11, 0x0a, 0x00, 0x00, 0x00, 0x00}, /*  17, -8.5dB */
	{0x13, 0x13, 0x10, 0x0a, 0x00, 0x00, 0x00, 0x00}, /*  18, -9.0dB */
	{0x12, 0x12, 0x0f, 0x09, 0x00, 0x00, 0x00, 0x00}, /*  19, -9.5dB */
	{0x11, 0x11, 0x0f, 0x09, 0x00, 0x00, 0x00, 0x00}, /*  20, -10.0dB */
	{0x10, 0x10, 0x0e, 0x08, 0x00, 0x00, 0x00, 0x00}, /*  21, -10.5dB */
	{0x0f, 0x0f, 0x0d, 0x08, 0x00, 0x00, 0x00, 0x00}, /*  22, -11.0dB */
	{0x0e, 0x0e, 0x0c, 0x07, 0x00, 0x00, 0x00, 0x00}, /*  23, -11.5dB */
	{0x0d, 0x0d, 0x0c, 0x07, 0x00, 0x00, 0x00, 0x00}, /*  24, -12.0dB */
	{0x0d, 0x0c, 0x0b, 0x06, 0x00, 0x00, 0x00, 0x00}, /*  25, -12.5dB */
	{0x0c, 0x0c, 0x0a, 0x06, 0x00, 0x00, 0x00, 0x00}, /*  26, -13.0dB */
	{0x0b, 0x0b, 0x0a, 0x06, 0x00, 0x00, 0x00, 0x00}, /*  27, -13.5dB */
	{0x0b, 0x0a, 0x09, 0x05, 0x00, 0x00, 0x00, 0x00}, /*  28, -14.0dB */
	{0x0a, 0x0a, 0x09, 0x05, 0x00, 0x00, 0x00, 0x00}, /*  29, -14.5dB */
	{0x0a, 0x09, 0x08, 0x05, 0x00, 0x00, 0x00, 0x00}, /*  30, -15.0dB */
	{0x09, 0x09, 0x08, 0x05, 0x00, 0x00, 0x00, 0x00}, /*  31, -15.5dB */
	{0x09, 0x08, 0x07, 0x04, 0x00, 0x00, 0x00, 0x00}	/*  32, -16.0dB */
};

u32 OFDMSwingTable_New[OFDM_TABLE_SIZE] = {
	0x0b40002d, /*  0,  -15.0dB */
	0x0c000030, /*  1,  -14.5dB */
	0x0cc00033, /*  2,  -14.0dB */
	0x0d800036, /*  3,  -13.5dB */
	0x0e400039, /*  4,  -13.0dB */
	0x0f00003c, /*  5,  -12.5dB */
	0x10000040, /*  6,  -12.0dB */
	0x11000044, /*  7,  -11.5dB */
	0x12000048, /*  8,  -11.0dB */
	0x1300004c, /*  9,  -10.5dB */
	0x14400051, /*  10, -10.0dB */
	0x15800056, /*  11, -9.5dB */
	0x16c0005b, /*  12, -9.0dB */
	0x18000060, /*  13, -8.5dB */
	0x19800066, /*  14, -8.0dB */
	0x1b00006c, /*  15, -7.5dB */
	0x1c800072, /*  16, -7.0dB */
	0x1e400079, /*  17, -6.5dB */
	0x20000080, /*  18, -6.0dB */
	0x22000088, /*  19, -5.5dB */
	0x24000090, /*  20, -5.0dB */
	0x26000098, /*  21, -4.5dB */
	0x288000a2, /*  22, -4.0dB */
	0x2ac000ab, /*  23, -3.5dB */
	0x2d4000b5, /*  24, -3.0dB */
	0x300000c0, /*  25, -2.5dB */
	0x32c000cb, /*  26, -2.0dB */
	0x35c000d7, /*  27, -1.5dB */
	0x390000e4, /*  28, -1.0dB */
	0x3c8000f2, /*  29, -0.5dB */
	0x40000100, /*  30, +0dB */
	0x43c0010f, /*  31, +0.5dB */
	0x47c0011f, /*  32, +1.0dB */
	0x4c000130, /*  33, +1.5dB */
	0x50800142, /*  34, +2.0dB */
	0x55400155, /*  35, +2.5dB */
	0x5a400169, /*  36, +3.0dB */
	0x5fc0017f, /*  37, +3.5dB */
	0x65400195, /*  38, +4.0dB */
	0x6b8001ae, /*  39, +4.5dB */
	0x71c001c7, /*  40, +5.0dB */
	0x788001e2, /*  41, +5.5dB */
	0x7f8001fe  /*  42, +6.0dB */
};

u8 CCKSwingTable_Ch1_Ch13_New[CCK_TABLE_SIZE][8] = {
	{0x09, 0x08, 0x07, 0x06, 0x04, 0x03, 0x01, 0x01}, /*   0, -16.0dB */
	{0x09, 0x09, 0x08, 0x06, 0x05, 0x03, 0x01, 0x01}, /*   1, -15.5dB */
	{0x0a, 0x09, 0x08, 0x07, 0x05, 0x03, 0x02, 0x01}, /*   2, -15.0dB */
	{0x0a, 0x0a, 0x09, 0x07, 0x05, 0x03, 0x02, 0x01}, /*   3, -14.5dB */
	{0x0b, 0x0a, 0x09, 0x08, 0x06, 0x04, 0x02, 0x01}, /*   4, -14.0dB */
	{0x0b, 0x0b, 0x0a, 0x08, 0x06, 0x04, 0x02, 0x01}, /*   5, -13.5dB */
	{0x0c, 0x0c, 0x0a, 0x09, 0x06, 0x04, 0x02, 0x01}, /*   6, -13.0dB */
	{0x0d, 0x0c, 0x0b, 0x09, 0x07, 0x04, 0x02, 0x01}, /*   7, -12.5dB */
	{0x0d, 0x0d, 0x0c, 0x0a, 0x07, 0x05, 0x02, 0x01}, /*   8, -12.0dB */
	{0x0e, 0x0e, 0x0c, 0x0a, 0x08, 0x05, 0x02, 0x01}, /*   9, -11.5dB */
	{0x0f, 0x0f, 0x0d, 0x0b, 0x08, 0x05, 0x03, 0x01}, /*  10, -11.0dB */
	{0x10, 0x10, 0x0e, 0x0b, 0x08, 0x05, 0x03, 0x01}, /*  11, -10.5dB */
	{0x11, 0x11, 0x0f, 0x0c, 0x09, 0x06, 0x03, 0x01}, /*  12, -10.0dB */
	{0x12, 0x12, 0x0f, 0x0c, 0x09, 0x06, 0x03, 0x01}, /*  13, -9.5dB */
	{0x13, 0x13, 0x10, 0x0d, 0x0a, 0x06, 0x03, 0x01}, /*  14, -9.0dB */
	{0x14, 0x14, 0x11, 0x0e, 0x0b, 0x07, 0x03, 0x02}, /*  15, -8.5dB */
	{0x16, 0x15, 0x12, 0x0f, 0x0b, 0x07, 0x04, 0x01}, /*  16, -8.0dB */
	{0x17, 0x16, 0x13, 0x10, 0x0c, 0x08, 0x04, 0x02}, /*  17, -7.5dB */
	{0x18, 0x17, 0x15, 0x11, 0x0c, 0x08, 0x04, 0x02}, /*  18, -7.0dB */
	{0x1a, 0x19, 0x16, 0x12, 0x0d, 0x09, 0x04, 0x02}, /*  19, -6.5dB */
	{0x1b, 0x1a, 0x17, 0x13, 0x0e, 0x09, 0x04, 0x02}, /*  20, -6.0dB */
	{0x1d, 0x1c, 0x18, 0x14, 0x0f, 0x0a, 0x05, 0x02}, /*  21, -5.5dB */
	{0x1f, 0x1e, 0x1a, 0x15, 0x10, 0x0a, 0x05, 0x02}, /*  22, -5.0dB */
	{0x20, 0x20, 0x1b, 0x16, 0x11, 0x08, 0x05, 0x02}, /*  23, -4.5dB */
	{0x22, 0x21, 0x1d, 0x18, 0x11, 0x0b, 0x06, 0x02}, /*  24, -4.0dB */
	{0x24, 0x23, 0x1f, 0x19, 0x13, 0x0c, 0x06, 0x03}, /*  25, -3.5dB */
	{0x26, 0x25, 0x21, 0x1b, 0x14, 0x0d, 0x06, 0x03}, /*  26, -3.0dB */
	{0x28, 0x28, 0x22, 0x1c, 0x15, 0x0d, 0x07, 0x03}, /*  27, -2.5dB */
	{0x2b, 0x2a, 0x25, 0x1e, 0x16, 0x0e, 0x07, 0x03}, /*  28, -2.0dB */
	{0x2d, 0x2d, 0x27, 0x1f, 0x18, 0x0f, 0x08, 0x03}, /*  29, -1.5dB */
	{0x30, 0x2f, 0x29, 0x21, 0x19, 0x10, 0x08, 0x03}, /*  30, -1.0dB */
	{0x33, 0x32, 0x2b, 0x23, 0x1a, 0x11, 0x08, 0x04}, /*  31, -0.5dB */
	{0x36, 0x35, 0x2e, 0x25, 0x1c, 0x12, 0x09, 0x04}	/*  32, +0dB */
};

u8 CCKSwingTable_Ch14_New[CCK_TABLE_SIZE][8] = {
	{0x09, 0x08, 0x07, 0x04, 0x00, 0x00, 0x00, 0x00}, /*   0, -16.0dB */
	{0x09, 0x09, 0x08, 0x05, 0x00, 0x00, 0x00, 0x00}, /*   1, -15.5dB */
	{0x0a, 0x09, 0x08, 0x05, 0x00, 0x00, 0x00, 0x00}, /*   2, -15.0dB */
	{0x0a, 0x0a, 0x09, 0x05, 0x00, 0x00, 0x00, 0x00}, /*   3, -14.5dB */
	{0x0b, 0x0a, 0x09, 0x05, 0x00, 0x00, 0x00, 0x00}, /*   4, -14.0dB */
	{0x0b, 0x0b, 0x0a, 0x06, 0x00, 0x00, 0x00, 0x00}, /*   5, -13.5dB */
	{0x0c, 0x0c, 0x0a, 0x06, 0x00, 0x00, 0x00, 0x00}, /*   6, -13.0dB */
	{0x0d, 0x0c, 0x0b, 0x06, 0x00, 0x00, 0x00, 0x00}, /*   7, -12.5dB */
	{0x0d, 0x0d, 0x0c, 0x07, 0x00, 0x00, 0x00, 0x00}, /*   8, -12.0dB */
	{0x0e, 0x0e, 0x0c, 0x07, 0x00, 0x00, 0x00, 0x00}, /*   9, -11.5dB */
	{0x0f, 0x0f, 0x0d, 0x08, 0x00, 0x00, 0x00, 0x00}, /*  10, -11.0dB */
	{0x10, 0x10, 0x0e, 0x08, 0x00, 0x00, 0x00, 0x00}, /*  11, -10.5dB */
	{0x11, 0x11, 0x0f, 0x09, 0x00, 0x00, 0x00, 0x00}, /*  12, -10.0dB */
	{0x12, 0x12, 0x0f, 0x09, 0x00, 0x00, 0x00, 0x00}, /*  13, -9.5dB */
	{0x13, 0x13, 0x10, 0x0a, 0x00, 0x00, 0x00, 0x00}, /*  14, -9.0dB */
	{0x14, 0x14, 0x11, 0x0a, 0x00, 0x00, 0x00, 0x00}, /*  15, -8.5dB */
	{0x16, 0x15, 0x12, 0x0b, 0x00, 0x00, 0x00, 0x00}, /*  16, -8.0dB */
	{0x17, 0x16, 0x13, 0x0b, 0x00, 0x00, 0x00, 0x00}, /*  17, -7.5dB */
	{0x18, 0x17, 0x15, 0x0c, 0x00, 0x00, 0x00, 0x00}, /*  18, -7.0dB */
	{0x1a, 0x19, 0x16, 0x0d, 0x00, 0x00, 0x00, 0x00}, /*  19, -6.5dB */
	{0x1b, 0x1a, 0x17, 0x0e, 0x00, 0x00, 0x00, 0x00}, /*  20, -6.0dB */
	{0x1d, 0x1c, 0x18, 0x0e, 0x00, 0x00, 0x00, 0x00}, /*  21, -5.5dB */
	{0x1f, 0x1e, 0x1a, 0x0f, 0x00, 0x00, 0x00, 0x00}, /*  22, -5.0dB */
	{0x20, 0x20, 0x1b, 0x10, 0x00, 0x00, 0x00, 0x00}, /*  23, -4.5dB */
	{0x22, 0x21, 0x1d, 0x11, 0x00, 0x00, 0x00, 0x00}, /*  24, -4.0dB */
	{0x24, 0x23, 0x1f, 0x12, 0x00, 0x00, 0x00, 0x00}, /*  25, -3.5dB */
	{0x26, 0x25, 0x21, 0x13, 0x00, 0x00, 0x00, 0x00}, /*  26, -3.0dB */
	{0x28, 0x28, 0x24, 0x14, 0x00, 0x00, 0x00, 0x00}, /*  27, -2.5dB */
	{0x2b, 0x2a, 0x25, 0x15, 0x00, 0x00, 0x00, 0x00}, /*  28, -2.0dB */
	{0x2d, 0x2d, 0x17, 0x17, 0x00, 0x00, 0x00, 0x00}, /*  29, -1.5dB */
	{0x30, 0x2f, 0x29, 0x18, 0x00, 0x00, 0x00, 0x00}, /*  30, -1.0dB */
	{0x33, 0x32, 0x2b, 0x19, 0x00, 0x00, 0x00, 0x00}, /*  31, -0.5dB */
	{0x36, 0x35, 0x2e, 0x1b, 0x00, 0x00, 0x00, 0x00}	/*  32, +0dB */
};

u32 TxScalingTable_Jaguar[TXSCALE_TABLE_SIZE] = {
	0x081, /*  0,  -12.0dB */
	0x088, /*  1,  -11.5dB */
	0x090, /*  2,  -11.0dB */
	0x099, /*  3,  -10.5dB */
	0x0A2, /*  4,  -10.0dB */
	0x0AC, /*  5,  -9.5dB */
	0x0B6, /*  6,  -9.0dB */
	0x0C0, /*  7,  -8.5dB */
	0x0CC, /*  8,  -8.0dB */
	0x0D8, /*  9,  -7.5dB */
	0x0E5, /*  10, -7.0dB */
	0x0F2, /*  11, -6.5dB */
	0x101, /*  12, -6.0dB */
	0x110, /*  13, -5.5dB */
	0x120, /*  14, -5.0dB */
	0x131, /*  15, -4.5dB */
	0x143, /*  16, -4.0dB */
	0x156, /*  17, -3.5dB */
	0x16A, /*  18, -3.0dB */
	0x180, /*  19, -2.5dB */
	0x197, /*  20, -2.0dB */
	0x1AF, /*  21, -1.5dB */
	0x1C8, /*  22, -1.0dB */
	0x1E3, /*  23, -0.5dB */
	0x200, /*  24, +0  dB */
	0x21E, /*  25, +0.5dB */
	0x23E, /*  26, +1.0dB */
	0x261, /*  27, +1.5dB */
	0x285, /*  28, +2.0dB */
	0x2AB, /*  29, +2.5dB */
	0x2D3, /*  30, +3.0dB */
	0x2FE, /*  31, +3.5dB */
	0x32B, /*  32, +4.0dB */
	0x35C, /*  33, +4.5dB */
	0x38E, /*  34, +5.0dB */
	0x3C4, /*  35, +5.5dB */
	0x3FE  /*  36, +6.0dB */
};

/*  Local Function predefine. */

/* START------------COMMON INFO RELATED--------------- */
void odm_CommonInfoSelfInit(PDM_ODM_T pDM_Odm);

void odm_CommonInfoSelfUpdate(PDM_ODM_T pDM_Odm);

void odm_CmnInfoInit_Debug(PDM_ODM_T pDM_Odm);

void odm_BasicDbgMessage(PDM_ODM_T pDM_Odm);

/* END------------COMMON INFO RELATED--------------- */

/* START---------------DIG--------------------------- */

/* Remove by Yuchen */

/* END---------------DIG--------------------------- */

/* START-------BB POWER SAVE----------------------- */
/* Remove BB power Saving by YuChen */
/* END---------BB POWER SAVE----------------------- */

void odm_RefreshRateAdaptiveMaskCE(PDM_ODM_T pDM_Odm);

/* Remove by YuChen */

void odm_RSSIMonitorInit(PDM_ODM_T pDM_Odm);

void odm_RSSIMonitorCheckCE(PDM_ODM_T pDM_Odm);

void odm_RSSIMonitorCheck(PDM_ODM_T pDM_Odm);

void odm_SwAntDetectInit(PDM_ODM_T pDM_Odm);

void odm_SwAntDivChkAntSwitchCallback(void *FunctionContext);



void odm_GlobalAdapterCheck(void);

void odm_RefreshRateAdaptiveMask(PDM_ODM_T pDM_Odm);

void ODM_TXPowerTrackingCheck(PDM_ODM_T pDM_Odm);

void odm_RateAdaptiveMaskInit(PDM_ODM_T pDM_Odm);


void odm_TXPowerTrackingInit(PDM_ODM_T pDM_Odm);

/* Remove Edca by Yu Chen */


#define RxDefaultAnt1		0x65a9
#define RxDefaultAnt2		0x569a

void odm_InitHybridAntDiv(PDM_ODM_T pDM_Odm);

bool odm_StaDefAntSel(
	PDM_ODM_T pDM_Odm,
	u32 OFDM_Ant1_Cnt,
	u32 OFDM_Ant2_Cnt,
	u32 CCK_Ant1_Cnt,
	u32 CCK_Ant2_Cnt,
	u8 *pDefAnt
);

void odm_SetRxIdleAnt(PDM_ODM_T pDM_Odm, u8 Ant, bool bDualPath);



void odm_HwAntDiv(PDM_ODM_T pDM_Odm);


/*  */
/* 3 Export Interface */
/*  */

/*  */
/*  2011/09/21 MH Add to describe different team necessary resource allocate?? */
/*  */
void ODM_DMInit(PDM_ODM_T pDM_Odm)
{

	odm_CommonInfoSelfInit(pDM_Odm);
	odm_CmnInfoInit_Debug(pDM_Odm);
	odm_DIGInit(pDM_Odm);
	odm_NHMCounterStatisticsInit(pDM_Odm);
	odm_AdaptivityInit(pDM_Odm);
	odm_RateAdaptiveMaskInit(pDM_Odm);
	ODM_CfoTrackingInit(pDM_Odm);
	ODM_EdcaTurboInit(pDM_Odm);
	odm_RSSIMonitorInit(pDM_Odm);
	odm_TXPowerTrackingInit(pDM_Odm);

	ODM_ClearTxPowerTrackingState(pDM_Odm);

	if (*(pDM_Odm->mp_mode) != 1)
		odm_PathDiversityInit(pDM_Odm);

	odm_DynamicBBPowerSavingInit(pDM_Odm);
	odm_DynamicTxPowerInit(pDM_Odm);

	odm_SwAntDetectInit(pDM_Odm);
}

/*  */
/*  2011/09/20 MH This is the entry pointer for all team to execute HW out source DM. */
/*  You can not add any dummy function here, be care, you can only use DM structure */
/*  to perform any new ODM_DM. */
/*  */
void ODM_DMWatchdog(PDM_ODM_T pDM_Odm)
{
	odm_CommonInfoSelfUpdate(pDM_Odm);
	odm_BasicDbgMessage(pDM_Odm);
	odm_FalseAlarmCounterStatistics(pDM_Odm);
	odm_NHMCounterStatistics(pDM_Odm);
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_DIG(): RSSI = 0x%x\n", pDM_Odm->RSSI_Min));

	odm_RSSIMonitorCheck(pDM_Odm);

	/* For CE Platform(SPRD or Tablet) */
	/* 8723A or 8189ES platform */
	/* NeilChen--2012--08--24-- */
	/* Fix Leave LPS issue */
	if ((adapter_to_pwrctl(pDM_Odm->Adapter)->pwr_mode != PS_MODE_ACTIVE) /*  in LPS mode */
		/*  */
		/* (pDM_Odm->SupportICType & (ODM_RTL8723A))|| */
		/* (pDM_Odm->SupportICType & (ODM_RTL8188E) &&(&&(((pDM_Odm->SupportInterface  == ODM_ITRF_SDIO))) */
		/*  */
	) {
			ODM_RT_TRACE(pDM_Odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("----Step1: odm_DIG is in LPS mode\n"));
			ODM_RT_TRACE(pDM_Odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("---Step2: 8723AS is in LPS mode\n"));
			odm_DIGbyRSSI_LPS(pDM_Odm);
	} else
		odm_DIG(pDM_Odm);

	{
		pDIG_T pDM_DigTable = &pDM_Odm->DM_DigTable;

		odm_Adaptivity(pDM_Odm, pDM_DigTable->CurIGValue);
	}
	odm_CCKPacketDetectionThresh(pDM_Odm);

	if (*(pDM_Odm->pbPowerSaving) == true)
		return;


	odm_RefreshRateAdaptiveMask(pDM_Odm);
	odm_EdcaTurboCheck(pDM_Odm);
	odm_PathDiversity(pDM_Odm);
	ODM_CfoTracking(pDM_Odm);

	ODM_TXPowerTrackingCheck(pDM_Odm);

	/* odm_EdcaTurboCheck(pDM_Odm); */

	/* 2010.05.30 LukeLee: For CE platform, files in IC subfolders may not be included to be compiled, */
	/*  so compile flags must be left here to prevent from compile errors */
	pDM_Odm->PhyDbgInfo.NumQryBeaconPkt = 0;
}


/*  */
/*  Init /.. Fixed HW value. Only init time. */
/*  */
void ODM_CmnInfoInit(PDM_ODM_T pDM_Odm, ODM_CMNINFO_E CmnInfo, u32 Value)
{
	/*  */
	/*  This section is used for init value */
	/*  */
	switch (CmnInfo) {
	/*  */
	/*  Fixed ODM value. */
	/*  */
	case ODM_CMNINFO_ABILITY:
		pDM_Odm->SupportAbility = (u32)Value;
		break;

	case ODM_CMNINFO_RF_TYPE:
		pDM_Odm->RFType = (u8)Value;
		break;

	case ODM_CMNINFO_PLATFORM:
		pDM_Odm->SupportPlatform = (u8)Value;
		break;

	case ODM_CMNINFO_INTERFACE:
		pDM_Odm->SupportInterface = (u8)Value;
		break;

	case ODM_CMNINFO_MP_TEST_CHIP:
		pDM_Odm->bIsMPChip = (u8)Value;
		break;

	case ODM_CMNINFO_IC_TYPE:
		pDM_Odm->SupportICType = Value;
		break;

	case ODM_CMNINFO_CUT_VER:
		pDM_Odm->CutVersion = (u8)Value;
		break;

	case ODM_CMNINFO_FAB_VER:
		pDM_Odm->FabVersion = (u8)Value;
		break;

	case ODM_CMNINFO_RFE_TYPE:
		pDM_Odm->RFEType = (u8)Value;
		break;

	case    ODM_CMNINFO_RF_ANTENNA_TYPE:
		pDM_Odm->AntDivType = (u8)Value;
		break;

	case ODM_CMNINFO_BOARD_TYPE:
		pDM_Odm->BoardType = (u8)Value;
		break;

	case ODM_CMNINFO_PACKAGE_TYPE:
		pDM_Odm->PackageType = (u8)Value;
		break;

	case ODM_CMNINFO_EXT_LNA:
		pDM_Odm->ExtLNA = (u8)Value;
		break;

	case ODM_CMNINFO_5G_EXT_LNA:
		pDM_Odm->ExtLNA5G = (u8)Value;
		break;

	case ODM_CMNINFO_EXT_PA:
		pDM_Odm->ExtPA = (u8)Value;
		break;

	case ODM_CMNINFO_5G_EXT_PA:
		pDM_Odm->ExtPA5G = (u8)Value;
		break;

	case ODM_CMNINFO_GPA:
		pDM_Odm->TypeGPA = (ODM_TYPE_GPA_E)Value;
		break;
	case ODM_CMNINFO_APA:
		pDM_Odm->TypeAPA = (ODM_TYPE_APA_E)Value;
		break;
	case ODM_CMNINFO_GLNA:
		pDM_Odm->TypeGLNA = (ODM_TYPE_GLNA_E)Value;
		break;
	case ODM_CMNINFO_ALNA:
		pDM_Odm->TypeALNA = (ODM_TYPE_ALNA_E)Value;
		break;

	case ODM_CMNINFO_EXT_TRSW:
		pDM_Odm->ExtTRSW = (u8)Value;
		break;
	case ODM_CMNINFO_PATCH_ID:
		pDM_Odm->PatchID = (u8)Value;
		break;
	case ODM_CMNINFO_BINHCT_TEST:
		pDM_Odm->bInHctTest = (bool)Value;
		break;
	case ODM_CMNINFO_BWIFI_TEST:
		pDM_Odm->bWIFITest = (bool)Value;
		break;

	case ODM_CMNINFO_SMART_CONCURRENT:
		pDM_Odm->bDualMacSmartConcurrent = (bool)Value;
		break;

	/* To remove the compiler warning, must add an empty default statement to handle the other values. */
	default:
		/* do nothing */
		break;
	}

}


void ODM_CmnInfoHook(PDM_ODM_T pDM_Odm, ODM_CMNINFO_E CmnInfo, void *pValue)
{
	/*  */
	/*  Hook call by reference pointer. */
	/*  */
	switch (CmnInfo) {
	/*  */
	/*  Dynamic call by reference pointer. */
	/*  */
	case ODM_CMNINFO_MAC_PHY_MODE:
		pDM_Odm->pMacPhyMode = pValue;
		break;

	case ODM_CMNINFO_TX_UNI:
		pDM_Odm->pNumTxBytesUnicast = pValue;
		break;

	case ODM_CMNINFO_RX_UNI:
		pDM_Odm->pNumRxBytesUnicast = pValue;
		break;

	case ODM_CMNINFO_WM_MODE:
		pDM_Odm->pwirelessmode = pValue;
		break;

	case ODM_CMNINFO_BAND:
		pDM_Odm->pBandType = pValue;
		break;

	case ODM_CMNINFO_SEC_CHNL_OFFSET:
		pDM_Odm->pSecChOffset = pValue;
		break;

	case ODM_CMNINFO_SEC_MODE:
		pDM_Odm->pSecurity = pValue;
		break;

	case ODM_CMNINFO_BW:
		pDM_Odm->pBandWidth = pValue;
		break;

	case ODM_CMNINFO_CHNL:
		pDM_Odm->pChannel = pValue;
		break;

	case ODM_CMNINFO_DMSP_GET_VALUE:
		pDM_Odm->pbGetValueFromOtherMac = pValue;
		break;

	case ODM_CMNINFO_BUDDY_ADAPTOR:
		pDM_Odm->pBuddyAdapter = pValue;
		break;

	case ODM_CMNINFO_DMSP_IS_MASTER:
		pDM_Odm->pbMasterOfDMSP = pValue;
		break;

	case ODM_CMNINFO_SCAN:
		pDM_Odm->pbScanInProcess = pValue;
		break;

	case ODM_CMNINFO_POWER_SAVING:
		pDM_Odm->pbPowerSaving = pValue;
		break;

	case ODM_CMNINFO_ONE_PATH_CCA:
		pDM_Odm->pOnePathCCA = pValue;
		break;

	case ODM_CMNINFO_DRV_STOP:
		pDM_Odm->pbDriverStopped =  pValue;
		break;

	case ODM_CMNINFO_PNP_IN:
		pDM_Odm->pbDriverIsGoingToPnpSetPowerSleep =  pValue;
		break;

	case ODM_CMNINFO_INIT_ON:
		pDM_Odm->pinit_adpt_in_progress =  pValue;
		break;

	case ODM_CMNINFO_ANT_TEST:
		pDM_Odm->pAntennaTest =  pValue;
		break;

	case ODM_CMNINFO_NET_CLOSED:
		pDM_Odm->pbNet_closed = pValue;
		break;

	case ODM_CMNINFO_FORCED_RATE:
		pDM_Odm->pForcedDataRate = pValue;
		break;

	case ODM_CMNINFO_FORCED_IGI_LB:
		pDM_Odm->pu1ForcedIgiLb = pValue;
		break;

	case ODM_CMNINFO_MP_MODE:
		pDM_Odm->mp_mode = pValue;
		break;

	/* case ODM_CMNINFO_RTSTA_AID: */
	/* pDM_Odm->pAidMap =  (u8 *)pValue; */
	/* break; */

	/* case ODM_CMNINFO_BT_COEXIST: */
	/* pDM_Odm->BTCoexist = (bool *)pValue; */

	/* case ODM_CMNINFO_STA_STATUS: */
	/* pDM_Odm->pODM_StaInfo[] = (PSTA_INFO_T)pValue; */
	/* break; */

	/* case ODM_CMNINFO_PHY_STATUS: */
	/* pDM_Odm->pPhyInfo = (ODM_PHY_INFO *)pValue; */
	/* break; */

	/* case ODM_CMNINFO_MAC_STATUS: */
	/* pDM_Odm->pMacInfo = (struct odm_mac_status_info *)pValue; */
	/* break; */
	/* To remove the compiler warning, must add an empty default statement to handle the other values. */
	default:
		/* do nothing */
		break;
	}

}


void ODM_CmnInfoPtrArrayHook(
	PDM_ODM_T pDM_Odm,
	ODM_CMNINFO_E CmnInfo,
	u16 Index,
	void *pValue
)
{
	/*  */
	/*  Hook call by reference pointer. */
	/*  */
	switch (CmnInfo) {
	/*  */
	/*  Dynamic call by reference pointer. */
	/*  */
	case ODM_CMNINFO_STA_STATUS:
		pDM_Odm->pODM_StaInfo[Index] = (PSTA_INFO_T)pValue;
		break;
	/* To remove the compiler warning, must add an empty default statement to handle the other values. */
	default:
		/* do nothing */
		break;
	}

}


/*  */
/*  Update Band/CHannel/.. The values are dynamic but non-per-packet. */
/*  */
void ODM_CmnInfoUpdate(PDM_ODM_T pDM_Odm, u32 CmnInfo, u64 Value)
{
	/*  */
	/*  This init variable may be changed in run time. */
	/*  */
	switch (CmnInfo) {
	case ODM_CMNINFO_LINK_IN_PROGRESS:
		pDM_Odm->bLinkInProcess = (bool)Value;
		break;

	case ODM_CMNINFO_ABILITY:
		pDM_Odm->SupportAbility = (u32)Value;
		break;

	case ODM_CMNINFO_RF_TYPE:
		pDM_Odm->RFType = (u8)Value;
		break;

	case ODM_CMNINFO_WIFI_DIRECT:
		pDM_Odm->bWIFI_Direct = (bool)Value;
		break;

	case ODM_CMNINFO_WIFI_DISPLAY:
		pDM_Odm->bWIFI_Display = (bool)Value;
		break;

	case ODM_CMNINFO_LINK:
		pDM_Odm->bLinked = (bool)Value;
		break;

	case ODM_CMNINFO_STATION_STATE:
		pDM_Odm->bsta_state = (bool)Value;
		break;

	case ODM_CMNINFO_RSSI_MIN:
		pDM_Odm->RSSI_Min = (u8)Value;
		break;

	case ODM_CMNINFO_DBG_COMP:
		pDM_Odm->DebugComponents = Value;
		break;

	case ODM_CMNINFO_DBG_LEVEL:
		pDM_Odm->DebugLevel = (u32)Value;
		break;
	case ODM_CMNINFO_RA_THRESHOLD_HIGH:
		pDM_Odm->RateAdaptive.HighRSSIThresh = (u8)Value;
		break;

	case ODM_CMNINFO_RA_THRESHOLD_LOW:
		pDM_Odm->RateAdaptive.LowRSSIThresh = (u8)Value;
		break;
	/*  The following is for BT HS mode and BT coexist mechanism. */
	case ODM_CMNINFO_BT_ENABLED:
		pDM_Odm->bBtEnabled = (bool)Value;
		break;

	case ODM_CMNINFO_BT_HS_CONNECT_PROCESS:
		pDM_Odm->bBtConnectProcess = (bool)Value;
		break;

	case ODM_CMNINFO_BT_HS_RSSI:
		pDM_Odm->btHsRssi = (u8)Value;
		break;

	case ODM_CMNINFO_BT_OPERATION:
		pDM_Odm->bBtHsOperation = (bool)Value;
		break;

	case ODM_CMNINFO_BT_LIMITED_DIG:
		pDM_Odm->bBtLimitedDig = (bool)Value;
		break;

	case ODM_CMNINFO_BT_DISABLE_EDCA:
		pDM_Odm->bBtDisableEdcaTurbo = (bool)Value;
		break;

/*
	case	ODM_CMNINFO_OP_MODE:
		pDM_Odm->OPMode = (u8)Value;
		break;

	case	ODM_CMNINFO_WM_MODE:
		pDM_Odm->WirelessMode = (u8)Value;
		break;

	case	ODM_CMNINFO_BAND:
		pDM_Odm->BandType = (u8)Value;
		break;

	case	ODM_CMNINFO_SEC_CHNL_OFFSET:
		pDM_Odm->SecChOffset = (u8)Value;
		break;

	case	ODM_CMNINFO_SEC_MODE:
		pDM_Odm->Security = (u8)Value;
		break;

	case	ODM_CMNINFO_BW:
		pDM_Odm->BandWidth = (u8)Value;
		break;

	case	ODM_CMNINFO_CHNL:
		pDM_Odm->Channel = (u8)Value;
		break;
*/
	default:
		/* do nothing */
		break;
	}


}

void odm_CommonInfoSelfInit(PDM_ODM_T pDM_Odm)
{
	pDM_Odm->bCckHighPower = (bool) PHY_QueryBBReg(pDM_Odm->Adapter, ODM_REG(CCK_RPT_FORMAT, pDM_Odm), ODM_BIT(CCK_RPT_FORMAT, pDM_Odm));
	pDM_Odm->RFPathRxEnable = (u8) PHY_QueryBBReg(pDM_Odm->Adapter, ODM_REG(BB_RX_PATH, pDM_Odm), ODM_BIT(BB_RX_PATH, pDM_Odm));

	ODM_InitDebugSetting(pDM_Odm);

	pDM_Odm->TxRate = 0xFF;
}

void odm_CommonInfoSelfUpdate(PDM_ODM_T pDM_Odm)
{
	u8 EntryCnt = 0;
	u8 i;
	PSTA_INFO_T	pEntry;

	if (*(pDM_Odm->pBandWidth) == ODM_BW40M) {
		if (*(pDM_Odm->pSecChOffset) == 1)
			pDM_Odm->ControlChannel = *(pDM_Odm->pChannel)-2;
		else if (*(pDM_Odm->pSecChOffset) == 2)
			pDM_Odm->ControlChannel = *(pDM_Odm->pChannel)+2;
	} else
		pDM_Odm->ControlChannel = *(pDM_Odm->pChannel);

	for (i = 0; i < ODM_ASSOCIATE_ENTRY_NUM; i++) {
		pEntry = pDM_Odm->pODM_StaInfo[i];
		if (IS_STA_VALID(pEntry))
			EntryCnt++;
	}

	if (EntryCnt == 1)
		pDM_Odm->bOneEntryOnly = true;
	else
		pDM_Odm->bOneEntryOnly = false;
}

void odm_CmnInfoInit_Debug(PDM_ODM_T pDM_Odm)
{
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_COMMON, ODM_DBG_LOUD, ("odm_CmnInfoInit_Debug ==>\n"));
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_COMMON, ODM_DBG_LOUD, ("SupportPlatform =%d\n", pDM_Odm->SupportPlatform));
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_COMMON, ODM_DBG_LOUD, ("SupportAbility = 0x%x\n", pDM_Odm->SupportAbility));
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_COMMON, ODM_DBG_LOUD, ("SupportInterface =%d\n", pDM_Odm->SupportInterface));
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_COMMON, ODM_DBG_LOUD, ("SupportICType = 0x%x\n", pDM_Odm->SupportICType));
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_COMMON, ODM_DBG_LOUD, ("CutVersion =%d\n", pDM_Odm->CutVersion));
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_COMMON, ODM_DBG_LOUD, ("FabVersion =%d\n", pDM_Odm->FabVersion));
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_COMMON, ODM_DBG_LOUD, ("RFType =%d\n", pDM_Odm->RFType));
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_COMMON, ODM_DBG_LOUD, ("BoardType =%d\n", pDM_Odm->BoardType));
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_COMMON, ODM_DBG_LOUD, ("ExtLNA =%d\n", pDM_Odm->ExtLNA));
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_COMMON, ODM_DBG_LOUD, ("ExtPA =%d\n", pDM_Odm->ExtPA));
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_COMMON, ODM_DBG_LOUD, ("ExtTRSW =%d\n", pDM_Odm->ExtTRSW));
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_COMMON, ODM_DBG_LOUD, ("PatchID =%d\n", pDM_Odm->PatchID));
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_COMMON, ODM_DBG_LOUD, ("bInHctTest =%d\n", pDM_Odm->bInHctTest));
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_COMMON, ODM_DBG_LOUD, ("bWIFITest =%d\n", pDM_Odm->bWIFITest));
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_COMMON, ODM_DBG_LOUD, ("bDualMacSmartConcurrent =%d\n", pDM_Odm->bDualMacSmartConcurrent));

}

void odm_BasicDbgMessage(PDM_ODM_T pDM_Odm)
{
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_COMMON, ODM_DBG_LOUD, ("odm_BasicDbgMsg ==>\n"));
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_COMMON, ODM_DBG_LOUD, ("bLinked = %d, RSSI_Min = %d,\n",
		pDM_Odm->bLinked, pDM_Odm->RSSI_Min));
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_COMMON, ODM_DBG_LOUD, ("RxRate = 0x%x, RSSI_A = %d, RSSI_B = %d\n",
		pDM_Odm->RxRate, pDM_Odm->RSSI_A, pDM_Odm->RSSI_B));
}

/* 3 ============================================================ */
/* 3 DIG */
/* 3 ============================================================ */
/*-----------------------------------------------------------------------------
 * Function:	odm_DIGInit()
 *
 * Overview:	Set DIG scheme init value.
 *
 * Input:		NONE
 *
 * Output:		NONE
 *
 * Return:		NONE
 *
 * Revised History:
 *When		Who		Remark
 *
 *---------------------------------------------------------------------------
 */

/* Remove DIG by yuchen */

/* Remove DIG and FA check by Yu Chen */


/* 3 ============================================================ */
/* 3 BB Power Save */
/* 3 ============================================================ */

/* Remove BB power saving by Yuchen */

/* 3 ============================================================ */
/* 3 RATR MASK */
/* 3 ============================================================ */
/* 3 ============================================================ */
/* 3 Rate Adaptive */
/* 3 ============================================================ */

void odm_RateAdaptiveMaskInit(PDM_ODM_T pDM_Odm)
{
	PODM_RATE_ADAPTIVE pOdmRA = &pDM_Odm->RateAdaptive;

	pOdmRA->Type = DM_Type_ByDriver;
	if (pOdmRA->Type == DM_Type_ByDriver)
		pDM_Odm->bUseRAMask = true;
	else
		pDM_Odm->bUseRAMask = false;

	pOdmRA->RATRState = DM_RATR_STA_INIT;
	pOdmRA->LdpcThres = 35;
	pOdmRA->bUseLdpc = false;
	pOdmRA->HighRSSIThresh = 50;
	pOdmRA->LowRSSIThresh = 20;
}

u32 ODM_Get_Rate_Bitmap(
	PDM_ODM_T pDM_Odm,
	u32 macid,
	u32 ra_mask,
	u8 rssi_level
)
{
	PSTA_INFO_T	pEntry;
	u32 rate_bitmap = 0;
	u8 WirelessMode;

	pEntry = pDM_Odm->pODM_StaInfo[macid];
	if (!IS_STA_VALID(pEntry))
		return ra_mask;

	WirelessMode = pEntry->wireless_mode;

	switch (WirelessMode) {
	case ODM_WM_B:
		if (ra_mask & 0x0000000c)		/* 11M or 5.5M enable */
			rate_bitmap = 0x0000000d;
		else
			rate_bitmap = 0x0000000f;
		break;

	case (ODM_WM_G):
	case (ODM_WM_A):
		if (rssi_level == DM_RATR_STA_HIGH)
			rate_bitmap = 0x00000f00;
		else
			rate_bitmap = 0x00000ff0;
		break;

	case (ODM_WM_B|ODM_WM_G):
		if (rssi_level == DM_RATR_STA_HIGH)
			rate_bitmap = 0x00000f00;
		else if (rssi_level == DM_RATR_STA_MIDDLE)
			rate_bitmap = 0x00000ff0;
		else
			rate_bitmap = 0x00000ff5;
		break;

	case (ODM_WM_B|ODM_WM_G|ODM_WM_N24G):
	case (ODM_WM_B|ODM_WM_N24G):
	case (ODM_WM_G|ODM_WM_N24G):
	case (ODM_WM_A|ODM_WM_N5G):
		if (pDM_Odm->RFType == ODM_1T2R || pDM_Odm->RFType == ODM_1T1R) {
			if (rssi_level == DM_RATR_STA_HIGH)
				rate_bitmap = 0x000f0000;
			else if (rssi_level == DM_RATR_STA_MIDDLE)
				rate_bitmap = 0x000ff000;
			else {
				if (*(pDM_Odm->pBandWidth) == ODM_BW40M)
					rate_bitmap = 0x000ff015;
				else
					rate_bitmap = 0x000ff005;
			}
		} else {
			if (rssi_level == DM_RATR_STA_HIGH)
				rate_bitmap = 0x0f8f0000;
			else if (rssi_level == DM_RATR_STA_MIDDLE)
				rate_bitmap = 0x0f8ff000;
			else {
				if (*(pDM_Odm->pBandWidth) == ODM_BW40M)
					rate_bitmap = 0x0f8ff015;
				else
					rate_bitmap = 0x0f8ff005;
			}
		}
		break;

	case (ODM_WM_AC|ODM_WM_G):
		if (rssi_level == 1)
			rate_bitmap = 0xfc3f0000;
		else if (rssi_level == 2)
			rate_bitmap = 0xfffff000;
		else
			rate_bitmap = 0xffffffff;
		break;

	case (ODM_WM_AC|ODM_WM_A):

		if (pDM_Odm->RFType == RF_1T1R) {
			if (rssi_level == 1)				/*  add by Gary for ac-series */
				rate_bitmap = 0x003f8000;
			else if (rssi_level == 2)
				rate_bitmap = 0x003ff000;
			else
				rate_bitmap = 0x003ff010;
		} else {
			if (rssi_level == 1)				/*  add by Gary for ac-series */
				rate_bitmap = 0xfe3f8000;       /*  VHT 2SS MCS3~9 */
			else if (rssi_level == 2)
				rate_bitmap = 0xfffff000;       /*  VHT 2SS MCS0~9 */
			else
				rate_bitmap = 0xfffff010;       /*  All */
		}
		break;

	default:
		if (pDM_Odm->RFType == RF_1T2R)
			rate_bitmap = 0x000fffff;
		else
			rate_bitmap = 0x0fffffff;
		break;
	}

	/* printk("%s ==> rssi_level:0x%02x, WirelessMode:0x%02x, rate_bitmap:0x%08x\n", __func__, rssi_level, WirelessMode, rate_bitmap); */
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_RA_MASK, ODM_DBG_LOUD, (" ==> rssi_level:0x%02x, WirelessMode:0x%02x, rate_bitmap:0x%08x\n", rssi_level, WirelessMode, rate_bitmap));

	return ra_mask & rate_bitmap;

}

/*-----------------------------------------------------------------------------
* Function:	odm_RefreshRateAdaptiveMask()
*
* Overview:	Update rate table mask according to rssi
*
* Input:		NONE
*
* Output:		NONE
*
* Return:		NONE
*
* Revised History:
*When		Who		Remark
*05/27/2009	hpfan	Create Version 0.
*
* --------------------------------------------------------------------------
*/
void odm_RefreshRateAdaptiveMask(PDM_ODM_T pDM_Odm)
{

	ODM_RT_TRACE(pDM_Odm, ODM_COMP_RA_MASK, ODM_DBG_TRACE, ("odm_RefreshRateAdaptiveMask()---------->\n"));
	if (!(pDM_Odm->SupportAbility & ODM_BB_RA_MASK)) {
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_RA_MASK, ODM_DBG_TRACE, ("odm_RefreshRateAdaptiveMask(): Return cos not supported\n"));
		return;
	}
	odm_RefreshRateAdaptiveMaskCE(pDM_Odm);
}

void odm_RefreshRateAdaptiveMaskCE(PDM_ODM_T pDM_Odm)
{
	u8 i;
	struct adapter *padapter =  pDM_Odm->Adapter;

	if (padapter->bDriverStopped) {
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_RA_MASK, ODM_DBG_TRACE, ("<---- odm_RefreshRateAdaptiveMask(): driver is going to unload\n"));
		return;
	}

	if (!pDM_Odm->bUseRAMask) {
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_RA_MASK, ODM_DBG_LOUD, ("<---- odm_RefreshRateAdaptiveMask(): driver does not control rate adaptive mask\n"));
		return;
	}

	/* printk("==> %s\n", __func__); */

	for (i = 0; i < ODM_ASSOCIATE_ENTRY_NUM; i++) {
		PSTA_INFO_T pstat = pDM_Odm->pODM_StaInfo[i];

		if (IS_STA_VALID(pstat)) {
			if (IS_MCAST(pstat->hwaddr))  /* if (psta->mac_id == 1) */
				continue;
			if (IS_MCAST(pstat->hwaddr))
				continue;

			if (true == ODM_RAStateCheck(pDM_Odm, pstat->rssi_stat.UndecoratedSmoothedPWDB, false, &pstat->rssi_level)) {
				ODM_RT_TRACE(pDM_Odm, ODM_COMP_RA_MASK, ODM_DBG_LOUD, ("RSSI:%d, RSSI_LEVEL:%d\n", pstat->rssi_stat.UndecoratedSmoothedPWDB, pstat->rssi_level));
				/* printk("RSSI:%d, RSSI_LEVEL:%d\n", pstat->rssi_stat.UndecoratedSmoothedPWDB, pstat->rssi_level); */
				rtw_hal_update_ra_mask(pstat, pstat->rssi_level);
			}

		}
	}
}

/*  Return Value: bool */
/*  - true: RATRState is changed. */
bool ODM_RAStateCheck(
	PDM_ODM_T pDM_Odm,
	s32 RSSI,
	bool bForceUpdate,
	u8 *pRATRState
)
{
	PODM_RATE_ADAPTIVE pRA = &pDM_Odm->RateAdaptive;
	const u8 GoUpGap = 5;
	u8 HighRSSIThreshForRA = pRA->HighRSSIThresh;
	u8 LowRSSIThreshForRA = pRA->LowRSSIThresh;
	u8 RATRState;

	/*  Threshold Adjustment: */
	/*  when RSSI state trends to go up one or two levels, make sure RSSI is high enough. */
	/*  Here GoUpGap is added to solve the boundary's level alternation issue. */
	switch (*pRATRState) {
	case DM_RATR_STA_INIT:
	case DM_RATR_STA_HIGH:
		break;

	case DM_RATR_STA_MIDDLE:
		HighRSSIThreshForRA += GoUpGap;
		break;

	case DM_RATR_STA_LOW:
		HighRSSIThreshForRA += GoUpGap;
		LowRSSIThreshForRA += GoUpGap;
		break;

	default:
		ODM_RT_ASSERT(pDM_Odm, false, ("wrong rssi level setting %d !", *pRATRState));
		break;
	}

	/*  Decide RATRState by RSSI. */
	if (RSSI > HighRSSIThreshForRA)
		RATRState = DM_RATR_STA_HIGH;
	else if (RSSI > LowRSSIThreshForRA)
		RATRState = DM_RATR_STA_MIDDLE;
	else
		RATRState = DM_RATR_STA_LOW;
	/* printk("==>%s, RATRState:0x%02x , RSSI:%d\n", __func__, RATRState, RSSI); */

	if (*pRATRState != RATRState || bForceUpdate) {
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_RA_MASK, ODM_DBG_LOUD, ("RSSI Level %d -> %d\n", *pRATRState, RATRState));
		*pRATRState = RATRState;
		return true;
	}

	return false;
}


/*  */

/* 3 ============================================================ */
/* 3 Dynamic Tx Power */
/* 3 ============================================================ */

/* Remove BY YuChen */

/* 3 ============================================================ */
/* 3 RSSI Monitor */
/* 3 ============================================================ */

void odm_RSSIMonitorInit(PDM_ODM_T pDM_Odm)
{
	pRA_T pRA_Table = &pDM_Odm->DM_RA_Table;

	pRA_Table->firstconnect = false;

}

void odm_RSSIMonitorCheck(PDM_ODM_T pDM_Odm)
{
	if (!(pDM_Odm->SupportAbility & ODM_BB_RSSI_MONITOR))
		return;

	odm_RSSIMonitorCheckCE(pDM_Odm);

}	/*  odm_RSSIMonitorCheck */

static void FindMinimumRSSI(struct adapter *padapter)
{
	struct hal_com_data	*pHalData = GET_HAL_DATA(padapter);
	struct dm_priv *pdmpriv = &pHalData->dmpriv;
	PDM_ODM_T pDM_Odm = &(pHalData->odmpriv);

	/* 1 1.Determine the minimum RSSI */

	if (
		(pDM_Odm->bLinked != true) &&
		(pdmpriv->EntryMinUndecoratedSmoothedPWDB == 0)
	) {
		pdmpriv->MinUndecoratedPWDBForDM = 0;
		/* ODM_RT_TRACE(pDM_Odm, COMP_BB_POWERSAVING, DBG_LOUD, ("Not connected to any\n")); */
	} else
		pdmpriv->MinUndecoratedPWDBForDM = pdmpriv->EntryMinUndecoratedSmoothedPWDB;

	/* DBG_8192C("%s =>MinUndecoratedPWDBForDM(%d)\n", __func__, pdmpriv->MinUndecoratedPWDBForDM); */
	/* ODM_RT_TRACE(pDM_Odm, COMP_DIG, DBG_LOUD, ("MinUndecoratedPWDBForDM =%d\n", pHalData->MinUndecoratedPWDBForDM)); */
}

void odm_RSSIMonitorCheckCE(PDM_ODM_T pDM_Odm)
{
	struct adapter *Adapter = pDM_Odm->Adapter;
	struct hal_com_data	*pHalData = GET_HAL_DATA(Adapter);
	struct dm_priv *pdmpriv = &pHalData->dmpriv;
	int i;
	int tmpEntryMaxPWDB = 0, tmpEntryMinPWDB = 0xff;
	u8 sta_cnt = 0;
	u32 PWDB_rssi[NUM_STA] = {0};/* 0~15]:MACID, [16~31]:PWDB_rssi */
	bool FirstConnect = false;
	pRA_T pRA_Table = &pDM_Odm->DM_RA_Table;

	if (pDM_Odm->bLinked != true)
		return;

	FirstConnect = (pDM_Odm->bLinked) && (pRA_Table->firstconnect == false);
	pRA_Table->firstconnect = pDM_Odm->bLinked;

	/* if (check_fwstate(&Adapter->mlmepriv, WIFI_AP_STATE|WIFI_ADHOC_STATE|WIFI_ADHOC_MASTER_STATE) == true) */
	{
		struct sta_info *psta;

		for (i = 0; i < ODM_ASSOCIATE_ENTRY_NUM; i++) {
			psta = pDM_Odm->pODM_StaInfo[i];
			if (IS_STA_VALID(psta)) {
				if (IS_MCAST(psta->hwaddr))  /* if (psta->mac_id == 1) */
					continue;

				if (psta->rssi_stat.UndecoratedSmoothedPWDB == (-1))
					continue;

				if (psta->rssi_stat.UndecoratedSmoothedPWDB < tmpEntryMinPWDB)
					tmpEntryMinPWDB = psta->rssi_stat.UndecoratedSmoothedPWDB;

				if (psta->rssi_stat.UndecoratedSmoothedPWDB > tmpEntryMaxPWDB)
					tmpEntryMaxPWDB = psta->rssi_stat.UndecoratedSmoothedPWDB;

				if (psta->rssi_stat.UndecoratedSmoothedPWDB != (-1))
					PWDB_rssi[sta_cnt++] = (psta->mac_id | (psta->rssi_stat.UndecoratedSmoothedPWDB<<16));
			}
		}

		/* printk("%s ==> sta_cnt(%d)\n", __func__, sta_cnt); */

		for (i = 0; i < sta_cnt; i++) {
			if (PWDB_rssi[i] != (0)) {
				if (pHalData->fw_ractrl == true)/*  Report every sta's RSSI to FW */
					rtl8723b_set_rssi_cmd(Adapter, (u8 *)(&PWDB_rssi[i]));
			}
		}
	}



	if (tmpEntryMaxPWDB != 0)	/*  If associated entry is found */
		pdmpriv->EntryMaxUndecoratedSmoothedPWDB = tmpEntryMaxPWDB;
	else
		pdmpriv->EntryMaxUndecoratedSmoothedPWDB = 0;

	if (tmpEntryMinPWDB != 0xff) /*  If associated entry is found */
		pdmpriv->EntryMinUndecoratedSmoothedPWDB = tmpEntryMinPWDB;
	else
		pdmpriv->EntryMinUndecoratedSmoothedPWDB = 0;

	FindMinimumRSSI(Adapter);/* get pdmpriv->MinUndecoratedPWDBForDM */

	pDM_Odm->RSSI_Min = pdmpriv->MinUndecoratedPWDBForDM;
	/* ODM_CmnInfoUpdate(&pHalData->odmpriv , ODM_CMNINFO_RSSI_MIN, pdmpriv->MinUndecoratedPWDBForDM); */
}

/* 3 ============================================================ */
/* 3 Tx Power Tracking */
/* 3 ============================================================ */

static u8 getSwingIndex(PDM_ODM_T pDM_Odm)
{
	struct adapter *Adapter = pDM_Odm->Adapter;
	u8 i = 0;
	u32 bbSwing;
	u32 swingTableSize;
	u32 *pSwingTable;

	bbSwing = PHY_QueryBBReg(Adapter, rOFDM0_XATxIQImbalance, 0xFFC00000);

	pSwingTable = OFDMSwingTable_New;
	swingTableSize = OFDM_TABLE_SIZE;

	for (i = 0; i < swingTableSize; ++i) {
		u32 tableValue = pSwingTable[i];

		if (tableValue >= 0x100000)
			tableValue >>= 22;
		if (bbSwing == tableValue)
			break;
	}
	return i;
}

void odm_TXPowerTrackingInit(PDM_ODM_T pDM_Odm)
{
	u8 defaultSwingIndex = getSwingIndex(pDM_Odm);
	u8 p = 0;
	struct adapter *Adapter = pDM_Odm->Adapter;
	struct hal_com_data *pHalData = GET_HAL_DATA(Adapter);


	struct dm_priv *pdmpriv = &pHalData->dmpriv;

	pdmpriv->bTXPowerTracking = true;
	pdmpriv->TXPowercount = 0;
	pdmpriv->bTXPowerTrackingInit = false;

	if (*(pDM_Odm->mp_mode) != 1)
		pdmpriv->TxPowerTrackControl = true;
	else
		pdmpriv->TxPowerTrackControl = false;


	/* MSG_8192C("pdmpriv->TxPowerTrackControl = %d\n", pdmpriv->TxPowerTrackControl); */

	/* pDM_Odm->RFCalibrateInfo.TxPowerTrackControl = true; */
	pDM_Odm->RFCalibrateInfo.ThermalValue = pHalData->EEPROMThermalMeter;
	pDM_Odm->RFCalibrateInfo.ThermalValue_IQK = pHalData->EEPROMThermalMeter;
	pDM_Odm->RFCalibrateInfo.ThermalValue_LCK = pHalData->EEPROMThermalMeter;

	/*  The index of "0 dB" in SwingTable. */
	pDM_Odm->DefaultOfdmIndex = (defaultSwingIndex >= OFDM_TABLE_SIZE) ? 30 : defaultSwingIndex;
	pDM_Odm->DefaultCckIndex = 20;

	pDM_Odm->BbSwingIdxCckBase = pDM_Odm->DefaultCckIndex;
	pDM_Odm->RFCalibrateInfo.CCK_index = pDM_Odm->DefaultCckIndex;

	for (p = ODM_RF_PATH_A; p < MAX_RF_PATH; ++p) {
		pDM_Odm->BbSwingIdxOfdmBase[p] = pDM_Odm->DefaultOfdmIndex;
		pDM_Odm->RFCalibrateInfo.OFDM_index[p] = pDM_Odm->DefaultOfdmIndex;
		pDM_Odm->RFCalibrateInfo.DeltaPowerIndex[p] = 0;
		pDM_Odm->RFCalibrateInfo.DeltaPowerIndexLast[p] = 0;
		pDM_Odm->RFCalibrateInfo.PowerIndexOffset[p] = 0;
	}

}

void ODM_TXPowerTrackingCheck(PDM_ODM_T pDM_Odm)
{
	struct adapter *Adapter = pDM_Odm->Adapter;

	if (!(pDM_Odm->SupportAbility & ODM_RF_TX_PWR_TRACK))
		return;

	if (!pDM_Odm->RFCalibrateInfo.TM_Trigger) { /* at least delay 1 sec */
		PHY_SetRFReg(pDM_Odm->Adapter, ODM_RF_PATH_A, RF_T_METER_NEW, (BIT17 | BIT16), 0x03);

		/* DBG_871X("Trigger Thermal Meter!!\n"); */

		pDM_Odm->RFCalibrateInfo.TM_Trigger = 1;
		return;
	} else {
		/* DBG_871X("Schedule TxPowerTracking direct call!!\n"); */
		ODM_TXPowerTrackingCallback_ThermalMeter(Adapter);
		pDM_Odm->RFCalibrateInfo.TM_Trigger = 0;
	}
}

/* 3 ============================================================ */
/* 3 SW Antenna Diversity */
/* 3 ============================================================ */
void odm_SwAntDetectInit(PDM_ODM_T pDM_Odm)
{
	pSWAT_T pDM_SWAT_Table = &pDM_Odm->DM_SWAT_Table;

	pDM_SWAT_Table->SWAS_NoLink_BK_Reg92c = rtw_read32(pDM_Odm->Adapter, rDPDT_control);
	pDM_SWAT_Table->PreAntenna = MAIN_ANT;
	pDM_SWAT_Table->CurAntenna = MAIN_ANT;
	pDM_SWAT_Table->SWAS_NoLink_State = 0;
}
