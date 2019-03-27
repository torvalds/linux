;; MIPS Paired-Single Floating and MIPS-3D Instructions.
;; Copyright (C) 2004 Free Software Foundation, Inc.
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

(define_insn "*movcc_v2sf_<mode>"
  [(set (match_operand:V2SF 0 "register_operand" "=f,f")
	(if_then_else:V2SF
	 (match_operator:GPR 4 "equality_operator"
			 [(match_operand:GPR 1 "register_operand" "d,d")
			  (const_int 0)])
	 (match_operand:V2SF 2 "register_operand" "f,0")
	 (match_operand:V2SF 3 "register_operand" "0,f")))]
  "TARGET_PAIRED_SINGLE_FLOAT"
  "@
    mov%T4.ps\t%0,%2,%1
    mov%t4.ps\t%0,%3,%1"
  [(set_attr "type" "condmove")
   (set_attr "mode" "SF")])

(define_insn "mips_cond_move_tf_ps"
  [(set (match_operand:V2SF 0 "register_operand" "=f,f")
	(unspec:V2SF [(match_operand:V2SF 1 "register_operand" "f,0")
		      (match_operand:V2SF 2 "register_operand" "0,f")
		      (match_operand:CCV2 3 "register_operand" "z,z")]
		     UNSPEC_MOVE_TF_PS))]
  "TARGET_PAIRED_SINGLE_FLOAT"
  "@
    movt.ps\t%0,%1,%3
    movf.ps\t%0,%2,%3"
  [(set_attr "type" "condmove")
   (set_attr "mode" "SF")])

(define_expand "movv2sfcc"
  [(set (match_dup 4) (match_operand 1 "comparison_operator"))
   (set (match_operand:V2SF 0 "register_operand")
	(if_then_else:V2SF (match_dup 5)
			   (match_operand:V2SF 2 "register_operand")
			   (match_operand:V2SF 3 "register_operand")))]
  "TARGET_PAIRED_SINGLE_FLOAT"
{
  /* We can only support MOVN.PS and MOVZ.PS.
     NOTE: MOVT.PS and MOVF.PS have different semantics from MOVN.PS and 
	   MOVZ.PS.  MOVT.PS and MOVF.PS depend on two CC values and move 
	   each item independently.  */

  if (GET_MODE_CLASS (GET_MODE (cmp_operands[0])) != MODE_INT)
    FAIL;

  gen_conditional_move (operands);
  DONE;
})

; pul.ps - Pair Upper Lower
(define_insn "mips_pul_ps"
  [(set (match_operand:V2SF 0 "register_operand" "=f")
	(vec_merge:V2SF
	 (match_operand:V2SF 1 "register_operand" "f")
	 (match_operand:V2SF 2 "register_operand" "f")
	 (const_int 2)))]
  "TARGET_PAIRED_SINGLE_FLOAT"
  "pul.ps\t%0,%1,%2"
  [(set_attr "type" "fmove")
   (set_attr "mode" "SF")])

; puu.ps - Pair upper upper
(define_insn "mips_puu_ps"
  [(set (match_operand:V2SF 0 "register_operand" "=f")
	(vec_merge:V2SF
	 (match_operand:V2SF 1 "register_operand" "f")
	 (vec_select:V2SF (match_operand:V2SF 2 "register_operand" "f")
			  (parallel [(const_int 1)
				     (const_int 0)]))
	 (const_int 2)))]
  "TARGET_PAIRED_SINGLE_FLOAT"
  "puu.ps\t%0,%1,%2"
  [(set_attr "type" "fmove")
   (set_attr "mode" "SF")])

; pll.ps - Pair Lower Lower
(define_insn "mips_pll_ps"
  [(set (match_operand:V2SF 0 "register_operand" "=f")
	(vec_merge:V2SF
	 (vec_select:V2SF (match_operand:V2SF 1 "register_operand" "f")
			  (parallel [(const_int 1)
				     (const_int 0)]))
	 (match_operand:V2SF 2 "register_operand" "f")
	 (const_int 2)))]
  "TARGET_PAIRED_SINGLE_FLOAT"
  "pll.ps\t%0,%1,%2"
  [(set_attr "type" "fmove")
   (set_attr "mode" "SF")])

