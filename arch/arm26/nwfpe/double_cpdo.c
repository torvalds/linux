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

float64 float64_exp(float64 Fm);
float64 float64_ln(float64 Fm);
float64 float64_sin(float64 rFm);
float64 float64_cos(float64 rFm);
float64 float64_arcsin(float64 rFm);
float64 float64_arctan(float64 rFm);
float64 float64_log(float64 rFm);
float64 float64_tan(float64 rFm);
float64 float64_arccos(float64 rFm);
float64 float64_pow(float64 rFn,float64 rFm);
float64 float64_pol(float64 rFn,float64 rFm);

unsigned int DoubleCPDO(const unsigned int opcode)
{
   FPA11 *fpa11 = GET_FPA11();
   float64 rFm, rFn = 0; //FIXME - should be zero?
   unsigned int Fd, Fm, Fn, nRc = 1;

   //printk("DoubleCPDO(0x%08x)\n",opcode);
   
   Fm = getFm(opcode);
   if (CONSTANT_FM(opcode))
   {
     rFm = getDoubleConstant(Fm);
   }
   else
   {  
     switch (fpa11->fType[Fm])
     {
        case typeSingle:
          rFm = float32_to_float64(fpa11->fpreg[Fm].fSingle);
        break;

        case typeDouble:
          rFm = fpa11->fpreg[Fm].fDouble;
          break;

        case typeExtended:
            // !! patb
	    //printk("not implemented! why not?\n");
            //!! ScottB
            // should never get here, if extended involved
            // then other operand should be promoted then
            // ExtendedCPDO called.
            break;

        default: return 0;
     }
   }

   if (!MONADIC_INSTRUCTION(opcode))
   {
      Fn = getFn(opcode);
      switch (fpa11->fType[Fn])
      {
        case typeSingle:
          rFn = float32_to_float64(fpa11->fpreg[Fn].fSingle);
        break;

        case typeDouble:
          rFn = fpa11->fpreg[Fn].fDouble;
        break;
        
        default: return 0;
      }
   }

   Fd = getFd(opcode);
   /* !! this switch isn't optimized; better (opcode & MASK_ARITHMETIC_OPCODE)>>24, sort of */
   switch (opcode & MASK_ARITHMETIC_OPCODE)
   {
      /* dyadic opcodes */
      case ADF_CODE:
         fpa11->fpreg[Fd].fDouble = float64_add(rFn,rFm);
      break;

      case MUF_CODE:
      case FML_CODE:
         fpa11->fpreg[Fd].fDouble = float64_mul(rFn,rFm);
      break;

      case SUF_CODE:
         fpa11->fpreg[Fd].fDouble = float64_sub(rFn,rFm);
      break;

      case RSF_CODE:
         fpa11->fpreg[Fd].fDouble = float64_sub(rFm,rFn);
      break;

      case DVF_CODE:
      case FDV_CODE:
         fpa11->fpreg[Fd].fDouble = float64_div(rFn,rFm);
      break;

      case RDF_CODE:
      case FRD_CODE:
         fpa11->fpreg[Fd].fDouble = float64_div(rFm,rFn);
      break;

#if 0
      case POW_CODE:
         fpa11->fpreg[Fd].fDouble = float64_pow(rFn,rFm);
      break;

      case RPW_CODE:
         fpa11->fpreg[Fd].fDouble = float64_pow(rFm,rFn);
      break;
#endif

      case RMF_CODE:
         fpa11->fpreg[Fd].fDouble = float64_rem(rFn,rFm);
      break;

#if 0
      case POL_CODE:
         fpa11->fpreg[Fd].fDouble = float64_pol(rFn,rFm);
      break;
#endif

      /* monadic opcodes */
      case MVF_CODE:
         fpa11->fpreg[Fd].fDouble = rFm;
      break;

      case MNF_CODE:
      {
         unsigned int *p = (unsigned int*)&rFm;
         p[1] ^= 0x80000000;
         fpa11->fpreg[Fd].fDouble = rFm;
      }
      break;

      case ABS_CODE:
      {
         unsigned int *p = (unsigned int*)&rFm;
         p[1] &= 0x7fffffff;
         fpa11->fpreg[Fd].fDouble = rFm;
      }
      break;

      case RND_CODE:
      case URD_CODE:
         fpa11->fpreg[Fd].fDouble = float64_round_to_int(rFm);
      break;

      case SQT_CODE:
         fpa11->fpreg[Fd].fDouble = float64_sqrt(rFm);
      break;

#if 0
      case LOG_CODE:
         fpa11->fpreg[Fd].fDouble = float64_log(rFm);
      break;

      case LGN_CODE:
         fpa11->fpreg[Fd].fDouble = float64_ln(rFm);
      break;

      case EXP_CODE:
         fpa11->fpreg[Fd].fDouble = float64_exp(rFm);
      break;

      case SIN_CODE:
         fpa11->fpreg[Fd].fDouble = float64_sin(rFm);
      break;

      case COS_CODE:
         fpa11->fpreg[Fd].fDouble = float64_cos(rFm);
      break;

      case TAN_CODE:
         fpa11->fpreg[Fd].fDouble = float64_tan(rFm);
      break;

      case ASN_CODE:
         fpa11->fpreg[Fd].fDouble = float64_arcsin(rFm);
      break;

      case ACS_CODE:
         fpa11->fpreg[Fd].fDouble = float64_arccos(rFm);
      break;

      case ATN_CODE:
         fpa11->fpreg[Fd].fDouble = float64_arctan(rFm);
      break;
#endif

      case NRM_CODE:
      break;
      
      default:
      {
        nRc = 0;
      }
   }

   if (0 != nRc) fpa11->fType[Fd] = typeDouble;
   return nRc;
}

#if 0
float64 float64_exp(float64 rFm)
{
  return rFm;
//series
}

float64 float64_ln(float64 rFm)
{
  return rFm;
//series
}

float64 float64_sin(float64 rFm)
{
  return rFm;
//series
}

float64 float64_cos(float64 rFm)
{
   return rFm;
   //series
}

#if 0
float64 float64_arcsin(float64 rFm)
{
//series
}

float64 float64_arctan(float64 rFm)
{
  //series
}
#endif

float64 float64_log(float64 rFm)
{
  return float64_div(float64_ln(rFm),getDoubleConstant(7));
}

float64 float64_tan(float64 rFm)
{
  return float64_div(float64_sin(rFm),float64_cos(rFm));
}

float64 float64_arccos(float64 rFm)
{
return rFm;
   //return float64_sub(halfPi,float64_arcsin(rFm));
}

float64 float64_pow(float64 rFn,float64 rFm)
{
  return float64_exp(float64_mul(rFm,float64_ln(rFn))); 
}

float64 float64_pol(float64 rFn,float64 rFm)
{
  return float64_arctan(float64_div(rFn,rFm)); 
}
#endif
