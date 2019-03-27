;; GCC machine description for SPARC synchronization instructions.
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

(define_mode_macro I12MODE [QI HI])
(define_mode_macro I24MODE [HI SI])
(define_mode_macro I48MODE [SI (DI "TARGET_ARCH64 || TARGET_V8PLUS")])
(define_mode_attr modesuffix [(SI "") (DI "x")])

(define_expand "memory_barrier"
  [(set (mem:BLK (match_dup 0))
	(unspec_volatile:BLK [(mem:BLK (match_dup 0)) (match_dup 1)]
			     UNSPECV_MEMBAR))]
  "TARGET_V8 || TARGET_V9"
{
  operands[0] = gen_rtx_MEM (BLKmode, gen_rtx_SCRATCH (DImode));
  MEM_VOLATILE_P (operands[0]) = 1;
  if (TARGET_V9)
    /* member #StoreStore | #LoadStore | #StoreLoad | #LoadLoad */
    operands[1] = GEN_INT (15);
  else
    /* stbar */
    operands[1] = GEN_INT (8);
})

(define_insn "*stbar"
  [(set (match_operand:BLK 0 "" "")
	(unspec_volatile:BLK [(match_operand:BLK 1 "" "")
			      (const_int 8)] UNSPECV_MEMBAR))]
  "TARGET_V8"
  "stbar"
  [(set_attr "type" "multi")])

(define_insn "*membar"
  [(set (match_operand:BLK 0 "" "")
	(unspec_volatile:BLK [(match_operand:BLK 1 "" "")
			      (match_operand:SI 2 "immediate_operand" "I")]
			      UNSPECV_MEMBAR))]
  "TARGET_V9"
  "membar\t%2"
  [(set_attr "type" "multi")])

(define_expand "sync_compare_and_swap<mode>"
  [(match_operand:I12MODE 0 "register_operand" "")
   (match_operand:I12MODE 1 "memory_operand" "")
   (match_operand:I12MODE 2 "register_operand" "")
   (match_operand:I12MODE 3 "register_operand" "")]
  "TARGET_V9"
{
  sparc_expand_compare_and_swap_12 (operands[0], operands[1],
				    operands[2], operands[3]);
  DONE;
})

(define_expand "sync_compare_and_swap<mode>"
  [(parallel
     [(set (match_operand:I48MODE 0 "register_operand" "=r")
	   (match_operand:I48MODE 1 "memory_operand" ""))
      (set (match_dup 1)
	   (unspec_volatile:I48MODE
	     [(match_operand:I48MODE 2 "register_operand" "")
	      (match_operand:I48MODE 3 "register_operand" "")]
	     UNSPECV_CAS))])]
  "TARGET_V9"
{
  if (! REG_P (XEXP (operands[1], 0)))
    {
      rtx addr = force_reg (Pmode, XEXP (operands[1], 0));
      operands[1] = replace_equiv_address (operands[1], addr);
    }
  emit_insn (gen_memory_barrier ());
})

(define_insn "*sync_compare_and_swap<mode>"
  [(set (match_operand:I48MODE 0 "register_operand" "=r")
	(match_operand:I48MODE 1 "memory_reg_operand" "+m"))
   (set (match_dup 1)
	(unspec_volatile:I48MODE
	  [(match_operand:I48MODE 2 "register_operand" "r")
	   (match_operand:I48MODE 3 "register_operand" "0")]
	  UNSPECV_CAS))]
  "TARGET_V9 && (<MODE>mode == SImode || TARGET_ARCH64)"
  "cas<modesuffix>\t%1, %2, %0"
  [(set_attr "type" "multi")])

