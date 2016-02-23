/*---------------------------------------------------------------------------+
 |  fpu_tags.c                                                               |
 |                                                                           |
 |  Set FPU register tags.                                                   |
 |                                                                           |
 | Copyright (C) 1997                                                        |
 |                  W. Metzenthen, 22 Parker St, Ormond, Vic 3163, Australia |
 |                  E-mail   billm@jacobi.maths.monash.edu.au                |
 |                                                                           |
 |                                                                           |
 +---------------------------------------------------------------------------*/

#include "fpu_emu.h"
#include "fpu_system.h"
#include "exception.h"

void FPU_pop(void)
{
	fpu_tag_word |= 3 << ((top & 7) * 2);
	top++;
}

int FPU_gettag0(void)
{
	return (fpu_tag_word >> ((top & 7) * 2)) & 3;
}

int FPU_gettagi(int stnr)
{
	return (fpu_tag_word >> (((top + stnr) & 7) * 2)) & 3;
}

int FPU_gettag(int regnr)
{
	return (fpu_tag_word >> ((regnr & 7) * 2)) & 3;
}

void FPU_settag0(int tag)
{
	int regnr = top;
	regnr &= 7;
	fpu_tag_word &= ~(3 << (regnr * 2));
	fpu_tag_word |= (tag & 3) << (regnr * 2);
}

void FPU_settagi(int stnr, int tag)
{
	int regnr = stnr + top;
	regnr &= 7;
	fpu_tag_word &= ~(3 << (regnr * 2));
	fpu_tag_word |= (tag & 3) << (regnr * 2);
}

void FPU_settag(int regnr, int tag)
{
	regnr &= 7;
	fpu_tag_word &= ~(3 << (regnr * 2));
	fpu_tag_word |= (tag & 3) << (regnr * 2);
}

int FPU_Special(FPU_REG const *ptr)
{
	int exp = exponent(ptr);

	if (exp == EXP_BIAS + EXP_UNDER)
		return TW_Denormal;
	else if (exp != EXP_BIAS + EXP_OVER)
		return TW_NaN;
	else if ((ptr->sigh == 0x80000000) && (ptr->sigl == 0))
		return TW_Infinity;
	return TW_NaN;
}

int isNaN(FPU_REG const *ptr)
{
	return ((exponent(ptr) == EXP_BIAS + EXP_OVER)
		&& !((ptr->sigh == 0x80000000) && (ptr->sigl == 0)));
}

int FPU_empty_i(int stnr)
{
	int regnr = (top + stnr) & 7;

	return ((fpu_tag_word >> (regnr * 2)) & 3) == TAG_Empty;
}

int FPU_stackoverflow(FPU_REG ** st_new_ptr)
{
	*st_new_ptr = &st(-1);

	return ((fpu_tag_word >> (((top - 1) & 7) * 2)) & 3) != TAG_Empty;
}

void FPU_copy_to_regi(FPU_REG const *r, u_char tag, int stnr)
{
	reg_copy(r, &st(stnr));
	FPU_settagi(stnr, tag);
}

void FPU_copy_to_reg1(FPU_REG const *r, u_char tag)
{
	reg_copy(r, &st(1));
	FPU_settagi(1, tag);
}

void FPU_copy_to_reg0(FPU_REG const *r, u_char tag)
{
	int regnr = top;
	regnr &= 7;

	reg_copy(r, &st(0));

	fpu_tag_word &= ~(3 << (regnr * 2));
	fpu_tag_word |= (tag & 3) << (regnr * 2);
}
