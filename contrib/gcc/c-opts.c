/* C/ObjC/C++ command line option handling.
   Copyright (C) 2002, 2003, 2004, 2005, 2006, 2007
   Free Software Foundation, Inc.
   Contributed by Neil Booth.

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

/* $FreeBSD$ */
/* Merged C99 inline changes from gcc trunk 122565 2007-03-05 */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "tree.h"
#include "c-common.h"
#include "c-pragma.h"
#include "flags.h"
#include "toplev.h"
#include "langhooks.h"
#include "tree-inline.h"
#include "diagnostic.h"
#include "intl.h"
#include "cppdefault.h"
#include "c-incpath.h"
#include "debug.h"		/* For debug_hooks.  */
#include "opts.h"
#include "options.h"
#include "mkdeps.h"

#ifndef DOLLARS_IN_IDENTIFIERS
# define DOLLARS_IN_IDENTIFIERS true
#endif

#ifndef TARGET_SYSTEM_ROOT
# define TARGET_SYSTEM_ROOT NULL
#endif

#ifndef TARGET_OPTF
#define TARGET_OPTF(ARG)
#endif

/* CPP's options.  */
static cpp_options *cpp_opts;

/* Input filename.  */
static const char *this_input_filename;

/* Filename and stream for preprocessed output.  */
static const char *out_fname;
static FILE *out_stream;

/* Append dependencies to deps_file.  */
static bool deps_append;

/* If dependency switches (-MF etc.) have been given.  */
static bool deps_seen;

/* If -v seen.  */
static bool verbose;

/* If -lang-fortran seen.  */
static bool lang_fortran = false;

/* Dependency output file.  */
static const char *deps_file;

/* The prefix given by -iprefix, if any.  */
static const char *iprefix;

/* The multilib directory given by -imultilib, if any.  */
static const char *imultilib;

/* The system root, if any.  Overridden by -isysroot.  */
static const char *sysroot = TARGET_SYSTEM_ROOT;

/* Zero disables all standard directories for headers.  */
static bool std_inc = true;

/* Zero disables the C++-specific standard directories for headers.  */
static bool std_cxx_inc = true;

/* If the quote chain has been split by -I-.  */
static bool quote_chain_split;

/* If -Wunused-macros.  */
static bool warn_unused_macros;

/* If -Wvariadic-macros.  */
static bool warn_variadic_macros = true;

/* Number of deferred options.  */
static size_t deferred_count;

/* Number of deferred options scanned for -include.  */
static size_t include_cursor;

static void set_Wimplicit (int);
static void handle_OPT_d (const char *);
static void set_std_cxx98 (int);
static void set_std_c89 (int, int);
static void set_std_c99 (int);
static void check_deps_environment_vars (void);
static void handle_deferred_opts (void);
static void sanitize_cpp_opts (void);
static void add_prefixed_path (const char *, size_t);
static void push_command_line_include (void);
static void cb_file_change (cpp_reader *, const struct line_map *);
static void cb_dir_change (cpp_reader *, const char *);
static void finish_options (void);

#ifndef STDC_0_IN_SYSTEM_HEADERS
#define STDC_0_IN_SYSTEM_HEADERS 0
#endif

/* Holds switches parsed by c_common_handle_option (), but whose
   handling is deferred to c_common_post_options ().  */
static void defer_opt (enum opt_code, const char *);
static struct deferred_opt
{
  enum opt_code code;
  const char *arg;
} *deferred_opts;

/* Complain that switch CODE expects an argument but none was
   provided.  OPT was the command-line option.  Return FALSE to get
   the default message in opts.c, TRUE if we provide a specialized
   one.  */
bool
c_common_missing_argument (const char *opt, size_t code)
{
  switch (code)
    {
    default:
      /* Pick up the default message.  */
      return false;

    case OPT_fconstant_string_class_:
      error ("no class name specified with %qs", opt);
      break;

    case OPT_A:
      error ("assertion missing after %qs", opt);
      break;

    case OPT_D:
    case OPT_U:
      error ("macro name missing after %qs", opt);
      break;

    case OPT_F:
    case OPT_I:
    case OPT_idirafter:
    case OPT_isysroot:
    case OPT_isystem:
    case OPT_iquote:
      error ("missing path after %qs", opt);
      break;

    case OPT_MF:
    case OPT_MD:
    case OPT_MMD:
    case OPT_include:
    case OPT_imacros:
    case OPT_o:
      error ("missing filename after %qs", opt);
      break;

    case OPT_MQ:
    case OPT_MT:
      error ("missing makefile target after %qs", opt);
      break;
    }

  return true;
}

/* Defer option CODE with argument ARG.  */
static void
defer_opt (enum opt_code code, const char *arg)
{
  deferred_opts[deferred_count].code = code;
  deferred_opts[deferred_count].arg = arg;
  deferred_count++;
}

