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

#include "fpa11.h"
#include "softfloat.h"
#include "fpopcode.h"

floatx80 floatx80_exp(floatx80 Fm);
floatx80 floatx80_ln(floatx80 Fm);
floatx80 floatx80_sin(floatx80 rFm);
floatx80 floatx80_cos(floatx80 rFm);
floatx80 floatx80_arcsin(floatx80 rFm);
floatx80 floatx80_arctan(floatx80 rFm);
floatx80 floatx80_log(floatx80 rFm);
floatx80 floatx80_tan(floatx80 rFm);
floatx80 floatx80_arccos(floatx80 rFm);
floatx80 floatx80_pow(floatx80 rFn, floatx80 rFm);
floatx80 floatx80_pol(floatx80 rFn, floatx80 rFm);

static floatx80 floatx80_rsf(struct roundingData *roundData, floatx80 rFn, floatx80 rFm)
{
	return floatx80_sub(roundData, rFm, rFn);
}

static floatx80 floatx80_rdv(struct roundingData *roundData, floatx80 rFn, floatx80 rFm)
{
	return floatx80_div(roundData, rFm, rFn);
}

static floatx80 (*const dyadic_extended[16])(struct roundingData*, floatx80 rFn, floatx80 rFm) = {
	[ADF_CODE >> 20] = floatx80_add,
	[MUF_CODE >> 20] = floatx80_mul,
	[SUF_CODE >> 20] = floatx80_sub,
	[RSF_CODE >> 20] = floatx80_rsf,
	[DVF_CODE >> 20] = floatx80_div,
	[RDF_CODE >> 20] = floatx80_rdv,
	[RMF_CODE >> 20] = floatx80_rem,

	/* strictly, these opcodes should not be implemented */
	[FML_CODE >> 20] = floatx80_mul,
	[FDV_CODE >> 20] = floatx80_div,
	[FRD_CODE >> 20] = floatx80_rdv,
};

static floatx80 floatx80_mvf(struct roundingData *roundData, floatx80 rFm)
{
	return rFm;
}

static floatx80 floatx80_mnf(struct roundingData *roundData, floatx80 rFm)
{
	rFm.high ^= 0x8000;
	return rFm;
}

static floatx80 floatx80_abs(struct roundingData *roundData, floatx80 rFm)
{
	rFm.high &= 0x7fff;
	return rFm;
}

static floatx80 (*const monadic_extended[16])(struct roundingData*, floatx80 rFm) = {
	[MVF_CODE >> 20] = floatx80_mvf,
	[MNF_CODE >> 20] = floatx80_mnf,
	[ABS_CODE >> 20] = floatx80_abs,
	[RND_CODE >> 20] = floatx80_round_to_int,
	[URD_CODE >> 20] = floatx80_round_to_int,
	[SQT_CODE >> 20] = floatx80_sqrt,
	[NRM_CODE >> 20] = floatx80_mvf,
};

unsigned int ExtendedCPDO(struct roundingData *roundData, const unsigned int opcode, FPREG * rFd)
{
	FPA11 *fpa11 = GET_FPA11();
	floatx80 rFm;
	unsigned int Fm, opc_mask_shift;

	Fm = getFm(opcode);
	if (CONSTANT_FM(opcode)) {
		rFm = getExtendedConstant(Fm);
	} else {
		switch (fpa11->fType[Fm]) {
		case typeSingle:
			rFm = float32_to_floatx80(fpa11->fpreg[Fm].fSingle);
			break;

		case typeDouble:
			rFm = float64_to_floatx80(fpa11->fpreg[Fm].fDouble);
			break;

		case typeExtended:
			rFm = fpa11->fpreg[Fm].fExtended;
			break;

		default:
			return 0;
		}
	}

	opc_mask_shift = (opcode & MASK_ARITHMETIC_OPCODE) >> 20;
	if (!MONADIC_INSTRUCTION(opcode)) {
		unsigned int Fn = getFn(opcode);
		floatx80 rFn;

		switch (fpa11->fType[Fn]) {
		case typeSingle:
			rFn = float32_to_floatx80(fpa11->fpreg[Fn].fSingle);
			break;

		case typeDouble:
			rFn = float64_to_floatx80(fpa11->fpreg[Fn].fDouble);
			break;

		case typeExtended:
			rFn = fpa11->fpreg[Fn].fExtended;
			break;

		default:
			return 0;
		}

		if (dyadic_extended[opc_mask_shift]) {
			rFd->fExtended = dyadic_extended[opc_mask_shift](roundData, rFn, rFm);
		} else {
			return 0;
		}
	} else {
		if (monadic_extended[opc_mask_shift]) {
			rFd->fExtended = monadic_extended[opc_mask_shift](roundData, rFm);
		} else {
			return 0;
		}
	}

	return 1;
}
