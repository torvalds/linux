/*---------------------------------------------------------------------------+
 |  reg_compare.c                                                            |
 |                                                                           |
 | Compare two floating point registers                                      |
 |                                                                           |
 | Copyright (C) 1992,1993,1994,1997                                         |
 |                  W. Metzenthen, 22 Parker St, Ormond, Vic 3163, Australia |
 |                  E-mail   billm@suburbia.net                              |
 |                                                                           |
 |                                                                           |
 +---------------------------------------------------------------------------*/

/*---------------------------------------------------------------------------+
 | compare() is the core FPU_REG comparison function                         |
 +---------------------------------------------------------------------------*/

#include "fpu_system.h"
#include "exception.h"
#include "fpu_emu.h"
#include "control_w.h"
#include "status_w.h"


static int compare(FPU_REG const *b, int tagb)
{
  int diff, exp0, expb;
  u_char	  	st0_tag;
  FPU_REG  	*st0_ptr;
  FPU_REG	x, y;
  u_char		st0_sign, signb = getsign(b);

  st0_ptr = &st(0);
  st0_tag = FPU_gettag0();
  st0_sign = getsign(st0_ptr);

  if ( tagb == TAG_Special )
    tagb = FPU_Special(b);
  if ( st0_tag == TAG_Special )
    st0_tag = FPU_Special(st0_ptr);

  if ( ((st0_tag != TAG_Valid) && (st0_tag != TW_Denormal))
       || ((tagb != TAG_Valid) && (tagb != TW_Denormal)) )
    {
      if ( st0_tag == TAG_Zero )
	{
	  if ( tagb == TAG_Zero ) return COMP_A_eq_B;
	  if ( tagb == TAG_Valid )
	    return ((signb == SIGN_POS) ? COMP_A_lt_B : COMP_A_gt_B);
	  if ( tagb == TW_Denormal )
	    return ((signb == SIGN_POS) ? COMP_A_lt_B : COMP_A_gt_B)
	    | COMP_Denormal;
	}
      else if ( tagb == TAG_Zero )
	{
	  if ( st0_tag == TAG_Valid )
	    return ((st0_sign == SIGN_POS) ? COMP_A_gt_B : COMP_A_lt_B);
	  if ( st0_tag == TW_Denormal )
	    return ((st0_sign == SIGN_POS) ? COMP_A_gt_B : COMP_A_lt_B)
	    | COMP_Denormal;
	}

      if ( st0_tag == TW_Infinity )
	{
	  if ( (tagb == TAG_Valid) || (tagb == TAG_Zero) )
	    return ((st0_sign == SIGN_POS) ? COMP_A_gt_B : COMP_A_lt_B);
	  else if ( tagb == TW_Denormal )
	    return ((st0_sign == SIGN_POS) ? COMP_A_gt_B : COMP_A_lt_B)
	      | COMP_Denormal;
	  else if ( tagb == TW_Infinity )
	    {
	      /* The 80486 book says that infinities can be equal! */
	      return (st0_sign == signb) ? COMP_A_eq_B :
		((st0_sign == SIGN_POS) ? COMP_A_gt_B : COMP_A_lt_B);
	    }
	  /* Fall through to the NaN code */
	}
      else if ( tagb == TW_Infinity )
	{
	  if ( (st0_tag == TAG_Valid) || (st0_tag == TAG_Zero) )
	    return ((signb == SIGN_POS) ? COMP_A_lt_B : COMP_A_gt_B);
	  if ( st0_tag == TW_Denormal )
	    return ((signb == SIGN_POS) ? COMP_A_lt_B : COMP_A_gt_B)
		| COMP_Denormal;
	  /* Fall through to the NaN code */
	}

      /* The only possibility now should be that one of the arguments
	 is a NaN */
      if ( (st0_tag == TW_NaN) || (tagb == TW_NaN) )
	{
	  int signalling = 0, unsupported = 0;
	  if ( st0_tag == TW_NaN )
	    {
	      signalling = (st0_ptr->sigh & 0xc0000000) == 0x80000000;
	      unsupported = !((exponent(st0_ptr) == EXP_OVER)
			      && (st0_ptr->sigh & 0x80000000));
	    }
	  if ( tagb == TW_NaN )
	    {
	      signalling |= (b->sigh & 0xc0000000) == 0x80000000;
	      unsupported |= !((exponent(b) == EXP_OVER)
			       && (b->sigh & 0x80000000));
	    }
	  if ( signalling || unsupported )
	    return COMP_No_Comp | COMP_SNaN | COMP_NaN;
	  else
	    /* Neither is a signaling NaN */
	    return COMP_No_Comp | COMP_NaN;
	}
      
      EXCEPTION(EX_Invalid);
    }
  
  if (st0_sign != signb)
    {
      return ((st0_sign == SIGN_POS) ? COMP_A_gt_B : COMP_A_lt_B)
	| ( ((st0_tag == TW_Denormal) || (tagb == TW_Denormal)) ?
	    COMP_Denormal : 0);
    }

  if ( (st0_tag == TW_Denormal) || (tagb == TW_Denormal) )
    {
      FPU_to_exp16(st0_ptr, &x);
      FPU_to_exp16(b, &y);
      st0_ptr = &x;
      b = &y;
      exp0 = exponent16(st0_ptr);
      expb = exponent16(b);
    }
  else
    {
      exp0 = exponent(st0_ptr);
      expb = exponent(b);
    }

#ifdef PARANOID
  if (!(st0_ptr->sigh & 0x80000000)) EXCEPTION(EX_Invalid);
  if (!(b->sigh & 0x80000000)) EXCEPTION(EX_Invalid);
#endif /* PARANOID */

  diff = exp0 - expb;
  if ( diff == 0 )
    {
      diff = st0_ptr->sigh - b->sigh;  /* Works only if ms bits are
					      identical */
      if ( diff == 0 )
	{
	diff = st0_ptr->sigl > b->sigl;
	if ( diff == 0 )
	  diff = -(st0_ptr->sigl < b->sigl);
	}
    }

  if ( diff > 0 )
    {
      return ((st0_sign == SIGN_POS) ? COMP_A_gt_B : COMP_A_lt_B)
	| ( ((st0_tag == TW_Denormal) || (tagb == TW_Denormal)) ?
	    COMP_Denormal : 0);
    }
  if ( diff < 0 )
    {
      return ((st0_sign == SIGN_POS) ? COMP_A_lt_B : COMP_A_gt_B)
	| ( ((st0_tag == TW_Denormal) || (tagb == TW_Denormal)) ?
	    COMP_Denormal : 0);
    }

  return COMP_A_eq_B
    | ( ((st0_tag == TW_Denormal) || (tagb == TW_Denormal)) ?
	COMP_Denormal : 0);

}


