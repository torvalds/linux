;; GCC machine description for MMX and 3dNOW! instructions
;; Copyright (C) 2005
;; Free Software Foundation, Inc.
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

;; The MMX and 3dNOW! patterns are in the same file because they use
;; the same register file, and 3dNOW! adds a number of extensions to
;; the base integer MMX isa.

;; Note!  Except for the basic move instructions, *all* of these 
;; patterns are outside the normal optabs namespace.  This is because
;; use of these registers requires the insertion of emms or femms
;; instructions to return to normal fpu mode.  The compiler doesn't
;; know how to do that itself, which means it's up to the user.  Which
;; means that we should never use any of these patterns except at the
;; direction of the user via a builtin.

;; 8 byte integral modes handled by MMX (and by extension, SSE)
(define_mode_macro MMXMODEI [V8QI V4HI V2SI])

;; All 8-byte vector modes handled by MMX
(define_mode_macro MMXMODE [V8QI V4HI V2SI V2SF])

;; Mix-n-match
(define_mode_macro MMXMODE12 [V8QI V4HI])
(define_mode_macro MMXMODE24 [V4HI V2SI])

;; Mapping from integer vector mode to mnemonic suffix
(define_mode_attr mmxvecsize [(V8QI "b") (V4HI "w") (V2SI "d") (DI "q")])

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;
;; Move patterns
;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

;; All of these patterns are enabled for MMX as well as 3dNOW.
;; This is essential for maintaining stable calling conventions.

(define_expand "mov<mode>"
  [(set (match_operand:MMXMODEI 0 "nonimmediate_operand" "")
	(match_operand:MMXMODEI 1 "nonimmediate_operand" ""))]
  "TARGET_MMX"
{
  ix86_expand_vector_move (<MODE>mode, operands);
  DONE;
})

(define_insn "*mov<mode>_internal_rex64"
  [(set (match_operand:MMXMODEI 0 "nonimmediate_operand"
				"=rm,r,*y,*y ,m ,*y,Y ,x,x ,m,r,x")
	(match_operand:MMXMODEI 1 "vector_move_operand"
				"Cr ,m,C ,*ym,*y,Y ,*y,C,xm,x,x,r"))]
  "TARGET_64BIT && TARGET_MMX
   && (GET_CODE (operands[0]) != MEM || GET_CODE (operands[1]) != MEM)"
  "@
    movq\t{%1, %0|%0, %1}
    movq\t{%1, %0|%0, %1}
    pxor\t%0, %0
    movq\t{%1, %0|%0, %1}
    movq\t{%1, %0|%0, %1}
    movdq2q\t{%1, %0|%0, %1}
    movq2dq\t{%1, %0|%0, %1}
    pxor\t%0, %0
    movq\t{%1, %0|%0, %1}
    movq\t{%1, %0|%0, %1}
    movd\t{%1, %0|%0, %1}
    movd\t{%1, %0|%0, %1}"
  [(set_attr "type" "imov,imov,mmx,mmxmov,mmxmov,ssecvt,ssecvt,sselog1,ssemov,ssemov,ssemov,ssemov")
   (set_attr "unit" "*,*,*,*,*,mmx,mmx,*,*,*,*,*")
   (set_attr "mode" "DI")])

(define_insn "*mov<mode>_internal"
  [(set (match_operand:MMXMODEI 0 "nonimmediate_operand"
			"=*y,*y ,m ,*y,*Y,*Y,*Y ,m ,*x,*x,*x,m ,?r ,?m")
	(match_operand:MMXMODEI 1 "vector_move_operand"
			"C  ,*ym,*y,*Y,*y,C ,*Ym,*Y,C ,*x,m ,*x,irm,r"))]
  "TARGET_MMX
   && (GET_CODE (operands[0]) != MEM || GET_CODE (operands[1]) != MEM)"
  "@
    pxor\t%0, %0
    movq\t{%1, %0|%0, %1}
    movq\t{%1, %0|%0, %1}
    movdq2q\t{%1, %0|%0, %1}
    movq2dq\t{%1, %0|%0, %1}
    pxor\t%0, %0
    movq\t{%1, %0|%0, %1}
    movq\t{%1, %0|%0, %1}
    xorps\t%0, %0
    movaps\t{%1, %0|%0, %1}
    movlps\t{%1, %0|%0, %1}
    movlps\t{%1, %0|%0, %1}
    #
    #"
  [(set_attr "type" "mmx,mmxmov,mmxmov,ssecvt,ssecvt,sselog1,ssemov,ssemov,sselog1,ssemov,ssemov,ssemov,*,*")
   (set_attr "unit" "*,*,*,mmx,mmx,*,*,*,*,*,*,*,*,*")
   (set_attr "mode" "DI,DI,DI,DI,DI,TI,DI,DI,V4SF,V4SF,V2SF,V2SF,DI,DI")])

(define_expand "movv2sf"
  [(set (match_operand:V2SF 0 "nonimmediate_operand" "")
	(match_operand:V2SF 1 "nonimmediate_operand" ""))]
  "TARGET_MMX"
{
  ix86_expand_vector_move (V2SFmode, operands);
  DONE;
})

(define_insn "*movv2sf_internal_rex64"
  [(set (match_operand:V2SF 0 "nonimmediate_operand"
				"=rm,r,*y ,*y ,m ,*y,Y ,x,x,x,m,r,x")
        (match_operand:V2SF 1 "vector_move_operand"
				"Cr ,m ,C ,*ym,*y,Y ,*y,C,x,m,x,x,r"))]
  "TARGET_64BIT && TARGET_MMX
   && (GET_CODE (operands[0]) != MEM || GET_CODE (operands[1]) != MEM)"
  "@
    movq\t{%1, %0|%0, %1}
    movq\t{%1, %0|%0, %1}
    pxor\t%0, %0
    movq\t{%1, %0|%0, %1}
    movq\t{%1, %0|%0, %1}
    movdq2q\t{%1, %0|%0, %1}
    movq2dq\t{%1, %0|%0, %1}
    xorps\t%0, %0
    movaps\t{%1, %0|%0, %1}
    movlps\t{%1, %0|%0, %1}
    movlps\t{%1, %0|%0, %1}
    movd\t{%1, %0|%0, %1}
    movd\t{%1, %0|%0, %1}"
  [(set_attr "type" "imov,imov,mmx,mmxmov,mmxmov,ssecvt,ssecvt,ssemov,sselog1,ssemov,ssemov,ssemov,ssemov")
   (set_attr "unit" "*,*,*,*,*,mmx,mmx,*,*,*,*,*,*")
   (set_attr "mode" "DI,DI,DI,DI,DI,DI,DI,V4SF,V4SF,V2SF,V2SF,DI,DI")])

(define_insn "*movv2sf_internal"
  [(set (match_operand:V2SF 0 "nonimmediate_operand"
					"=*y,*y ,m,*y,*Y,*x,*x,*x,m ,?r ,?m")
        (match_operand:V2SF 1 "vector_move_operand"
					"C ,*ym,*y,*Y,*y,C ,*x,m ,*x,irm,r"))]
  "TARGET_MMX
   && (GET_CODE (operands[0]) != MEM || GET_CODE (operands[1]) != MEM)"
  "@
    pxor\t%0, %0
    movq\t{%1, %0|%0, %1}
    movq\t{%1, %0|%0, %1}
    movdq2q\t{%1, %0|%0, %1}
    movq2dq\t{%1, %0|%0, %1}
    xorps\t%0, %0
    movaps\t{%1, %0|%0, %1}
    movlps\t{%1, %0|%0, %1}
    movlps\t{%1, %0|%0, %1}
    #
    #"
  [(set_attr "type" "mmx,mmxmov,mmxmov,ssecvt,ssecvt,sselog1,ssemov,ssemov,ssemov,*,*")
   (set_attr "unit" "*,*,*,mmx,mmx,*,*,*,*,*,*")
   (set_attr "mode" "DI,DI,DI,DI,DI,V4SF,V4SF,V2SF,V2SF,DI,DI")])