; plu.ps - Pair Lower Upper
(define_insn "mips_plu_ps"
  [(set (match_operand:V2SF 0 "register_operand" "=f")
	(vec_merge:V2SF
	 (vec_select:V2SF (match_operand:V2SF 1 "register_operand" "f")
			  (parallel [(const_int 1)
				     (const_int 0)]))
	 (vec_select:V2SF (match_operand:V2SF 2 "register_operand" "f")
			  (parallel [(const_int 1)
				     (const_int 0)]))
	 (const_int 2)))]
  "TARGET_PAIRED_SINGLE_FLOAT"
  "plu.ps\t%0,%1,%2"
  [(set_attr "type" "fmove")
   (set_attr "mode" "SF")])

; vec_init
(define_expand "vec_initv2sf"
  [(match_operand:V2SF 0 "register_operand")
   (match_operand:V2SF 1 "")]
  "TARGET_PAIRED_SINGLE_FLOAT"
{
  rtx op0 = force_reg (SFmode, XVECEXP (operands[1], 0, 0));
  rtx op1 = force_reg (SFmode, XVECEXP (operands[1], 0, 1));
  emit_insn (gen_vec_initv2sf_internal (operands[0], op0, op1));
  DONE;
})

(define_insn "vec_initv2sf_internal"
  [(set (match_operand:V2SF 0 "register_operand" "=f")
	(vec_concat:V2SF
	 (match_operand:SF 1 "register_operand" "f")
	 (match_operand:SF 2 "register_operand" "f")))]
  "TARGET_PAIRED_SINGLE_FLOAT"
{
  if (BYTES_BIG_ENDIAN)
    return "cvt.ps.s\t%0,%1,%2";
  else
    return "cvt.ps.s\t%0,%2,%1";
}
  [(set_attr "type" "fcvt")
   (set_attr "mode" "SF")])

;; ??? This is only generated if we perform a vector operation that has to be
;; emulated.  There is no other way to get a vector mode bitfield extract
;; currently.

(define_insn "vec_extractv2sf"
  [(set (match_operand:SF 0 "register_operand" "=f")
	(vec_select:SF (match_operand:V2SF 1 "register_operand" "f")
		       (parallel
			[(match_operand 2 "const_0_or_1_operand" "")])))]
  "TARGET_PAIRED_SINGLE_FLOAT"
{
  if (INTVAL (operands[2]) == !BYTES_BIG_ENDIAN)
    return "cvt.s.pu\t%0,%1";
  else
    return "cvt.s.pl\t%0,%1";
}
  [(set_attr "type" "fcvt")
   (set_attr "mode" "SF")])

;; ??? This is only generated if we disable the vec_init pattern.  There is
;; no other way to get a vector mode bitfield store currently.

(define_expand "vec_setv2sf"
  [(match_operand:V2SF 0 "register_operand")
   (match_operand:SF 1 "register_operand")
   (match_operand 2 "const_0_or_1_operand")]
  "TARGET_PAIRED_SINGLE_FLOAT"
{
  rtx temp;

  /* We don't have an insert instruction, so we duplicate the float, and
     then use a PUL instruction.  */
  temp = gen_reg_rtx (V2SFmode);
  emit_insn (gen_mips_cvt_ps_s (temp, operands[1], operands[1]));
  if (INTVAL (operands[2]) == !BYTES_BIG_ENDIAN)
    emit_insn (gen_mips_pul_ps (operands[0], temp, operands[0]));
  else
    emit_insn (gen_mips_pul_ps (operands[0], operands[0], temp));
  DONE;
})

; cvt.ps.s - Floating Point Convert Pair to Paired Single
(define_expand "mips_cvt_ps_s"
  [(match_operand:V2SF 0 "register_operand")
   (match_operand:SF 1 "register_operand")
   (match_operand:SF 2 "register_operand")]
  "TARGET_PAIRED_SINGLE_FLOAT"
{
  if (BYTES_BIG_ENDIAN)
    emit_insn (gen_vec_initv2sf_internal (operands[0], operands[1],
	       operands[2]));
  else
    emit_insn (gen_vec_initv2sf_internal (operands[0], operands[2],
	       operands[1]));
  DONE;
})

; cvt.s.pl - Floating Point Convert Pair Lower to Single Floating Point
(define_expand "mips_cvt_s_pl"
  [(set (match_operand:SF 0 "register_operand")
	(vec_select:SF (match_operand:V2SF 1 "register_operand")
		       (parallel [(match_dup 2)])))]
  "TARGET_PAIRED_SINGLE_FLOAT"
  { operands[2] = GEN_INT (BYTES_BIG_ENDIAN); })

