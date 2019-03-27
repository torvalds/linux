;; Predicate definitions for S/390 and zSeries.
;; Copyright (C) 2005 Free Software Foundation, Inc.
;; Contributed by Hartmut Penner (hpenner@de.ibm.com) and
;;                Ulrich Weigand (uweigand@de.ibm.com).
;;
;; This file is part of GCC.
;;
;; GCC is free software; you can redistribute it and/or modify
;; it under the terms of the GNU General Public License as published by
;; the Free Software Foundation; either version 2, or (at your option)
;; any later version.
;;
;; GCC is distributed in the hope that it will be useful,
;; but WITHOUT ANY WARRANTY; without even the implied warranty of
;; MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
;; GNU General Public License for more details.
;;
;; You should have received a copy of the GNU General Public License
;; along with GCC; see the file COPYING.  If not, write to
;; the Free Software Foundation, 51 Franklin Street, Fifth Floor,
;; Boston, MA 02110-1301, USA.

;; OP is the current operation.
;; MODE is the current operation mode.

;; operands --------------------------------------------------------------

;; Return true if OP a (const_int 0) operand.

(define_predicate "const0_operand"
  (and (match_code "const_int, const_double")
       (match_test "op == CONST0_RTX (mode)")))

;; Return true if OP is constant.

(define_special_predicate "consttable_operand"
  (and (match_code "symbol_ref, label_ref, const, const_int, const_double")
       (match_test "CONSTANT_P (op)")))

;; Return true if OP is a valid S-type operand.

(define_predicate "s_operand"
  (and (match_code "subreg, mem")
       (match_operand 0 "general_operand"))
{
  /* Just like memory_operand, allow (subreg (mem ...))
     after reload.  */
  if (reload_completed
      && GET_CODE (op) == SUBREG
      && GET_CODE (SUBREG_REG (op)) == MEM)
    op = SUBREG_REG (op);

  if (GET_CODE (op) != MEM)
    return false;
  if (!s390_legitimate_address_without_index_p (op))
    return false;

  return true;
})

;; Return true if OP is a valid operand for the BRAS instruction.
;; Allow SYMBOL_REFs and @PLT stubs.

(define_special_predicate "bras_sym_operand"
  (ior (and (match_code "symbol_ref")
	    (match_test "!flag_pic || SYMBOL_REF_LOCAL_P (op)"))
       (and (match_code "const")
	    (and (match_test "GET_CODE (XEXP (op, 0)) == UNSPEC")
		 (match_test "XINT (XEXP (op, 0), 1) == UNSPEC_PLT")))))

;; Return true if OP is a PLUS that is not a legitimate
;; operand for the LA instruction.

(define_predicate "s390_plus_operand"
  (and (match_code "plus")
       (and (match_test "mode == Pmode")
	    (match_test "!legitimate_la_operand_p (op)"))))

;; Return true if OP is a valid operand as shift count or setmem.

(define_predicate "shift_count_or_setmem_operand"
  (match_code "reg, subreg, plus, const_int")
{
  HOST_WIDE_INT offset;
  rtx base;

  /* Extract base register and offset.  */
  if (!s390_decompose_shift_count (op, &base, &offset))
    return false;

  /* Don't allow any non-base hard registers.  Doing so without
     confusing reload and/or regrename would be tricky, and doesn't
     buy us much anyway.  */
  if (base && REGNO (base) < FIRST_PSEUDO_REGISTER && !ADDR_REG_P (base))
    return false;

  /* Unfortunately we have to reject constants that are invalid
     for an address, or else reload will get confused.  */
  if (!DISP_IN_RANGE (offset))
    return false;

  return true;
})

;;  Return true if OP a valid operand for the LARL instruction.

