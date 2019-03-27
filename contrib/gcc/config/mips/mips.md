;;  Mips.md	     Machine Description for MIPS based processors
;;  Copyright (C) 1989, 1990, 1991, 1992, 1993, 1994, 1995, 1996, 1997, 1998,
;;  1999, 2000, 2001, 2002, 2003, 2004, 2005 Free Software Foundation, Inc.
;;  Contributed by   A. Lichnewsky, lich@inria.inria.fr
;;  Changes by       Michael Meissner, meissner@osf.org
;;  64 bit r4000 support by Ian Lance Taylor, ian@cygnus.com, and
;;  Brendan Eich, brendan@microunity.com.

;; This file is part of GCC.

;; GCC is free software; you can redistribute it and/or modify
;; it under the terms of the GNU General Public License as published by
;; the Free Software Foundation; either version 2, or (at your option)
;; any later version.

;; GCC is distributed in the hope that it will be useful,
;; but WITHOUT ANY WARRANTY; without even the implied warranty of
;; MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
;; GNU General Public License for more details.

;; You should have received a copy of the GNU General Public License
;; along with GCC; see the file COPYING.  If not, write to
;; the Free Software Foundation, 51 Franklin Street, Fifth Floor,
;; Boston, MA 02110-1301, USA.

(define_constants
  [(UNSPEC_LOAD_DF_LOW		 0)
   (UNSPEC_LOAD_DF_HIGH		 1)
   (UNSPEC_STORE_DF_HIGH	 2)
   (UNSPEC_GET_FNADDR		 3)
   (UNSPEC_BLOCKAGE		 4)
   (UNSPEC_CPRESTORE		 5)
   (UNSPEC_EH_RECEIVER		 6)
   (UNSPEC_EH_RETURN		 7)
   (UNSPEC_CONSTTABLE_INT	 8)
   (UNSPEC_CONSTTABLE_FLOAT	 9)
   (UNSPEC_ALIGN		14)
   (UNSPEC_HIGH			17)
   (UNSPEC_LOAD_LEFT		18)
   (UNSPEC_LOAD_RIGHT		19)
   (UNSPEC_STORE_LEFT		20)
   (UNSPEC_STORE_RIGHT		21)
   (UNSPEC_LOADGP		22)
   (UNSPEC_LOAD_CALL		23)
   (UNSPEC_LOAD_GOT		24)
   (UNSPEC_GP			25)
   (UNSPEC_MFHILO		26)
   (UNSPEC_TLS_LDM		27)
   (UNSPEC_TLS_GET_TP		28)

   (UNSPEC_ADDRESS_FIRST	100)

   (FAKE_CALL_REGNO		79)

   ;; For MIPS Paired-Singled Floating Point Instructions.

   (UNSPEC_MOVE_TF_PS		200)
   (UNSPEC_C			201)

   ;; MIPS64/MIPS32R2 alnv.ps
   (UNSPEC_ALNV_PS		202)

   ;; MIPS-3D instructions
   (UNSPEC_CABS			203)

   (UNSPEC_ADDR_PS		204)
   (UNSPEC_CVT_PW_PS		205)
   (UNSPEC_CVT_PS_PW		206)
   (UNSPEC_MULR_PS		207)
   (UNSPEC_ABS_PS		208)

   (UNSPEC_RSQRT1		209)
   (UNSPEC_RSQRT2		210)
   (UNSPEC_RECIP1		211)
   (UNSPEC_RECIP2		212)
   (UNSPEC_SINGLE_CC		213)
   (UNSPEC_SCC			214)

   ;; MIPS DSP ASE Revision 0.98 3/24/2005
   (UNSPEC_ADDQ			300)
   (UNSPEC_ADDQ_S		301)
   (UNSPEC_SUBQ			302)
   (UNSPEC_SUBQ_S		303)
   (UNSPEC_ADDSC		304)
   (UNSPEC_ADDWC		305)
   (UNSPEC_MODSUB		306)
   (UNSPEC_RADDU_W_QB		307)
   (UNSPEC_ABSQ_S		308)
   (UNSPEC_PRECRQ_QB_PH		309)
   (UNSPEC_PRECRQ_PH_W		310)
   (UNSPEC_PRECRQ_RS_PH_W	311)
   (UNSPEC_PRECRQU_S_QB_PH	312)
   (UNSPEC_PRECEQ_W_PHL		313)
   (UNSPEC_PRECEQ_W_PHR		314)
   (UNSPEC_PRECEQU_PH_QBL	315)
   (UNSPEC_PRECEQU_PH_QBR	316)
   (UNSPEC_PRECEQU_PH_QBLA	317)
   (UNSPEC_PRECEQU_PH_QBRA	318)
   (UNSPEC_PRECEU_PH_QBL	319)
   (UNSPEC_PRECEU_PH_QBR	320)
   (UNSPEC_PRECEU_PH_QBLA	321)
   (UNSPEC_PRECEU_PH_QBRA	322)
   (UNSPEC_SHLL			323)
   (UNSPEC_SHLL_S		324)
   (UNSPEC_SHRL_QB		325)
   (UNSPEC_SHRA_PH		326)
   (UNSPEC_SHRA_R		327)
   (UNSPEC_MULEU_S_PH_QBL	328)
   (UNSPEC_MULEU_S_PH_QBR	329)
   (UNSPEC_MULQ_RS_PH		330)
   (UNSPEC_MULEQ_S_W_PHL	331)
   (UNSPEC_MULEQ_S_W_PHR	332)
   (UNSPEC_DPAU_H_QBL		333)
   (UNSPEC_DPAU_H_QBR		334)
   (UNSPEC_DPSU_H_QBL		335)
   (UNSPEC_DPSU_H_QBR		336)
   (UNSPEC_DPAQ_S_W_PH		337)
   (UNSPEC_DPSQ_S_W_PH		338)
   (UNSPEC_MULSAQ_S_W_PH	339)
   (UNSPEC_DPAQ_SA_L_W		340)
   (UNSPEC_DPSQ_SA_L_W		341)
   (UNSPEC_MAQ_S_W_PHL		342)
   (UNSPEC_MAQ_S_W_PHR		343)
   (UNSPEC_MAQ_SA_W_PHL		344)
   (UNSPEC_MAQ_SA_W_PHR		345)
   (UNSPEC_BITREV		346)
   (UNSPEC_INSV			347)
   (UNSPEC_REPL_QB		348)
   (UNSPEC_REPL_PH		349)
   (UNSPEC_CMP_EQ		350)
   (UNSPEC_CMP_LT		351)
   (UNSPEC_CMP_LE		352)
   (UNSPEC_CMPGU_EQ_QB		353)
   (UNSPEC_CMPGU_LT_QB		354)
   (UNSPEC_CMPGU_LE_QB		355)
   (UNSPEC_PICK			356)
   (UNSPEC_PACKRL_PH		357)
   (UNSPEC_EXTR_W		358)
   (UNSPEC_EXTR_R_W		359)
   (UNSPEC_EXTR_RS_W		360)
   (UNSPEC_EXTR_S_H		361)
   (UNSPEC_EXTP			362)
   (UNSPEC_EXTPDP		363)
   (UNSPEC_SHILO		364)
   (UNSPEC_MTHLIP		365)
   (UNSPEC_WRDSP		366)
   (UNSPEC_RDDSP		367)
  ]
)

(include "predicates.md")
(include "constraints.md")

;; ....................
;;
;;	Attributes
;;
;; ....................

(define_attr "got" "unset,xgot_high,load"
  (const_string "unset"))

;; For jal instructions, this attribute is DIRECT when the target address
;; is symbolic and INDIRECT when it is a register.
(define_attr "jal" "unset,direct,indirect"
  (const_string "unset"))

;; This attribute is YES if the instruction is a jal macro (not a
;; real jal instruction).
;;
;; jal is always a macro for o32 and o64 abicalls because it includes an
;; instruction to restore $gp.  Direct jals are also macros for -mshared
;; abicalls because they first load the target address into $25.
(define_attr "jal_macro" "no,yes"
  (cond [(eq_attr "jal" "direct")
	 (symbol_ref "TARGET_ABICALLS
		      && (TARGET_OLDABI || !TARGET_ABSOLUTE_ABICALLS)")
	 (eq_attr "jal" "indirect")
	 (symbol_ref "TARGET_ABICALLS && TARGET_OLDABI")]
	(const_string "no")))

;; Classification of each insn.
;; branch	conditional branch
;; jump		unconditional jump
;; call		unconditional call
;; load		load instruction(s)
;; fpload	floating point load
;; fpidxload    floating point indexed load
;; store	store instruction(s)
;; fpstore	floating point store
;; fpidxstore	floating point indexed store
;; prefetch	memory prefetch (register + offset)
;; prefetchx	memory indexed prefetch (register + register)
;; condmove	conditional moves
;; xfer		transfer to/from coprocessor
;; mthilo	transfer to hi/lo registers
;; mfhilo	transfer from hi/lo registers
;; const	load constant
;; arith	integer arithmetic and logical instructions
;; shift	integer shift instructions
;; slt		set less than instructions
;; clz		the clz and clo instructions
;; trap		trap if instructions
;; imul		integer multiply 2 operands
;; imul3	integer multiply 3 operands
;; imadd	integer multiply-add
;; idiv		integer divide
;; fmove	floating point register move
;; fadd		floating point add/subtract
;; fmul		floating point multiply
;; fmadd	floating point multiply-add
;; fdiv		floating point divide
;; frdiv	floating point reciprocal divide
;; frdiv1	floating point reciprocal divide step 1
;; frdiv2	floating point reciprocal divide step 2
;; fabs		floating point absolute value
;; fneg		floating point negation
;; fcmp		floating point compare
;; fcvt		floating point convert
;; fsqrt	floating point square root
;; frsqrt       floating point reciprocal square root
;; frsqrt1      floating point reciprocal square root step1
;; frsqrt2      floating point reciprocal square root step2
;; multi	multiword sequence (or user asm statements)
;; nop		no operation
(define_attr "type"
  "unknown,branch,jump,call,load,fpload,fpidxload,store,fpstore,fpidxstore,prefetch,prefetchx,condmove,xfer,mthilo,mfhilo,const,arith,shift,slt,clz,trap,imul,imul3,imadd,idiv,fmove,fadd,fmul,fmadd,fdiv,frdiv,frdiv1,frdiv2,fabs,fneg,fcmp,fcvt,fsqrt,frsqrt,frsqrt1,frsqrt2,multi,nop"
  (cond [(eq_attr "jal" "!unset") (const_string "call")
	 (eq_attr "got" "load") (const_string "load")]
	(const_string "unknown")))

;; Main data type used by the insn
(define_attr "mode" "unknown,none,QI,HI,SI,DI,SF,DF,FPSW"
  (const_string "unknown"))

;; Mode for conversion types (fcvt)
;; I2S          integer to float single (SI/DI to SF)
;; I2D          integer to float double (SI/DI to DF)
;; S2I          float to integer (SF to SI/DI)
;; D2I          float to integer (DF to SI/DI)
;; D2S          double to float single
;; S2D          float single to double

(define_attr "cnv_mode" "unknown,I2S,I2D,S2I,D2I,D2S,S2D" 
  (const_string "unknown"))

;; Is this an extended instruction in mips16 mode?
(define_attr "extended_mips16" "no,yes"
  (const_string "no"))

;; Length of instruction in bytes.
(define_attr "length" ""
   (cond [;; Direct branch instructions have a range of [-0x40000,0x3fffc].
	  ;; If a branch is outside this range, we have a choice of two
	  ;; sequences.  For PIC, an out-of-range branch like:
	  ;;
	  ;;	bne	r1,r2,target
	  ;;	dslot
	  ;;
	  ;; becomes the equivalent of:
	  ;;
	  ;;	beq	r1,r2,1f
	  ;;	dslot
	  ;;	la	$at,target
	  ;;	jr	$at
	  ;;	nop
	  ;; 1:
	  ;;
	  ;; where the load address can be up to three instructions long
	  ;; (lw, nop, addiu).
	  ;;
	  ;; The non-PIC case is similar except that we use a direct
	  ;; jump instead of an la/jr pair.  Since the target of this
	  ;; jump is an absolute 28-bit bit address (the other bits
	  ;; coming from the address of the delay slot) this form cannot
	  ;; cross a 256MB boundary.  We could provide the option of
	  ;; using la/jr in this case too, but we do not do so at
	  ;; present.
	  ;;
	  ;; Note that this value does not account for the delay slot
	  ;; instruction, whose length is added separately.  If the RTL
	  ;; pattern has no explicit delay slot, mips_adjust_insn_length
	  ;; will add the length of the implicit nop.  The values for
	  ;; forward and backward branches will be different as well.
	  (eq_attr "type" "branch")
	  (cond [(and (le (minus (match_dup 1) (pc)) (const_int 131064))
                      (le (minus (pc) (match_dup 1)) (const_int 131068)))
                  (const_int 4)
		 (ne (symbol_ref "flag_pic") (const_int 0))
		 (const_int 24)
		 ] (const_int 12))

	  (eq_attr "got" "load")
	  (const_int 4)
	  (eq_attr "got" "xgot_high")
	  (const_int 8)

	  (eq_attr "type" "const")
	  (symbol_ref "mips_const_insns (operands[1]) * 4")
	  (eq_attr "type" "load,fpload")
	  (symbol_ref "mips_fetch_insns (operands[1]) * 4")
	  (eq_attr "type" "store,fpstore")
	  (symbol_ref "mips_fetch_insns (operands[0]) * 4")

	  ;; In the worst case, a call macro will take 8 instructions:
	  ;;
	  ;;	 lui $25,%call_hi(FOO)
	  ;;	 addu $25,$25,$28
	  ;;     lw $25,%call_lo(FOO)($25)
	  ;;	 nop
	  ;;	 jalr $25
	  ;;	 nop
	  ;;	 lw $gp,X($sp)
	  ;;	 nop
	  (eq_attr "jal_macro" "yes")
	  (const_int 32)

	  (and (eq_attr "extended_mips16" "yes")
	       (ne (symbol_ref "TARGET_MIPS16") (const_int 0)))
	  (const_int 8)

	  ;; Various VR4120 errata require a nop to be inserted after a macc
	  ;; instruction.  The assembler does this for us, so account for
	  ;; the worst-case length here.
	  (and (eq_attr "type" "imadd")
	       (ne (symbol_ref "TARGET_FIX_VR4120") (const_int 0)))
	  (const_int 8)

	  ;; VR4120 errata MD(4): if there are consecutive dmult instructions,
	  ;; the result of the second one is missed.  The assembler should work
	  ;; around this by inserting a nop after the first dmult.
	  (and (eq_attr "type" "imul,imul3")
	       (and (eq_attr "mode" "DI")
		    (ne (symbol_ref "TARGET_FIX_VR4120") (const_int 0))))
	  (const_int 8)

	  (eq_attr "type" "idiv")
	  (symbol_ref "mips_idiv_insns () * 4")
	  ] (const_int 4)))

;; Attribute describing the processor.  This attribute must match exactly
;; with the processor_type enumeration in mips.h.
(define_attr "cpu"
  "r3000,4kc,4kp,5kc,5kf,20kc,24k,24kx,m4k,r3900,r6000,r4000,r4100,r4111,r4120,r4130,r4300,r4600,r4650,r5000,r5400,r5500,r7000,r8000,r9000,sb1,sb1a,sr71000"
  (const (symbol_ref "mips_tune")))

;; The type of hardware hazard associated with this instruction.
;; DELAY means that the next instruction cannot read the result
;; of this one.  HILO means that the next two instructions cannot
;; write to HI or LO.
(define_attr "hazard" "none,delay,hilo"
  (cond [(and (eq_attr "type" "load,fpload,fpidxload")
	      (ne (symbol_ref "ISA_HAS_LOAD_DELAY") (const_int 0)))
	 (const_string "delay")

	 (and (eq_attr "type" "xfer")
	      (ne (symbol_ref "ISA_HAS_XFER_DELAY") (const_int 0)))
	 (const_string "delay")

	 (and (eq_attr "type" "fcmp")
	      (ne (symbol_ref "ISA_HAS_FCMP_DELAY") (const_int 0)))
	 (const_string "delay")

	 ;; The r4000 multiplication patterns include an mflo instruction.
	 (and (eq_attr "type" "imul")
	      (ne (symbol_ref "TARGET_FIX_R4000") (const_int 0)))
	 (const_string "hilo")

	 (and (eq_attr "type" "mfhilo")
	      (eq (symbol_ref "ISA_HAS_HILO_INTERLOCKS") (const_int 0)))
	 (const_string "hilo")]
	(const_string "none")))

;; Is it a single instruction?
(define_attr "single_insn" "no,yes"
  (symbol_ref "get_attr_length (insn) == (TARGET_MIPS16 ? 2 : 4)"))

;; Can the instruction be put into a delay slot?
(define_attr "can_delay" "no,yes"
  (if_then_else (and (eq_attr "type" "!branch,call,jump")
		     (and (eq_attr "hazard" "none")
			  (eq_attr "single_insn" "yes")))
		(const_string "yes")
		(const_string "no")))

;; Attribute defining whether or not we can use the branch-likely instructions
(define_attr "branch_likely" "no,yes"
  (const
   (if_then_else (ne (symbol_ref "GENERATE_BRANCHLIKELY") (const_int 0))
		 (const_string "yes")
		 (const_string "no"))))

;; True if an instruction might assign to hi or lo when reloaded.
;; This is used by the TUNE_MACC_CHAINS code.
(define_attr "may_clobber_hilo" "no,yes"
  (if_then_else (eq_attr "type" "imul,imul3,imadd,idiv,mthilo")
		(const_string "yes")
		(const_string "no")))

;; Describe a user's asm statement.
(define_asm_attributes
  [(set_attr "type" "multi")
   (set_attr "can_delay" "no")])

;; This mode macro allows 32-bit and 64-bit GPR patterns to be generated
;; from the same template.
(define_mode_macro GPR [SI (DI "TARGET_64BIT")])

;; This mode macro allows :P to be used for patterns that operate on
;; pointer-sized quantities.  Exactly one of the two alternatives will match.
(define_mode_macro P [(SI "Pmode == SImode") (DI "Pmode == DImode")])

;; This mode macro allows :MOVECC to be used anywhere that a
;; conditional-move-type condition is needed.
(define_mode_macro MOVECC [SI (DI "TARGET_64BIT") (CC "TARGET_HARD_FLOAT")])

;; This mode macro allows the QI and HI extension patterns to be defined from
;; the same template.
(define_mode_macro SHORT [QI HI])

;; This mode macro allows :ANYF to be used wherever a scalar or vector
;; floating-point mode is allowed.
(define_mode_macro ANYF [(SF "TARGET_HARD_FLOAT")
			 (DF "TARGET_HARD_FLOAT && TARGET_DOUBLE_FLOAT")
			 (V2SF "TARGET_PAIRED_SINGLE_FLOAT")])

;; Like ANYF, but only applies to scalar modes.
(define_mode_macro SCALARF [(SF "TARGET_HARD_FLOAT")
			    (DF "TARGET_HARD_FLOAT && TARGET_DOUBLE_FLOAT")])

;; In GPR templates, a string like "<d>subu" will expand to "subu" in the
;; 32-bit version and "dsubu" in the 64-bit version.
(define_mode_attr d [(SI "") (DI "d")])

;; This attribute gives the length suffix for a sign- or zero-extension
;; instruction.
(define_mode_attr size [(QI "b") (HI "h")])

;; This attributes gives the mode mask of a SHORT.
(define_mode_attr mask [(QI "0x00ff") (HI "0xffff")])

;; Mode attributes for GPR loads and stores.
(define_mode_attr load [(SI "lw") (DI "ld")])
(define_mode_attr store [(SI "sw") (DI "sd")])

;; Similarly for MIPS IV indexed FPR loads and stores.
(define_mode_attr loadx [(SF "lwxc1") (DF "ldxc1") (V2SF "ldxc1")])
(define_mode_attr storex [(SF "swxc1") (DF "sdxc1") (V2SF "sdxc1")])

;; The unextended ranges of the MIPS16 addiu and daddiu instructions
;; are different.  Some forms of unextended addiu have an 8-bit immediate
;; field but the equivalent daddiu has only a 5-bit field.
(define_mode_attr si8_di5 [(SI "8") (DI "5")])

;; This attribute gives the best constraint to use for registers of
;; a given mode.
(define_mode_attr reg [(SI "d") (DI "d") (CC "z")])

;; This attribute gives the format suffix for floating-point operations.
(define_mode_attr fmt [(SF "s") (DF "d") (V2SF "ps")])

;; This attribute gives the upper-case mode name for one unit of a
;; floating-point mode.
(define_mode_attr UNITMODE [(SF "SF") (DF "DF") (V2SF "SF")])

;; This attribute works around the early SB-1 rev2 core "F2" erratum:
;;
;; In certain cases, div.s and div.ps may have a rounding error
;; and/or wrong inexact flag.
;;
;; Therefore, we only allow div.s if not working around SB-1 rev2
;; errata or if a slight loss of precision is OK.
(define_mode_attr divide_condition
  [DF (SF "!TARGET_FIX_SB1 || flag_unsafe_math_optimizations")
   (V2SF "TARGET_SB1 && (!TARGET_FIX_SB1 || flag_unsafe_math_optimizations)")])

; This attribute gives the condition for which sqrt instructions exist.
(define_mode_attr sqrt_condition
  [(SF "!ISA_MIPS1") (DF "!ISA_MIPS1") (V2SF "TARGET_SB1")])

; This attribute gives the condition for which recip and rsqrt instructions
; exist.
(define_mode_attr recip_condition
  [(SF "ISA_HAS_FP4") (DF "ISA_HAS_FP4") (V2SF "TARGET_SB1")])

;; This code macro allows all branch instructions to be generated from
;; a single define_expand template.
(define_code_macro any_cond [unordered ordered unlt unge uneq ltgt unle ungt
			     eq ne gt ge lt le gtu geu ltu leu])

;; This code macro allows signed and unsigned widening multiplications
;; to use the same template.
(define_code_macro any_extend [sign_extend zero_extend])

;; This code macro allows the three shift instructions to be generated
;; from the same template.
(define_code_macro any_shift [ashift ashiftrt lshiftrt])

;; This code macro allows all native floating-point comparisons to be
;; generated from the same template.
(define_code_macro fcond [unordered uneq unlt unle eq lt le])

;; This code macro is used for comparisons that can be implemented
;; by swapping the operands.
(define_code_macro swapped_fcond [ge gt unge ungt])

;; <u> expands to an empty string when doing a signed operation and
;; "u" when doing an unsigned operation.
(define_code_attr u [(sign_extend "") (zero_extend "u")])

;; <su> is like <u>, but the signed form expands to "s" rather than "".
(define_code_attr su [(sign_extend "s") (zero_extend "u")])

;; <optab> expands to the name of the optab for a particular code.
(define_code_attr optab [(ashift "ashl")
			 (ashiftrt "ashr")
			 (lshiftrt "lshr")])

;; <insn> expands to the name of the insn that implements a particular code.
(define_code_attr insn [(ashift "sll")
			(ashiftrt "sra")
			(lshiftrt "srl")])

;; <fcond> is the c.cond.fmt condition associated with a particular code.
(define_code_attr fcond [(unordered "un")
			 (uneq "ueq")
			 (unlt "ult")
			 (unle "ule")
			 (eq "eq")
			 (lt "lt")
			 (le "le")])

;; Similar, but for swapped conditions.
(define_code_attr swapped_fcond [(ge "le")
				 (gt "lt")
				 (unge "ule")
				 (ungt "ult")])

;; .........................
;;
;;	Branch, call and jump delay slots
;;
;; .........................

(define_delay (and (eq_attr "type" "branch")
		   (eq (symbol_ref "TARGET_MIPS16") (const_int 0)))
  [(eq_attr "can_delay" "yes")
   (nil)
   (and (eq_attr "branch_likely" "yes")
	(eq_attr "can_delay" "yes"))])

(define_delay (eq_attr "type" "jump")
  [(eq_attr "can_delay" "yes")
   (nil)
   (nil)])

(define_delay (and (eq_attr "type" "call")
		   (eq_attr "jal_macro" "no"))
  [(eq_attr "can_delay" "yes")
   (nil)
   (nil)])

;; Pipeline descriptions.
;;
;; generic.md provides a fallback for processors without a specific
;; pipeline description.  It is derived from the old define_function_unit
;; version and uses the "alu" and "imuldiv" units declared below.
;;
;; Some of the processor-specific files are also derived from old
;; define_function_unit descriptions and simply override the parts of
;; generic.md that don't apply.  The other processor-specific files
;; are self-contained.
(define_automaton "alu,imuldiv")

(define_cpu_unit "alu" "alu")
(define_cpu_unit "imuldiv" "imuldiv")

(include "4k.md")
(include "5k.md")
(include "24k.md")
(include "3000.md")
(include "4000.md")
(include "4100.md")
(include "4130.md")
(include "4300.md")
(include "4600.md")
(include "5000.md")
(include "5400.md")
(include "5500.md")
(include "6000.md")
(include "7000.md")
(include "9000.md")
(include "sb1.md")
(include "sr71k.md")
(include "generic.md")

;;
;;  ....................
;;
;;	CONDITIONAL TRAPS
;;
;;  ....................
;;

(define_insn "trap"
  [(trap_if (const_int 1) (const_int 0))]
  ""
{
  if (ISA_HAS_COND_TRAP)
    return "teq\t$0,$0";
  else if (TARGET_MIPS16)
    return "break 0";
  else
    return "break";
}
  [(set_attr "type" "trap")])

(define_expand "conditional_trap"
  [(trap_if (match_operator 0 "comparison_operator"
			    [(match_dup 2) (match_dup 3)])
	    (match_operand 1 "const_int_operand"))]
  "ISA_HAS_COND_TRAP"
{
  if (GET_MODE_CLASS (GET_MODE (cmp_operands[0])) == MODE_INT
      && operands[1] == const0_rtx)
    {
      mips_gen_conditional_trap (operands);
      DONE;
    }
  else
    FAIL;
})

(define_insn "*conditional_trap<mode>"
  [(trap_if (match_operator:GPR 0 "trap_comparison_operator"
				[(match_operand:GPR 1 "reg_or_0_operand" "dJ")
				 (match_operand:GPR 2 "arith_operand" "dI")])
	    (const_int 0))]
  "ISA_HAS_COND_TRAP"
  "t%C0\t%z1,%2"
  [(set_attr "type" "trap")])

;;
;;  ....................
;;
;;	ADDITION
;;
;;  ....................
;;

(define_insn "add<mode>3"
  [(set (match_operand:ANYF 0 "register_operand" "=f")
	(plus:ANYF (match_operand:ANYF 1 "register_operand" "f")
		   (match_operand:ANYF 2 "register_operand" "f")))]
  ""
  "add.<fmt>\t%0,%1,%2"
  [(set_attr "type" "fadd")
   (set_attr "mode" "<UNITMODE>")])

(define_expand "add<mode>3"
  [(set (match_operand:GPR 0 "register_operand")
	(plus:GPR (match_operand:GPR 1 "register_operand")
		  (match_operand:GPR 2 "arith_operand")))]
  "")

(define_insn "*add<mode>3"
  [(set (match_operand:GPR 0 "register_operand" "=d,d")
	(plus:GPR (match_operand:GPR 1 "register_operand" "d,d")
		  (match_operand:GPR 2 "arith_operand" "d,Q")))]
  "!TARGET_MIPS16"
  "@
    <d>addu\t%0,%1,%2
    <d>addiu\t%0,%1,%2"
  [(set_attr "type" "arith")
   (set_attr "mode" "<MODE>")])

;; We need to recognize MIPS16 stack pointer additions explicitly, since
;; we don't have a constraint for $sp.  These insns will be generated by
;; the save_restore_insns functions.

(define_insn "*add<mode>3_sp1"
  [(set (reg:GPR 29)
	(plus:GPR (reg:GPR 29)
		  (match_operand:GPR 0 "const_arith_operand" "")))]
  "TARGET_MIPS16"
  "<d>addiu\t%$,%$,%0"
  [(set_attr "type" "arith")
   (set_attr "mode" "<MODE>")
   (set (attr "length") (if_then_else (match_operand 0 "m16_simm8_8")
				      (const_int 4)
				      (const_int 8)))])

(define_insn "*add<mode>3_sp2"
  [(set (match_operand:GPR 0 "register_operand" "=d")
	(plus:GPR (reg:GPR 29)
		  (match_operand:GPR 1 "const_arith_operand" "")))]
  "TARGET_MIPS16"
  "<d>addiu\t%0,%$,%1"
  [(set_attr "type" "arith")
   (set_attr "mode" "<MODE>")
   (set (attr "length") (if_then_else (match_operand 1 "m16_uimm<si8_di5>_4")
				      (const_int 4)
				      (const_int 8)))])

(define_insn "*add<mode>3_mips16"
  [(set (match_operand:GPR 0 "register_operand" "=d,d,d")
	(plus:GPR (match_operand:GPR 1 "register_operand" "0,d,d")
		  (match_operand:GPR 2 "arith_operand" "Q,O,d")))]
  "TARGET_MIPS16"
  "@
    <d>addiu\t%0,%2
    <d>addiu\t%0,%1,%2
    <d>addu\t%0,%1,%2"
  [(set_attr "type" "arith")
   (set_attr "mode" "<MODE>")
   (set_attr_alternative "length"
		[(if_then_else (match_operand 2 "m16_simm<si8_di5>_1")
			       (const_int 4)
			       (const_int 8))
		 (if_then_else (match_operand 2 "m16_simm4_1")
			       (const_int 4)
			       (const_int 8))
		 (const_int 4)])])


;; On the mips16, we can sometimes split an add of a constant which is
;; a 4 byte instruction into two adds which are both 2 byte
;; instructions.  There are two cases: one where we are adding a
;; constant plus a register to another register, and one where we are
;; simply adding a constant to a register.

(define_split
  [(set (match_operand:SI 0 "register_operand")
	(plus:SI (match_dup 0)
		 (match_operand:SI 1 "const_int_operand")))]
  "TARGET_MIPS16 && reload_completed && !TARGET_DEBUG_D_MODE
   && REG_P (operands[0])
   && M16_REG_P (REGNO (operands[0]))
   && GET_CODE (operands[1]) == CONST_INT
   && ((INTVAL (operands[1]) > 0x7f
	&& INTVAL (operands[1]) <= 0x7f + 0x7f)
       || (INTVAL (operands[1]) < - 0x80
	   && INTVAL (operands[1]) >= - 0x80 - 0x80))"
  [(set (match_dup 0) (plus:SI (match_dup 0) (match_dup 1)))
   (set (match_dup 0) (plus:SI (match_dup 0) (match_dup 2)))]
{
  HOST_WIDE_INT val = INTVAL (operands[1]);

  if (val >= 0)
    {
      operands[1] = GEN_INT (0x7f);
      operands[2] = GEN_INT (val - 0x7f);
    }
  else
    {
      operands[1] = GEN_INT (- 0x80);
      operands[2] = GEN_INT (val + 0x80);
    }
})

(define_split
  [(set (match_operand:SI 0 "register_operand")
	(plus:SI (match_operand:SI 1 "register_operand")
		 (match_operand:SI 2 "const_int_operand")))]
  "TARGET_MIPS16 && reload_completed && !TARGET_DEBUG_D_MODE
   && REG_P (operands[0])
   && M16_REG_P (REGNO (operands[0]))
   && REG_P (operands[1])
   && M16_REG_P (REGNO (operands[1]))
   && REGNO (operands[0]) != REGNO (operands[1])
   && GET_CODE (operands[2]) == CONST_INT
   && ((INTVAL (operands[2]) > 0x7
	&& INTVAL (operands[2]) <= 0x7 + 0x7f)
       || (INTVAL (operands[2]) < - 0x8
	   && INTVAL (operands[2]) >= - 0x8 - 0x80))"
  [(set (match_dup 0) (plus:SI (match_dup 1) (match_dup 2)))
   (set (match_dup 0) (plus:SI (match_dup 0) (match_dup 3)))]
{
  HOST_WIDE_INT val = INTVAL (operands[2]);

  if (val >= 0)
    {
      operands[2] = GEN_INT (0x7);
      operands[3] = GEN_INT (val - 0x7);
    }
  else
    {
      operands[2] = GEN_INT (- 0x8);
      operands[3] = GEN_INT (val + 0x8);
    }
})

(define_split
  [(set (match_operand:DI 0 "register_operand")
	(plus:DI (match_dup 0)
		 (match_operand:DI 1 "const_int_operand")))]
  "TARGET_MIPS16 && TARGET_64BIT && reload_completed && !TARGET_DEBUG_D_MODE
   && REG_P (operands[0])
   && M16_REG_P (REGNO (operands[0]))
   && GET_CODE (operands[1]) == CONST_INT
   && ((INTVAL (operands[1]) > 0xf
	&& INTVAL (operands[1]) <= 0xf + 0xf)
       || (INTVAL (operands[1]) < - 0x10
	   && INTVAL (operands[1]) >= - 0x10 - 0x10))"
  [(set (match_dup 0) (plus:DI (match_dup 0) (match_dup 1)))
   (set (match_dup 0) (plus:DI (match_dup 0) (match_dup 2)))]
{
  HOST_WIDE_INT val = INTVAL (operands[1]);

  if (val >= 0)
    {
      operands[1] = GEN_INT (0xf);
      operands[2] = GEN_INT (val - 0xf);
    }
  else
    {
      operands[1] = GEN_INT (- 0x10);
      operands[2] = GEN_INT (val + 0x10);
    }
})

(define_split
  [(set (match_operand:DI 0 "register_operand")
	(plus:DI (match_operand:DI 1 "register_operand")
		 (match_operand:DI 2 "const_int_operand")))]
  "TARGET_MIPS16 && TARGET_64BIT && reload_completed && !TARGET_DEBUG_D_MODE
   && REG_P (operands[0])
   && M16_REG_P (REGNO (operands[0]))
   && REG_P (operands[1])
   && M16_REG_P (REGNO (operands[1]))
   && REGNO (operands[0]) != REGNO (operands[1])
   && GET_CODE (operands[2]) == CONST_INT
   && ((INTVAL (operands[2]) > 0x7
	&& INTVAL (operands[2]) <= 0x7 + 0xf)
       || (INTVAL (operands[2]) < - 0x8
	   && INTVAL (operands[2]) >= - 0x8 - 0x10))"
  [(set (match_dup 0) (plus:DI (match_dup 1) (match_dup 2)))
   (set (match_dup 0) (plus:DI (match_dup 0) (match_dup 3)))]
{
  HOST_WIDE_INT val = INTVAL (operands[2]);

  if (val >= 0)
    {
      operands[2] = GEN_INT (0x7);
      operands[3] = GEN_INT (val - 0x7);
    }
  else
    {
      operands[2] = GEN_INT (- 0x8);
      operands[3] = GEN_INT (val + 0x8);
    }
})

(define_insn "*addsi3_extended"
  [(set (match_operand:DI 0 "register_operand" "=d,d")
	(sign_extend:DI
	     (plus:SI (match_operand:SI 1 "register_operand" "d,d")
		      (match_operand:SI 2 "arith_operand" "d,Q"))))]
  "TARGET_64BIT && !TARGET_MIPS16"
  "@
    addu\t%0,%1,%2
    addiu\t%0,%1,%2"
  [(set_attr "type" "arith")
   (set_attr "mode" "SI")])

;; Split this insn so that the addiu splitters can have a crack at it.
;; Use a conservative length estimate until the split.
(define_insn_and_split "*addsi3_extended_mips16"
  [(set (match_operand:DI 0 "register_operand" "=d,d,d")
	(sign_extend:DI
	     (plus:SI (match_operand:SI 1 "register_operand" "0,d,d")
		      (match_operand:SI 2 "arith_operand" "Q,O,d"))))]
  "TARGET_64BIT && TARGET_MIPS16"
  "#"
  "&& reload_completed"
  [(set (match_dup 3) (plus:SI (match_dup 1) (match_dup 2)))]
  { operands[3] = gen_lowpart (SImode, operands[0]); }
  [(set_attr "type" "arith")
   (set_attr "mode" "SI")
   (set_attr "extended_mips16" "yes")])

;;
;;  ....................
;;
;;	SUBTRACTION
;;
;;  ....................
;;

(define_insn "sub<mode>3"
  [(set (match_operand:ANYF 0 "register_operand" "=f")
	(minus:ANYF (match_operand:ANYF 1 "register_operand" "f")
		    (match_operand:ANYF 2 "register_operand" "f")))]
  ""
  "sub.<fmt>\t%0,%1,%2"
  [(set_attr "type" "fadd")
   (set_attr "mode" "<UNITMODE>")])

(define_insn "sub<mode>3"
  [(set (match_operand:GPR 0 "register_operand" "=d")
	(minus:GPR (match_operand:GPR 1 "register_operand" "d")
		   (match_operand:GPR 2 "register_operand" "d")))]
  ""
  "<d>subu\t%0,%1,%2"
  [(set_attr "type" "arith")
   (set_attr "mode" "<MODE>")])

(define_insn "*subsi3_extended"
  [(set (match_operand:DI 0 "register_operand" "=d")
	(sign_extend:DI
	    (minus:SI (match_operand:SI 1 "register_operand" "d")
		      (match_operand:SI 2 "register_operand" "d"))))]
  "TARGET_64BIT"
  "subu\t%0,%1,%2"
  [(set_attr "type" "arith")
   (set_attr "mode" "DI")])

;;
;;  ....................
;;
;;	MULTIPLICATION
;;
;;  ....................
;;

(define_expand "mul<mode>3"
  [(set (match_operand:SCALARF 0 "register_operand")
	(mult:SCALARF (match_operand:SCALARF 1 "register_operand")
		      (match_operand:SCALARF 2 "register_operand")))]
  ""
  "")

(define_insn "*mul<mode>3"
  [(set (match_operand:SCALARF 0 "register_operand" "=f")
	(mult:SCALARF (match_operand:SCALARF 1 "register_operand" "f")
		      (match_operand:SCALARF 2 "register_operand" "f")))]
  "!TARGET_4300_MUL_FIX"
  "mul.<fmt>\t%0,%1,%2"
  [(set_attr "type" "fmul")
   (set_attr "mode" "<MODE>")])

;; Early VR4300 silicon has a CPU bug where multiplies with certain
;; operands may corrupt immediately following multiplies. This is a
;; simple fix to insert NOPs.

(define_insn "*mul<mode>3_r4300"
  [(set (match_operand:SCALARF 0 "register_operand" "=f")
	(mult:SCALARF (match_operand:SCALARF 1 "register_operand" "f")
		      (match_operand:SCALARF 2 "register_operand" "f")))]
  "TARGET_4300_MUL_FIX"
  "mul.<fmt>\t%0,%1,%2\;nop"
  [(set_attr "type" "fmul")
   (set_attr "mode" "<MODE>")
   (set_attr "length" "8")])

(define_insn "mulv2sf3"
  [(set (match_operand:V2SF 0 "register_operand" "=f")
	(mult:V2SF (match_operand:V2SF 1 "register_operand" "f")
		   (match_operand:V2SF 2 "register_operand" "f")))]
  "TARGET_PAIRED_SINGLE_FLOAT"
  "mul.ps\t%0,%1,%2"
  [(set_attr "type" "fmul")
   (set_attr "mode" "SF")])

;; The original R4000 has a cpu bug.  If a double-word or a variable
;; shift executes while an integer multiplication is in progress, the
;; shift may give an incorrect result.  Avoid this by keeping the mflo
;; with the mult on the R4000.
;;
;; From "MIPS R4000PC/SC Errata, Processor Revision 2.2 and 3.0"
;; (also valid for MIPS R4000MC processors):
;;
;; "16. R4000PC, R4000SC: Please refer to errata 28 for an update to
;;	this errata description.
;;	The following code sequence causes the R4000 to incorrectly
;;	execute the Double Shift Right Arithmetic 32 (dsra32)
;;	instruction.  If the dsra32 instruction is executed during an
;;	integer multiply, the dsra32 will only shift by the amount in
;;	specified in the instruction rather than the amount plus 32
;;	bits.
;;	instruction 1:		mult	rs,rt		integer multiply
;;	instruction 2-12:	dsra32	rd,rt,rs	doubleword shift
;;							right arithmetic + 32
;;	Workaround: A dsra32 instruction placed after an integer
;;	multiply should not be one of the 11 instructions after the
;;	multiply instruction."
;;
;; and:
;;
;; "28. R4000PC, R4000SC: The text from errata 16 should be replaced by
;;	the following description.
;;	All extended shifts (shift by n+32) and variable shifts (32 and
;;	64-bit versions) may produce incorrect results under the
;;	following conditions:
;;	1) An integer multiply is currently executing
;;	2) These types of shift instructions are executed immediately
;;	   following an integer divide instruction.
;;	Workaround:
;;	1) Make sure no integer multiply is running wihen these
;;	   instruction are executed.  If this cannot be predicted at
;;	   compile time, then insert a "mfhi" to R0 instruction
;;	   immediately after the integer multiply instruction.  This
;;	   will cause the integer multiply to complete before the shift
;;	   is executed.
;;	2) Separate integer divide and these two classes of shift
;;	   instructions by another instruction or a noop."
;;
;; These processors have PRId values of 0x00004220 and 0x00004300,
;; respectively.

