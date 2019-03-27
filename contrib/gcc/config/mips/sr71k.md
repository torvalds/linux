;; .........................
;;
;; DFA-based pipeline description for Sandcraft SR3 (MIPS64 based)
;;
;; The SR3 is described as:
;;     - nine-stage pipeline, insn buffering with out-of-order issue to
;;       multiple function units, with an average dispatch rate of 2
;;       insn.s per cycle (max 6 insns: 2 fpu, 4 cpu).
;;
;;  The details on this are scant except for a diagram in
;;  Chap. 6 of Rev. 1.0 SR3 Spec.
;;
;;  The model employed below is designed to closely approximate the
;;  published latencies. Emulation of out-of-order issue and the insn
;;  buffering is done via a VLIW dispatch style (with a packing of 6 insns);
;;  the function unit reservations restrictions (define_*_set) are
;;  contrived to support published timings.
;;
;; Reference:
;;   "SR3 Microprocessor Specification, System development information,"
;;   Revision 1.0, 13 December 2000.
;;
;;
;; Reservation model is based on:
;;   1) Figure 6-1, from the 1.0 specification.
;;   2) Chapter 19, from the 1.0 specification.
;;   3) following questions(Red Hat)/answers(Sandcraft):
;;     RH> From Section 19.1
;;     RH>      1) In terms of figure 6-1, are all the instructions in
;;     RH>         table 19-1 restricted
;;     RH>         to ALUx? When ALUx is not in use for an instruction in table;;     RH>          19-1 is
;;     RH>         it fully compatible with all insns that issue to ALUy?
;;
;;     Yes, all the instructions in Table 19-1 only go to ALUX, and all the
;;     instructions that can be issued to ALUY can also be issued to ALUX.
;;
;;
;;     RH> From Section 19.2
;;     RH>      2) Explain conditional moves execution path (in terms of
;;     RH>      figure 6-1)
;;
;;     Conditional move of integer registers (based on floating point condition
;;     codes or integer register value) go to ALUX or ALUY.
;;
;;     RH>      3) Explain floating point store execution path (in terms of
;;     RH>      figure 6-1)
;;
;;     Floating point stores go to Ld/St and go to MOV in the floating point
;;     pipeline.
;;
;;     Floating point loads go to Ld/St and go to LOAD in the floating point
;;     pipeline.
;;
;;     RH>      4) Explain branch on floating condition (in terms of figure 6-1);;
;;     Branch on floating condition go to BRU.
;;
;;     RH>      5) Is the column for single RECIP instruction latency correct?
;;     RH>      What about for RSQRT single and double?
;;
;;     The latency/repeat for RECIP and RSQRT are correct.
;;

;;
;; Use four automata to isolate long latency operations, and to
;; reduce the complexity of cpu+fpu, reducing space.
;;
(define_automaton "sr71_cpu, sr71_cpu1, sr71_cp1, sr71_cp2, sr71_fextra, sr71_imacc")

;;  feeders for CPU function units and feeders for fpu (CP1 interface)
(define_cpu_unit "sr_iss0,sr_iss1,sr_iss2,sr_iss3,sr_iss4,sr_iss5" "sr71_cpu")

;; CPU function units
(define_cpu_unit "ipu_bru"       "sr71_cpu1")
(define_cpu_unit "ipu_alux"      "sr71_cpu1")
(define_cpu_unit "ipu_aluy"      "sr71_cpu1")
(define_cpu_unit "ipu_ldst"      "sr71_cpu1")
(define_cpu_unit "ipu_macc_iter" "sr71_imacc")


;; Floating-point unit (Co-processor interface 1).
(define_cpu_unit "fpu_mov"          "sr71_cp1")
(define_cpu_unit "fpu_load"         "sr71_cp1")
(define_cpu_unit "fpu_fpu"          "sr71_cp2")

;; fictitous unit to track long float insns with separate automaton
(define_cpu_unit "fpu_iter"         "sr71_fextra")


;;
;; Define common execution path (reservation) combinations
;;

;;
(define_reservation "cpu_iss"         "sr_iss0|sr_iss1|sr_iss2|sr_iss3")

;; two cycles are used for instruction using the fpu as it runs
;; at half the clock speed of the cpu. By adding an extra cycle
;; to the issue units, the default/minimum "repeat" dispatch delay is
;; accounted for all insn.s
(define_reservation "cp1_iss"         "(sr_iss4*2)|(sr_iss5*2)")

(define_reservation "serial_dispatch" "sr_iss0+sr_iss1+sr_iss2+sr_iss3+sr_iss4+sr_iss5")

