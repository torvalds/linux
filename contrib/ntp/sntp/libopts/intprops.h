/* intprops.h -- properties of integer types

   Copyright (C) 2001-2005, 2009-2015 Free Software Foundation, Inc.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU Lesser General Public License as published by
   the Free Software Foundation; either version 2.1 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.  */

/* Written by Paul Eggert.  */

#ifndef _GL_INTPROPS_H
#define _GL_INTPROPS_H

#include <limits.h>

/* Return an integer value, converted to the same type as the integer
   expression E after integer type promotion.  V is the unconverted value.  */
#define _GL_INT_CONVERT(e, v) (0 * (e) + (v))

/* Act like _GL_INT_CONVERT (E, -V) but work around a bug in IRIX 6.5 cc; see
   <http://lists.gnu.org/archive/html/bug-gnulib/2011-05/msg00406.html>.  */
#define _GL_INT_NEGATE_CONVERT(e, v) (0 * (e) - (v))

/* The extra casts in the following macros work around compiler bugs,
   e.g., in Cray C 5.0.3.0.  */

/* True if the arithmetic type T is an integer type.  bool counts as
   an integer.  */
#define TYPE_IS_INTEGER(t) ((t) 1.5 == 1)

/* True if negative values of the signed integer type T use two's
   complement, ones' complement, or signed magnitude representation,
   respectively.  Much GNU code assumes two's complement, but some
   people like to be portable to all possible C hosts.  */
#define TYPE_TWOS_COMPLEMENT(t) ((t) ~ (t) 0 == (t) -1)
#define TYPE_ONES_COMPLEMENT(t) ((t) ~ (t) 0 == 0)
#define TYPE_SIGNED_MAGNITUDE(t) ((t) ~ (t) 0 < (t) -1)

/* True if the signed integer expression E uses two's complement.  */
#define _GL_INT_TWOS_COMPLEMENT(e) (~ _GL_INT_CONVERT (e, 0) == -1)

/* True if the arithmetic type T is signed.  */
#define TYPE_SIGNED(t) (! ((t) 0 < (t) -1))

/* Return 1 if the integer expression E, after integer promotion, has
   a signed type.  */
#define _GL_INT_SIGNED(e) (_GL_INT_NEGATE_CONVERT (e, 1) < 0)


/* Minimum and maximum values for integer types and expressions.  These
   macros have undefined behavior if T is signed and has padding bits.
   If this is a problem for you, please let us know how to fix it for
   your host.  */

/* The maximum and minimum values for the integer type T.  */
#define TYPE_MINIMUM(t)                                                 \
  ((t) (! TYPE_SIGNED (t)                                               \
        ? (t) 0                                                         \
        : TYPE_SIGNED_MAGNITUDE (t)                                     \
        ? ~ (t) 0                                                       \
        : ~ TYPE_MAXIMUM (t)))
#define TYPE_MAXIMUM(t)                                                 \
  ((t) (! TYPE_SIGNED (t)                                               \
        ? (t) -1                                                        \
        : ((((t) 1 << (sizeof (t) * CHAR_BIT - 2)) - 1) * 2 + 1)))

/* The maximum and minimum values for the type of the expression E,
   after integer promotion.  E should not have side effects.  */
#define _GL_INT_MINIMUM(e)                                              \
  (_GL_INT_SIGNED (e)                                                   \
   ? - _GL_INT_TWOS_COMPLEMENT (e) - _GL_SIGNED_INT_MAXIMUM (e)         \
   : _GL_INT_CONVERT (e, 0))
#define _GL_INT_MAXIMUM(e)                                              \
  (_GL_INT_SIGNED (e)                                                   \
   ? _GL_SIGNED_INT_MAXIMUM (e)                                         \
   : _GL_INT_NEGATE_CONVERT (e, 1))
#define _GL_SIGNED_INT_MAXIMUM(e)                                       \
  (((_GL_INT_CONVERT (e, 1) << (sizeof ((e) + 0) * CHAR_BIT - 2)) - 1) * 2 + 1)


