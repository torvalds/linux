/*
    NetWinder Floating Point Emulator
    (c) Rebel.COM, 1998,1999
    (c) Philip Blundell, 2001

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

#ifndef __FPOPCODE_H__
#define __FPOPCODE_H__


/*
ARM Floating Point Instruction Classes
| | | | | | | | | | | | | | | | | | | | | | | | | | | | | | | | | 
|c o n d|1 1 0 P|U|u|W|L|   Rn  |v|  Fd |0|0|0|1|  o f f s e t  | CPDT
|c o n d|1 1 0 P|U|w|W|L|   Rn  |x|  Fd |0|0|1|0|  o f f s e t  | CPDT (copro 2)
| | | | | | | | | | | | | | | | | | | | | | | | | | | | | | | | | 
|c o n d|1 1 1 0|a|b|c|d|e|  Fn |j|  Fd |0|0|0|1|f|g|h|0|i|  Fm | CPDO
|c o n d|1 1 1 0|a|b|c|L|e|  Fn |   Rd  |0|0|0|1|f|g|h|1|i|  Fm | CPRT
|c o n d|1 1 1 0|a|b|c|1|e|  Fn |1|1|1|1|0|0|0|1|f|g|h|1|i|  Fm | comparisons
| | | | | | | | | | | | | | | | | | | | | | | | | | | | | | | | | 

CPDT		data transfer instructions
		LDF, STF, LFM (copro 2), SFM (copro 2)
		
CPDO		dyadic arithmetic instructions
		ADF, MUF, SUF, RSF, DVF, RDF,
		POW, RPW, RMF, FML, FDV, FRD, POL

CPDO		monadic arithmetic instructions
		MVF, MNF, ABS, RND, SQT, LOG, LGN, EXP,
		SIN, COS, TAN, ASN, ACS, ATN, URD, NRM
		
CPRT		joint arithmetic/data transfer instructions
		FIX (arithmetic followed by load/store)
		FLT (load/store followed by arithmetic)
		CMF, CNF CMFE, CNFE (comparisons)
		WFS, RFS (write/read floating point status register)
		WFC, RFC (write/read floating point control register)

cond		condition codes
P		pre/post index bit: 0 = postindex, 1 = preindex
U		up/down bit: 0 = stack grows down, 1 = stack grows up
W		write back bit: 1 = update base register (Rn)
L		load/store bit: 0 = store, 1 = load
Rn		base register
Rd		destination/source register		
Fd		floating point destination register
Fn		floating point source register
Fm		floating point source register or floating point constant

uv		transfer length (TABLE 1)
wx		register count (TABLE 2)
abcd		arithmetic opcode (TABLES 3 & 4)
ef		destination size (rounding precision) (TABLE 5)
gh		rounding mode (TABLE 6)
j		dyadic/monadic bit: 0 = dyadic, 1 = monadic
i 		constant bit: 1 = constant (TABLE 6)
*/

/*
TABLE 1
+-------------------------+---+---+---------+---------+
|  Precision              | u | v | FPSR.EP | length  |
+-------------------------+---+---+---------+---------+
| Single                  | 0 ü 0 |    x    | 1 words |
| Double                  | 1 ü 1 |    x    | 2 words |
| Extended                | 1 ü 1 |    x    | 3 words |
| Packed decimal          | 1 ü 1 |    0    | 3 words |
| Expanded packed decimal | 1 ü 1 |    1    | 4 words |
+-------------------------+---+---+---------+---------+
Note: x = don't care
*/

/*
TABLE 2
+---+---+---------------------------------+
| w | x | Number of registers to transfer |
+---+---+---------------------------------+
| 0 ü 1 |  1                              |
| 1 ü 0 |  2                              |
| 1 ü 1 |  3                              |
| 0 ü 0 |  4                              |
+---+---+---------------------------------+
*/

