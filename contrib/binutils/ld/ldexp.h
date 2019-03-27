/* ldexp.h -
   Copyright 1991, 1992, 1993, 1994, 1995, 1998, 1999, 2000, 2001, 2002,
   2003, 2004, 2005 Free Software Foundation, Inc.

   This file is part of GLD, the Gnu Linker.

   GLD is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   GLD is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with GLD; see the file COPYING.  If not, write to the Free
   Software Foundation, 51 Franklin Street - Fifth Floor, Boston, MA
   02110-1301, USA.  */

#ifndef LDEXP_H
#define LDEXP_H

/* The result of an expression tree */
typedef struct {
  bfd_vma value;
  char *str;
  asection *section;
  bfd_boolean valid_p;
} etree_value_type;

typedef struct {
  int node_code;
  unsigned int lineno;
  enum {
    etree_binary,
    etree_trinary,
    etree_unary,
    etree_name,
    etree_assign,
    etree_provide,
    etree_provided,
    etree_value,
    etree_assert,
    etree_rel
  } node_class;
} node_type;

typedef union etree_union {
  node_type type;
  struct {
    node_type type;
    union etree_union *lhs;
    union etree_union *rhs;
  } binary;
  struct {
    node_type type;
    union etree_union *cond;
    union etree_union *lhs;
    union etree_union *rhs;
  } trinary;
  struct {
    node_type type;
    const char *dst;
    union etree_union *src;
    bfd_boolean hidden;
  } assign;
  struct {
    node_type type;
    union etree_union *child;
  } unary;
  struct {
    node_type type;
    const char *name;
  } name;
  struct {
    node_type type;
    bfd_vma value;
    char *str;
  } value;
  struct {
    node_type type;
    asection *section;
    bfd_vma value;
  } rel;
  struct {
    node_type type;
    union etree_union *child;
    const char *message;
  } assert_s;
} etree_type;

typedef enum {
  lang_first_phase_enum,
  lang_mark_phase_enum,
  lang_allocating_phase_enum,
  lang_final_phase_enum
} lang_phase_type;

struct ldexp_control {
  /* Modify expression evaluation depending on this.  */
  lang_phase_type phase;

  /* Principally used for diagnostics.  */
  bfd_boolean assigning_to_dot;

  /* Working results.  */
  etree_value_type result;
  bfd_vma dot;

  /* Current dot and section passed to ldexp folder.  */
  bfd_vma *dotp;
  asection *section;

  /* State machine and results for DATASEG.  */
  struct {
    enum {
      exp_dataseg_none,
      exp_dataseg_align_seen,
      exp_dataseg_relro_seen,
      exp_dataseg_end_seen,
      exp_dataseg_relro_adjust,
      exp_dataseg_adjust
    } phase;

    bfd_vma base, min_base, relro_end, end, pagesize, maxpagesize;
  } dataseg;
};

extern struct ldexp_control expld;

/* A maps from a segment name to a base address.  */
typedef struct segment_struct {
  /* The next segment in the linked list.  */
  struct segment_struct *next;
  /* The name of the sgement.  */
  const char *name;
  /* The base address for the segment.  */
  bfd_vma value;
  /* True if a SEGMENT_START directive corresponding to this segment
     has been seen.  */
  bfd_boolean used;
} segment_type;

/* The segments specified by the user on the command-line.  */
extern segment_type *segments;

typedef struct _fill_type fill_type;

etree_type *exp_intop
  (bfd_vma);
etree_type *exp_bigintop
  (bfd_vma, char *);
etree_type *exp_relop
  (asection *, bfd_vma);
void exp_fold_tree
  (etree_type *, asection *, bfd_vma *);
etree_type *exp_binop
  (int, etree_type *, etree_type *);
etree_type *exp_trinop
  (int,etree_type *, etree_type *, etree_type *);
etree_type *exp_unop
  (int, etree_type *);
etree_type *exp_nameop
  (int, const char *);
etree_type *exp_assop
  (int, const char *, etree_type *);
etree_type *exp_provide
  (const char *, etree_type *, bfd_boolean);
etree_type *exp_assert
  (etree_type *, const char *);
void exp_print_tree
  (etree_type *);
bfd_vma exp_get_vma
  (etree_type *, bfd_vma, char *);
int exp_get_value_int
  (etree_type *, int, char *);
fill_type *exp_get_fill
  (etree_type *, fill_type *, char *);
bfd_vma exp_get_abs_int
  (etree_type *, int, char *);

#endif