(define_expand "mul<mode>3"
  [(set (match_operand:GPR 0 "register_operand")
	(mult:GPR (match_operand:GPR 1 "register_operand")
		  (match_operand:GPR 2 "register_operand")))]
  ""
{
  if (GENERATE_MULT3_<MODE>)
    emit_insn (gen_mul<mode>3_mult3 (operands[0], operands[1], operands[2]));
  else if (!TARGET_FIX_R4000)
    emit_insn (gen_mul<mode>3_internal (operands[0], operands[1],
					operands[2]));
  else
    emit_insn (gen_mul<mode>3_r4000 (operands[0], operands[1], operands[2]));
  DONE;
})

(define_insn "mulsi3_mult3"
  [(set (match_operand:SI 0 "register_operand" "=d,l")
	(mult:SI (match_operand:SI 1 "register_operand" "d,d")
		 (match_operand:SI 2 "register_operand" "d,d")))
   (clobber (match_scratch:SI 3 "=h,h"))
   (clobber (match_scratch:SI 4 "=l,X"))]
  "GENERATE_MULT3_SI"
{
  if (which_alternative == 1)
    return "mult\t%1,%2";
  if (TARGET_MAD
      || TARGET_MIPS5400
      || TARGET_MIPS5500
      || TARGET_MIPS7000
      || TARGET_MIPS9000
      || ISA_MIPS32
      || ISA_MIPS32R2
      || ISA_MIPS64
      || ISA_MIPS64R2)
    return "mul\t%0,%1,%2";
  return "mult\t%0,%1,%2";
}
  [(set_attr "type" "imul3,imul")
   (set_attr "mode" "SI")])

(define_insn "muldi3_mult3"
  [(set (match_operand:DI 0 "register_operand" "=d")
	(mult:DI (match_operand:DI 1 "register_operand" "d")
		 (match_operand:DI 2 "register_operand" "d")))
   (clobber (match_scratch:DI 3 "=h"))
   (clobber (match_scratch:DI 4 "=l"))]
  "TARGET_64BIT && GENERATE_MULT3_DI"
  "dmult\t%0,%1,%2"
  [(set_attr "type" "imul3")
   (set_attr "mode" "DI")])

;; If a register gets allocated to LO, and we spill to memory, the reload
;; will include a move from LO to a GPR.  Merge it into the multiplication
;; if it can set the GPR directly.
;;
;; Operand 0: LO
;; Operand 1: GPR (1st multiplication operand)
;; Operand 2: GPR (2nd multiplication operand)
;; Operand 3: HI
;; Operand 4: GPR (destination)
(define_peephole2
  [(parallel
       [(set (match_operand:SI 0 "register_operand")
	     (mult:SI (match_operand:SI 1 "register_operand")
		      (match_operand:SI 2 "register_operand")))
        (clobber (match_operand:SI 3 "register_operand"))
        (clobber (scratch:SI))])
   (set (match_operand:SI 4 "register_operand")
	(unspec [(match_dup 0) (match_dup 3)] UNSPEC_MFHILO))]
  "GENERATE_MULT3_SI && peep2_reg_dead_p (2, operands[0])"
  [(parallel
       [(set (match_dup 4)
	     (mult:SI (match_dup 1)
		      (match_dup 2)))
        (clobber (match_dup 3))
        (clobber (match_dup 0))])])

(define_insn "mul<mode>3_internal"
  [(set (match_operand:GPR 0 "register_operand" "=l")
	(mult:GPR (match_operand:GPR 1 "register_operand" "d")
		  (match_operand:GPR 2 "register_operand" "d")))
   (clobber (match_scratch:GPR 3 "=h"))]
  "!TARGET_FIX_R4000"
  "<d>mult\t%1,%2"
  [(set_attr "type" "imul")
   (set_attr "mode" "<MODE>")])

(define_insn "mul<mode>3_r4000"
  [(set (match_operand:GPR 0 "register_operand" "=d")
	(mult:GPR (match_operand:GPR 1 "register_operand" "d")
		  (match_operand:GPR 2 "register_operand" "d")))
   (clobber (match_scratch:GPR 3 "=h"))
   (clobber (match_scratch:GPR 4 "=l"))]
  "TARGET_FIX_R4000"
  "<d>mult\t%1,%2\;mflo\t%0"
  [(set_attr "type" "imul")
   (set_attr "mode" "<MODE>")
   (set_attr "length" "8")])

;; On the VR4120 and VR4130, it is better to use "mtlo $0; macc" instead
;; of "mult; mflo".  They have the same latency, but the first form gives
;; us an extra cycle to compute the operands.

;; Operand 0: LO
;; Operand 1: GPR (1st multiplication operand)
;; Operand 2: GPR (2nd multiplication operand)
;; Operand 3: HI
;; Operand 4: GPR (destination)
(define_peephole2
  [(parallel
       [(set (match_operand:SI 0 "register_operand")
	     (mult:SI (match_operand:SI 1 "register_operand")
		      (match_operand:SI 2 "register_operand")))
        (clobber (match_operand:SI 3 "register_operand"))])
   (set (match_operand:SI 4 "register_operand")
	(unspec:SI [(match_dup 0) (match_dup 3)] UNSPEC_MFHILO))]
  "ISA_HAS_MACC && !GENERATE_MULT3_SI"
  [(set (match_dup 0)
	(const_int 0))
   (parallel
       [(set (match_dup 0)
	     (plus:SI (mult:SI (match_dup 1)
			       (match_dup 2))
		      (match_dup 0)))
	(set (match_dup 4)
	     (plus:SI (mult:SI (match_dup 1)
			       (match_dup 2))
		      (match_dup 0)))
        (clobber (match_dup 3))])])

;; Multiply-accumulate patterns

;; For processors that can copy the output to a general register:
;;
;; The all-d alternative is needed because the combiner will find this
;; pattern and then register alloc/reload will move registers around to
;; make them fit, and we don't want to trigger unnecessary loads to LO.
;;
;; The last alternative should be made slightly less desirable, but adding
;; "?" to the constraint is too strong, and causes values to be loaded into
;; LO even when that's more costly.  For now, using "*d" mostly does the
;; trick.
(define_insn "*mul_acc_si"
  [(set (match_operand:SI 0 "register_operand" "=l,*d,*d")
	(plus:SI (mult:SI (match_operand:SI 1 "register_operand" "d,d,d")
			  (match_operand:SI 2 "register_operand" "d,d,d"))
		 (match_operand:SI 3 "register_operand" "0,l,*d")))
   (clobber (match_scratch:SI 4 "=h,h,h"))
   (clobber (match_scratch:SI 5 "=X,3,l"))
   (clobber (match_scratch:SI 6 "=X,X,&d"))]
  "(TARGET_MIPS3900
   || ISA_HAS_MADD_MSUB)
   && !TARGET_MIPS16"
{
  static const char *const madd[] = { "madd\t%1,%2", "madd\t%0,%1,%2" };
  if (which_alternative == 2)
    return "#";
  if (ISA_HAS_MADD_MSUB && which_alternative != 0)
    return "#";
  return madd[which_alternative];
}
  [(set_attr "type"	"imadd,imadd,multi")
   (set_attr "mode"	"SI")
   (set_attr "length"	"4,4,8")])

;; Split the above insn if we failed to get LO allocated.
(define_split
  [(set (match_operand:SI 0 "register_operand")
	(plus:SI (mult:SI (match_operand:SI 1 "register_operand")
			  (match_operand:SI 2 "register_operand"))
		 (match_operand:SI 3 "register_operand")))
   (clobber (match_scratch:SI 4))
   (clobber (match_scratch:SI 5))
   (clobber (match_scratch:SI 6))]
  "reload_completed && !TARGET_DEBUG_D_MODE
   && GP_REG_P (true_regnum (operands[0]))
   && GP_REG_P (true_regnum (operands[3]))"
  [(parallel [(set (match_dup 6)
		   (mult:SI (match_dup 1) (match_dup 2)))
	      (clobber (match_dup 4))
	      (clobber (match_dup 5))])
   (set (match_dup 0) (plus:SI (match_dup 6) (match_dup 3)))]
  "")

;; Splitter to copy result of MADD to a general register
(define_split
  [(set (match_operand:SI                   0 "register_operand")
        (plus:SI (mult:SI (match_operand:SI 1 "register_operand")
                          (match_operand:SI 2 "register_operand"))
                 (match_operand:SI          3 "register_operand")))
   (clobber (match_scratch:SI               4))
   (clobber (match_scratch:SI               5))
   (clobber (match_scratch:SI               6))]
  "reload_completed && !TARGET_DEBUG_D_MODE
   && GP_REG_P (true_regnum (operands[0]))
   && true_regnum (operands[3]) == LO_REGNUM"
  [(parallel [(set (match_dup 3)
                   (plus:SI (mult:SI (match_dup 1) (match_dup 2))
                            (match_dup 3)))
              (clobber (match_dup 4))
              (clobber (match_dup 5))
              (clobber (match_dup 6))])
   (set (match_dup 0) (unspec:SI [(match_dup 5) (match_dup 4)] UNSPEC_MFHILO))]
  "")

(define_insn "*macc"
  [(set (match_operand:SI 0 "register_operand" "=l,d")
	(plus:SI (mult:SI (match_operand:SI 1 "register_operand" "d,d")
			  (match_operand:SI 2 "register_operand" "d,d"))
		 (match_operand:SI 3 "register_operand" "0,l")))
   (clobber (match_scratch:SI 4 "=h,h"))
   (clobber (match_scratch:SI 5 "=X,3"))]
  "ISA_HAS_MACC"
{
  if (which_alternative == 1)
    return "macc\t%0,%1,%2";
  else if (TARGET_MIPS5500)
    return "madd\t%1,%2";
  else
    /* The VR4130 assumes that there is a two-cycle latency between a macc
       that "writes" to $0 and an instruction that reads from it.  We avoid
       this by assigning to $1 instead.  */
    return "%[macc\t%@,%1,%2%]";
}
  [(set_attr "type" "imadd")
   (set_attr "mode" "SI")])

(define_insn "*msac"
  [(set (match_operand:SI 0 "register_operand" "=l,d")
        (minus:SI (match_operand:SI 1 "register_operand" "0,l")
                  (mult:SI (match_operand:SI 2 "register_operand" "d,d")
                           (match_operand:SI 3 "register_operand" "d,d"))))
   (clobber (match_scratch:SI 4 "=h,h"))
   (clobber (match_scratch:SI 5 "=X,1"))]
  "ISA_HAS_MSAC"
{
  if (which_alternative == 1)
    return "msac\t%0,%2,%3";
  else if (TARGET_MIPS5500)
    return "msub\t%2,%3";
  else
    return "msac\t$0,%2,%3";
}
  [(set_attr "type"     "imadd")
   (set_attr "mode"     "SI")])

;; An msac-like instruction implemented using negation and a macc.
(define_insn_and_split "*msac_using_macc"
  [(set (match_operand:SI 0 "register_operand" "=l,d")
        (minus:SI (match_operand:SI 1 "register_operand" "0,l")
                  (mult:SI (match_operand:SI 2 "register_operand" "d,d")
                           (match_operand:SI 3 "register_operand" "d,d"))))
   (clobber (match_scratch:SI 4 "=h,h"))
   (clobber (match_scratch:SI 5 "=X,1"))
   (clobber (match_scratch:SI 6 "=d,d"))]
  "ISA_HAS_MACC && !ISA_HAS_MSAC"
  "#"
  "&& reload_completed"
  [(set (match_dup 6)
	(neg:SI (match_dup 3)))
   (parallel
       [(set (match_dup 0)
	     (plus:SI (mult:SI (match_dup 2)
			       (match_dup 6))
		      (match_dup 1)))
	(clobber (match_dup 4))
	(clobber (match_dup 5))])]
  ""
  [(set_attr "type"     "imadd")
   (set_attr "length"	"8")])

;; Patterns generated by the define_peephole2 below.

(define_insn "*macc2"
  [(set (match_operand:SI 0 "register_operand" "=l")
	(plus:SI (mult:SI (match_operand:SI 1 "register_operand" "d")
			  (match_operand:SI 2 "register_operand" "d"))
		 (match_dup 0)))
   (set (match_operand:SI 3 "register_operand" "=d")
	(plus:SI (mult:SI (match_dup 1)
			  (match_dup 2))
		 (match_dup 0)))
   (clobber (match_scratch:SI 4 "=h"))]
  "ISA_HAS_MACC && reload_completed"
  "macc\t%3,%1,%2"
  [(set_attr "type"	"imadd")
   (set_attr "mode"	"SI")])

(define_insn "*msac2"
  [(set (match_operand:SI 0 "register_operand" "=l")
	(minus:SI (match_dup 0)
		  (mult:SI (match_operand:SI 1 "register_operand" "d")
			   (match_operand:SI 2 "register_operand" "d"))))
   (set (match_operand:SI 3 "register_operand" "=d")
	(minus:SI (match_dup 0)
		  (mult:SI (match_dup 1)
			   (match_dup 2))))
   (clobber (match_scratch:SI 4 "=h"))]
  "ISA_HAS_MSAC && reload_completed"
  "msac\t%3,%1,%2"
  [(set_attr "type"	"imadd")
   (set_attr "mode"	"SI")])

;; Convert macc $0,<r1>,<r2> & mflo <r3> into macc <r3>,<r1>,<r2>
;; Similarly msac.
;;
;; Operand 0: LO
;; Operand 1: macc/msac
;; Operand 2: HI
;; Operand 3: GPR (destination)
(define_peephole2
  [(parallel
       [(set (match_operand:SI 0 "register_operand")
	     (match_operand:SI 1 "macc_msac_operand"))
	(clobber (match_operand:SI 2 "register_operand"))
	(clobber (scratch:SI))])
   (set (match_operand:SI 3 "register_operand")
	(unspec:SI [(match_dup 0) (match_dup 2)] UNSPEC_MFHILO))]
  ""
  [(parallel [(set (match_dup 0)
		   (match_dup 1))
	      (set (match_dup 3)
		   (match_dup 1))
	      (clobber (match_dup 2))])]
  "")

