/* Subroutines used for code generation on IBM S/390 and zSeries
   Copyright (C) 1999, 2000, 2001, 2002, 2003, 2004, 2005, 2006
   Free Software Foundation, Inc.
   Contributed by Hartmut Penner (hpenner@de.ibm.com) and
                  Ulrich Weigand (uweigand@de.ibm.com).

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation; either version 2, or (at your option) any later
version.

GCC is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to the Free
Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA
02110-1301, USA.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "rtl.h"
#include "tree.h"
#include "tm_p.h"
#include "regs.h"
#include "hard-reg-set.h"
#include "real.h"
#include "insn-config.h"
#include "conditions.h"
#include "output.h"
#include "insn-attr.h"
#include "flags.h"
#include "except.h"
#include "function.h"
#include "recog.h"
#include "expr.h"
#include "reload.h"
#include "toplev.h"
#include "basic-block.h"
#include "integrate.h"
#include "ggc.h"
#include "target.h"
#include "target-def.h"
#include "debug.h"
#include "langhooks.h"
#include "optabs.h"
#include "tree-gimple.h"


/* Define the specific costs for a given cpu.  */

struct processor_costs 
{
  /* multiplication */
  const int m;        /* cost of an M instruction.  */
  const int mghi;     /* cost of an MGHI instruction.  */
  const int mh;       /* cost of an MH instruction.  */
  const int mhi;      /* cost of an MHI instruction.  */
  const int ml;       /* cost of an ML instruction.  */
  const int mr;       /* cost of an MR instruction.  */
  const int ms;       /* cost of an MS instruction.  */
  const int msg;      /* cost of an MSG instruction.  */
  const int msgf;     /* cost of an MSGF instruction.  */
  const int msgfr;    /* cost of an MSGFR instruction.  */
  const int msgr;     /* cost of an MSGR instruction.  */
  const int msr;      /* cost of an MSR instruction.  */
  const int mult_df;  /* cost of multiplication in DFmode.  */
  const int mxbr;
  /* square root */
  const int sqxbr;    /* cost of square root in TFmode.  */
  const int sqdbr;    /* cost of square root in DFmode.  */
  const int sqebr;    /* cost of square root in SFmode.  */
  /* multiply and add */
  const int madbr;    /* cost of multiply and add in DFmode.  */
  const int maebr;    /* cost of multiply and add in SFmode.  */
  /* division */
  const int dxbr;
  const int dxr;
  const int ddbr;
  const int ddr;
  const int debr;
  const int der;
  const int dlgr;
  const int dlr;
  const int dr;
  const int dsgfr;
  const int dsgr;
};

const struct processor_costs *s390_cost;

static const
struct processor_costs z900_cost = 
{
  COSTS_N_INSNS (5),     /* M     */
  COSTS_N_INSNS (10),    /* MGHI  */
  COSTS_N_INSNS (5),     /* MH    */
  COSTS_N_INSNS (4),     /* MHI   */
  COSTS_N_INSNS (5),     /* ML    */
  COSTS_N_INSNS (5),     /* MR    */
  COSTS_N_INSNS (4),     /* MS    */
  COSTS_N_INSNS (15),    /* MSG   */
  COSTS_N_INSNS (7),     /* MSGF  */
  COSTS_N_INSNS (7),     /* MSGFR */
  COSTS_N_INSNS (10),    /* MSGR  */
  COSTS_N_INSNS (4),     /* MSR   */
  COSTS_N_INSNS (7),     /* multiplication in DFmode */
  COSTS_N_INSNS (13),    /* MXBR */
  COSTS_N_INSNS (136),   /* SQXBR */
  COSTS_N_INSNS (44),    /* SQDBR */
  COSTS_N_INSNS (35),    /* SQEBR */
  COSTS_N_INSNS (18),    /* MADBR */
  COSTS_N_INSNS (13),    /* MAEBR */
  COSTS_N_INSNS (134),   /* DXBR */
  COSTS_N_INSNS (135),   /* DXR */
  COSTS_N_INSNS (30),    /* DDBR */
  COSTS_N_INSNS (30),    /* DDR  */
  COSTS_N_INSNS (27),    /* DEBR */
  COSTS_N_INSNS (26),    /* DER  */
  COSTS_N_INSNS (220),   /* DLGR */
  COSTS_N_INSNS (34),    /* DLR */
  COSTS_N_INSNS (34),    /* DR */
  COSTS_N_INSNS (32),    /* DSGFR */
  COSTS_N_INSNS (32),    /* DSGR */
};

static const
struct processor_costs z990_cost = 
{
  COSTS_N_INSNS (4),     /* M     */
  COSTS_N_INSNS (2),     /* MGHI  */
  COSTS_N_INSNS (2),     /* MH    */
  COSTS_N_INSNS (2),     /* MHI   */
  COSTS_N_INSNS (4),     /* ML    */
  COSTS_N_INSNS (4),     /* MR    */
  COSTS_N_INSNS (5),     /* MS    */
  COSTS_N_INSNS (6),     /* MSG   */
  COSTS_N_INSNS (4),     /* MSGF  */
  COSTS_N_INSNS (4),     /* MSGFR */
  COSTS_N_INSNS (4),     /* MSGR  */
  COSTS_N_INSNS (4),     /* MSR   */
  COSTS_N_INSNS (1),     /* multiplication in DFmode */
  COSTS_N_INSNS (28),    /* MXBR */
  COSTS_N_INSNS (130),   /* SQXBR */
  COSTS_N_INSNS (66),    /* SQDBR */
  COSTS_N_INSNS (38),    /* SQEBR */
  COSTS_N_INSNS (1),     /* MADBR */
  COSTS_N_INSNS (1),     /* MAEBR */
  COSTS_N_INSNS (60),    /* DXBR */
  COSTS_N_INSNS (72),    /* DXR */
  COSTS_N_INSNS (40),    /* DDBR */
  COSTS_N_INSNS (44),    /* DDR  */
  COSTS_N_INSNS (26),    /* DDBR */
  COSTS_N_INSNS (28),    /* DER  */
  COSTS_N_INSNS (176),   /* DLGR */
  COSTS_N_INSNS (31),    /* DLR */
  COSTS_N_INSNS (31),    /* DR */
  COSTS_N_INSNS (31),    /* DSGFR */
  COSTS_N_INSNS (31),    /* DSGR */
};

static const
struct processor_costs z9_109_cost = 
{
  COSTS_N_INSNS (4),     /* M     */
  COSTS_N_INSNS (2),     /* MGHI  */
  COSTS_N_INSNS (2),     /* MH    */
  COSTS_N_INSNS (2),     /* MHI   */
  COSTS_N_INSNS (4),     /* ML    */
  COSTS_N_INSNS (4),     /* MR    */
  COSTS_N_INSNS (5),     /* MS    */
  COSTS_N_INSNS (6),     /* MSG   */
  COSTS_N_INSNS (4),     /* MSGF  */
  COSTS_N_INSNS (4),     /* MSGFR */
  COSTS_N_INSNS (4),     /* MSGR  */
  COSTS_N_INSNS (4),     /* MSR   */
  COSTS_N_INSNS (1),     /* multiplication in DFmode */
  COSTS_N_INSNS (28),    /* MXBR */
  COSTS_N_INSNS (130),   /* SQXBR */
  COSTS_N_INSNS (66),    /* SQDBR */
  COSTS_N_INSNS (38),    /* SQEBR */
  COSTS_N_INSNS (1),     /* MADBR */
  COSTS_N_INSNS (1),     /* MAEBR */
  COSTS_N_INSNS (60),    /* DXBR */
  COSTS_N_INSNS (72),    /* DXR */
  COSTS_N_INSNS (40),    /* DDBR */
  COSTS_N_INSNS (37),    /* DDR  */
  COSTS_N_INSNS (26),    /* DDBR */
  COSTS_N_INSNS (28),    /* DER  */
  COSTS_N_INSNS (30),    /* DLGR */
  COSTS_N_INSNS (23),    /* DLR */
  COSTS_N_INSNS (23),    /* DR */
  COSTS_N_INSNS (24),    /* DSGFR */
  COSTS_N_INSNS (24),    /* DSGR */
};

extern int reload_completed;

/* Save information from a "cmpxx" operation until the branch or scc is
   emitted.  */
rtx s390_compare_op0, s390_compare_op1;

/* Save the result of a compare_and_swap  until the branch or scc is
   emitted.  */
rtx s390_compare_emitted = NULL_RTX;

/* Structure used to hold the components of a S/390 memory
   address.  A legitimate address on S/390 is of the general
   form
          base + index + displacement
   where any of the components is optional.

   base and index are registers of the class ADDR_REGS,
   displacement is an unsigned 12-bit immediate constant.  */

struct s390_address
{
  rtx base;
  rtx indx;
  rtx disp;
  bool pointer;
  bool literal_pool;
};

/* Which cpu are we tuning for.  */
enum processor_type s390_tune = PROCESSOR_max;
enum processor_flags s390_tune_flags;
/* Which instruction set architecture to use.  */
enum processor_type s390_arch;
enum processor_flags s390_arch_flags;

HOST_WIDE_INT s390_warn_framesize = 0;
HOST_WIDE_INT s390_stack_size = 0;
HOST_WIDE_INT s390_stack_guard = 0;

/* The following structure is embedded in the machine 
   specific part of struct function.  */

struct s390_frame_layout GTY (())
{
  /* Offset within stack frame.  */
  HOST_WIDE_INT gprs_offset;
  HOST_WIDE_INT f0_offset;
  HOST_WIDE_INT f4_offset;
  HOST_WIDE_INT f8_offset;
  HOST_WIDE_INT backchain_offset;

  /* Number of first and last gpr where slots in the register
     save area are reserved for.  */
  int first_save_gpr_slot;
  int last_save_gpr_slot;

  /* Number of first and last gpr to be saved, restored.  */
  int first_save_gpr;
  int first_restore_gpr;
  int last_save_gpr;
  int last_restore_gpr;

  /* Bits standing for floating point registers. Set, if the 
     respective register has to be saved. Starting with reg 16 (f0) 
     at the rightmost bit.
     Bit 15 -  8  7  6  5  4  3  2  1  0
     fpr 15 -  8  7  5  3  1  6  4  2  0
     reg 31 - 24 23 22 21 20 19 18 17 16  */
  unsigned int fpr_bitmap;

  /* Number of floating point registers f8-f15 which must be saved.  */
  int high_fprs;

  /* Set if return address needs to be saved.
     This flag is set by s390_return_addr_rtx if it could not use
     the initial value of r14 and therefore depends on r14 saved
     to the stack.  */
  bool save_return_addr_p;

  /* Size of stack frame.  */
  HOST_WIDE_INT frame_size;
};

/* Define the structure for the machine field in struct function.  */

struct machine_function GTY(())
{
  struct s390_frame_layout frame_layout;

  /* Literal pool base register.  */
  rtx base_reg;

  /* True if we may need to perform branch splitting.  */
  bool split_branches_pending_p;

  /* True during final stage of literal pool processing.  */
  bool decomposed_literal_pool_addresses_ok_p;

  /* Some local-dynamic TLS symbol name.  */
  const char *some_ld_name;

  bool has_landing_pad_p;
};

/* Few accessor macros for struct cfun->machine->s390_frame_layout.  */

#define cfun_frame_layout (cfun->machine->frame_layout)
#define cfun_save_high_fprs_p (!!cfun_frame_layout.high_fprs)
#define cfun_gprs_save_area_size ((cfun_frame_layout.last_save_gpr_slot -           \
  cfun_frame_layout.first_save_gpr_slot + 1) * UNITS_PER_WORD)
#define cfun_set_fpr_bit(BITNUM) (cfun->machine->frame_layout.fpr_bitmap |=    \
  (1 << (BITNUM)))
#define cfun_fpr_bit_p(BITNUM) (!!(cfun->machine->frame_layout.fpr_bitmap &    \
  (1 << (BITNUM))))

/* Number of GPRs and FPRs used for argument passing.  */
#define GP_ARG_NUM_REG 5
#define FP_ARG_NUM_REG (TARGET_64BIT? 4 : 2)

/* A couple of shortcuts.  */
#define CONST_OK_FOR_J(x) \
	CONST_OK_FOR_CONSTRAINT_P((x), 'J', "J")
#define CONST_OK_FOR_K(x) \
	CONST_OK_FOR_CONSTRAINT_P((x), 'K', "K")
#define CONST_OK_FOR_Os(x) \
        CONST_OK_FOR_CONSTRAINT_P((x), 'O', "Os")
#define CONST_OK_FOR_Op(x) \
        CONST_OK_FOR_CONSTRAINT_P((x), 'O', "Op")
#define CONST_OK_FOR_On(x) \
        CONST_OK_FOR_CONSTRAINT_P((x), 'O', "On")

#define REGNO_PAIR_OK(REGNO, MODE)                               \
  (HARD_REGNO_NREGS ((REGNO), (MODE)) == 1 || !((REGNO) & 1))

/* Return true if the back end supports mode MODE.  */
static bool
s390_scalar_mode_supported_p (enum machine_mode mode)
{
  if (DECIMAL_FLOAT_MODE_P (mode))
    return true;
  else
    return default_scalar_mode_supported_p (mode);
}

/* Set the has_landing_pad_p flag in struct machine_function to VALUE.  */

void
s390_set_has_landing_pad_p (bool value)
{
  cfun->machine->has_landing_pad_p = value;
}

/* If two condition code modes are compatible, return a condition code
   mode which is compatible with both.  Otherwise, return
   VOIDmode.  */

static enum machine_mode
s390_cc_modes_compatible (enum machine_mode m1, enum machine_mode m2)
{
  if (m1 == m2)
    return m1;

  switch (m1)
    {
    case CCZmode:
      if (m2 == CCUmode || m2 == CCTmode || m2 == CCZ1mode
	  || m2 == CCSmode || m2 == CCSRmode || m2 == CCURmode)
        return m2;
      return VOIDmode;

    case CCSmode:
    case CCUmode:
    case CCTmode:
    case CCSRmode:
    case CCURmode:
    case CCZ1mode:
      if (m2 == CCZmode)
	return m1;
      
      return VOIDmode;

    default:
      return VOIDmode;
    }
  return VOIDmode;
}

/* Return true if SET either doesn't set the CC register, or else
   the source and destination have matching CC modes and that
   CC mode is at least as constrained as REQ_MODE.  */

static bool
s390_match_ccmode_set (rtx set, enum machine_mode req_mode)
{
  enum machine_mode set_mode;

  gcc_assert (GET_CODE (set) == SET);

  if (GET_CODE (SET_DEST (set)) != REG || !CC_REGNO_P (REGNO (SET_DEST (set))))
    return 1;

  set_mode = GET_MODE (SET_DEST (set));
  switch (set_mode)
    {
    case CCSmode:
    case CCSRmode:
    case CCUmode:
    case CCURmode:
    case CCLmode:
    case CCL1mode:
    case CCL2mode:
    case CCL3mode:
    case CCT1mode:
    case CCT2mode:
    case CCT3mode:
      if (req_mode != set_mode)
        return 0;
      break;

    case CCZmode:
      if (req_mode != CCSmode && req_mode != CCUmode && req_mode != CCTmode
	  && req_mode != CCSRmode && req_mode != CCURmode)
        return 0;
      break;

    case CCAPmode:
    case CCANmode:
      if (req_mode != CCAmode)
        return 0;
      break;

    default:
      gcc_unreachable ();
    }

  return (GET_MODE (SET_SRC (set)) == set_mode);
}

/* Return true if every SET in INSN that sets the CC register
   has source and destination with matching CC modes and that
   CC mode is at least as constrained as REQ_MODE.
   If REQ_MODE is VOIDmode, always return false.  */

bool
s390_match_ccmode (rtx insn, enum machine_mode req_mode)
{
  int i;

  /* s390_tm_ccmode returns VOIDmode to indicate failure.  */
  if (req_mode == VOIDmode)
    return false;

  if (GET_CODE (PATTERN (insn)) == SET)
    return s390_match_ccmode_set (PATTERN (insn), req_mode);

  if (GET_CODE (PATTERN (insn)) == PARALLEL)
      for (i = 0; i < XVECLEN (PATTERN (insn), 0); i++)
        {
          rtx set = XVECEXP (PATTERN (insn), 0, i);
          if (GET_CODE (set) == SET)
            if (!s390_match_ccmode_set (set, req_mode))
              return false;
        }

  return true;
}

/* If a test-under-mask instruction can be used to implement
   (compare (and ... OP1) OP2), return the CC mode required
   to do that.  Otherwise, return VOIDmode.
   MIXED is true if the instruction can distinguish between
   CC1 and CC2 for mixed selected bits (TMxx), it is false
   if the instruction cannot (TM).  */

enum machine_mode
s390_tm_ccmode (rtx op1, rtx op2, bool mixed)
{
  int bit0, bit1;

  /* ??? Fixme: should work on CONST_DOUBLE as well.  */
  if (GET_CODE (op1) != CONST_INT || GET_CODE (op2) != CONST_INT)
    return VOIDmode;

  /* Selected bits all zero: CC0.
     e.g.: int a; if ((a & (16 + 128)) == 0) */
  if (INTVAL (op2) == 0)
    return CCTmode;

  /* Selected bits all one: CC3. 
     e.g.: int a; if ((a & (16 + 128)) == 16 + 128) */
  if (INTVAL (op2) == INTVAL (op1))
    return CCT3mode;

  /* Exactly two bits selected, mixed zeroes and ones: CC1 or CC2. e.g.:
     int a;
     if ((a & (16 + 128)) == 16)         -> CCT1
     if ((a & (16 + 128)) == 128)        -> CCT2  */
  if (mixed)
    {
      bit1 = exact_log2 (INTVAL (op2));
      bit0 = exact_log2 (INTVAL (op1) ^ INTVAL (op2));
      if (bit0 != -1 && bit1 != -1)
        return bit0 > bit1 ? CCT1mode : CCT2mode;
    }

  return VOIDmode;
}

/* Given a comparison code OP (EQ, NE, etc.) and the operands
   OP0 and OP1 of a COMPARE, return the mode to be used for the
   comparison.  */

enum machine_mode
s390_select_ccmode (enum rtx_code code, rtx op0, rtx op1)
{
  switch (code)
    {
      case EQ:
      case NE:
	if ((GET_CODE (op0) == NEG || GET_CODE (op0) == ABS)
	    && GET_MODE_CLASS (GET_MODE (op0)) == MODE_INT)
	  return CCAPmode;
	if (GET_CODE (op0) == PLUS && GET_CODE (XEXP (op0, 1)) == CONST_INT
	    && CONST_OK_FOR_K (INTVAL (XEXP (op0, 1))))
	  return CCAPmode;
	if ((GET_CODE (op0) == PLUS || GET_CODE (op0) == MINUS
	     || GET_CODE (op1) == NEG)
	    && GET_MODE_CLASS (GET_MODE (op0)) == MODE_INT)
	  return CCLmode;

	if (GET_CODE (op0) == AND)
	  {
	    /* Check whether we can potentially do it via TM.  */
	    enum machine_mode ccmode;
	    ccmode = s390_tm_ccmode (XEXP (op0, 1), op1, 1);
	    if (ccmode != VOIDmode)
	      {
		/* Relax CCTmode to CCZmode to allow fall-back to AND
		   if that turns out to be beneficial.  */
	        return ccmode == CCTmode ? CCZmode : ccmode;
	      }
	  }

	if (register_operand (op0, HImode)
	    && GET_CODE (op1) == CONST_INT
	    && (INTVAL (op1) == -1 || INTVAL (op1) == 65535))
	  return CCT3mode;
	if (register_operand (op0, QImode)
	    && GET_CODE (op1) == CONST_INT
	    && (INTVAL (op1) == -1 || INTVAL (op1) == 255))
	  return CCT3mode;

	return CCZmode;

      case LE:
      case LT:
      case GE:
      case GT:
	/* The only overflow condition of NEG and ABS happens when
	   -INT_MAX is used as parameter, which stays negative. So
	   we have an overflow from a positive value to a negative. 
	   Using CCAP mode the resulting cc can be used for comparisons.  */
	if ((GET_CODE (op0) == NEG || GET_CODE (op0) == ABS)
	    && GET_MODE_CLASS (GET_MODE (op0)) == MODE_INT)
	  return CCAPmode;

 	/* If constants are involved in an add instruction it is possible to use
 	   the resulting cc for comparisons with zero. Knowing the sign of the
	   constant the overflow behavior gets predictable. e.g.:
 	     int a, b; if ((b = a + c) > 0)  
 	   with c as a constant value: c < 0 -> CCAN and c >= 0 -> CCAP  */
	if (GET_CODE (op0) == PLUS && GET_CODE (XEXP (op0, 1)) == CONST_INT
	    && CONST_OK_FOR_K (INTVAL (XEXP (op0, 1))))
	  {
	    if (INTVAL (XEXP((op0), 1)) < 0)
	      return CCANmode;
	    else
	      return CCAPmode;
	  }
	/* Fall through.  */
      case UNORDERED:
      case ORDERED:
      case UNEQ:
      case UNLE:
      case UNLT:
      case UNGE:
      case UNGT:
      case LTGT:
	if ((GET_CODE (op0) == SIGN_EXTEND || GET_CODE (op0) == ZERO_EXTEND)
	    && GET_CODE (op1) != CONST_INT)
	  return CCSRmode;
	return CCSmode;

      case LTU:
      case GEU:
	if (GET_CODE (op0) == PLUS
	    && GET_MODE_CLASS (GET_MODE (op0)) == MODE_INT)
	  return CCL1mode;

	if ((GET_CODE (op0) == SIGN_EXTEND || GET_CODE (op0) == ZERO_EXTEND)
	    && GET_CODE (op1) != CONST_INT)
	  return CCURmode;
	return CCUmode;

      case LEU:
      case GTU:
	if (GET_CODE (op0) == MINUS
	    && GET_MODE_CLASS (GET_MODE (op0)) == MODE_INT)
	  return CCL2mode;

	if ((GET_CODE (op0) == SIGN_EXTEND || GET_CODE (op0) == ZERO_EXTEND)
	    && GET_CODE (op1) != CONST_INT)
	  return CCURmode;
	return CCUmode;

      default:
	gcc_unreachable ();
    }
}

/* Replace the comparison OP0 CODE OP1 by a semantically equivalent one
   that we can implement more efficiently.  */

void
s390_canonicalize_comparison (enum rtx_code *code, rtx *op0, rtx *op1)
{
  /* Convert ZERO_EXTRACT back to AND to enable TM patterns.  */
  if ((*code == EQ || *code == NE)
      && *op1 == const0_rtx
      && GET_CODE (*op0) == ZERO_EXTRACT
      && GET_CODE (XEXP (*op0, 1)) == CONST_INT
      && GET_CODE (XEXP (*op0, 2)) == CONST_INT
      && SCALAR_INT_MODE_P (GET_MODE (XEXP (*op0, 0))))
    {
      rtx inner = XEXP (*op0, 0);
      HOST_WIDE_INT modesize = GET_MODE_BITSIZE (GET_MODE (inner));
      HOST_WIDE_INT len = INTVAL (XEXP (*op0, 1));
      HOST_WIDE_INT pos = INTVAL (XEXP (*op0, 2));

      if (len > 0 && len < modesize
	  && pos >= 0 && pos + len <= modesize
	  && modesize <= HOST_BITS_PER_WIDE_INT)
	{
	  unsigned HOST_WIDE_INT block;
	  block = ((unsigned HOST_WIDE_INT) 1 << len) - 1;
	  block <<= modesize - pos - len;

	  *op0 = gen_rtx_AND (GET_MODE (inner), inner,
			      gen_int_mode (block, GET_MODE (inner)));
	}
    }

  /* Narrow AND of memory against immediate to enable TM.  */
  if ((*code == EQ || *code == NE)
      && *op1 == const0_rtx
      && GET_CODE (*op0) == AND
      && GET_CODE (XEXP (*op0, 1)) == CONST_INT
      && SCALAR_INT_MODE_P (GET_MODE (XEXP (*op0, 0))))
    {
      rtx inner = XEXP (*op0, 0);
      rtx mask = XEXP (*op0, 1);

      /* Ignore paradoxical SUBREGs if all extra bits are masked out.  */
      if (GET_CODE (inner) == SUBREG
	  && SCALAR_INT_MODE_P (GET_MODE (SUBREG_REG (inner)))
	  && (GET_MODE_SIZE (GET_MODE (inner))
	      >= GET_MODE_SIZE (GET_MODE (SUBREG_REG (inner))))
	  && ((INTVAL (mask)
               & GET_MODE_MASK (GET_MODE (inner))
               & ~GET_MODE_MASK (GET_MODE (SUBREG_REG (inner))))
	      == 0))
	inner = SUBREG_REG (inner);

      /* Do not change volatile MEMs.  */
      if (MEM_P (inner) && !MEM_VOLATILE_P (inner))
	{
	  int part = s390_single_part (XEXP (*op0, 1),
				       GET_MODE (inner), QImode, 0);
	  if (part >= 0)
	    {
	      mask = gen_int_mode (s390_extract_part (mask, QImode, 0), QImode);
	      inner = adjust_address_nv (inner, QImode, part);
	      *op0 = gen_rtx_AND (QImode, inner, mask);
	    }
	}
    }

  /* Narrow comparisons against 0xffff to HImode if possible.  */
  if ((*code == EQ || *code == NE)
      && GET_CODE (*op1) == CONST_INT
      && INTVAL (*op1) == 0xffff
      && SCALAR_INT_MODE_P (GET_MODE (*op0))
      && (nonzero_bits (*op0, GET_MODE (*op0)) 
	  & ~(unsigned HOST_WIDE_INT) 0xffff) == 0)
    {
      *op0 = gen_lowpart (HImode, *op0);
      *op1 = constm1_rtx;
    }


  /* Remove redundant UNSPEC_CMPINT conversions if possible.  */
  if (GET_CODE (*op0) == UNSPEC
      && XINT (*op0, 1) == UNSPEC_CMPINT
      && XVECLEN (*op0, 0) == 1
      && GET_MODE (XVECEXP (*op0, 0, 0)) == CCUmode
      && GET_CODE (XVECEXP (*op0, 0, 0)) == REG
      && REGNO (XVECEXP (*op0, 0, 0)) == CC_REGNUM
      && *op1 == const0_rtx)
    {
      enum rtx_code new_code = UNKNOWN;
      switch (*code)
	{
	  case EQ: new_code = EQ;  break;
	  case NE: new_code = NE;  break;
	  case LT: new_code = GTU; break;
	  case GT: new_code = LTU; break;
	  case LE: new_code = GEU; break;
	  case GE: new_code = LEU; break;
	  default: break;
	}

      if (new_code != UNKNOWN)
	{
	  *op0 = XVECEXP (*op0, 0, 0);
	  *code = new_code;
	}
    }

  /* Simplify cascaded EQ, NE with const0_rtx.  */
  if ((*code == NE || *code == EQ)
      && (GET_CODE (*op0) == EQ || GET_CODE (*op0) == NE)
      && GET_MODE (*op0) == SImode
      && GET_MODE (XEXP (*op0, 0)) == CCZ1mode
      && REG_P (XEXP (*op0, 0))
      && XEXP (*op0, 1) == const0_rtx
      && *op1 == const0_rtx)
    {
      if ((*code == EQ && GET_CODE (*op0) == NE)
          || (*code == NE && GET_CODE (*op0) == EQ))
	*code = EQ;
      else
	*code = NE;
      *op0 = XEXP (*op0, 0);
    }

  /* Prefer register over memory as first operand.  */
  if (MEM_P (*op0) && REG_P (*op1))
    {
      rtx tem = *op0; *op0 = *op1; *op1 = tem;
      *code = swap_condition (*code);
    }
}

/* Emit a compare instruction suitable to implement the comparison
   OP0 CODE OP1.  Return the correct condition RTL to be placed in
   the IF_THEN_ELSE of the conditional branch testing the result.  */

rtx
s390_emit_compare (enum rtx_code code, rtx op0, rtx op1)
{
  enum machine_mode mode = s390_select_ccmode (code, op0, op1);
  rtx ret = NULL_RTX;

  /* Do not output a redundant compare instruction if a compare_and_swap
     pattern already computed the result and the machine modes are compatible.  */
  if (s390_compare_emitted 
      && (s390_cc_modes_compatible (GET_MODE (s390_compare_emitted), mode)
	  == GET_MODE (s390_compare_emitted)))
    ret = gen_rtx_fmt_ee (code, VOIDmode, s390_compare_emitted, const0_rtx); 
  else
    {
      rtx cc = gen_rtx_REG (mode, CC_REGNUM);
      
      emit_insn (gen_rtx_SET (VOIDmode, cc, gen_rtx_COMPARE (mode, op0, op1)));
      ret = gen_rtx_fmt_ee (code, VOIDmode, cc, const0_rtx); 
    }
  s390_compare_emitted = NULL_RTX;
  return ret;
}

/* Emit a SImode compare and swap instruction setting MEM to NEW if OLD
   matches CMP.
   Return the correct condition RTL to be placed in the IF_THEN_ELSE of the
   conditional branch testing the result.  */

static rtx
s390_emit_compare_and_swap (enum rtx_code code, rtx old, rtx mem, rtx cmp, rtx new)
{
  rtx ret;

  emit_insn (gen_sync_compare_and_swap_ccsi (old, mem, cmp, new));
  ret = gen_rtx_fmt_ee (code, VOIDmode, s390_compare_emitted, const0_rtx);

  s390_compare_emitted = NULL_RTX;

  return ret;
}

/* Emit a jump instruction to TARGET.  If COND is NULL_RTX, emit an
   unconditional jump, else a conditional jump under condition COND.  */

void
s390_emit_jump (rtx target, rtx cond)
{
  rtx insn;

  target = gen_rtx_LABEL_REF (VOIDmode, target);
  if (cond)
    target = gen_rtx_IF_THEN_ELSE (VOIDmode, cond, target, pc_rtx);

  insn = gen_rtx_SET (VOIDmode, pc_rtx, target);
  emit_jump_insn (insn);
}

/* Return branch condition mask to implement a branch
   specified by CODE.  Return -1 for invalid comparisons.  */

int
s390_branch_condition_mask (rtx code)
{
  const int CC0 = 1 << 3;
  const int CC1 = 1 << 2;
  const int CC2 = 1 << 1;
  const int CC3 = 1 << 0;

  gcc_assert (GET_CODE (XEXP (code, 0)) == REG);
  gcc_assert (REGNO (XEXP (code, 0)) == CC_REGNUM);
  gcc_assert (XEXP (code, 1) == const0_rtx);

  switch (GET_MODE (XEXP (code, 0)))
    {
    case CCZmode:
    case CCZ1mode:
      switch (GET_CODE (code))
        {
        case EQ:	return CC0;
	case NE:	return CC1 | CC2 | CC3;
	default:	return -1;
        }
      break;

    case CCT1mode:
      switch (GET_CODE (code))
        {
        case EQ:	return CC1;
	case NE:	return CC0 | CC2 | CC3;
	default:	return -1;
        }
      break;

    case CCT2mode:
      switch (GET_CODE (code))
        {
        case EQ:	return CC2;
	case NE:	return CC0 | CC1 | CC3;
	default:	return -1;
        }
      break;

    case CCT3mode:
      switch (GET_CODE (code))
        {
        case EQ:	return CC3;
	case NE:	return CC0 | CC1 | CC2;
	default:	return -1;
        }
      break;

    case CCLmode:
      switch (GET_CODE (code))
        {
        case EQ:	return CC0 | CC2;
	case NE:	return CC1 | CC3;
	default:	return -1;
        }
      break;

    case CCL1mode:
      switch (GET_CODE (code))
        {
	case LTU:	return CC2 | CC3;  /* carry */
	case GEU:	return CC0 | CC1;  /* no carry */
	default:	return -1;
        }
      break;

    case CCL2mode:
      switch (GET_CODE (code))
        {
	case GTU:	return CC0 | CC1;  /* borrow */
	case LEU:	return CC2 | CC3;  /* no borrow */
	default:	return -1;
        }
      break;

    case CCL3mode:
      switch (GET_CODE (code))
	{
	case EQ:	return CC0 | CC2;
	case NE:	return CC1 | CC3;
	case LTU:	return CC1;
	case GTU:	return CC3;
	case LEU:	return CC1 | CC2;
	case GEU:	return CC2 | CC3;
	default:	return -1;
	}

    case CCUmode:
      switch (GET_CODE (code))
        {
        case EQ:	return CC0;
        case NE:	return CC1 | CC2 | CC3;
        case LTU:	return CC1;
        case GTU:	return CC2;
        case LEU:	return CC0 | CC1;
        case GEU:	return CC0 | CC2;
	default:	return -1;
        }
      break;

    case CCURmode:
      switch (GET_CODE (code))
        {
        case EQ:	return CC0;
        case NE:	return CC2 | CC1 | CC3;
        case LTU:	return CC2;
        case GTU:	return CC1;
        case LEU:	return CC0 | CC2;
        case GEU:	return CC0 | CC1;
	default:	return -1;
        }
      break;

    case CCAPmode:
      switch (GET_CODE (code))
        {
        case EQ:	return CC0;
        case NE:	return CC1 | CC2 | CC3;
        case LT:	return CC1 | CC3;
        case GT:	return CC2;
        case LE:	return CC0 | CC1 | CC3;
        case GE:	return CC0 | CC2;
	default:	return -1;
        }
      break;

    case CCANmode:
      switch (GET_CODE (code))
        {
        case EQ:	return CC0;
        case NE:	return CC1 | CC2 | CC3;
        case LT:	return CC1;
        case GT:	return CC2 | CC3;
        case LE:	return CC0 | CC1;
        case GE:	return CC0 | CC2 | CC3;
	default:	return -1;
        }
      break;

    case CCSmode:
      switch (GET_CODE (code))
        {
        case EQ:	return CC0;
        case NE:	return CC1 | CC2 | CC3;
        case LT:	return CC1;
        case GT:	return CC2;
        case LE:	return CC0 | CC1;
        case GE:	return CC0 | CC2;
	case UNORDERED:	return CC3;
	case ORDERED:	return CC0 | CC1 | CC2;
	case UNEQ:	return CC0 | CC3;
        case UNLT:	return CC1 | CC3;
        case UNGT:	return CC2 | CC3;
        case UNLE:	return CC0 | CC1 | CC3;
        case UNGE:	return CC0 | CC2 | CC3;
	case LTGT:	return CC1 | CC2;
	default:	return -1;
        }
      break;

    case CCSRmode:
      switch (GET_CODE (code))
        {
        case EQ:	return CC0;
        case NE:	return CC2 | CC1 | CC3;
        case LT:	return CC2;
        case GT:	return CC1;
        case LE:	return CC0 | CC2;
        case GE:	return CC0 | CC1;
	case UNORDERED:	return CC3;
	case ORDERED:	return CC0 | CC2 | CC1;
	case UNEQ:	return CC0 | CC3;
        case UNLT:	return CC2 | CC3;
        case UNGT:	return CC1 | CC3;
        case UNLE:	return CC0 | CC2 | CC3;
        case UNGE:	return CC0 | CC1 | CC3;
	case LTGT:	return CC2 | CC1;
	default:	return -1;
        }
      break;

    default:
      return -1;
    }
}

/* If INV is false, return assembler mnemonic string to implement
   a branch specified by CODE.  If INV is true, return mnemonic
   for the corresponding inverted branch.  */

static const char *
s390_branch_condition_mnemonic (rtx code, int inv)
{
  static const char *const mnemonic[16] =
    {
      NULL, "o", "h", "nle",
      "l", "nhe", "lh", "ne",
      "e", "nlh", "he", "nl",
      "le", "nh", "no", NULL
    };

  int mask = s390_branch_condition_mask (code);
  gcc_assert (mask >= 0);

  if (inv)
    mask ^= 15;

  gcc_assert (mask >= 1 && mask <= 14);

  return mnemonic[mask];
}

/* Return the part of op which has a value different from def.
   The size of the part is determined by mode.
   Use this function only if you already know that op really
   contains such a part.  */

unsigned HOST_WIDE_INT
s390_extract_part (rtx op, enum machine_mode mode, int def)
{
  unsigned HOST_WIDE_INT value = 0;
  int max_parts = HOST_BITS_PER_WIDE_INT / GET_MODE_BITSIZE (mode);
  int part_bits = GET_MODE_BITSIZE (mode);
  unsigned HOST_WIDE_INT part_mask
    = ((unsigned HOST_WIDE_INT)1 << part_bits) - 1;
  int i;

  for (i = 0; i < max_parts; i++)
    {
      if (i == 0)
	value = (unsigned HOST_WIDE_INT) INTVAL (op);
      else
	value >>= part_bits;

      if ((value & part_mask) != (def & part_mask))
	return value & part_mask;
    }

  gcc_unreachable ();
}

/* If OP is an integer constant of mode MODE with exactly one
   part of mode PART_MODE unequal to DEF, return the number of that
   part. Otherwise, return -1.  */

int
s390_single_part (rtx op,
		  enum machine_mode mode,
		  enum machine_mode part_mode,
		  int def)
{
  unsigned HOST_WIDE_INT value = 0;
  int n_parts = GET_MODE_SIZE (mode) / GET_MODE_SIZE (part_mode);
  unsigned HOST_WIDE_INT part_mask
    = ((unsigned HOST_WIDE_INT)1 << GET_MODE_BITSIZE (part_mode)) - 1;
  int i, part = -1;

  if (GET_CODE (op) != CONST_INT)
    return -1;

  for (i = 0; i < n_parts; i++)
    {
      if (i == 0)
	value = (unsigned HOST_WIDE_INT) INTVAL (op);
      else
	value >>= GET_MODE_BITSIZE (part_mode);

      if ((value & part_mask) != (def & part_mask))
	{
	  if (part != -1)
	    return -1;
	  else
	    part = i;
	}
    }
  return part == -1 ? -1 : n_parts - 1 - part;
}

/* Check whether we can (and want to) split a double-word
   move in mode MODE from SRC to DST into two single-word
   moves, moving the subword FIRST_SUBWORD first.  */

bool
s390_split_ok_p (rtx dst, rtx src, enum machine_mode mode, int first_subword)
{
  /* Floating point registers cannot be split.  */
  if (FP_REG_P (src) || FP_REG_P (dst))
    return false;

  /* We don't need to split if operands are directly accessible.  */
  if (s_operand (src, mode) || s_operand (dst, mode))
    return false;

  /* Non-offsettable memory references cannot be split.  */
  if ((GET_CODE (src) == MEM && !offsettable_memref_p (src))
      || (GET_CODE (dst) == MEM && !offsettable_memref_p (dst)))
    return false;

  /* Moving the first subword must not clobber a register
     needed to move the second subword.  */
  if (register_operand (dst, mode))
    {
      rtx subreg = operand_subword (dst, first_subword, 0, mode);
      if (reg_overlap_mentioned_p (subreg, src))
        return false;
    }

  return true;
}

/* Return true if it can be proven that [MEM1, MEM1 + SIZE]
   and [MEM2, MEM2 + SIZE] do overlap and false
   otherwise.  */

bool
s390_overlap_p (rtx mem1, rtx mem2, HOST_WIDE_INT size)
{
  rtx addr1, addr2, addr_delta;
  HOST_WIDE_INT delta;

  if (GET_CODE (mem1) != MEM || GET_CODE (mem2) != MEM)
    return true;

  if (size == 0)
    return false;

  addr1 = XEXP (mem1, 0);
  addr2 = XEXP (mem2, 0);

  addr_delta = simplify_binary_operation (MINUS, Pmode, addr2, addr1);

  /* This overlapping check is used by peepholes merging memory block operations.
     Overlapping operations would otherwise be recognized by the S/390 hardware
     and would fall back to a slower implementation. Allowing overlapping 
     operations would lead to slow code but not to wrong code. Therefore we are
     somewhat optimistic if we cannot prove that the memory blocks are 
     overlapping.
     That's why we return false here although this may accept operations on
     overlapping memory areas.  */
  if (!addr_delta || GET_CODE (addr_delta) != CONST_INT)
    return false;

  delta = INTVAL (addr_delta);

  if (delta == 0
      || (delta > 0 && delta < size)
      || (delta < 0 && -delta < size))
    return true;

  return false;
}

