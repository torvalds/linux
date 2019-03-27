;;
;; DFA-based pipeline description for Broadcom SB-1
;;

;; The Broadcom SB-1 core is 4-way superscalar, in-order.  It has 2 load/store
;; pipes (one of which can support some ALU operations), 2 alu pipes, 2 FP
;; pipes, and 1 MDMX pipes.  It can issue 2 ls insns and 2 exe/fpu/mdmx insns
;; each cycle.

;; We model the 4-way issue by ordering unit choices.  The possible choices are
;; {ex1,fp1}|{ex0,fp0}|ls1|ls0.  Instructions issue to the first eligible unit
;; in the list in most cases.  Non-indexed load/stores issue to ls0 first.
;; simple alu operations issue to ls1 if it is still available, and their
;; operands are ready (no co-issue with loads), otherwise to the first
;; available ex unit.

;; When exceptions are enabled, can only issue FP insns to fp1.  This is
;; to ensure that instructions complete in order.  The -mfp-exceptions option
;; can be used to specify whether the system has FP exceptions enabled or not.

;; In 32-bit mode, dependent FP can't co-issue with load, and only one FP exe
;; insn can issue per cycle (fp1).

;; The A1 MDMX pipe is separate from the FP pipes, but uses the same register
;; file.  As a result, once an MDMX insn is issued, no FP insns can be issued
;; for 3 cycles.  When an FP insn is issued, no MDMX insn can be issued for
;; 5 cycles.  This is currently not handled because there is no MDMX insn
;; support as yet.

;;
;; We use two automata.  sb1_cpu_div is for the integer divides, which are
;; not pipelined.  sb1_cpu is for everything else.
;;
(define_automaton "sb1_cpu, sb1_cpu_div")

;; Load/store function units.
(define_cpu_unit "sb1_ls0" "sb1_cpu")
(define_cpu_unit "sb1_ls1" "sb1_cpu")

;; CPU function units.
(define_cpu_unit "sb1_ex0" "sb1_cpu")
(define_cpu_unit "sb1_ex1" "sb1_cpu")

;; The divide unit is not pipelined, and blocks hi/lo reads and writes.
(define_cpu_unit "sb1_div" "sb1_cpu_div")
;; DMULT block any multiply from issuing in the next cycle.
(define_cpu_unit "sb1_mul" "sb1_cpu")

;; Floating-point units.
(define_cpu_unit "sb1_fp0" "sb1_cpu")
(define_cpu_unit "sb1_fp1" "sb1_cpu")

;; Can only issue to one of the ex and fp pipes at a time.
(exclusion_set "sb1_ex0" "sb1_fp0")
(exclusion_set "sb1_ex1" "sb1_fp1")

;; Define an SB-1 specific attribute to simplify some FP descriptions.
;; We can use 2 FP pipes only if we have 64-bit FP code, and exceptions are
;; disabled.

(define_attr "sb1_fp_pipes" "one,two"
  (cond [(and (ne (symbol_ref "TARGET_FLOAT64") (const_int 0))
	      (eq (symbol_ref "TARGET_FP_EXCEPTIONS") (const_int 0)))
	 (const_string "two")]
	(const_string "one")))

;; Define reservations for common combinations.

;; For long cycle operations, the FPU has a 4 cycle pipeline that repeats,
;; effectively re-issuing the operation every 4 cycles.  This means that we
;; can have at most 4 long-cycle operations per pipe.

;; ??? The fdiv operations should be e.g.
;; sb1_fp1_4cycles*7" | "sb1_fp0_4cycle*7
;; but the DFA is too large when we do that.  Perhaps have to use scheduler
;; hooks here.

;; ??? Try limiting scheduler to 2 long latency operations, and see if this
;; results in a usable DFA, and whether it helps code performance.

;;(define_reservation "sb1_fp0_4cycles" "sb1_fp0, nothing*3")
;;(define_reservation "sb1_fp1_4cycles" "sb1_fp1, nothing*3")

;;
;; The ordering of the instruction-execution-path/resource-usage
;; descriptions (also known as reservation RTL) is roughly ordered
;; based on the define attribute RTL for the "type" classification.
;; When modifying, remember that the first test that matches is the
;; reservation used!
;;

(define_insn_reservation "ir_sb1_unknown" 1
  (and (eq_attr "cpu" "sb1,sb1a")
       (eq_attr "type" "unknown,multi"))
  "sb1_ls0+sb1_ls1+sb1_ex0+sb1_ex1+sb1_fp0+sb1_fp1")

