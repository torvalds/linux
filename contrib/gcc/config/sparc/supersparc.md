;; Scheduling description for SuperSPARC.
;;   Copyright (C) 2002 Free Software Foundation, Inc.
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

;; The SuperSPARC is a tri-issue, which was considered quite parallel
;; at the time it was released.  Much like UltraSPARC-I and UltraSPARC-II
;; there are two integer units but only one of them may take shifts.
;;
;; ??? If SuperSPARC has the same slotting rules as ultrasparc for these
;; ??? shifts, we should model that.

(define_automaton "supersparc_0,supersparc_1")

(define_cpu_unit "ss_memory, ss_shift, ss_iwport0, ss_iwport1" "supersparc_0")
(define_cpu_unit "ss_fpalu" "supersparc_0")
(define_cpu_unit "ss_fpmds" "supersparc_1")

(define_reservation "ss_iwport" "(ss_iwport0 | ss_iwport1)")

(define_insn_reservation "ss_iuload" 1
  (and (eq_attr "cpu" "supersparc")
    (eq_attr "type" "load,sload"))
  "ss_memory")

;; Ok, fpu loads deliver the result in zero cycles.  But we
;; have to show the ss_memory reservation somehow, thus...
(define_insn_reservation "ss_fpload" 0
  (and (eq_attr "cpu" "supersparc")
    (eq_attr "type" "fpload"))
  "ss_memory")

(define_bypass 0 "ss_fpload" "ss_fp_alu,ss_fp_mult,ss_fp_divs,ss_fp_divd,ss_fp_sqrt")

(define_insn_reservation "ss_store" 1
  (and (eq_attr "cpu" "supersparc")
    (eq_attr "type" "store,fpstore"))
  "ss_memory")

(define_insn_reservation "ss_ialu_shift" 1
  (and (eq_attr "cpu" "supersparc")
    (eq_attr "type" "shift"))
  "ss_shift + ss_iwport")

(define_insn_reservation "ss_ialu_any" 1
  (and (eq_attr "cpu" "supersparc")
    (eq_attr "type" "load,sload,store,shift,ialu"))
  "ss_iwport")

(define_insn_reservation "ss_fp_alu" 3
  (and (eq_attr "cpu" "supersparc")
    (eq_attr "type" "fp,fpmove,fpcmp"))
  "ss_fpalu, nothing*2")

(define_insn_reservation "ss_fp_mult" 3
  (and (eq_attr "cpu" "supersparc")
    (eq_attr "type" "fpmul"))
  "ss_fpmds, nothing*2")

(define_insn_reservation "ss_fp_divs" 6
  (and (eq_attr "cpu" "supersparc")
    (eq_attr "type" "fpdivs"))
  "ss_fpmds*4, nothing*2")

(define_insn_reservation "ss_fp_divd" 9
  (and (eq_attr "cpu" "supersparc")
    (eq_attr "type" "fpdivd"))
  "ss_fpmds*7, nothing*2")

(define_insn_reservation "ss_fp_sqrt" 12
  (and (eq_attr "cpu" "supersparc")
    (eq_attr "type" "fpsqrts,fpsqrtd"))
  "ss_fpmds*10, nothing*2")

(define_insn_reservation "ss_imul" 4
  (and (eq_attr "cpu" "supersparc")
    (eq_attr "type" "imul"))
  "ss_fpmds*4")