/* Check whether the address of memory reference MEM2 equals exactly
   the address of memory reference MEM1 plus DELTA.  Return true if
   we can prove this to be the case, false otherwise.  */

bool
s390_offset_p (rtx mem1, rtx mem2, rtx delta)
{
  rtx addr1, addr2, addr_delta;

  if (GET_CODE (mem1) != MEM || GET_CODE (mem2) != MEM)
    return false;

  addr1 = XEXP (mem1, 0);
  addr2 = XEXP (mem2, 0);

  addr_delta = simplify_binary_operation (MINUS, Pmode, addr2, addr1);
  if (!addr_delta || !rtx_equal_p (addr_delta, delta))
    return false;

  return true;
}

/* Expand logical operator CODE in mode MODE with operands OPERANDS.  */

void
s390_expand_logical_operator (enum rtx_code code, enum machine_mode mode,
			      rtx *operands)
{
  enum machine_mode wmode = mode;
  rtx dst = operands[0];
  rtx src1 = operands[1];
  rtx src2 = operands[2];
  rtx op, clob, tem;

  /* If we cannot handle the operation directly, use a temp register.  */
  if (!s390_logical_operator_ok_p (operands))
    dst = gen_reg_rtx (mode);

  /* QImode and HImode patterns make sense only if we have a destination
     in memory.  Otherwise perform the operation in SImode.  */
  if ((mode == QImode || mode == HImode) && GET_CODE (dst) != MEM)
    wmode = SImode;

  /* Widen operands if required.  */
  if (mode != wmode)
    {
      if (GET_CODE (dst) == SUBREG
	  && (tem = simplify_subreg (wmode, dst, mode, 0)) != 0)
	dst = tem;
      else if (REG_P (dst))
	dst = gen_rtx_SUBREG (wmode, dst, 0);
      else
        dst = gen_reg_rtx (wmode);

      if (GET_CODE (src1) == SUBREG
	  && (tem = simplify_subreg (wmode, src1, mode, 0)) != 0)
	src1 = tem;
      else if (GET_MODE (src1) != VOIDmode)
	src1 = gen_rtx_SUBREG (wmode, force_reg (mode, src1), 0);

      if (GET_CODE (src2) == SUBREG
	  && (tem = simplify_subreg (wmode, src2, mode, 0)) != 0)
	src2 = tem;
      else if (GET_MODE (src2) != VOIDmode)
	src2 = gen_rtx_SUBREG (wmode, force_reg (mode, src2), 0);
    }

  /* Emit the instruction.  */
  op = gen_rtx_SET (VOIDmode, dst, gen_rtx_fmt_ee (code, wmode, src1, src2));
  clob = gen_rtx_CLOBBER (VOIDmode, gen_rtx_REG (CCmode, CC_REGNUM));
  emit_insn (gen_rtx_PARALLEL (VOIDmode, gen_rtvec (2, op, clob)));

  /* Fix up the destination if needed.  */
  if (dst != operands[0])
    emit_move_insn (operands[0], gen_lowpart (mode, dst));
}

/* Check whether OPERANDS are OK for a logical operation (AND, IOR, XOR).  */

bool
s390_logical_operator_ok_p (rtx *operands)
{
  /* If the destination operand is in memory, it needs to coincide
     with one of the source operands.  After reload, it has to be
     the first source operand.  */
  if (GET_CODE (operands[0]) == MEM)
    return rtx_equal_p (operands[0], operands[1])
	   || (!reload_completed && rtx_equal_p (operands[0], operands[2]));

  return true;
}

/* Narrow logical operation CODE of memory operand MEMOP with immediate
   operand IMMOP to switch from SS to SI type instructions.  */

void
s390_narrow_logical_operator (enum rtx_code code, rtx *memop, rtx *immop)
{
  int def = code == AND ? -1 : 0;
  HOST_WIDE_INT mask;
  int part;

  gcc_assert (GET_CODE (*memop) == MEM);
  gcc_assert (!MEM_VOLATILE_P (*memop));

  mask = s390_extract_part (*immop, QImode, def);
  part = s390_single_part (*immop, GET_MODE (*memop), QImode, def);
  gcc_assert (part >= 0);

  *memop = adjust_address (*memop, QImode, part);
  *immop = gen_int_mode (mask, QImode);
}


/* How to allocate a 'struct machine_function'.  */

static struct machine_function *
s390_init_machine_status (void)
{
  return ggc_alloc_cleared (sizeof (struct machine_function));
}

/* Change optimizations to be performed, depending on the
   optimization level.

   LEVEL is the optimization level specified; 2 if `-O2' is
   specified, 1 if `-O' is specified, and 0 if neither is specified.

   SIZE is nonzero if `-Os' is specified and zero otherwise.  */

void
optimization_options (int level ATTRIBUTE_UNUSED, int size ATTRIBUTE_UNUSED)
{
  /* ??? There are apparently still problems with -fcaller-saves.  */
  flag_caller_saves = 0;

  /* By default, always emit DWARF-2 unwind info.  This allows debugging
     without maintaining a stack frame back-chain.  */
  flag_asynchronous_unwind_tables = 1;

  /* Use MVCLE instructions to decrease code size if requested.  */
  if (size != 0)
    target_flags |= MASK_MVCLE;
}

/* Return true if ARG is the name of a processor.  Set *TYPE and *FLAGS
   to the associated processor_type and processor_flags if so.  */

static bool
s390_handle_arch_option (const char *arg,
			 enum processor_type *type,
			 enum processor_flags *flags)
{
  static struct pta
    {
      const char *const name;		/* processor name or nickname.  */
      const enum processor_type processor;
      const enum processor_flags flags;
    }
  const processor_alias_table[] =
    {
      {"g5", PROCESSOR_9672_G5, PF_IEEE_FLOAT},
      {"g6", PROCESSOR_9672_G6, PF_IEEE_FLOAT},
      {"z900", PROCESSOR_2064_Z900, PF_IEEE_FLOAT | PF_ZARCH},
      {"z990", PROCESSOR_2084_Z990, PF_IEEE_FLOAT | PF_ZARCH
				    | PF_LONG_DISPLACEMENT},
      {"z9-109", PROCESSOR_2094_Z9_109, PF_IEEE_FLOAT | PF_ZARCH
                                       | PF_LONG_DISPLACEMENT | PF_EXTIMM},
    };
  size_t i;

  for (i = 0; i < ARRAY_SIZE (processor_alias_table); i++)
    if (strcmp (arg, processor_alias_table[i].name) == 0)
      {
	*type = processor_alias_table[i].processor;
	*flags = processor_alias_table[i].flags;
	return true;
      }
  return false;
}

/* Implement TARGET_HANDLE_OPTION.  */

static bool
s390_handle_option (size_t code, const char *arg, int value ATTRIBUTE_UNUSED)
{
  switch (code)
    {
    case OPT_march_:
      return s390_handle_arch_option (arg, &s390_arch, &s390_arch_flags);

    case OPT_mstack_guard_:
      if (sscanf (arg, HOST_WIDE_INT_PRINT_DEC, &s390_stack_guard) != 1)
	return false;
      if (exact_log2 (s390_stack_guard) == -1)
	error ("stack guard value must be an exact power of 2");
      return true;

    case OPT_mstack_size_:
      if (sscanf (arg, HOST_WIDE_INT_PRINT_DEC, &s390_stack_size) != 1)
	return false;
      if (exact_log2 (s390_stack_size) == -1)
	error ("stack size must be an exact power of 2");
      return true;

    case OPT_mtune_:
      return s390_handle_arch_option (arg, &s390_tune, &s390_tune_flags);

    case OPT_mwarn_framesize_:
      return sscanf (arg, HOST_WIDE_INT_PRINT_DEC, &s390_warn_framesize) == 1;

    default:
      return true;
    }
}

void
override_options (void)
{
  /* Set up function hooks.  */
  init_machine_status = s390_init_machine_status;

  /* Architecture mode defaults according to ABI.  */
  if (!(target_flags_explicit & MASK_ZARCH))
    {
      if (TARGET_64BIT)
	target_flags |= MASK_ZARCH;
      else
	target_flags &= ~MASK_ZARCH;
    }

  /* Determine processor architectural level.  */
  if (!s390_arch_string)
    {
      s390_arch_string = TARGET_ZARCH? "z900" : "g5";
      s390_handle_arch_option (s390_arch_string, &s390_arch, &s390_arch_flags);
    }

  /* Determine processor to tune for.  */
  if (s390_tune == PROCESSOR_max)
    {
      s390_tune = s390_arch;
      s390_tune_flags = s390_arch_flags;
    }

  /* Sanity checks.  */
  if (TARGET_ZARCH && !(s390_arch_flags & PF_ZARCH))
    error ("z/Architecture mode not supported on %s", s390_arch_string);
  if (TARGET_64BIT && !TARGET_ZARCH)
    error ("64-bit ABI not supported in ESA/390 mode");

  /* Set processor cost function.  */
  if (s390_tune == PROCESSOR_2094_Z9_109)
    s390_cost = &z9_109_cost;
  else if (s390_tune == PROCESSOR_2084_Z990)
    s390_cost = &z990_cost;
  else
    s390_cost = &z900_cost;
  
  if (TARGET_BACKCHAIN && TARGET_PACKED_STACK && TARGET_HARD_FLOAT)
    error ("-mbackchain -mpacked-stack -mhard-float are not supported "
	   "in combination");

  if (s390_stack_size)
    {
      if (!s390_stack_guard)
	error ("-mstack-size implies use of -mstack-guard");
      else if (s390_stack_guard >= s390_stack_size)
	error ("stack size must be greater than the stack guard value");
      else if (s390_stack_size > 1 << 16)
	error ("stack size must not be greater than 64k");
    }
  else if (s390_stack_guard)
    error ("-mstack-guard implies use of -mstack-size"); 

#ifdef TARGET_DEFAULT_LONG_DOUBLE_128
  if (!(target_flags_explicit & MASK_LONG_DOUBLE_128))
    target_flags |= MASK_LONG_DOUBLE_128;
#endif
}

/* Map for smallest class containing reg regno.  */

const enum reg_class regclass_map[FIRST_PSEUDO_REGISTER] =
{ GENERAL_REGS, ADDR_REGS, ADDR_REGS, ADDR_REGS,
  ADDR_REGS,    ADDR_REGS, ADDR_REGS, ADDR_REGS,
  ADDR_REGS,    ADDR_REGS, ADDR_REGS, ADDR_REGS,
  ADDR_REGS,    ADDR_REGS, ADDR_REGS, ADDR_REGS,
  FP_REGS,      FP_REGS,   FP_REGS,   FP_REGS,
  FP_REGS,      FP_REGS,   FP_REGS,   FP_REGS,
  FP_REGS,      FP_REGS,   FP_REGS,   FP_REGS,
  FP_REGS,      FP_REGS,   FP_REGS,   FP_REGS,
  ADDR_REGS,    CC_REGS,   ADDR_REGS, ADDR_REGS,
  ACCESS_REGS,	ACCESS_REGS
};

/* Return attribute type of insn.  */

static enum attr_type
s390_safe_attr_type (rtx insn)
{
  if (recog_memoized (insn) >= 0)
    return get_attr_type (insn);
  else
    return TYPE_NONE;
}

/* Return true if DISP is a valid short displacement.  */

static bool
s390_short_displacement (rtx disp)
{
  /* No displacement is OK.  */
  if (!disp)
    return true;

  /* Integer displacement in range.  */
  if (GET_CODE (disp) == CONST_INT)
    return INTVAL (disp) >= 0 && INTVAL (disp) < 4096;

  /* GOT offset is not OK, the GOT can be large.  */
  if (GET_CODE (disp) == CONST
      && GET_CODE (XEXP (disp, 0)) == UNSPEC
      && (XINT (XEXP (disp, 0), 1) == UNSPEC_GOT
          || XINT (XEXP (disp, 0), 1) == UNSPEC_GOTNTPOFF))
    return false;

  /* All other symbolic constants are literal pool references,
     which are OK as the literal pool must be small.  */
  if (GET_CODE (disp) == CONST)
    return true;

  return false;
}

/* Decompose a RTL expression ADDR for a memory address into
   its components, returned in OUT.

   Returns false if ADDR is not a valid memory address, true
   otherwise.  If OUT is NULL, don't return the components,
   but check for validity only.

   Note: Only addresses in canonical form are recognized.
   LEGITIMIZE_ADDRESS should convert non-canonical forms to the
   canonical form so that they will be recognized.  */

static int
s390_decompose_address (rtx addr, struct s390_address *out)
{
  HOST_WIDE_INT offset = 0;
  rtx base = NULL_RTX;
  rtx indx = NULL_RTX;
  rtx disp = NULL_RTX;
  rtx orig_disp;
  bool pointer = false;
  bool base_ptr = false;
  bool indx_ptr = false;
  bool literal_pool = false;

  /* We may need to substitute the literal pool base register into the address
     below.  However, at this point we do not know which register is going to
     be used as base, so we substitute the arg pointer register.  This is going
     to be treated as holding a pointer below -- it shouldn't be used for any
     other purpose.  */
  rtx fake_pool_base = gen_rtx_REG (Pmode, ARG_POINTER_REGNUM);

  /* Decompose address into base + index + displacement.  */

  if (GET_CODE (addr) == REG || GET_CODE (addr) == UNSPEC)
    base = addr;

  else if (GET_CODE (addr) == PLUS)
    {
      rtx op0 = XEXP (addr, 0);
      rtx op1 = XEXP (addr, 1);
      enum rtx_code code0 = GET_CODE (op0);
      enum rtx_code code1 = GET_CODE (op1);

      if (code0 == REG || code0 == UNSPEC)
	{
	  if (code1 == REG || code1 == UNSPEC)
	    {
	      indx = op0;	/* index + base */
	      base = op1;
	    }

	  else
	    {
	      base = op0;	/* base + displacement */
	      disp = op1;
	    }
	}

      else if (code0 == PLUS)
	{
	  indx = XEXP (op0, 0);	/* index + base + disp */
	  base = XEXP (op0, 1);
	  disp = op1;
	}

      else
	{
	  return false;
	}
    }

  else
    disp = addr;		/* displacement */

  /* Extract integer part of displacement.  */
  orig_disp = disp;
  if (disp)
    {
      if (GET_CODE (disp) == CONST_INT)
	{
	  offset = INTVAL (disp);
	  disp = NULL_RTX;
	}
      else if (GET_CODE (disp) == CONST
	       && GET_CODE (XEXP (disp, 0)) == PLUS
	       && GET_CODE (XEXP (XEXP (disp, 0), 1)) == CONST_INT)
	{
	  offset = INTVAL (XEXP (XEXP (disp, 0), 1));
	  disp = XEXP (XEXP (disp, 0), 0);
	}
    }

  /* Strip off CONST here to avoid special case tests later.  */
  if (disp && GET_CODE (disp) == CONST)
    disp = XEXP (disp, 0);

  /* We can convert literal pool addresses to
     displacements by basing them off the base register.  */
  if (disp && GET_CODE (disp) == SYMBOL_REF && CONSTANT_POOL_ADDRESS_P (disp))
    {
      /* Either base or index must be free to hold the base register.  */
      if (!base)
        base = fake_pool_base, literal_pool = true;
      else if (!indx)
        indx = fake_pool_base, literal_pool = true;
      else
        return false;

      /* Mark up the displacement.  */
      disp = gen_rtx_UNSPEC (Pmode, gen_rtvec (1, disp),
			     UNSPEC_LTREL_OFFSET);
    }

  /* Validate base register.  */
  if (base)
    {
      if (GET_CODE (base) == UNSPEC)
	switch (XINT (base, 1))
	  {
	  case UNSPEC_LTREF:
	    if (!disp)
	      disp = gen_rtx_UNSPEC (Pmode, 
				     gen_rtvec (1, XVECEXP (base, 0, 0)),
				     UNSPEC_LTREL_OFFSET);
	    else
	      return false;

	    base = XVECEXP (base, 0, 1);
	    break;

	  case UNSPEC_LTREL_BASE:
	    if (XVECLEN (base, 0) == 1)
	      base = fake_pool_base, literal_pool = true;
	    else
	      base = XVECEXP (base, 0, 1);
	    break;

	  default:
	    return false;
	  }

      if (!REG_P (base) 
	  || (GET_MODE (base) != SImode 
	      && GET_MODE (base) != Pmode))
	return false;

      if (REGNO (base) == STACK_POINTER_REGNUM
	  || REGNO (base) == FRAME_POINTER_REGNUM
	  || ((reload_completed || reload_in_progress)
	      && frame_pointer_needed
	      && REGNO (base) == HARD_FRAME_POINTER_REGNUM)
	  || REGNO (base) == ARG_POINTER_REGNUM
          || (flag_pic
              && REGNO (base) == PIC_OFFSET_TABLE_REGNUM))
        pointer = base_ptr = true;

      if ((reload_completed || reload_in_progress)
	  && base == cfun->machine->base_reg)
        pointer = base_ptr = literal_pool = true;
    }

  /* Validate index register.  */
  if (indx)
    {
      if (GET_CODE (indx) == UNSPEC)
	switch (XINT (indx, 1))
	  {
	  case UNSPEC_LTREF:
	    if (!disp)
	      disp = gen_rtx_UNSPEC (Pmode, 
				     gen_rtvec (1, XVECEXP (indx, 0, 0)),
				     UNSPEC_LTREL_OFFSET);
	    else
	      return false;

	    indx = XVECEXP (indx, 0, 1);
	    break;

	  case UNSPEC_LTREL_BASE:
	    if (XVECLEN (indx, 0) == 1)
	      indx = fake_pool_base, literal_pool = true;
	    else
	      indx = XVECEXP (indx, 0, 1);
	    break;

	  default:
	    return false;
	  }

      if (!REG_P (indx) 
	  || (GET_MODE (indx) != SImode
	      && GET_MODE (indx) != Pmode))
	return false;

      if (REGNO (indx) == STACK_POINTER_REGNUM
	  || REGNO (indx) == FRAME_POINTER_REGNUM
	  || ((reload_completed || reload_in_progress)
	      && frame_pointer_needed
	      && REGNO (indx) == HARD_FRAME_POINTER_REGNUM)
	  || REGNO (indx) == ARG_POINTER_REGNUM
          || (flag_pic
              && REGNO (indx) == PIC_OFFSET_TABLE_REGNUM))
        pointer = indx_ptr = true;

      if ((reload_completed || reload_in_progress)
	  && indx == cfun->machine->base_reg)
        pointer = indx_ptr = literal_pool = true;
    }

  /* Prefer to use pointer as base, not index.  */
  if (base && indx && !base_ptr
      && (indx_ptr || (!REG_POINTER (base) && REG_POINTER (indx))))
    {
      rtx tmp = base;
      base = indx;
      indx = tmp;
    }

  /* Validate displacement.  */
  if (!disp)
    {
      /* If virtual registers are involved, the displacement will change later 
	 anyway as the virtual registers get eliminated.  This could make a 
	 valid displacement invalid, but it is more likely to make an invalid 
	 displacement valid, because we sometimes access the register save area 
	 via negative offsets to one of those registers.
	 Thus we don't check the displacement for validity here.  If after
	 elimination the displacement turns out to be invalid after all,
	 this is fixed up by reload in any case.  */
      if (base != arg_pointer_rtx 
	  && indx != arg_pointer_rtx 
	  && base != return_address_pointer_rtx 
	  && indx != return_address_pointer_rtx
	  && base != frame_pointer_rtx 
	  && indx != frame_pointer_rtx
	  && base != virtual_stack_vars_rtx 
	  && indx != virtual_stack_vars_rtx)
	if (!DISP_IN_RANGE (offset))
	  return false;
    }
  else
    {
      /* All the special cases are pointers.  */
      pointer = true;

      /* In the small-PIC case, the linker converts @GOT
         and @GOTNTPOFF offsets to possible displacements.  */
      if (GET_CODE (disp) == UNSPEC
          && (XINT (disp, 1) == UNSPEC_GOT
	      || XINT (disp, 1) == UNSPEC_GOTNTPOFF)
	  && flag_pic == 1)
        {
	  ;
        }

      /* Accept chunkified literal pool symbol references.  */
      else if (cfun && cfun->machine
	       && cfun->machine->decomposed_literal_pool_addresses_ok_p
	       && GET_CODE (disp) == MINUS
               && GET_CODE (XEXP (disp, 0)) == LABEL_REF
               && GET_CODE (XEXP (disp, 1)) == LABEL_REF)
        {
	  ;
        }

      /* Accept literal pool references.  */
      else if (GET_CODE (disp) == UNSPEC
	       && XINT (disp, 1) == UNSPEC_LTREL_OFFSET)
        {
	  orig_disp = gen_rtx_CONST (Pmode, disp);
	  if (offset)
	    {
	      /* If we have an offset, make sure it does not
		 exceed the size of the constant pool entry.  */
	      rtx sym = XVECEXP (disp, 0, 0);
	      if (offset >= GET_MODE_SIZE (get_pool_mode (sym)))
		return false;

              orig_disp = plus_constant (orig_disp, offset);
	    }
        }

      else
	return false;
    }

  if (!base && !indx)
    pointer = true;

  if (out)
    {
      out->base = base;
      out->indx = indx;
      out->disp = orig_disp;
      out->pointer = pointer;
      out->literal_pool = literal_pool;
    }

  return true;
}

/* Decompose a RTL expression OP for a shift count into its components,
   and return the base register in BASE and the offset in OFFSET.

   Return true if OP is a valid shift count, false if not.  */

bool
s390_decompose_shift_count (rtx op, rtx *base, HOST_WIDE_INT *offset)
{
  HOST_WIDE_INT off = 0;

  /* We can have an integer constant, an address register,
     or a sum of the two.  */
  if (GET_CODE (op) == CONST_INT)
    {
      off = INTVAL (op);
      op = NULL_RTX;
    }
  if (op && GET_CODE (op) == PLUS && GET_CODE (XEXP (op, 1)) == CONST_INT)
    {
      off = INTVAL (XEXP (op, 1));
      op = XEXP (op, 0);
    }
  while (op && GET_CODE (op) == SUBREG)
    op = SUBREG_REG (op);

  if (op && GET_CODE (op) != REG)
    return false;

  if (offset)
    *offset = off;
  if (base)
    *base = op;

   return true;
}


/* Return true if CODE is a valid address without index.  */

bool
s390_legitimate_address_without_index_p (rtx op)
{
  struct s390_address addr;

  if (!s390_decompose_address (XEXP (op, 0), &addr))
    return false;
  if (addr.indx)
    return false;

  return true;
}


/* Evaluates constraint strings described by the regular expression
   ([A|B](Q|R|S|T))|U|W and returns 1 if OP is a valid operand for the
   constraint given in STR, or 0 else.  */

int
s390_mem_constraint (const char *str, rtx op)
{
  struct s390_address addr;
  char c = str[0];

  /* Check for offsettable variants of memory constraints.  */
  if (c == 'A')
    {
      /* Only accept non-volatile MEMs.  */
      if (!MEM_P (op) || MEM_VOLATILE_P (op))
	return 0;

      if ((reload_completed || reload_in_progress)
	  ? !offsettable_memref_p (op) : !offsettable_nonstrict_memref_p (op))
	return 0;

      c = str[1];
    }

  /* Check for non-literal-pool variants of memory constraints.  */
  else if (c == 'B')
    {
      if (GET_CODE (op) != MEM)
	return 0;
      if (!s390_decompose_address (XEXP (op, 0), &addr))
	return 0;
      if (addr.literal_pool)
	return 0;

      c = str[1];
    }

  switch (c)
    {
    case 'Q':
      if (GET_CODE (op) != MEM)
	return 0;
      if (!s390_decompose_address (XEXP (op, 0), &addr))
	return 0;
      if (addr.indx)
	return 0;

      if (TARGET_LONG_DISPLACEMENT)
	{
	  if (!s390_short_displacement (addr.disp))
	    return 0;
	}
      break;

    case 'R':
      if (GET_CODE (op) != MEM)
	return 0;

      if (TARGET_LONG_DISPLACEMENT)
	{
	  if (!s390_decompose_address (XEXP (op, 0), &addr))
	    return 0;
	  if (!s390_short_displacement (addr.disp))
	    return 0;
	}
      break;

    case 'S':
      if (!TARGET_LONG_DISPLACEMENT)
	return 0;
      if (GET_CODE (op) != MEM)
	return 0;
      if (!s390_decompose_address (XEXP (op, 0), &addr))
	return 0;
      if (addr.indx)
	return 0;
      if (s390_short_displacement (addr.disp))
	return 0;
      break;

    case 'T':
      if (!TARGET_LONG_DISPLACEMENT)
	return 0;
      if (GET_CODE (op) != MEM)
	return 0;
      /* Any invalid address here will be fixed up by reload,
	 so accept it for the most generic constraint.  */
      if (s390_decompose_address (XEXP (op, 0), &addr)
	  && s390_short_displacement (addr.disp))
	return 0;
      break;

    case 'U':
      if (TARGET_LONG_DISPLACEMENT)
	{
	  if (!s390_decompose_address (op, &addr))
	    return 0;
	  if (!s390_short_displacement (addr.disp))
	    return 0;
	}
      break;

    case 'W':
      if (!TARGET_LONG_DISPLACEMENT)
	return 0;
      /* Any invalid address here will be fixed up by reload,
	 so accept it for the most generic constraint.  */
      if (s390_decompose_address (op, &addr)
	  && s390_short_displacement (addr.disp))
	return 0;
      break;

    case 'Y':
      /* Simply check for the basic form of a shift count.  Reload will
	 take care of making sure we have a proper base register.  */
      if (!s390_decompose_shift_count (op, NULL, NULL))
	return 0;
      break;

    default:
      return 0;
    }

  return 1;
}



/* Evaluates constraint strings starting with letter O.  Input
   parameter C is the second letter following the "O" in the constraint
   string. Returns 1 if VALUE meets the respective constraint and 0
   otherwise.  */

int
s390_O_constraint_str (const char c, HOST_WIDE_INT value)
{
  if (!TARGET_EXTIMM)
    return 0;

  switch (c)
    {
    case 's':
      return trunc_int_for_mode (value, SImode) == value;

    case 'p':
      return value == 0
	|| s390_single_part (GEN_INT (value), DImode, SImode, 0) == 1;

    case 'n':
      return value == -1
	|| s390_single_part (GEN_INT (value), DImode, SImode, -1) == 1;

    default:
      gcc_unreachable ();
    }
}


/* Evaluates constraint strings starting with letter N.  Parameter STR
   contains the letters following letter "N" in the constraint string.
   Returns true if VALUE matches the constraint.  */

int
s390_N_constraint_str (const char *str, HOST_WIDE_INT value)
{
  enum machine_mode mode, part_mode;
  int def;
  int part, part_goal;


  if (str[0] == 'x')
    part_goal = -1;
  else
    part_goal = str[0] - '0';

  switch (str[1])
    {
    case 'Q':
      part_mode = QImode;
      break;
    case 'H':
      part_mode = HImode;
      break;
    case 'S':
      part_mode = SImode;
      break;
    default:
      return 0;
    }

  switch (str[2])
    {
    case 'H':
      mode = HImode;
      break;
    case 'S':
      mode = SImode;
      break;
    case 'D':
      mode = DImode;
      break;
    default:
      return 0;
    }

  switch (str[3])
    {
    case '0':
      def = 0;
      break;
    case 'F':
      def = -1;
      break;
    default:
      return 0;
    }

  if (GET_MODE_SIZE (mode) <= GET_MODE_SIZE (part_mode))
    return 0;

  part = s390_single_part (GEN_INT (value), mode, part_mode, def);
  if (part < 0)
    return 0;
  if (part_goal != -1 && part_goal != part)
    return 0;

  return 1;
}


/* Returns true if the input parameter VALUE is a float zero.  */

int
s390_float_const_zero_p (rtx value)
{
  return (GET_MODE_CLASS (GET_MODE (value)) == MODE_FLOAT
	  && value == CONST0_RTX (GET_MODE (value)));
}


/* Compute a (partial) cost for rtx X.  Return true if the complete
   cost has been computed, and false if subexpressions should be
   scanned.  In either case, *TOTAL contains the cost result.  
   CODE contains GET_CODE (x), OUTER_CODE contains the code 
   of the superexpression of x.  */

static bool
s390_rtx_costs (rtx x, int code, int outer_code, int *total)
{
  switch (code)
    {
    case CONST:
    case CONST_INT:
    case LABEL_REF:
    case SYMBOL_REF:
    case CONST_DOUBLE:
    case MEM:
      *total = 0;
      return true;

    case ASHIFT:
    case ASHIFTRT:
    case LSHIFTRT:
    case ROTATE:
    case ROTATERT:
    case AND:
    case IOR:
    case XOR:
    case NEG:
    case NOT:
      *total = COSTS_N_INSNS (1);
      return false;

    case PLUS:
    case MINUS:
      /* Check for multiply and add.  */
      if ((GET_MODE (x) == DFmode || GET_MODE (x) == SFmode)
	  && GET_CODE (XEXP (x, 0)) == MULT
	  && TARGET_HARD_FLOAT && TARGET_IEEE_FLOAT && TARGET_FUSED_MADD)
	{
	  /* This is the multiply and add case.  */
	  if (GET_MODE (x) == DFmode)
	    *total = s390_cost->madbr;
	  else
	    *total = s390_cost->maebr;
	  *total += rtx_cost (XEXP (XEXP (x, 0), 0), MULT) 
	    + rtx_cost (XEXP (XEXP (x, 0), 1), MULT) 
	    + rtx_cost (XEXP (x, 1), code);
	  return true;  /* Do not do an additional recursive descent.  */
	}
      *total = COSTS_N_INSNS (1);
      return false;

    case MULT:      
      switch (GET_MODE (x))
	{
	case SImode:
	  {
	    rtx left = XEXP (x, 0);
	    rtx right = XEXP (x, 1);
	    if (GET_CODE (right) == CONST_INT
		&& CONST_OK_FOR_K (INTVAL (right)))
	      *total = s390_cost->mhi;
	    else if (GET_CODE (left) == SIGN_EXTEND)
	      *total = s390_cost->mh;
	    else
	      *total = s390_cost->ms;  /* msr, ms, msy */
	    break;
	  }
	case DImode:
	  {
	    rtx left = XEXP (x, 0);
	    rtx right = XEXP (x, 1);
	    if (TARGET_64BIT)
	      {
		if (GET_CODE (right) == CONST_INT
		    && CONST_OK_FOR_K (INTVAL (right)))
		  *total = s390_cost->mghi;
		else if (GET_CODE (left) == SIGN_EXTEND)
		  *total = s390_cost->msgf;
		else
		  *total = s390_cost->msg;  /* msgr, msg */
	      }
	    else /* TARGET_31BIT */
	      {
		if (GET_CODE (left) == SIGN_EXTEND
		    && GET_CODE (right) == SIGN_EXTEND)
		  /* mulsidi case: mr, m */
		  *total = s390_cost->m;
		else if (GET_CODE (left) == ZERO_EXTEND
			 && GET_CODE (right) == ZERO_EXTEND
			 && TARGET_CPU_ZARCH)
		  /* umulsidi case: ml, mlr */
		  *total = s390_cost->ml;
		else
		  /* Complex calculation is required.  */
		  *total = COSTS_N_INSNS (40);
	      }
	    break;
	  }
	case SFmode:
	case DFmode:
	  *total = s390_cost->mult_df;
	  break;
	case TFmode:
	  *total = s390_cost->mxbr;
	  break;
	default:
	  return false;
	}
      return false;

    case UDIV:
    case UMOD:
      if (GET_MODE (x) == TImode) 	       /* 128 bit division */
	*total = s390_cost->dlgr;
      else if (GET_MODE (x) == DImode)
	{
	  rtx right = XEXP (x, 1);
	  if (GET_CODE (right) == ZERO_EXTEND) /* 64 by 32 bit division */
	    *total = s390_cost->dlr;
	  else 	                               /* 64 by 64 bit division */
	    *total = s390_cost->dlgr;
	}
      else if (GET_MODE (x) == SImode)         /* 32 bit division */
	*total = s390_cost->dlr;
      return false;

    case DIV:
    case MOD:
      if (GET_MODE (x) == DImode)
	{
	  rtx right = XEXP (x, 1);
	  if (GET_CODE (right) == ZERO_EXTEND) /* 64 by 32 bit division */
	    if (TARGET_64BIT)
	      *total = s390_cost->dsgfr;
	    else
	      *total = s390_cost->dr;
	  else 	                               /* 64 by 64 bit division */
	    *total = s390_cost->dsgr;
	}
      else if (GET_MODE (x) == SImode)         /* 32 bit division */
	*total = s390_cost->dlr;
      else if (GET_MODE (x) == SFmode)
	{
	  if (TARGET_IEEE_FLOAT)
	    *total = s390_cost->debr;
	  else /* TARGET_IBM_FLOAT */
	    *total = s390_cost->der;
	}
      else if (GET_MODE (x) == DFmode)
	{
	  if (TARGET_IEEE_FLOAT)
	    *total = s390_cost->ddbr;
	  else /* TARGET_IBM_FLOAT */
	    *total = s390_cost->ddr;
	}
      else if (GET_MODE (x) == TFmode)
	{
	  if (TARGET_IEEE_FLOAT)
	    *total = s390_cost->dxbr;
	  else /* TARGET_IBM_FLOAT */
	    *total = s390_cost->dxr;
	}
      return false;

    case SQRT:
      if (GET_MODE (x) == SFmode)
	*total = s390_cost->sqebr;
      else if (GET_MODE (x) == DFmode)
	*total = s390_cost->sqdbr;
      else /* TFmode */
	*total = s390_cost->sqxbr;
      return false;

    case SIGN_EXTEND:
    case ZERO_EXTEND:
      if (outer_code == MULT || outer_code == DIV || outer_code == MOD
	  || outer_code == PLUS || outer_code == MINUS
	  || outer_code == COMPARE)
	*total = 0;
      return false;

    case COMPARE:
      *total = COSTS_N_INSNS (1);
      if (GET_CODE (XEXP (x, 0)) == AND
	  && GET_CODE (XEXP (x, 1)) == CONST_INT
	  && GET_CODE (XEXP (XEXP (x, 0), 1)) == CONST_INT)
	{
	  rtx op0 = XEXP (XEXP (x, 0), 0);
	  rtx op1 = XEXP (XEXP (x, 0), 1);
	  rtx op2 = XEXP (x, 1);

	  if (memory_operand (op0, GET_MODE (op0))
	      && s390_tm_ccmode (op1, op2, 0) != VOIDmode)
	    return true;
	  if (register_operand (op0, GET_MODE (op0))
	      && s390_tm_ccmode (op1, op2, 1) != VOIDmode)
	    return true;
	}
      return false;

    default:
      return false;
    }
}

/* Return the cost of an address rtx ADDR.  */

static int
s390_address_cost (rtx addr)
{
  struct s390_address ad;
  if (!s390_decompose_address (addr, &ad))
    return 1000;

  return ad.indx? COSTS_N_INSNS (1) + 1 : COSTS_N_INSNS (1);
}

/* If OP is a SYMBOL_REF of a thread-local symbol, return its TLS mode,
   otherwise return 0.  */

int
tls_symbolic_operand (rtx op)
{
  if (GET_CODE (op) != SYMBOL_REF)
    return 0;
  return SYMBOL_REF_TLS_MODEL (op);
}

/* Split DImode access register reference REG (on 64-bit) into its constituent
   low and high parts, and store them into LO and HI.  Note that gen_lowpart/
   gen_highpart cannot be used as they assume all registers are word-sized,
   while our access registers have only half that size.  */

void
s390_split_access_reg (rtx reg, rtx *lo, rtx *hi)
{
  gcc_assert (TARGET_64BIT);
  gcc_assert (ACCESS_REG_P (reg));
  gcc_assert (GET_MODE (reg) == DImode);
  gcc_assert (!(REGNO (reg) & 1));

  *lo = gen_rtx_REG (SImode, REGNO (reg) + 1);
  *hi = gen_rtx_REG (SImode, REGNO (reg));
}

/* Return true if OP contains a symbol reference */

bool
symbolic_reference_mentioned_p (rtx op)
{
  const char *fmt;
  int i;

  if (GET_CODE (op) == SYMBOL_REF || GET_CODE (op) == LABEL_REF)
    return 1;

  fmt = GET_RTX_FORMAT (GET_CODE (op));
  for (i = GET_RTX_LENGTH (GET_CODE (op)) - 1; i >= 0; i--)
    {
      if (fmt[i] == 'E')
	{
	  int j;

	  for (j = XVECLEN (op, i) - 1; j >= 0; j--)
	    if (symbolic_reference_mentioned_p (XVECEXP (op, i, j)))
	      return 1;
	}

      else if (fmt[i] == 'e' && symbolic_reference_mentioned_p (XEXP (op, i)))
	return 1;
    }

  return 0;
}

/* Return true if OP contains a reference to a thread-local symbol.  */

bool
tls_symbolic_reference_mentioned_p (rtx op)
{
  const char *fmt;
  int i;

  if (GET_CODE (op) == SYMBOL_REF)
    return tls_symbolic_operand (op);

  fmt = GET_RTX_FORMAT (GET_CODE (op));
  for (i = GET_RTX_LENGTH (GET_CODE (op)) - 1; i >= 0; i--)
    {
      if (fmt[i] == 'E')
	{
	  int j;

	  for (j = XVECLEN (op, i) - 1; j >= 0; j--)
	    if (tls_symbolic_reference_mentioned_p (XVECEXP (op, i, j)))
	      return true;
	}

      else if (fmt[i] == 'e' && tls_symbolic_reference_mentioned_p (XEXP (op, i)))
	return true;
    }

  return false;
}


/* Return true if OP is a legitimate general operand when
   generating PIC code.  It is given that flag_pic is on
   and that OP satisfies CONSTANT_P or is a CONST_DOUBLE.  */

int
legitimate_pic_operand_p (rtx op)
{
  /* Accept all non-symbolic constants.  */
  if (!SYMBOLIC_CONST (op))
    return 1;

  /* Reject everything else; must be handled
     via emit_symbolic_move.  */
  return 0;
}

/* Returns true if the constant value OP is a legitimate general operand.
   It is given that OP satisfies CONSTANT_P or is a CONST_DOUBLE.  */

int
legitimate_constant_p (rtx op)
{
  /* Accept all non-symbolic constants.  */
  if (!SYMBOLIC_CONST (op))
    return 1;

  /* Accept immediate LARL operands.  */
  if (TARGET_CPU_ZARCH && larl_operand (op, VOIDmode))
    return 1;

  /* Thread-local symbols are never legal constants.  This is
     so that emit_call knows that computing such addresses
     might require a function call.  */
  if (TLS_SYMBOLIC_CONST (op))
    return 0;

  /* In the PIC case, symbolic constants must *not* be
     forced into the literal pool.  We accept them here,
     so that they will be handled by emit_symbolic_move.  */
  if (flag_pic)
    return 1;

  /* All remaining non-PIC symbolic constants are
     forced into the literal pool.  */
  return 0;
}

/* Determine if it's legal to put X into the constant pool.  This
   is not possible if X contains the address of a symbol that is
   not constant (TLS) or not known at final link time (PIC).  */

