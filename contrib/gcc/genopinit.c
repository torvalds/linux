/* Generate code to initialize optabs from machine description.
   Copyright (C) 1993, 1994, 1995, 1996, 1997, 1998, 1999, 2000,
   2001, 2002, 2003, 2004, 2005, 2006 Free Software Foundation, Inc.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation; either version 2, or (at your option) any later
version.

GCC is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to the Free
Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA
02110-1301, USA.  */


#include "bconfig.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "rtl.h"
#include "errors.h"
#include "gensupport.h"


/* Many parts of GCC use arrays that are indexed by machine mode and
   contain the insn codes for pattern in the MD file that perform a given
   operation on operands of that mode.

   These patterns are present in the MD file with names that contain
   the mode(s) used and the name of the operation.  This program
   writes a function `init_all_optabs' that initializes the optabs with
   all the insn codes of the relevant patterns present in the MD file.

   This array contains a list of optabs that need to be initialized.  Within
   each string, the name of the pattern to be matched against is delimited
   with $( and $).  In the string, $a and $b are used to match a short mode
   name (the part of the mode name not including `mode' and converted to
   lower-case).  When writing out the initializer, the entire string is
   used.  $A and $B are replaced with the full name of the mode; $a and $b
   are replaced with the short form of the name, as above.

   If $N is present in the pattern, it means the two modes must be consecutive
   widths in the same mode class (e.g, QImode and HImode).  $I means that
   only full integer modes should be considered for the next mode, and $F
   means that only float modes should be considered.
   $P means that both full and partial integer modes should be considered.

   $V means to emit 'v' if the first mode is a MODE_FLOAT mode.

   For some optabs, we store the operation by RTL codes.  These are only
   used for comparisons.  In that case, $c and $C are the lower-case and
   upper-case forms of the comparison, respectively.  */