;; %%% This multiword shite has got to go.
(define_split
  [(set (match_operand:MMXMODE 0 "nonimmediate_operand" "")
        (match_operand:MMXMODE 1 "general_operand" ""))]
  "!TARGET_64BIT && reload_completed
   && (!MMX_REG_P (operands[0]) && !SSE_REG_P (operands[0]))
   && (!MMX_REG_P (operands[1]) && !SSE_REG_P (operands[1]))"
  [(const_int 0)]
  "ix86_split_long_move (operands); DONE;")

(define_expand "push<mode>1"
  [(match_operand:MMXMODE 0 "register_operand" "")]
  "TARGET_MMX"
{
  ix86_expand_push (<MODE>mode, operands[0]);
  DONE;
})

(define_expand "movmisalign<mode>"
  [(set (match_operand:MMXMODE 0 "nonimmediate_operand" "")
	(match_operand:MMXMODE 1 "nonimmediate_operand" ""))]
  "TARGET_MMX"
{
  ix86_expand_vector_move (<MODE>mode, operands);
  DONE;
})

(define_insn "sse_movntdi"
  [(set (match_operand:DI 0 "memory_operand" "=m")
	(unspec:DI [(match_operand:DI 1 "register_operand" "y")]
		   UNSPEC_MOVNT))]
  "TARGET_SSE || TARGET_3DNOW_A"
  "movntq\t{%1, %0|%0, %1}"
  [(set_attr "type" "mmxmov")
   (set_attr "mode" "DI")])

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;
;; Parallel single-precision floating point arithmetic
;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(define_insn "mmx_addv2sf3"
  [(set (match_operand:V2SF 0 "register_operand" "=y")
	(plus:V2SF (match_operand:V2SF 1 "nonimmediate_operand" "%0")
		   (match_operand:V2SF 2 "nonimmediate_operand" "ym")))]
  "TARGET_3DNOW && ix86_binary_operator_ok (PLUS, V2SFmode, operands)"
  "pfadd\\t{%2, %0|%0, %2}"
  [(set_attr "type" "mmxadd")
   (set_attr "mode" "V2SF")])

(define_insn "mmx_subv2sf3"
  [(set (match_operand:V2SF 0 "register_operand" "=y,y")
        (minus:V2SF (match_operand:V2SF 1 "nonimmediate_operand" "0,ym")
		    (match_operand:V2SF 2 "nonimmediate_operand" "ym,0")))]
  "TARGET_3DNOW && !(MEM_P (operands[0]) && MEM_P (operands[1]))"
  "@
   pfsub\\t{%2, %0|%0, %2}
   pfsubr\\t{%2, %0|%0, %2}"
  [(set_attr "type" "mmxadd")
   (set_attr "mode" "V2SF")])

(define_expand "mmx_subrv2sf3"
  [(set (match_operand:V2SF 0 "register_operand" "")
        (minus:V2SF (match_operand:V2SF 2 "nonimmediate_operand" "")
		    (match_operand:V2SF 1 "nonimmediate_operand" "")))]
  "TARGET_3DNOW && !(MEM_P (operands[0]) && MEM_P (operands[1]))"
  "")

(define_insn "mmx_mulv2sf3"
  [(set (match_operand:V2SF 0 "register_operand" "=y")
	(mult:V2SF (match_operand:V2SF 1 "nonimmediate_operand" "%0")
		   (match_operand:V2SF 2 "nonimmediate_operand" "ym")))]
  "TARGET_3DNOW && ix86_binary_operator_ok (MULT, V2SFmode, operands)"
  "pfmul\\t{%2, %0|%0, %2}"
  [(set_attr "type" "mmxmul")
   (set_attr "mode" "V2SF")])

(define_insn "mmx_smaxv2sf3"
  [(set (match_operand:V2SF 0 "register_operand" "=y")
        (smax:V2SF (match_operand:V2SF 1 "nonimmediate_operand" "%0")
                   (match_operand:V2SF 2 "nonimmediate_operand" "ym")))]
  "TARGET_3DNOW && ix86_binary_operator_ok (SMAX, V2SFmode, operands)"
  "pfmax\\t{%2, %0|%0, %2}"
  [(set_attr "type" "mmxadd")
   (set_attr "mode" "V2SF")])

(define_insn "mmx_sminv2sf3"
  [(set (match_operand:V2SF 0 "register_operand" "=y")
        (smin:V2SF (match_operand:V2SF 1 "nonimmediate_operand" "%0")
                   (match_operand:V2SF 2 "nonimmediate_operand" "ym")))]
  "TARGET_3DNOW && ix86_binary_operator_ok (SMIN, V2SFmode, operands)"
  "pfmin\\t{%2, %0|%0, %2}"
  [(set_attr "type" "mmxadd")
   (set_attr "mode" "V2SF")])

(define_insn "mmx_rcpv2sf2"
  [(set (match_operand:V2SF 0 "register_operand" "=y")
        (unspec:V2SF [(match_operand:V2SF 1 "nonimmediate_operand" "ym")]
		     UNSPEC_PFRCP))]
  "TARGET_3DNOW"
  "pfrcp\\t{%1, %0|%0, %1}"
  [(set_attr "type" "mmx")
   (set_attr "mode" "V2SF")])

(define_insn "mmx_rcpit1v2sf3"
  [(set (match_operand:V2SF 0 "register_operand" "=y")
	(unspec:V2SF [(match_operand:V2SF 1 "register_operand" "0")
		      (match_operand:V2SF 2 "nonimmediate_operand" "ym")]
		     UNSPEC_PFRCPIT1))]
  "TARGET_3DNOW"
  "pfrcpit1\\t{%2, %0|%0, %2}"
  [(set_attr "type" "mmx")
   (set_attr "mode" "V2SF")])

(define_insn "mmx_rcpit2v2sf3"
  [(set (match_operand:V2SF 0 "register_operand" "=y")
	(unspec:V2SF [(match_operand:V2SF 1 "register_operand" "0")
		      (match_operand:V2SF 2 "nonimmediate_operand" "ym")]
		     UNSPEC_PFRCPIT2))]
  "TARGET_3DNOW"
  "pfrcpit2\\t{%2, %0|%0, %2}"
  [(set_attr "type" "mmx")
   (set_attr "mode" "V2SF")])

(define_insn "mmx_rsqrtv2sf2"
  [(set (match_operand:V2SF 0 "register_operand" "=y")
	(unspec:V2SF [(match_operand:V2SF 1 "nonimmediate_operand" "ym")]
		     UNSPEC_PFRSQRT))]
  "TARGET_3DNOW"
  "pfrsqrt\\t{%1, %0|%0, %1}"
  [(set_attr "type" "mmx")
   (set_attr "mode" "V2SF")])
		
(define_insn "mmx_rsqit1v2sf3"
  [(set (match_operand:V2SF 0 "register_operand" "=y")
	(unspec:V2SF [(match_operand:V2SF 1 "register_operand" "0")
		      (match_operand:V2SF 2 "nonimmediate_operand" "ym")]
		     UNSPEC_PFRSQIT1))]
  "TARGET_3DNOW"
  "pfrsqit1\\t{%2, %0|%0, %2}"
  [(set_attr "type" "mmx")
   (set_attr "mode" "V2SF")])

(define_insn "mmx_haddv2sf3"
  [(set (match_operand:V2SF 0 "register_operand" "=y")
	(vec_concat:V2SF
	  (plus:SF
	    (vec_select:SF
	      (match_operand:V2SF 1 "register_operand" "0")
	      (parallel [(const_int  0)]))
	    (vec_select:SF (match_dup 1) (parallel [(const_int 1)])))
	  (plus:SF
            (vec_select:SF
	      (match_operand:V2SF 2 "nonimmediate_operand" "ym")
	      (parallel [(const_int  0)]))
	    (vec_select:SF (match_dup 2) (parallel [(const_int 1)])))))]
  "TARGET_3DNOW"
  "pfacc\\t{%2, %0|%0, %2}"
  [(set_attr "type" "mmxadd")
   (set_attr "mode" "V2SF")])