;; Simulate a 6 insn VLIW dispatch, 1 cycle in dispatch followed by
;; reservation of function unit.
(define_reservation "ri_insns"         "cpu_iss,(ipu_alux|ipu_aluy)")
(define_reservation "ri_mem"           "cpu_iss,ipu_ldst")
(define_reservation "ri_alux"          "cpu_iss,ipu_alux")
(define_reservation "ri_branch"        "cpu_iss,ipu_bru")

(define_reservation "rf_insn"          "cp1_iss,fpu_fpu")
(define_reservation "rf_ldmem"         "cp1_iss,fpu_load")

; simultaneous reservation of pseudo-unit keeps cp1 fpu tied
; up until long cycle insn is finished...
(define_reservation "rf_multi1"        "rf_insn+fpu_iter")

;;
;; The ordering of the instruction-execution-path/resource-usage
;; descriptions (also known as reservation RTL) is roughly ordered
;; based on the define attribute RTL for the "type" classification.
;; When modifying, remember that the first test that matches is the
;; reservation used!
;;


(define_insn_reservation "ir_sr70_unknown" 1
  (and (eq_attr "cpu" "sr71000")
       (eq_attr "type" "unknown"))
  "serial_dispatch")


;; Assume prediction fails.
(define_insn_reservation "ir_sr70_branch" 6
  (and (eq_attr "cpu" "sr71000")
       (eq_attr "type" "branch,jump,call"))
  "ri_branch")

(define_insn_reservation "ir_sr70_load" 2
  (and (eq_attr "cpu" "sr71000")
       (eq_attr "type" "load"))
  "ri_mem")

(define_insn_reservation "ir_sr70_store" 1
  (and (eq_attr "cpu" "sr71000")
       (eq_attr "type" "store"))
  "ri_mem")


;;
;; float loads/stores flow through both cpu and cp1...
;;
(define_insn_reservation "ir_sr70_fload" 9
  (and (eq_attr "cpu" "sr71000")
       (eq_attr "type" "fpload,fpidxload"))
  "(cpu_iss+cp1_iss),(ri_mem+rf_ldmem)")

(define_insn_reservation "ir_sr70_fstore" 1
  (and (eq_attr "cpu" "sr71000")
       (eq_attr "type" "fpstore,fpidxstore"))
  "(cpu_iss+cp1_iss),(fpu_mov+ri_mem)")


;; This reservation is for conditional move based on integer
;; or floating point CC.
(define_insn_reservation "ir_sr70_condmove" 4
  (and (eq_attr "cpu" "sr71000")
       (eq_attr "type" "condmove"))
  "ri_insns")

;; Try to discriminate move-from-cp1 versus move-to-cp1 as latencies
;; are different. Like float load/store, these insns use multiple
;; resources simultaneously
(define_insn_reservation "ir_sr70_xfer_from" 6
  (and (eq_attr "cpu" "sr71000")
       (and (eq_attr "type" "xfer")
	    (eq_attr "mode" "!SF,DF,FPSW")))
  "(cpu_iss+cp1_iss),(fpu_mov+ri_mem)")

(define_insn_reservation "ir_sr70_xfer_to" 9
  (and (eq_attr "cpu" "sr71000")
       (and (eq_attr "type" "xfer")
	    (eq_attr "mode" "SF,DF")))
  "(cpu_iss+cp1_iss),(ri_mem+rf_ldmem)")

(define_insn_reservation "ir_sr70_hilo" 1
  (and (eq_attr "cpu" "sr71000")
       (eq_attr "type" "mthilo,mfhilo"))
  "ri_insns")

(define_insn_reservation "ir_sr70_arith" 1
  (and (eq_attr "cpu" "sr71000")
       (eq_attr "type" "arith,shift,slt,clz,const,trap"))
  "ri_insns")

;; emulate repeat (dispatch stall) by spending extra cycle(s) in
;; in iter unit
(define_insn_reservation "ir_sr70_imul_si" 4
  (and (eq_attr "cpu" "sr71000")
       (and (eq_attr "type" "imul,imul3,imadd")
	    (eq_attr "mode" "SI")))
  "ri_alux,ipu_alux,ipu_macc_iter")

(define_insn_reservation "ir_sr70_imul_di" 6
  (and (eq_attr "cpu" "sr71000")
       (and (eq_attr "type" "imul,imul3,imadd")
	    (eq_attr "mode" "DI")))
  "ri_alux,ipu_alux,(ipu_macc_iter*3)")

;; Divide algorithm is early out with best latency of 7 pcycles.
;; Use worst case for scheduling purposes.
(define_insn_reservation "ir_sr70_idiv_si" 41
  (and (eq_attr "cpu" "sr71000")
       (and (eq_attr "type" "idiv")
	    (eq_attr "mode" "SI")))
  "ri_alux,ipu_alux,(ipu_macc_iter*38)")

