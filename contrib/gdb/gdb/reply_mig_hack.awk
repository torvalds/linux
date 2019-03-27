# Reply server mig-output massager
#
#   Copyright 1995, 1996, 1999 Free Software Foundation, Inc.
#
#   Written by Miles Bader <miles@gnu.ai.mit.edu>
#
#   This program is free software; you can redistribute it and/or
#   modify it under the terms of the GNU General Public License as
#   published by the Free Software Foundation; either version 2, or (at
#   your option) any later version.
#
#   This program is distributed in the hope that it will be useful, but
#   WITHOUT ANY WARRANTY; without even the implied warranty of
#   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
#   General Public License for more details.
#
#   You should have received a copy of the GNU General Public License
#   along with this program; if not, write to the Free Software
#   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
#
# This awk script hacks the output of mig-generated reply server code
# so that it allows replies with just the error-code in them (as this is
# how mig returns errors).
#
# It is highly, highly, dependent on the exact format of mig output.  Ick.
#

BEGIN { parse_phase = 0; }

/^}/ { parse_phase = 0; }

parse_phase == 0 && /^mig_internal void _X[a-zA-Z0-9_]*_reply/ {
  # The start of a mig server routine.  Reset everything.  Note that we only
  # mess with rpcs that have the suffix `_reply'.
  num_args = 0;
  num_checks = 0;
  parse_phase = 1;
  print; next;
}

parse_phase == 1 && /^[\t ]*typedef struct/ {
  # The first structure in the server routine should describe the arguments
  parse_phase = 2;
  print; next;
}

parse_phase == 2 {
  # The message header field in the args structure, which skip.
  parse_phase = 3;
  print; next;
}

parse_phase == 3 && /}/ {
  # The args structure is over.
  if (num_args > 1)
    parse_phase = 5;
  else
    # There's no extra args that could screw up the normal mechanism for
    # error returns, so we don't have to insert any new code.
    parse_phase = 0;
  print; next;
}

parse_phase == 3 {
  # The type field for an argument.
  arg_type_code_name[num_args] = $2;
  sub (/;$/, "", arg_type_code_name[num_args]) # Get rid of the semi-colon
  parse_phase = 4;
  print; next;
}

parse_phase == 4 {
  # The value field for an argument.
  arg_name[num_args] = $2;
  sub (/;$/, "", arg_name[num_args]) # Get rid of the semi-colon
  arg_type[num_args] = $1;
  num_args++;
  parse_phase = 3;
  print; next;
}

parse_phase == 5 && /^[ \t]*static const mach_msg_type_t/ {
  # The type check structure for an argument.
  arg_check_name[num_checks] = $4;
  num_checks++;
  print; next;
}

parse_phase == 5 && /^[ \t]*mig_external kern_return_t/ {
  # The declaration of the user server function for this rpc.
  user_function_name = $3;
  print; next;
}

parse_phase == 5 && /^#if[ \t]TypeCheck/ {
  # The first args type checking statement; we need to insert our chunk of
  # code that bypasses all the type checks if this is an error return, after
  # which we're done until we get to the next function.  Handily, the size
  # of mig's Reply structure is also the size of the alternate Request 
  # structure that we want to check for.
  print "\tif (In0P->Head.msgh_size == sizeof (Reply)";
  print "\t    && ! (In0P->Head.msgh_bits & MACH_MSGH_BITS_COMPLEX)";
  print "\t    && *(int *)&In0P->" arg_type_code_name[0] " == *(int *)&" arg_check_name[0];
  print "\t    && In0P->" arg_name[0] " != 0)";
  print "\t  /* Error return, only the error code argument is passed.  */";
  print "\t  {";
  # Force the function into a type that only takes the first two args, via
  # the temp variable SFUN (is there another way to correctly do this cast?).
  # This is possibly bogus, but easier than supplying bogus values for all
  # the other args (we can't just pass 0 for them, as they might not be scalar).
  printf ("\t    kern_return_t (*sfun)(mach_port_t");
  for (i = 0; i < num_args; i++)
    printf (", %s", arg_type[i]);
  printf (") = %s;\n", user_function_name);
  print "\t    OutP->RetCode = (*(kern_return_t (*)(mach_port_t, kern_return_t))sfun) (In0P->Head.msgh_request_port, In0P->" arg_name[0] ");";
  print "\t    return;";
  print "\t  }";
  print "";
  parse_phase = 0;
  print; next;
}

{ print; }