/* This function requires that st(0) is not empty */
int FPU_compare_st_data(FPU_REG const *loaded_data, u_char loaded_tag)
{
  int f = 0, c;

  c = compare(loaded_data, loaded_tag);

  if (c & COMP_NaN)
    {
      EXCEPTION(EX_Invalid);
      f = SW_C3 | SW_C2 | SW_C0;
    }
  else
    switch (c & 7)
      {
      case COMP_A_lt_B:
	f = SW_C0;
	break;
      case COMP_A_eq_B:
	f = SW_C3;
	break;
      case COMP_A_gt_B:
	f = 0;
	break;
      case COMP_No_Comp:
	f = SW_C3 | SW_C2 | SW_C0;
	break;
#ifdef PARANOID
      default:
	EXCEPTION(EX_INTERNAL|0x121);
	f = SW_C3 | SW_C2 | SW_C0;
	break;
#endif /* PARANOID */
      }
  setcc(f);
  if (c & COMP_Denormal)
    {
      return denormal_operand() < 0;
    }
  return 0;
}


static int compare_st_st(int nr)
{
  int f = 0, c;
  FPU_REG *st_ptr;

  if ( !NOT_EMPTY(0) || !NOT_EMPTY(nr) )
    {
      setcc(SW_C3 | SW_C2 | SW_C0);
      /* Stack fault */
      EXCEPTION(EX_StackUnder);
      return !(control_word & CW_Invalid);
    }

  st_ptr = &st(nr);
  c = compare(st_ptr, FPU_gettagi(nr));
  if (c & COMP_NaN)
    {
      setcc(SW_C3 | SW_C2 | SW_C0);
      EXCEPTION(EX_Invalid);
      return !(control_word & CW_Invalid);
    }
  else
    switch (c & 7)
      {
      case COMP_A_lt_B:
	f = SW_C0;
	break;
      case COMP_A_eq_B:
	f = SW_C3;
	break;
      case COMP_A_gt_B:
	f = 0;
	break;
      case COMP_No_Comp:
	f = SW_C3 | SW_C2 | SW_C0;
	break;
#ifdef PARANOID
      default:
	EXCEPTION(EX_INTERNAL|0x122);
	f = SW_C3 | SW_C2 | SW_C0;
	break;
#endif /* PARANOID */
      }
  setcc(f);
  if (c & COMP_Denormal)
    {
      return denormal_operand() < 0;
    }
  return 0;
}