static bool
s390_cannot_force_const_mem (rtx x)
{
  switch (GET_CODE (x))
    {
    case CONST_INT:
    case CONST_DOUBLE:
      /* Accept all non-symbolic constants.  */
      return false;

    case LABEL_REF:
      /* Labels are OK iff we are non-PIC.  */
      return flag_pic != 0;

    case SYMBOL_REF:
      /* 'Naked' TLS symbol references are never OK,
         non-TLS symbols are OK iff we are non-PIC.  */
      if (tls_symbolic_operand (x))
	return true;
      else
	return flag_pic != 0;

    case CONST:
      return s390_cannot_force_const_mem (XEXP (x, 0));
    case PLUS:
    case MINUS:
      return s390_cannot_force_const_mem (XEXP (x, 0))
	     || s390_cannot_force_const_mem (XEXP (x, 1));

    case UNSPEC:
      switch (XINT (x, 1))
	{
	/* Only lt-relative or GOT-relative UNSPECs are OK.  */
	case UNSPEC_LTREL_OFFSET:
	case UNSPEC_GOT:
	case UNSPEC_GOTOFF:
	case UNSPEC_PLTOFF:
	case UNSPEC_TLSGD:
	case UNSPEC_TLSLDM:
	case UNSPEC_NTPOFF:
	case UNSPEC_DTPOFF:
	case UNSPEC_GOTNTPOFF:
	case UNSPEC_INDNTPOFF:
	  return false;

	/* If the literal pool shares the code section, be put
	   execute template placeholders into the pool as well.  */
	case UNSPEC_INSN:
	  return TARGET_CPU_ZARCH;

	default:
	  return true;
	}
      break;

    default:
      gcc_unreachable ();
    }
}

/* Returns true if the constant value OP is a legitimate general
   operand during and after reload.  The difference to
   legitimate_constant_p is that this function will not accept
   a constant that would need to be forced to the literal pool
   before it can be used as operand.  */

bool
legitimate_reload_constant_p (rtx op)
{
  /* Accept la(y) operands.  */
  if (GET_CODE (op) == CONST_INT
      && DISP_IN_RANGE (INTVAL (op)))
    return true;

  /* Accept l(g)hi/l(g)fi operands.  */
  if (GET_CODE (op) == CONST_INT
      && (CONST_OK_FOR_K (INTVAL (op)) || CONST_OK_FOR_Os (INTVAL (op))))
    return true;

  /* Accept lliXX operands.  */
  if (TARGET_ZARCH
      && GET_CODE (op) == CONST_INT
      && trunc_int_for_mode (INTVAL (op), word_mode) == INTVAL (op)
      && s390_single_part (op, word_mode, HImode, 0) >= 0)
  return true;

  if (TARGET_EXTIMM
      && GET_CODE (op) == CONST_INT
      && trunc_int_for_mode (INTVAL (op), word_mode) == INTVAL (op)
      && s390_single_part (op, word_mode, SImode, 0) >= 0)
    return true;

  /* Accept larl operands.  */
  if (TARGET_CPU_ZARCH
      && larl_operand (op, VOIDmode))
    return true;

  /* Accept lzXX operands.  */
  if (GET_CODE (op) == CONST_DOUBLE
      && CONST_DOUBLE_OK_FOR_CONSTRAINT_P (op, 'G', "G"))
    return true;

  /* Accept double-word operands that can be split.  */
  if (GET_CODE (op) == CONST_INT
      && trunc_int_for_mode (INTVAL (op), word_mode) != INTVAL (op))
    {
      enum machine_mode dword_mode = word_mode == SImode ? DImode : TImode;
      rtx hi = operand_subword (op, 0, 0, dword_mode);
      rtx lo = operand_subword (op, 1, 0, dword_mode);
      return legitimate_reload_constant_p (hi)
	     && legitimate_reload_constant_p (lo);
    }

  /* Everything else cannot be handled without reload.  */
  return false;
}

/* Given an rtx OP being reloaded into a reg required to be in class CLASS,
   return the class of reg to actually use.  */

enum reg_class
s390_preferred_reload_class (rtx op, enum reg_class class)
{
  switch (GET_CODE (op))
    {
      /* Constants we cannot reload must be forced into the
	 literal pool.  */

      case CONST_DOUBLE:
      case CONST_INT:
	if (legitimate_reload_constant_p (op))
	  return class;
	else
	  return NO_REGS;

      /* If a symbolic constant or a PLUS is reloaded,
	 it is most likely being used as an address, so
	 prefer ADDR_REGS.  If 'class' is not a superset
	 of ADDR_REGS, e.g. FP_REGS, reject this reload.  */
      case PLUS:
      case LABEL_REF:
      case SYMBOL_REF:
      case CONST:
	if (reg_class_subset_p (ADDR_REGS, class))
          return ADDR_REGS;
	else
	  return NO_REGS;

      default:
	break;
    }

  return class;
}

/* Return the register class of a scratch register needed to
   load IN into a register of class CLASS in MODE.

   We need a temporary when loading a PLUS expression which
   is not a legitimate operand of the LOAD ADDRESS instruction.  */

enum reg_class
s390_secondary_input_reload_class (enum reg_class class,
				   enum machine_mode mode, rtx in)
{
  if (s390_plus_operand (in, mode))
    return ADDR_REGS;

  if (reg_classes_intersect_p (FP_REGS, class)
      && mode == TFmode
      && GET_CODE (in) == MEM
      && GET_CODE (XEXP (in, 0)) == PLUS
      && GET_CODE (XEXP (XEXP (in, 0), 1)) == CONST_INT
      && !DISP_IN_RANGE (INTVAL (XEXP (XEXP (in, 0), 1))
			 + GET_MODE_SIZE (mode) - 1))
    return ADDR_REGS;

  if (reg_classes_intersect_p (CC_REGS, class))
    return GENERAL_REGS;

  return NO_REGS;
}

/* Return the register class of a scratch register needed to
   store a register of class CLASS in MODE into OUT:

   We need a temporary when storing a double-word to a
   non-offsettable memory address.  */

enum reg_class
s390_secondary_output_reload_class (enum reg_class class,
				    enum machine_mode mode, rtx out)
{
  if ((TARGET_64BIT ? (mode == TImode || mode == TFmode)
                    : (mode == DImode || mode == DFmode))
      && reg_classes_intersect_p (GENERAL_REGS, class)
      && GET_CODE (out) == MEM
      && GET_CODE (XEXP (out, 0)) == PLUS
      && GET_CODE (XEXP (XEXP (out, 0), 0)) == PLUS
      && GET_CODE (XEXP (XEXP (out, 0), 1)) == CONST_INT
      && !DISP_IN_RANGE (INTVAL (XEXP (XEXP (out, 0), 1))
			 + GET_MODE_SIZE (mode) - 1))
    return ADDR_REGS;

  if (reg_classes_intersect_p (FP_REGS, class)
      && mode == TFmode
      && GET_CODE (out) == MEM
      && GET_CODE (XEXP (out, 0)) == PLUS
      && GET_CODE (XEXP (XEXP (out, 0), 1)) == CONST_INT
      && !DISP_IN_RANGE (INTVAL (XEXP (XEXP (out, 0), 1))
			 + GET_MODE_SIZE (mode) - 1))
    return ADDR_REGS;

  if (reg_classes_intersect_p (CC_REGS, class))
    return GENERAL_REGS;

  return NO_REGS;
}

/* Generate code to load SRC, which is PLUS that is not a
   legitimate operand for the LA instruction, into TARGET.
   SCRATCH may be used as scratch register.  */

void
s390_expand_plus_operand (rtx target, rtx src,
			  rtx scratch)
{
  rtx sum1, sum2;
  struct s390_address ad;

  /* src must be a PLUS; get its two operands.  */
  gcc_assert (GET_CODE (src) == PLUS);
  gcc_assert (GET_MODE (src) == Pmode);

  /* Check if any of the two operands is already scheduled
     for replacement by reload.  This can happen e.g. when
     float registers occur in an address.  */
  sum1 = find_replacement (&XEXP (src, 0));
  sum2 = find_replacement (&XEXP (src, 1));
  src = gen_rtx_PLUS (Pmode, sum1, sum2);

  /* If the address is already strictly valid, there's nothing to do.  */
  if (!s390_decompose_address (src, &ad)
      || (ad.base && !REGNO_OK_FOR_BASE_P (REGNO (ad.base)))
      || (ad.indx && !REGNO_OK_FOR_INDEX_P (REGNO (ad.indx))))
    {
      /* Otherwise, one of the operands cannot be an address register;
         we reload its value into the scratch register.  */
      if (true_regnum (sum1) < 1 || true_regnum (sum1) > 15)
	{
	  emit_move_insn (scratch, sum1);
	  sum1 = scratch;
	}
      if (true_regnum (sum2) < 1 || true_regnum (sum2) > 15)
	{
	  emit_move_insn (scratch, sum2);
	  sum2 = scratch;
	}

      /* According to the way these invalid addresses are generated
         in reload.c, it should never happen (at least on s390) that
         *neither* of the PLUS components, after find_replacements
         was applied, is an address register.  */
      if (sum1 == scratch && sum2 == scratch)
	{
	  debug_rtx (src);
	  gcc_unreachable ();
	}

      src = gen_rtx_PLUS (Pmode, sum1, sum2);
    }

  /* Emit the LOAD ADDRESS pattern.  Note that reload of PLUS
     is only ever performed on addresses, so we can mark the
     sum as legitimate for LA in any case.  */
  s390_load_address (target, src);
}


/* Return true if ADDR is a valid memory address.
   STRICT specifies whether strict register checking applies.  */

bool
legitimate_address_p (enum machine_mode mode ATTRIBUTE_UNUSED,
		      rtx addr, int strict)
{
  struct s390_address ad;
  if (!s390_decompose_address (addr, &ad))
    return false;

  if (strict)
    {
      if (ad.base && !REGNO_OK_FOR_BASE_P (REGNO (ad.base)))
	return false;

      if (ad.indx && !REGNO_OK_FOR_INDEX_P (REGNO (ad.indx)))
	return false;
    }
  else
    {
      if (ad.base 
	  && !(REGNO (ad.base) >= FIRST_PSEUDO_REGISTER
	       || REGNO_REG_CLASS (REGNO (ad.base)) == ADDR_REGS))
	return false;
      
      if (ad.indx
	  && !(REGNO (ad.indx) >= FIRST_PSEUDO_REGISTER
	       || REGNO_REG_CLASS (REGNO (ad.indx)) == ADDR_REGS))
	  return false;
    }
  return true;
}

/* Return true if OP is a valid operand for the LA instruction.
   In 31-bit, we need to prove that the result is used as an
   address, as LA performs only a 31-bit addition.  */

bool
legitimate_la_operand_p (rtx op)
{
  struct s390_address addr;
  if (!s390_decompose_address (op, &addr))
    return false;

  return (TARGET_64BIT || addr.pointer);
}

/* Return true if it is valid *and* preferable to use LA to
   compute the sum of OP1 and OP2.  */

bool
preferred_la_operand_p (rtx op1, rtx op2)
{
  struct s390_address addr;

  if (op2 != const0_rtx)
    op1 = gen_rtx_PLUS (Pmode, op1, op2);

  if (!s390_decompose_address (op1, &addr))
    return false;
  if (addr.base && !REGNO_OK_FOR_BASE_P (REGNO (addr.base)))
    return false;
  if (addr.indx && !REGNO_OK_FOR_INDEX_P (REGNO (addr.indx)))
    return false;

  if (!TARGET_64BIT && !addr.pointer)
    return false;

  if (addr.pointer)
    return true;

  if ((addr.base && REG_P (addr.base) && REG_POINTER (addr.base))
      || (addr.indx && REG_P (addr.indx) && REG_POINTER (addr.indx)))
    return true;

  return false;
}

/* Emit a forced load-address operation to load SRC into DST.
   This will use the LOAD ADDRESS instruction even in situations
   where legitimate_la_operand_p (SRC) returns false.  */

void
s390_load_address (rtx dst, rtx src)
{
  if (TARGET_64BIT)
    emit_move_insn (dst, src);
  else
    emit_insn (gen_force_la_31 (dst, src));
}

/* Return a legitimate reference for ORIG (an address) using the
   register REG.  If REG is 0, a new pseudo is generated.

   There are two types of references that must be handled:

   1. Global data references must load the address from the GOT, via
      the PIC reg.  An insn is emitted to do this load, and the reg is
      returned.

   2. Static data references, constant pool addresses, and code labels
      compute the address as an offset from the GOT, whose base is in
      the PIC reg.  Static data objects have SYMBOL_FLAG_LOCAL set to
      differentiate them from global data objects.  The returned
      address is the PIC reg + an unspec constant.

   GO_IF_LEGITIMATE_ADDRESS rejects symbolic references unless the PIC
   reg also appears in the address.  */

rtx
legitimize_pic_address (rtx orig, rtx reg)
{
  rtx addr = orig;
  rtx new = orig;
  rtx base;

  gcc_assert (!TLS_SYMBOLIC_CONST (addr));

  if (GET_CODE (addr) == LABEL_REF
      || (GET_CODE (addr) == SYMBOL_REF && SYMBOL_REF_LOCAL_P (addr)))
    {
      /* This is a local symbol.  */
      if (TARGET_CPU_ZARCH && larl_operand (addr, VOIDmode))
        {
          /* Access local symbols PC-relative via LARL.
             This is the same as in the non-PIC case, so it is
             handled automatically ...  */
        }
      else
        {
          /* Access local symbols relative to the GOT.  */

          rtx temp = reg? reg : gen_reg_rtx (Pmode);

	  if (reload_in_progress || reload_completed)
	    regs_ever_live[PIC_OFFSET_TABLE_REGNUM] = 1;

          addr = gen_rtx_UNSPEC (Pmode, gen_rtvec (1, addr), UNSPEC_GOTOFF);
          addr = gen_rtx_CONST (Pmode, addr);
          addr = force_const_mem (Pmode, addr);
	  emit_move_insn (temp, addr);

          new = gen_rtx_PLUS (Pmode, pic_offset_table_rtx, temp);
          if (reg != 0)
            {
              s390_load_address (reg, new);
              new = reg;
            }
        }
    }
  else if (GET_CODE (addr) == SYMBOL_REF)
    {
      if (reg == 0)
        reg = gen_reg_rtx (Pmode);

      if (flag_pic == 1)
        {
          /* Assume GOT offset < 4k.  This is handled the same way
             in both 31- and 64-bit code (@GOT).  */

	  if (reload_in_progress || reload_completed)
	    regs_ever_live[PIC_OFFSET_TABLE_REGNUM] = 1;

          new = gen_rtx_UNSPEC (Pmode, gen_rtvec (1, addr), UNSPEC_GOT);
          new = gen_rtx_CONST (Pmode, new);
          new = gen_rtx_PLUS (Pmode, pic_offset_table_rtx, new);
          new = gen_const_mem (Pmode, new);
          emit_move_insn (reg, new);
          new = reg;
        }
      else if (TARGET_CPU_ZARCH)
        {
          /* If the GOT offset might be >= 4k, we determine the position
             of the GOT entry via a PC-relative LARL (@GOTENT).  */

          rtx temp = gen_reg_rtx (Pmode);

          new = gen_rtx_UNSPEC (Pmode, gen_rtvec (1, addr), UNSPEC_GOTENT);
          new = gen_rtx_CONST (Pmode, new);
          emit_move_insn (temp, new);

          new = gen_const_mem (Pmode, temp);
          emit_move_insn (reg, new);
          new = reg;
        }
      else
        {
          /* If the GOT offset might be >= 4k, we have to load it
             from the literal pool (@GOT).  */

          rtx temp = gen_reg_rtx (Pmode);

	  if (reload_in_progress || reload_completed)
	    regs_ever_live[PIC_OFFSET_TABLE_REGNUM] = 1;

          addr = gen_rtx_UNSPEC (Pmode, gen_rtvec (1, addr), UNSPEC_GOT);
          addr = gen_rtx_CONST (Pmode, addr);
          addr = force_const_mem (Pmode, addr);
          emit_move_insn (temp, addr);

          new = gen_rtx_PLUS (Pmode, pic_offset_table_rtx, temp);
          new = gen_const_mem (Pmode, new);
          emit_move_insn (reg, new);
          new = reg;
        }
    }
  else
    {
      if (GET_CODE (addr) == CONST)
	{
	  addr = XEXP (addr, 0);
	  if (GET_CODE (addr) == UNSPEC)
	    {
	      gcc_assert (XVECLEN (addr, 0) == 1);
              switch (XINT (addr, 1))
                {
                  /* If someone moved a GOT-relative UNSPEC
                     out of the literal pool, force them back in.  */
                  case UNSPEC_GOTOFF:
                  case UNSPEC_PLTOFF:
                    new = force_const_mem (Pmode, orig);
                    break;

                  /* @GOT is OK as is if small.  */
		  case UNSPEC_GOT:
		    if (flag_pic == 2)
		      new = force_const_mem (Pmode, orig);
		    break;

                  /* @GOTENT is OK as is.  */
                  case UNSPEC_GOTENT:
                    break;

                  /* @PLT is OK as is on 64-bit, must be converted to
                     GOT-relative @PLTOFF on 31-bit.  */
                  case UNSPEC_PLT:
                    if (!TARGET_CPU_ZARCH)
                      {
                        rtx temp = reg? reg : gen_reg_rtx (Pmode);

			if (reload_in_progress || reload_completed)
			  regs_ever_live[PIC_OFFSET_TABLE_REGNUM] = 1;

                        addr = XVECEXP (addr, 0, 0);
                        addr = gen_rtx_UNSPEC (Pmode, gen_rtvec (1, addr),
					       UNSPEC_PLTOFF);
                        addr = gen_rtx_CONST (Pmode, addr);
                        addr = force_const_mem (Pmode, addr);
	                emit_move_insn (temp, addr);

                        new = gen_rtx_PLUS (Pmode, pic_offset_table_rtx, temp);
                        if (reg != 0)
                          {
                            s390_load_address (reg, new);
                            new = reg;
                          }
                      }
                    break;

                  /* Everything else cannot happen.  */
                  default:
                    gcc_unreachable ();
                }
	    }
	  else 
	    gcc_assert (GET_CODE (addr) == PLUS);
	}
      if (GET_CODE (addr) == PLUS)
	{
	  rtx op0 = XEXP (addr, 0), op1 = XEXP (addr, 1);

	  gcc_assert (!TLS_SYMBOLIC_CONST (op0));
	  gcc_assert (!TLS_SYMBOLIC_CONST (op1));

	  /* Check first to see if this is a constant offset
             from a local symbol reference.  */
	  if ((GET_CODE (op0) == LABEL_REF
		|| (GET_CODE (op0) == SYMBOL_REF && SYMBOL_REF_LOCAL_P (op0)))
	      && GET_CODE (op1) == CONST_INT)
	    {
              if (TARGET_CPU_ZARCH
		  && larl_operand (op0, VOIDmode)
		  && INTVAL (op1) < (HOST_WIDE_INT)1 << 31
		  && INTVAL (op1) >= -((HOST_WIDE_INT)1 << 31))
                {
                  if (INTVAL (op1) & 1)
                    {
                      /* LARL can't handle odd offsets, so emit a
                         pair of LARL and LA.  */
                      rtx temp = reg? reg : gen_reg_rtx (Pmode);

                      if (!DISP_IN_RANGE (INTVAL (op1)))
                        {
                          HOST_WIDE_INT even = INTVAL (op1) - 1;
                          op0 = gen_rtx_PLUS (Pmode, op0, GEN_INT (even));
			  op0 = gen_rtx_CONST (Pmode, op0);
                          op1 = const1_rtx;
                        }

                      emit_move_insn (temp, op0);
                      new = gen_rtx_PLUS (Pmode, temp, op1);

                      if (reg != 0)
                        {
                          s390_load_address (reg, new);
                          new = reg;
                        }
                    }
                  else
                    {
                      /* If the offset is even, we can just use LARL.
                         This will happen automatically.  */
                    }
                }
              else
                {
                  /* Access local symbols relative to the GOT.  */

                  rtx temp = reg? reg : gen_reg_rtx (Pmode);

		  if (reload_in_progress || reload_completed)
		    regs_ever_live[PIC_OFFSET_TABLE_REGNUM] = 1;

                  addr = gen_rtx_UNSPEC (Pmode, gen_rtvec (1, op0),
					 UNSPEC_GOTOFF);
                  addr = gen_rtx_PLUS (Pmode, addr, op1);
                  addr = gen_rtx_CONST (Pmode, addr);
                  addr = force_const_mem (Pmode, addr);
		  emit_move_insn (temp, addr);

                  new = gen_rtx_PLUS (Pmode, pic_offset_table_rtx, temp);
                  if (reg != 0)
                    {
                      s390_load_address (reg, new);
                      new = reg;
                    }
                }
	    }

          /* Now, check whether it is a GOT relative symbol plus offset
             that was pulled out of the literal pool.  Force it back in.  */

	  else if (GET_CODE (op0) == UNSPEC
	           && GET_CODE (op1) == CONST_INT
	           && XINT (op0, 1) == UNSPEC_GOTOFF)
            {
	      gcc_assert (XVECLEN (op0, 0) == 1);

              new = force_const_mem (Pmode, orig);
            }

          /* Otherwise, compute the sum.  */
	  else
	    {
	      base = legitimize_pic_address (XEXP (addr, 0), reg);
	      new  = legitimize_pic_address (XEXP (addr, 1),
					     base == reg ? NULL_RTX : reg);
	      if (GET_CODE (new) == CONST_INT)
		new = plus_constant (base, INTVAL (new));
	      else
		{
		  if (GET_CODE (new) == PLUS && CONSTANT_P (XEXP (new, 1)))
		    {
		      base = gen_rtx_PLUS (Pmode, base, XEXP (new, 0));
		      new = XEXP (new, 1);
		    }
		  new = gen_rtx_PLUS (Pmode, base, new);
		}

	      if (GET_CODE (new) == CONST)
		new = XEXP (new, 0);
              new = force_operand (new, 0);
	    }
	}
    }
  return new;
}

/* Load the thread pointer into a register.  */

rtx
s390_get_thread_pointer (void)
{
  rtx tp = gen_reg_rtx (Pmode);

  emit_move_insn (tp, gen_rtx_REG (Pmode, TP_REGNUM));
  mark_reg_pointer (tp, BITS_PER_WORD);

  return tp;
}

/* Emit a tls call insn. The call target is the SYMBOL_REF stored
   in s390_tls_symbol which always refers to __tls_get_offset.
   The returned offset is written to RESULT_REG and an USE rtx is
   generated for TLS_CALL.  */

static GTY(()) rtx s390_tls_symbol;

static void
s390_emit_tls_call_insn (rtx result_reg, rtx tls_call)
{
  rtx insn;

  gcc_assert (flag_pic);

  if (!s390_tls_symbol)
    s390_tls_symbol = gen_rtx_SYMBOL_REF (Pmode, "__tls_get_offset");

  insn = s390_emit_call (s390_tls_symbol, tls_call, result_reg,
			 gen_rtx_REG (Pmode, RETURN_REGNUM));

  use_reg (&CALL_INSN_FUNCTION_USAGE (insn), result_reg);
  CONST_OR_PURE_CALL_P (insn) = 1;
}

/* ADDR contains a thread-local SYMBOL_REF.  Generate code to compute
   this (thread-local) address.  REG may be used as temporary.  */

static rtx
legitimize_tls_address (rtx addr, rtx reg)
{
  rtx new, tls_call, temp, base, r2, insn;

  if (GET_CODE (addr) == SYMBOL_REF)
    switch (tls_symbolic_operand (addr))
      {
      case TLS_MODEL_GLOBAL_DYNAMIC:
	start_sequence ();
	r2 = gen_rtx_REG (Pmode, 2);
	tls_call = gen_rtx_UNSPEC (Pmode, gen_rtvec (1, addr), UNSPEC_TLSGD);
	new = gen_rtx_CONST (Pmode, tls_call);
	new = force_const_mem (Pmode, new);
	emit_move_insn (r2, new);
	s390_emit_tls_call_insn (r2, tls_call);
	insn = get_insns ();
	end_sequence ();

	new = gen_rtx_UNSPEC (Pmode, gen_rtvec (1, addr), UNSPEC_NTPOFF);
	temp = gen_reg_rtx (Pmode);
	emit_libcall_block (insn, temp, r2, new);

	new = gen_rtx_PLUS (Pmode, s390_get_thread_pointer (), temp);
	if (reg != 0)
	  {
	    s390_load_address (reg, new);
	    new = reg;
	  }
	break;

      case TLS_MODEL_LOCAL_DYNAMIC:
	start_sequence ();
	r2 = gen_rtx_REG (Pmode, 2);
	tls_call = gen_rtx_UNSPEC (Pmode, gen_rtvec (1, const0_rtx), UNSPEC_TLSLDM);
	new = gen_rtx_CONST (Pmode, tls_call);
	new = force_const_mem (Pmode, new);
	emit_move_insn (r2, new);
	s390_emit_tls_call_insn (r2, tls_call);
	insn = get_insns ();
	end_sequence ();

	new = gen_rtx_UNSPEC (Pmode, gen_rtvec (1, const0_rtx), UNSPEC_TLSLDM_NTPOFF);
	temp = gen_reg_rtx (Pmode);
	emit_libcall_block (insn, temp, r2, new);

	new = gen_rtx_PLUS (Pmode, s390_get_thread_pointer (), temp);
	base = gen_reg_rtx (Pmode);
	s390_load_address (base, new);

	new = gen_rtx_UNSPEC (Pmode, gen_rtvec (1, addr), UNSPEC_DTPOFF);
	new = gen_rtx_CONST (Pmode, new);
	new = force_const_mem (Pmode, new);
	temp = gen_reg_rtx (Pmode);
	emit_move_insn (temp, new);

	new = gen_rtx_PLUS (Pmode, base, temp);
	if (reg != 0)
	  {
	    s390_load_address (reg, new);
	    new = reg;
	  }
	break;

      case TLS_MODEL_INITIAL_EXEC:
	if (flag_pic == 1)
	  {
	    /* Assume GOT offset < 4k.  This is handled the same way
	       in both 31- and 64-bit code.  */

	    if (reload_in_progress || reload_completed)
	      regs_ever_live[PIC_OFFSET_TABLE_REGNUM] = 1;

	    new = gen_rtx_UNSPEC (Pmode, gen_rtvec (1, addr), UNSPEC_GOTNTPOFF);
	    new = gen_rtx_CONST (Pmode, new);
	    new = gen_rtx_PLUS (Pmode, pic_offset_table_rtx, new);
	    new = gen_const_mem (Pmode, new);
	    temp = gen_reg_rtx (Pmode);
	    emit_move_insn (temp, new);
	  }
	else if (TARGET_CPU_ZARCH)
	  {
	    /* If the GOT offset might be >= 4k, we determine the position
	       of the GOT entry via a PC-relative LARL.  */

	    new = gen_rtx_UNSPEC (Pmode, gen_rtvec (1, addr), UNSPEC_INDNTPOFF);
	    new = gen_rtx_CONST (Pmode, new);
	    temp = gen_reg_rtx (Pmode);
	    emit_move_insn (temp, new);

	    new = gen_const_mem (Pmode, temp);
	    temp = gen_reg_rtx (Pmode);
	    emit_move_insn (temp, new);
	  }
	else if (flag_pic)
	  {
	    /* If the GOT offset might be >= 4k, we have to load it
	       from the literal pool.  */

	    if (reload_in_progress || reload_completed)
	      regs_ever_live[PIC_OFFSET_TABLE_REGNUM] = 1;

	    new = gen_rtx_UNSPEC (Pmode, gen_rtvec (1, addr), UNSPEC_GOTNTPOFF);
	    new = gen_rtx_CONST (Pmode, new);
	    new = force_const_mem (Pmode, new);
	    temp = gen_reg_rtx (Pmode);
	    emit_move_insn (temp, new);

            new = gen_rtx_PLUS (Pmode, pic_offset_table_rtx, temp);
	    new = gen_const_mem (Pmode, new);

	    new = gen_rtx_UNSPEC (Pmode, gen_rtvec (2, new, addr), UNSPEC_TLS_LOAD);
	    temp = gen_reg_rtx (Pmode);
	    emit_insn (gen_rtx_SET (Pmode, temp, new));
	  }
	else
	  {
	    /* In position-dependent code, load the absolute address of
	       the GOT entry from the literal pool.  */

	    new = gen_rtx_UNSPEC (Pmode, gen_rtvec (1, addr), UNSPEC_INDNTPOFF);
	    new = gen_rtx_CONST (Pmode, new);
	    new = force_const_mem (Pmode, new);
	    temp = gen_reg_rtx (Pmode);
	    emit_move_insn (temp, new);

	    new = temp;
	    new = gen_const_mem (Pmode, new);
	    new = gen_rtx_UNSPEC (Pmode, gen_rtvec (2, new, addr), UNSPEC_TLS_LOAD);
	    temp = gen_reg_rtx (Pmode);
	    emit_insn (gen_rtx_SET (Pmode, temp, new));
	  }

	new = gen_rtx_PLUS (Pmode, s390_get_thread_pointer (), temp);
	if (reg != 0)
	  {
	    s390_load_address (reg, new);
	    new = reg;
	  }
	break;

      case TLS_MODEL_LOCAL_EXEC:
	new = gen_rtx_UNSPEC (Pmode, gen_rtvec (1, addr), UNSPEC_NTPOFF);
	new = gen_rtx_CONST (Pmode, new);
	new = force_const_mem (Pmode, new);
        temp = gen_reg_rtx (Pmode);
	emit_move_insn (temp, new);

	new = gen_rtx_PLUS (Pmode, s390_get_thread_pointer (), temp);
	if (reg != 0)
	  {
	    s390_load_address (reg, new);
	    new = reg;
	  }
	break;

      default:
	gcc_unreachable ();
      }

  else if (GET_CODE (addr) == CONST && GET_CODE (XEXP (addr, 0)) == UNSPEC)
    {
      switch (XINT (XEXP (addr, 0), 1))
	{
	case UNSPEC_INDNTPOFF:
	  gcc_assert (TARGET_CPU_ZARCH);
	  new = addr;
	  break;

	default:
	  gcc_unreachable ();
	}
    }

  else if (GET_CODE (addr) == CONST && GET_CODE (XEXP (addr, 0)) == PLUS
	   && GET_CODE (XEXP (XEXP (addr, 0), 1)) == CONST_INT)
    {
      new = XEXP (XEXP (addr, 0), 0);
      if (GET_CODE (new) != SYMBOL_REF)
	new = gen_rtx_CONST (Pmode, new);

      new = legitimize_tls_address (new, reg);
      new = plus_constant (new, INTVAL (XEXP (XEXP (addr, 0), 1)));
      new = force_operand (new, 0);
    }

  else
    gcc_unreachable ();  /* for now ... */

  return new;
}

/* Emit insns to move operands[1] into operands[0].  */

void
emit_symbolic_move (rtx *operands)
{
  rtx temp = no_new_pseudos ? operands[0] : gen_reg_rtx (Pmode);

  if (GET_CODE (operands[0]) == MEM)
    operands[1] = force_reg (Pmode, operands[1]);
  else if (TLS_SYMBOLIC_CONST (operands[1]))
    operands[1] = legitimize_tls_address (operands[1], temp);
  else if (flag_pic)
    operands[1] = legitimize_pic_address (operands[1], temp);
}

/* Try machine-dependent ways of modifying an illegitimate address X
   to be legitimate.  If we find one, return the new, valid address.

   OLDX is the address as it was before break_out_memory_refs was called.
   In some cases it is useful to look at this to decide what needs to be done.

   MODE is the mode of the operand pointed to by X.

   When -fpic is used, special handling is needed for symbolic references.
   See comments by legitimize_pic_address for details.  */

rtx
legitimize_address (rtx x, rtx oldx ATTRIBUTE_UNUSED,
		    enum machine_mode mode ATTRIBUTE_UNUSED)
{
  rtx constant_term = const0_rtx;

  if (TLS_SYMBOLIC_CONST (x))
    {
      x = legitimize_tls_address (x, 0);

      if (legitimate_address_p (mode, x, FALSE))
	return x;
    }
  else if (GET_CODE (x) == PLUS
	   && (TLS_SYMBOLIC_CONST (XEXP (x, 0)) 
	       || TLS_SYMBOLIC_CONST (XEXP (x, 1))))
    {
      return x;
    }
  else if (flag_pic)
    {
      if (SYMBOLIC_CONST (x)
          || (GET_CODE (x) == PLUS
              && (SYMBOLIC_CONST (XEXP (x, 0))
                  || SYMBOLIC_CONST (XEXP (x, 1)))))
	  x = legitimize_pic_address (x, 0);

      if (legitimate_address_p (mode, x, FALSE))
	return x;
    }

  x = eliminate_constant_term (x, &constant_term);

  /* Optimize loading of large displacements by splitting them
     into the multiple of 4K and the rest; this allows the
     former to be CSE'd if possible.

     Don't do this if the displacement is added to a register
     pointing into the stack frame, as the offsets will
     change later anyway.  */

  if (GET_CODE (constant_term) == CONST_INT
      && !TARGET_LONG_DISPLACEMENT
      && !DISP_IN_RANGE (INTVAL (constant_term))
      && !(REG_P (x) && REGNO_PTR_FRAME_P (REGNO (x))))
    {
      HOST_WIDE_INT lower = INTVAL (constant_term) & 0xfff;
      HOST_WIDE_INT upper = INTVAL (constant_term) ^ lower;

      rtx temp = gen_reg_rtx (Pmode);
      rtx val  = force_operand (GEN_INT (upper), temp);
      if (val != temp)
	emit_move_insn (temp, val);

      x = gen_rtx_PLUS (Pmode, x, temp);
      constant_term = GEN_INT (lower);
    }

  if (GET_CODE (x) == PLUS)
    {
      if (GET_CODE (XEXP (x, 0)) == REG)
	{
	  rtx temp = gen_reg_rtx (Pmode);
	  rtx val  = force_operand (XEXP (x, 1), temp);
	  if (val != temp)
	    emit_move_insn (temp, val);

	  x = gen_rtx_PLUS (Pmode, XEXP (x, 0), temp);
	}

      else if (GET_CODE (XEXP (x, 1)) == REG)
	{
	  rtx temp = gen_reg_rtx (Pmode);
	  rtx val  = force_operand (XEXP (x, 0), temp);
	  if (val != temp)
	    emit_move_insn (temp, val);

	  x = gen_rtx_PLUS (Pmode, temp, XEXP (x, 1));
	}
    }

  if (constant_term != const0_rtx)
    x = gen_rtx_PLUS (Pmode, x, constant_term);

  return x;
}

/* Try a machine-dependent way of reloading an illegitimate address AD
   operand.  If we find one, push the reload and and return the new address.

   MODE is the mode of the enclosing MEM.  OPNUM is the operand number
   and TYPE is the reload type of the current reload.  */

rtx 
legitimize_reload_address (rtx ad, enum machine_mode mode ATTRIBUTE_UNUSED,
			   int opnum, int type)
{
  if (!optimize || TARGET_LONG_DISPLACEMENT)
    return NULL_RTX;

  if (GET_CODE (ad) == PLUS)
    {
      rtx tem = simplify_binary_operation (PLUS, Pmode,
					   XEXP (ad, 0), XEXP (ad, 1));
      if (tem)
	ad = tem;
    }

  if (GET_CODE (ad) == PLUS
      && GET_CODE (XEXP (ad, 0)) == REG
      && GET_CODE (XEXP (ad, 1)) == CONST_INT
      && !DISP_IN_RANGE (INTVAL (XEXP (ad, 1))))
    {
      HOST_WIDE_INT lower = INTVAL (XEXP (ad, 1)) & 0xfff;
      HOST_WIDE_INT upper = INTVAL (XEXP (ad, 1)) ^ lower;
      rtx cst, tem, new;

      cst = GEN_INT (upper);
      if (!legitimate_reload_constant_p (cst))
	cst = force_const_mem (Pmode, cst);

      tem = gen_rtx_PLUS (Pmode, XEXP (ad, 0), cst);
      new = gen_rtx_PLUS (Pmode, tem, GEN_INT (lower));

      push_reload (XEXP (tem, 1), 0, &XEXP (tem, 1), 0,
		   BASE_REG_CLASS, Pmode, VOIDmode, 0, 0, 
		   opnum, (enum reload_type) type);
      return new;
    }

  return NULL_RTX;
}

/* Emit code to move LEN bytes from DST to SRC.  */

void
s390_expand_movmem (rtx dst, rtx src, rtx len)
{
  if (GET_CODE (len) == CONST_INT && INTVAL (len) >= 0 && INTVAL (len) <= 256)
    {
      if (INTVAL (len) > 0)
        emit_insn (gen_movmem_short (dst, src, GEN_INT (INTVAL (len) - 1)));
    }

  else if (TARGET_MVCLE)
    {
      emit_insn (gen_movmem_long (dst, src, convert_to_mode (Pmode, len, 1)));
    }

  else
    {
      rtx dst_addr, src_addr, count, blocks, temp;
      rtx loop_start_label = gen_label_rtx ();
      rtx loop_end_label = gen_label_rtx ();
      rtx end_label = gen_label_rtx ();
      enum machine_mode mode;

      mode = GET_MODE (len);
      if (mode == VOIDmode)
        mode = Pmode;

      dst_addr = gen_reg_rtx (Pmode);
      src_addr = gen_reg_rtx (Pmode);
      count = gen_reg_rtx (mode);
      blocks = gen_reg_rtx (mode);

      convert_move (count, len, 1);
      emit_cmp_and_jump_insns (count, const0_rtx,
			       EQ, NULL_RTX, mode, 1, end_label);

      emit_move_insn (dst_addr, force_operand (XEXP (dst, 0), NULL_RTX));
      emit_move_insn (src_addr, force_operand (XEXP (src, 0), NULL_RTX));
      dst = change_address (dst, VOIDmode, dst_addr);
      src = change_address (src, VOIDmode, src_addr);

      temp = expand_binop (mode, add_optab, count, constm1_rtx, count, 1, 0);
      if (temp != count)
        emit_move_insn (count, temp);

      temp = expand_binop (mode, lshr_optab, count, GEN_INT (8), blocks, 1, 0);
      if (temp != blocks)
        emit_move_insn (blocks, temp);

      emit_cmp_and_jump_insns (blocks, const0_rtx,
			       EQ, NULL_RTX, mode, 1, loop_end_label);

      emit_label (loop_start_label);

      emit_insn (gen_movmem_short (dst, src, GEN_INT (255)));
      s390_load_address (dst_addr,
			 gen_rtx_PLUS (Pmode, dst_addr, GEN_INT (256)));
      s390_load_address (src_addr,
			 gen_rtx_PLUS (Pmode, src_addr, GEN_INT (256)));

      temp = expand_binop (mode, add_optab, blocks, constm1_rtx, blocks, 1, 0);
      if (temp != blocks)
        emit_move_insn (blocks, temp);

      emit_cmp_and_jump_insns (blocks, const0_rtx,
			       EQ, NULL_RTX, mode, 1, loop_end_label);

      emit_jump (loop_start_label);
      emit_label (loop_end_label);

      emit_insn (gen_movmem_short (dst, src,
				   convert_to_mode (Pmode, count, 1)));
      emit_label (end_label);
    }
}

/* Emit code to set LEN bytes at DST to VAL.
   Make use of clrmem if VAL is zero.  */