(define_predicate "larl_operand"
  (match_code "label_ref, symbol_ref, const, const_int, const_double")
{
  /* Allow labels and local symbols.  */
  if (GET_CODE (op) == LABEL_REF)
    return true;
  if (GET_CODE (op) == SYMBOL_REF)
    return ((SYMBOL_REF_FLAGS (op) & SYMBOL_FLAG_ALIGN1) == 0
	    && SYMBOL_REF_TLS_MODEL (op) == 0
	    && (!flag_pic || SYMBOL_REF_LOCAL_P (op)));

  /* Everything else must have a CONST, so strip it.  */
  if (GET_CODE (op) != CONST)
    return false;
  op = XEXP (op, 0);

  /* Allow adding *even* in-range constants.  */
  if (GET_CODE (op) == PLUS)
    {
      if (GET_CODE (XEXP (op, 1)) != CONST_INT
          || (INTVAL (XEXP (op, 1)) & 1) != 0)
        return false;
      if (INTVAL (XEXP (op, 1)) >= (HOST_WIDE_INT)1 << 31
	  || INTVAL (XEXP (op, 1)) < -((HOST_WIDE_INT)1 << 31))
        return false;
      op = XEXP (op, 0);
    }

  /* Labels and local symbols allowed here as well.  */
  if (GET_CODE (op) == LABEL_REF)
    return true;
  if (GET_CODE (op) == SYMBOL_REF)
    return ((SYMBOL_REF_FLAGS (op) & SYMBOL_FLAG_ALIGN1) == 0
	    && SYMBOL_REF_TLS_MODEL (op) == 0
	    && (!flag_pic || SYMBOL_REF_LOCAL_P (op)));

  /* Now we must have a @GOTENT offset or @PLT stub
     or an @INDNTPOFF TLS offset.  */
  if (GET_CODE (op) == UNSPEC
      && XINT (op, 1) == UNSPEC_GOTENT)
    return true;
  if (GET_CODE (op) == UNSPEC
      && XINT (op, 1) == UNSPEC_PLT)
    return true;
  if (GET_CODE (op) == UNSPEC
      && XINT (op, 1) == UNSPEC_INDNTPOFF)
    return true;

  return false;
})

;; operators --------------------------------------------------------------

;; Return nonzero if OP is a valid comparison operator
;; for a branch condition.

