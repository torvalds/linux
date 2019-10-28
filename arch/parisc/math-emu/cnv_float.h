/*
 * Linux/PA-RISC Project (http://www.parisc-linux.org/)
 *
 * Floating-point emulation code
 *  Copyright (C) 2001 Hewlett-Packard (Paul Bame) <bame@debian.org>
 *
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2, or (at your option)
 *    any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program; if not, write to the Free Software
 *    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifdef __NO_PA_HDRS
    PA header file -- do not include this header file for non-PA builds.
#endif

/*
 * Some more constants
 */
#define SGL_FX_MAX_EXP 30
#define DBL_FX_MAX_EXP 62
#define QUAD_FX_MAX_EXP 126

#define Dintp1(object) (object)
#define Dintp2(object) (object)

#define Duintp1(object) (object)
#define Duintp2(object) (object)

#define Qintp0(object) (object)
#define Qintp1(object) (object)
#define Qintp2(object) (object)
#define Qintp3(object) (object)


/*
 * These macros will be used specifically by the convert instructions.
 *
 *
 * Single format macros
 */

#define Sgl_to_dbl_exponent(src_exponent,dest)			\
    Deposit_dexponent(dest,src_exponent+(DBL_BIAS-SGL_BIAS))

#define Sgl_to_dbl_mantissa(src_mantissa,destA,destB)	\
    Deposit_dmantissap1(destA,src_mantissa>>3);		\
    Dmantissap2(destB) = src_mantissa << 29

#define Sgl_isinexact_to_fix(sgl_value,exponent)	\
    ((exponent < (SGL_P - 1)) ?				\
     (Sall(sgl_value) << (SGL_EXP_LENGTH + 1 + exponent)) : FALSE)

#define Int_isinexact_to_sgl(int_value)	((int_value << 33 - SGL_EXP_LENGTH) != 0)

#define Sgl_roundnearest_from_int(int_value,sgl_value)			\
    if (int_value & 1<<(SGL_EXP_LENGTH - 2))   /* round bit */		\
	if (((int_value << 34 - SGL_EXP_LENGTH) != 0) || Slow(sgl_value)) \
		Sall(sgl_value)++

#define Dint_isinexact_to_sgl(dint_valueA,dint_valueB)		\
    (((Dintp1(dint_valueA) << 33 - SGL_EXP_LENGTH) != 0) || Dintp2(dint_valueB))

#define Sgl_roundnearest_from_dint(dint_valueA,dint_valueB,sgl_value)	\
    if (Dintp1(dint_valueA) & 1<<(SGL_EXP_LENGTH - 2)) 			\
	if (((Dintp1(dint_valueA) << 34 - SGL_EXP_LENGTH) != 0) ||	\
    	Dintp2(dint_valueB) || Slow(sgl_value)) Sall(sgl_value)++

#define Dint_isinexact_to_dbl(dint_value) 	\
    (Dintp2(dint_value) << 33 - DBL_EXP_LENGTH)

#define Dbl_roundnearest_from_dint(dint_opndB,dbl_opndA,dbl_opndB) 	\
    if (Dintp2(dint_opndB) & 1<<(DBL_EXP_LENGTH - 2))			\
       if ((Dintp2(dint_opndB) << 34 - DBL_EXP_LENGTH) || Dlowp2(dbl_opndB))  \
          if ((++Dallp2(dbl_opndB))==0) Dallp1(dbl_opndA)++

#define Sgl_isone_roundbit(sgl_value,exponent)			\
    ((Sall(sgl_value) << (SGL_EXP_LENGTH + 1 + exponent)) >> 31)

#define Sgl_isone_stickybit(sgl_value,exponent)		\
    (exponent < (SGL_P - 2) ?				\
     Sall(sgl_value) << (SGL_EXP_LENGTH + 2 + exponent) : FALSE)


/* 
 * Double format macros
 */

#define Dbl_to_sgl_exponent(src_exponent,dest)			\
    dest = src_exponent + (SGL_BIAS - DBL_BIAS)

#define Dbl_to_sgl_mantissa(srcA,srcB,dest,inexact,guard,sticky,odd)	\
    Shiftdouble(Dmantissap1(srcA),Dmantissap2(srcB),29,dest); 	\
    guard = Dbit3p2(srcB);					\
    sticky = Dallp2(srcB)<<4;					\
    inexact = guard | sticky;					\
    odd = Dbit2p2(srcB)

