/* Register Transfer Language (RTL) definitions for GCC
   Copyright (C) 1987, 1991, 1992, 1993, 1994, 1995, 1996, 1997, 1998, 1999,
   2000, 2001, 2002, 2003, 2004, 2005, 2006 Free Software Foundation, Inc.

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

#ifndef GCC_RTL_H
#define GCC_RTL_H

#include <sys/param.h>
#ifndef __PAST_END
# define __PAST_END(array, offset) (((typeof(*(array)) *)(array))[offset])
#endif

#include "statistics.h"
#include "machmode.h"
#include "input.h"
#include "real.h"
#include "vec.h"

#undef FFS  /* Some systems predefine this symbol; don't let it interfere.  */
#undef FLOAT /* Likewise.  */
#undef ABS /* Likewise.  */
#undef PC /* Likewise.  */

/* Value used by some passes to "recognize" noop moves as valid
 instructions.  */
#define NOOP_MOVE_INSN_CODE	INT_MAX

/* Register Transfer Language EXPRESSIONS CODES */

#define RTX_CODE	enum rtx_code
enum rtx_code  {

#define DEF_RTL_EXPR(ENUM, NAME, FORMAT, CLASS)   ENUM ,
#include "rtl.def"		/* rtl expressions are documented here */
#undef DEF_RTL_EXPR

  LAST_AND_UNUSED_RTX_CODE};	/* A convenient way to get a value for
				   NUM_RTX_CODE.
				   Assumes default enum value assignment.  */

#define NUM_RTX_CODE ((int) LAST_AND_UNUSED_RTX_CODE)
				/* The cast here, saves many elsewhere.  */

/* Register Transfer Language EXPRESSIONS CODE CLASSES */

enum rtx_class  {
  /* We check bit 0-1 of some rtx class codes in the predicates below.  */

  /* Bit 0 = comparison if 0, arithmetic is 1
     Bit 1 = 1 if commutative.  */
  RTX_COMPARE,		/* 0 */
  RTX_COMM_COMPARE,
  RTX_BIN_ARITH,
  RTX_COMM_ARITH,

  /* Must follow the four preceding values.  */
  RTX_UNARY,		/* 4 */

  RTX_EXTRA,
  RTX_MATCH,
  RTX_INSN,

  /* Bit 0 = 1 if constant.  */
  RTX_OBJ,		/* 8 */
  RTX_CONST_OBJ,

  RTX_TERNARY,
  RTX_BITFIELD_OPS,
  RTX_AUTOINC
};

#define RTX_OBJ_MASK (~1)
#define RTX_OBJ_RESULT (RTX_OBJ & RTX_OBJ_MASK)
#define RTX_COMPARE_MASK (~1)
#define RTX_COMPARE_RESULT (RTX_COMPARE & RTX_COMPARE_MASK)
#define RTX_ARITHMETIC_MASK (~1)
#define RTX_ARITHMETIC_RESULT (RTX_COMM_ARITH & RTX_ARITHMETIC_MASK)
#define RTX_BINARY_MASK (~3)
#define RTX_BINARY_RESULT (RTX_COMPARE & RTX_BINARY_MASK)
#define RTX_COMMUTATIVE_MASK (~2)
#define RTX_COMMUTATIVE_RESULT (RTX_COMM_COMPARE & RTX_COMMUTATIVE_MASK)
#define RTX_NON_COMMUTATIVE_RESULT (RTX_COMPARE & RTX_COMMUTATIVE_MASK)

extern const unsigned char rtx_length[NUM_RTX_CODE];
#define GET_RTX_LENGTH(CODE)		(rtx_length[(int) (CODE)])

extern const char * const rtx_name[NUM_RTX_CODE];
#define GET_RTX_NAME(CODE)		(rtx_name[(int) (CODE)])

extern const char * const rtx_format[NUM_RTX_CODE];
#define GET_RTX_FORMAT(CODE)		(rtx_format[(int) (CODE)])

extern const enum rtx_class rtx_class[NUM_RTX_CODE];
#define GET_RTX_CLASS(CODE)		(rtx_class[(int) (CODE)])

extern const unsigned char rtx_code_size[NUM_RTX_CODE];
extern const unsigned char rtx_next[NUM_RTX_CODE];

/* The flags and bitfields of an ADDR_DIFF_VEC.  BASE is the base label
   relative to which the offsets are calculated, as explained in rtl.def.  */
typedef struct
{
  /* Set at the start of shorten_branches - ONLY WHEN OPTIMIZING - : */
  unsigned min_align: 8;
  /* Flags: */
  unsigned base_after_vec: 1; /* BASE is after the ADDR_DIFF_VEC.  */
  unsigned min_after_vec: 1;  /* minimum address target label is
				 after the ADDR_DIFF_VEC.  */
  unsigned max_after_vec: 1;  /* maximum address target label is
				 after the ADDR_DIFF_VEC.  */
  unsigned min_after_base: 1; /* minimum address target label is
				 after BASE.  */
  unsigned max_after_base: 1; /* maximum address target label is
				 after BASE.  */
  /* Set by the actual branch shortening process - ONLY WHEN OPTIMIZING - : */
  unsigned offset_unsigned: 1; /* offsets have to be treated as unsigned.  */
  unsigned : 2;
  unsigned scale : 8;
} addr_diff_vec_flags;

/* Structure used to describe the attributes of a MEM.  These are hashed
   so MEMs that the same attributes share a data structure.  This means
   they cannot be modified in place.  If any element is nonzero, it means
   the value of the corresponding attribute is unknown.  */
/* ALIGN and SIZE are the alignment and size of the MEM itself,
   while EXPR can describe a larger underlying object, which might have a
   stricter alignment; OFFSET is the offset of the MEM within that object.  */
typedef struct mem_attrs GTY(())
{
  HOST_WIDE_INT alias;		/* Memory alias set.  */
  tree expr;			/* expr corresponding to MEM.  */
  rtx offset;			/* Offset from start of DECL, as CONST_INT.  */
  rtx size;			/* Size in bytes, as a CONST_INT.  */
  unsigned int align;		/* Alignment of MEM in bits.  */
} mem_attrs;

/* Structure used to describe the attributes of a REG in similar way as
   mem_attrs does for MEM above.  */

typedef struct reg_attrs GTY(())
{
  tree decl;			/* decl corresponding to REG.  */
  HOST_WIDE_INT offset;		/* Offset from start of DECL.  */
} reg_attrs;

/* Common union for an element of an rtx.  */

union rtunion_def
{
  int rt_int;
  unsigned int rt_uint;
  const char *rt_str;
  rtx rt_rtx;
  rtvec rt_rtvec;
  enum machine_mode rt_type;
  addr_diff_vec_flags rt_addr_diff_vec_flags;
  struct cselib_val_struct *rt_cselib;
  struct bitmap_head_def *rt_bit;
  tree rt_tree;
  struct basic_block_def *rt_bb;
  mem_attrs *rt_mem;
  reg_attrs *rt_reg;
  struct constant_descriptor_rtx *rt_constant;
};
typedef union rtunion_def rtunion;

/* This structure remembers the position of a SYMBOL_REF within an
   object_block structure.  A SYMBOL_REF only provides this information
   if SYMBOL_REF_HAS_BLOCK_INFO_P is true.  */
struct block_symbol GTY(()) {
  /* The usual SYMBOL_REF fields.  */
  rtunion GTY ((skip)) fld[3];

  /* The block that contains this object.  */
  struct object_block *block;

  /* The offset of this object from the start of its block.  It is negative
     if the symbol has not yet been assigned an offset.  */
  HOST_WIDE_INT offset;
};

DEF_VEC_P(rtx);
DEF_VEC_ALLOC_P(rtx,heap);
DEF_VEC_ALLOC_P(rtx,gc);

/* Describes a group of objects that are to be placed together in such
   a way that their relative positions are known.  */
struct object_block GTY(())
{
  /* The section in which these objects should be placed.  */
  section *sect;

  /* The alignment of the first object, measured in bits.  */
  unsigned int alignment;

  /* The total size of the objects, measured in bytes.  */
  HOST_WIDE_INT size;

  /* The SYMBOL_REFs for each object.  The vector is sorted in
     order of increasing offset and the following conditions will
     hold for each element X:

	 SYMBOL_REF_HAS_BLOCK_INFO_P (X)
	 !SYMBOL_REF_ANCHOR_P (X)
	 SYMBOL_REF_BLOCK (X) == [address of this structure]
	 SYMBOL_REF_BLOCK_OFFSET (X) >= 0.  */
  VEC(rtx,gc) *objects;

  /* All the anchor SYMBOL_REFs used to address these objects, sorted
     in order of increasing offset, and then increasing TLS model.
     The following conditions will hold for each element X in this vector:

	 SYMBOL_REF_HAS_BLOCK_INFO_P (X)
	 SYMBOL_REF_ANCHOR_P (X)
	 SYMBOL_REF_BLOCK (X) == [address of this structure]
	 SYMBOL_REF_BLOCK_OFFSET (X) >= 0.  */
  VEC(rtx,gc) *anchors;
};

/* RTL expression ("rtx").  */

struct rtx_def GTY((chain_next ("RTX_NEXT (&%h)"),
		    chain_prev ("RTX_PREV (&%h)")))
{
  /* The kind of expression this is.  */
  ENUM_BITFIELD(rtx_code) code: 16;

  /* The kind of value the expression has.  */
  ENUM_BITFIELD(machine_mode) mode : 8;

  /* 1 in a MEM if we should keep the alias set for this mem unchanged
     when we access a component.
     1 in a CALL_INSN if it is a sibling call.
     1 in a SET that is for a return.
     In a CODE_LABEL, part of the two-bit alternate entry field.  */
  unsigned int jump : 1;
  /* In a CODE_LABEL, part of the two-bit alternate entry field.
     1 in a MEM if it cannot trap.  */
  unsigned int call : 1;
  /* 1 in a REG, MEM, or CONCAT if the value is set at most once, anywhere.
     1 in a SUBREG if it references an unsigned object whose mode has been
     from a promoted to a wider mode.
     1 in a SYMBOL_REF if it addresses something in the per-function
     constants pool.
     1 in a CALL_INSN, NOTE, or EXPR_LIST for a const or pure call.
     1 in a JUMP_INSN, CALL_INSN, or INSN of an annulling branch.  */
  unsigned int unchanging : 1;
  /* 1 in a MEM or ASM_OPERANDS expression if the memory reference is volatile.
     1 in an INSN, CALL_INSN, JUMP_INSN, CODE_LABEL, BARRIER, or NOTE
     if it has been deleted.
     1 in a REG expression if corresponds to a variable declared by the user,
     0 for an internally generated temporary.
     1 in a SUBREG with a negative value.
     1 in a LABEL_REF or in a REG_LABEL note for a non-local label.
     In a SYMBOL_REF, this flag is used for machine-specific purposes.  */
  unsigned int volatil : 1;
  /* 1 in a MEM referring to a field of an aggregate.
     0 if the MEM was a variable or the result of a * operator in C;
     1 if it was the result of a . or -> operator (on a struct) in C.
     1 in a REG if the register is used only in exit code a loop.
     1 in a SUBREG expression if was generated from a variable with a
     promoted mode.
     1 in a CODE_LABEL if the label is used for nonlocal gotos
     and must not be deleted even if its count is zero.
     1 in an INSN, JUMP_INSN or CALL_INSN if this insn must be scheduled
     together with the preceding insn.  Valid only within sched.
     1 in an INSN, JUMP_INSN, or CALL_INSN if insn is in a delay slot and
     from the target of a branch.  Valid from reorg until end of compilation;
     cleared before used.  */
  unsigned int in_struct : 1;
  /* At the end of RTL generation, 1 if this rtx is used.  This is used for
     copying shared structure.  See `unshare_all_rtl'.
     In a REG, this is not needed for that purpose, and used instead
     in `leaf_renumber_regs_insn'.
     1 in a SYMBOL_REF, means that emit_library_call
     has used it as the function.  */
  unsigned int used : 1;
  /* 1 in an INSN or a SET if this rtx is related to the call frame,
     either changing how we compute the frame address or saving and
     restoring registers in the prologue and epilogue.
     1 in a REG or MEM if it is a pointer.
     1 in a SYMBOL_REF if it addresses something in the per-function
     constant string pool.  */
  unsigned frame_related : 1;
  /* 1 in a REG or PARALLEL that is the current function's return value.
     1 in a MEM if it refers to a scalar.
     1 in a SYMBOL_REF for a weak symbol.  */
  unsigned return_val : 1;

  /* The first element of the operands of this rtx.
     The number of operands and their types are controlled
     by the `code' field, according to rtl.def.  */
  union u {
    rtunion fld[1];
    HOST_WIDE_INT hwint[1];
    struct block_symbol block_sym;
    struct real_value rv;
  } GTY ((special ("rtx_def"), desc ("GET_CODE (&%0)"))) u;
};

/* The size in bytes of an rtx header (code, mode and flags).  */
#define RTX_HDR_SIZE offsetof (struct rtx_def, u)

/* The size in bytes of an rtx with code CODE.  */
#define RTX_CODE_SIZE(CODE) rtx_code_size[CODE]

#define NULL_RTX (rtx) 0

/* The "next" and "previous" RTX, relative to this one.  */

#define RTX_NEXT(X) (rtx_next[GET_CODE (X)] == 0 ? NULL			\
		     : *(rtx *)(((char *)X) + rtx_next[GET_CODE (X)]))

/* FIXME: the "NEXT_INSN (PREV_INSN (X)) == X" condition shouldn't be needed.
 */
#define RTX_PREV(X) ((INSN_P (X)       			\
                      || NOTE_P (X)       		\
                      || BARRIER_P (X)        		\
                      || LABEL_P (X))    		\
                     && PREV_INSN (X) != NULL           \
                     && NEXT_INSN (PREV_INSN (X)) == X  \
                     ? PREV_INSN (X) : NULL)

/* Define macros to access the `code' field of the rtx.  */

#define GET_CODE(RTX)	    ((enum rtx_code) (RTX)->code)
#define PUT_CODE(RTX, CODE) ((RTX)->code = (CODE))

