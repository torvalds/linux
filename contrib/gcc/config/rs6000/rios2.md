;; Scheduling description for IBM Power2 processor.
;;   Copyright (C) 2003, 2004 Free Software Foundation, Inc.
;;
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

(define_automaton "rios2,rios2fp")
(define_cpu_unit "iu1_rios2,iu2_rios2" "rios2")
(define_cpu_unit "fpu1_rios2,fpu2_rios2" "rios2fp")
(define_cpu_unit "bpu_rios2" "rios2")

;; RIOS2 32-bit 2xIU, 2xFPU, BPU
;; IU1 can perform all integer operations
;; IU2 can perform all integer operations except imul and idiv

(define_insn_reservation "rios2-load" 2
  (and (eq_attr "type" "load,load_ext,load_ext_u,load_ext_ux,\
		        load_ux,load_u,fpload,fpload_ux,fpload_u,\
			load_l,store_c,sync")
       (eq_attr "cpu" "rios2"))
  "iu1_rios2|iu2_rios2")

(define_insn_reservation "rios2-store" 2
  (and (eq_attr "type" "store,store_ux,store_u,fpstore,fpstore_ux,fpstore_u")
       (eq_attr "cpu" "rios2"))
  "iu1_rios2|iu2_rios2")

(define_insn_reservation "rios2-integer" 1
  (and (eq_attr "type" "integer,insert_word")
       (eq_attr "cpu" "rios2"))
  "iu1_rios2|iu2_rios2")

(define_insn_reservation "rios2-two" 1
  (and (eq_attr "type" "two")
       (eq_attr "cpu" "rios2"))
  "iu1_rios2|iu2_rios2,iu1_rios2|iu2_rios2")

(define_insn_reservation "rios2-three" 1
  (and (eq_attr "type" "three")
       (eq_attr "cpu" "rios2"))
  "iu1_rios2|iu2_rios2,iu1_rios2|iu2_rios2,iu1_rios2|iu2_rios2")

(define_insn_reservation "rios2-imul" 2
  (and (eq_attr "type" "imul,imul2,imul3,imul_compare")
       (eq_attr "cpu" "rios2"))
  "iu1_rios2*2")

(define_insn_reservation "rios2-idiv" 13
  (and (eq_attr "type" "idiv")
       (eq_attr "cpu" "rios2"))
  "iu1_rios2*13")

; compare executes on integer unit, but feeds insns which
; execute on the branch unit.
(define_insn_reservation "rios2-compare" 3
  (and (eq_attr "type" "cmp,fast_compare,compare,delayed_compare")
       (eq_attr "cpu" "rios2"))
  "(iu1_rios2|iu2_rios2),nothing,bpu_rios2")

(define_insn_reservation "rios2-fp" 2
  (and (eq_attr "type" "fp")
       (eq_attr "cpu" "rios2"))
  "fpu1_rios2|fpu2_rios2")

(define_insn_reservation "rios2-fpcompare" 5
  (and (eq_attr "type" "fpcompare")
       (eq_attr "cpu" "rios2"))
  "(fpu1_rios2|fpu2_rios2),nothing*3,bpu_rios2")

(define_insn_reservation "rios2-dmul" 2
  (and (eq_attr "type" "dmul")
       (eq_attr "cpu" "rios2"))
  "fpu1_rios2|fpu2_rios2")

(define_insn_reservation "rios2-sdiv" 17
  (and (eq_attr "type" "sdiv,ddiv")
       (eq_attr "cpu" "rios2"))
  "(fpu1_rios2*17)|(fpu2_rios2*17)")

(define_insn_reservation "rios2-ssqrt" 26
  (and (eq_attr "type" "ssqrt,dsqrt")
       (eq_attr "cpu" "rios2"))
  "(fpu1_rios2*26)|(fpu2_rios2*26)")

(define_insn_reservation "rios2-mfcr" 2
  (and (eq_attr "type" "mfcr")
       (eq_attr "cpu" "rios2"))
  "iu1_rios2,bpu_rios2")

(define_insn_reservation "rios2-mtcr" 3
  (and (eq_attr "type" "mtcr")
       (eq_attr "cpu" "rios2"))
  "iu1_rios2,bpu_rios2")

(define_insn_reservation "rios2-crlogical" 3
  (and (eq_attr "type" "cr_logical,delayed_cr")
       (eq_attr "cpu" "rios2"))
  "bpu_rios2")

(define_insn_reservation "rios2-mtjmpr" 5
  (and (eq_attr "type" "mtjmpr")
       (eq_attr "cpu" "rios2"))
  "iu1_rios2,bpu_rios2")

(define_insn_reservation "rios2-mfjmpr" 2
  (and (eq_attr "type" "mfjmpr")
       (eq_attr "cpu" "rios2"))
  "iu1_rios2,bpu_rios2")

(define_insn_reservation "rios2-branch" 1
  (and (eq_attr "type" "jmpreg,branch,isync")
       (eq_attr "cpu" "rios2"))
  "bpu_rios2")

