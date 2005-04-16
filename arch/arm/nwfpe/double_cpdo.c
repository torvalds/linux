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

union float64_components {
	float64 f64;
	unsigned int i[2];
};

float64 float64_exp(float64 Fm);
float64 float64_ln(float64 Fm);
float64 float64_sin(float64 rFm);
float64 float64_cos(float64 rFm);
float64 float64_arcsin(float64 rFm);
float64 float64_arctan(float64 rFm);
float64 float64_log(float64 rFm);
float64 float64_tan(float64 rFm);
float64 float64_arccos(float64 rFm);
float64 float64_pow(float64 rFn, float64 rFm);
float64 float64_pol(float64 rFn, float64 rFm);

static float64 float64_rsf(float64 rFn, float64 rFm)
{
	return float64_sub(rFm, rFn);
}

static float64 float64_rdv(float64 rFn, float64 rFm)
{
	return float64_div(rFm, rFn);
}

static float64 (*const dyadic_double[16])(float64 rFn, float64 rFm) = {
	[ADF_CODE >> 20] = float64_add,
	[MUF_CODE >> 20] = float64_mul,
	[SUF_CODE >> 20] = float64_sub,
	[RSF_CODE >> 20] = float64_rsf,
	[DVF_CODE >> 20] = float64_div,
	[RDF_CODE >> 20] = float64_rdv,
	[RMF_CODE >> 20] = float64_rem,

	/* strictly, these opcodes should not be implemented */
	[FML_CODE >> 20] = float64_mul,
	[FDV_CODE >> 20] = float64_div,
	[FRD_CODE >> 20] = float64_rdv,
};

static float64 float64_mvf(float64 rFm)
{
	return rFm;
}

static float64 float64_mnf(float64 rFm)
{
	union float64_components u;

	u.f64 = rFm;
#ifdef __ARMEB__
	u.i[0] ^= 0x80000000;
#else
	u.i[1] ^= 0x80000000;
#endif

	return u.f64;
}

static float64 float64_abs(float64 rFm)
{
	union float64_components u;

	u.f64 = rFm;
#ifdef __ARMEB__
	u.i[0] &= 0x7fffffff;
#else
	u.i[1] &= 0x7fffffff;
#endif

	return u.f64;
}

static float64 (*const monadic_double[16])(float64 rFm) = {
	[MVF_CODE >> 20] = float64_mvf,
	[MNF_CODE >> 20] = float64_mnf,
	[ABS_CODE >> 20] = float64_abs,
	[RND_CODE >> 20] = float64_round_to_int,
	[URD_CODE >> 20] = float64_round_to_int,
	[SQT_CODE >> 20] = float64_sqrt,
	[NRM_CODE >> 20] = float64_mvf,
};

unsigned int DoubleCPDO(const unsigned int opcode, FPREG * rFd)
{
	FPA11 *fpa11 = GET_FPA11();
	float64 rFm;
	unsigned int Fm, opc_mask_shift;

	Fm = getFm(opcode);
	if (CONSTANT_FM(opcode)) {
		rFm = getDoubleConstant(Fm);
	} else {
		switch (fpa11->fType[Fm]) {
		case typeSingle:
			rFm = float32_to_float64(fpa11->fpreg[Fm].fSingle);
			break;

		case typeDouble:
			rFm = fpa11->fpreg[Fm].fDouble;
			break;

		default:
			return 0;
		}
	}

	opc_mask_shift = (opcode & MASK_ARITHMETIC_OPCODE) >> 20;
	if (!MONADIC_INSTRUCTION(opcode)) {
		unsigned int Fn = getFn(opcode);
		float64 rFn;

		switch (fpa11->fType[Fn]) {
		case typeSingle:
			rFn = float32_to_float64(fpa11->fpreg[Fn].fSingle);
			break;

		case typeDouble:
			rFn = fpa11->fpreg[Fn].fDouble;
			break;

		default:
			return 0;
		}

		if (dyadic_double[opc_mask_shift]) {
			rFd->fDouble = dyadic_double[opc_mask_shift](rFn, rFm);
		} else {
			return 0;
		}
	} else {
		if (monadic_double[opc_mask_shift]) {
			rFd->fDouble = monadic_double[opc_mask_shift](rFm);
		} else {
			return 0;
		}
	}

	return 1;
}