#define GET_MODE(RTX)	    ((enum machine_mode) (RTX)->mode)
#define PUT_MODE(RTX, MODE) ((RTX)->mode = (MODE))

/* RTL vector.  These appear inside RTX's when there is a need
   for a variable number of things.  The principle use is inside
   PARALLEL expressions.  */

struct rtvec_def GTY(()) {
  int num_elem;		/* number of elements */
  rtx GTY ((length ("%h.num_elem"))) elem[1];
};

#define NULL_RTVEC (rtvec) 0

#define GET_NUM_ELEM(RTVEC)		((RTVEC)->num_elem)
#define PUT_NUM_ELEM(RTVEC, NUM)	((RTVEC)->num_elem = (NUM))

/* Predicate yielding nonzero iff X is an rtx for a register.  */
#define REG_P(X) (GET_CODE (X) == REG)

/* Predicate yielding nonzero iff X is an rtx for a memory location.  */
#define MEM_P(X) (GET_CODE (X) == MEM)

/* Predicate yielding nonzero iff X is an rtx for a constant integer.  */
#define CONST_INT_P(X) (GET_CODE (X) == CONST_INT)

/* Predicate yielding nonzero iff X is a label insn.  */
#define LABEL_P(X) (GET_CODE (X) == CODE_LABEL)

/* Predicate yielding nonzero iff X is a jump insn.  */
#define JUMP_P(X) (GET_CODE (X) == JUMP_INSN)

/* Predicate yielding nonzero iff X is a call insn.  */
#define CALL_P(X) (GET_CODE (X) == CALL_INSN)

/* Predicate yielding nonzero iff X is an insn that cannot jump.  */
#define NONJUMP_INSN_P(X) (GET_CODE (X) == INSN)

/* Predicate yielding nonzero iff X is a real insn.  */
#define INSN_P(X) \
  (NONJUMP_INSN_P (X) || JUMP_P (X) || CALL_P (X))

/* Predicate yielding nonzero iff X is a note insn.  */
#define NOTE_P(X) (GET_CODE (X) == NOTE)

/* Predicate yielding nonzero iff X is a barrier insn.  */
#define BARRIER_P(X) (GET_CODE (X) == BARRIER)

/* Predicate yielding nonzero iff X is a data for a jump table.  */
#define JUMP_TABLE_DATA_P(INSN) \
  (JUMP_P (INSN) && (GET_CODE (PATTERN (INSN)) == ADDR_VEC || \
		     GET_CODE (PATTERN (INSN)) == ADDR_DIFF_VEC))

/* 1 if X is a unary operator.  */

#define UNARY_P(X)   \
  (GET_RTX_CLASS (GET_CODE (X)) == RTX_UNARY)

/* 1 if X is a binary operator.  */

#define BINARY_P(X)   \
  ((GET_RTX_CLASS (GET_CODE (X)) & RTX_BINARY_MASK) == RTX_BINARY_RESULT)

/* 1 if X is an arithmetic operator.  */

#define ARITHMETIC_P(X)   \
  ((GET_RTX_CLASS (GET_CODE (X)) & RTX_ARITHMETIC_MASK)			\
    == RTX_ARITHMETIC_RESULT)

/* 1 if X is an arithmetic operator.  */

#define COMMUTATIVE_ARITH_P(X)   \
  (GET_RTX_CLASS (GET_CODE (X)) == RTX_COMM_ARITH)

/* 1 if X is a commutative arithmetic operator or a comparison operator.
   These two are sometimes selected together because it is possible to
   swap the two operands.  */

#define SWAPPABLE_OPERANDS_P(X)   \
  ((1 << GET_RTX_CLASS (GET_CODE (X)))					\
    & ((1 << RTX_COMM_ARITH) | (1 << RTX_COMM_COMPARE)			\
       | (1 << RTX_COMPARE)))

/* 1 if X is a non-commutative operator.  */

#define NON_COMMUTATIVE_P(X)   \
  ((GET_RTX_CLASS (GET_CODE (X)) & RTX_COMMUTATIVE_MASK)		\
    == RTX_NON_COMMUTATIVE_RESULT)

/* 1 if X is a commutative operator on integers.  */

#define COMMUTATIVE_P(X)   \
  ((GET_RTX_CLASS (GET_CODE (X)) & RTX_COMMUTATIVE_MASK)		\
    == RTX_COMMUTATIVE_RESULT)

/* 1 if X is a relational operator.  */

#define COMPARISON_P(X)   \
  ((GET_RTX_CLASS (GET_CODE (X)) & RTX_COMPARE_MASK) == RTX_COMPARE_RESULT)

/* 1 if X is a constant value that is an integer.  */

#define CONSTANT_P(X)   \
  (GET_RTX_CLASS (GET_CODE (X)) == RTX_CONST_OBJ)

/* 1 if X can be used to represent an object.  */
#define OBJECT_P(X)							\
  ((GET_RTX_CLASS (GET_CODE (X)) & RTX_OBJ_MASK) == RTX_OBJ_RESULT)

/* General accessor macros for accessing the fields of an rtx.  */

#if defined ENABLE_RTL_CHECKING && (GCC_VERSION >= 2007)
/* The bit with a star outside the statement expr and an & inside is
   so that N can be evaluated only once.  */
#define RTL_CHECK1(RTX, N, C1) __extension__				\
(*({ rtx const _rtx = (RTX); const int _n = (N);			\
     const enum rtx_code _code = GET_CODE (_rtx);			\
     if (_n < 0 || _n >= GET_RTX_LENGTH (_code))			\
       rtl_check_failed_bounds (_rtx, _n, __FILE__, __LINE__,		\
				__FUNCTION__);				\
     if (GET_RTX_FORMAT(_code)[_n] != C1)				\
       rtl_check_failed_type1 (_rtx, _n, C1, __FILE__, __LINE__,	\
			       __FUNCTION__);				\
     &_rtx->u.fld[_n]; }))

#define RTL_CHECK2(RTX, N, C1, C2) __extension__			\
(*({ rtx const _rtx = (RTX); const int _n = (N);			\
     const enum rtx_code _code = GET_CODE (_rtx);			\
     if (_n < 0 || _n >= GET_RTX_LENGTH (_code))			\
       rtl_check_failed_bounds (_rtx, _n, __FILE__, __LINE__,		\
				__FUNCTION__);				\
     if (GET_RTX_FORMAT(_code)[_n] != C1				\
	 && GET_RTX_FORMAT(_code)[_n] != C2)				\
       rtl_check_failed_type2 (_rtx, _n, C1, C2, __FILE__, __LINE__,	\
			       __FUNCTION__);				\
     &_rtx->u.fld[_n]; }))

#define RTL_CHECKC1(RTX, N, C) __extension__				\
(*({ rtx const _rtx = (RTX); const int _n = (N);			\
     if (GET_CODE (_rtx) != (C))					\
       rtl_check_failed_code1 (_rtx, (C), __FILE__, __LINE__,		\
			       __FUNCTION__);				\
     &_rtx->u.fld[_n]; }))

#define RTL_CHECKC2(RTX, N, C1, C2) __extension__			\
(*({ rtx const _rtx = (RTX); const int _n = (N);			\
     const enum rtx_code _code = GET_CODE (_rtx);			\
     if (_code != (C1) && _code != (C2))				\
       rtl_check_failed_code2 (_rtx, (C1), (C2), __FILE__, __LINE__,	\
			       __FUNCTION__); \
     &_rtx->u.fld[_n]; }))

#define RTVEC_ELT(RTVEC, I) __extension__				\
(*({ rtvec const _rtvec = (RTVEC); const int _i = (I);			\
     if (_i < 0 || _i >= GET_NUM_ELEM (_rtvec))				\
       rtvec_check_failed_bounds (_rtvec, _i, __FILE__, __LINE__,	\
				  __FUNCTION__);			\
     &_rtvec->elem[_i]; }))

#define XWINT(RTX, N) __extension__					\
(*({ rtx const _rtx = (RTX); const int _n = (N);			\
     const enum rtx_code _code = GET_CODE (_rtx);			\
     if (_n < 0 || _n >= GET_RTX_LENGTH (_code))			\
       rtl_check_failed_bounds (_rtx, _n, __FILE__, __LINE__,		\
				__FUNCTION__);				\
     if (GET_RTX_FORMAT(_code)[_n] != 'w')				\
       rtl_check_failed_type1 (_rtx, _n, 'w', __FILE__, __LINE__,	\
			       __FUNCTION__);				\
     &_rtx->u.hwint[_n]; }))

#define XCWINT(RTX, N, C) __extension__					\
(*({ rtx const _rtx = (RTX);						\
     if (GET_CODE (_rtx) != (C))					\
       rtl_check_failed_code1 (_rtx, (C), __FILE__, __LINE__,		\
			       __FUNCTION__);				\
     &_rtx->u.hwint[N]; }))

#define XCMWINT(RTX, N, C, M) __extension__				\
(*({ rtx const _rtx = (RTX);						\
     if (GET_CODE (_rtx) != (C) || GET_MODE (_rtx) != (M))		\
       rtl_check_failed_code_mode (_rtx, (C), (M), false, __FILE__,	\
				   __LINE__, __FUNCTION__);		\
     &_rtx->u.hwint[N]; }))

#define XCNMPRV(RTX, C, M) __extension__				\
({ rtx const _rtx = (RTX);						\
   if (GET_CODE (_rtx) != (C) || GET_MODE (_rtx) == (M))		\
     rtl_check_failed_code_mode (_rtx, (C), (M), true, __FILE__,	\
				 __LINE__, __FUNCTION__);		\
   &_rtx->u.rv; })

#define BLOCK_SYMBOL_CHECK(RTX) __extension__				\
({ rtx const _symbol = (RTX);						\
   unsigned int flags = RTL_CHECKC1 (_symbol, 1, SYMBOL_REF).rt_int;	\
   if ((flags & SYMBOL_FLAG_HAS_BLOCK_INFO) == 0)			\
     rtl_check_failed_block_symbol (__FILE__, __LINE__,			\
				    __FUNCTION__);			\
   &_symbol->u.block_sym; })

extern void rtl_check_failed_bounds (rtx, int, const char *, int,
				     const char *)
    ATTRIBUTE_NORETURN;
extern void rtl_check_failed_type1 (rtx, int, int, const char *, int,
				    const char *)
    ATTRIBUTE_NORETURN;
extern void rtl_check_failed_type2 (rtx, int, int, int, const char *,
				    int, const char *)
    ATTRIBUTE_NORETURN;
extern void rtl_check_failed_code1 (rtx, enum rtx_code, const char *,
				    int, const char *)
    ATTRIBUTE_NORETURN;
extern void rtl_check_failed_code2 (rtx, enum rtx_code, enum rtx_code,
				    const char *, int, const char *)
    ATTRIBUTE_NORETURN;
extern void rtl_check_failed_code_mode (rtx, enum rtx_code, enum machine_mode,
					bool, const char *, int, const char *)
    ATTRIBUTE_NORETURN;
extern void rtl_check_failed_block_symbol (const char *, int, const char *)
    ATTRIBUTE_NORETURN;
extern void rtvec_check_failed_bounds (rtvec, int, const char *, int,
				       const char *)
    ATTRIBUTE_NORETURN;

#else   /* not ENABLE_RTL_CHECKING */

#define RTL_CHECK1(RTX, N, C1)      ((RTX)->u.fld[N])
#define RTL_CHECK2(RTX, N, C1, C2)  ((RTX)->u.fld[N])
#define RTL_CHECKC1(RTX, N, C)	    __PAST_END((RTX)->u.fld, N)
#define RTL_CHECKC2(RTX, N, C1, C2) ((RTX)->u.fld[N])
#define RTVEC_ELT(RTVEC, I)	    __PAST_END((RTVEC)->elem, I)
#define XWINT(RTX, N)		    ((RTX)->u.hwint[N])
#define XCWINT(RTX, N, C)	    ((RTX)->u.hwint[N])
#define XCMWINT(RTX, N, C, M)	    __PAST_END((RTX)->u.hwint, N)
#define XCNMWINT(RTX, N, C, M)	    ((RTX)->u.hwint[N])
#define XCNMPRV(RTX, C, M)	    (&(RTX)->u.rv)
#define BLOCK_SYMBOL_CHECK(RTX)	    (&(RTX)->u.block_sym)

#endif

/* General accessor macros for accessing the flags of an rtx.  */

/* Access an individual rtx flag, with no checking of any kind.  */
#define RTX_FLAG(RTX, FLAG)	((RTX)->FLAG)

#if defined ENABLE_RTL_FLAG_CHECKING && (GCC_VERSION >= 2007)
#define RTL_FLAG_CHECK1(NAME, RTX, C1) __extension__			\
({ rtx const _rtx = (RTX);						\
   if (GET_CODE(_rtx) != C1)						\
     rtl_check_failed_flag  (NAME, _rtx, __FILE__, __LINE__,		\
			     __FUNCTION__);				\
   _rtx; })

#define RTL_FLAG_CHECK2(NAME, RTX, C1, C2) __extension__		\
({ rtx const _rtx = (RTX);						\
   if (GET_CODE(_rtx) != C1 && GET_CODE(_rtx) != C2)			\
     rtl_check_failed_flag  (NAME,_rtx, __FILE__, __LINE__,		\
			      __FUNCTION__);				\
   _rtx; })

#define RTL_FLAG_CHECK3(NAME, RTX, C1, C2, C3) __extension__		\
({ rtx const _rtx = (RTX);						\
   if (GET_CODE(_rtx) != C1 && GET_CODE(_rtx) != C2			\
       && GET_CODE(_rtx) != C3)						\
     rtl_check_failed_flag  (NAME, _rtx, __FILE__, __LINE__,		\
			     __FUNCTION__);				\
   _rtx; })

#define RTL_FLAG_CHECK4(NAME, RTX, C1, C2, C3, C4) __extension__	\
({ rtx const _rtx = (RTX);						\
   if (GET_CODE(_rtx) != C1 && GET_CODE(_rtx) != C2			\
       && GET_CODE(_rtx) != C3 && GET_CODE(_rtx) != C4)			\
     rtl_check_failed_flag  (NAME, _rtx, __FILE__, __LINE__,		\
			      __FUNCTION__);				\
   _rtx; })

