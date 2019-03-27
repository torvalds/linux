;; GCC machine description for i386 synchronization instructions.
;; Copyright (C) 2005, 2006
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

(define_mode_macro IMODE [QI HI SI (DI "TARGET_64BIT")])
(define_mode_attr modesuffix [(QI "b") (HI "w") (SI "l") (DI "q")])
(define_mode_attr modeconstraint [(QI "q") (HI "r") (SI "r") (DI "r")])
(define_mode_attr immconstraint [(QI "i") (HI "i") (SI "i") (DI "e")])

(define_mode_macro CASMODE [QI HI SI (DI "TARGET_64BIT || TARGET_CMPXCHG8B")
			   (TI "TARGET_64BIT && TARGET_CMPXCHG16B")])
(define_mode_macro DCASMODE
  [(DI "!TARGET_64BIT && TARGET_CMPXCHG8B && !flag_pic")
   (TI "TARGET_64BIT && TARGET_CMPXCHG16B")])
(define_mode_attr doublemodesuffix [(DI "8") (TI "16")])
(define_mode_attr DCASHMODE [(DI "SI") (TI "DI")])

;; ??? It would be possible to use cmpxchg8b on pentium for DImode
;; changes.  It's complicated because the insn uses ecx:ebx as the
;; new value; note that the registers are reversed from the order
;; that they'd be in with (reg:DI 2 ecx).  Similarly for TImode 
;; data in 64-bit mode.

(define_expand "sync_compare_and_swap<mode>"
  [(parallel
    [(set (match_operand:CASMODE 0 "register_operand" "")
	  (match_operand:CASMODE 1 "memory_operand" ""))
     (set (match_dup 1)
	  (unspec_volatile:CASMODE
	    [(match_dup 1)
	     (match_operand:CASMODE 2 "register_operand" "")
	     (match_operand:CASMODE 3 "register_operand" "")]
	    UNSPECV_CMPXCHG_1))
     (clobber (reg:CC FLAGS_REG))])]
  "TARGET_CMPXCHG"
{
  if ((<MODE>mode == DImode && !TARGET_64BIT) || <MODE>mode == TImode)
    {
      enum machine_mode hmode = <MODE>mode == DImode ? SImode : DImode;
      rtx low = simplify_gen_subreg (hmode, operands[3], <MODE>mode, 0);
      rtx high = simplify_gen_subreg (hmode, operands[3], <MODE>mode,
				      GET_MODE_SIZE (hmode));
      low = force_reg (hmode, low);
      high = force_reg (hmode, high);
      if (<MODE>mode == DImode)
	emit_insn (gen_sync_double_compare_and_swapdi
		   (operands[0], operands[1], operands[2], low, high));
      else if (<MODE>mode == TImode)
	emit_insn (gen_sync_double_compare_and_swapti
		   (operands[0], operands[1], operands[2], low, high));
      else
	gcc_unreachable ();
      DONE;
    }
})

(define_insn "*sync_compare_and_swap<mode>"
  [(set (match_operand:IMODE 0 "register_operand" "=a")
	(match_operand:IMODE 1 "memory_operand" "+m"))
   (set (match_dup 1)
	(unspec_volatile:IMODE
	  [(match_dup 1)
	   (match_operand:IMODE 2 "register_operand" "a")
	   (match_operand:IMODE 3 "register_operand" "<modeconstraint>")]
	  UNSPECV_CMPXCHG_1))
   (clobber (reg:CC FLAGS_REG))]
  "TARGET_CMPXCHG"
  "lock\;cmpxchg{<modesuffix>}\t{%3, %1|%1, %3}")

(define_insn "sync_double_compare_and_swap<mode>"
  [(set (match_operand:DCASMODE 0 "register_operand" "=A")
	(match_operand:DCASMODE 1 "memory_operand" "+m"))
   (set (match_dup 1)
	(unspec_volatile:DCASMODE
	  [(match_dup 1)
	   (match_operand:DCASMODE 2 "register_operand" "A")
	   (match_operand:<DCASHMODE> 3 "register_operand" "b")
	   (match_operand:<DCASHMODE> 4 "register_operand" "c")]
	  UNSPECV_CMPXCHG_1))
   (clobber (reg:CC FLAGS_REG))]
  ""
  "lock\;cmpxchg<doublemodesuffix>b\t%1")