void
s390_expand_setmem (rtx dst, rtx len, rtx val)
{
  if (GET_CODE (len) == CONST_INT && INTVAL (len) == 0)
    return;

  gcc_assert (GET_CODE (val) == CONST_INT || GET_MODE (val) == QImode);
  
  if (GET_CODE (len) == CONST_INT && INTVAL (len) > 0 && INTVAL (len) <= 257)
    {
      if (val == const0_rtx && INTVAL (len) <= 256)
        emit_insn (gen_clrmem_short (dst, GEN_INT (INTVAL (len) - 1)));
      else
	{
	  /* Initialize memory by storing the first byte.  */
	  emit_move_insn (adjust_address (dst, QImode, 0), val);
	  
	  if (INTVAL (len) > 1)
	    {
	      /* Initiate 1 byte overlap move.
	         The first byte of DST is propagated through DSTP1.
		 Prepare a movmem for:  DST+1 = DST (length = LEN - 1).
		 DST is set to size 1 so the rest of the memory location
		 does not count as source operand.  */
	      rtx dstp1 = adjust_address (dst, VOIDmode, 1);
	      set_mem_size (dst, const1_rtx);

	      emit_insn (gen_movmem_short (dstp1, dst, 
					   GEN_INT (INTVAL (len) - 2)));
	    }
	}
    }

  else if (TARGET_MVCLE)
    {
      val = force_not_mem (convert_modes (Pmode, QImode, val, 1));
      emit_insn (gen_setmem_long (dst, convert_to_mode (Pmode, len, 1), val));
    }

  else
    {
      rtx dst_addr, src_addr, count, blocks, temp, dstp1 = NULL_RTX;
      rtx loop_start_label = gen_label_rtx ();
      rtx loop_end_label = gen_label_rtx ();
      rtx end_label = gen_label_rtx ();
      enum machine_mode mode;

      mode = GET_MODE (len);
      if (mode == VOIDmode)
        mode = Pmode;

      dst_addr = gen_reg_rtx (Pmode);
      src_addr = gen_reg_rtx (Pmode);
      count = gen_reg_rtx (mode);
      blocks = gen_reg_rtx (mode);

      convert_move (count, len, 1);
      emit_cmp_and_jump_insns (count, const0_rtx,
			       EQ, NULL_RTX, mode, 1, end_label);

      emit_move_insn (dst_addr, force_operand (XEXP (dst, 0), NULL_RTX));
      dst = change_address (dst, VOIDmode, dst_addr);

      if (val == const0_rtx)
        temp = expand_binop (mode, add_optab, count, constm1_rtx, count, 1, 0);
      else
	{
	  dstp1 = adjust_address (dst, VOIDmode, 1);
	  set_mem_size (dst, const1_rtx);

	  /* Initialize memory by storing the first byte.  */
	  emit_move_insn (adjust_address (dst, QImode, 0), val);
	  
	  /* If count is 1 we are done.  */
	  emit_cmp_and_jump_insns (count, const1_rtx,
				   EQ, NULL_RTX, mode, 1, end_label);

	  temp = expand_binop (mode, add_optab, count, GEN_INT (-2), count, 1, 0);
	}
      if (temp != count)
        emit_move_insn (count, temp);

      temp = expand_binop (mode, lshr_optab, count, GEN_INT (8), blocks, 1, 0);
      if (temp != blocks)
        emit_move_insn (blocks, temp);

      emit_cmp_and_jump_insns (blocks, const0_rtx,
			       EQ, NULL_RTX, mode, 1, loop_end_label);

      emit_label (loop_start_label);

      if (val == const0_rtx)
	emit_insn (gen_clrmem_short (dst, GEN_INT (255)));
      else
	emit_insn (gen_movmem_short (dstp1, dst, GEN_INT (255)));
      s390_load_address (dst_addr,
			 gen_rtx_PLUS (Pmode, dst_addr, GEN_INT (256)));

      temp = expand_binop (mode, add_optab, blocks, constm1_rtx, blocks, 1, 0);
      if (temp != blocks)
        emit_move_insn (blocks, temp);

      emit_cmp_and_jump_insns (blocks, const0_rtx,
			       EQ, NULL_RTX, mode, 1, loop_end_label);

      emit_jump (loop_start_label);
      emit_label (loop_end_label);

      if (val == const0_rtx)
        emit_insn (gen_clrmem_short (dst, convert_to_mode (Pmode, count, 1)));
      else
        emit_insn (gen_movmem_short (dstp1, dst, convert_to_mode (Pmode, count, 1)));
      emit_label (end_label);
    }
}

/* Emit code to compare LEN bytes at OP0 with those at OP1,
   and return the result in TARGET.  */

void
s390_expand_cmpmem (rtx target, rtx op0, rtx op1, rtx len)
{
  rtx ccreg = gen_rtx_REG (CCUmode, CC_REGNUM);
  rtx tmp;

  /* As the result of CMPINT is inverted compared to what we need,
     we have to swap the operands.  */
  tmp = op0; op0 = op1; op1 = tmp;

  if (GET_CODE (len) == CONST_INT && INTVAL (len) >= 0 && INTVAL (len) <= 256)
    {
      if (INTVAL (len) > 0)
        {
          emit_insn (gen_cmpmem_short (op0, op1, GEN_INT (INTVAL (len) - 1)));
          emit_insn (gen_cmpint (target, ccreg));
        }
      else
        emit_move_insn (target, const0_rtx);
    }
  else if (TARGET_MVCLE)
    {
      emit_insn (gen_cmpmem_long (op0, op1, convert_to_mode (Pmode, len, 1)));
      emit_insn (gen_cmpint (target, ccreg));
    }
  else
    {
      rtx addr0, addr1, count, blocks, temp;
      rtx loop_start_label = gen_label_rtx ();
      rtx loop_end_label = gen_label_rtx ();
      rtx end_label = gen_label_rtx ();
      enum machine_mode mode;

      mode = GET_MODE (len);
      if (mode == VOIDmode)
        mode = Pmode;

      addr0 = gen_reg_rtx (Pmode);
      addr1 = gen_reg_rtx (Pmode);
      count = gen_reg_rtx (mode);
      blocks = gen_reg_rtx (mode);

      convert_move (count, len, 1);
      emit_cmp_and_jump_insns (count, const0_rtx,
			       EQ, NULL_RTX, mode, 1, end_label);

      emit_move_insn (addr0, force_operand (XEXP (op0, 0), NULL_RTX));
      emit_move_insn (addr1, force_operand (XEXP (op1, 0), NULL_RTX));
      op0 = change_address (op0, VOIDmode, addr0);
      op1 = change_address (op1, VOIDmode, addr1);

      temp = expand_binop (mode, add_optab, count, constm1_rtx, count, 1, 0);
      if (temp != count)
        emit_move_insn (count, temp);

      temp = expand_binop (mode, lshr_optab, count, GEN_INT (8), blocks, 1, 0);
      if (temp != blocks)
        emit_move_insn (blocks, temp);

      emit_cmp_and_jump_insns (blocks, const0_rtx,
			       EQ, NULL_RTX, mode, 1, loop_end_label);

      emit_label (loop_start_label);

      emit_insn (gen_cmpmem_short (op0, op1, GEN_INT (255)));
      temp = gen_rtx_NE (VOIDmode, ccreg, const0_rtx);
      temp = gen_rtx_IF_THEN_ELSE (VOIDmode, temp,
			gen_rtx_LABEL_REF (VOIDmode, end_label), pc_rtx);
      temp = gen_rtx_SET (VOIDmode, pc_rtx, temp);
      emit_jump_insn (temp);

      s390_load_address (addr0,
			 gen_rtx_PLUS (Pmode, addr0, GEN_INT (256)));
      s390_load_address (addr1,
			 gen_rtx_PLUS (Pmode, addr1, GEN_INT (256)));

      temp = expand_binop (mode, add_optab, blocks, constm1_rtx, blocks, 1, 0);
      if (temp != blocks)
        emit_move_insn (blocks, temp);

      emit_cmp_and_jump_insns (blocks, const0_rtx,
			       EQ, NULL_RTX, mode, 1, loop_end_label);

      emit_jump (loop_start_label);
      emit_label (loop_end_label);

      emit_insn (gen_cmpmem_short (op0, op1,
				   convert_to_mode (Pmode, count, 1)));
      emit_label (end_label);

      emit_insn (gen_cmpint (target, ccreg));
    }
}


/* Expand conditional increment or decrement using alc/slb instructions.
   Should generate code setting DST to either SRC or SRC + INCREMENT,
   depending on the result of the comparison CMP_OP0 CMP_CODE CMP_OP1.
   Returns true if successful, false otherwise.

   That makes it possible to implement some if-constructs without jumps e.g.:
   (borrow = CC0 | CC1 and carry = CC2 | CC3)
   unsigned int a, b, c;
   if (a < b)  c++; -> CCU  b > a  -> CC2;    c += carry;
   if (a < b)  c--; -> CCL3 a - b  -> borrow; c -= borrow;
   if (a <= b) c++; -> CCL3 b - a  -> borrow; c += carry;
   if (a <= b) c--; -> CCU  a <= b -> borrow; c -= borrow;

   Checks for EQ and NE with a nonzero value need an additional xor e.g.:
   if (a == b) c++; -> CCL3 a ^= b; 0 - a  -> borrow;    c += carry;
   if (a == b) c--; -> CCU  a ^= b; a <= 0 -> CC0 | CC1; c -= borrow;
   if (a != b) c++; -> CCU  a ^= b; a > 0  -> CC2;       c += carry;
   if (a != b) c--; -> CCL3 a ^= b; 0 - a  -> borrow;    c -= borrow; */

bool
s390_expand_addcc (enum rtx_code cmp_code, rtx cmp_op0, rtx cmp_op1,
		   rtx dst, rtx src, rtx increment)
{
  enum machine_mode cmp_mode;
  enum machine_mode cc_mode;
  rtx op_res;
  rtx insn;
  rtvec p;
  int ret;

  if ((GET_MODE (cmp_op0) == SImode || GET_MODE (cmp_op0) == VOIDmode)
      && (GET_MODE (cmp_op1) == SImode || GET_MODE (cmp_op1) == VOIDmode))
    cmp_mode = SImode;
  else if ((GET_MODE (cmp_op0) == DImode || GET_MODE (cmp_op0) == VOIDmode)
	   && (GET_MODE (cmp_op1) == DImode || GET_MODE (cmp_op1) == VOIDmode))
    cmp_mode = DImode;
  else
    return false;

  /* Try ADD LOGICAL WITH CARRY.  */
  if (increment == const1_rtx)
    {
      /* Determine CC mode to use.  */
      if (cmp_code == EQ || cmp_code == NE)
	{
	  if (cmp_op1 != const0_rtx)
	    {
	      cmp_op0 = expand_simple_binop (cmp_mode, XOR, cmp_op0, cmp_op1,
					     NULL_RTX, 0, OPTAB_WIDEN);
	      cmp_op1 = const0_rtx;
	    }

	  cmp_code = cmp_code == EQ ? LEU : GTU;
	}

      if (cmp_code == LTU || cmp_code == LEU)
	{
	  rtx tem = cmp_op0;
	  cmp_op0 = cmp_op1;
	  cmp_op1 = tem;
	  cmp_code = swap_condition (cmp_code);
	}

      switch (cmp_code)
	{
	  case GTU:
	    cc_mode = CCUmode;
	    break;

	  case GEU:
	    cc_mode = CCL3mode;
	    break;

	  default:
	    return false;
	}

      /* Emit comparison instruction pattern. */
      if (!register_operand (cmp_op0, cmp_mode))
	cmp_op0 = force_reg (cmp_mode, cmp_op0);

      insn = gen_rtx_SET (VOIDmode, gen_rtx_REG (cc_mode, CC_REGNUM),
			  gen_rtx_COMPARE (cc_mode, cmp_op0, cmp_op1));
      /* We use insn_invalid_p here to add clobbers if required.  */
      ret = insn_invalid_p (emit_insn (insn));
      gcc_assert (!ret);

      /* Emit ALC instruction pattern.  */
      op_res = gen_rtx_fmt_ee (cmp_code, GET_MODE (dst),
			       gen_rtx_REG (cc_mode, CC_REGNUM),
			       const0_rtx);

      if (src != const0_rtx)
	{
	  if (!register_operand (src, GET_MODE (dst)))
	    src = force_reg (GET_MODE (dst), src);

	  src = gen_rtx_PLUS (GET_MODE (dst), src, const0_rtx);
	  op_res = gen_rtx_PLUS (GET_MODE (dst), src, op_res);
	}

      p = rtvec_alloc (2);
      RTVEC_ELT (p, 0) = 
        gen_rtx_SET (VOIDmode, dst, op_res);
      RTVEC_ELT (p, 1) = 
	gen_rtx_CLOBBER (VOIDmode, gen_rtx_REG (CCmode, CC_REGNUM));
      emit_insn (gen_rtx_PARALLEL (VOIDmode, p));

      return true;
    }

  /* Try SUBTRACT LOGICAL WITH BORROW.  */
  if (increment == constm1_rtx)
    {
      /* Determine CC mode to use.  */
      if (cmp_code == EQ || cmp_code == NE)
	{
	  if (cmp_op1 != const0_rtx)
	    {
	      cmp_op0 = expand_simple_binop (cmp_mode, XOR, cmp_op0, cmp_op1,
					     NULL_RTX, 0, OPTAB_WIDEN);
	      cmp_op1 = const0_rtx;
	    }

	  cmp_code = cmp_code == EQ ? LEU : GTU;
	}

      if (cmp_code == GTU || cmp_code == GEU)
	{
	  rtx tem = cmp_op0;
	  cmp_op0 = cmp_op1;
	  cmp_op1 = tem;
	  cmp_code = swap_condition (cmp_code);
	}

      switch (cmp_code)
	{
	  case LEU:
	    cc_mode = CCUmode;
	    break;

	  case LTU:
	    cc_mode = CCL3mode;
	    break;

	  default:
	    return false;
	}

      /* Emit comparison instruction pattern. */
      if (!register_operand (cmp_op0, cmp_mode))
	cmp_op0 = force_reg (cmp_mode, cmp_op0);

      insn = gen_rtx_SET (VOIDmode, gen_rtx_REG (cc_mode, CC_REGNUM),
			  gen_rtx_COMPARE (cc_mode, cmp_op0, cmp_op1));
      /* We use insn_invalid_p here to add clobbers if required.  */
      ret = insn_invalid_p (emit_insn (insn));
      gcc_assert (!ret);

      /* Emit SLB instruction pattern.  */
      if (!register_operand (src, GET_MODE (dst)))
	src = force_reg (GET_MODE (dst), src);

      op_res = gen_rtx_MINUS (GET_MODE (dst), 
			      gen_rtx_MINUS (GET_MODE (dst), src, const0_rtx), 
			      gen_rtx_fmt_ee (cmp_code, GET_MODE (dst), 
					      gen_rtx_REG (cc_mode, CC_REGNUM), 
					      const0_rtx));
      p = rtvec_alloc (2);
      RTVEC_ELT (p, 0) = 
        gen_rtx_SET (VOIDmode, dst, op_res);
      RTVEC_ELT (p, 1) = 
	gen_rtx_CLOBBER (VOIDmode, gen_rtx_REG (CCmode, CC_REGNUM));
      emit_insn (gen_rtx_PARALLEL (VOIDmode, p));

      return true;
    }

  return false;
}

/* Expand code for the insv template. Return true if successful, false else.  */

bool 
s390_expand_insv (rtx dest, rtx op1, rtx op2, rtx src)
{
  int bitsize = INTVAL (op1);
  int bitpos = INTVAL (op2);

  /* We need byte alignment.  */
  if (bitsize % BITS_PER_UNIT)
    return false;

  if (bitpos == 0
      && memory_operand (dest, VOIDmode)
      && (register_operand (src, word_mode)
	  || const_int_operand (src, VOIDmode)))
    {
      /* Emit standard pattern if possible.  */
      enum machine_mode mode = smallest_mode_for_size (bitsize, MODE_INT);
      if (GET_MODE_BITSIZE (mode) == bitsize)
	emit_move_insn (adjust_address (dest, mode, 0), gen_lowpart (mode, src));

      /* (set (ze (mem)) (const_int)).  */
      else if (const_int_operand (src, VOIDmode))
	{
	  int size = bitsize / BITS_PER_UNIT;
	  rtx src_mem = adjust_address (force_const_mem (word_mode, src), BLKmode,
					GET_MODE_SIZE (word_mode) - size);

	  dest = adjust_address (dest, BLKmode, 0);
	  set_mem_size (dest, GEN_INT (size));
	  s390_expand_movmem (dest, src_mem, GEN_INT (size));
	}
	  
      /* (set (ze (mem)) (reg)).  */
      else if (register_operand (src, word_mode))
	{
	  if (bitsize <= GET_MODE_BITSIZE (SImode))
	    emit_move_insn (gen_rtx_ZERO_EXTRACT (word_mode, dest, op1,
						  const0_rtx), src);
	  else
	    {
	      /* Emit st,stcmh sequence.  */
	      int stcmh_width = bitsize - GET_MODE_BITSIZE (SImode);
	      int size = stcmh_width / BITS_PER_UNIT;

	      emit_move_insn (adjust_address (dest, SImode, size), 
			      gen_lowpart (SImode, src));
	      set_mem_size (dest, GEN_INT (size));
	      emit_move_insn (gen_rtx_ZERO_EXTRACT (word_mode, dest, GEN_INT
						    (stcmh_width), const0_rtx),
			      gen_rtx_LSHIFTRT (word_mode, src, GEN_INT
						(GET_MODE_BITSIZE (SImode))));
	    }
	}
      else
	return false;

      return true;
    }

  /* (set (ze (reg)) (const_int)).  */
  if (TARGET_ZARCH
      && register_operand (dest, word_mode) 
      && (bitpos % 16) == 0
      && (bitsize % 16) == 0
      && const_int_operand (src, VOIDmode))
    {
      HOST_WIDE_INT val = INTVAL (src);
      int regpos = bitpos + bitsize;

      while (regpos > bitpos)
	{
	  enum machine_mode putmode;
	  int putsize;

	  if (TARGET_EXTIMM && (regpos % 32 == 0) && (regpos >= bitpos + 32))
	    putmode = SImode;
	  else
	    putmode = HImode;

	  putsize = GET_MODE_BITSIZE (putmode);
	  regpos -= putsize;
	  emit_move_insn (gen_rtx_ZERO_EXTRACT (word_mode, dest, 
						GEN_INT (putsize),
						GEN_INT (regpos)), 
			  gen_int_mode (val, putmode));
	  val >>= putsize;
	}
      gcc_assert (regpos == bitpos);
      return true;
    }

  return false;
}

/* A subroutine of s390_expand_cs_hqi and s390_expand_atomic which returns a
   register that holds VAL of mode MODE shifted by COUNT bits.  */

static inline rtx
s390_expand_mask_and_shift (rtx val, enum machine_mode mode, rtx count)
{
  val = expand_simple_binop (SImode, AND, val, GEN_INT (GET_MODE_MASK (mode)),
			     NULL_RTX, 1, OPTAB_DIRECT);
  return expand_simple_binop (SImode, ASHIFT, val, count, 
			      NULL_RTX, 1, OPTAB_DIRECT);
}

/* Structure to hold the initial parameters for a compare_and_swap operation
   in HImode and QImode.  */ 

struct alignment_context
{
  rtx memsi;	  /* SI aligned memory location.  */ 
  rtx shift;	  /* Bit offset with regard to lsb.  */
  rtx modemask;	  /* Mask of the HQImode shifted by SHIFT bits.  */
  rtx modemaski;  /* ~modemask */
  bool aligned;	  /* True if memory is aligned, false else.  */
};

/* A subroutine of s390_expand_cs_hqi and s390_expand_atomic to initialize
   structure AC for transparent simplifying, if the memory alignment is known
   to be at least 32bit.  MEM is the memory location for the actual operation
   and MODE its mode.  */

static void
init_alignment_context (struct alignment_context *ac, rtx mem,
			enum machine_mode mode)
{
  ac->shift = GEN_INT (GET_MODE_SIZE (SImode) - GET_MODE_SIZE (mode));
  ac->aligned = (MEM_ALIGN (mem) >= GET_MODE_BITSIZE (SImode));

  if (ac->aligned)
    ac->memsi = adjust_address (mem, SImode, 0); /* Memory is aligned.  */
  else
    {
      /* Alignment is unknown.  */
      rtx byteoffset, addr, align;

      /* Force the address into a register.  */
      addr = force_reg (Pmode, XEXP (mem, 0));

      /* Align it to SImode.  */
      align = expand_simple_binop (Pmode, AND, addr,
				   GEN_INT (-GET_MODE_SIZE (SImode)),
				   NULL_RTX, 1, OPTAB_DIRECT);
      /* Generate MEM.  */
      ac->memsi = gen_rtx_MEM (SImode, align);
      MEM_VOLATILE_P (ac->memsi) = MEM_VOLATILE_P (mem);
      set_mem_alias_set (ac->memsi, ALIAS_SET_MEMORY_BARRIER);
      set_mem_align (ac->memsi, GET_MODE_BITSIZE (SImode));

      /* Calculate shiftcount.  */
      byteoffset = expand_simple_binop (Pmode, AND, addr,
					GEN_INT (GET_MODE_SIZE (SImode) - 1),
					NULL_RTX, 1, OPTAB_DIRECT);
      /* As we already have some offset, evaluate the remaining distance.  */
      ac->shift = expand_simple_binop (SImode, MINUS, ac->shift, byteoffset,
				      NULL_RTX, 1, OPTAB_DIRECT);

    }
  /* Shift is the byte count, but we need the bitcount.  */
  ac->shift = expand_simple_binop (SImode, MULT, ac->shift, GEN_INT (BITS_PER_UNIT),
				  NULL_RTX, 1, OPTAB_DIRECT);
  /* Calculate masks.  */
  ac->modemask = expand_simple_binop (SImode, ASHIFT, 
				     GEN_INT (GET_MODE_MASK (mode)), ac->shift,
				     NULL_RTX, 1, OPTAB_DIRECT);
  ac->modemaski = expand_simple_unop (SImode, NOT, ac->modemask, NULL_RTX, 1);
}

/* Expand an atomic compare and swap operation for HImode and QImode.  MEM is
   the memory location, CMP the old value to compare MEM with and NEW the value
   to set if CMP == MEM.
   CMP is never in memory for compare_and_swap_cc because
   expand_bool_compare_and_swap puts it into a register for later compare.  */

void
s390_expand_cs_hqi (enum machine_mode mode, rtx target, rtx mem, rtx cmp, rtx new)
{
  struct alignment_context ac;
  rtx cmpv, newv, val, resv, cc;
  rtx res = gen_reg_rtx (SImode);
  rtx csloop = gen_label_rtx ();
  rtx csend = gen_label_rtx ();

  gcc_assert (register_operand (target, VOIDmode));
  gcc_assert (MEM_P (mem));

  init_alignment_context (&ac, mem, mode);

  /* Shift the values to the correct bit positions.  */
  if (!(ac.aligned && MEM_P (cmp)))
    cmp = s390_expand_mask_and_shift (cmp, mode, ac.shift);
  if (!(ac.aligned && MEM_P (new)))
    new = s390_expand_mask_and_shift (new, mode, ac.shift);

  /* Load full word.  Subsequent loads are performed by CS.  */
  val = expand_simple_binop (SImode, AND, ac.memsi, ac.modemaski,
			     NULL_RTX, 1, OPTAB_DIRECT);

  /* Start CS loop.  */
  emit_label (csloop);
  /* val = "<mem>00..0<mem>" 
   * cmp = "00..0<cmp>00..0"
   * new = "00..0<new>00..0" 
   */

  /* Patch cmp and new with val at correct position.  */
  if (ac.aligned && MEM_P (cmp))
    {
      cmpv = force_reg (SImode, val);
      store_bit_field (cmpv, GET_MODE_BITSIZE (mode), 0, SImode, cmp);
    }
  else
    cmpv = force_reg (SImode, expand_simple_binop (SImode, IOR, cmp, val,
						   NULL_RTX, 1, OPTAB_DIRECT));
  if (ac.aligned && MEM_P (new))
    {
      newv = force_reg (SImode, val);
      store_bit_field (newv, GET_MODE_BITSIZE (mode), 0, SImode, new);
    }
  else
    newv = force_reg (SImode, expand_simple_binop (SImode, IOR, new, val,
						   NULL_RTX, 1, OPTAB_DIRECT));

  /* Jump to end if we're done (likely?).  */
  s390_emit_jump (csend, s390_emit_compare_and_swap (EQ, res, ac.memsi,
						     cmpv, newv));

  /* Check for changes outside mode.  */
  resv = expand_simple_binop (SImode, AND, res, ac.modemaski, 
			      NULL_RTX, 1, OPTAB_DIRECT);
  cc = s390_emit_compare (NE, resv, val); 
  emit_move_insn (val, resv);
  /* Loop internal if so.  */
  s390_emit_jump (csloop, cc);

  emit_label (csend);
  
  /* Return the correct part of the bitfield.  */
  convert_move (target, expand_simple_binop (SImode, LSHIFTRT, res, ac.shift, 
					     NULL_RTX, 1, OPTAB_DIRECT), 1);
}

/* Expand an atomic operation CODE of mode MODE.  MEM is the memory location
   and VAL the value to play with.  If AFTER is true then store the the value
   MEM holds after the operation, if AFTER is false then store the value MEM
   holds before the operation.  If TARGET is zero then discard that value, else
   store it to TARGET.  */

void
s390_expand_atomic (enum machine_mode mode, enum rtx_code code,
		    rtx target, rtx mem, rtx val, bool after)
{
  struct alignment_context ac;
  rtx cmp;
  rtx new = gen_reg_rtx (SImode);
  rtx orig = gen_reg_rtx (SImode);
  rtx csloop = gen_label_rtx ();

  gcc_assert (!target || register_operand (target, VOIDmode));
  gcc_assert (MEM_P (mem));

  init_alignment_context (&ac, mem, mode);

  /* Shift val to the correct bit positions.
     Preserve "icm", but prevent "ex icm".  */
  if (!(ac.aligned && code == SET && MEM_P (val)))
    val = s390_expand_mask_and_shift (val, mode, ac.shift);

  /* Further preparation insns.  */
  if (code == PLUS || code == MINUS)
    emit_move_insn (orig, val);
  else if (code == MULT || code == AND) /* val = "11..1<val>11..1" */
    val = expand_simple_binop (SImode, XOR, val, ac.modemaski,
			       NULL_RTX, 1, OPTAB_DIRECT);

  /* Load full word.  Subsequent loads are performed by CS.  */
  cmp = force_reg (SImode, ac.memsi);

  /* Start CS loop.  */
  emit_label (csloop);
  emit_move_insn (new, cmp);

  /* Patch new with val at correct position.  */
  switch (code)
    {
    case PLUS:
    case MINUS:
      val = expand_simple_binop (SImode, code, new, orig,
				 NULL_RTX, 1, OPTAB_DIRECT);
      val = expand_simple_binop (SImode, AND, val, ac.modemask,
				 NULL_RTX, 1, OPTAB_DIRECT);
      /* FALLTHRU */
    case SET: 
      if (ac.aligned && MEM_P (val))
	store_bit_field (new, GET_MODE_BITSIZE (mode), 0, SImode, val);
      else
	{
	  new = expand_simple_binop (SImode, AND, new, ac.modemaski,
				     NULL_RTX, 1, OPTAB_DIRECT);
	  new = expand_simple_binop (SImode, IOR, new, val,
				     NULL_RTX, 1, OPTAB_DIRECT);
	}
      break;
    case AND:
    case IOR:
    case XOR:
      new = expand_simple_binop (SImode, code, new, val,
				 NULL_RTX, 1, OPTAB_DIRECT);
      break;
    case MULT: /* NAND */
      new = expand_simple_binop (SImode, XOR, new, ac.modemask,
				 NULL_RTX, 1, OPTAB_DIRECT);
      new = expand_simple_binop (SImode, AND, new, val,
				 NULL_RTX, 1, OPTAB_DIRECT);
      break;
    default:
      gcc_unreachable ();
    }

  s390_emit_jump (csloop, s390_emit_compare_and_swap (NE, cmp,
						      ac.memsi, cmp, new));

  /* Return the correct part of the bitfield.  */
  if (target)
    convert_move (target, expand_simple_binop (SImode, LSHIFTRT,
					       after ? new : cmp, ac.shift,
					       NULL_RTX, 1, OPTAB_DIRECT), 1);
}

/* This is called from dwarf2out.c via TARGET_ASM_OUTPUT_DWARF_DTPREL.
   We need to emit DTP-relative relocations.  */

static void s390_output_dwarf_dtprel (FILE *, int, rtx) ATTRIBUTE_UNUSED;

static void
s390_output_dwarf_dtprel (FILE *file, int size, rtx x)
{
  switch (size)
    {
    case 4:
      fputs ("\t.long\t", file);
      break;
    case 8:
      fputs ("\t.quad\t", file);
      break;
    default:
      gcc_unreachable ();
    }
  output_addr_const (file, x);
  fputs ("@DTPOFF", file);
}

#ifdef TARGET_ALTERNATE_LONG_DOUBLE_MANGLING
/* Implement TARGET_MANGLE_FUNDAMENTAL_TYPE.  */

static const char *
s390_mangle_fundamental_type (tree type)
{
  if (TYPE_MAIN_VARIANT (type) == long_double_type_node
      && TARGET_LONG_DOUBLE_128)
    return "g";

  /* For all other types, use normal C++ mangling.  */
  return NULL;
}
#endif

/* In the name of slightly smaller debug output, and to cater to
   general assembler lossage, recognize various UNSPEC sequences
   and turn them back into a direct symbol reference.  */

static rtx
s390_delegitimize_address (rtx orig_x)
{
  rtx x = orig_x, y;

  if (GET_CODE (x) != MEM)
    return orig_x;

  x = XEXP (x, 0);
  if (GET_CODE (x) == PLUS
      && GET_CODE (XEXP (x, 1)) == CONST
      && GET_CODE (XEXP (x, 0)) == REG
      && REGNO (XEXP (x, 0)) == PIC_OFFSET_TABLE_REGNUM)
    {
      y = XEXP (XEXP (x, 1), 0);
      if (GET_CODE (y) == UNSPEC
	  && XINT (y, 1) == UNSPEC_GOT)
	return XVECEXP (y, 0, 0);
      return orig_x;
    }

  if (GET_CODE (x) == CONST)
    {
      y = XEXP (x, 0);
      if (GET_CODE (y) == UNSPEC
	  && XINT (y, 1) == UNSPEC_GOTENT)
	return XVECEXP (y, 0, 0);
      return orig_x;
    }

  return orig_x;
}

/* Output operand OP to stdio stream FILE.
   OP is an address (register + offset) which is not used to address data;
   instead the rightmost bits are interpreted as the value.  */

static void
print_shift_count_operand (FILE *file, rtx op)
{
  HOST_WIDE_INT offset;
  rtx base;

  /* Extract base register and offset.  */
  if (!s390_decompose_shift_count (op, &base, &offset))
    gcc_unreachable ();

  /* Sanity check.  */
  if (base)
    {
      gcc_assert (GET_CODE (base) == REG);
      gcc_assert (REGNO (base) < FIRST_PSEUDO_REGISTER);
      gcc_assert (REGNO_REG_CLASS (REGNO (base)) == ADDR_REGS);
    }

  /* Offsets are constricted to twelve bits.  */
  fprintf (file, HOST_WIDE_INT_PRINT_DEC, offset & ((1 << 12) - 1));
  if (base)
    fprintf (file, "(%s)", reg_names[REGNO (base)]);
}

/* See 'get_some_local_dynamic_name'.  */

static int
get_some_local_dynamic_name_1 (rtx *px, void *data ATTRIBUTE_UNUSED)
{
  rtx x = *px;

  if (GET_CODE (x) == SYMBOL_REF && CONSTANT_POOL_ADDRESS_P (x))
    {
      x = get_pool_constant (x);
      return for_each_rtx (&x, get_some_local_dynamic_name_1, 0);
    }

  if (GET_CODE (x) == SYMBOL_REF
      && tls_symbolic_operand (x) == TLS_MODEL_LOCAL_DYNAMIC)
    {
      cfun->machine->some_ld_name = XSTR (x, 0);
      return 1;
    }

  return 0;
}

/* Locate some local-dynamic symbol still in use by this function
   so that we can print its name in local-dynamic base patterns.  */

static const char *
get_some_local_dynamic_name (void)
{
  rtx insn;

  if (cfun->machine->some_ld_name)
    return cfun->machine->some_ld_name;

  for (insn = get_insns (); insn ; insn = NEXT_INSN (insn))
    if (INSN_P (insn)
        && for_each_rtx (&PATTERN (insn), get_some_local_dynamic_name_1, 0))
      return cfun->machine->some_ld_name;

  gcc_unreachable ();
}

/* Output machine-dependent UNSPECs occurring in address constant X
   in assembler syntax to stdio stream FILE.  Returns true if the
   constant X could be recognized, false otherwise.  */

bool
s390_output_addr_const_extra (FILE *file, rtx x)
{
  if (GET_CODE (x) == UNSPEC && XVECLEN (x, 0) == 1)
    switch (XINT (x, 1))
      {
      case UNSPEC_GOTENT:
	output_addr_const (file, XVECEXP (x, 0, 0));
	fprintf (file, "@GOTENT");
	return true;
      case UNSPEC_GOT:
	output_addr_const (file, XVECEXP (x, 0, 0));
	fprintf (file, "@GOT");
	return true;
      case UNSPEC_GOTOFF:
	output_addr_const (file, XVECEXP (x, 0, 0));
	fprintf (file, "@GOTOFF");
	return true;
      case UNSPEC_PLT:
	output_addr_const (file, XVECEXP (x, 0, 0));
	fprintf (file, "@PLT");
	return true;
      case UNSPEC_PLTOFF:
	output_addr_const (file, XVECEXP (x, 0, 0));
	fprintf (file, "@PLTOFF");
	return true;
      case UNSPEC_TLSGD:
	output_addr_const (file, XVECEXP (x, 0, 0));
	fprintf (file, "@TLSGD");
	return true;
      case UNSPEC_TLSLDM:
	assemble_name (file, get_some_local_dynamic_name ());
	fprintf (file, "@TLSLDM");
	return true;
      case UNSPEC_DTPOFF:
	output_addr_const (file, XVECEXP (x, 0, 0));
	fprintf (file, "@DTPOFF");
	return true;
      case UNSPEC_NTPOFF:
	output_addr_const (file, XVECEXP (x, 0, 0));
	fprintf (file, "@NTPOFF");
	return true;
      case UNSPEC_GOTNTPOFF:
	output_addr_const (file, XVECEXP (x, 0, 0));
	fprintf (file, "@GOTNTPOFF");
	return true;
      case UNSPEC_INDNTPOFF:
	output_addr_const (file, XVECEXP (x, 0, 0));
	fprintf (file, "@INDNTPOFF");
	return true;
      }

  return false;
}

/* Output address operand ADDR in assembler syntax to
   stdio stream FILE.  */

void
print_operand_address (FILE *file, rtx addr)
{
  struct s390_address ad;

  if (!s390_decompose_address (addr, &ad)
      || (ad.base && !REGNO_OK_FOR_BASE_P (REGNO (ad.base)))
      || (ad.indx && !REGNO_OK_FOR_INDEX_P (REGNO (ad.indx))))
    output_operand_lossage ("cannot decompose address");

  if (ad.disp)
    output_addr_const (file, ad.disp);
  else
    fprintf (file, "0");

  if (ad.base && ad.indx)
    fprintf (file, "(%s,%s)", reg_names[REGNO (ad.indx)],
                              reg_names[REGNO (ad.base)]);
  else if (ad.base)
    fprintf (file, "(%s)", reg_names[REGNO (ad.base)]);
}

/* Output operand X in assembler syntax to stdio stream FILE.
   CODE specified the format flag.  The following format flags
   are recognized:

    'C': print opcode suffix for branch condition.
    'D': print opcode suffix for inverse branch condition.
    'J': print tls_load/tls_gdcall/tls_ldcall suffix
    'G': print the size of the operand in bytes.
    'O': print only the displacement of a memory reference.
    'R': print only the base register of a memory reference.
    'S': print S-type memory reference (base+displacement).
    'N': print the second word of a DImode operand.
    'M': print the second word of a TImode operand.
    'Y': print shift count operand.

    'b': print integer X as if it's an unsigned byte.
    'x': print integer X as if it's an unsigned halfword.
    'h': print integer X as if it's a signed halfword.
    'i': print the first nonzero HImode part of X.
    'j': print the first HImode part unequal to -1 of X.
    'k': print the first nonzero SImode part of X.
    'm': print the first SImode part unequal to -1 of X.
    'o': print integer X as if it's an unsigned 32bit word.  */

void
print_operand (FILE *file, rtx x, int code)
{
  switch (code)
    {
    case 'C':
      fprintf (file, s390_branch_condition_mnemonic (x, FALSE));
      return;

    case 'D':
      fprintf (file, s390_branch_condition_mnemonic (x, TRUE));
      return;

    case 'J':
      if (GET_CODE (x) == SYMBOL_REF)
	{
	  fprintf (file, "%s", ":tls_load:");
	  output_addr_const (file, x);
	}
      else if (GET_CODE (x) == UNSPEC && XINT (x, 1) == UNSPEC_TLSGD)
	{
	  fprintf (file, "%s", ":tls_gdcall:");
	  output_addr_const (file, XVECEXP (x, 0, 0));
	}
      else if (GET_CODE (x) == UNSPEC && XINT (x, 1) == UNSPEC_TLSLDM)
	{
	  fprintf (file, "%s", ":tls_ldcall:");
	  assemble_name (file, get_some_local_dynamic_name ());
	}
      else
	gcc_unreachable ();
      return;

    case 'G':
      fprintf (file, "%u", GET_MODE_SIZE (GET_MODE (x)));
      return;

    case 'O':
      {
        struct s390_address ad;
	int ret;

        gcc_assert (GET_CODE (x) == MEM);
	ret = s390_decompose_address (XEXP (x, 0), &ad);
	gcc_assert (ret);
	gcc_assert (!ad.base || REGNO_OK_FOR_BASE_P (REGNO (ad.base)));
	gcc_assert (!ad.indx);

        if (ad.disp)
          output_addr_const (file, ad.disp);
        else
          fprintf (file, "0");
      }
      return;

    case 'R':
      {
        struct s390_address ad;
	int ret;

        gcc_assert (GET_CODE (x) == MEM);
	ret = s390_decompose_address (XEXP (x, 0), &ad);
	gcc_assert (ret);
	gcc_assert (!ad.base || REGNO_OK_FOR_BASE_P (REGNO (ad.base)));
	gcc_assert (!ad.indx);

        if (ad.base)
          fprintf (file, "%s", reg_names[REGNO (ad.base)]);
        else
          fprintf (file, "0");
      }
      return;

    case 'S':
      {
	struct s390_address ad;
	int ret;

        gcc_assert (GET_CODE (x) == MEM);
	ret = s390_decompose_address (XEXP (x, 0), &ad);
	gcc_assert (ret);
	gcc_assert (!ad.base || REGNO_OK_FOR_BASE_P (REGNO (ad.base)));
	gcc_assert (!ad.indx);

	if (ad.disp)
	  output_addr_const (file, ad.disp);
	else
	  fprintf (file, "0");

	if (ad.base)
	  fprintf (file, "(%s)", reg_names[REGNO (ad.base)]);
      }
      return;

    case 'N':
      if (GET_CODE (x) == REG)
	x = gen_rtx_REG (GET_MODE (x), REGNO (x) + 1);
      else if (GET_CODE (x) == MEM)
	x = change_address (x, VOIDmode, plus_constant (XEXP (x, 0), 4));
      else
        gcc_unreachable ();
      break;

    case 'M':
      if (GET_CODE (x) == REG)
	x = gen_rtx_REG (GET_MODE (x), REGNO (x) + 1);
      else if (GET_CODE (x) == MEM)
	x = change_address (x, VOIDmode, plus_constant (XEXP (x, 0), 8));
      else
        gcc_unreachable ();
      break;

    case 'Y':
      print_shift_count_operand (file, x);
      return;
    }

  switch (GET_CODE (x))
    {
    case REG:
      fprintf (file, "%s", reg_names[REGNO (x)]);
      break;

    case MEM:
      output_address (XEXP (x, 0));
      break;

    case CONST:
    case CODE_LABEL:
    case LABEL_REF:
    case SYMBOL_REF:
      output_addr_const (file, x);
      break;

    case CONST_INT:
      if (code == 'b')
        fprintf (file, HOST_WIDE_INT_PRINT_DEC, INTVAL (x) & 0xff);
      else if (code == 'x')
        fprintf (file, HOST_WIDE_INT_PRINT_DEC, INTVAL (x) & 0xffff);
      else if (code == 'h')
        fprintf (file, HOST_WIDE_INT_PRINT_DEC, ((INTVAL (x) & 0xffff) ^ 0x8000) - 0x8000);
      else if (code == 'i')
	fprintf (file, HOST_WIDE_INT_PRINT_DEC,
		 s390_extract_part (x, HImode, 0));
      else if (code == 'j')
	fprintf (file, HOST_WIDE_INT_PRINT_DEC,
		 s390_extract_part (x, HImode, -1));
      else if (code == 'k')
 	fprintf (file, HOST_WIDE_INT_PRINT_DEC,
 		 s390_extract_part (x, SImode, 0));
      else if (code == 'm')
 	fprintf (file, HOST_WIDE_INT_PRINT_DEC,
 		 s390_extract_part (x, SImode, -1));
      else if (code == 'o')
	fprintf (file, HOST_WIDE_INT_PRINT_DEC, INTVAL (x) & 0xffffffff);
      else
        fprintf (file, HOST_WIDE_INT_PRINT_DEC, INTVAL (x));
      break;

    case CONST_DOUBLE:
      gcc_assert (GET_MODE (x) == VOIDmode);
      if (code == 'b')
        fprintf (file, HOST_WIDE_INT_PRINT_DEC, CONST_DOUBLE_LOW (x) & 0xff);
      else if (code == 'x')
        fprintf (file, HOST_WIDE_INT_PRINT_DEC, CONST_DOUBLE_LOW (x) & 0xffff);
      else if (code == 'h')
        fprintf (file, HOST_WIDE_INT_PRINT_DEC, ((CONST_DOUBLE_LOW (x) & 0xffff) ^ 0x8000) - 0x8000);
      else
        gcc_unreachable ();
      break;

    default:
      fatal_insn ("UNKNOWN in print_operand !?", x);
      break;
    }
}