/* Return 1 if the __typeof__ keyword works.  This could be done by
   'configure', but for now it's easier to do it by hand.  */
#if (2 <= __GNUC__ || defined __IBM__TYPEOF__ \
     || (0x5110 <= __SUNPRO_C && !__STDC__))
# define _GL_HAVE___TYPEOF__ 1
#else
# define _GL_HAVE___TYPEOF__ 0
#endif

/* Return 1 if the integer type or expression T might be signed.  Return 0
   if it is definitely unsigned.  This macro does not evaluate its argument,
   and expands to an integer constant expression.  */
#if _GL_HAVE___TYPEOF__
# define _GL_SIGNED_TYPE_OR_EXPR(t) TYPE_SIGNED (__typeof__ (t))
#else
# define _GL_SIGNED_TYPE_OR_EXPR(t) 1
#endif

/* Bound on length of the string representing an unsigned integer
   value representable in B bits.  log10 (2.0) < 146/485.  The
   smallest value of B where this bound is not tight is 2621.  */
#define INT_BITS_STRLEN_BOUND(b) (((b) * 146 + 484) / 485)

/* Bound on length of the string representing an integer type or expression T.
   Subtract 1 for the sign bit if T is signed, and then add 1 more for
   a minus sign if needed.

   Because _GL_SIGNED_TYPE_OR_EXPR sometimes returns 0 when its argument is
   signed, this macro may overestimate the true bound by one byte when
   applied to unsigned types of size 2, 4, 16, ... bytes.  */
#define INT_STRLEN_BOUND(t)                                     \
  (INT_BITS_STRLEN_BOUND (sizeof (t) * CHAR_BIT                 \
                          - _GL_SIGNED_TYPE_OR_EXPR (t))        \
   + _GL_SIGNED_TYPE_OR_EXPR (t))

/* Bound on buffer size needed to represent an integer type or expression T,
   including the terminating null.  */
#define INT_BUFSIZE_BOUND(t) (INT_STRLEN_BOUND (t) + 1)


/* Range overflow checks.

   The INT_<op>_RANGE_OVERFLOW macros return 1 if the corresponding C
   operators might not yield numerically correct answers due to
   arithmetic overflow.  They do not rely on undefined or
   implementation-defined behavior.  Their implementations are simple
   and straightforward, but they are a bit harder to use than the
   INT_<op>_OVERFLOW macros described below.

   Example usage:

     long int i = ...;
     long int j = ...;
     if (INT_MULTIPLY_RANGE_OVERFLOW (i, j, LONG_MIN, LONG_MAX))
       printf ("multiply would overflow");
     else
       printf ("product is %ld", i * j);

   Restrictions on *_RANGE_OVERFLOW macros:

   These macros do not check for all possible numerical problems or
   undefined or unspecified behavior: they do not check for division
   by zero, for bad shift counts, or for shifting negative numbers.

   These macros may evaluate their arguments zero or multiple times,
   so the arguments should not have side effects.  The arithmetic
   arguments (including the MIN and MAX arguments) must be of the same
   integer type after the usual arithmetic conversions, and the type
   must have minimum value MIN and maximum MAX.  Unsigned types should
   use a zero MIN of the proper type.

   These macros are tuned for constant MIN and MAX.  For commutative
   operations such as A + B, they are also tuned for constant B.  */

/* Return 1 if A + B would overflow in [MIN,MAX] arithmetic.
   See above for restrictions.  */
#define INT_ADD_RANGE_OVERFLOW(a, b, min, max)          \
  ((b) < 0                                              \
   ? (a) < (min) - (b)                                  \
   : (max) - (b) < (a))

/* Return 1 if A - B would overflow in [MIN,MAX] arithmetic.
   See above for restrictions.  */
#define INT_SUBTRACT_RANGE_OVERFLOW(a, b, min, max)     \
  ((b) < 0                                              \
   ? (max) + (b) < (a)                                  \
   : (a) < (min) + (b))

/* Return 1 if - A would overflow in [MIN,MAX] arithmetic.
   See above for restrictions.  */