static const char * const optabs[] =
{ "sext_optab->handlers[$B][$A].insn_code = CODE_FOR_$(extend$a$b2$)",
  "zext_optab->handlers[$B][$A].insn_code = CODE_FOR_$(zero_extend$a$b2$)",
  "sfix_optab->handlers[$B][$A].insn_code = CODE_FOR_$(fix$F$a$I$b2$)",
  "ufix_optab->handlers[$B][$A].insn_code = CODE_FOR_$(fixuns$F$a$b2$)",
  "sfixtrunc_optab->handlers[$B][$A].insn_code = CODE_FOR_$(fix_trunc$F$a$I$b2$)",
  "ufixtrunc_optab->handlers[$B][$A].insn_code = CODE_FOR_$(fixuns_trunc$F$a$I$b2$)",
  "sfloat_optab->handlers[$B][$A].insn_code = CODE_FOR_$(float$I$a$F$b2$)",
  "ufloat_optab->handlers[$B][$A].insn_code = CODE_FOR_$(floatuns$I$a$F$b2$)",
  "trunc_optab->handlers[$B][$A].insn_code = CODE_FOR_$(trunc$a$b2$)",
  "add_optab->handlers[$A].insn_code = CODE_FOR_$(add$P$a3$)",
  "addv_optab->handlers[$A].insn_code =\n\
    add_optab->handlers[$A].insn_code = CODE_FOR_$(add$F$a3$)",
  "addv_optab->handlers[$A].insn_code = CODE_FOR_$(addv$I$a3$)",
  "sub_optab->handlers[$A].insn_code = CODE_FOR_$(sub$P$a3$)",
  "subv_optab->handlers[$A].insn_code =\n\
    sub_optab->handlers[$A].insn_code = CODE_FOR_$(sub$F$a3$)",
  "subv_optab->handlers[$A].insn_code = CODE_FOR_$(subv$I$a3$)",
  "smul_optab->handlers[$A].insn_code = CODE_FOR_$(mul$P$a3$)",
  "smulv_optab->handlers[$A].insn_code =\n\
    smul_optab->handlers[$A].insn_code = CODE_FOR_$(mul$F$a3$)",
  "smulv_optab->handlers[$A].insn_code = CODE_FOR_$(mulv$I$a3$)",
  "umul_highpart_optab->handlers[$A].insn_code = CODE_FOR_$(umul$a3_highpart$)",
  "smul_highpart_optab->handlers[$A].insn_code = CODE_FOR_$(smul$a3_highpart$)",
  "smul_widen_optab->handlers[$B].insn_code = CODE_FOR_$(mul$a$b3$)$N",
  "umul_widen_optab->handlers[$B].insn_code = CODE_FOR_$(umul$a$b3$)$N",
  "usmul_widen_optab->handlers[$B].insn_code = CODE_FOR_$(usmul$a$b3$)$N",
  "sdiv_optab->handlers[$A].insn_code = CODE_FOR_$(div$a3$)",
  "sdivv_optab->handlers[$A].insn_code = CODE_FOR_$(div$V$I$a3$)",
  "udiv_optab->handlers[$A].insn_code = CODE_FOR_$(udiv$I$a3$)",
  "sdivmod_optab->handlers[$A].insn_code = CODE_FOR_$(divmod$a4$)",
  "udivmod_optab->handlers[$A].insn_code = CODE_FOR_$(udivmod$a4$)",
  "smod_optab->handlers[$A].insn_code = CODE_FOR_$(mod$a3$)",
  "umod_optab->handlers[$A].insn_code = CODE_FOR_$(umod$a3$)",
  "fmod_optab->handlers[$A].insn_code = CODE_FOR_$(fmod$a3$)",
  "drem_optab->handlers[$A].insn_code = CODE_FOR_$(drem$a3$)",
  "ftrunc_optab->handlers[$A].insn_code = CODE_FOR_$(ftrunc$F$a2$)",
  "and_optab->handlers[$A].insn_code = CODE_FOR_$(and$a3$)",
  "ior_optab->handlers[$A].insn_code = CODE_FOR_$(ior$a3$)",
  "xor_optab->handlers[$A].insn_code = CODE_FOR_$(xor$a3$)",
  "ashl_optab->handlers[$A].insn_code = CODE_FOR_$(ashl$a3$)",
  "ashr_optab->handlers[$A].insn_code = CODE_FOR_$(ashr$a3$)",
  "lshr_optab->handlers[$A].insn_code = CODE_FOR_$(lshr$a3$)",
  "rotl_optab->handlers[$A].insn_code = CODE_FOR_$(rotl$a3$)",
  "rotr_optab->handlers[$A].insn_code = CODE_FOR_$(rotr$a3$)",
  "smin_optab->handlers[$A].insn_code = CODE_FOR_$(smin$a3$)",
  "smax_optab->handlers[$A].insn_code = CODE_FOR_$(smax$a3$)",
  "umin_optab->handlers[$A].insn_code = CODE_FOR_$(umin$I$a3$)",
  "umax_optab->handlers[$A].insn_code = CODE_FOR_$(umax$I$a3$)",
  "pow_optab->handlers[$A].insn_code = CODE_FOR_$(pow$a3$)",
  "atan2_optab->handlers[$A].insn_code = CODE_FOR_$(atan2$a3$)",
  "neg_optab->handlers[$A].insn_code = CODE_FOR_$(neg$P$a2$)",
  "negv_optab->handlers[$A].insn_code =\n\
    neg_optab->handlers[$A].insn_code = CODE_FOR_$(neg$F$a2$)",
  "negv_optab->handlers[$A].insn_code = CODE_FOR_$(negv$I$a2$)",
  "abs_optab->handlers[$A].insn_code = CODE_FOR_$(abs$P$a2$)",
  "absv_optab->handlers[$A].insn_code =\n\
    abs_optab->handlers[$A].insn_code = CODE_FOR_$(abs$F$a2$)",
  "absv_optab->handlers[$A].insn_code = CODE_FOR_$(absv$I$a2$)",
  "copysign_optab->handlers[$A].insn_code = CODE_FOR_$(copysign$F$a3$)",
  "sqrt_optab->handlers[$A].insn_code = CODE_FOR_$(sqrt$a2$)",
  "floor_optab->handlers[$A].insn_code = CODE_FOR_$(floor$a2$)",
  "lfloor_optab->handlers[$A].insn_code = CODE_FOR_$(lfloor$a2$)",
  "ceil_optab->handlers[$A].insn_code = CODE_FOR_$(ceil$a2$)",
  "lceil_optab->handlers[$A].insn_code = CODE_FOR_$(lceil$a2$)",
  "round_optab->handlers[$A].insn_code = CODE_FOR_$(round$a2$)",
  "btrunc_optab->handlers[$A].insn_code = CODE_FOR_$(btrunc$a2$)",
  "nearbyint_optab->handlers[$A].insn_code = CODE_FOR_$(nearbyint$a2$)",
  "rint_optab->handlers[$A].insn_code = CODE_FOR_$(rint$a2$)",
  "lrint_optab->handlers[$A].insn_code = CODE_FOR_$(lrint$a2$)",
  "sincos_optab->handlers[$A].insn_code = CODE_FOR_$(sincos$a3$)",
  "sin_optab->handlers[$A].insn_code = CODE_FOR_$(sin$a2$)",
  "asin_optab->handlers[$A].insn_code = CODE_FOR_$(asin$a2$)",
  "cos_optab->handlers[$A].insn_code = CODE_FOR_$(cos$a2$)",
  "acos_optab->handlers[$A].insn_code = CODE_FOR_$(acos$a2$)",
  "exp_optab->handlers[$A].insn_code = CODE_FOR_$(exp$a2$)",
  "exp10_optab->handlers[$A].insn_code = CODE_FOR_$(exp10$a2$)",
  "exp2_optab->handlers[$A].insn_code = CODE_FOR_$(exp2$a2$)",
  "expm1_optab->handlers[$A].insn_code = CODE_FOR_$(expm1$a2$)",
  "ldexp_optab->handlers[$A].insn_code = CODE_FOR_$(ldexp$a3$)",
  "logb_optab->handlers[$A].insn_code = CODE_FOR_$(logb$a2$)",
  "ilogb_optab->handlers[$A].insn_code = CODE_FOR_$(ilogb$a2$)",
  "log_optab->handlers[$A].insn_code = CODE_FOR_$(log$a2$)",
  "log10_optab->handlers[$A].insn_code = CODE_FOR_$(log10$a2$)",  
  "log2_optab->handlers[$A].insn_code = CODE_FOR_$(log2$a2$)",  
  "log1p_optab->handlers[$A].insn_code = CODE_FOR_$(log1p$a2$)",  
  "tan_optab->handlers[$A].insn_code = CODE_FOR_$(tan$a2$)",
  "atan_optab->handlers[$A].insn_code = CODE_FOR_$(atan$a2$)",
  "strlen_optab->handlers[$A].insn_code = CODE_FOR_$(strlen$a$)",
  "one_cmpl_optab->handlers[$A].insn_code = CODE_FOR_$(one_cmpl$a2$)",
  "bswap_optab->handlers[$A].insn_code = CODE_FOR_$(bswap$a2$)",
  "ffs_optab->handlers[$A].insn_code = CODE_FOR_$(ffs$a2$)",
  "clz_optab->handlers[$A].insn_code = CODE_FOR_$(clz$a2$)",
  "ctz_optab->handlers[$A].insn_code = CODE_FOR_$(ctz$a2$)",
  "popcount_optab->handlers[$A].insn_code = CODE_FOR_$(popcount$a2$)",
  "parity_optab->handlers[$A].insn_code = CODE_FOR_$(parity$a2$)",
  "mov_optab->handlers[$A].insn_code = CODE_FOR_$(mov$a$)",
  "movstrict_optab->handlers[$A].insn_code = CODE_FOR_$(movstrict$a$)",
  "movmisalign_optab->handlers[$A].insn_code = CODE_FOR_$(movmisalign$a$)",
  "cmp_optab->handlers[$A].insn_code = CODE_FOR_$(cmp$a$)",
  "tst_optab->handlers[$A].insn_code = CODE_FOR_$(tst$a$)",
  "addcc_optab->handlers[$A].insn_code = CODE_FOR_$(add$acc$)",
  "bcc_gen_fctn[$C] = gen_$(b$c$)",
  "setcc_gen_code[$C] = CODE_FOR_$(s$c$)",
  "movcc_gen_code[$A] = CODE_FOR_$(mov$acc$)",
  "cbranch_optab->handlers[$A].insn_code = CODE_FOR_$(cbranch$a4$)",
  "cmov_optab->handlers[$A].insn_code = CODE_FOR_$(cmov$a6$)",
  "cstore_optab->handlers[$A].insn_code = CODE_FOR_$(cstore$a4$)",
  "push_optab->handlers[$A].insn_code = CODE_FOR_$(push$a1$)",
  "reload_in_optab[$A] = CODE_FOR_$(reload_in$a$)",
  "reload_out_optab[$A] = CODE_FOR_$(reload_out$a$)",
  "movmem_optab[$A] = CODE_FOR_$(movmem$a$)",
  "cmpstr_optab[$A] = CODE_FOR_$(cmpstr$a$)",
  "cmpstrn_optab[$A] = CODE_FOR_$(cmpstrn$a$)",
  "cmpmem_optab[$A] = CODE_FOR_$(cmpmem$a$)",
  "setmem_optab[$A] = CODE_FOR_$(setmem$a$)",
  "sync_add_optab[$A] = CODE_FOR_$(sync_add$I$a$)",
  "sync_sub_optab[$A] = CODE_FOR_$(sync_sub$I$a$)",
  "sync_ior_optab[$A] = CODE_FOR_$(sync_ior$I$a$)",
  "sync_and_optab[$A] = CODE_FOR_$(sync_and$I$a$)",
  "sync_xor_optab[$A] = CODE_FOR_$(sync_xor$I$a$)",
  "sync_nand_optab[$A] = CODE_FOR_$(sync_nand$I$a$)",
  "sync_old_add_optab[$A] = CODE_FOR_$(sync_old_add$I$a$)",
  "sync_old_sub_optab[$A] = CODE_FOR_$(sync_old_sub$I$a$)",
  "sync_old_ior_optab[$A] = CODE_FOR_$(sync_old_ior$I$a$)",
  "sync_old_and_optab[$A] = CODE_FOR_$(sync_old_and$I$a$)",
  "sync_old_xor_optab[$A] = CODE_FOR_$(sync_old_xor$I$a$)",
  "sync_old_nand_optab[$A] = CODE_FOR_$(sync_old_nand$I$a$)",
  "sync_new_add_optab[$A] = CODE_FOR_$(sync_new_add$I$a$)",
  "sync_new_sub_optab[$A] = CODE_FOR_$(sync_new_sub$I$a$)",
  "sync_new_ior_optab[$A] = CODE_FOR_$(sync_new_ior$I$a$)",
  "sync_new_and_optab[$A] = CODE_FOR_$(sync_new_and$I$a$)",
  "sync_new_xor_optab[$A] = CODE_FOR_$(sync_new_xor$I$a$)",
  "sync_new_nand_optab[$A] = CODE_FOR_$(sync_new_nand$I$a$)",
  "sync_compare_and_swap[$A] = CODE_FOR_$(sync_compare_and_swap$I$a$)",
  "sync_compare_and_swap_cc[$A] = CODE_FOR_$(sync_compare_and_swap_cc$I$a$)",
  "sync_lock_test_and_set[$A] = CODE_FOR_$(sync_lock_test_and_set$I$a$)",
  "sync_lock_release[$A] = CODE_FOR_$(sync_lock_release$I$a$)",
  "vec_set_optab->handlers[$A].insn_code = CODE_FOR_$(vec_set$a$)",
  "vec_extract_optab->handlers[$A].insn_code = CODE_FOR_$(vec_extract$a$)",
  "vec_init_optab->handlers[$A].insn_code = CODE_FOR_$(vec_init$a$)",
  "vec_shl_optab->handlers[$A].insn_code = CODE_FOR_$(vec_shl_$a$)",
  "vec_shr_optab->handlers[$A].insn_code = CODE_FOR_$(vec_shr_$a$)",
  "vec_realign_load_optab->handlers[$A].insn_code = CODE_FOR_$(vec_realign_load_$a$)",
  "vcond_gen_code[$A] = CODE_FOR_$(vcond$a$)",
  "vcondu_gen_code[$A] = CODE_FOR_$(vcondu$a$)",
  "ssum_widen_optab->handlers[$A].insn_code = CODE_FOR_$(widen_ssum$I$a3$)",
  "usum_widen_optab->handlers[$A].insn_code = CODE_FOR_$(widen_usum$I$a3$)",
  "udot_prod_optab->handlers[$A].insn_code = CODE_FOR_$(udot_prod$I$a$)",
  "sdot_prod_optab->handlers[$A].insn_code = CODE_FOR_$(sdot_prod$I$a$)",
  "reduc_smax_optab->handlers[$A].insn_code = CODE_FOR_$(reduc_smax_$a$)",
  "reduc_umax_optab->handlers[$A].insn_code = CODE_FOR_$(reduc_umax_$a$)",
  "reduc_smin_optab->handlers[$A].insn_code = CODE_FOR_$(reduc_smin_$a$)",
  "reduc_umin_optab->handlers[$A].insn_code = CODE_FOR_$(reduc_umin_$a$)",
  "reduc_splus_optab->handlers[$A].insn_code = CODE_FOR_$(reduc_splus_$a$)" ,
  "reduc_uplus_optab->handlers[$A].insn_code = CODE_FOR_$(reduc_uplus_$a$)" 
};