;; When we have a three-address multiplication instruction, it should
;; be faster to do a separate multiply and add, rather than moving
;; something into LO in order to use a macc instruction.
;;
;; This peephole needs a scratch register to cater for the case when one
;; of the multiplication operands is the same as the destination.
;;
;; Operand 0: GPR (scratch)
;; Operand 1: LO
;; Operand 2: GPR (addend)
;; Operand 3: GPR (destination)
;; Operand 4: macc/msac
;; Operand 5: HI
;; Operand 6: new multiplication
;; Operand 7: new addition/subtraction
(define_peephole2
  [(match_scratch:SI 0 "d")
   (set (match_operand:SI 1 "register_operand")
	(match_operand:SI 2 "register_operand"))
   (match_dup 0)
   (parallel
       [(set (match_operand:SI 3 "register_operand")
	     (match_operand:SI 4 "macc_msac_operand"))
	(clobber (match_operand:SI 5 "register_operand"))
	(clobber (match_dup 1))])]
  "GENERATE_MULT3_SI
   && true_regnum (operands[1]) == LO_REGNUM
   && peep2_reg_dead_p (2, operands[1])
   && GP_REG_P (true_regnum (operands[3]))"
  [(parallel [(set (match_dup 0)
		   (match_dup 6))
	      (clobber (match_dup 5))
	      (clobber (match_dup 1))])
   (set (match_dup 3)
	(match_dup 7))]
{
  operands[6] = XEXP (operands[4], GET_CODE (operands[4]) == PLUS ? 0 : 1);
  operands[7] = gen_rtx_fmt_ee (GET_CODE (operands[4]), SImode,
				operands[2], operands[0]);
})

;; Same as above, except LO is the initial target of the macc.
;;
;; Operand 0: GPR (scratch)
;; Operand 1: LO
;; Operand 2: GPR (addend)
;; Operand 3: macc/msac
;; Operand 4: HI
;; Operand 5: GPR (destination)
;; Operand 6: new multiplication
;; Operand 7: new addition/subtraction
(define_peephole2
  [(match_scratch:SI 0 "d")
   (set (match_operand:SI 1 "register_operand")
	(match_operand:SI 2 "register_operand"))
   (match_dup 0)
   (parallel
       [(set (match_dup 1)
	     (match_operand:SI 3 "macc_msac_operand"))
	(clobber (match_operand:SI 4 "register_operand"))
	(clobber (scratch:SI))])
   (match_dup 0)
   (set (match_operand:SI 5 "register_operand")
	(unspec:SI [(match_dup 1) (match_dup 4)] UNSPEC_MFHILO))]
  "GENERATE_MULT3_SI && peep2_reg_dead_p (3, operands[1])"
  [(parallel [(set (match_dup 0)
		   (match_dup 6))
	      (clobber (match_dup 4))
	      (clobber (match_dup 1))])
   (set (match_dup 5)
	(match_dup 7))]
{
  operands[6] = XEXP (operands[4], GET_CODE (operands[4]) == PLUS ? 0 : 1);
  operands[7] = gen_rtx_fmt_ee (GET_CODE (operands[4]), SImode,
				operands[2], operands[0]);
})

(define_insn "*mul_sub_si"
  [(set (match_operand:SI 0 "register_operand" "=l,*d,*d")
        (minus:SI (match_operand:SI 1 "register_operand" "0,l,*d")
                  (mult:SI (match_operand:SI 2 "register_operand" "d,d,d")
                           (match_operand:SI 3 "register_operand" "d,d,d"))))
   (clobber (match_scratch:SI 4 "=h,h,h"))
   (clobber (match_scratch:SI 5 "=X,1,l"))
   (clobber (match_scratch:SI 6 "=X,X,&d"))]
  "ISA_HAS_MADD_MSUB"
  "@
   msub\t%2,%3
   #
   #"
  [(set_attr "type"     "imadd,multi,multi")
   (set_attr "mode"     "SI")
   (set_attr "length"   "4,8,8")])

;; Split the above insn if we failed to get LO allocated.
(define_split
  [(set (match_operand:SI 0 "register_operand")
        (minus:SI (match_operand:SI 1 "register_operand")
                  (mult:SI (match_operand:SI 2 "register_operand")
                           (match_operand:SI 3 "register_operand"))))
   (clobber (match_scratch:SI 4))
   (clobber (match_scratch:SI 5))
   (clobber (match_scratch:SI 6))]
  "reload_completed && !TARGET_DEBUG_D_MODE
   && GP_REG_P (true_regnum (operands[0]))
   && GP_REG_P (true_regnum (operands[1]))"
  [(parallel [(set (match_dup 6)
                   (mult:SI (match_dup 2) (match_dup 3)))
              (clobber (match_dup 4))
              (clobber (match_dup 5))])
   (set (match_dup 0) (minus:SI (match_dup 1) (match_dup 6)))]
  "")

;; Splitter to copy result of MSUB to a general register
(define_split
  [(set (match_operand:SI 0 "register_operand")
        (minus:SI (match_operand:SI 1 "register_operand")
                  (mult:SI (match_operand:SI 2 "register_operand")
                           (match_operand:SI 3 "register_operand"))))
   (clobber (match_scratch:SI 4))
   (clobber (match_scratch:SI 5))
   (clobber (match_scratch:SI 6))]
  "reload_completed && !TARGET_DEBUG_D_MODE
   && GP_REG_P (true_regnum (operands[0]))
   && true_regnum (operands[1]) == LO_REGNUM"
  [(parallel [(set (match_dup 1)
                   (minus:SI (match_dup 1)
                             (mult:SI (match_dup 2) (match_dup 3))))
              (clobber (match_dup 4))
              (clobber (match_dup 5))
              (clobber (match_dup 6))])
   (set (match_dup 0) (unspec:SI [(match_dup 5) (match_dup 4)] UNSPEC_MFHILO))]
  "")

(define_insn "*muls"
  [(set (match_operand:SI                  0 "register_operand" "=l,d")
        (neg:SI (mult:SI (match_operand:SI 1 "register_operand" "d,d")
                         (match_operand:SI 2 "register_operand" "d,d"))))
   (clobber (match_scratch:SI              3                    "=h,h"))
   (clobber (match_scratch:SI              4                    "=X,l"))]
  "ISA_HAS_MULS"
  "@
   muls\t$0,%1,%2
   muls\t%0,%1,%2"
  [(set_attr "type"     "imul,imul3")
   (set_attr "mode"     "SI")])

;; ??? We could define a mulditi3 pattern when TARGET_64BIT.

(define_expand "<u>mulsidi3"
  [(parallel
      [(set (match_operand:DI 0 "register_operand")
	    (mult:DI (any_extend:DI (match_operand:SI 1 "register_operand"))
		     (any_extend:DI (match_operand:SI 2 "register_operand"))))
       (clobber (scratch:DI))
       (clobber (scratch:DI))
       (clobber (scratch:DI))])]
  "!TARGET_64BIT || !TARGET_FIX_R4000"
{
  if (!TARGET_64BIT)
    {
      if (!TARGET_FIX_R4000)
	emit_insn (gen_<u>mulsidi3_32bit_internal (operands[0], operands[1],
						   operands[2]));
      else
	emit_insn (gen_<u>mulsidi3_32bit_r4000 (operands[0], operands[1],
					        operands[2]));
      DONE;
    }
})

(define_insn "<u>mulsidi3_32bit_internal"
  [(set (match_operand:DI 0 "register_operand" "=x")
	(mult:DI (any_extend:DI (match_operand:SI 1 "register_operand" "d"))
		 (any_extend:DI (match_operand:SI 2 "register_operand" "d"))))]
  "!TARGET_64BIT && !TARGET_FIX_R4000"
  "mult<u>\t%1,%2"
  [(set_attr "type" "imul")
   (set_attr "mode" "SI")])

(define_insn "<u>mulsidi3_32bit_r4000"
  [(set (match_operand:DI 0 "register_operand" "=d")
	(mult:DI (any_extend:DI (match_operand:SI 1 "register_operand" "d"))
		 (any_extend:DI (match_operand:SI 2 "register_operand" "d"))))
   (clobber (match_scratch:DI 3 "=x"))]
  "!TARGET_64BIT && TARGET_FIX_R4000"
  "mult<u>\t%1,%2\;mflo\t%L0;mfhi\t%M0"
  [(set_attr "type" "imul")
   (set_attr "mode" "SI")
   (set_attr "length" "12")])

(define_insn_and_split "*<u>mulsidi3_64bit"
  [(set (match_operand:DI 0 "register_operand" "=d")
	(mult:DI (any_extend:DI (match_operand:SI 1 "register_operand" "d"))
		 (any_extend:DI (match_operand:SI 2 "register_operand" "d"))))
   (clobber (match_scratch:DI 3 "=l"))
   (clobber (match_scratch:DI 4 "=h"))
   (clobber (match_scratch:DI 5 "=d"))]
  "TARGET_64BIT && !TARGET_FIX_R4000"
  "#"
  "&& reload_completed"
  [(parallel
       [(set (match_dup 3)
	     (sign_extend:DI
		(mult:SI (match_dup 1)
			 (match_dup 2))))
	(set (match_dup 4)
	     (ashiftrt:DI
		(mult:DI (any_extend:DI (match_dup 1))
			 (any_extend:DI (match_dup 2)))
		(const_int 32)))])

   ;; OP5 <- LO, OP0 <- HI
   (set (match_dup 5) (unspec:DI [(match_dup 3) (match_dup 4)] UNSPEC_MFHILO))
   (set (match_dup 0) (unspec:DI [(match_dup 4) (match_dup 3)] UNSPEC_MFHILO))

   ;; Zero-extend OP5.
   (set (match_dup 5)
	(ashift:DI (match_dup 5)
		   (const_int 32)))
   (set (match_dup 5)
	(lshiftrt:DI (match_dup 5)
		     (const_int 32)))

   ;; Shift OP0 into place.
   (set (match_dup 0)
	(ashift:DI (match_dup 0)
		   (const_int 32)))

   ;; OR the two halves together
   (set (match_dup 0)
	(ior:DI (match_dup 0)
		(match_dup 5)))]
  ""
  [(set_attr "type" "imul")
   (set_attr "mode" "SI")
   (set_attr "length" "24")])

(define_insn "*<u>mulsidi3_64bit_parts"
  [(set (match_operand:DI 0 "register_operand" "=l")
	(sign_extend:DI
	   (mult:SI (match_operand:SI 2 "register_operand" "d")
		    (match_operand:SI 3 "register_operand" "d"))))
   (set (match_operand:DI 1 "register_operand" "=h")
	(ashiftrt:DI
	   (mult:DI (any_extend:DI (match_dup 2))
		    (any_extend:DI (match_dup 3)))
	   (const_int 32)))]
  "TARGET_64BIT && !TARGET_FIX_R4000"
  "mult<u>\t%2,%3"
  [(set_attr "type" "imul")
   (set_attr "mode" "SI")])

;; Widening multiply with negation.
(define_insn "*muls<u>_di"
  [(set (match_operand:DI 0 "register_operand" "=x")
        (neg:DI
	 (mult:DI
	  (any_extend:DI (match_operand:SI 1 "register_operand" "d"))
	  (any_extend:DI (match_operand:SI 2 "register_operand" "d")))))]
  "!TARGET_64BIT && ISA_HAS_MULS"
  "muls<u>\t$0,%1,%2"
  [(set_attr "type" "imul")
   (set_attr "mode" "SI")])

(define_insn "*msac<u>_di"
  [(set (match_operand:DI 0 "register_operand" "=x")
        (minus:DI
	   (match_operand:DI 3 "register_operand" "0")
	   (mult:DI
	      (any_extend:DI (match_operand:SI 1 "register_operand" "d"))
	      (any_extend:DI (match_operand:SI 2 "register_operand" "d")))))]
  "!TARGET_64BIT && ISA_HAS_MSAC"
{
  if (TARGET_MIPS5500)
    return "msub<u>\t%1,%2";
  else
    return "msac<u>\t$0,%1,%2";
}
  [(set_attr "type" "imadd")
   (set_attr "mode" "SI")])

;; _highpart patterns

(define_expand "<su>mulsi3_highpart"
  [(set (match_operand:SI 0 "register_operand")
	(truncate:SI
	 (lshiftrt:DI
	  (mult:DI (any_extend:DI (match_operand:SI 1 "register_operand"))
		   (any_extend:DI (match_operand:SI 2 "register_operand")))
	  (const_int 32))))]
  "ISA_HAS_MULHI || !TARGET_FIX_R4000"
{
  if (ISA_HAS_MULHI)
    emit_insn (gen_<su>mulsi3_highpart_mulhi_internal (operands[0],
						       operands[1],
						       operands[2]));
  else
    emit_insn (gen_<su>mulsi3_highpart_internal (operands[0], operands[1],
					         operands[2]));
  DONE;
})

(define_insn "<su>mulsi3_highpart_internal"
  [(set (match_operand:SI 0 "register_operand" "=h")
	(truncate:SI
	 (lshiftrt:DI
	  (mult:DI (any_extend:DI (match_operand:SI 1 "register_operand" "d"))
		   (any_extend:DI (match_operand:SI 2 "register_operand" "d")))
	  (const_int 32))))
   (clobber (match_scratch:SI 3 "=l"))]
  "!ISA_HAS_MULHI && !TARGET_FIX_R4000"
  "mult<u>\t%1,%2"
  [(set_attr "type" "imul")
   (set_attr "mode" "SI")])

(define_insn "<su>mulsi3_highpart_mulhi_internal"
  [(set (match_operand:SI 0 "register_operand" "=h,d")
        (truncate:SI
	 (lshiftrt:DI
	  (mult:DI
	   (any_extend:DI (match_operand:SI 1 "register_operand" "d,d"))
	   (any_extend:DI (match_operand:SI 2 "register_operand" "d,d")))
	  (const_int 32))))
   (clobber (match_scratch:SI 3 "=l,l"))
   (clobber (match_scratch:SI 4 "=X,h"))]
  "ISA_HAS_MULHI"
  "@
   mult<u>\t%1,%2
   mulhi<u>\t%0,%1,%2"
  [(set_attr "type" "imul,imul3")
   (set_attr "mode" "SI")])

(define_insn "*<su>mulsi3_highpart_neg_mulhi_internal"
  [(set (match_operand:SI 0 "register_operand" "=h,d")
        (truncate:SI
	 (lshiftrt:DI
	  (neg:DI
	   (mult:DI
	    (any_extend:DI (match_operand:SI 1 "register_operand" "d,d"))
	    (any_extend:DI (match_operand:SI 2 "register_operand" "d,d"))))
	  (const_int 32))))
   (clobber (match_scratch:SI 3 "=l,l"))
   (clobber (match_scratch:SI 4 "=X,h"))]
  "ISA_HAS_MULHI"
  "@
   mulshi<u>\t%.,%1,%2
   mulshi<u>\t%0,%1,%2"
  [(set_attr "type" "imul,imul3")
   (set_attr "mode" "SI")])

;; Disable unsigned multiplication for -mfix-vr4120.  This is for VR4120
;; errata MD(0), which says that dmultu does not always produce the
;; correct result.
(define_insn "<su>muldi3_highpart"
  [(set (match_operand:DI 0 "register_operand" "=h")
	(truncate:DI
	 (lshiftrt:TI
	  (mult:TI
	   (any_extend:TI (match_operand:DI 1 "register_operand" "d"))
	   (any_extend:TI (match_operand:DI 2 "register_operand" "d")))
	  (const_int 64))))
   (clobber (match_scratch:DI 3 "=l"))]
  "TARGET_64BIT && !TARGET_FIX_R4000
   && !(<CODE> == ZERO_EXTEND && TARGET_FIX_VR4120)"
  "dmult<u>\t%1,%2"
  [(set_attr "type" "imul")
   (set_attr "mode" "DI")])

;; The R4650 supports a 32 bit multiply/ 64 bit accumulate
;; instruction.  The HI/LO registers are used as a 64 bit accumulator.

(define_insn "madsi"
  [(set (match_operand:SI 0 "register_operand" "+l")
	(plus:SI (mult:SI (match_operand:SI 1 "register_operand" "d")
			  (match_operand:SI 2 "register_operand" "d"))
		 (match_dup 0)))
   (clobber (match_scratch:SI 3 "=h"))]
  "TARGET_MAD"
  "mad\t%1,%2"
  [(set_attr "type"	"imadd")
   (set_attr "mode"	"SI")])

(define_insn "*<su>mul_acc_di"
  [(set (match_operand:DI 0 "register_operand" "=x")
	(plus:DI
	 (mult:DI (any_extend:DI (match_operand:SI 1 "register_operand" "d"))
		  (any_extend:DI (match_operand:SI 2 "register_operand" "d")))
	 (match_operand:DI 3 "register_operand" "0")))]
  "(TARGET_MAD || ISA_HAS_MACC)
   && !TARGET_64BIT"
{
  if (TARGET_MAD)
    return "mad<u>\t%1,%2";
  else if (TARGET_MIPS5500)
    return "madd<u>\t%1,%2";
  else
    /* See comment in *macc.  */
    return "%[macc<u>\t%@,%1,%2%]";
}
  [(set_attr "type" "imadd")
   (set_attr "mode" "SI")])

;; Floating point multiply accumulate instructions.

(define_insn "*madd<mode>"
  [(set (match_operand:ANYF 0 "register_operand" "=f")
	(plus:ANYF (mult:ANYF (match_operand:ANYF 1 "register_operand" "f")
			      (match_operand:ANYF 2 "register_operand" "f"))
		   (match_operand:ANYF 3 "register_operand" "f")))]
  "ISA_HAS_FP4 && TARGET_FUSED_MADD"
  "madd.<fmt>\t%0,%3,%1,%2"
  [(set_attr "type" "fmadd")
   (set_attr "mode" "<UNITMODE>")])

(define_insn "*msub<mode>"
  [(set (match_operand:ANYF 0 "register_operand" "=f")
	(minus:ANYF (mult:ANYF (match_operand:ANYF 1 "register_operand" "f")
			       (match_operand:ANYF 2 "register_operand" "f"))
		    (match_operand:ANYF 3 "register_operand" "f")))]
  "ISA_HAS_FP4 && TARGET_FUSED_MADD"
  "msub.<fmt>\t%0,%3,%1,%2"
  [(set_attr "type" "fmadd")
   (set_attr "mode" "<UNITMODE>")])

(define_insn "*nmadd<mode>"
  [(set (match_operand:ANYF 0 "register_operand" "=f")
	(neg:ANYF (plus:ANYF
		   (mult:ANYF (match_operand:ANYF 1 "register_operand" "f")
			      (match_operand:ANYF 2 "register_operand" "f"))
		   (match_operand:ANYF 3 "register_operand" "f"))))]
  "ISA_HAS_NMADD_NMSUB && TARGET_FUSED_MADD
   && HONOR_SIGNED_ZEROS (<MODE>mode)
   && !HONOR_NANS (<MODE>mode)"
  "nmadd.<fmt>\t%0,%3,%1,%2"
  [(set_attr "type" "fmadd")
   (set_attr "mode" "<UNITMODE>")])

(define_insn "*nmadd<mode>_fastmath"
  [(set (match_operand:ANYF 0 "register_operand" "=f")
	(minus:ANYF
	 (mult:ANYF (neg:ANYF (match_operand:ANYF 1 "register_operand" "f"))
		    (match_operand:ANYF 2 "register_operand" "f"))
	 (match_operand:ANYF 3 "register_operand" "f")))]
  "ISA_HAS_NMADD_NMSUB && TARGET_FUSED_MADD
   && !HONOR_SIGNED_ZEROS (<MODE>mode)
   && !HONOR_NANS (<MODE>mode)"
  "nmadd.<fmt>\t%0,%3,%1,%2"
  [(set_attr "type" "fmadd")
   (set_attr "mode" "<UNITMODE>")])

(define_insn "*nmsub<mode>"
  [(set (match_operand:ANYF 0 "register_operand" "=f")
	(neg:ANYF (minus:ANYF
		   (mult:ANYF (match_operand:ANYF 2 "register_operand" "f")
			      (match_operand:ANYF 3 "register_operand" "f"))
		   (match_operand:ANYF 1 "register_operand" "f"))))]
  "ISA_HAS_NMADD_NMSUB && TARGET_FUSED_MADD
   && HONOR_SIGNED_ZEROS (<MODE>mode)
   && !HONOR_NANS (<MODE>mode)"
  "nmsub.<fmt>\t%0,%1,%2,%3"
  [(set_attr "type" "fmadd")
   (set_attr "mode" "<UNITMODE>")])

(define_insn "*nmsub<mode>_fastmath"
  [(set (match_operand:ANYF 0 "register_operand" "=f")
	(minus:ANYF
	 (match_operand:ANYF 1 "register_operand" "f")
	 (mult:ANYF (match_operand:ANYF 2 "register_operand" "f")
		    (match_operand:ANYF 3 "register_operand" "f"))))]
  "ISA_HAS_NMADD_NMSUB && TARGET_FUSED_MADD
   && !HONOR_SIGNED_ZEROS (<MODE>mode)
   && !HONOR_NANS (<MODE>mode)"
  "nmsub.<fmt>\t%0,%1,%2,%3"
  [(set_attr "type" "fmadd")
   (set_attr "mode" "<UNITMODE>")])

;;
;;  ....................
;;
;;	DIVISION and REMAINDER
;;
;;  ....................
;;

(define_expand "div<mode>3"
  [(set (match_operand:ANYF 0 "register_operand")
	(div:ANYF (match_operand:ANYF 1 "reg_or_1_operand")
		  (match_operand:ANYF 2 "register_operand")))]
  "<divide_condition>"
{
  if (const_1_operand (operands[1], <MODE>mode))
    if (!(ISA_HAS_FP4 && flag_unsafe_math_optimizations))
      operands[1] = force_reg (<MODE>mode, operands[1]);
})

;; These patterns work around the early SB-1 rev2 core "F1" erratum:
;;
;; If an mfc1 or dmfc1 happens to access the floating point register
;; file at the same time a long latency operation (div, sqrt, recip,
;; sqrt) iterates an intermediate result back through the floating
;; point register file bypass, then instead returning the correct
;; register value the mfc1 or dmfc1 operation returns the intermediate
;; result of the long latency operation.
;;
;; The workaround is to insert an unconditional 'mov' from/to the
;; long latency op destination register.

(define_insn "*div<mode>3"
  [(set (match_operand:ANYF 0 "register_operand" "=f")
	(div:ANYF (match_operand:ANYF 1 "register_operand" "f")
		  (match_operand:ANYF 2 "register_operand" "f")))]
  "<divide_condition>"
{
  if (TARGET_FIX_SB1)
    return "div.<fmt>\t%0,%1,%2\;mov.<fmt>\t%0,%0";
  else
    return "div.<fmt>\t%0,%1,%2";
}
  [(set_attr "type" "fdiv")
   (set_attr "mode" "<UNITMODE>")
   (set (attr "length")
        (if_then_else (ne (symbol_ref "TARGET_FIX_SB1") (const_int 0))
                      (const_int 8)
                      (const_int 4)))])

(define_insn "*recip<mode>3"
  [(set (match_operand:ANYF 0 "register_operand" "=f")
	(div:ANYF (match_operand:ANYF 1 "const_1_operand" "")
		  (match_operand:ANYF 2 "register_operand" "f")))]
  "<recip_condition> && flag_unsafe_math_optimizations"
{
  if (TARGET_FIX_SB1)
    return "recip.<fmt>\t%0,%2\;mov.<fmt>\t%0,%0";
  else
    return "recip.<fmt>\t%0,%2";
}
  [(set_attr "type" "frdiv")
   (set_attr "mode" "<UNITMODE>")
   (set (attr "length")
        (if_then_else (ne (symbol_ref "TARGET_FIX_SB1") (const_int 0))
                      (const_int 8)
                      (const_int 4)))])

;; VR4120 errata MD(A1): signed division instructions do not work correctly
;; with negative operands.  We use special libgcc functions instead.
(define_insn "divmod<mode>4"
  [(set (match_operand:GPR 0 "register_operand" "=l")
	(div:GPR (match_operand:GPR 1 "register_operand" "d")
		 (match_operand:GPR 2 "register_operand" "d")))
   (set (match_operand:GPR 3 "register_operand" "=h")
	(mod:GPR (match_dup 1)
		 (match_dup 2)))]
  "!TARGET_FIX_VR4120"
  { return mips_output_division ("<d>div\t$0,%1,%2", operands); }
  [(set_attr "type" "idiv")
   (set_attr "mode" "<MODE>")])

(define_insn "udivmod<mode>4"
  [(set (match_operand:GPR 0 "register_operand" "=l")
	(udiv:GPR (match_operand:GPR 1 "register_operand" "d")
		  (match_operand:GPR 2 "register_operand" "d")))
   (set (match_operand:GPR 3 "register_operand" "=h")
	(umod:GPR (match_dup 1)
		  (match_dup 2)))]
  ""
  { return mips_output_division ("<d>divu\t$0,%1,%2", operands); }
  [(set_attr "type" "idiv")
   (set_attr "mode" "<MODE>")])

;;
;;  ....................
;;
;;	SQUARE ROOT
;;
;;  ....................

;; These patterns work around the early SB-1 rev2 core "F1" erratum (see
;; "*div[sd]f3" comment for details).

(define_insn "sqrt<mode>2"
  [(set (match_operand:ANYF 0 "register_operand" "=f")
	(sqrt:ANYF (match_operand:ANYF 1 "register_operand" "f")))]
  "<sqrt_condition>"
{
  if (TARGET_FIX_SB1)
    return "sqrt.<fmt>\t%0,%1\;mov.<fmt>\t%0,%0";
  else
    return "sqrt.<fmt>\t%0,%1";
}
  [(set_attr "type" "fsqrt")
   (set_attr "mode" "<UNITMODE>")
   (set (attr "length")
        (if_then_else (ne (symbol_ref "TARGET_FIX_SB1") (const_int 0))
                      (const_int 8)
                      (const_int 4)))])

(define_insn "*rsqrt<mode>a"
  [(set (match_operand:ANYF 0 "register_operand" "=f")
	(div:ANYF (match_operand:ANYF 1 "const_1_operand" "")
		  (sqrt:ANYF (match_operand:ANYF 2 "register_operand" "f"))))]
  "<recip_condition> && flag_unsafe_math_optimizations"
{
  if (TARGET_FIX_SB1)
    return "rsqrt.<fmt>\t%0,%2\;mov.<fmt>\t%0,%0";
  else
    return "rsqrt.<fmt>\t%0,%2";
}
  [(set_attr "type" "frsqrt")
   (set_attr "mode" "<UNITMODE>")
   (set (attr "length")
        (if_then_else (ne (symbol_ref "TARGET_FIX_SB1") (const_int 0))
                      (const_int 8)
                      (const_int 4)))])

(define_insn "*rsqrt<mode>b"
  [(set (match_operand:ANYF 0 "register_operand" "=f")
	(sqrt:ANYF (div:ANYF (match_operand:ANYF 1 "const_1_operand" "")
			     (match_operand:ANYF 2 "register_operand" "f"))))]
  "<recip_condition> && flag_unsafe_math_optimizations"
{
  if (TARGET_FIX_SB1)
    return "rsqrt.<fmt>\t%0,%2\;mov.<fmt>\t%0,%0";
  else
    return "rsqrt.<fmt>\t%0,%2";
}
  [(set_attr "type" "frsqrt")
   (set_attr "mode" "<UNITMODE>")
   (set (attr "length")
        (if_then_else (ne (symbol_ref "TARGET_FIX_SB1") (const_int 0))
                      (const_int 8)
                      (const_int 4)))])

;;
;;  ....................
;;
;;	ABSOLUTE VALUE
;;
;;  ....................

;; Do not use the integer abs macro instruction, since that signals an
;; exception on -2147483648 (sigh).

;; abs.fmt is an arithmetic instruction and treats all NaN inputs as
;; invalid; it does not clear their sign bits.  We therefore can't use
;; abs.fmt if the signs of NaNs matter.

(define_insn "abs<mode>2"
  [(set (match_operand:ANYF 0 "register_operand" "=f")
	(abs:ANYF (match_operand:ANYF 1 "register_operand" "f")))]
  "!HONOR_NANS (<MODE>mode)"
  "abs.<fmt>\t%0,%1"
  [(set_attr "type" "fabs")
   (set_attr "mode" "<UNITMODE>")])

;;
;;  ...................
;;
;;  Count leading zeroes.
;;
;;  ...................
;;

