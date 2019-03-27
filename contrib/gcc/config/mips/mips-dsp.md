(define_constants
  [(CCDSP_PO_REGNUM	182)
   (CCDSP_SC_REGNUM	183)
   (CCDSP_CA_REGNUM	184)
   (CCDSP_OU_REGNUM	185)
   (CCDSP_CC_REGNUM	186)
   (CCDSP_EF_REGNUM	187)])

;; This mode macro allows si, v2hi, v4qi for all possible modes in DSP ASE.
(define_mode_macro DSP [(SI "TARGET_DSP")
			(V2HI "TARGET_DSP")
		 	(V4QI "TARGET_DSP")])

;; This mode macro allows v2hi, v4qi for vector/SIMD data.
(define_mode_macro DSPV [(V2HI "TARGET_DSP")
			 (V4QI "TARGET_DSP")])

;; This mode macro allows si, v2hi for Q31 and V2Q15 fixed-point data.
(define_mode_macro DSPQ [(SI "TARGET_DSP")
		         (V2HI "TARGET_DSP")])

;; DSP instructions use q for fixed-point data, and u for integer in the infix.
(define_mode_attr dspfmt1 [(SI "q") (V2HI "q") (V4QI "u")])

;; DSP instructions use nothing for fixed-point data, and u for integer in
;; the infix.
(define_mode_attr dspfmt1_1 [(SI "") (V2HI "") (V4QI "u")])

;; DSP instructions use w, ph, qb in the postfix.
(define_mode_attr dspfmt2 [(SI "w") (V2HI "ph") (V4QI "qb")])

;; DSP shift masks for SI, V2HI, V4QI.
(define_mode_attr dspshift_mask [(SI "0x1f") (V2HI "0xf") (V4QI "0x7")])

;; MIPS DSP ASE Revision 0.98 3/24/2005
;; Table 2-1. MIPS DSP ASE Instructions: Arithmetic
;; ADDQ*
(define_insn "add<DSPV:mode>3"
  [(parallel
    [(set (match_operand:DSPV 0 "register_operand" "=d")
	  (plus:DSPV (match_operand:DSPV 1 "register_operand" "d")
		     (match_operand:DSPV 2 "register_operand" "d")))
     (set (reg:CCDSP CCDSP_OU_REGNUM)
	  (unspec:CCDSP [(match_dup 1) (match_dup 2)] UNSPEC_ADDQ))])]
  ""
  "add<DSPV:dspfmt1>.<DSPV:dspfmt2>\t%0,%1,%2"
  [(set_attr "type"	"arith")
   (set_attr "mode"	"SI")])

(define_insn "mips_add<DSP:dspfmt1>_s_<DSP:dspfmt2>"
  [(parallel
    [(set (match_operand:DSP 0 "register_operand" "=d")
	  (unspec:DSP [(match_operand:DSP 1 "register_operand" "d")
		       (match_operand:DSP 2 "register_operand" "d")]
		      UNSPEC_ADDQ_S))
     (set (reg:CCDSP CCDSP_OU_REGNUM)
	  (unspec:CCDSP [(match_dup 1) (match_dup 2)] UNSPEC_ADDQ_S))])]
  ""
  "add<DSP:dspfmt1>_s.<DSP:dspfmt2>\t%0,%1,%2"
  [(set_attr "type"	"arith")
   (set_attr "mode"	"SI")])

;; SUBQ*
(define_insn "sub<DSPV:mode>3"
  [(parallel
    [(set (match_operand:DSPV 0 "register_operand" "=d")
	  (minus:DSPV (match_operand:DSPV 1 "register_operand" "d")
		      (match_operand:DSPV 2 "register_operand" "d")))
     (set (reg:CCDSP CCDSP_OU_REGNUM)
	  (unspec:CCDSP [(match_dup 1) (match_dup 2)] UNSPEC_SUBQ))])]
  "TARGET_DSP"
  "sub<DSPV:dspfmt1>.<DSPV:dspfmt2>\t%0,%1,%2"
  [(set_attr "type"	"arith")
   (set_attr "mode"	"SI")])

(define_insn "mips_sub<DSP:dspfmt1>_s_<DSP:dspfmt2>"
  [(parallel
    [(set (match_operand:DSP 0 "register_operand" "=d")
	  (unspec:DSP [(match_operand:DSP 1 "register_operand" "d")
		       (match_operand:DSP 2 "register_operand" "d")]
		      UNSPEC_SUBQ_S))
     (set (reg:CCDSP CCDSP_OU_REGNUM)
	  (unspec:CCDSP [(match_dup 1) (match_dup 2)] UNSPEC_SUBQ_S))])]
  "TARGET_DSP"
  "sub<DSP:dspfmt1>_s.<DSP:dspfmt2>\t%0,%1,%2"
  [(set_attr "type"	"arith")
   (set_attr "mode"	"SI")])

;; ADDSC
(define_insn "mips_addsc"
  [(parallel
    [(set (match_operand:SI 0 "register_operand" "=d")
	  (unspec:SI [(match_operand:SI 1 "register_operand" "d")
		      (match_operand:SI 2 "register_operand" "d")]
		     UNSPEC_ADDSC))
     (set (reg:CCDSP CCDSP_CA_REGNUM)
	  (unspec:CCDSP [(match_dup 1) (match_dup 2)] UNSPEC_ADDSC))])]
  "TARGET_DSP"
  "addsc\t%0,%1,%2"
  [(set_attr "type"	"arith")
   (set_attr "mode"	"SI")])

;; ADDWC
(define_insn "mips_addwc"
  [(parallel
    [(set (match_operand:SI 0 "register_operand" "=d")
	  (unspec:SI [(match_operand:SI 1 "register_operand" "d")
		      (match_operand:SI 2 "register_operand" "d")
		    (reg:CCDSP CCDSP_CA_REGNUM)]
		     UNSPEC_ADDWC))
     (set (reg:CCDSP CCDSP_OU_REGNUM)
	  (unspec:CCDSP [(match_dup 1) (match_dup 2)] UNSPEC_ADDWC))])]
  "TARGET_DSP"
  "addwc\t%0,%1,%2"
  [(set_attr "type"	"arith")
   (set_attr "mode"	"SI")])

;; MODSUB
(define_insn "mips_modsub"
  [(set (match_operand:SI 0 "register_operand" "=d")
	(unspec:SI [(match_operand:SI 1 "register_operand" "d")
		    (match_operand:SI 2 "register_operand" "d")]
		   UNSPEC_MODSUB))]
  "TARGET_DSP"
  "modsub\t%0,%1,%2"
  [(set_attr "type"	"arith")
   (set_attr "mode"	"SI")])

