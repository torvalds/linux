/*
 * Copyright (c) 1998-2006 The TCPDUMP project
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that: (1) source code
 * distributions retain the above copyright notice and this paragraph
 * in its entirety, and (2) distributions including binary code include
 * the above copyright notice and this paragraph in its entirety in
 * the documentation or other materials provided with the distribution.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND
 * WITHOUT ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, WITHOUT
 * LIMITATION, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE.
 *
 * miscellaneous checksumming routines
 *
 * Original code by Hannes Gredler (hannes@gredler.at)
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <netdissect-stdinc.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "netdissect.h"

/*
 * CRC-10 table generated using the following Python snippet:

import sys

crc_table = []
for i in range(256):
	accum = i << 2
	for j in range(8):
		accum <<= 1
		if accum & 0x400:
			accum ^= 0x633
	crc_table.append(accum)

for i in range(len(crc_table)/8):
	for j in range(8):
		sys.stdout.write("0x%04x, " % crc_table[i*8+j])
	sys.stdout.write("\n")

 */
static const uint16_t crc10_table[256] =
{
	0x0000, 0x0233, 0x0255, 0x0066, 0x0299, 0x00aa, 0x00cc, 0x02ff,
	0x0301, 0x0132, 0x0154, 0x0367, 0x0198, 0x03ab, 0x03cd, 0x01fe,
	0x0031, 0x0202, 0x0264, 0x0057, 0x02a8, 0x009b, 0x00fd, 0x02ce,
	0x0330, 0x0103, 0x0165, 0x0356, 0x01a9, 0x039a, 0x03fc, 0x01cf,
	0x0062, 0x0251, 0x0237, 0x0004, 0x02fb, 0x00c8, 0x00ae, 0x029d,
	0x0363, 0x0150, 0x0136, 0x0305, 0x01fa, 0x03c9, 0x03af, 0x019c,
	0x0053, 0x0260, 0x0206, 0x0035, 0x02ca, 0x00f9, 0x009f, 0x02ac,
	0x0352, 0x0161, 0x0107, 0x0334, 0x01cb, 0x03f8, 0x039e, 0x01ad,
	0x00c4, 0x02f7, 0x0291, 0x00a2, 0x025d, 0x006e, 0x0008, 0x023b,
	0x03c5, 0x01f6, 0x0190, 0x03a3, 0x015c, 0x036f, 0x0309, 0x013a,
	0x00f5, 0x02c6, 0x02a0, 0x0093, 0x026c, 0x005f, 0x0039, 0x020a,
	0x03f4, 0x01c7, 0x01a1, 0x0392, 0x016d, 0x035e, 0x0338, 0x010b,
	0x00a6, 0x0295, 0x02f3, 0x00c0, 0x023f, 0x000c, 0x006a, 0x0259,
	0x03a7, 0x0194, 0x01f2, 0x03c1, 0x013e, 0x030d, 0x036b, 0x0158,
	0x0097, 0x02a4, 0x02c2, 0x00f1, 0x020e, 0x003d, 0x005b, 0x0268,
	0x0396, 0x01a5, 0x01c3, 0x03f0, 0x010f, 0x033c, 0x035a, 0x0169,
	0x0188, 0x03bb, 0x03dd, 0x01ee, 0x0311, 0x0122, 0x0144, 0x0377,
	0x0289, 0x00ba, 0x00dc, 0x02ef, 0x0010, 0x0223, 0x0245, 0x0076,
	0x01b9, 0x038a, 0x03ec, 0x01df, 0x0320, 0x0113, 0x0175, 0x0346,
	0x02b8, 0x008b, 0x00ed, 0x02de, 0x0021, 0x0212, 0x0274, 0x0047,
	0x01ea, 0x03d9, 0x03bf, 0x018c, 0x0373, 0x0140, 0x0126, 0x0315,
	0x02eb, 0x00d8, 0x00be, 0x028d, 0x0072, 0x0241, 0x0227, 0x0014,
	0x01db, 0x03e8, 0x038e, 0x01bd, 0x0342, 0x0171, 0x0117, 0x0324,
	0x02da, 0x00e9, 0x008f, 0x02bc, 0x0043, 0x0270, 0x0216, 0x0025,
	0x014c, 0x037f, 0x0319, 0x012a, 0x03d5, 0x01e6, 0x0180, 0x03b3,
	0x024d, 0x007e, 0x0018, 0x022b, 0x00d4, 0x02e7, 0x0281, 0x00b2,
	0x017d, 0x034e, 0x0328, 0x011b, 0x03e4, 0x01d7, 0x01b1, 0x0382,
	0x027c, 0x004f, 0x0029, 0x021a, 0x00e5, 0x02d6, 0x02b0, 0x0083,
	0x012e, 0x031d, 0x037b, 0x0148, 0x03b7, 0x0184, 0x01e2, 0x03d1,
	0x022f, 0x001c, 0x007a, 0x0249, 0x00b6, 0x0285, 0x02e3, 0x00d0,
	0x011f, 0x032c, 0x034a, 0x0179, 0x0386, 0x01b5, 0x01d3, 0x03e0,
	0x021e, 0x002d, 0x004b, 0x0278, 0x0087, 0x02b4, 0x02d2, 0x00e1
};

static void
init_crc10_table(void)
{
#define CRC10_POLYNOMIAL 0x633
    register int i, j;
    register uint16_t accum;
    uint16_t verify_crc10_table[256];

    for ( i = 0;  i < 256;  i++ )
    {
        accum = ((unsigned short) i << 2);
        for ( j = 0;  j < 8;  j++ )
        {
            if ((accum <<= 1) & 0x400) accum ^= CRC10_POLYNOMIAL;
        }
        verify_crc10_table[i] = accum;
    }
    assert(memcmp(verify_crc10_table,
				  crc10_table,
				  sizeof(verify_crc10_table)) == 0);
#undef CRC10_POLYNOMIAL
}

uint16_t
verify_crc10_cksum(uint16_t accum, const u_char *p, int length)
{
    register int i;

    for ( i = 0;  i < length;  i++ )
    {
        accum = ((accum << 8) & 0x3ff)
            ^ crc10_table[( accum >> 2) & 0xff]
            ^ *p++;
    }
    return accum;
}

/* precompute checksum tables */
void
init_checksum(void) {

    init_crc10_table();

}

/*
 * Creates the OSI Fletcher checksum. See 8473-1, Appendix C, section C.3.
 * The checksum field of the passed PDU does not need to be reset to zero.
 */
uint16_t
create_osi_cksum (const uint8_t *pptr, int checksum_offset, int length)
{

    int x;
    int y;
    uint32_t mul;
    uint32_t c0;
    uint32_t c1;
    uint16_t checksum;
    int idx;

    c0 = 0;
    c1 = 0;

    for (idx = 0; idx < length; idx++) {
        /*
         * Ignore the contents of the checksum field.
         */
        if (idx == checksum_offset ||
            idx == checksum_offset+1) {
            c1 += c0;
            pptr++;
        } else {
            c0 = c0 + *(pptr++);
            c1 += c0;
        }
    }

    c0 = c0 % 255;
    c1 = c1 % 255;

    mul = (length - checksum_offset)*(c0);

    x = mul - c0 - c1;
    y = c1 - mul - 1;

    if ( y >= 0 ) y++;
    if ( x < 0 ) x--;

    x %= 255;
    y %= 255;


    if (x == 0) x = 255;
    if (y == 0) y = 255;

    y &= 0x00FF;
    checksum = ((x << 8) | y);

    return checksum;
}