(define_insn "clz<mode>2"
  [(set (match_operand:GPR 0 "register_operand" "=d")
	(clz:GPR (match_operand:GPR 1 "register_operand" "d")))]
  "ISA_HAS_CLZ_CLO"
  "<d>clz\t%0,%1"
  [(set_attr "type" "clz")
   (set_attr "mode" "<MODE>")])

;;
;;  ....................
;;
;;	NEGATION and ONE'S COMPLEMENT
;;
;;  ....................

(define_insn "negsi2"
  [(set (match_operand:SI 0 "register_operand" "=d")
	(neg:SI (match_operand:SI 1 "register_operand" "d")))]
  ""
{
  if (TARGET_MIPS16)
    return "neg\t%0,%1";
  else
    return "subu\t%0,%.,%1";
}
  [(set_attr "type"	"arith")
   (set_attr "mode"	"SI")])

(define_insn "negdi2"
  [(set (match_operand:DI 0 "register_operand" "=d")
	(neg:DI (match_operand:DI 1 "register_operand" "d")))]
  "TARGET_64BIT && !TARGET_MIPS16"
  "dsubu\t%0,%.,%1"
  [(set_attr "type"	"arith")
   (set_attr "mode"	"DI")])

;; neg.fmt is an arithmetic instruction and treats all NaN inputs as
;; invalid; it does not flip their sign bit.  We therefore can't use
;; neg.fmt if the signs of NaNs matter.

(define_insn "neg<mode>2"
  [(set (match_operand:ANYF 0 "register_operand" "=f")
	(neg:ANYF (match_operand:ANYF 1 "register_operand" "f")))]
  "!HONOR_NANS (<MODE>mode)"
  "neg.<fmt>\t%0,%1"
  [(set_attr "type" "fneg")
   (set_attr "mode" "<UNITMODE>")])

(define_insn "one_cmpl<mode>2"
  [(set (match_operand:GPR 0 "register_operand" "=d")
	(not:GPR (match_operand:GPR 1 "register_operand" "d")))]
  ""
{
  if (TARGET_MIPS16)
    return "not\t%0,%1";
  else
    return "nor\t%0,%.,%1";
}
  [(set_attr "type" "arith")
   (set_attr "mode" "<MODE>")])

;;
;;  ....................
;;
;;	LOGICAL
;;
;;  ....................
;;

;; Many of these instructions use trivial define_expands, because we
;; want to use a different set of constraints when TARGET_MIPS16.

(define_expand "and<mode>3"
  [(set (match_operand:GPR 0 "register_operand")
	(and:GPR (match_operand:GPR 1 "register_operand")
		 (match_operand:GPR 2 "uns_arith_operand")))]
  ""
{
  if (TARGET_MIPS16)
    operands[2] = force_reg (<MODE>mode, operands[2]);
})

(define_insn "*and<mode>3"
  [(set (match_operand:GPR 0 "register_operand" "=d,d")
	(and:GPR (match_operand:GPR 1 "register_operand" "%d,d")
		 (match_operand:GPR 2 "uns_arith_operand" "d,K")))]
  "!TARGET_MIPS16"
  "@
   and\t%0,%1,%2
   andi\t%0,%1,%x2"
  [(set_attr "type" "arith")
   (set_attr "mode" "<MODE>")])

(define_insn "*and<mode>3_mips16"
  [(set (match_operand:GPR 0 "register_operand" "=d")
	(and:GPR (match_operand:GPR 1 "register_operand" "%0")
		 (match_operand:GPR 2 "register_operand" "d")))]
  "TARGET_MIPS16"
  "and\t%0,%2"
  [(set_attr "type" "arith")
   (set_attr "mode" "<MODE>")])

(define_expand "ior<mode>3"
  [(set (match_operand:GPR 0 "register_operand")
	(ior:GPR (match_operand:GPR 1 "register_operand")
		 (match_operand:GPR 2 "uns_arith_operand")))]
  ""
{
  if (TARGET_MIPS16)
    operands[2] = force_reg (<MODE>mode, operands[2]);
})

(define_insn "*ior<mode>3"
  [(set (match_operand:GPR 0 "register_operand" "=d,d")
	(ior:GPR (match_operand:GPR 1 "register_operand" "%d,d")
		 (match_operand:GPR 2 "uns_arith_operand" "d,K")))]
  "!TARGET_MIPS16"
  "@
   or\t%0,%1,%2
   ori\t%0,%1,%x2"
  [(set_attr "type" "arith")
   (set_attr "mode" "<MODE>")])

(define_insn "*ior<mode>3_mips16"
  [(set (match_operand:GPR 0 "register_operand" "=d")
	(ior:GPR (match_operand:GPR 1 "register_operand" "%0")
		 (match_operand:GPR 2 "register_operand" "d")))]
  "TARGET_MIPS16"
  "or\t%0,%2"
  [(set_attr "type" "arith")
   (set_attr "mode" "<MODE>")])

(define_expand "xor<mode>3"
  [(set (match_operand:GPR 0 "register_operand")
	(xor:GPR (match_operand:GPR 1 "register_operand")
		 (match_operand:GPR 2 "uns_arith_operand")))]
  ""
  "")

(define_insn ""
  [(set (match_operand:GPR 0 "register_operand" "=d,d")
	(xor:GPR (match_operand:GPR 1 "register_operand" "%d,d")
		 (match_operand:GPR 2 "uns_arith_operand" "d,K")))]
  "!TARGET_MIPS16"
  "@
   xor\t%0,%1,%2
   xori\t%0,%1,%x2"
  [(set_attr "type" "arith")
   (set_attr "mode" "<MODE>")])

(define_insn ""
  [(set (match_operand:GPR 0 "register_operand" "=d,t,t")
	(xor:GPR (match_operand:GPR 1 "register_operand" "%0,d,d")
		 (match_operand:GPR 2 "uns_arith_operand" "d,K,d")))]
  "TARGET_MIPS16"
  "@
   xor\t%0,%2
   cmpi\t%1,%2
   cmp\t%1,%2"
  [(set_attr "type" "arith")
   (set_attr "mode" "<MODE>")
   (set_attr_alternative "length"
		[(const_int 4)
		 (if_then_else (match_operand:VOID 2 "m16_uimm8_1")
			       (const_int 4)
			       (const_int 8))
		 (const_int 4)])])

(define_insn "*nor<mode>3"
  [(set (match_operand:GPR 0 "register_operand" "=d")
	(and:GPR (not:GPR (match_operand:GPR 1 "register_operand" "d"))
		 (not:GPR (match_operand:GPR 2 "register_operand" "d"))))]
  "!TARGET_MIPS16"
  "nor\t%0,%1,%2"
  [(set_attr "type" "arith")
   (set_attr "mode" "<MODE>")])

;;
;;  ....................
;;
;;	TRUNCATION
;;
;;  ....................



(define_insn "truncdfsf2"
  [(set (match_operand:SF 0 "register_operand" "=f")
	(float_truncate:SF (match_operand:DF 1 "register_operand" "f")))]
  "TARGET_HARD_FLOAT && TARGET_DOUBLE_FLOAT"
  "cvt.s.d\t%0,%1"
  [(set_attr "type"	"fcvt")
   (set_attr "cnv_mode"	"D2S")   
   (set_attr "mode"	"SF")])

;; Integer truncation patterns.  Truncating SImode values to smaller
;; modes is a no-op, as it is for most other GCC ports.  Truncating
;; DImode values to SImode is not a no-op for TARGET_64BIT since we
;; need to make sure that the lower 32 bits are properly sign-extended
;; (see TRULY_NOOP_TRUNCATION).  Truncating DImode values into modes
;; smaller than SImode is equivalent to two separate truncations:
;;
;;                        A       B
;;    DI ---> HI  ==  DI ---> SI ---> HI
;;    DI ---> QI  ==  DI ---> SI ---> QI
;;
;; Step A needs a real instruction but step B does not.

(define_insn "truncdisi2"
  [(set (match_operand:SI 0 "nonimmediate_operand" "=d,m")
        (truncate:SI (match_operand:DI 1 "register_operand" "d,d")))]
  "TARGET_64BIT"
  "@
    sll\t%0,%1,0
    sw\t%1,%0"
  [(set_attr "type" "shift,store")
   (set_attr "mode" "SI")
   (set_attr "extended_mips16" "yes,*")])

(define_insn "truncdihi2"
  [(set (match_operand:HI 0 "nonimmediate_operand" "=d,m")
        (truncate:HI (match_operand:DI 1 "register_operand" "d,d")))]
  "TARGET_64BIT"
  "@
    sll\t%0,%1,0
    sh\t%1,%0"
  [(set_attr "type" "shift,store")
   (set_attr "mode" "SI")
   (set_attr "extended_mips16" "yes,*")])

(define_insn "truncdiqi2"
  [(set (match_operand:QI 0 "nonimmediate_operand" "=d,m")
        (truncate:QI (match_operand:DI 1 "register_operand" "d,d")))]
  "TARGET_64BIT"
  "@
    sll\t%0,%1,0
    sb\t%1,%0"
  [(set_attr "type" "shift,store")
   (set_attr "mode" "SI")
   (set_attr "extended_mips16" "yes,*")])

;; Combiner patterns to optimize shift/truncate combinations.

(define_insn ""
  [(set (match_operand:SI 0 "register_operand" "=d")
        (truncate:SI
	  (ashiftrt:DI (match_operand:DI 1 "register_operand" "d")
                       (match_operand:DI 2 "const_arith_operand" ""))))]
  "TARGET_64BIT && !TARGET_MIPS16 && INTVAL (operands[2]) >= 32"
  "dsra\t%0,%1,%2"
  [(set_attr "type" "shift")
   (set_attr "mode" "SI")])

(define_insn ""
  [(set (match_operand:SI 0 "register_operand" "=d")
        (truncate:SI (lshiftrt:DI (match_operand:DI 1 "register_operand" "d")
                                  (const_int 32))))]
  "TARGET_64BIT && !TARGET_MIPS16"
  "dsra\t%0,%1,32"
  [(set_attr "type" "shift")
   (set_attr "mode" "SI")])


;; Combiner patterns for truncate/sign_extend combinations.  They use
;; the shift/truncate patterns above.

(define_insn_and_split ""
  [(set (match_operand:SI 0 "register_operand" "=d")
	(sign_extend:SI
	    (truncate:HI (match_operand:DI 1 "register_operand" "d"))))]
  "TARGET_64BIT && !TARGET_MIPS16"
  "#"
  "&& reload_completed"
  [(set (match_dup 2)
	(ashift:DI (match_dup 1)
		   (const_int 48)))
   (set (match_dup 0)
	(truncate:SI (ashiftrt:DI (match_dup 2)
				  (const_int 48))))]
  { operands[2] = gen_lowpart (DImode, operands[0]); })

(define_insn_and_split ""
  [(set (match_operand:SI 0 "register_operand" "=d")
	(sign_extend:SI
	    (truncate:QI (match_operand:DI 1 "register_operand" "d"))))]
  "TARGET_64BIT && !TARGET_MIPS16"
  "#"
  "&& reload_completed"
  [(set (match_dup 2)
	(ashift:DI (match_dup 1)
		   (const_int 56)))
   (set (match_dup 0)
	(truncate:SI (ashiftrt:DI (match_dup 2)
				  (const_int 56))))]
  { operands[2] = gen_lowpart (DImode, operands[0]); })


;; Combiner patterns to optimize truncate/zero_extend combinations.

(define_insn ""
  [(set (match_operand:SI 0 "register_operand" "=d")
        (zero_extend:SI (truncate:HI
                         (match_operand:DI 1 "register_operand" "d"))))]
  "TARGET_64BIT && !TARGET_MIPS16"
  "andi\t%0,%1,0xffff"
  [(set_attr "type"     "arith")
   (set_attr "mode"     "SI")])

(define_insn ""
  [(set (match_operand:SI 0 "register_operand" "=d")
        (zero_extend:SI (truncate:QI
                         (match_operand:DI 1 "register_operand" "d"))))]
  "TARGET_64BIT && !TARGET_MIPS16"
  "andi\t%0,%1,0xff"
  [(set_attr "type"     "arith")
   (set_attr "mode"     "SI")])

(define_insn ""
  [(set (match_operand:HI 0 "register_operand" "=d")
        (zero_extend:HI (truncate:QI
                         (match_operand:DI 1 "register_operand" "d"))))]
  "TARGET_64BIT && !TARGET_MIPS16"
  "andi\t%0,%1,0xff"
  [(set_attr "type"     "arith")
   (set_attr "mode"     "HI")])

;;
;;  ....................
;;
;;	ZERO EXTENSION
;;
;;  ....................

;; Extension insns.

(define_insn_and_split "zero_extendsidi2"
  [(set (match_operand:DI 0 "register_operand" "=d,d")
        (zero_extend:DI (match_operand:SI 1 "nonimmediate_operand" "d,W")))]
  "TARGET_64BIT"
  "@
   #
   lwu\t%0,%1"
  "&& reload_completed && REG_P (operands[1])"
  [(set (match_dup 0)
        (ashift:DI (match_dup 1) (const_int 32)))
   (set (match_dup 0)
        (lshiftrt:DI (match_dup 0) (const_int 32)))]
  { operands[1] = gen_lowpart (DImode, operands[1]); }
  [(set_attr "type" "multi,load")
   (set_attr "mode" "DI")
   (set_attr "length" "8,*")])

;; Combine is not allowed to convert this insn into a zero_extendsidi2
;; because of TRULY_NOOP_TRUNCATION.

(define_insn_and_split "*clear_upper32"
  [(set (match_operand:DI 0 "register_operand" "=d,d")
        (and:DI (match_operand:DI 1 "nonimmediate_operand" "d,o")
		(const_int 4294967295)))]
  "TARGET_64BIT"
{
  if (which_alternative == 0)
    return "#";

  operands[1] = gen_lowpart (SImode, operands[1]);
  return "lwu\t%0,%1";
}
  "&& reload_completed && REG_P (operands[1])"
  [(set (match_dup 0)
        (ashift:DI (match_dup 1) (const_int 32)))
   (set (match_dup 0)
        (lshiftrt:DI (match_dup 0) (const_int 32)))]
  ""
  [(set_attr "type" "multi,load")
   (set_attr "mode" "DI")
   (set_attr "length" "8,*")])

(define_expand "zero_extend<SHORT:mode><GPR:mode>2"
  [(set (match_operand:GPR 0 "register_operand")
        (zero_extend:GPR (match_operand:SHORT 1 "nonimmediate_operand")))]
  ""
{
  if (TARGET_MIPS16 && !GENERATE_MIPS16E
      && !memory_operand (operands[1], <SHORT:MODE>mode))
    {
      emit_insn (gen_and<GPR:mode>3 (operands[0],
				     gen_lowpart (<GPR:MODE>mode, operands[1]),
				     force_reg (<GPR:MODE>mode,
						GEN_INT (<SHORT:mask>))));
      DONE;
    }
})

(define_insn "*zero_extend<SHORT:mode><GPR:mode>2"
  [(set (match_operand:GPR 0 "register_operand" "=d,d")
        (zero_extend:GPR
	     (match_operand:SHORT 1 "nonimmediate_operand" "d,m")))]
  "!TARGET_MIPS16"
  "@
   andi\t%0,%1,<SHORT:mask>
   l<SHORT:size>u\t%0,%1"
  [(set_attr "type" "arith,load")
   (set_attr "mode" "<GPR:MODE>")])

(define_insn "*zero_extend<SHORT:mode><GPR:mode>2_mips16e"
  [(set (match_operand:GPR 0 "register_operand" "=d")
        (zero_extend:GPR (match_operand:SHORT 1 "register_operand" "0")))]
  "GENERATE_MIPS16E"
  "ze<SHORT:size>\t%0"
  [(set_attr "type" "arith")
   (set_attr "mode" "<GPR:MODE>")])

(define_insn "*zero_extend<SHORT:mode><GPR:mode>2_mips16"
  [(set (match_operand:GPR 0 "register_operand" "=d")
        (zero_extend:GPR (match_operand:SHORT 1 "memory_operand" "m")))]
  "TARGET_MIPS16"
  "l<SHORT:size>u\t%0,%1"
  [(set_attr "type" "load")
   (set_attr "mode" "<GPR:MODE>")])

(define_expand "zero_extendqihi2"
  [(set (match_operand:HI 0 "register_operand")
	(zero_extend:HI (match_operand:QI 1 "nonimmediate_operand")))]
  ""
{
  if (TARGET_MIPS16 && !memory_operand (operands[1], QImode))
    {
      emit_insn (gen_zero_extendqisi2 (gen_lowpart (SImode, operands[0]),
				       operands[1]));
      DONE;
    }
})

(define_insn "*zero_extendqihi2"
  [(set (match_operand:HI 0 "register_operand" "=d,d")
        (zero_extend:HI (match_operand:QI 1 "nonimmediate_operand" "d,m")))]
  "!TARGET_MIPS16"
  "@
   andi\t%0,%1,0x00ff
   lbu\t%0,%1"
  [(set_attr "type" "arith,load")
   (set_attr "mode" "HI")])

(define_insn "*zero_extendqihi2_mips16"
  [(set (match_operand:HI 0 "register_operand" "=d")
        (zero_extend:HI (match_operand:QI 1 "memory_operand" "m")))]
  "TARGET_MIPS16"
  "lbu\t%0,%1"
  [(set_attr "type" "load")
   (set_attr "mode" "HI")])

;;
;;  ....................
;;
;;	SIGN EXTENSION
;;
;;  ....................

;; Extension insns.
;; Those for integer source operand are ordered widest source type first.

;; When TARGET_64BIT, all SImode integer registers should already be in
;; sign-extended form (see TRULY_NOOP_TRUNCATION and truncdisi2).  We can
;; therefore get rid of register->register instructions if we constrain
;; the source to be in the same register as the destination.
;;
;; The register alternative has type "arith" so that the pre-reload
;; scheduler will treat it as a move.  This reflects what happens if
;; the register alternative needs a reload.
(define_insn_and_split "extendsidi2"
  [(set (match_operand:DI 0 "register_operand" "=d,d")
        (sign_extend:DI (match_operand:SI 1 "nonimmediate_operand" "0,m")))]
  "TARGET_64BIT"
  "@
   #
   lw\t%0,%1"
  "&& reload_completed && register_operand (operands[1], VOIDmode)"
  [(const_int 0)]
{
  emit_note (NOTE_INSN_DELETED);
  DONE;
}
  [(set_attr "type" "arith,load")
   (set_attr "mode" "DI")])

(define_expand "extend<SHORT:mode><GPR:mode>2"
  [(set (match_operand:GPR 0 "register_operand")
        (sign_extend:GPR (match_operand:SHORT 1 "nonimmediate_operand")))]
  "")

(define_insn "*extend<SHORT:mode><GPR:mode>2_mips16e"
  [(set (match_operand:GPR 0 "register_operand" "=d,d")
        (sign_extend:GPR (match_operand:SHORT 1 "nonimmediate_operand" "0,m")))]
  "GENERATE_MIPS16E"
  "@
   se<SHORT:size>\t%0
   l<SHORT:size>\t%0,%1"
  [(set_attr "type" "arith,load")
   (set_attr "mode" "<GPR:MODE>")])

(define_insn_and_split "*extend<SHORT:mode><GPR:mode>2"
  [(set (match_operand:GPR 0 "register_operand" "=d,d")
        (sign_extend:GPR
	     (match_operand:SHORT 1 "nonimmediate_operand" "d,m")))]
  "!ISA_HAS_SEB_SEH && !GENERATE_MIPS16E"
  "@
   #
   l<SHORT:size>\t%0,%1"
  "&& reload_completed && REG_P (operands[1])"
  [(set (match_dup 0) (ashift:GPR (match_dup 1) (match_dup 2)))
   (set (match_dup 0) (ashiftrt:GPR (match_dup 0) (match_dup 2)))]
{
  operands[1] = gen_lowpart (<GPR:MODE>mode, operands[1]);
  operands[2] = GEN_INT (GET_MODE_BITSIZE (<GPR:MODE>mode)
			 - GET_MODE_BITSIZE (<SHORT:MODE>mode));
}
  [(set_attr "type" "arith,load")
   (set_attr "mode" "<GPR:MODE>")
   (set_attr "length" "8,*")])

(define_insn "*extend<SHORT:mode><GPR:mode>2_se<SHORT:size>"
  [(set (match_operand:GPR 0 "register_operand" "=d,d")
        (sign_extend:GPR
	     (match_operand:SHORT 1 "nonimmediate_operand" "d,m")))]
  "ISA_HAS_SEB_SEH"
  "@
   se<SHORT:size>\t%0,%1
   l<SHORT:size>\t%0,%1"
  [(set_attr "type" "arith,load")
   (set_attr "mode" "<GPR:MODE>")])

;; This pattern generates the same code as extendqisi2; split it into
;; that form after reload.
(define_insn_and_split "extendqihi2"
  [(set (match_operand:HI 0 "register_operand" "=d,d")
        (sign_extend:HI (match_operand:QI 1 "nonimmediate_operand" "d,m")))]
  ""
  "#"
  "reload_completed"
  [(set (match_dup 0) (sign_extend:SI (match_dup 1)))]
  { operands[0] = gen_lowpart (SImode, operands[0]); }
  [(set_attr "type" "arith,load")
   (set_attr "mode" "SI")
   (set_attr "length" "8,*")])

(define_insn "extendsfdf2"
  [(set (match_operand:DF 0 "register_operand" "=f")
	(float_extend:DF (match_operand:SF 1 "register_operand" "f")))]
  "TARGET_HARD_FLOAT && TARGET_DOUBLE_FLOAT"
  "cvt.d.s\t%0,%1"
  [(set_attr "type"	"fcvt")
   (set_attr "cnv_mode"	"S2D")   
   (set_attr "mode"	"DF")])

;;
;;  ....................
;;
;;	CONVERSIONS
;;
;;  ....................

(define_expand "fix_truncdfsi2"
  [(set (match_operand:SI 0 "register_operand")
	(fix:SI (match_operand:DF 1 "register_operand")))]
  "TARGET_HARD_FLOAT && TARGET_DOUBLE_FLOAT"
{
  if (!ISA_HAS_TRUNC_W)
    {
      emit_insn (gen_fix_truncdfsi2_macro (operands[0], operands[1]));
      DONE;
    }
})

(define_insn "fix_truncdfsi2_insn"
  [(set (match_operand:SI 0 "register_operand" "=f")
	(fix:SI (match_operand:DF 1 "register_operand" "f")))]
  "TARGET_HARD_FLOAT && TARGET_DOUBLE_FLOAT && ISA_HAS_TRUNC_W"
  "trunc.w.d %0,%1"
  [(set_attr "type"	"fcvt")
   (set_attr "mode"	"DF")
   (set_attr "cnv_mode"	"D2I")
   (set_attr "length"	"4")])

(define_insn "fix_truncdfsi2_macro"
  [(set (match_operand:SI 0 "register_operand" "=f")
	(fix:SI (match_operand:DF 1 "register_operand" "f")))
   (clobber (match_scratch:DF 2 "=d"))]
  "TARGET_HARD_FLOAT && TARGET_DOUBLE_FLOAT && !ISA_HAS_TRUNC_W"
{
  if (set_nomacro)
    return ".set\tmacro\;trunc.w.d %0,%1,%2\;.set\tnomacro";
  else
    return "trunc.w.d %0,%1,%2";
}
  [(set_attr "type"	"fcvt")
   (set_attr "mode"	"DF")
   (set_attr "cnv_mode"	"D2I")
   (set_attr "length"	"36")])

(define_expand "fix_truncsfsi2"
  [(set (match_operand:SI 0 "register_operand")
	(fix:SI (match_operand:SF 1 "register_operand")))]
  "TARGET_HARD_FLOAT"
{
  if (!ISA_HAS_TRUNC_W)
    {
      emit_insn (gen_fix_truncsfsi2_macro (operands[0], operands[1]));
      DONE;
    }
})

(define_insn "fix_truncsfsi2_insn"
  [(set (match_operand:SI 0 "register_operand" "=f")
	(fix:SI (match_operand:SF 1 "register_operand" "f")))]
  "TARGET_HARD_FLOAT && ISA_HAS_TRUNC_W"
  "trunc.w.s %0,%1"
  [(set_attr "type"	"fcvt")
   (set_attr "mode"	"SF")
   (set_attr "cnv_mode"	"S2I")
   (set_attr "length"	"4")])

(define_insn "fix_truncsfsi2_macro"
  [(set (match_operand:SI 0 "register_operand" "=f")
	(fix:SI (match_operand:SF 1 "register_operand" "f")))
   (clobber (match_scratch:SF 2 "=d"))]
  "TARGET_HARD_FLOAT && !ISA_HAS_TRUNC_W"
{
  if (set_nomacro)
    return ".set\tmacro\;trunc.w.s %0,%1,%2\;.set\tnomacro";
  else
    return "trunc.w.s %0,%1,%2";
}
  [(set_attr "type"	"fcvt")
   (set_attr "mode"	"SF")
   (set_attr "cnv_mode"	"S2I")
   (set_attr "length"	"36")])


(define_insn "fix_truncdfdi2"
  [(set (match_operand:DI 0 "register_operand" "=f")
	(fix:DI (match_operand:DF 1 "register_operand" "f")))]
  "TARGET_HARD_FLOAT && TARGET_FLOAT64 && TARGET_DOUBLE_FLOAT"
  "trunc.l.d %0,%1"
  [(set_attr "type"	"fcvt")
   (set_attr "mode"	"DF")
   (set_attr "cnv_mode"	"D2I")
   (set_attr "length"	"4")])


(define_insn "fix_truncsfdi2"
  [(set (match_operand:DI 0 "register_operand" "=f")
	(fix:DI (match_operand:SF 1 "register_operand" "f")))]
  "TARGET_HARD_FLOAT && TARGET_FLOAT64 && TARGET_DOUBLE_FLOAT"
  "trunc.l.s %0,%1"
  [(set_attr "type"	"fcvt")
   (set_attr "mode"	"SF")
   (set_attr "cnv_mode"	"S2I")
   (set_attr "length"	"4")])


(define_insn "floatsidf2"
  [(set (match_operand:DF 0 "register_operand" "=f")
	(float:DF (match_operand:SI 1 "register_operand" "f")))]
  "TARGET_HARD_FLOAT && TARGET_DOUBLE_FLOAT"
  "cvt.d.w\t%0,%1"
  [(set_attr "type"	"fcvt")
   (set_attr "mode"	"DF")
   (set_attr "cnv_mode"	"I2D")   
   (set_attr "length"	"4")])


(define_insn "floatdidf2"
  [(set (match_operand:DF 0 "register_operand" "=f")
	(float:DF (match_operand:DI 1 "register_operand" "f")))]
  "TARGET_HARD_FLOAT && TARGET_FLOAT64 && TARGET_DOUBLE_FLOAT"
  "cvt.d.l\t%0,%1"
  [(set_attr "type"	"fcvt")
   (set_attr "mode"	"DF")
   (set_attr "cnv_mode"	"I2D")   
   (set_attr "length"	"4")])


(define_insn "floatsisf2"
  [(set (match_operand:SF 0 "register_operand" "=f")
	(float:SF (match_operand:SI 1 "register_operand" "f")))]
  "TARGET_HARD_FLOAT"
  "cvt.s.w\t%0,%1"
  [(set_attr "type"	"fcvt")
   (set_attr "mode"	"SF")
   (set_attr "cnv_mode"	"I2S")   
   (set_attr "length"	"4")])


(define_insn "floatdisf2"
  [(set (match_operand:SF 0 "register_operand" "=f")
	(float:SF (match_operand:DI 1 "register_operand" "f")))]
  "TARGET_HARD_FLOAT && TARGET_FLOAT64 && TARGET_DOUBLE_FLOAT"
  "cvt.s.l\t%0,%1"
  [(set_attr "type"	"fcvt")
   (set_attr "mode"	"SF")
   (set_attr "cnv_mode"	"I2S")   
   (set_attr "length"	"4")])