#define Dbl_to_sgl_denormalized(srcA,srcB,exp,dest,inexact,guard,sticky,odd,tiny) \
    Deposit_dexponent(srcA,1);						\
    tiny = TRUE;							\
    if (exp >= -2) {							\
	if (exp == 0) {							\
	    inexact = Dallp2(srcB) << 3;				\
	    guard = inexact >> 31;					\
	    sticky = inexact << 1;					\
	    Shiftdouble(Dmantissap1(srcA),Dmantissap2(srcB),29,dest);	\
	    odd = dest << 31;						\
	    if (inexact) {						\
		switch(Rounding_mode()) {				\
		    case ROUNDPLUS:					\
			if (Dbl_iszero_sign(srcA)) {			\
			    dest++;					\
			    if (Sgl_isone_hidden(dest))	\
				tiny = FALSE;				\
			    dest--;					\
			}						\
			break;						\
		    case ROUNDMINUS:					\
			if (Dbl_isone_sign(srcA)) {			\
			    dest++;					\
			    if (Sgl_isone_hidden(dest))	\
				tiny = FALSE;				\
			    dest--;					\
			}						\
			break;						\
		    case ROUNDNEAREST:					\
			if (guard && (sticky || odd)) {			\
			    dest++;					\
			    if (Sgl_isone_hidden(dest))	\
				tiny = FALSE;				\
			    dest--;					\
			}						\
			break;						\
		}							\
	    }								\
		/* shift right by one to get correct result */		\
		guard = odd;						\
		sticky = inexact;					\
		inexact |= guard;					\
		dest >>= 1;						\
    		Deposit_dsign(srcA,0);					\
    	        Shiftdouble(Dallp1(srcA),Dallp2(srcB),30,dest);		\
	        odd = dest << 31;					\
	}								\
	else {								\
    	    inexact = Dallp2(srcB) << (2 + exp);			\
    	    guard = inexact >> 31;					\
    	    sticky = inexact << 1; 					\
    	    Deposit_dsign(srcA,0);					\
    	    if (exp == -2) dest = Dallp1(srcA);				\
    	    else Variable_shift_double(Dallp1(srcA),Dallp2(srcB),30-exp,dest); \
    	    odd = dest << 31;						\
	}								\
    }									\
    else {								\
    	Deposit_dsign(srcA,0);						\
    	if (exp > (1 - SGL_P)) {					\
    	    dest = Dallp1(srcA) >> (- 2 - exp);				\
    	    inexact = Dallp1(srcA) << (34 + exp);			\
    	    guard = inexact >> 31;					\
    	    sticky = (inexact << 1) | Dallp2(srcB);			\
    	    inexact |= Dallp2(srcB); 					\
    	    odd = dest << 31;						\
    	}								\
    	else {								\
    	    dest = 0;							\
    	    inexact = Dallp1(srcA) | Dallp2(srcB);			\
    	    if (exp == (1 - SGL_P)) {					\
    	    	guard = Dhidden(srcA);					\
    	    	sticky = Dmantissap1(srcA) | Dallp2(srcB); 		\
    	    }								\
    	    else {							\
    	    	guard = 0;						\
    	    	sticky = inexact;					\
    	    }								\
    	    odd = 0;							\
    	}								\
    }									\
    exp = 0

#define Dbl_isinexact_to_fix(dbl_valueA,dbl_valueB,exponent)		\
    (exponent < (DBL_P-33) ? 						\
     Dallp2(dbl_valueB) || Dallp1(dbl_valueA) << (DBL_EXP_LENGTH+1+exponent) : \
     (exponent < (DBL_P-1) ? Dallp2(dbl_valueB) << (exponent + (33-DBL_P)) :   \
      FALSE))

#define Dbl_isoverflow_to_int(exponent,dbl_valueA,dbl_valueB)		\
    ((exponent > SGL_FX_MAX_EXP + 1) || Dsign(dbl_valueA)==0 ||		\
     Dmantissap1(dbl_valueA)!=0 || (Dallp2(dbl_valueB)>>21)!=0 ) 

#define Dbl_isone_roundbit(dbl_valueA,dbl_valueB,exponent)              \
    ((exponent < (DBL_P - 33) ?						\
      Dallp1(dbl_valueA) >> ((30 - DBL_EXP_LENGTH) - exponent) :	\
      Dallp2(dbl_valueB) >> ((DBL_P - 2) - exponent)) & 1)