/*
TABLE 3: Dyadic Floating Point Opcodes
+---+---+---+---+----------+-----------------------+-----------------------+
| a | b | c | d | Mnemonic | Description           | Operation             |
+---+---+---+---+----------+-----------------------+-----------------------+
| 0 | 0 | 0 | 0 | ADF      | Add                   | Fd := Fn + Fm         |
| 0 | 0 | 0 | 1 | MUF      | Multiply              | Fd := Fn * Fm         |
| 0 | 0 | 1 | 0 | SUF      | Subtract              | Fd := Fn - Fm         |
| 0 | 0 | 1 | 1 | RSF      | Reverse subtract      | Fd := Fm - Fn         |
| 0 | 1 | 0 | 0 | DVF      | Divide                | Fd := Fn / Fm         |
| 0 | 1 | 0 | 1 | RDF      | Reverse divide        | Fd := Fm / Fn         |
| 0 | 1 | 1 | 0 | POW      | Power                 | Fd := Fn ^ Fm         |
| 0 | 1 | 1 | 1 | RPW      | Reverse power         | Fd := Fm ^ Fn         |
| 1 | 0 | 0 | 0 | RMF      | Remainder             | Fd := IEEE rem(Fn/Fm) |
| 1 | 0 | 0 | 1 | FML      | Fast Multiply         | Fd := Fn * Fm         |
| 1 | 0 | 1 | 0 | FDV      | Fast Divide           | Fd := Fn / Fm         |
| 1 | 0 | 1 | 1 | FRD      | Fast reverse divide   | Fd := Fm / Fn         |
| 1 | 1 | 0 | 0 | POL      | Polar angle (ArcTan2) | Fd := arctan2(Fn,Fm)  |
| 1 | 1 | 0 | 1 |          | undefined instruction | trap                  |
| 1 | 1 | 1 | 0 |          | undefined instruction | trap                  |
| 1 | 1 | 1 | 1 |          | undefined instruction | trap                  |
+---+---+---+---+----------+-----------------------+-----------------------+
Note: POW, RPW, POL are deprecated, and are available for backwards
      compatibility only.
*/

/*
TABLE 4: Monadic Floating Point Opcodes
+---+---+---+---+----------+-----------------------+-----------------------+
| a | b | c | d | Mnemonic | Description           | Operation             |
+---+---+---+---+----------+-----------------------+-----------------------+
| 0 | 0 | 0 | 0 | MVF      | Move                  | Fd := Fm              |
| 0 | 0 | 0 | 1 | MNF      | Move negated          | Fd := - Fm            |
| 0 | 0 | 1 | 0 | ABS      | Absolute value        | Fd := abs(Fm)         |
| 0 | 0 | 1 | 1 | RND      | Round to integer      | Fd := int(Fm)         |
| 0 | 1 | 0 | 0 | SQT      | Square root           | Fd := sqrt(Fm)        |
| 0 | 1 | 0 | 1 | LOG      | Log base 10           | Fd := log10(Fm)       |
| 0 | 1 | 1 | 0 | LGN      | Log base e            | Fd := ln(Fm)          |
| 0 | 1 | 1 | 1 | EXP      | Exponent              | Fd := e ^ Fm          |
| 1 | 0 | 0 | 0 | SIN      | Sine                  | Fd := sin(Fm)         |
| 1 | 0 | 0 | 1 | COS      | Cosine                | Fd := cos(Fm)         |
| 1 | 0 | 1 | 0 | TAN      | Tangent               | Fd := tan(Fm)         |
| 1 | 0 | 1 | 1 | ASN      | Arc Sine              | Fd := arcsin(Fm)      |
| 1 | 1 | 0 | 0 | ACS      | Arc Cosine            | Fd := arccos(Fm)      |
| 1 | 1 | 0 | 1 | ATN      | Arc Tangent           | Fd := arctan(Fm)      |
| 1 | 1 | 1 | 0 | URD      | Unnormalized round    | Fd := int(Fm)         |
| 1 | 1 | 1 | 1 | NRM      | Normalize             | Fd := norm(Fm)        |
+---+---+---+---+----------+-----------------------+-----------------------+
Note: LOG, LGN, EXP, SIN, COS, TAN, ASN, ACS, ATN are deprecated, and are
      available for backwards compatibility only.
*/

/*
TABLE 5
+-------------------------+---+---+
|  Rounding Precision     | e | f |
+-------------------------+---+---+
| IEEE Single precision   | 0 ü 0 |
| IEEE Double precision   | 0 ü 1 |
| IEEE Extended precision | 1 ü 0 |
| undefined (trap)        | 1 ü 1 |
+-------------------------+---+---+
*/

/*
TABLE 5
+---------------------------------+---+---+
|  Rounding Mode                  | g | h |
+---------------------------------+---+---+
| Round to nearest (default)      | 0 ü 0 |
| Round toward plus infinity      | 0 ü 1 |
| Round toward negative infinity  | 1 ü 0 |
| Round toward zero               | 1 ü 1 |
+---------------------------------+---+---+
*/

/*
===
=== Definitions for load and store instructions
===
*/

/* bit masks */
#define BIT_PREINDEX	0x01000000
#define BIT_UP		0x00800000
#define BIT_WRITE_BACK	0x00200000
#define BIT_LOAD	0x00100000

/* masks for load/store */
#define MASK_CPDT		0x0c000000	/* data processing opcode */
#define MASK_OFFSET		0x000000ff
#define MASK_TRANSFER_LENGTH	0x00408000
#define MASK_REGISTER_COUNT	MASK_TRANSFER_LENGTH
#define MASK_COPROCESSOR	0x00000f00

/* Tests for transfer length */
#define TRANSFER_SINGLE		0x00000000
#define TRANSFER_DOUBLE		0x00008000
#define TRANSFER_EXTENDED	0x00400000
#define TRANSFER_PACKED		MASK_TRANSFER_LENGTH