#define RTL_FLAG_CHECK5(NAME, RTX, C1, C2, C3, C4, C5) __extension__	\
({ rtx const _rtx = (RTX);						\
   if (GET_CODE(_rtx) != C1 && GET_CODE(_rtx) != C2			\
       && GET_CODE(_rtx) != C3 && GET_CODE(_rtx) != C4			\
       && GET_CODE(_rtx) != C5)						\
     rtl_check_failed_flag  (NAME, _rtx, __FILE__, __LINE__,		\
			     __FUNCTION__);				\
   _rtx; })

#define RTL_FLAG_CHECK6(NAME, RTX, C1, C2, C3, C4, C5, C6)		\
  __extension__								\
({ rtx const _rtx = (RTX);						\
   if (GET_CODE(_rtx) != C1 && GET_CODE(_rtx) != C2			\
       && GET_CODE(_rtx) != C3 && GET_CODE(_rtx) != C4			\
       && GET_CODE(_rtx) != C5 && GET_CODE(_rtx) != C6)			\
     rtl_check_failed_flag  (NAME,_rtx, __FILE__, __LINE__,		\
			     __FUNCTION__);				\
   _rtx; })

#define RTL_FLAG_CHECK7(NAME, RTX, C1, C2, C3, C4, C5, C6, C7)		\
  __extension__								\
({ rtx const _rtx = (RTX);						\
   if (GET_CODE(_rtx) != C1 && GET_CODE(_rtx) != C2			\
       && GET_CODE(_rtx) != C3 && GET_CODE(_rtx) != C4			\
       && GET_CODE(_rtx) != C5 && GET_CODE(_rtx) != C6			\
       && GET_CODE(_rtx) != C7)						\
     rtl_check_failed_flag  (NAME, _rtx, __FILE__, __LINE__,		\
			     __FUNCTION__);				\
   _rtx; })

#define RTL_FLAG_CHECK8(NAME, RTX, C1, C2, C3, C4, C5, C6, C7, C8)	\
  __extension__								\
({ rtx const _rtx = (RTX);						\
   if (GET_CODE(_rtx) != C1 && GET_CODE(_rtx) != C2			\
       && GET_CODE(_rtx) != C3 && GET_CODE(_rtx) != C4			\
       && GET_CODE(_rtx) != C5 && GET_CODE(_rtx) != C6			\
       && GET_CODE(_rtx) != C7 && GET_CODE(_rtx) != C8)			\
     rtl_check_failed_flag  (NAME, _rtx, __FILE__, __LINE__,		\
			     __FUNCTION__);				\
   _rtx; })

extern void rtl_check_failed_flag (const char *, rtx, const char *,
				   int, const char *)
    ATTRIBUTE_NORETURN
    ;

#else	/* not ENABLE_RTL_FLAG_CHECKING */

#define RTL_FLAG_CHECK1(NAME, RTX, C1)					(RTX)
#define RTL_FLAG_CHECK2(NAME, RTX, C1, C2)				(RTX)
#define RTL_FLAG_CHECK3(NAME, RTX, C1, C2, C3)				(RTX)
#define RTL_FLAG_CHECK4(NAME, RTX, C1, C2, C3, C4)			(RTX)
#define RTL_FLAG_CHECK5(NAME, RTX, C1, C2, C3, C4, C5)		(RTX)
#define RTL_FLAG_CHECK6(NAME, RTX, C1, C2, C3, C4, C5, C6)		(RTX)
#define RTL_FLAG_CHECK7(NAME, RTX, C1, C2, C3, C4, C5, C6, C7)		(RTX)
#define RTL_FLAG_CHECK8(NAME, RTX, C1, C2, C3, C4, C5, C6, C7, C8)	(RTX)
#endif

#define XINT(RTX, N)	(RTL_CHECK2 (RTX, N, 'i', 'n').rt_int)
#define XSTR(RTX, N)	(RTL_CHECK2 (RTX, N, 's', 'S').rt_str)
#define XEXP(RTX, N)	(RTL_CHECK2 (RTX, N, 'e', 'u').rt_rtx)
#define XVEC(RTX, N)	(RTL_CHECK2 (RTX, N, 'E', 'V').rt_rtvec)
#define XMODE(RTX, N)	(RTL_CHECK1 (RTX, N, 'M').rt_type)
#define XBITMAP(RTX, N) (RTL_CHECK1 (RTX, N, 'b').rt_bit)
#define XTREE(RTX, N)   (RTL_CHECK1 (RTX, N, 't').rt_tree)
#define XBBDEF(RTX, N)	(RTL_CHECK1 (RTX, N, 'B').rt_bb)
#define XTMPL(RTX, N)	(RTL_CHECK1 (RTX, N, 'T').rt_str)

#define XVECEXP(RTX, N, M)	RTVEC_ELT (XVEC (RTX, N), M)
#define XVECLEN(RTX, N)		GET_NUM_ELEM (XVEC (RTX, N))

/* These are like XINT, etc. except that they expect a '0' field instead
   of the normal type code.  */

#define X0INT(RTX, N)	   (RTL_CHECK1 (RTX, N, '0').rt_int)
#define X0UINT(RTX, N)	   (RTL_CHECK1 (RTX, N, '0').rt_uint)
#define X0STR(RTX, N)	   (RTL_CHECK1 (RTX, N, '0').rt_str)
#define X0EXP(RTX, N)	   (RTL_CHECK1 (RTX, N, '0').rt_rtx)
#define X0VEC(RTX, N)	   (RTL_CHECK1 (RTX, N, '0').rt_rtvec)
#define X0MODE(RTX, N)	   (RTL_CHECK1 (RTX, N, '0').rt_type)
#define X0BITMAP(RTX, N)   (RTL_CHECK1 (RTX, N, '0').rt_bit)
#define X0TREE(RTX, N)	   (RTL_CHECK1 (RTX, N, '0').rt_tree)
#define X0BBDEF(RTX, N)	   (RTL_CHECK1 (RTX, N, '0').rt_bb)
#define X0ADVFLAGS(RTX, N) (RTL_CHECK1 (RTX, N, '0').rt_addr_diff_vec_flags)
#define X0CSELIB(RTX, N)   (RTL_CHECK1 (RTX, N, '0').rt_cselib)
#define X0MEMATTR(RTX, N)  (RTL_CHECKC1 (RTX, N, MEM).rt_mem)
#define X0REGATTR(RTX, N)  (RTL_CHECKC1 (RTX, N, REG).rt_reg)
#define X0CONSTANT(RTX, N) (RTL_CHECK1 (RTX, N, '0').rt_constant)

/* Access a '0' field with any type.  */
#define X0ANY(RTX, N)	   RTL_CHECK1 (RTX, N, '0')

#define XCINT(RTX, N, C)      (RTL_CHECKC1 (RTX, N, C).rt_int)
#define XCUINT(RTX, N, C)     (RTL_CHECKC1 (RTX, N, C).rt_uint)
#define XCSTR(RTX, N, C)      (RTL_CHECKC1 (RTX, N, C).rt_str)
#define XCEXP(RTX, N, C)      (RTL_CHECKC1 (RTX, N, C).rt_rtx)
#define XCVEC(RTX, N, C)      (RTL_CHECKC1 (RTX, N, C).rt_rtvec)
#define XCMODE(RTX, N, C)     (RTL_CHECKC1 (RTX, N, C).rt_type)
#define XCBITMAP(RTX, N, C)   (RTL_CHECKC1 (RTX, N, C).rt_bit)
#define XCTREE(RTX, N, C)     (RTL_CHECKC1 (RTX, N, C).rt_tree)
#define XCBBDEF(RTX, N, C)    (RTL_CHECKC1 (RTX, N, C).rt_bb)
#define XCCSELIB(RTX, N, C)   (RTL_CHECKC1 (RTX, N, C).rt_cselib)

#define XCVECEXP(RTX, N, M, C)	RTVEC_ELT (XCVEC (RTX, N, C), M)
#define XCVECLEN(RTX, N, C)	GET_NUM_ELEM (XCVEC (RTX, N, C))

#define XC2EXP(RTX, N, C1, C2)      (RTL_CHECKC2 (RTX, N, C1, C2).rt_rtx)

/* ACCESS MACROS for particular fields of insns.  */

/* Holds a unique number for each insn.
   These are not necessarily sequentially increasing.  */
#define INSN_UID(INSN)  XINT (INSN, 0)

/* Chain insns together in sequence.  */
#define PREV_INSN(INSN)	XEXP (INSN, 1)
#define NEXT_INSN(INSN)	XEXP (INSN, 2)

#define BLOCK_FOR_INSN(INSN) XBBDEF (INSN, 3)
#define INSN_LOCATOR(INSN) XINT (INSN, 4)
/* The body of an insn.  */
#define PATTERN(INSN)	XEXP (INSN, 5)

/* Code number of instruction, from when it was recognized.
   -1 means this instruction has not been recognized yet.  */
#define INSN_CODE(INSN) XINT (INSN, 6)

/* Set up in flow.c; empty before then.
   Holds a chain of INSN_LIST rtx's whose first operands point at
   previous insns with direct data-flow connections to this one.
   That means that those insns set variables whose next use is in this insn.
   They are always in the same basic block as this insn.  */
#define LOG_LINKS(INSN)	XEXP(INSN, 7)

#define RTX_FRAME_RELATED_P(RTX)					\
  (RTL_FLAG_CHECK5("RTX_FRAME_RELATED_P", (RTX), INSN, CALL_INSN,	\
		   JUMP_INSN, BARRIER, SET)->frame_related)

/* 1 if RTX is an insn that has been deleted.  */
#define INSN_DELETED_P(RTX)						\
  (RTL_FLAG_CHECK6("INSN_DELETED_P", (RTX), INSN, CALL_INSN, JUMP_INSN,	\
		   CODE_LABEL, BARRIER, NOTE)->volatil)

/* 1 if RTX is a call to a const or pure function.  */
#define CONST_OR_PURE_CALL_P(RTX)					\
  (RTL_FLAG_CHECK3("CONST_OR_PURE_CALL_P", (RTX), CALL_INSN, NOTE,	\
		   EXPR_LIST)->unchanging)

/* 1 if RTX is a call_insn for a sibling call.  */
#define SIBLING_CALL_P(RTX)						\
  (RTL_FLAG_CHECK1("SIBLING_CALL_P", (RTX), CALL_INSN)->jump)

/* 1 if RTX is a jump_insn, call_insn, or insn that is an annulling branch.  */
#define INSN_ANNULLED_BRANCH_P(RTX)					\
  (RTL_FLAG_CHECK3("INSN_ANNULLED_BRANCH_P", (RTX), JUMP_INSN, CALL_INSN, INSN)->unchanging)

/* 1 if RTX is an insn in a delay slot and is from the target of the branch.
   If the branch insn has INSN_ANNULLED_BRANCH_P set, this insn should only be
   executed if the branch is taken.  For annulled branches with this bit
   clear, the insn should be executed only if the branch is not taken.  */
#define INSN_FROM_TARGET_P(RTX)						\
  (RTL_FLAG_CHECK3("INSN_FROM_TARGET_P", (RTX), INSN, JUMP_INSN, CALL_INSN)->in_struct)

/* In an ADDR_DIFF_VEC, the flags for RTX for use by branch shortening.
   See the comments for ADDR_DIFF_VEC in rtl.def.  */
#define ADDR_DIFF_VEC_FLAGS(RTX) X0ADVFLAGS(RTX, 4)

/* In a VALUE, the value cselib has assigned to RTX.
   This is a "struct cselib_val_struct", see cselib.h.  */
#define CSELIB_VAL_PTR(RTX) X0CSELIB(RTX, 0)

/* Holds a list of notes on what this insn does to various REGs.
   It is a chain of EXPR_LIST rtx's, where the second operand is the
   chain pointer and the first operand is the REG being described.
   The mode field of the EXPR_LIST contains not a real machine mode
   but a value from enum reg_note.  */
#define REG_NOTES(INSN)	XEXP(INSN, 8)

enum reg_note
{
#define DEF_REG_NOTE(NAME) NAME,
#include "reg-notes.def"
#undef DEF_REG_NOTE
  REG_NOTE_MAX
};

/* Define macros to extract and insert the reg-note kind in an EXPR_LIST.  */
#define REG_NOTE_KIND(LINK) ((enum reg_note) GET_MODE (LINK))
#define PUT_REG_NOTE_KIND(LINK, KIND) \
  PUT_MODE (LINK, (enum machine_mode) (KIND))

/* Names for REG_NOTE's in EXPR_LIST insn's.  */

extern const char * const reg_note_name[];
#define GET_REG_NOTE_NAME(MODE) (reg_note_name[(int) (MODE)])

/* This field is only present on CALL_INSNs.  It holds a chain of EXPR_LIST of
   USE and CLOBBER expressions.
     USE expressions list the registers filled with arguments that
   are passed to the function.
     CLOBBER expressions document the registers explicitly clobbered
   by this CALL_INSN.
     Pseudo registers can not be mentioned in this list.  */
#define CALL_INSN_FUNCTION_USAGE(INSN)	XEXP(INSN, 9)

/* The label-number of a code-label.  The assembler label
   is made from `L' and the label-number printed in decimal.
   Label numbers are unique in a compilation.  */
#define CODE_LABEL_NUMBER(INSN)	XINT (INSN, 6)

/* In a NOTE that is a line number, this is a string for the file name that the
   line is in.  We use the same field to record block numbers temporarily in
   NOTE_INSN_BLOCK_BEG and NOTE_INSN_BLOCK_END notes.  (We avoid lots of casts
   between ints and pointers if we use a different macro for the block number.)
   */

/* Opaque data.  */
#define NOTE_DATA(INSN)	        RTL_CHECKC1 (INSN, 4, NOTE)
#define NOTE_DELETED_LABEL_NAME(INSN) XCSTR (INSN, 4, NOTE)
#ifdef USE_MAPPED_LOCATION
#define NOTE_SOURCE_LOCATION(INSN) XCUINT (INSN, 5, NOTE)
#define NOTE_EXPANDED_LOCATION(XLOC, INSN)	\
  (XLOC) = expand_location (NOTE_SOURCE_LOCATION (INSN))