;; RADDU*
(define_insn "mips_raddu_w_qb"
  [(set (match_operand:SI 0 "register_operand" "=d")
	(unspec:SI [(match_operand:V4QI 1 "register_operand" "d")]
		   UNSPEC_RADDU_W_QB))]
  "TARGET_DSP"
  "raddu.w.qb\t%0,%1"
  [(set_attr "type"	"arith")
   (set_attr "mode"	"SI")])

;; ABSQ*
(define_insn "mips_absq_s_<DSPQ:dspfmt2>"
  [(parallel
    [(set (match_operand:DSPQ 0 "register_operand" "=d")
	  (unspec:DSPQ [(match_operand:DSPQ 1 "register_operand" "d")]
		       UNSPEC_ABSQ_S))
     (set (reg:CCDSP CCDSP_OU_REGNUM)
	  (unspec:CCDSP [(match_dup 1)] UNSPEC_ABSQ_S))])]
  "TARGET_DSP"
  "absq_s.<DSPQ:dspfmt2>\t%0,%1"
  [(set_attr "type"	"arith")
   (set_attr "mode"	"SI")])

;; PRECRQ*
(define_insn "mips_precrq_qb_ph"
  [(set (match_operand:V4QI 0 "register_operand" "=d")
	(unspec:V4QI [(match_operand:V2HI 1 "register_operand" "d")
		      (match_operand:V2HI 2 "register_operand" "d")]
		     UNSPEC_PRECRQ_QB_PH))]
  "TARGET_DSP"
  "precrq.qb.ph\t%0,%1,%2"
  [(set_attr "type"	"arith")
   (set_attr "mode"	"SI")])

(define_insn "mips_precrq_ph_w"
  [(set (match_operand:V2HI 0 "register_operand" "=d")
	(unspec:V2HI [(match_operand:SI 1 "register_operand" "d")
		      (match_operand:SI 2 "register_operand" "d")]
		     UNSPEC_PRECRQ_PH_W))]
  "TARGET_DSP"
  "precrq.ph.w\t%0,%1,%2"
  [(set_attr "type"	"arith")
   (set_attr "mode"	"SI")])

(define_insn "mips_precrq_rs_ph_w"
  [(parallel
    [(set (match_operand:V2HI 0 "register_operand" "=d")
	  (unspec:V2HI [(match_operand:SI 1 "register_operand" "d")
			(match_operand:SI 2 "register_operand" "d")]
		       UNSPEC_PRECRQ_RS_PH_W))
     (set (reg:CCDSP CCDSP_OU_REGNUM)
	  (unspec:CCDSP [(match_dup 1) (match_dup 2)]
			UNSPEC_PRECRQ_RS_PH_W))])]
  "TARGET_DSP"
  "precrq_rs.ph.w\t%0,%1,%2"
  [(set_attr "type"	"arith")
   (set_attr "mode"	"SI")])

;; PRECRQU*
(define_insn "mips_precrqu_s_qb_ph"
  [(parallel
    [(set (match_operand:V4QI 0 "register_operand" "=d")
	  (unspec:V4QI [(match_operand:V2HI 1 "register_operand" "d")
			(match_operand:V2HI 2 "register_operand" "d")]
		       UNSPEC_PRECRQU_S_QB_PH))
     (set (reg:CCDSP CCDSP_OU_REGNUM)
	  (unspec:CCDSP [(match_dup 1) (match_dup 2)]
			UNSPEC_PRECRQU_S_QB_PH))])]
  "TARGET_DSP"
  "precrqu_s.qb.ph\t%0,%1,%2"
  [(set_attr "type"	"arith")
   (set_attr "mode"	"SI")])

;; PRECEQ*
(define_insn "mips_preceq_w_phl"
  [(set (match_operand:SI 0 "register_operand" "=d")
	(unspec:SI [(match_operand:V2HI 1 "register_operand" "d")]
		   UNSPEC_PRECEQ_W_PHL))]
  "TARGET_DSP"
  "preceq.w.phl\t%0,%1"
  [(set_attr "type"	"arith")
   (set_attr "mode"	"SI")])

(define_insn "mips_preceq_w_phr"
  [(set (match_operand:SI 0 "register_operand" "=d")
	(unspec:SI [(match_operand:V2HI 1 "register_operand" "d")]
		   UNSPEC_PRECEQ_W_PHR))]
  "TARGET_DSP"
  "preceq.w.phr\t%0,%1"
  [(set_attr "type"	"arith")
   (set_attr "mode"	"SI")])

;; PRECEQU*
(define_insn "mips_precequ_ph_qbl"
  [(set (match_operand:V2HI 0 "register_operand" "=d")
	(unspec:V2HI [(match_operand:V4QI 1 "register_operand" "d")]
		     UNSPEC_PRECEQU_PH_QBL))]
  "TARGET_DSP"
  "precequ.ph.qbl\t%0,%1"
  [(set_attr "type"	"arith")
   (set_attr "mode"	"SI")])

(define_insn "mips_precequ_ph_qbr"
  [(set (match_operand:V2HI 0 "register_operand" "=d")
	(unspec:V2HI [(match_operand:V4QI 1 "register_operand" "d")]
		     UNSPEC_PRECEQU_PH_QBR))]
  "TARGET_DSP"
  "precequ.ph.qbr\t%0,%1"
  [(set_attr "type"	"arith")
   (set_attr "mode"	"SI")])

(define_insn "mips_precequ_ph_qbla"
  [(set (match_operand:V2HI 0 "register_operand" "=d")
	(unspec:V2HI [(match_operand:V4QI 1 "register_operand" "d")]
		     UNSPEC_PRECEQU_PH_QBLA))]
  "TARGET_DSP"
  "precequ.ph.qbla\t%0,%1"
  [(set_attr "type"	"arith")
   (set_attr "mode"	"SI")])

(define_insn "mips_precequ_ph_qbra"
  [(set (match_operand:V2HI 0 "register_operand" "=d")
	(unspec:V2HI [(match_operand:V4QI 1 "register_operand" "d")]
		     UNSPEC_PRECEQU_PH_QBRA))]
  "TARGET_DSP"
  "precequ.ph.qbra\t%0,%1"
  [(set_attr "type"	"arith")
   (set_attr "mode"	"SI")])

;; PRECEU*
(define_insn "mips_preceu_ph_qbl"
  [(set (match_operand:V2HI 0 "register_operand" "=d")
	(unspec:V2HI [(match_operand:V4QI 1 "register_operand" "d")]
		     UNSPEC_PRECEU_PH_QBL))]
  "TARGET_DSP"
  "preceu.ph.qbl\t%0,%1"
  [(set_attr "type"	"arith")
   (set_attr "mode"	"SI")])

