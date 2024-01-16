// SPDX-License-Identifier: GPL-2.0-or-later
/*
    NetWinder Floating Point Emulator
    (c) Rebel.COM, 1998,1999

    Direct questions, comments to Scott Bambrough <scottb@netwinder.org>

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

static float64 float64_rsf(struct roundingData *roundData, float64 rFn, float64 rFm)
{
	return float64_sub(roundData, rFm, rFn);
}

static float64 float64_rdv(struct roundingData *roundData, float64 rFn, float64 rFm)
{
	return float64_div(roundData, rFm, rFn);
}

static float64 (*const dyadic_double[16])(struct roundingData*, float64 rFn, float64 rFm) = {
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

static float64 float64_mvf(struct roundingData *roundData,float64 rFm)
{
	return rFm;
}

static float64 float64_mnf(struct roundingData *roundData,float64 rFm)
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

static float64 float64_abs(struct roundingData *roundData,float64 rFm)
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

static float64 (*const monadic_double[16])(struct roundingData *, float64 rFm) = {
	[MVF_CODE >> 20] = float64_mvf,
	[MNF_CODE >> 20] = float64_mnf,
	[ABS_CODE >> 20] = float64_abs,
	[RND_CODE >> 20] = float64_round_to_int,
	[URD_CODE >> 20] = float64_round_to_int,
	[SQT_CODE >> 20] = float64_sqrt,
	[NRM_CODE >> 20] = float64_mvf,
};

unsigned int DoubleCPDO(struct roundingData *roundData, const unsigned int opcode, FPREG * rFd)
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
			rFd->fDouble = dyadic_double[opc_mask_shift](roundData, rFn, rFm);
		} else {
			return 0;
		}
	} else {
		if (monadic_double[opc_mask_shift]) {
			rFd->fDouble = monadic_double[opc_mask_shift](roundData, rFm);
		} else {
			return 0;
		}
	}

	return 1;
}