/* Target hook for assembling integer objects.  We need to define it
   here to work a round a bug in some versions of GAS, which couldn't
   handle values smaller than INT_MIN when printed in decimal.  */

static bool
s390_assemble_integer (rtx x, unsigned int size, int aligned_p)
{
  if (size == 8 && aligned_p
      && GET_CODE (x) == CONST_INT && INTVAL (x) < INT_MIN)
    {
      fprintf (asm_out_file, "\t.quad\t" HOST_WIDE_INT_PRINT_HEX "\n",
	       INTVAL (x));
      return true;
    }
  return default_assemble_integer (x, size, aligned_p);
}

/* Returns true if register REGNO is used  for forming
   a memory address in expression X.  */

static bool
reg_used_in_mem_p (int regno, rtx x)
{
  enum rtx_code code = GET_CODE (x);
  int i, j;
  const char *fmt;

  if (code == MEM)
    {
      if (refers_to_regno_p (regno, regno+1,
			     XEXP (x, 0), 0))
	return true;
    }
  else if (code == SET
	   && GET_CODE (SET_DEST (x)) == PC)
    {
      if (refers_to_regno_p (regno, regno+1,
			     SET_SRC (x), 0))
	return true;
    }

  fmt = GET_RTX_FORMAT (code);
  for (i = GET_RTX_LENGTH (code) - 1; i >= 0; i--)
    {
      if (fmt[i] == 'e'
	  && reg_used_in_mem_p (regno, XEXP (x, i)))
	return true;

      else if (fmt[i] == 'E')
	for (j = 0; j < XVECLEN (x, i); j++)
	  if (reg_used_in_mem_p (regno, XVECEXP (x, i, j)))
	    return true;
    }
  return false;
}

/* Returns true if expression DEP_RTX sets an address register
   used by instruction INSN to address memory.  */

static bool
addr_generation_dependency_p (rtx dep_rtx, rtx insn)
{
  rtx target, pat;

  if (GET_CODE (dep_rtx) == INSN)
      dep_rtx = PATTERN (dep_rtx);

  if (GET_CODE (dep_rtx) == SET)
    {
      target = SET_DEST (dep_rtx);
      if (GET_CODE (target) == STRICT_LOW_PART)
	target = XEXP (target, 0);
      while (GET_CODE (target) == SUBREG)
	target = SUBREG_REG (target);

      if (GET_CODE (target) == REG)
	{
	  int regno = REGNO (target);

	  if (s390_safe_attr_type (insn) == TYPE_LA)
	    {
	      pat = PATTERN (insn);
	      if (GET_CODE (pat) == PARALLEL)
		{
		  gcc_assert (XVECLEN (pat, 0) == 2);
		  pat = XVECEXP (pat, 0, 0);
		}
	      gcc_assert (GET_CODE (pat) == SET);
	      return refers_to_regno_p (regno, regno+1, SET_SRC (pat), 0);
	    }
	  else if (get_attr_atype (insn) == ATYPE_AGEN)
	    return reg_used_in_mem_p (regno, PATTERN (insn));
	}
    }
  return false;
}

/* Return 1, if dep_insn sets register used in insn in the agen unit.  */

int
s390_agen_dep_p (rtx dep_insn, rtx insn)
{
  rtx dep_rtx = PATTERN (dep_insn);
  int i;

  if (GET_CODE (dep_rtx) == SET
      && addr_generation_dependency_p (dep_rtx, insn))
    return 1;
  else if (GET_CODE (dep_rtx) == PARALLEL)
    {
      for (i = 0; i < XVECLEN (dep_rtx, 0); i++)
	{
	  if (addr_generation_dependency_p (XVECEXP (dep_rtx, 0, i), insn))
	    return 1;
	}
    }
  return 0;
}

/* A C statement (sans semicolon) to update the integer scheduling priority
   INSN_PRIORITY (INSN).  Increase the priority to execute the INSN earlier,
   reduce the priority to execute INSN later.  Do not define this macro if
   you do not need to adjust the scheduling priorities of insns.

   A STD instruction should be scheduled earlier,
   in order to use the bypass.  */

static int
s390_adjust_priority (rtx insn ATTRIBUTE_UNUSED, int priority)
{
  if (! INSN_P (insn))
    return priority;

  if (s390_tune != PROCESSOR_2084_Z990
      && s390_tune != PROCESSOR_2094_Z9_109)
    return priority;

  switch (s390_safe_attr_type (insn))
    {
      case TYPE_FSTOREDF:
      case TYPE_FSTORESF:
	priority = priority << 3;
	break;
      case TYPE_STORE:
      case TYPE_STM:
	priority = priority << 1;
	break;
      default:
        break;
    }
  return priority;
}

/* The number of instructions that can be issued per cycle.  */

static int
s390_issue_rate (void)
{
  if (s390_tune == PROCESSOR_2084_Z990
      || s390_tune == PROCESSOR_2094_Z9_109)
    return 3;
  return 1;
}

static int
s390_first_cycle_multipass_dfa_lookahead (void)
{
  return 4;
}


/* Annotate every literal pool reference in X by an UNSPEC_LTREF expression.
   Fix up MEMs as required.  */

static void
annotate_constant_pool_refs (rtx *x)
{
  int i, j;
  const char *fmt;

  gcc_assert (GET_CODE (*x) != SYMBOL_REF
	      || !CONSTANT_POOL_ADDRESS_P (*x));

  /* Literal pool references can only occur inside a MEM ...  */
  if (GET_CODE (*x) == MEM)
    {
      rtx memref = XEXP (*x, 0);

      if (GET_CODE (memref) == SYMBOL_REF
	  && CONSTANT_POOL_ADDRESS_P (memref))
	{
	  rtx base = cfun->machine->base_reg;
	  rtx addr = gen_rtx_UNSPEC (Pmode, gen_rtvec (2, memref, base),
				     UNSPEC_LTREF);

	  *x = replace_equiv_address (*x, addr);
	  return;
	}

      if (GET_CODE (memref) == CONST
	  && GET_CODE (XEXP (memref, 0)) == PLUS
	  && GET_CODE (XEXP (XEXP (memref, 0), 1)) == CONST_INT
	  && GET_CODE (XEXP (XEXP (memref, 0), 0)) == SYMBOL_REF
	  && CONSTANT_POOL_ADDRESS_P (XEXP (XEXP (memref, 0), 0)))
	{
	  HOST_WIDE_INT off = INTVAL (XEXP (XEXP (memref, 0), 1));
	  rtx sym = XEXP (XEXP (memref, 0), 0);
	  rtx base = cfun->machine->base_reg;
	  rtx addr = gen_rtx_UNSPEC (Pmode, gen_rtvec (2, sym, base),
				     UNSPEC_LTREF);

	  *x = replace_equiv_address (*x, plus_constant (addr, off));
	  return;
	}
    }

  /* ... or a load-address type pattern.  */
  if (GET_CODE (*x) == SET)
    {
      rtx addrref = SET_SRC (*x);

      if (GET_CODE (addrref) == SYMBOL_REF
	  && CONSTANT_POOL_ADDRESS_P (addrref))
	{
	  rtx base = cfun->machine->base_reg;
	  rtx addr = gen_rtx_UNSPEC (Pmode, gen_rtvec (2, addrref, base),
				     UNSPEC_LTREF);

	  SET_SRC (*x) = addr;
	  return;
	}

      if (GET_CODE (addrref) == CONST
	  && GET_CODE (XEXP (addrref, 0)) == PLUS
	  && GET_CODE (XEXP (XEXP (addrref, 0), 1)) == CONST_INT
	  && GET_CODE (XEXP (XEXP (addrref, 0), 0)) == SYMBOL_REF
	  && CONSTANT_POOL_ADDRESS_P (XEXP (XEXP (addrref, 0), 0)))
	{
	  HOST_WIDE_INT off = INTVAL (XEXP (XEXP (addrref, 0), 1));
	  rtx sym = XEXP (XEXP (addrref, 0), 0);
	  rtx base = cfun->machine->base_reg;
	  rtx addr = gen_rtx_UNSPEC (Pmode, gen_rtvec (2, sym, base),
				     UNSPEC_LTREF);

	  SET_SRC (*x) = plus_constant (addr, off);
	  return;
	}
    }

  /* Annotate LTREL_BASE as well.  */
  if (GET_CODE (*x) == UNSPEC
      && XINT (*x, 1) == UNSPEC_LTREL_BASE)
    {
      rtx base = cfun->machine->base_reg;
      *x = gen_rtx_UNSPEC (Pmode, gen_rtvec (2, XVECEXP (*x, 0, 0), base),
				  UNSPEC_LTREL_BASE);
      return;
    }

  fmt = GET_RTX_FORMAT (GET_CODE (*x));
  for (i = GET_RTX_LENGTH (GET_CODE (*x)) - 1; i >= 0; i--)
    {
      if (fmt[i] == 'e')
        {
          annotate_constant_pool_refs (&XEXP (*x, i));
        }
      else if (fmt[i] == 'E')
        {
          for (j = 0; j < XVECLEN (*x, i); j++)
            annotate_constant_pool_refs (&XVECEXP (*x, i, j));
        }
    }
}

/* Split all branches that exceed the maximum distance.
   Returns true if this created a new literal pool entry.  */

static int
s390_split_branches (void)
{
  rtx temp_reg = gen_rtx_REG (Pmode, RETURN_REGNUM);
  int new_literal = 0, ret;
  rtx insn, pat, tmp, target;
  rtx *label;

  /* We need correct insn addresses.  */

  shorten_branches (get_insns ());

  /* Find all branches that exceed 64KB, and split them.  */

  for (insn = get_insns (); insn; insn = NEXT_INSN (insn))
    {
      if (GET_CODE (insn) != JUMP_INSN)
	continue;

      pat = PATTERN (insn);
      if (GET_CODE (pat) == PARALLEL && XVECLEN (pat, 0) > 2)
	pat = XVECEXP (pat, 0, 0);
      if (GET_CODE (pat) != SET || SET_DEST (pat) != pc_rtx)
	continue;

      if (GET_CODE (SET_SRC (pat)) == LABEL_REF)
	{
	  label = &SET_SRC (pat);
	}
      else if (GET_CODE (SET_SRC (pat)) == IF_THEN_ELSE)
	{
	  if (GET_CODE (XEXP (SET_SRC (pat), 1)) == LABEL_REF)
	    label = &XEXP (SET_SRC (pat), 1);
          else if (GET_CODE (XEXP (SET_SRC (pat), 2)) == LABEL_REF)
            label = &XEXP (SET_SRC (pat), 2);
	  else
	    continue;
        }
      else
	continue;

      if (get_attr_length (insn) <= 4)
	continue;

      /* We are going to use the return register as scratch register,
	 make sure it will be saved/restored by the prologue/epilogue.  */
      cfun_frame_layout.save_return_addr_p = 1;

      if (!flag_pic)
	{
	  new_literal = 1;
	  tmp = force_const_mem (Pmode, *label);
	  tmp = emit_insn_before (gen_rtx_SET (Pmode, temp_reg, tmp), insn);
	  INSN_ADDRESSES_NEW (tmp, -1);
	  annotate_constant_pool_refs (&PATTERN (tmp));

	  target = temp_reg;
	}
      else
	{
	  new_literal = 1;
	  target = gen_rtx_UNSPEC (Pmode, gen_rtvec (1, *label),
				   UNSPEC_LTREL_OFFSET);
	  target = gen_rtx_CONST (Pmode, target);
	  target = force_const_mem (Pmode, target);
	  tmp = emit_insn_before (gen_rtx_SET (Pmode, temp_reg, target), insn);
	  INSN_ADDRESSES_NEW (tmp, -1);
	  annotate_constant_pool_refs (&PATTERN (tmp));

          target = gen_rtx_UNSPEC (Pmode, gen_rtvec (2, XEXP (target, 0),
							cfun->machine->base_reg),
				   UNSPEC_LTREL_BASE);
	  target = gen_rtx_PLUS (Pmode, temp_reg, target);
	}

      ret = validate_change (insn, label, target, 0);
      gcc_assert (ret);
    }

  return new_literal;
}


/* Find an annotated literal pool symbol referenced in RTX X, 
   and store it at REF.  Will abort if X contains references to 
   more than one such pool symbol; multiple references to the same
   symbol are allowed, however.

   The rtx pointed to by REF must be initialized to NULL_RTX
   by the caller before calling this routine.  */

static void
find_constant_pool_ref (rtx x, rtx *ref)
{
  int i, j;
  const char *fmt;

  /* Ignore LTREL_BASE references.  */
  if (GET_CODE (x) == UNSPEC
      && XINT (x, 1) == UNSPEC_LTREL_BASE)
    return;
  /* Likewise POOL_ENTRY insns.  */
  if (GET_CODE (x) == UNSPEC_VOLATILE
      && XINT (x, 1) == UNSPECV_POOL_ENTRY)
    return;

  gcc_assert (GET_CODE (x) != SYMBOL_REF
              || !CONSTANT_POOL_ADDRESS_P (x));

  if (GET_CODE (x) == UNSPEC && XINT (x, 1) == UNSPEC_LTREF)
    {
      rtx sym = XVECEXP (x, 0, 0);
      gcc_assert (GET_CODE (sym) == SYMBOL_REF
	          && CONSTANT_POOL_ADDRESS_P (sym));

      if (*ref == NULL_RTX)
	*ref = sym;
      else 
	gcc_assert (*ref == sym);

      return;
    }

  fmt = GET_RTX_FORMAT (GET_CODE (x));
  for (i = GET_RTX_LENGTH (GET_CODE (x)) - 1; i >= 0; i--)
    {
      if (fmt[i] == 'e')
        {
          find_constant_pool_ref (XEXP (x, i), ref);
        }
      else if (fmt[i] == 'E')
        {
          for (j = 0; j < XVECLEN (x, i); j++)
            find_constant_pool_ref (XVECEXP (x, i, j), ref);
        }
    }
}

/* Replace every reference to the annotated literal pool 
   symbol REF in X by its base plus OFFSET.  */

static void
replace_constant_pool_ref (rtx *x, rtx ref, rtx offset)
{
  int i, j;
  const char *fmt;

  gcc_assert (*x != ref);

  if (GET_CODE (*x) == UNSPEC
      && XINT (*x, 1) == UNSPEC_LTREF
      && XVECEXP (*x, 0, 0) == ref)
    {
      *x = gen_rtx_PLUS (Pmode, XVECEXP (*x, 0, 1), offset);
      return;
    }

  if (GET_CODE (*x) == PLUS
      && GET_CODE (XEXP (*x, 1)) == CONST_INT
      && GET_CODE (XEXP (*x, 0)) == UNSPEC
      && XINT (XEXP (*x, 0), 1) == UNSPEC_LTREF
      && XVECEXP (XEXP (*x, 0), 0, 0) == ref)
    {
      rtx addr = gen_rtx_PLUS (Pmode, XVECEXP (XEXP (*x, 0), 0, 1), offset);
      *x = plus_constant (addr, INTVAL (XEXP (*x, 1)));
      return;
    }

  fmt = GET_RTX_FORMAT (GET_CODE (*x));
  for (i = GET_RTX_LENGTH (GET_CODE (*x)) - 1; i >= 0; i--)
    {
      if (fmt[i] == 'e')
        {
          replace_constant_pool_ref (&XEXP (*x, i), ref, offset);
        }
      else if (fmt[i] == 'E')
        {
          for (j = 0; j < XVECLEN (*x, i); j++)
            replace_constant_pool_ref (&XVECEXP (*x, i, j), ref, offset);
        }
    }
}

/* Check whether X contains an UNSPEC_LTREL_BASE.
   Return its constant pool symbol if found, NULL_RTX otherwise.  */

static rtx
find_ltrel_base (rtx x)
{
  int i, j;
  const char *fmt;

  if (GET_CODE (x) == UNSPEC
      && XINT (x, 1) == UNSPEC_LTREL_BASE)
    return XVECEXP (x, 0, 0);

  fmt = GET_RTX_FORMAT (GET_CODE (x));
  for (i = GET_RTX_LENGTH (GET_CODE (x)) - 1; i >= 0; i--)
    {
      if (fmt[i] == 'e')
        {
          rtx fnd = find_ltrel_base (XEXP (x, i));
	  if (fnd)
	    return fnd;
        }
      else if (fmt[i] == 'E')
        {
          for (j = 0; j < XVECLEN (x, i); j++)
	    {
              rtx fnd = find_ltrel_base (XVECEXP (x, i, j));
	      if (fnd)
		return fnd;
	    }
        }
    }

  return NULL_RTX;
}

/* Replace any occurrence of UNSPEC_LTREL_BASE in X with its base.  */

static void
replace_ltrel_base (rtx *x)
{
  int i, j;
  const char *fmt;

  if (GET_CODE (*x) == UNSPEC
      && XINT (*x, 1) == UNSPEC_LTREL_BASE)
    {
      *x = XVECEXP (*x, 0, 1);
      return;
    }

  fmt = GET_RTX_FORMAT (GET_CODE (*x));
  for (i = GET_RTX_LENGTH (GET_CODE (*x)) - 1; i >= 0; i--)
    {
      if (fmt[i] == 'e')
        {
          replace_ltrel_base (&XEXP (*x, i));
        }
      else if (fmt[i] == 'E')
        {
          for (j = 0; j < XVECLEN (*x, i); j++)
            replace_ltrel_base (&XVECEXP (*x, i, j));
        }
    }
}


/* We keep a list of constants which we have to add to internal
   constant tables in the middle of large functions.  */

#define NR_C_MODES 11
enum machine_mode constant_modes[NR_C_MODES] =
{
  TFmode, TImode, TDmode,
  DFmode, DImode, DDmode,
  SFmode, SImode, SDmode,
  HImode,
  QImode
};

struct constant
{
  struct constant *next;
  rtx value;
  rtx label;
};

struct constant_pool
{
  struct constant_pool *next;
  rtx first_insn;
  rtx pool_insn;
  bitmap insns;

  struct constant *constants[NR_C_MODES];
  struct constant *execute;
  rtx label;
  int size;
};

/* Allocate new constant_pool structure.  */

static struct constant_pool *
s390_alloc_pool (void)
{
  struct constant_pool *pool;
  int i;

  pool = (struct constant_pool *) xmalloc (sizeof *pool);
  pool->next = NULL;
  for (i = 0; i < NR_C_MODES; i++)
    pool->constants[i] = NULL;

  pool->execute = NULL;
  pool->label = gen_label_rtx ();
  pool->first_insn = NULL_RTX;
  pool->pool_insn = NULL_RTX;
  pool->insns = BITMAP_ALLOC (NULL);
  pool->size = 0;

  return pool;
}

/* Create new constant pool covering instructions starting at INSN
   and chain it to the end of POOL_LIST.  */

static struct constant_pool *
s390_start_pool (struct constant_pool **pool_list, rtx insn)
{
  struct constant_pool *pool, **prev;

  pool = s390_alloc_pool ();
  pool->first_insn = insn;

  for (prev = pool_list; *prev; prev = &(*prev)->next)
    ;
  *prev = pool;

  return pool;
}

/* End range of instructions covered by POOL at INSN and emit
   placeholder insn representing the pool.  */

static void
s390_end_pool (struct constant_pool *pool, rtx insn)
{
  rtx pool_size = GEN_INT (pool->size + 8 /* alignment slop */);

  if (!insn)
    insn = get_last_insn ();

  pool->pool_insn = emit_insn_after (gen_pool (pool_size), insn);
  INSN_ADDRESSES_NEW (pool->pool_insn, -1);
}

/* Add INSN to the list of insns covered by POOL.  */

static void
s390_add_pool_insn (struct constant_pool *pool, rtx insn)
{
  bitmap_set_bit (pool->insns, INSN_UID (insn));
}

/* Return pool out of POOL_LIST that covers INSN.  */

static struct constant_pool *
s390_find_pool (struct constant_pool *pool_list, rtx insn)
{
  struct constant_pool *pool;

  for (pool = pool_list; pool; pool = pool->next)
    if (bitmap_bit_p (pool->insns, INSN_UID (insn)))
      break;

  return pool;
}

/* Add constant VAL of mode MODE to the constant pool POOL.  */

static void
s390_add_constant (struct constant_pool *pool, rtx val, enum machine_mode mode)
{
  struct constant *c;
  int i;

  for (i = 0; i < NR_C_MODES; i++)
    if (constant_modes[i] == mode)
      break;
  gcc_assert (i != NR_C_MODES);

  for (c = pool->constants[i]; c != NULL; c = c->next)
    if (rtx_equal_p (val, c->value))
      break;

  if (c == NULL)
    {
      c = (struct constant *) xmalloc (sizeof *c);
      c->value = val;
      c->label = gen_label_rtx ();
      c->next = pool->constants[i];
      pool->constants[i] = c;
      pool->size += GET_MODE_SIZE (mode);
    }
}

/* Find constant VAL of mode MODE in the constant pool POOL.
   Return an RTX describing the distance from the start of
   the pool to the location of the new constant.  */

static rtx
s390_find_constant (struct constant_pool *pool, rtx val,
		    enum machine_mode mode)
{
  struct constant *c;
  rtx offset;
  int i;

  for (i = 0; i < NR_C_MODES; i++)
    if (constant_modes[i] == mode)
      break;
  gcc_assert (i != NR_C_MODES);

  for (c = pool->constants[i]; c != NULL; c = c->next)
    if (rtx_equal_p (val, c->value))
      break;

  gcc_assert (c);

  offset = gen_rtx_MINUS (Pmode, gen_rtx_LABEL_REF (Pmode, c->label),
                                 gen_rtx_LABEL_REF (Pmode, pool->label));
  offset = gen_rtx_CONST (Pmode, offset);
  return offset;
}

/* Check whether INSN is an execute.  Return the label_ref to its
   execute target template if so, NULL_RTX otherwise.  */

static rtx
s390_execute_label (rtx insn)
{
  if (GET_CODE (insn) == INSN
      && GET_CODE (PATTERN (insn)) == PARALLEL
      && GET_CODE (XVECEXP (PATTERN (insn), 0, 0)) == UNSPEC
      && XINT (XVECEXP (PATTERN (insn), 0, 0), 1) == UNSPEC_EXECUTE)
    return XVECEXP (XVECEXP (PATTERN (insn), 0, 0), 0, 2);

  return NULL_RTX;
}

/* Add execute target for INSN to the constant pool POOL.  */

static void
s390_add_execute (struct constant_pool *pool, rtx insn)
{
  struct constant *c;

  for (c = pool->execute; c != NULL; c = c->next)
    if (INSN_UID (insn) == INSN_UID (c->value))
      break;

  if (c == NULL)
    {
      c = (struct constant *) xmalloc (sizeof *c);
      c->value = insn;
      c->label = gen_label_rtx ();
      c->next = pool->execute;
      pool->execute = c;
      pool->size += 6;
    }
}

/* Find execute target for INSN in the constant pool POOL.
   Return an RTX describing the distance from the start of
   the pool to the location of the execute target.  */

static rtx
s390_find_execute (struct constant_pool *pool, rtx insn)
{
  struct constant *c;
  rtx offset;

  for (c = pool->execute; c != NULL; c = c->next)
    if (INSN_UID (insn) == INSN_UID (c->value))
      break;

  gcc_assert (c);

  offset = gen_rtx_MINUS (Pmode, gen_rtx_LABEL_REF (Pmode, c->label),
				 gen_rtx_LABEL_REF (Pmode, pool->label));
  offset = gen_rtx_CONST (Pmode, offset);
  return offset;
}

/* For an execute INSN, extract the execute target template.  */

static rtx
s390_execute_target (rtx insn)
{
  rtx pattern = PATTERN (insn);
  gcc_assert (s390_execute_label (insn));

  if (XVECLEN (pattern, 0) == 2)
    {
      pattern = copy_rtx (XVECEXP (pattern, 0, 1));
    }
  else
    {
      rtvec vec = rtvec_alloc (XVECLEN (pattern, 0) - 1);
      int i;

      for (i = 0; i < XVECLEN (pattern, 0) - 1; i++)
	RTVEC_ELT (vec, i) = copy_rtx (XVECEXP (pattern, 0, i + 1));

      pattern = gen_rtx_PARALLEL (VOIDmode, vec);
    }

  return pattern;
}

/* Indicate that INSN cannot be duplicated.  This is the case for
   execute insns that carry a unique label.  */

static bool
s390_cannot_copy_insn_p (rtx insn)
{
  rtx label = s390_execute_label (insn);
  return label && label != const0_rtx;
}

/* Dump out the constants in POOL.  If REMOTE_LABEL is true,
   do not emit the pool base label.  */

static void
s390_dump_pool (struct constant_pool *pool, bool remote_label)
{
  struct constant *c;
  rtx insn = pool->pool_insn;
  int i;

  /* Switch to rodata section.  */
  if (TARGET_CPU_ZARCH)
    {
      insn = emit_insn_after (gen_pool_section_start (), insn);
      INSN_ADDRESSES_NEW (insn, -1);
    }

  /* Ensure minimum pool alignment.  */
  if (TARGET_CPU_ZARCH)
    insn = emit_insn_after (gen_pool_align (GEN_INT (8)), insn);
  else
    insn = emit_insn_after (gen_pool_align (GEN_INT (4)), insn);
  INSN_ADDRESSES_NEW (insn, -1);

  /* Emit pool base label.  */
  if (!remote_label)
    {
      insn = emit_label_after (pool->label, insn);
      INSN_ADDRESSES_NEW (insn, -1);
    }

  /* Dump constants in descending alignment requirement order,
     ensuring proper alignment for every constant.  */
  for (i = 0; i < NR_C_MODES; i++)
    for (c = pool->constants[i]; c; c = c->next)
      {
	/* Convert UNSPEC_LTREL_OFFSET unspecs to pool-relative references.  */
	rtx value = c->value;
	if (GET_CODE (value) == CONST
	    && GET_CODE (XEXP (value, 0)) == UNSPEC
	    && XINT (XEXP (value, 0), 1) == UNSPEC_LTREL_OFFSET
	    && XVECLEN (XEXP (value, 0), 0) == 1)
	  {
	    value = gen_rtx_MINUS (Pmode, XVECEXP (XEXP (value, 0), 0, 0),
				   gen_rtx_LABEL_REF (VOIDmode, pool->label));
	    value = gen_rtx_CONST (VOIDmode, value);
	  }

	insn = emit_label_after (c->label, insn);
	INSN_ADDRESSES_NEW (insn, -1);

	value = gen_rtx_UNSPEC_VOLATILE (constant_modes[i],
					 gen_rtvec (1, value),
					 UNSPECV_POOL_ENTRY);
	insn = emit_insn_after (value, insn);
	INSN_ADDRESSES_NEW (insn, -1);
      }

  /* Ensure minimum alignment for instructions.  */
  insn = emit_insn_after (gen_pool_align (GEN_INT (2)), insn);
  INSN_ADDRESSES_NEW (insn, -1);

  /* Output in-pool execute template insns.  */
  for (c = pool->execute; c; c = c->next)
    {
      insn = emit_label_after (c->label, insn);
      INSN_ADDRESSES_NEW (insn, -1);

      insn = emit_insn_after (s390_execute_target (c->value), insn);
      INSN_ADDRESSES_NEW (insn, -1);
    }

  /* Switch back to previous section.  */
  if (TARGET_CPU_ZARCH)
    {
      insn = emit_insn_after (gen_pool_section_end (), insn);
      INSN_ADDRESSES_NEW (insn, -1);
    }

  insn = emit_barrier_after (insn);
  INSN_ADDRESSES_NEW (insn, -1);

  /* Remove placeholder insn.  */
  remove_insn (pool->pool_insn);
}

/* Free all memory used by POOL.  */

static void
s390_free_pool (struct constant_pool *pool)
{
  struct constant *c, *next;
  int i;

  for (i = 0; i < NR_C_MODES; i++)
    for (c = pool->constants[i]; c; c = next)
      {
	next = c->next;
	free (c);
      }

  for (c = pool->execute; c; c = next)
    {
      next = c->next;
      free (c);
    }

  BITMAP_FREE (pool->insns);
  free (pool);
}


/* Collect main literal pool.  Return NULL on overflow.  */

static struct constant_pool *
s390_mainpool_start (void)
{
  struct constant_pool *pool;
  rtx insn;

  pool = s390_alloc_pool ();

  for (insn = get_insns (); insn; insn = NEXT_INSN (insn))
    {
      if (GET_CODE (insn) == INSN
	  && GET_CODE (PATTERN (insn)) == SET
	  && GET_CODE (SET_SRC (PATTERN (insn))) == UNSPEC_VOLATILE
	  && XINT (SET_SRC (PATTERN (insn)), 1) == UNSPECV_MAIN_POOL)
	{
	  gcc_assert (!pool->pool_insn);
	  pool->pool_insn = insn;
	}

      if (!TARGET_CPU_ZARCH && s390_execute_label (insn))
	{
	  s390_add_execute (pool, insn);
	}
      else if (GET_CODE (insn) == INSN || GET_CODE (insn) == CALL_INSN)
	{
	  rtx pool_ref = NULL_RTX;
	  find_constant_pool_ref (PATTERN (insn), &pool_ref);
	  if (pool_ref)
	    {
	      rtx constant = get_pool_constant (pool_ref);
	      enum machine_mode mode = get_pool_mode (pool_ref);
	      s390_add_constant (pool, constant, mode);
	    }
	}
    }

  gcc_assert (pool->pool_insn || pool->size == 0);

  if (pool->size >= 4096)
    {
      /* We're going to chunkify the pool, so remove the main
	 pool placeholder insn.  */
      remove_insn (pool->pool_insn);

      s390_free_pool (pool);
      pool = NULL;
    }

  return pool;
}

/* POOL holds the main literal pool as collected by s390_mainpool_start.
   Modify the current function to output the pool constants as well as
   the pool register setup instruction.  */

static void
s390_mainpool_finish (struct constant_pool *pool)
{
  rtx base_reg = cfun->machine->base_reg;
  rtx insn;

  /* If the pool is empty, we're done.  */
  if (pool->size == 0)
    {
      /* We don't actually need a base register after all.  */
      cfun->machine->base_reg = NULL_RTX;

      if (pool->pool_insn)
	remove_insn (pool->pool_insn);
      s390_free_pool (pool);
      return;
    }

  /* We need correct insn addresses.  */
  shorten_branches (get_insns ());

  /* On zSeries, we use a LARL to load the pool register.  The pool is
     located in the .rodata section, so we emit it after the function.  */
  if (TARGET_CPU_ZARCH)
    {
      insn = gen_main_base_64 (base_reg, pool->label);
      insn = emit_insn_after (insn, pool->pool_insn);
      INSN_ADDRESSES_NEW (insn, -1);
      remove_insn (pool->pool_insn);

      insn = get_last_insn ();
      pool->pool_insn = emit_insn_after (gen_pool (const0_rtx), insn);
      INSN_ADDRESSES_NEW (pool->pool_insn, -1);

      s390_dump_pool (pool, 0);
    }

  /* On S/390, if the total size of the function's code plus literal pool
     does not exceed 4096 bytes, we use BASR to set up a function base
     pointer, and emit the literal pool at the end of the function.  */
  else if (INSN_ADDRESSES (INSN_UID (get_last_insn ()))
	   + pool->size + 8 /* alignment slop */ < 4096)
    {
      insn = gen_main_base_31_small (base_reg, pool->label);
      insn = emit_insn_after (insn, pool->pool_insn);
      INSN_ADDRESSES_NEW (insn, -1);
      remove_insn (pool->pool_insn);

      insn = emit_label_after (pool->label, insn);
      INSN_ADDRESSES_NEW (insn, -1);

      insn = get_last_insn ();
      pool->pool_insn = emit_insn_after (gen_pool (const0_rtx), insn);
      INSN_ADDRESSES_NEW (pool->pool_insn, -1);

      s390_dump_pool (pool, 1);
    }

  /* Otherwise, we emit an inline literal pool and use BASR to branch
     over it, setting up the pool register at the same time.  */
  else
    {
      rtx pool_end = gen_label_rtx ();

      insn = gen_main_base_31_large (base_reg, pool->label, pool_end);
      insn = emit_insn_after (insn, pool->pool_insn);
      INSN_ADDRESSES_NEW (insn, -1);
      remove_insn (pool->pool_insn);

      insn = emit_label_after (pool->label, insn);
      INSN_ADDRESSES_NEW (insn, -1);

      pool->pool_insn = emit_insn_after (gen_pool (const0_rtx), insn);
      INSN_ADDRESSES_NEW (pool->pool_insn, -1);

      insn = emit_label_after (pool_end, pool->pool_insn);
      INSN_ADDRESSES_NEW (insn, -1);

      s390_dump_pool (pool, 1);
    }


  /* Replace all literal pool references.  */

  for (insn = get_insns (); insn; insn = NEXT_INSN (insn))
    {
      if (INSN_P (insn))
	replace_ltrel_base (&PATTERN (insn));

      if (GET_CODE (insn) == INSN || GET_CODE (insn) == CALL_INSN)
        {
          rtx addr, pool_ref = NULL_RTX;
          find_constant_pool_ref (PATTERN (insn), &pool_ref);
          if (pool_ref)
            {
	      if (s390_execute_label (insn))
		addr = s390_find_execute (pool, insn);
	      else
		addr = s390_find_constant (pool, get_pool_constant (pool_ref),
						 get_pool_mode (pool_ref));

              replace_constant_pool_ref (&PATTERN (insn), pool_ref, addr);
              INSN_CODE (insn) = -1;
            }
        }
    }


  /* Free the pool.  */
  s390_free_pool (pool);
}

/* POOL holds the main literal pool as collected by s390_mainpool_start.
   We have decided we cannot use this pool, so revert all changes
   to the current function that were done by s390_mainpool_start.  */
static void
s390_mainpool_cancel (struct constant_pool *pool)
{
  /* We didn't actually change the instruction stream, so simply
     free the pool memory.  */
  s390_free_pool (pool);
}


/* Chunkify the literal pool.  */

#define S390_POOL_CHUNK_MIN	0xc00
#define S390_POOL_CHUNK_MAX	0xe00