#define Dbl_isone_stickybit(dbl_valueA,dbl_valueB,exponent)		\
    (exponent < (DBL_P-34) ? 						\
     (Dallp2(dbl_valueB) || Dallp1(dbl_valueA)<<(DBL_EXP_LENGTH+2+exponent)) : \
     (exponent<(DBL_P-2) ? (Dallp2(dbl_valueB) << (exponent + (34-DBL_P))) : \
      FALSE))


/* Int macros */

#define Int_from_sgl_mantissa(sgl_value,exponent)	\
    Sall(sgl_value) = 				\
    	(unsigned)(Sall(sgl_value) << SGL_EXP_LENGTH)>>(31 - exponent)

#define Int_from_dbl_mantissa(dbl_valueA,dbl_valueB,exponent)	\
    Shiftdouble(Dallp1(dbl_valueA),Dallp2(dbl_valueB),22,Dallp1(dbl_valueA)); \
    if (exponent < 31) Dallp1(dbl_valueA) >>= 30 - exponent;	\
    else Dallp1(dbl_valueA) <<= 1

#define Int_negate(int_value) int_value = -int_value


/* Dint macros */

#define Dint_from_sgl_mantissa(sgl_value,exponent,dresultA,dresultB)	\
    {Sall(sgl_value) <<= SGL_EXP_LENGTH;  /*  left-justify  */		\
    if (exponent <= 31) {						\
    	Dintp1(dresultA) = 0;						\
    	Dintp2(dresultB) = (unsigned)Sall(sgl_value) >> (31 - exponent); \
    }									\
    else {								\
    	Dintp1(dresultA) = Sall(sgl_value) >> (63 - exponent);		\
    	Dintp2(dresultB) = Sall(sgl_value) << (exponent - 31);		\
    }}


#define Dint_from_dbl_mantissa(dbl_valueA,dbl_valueB,exponent,destA,destB) \
    {if (exponent < 32) {						\
    	Dintp1(destA) = 0;						\
    	if (exponent <= 20)						\
    	    Dintp2(destB) = Dallp1(dbl_valueA) >> 20-exponent;		\
    	else Variable_shift_double(Dallp1(dbl_valueA),Dallp2(dbl_valueB), \
	     52-exponent,Dintp2(destB));					\
    }									\
    else {								\
    	if (exponent <= 52) {						\
    	    Dintp1(destA) = Dallp1(dbl_valueA) >> 52-exponent;		\
	    if (exponent == 52) Dintp2(destB) = Dallp2(dbl_valueB);	\
	    else Variable_shift_double(Dallp1(dbl_valueA),Dallp2(dbl_valueB), \
	    52-exponent,Dintp2(destB));					\
        }								\
    	else {								\
    	    Variable_shift_double(Dallp1(dbl_valueA),Dallp2(dbl_valueB), \
	    84-exponent,Dintp1(destA));					\
    	    Dintp2(destB) = Dallp2(dbl_valueB) << exponent-52;		\
    	}								\
    }}

#define Dint_setzero(dresultA,dresultB) 	\
    Dintp1(dresultA) = 0; 	\
    Dintp2(dresultB) = 0

#define Dint_setone_sign(dresultA,dresultB)		\
    Dintp1(dresultA) = ~Dintp1(dresultA);		\
    if ((Dintp2(dresultB) = -Dintp2(dresultB)) == 0) Dintp1(dresultA)++

#define Dint_set_minint(dresultA,dresultB)		\
    Dintp1(dresultA) = (unsigned int)1<<31;		\
    Dintp2(dresultB) = 0

#define Dint_isone_lowp2(dresultB)  (Dintp2(dresultB) & 01)

#define Dint_increment(dresultA,dresultB) 		\
    if ((++Dintp2(dresultB))==0) Dintp1(dresultA)++

#define Dint_decrement(dresultA,dresultB) 		\
    if ((Dintp2(dresultB)--)==0) Dintp1(dresultA)--

#define Dint_negate(dresultA,dresultB)			\
    Dintp1(dresultA) = ~Dintp1(dresultA);		\
    if ((Dintp2(dresultB) = -Dintp2(dresultB))==0) Dintp1(dresultA)++

#define Dint_copyfromptr(src,destA,destB) \
     Dintp1(destA) = src->wd0;		\
     Dintp2(destB) = src->wd1