/* Get the coprocessor number from the opcode. */
#define getCoprocessorNumber(opcode)	((opcode & MASK_COPROCESSOR) >> 8)

/* Get the offset from the opcode. */
#define getOffset(opcode)		(opcode & MASK_OFFSET)

/* Tests for specific data transfer load/store opcodes. */
#define TEST_OPCODE(opcode,mask)	(((opcode) & (mask)) == (mask))

#define LOAD_OP(opcode)   TEST_OPCODE((opcode),MASK_CPDT | BIT_LOAD)
#define STORE_OP(opcode)  ((opcode & (MASK_CPDT | BIT_LOAD)) == MASK_CPDT)

#define LDF_OP(opcode)	(LOAD_OP(opcode) && (getCoprocessorNumber(opcode) == 1))
#define LFM_OP(opcode)	(LOAD_OP(opcode) && (getCoprocessorNumber(opcode) == 2))
#define STF_OP(opcode)	(STORE_OP(opcode) && (getCoprocessorNumber(opcode) == 1))
#define SFM_OP(opcode)	(STORE_OP(opcode) && (getCoprocessorNumber(opcode) == 2))

#define PREINDEXED(opcode)		((opcode & BIT_PREINDEX) != 0)
#define POSTINDEXED(opcode)		((opcode & BIT_PREINDEX) == 0)
#define BIT_UP_SET(opcode)		((opcode & BIT_UP) != 0)
#define BIT_UP_CLEAR(opcode)		((opcode & BIT_DOWN) == 0)
#define WRITE_BACK(opcode)		((opcode & BIT_WRITE_BACK) != 0)
#define LOAD(opcode)			((opcode & BIT_LOAD) != 0)
#define STORE(opcode)			((opcode & BIT_LOAD) == 0)

/*
===
=== Definitions for arithmetic instructions
===
*/
/* bit masks */
#define BIT_MONADIC	0x00008000
#define BIT_CONSTANT	0x00000008

#define CONSTANT_FM(opcode)		((opcode & BIT_CONSTANT) != 0)
#define MONADIC_INSTRUCTION(opcode)	((opcode & BIT_MONADIC) != 0)

/* instruction identification masks */
#define MASK_CPDO		0x0e000000	/* arithmetic opcode */
#define MASK_ARITHMETIC_OPCODE	0x00f08000
#define MASK_DESTINATION_SIZE	0x00080080

/* dyadic arithmetic opcodes. */
#define ADF_CODE	0x00000000
#define MUF_CODE	0x00100000
#define SUF_CODE	0x00200000
#define RSF_CODE	0x00300000
#define DVF_CODE	0x00400000
#define RDF_CODE	0x00500000
#define POW_CODE	0x00600000
#define RPW_CODE	0x00700000
#define RMF_CODE	0x00800000
#define FML_CODE	0x00900000
#define FDV_CODE	0x00a00000
#define FRD_CODE	0x00b00000
#define POL_CODE	0x00c00000
/* 0x00d00000 is an invalid dyadic arithmetic opcode */
/* 0x00e00000 is an invalid dyadic arithmetic opcode */
/* 0x00f00000 is an invalid dyadic arithmetic opcode */

/* monadic arithmetic opcodes. */
#define MVF_CODE	0x00008000
#define MNF_CODE	0x00108000
#define ABS_CODE	0x00208000
#define RND_CODE	0x00308000
#define SQT_CODE	0x00408000
#define LOG_CODE	0x00508000
#define LGN_CODE	0x00608000
#define EXP_CODE	0x00708000
#define SIN_CODE	0x00808000
#define COS_CODE	0x00908000
#define TAN_CODE	0x00a08000
#define ASN_CODE	0x00b08000
#define ACS_CODE	0x00c08000
#define ATN_CODE	0x00d08000
#define URD_CODE	0x00e08000
#define NRM_CODE	0x00f08000

/*
===
=== Definitions for register transfer and comparison instructions
===
*/

#define MASK_CPRT		0x0e000010	/* register transfer opcode */
#define MASK_CPRT_CODE		0x00f00000
#define FLT_CODE		0x00000000
#define FIX_CODE		0x00100000
#define WFS_CODE		0x00200000
#define RFS_CODE		0x00300000
#define WFC_CODE		0x00400000
#define RFC_CODE		0x00500000
#define CMF_CODE		0x00900000
#define CNF_CODE		0x00b00000
#define CMFE_CODE		0x00d00000
#define CNFE_CODE		0x00f00000

/*
===
=== Common definitions
===
*/

/* register masks */
#define MASK_Rd		0x0000f000
#define MASK_Rn		0x000f0000
#define MASK_Fd		0x00007000
#define MASK_Fm		0x00000007
#define MASK_Fn		0x00070000