#define SET_INSN_DELETED(INSN) \
  (PUT_CODE (INSN, NOTE), NOTE_LINE_NUMBER (INSN) = NOTE_INSN_DELETED)
#else
#define NOTE_EXPANDED_LOCATION(XLOC, INSN)	\
  ((XLOC).file = NOTE_SOURCE_FILE (INSN),	\
   (XLOC).line = NOTE_LINE_NUMBER (INSN))
#define NOTE_SOURCE_FILE(INSN)	XCSTR (INSN, 4, NOTE)
#define SET_INSN_DELETED(INSN) \
  (PUT_CODE (INSN, NOTE),  NOTE_SOURCE_FILE (INSN) = 0, \
   NOTE_LINE_NUMBER (INSN) = NOTE_INSN_DELETED)
#endif
#define NOTE_BLOCK(INSN)	XCTREE (INSN, 4, NOTE)
#define NOTE_EH_HANDLER(INSN)	XCINT (INSN, 4, NOTE)
#define NOTE_BASIC_BLOCK(INSN)	XCBBDEF (INSN, 4, NOTE)
#define NOTE_EXPECTED_VALUE(INSN) XCEXP (INSN, 4, NOTE)
#define NOTE_VAR_LOCATION(INSN)	XCEXP (INSN, 4, NOTE)

/* In a NOTE that is a line number, this is the line number.
   Other kinds of NOTEs are identified by negative numbers here.  */
#define NOTE_LINE_NUMBER(INSN) XCINT (INSN, 5, NOTE)

/* Nonzero if INSN is a note marking the beginning of a basic block.  */
#define NOTE_INSN_BASIC_BLOCK_P(INSN)			\
  (GET_CODE (INSN) == NOTE				\
   && NOTE_LINE_NUMBER (INSN) == NOTE_INSN_BASIC_BLOCK)

/* Variable declaration and the location of a variable.  */
#define NOTE_VAR_LOCATION_DECL(INSN)	(XCTREE (XCEXP (INSN, 4, NOTE), \
						 0, VAR_LOCATION))
#define NOTE_VAR_LOCATION_LOC(INSN)	(XCEXP (XCEXP (INSN, 4, NOTE),  \
						1, VAR_LOCATION))

/* Codes that appear in the NOTE_LINE_NUMBER field for kinds of notes
   that are not line numbers.  These codes are all negative.
   
   Notice that we do not try to use zero here for any of
   the special note codes because sometimes the source line
   actually can be zero!  This happens (for example) when we
   are generating code for the per-translation-unit constructor
   and destructor routines for some C++ translation unit.  */

enum insn_note
{
  /* Keep all of these numbers negative.  Adjust as needed.  */
  NOTE_INSN_BIAS = -100,

#define DEF_INSN_NOTE(NAME) NAME,
#include "insn-notes.def"
#undef DEF_INSN_NOTE

  NOTE_INSN_MAX
};

/* Names for NOTE insn's other than line numbers.  */

extern const char * const note_insn_name[NOTE_INSN_MAX - NOTE_INSN_BIAS];
#define GET_NOTE_INSN_NAME(NOTE_CODE) \
  (note_insn_name[(NOTE_CODE) - (int) NOTE_INSN_BIAS])

/* The name of a label, in case it corresponds to an explicit label
   in the input source code.  */
#define LABEL_NAME(RTX) XCSTR (RTX, 7, CODE_LABEL)

/* In jump.c, each label contains a count of the number
   of LABEL_REFs that point at it, so unused labels can be deleted.  */
#define LABEL_NUSES(RTX) XCINT (RTX, 4, CODE_LABEL)

/* APPLE LOCAL begin for-fsf-4_4 3274130 5295549 */ \
/* The alignment of the label, as the log-base-2 of the alignment in bytes.  */
#define LABEL_ALIGN_LOG(RTX) (XCUINT (RTX, 8, CODE_LABEL) & 0xFF)
/* The maximum number of bytes to skip to achieve that alignment.  */
#define LABEL_MAX_SKIP(RTX) (XCUINT (RTX, 8, CODE_LABEL) >> 8)
#define SET_LABEL_ALIGN(RTX, ALIGN, MAX_SKIP) \
  (XCUINT (RTX, 8, CODE_LABEL) = (ALIGN) | ((MAX_SKIP) << 8))

/* APPLE LOCAL end for-fsf-4_4 3274130 5295549 */ \
/* Labels carry a two-bit field composed of the ->jump and ->call
   bits.  This field indicates whether the label is an alternate
   entry point, and if so, what kind.  */
enum label_kind
{
  LABEL_NORMAL = 0,	/* ordinary label */
  LABEL_STATIC_ENTRY,	/* alternate entry point, not exported */
  LABEL_GLOBAL_ENTRY,	/* alternate entry point, exported */
  LABEL_WEAK_ENTRY	/* alternate entry point, exported as weak symbol */
};

#if defined ENABLE_RTL_FLAG_CHECKING && (GCC_VERSION > 2007)

/* Retrieve the kind of LABEL.  */
#define LABEL_KIND(LABEL) __extension__					\
({ rtx const _label = (LABEL);						\
   if (GET_CODE (_label) != CODE_LABEL)					\
     rtl_check_failed_flag ("LABEL_KIND", _label, __FILE__, __LINE__,	\
			    __FUNCTION__);				\
   (enum label_kind) ((_label->jump << 1) | _label->call); })

/* Set the kind of LABEL.  */
#define SET_LABEL_KIND(LABEL, KIND) do {				\
   rtx _label = (LABEL);						\
   unsigned int _kind = (KIND);						\
   if (GET_CODE (_label) != CODE_LABEL)					\
     rtl_check_failed_flag ("SET_LABEL_KIND", _label, __FILE__, __LINE__, \
			    __FUNCTION__);				\
   _label->jump = ((_kind >> 1) & 1);					\
   _label->call = (_kind & 1);						\
} while (0)

#else

/* Retrieve the kind of LABEL.  */
#define LABEL_KIND(LABEL) \
   ((enum label_kind) (((LABEL)->jump << 1) | (LABEL)->call))

/* Set the kind of LABEL.  */
#define SET_LABEL_KIND(LABEL, KIND) do {				\
   rtx _label = (LABEL);						\
   unsigned int _kind = (KIND);						\
   _label->jump = ((_kind >> 1) & 1);					\
   _label->call = (_kind & 1);						\
} while (0)

#endif /* rtl flag checking */

#define LABEL_ALT_ENTRY_P(LABEL) (LABEL_KIND (LABEL) != LABEL_NORMAL)

/* In jump.c, each JUMP_INSN can point to a label that it can jump to,
   so that if the JUMP_INSN is deleted, the label's LABEL_NUSES can
   be decremented and possibly the label can be deleted.  */
#define JUMP_LABEL(INSN)   XCEXP (INSN, 9, JUMP_INSN)

/* Once basic blocks are found in flow.c,
   each CODE_LABEL starts a chain that goes through
   all the LABEL_REFs that jump to that label.
   The chain eventually winds up at the CODE_LABEL: it is circular.  */
#define LABEL_REFS(LABEL) XCEXP (LABEL, 5, CODE_LABEL)

/* For a REG rtx, REGNO extracts the register number.  ORIGINAL_REGNO holds
   the number the register originally had; for a pseudo register turned into
   a hard reg this will hold the old pseudo register number.  */

#define REGNO(RTX) XCUINT (RTX, 0, REG)
#define ORIGINAL_REGNO(RTX) X0UINT (RTX, 1)

/* 1 if RTX is a reg or parallel that is the current function's return
   value.  */
#define REG_FUNCTION_VALUE_P(RTX)					\
  (RTL_FLAG_CHECK2("REG_FUNCTION_VALUE_P", (RTX), REG, PARALLEL)->return_val)

/* 1 if RTX is a reg that corresponds to a variable declared by the user.  */
#define REG_USERVAR_P(RTX)						\
  (RTL_FLAG_CHECK1("REG_USERVAR_P", (RTX), REG)->volatil)

/* 1 if RTX is a reg that holds a pointer value.  */
#define REG_POINTER(RTX)						\
  (RTL_FLAG_CHECK1("REG_POINTER", (RTX), REG)->frame_related)

/* 1 if RTX is a mem that holds a pointer value.  */
#define MEM_POINTER(RTX)						\
  (RTL_FLAG_CHECK1("MEM_POINTER", (RTX), MEM)->frame_related)

/* 1 if the given register REG corresponds to a hard register.  */
#define HARD_REGISTER_P(REG) (HARD_REGISTER_NUM_P (REGNO (REG)))

/* 1 if the given register number REG_NO corresponds to a hard register.  */
#define HARD_REGISTER_NUM_P(REG_NO) ((REG_NO) < FIRST_PSEUDO_REGISTER)

/* For a CONST_INT rtx, INTVAL extracts the integer.  */
#define INTVAL(RTX) XCWINT(RTX, 0, CONST_INT)
#define UINTVAL(RTX) ((unsigned HOST_WIDE_INT) INTVAL (RTX))

/* For a CONST_DOUBLE:
   For a VOIDmode, there are two integers CONST_DOUBLE_LOW is the
     low-order word and ..._HIGH the high-order.
   For a float, there is a REAL_VALUE_TYPE structure, and
     CONST_DOUBLE_REAL_VALUE(r) is a pointer to it.  */
#define CONST_DOUBLE_LOW(r) XCMWINT (r, 0, CONST_DOUBLE, VOIDmode)
#define CONST_DOUBLE_HIGH(r) XCMWINT (r, 1, CONST_DOUBLE, VOIDmode)
#define CONST_DOUBLE_REAL_VALUE(r) \
  ((const struct real_value *) XCNMPRV (r, CONST_DOUBLE, VOIDmode))

/* For a CONST_VECTOR, return element #n.  */
#define CONST_VECTOR_ELT(RTX, N) XCVECEXP (RTX, 0, N, CONST_VECTOR)

/* For a CONST_VECTOR, return the number of elements in a vector.  */
#define CONST_VECTOR_NUNITS(RTX) XCVECLEN (RTX, 0, CONST_VECTOR)

/* For a SUBREG rtx, SUBREG_REG extracts the value we want a subreg of.
   SUBREG_BYTE extracts the byte-number.  */

#define SUBREG_REG(RTX) XCEXP (RTX, 0, SUBREG)
#define SUBREG_BYTE(RTX) XCUINT (RTX, 1, SUBREG)

/* in rtlanal.c */
/* Return the right cost to give to an operation
   to make the cost of the corresponding register-to-register instruction
   N times that of a fast register-to-register instruction.  */
#define COSTS_N_INSNS(N) ((N) * 4)

/* Maximum cost of an rtl expression.  This value has the special meaning
   not to use an rtx with this cost under any circumstances.  */
#define MAX_COST INT_MAX

extern void init_rtlanal (void);
extern int rtx_cost (rtx, enum rtx_code);
extern int address_cost (rtx, enum machine_mode);
extern unsigned int subreg_lsb (rtx);
extern unsigned int subreg_lsb_1 (enum machine_mode, enum machine_mode,
				  unsigned int);
extern unsigned int subreg_regno_offset	(unsigned int, enum machine_mode,
					 unsigned int, enum machine_mode);
extern bool subreg_offset_representable_p (unsigned int, enum machine_mode,
					   unsigned int, enum machine_mode);
extern unsigned int subreg_regno (rtx);
extern unsigned HOST_WIDE_INT nonzero_bits (rtx, enum machine_mode);
extern unsigned int num_sign_bit_copies (rtx, enum machine_mode);
extern bool constant_pool_constant_p (rtx);
extern bool truncated_to_mode (enum machine_mode, rtx);


/* 1 if RTX is a subreg containing a reg that is already known to be
   sign- or zero-extended from the mode of the subreg to the mode of
   the reg.  SUBREG_PROMOTED_UNSIGNED_P gives the signedness of the
   extension.

   When used as a LHS, is means that this extension must be done
   when assigning to SUBREG_REG.  */

#define SUBREG_PROMOTED_VAR_P(RTX)					\
  (RTL_FLAG_CHECK1("SUBREG_PROMOTED", (RTX), SUBREG)->in_struct)

#define SUBREG_PROMOTED_UNSIGNED_SET(RTX, VAL)				\
do {									\
  rtx const _rtx = RTL_FLAG_CHECK1("SUBREG_PROMOTED_UNSIGNED_SET", (RTX), SUBREG); \
  if ((VAL) < 0)							\
    _rtx->volatil = 1;							\
  else {								\
    _rtx->volatil = 0;							\
    _rtx->unchanging = (VAL);						\
  }									\
} while (0)
#define SUBREG_PROMOTED_UNSIGNED_P(RTX)	\
  ((RTL_FLAG_CHECK1("SUBREG_PROMOTED_UNSIGNED_P", (RTX), SUBREG)->volatil) \
     ? -1 : (RTX)->unchanging)

/* Access various components of an ASM_OPERANDS rtx.  */

#define ASM_OPERANDS_TEMPLATE(RTX) XCSTR (RTX, 0, ASM_OPERANDS)
#define ASM_OPERANDS_OUTPUT_CONSTRAINT(RTX) XCSTR (RTX, 1, ASM_OPERANDS)
#define ASM_OPERANDS_OUTPUT_IDX(RTX) XCINT (RTX, 2, ASM_OPERANDS)
#define ASM_OPERANDS_INPUT_VEC(RTX) XCVEC (RTX, 3, ASM_OPERANDS)
#define ASM_OPERANDS_INPUT_CONSTRAINT_VEC(RTX) XCVEC (RTX, 4, ASM_OPERANDS)
#define ASM_OPERANDS_INPUT(RTX, N) XCVECEXP (RTX, 3, N, ASM_OPERANDS)
#define ASM_OPERANDS_INPUT_LENGTH(RTX) XCVECLEN (RTX, 3, ASM_OPERANDS)
#define ASM_OPERANDS_INPUT_CONSTRAINT_EXP(RTX, N) \
  XCVECEXP (RTX, 4, N, ASM_OPERANDS)