/* Common initialization before parsing options.  */
unsigned int
c_common_init_options (unsigned int argc, const char **argv)
{
  static const unsigned int lang_flags[] = {CL_C, CL_ObjC, CL_CXX, CL_ObjCXX};
  unsigned int i, result;

  /* This is conditionalized only because that is the way the front
     ends used to do it.  Maybe this should be unconditional?  */
  if (c_dialect_cxx ())
    {
      /* By default wrap lines at 80 characters.  Is getenv
	 ("COLUMNS") preferable?  */
      diagnostic_line_cutoff (global_dc) = 80;
      /* By default, emit location information once for every
	 diagnostic message.  */
      diagnostic_prefixing_rule (global_dc) = DIAGNOSTICS_SHOW_PREFIX_ONCE;
    }

  parse_in = cpp_create_reader (c_dialect_cxx () ? CLK_GNUCXX: CLK_GNUC89,
				ident_hash, &line_table);

  cpp_opts = cpp_get_options (parse_in);
  cpp_opts->dollars_in_ident = DOLLARS_IN_IDENTIFIERS;
  cpp_opts->objc = c_dialect_objc ();

  /* Reset to avoid warnings on internal definitions.  We set it just
     before passing on command-line options to cpplib.  */
  cpp_opts->warn_dollars = 0;

  flag_exceptions = c_dialect_cxx ();
  warn_pointer_arith = c_dialect_cxx ();
  warn_write_strings = c_dialect_cxx();

  deferred_opts = XNEWVEC (struct deferred_opt, argc);

  result = lang_flags[c_language];

  if (c_language == clk_c)
    {
      /* If preprocessing assembly language, accept any of the C-family
	 front end options since the driver may pass them through.  */
      for (i = 1; i < argc; i++)
	if (! strcmp (argv[i], "-lang-asm"))
	  {
	    result |= CL_C | CL_ObjC | CL_CXX | CL_ObjCXX;
	    break;
	  }

#ifdef CL_Fortran
      for (i = 1; i < argc; i++)
	if (! strcmp (argv[i], "-lang-fortran"))
	{
	    result |= CL_Fortran;
	    break;
	}
#endif
    }

  return result;
}

/* Handle switch SCODE with argument ARG.  VALUE is true, unless no-
   form of an -f or -W option was given.  Returns 0 if the switch was
   invalid, a negative number to prevent language-independent
   processing in toplev.c (a hack necessary for the short-term).  */
