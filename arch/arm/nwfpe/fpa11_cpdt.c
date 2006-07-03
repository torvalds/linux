/*
    NetWinder Floating Point Emulator
    (c) Rebel.com, 1998-1999
    (c) Philip Blundell, 1998, 2001

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
#include "fpmodule.h"
#include "fpmodule.inl"

#include <asm/uaccess.h>

static inline void loadSingle(const unsigned int Fn, const unsigned int __user *pMem)
{
	FPA11 *fpa11 = GET_FPA11();
	fpa11->fType[Fn] = typeSingle;
	get_user(fpa11->fpreg[Fn].fSingle, pMem);
}

static inline void loadDouble(const unsigned int Fn, const unsigned int __user *pMem)
{
	FPA11 *fpa11 = GET_FPA11();
	unsigned int *p;
	p = (unsigned int *) &fpa11->fpreg[Fn].fDouble;
	fpa11->fType[Fn] = typeDouble;
#ifdef __ARMEB__
	get_user(p[0], &pMem[0]);	/* sign & exponent */
	get_user(p[1], &pMem[1]);
#else
	get_user(p[0], &pMem[1]);
	get_user(p[1], &pMem[0]);	/* sign & exponent */
#endif
}

#ifdef CONFIG_FPE_NWFPE_XP
static inline void loadExtended(const unsigned int Fn, const unsigned int __user *pMem)
{
	FPA11 *fpa11 = GET_FPA11();
	unsigned int *p;
	p = (unsigned int *) &fpa11->fpreg[Fn].fExtended;
	fpa11->fType[Fn] = typeExtended;
	get_user(p[0], &pMem[0]);	/* sign & exponent */
#ifdef __ARMEB__
	get_user(p[1], &pMem[1]);	/* ms bits */
	get_user(p[2], &pMem[2]);	/* ls bits */
#else
	get_user(p[1], &pMem[2]);	/* ls bits */
	get_user(p[2], &pMem[1]);	/* ms bits */
#endif
}
#endif

static inline void loadMultiple(const unsigned int Fn, const unsigned int __user *pMem)
{
	FPA11 *fpa11 = GET_FPA11();
	register unsigned int *p;
	unsigned long x;

	p = (unsigned int *) &(fpa11->fpreg[Fn]);
	get_user(x, &pMem[0]);
	fpa11->fType[Fn] = (x >> 14) & 0x00000003;

	switch (fpa11->fType[Fn]) {
	case typeSingle:
	case typeDouble:
		{
			get_user(p[0], &pMem[2]);	/* Single */
			get_user(p[1], &pMem[1]);	/* double msw */
			p[2] = 0;			/* empty */
		}
		break;

#ifdef CONFIG_FPE_NWFPE_XP
	case typeExtended:
		{
			get_user(p[1], &pMem[2]);
			get_user(p[2], &pMem[1]);	/* msw */
			p[0] = (x & 0x80003fff);
		}
		break;
#endif
	}
}

static inline void storeSingle(struct roundingData *roundData, const unsigned int Fn, unsigned int __user *pMem)
{
	FPA11 *fpa11 = GET_FPA11();
	union {
		float32 f;
		unsigned int i[1];
	} val;

	switch (fpa11->fType[Fn]) {
	case typeDouble:
		val.f = float64_to_float32(roundData, fpa11->fpreg[Fn].fDouble);
		break;

#ifdef CONFIG_FPE_NWFPE_XP
	case typeExtended:
		val.f = floatx80_to_float32(roundData, fpa11->fpreg[Fn].fExtended);
		break;
#endif

	default:
		val.f = fpa11->fpreg[Fn].fSingle;
	}

	put_user(val.i[0], pMem);
}