;; predicted taken branch causes 2 cycle ifetch bubble.  predicted not
;; taken branch causes 0 cycle ifetch bubble.  mispredicted branch causes 8
;; cycle ifetch bubble.  We assume all branches predicted not taken.

;; ??? This assumption that branches are predicated not taken should be
;; investigated.  Maybe using 2 here will give better results.

(define_insn_reservation "ir_sb1_branch" 0
  (and (eq_attr "cpu" "sb1,sb1a")
       (eq_attr "type" "branch,jump,call"))
  "sb1_ex0")

;; ??? This is 1 cycle for ldl/ldr to ldl/ldr when they use the same data
;; register as destination.

;; ??? SB-1 can co-issue a load with a dependent arith insn if it executes on
;; an EX unit.  Can not co-issue if the dependent insn executes on an LS unit.
;; SB-1A can always co-issue here.

;; A load normally has a latency of zero cycles.  In some cases, dependent
;; insns can be issued in the same cycle.  However, a value of 1 gives
;; better performance in empirical testing.

(define_insn_reservation "ir_sb1_load" 1
  (and (eq_attr "cpu" "sb1")
       (eq_attr "type" "load,prefetch"))
  "sb1_ls0 | sb1_ls1")

(define_insn_reservation "ir_sb1a_load" 0
  (and (eq_attr "cpu" "sb1a")
       (eq_attr "type" "load,prefetch"))
  "sb1_ls0 | sb1_ls1")

;; Can not co-issue fpload with fp exe when in 32-bit mode.

(define_insn_reservation "ir_sb1_fpload" 0
  (and (eq_attr "cpu" "sb1,sb1a")
       (and (eq_attr "type" "fpload")
	    (ne (symbol_ref "TARGET_FLOAT64")
		(const_int 0))))
  "sb1_ls0 | sb1_ls1")

(define_insn_reservation "ir_sb1_fpload_32bitfp" 1
  (and (eq_attr "cpu" "sb1,sb1a")
       (and (eq_attr "type" "fpload")
	    (eq (symbol_ref "TARGET_FLOAT64")
		(const_int 0))))
  "sb1_ls0 | sb1_ls1")

;; Indexed loads can only execute on LS1 pipe.

(define_insn_reservation "ir_sb1_fpidxload" 0
  (and (eq_attr "cpu" "sb1,sb1a")
       (and (eq_attr "type" "fpidxload")
	    (ne (symbol_ref "TARGET_FLOAT64")
		(const_int 0))))
  "sb1_ls1")

(define_insn_reservation "ir_sb1_fpidxload_32bitfp" 1
  (and (eq_attr "cpu" "sb1,sb1a")
       (and (eq_attr "type" "fpidxload")
	    (eq (symbol_ref "TARGET_FLOAT64")
		(const_int 0))))
  "sb1_ls1")

;; prefx can only execute on the ls1 pipe.

(define_insn_reservation "ir_sb1_prefetchx" 0
  (and (eq_attr "cpu" "sb1,sb1a")
       (eq_attr "type" "prefetchx"))
  "sb1_ls1")

;; ??? There is a 4.5 cycle latency if a store is followed by a load, and
;; there is a RAW dependency.

(define_insn_reservation "ir_sb1_store" 1
  (and (eq_attr "cpu" "sb1,sb1a")
       (eq_attr "type" "store"))
  "sb1_ls0+sb1_ex1 | sb1_ls0+sb1_ex0 | sb1_ls1+sb1_ex1 | sb1_ls1+sb1_ex0")

(define_insn_reservation "ir_sb1_fpstore" 1
  (and (eq_attr "cpu" "sb1,sb1a")
       (eq_attr "type" "fpstore"))
  "sb1_ls0+sb1_fp1 | sb1_ls0+sb1_fp0 | sb1_ls1+sb1_fp1 | sb1_ls1+sb1_fp0")

;; Indexed stores can only execute on LS1 pipe.

(define_insn_reservation "ir_sb1_fpidxstore" 1
  (and (eq_attr "cpu" "sb1,sb1a")
       (eq_attr "type" "fpidxstore"))
  "sb1_ls1+sb1_fp1 | sb1_ls1+sb1_fp0")

;; Load latencies are 3 cycles for one load to another load or store (address
;; only).  This is 0 cycles for one load to a store using it as the data
;; written.