static struct constant_pool *
s390_chunkify_start (void)
{
  struct constant_pool *curr_pool = NULL, *pool_list = NULL;
  int extra_size = 0;
  bitmap far_labels;
  rtx pending_ltrel = NULL_RTX;
  rtx insn;

  rtx (*gen_reload_base) (rtx, rtx) =
    TARGET_CPU_ZARCH? gen_reload_base_64 : gen_reload_base_31;


  /* We need correct insn addresses.  */

  shorten_branches (get_insns ());

  /* Scan all insns and move literals to pool chunks.  */

  for (insn = get_insns (); insn; insn = NEXT_INSN (insn))
    {
      /* Check for pending LTREL_BASE.  */
      if (INSN_P (insn))
	{
	  rtx ltrel_base = find_ltrel_base (PATTERN (insn));
	  if (ltrel_base)
	    {
	      gcc_assert (ltrel_base == pending_ltrel);
	      pending_ltrel = NULL_RTX;
	    }
	}

      if (!TARGET_CPU_ZARCH && s390_execute_label (insn))
	{
	  if (!curr_pool)
	    curr_pool = s390_start_pool (&pool_list, insn);

	  s390_add_execute (curr_pool, insn);
	  s390_add_pool_insn (curr_pool, insn);
	}
      else if (GET_CODE (insn) == INSN || GET_CODE (insn) == CALL_INSN)
	{
	  rtx pool_ref = NULL_RTX;
	  find_constant_pool_ref (PATTERN (insn), &pool_ref);
	  if (pool_ref)
	    {
	      rtx constant = get_pool_constant (pool_ref);
	      enum machine_mode mode = get_pool_mode (pool_ref);

	      if (!curr_pool)
		curr_pool = s390_start_pool (&pool_list, insn);

	      s390_add_constant (curr_pool, constant, mode);
	      s390_add_pool_insn (curr_pool, insn);

	      /* Don't split the pool chunk between a LTREL_OFFSET load
		 and the corresponding LTREL_BASE.  */
	      if (GET_CODE (constant) == CONST
		  && GET_CODE (XEXP (constant, 0)) == UNSPEC
		  && XINT (XEXP (constant, 0), 1) == UNSPEC_LTREL_OFFSET)
		{
		  gcc_assert (!pending_ltrel);
		  pending_ltrel = pool_ref;
		}
	    }
	}

      if (GET_CODE (insn) == JUMP_INSN || GET_CODE (insn) == CODE_LABEL)
	{
	  if (curr_pool)
	    s390_add_pool_insn (curr_pool, insn);
	  /* An LTREL_BASE must follow within the same basic block.  */
	  gcc_assert (!pending_ltrel);
	}

      if (!curr_pool
	  || INSN_ADDRESSES_SIZE () <= (size_t) INSN_UID (insn)
          || INSN_ADDRESSES (INSN_UID (insn)) == -1)
	continue;

      if (TARGET_CPU_ZARCH)
	{
	  if (curr_pool->size < S390_POOL_CHUNK_MAX)
	    continue;

	  s390_end_pool (curr_pool, NULL_RTX);
	  curr_pool = NULL;
	}
      else
	{
          int chunk_size = INSN_ADDRESSES (INSN_UID (insn))
			   - INSN_ADDRESSES (INSN_UID (curr_pool->first_insn))
			 + extra_size;

	  /* We will later have to insert base register reload insns.
	     Those will have an effect on code size, which we need to
	     consider here.  This calculation makes rather pessimistic
	     worst-case assumptions.  */
	  if (GET_CODE (insn) == CODE_LABEL)
	    extra_size += 6;

	  if (chunk_size < S390_POOL_CHUNK_MIN
	      && curr_pool->size < S390_POOL_CHUNK_MIN)
	    continue;

	  /* Pool chunks can only be inserted after BARRIERs ...  */
	  if (GET_CODE (insn) == BARRIER)
	    {
	      s390_end_pool (curr_pool, insn);
	      curr_pool = NULL;
	      extra_size = 0;
	    }

	  /* ... so if we don't find one in time, create one.  */
          else if ((chunk_size > S390_POOL_CHUNK_MAX
	           || curr_pool->size > S390_POOL_CHUNK_MAX))
	    {
              rtx label, jump, barrier;

	      /* We can insert the barrier only after a 'real' insn.  */
	      if (GET_CODE (insn) != INSN && GET_CODE (insn) != CALL_INSN)
		continue;
	      if (get_attr_length (insn) == 0)
		continue;

	      /* Don't separate LTREL_BASE from the corresponding
		 LTREL_OFFSET load.  */
	      if (pending_ltrel)
		continue;

	      label = gen_label_rtx ();
	      jump = emit_jump_insn_after (gen_jump (label), insn);
	      barrier = emit_barrier_after (jump);
	      insn = emit_label_after (label, barrier);
	      JUMP_LABEL (jump) = label;
	      LABEL_NUSES (label) = 1;

	      INSN_ADDRESSES_NEW (jump, -1);
	      INSN_ADDRESSES_NEW (barrier, -1);
	      INSN_ADDRESSES_NEW (insn, -1);

	      s390_end_pool (curr_pool, barrier);
	      curr_pool = NULL;
	      extra_size = 0;
	    }
	}
    }

  if (curr_pool)
    s390_end_pool (curr_pool, NULL_RTX);
  gcc_assert (!pending_ltrel);

  /* Find all labels that are branched into
     from an insn belonging to a different chunk.  */

  far_labels = BITMAP_ALLOC (NULL);

  for (insn = get_insns (); insn; insn = NEXT_INSN (insn))
    {
      /* Labels marked with LABEL_PRESERVE_P can be target
	 of non-local jumps, so we have to mark them.
	 The same holds for named labels.

	 Don't do that, however, if it is the label before
	 a jump table.  */

      if (GET_CODE (insn) == CODE_LABEL
	  && (LABEL_PRESERVE_P (insn) || LABEL_NAME (insn)))
	{
	  rtx vec_insn = next_real_insn (insn);
	  rtx vec_pat = vec_insn && GET_CODE (vec_insn) == JUMP_INSN ?
			PATTERN (vec_insn) : NULL_RTX;
	  if (!vec_pat
	      || !(GET_CODE (vec_pat) == ADDR_VEC
		   || GET_CODE (vec_pat) == ADDR_DIFF_VEC))
	    bitmap_set_bit (far_labels, CODE_LABEL_NUMBER (insn));
	}

      /* If we have a direct jump (conditional or unconditional)
	 or a casesi jump, check all potential targets.  */
      else if (GET_CODE (insn) == JUMP_INSN)
	{
          rtx pat = PATTERN (insn);
	  if (GET_CODE (pat) == PARALLEL && XVECLEN (pat, 0) > 2)
	    pat = XVECEXP (pat, 0, 0);

          if (GET_CODE (pat) == SET)
            {
	      rtx label = JUMP_LABEL (insn);
	      if (label)
		{
	          if (s390_find_pool (pool_list, label)
		      != s390_find_pool (pool_list, insn))
		    bitmap_set_bit (far_labels, CODE_LABEL_NUMBER (label));
		}
            }
	  else if (GET_CODE (pat) == PARALLEL
		   && XVECLEN (pat, 0) == 2
		   && GET_CODE (XVECEXP (pat, 0, 0)) == SET
		   && GET_CODE (XVECEXP (pat, 0, 1)) == USE
		   && GET_CODE (XEXP (XVECEXP (pat, 0, 1), 0)) == LABEL_REF)
	    {
	      /* Find the jump table used by this casesi jump.  */
	      rtx vec_label = XEXP (XEXP (XVECEXP (pat, 0, 1), 0), 0);
	      rtx vec_insn = next_real_insn (vec_label);
	      rtx vec_pat = vec_insn && GET_CODE (vec_insn) == JUMP_INSN ?
			    PATTERN (vec_insn) : NULL_RTX;
	      if (vec_pat
		  && (GET_CODE (vec_pat) == ADDR_VEC
		      || GET_CODE (vec_pat) == ADDR_DIFF_VEC))
		{
		  int i, diff_p = GET_CODE (vec_pat) == ADDR_DIFF_VEC;

		  for (i = 0; i < XVECLEN (vec_pat, diff_p); i++)
		    {
		      rtx label = XEXP (XVECEXP (vec_pat, diff_p, i), 0);

		      if (s390_find_pool (pool_list, label)
			  != s390_find_pool (pool_list, insn))
			bitmap_set_bit (far_labels, CODE_LABEL_NUMBER (label));
		    }
		}
	    }
        }
    }

  /* Insert base register reload insns before every pool.  */

  for (curr_pool = pool_list; curr_pool; curr_pool = curr_pool->next)
    {
      rtx new_insn = gen_reload_base (cfun->machine->base_reg, 
				      curr_pool->label);
      rtx insn = curr_pool->first_insn;
      INSN_ADDRESSES_NEW (emit_insn_before (new_insn, insn), -1);
    }

  /* Insert base register reload insns at every far label.  */

  for (insn = get_insns (); insn; insn = NEXT_INSN (insn))
    if (GET_CODE (insn) == CODE_LABEL
        && bitmap_bit_p (far_labels, CODE_LABEL_NUMBER (insn)))
      {
	struct constant_pool *pool = s390_find_pool (pool_list, insn);
	if (pool)
	  {
	    rtx new_insn = gen_reload_base (cfun->machine->base_reg, 
					    pool->label);
	    INSN_ADDRESSES_NEW (emit_insn_after (new_insn, insn), -1);
	  }
      }


  BITMAP_FREE (far_labels);


  /* Recompute insn addresses.  */

  init_insn_lengths ();
  shorten_branches (get_insns ());

  return pool_list;
}

/* POOL_LIST is a chunk list as prepared by s390_chunkify_start.
   After we have decided to use this list, finish implementing
   all changes to the current function as required.  */

static void
s390_chunkify_finish (struct constant_pool *pool_list)
{
  struct constant_pool *curr_pool = NULL;
  rtx insn;


  /* Replace all literal pool references.  */

  for (insn = get_insns (); insn; insn = NEXT_INSN (insn))
    {
      if (INSN_P (insn))
	replace_ltrel_base (&PATTERN (insn));

      curr_pool = s390_find_pool (pool_list, insn);
      if (!curr_pool)
	continue;

      if (GET_CODE (insn) == INSN || GET_CODE (insn) == CALL_INSN)
        {
          rtx addr, pool_ref = NULL_RTX;
          find_constant_pool_ref (PATTERN (insn), &pool_ref);
          if (pool_ref)
            {
	      if (s390_execute_label (insn))
		addr = s390_find_execute (curr_pool, insn);
	      else
		addr = s390_find_constant (curr_pool,
					   get_pool_constant (pool_ref),
					   get_pool_mode (pool_ref));

              replace_constant_pool_ref (&PATTERN (insn), pool_ref, addr);
              INSN_CODE (insn) = -1;
            }
        }
    }

  /* Dump out all literal pools.  */

  for (curr_pool = pool_list; curr_pool; curr_pool = curr_pool->next)
    s390_dump_pool (curr_pool, 0);

  /* Free pool list.  */

  while (pool_list)
    {
      struct constant_pool *next = pool_list->next;
      s390_free_pool (pool_list);
      pool_list = next;
    }
}

/* POOL_LIST is a chunk list as prepared by s390_chunkify_start.
   We have decided we cannot use this list, so revert all changes
   to the current function that were done by s390_chunkify_start.  */

static void
s390_chunkify_cancel (struct constant_pool *pool_list)
{
  struct constant_pool *curr_pool = NULL;
  rtx insn;

  /* Remove all pool placeholder insns.  */

  for (curr_pool = pool_list; curr_pool; curr_pool = curr_pool->next)
    {
      /* Did we insert an extra barrier?  Remove it.  */
      rtx barrier = PREV_INSN (curr_pool->pool_insn);
      rtx jump = barrier? PREV_INSN (barrier) : NULL_RTX;
      rtx label = NEXT_INSN (curr_pool->pool_insn);

      if (jump && GET_CODE (jump) == JUMP_INSN
	  && barrier && GET_CODE (barrier) == BARRIER
	  && label && GET_CODE (label) == CODE_LABEL
	  && GET_CODE (PATTERN (jump)) == SET
	  && SET_DEST (PATTERN (jump)) == pc_rtx
	  && GET_CODE (SET_SRC (PATTERN (jump))) == LABEL_REF
	  && XEXP (SET_SRC (PATTERN (jump)), 0) == label)
	{
	  remove_insn (jump);
	  remove_insn (barrier);
	  remove_insn (label);
	}

      remove_insn (curr_pool->pool_insn);
    }

  /* Remove all base register reload insns.  */

  for (insn = get_insns (); insn; )
    {
      rtx next_insn = NEXT_INSN (insn);

      if (GET_CODE (insn) == INSN
	  && GET_CODE (PATTERN (insn)) == SET
	  && GET_CODE (SET_SRC (PATTERN (insn))) == UNSPEC
	  && XINT (SET_SRC (PATTERN (insn)), 1) == UNSPEC_RELOAD_BASE)
	remove_insn (insn);

      insn = next_insn;
    }

  /* Free pool list.  */

  while (pool_list)
    {
      struct constant_pool *next = pool_list->next;
      s390_free_pool (pool_list);
      pool_list = next;
    }
}


/* Output the constant pool entry EXP in mode MODE with alignment ALIGN.  */

void
s390_output_pool_entry (rtx exp, enum machine_mode mode, unsigned int align)
{
  REAL_VALUE_TYPE r;

  switch (GET_MODE_CLASS (mode))
    {
    case MODE_FLOAT:
    case MODE_DECIMAL_FLOAT:
      gcc_assert (GET_CODE (exp) == CONST_DOUBLE);

      REAL_VALUE_FROM_CONST_DOUBLE (r, exp);
      assemble_real (r, mode, align);
      break;

    case MODE_INT:
      assemble_integer (exp, GET_MODE_SIZE (mode), align, 1);
      break;

    default:
      gcc_unreachable ();
    }
}


/* Return an RTL expression representing the value of the return address
   for the frame COUNT steps up from the current frame.  FRAME is the
   frame pointer of that frame.  */

rtx
s390_return_addr_rtx (int count, rtx frame ATTRIBUTE_UNUSED)
{
  int offset;
  rtx addr;

  /* Without backchain, we fail for all but the current frame.  */

  if (!TARGET_BACKCHAIN && count > 0)
    return NULL_RTX;

  /* For the current frame, we need to make sure the initial
     value of RETURN_REGNUM is actually saved.  */

  if (count == 0)
    {
      /* On non-z architectures branch splitting could overwrite r14.  */
      if (TARGET_CPU_ZARCH)
	return get_hard_reg_initial_val (Pmode, RETURN_REGNUM);
      else
	{
	  cfun_frame_layout.save_return_addr_p = true;
	  return gen_rtx_MEM (Pmode, return_address_pointer_rtx);
	}
    }

  if (TARGET_PACKED_STACK)
    offset = -2 * UNITS_PER_WORD;
  else
    offset = RETURN_REGNUM * UNITS_PER_WORD;

  addr = plus_constant (frame, offset);
  addr = memory_address (Pmode, addr);
  return gen_rtx_MEM (Pmode, addr);
}

/* Return an RTL expression representing the back chain stored in
   the current stack frame.  */

rtx
s390_back_chain_rtx (void)
{
  rtx chain;

  gcc_assert (TARGET_BACKCHAIN);

  if (TARGET_PACKED_STACK)
    chain = plus_constant (stack_pointer_rtx,
			   STACK_POINTER_OFFSET - UNITS_PER_WORD);
  else
    chain = stack_pointer_rtx;

  chain = gen_rtx_MEM (Pmode, chain);
  return chain;
}

/* Find first call clobbered register unused in a function.
   This could be used as base register in a leaf function
   or for holding the return address before epilogue.  */

static int
find_unused_clobbered_reg (void)
{
  int i;
  for (i = 0; i < 6; i++)
    if (!regs_ever_live[i])
      return i;
  return 0;
}


/* Helper function for s390_regs_ever_clobbered.  Sets the fields in DATA for all 
   clobbered hard regs in SETREG.  */

static void
s390_reg_clobbered_rtx (rtx setreg, rtx set_insn ATTRIBUTE_UNUSED, void *data)
{
  int *regs_ever_clobbered = (int *)data;
  unsigned int i, regno;
  enum machine_mode mode = GET_MODE (setreg);

  if (GET_CODE (setreg) == SUBREG)
    {
      rtx inner = SUBREG_REG (setreg);
      if (!GENERAL_REG_P (inner))
	return;
      regno = subreg_regno (setreg);
    }
  else if (GENERAL_REG_P (setreg))
    regno = REGNO (setreg);
  else
    return;

  for (i = regno;
       i < regno + HARD_REGNO_NREGS (regno, mode);
       i++)
    regs_ever_clobbered[i] = 1;
}

/* Walks through all basic blocks of the current function looking
   for clobbered hard regs using s390_reg_clobbered_rtx.  The fields
   of the passed integer array REGS_EVER_CLOBBERED are set to one for
   each of those regs.  */

static void
s390_regs_ever_clobbered (int *regs_ever_clobbered)
{
  basic_block cur_bb;
  rtx cur_insn;
  unsigned int i;

  memset (regs_ever_clobbered, 0, 16 * sizeof (int));

  /* For non-leaf functions we have to consider all call clobbered regs to be
     clobbered.  */
  if (!current_function_is_leaf)
    {
      for (i = 0; i < 16; i++)
	regs_ever_clobbered[i] = call_really_used_regs[i];
    }

  /* Make the "magic" eh_return registers live if necessary.  For regs_ever_live
     this work is done by liveness analysis (mark_regs_live_at_end).
     Special care is needed for functions containing landing pads.  Landing pads
     may use the eh registers, but the code which sets these registers is not
     contained in that function.  Hence s390_regs_ever_clobbered is not able to
     deal with this automatically.  */
  if (current_function_calls_eh_return || cfun->machine->has_landing_pad_p)
    for (i = 0; EH_RETURN_DATA_REGNO (i) != INVALID_REGNUM ; i++)
      if (current_function_calls_eh_return 
	  || (cfun->machine->has_landing_pad_p 
	      && regs_ever_live [EH_RETURN_DATA_REGNO (i)]))
	regs_ever_clobbered[EH_RETURN_DATA_REGNO (i)] = 1;

  /* For nonlocal gotos all call-saved registers have to be saved.
     This flag is also set for the unwinding code in libgcc.
     See expand_builtin_unwind_init.  For regs_ever_live this is done by
     reload.  */
  if (current_function_has_nonlocal_label)
    for (i = 0; i < 16; i++)
      if (!call_really_used_regs[i])
	regs_ever_clobbered[i] = 1;

  FOR_EACH_BB (cur_bb)
    {
      FOR_BB_INSNS (cur_bb, cur_insn)
	{
	  if (INSN_P (cur_insn))
	    note_stores (PATTERN (cur_insn),
			 s390_reg_clobbered_rtx, 
			 regs_ever_clobbered);
	}
    }
}

/* Determine the frame area which actually has to be accessed 
   in the function epilogue. The values are stored at the 
   given pointers AREA_BOTTOM (address of the lowest used stack
   address) and AREA_TOP (address of the first item which does 
   not belong to the stack frame).  */

static void
s390_frame_area (int *area_bottom, int *area_top)
{
  int b, t;
  int i;

  b = INT_MAX;
  t = INT_MIN;

  if (cfun_frame_layout.first_restore_gpr != -1)
    {
      b = (cfun_frame_layout.gprs_offset
	   + cfun_frame_layout.first_restore_gpr * UNITS_PER_WORD);
      t = b + (cfun_frame_layout.last_restore_gpr
	       - cfun_frame_layout.first_restore_gpr + 1) * UNITS_PER_WORD;
    }

  if (TARGET_64BIT && cfun_save_high_fprs_p)
    {
      b = MIN (b, cfun_frame_layout.f8_offset);
      t = MAX (t, (cfun_frame_layout.f8_offset
		   + cfun_frame_layout.high_fprs * 8));
    }

  if (!TARGET_64BIT)
    for (i = 2; i < 4; i++)
      if (cfun_fpr_bit_p (i))
	{
	  b = MIN (b, cfun_frame_layout.f4_offset + (i - 2) * 8);
	  t = MAX (t, cfun_frame_layout.f4_offset + (i - 1) * 8);
	}
  
  *area_bottom = b;
  *area_top = t;
}

/* Fill cfun->machine with info about register usage of current function.
   Return in CLOBBERED_REGS which GPRs are currently considered set.  */

static void
s390_register_info (int clobbered_regs[])
{
  int i, j;

  /* fprs 8 - 15 are call saved for 64 Bit ABI.  */
  cfun_frame_layout.fpr_bitmap = 0;
  cfun_frame_layout.high_fprs = 0;
  if (TARGET_64BIT)
    for (i = 24; i < 32; i++)
      if (regs_ever_live[i] && !global_regs[i])
	{
	  cfun_set_fpr_bit (i - 16);
	  cfun_frame_layout.high_fprs++;
	}

  /* Find first and last gpr to be saved.  We trust regs_ever_live
     data, except that we don't save and restore global registers.

     Also, all registers with special meaning to the compiler need
     to be handled extra.  */

  s390_regs_ever_clobbered (clobbered_regs);

  for (i = 0; i < 16; i++)
    clobbered_regs[i] = clobbered_regs[i] && !global_regs[i] && !fixed_regs[i];

  if (frame_pointer_needed)
    clobbered_regs[HARD_FRAME_POINTER_REGNUM] = 1;

  if (flag_pic)
    clobbered_regs[PIC_OFFSET_TABLE_REGNUM] 
      |= regs_ever_live[PIC_OFFSET_TABLE_REGNUM];

  clobbered_regs[BASE_REGNUM] 
    |= (cfun->machine->base_reg
        && REGNO (cfun->machine->base_reg) == BASE_REGNUM);

  clobbered_regs[RETURN_REGNUM]
    |= (!current_function_is_leaf
	|| TARGET_TPF_PROFILING
	|| cfun->machine->split_branches_pending_p
	|| cfun_frame_layout.save_return_addr_p
	|| current_function_calls_eh_return
	|| current_function_stdarg);

  clobbered_regs[STACK_POINTER_REGNUM]
    |= (!current_function_is_leaf
	|| TARGET_TPF_PROFILING
	|| cfun_save_high_fprs_p
	|| get_frame_size () > 0
	|| current_function_calls_alloca
	|| current_function_stdarg);

  for (i = 6; i < 16; i++)
    if (regs_ever_live[i] || clobbered_regs[i])
      break;
  for (j = 15; j > i; j--)
    if (regs_ever_live[j] || clobbered_regs[j])
      break;

  if (i == 16)
    {
      /* Nothing to save/restore.  */
      cfun_frame_layout.first_save_gpr_slot = -1;
      cfun_frame_layout.last_save_gpr_slot = -1;
      cfun_frame_layout.first_save_gpr = -1;
      cfun_frame_layout.first_restore_gpr = -1;
      cfun_frame_layout.last_save_gpr = -1;
      cfun_frame_layout.last_restore_gpr = -1;
    }
  else
    {
      /* Save slots for gprs from i to j.  */
      cfun_frame_layout.first_save_gpr_slot = i;
      cfun_frame_layout.last_save_gpr_slot = j;

      for (i = cfun_frame_layout.first_save_gpr_slot; 
	   i < cfun_frame_layout.last_save_gpr_slot + 1; 
	   i++)
	if (clobbered_regs[i])
	  break;

      for (j = cfun_frame_layout.last_save_gpr_slot; j > i; j--)
	if (clobbered_regs[j])
	  break;
      
      if (i == cfun_frame_layout.last_save_gpr_slot + 1)
	{
	  /* Nothing to save/restore.  */
	  cfun_frame_layout.first_save_gpr = -1;
	  cfun_frame_layout.first_restore_gpr = -1;
	  cfun_frame_layout.last_save_gpr = -1;
	  cfun_frame_layout.last_restore_gpr = -1;
	}
      else
	{
	  /* Save / Restore from gpr i to j.  */
	  cfun_frame_layout.first_save_gpr = i;
	  cfun_frame_layout.first_restore_gpr = i;
	  cfun_frame_layout.last_save_gpr = j;
	  cfun_frame_layout.last_restore_gpr = j;
	}
    }

  if (current_function_stdarg)
    {
      /* Varargs functions need to save gprs 2 to 6.  */
      if (cfun->va_list_gpr_size
	  && current_function_args_info.gprs < GP_ARG_NUM_REG)
	{
	  int min_gpr = current_function_args_info.gprs;
	  int max_gpr = min_gpr + cfun->va_list_gpr_size;
	  if (max_gpr > GP_ARG_NUM_REG)
	    max_gpr = GP_ARG_NUM_REG;

	  if (cfun_frame_layout.first_save_gpr == -1
	      || cfun_frame_layout.first_save_gpr > 2 + min_gpr)
	    {
	      cfun_frame_layout.first_save_gpr = 2 + min_gpr;
	      cfun_frame_layout.first_save_gpr_slot = 2 + min_gpr;
	    }

	  if (cfun_frame_layout.last_save_gpr == -1
	      || cfun_frame_layout.last_save_gpr < 2 + max_gpr - 1)
	    {
	      cfun_frame_layout.last_save_gpr = 2 + max_gpr - 1;
	      cfun_frame_layout.last_save_gpr_slot = 2 + max_gpr - 1;
	    }
	}

      /* Mark f0, f2 for 31 bit and f0-f4 for 64 bit to be saved.  */
      if (TARGET_HARD_FLOAT && cfun->va_list_fpr_size
	  && current_function_args_info.fprs < FP_ARG_NUM_REG)
	{
	  int min_fpr = current_function_args_info.fprs;
	  int max_fpr = min_fpr + cfun->va_list_fpr_size;
	  if (max_fpr > FP_ARG_NUM_REG)
	    max_fpr = FP_ARG_NUM_REG;

	  /* ??? This is currently required to ensure proper location
	     of the fpr save slots within the va_list save area.  */
	  if (TARGET_PACKED_STACK)
	    min_fpr = 0;

	  for (i = min_fpr; i < max_fpr; i++)
	    cfun_set_fpr_bit (i);
	}
    }

  if (!TARGET_64BIT)
    for (i = 2; i < 4; i++)
      if (regs_ever_live[i + 16] && !global_regs[i + 16])
	cfun_set_fpr_bit (i);
}

/* Fill cfun->machine with info about frame of current function.  */

static void
s390_frame_info (void)
{
  int i;

  cfun_frame_layout.frame_size = get_frame_size ();
  if (!TARGET_64BIT && cfun_frame_layout.frame_size > 0x7fff0000)
    fatal_error ("total size of local variables exceeds architecture limit");
  
  if (!TARGET_PACKED_STACK)
    {
      cfun_frame_layout.backchain_offset = 0;
      cfun_frame_layout.f0_offset = 16 * UNITS_PER_WORD;
      cfun_frame_layout.f4_offset = cfun_frame_layout.f0_offset + 2 * 8;
      cfun_frame_layout.f8_offset = -cfun_frame_layout.high_fprs * 8;
      cfun_frame_layout.gprs_offset = (cfun_frame_layout.first_save_gpr_slot
				       * UNITS_PER_WORD);
    }
  else if (TARGET_BACKCHAIN) /* kernel stack layout */
    {
      cfun_frame_layout.backchain_offset = (STACK_POINTER_OFFSET
					    - UNITS_PER_WORD);
      cfun_frame_layout.gprs_offset 
	= (cfun_frame_layout.backchain_offset 
	   - (STACK_POINTER_REGNUM - cfun_frame_layout.first_save_gpr_slot + 1)
	   * UNITS_PER_WORD);
	  
      if (TARGET_64BIT)
	{
	  cfun_frame_layout.f4_offset 
	    = (cfun_frame_layout.gprs_offset
	       - 8 * (cfun_fpr_bit_p (2) + cfun_fpr_bit_p (3)));
	  
	  cfun_frame_layout.f0_offset 
	    = (cfun_frame_layout.f4_offset 
	       - 8 * (cfun_fpr_bit_p (0) + cfun_fpr_bit_p (1)));
	}
      else
	{
	  /* On 31 bit we have to care about alignment of the
	     floating point regs to provide fastest access.  */
	  cfun_frame_layout.f0_offset 
	    = ((cfun_frame_layout.gprs_offset 
		& ~(STACK_BOUNDARY / BITS_PER_UNIT - 1))
	       - 8 * (cfun_fpr_bit_p (0) + cfun_fpr_bit_p (1)));
	  
	  cfun_frame_layout.f4_offset 
	    = (cfun_frame_layout.f0_offset
	       - 8 * (cfun_fpr_bit_p (2) + cfun_fpr_bit_p (3)));
	}
    }
  else /* no backchain */
    {
      cfun_frame_layout.f4_offset 
	= (STACK_POINTER_OFFSET
	   - 8 * (cfun_fpr_bit_p (2) + cfun_fpr_bit_p (3)));
      
      cfun_frame_layout.f0_offset 
	= (cfun_frame_layout.f4_offset
	   - 8 * (cfun_fpr_bit_p (0) + cfun_fpr_bit_p (1)));
      
      cfun_frame_layout.gprs_offset 
	= cfun_frame_layout.f0_offset - cfun_gprs_save_area_size;
    }

  if (current_function_is_leaf
      && !TARGET_TPF_PROFILING
      && cfun_frame_layout.frame_size == 0
      && !cfun_save_high_fprs_p
      && !current_function_calls_alloca
      && !current_function_stdarg)
    return;

  if (!TARGET_PACKED_STACK)
    cfun_frame_layout.frame_size += (STACK_POINTER_OFFSET
				     + current_function_outgoing_args_size
				     + cfun_frame_layout.high_fprs * 8);
  else
    {
      if (TARGET_BACKCHAIN)
	cfun_frame_layout.frame_size += UNITS_PER_WORD;

      /* No alignment trouble here because f8-f15 are only saved under 
	 64 bit.  */
      cfun_frame_layout.f8_offset = (MIN (MIN (cfun_frame_layout.f0_offset,
					       cfun_frame_layout.f4_offset),
					  cfun_frame_layout.gprs_offset)
				     - cfun_frame_layout.high_fprs * 8);

      cfun_frame_layout.frame_size += cfun_frame_layout.high_fprs * 8;

      for (i = 0; i < 8; i++)
	if (cfun_fpr_bit_p (i))
	  cfun_frame_layout.frame_size += 8;
      
      cfun_frame_layout.frame_size += cfun_gprs_save_area_size;
      
      /* If under 31 bit an odd number of gprs has to be saved we have to adjust
	 the frame size to sustain 8 byte alignment of stack frames.  */
      cfun_frame_layout.frame_size = ((cfun_frame_layout.frame_size +
				       STACK_BOUNDARY / BITS_PER_UNIT - 1)
				      & ~(STACK_BOUNDARY / BITS_PER_UNIT - 1));

      cfun_frame_layout.frame_size += current_function_outgoing_args_size;
    }
}

/* Generate frame layout.  Fills in register and frame data for the current
   function in cfun->machine.  This routine can be called multiple times;
   it will re-do the complete frame layout every time.  */

static void
s390_init_frame_layout (void)
{
  HOST_WIDE_INT frame_size;
  int base_used;
  int clobbered_regs[16];

  /* On S/390 machines, we may need to perform branch splitting, which
     will require both base and return address register.  We have no
     choice but to assume we're going to need them until right at the
     end of the machine dependent reorg phase.  */
  if (!TARGET_CPU_ZARCH)
    cfun->machine->split_branches_pending_p = true;

  do
    {
      frame_size = cfun_frame_layout.frame_size;

      /* Try to predict whether we'll need the base register.  */
      base_used = cfun->machine->split_branches_pending_p
		  || current_function_uses_const_pool
		  || (!DISP_IN_RANGE (frame_size)
		      && !CONST_OK_FOR_K (frame_size));

      /* Decide which register to use as literal pool base.  In small
	 leaf functions, try to use an unused call-clobbered register
	 as base register to avoid save/restore overhead.  */
      if (!base_used)
	cfun->machine->base_reg = NULL_RTX;
      else if (current_function_is_leaf && !regs_ever_live[5])
	cfun->machine->base_reg = gen_rtx_REG (Pmode, 5);
      else
	cfun->machine->base_reg = gen_rtx_REG (Pmode, BASE_REGNUM);

      s390_register_info (clobbered_regs);
      s390_frame_info ();
    }
  while (frame_size != cfun_frame_layout.frame_size);
}

/* Update frame layout.  Recompute actual register save data based on
   current info and update regs_ever_live for the special registers.
   May be called multiple times, but may never cause *more* registers
   to be saved than s390_init_frame_layout allocated room for.  */

static void
s390_update_frame_layout (void)
{
  int clobbered_regs[16];

  s390_register_info (clobbered_regs);

  regs_ever_live[BASE_REGNUM] = clobbered_regs[BASE_REGNUM];
  regs_ever_live[RETURN_REGNUM] = clobbered_regs[RETURN_REGNUM];
  regs_ever_live[STACK_POINTER_REGNUM] = clobbered_regs[STACK_POINTER_REGNUM];

  if (cfun->machine->base_reg)
    regs_ever_live[REGNO (cfun->machine->base_reg)] = 1;
}

/* Return true if it is legal to put a value with MODE into REGNO.  */

bool
s390_hard_regno_mode_ok (unsigned int regno, enum machine_mode mode)
{
  switch (REGNO_REG_CLASS (regno))
    {
    case FP_REGS:
      if (REGNO_PAIR_OK (regno, mode))
	{
	  if (mode == SImode || mode == DImode)
	    return true;

	  if (FLOAT_MODE_P (mode) && GET_MODE_CLASS (mode) != MODE_VECTOR_FLOAT)
	    return true;
	}
      break;
    case ADDR_REGS:
      if (FRAME_REGNO_P (regno) && mode == Pmode)
	return true;

      /* fallthrough */
    case GENERAL_REGS:
      if (REGNO_PAIR_OK (regno, mode))
	{
	  if (TARGET_64BIT 
	      || (mode != TFmode && mode != TCmode && mode != TDmode))
	    return true;
	}	  
      break;
    case CC_REGS:
      if (GET_MODE_CLASS (mode) == MODE_CC)
	return true;
      break;
    case ACCESS_REGS:
      if (REGNO_PAIR_OK (regno, mode))
	{
	  if (mode == SImode || mode == Pmode)
	    return true;
	}
      break;
    default:
      return false;
    }
  
  return false;
}

/* Return nonzero if register OLD_REG can be renamed to register NEW_REG.  */

bool
s390_hard_regno_rename_ok (unsigned int old_reg, unsigned int new_reg)
{
   /* Once we've decided upon a register to use as base register, it must
      no longer be used for any other purpose.  */
  if (cfun->machine->base_reg)
    if (REGNO (cfun->machine->base_reg) == old_reg
	|| REGNO (cfun->machine->base_reg) == new_reg)
      return false;

  return true;
}

/* Maximum number of registers to represent a value of mode MODE
   in a register of class CLASS.  */

bool
s390_class_max_nregs (enum reg_class class, enum machine_mode mode)
{
  switch (class)
    {
    case FP_REGS:
      if (GET_MODE_CLASS (mode) == MODE_COMPLEX_FLOAT)
	return 2 * ((GET_MODE_SIZE (mode) / 2 + 8 - 1) / 8);
      else
	return (GET_MODE_SIZE (mode) + 8 - 1) / 8;
    case ACCESS_REGS:
      return (GET_MODE_SIZE (mode) + 4 - 1) / 4;
    default:
      break;
    }
  return (GET_MODE_SIZE (mode) + UNITS_PER_WORD - 1) / UNITS_PER_WORD;
}

/* Return true if register FROM can be eliminated via register TO.  */

bool
s390_can_eliminate (int from, int to)
{
  /* On zSeries machines, we have not marked the base register as fixed.
     Instead, we have an elimination rule BASE_REGNUM -> BASE_REGNUM.
     If a function requires the base register, we say here that this
     elimination cannot be performed.  This will cause reload to free
     up the base register (as if it were fixed).  On the other hand,
     if the current function does *not* require the base register, we
     say here the elimination succeeds, which in turn allows reload
     to allocate the base register for any other purpose.  */
  if (from == BASE_REGNUM && to == BASE_REGNUM)
    {
      if (TARGET_CPU_ZARCH)
	{
	  s390_init_frame_layout ();
	  return cfun->machine->base_reg == NULL_RTX;
	}

      return false;
    }

  /* Everything else must point into the stack frame.  */
  gcc_assert (to == STACK_POINTER_REGNUM
	      || to == HARD_FRAME_POINTER_REGNUM);

  gcc_assert (from == FRAME_POINTER_REGNUM
	      || from == ARG_POINTER_REGNUM
	      || from == RETURN_ADDRESS_POINTER_REGNUM);

  /* Make sure we actually saved the return address.  */
  if (from == RETURN_ADDRESS_POINTER_REGNUM)
    if (!current_function_calls_eh_return
	&& !current_function_stdarg
	&& !cfun_frame_layout.save_return_addr_p)
      return false;

  return true;
}

/* Return offset between register FROM and TO initially after prolog.  */

HOST_WIDE_INT
s390_initial_elimination_offset (int from, int to)
{
  HOST_WIDE_INT offset;
  int index;

  /* ??? Why are we called for non-eliminable pairs?  */
  if (!s390_can_eliminate (from, to))
    return 0;

  switch (from)
    {
    case FRAME_POINTER_REGNUM:
      offset = (get_frame_size() 
		+ STACK_POINTER_OFFSET
		+ current_function_outgoing_args_size);
      break;

    case ARG_POINTER_REGNUM:
      s390_init_frame_layout ();
      offset = cfun_frame_layout.frame_size + STACK_POINTER_OFFSET;
      break;

    case RETURN_ADDRESS_POINTER_REGNUM:
      s390_init_frame_layout ();
      index = RETURN_REGNUM - cfun_frame_layout.first_save_gpr_slot;
      gcc_assert (index >= 0);
      offset = cfun_frame_layout.frame_size + cfun_frame_layout.gprs_offset;
      offset += index * UNITS_PER_WORD;
      break;

    case BASE_REGNUM:
      offset = 0;
      break;

    default:
      gcc_unreachable ();
    }

  return offset;
}

/* Emit insn to save fpr REGNUM at offset OFFSET relative
   to register BASE.  Return generated insn.  */

static rtx
save_fpr (rtx base, int offset, int regnum)
{
  rtx addr;
  addr = gen_rtx_MEM (DFmode, plus_constant (base, offset));

  if (regnum >= 16 && regnum <= (16 + FP_ARG_NUM_REG))
    set_mem_alias_set (addr, get_varargs_alias_set ());
  else
    set_mem_alias_set (addr, get_frame_alias_set ());

  return emit_move_insn (addr, gen_rtx_REG (DFmode, regnum));
}

/* Emit insn to restore fpr REGNUM from offset OFFSET relative
   to register BASE.  Return generated insn.  */

static rtx
restore_fpr (rtx base, int offset, int regnum)
{
  rtx addr;
  addr = gen_rtx_MEM (DFmode, plus_constant (base, offset));
  set_mem_alias_set (addr, get_frame_alias_set ());

  return emit_move_insn (gen_rtx_REG (DFmode, regnum), addr);
}

/* Generate insn to save registers FIRST to LAST into
   the register save area located at offset OFFSET
   relative to register BASE.  */

static rtx
save_gprs (rtx base, int offset, int first, int last)
{
  rtx addr, insn, note;
  int i;

  addr = plus_constant (base, offset);
  addr = gen_rtx_MEM (Pmode, addr);

  set_mem_alias_set (addr, get_frame_alias_set ());

  /* Special-case single register.  */
  if (first == last)
    {
      if (TARGET_64BIT)
        insn = gen_movdi (addr, gen_rtx_REG (Pmode, first));
      else
        insn = gen_movsi (addr, gen_rtx_REG (Pmode, first));

      RTX_FRAME_RELATED_P (insn) = 1;
      return insn;
    }


  insn = gen_store_multiple (addr,
			     gen_rtx_REG (Pmode, first),
			     GEN_INT (last - first + 1));

  if (first <= 6 && current_function_stdarg)
    for (i = 0; i < XVECLEN (PATTERN (insn), 0); i++)
      {
	rtx mem = XEXP (XVECEXP (PATTERN (insn), 0, i), 0);
	
	if (first + i <= 6)
	  set_mem_alias_set (mem, get_varargs_alias_set ());
      }

  /* We need to set the FRAME_RELATED flag on all SETs
     inside the store-multiple pattern.

     However, we must not emit DWARF records for registers 2..5
     if they are stored for use by variable arguments ...

     ??? Unfortunately, it is not enough to simply not the
     FRAME_RELATED flags for those SETs, because the first SET
     of the PARALLEL is always treated as if it had the flag
     set, even if it does not.  Therefore we emit a new pattern
     without those registers as REG_FRAME_RELATED_EXPR note.  */

  if (first >= 6)
    {
      rtx pat = PATTERN (insn);

      for (i = 0; i < XVECLEN (pat, 0); i++)
	if (GET_CODE (XVECEXP (pat, 0, i)) == SET)
	  RTX_FRAME_RELATED_P (XVECEXP (pat, 0, i)) = 1;

      RTX_FRAME_RELATED_P (insn) = 1;
    }
  else if (last >= 6)
    {
      addr = plus_constant (base, offset + (6 - first) * UNITS_PER_WORD);
      note = gen_store_multiple (gen_rtx_MEM (Pmode, addr),
				 gen_rtx_REG (Pmode, 6),
				 GEN_INT (last - 6 + 1));
      note = PATTERN (note);

      REG_NOTES (insn) =
	gen_rtx_EXPR_LIST (REG_FRAME_RELATED_EXPR,
			   note, REG_NOTES (insn));

      for (i = 0; i < XVECLEN (note, 0); i++)
	if (GET_CODE (XVECEXP (note, 0, i)) == SET)
	  RTX_FRAME_RELATED_P (XVECEXP (note, 0, i)) = 1;

      RTX_FRAME_RELATED_P (insn) = 1;
    }

  return insn;
}

/* Generate insn to restore registers FIRST to LAST from
   the register save area located at offset OFFSET
   relative to register BASE.  */

static rtx
restore_gprs (rtx base, int offset, int first, int last)
{
  rtx addr, insn;

  addr = plus_constant (base, offset);
  addr = gen_rtx_MEM (Pmode, addr);
  set_mem_alias_set (addr, get_frame_alias_set ());

  /* Special-case single register.  */
  if (first == last)
    {
      if (TARGET_64BIT)
        insn = gen_movdi (gen_rtx_REG (Pmode, first), addr);
      else
        insn = gen_movsi (gen_rtx_REG (Pmode, first), addr);

      return insn;
    }

  insn = gen_load_multiple (gen_rtx_REG (Pmode, first),
			    addr,
			    GEN_INT (last - first + 1));
  return insn;
}

/* Return insn sequence to load the GOT register.  */

