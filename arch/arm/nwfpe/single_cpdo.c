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

#include "fpa11.h"
#include "softfloat.h"
#include "fpopcode.h"

float32 float32_exp(float32 Fm);
float32 float32_ln(float32 Fm);
float32 float32_sin(float32 rFm);
float32 float32_cos(float32 rFm);
float32 float32_arcsin(float32 rFm);
float32 float32_arctan(float32 rFm);
float32 float32_log(float32 rFm);
float32 float32_tan(float32 rFm);
float32 float32_arccos(float32 rFm);
float32 float32_pow(float32 rFn, float32 rFm);
float32 float32_pol(float32 rFn, float32 rFm);

static float32 float32_rsf(float32 rFn, float32 rFm)
{
	return float32_sub(rFm, rFn);
}

static float32 float32_rdv(float32 rFn, float32 rFm)
{
	return float32_div(rFm, rFn);
}

static float32 (*const dyadic_single[16])(float32 rFn, float32 rFm) = {
	[ADF_CODE >> 20] = float32_add,
	[MUF_CODE >> 20] = float32_mul,
	[SUF_CODE >> 20] = float32_sub,
	[RSF_CODE >> 20] = float32_rsf,
	[DVF_CODE >> 20] = float32_div,
	[RDF_CODE >> 20] = float32_rdv,
	[RMF_CODE >> 20] = float32_rem,

	[FML_CODE >> 20] = float32_mul,
	[FDV_CODE >> 20] = float32_div,
	[FRD_CODE >> 20] = float32_rdv,
};

static float32 float32_mvf(float32 rFm)
{
	return rFm;
}

static float32 float32_mnf(float32 rFm)
{
	return rFm ^ 0x80000000;
}

static float32 float32_abs(float32 rFm)
{
	return rFm & 0x7fffffff;
}

static float32 (*const monadic_single[16])(float32 rFm) = {
	[MVF_CODE >> 20] = float32_mvf,
	[MNF_CODE >> 20] = float32_mnf,
	[ABS_CODE >> 20] = float32_abs,
	[RND_CODE >> 20] = float32_round_to_int,
	[URD_CODE >> 20] = float32_round_to_int,
	[SQT_CODE >> 20] = float32_sqrt,
	[NRM_CODE >> 20] = float32_mvf,
};

unsigned int SingleCPDO(const unsigned int opcode, FPREG * rFd)
{
	FPA11 *fpa11 = GET_FPA11();
	float32 rFm;
	unsigned int Fm, opc_mask_shift;

	Fm = getFm(opcode);
	if (CONSTANT_FM(opcode)) {
		rFm = getSingleConstant(Fm);
	} else if (fpa11->fType[Fm] == typeSingle) {
		rFm = fpa11->fpreg[Fm].fSingle;
	} else {
		return 0;
	}

	opc_mask_shift = (opcode & MASK_ARITHMETIC_OPCODE) >> 20;
	if (!MONADIC_INSTRUCTION(opcode)) {
		unsigned int Fn = getFn(opcode);
		float32 rFn;

		if (fpa11->fType[Fn] == typeSingle &&
		    dyadic_single[opc_mask_shift]) {
			rFn = fpa11->fpreg[Fn].fSingle;
			rFd->fSingle = dyadic_single[opc_mask_shift](rFn, rFm);
		} else {
			return 0;
		}
	} else {
		if (monadic_single[opc_mask_shift]) {
			rFd->fSingle = monadic_single[opc_mask_shift](rFm);
		} else {
			return 0;
		}
	}

	return 1;
}