; cvt.s.pu - Floating Point Convert Pair Upper to Single Floating Point
(define_expand "mips_cvt_s_pu"
  [(set (match_operand:SF 0 "register_operand")
	(vec_select:SF (match_operand:V2SF 1 "register_operand")
		       (parallel [(match_dup 2)])))]
  "TARGET_PAIRED_SINGLE_FLOAT"
  { operands[2] = GEN_INT (!BYTES_BIG_ENDIAN); })

; alnv.ps - Floating Point Align Variable
(define_insn "mips_alnv_ps"
  [(set (match_operand:V2SF 0 "register_operand" "=f")
	(unspec:V2SF [(match_operand:V2SF 1 "register_operand" "f")
		      (match_operand:V2SF 2 "register_operand" "f")
		      (match_operand:SI 3 "register_operand" "d")]
		     UNSPEC_ALNV_PS))]
  "TARGET_PAIRED_SINGLE_FLOAT"
  "alnv.ps\t%0,%1,%2,%3"
  [(set_attr "type" "fmove")
   (set_attr "mode" "SF")])

; addr.ps - Floating Point Reduction Add
(define_insn "mips_addr_ps"
  [(set (match_operand:V2SF 0 "register_operand" "=f")
	(unspec:V2SF [(match_operand:V2SF 1 "register_operand" "f")
		      (match_operand:V2SF 2 "register_operand" "f")]
		     UNSPEC_ADDR_PS))]
  "TARGET_MIPS3D"
  "addr.ps\t%0,%1,%2"
  [(set_attr "type" "fadd")
   (set_attr "mode" "SF")])

; cvt.pw.ps - Floating Point Convert Paired Single to Paired Word
(define_insn "mips_cvt_pw_ps"
  [(set (match_operand:V2SF 0 "register_operand" "=f")
	(unspec:V2SF [(match_operand:V2SF 1 "register_operand" "f")]
		     UNSPEC_CVT_PW_PS))]
  "TARGET_MIPS3D"
  "cvt.pw.ps\t%0,%1"
  [(set_attr "type" "fcvt")
   (set_attr "mode" "SF")])

; cvt.ps.pw - Floating Point Convert Paired Word to Paired Single
(define_insn "mips_cvt_ps_pw"
  [(set (match_operand:V2SF 0 "register_operand" "=f")
	(unspec:V2SF [(match_operand:V2SF 1 "register_operand" "f")]
		     UNSPEC_CVT_PS_PW))]
  "TARGET_MIPS3D"
  "cvt.ps.pw\t%0,%1"
  [(set_attr "type" "fcvt")
   (set_attr "mode" "SF")])

; mulr.ps - Floating Point Reduction Multiply
(define_insn "mips_mulr_ps"
  [(set (match_operand:V2SF 0 "register_operand" "=f")
	(unspec:V2SF [(match_operand:V2SF 1 "register_operand" "f")
		      (match_operand:V2SF 2 "register_operand" "f")]
		     UNSPEC_MULR_PS))]
  "TARGET_MIPS3D"
  "mulr.ps\t%0,%1,%2"
  [(set_attr "type" "fmul")
   (set_attr "mode" "SF")])

; abs.ps
(define_expand "mips_abs_ps"
  [(set (match_operand:V2SF 0 "register_operand")
	(unspec:V2SF [(match_operand:V2SF 1 "register_operand")]
		     UNSPEC_ABS_PS))]
  "TARGET_PAIRED_SINGLE_FLOAT"
{
  /* If we can ignore NaNs, this operation is equivalent to the
     rtl ABS code.  */
  if (!HONOR_NANS (V2SFmode))
    {
      emit_insn (gen_absv2sf2 (operands[0], operands[1]));
      DONE;
    }
})

(define_insn "*mips_abs_ps"
  [(set (match_operand:V2SF 0 "register_operand" "=f")
	(unspec:V2SF [(match_operand:V2SF 1 "register_operand" "f")]
		     UNSPEC_ABS_PS))]
  "TARGET_PAIRED_SINGLE_FLOAT"
  "abs.ps\t%0,%1"
  [(set_attr "type" "fabs")
   (set_attr "mode" "SF")])

;----------------------------------------------------------------------------
; Floating Point Comparisons for Scalars
;----------------------------------------------------------------------------

