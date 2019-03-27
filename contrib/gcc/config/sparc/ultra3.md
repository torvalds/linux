;; Scheduling description for UltraSPARC-III.
;;   Copyright (C) 2002, 2004 Free Software Foundation, Inc.
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

;; UltraSPARC-III is a quad-issue processor.
;;
;; It is also a much simpler beast than Ultra-I/II, no silly
;; slotting rules and both integer units are fully symmetric.
;; It does still have single-issue instructions though.

(define_automaton "ultrasparc3_0,ultrasparc3_1")

(define_cpu_unit "us3_ms,us3_br,us3_fpm" "ultrasparc3_0")
(define_cpu_unit "us3_a0,us3_a1,us3_slot0,\
                  us3_slot1,us3_slot2,us3_slot3,us3_fpa" "ultrasparc3_1")
(define_cpu_unit "us3_load_writeback" "ultrasparc3_1")

(define_reservation "us3_slotany" "(us3_slot0 | us3_slot1 | us3_slot2 | us3_slot3)")
(define_reservation "us3_single_issue" "us3_slot0 + us3_slot1 + us3_slot2 + us3_slot3")
(define_reservation "us3_ax" "(us3_a0 | us3_a1)")

(define_insn_reservation "us3_single" 1
  (and (eq_attr "cpu" "ultrasparc3")
    (eq_attr "type" "multi,savew,flushw,iflush,trap"))
  "us3_single_issue")

(define_insn_reservation "us3_integer" 1
  (and (eq_attr "cpu" "ultrasparc3")
    (eq_attr "type" "ialu,shift,compare"))
  "us3_ax + us3_slotany")

(define_insn_reservation "us3_ialuX" 5
  (and (eq_attr "cpu" "ultrasparc3")
    (eq_attr "type" "ialu,shift,compare"))
  "us3_single_issue*4, nothing")

(define_insn_reservation "us3_cmove" 2
  (and (eq_attr "cpu" "ultrasparc3")
    (eq_attr "type" "cmove"))
  "us3_ms + us3_br + us3_slotany, nothing")

;; ??? Not entirely accurate.
;; ??? It can run from 6 to 9 cycles.  The first cycle the MS pipe
;; ??? is needed, and the instruction group is broken right after
;; ??? the imul.  Then 'helper' instructions are generated to perform
;; ??? each further stage of the multiplication, each such 'helper' is
;; ??? single group.  So, the reservation aspect is represented accurately
;; ??? here, but the variable cycles are not.
;; ??? Currently I have no idea how to determine the variability, but once
;; ??? known we can simply add a define_bypass or similar to model it.
(define_insn_reservation "us3_imul" 7
  (and (eq_attr "cpu" "ultrasparc3")
    (eq_attr "type" "imul"))
  "us3_ms + us3_slotany, us3_single_issue*4, nothing*2")

(define_insn_reservation "us3_idiv" 72
  (and (eq_attr "cpu" "ultrasparc3")
    (eq_attr "type" "idiv"))
  "us3_ms + us3_slotany, us3_single_issue*69, nothing*2")

;; UltraSPARC-III has a similar load delay as UltraSPARC-I/II except
;; that all loads except 32-bit/64-bit unsigned loads take the extra
;; delay for sign/zero extension.
(define_insn_reservation "us3_2cycle_load" 2
  (and (eq_attr "cpu" "ultrasparc3")
    (and (eq_attr "type" "load,fpload")
      (eq_attr "us3load_type" "2cycle")))
  "us3_ms + us3_slotany, us3_load_writeback")

(define_insn_reservation "us3_load_delayed" 3
  (and (eq_attr "cpu" "ultrasparc3")
    (and (eq_attr "type" "load,sload")
      (eq_attr "us3load_type" "3cycle")))
  "us3_ms + us3_slotany, nothing, us3_load_writeback")

(define_insn_reservation "us3_store" 1
  (and (eq_attr "cpu" "ultrasparc3")
    (eq_attr "type" "store,fpstore"))
  "us3_ms + us3_slotany")