(define_insn_reservation "ir_sr70_idiv_di" 73
  (and (eq_attr "cpu" "sr71000")
       (and (eq_attr "type" "idiv")
	    (eq_attr "mode" "DI")))
  "ri_alux,ipu_alux,(ipu_macc_iter*70)")

;; extra reservations of fpu_fpu are for repeat latency
(define_insn_reservation "ir_sr70_fadd_sf" 8
  (and (eq_attr "cpu" "sr71000")
       (and (eq_attr "type" "fadd")
	    (eq_attr "mode" "SF")))
  "rf_insn,fpu_fpu")

(define_insn_reservation "ir_sr70_fadd_df" 10
  (and (eq_attr "cpu" "sr71000")
       (and (eq_attr "type" "fadd")
	    (eq_attr "mode" "DF")))
  "rf_insn,fpu_fpu")

;; Latencies for MADD,MSUB, NMADD, NMSUB assume the Multiply is fused
;; with the sub or add.
(define_insn_reservation "ir_sr70_fmul_sf" 8
  (and (eq_attr "cpu" "sr71000")
       (and (eq_attr "type" "fmul,fmadd")
	    (eq_attr "mode" "SF")))
  "rf_insn,fpu_fpu")

;; tie up the fpu unit to emulate the balance for the "repeat
;; rate" of 8 (2 are spent in the iss unit)
(define_insn_reservation "ir_sr70_fmul_df" 16
  (and (eq_attr "cpu" "sr71000")
       (and (eq_attr "type" "fmul,fmadd")
	    (eq_attr "mode" "DF")))
  "rf_insn,fpu_fpu*6")


;; RECIP insn uses same type attr as div, and for SR3, has same
;; timings for double. However, single RECIP has a latency of
;; 28 -- only way to fix this is to introduce new insn attrs.
;; cycles spent in iter unit are designed to satisfy balance
;; of "repeat" latency after insn uses up rf_multi1 reservation
(define_insn_reservation "ir_sr70_fdiv_sf" 60
  (and (eq_attr "cpu" "sr71000")
       (and (eq_attr "type" "fdiv,frdiv")
	    (eq_attr "mode" "SF")))
  "rf_multi1+(fpu_iter*51)")

(define_insn_reservation "ir_sr70_fdiv_df" 120
  (and (eq_attr "cpu" "sr71000")
       (and (eq_attr "type" "fdiv,frdiv")
	    (eq_attr "mode" "DF")))
  "rf_multi1+(fpu_iter*109)")

(define_insn_reservation "ir_sr70_fabs" 4
  (and (eq_attr "cpu" "sr71000")
       (eq_attr "type" "fabs,fneg,fmove"))
  "rf_insn,fpu_fpu")

(define_insn_reservation "ir_sr70_fcmp" 10
  (and (eq_attr "cpu" "sr71000")
       (eq_attr "type" "fcmp"))
  "rf_insn,fpu_fpu")

;; "fcvt" type attribute covers a number of diff insns, most have the same
;; latency descriptions, a few vary. We use the
;; most common timing (which is also worst case).
(define_insn_reservation "ir_sr70_fcvt" 12
  (and (eq_attr "cpu" "sr71000")
       (eq_attr "type" "fcvt"))
  "rf_insn,fpu_fpu*4")

(define_insn_reservation "ir_sr70_fsqrt_sf" 62
  (and (eq_attr "cpu" "sr71000")
       (and (eq_attr "type" "fsqrt")
	    (eq_attr "mode" "SF")))
  "rf_multi1+(fpu_iter*53)")

(define_insn_reservation "ir_sr70_fsqrt_df" 122
  (and (eq_attr "cpu" "sr71000")
       (and (eq_attr "type" "fsqrt")
	    (eq_attr "mode" "DF")))
  "rf_multi1+(fpu_iter*111)")

(define_insn_reservation "ir_sr70_frsqrt_sf" 48
  (and (eq_attr "cpu" "sr71000")
       (and (eq_attr "type" "frsqrt")
	    (eq_attr "mode" "SF")))
  "rf_multi1+(fpu_iter*39)")

(define_insn_reservation "ir_sr70_frsqrt_df" 240
  (and (eq_attr "cpu" "sr71000")
       (and (eq_attr "type" "frsqrt")
	    (eq_attr "mode" "DF")))
  "rf_multi1+(fpu_iter*229)")

(define_insn_reservation "ir_sr70_multi" 1
  (and (eq_attr "cpu" "sr71000")
       (eq_attr "type" "multi"))
  "serial_dispatch")

(define_insn_reservation "ir_sr70_nop" 1
  (and (eq_attr "cpu" "sr71000")
       (eq_attr "type" "nop"))
  "ri_insns")
