;; Machine description for PowerPC synchronization instructions.
;; Copyright (C) 2005 Free Software Foundation, Inc.
;; Contributed by Geoffrey Keating.

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
;; along with GCC; see the file COPYING.  If not, write to the
;; Free Software Foundation, 51 Franklin Street, Fifth Floor, Boston,
;; MA 02110-1301, USA.

(define_mode_attr larx [(SI "lwarx") (DI "ldarx")])
(define_mode_attr stcx [(SI "stwcx.") (DI "stdcx.")])

(define_code_macro FETCHOP [plus minus ior xor and])
(define_code_attr fetchop_name
  [(plus "add") (minus "sub") (ior "ior") (xor "xor") (and "and")])
(define_code_attr fetchop_pred
  [(plus "add_operand") (minus "gpc_reg_operand")
   (ior "logical_operand") (xor "logical_operand") (and "and_operand")])
(define_code_attr fetchopsi_constr
  [(plus "rIL") (minus "r") (ior "rKL") (xor "rKL") (and "rTKL")])
(define_code_attr fetchopdi_constr
  [(plus "rIL") (minus "r") (ior "rKJF") (xor "rKJF") (and "rSTKJ")])

(define_expand "memory_barrier"
  [(set (mem:BLK (match_dup 0))
	(unspec:BLK [(mem:BLK (match_dup 0))] UNSPEC_SYNC))]
  ""
{
  operands[0] = gen_rtx_MEM (BLKmode, gen_rtx_SCRATCH (Pmode));
  MEM_VOLATILE_P (operands[0]) = 1;
})

(define_insn "*sync_internal"
  [(set (match_operand:BLK 0 "" "")
	(unspec:BLK [(match_operand:BLK 1 "" "")] UNSPEC_SYNC))]
  ""
  "{dcs|sync}"
  [(set_attr "type" "sync")])

(define_insn "load_locked_<mode>"
  [(set (match_operand:GPR 0 "gpc_reg_operand" "=r")
	(unspec_volatile:GPR
	  [(match_operand:GPR 1 "memory_operand" "Z")] UNSPECV_LL))]
  "TARGET_POWERPC"
  "<larx> %0,%y1"
  [(set_attr "type" "load_l")])

(define_insn "store_conditional_<mode>"
  [(set (match_operand:CC 0 "cc_reg_operand" "=x")
	(unspec_volatile:CC [(const_int 0)] UNSPECV_SC))
   (set (match_operand:GPR 1 "memory_operand" "=Z")
	(match_operand:GPR 2 "gpc_reg_operand" "r"))]
  "TARGET_POWERPC"
  "<stcx> %2,%y1"
  [(set_attr "type" "store_c")])

(define_insn_and_split "sync_compare_and_swap<mode>"
  [(set (match_operand:GPR 0 "gpc_reg_operand" "=&r")
	(match_operand:GPR 1 "memory_operand" "+Z"))
   (set (match_dup 1)
	(unspec:GPR
	  [(match_operand:GPR 2 "reg_or_short_operand" "rI")
	   (match_operand:GPR 3 "gpc_reg_operand" "r")]
	  UNSPEC_CMPXCHG))
   (clobber (match_scratch:GPR 4 "=&r"))
   (clobber (match_scratch:CC 5 "=&x"))]
  "TARGET_POWERPC"
  "#"
  "&& reload_completed"
  [(const_int 0)]
{
  rs6000_split_compare_and_swap (operands[0], operands[1], operands[2],
				 operands[3], operands[4]);
  DONE;
})

(define_expand "sync_compare_and_swaphi"
  [(match_operand:HI 0 "gpc_reg_operand" "")
   (match_operand:HI 1 "memory_operand" "")
   (match_operand:HI 2 "gpc_reg_operand" "")
   (match_operand:HI 3 "gpc_reg_operand" "")]
  "TARGET_POWERPC"
{
  rs6000_expand_compare_and_swapqhi (operands[0], operands[1],
				     operands[2], operands[3]);
  DONE;
})