(define_insn "*sync_compare_and_swapdi_v8plus"
  [(set (match_operand:DI 0 "register_operand" "=h")
	(match_operand:DI 1 "memory_reg_operand" "+m"))
   (set (match_dup 1)
	(unspec_volatile:DI
	  [(match_operand:DI 2 "register_operand" "h")
	   (match_operand:DI 3 "register_operand" "0")]
	  UNSPECV_CAS))]
  "TARGET_V8PLUS"
{
  if (sparc_check_64 (operands[3], insn) <= 0)
    output_asm_insn ("srl\t%L3, 0, %L3", operands);
  output_asm_insn ("sllx\t%H3, 32, %H3", operands);
  output_asm_insn ("or\t%L3, %H3, %L3", operands);
  if (sparc_check_64 (operands[2], insn) <= 0)
    output_asm_insn ("srl\t%L2, 0, %L2", operands);
  output_asm_insn ("sllx\t%H2, 32, %H3", operands);
  output_asm_insn ("or\t%L2, %H3, %H3", operands);
  output_asm_insn ("casx\t%1, %H3, %L3", operands);
  return "srlx\t%L3, 32, %H3";
}
  [(set_attr "type" "multi")
   (set_attr "length" "8")])

(define_expand "sync_lock_test_and_set<mode>"
  [(match_operand:I12MODE 0 "register_operand" "")
   (match_operand:I12MODE 1 "memory_operand" "")
   (match_operand:I12MODE 2 "arith_operand" "")]
  "!TARGET_V9"
{
  if (operands[2] != const1_rtx)
    FAIL;
  if (TARGET_V8)
    emit_insn (gen_memory_barrier ());
  if (<MODE>mode != QImode)
    operands[1] = adjust_address (operands[1], QImode, 0);
  emit_insn (gen_ldstub<mode> (operands[0], operands[1]));
  DONE;
})

(define_expand "sync_lock_test_and_setsi"
  [(parallel
     [(set (match_operand:SI 0 "register_operand" "")
	   (unspec_volatile:SI [(match_operand:SI 1 "memory_operand" "")]
			       UNSPECV_SWAP))
      (set (match_dup 1)
	   (match_operand:SI 2 "arith_operand" ""))])]
  ""
{
  if (! TARGET_V8 && ! TARGET_V9)
    {
      if (operands[2] != const1_rtx)
	FAIL;
      operands[1] = adjust_address (operands[1], QImode, 0);
      emit_insn (gen_ldstubsi (operands[0], operands[1]));
      DONE;
    }
  emit_insn (gen_memory_barrier ());
  operands[2] = force_reg (SImode, operands[2]);
})

(define_insn "*swapsi"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(unspec_volatile:SI [(match_operand:SI 1 "memory_operand" "+m")]
			    UNSPECV_SWAP))
   (set (match_dup 1)
	(match_operand:SI 2 "register_operand" "0"))]
  "TARGET_V8 || TARGET_V9"
  "swap\t%1, %0"
  [(set_attr "type" "multi")])

(define_expand "ldstubqi"
  [(parallel [(set (match_operand:QI 0 "register_operand" "")
		   (unspec_volatile:QI [(match_operand:QI 1 "memory_operand" "")]
				       UNSPECV_LDSTUB))
	      (set (match_dup 1) (const_int -1))])]
  ""
  "")

(define_expand "ldstub<mode>"
  [(parallel [(set (match_operand:I24MODE 0 "register_operand" "")
		   (zero_extend:I24MODE
		      (unspec_volatile:QI [(match_operand:QI 1 "memory_operand" "")]
					  UNSPECV_LDSTUB)))
	      (set (match_dup 1) (const_int -1))])]
  ""
  "")

(define_insn "*ldstubqi"
  [(set (match_operand:QI 0 "register_operand" "=r")
	(unspec_volatile:QI [(match_operand:QI 1 "memory_operand" "+m")]
			    UNSPECV_LDSTUB))
   (set (match_dup 1) (const_int -1))]
  ""
  "ldstub\t%1, %0"
  [(set_attr "type" "multi")])

(define_insn "*ldstub<mode>"
  [(set (match_operand:I24MODE 0 "register_operand" "=r")
	(zero_extend:I24MODE
	  (unspec_volatile:QI [(match_operand:QI 1 "memory_operand" "+m")]
			      UNSPECV_LDSTUB)))
   (set (match_dup 1) (const_int -1))]
  ""
  "ldstub\t%1, %0"
  [(set_attr "type" "multi")])
