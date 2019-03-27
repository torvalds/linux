/* Machine description patterns for PowerPC running Darwin (Mac OS X).
   Copyright (C) 2004, 2005 Free Software Foundation, Inc.
   Contributed by Apple Computer Inc.

This file is part of GCC.

GNU CC is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2, or (at your option)
any later version.

GNU CC is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GNU CC; see the file COPYING.  If not, write to
the Free Software Foundation, 51 Franklin Street, Fifth Floor,
Boston, MA 02110-1301, USA.  */

(define_insn "adddi3_high"
  [(set (match_operand:DI 0 "gpc_reg_operand" "=b")
        (plus:DI (match_operand:DI 1 "gpc_reg_operand" "b")
                 (high:DI (match_operand 2 "" ""))))]
  "TARGET_MACHO && TARGET_64BIT"
  "{cau|addis} %0,%1,ha16(%2)"
  [(set_attr "length" "4")])

(define_insn "movdf_low_si"
  [(set (match_operand:DF 0 "gpc_reg_operand" "=f,!r")
        (mem:DF (lo_sum:SI (match_operand:SI 1 "gpc_reg_operand" "b,b")
                           (match_operand 2 "" ""))))]
  "TARGET_MACHO && TARGET_HARD_FLOAT && TARGET_FPRS && !TARGET_64BIT"
  "*
{
  switch (which_alternative)
    {
      case 0:
	return \"lfd %0,lo16(%2)(%1)\";
      case 1:
	{
	  if (TARGET_POWERPC64 && TARGET_32BIT)
	    /* Note, old assemblers didn't support relocation here.  */
	    return \"ld %0,lo16(%2)(%1)\";
	  else
	    {
	      output_asm_insn (\"{cal|la} %0,lo16(%2)(%1)\", operands);
	      output_asm_insn (\"{l|lwz} %L0,4(%0)\", operands);
	      return (\"{l|lwz} %0,0(%0)\");
	    }
	}
      default:
	gcc_unreachable ();
    }
}"
  [(set_attr "type" "load")
   (set_attr "length" "4,12")])


(define_insn "movdf_low_di"
  [(set (match_operand:DF 0 "gpc_reg_operand" "=f,!r")
        (mem:DF (lo_sum:DI (match_operand:DI 1 "gpc_reg_operand" "b,b")
                           (match_operand 2 "" ""))))]
  "TARGET_MACHO && TARGET_HARD_FLOAT && TARGET_FPRS && TARGET_64BIT"
  "*
{
  switch (which_alternative)
    {
      case 0:
	return \"lfd %0,lo16(%2)(%1)\";
      case 1:
	return \"ld %0,lo16(%2)(%1)\";
      default:
	gcc_unreachable ();
    }
}"
  [(set_attr "type" "load")
   (set_attr "length" "4,4")])

(define_insn "movdf_low_st_si"
  [(set (mem:DF (lo_sum:SI (match_operand:SI 1 "gpc_reg_operand" "b")
                           (match_operand 2 "" "")))
	(match_operand:DF 0 "gpc_reg_operand" "f"))]
  "TARGET_MACHO && TARGET_HARD_FLOAT && TARGET_FPRS && ! TARGET_64BIT"
  "stfd %0,lo16(%2)(%1)"
  [(set_attr "type" "store")
   (set_attr "length" "4")])

(define_insn "movdf_low_st_di"
  [(set (mem:DF (lo_sum:DI (match_operand:DI 1 "gpc_reg_operand" "b")
                           (match_operand 2 "" "")))
	(match_operand:DF 0 "gpc_reg_operand" "f"))]
  "TARGET_MACHO && TARGET_HARD_FLOAT && TARGET_FPRS && TARGET_64BIT"
  "stfd %0,lo16(%2)(%1)"
  [(set_attr "type" "store")
   (set_attr "length" "4")])

(define_insn "movsf_low_si"
  [(set (match_operand:SF 0 "gpc_reg_operand" "=f,!r")
        (mem:SF (lo_sum:SI (match_operand:SI 1 "gpc_reg_operand" "b,b")
                           (match_operand 2 "" ""))))]
  "TARGET_MACHO && TARGET_HARD_FLOAT && TARGET_FPRS && ! TARGET_64BIT"
  "@
   lfs %0,lo16(%2)(%1)
   {l|lwz} %0,lo16(%2)(%1)"
  [(set_attr "type" "load")
   (set_attr "length" "4")])

(define_insn "movsf_low_di"
  [(set (match_operand:SF 0 "gpc_reg_operand" "=f,!r")
        (mem:SF (lo_sum:DI (match_operand:DI 1 "gpc_reg_operand" "b,b")
                           (match_operand 2 "" ""))))]
  "TARGET_MACHO && TARGET_HARD_FLOAT && TARGET_FPRS && TARGET_64BIT"
  "@
   lfs %0,lo16(%2)(%1)
   {l|lwz} %0,lo16(%2)(%1)"
  [(set_attr "type" "load")
   (set_attr "length" "4")])

(define_insn "movsf_low_st_si"
  [(set (mem:SF (lo_sum:SI (match_operand:SI 1 "gpc_reg_operand" "b,b")
                           (match_operand 2 "" "")))
	(match_operand:SF 0 "gpc_reg_operand" "f,!r"))]
  "TARGET_MACHO && TARGET_HARD_FLOAT && TARGET_FPRS && ! TARGET_64BIT"
  "@
   stfs %0,lo16(%2)(%1)
   {st|stw} %0,lo16(%2)(%1)"
  [(set_attr "type" "store")
   (set_attr "length" "4")])

(define_insn "movsf_low_st_di"
  [(set (mem:SF (lo_sum:DI (match_operand:DI 1 "gpc_reg_operand" "b,b")
                           (match_operand 2 "" "")))
	(match_operand:SF 0 "gpc_reg_operand" "f,!r"))]
  "TARGET_MACHO && TARGET_HARD_FLOAT && TARGET_FPRS && TARGET_64BIT"
  "@
   stfs %0,lo16(%2)(%1)
   {st|stw} %0,lo16(%2)(%1)"
  [(set_attr "type" "store")
   (set_attr "length" "4")])

;; 64-bit MachO load/store support
(define_insn "movdi_low"
  [(set (match_operand:DI 0 "gpc_reg_operand" "=r")
        (mem:DI (lo_sum:DI (match_operand:DI 1 "gpc_reg_operand" "b")
                           (match_operand 2 "" ""))))]
  "TARGET_MACHO && TARGET_64BIT"
  "{l|ld} %0,lo16(%2)(%1)"
  [(set_attr "type" "load")
   (set_attr "length" "4")])

(define_insn "movsi_low_st"
  [(set (mem:SI (lo_sum:SI (match_operand:SI 1 "gpc_reg_operand" "b")
                           (match_operand 2 "" "")))
	(match_operand:SI 0 "gpc_reg_operand" "r"))]
  "TARGET_MACHO && ! TARGET_64BIT"
  "{st|stw} %0,lo16(%2)(%1)"
  [(set_attr "type" "store")
   (set_attr "length" "4")])

(define_insn "movdi_low_st"
  [(set (mem:DI (lo_sum:DI (match_operand:DI 1 "gpc_reg_operand" "b")
                           (match_operand 2 "" "")))
	(match_operand:DI 0 "gpc_reg_operand" "r"))]
  "TARGET_MACHO && TARGET_64BIT"
  "{st|std} %0,lo16(%2)(%1)"
  [(set_attr "type" "store")
   (set_attr "length" "4")])

;; Mach-O PIC trickery.
(define_expand "macho_high"
  [(set (match_operand 0 "" "")
	(high (match_operand 1 "" "")))]
  "TARGET_MACHO"
{
  if (TARGET_64BIT)
    emit_insn (gen_macho_high_di (operands[0], operands[1]));
  else
    emit_insn (gen_macho_high_si (operands[0], operands[1]));

  DONE;
})

(define_insn "macho_high_si"
  [(set (match_operand:SI 0 "gpc_reg_operand" "=b*r")
	(high:SI (match_operand 1 "" "")))]
  "TARGET_MACHO && ! TARGET_64BIT"
  "{liu|lis} %0,ha16(%1)")
  

(define_insn "macho_high_di"
  [(set (match_operand:DI 0 "gpc_reg_operand" "=b*r")
	(high:DI (match_operand 1 "" "")))]
  "TARGET_MACHO && TARGET_64BIT"
  "{liu|lis} %0,ha16(%1)")

(define_expand "macho_low"
  [(set (match_operand 0 "" "")
	(lo_sum (match_operand 1 "" "")
		   (match_operand 2 "" "")))]
   "TARGET_MACHO"
{
  if (TARGET_64BIT)
    emit_insn (gen_macho_low_di (operands[0], operands[1], operands[2]));
  else
    emit_insn (gen_macho_low_si (operands[0], operands[1], operands[2]));

  DONE;
})

(define_insn "macho_low_si"
  [(set (match_operand:SI 0 "gpc_reg_operand" "=r,r")
	(lo_sum:SI (match_operand:SI 1 "gpc_reg_operand" "b,!*r")
		   (match_operand 2 "" "")))]
   "TARGET_MACHO && ! TARGET_64BIT"
   "@
    {cal %0,%a2@l(%1)|la %0,lo16(%2)(%1)}
    {cal %0,%a2@l(%1)|addic %0,%1,lo16(%2)}")

(define_insn "macho_low_di"
  [(set (match_operand:DI 0 "gpc_reg_operand" "=r,r")
	(lo_sum:DI (match_operand:DI 1 "gpc_reg_operand" "b,!*r")
		   (match_operand 2 "" "")))]
   "TARGET_MACHO && TARGET_64BIT"
   "@
    {cal %0,%a2@l(%1)|la %0,lo16(%2)(%1)}
    {cal %0,%a2@l(%1)|addic %0,%1,lo16(%2)}")

(define_split
  [(set (mem:V4SI (plus:DI (match_operand:DI 0 "gpc_reg_operand" "")
			 (match_operand:DI 1 "short_cint_operand" "")))
	(match_operand:V4SI 2 "register_operand" ""))
   (clobber (match_operand:DI 3 "gpc_reg_operand" ""))]
  "TARGET_MACHO && TARGET_64BIT"
  [(set (match_dup 3) (plus:DI (match_dup 0) (match_dup 1)))
   (set (mem:V4SI (match_dup 3))
	(match_dup 2))]
  "")

(define_expand "load_macho_picbase"
  [(set (match_operand 0 "" "")
        (unspec [(match_operand 1 "" "")]
                   UNSPEC_LD_MPIC))]
  "(DEFAULT_ABI == ABI_DARWIN) && flag_pic"
{
  if (TARGET_32BIT)
    emit_insn (gen_load_macho_picbase_si (operands[0], operands[1]));
  else
    emit_insn (gen_load_macho_picbase_di (operands[0], operands[1]));

  DONE;
})

(define_insn "load_macho_picbase_si"
  [(set (match_operand:SI 0 "register_operand" "=l")
	(unspec:SI [(match_operand:SI 1 "immediate_operand" "s")
		    (pc)] UNSPEC_LD_MPIC))]
  "(DEFAULT_ABI == ABI_DARWIN) && flag_pic"
  "bcl 20,31,%1\\n%1:"
  [(set_attr "type" "branch")
   (set_attr "length" "4")])

(define_insn "load_macho_picbase_di"
  [(set (match_operand:DI 0 "register_operand" "=l")
	(unspec:DI [(match_operand:DI 1 "immediate_operand" "s")
		    (pc)] UNSPEC_LD_MPIC))]
  "(DEFAULT_ABI == ABI_DARWIN) && flag_pic && TARGET_64BIT"
  "bcl 20,31,%1\\n%1:"
  [(set_attr "type" "branch")
   (set_attr "length" "4")])

(define_expand "macho_correct_pic"
  [(set (match_operand 0 "" "")
	(plus (match_operand 1 "" "")
		 (unspec [(match_operand 2 "" "")
			     (match_operand 3 "" "")]
			    UNSPEC_MPIC_CORRECT)))]
  "DEFAULT_ABI == ABI_DARWIN"
{
  if (TARGET_32BIT)
    emit_insn (gen_macho_correct_pic_si (operands[0], operands[1], operands[2],
	       operands[3]));
  else
    emit_insn (gen_macho_correct_pic_di (operands[0], operands[1], operands[2],
	       operands[3]));

  DONE;
})

(define_insn "macho_correct_pic_si"
  [(set (match_operand:SI 0 "gpc_reg_operand" "=r")
	(plus:SI (match_operand:SI 1 "gpc_reg_operand" "r")
		 (unspec:SI [(match_operand:SI 2 "immediate_operand" "s")
			     (match_operand:SI 3 "immediate_operand" "s")]
			    UNSPEC_MPIC_CORRECT)))]
  "DEFAULT_ABI == ABI_DARWIN"
  "addis %0,%1,ha16(%2-%3)\n\taddi %0,%0,lo16(%2-%3)"
  [(set_attr "length" "8")])

(define_insn "macho_correct_pic_di"
  [(set (match_operand:DI 0 "gpc_reg_operand" "=r")
	(plus:DI (match_operand:DI 1 "gpc_reg_operand" "r")
		 (unspec:DI [(match_operand:DI 2 "immediate_operand" "s")
			     (match_operand:DI 3 "immediate_operand" "s")]
			    16)))]
  "DEFAULT_ABI == ABI_DARWIN && TARGET_64BIT"
  "addis %0,%1,ha16(%2-%3)\n\taddi %0,%0,lo16(%2-%3)"
  [(set_attr "length" "8")])

(define_insn "*call_indirect_nonlocal_darwin64"
  [(call (mem:SI (match_operand:DI 0 "register_operand" "c,*l,c,*l"))
	 (match_operand 1 "" "g,g,g,g"))
   (use (match_operand:SI 2 "immediate_operand" "O,O,n,n"))
   (clobber (match_scratch:SI 3 "=l,l,l,l"))]
  "DEFAULT_ABI == ABI_DARWIN && TARGET_64BIT"
{
  return "b%T0l";
}
  [(set_attr "type" "jmpreg,jmpreg,jmpreg,jmpreg")
   (set_attr "length" "4,4,8,8")])

(define_insn "*call_nonlocal_darwin64"
  [(call (mem:SI (match_operand:DI 0 "symbol_ref_operand" "s,s"))
	 (match_operand 1 "" "g,g"))
   (use (match_operand:SI 2 "immediate_operand" "O,n"))
   (clobber (match_scratch:SI 3 "=l,l"))]
  "(DEFAULT_ABI == ABI_DARWIN)
   && (INTVAL (operands[2]) & CALL_LONG) == 0"
{
#if TARGET_MACHO
  return output_call(insn, operands, 0, 2);
#else
  gcc_unreachable ();
#endif
}
  [(set_attr "type" "branch,branch")
   (set_attr "length" "4,8")])

(define_insn "*call_value_indirect_nonlocal_darwin64"
  [(set (match_operand 0 "" "")
	(call (mem:SI (match_operand:DI 1 "register_operand" "c,*l,c,*l"))
	      (match_operand 2 "" "g,g,g,g")))
   (use (match_operand:SI 3 "immediate_operand" "O,O,n,n"))
   (clobber (match_scratch:SI 4 "=l,l,l,l"))]
  "DEFAULT_ABI == ABI_DARWIN"
{
  return "b%T1l";
}
  [(set_attr "type" "jmpreg,jmpreg,jmpreg,jmpreg")
   (set_attr "length" "4,4,8,8")])

(define_insn "*call_value_nonlocal_darwin64"
  [(set (match_operand 0 "" "")
	(call (mem:SI (match_operand:DI 1 "symbol_ref_operand" "s,s"))
	      (match_operand 2 "" "g,g")))
   (use (match_operand:SI 3 "immediate_operand" "O,n"))
   (clobber (match_scratch:SI 4 "=l,l"))]
  "(DEFAULT_ABI == ABI_DARWIN)
   && (INTVAL (operands[3]) & CALL_LONG) == 0"
{
#if TARGET_MACHO
  return output_call(insn, operands, 1, 3);
#else
  gcc_unreachable ();
#endif
}
  [(set_attr "type" "branch,branch")
   (set_attr "length" "4,8")])

(define_insn "*sibcall_nonlocal_darwin64"
  [(call (mem:SI (match_operand:DI 0 "symbol_ref_operand" "s,s"))
	 (match_operand 1 "" ""))
   (use (match_operand 2 "immediate_operand" "O,n"))
   (use (match_operand:SI 3 "register_operand" "l,l"))
   (return)]
  "(DEFAULT_ABI == ABI_DARWIN)
   && (INTVAL (operands[2]) & CALL_LONG) == 0"
{
  return "b %z0";
}
  [(set_attr "type" "branch,branch")
   (set_attr "length" "4,8")])

(define_insn "*sibcall_value_nonlocal_darwin64"
  [(set (match_operand 0 "" "")
	(call (mem:SI (match_operand:DI 1 "symbol_ref_operand" "s,s"))
	      (match_operand 2 "" "")))
   (use (match_operand:SI 3 "immediate_operand" "O,n"))
   (use (match_operand:SI 4 "register_operand" "l,l"))
   (return)]
  "(DEFAULT_ABI == ABI_DARWIN)
   && (INTVAL (operands[3]) & CALL_LONG) == 0"
  "*
{
  return \"b %z1\";
}"
  [(set_attr "type" "branch,branch")
   (set_attr "length" "4,8")])


(define_insn "*sibcall_symbolic_64"
  [(call (mem:SI (match_operand:DI 0 "call_operand" "s,c")) ; 64
	 (match_operand 1 "" ""))
   (use (match_operand 2 "" ""))
   (use (match_operand:SI 3 "register_operand" "l,l"))
   (return)]
  "TARGET_64BIT && DEFAULT_ABI == ABI_DARWIN"
  "*
{
  switch (which_alternative)
    {
      case 0:  return \"b %z0\";
      case 1:  return \"b%T0\";
      default:  gcc_unreachable ();
    }
}"
  [(set_attr "type" "branch")
   (set_attr "length" "4")])

(define_insn "*sibcall_value_symbolic_64"
  [(set (match_operand 0 "" "")
	(call (mem:SI (match_operand:DI 1 "call_operand" "s,c"))
	      (match_operand 2 "" "")))
   (use (match_operand:SI 3 "" ""))
   (use (match_operand:SI 4 "register_operand" "l,l"))
   (return)]
  "TARGET_64BIT && DEFAULT_ABI == ABI_DARWIN"
  "*
{
  switch (which_alternative)
    {
      case 0:  return \"b %z1\";
      case 1:  return \"b%T1\";
      default:  gcc_unreachable ();
    }
}"
  [(set_attr "type" "branch")
   (set_attr "length" "4")])