#define Dint_copytoptr(srcA,srcB,dest)	\
    dest->wd0 = Dintp1(srcA);		\
    dest->wd1 = Dintp2(srcB)


/* other macros  */

#define Find_ms_one_bit(value, position)	\
    {						\
	int var;				\
	for (var=8; var >=1; var >>= 1) {	\
	    if (value >> 32 - position)		\
		position -= var;		\
		else position += var;		\
	}					\
	if ((value >> 32 - position) == 0)	\
	    position--;				\
	else position -= 2;			\
    }


/*
 * Unsigned int macros
 */
#define Duint_copyfromptr(src,destA,destB) \
    Dint_copyfromptr(src,destA,destB)
#define Duint_copytoptr(srcA,srcB,dest)	\
    Dint_copytoptr(srcA,srcB,dest)

#define Suint_isinexact_to_sgl(int_value) \
    (int_value << 32 - SGL_EXP_LENGTH)

#define Sgl_roundnearest_from_suint(suint_value,sgl_value)		\
    if (suint_value & 1<<(SGL_EXP_LENGTH - 1))   /* round bit */	\
    	if ((suint_value << 33 - SGL_EXP_LENGTH) || Slow(sgl_value))	\
		Sall(sgl_value)++

#define Duint_isinexact_to_sgl(duint_valueA,duint_valueB)	\
    ((Duintp1(duint_valueA) << 32 - SGL_EXP_LENGTH) || Duintp2(duint_valueB))

#define Sgl_roundnearest_from_duint(duint_valueA,duint_valueB,sgl_value) \
    if (Duintp1(duint_valueA) & 1<<(SGL_EXP_LENGTH - 1))		\
    	if ((Duintp1(duint_valueA) << 33 - SGL_EXP_LENGTH) ||		\
    	Duintp2(duint_valueB) || Slow(sgl_value)) Sall(sgl_value)++

#define Duint_isinexact_to_dbl(duint_value) 	\
    (Duintp2(duint_value) << 32 - DBL_EXP_LENGTH)

#define Dbl_roundnearest_from_duint(duint_opndB,dbl_opndA,dbl_opndB) 	\
    if (Duintp2(duint_opndB) & 1<<(DBL_EXP_LENGTH - 1))			\
       if ((Duintp2(duint_opndB) << 33 - DBL_EXP_LENGTH) || Dlowp2(dbl_opndB)) \
          if ((++Dallp2(dbl_opndB))==0) Dallp1(dbl_opndA)++

#define Suint_from_sgl_mantissa(src,exponent,result)	\
    Sall(result) = (unsigned)(Sall(src) << SGL_EXP_LENGTH)>>(31 - exponent)

#define Sgl_isinexact_to_unsigned(sgl_value,exponent)	\
    Sgl_isinexact_to_fix(sgl_value,exponent)

#define Duint_from_sgl_mantissa(sgl_value,exponent,dresultA,dresultB)	\
  {unsigned int val = Sall(sgl_value) << SGL_EXP_LENGTH;		\
    if (exponent <= 31) {						\
	Dintp1(dresultA) = 0;						\
	Dintp2(dresultB) = val >> (31 - exponent);			\
    }									\
    else {								\
	Dintp1(dresultA) = val >> (63 - exponent);			\
	Dintp2(dresultB) = exponent <= 62 ? val << (exponent - 31) : 0;	\
    }									\
  }

#define Duint_setzero(dresultA,dresultB) 	\
    Dint_setzero(dresultA,dresultB)

#define Duint_increment(dresultA,dresultB) Dint_increment(dresultA,dresultB) 

#define Duint_isone_lowp2(dresultB)  Dint_isone_lowp2(dresultB)

#define Suint_from_dbl_mantissa(srcA,srcB,exponent,dest) \
    Shiftdouble(Dallp1(srcA),Dallp2(srcB),21,dest); \
    dest = (unsigned)dest >> 31 - exponent

#define Dbl_isinexact_to_unsigned(dbl_valueA,dbl_valueB,exponent) \
    Dbl_isinexact_to_fix(dbl_valueA,dbl_valueB,exponent)

#define Duint_from_dbl_mantissa(dbl_valueA,dbl_valueB,exponent,destA,destB) \
    Dint_from_dbl_mantissa(dbl_valueA,dbl_valueB,exponent,destA,destB) 
