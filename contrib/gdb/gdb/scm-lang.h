/* Scheme/Guile language support routines for GDB, the GNU debugger.

   Copyright 1995, 1996, 1998, 1999, 2000, 2003 Free Software
   Foundation, Inc.

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

#define SICP
#include "scm-tags.h"
#undef SCM_NCELLP
#define SCM_NCELLP(x) 	((SCM_SIZE-1) & (int)(x))
#define SCM_ITAG8_DATA(X)	((X)>>8)
#define SCM_ICHR(x)	((unsigned char)SCM_ITAG8_DATA(x))
#define SCM_ICHRP(x)    (SCM_ITAG8(x) == scm_tc8_char)
#define scm_tc8_char 0xf4
#define SCM_IFLAGP(n)            ((0x87 & (int)(n))==4)
#define SCM_ISYMNUM(n)           ((int)((n)>>9))
#define SCM_ISYMCHARS(n)         (scm_isymnames[SCM_ISYMNUM(n)])
#define SCM_ILOCP(n)             ((0xff & (int)(n))==0xfc)
#define SCM_ITAG8(X)             ((int)(X) & 0xff)
#define SCM_TYP7(x)              (0x7f & (int)SCM_CAR(x))
#define SCM_LENGTH(x) (((unsigned long)SCM_CAR(x))>>8)
#define SCM_NCONSP(x) (1 & (int)SCM_CAR(x))
#define SCM_NECONSP(x) (SCM_NCONSP(x) && (1 != SCM_TYP3(x)))
#define SCM_CAR(x) scm_get_field (x, 0)
#define SCM_CDR(x) scm_get_field (x, 1)
#define SCM_VELTS(x) ((SCM *)SCM_CDR(x))
#define SCM_CLOSCAR(x) (SCM_CAR(x)-scm_tc3_closure)
#define SCM_CODE(x) SCM_CAR(SCM_CLOSCAR (x))
#define SCM_MAKINUM(x) (((x)<<2)+2L)

/* Forward decls for prototypes */
struct value;

extern int scm_value_print (struct value *, struct ui_file *,
			    int, enum val_prettyprint);

extern int scm_val_print (struct type *, char *, int, CORE_ADDR,
			  struct ui_file *, int, int, int,
			  enum val_prettyprint);

extern LONGEST scm_get_field (LONGEST, int);

extern void scm_scmval_print (LONGEST, struct ui_file *, int, int, int,
			      enum val_prettyprint);

extern int is_scmvalue_type (struct type *);

extern void scm_printchar (int, struct ui_file *);

extern struct value *scm_evaluate_string (char *, int);

extern struct type *builtin_type_scm;

extern int scm_parse (void);

extern LONGEST scm_unpack (struct type *, const char *, enum type_code);