(define_expand "sync_compare_and_swapqi"
  [(match_operand:QI 0 "gpc_reg_operand" "")
   (match_operand:QI 1 "memory_operand" "")
   (match_operand:QI 2 "gpc_reg_operand" "")
   (match_operand:QI 3 "gpc_reg_operand" "")]
  "TARGET_POWERPC"
{
  rs6000_expand_compare_and_swapqhi (operands[0], operands[1],
				     operands[2], operands[3]);
  DONE;
})

(define_insn_and_split "sync_compare_and_swapqhi_internal"
  [(set (match_operand:SI 0 "gpc_reg_operand" "=&r")
	(match_operand:SI 4 "memory_operand" "+Z"))
   (set (match_dup 4)
        (unspec:SI
          [(match_operand:SI 1 "gpc_reg_operand" "r")
           (match_operand:SI 2 "gpc_reg_operand" "r")
           (match_operand:SI 3 "gpc_reg_operand" "r")]
          UNSPEC_CMPXCHG))
   (clobber (match_scratch:SI 5 "=&r"))
   (clobber (match_scratch:CC 6 "=&x"))]
  "TARGET_POWERPC"
  "#"
  "&& reload_completed"
  [(const_int 0)]
{
  rs6000_split_compare_and_swapqhi (operands[0], operands[1],
				    operands[2], operands[3], operands[4],
				    operands[5]);
  DONE;
})

(define_insn_and_split "sync_lock_test_and_set<mode>"
  [(set (match_operand:GPR 0 "gpc_reg_operand" "=&r")
	(match_operand:GPR 1 "memory_operand" "+Z"))
   (set (match_dup 1)
	(unspec:GPR
	  [(match_operand:GPR 2 "reg_or_short_operand" "rL")]
	  UNSPEC_XCHG))
   (clobber (match_scratch:GPR 3 "=&r"))
   (clobber (match_scratch:CC 4 "=&x"))]
  "TARGET_POWERPC"
  "#"
  "&& reload_completed"
  [(const_int 0)]
{
  rs6000_split_lock_test_and_set (operands[0], operands[1], operands[2],
				  operands[3]);
  DONE;
})