(define_insn "mips_preceu_ph_qbr"
  [(set (match_operand:V2HI 0 "register_operand" "=d")
	(unspec:V2HI [(match_operand:V4QI 1 "register_operand" "d")]
		     UNSPEC_PRECEU_PH_QBR))]
  "TARGET_DSP"
  "preceu.ph.qbr\t%0,%1"
  [(set_attr "type"	"arith")
   (set_attr "mode"	"SI")])

(define_insn "mips_preceu_ph_qbla"
  [(set (match_operand:V2HI 0 "register_operand" "=d")
	(unspec:V2HI [(match_operand:V4QI 1 "register_operand" "d")]
		     UNSPEC_PRECEU_PH_QBLA))]
  "TARGET_DSP"
  "preceu.ph.qbla\t%0,%1"
  [(set_attr "type"	"arith")
   (set_attr "mode"	"SI")])

(define_insn "mips_preceu_ph_qbra"
  [(set (match_operand:V2HI 0 "register_operand" "=d")
	(unspec:V2HI [(match_operand:V4QI 1 "register_operand" "d")]
		     UNSPEC_PRECEU_PH_QBRA))]
  "TARGET_DSP"
  "preceu.ph.qbra\t%0,%1"
  [(set_attr "type"	"arith")
   (set_attr "mode"	"SI")])

;; Table 2-2. MIPS DSP ASE Instructions: Shift
;; SHLL*
(define_insn "mips_shll_<DSPV:dspfmt2>"
  [(parallel
    [(set (match_operand:DSPV 0 "register_operand" "=d,d")
	  (unspec:DSPV [(match_operand:DSPV 1 "register_operand" "d,d")
			(match_operand:SI 2 "arith_operand" "I,d")]
		       UNSPEC_SHLL))
     (set (reg:CCDSP CCDSP_OU_REGNUM)
	  (unspec:CCDSP [(match_dup 1) (match_dup 2)] UNSPEC_SHLL))])]
  "TARGET_DSP"
{
  if (which_alternative == 0)
    {
      if (INTVAL (operands[2])
	  & ~(unsigned HOST_WIDE_INT) <DSPV:dspshift_mask>)
	operands[2] = GEN_INT (INTVAL (operands[2]) & <DSPV:dspshift_mask>);
      return "shll.<DSPV:dspfmt2>\t%0,%1,%2";
    }
  return "shllv.<DSPV:dspfmt2>\t%0,%1,%2";
}
  [(set_attr "type"	"shift")
   (set_attr "mode"	"SI")])

(define_insn "mips_shll_s_<DSPQ:dspfmt2>"
  [(parallel
    [(set (match_operand:DSPQ 0 "register_operand" "=d,d")
	  (unspec:DSPQ [(match_operand:DSPQ 1 "register_operand" "d,d")
			(match_operand:SI 2 "arith_operand" "I,d")]
		       UNSPEC_SHLL_S))
     (set (reg:CCDSP CCDSP_OU_REGNUM)
	  (unspec:CCDSP [(match_dup 1) (match_dup 2)] UNSPEC_SHLL_S))])]
  "TARGET_DSP"
{
  if (which_alternative == 0)
    {
      if (INTVAL (operands[2])
          & ~(unsigned HOST_WIDE_INT) <DSPQ:dspshift_mask>)
	operands[2] = GEN_INT (INTVAL (operands[2]) & <DSPQ:dspshift_mask>);
      return "shll_s.<DSPQ:dspfmt2>\t%0,%1,%2";
    }
  return "shllv_s.<DSPQ:dspfmt2>\t%0,%1,%2";
}
  [(set_attr "type"	"shift")
   (set_attr "mode"	"SI")])

;; SHRL*
(define_insn "mips_shrl_qb"
  [(set (match_operand:V4QI 0 "register_operand" "=d,d")
	(unspec:V4QI [(match_operand:V4QI 1 "register_operand" "d,d")
		      (match_operand:SI 2 "arith_operand" "I,d")]
		     UNSPEC_SHRL_QB))]
  "TARGET_DSP"
{
  if (which_alternative == 0)
    {
      if (INTVAL (operands[2]) & ~(unsigned HOST_WIDE_INT) 0x7)
	operands[2] = GEN_INT (INTVAL (operands[2]) & 0x7);
      return "shrl.qb\t%0,%1,%2";
    }
  return "shrlv.qb\t%0,%1,%2";
}
  [(set_attr "type"	"shift")
   (set_attr "mode"	"SI")])

;; SHRA*
(define_insn "mips_shra_ph"
  [(set (match_operand:V2HI 0 "register_operand" "=d,d")
	(unspec:V2HI [(match_operand:V2HI 1 "register_operand" "d,d")
		      (match_operand:SI 2 "arith_operand" "I,d")]
		     UNSPEC_SHRA_PH))]
  "TARGET_DSP"
{
  if (which_alternative == 0)
    {
      if (INTVAL (operands[2]) & ~(unsigned HOST_WIDE_INT) 0xf)
	operands[2] = GEN_INT (INTVAL (operands[2]) & 0xf);
      return "shra.ph\t%0,%1,%2";
    }
  return "shrav.ph\t%0,%1,%2";
}
  [(set_attr "type"	"shift")
   (set_attr "mode"	"SI")])

(define_insn "mips_shra_r_<DSPQ:dspfmt2>"
  [(set (match_operand:DSPQ 0 "register_operand" "=d,d")
	(unspec:DSPQ [(match_operand:DSPQ 1 "register_operand" "d,d")
		      (match_operand:SI 2 "arith_operand" "I,d")]
		     UNSPEC_SHRA_R))]
  "TARGET_DSP"
{
  if (which_alternative == 0)
    {
      if (INTVAL (operands[2])
	  & ~(unsigned HOST_WIDE_INT) <DSPQ:dspshift_mask>)
	operands[2] = GEN_INT (INTVAL (operands[2]) & <DSPQ:dspshift_mask>);
      return "shra_r.<DSPQ:dspfmt2>\t%0,%1,%2";
    }
  return "shrav_r.<DSPQ:dspfmt2>\t%0,%1,%2";
}
  [(set_attr "type"	"shift")
   (set_attr "mode"	"SI")])

