;; Geode Scheduling
;; Copyright (C) 2006
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
;;
;; The Geode architecture is one insn issue processor.
;;
;; This description is based on data from the following documents:
;;
;;    "AMD Geode GX Processor Data Book"
;;    Advanced Micro Devices, Inc., Aug 2005.
;;
;;    "AMD Geode LX Processor Data Book"
;;    Advanced Micro Devices, Inc., Jan 2006.
;;
;;
;; CPU execution units of the Geode:
;;
;; issue	describes the issue pipeline.
;; alu		describes the Integer unit
;; fpu		describes the FP unit
;;
;; The fp unit is out of order execution unit with register renaming.
;; There is also memory management unit and execution pipeline for
;; load/store operations.  We ignore it and difference between insns
;; using memory and registers.

(define_automaton "geode")

(define_cpu_unit "geode_issue,geode_alu,geode_fpu" "geode")

(define_insn_reservation "alu" 1
			 (and (eq_attr "cpu" "geode")
			      (eq_attr "type" "alu,alu1,negnot,icmp,lea,test,imov,imovx,icmov,incdec,setcc"))
			 "geode_issue,geode_alu")

(define_insn_reservation "shift" 2
			 (and (eq_attr "cpu" "geode")
			      (eq_attr "type" "ishift,ishift1,rotate,rotate1,cld"))
			 "geode_issue,geode_alu*2")

(define_insn_reservation "imul" 7
			 (and (eq_attr "cpu" "geode")
			      (eq_attr "type" "imul"))
			 "geode_issue,geode_alu*7")

(define_insn_reservation "idiv" 40
			 (and (eq_attr "cpu" "geode")
			      (eq_attr "type" "idiv"))
			 "geode_issue,geode_alu*40")

;; The branch unit.
(define_insn_reservation "call" 2
			 (and (eq_attr "cpu" "geode")
			      (eq_attr "type" "call,callv"))
			 "geode_issue,geode_alu*2")

(define_insn_reservation "geode_branch" 1
			 (and (eq_attr "cpu" "geode")
			      (eq_attr "type" "ibr"))
			 "geode_issue,geode_alu")

(define_insn_reservation "geode_pop_push" 1
			 (and (eq_attr "cpu" "geode")
			      (eq_attr "type" "pop,push"))
			 "geode_issue,geode_alu")

(define_insn_reservation "geode_leave" 2
			 (and (eq_attr "cpu" "geode")
			      (eq_attr "type" "leave"))
			 "geode_issue,geode_alu*2")

(define_insn_reservation "geode_load_str" 4
			 (and (eq_attr "cpu" "geode")
			      (and (eq_attr "type" "str")
				   (eq_attr "memory" "load,both")))
			 "geode_issue,geode_alu*4")

(define_insn_reservation "geode_store_str" 2
			 (and (eq_attr "cpu" "geode")
			      (and (eq_attr "type" "str")
				   (eq_attr "memory" "store")))
			 "geode_issue,geode_alu*2")

;; Be optimistic
(define_insn_reservation "geode_unknown" 1
			 (and (eq_attr "cpu" "geode")
			      (eq_attr "type" "multi,other"))
			 "geode_issue,geode_alu")

;; FPU

(define_insn_reservation "geode_fop" 6
			 (and (eq_attr "cpu" "geode")
			      (eq_attr "type" "fop,fcmp"))
			 "geode_issue,geode_fpu*6")

(define_insn_reservation "geode_fsimple" 1
			 (and (eq_attr "cpu" "geode")
			      (eq_attr "type" "fmov,fcmov,fsgn,fxch"))
			 "geode_issue,geode_fpu")

(define_insn_reservation "geode_fist" 4
			 (and (eq_attr "cpu" "geode")
			      (eq_attr "type" "fistp,fisttp"))
			 "geode_issue,geode_fpu*4")

(define_insn_reservation "geode_fmul" 10
			 (and (eq_attr "cpu" "geode")
			      (eq_attr "type" "fmul"))
			 "geode_issue,geode_fpu*10")

(define_insn_reservation "geode_fdiv" 47
			 (and (eq_attr "cpu" "geode")
			      (eq_attr "type" "fdiv"))
			 "geode_issue,geode_fpu*47")

;; We use minimal latency (fsin) here
(define_insn_reservation "geode_fpspc" 54
			 (and (eq_attr "cpu" "geode")
			      (eq_attr "type" "fpspc"))
			 "geode_issue,geode_fpu*54")

(define_insn_reservation "geode_frndint" 12
			 (and (eq_attr "cpu" "geode")
			      (eq_attr "type" "frndint"))
			 "geode_issue,geode_fpu*12")

(define_insn_reservation "geode_mmxmov" 1
			 (and (eq_attr "cpu" "geode")
			      (eq_attr "type" "mmxmov"))
			 "geode_issue,geode_fpu")

(define_insn_reservation "geode_mmx" 2
			 (and (eq_attr "cpu" "geode")
			      (eq_attr "type" "mmx,mmxadd,mmxmul,mmxcmp,mmxcvt,mmxshft"))
			 "geode_issue,geode_fpu*2")