(define_expand "sync_<fetchop_name><mode>"
  [(parallel [(set (match_operand:INT1 0 "memory_operand" "")
		   (unspec:INT1
		     [(FETCHOP:INT1 (match_dup 0)
			(match_operand:INT1 1 "<fetchop_pred>" ""))]
		     UNSPEC_ATOMIC))
	      (clobber (scratch:INT1))
	      (clobber (scratch:CC))])]
  "TARGET_POWERPC"
  "
{
  if (<MODE>mode != SImode && <MODE>mode != DImode)
    {
      if (PPC405_ERRATUM77)
	FAIL;
      rs6000_emit_sync (<CODE>, <MODE>mode, operands[0], operands[1],
			NULL_RTX, NULL_RTX, true);
      DONE;
    }
}")

(define_insn_and_split "*sync_<fetchop_name>si_internal"
  [(set (match_operand:SI 0 "memory_operand" "+Z")
	(unspec:SI
	  [(FETCHOP:SI (match_dup 0)
	     (match_operand:SI 1 "<fetchop_pred>" "<fetchopsi_constr>"))]
	  UNSPEC_ATOMIC))
   (clobber (match_scratch:SI 2 "=&b"))
   (clobber (match_scratch:CC 3 "=&x"))]
  "TARGET_POWERPC"
  "#"
  "&& reload_completed"
  [(const_int 0)]
{
  rs6000_split_atomic_op (<CODE>, operands[0], operands[1],
			  NULL_RTX, NULL_RTX, operands[2]);
  DONE;
})

(define_insn_and_split "*sync_<fetchop_name>di_internal"
  [(set (match_operand:DI 0 "memory_operand" "+Z")
	(unspec:DI
	  [(FETCHOP:DI (match_dup 0)
	     (match_operand:DI 1 "<fetchop_pred>" "<fetchopdi_constr>"))]
	  UNSPEC_ATOMIC))
   (clobber (match_scratch:DI 2 "=&b"))
   (clobber (match_scratch:CC 3 "=&x"))]
  "TARGET_POWERPC"
  "#"
  "&& reload_completed"
  [(const_int 0)]
{
  rs6000_split_atomic_op (<CODE>, operands[0], operands[1],
			  NULL_RTX, NULL_RTX, operands[2]);
  DONE;
})

(define_expand "sync_nand<mode>"
  [(parallel [(set (match_operand:INT1 0 "memory_operand" "")
	      (unspec:INT1
		[(and:INT1 (not:INT1 (match_dup 0))
		   (match_operand:INT1 1 "gpc_reg_operand" ""))]
		UNSPEC_ATOMIC))
	      (clobber (scratch:INT1))
	      (clobber (scratch:CC))])]
  "TARGET_POWERPC"
  "
{
  if (<MODE>mode != SImode && <MODE>mode != DImode)
    {
      if (PPC405_ERRATUM77)
	FAIL;
      rs6000_emit_sync (AND, <MODE>mode,
			gen_rtx_NOT (<MODE>mode, operands[0]),
			operands[1],
			NULL_RTX, NULL_RTX, true);
      DONE;
    }
}")

(define_insn_and_split "*sync_nand<mode>_internal"
  [(set (match_operand:GPR 0 "memory_operand" "+Z")
	(unspec:GPR
	  [(and:GPR (not:GPR (match_dup 0))
	     (match_operand:GPR 1 "gpc_reg_operand" "r"))]
	  UNSPEC_ATOMIC))
   (clobber (match_scratch:GPR 2 "=&r"))
   (clobber (match_scratch:CC 3 "=&x"))]
  "TARGET_POWERPC"
  "#"
  "&& reload_completed"
  [(const_int 0)]
{
  rs6000_split_atomic_op (NOT, operands[0], operands[1],
			  NULL_RTX, NULL_RTX, operands[2]);
  DONE;
})

(define_expand "sync_old_<fetchop_name><mode>"
  [(parallel [(set (match_operand:INT1 0 "gpc_reg_operand" "")
		   (match_operand:INT1 1 "memory_operand" ""))
	      (set (match_dup 1)
		   (unspec:INT1
		     [(FETCHOP:INT1 (match_dup 1)
			(match_operand:INT1 2 "<fetchop_pred>" ""))]
		     UNSPEC_ATOMIC))
	      (clobber (scratch:INT1))
	      (clobber (scratch:CC))])]
  "TARGET_POWERPC"
  "
{ 
  if (<MODE>mode != SImode && <MODE>mode != DImode)
    {
      if (PPC405_ERRATUM77)
	FAIL;
      rs6000_emit_sync (<CODE>, <MODE>mode, operands[1], operands[2],
			operands[0], NULL_RTX, true);
      DONE;
    }
}")

(define_insn_and_split "*sync_old_<fetchop_name>si_internal"
  [(set (match_operand:SI 0 "gpc_reg_operand" "=&r")
	(match_operand:SI 1 "memory_operand" "+Z"))
   (set (match_dup 1)
	(unspec:SI
	  [(FETCHOP:SI (match_dup 1)
	     (match_operand:SI 2 "<fetchop_pred>" "<fetchopsi_constr>"))]
	  UNSPEC_ATOMIC))
   (clobber (match_scratch:SI 3 "=&b"))
   (clobber (match_scratch:CC 4 "=&x"))]
  "TARGET_POWERPC"
  "#"
  "&& reload_completed"
  [(const_int 0)]
{
  rs6000_split_atomic_op (<CODE>, operands[1], operands[2],
			  operands[0], NULL_RTX, operands[3]);
  DONE;
})

(define_insn_and_split "*sync_old_<fetchop_name>di_internal"
  [(set (match_operand:DI 0 "gpc_reg_operand" "=&r")
	(match_operand:DI 1 "memory_operand" "+Z"))
   (set (match_dup 1)
	(unspec:DI
	  [(FETCHOP:DI (match_dup 1)
	     (match_operand:DI 2 "<fetchop_pred>" "<fetchopdi_constr>"))]
	  UNSPEC_ATOMIC))
   (clobber (match_scratch:DI 3 "=&b"))
   (clobber (match_scratch:CC 4 "=&x"))]
  "TARGET_POWERPC"
  "#"
  "&& reload_completed"
  [(const_int 0)]
{
  rs6000_split_atomic_op (<CODE>, operands[1], operands[2],
			  operands[0], NULL_RTX, operands[3]);
  DONE;
})

(define_expand "sync_old_nand<mode>"
  [(parallel [(set (match_operand:INT1 0 "gpc_reg_operand" "")
		   (match_operand:INT1 1 "memory_operand" ""))
	      (set (match_dup 1)
		   (unspec:INT1
		     [(and:INT1 (not:INT1 (match_dup 1))
			(match_operand:INT1 2 "gpc_reg_operand" ""))]
		     UNSPEC_ATOMIC))
	      (clobber (scratch:INT1))
	      (clobber (scratch:CC))])]
  "TARGET_POWERPC"
  "
{
  if (<MODE>mode != SImode && <MODE>mode != DImode)
    {
      if (PPC405_ERRATUM77)
	FAIL;
      rs6000_emit_sync (AND, <MODE>mode,
			gen_rtx_NOT (<MODE>mode, operands[1]),
			operands[2],
			operands[0], NULL_RTX, true);
      DONE;
    }
}")

(define_insn_and_split "*sync_old_nand<mode>_internal"
  [(set (match_operand:GPR 0 "gpc_reg_operand" "=&r")
	(match_operand:GPR 1 "memory_operand" "+Z"))
   (set (match_dup 1)
	(unspec:GPR
	  [(and:GPR (not:GPR (match_dup 1))
	     (match_operand:GPR 2 "gpc_reg_operand" "r"))]
	  UNSPEC_ATOMIC))
   (clobber (match_scratch:GPR 3 "=&r"))
   (clobber (match_scratch:CC 4 "=&x"))]
  "TARGET_POWERPC"
  "#"
  "&& reload_completed"
  [(const_int 0)]
{
  rs6000_split_atomic_op (NOT, operands[1], operands[2],
			  operands[0], NULL_RTX, operands[3]);
  DONE;
})

(define_expand "sync_new_<fetchop_name><mode>"
  [(parallel [(set (match_operand:INT1 0 "gpc_reg_operand" "")
		   (FETCHOP:INT1
		     (match_operand:INT1 1 "memory_operand" "")
		     (match_operand:INT1 2 "<fetchop_pred>" "")))
	      (set (match_dup 1)
		   (unspec:INT1
		     [(FETCHOP:INT1 (match_dup 1) (match_dup 2))]
		     UNSPEC_ATOMIC))
	      (clobber (scratch:INT1))
	      (clobber (scratch:CC))])]
  "TARGET_POWERPC"
  "
{
  if (<MODE>mode != SImode && <MODE>mode != DImode)
    {
      if (PPC405_ERRATUM77)
	FAIL;
      rs6000_emit_sync (<CODE>, <MODE>mode, operands[1], operands[2],
			NULL_RTX, operands[0], true);
      DONE;
    }
}")

(define_insn_and_split "*sync_new_<fetchop_name>si_internal"
  [(set (match_operand:SI 0 "gpc_reg_operand" "=&r")
	(FETCHOP:SI
	  (match_operand:SI 1 "memory_operand" "+Z")
	  (match_operand:SI 2 "<fetchop_pred>" "<fetchopsi_constr>")))
   (set (match_dup 1)
	(unspec:SI
	  [(FETCHOP:SI (match_dup 1) (match_dup 2))]
	  UNSPEC_ATOMIC))
   (clobber (match_scratch:SI 3 "=&b"))
   (clobber (match_scratch:CC 4 "=&x"))]
  "TARGET_POWERPC"
  "#"
  "&& reload_completed"
  [(const_int 0)]
{
  rs6000_split_atomic_op (<CODE>, operands[1], operands[2],
			  NULL_RTX, operands[0], operands[3]);
  DONE;
})

(define_insn_and_split "*sync_new_<fetchop_name>di_internal"
  [(set (match_operand:DI 0 "gpc_reg_operand" "=&r")
	(FETCHOP:DI
	  (match_operand:DI 1 "memory_operand" "+Z")
	  (match_operand:DI 2 "<fetchop_pred>" "<fetchopdi_constr>")))
   (set (match_dup 1)
	(unspec:DI
	  [(FETCHOP:DI (match_dup 1) (match_dup 2))]
	  UNSPEC_ATOMIC))
   (clobber (match_scratch:DI 3 "=&b"))
   (clobber (match_scratch:CC 4 "=&x"))]
  "TARGET_POWERPC"
  "#"
  "&& reload_completed"
  [(const_int 0)]
{
  rs6000_split_atomic_op (<CODE>, operands[1], operands[2],
			  NULL_RTX, operands[0], operands[3]);
  DONE;
})

(define_expand "sync_new_nand<mode>"
  [(parallel [(set (match_operand:INT1 0 "gpc_reg_operand" "")
		   (and:INT1
		     (not:INT1 (match_operand:INT1 1 "memory_operand" ""))
		     (match_operand:INT1 2 "gpc_reg_operand" "")))
	      (set (match_dup 1)
		   (unspec:INT1
		     [(and:INT1 (not:INT1 (match_dup 1)) (match_dup 2))]
		     UNSPEC_ATOMIC))
	      (clobber (scratch:INT1))
	      (clobber (scratch:CC))])]
  "TARGET_POWERPC"
  "
{
  if (<MODE>mode != SImode && <MODE>mode != DImode)
    {
      if (PPC405_ERRATUM77)
	FAIL;
      rs6000_emit_sync (AND, <MODE>mode,
			gen_rtx_NOT (<MODE>mode, operands[1]),
			operands[2],
			NULL_RTX, operands[0], true);
      DONE;
    }
}")

(define_insn_and_split "*sync_new_nand<mode>_internal"
  [(set (match_operand:GPR 0 "gpc_reg_operand" "=&r")
	(and:GPR
	  (not:GPR (match_operand:GPR 1 "memory_operand" "+Z"))
	  (match_operand:GPR 2 "gpc_reg_operand" "r")))
   (set (match_dup 1)
	(unspec:GPR
	  [(and:GPR (not:GPR (match_dup 1)) (match_dup 2))]
	  UNSPEC_ATOMIC))
   (clobber (match_scratch:GPR 3 "=&r"))
   (clobber (match_scratch:CC 4 "=&x"))]
  "TARGET_POWERPC"
  "#"
  "&& reload_completed"
  [(const_int 0)]
{
  rs6000_split_atomic_op (NOT, operands[1], operands[2],
			  NULL_RTX, operands[0], operands[3]);
  DONE;
})

; and<mode> without cr0 clobber to avoid generation of additional clobber 
; in atomic splitters causing internal consistency failure.
; cr0 already clobbered by larx/stcx.
(define_insn "*atomic_andsi"
  [(set (match_operand:SI 0 "gpc_reg_operand" "=r,r,r,r")
	(unspec:SI [(match_operand:SI 1 "gpc_reg_operand" "%r,r,r,r")
		    (match_operand:SI 2 "and_operand" "?r,T,K,L")]
		    UNSPEC_AND))]
  ""
  "@
   and %0,%1,%2
   {rlinm|rlwinm} %0,%1,0,%m2,%M2
   {andil.|andi.} %0,%1,%b2
   {andiu.|andis.} %0,%1,%u2"
  [(set_attr "type" "*,*,compare,compare")])

(define_insn "*atomic_anddi"
  [(set (match_operand:DI 0 "gpc_reg_operand" "=r,r,r,r,r")
	(unspec:DI [(match_operand:DI 1 "gpc_reg_operand" "%r,r,r,r,r")
		    (match_operand:DI 2 "and_operand" "?r,S,T,K,J")]
		    UNSPEC_AND))]
  "TARGET_POWERPC64"
  "@
   and %0,%1,%2
   rldic%B2 %0,%1,0,%S2
   rlwinm %0,%1,0,%m2,%M2
   andi. %0,%1,%b2
   andis. %0,%1,%u2"
  [(set_attr "type" "*,*,*,compare,compare")
   (set_attr "length" "4,4,4,4,4")])

; the sync_*_internal patterns all have these operands:
; 0 - memory location
; 1 - operand
; 2 - value in memory after operation
; 3 - value in memory immediately before operation

(define_insn "*sync_addshort_internal"
  [(set (match_operand:SI 2 "gpc_reg_operand" "=&r")
	(ior:SI (and:SI (plus:SI (match_operand:SI 0 "memory_operand" "+Z")
				 (match_operand:SI 1 "add_operand" "rI"))
			(match_operand:SI 4 "gpc_reg_operand" "r"))
		(and:SI (not:SI (match_dup 4)) (match_dup 0))))
   (set (match_operand:SI 3 "gpc_reg_operand" "=&b") (match_dup 0))
   (set (match_dup 0)
	(unspec:SI [(ior:SI (and:SI (plus:SI (match_dup 0) (match_dup 1))
				    (match_dup 4))
			    (and:SI (not:SI (match_dup 4)) (match_dup 0)))]
		   UNSPEC_SYNC_OP))
   (clobber (match_scratch:CC 5 "=&x"))
   (clobber (match_scratch:SI 6 "=&r"))]
  "TARGET_POWERPC && !PPC405_ERRATUM77"
  "lwarx %3,%y0\n\tadd%I1 %2,%3,%1\n\tandc %6,%3,%4\n\tand %2,%2,%4\n\tor %2,%2,%6\n\tstwcx. %2,%y0\n\tbne- $-24"
  [(set_attr "length" "28")])

(define_insn "*sync_subshort_internal"
  [(set (match_operand:SI 2 "gpc_reg_operand" "=&r")
	(ior:SI (and:SI (minus:SI (match_operand:SI 0 "memory_operand" "+Z")
				  (match_operand:SI 1 "add_operand" "rI"))
			(match_operand:SI 4 "gpc_reg_operand" "r"))
		(and:SI (not:SI (match_dup 4)) (match_dup 0))))
   (set (match_operand:SI 3 "gpc_reg_operand" "=&b") (match_dup 0))
   (set (match_dup 0)
	(unspec:SI [(ior:SI (and:SI (minus:SI (match_dup 0) (match_dup 1))
				    (match_dup 4))
			    (and:SI (not:SI (match_dup 4)) (match_dup 0)))]
		   UNSPEC_SYNC_OP))
   (clobber (match_scratch:CC 5 "=&x"))
   (clobber (match_scratch:SI 6 "=&r"))]
  "TARGET_POWERPC && !PPC405_ERRATUM77"
  "lwarx %3,%y0\n\tsubf %2,%1,%3\n\tandc %6,%3,%4\n\tand %2,%2,%4\n\tor %2,%2,%6\n\tstwcx. %2,%y0\n\tbne- $-24"
  [(set_attr "length" "28")])

(define_insn "*sync_andsi_internal"
  [(set (match_operand:SI 2 "gpc_reg_operand" "=&r,&r,&r,&r")
	(and:SI (match_operand:SI 0 "memory_operand" "+Z,Z,Z,Z")
		(match_operand:SI 1 "and_operand" "r,T,K,L")))
   (set (match_operand:SI 3 "gpc_reg_operand" "=&b,&b,&b,&b") (match_dup 0))
   (set (match_dup 0)
	(unspec:SI [(and:SI (match_dup 0) (match_dup 1))]
		   UNSPEC_SYNC_OP))
   (clobber (match_scratch:CC 4 "=&x,&x,&x,&x"))]
  "TARGET_POWERPC && !PPC405_ERRATUM77"
  "@
   lwarx %3,%y0\n\tand %2,%3,%1\n\tstwcx. %2,%y0\n\tbne- $-12
   lwarx %3,%y0\n\trlwinm %2,%3,0,%m1,%M1\n\tstwcx. %2,%y0\n\tbne- $-12
   lwarx %3,%y0\n\tandi. %2,%3,%b1\n\tstwcx. %2,%y0\n\tbne- $-12
   lwarx %3,%y0\n\tandis. %2,%3,%u1\n\tstwcx. %2,%y0\n\tbne- $-12"
  [(set_attr "length" "16,16,16,16")])

(define_insn "*sync_boolsi_internal"
  [(set (match_operand:SI 2 "gpc_reg_operand" "=&r,&r,&r")
	(match_operator:SI 4 "boolean_or_operator"
	 [(match_operand:SI 0 "memory_operand" "+Z,Z,Z")
	  (match_operand:SI 1 "logical_operand" "r,K,L")]))
   (set (match_operand:SI 3 "gpc_reg_operand" "=&b,&b,&b") (match_dup 0))
   (set (match_dup 0) (unspec:SI [(match_dup 4)] UNSPEC_SYNC_OP))
   (clobber (match_scratch:CC 5 "=&x,&x,&x"))]
  "TARGET_POWERPC && !PPC405_ERRATUM77"
  "@
   lwarx %3,%y0\n\t%q4 %2,%3,%1\n\tstwcx. %2,%y0\n\tbne- $-12
   lwarx %3,%y0\n\t%q4i %2,%3,%b1\n\tstwcx. %2,%y0\n\tbne- $-12
   lwarx %3,%y0\n\t%q4is %2,%3,%u1\n\tstwcx. %2,%y0\n\tbne- $-12"
  [(set_attr "length" "16,16,16")])

; This pattern could also take immediate values of operand 1,
; since the non-NOT version of the operator is used; but this is not
; very useful, since in practice operand 1 is a full 32-bit value.
; Likewise, operand 5 is in practice either <= 2^16 or it is a register.
(define_insn "*sync_boolcshort_internal"
  [(set (match_operand:SI 2 "gpc_reg_operand" "=&r")
	(match_operator:SI 4 "boolean_operator"
	 [(xor:SI (match_operand:SI 0 "memory_operand" "+Z")
		  (match_operand:SI 5 "logical_operand" "rK"))
	  (match_operand:SI 1 "gpc_reg_operand" "r")]))
   (set (match_operand:SI 3 "gpc_reg_operand" "=&b") (match_dup 0))
   (set (match_dup 0) (unspec:SI [(match_dup 4)] UNSPEC_SYNC_OP))
   (clobber (match_scratch:CC 6 "=&x"))]
  "TARGET_POWERPC && !PPC405_ERRATUM77"
  "lwarx %3,%y0\n\txor%I2 %2,%3,%5\n\t%q4 %2,%2,%1\n\tstwcx. %2,%y0\n\tbne- $-16"
  [(set_attr "length" "20")])

(define_insn "isync"
  [(set (mem:BLK (match_scratch 0 "X"))
	(unspec_volatile:BLK [(mem:BLK (match_scratch 1 "X"))] UNSPEC_ISYNC))]
  ""
  "{ics|isync}"
  [(set_attr "type" "isync")])

(define_expand "sync_lock_release<mode>"
  [(set (match_operand:INT 0 "memory_operand")
	(match_operand:INT 1 "any_operand"))]
  ""
  "
{
  emit_insn (gen_lwsync ());
  emit_move_insn (operands[0], operands[1]);
  DONE;
}")

; Some AIX assemblers don't accept lwsync, so we use a .long.
(define_insn "lwsync"
  [(set (mem:BLK (match_scratch 0 "X"))
	(unspec_volatile:BLK [(mem:BLK (match_scratch 1 "X"))] UNSPEC_LWSYNC))]
  ""
{
  if (TARGET_NO_LWSYNC)
    return "sync";
  else
    return ".long 0x7c2004ac";
}
  [(set_attr "type" "sync")])