;; Table 2-3. MIPS DSP ASE Instructions: Multiply
;; MULEU*
(define_insn "mips_muleu_s_ph_qbl"
  [(parallel
    [(set (match_operand:V2HI 0 "register_operand" "=d")
	  (unspec:V2HI [(match_operand:V4QI 1 "register_operand" "d")
			(match_operand:V2HI 2 "register_operand" "d")]
		       UNSPEC_MULEU_S_PH_QBL))
     (set (reg:CCDSP CCDSP_OU_REGNUM)
	  (unspec:CCDSP [(match_dup 1) (match_dup 2)] UNSPEC_MULEU_S_PH_QBL))
     (clobber (match_scratch:DI 3 "=x"))])]
  "TARGET_DSP"
  "muleu_s.ph.qbl\t%0,%1,%2"
  [(set_attr "type"	"imul3")
   (set_attr "mode"	"SI")])

(define_insn "mips_muleu_s_ph_qbr"
  [(parallel
    [(set (match_operand:V2HI 0 "register_operand" "=d")
	  (unspec:V2HI [(match_operand:V4QI 1 "register_operand" "d")
			(match_operand:V2HI 2 "register_operand" "d")]
		       UNSPEC_MULEU_S_PH_QBR))
     (set (reg:CCDSP CCDSP_OU_REGNUM)
	  (unspec:CCDSP [(match_dup 1) (match_dup 2)] UNSPEC_MULEU_S_PH_QBR))
     (clobber (match_scratch:DI 3 "=x"))])]
  "TARGET_DSP"
  "muleu_s.ph.qbr\t%0,%1,%2"
  [(set_attr "type"	"imul3")
   (set_attr "mode"	"SI")])

;; MULQ*
(define_insn "mips_mulq_rs_ph"
  [(parallel
    [(set (match_operand:V2HI 0 "register_operand" "=d")
	  (unspec:V2HI [(match_operand:V2HI 1 "register_operand" "d")
			(match_operand:V2HI 2 "register_operand" "d")]
		       UNSPEC_MULQ_RS_PH))
     (set (reg:CCDSP CCDSP_OU_REGNUM)
	  (unspec:CCDSP [(match_dup 1) (match_dup 2)] UNSPEC_MULQ_RS_PH))
     (clobber (match_scratch:DI 3 "=x"))])]
  "TARGET_DSP"
  "mulq_rs.ph\t%0,%1,%2"
  [(set_attr "type"	"imul3")
   (set_attr "mode"	"SI")])

;; MULEQ*
(define_insn "mips_muleq_s_w_phl"
  [(parallel
    [(set (match_operand:SI 0 "register_operand" "=d")
	  (unspec:SI [(match_operand:V2HI 1 "register_operand" "d")
		      (match_operand:V2HI 2 "register_operand" "d")]
		     UNSPEC_MULEQ_S_W_PHL))
     (set (reg:CCDSP CCDSP_OU_REGNUM)
	  (unspec:CCDSP [(match_dup 1) (match_dup 2)] UNSPEC_MULEQ_S_W_PHL))
     (clobber (match_scratch:DI 3 "=x"))])]
  "TARGET_DSP"
  "muleq_s.w.phl\t%0,%1,%2"
  [(set_attr "type"	"imul3")
   (set_attr "mode"	"SI")])

(define_insn "mips_muleq_s_w_phr"
  [(parallel
    [(set (match_operand:SI 0 "register_operand" "=d")
	  (unspec:SI [(match_operand:V2HI 1 "register_operand" "d")
		      (match_operand:V2HI 2 "register_operand" "d")]
		     UNSPEC_MULEQ_S_W_PHR))
     (set (reg:CCDSP CCDSP_OU_REGNUM)
	  (unspec:CCDSP [(match_dup 1) (match_dup 2)] UNSPEC_MULEQ_S_W_PHR))
     (clobber (match_scratch:DI 3 "=x"))])]
  "TARGET_DSP"
  "muleq_s.w.phr\t%0,%1,%2"
  [(set_attr "type"	"imul3")
   (set_attr "mode"	"SI")])

;; DPAU*
(define_insn "mips_dpau_h_qbl"
  [(set (match_operand:DI 0 "register_operand" "=a")
	(unspec:DI [(match_operand:DI 1 "register_operand" "0")
		    (match_operand:V4QI 2 "register_operand" "d")
		    (match_operand:V4QI 3 "register_operand" "d")]
		   UNSPEC_DPAU_H_QBL))]
  "TARGET_DSP && !TARGET_64BIT"
  "dpau.h.qbl\t%q0,%2,%3"
  [(set_attr "type"	"imadd")
   (set_attr "mode"	"SI")])

(define_insn "mips_dpau_h_qbr"
  [(set (match_operand:DI 0 "register_operand" "=a")
	(unspec:DI [(match_operand:DI 1 "register_operand" "0")
		    (match_operand:V4QI 2 "register_operand" "d")
		    (match_operand:V4QI 3 "register_operand" "d")]
		   UNSPEC_DPAU_H_QBR))]
  "TARGET_DSP && !TARGET_64BIT"
  "dpau.h.qbr\t%q0,%2,%3"
  [(set_attr "type"	"imadd")
   (set_attr "mode"	"SI")])

;; DPSU*
(define_insn "mips_dpsu_h_qbl"
  [(set (match_operand:DI 0 "register_operand" "=a")
	(unspec:DI [(match_operand:DI 1 "register_operand" "0")
		    (match_operand:V4QI 2 "register_operand" "d")
		    (match_operand:V4QI 3 "register_operand" "d")]
		   UNSPEC_DPSU_H_QBL))]
  "TARGET_DSP && !TARGET_64BIT"
  "dpsu.h.qbl\t%q0,%2,%3"
  [(set_attr "type"	"imadd")
   (set_attr "mode"	"SI")])

(define_insn "mips_dpsu_h_qbr"
  [(set (match_operand:DI 0 "register_operand" "=a")
	(unspec:DI [(match_operand:DI 1 "register_operand" "0")
		    (match_operand:V4QI 2 "register_operand" "d")
		    (match_operand:V4QI 3 "register_operand" "d")]
		   UNSPEC_DPSU_H_QBR))]
  "TARGET_DSP && !TARGET_64BIT"
  "dpsu.h.qbr\t%q0,%2,%3"
  [(set_attr "type"	"imadd")
   (set_attr "mode"	"SI")])

;; DPAQ*
(define_insn "mips_dpaq_s_w_ph"
  [(parallel
    [(set (match_operand:DI 0 "register_operand" "=a")
	  (unspec:DI [(match_operand:DI 1 "register_operand" "0")
		      (match_operand:V2HI 2 "register_operand" "d")
		      (match_operand:V2HI 3 "register_operand" "d")]
		     UNSPEC_DPAQ_S_W_PH))
     (set (reg:CCDSP CCDSP_OU_REGNUM)
	  (unspec:CCDSP [(match_dup 1) (match_dup 2) (match_dup 3)]
			UNSPEC_DPAQ_S_W_PH))])]
  "TARGET_DSP && !TARGET_64BIT"
  "dpaq_s.w.ph\t%q0,%2,%3"
  [(set_attr "type"	"imadd")
   (set_attr "mode"	"SI")])