#define ASM_OPERANDS_INPUT_CONSTRAINT(RTX, N) \
  XSTR (XCVECEXP (RTX, 4, N, ASM_OPERANDS), 0)
#define ASM_OPERANDS_INPUT_MODE(RTX, N)  \
  GET_MODE (XCVECEXP (RTX, 4, N, ASM_OPERANDS))
#ifdef USE_MAPPED_LOCATION
#define ASM_OPERANDS_SOURCE_LOCATION(RTX) XCUINT (RTX, 5, ASM_OPERANDS)
#else
#define ASM_OPERANDS_SOURCE_FILE(RTX) XCSTR (RTX, 5, ASM_OPERANDS)
#define ASM_OPERANDS_SOURCE_LINE(RTX) XCINT (RTX, 6, ASM_OPERANDS)
#endif

/* 1 if RTX is a mem that is statically allocated in read-only memory.  */
#define MEM_READONLY_P(RTX) \
  (RTL_FLAG_CHECK1("MEM_READONLY_P", (RTX), MEM)->unchanging)

/* 1 if RTX is a mem and we should keep the alias set for this mem
   unchanged when we access a component.  Set to 1, or example, when we
   are already in a non-addressable component of an aggregate.  */
#define MEM_KEEP_ALIAS_SET_P(RTX)					\
  (RTL_FLAG_CHECK1("MEM_KEEP_ALIAS_SET_P", (RTX), MEM)->jump)

/* 1 if RTX is a mem or asm_operand for a volatile reference.  */
#define MEM_VOLATILE_P(RTX)						\
  (RTL_FLAG_CHECK3("MEM_VOLATILE_P", (RTX), MEM, ASM_OPERANDS,		\
		   ASM_INPUT)->volatil)

/* 1 if RTX is a mem that refers to an aggregate, either to the
   aggregate itself or to a field of the aggregate.  If zero, RTX may
   or may not be such a reference.  */
#define MEM_IN_STRUCT_P(RTX)						\
  (RTL_FLAG_CHECK1("MEM_IN_STRUCT_P", (RTX), MEM)->in_struct)

/* 1 if RTX is a MEM that refers to a scalar.  If zero, RTX may or may
   not refer to a scalar.  */
#define MEM_SCALAR_P(RTX)						\
  (RTL_FLAG_CHECK1("MEM_SCALAR_P", (RTX), MEM)->return_val)

/* 1 if RTX is a mem that cannot trap.  */
#define MEM_NOTRAP_P(RTX) \
  (RTL_FLAG_CHECK1("MEM_NOTRAP_P", (RTX), MEM)->call)

/* If VAL is nonzero, set MEM_IN_STRUCT_P and clear MEM_SCALAR_P in
   RTX.  Otherwise, vice versa.  Use this macro only when you are
   *sure* that you know that the MEM is in a structure, or is a
   scalar.  VAL is evaluated only once.  */
#define MEM_SET_IN_STRUCT_P(RTX, VAL)		\
do {						\
  if (VAL)					\
    {						\
      MEM_IN_STRUCT_P (RTX) = 1;		\
      MEM_SCALAR_P (RTX) = 0;			\
    }						\
  else						\
    {						\
      MEM_IN_STRUCT_P (RTX) = 0;		\
      MEM_SCALAR_P (RTX) = 1;			\
    }						\
} while (0)

/* The memory attribute block.  We provide access macros for each value
   in the block and provide defaults if none specified.  */
#define MEM_ATTRS(RTX) X0MEMATTR (RTX, 1)

/* The register attribute block.  We provide access macros for each value
   in the block and provide defaults if none specified.  */
#define REG_ATTRS(RTX) X0REGATTR (RTX, 2)

/* For a MEM rtx, the alias set.  If 0, this MEM is not in any alias
   set, and may alias anything.  Otherwise, the MEM can only alias
   MEMs in a conflicting alias set.  This value is set in a
   language-dependent manner in the front-end, and should not be
   altered in the back-end.  These set numbers are tested with
   alias_sets_conflict_p.  */
#define MEM_ALIAS_SET(RTX) (MEM_ATTRS (RTX) == 0 ? 0 : MEM_ATTRS (RTX)->alias)

/* For a MEM rtx, the decl it is known to refer to, if it is known to
   refer to part of a DECL.  It may also be a COMPONENT_REF.  */
#define MEM_EXPR(RTX) (MEM_ATTRS (RTX) == 0 ? 0 : MEM_ATTRS (RTX)->expr)

/* For a MEM rtx, the offset from the start of MEM_EXPR, if known, as a
   RTX that is always a CONST_INT.  */
#define MEM_OFFSET(RTX) (MEM_ATTRS (RTX) == 0 ? 0 : MEM_ATTRS (RTX)->offset)

/* For a MEM rtx, the size in bytes of the MEM, if known, as an RTX that
   is always a CONST_INT.  */
#define MEM_SIZE(RTX)							\
(MEM_ATTRS (RTX) != 0 ? MEM_ATTRS (RTX)->size				\
 : GET_MODE (RTX) != BLKmode ? GEN_INT (GET_MODE_SIZE (GET_MODE (RTX)))	\
 : 0)

/* For a MEM rtx, the alignment in bits.  We can use the alignment of the
   mode as a default when STRICT_ALIGNMENT, but not if not.  */
#define MEM_ALIGN(RTX)							\
(MEM_ATTRS (RTX) != 0 ? MEM_ATTRS (RTX)->align				\
 : (STRICT_ALIGNMENT && GET_MODE (RTX) != BLKmode			\
    ? GET_MODE_ALIGNMENT (GET_MODE (RTX)) : BITS_PER_UNIT))

/* For a REG rtx, the decl it is known to refer to, if it is known to
   refer to part of a DECL.  */
#define REG_EXPR(RTX) (REG_ATTRS (RTX) == 0 ? 0 : REG_ATTRS (RTX)->decl)

/* For a REG rtx, the offset from the start of REG_EXPR, if known, as an
   HOST_WIDE_INT.  */
#define REG_OFFSET(RTX) (REG_ATTRS (RTX) == 0 ? 0 : REG_ATTRS (RTX)->offset)

/* Copy the attributes that apply to memory locations from RHS to LHS.  */
#define MEM_COPY_ATTRIBUTES(LHS, RHS)				\
  (MEM_VOLATILE_P (LHS) = MEM_VOLATILE_P (RHS),			\
   MEM_IN_STRUCT_P (LHS) = MEM_IN_STRUCT_P (RHS),		\
   MEM_SCALAR_P (LHS) = MEM_SCALAR_P (RHS),			\
   MEM_NOTRAP_P (LHS) = MEM_NOTRAP_P (RHS),			\
   MEM_READONLY_P (LHS) = MEM_READONLY_P (RHS),			\
   MEM_KEEP_ALIAS_SET_P (LHS) = MEM_KEEP_ALIAS_SET_P (RHS),	\
   MEM_ATTRS (LHS) = MEM_ATTRS (RHS))

/* 1 if RTX is a label_ref for a nonlocal label.  */
/* Likewise in an expr_list for a reg_label note.  */
#define LABEL_REF_NONLOCAL_P(RTX)					\
  (RTL_FLAG_CHECK2("LABEL_REF_NONLOCAL_P", (RTX), LABEL_REF,		\
		   REG_LABEL)->volatil)

/* 1 if RTX is a code_label that should always be considered to be needed.  */
#define LABEL_PRESERVE_P(RTX)						\
  (RTL_FLAG_CHECK2("LABEL_PRESERVE_P", (RTX), CODE_LABEL, NOTE)->in_struct)

/* During sched, 1 if RTX is an insn that must be scheduled together
   with the preceding insn.  */
#define SCHED_GROUP_P(RTX)						\
  (RTL_FLAG_CHECK3("SCHED_GROUP_P", (RTX), INSN, JUMP_INSN, CALL_INSN	\
		          )->in_struct)

/* For a SET rtx, SET_DEST is the place that is set
   and SET_SRC is the value it is set to.  */
#define SET_DEST(RTX) XC2EXP(RTX, 0, SET, CLOBBER)
#define SET_SRC(RTX) XCEXP(RTX, 1, SET)
#define SET_IS_RETURN_P(RTX)						\
  (RTL_FLAG_CHECK1("SET_IS_RETURN_P", (RTX), SET)->jump)

/* For a TRAP_IF rtx, TRAP_CONDITION is an expression.  */
#define TRAP_CONDITION(RTX) XCEXP (RTX, 0, TRAP_IF)
#define TRAP_CODE(RTX) XCEXP (RTX, 1, TRAP_IF)

/* For a COND_EXEC rtx, COND_EXEC_TEST is the condition to base
   conditionally executing the code on, COND_EXEC_CODE is the code
   to execute if the condition is true.  */
#define COND_EXEC_TEST(RTX) XCEXP (RTX, 0, COND_EXEC)
#define COND_EXEC_CODE(RTX) XCEXP (RTX, 1, COND_EXEC)

/* 1 if RTX is a symbol_ref that addresses this function's rtl
   constants pool.  */
#define CONSTANT_POOL_ADDRESS_P(RTX)					\
  (RTL_FLAG_CHECK1("CONSTANT_POOL_ADDRESS_P", (RTX), SYMBOL_REF)->unchanging)

/* 1 if RTX is a symbol_ref that addresses a value in the file's
   tree constant pool.  This information is private to varasm.c.  */
#define TREE_CONSTANT_POOL_ADDRESS_P(RTX)				\
  (RTL_FLAG_CHECK1("TREE_CONSTANT_POOL_ADDRESS_P",			\
		   (RTX), SYMBOL_REF)->frame_related)

/* Used if RTX is a symbol_ref, for machine-specific purposes.  */
#define SYMBOL_REF_FLAG(RTX)						\
  (RTL_FLAG_CHECK1("SYMBOL_REF_FLAG", (RTX), SYMBOL_REF)->volatil)

/* 1 if RTX is a symbol_ref that has been the library function in
   emit_library_call.  */
#define SYMBOL_REF_USED(RTX)						\
  (RTL_FLAG_CHECK1("SYMBOL_REF_USED", (RTX), SYMBOL_REF)->used)

/* 1 if RTX is a symbol_ref for a weak symbol.  */
#define SYMBOL_REF_WEAK(RTX)						\
  (RTL_FLAG_CHECK1("SYMBOL_REF_WEAK", (RTX), SYMBOL_REF)->return_val)

/* A pointer attached to the SYMBOL_REF; either SYMBOL_REF_DECL or
   SYMBOL_REF_CONSTANT.  */
#define SYMBOL_REF_DATA(RTX) X0ANY ((RTX), 2)

/* Set RTX's SYMBOL_REF_DECL to DECL.  RTX must not be a constant
   pool symbol.  */
#define SET_SYMBOL_REF_DECL(RTX, DECL) \
  (gcc_assert (!CONSTANT_POOL_ADDRESS_P (RTX)), X0TREE ((RTX), 2) = (DECL))

/* The tree (decl or constant) associated with the symbol, or null.  */
#define SYMBOL_REF_DECL(RTX) \
  (CONSTANT_POOL_ADDRESS_P (RTX) ? NULL : X0TREE ((RTX), 2))

/* Set RTX's SYMBOL_REF_CONSTANT to C.  RTX must be a constant pool symbol.  */
#define SET_SYMBOL_REF_CONSTANT(RTX, C) \
  (gcc_assert (CONSTANT_POOL_ADDRESS_P (RTX)), X0CONSTANT ((RTX), 2) = (C))

/* The rtx constant pool entry for a symbol, or null.  */
#define SYMBOL_REF_CONSTANT(RTX) \
  (CONSTANT_POOL_ADDRESS_P (RTX) ? X0CONSTANT ((RTX), 2) : NULL)

/* A set of flags on a symbol_ref that are, in some respects, redundant with
   information derivable from the tree decl associated with this symbol.
   Except that we build a *lot* of SYMBOL_REFs that aren't associated with a
   decl.  In some cases this is a bug.  But beyond that, it's nice to cache
   this information to avoid recomputing it.  Finally, this allows space for
   the target to store more than one bit of information, as with
   SYMBOL_REF_FLAG.  */
#define SYMBOL_REF_FLAGS(RTX)	X0INT ((RTX), 1)

/* These flags are common enough to be defined for all targets.  They
   are computed by the default version of targetm.encode_section_info.  */

/* Set if this symbol is a function.  */
#define SYMBOL_FLAG_FUNCTION	(1 << 0)
#define SYMBOL_REF_FUNCTION_P(RTX) \
  ((SYMBOL_REF_FLAGS (RTX) & SYMBOL_FLAG_FUNCTION) != 0)
/* Set if targetm.binds_local_p is true.  */
#define SYMBOL_FLAG_LOCAL	(1 << 1)
#define SYMBOL_REF_LOCAL_P(RTX) \
  ((SYMBOL_REF_FLAGS (RTX) & SYMBOL_FLAG_LOCAL) != 0)
/* Set if targetm.in_small_data_p is true.  */
#define SYMBOL_FLAG_SMALL	(1 << 2)
#define SYMBOL_REF_SMALL_P(RTX) \
  ((SYMBOL_REF_FLAGS (RTX) & SYMBOL_FLAG_SMALL) != 0)
/* The three-bit field at [5:3] is true for TLS variables; use
   SYMBOL_REF_TLS_MODEL to extract the field as an enum tls_model.  */
#define SYMBOL_FLAG_TLS_SHIFT	3
#define SYMBOL_REF_TLS_MODEL(RTX) \
  ((enum tls_model) ((SYMBOL_REF_FLAGS (RTX) >> SYMBOL_FLAG_TLS_SHIFT) & 7))
/* Set if this symbol is not defined in this translation unit.  */
#define SYMBOL_FLAG_EXTERNAL	(1 << 6)
#define SYMBOL_REF_EXTERNAL_P(RTX) \
  ((SYMBOL_REF_FLAGS (RTX) & SYMBOL_FLAG_EXTERNAL) != 0)