(define_insn "mmx_hsubv2sf3"
  [(set (match_operand:V2SF 0 "register_operand" "=y")
	(vec_concat:V2SF
	  (minus:SF
	    (vec_select:SF
	      (match_operand:V2SF 1 "register_operand" "0")
	      (parallel [(const_int  0)]))
	    (vec_select:SF (match_dup 1) (parallel [(const_int 1)])))
	  (minus:SF
            (vec_select:SF
	      (match_operand:V2SF 2 "nonimmediate_operand" "ym")
	      (parallel [(const_int  0)]))
	    (vec_select:SF (match_dup 2) (parallel [(const_int 1)])))))]
  "TARGET_3DNOW_A"
  "pfnacc\\t{%2, %0|%0, %2}"
  [(set_attr "type" "mmxadd")
   (set_attr "mode" "V2SF")])

(define_insn "mmx_addsubv2sf3"
  [(set (match_operand:V2SF 0 "register_operand" "=y")
        (vec_merge:V2SF
          (plus:V2SF
            (match_operand:V2SF 1 "register_operand" "0")
            (match_operand:V2SF 2 "nonimmediate_operand" "ym"))
          (minus:V2SF (match_dup 1) (match_dup 2))
          (const_int 1)))]
  "TARGET_3DNOW_A"
  "pfpnacc\\t{%2, %0|%0, %2}"
  [(set_attr "type" "mmxadd")
   (set_attr "mode" "V2SF")])

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;
;; Parallel single-precision floating point comparisons
;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(define_insn "mmx_gtv2sf3"
  [(set (match_operand:V2SI 0 "register_operand" "=y")
	(gt:V2SI (match_operand:V2SF 1 "register_operand" "0")
		 (match_operand:V2SF 2 "nonimmediate_operand" "ym")))]
  "TARGET_3DNOW"
  "pfcmpgt\\t{%2, %0|%0, %2}"
  [(set_attr "type" "mmxcmp")
   (set_attr "mode" "V2SF")])

(define_insn "mmx_gev2sf3"
  [(set (match_operand:V2SI 0 "register_operand" "=y")
	(ge:V2SI (match_operand:V2SF 1 "register_operand" "0")
		 (match_operand:V2SF 2 "nonimmediate_operand" "ym")))]
  "TARGET_3DNOW"
  "pfcmpge\\t{%2, %0|%0, %2}"
  [(set_attr "type" "mmxcmp")
   (set_attr "mode" "V2SF")])

(define_insn "mmx_eqv2sf3"
  [(set (match_operand:V2SI 0 "register_operand" "=y")
	(eq:V2SI (match_operand:V2SF 1 "nonimmediate_operand" "%0")
		 (match_operand:V2SF 2 "nonimmediate_operand" "ym")))]
  "TARGET_3DNOW && ix86_binary_operator_ok (EQ, V2SFmode, operands)"
  "pfcmpeq\\t{%2, %0|%0, %2}"
  [(set_attr "type" "mmxcmp")
   (set_attr "mode" "V2SF")])

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;
;; Parallel single-precision floating point conversion operations
;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(define_insn "mmx_pf2id"
  [(set (match_operand:V2SI 0 "register_operand" "=y")
	(fix:V2SI (match_operand:V2SF 1 "nonimmediate_operand" "ym")))]
  "TARGET_3DNOW"
  "pf2id\\t{%1, %0|%0, %1}"
  [(set_attr "type" "mmxcvt")
   (set_attr "mode" "V2SF")])

(define_insn "mmx_pf2iw"
  [(set (match_operand:V2SI 0 "register_operand" "=y")
	(sign_extend:V2SI
	  (ss_truncate:V2HI
	    (fix:V2SI
	      (match_operand:V2SF 1 "nonimmediate_operand" "ym")))))]
  "TARGET_3DNOW_A"
  "pf2iw\\t{%1, %0|%0, %1}"
  [(set_attr "type" "mmxcvt")
   (set_attr "mode" "V2SF")])

(define_insn "mmx_pi2fw"
  [(set (match_operand:V2SF 0 "register_operand" "=y")
	(float:V2SF
	  (sign_extend:V2SI
	    (truncate:V2HI
	      (match_operand:V2SI 1 "nonimmediate_operand" "ym")))))]
  "TARGET_3DNOW_A"
  "pi2fw\\t{%1, %0|%0, %1}"
  [(set_attr "type" "mmxcvt")
   (set_attr "mode" "V2SF")])

(define_insn "mmx_floatv2si2"
  [(set (match_operand:V2SF 0 "register_operand" "=y")
	(float:V2SF (match_operand:V2SI 1 "nonimmediate_operand" "ym")))]
  "TARGET_3DNOW"
  "pi2fd\\t{%1, %0|%0, %1}"
  [(set_attr "type" "mmxcvt")
   (set_attr "mode" "V2SF")])

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;
;; Parallel single-precision floating point element swizzling
;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(define_insn "mmx_pswapdv2sf2"
  [(set (match_operand:V2SF 0 "register_operand" "=y")
	(vec_select:V2SF (match_operand:V2SF 1 "nonimmediate_operand" "ym")
			 (parallel [(const_int 1) (const_int 0)])))]
  "TARGET_3DNOW_A"
  "pswapd\\t{%1, %0|%0, %1}"
  [(set_attr "type" "mmxcvt")
   (set_attr "mode" "V2SF")])

(define_insn "*vec_dupv2sf"
  [(set (match_operand:V2SF 0 "register_operand" "=y")
	(vec_duplicate:V2SF
	  (match_operand:SF 1 "register_operand" "0")))]
  "TARGET_MMX"
  "punpckldq\t%0, %0"
  [(set_attr "type" "mmxcvt")
   (set_attr "mode" "DI")])

(define_insn "*mmx_concatv2sf"
  [(set (match_operand:V2SF 0 "register_operand"     "=y,y")
	(vec_concat:V2SF
	  (match_operand:SF 1 "nonimmediate_operand" " 0,rm")
	  (match_operand:SF 2 "vector_move_operand"  "ym,C")))]
  "TARGET_MMX && !TARGET_SSE"
  "@
   punpckldq\t{%2, %0|%0, %2}
   movd\t{%1, %0|%0, %1}"
  [(set_attr "type" "mmxcvt,mmxmov")
   (set_attr "mode" "DI")])

(define_expand "vec_setv2sf"
  [(match_operand:V2SF 0 "register_operand" "")
   (match_operand:SF 1 "register_operand" "")
   (match_operand 2 "const_int_operand" "")]
  "TARGET_MMX"
{
  ix86_expand_vector_set (false, operands[0], operands[1],
			  INTVAL (operands[2]));
  DONE;
})

(define_insn_and_split "*vec_extractv2sf_0"
  [(set (match_operand:SF 0 "nonimmediate_operand"     "=x,y,m,m,frxy")
	(vec_select:SF
	  (match_operand:V2SF 1 "nonimmediate_operand" " x,y,x,y,m")
	  (parallel [(const_int 0)])))]
  "TARGET_MMX && !(MEM_P (operands[0]) && MEM_P (operands[1]))"
  "#"
  "&& reload_completed"
  [(const_int 0)]
{
  rtx op1 = operands[1];
  if (REG_P (op1))
    op1 = gen_rtx_REG (SFmode, REGNO (op1));
  else
    op1 = gen_lowpart (SFmode, op1);
  emit_move_insn (operands[0], op1);
  DONE;
})

(define_insn "*vec_extractv2sf_1"
  [(set (match_operand:SF 0 "nonimmediate_operand"     "=y,x,frxy")
	(vec_select:SF
	  (match_operand:V2SF 1 "nonimmediate_operand" " 0,0,o")
	  (parallel [(const_int 1)])))]
  "TARGET_MMX && !(MEM_P (operands[0]) && MEM_P (operands[1]))"
  "@
   punpckhdq\t%0, %0
   unpckhps\t%0, %0
   #"
  [(set_attr "type" "mmxcvt,sselog1,*")
   (set_attr "mode" "DI,V4SF,SI")])