;; DPSQ*
(define_insn "mips_dpsq_s_w_ph"
  [(parallel
    [(set (match_operand:DI 0 "register_operand" "=a")
	  (unspec:DI [(match_operand:DI 1 "register_operand" "0")
		      (match_operand:V2HI 2 "register_operand" "d")
		      (match_operand:V2HI 3 "register_operand" "d")]
		     UNSPEC_DPSQ_S_W_PH))
     (set (reg:CCDSP CCDSP_OU_REGNUM)
	  (unspec:CCDSP [(match_dup 1) (match_dup 2) (match_dup 3)]
			UNSPEC_DPSQ_S_W_PH))])]
  "TARGET_DSP && !TARGET_64BIT"
  "dpsq_s.w.ph\t%q0,%2,%3"
  [(set_attr "type"	"imadd")
   (set_attr "mode"	"SI")])

;; MULSAQ*
(define_insn "mips_mulsaq_s_w_ph"
  [(parallel
    [(set (match_operand:DI 0 "register_operand" "=a")
	  (unspec:DI [(match_operand:DI 1 "register_operand" "0")
		      (match_operand:V2HI 2 "register_operand" "d")
		      (match_operand:V2HI 3 "register_operand" "d")]
		     UNSPEC_MULSAQ_S_W_PH))
     (set (reg:CCDSP CCDSP_OU_REGNUM)
	  (unspec:CCDSP [(match_dup 1) (match_dup 2) (match_dup 3)]
			UNSPEC_MULSAQ_S_W_PH))])]
  "TARGET_DSP && !TARGET_64BIT"
  "mulsaq_s.w.ph\t%q0,%2,%3"
  [(set_attr "type"	"imadd")
   (set_attr "mode"	"SI")])

;; DPAQ*
(define_insn "mips_dpaq_sa_l_w"
  [(parallel
    [(set (match_operand:DI 0 "register_operand" "=a")
	  (unspec:DI [(match_operand:DI 1 "register_operand" "0")
		      (match_operand:SI 2 "register_operand" "d")
		      (match_operand:SI 3 "register_operand" "d")]
		     UNSPEC_DPAQ_SA_L_W))
     (set (reg:CCDSP CCDSP_OU_REGNUM)
	  (unspec:CCDSP [(match_dup 1) (match_dup 2) (match_dup 3)]
			UNSPEC_DPAQ_SA_L_W))])]
  "TARGET_DSP && !TARGET_64BIT"
  "dpaq_sa.l.w\t%q0,%2,%3"
  [(set_attr "type"	"imadd")
   (set_attr "mode"	"SI")])

;; DPSQ*
(define_insn "mips_dpsq_sa_l_w"
  [(parallel
    [(set (match_operand:DI 0 "register_operand" "=a")
	  (unspec:DI [(match_operand:DI 1 "register_operand" "0")
		      (match_operand:SI 2 "register_operand" "d")
		      (match_operand:SI 3 "register_operand" "d")]
		     UNSPEC_DPSQ_SA_L_W))
     (set (reg:CCDSP CCDSP_OU_REGNUM)
	  (unspec:CCDSP [(match_dup 1) (match_dup 2) (match_dup 3)]
			UNSPEC_DPSQ_SA_L_W))])]
  "TARGET_DSP && !TARGET_64BIT"
  "dpsq_sa.l.w\t%q0,%2,%3"
  [(set_attr "type"	"imadd")
   (set_attr "mode"	"SI")])

;; MAQ*
(define_insn "mips_maq_s_w_phl"
  [(parallel
    [(set (match_operand:DI 0 "register_operand" "=a")
	  (unspec:DI [(match_operand:DI 1 "register_operand" "0")
		      (match_operand:V2HI 2 "register_operand" "d")
		      (match_operand:V2HI 3 "register_operand" "d")]
		     UNSPEC_MAQ_S_W_PHL))
     (set (reg:CCDSP CCDSP_OU_REGNUM)
	  (unspec:CCDSP [(match_dup 1) (match_dup 2) (match_dup 3)]
			UNSPEC_MAQ_S_W_PHL))])]
  "TARGET_DSP && !TARGET_64BIT"
  "maq_s.w.phl\t%q0,%2,%3"
  [(set_attr "type"	"imadd")
   (set_attr "mode"	"SI")])

(define_insn "mips_maq_s_w_phr"
  [(parallel
    [(set (match_operand:DI 0 "register_operand" "=a")
	  (unspec:DI [(match_operand:DI 1 "register_operand" "0")
		      (match_operand:V2HI 2 "register_operand" "d")
		      (match_operand:V2HI 3 "register_operand" "d")]
		     UNSPEC_MAQ_S_W_PHR))
     (set (reg:CCDSP CCDSP_OU_REGNUM)
	  (unspec:CCDSP [(match_dup 1) (match_dup 2) (match_dup 3)]
			UNSPEC_MAQ_S_W_PHR))])]
  "TARGET_DSP && !TARGET_64BIT"
  "maq_s.w.phr\t%q0,%2,%3"
  [(set_attr "type"	"imadd")
   (set_attr "mode"	"SI")])

;; MAQ_SA*
(define_insn "mips_maq_sa_w_phl"
  [(parallel
    [(set (match_operand:DI 0 "register_operand" "=a")
	  (unspec:DI [(match_operand:DI 1 "register_operand" "0")
		      (match_operand:V2HI 2 "register_operand" "d")
		      (match_operand:V2HI 3 "register_operand" "d")]
		     UNSPEC_MAQ_SA_W_PHL))
     (set (reg:CCDSP CCDSP_OU_REGNUM)
	  (unspec:CCDSP [(match_dup 1) (match_dup 2) (match_dup 3)]
			UNSPEC_MAQ_SA_W_PHL))])]
  "TARGET_DSP && !TARGET_64BIT"
  "maq_sa.w.phl\t%q0,%2,%3"
  [(set_attr "type"	"imadd")
   (set_attr "mode"	"SI")])

