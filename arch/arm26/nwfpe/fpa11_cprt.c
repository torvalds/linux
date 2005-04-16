/*
    NetWinder Floating Point Emulator
    (c) Rebel.COM, 1998,1999
    (c) Philip Blundell, 1999

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
#include "milieu.h"
#include "softfloat.h"
#include "fpopcode.h"
#include "fpa11.inl"
#include "fpmodule.h"
#include "fpmodule.inl"

extern flag floatx80_is_nan(floatx80);
extern flag float64_is_nan( float64);
extern flag float32_is_nan( float32);

void SetRoundingMode(const unsigned int opcode);

unsigned int PerformFLT(const unsigned int opcode);
unsigned int PerformFIX(const unsigned int opcode);

static unsigned int
PerformComparison(const unsigned int opcode);

unsigned int EmulateCPRT(const unsigned int opcode)
{
  unsigned int nRc = 1;

  //printk("EmulateCPRT(0x%08x)\n",opcode);

  if (opcode & 0x800000)
  {
     /* This is some variant of a comparison (PerformComparison will
	sort out which one).  Since most of the other CPRT
	instructions are oddball cases of some sort or other it makes
	sense to pull this out into a fast path.  */
     return PerformComparison(opcode);
  }

  /* Hint to GCC that we'd like a jump table rather than a load of CMPs */
  switch ((opcode & 0x700000) >> 20)
  {
    case  FLT_CODE >> 20: nRc = PerformFLT(opcode); break;
    case  FIX_CODE >> 20: nRc = PerformFIX(opcode); break;
    
    case  WFS_CODE >> 20: writeFPSR(readRegister(getRd(opcode))); break;
    case  RFS_CODE >> 20: writeRegister(getRd(opcode),readFPSR()); break;

#if 0    /* We currently have no use for the FPCR, so there's no point
	    in emulating it. */
    case  WFC_CODE >> 20: writeFPCR(readRegister(getRd(opcode)));
    case  RFC_CODE >> 20: writeRegister(getRd(opcode),readFPCR()); break;
#endif

    default: nRc = 0;
  }
  
  return nRc;
}

unsigned int PerformFLT(const unsigned int opcode)
{
   FPA11 *fpa11 = GET_FPA11();
   
   unsigned int nRc = 1;
   SetRoundingMode(opcode);

   switch (opcode & MASK_ROUNDING_PRECISION)
   {
      case ROUND_SINGLE:
      {
        fpa11->fType[getFn(opcode)] = typeSingle;
        fpa11->fpreg[getFn(opcode)].fSingle =
	   int32_to_float32(readRegister(getRd(opcode)));
      }
      break;

      case ROUND_DOUBLE:
      {
        fpa11->fType[getFn(opcode)] = typeDouble;
        fpa11->fpreg[getFn(opcode)].fDouble =
            int32_to_float64(readRegister(getRd(opcode)));
      }
      break;
        
      case ROUND_EXTENDED:
      {
        fpa11->fType[getFn(opcode)] = typeExtended;
        fpa11->fpreg[getFn(opcode)].fExtended =
	   int32_to_floatx80(readRegister(getRd(opcode)));
      }
      break;
      
      default: nRc = 0;
  }
  
  return nRc;
}

unsigned int PerformFIX(const unsigned int opcode)
{
   FPA11 *fpa11 = GET_FPA11();
   unsigned int nRc = 1;
   unsigned int Fn = getFm(opcode);
   
   SetRoundingMode(opcode);

   switch (fpa11->fType[Fn])
   {
      case typeSingle:
      {
         writeRegister(getRd(opcode),
	               float32_to_int32(fpa11->fpreg[Fn].fSingle));
      }
      break;

      case typeDouble:
      {
         writeRegister(getRd(opcode),
	               float64_to_int32(fpa11->fpreg[Fn].fDouble));
      }
      break;
      	               
      case typeExtended:
      {
         writeRegister(getRd(opcode),
	               floatx80_to_int32(fpa11->fpreg[Fn].fExtended));
      }
      break;
      
      default: nRc = 0;
  }
  
  return nRc;
}

   
static unsigned int __inline__
PerformComparisonOperation(floatx80 Fn, floatx80 Fm)
{
   unsigned int flags = 0;

   /* test for less than condition */
   if (floatx80_lt(Fn,Fm))
   {
      flags |= CC_NEGATIVE;
   }
  
   /* test for equal condition */
   if (floatx80_eq(Fn,Fm))
   {
      flags |= CC_ZERO;
   }

   /* test for greater than or equal condition */
   if (floatx80_lt(Fm,Fn))
   {
      flags |= CC_CARRY;
   }
   
   writeConditionCodes(flags);
   return 1;
}