/* condition code masks */
#define CC_MASK		0xf0000000
#define CC_NEGATIVE	0x80000000
#define CC_ZERO		0x40000000
#define CC_CARRY	0x20000000
#define CC_OVERFLOW	0x10000000
#define CC_EQ		0x00000000
#define CC_NE		0x10000000
#define CC_CS		0x20000000
#define CC_HS		CC_CS
#define CC_CC		0x30000000
#define CC_LO		CC_CC
#define CC_MI		0x40000000
#define CC_PL		0x50000000
#define CC_VS		0x60000000
#define CC_VC		0x70000000
#define CC_HI		0x80000000
#define CC_LS		0x90000000
#define CC_GE		0xa0000000
#define CC_LT		0xb0000000
#define CC_GT		0xc0000000
#define CC_LE		0xd0000000
#define CC_AL		0xe0000000
#define CC_NV		0xf0000000

/* rounding masks/values */
#define MASK_ROUNDING_MODE	0x00000060
#define ROUND_TO_NEAREST	0x00000000
#define ROUND_TO_PLUS_INFINITY	0x00000020
#define ROUND_TO_MINUS_INFINITY	0x00000040
#define ROUND_TO_ZERO		0x00000060

#define MASK_ROUNDING_PRECISION	0x00080080
#define ROUND_SINGLE		0x00000000
#define ROUND_DOUBLE		0x00000080
#define ROUND_EXTENDED		0x00080000

/* Get the condition code from the opcode. */
#define getCondition(opcode)		(opcode >> 28)

/* Get the source register from the opcode. */
#define getRn(opcode)			((opcode & MASK_Rn) >> 16)

/* Get the destination floating point register from the opcode. */
#define getFd(opcode)			((opcode & MASK_Fd) >> 12)

/* Get the first source floating point register from the opcode. */
#define getFn(opcode)		((opcode & MASK_Fn) >> 16)

/* Get the second source floating point register from the opcode. */
#define getFm(opcode)		(opcode & MASK_Fm)

/* Get the destination register from the opcode. */
#define getRd(opcode)		((opcode & MASK_Rd) >> 12)

/* Get the rounding mode from the opcode. */
#define getRoundingMode(opcode)		((opcode & MASK_ROUNDING_MODE) >> 5)

#ifdef CONFIG_FPE_NWFPE_XP
static inline __attribute_pure__ floatx80 getExtendedConstant(const unsigned int nIndex)
{
	extern const floatx80 floatx80Constant[];
	return floatx80Constant[nIndex];
}
#endif

static inline __attribute_pure__ float64 getDoubleConstant(const unsigned int nIndex)
{
	extern const float64 float64Constant[];
	return float64Constant[nIndex];
}

static inline __attribute_pure__ float32 getSingleConstant(const unsigned int nIndex)
{
	extern const float32 float32Constant[];
	return float32Constant[nIndex];
}

static inline unsigned int getTransferLength(const unsigned int opcode)
{
	unsigned int nRc;

	switch (opcode & MASK_TRANSFER_LENGTH) {
	case 0x00000000:
		nRc = 1;
		break;		/* single precision */
	case 0x00008000:
		nRc = 2;
		break;		/* double precision */
	case 0x00400000:
		nRc = 3;
		break;		/* extended precision */
	default:
		nRc = 0;
	}

	return (nRc);
}

static inline unsigned int getRegisterCount(const unsigned int opcode)
{
	unsigned int nRc;

	switch (opcode & MASK_REGISTER_COUNT) {
	case 0x00000000:
		nRc = 4;
		break;
	case 0x00008000:
		nRc = 1;
		break;
	case 0x00400000:
		nRc = 2;
		break;
	case 0x00408000:
		nRc = 3;
		break;
	default:
		nRc = 0;
	}

	return (nRc);
}

static inline unsigned int getRoundingPrecision(const unsigned int opcode)
{
	unsigned int nRc;

	switch (opcode & MASK_ROUNDING_PRECISION) {
	case 0x00000000:
		nRc = 1;
		break;
	case 0x00000080:
		nRc = 2;
		break;
	case 0x00080000:
		nRc = 3;
		break;
	default:
		nRc = 0;
	}

	return (nRc);
}

static inline unsigned int getDestinationSize(const unsigned int opcode)
{
	unsigned int nRc;

	switch (opcode & MASK_DESTINATION_SIZE) {
	case 0x00000000:
		nRc = typeSingle;
		break;
	case 0x00000080:
		nRc = typeDouble;
		break;
	case 0x00080000:
		nRc = typeExtended;
		break;
	default:
		nRc = typeNone;
	}

	return (nRc);
}

extern unsigned int checkCondition(const unsigned int opcode,
				   const unsigned int ccodes);

extern const float64 float64Constant[];
extern const float32 float32Constant[];

#endif