(define_insn "mips_maq_sa_w_phr"
  [(parallel
    [(set (match_operand:DI 0 "register_operand" "=a")
	  (unspec:DI [(match_operand:DI 1 "register_operand" "0")
		      (match_operand:V2HI 2 "register_operand" "d")
		      (match_operand:V2HI 3 "register_operand" "d")]
		     UNSPEC_MAQ_SA_W_PHR))
     (set (reg:CCDSP CCDSP_OU_REGNUM)
	  (unspec:CCDSP [(match_dup 1) (match_dup 2) (match_dup 3)]
			UNSPEC_MAQ_SA_W_PHR))])]
  "TARGET_DSP && !TARGET_64BIT"
  "maq_sa.w.phr\t%q0,%2,%3"
  [(set_attr "type"	"imadd")
   (set_attr "mode"	"SI")])

;; Table 2-4. MIPS DSP ASE Instructions: General Bit/Manipulation
;; BITREV
(define_insn "mips_bitrev"
  [(set (match_operand:SI 0 "register_operand" "=d")
	(unspec:SI [(match_operand:SI 1 "register_operand" "d")]
		   UNSPEC_BITREV))]
  "TARGET_DSP"
  "bitrev\t%0,%1"
  [(set_attr "type"	"arith")
   (set_attr "mode"	"SI")])

;; INSV
(define_insn "mips_insv"
  [(set (match_operand:SI 0 "register_operand" "=d")
	(unspec:SI [(match_operand:SI 1 "register_operand" "0")
		    (match_operand:SI 2 "register_operand" "d")
		    (reg:CCDSP CCDSP_SC_REGNUM)
		    (reg:CCDSP CCDSP_PO_REGNUM)]
		   UNSPEC_INSV))]
  "TARGET_DSP"
  "insv\t%0,%2"
  [(set_attr "type"	"arith")
   (set_attr "mode"	"SI")])

;; REPL*
(define_insn "mips_repl_qb"
  [(set (match_operand:V4QI 0 "register_operand" "=d,d")
	(unspec:V4QI [(match_operand:SI 1 "arith_operand" "I,d")]
		     UNSPEC_REPL_QB))]
  "TARGET_DSP"
{
  if (which_alternative == 0)
    {
      if (INTVAL (operands[1]) & ~(unsigned HOST_WIDE_INT) 0xff)
	operands[1] = GEN_INT (INTVAL (operands[1]) & 0xff);
      return "repl.qb\t%0,%1";
    }
  return "replv.qb\t%0,%1";
}
  [(set_attr "type"	"arith")
   (set_attr "mode"	"SI")])

(define_insn "mips_repl_ph"
  [(set (match_operand:V2HI 0 "register_operand" "=d,d")
	(unspec:V2HI [(match_operand:SI 1 "reg_imm10_operand" "YB,d")]
		     UNSPEC_REPL_PH))]
  "TARGET_DSP"
  "@
   repl.ph\t%0,%1
   replv.ph\t%0,%1"
  [(set_attr "type"	"arith")
   (set_attr "mode"	"SI")])

;; Table 2-5. MIPS DSP ASE Instructions: Compare-Pick
;; CMPU.* CMP.*
(define_insn "mips_cmp<DSPV:dspfmt1_1>_eq_<DSPV:dspfmt2>"
  [(set (reg:CCDSP CCDSP_CC_REGNUM)
	(unspec:CCDSP [(match_operand:DSPV 0 "register_operand" "d")
		       (match_operand:DSPV 1 "register_operand" "d")
		       (reg:CCDSP CCDSP_CC_REGNUM)]
		      UNSPEC_CMP_EQ))]
  "TARGET_DSP"
  "cmp<DSPV:dspfmt1_1>.eq.<DSPV:dspfmt2>\t%0,%1"
  [(set_attr "type"	"arith")
   (set_attr "mode"	"SI")])

(define_insn "mips_cmp<DSPV:dspfmt1_1>_lt_<DSPV:dspfmt2>"
  [(set (reg:CCDSP CCDSP_CC_REGNUM)
	(unspec:CCDSP [(match_operand:DSPV 0 "register_operand" "d")
		       (match_operand:DSPV 1 "register_operand" "d")
		       (reg:CCDSP CCDSP_CC_REGNUM)]
		      UNSPEC_CMP_LT))]
  "TARGET_DSP"
  "cmp<DSPV:dspfmt1_1>.lt.<DSPV:dspfmt2>\t%0,%1"
  [(set_attr "type"	"arith")
   (set_attr "mode"	"SI")])

(define_insn "mips_cmp<DSPV:dspfmt1_1>_le_<DSPV:dspfmt2>"
  [(set (reg:CCDSP CCDSP_CC_REGNUM)
	(unspec:CCDSP [(match_operand:DSPV 0 "register_operand" "d")
		       (match_operand:DSPV 1 "register_operand" "d")
		       (reg:CCDSP CCDSP_CC_REGNUM)]
		      UNSPEC_CMP_LE))]
  "TARGET_DSP"
  "cmp<DSPV:dspfmt1_1>.le.<DSPV:dspfmt2>\t%0,%1"
  [(set_attr "type"	"arith")
   (set_attr "mode"	"SI")])

(define_insn "mips_cmpgu_eq_qb"
  [(set (match_operand:SI 0 "register_operand" "=d")
	(unspec:SI [(match_operand:V4QI 1 "register_operand" "d")
		    (match_operand:V4QI 2 "register_operand" "d")]
		   UNSPEC_CMPGU_EQ_QB))]
  "TARGET_DSP"
  "cmpgu.eq.qb\t%0,%1,%2"
  [(set_attr "type"	"arith")
   (set_attr "mode"	"SI")])

(define_insn "mips_cmpgu_lt_qb"
  [(set (match_operand:SI 0 "register_operand" "=d")
	(unspec:SI [(match_operand:V4QI 1 "register_operand" "d")
		    (match_operand:V4QI 2 "register_operand" "d")]
		   UNSPEC_CMPGU_LT_QB))]
  "TARGET_DSP"
  "cmpgu.lt.qb\t%0,%1,%2"
  [(set_attr "type"	"arith")
   (set_attr "mode"	"SI")])

(define_insn "mips_cmpgu_le_qb"
  [(set (match_operand:SI 0 "register_operand" "=d")
	(unspec:SI [(match_operand:V4QI 1 "register_operand" "d")
		    (match_operand:V4QI 2 "register_operand" "d")]
		   UNSPEC_CMPGU_LE_QB))]
  "TARGET_DSP"
  "cmpgu.le.qb\t%0,%1,%2"
  [(set_attr "type"	"arith")
   (set_attr "mode"	"SI")])

;; PICK*
(define_insn "mips_pick_<DSPV:dspfmt2>"
  [(set (match_operand:DSPV 0 "register_operand" "=d")
	(unspec:DSPV [(match_operand:DSPV 1 "register_operand" "d")
		      (match_operand:DSPV 2 "register_operand" "d")
		      (reg:CCDSP CCDSP_CC_REGNUM)]
		     UNSPEC_PICK))]
  "TARGET_DSP"
  "pick.<DSPV:dspfmt2>\t%0,%1,%2"
  [(set_attr "type"	"arith")
   (set_attr "mode"	"SI")])