(define_insn_reservation "us3_branch" 1
  (and (eq_attr "cpu" "ultrasparc3")
    (eq_attr "type" "branch"))
  "us3_br + us3_slotany")

(define_insn_reservation "us3_call_jmpl" 1
  (and (eq_attr "cpu" "ultrasparc3")
    (eq_attr "type" "call,sibcall,call_no_delay_slot,uncond_branch"))
  "us3_br + us3_ms + us3_slotany")

(define_insn_reservation "us3_fmov" 3
  (and (eq_attr "cpu" "ultrasparc3")
    (eq_attr "type" "fpmove"))
  "us3_fpa + us3_slotany, nothing*2")

(define_insn_reservation "us3_fcmov" 3
  (and (eq_attr "cpu" "ultrasparc3")
    (eq_attr "type" "fpcmove"))
  "us3_fpa + us3_br + us3_slotany, nothing*2")

(define_insn_reservation "us3_fcrmov" 3
  (and (eq_attr "cpu" "ultrasparc3")
    (eq_attr "type" "fpcrmove"))
  "us3_fpa + us3_ms + us3_slotany, nothing*2")

(define_insn_reservation "us3_faddsub" 4
  (and (eq_attr "cpu" "ultrasparc3")
    (eq_attr "type" "fp"))
  "us3_fpa + us3_slotany, nothing*3")

(define_insn_reservation "us3_fpcmp" 5
  (and (eq_attr "cpu" "ultrasparc3")
    (eq_attr "type" "fpcmp"))
  "us3_fpa + us3_slotany, nothing*4")

(define_insn_reservation "us3_fmult" 4
 (and (eq_attr "cpu" "ultrasparc3")
    (eq_attr "type" "fpmul"))
  "us3_fpm + us3_slotany, nothing*3")

(define_insn_reservation "us3_fdivs" 17
  (and (eq_attr "cpu" "ultrasparc3")
    (eq_attr "type" "fpdivs"))
  "(us3_fpm + us3_slotany), us3_fpm*14, nothing*2")

(define_insn_reservation "us3_fsqrts" 20
  (and (eq_attr "cpu" "ultrasparc3")
    (eq_attr "type" "fpsqrts"))
  "(us3_fpm + us3_slotany), us3_fpm*17, nothing*2")

(define_insn_reservation "us3_fdivd" 20
  (and (eq_attr "cpu" "ultrasparc3")
    (eq_attr "type" "fpdivd"))
  "(us3_fpm + us3_slotany), us3_fpm*17, nothing*2")

(define_insn_reservation "us3_fsqrtd" 29
  (and (eq_attr "cpu" "ultrasparc3")
    (eq_attr "type" "fpsqrtd"))
  "(us3_fpm + us3_slotany), us3_fpm*26, nothing*2")

;; Any store may multi issue with the insn creating the source
;; data as long as that creating insn is not an FPU div/sqrt.
;; We need a special guard function because this bypass does
;; not apply to the address inputs of the store.
(define_bypass 0 "us3_integer,us3_faddsub,us3_fmov,us3_fcmov,us3_fmult" "us3_store"
   "store_data_bypass_p")

;; An integer branch may execute in the same cycle as the compare
;; creating the condition codes.
(define_bypass 0 "us3_integer" "us3_branch")

;; If FMOVfcc is user of FPCMP, latency is only 1 cycle.
(define_bypass 1 "us3_fpcmp" "us3_fcmov")

;; VIS scheduling
(define_insn_reservation "us3_fga"
  3
  (and (eq_attr "cpu" "ultrasparc3")
       (eq_attr "type" "fga"))
  "us3_fpa + us3_slotany, nothing*2")

(define_insn_reservation "us3_fgm"
  4
  (and (eq_attr "cpu" "ultrasparc3")
       (eq_attr "type" "fgm_pack,fgm_mul,fgm_cmp"))
  "us3_fpm + us3_slotany, nothing*3")

(define_insn_reservation "us3_pdist"
  4
  (and (eq_attr "cpu" "ultrasparc3")
       (eq_attr "type" "fgm_pdist"))
  "us3_fpm + us3_slotany, nothing*3")

(define_bypass 1 "us3_pdist" "us3_pdist")