;; Theoretically we'd like to use constraint "r" (any reg) for operand
;; 3, but that includes ecx.  If operand 3 and 4 are the same (like when
;; the input is -1LL) GCC might chose to allocate operand 3 to ecx, like
;; operand 4.  This breaks, as the xchg will move the PIC register contents
;; to %ecx then --> boom.  Operands 3 and 4 really need to be different
;; registers, which in this case means operand 3 must not be ecx.
;; Instead of playing tricks with fake early clobbers or the like we
;; just enumerate all regs possible here, which (as this is !TARGET_64BIT)
;; are just esi and edi.
(define_insn "*sync_double_compare_and_swapdi_pic"
  [(set (match_operand:DI 0 "register_operand" "=A")
	(match_operand:DI 1 "memory_operand" "+m"))
   (set (match_dup 1)
	(unspec_volatile:DI
	  [(match_dup 1)
	   (match_operand:DI 2 "register_operand" "A")
	   (match_operand:SI 3 "register_operand" "SD")
	   (match_operand:SI 4 "register_operand" "c")]
	  UNSPECV_CMPXCHG_1))
   (clobber (reg:CC FLAGS_REG))]
  "!TARGET_64BIT && TARGET_CMPXCHG8B && flag_pic"
  "xchg{l}\t%%ebx, %3\;lock\;cmpxchg8b\t%1\;xchg{l}\t%%ebx, %3")

(define_expand "sync_compare_and_swap_cc<mode>"
  [(parallel
    [(set (match_operand:CASMODE 0 "register_operand" "")
	  (match_operand:CASMODE 1 "memory_operand" ""))
     (set (match_dup 1)
	  (unspec_volatile:CASMODE
	    [(match_dup 1)
	     (match_operand:CASMODE 2 "register_operand" "")
	     (match_operand:CASMODE 3 "register_operand" "")]
	    UNSPECV_CMPXCHG_1))
     (set (match_dup 4)
	  (compare:CCZ
	    (unspec_volatile:CASMODE
	      [(match_dup 1) (match_dup 2) (match_dup 3)] UNSPECV_CMPXCHG_2)
	    (match_dup 2)))])]
  "TARGET_CMPXCHG"
{
  operands[4] = gen_rtx_REG (CCZmode, FLAGS_REG);
  ix86_compare_op0 = operands[3];
  ix86_compare_op1 = NULL;
  ix86_compare_emitted = operands[4];
  if ((<MODE>mode == DImode && !TARGET_64BIT) || <MODE>mode == TImode)
    {
      enum machine_mode hmode = <MODE>mode == DImode ? SImode : DImode;
      rtx low = simplify_gen_subreg (hmode, operands[3], <MODE>mode, 0);
      rtx high = simplify_gen_subreg (hmode, operands[3], <MODE>mode,
				      GET_MODE_SIZE (hmode));
      low = force_reg (hmode, low);
      high = force_reg (hmode, high);
      if (<MODE>mode == DImode)
	emit_insn (gen_sync_double_compare_and_swap_ccdi
		   (operands[0], operands[1], operands[2], low, high));
      else if (<MODE>mode == TImode)
	emit_insn (gen_sync_double_compare_and_swap_ccti
		   (operands[0], operands[1], operands[2], low, high));
      else
	gcc_unreachable ();
      DONE;
    }
})

(define_insn "*sync_compare_and_swap_cc<mode>"
  [(set (match_operand:IMODE 0 "register_operand" "=a")
	(match_operand:IMODE 1 "memory_operand" "+m"))
   (set (match_dup 1)
	(unspec_volatile:IMODE
	  [(match_dup 1)
	   (match_operand:IMODE 2 "register_operand" "a")
	   (match_operand:IMODE 3 "register_operand" "<modeconstraint>")]
	  UNSPECV_CMPXCHG_1))
   (set (reg:CCZ FLAGS_REG)
	(compare:CCZ
	  (unspec_volatile:IMODE
	    [(match_dup 1) (match_dup 2) (match_dup 3)] UNSPECV_CMPXCHG_2)
	  (match_dup 2)))]
  "TARGET_CMPXCHG"
  "lock\;cmpxchg{<modesuffix>}\t{%3, %1|%1, %3}")

(define_insn "sync_double_compare_and_swap_cc<mode>"
  [(set (match_operand:DCASMODE 0 "register_operand" "=A")
	(match_operand:DCASMODE 1 "memory_operand" "+m"))
   (set (match_dup 1)
	(unspec_volatile:DCASMODE
	  [(match_dup 1)
	   (match_operand:DCASMODE 2 "register_operand" "A")
	   (match_operand:<DCASHMODE> 3 "register_operand" "b")
	   (match_operand:<DCASHMODE> 4 "register_operand" "c")]
	  UNSPECV_CMPXCHG_1))
   (set (reg:CCZ FLAGS_REG)
	(compare:CCZ
	  (unspec_volatile:DCASMODE
	    [(match_dup 1) (match_dup 2) (match_dup 3) (match_dup 4)]
	    UNSPECV_CMPXCHG_2)
	  (match_dup 2)))]
  ""
  "lock\;cmpxchg<doublemodesuffix>b\t%1")