;; PACKRL*
(define_insn "mips_packrl_ph"
  [(set (match_operand:V2HI 0 "register_operand" "=d")
	(unspec:V2HI [(match_operand:V2HI 1 "register_operand" "d")
		      (match_operand:V2HI 2 "register_operand" "d")]
		     UNSPEC_PACKRL_PH))]
  "TARGET_DSP"
  "packrl.ph\t%0,%1,%2"
  [(set_attr "type"	"arith")
   (set_attr "mode"	"SI")])

;; Table 2-6. MIPS DSP ASE Instructions: Accumulator and DSPControl Access
;; EXTR*
(define_insn "mips_extr_w"
  [(parallel
    [(set (match_operand:SI 0 "register_operand" "=d,d")
	  (unspec:SI [(match_operand:DI 1 "register_operand" "a,a")
		      (match_operand:SI 2 "arith_operand" "I,d")]
		     UNSPEC_EXTR_W))
     (set (reg:CCDSP CCDSP_OU_REGNUM)
	  (unspec:CCDSP [(match_dup 1) (match_dup 2)] UNSPEC_EXTR_W))])]
  "TARGET_DSP && !TARGET_64BIT"
{
  if (which_alternative == 0)
    {
      if (INTVAL (operands[2]) & ~(unsigned HOST_WIDE_INT) 0x1f)
	operands[2] = GEN_INT (INTVAL (operands[2]) & 0x1f);
      return "extr.w\t%0,%q1,%2";
    }
  return "extrv.w\t%0,%q1,%2";
}
  [(set_attr "type"	"mfhilo")
   (set_attr "mode"	"SI")])

(define_insn "mips_extr_r_w"
  [(parallel
    [(set (match_operand:SI 0 "register_operand" "=d,d")
	  (unspec:SI [(match_operand:DI 1 "register_operand" "a,a")
		      (match_operand:SI 2 "arith_operand" "I,d")]
		     UNSPEC_EXTR_R_W))
     (set (reg:CCDSP CCDSP_OU_REGNUM)
	  (unspec:CCDSP [(match_dup 1) (match_dup 2)] UNSPEC_EXTR_R_W))])]
  "TARGET_DSP && !TARGET_64BIT"
{
  if (which_alternative == 0)
    {
      if (INTVAL (operands[2]) & ~(unsigned HOST_WIDE_INT) 0x1f)
	operands[2] = GEN_INT (INTVAL (operands[2]) & 0x1f);
      return "extr_r.w\t%0,%q1,%2";
    }
  return "extrv_r.w\t%0,%q1,%2";
}
  [(set_attr "type"	"mfhilo")
   (set_attr "mode"	"SI")])

(define_insn "mips_extr_rs_w"
  [(parallel
    [(set (match_operand:SI 0 "register_operand" "=d,d")
	  (unspec:SI [(match_operand:DI 1 "register_operand" "a,a")
		      (match_operand:SI 2 "arith_operand" "I,d")]
		     UNSPEC_EXTR_RS_W))
     (set (reg:CCDSP CCDSP_OU_REGNUM)
	  (unspec:CCDSP [(match_dup 1) (match_dup 2)] UNSPEC_EXTR_RS_W))])]
  "TARGET_DSP && !TARGET_64BIT"
{
  if (which_alternative == 0)
    {
      if (INTVAL (operands[2]) & ~(unsigned HOST_WIDE_INT) 0x1f)
	operands[2] = GEN_INT (INTVAL (operands[2]) & 0x1f);
      return "extr_rs.w\t%0,%q1,%2";
    }
  return "extrv_rs.w\t%0,%q1,%2";
}
  [(set_attr "type"	"mfhilo")
   (set_attr "mode"	"SI")])

;; EXTR*_S.H
(define_insn "mips_extr_s_h"
  [(parallel
    [(set (match_operand:SI 0 "register_operand" "=d,d")
	  (unspec:SI [(match_operand:DI 1 "register_operand" "a,a")
		      (match_operand:SI 2 "arith_operand" "I,d")]
		     UNSPEC_EXTR_S_H))
     (set (reg:CCDSP CCDSP_OU_REGNUM)
	  (unspec:CCDSP [(match_dup 1) (match_dup 2)] UNSPEC_EXTR_S_H))])]
  "TARGET_DSP && !TARGET_64BIT"
{
  if (which_alternative == 0)
    {
      if (INTVAL (operands[2]) & ~(unsigned HOST_WIDE_INT) 0x1f)
	operands[2] = GEN_INT (INTVAL (operands[2]) & 0x1f);
      return "extr_s.h\t%0,%q1,%2";
    }
  return "extrv_s.h\t%0,%q1,%2";
}
  [(set_attr "type"	"mfhilo")
   (set_attr "mode"	"SI")])

;; EXTP*
(define_insn "mips_extp"
  [(parallel
    [(set (match_operand:SI 0 "register_operand" "=d,d")
	  (unspec:SI [(match_operand:DI 1 "register_operand" "a,a")
		      (match_operand:SI 2 "arith_operand" "I,d")
		      (reg:CCDSP CCDSP_PO_REGNUM)]
		     UNSPEC_EXTP))
     (set (reg:CCDSP CCDSP_EF_REGNUM)
	  (unspec:CCDSP [(match_dup 1) (match_dup 2)] UNSPEC_EXTP))])]
  "TARGET_DSP && !TARGET_64BIT"
{
  if (which_alternative == 0)
    {
      if (INTVAL (operands[2]) & ~(unsigned HOST_WIDE_INT) 0x1f)
	operands[2] = GEN_INT (INTVAL (operands[2]) & 0x1f);
      return "extp\t%0,%q1,%2";
    }
  return "extpv\t%0,%q1,%2";
}
  [(set_attr "type"	"mfhilo")
   (set_attr "mode"	"SI")])