(define_expand "fixuns_truncdfsi2"
  [(set (match_operand:SI 0 "register_operand")
	(unsigned_fix:SI (match_operand:DF 1 "register_operand")))]
  "TARGET_HARD_FLOAT && TARGET_DOUBLE_FLOAT"
{
  rtx reg1 = gen_reg_rtx (DFmode);
  rtx reg2 = gen_reg_rtx (DFmode);
  rtx reg3 = gen_reg_rtx (SImode);
  rtx label1 = gen_label_rtx ();
  rtx label2 = gen_label_rtx ();
  REAL_VALUE_TYPE offset;

  real_2expN (&offset, 31);

  if (reg1)			/* Turn off complaints about unreached code.  */
    {
      emit_move_insn (reg1, CONST_DOUBLE_FROM_REAL_VALUE (offset, DFmode));
      do_pending_stack_adjust ();

      emit_insn (gen_cmpdf (operands[1], reg1));
      emit_jump_insn (gen_bge (label1));

      emit_insn (gen_fix_truncdfsi2 (operands[0], operands[1]));
      emit_jump_insn (gen_rtx_SET (VOIDmode, pc_rtx,
				   gen_rtx_LABEL_REF (VOIDmode, label2)));
      emit_barrier ();

      emit_label (label1);
      emit_move_insn (reg2, gen_rtx_MINUS (DFmode, operands[1], reg1));
      emit_move_insn (reg3, GEN_INT (trunc_int_for_mode
				     (BITMASK_HIGH, SImode)));

      emit_insn (gen_fix_truncdfsi2 (operands[0], reg2));
      emit_insn (gen_iorsi3 (operands[0], operands[0], reg3));

      emit_label (label2);

      /* Allow REG_NOTES to be set on last insn (labels don't have enough
	 fields, and can't be used for REG_NOTES anyway).  */
      emit_insn (gen_rtx_USE (VOIDmode, stack_pointer_rtx));
      DONE;
    }
})


(define_expand "fixuns_truncdfdi2"
  [(set (match_operand:DI 0 "register_operand")
	(unsigned_fix:DI (match_operand:DF 1 "register_operand")))]
  "TARGET_HARD_FLOAT && TARGET_64BIT && TARGET_DOUBLE_FLOAT"
{
  rtx reg1 = gen_reg_rtx (DFmode);
  rtx reg2 = gen_reg_rtx (DFmode);
  rtx reg3 = gen_reg_rtx (DImode);
  rtx label1 = gen_label_rtx ();
  rtx label2 = gen_label_rtx ();
  REAL_VALUE_TYPE offset;

  real_2expN (&offset, 63);

  emit_move_insn (reg1, CONST_DOUBLE_FROM_REAL_VALUE (offset, DFmode));
  do_pending_stack_adjust ();

  emit_insn (gen_cmpdf (operands[1], reg1));
  emit_jump_insn (gen_bge (label1));

  emit_insn (gen_fix_truncdfdi2 (operands[0], operands[1]));
  emit_jump_insn (gen_rtx_SET (VOIDmode, pc_rtx,
			       gen_rtx_LABEL_REF (VOIDmode, label2)));
  emit_barrier ();

  emit_label (label1);
  emit_move_insn (reg2, gen_rtx_MINUS (DFmode, operands[1], reg1));
  emit_move_insn (reg3, GEN_INT (BITMASK_HIGH));
  emit_insn (gen_ashldi3 (reg3, reg3, GEN_INT (32)));

  emit_insn (gen_fix_truncdfdi2 (operands[0], reg2));
  emit_insn (gen_iordi3 (operands[0], operands[0], reg3));

  emit_label (label2);

  /* Allow REG_NOTES to be set on last insn (labels don't have enough
     fields, and can't be used for REG_NOTES anyway).  */
  emit_insn (gen_rtx_USE (VOIDmode, stack_pointer_rtx));
  DONE;
})


(define_expand "fixuns_truncsfsi2"
  [(set (match_operand:SI 0 "register_operand")
	(unsigned_fix:SI (match_operand:SF 1 "register_operand")))]
  "TARGET_HARD_FLOAT"
{
  rtx reg1 = gen_reg_rtx (SFmode);
  rtx reg2 = gen_reg_rtx (SFmode);
  rtx reg3 = gen_reg_rtx (SImode);
  rtx label1 = gen_label_rtx ();
  rtx label2 = gen_label_rtx ();
  REAL_VALUE_TYPE offset;

  real_2expN (&offset, 31);

  emit_move_insn (reg1, CONST_DOUBLE_FROM_REAL_VALUE (offset, SFmode));
  do_pending_stack_adjust ();

  emit_insn (gen_cmpsf (operands[1], reg1));
  emit_jump_insn (gen_bge (label1));

  emit_insn (gen_fix_truncsfsi2 (operands[0], operands[1]));
  emit_jump_insn (gen_rtx_SET (VOIDmode, pc_rtx,
			       gen_rtx_LABEL_REF (VOIDmode, label2)));
  emit_barrier ();

  emit_label (label1);
  emit_move_insn (reg2, gen_rtx_MINUS (SFmode, operands[1], reg1));
  emit_move_insn (reg3, GEN_INT (trunc_int_for_mode
				 (BITMASK_HIGH, SImode)));

  emit_insn (gen_fix_truncsfsi2 (operands[0], reg2));
  emit_insn (gen_iorsi3 (operands[0], operands[0], reg3));

  emit_label (label2);

  /* Allow REG_NOTES to be set on last insn (labels don't have enough
     fields, and can't be used for REG_NOTES anyway).  */
  emit_insn (gen_rtx_USE (VOIDmode, stack_pointer_rtx));
  DONE;
})


(define_expand "fixuns_truncsfdi2"
  [(set (match_operand:DI 0 "register_operand")
	(unsigned_fix:DI (match_operand:SF 1 "register_operand")))]
  "TARGET_HARD_FLOAT && TARGET_64BIT && TARGET_DOUBLE_FLOAT"
{
  rtx reg1 = gen_reg_rtx (SFmode);
  rtx reg2 = gen_reg_rtx (SFmode);
  rtx reg3 = gen_reg_rtx (DImode);
  rtx label1 = gen_label_rtx ();
  rtx label2 = gen_label_rtx ();
  REAL_VALUE_TYPE offset;

  real_2expN (&offset, 63);

  emit_move_insn (reg1, CONST_DOUBLE_FROM_REAL_VALUE (offset, SFmode));
  do_pending_stack_adjust ();

  emit_insn (gen_cmpsf (operands[1], reg1));
  emit_jump_insn (gen_bge (label1));

  emit_insn (gen_fix_truncsfdi2 (operands[0], operands[1]));
  emit_jump_insn (gen_rtx_SET (VOIDmode, pc_rtx,
			       gen_rtx_LABEL_REF (VOIDmode, label2)));
  emit_barrier ();

  emit_label (label1);
  emit_move_insn (reg2, gen_rtx_MINUS (SFmode, operands[1], reg1));
  emit_move_insn (reg3, GEN_INT (BITMASK_HIGH));
  emit_insn (gen_ashldi3 (reg3, reg3, GEN_INT (32)));

  emit_insn (gen_fix_truncsfdi2 (operands[0], reg2));
  emit_insn (gen_iordi3 (operands[0], operands[0], reg3));

  emit_label (label2);

  /* Allow REG_NOTES to be set on last insn (labels don't have enough
     fields, and can't be used for REG_NOTES anyway).  */
  emit_insn (gen_rtx_USE (VOIDmode, stack_pointer_rtx));
  DONE;
})

;;
;;  ....................
;;
;;	DATA MOVEMENT
;;
;;  ....................

;; Bit field extract patterns which use lwl/lwr or ldl/ldr.

(define_expand "extv"
  [(set (match_operand 0 "register_operand")
	(sign_extract (match_operand:QI 1 "memory_operand")
		      (match_operand 2 "immediate_operand")
		      (match_operand 3 "immediate_operand")))]
  "!TARGET_MIPS16"
{
  if (mips_expand_unaligned_load (operands[0], operands[1],
				  INTVAL (operands[2]),
				  INTVAL (operands[3])))
    DONE;
  else
    FAIL;
})

(define_expand "extzv"
  [(set (match_operand 0 "register_operand")
	(zero_extract (match_operand 1 "nonimmediate_operand")
		      (match_operand 2 "immediate_operand")
		      (match_operand 3 "immediate_operand")))]
  "!TARGET_MIPS16"
{
  if (mips_expand_unaligned_load (operands[0], operands[1],
				  INTVAL (operands[2]),
				  INTVAL (operands[3])))
    DONE;
  else if (mips_use_ins_ext_p (operands[1], operands[2], operands[3]))
    {
      if (GET_MODE (operands[0]) == DImode)
        emit_insn (gen_extzvdi (operands[0], operands[1], operands[2],
				operands[3]));
      else
        emit_insn (gen_extzvsi (operands[0], operands[1], operands[2],
				operands[3]));
      DONE;
    }
  else
    FAIL;
})

(define_insn "extzv<mode>"
  [(set (match_operand:GPR 0 "register_operand" "=d")
	(zero_extract:GPR (match_operand:GPR 1 "register_operand" "d")
			  (match_operand:SI 2 "immediate_operand" "I")
			  (match_operand:SI 3 "immediate_operand" "I")))]
  "mips_use_ins_ext_p (operands[1], operands[2], operands[3])"
  "<d>ext\t%0,%1,%3,%2"
  [(set_attr "type"	"arith")
   (set_attr "mode"	"<MODE>")])


(define_expand "insv"
  [(set (zero_extract (match_operand 0 "nonimmediate_operand")
		      (match_operand 1 "immediate_operand")
		      (match_operand 2 "immediate_operand"))
	(match_operand 3 "reg_or_0_operand"))]
  "!TARGET_MIPS16"
{
  if (mips_expand_unaligned_store (operands[0], operands[3],
				   INTVAL (operands[1]),
				   INTVAL (operands[2])))
    DONE;
  else if (mips_use_ins_ext_p (operands[0], operands[1], operands[2]))
    {
      if (GET_MODE (operands[0]) == DImode)
        emit_insn (gen_insvdi (operands[0], operands[1], operands[2],
			       operands[3]));
      else
        emit_insn (gen_insvsi (operands[0], operands[1], operands[2],
			       operands[3]));
      DONE;
   }
   else
     FAIL;
})

(define_insn "insv<mode>"
  [(set (zero_extract:GPR (match_operand:GPR 0 "register_operand" "+d")
			  (match_operand:SI 1 "immediate_operand" "I")
			  (match_operand:SI 2 "immediate_operand" "I"))
	(match_operand:GPR 3 "reg_or_0_operand" "dJ"))]
  "mips_use_ins_ext_p (operands[0], operands[1], operands[2])"
  "<d>ins\t%0,%z3,%2,%1"
  [(set_attr "type"	"arith")
   (set_attr "mode"	"<MODE>")])

;; Unaligned word moves generated by the bit field patterns.
;;
;; As far as the rtl is concerned, both the left-part and right-part
;; instructions can access the whole field.  However, the real operand
;; refers to just the first or the last byte (depending on endianness).
;; We therefore use two memory operands to each instruction, one to
;; describe the rtl effect and one to use in the assembly output.
;;
;; Operands 0 and 1 are the rtl-level target and source respectively.
;; This allows us to use the standard length calculations for the "load"
;; and "store" type attributes.

(define_insn "mov_<load>l"
  [(set (match_operand:GPR 0 "register_operand" "=d")
	(unspec:GPR [(match_operand:BLK 1 "memory_operand" "m")
		     (match_operand:QI 2 "memory_operand" "m")]
		    UNSPEC_LOAD_LEFT))]
  "!TARGET_MIPS16 && mips_mem_fits_mode_p (<MODE>mode, operands[1])"
  "<load>l\t%0,%2"
  [(set_attr "type" "load")
   (set_attr "mode" "<MODE>")])

(define_insn "mov_<load>r"
  [(set (match_operand:GPR 0 "register_operand" "=d")
	(unspec:GPR [(match_operand:BLK 1 "memory_operand" "m")
		     (match_operand:QI 2 "memory_operand" "m")
		     (match_operand:GPR 3 "register_operand" "0")]
		    UNSPEC_LOAD_RIGHT))]
  "!TARGET_MIPS16 && mips_mem_fits_mode_p (<MODE>mode, operands[1])"
  "<load>r\t%0,%2"
  [(set_attr "type" "load")
   (set_attr "mode" "<MODE>")])

(define_insn "mov_<store>l"
  [(set (match_operand:BLK 0 "memory_operand" "=m")
	(unspec:BLK [(match_operand:GPR 1 "reg_or_0_operand" "dJ")
		     (match_operand:QI 2 "memory_operand" "m")]
		    UNSPEC_STORE_LEFT))]
  "!TARGET_MIPS16 && mips_mem_fits_mode_p (<MODE>mode, operands[0])"
  "<store>l\t%z1,%2"
  [(set_attr "type" "store")
   (set_attr "mode" "<MODE>")])

(define_insn "mov_<store>r"
  [(set (match_operand:BLK 0 "memory_operand" "+m")
	(unspec:BLK [(match_operand:GPR 1 "reg_or_0_operand" "dJ")
		     (match_operand:QI 2 "memory_operand" "m")
		     (match_dup 0)]
		    UNSPEC_STORE_RIGHT))]
  "!TARGET_MIPS16 && mips_mem_fits_mode_p (<MODE>mode, operands[0])"
  "<store>r\t%z1,%2"
  [(set_attr "type" "store")
   (set_attr "mode" "<MODE>")])

;; An instruction to calculate the high part of a 64-bit SYMBOL_GENERAL.
;; The required value is:
;;
;;	(%highest(op1) << 48) + (%higher(op1) << 32) + (%hi(op1) << 16)
;;
;; which translates to:
;;
;;	lui	op0,%highest(op1)
;;	daddiu	op0,op0,%higher(op1)
;;	dsll	op0,op0,16
;;	daddiu	op0,op0,%hi(op1)
;;	dsll	op0,op0,16
;;
;; The split is deferred until after flow2 to allow the peephole2 below
;; to take effect.
(define_insn_and_split "*lea_high64"
  [(set (match_operand:DI 0 "register_operand" "=d")
	(high:DI (match_operand:DI 1 "general_symbolic_operand" "")))]
  "TARGET_EXPLICIT_RELOCS && ABI_HAS_64BIT_SYMBOLS"
  "#"
  "&& flow2_completed"
  [(set (match_dup 0) (high:DI (match_dup 2)))
   (set (match_dup 0) (lo_sum:DI (match_dup 0) (match_dup 2)))
   (set (match_dup 0) (ashift:DI (match_dup 0) (const_int 16)))
   (set (match_dup 0) (lo_sum:DI (match_dup 0) (match_dup 3)))
   (set (match_dup 0) (ashift:DI (match_dup 0) (const_int 16)))]
{
  operands[2] = mips_unspec_address (operands[1], SYMBOL_64_HIGH);
  operands[3] = mips_unspec_address (operands[1], SYMBOL_64_MID);
}
  [(set_attr "length" "20")])

;; Use a scratch register to reduce the latency of the above pattern
;; on superscalar machines.  The optimized sequence is:
;;
;;	lui	op1,%highest(op2)
;;	lui	op0,%hi(op2)
;;	daddiu	op1,op1,%higher(op2)
;;	dsll32	op1,op1,0
;;	daddu	op1,op1,op0
(define_peephole2
  [(set (match_operand:DI 1 "register_operand")
	(high:DI (match_operand:DI 2 "general_symbolic_operand")))
   (match_scratch:DI 0 "d")]
  "TARGET_EXPLICIT_RELOCS && ABI_HAS_64BIT_SYMBOLS"
  [(set (match_dup 1) (high:DI (match_dup 3)))
   (set (match_dup 0) (high:DI (match_dup 4)))
   (set (match_dup 1) (lo_sum:DI (match_dup 1) (match_dup 3)))
   (set (match_dup 1) (ashift:DI (match_dup 1) (const_int 32)))
   (set (match_dup 1) (plus:DI (match_dup 1) (match_dup 0)))]
{
  operands[3] = mips_unspec_address (operands[2], SYMBOL_64_HIGH);
  operands[4] = mips_unspec_address (operands[2], SYMBOL_64_LOW);
})

;; On most targets, the expansion of (lo_sum (high X) X) for a 64-bit
;; SYMBOL_GENERAL X will take 6 cycles.  This next pattern allows combine
;; to merge the HIGH and LO_SUM parts of a move if the HIGH part is only
;; used once.  We can then use the sequence:
;;
;;	lui	op0,%highest(op1)
;;	lui	op2,%hi(op1)
;;	daddiu	op0,op0,%higher(op1)
;;	daddiu	op2,op2,%lo(op1)
;;	dsll32	op0,op0,0
;;	daddu	op0,op0,op2
;;
;; which takes 4 cycles on most superscalar targets.
(define_insn_and_split "*lea64"
  [(set (match_operand:DI 0 "register_operand" "=d")
	(match_operand:DI 1 "general_symbolic_operand" ""))
   (clobber (match_scratch:DI 2 "=&d"))]
  "TARGET_EXPLICIT_RELOCS && ABI_HAS_64BIT_SYMBOLS && cse_not_expected"
  "#"
  "&& reload_completed"
  [(set (match_dup 0) (high:DI (match_dup 3)))
   (set (match_dup 2) (high:DI (match_dup 4)))
   (set (match_dup 0) (lo_sum:DI (match_dup 0) (match_dup 3)))
   (set (match_dup 2) (lo_sum:DI (match_dup 2) (match_dup 4)))
   (set (match_dup 0) (ashift:DI (match_dup 0) (const_int 32)))
   (set (match_dup 0) (plus:DI (match_dup 0) (match_dup 2)))]
{
  operands[3] = mips_unspec_address (operands[1], SYMBOL_64_HIGH);
  operands[4] = mips_unspec_address (operands[1], SYMBOL_64_LOW);
}
  [(set_attr "length" "24")])

;; Insns to fetch a global symbol from a big GOT.

(define_insn_and_split "*xgot_hi<mode>"
  [(set (match_operand:P 0 "register_operand" "=d")
	(high:P (match_operand:P 1 "global_got_operand" "")))]
  "TARGET_EXPLICIT_RELOCS && TARGET_XGOT"
  "#"
  "&& reload_completed"
  [(set (match_dup 0) (high:P (match_dup 2)))
   (set (match_dup 0) (plus:P (match_dup 0) (match_dup 3)))]
{
  operands[2] = mips_unspec_address (operands[1], SYMBOL_GOTOFF_GLOBAL);
  operands[3] = pic_offset_table_rtx;
}
  [(set_attr "got" "xgot_high")
   (set_attr "mode" "<MODE>")])

(define_insn_and_split "*xgot_lo<mode>"
  [(set (match_operand:P 0 "register_operand" "=d")
	(lo_sum:P (match_operand:P 1 "register_operand" "d")
		  (match_operand:P 2 "global_got_operand" "")))]
  "TARGET_EXPLICIT_RELOCS && TARGET_XGOT"
  "#"
  "&& reload_completed"
  [(set (match_dup 0)
	(unspec:P [(match_dup 1) (match_dup 3)] UNSPEC_LOAD_GOT))]
  { operands[3] = mips_unspec_address (operands[2], SYMBOL_GOTOFF_GLOBAL); }
  [(set_attr "got" "load")
   (set_attr "mode" "<MODE>")])

;; Insns to fetch a global symbol from a normal GOT.

(define_insn_and_split "*got_disp<mode>"
  [(set (match_operand:P 0 "register_operand" "=d")
	(match_operand:P 1 "global_got_operand" ""))]
  "TARGET_EXPLICIT_RELOCS && !TARGET_XGOT"
  "#"
  "&& reload_completed"
  [(set (match_dup 0)
	(unspec:P [(match_dup 2) (match_dup 3)] UNSPEC_LOAD_GOT))]
{
  operands[2] = pic_offset_table_rtx;
  operands[3] = mips_unspec_address (operands[1], SYMBOL_GOTOFF_GLOBAL);
}
  [(set_attr "got" "load")
   (set_attr "mode" "<MODE>")])

;; Insns for loading the high part of a local symbol.

(define_insn_and_split "*got_page<mode>"
  [(set (match_operand:P 0 "register_operand" "=d")
	(high:P (match_operand:P 1 "local_got_operand" "")))]
  "TARGET_EXPLICIT_RELOCS"
  "#"
  "&& reload_completed"
  [(set (match_dup 0)
	(unspec:P [(match_dup 2) (match_dup 3)] UNSPEC_LOAD_GOT))]
{
  operands[2] = pic_offset_table_rtx;
  operands[3] = mips_unspec_address (operands[1], SYMBOL_GOTOFF_PAGE);
}
  [(set_attr "got" "load")
   (set_attr "mode" "<MODE>")])

;; Lower-level instructions for loading an address from the GOT.
;; We could use MEMs, but an unspec gives more optimization
;; opportunities.

(define_insn "load_got<mode>"
  [(set (match_operand:P 0 "register_operand" "=d")
	(unspec:P [(match_operand:P 1 "register_operand" "d")
		   (match_operand:P 2 "immediate_operand" "")]
		  UNSPEC_LOAD_GOT))]
  ""
  "<load>\t%0,%R2(%1)"
  [(set_attr "type" "load")
   (set_attr "mode" "<MODE>")
   (set_attr "length" "4")])

;; Instructions for adding the low 16 bits of an address to a register.
;; Operand 2 is the address: print_operand works out which relocation
;; should be applied.

(define_insn "*low<mode>"
  [(set (match_operand:P 0 "register_operand" "=d")
	(lo_sum:P (match_operand:P 1 "register_operand" "d")
		  (match_operand:P 2 "immediate_operand" "")))]
  "!TARGET_MIPS16"
  "<d>addiu\t%0,%1,%R2"
  [(set_attr "type" "arith")
   (set_attr "mode" "<MODE>")])

(define_insn "*low<mode>_mips16"
  [(set (match_operand:P 0 "register_operand" "=d")
	(lo_sum:P (match_operand:P 1 "register_operand" "0")
		  (match_operand:P 2 "immediate_operand" "")))]
  "TARGET_MIPS16"
  "<d>addiu\t%0,%R2"
  [(set_attr "type" "arith")
   (set_attr "mode" "<MODE>")
   (set_attr "length" "8")])

;; Allow combine to split complex const_int load sequences, using operand 2
;; to store the intermediate results.  See move_operand for details.
(define_split
  [(set (match_operand:GPR 0 "register_operand")
	(match_operand:GPR 1 "splittable_const_int_operand"))
   (clobber (match_operand:GPR 2 "register_operand"))]
  ""
  [(const_int 0)]
{
  mips_move_integer (operands[0], operands[2], INTVAL (operands[1]));
  DONE;
})

;; Likewise, for symbolic operands.
(define_split
  [(set (match_operand:P 0 "register_operand")
	(match_operand:P 1 "splittable_symbolic_operand"))
   (clobber (match_operand:P 2 "register_operand"))]
  ""
  [(set (match_dup 0) (match_dup 1))]
  { operands[1] = mips_split_symbol (operands[2], operands[1]); })

;; 64-bit integer moves

;; Unlike most other insns, the move insns can't be split with
;; different predicates, because register spilling and other parts of
;; the compiler, have memoized the insn number already.

(define_expand "movdi"
  [(set (match_operand:DI 0 "")
	(match_operand:DI 1 ""))]
  ""
{
  if (mips_legitimize_move (DImode, operands[0], operands[1]))
    DONE;
})

;; For mips16, we need a special case to handle storing $31 into
;; memory, since we don't have a constraint to match $31.  This
;; instruction can be generated by save_restore_insns.

(define_insn "*mov<mode>_ra"
  [(set (match_operand:GPR 0 "stack_operand" "=m")
	(reg:GPR 31))]
  "TARGET_MIPS16"
  "<store>\t$31,%0"
  [(set_attr "type" "store")
   (set_attr "mode" "<MODE>")])

(define_insn "*movdi_32bit"
  [(set (match_operand:DI 0 "nonimmediate_operand" "=d,d,d,m,*a,*d,*B*C*D,*B*C*D,*d,*m")
	(match_operand:DI 1 "move_operand" "d,i,m,d,*J*d,*a,*d,*m,*B*C*D,*B*C*D"))]
  "!TARGET_64BIT && !TARGET_MIPS16
   && (register_operand (operands[0], DImode)
       || reg_or_0_operand (operands[1], DImode))"
  { return mips_output_move (operands[0], operands[1]); }
  [(set_attr "type"	"arith,arith,load,store,mthilo,mfhilo,xfer,load,xfer,store")
   (set_attr "mode"	"DI")
   (set_attr "length"   "8,16,*,*,8,8,8,*,8,*")])

(define_insn "*movdi_32bit_mips16"
  [(set (match_operand:DI 0 "nonimmediate_operand" "=d,y,d,d,d,d,m,*d")
	(match_operand:DI 1 "move_operand" "d,d,y,K,N,m,d,*x"))]
  "!TARGET_64BIT && TARGET_MIPS16
   && (register_operand (operands[0], DImode)
       || register_operand (operands[1], DImode))"
  { return mips_output_move (operands[0], operands[1]); }
  [(set_attr "type"	"arith,arith,arith,arith,arith,load,store,mfhilo")
   (set_attr "mode"	"DI")
   (set_attr "length"	"8,8,8,8,12,*,*,8")])

(define_insn "*movdi_64bit"
  [(set (match_operand:DI 0 "nonimmediate_operand" "=d,d,e,d,m,*f,*f,*f,*d,*m,*x,*B*C*D,*B*C*D,*d,*m")
	(match_operand:DI 1 "move_operand" "d,U,T,m,dJ,*f,*d*J,*m,*f,*f,*J*d,*d,*m,*B*C*D,*B*C*D"))]
  "TARGET_64BIT && !TARGET_MIPS16
   && (register_operand (operands[0], DImode)
       || reg_or_0_operand (operands[1], DImode))"
  { return mips_output_move (operands[0], operands[1]); }
  [(set_attr "type"	"arith,const,const,load,store,fmove,xfer,fpload,xfer,fpstore,mthilo,xfer,load,xfer,store")
   (set_attr "mode"	"DI")
   (set_attr "length"	"4,*,*,*,*,4,4,*,4,*,4,8,*,8,*")])

(define_insn "*movdi_64bit_mips16"
  [(set (match_operand:DI 0 "nonimmediate_operand" "=d,y,d,d,d,d,d,m")
	(match_operand:DI 1 "move_operand" "d,d,y,K,N,U,m,d"))]
  "TARGET_64BIT && TARGET_MIPS16
   && (register_operand (operands[0], DImode)
       || register_operand (operands[1], DImode))"
  { return mips_output_move (operands[0], operands[1]); }
  [(set_attr "type"	"arith,arith,arith,arith,arith,const,load,store")
   (set_attr "mode"	"DI")
   (set_attr_alternative "length"
		[(const_int 4)
		 (const_int 4)
		 (const_int 4)
		 (if_then_else (match_operand:VOID 1 "m16_uimm8_1")
			       (const_int 4)
			       (const_int 8))
		 (if_then_else (match_operand:VOID 1 "m16_nuimm8_1")
			       (const_int 8)
			       (const_int 12))
		 (const_string "*")
		 (const_string "*")
		 (const_string "*")])])


;; On the mips16, we can split ld $r,N($r) into an add and a load,
;; when the original load is a 4 byte instruction but the add and the
;; load are 2 2 byte instructions.

