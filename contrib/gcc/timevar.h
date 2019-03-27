/* Timing variables for measuring compiler performance.
   Copyright (C) 2000, 2003, 2004, 2005 Free Software Foundation, Inc.
   Contributed by Alex Samuel <samuel@codesourcery.com>

   This file is part of GCC.

   GCC is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   GCC is distributed in the hope that it will be useful, but WITHOUT
   ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
   or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public
   License for more details.

   You should have received a copy of the GNU General Public License
   along with GCC; see the file COPYING.  If not, write to the Free
   Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA
   02110-1301, USA.  */

#ifndef GCC_TIMEVAR_H
#define GCC_TIMEVAR_H

/* Timing variables are used to measure elapsed time in various
   portions of the compiler.  Each measures elapsed user, system, and
   wall-clock time, as appropriate to and supported by the host
   system.

   Timing variables are defined using the DEFTIMEVAR macro in
   timevar.def.  Each has an enumeral identifier, used when referring
   to the timing variable in code, and a character string name.

   Timing variables can be used in two ways:

     - On the timing stack, using timevar_push and timevar_pop.
       Timing variables may be pushed onto the stack; elapsed time is
       attributed to the topmost timing variable on the stack.  When
       another variable is pushed on, the previous topmost variable is
       `paused' until the pushed variable is popped back off.

     - As a standalone timer, using timevar_start and timevar_stop.
       All time elapsed between the two calls is attributed to the
       variable.
*/

/* This structure stores the various varieties of time that can be
   measured.  Times are stored in seconds.  The time may be an
   absolute time or a time difference; in the former case, the time
   base is undefined, except that the difference between two times
   produces a valid time difference.  */

struct timevar_time_def
{
  /* User time in this process.  */
  double user;

  /* System time (if applicable for this host platform) in this
     process.  */
  double sys;

  /* Wall clock time.  */
  double wall;

  /* Garbage collector memory.  */
  unsigned ggc_mem;
};

/* An enumeration of timing variable identifiers.  Constructed from
   the contents of timevar.def.  */

#define DEFTIMEVAR(identifier__, name__) \
    identifier__,
typedef enum
{
#include "timevar.def"
  TIMEVAR_LAST
}
timevar_id_t;
#undef DEFTIMEVAR

/* Execute the sequence: timevar_pop (TV), return (E);  */
#define POP_TIMEVAR_AND_RETURN(TV, E)  do { timevar_pop (TV); return (E); }while(0)
#define timevar_pop(TV) do { if (timevar_enable) timevar_pop_1 (TV); }while(0)
#define timevar_push(TV) do { if (timevar_enable) timevar_push_1 (TV); }while(0)

extern void timevar_init (void);
extern void timevar_push_1 (timevar_id_t);
extern void timevar_pop_1 (timevar_id_t);
extern void timevar_start (timevar_id_t);
extern void timevar_stop (timevar_id_t);
extern void timevar_print (FILE *);

/* Provided for backward compatibility.  */
extern void print_time (const char *, long);

extern bool timevar_enable;

extern size_t timevar_ggc_mem_total;

#endif /* ! GCC_TIMEVAR_H */