int
c_common_handle_option (size_t scode, const char *arg, int value)
{
  const struct cl_option *option = &cl_options[scode];
  enum opt_code code = (enum opt_code) scode;
  int result = 1;

  /* Prevent resetting the language standard to a C dialect when the driver
     has already determined that we're looking at assembler input.  */
  bool preprocessing_asm_p = (cpp_get_options (parse_in)->lang == CLK_ASM);
 
  switch (code)
    {
    default:
      if (cl_options[code].flags & (CL_C | CL_CXX | CL_ObjC | CL_ObjCXX))
	break;
#ifdef CL_Fortran
      if (lang_fortran && (cl_options[code].flags & (CL_Fortran)))
	break;
#endif
      result = 0;
      break;

    case OPT__output_pch_:
      pch_file = arg;
      break;

    case OPT_A:
      defer_opt (code, arg);
      break;

    case OPT_C:
      cpp_opts->discard_comments = 0;
      break;

    case OPT_CC:
      cpp_opts->discard_comments = 0;
      cpp_opts->discard_comments_in_macro_exp = 0;
      break;

    case OPT_D:
      defer_opt (code, arg);
      break;

    case OPT_E:
      flag_preprocess_only = 1;
      break;

    case OPT_H:
      cpp_opts->print_include_names = 1;
      break;

    case OPT_F:
      TARGET_OPTF (xstrdup (arg));
      break;

    case OPT_I:
      if (strcmp (arg, "-"))
	add_path (xstrdup (arg), BRACKET, 0, true);
      else
	{
	  if (quote_chain_split)
	    error ("-I- specified twice");
	  quote_chain_split = true;
	  split_quote_chain ();
	  inform ("obsolete option -I- used, please use -iquote instead");
	}
      break;

    case OPT_M:
    case OPT_MM:
      /* When doing dependencies with -M or -MM, suppress normal
	 preprocessed output, but still do -dM etc. as software
	 depends on this.  Preprocessed output does occur if -MD, -MMD
	 or environment var dependency generation is used.  */
      cpp_opts->deps.style = (code == OPT_M ? DEPS_SYSTEM: DEPS_USER);
      flag_no_output = 1;
      cpp_opts->inhibit_warnings = 1;
      break;

    case OPT_MD:
    case OPT_MMD:
      cpp_opts->deps.style = (code == OPT_MD ? DEPS_SYSTEM: DEPS_USER);
      deps_file = arg;
      break;

    case OPT_MF:
      deps_seen = true;
      deps_file = arg;
      break;

    case OPT_MG:
      deps_seen = true;
      cpp_opts->deps.missing_files = true;
      break;

    case OPT_MP:
      deps_seen = true;
      cpp_opts->deps.phony_targets = true;
      break;

    case OPT_MQ:
    case OPT_MT:
      deps_seen = true;
      defer_opt (code, arg);
      break;

    case OPT_P:
      flag_no_line_commands = 1;
      break;

    case OPT_fworking_directory:
      flag_working_directory = value;
      break;

    case OPT_U:
      defer_opt (code, arg);
      break;

    case OPT_Wall:
      /* APPLE LOCAL -Wmost */
    case OPT_Wmost:
      set_Wunused (value);
      set_Wformat (value);
      set_Wimplicit (value);
      warn_char_subscripts = value;
      warn_missing_braces = value;
      /* APPLE LOCAL begin -Wmost --dpatel */
      if (code != OPT_Wmost) 
	warn_parentheses = value;
      /* APPLE LOCAL end -Wmost --dpatel */
      warn_return_type = value;
      warn_sequence_point = value;	/* Was C only.  */
      if (c_dialect_cxx ())
	warn_sign_compare = value;
      warn_switch = value;
      set_warn_strict_aliasing (value);
      warn_strict_overflow = value;
      warn_address = value;

      /* Only warn about unknown pragmas that are not in system
	 headers.  */
      warn_unknown_pragmas = value;

      /* We save the value of warn_uninitialized, since if they put
	 -Wuninitialized on the command line, we need to generate a
	 warning about not using it without also specifying -O.  */
      if (warn_uninitialized != 1)
	warn_uninitialized = (value ? 2 : 0);

      if (!c_dialect_cxx ())
	/* We set this to 2 here, but 1 in -Wmain, so -ffreestanding
	   can turn it off only if it's not explicit.  */
	warn_main = value * 2;
      else
	{
	  /* C++-specific warnings.  */
	  warn_reorder = value;
	  warn_nontemplate_friend = value;
	}

      cpp_opts->warn_trigraphs = value;
      cpp_opts->warn_comments = value;
      cpp_opts->warn_num_sign_change = value;
      cpp_opts->warn_multichar = value;	/* Was C++ only.  */

      if (warn_pointer_sign == -1)
	warn_pointer_sign = 1;
      break;

    case OPT_Wcomment:
    case OPT_Wcomments:
      cpp_opts->warn_comments = value;
      break;

    case OPT_Wdeprecated:
      cpp_opts->warn_deprecated = value;
      break;

    case OPT_Wendif_labels:
      cpp_opts->warn_endif_labels = value;
      break;

    case OPT_Werror:
      cpp_opts->warnings_are_errors = value;
      global_dc->warning_as_error_requested = value;
      break;

    case OPT_Werror_implicit_function_declaration:
      mesg_implicit_function_declaration = 2;
      break;

    case OPT_Wformat:
      set_Wformat (value);
      break;

    case OPT_Wformat_:
      set_Wformat (atoi (arg));
      break;

    case OPT_Wimplicit:
      set_Wimplicit (value);
      break;

    case OPT_Wimport:
      /* Silently ignore for now.  */
      break;

    case OPT_Winvalid_pch:
      cpp_opts->warn_invalid_pch = value;
      break;

    case OPT_Wmain:
      if (value)
	warn_main = 1;
      else
	warn_main = -1;
      break;

    case OPT_Wmissing_include_dirs:
      cpp_opts->warn_missing_include_dirs = value;
      break;

    case OPT_Wmultichar:
      cpp_opts->warn_multichar = value;
      break;

      /* APPLE LOCAL begin -Wnewline-eof */
    case OPT_Wnewline_eof:
      cpp_opts->warn_newline_at_eof = value;
      break;
      /* APPLE LOCAL end -Wnewline-eof */

    case OPT_Wnormalized_:
      if (!value || (arg && strcasecmp (arg, "none") == 0))
	cpp_opts->warn_normalize = normalized_none;
      else if (!arg || strcasecmp (arg, "nfkc") == 0)
	cpp_opts->warn_normalize = normalized_KC;
      else if (strcasecmp (arg, "id") == 0)
	cpp_opts->warn_normalize = normalized_identifier_C;
      else if (strcasecmp (arg, "nfc") == 0)
	cpp_opts->warn_normalize = normalized_C;
      else
	error ("argument %qs to %<-Wnormalized%> not recognized", arg);
      break;

    case OPT_Wreturn_type:
      warn_return_type = value;
      break;

    case OPT_Wstrict_null_sentinel:
      warn_strict_null_sentinel = value;
      break;

    case OPT_Wsystem_headers:
      cpp_opts->warn_system_headers = value;
      break;

    case OPT_Wtraditional:
      cpp_opts->warn_traditional = value;
      break;

    case OPT_Wtrigraphs:
      cpp_opts->warn_trigraphs = value;
      break;

    case OPT_Wundef:
      cpp_opts->warn_undef = value;
      break;

    case OPT_Wunknown_pragmas:
      /* Set to greater than 1, so that even unknown pragmas in
	 system headers will be warned about.  */
      warn_unknown_pragmas = value * 2;
      break;

    case OPT_Wunused_macros:
      warn_unused_macros = value;
      break;

    case OPT_Wvariadic_macros:
      warn_variadic_macros = value;
      break;

    case OPT_Wwrite_strings:
      warn_write_strings = value;
      break;

    case OPT_Weffc__:
      warn_ecpp = value;
      if (value)
        warn_nonvdtor = true;
      break;

    case OPT_ansi:
      if (!c_dialect_cxx ())
	set_std_c89 (false, true);
      else
	set_std_cxx98 (true);
      break;

    case OPT_d:
      handle_OPT_d (arg);
      break;

    case OPT_fcond_mismatch:
      if (!c_dialect_cxx ())
	{
	  flag_cond_mismatch = value;
	  break;
	}
      /* Fall through.  */

    case OPT_fall_virtual:
    case OPT_falt_external_templates:
    case OPT_fenum_int_equiv:
    case OPT_fexternal_templates:
    case OPT_fguiding_decls:
    case OPT_fhonor_std:
    case OPT_fhuge_objects:
    case OPT_flabels_ok:
    case OPT_fname_mangling_version_:
    case OPT_fnew_abi:
    case OPT_fnonnull_objects:
    case OPT_fsquangle:
    case OPT_fstrict_prototype:
    case OPT_fthis_is_variable:
    case OPT_fvtable_thunks:
    case OPT_fxref:
    case OPT_fvtable_gc:
      warning (0, "switch %qs is no longer supported", option->opt_text);
      break;

    case OPT_faccess_control:
      flag_access_control = value;
      break;

    case OPT_fasm:
      flag_no_asm = !value;
      break;

    case OPT_fbuiltin:
      flag_no_builtin = !value;
      break;

    case OPT_fbuiltin_:
      if (value)
	result = 0;
      else
	disable_builtin_function (arg);
      break;

    case OPT_fdirectives_only:
      cpp_opts->directives_only = 1;
      break;

    case OPT_fdollars_in_identifiers:
      cpp_opts->dollars_in_ident = value;
      break;

    case OPT_ffreestanding:
      value = !value;
      /* Fall through....  */
    case OPT_fhosted:
      flag_hosted = value;
      flag_no_builtin = !value;
      /* warn_main will be 2 if set by -Wall, 1 if set by -Wmain */
      if (!value && warn_main == 2)
	warn_main = 0;
      break;

    case OPT_fshort_double:
      flag_short_double = value;
      break;

    case OPT_fshort_enums:
      flag_short_enums = value;
      break;

    case OPT_fshort_wchar:
      flag_short_wchar = value;
      break;

    case OPT_fsigned_bitfields:
      flag_signed_bitfields = value;
      break;

    case OPT_fsigned_char:
      flag_signed_char = value;
      break;

    case OPT_funsigned_bitfields:
      flag_signed_bitfields = !value;
      break;

    case OPT_funsigned_char:
      flag_signed_char = !value;
      break;

    case OPT_fcheck_new:
      flag_check_new = value;
      break;

    case OPT_fconserve_space:
      flag_conserve_space = value;
      break;

    case OPT_fconstant_string_class_:
      constant_string_class_name = arg;
      break;

    case OPT_fdefault_inline:
      flag_default_inline = value;
      break;

    case OPT_felide_constructors:
      flag_elide_constructors = value;
      break;

    case OPT_fenforce_eh_specs:
      flag_enforce_eh_specs = value;
      break;

    case OPT_fextended_identifiers:
      cpp_opts->extended_identifiers = value;
      break;

    case OPT_ffor_scope:
      flag_new_for_scope = value;
      break;

    case OPT_fgnu_keywords:
      flag_no_gnu_keywords = !value;
      break;

    case OPT_fgnu_runtime:
      flag_next_runtime = !value;
      break;

    case OPT_fhandle_exceptions:
      warning (0, "-fhandle-exceptions has been renamed -fexceptions (and is now on by default)");
      flag_exceptions = value;
      break;

    case OPT_fimplement_inlines:
      flag_implement_inlines = value;
      break;

    case OPT_fimplicit_inline_templates:
      flag_implicit_inline_templates = value;
      break;

    case OPT_fimplicit_templates:
      flag_implicit_templates = value;
      break;

    case OPT_flax_vector_conversions:
      flag_lax_vector_conversions = value;
      break;

    case OPT_fms_extensions:
      flag_ms_extensions = value;
      break;

    case OPT_fnext_runtime:
      flag_next_runtime = value;
      break;

    case OPT_fnil_receivers:
      flag_nil_receivers = value;
      break;

    case OPT_fnonansi_builtins:
      flag_no_nonansi_builtin = !value;
      break;

    case OPT_foperator_names:
      cpp_opts->operator_names = value;
      break;

    case OPT_foptional_diags:
      flag_optional_diags = value;
      break;

    case OPT_fpch_deps:
      cpp_opts->restore_pch_deps = value;
      break;

    case OPT_fpch_preprocess:
      flag_pch_preprocess = value;
      break;

    case OPT_fpermissive:
      flag_permissive = value;
      break;

    case OPT_fpreprocessed:
      cpp_opts->preprocessed = value;
      break;

    case OPT_freplace_objc_classes:
      flag_replace_objc_classes = value;
      break;

    case OPT_frepo:
      flag_use_repository = value;
      if (value)
	flag_implicit_templates = 0;
      break;

    case OPT_frtti:
      flag_rtti = value;
      break;

    case OPT_fshow_column:
      cpp_opts->show_column = value;
      break;

    case OPT_fstats:
      flag_detailed_statistics = value;
      break;

    case OPT_ftabstop_:
      /* It is documented that we silently ignore silly values.  */
      if (value >= 1 && value <= 100)
	cpp_opts->tabstop = value;
      break;

    case OPT_fexec_charset_:
      cpp_opts->narrow_charset = arg;
      break;

    case OPT_fwide_exec_charset_:
      cpp_opts->wide_charset = arg;
      break;

    case OPT_finput_charset_:
      cpp_opts->input_charset = arg;
      break;

    case OPT_ftemplate_depth_:
      max_tinst_depth = value;
      break;

    case OPT_fuse_cxa_atexit:
      flag_use_cxa_atexit = value;
      break;
      
    case OPT_fuse_cxa_get_exception_ptr:
      flag_use_cxa_get_exception_ptr = value;
      break;

    case OPT_fvisibility_inlines_hidden:
      visibility_options.inlines_hidden = value;
      break;

    case OPT_fweak:
      flag_weak = value;
      break;

    case OPT_fthreadsafe_statics:
      flag_threadsafe_statics = value;
      break;

    case OPT_fzero_link:
      flag_zero_link = value;
      break;

    case OPT_gen_decls:
      flag_gen_declaration = 1;
      break;

    case OPT_femit_struct_debug_baseonly:
      set_struct_debug_option ("base");
      break;

    case OPT_femit_struct_debug_reduced:
      set_struct_debug_option ("dir:ord:sys,dir:gen:any,ind:base");
      break;

    case OPT_femit_struct_debug_detailed_:
      set_struct_debug_option (arg);
      break;

    case OPT_idirafter:
      add_path (xstrdup (arg), AFTER, 0, true);
      break;

    case OPT_imacros:
    case OPT_include:
      defer_opt (code, arg);
      break;

    case OPT_imultilib:
      imultilib = arg;
      break;

    case OPT_iprefix:
      iprefix = arg;
      break;

    case OPT_iquote:
      add_path (xstrdup (arg), QUOTE, 0, true);
      break;

    case OPT_isysroot:
      sysroot = arg;
      break;

    case OPT_isystem:
      add_path (xstrdup (arg), SYSTEM, 0, true);
      break;

    case OPT_iwithprefix:
      add_prefixed_path (arg, SYSTEM);
      break;

    case OPT_iwithprefixbefore:
      add_prefixed_path (arg, BRACKET);
      break;

    case OPT_lang_asm:
      cpp_set_lang (parse_in, CLK_ASM);
      cpp_opts->dollars_in_ident = false;
      break;

    case OPT_lang_fortran:
      lang_fortran = true;
      break;

    case OPT_lang_objc:
      cpp_opts->objc = 1;
      break;

    case OPT_nostdinc:
      std_inc = false;
      break;

    case OPT_nostdinc__:
      std_cxx_inc = false;
      break;

    case OPT_o:
      if (!out_fname)
	out_fname = arg;
      else
	error ("output filename specified twice");
      break;

      /* We need to handle the -pedantic switches here, rather than in
	 c_common_post_options, so that a subsequent -Wno-endif-labels
	 is not overridden.  */
    case OPT_pedantic_errors:
      cpp_opts->pedantic_errors = 1;
      /* Fall through.  */
    case OPT_pedantic:
      cpp_opts->pedantic = 1;
      cpp_opts->warn_endif_labels = 1;
      if (warn_pointer_sign == -1)
	warn_pointer_sign = 1;
      if (warn_overlength_strings == -1)
	warn_overlength_strings = 1;
      break;

    case OPT_print_objc_runtime_info:
      print_struct_values = 1;
      break;

    case OPT_print_pch_checksum:
      c_common_print_pch_checksum (stdout);
      exit_after_options = true;
      break;

    case OPT_remap:
      cpp_opts->remap = 1;
      break;

    case OPT_std_c__98:
    case OPT_std_gnu__98:
      if (!preprocessing_asm_p)
	set_std_cxx98 (code == OPT_std_c__98 /* ISO */);
      break;

    case OPT_std_c89:
    case OPT_std_iso9899_1990:
    case OPT_std_iso9899_199409:
      if (!preprocessing_asm_p)
	set_std_c89 (code == OPT_std_iso9899_199409 /* c94 */, true /* ISO */);
      break;

    case OPT_std_gnu89:
      if (!preprocessing_asm_p)
	set_std_c89 (false /* c94 */, false /* ISO */);
      break;

    case OPT_std_c99:
    case OPT_std_c9x:
    case OPT_std_iso9899_1999:
    case OPT_std_iso9899_199x:
      if (!preprocessing_asm_p)
	set_std_c99 (true /* ISO */);
      break;

    case OPT_std_gnu99:
    case OPT_std_gnu9x:
      if (!preprocessing_asm_p)
	set_std_c99 (false /* ISO */);
      break;

    case OPT_trigraphs:
      cpp_opts->trigraphs = 1;
      break;

    case OPT_traditional_cpp:
      cpp_opts->traditional = 1;
      break;

    case OPT_undef:
      flag_undef = 1;
      break;

    case OPT_w:
      cpp_opts->inhibit_warnings = 1;
      break;

    case OPT_v:
      verbose = true;
      break;
    }

  return result;
}