static void gen_insn (rtx);

static void
gen_insn (rtx insn)
{
  const char *name = XSTR (insn, 0);
  int m1 = 0, m2 = 0, op = 0;
  size_t pindex;
  int i;
  const char *np, *pp, *p, *q;

  /* Don't mention instructions whose names are the null string.
     They are in the machine description just to be recognized.  */
  if (*name == 0)
    return;

  /* See if NAME matches one of the patterns we have for the optabs we know
     about.  */

  for (pindex = 0; pindex < ARRAY_SIZE (optabs); pindex++)
    {
      int force_float = 0, force_int = 0, force_partial_int = 0;
      int force_consec = 0;
      int matches = 1;

      for (pp = optabs[pindex]; pp[0] != '$' || pp[1] != '('; pp++)
	;

      for (pp += 2, np = name; matches && ! (pp[0] == '$' && pp[1] == ')');
	   pp++)
	{
	  if (*pp != '$')
	    {
	      if (*pp != *np++)
		break;
	    }
	  else
	    switch (*++pp)
	      {
	      case 'N':
		force_consec = 1;
		break;
	      case 'I':
		force_int = 1;
		break;
	      case 'P':
                force_partial_int = 1;
                break;
	      case 'F':
		force_float = 1;
		break;
	      case 'V':
                break;
	      case 'c':
		for (op = 0; op < NUM_RTX_CODE; op++)
		  {
		    for (p = GET_RTX_NAME(op), q = np; *p; p++, q++)
		      if (*p != *q)
			break;

		    /* We have to be concerned about matching "gt" and
		       missing "gtu", e.g., so verify we have reached the
		       end of thing we are to match.  */
		    if (*p == 0 && *q == 0
			&& (GET_RTX_CLASS (op) == RTX_COMPARE
			    || GET_RTX_CLASS (op) == RTX_COMM_COMPARE))
		      break;
		  }

		if (op == NUM_RTX_CODE)
		  matches = 0;
		else
		  np += strlen (GET_RTX_NAME(op));
		break;
	      case 'a':
	      case 'b':
		/* This loop will stop at the first prefix match, so
                   look through the modes in reverse order, in case
                   there are extra CC modes and CC is a prefix of the
                   CC modes (as it should be).  */
		for (i = (MAX_MACHINE_MODE) - 1; i >= 0; i--)
		  {
		    for (p = GET_MODE_NAME(i), q = np; *p; p++, q++)
		      if (TOLOWER (*p) != *q)
			break;

		    if (*p == 0
			&& (! force_int || mode_class[i] == MODE_INT 
			    || mode_class[i] == MODE_VECTOR_INT)
		        && (! force_partial_int
                            || mode_class[i] == MODE_INT
                            || mode_class[i] == MODE_PARTIAL_INT
			    || mode_class[i] == MODE_VECTOR_INT)
			&& (! force_float
			    || mode_class[i] == MODE_FLOAT 
			    || mode_class[i] == MODE_DECIMAL_FLOAT
			    || mode_class[i] == MODE_COMPLEX_FLOAT
			    || mode_class[i] == MODE_VECTOR_FLOAT))
		      break;
		  }

		if (i < 0)
		  matches = 0;
		else if (*pp == 'a')
		  m1 = i, np += strlen (GET_MODE_NAME(i));
		else
		  m2 = i, np += strlen (GET_MODE_NAME(i));

		force_int = force_partial_int = force_float = 0;
		break;

	      default:
		gcc_unreachable ();
	      }
	}

      if (matches && pp[0] == '$' && pp[1] == ')'
	  && *np == 0
	  && (! force_consec || (int) GET_MODE_WIDER_MODE(m1) == m2))
	break;
    }

  if (pindex == ARRAY_SIZE (optabs))
    return;

  /* We found a match.  If this pattern is only conditionally present,
     write out the "if" and two extra blanks.  */

  if (*XSTR (insn, 2) != 0)
    printf ("  if (HAVE_%s)\n  ", name);

  printf ("  ");

  /* Now write out the initialization, making all required substitutions.  */
  for (pp = optabs[pindex]; *pp; pp++)
    {
      if (*pp != '$')
	putchar (*pp);
      else
	switch (*++pp)
	  {
	  case '(':  case ')':
	  case 'I':  case 'F':  case 'N':
	    break;
	  case 'V':
	    if (SCALAR_FLOAT_MODE_P (m1))
              printf ("v");
            break;
	  case 'a':
	    for (np = GET_MODE_NAME(m1); *np; np++)
	      putchar (TOLOWER (*np));
	    break;
	  case 'b':
	    for (np = GET_MODE_NAME(m2); *np; np++)
	      putchar (TOLOWER (*np));
	    break;
	  case 'A':
	    printf ("%smode", GET_MODE_NAME(m1));
	    break;
	  case 'B':
	    printf ("%smode", GET_MODE_NAME(m2));
	    break;
	  case 'c':
	    printf ("%s", GET_RTX_NAME(op));
	    break;
	  case 'C':
	    for (np = GET_RTX_NAME(op); *np; np++)
	      putchar (TOUPPER (*np));
	    break;
	  }
    }

  printf (";\n");
}