static GTY(()) rtx got_symbol;
rtx
s390_load_got (void)
{
  rtx insns;

  if (!got_symbol)
    {
      got_symbol = gen_rtx_SYMBOL_REF (Pmode, "_GLOBAL_OFFSET_TABLE_");
      SYMBOL_REF_FLAGS (got_symbol) = SYMBOL_FLAG_LOCAL;
    }

  start_sequence ();

  if (TARGET_CPU_ZARCH)
    {
      emit_move_insn (pic_offset_table_rtx, got_symbol);
    }
  else
    {
      rtx offset;

      offset = gen_rtx_UNSPEC (Pmode, gen_rtvec (1, got_symbol),
			       UNSPEC_LTREL_OFFSET);
      offset = gen_rtx_CONST (Pmode, offset);
      offset = force_const_mem (Pmode, offset);

      emit_move_insn (pic_offset_table_rtx, offset);

      offset = gen_rtx_UNSPEC (Pmode, gen_rtvec (1, XEXP (offset, 0)),
			       UNSPEC_LTREL_BASE);
      offset = gen_rtx_PLUS (Pmode, pic_offset_table_rtx, offset);

      emit_move_insn (pic_offset_table_rtx, offset);
    }

  insns = get_insns ();
  end_sequence ();
  return insns;
}

/* Expand the prologue into a bunch of separate insns.  */

void
s390_emit_prologue (void)
{
  rtx insn, addr;
  rtx temp_reg;
  int i;
  int offset;
  int next_fpr = 0;

  /* Complete frame layout.  */

  s390_update_frame_layout ();

  /* Annotate all constant pool references to let the scheduler know
     they implicitly use the base register.  */

  push_topmost_sequence ();

  for (insn = get_insns (); insn; insn = NEXT_INSN (insn))
    if (INSN_P (insn))
      annotate_constant_pool_refs (&PATTERN (insn));

  pop_topmost_sequence ();

  /* Choose best register to use for temp use within prologue.
     See below for why TPF must use the register 1.  */

  if (!has_hard_reg_initial_val (Pmode, RETURN_REGNUM) 
      && !current_function_is_leaf 
      && !TARGET_TPF_PROFILING)
    temp_reg = gen_rtx_REG (Pmode, RETURN_REGNUM);
  else
    temp_reg = gen_rtx_REG (Pmode, 1);

  /* Save call saved gprs.  */
  if (cfun_frame_layout.first_save_gpr != -1)
    {
      insn = save_gprs (stack_pointer_rtx, 
			cfun_frame_layout.gprs_offset + 
			UNITS_PER_WORD * (cfun_frame_layout.first_save_gpr 
					  - cfun_frame_layout.first_save_gpr_slot),
			cfun_frame_layout.first_save_gpr, 
			cfun_frame_layout.last_save_gpr);
      emit_insn (insn);
    }

  /* Dummy insn to mark literal pool slot.  */

  if (cfun->machine->base_reg)
    emit_insn (gen_main_pool (cfun->machine->base_reg));

  offset = cfun_frame_layout.f0_offset;

  /* Save f0 and f2.  */
  for (i = 0; i < 2; i++)
    {
      if (cfun_fpr_bit_p (i))
	{
	  save_fpr (stack_pointer_rtx, offset, i + 16);
	  offset += 8;
	}
      else if (!TARGET_PACKED_STACK)
	  offset += 8;
    }

  /* Save f4 and f6.  */
  offset = cfun_frame_layout.f4_offset;
  for (i = 2; i < 4; i++)
    {
      if (cfun_fpr_bit_p (i))
	{
	  insn = save_fpr (stack_pointer_rtx, offset, i + 16);
	  offset += 8;

	  /* If f4 and f6 are call clobbered they are saved due to stdargs and
	     therefore are not frame related.  */
	  if (!call_really_used_regs[i + 16])
	    RTX_FRAME_RELATED_P (insn) = 1;
	}
      else if (!TARGET_PACKED_STACK)
	offset += 8;
    }

  if (TARGET_PACKED_STACK
      && cfun_save_high_fprs_p
      && cfun_frame_layout.f8_offset + cfun_frame_layout.high_fprs * 8 > 0)
    {
      offset = (cfun_frame_layout.f8_offset
		+ (cfun_frame_layout.high_fprs - 1) * 8);

      for (i = 15; i > 7 && offset >= 0; i--)
	if (cfun_fpr_bit_p (i))
	  {
	    insn = save_fpr (stack_pointer_rtx, offset, i + 16);
	    	       
	    RTX_FRAME_RELATED_P (insn) = 1;
	    offset -= 8;
	  }
      if (offset >= cfun_frame_layout.f8_offset)
	next_fpr = i + 16;
    }
  
  if (!TARGET_PACKED_STACK)
    next_fpr = cfun_save_high_fprs_p ? 31 : 0;

  /* Decrement stack pointer.  */

  if (cfun_frame_layout.frame_size > 0)
    {
      rtx frame_off = GEN_INT (-cfun_frame_layout.frame_size);

      if (s390_stack_size)
  	{
	  HOST_WIDE_INT stack_check_mask = ((s390_stack_size - 1)
					    & ~(s390_stack_guard - 1));
	  rtx t = gen_rtx_AND (Pmode, stack_pointer_rtx,
			       GEN_INT (stack_check_mask));

	  if (TARGET_64BIT)
	    gen_cmpdi (t, const0_rtx);
	  else
	    gen_cmpsi (t, const0_rtx);

	  emit_insn (gen_conditional_trap (gen_rtx_EQ (CCmode, 
						       gen_rtx_REG (CCmode, 
								    CC_REGNUM),
						       const0_rtx),
					   const0_rtx));
  	}

      if (s390_warn_framesize > 0 
	  && cfun_frame_layout.frame_size >= s390_warn_framesize)
	warning (0, "frame size of %qs is " HOST_WIDE_INT_PRINT_DEC " bytes", 
		 current_function_name (), cfun_frame_layout.frame_size);

      if (s390_warn_dynamicstack_p && cfun->calls_alloca)
	warning (0, "%qs uses dynamic stack allocation", current_function_name ());

      /* Save incoming stack pointer into temp reg.  */
      if (TARGET_BACKCHAIN || next_fpr)
	insn = emit_insn (gen_move_insn (temp_reg, stack_pointer_rtx));

      /* Subtract frame size from stack pointer.  */

      if (DISP_IN_RANGE (INTVAL (frame_off)))
	{
	  insn = gen_rtx_SET (VOIDmode, stack_pointer_rtx,
			      gen_rtx_PLUS (Pmode, stack_pointer_rtx, 
					    frame_off));
	  insn = emit_insn (insn);
	}
      else
	{
	  if (!CONST_OK_FOR_K (INTVAL (frame_off)))
	    frame_off = force_const_mem (Pmode, frame_off);

          insn = emit_insn (gen_add2_insn (stack_pointer_rtx, frame_off));
	  annotate_constant_pool_refs (&PATTERN (insn));
	}

      RTX_FRAME_RELATED_P (insn) = 1;
      REG_NOTES (insn) =
	gen_rtx_EXPR_LIST (REG_FRAME_RELATED_EXPR,
			   gen_rtx_SET (VOIDmode, stack_pointer_rtx,
			     gen_rtx_PLUS (Pmode, stack_pointer_rtx,
			       GEN_INT (-cfun_frame_layout.frame_size))),
			   REG_NOTES (insn));

      /* Set backchain.  */

      if (TARGET_BACKCHAIN)
	{
	  if (cfun_frame_layout.backchain_offset)
	    addr = gen_rtx_MEM (Pmode, 
				plus_constant (stack_pointer_rtx, 
				  cfun_frame_layout.backchain_offset));
	  else
	    addr = gen_rtx_MEM (Pmode, stack_pointer_rtx);  
	  set_mem_alias_set (addr, get_frame_alias_set ());
	  insn = emit_insn (gen_move_insn (addr, temp_reg));
	}

      /* If we support asynchronous exceptions (e.g. for Java),
	 we need to make sure the backchain pointer is set up
	 before any possibly trapping memory access.  */

      if (TARGET_BACKCHAIN && flag_non_call_exceptions)
	{
	  addr = gen_rtx_MEM (BLKmode, gen_rtx_SCRATCH (VOIDmode));
	  emit_insn (gen_rtx_CLOBBER (VOIDmode, addr));
	}
    }

  /* Save fprs 8 - 15 (64 bit ABI).  */

  if (cfun_save_high_fprs_p && next_fpr)
    {
      insn = emit_insn (gen_add2_insn (temp_reg, 
				       GEN_INT (cfun_frame_layout.f8_offset)));

      offset = 0;

      for (i = 24; i <= next_fpr; i++)
	if (cfun_fpr_bit_p (i - 16))
	  {
	    rtx addr = plus_constant (stack_pointer_rtx,
				      cfun_frame_layout.frame_size
				      + cfun_frame_layout.f8_offset
				      + offset);
	    
	    insn = save_fpr (temp_reg, offset, i);
	    offset += 8;
	    RTX_FRAME_RELATED_P (insn) = 1;
	    REG_NOTES (insn) =
	      gen_rtx_EXPR_LIST (REG_FRAME_RELATED_EXPR,
				 gen_rtx_SET (VOIDmode,
					      gen_rtx_MEM (DFmode, addr),
					      gen_rtx_REG (DFmode, i)),
				 REG_NOTES (insn));
	  }
    }

  /* Set frame pointer, if needed.  */

  if (frame_pointer_needed)
    {
      insn = emit_move_insn (hard_frame_pointer_rtx, stack_pointer_rtx);
      RTX_FRAME_RELATED_P (insn) = 1;
    }

  /* Set up got pointer, if needed.  */

  if (flag_pic && regs_ever_live[PIC_OFFSET_TABLE_REGNUM])
    {
      rtx insns = s390_load_got ();

      for (insn = insns; insn; insn = NEXT_INSN (insn))
	{
	  annotate_constant_pool_refs (&PATTERN (insn));

	  REG_NOTES (insn) = gen_rtx_EXPR_LIST (REG_MAYBE_DEAD, NULL_RTX,
						REG_NOTES (insn));
	}

      emit_insn (insns);
    }

  if (TARGET_TPF_PROFILING)
    {
      /* Generate a BAS instruction to serve as a function
	 entry intercept to facilitate the use of tracing
	 algorithms located at the branch target.  */
      emit_insn (gen_prologue_tpf ());

      /* Emit a blockage here so that all code
	 lies between the profiling mechanisms.  */
      emit_insn (gen_blockage ());
    }
}

/* Expand the epilogue into a bunch of separate insns.  */

void
s390_emit_epilogue (bool sibcall)
{
  rtx frame_pointer, return_reg;
  int area_bottom, area_top, offset = 0;
  int next_offset;
  rtvec p;
  int i;

  if (TARGET_TPF_PROFILING)
    {

      /* Generate a BAS instruction to serve as a function
	 entry intercept to facilitate the use of tracing
	 algorithms located at the branch target.  */

      /* Emit a blockage here so that all code
         lies between the profiling mechanisms.  */
      emit_insn (gen_blockage ());

      emit_insn (gen_epilogue_tpf ());
    }

  /* Check whether to use frame or stack pointer for restore.  */

  frame_pointer = (frame_pointer_needed 
		   ? hard_frame_pointer_rtx : stack_pointer_rtx);

  s390_frame_area (&area_bottom, &area_top);

  /* Check whether we can access the register save area.
     If not, increment the frame pointer as required.  */

  if (area_top <= area_bottom)
    {
      /* Nothing to restore.  */
    }
  else if (DISP_IN_RANGE (cfun_frame_layout.frame_size + area_bottom)
           && DISP_IN_RANGE (cfun_frame_layout.frame_size + area_top - 1))
    {
      /* Area is in range.  */
      offset = cfun_frame_layout.frame_size;
    }
  else
    {
      rtx insn, frame_off;

      offset = area_bottom < 0 ? -area_bottom : 0;
      frame_off = GEN_INT (cfun_frame_layout.frame_size - offset);

      if (DISP_IN_RANGE (INTVAL (frame_off)))
	{
	  insn = gen_rtx_SET (VOIDmode, frame_pointer,
			      gen_rtx_PLUS (Pmode, frame_pointer, frame_off));
	  insn = emit_insn (insn);
	}
      else
	{
	  if (!CONST_OK_FOR_K (INTVAL (frame_off)))
	    frame_off = force_const_mem (Pmode, frame_off);

	  insn = emit_insn (gen_add2_insn (frame_pointer, frame_off));
	  annotate_constant_pool_refs (&PATTERN (insn));
	}
    }

  /* Restore call saved fprs.  */

  if (TARGET_64BIT)
    {
      if (cfun_save_high_fprs_p)
	{
	  next_offset = cfun_frame_layout.f8_offset;
	  for (i = 24; i < 32; i++)
	    {
	      if (cfun_fpr_bit_p (i - 16))
		{
		  restore_fpr (frame_pointer,
			       offset + next_offset, i);
		  next_offset += 8;
		}
	    }
	}
	      
    }
  else
    {
      next_offset = cfun_frame_layout.f4_offset;
      for (i = 18; i < 20; i++)
	{
	  if (cfun_fpr_bit_p (i - 16))
	    {
	      restore_fpr (frame_pointer,
			   offset + next_offset, i);
	      next_offset += 8;
	    }
	  else if (!TARGET_PACKED_STACK)
	    next_offset += 8;
	}
      
    }

  /* Return register.  */

  return_reg = gen_rtx_REG (Pmode, RETURN_REGNUM);

  /* Restore call saved gprs.  */

  if (cfun_frame_layout.first_restore_gpr != -1)
    {
      rtx insn, addr;
      int i;

      /* Check for global register and save them
	 to stack location from where they get restored.  */

      for (i = cfun_frame_layout.first_restore_gpr;
	   i <= cfun_frame_layout.last_restore_gpr;
	   i++)
	{
	  /* These registers are special and need to be
	     restored in any case.  */
	  if (i == STACK_POINTER_REGNUM
              || i == RETURN_REGNUM
              || i == BASE_REGNUM
              || (flag_pic && i == (int)PIC_OFFSET_TABLE_REGNUM))
	    continue;

	  if (global_regs[i])
	    {
	      addr = plus_constant (frame_pointer,
				    offset + cfun_frame_layout.gprs_offset 
				    + (i - cfun_frame_layout.first_save_gpr_slot)
				    * UNITS_PER_WORD);
	      addr = gen_rtx_MEM (Pmode, addr);
	      set_mem_alias_set (addr, get_frame_alias_set ());
	      emit_move_insn (addr, gen_rtx_REG (Pmode, i));
	    }
	}

      if (! sibcall)
	{
	  /* Fetch return address from stack before load multiple,
	     this will do good for scheduling.  */

	  if (cfun_frame_layout.save_return_addr_p
	      || (cfun_frame_layout.first_restore_gpr < BASE_REGNUM
		  && cfun_frame_layout.last_restore_gpr > RETURN_REGNUM))
	    {
	      int return_regnum = find_unused_clobbered_reg();
	      if (!return_regnum)
		return_regnum = 4;
	      return_reg = gen_rtx_REG (Pmode, return_regnum);

	      addr = plus_constant (frame_pointer,
				    offset + cfun_frame_layout.gprs_offset
				    + (RETURN_REGNUM 
				       - cfun_frame_layout.first_save_gpr_slot)
				    * UNITS_PER_WORD);
	      addr = gen_rtx_MEM (Pmode, addr);
	      set_mem_alias_set (addr, get_frame_alias_set ());
	      emit_move_insn (return_reg, addr);
	    }
	}

      insn = restore_gprs (frame_pointer,
			   offset + cfun_frame_layout.gprs_offset
			   + (cfun_frame_layout.first_restore_gpr 
			      - cfun_frame_layout.first_save_gpr_slot)
			   * UNITS_PER_WORD,
			   cfun_frame_layout.first_restore_gpr,
			   cfun_frame_layout.last_restore_gpr);
      emit_insn (insn);
    }

  if (! sibcall)
    {

      /* Return to caller.  */

      p = rtvec_alloc (2);

      RTVEC_ELT (p, 0) = gen_rtx_RETURN (VOIDmode);
      RTVEC_ELT (p, 1) = gen_rtx_USE (VOIDmode, return_reg);
      emit_jump_insn (gen_rtx_PARALLEL (VOIDmode, p));
    }
}


/* Return the size in bytes of a function argument of
   type TYPE and/or mode MODE.  At least one of TYPE or
   MODE must be specified.  */

static int
s390_function_arg_size (enum machine_mode mode, tree type)
{
  if (type)
    return int_size_in_bytes (type);

  /* No type info available for some library calls ...  */
  if (mode != BLKmode)
    return GET_MODE_SIZE (mode);

  /* If we have neither type nor mode, abort */
  gcc_unreachable ();
}

/* Return true if a function argument of type TYPE and mode MODE
   is to be passed in a floating-point register, if available.  */

static bool
s390_function_arg_float (enum machine_mode mode, tree type)
{
  int size = s390_function_arg_size (mode, type);
  if (size > 8)
    return false;

  /* Soft-float changes the ABI: no floating-point registers are used.  */
  if (TARGET_SOFT_FLOAT)
    return false;

  /* No type info available for some library calls ...  */
  if (!type)
    return mode == SFmode || mode == DFmode || mode == SDmode || mode == DDmode;

  /* The ABI says that record types with a single member are treated
     just like that member would be.  */
  while (TREE_CODE (type) == RECORD_TYPE)
    {
      tree field, single = NULL_TREE;

      for (field = TYPE_FIELDS (type); field; field = TREE_CHAIN (field))
	{
	  if (TREE_CODE (field) != FIELD_DECL)
	    continue;

	  if (single == NULL_TREE)
	    single = TREE_TYPE (field);
	  else
	    return false;
	}

      if (single == NULL_TREE)
	return false;
      else
	type = single;
    }

  return TREE_CODE (type) == REAL_TYPE;
}

/* Return true if a function argument of type TYPE and mode MODE
   is to be passed in an integer register, or a pair of integer
   registers, if available.  */

static bool
s390_function_arg_integer (enum machine_mode mode, tree type)
{
  int size = s390_function_arg_size (mode, type);
  if (size > 8)
    return false;

  /* No type info available for some library calls ...  */
  if (!type)
    return GET_MODE_CLASS (mode) == MODE_INT
	   || (TARGET_SOFT_FLOAT &&  SCALAR_FLOAT_MODE_P (mode));

  /* We accept small integral (and similar) types.  */
  if (INTEGRAL_TYPE_P (type)
      || POINTER_TYPE_P (type)
      || TREE_CODE (type) == OFFSET_TYPE
      || (TARGET_SOFT_FLOAT && TREE_CODE (type) == REAL_TYPE))
    return true;

  /* We also accept structs of size 1, 2, 4, 8 that are not
     passed in floating-point registers.  */
  if (AGGREGATE_TYPE_P (type)
      && exact_log2 (size) >= 0
      && !s390_function_arg_float (mode, type))
    return true;

  return false;
}

/* Return 1 if a function argument of type TYPE and mode MODE
   is to be passed by reference.  The ABI specifies that only
   structures of size 1, 2, 4, or 8 bytes are passed by value,
   all other structures (and complex numbers) are passed by
   reference.  */

static bool
s390_pass_by_reference (CUMULATIVE_ARGS *ca ATTRIBUTE_UNUSED,
			enum machine_mode mode, tree type,
			bool named ATTRIBUTE_UNUSED)
{
  int size = s390_function_arg_size (mode, type);
  if (size > 8)
    return true;

  if (type)
    {
      if (AGGREGATE_TYPE_P (type) && exact_log2 (size) < 0)
        return 1;

      if (TREE_CODE (type) == COMPLEX_TYPE
	  || TREE_CODE (type) == VECTOR_TYPE)
        return 1;
    }

  return 0;
}

/* Update the data in CUM to advance over an argument of mode MODE and
   data type TYPE.  (TYPE is null for libcalls where that information
   may not be available.).  The boolean NAMED specifies whether the
   argument is a named argument (as opposed to an unnamed argument
   matching an ellipsis).  */

void
s390_function_arg_advance (CUMULATIVE_ARGS *cum, enum machine_mode mode,
			   tree type, int named ATTRIBUTE_UNUSED)
{
  if (s390_function_arg_float (mode, type))
    {
      cum->fprs += 1;
    }
  else if (s390_function_arg_integer (mode, type))
    {
      int size = s390_function_arg_size (mode, type);
      cum->gprs += ((size + UNITS_PER_WORD-1) / UNITS_PER_WORD);
    }
  else
    gcc_unreachable ();
}

/* Define where to put the arguments to a function.
   Value is zero to push the argument on the stack,
   or a hard register in which to store the argument.

   MODE is the argument's machine mode.
   TYPE is the data type of the argument (as a tree).
    This is null for libcalls where that information may
    not be available.
   CUM is a variable of type CUMULATIVE_ARGS which gives info about
    the preceding args and about the function being called.
   NAMED is nonzero if this argument is a named parameter
    (otherwise it is an extra parameter matching an ellipsis).

   On S/390, we use general purpose registers 2 through 6 to
   pass integer, pointer, and certain structure arguments, and
   floating point registers 0 and 2 (0, 2, 4, and 6 on 64-bit)
   to pass floating point arguments.  All remaining arguments
   are pushed to the stack.  */

rtx
s390_function_arg (CUMULATIVE_ARGS *cum, enum machine_mode mode, tree type,
		   int named ATTRIBUTE_UNUSED)
{
  if (s390_function_arg_float (mode, type))
    {
      if (cum->fprs + 1 > FP_ARG_NUM_REG)
	return 0;
      else
	return gen_rtx_REG (mode, cum->fprs + 16);
    }
  else if (s390_function_arg_integer (mode, type))
    {
      int size = s390_function_arg_size (mode, type);
      int n_gprs = (size + UNITS_PER_WORD-1) / UNITS_PER_WORD;

      if (cum->gprs + n_gprs > GP_ARG_NUM_REG)
	return 0;
      else
	return gen_rtx_REG (mode, cum->gprs + 2);
    }

  /* After the real arguments, expand_call calls us once again
     with a void_type_node type.  Whatever we return here is
     passed as operand 2 to the call expanders.

     We don't need this feature ...  */
  else if (type == void_type_node)
    return const0_rtx;

  gcc_unreachable ();
}

/* Return true if return values of type TYPE should be returned
   in a memory buffer whose address is passed by the caller as
   hidden first argument.  */

static bool
s390_return_in_memory (tree type, tree fundecl ATTRIBUTE_UNUSED)
{
  /* We accept small integral (and similar) types.  */
  if (INTEGRAL_TYPE_P (type)
      || POINTER_TYPE_P (type)
      || TREE_CODE (type) == OFFSET_TYPE
      || TREE_CODE (type) == REAL_TYPE)
    return int_size_in_bytes (type) > 8;

  /* Aggregates and similar constructs are always returned
     in memory.  */
  if (AGGREGATE_TYPE_P (type)
      || TREE_CODE (type) == COMPLEX_TYPE
      || TREE_CODE (type) == VECTOR_TYPE)
    return true;

  /* ??? We get called on all sorts of random stuff from
     aggregate_value_p.  We can't abort, but it's not clear
     what's safe to return.  Pretend it's a struct I guess.  */
  return true;
}

/* Define where to return a (scalar) value of type TYPE.
   If TYPE is null, define where to return a (scalar)
   value of mode MODE from a libcall.  */

rtx
s390_function_value (tree type, enum machine_mode mode)
{
  if (type)
    {
      int unsignedp = TYPE_UNSIGNED (type);
      mode = promote_mode (type, TYPE_MODE (type), &unsignedp, 1);
    }

  gcc_assert (GET_MODE_CLASS (mode) == MODE_INT || SCALAR_FLOAT_MODE_P (mode));
  gcc_assert (GET_MODE_SIZE (mode) <= 8);

  if (TARGET_HARD_FLOAT && SCALAR_FLOAT_MODE_P (mode))
    return gen_rtx_REG (mode, 16);
  else
    return gen_rtx_REG (mode, 2);
}


/* Create and return the va_list datatype.

   On S/390, va_list is an array type equivalent to

      typedef struct __va_list_tag
        {
            long __gpr;
            long __fpr;
            void *__overflow_arg_area;
            void *__reg_save_area;
        } va_list[1];

   where __gpr and __fpr hold the number of general purpose
   or floating point arguments used up to now, respectively,
   __overflow_arg_area points to the stack location of the
   next argument passed on the stack, and __reg_save_area
   always points to the start of the register area in the
   call frame of the current function.  The function prologue
   saves all registers used for argument passing into this
   area if the function uses variable arguments.  */

static tree
s390_build_builtin_va_list (void)
{
  tree f_gpr, f_fpr, f_ovf, f_sav, record, type_decl;

  record = lang_hooks.types.make_type (RECORD_TYPE);

  type_decl =
    build_decl (TYPE_DECL, get_identifier ("__va_list_tag"), record);

  f_gpr = build_decl (FIELD_DECL, get_identifier ("__gpr"),
		      long_integer_type_node);
  f_fpr = build_decl (FIELD_DECL, get_identifier ("__fpr"),
		      long_integer_type_node);
  f_ovf = build_decl (FIELD_DECL, get_identifier ("__overflow_arg_area"),
		      ptr_type_node);
  f_sav = build_decl (FIELD_DECL, get_identifier ("__reg_save_area"),
		      ptr_type_node);

  va_list_gpr_counter_field = f_gpr;
  va_list_fpr_counter_field = f_fpr;

  DECL_FIELD_CONTEXT (f_gpr) = record;
  DECL_FIELD_CONTEXT (f_fpr) = record;
  DECL_FIELD_CONTEXT (f_ovf) = record;
  DECL_FIELD_CONTEXT (f_sav) = record;

  TREE_CHAIN (record) = type_decl;
  TYPE_NAME (record) = type_decl;
  TYPE_FIELDS (record) = f_gpr;
  TREE_CHAIN (f_gpr) = f_fpr;
  TREE_CHAIN (f_fpr) = f_ovf;
  TREE_CHAIN (f_ovf) = f_sav;

  layout_type (record);

  /* The correct type is an array type of one element.  */
  return build_array_type (record, build_index_type (size_zero_node));
}

/* Implement va_start by filling the va_list structure VALIST.
   STDARG_P is always true, and ignored.
   NEXTARG points to the first anonymous stack argument.

   The following global variables are used to initialize
   the va_list structure:

     current_function_args_info:
       holds number of gprs and fprs used for named arguments.
     current_function_arg_offset_rtx:
       holds the offset of the first anonymous stack argument
       (relative to the virtual arg pointer).  */

void
s390_va_start (tree valist, rtx nextarg ATTRIBUTE_UNUSED)
{
  HOST_WIDE_INT n_gpr, n_fpr;
  int off;
  tree f_gpr, f_fpr, f_ovf, f_sav;
  tree gpr, fpr, ovf, sav, t;

  f_gpr = TYPE_FIELDS (TREE_TYPE (va_list_type_node));
  f_fpr = TREE_CHAIN (f_gpr);
  f_ovf = TREE_CHAIN (f_fpr);
  f_sav = TREE_CHAIN (f_ovf);

  valist = build_va_arg_indirect_ref (valist);
  gpr = build3 (COMPONENT_REF, TREE_TYPE (f_gpr), valist, f_gpr, NULL_TREE);
  fpr = build3 (COMPONENT_REF, TREE_TYPE (f_fpr), valist, f_fpr, NULL_TREE);
  ovf = build3 (COMPONENT_REF, TREE_TYPE (f_ovf), valist, f_ovf, NULL_TREE);
  sav = build3 (COMPONENT_REF, TREE_TYPE (f_sav), valist, f_sav, NULL_TREE);

  /* Count number of gp and fp argument registers used.  */

  n_gpr = current_function_args_info.gprs;
  n_fpr = current_function_args_info.fprs;

  if (cfun->va_list_gpr_size)
    {
      t = build2 (MODIFY_EXPR, TREE_TYPE (gpr), gpr,
	          build_int_cst (NULL_TREE, n_gpr));
      TREE_SIDE_EFFECTS (t) = 1;
      expand_expr (t, const0_rtx, VOIDmode, EXPAND_NORMAL);
    }

  if (cfun->va_list_fpr_size)
    {
      t = build2 (MODIFY_EXPR, TREE_TYPE (fpr), fpr,
	          build_int_cst (NULL_TREE, n_fpr));
      TREE_SIDE_EFFECTS (t) = 1;
      expand_expr (t, const0_rtx, VOIDmode, EXPAND_NORMAL);
    }

  /* Find the overflow area.  */
  if (n_gpr + cfun->va_list_gpr_size > GP_ARG_NUM_REG
      || n_fpr + cfun->va_list_fpr_size > FP_ARG_NUM_REG)
    {
      t = make_tree (TREE_TYPE (ovf), virtual_incoming_args_rtx);

      off = INTVAL (current_function_arg_offset_rtx);
      off = off < 0 ? 0 : off;
      if (TARGET_DEBUG_ARG)
	fprintf (stderr, "va_start: n_gpr = %d, n_fpr = %d off %d\n",
		 (int)n_gpr, (int)n_fpr, off);

      t = build2 (PLUS_EXPR, TREE_TYPE (ovf), t, build_int_cst (NULL_TREE, off));

      t = build2 (MODIFY_EXPR, TREE_TYPE (ovf), ovf, t);
      TREE_SIDE_EFFECTS (t) = 1;
      expand_expr (t, const0_rtx, VOIDmode, EXPAND_NORMAL);
    }

  /* Find the register save area.  */
  if ((cfun->va_list_gpr_size && n_gpr < GP_ARG_NUM_REG)
      || (cfun->va_list_fpr_size && n_fpr < FP_ARG_NUM_REG))
    {
      t = make_tree (TREE_TYPE (sav), return_address_pointer_rtx);
      t = build2 (PLUS_EXPR, TREE_TYPE (sav), t,
	          build_int_cst (NULL_TREE, -RETURN_REGNUM * UNITS_PER_WORD));
  
      t = build2 (MODIFY_EXPR, TREE_TYPE (sav), sav, t);
      TREE_SIDE_EFFECTS (t) = 1;
      expand_expr (t, const0_rtx, VOIDmode, EXPAND_NORMAL);
    }
}

/* Implement va_arg by updating the va_list structure
   VALIST as required to retrieve an argument of type
   TYPE, and returning that argument.

   Generates code equivalent to:

   if (integral value) {
     if (size  <= 4 && args.gpr < 5 ||
         size  > 4 && args.gpr < 4 )
       ret = args.reg_save_area[args.gpr+8]
     else
       ret = *args.overflow_arg_area++;
   } else if (float value) {
     if (args.fgpr < 2)
       ret = args.reg_save_area[args.fpr+64]
     else
       ret = *args.overflow_arg_area++;
   } else if (aggregate value) {
     if (args.gpr < 5)
       ret = *args.reg_save_area[args.gpr]
     else
       ret = **args.overflow_arg_area++;
   } */

static tree
s390_gimplify_va_arg (tree valist, tree type, tree *pre_p, 
		      tree *post_p ATTRIBUTE_UNUSED)
{
  tree f_gpr, f_fpr, f_ovf, f_sav;
  tree gpr, fpr, ovf, sav, reg, t, u;
  int indirect_p, size, n_reg, sav_ofs, sav_scale, max_reg;
  tree lab_false, lab_over, addr;

  f_gpr = TYPE_FIELDS (TREE_TYPE (va_list_type_node));
  f_fpr = TREE_CHAIN (f_gpr);
  f_ovf = TREE_CHAIN (f_fpr);
  f_sav = TREE_CHAIN (f_ovf);

  valist = build_va_arg_indirect_ref (valist);
  gpr = build3 (COMPONENT_REF, TREE_TYPE (f_gpr), valist, f_gpr, NULL_TREE);
  fpr = build3 (COMPONENT_REF, TREE_TYPE (f_fpr), valist, f_fpr, NULL_TREE);
  ovf = build3 (COMPONENT_REF, TREE_TYPE (f_ovf), valist, f_ovf, NULL_TREE);
  sav = build3 (COMPONENT_REF, TREE_TYPE (f_sav), valist, f_sav, NULL_TREE);

  size = int_size_in_bytes (type);

  if (pass_by_reference (NULL, TYPE_MODE (type), type, false))
    {
      if (TARGET_DEBUG_ARG)
	{
	  fprintf (stderr, "va_arg: aggregate type");
	  debug_tree (type);
	}

      /* Aggregates are passed by reference.  */
      indirect_p = 1;
      reg = gpr;
      n_reg = 1;

      /* kernel stack layout on 31 bit: It is assumed here that no padding
	 will be added by s390_frame_info because for va_args always an even
	 number of gprs has to be saved r15-r2 = 14 regs.  */
      sav_ofs = 2 * UNITS_PER_WORD;
      sav_scale = UNITS_PER_WORD;
      size = UNITS_PER_WORD;
      max_reg = GP_ARG_NUM_REG - n_reg;
    }
  else if (s390_function_arg_float (TYPE_MODE (type), type))
    {
      if (TARGET_DEBUG_ARG)
	{
	  fprintf (stderr, "va_arg: float type");
	  debug_tree (type);
	}

      /* FP args go in FP registers, if present.  */
      indirect_p = 0;
      reg = fpr;
      n_reg = 1;
      sav_ofs = 16 * UNITS_PER_WORD;
      sav_scale = 8;
      max_reg = FP_ARG_NUM_REG - n_reg;
    }
  else
    {
      if (TARGET_DEBUG_ARG)
	{
	  fprintf (stderr, "va_arg: other type");
	  debug_tree (type);
	}

      /* Otherwise into GP registers.  */
      indirect_p = 0;
      reg = gpr;
      n_reg = (size + UNITS_PER_WORD - 1) / UNITS_PER_WORD;

      /* kernel stack layout on 31 bit: It is assumed here that no padding
	 will be added by s390_frame_info because for va_args always an even
	 number of gprs has to be saved r15-r2 = 14 regs.  */
      sav_ofs = 2 * UNITS_PER_WORD;

      if (size < UNITS_PER_WORD)
	sav_ofs += UNITS_PER_WORD - size;

      sav_scale = UNITS_PER_WORD;
      max_reg = GP_ARG_NUM_REG - n_reg;
    }

  /* Pull the value out of the saved registers ...  */

  lab_false = create_artificial_label ();
  lab_over = create_artificial_label ();
  addr = create_tmp_var (ptr_type_node, "addr");
  DECL_POINTER_ALIAS_SET (addr) = get_varargs_alias_set ();

  t = fold_convert (TREE_TYPE (reg), size_int (max_reg));
  t = build2 (GT_EXPR, boolean_type_node, reg, t);
  u = build1 (GOTO_EXPR, void_type_node, lab_false);
  t = build3 (COND_EXPR, void_type_node, t, u, NULL_TREE);
  gimplify_and_add (t, pre_p);

  t = build2 (PLUS_EXPR, ptr_type_node, sav, 
	      fold_convert (ptr_type_node, size_int (sav_ofs)));
  u = build2 (MULT_EXPR, TREE_TYPE (reg), reg, 
	      fold_convert (TREE_TYPE (reg), size_int (sav_scale)));
  t = build2 (PLUS_EXPR, ptr_type_node, t, fold_convert (ptr_type_node, u));

  t = build2 (MODIFY_EXPR, void_type_node, addr, t);
  gimplify_and_add (t, pre_p);

  t = build1 (GOTO_EXPR, void_type_node, lab_over);
  gimplify_and_add (t, pre_p);

  t = build1 (LABEL_EXPR, void_type_node, lab_false);
  append_to_statement_list (t, pre_p);


  /* ... Otherwise out of the overflow area.  */

  t = ovf;
  if (size < UNITS_PER_WORD)
    t = build2 (PLUS_EXPR, ptr_type_node, t, 
		fold_convert (ptr_type_node, size_int (UNITS_PER_WORD - size)));

  gimplify_expr (&t, pre_p, NULL, is_gimple_val, fb_rvalue);

  u = build2 (MODIFY_EXPR, void_type_node, addr, t);
  gimplify_and_add (u, pre_p);

  t = build2 (PLUS_EXPR, ptr_type_node, t, 
	      fold_convert (ptr_type_node, size_int (size)));
  t = build2 (MODIFY_EXPR, ptr_type_node, ovf, t);
  gimplify_and_add (t, pre_p);

  t = build1 (LABEL_EXPR, void_type_node, lab_over);
  append_to_statement_list (t, pre_p);


  /* Increment register save count.  */

  u = build2 (PREINCREMENT_EXPR, TREE_TYPE (reg), reg,
	      fold_convert (TREE_TYPE (reg), size_int (n_reg)));
  gimplify_and_add (u, pre_p);

  if (indirect_p)
    {
      t = build_pointer_type (build_pointer_type (type));
      addr = fold_convert (t, addr);
      addr = build_va_arg_indirect_ref (addr);
    }
  else
    {
      t = build_pointer_type (type);
      addr = fold_convert (t, addr);
    }

  return build_va_arg_indirect_ref (addr);
}


/* Builtins.  */

enum s390_builtin
{
  S390_BUILTIN_THREAD_POINTER,
  S390_BUILTIN_SET_THREAD_POINTER,

  S390_BUILTIN_max
};

static unsigned int const code_for_builtin_64[S390_BUILTIN_max] = {
  CODE_FOR_get_tp_64,
  CODE_FOR_set_tp_64
};

static unsigned int const code_for_builtin_31[S390_BUILTIN_max] = {
  CODE_FOR_get_tp_31,
  CODE_FOR_set_tp_31
};

static void
s390_init_builtins (void)
{
  tree ftype;

  ftype = build_function_type (ptr_type_node, void_list_node);
  lang_hooks.builtin_function ("__builtin_thread_pointer", ftype,
			       S390_BUILTIN_THREAD_POINTER, BUILT_IN_MD,
			       NULL, NULL_TREE);

  ftype = build_function_type_list (void_type_node, ptr_type_node, NULL_TREE);
  lang_hooks.builtin_function ("__builtin_set_thread_pointer", ftype,
			       S390_BUILTIN_SET_THREAD_POINTER, BUILT_IN_MD,
			       NULL, NULL_TREE);
}

/* Expand an expression EXP that calls a built-in function,
   with result going to TARGET if that's convenient
   (and in mode MODE if that's convenient).
   SUBTARGET may be used as the target for computing one of EXP's operands.
   IGNORE is nonzero if the value is to be ignored.  */

static rtx
s390_expand_builtin (tree exp, rtx target, rtx subtarget ATTRIBUTE_UNUSED,
		     enum machine_mode mode ATTRIBUTE_UNUSED,
		     int ignore ATTRIBUTE_UNUSED)
{
#define MAX_ARGS 2

  unsigned int const *code_for_builtin =
    TARGET_64BIT ? code_for_builtin_64 : code_for_builtin_31;

  tree fndecl = TREE_OPERAND (TREE_OPERAND (exp, 0), 0);
  unsigned int fcode = DECL_FUNCTION_CODE (fndecl);
  tree arglist = TREE_OPERAND (exp, 1);
  enum insn_code icode;
  rtx op[MAX_ARGS], pat;
  int arity;
  bool nonvoid;

  if (fcode >= S390_BUILTIN_max)
    internal_error ("bad builtin fcode");
  icode = code_for_builtin[fcode];
  if (icode == 0)
    internal_error ("bad builtin fcode");

  nonvoid = TREE_TYPE (TREE_TYPE (fndecl)) != void_type_node;

  for (arglist = TREE_OPERAND (exp, 1), arity = 0;
       arglist;
       arglist = TREE_CHAIN (arglist), arity++)
    {
      const struct insn_operand_data *insn_op;

      tree arg = TREE_VALUE (arglist);
      if (arg == error_mark_node)
	return NULL_RTX;
      if (arity > MAX_ARGS)
	return NULL_RTX;

      insn_op = &insn_data[icode].operand[arity + nonvoid];

      op[arity] = expand_expr (arg, NULL_RTX, insn_op->mode, 0);

      if (!(*insn_op->predicate) (op[arity], insn_op->mode))
	op[arity] = copy_to_mode_reg (insn_op->mode, op[arity]);
    }

  if (nonvoid)
    {
      enum machine_mode tmode = insn_data[icode].operand[0].mode;
      if (!target
	  || GET_MODE (target) != tmode
	  || !(*insn_data[icode].operand[0].predicate) (target, tmode))
	target = gen_reg_rtx (tmode);
    }

  switch (arity)
    {
    case 0:
      pat = GEN_FCN (icode) (target);
      break;
    case 1:
      if (nonvoid)
        pat = GEN_FCN (icode) (target, op[0]);
      else
	pat = GEN_FCN (icode) (op[0]);
      break;
    case 2:
      pat = GEN_FCN (icode) (target, op[0], op[1]);
      break;
    default:
      gcc_unreachable ();
    }
  if (!pat)
    return NULL_RTX;
  emit_insn (pat);

  if (nonvoid)
    return target;
  else
    return const0_rtx;
}


