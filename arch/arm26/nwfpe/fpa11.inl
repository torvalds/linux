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

/* Read and write floating point status register */
extern __inline__ unsigned int readFPSR(void)
{
  FPA11 *fpa11 = GET_FPA11();
  return(fpa11->fpsr);
}

extern __inline__ void writeFPSR(FPSR reg)
{
  FPA11 *fpa11 = GET_FPA11();
  /* the sysid byte in the status register is readonly */
  fpa11->fpsr = (fpa11->fpsr & MASK_SYSID) | (reg & ~MASK_SYSID);
}

/* Read and write floating point control register */
extern __inline__ FPCR readFPCR(void)
{
  FPA11 *fpa11 = GET_FPA11();
  /* clear SB, AB and DA bits before returning FPCR */
  return(fpa11->fpcr & ~MASK_RFC);
}

extern __inline__ void writeFPCR(FPCR reg)
{
  FPA11 *fpa11 = GET_FPA11();
  fpa11->fpcr &= ~MASK_WFC;		/* clear SB, AB and DA bits */
  fpa11->fpcr |= (reg & MASK_WFC);	/* write SB, AB and DA bits */
}