(define_insn "mips_cabs_cond_<fmt>"
  [(set (match_operand:CC 0 "register_operand" "=z")
	(unspec:CC [(match_operand:SCALARF 1 "register_operand" "f")
		    (match_operand:SCALARF 2 "register_operand" "f")
		    (match_operand 3 "const_int_operand" "")]
		   UNSPEC_CABS))]
  "TARGET_MIPS3D"
  "cabs.%Y3.<fmt>\t%0,%1,%2"
  [(set_attr "type" "fcmp")
   (set_attr "mode" "FPSW")])


;----------------------------------------------------------------------------
; Floating Point Comparisons for Four Singles
;----------------------------------------------------------------------------

(define_insn_and_split "mips_c_cond_4s"
  [(set (match_operand:CCV4 0 "register_operand" "=z")
	(unspec:CCV4 [(match_operand:V2SF 1 "register_operand" "f")
		      (match_operand:V2SF 2 "register_operand" "f")
		      (match_operand:V2SF 3 "register_operand" "f")
		      (match_operand:V2SF 4 "register_operand" "f")
		      (match_operand 5 "const_int_operand" "")]
		     UNSPEC_C))]
  "TARGET_PAIRED_SINGLE_FLOAT"
  "#"
  "&& reload_completed"
  [(set (match_dup 6)
	(unspec:CCV2 [(match_dup 1)
		      (match_dup 2)
		      (match_dup 5)]
		     UNSPEC_C))
   (set (match_dup 7)
	(unspec:CCV2 [(match_dup 3)
		      (match_dup 4)
		      (match_dup 5)]
		     UNSPEC_C))]
{
  operands[6] = simplify_gen_subreg (CCV2mode, operands[0], CCV4mode, 0);
  operands[7] = simplify_gen_subreg (CCV2mode, operands[0], CCV4mode, 8);
}
  [(set_attr "type" "fcmp")
   (set_attr "length" "8")
   (set_attr "mode" "FPSW")])

(define_insn_and_split "mips_cabs_cond_4s"
  [(set (match_operand:CCV4 0 "register_operand" "=z")
	(unspec:CCV4 [(match_operand:V2SF 1 "register_operand" "f")
		      (match_operand:V2SF 2 "register_operand" "f")
		      (match_operand:V2SF 3 "register_operand" "f")
		      (match_operand:V2SF 4 "register_operand" "f")
		      (match_operand 5 "const_int_operand" "")]
		     UNSPEC_CABS))]
  "TARGET_MIPS3D"
  "#"
  "&& reload_completed"
  [(set (match_dup 6)
	(unspec:CCV2 [(match_dup 1)
		      (match_dup 2)
		      (match_dup 5)]
		     UNSPEC_CABS))
   (set (match_dup 7)
	(unspec:CCV2 [(match_dup 3)
		      (match_dup 4)
		      (match_dup 5)]
		     UNSPEC_CABS))]
{
  operands[6] = simplify_gen_subreg (CCV2mode, operands[0], CCV4mode, 0);
  operands[7] = simplify_gen_subreg (CCV2mode, operands[0], CCV4mode, 8);
}
  [(set_attr "type" "fcmp")
   (set_attr "length" "8")
   (set_attr "mode" "FPSW")])


;----------------------------------------------------------------------------
; Floating Point Comparisons for Paired Singles
;----------------------------------------------------------------------------

(define_insn "mips_c_cond_ps"
  [(set (match_operand:CCV2 0 "register_operand" "=z")
	(unspec:CCV2 [(match_operand:V2SF 1 "register_operand" "f")
		      (match_operand:V2SF 2 "register_operand" "f")
		      (match_operand 3 "const_int_operand" "")]
		     UNSPEC_C))]
  "TARGET_PAIRED_SINGLE_FLOAT"
  "c.%Y3.ps\t%0,%1,%2"
  [(set_attr "type" "fcmp")
   (set_attr "mode" "FPSW")])

(define_insn "mips_cabs_cond_ps"
  [(set (match_operand:CCV2 0 "register_operand" "=z")
	(unspec:CCV2 [(match_operand:V2SF 1 "register_operand" "f")
		      (match_operand:V2SF 2 "register_operand" "f")
		      (match_operand 3 "const_int_operand" "")]
		     UNSPEC_CABS))]
  "TARGET_MIPS3D"
  "cabs.%Y3.ps\t%0,%1,%2"
  [(set_attr "type" "fcmp")
   (set_attr "mode" "FPSW")])

;; An expander for generating an scc operation.
(define_expand "scc_ps"
  [(set (match_operand:CCV2 0)
	(unspec:CCV2 [(match_operand 1)] UNSPEC_SCC))])