/* Post-switch processing.  */
bool
c_common_post_options (const char **pfilename)
{
  struct cpp_callbacks *cb;

  /* Canonicalize the input and output filenames.  */
  if (in_fnames == NULL)
    {
      in_fnames = XNEWVEC (const char *, 1);
      in_fnames[0] = "";
    }
  else if (strcmp (in_fnames[0], "-") == 0)
    in_fnames[0] = "";

  if (out_fname == NULL || !strcmp (out_fname, "-"))
    out_fname = "";

  if (cpp_opts->deps.style == DEPS_NONE)
    check_deps_environment_vars ();

  handle_deferred_opts ();

  sanitize_cpp_opts ();

  register_include_chains (parse_in, sysroot, iprefix, imultilib,
			   std_inc, std_cxx_inc && c_dialect_cxx (), verbose);

#ifdef C_COMMON_OVERRIDE_OPTIONS
  /* Some machines may reject certain combinations of C
     language-specific options.  */
  C_COMMON_OVERRIDE_OPTIONS;
#endif

  flag_inline_trees = 1;

  /* Use tree inlining.  */
  if (!flag_no_inline)
    flag_no_inline = 1;
  if (flag_inline_functions)
    flag_inline_trees = 2;

  /* APPLE LOCAL begin radar 5811887  - radar 6084601 */
  /* In all flavors of c99, except for ObjC/ObjC++, blocks are off by default 
     unless requested via -fblocks. */
  if (flag_blocks == -1 && flag_iso && !c_dialect_objc())
    flag_blocks = 0;
  /* APPLE LOCAL end radar 5811887 - radar 6084601 */

  /* By default we use C99 inline semantics in GNU99 or C99 mode.  C99
     inline semantics are not supported in GNU89 or C89 mode.  */
  if (flag_gnu89_inline == -1)
    flag_gnu89_inline = !flag_isoc99;
  else if (!flag_gnu89_inline && !flag_isoc99)
    error ("-fno-gnu89-inline is only supported in GNU99 or C99 mode");

  /* If we are given more than one input file, we must use
     unit-at-a-time mode.  */
  if (num_in_fnames > 1)
    flag_unit_at_a_time = 1;

  /* Default to ObjC sjlj exception handling if NeXT runtime.  */
  if (flag_objc_sjlj_exceptions < 0)
    flag_objc_sjlj_exceptions = flag_next_runtime;
  if (flag_objc_exceptions && !flag_objc_sjlj_exceptions)
    flag_exceptions = 1;

  /* -Wextra implies -Wsign-compare, -Wmissing-field-initializers and
     -Woverride-init, but not if explicitly overridden.  */
  if (warn_sign_compare == -1)
    warn_sign_compare = extra_warnings;
  if (warn_missing_field_initializers == -1)
    warn_missing_field_initializers = extra_warnings;
  if (warn_override_init == -1)
    warn_override_init = extra_warnings;

  /* -Wpointer_sign is disabled by default, but it is enabled if any
     of -Wall or -pedantic are given.  */
  if (warn_pointer_sign == -1)
    warn_pointer_sign = 0;

  /* -Woverlength-strings is off by default, but is enabled by -pedantic.
     It is never enabled in C++, as the minimum limit is not normative
     in that standard.  */
  if (warn_overlength_strings == -1 || c_dialect_cxx ())
    warn_overlength_strings = 0;

  /* Special format checking options don't work without -Wformat; warn if
     they are used.  */
  if (!warn_format)
    {
      warning (OPT_Wformat_y2k,
	       "-Wformat-y2k ignored without -Wformat");
      warning (OPT_Wformat_extra_args,
	       "-Wformat-extra-args ignored without -Wformat");
      warning (OPT_Wformat_zero_length,
	       "-Wformat-zero-length ignored without -Wformat");
      warning (OPT_Wformat_nonliteral,
	       "-Wformat-nonliteral ignored without -Wformat");
      warning (OPT_Wformat_security,
	       "-Wformat-security ignored without -Wformat");
    }

  /* C99 requires special handling of complex multiplication and division;
     -ffast-math and -fcx-limited-range are handled in process_options.  */
  if (flag_isoc99)
    flag_complex_method = 2;

  if (flag_preprocess_only)
    {
      /* Open the output now.  We must do so even if flag_no_output is
	 on, because there may be other output than from the actual
	 preprocessing (e.g. from -dM).  */
      if (out_fname[0] == '\0')
	out_stream = stdout;
      else
	out_stream = fopen (out_fname, "w");

      if (out_stream == NULL)
	{
	  fatal_error ("opening output file %s: %m", out_fname);
	  return false;
	}

      if (num_in_fnames > 1)
	error ("too many filenames given.  Type %s --help for usage",
	       progname);

      init_pp_output (out_stream);
    }
  else
    {
      init_c_lex ();

      /* Yuk.  WTF is this?  I do know ObjC relies on it somewhere.  */
      input_location = UNKNOWN_LOCATION;
    }

  cb = cpp_get_callbacks (parse_in);
  cb->file_change = cb_file_change;
  cb->dir_change = cb_dir_change;
  cpp_post_options (parse_in);

  input_location = UNKNOWN_LOCATION;

  /* If an error has occurred in cpplib, note it so we fail
     immediately.  */
  errorcount += cpp_errors (parse_in);

  *pfilename = this_input_filename
    = cpp_read_main_file (parse_in, in_fnames[0]);
  /* Don't do any compilation or preprocessing if there is no input file.  */
  if (this_input_filename == NULL)
    {
      errorcount++;
      return false;
    }

  if (flag_working_directory
      && flag_preprocess_only && !flag_no_line_commands)
    pp_dir_change (parse_in, get_src_pwd ());

  return flag_preprocess_only;
}