static inline void storeDouble(struct roundingData *roundData, const unsigned int Fn, unsigned int __user *pMem)
{
	FPA11 *fpa11 = GET_FPA11();
	union {
		float64 f;
		unsigned int i[2];
	} val;

	switch (fpa11->fType[Fn]) {
	case typeSingle:
		val.f = float32_to_float64(fpa11->fpreg[Fn].fSingle);
		break;

#ifdef CONFIG_FPE_NWFPE_XP
	case typeExtended:
		val.f = floatx80_to_float64(roundData, fpa11->fpreg[Fn].fExtended);
		break;
#endif

	default:
		val.f = fpa11->fpreg[Fn].fDouble;
	}

#ifdef __ARMEB__
	put_user(val.i[0], &pMem[0]);	/* msw */
	put_user(val.i[1], &pMem[1]);	/* lsw */
#else
	put_user(val.i[1], &pMem[0]);	/* msw */
	put_user(val.i[0], &pMem[1]);	/* lsw */
#endif
}

#ifdef CONFIG_FPE_NWFPE_XP
static inline void storeExtended(const unsigned int Fn, unsigned int __user *pMem)
{
	FPA11 *fpa11 = GET_FPA11();
	union {
		floatx80 f;
		unsigned int i[3];
	} val;

	switch (fpa11->fType[Fn]) {
	case typeSingle:
		val.f = float32_to_floatx80(fpa11->fpreg[Fn].fSingle);
		break;

	case typeDouble:
		val.f = float64_to_floatx80(fpa11->fpreg[Fn].fDouble);
		break;

	default:
		val.f = fpa11->fpreg[Fn].fExtended;
	}

	put_user(val.i[0], &pMem[0]);	/* sign & exp */
#ifdef __ARMEB__
	put_user(val.i[1], &pMem[1]);	/* msw */
	put_user(val.i[2], &pMem[2]);
#else
	put_user(val.i[1], &pMem[2]);
	put_user(val.i[2], &pMem[1]);	/* msw */
#endif
}
#endif

static inline void storeMultiple(const unsigned int Fn, unsigned int __user *pMem)
{
	FPA11 *fpa11 = GET_FPA11();
	register unsigned int nType, *p;

	p = (unsigned int *) &(fpa11->fpreg[Fn]);
	nType = fpa11->fType[Fn];

	switch (nType) {
	case typeSingle:
	case typeDouble:
		{
			put_user(p[0], &pMem[2]);	/* single */
			put_user(p[1], &pMem[1]);	/* double msw */
			put_user(nType << 14, &pMem[0]);
		}
		break;

#ifdef CONFIG_FPE_NWFPE_XP
	case typeExtended:
		{
			put_user(p[2], &pMem[1]);	/* msw */
			put_user(p[1], &pMem[2]);
			put_user((p[0] & 0x80003fff) | (nType << 14), &pMem[0]);
		}
		break;
#endif
	}
}

unsigned int PerformLDF(const unsigned int opcode)
{
	unsigned int __user *pBase, *pAddress, *pFinal;
	unsigned int nRc = 1, write_back = WRITE_BACK(opcode);

	pBase = (unsigned int __user *) readRegister(getRn(opcode));
	if (REG_PC == getRn(opcode)) {
		pBase += 2;
		write_back = 0;
	}

	pFinal = pBase;
	if (BIT_UP_SET(opcode))
		pFinal += getOffset(opcode);
	else
		pFinal -= getOffset(opcode);

	if (PREINDEXED(opcode))
		pAddress = pFinal;
	else
		pAddress = pBase;

	switch (opcode & MASK_TRANSFER_LENGTH) {
	case TRANSFER_SINGLE:
		loadSingle(getFd(opcode), pAddress);
		break;
	case TRANSFER_DOUBLE:
		loadDouble(getFd(opcode), pAddress);
		break;
#ifdef CONFIG_FPE_NWFPE_XP
	case TRANSFER_EXTENDED:
		loadExtended(getFd(opcode), pAddress);
		break;
#endif
	default:
		nRc = 0;
	}

	if (write_back)
		writeRegister(getRn(opcode), (unsigned long) pFinal);
	return nRc;
}

