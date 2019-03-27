/* Command line option handling.
   Copyright (C) 2002, 2003, 2004, 2005 Free Software Foundation, Inc.

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

#ifndef GCC_OPTS_H
#define GCC_OPTS_H

/* Specifies how a switch's VAR_VALUE relates to its FLAG_VAR.  */
enum cl_var_type {
  /* The switch is enabled when FLAG_VAR is nonzero.  */
  CLVC_BOOLEAN,

  /* The switch is enabled when FLAG_VAR == VAR_VALUE.  */
  CLVC_EQUAL,

  /* The switch is enabled when VAR_VALUE is not set in FLAG_VAR.  */
  CLVC_BIT_CLEAR,

  /* The switch is enabled when VAR_VALUE is set in FLAG_VAR.  */
  CLVC_BIT_SET,

  /* The switch takes a string argument and FLAG_VAR points to that
     argument.  */
  CLVC_STRING
};

struct cl_option
{
  const char *opt_text;
  const char *help;
  unsigned short back_chain;
  unsigned char opt_len;
  int neg_index;
  unsigned int flags;
  void *flag_var;
  enum cl_var_type var_type;
  int var_value;
};

/* Records that the state of an option consists of SIZE bytes starting
   at DATA.  DATA might point to CH in some cases.  */
struct cl_option_state {
  const void *data;
  size_t size;
  char ch;
};

extern const struct cl_option cl_options[];
extern const unsigned int cl_options_count;
extern const char *const lang_names[];
extern bool no_unit_at_a_time_default;

#define CL_DISABLED		(1 << 21) /* Disabled in this configuration.  */
#define CL_TARGET		(1 << 22) /* Target-specific option.  */
#define CL_REPORT		(1 << 23) /* Report argument with -fverbose-asm  */
#define CL_JOINED		(1 << 24) /* If takes joined argument.  */
#define CL_SEPARATE		(1 << 25) /* If takes a separate argument.  */
#define CL_REJECT_NEGATIVE	(1 << 26) /* Reject no- form.  */
#define CL_MISSING_OK		(1 << 27) /* Missing argument OK (joined).  */
#define CL_UINTEGER		(1 << 28) /* Argument is an integer >=0.  */
#define CL_COMMON		(1 << 29) /* Language-independent.  */
#define CL_UNDOCUMENTED		(1 << 30) /* Do not output with --help.  */

/* Input file names.  */

extern const char **in_fnames;

/* The count of input filenames.  */

extern unsigned num_in_fnames;

size_t find_opt (const char *input, int lang_mask);
extern void prune_options (int *argcp, char ***argvp);
extern void decode_options (unsigned int argc, const char **argv);
extern int option_enabled (int opt_idx);
extern bool get_option_state (int, struct cl_option_state *);

#endif
