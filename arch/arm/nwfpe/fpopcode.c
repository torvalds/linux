/*
    NetWinder Floating Point Emulator
    (c) Rebel.COM, 1998,1999

    Direct questions, comments to Scott Bambrough <scottb@netwinder.org>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#include <linux/config.h>
#include "fpa11.h"
#include "softfloat.h"
#include "fpopcode.h"
#include "fpsr.h"
#include "fpmodule.h"
#include "fpmodule.inl"

#ifdef CONFIG_FPE_NWFPE_XP
const floatx80 floatx80Constant[] = {
	{0x0000, 0x0000000000000000ULL},	/* extended 0.0 */
	{0x3fff, 0x8000000000000000ULL},	/* extended 1.0 */
	{0x4000, 0x8000000000000000ULL},	/* extended 2.0 */
	{0x4000, 0xc000000000000000ULL},	/* extended 3.0 */
	{0x4001, 0x8000000000000000ULL},	/* extended 4.0 */
	{0x4001, 0xa000000000000000ULL},	/* extended 5.0 */
	{0x3ffe, 0x8000000000000000ULL},	/* extended 0.5 */
	{0x4002, 0xa000000000000000ULL}		/* extended 10.0 */
};
#endif

const float64 float64Constant[] = {
	0x0000000000000000ULL,	/* double 0.0 */
	0x3ff0000000000000ULL,	/* double 1.0 */
	0x4000000000000000ULL,	/* double 2.0 */
	0x4008000000000000ULL,	/* double 3.0 */
	0x4010000000000000ULL,	/* double 4.0 */
	0x4014000000000000ULL,	/* double 5.0 */
	0x3fe0000000000000ULL,	/* double 0.5 */
	0x4024000000000000ULL	/* double 10.0 */
};

const float32 float32Constant[] = {
	0x00000000,		/* single 0.0 */
	0x3f800000,		/* single 1.0 */
	0x40000000,		/* single 2.0 */
	0x40400000,		/* single 3.0 */
	0x40800000,		/* single 4.0 */
	0x40a00000,		/* single 5.0 */
	0x3f000000,		/* single 0.5 */
	0x41200000		/* single 10.0 */
};

/* condition code lookup table
 index into the table is test code: EQ, NE, ... LT, GT, AL, NV
 bit position in short is condition code: NZCV */
static const unsigned short aCC[16] = {
	0xF0F0,			// EQ == Z set
	0x0F0F,			// NE
	0xCCCC,			// CS == C set
	0x3333,			// CC
	0xFF00,			// MI == N set
	0x00FF,			// PL
	0xAAAA,			// VS == V set
	0x5555,			// VC
	0x0C0C,			// HI == C set && Z clear
	0xF3F3,			// LS == C clear || Z set
	0xAA55,			// GE == (N==V)
	0x55AA,			// LT == (N!=V)
	0x0A05,			// GT == (!Z && (N==V))
	0xF5FA,			// LE == (Z || (N!=V))
	0xFFFF,			// AL always
	0			// NV
};

unsigned int checkCondition(const unsigned int opcode, const unsigned int ccodes)
{
	return (aCC[opcode >> 28] >> (ccodes >> 28)) & 1;
}