(define_split
  [(set (match_operand:SF 0 "register_operand" "")
	(vec_select:SF
	  (match_operand:V2SF 1 "memory_operand" "")
	  (parallel [(const_int 1)])))]
  "TARGET_MMX && reload_completed"
  [(const_int 0)]
{
  operands[1] = adjust_address (operands[1], SFmode, 4);
  emit_move_insn (operands[0], operands[1]);
  DONE;
})

(define_expand "vec_extractv2sf"
  [(match_operand:SF 0 "register_operand" "")
   (match_operand:V2SF 1 "register_operand" "")
   (match_operand 2 "const_int_operand" "")]
  "TARGET_MMX"
{
  ix86_expand_vector_extract (false, operands[0], operands[1],
			      INTVAL (operands[2]));
  DONE;
})

(define_expand "vec_initv2sf"
  [(match_operand:V2SF 0 "register_operand" "")
   (match_operand 1 "" "")]
  "TARGET_SSE"
{
  ix86_expand_vector_init (false, operands[0], operands[1]);
  DONE;
})

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;
;; Parallel integral arithmetic
;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(define_insn "mmx_add<mode>3"
  [(set (match_operand:MMXMODEI 0 "register_operand" "=y")
        (plus:MMXMODEI
	  (match_operand:MMXMODEI 1 "nonimmediate_operand" "%0")
	  (match_operand:MMXMODEI 2 "nonimmediate_operand" "ym")))]
  "TARGET_MMX && ix86_binary_operator_ok (PLUS, <MODE>mode, operands)"
  "padd<mmxvecsize>\t{%2, %0|%0, %2}"
  [(set_attr "type" "mmxadd")
   (set_attr "mode" "DI")])

(define_insn "mmx_adddi3"
  [(set (match_operand:DI 0 "register_operand" "=y")
        (unspec:DI
	 [(plus:DI (match_operand:DI 1 "nonimmediate_operand" "%0")
		   (match_operand:DI 2 "nonimmediate_operand" "ym"))]
	 UNSPEC_NOP))]
  "TARGET_SSE2 && ix86_binary_operator_ok (PLUS, DImode, operands)"
  "paddq\t{%2, %0|%0, %2}"
  [(set_attr "type" "mmxadd")
   (set_attr "mode" "DI")])

(define_insn "mmx_ssadd<mode>3"
  [(set (match_operand:MMXMODE12 0 "register_operand" "=y")
        (ss_plus:MMXMODE12
	  (match_operand:MMXMODE12 1 "nonimmediate_operand" "%0")
	  (match_operand:MMXMODE12 2 "nonimmediate_operand" "ym")))]
  "TARGET_MMX"
  "padds<mmxvecsize>\t{%2, %0|%0, %2}"
  [(set_attr "type" "mmxadd")
   (set_attr "mode" "DI")])

(define_insn "mmx_usadd<mode>3"
  [(set (match_operand:MMXMODE12 0 "register_operand" "=y")
        (us_plus:MMXMODE12
	  (match_operand:MMXMODE12 1 "nonimmediate_operand" "%0")
	  (match_operand:MMXMODE12 2 "nonimmediate_operand" "ym")))]
  "TARGET_MMX"
  "paddus<mmxvecsize>\t{%2, %0|%0, %2}"
  [(set_attr "type" "mmxadd")
   (set_attr "mode" "DI")])

(define_insn "mmx_sub<mode>3"
  [(set (match_operand:MMXMODEI 0 "register_operand" "=y")
        (minus:MMXMODEI
	  (match_operand:MMXMODEI 1 "register_operand" "0")
	  (match_operand:MMXMODEI 2 "nonimmediate_operand" "ym")))]
  "TARGET_MMX"
  "psub<mmxvecsize>\t{%2, %0|%0, %2}"
  [(set_attr "type" "mmxadd")
   (set_attr "mode" "DI")])

(define_insn "mmx_subdi3"
  [(set (match_operand:DI 0 "register_operand" "=y")
        (unspec:DI
	 [(minus:DI (match_operand:DI 1 "register_operand" "0")
		    (match_operand:DI 2 "nonimmediate_operand" "ym"))]
	 UNSPEC_NOP))]
  "TARGET_SSE2"
  "psubq\t{%2, %0|%0, %2}"
  [(set_attr "type" "mmxadd")
   (set_attr "mode" "DI")])

(define_insn "mmx_sssub<mode>3"
  [(set (match_operand:MMXMODE12 0 "register_operand" "=y")
        (ss_minus:MMXMODE12
	  (match_operand:MMXMODE12 1 "register_operand" "0")
	  (match_operand:MMXMODE12 2 "nonimmediate_operand" "ym")))]
  "TARGET_MMX"
  "psubs<mmxvecsize>\t{%2, %0|%0, %2}"
  [(set_attr "type" "mmxadd")
   (set_attr "mode" "DI")])

(define_insn "mmx_ussub<mode>3"
  [(set (match_operand:MMXMODE12 0 "register_operand" "=y")
        (us_minus:MMXMODE12
	  (match_operand:MMXMODE12 1 "register_operand" "0")
	  (match_operand:MMXMODE12 2 "nonimmediate_operand" "ym")))]
  "TARGET_MMX"
  "psubus<mmxvecsize>\t{%2, %0|%0, %2}"
  [(set_attr "type" "mmxadd")
   (set_attr "mode" "DI")])

(define_insn "mmx_mulv4hi3"
  [(set (match_operand:V4HI 0 "register_operand" "=y")
        (mult:V4HI (match_operand:V4HI 1 "nonimmediate_operand" "%0")
		   (match_operand:V4HI 2 "nonimmediate_operand" "ym")))]
  "TARGET_MMX && ix86_binary_operator_ok (MULT, V4HImode, operands)"
  "pmullw\t{%2, %0|%0, %2}"
  [(set_attr "type" "mmxmul")
   (set_attr "mode" "DI")])

(define_insn "mmx_smulv4hi3_highpart"
  [(set (match_operand:V4HI 0 "register_operand" "=y")
	(truncate:V4HI
	 (lshiftrt:V4SI
	  (mult:V4SI (sign_extend:V4SI
		      (match_operand:V4HI 1 "nonimmediate_operand" "%0"))
		     (sign_extend:V4SI
		      (match_operand:V4HI 2 "nonimmediate_operand" "ym")))
	  (const_int 16))))]
  "TARGET_MMX && ix86_binary_operator_ok (MULT, V4HImode, operands)"
  "pmulhw\t{%2, %0|%0, %2}"
  [(set_attr "type" "mmxmul")
   (set_attr "mode" "DI")])

(define_insn "mmx_umulv4hi3_highpart"
  [(set (match_operand:V4HI 0 "register_operand" "=y")
	(truncate:V4HI
	 (lshiftrt:V4SI
	  (mult:V4SI (zero_extend:V4SI
		      (match_operand:V4HI 1 "nonimmediate_operand" "%0"))
		     (zero_extend:V4SI
		      (match_operand:V4HI 2 "nonimmediate_operand" "ym")))
	  (const_int 16))))]
  "(TARGET_SSE || TARGET_3DNOW_A)
   && ix86_binary_operator_ok (MULT, V4HImode, operands)"
  "pmulhuw\t{%2, %0|%0, %2}"
  [(set_attr "type" "mmxmul")
   (set_attr "mode" "DI")])

(define_insn "mmx_pmaddwd"
  [(set (match_operand:V2SI 0 "register_operand" "=y")
        (plus:V2SI
	  (mult:V2SI
	    (sign_extend:V2SI
	      (vec_select:V2HI
		(match_operand:V4HI 1 "nonimmediate_operand" "%0")
		(parallel [(const_int 0) (const_int 2)])))
	    (sign_extend:V2SI
	      (vec_select:V2HI
		(match_operand:V4HI 2 "nonimmediate_operand" "ym")
		(parallel [(const_int 0) (const_int 2)]))))
	  (mult:V2SI
	    (sign_extend:V2SI
	      (vec_select:V2HI (match_dup 1)
		(parallel [(const_int 1) (const_int 3)])))
	    (sign_extend:V2SI
	      (vec_select:V2HI (match_dup 2)
		(parallel [(const_int 1) (const_int 3)]))))))]
  "TARGET_MMX && ix86_binary_operator_ok (MULT, V4HImode, operands)"
  "pmaddwd\t{%2, %0|%0, %2}"
  [(set_attr "type" "mmxmul")
   (set_attr "mode" "DI")])