(define_predicate "s390_comparison"
  (match_code "eq, ne, lt, gt, le, ge, ltu, gtu, leu, geu,
	       uneq, unlt, ungt, unle, unge, ltgt,
	       unordered, ordered")
{
  if (GET_CODE (XEXP (op, 0)) != REG
      || REGNO (XEXP (op, 0)) != CC_REGNUM
      || XEXP (op, 1) != const0_rtx)
    return false;

  return (s390_branch_condition_mask (op) >= 0);
})

;; Return nonzero if OP is a valid comparison operator
;; for an ALC condition.

(define_predicate "s390_alc_comparison"
  (match_code "zero_extend, sign_extend, ltu, gtu, leu, geu")
{
  while (GET_CODE (op) == ZERO_EXTEND || GET_CODE (op) == SIGN_EXTEND)
    op = XEXP (op, 0);

  if (!COMPARISON_P (op))
    return false;

  if (GET_CODE (XEXP (op, 0)) != REG
      || REGNO (XEXP (op, 0)) != CC_REGNUM
      || XEXP (op, 1) != const0_rtx)
    return false;

  switch (GET_MODE (XEXP (op, 0)))
    {
    case CCL1mode:
      return GET_CODE (op) == LTU;

    case CCL2mode:
      return GET_CODE (op) == LEU;

    case CCL3mode:
      return GET_CODE (op) == GEU;

    case CCUmode:
      return GET_CODE (op) == GTU;

    case CCURmode:
      return GET_CODE (op) == LTU;

    case CCSmode:
      return GET_CODE (op) == UNGT;

    case CCSRmode:
      return GET_CODE (op) == UNLT;

    default:
      return false;
    }
})

;; Return nonzero if OP is a valid comparison operator
;; for an SLB condition.

(define_predicate "s390_slb_comparison"
  (match_code "zero_extend, sign_extend, ltu, gtu, leu, geu")
{
  while (GET_CODE (op) == ZERO_EXTEND || GET_CODE (op) == SIGN_EXTEND)
    op = XEXP (op, 0);

  if (!COMPARISON_P (op))
    return false;

  if (GET_CODE (XEXP (op, 0)) != REG
      || REGNO (XEXP (op, 0)) != CC_REGNUM
      || XEXP (op, 1) != const0_rtx)
    return false;

  switch (GET_MODE (XEXP (op, 0)))
    {
    case CCL1mode:
      return GET_CODE (op) == GEU;

    case CCL2mode:
      return GET_CODE (op) == GTU;

    case CCL3mode:
      return GET_CODE (op) == LTU;

    case CCUmode:
      return GET_CODE (op) == LEU;

    case CCURmode:
      return GET_CODE (op) == GEU;

    case CCSmode:
      return GET_CODE (op) == LE;

    case CCSRmode:
      return GET_CODE (op) == GE;

    default:
      return false;
    }
})

;; Return true if OP is a load multiple operation.  It is known to be a
;; PARALLEL and the first section will be tested.

(define_special_predicate "load_multiple_operation"
  (match_code "parallel")
{
  enum machine_mode elt_mode;
  int count = XVECLEN (op, 0);
  unsigned int dest_regno;
  rtx src_addr;
  int i, off;

  /* Perform a quick check so we don't blow up below.  */
  if (count <= 1
      || GET_CODE (XVECEXP (op, 0, 0)) != SET
      || GET_CODE (SET_DEST (XVECEXP (op, 0, 0))) != REG
      || GET_CODE (SET_SRC (XVECEXP (op, 0, 0))) != MEM)
    return false;

  dest_regno = REGNO (SET_DEST (XVECEXP (op, 0, 0)));
  src_addr = XEXP (SET_SRC (XVECEXP (op, 0, 0)), 0);
  elt_mode = GET_MODE (SET_DEST (XVECEXP (op, 0, 0)));

  /* Check, is base, or base + displacement.  */

  if (GET_CODE (src_addr) == REG)
    off = 0;
  else if (GET_CODE (src_addr) == PLUS
	   && GET_CODE (XEXP (src_addr, 0)) == REG
	   && GET_CODE (XEXP (src_addr, 1)) == CONST_INT)
    {
      off = INTVAL (XEXP (src_addr, 1));
      src_addr = XEXP (src_addr, 0);
    }
  else
    return false;

  for (i = 1; i < count; i++)
    {
      rtx elt = XVECEXP (op, 0, i);

      if (GET_CODE (elt) != SET
	  || GET_CODE (SET_DEST (elt)) != REG
	  || GET_MODE (SET_DEST (elt)) != elt_mode
	  || REGNO (SET_DEST (elt)) != dest_regno + i
	  || GET_CODE (SET_SRC (elt)) != MEM
	  || GET_MODE (SET_SRC (elt)) != elt_mode
	  || GET_CODE (XEXP (SET_SRC (elt), 0)) != PLUS
	  || ! rtx_equal_p (XEXP (XEXP (SET_SRC (elt), 0), 0), src_addr)
	  || GET_CODE (XEXP (XEXP (SET_SRC (elt), 0), 1)) != CONST_INT
	  || INTVAL (XEXP (XEXP (SET_SRC (elt), 0), 1))
	     != off + i * GET_MODE_SIZE (elt_mode))
	return false;
    }

  return true;
})

;; Return true if OP is a store multiple operation.  It is known to be a
;; PARALLEL and the first section will be tested.

(define_special_predicate "store_multiple_operation"
  (match_code "parallel")
{
  enum machine_mode elt_mode;
  int count = XVECLEN (op, 0);
  unsigned int src_regno;
  rtx dest_addr;
  int i, off;

  /* Perform a quick check so we don't blow up below.  */
  if (count <= 1
      || GET_CODE (XVECEXP (op, 0, 0)) != SET
      || GET_CODE (SET_DEST (XVECEXP (op, 0, 0))) != MEM
      || GET_CODE (SET_SRC (XVECEXP (op, 0, 0))) != REG)
    return false;

  src_regno = REGNO (SET_SRC (XVECEXP (op, 0, 0)));
  dest_addr = XEXP (SET_DEST (XVECEXP (op, 0, 0)), 0);
  elt_mode = GET_MODE (SET_SRC (XVECEXP (op, 0, 0)));

  /* Check, is base, or base + displacement.  */

  if (GET_CODE (dest_addr) == REG)
    off = 0;
  else if (GET_CODE (dest_addr) == PLUS
	   && GET_CODE (XEXP (dest_addr, 0)) == REG
	   && GET_CODE (XEXP (dest_addr, 1)) == CONST_INT)
    {
      off = INTVAL (XEXP (dest_addr, 1));
      dest_addr = XEXP (dest_addr, 0);
    }
  else
    return false;

  for (i = 1; i < count; i++)
    {
      rtx elt = XVECEXP (op, 0, i);

      if (GET_CODE (elt) != SET
	  || GET_CODE (SET_SRC (elt)) != REG
	  || GET_MODE (SET_SRC (elt)) != elt_mode
	  || REGNO (SET_SRC (elt)) != src_regno + i
	  || GET_CODE (SET_DEST (elt)) != MEM
	  || GET_MODE (SET_DEST (elt)) != elt_mode
	  || GET_CODE (XEXP (SET_DEST (elt), 0)) != PLUS
	  || ! rtx_equal_p (XEXP (XEXP (SET_DEST (elt), 0), 0), dest_addr)
	  || GET_CODE (XEXP (XEXP (SET_DEST (elt), 0), 1)) != CONST_INT
	  || INTVAL (XEXP (XEXP (SET_DEST (elt), 0), 1))
	     != off + i * GET_MODE_SIZE (elt_mode))
	return false;
    }
  return true;
})