#define INT_NEGATE_RANGE_OVERFLOW(a, min, max)          \
  ((min) < 0                                            \
   ? (a) < - (max)                                      \
   : 0 < (a))

/* Return 1 if A * B would overflow in [MIN,MAX] arithmetic.
   See above for restrictions.  Avoid && and || as they tickle
   bugs in Sun C 5.11 2010/08/13 and other compilers; see
   <http://lists.gnu.org/archive/html/bug-gnulib/2011-05/msg00401.html>.  */
#define INT_MULTIPLY_RANGE_OVERFLOW(a, b, min, max)     \
  ((b) < 0                                              \
   ? ((a) < 0                                           \
      ? (a) < (max) / (b)                               \
      : (b) == -1                                       \
      ? 0                                               \
      : (min) / (b) < (a))                              \
   : (b) == 0                                           \
   ? 0                                                  \
   : ((a) < 0                                           \
      ? (a) < (min) / (b)                               \
      : (max) / (b) < (a)))

/* Return 1 if A / B would overflow in [MIN,MAX] arithmetic.
   See above for restrictions.  Do not check for division by zero.  */
#define INT_DIVIDE_RANGE_OVERFLOW(a, b, min, max)       \
  ((min) < 0 && (b) == -1 && (a) < - (max))

/* Return 1 if A % B would overflow in [MIN,MAX] arithmetic.
   See above for restrictions.  Do not check for division by zero.
   Mathematically, % should never overflow, but on x86-like hosts
   INT_MIN % -1 traps, and the C standard permits this, so treat this
   as an overflow too.  */
#define INT_REMAINDER_RANGE_OVERFLOW(a, b, min, max)    \
  INT_DIVIDE_RANGE_OVERFLOW (a, b, min, max)

/* Return 1 if A << B would overflow in [MIN,MAX] arithmetic.
   See above for restrictions.  Here, MIN and MAX are for A only, and B need
   not be of the same type as the other arguments.  The C standard says that
   behavior is undefined for shifts unless 0 <= B < wordwidth, and that when
   A is negative then A << B has undefined behavior and A >> B has
   implementation-defined behavior, but do not check these other
   restrictions.  */
#define INT_LEFT_SHIFT_RANGE_OVERFLOW(a, b, min, max)   \
  ((a) < 0                                              \
   ? (a) < (min) >> (b)                                 \
   : (max) >> (b) < (a))


/* The _GL*_OVERFLOW macros have the same restrictions as the
   *_RANGE_OVERFLOW macros, except that they do not assume that operands
   (e.g., A and B) have the same type as MIN and MAX.  Instead, they assume
   that the result (e.g., A + B) has that type.  */
#define _GL_ADD_OVERFLOW(a, b, min, max)                                \
  ((min) < 0 ? INT_ADD_RANGE_OVERFLOW (a, b, min, max)                  \
   : (a) < 0 ? (b) <= (a) + (b)                                         \
   : (b) < 0 ? (a) <= (a) + (b)                                         \
   : (a) + (b) < (b))
#define _GL_SUBTRACT_OVERFLOW(a, b, min, max)                           \
  ((min) < 0 ? INT_SUBTRACT_RANGE_OVERFLOW (a, b, min, max)             \
   : (a) < 0 ? 1                                                        \
   : (b) < 0 ? (a) - (b) <= (a)                                         \
   : (a) < (b))
#define _GL_MULTIPLY_OVERFLOW(a, b, min, max)                           \
  (((min) == 0 && (((a) < 0 && 0 < (b)) || ((b) < 0 && 0 < (a))))       \
   || INT_MULTIPLY_RANGE_OVERFLOW (a, b, min, max))
#define _GL_DIVIDE_OVERFLOW(a, b, min, max)                             \
  ((min) < 0 ? (b) == _GL_INT_NEGATE_CONVERT (min, 1) && (a) < - (max)  \
   : (a) < 0 ? (b) <= (a) + (b) - 1                                     \
   : (b) < 0 && (a) + (b) <= (a))