unsigned int PerformSTF(const unsigned int opcode)
{
	unsigned int __user *pBase, *pAddress, *pFinal;
	unsigned int nRc = 1, write_back = WRITE_BACK(opcode);
	struct roundingData roundData;

	roundData.mode = SetRoundingMode(opcode);
	roundData.precision = SetRoundingPrecision(opcode);
	roundData.exception = 0;

	pBase = (unsigned int __user *) readRegister(getRn(opcode));
	if (REG_PC == getRn(opcode)) {
		pBase += 2;
		write_back = 0;
	}

	pFinal = pBase;
	if (BIT_UP_SET(opcode))
		pFinal += getOffset(opcode);
	else
		pFinal -= getOffset(opcode);

	if (PREINDEXED(opcode))
		pAddress = pFinal;
	else
		pAddress = pBase;

	switch (opcode & MASK_TRANSFER_LENGTH) {
	case TRANSFER_SINGLE:
		storeSingle(&roundData, getFd(opcode), pAddress);
		break;
	case TRANSFER_DOUBLE:
		storeDouble(&roundData, getFd(opcode), pAddress);
		break;
#ifdef CONFIG_FPE_NWFPE_XP
	case TRANSFER_EXTENDED:
		storeExtended(getFd(opcode), pAddress);
		break;
#endif
	default:
		nRc = 0;
	}

	if (roundData.exception)
		float_raise(roundData.exception);

	if (write_back)
		writeRegister(getRn(opcode), (unsigned long) pFinal);
	return nRc;
}

unsigned int PerformLFM(const unsigned int opcode)
{
	unsigned int __user *pBase, *pAddress, *pFinal;
	unsigned int i, Fd, write_back = WRITE_BACK(opcode);

	pBase = (unsigned int __user *) readRegister(getRn(opcode));
	if (REG_PC == getRn(opcode)) {
		pBase += 2;
		write_back = 0;
	}

	pFinal = pBase;
	if (BIT_UP_SET(opcode))
		pFinal += getOffset(opcode);
	else
		pFinal -= getOffset(opcode);

	if (PREINDEXED(opcode))
		pAddress = pFinal;
	else
		pAddress = pBase;

	Fd = getFd(opcode);
	for (i = getRegisterCount(opcode); i > 0; i--) {
		loadMultiple(Fd, pAddress);
		pAddress += 3;
		Fd++;
		if (Fd == 8)
			Fd = 0;
	}

	if (write_back)
		writeRegister(getRn(opcode), (unsigned long) pFinal);
	return 1;
}

unsigned int PerformSFM(const unsigned int opcode)
{
	unsigned int __user *pBase, *pAddress, *pFinal;
	unsigned int i, Fd, write_back = WRITE_BACK(opcode);

	pBase = (unsigned int __user *) readRegister(getRn(opcode));
	if (REG_PC == getRn(opcode)) {
		pBase += 2;
		write_back = 0;
	}

	pFinal = pBase;
	if (BIT_UP_SET(opcode))
		pFinal += getOffset(opcode);
	else
		pFinal -= getOffset(opcode);

	if (PREINDEXED(opcode))
		pAddress = pFinal;
	else
		pAddress = pBase;

	Fd = getFd(opcode);
	for (i = getRegisterCount(opcode); i > 0; i--) {
		storeMultiple(Fd, pAddress);
		pAddress += 3;
		Fd++;
		if (Fd == 8)
			Fd = 0;
	}

	if (write_back)
		writeRegister(getRn(opcode), (unsigned long) pFinal);
	return 1;
}

unsigned int EmulateCPDT(const unsigned int opcode)
{
	unsigned int nRc = 0;

	if (LDF_OP(opcode)) {
		nRc = PerformLDF(opcode);
	} else if (LFM_OP(opcode)) {
		nRc = PerformLFM(opcode);
	} else if (STF_OP(opcode)) {
		nRc = PerformSTF(opcode);
	} else if (SFM_OP(opcode)) {
		nRc = PerformSFM(opcode);
	} else {
		nRc = 0;
	}

	return nRc;
}