(define_insn "s<code>_ps"
  [(set (match_operand:CCV2 0 "register_operand" "=z")
	(unspec:CCV2
	   [(fcond (match_operand:V2SF 1 "register_operand" "f")
		   (match_operand:V2SF 2 "register_operand" "f"))]
	   UNSPEC_SCC))]
  "TARGET_PAIRED_SINGLE_FLOAT"
  "c.<fcond>.ps\t%0,%1,%2"
  [(set_attr "type" "fcmp")
   (set_attr "mode" "FPSW")])

(define_insn "s<code>_ps"
  [(set (match_operand:CCV2 0 "register_operand" "=z")
	(unspec:CCV2
	   [(swapped_fcond (match_operand:V2SF 1 "register_operand" "f")
			   (match_operand:V2SF 2 "register_operand" "f"))]
	   UNSPEC_SCC))]
  "TARGET_PAIRED_SINGLE_FLOAT"
  "c.<swapped_fcond>.ps\t%0,%2,%1"
  [(set_attr "type" "fcmp")
   (set_attr "mode" "FPSW")])

;----------------------------------------------------------------------------
; Floating Point Branch Instructions.
;----------------------------------------------------------------------------

; Branch on Any of Four Floating Point Condition Codes True
(define_insn "bc1any4t"
  [(set (pc)
	(if_then_else (ne (match_operand:CCV4 0 "register_operand" "z")
			  (const_int 0))
		      (label_ref (match_operand 1 "" ""))
		      (pc)))]
  "TARGET_MIPS3D"
  "%*bc1any4t\t%0,%1%/"
  [(set_attr "type" "branch")
   (set_attr "mode" "none")])

; Branch on Any of Four Floating Point Condition Codes False
(define_insn "bc1any4f"
  [(set (pc)
	(if_then_else (ne (match_operand:CCV4 0 "register_operand" "z")
			  (const_int -1))
		      (label_ref (match_operand 1 "" ""))
		      (pc)))]
  "TARGET_MIPS3D"
  "%*bc1any4f\t%0,%1%/"
  [(set_attr "type" "branch")
   (set_attr "mode" "none")])

; Branch on Any of Two Floating Point Condition Codes True
(define_insn "bc1any2t"
  [(set (pc)
	(if_then_else (ne (match_operand:CCV2 0 "register_operand" "z")
			  (const_int 0))
		      (label_ref (match_operand 1 "" ""))
		      (pc)))]
  "TARGET_MIPS3D"
  "%*bc1any2t\t%0,%1%/"
  [(set_attr "type" "branch")
   (set_attr "mode" "none")])

; Branch on Any of Two Floating Point Condition Codes False
(define_insn "bc1any2f"
  [(set (pc)
	(if_then_else (ne (match_operand:CCV2 0 "register_operand" "z")
			  (const_int -1))
		      (label_ref (match_operand 1 "" ""))
		      (pc)))]
  "TARGET_MIPS3D"
  "%*bc1any2f\t%0,%1%/"
  [(set_attr "type" "branch")
   (set_attr "mode" "none")])

; Used to access one register in a CCV2 pair.  Operand 0 is the register
; pair and operand 1 is the index of the register we want (a CONST_INT).
(define_expand "single_cc"
  [(ne (unspec:CC [(match_operand 0) (match_operand 1)] UNSPEC_SINGLE_CC)
       (const_int 0))])

; This is a normal floating-point branch pattern, but rather than check
; a single CCmode register, it checks one register in a CCV2 pair.
; Operand 2 is the register pair and operand 3 is the index of the
; register we want.
(define_insn "*branch_upper_lower"
  [(set (pc)
        (if_then_else
	 (match_operator 0 "equality_operator"
	    [(unspec:CC [(match_operand:CCV2 2 "register_operand" "z")
			 (match_operand 3 "const_int_operand")]
			UNSPEC_SINGLE_CC)
	     (const_int 0)])
	 (label_ref (match_operand 1 "" ""))
	 (pc)))]
  "TARGET_HARD_FLOAT"
{
  operands[2]
    = gen_rtx_REG (CCmode, REGNO (operands[2]) + INTVAL (operands[3]));
  return mips_output_conditional_branch (insn, operands,
					 MIPS_BRANCH ("b%F0", "%2,%1"),
					 MIPS_BRANCH ("b%W0", "%2,%1"));
}
  [(set_attr "type" "branch")
   (set_attr "mode" "none")])