(define_split
  [(set (match_operand:DI 0 "register_operand")
	(mem:DI (plus:DI (match_dup 0)
			 (match_operand:DI 1 "const_int_operand"))))]
  "TARGET_64BIT && TARGET_MIPS16 && reload_completed
   && !TARGET_DEBUG_D_MODE
   && REG_P (operands[0])
   && M16_REG_P (REGNO (operands[0]))
   && GET_CODE (operands[1]) == CONST_INT
   && ((INTVAL (operands[1]) < 0
	&& INTVAL (operands[1]) >= -0x10)
       || (INTVAL (operands[1]) >= 32 * 8
	   && INTVAL (operands[1]) <= 31 * 8 + 0x8)
       || (INTVAL (operands[1]) >= 0
	   && INTVAL (operands[1]) < 32 * 8
	   && (INTVAL (operands[1]) & 7) != 0))"
  [(set (match_dup 0) (plus:DI (match_dup 0) (match_dup 1)))
   (set (match_dup 0) (mem:DI (plus:DI (match_dup 0) (match_dup 2))))]
{
  HOST_WIDE_INT val = INTVAL (operands[1]);

  if (val < 0)
    operands[2] = const0_rtx;
  else if (val >= 32 * 8)
    {
      int off = val & 7;

      operands[1] = GEN_INT (0x8 + off);
      operands[2] = GEN_INT (val - off - 0x8);
    }
  else
    {
      int off = val & 7;

      operands[1] = GEN_INT (off);
      operands[2] = GEN_INT (val - off);
    }
})

;; 32-bit Integer moves

;; Unlike most other insns, the move insns can't be split with
;; different predicates, because register spilling and other parts of
;; the compiler, have memoized the insn number already.

(define_expand "movsi"
  [(set (match_operand:SI 0 "")
	(match_operand:SI 1 ""))]
  ""
{
  if (mips_legitimize_move (SImode, operands[0], operands[1]))
    DONE;
})

;; The difference between these two is whether or not ints are allowed
;; in FP registers (off by default, use -mdebugh to enable).

(define_insn "*movsi_internal"
  [(set (match_operand:SI 0 "nonimmediate_operand" "=d,d,e,d,m,*f,*f,*f,*d,*m,*d,*z,*a,*d,*B*C*D,*B*C*D,*d,*m")
	(match_operand:SI 1 "move_operand" "d,U,T,m,dJ,*f,*d*J,*m,*f,*f,*z,*d,*J*d,*A,*d,*m,*B*C*D,*B*C*D"))]
  "!TARGET_MIPS16
   && (register_operand (operands[0], SImode)
       || reg_or_0_operand (operands[1], SImode))"
  { return mips_output_move (operands[0], operands[1]); }
  [(set_attr "type"	"arith,const,const,load,store,fmove,xfer,fpload,xfer,fpstore,xfer,xfer,mthilo,mfhilo,xfer,load,xfer,store")
   (set_attr "mode"	"SI")
   (set_attr "length"	"4,*,*,*,*,4,4,*,4,*,4,4,4,4,4,*,4,*")])

(define_insn "*movsi_mips16"
  [(set (match_operand:SI 0 "nonimmediate_operand" "=d,y,d,d,d,d,d,m")
	(match_operand:SI 1 "move_operand" "d,d,y,K,N,U,m,d"))]
  "TARGET_MIPS16
   && (register_operand (operands[0], SImode)
       || register_operand (operands[1], SImode))"
  { return mips_output_move (operands[0], operands[1]); }
  [(set_attr "type"	"arith,arith,arith,arith,arith,const,load,store")
   (set_attr "mode"	"SI")
   (set_attr_alternative "length"
		[(const_int 4)
		 (const_int 4)
		 (const_int 4)
		 (if_then_else (match_operand:VOID 1 "m16_uimm8_1")
			       (const_int 4)
			       (const_int 8))
		 (if_then_else (match_operand:VOID 1 "m16_nuimm8_1")
			       (const_int 8)
			       (const_int 12))
		 (const_string "*")
		 (const_string "*")
		 (const_string "*")])])

;; On the mips16, we can split lw $r,N($r) into an add and a load,
;; when the original load is a 4 byte instruction but the add and the
;; load are 2 2 byte instructions.

(define_split
  [(set (match_operand:SI 0 "register_operand")
	(mem:SI (plus:SI (match_dup 0)
			 (match_operand:SI 1 "const_int_operand"))))]
  "TARGET_MIPS16 && reload_completed && !TARGET_DEBUG_D_MODE
   && REG_P (operands[0])
   && M16_REG_P (REGNO (operands[0]))
   && GET_CODE (operands[1]) == CONST_INT
   && ((INTVAL (operands[1]) < 0
	&& INTVAL (operands[1]) >= -0x80)
       || (INTVAL (operands[1]) >= 32 * 4
	   && INTVAL (operands[1]) <= 31 * 4 + 0x7c)
       || (INTVAL (operands[1]) >= 0
	   && INTVAL (operands[1]) < 32 * 4
	   && (INTVAL (operands[1]) & 3) != 0))"
  [(set (match_dup 0) (plus:SI (match_dup 0) (match_dup 1)))
   (set (match_dup 0) (mem:SI (plus:SI (match_dup 0) (match_dup 2))))]
{
  HOST_WIDE_INT val = INTVAL (operands[1]);

  if (val < 0)
    operands[2] = const0_rtx;
  else if (val >= 32 * 4)
    {
      int off = val & 3;

      operands[1] = GEN_INT (0x7c + off);
      operands[2] = GEN_INT (val - off - 0x7c);
    }
  else
    {
      int off = val & 3;

      operands[1] = GEN_INT (off);
      operands[2] = GEN_INT (val - off);
    }
})

;; On the mips16, we can split a load of certain constants into a load
;; and an add.  This turns a 4 byte instruction into 2 2 byte
;; instructions.

(define_split
  [(set (match_operand:SI 0 "register_operand")
	(match_operand:SI 1 "const_int_operand"))]
  "TARGET_MIPS16 && reload_completed && !TARGET_DEBUG_D_MODE
   && REG_P (operands[0])
   && M16_REG_P (REGNO (operands[0]))
   && GET_CODE (operands[1]) == CONST_INT
   && INTVAL (operands[1]) >= 0x100
   && INTVAL (operands[1]) <= 0xff + 0x7f"
  [(set (match_dup 0) (match_dup 1))
   (set (match_dup 0) (plus:SI (match_dup 0) (match_dup 2)))]
{
  int val = INTVAL (operands[1]);

  operands[1] = GEN_INT (0xff);
  operands[2] = GEN_INT (val - 0xff);
})

;; This insn handles moving CCmode values.  It's really just a
;; slightly simplified copy of movsi_internal2, with additional cases
;; to move a condition register to a general register and to move
;; between the general registers and the floating point registers.

(define_insn "movcc"
  [(set (match_operand:CC 0 "nonimmediate_operand" "=d,*d,*d,*m,*d,*f,*f,*f,*m")
	(match_operand:CC 1 "general_operand" "z,*d,*m,*d,*f,*d,*f,*m,*f"))]
  "ISA_HAS_8CC && TARGET_HARD_FLOAT"
  { return mips_output_move (operands[0], operands[1]); }
  [(set_attr "type"	"xfer,arith,load,store,xfer,xfer,fmove,fpload,fpstore")
   (set_attr "mode"	"SI")
   (set_attr "length"	"8,4,*,*,4,4,4,*,*")])

;; Reload condition code registers.  reload_incc and reload_outcc
;; both handle moves from arbitrary operands into condition code
;; registers.  reload_incc handles the more common case in which
;; a source operand is constrained to be in a condition-code
;; register, but has not been allocated to one.
;;
;; Sometimes, such as in movcc, we have a CCmode destination whose
;; constraints do not include 'z'.  reload_outcc handles the case
;; when such an operand is allocated to a condition-code register.
;;
;; Note that reloads from a condition code register to some
;; other location can be done using ordinary moves.  Moving
;; into a GPR takes a single movcc, moving elsewhere takes
;; two.  We can leave these cases to the generic reload code.
(define_expand "reload_incc"
  [(set (match_operand:CC 0 "fcc_reload_operand" "=z")
	(match_operand:CC 1 "general_operand" ""))
   (clobber (match_operand:TF 2 "register_operand" "=&f"))]
  "ISA_HAS_8CC && TARGET_HARD_FLOAT"
{
  mips_emit_fcc_reload (operands[0], operands[1], operands[2]);
  DONE;
})

(define_expand "reload_outcc"
  [(set (match_operand:CC 0 "fcc_reload_operand" "=z")
	(match_operand:CC 1 "register_operand" ""))
   (clobber (match_operand:TF 2 "register_operand" "=&f"))]
  "ISA_HAS_8CC && TARGET_HARD_FLOAT"
{
  mips_emit_fcc_reload (operands[0], operands[1], operands[2]);
  DONE;
})

;; MIPS4 supports loading and storing a floating point register from
;; the sum of two general registers.  We use two versions for each of
;; these four instructions: one where the two general registers are
;; SImode, and one where they are DImode.  This is because general
;; registers will be in SImode when they hold 32 bit values, but,
;; since the 32 bit values are always sign extended, the [ls][wd]xc1
;; instructions will still work correctly.

;; ??? Perhaps it would be better to support these instructions by
;; modifying GO_IF_LEGITIMATE_ADDRESS and friends.  However, since
;; these instructions can only be used to load and store floating
;; point registers, that would probably cause trouble in reload.

(define_insn "*<ANYF:loadx>_<P:mode>"
  [(set (match_operand:ANYF 0 "register_operand" "=f")
	(mem:ANYF (plus:P (match_operand:P 1 "register_operand" "d")
			  (match_operand:P 2 "register_operand" "d"))))]
  "ISA_HAS_FP4"
  "<ANYF:loadx>\t%0,%1(%2)"
  [(set_attr "type" "fpidxload")
   (set_attr "mode" "<ANYF:UNITMODE>")])

(define_insn "*<ANYF:storex>_<P:mode>"
  [(set (mem:ANYF (plus:P (match_operand:P 1 "register_operand" "d")
			  (match_operand:P 2 "register_operand" "d")))
	(match_operand:ANYF 0 "register_operand" "f"))]
  "ISA_HAS_FP4"
  "<ANYF:storex>\t%0,%1(%2)"
  [(set_attr "type" "fpidxstore")
   (set_attr "mode" "<ANYF:UNITMODE>")])

;; 16-bit Integer moves

;; Unlike most other insns, the move insns can't be split with
;; different predicates, because register spilling and other parts of
;; the compiler, have memoized the insn number already.
;; Unsigned loads are used because LOAD_EXTEND_OP returns ZERO_EXTEND.

(define_expand "movhi"
  [(set (match_operand:HI 0 "")
	(match_operand:HI 1 ""))]
  ""
{
  if (mips_legitimize_move (HImode, operands[0], operands[1]))
    DONE;
})

(define_insn "*movhi_internal"
  [(set (match_operand:HI 0 "nonimmediate_operand" "=d,d,d,m,*d,*f,*f,*x")
	(match_operand:HI 1 "move_operand"         "d,I,m,dJ,*f,*d,*f,*d"))]
  "!TARGET_MIPS16
   && (register_operand (operands[0], HImode)
       || reg_or_0_operand (operands[1], HImode))"
  "@
    move\t%0,%1
    li\t%0,%1
    lhu\t%0,%1
    sh\t%z1,%0
    mfc1\t%0,%1
    mtc1\t%1,%0
    mov.s\t%0,%1
    mt%0\t%1"
  [(set_attr "type"	"arith,arith,load,store,xfer,xfer,fmove,mthilo")
   (set_attr "mode"	"HI")
   (set_attr "length"	"4,4,*,*,4,4,4,4")])

(define_insn "*movhi_mips16"
  [(set (match_operand:HI 0 "nonimmediate_operand" "=d,y,d,d,d,d,m")
	(match_operand:HI 1 "move_operand"         "d,d,y,K,N,m,d"))]
  "TARGET_MIPS16
   && (register_operand (operands[0], HImode)
       || register_operand (operands[1], HImode))"
  "@
    move\t%0,%1
    move\t%0,%1
    move\t%0,%1
    li\t%0,%1
    #
    lhu\t%0,%1
    sh\t%1,%0"
  [(set_attr "type"	"arith,arith,arith,arith,arith,load,store")
   (set_attr "mode"	"HI")
   (set_attr_alternative "length"
		[(const_int 4)
		 (const_int 4)
		 (const_int 4)
		 (if_then_else (match_operand:VOID 1 "m16_uimm8_1")
			       (const_int 4)
			       (const_int 8))
		 (if_then_else (match_operand:VOID 1 "m16_nuimm8_1")
			       (const_int 8)
			       (const_int 12))
		 (const_string "*")
		 (const_string "*")])])


;; On the mips16, we can split lh $r,N($r) into an add and a load,
;; when the original load is a 4 byte instruction but the add and the
;; load are 2 2 byte instructions.

(define_split
  [(set (match_operand:HI 0 "register_operand")
	(mem:HI (plus:SI (match_dup 0)
			 (match_operand:SI 1 "const_int_operand"))))]
  "TARGET_MIPS16 && reload_completed && !TARGET_DEBUG_D_MODE
   && REG_P (operands[0])
   && M16_REG_P (REGNO (operands[0]))
   && GET_CODE (operands[1]) == CONST_INT
   && ((INTVAL (operands[1]) < 0
	&& INTVAL (operands[1]) >= -0x80)
       || (INTVAL (operands[1]) >= 32 * 2
	   && INTVAL (operands[1]) <= 31 * 2 + 0x7e)
       || (INTVAL (operands[1]) >= 0
	   && INTVAL (operands[1]) < 32 * 2
	   && (INTVAL (operands[1]) & 1) != 0))"
  [(set (match_dup 0) (plus:SI (match_dup 0) (match_dup 1)))
   (set (match_dup 0) (mem:HI (plus:SI (match_dup 0) (match_dup 2))))]
{
  HOST_WIDE_INT val = INTVAL (operands[1]);

  if (val < 0)
    operands[2] = const0_rtx;
  else if (val >= 32 * 2)
    {
      int off = val & 1;

      operands[1] = GEN_INT (0x7e + off);
      operands[2] = GEN_INT (val - off - 0x7e);
    }
  else
    {
      int off = val & 1;

      operands[1] = GEN_INT (off);
      operands[2] = GEN_INT (val - off);
    }
})

;; 8-bit Integer moves

;; Unlike most other insns, the move insns can't be split with
;; different predicates, because register spilling and other parts of
;; the compiler, have memoized the insn number already.
;; Unsigned loads are used because LOAD_EXTEND_OP returns ZERO_EXTEND.

(define_expand "movqi"
  [(set (match_operand:QI 0 "")
	(match_operand:QI 1 ""))]
  ""
{
  if (mips_legitimize_move (QImode, operands[0], operands[1]))
    DONE;
})

(define_insn "*movqi_internal"
  [(set (match_operand:QI 0 "nonimmediate_operand" "=d,d,d,m,*d,*f,*f,*x")
	(match_operand:QI 1 "move_operand"         "d,I,m,dJ,*f,*d,*f,*d"))]
  "!TARGET_MIPS16
   && (register_operand (operands[0], QImode)
       || reg_or_0_operand (operands[1], QImode))"
  "@
    move\t%0,%1
    li\t%0,%1
    lbu\t%0,%1
    sb\t%z1,%0
    mfc1\t%0,%1
    mtc1\t%1,%0
    mov.s\t%0,%1
    mt%0\t%1"
  [(set_attr "type"	"arith,arith,load,store,xfer,xfer,fmove,mthilo")
   (set_attr "mode"	"QI")
   (set_attr "length"	"4,4,*,*,4,4,4,4")])

(define_insn "*movqi_mips16"
  [(set (match_operand:QI 0 "nonimmediate_operand" "=d,y,d,d,d,d,m")
	(match_operand:QI 1 "move_operand"         "d,d,y,K,N,m,d"))]
  "TARGET_MIPS16
   && (register_operand (operands[0], QImode)
       || register_operand (operands[1], QImode))"
  "@
    move\t%0,%1
    move\t%0,%1
    move\t%0,%1
    li\t%0,%1
    #
    lbu\t%0,%1
    sb\t%1,%0"
  [(set_attr "type"	"arith,arith,arith,arith,arith,load,store")
   (set_attr "mode"	"QI")
   (set_attr "length"	"4,4,4,4,8,*,*")])

;; On the mips16, we can split lb $r,N($r) into an add and a load,
;; when the original load is a 4 byte instruction but the add and the
;; load are 2 2 byte instructions.

(define_split
  [(set (match_operand:QI 0 "register_operand")
	(mem:QI (plus:SI (match_dup 0)
			 (match_operand:SI 1 "const_int_operand"))))]
  "TARGET_MIPS16 && reload_completed && !TARGET_DEBUG_D_MODE
   && REG_P (operands[0])
   && M16_REG_P (REGNO (operands[0]))
   && GET_CODE (operands[1]) == CONST_INT
   && ((INTVAL (operands[1]) < 0
	&& INTVAL (operands[1]) >= -0x80)
       || (INTVAL (operands[1]) >= 32
	   && INTVAL (operands[1]) <= 31 + 0x7f))"
  [(set (match_dup 0) (plus:SI (match_dup 0) (match_dup 1)))
   (set (match_dup 0) (mem:QI (plus:SI (match_dup 0) (match_dup 2))))]
{
  HOST_WIDE_INT val = INTVAL (operands[1]);

  if (val < 0)
    operands[2] = const0_rtx;
  else
    {
      operands[1] = GEN_INT (0x7f);
      operands[2] = GEN_INT (val - 0x7f);
    }
})

;; 32-bit floating point moves

(define_expand "movsf"
  [(set (match_operand:SF 0 "")
	(match_operand:SF 1 ""))]
  ""
{
  if (mips_legitimize_move (SFmode, operands[0], operands[1]))
    DONE;
})

(define_insn "*movsf_hardfloat"
  [(set (match_operand:SF 0 "nonimmediate_operand" "=f,f,f,m,m,*f,*d,*d,*d,*m")
	(match_operand:SF 1 "move_operand" "f,G,m,f,G,*d,*f,*G*d,*m,*d"))]
  "TARGET_HARD_FLOAT
   && (register_operand (operands[0], SFmode)
       || reg_or_0_operand (operands[1], SFmode))"
  { return mips_output_move (operands[0], operands[1]); }
  [(set_attr "type"	"fmove,xfer,fpload,fpstore,store,xfer,xfer,arith,load,store")
   (set_attr "mode"	"SF")
   (set_attr "length"	"4,4,*,*,*,4,4,4,*,*")])

(define_insn "*movsf_softfloat"
  [(set (match_operand:SF 0 "nonimmediate_operand" "=d,d,m")
	(match_operand:SF 1 "move_operand" "Gd,m,d"))]
  "TARGET_SOFT_FLOAT && !TARGET_MIPS16
   && (register_operand (operands[0], SFmode)
       || reg_or_0_operand (operands[1], SFmode))"
  { return mips_output_move (operands[0], operands[1]); }
  [(set_attr "type"	"arith,load,store")
   (set_attr "mode"	"SF")
   (set_attr "length"	"4,*,*")])

(define_insn "*movsf_mips16"
  [(set (match_operand:SF 0 "nonimmediate_operand" "=d,y,d,d,m")
	(match_operand:SF 1 "move_operand" "d,d,y,m,d"))]
  "TARGET_MIPS16
   && (register_operand (operands[0], SFmode)
       || register_operand (operands[1], SFmode))"
  { return mips_output_move (operands[0], operands[1]); }
  [(set_attr "type"	"arith,arith,arith,load,store")
   (set_attr "mode"	"SF")
   (set_attr "length"	"4,4,4,*,*")])


;; 64-bit floating point moves

(define_expand "movdf"
  [(set (match_operand:DF 0 "")
	(match_operand:DF 1 ""))]
  ""
{
  if (mips_legitimize_move (DFmode, operands[0], operands[1]))
    DONE;
})

(define_insn "*movdf_hardfloat_64bit"
  [(set (match_operand:DF 0 "nonimmediate_operand" "=f,f,f,m,m,*f,*d,*d,*d,*m")
	(match_operand:DF 1 "move_operand" "f,G,m,f,G,*d,*f,*d*G,*m,*d"))]
  "TARGET_HARD_FLOAT && TARGET_DOUBLE_FLOAT && TARGET_64BIT
   && (register_operand (operands[0], DFmode)
       || reg_or_0_operand (operands[1], DFmode))"
  { return mips_output_move (operands[0], operands[1]); }
  [(set_attr "type"	"fmove,xfer,fpload,fpstore,store,xfer,xfer,arith,load,store")
   (set_attr "mode"	"DF")
   (set_attr "length"	"4,4,*,*,*,4,4,4,*,*")])

(define_insn "*movdf_hardfloat_32bit"
  [(set (match_operand:DF 0 "nonimmediate_operand" "=f,f,f,m,m,*f,*d,*d,*d,*m")
	(match_operand:DF 1 "move_operand" "f,G,m,f,G,*d,*f,*d*G,*m,*d"))]
  "TARGET_HARD_FLOAT && TARGET_DOUBLE_FLOAT && !TARGET_64BIT
   && (register_operand (operands[0], DFmode)
       || reg_or_0_operand (operands[1], DFmode))"
  { return mips_output_move (operands[0], operands[1]); }
  [(set_attr "type"	"fmove,xfer,fpload,fpstore,store,xfer,xfer,arith,load,store")
   (set_attr "mode"	"DF")
   (set_attr "length"	"4,8,*,*,*,8,8,8,*,*")])

(define_insn "*movdf_softfloat"
  [(set (match_operand:DF 0 "nonimmediate_operand" "=d,d,m,d,f,f")
	(match_operand:DF 1 "move_operand" "dG,m,dG,f,d,f"))]
  "(TARGET_SOFT_FLOAT || TARGET_SINGLE_FLOAT) && !TARGET_MIPS16
   && (register_operand (operands[0], DFmode)
       || reg_or_0_operand (operands[1], DFmode))"
  { return mips_output_move (operands[0], operands[1]); }
  [(set_attr "type"	"arith,load,store,xfer,xfer,fmove")
   (set_attr "mode"	"DF")
   (set_attr "length"	"8,*,*,4,4,4")])

(define_insn "*movdf_mips16"
  [(set (match_operand:DF 0 "nonimmediate_operand" "=d,y,d,d,m")
	(match_operand:DF 1 "move_operand" "d,d,y,m,d"))]
  "TARGET_MIPS16
   && (register_operand (operands[0], DFmode)
       || register_operand (operands[1], DFmode))"
  { return mips_output_move (operands[0], operands[1]); }
  [(set_attr "type"	"arith,arith,arith,load,store")
   (set_attr "mode"	"DF")
   (set_attr "length"	"8,8,8,*,*")])

(define_split
  [(set (match_operand:DI 0 "nonimmediate_operand")
	(match_operand:DI 1 "move_operand"))]
  "reload_completed && !TARGET_64BIT
   && mips_split_64bit_move_p (operands[0], operands[1])"
  [(const_int 0)]
{
  mips_split_64bit_move (operands[0], operands[1]);
  DONE;
})

(define_split
  [(set (match_operand:DF 0 "nonimmediate_operand")
	(match_operand:DF 1 "move_operand"))]
  "reload_completed && !TARGET_64BIT
   && mips_split_64bit_move_p (operands[0], operands[1])"
  [(const_int 0)]
{
  mips_split_64bit_move (operands[0], operands[1]);
  DONE;
})

;; When generating mips16 code, split moves of negative constants into
;; a positive "li" followed by a negation.
(define_split
  [(set (match_operand 0 "register_operand")
	(match_operand 1 "const_int_operand"))]
  "TARGET_MIPS16 && reload_completed && INTVAL (operands[1]) < 0"
  [(set (match_dup 2)
	(match_dup 3))
   (set (match_dup 2)
	(neg:SI (match_dup 2)))]
{
  operands[2] = gen_lowpart (SImode, operands[0]);
  operands[3] = GEN_INT (-INTVAL (operands[1]));
})

;; 64-bit paired-single floating point moves

(define_expand "movv2sf"
  [(set (match_operand:V2SF 0)
	(match_operand:V2SF 1))]
  "TARGET_PAIRED_SINGLE_FLOAT"
{
  if (mips_legitimize_move (V2SFmode, operands[0], operands[1]))
    DONE;
})

(define_insn "movv2sf_hardfloat_64bit"
  [(set (match_operand:V2SF 0 "nonimmediate_operand" "=f,f,f,m,m,*f,*d,*d,*d,*m")
	(match_operand:V2SF 1 "move_operand" "f,YG,m,f,YG,*d,*f,*d*YG,*m,*d"))]
  "TARGET_PAIRED_SINGLE_FLOAT
   && TARGET_64BIT
   && (register_operand (operands[0], V2SFmode)
       || reg_or_0_operand (operands[1], V2SFmode))"
  { return mips_output_move (operands[0], operands[1]); }
  [(set_attr "type" "fmove,xfer,fpload,fpstore,store,xfer,xfer,arith,load,store")
   (set_attr "mode" "SF")
   (set_attr "length" "4,4,*,*,*,4,4,4,*,*")])

;; The HI and LO registers are not truly independent.  If we move an mthi
;; instruction before an mflo instruction, it will make the result of the
;; mflo unpredictable.  The same goes for mtlo and mfhi.
;;
;; We cope with this by making the mflo and mfhi patterns use both HI and LO.
;; Operand 1 is the register we want, operand 2 is the other one.
;;
;; When generating VR4120 or VR4130 code, we use macc{,hi} and
;; dmacc{,hi} instead of mfhi and mflo.  This avoids both the normal
;; MIPS III hi/lo hazards and the errata related to -mfix-vr4130.

(define_expand "mfhilo_<mode>"
  [(set (match_operand:GPR 0 "register_operand")
	(unspec:GPR [(match_operand:GPR 1 "register_operand")
		     (match_operand:GPR 2 "register_operand")]
		    UNSPEC_MFHILO))])

(define_insn "*mfhilo_<mode>"
  [(set (match_operand:GPR 0 "register_operand" "=d,d")
	(unspec:GPR [(match_operand:GPR 1 "register_operand" "h,l")
		     (match_operand:GPR 2 "register_operand" "l,h")]
		    UNSPEC_MFHILO))]
  "!ISA_HAS_MACCHI"
  "mf%1\t%0"
  [(set_attr "type" "mfhilo")
   (set_attr "mode" "<MODE>")])

(define_insn "*mfhilo_<mode>_macc"
  [(set (match_operand:GPR 0 "register_operand" "=d,d")
	(unspec:GPR [(match_operand:GPR 1 "register_operand" "h,l")
		     (match_operand:GPR 2 "register_operand" "l,h")]
		    UNSPEC_MFHILO))]
  "ISA_HAS_MACCHI"
{
  if (REGNO (operands[1]) == HI_REGNUM)
    return "<d>macchi\t%0,%.,%.";
  else
    return "<d>macc\t%0,%.,%.";
}
  [(set_attr "type" "mfhilo")
   (set_attr "mode" "<MODE>")])

;; Patterns for loading or storing part of a paired floating point
;; register.  We need them because odd-numbered floating-point registers
;; are not fully independent: see mips_split_64bit_move.

;; Load the low word of operand 0 with operand 1.
(define_insn "load_df_low"
  [(set (match_operand:DF 0 "register_operand" "=f,f")
	(unspec:DF [(match_operand:SI 1 "general_operand" "dJ,m")]
		   UNSPEC_LOAD_DF_LOW))]
  "TARGET_HARD_FLOAT && TARGET_DOUBLE_FLOAT && !TARGET_64BIT"
{
  operands[0] = mips_subword (operands[0], 0);
  return mips_output_move (operands[0], operands[1]);
}
  [(set_attr "type"	"xfer,fpload")
   (set_attr "mode"	"SF")])

;; Load the high word of operand 0 from operand 1, preserving the value
;; in the low word.
(define_insn "load_df_high"
  [(set (match_operand:DF 0 "register_operand" "=f,f")
	(unspec:DF [(match_operand:SI 1 "general_operand" "dJ,m")
		    (match_operand:DF 2 "register_operand" "0,0")]
		   UNSPEC_LOAD_DF_HIGH))]
  "TARGET_HARD_FLOAT && TARGET_DOUBLE_FLOAT && !TARGET_64BIT"
{
  operands[0] = mips_subword (operands[0], 1);
  return mips_output_move (operands[0], operands[1]);
}
  [(set_attr "type"	"xfer,fpload")
   (set_attr "mode"	"SF")])