(define_insn "mips_extpdp"
  [(parallel
    [(set (match_operand:SI 0 "register_operand" "=d,d")
	  (unspec:SI [(match_operand:DI 1 "register_operand" "a,a")
		      (match_operand:SI 2 "arith_operand" "I,d")
		      (reg:CCDSP CCDSP_PO_REGNUM)]
		     UNSPEC_EXTPDP))
     (set (reg:CCDSP CCDSP_PO_REGNUM)
	  (unspec:CCDSP [(match_dup 1) (match_dup 2)
			 (reg:CCDSP CCDSP_PO_REGNUM)] UNSPEC_EXTPDP))
     (set (reg:CCDSP CCDSP_EF_REGNUM)
	  (unspec:CCDSP [(match_dup 1) (match_dup 2)] UNSPEC_EXTPDP))])]
  "TARGET_DSP && !TARGET_64BIT"
{
  if (which_alternative == 0)
    {
      if (INTVAL (operands[2]) & ~(unsigned HOST_WIDE_INT) 0x1f)
	operands[2] = GEN_INT (INTVAL (operands[2]) & 0x1f);
      return "extpdp\t%0,%q1,%2";
    }
  return "extpdpv\t%0,%q1,%2";
}
  [(set_attr "type"	"mfhilo")
   (set_attr "mode"	"SI")])

;; SHILO*
(define_insn "mips_shilo"
  [(set (match_operand:DI 0 "register_operand" "=a,a")
	(unspec:DI [(match_operand:DI 1 "register_operand" "0,0")
		    (match_operand:SI 2 "arith_operand" "I,d")]
		   UNSPEC_SHILO))]
  "TARGET_DSP && !TARGET_64BIT"
{
  if (which_alternative == 0)
    {
      if (INTVAL (operands[2]) < -32 || INTVAL (operands[2]) > 31)
	operands[2] = GEN_INT (INTVAL (operands[2]) & 0x3f);
      return "shilo\t%q0,%2";
    }
  return "shilov\t%q0,%2";
}
  [(set_attr "type"	"mfhilo")
   (set_attr "mode"	"SI")])

;; MTHLIP*
(define_insn "mips_mthlip"
  [(parallel
    [(set (match_operand:DI 0 "register_operand" "=a")
	  (unspec:DI [(match_operand:DI 1 "register_operand" "0")
		      (match_operand:SI 2 "register_operand" "d")
		      (reg:CCDSP CCDSP_PO_REGNUM)]
		     UNSPEC_MTHLIP))
     (set (reg:CCDSP CCDSP_PO_REGNUM)
	  (unspec:CCDSP [(match_dup 1) (match_dup 2)
			 (reg:CCDSP CCDSP_PO_REGNUM)] UNSPEC_MTHLIP))])]
  "TARGET_DSP && !TARGET_64BIT"
  "mthlip\t%2,%q0"
  [(set_attr "type"	"mfhilo")
   (set_attr "mode"	"SI")])

;; WRDSP
(define_insn "mips_wrdsp"
  [(parallel
    [(set (reg:CCDSP CCDSP_PO_REGNUM)
	  (unspec:CCDSP [(match_operand:SI 0 "register_operand" "d")
			 (match_operand:SI 1 "const_uimm6_operand" "YA")]
			 UNSPEC_WRDSP))
     (set (reg:CCDSP CCDSP_SC_REGNUM)
	  (unspec:CCDSP [(match_dup 0) (match_dup 1)] UNSPEC_WRDSP))
     (set (reg:CCDSP CCDSP_CA_REGNUM)
	  (unspec:CCDSP [(match_dup 0) (match_dup 1)] UNSPEC_WRDSP))
     (set (reg:CCDSP CCDSP_OU_REGNUM)
	  (unspec:CCDSP [(match_dup 0) (match_dup 1)] UNSPEC_WRDSP))
     (set (reg:CCDSP CCDSP_CC_REGNUM)
	  (unspec:CCDSP [(match_dup 0) (match_dup 1)] UNSPEC_WRDSP))
     (set (reg:CCDSP CCDSP_EF_REGNUM)
	  (unspec:CCDSP [(match_dup 0) (match_dup 1)] UNSPEC_WRDSP))])]
  "TARGET_DSP"
  "wrdsp\t%0,%1"
  [(set_attr "type"	"arith")
   (set_attr "mode"	"SI")])

;; RDDSP
(define_insn "mips_rddsp"
  [(set (match_operand:SI 0 "register_operand" "=d")
	(unspec:SI [(match_operand:SI 1 "const_uimm6_operand" "YA")
		    (reg:CCDSP CCDSP_PO_REGNUM)
		    (reg:CCDSP CCDSP_SC_REGNUM)
		    (reg:CCDSP CCDSP_CA_REGNUM)
		    (reg:CCDSP CCDSP_OU_REGNUM)
		    (reg:CCDSP CCDSP_CC_REGNUM)
		    (reg:CCDSP CCDSP_EF_REGNUM)]
		   UNSPEC_RDDSP))]
  "TARGET_DSP"
  "rddsp\t%0,%1"
  [(set_attr "type"	"arith")
   (set_attr "mode"	"SI")])

;; Table 2-7. MIPS DSP ASE Instructions: Indexed-Load
;; L*X
(define_insn "mips_lbux"
  [(set (match_operand:SI 0 "register_operand" "=d")
	(zero_extend:SI (mem:QI (plus:SI (match_operand:SI 1
					  "register_operand" "d")
					 (match_operand:SI 2
					  "register_operand" "d")))))]
  "TARGET_DSP"
  "lbux\t%0,%2(%1)"
  [(set_attr "type"	"load")
   (set_attr "mode"	"SI")
   (set_attr "length"	"4")])

(define_insn "mips_lhx"
  [(set (match_operand:SI 0 "register_operand" "=d")
	(sign_extend:SI (mem:HI (plus:SI (match_operand:SI 1
					  "register_operand" "d")
					 (match_operand:SI 2
					  "register_operand" "d")))))]
  "TARGET_DSP"
  "lhx\t%0,%2(%1)"
  [(set_attr "type"	"load")
   (set_attr "mode"	"SI")
   (set_attr "length"	"4")])

(define_insn "mips_lwx"
  [(set (match_operand:SI 0 "register_operand" "=d")
	(mem:SI (plus:SI (match_operand:SI 1 "register_operand" "d")
			 (match_operand:SI 2 "register_operand" "d"))))]
  "TARGET_DSP"
  "lwx\t%0,%2(%1)"
  [(set_attr "type"	"load")
   (set_attr "mode"	"SI")
   (set_attr "length"	"4")])

;; Table 2-8. MIPS DSP ASE Instructions: Branch
;; BPOSGE32
(define_insn "mips_bposge"
  [(set (pc)
	(if_then_else (ge (reg:CCDSP CCDSP_PO_REGNUM)
			  (match_operand:SI 0 "immediate_operand" "I"))
		      (label_ref (match_operand 1 "" ""))
		      (pc)))]
  "TARGET_DSP"
  "%*bposge%0\t%1%/"
  [(set_attr "type"	"branch")
   (set_attr "mode"	"none")])