/* Output assembly code for the trampoline template to
   stdio stream FILE.

   On S/390, we use gpr 1 internally in the trampoline code;
   gpr 0 is used to hold the static chain.  */

void
s390_trampoline_template (FILE *file)
{
  rtx op[2];
  op[0] = gen_rtx_REG (Pmode, 0);
  op[1] = gen_rtx_REG (Pmode, 1);

  if (TARGET_64BIT)
    {
      output_asm_insn ("basr\t%1,0", op);
      output_asm_insn ("lmg\t%0,%1,14(%1)", op);
      output_asm_insn ("br\t%1", op);
      ASM_OUTPUT_SKIP (file, (HOST_WIDE_INT)(TRAMPOLINE_SIZE - 10));
    }
  else
    {
      output_asm_insn ("basr\t%1,0", op);
      output_asm_insn ("lm\t%0,%1,6(%1)", op);
      output_asm_insn ("br\t%1", op);
      ASM_OUTPUT_SKIP (file, (HOST_WIDE_INT)(TRAMPOLINE_SIZE - 8));
    }
}

/* Emit RTL insns to initialize the variable parts of a trampoline.
   FNADDR is an RTX for the address of the function's pure code.
   CXT is an RTX for the static chain value for the function.  */

void
s390_initialize_trampoline (rtx addr, rtx fnaddr, rtx cxt)
{
  emit_move_insn (gen_rtx_MEM (Pmode,
		   memory_address (Pmode,
		   plus_constant (addr, (TARGET_64BIT ? 16 : 8)))), cxt);
  emit_move_insn (gen_rtx_MEM (Pmode,
		   memory_address (Pmode,
		   plus_constant (addr, (TARGET_64BIT ? 24 : 12)))), fnaddr);
}

/* Return rtx for 64-bit constant formed from the 32-bit subwords
   LOW and HIGH, independent of the host word size.  */

rtx
s390_gen_rtx_const_DI (int high, int low)
{
#if HOST_BITS_PER_WIDE_INT >= 64
  HOST_WIDE_INT val;
  val = (HOST_WIDE_INT)high;
  val <<= 32;
  val |= (HOST_WIDE_INT)low;

  return GEN_INT (val);
#else
#if HOST_BITS_PER_WIDE_INT >= 32
  return immed_double_const ((HOST_WIDE_INT)low, (HOST_WIDE_INT)high, DImode);
#else
  gcc_unreachable ();
#endif
#endif
}

/* Output assembler code to FILE to increment profiler label # LABELNO
   for profiling a function entry.  */

void
s390_function_profiler (FILE *file, int labelno)
{
  rtx op[7];

  char label[128];
  ASM_GENERATE_INTERNAL_LABEL (label, "LP", labelno);

  fprintf (file, "# function profiler \n");

  op[0] = gen_rtx_REG (Pmode, RETURN_REGNUM);
  op[1] = gen_rtx_REG (Pmode, STACK_POINTER_REGNUM);
  op[1] = gen_rtx_MEM (Pmode, plus_constant (op[1], UNITS_PER_WORD));

  op[2] = gen_rtx_REG (Pmode, 1);
  op[3] = gen_rtx_SYMBOL_REF (Pmode, label);
  SYMBOL_REF_FLAGS (op[3]) = SYMBOL_FLAG_LOCAL;

  op[4] = gen_rtx_SYMBOL_REF (Pmode, "_mcount");
  if (flag_pic)
    {
      op[4] = gen_rtx_UNSPEC (Pmode, gen_rtvec (1, op[4]), UNSPEC_PLT);
      op[4] = gen_rtx_CONST (Pmode, op[4]);
    }

  if (TARGET_64BIT)
    {
      output_asm_insn ("stg\t%0,%1", op);
      output_asm_insn ("larl\t%2,%3", op);
      output_asm_insn ("brasl\t%0,%4", op);
      output_asm_insn ("lg\t%0,%1", op);
    }
  else if (!flag_pic)
    {
      op[6] = gen_label_rtx ();

      output_asm_insn ("st\t%0,%1", op);
      output_asm_insn ("bras\t%2,%l6", op);
      output_asm_insn (".long\t%4", op);
      output_asm_insn (".long\t%3", op);
      targetm.asm_out.internal_label (file, "L", CODE_LABEL_NUMBER (op[6]));
      output_asm_insn ("l\t%0,0(%2)", op);
      output_asm_insn ("l\t%2,4(%2)", op);
      output_asm_insn ("basr\t%0,%0", op);
      output_asm_insn ("l\t%0,%1", op);
    }
  else
    {
      op[5] = gen_label_rtx ();
      op[6] = gen_label_rtx ();

      output_asm_insn ("st\t%0,%1", op);
      output_asm_insn ("bras\t%2,%l6", op);
      targetm.asm_out.internal_label (file, "L", CODE_LABEL_NUMBER (op[5]));
      output_asm_insn (".long\t%4-%l5", op);
      output_asm_insn (".long\t%3-%l5", op);
      targetm.asm_out.internal_label (file, "L", CODE_LABEL_NUMBER (op[6]));
      output_asm_insn ("lr\t%0,%2", op);
      output_asm_insn ("a\t%0,0(%2)", op);
      output_asm_insn ("a\t%2,4(%2)", op);
      output_asm_insn ("basr\t%0,%0", op);
      output_asm_insn ("l\t%0,%1", op);
    }
}

/* Encode symbol attributes (local vs. global, tls model) of a SYMBOL_REF
   into its SYMBOL_REF_FLAGS.  */

static void
s390_encode_section_info (tree decl, rtx rtl, int first)
{
  default_encode_section_info (decl, rtl, first);

  /* If a variable has a forced alignment to < 2 bytes, mark it with
     SYMBOL_FLAG_ALIGN1 to prevent it from being used as LARL operand.  */
  if (TREE_CODE (decl) == VAR_DECL
      && DECL_USER_ALIGN (decl) && DECL_ALIGN (decl) < 16)
    SYMBOL_REF_FLAGS (XEXP (rtl, 0)) |= SYMBOL_FLAG_ALIGN1;
}

/* Output thunk to FILE that implements a C++ virtual function call (with
   multiple inheritance) to FUNCTION.  The thunk adjusts the this pointer
   by DELTA, and unless VCALL_OFFSET is zero, applies an additional adjustment
   stored at VCALL_OFFSET in the vtable whose address is located at offset 0
   relative to the resulting this pointer.  */

static void
s390_output_mi_thunk (FILE *file, tree thunk ATTRIBUTE_UNUSED,
		      HOST_WIDE_INT delta, HOST_WIDE_INT vcall_offset,
		      tree function)
{
  rtx op[10];
  int nonlocal = 0;

  /* Operand 0 is the target function.  */
  op[0] = XEXP (DECL_RTL (function), 0);
  if (flag_pic && !SYMBOL_REF_LOCAL_P (op[0]))
    {
      nonlocal = 1;
      op[0] = gen_rtx_UNSPEC (Pmode, gen_rtvec (1, op[0]),
			      TARGET_64BIT ? UNSPEC_PLT : UNSPEC_GOT);
      op[0] = gen_rtx_CONST (Pmode, op[0]);
    }

  /* Operand 1 is the 'this' pointer.  */
  if (aggregate_value_p (TREE_TYPE (TREE_TYPE (function)), function))
    op[1] = gen_rtx_REG (Pmode, 3);
  else
    op[1] = gen_rtx_REG (Pmode, 2);

  /* Operand 2 is the delta.  */
  op[2] = GEN_INT (delta);

  /* Operand 3 is the vcall_offset.  */
  op[3] = GEN_INT (vcall_offset);

  /* Operand 4 is the temporary register.  */
  op[4] = gen_rtx_REG (Pmode, 1);

  /* Operands 5 to 8 can be used as labels.  */
  op[5] = NULL_RTX;
  op[6] = NULL_RTX;
  op[7] = NULL_RTX;
  op[8] = NULL_RTX;

  /* Operand 9 can be used for temporary register.  */
  op[9] = NULL_RTX;

  /* Generate code.  */
  if (TARGET_64BIT)
    {
      /* Setup literal pool pointer if required.  */
      if ((!DISP_IN_RANGE (delta)
	   && !CONST_OK_FOR_K (delta)
	   && !CONST_OK_FOR_Os (delta))
	  || (!DISP_IN_RANGE (vcall_offset)
	      && !CONST_OK_FOR_K (vcall_offset)
	      && !CONST_OK_FOR_Os (vcall_offset)))
	{
	  op[5] = gen_label_rtx ();
	  output_asm_insn ("larl\t%4,%5", op);
	}

      /* Add DELTA to this pointer.  */
      if (delta)
	{
	  if (CONST_OK_FOR_J (delta))
	    output_asm_insn ("la\t%1,%2(%1)", op);
	  else if (DISP_IN_RANGE (delta))
	    output_asm_insn ("lay\t%1,%2(%1)", op);
	  else if (CONST_OK_FOR_K (delta))
	    output_asm_insn ("aghi\t%1,%2", op);
 	  else if (CONST_OK_FOR_Os (delta))
 	    output_asm_insn ("agfi\t%1,%2", op);
	  else
	    {
	      op[6] = gen_label_rtx ();
	      output_asm_insn ("agf\t%1,%6-%5(%4)", op);
	    }
	}

      /* Perform vcall adjustment.  */
      if (vcall_offset)
	{
	  if (DISP_IN_RANGE (vcall_offset))
	    {
	      output_asm_insn ("lg\t%4,0(%1)", op);
	      output_asm_insn ("ag\t%1,%3(%4)", op);
	    }
	  else if (CONST_OK_FOR_K (vcall_offset))
	    {
	      output_asm_insn ("lghi\t%4,%3", op);
	      output_asm_insn ("ag\t%4,0(%1)", op);
	      output_asm_insn ("ag\t%1,0(%4)", op);
	    }
 	  else if (CONST_OK_FOR_Os (vcall_offset))
 	    {
 	      output_asm_insn ("lgfi\t%4,%3", op);
 	      output_asm_insn ("ag\t%4,0(%1)", op);
 	      output_asm_insn ("ag\t%1,0(%4)", op);
 	    }
	  else
	    {
	      op[7] = gen_label_rtx ();
	      output_asm_insn ("llgf\t%4,%7-%5(%4)", op);
	      output_asm_insn ("ag\t%4,0(%1)", op);
	      output_asm_insn ("ag\t%1,0(%4)", op);
	    }
	}

      /* Jump to target.  */
      output_asm_insn ("jg\t%0", op);

      /* Output literal pool if required.  */
      if (op[5])
	{
	  output_asm_insn (".align\t4", op);
	  targetm.asm_out.internal_label (file, "L",
					  CODE_LABEL_NUMBER (op[5]));
	}
      if (op[6])
	{
	  targetm.asm_out.internal_label (file, "L",
					  CODE_LABEL_NUMBER (op[6]));
	  output_asm_insn (".long\t%2", op);
	}
      if (op[7])
	{
	  targetm.asm_out.internal_label (file, "L",
					  CODE_LABEL_NUMBER (op[7]));
	  output_asm_insn (".long\t%3", op);
	}
    }
  else
    {
      /* Setup base pointer if required.  */
      if (!vcall_offset
	  || (!DISP_IN_RANGE (delta)
              && !CONST_OK_FOR_K (delta)
	      && !CONST_OK_FOR_Os (delta))
	  || (!DISP_IN_RANGE (delta)
              && !CONST_OK_FOR_K (vcall_offset)
	      && !CONST_OK_FOR_Os (vcall_offset)))
	{
	  op[5] = gen_label_rtx ();
	  output_asm_insn ("basr\t%4,0", op);
	  targetm.asm_out.internal_label (file, "L",
					  CODE_LABEL_NUMBER (op[5]));
	}

      /* Add DELTA to this pointer.  */
      if (delta)
	{
	  if (CONST_OK_FOR_J (delta))
	    output_asm_insn ("la\t%1,%2(%1)", op);
	  else if (DISP_IN_RANGE (delta))
	    output_asm_insn ("lay\t%1,%2(%1)", op);
	  else if (CONST_OK_FOR_K (delta))
	    output_asm_insn ("ahi\t%1,%2", op);
	  else if (CONST_OK_FOR_Os (delta))
 	    output_asm_insn ("afi\t%1,%2", op);
	  else
	    {
	      op[6] = gen_label_rtx ();
	      output_asm_insn ("a\t%1,%6-%5(%4)", op);
	    }
	}

      /* Perform vcall adjustment.  */
      if (vcall_offset)
        {
	  if (CONST_OK_FOR_J (vcall_offset))
	    {
	      output_asm_insn ("l\t%4,0(%1)", op);
	      output_asm_insn ("a\t%1,%3(%4)", op);
	    }
	  else if (DISP_IN_RANGE (vcall_offset))
	    {
	      output_asm_insn ("l\t%4,0(%1)", op);
	      output_asm_insn ("ay\t%1,%3(%4)", op);
	    }
	  else if (CONST_OK_FOR_K (vcall_offset))
	    {
	      output_asm_insn ("lhi\t%4,%3", op);
	      output_asm_insn ("a\t%4,0(%1)", op);
	      output_asm_insn ("a\t%1,0(%4)", op);
	    }
	  else if (CONST_OK_FOR_Os (vcall_offset))
 	    {
 	      output_asm_insn ("iilf\t%4,%3", op);
 	      output_asm_insn ("a\t%4,0(%1)", op);
 	      output_asm_insn ("a\t%1,0(%4)", op);
 	    }
	  else
	    {
	      op[7] = gen_label_rtx ();
	      output_asm_insn ("l\t%4,%7-%5(%4)", op);
	      output_asm_insn ("a\t%4,0(%1)", op);
	      output_asm_insn ("a\t%1,0(%4)", op);
	    }

	  /* We had to clobber the base pointer register.
	     Re-setup the base pointer (with a different base).  */
	  op[5] = gen_label_rtx ();
	  output_asm_insn ("basr\t%4,0", op);
	  targetm.asm_out.internal_label (file, "L",
					  CODE_LABEL_NUMBER (op[5]));
	}

      /* Jump to target.  */
      op[8] = gen_label_rtx ();

      if (!flag_pic)
	output_asm_insn ("l\t%4,%8-%5(%4)", op);
      else if (!nonlocal)
	output_asm_insn ("a\t%4,%8-%5(%4)", op);
      /* We cannot call through .plt, since .plt requires %r12 loaded.  */
      else if (flag_pic == 1)
	{
	  output_asm_insn ("a\t%4,%8-%5(%4)", op);
	  output_asm_insn ("l\t%4,%0(%4)", op);
	}
      else if (flag_pic == 2)
	{
	  op[9] = gen_rtx_REG (Pmode, 0);
	  output_asm_insn ("l\t%9,%8-4-%5(%4)", op);
	  output_asm_insn ("a\t%4,%8-%5(%4)", op);
	  output_asm_insn ("ar\t%4,%9", op);
	  output_asm_insn ("l\t%4,0(%4)", op);
	}

      output_asm_insn ("br\t%4", op);

      /* Output literal pool.  */
      output_asm_insn (".align\t4", op);

      if (nonlocal && flag_pic == 2)
	output_asm_insn (".long\t%0", op);
      if (nonlocal)
	{
	  op[0] = gen_rtx_SYMBOL_REF (Pmode, "_GLOBAL_OFFSET_TABLE_");
	  SYMBOL_REF_FLAGS (op[0]) = SYMBOL_FLAG_LOCAL;
	}

      targetm.asm_out.internal_label (file, "L", CODE_LABEL_NUMBER (op[8]));
      if (!flag_pic)
	output_asm_insn (".long\t%0", op);
      else
	output_asm_insn (".long\t%0-%5", op);

      if (op[6])
	{
	  targetm.asm_out.internal_label (file, "L",
					  CODE_LABEL_NUMBER (op[6]));
	  output_asm_insn (".long\t%2", op);
	}
      if (op[7])
	{
	  targetm.asm_out.internal_label (file, "L",
					  CODE_LABEL_NUMBER (op[7]));
	  output_asm_insn (".long\t%3", op);
	}
    }
}

static bool
s390_valid_pointer_mode (enum machine_mode mode)
{
  return (mode == SImode || (TARGET_64BIT && mode == DImode));
}

/* Checks whether the given ARGUMENT_LIST would use a caller
   saved register.  This is used to decide whether sibling call
   optimization could be performed on the respective function
   call.  */

static bool
s390_call_saved_register_used (tree argument_list)
{
  CUMULATIVE_ARGS cum;
  tree parameter;
  enum machine_mode mode;
  tree type;
  rtx parm_rtx;
  int reg;

  INIT_CUMULATIVE_ARGS (cum, NULL, NULL, 0, 0);

  while (argument_list)
    {
      parameter = TREE_VALUE (argument_list);
      argument_list = TREE_CHAIN (argument_list);

      gcc_assert (parameter);

      /* For an undeclared variable passed as parameter we will get
	 an ERROR_MARK node here.  */
      if (TREE_CODE (parameter) == ERROR_MARK)
	return true;

      type = TREE_TYPE (parameter);
      gcc_assert (type);

      mode = TYPE_MODE (type);
      gcc_assert (mode);

      if (pass_by_reference (&cum, mode, type, true))
 	{
 	  mode = Pmode;
 	  type = build_pointer_type (type);
 	}

       parm_rtx = s390_function_arg (&cum, mode, type, 0);

       s390_function_arg_advance (&cum, mode, type, 0);

       if (parm_rtx && REG_P (parm_rtx))
	 {
	   for (reg = 0;
		reg < HARD_REGNO_NREGS (REGNO (parm_rtx), GET_MODE (parm_rtx));
		reg++)
	     if (! call_used_regs[reg + REGNO (parm_rtx)])
	       return true;
	 }
    }
  return false;
}

/* Return true if the given call expression can be
   turned into a sibling call.
   DECL holds the declaration of the function to be called whereas
   EXP is the call expression itself.  */

static bool
s390_function_ok_for_sibcall (tree decl, tree exp)
{
  /* The TPF epilogue uses register 1.  */
  if (TARGET_TPF_PROFILING)
    return false;

  /* The 31 bit PLT code uses register 12 (GOT pointer - caller saved)
     which would have to be restored before the sibcall.  */
  if (!TARGET_64BIT && flag_pic && decl && !targetm.binds_local_p (decl))
    return false;

  /* Register 6 on s390 is available as an argument register but unfortunately
     "caller saved". This makes functions needing this register for arguments
     not suitable for sibcalls.  */
  if (TREE_OPERAND (exp, 1)
      && s390_call_saved_register_used (TREE_OPERAND (exp, 1)))
      return false;

  return true;
}

/* Return the fixed registers used for condition codes.  */

static bool
s390_fixed_condition_code_regs (unsigned int *p1, unsigned int *p2)
{
  *p1 = CC_REGNUM;
  *p2 = INVALID_REGNUM;
 
  return true;
}

/* This function is used by the call expanders of the machine description.
   It emits the call insn itself together with the necessary operations
   to adjust the target address and returns the emitted insn.
   ADDR_LOCATION is the target address rtx
   TLS_CALL the location of the thread-local symbol
   RESULT_REG the register where the result of the call should be stored
   RETADDR_REG the register where the return address should be stored
               If this parameter is NULL_RTX the call is considered
               to be a sibling call.  */

rtx
s390_emit_call (rtx addr_location, rtx tls_call, rtx result_reg,
		rtx retaddr_reg)
{
  bool plt_call = false;
  rtx insn;
  rtx call;
  rtx clobber;
  rtvec vec;

  /* Direct function calls need special treatment.  */
  if (GET_CODE (addr_location) == SYMBOL_REF)
    {
      /* When calling a global routine in PIC mode, we must
         replace the symbol itself with the PLT stub.  */
      if (flag_pic && !SYMBOL_REF_LOCAL_P (addr_location))
        {
	  addr_location = gen_rtx_UNSPEC (Pmode,
					  gen_rtvec (1, addr_location),
					  UNSPEC_PLT);
	  addr_location = gen_rtx_CONST (Pmode, addr_location);
	  plt_call = true;
        }

      /* Unless we can use the bras(l) insn, force the
         routine address into a register.  */
      if (!TARGET_SMALL_EXEC && !TARGET_CPU_ZARCH)
        {
	  if (flag_pic)
	    addr_location = legitimize_pic_address (addr_location, 0);
	  else
	    addr_location = force_reg (Pmode, addr_location);
	}
    }

  /* If it is already an indirect call or the code above moved the
     SYMBOL_REF to somewhere else make sure the address can be found in
     register 1.  */
  if (retaddr_reg == NULL_RTX
      && GET_CODE (addr_location) != SYMBOL_REF
      && !plt_call)
    {
      emit_move_insn (gen_rtx_REG (Pmode, SIBCALL_REGNUM), addr_location);
      addr_location = gen_rtx_REG (Pmode, SIBCALL_REGNUM);
    }

  addr_location = gen_rtx_MEM (QImode, addr_location);
  call = gen_rtx_CALL (VOIDmode, addr_location, const0_rtx);

  if (result_reg != NULL_RTX)
    call = gen_rtx_SET (VOIDmode, result_reg, call);

  if (retaddr_reg != NULL_RTX)
    {
      clobber = gen_rtx_CLOBBER (VOIDmode, retaddr_reg);

      if (tls_call != NULL_RTX)
	vec = gen_rtvec (3, call, clobber,
			 gen_rtx_USE (VOIDmode, tls_call));
      else
	vec = gen_rtvec (2, call, clobber);

      call = gen_rtx_PARALLEL (VOIDmode, vec);
    }

  insn = emit_call_insn (call);

  /* 31-bit PLT stubs and tls calls use the GOT register implicitly.  */
  if ((!TARGET_64BIT && plt_call) || tls_call != NULL_RTX)
    {
      /* s390_function_ok_for_sibcall should
	 have denied sibcalls in this case.  */
      gcc_assert (retaddr_reg != NULL_RTX);

      use_reg (&CALL_INSN_FUNCTION_USAGE (insn), pic_offset_table_rtx);
    }
  return insn;
}

/* Implement CONDITIONAL_REGISTER_USAGE.  */

void
s390_conditional_register_usage (void)
{
  int i;

  if (flag_pic)
    {
      fixed_regs[PIC_OFFSET_TABLE_REGNUM] = 1;
      call_used_regs[PIC_OFFSET_TABLE_REGNUM] = 1;
    }
  if (TARGET_CPU_ZARCH)
    {
      fixed_regs[BASE_REGNUM] = 0;
      call_used_regs[BASE_REGNUM] = 0;
      fixed_regs[RETURN_REGNUM] = 0;
      call_used_regs[RETURN_REGNUM] = 0;
    }
  if (TARGET_64BIT)
    {
      for (i = 24; i < 32; i++)
	call_used_regs[i] = call_really_used_regs[i] = 0;
    }
  else
    {
      for (i = 18; i < 20; i++)
	call_used_regs[i] = call_really_used_regs[i] = 0;
    }

  if (TARGET_SOFT_FLOAT)
    {
      for (i = 16; i < 32; i++)
	call_used_regs[i] = fixed_regs[i] = 1;
    }
}

/* Corresponding function to eh_return expander.  */

static GTY(()) rtx s390_tpf_eh_return_symbol;
void
s390_emit_tpf_eh_return (rtx target)
{
  rtx insn, reg;

  if (!s390_tpf_eh_return_symbol)
    s390_tpf_eh_return_symbol = gen_rtx_SYMBOL_REF (Pmode, "__tpf_eh_return");

  reg = gen_rtx_REG (Pmode, 2);

  emit_move_insn (reg, target);
  insn = s390_emit_call (s390_tpf_eh_return_symbol, NULL_RTX, reg,
                                     gen_rtx_REG (Pmode, RETURN_REGNUM));
  use_reg (&CALL_INSN_FUNCTION_USAGE (insn), reg);

  emit_move_insn (EH_RETURN_HANDLER_RTX, reg);
}

/* Rework the prologue/epilogue to avoid saving/restoring
   registers unnecessarily.  */

static void
s390_optimize_prologue (void)
{
  rtx insn, new_insn, next_insn;

  /* Do a final recompute of the frame-related data.  */

  s390_update_frame_layout ();

  /* If all special registers are in fact used, there's nothing we
     can do, so no point in walking the insn list.  */

  if (cfun_frame_layout.first_save_gpr <= BASE_REGNUM 
      && cfun_frame_layout.last_save_gpr >= BASE_REGNUM
      && (TARGET_CPU_ZARCH 
          || (cfun_frame_layout.first_save_gpr <= RETURN_REGNUM 
              && cfun_frame_layout.last_save_gpr >= RETURN_REGNUM)))
    return;

  /* Search for prologue/epilogue insns and replace them.  */

  for (insn = get_insns (); insn; insn = next_insn)
    {
      int first, last, off;
      rtx set, base, offset;

      next_insn = NEXT_INSN (insn);

      if (GET_CODE (insn) != INSN)
	continue;

      if (GET_CODE (PATTERN (insn)) == PARALLEL
	  && store_multiple_operation (PATTERN (insn), VOIDmode))
	{
	  set = XVECEXP (PATTERN (insn), 0, 0);
	  first = REGNO (SET_SRC (set));
	  last = first + XVECLEN (PATTERN (insn), 0) - 1;
	  offset = const0_rtx;
	  base = eliminate_constant_term (XEXP (SET_DEST (set), 0), &offset);
	  off = INTVAL (offset);

	  if (GET_CODE (base) != REG || off < 0)
	    continue;
	  if (cfun_frame_layout.first_save_gpr != -1
	      && (cfun_frame_layout.first_save_gpr < first
		  || cfun_frame_layout.last_save_gpr > last))
	    continue;
	  if (REGNO (base) != STACK_POINTER_REGNUM
	      && REGNO (base) != HARD_FRAME_POINTER_REGNUM)
	    continue;
	  if (first > BASE_REGNUM || last < BASE_REGNUM)
	    continue;

	  if (cfun_frame_layout.first_save_gpr != -1)
	    {
	      new_insn 	= save_gprs (base, 
				     off + (cfun_frame_layout.first_save_gpr
					    - first) * UNITS_PER_WORD, 
				     cfun_frame_layout.first_save_gpr,
				     cfun_frame_layout.last_save_gpr);
	      new_insn = emit_insn_before (new_insn, insn);
	      INSN_ADDRESSES_NEW (new_insn, -1);
	    }

	  remove_insn (insn);
	  continue;
	}

      if (cfun_frame_layout.first_save_gpr == -1
	  && GET_CODE (PATTERN (insn)) == SET
	  && GET_CODE (SET_SRC (PATTERN (insn))) == REG
	  && (REGNO (SET_SRC (PATTERN (insn))) == BASE_REGNUM
	      || (!TARGET_CPU_ZARCH
		  && REGNO (SET_SRC (PATTERN (insn))) == RETURN_REGNUM))
	  && GET_CODE (SET_DEST (PATTERN (insn))) == MEM)
	{
	  set = PATTERN (insn);
	  first = REGNO (SET_SRC (set));
	  offset = const0_rtx;
	  base = eliminate_constant_term (XEXP (SET_DEST (set), 0), &offset);
	  off = INTVAL (offset);

	  if (GET_CODE (base) != REG || off < 0)
	    continue;
	  if (REGNO (base) != STACK_POINTER_REGNUM
	      && REGNO (base) != HARD_FRAME_POINTER_REGNUM)
	    continue;

	  remove_insn (insn);
	  continue;
	}

      if (GET_CODE (PATTERN (insn)) == PARALLEL
	  && load_multiple_operation (PATTERN (insn), VOIDmode))
	{
	  set = XVECEXP (PATTERN (insn), 0, 0);
	  first = REGNO (SET_DEST (set));
	  last = first + XVECLEN (PATTERN (insn), 0) - 1;
	  offset = const0_rtx;
	  base = eliminate_constant_term (XEXP (SET_SRC (set), 0), &offset);
	  off = INTVAL (offset);

	  if (GET_CODE (base) != REG || off < 0)
	    continue;
	  if (cfun_frame_layout.first_restore_gpr != -1
	      && (cfun_frame_layout.first_restore_gpr < first
		  || cfun_frame_layout.last_restore_gpr > last))
	    continue;
	  if (REGNO (base) != STACK_POINTER_REGNUM
	      && REGNO (base) != HARD_FRAME_POINTER_REGNUM)
	    continue;
	  if (first > BASE_REGNUM || last < BASE_REGNUM)
	    continue;

	  if (cfun_frame_layout.first_restore_gpr != -1)
	    {
	      new_insn = restore_gprs (base, 
				       off + (cfun_frame_layout.first_restore_gpr
					      - first) * UNITS_PER_WORD, 
				       cfun_frame_layout.first_restore_gpr,
				       cfun_frame_layout.last_restore_gpr);
	      new_insn = emit_insn_before (new_insn, insn);
	      INSN_ADDRESSES_NEW (new_insn, -1);
	    }

	  remove_insn (insn);
	  continue;
	}

      if (cfun_frame_layout.first_restore_gpr == -1
	  && GET_CODE (PATTERN (insn)) == SET
	  && GET_CODE (SET_DEST (PATTERN (insn))) == REG
	  && (REGNO (SET_DEST (PATTERN (insn))) == BASE_REGNUM
	      || (!TARGET_CPU_ZARCH
		  && REGNO (SET_DEST (PATTERN (insn))) == RETURN_REGNUM))
	  && GET_CODE (SET_SRC (PATTERN (insn))) == MEM)
	{
	  set = PATTERN (insn);
	  first = REGNO (SET_DEST (set));
	  offset = const0_rtx;
	  base = eliminate_constant_term (XEXP (SET_SRC (set), 0), &offset);
	  off = INTVAL (offset);

	  if (GET_CODE (base) != REG || off < 0)
	    continue;
	  if (REGNO (base) != STACK_POINTER_REGNUM
	      && REGNO (base) != HARD_FRAME_POINTER_REGNUM)
	    continue;

	  remove_insn (insn);
	  continue;
	}
    }
}

/* Perform machine-dependent processing.  */

static void
s390_reorg (void)
{
  bool pool_overflow = false;

  /* Make sure all splits have been performed; splits after
     machine_dependent_reorg might confuse insn length counts.  */
  split_all_insns_noflow ();

  /* From here on decomposed literal pool addresses must be accepted.  */
  cfun->machine->decomposed_literal_pool_addresses_ok_p = true;

  /* Install the main literal pool and the associated base
     register load insns.

     In addition, there are two problematic situations we need
     to correct:

     - the literal pool might be > 4096 bytes in size, so that
       some of its elements cannot be directly accessed

     - a branch target might be > 64K away from the branch, so that
       it is not possible to use a PC-relative instruction.

     To fix those, we split the single literal pool into multiple
     pool chunks, reloading the pool base register at various
     points throughout the function to ensure it always points to
     the pool chunk the following code expects, and / or replace
     PC-relative branches by absolute branches.

     However, the two problems are interdependent: splitting the
     literal pool can move a branch further away from its target,
     causing the 64K limit to overflow, and on the other hand,
     replacing a PC-relative branch by an absolute branch means
     we need to put the branch target address into the literal
     pool, possibly causing it to overflow.

     So, we loop trying to fix up both problems until we manage
     to satisfy both conditions at the same time.  Note that the
     loop is guaranteed to terminate as every pass of the loop
     strictly decreases the total number of PC-relative branches
     in the function.  (This is not completely true as there
     might be branch-over-pool insns introduced by chunkify_start.
     Those never need to be split however.)  */

  for (;;)
    {
      struct constant_pool *pool = NULL;

      /* Collect the literal pool.  */
      if (!pool_overflow)
	{
	  pool = s390_mainpool_start ();
	  if (!pool)
	    pool_overflow = true;
	}

      /* If literal pool overflowed, start to chunkify it.  */
      if (pool_overflow)
        pool = s390_chunkify_start ();

      /* Split out-of-range branches.  If this has created new
	 literal pool entries, cancel current chunk list and
	 recompute it.  zSeries machines have large branch
	 instructions, so we never need to split a branch.  */
      if (!TARGET_CPU_ZARCH && s390_split_branches ())
        {
          if (pool_overflow)
            s390_chunkify_cancel (pool);
	  else
            s390_mainpool_cancel (pool);

          continue;
        }

      /* If we made it up to here, both conditions are satisfied.
	 Finish up literal pool related changes.  */
      if (pool_overflow)
	s390_chunkify_finish (pool);
      else
	s390_mainpool_finish (pool);

      /* We're done splitting branches.  */
      cfun->machine->split_branches_pending_p = false;
      break;
    }

  /* Generate out-of-pool execute target insns.  */
  if (TARGET_CPU_ZARCH)
    {
      rtx insn, label, target;

      for (insn = get_insns (); insn; insn = NEXT_INSN (insn))
	{
	  label = s390_execute_label (insn);
	  if (!label)
	    continue;

	  gcc_assert (label != const0_rtx);

	  target = emit_label (XEXP (label, 0));
	  INSN_ADDRESSES_NEW (target, -1);

	  target = emit_insn (s390_execute_target (insn));
	  INSN_ADDRESSES_NEW (target, -1);
	}
    }

  /* Try to optimize prologue and epilogue further.  */
  s390_optimize_prologue ();
}


/* Initialize GCC target structure.  */

#undef  TARGET_ASM_ALIGNED_HI_OP
#define TARGET_ASM_ALIGNED_HI_OP "\t.word\t"
#undef  TARGET_ASM_ALIGNED_DI_OP
#define TARGET_ASM_ALIGNED_DI_OP "\t.quad\t"
#undef  TARGET_ASM_INTEGER
#define TARGET_ASM_INTEGER s390_assemble_integer

#undef  TARGET_ASM_OPEN_PAREN
#define TARGET_ASM_OPEN_PAREN ""

#undef  TARGET_ASM_CLOSE_PAREN
#define TARGET_ASM_CLOSE_PAREN ""

#undef TARGET_DEFAULT_TARGET_FLAGS
#define TARGET_DEFAULT_TARGET_FLAGS (TARGET_DEFAULT | MASK_FUSED_MADD)
#undef TARGET_HANDLE_OPTION
#define TARGET_HANDLE_OPTION s390_handle_option

#undef	TARGET_ENCODE_SECTION_INFO
#define TARGET_ENCODE_SECTION_INFO s390_encode_section_info

#ifdef HAVE_AS_TLS
#undef TARGET_HAVE_TLS
#define TARGET_HAVE_TLS true
#endif
#undef TARGET_CANNOT_FORCE_CONST_MEM
#define TARGET_CANNOT_FORCE_CONST_MEM s390_cannot_force_const_mem

#undef TARGET_DELEGITIMIZE_ADDRESS
#define TARGET_DELEGITIMIZE_ADDRESS s390_delegitimize_address

#undef TARGET_RETURN_IN_MEMORY
#define TARGET_RETURN_IN_MEMORY s390_return_in_memory

#undef  TARGET_INIT_BUILTINS
#define TARGET_INIT_BUILTINS s390_init_builtins
#undef  TARGET_EXPAND_BUILTIN
#define TARGET_EXPAND_BUILTIN s390_expand_builtin

#undef TARGET_ASM_OUTPUT_MI_THUNK
#define TARGET_ASM_OUTPUT_MI_THUNK s390_output_mi_thunk
#undef TARGET_ASM_CAN_OUTPUT_MI_THUNK
#define TARGET_ASM_CAN_OUTPUT_MI_THUNK hook_bool_tree_hwi_hwi_tree_true

#undef  TARGET_SCHED_ADJUST_PRIORITY
#define TARGET_SCHED_ADJUST_PRIORITY s390_adjust_priority
#undef TARGET_SCHED_ISSUE_RATE
#define TARGET_SCHED_ISSUE_RATE s390_issue_rate
#undef TARGET_SCHED_FIRST_CYCLE_MULTIPASS_DFA_LOOKAHEAD
#define TARGET_SCHED_FIRST_CYCLE_MULTIPASS_DFA_LOOKAHEAD s390_first_cycle_multipass_dfa_lookahead

#undef TARGET_CANNOT_COPY_INSN_P
#define TARGET_CANNOT_COPY_INSN_P s390_cannot_copy_insn_p
#undef TARGET_RTX_COSTS
#define TARGET_RTX_COSTS s390_rtx_costs
#undef TARGET_ADDRESS_COST
#define TARGET_ADDRESS_COST s390_address_cost

#undef TARGET_MACHINE_DEPENDENT_REORG
#define TARGET_MACHINE_DEPENDENT_REORG s390_reorg

#undef TARGET_VALID_POINTER_MODE
#define TARGET_VALID_POINTER_MODE s390_valid_pointer_mode

#undef TARGET_BUILD_BUILTIN_VA_LIST
#define TARGET_BUILD_BUILTIN_VA_LIST s390_build_builtin_va_list
#undef TARGET_GIMPLIFY_VA_ARG_EXPR
#define TARGET_GIMPLIFY_VA_ARG_EXPR s390_gimplify_va_arg

#undef TARGET_PROMOTE_FUNCTION_ARGS
#define TARGET_PROMOTE_FUNCTION_ARGS hook_bool_tree_true
#undef TARGET_PROMOTE_FUNCTION_RETURN
#define TARGET_PROMOTE_FUNCTION_RETURN hook_bool_tree_true
#undef TARGET_PASS_BY_REFERENCE
#define TARGET_PASS_BY_REFERENCE s390_pass_by_reference

#undef TARGET_FUNCTION_OK_FOR_SIBCALL
#define TARGET_FUNCTION_OK_FOR_SIBCALL s390_function_ok_for_sibcall

#undef TARGET_FIXED_CONDITION_CODE_REGS
#define TARGET_FIXED_CONDITION_CODE_REGS s390_fixed_condition_code_regs

#undef TARGET_CC_MODES_COMPATIBLE
#define TARGET_CC_MODES_COMPATIBLE s390_cc_modes_compatible

#undef TARGET_INVALID_WITHIN_DOLOOP
#define TARGET_INVALID_WITHIN_DOLOOP hook_constcharptr_rtx_null

#ifdef HAVE_AS_TLS
#undef TARGET_ASM_OUTPUT_DWARF_DTPREL
#define TARGET_ASM_OUTPUT_DWARF_DTPREL s390_output_dwarf_dtprel
#endif

#ifdef TARGET_ALTERNATE_LONG_DOUBLE_MANGLING
#undef TARGET_MANGLE_FUNDAMENTAL_TYPE
#define TARGET_MANGLE_FUNDAMENTAL_TYPE s390_mangle_fundamental_type
#endif

#undef TARGET_SCALAR_MODE_SUPPORTED_P
#define TARGET_SCALAR_MODE_SUPPORTED_P s390_scalar_mode_supported_p

struct gcc_target targetm = TARGET_INITIALIZER;

#include "gt-s390.h"