/* Front end initialization common to C, ObjC and C++.  */
bool
c_common_init (void)
{
  /* Set up preprocessor arithmetic.  Must be done after call to
     c_common_nodes_and_builtins for type nodes to be good.  */
  cpp_opts->precision = TYPE_PRECISION (intmax_type_node);
  cpp_opts->char_precision = TYPE_PRECISION (char_type_node);
  cpp_opts->int_precision = TYPE_PRECISION (integer_type_node);
  cpp_opts->wchar_precision = TYPE_PRECISION (wchar_type_node);
  cpp_opts->unsigned_wchar = TYPE_UNSIGNED (wchar_type_node);
  cpp_opts->bytes_big_endian = BYTES_BIG_ENDIAN;

  /* This can't happen until after wchar_precision and bytes_big_endian
     are known.  */
  cpp_init_iconv (parse_in);

  if (version_flag)
    c_common_print_pch_checksum (stderr);

  if (flag_preprocess_only)
    {
      finish_options ();
      preprocess_file (parse_in);
      return false;
    }

  /* Has to wait until now so that cpplib has its hash table.  */
  init_pragma ();

  return true;
}

/* Initialize the integrated preprocessor after debug output has been
   initialized; loop over each input file.  */
void
c_common_parse_file (int set_yydebug)
{
  unsigned int i;

  /* Enable parser debugging, if requested and we can.  If requested
     and we can't, notify the user.  */
#if YYDEBUG != 0
  yydebug = set_yydebug;
#else
  if (set_yydebug)
    warning (0, "YYDEBUG was not defined at build time, -dy ignored");
#endif

  i = 0;
  for (;;)
    {
      /* Start the main input file, if the debug writer wants it. */
      if (debug_hooks->start_end_main_source_file)
	(*debug_hooks->start_source_file) (0, this_input_filename);
      finish_options ();
      pch_init ();
      push_file_scope ();
      c_parse_file ();
      finish_file ();
      pop_file_scope ();
      /* And end the main input file, if the debug writer wants it  */
      if (debug_hooks->start_end_main_source_file)
	(*debug_hooks->end_source_file) (0);
      if (++i >= num_in_fnames)
	break;
      cpp_undef_all (parse_in);
      this_input_filename
	= cpp_read_main_file (parse_in, in_fnames[i]);
      /* If an input file is missing, abandon further compilation.
	 cpplib has issued a diagnostic.  */
      if (!this_input_filename)
	break;
    }
}