(define_insn "mmx_pmulhrwv4hi3"
  [(set (match_operand:V4HI 0 "register_operand" "=y")
	(truncate:V4HI
	  (lshiftrt:V4SI
	    (plus:V4SI
	      (mult:V4SI
	        (sign_extend:V4SI
		  (match_operand:V4HI 1 "nonimmediate_operand" "%0"))
	        (sign_extend:V4SI
		  (match_operand:V4HI 2 "nonimmediate_operand" "ym")))
	      (const_vector:V4SI [(const_int 32768) (const_int 32768)
				  (const_int 32768) (const_int 32768)]))
	    (const_int 16))))]
  "TARGET_3DNOW && ix86_binary_operator_ok (MULT, V4HImode, operands)"
  "pmulhrw\\t{%2, %0|%0, %2}"
  [(set_attr "type" "mmxmul")
   (set_attr "mode" "DI")])

(define_insn "sse2_umulsidi3"
  [(set (match_operand:DI 0 "register_operand" "=y")
        (mult:DI
	  (zero_extend:DI
	    (vec_select:SI
	      (match_operand:V2SI 1 "nonimmediate_operand" "%0")
	      (parallel [(const_int 0)])))
	  (zero_extend:DI
	    (vec_select:SI
	      (match_operand:V2SI 2 "nonimmediate_operand" "ym")
	      (parallel [(const_int 0)])))))]
  "TARGET_SSE2 && ix86_binary_operator_ok (MULT, V2SImode, operands)"
  "pmuludq\t{%2, %0|%0, %2}"
  [(set_attr "type" "mmxmul")
   (set_attr "mode" "DI")])

(define_insn "mmx_umaxv8qi3"
  [(set (match_operand:V8QI 0 "register_operand" "=y")
        (umax:V8QI (match_operand:V8QI 1 "nonimmediate_operand" "%0")
		   (match_operand:V8QI 2 "nonimmediate_operand" "ym")))]
  "(TARGET_SSE || TARGET_3DNOW_A)
   && ix86_binary_operator_ok (UMAX, V8QImode, operands)"
  "pmaxub\t{%2, %0|%0, %2}"
  [(set_attr "type" "mmxadd")
   (set_attr "mode" "DI")])

(define_insn "mmx_smaxv4hi3"
  [(set (match_operand:V4HI 0 "register_operand" "=y")
        (smax:V4HI (match_operand:V4HI 1 "nonimmediate_operand" "%0")
		   (match_operand:V4HI 2 "nonimmediate_operand" "ym")))]
  "(TARGET_SSE || TARGET_3DNOW_A)
   && ix86_binary_operator_ok (SMAX, V4HImode, operands)"
  "pmaxsw\t{%2, %0|%0, %2}"
  [(set_attr "type" "mmxadd")
   (set_attr "mode" "DI")])

(define_insn "mmx_uminv8qi3"
  [(set (match_operand:V8QI 0 "register_operand" "=y")
        (umin:V8QI (match_operand:V8QI 1 "nonimmediate_operand" "%0")
		   (match_operand:V8QI 2 "nonimmediate_operand" "ym")))]
  "(TARGET_SSE || TARGET_3DNOW_A)
   && ix86_binary_operator_ok (UMIN, V8QImode, operands)"
  "pminub\t{%2, %0|%0, %2}"
  [(set_attr "type" "mmxadd")
   (set_attr "mode" "DI")])

(define_insn "mmx_sminv4hi3"
  [(set (match_operand:V4HI 0 "register_operand" "=y")
        (smin:V4HI (match_operand:V4HI 1 "nonimmediate_operand" "%0")
		   (match_operand:V4HI 2 "nonimmediate_operand" "ym")))]
  "(TARGET_SSE || TARGET_3DNOW_A)
   && ix86_binary_operator_ok (SMIN, V4HImode, operands)"
  "pminsw\t{%2, %0|%0, %2}"
  [(set_attr "type" "mmxadd")
   (set_attr "mode" "DI")])

(define_insn "mmx_ashr<mode>3"
  [(set (match_operand:MMXMODE24 0 "register_operand" "=y")
        (ashiftrt:MMXMODE24
	  (match_operand:MMXMODE24 1 "register_operand" "0")
	  (match_operand:DI 2 "nonmemory_operand" "yi")))]
  "TARGET_MMX"
  "psra<mmxvecsize>\t{%2, %0|%0, %2}"
  [(set_attr "type" "mmxshft")
   (set_attr "mode" "DI")])

(define_insn "mmx_lshr<mode>3"
  [(set (match_operand:MMXMODE24 0 "register_operand" "=y")
        (lshiftrt:MMXMODE24
	  (match_operand:MMXMODE24 1 "register_operand" "0")
	  (match_operand:DI 2 "nonmemory_operand" "yi")))]
  "TARGET_MMX"
  "psrl<mmxvecsize>\t{%2, %0|%0, %2}"
  [(set_attr "type" "mmxshft")
   (set_attr "mode" "DI")])

(define_insn "mmx_lshrdi3"
  [(set (match_operand:DI 0 "register_operand" "=y")
        (unspec:DI
	  [(lshiftrt:DI (match_operand:DI 1 "register_operand" "0")
		       (match_operand:DI 2 "nonmemory_operand" "yi"))]
	  UNSPEC_NOP))]
  "TARGET_MMX"
  "psrlq\t{%2, %0|%0, %2}"
  [(set_attr "type" "mmxshft")
   (set_attr "mode" "DI")])

(define_insn "mmx_ashl<mode>3"
  [(set (match_operand:MMXMODE24 0 "register_operand" "=y")
        (ashift:MMXMODE24
	  (match_operand:MMXMODE24 1 "register_operand" "0")
	  (match_operand:DI 2 "nonmemory_operand" "yi")))]
  "TARGET_MMX"
  "psll<mmxvecsize>\t{%2, %0|%0, %2}"
  [(set_attr "type" "mmxshft")
   (set_attr "mode" "DI")])

(define_insn "mmx_ashldi3"
  [(set (match_operand:DI 0 "register_operand" "=y")
        (unspec:DI
	 [(ashift:DI (match_operand:DI 1 "register_operand" "0")
		     (match_operand:DI 2 "nonmemory_operand" "yi"))]
	 UNSPEC_NOP))]
  "TARGET_MMX"
  "psllq\t{%2, %0|%0, %2}"
  [(set_attr "type" "mmxshft")
   (set_attr "mode" "DI")])

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;
;; Parallel integral comparisons
;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(define_insn "mmx_eq<mode>3"
  [(set (match_operand:MMXMODEI 0 "register_operand" "=y")
        (eq:MMXMODEI
	  (match_operand:MMXMODEI 1 "nonimmediate_operand" "%0")
	  (match_operand:MMXMODEI 2 "nonimmediate_operand" "ym")))]
  "TARGET_MMX && ix86_binary_operator_ok (EQ, <MODE>mode, operands)"
  "pcmpeq<mmxvecsize>\t{%2, %0|%0, %2}"
  [(set_attr "type" "mmxcmp")
   (set_attr "mode" "DI")])

(define_insn "mmx_gt<mode>3"
  [(set (match_operand:MMXMODEI 0 "register_operand" "=y")
        (gt:MMXMODEI
	  (match_operand:MMXMODEI 1 "register_operand" "0")
	  (match_operand:MMXMODEI 2 "nonimmediate_operand" "ym")))]
  "TARGET_MMX"
  "pcmpgt<mmxvecsize>\t{%2, %0|%0, %2}"
  [(set_attr "type" "mmxcmp")
   (set_attr "mode" "DI")])

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;
;; Parallel integral logical operations
;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(define_insn "mmx_and<mode>3"
  [(set (match_operand:MMXMODEI 0 "register_operand" "=y")
	(and:MMXMODEI
	  (match_operand:MMXMODEI 1 "nonimmediate_operand" "%0")
	  (match_operand:MMXMODEI 2 "nonimmediate_operand" "ym")))]
  "TARGET_MMX && ix86_binary_operator_ok (AND, <MODE>mode, operands)"
  "pand\t{%2, %0|%0, %2}"
  [(set_attr "type" "mmxadd")
   (set_attr "mode" "DI")])