;; See above for the explanation of using the constraint "SD" for
;; operand 3.
(define_insn "*sync_double_compare_and_swap_ccdi_pic"
  [(set (match_operand:DI 0 "register_operand" "=A")
	(match_operand:DI 1 "memory_operand" "+m"))
   (set (match_dup 1)
	(unspec_volatile:DI
	  [(match_dup 1)
	   (match_operand:DI 2 "register_operand" "A")
	   (match_operand:SI 3 "register_operand" "SD")
	   (match_operand:SI 4 "register_operand" "c")]
	  UNSPECV_CMPXCHG_1))
   (set (reg:CCZ FLAGS_REG)
	(compare:CCZ
	  (unspec_volatile:DI
	    [(match_dup 1) (match_dup 2) (match_dup 3) (match_dup 4)]
	    UNSPECV_CMPXCHG_2)
	  (match_dup 2)))]
  "!TARGET_64BIT && TARGET_CMPXCHG8B && flag_pic"
  "xchg{l}\t%%ebx, %3\;lock\;cmpxchg8b\t%1\;xchg{l}\t%%ebx, %3")

(define_insn "sync_old_add<mode>"
  [(set (match_operand:IMODE 0 "register_operand" "=<modeconstraint>")
	(unspec_volatile:IMODE
	  [(match_operand:IMODE 1 "memory_operand" "+m")] UNSPECV_XCHG))
   (set (match_dup 1)
	(plus:IMODE (match_dup 1)
		    (match_operand:IMODE 2 "register_operand" "0")))
   (clobber (reg:CC FLAGS_REG))]
  "TARGET_XADD"
  "lock\;xadd{<modesuffix>}\t{%0, %1|%1, %0}")

;; Recall that xchg implicitly sets LOCK#, so adding it again wastes space.
(define_insn "sync_lock_test_and_set<mode>"
  [(set (match_operand:IMODE 0 "register_operand" "=<modeconstraint>")
	(unspec_volatile:IMODE
	  [(match_operand:IMODE 1 "memory_operand" "+m")] UNSPECV_XCHG))
   (set (match_dup 1)
	(match_operand:IMODE 2 "register_operand" "0"))]
  ""
  "xchg{<modesuffix>}\t{%1, %0|%0, %1}")

(define_insn "sync_add<mode>"
  [(set (match_operand:IMODE 0 "memory_operand" "+m")
	(unspec_volatile:IMODE
	  [(plus:IMODE (match_dup 0)
	     (match_operand:IMODE 1 "nonmemory_operand" "<modeconstraint><immconstraint>"))]
	  UNSPECV_LOCK))
   (clobber (reg:CC FLAGS_REG))]
  ""
  "lock\;add{<modesuffix>}\t{%1, %0|%0, %1}")

(define_insn "sync_sub<mode>"
  [(set (match_operand:IMODE 0 "memory_operand" "+m")
	(unspec_volatile:IMODE
	  [(minus:IMODE (match_dup 0)
	     (match_operand:IMODE 1 "nonmemory_operand" "<modeconstraint><immconstraint>"))]
	  UNSPECV_LOCK))
   (clobber (reg:CC FLAGS_REG))]
  ""
  "lock\;sub{<modesuffix>}\t{%1, %0|%0, %1}")

(define_insn "sync_ior<mode>"
  [(set (match_operand:IMODE 0 "memory_operand" "+m")
	(unspec_volatile:IMODE
	  [(ior:IMODE (match_dup 0)
	     (match_operand:IMODE 1 "nonmemory_operand" "<modeconstraint><immconstraint>"))]
	  UNSPECV_LOCK))
   (clobber (reg:CC FLAGS_REG))]
  ""
  "lock\;or{<modesuffix>}\t{%1, %0|%0, %1}")

(define_insn "sync_and<mode>"
  [(set (match_operand:IMODE 0 "memory_operand" "+m")
	(unspec_volatile:IMODE
	  [(and:IMODE (match_dup 0)
	     (match_operand:IMODE 1 "nonmemory_operand" "<modeconstraint><immconstraint>"))]
	  UNSPECV_LOCK))
   (clobber (reg:CC FLAGS_REG))]
  ""
  "lock\;and{<modesuffix>}\t{%1, %0|%0, %1}")

(define_insn "sync_xor<mode>"
  [(set (match_operand:IMODE 0 "memory_operand" "+m")
	(unspec_volatile:IMODE
	  [(xor:IMODE (match_dup 0)
	     (match_operand:IMODE 1 "nonmemory_operand" "<modeconstraint><immconstraint>"))]
	  UNSPECV_LOCK))
   (clobber (reg:CC FLAGS_REG))]
  ""
  "lock\;xor{<modesuffix>}\t{%1, %0|%0, %1}")