/* Set if this symbol has a block_symbol structure associated with it.  */
#define SYMBOL_FLAG_HAS_BLOCK_INFO (1 << 7)
#define SYMBOL_REF_HAS_BLOCK_INFO_P(RTX) \
  ((SYMBOL_REF_FLAGS (RTX) & SYMBOL_FLAG_HAS_BLOCK_INFO) != 0)
/* Set if this symbol is a section anchor.  SYMBOL_REF_ANCHOR_P implies
   SYMBOL_REF_HAS_BLOCK_INFO_P.  */
#define SYMBOL_FLAG_ANCHOR	(1 << 8)
#define SYMBOL_REF_ANCHOR_P(RTX) \
  ((SYMBOL_REF_FLAGS (RTX) & SYMBOL_FLAG_ANCHOR) != 0)

/* Subsequent bits are available for the target to use.  */
#define SYMBOL_FLAG_MACH_DEP_SHIFT	9
#define SYMBOL_FLAG_MACH_DEP		(1 << SYMBOL_FLAG_MACH_DEP_SHIFT)

/* If SYMBOL_REF_HAS_BLOCK_INFO_P (RTX), this is the object_block
   structure to which the symbol belongs, or NULL if it has not been
   assigned a block.  */
#define SYMBOL_REF_BLOCK(RTX) (BLOCK_SYMBOL_CHECK (RTX)->block)

/* If SYMBOL_REF_HAS_BLOCK_INFO_P (RTX), this is the offset of RTX from
   the first object in SYMBOL_REF_BLOCK (RTX).  The value is negative if
   RTX has not yet been assigned to a block, or it has not been given an
   offset within that block.  */
#define SYMBOL_REF_BLOCK_OFFSET(RTX) (BLOCK_SYMBOL_CHECK (RTX)->offset)

/* Define a macro to look for REG_INC notes,
   but save time on machines where they never exist.  */

#if (defined (HAVE_PRE_INCREMENT) || defined (HAVE_PRE_DECREMENT) || defined (HAVE_POST_INCREMENT) || defined (HAVE_POST_DECREMENT))
#define FIND_REG_INC_NOTE(INSN, REG)			\
  ((REG) != NULL_RTX && REG_P ((REG))			\
   ? find_regno_note ((INSN), REG_INC, REGNO (REG))	\
   : find_reg_note ((INSN), REG_INC, (REG)))
#else
#define FIND_REG_INC_NOTE(INSN, REG) 0
#endif

/* Indicate whether the machine has any sort of auto increment addressing.
   If not, we can avoid checking for REG_INC notes.  */

#if (defined (HAVE_PRE_INCREMENT) || defined (HAVE_PRE_DECREMENT) || defined (HAVE_POST_INCREMENT) || defined (HAVE_POST_DECREMENT))
#define AUTO_INC_DEC
#endif

#ifndef HAVE_PRE_INCREMENT
#define HAVE_PRE_INCREMENT 0
#endif

#ifndef HAVE_PRE_DECREMENT
#define HAVE_PRE_DECREMENT 0
#endif

#ifndef HAVE_POST_INCREMENT
#define HAVE_POST_INCREMENT 0
#endif

#ifndef HAVE_POST_DECREMENT
#define HAVE_POST_DECREMENT 0
#endif

#ifndef HAVE_POST_MODIFY_DISP
#define HAVE_POST_MODIFY_DISP 0
#endif

#ifndef HAVE_POST_MODIFY_REG
#define HAVE_POST_MODIFY_REG 0
#endif

#ifndef HAVE_PRE_MODIFY_DISP
#define HAVE_PRE_MODIFY_DISP 0
#endif

#ifndef HAVE_PRE_MODIFY_REG
#define HAVE_PRE_MODIFY_REG 0
#endif


/* Some architectures do not have complete pre/post increment/decrement
   instruction sets, or only move some modes efficiently.  These macros
   allow us to tune autoincrement generation.  */

#ifndef USE_LOAD_POST_INCREMENT
#define USE_LOAD_POST_INCREMENT(MODE)   HAVE_POST_INCREMENT
#endif

#ifndef USE_LOAD_POST_DECREMENT
#define USE_LOAD_POST_DECREMENT(MODE)   HAVE_POST_DECREMENT
#endif

#ifndef USE_LOAD_PRE_INCREMENT
#define USE_LOAD_PRE_INCREMENT(MODE)    HAVE_PRE_INCREMENT
#endif

#ifndef USE_LOAD_PRE_DECREMENT
#define USE_LOAD_PRE_DECREMENT(MODE)    HAVE_PRE_DECREMENT
#endif

#ifndef USE_STORE_POST_INCREMENT
#define USE_STORE_POST_INCREMENT(MODE)  HAVE_POST_INCREMENT
#endif

#ifndef USE_STORE_POST_DECREMENT
#define USE_STORE_POST_DECREMENT(MODE)  HAVE_POST_DECREMENT
#endif

#ifndef USE_STORE_PRE_INCREMENT
#define USE_STORE_PRE_INCREMENT(MODE)   HAVE_PRE_INCREMENT
#endif

#ifndef USE_STORE_PRE_DECREMENT
#define USE_STORE_PRE_DECREMENT(MODE)   HAVE_PRE_DECREMENT
#endif

/* Nonzero when we are generating CONCATs.  */
extern int generating_concat_p;

/* Nonzero when we are expanding trees to RTL.  */
extern int currently_expanding_to_rtl;

/* Generally useful functions.  */

/* In expmed.c */
extern int ceil_log2 (unsigned HOST_WIDE_INT);

/* In builtins.c */
extern rtx expand_builtin_expect_jump (tree, rtx, rtx);

/* In explow.c */
extern void set_stack_check_libfunc (rtx);
extern HOST_WIDE_INT trunc_int_for_mode	(HOST_WIDE_INT, enum machine_mode);
extern rtx plus_constant (rtx, HOST_WIDE_INT);

/* In emit-rtl.c */
extern rtvec gen_rtvec (int, ...);
extern rtx copy_insn_1 (rtx);
extern rtx copy_insn (rtx);
extern rtx gen_int_mode (HOST_WIDE_INT, enum machine_mode);
extern rtx emit_copy_of_insn_after (rtx, rtx);
extern void set_reg_attrs_from_mem (rtx, rtx);
extern void set_mem_attrs_from_reg (rtx, rtx);
extern void set_reg_attrs_for_parm (rtx, rtx);
extern int mem_expr_equal_p (tree, tree);

/* In rtl.c */
extern rtx rtx_alloc_stat (RTX_CODE MEM_STAT_DECL);
#define rtx_alloc(c) rtx_alloc_stat (c MEM_STAT_INFO)

extern rtvec rtvec_alloc (int);
extern rtx copy_rtx (rtx);
extern void dump_rtx_statistics (void);

/* In emit-rtl.c */
extern rtx copy_rtx_if_shared (rtx);

/* In rtl.c */
extern unsigned int rtx_size (rtx);
extern rtx shallow_copy_rtx_stat (rtx MEM_STAT_DECL);
#define shallow_copy_rtx(a) shallow_copy_rtx_stat (a MEM_STAT_INFO)
extern int rtx_equal_p (rtx, rtx);

/* In emit-rtl.c */
extern rtvec gen_rtvec_v (int, rtx *);
extern rtx gen_reg_rtx (enum machine_mode);
extern rtx gen_rtx_REG_offset (rtx, enum machine_mode, unsigned int, int);
extern rtx gen_label_rtx (void);
extern rtx gen_lowpart_common (enum machine_mode, rtx);

/* In cse.c */
extern rtx gen_lowpart_if_possible (enum machine_mode, rtx);

/* In emit-rtl.c */
extern rtx gen_highpart (enum machine_mode, rtx);
extern rtx gen_highpart_mode (enum machine_mode, enum machine_mode, rtx);
extern rtx operand_subword (rtx, unsigned int, int, enum machine_mode);

/* In emit-rtl.c */
extern rtx operand_subword_force (rtx, unsigned int, enum machine_mode);
extern int subreg_lowpart_p (rtx);
extern unsigned int subreg_lowpart_offset (enum machine_mode,
					   enum machine_mode);
extern unsigned int subreg_highpart_offset (enum machine_mode,
					    enum machine_mode);
extern rtx make_safe_from (rtx, rtx);
extern rtx convert_memory_address (enum machine_mode, rtx);
extern rtx get_insns (void);
extern const char *get_insn_name (int);
extern rtx get_last_insn (void);
extern rtx get_last_insn_anywhere (void);
extern rtx get_first_nonnote_insn (void);
extern rtx get_last_nonnote_insn (void);
extern void start_sequence (void);
extern void push_to_sequence (rtx);
extern void end_sequence (void);
extern rtx immed_double_const (HOST_WIDE_INT, HOST_WIDE_INT,
			       enum machine_mode);

/* In loop-iv.c  */

extern rtx lowpart_subreg (enum machine_mode, rtx, enum machine_mode);

/* In varasm.c  */
extern rtx force_const_mem (enum machine_mode, rtx);

/* In varasm.c  */

struct function;
extern rtx get_pool_constant (rtx);
extern rtx get_pool_constant_mark (rtx, bool *);
extern enum machine_mode get_pool_mode (rtx);
extern rtx simplify_subtraction (rtx);

/* In function.c  */
extern rtx assign_stack_local (enum machine_mode, HOST_WIDE_INT, int);
extern rtx assign_stack_temp (enum machine_mode, HOST_WIDE_INT, int);
extern rtx assign_stack_temp_for_type (enum machine_mode,
				       HOST_WIDE_INT, int, tree);
extern rtx assign_temp (tree, int, int, int);

/* In emit-rtl.c */
extern rtx emit_insn_before (rtx, rtx);
extern rtx emit_insn_before_noloc (rtx, rtx);
extern rtx emit_insn_before_setloc (rtx, rtx, int);
extern rtx emit_jump_insn_before (rtx, rtx);
extern rtx emit_jump_insn_before_noloc (rtx, rtx);
extern rtx emit_jump_insn_before_setloc (rtx, rtx, int);
extern rtx emit_call_insn_before (rtx, rtx);
extern rtx emit_call_insn_before_noloc (rtx, rtx);
extern rtx emit_call_insn_before_setloc (rtx, rtx, int);
extern rtx emit_barrier_before (rtx);
extern rtx emit_label_before (rtx, rtx);
extern rtx emit_note_before (int, rtx);
extern rtx emit_insn_after (rtx, rtx);
extern rtx emit_insn_after_noloc (rtx, rtx);
extern rtx emit_insn_after_setloc (rtx, rtx, int);
extern rtx emit_jump_insn_after (rtx, rtx);
extern rtx emit_jump_insn_after_noloc (rtx, rtx);
extern rtx emit_jump_insn_after_setloc (rtx, rtx, int);
extern rtx emit_call_insn_after (rtx, rtx);
extern rtx emit_call_insn_after_noloc (rtx, rtx);
extern rtx emit_call_insn_after_setloc (rtx, rtx, int);
extern rtx emit_barrier_after (rtx);
extern rtx emit_label_after (rtx, rtx);
extern rtx emit_note_after (int, rtx);
extern rtx emit_note_copy_after (rtx, rtx);
extern rtx emit_insn (rtx);
extern rtx emit_jump_insn (rtx);
extern rtx emit_call_insn (rtx);
extern rtx emit_label (rtx);
extern rtx emit_barrier (void);
extern rtx emit_note (int);
extern rtx emit_note_copy (rtx);
extern rtx emit_line_note (location_t);
extern rtx make_insn_raw (rtx);
extern rtx make_jump_insn_raw (rtx);
extern void add_function_usage_to (rtx, rtx);
extern rtx last_call_insn (void);
extern rtx previous_insn (rtx);
extern rtx next_insn (rtx);
extern rtx prev_nonnote_insn (rtx);
extern rtx next_nonnote_insn (rtx);
extern rtx prev_real_insn (rtx);
extern rtx next_real_insn (rtx);
extern rtx prev_active_insn (rtx);
extern rtx next_active_insn (rtx);
extern int active_insn_p (rtx);
extern rtx prev_label (rtx);
extern rtx next_label (rtx);
extern rtx skip_consecutive_labels (rtx);
extern rtx next_cc0_user (rtx);
extern rtx prev_cc0_setter (rtx);

/* In cfglayout.c  */
extern int insn_line (rtx);
extern const char * insn_file (rtx);
extern int locator_line (int);
extern const char * locator_file (int);
extern int prologue_locator, epilogue_locator;

/* In jump.c */
extern enum rtx_code reverse_condition (enum rtx_code);
extern enum rtx_code reverse_condition_maybe_unordered (enum rtx_code);
extern enum rtx_code swap_condition (enum rtx_code);
extern enum rtx_code unsigned_condition (enum rtx_code);
extern enum rtx_code signed_condition (enum rtx_code);
extern void mark_jump_label (rtx, rtx, int);
extern unsigned int cleanup_barriers (void);

/* In jump.c */
extern bool squeeze_notes (rtx *, rtx *);
extern rtx delete_related_insns (rtx);
extern void delete_jump (rtx);
extern rtx get_label_before (rtx);
extern rtx get_label_after (rtx);
extern rtx follow_jumps (rtx);

/* In recog.c  */
extern rtx *find_constant_term_loc (rtx *);

/* In emit-rtl.c  */
extern rtx try_split (rtx, rtx, int);
extern int split_branch_probability;

/* In unknown file  */
extern rtx split_insns (rtx, rtx);

/* In simplify-rtx.c  */
extern rtx simplify_const_unary_operation (enum rtx_code, enum machine_mode,
					   rtx, enum machine_mode);
extern rtx simplify_unary_operation (enum rtx_code, enum machine_mode, rtx,
				     enum machine_mode);
extern rtx simplify_const_binary_operation (enum rtx_code, enum machine_mode,
					    rtx, rtx);
extern rtx simplify_binary_operation (enum rtx_code, enum machine_mode, rtx,
				      rtx);
extern rtx simplify_ternary_operation (enum rtx_code, enum machine_mode,
				       enum machine_mode, rtx, rtx, rtx);
extern rtx simplify_const_relational_operation (enum rtx_code,
						enum machine_mode, rtx, rtx);