(define_insn "mmx_nand<mode>3"
  [(set (match_operand:MMXMODEI 0 "register_operand" "=y")
	(and:MMXMODEI
	  (not:MMXMODEI (match_operand:MMXMODEI 1 "register_operand" "0"))
	  (match_operand:MMXMODEI 2 "nonimmediate_operand" "ym")))]
  "TARGET_MMX"
  "pandn\t{%2, %0|%0, %2}"
  [(set_attr "type" "mmxadd")
   (set_attr "mode" "DI")])

(define_insn "mmx_ior<mode>3"
  [(set (match_operand:MMXMODEI 0 "register_operand" "=y")
        (ior:MMXMODEI
	  (match_operand:MMXMODEI 1 "nonimmediate_operand" "%0")
	  (match_operand:MMXMODEI 2 "nonimmediate_operand" "ym")))]
  "TARGET_MMX && ix86_binary_operator_ok (IOR, <MODE>mode, operands)"
  "por\t{%2, %0|%0, %2}"
  [(set_attr "type" "mmxadd")
   (set_attr "mode" "DI")])

(define_insn "mmx_xor<mode>3"
  [(set (match_operand:MMXMODEI 0 "register_operand" "=y")
	(xor:MMXMODEI
	  (match_operand:MMXMODEI 1 "nonimmediate_operand" "%0")
	  (match_operand:MMXMODEI 2 "nonimmediate_operand" "ym")))]
  "TARGET_MMX && ix86_binary_operator_ok (XOR, <MODE>mode, operands)"
  "pxor\t{%2, %0|%0, %2}"
  [(set_attr "type" "mmxadd")
   (set_attr "mode" "DI")
   (set_attr "memory" "none")])

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;
;; Parallel integral element swizzling
;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(define_insn "mmx_packsswb"
  [(set (match_operand:V8QI 0 "register_operand" "=y")
	(vec_concat:V8QI
	  (ss_truncate:V4QI
	    (match_operand:V4HI 1 "register_operand" "0"))
	  (ss_truncate:V4QI
	    (match_operand:V4HI 2 "nonimmediate_operand" "ym"))))]
  "TARGET_MMX"
  "packsswb\t{%2, %0|%0, %2}"
  [(set_attr "type" "mmxshft")
   (set_attr "mode" "DI")])

(define_insn "mmx_packssdw"
  [(set (match_operand:V4HI 0 "register_operand" "=y")
	(vec_concat:V4HI
	  (ss_truncate:V2HI
	    (match_operand:V2SI 1 "register_operand" "0"))
	  (ss_truncate:V2HI
	    (match_operand:V2SI 2 "nonimmediate_operand" "ym"))))]
  "TARGET_MMX"
  "packssdw\t{%2, %0|%0, %2}"
  [(set_attr "type" "mmxshft")
   (set_attr "mode" "DI")])

(define_insn "mmx_packuswb"
  [(set (match_operand:V8QI 0 "register_operand" "=y")
	(vec_concat:V8QI
	  (us_truncate:V4QI
	    (match_operand:V4HI 1 "register_operand" "0"))
	  (us_truncate:V4QI
	    (match_operand:V4HI 2 "nonimmediate_operand" "ym"))))]
  "TARGET_MMX"
  "packuswb\t{%2, %0|%0, %2}"
  [(set_attr "type" "mmxshft")
   (set_attr "mode" "DI")])

(define_insn "mmx_punpckhbw"
  [(set (match_operand:V8QI 0 "register_operand" "=y")
	(vec_select:V8QI
	  (vec_concat:V16QI
	    (match_operand:V8QI 1 "register_operand" "0")
	    (match_operand:V8QI 2 "nonimmediate_operand" "ym"))
          (parallel [(const_int 4) (const_int 12)
                     (const_int 5) (const_int 13)
                     (const_int 6) (const_int 14)
                     (const_int 7) (const_int 15)])))]
  "TARGET_MMX"
  "punpckhbw\t{%2, %0|%0, %2}"
  [(set_attr "type" "mmxcvt")
   (set_attr "mode" "DI")])

(define_insn "mmx_punpcklbw"
  [(set (match_operand:V8QI 0 "register_operand" "=y")
	(vec_select:V8QI
	  (vec_concat:V16QI
	    (match_operand:V8QI 1 "register_operand" "0")
	    (match_operand:V8QI 2 "nonimmediate_operand" "ym"))
          (parallel [(const_int 0) (const_int 8)
                     (const_int 1) (const_int 9)
                     (const_int 2) (const_int 10)
                     (const_int 3) (const_int 11)])))]
  "TARGET_MMX"
  "punpcklbw\t{%2, %0|%0, %2}"
  [(set_attr "type" "mmxcvt")
   (set_attr "mode" "DI")])

(define_insn "mmx_punpckhwd"
  [(set (match_operand:V4HI 0 "register_operand" "=y")
	(vec_select:V4HI
	  (vec_concat:V8HI
	    (match_operand:V4HI 1 "register_operand" "0")
	    (match_operand:V4HI 2 "nonimmediate_operand" "ym"))
          (parallel [(const_int 2) (const_int 6)
                     (const_int 3) (const_int 7)])))]
  "TARGET_MMX"
  "punpckhwd\t{%2, %0|%0, %2}"
  [(set_attr "type" "mmxcvt")
   (set_attr "mode" "DI")])

(define_insn "mmx_punpcklwd"
  [(set (match_operand:V4HI 0 "register_operand" "=y")
	(vec_select:V4HI
	  (vec_concat:V8HI
	    (match_operand:V4HI 1 "register_operand" "0")
	    (match_operand:V4HI 2 "nonimmediate_operand" "ym"))
          (parallel [(const_int 0) (const_int 4)
                     (const_int 1) (const_int 5)])))]
  "TARGET_MMX"
  "punpcklwd\t{%2, %0|%0, %2}"
  [(set_attr "type" "mmxcvt")
   (set_attr "mode" "DI")])

(define_insn "mmx_punpckhdq"
  [(set (match_operand:V2SI 0 "register_operand" "=y")
	(vec_select:V2SI
	  (vec_concat:V4SI
	    (match_operand:V2SI 1 "register_operand" "0")
	    (match_operand:V2SI 2 "nonimmediate_operand" "ym"))
	  (parallel [(const_int 1)
		     (const_int 3)])))]
  "TARGET_MMX"
  "punpckhdq\t{%2, %0|%0, %2}"
  [(set_attr "type" "mmxcvt")
   (set_attr "mode" "DI")])

(define_insn "mmx_punpckldq"
  [(set (match_operand:V2SI 0 "register_operand" "=y")
	(vec_select:V2SI
	  (vec_concat:V4SI
	    (match_operand:V2SI 1 "register_operand" "0")
	    (match_operand:V2SI 2 "nonimmediate_operand" "ym"))
	  (parallel [(const_int 0)
		     (const_int 2)])))]
  "TARGET_MMX"
  "punpckldq\t{%2, %0|%0, %2}"
  [(set_attr "type" "mmxcvt")
   (set_attr "mode" "DI")])

(define_expand "mmx_pinsrw"
  [(set (match_operand:V4HI 0 "register_operand" "")
        (vec_merge:V4HI
          (vec_duplicate:V4HI
            (match_operand:SI 2 "nonimmediate_operand" ""))
	  (match_operand:V4HI 1 "register_operand" "")
          (match_operand:SI 3 "const_0_to_3_operand" "")))]
  "TARGET_SSE || TARGET_3DNOW_A"
{
  operands[2] = gen_lowpart (HImode, operands[2]);
  operands[3] = GEN_INT (1 << INTVAL (operands[3]));
})