/* Common finish hook for the C, ObjC and C++ front ends.  */
void
c_common_finish (void)
{
  FILE *deps_stream = NULL;

  if (cpp_opts->deps.style != DEPS_NONE)
    {
      /* If -M or -MM was seen without -MF, default output to the
	 output stream.  */
      if (!deps_file)
	deps_stream = out_stream;
      else
	{
	  deps_stream = fopen (deps_file, deps_append ? "a": "w");
	  if (!deps_stream)
	    fatal_error ("opening dependency file %s: %m", deps_file);
	}
    }

  /* For performance, avoid tearing down cpplib's internal structures
     with cpp_destroy ().  */
  errorcount += cpp_finish (parse_in, deps_stream);

  if (deps_stream && deps_stream != out_stream
      && (ferror (deps_stream) || fclose (deps_stream)))
    fatal_error ("closing dependency file %s: %m", deps_file);

  if (out_stream && (ferror (out_stream) || fclose (out_stream)))
    fatal_error ("when writing output to %s: %m", out_fname);
}

/* Either of two environment variables can specify output of
   dependencies.  Their value is either "OUTPUT_FILE" or "OUTPUT_FILE
   DEPS_TARGET", where OUTPUT_FILE is the file to write deps info to
   and DEPS_TARGET is the target to mention in the deps.  They also
   result in dependency information being appended to the output file
   rather than overwriting it, and like Sun's compiler
   SUNPRO_DEPENDENCIES suppresses the dependency on the main file.  */