extern int main (int, char **);

int
main (int argc, char **argv)
{
  rtx desc;

  progname = "genopinit";

  if (init_md_reader_args (argc, argv) != SUCCESS_EXIT_CODE)
    return (FATAL_EXIT_CODE);

  printf ("/* Generated automatically by the program `genopinit'\n\
from the machine description file `md'.  */\n\n");

  printf ("#include \"config.h\"\n");
  printf ("#include \"system.h\"\n");
  printf ("#include \"coretypes.h\"\n");
  printf ("#include \"tm.h\"\n");
  printf ("#include \"rtl.h\"\n");
  printf ("#include \"flags.h\"\n");
  printf ("#include \"insn-config.h\"\n");
  printf ("#include \"recog.h\"\n");
  printf ("#include \"expr.h\"\n");
  printf ("#include \"optabs.h\"\n");
  printf ("#include \"reload.h\"\n\n");

  printf ("void\ninit_all_optabs (void)\n{\n");

  puts ("\
#ifdef FIXUNS_TRUNC_LIKE_FIX_TRUNC\n\
  int i, j;\n\
#endif\n");

  /* Read the machine description.  */

  while (1)
    {
      int line_no, insn_code_number = 0;

      desc = read_md_rtx (&line_no, &insn_code_number);
      if (desc == NULL)
	break;

      if (GET_CODE (desc) == DEFINE_INSN || GET_CODE (desc) == DEFINE_EXPAND)
	gen_insn (desc);
    }

  puts ("\
\n\
#ifdef FIXUNS_TRUNC_LIKE_FIX_TRUNC\n\
  /* This flag says the same insns that convert to a signed fixnum\n\
     also convert validly to an unsigned one.  */\n\
  for (i = 0; i < NUM_MACHINE_MODES; i++)\n\
    for (j = 0; j < NUM_MACHINE_MODES; j++)\n\
      ufixtrunc_optab->handlers[i][j].insn_code\n\
      = sfixtrunc_optab->handlers[i][j].insn_code;\n\
#endif\n\
}");

  fflush (stdout);
  return (ferror (stdout) != 0 ? FATAL_EXIT_CODE : SUCCESS_EXIT_CODE);
}
