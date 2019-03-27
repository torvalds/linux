;; Predicate definitions for ARM and Thumb
;; Copyright (C) 2004 Free Software Foundation, Inc.
;; Contributed by ARM Ltd.

;; This file is part of GCC.

;; GCC is free software; you can redistribute it and/or modify it
;; under the terms of the GNU General Public License as published
;; by the Free Software Foundation; either version 2, or (at your
;; option) any later version.

;; GCC is distributed in the hope that it will be useful, but WITHOUT
;; ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
;; or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public
;; License for more details.

;; You should have received a copy of the GNU General Public License
;; along with GCC; see the file COPYING.  If not, write to
;; the Free Software Foundation, 51 Franklin Street, Fifth Floor,
;; Boston, MA 02110-1301, USA.

(define_predicate "s_register_operand"
  (match_code "reg,subreg")
{
  if (GET_CODE (op) == SUBREG)
    op = SUBREG_REG (op);
  /* We don't consider registers whose class is NO_REGS
     to be a register operand.  */
  /* XXX might have to check for lo regs only for thumb ??? */
  return (GET_CODE (op) == REG
	  && (REGNO (op) >= FIRST_PSEUDO_REGISTER
	      || REGNO_REG_CLASS (REGNO (op)) != NO_REGS));
})

;; Any hard register.
(define_predicate "arm_hard_register_operand"
  (match_code "reg")
{
  return REGNO (op) < FIRST_PSEUDO_REGISTER;
})

;; Any core register, or any pseudo.  */ 
(define_predicate "arm_general_register_operand"
  (match_code "reg,subreg")
{
  if (GET_CODE (op) == SUBREG)
    op = SUBREG_REG (op);

  return (GET_CODE (op) == REG
	  && (REGNO (op) <= LAST_ARM_REGNUM
	      || REGNO (op) >= FIRST_PSEUDO_REGISTER));
})

(define_predicate "f_register_operand"
  (match_code "reg,subreg")
{
  if (GET_CODE (op) == SUBREG)
    op = SUBREG_REG (op);

  /* We don't consider registers whose class is NO_REGS
     to be a register operand.  */
  return (GET_CODE (op) == REG
	  && (REGNO (op) >= FIRST_PSEUDO_REGISTER
	      || REGNO_REG_CLASS (REGNO (op)) == FPA_REGS));
})

;; Reg, subreg(reg) or const_int.
(define_predicate "reg_or_int_operand"
  (ior (match_code "const_int")
       (match_operand 0 "s_register_operand")))

(define_predicate "arm_immediate_operand"
  (and (match_code "const_int")
       (match_test "const_ok_for_arm (INTVAL (op))")))

(define_predicate "arm_neg_immediate_operand"
  (and (match_code "const_int")
       (match_test "const_ok_for_arm (-INTVAL (op))")))

(define_predicate "arm_not_immediate_operand"
  (and (match_code "const_int")
       (match_test "const_ok_for_arm (~INTVAL (op))")))

;; Something valid on the RHS of an ARM data-processing instruction
(define_predicate "arm_rhs_operand"
  (ior (match_operand 0 "s_register_operand")
       (match_operand 0 "arm_immediate_operand")))

(define_predicate "arm_rhsm_operand"
  (ior (match_operand 0 "arm_rhs_operand")
       (match_operand 0 "memory_operand")))

(define_predicate "arm_add_operand"
  (ior (match_operand 0 "arm_rhs_operand")
       (match_operand 0 "arm_neg_immediate_operand")))

(define_predicate "arm_addimm_operand"
  (ior (match_operand 0 "arm_immediate_operand")
       (match_operand 0 "arm_neg_immediate_operand")))

(define_predicate "arm_not_operand"
  (ior (match_operand 0 "arm_rhs_operand")
       (match_operand 0 "arm_not_immediate_operand")))