static void
check_deps_environment_vars (void)
{
  char *spec;

  GET_ENVIRONMENT (spec, "DEPENDENCIES_OUTPUT");
  if (spec)
    cpp_opts->deps.style = DEPS_USER;
  else
    {
      GET_ENVIRONMENT (spec, "SUNPRO_DEPENDENCIES");
      if (spec)
	{
	  cpp_opts->deps.style = DEPS_SYSTEM;
	  cpp_opts->deps.ignore_main_file = true;
	}
    }

  if (spec)
    {
      /* Find the space before the DEPS_TARGET, if there is one.  */
      char *s = strchr (spec, ' ');
      if (s)
	{
	  /* Let the caller perform MAKE quoting.  */
	  defer_opt (OPT_MT, s + 1);
	  *s = '\0';
	}

      /* Command line -MF overrides environment variables and default.  */
      if (!deps_file)
	deps_file = spec;

      deps_append = 1;
      deps_seen = true;
    }
}

/* Handle deferred command line switches.  */
static void
handle_deferred_opts (void)
{
  size_t i;
  struct deps *deps;

  /* Avoid allocating the deps buffer if we don't need it.
     (This flag may be true without there having been -MT or -MQ
     options, but we'll still need the deps buffer.)  */
  if (!deps_seen)
    return;

  deps = cpp_get_deps (parse_in);

  for (i = 0; i < deferred_count; i++)
    {
      struct deferred_opt *opt = &deferred_opts[i];

      if (opt->code == OPT_MT || opt->code == OPT_MQ)
	deps_add_target (deps, opt->arg, opt->code == OPT_MQ);
    }
}

/* These settings are appropriate for GCC, but not necessarily so for
   cpplib as a library.  */
static void
sanitize_cpp_opts (void)
{
  /* If we don't know what style of dependencies to output, complain
     if any other dependency switches have been given.  */
  if (deps_seen && cpp_opts->deps.style == DEPS_NONE)
    error ("to generate dependencies you must specify either -M or -MM");

  /* -dM and dependencies suppress normal output; do it here so that
     the last -d[MDN] switch overrides earlier ones.  */
  if (flag_dump_macros == 'M')
    flag_no_output = 1;

  /* By default, -fdirectives-only implies -dD.  This allows subsequent phases
     to perform proper macro expansion.  */
  if (cpp_opts->directives_only && !cpp_opts->preprocessed && !flag_dump_macros)
    flag_dump_macros = 'D';

  /* Disable -dD, -dN and -dI if normal output is suppressed.  Allow
     -dM since at least glibc relies on -M -dM to work.  */
  /* Also, flag_no_output implies flag_no_line_commands, always.  */
  if (flag_no_output)
    {
      if (flag_dump_macros != 'M')
	flag_dump_macros = 0;
      flag_dump_includes = 0;
      flag_no_line_commands = 1;
    }

  cpp_opts->unsigned_char = !flag_signed_char;
  cpp_opts->stdc_0_in_system_headers = STDC_0_IN_SYSTEM_HEADERS;

  /* We want -Wno-long-long to override -pedantic -std=non-c99
     and/or -Wtraditional, whatever the ordering.  */
  cpp_opts->warn_long_long
    = warn_long_long && ((!flag_isoc99 && pedantic) || warn_traditional);

  /* Similarly with -Wno-variadic-macros.  No check for c99 here, since
     this also turns off warnings about GCCs extension.  */
  cpp_opts->warn_variadic_macros
    = warn_variadic_macros && (pedantic || warn_traditional);

  /* If we're generating preprocessor output, emit current directory
     if explicitly requested or if debugging information is enabled.
     ??? Maybe we should only do it for debugging formats that
     actually output the current directory?  */
  if (flag_working_directory == -1)
    flag_working_directory = (debug_info_level != DINFO_LEVEL_NONE);

  if (cpp_opts->directives_only)
    {
      if (warn_unused_macros)
	error ("-fdirectives-only is incompatible with -Wunused_macros");
      if (cpp_opts->traditional)
	error ("-fdirectives-only is incompatible with -traditional");
    }
}

/* Add include path with a prefix at the front of its name.  */
static void
add_prefixed_path (const char *suffix, size_t chain)
{
  char *path;
  const char *prefix;
  size_t prefix_len, suffix_len;

  suffix_len = strlen (suffix);
  prefix     = iprefix ? iprefix : cpp_GCC_INCLUDE_DIR;
  prefix_len = iprefix ? strlen (iprefix) : cpp_GCC_INCLUDE_DIR_len;

  path = (char *) xmalloc (prefix_len + suffix_len + 1);
  memcpy (path, prefix, prefix_len);
  memcpy (path + prefix_len, suffix, suffix_len);
  path[prefix_len + suffix_len] = '\0';

  add_path (path, chain, 0, false);
}

