/* Definitions for transformations based on profile information for values.
   Copyright (C) 2003, 2004, 2005 Free Software Foundation, Inc.

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

#ifndef GCC_VALUE_PROF_H
#define GCC_VALUE_PROF_H

/* Supported histogram types.  */
enum hist_type
{
  HIST_TYPE_INTERVAL,	/* Measures histogram of values inside a specified
			   interval.  */
  HIST_TYPE_POW2,	/* Histogram of power of 2 values.  */
  HIST_TYPE_SINGLE_VALUE, /* Tries to identify the value that is (almost)
			   always constant.  */
  HIST_TYPE_CONST_DELTA	/* Tries to identify the (almost) always constant
			   difference between two evaluations of a value.  */
};

#define COUNTER_FOR_HIST_TYPE(TYPE) ((int) (TYPE) + GCOV_FIRST_VALUE_COUNTER)
#define HIST_TYPE_FOR_COUNTER(COUNTER) \
  ((enum hist_type) ((COUNTER) - GCOV_FIRST_VALUE_COUNTER))

/* The value to measure.  */
struct histogram_value_t
{
  struct
    {
      tree value;		/* The value to profile.  */
      tree stmt;		/* Insn containing the value.  */
      gcov_type *counters;		        /* Pointer to first counter.  */
      struct histogram_value_t *next;		/* Linked list pointer.  */
    } hvalue;
  enum hist_type type;			/* Type of information to measure.  */
  unsigned n_counters;			/* Number of required counters.  */
  union
    {
      struct
	{
	  int int_start;	/* First value in interval.  */
	  unsigned int steps;	/* Number of values in it.  */
	} intvl;	/* Interval histogram data.  */
    } hdata;		/* Profiled information specific data.  */
};

typedef struct histogram_value_t *histogram_value;

DEF_VEC_P(histogram_value);
DEF_VEC_ALLOC_P(histogram_value,heap);

typedef VEC(histogram_value,heap) *histogram_values;

/* Hooks registration.  */
extern void tree_register_value_prof_hooks (void);

/* IR-independent entry points.  */
extern void find_values_to_profile (histogram_values *);
extern bool value_profile_transformations (void);

/* External declarations for edge-based profiling.  */
struct profile_hooks {

  /* Insert code to initialize edge profiler.  */
  void (*init_edge_profiler) (void);

  /* Insert code to increment an edge count.  */
  void (*gen_edge_profiler) (int, edge);

  /* Insert code to increment the interval histogram counter.  */
  void (*gen_interval_profiler) (histogram_value, unsigned, unsigned);

  /* Insert code to increment the power of two histogram counter.  */
  void (*gen_pow2_profiler) (histogram_value, unsigned, unsigned);

  /* Insert code to find the most common value.  */
  void (*gen_one_value_profiler) (histogram_value, unsigned, unsigned);

  /* Insert code to find the most common value of a difference between two
     evaluations of an expression.  */
  void (*gen_const_delta_profiler) (histogram_value, unsigned, unsigned);
};

/* In profile.c.  */
extern void init_branch_prob (void);
extern void branch_prob (void);
extern void end_branch_prob (void);
extern void tree_register_profile_hooks (void);

/* In tree-profile.c.  */
extern struct profile_hooks tree_profile_hooks;

#endif	/* GCC_VALUE_PROF_H */