;; True if the operand is a memory reference which contains an
;; offsettable address.
(define_predicate "offsettable_memory_operand"
  (and (match_code "mem")
       (match_test
        "offsettable_address_p (reload_completed | reload_in_progress,
				mode, XEXP (op, 0))")))

;; True if the operand is a memory operand that does not have an
;; automodified base register (and thus will not generate output reloads).
(define_predicate "call_memory_operand"
  (and (match_code "mem")
       (and (match_test "GET_RTX_CLASS (GET_CODE (XEXP (op, 0)))
			 != RTX_AUTOINC")
	    (match_operand 0 "memory_operand"))))

(define_predicate "arm_reload_memory_operand"
  (and (match_code "mem,reg,subreg")
       (match_test "(!CONSTANT_P (op)
		     && (true_regnum(op) == -1
			 || (GET_CODE (op) == REG
			     && REGNO (op) >= FIRST_PSEUDO_REGISTER)))")))

;; True for valid operands for the rhs of an floating point insns.
;;   Allows regs or certain consts on FPA, just regs for everything else.
(define_predicate "arm_float_rhs_operand"
  (ior (match_operand 0 "s_register_operand")
       (and (match_code "const_double")
	    (match_test "TARGET_FPA && arm_const_double_rtx (op)"))))

(define_predicate "arm_float_add_operand"
  (ior (match_operand 0 "arm_float_rhs_operand")
       (and (match_code "const_double")
	    (match_test "TARGET_FPA && neg_const_double_rtx_ok_for_fpa (op)"))))

(define_predicate "vfp_compare_operand"
  (ior (match_operand 0 "s_register_operand")
       (and (match_code "const_double")
	    (match_test "arm_const_double_rtx (op)"))))

(define_predicate "arm_float_compare_operand"
  (if_then_else (match_test "TARGET_VFP")
		(match_operand 0 "vfp_compare_operand")
		(match_operand 0 "arm_float_rhs_operand")))

;; True for valid index operands.
(define_predicate "index_operand"
  (ior (match_operand 0 "s_register_operand")
       (and (match_operand 0 "immediate_operand")
	    (match_test "(GET_CODE (op) != CONST_INT
			  || (INTVAL (op) < 4096 && INTVAL (op) > -4096))"))))

;; True for operators that can be combined with a shift in ARM state.
(define_special_predicate "shiftable_operator"
  (and (match_code "plus,minus,ior,xor,and")
       (match_test "mode == GET_MODE (op)")))

;; True for logical binary operators.
(define_special_predicate "logical_binary_operator"
  (and (match_code "ior,xor,and")
       (match_test "mode == GET_MODE (op)")))

;; True for shift operators.
(define_special_predicate "shift_operator"
  (and (ior (ior (and (match_code "mult")
		      (match_test "power_of_two_operand (XEXP (op, 1), mode)"))
		 (and (match_code "rotate")
		      (match_test "GET_CODE (XEXP (op, 1)) == CONST_INT
				   && ((unsigned HOST_WIDE_INT) INTVAL (XEXP (op, 1))) < 32")))
	    (match_code "ashift,ashiftrt,lshiftrt,rotatert"))
       (match_test "mode == GET_MODE (op)")))

;; True for EQ & NE
(define_special_predicate "equality_operator"
  (match_code "eq,ne"))

;; True for comparisons other than LTGT or UNEQ.
(define_special_predicate "arm_comparison_operator"
  (match_code "eq,ne,le,lt,ge,gt,geu,gtu,leu,ltu,unordered,ordered,unlt,unle,unge,ungt"))

(define_special_predicate "minmax_operator"
  (and (match_code "smin,smax,umin,umax")
       (match_test "mode == GET_MODE (op)")))

(define_special_predicate "cc_register"
  (and (match_code "reg")
       (and (match_test "REGNO (op) == CC_REGNUM")
	    (ior (match_test "mode == GET_MODE (op)")
		 (match_test "mode == VOIDmode && GET_MODE_CLASS (GET_MODE (op)) == MODE_CC")))))

(define_special_predicate "dominant_cc_register"
  (match_code "reg")
{
  if (mode == VOIDmode)
    {
      mode = GET_MODE (op);
      
      if (GET_MODE_CLASS (mode) != MODE_CC)
	return false;
    }

  return (cc_register (op, mode)
	  && (mode == CC_DNEmode
	     || mode == CC_DEQmode
	     || mode == CC_DLEmode
	     || mode == CC_DLTmode
	     || mode == CC_DGEmode
	     || mode == CC_DGTmode
	     || mode == CC_DLEUmode
	     || mode == CC_DLTUmode
	     || mode == CC_DGEUmode
	     || mode == CC_DGTUmode));
})

(define_special_predicate "arm_extendqisi_mem_op"
  (and (match_operand 0 "memory_operand")
       (match_test "arm_legitimate_address_p (mode, XEXP (op, 0), SIGN_EXTEND,
					      0)")))

(define_predicate "power_of_two_operand"
  (match_code "const_int")
{
  HOST_WIDE_INT value = INTVAL (op);

  return value != 0 && (value & (value - 1)) == 0;
})

(define_predicate "nonimmediate_di_operand"
  (match_code "reg,subreg,mem")
{
   if (s_register_operand (op, mode))
     return true;

   if (GET_CODE (op) == SUBREG)
     op = SUBREG_REG (op);

   return GET_CODE (op) == MEM && memory_address_p (DImode, XEXP (op, 0));
})

(define_predicate "di_operand"
  (ior (match_code "const_int,const_double")
       (and (match_code "reg,subreg,mem")
	    (match_operand 0 "nonimmediate_di_operand"))))

(define_predicate "nonimmediate_soft_df_operand"
  (match_code "reg,subreg,mem")
{
  if (s_register_operand (op, mode))
    return true;

  if (GET_CODE (op) == SUBREG)
    op = SUBREG_REG (op);

  return GET_CODE (op) == MEM && memory_address_p (DFmode, XEXP (op, 0));
})

(define_predicate "soft_df_operand"
  (ior (match_code "const_double")
       (and (match_code "reg,subreg,mem")
	    (match_operand 0 "nonimmediate_soft_df_operand"))))

(define_predicate "const_shift_operand"
  (and (match_code "const_int")
       (ior (match_operand 0 "power_of_two_operand")
	    (match_test "((unsigned HOST_WIDE_INT) INTVAL (op)) < 32"))))


(define_special_predicate "load_multiple_operation"
  (match_code "parallel")
{
  HOST_WIDE_INT count = XVECLEN (op, 0);
  int dest_regno;
  rtx src_addr;
  HOST_WIDE_INT i = 1, base = 0;
  rtx elt;

  if (count <= 1
      || GET_CODE (XVECEXP (op, 0, 0)) != SET)
    return false;

  /* Check to see if this might be a write-back.  */
  if (GET_CODE (SET_SRC (elt = XVECEXP (op, 0, 0))) == PLUS)
    {
      i++;
      base = 1;

      /* Now check it more carefully.  */
      if (GET_CODE (SET_DEST (elt)) != REG
          || GET_CODE (XEXP (SET_SRC (elt), 0)) != REG
          || GET_CODE (XEXP (SET_SRC (elt), 1)) != CONST_INT
          || INTVAL (XEXP (SET_SRC (elt), 1)) != (count - 1) * 4)
        return false;
    }

  /* Perform a quick check so we don't blow up below.  */
  if (count <= i
      || GET_CODE (XVECEXP (op, 0, i - 1)) != SET
      || GET_CODE (SET_DEST (XVECEXP (op, 0, i - 1))) != REG
      || GET_CODE (SET_SRC (XVECEXP (op, 0, i - 1))) != MEM)
    return false;

  dest_regno = REGNO (SET_DEST (XVECEXP (op, 0, i - 1)));
  src_addr = XEXP (SET_SRC (XVECEXP (op, 0, i - 1)), 0);

  for (; i < count; i++)
    {
      elt = XVECEXP (op, 0, i);

      if (GET_CODE (elt) != SET
          || GET_CODE (SET_DEST (elt)) != REG
          || GET_MODE (SET_DEST (elt)) != SImode
          || REGNO (SET_DEST (elt)) != (unsigned int)(dest_regno + i - base)
          || GET_CODE (SET_SRC (elt)) != MEM
          || GET_MODE (SET_SRC (elt)) != SImode
          || GET_CODE (XEXP (SET_SRC (elt), 0)) != PLUS
          || !rtx_equal_p (XEXP (XEXP (SET_SRC (elt), 0), 0), src_addr)
          || GET_CODE (XEXP (XEXP (SET_SRC (elt), 0), 1)) != CONST_INT
          || INTVAL (XEXP (XEXP (SET_SRC (elt), 0), 1)) != (i - base) * 4)
        return false;
    }

  return true;
})

(define_special_predicate "store_multiple_operation"
  (match_code "parallel")
{
  HOST_WIDE_INT count = XVECLEN (op, 0);
  int src_regno;
  rtx dest_addr;
  HOST_WIDE_INT i = 1, base = 0;
  rtx elt;

  if (count <= 1
      || GET_CODE (XVECEXP (op, 0, 0)) != SET)
    return false;

  /* Check to see if this might be a write-back.  */
  if (GET_CODE (SET_SRC (elt = XVECEXP (op, 0, 0))) == PLUS)
    {
      i++;
      base = 1;

      /* Now check it more carefully.  */
      if (GET_CODE (SET_DEST (elt)) != REG
          || GET_CODE (XEXP (SET_SRC (elt), 0)) != REG
          || GET_CODE (XEXP (SET_SRC (elt), 1)) != CONST_INT
          || INTVAL (XEXP (SET_SRC (elt), 1)) != (count - 1) * 4)
        return false;
    }

  /* Perform a quick check so we don't blow up below.  */
  if (count <= i
      || GET_CODE (XVECEXP (op, 0, i - 1)) != SET
      || GET_CODE (SET_DEST (XVECEXP (op, 0, i - 1))) != MEM
      || GET_CODE (SET_SRC (XVECEXP (op, 0, i - 1))) != REG)
    return false;

  src_regno = REGNO (SET_SRC (XVECEXP (op, 0, i - 1)));
  dest_addr = XEXP (SET_DEST (XVECEXP (op, 0, i - 1)), 0);

  for (; i < count; i++)
    {
      elt = XVECEXP (op, 0, i);

      if (GET_CODE (elt) != SET
          || GET_CODE (SET_SRC (elt)) != REG
          || GET_MODE (SET_SRC (elt)) != SImode
          || REGNO (SET_SRC (elt)) != (unsigned int)(src_regno + i - base)
          || GET_CODE (SET_DEST (elt)) != MEM
          || GET_MODE (SET_DEST (elt)) != SImode
          || GET_CODE (XEXP (SET_DEST (elt), 0)) != PLUS
          || !rtx_equal_p (XEXP (XEXP (SET_DEST (elt), 0), 0), dest_addr)
          || GET_CODE (XEXP (XEXP (SET_DEST (elt), 0), 1)) != CONST_INT
          || INTVAL (XEXP (XEXP (SET_DEST (elt), 0), 1)) != (i - base) * 4)
        return false;
    }

  return true;
})

(define_special_predicate "multi_register_push"
  (match_code "parallel")
{
  if ((GET_CODE (XVECEXP (op, 0, 0)) != SET)
      || (GET_CODE (SET_SRC (XVECEXP (op, 0, 0))) != UNSPEC)
      || (XINT (SET_SRC (XVECEXP (op, 0, 0)), 1) != UNSPEC_PUSH_MULT))
    return false;

  return true;
})

;;-------------------------------------------------------------------------
;;
;; Thumb predicates
;;

(define_predicate "thumb_cmp_operand"
  (ior (and (match_code "reg,subreg")
	    (match_operand 0 "s_register_operand"))
       (and (match_code "const_int")
	    (match_test "((unsigned HOST_WIDE_INT) INTVAL (op)) < 256"))))

(define_predicate "thumb_cmpneg_operand"
  (and (match_code "const_int")
       (match_test "INTVAL (op) < 0 && INTVAL (op) > -256")))

;; Return TRUE if a result can be stored in OP without clobbering the
;; condition code register.  Prior to reload we only accept a
;; register.  After reload we have to be able to handle memory as
;; well, since a pseudo may not get a hard reg and reload cannot
;; handle output-reloads on jump insns.

;; We could possibly handle mem before reload as well, but that might
;; complicate things with the need to handle increment
;; side-effects.
(define_predicate "thumb_cbrch_target_operand"
  (and (match_code "reg,subreg,mem")
       (ior (match_operand 0 "s_register_operand")
	    (and (match_test "reload_in_progress || reload_completed")
		 (match_operand 0 "memory_operand")))))

;;-------------------------------------------------------------------------
;;
;; MAVERICK predicates
;;

(define_predicate "cirrus_register_operand"
  (match_code "reg,subreg")
{
  if (GET_CODE (op) == SUBREG)
    op = SUBREG_REG (op);

  return (GET_CODE (op) == REG
	  && (REGNO_REG_CLASS (REGNO (op)) == CIRRUS_REGS
	      || REGNO_REG_CLASS (REGNO (op)) == GENERAL_REGS));
})

(define_predicate "cirrus_fp_register"
  (match_code "reg,subreg")
{
  if (GET_CODE (op) == SUBREG)
    op = SUBREG_REG (op);

  return (GET_CODE (op) == REG
	  && (REGNO (op) >= FIRST_PSEUDO_REGISTER
	      || REGNO_REG_CLASS (REGNO (op)) == CIRRUS_REGS));
})

(define_predicate "cirrus_shift_const"
  (and (match_code "const_int")
       (match_test "((unsigned HOST_WIDE_INT) INTVAL (op)) < 64")))