; As above, but with the sense of the condition reversed.
(define_insn "*branch_upper_lower_inverted"
  [(set (pc)
        (if_then_else
	 (match_operator 0 "equality_operator"
	    [(unspec:CC [(match_operand:CCV2 2 "register_operand" "z")
			 (match_operand 3 "const_int_operand")]
			UNSPEC_SINGLE_CC)
	     (const_int 0)])
	 (pc)
	 (label_ref (match_operand 1 "" ""))))]
  "TARGET_HARD_FLOAT"
{
  operands[2]
    = gen_rtx_REG (CCmode, REGNO (operands[2]) + INTVAL (operands[3]));
  return mips_output_conditional_branch (insn, operands,
					 MIPS_BRANCH ("b%W0", "%2,%1"),
					 MIPS_BRANCH ("b%F0", "%2,%1"));
}
  [(set_attr "type" "branch")
   (set_attr "mode" "none")])

;----------------------------------------------------------------------------
; Floating Point Reduced Precision Reciprocal Square Root Instructions.
;----------------------------------------------------------------------------

(define_insn "mips_rsqrt1_<fmt>"
  [(set (match_operand:ANYF 0 "register_operand" "=f")
	(unspec:ANYF [(match_operand:ANYF 1 "register_operand" "f")]
		     UNSPEC_RSQRT1))]
  "TARGET_MIPS3D"
  "rsqrt1.<fmt>\t%0,%1"
  [(set_attr "type" "frsqrt1")
   (set_attr "mode" "<UNITMODE>")])

(define_insn "mips_rsqrt2_<fmt>"
  [(set (match_operand:ANYF 0 "register_operand" "=f")
	(unspec:ANYF [(match_operand:ANYF 1 "register_operand" "f")
		      (match_operand:ANYF 2 "register_operand" "f")]
		     UNSPEC_RSQRT2))]
  "TARGET_MIPS3D"
  "rsqrt2.<fmt>\t%0,%1,%2"
  [(set_attr "type" "frsqrt2")
   (set_attr "mode" "<UNITMODE>")])

(define_insn "mips_recip1_<fmt>"
  [(set (match_operand:ANYF 0 "register_operand" "=f")
	(unspec:ANYF [(match_operand:ANYF 1 "register_operand" "f")]
		     UNSPEC_RECIP1))]
  "TARGET_MIPS3D"
  "recip1.<fmt>\t%0,%1"
  [(set_attr "type" "frdiv1")
   (set_attr "mode" "<UNITMODE>")])

(define_insn "mips_recip2_<fmt>"
  [(set (match_operand:ANYF 0 "register_operand" "=f")
	(unspec:ANYF [(match_operand:ANYF 1 "register_operand" "f")
		      (match_operand:ANYF 2 "register_operand" "f")]
		     UNSPEC_RECIP2))]
  "TARGET_MIPS3D"
  "recip2.<fmt>\t%0,%1,%2"
  [(set_attr "type" "frdiv2")
   (set_attr "mode" "<UNITMODE>")])

(define_expand "vcondv2sf"
  [(set (match_operand:V2SF 0 "register_operand")
	(if_then_else:V2SF
	  (match_operator 3 ""
	    [(match_operand:V2SF 4 "register_operand")
	     (match_operand:V2SF 5 "register_operand")])
	  (match_operand:V2SF 1 "register_operand")
	  (match_operand:V2SF 2 "register_operand")))]
  "TARGET_PAIRED_SINGLE_FLOAT"
{
  mips_expand_vcondv2sf (operands[0], operands[1], operands[2],
			 GET_CODE (operands[3]), operands[4], operands[5]);
  DONE;
})

(define_expand "sminv2sf3"
  [(set (match_operand:V2SF 0 "register_operand")
	(smin:V2SF (match_operand:V2SF 1 "register_operand")
		   (match_operand:V2SF 2 "register_operand")))]
  "TARGET_PAIRED_SINGLE_FLOAT"
{
  mips_expand_vcondv2sf (operands[0], operands[1], operands[2],
			 LE, operands[1], operands[2]);
  DONE;
})

(define_expand "smaxv2sf3"
  [(set (match_operand:V2SF 0 "register_operand")
	(smax:V2SF (match_operand:V2SF 1 "register_operand")
		   (match_operand:V2SF 2 "register_operand")))]
  "TARGET_PAIRED_SINGLE_FLOAT"
{
  mips_expand_vcondv2sf (operands[0], operands[1], operands[2],
			 LE, operands[2], operands[1]);
  DONE;
})