static int compare_u_st_st(int nr)
{
  int f = 0, c;
  FPU_REG *st_ptr;

  if ( !NOT_EMPTY(0) || !NOT_EMPTY(nr) )
    {
      setcc(SW_C3 | SW_C2 | SW_C0);
      /* Stack fault */
      EXCEPTION(EX_StackUnder);
      return !(control_word & CW_Invalid);
    }

  st_ptr = &st(nr);
  c = compare(st_ptr, FPU_gettagi(nr));
  if (c & COMP_NaN)
    {
      setcc(SW_C3 | SW_C2 | SW_C0);
      if (c & COMP_SNaN)       /* This is the only difference between
				  un-ordered and ordinary comparisons */
	{
	  EXCEPTION(EX_Invalid);
	  return !(control_word & CW_Invalid);
	}
      return 0;
    }
  else
    switch (c & 7)
      {
      case COMP_A_lt_B:
	f = SW_C0;
	break;
      case COMP_A_eq_B:
	f = SW_C3;
	break;
      case COMP_A_gt_B:
	f = 0;
	break;
      case COMP_No_Comp:
	f = SW_C3 | SW_C2 | SW_C0;
	break;
#ifdef PARANOID
      default:
	EXCEPTION(EX_INTERNAL|0x123);
	f = SW_C3 | SW_C2 | SW_C0;
	break;
#endif /* PARANOID */ 
      }
  setcc(f);
  if (c & COMP_Denormal)
    {
      return denormal_operand() < 0;
    }
  return 0;
}

/*---------------------------------------------------------------------------*/

void fcom_st(void)
{
  /* fcom st(i) */
  compare_st_st(FPU_rm);
}


void fcompst(void)
{
  /* fcomp st(i) */
  if ( !compare_st_st(FPU_rm) )
    FPU_pop();
}


void fcompp(void)
{
  /* fcompp */
  if (FPU_rm != 1)
    {
      FPU_illegal();
      return;
    }
  if ( !compare_st_st(1) )
      poppop();
}


void fucom_(void)
{
  /* fucom st(i) */
  compare_u_st_st(FPU_rm);

}


void fucomp(void)
{
  /* fucomp st(i) */
  if ( !compare_u_st_st(FPU_rm) )
    FPU_pop();
}


void fucompp(void)
{
  /* fucompp */
  if (FPU_rm == 1)
    {
      if ( !compare_u_st_st(1) )
	poppop();
    }
  else
    FPU_illegal();
}