(define_insn "*mmx_pinsrw"
  [(set (match_operand:V4HI 0 "register_operand" "=y")
        (vec_merge:V4HI
          (vec_duplicate:V4HI
            (match_operand:HI 2 "nonimmediate_operand" "rm"))
	  (match_operand:V4HI 1 "register_operand" "0")
          (match_operand:SI 3 "const_pow2_1_to_8_operand" "n")))]
  "TARGET_SSE || TARGET_3DNOW_A"
{
  operands[3] = GEN_INT (exact_log2 (INTVAL (operands[3])));
  return "pinsrw\t{%3, %k2, %0|%0, %k2, %3}";
}
  [(set_attr "type" "mmxcvt")
   (set_attr "mode" "DI")])

(define_insn "mmx_pextrw"
  [(set (match_operand:SI 0 "register_operand" "=r")
        (zero_extend:SI
	  (vec_select:HI
	    (match_operand:V4HI 1 "register_operand" "y")
	    (parallel [(match_operand:SI 2 "const_0_to_3_operand" "n")]))))]
  "TARGET_SSE || TARGET_3DNOW_A"
  "pextrw\t{%2, %1, %0|%0, %1, %2}"
  [(set_attr "type" "mmxcvt")
   (set_attr "mode" "DI")])

(define_expand "mmx_pshufw"
  [(match_operand:V4HI 0 "register_operand" "")
   (match_operand:V4HI 1 "nonimmediate_operand" "")
   (match_operand:SI 2 "const_int_operand" "")]
  "TARGET_SSE || TARGET_3DNOW_A"
{
  int mask = INTVAL (operands[2]);
  emit_insn (gen_mmx_pshufw_1 (operands[0], operands[1],
                               GEN_INT ((mask >> 0) & 3),
                               GEN_INT ((mask >> 2) & 3),
                               GEN_INT ((mask >> 4) & 3),
                               GEN_INT ((mask >> 6) & 3)));
  DONE;
})

(define_insn "mmx_pshufw_1"
  [(set (match_operand:V4HI 0 "register_operand" "=y")
        (vec_select:V4HI
          (match_operand:V4HI 1 "nonimmediate_operand" "ym")
          (parallel [(match_operand 2 "const_0_to_3_operand" "")
                     (match_operand 3 "const_0_to_3_operand" "")
                     (match_operand 4 "const_0_to_3_operand" "")
                     (match_operand 5 "const_0_to_3_operand" "")])))]
  "TARGET_SSE || TARGET_3DNOW_A"
{
  int mask = 0;
  mask |= INTVAL (operands[2]) << 0;
  mask |= INTVAL (operands[3]) << 2;
  mask |= INTVAL (operands[4]) << 4;
  mask |= INTVAL (operands[5]) << 6;
  operands[2] = GEN_INT (mask);

  return "pshufw\t{%2, %1, %0|%0, %1, %2}";
}
  [(set_attr "type" "mmxcvt")
   (set_attr "mode" "DI")])

(define_insn "mmx_pswapdv2si2"
  [(set (match_operand:V2SI 0 "register_operand" "=y")
	(vec_select:V2SI
	  (match_operand:V2SI 1 "nonimmediate_operand" "ym")
	  (parallel [(const_int 1) (const_int 0)])))]
  "TARGET_3DNOW_A"
  "pswapd\\t{%1, %0|%0, %1}"
  [(set_attr "type" "mmxcvt")
   (set_attr "mode" "DI")])

(define_insn "*vec_dupv4hi"
  [(set (match_operand:V4HI 0 "register_operand" "=y")
	(vec_duplicate:V4HI
	  (truncate:HI
	    (match_operand:SI 1 "register_operand" "0"))))]
  "TARGET_SSE || TARGET_3DNOW_A"
  "pshufw\t{$0, %0, %0|%0, %0, 0}"
  [(set_attr "type" "mmxcvt")
   (set_attr "mode" "DI")])

(define_insn "*vec_dupv2si"
  [(set (match_operand:V2SI 0 "register_operand" "=y")
	(vec_duplicate:V2SI
	  (match_operand:SI 1 "register_operand" "0")))]
  "TARGET_MMX"
  "punpckldq\t%0, %0"
  [(set_attr "type" "mmxcvt")
   (set_attr "mode" "DI")])

(define_insn "*mmx_concatv2si"
  [(set (match_operand:V2SI 0 "register_operand"     "=y,y")
	(vec_concat:V2SI
	  (match_operand:SI 1 "nonimmediate_operand" " 0,rm")
	  (match_operand:SI 2 "vector_move_operand"  "ym,C")))]
  "TARGET_MMX && !TARGET_SSE"
  "@
   punpckldq\t{%2, %0|%0, %2}
   movd\t{%1, %0|%0, %1}"
  [(set_attr "type" "mmxcvt,mmxmov")
   (set_attr "mode" "DI")])

(define_expand "vec_setv2si"
  [(match_operand:V2SI 0 "register_operand" "")
   (match_operand:SI 1 "register_operand" "")
   (match_operand 2 "const_int_operand" "")]
  "TARGET_MMX"
{
  ix86_expand_vector_set (false, operands[0], operands[1],
			  INTVAL (operands[2]));
  DONE;
})

(define_insn_and_split "*vec_extractv2si_0"
  [(set (match_operand:SI 0 "nonimmediate_operand"     "=x,y,m,m,frxy")
	(vec_select:SI
	  (match_operand:V2SI 1 "nonimmediate_operand" " x,y,x,y,m")
	  (parallel [(const_int 0)])))]
  "TARGET_MMX && !(MEM_P (operands[0]) && MEM_P (operands[1]))"
  "#"
  "&& reload_completed"
  [(const_int 0)]
{
  rtx op1 = operands[1];
  if (REG_P (op1))
    op1 = gen_rtx_REG (SImode, REGNO (op1));
  else
    op1 = gen_lowpart (SImode, op1);
  emit_move_insn (operands[0], op1);
  DONE;
})

(define_insn "*vec_extractv2si_1"
  [(set (match_operand:SI 0 "nonimmediate_operand"     "=y,Y,Y,x,frxy")
	(vec_select:SI
	  (match_operand:V2SI 1 "nonimmediate_operand" " 0,0,Y,0,o")
	  (parallel [(const_int 1)])))]
  "TARGET_MMX && !(MEM_P (operands[0]) && MEM_P (operands[1]))"
  "@
   punpckhdq\t%0, %0
   punpckhdq\t%0, %0
   pshufd\t{$85, %1, %0|%0, %1, 85}
   unpckhps\t%0, %0
   #"
  [(set_attr "type" "mmxcvt,sselog1,sselog1,sselog1,*")
   (set_attr "mode" "DI,TI,TI,V4SF,SI")])

(define_split
  [(set (match_operand:SI 0 "register_operand" "")
	(vec_select:SI
	  (match_operand:V2SI 1 "memory_operand" "")
	  (parallel [(const_int 1)])))]
  "TARGET_MMX && reload_completed"
  [(const_int 0)]
{
  operands[1] = adjust_address (operands[1], SImode, 4);
  emit_move_insn (operands[0], operands[1]);
  DONE;
})

(define_expand "vec_extractv2si"
  [(match_operand:SI 0 "register_operand" "")
   (match_operand:V2SI 1 "register_operand" "")
   (match_operand 2 "const_int_operand" "")]
  "TARGET_MMX"
{
  ix86_expand_vector_extract (false, operands[0], operands[1],
			      INTVAL (operands[2]));
  DONE;
})

(define_expand "vec_initv2si"
  [(match_operand:V2SI 0 "register_operand" "")
   (match_operand 1 "" "")]
  "TARGET_SSE"
{
  ix86_expand_vector_init (false, operands[0], operands[1]);
  DONE;
})

(define_expand "vec_setv4hi"
  [(match_operand:V4HI 0 "register_operand" "")
   (match_operand:HI 1 "register_operand" "")
   (match_operand 2 "const_int_operand" "")]
  "TARGET_MMX"
{
  ix86_expand_vector_set (false, operands[0], operands[1],
			  INTVAL (operands[2]));
  DONE;
})

