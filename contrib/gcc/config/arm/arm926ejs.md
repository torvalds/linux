;; ARM 926EJ-S Pipeline Description
;; Copyright (C) 2003 Free Software Foundation, Inc.
;; Written by CodeSourcery, LLC.
;;
;; This file is part of GCC.
;;
;; GCC is free software; you can redistribute it and/or modify it
;; under the terms of the GNU General Public License as published by
;; the Free Software Foundation; either version 2, or (at your option)
;; any later version.
;;
;; GCC is distributed in the hope that it will be useful, but
;; WITHOUT ANY WARRANTY; without even the implied warranty of
;; MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
;; General Public License for more details.
;;
;; You should have received a copy of the GNU General Public License
;; along with GCC; see the file COPYING.  If not, write to the Free
;; Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA
;; 02110-1301, USA.  */

;; These descriptions are based on the information contained in the
;; ARM926EJ-S Technical Reference Manual, Copyright (c) 2002 ARM
;; Limited.
;;

;; This automaton provides a pipeline description for the ARM
;; 926EJ-S core.
;;
;; The model given here assumes that the condition for all conditional
;; instructions is "true", i.e., that all of the instructions are
;; actually executed.

(define_automaton "arm926ejs")

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Pipelines
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

;; There is a single pipeline
;;
;;   The ALU pipeline has fetch, decode, execute, memory, and
;;   write stages. We only need to model the execute, memory and write
;;   stages.

(define_cpu_unit "e,m,w" "arm926ejs")

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; ALU Instructions
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

;; ALU instructions require three cycles to execute, and use the ALU
;; pipeline in each of the three stages.  The results are available
;; after the execute stage stage has finished.
;;
;; If the destination register is the PC, the pipelines are stalled
;; for several cycles.  That case is not modeled here.

;; ALU operations with no shifted operand
(define_insn_reservation "9_alu_op" 1 
 (and (eq_attr "tune" "arm926ejs")
      (eq_attr "type" "alu,alu_shift"))
 "e,m,w")

;; ALU operations with a shift-by-register operand
;; These really stall in the decoder, in order to read
;; the shift value in a second cycle. Pretend we take two cycles in
;; the execute stage.
(define_insn_reservation "9_alu_shift_reg_op" 2 
 (and (eq_attr "tune" "arm926ejs")
      (eq_attr "type" "alu_shift_reg"))
 "e*2,m,w")

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Multiplication Instructions
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

;; Multiplication instructions loop in the execute stage until the
;; instruction has been passed through the multiplier array enough
;; times. Multiply operations occur in both the execute and memory
;; stages of the pipeline

(define_insn_reservation "9_mult1" 3
 (and (eq_attr "tune" "arm926ejs")
      (eq_attr "insn" "smlalxy,mul,mla"))
 "e*2,m,w")

(define_insn_reservation "9_mult2" 4
 (and (eq_attr "tune" "arm926ejs")
      (eq_attr "insn" "muls,mlas"))
 "e*3,m,w")

(define_insn_reservation "9_mult3" 4
 (and (eq_attr "tune" "arm926ejs")
      (eq_attr "insn" "umull,umlal,smull,smlal"))
 "e*3,m,w")

(define_insn_reservation "9_mult4" 5
 (and (eq_attr "tune" "arm926ejs")
      (eq_attr "insn" "umulls,umlals,smulls,smlals"))
 "e*4,m,w")

(define_insn_reservation "9_mult5" 2
 (and (eq_attr "tune" "arm926ejs")
      (eq_attr "insn" "smulxy,smlaxy,smlawx"))
 "e,m,w")

(define_insn_reservation "9_mult6" 3
 (and (eq_attr "tune" "arm926ejs")
      (eq_attr "insn" "smlalxy"))
 "e*2,m,w")

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Load/Store Instructions
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

;; The models for load/store instructions do not accurately describe
;; the difference between operations with a base register writeback
;; (such as "ldm!").  These models assume that all memory references
;; hit in dcache.

;; Loads with a shifted offset take 3 cycles, and are (a) probably the
;; most common and (b) the pessimistic assumption will lead to fewer stalls.
(define_insn_reservation "9_load1_op" 3
 (and (eq_attr "tune" "arm926ejs")
      (eq_attr "type" "load1,load_byte"))
 "e*2,m,w")

(define_insn_reservation "9_store1_op" 0
 (and (eq_attr "tune" "arm926ejs")
      (eq_attr "type" "store1"))
 "e,m,w")

;; multiple word loads and stores
(define_insn_reservation "9_load2_op" 3
 (and (eq_attr "tune" "arm926ejs")
      (eq_attr "type" "load2"))
 "e,m*2,w")

(define_insn_reservation "9_load3_op" 4
 (and (eq_attr "tune" "arm926ejs")
      (eq_attr "type" "load3"))
 "e,m*3,w")

(define_insn_reservation "9_load4_op" 5
 (and (eq_attr "tune" "arm926ejs")
      (eq_attr "type" "load4"))
 "e,m*4,w")

(define_insn_reservation "9_store2_op" 0
 (and (eq_attr "tune" "arm926ejs")
      (eq_attr "type" "store2"))
 "e,m*2,w")

(define_insn_reservation "9_store3_op" 0
 (and (eq_attr "tune" "arm926ejs")
      (eq_attr "type" "store3"))
 "e,m*3,w")

(define_insn_reservation "9_store4_op" 0
 (and (eq_attr "tune" "arm926ejs")
      (eq_attr "type" "store4"))
 "e,m*4,w")

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Branch and Call Instructions
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

;; Branch instructions are difficult to model accurately.  The ARM
;; core can predict most branches.  If the branch is predicted
;; correctly, and predicted early enough, the branch can be completely
;; eliminated from the instruction stream.  Some branches can
;; therefore appear to require zero cycles to execute.  We assume that
;; all branches are predicted correctly, and that the latency is
;; therefore the minimum value.

(define_insn_reservation "9_branch_op" 0
 (and (eq_attr "tune" "arm926ejs")
      (eq_attr "type" "branch"))
 "nothing")

;; The latency for a call is not predictable.  Therefore, we use 32 as
;; roughly equivalent to positive infinity.

(define_insn_reservation "9_call_op" 32
 (and (eq_attr "tune" "arm926ejs")
      (eq_attr "type" "call"))
 "nothing")