;; This assumes that if a load is dependent on a previous insn, then it must
;; be an address dependence.

(define_bypass 3
  "ir_sb1_load,ir_sb1a_load,ir_sb1_fpload,ir_sb1_fpload_32bitfp,
   ir_sb1_fpidxload,ir_sb1_fpidxload_32bitfp"
  "ir_sb1_load,ir_sb1a_load,ir_sb1_fpload,ir_sb1_fpload_32bitfp,
   ir_sb1_fpidxload,ir_sb1_fpidxload_32bitfp,ir_sb1_prefetchx")

(define_bypass 3
  "ir_sb1_load,ir_sb1a_load,ir_sb1_fpload,ir_sb1_fpload_32bitfp,
   ir_sb1_fpidxload,ir_sb1_fpidxload_32bitfp"
  "ir_sb1_store,ir_sb1_fpstore,ir_sb1_fpidxstore"
  "mips_store_data_bypass_p")

;; On SB-1, simple alu instructions can execute on the LS1 unit.

;; ??? A simple alu insn issued on an LS unit has 0 cycle latency to an EX
;; insn, to a store (for data), and to an xfer insn.  It has 1 cycle latency to
;; another LS insn (excluding store data).  A simple alu insn issued on an EX
;; unit has a latency of 5 cycles when the results goes to a LS unit (excluding
;; store data), otherwise a latency of 1 cycle.

;; ??? We cannot handle latencies properly for simple alu instructions
;; within the DFA pipeline model.  Latencies can be defined only from one
;; insn reservation to another.  We can't make them depend on which function
;; unit was used.  This isn't a DFA flaw.  There is a conflict here, as we
;; need to know the latency before we can determine which unit will be
;; available, but we need to know which unit it is issued to before we can
;; compute the latency.  Perhaps this can be handled via scheduler hooks.
;; This needs to be investigated.

;; ??? Optimal scheduling taking the LS units into account seems to require
;; a pre-scheduling pass.  We need to determine which instructions feed results
;; into store/load addresses, and thus benefit most from being issued to the
;; LS unit.  Also, we need to prune the list to ensure we don't overschedule
;; insns to the LS unit, and that we don't conflict with insns that need LS1
;; such as indexed loads.  We then need to emit nops to ensure that simple
;; alu instructions that are not supposed to be scheduled to LS1 don't
;; accidentally end up there because LS1 is free when they are issued.  This
;; will be a lot of work, and it isn't clear how useful it will be.

;; Empirical testing shows that 2 gives the best result.

(define_insn_reservation "ir_sb1_simple_alu" 2
  (and (eq_attr "cpu" "sb1")
       (eq_attr "type" "const,arith"))
  "sb1_ls1 | sb1_ex1 | sb1_ex0")

;; On SB-1A, simple alu instructions can not execute on the LS1 unit, and we
;; have none of the above problems.

(define_insn_reservation "ir_sb1a_simple_alu" 1
  (and (eq_attr "cpu" "sb1a")
       (eq_attr "type" "const,arith"))
  "sb1_ex1 | sb1_ex0")

;; ??? condmove also includes some FP instructions that execute on the FP
;; units.  This needs to be clarified.

(define_insn_reservation "ir_sb1_alu" 1
  (and (eq_attr "cpu" "sb1,sb1a")
       (eq_attr "type" "condmove,nop,shift"))
  "sb1_ex1 | sb1_ex0")

;; These are type arith/darith that only execute on the EX0 unit.

(define_insn_reservation "ir_sb1_alu_0" 1
  (and (eq_attr "cpu" "sb1,sb1a")
       (eq_attr "type" "slt,clz,trap"))
  "sb1_ex0")

;; An alu insn issued on an EX unit has a latency of 5 cycles when the
;; result goes to a LS unit (excluding store data).

;; This assumes that if a load is dependent on a previous insn, then it must
;; be an address dependence.

(define_bypass 5
  "ir_sb1a_simple_alu,ir_sb1_alu,ir_sb1_alu_0,ir_sb1_mfhi,ir_sb1_mflo"
  "ir_sb1_load,ir_sb1a_load,ir_sb1_fpload,ir_sb1_fpload_32bitfp,
   ir_sb1_fpidxload,ir_sb1_fpidxload_32bitfp,ir_sb1_prefetchx")

(define_bypass 5
  "ir_sb1a_simple_alu,ir_sb1_alu,ir_sb1_alu_0,ir_sb1_mfhi,ir_sb1_mflo"
  "ir_sb1_store,ir_sb1_fpstore,ir_sb1_fpidxstore"
  "mips_store_data_bypass_p")

;; mf{hi,lo} is 1 cycle.  

(define_insn_reservation "ir_sb1_mfhi" 1
  (and (eq_attr "cpu" "sb1,sb1a")
       (and (eq_attr "type" "mfhilo")
	    (not (match_operand 1 "lo_operand"))))
  "sb1_ex1")

(define_insn_reservation "ir_sb1_mflo" 1
  (and (eq_attr "cpu" "sb1,sb1a")
       (and (eq_attr "type" "mfhilo")
	    (match_operand 1 "lo_operand")))
  "sb1_ex1")

;; mt{hi,lo} to mul/div is 4 cycles.

(define_insn_reservation "ir_sb1_mthilo" 4
  (and (eq_attr "cpu" "sb1,sb1a")
       (eq_attr "type" "mthilo"))
  "sb1_ex1")

;; mt{hi,lo} to mf{hi,lo} is 3 cycles.

(define_bypass 3 "ir_sb1_mthilo" "ir_sb1_mfhi,ir_sb1_mflo")

;; multiply latency to an EX operation is 3 cycles.

;; ??? Should check whether we need to make multiply conflict with moves
;; to/from hilo registers.

(define_insn_reservation "ir_sb1_mulsi" 3
  (and (eq_attr "cpu" "sb1,sb1a")
       (and (eq_attr "type" "imul,imul3,imadd")
	    (eq_attr "mode" "SI")))
  "sb1_ex1+sb1_mul")

;; muldi to mfhi is 4 cycles.
;; Blocks any other multiply insn issue for 1 cycle.

(define_insn_reservation "ir_sb1_muldi" 4
  (and (eq_attr "cpu" "sb1,sb1a")
       (and (eq_attr "type" "imul,imul3")
	    (eq_attr "mode" "DI")))
  "sb1_ex1+sb1_mul, sb1_mul")

;; muldi to mflo is 3 cycles.

(define_bypass 3 "ir_sb1_muldi" "ir_sb1_mflo")

;;  mul latency is 7 cycles if the result is used by any LS insn.

;; This assumes that if a load is dependent on a previous insn, then it must
;; be an address dependence.

(define_bypass 7
  "ir_sb1_mulsi,ir_sb1_muldi"
  "ir_sb1_load,ir_sb1a_load,ir_sb1_fpload,ir_sb1_fpload_32bitfp,
   ir_sb1_fpidxload,ir_sb1_fpidxload_32bitfp,ir_sb1_prefetchx")

(define_bypass 7
  "ir_sb1_mulsi,ir_sb1_muldi"
  "ir_sb1_store,ir_sb1_fpstore,ir_sb1_fpidxstore"
  "mips_store_data_bypass_p")

;; The divide unit is not pipelined.  Divide busy is asserted in the 4th
;; cycle, and then deasserted on the latency cycle.  So only one divide at
;; a time, but the first/last 4 cycles can overlap.

;; ??? All divides block writes to hi/lo regs.  hi/lo regs are written 4 cycles
;; after the latency cycle for divides (e.g. 40/72).  dmult writes lo in
;; cycle 7, and hi in cycle 8.  All other insns write hi/lo regs in cycle 7.
;; Default for output dependencies is the difference in latencies, which is
;; only 1 cycle off here, e.g. div to mtlo stalls for 32 cycles, but should
;; stall for 33 cycles.  This does not seem significant enough to worry about.

(define_insn_reservation "ir_sb1_divsi" 36
  (and (eq_attr "cpu" "sb1,sb1a")
       (and (eq_attr "type" "idiv")
	    (eq_attr "mode" "SI")))
  "sb1_ex1, nothing*3, sb1_div*32")

(define_insn_reservation "ir_sb1_divdi" 68
  (and (eq_attr "cpu" "sb1,sb1a")
       (and (eq_attr "type" "idiv")
	    (eq_attr "mode" "DI")))
  "sb1_ex1, nothing*3, sb1_div*64")

(define_insn_reservation "ir_sb1_fpu_2pipes" 4
  (and (eq_attr "cpu" "sb1,sb1a")
       (and (eq_attr "type" "fmove,fadd,fmul,fabs,fneg,fcvt,frdiv1,frsqrt1")
	    (eq_attr "sb1_fp_pipes" "two")))
  "sb1_fp1 | sb1_fp0")

(define_insn_reservation "ir_sb1_fpu_1pipe" 4
  (and (eq_attr "cpu" "sb1,sb1a")
       (and (eq_attr "type" "fmove,fadd,fmul,fabs,fneg,fcvt,frdiv1,frsqrt1")
	    (eq_attr "sb1_fp_pipes" "one")))
  "sb1_fp1")

(define_insn_reservation "ir_sb1_fpu_step2_2pipes" 8
  (and (eq_attr "cpu" "sb1,sb1a")
       (and (eq_attr "type" "frdiv2,frsqrt2")
	    (eq_attr "sb1_fp_pipes" "two")))
  "sb1_fp1 | sb1_fp0")

(define_insn_reservation "ir_sb1_fpu_step2_1pipe" 8
  (and (eq_attr "cpu" "sb1,sb1a")
       (and (eq_attr "type" "frdiv2,frsqrt2")
	    (eq_attr "sb1_fp_pipes" "one")))
  "sb1_fp1")

;; ??? madd/msub 4-cycle latency to itself (same fr?), but 8 cycle latency
;; otherwise.

;; ??? Blocks issue of another non-madd/msub after 4 cycles.

(define_insn_reservation "ir_sb1_fmadd_2pipes" 8
  (and (eq_attr "cpu" "sb1,sb1a")
       (and (eq_attr "type" "fmadd")
	    (eq_attr "sb1_fp_pipes" "two")))
  "sb1_fp1 | sb1_fp0")

(define_insn_reservation "ir_sb1_fmadd_1pipe" 8
  (and (eq_attr "cpu" "sb1,sb1a")
       (and (eq_attr "type" "fmadd")
	    (eq_attr "sb1_fp_pipes" "one")))
  "sb1_fp1")

(define_insn_reservation "ir_sb1_fcmp" 4
  (and (eq_attr "cpu" "sb1,sb1a")
       (eq_attr "type" "fcmp"))
  "sb1_fp1")

;; mtc1 latency 5 cycles.

(define_insn_reservation "ir_sb1_mtxfer" 5
  (and (eq_attr "cpu" "sb1,sb1a")
       (and (eq_attr "type" "xfer")
	    (match_operand 0 "fpr_operand")))
  "sb1_fp0")

;; mfc1 latency 1 cycle.  

(define_insn_reservation "ir_sb1_mfxfer" 1
  (and (eq_attr "cpu" "sb1,sb1a")
       (and (eq_attr "type" "xfer")
	    (not (match_operand 0 "fpr_operand"))))
  "sb1_fp0")

;; ??? Can deliver at most 1 result per every 6 cycles because of issue
;; restrictions.

(define_insn_reservation "ir_sb1_divsf_2pipes" 24
  (and (eq_attr "cpu" "sb1,sb1a")
       (and (eq_attr "type" "fdiv")
	    (and (eq_attr "mode" "SF")
		 (eq_attr "sb1_fp_pipes" "two"))))
  "sb1_fp1 | sb1_fp0")

(define_insn_reservation "ir_sb1_divsf_1pipe" 24
  (and (eq_attr "cpu" "sb1,sb1a")
       (and (eq_attr "type" "fdiv")
	    (and (eq_attr "mode" "SF")
		 (eq_attr "sb1_fp_pipes" "one"))))
  "sb1_fp1")

;; ??? Can deliver at most 1 result per every 8 cycles because of issue
;; restrictions.

(define_insn_reservation "ir_sb1_divdf_2pipes" 32
  (and (eq_attr "cpu" "sb1,sb1a")
       (and (eq_attr "type" "fdiv")
	    (and (eq_attr "mode" "DF")
		 (eq_attr "sb1_fp_pipes" "two"))))
  "sb1_fp1 | sb1_fp0")

(define_insn_reservation "ir_sb1_divdf_1pipe" 32
  (and (eq_attr "cpu" "sb1,sb1a")
       (and (eq_attr "type" "fdiv")
	    (and (eq_attr "mode" "DF")
		 (eq_attr "sb1_fp_pipes" "one"))))
  "sb1_fp1")

;; ??? Can deliver at most 1 result per every 3 cycles because of issue
;; restrictions.

(define_insn_reservation "ir_sb1_recipsf_2pipes" 12
  (and (eq_attr "cpu" "sb1,sb1a")
       (and (eq_attr "type" "frdiv")
	    (and (eq_attr "mode" "SF")
		 (eq_attr "sb1_fp_pipes" "two"))))
  "sb1_fp1 | sb1_fp0")

(define_insn_reservation "ir_sb1_recipsf_1pipe" 12
  (and (eq_attr "cpu" "sb1,sb1a")
       (and (eq_attr "type" "frdiv")
	    (and (eq_attr "mode" "SF")
		 (eq_attr "sb1_fp_pipes" "one"))))
  "sb1_fp1")

;; ??? Can deliver at most 1 result per every 5 cycles because of issue
;; restrictions.

(define_insn_reservation "ir_sb1_recipdf_2pipes" 20
  (and (eq_attr "cpu" "sb1,sb1a")
       (and (eq_attr "type" "frdiv")
	    (and (eq_attr "mode" "DF")
		 (eq_attr "sb1_fp_pipes" "two"))))
  "sb1_fp1 | sb1_fp0")

(define_insn_reservation "ir_sb1_recipdf_1pipe" 20
  (and (eq_attr "cpu" "sb1,sb1a")
       (and (eq_attr "type" "frdiv")
	    (and (eq_attr "mode" "DF")
		 (eq_attr "sb1_fp_pipes" "one"))))
  "sb1_fp1")

;; ??? Can deliver at most 1 result per every 7 cycles because of issue
;; restrictions.

(define_insn_reservation "ir_sb1_sqrtsf_2pipes" 28
  (and (eq_attr "cpu" "sb1,sb1a")
       (and (eq_attr "type" "fsqrt")
	    (and (eq_attr "mode" "SF")
		 (eq_attr "sb1_fp_pipes" "two"))))
  "sb1_fp1 | sb1_fp0")

(define_insn_reservation "ir_sb1_sqrtsf_1pipe" 28
  (and (eq_attr "cpu" "sb1,sb1a")
       (and (eq_attr "type" "fsqrt")
	    (and (eq_attr "mode" "SF")
		 (eq_attr "sb1_fp_pipes" "one"))))
  "sb1_fp1")

;; ??? Can deliver at most 1 result per every 10 cycles because of issue
;; restrictions.

(define_insn_reservation "ir_sb1_sqrtdf_2pipes" 40
  (and (eq_attr "cpu" "sb1,sb1a")
       (and (eq_attr "type" "fsqrt")
	    (and (eq_attr "mode" "DF")
		 (eq_attr "sb1_fp_pipes" "two"))))
  "sb1_fp1 | sb1_fp0")

(define_insn_reservation "ir_sb1_sqrtdf_1pipe" 40
  (and (eq_attr "cpu" "sb1,sb1a")
       (and (eq_attr "type" "fsqrt")
	    (and (eq_attr "mode" "DF")
		 (eq_attr "sb1_fp_pipes" "one"))))
  "sb1_fp1")

;; ??? Can deliver at most 1 result per every 4 cycles because of issue
;; restrictions.

(define_insn_reservation "ir_sb1_rsqrtsf_2pipes" 16
  (and (eq_attr "cpu" "sb1,sb1a")
       (and (eq_attr "type" "frsqrt")
	    (and (eq_attr "mode" "SF")
		 (eq_attr "sb1_fp_pipes" "two"))))
  "sb1_fp1 | sb1_fp0")

(define_insn_reservation "ir_sb1_rsqrtsf_1pipe" 16
  (and (eq_attr "cpu" "sb1,sb1a")
       (and (eq_attr "type" "frsqrt")
	    (and (eq_attr "mode" "SF")
		 (eq_attr "sb1_fp_pipes" "one"))))
  "sb1_fp1")

;; ??? Can deliver at most 1 result per every 7 cycles because of issue
;; restrictions.

(define_insn_reservation "ir_sb1_rsqrtdf_2pipes" 28
  (and (eq_attr "cpu" "sb1,sb1a")
       (and (eq_attr "type" "frsqrt")
	    (and (eq_attr "mode" "DF")
		 (eq_attr "sb1_fp_pipes" "two"))))
  "sb1_fp1 | sb1_fp0")

(define_insn_reservation "ir_sb1_rsqrtdf_1pipe" 28
  (and (eq_attr "cpu" "sb1,sb1a")
       (and (eq_attr "type" "frsqrt")
	    (and (eq_attr "mode" "DF")
		 (eq_attr "sb1_fp_pipes" "one"))))
  "sb1_fp1")