(define_expand "vec_extractv4hi"
  [(match_operand:HI 0 "register_operand" "")
   (match_operand:V4HI 1 "register_operand" "")
   (match_operand 2 "const_int_operand" "")]
  "TARGET_MMX"
{
  ix86_expand_vector_extract (false, operands[0], operands[1],
			      INTVAL (operands[2]));
  DONE;
})

(define_expand "vec_initv4hi"
  [(match_operand:V4HI 0 "register_operand" "")
   (match_operand 1 "" "")]
  "TARGET_SSE"
{
  ix86_expand_vector_init (false, operands[0], operands[1]);
  DONE;
})

(define_expand "vec_setv8qi"
  [(match_operand:V8QI 0 "register_operand" "")
   (match_operand:QI 1 "register_operand" "")
   (match_operand 2 "const_int_operand" "")]
  "TARGET_MMX"
{
  ix86_expand_vector_set (false, operands[0], operands[1],
			  INTVAL (operands[2]));
  DONE;
})

(define_expand "vec_extractv8qi"
  [(match_operand:QI 0 "register_operand" "")
   (match_operand:V8QI 1 "register_operand" "")
   (match_operand 2 "const_int_operand" "")]
  "TARGET_MMX"
{
  ix86_expand_vector_extract (false, operands[0], operands[1],
			      INTVAL (operands[2]));
  DONE;
})

(define_expand "vec_initv8qi"
  [(match_operand:V8QI 0 "register_operand" "")
   (match_operand 1 "" "")]
  "TARGET_SSE"
{
  ix86_expand_vector_init (false, operands[0], operands[1]);
  DONE;
})

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;
;; Miscellaneous
;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(define_insn "mmx_uavgv8qi3"
  [(set (match_operand:V8QI 0 "register_operand" "=y")
	(truncate:V8QI
	  (lshiftrt:V8HI
	    (plus:V8HI
	      (plus:V8HI
		(zero_extend:V8HI
		  (match_operand:V8QI 1 "nonimmediate_operand" "%0"))
		(zero_extend:V8HI
		  (match_operand:V8QI 2 "nonimmediate_operand" "ym")))
	      (const_vector:V8HI [(const_int 1) (const_int 1)
				  (const_int 1) (const_int 1)
				  (const_int 1) (const_int 1)
				  (const_int 1) (const_int 1)]))
	    (const_int 1))))]
  "(TARGET_SSE || TARGET_3DNOW)
   && ix86_binary_operator_ok (PLUS, V8QImode, operands)"
{
  /* These two instructions have the same operation, but their encoding
     is different.  Prefer the one that is de facto standard.  */
  if (TARGET_SSE || TARGET_3DNOW_A)
    return "pavgb\t{%2, %0|%0, %2}";
  else
    return "pavgusb\\t{%2, %0|%0, %2}";
}
  [(set_attr "type" "mmxshft")
   (set_attr "mode" "DI")])

(define_insn "mmx_uavgv4hi3"
  [(set (match_operand:V4HI 0 "register_operand" "=y")
	(truncate:V4HI
	  (lshiftrt:V4SI
	    (plus:V4SI
	      (plus:V4SI
		(zero_extend:V4SI
		  (match_operand:V4HI 1 "nonimmediate_operand" "%0"))
		(zero_extend:V4SI
		  (match_operand:V4HI 2 "nonimmediate_operand" "ym")))
	      (const_vector:V4SI [(const_int 1) (const_int 1)
				  (const_int 1) (const_int 1)]))
	    (const_int 1))))]
  "(TARGET_SSE || TARGET_3DNOW_A)
   && ix86_binary_operator_ok (PLUS, V4HImode, operands)"
  "pavgw\t{%2, %0|%0, %2}"
  [(set_attr "type" "mmxshft")
   (set_attr "mode" "DI")])

(define_insn "mmx_psadbw"
  [(set (match_operand:DI 0 "register_operand" "=y")
        (unspec:DI [(match_operand:V8QI 1 "register_operand" "0")
		    (match_operand:V8QI 2 "nonimmediate_operand" "ym")]
		   UNSPEC_PSADBW))]
  "TARGET_SSE || TARGET_3DNOW_A"
  "psadbw\t{%2, %0|%0, %2}"
  [(set_attr "type" "mmxshft")
   (set_attr "mode" "DI")])

(define_insn "mmx_pmovmskb"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(unspec:SI [(match_operand:V8QI 1 "register_operand" "y")]
		   UNSPEC_MOVMSK))]
  "TARGET_SSE || TARGET_3DNOW_A"
  "pmovmskb\t{%1, %0|%0, %1}"
  [(set_attr "type" "mmxcvt")
   (set_attr "mode" "DI")])

(define_expand "mmx_maskmovq"
  [(set (match_operand:V8QI 0 "memory_operand" "")
	(unspec:V8QI [(match_operand:V8QI 1 "register_operand" "y")
		      (match_operand:V8QI 2 "register_operand" "y")
		      (match_dup 0)]
		     UNSPEC_MASKMOV))]
  "TARGET_SSE || TARGET_3DNOW_A"
  "")

(define_insn "*mmx_maskmovq"
  [(set (mem:V8QI (match_operand:SI 0 "register_operand" "D"))
	(unspec:V8QI [(match_operand:V8QI 1 "register_operand" "y")
		      (match_operand:V8QI 2 "register_operand" "y")
		      (mem:V8QI (match_dup 0))]
		     UNSPEC_MASKMOV))]
  "(TARGET_SSE || TARGET_3DNOW_A) && !TARGET_64BIT"
  ;; @@@ check ordering of operands in intel/nonintel syntax
  "maskmovq\t{%2, %1|%1, %2}"
  [(set_attr "type" "mmxcvt")
   (set_attr "mode" "DI")])

(define_insn "*mmx_maskmovq_rex"
  [(set (mem:V8QI (match_operand:DI 0 "register_operand" "D"))
	(unspec:V8QI [(match_operand:V8QI 1 "register_operand" "y")
		      (match_operand:V8QI 2 "register_operand" "y")
		      (mem:V8QI (match_dup 0))]
		     UNSPEC_MASKMOV))]
  "(TARGET_SSE || TARGET_3DNOW_A) && TARGET_64BIT"
  ;; @@@ check ordering of operands in intel/nonintel syntax
  "maskmovq\t{%2, %1|%1, %2}"
  [(set_attr "type" "mmxcvt")
   (set_attr "mode" "DI")])

(define_insn "mmx_emms"
  [(unspec_volatile [(const_int 0)] UNSPECV_EMMS)
   (clobber (reg:XF 8))
   (clobber (reg:XF 9))
   (clobber (reg:XF 10))
   (clobber (reg:XF 11))
   (clobber (reg:XF 12))
   (clobber (reg:XF 13))
   (clobber (reg:XF 14))
   (clobber (reg:XF 15))
   (clobber (reg:DI 29))
   (clobber (reg:DI 30))
   (clobber (reg:DI 31))
   (clobber (reg:DI 32))
   (clobber (reg:DI 33))
   (clobber (reg:DI 34))
   (clobber (reg:DI 35))
   (clobber (reg:DI 36))]
  "TARGET_MMX"
  "emms"
  [(set_attr "type" "mmx")
   (set_attr "memory" "unknown")])

(define_insn "mmx_femms"
  [(unspec_volatile [(const_int 0)] UNSPECV_FEMMS)
   (clobber (reg:XF 8))
   (clobber (reg:XF 9))
   (clobber (reg:XF 10))
   (clobber (reg:XF 11))
   (clobber (reg:XF 12))
   (clobber (reg:XF 13))
   (clobber (reg:XF 14))
   (clobber (reg:XF 15))
   (clobber (reg:DI 29))
   (clobber (reg:DI 30))
   (clobber (reg:DI 31))
   (clobber (reg:DI 32))
   (clobber (reg:DI 33))
   (clobber (reg:DI 34))
   (clobber (reg:DI 35))
   (clobber (reg:DI 36))]
  "TARGET_3DNOW"
  "femms"
  [(set_attr "type" "mmx")
   (set_attr "memory" "none")]) 