/* This instruction sets the flags N, Z, C, V in the FPSR. */
   
static unsigned int PerformComparison(const unsigned int opcode)
{
   FPA11 *fpa11 = GET_FPA11();
   unsigned int Fn, Fm;
   floatx80 rFn, rFm;
   int e_flag = opcode & 0x400000;	/* 1 if CxFE */
   int n_flag = opcode & 0x200000;	/* 1 if CNxx */
   unsigned int flags = 0;

   //printk("PerformComparison(0x%08x)\n",opcode);

   Fn = getFn(opcode);
   Fm = getFm(opcode);

   /* Check for unordered condition and convert all operands to 80-bit
      format.
      ?? Might be some mileage in avoiding this conversion if possible.
      Eg, if both operands are 32-bit, detect this and do a 32-bit
      comparison (cheaper than an 80-bit one).  */
   switch (fpa11->fType[Fn])
   {
      case typeSingle: 
        //printk("single.\n");
	if (float32_is_nan(fpa11->fpreg[Fn].fSingle))
	   goto unordered;
        rFn = float32_to_floatx80(fpa11->fpreg[Fn].fSingle);
      break;

      case typeDouble: 
        //printk("double.\n");
	if (float64_is_nan(fpa11->fpreg[Fn].fDouble))
	   goto unordered;
        rFn = float64_to_floatx80(fpa11->fpreg[Fn].fDouble);
      break;
      
      case typeExtended: 
        //printk("extended.\n");
	if (floatx80_is_nan(fpa11->fpreg[Fn].fExtended))
	   goto unordered;
        rFn = fpa11->fpreg[Fn].fExtended;
      break;
      
      default: return 0;
   }

   if (CONSTANT_FM(opcode))
   {
     //printk("Fm is a constant: #%d.\n",Fm);
     rFm = getExtendedConstant(Fm);
     if (floatx80_is_nan(rFm))
        goto unordered;
   }
   else
   {
     //printk("Fm = r%d which contains a ",Fm);
      switch (fpa11->fType[Fm])
      {
         case typeSingle: 
           //printk("single.\n");
	   if (float32_is_nan(fpa11->fpreg[Fm].fSingle))
	      goto unordered;
           rFm = float32_to_floatx80(fpa11->fpreg[Fm].fSingle);
         break;

         case typeDouble: 
           //printk("double.\n");
	   if (float64_is_nan(fpa11->fpreg[Fm].fDouble))
	      goto unordered;
           rFm = float64_to_floatx80(fpa11->fpreg[Fm].fDouble);
         break;
      
         case typeExtended: 
           //printk("extended.\n");
	   if (floatx80_is_nan(fpa11->fpreg[Fm].fExtended))
	      goto unordered;
           rFm = fpa11->fpreg[Fm].fExtended;
         break;
      
         default: return 0;
      }
   }

   if (n_flag)
   {
      rFm.high ^= 0x8000;
   }

   return PerformComparisonOperation(rFn,rFm);

 unordered:
   /* ?? The FPA data sheet is pretty vague about this, in particular
      about whether the non-E comparisons can ever raise exceptions.
      This implementation is based on a combination of what it says in
      the data sheet, observation of how the Acorn emulator actually
      behaves (and how programs expect it to) and guesswork.  */
   flags |= CC_OVERFLOW;
   flags &= ~(CC_ZERO | CC_NEGATIVE);

   if (BIT_AC & readFPSR()) flags |= CC_CARRY;

   if (e_flag) float_raise(float_flag_invalid);

   writeConditionCodes(flags);
   return 1;
}