#define _GL_REMAINDER_OVERFLOW(a, b, min, max)                          \
  ((min) < 0 ? (b) == _GL_INT_NEGATE_CONVERT (min, 1) && (a) < - (max)  \
   : (a) < 0 ? (a) % (b) != ((max) - (b) + 1) % (b)                     \
   : (b) < 0 && ! _GL_UNSIGNED_NEG_MULTIPLE (a, b, max))

/* Return a nonzero value if A is a mathematical multiple of B, where
   A is unsigned, B is negative, and MAX is the maximum value of A's
   type.  A's type must be the same as (A % B)'s type.  Normally (A %
   -B == 0) suffices, but things get tricky if -B would overflow.  */
#define _GL_UNSIGNED_NEG_MULTIPLE(a, b, max)                            \
  (((b) < -_GL_SIGNED_INT_MAXIMUM (b)                                   \
    ? (_GL_SIGNED_INT_MAXIMUM (b) == (max)                              \
       ? (a)                                                            \
       : (a) % (_GL_INT_CONVERT (a, _GL_SIGNED_INT_MAXIMUM (b)) + 1))   \
    : (a) % - (b))                                                      \
   == 0)


/* Integer overflow checks.

   The INT_<op>_OVERFLOW macros return 1 if the corresponding C operators
   might not yield numerically correct answers due to arithmetic overflow.
   They work correctly on all known practical hosts, and do not rely
   on undefined behavior due to signed arithmetic overflow.

   Example usage:

     long int i = ...;
     long int j = ...;
     if (INT_MULTIPLY_OVERFLOW (i, j))
       printf ("multiply would overflow");
     else
       printf ("product is %ld", i * j);

   These macros do not check for all possible numerical problems or
   undefined or unspecified behavior: they do not check for division
   by zero, for bad shift counts, or for shifting negative numbers.

   These macros may evaluate their arguments zero or multiple times, so the
   arguments should not have side effects.

   These macros are tuned for their last argument being a constant.

   Return 1 if the integer expressions A * B, A - B, -A, A * B, A / B,
   A % B, and A << B would overflow, respectively.  */

#define INT_ADD_OVERFLOW(a, b) \
  _GL_BINARY_OP_OVERFLOW (a, b, _GL_ADD_OVERFLOW)
#define INT_SUBTRACT_OVERFLOW(a, b) \
  _GL_BINARY_OP_OVERFLOW (a, b, _GL_SUBTRACT_OVERFLOW)
#define INT_NEGATE_OVERFLOW(a) \
  INT_NEGATE_RANGE_OVERFLOW (a, _GL_INT_MINIMUM (a), _GL_INT_MAXIMUM (a))
#define INT_MULTIPLY_OVERFLOW(a, b) \
  _GL_BINARY_OP_OVERFLOW (a, b, _GL_MULTIPLY_OVERFLOW)
#define INT_DIVIDE_OVERFLOW(a, b) \
  _GL_BINARY_OP_OVERFLOW (a, b, _GL_DIVIDE_OVERFLOW)
#define INT_REMAINDER_OVERFLOW(a, b) \
  _GL_BINARY_OP_OVERFLOW (a, b, _GL_REMAINDER_OVERFLOW)
#define INT_LEFT_SHIFT_OVERFLOW(a, b) \
  INT_LEFT_SHIFT_RANGE_OVERFLOW (a, b, \
                                 _GL_INT_MINIMUM (a), _GL_INT_MAXIMUM (a))

/* Return 1 if the expression A <op> B would overflow,
   where OP_RESULT_OVERFLOW (A, B, MIN, MAX) does the actual test,
   assuming MIN and MAX are the minimum and maximum for the result type.
   Arguments should be free of side effects.  */
#define _GL_BINARY_OP_OVERFLOW(a, b, op_result_overflow)        \
  op_result_overflow (a, b,                                     \
                      _GL_INT_MINIMUM (0 * (b) + (a)),          \
                      _GL_INT_MAXIMUM (0 * (b) + (a)))

#endif /* _GL_INTPROPS_H */