;; Store the high word of operand 1 in operand 0.  The corresponding
;; low-word move is done in the normal way.
(define_insn "store_df_high"
  [(set (match_operand:SI 0 "nonimmediate_operand" "=d,m")
	(unspec:SI [(match_operand:DF 1 "register_operand" "f,f")]
		   UNSPEC_STORE_DF_HIGH))]
  "TARGET_HARD_FLOAT && TARGET_DOUBLE_FLOAT && !TARGET_64BIT"
{
  operands[1] = mips_subword (operands[1], 1);
  return mips_output_move (operands[0], operands[1]);
}
  [(set_attr "type"	"xfer,fpstore")
   (set_attr "mode"	"SF")])

;; Insn to initialize $gp for n32/n64 abicalls.  Operand 0 is the offset
;; of _gp from the start of this function.  Operand 1 is the incoming
;; function address.
(define_insn_and_split "loadgp"
  [(unspec_volatile [(match_operand 0 "" "")
		     (match_operand 1 "register_operand" "")] UNSPEC_LOADGP)]
  "mips_current_loadgp_style () == LOADGP_NEWABI"
  "#"
  ""
  [(set (match_dup 2) (match_dup 3))
   (set (match_dup 2) (match_dup 4))
   (set (match_dup 2) (match_dup 5))]
{
  operands[2] = pic_offset_table_rtx;
  operands[3] = gen_rtx_HIGH (Pmode, operands[0]);
  operands[4] = gen_rtx_PLUS (Pmode, operands[2], operands[1]);
  operands[5] = gen_rtx_LO_SUM (Pmode, operands[2], operands[0]);
}
  [(set_attr "length" "12")])

;; Likewise, for -mno-shared code.  Operand 0 is the __gnu_local_gp symbol.
(define_insn_and_split "loadgp_noshared"
  [(unspec_volatile [(match_operand 0 "" "")] UNSPEC_LOADGP)]
  "mips_current_loadgp_style () == LOADGP_ABSOLUTE"
  "#"
  ""
  [(const_int 0)]
{
  emit_move_insn (pic_offset_table_rtx, operands[0]);
  DONE;
}
  [(set_attr "length" "8")])

;; The use of gp is hidden when not using explicit relocations.
;; This blockage instruction prevents the gp load from being
;; scheduled after an implicit use of gp.  It also prevents
;; the load from being deleted as dead.
(define_insn "loadgp_blockage"
  [(unspec_volatile [(reg:DI 28)] UNSPEC_BLOCKAGE)]
  ""
  ""
  [(set_attr "type"	"unknown")
   (set_attr "mode"	"none")
   (set_attr "length"	"0")])

;; Emit a .cprestore directive, which normally expands to a single store
;; instruction.  Note that we continue to use .cprestore for explicit reloc
;; code so that jals inside inline asms will work correctly.
(define_insn "cprestore"
  [(unspec_volatile [(match_operand 0 "const_int_operand" "I,i")]
		    UNSPEC_CPRESTORE)]
  ""
{
  if (set_nomacro && which_alternative == 1)
    return ".set\tmacro\;.cprestore\t%0\;.set\tnomacro";
  else
    return ".cprestore\t%0";
}
  [(set_attr "type" "store")
   (set_attr "length" "4,12")])

;; Block moves, see mips.c for more details.
;; Argument 0 is the destination
;; Argument 1 is the source
;; Argument 2 is the length
;; Argument 3 is the alignment

(define_expand "movmemsi"
  [(parallel [(set (match_operand:BLK 0 "general_operand")
		   (match_operand:BLK 1 "general_operand"))
	      (use (match_operand:SI 2 ""))
	      (use (match_operand:SI 3 "const_int_operand"))])]
  "!TARGET_MIPS16 && !TARGET_MEMCPY"
{
  if (mips_expand_block_move (operands[0], operands[1], operands[2]))
    DONE;
  else
    FAIL;
})

;;
;;  ....................
;;
;;	SHIFTS
;;
;;  ....................

(define_expand "<optab><mode>3"
  [(set (match_operand:GPR 0 "register_operand")
	(any_shift:GPR (match_operand:GPR 1 "register_operand")
		       (match_operand:SI 2 "arith_operand")))]
  ""
{
  /* On the mips16, a shift of more than 8 is a four byte instruction,
     so, for a shift between 8 and 16, it is just as fast to do two
     shifts of 8 or less.  If there is a lot of shifting going on, we
     may win in CSE.  Otherwise combine will put the shifts back
     together again.  This can be called by function_arg, so we must
     be careful not to allocate a new register if we've reached the
     reload pass.  */
  if (TARGET_MIPS16
      && optimize
      && GET_CODE (operands[2]) == CONST_INT
      && INTVAL (operands[2]) > 8
      && INTVAL (operands[2]) <= 16
      && !reload_in_progress
      && !reload_completed)
    {
      rtx temp = gen_reg_rtx (<MODE>mode);

      emit_insn (gen_<optab><mode>3 (temp, operands[1], GEN_INT (8)));
      emit_insn (gen_<optab><mode>3 (operands[0], temp,
				     GEN_INT (INTVAL (operands[2]) - 8)));
      DONE;
    }
})

(define_insn "*<optab><mode>3"
  [(set (match_operand:GPR 0 "register_operand" "=d")
	(any_shift:GPR (match_operand:GPR 1 "register_operand" "d")
		       (match_operand:SI 2 "arith_operand" "dI")))]
  "!TARGET_MIPS16"
{
  if (GET_CODE (operands[2]) == CONST_INT)
    operands[2] = GEN_INT (INTVAL (operands[2])
			   & (GET_MODE_BITSIZE (<MODE>mode) - 1));

  return "<d><insn>\t%0,%1,%2";
}
  [(set_attr "type" "shift")
   (set_attr "mode" "<MODE>")])

(define_insn "*<optab>si3_extend"
  [(set (match_operand:DI 0 "register_operand" "=d")
	(sign_extend:DI
	   (any_shift:SI (match_operand:SI 1 "register_operand" "d")
			 (match_operand:SI 2 "arith_operand" "dI"))))]
  "TARGET_64BIT && !TARGET_MIPS16"
{
  if (GET_CODE (operands[2]) == CONST_INT)
    operands[2] = GEN_INT (INTVAL (operands[2]) & 0x1f);

  return "<insn>\t%0,%1,%2";
}
  [(set_attr "type" "shift")
   (set_attr "mode" "SI")])

(define_insn "*<optab>si3_mips16"
  [(set (match_operand:SI 0 "register_operand" "=d,d")
	(any_shift:SI (match_operand:SI 1 "register_operand" "0,d")
		      (match_operand:SI 2 "arith_operand" "d,I")))]
  "TARGET_MIPS16"
{
  if (which_alternative == 0)
    return "<insn>\t%0,%2";

  operands[2] = GEN_INT (INTVAL (operands[2]) & 0x1f);
  return "<insn>\t%0,%1,%2";
}
  [(set_attr "type" "shift")
   (set_attr "mode" "SI")
   (set_attr_alternative "length"
		[(const_int 4)
		 (if_then_else (match_operand 2 "m16_uimm3_b")
			       (const_int 4)
			       (const_int 8))])])

;; We need separate DImode MIPS16 patterns because of the irregularity
;; of right shifts.
(define_insn "*ashldi3_mips16"
  [(set (match_operand:DI 0 "register_operand" "=d,d")
	(ashift:DI (match_operand:DI 1 "register_operand" "0,d")
		   (match_operand:SI 2 "arith_operand" "d,I")))]
  "TARGET_64BIT && TARGET_MIPS16"
{
  if (which_alternative == 0)
    return "dsll\t%0,%2";

  operands[2] = GEN_INT (INTVAL (operands[2]) & 0x3f);
  return "dsll\t%0,%1,%2";
}
  [(set_attr "type" "shift")
   (set_attr "mode" "DI")
   (set_attr_alternative "length"
		[(const_int 4)
		 (if_then_else (match_operand 2 "m16_uimm3_b")
			       (const_int 4)
			       (const_int 8))])])

(define_insn "*ashrdi3_mips16"
  [(set (match_operand:DI 0 "register_operand" "=d,d")
	(ashiftrt:DI (match_operand:DI 1 "register_operand" "0,0")
		     (match_operand:SI 2 "arith_operand" "d,I")))]
  "TARGET_64BIT && TARGET_MIPS16"
{
  if (GET_CODE (operands[2]) == CONST_INT)
    operands[2] = GEN_INT (INTVAL (operands[2]) & 0x3f);

  return "dsra\t%0,%2";
}
  [(set_attr "type" "shift")
   (set_attr "mode" "DI")
   (set_attr_alternative "length"
		[(const_int 4)
		 (if_then_else (match_operand 2 "m16_uimm3_b")
			       (const_int 4)
			       (const_int 8))])])

(define_insn "*lshrdi3_mips16"
  [(set (match_operand:DI 0 "register_operand" "=d,d")
	(lshiftrt:DI (match_operand:DI 1 "register_operand" "0,0")
		     (match_operand:SI 2 "arith_operand" "d,I")))]
  "TARGET_64BIT && TARGET_MIPS16"
{
  if (GET_CODE (operands[2]) == CONST_INT)
    operands[2] = GEN_INT (INTVAL (operands[2]) & 0x3f);

  return "dsrl\t%0,%2";
}
  [(set_attr "type" "shift")
   (set_attr "mode" "DI")
   (set_attr_alternative "length"
		[(const_int 4)
		 (if_then_else (match_operand 2 "m16_uimm3_b")
			       (const_int 4)
			       (const_int 8))])])

;; On the mips16, we can split a 4 byte shift into 2 2 byte shifts.

(define_split
  [(set (match_operand:GPR 0 "register_operand")
	(any_shift:GPR (match_operand:GPR 1 "register_operand")
		       (match_operand:GPR 2 "const_int_operand")))]
  "TARGET_MIPS16 && reload_completed && !TARGET_DEBUG_D_MODE
   && GET_CODE (operands[2]) == CONST_INT
   && INTVAL (operands[2]) > 8
   && INTVAL (operands[2]) <= 16"
  [(set (match_dup 0) (any_shift:GPR (match_dup 1) (const_int 8)))
   (set (match_dup 0) (any_shift:GPR (match_dup 0) (match_dup 2)))]
  { operands[2] = GEN_INT (INTVAL (operands[2]) - 8); })

;; If we load a byte on the mips16 as a bitfield, the resulting
;; sequence of instructions is too complicated for combine, because it
;; involves four instructions: a load, a shift, a constant load into a
;; register, and an and (the key problem here is that the mips16 does
;; not have and immediate).  We recognize a shift of a load in order
;; to make it simple enough for combine to understand.
;;
;; The length here is the worst case: the length of the split version
;; will be more accurate.
(define_insn_and_split ""
  [(set (match_operand:SI 0 "register_operand" "=d")
	(lshiftrt:SI (match_operand:SI 1 "memory_operand" "m")
		     (match_operand:SI 2 "immediate_operand" "I")))]
  "TARGET_MIPS16"
  "#"
  ""
  [(set (match_dup 0) (match_dup 1))
   (set (match_dup 0) (lshiftrt:SI (match_dup 0) (match_dup 2)))]
  ""
  [(set_attr "type"	"load")
   (set_attr "mode"	"SI")
   (set_attr "length"	"16")])

(define_insn "rotr<mode>3"
  [(set (match_operand:GPR 0 "register_operand" "=d")
	(rotatert:GPR (match_operand:GPR 1 "register_operand" "d")
		      (match_operand:SI 2 "arith_operand" "dI")))]
  "ISA_HAS_ROTR_<MODE>"
{
  if (GET_CODE (operands[2]) == CONST_INT)
    gcc_assert (INTVAL (operands[2]) >= 0
		&& INTVAL (operands[2]) < GET_MODE_BITSIZE (<MODE>mode));

  return "<d>ror\t%0,%1,%2";
}
  [(set_attr "type" "shift")
   (set_attr "mode" "<MODE>")])

;;
;;  ....................
;;
;;	COMPARISONS
;;
;;  ....................

;; Flow here is rather complex:
;;
;;  1)	The cmp{si,di,sf,df} routine is called.  It deposits the arguments
;;	into cmp_operands[] but generates no RTL.
;;
;;  2)	The appropriate branch define_expand is called, which then
;;	creates the appropriate RTL for the comparison and branch.
;;	Different CC modes are used, based on what type of branch is
;;	done, so that we can constrain things appropriately.  There
;;	are assumptions in the rest of GCC that break if we fold the
;;	operands into the branches for integer operations, and use cc0
;;	for floating point, so we use the fp status register instead.
;;	If needed, an appropriate temporary is created to hold the
;;	of the integer compare.

(define_expand "cmp<mode>"
  [(set (cc0)
	(compare:CC (match_operand:GPR 0 "register_operand")
		    (match_operand:GPR 1 "nonmemory_operand")))]
  ""
{
  cmp_operands[0] = operands[0];
  cmp_operands[1] = operands[1];
  DONE;
})

(define_expand "cmp<mode>"
  [(set (cc0)
	(compare:CC (match_operand:SCALARF 0 "register_operand")
		    (match_operand:SCALARF 1 "register_operand")))]
  ""
{
  cmp_operands[0] = operands[0];
  cmp_operands[1] = operands[1];
  DONE;
})

;;
;;  ....................
;;
;;	CONDITIONAL BRANCHES
;;
;;  ....................

;; Conditional branches on floating-point equality tests.

(define_insn "*branch_fp"
  [(set (pc)
        (if_then_else
         (match_operator 0 "equality_operator"
                         [(match_operand:CC 2 "register_operand" "z")
			  (const_int 0)])
         (label_ref (match_operand 1 "" ""))
         (pc)))]
  "TARGET_HARD_FLOAT"
{
  return mips_output_conditional_branch (insn, operands,
					 MIPS_BRANCH ("b%F0", "%Z2%1"),
					 MIPS_BRANCH ("b%W0", "%Z2%1"));
}
  [(set_attr "type" "branch")
   (set_attr "mode" "none")])

(define_insn "*branch_fp_inverted"
  [(set (pc)
        (if_then_else
         (match_operator 0 "equality_operator"
                         [(match_operand:CC 2 "register_operand" "z")
			  (const_int 0)])
         (pc)
         (label_ref (match_operand 1 "" ""))))]
  "TARGET_HARD_FLOAT"
{
  return mips_output_conditional_branch (insn, operands,
					 MIPS_BRANCH ("b%W0", "%Z2%1"),
					 MIPS_BRANCH ("b%F0", "%Z2%1"));
}
  [(set_attr "type" "branch")
   (set_attr "mode" "none")])

;; Conditional branches on ordered comparisons with zero.

(define_insn "*branch_order<mode>"
  [(set (pc)
	(if_then_else
	 (match_operator 0 "order_operator"
			 [(match_operand:GPR 2 "register_operand" "d")
			  (const_int 0)])
	 (label_ref (match_operand 1 "" ""))
	 (pc)))]
  "!TARGET_MIPS16"
  { return mips_output_order_conditional_branch (insn, operands, false); }
  [(set_attr "type" "branch")
   (set_attr "mode" "none")])

(define_insn "*branch_order<mode>_inverted"
  [(set (pc)
	(if_then_else
	 (match_operator 0 "order_operator"
			 [(match_operand:GPR 2 "register_operand" "d")
			  (const_int 0)])
	 (pc)
	 (label_ref (match_operand 1 "" ""))))]
  "!TARGET_MIPS16"
  { return mips_output_order_conditional_branch (insn, operands, true); }
  [(set_attr "type" "branch")
   (set_attr "mode" "none")])

;; Conditional branch on equality comparison.

(define_insn "*branch_equality<mode>"
  [(set (pc)
	(if_then_else
	 (match_operator 0 "equality_operator"
			 [(match_operand:GPR 2 "register_operand" "d")
			  (match_operand:GPR 3 "reg_or_0_operand" "dJ")])
	 (label_ref (match_operand 1 "" ""))
	 (pc)))]
  "!TARGET_MIPS16"
{
  return mips_output_conditional_branch (insn, operands,
					 MIPS_BRANCH ("b%C0", "%2,%z3,%1"),
					 MIPS_BRANCH ("b%N0", "%2,%z3,%1"));
}
  [(set_attr "type" "branch")
   (set_attr "mode" "none")])

(define_insn "*branch_equality<mode>_inverted"
  [(set (pc)
	(if_then_else
	 (match_operator 0 "equality_operator"
			 [(match_operand:GPR 2 "register_operand" "d")
			  (match_operand:GPR 3 "reg_or_0_operand" "dJ")])
	 (pc)
	 (label_ref (match_operand 1 "" ""))))]
  "!TARGET_MIPS16"
{
  return mips_output_conditional_branch (insn, operands,
					 MIPS_BRANCH ("b%N0", "%2,%z3,%1"),
					 MIPS_BRANCH ("b%C0", "%2,%z3,%1"));
}
  [(set_attr "type" "branch")
   (set_attr "mode" "none")])

;; MIPS16 branches

(define_insn "*branch_equality<mode>_mips16"
  [(set (pc)
	(if_then_else
	 (match_operator 0 "equality_operator"
			 [(match_operand:GPR 1 "register_operand" "d,t")
			  (const_int 0)])
	 (match_operand 2 "pc_or_label_operand" "")
	 (match_operand 3 "pc_or_label_operand" "")))]
  "TARGET_MIPS16"
{
  if (operands[2] != pc_rtx)
    {
      if (which_alternative == 0)
	return "b%C0z\t%1,%2";
      else
	return "bt%C0z\t%2";
    }
  else
    {
      if (which_alternative == 0)
	return "b%N0z\t%1,%3";
      else
	return "bt%N0z\t%3";
    }
}
  [(set_attr "type" "branch")
   (set_attr "mode" "none")
   (set_attr "length" "8")])

(define_expand "b<code>"
  [(set (pc)
	(if_then_else (any_cond:CC (cc0)
				   (const_int 0))
		      (label_ref (match_operand 0 ""))
		      (pc)))]
  ""
{
  gen_conditional_branch (operands, <CODE>);
  DONE;
})

;; Used to implement built-in functions.
(define_expand "condjump"
  [(set (pc)
	(if_then_else (match_operand 0)
		      (label_ref (match_operand 1))
		      (pc)))])

;;
;;  ....................
;;
;;	SETTING A REGISTER FROM A COMPARISON
;;
;;  ....................

(define_expand "seq"
  [(set (match_operand:SI 0 "register_operand")
	(eq:SI (match_dup 1)
	       (match_dup 2)))]
  ""
  { if (mips_emit_scc (EQ, operands[0])) DONE; else FAIL; })

(define_insn "*seq_<mode>"
  [(set (match_operand:GPR 0 "register_operand" "=d")
	(eq:GPR (match_operand:GPR 1 "register_operand" "d")
		(const_int 0)))]
  "!TARGET_MIPS16"
  "sltu\t%0,%1,1"
  [(set_attr "type" "slt")
   (set_attr "mode" "<MODE>")])

(define_insn "*seq_<mode>_mips16"
  [(set (match_operand:GPR 0 "register_operand" "=t")
	(eq:GPR (match_operand:GPR 1 "register_operand" "d")
		(const_int 0)))]
  "TARGET_MIPS16"
  "sltu\t%1,1"
  [(set_attr "type" "slt")
   (set_attr "mode" "<MODE>")])

;; "sne" uses sltu instructions in which the first operand is $0.
;; This isn't possible in mips16 code.

(define_expand "sne"
  [(set (match_operand:SI 0 "register_operand")
	(ne:SI (match_dup 1)
	       (match_dup 2)))]
  "!TARGET_MIPS16"
  { if (mips_emit_scc (NE, operands[0])) DONE; else FAIL; })

(define_insn "*sne_<mode>"
  [(set (match_operand:GPR 0 "register_operand" "=d")
	(ne:GPR (match_operand:GPR 1 "register_operand" "d")
		(const_int 0)))]
  "!TARGET_MIPS16"
  "sltu\t%0,%.,%1"
  [(set_attr "type" "slt")
   (set_attr "mode" "<MODE>")])

(define_expand "sgt"
  [(set (match_operand:SI 0 "register_operand")
	(gt:SI (match_dup 1)
	       (match_dup 2)))]
  ""
  { if (mips_emit_scc (GT, operands[0])) DONE; else FAIL; })

(define_insn "*sgt_<mode>"
  [(set (match_operand:GPR 0 "register_operand" "=d")
	(gt:GPR (match_operand:GPR 1 "register_operand" "d")
		(match_operand:GPR 2 "reg_or_0_operand" "dJ")))]
  "!TARGET_MIPS16"
  "slt\t%0,%z2,%1"
  [(set_attr "type" "slt")
   (set_attr "mode" "<MODE>")])

(define_insn "*sgt_<mode>_mips16"
  [(set (match_operand:GPR 0 "register_operand" "=t")
	(gt:GPR (match_operand:GPR 1 "register_operand" "d")
		(match_operand:GPR 2 "register_operand" "d")))]
  "TARGET_MIPS16"
  "slt\t%2,%1"
  [(set_attr "type" "slt")
   (set_attr "mode" "<MODE>")])

(define_expand "sge"
  [(set (match_operand:SI 0 "register_operand")
	(ge:SI (match_dup 1)
	       (match_dup 2)))]
  ""
  { if (mips_emit_scc (GE, operands[0])) DONE; else FAIL; })

(define_insn "*sge_<mode>"
  [(set (match_operand:GPR 0 "register_operand" "=d")
	(ge:GPR (match_operand:GPR 1 "register_operand" "d")
		(const_int 1)))]
  "!TARGET_MIPS16"
  "slt\t%0,%.,%1"
  [(set_attr "type" "slt")
   (set_attr "mode" "<MODE>")])

(define_expand "slt"
  [(set (match_operand:SI 0 "register_operand")
	(lt:SI (match_dup 1)
	       (match_dup 2)))]
  ""
  { if (mips_emit_scc (LT, operands[0])) DONE; else FAIL; })

(define_insn "*slt_<mode>"
  [(set (match_operand:GPR 0 "register_operand" "=d")
	(lt:GPR (match_operand:GPR 1 "register_operand" "d")
		(match_operand:GPR 2 "arith_operand" "dI")))]
  "!TARGET_MIPS16"
  "slt\t%0,%1,%2"
  [(set_attr "type" "slt")
   (set_attr "mode" "<MODE>")])

(define_insn "*slt_<mode>_mips16"
  [(set (match_operand:GPR 0 "register_operand" "=t,t")
	(lt:GPR (match_operand:GPR 1 "register_operand" "d,d")
		(match_operand:GPR 2 "arith_operand" "d,I")))]
  "TARGET_MIPS16"
  "slt\t%1,%2"
  [(set_attr "type" "slt")
   (set_attr "mode" "<MODE>")
   (set_attr_alternative "length"
		[(const_int 4)
		 (if_then_else (match_operand 2 "m16_uimm8_1")
			       (const_int 4)
			       (const_int 8))])])

(define_expand "sle"
  [(set (match_operand:SI 0 "register_operand")
	(le:SI (match_dup 1)
	       (match_dup 2)))]
  ""
  { if (mips_emit_scc (LE, operands[0])) DONE; else FAIL; })

(define_insn "*sle_<mode>"
  [(set (match_operand:GPR 0 "register_operand" "=d")
	(le:GPR (match_operand:GPR 1 "register_operand" "d")
		(match_operand:GPR 2 "sle_operand" "")))]
  "!TARGET_MIPS16"
{
  operands[2] = GEN_INT (INTVAL (operands[2]) + 1);
  return "slt\t%0,%1,%2";
}
  [(set_attr "type" "slt")
   (set_attr "mode" "<MODE>")])

(define_insn "*sle_<mode>_mips16"
  [(set (match_operand:GPR 0 "register_operand" "=t")
	(le:GPR (match_operand:GPR 1 "register_operand" "d")
		(match_operand:GPR 2 "sle_operand" "")))]
  "TARGET_MIPS16"
{
  operands[2] = GEN_INT (INTVAL (operands[2]) + 1);
  return "slt\t%1,%2";
}
  [(set_attr "type" "slt")
   (set_attr "mode" "<MODE>")
   (set (attr "length") (if_then_else (match_operand 2 "m16_uimm8_m1_1")
				      (const_int 4)
				      (const_int 8)))])

(define_expand "sgtu"
  [(set (match_operand:SI 0 "register_operand")
	(gtu:SI (match_dup 1)
		(match_dup 2)))]
  ""
  { if (mips_emit_scc (GTU, operands[0])) DONE; else FAIL; })

(define_insn "*sgtu_<mode>"
  [(set (match_operand:GPR 0 "register_operand" "=d")
	(gtu:GPR (match_operand:GPR 1 "register_operand" "d")
		 (match_operand:GPR 2 "reg_or_0_operand" "dJ")))]
  "!TARGET_MIPS16"
  "sltu\t%0,%z2,%1"
  [(set_attr "type" "slt")
   (set_attr "mode" "<MODE>")])

(define_insn "*sgtu_<mode>_mips16"
  [(set (match_operand:GPR 0 "register_operand" "=t")
	(gtu:GPR (match_operand:GPR 1 "register_operand" "d")
		 (match_operand:GPR 2 "register_operand" "d")))]
  "TARGET_MIPS16"
  "sltu\t%2,%1"
  [(set_attr "type" "slt")
   (set_attr "mode" "<MODE>")])

(define_expand "sgeu"
  [(set (match_operand:SI 0 "register_operand")
        (geu:SI (match_dup 1)
                (match_dup 2)))]
  ""
  { if (mips_emit_scc (GEU, operands[0])) DONE; else FAIL; })

(define_insn "*sge_<mode>"
  [(set (match_operand:GPR 0 "register_operand" "=d")
	(geu:GPR (match_operand:GPR 1 "register_operand" "d")
	         (const_int 1)))]
  "!TARGET_MIPS16"
  "sltu\t%0,%.,%1"
  [(set_attr "type" "slt")
   (set_attr "mode" "<MODE>")])

(define_expand "sltu"
  [(set (match_operand:SI 0 "register_operand")
	(ltu:SI (match_dup 1)
		(match_dup 2)))]
  ""
  { if (mips_emit_scc (LTU, operands[0])) DONE; else FAIL; })

(define_insn "*sltu_<mode>"
  [(set (match_operand:GPR 0 "register_operand" "=d")
	(ltu:GPR (match_operand:GPR 1 "register_operand" "d")
		 (match_operand:GPR 2 "arith_operand" "dI")))]
  "!TARGET_MIPS16"
  "sltu\t%0,%1,%2"
  [(set_attr "type" "slt")
   (set_attr "mode" "<MODE>")])

(define_insn "*sltu_<mode>_mips16"
  [(set (match_operand:GPR 0 "register_operand" "=t,t")
	(ltu:GPR (match_operand:GPR 1 "register_operand" "d,d")
		 (match_operand:GPR 2 "arith_operand" "d,I")))]
  "TARGET_MIPS16"
  "sltu\t%1,%2"
  [(set_attr "type" "slt")
   (set_attr "mode" "<MODE>")
   (set_attr_alternative "length"
		[(const_int 4)
		 (if_then_else (match_operand 2 "m16_uimm8_1")
			       (const_int 4)
			       (const_int 8))])])

(define_expand "sleu"
  [(set (match_operand:SI 0 "register_operand")
	(leu:SI (match_dup 1)
		(match_dup 2)))]
  ""
  { if (mips_emit_scc (LEU, operands[0])) DONE; else FAIL; })

(define_insn "*sleu_<mode>"
  [(set (match_operand:GPR 0 "register_operand" "=d")
	(leu:GPR (match_operand:GPR 1 "register_operand" "d")
	         (match_operand:GPR 2 "sleu_operand" "")))]
  "!TARGET_MIPS16"
{
  operands[2] = GEN_INT (INTVAL (operands[2]) + 1);
  return "sltu\t%0,%1,%2";
}
  [(set_attr "type" "slt")
   (set_attr "mode" "<MODE>")])