/* Handle -D, -U, -A, -imacros, and the first -include.  */
static void
finish_options (void)
{
  if (!cpp_opts->preprocessed)
    {
      size_t i;

      cb_file_change (parse_in,
		      linemap_add (&line_table, LC_RENAME, 0,
				   _("<built-in>"), 0));

      cpp_init_builtins (parse_in, flag_hosted);
      c_cpp_builtins (parse_in);

      /* We're about to send user input to cpplib, so make it warn for
	 things that we previously (when we sent it internal definitions)
	 told it to not warn.

	 C99 permits implementation-defined characters in identifiers.
	 The documented meaning of -std= is to turn off extensions that
	 conflict with the specified standard, and since a strictly
	 conforming program cannot contain a '$', we do not condition
	 their acceptance on the -std= setting.  */
      cpp_opts->warn_dollars = (cpp_opts->pedantic && !cpp_opts->c99);

      cb_file_change (parse_in,
		      linemap_add (&line_table, LC_RENAME, 0,
				   _("<command-line>"), 0));

      for (i = 0; i < deferred_count; i++)
	{
	  struct deferred_opt *opt = &deferred_opts[i];

	  if (opt->code == OPT_D)
	    cpp_define (parse_in, opt->arg);
	  else if (opt->code == OPT_U)
	    cpp_undef (parse_in, opt->arg);
	  else if (opt->code == OPT_A)
	    {
	      if (opt->arg[0] == '-')
		cpp_unassert (parse_in, opt->arg + 1);
	      else
		cpp_assert (parse_in, opt->arg);
	    }
	}

      /* Handle -imacros after -D and -U.  */
      for (i = 0; i < deferred_count; i++)
	{
	  struct deferred_opt *opt = &deferred_opts[i];

	  if (opt->code == OPT_imacros
	      && cpp_push_include (parse_in, opt->arg))
	    {
	      /* Disable push_command_line_include callback for now.  */
	      include_cursor = deferred_count + 1;
	      cpp_scan_nooutput (parse_in);
	    }
	}
    }
  else if (cpp_opts->directives_only)
    cpp_init_special_builtins (parse_in);

  include_cursor = 0;
  push_command_line_include ();
}

/* Give CPP the next file given by -include, if any.  */
static void
push_command_line_include (void)
{
  while (include_cursor < deferred_count)
    {
      struct deferred_opt *opt = &deferred_opts[include_cursor++];

      if (!cpp_opts->preprocessed && opt->code == OPT_include
	  && cpp_push_include (parse_in, opt->arg))
	return;
    }

  if (include_cursor == deferred_count)
    {
      include_cursor++;
      /* -Wunused-macros should only warn about macros defined hereafter.  */
      cpp_opts->warn_unused_macros = warn_unused_macros;
      /* Restore the line map from <command line>.  */
      if (!cpp_opts->preprocessed)
	cpp_change_file (parse_in, LC_RENAME, this_input_filename);

      /* Set this here so the client can change the option if it wishes,
	 and after stacking the main file so we don't trace the main file.  */
      line_table.trace_includes = cpp_opts->print_include_names;
    }
}

/* File change callback.  Has to handle -include files.  */
static void
cb_file_change (cpp_reader * ARG_UNUSED (pfile),
		const struct line_map *new_map)
{
  if (flag_preprocess_only)
    pp_file_change (new_map);
  else
    fe_file_change (new_map);

  if (new_map == 0 || (new_map->reason == LC_LEAVE && MAIN_FILE_P (new_map)))
    push_command_line_include ();
}

void
cb_dir_change (cpp_reader * ARG_UNUSED (pfile), const char *dir)
{
  if (!set_src_pwd (dir))
    warning (0, "too late for # directive to set debug directory");
}

/* Set the C 89 standard (with 1994 amendments if C94, without GNU
   extensions if ISO).  There is no concept of gnu94.  */
static void
set_std_c89 (int c94, int iso)
{
  cpp_set_lang (parse_in, c94 ? CLK_STDC94: iso ? CLK_STDC89: CLK_GNUC89);
  flag_iso = iso;
  flag_no_asm = iso;
  flag_no_gnu_keywords = iso;
  flag_no_nonansi_builtin = iso;
  flag_isoc94 = c94;
  flag_isoc99 = 0;
}

/* Set the C 99 standard (without GNU extensions if ISO).  */
static void
set_std_c99 (int iso)
{
  cpp_set_lang (parse_in, iso ? CLK_STDC99: CLK_GNUC99);
  flag_no_asm = iso;
  flag_no_nonansi_builtin = iso;
  flag_iso = iso;
  flag_isoc99 = 1;
  flag_isoc94 = 1;
}

/* Set the C++ 98 standard (without GNU extensions if ISO).  */
static void
set_std_cxx98 (int iso)
{
  cpp_set_lang (parse_in, iso ? CLK_CXX98: CLK_GNUCXX);
  flag_no_gnu_keywords = iso;
  flag_no_nonansi_builtin = iso;
  flag_iso = iso;
}

/* Handle setting implicit to ON.  */
static void
set_Wimplicit (int on)
{
  warn_implicit = on;
  warn_implicit_int = on;
  if (on)
    {
      if (mesg_implicit_function_declaration != 2)
	mesg_implicit_function_declaration = 1;
    }
  else
    mesg_implicit_function_declaration = 0;
}

/* Args to -d specify what to dump.  Silently ignore
   unrecognized options; they may be aimed at toplev.c.  */
static void
handle_OPT_d (const char *arg)
{
  char c;

  while ((c = *arg++) != '\0')
    switch (c)
      {
      case 'M':			/* Dump macros only.  */
      case 'N':			/* Dump names.  */
      case 'D':			/* Dump definitions.  */
	flag_dump_macros = c;
	break;

      case 'I':
	flag_dump_includes = 1;
	break;
      }
}
