;; Scheduling description for SPARC Cypress.
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

;; The Cypress is a pretty simple single-issue processor.

(define_automaton "cypress_0,cypress_1")

(define_cpu_unit "cyp_memory, cyp_fpalu" "cypress_0")
(define_cpu_unit "cyp_fpmds" "cypress_1")

(define_insn_reservation "cyp_load" 2
  (and (eq_attr "cpu" "cypress")
    (eq_attr "type" "load,sload,fpload"))
  "cyp_memory, nothing")

(define_insn_reservation "cyp_fp_alu" 5
  (and (eq_attr "cpu" "cypress")
    (eq_attr "type" "fp,fpmove"))
  "cyp_fpalu, nothing*3")

(define_insn_reservation "cyp_fp_mult" 7
  (and (eq_attr "cpu" "cypress")
    (eq_attr "type" "fpmul"))
  "cyp_fpmds, nothing*5")

(define_insn_reservation "cyp_fp_div" 37
  (and (eq_attr "cpu" "cypress")
    (eq_attr "type" "fpdivs,fpdivd"))
  "cyp_fpmds, nothing*35")

(define_insn_reservation "cyp_fp_sqrt" 63
  (and (eq_attr "cpu" "cypress")
    (eq_attr "type" "fpsqrts,fpsqrtd"))
  "cyp_fpmds, nothing*61")
