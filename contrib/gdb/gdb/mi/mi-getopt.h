/* MI Option Parser.
   Copyright 2000 Free Software Foundation, Inc.
   Contributed by Cygnus Solutions (a Red Hat company).

   This file is part of GDB.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.  */

#ifndef MI_GETOPT_H
#define MI_GETOPT_H

/* Like getopt() but with simpler semantics.

   An option has the form ``-<name>''. The special option ``--''
   denotes the end of the option list. An option can be followed by a
   separate argument (on a per option basis).

   On entry OPTIND contains the index of the next element of ARGV that
   needs parsing.  OPTIND is updated to indicate the index of the next
   argument before mi_getopt() returns.

   If ARGV[OPTIND] is an option, that options INDEX is returned.
   OPTARG is set to the options argument or NULL.  OPTIND is updated.

   If ARGV[OPTIND] is not an option, -1 is returned and OPTIND updated
   to specify the non-option argument.  OPTARG is set to NULL.

   mi_getopt() calls ``error("%s: Unknown option %c", prefix,
   option)'' if an unknown option is encountered. */

struct mi_opt;
extern int mi_getopt (const char *prefix, int argc, char **argv,
		      struct mi_opt *opt, int *optind, char **optarg);

/* The option list.  Terminated by NAME==NULL.  ARG_P that the option
   requires an argument.  INDEX is returned to identify th option. */

struct mi_opt
  {
    const char *name;
    int index;
    int arg_p;
  };

struct mi_opt;

/* mi_valid_noargs
   
   Determines if ARGC/ARGV are a valid set of parameters to satisfy 
   an MI function that is not supposed to recieve any arguments.
   
   An MI function that should not recieve arguments can still be 
   passed parameters after the special option '--' such as below.

   Example: The MI function -exec-run takes no args.
   However, the client may pass '-exec-run -- -a ...'
   See PR-783

   PREFIX is passed to mi_getopt for an error message.
    
   This function Returns 1 if the parameter pair ARGC/ARGV are valid
   for an MI function that takes no arguments. Otherwise, it returns 0
   and the appropriate error message is displayed by mi_getopt.  */

extern int mi_valid_noargs (const char *prefix, int argc, char **argv);
				
#endif