extern rtx simplify_relational_operation (enum rtx_code, enum machine_mode,
					  enum machine_mode, rtx, rtx);
extern rtx simplify_gen_binary (enum rtx_code, enum machine_mode, rtx, rtx);
extern rtx simplify_gen_unary (enum rtx_code, enum machine_mode, rtx,
			       enum machine_mode);
extern rtx simplify_gen_ternary (enum rtx_code, enum machine_mode,
				 enum machine_mode, rtx, rtx, rtx);
extern rtx simplify_gen_relational (enum rtx_code, enum machine_mode,
				    enum machine_mode, rtx, rtx);
extern rtx simplify_subreg (enum machine_mode, rtx, enum machine_mode,
			    unsigned int);
extern rtx simplify_gen_subreg (enum machine_mode, rtx, enum machine_mode,
				unsigned int);
extern rtx simplify_replace_rtx (rtx, rtx, rtx);
extern rtx simplify_rtx (rtx);
extern rtx avoid_constant_pool_reference (rtx);
extern bool constant_pool_reference_p (rtx x);
extern bool mode_signbit_p (enum machine_mode, rtx);

/* In regclass.c  */
extern enum machine_mode choose_hard_reg_mode (unsigned int, unsigned int,
					       bool);

/* In emit-rtl.c  */
extern rtx set_unique_reg_note (rtx, enum reg_note, rtx);

/* Functions in rtlanal.c */

/* Single set is implemented as macro for performance reasons.  */
#define single_set(I) (INSN_P (I) \
		       ? (GET_CODE (PATTERN (I)) == SET \
			  ? PATTERN (I) : single_set_1 (I)) \
		       : NULL_RTX)
#define single_set_1(I) single_set_2 (I, PATTERN (I))

/* Structure used for passing data to REPLACE_LABEL.  */
typedef struct replace_label_data
{
  rtx r1;
  rtx r2;
  bool update_label_nuses;
} replace_label_data;

extern int rtx_addr_can_trap_p (rtx);
extern bool nonzero_address_p (rtx);
extern int rtx_unstable_p (rtx);
extern int rtx_varies_p (rtx, int);
extern int rtx_addr_varies_p (rtx, int);
extern HOST_WIDE_INT get_integer_term (rtx);
extern rtx get_related_value (rtx);
extern int reg_mentioned_p (rtx, rtx);
extern int count_occurrences (rtx, rtx, int);
extern int reg_referenced_p (rtx, rtx);
extern int reg_used_between_p (rtx, rtx, rtx);
extern int reg_set_between_p (rtx, rtx, rtx);
extern int commutative_operand_precedence (rtx);
extern int swap_commutative_operands_p (rtx, rtx);
extern int modified_between_p (rtx, rtx, rtx);
extern int no_labels_between_p (rtx, rtx);
extern int modified_in_p (rtx, rtx);
extern int reg_set_p (rtx, rtx);
extern rtx single_set_2 (rtx, rtx);
extern int multiple_sets (rtx);
extern int set_noop_p (rtx);
extern int noop_move_p (rtx);
extern rtx find_last_value (rtx, rtx *, rtx, int);
extern int refers_to_regno_p (unsigned int, unsigned int, rtx, rtx *);
extern int reg_overlap_mentioned_p (rtx, rtx);
extern rtx set_of (rtx, rtx);
extern void note_stores (rtx, void (*) (rtx, rtx, void *), void *);
extern void note_uses (rtx *, void (*) (rtx *, void *), void *);
extern int dead_or_set_p (rtx, rtx);
extern int dead_or_set_regno_p (rtx, unsigned int);
extern rtx find_reg_note (rtx, enum reg_note, rtx);
extern rtx find_regno_note (rtx, enum reg_note, unsigned int);
extern rtx find_reg_equal_equiv_note (rtx);
extern int find_reg_fusage (rtx, enum rtx_code, rtx);
extern int find_regno_fusage (rtx, enum rtx_code, unsigned int);
extern int pure_call_p (rtx);
extern void remove_note (rtx, rtx);
extern int side_effects_p (rtx);
extern int volatile_refs_p (rtx);
extern int volatile_insn_p (rtx);
extern int may_trap_p (rtx);
extern int may_trap_after_code_motion_p (rtx);
extern int may_trap_or_fault_p (rtx);
extern int inequality_comparisons_p (rtx);
extern rtx replace_rtx (rtx, rtx, rtx);
extern int replace_label (rtx *, void *);
extern int rtx_referenced_p (rtx, rtx);
extern bool tablejump_p (rtx, rtx *, rtx *);
extern int computed_jump_p (rtx);
typedef int (*rtx_function) (rtx *, void *);
extern int for_each_rtx (rtx *, rtx_function, void *);
extern rtx regno_use_in (unsigned int, rtx);
extern int auto_inc_p (rtx);
extern int in_expr_list_p (rtx, rtx);
extern void remove_node_from_expr_list (rtx, rtx *);
extern int loc_mentioned_in_p (rtx *, rtx);
extern rtx find_first_parameter_load (rtx, rtx);
extern bool keep_with_call_p (rtx);
extern bool label_is_jump_target_p (rtx, rtx);
extern int insn_rtx_cost (rtx);

/* Given an insn and condition, return a canonical description of
   the test being made.  */
extern rtx canonicalize_condition (rtx, rtx, int, rtx *, rtx, int, int);

/* Given a JUMP_INSN, return a canonical description of the test
   being made.  */
extern rtx get_condition (rtx, rtx *, int, int);


/* flow.c */

extern rtx find_use_as_address (rtx, rtx, HOST_WIDE_INT);

/* lists.c */

void free_EXPR_LIST_list		(rtx *);
void free_INSN_LIST_list		(rtx *);
void free_EXPR_LIST_node		(rtx);
void free_INSN_LIST_node		(rtx);
rtx alloc_INSN_LIST			(rtx, rtx);
rtx alloc_EXPR_LIST			(int, rtx, rtx);
void free_DEPS_LIST_list (rtx *);
rtx alloc_DEPS_LIST (rtx, rtx, int);
void remove_free_DEPS_LIST_elem (rtx, rtx *);
void remove_free_INSN_LIST_elem (rtx, rtx *);
rtx remove_list_elem (rtx, rtx *);
rtx copy_DEPS_LIST_list (rtx);

/* regclass.c */

/* Maximum number of parallel sets and clobbers in any insn in this fn.
   Always at least 3, since the combiner could put that many together
   and we want this to remain correct for all the remaining passes.  */

extern int max_parallel;

/* Free up register info memory.  */
extern void free_reg_info (void);

/* recog.c */
extern int asm_noperands (rtx);
extern const char *decode_asm_operands (rtx, rtx *, rtx **, const char **,
					enum machine_mode *);

extern enum reg_class reg_preferred_class (int);
extern enum reg_class reg_alternate_class (int);

extern void split_all_insns (int);
extern unsigned int split_all_insns_noflow (void);

#define MAX_SAVED_CONST_INT 64
extern GTY(()) rtx const_int_rtx[MAX_SAVED_CONST_INT * 2 + 1];

#define const0_rtx	(const_int_rtx[MAX_SAVED_CONST_INT])
#define const1_rtx	(const_int_rtx[MAX_SAVED_CONST_INT+1])
#define const2_rtx	(const_int_rtx[MAX_SAVED_CONST_INT+2])
#define constm1_rtx	(const_int_rtx[MAX_SAVED_CONST_INT-1])
extern GTY(()) rtx const_true_rtx;

extern GTY(()) rtx const_tiny_rtx[3][(int) MAX_MACHINE_MODE];

/* Returns a constant 0 rtx in mode MODE.  Integer modes are treated the
   same as VOIDmode.  */

#define CONST0_RTX(MODE) (const_tiny_rtx[0][(int) (MODE)])

/* Likewise, for the constants 1 and 2.  */

#define CONST1_RTX(MODE) (const_tiny_rtx[1][(int) (MODE)])
#define CONST2_RTX(MODE) (const_tiny_rtx[2][(int) (MODE)])

/* If HARD_FRAME_POINTER_REGNUM is defined, then a special dummy reg
   is used to represent the frame pointer.  This is because the
   hard frame pointer and the automatic variables are separated by an amount
   that cannot be determined until after register allocation.  We can assume
   that in this case ELIMINABLE_REGS will be defined, one action of which
   will be to eliminate FRAME_POINTER_REGNUM into HARD_FRAME_POINTER_REGNUM.  */
#ifndef HARD_FRAME_POINTER_REGNUM
#define HARD_FRAME_POINTER_REGNUM FRAME_POINTER_REGNUM
#endif

/* Index labels for global_rtl.  */
enum global_rtl_index
{
  GR_PC,
  GR_CC0,
  GR_STACK_POINTER,
  GR_FRAME_POINTER,
/* For register elimination to work properly these hard_frame_pointer_rtx,
   frame_pointer_rtx, and arg_pointer_rtx must be the same if they refer to
   the same register.  */
#if FRAME_POINTER_REGNUM == ARG_POINTER_REGNUM
  GR_ARG_POINTER = GR_FRAME_POINTER,
#endif
#if HARD_FRAME_POINTER_REGNUM == FRAME_POINTER_REGNUM
  GR_HARD_FRAME_POINTER = GR_FRAME_POINTER,
#else
  GR_HARD_FRAME_POINTER,
#endif
#if FRAME_POINTER_REGNUM != ARG_POINTER_REGNUM
#if HARD_FRAME_POINTER_REGNUM == ARG_POINTER_REGNUM
  GR_ARG_POINTER = GR_HARD_FRAME_POINTER,
#else
  GR_ARG_POINTER,
#endif
#endif
  GR_VIRTUAL_INCOMING_ARGS,
  GR_VIRTUAL_STACK_ARGS,
  GR_VIRTUAL_STACK_DYNAMIC,
  GR_VIRTUAL_OUTGOING_ARGS,
  GR_VIRTUAL_CFA,

  GR_MAX
};

/* Pointers to standard pieces of rtx are stored here.  */
extern GTY(()) rtx global_rtl[GR_MAX];

/* Standard pieces of rtx, to be substituted directly into things.  */
#define pc_rtx                  (global_rtl[GR_PC])
#define cc0_rtx                 (global_rtl[GR_CC0])

/* All references to certain hard regs, except those created
   by allocating pseudo regs into them (when that's possible),
   go through these unique rtx objects.  */
#define stack_pointer_rtx       (global_rtl[GR_STACK_POINTER])
#define frame_pointer_rtx       (global_rtl[GR_FRAME_POINTER])
#define hard_frame_pointer_rtx	(global_rtl[GR_HARD_FRAME_POINTER])
#define arg_pointer_rtx		(global_rtl[GR_ARG_POINTER])

extern GTY(()) rtx pic_offset_table_rtx;
extern GTY(()) rtx static_chain_rtx;
extern GTY(()) rtx static_chain_incoming_rtx;
extern GTY(()) rtx return_address_pointer_rtx;

/* Include the RTL generation functions.  */

#ifndef GENERATOR_FILE
#include "genrtl.h"
#ifndef USE_MAPPED_LOCATION
#undef gen_rtx_ASM_OPERANDS
#define gen_rtx_ASM_OPERANDS(MODE, ARG0, ARG1, ARG2, ARG3, ARG4, LOC) \
  gen_rtx_fmt_ssiEEsi (ASM_OPERANDS, (MODE), (ARG0), (ARG1), (ARG2), (ARG3), (ARG4), (LOC).file, (LOC).line)
#endif
#endif

/* There are some RTL codes that require special attention; the
   generation functions included above do the raw handling.  If you
   add to this list, modify special_rtx in gengenrtl.c as well.  */

extern rtx gen_rtx_CONST_INT (enum machine_mode, HOST_WIDE_INT);
extern rtx gen_rtx_CONST_VECTOR (enum machine_mode, rtvec);
extern rtx gen_raw_REG (enum machine_mode, int);
extern rtx gen_rtx_REG (enum machine_mode, unsigned);
extern rtx gen_rtx_SUBREG (enum machine_mode, rtx, int);
extern rtx gen_rtx_MEM (enum machine_mode, rtx);

#define GEN_INT(N)  gen_rtx_CONST_INT (VOIDmode, (N))

/* Virtual registers are used during RTL generation to refer to locations into
   the stack frame when the actual location isn't known until RTL generation
   is complete.  The routine instantiate_virtual_regs replaces these with
   the proper value, which is normally {frame,arg,stack}_pointer_rtx plus
   a constant.  */

#define FIRST_VIRTUAL_REGISTER	(FIRST_PSEUDO_REGISTER)

/* This points to the first word of the incoming arguments passed on the stack,
   either by the caller or by the callee when pretending it was passed by the
   caller.  */

#define virtual_incoming_args_rtx       (global_rtl[GR_VIRTUAL_INCOMING_ARGS])

#define VIRTUAL_INCOMING_ARGS_REGNUM	(FIRST_VIRTUAL_REGISTER)

/* If FRAME_GROWS_DOWNWARD, this points to immediately above the first
   variable on the stack.  Otherwise, it points to the first variable on
   the stack.  */

#define virtual_stack_vars_rtx	        (global_rtl[GR_VIRTUAL_STACK_ARGS])

#define VIRTUAL_STACK_VARS_REGNUM	((FIRST_VIRTUAL_REGISTER) + 1)

/* This points to the location of dynamically-allocated memory on the stack
   immediately after the stack pointer has been adjusted by the amount
   desired.  */

#define virtual_stack_dynamic_rtx	(global_rtl[GR_VIRTUAL_STACK_DYNAMIC])

#define VIRTUAL_STACK_DYNAMIC_REGNUM	((FIRST_VIRTUAL_REGISTER) + 2)

/* This points to the location in the stack at which outgoing arguments should
   be written when the stack is pre-pushed (arguments pushed using push
   insns always use sp).  */

#define virtual_outgoing_args_rtx	(global_rtl[GR_VIRTUAL_OUTGOING_ARGS])

#define VIRTUAL_OUTGOING_ARGS_REGNUM	((FIRST_VIRTUAL_REGISTER) + 3)