(define_insn "*sleu_<mode>_mips16"
  [(set (match_operand:GPR 0 "register_operand" "=t")
	(leu:GPR (match_operand:GPR 1 "register_operand" "d")
	         (match_operand:GPR 2 "sleu_operand" "")))]
  "TARGET_MIPS16"
{
  operands[2] = GEN_INT (INTVAL (operands[2]) + 1);
  return "sltu\t%1,%2";
}
  [(set_attr "type" "slt")
   (set_attr "mode" "<MODE>")
   (set (attr "length") (if_then_else (match_operand 2 "m16_uimm8_m1_1")
				      (const_int 4)
				      (const_int 8)))])

;;
;;  ....................
;;
;;	FLOATING POINT COMPARISONS
;;
;;  ....................

(define_insn "s<code>_<mode>"
  [(set (match_operand:CC 0 "register_operand" "=z")
	(fcond:CC (match_operand:SCALARF 1 "register_operand" "f")
		  (match_operand:SCALARF 2 "register_operand" "f")))]
  ""
  "c.<fcond>.<fmt>\t%Z0%1,%2"
  [(set_attr "type" "fcmp")
   (set_attr "mode" "FPSW")])

(define_insn "s<code>_<mode>"
  [(set (match_operand:CC 0 "register_operand" "=z")
	(swapped_fcond:CC (match_operand:SCALARF 1 "register_operand" "f")
		          (match_operand:SCALARF 2 "register_operand" "f")))]
  ""
  "c.<swapped_fcond>.<fmt>\t%Z0%2,%1"
  [(set_attr "type" "fcmp")
   (set_attr "mode" "FPSW")])

;;
;;  ....................
;;
;;	UNCONDITIONAL BRANCHES
;;
;;  ....................

;; Unconditional branches.

(define_insn "jump"
  [(set (pc)
	(label_ref (match_operand 0 "" "")))]
  "!TARGET_MIPS16"
{
  if (flag_pic)
    {
      if (get_attr_length (insn) <= 8)
	return "%*b\t%l0%/";
      else
	{
	  output_asm_insn (mips_output_load_label (), operands);
	  return "%*jr\t%@%/%]";
	}
    }
  else
    return "%*j\t%l0%/";
}
  [(set_attr "type"	"jump")
   (set_attr "mode"	"none")
   (set (attr "length")
	;; We can't use `j' when emitting PIC.  Emit a branch if it's
	;; in range, otherwise load the address of the branch target into
	;; $at and then jump to it.
	(if_then_else
	 (ior (eq (symbol_ref "flag_pic") (const_int 0))
	      (lt (abs (minus (match_dup 0)
			      (plus (pc) (const_int 4))))
		  (const_int 131072)))
	 (const_int 4) (const_int 16)))])

;; We need a different insn for the mips16, because a mips16 branch
;; does not have a delay slot.

(define_insn ""
  [(set (pc)
	(label_ref (match_operand 0 "" "")))]
  "TARGET_MIPS16"
  "b\t%l0"
  [(set_attr "type"	"branch")
   (set_attr "mode"	"none")
   (set_attr "length"	"8")])

(define_expand "indirect_jump"
  [(set (pc) (match_operand 0 "register_operand"))]
  ""
{
  operands[0] = force_reg (Pmode, operands[0]);
  if (Pmode == SImode)
    emit_jump_insn (gen_indirect_jumpsi (operands[0]));
  else
    emit_jump_insn (gen_indirect_jumpdi (operands[0]));
  DONE;
})

(define_insn "indirect_jump<mode>"
  [(set (pc) (match_operand:P 0 "register_operand" "d"))]
  ""
  "%*j\t%0%/"
  [(set_attr "type" "jump")
   (set_attr "mode" "none")])

(define_expand "tablejump"
  [(set (pc)
	(match_operand 0 "register_operand"))
   (use (label_ref (match_operand 1 "")))]
  ""
{
  if (TARGET_MIPS16)
    operands[0] = expand_binop (Pmode, add_optab,
				convert_to_mode (Pmode, operands[0], false),
				gen_rtx_LABEL_REF (Pmode, operands[1]),
				0, 0, OPTAB_WIDEN);
  else if (TARGET_GPWORD)
    operands[0] = expand_binop (Pmode, add_optab, operands[0],
				pic_offset_table_rtx, 0, 0, OPTAB_WIDEN);

  if (Pmode == SImode)
    emit_jump_insn (gen_tablejumpsi (operands[0], operands[1]));
  else
    emit_jump_insn (gen_tablejumpdi (operands[0], operands[1]));
  DONE;
})

(define_insn "tablejump<mode>"
  [(set (pc)
	(match_operand:P 0 "register_operand" "d"))
   (use (label_ref (match_operand 1 "" "")))]
  ""
  "%*j\t%0%/"
  [(set_attr "type" "jump")
   (set_attr "mode" "none")])

;; For TARGET_ABICALLS, we save the gp in the jmp_buf as well.
;; While it is possible to either pull it off the stack (in the
;; o32 case) or recalculate it given t9 and our target label,
;; it takes 3 or 4 insns to do so.

(define_expand "builtin_setjmp_setup"
  [(use (match_operand 0 "register_operand"))]
  "TARGET_ABICALLS"
{
  rtx addr;

  addr = plus_constant (operands[0], GET_MODE_SIZE (Pmode) * 3);
  emit_move_insn (gen_rtx_MEM (Pmode, addr), pic_offset_table_rtx);
  DONE;
})

;; Restore the gp that we saved above.  Despite the earlier comment, it seems
;; that older code did recalculate the gp from $25.  Continue to jump through
;; $25 for compatibility (we lose nothing by doing so).

(define_expand "builtin_longjmp"
  [(use (match_operand 0 "register_operand"))]
  "TARGET_ABICALLS"
{
  /* The elements of the buffer are, in order:  */
  int W = GET_MODE_SIZE (Pmode);
  rtx fp = gen_rtx_MEM (Pmode, operands[0]);
  rtx lab = gen_rtx_MEM (Pmode, plus_constant (operands[0], 1*W));
  rtx stack = gen_rtx_MEM (Pmode, plus_constant (operands[0], 2*W));
  rtx gpv = gen_rtx_MEM (Pmode, plus_constant (operands[0], 3*W));
  rtx pv = gen_rtx_REG (Pmode, PIC_FUNCTION_ADDR_REGNUM);
  /* Use gen_raw_REG to avoid being given pic_offset_table_rtx.
     The target is bound to be using $28 as the global pointer
     but the current function might not be.  */
  rtx gp = gen_raw_REG (Pmode, GLOBAL_POINTER_REGNUM);

  /* This bit is similar to expand_builtin_longjmp except that it
     restores $gp as well.  */
  emit_move_insn (hard_frame_pointer_rtx, fp);
  emit_move_insn (pv, lab);
  emit_stack_restore (SAVE_NONLOCAL, stack, NULL_RTX);
  emit_move_insn (gp, gpv);
  emit_insn (gen_rtx_USE (VOIDmode, hard_frame_pointer_rtx));
  emit_insn (gen_rtx_USE (VOIDmode, stack_pointer_rtx));
  emit_insn (gen_rtx_USE (VOIDmode, gp));
  emit_indirect_jump (pv);
  DONE;
})

;;
;;  ....................
;;
;;	Function prologue/epilogue
;;
;;  ....................
;;

(define_expand "prologue"
  [(const_int 1)]
  ""
{
  mips_expand_prologue ();
  DONE;
})

;; Block any insns from being moved before this point, since the
;; profiling call to mcount can use various registers that aren't
;; saved or used to pass arguments.

(define_insn "blockage"
  [(unspec_volatile [(const_int 0)] UNSPEC_BLOCKAGE)]
  ""
  ""
  [(set_attr "type"	"unknown")
   (set_attr "mode"	"none")
   (set_attr "length"	"0")])

(define_expand "epilogue"
  [(const_int 2)]
  ""
{
  mips_expand_epilogue (false);
  DONE;
})

(define_expand "sibcall_epilogue"
  [(const_int 2)]
  ""
{
  mips_expand_epilogue (true);
  DONE;
})

;; Trivial return.  Make it look like a normal return insn as that
;; allows jump optimizations to work better.

(define_insn "return"
  [(return)]
  "mips_can_use_return_insn ()"
  "%*j\t$31%/"
  [(set_attr "type"	"jump")
   (set_attr "mode"	"none")])

;; Normal return.

(define_insn "return_internal"
  [(return)
   (use (match_operand 0 "pmode_register_operand" ""))]
  ""
  "%*j\t%0%/"
  [(set_attr "type"	"jump")
   (set_attr "mode"	"none")])

;; This is used in compiling the unwind routines.
(define_expand "eh_return"
  [(use (match_operand 0 "general_operand"))]
  ""
{
  enum machine_mode gpr_mode = TARGET_64BIT ? DImode : SImode;

  if (GET_MODE (operands[0]) != gpr_mode)
    operands[0] = convert_to_mode (gpr_mode, operands[0], 0);
  if (TARGET_64BIT)
    emit_insn (gen_eh_set_lr_di (operands[0]));
  else
    emit_insn (gen_eh_set_lr_si (operands[0]));

  DONE;
})

;; Clobber the return address on the stack.  We can't expand this
;; until we know where it will be put in the stack frame.

(define_insn "eh_set_lr_si"
  [(unspec [(match_operand:SI 0 "register_operand" "d")] UNSPEC_EH_RETURN)
   (clobber (match_scratch:SI 1 "=&d"))]
  "! TARGET_64BIT"
  "#")

(define_insn "eh_set_lr_di"
  [(unspec [(match_operand:DI 0 "register_operand" "d")] UNSPEC_EH_RETURN)
   (clobber (match_scratch:DI 1 "=&d"))]
  "TARGET_64BIT"
  "#")

(define_split
  [(unspec [(match_operand 0 "register_operand")] UNSPEC_EH_RETURN)
   (clobber (match_scratch 1))]
  "reload_completed && !TARGET_DEBUG_D_MODE"
  [(const_int 0)]
{
  mips_set_return_address (operands[0], operands[1]);
  DONE;
})

(define_insn_and_split "exception_receiver"
  [(set (reg:SI 28)
	(unspec_volatile:SI [(const_int 0)] UNSPEC_EH_RECEIVER))]
  "TARGET_ABICALLS && TARGET_OLDABI"
  "#"
  "&& reload_completed"
  [(const_int 0)]
{
  mips_restore_gp ();
  DONE;
}
  [(set_attr "type"   "load")
   (set_attr "length" "12")])

;;
;;  ....................
;;
;;	FUNCTION CALLS
;;
;;  ....................

;; Instructions to load a call address from the GOT.  The address might
;; point to a function or to a lazy binding stub.  In the latter case,
;; the stub will use the dynamic linker to resolve the function, which
;; in turn will change the GOT entry to point to the function's real
;; address.
;;
;; This means that every call, even pure and constant ones, can
;; potentially modify the GOT entry.  And once a stub has been called,
;; we must not call it again.
;;
;; We represent this restriction using an imaginary fixed register that
;; acts like a GOT version number.  By making the register call-clobbered,
;; we tell the target-independent code that the address could be changed
;; by any call insn.
(define_insn "load_call<mode>"
  [(set (match_operand:P 0 "register_operand" "=c")
	(unspec:P [(match_operand:P 1 "register_operand" "r")
		   (match_operand:P 2 "immediate_operand" "")
		   (reg:P FAKE_CALL_REGNO)]
		  UNSPEC_LOAD_CALL))]
  "TARGET_ABICALLS"
  "<load>\t%0,%R2(%1)"
  [(set_attr "type" "load")
   (set_attr "mode" "<MODE>")
   (set_attr "length" "4")])

;; Sibling calls.  All these patterns use jump instructions.

;; If TARGET_SIBCALLS, call_insn_operand will only accept constant
;; addresses if a direct jump is acceptable.  Since the 'S' constraint
;; is defined in terms of call_insn_operand, the same is true of the
;; constraints.

;; When we use an indirect jump, we need a register that will be
;; preserved by the epilogue.  Since TARGET_ABICALLS forces us to
;; use $25 for this purpose -- and $25 is never clobbered by the
;; epilogue -- we might as well use it for !TARGET_ABICALLS as well.

(define_expand "sibcall"
  [(parallel [(call (match_operand 0 "")
		    (match_operand 1 ""))
	      (use (match_operand 2 ""))	;; next_arg_reg
	      (use (match_operand 3 ""))])]	;; struct_value_size_rtx
  "TARGET_SIBCALLS"
{
  mips_expand_call (0, XEXP (operands[0], 0), operands[1], operands[2], true);
  DONE;
})

(define_insn "sibcall_internal"
  [(call (mem:SI (match_operand 0 "call_insn_operand" "j,S"))
	 (match_operand 1 "" ""))]
  "TARGET_SIBCALLS && SIBLING_CALL_P (insn)"
  { return MIPS_CALL ("j", operands, 0); }
  [(set_attr "type" "call")])

(define_expand "sibcall_value"
  [(parallel [(set (match_operand 0 "")
		   (call (match_operand 1 "")
			 (match_operand 2 "")))
	      (use (match_operand 3 ""))])]		;; next_arg_reg
  "TARGET_SIBCALLS"
{
  mips_expand_call (operands[0], XEXP (operands[1], 0),
		    operands[2], operands[3], true);
  DONE;
})

(define_insn "sibcall_value_internal"
  [(set (match_operand 0 "register_operand" "=df,df")
        (call (mem:SI (match_operand 1 "call_insn_operand" "j,S"))
              (match_operand 2 "" "")))]
  "TARGET_SIBCALLS && SIBLING_CALL_P (insn)"
  { return MIPS_CALL ("j", operands, 1); }
  [(set_attr "type" "call")])

(define_insn "sibcall_value_multiple_internal"
  [(set (match_operand 0 "register_operand" "=df,df")
        (call (mem:SI (match_operand 1 "call_insn_operand" "j,S"))
              (match_operand 2 "" "")))
   (set (match_operand 3 "register_operand" "=df,df")
	(call (mem:SI (match_dup 1))
	      (match_dup 2)))]
  "TARGET_SIBCALLS && SIBLING_CALL_P (insn)"
  { return MIPS_CALL ("j", operands, 1); }
  [(set_attr "type" "call")])

(define_expand "call"
  [(parallel [(call (match_operand 0 "")
		    (match_operand 1 ""))
	      (use (match_operand 2 ""))	;; next_arg_reg
	      (use (match_operand 3 ""))])]	;; struct_value_size_rtx
  ""
{
  mips_expand_call (0, XEXP (operands[0], 0), operands[1], operands[2], false);
  DONE;
})

;; This instruction directly corresponds to an assembly-language "jal".
;; There are four cases:
;;
;;    - -mno-abicalls:
;;	  Both symbolic and register destinations are OK.  The pattern
;;	  always expands to a single mips instruction.
;;
;;    - -mabicalls/-mno-explicit-relocs:
;;	  Again, both symbolic and register destinations are OK.
;;	  The call is treated as a multi-instruction black box.
;;
;;    - -mabicalls/-mexplicit-relocs with n32 or n64:
;;	  Only "jal $25" is allowed.  This expands to a single "jalr $25"
;;	  instruction.
;;
;;    - -mabicalls/-mexplicit-relocs with o32 or o64:
;;	  Only "jal $25" is allowed.  The call is actually two instructions:
;;	  "jalr $25" followed by an insn to reload $gp.
;;
;; In the last case, we can generate the individual instructions with
;; a define_split.  There are several things to be wary of:
;;
;;   - We can't expose the load of $gp before reload.  If we did,
;;     it might get removed as dead, but reload can introduce new
;;     uses of $gp by rematerializing constants.
;;
;;   - We shouldn't restore $gp after calls that never return.
;;     It isn't valid to insert instructions between a noreturn
;;     call and the following barrier.
;;
;;   - The splitter deliberately changes the liveness of $gp.  The unsplit
;;     instruction preserves $gp and so have no effect on its liveness.
;;     But once we generate the separate insns, it becomes obvious that
;;     $gp is not live on entry to the call.
;;
;; ??? The operands[2] = insn check is a hack to make the original insn
;; available to the splitter.
(define_insn_and_split "call_internal"
  [(call (mem:SI (match_operand 0 "call_insn_operand" "c,S"))
	 (match_operand 1 "" ""))
   (clobber (reg:SI 31))]
  ""
  { return TARGET_SPLIT_CALLS ? "#" : MIPS_CALL ("jal", operands, 0); }
  "reload_completed && TARGET_SPLIT_CALLS && (operands[2] = insn)"
  [(const_int 0)]
{
  emit_call_insn (gen_call_split (operands[0], operands[1]));
  if (!find_reg_note (operands[2], REG_NORETURN, 0))
    mips_restore_gp ();
  DONE;
}
  [(set_attr "jal" "indirect,direct")
   (set_attr "extended_mips16" "no,yes")])

(define_insn "call_split"
  [(call (mem:SI (match_operand 0 "call_insn_operand" "cS"))
	 (match_operand 1 "" ""))
   (clobber (reg:SI 31))
   (clobber (reg:SI 28))]
  "TARGET_SPLIT_CALLS"
  { return MIPS_CALL ("jal", operands, 0); }
  [(set_attr "type" "call")])

(define_expand "call_value"
  [(parallel [(set (match_operand 0 "")
		   (call (match_operand 1 "")
			 (match_operand 2 "")))
	      (use (match_operand 3 ""))])]		;; next_arg_reg
  ""
{
  mips_expand_call (operands[0], XEXP (operands[1], 0),
		    operands[2], operands[3], false);
  DONE;
})

;; See comment for call_internal.
(define_insn_and_split "call_value_internal"
  [(set (match_operand 0 "register_operand" "=df,df")
        (call (mem:SI (match_operand 1 "call_insn_operand" "c,S"))
              (match_operand 2 "" "")))
   (clobber (reg:SI 31))]
  ""
  { return TARGET_SPLIT_CALLS ? "#" : MIPS_CALL ("jal", operands, 1); }
  "reload_completed && TARGET_SPLIT_CALLS && (operands[3] = insn)"
  [(const_int 0)]
{
  emit_call_insn (gen_call_value_split (operands[0], operands[1],
					operands[2]));
  if (!find_reg_note (operands[3], REG_NORETURN, 0))
    mips_restore_gp ();
  DONE;
}
  [(set_attr "jal" "indirect,direct")
   (set_attr "extended_mips16" "no,yes")])

(define_insn "call_value_split"
  [(set (match_operand 0 "register_operand" "=df")
        (call (mem:SI (match_operand 1 "call_insn_operand" "cS"))
              (match_operand 2 "" "")))
   (clobber (reg:SI 31))
   (clobber (reg:SI 28))]
  "TARGET_SPLIT_CALLS"
  { return MIPS_CALL ("jal", operands, 1); }
  [(set_attr "type" "call")])

;; See comment for call_internal.
(define_insn_and_split "call_value_multiple_internal"
  [(set (match_operand 0 "register_operand" "=df,df")
        (call (mem:SI (match_operand 1 "call_insn_operand" "c,S"))
              (match_operand 2 "" "")))
   (set (match_operand 3 "register_operand" "=df,df")
	(call (mem:SI (match_dup 1))
	      (match_dup 2)))
   (clobber (reg:SI 31))]
  ""
  { return TARGET_SPLIT_CALLS ? "#" : MIPS_CALL ("jal", operands, 1); }
  "reload_completed && TARGET_SPLIT_CALLS && (operands[4] = insn)"
  [(const_int 0)]
{
  emit_call_insn (gen_call_value_multiple_split (operands[0], operands[1],
						 operands[2], operands[3]));
  if (!find_reg_note (operands[4], REG_NORETURN, 0))
    mips_restore_gp ();
  DONE;
}
  [(set_attr "jal" "indirect,direct")
   (set_attr "extended_mips16" "no,yes")])

(define_insn "call_value_multiple_split"
  [(set (match_operand 0 "register_operand" "=df")
        (call (mem:SI (match_operand 1 "call_insn_operand" "cS"))
              (match_operand 2 "" "")))
   (set (match_operand 3 "register_operand" "=df")
	(call (mem:SI (match_dup 1))
	      (match_dup 2)))
   (clobber (reg:SI 31))
   (clobber (reg:SI 28))]
  "TARGET_SPLIT_CALLS"
  { return MIPS_CALL ("jal", operands, 1); }
  [(set_attr "type" "call")])

;; Call subroutine returning any type.

(define_expand "untyped_call"
  [(parallel [(call (match_operand 0 "")
		    (const_int 0))
	      (match_operand 1 "")
	      (match_operand 2 "")])]
  ""
{
  int i;

  emit_call_insn (GEN_CALL (operands[0], const0_rtx, NULL, const0_rtx));

  for (i = 0; i < XVECLEN (operands[2], 0); i++)
    {
      rtx set = XVECEXP (operands[2], 0, i);
      emit_move_insn (SET_DEST (set), SET_SRC (set));
    }

  emit_insn (gen_blockage ());
  DONE;
})

;;
;;  ....................
;;
;;	MISC.
;;
;;  ....................
;;


(define_insn "prefetch"
  [(prefetch (match_operand:QI 0 "address_operand" "p")
	     (match_operand 1 "const_int_operand" "n")
	     (match_operand 2 "const_int_operand" "n"))]
  "ISA_HAS_PREFETCH && TARGET_EXPLICIT_RELOCS"
{
  operands[1] = mips_prefetch_cookie (operands[1], operands[2]);
  return "pref\t%1,%a0";
}
  [(set_attr "type" "prefetch")])

(define_insn "*prefetch_indexed_<mode>"
  [(prefetch (plus:P (match_operand:P 0 "register_operand" "d")
		     (match_operand:P 1 "register_operand" "d"))
	     (match_operand 2 "const_int_operand" "n")
	     (match_operand 3 "const_int_operand" "n"))]
  "ISA_HAS_PREFETCHX && TARGET_HARD_FLOAT && TARGET_DOUBLE_FLOAT"
{
  operands[2] = mips_prefetch_cookie (operands[2], operands[3]);
  return "prefx\t%2,%1(%0)";
}
  [(set_attr "type" "prefetchx")])

(define_insn "nop"
  [(const_int 0)]
  ""
  "%(nop%)"
  [(set_attr "type"	"nop")
   (set_attr "mode"	"none")])

;; Like nop, but commented out when outside a .set noreorder block.
(define_insn "hazard_nop"
  [(const_int 1)]
  ""
  {
    if (set_noreorder)
      return "nop";
    else
      return "#nop";
  }
  [(set_attr "type"	"nop")])

;; MIPS4 Conditional move instructions.

(define_insn "*mov<GPR:mode>_on_<MOVECC:mode>"
  [(set (match_operand:GPR 0 "register_operand" "=d,d")
	(if_then_else:GPR
	 (match_operator:MOVECC 4 "equality_operator"
		[(match_operand:MOVECC 1 "register_operand" "<MOVECC:reg>,<MOVECC:reg>")
		 (const_int 0)])
	 (match_operand:GPR 2 "reg_or_0_operand" "dJ,0")
	 (match_operand:GPR 3 "reg_or_0_operand" "0,dJ")))]
  "ISA_HAS_CONDMOVE"
  "@
    mov%T4\t%0,%z2,%1
    mov%t4\t%0,%z3,%1"
  [(set_attr "type" "condmove")
   (set_attr "mode" "<GPR:MODE>")])

(define_insn "*mov<SCALARF:mode>_on_<MOVECC:mode>"
  [(set (match_operand:SCALARF 0 "register_operand" "=f,f")
	(if_then_else:SCALARF
	 (match_operator:MOVECC 4 "equality_operator"
		[(match_operand:MOVECC 1 "register_operand" "<MOVECC:reg>,<MOVECC:reg>")
		 (const_int 0)])
	 (match_operand:SCALARF 2 "register_operand" "f,0")
	 (match_operand:SCALARF 3 "register_operand" "0,f")))]
  "ISA_HAS_CONDMOVE"
  "@
    mov%T4.<fmt>\t%0,%2,%1
    mov%t4.<fmt>\t%0,%3,%1"
  [(set_attr "type" "condmove")
   (set_attr "mode" "<SCALARF:MODE>")])

;; These are the main define_expand's used to make conditional moves.

(define_expand "mov<mode>cc"
  [(set (match_dup 4) (match_operand 1 "comparison_operator"))
   (set (match_operand:GPR 0 "register_operand")
	(if_then_else:GPR (match_dup 5)
			  (match_operand:GPR 2 "reg_or_0_operand")
			  (match_operand:GPR 3 "reg_or_0_operand")))]
  "ISA_HAS_CONDMOVE"
{
  gen_conditional_move (operands);
  DONE;
})

(define_expand "mov<mode>cc"
  [(set (match_dup 4) (match_operand 1 "comparison_operator"))
   (set (match_operand:SCALARF 0 "register_operand")
	(if_then_else:SCALARF (match_dup 5)
			      (match_operand:SCALARF 2 "register_operand")
			      (match_operand:SCALARF 3 "register_operand")))]
  "ISA_HAS_CONDMOVE"
{
  gen_conditional_move (operands);
  DONE;
})

;;
;;  ....................
;;
;;	mips16 inline constant tables
;;
;;  ....................
;;

(define_insn "consttable_int"
  [(unspec_volatile [(match_operand 0 "consttable_operand" "")
		     (match_operand 1 "const_int_operand" "")]
		    UNSPEC_CONSTTABLE_INT)]
  "TARGET_MIPS16"
{
  assemble_integer (operands[0], INTVAL (operands[1]),
		    BITS_PER_UNIT * INTVAL (operands[1]), 1);
  return "";
}
  [(set (attr "length") (symbol_ref "INTVAL (operands[1])"))])

(define_insn "consttable_float"
  [(unspec_volatile [(match_operand 0 "consttable_operand" "")]
		    UNSPEC_CONSTTABLE_FLOAT)]
  "TARGET_MIPS16"
{
  REAL_VALUE_TYPE d;

  gcc_assert (GET_CODE (operands[0]) == CONST_DOUBLE);
  REAL_VALUE_FROM_CONST_DOUBLE (d, operands[0]);
  assemble_real (d, GET_MODE (operands[0]),
		 GET_MODE_BITSIZE (GET_MODE (operands[0])));
  return "";
}
  [(set (attr "length")
	(symbol_ref "GET_MODE_SIZE (GET_MODE (operands[0]))"))])

(define_insn "align"
  [(unspec_volatile [(match_operand 0 "const_int_operand" "")] UNSPEC_ALIGN)]
  ""
  ".align\t%0"
  [(set (attr "length") (symbol_ref "(1 << INTVAL (operands[0])) - 1"))])

(define_split
  [(match_operand 0 "small_data_pattern")]
  "reload_completed"
  [(match_dup 0)]
  { operands[0] = mips_rewrite_small_data (operands[0]); })

; Thread-Local Storage

; The TLS base pointer is accessed via "rdhwr $v1, $29".  No current
; MIPS architecture defines this register, and no current
; implementation provides it; instead, any OS which supports TLS is
; expected to trap and emulate this instruction.  rdhwr is part of the
; MIPS 32r2 specification, but we use it on any architecture because
; we expect it to be emulated.  Use .set to force the assembler to
; accept it.

(define_insn "tls_get_tp_<mode>"
  [(set (match_operand:P 0 "register_operand" "=v")
	(unspec:P [(const_int 0)]
		  UNSPEC_TLS_GET_TP))]
  "HAVE_AS_TLS && !TARGET_MIPS16"
  ".set\tpush\;.set\tmips32r2\t\;rdhwr\t%0,$29\;.set\tpop"
  [(set_attr "type" "unknown")
   ; Since rdhwr always generates a trap for now, putting it in a delay
   ; slot would make the kernel's emulation of it much slower.
   (set_attr "can_delay" "no")
   (set_attr "mode" "<MODE>")])

; The MIPS Paired-Single Floating Point and MIPS-3D Instructions.

(include "mips-ps-3d.md")

; The MIPS DSP Instructions.

(include "mips-dsp.md")
