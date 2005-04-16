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

#include <linux/config.h>
#include "fpa11.h"
#include "fpopcode.h"

unsigned int SingleCPDO(const unsigned int opcode, FPREG * rFd);
unsigned int DoubleCPDO(const unsigned int opcode, FPREG * rFd);
unsigned int ExtendedCPDO(const unsigned int opcode, FPREG * rFd);

unsigned int EmulateCPDO(const unsigned int opcode)
{
	FPA11 *fpa11 = GET_FPA11();
	FPREG *rFd;
	unsigned int nType, nDest, nRc;

	/* Get the destination size.  If not valid let Linux perform
	   an invalid instruction trap. */
	nDest = getDestinationSize(opcode);
	if (typeNone == nDest)
		return 0;

	SetRoundingMode(opcode);

	/* Compare the size of the operands in Fn and Fm.
	   Choose the largest size and perform operations in that size,
	   in order to make use of all the precision of the operands.
	   If Fm is a constant, we just grab a constant of a size
	   matching the size of the operand in Fn. */
	if (MONADIC_INSTRUCTION(opcode))
		nType = nDest;
	else
		nType = fpa11->fType[getFn(opcode)];

	if (!CONSTANT_FM(opcode)) {
		register unsigned int Fm = getFm(opcode);
		if (nType < fpa11->fType[Fm]) {
			nType = fpa11->fType[Fm];
		}
	}

	rFd = &fpa11->fpreg[getFd(opcode)];

	switch (nType) {
	case typeSingle:
		nRc = SingleCPDO(opcode, rFd);
		break;
	case typeDouble:
		nRc = DoubleCPDO(opcode, rFd);
		break;
#ifdef CONFIG_FPE_NWFPE_XP
	case typeExtended:
		nRc = ExtendedCPDO(opcode, rFd);
		break;
#endif
	default:
		nRc = 0;
	}

	/* The CPDO functions used to always set the destination type
	   to be the same as their working size. */

	if (nRc != 0) {
		/* If the operation succeeded, check to see if the result in the
		   destination register is the correct size.  If not force it
		   to be. */

		fpa11->fType[getFd(opcode)] = nDest;

#ifdef CONFIG_FPE_NWFPE_XP
		if (nDest != nType) {
			switch (nDest) {
			case typeSingle:
				{
					if (typeDouble == nType)
						rFd->fSingle = float64_to_float32(rFd->fDouble);
					else
						rFd->fSingle = floatx80_to_float32(rFd->fExtended);
				}
				break;

			case typeDouble:
				{
					if (typeSingle == nType)
						rFd->fDouble = float32_to_float64(rFd->fSingle);
					else
						rFd->fDouble = floatx80_to_float64(rFd->fExtended);
				}
				break;

			case typeExtended:
				{
					if (typeSingle == nType)
						rFd->fExtended = float32_to_floatx80(rFd->fSingle);
					else
						rFd->fExtended = float64_to_floatx80(rFd->fDouble);
				}
				break;
			}
		}
#else
		if (nDest != nType) {
			if (nDest == typeSingle)
				rFd->fSingle = float64_to_float32(rFd->fDouble);
			else
				rFd->fDouble = float32_to_float64(rFd->fSingle);
		}
#endif
	}

	return nRc;
}