/* This points to the Canonical Frame Address of the function.  This
   should correspond to the CFA produced by INCOMING_FRAME_SP_OFFSET,
   but is calculated relative to the arg pointer for simplicity; the
   frame pointer nor stack pointer are necessarily fixed relative to
   the CFA until after reload.  */

#define virtual_cfa_rtx			(global_rtl[GR_VIRTUAL_CFA])

#define VIRTUAL_CFA_REGNUM		((FIRST_VIRTUAL_REGISTER) + 4)

#define LAST_VIRTUAL_REGISTER		((FIRST_VIRTUAL_REGISTER) + 4)

/* Nonzero if REGNUM is a pointer into the stack frame.  */
#define REGNO_PTR_FRAME_P(REGNUM)		\
  ((REGNUM) == STACK_POINTER_REGNUM		\
   || (REGNUM) == FRAME_POINTER_REGNUM		\
   || (REGNUM) == HARD_FRAME_POINTER_REGNUM	\
   || (REGNUM) == ARG_POINTER_REGNUM		\
   || ((REGNUM) >= FIRST_VIRTUAL_REGISTER	\
       && (REGNUM) <= LAST_VIRTUAL_REGISTER))

/* REGNUM never really appearing in the INSN stream.  */
#define INVALID_REGNUM			(~(unsigned int) 0)

extern rtx output_constant_def (tree, int);
extern rtx lookup_constant_def (tree);

/* Nonzero after the second flow pass has completed.
   Set to 1 or 0 by toplev.c  */
extern int flow2_completed;

/* Nonzero after end of reload pass.
   Set to 1 or 0 by reload1.c.  */

extern int reload_completed;

/* Nonzero after thread_prologue_and_epilogue_insns has run.  */
extern int epilogue_completed;

/* Set to 1 while reload_as_needed is operating.
   Required by some machines to handle any generated moves differently.  */

extern int reload_in_progress;

#ifdef STACK_REGS
/* Nonzero after end of regstack pass.
   Set to 1 or 0 by reg-stack.c.  */
extern int regstack_completed;
#endif

/* If this is nonzero, we do not bother generating VOLATILE
   around volatile memory references, and we are willing to
   output indirect addresses.  If cse is to follow, we reject
   indirect addresses so a useful potential cse is generated;
   if it is used only once, instruction combination will produce
   the same indirect address eventually.  */
extern int cse_not_expected;

/* Set to nonzero before life analysis to indicate that it is unsafe to
   generate any new pseudo registers.  */
extern int no_new_pseudos;

/* Translates rtx code to tree code, for those codes needed by
   REAL_ARITHMETIC.  The function returns an int because the caller may not
   know what `enum tree_code' means.  */

extern int rtx_to_tree_code (enum rtx_code);

/* In cse.c */
extern int delete_trivially_dead_insns (rtx, int);
extern int cse_main (rtx, int);
extern int exp_equiv_p (rtx, rtx, int, bool);
extern unsigned hash_rtx (rtx x, enum machine_mode, int *, int *, bool);

/* In jump.c */
extern int comparison_dominates_p (enum rtx_code, enum rtx_code);
extern int condjump_p (rtx);
extern int any_condjump_p (rtx);
extern int any_uncondjump_p (rtx);
extern rtx pc_set (rtx);
extern rtx condjump_label (rtx);
extern int simplejump_p (rtx);
extern int returnjump_p (rtx);
extern int onlyjump_p (rtx);
extern int only_sets_cc0_p (rtx);
extern int sets_cc0_p (rtx);
extern int invert_jump_1 (rtx, rtx);
extern int invert_jump (rtx, rtx, int);
extern int rtx_renumbered_equal_p (rtx, rtx);
extern int true_regnum (rtx);
extern unsigned int reg_or_subregno (rtx);
extern int redirect_jump_1 (rtx, rtx);
extern void redirect_jump_2 (rtx, rtx, rtx, int, int);
extern int redirect_jump (rtx, rtx, int);
extern void rebuild_jump_labels (rtx);
extern rtx reversed_comparison (rtx, enum machine_mode);
extern enum rtx_code reversed_comparison_code (rtx, rtx);
extern enum rtx_code reversed_comparison_code_parts (enum rtx_code,
						     rtx, rtx, rtx);
extern void delete_for_peephole (rtx, rtx);
extern int condjump_in_parallel_p (rtx);
extern unsigned int purge_line_number_notes (void);

/* In emit-rtl.c.  */
extern int max_reg_num (void);
extern int max_label_num (void);
extern int get_first_label_num (void);
extern void maybe_set_first_label_num (rtx);
extern void delete_insns_since (rtx);
extern void mark_reg_pointer (rtx, int);
extern void mark_user_reg (rtx);
extern void reset_used_flags (rtx);
extern void set_used_flags (rtx);
extern void reorder_insns (rtx, rtx, rtx);
extern void reorder_insns_nobb (rtx, rtx, rtx);
extern int get_max_uid (void);
extern int in_sequence_p (void);
extern void force_next_line_note (void);
extern void init_emit (void);
extern void init_emit_once (int);
extern void push_topmost_sequence (void);
extern void pop_topmost_sequence (void);
extern void set_new_first_and_last_insn (rtx, rtx);
extern unsigned int unshare_all_rtl (void);
extern void unshare_all_rtl_again (rtx);
extern void unshare_all_rtl_in_chain (rtx);
extern void verify_rtl_sharing (void);
extern void set_first_insn (rtx);
extern void set_last_insn (rtx);
extern void link_cc0_insns (rtx);
extern void add_insn (rtx);
extern void add_insn_before (rtx, rtx);
extern void add_insn_after (rtx, rtx);
extern void remove_insn (rtx);
extern void emit_insn_after_with_line_notes (rtx, rtx, rtx);
extern rtx emit (rtx);
extern void renumber_insns (void);
extern rtx delete_insn (rtx);
extern rtx entry_of_function (void);
extern void emit_insn_at_entry (rtx);
extern void delete_insn_chain (rtx, rtx);
extern rtx unlink_insn_chain (rtx, rtx);
extern rtx delete_insn_and_edges (rtx);
extern void delete_insn_chain_and_edges (rtx, rtx);
extern rtx gen_lowpart_SUBREG (enum machine_mode, rtx);
extern rtx gen_const_mem (enum machine_mode, rtx);
extern rtx gen_frame_mem (enum machine_mode, rtx);
extern rtx gen_tmp_stack_mem (enum machine_mode, rtx);
extern bool validate_subreg (enum machine_mode, enum machine_mode,
			     rtx, unsigned int);

/* In combine.c */
extern unsigned int extended_count (rtx, enum machine_mode, int);
extern rtx remove_death (unsigned int, rtx);
extern void dump_combine_stats (FILE *);
extern void dump_combine_total_stats (FILE *);

/* In sched-vis.c.  */
extern void print_rtl_slim_with_bb (FILE *, rtx, int);
extern void dump_insn_slim (FILE *f, rtx x);
extern void debug_insn_slim (rtx x);

/* In sched-rgn.c.  */
extern void schedule_insns (void);

/* In sched-ebb.c.  */
extern void schedule_ebbs (void);

/* In haifa-sched.c.  */
extern void fix_sched_param (const char *, const char *);

/* In print-rtl.c */
extern const char *print_rtx_head;
extern void debug_rtx (rtx);
extern void debug_rtx_list (rtx, int);
extern void debug_rtx_range (rtx, rtx);
extern rtx debug_rtx_find (rtx, int);
extern void print_mem_expr (FILE *, tree);
extern void print_rtl (FILE *, rtx);
extern void print_simple_rtl (FILE *, rtx);
extern int print_rtl_single (FILE *, rtx);
extern void print_inline_rtx (FILE *, rtx, int);

/* In bt-load.c */
extern void branch_target_load_optimize (bool);

/* In function.c */
extern void reposition_prologue_and_epilogue_notes (rtx);
extern void thread_prologue_and_epilogue_insns (rtx);
extern int prologue_epilogue_contains (rtx);
extern int sibcall_epilogue_contains (rtx);
extern void mark_temp_addr_taken (rtx);
extern void update_temp_slot_address (rtx, rtx);

/* In stmt.c */
extern void expand_null_return (void);
extern void expand_naked_return (void);
extern void emit_jump (rtx);

/* In expr.c */
extern rtx move_by_pieces (rtx, rtx, unsigned HOST_WIDE_INT,
			   unsigned int, int);

/* In flow.c */
extern void delete_dead_jumptables (void);
extern void print_rtl_with_bb (FILE *, rtx);
extern void dump_flow_info (FILE *, int);

/* In expmed.c */
extern void init_expmed (void);
extern void expand_inc (rtx, rtx);
extern void expand_dec (rtx, rtx);

/* In gcse.c */
extern bool can_copy_p (enum machine_mode);
extern rtx fis_get_condition (rtx);

/* In global.c */
extern void mark_elimination (int, int);
extern void dump_global_regs (FILE *);
#ifdef HARD_CONST
/* Yes, this ifdef is silly, but HARD_REG_SET is not always defined.  */
extern void retry_global_alloc (int, HARD_REG_SET);
#endif
extern void build_insn_chain (rtx);

/* In regclass.c */
extern int reg_classes_intersect_p (enum reg_class, enum reg_class);
extern int reg_class_subset_p (enum reg_class, enum reg_class);
extern void globalize_reg (int);
extern void init_reg_modes_once (void);
extern void init_regs (void);
extern void init_fake_stack_mems (void);
extern void init_reg_sets (void);
extern void regclass_init (void);
extern void regclass (rtx, int);
extern void reg_scan (rtx, unsigned int);
extern void fix_register (const char *, int, int);
extern void init_subregs_of_mode (void);
extern void record_subregs_of_mode (rtx);
#ifdef HARD_CONST
extern void cannot_change_mode_set_regs (HARD_REG_SET *,
					 enum machine_mode, unsigned int);
#endif
extern bool invalid_mode_change_p (unsigned int, enum reg_class,
				   enum machine_mode);

/* In reorg.c */
extern void dbr_schedule (rtx);

/* In local-alloc.c */
extern void dump_local_alloc (FILE *);

/* In reload1.c */
extern int function_invariant_p (rtx);

/* In calls.c */
enum libcall_type
{
  LCT_NORMAL = 0,
  LCT_CONST = 1,
  LCT_PURE = 2,
  LCT_CONST_MAKE_BLOCK = 3,
  LCT_PURE_MAKE_BLOCK = 4,
  LCT_NORETURN = 5,
  LCT_THROW = 6,
  LCT_RETURNS_TWICE = 7
};

extern void emit_library_call (rtx, enum libcall_type, enum machine_mode, int,
			       ...);
extern rtx emit_library_call_value (rtx, rtx, enum libcall_type,
				    enum machine_mode, int, ...);

/* In varasm.c */
extern void init_varasm_once (void);
extern enum tls_model decl_default_tls_model (tree);
  
/* In rtl.c */
extern void traverse_md_constants (int (*) (void **, void *), void *);
struct md_constant { char *name, *value; };

/* In read-rtl.c */
extern int read_skip_spaces (FILE *);
extern bool read_rtx (FILE *, rtx *, int *);
extern void copy_rtx_ptr_loc (const void *, const void *);
extern void print_rtx_ptr_loc (const void *);
extern const char *join_c_conditions (const char *, const char *);
extern void print_c_condition (const char *);
extern const char *read_rtx_filename;
extern int read_rtx_lineno;

/* In alias.c */
extern void clear_reg_alias_info (rtx);
extern rtx canon_rtx (rtx);
extern int true_dependence (rtx, enum machine_mode, rtx, int (*)(rtx, int));
extern rtx get_addr (rtx);
extern int canon_true_dependence (rtx, enum machine_mode, rtx, rtx,
				  int (*)(rtx, int));
extern int read_dependence (rtx, rtx);
extern int anti_dependence (rtx, rtx);
extern int output_dependence (rtx, rtx);
extern void init_alias_once (void);
extern void init_alias_analysis (void);
extern void end_alias_analysis (void);
extern bool memory_modified_in_insn_p (rtx, rtx);
extern rtx find_base_term (rtx);
extern rtx gen_hard_reg_clobber (enum machine_mode, unsigned int);
extern rtx get_reg_known_value (unsigned int);
extern bool get_reg_known_equiv_p (unsigned int);

#ifdef STACK_REGS
extern int stack_regs_mentioned (rtx insn);
#endif

/* In toplev.c */
extern GTY(()) rtx stack_limit_rtx;

/* In predict.c */
extern void invert_br_probabilities (rtx);
extern bool expensive_function_p (int);
/* In tracer.c */
extern void tracer (unsigned int);

/* In var-tracking.c */
extern unsigned int variable_tracking_main (void);

/* In stor-layout.c.  */
extern void get_mode_bounds (enum machine_mode, int, enum machine_mode,
			     rtx *, rtx *);

/* In loop-unswitch.c  */
extern rtx reversed_condition (rtx);
extern rtx compare_and_jump_seq (rtx, rtx, enum rtx_code, rtx, int, rtx);

/* In loop-iv.c  */
extern rtx canon_condition (rtx);
extern void simplify_using_condition (rtx, rtx *, struct bitmap_head_def *);

struct rtl_hooks
{
  rtx (*gen_lowpart) (enum machine_mode, rtx);
  rtx (*gen_lowpart_no_emit) (enum machine_mode, rtx);
  rtx (*reg_nonzero_bits) (rtx, enum machine_mode, rtx, enum machine_mode,
			   unsigned HOST_WIDE_INT, unsigned HOST_WIDE_INT *);
  rtx (*reg_num_sign_bit_copies) (rtx, enum machine_mode, rtx, enum machine_mode,
				  unsigned int, unsigned int *);
  bool (*reg_truncated_to_mode) (enum machine_mode, rtx);

  /* Whenever you add entries here, make sure you adjust rtlhooks-def.h.  */
};

/* Each pass can provide its own.  */
extern struct rtl_hooks rtl_hooks;

/* ... but then it has to restore these.  */
extern const struct rtl_hooks general_rtl_hooks;

/* Keep this for the nonce.  */
#define gen_lowpart rtl_hooks.gen_lowpart

#endif /* ! GCC_RTL_H */
