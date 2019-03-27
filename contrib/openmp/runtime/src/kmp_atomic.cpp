/*
 * kmp_atomic.cpp -- ATOMIC implementation routines
 */

//===----------------------------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.txt for details.
//
//===----------------------------------------------------------------------===//

#include "kmp_atomic.h"
#include "kmp.h" // TRUE, asm routines prototypes

typedef unsigned char uchar;
typedef unsigned short ushort;

/*!
@defgroup ATOMIC_OPS Atomic Operations
These functions are used for implementing the many different varieties of atomic
operations.

The compiler is at liberty to inline atomic operations that are naturally
supported by the target architecture. For instance on IA-32 architecture an
atomic like this can be inlined
@code
static int s = 0;
#pragma omp atomic
    s++;
@endcode
using the single instruction: `lock; incl s`

However the runtime does provide entrypoints for these operations to support
compilers that choose not to inline them. (For instance,
`__kmpc_atomic_fixed4_add` could be used to perform the increment above.)

The names of the functions are encoded by using the data type name and the
operation name, as in these tables.

Data Type  | Data type encoding
-----------|---------------
int8_t     | `fixed1`
uint8_t    | `fixed1u`
int16_t    | `fixed2`
uint16_t   | `fixed2u`
int32_t    | `fixed4`
uint32_t   | `fixed4u`
int32_t    | `fixed8`
uint32_t   | `fixed8u`
float      | `float4`
double     | `float8`
float 10 (8087 eighty bit float)  | `float10`
complex<float>   |  `cmplx4`
complex<double>  | `cmplx8`
complex<float10> | `cmplx10`
<br>

Operation | Operation encoding
----------|-------------------
+ | add
- | sub
\* | mul
/ | div
& | andb
<< | shl
\>\> | shr
\| | orb
^  | xor
&& | andl
\|\| | orl
maximum | max
minimum | min
.eqv.   | eqv
.neqv.  | neqv

<br>
For non-commutative operations, `_rev` can also be added for the reversed
operation. For the functions that capture the result, the suffix `_cpt` is
added.

Update Functions
================
The general form of an atomic function that just performs an update (without a
`capture`)
@code
void __kmpc_atomic_<datatype>_<operation>( ident_t *id_ref, int gtid, TYPE *
lhs, TYPE rhs );
@endcode
@param ident_t  a pointer to source location
@param gtid  the global thread id
@param lhs   a pointer to the left operand
@param rhs   the right operand

`capture` functions
===================
The capture functions perform an atomic update and return a result, which is
either the value before the capture, or that after. They take an additional
argument to determine which result is returned.
Their general form is therefore
@code
TYPE __kmpc_atomic_<datatype>_<operation>_cpt( ident_t *id_ref, int gtid, TYPE *
lhs, TYPE rhs, int flag );
@endcode
@param ident_t  a pointer to source location
@param gtid  the global thread id
@param lhs   a pointer to the left operand
@param rhs   the right operand
@param flag  one if the result is to be captured *after* the operation, zero if
captured *before*.

The one set of exceptions to this is the `complex<float>` type where the value
is not returned, rather an extra argument pointer is passed.

They look like
@code
void __kmpc_atomic_cmplx4_<op>_cpt(  ident_t *id_ref, int gtid, kmp_cmplx32 *
lhs, kmp_cmplx32 rhs, kmp_cmplx32 * out, int flag );
@endcode

Read and Write Operations
=========================
The OpenMP<sup>*</sup> standard now supports atomic operations that simply
ensure that the value is read or written atomically, with no modification
performed. In many cases on IA-32 architecture these operations can be inlined
since the architecture guarantees that no tearing occurs on aligned objects
accessed with a single memory operation of up to 64 bits in size.

The general form of the read operations is
@code
TYPE __kmpc_atomic_<type>_rd ( ident_t *id_ref, int gtid, TYPE * loc );
@endcode

For the write operations the form is
@code
void __kmpc_atomic_<type>_wr ( ident_t *id_ref, int gtid, TYPE * lhs, TYPE rhs
);
@endcode

Full list of functions
======================
This leads to the generation of 376 atomic functions, as follows.

Functons for integers
---------------------
There are versions here for integers of size 1,2,4 and 8 bytes both signed and
unsigned (where that matters).
@code
    __kmpc_atomic_fixed1_add
    __kmpc_atomic_fixed1_add_cpt
    __kmpc_atomic_fixed1_add_fp
    __kmpc_atomic_fixed1_andb
    __kmpc_atomic_fixed1_andb_cpt
    __kmpc_atomic_fixed1_andl
    __kmpc_atomic_fixed1_andl_cpt
    __kmpc_atomic_fixed1_div
    __kmpc_atomic_fixed1_div_cpt
    __kmpc_atomic_fixed1_div_cpt_rev
    __kmpc_atomic_fixed1_div_float8
    __kmpc_atomic_fixed1_div_fp
    __kmpc_atomic_fixed1_div_rev
    __kmpc_atomic_fixed1_eqv
    __kmpc_atomic_fixed1_eqv_cpt
    __kmpc_atomic_fixed1_max
    __kmpc_atomic_fixed1_max_cpt
    __kmpc_atomic_fixed1_min
    __kmpc_atomic_fixed1_min_cpt
    __kmpc_atomic_fixed1_mul
    __kmpc_atomic_fixed1_mul_cpt
    __kmpc_atomic_fixed1_mul_float8
    __kmpc_atomic_fixed1_mul_fp
    __kmpc_atomic_fixed1_neqv
    __kmpc_atomic_fixed1_neqv_cpt
    __kmpc_atomic_fixed1_orb
    __kmpc_atomic_fixed1_orb_cpt
    __kmpc_atomic_fixed1_orl
    __kmpc_atomic_fixed1_orl_cpt
    __kmpc_atomic_fixed1_rd
    __kmpc_atomic_fixed1_shl
    __kmpc_atomic_fixed1_shl_cpt
    __kmpc_atomic_fixed1_shl_cpt_rev
    __kmpc_atomic_fixed1_shl_rev
    __kmpc_atomic_fixed1_shr
    __kmpc_atomic_fixed1_shr_cpt
    __kmpc_atomic_fixed1_shr_cpt_rev
    __kmpc_atomic_fixed1_shr_rev
    __kmpc_atomic_fixed1_sub
    __kmpc_atomic_fixed1_sub_cpt
    __kmpc_atomic_fixed1_sub_cpt_rev
    __kmpc_atomic_fixed1_sub_fp
    __kmpc_atomic_fixed1_sub_rev
    __kmpc_atomic_fixed1_swp
    __kmpc_atomic_fixed1_wr
    __kmpc_atomic_fixed1_xor
    __kmpc_atomic_fixed1_xor_cpt
    __kmpc_atomic_fixed1u_add_fp
    __kmpc_atomic_fixed1u_sub_fp
    __kmpc_atomic_fixed1u_mul_fp
    __kmpc_atomic_fixed1u_div
    __kmpc_atomic_fixed1u_div_cpt
    __kmpc_atomic_fixed1u_div_cpt_rev
    __kmpc_atomic_fixed1u_div_fp
    __kmpc_atomic_fixed1u_div_rev
    __kmpc_atomic_fixed1u_shr
    __kmpc_atomic_fixed1u_shr_cpt
    __kmpc_atomic_fixed1u_shr_cpt_rev
    __kmpc_atomic_fixed1u_shr_rev
    __kmpc_atomic_fixed2_add
    __kmpc_atomic_fixed2_add_cpt
    __kmpc_atomic_fixed2_add_fp
    __kmpc_atomic_fixed2_andb
    __kmpc_atomic_fixed2_andb_cpt
    __kmpc_atomic_fixed2_andl
    __kmpc_atomic_fixed2_andl_cpt
    __kmpc_atomic_fixed2_div
    __kmpc_atomic_fixed2_div_cpt
    __kmpc_atomic_fixed2_div_cpt_rev
    __kmpc_atomic_fixed2_div_float8
    __kmpc_atomic_fixed2_div_fp
    __kmpc_atomic_fixed2_div_rev
    __kmpc_atomic_fixed2_eqv
    __kmpc_atomic_fixed2_eqv_cpt
    __kmpc_atomic_fixed2_max
    __kmpc_atomic_fixed2_max_cpt
    __kmpc_atomic_fixed2_min
    __kmpc_atomic_fixed2_min_cpt
    __kmpc_atomic_fixed2_mul
    __kmpc_atomic_fixed2_mul_cpt
    __kmpc_atomic_fixed2_mul_float8
    __kmpc_atomic_fixed2_mul_fp
    __kmpc_atomic_fixed2_neqv
    __kmpc_atomic_fixed2_neqv_cpt
    __kmpc_atomic_fixed2_orb
    __kmpc_atomic_fixed2_orb_cpt
    __kmpc_atomic_fixed2_orl
    __kmpc_atomic_fixed2_orl_cpt
    __kmpc_atomic_fixed2_rd
    __kmpc_atomic_fixed2_shl
    __kmpc_atomic_fixed2_shl_cpt
    __kmpc_atomic_fixed2_shl_cpt_rev
    __kmpc_atomic_fixed2_shl_rev
    __kmpc_atomic_fixed2_shr
    __kmpc_atomic_fixed2_shr_cpt
    __kmpc_atomic_fixed2_shr_cpt_rev
    __kmpc_atomic_fixed2_shr_rev
    __kmpc_atomic_fixed2_sub
    __kmpc_atomic_fixed2_sub_cpt
    __kmpc_atomic_fixed2_sub_cpt_rev
    __kmpc_atomic_fixed2_sub_fp
    __kmpc_atomic_fixed2_sub_rev
    __kmpc_atomic_fixed2_swp
    __kmpc_atomic_fixed2_wr
    __kmpc_atomic_fixed2_xor
    __kmpc_atomic_fixed2_xor_cpt
    __kmpc_atomic_fixed2u_add_fp
    __kmpc_atomic_fixed2u_sub_fp
    __kmpc_atomic_fixed2u_mul_fp
    __kmpc_atomic_fixed2u_div
    __kmpc_atomic_fixed2u_div_cpt
    __kmpc_atomic_fixed2u_div_cpt_rev
    __kmpc_atomic_fixed2u_div_fp
    __kmpc_atomic_fixed2u_div_rev
    __kmpc_atomic_fixed2u_shr
    __kmpc_atomic_fixed2u_shr_cpt
    __kmpc_atomic_fixed2u_shr_cpt_rev
    __kmpc_atomic_fixed2u_shr_rev
    __kmpc_atomic_fixed4_add
    __kmpc_atomic_fixed4_add_cpt
    __kmpc_atomic_fixed4_add_fp
    __kmpc_atomic_fixed4_andb
    __kmpc_atomic_fixed4_andb_cpt
    __kmpc_atomic_fixed4_andl
    __kmpc_atomic_fixed4_andl_cpt
    __kmpc_atomic_fixed4_div
    __kmpc_atomic_fixed4_div_cpt
    __kmpc_atomic_fixed4_div_cpt_rev
    __kmpc_atomic_fixed4_div_float8
    __kmpc_atomic_fixed4_div_fp
    __kmpc_atomic_fixed4_div_rev
    __kmpc_atomic_fixed4_eqv
    __kmpc_atomic_fixed4_eqv_cpt
    __kmpc_atomic_fixed4_max
    __kmpc_atomic_fixed4_max_cpt
    __kmpc_atomic_fixed4_min
    __kmpc_atomic_fixed4_min_cpt
    __kmpc_atomic_fixed4_mul
    __kmpc_atomic_fixed4_mul_cpt
    __kmpc_atomic_fixed4_mul_float8
    __kmpc_atomic_fixed4_mul_fp
    __kmpc_atomic_fixed4_neqv
    __kmpc_atomic_fixed4_neqv_cpt
    __kmpc_atomic_fixed4_orb
    __kmpc_atomic_fixed4_orb_cpt
    __kmpc_atomic_fixed4_orl
    __kmpc_atomic_fixed4_orl_cpt
    __kmpc_atomic_fixed4_rd
    __kmpc_atomic_fixed4_shl
    __kmpc_atomic_fixed4_shl_cpt
    __kmpc_atomic_fixed4_shl_cpt_rev
    __kmpc_atomic_fixed4_shl_rev
    __kmpc_atomic_fixed4_shr
    __kmpc_atomic_fixed4_shr_cpt
    __kmpc_atomic_fixed4_shr_cpt_rev
    __kmpc_atomic_fixed4_shr_rev
    __kmpc_atomic_fixed4_sub
    __kmpc_atomic_fixed4_sub_cpt
    __kmpc_atomic_fixed4_sub_cpt_rev
    __kmpc_atomic_fixed4_sub_fp
    __kmpc_atomic_fixed4_sub_rev
    __kmpc_atomic_fixed4_swp
    __kmpc_atomic_fixed4_wr
    __kmpc_atomic_fixed4_xor
    __kmpc_atomic_fixed4_xor_cpt
    __kmpc_atomic_fixed4u_add_fp
    __kmpc_atomic_fixed4u_sub_fp
    __kmpc_atomic_fixed4u_mul_fp
    __kmpc_atomic_fixed4u_div
    __kmpc_atomic_fixed4u_div_cpt
    __kmpc_atomic_fixed4u_div_cpt_rev
    __kmpc_atomic_fixed4u_div_fp
    __kmpc_atomic_fixed4u_div_rev
    __kmpc_atomic_fixed4u_shr
    __kmpc_atomic_fixed4u_shr_cpt
    __kmpc_atomic_fixed4u_shr_cpt_rev
    __kmpc_atomic_fixed4u_shr_rev
    __kmpc_atomic_fixed8_add
    __kmpc_atomic_fixed8_add_cpt
    __kmpc_atomic_fixed8_add_fp
    __kmpc_atomic_fixed8_andb
    __kmpc_atomic_fixed8_andb_cpt
    __kmpc_atomic_fixed8_andl
    __kmpc_atomic_fixed8_andl_cpt
    __kmpc_atomic_fixed8_div
    __kmpc_atomic_fixed8_div_cpt
    __kmpc_atomic_fixed8_div_cpt_rev
    __kmpc_atomic_fixed8_div_float8
    __kmpc_atomic_fixed8_div_fp
    __kmpc_atomic_fixed8_div_rev
    __kmpc_atomic_fixed8_eqv
    __kmpc_atomic_fixed8_eqv_cpt
    __kmpc_atomic_fixed8_max
    __kmpc_atomic_fixed8_max_cpt
    __kmpc_atomic_fixed8_min
    __kmpc_atomic_fixed8_min_cpt
    __kmpc_atomic_fixed8_mul
    __kmpc_atomic_fixed8_mul_cpt
    __kmpc_atomic_fixed8_mul_float8
    __kmpc_atomic_fixed8_mul_fp
    __kmpc_atomic_fixed8_neqv
    __kmpc_atomic_fixed8_neqv_cpt
    __kmpc_atomic_fixed8_orb
    __kmpc_atomic_fixed8_orb_cpt
    __kmpc_atomic_fixed8_orl
    __kmpc_atomic_fixed8_orl_cpt
    __kmpc_atomic_fixed8_rd
    __kmpc_atomic_fixed8_shl
    __kmpc_atomic_fixed8_shl_cpt
    __kmpc_atomic_fixed8_shl_cpt_rev
    __kmpc_atomic_fixed8_shl_rev
    __kmpc_atomic_fixed8_shr
    __kmpc_atomic_fixed8_shr_cpt
    __kmpc_atomic_fixed8_shr_cpt_rev
    __kmpc_atomic_fixed8_shr_rev
    __kmpc_atomic_fixed8_sub
    __kmpc_atomic_fixed8_sub_cpt
    __kmpc_atomic_fixed8_sub_cpt_rev
    __kmpc_atomic_fixed8_sub_fp
    __kmpc_atomic_fixed8_sub_rev
    __kmpc_atomic_fixed8_swp
    __kmpc_atomic_fixed8_wr
    __kmpc_atomic_fixed8_xor
    __kmpc_atomic_fixed8_xor_cpt
    __kmpc_atomic_fixed8u_add_fp
    __kmpc_atomic_fixed8u_sub_fp
    __kmpc_atomic_fixed8u_mul_fp
    __kmpc_atomic_fixed8u_div
    __kmpc_atomic_fixed8u_div_cpt
    __kmpc_atomic_fixed8u_div_cpt_rev
    __kmpc_atomic_fixed8u_div_fp
    __kmpc_atomic_fixed8u_div_rev
    __kmpc_atomic_fixed8u_shr
    __kmpc_atomic_fixed8u_shr_cpt
    __kmpc_atomic_fixed8u_shr_cpt_rev
    __kmpc_atomic_fixed8u_shr_rev
@endcode

Functions for floating point
----------------------------
There are versions here for floating point numbers of size 4, 8, 10 and 16
bytes. (Ten byte floats are used by X87, but are now rare).
@code
    __kmpc_atomic_float4_add
    __kmpc_atomic_float4_add_cpt
    __kmpc_atomic_float4_add_float8
    __kmpc_atomic_float4_add_fp
    __kmpc_atomic_float4_div
    __kmpc_atomic_float4_div_cpt
    __kmpc_atomic_float4_div_cpt_rev
    __kmpc_atomic_float4_div_float8
    __kmpc_atomic_float4_div_fp
    __kmpc_atomic_float4_div_rev
    __kmpc_atomic_float4_max
    __kmpc_atomic_float4_max_cpt
    __kmpc_atomic_float4_min
    __kmpc_atomic_float4_min_cpt
    __kmpc_atomic_float4_mul
    __kmpc_atomic_float4_mul_cpt
    __kmpc_atomic_float4_mul_float8
    __kmpc_atomic_float4_mul_fp
    __kmpc_atomic_float4_rd
    __kmpc_atomic_float4_sub
    __kmpc_atomic_float4_sub_cpt
    __kmpc_atomic_float4_sub_cpt_rev
    __kmpc_atomic_float4_sub_float8
    __kmpc_atomic_float4_sub_fp
    __kmpc_atomic_float4_sub_rev
    __kmpc_atomic_float4_swp
    __kmpc_atomic_float4_wr
    __kmpc_atomic_float8_add
    __kmpc_atomic_float8_add_cpt
    __kmpc_atomic_float8_add_fp
    __kmpc_atomic_float8_div
    __kmpc_atomic_float8_div_cpt
    __kmpc_atomic_float8_div_cpt_rev
    __kmpc_atomic_float8_div_fp
    __kmpc_atomic_float8_div_rev
    __kmpc_atomic_float8_max
    __kmpc_atomic_float8_max_cpt
    __kmpc_atomic_float8_min
    __kmpc_atomic_float8_min_cpt
    __kmpc_atomic_float8_mul
    __kmpc_atomic_float8_mul_cpt
    __kmpc_atomic_float8_mul_fp
    __kmpc_atomic_float8_rd
    __kmpc_atomic_float8_sub
    __kmpc_atomic_float8_sub_cpt
    __kmpc_atomic_float8_sub_cpt_rev
    __kmpc_atomic_float8_sub_fp
    __kmpc_atomic_float8_sub_rev
    __kmpc_atomic_float8_swp
    __kmpc_atomic_float8_wr
    __kmpc_atomic_float10_add
    __kmpc_atomic_float10_add_cpt
    __kmpc_atomic_float10_add_fp
    __kmpc_atomic_float10_div
    __kmpc_atomic_float10_div_cpt
    __kmpc_atomic_float10_div_cpt_rev
    __kmpc_atomic_float10_div_fp
    __kmpc_atomic_float10_div_rev
    __kmpc_atomic_float10_mul
    __kmpc_atomic_float10_mul_cpt
    __kmpc_atomic_float10_mul_fp
    __kmpc_atomic_float10_rd
    __kmpc_atomic_float10_sub
    __kmpc_atomic_float10_sub_cpt
    __kmpc_atomic_float10_sub_cpt_rev
    __kmpc_atomic_float10_sub_fp
    __kmpc_atomic_float10_sub_rev
    __kmpc_atomic_float10_swp
    __kmpc_atomic_float10_wr
    __kmpc_atomic_float16_add
    __kmpc_atomic_float16_add_cpt
    __kmpc_atomic_float16_div
    __kmpc_atomic_float16_div_cpt
    __kmpc_atomic_float16_div_cpt_rev
    __kmpc_atomic_float16_div_rev
    __kmpc_atomic_float16_max
    __kmpc_atomic_float16_max_cpt
    __kmpc_atomic_float16_min
    __kmpc_atomic_float16_min_cpt
    __kmpc_atomic_float16_mul
    __kmpc_atomic_float16_mul_cpt
    __kmpc_atomic_float16_rd
    __kmpc_atomic_float16_sub
    __kmpc_atomic_float16_sub_cpt
    __kmpc_atomic_float16_sub_cpt_rev
    __kmpc_atomic_float16_sub_rev
    __kmpc_atomic_float16_swp
    __kmpc_atomic_float16_wr
@endcode

Functions for Complex types
---------------------------
Functions for complex types whose component floating point variables are of size
4,8,10 or 16 bytes. The names here are based on the size of the component float,
*not* the size of the complex type. So `__kmpc_atomc_cmplx8_add` is an operation
on a `complex<double>` or `complex(kind=8)`, *not* `complex<float>`.

@code
    __kmpc_atomic_cmplx4_add
    __kmpc_atomic_cmplx4_add_cmplx8
    __kmpc_atomic_cmplx4_add_cpt
    __kmpc_atomic_cmplx4_div
    __kmpc_atomic_cmplx4_div_cmplx8
    __kmpc_atomic_cmplx4_div_cpt
    __kmpc_atomic_cmplx4_div_cpt_rev
    __kmpc_atomic_cmplx4_div_rev
    __kmpc_atomic_cmplx4_mul
    __kmpc_atomic_cmplx4_mul_cmplx8
    __kmpc_atomic_cmplx4_mul_cpt
    __kmpc_atomic_cmplx4_rd
    __kmpc_atomic_cmplx4_sub
    __kmpc_atomic_cmplx4_sub_cmplx8
    __kmpc_atomic_cmplx4_sub_cpt
    __kmpc_atomic_cmplx4_sub_cpt_rev
    __kmpc_atomic_cmplx4_sub_rev
    __kmpc_atomic_cmplx4_swp
    __kmpc_atomic_cmplx4_wr
    __kmpc_atomic_cmplx8_add
    __kmpc_atomic_cmplx8_add_cpt
    __kmpc_atomic_cmplx8_div
    __kmpc_atomic_cmplx8_div_cpt
    __kmpc_atomic_cmplx8_div_cpt_rev
    __kmpc_atomic_cmplx8_div_rev
    __kmpc_atomic_cmplx8_mul
    __kmpc_atomic_cmplx8_mul_cpt
    __kmpc_atomic_cmplx8_rd
    __kmpc_atomic_cmplx8_sub
    __kmpc_atomic_cmplx8_sub_cpt
    __kmpc_atomic_cmplx8_sub_cpt_rev
    __kmpc_atomic_cmplx8_sub_rev
    __kmpc_atomic_cmplx8_swp
    __kmpc_atomic_cmplx8_wr
    __kmpc_atomic_cmplx10_add
    __kmpc_atomic_cmplx10_add_cpt
    __kmpc_atomic_cmplx10_div
    __kmpc_atomic_cmplx10_div_cpt
    __kmpc_atomic_cmplx10_div_cpt_rev
    __kmpc_atomic_cmplx10_div_rev
    __kmpc_atomic_cmplx10_mul
    __kmpc_atomic_cmplx10_mul_cpt
    __kmpc_atomic_cmplx10_rd
    __kmpc_atomic_cmplx10_sub
    __kmpc_atomic_cmplx10_sub_cpt
    __kmpc_atomic_cmplx10_sub_cpt_rev
    __kmpc_atomic_cmplx10_sub_rev
    __kmpc_atomic_cmplx10_swp
    __kmpc_atomic_cmplx10_wr
    __kmpc_atomic_cmplx16_add
    __kmpc_atomic_cmplx16_add_cpt
    __kmpc_atomic_cmplx16_div
    __kmpc_atomic_cmplx16_div_cpt
    __kmpc_atomic_cmplx16_div_cpt_rev
    __kmpc_atomic_cmplx16_div_rev
    __kmpc_atomic_cmplx16_mul
    __kmpc_atomic_cmplx16_mul_cpt
    __kmpc_atomic_cmplx16_rd
    __kmpc_atomic_cmplx16_sub
    __kmpc_atomic_cmplx16_sub_cpt
    __kmpc_atomic_cmplx16_sub_cpt_rev
    __kmpc_atomic_cmplx16_swp
    __kmpc_atomic_cmplx16_wr
@endcode
*/

/*!
@ingroup ATOMIC_OPS
@{
*/

/*
 * Global vars
 */

#ifndef KMP_GOMP_COMPAT
int __kmp_atomic_mode = 1; // Intel perf
#else
int __kmp_atomic_mode = 2; // GOMP compatibility
#endif /* KMP_GOMP_COMPAT */

KMP_ALIGN(128)

// Control access to all user coded atomics in Gnu compat mode
kmp_atomic_lock_t __kmp_atomic_lock;
// Control access to all user coded atomics for 1-byte fixed data types
kmp_atomic_lock_t __kmp_atomic_lock_1i;
// Control access to all user coded atomics for 2-byte fixed data types
kmp_atomic_lock_t __kmp_atomic_lock_2i;
// Control access to all user coded atomics for 4-byte fixed data types
kmp_atomic_lock_t __kmp_atomic_lock_4i;
// Control access to all user coded atomics for kmp_real32 data type
kmp_atomic_lock_t __kmp_atomic_lock_4r;
// Control access to all user coded atomics for 8-byte fixed data types
kmp_atomic_lock_t __kmp_atomic_lock_8i;
// Control access to all user coded atomics for kmp_real64 data type
kmp_atomic_lock_t __kmp_atomic_lock_8r;
// Control access to all user coded atomics for complex byte data type
kmp_atomic_lock_t __kmp_atomic_lock_8c;
// Control access to all user coded atomics for long double data type
kmp_atomic_lock_t __kmp_atomic_lock_10r;
// Control access to all user coded atomics for _Quad data type
kmp_atomic_lock_t __kmp_atomic_lock_16r;
// Control access to all user coded atomics for double complex data type
kmp_atomic_lock_t __kmp_atomic_lock_16c;
// Control access to all user coded atomics for long double complex type
kmp_atomic_lock_t __kmp_atomic_lock_20c;
// Control access to all user coded atomics for _Quad complex data type
kmp_atomic_lock_t __kmp_atomic_lock_32c;

/* 2007-03-02:
   Without "volatile" specifier in OP_CMPXCHG and MIN_MAX_CMPXCHG we have a bug
   on *_32 and *_32e. This is just a temporary workaround for the problem. It
   seems the right solution is writing OP_CMPXCHG and MIN_MAX_CMPXCHG routines
   in assembler language. */
#define KMP_ATOMIC_VOLATILE volatile

#if (KMP_ARCH_X86) && KMP_HAVE_QUAD

static inline void operator+=(Quad_a4_t &lhs, Quad_a4_t &rhs) {
  lhs.q += rhs.q;
}
static inline void operator-=(Quad_a4_t &lhs, Quad_a4_t &rhs) {
  lhs.q -= rhs.q;
}
static inline void operator*=(Quad_a4_t &lhs, Quad_a4_t &rhs) {
  lhs.q *= rhs.q;
}
static inline void operator/=(Quad_a4_t &lhs, Quad_a4_t &rhs) {
  lhs.q /= rhs.q;
}
static inline bool operator<(Quad_a4_t &lhs, Quad_a4_t &rhs) {
  return lhs.q < rhs.q;
}
static inline bool operator>(Quad_a4_t &lhs, Quad_a4_t &rhs) {
  return lhs.q > rhs.q;
}

static inline void operator+=(Quad_a16_t &lhs, Quad_a16_t &rhs) {
  lhs.q += rhs.q;
}
static inline void operator-=(Quad_a16_t &lhs, Quad_a16_t &rhs) {
  lhs.q -= rhs.q;
}
static inline void operator*=(Quad_a16_t &lhs, Quad_a16_t &rhs) {
  lhs.q *= rhs.q;
}
static inline void operator/=(Quad_a16_t &lhs, Quad_a16_t &rhs) {
  lhs.q /= rhs.q;
}
static inline bool operator<(Quad_a16_t &lhs, Quad_a16_t &rhs) {
  return lhs.q < rhs.q;
}
static inline bool operator>(Quad_a16_t &lhs, Quad_a16_t &rhs) {
  return lhs.q > rhs.q;
}

static inline void operator+=(kmp_cmplx128_a4_t &lhs, kmp_cmplx128_a4_t &rhs) {
  lhs.q += rhs.q;
}
static inline void operator-=(kmp_cmplx128_a4_t &lhs, kmp_cmplx128_a4_t &rhs) {
  lhs.q -= rhs.q;
}
static inline void operator*=(kmp_cmplx128_a4_t &lhs, kmp_cmplx128_a4_t &rhs) {
  lhs.q *= rhs.q;
}
static inline void operator/=(kmp_cmplx128_a4_t &lhs, kmp_cmplx128_a4_t &rhs) {
  lhs.q /= rhs.q;
}

static inline void operator+=(kmp_cmplx128_a16_t &lhs,
                              kmp_cmplx128_a16_t &rhs) {
  lhs.q += rhs.q;
}
static inline void operator-=(kmp_cmplx128_a16_t &lhs,
                              kmp_cmplx128_a16_t &rhs) {
  lhs.q -= rhs.q;
}
static inline void operator*=(kmp_cmplx128_a16_t &lhs,
                              kmp_cmplx128_a16_t &rhs) {
  lhs.q *= rhs.q;
}
static inline void operator/=(kmp_cmplx128_a16_t &lhs,
                              kmp_cmplx128_a16_t &rhs) {
  lhs.q /= rhs.q;
}

#endif

// ATOMIC implementation routines -----------------------------------------
// One routine for each operation and operand type.
// All routines declarations looks like
// void __kmpc_atomic_RTYPE_OP( ident_t*, int, TYPE *lhs, TYPE rhs );

#define KMP_CHECK_GTID                                                         \
  if (gtid == KMP_GTID_UNKNOWN) {                                              \
    gtid = __kmp_entry_gtid();                                                 \
  } // check and get gtid when needed

// Beginning of a definition (provides name, parameters, gebug trace)
//     TYPE_ID - operands type and size (fixed*, fixed*u for signed, unsigned
//     fixed)
//     OP_ID   - operation identifier (add, sub, mul, ...)
//     TYPE    - operands' type
#define ATOMIC_BEGIN(TYPE_ID, OP_ID, TYPE, RET_TYPE)                           \
  RET_TYPE __kmpc_atomic_##TYPE_ID##_##OP_ID(ident_t *id_ref, int gtid,        \
                                             TYPE *lhs, TYPE rhs) {            \
    KMP_DEBUG_ASSERT(__kmp_init_serial);                                       \
    KA_TRACE(100, ("__kmpc_atomic_" #TYPE_ID "_" #OP_ID ": T#%d\n", gtid));

// ------------------------------------------------------------------------
// Lock variables used for critical sections for various size operands
#define ATOMIC_LOCK0 __kmp_atomic_lock // all types, for Gnu compat
#define ATOMIC_LOCK1i __kmp_atomic_lock_1i // char
#define ATOMIC_LOCK2i __kmp_atomic_lock_2i // short
#define ATOMIC_LOCK4i __kmp_atomic_lock_4i // long int
#define ATOMIC_LOCK4r __kmp_atomic_lock_4r // float
#define ATOMIC_LOCK8i __kmp_atomic_lock_8i // long long int
#define ATOMIC_LOCK8r __kmp_atomic_lock_8r // double
#define ATOMIC_LOCK8c __kmp_atomic_lock_8c // float complex
#define ATOMIC_LOCK10r __kmp_atomic_lock_10r // long double
#define ATOMIC_LOCK16r __kmp_atomic_lock_16r // _Quad
#define ATOMIC_LOCK16c __kmp_atomic_lock_16c // double complex
#define ATOMIC_LOCK20c __kmp_atomic_lock_20c // long double complex
#define ATOMIC_LOCK32c __kmp_atomic_lock_32c // _Quad complex

// ------------------------------------------------------------------------
// Operation on *lhs, rhs bound by critical section
//     OP     - operator (it's supposed to contain an assignment)
//     LCK_ID - lock identifier
// Note: don't check gtid as it should always be valid
// 1, 2-byte - expect valid parameter, other - check before this macro
#define OP_CRITICAL(OP, LCK_ID)                                                \
  __kmp_acquire_atomic_lock(&ATOMIC_LOCK##LCK_ID, gtid);                       \
                                                                               \
  (*lhs) OP(rhs);                                                              \
                                                                               \
  __kmp_release_atomic_lock(&ATOMIC_LOCK##LCK_ID, gtid);

// ------------------------------------------------------------------------
// For GNU compatibility, we may need to use a critical section,
// even though it is not required by the ISA.
//
// On IA-32 architecture, all atomic operations except for fixed 4 byte add,
// sub, and bitwise logical ops, and 1 & 2 byte logical ops use a common
// critical section.  On Intel(R) 64, all atomic operations are done with fetch
// and add or compare and exchange.  Therefore, the FLAG parameter to this
// macro is either KMP_ARCH_X86 or 0 (or 1, for Intel-specific extension which
// require a critical section, where we predict that they will be implemented
// in the Gnu codegen by calling GOMP_atomic_start() / GOMP_atomic_end()).
//
// When the OP_GOMP_CRITICAL macro is used in a *CRITICAL* macro construct,
// the FLAG parameter should always be 1.  If we know that we will be using
// a critical section, then we want to make certain that we use the generic
// lock __kmp_atomic_lock to protect the atomic update, and not of of the
// locks that are specialized based upon the size or type of the data.
//
// If FLAG is 0, then we are relying on dead code elimination by the build
// compiler to get rid of the useless block of code, and save a needless
// branch at runtime.

#ifdef KMP_GOMP_COMPAT
#define OP_GOMP_CRITICAL(OP, FLAG)                                             \
  if ((FLAG) && (__kmp_atomic_mode == 2)) {                                    \
    KMP_CHECK_GTID;                                                            \
    OP_CRITICAL(OP, 0);                                                        \
    return;                                                                    \
  }
#else
#define OP_GOMP_CRITICAL(OP, FLAG)
#endif /* KMP_GOMP_COMPAT */

#if KMP_MIC
#define KMP_DO_PAUSE _mm_delay_32(1)
#else
#define KMP_DO_PAUSE KMP_CPU_PAUSE()
#endif /* KMP_MIC */

// ------------------------------------------------------------------------
// Operation on *lhs, rhs using "compare_and_store" routine
//     TYPE    - operands' type
//     BITS    - size in bits, used to distinguish low level calls
//     OP      - operator
#define OP_CMPXCHG(TYPE, BITS, OP)                                             \
  {                                                                            \
    TYPE old_value, new_value;                                                 \
    old_value = *(TYPE volatile *)lhs;                                         \
    new_value = old_value OP rhs;                                              \
    while (!KMP_COMPARE_AND_STORE_ACQ##BITS(                                   \
        (kmp_int##BITS *)lhs, *VOLATILE_CAST(kmp_int##BITS *) & old_value,     \
        *VOLATILE_CAST(kmp_int##BITS *) & new_value)) {                        \
      KMP_DO_PAUSE;                                                            \
                                                                               \
      old_value = *(TYPE volatile *)lhs;                                       \
      new_value = old_value OP rhs;                                            \
    }                                                                          \
  }

#if USE_CMPXCHG_FIX
// 2007-06-25:
// workaround for C78287 (complex(kind=4) data type). lin_32, lin_32e, win_32
// and win_32e are affected (I verified the asm). Compiler ignores the volatile
// qualifier of the temp_val in the OP_CMPXCHG macro. This is a problem of the
// compiler. Related tracker is C76005, targeted to 11.0. I verified the asm of
// the workaround.
#define OP_CMPXCHG_WORKAROUND(TYPE, BITS, OP)                                  \
  {                                                                            \
    struct _sss {                                                              \
      TYPE cmp;                                                                \
      kmp_int##BITS *vvv;                                                      \
    };                                                                         \
    struct _sss old_value, new_value;                                          \
    old_value.vvv = (kmp_int##BITS *)&old_value.cmp;                           \
    new_value.vvv = (kmp_int##BITS *)&new_value.cmp;                           \
    *old_value.vvv = *(volatile kmp_int##BITS *)lhs;                           \
    new_value.cmp = old_value.cmp OP rhs;                                      \
    while (!KMP_COMPARE_AND_STORE_ACQ##BITS(                                   \
        (kmp_int##BITS *)lhs, *VOLATILE_CAST(kmp_int##BITS *) old_value.vvv,   \
        *VOLATILE_CAST(kmp_int##BITS *) new_value.vvv)) {                      \
      KMP_DO_PAUSE;                                                            \
                                                                               \
      *old_value.vvv = *(volatile kmp_int##BITS *)lhs;                         \
      new_value.cmp = old_value.cmp OP rhs;                                    \
    }                                                                          \
  }
// end of the first part of the workaround for C78287
#endif // USE_CMPXCHG_FIX

#if KMP_ARCH_X86 || KMP_ARCH_X86_64

// ------------------------------------------------------------------------
// X86 or X86_64: no alignment problems ====================================
#define ATOMIC_FIXED_ADD(TYPE_ID, OP_ID, TYPE, BITS, OP, LCK_ID, MASK,         \
                         GOMP_FLAG)                                            \
  ATOMIC_BEGIN(TYPE_ID, OP_ID, TYPE, void)                                     \
  OP_GOMP_CRITICAL(OP## =, GOMP_FLAG)                                          \
  /* OP used as a sign for subtraction: (lhs-rhs) --> (lhs+-rhs) */            \
  KMP_TEST_THEN_ADD##BITS(lhs, OP rhs);                                        \
  }
// -------------------------------------------------------------------------
#define ATOMIC_CMPXCHG(TYPE_ID, OP_ID, TYPE, BITS, OP, LCK_ID, MASK,           \
                       GOMP_FLAG)                                              \
  ATOMIC_BEGIN(TYPE_ID, OP_ID, TYPE, void)                                     \
  OP_GOMP_CRITICAL(OP## =, GOMP_FLAG)                                          \
  OP_CMPXCHG(TYPE, BITS, OP)                                                   \
  }
#if USE_CMPXCHG_FIX
// -------------------------------------------------------------------------
// workaround for C78287 (complex(kind=4) data type)
#define ATOMIC_CMPXCHG_WORKAROUND(TYPE_ID, OP_ID, TYPE, BITS, OP, LCK_ID,      \
                                  MASK, GOMP_FLAG)                             \
  ATOMIC_BEGIN(TYPE_ID, OP_ID, TYPE, void)                                     \
  OP_GOMP_CRITICAL(OP## =, GOMP_FLAG)                                          \
  OP_CMPXCHG_WORKAROUND(TYPE, BITS, OP)                                        \
  }
// end of the second part of the workaround for C78287
#endif

#else
// -------------------------------------------------------------------------
// Code for other architectures that don't handle unaligned accesses.
#define ATOMIC_FIXED_ADD(TYPE_ID, OP_ID, TYPE, BITS, OP, LCK_ID, MASK,         \
                         GOMP_FLAG)                                            \
  ATOMIC_BEGIN(TYPE_ID, OP_ID, TYPE, void)                                     \
  OP_GOMP_CRITICAL(OP## =, GOMP_FLAG)                                          \
  if (!((kmp_uintptr_t)lhs & 0x##MASK)) {                                      \
    /* OP used as a sign for subtraction: (lhs-rhs) --> (lhs+-rhs) */          \
    KMP_TEST_THEN_ADD##BITS(lhs, OP rhs);                                      \
  } else {                                                                     \
    KMP_CHECK_GTID;                                                            \
    OP_CRITICAL(OP## =, LCK_ID) /* unaligned address - use critical */         \
  }                                                                            \
  }
// -------------------------------------------------------------------------
#define ATOMIC_CMPXCHG(TYPE_ID, OP_ID, TYPE, BITS, OP, LCK_ID, MASK,           \
                       GOMP_FLAG)                                              \
  ATOMIC_BEGIN(TYPE_ID, OP_ID, TYPE, void)                                     \
  OP_GOMP_CRITICAL(OP## =, GOMP_FLAG)                                          \
  if (!((kmp_uintptr_t)lhs & 0x##MASK)) {                                      \
    OP_CMPXCHG(TYPE, BITS, OP) /* aligned address */                           \
  } else {                                                                     \
    KMP_CHECK_GTID;                                                            \
    OP_CRITICAL(OP## =, LCK_ID) /* unaligned address - use critical */         \
  }                                                                            \
  }
#if USE_CMPXCHG_FIX
// -------------------------------------------------------------------------
// workaround for C78287 (complex(kind=4) data type)
#define ATOMIC_CMPXCHG_WORKAROUND(TYPE_ID, OP_ID, TYPE, BITS, OP, LCK_ID,      \
                                  MASK, GOMP_FLAG)                             \
  ATOMIC_BEGIN(TYPE_ID, OP_ID, TYPE, void)                                     \
  OP_GOMP_CRITICAL(OP## =, GOMP_FLAG)                                          \
  if (!((kmp_uintptr_t)lhs & 0x##MASK)) {                                      \
    OP_CMPXCHG(TYPE, BITS, OP) /* aligned address */                           \
  } else {                                                                     \
    KMP_CHECK_GTID;                                                            \
    OP_CRITICAL(OP## =, LCK_ID) /* unaligned address - use critical */         \
  }                                                                            \
  }
// end of the second part of the workaround for C78287
#endif // USE_CMPXCHG_FIX
#endif /* KMP_ARCH_X86 || KMP_ARCH_X86_64 */

// Routines for ATOMIC 4-byte operands addition and subtraction
ATOMIC_FIXED_ADD(fixed4, add, kmp_int32, 32, +, 4i, 3,
                 0) // __kmpc_atomic_fixed4_add
ATOMIC_FIXED_ADD(fixed4, sub, kmp_int32, 32, -, 4i, 3,
                 0) // __kmpc_atomic_fixed4_sub

ATOMIC_CMPXCHG(float4, add, kmp_real32, 32, +, 4r, 3,
               KMP_ARCH_X86) // __kmpc_atomic_float4_add
ATOMIC_CMPXCHG(float4, sub, kmp_real32, 32, -, 4r, 3,
               KMP_ARCH_X86) // __kmpc_atomic_float4_sub

// Routines for ATOMIC 8-byte operands addition and subtraction
ATOMIC_FIXED_ADD(fixed8, add, kmp_int64, 64, +, 8i, 7,
                 KMP_ARCH_X86) // __kmpc_atomic_fixed8_add
ATOMIC_FIXED_ADD(fixed8, sub, kmp_int64, 64, -, 8i, 7,
                 KMP_ARCH_X86) // __kmpc_atomic_fixed8_sub

ATOMIC_CMPXCHG(float8, add, kmp_real64, 64, +, 8r, 7,
               KMP_ARCH_X86) // __kmpc_atomic_float8_add
ATOMIC_CMPXCHG(float8, sub, kmp_real64, 64, -, 8r, 7,
               KMP_ARCH_X86) // __kmpc_atomic_float8_sub

// ------------------------------------------------------------------------
// Entries definition for integer operands
//     TYPE_ID - operands type and size (fixed4, float4)
//     OP_ID   - operation identifier (add, sub, mul, ...)
//     TYPE    - operand type
//     BITS    - size in bits, used to distinguish low level calls
//     OP      - operator (used in critical section)
//     LCK_ID  - lock identifier, used to possibly distinguish lock variable
//     MASK    - used for alignment check

//               TYPE_ID,OP_ID,  TYPE,   BITS,OP,LCK_ID,MASK,GOMP_FLAG
// ------------------------------------------------------------------------
// Routines for ATOMIC integer operands, other operators
// ------------------------------------------------------------------------
//              TYPE_ID,OP_ID, TYPE,          OP, LCK_ID, GOMP_FLAG
ATOMIC_CMPXCHG(fixed1, add, kmp_int8, 8, +, 1i, 0,
               KMP_ARCH_X86) // __kmpc_atomic_fixed1_add
ATOMIC_CMPXCHG(fixed1, andb, kmp_int8, 8, &, 1i, 0,
               0) // __kmpc_atomic_fixed1_andb
ATOMIC_CMPXCHG(fixed1, div, kmp_int8, 8, /, 1i, 0,
               KMP_ARCH_X86) // __kmpc_atomic_fixed1_div
ATOMIC_CMPXCHG(fixed1u, div, kmp_uint8, 8, /, 1i, 0,
               KMP_ARCH_X86) // __kmpc_atomic_fixed1u_div
ATOMIC_CMPXCHG(fixed1, mul, kmp_int8, 8, *, 1i, 0,
               KMP_ARCH_X86) // __kmpc_atomic_fixed1_mul
ATOMIC_CMPXCHG(fixed1, orb, kmp_int8, 8, |, 1i, 0,
               0) // __kmpc_atomic_fixed1_orb
ATOMIC_CMPXCHG(fixed1, shl, kmp_int8, 8, <<, 1i, 0,
               KMP_ARCH_X86) // __kmpc_atomic_fixed1_shl
ATOMIC_CMPXCHG(fixed1, shr, kmp_int8, 8, >>, 1i, 0,
               KMP_ARCH_X86) // __kmpc_atomic_fixed1_shr
ATOMIC_CMPXCHG(fixed1u, shr, kmp_uint8, 8, >>, 1i, 0,
               KMP_ARCH_X86) // __kmpc_atomic_fixed1u_shr
ATOMIC_CMPXCHG(fixed1, sub, kmp_int8, 8, -, 1i, 0,
               KMP_ARCH_X86) // __kmpc_atomic_fixed1_sub
ATOMIC_CMPXCHG(fixed1, xor, kmp_int8, 8, ^, 1i, 0,
               0) // __kmpc_atomic_fixed1_xor
ATOMIC_CMPXCHG(fixed2, add, kmp_int16, 16, +, 2i, 1,
               KMP_ARCH_X86) // __kmpc_atomic_fixed2_add
ATOMIC_CMPXCHG(fixed2, andb, kmp_int16, 16, &, 2i, 1,
               0) // __kmpc_atomic_fixed2_andb
ATOMIC_CMPXCHG(fixed2, div, kmp_int16, 16, /, 2i, 1,
               KMP_ARCH_X86) // __kmpc_atomic_fixed2_div
ATOMIC_CMPXCHG(fixed2u, div, kmp_uint16, 16, /, 2i, 1,
               KMP_ARCH_X86) // __kmpc_atomic_fixed2u_div
ATOMIC_CMPXCHG(fixed2, mul, kmp_int16, 16, *, 2i, 1,
               KMP_ARCH_X86) // __kmpc_atomic_fixed2_mul
ATOMIC_CMPXCHG(fixed2, orb, kmp_int16, 16, |, 2i, 1,
               0) // __kmpc_atomic_fixed2_orb
ATOMIC_CMPXCHG(fixed2, shl, kmp_int16, 16, <<, 2i, 1,
               KMP_ARCH_X86) // __kmpc_atomic_fixed2_shl
ATOMIC_CMPXCHG(fixed2, shr, kmp_int16, 16, >>, 2i, 1,
               KMP_ARCH_X86) // __kmpc_atomic_fixed2_shr
ATOMIC_CMPXCHG(fixed2u, shr, kmp_uint16, 16, >>, 2i, 1,
               KMP_ARCH_X86) // __kmpc_atomic_fixed2u_shr
ATOMIC_CMPXCHG(fixed2, sub, kmp_int16, 16, -, 2i, 1,
               KMP_ARCH_X86) // __kmpc_atomic_fixed2_sub
ATOMIC_CMPXCHG(fixed2, xor, kmp_int16, 16, ^, 2i, 1,
               0) // __kmpc_atomic_fixed2_xor
ATOMIC_CMPXCHG(fixed4, andb, kmp_int32, 32, &, 4i, 3,
               0) // __kmpc_atomic_fixed4_andb
ATOMIC_CMPXCHG(fixed4, div, kmp_int32, 32, /, 4i, 3,
               KMP_ARCH_X86) // __kmpc_atomic_fixed4_div
ATOMIC_CMPXCHG(fixed4u, div, kmp_uint32, 32, /, 4i, 3,
               KMP_ARCH_X86) // __kmpc_atomic_fixed4u_div
ATOMIC_CMPXCHG(fixed4, mul, kmp_int32, 32, *, 4i, 3,
               KMP_ARCH_X86) // __kmpc_atomic_fixed4_mul
ATOMIC_CMPXCHG(fixed4, orb, kmp_int32, 32, |, 4i, 3,
               0) // __kmpc_atomic_fixed4_orb
ATOMIC_CMPXCHG(fixed4, shl, kmp_int32, 32, <<, 4i, 3,
               KMP_ARCH_X86) // __kmpc_atomic_fixed4_shl
ATOMIC_CMPXCHG(fixed4, shr, kmp_int32, 32, >>, 4i, 3,
               KMP_ARCH_X86) // __kmpc_atomic_fixed4_shr
ATOMIC_CMPXCHG(fixed4u, shr, kmp_uint32, 32, >>, 4i, 3,
               KMP_ARCH_X86) // __kmpc_atomic_fixed4u_shr
ATOMIC_CMPXCHG(fixed4, xor, kmp_int32, 32, ^, 4i, 3,
               0) // __kmpc_atomic_fixed4_xor
ATOMIC_CMPXCHG(fixed8, andb, kmp_int64, 64, &, 8i, 7,
               KMP_ARCH_X86) // __kmpc_atomic_fixed8_andb
ATOMIC_CMPXCHG(fixed8, div, kmp_int64, 64, /, 8i, 7,
               KMP_ARCH_X86) // __kmpc_atomic_fixed8_div
ATOMIC_CMPXCHG(fixed8u, div, kmp_uint64, 64, /, 8i, 7,
               KMP_ARCH_X86) // __kmpc_atomic_fixed8u_div
ATOMIC_CMPXCHG(fixed8, mul, kmp_int64, 64, *, 8i, 7,
               KMP_ARCH_X86) // __kmpc_atomic_fixed8_mul
ATOMIC_CMPXCHG(fixed8, orb, kmp_int64, 64, |, 8i, 7,
               KMP_ARCH_X86) // __kmpc_atomic_fixed8_orb
ATOMIC_CMPXCHG(fixed8, shl, kmp_int64, 64, <<, 8i, 7,
               KMP_ARCH_X86) // __kmpc_atomic_fixed8_shl
ATOMIC_CMPXCHG(fixed8, shr, kmp_int64, 64, >>, 8i, 7,
               KMP_ARCH_X86) // __kmpc_atomic_fixed8_shr
ATOMIC_CMPXCHG(fixed8u, shr, kmp_uint64, 64, >>, 8i, 7,
               KMP_ARCH_X86) // __kmpc_atomic_fixed8u_shr
ATOMIC_CMPXCHG(fixed8, xor, kmp_int64, 64, ^, 8i, 7,
               KMP_ARCH_X86) // __kmpc_atomic_fixed8_xor
ATOMIC_CMPXCHG(float4, div, kmp_real32, 32, /, 4r, 3,
               KMP_ARCH_X86) // __kmpc_atomic_float4_div
ATOMIC_CMPXCHG(float4, mul, kmp_real32, 32, *, 4r, 3,
               KMP_ARCH_X86) // __kmpc_atomic_float4_mul
ATOMIC_CMPXCHG(float8, div, kmp_real64, 64, /, 8r, 7,
               KMP_ARCH_X86) // __kmpc_atomic_float8_div
ATOMIC_CMPXCHG(float8, mul, kmp_real64, 64, *, 8r, 7,
               KMP_ARCH_X86) // __kmpc_atomic_float8_mul
//              TYPE_ID,OP_ID, TYPE,          OP, LCK_ID, GOMP_FLAG

/* ------------------------------------------------------------------------ */
/* Routines for C/C++ Reduction operators && and ||                         */

// ------------------------------------------------------------------------
// Need separate macros for &&, || because there is no combined assignment
//   TODO: eliminate ATOMIC_CRIT_{L,EQV} macros as not used
#define ATOMIC_CRIT_L(TYPE_ID, OP_ID, TYPE, OP, LCK_ID, GOMP_FLAG)             \
  ATOMIC_BEGIN(TYPE_ID, OP_ID, TYPE, void)                                     \
  OP_GOMP_CRITICAL(= *lhs OP, GOMP_FLAG)                                       \
  OP_CRITICAL(= *lhs OP, LCK_ID)                                               \
  }

#if KMP_ARCH_X86 || KMP_ARCH_X86_64

// ------------------------------------------------------------------------
// X86 or X86_64: no alignment problems ===================================
#define ATOMIC_CMPX_L(TYPE_ID, OP_ID, TYPE, BITS, OP, LCK_ID, MASK, GOMP_FLAG) \
  ATOMIC_BEGIN(TYPE_ID, OP_ID, TYPE, void)                                     \
  OP_GOMP_CRITICAL(= *lhs OP, GOMP_FLAG)                                       \
  OP_CMPXCHG(TYPE, BITS, OP)                                                   \
  }

#else
// ------------------------------------------------------------------------
// Code for other architectures that don't handle unaligned accesses.
#define ATOMIC_CMPX_L(TYPE_ID, OP_ID, TYPE, BITS, OP, LCK_ID, MASK, GOMP_FLAG) \
  ATOMIC_BEGIN(TYPE_ID, OP_ID, TYPE, void)                                     \
  OP_GOMP_CRITICAL(= *lhs OP, GOMP_FLAG)                                       \
  if (!((kmp_uintptr_t)lhs & 0x##MASK)) {                                      \
    OP_CMPXCHG(TYPE, BITS, OP) /* aligned address */                           \
  } else {                                                                     \
    KMP_CHECK_GTID;                                                            \
    OP_CRITICAL(= *lhs OP, LCK_ID) /* unaligned - use critical */              \
  }                                                                            \
  }
#endif /* KMP_ARCH_X86 || KMP_ARCH_X86_64 */

ATOMIC_CMPX_L(fixed1, andl, char, 8, &&, 1i, 0,
              KMP_ARCH_X86) // __kmpc_atomic_fixed1_andl
ATOMIC_CMPX_L(fixed1, orl, char, 8, ||, 1i, 0,
              KMP_ARCH_X86) // __kmpc_atomic_fixed1_orl
ATOMIC_CMPX_L(fixed2, andl, short, 16, &&, 2i, 1,
              KMP_ARCH_X86) // __kmpc_atomic_fixed2_andl
ATOMIC_CMPX_L(fixed2, orl, short, 16, ||, 2i, 1,
              KMP_ARCH_X86) // __kmpc_atomic_fixed2_orl
ATOMIC_CMPX_L(fixed4, andl, kmp_int32, 32, &&, 4i, 3,
              0) // __kmpc_atomic_fixed4_andl
ATOMIC_CMPX_L(fixed4, orl, kmp_int32, 32, ||, 4i, 3,
              0) // __kmpc_atomic_fixed4_orl
ATOMIC_CMPX_L(fixed8, andl, kmp_int64, 64, &&, 8i, 7,
              KMP_ARCH_X86) // __kmpc_atomic_fixed8_andl
ATOMIC_CMPX_L(fixed8, orl, kmp_int64, 64, ||, 8i, 7,
              KMP_ARCH_X86) // __kmpc_atomic_fixed8_orl

/* ------------------------------------------------------------------------- */
/* Routines for Fortran operators that matched no one in C:                  */
/* MAX, MIN, .EQV., .NEQV.                                                   */
/* Operators .AND., .OR. are covered by __kmpc_atomic_*_{andl,orl}           */
/* Intrinsics IAND, IOR, IEOR are covered by __kmpc_atomic_*_{andb,orb,xor}  */

// -------------------------------------------------------------------------
// MIN and MAX need separate macros
// OP - operator to check if we need any actions?
#define MIN_MAX_CRITSECT(OP, LCK_ID)                                           \
  __kmp_acquire_atomic_lock(&ATOMIC_LOCK##LCK_ID, gtid);                       \
                                                                               \
  if (*lhs OP rhs) { /* still need actions? */                                 \
    *lhs = rhs;                                                                \
  }                                                                            \
  __kmp_release_atomic_lock(&ATOMIC_LOCK##LCK_ID, gtid);

// -------------------------------------------------------------------------
#ifdef KMP_GOMP_COMPAT
#define GOMP_MIN_MAX_CRITSECT(OP, FLAG)                                        \
  if ((FLAG) && (__kmp_atomic_mode == 2)) {                                    \
    KMP_CHECK_GTID;                                                            \
    MIN_MAX_CRITSECT(OP, 0);                                                   \
    return;                                                                    \
  }
#else
#define GOMP_MIN_MAX_CRITSECT(OP, FLAG)
#endif /* KMP_GOMP_COMPAT */

// -------------------------------------------------------------------------
#define MIN_MAX_CMPXCHG(TYPE, BITS, OP)                                        \
  {                                                                            \
    TYPE KMP_ATOMIC_VOLATILE temp_val;                                         \
    TYPE old_value;                                                            \
    temp_val = *lhs;                                                           \
    old_value = temp_val;                                                      \
    while (old_value OP rhs && /* still need actions? */                       \
           !KMP_COMPARE_AND_STORE_ACQ##BITS(                                   \
               (kmp_int##BITS *)lhs,                                           \
               *VOLATILE_CAST(kmp_int##BITS *) & old_value,                    \
               *VOLATILE_CAST(kmp_int##BITS *) & rhs)) {                       \
      KMP_CPU_PAUSE();                                                         \
      temp_val = *lhs;                                                         \
      old_value = temp_val;                                                    \
    }                                                                          \
  }

// -------------------------------------------------------------------------
// 1-byte, 2-byte operands - use critical section
#define MIN_MAX_CRITICAL(TYPE_ID, OP_ID, TYPE, OP, LCK_ID, GOMP_FLAG)          \
  ATOMIC_BEGIN(TYPE_ID, OP_ID, TYPE, void)                                     \
  if (*lhs OP rhs) { /* need actions? */                                       \
    GOMP_MIN_MAX_CRITSECT(OP, GOMP_FLAG)                                       \
    MIN_MAX_CRITSECT(OP, LCK_ID)                                               \
  }                                                                            \
  }

#if KMP_ARCH_X86 || KMP_ARCH_X86_64

// -------------------------------------------------------------------------
// X86 or X86_64: no alignment problems ====================================
#define MIN_MAX_COMPXCHG(TYPE_ID, OP_ID, TYPE, BITS, OP, LCK_ID, MASK,         \
                         GOMP_FLAG)                                            \
  ATOMIC_BEGIN(TYPE_ID, OP_ID, TYPE, void)                                     \
  if (*lhs OP rhs) {                                                           \
    GOMP_MIN_MAX_CRITSECT(OP, GOMP_FLAG)                                       \
    MIN_MAX_CMPXCHG(TYPE, BITS, OP)                                            \
  }                                                                            \
  }

#else
// -------------------------------------------------------------------------
// Code for other architectures that don't handle unaligned accesses.
#define MIN_MAX_COMPXCHG(TYPE_ID, OP_ID, TYPE, BITS, OP, LCK_ID, MASK,         \
                         GOMP_FLAG)                                            \
  ATOMIC_BEGIN(TYPE_ID, OP_ID, TYPE, void)                                     \
  if (*lhs OP rhs) {                                                           \
    GOMP_MIN_MAX_CRITSECT(OP, GOMP_FLAG)                                       \
    if (!((kmp_uintptr_t)lhs & 0x##MASK)) {                                    \
      MIN_MAX_CMPXCHG(TYPE, BITS, OP) /* aligned address */                    \
    } else {                                                                   \
      KMP_CHECK_GTID;                                                          \
      MIN_MAX_CRITSECT(OP, LCK_ID) /* unaligned address */                     \
    }                                                                          \
  }                                                                            \
  }
#endif /* KMP_ARCH_X86 || KMP_ARCH_X86_64 */

MIN_MAX_COMPXCHG(fixed1, max, char, 8, <, 1i, 0,
                 KMP_ARCH_X86) // __kmpc_atomic_fixed1_max
MIN_MAX_COMPXCHG(fixed1, min, char, 8, >, 1i, 0,
                 KMP_ARCH_X86) // __kmpc_atomic_fixed1_min
MIN_MAX_COMPXCHG(fixed2, max, short, 16, <, 2i, 1,
                 KMP_ARCH_X86) // __kmpc_atomic_fixed2_max
MIN_MAX_COMPXCHG(fixed2, min, short, 16, >, 2i, 1,
                 KMP_ARCH_X86) // __kmpc_atomic_fixed2_min
MIN_MAX_COMPXCHG(fixed4, max, kmp_int32, 32, <, 4i, 3,
                 0) // __kmpc_atomic_fixed4_max
MIN_MAX_COMPXCHG(fixed4, min, kmp_int32, 32, >, 4i, 3,
                 0) // __kmpc_atomic_fixed4_min
MIN_MAX_COMPXCHG(fixed8, max, kmp_int64, 64, <, 8i, 7,
                 KMP_ARCH_X86) // __kmpc_atomic_fixed8_max
MIN_MAX_COMPXCHG(fixed8, min, kmp_int64, 64, >, 8i, 7,
                 KMP_ARCH_X86) // __kmpc_atomic_fixed8_min
MIN_MAX_COMPXCHG(float4, max, kmp_real32, 32, <, 4r, 3,
                 KMP_ARCH_X86) // __kmpc_atomic_float4_max
MIN_MAX_COMPXCHG(float4, min, kmp_real32, 32, >, 4r, 3,
                 KMP_ARCH_X86) // __kmpc_atomic_float4_min
MIN_MAX_COMPXCHG(float8, max, kmp_real64, 64, <, 8r, 7,
                 KMP_ARCH_X86) // __kmpc_atomic_float8_max
MIN_MAX_COMPXCHG(float8, min, kmp_real64, 64, >, 8r, 7,
                 KMP_ARCH_X86) // __kmpc_atomic_float8_min
#if KMP_HAVE_QUAD
MIN_MAX_CRITICAL(float16, max, QUAD_LEGACY, <, 16r,
                 1) // __kmpc_atomic_float16_max
MIN_MAX_CRITICAL(float16, min, QUAD_LEGACY, >, 16r,
                 1) // __kmpc_atomic_float16_min
#if (KMP_ARCH_X86)
MIN_MAX_CRITICAL(float16, max_a16, Quad_a16_t, <, 16r,
                 1) // __kmpc_atomic_float16_max_a16
MIN_MAX_CRITICAL(float16, min_a16, Quad_a16_t, >, 16r,
                 1) // __kmpc_atomic_float16_min_a16
#endif
#endif
// ------------------------------------------------------------------------
// Need separate macros for .EQV. because of the need of complement (~)
// OP ignored for critical sections, ^=~ used instead
#define ATOMIC_CRIT_EQV(TYPE_ID, OP_ID, TYPE, OP, LCK_ID, GOMP_FLAG)           \
  ATOMIC_BEGIN(TYPE_ID, OP_ID, TYPE, void)                                     \
  OP_GOMP_CRITICAL(^= ~, GOMP_FLAG) /* send assignment */                      \
  OP_CRITICAL(^= ~, LCK_ID) /* send assignment and complement */               \
  }

// ------------------------------------------------------------------------
#if KMP_ARCH_X86 || KMP_ARCH_X86_64
// ------------------------------------------------------------------------
// X86 or X86_64: no alignment problems ===================================
#define ATOMIC_CMPX_EQV(TYPE_ID, OP_ID, TYPE, BITS, OP, LCK_ID, MASK,          \
                        GOMP_FLAG)                                             \
  ATOMIC_BEGIN(TYPE_ID, OP_ID, TYPE, void)                                     \
  OP_GOMP_CRITICAL(^= ~, GOMP_FLAG) /* send assignment */                      \
  OP_CMPXCHG(TYPE, BITS, OP)                                                   \
  }
// ------------------------------------------------------------------------
#else
// ------------------------------------------------------------------------
// Code for other architectures that don't handle unaligned accesses.
#define ATOMIC_CMPX_EQV(TYPE_ID, OP_ID, TYPE, BITS, OP, LCK_ID, MASK,          \
                        GOMP_FLAG)                                             \
  ATOMIC_BEGIN(TYPE_ID, OP_ID, TYPE, void)                                     \
  OP_GOMP_CRITICAL(^= ~, GOMP_FLAG)                                            \
  if (!((kmp_uintptr_t)lhs & 0x##MASK)) {                                      \
    OP_CMPXCHG(TYPE, BITS, OP) /* aligned address */                           \
  } else {                                                                     \
    KMP_CHECK_GTID;                                                            \
    OP_CRITICAL(^= ~, LCK_ID) /* unaligned address - use critical */           \
  }                                                                            \
  }
#endif /* KMP_ARCH_X86 || KMP_ARCH_X86_64 */

ATOMIC_CMPXCHG(fixed1, neqv, kmp_int8, 8, ^, 1i, 0,
               KMP_ARCH_X86) // __kmpc_atomic_fixed1_neqv
ATOMIC_CMPXCHG(fixed2, neqv, kmp_int16, 16, ^, 2i, 1,
               KMP_ARCH_X86) // __kmpc_atomic_fixed2_neqv
ATOMIC_CMPXCHG(fixed4, neqv, kmp_int32, 32, ^, 4i, 3,
               KMP_ARCH_X86) // __kmpc_atomic_fixed4_neqv
ATOMIC_CMPXCHG(fixed8, neqv, kmp_int64, 64, ^, 8i, 7,
               KMP_ARCH_X86) // __kmpc_atomic_fixed8_neqv
ATOMIC_CMPX_EQV(fixed1, eqv, kmp_int8, 8, ^~, 1i, 0,
                KMP_ARCH_X86) // __kmpc_atomic_fixed1_eqv
ATOMIC_CMPX_EQV(fixed2, eqv, kmp_int16, 16, ^~, 2i, 1,
                KMP_ARCH_X86) // __kmpc_atomic_fixed2_eqv
ATOMIC_CMPX_EQV(fixed4, eqv, kmp_int32, 32, ^~, 4i, 3,
                KMP_ARCH_X86) // __kmpc_atomic_fixed4_eqv
ATOMIC_CMPX_EQV(fixed8, eqv, kmp_int64, 64, ^~, 8i, 7,
                KMP_ARCH_X86) // __kmpc_atomic_fixed8_eqv

// ------------------------------------------------------------------------
// Routines for Extended types: long double, _Quad, complex flavours (use
// critical section)
//     TYPE_ID, OP_ID, TYPE - detailed above
//     OP      - operator
//     LCK_ID  - lock identifier, used to possibly distinguish lock variable
#define ATOMIC_CRITICAL(TYPE_ID, OP_ID, TYPE, OP, LCK_ID, GOMP_FLAG)           \
  ATOMIC_BEGIN(TYPE_ID, OP_ID, TYPE, void)                                     \
  OP_GOMP_CRITICAL(OP## =, GOMP_FLAG) /* send assignment */                    \
  OP_CRITICAL(OP## =, LCK_ID) /* send assignment */                            \
  }

/* ------------------------------------------------------------------------- */
// routines for long double type
ATOMIC_CRITICAL(float10, add, long double, +, 10r,
                1) // __kmpc_atomic_float10_add
ATOMIC_CRITICAL(float10, sub, long double, -, 10r,
                1) // __kmpc_atomic_float10_sub
ATOMIC_CRITICAL(float10, mul, long double, *, 10r,
                1) // __kmpc_atomic_float10_mul
ATOMIC_CRITICAL(float10, div, long double, /, 10r,
                1) // __kmpc_atomic_float10_div
#if KMP_HAVE_QUAD
// routines for _Quad type
ATOMIC_CRITICAL(float16, add, QUAD_LEGACY, +, 16r,
                1) // __kmpc_atomic_float16_add
ATOMIC_CRITICAL(float16, sub, QUAD_LEGACY, -, 16r,
                1) // __kmpc_atomic_float16_sub
ATOMIC_CRITICAL(float16, mul, QUAD_LEGACY, *, 16r,
                1) // __kmpc_atomic_float16_mul
ATOMIC_CRITICAL(float16, div, QUAD_LEGACY, /, 16r,
                1) // __kmpc_atomic_float16_div
#if (KMP_ARCH_X86)
ATOMIC_CRITICAL(float16, add_a16, Quad_a16_t, +, 16r,
                1) // __kmpc_atomic_float16_add_a16
ATOMIC_CRITICAL(float16, sub_a16, Quad_a16_t, -, 16r,
                1) // __kmpc_atomic_float16_sub_a16
ATOMIC_CRITICAL(float16, mul_a16, Quad_a16_t, *, 16r,
                1) // __kmpc_atomic_float16_mul_a16
ATOMIC_CRITICAL(float16, div_a16, Quad_a16_t, /, 16r,
                1) // __kmpc_atomic_float16_div_a16
#endif
#endif
// routines for complex types

#if USE_CMPXCHG_FIX
// workaround for C78287 (complex(kind=4) data type)
ATOMIC_CMPXCHG_WORKAROUND(cmplx4, add, kmp_cmplx32, 64, +, 8c, 7,
                          1) // __kmpc_atomic_cmplx4_add
ATOMIC_CMPXCHG_WORKAROUND(cmplx4, sub, kmp_cmplx32, 64, -, 8c, 7,
                          1) // __kmpc_atomic_cmplx4_sub
ATOMIC_CMPXCHG_WORKAROUND(cmplx4, mul, kmp_cmplx32, 64, *, 8c, 7,
                          1) // __kmpc_atomic_cmplx4_mul
ATOMIC_CMPXCHG_WORKAROUND(cmplx4, div, kmp_cmplx32, 64, /, 8c, 7,
                          1) // __kmpc_atomic_cmplx4_div
// end of the workaround for C78287
#else
ATOMIC_CRITICAL(cmplx4, add, kmp_cmplx32, +, 8c, 1) // __kmpc_atomic_cmplx4_add
ATOMIC_CRITICAL(cmplx4, sub, kmp_cmplx32, -, 8c, 1) // __kmpc_atomic_cmplx4_sub
ATOMIC_CRITICAL(cmplx4, mul, kmp_cmplx32, *, 8c, 1) // __kmpc_atomic_cmplx4_mul
ATOMIC_CRITICAL(cmplx4, div, kmp_cmplx32, /, 8c, 1) // __kmpc_atomic_cmplx4_div
#endif // USE_CMPXCHG_FIX

ATOMIC_CRITICAL(cmplx8, add, kmp_cmplx64, +, 16c, 1) // __kmpc_atomic_cmplx8_add
ATOMIC_CRITICAL(cmplx8, sub, kmp_cmplx64, -, 16c, 1) // __kmpc_atomic_cmplx8_sub
ATOMIC_CRITICAL(cmplx8, mul, kmp_cmplx64, *, 16c, 1) // __kmpc_atomic_cmplx8_mul
ATOMIC_CRITICAL(cmplx8, div, kmp_cmplx64, /, 16c, 1) // __kmpc_atomic_cmplx8_div
ATOMIC_CRITICAL(cmplx10, add, kmp_cmplx80, +, 20c,
                1) // __kmpc_atomic_cmplx10_add
ATOMIC_CRITICAL(cmplx10, sub, kmp_cmplx80, -, 20c,
                1) // __kmpc_atomic_cmplx10_sub
ATOMIC_CRITICAL(cmplx10, mul, kmp_cmplx80, *, 20c,
                1) // __kmpc_atomic_cmplx10_mul
ATOMIC_CRITICAL(cmplx10, div, kmp_cmplx80, /, 20c,
                1) // __kmpc_atomic_cmplx10_div
#if KMP_HAVE_QUAD
ATOMIC_CRITICAL(cmplx16, add, CPLX128_LEG, +, 32c,
                1) // __kmpc_atomic_cmplx16_add
ATOMIC_CRITICAL(cmplx16, sub, CPLX128_LEG, -, 32c,
                1) // __kmpc_atomic_cmplx16_sub
ATOMIC_CRITICAL(cmplx16, mul, CPLX128_LEG, *, 32c,
                1) // __kmpc_atomic_cmplx16_mul
ATOMIC_CRITICAL(cmplx16, div, CPLX128_LEG, /, 32c,
                1) // __kmpc_atomic_cmplx16_div
#if (KMP_ARCH_X86)
ATOMIC_CRITICAL(cmplx16, add_a16, kmp_cmplx128_a16_t, +, 32c,
                1) // __kmpc_atomic_cmplx16_add_a16
ATOMIC_CRITICAL(cmplx16, sub_a16, kmp_cmplx128_a16_t, -, 32c,
                1) // __kmpc_atomic_cmplx16_sub_a16
ATOMIC_CRITICAL(cmplx16, mul_a16, kmp_cmplx128_a16_t, *, 32c,
                1) // __kmpc_atomic_cmplx16_mul_a16
ATOMIC_CRITICAL(cmplx16, div_a16, kmp_cmplx128_a16_t, /, 32c,
                1) // __kmpc_atomic_cmplx16_div_a16
#endif
#endif

#if OMP_40_ENABLED

// OpenMP 4.0: x = expr binop x for non-commutative operations.
// Supported only on IA-32 architecture and Intel(R) 64
#if KMP_ARCH_X86 || KMP_ARCH_X86_64

// ------------------------------------------------------------------------
// Operation on *lhs, rhs bound by critical section
//     OP     - operator (it's supposed to contain an assignment)
//     LCK_ID - lock identifier
// Note: don't check gtid as it should always be valid
// 1, 2-byte - expect valid parameter, other - check before this macro
#define OP_CRITICAL_REV(OP, LCK_ID)                                            \
  __kmp_acquire_atomic_lock(&ATOMIC_LOCK##LCK_ID, gtid);                       \
                                                                               \
  (*lhs) = (rhs)OP(*lhs);                                                      \
                                                                               \
  __kmp_release_atomic_lock(&ATOMIC_LOCK##LCK_ID, gtid);

#ifdef KMP_GOMP_COMPAT
#define OP_GOMP_CRITICAL_REV(OP, FLAG)                                         \
  if ((FLAG) && (__kmp_atomic_mode == 2)) {                                    \
    KMP_CHECK_GTID;                                                            \
    OP_CRITICAL_REV(OP, 0);                                                    \
    return;                                                                    \
  }
#else
#define OP_GOMP_CRITICAL_REV(OP, FLAG)
#endif /* KMP_GOMP_COMPAT */

// Beginning of a definition (provides name, parameters, gebug trace)
//     TYPE_ID - operands type and size (fixed*, fixed*u for signed, unsigned
//     fixed)
//     OP_ID   - operation identifier (add, sub, mul, ...)
//     TYPE    - operands' type
#define ATOMIC_BEGIN_REV(TYPE_ID, OP_ID, TYPE, RET_TYPE)                       \
  RET_TYPE __kmpc_atomic_##TYPE_ID##_##OP_ID##_rev(ident_t *id_ref, int gtid,  \
                                                   TYPE *lhs, TYPE rhs) {      \
    KMP_DEBUG_ASSERT(__kmp_init_serial);                                       \
    KA_TRACE(100, ("__kmpc_atomic_" #TYPE_ID "_" #OP_ID "_rev: T#%d\n", gtid));

// ------------------------------------------------------------------------
// Operation on *lhs, rhs using "compare_and_store" routine
//     TYPE    - operands' type
//     BITS    - size in bits, used to distinguish low level calls
//     OP      - operator
// Note: temp_val introduced in order to force the compiler to read
//       *lhs only once (w/o it the compiler reads *lhs twice)
#define OP_CMPXCHG_REV(TYPE, BITS, OP)                                         \
  {                                                                            \
    TYPE KMP_ATOMIC_VOLATILE temp_val;                                         \
    TYPE old_value, new_value;                                                 \
    temp_val = *lhs;                                                           \
    old_value = temp_val;                                                      \
    new_value = rhs OP old_value;                                              \
    while (!KMP_COMPARE_AND_STORE_ACQ##BITS(                                   \
        (kmp_int##BITS *)lhs, *VOLATILE_CAST(kmp_int##BITS *) & old_value,     \
        *VOLATILE_CAST(kmp_int##BITS *) & new_value)) {                        \
      KMP_DO_PAUSE;                                                            \
                                                                               \
      temp_val = *lhs;                                                         \
      old_value = temp_val;                                                    \
      new_value = rhs OP old_value;                                            \
    }                                                                          \
  }

// -------------------------------------------------------------------------
#define ATOMIC_CMPXCHG_REV(TYPE_ID, OP_ID, TYPE, BITS, OP, LCK_ID, GOMP_FLAG)  \
  ATOMIC_BEGIN_REV(TYPE_ID, OP_ID, TYPE, void)                                 \
  OP_GOMP_CRITICAL_REV(OP, GOMP_FLAG)                                          \
  OP_CMPXCHG_REV(TYPE, BITS, OP)                                               \
  }

// ------------------------------------------------------------------------
// Entries definition for integer operands
//     TYPE_ID - operands type and size (fixed4, float4)
//     OP_ID   - operation identifier (add, sub, mul, ...)
//     TYPE    - operand type
//     BITS    - size in bits, used to distinguish low level calls
//     OP      - operator (used in critical section)
//     LCK_ID  - lock identifier, used to possibly distinguish lock variable

//               TYPE_ID,OP_ID,  TYPE,   BITS,OP,LCK_ID,GOMP_FLAG
// ------------------------------------------------------------------------
// Routines for ATOMIC integer operands, other operators
// ------------------------------------------------------------------------
//                  TYPE_ID,OP_ID, TYPE,    BITS, OP, LCK_ID, GOMP_FLAG
ATOMIC_CMPXCHG_REV(fixed1, div, kmp_int8, 8, /, 1i,
                   KMP_ARCH_X86) // __kmpc_atomic_fixed1_div_rev
ATOMIC_CMPXCHG_REV(fixed1u, div, kmp_uint8, 8, /, 1i,
                   KMP_ARCH_X86) // __kmpc_atomic_fixed1u_div_rev
ATOMIC_CMPXCHG_REV(fixed1, shl, kmp_int8, 8, <<, 1i,
                   KMP_ARCH_X86) // __kmpc_atomic_fixed1_shl_rev
ATOMIC_CMPXCHG_REV(fixed1, shr, kmp_int8, 8, >>, 1i,
                   KMP_ARCH_X86) // __kmpc_atomic_fixed1_shr_rev
ATOMIC_CMPXCHG_REV(fixed1u, shr, kmp_uint8, 8, >>, 1i,
                   KMP_ARCH_X86) // __kmpc_atomic_fixed1u_shr_rev
ATOMIC_CMPXCHG_REV(fixed1, sub, kmp_int8, 8, -, 1i,
                   KMP_ARCH_X86) // __kmpc_atomic_fixed1_sub_rev

ATOMIC_CMPXCHG_REV(fixed2, div, kmp_int16, 16, /, 2i,
                   KMP_ARCH_X86) // __kmpc_atomic_fixed2_div_rev
ATOMIC_CMPXCHG_REV(fixed2u, div, kmp_uint16, 16, /, 2i,
                   KMP_ARCH_X86) // __kmpc_atomic_fixed2u_div_rev
ATOMIC_CMPXCHG_REV(fixed2, shl, kmp_int16, 16, <<, 2i,
                   KMP_ARCH_X86) // __kmpc_atomic_fixed2_shl_rev
ATOMIC_CMPXCHG_REV(fixed2, shr, kmp_int16, 16, >>, 2i,
                   KMP_ARCH_X86) // __kmpc_atomic_fixed2_shr_rev
ATOMIC_CMPXCHG_REV(fixed2u, shr, kmp_uint16, 16, >>, 2i,
                   KMP_ARCH_X86) // __kmpc_atomic_fixed2u_shr_rev
ATOMIC_CMPXCHG_REV(fixed2, sub, kmp_int16, 16, -, 2i,
                   KMP_ARCH_X86) // __kmpc_atomic_fixed2_sub_rev

ATOMIC_CMPXCHG_REV(fixed4, div, kmp_int32, 32, /, 4i,
                   KMP_ARCH_X86) // __kmpc_atomic_fixed4_div_rev
ATOMIC_CMPXCHG_REV(fixed4u, div, kmp_uint32, 32, /, 4i,
                   KMP_ARCH_X86) // __kmpc_atomic_fixed4u_div_rev
ATOMIC_CMPXCHG_REV(fixed4, shl, kmp_int32, 32, <<, 4i,
                   KMP_ARCH_X86) // __kmpc_atomic_fixed4_shl_rev
ATOMIC_CMPXCHG_REV(fixed4, shr, kmp_int32, 32, >>, 4i,
                   KMP_ARCH_X86) // __kmpc_atomic_fixed4_shr_rev
ATOMIC_CMPXCHG_REV(fixed4u, shr, kmp_uint32, 32, >>, 4i,
                   KMP_ARCH_X86) // __kmpc_atomic_fixed4u_shr_rev
ATOMIC_CMPXCHG_REV(fixed4, sub, kmp_int32, 32, -, 4i,
                   KMP_ARCH_X86) // __kmpc_atomic_fixed4_sub_rev

ATOMIC_CMPXCHG_REV(fixed8, div, kmp_int64, 64, /, 8i,
                   KMP_ARCH_X86) // __kmpc_atomic_fixed8_div_rev
ATOMIC_CMPXCHG_REV(fixed8u, div, kmp_uint64, 64, /, 8i,
                   KMP_ARCH_X86) // __kmpc_atomic_fixed8u_div_rev
ATOMIC_CMPXCHG_REV(fixed8, shl, kmp_int64, 64, <<, 8i,
                   KMP_ARCH_X86) // __kmpc_atomic_fixed8_shl_rev
ATOMIC_CMPXCHG_REV(fixed8, shr, kmp_int64, 64, >>, 8i,
                   KMP_ARCH_X86) // __kmpc_atomic_fixed8_shr_rev
ATOMIC_CMPXCHG_REV(fixed8u, shr, kmp_uint64, 64, >>, 8i,
                   KMP_ARCH_X86) // __kmpc_atomic_fixed8u_shr_rev
ATOMIC_CMPXCHG_REV(fixed8, sub, kmp_int64, 64, -, 8i,
                   KMP_ARCH_X86) // __kmpc_atomic_fixed8_sub_rev

ATOMIC_CMPXCHG_REV(float4, div, kmp_real32, 32, /, 4r,
                   KMP_ARCH_X86) // __kmpc_atomic_float4_div_rev
ATOMIC_CMPXCHG_REV(float4, sub, kmp_real32, 32, -, 4r,
                   KMP_ARCH_X86) // __kmpc_atomic_float4_sub_rev

ATOMIC_CMPXCHG_REV(float8, div, kmp_real64, 64, /, 8r,
                   KMP_ARCH_X86) // __kmpc_atomic_float8_div_rev
ATOMIC_CMPXCHG_REV(float8, sub, kmp_real64, 64, -, 8r,
                   KMP_ARCH_X86) // __kmpc_atomic_float8_sub_rev
//                  TYPE_ID,OP_ID, TYPE,     BITS,OP,LCK_ID, GOMP_FLAG

// ------------------------------------------------------------------------
// Routines for Extended types: long double, _Quad, complex flavours (use
// critical section)
//     TYPE_ID, OP_ID, TYPE - detailed above
//     OP      - operator
//     LCK_ID  - lock identifier, used to possibly distinguish lock variable
#define ATOMIC_CRITICAL_REV(TYPE_ID, OP_ID, TYPE, OP, LCK_ID, GOMP_FLAG)       \
  ATOMIC_BEGIN_REV(TYPE_ID, OP_ID, TYPE, void)                                 \
  OP_GOMP_CRITICAL_REV(OP, GOMP_FLAG)                                          \
  OP_CRITICAL_REV(OP, LCK_ID)                                                  \
  }

/* ------------------------------------------------------------------------- */
// routines for long double type
ATOMIC_CRITICAL_REV(float10, sub, long double, -, 10r,
                    1) // __kmpc_atomic_float10_sub_rev
ATOMIC_CRITICAL_REV(float10, div, long double, /, 10r,
                    1) // __kmpc_atomic_float10_div_rev
#if KMP_HAVE_QUAD
// routines for _Quad type
ATOMIC_CRITICAL_REV(float16, sub, QUAD_LEGACY, -, 16r,
                    1) // __kmpc_atomic_float16_sub_rev
ATOMIC_CRITICAL_REV(float16, div, QUAD_LEGACY, /, 16r,
                    1) // __kmpc_atomic_float16_div_rev
#if (KMP_ARCH_X86)
ATOMIC_CRITICAL_REV(float16, sub_a16, Quad_a16_t, -, 16r,
                    1) // __kmpc_atomic_float16_sub_a16_rev
ATOMIC_CRITICAL_REV(float16, div_a16, Quad_a16_t, /, 16r,
                    1) // __kmpc_atomic_float16_div_a16_rev
#endif
#endif

// routines for complex types
ATOMIC_CRITICAL_REV(cmplx4, sub, kmp_cmplx32, -, 8c,
                    1) // __kmpc_atomic_cmplx4_sub_rev
ATOMIC_CRITICAL_REV(cmplx4, div, kmp_cmplx32, /, 8c,
                    1) // __kmpc_atomic_cmplx4_div_rev
ATOMIC_CRITICAL_REV(cmplx8, sub, kmp_cmplx64, -, 16c,
                    1) // __kmpc_atomic_cmplx8_sub_rev
ATOMIC_CRITICAL_REV(cmplx8, div, kmp_cmplx64, /, 16c,
                    1) // __kmpc_atomic_cmplx8_div_rev
ATOMIC_CRITICAL_REV(cmplx10, sub, kmp_cmplx80, -, 20c,
                    1) // __kmpc_atomic_cmplx10_sub_rev
ATOMIC_CRITICAL_REV(cmplx10, div, kmp_cmplx80, /, 20c,
                    1) // __kmpc_atomic_cmplx10_div_rev
#if KMP_HAVE_QUAD
ATOMIC_CRITICAL_REV(cmplx16, sub, CPLX128_LEG, -, 32c,
                    1) // __kmpc_atomic_cmplx16_sub_rev
ATOMIC_CRITICAL_REV(cmplx16, div, CPLX128_LEG, /, 32c,
                    1) // __kmpc_atomic_cmplx16_div_rev
#if (KMP_ARCH_X86)
ATOMIC_CRITICAL_REV(cmplx16, sub_a16, kmp_cmplx128_a16_t, -, 32c,
                    1) // __kmpc_atomic_cmplx16_sub_a16_rev
ATOMIC_CRITICAL_REV(cmplx16, div_a16, kmp_cmplx128_a16_t, /, 32c,
                    1) // __kmpc_atomic_cmplx16_div_a16_rev
#endif
#endif

#endif // KMP_ARCH_X86 || KMP_ARCH_X86_64
// End of OpenMP 4.0: x = expr binop x for non-commutative operations.

#endif // OMP_40_ENABLED

/* ------------------------------------------------------------------------ */
/* Routines for mixed types of LHS and RHS, when RHS is "larger"            */
/* Note: in order to reduce the total number of types combinations          */
/*       it is supposed that compiler converts RHS to longest floating type,*/
/*       that is _Quad, before call to any of these routines                */
/* Conversion to _Quad will be done by the compiler during calculation,     */
/*    conversion back to TYPE - before the assignment, like:                */
/*    *lhs = (TYPE)( (_Quad)(*lhs) OP rhs )                                 */
/* Performance penalty expected because of SW emulation use                 */
/* ------------------------------------------------------------------------ */

#define ATOMIC_BEGIN_MIX(TYPE_ID, TYPE, OP_ID, RTYPE_ID, RTYPE)                \
  void __kmpc_atomic_##TYPE_ID##_##OP_ID##_##RTYPE_ID(                         \
      ident_t *id_ref, int gtid, TYPE *lhs, RTYPE rhs) {                       \
    KMP_DEBUG_ASSERT(__kmp_init_serial);                                       \
    KA_TRACE(100,                                                              \
             ("__kmpc_atomic_" #TYPE_ID "_" #OP_ID "_" #RTYPE_ID ": T#%d\n",   \
              gtid));

// -------------------------------------------------------------------------
#define ATOMIC_CRITICAL_FP(TYPE_ID, TYPE, OP_ID, OP, RTYPE_ID, RTYPE, LCK_ID,  \
                           GOMP_FLAG)                                          \
  ATOMIC_BEGIN_MIX(TYPE_ID, TYPE, OP_ID, RTYPE_ID, RTYPE)                      \
  OP_GOMP_CRITICAL(OP## =, GOMP_FLAG) /* send assignment */                    \
  OP_CRITICAL(OP## =, LCK_ID) /* send assignment */                            \
  }

// -------------------------------------------------------------------------
#if KMP_ARCH_X86 || KMP_ARCH_X86_64
// -------------------------------------------------------------------------
// X86 or X86_64: no alignment problems ====================================
#define ATOMIC_CMPXCHG_MIX(TYPE_ID, TYPE, OP_ID, BITS, OP, RTYPE_ID, RTYPE,    \
                           LCK_ID, MASK, GOMP_FLAG)                            \
  ATOMIC_BEGIN_MIX(TYPE_ID, TYPE, OP_ID, RTYPE_ID, RTYPE)                      \
  OP_GOMP_CRITICAL(OP## =, GOMP_FLAG)                                          \
  OP_CMPXCHG(TYPE, BITS, OP)                                                   \
  }
// -------------------------------------------------------------------------
#else
// ------------------------------------------------------------------------
// Code for other architectures that don't handle unaligned accesses.
#define ATOMIC_CMPXCHG_MIX(TYPE_ID, TYPE, OP_ID, BITS, OP, RTYPE_ID, RTYPE,    \
                           LCK_ID, MASK, GOMP_FLAG)                            \
  ATOMIC_BEGIN_MIX(TYPE_ID, TYPE, OP_ID, RTYPE_ID, RTYPE)                      \
  OP_GOMP_CRITICAL(OP## =, GOMP_FLAG)                                          \
  if (!((kmp_uintptr_t)lhs & 0x##MASK)) {                                      \
    OP_CMPXCHG(TYPE, BITS, OP) /* aligned address */                           \
  } else {                                                                     \
    KMP_CHECK_GTID;                                                            \
    OP_CRITICAL(OP## =, LCK_ID) /* unaligned address - use critical */         \
  }                                                                            \
  }
#endif /* KMP_ARCH_X86 || KMP_ARCH_X86_64 */

// -------------------------------------------------------------------------
#if KMP_ARCH_X86 || KMP_ARCH_X86_64
// -------------------------------------------------------------------------
#define ATOMIC_CMPXCHG_REV_MIX(TYPE_ID, TYPE, OP_ID, BITS, OP, RTYPE_ID,       \
                               RTYPE, LCK_ID, MASK, GOMP_FLAG)                 \
  ATOMIC_BEGIN_MIX(TYPE_ID, TYPE, OP_ID, RTYPE_ID, RTYPE)                      \
  OP_GOMP_CRITICAL_REV(OP, GOMP_FLAG)                                          \
  OP_CMPXCHG_REV(TYPE, BITS, OP)                                               \
  }
#define ATOMIC_CRITICAL_REV_FP(TYPE_ID, TYPE, OP_ID, OP, RTYPE_ID, RTYPE,      \
                               LCK_ID, GOMP_FLAG)                              \
  ATOMIC_BEGIN_MIX(TYPE_ID, TYPE, OP_ID, RTYPE_ID, RTYPE)                      \
  OP_GOMP_CRITICAL_REV(OP, GOMP_FLAG)                                          \
  OP_CRITICAL_REV(OP, LCK_ID)                                                  \
  }
#endif /* KMP_ARCH_X86 || KMP_ARCH_X86_64 */

// RHS=float8
ATOMIC_CMPXCHG_MIX(fixed1, char, mul, 8, *, float8, kmp_real64, 1i, 0,
                   KMP_ARCH_X86) // __kmpc_atomic_fixed1_mul_float8
ATOMIC_CMPXCHG_MIX(fixed1, char, div, 8, /, float8, kmp_real64, 1i, 0,
                   KMP_ARCH_X86) // __kmpc_atomic_fixed1_div_float8
ATOMIC_CMPXCHG_MIX(fixed2, short, mul, 16, *, float8, kmp_real64, 2i, 1,
                   KMP_ARCH_X86) // __kmpc_atomic_fixed2_mul_float8
ATOMIC_CMPXCHG_MIX(fixed2, short, div, 16, /, float8, kmp_real64, 2i, 1,
                   KMP_ARCH_X86) // __kmpc_atomic_fixed2_div_float8
ATOMIC_CMPXCHG_MIX(fixed4, kmp_int32, mul, 32, *, float8, kmp_real64, 4i, 3,
                   0) // __kmpc_atomic_fixed4_mul_float8
ATOMIC_CMPXCHG_MIX(fixed4, kmp_int32, div, 32, /, float8, kmp_real64, 4i, 3,
                   0) // __kmpc_atomic_fixed4_div_float8
ATOMIC_CMPXCHG_MIX(fixed8, kmp_int64, mul, 64, *, float8, kmp_real64, 8i, 7,
                   KMP_ARCH_X86) // __kmpc_atomic_fixed8_mul_float8
ATOMIC_CMPXCHG_MIX(fixed8, kmp_int64, div, 64, /, float8, kmp_real64, 8i, 7,
                   KMP_ARCH_X86) // __kmpc_atomic_fixed8_div_float8
ATOMIC_CMPXCHG_MIX(float4, kmp_real32, add, 32, +, float8, kmp_real64, 4r, 3,
                   KMP_ARCH_X86) // __kmpc_atomic_float4_add_float8
ATOMIC_CMPXCHG_MIX(float4, kmp_real32, sub, 32, -, float8, kmp_real64, 4r, 3,
                   KMP_ARCH_X86) // __kmpc_atomic_float4_sub_float8
ATOMIC_CMPXCHG_MIX(float4, kmp_real32, mul, 32, *, float8, kmp_real64, 4r, 3,
                   KMP_ARCH_X86) // __kmpc_atomic_float4_mul_float8
ATOMIC_CMPXCHG_MIX(float4, kmp_real32, div, 32, /, float8, kmp_real64, 4r, 3,
                   KMP_ARCH_X86) // __kmpc_atomic_float4_div_float8

// RHS=float16 (deprecated, to be removed when we are sure the compiler does not
// use them)
#if KMP_HAVE_QUAD
ATOMIC_CMPXCHG_MIX(fixed1, char, add, 8, +, fp, _Quad, 1i, 0,
                   KMP_ARCH_X86) // __kmpc_atomic_fixed1_add_fp
ATOMIC_CMPXCHG_MIX(fixed1u, uchar, add, 8, +, fp, _Quad, 1i, 0,
                   KMP_ARCH_X86) // __kmpc_atomic_fixed1u_add_fp
ATOMIC_CMPXCHG_MIX(fixed1, char, sub, 8, -, fp, _Quad, 1i, 0,
                   KMP_ARCH_X86) // __kmpc_atomic_fixed1_sub_fp
ATOMIC_CMPXCHG_MIX(fixed1u, uchar, sub, 8, -, fp, _Quad, 1i, 0,
                   KMP_ARCH_X86) // __kmpc_atomic_fixed1u_sub_fp
ATOMIC_CMPXCHG_MIX(fixed1, char, mul, 8, *, fp, _Quad, 1i, 0,
                   KMP_ARCH_X86) // __kmpc_atomic_fixed1_mul_fp
ATOMIC_CMPXCHG_MIX(fixed1u, uchar, mul, 8, *, fp, _Quad, 1i, 0,
                   KMP_ARCH_X86) // __kmpc_atomic_fixed1u_mul_fp
ATOMIC_CMPXCHG_MIX(fixed1, char, div, 8, /, fp, _Quad, 1i, 0,
                   KMP_ARCH_X86) // __kmpc_atomic_fixed1_div_fp
ATOMIC_CMPXCHG_MIX(fixed1u, uchar, div, 8, /, fp, _Quad, 1i, 0,
                   KMP_ARCH_X86) // __kmpc_atomic_fixed1u_div_fp

ATOMIC_CMPXCHG_MIX(fixed2, short, add, 16, +, fp, _Quad, 2i, 1,
                   KMP_ARCH_X86) // __kmpc_atomic_fixed2_add_fp
ATOMIC_CMPXCHG_MIX(fixed2u, ushort, add, 16, +, fp, _Quad, 2i, 1,
                   KMP_ARCH_X86) // __kmpc_atomic_fixed2u_add_fp
ATOMIC_CMPXCHG_MIX(fixed2, short, sub, 16, -, fp, _Quad, 2i, 1,
                   KMP_ARCH_X86) // __kmpc_atomic_fixed2_sub_fp
ATOMIC_CMPXCHG_MIX(fixed2u, ushort, sub, 16, -, fp, _Quad, 2i, 1,
                   KMP_ARCH_X86) // __kmpc_atomic_fixed2u_sub_fp
ATOMIC_CMPXCHG_MIX(fixed2, short, mul, 16, *, fp, _Quad, 2i, 1,
                   KMP_ARCH_X86) // __kmpc_atomic_fixed2_mul_fp
ATOMIC_CMPXCHG_MIX(fixed2u, ushort, mul, 16, *, fp, _Quad, 2i, 1,
                   KMP_ARCH_X86) // __kmpc_atomic_fixed2u_mul_fp
ATOMIC_CMPXCHG_MIX(fixed2, short, div, 16, /, fp, _Quad, 2i, 1,
                   KMP_ARCH_X86) // __kmpc_atomic_fixed2_div_fp
ATOMIC_CMPXCHG_MIX(fixed2u, ushort, div, 16, /, fp, _Quad, 2i, 1,
                   KMP_ARCH_X86) // __kmpc_atomic_fixed2u_div_fp

ATOMIC_CMPXCHG_MIX(fixed4, kmp_int32, add, 32, +, fp, _Quad, 4i, 3,
                   0) // __kmpc_atomic_fixed4_add_fp
ATOMIC_CMPXCHG_MIX(fixed4u, kmp_uint32, add, 32, +, fp, _Quad, 4i, 3,
                   0) // __kmpc_atomic_fixed4u_add_fp
ATOMIC_CMPXCHG_MIX(fixed4, kmp_int32, sub, 32, -, fp, _Quad, 4i, 3,
                   0) // __kmpc_atomic_fixed4_sub_fp
ATOMIC_CMPXCHG_MIX(fixed4u, kmp_uint32, sub, 32, -, fp, _Quad, 4i, 3,
                   0) // __kmpc_atomic_fixed4u_sub_fp
ATOMIC_CMPXCHG_MIX(fixed4, kmp_int32, mul, 32, *, fp, _Quad, 4i, 3,
                   0) // __kmpc_atomic_fixed4_mul_fp
ATOMIC_CMPXCHG_MIX(fixed4u, kmp_uint32, mul, 32, *, fp, _Quad, 4i, 3,
                   0) // __kmpc_atomic_fixed4u_mul_fp
ATOMIC_CMPXCHG_MIX(fixed4, kmp_int32, div, 32, /, fp, _Quad, 4i, 3,
                   0) // __kmpc_atomic_fixed4_div_fp
ATOMIC_CMPXCHG_MIX(fixed4u, kmp_uint32, div, 32, /, fp, _Quad, 4i, 3,
                   0) // __kmpc_atomic_fixed4u_div_fp

ATOMIC_CMPXCHG_MIX(fixed8, kmp_int64, add, 64, +, fp, _Quad, 8i, 7,
                   KMP_ARCH_X86) // __kmpc_atomic_fixed8_add_fp
ATOMIC_CMPXCHG_MIX(fixed8u, kmp_uint64, add, 64, +, fp, _Quad, 8i, 7,
                   KMP_ARCH_X86) // __kmpc_atomic_fixed8u_add_fp
ATOMIC_CMPXCHG_MIX(fixed8, kmp_int64, sub, 64, -, fp, _Quad, 8i, 7,
                   KMP_ARCH_X86) // __kmpc_atomic_fixed8_sub_fp
ATOMIC_CMPXCHG_MIX(fixed8u, kmp_uint64, sub, 64, -, fp, _Quad, 8i, 7,
                   KMP_ARCH_X86) // __kmpc_atomic_fixed8u_sub_fp
ATOMIC_CMPXCHG_MIX(fixed8, kmp_int64, mul, 64, *, fp, _Quad, 8i, 7,
                   KMP_ARCH_X86) // __kmpc_atomic_fixed8_mul_fp
ATOMIC_CMPXCHG_MIX(fixed8u, kmp_uint64, mul, 64, *, fp, _Quad, 8i, 7,
                   KMP_ARCH_X86) // __kmpc_atomic_fixed8u_mul_fp
ATOMIC_CMPXCHG_MIX(fixed8, kmp_int64, div, 64, /, fp, _Quad, 8i, 7,
                   KMP_ARCH_X86) // __kmpc_atomic_fixed8_div_fp
ATOMIC_CMPXCHG_MIX(fixed8u, kmp_uint64, div, 64, /, fp, _Quad, 8i, 7,
                   KMP_ARCH_X86) // __kmpc_atomic_fixed8u_div_fp

ATOMIC_CMPXCHG_MIX(float4, kmp_real32, add, 32, +, fp, _Quad, 4r, 3,
                   KMP_ARCH_X86) // __kmpc_atomic_float4_add_fp
ATOMIC_CMPXCHG_MIX(float4, kmp_real32, sub, 32, -, fp, _Quad, 4r, 3,
                   KMP_ARCH_X86) // __kmpc_atomic_float4_sub_fp
ATOMIC_CMPXCHG_MIX(float4, kmp_real32, mul, 32, *, fp, _Quad, 4r, 3,
                   KMP_ARCH_X86) // __kmpc_atomic_float4_mul_fp
ATOMIC_CMPXCHG_MIX(float4, kmp_real32, div, 32, /, fp, _Quad, 4r, 3,
                   KMP_ARCH_X86) // __kmpc_atomic_float4_div_fp

ATOMIC_CMPXCHG_MIX(float8, kmp_real64, add, 64, +, fp, _Quad, 8r, 7,
                   KMP_ARCH_X86) // __kmpc_atomic_float8_add_fp
ATOMIC_CMPXCHG_MIX(float8, kmp_real64, sub, 64, -, fp, _Quad, 8r, 7,
                   KMP_ARCH_X86) // __kmpc_atomic_float8_sub_fp
ATOMIC_CMPXCHG_MIX(float8, kmp_real64, mul, 64, *, fp, _Quad, 8r, 7,
                   KMP_ARCH_X86) // __kmpc_atomic_float8_mul_fp
ATOMIC_CMPXCHG_MIX(float8, kmp_real64, div, 64, /, fp, _Quad, 8r, 7,
                   KMP_ARCH_X86) // __kmpc_atomic_float8_div_fp

ATOMIC_CRITICAL_FP(float10, long double, add, +, fp, _Quad, 10r,
                   1) // __kmpc_atomic_float10_add_fp
ATOMIC_CRITICAL_FP(float10, long double, sub, -, fp, _Quad, 10r,
                   1) // __kmpc_atomic_float10_sub_fp
ATOMIC_CRITICAL_FP(float10, long double, mul, *, fp, _Quad, 10r,
                   1) // __kmpc_atomic_float10_mul_fp
ATOMIC_CRITICAL_FP(float10, long double, div, /, fp, _Quad, 10r,
                   1) // __kmpc_atomic_float10_div_fp

#if KMP_ARCH_X86 || KMP_ARCH_X86_64
// Reverse operations
ATOMIC_CMPXCHG_REV_MIX(fixed1, char, sub_rev, 8, -, fp, _Quad, 1i, 0,
                       KMP_ARCH_X86) // __kmpc_atomic_fixed1_sub_rev_fp
ATOMIC_CMPXCHG_REV_MIX(fixed1u, uchar, sub_rev, 8, -, fp, _Quad, 1i, 0,
                       KMP_ARCH_X86) // __kmpc_atomic_fixed1u_sub_rev_fp
ATOMIC_CMPXCHG_REV_MIX(fixed1, char, div_rev, 8, /, fp, _Quad, 1i, 0,
                       KMP_ARCH_X86) // __kmpc_atomic_fixed1_div_rev_fp
ATOMIC_CMPXCHG_REV_MIX(fixed1u, uchar, div_rev, 8, /, fp, _Quad, 1i, 0,
                       KMP_ARCH_X86) // __kmpc_atomic_fixed1u_div_rev_fp

ATOMIC_CMPXCHG_REV_MIX(fixed2, short, sub_rev, 16, -, fp, _Quad, 2i, 1,
                       KMP_ARCH_X86) // __kmpc_atomic_fixed2_sub_rev_fp
ATOMIC_CMPXCHG_REV_MIX(fixed2u, ushort, sub_rev, 16, -, fp, _Quad, 2i, 1,
                       KMP_ARCH_X86) // __kmpc_atomic_fixed2u_sub_rev_fp
ATOMIC_CMPXCHG_REV_MIX(fixed2, short, div_rev, 16, /, fp, _Quad, 2i, 1,
                       KMP_ARCH_X86) // __kmpc_atomic_fixed2_div_rev_fp
ATOMIC_CMPXCHG_REV_MIX(fixed2u, ushort, div_rev, 16, /, fp, _Quad, 2i, 1,
                       KMP_ARCH_X86) // __kmpc_atomic_fixed2u_div_rev_fp

ATOMIC_CMPXCHG_REV_MIX(fixed4, kmp_int32, sub_rev, 32, -, fp, _Quad, 4i, 3,
                       0) // __kmpc_atomic_fixed4_sub_rev_fp
ATOMIC_CMPXCHG_REV_MIX(fixed4u, kmp_uint32, sub_rev, 32, -, fp, _Quad, 4i, 3,
                       0) // __kmpc_atomic_fixed4u_sub_rev_fp
ATOMIC_CMPXCHG_REV_MIX(fixed4, kmp_int32, div_rev, 32, /, fp, _Quad, 4i, 3,
                       0) // __kmpc_atomic_fixed4_div_rev_fp
ATOMIC_CMPXCHG_REV_MIX(fixed4u, kmp_uint32, div_rev, 32, /, fp, _Quad, 4i, 3,
                       0) // __kmpc_atomic_fixed4u_div_rev_fp

ATOMIC_CMPXCHG_REV_MIX(fixed8, kmp_int64, sub_rev, 64, -, fp, _Quad, 8i, 7,
                       KMP_ARCH_X86) // __kmpc_atomic_fixed8_sub_rev_fp
ATOMIC_CMPXCHG_REV_MIX(fixed8u, kmp_uint64, sub_rev, 64, -, fp, _Quad, 8i, 7,
                       KMP_ARCH_X86) // __kmpc_atomic_fixed8u_sub_rev_fp
ATOMIC_CMPXCHG_REV_MIX(fixed8, kmp_int64, div_rev, 64, /, fp, _Quad, 8i, 7,
                       KMP_ARCH_X86) // __kmpc_atomic_fixed8_div_rev_fp
ATOMIC_CMPXCHG_REV_MIX(fixed8u, kmp_uint64, div_rev, 64, /, fp, _Quad, 8i, 7,
                       KMP_ARCH_X86) // __kmpc_atomic_fixed8u_div_rev_fp

ATOMIC_CMPXCHG_REV_MIX(float4, kmp_real32, sub_rev, 32, -, fp, _Quad, 4r, 3,
                       KMP_ARCH_X86) // __kmpc_atomic_float4_sub_rev_fp
ATOMIC_CMPXCHG_REV_MIX(float4, kmp_real32, div_rev, 32, /, fp, _Quad, 4r, 3,
                       KMP_ARCH_X86) // __kmpc_atomic_float4_div_rev_fp

ATOMIC_CMPXCHG_REV_MIX(float8, kmp_real64, sub_rev, 64, -, fp, _Quad, 8r, 7,
                       KMP_ARCH_X86) // __kmpc_atomic_float8_sub_rev_fp
ATOMIC_CMPXCHG_REV_MIX(float8, kmp_real64, div_rev, 64, /, fp, _Quad, 8r, 7,
                       KMP_ARCH_X86) // __kmpc_atomic_float8_div_rev_fp

ATOMIC_CRITICAL_REV_FP(float10, long double, sub_rev, -, fp, _Quad, 10r,
                       1) // __kmpc_atomic_float10_sub_rev_fp
ATOMIC_CRITICAL_REV_FP(float10, long double, div_rev, /, fp, _Quad, 10r,
                       1) // __kmpc_atomic_float10_div_rev_fp
#endif /* KMP_ARCH_X86 || KMP_ARCH_X86_64 */

#endif

#if KMP_ARCH_X86 || KMP_ARCH_X86_64
// ------------------------------------------------------------------------
// X86 or X86_64: no alignment problems ====================================
#if USE_CMPXCHG_FIX
// workaround for C78287 (complex(kind=4) data type)
#define ATOMIC_CMPXCHG_CMPLX(TYPE_ID, TYPE, OP_ID, BITS, OP, RTYPE_ID, RTYPE,  \
                             LCK_ID, MASK, GOMP_FLAG)                          \
  ATOMIC_BEGIN_MIX(TYPE_ID, TYPE, OP_ID, RTYPE_ID, RTYPE)                      \
  OP_GOMP_CRITICAL(OP## =, GOMP_FLAG)                                          \
  OP_CMPXCHG_WORKAROUND(TYPE, BITS, OP)                                        \
  }
// end of the second part of the workaround for C78287
#else
#define ATOMIC_CMPXCHG_CMPLX(TYPE_ID, TYPE, OP_ID, BITS, OP, RTYPE_ID, RTYPE,  \
                             LCK_ID, MASK, GOMP_FLAG)                          \
  ATOMIC_BEGIN_MIX(TYPE_ID, TYPE, OP_ID, RTYPE_ID, RTYPE)                      \
  OP_GOMP_CRITICAL(OP## =, GOMP_FLAG)                                          \
  OP_CMPXCHG(TYPE, BITS, OP)                                                   \
  }
#endif // USE_CMPXCHG_FIX
#else
// ------------------------------------------------------------------------
// Code for other architectures that don't handle unaligned accesses.
#define ATOMIC_CMPXCHG_CMPLX(TYPE_ID, TYPE, OP_ID, BITS, OP, RTYPE_ID, RTYPE,  \
                             LCK_ID, MASK, GOMP_FLAG)                          \
  ATOMIC_BEGIN_MIX(TYPE_ID, TYPE, OP_ID, RTYPE_ID, RTYPE)                      \
  OP_GOMP_CRITICAL(OP## =, GOMP_FLAG)                                          \
  if (!((kmp_uintptr_t)lhs & 0x##MASK)) {                                      \
    OP_CMPXCHG(TYPE, BITS, OP) /* aligned address */                           \
  } else {                                                                     \
    KMP_CHECK_GTID;                                                            \
    OP_CRITICAL(OP## =, LCK_ID) /* unaligned address - use critical */         \
  }                                                                            \
  }
#endif /* KMP_ARCH_X86 || KMP_ARCH_X86_64 */

ATOMIC_CMPXCHG_CMPLX(cmplx4, kmp_cmplx32, add, 64, +, cmplx8, kmp_cmplx64, 8c,
                     7, KMP_ARCH_X86) // __kmpc_atomic_cmplx4_add_cmplx8
ATOMIC_CMPXCHG_CMPLX(cmplx4, kmp_cmplx32, sub, 64, -, cmplx8, kmp_cmplx64, 8c,
                     7, KMP_ARCH_X86) // __kmpc_atomic_cmplx4_sub_cmplx8
ATOMIC_CMPXCHG_CMPLX(cmplx4, kmp_cmplx32, mul, 64, *, cmplx8, kmp_cmplx64, 8c,
                     7, KMP_ARCH_X86) // __kmpc_atomic_cmplx4_mul_cmplx8
ATOMIC_CMPXCHG_CMPLX(cmplx4, kmp_cmplx32, div, 64, /, cmplx8, kmp_cmplx64, 8c,
                     7, KMP_ARCH_X86) // __kmpc_atomic_cmplx4_div_cmplx8

// READ, WRITE, CAPTURE are supported only on IA-32 architecture and Intel(R) 64
#if KMP_ARCH_X86 || KMP_ARCH_X86_64

// ------------------------------------------------------------------------
// Atomic READ routines

// ------------------------------------------------------------------------
// Beginning of a definition (provides name, parameters, gebug trace)
//     TYPE_ID - operands type and size (fixed*, fixed*u for signed, unsigned
//     fixed)
//     OP_ID   - operation identifier (add, sub, mul, ...)
//     TYPE    - operands' type
#define ATOMIC_BEGIN_READ(TYPE_ID, OP_ID, TYPE, RET_TYPE)                      \
  RET_TYPE __kmpc_atomic_##TYPE_ID##_##OP_ID(ident_t *id_ref, int gtid,        \
                                             TYPE *loc) {                      \
    KMP_DEBUG_ASSERT(__kmp_init_serial);                                       \
    KA_TRACE(100, ("__kmpc_atomic_" #TYPE_ID "_" #OP_ID ": T#%d\n", gtid));

// ------------------------------------------------------------------------
// Operation on *lhs, rhs using "compare_and_store_ret" routine
//     TYPE    - operands' type
//     BITS    - size in bits, used to distinguish low level calls
//     OP      - operator
// Note: temp_val introduced in order to force the compiler to read
//       *lhs only once (w/o it the compiler reads *lhs twice)
// TODO: check if it is still necessary
// Return old value regardless of the result of "compare & swap# operation
#define OP_CMPXCHG_READ(TYPE, BITS, OP)                                        \
  {                                                                            \
    TYPE KMP_ATOMIC_VOLATILE temp_val;                                         \
    union f_i_union {                                                          \
      TYPE f_val;                                                              \
      kmp_int##BITS i_val;                                                     \
    };                                                                         \
    union f_i_union old_value;                                                 \
    temp_val = *loc;                                                           \
    old_value.f_val = temp_val;                                                \
    old_value.i_val = KMP_COMPARE_AND_STORE_RET##BITS(                         \
        (kmp_int##BITS *)loc,                                                  \
        *VOLATILE_CAST(kmp_int##BITS *) & old_value.i_val,                     \
        *VOLATILE_CAST(kmp_int##BITS *) & old_value.i_val);                    \
    new_value = old_value.f_val;                                               \
    return new_value;                                                          \
  }

// -------------------------------------------------------------------------
// Operation on *lhs, rhs bound by critical section
//     OP     - operator (it's supposed to contain an assignment)
//     LCK_ID - lock identifier
// Note: don't check gtid as it should always be valid
// 1, 2-byte - expect valid parameter, other - check before this macro
#define OP_CRITICAL_READ(OP, LCK_ID)                                           \
  __kmp_acquire_atomic_lock(&ATOMIC_LOCK##LCK_ID, gtid);                       \
                                                                               \
  new_value = (*loc);                                                          \
                                                                               \
  __kmp_release_atomic_lock(&ATOMIC_LOCK##LCK_ID, gtid);

// -------------------------------------------------------------------------
#ifdef KMP_GOMP_COMPAT
#define OP_GOMP_CRITICAL_READ(OP, FLAG)                                        \
  if ((FLAG) && (__kmp_atomic_mode == 2)) {                                    \
    KMP_CHECK_GTID;                                                            \
    OP_CRITICAL_READ(OP, 0);                                                   \
    return new_value;                                                          \
  }
#else
#define OP_GOMP_CRITICAL_READ(OP, FLAG)
#endif /* KMP_GOMP_COMPAT */

// -------------------------------------------------------------------------
#define ATOMIC_FIXED_READ(TYPE_ID, OP_ID, TYPE, BITS, OP, GOMP_FLAG)           \
  ATOMIC_BEGIN_READ(TYPE_ID, OP_ID, TYPE, TYPE)                                \
  TYPE new_value;                                                              \
  OP_GOMP_CRITICAL_READ(OP## =, GOMP_FLAG)                                     \
  new_value = KMP_TEST_THEN_ADD##BITS(loc, OP 0);                              \
  return new_value;                                                            \
  }
// -------------------------------------------------------------------------
#define ATOMIC_CMPXCHG_READ(TYPE_ID, OP_ID, TYPE, BITS, OP, GOMP_FLAG)         \
  ATOMIC_BEGIN_READ(TYPE_ID, OP_ID, TYPE, TYPE)                                \
  TYPE new_value;                                                              \
  OP_GOMP_CRITICAL_READ(OP## =, GOMP_FLAG)                                     \
  OP_CMPXCHG_READ(TYPE, BITS, OP)                                              \
  }
// ------------------------------------------------------------------------
// Routines for Extended types: long double, _Quad, complex flavours (use
// critical section)
//     TYPE_ID, OP_ID, TYPE - detailed above
//     OP      - operator
//     LCK_ID  - lock identifier, used to possibly distinguish lock variable
#define ATOMIC_CRITICAL_READ(TYPE_ID, OP_ID, TYPE, OP, LCK_ID, GOMP_FLAG)      \
  ATOMIC_BEGIN_READ(TYPE_ID, OP_ID, TYPE, TYPE)                                \
  TYPE new_value;                                                              \
  OP_GOMP_CRITICAL_READ(OP## =, GOMP_FLAG) /* send assignment */               \
  OP_CRITICAL_READ(OP, LCK_ID) /* send assignment */                           \
  return new_value;                                                            \
  }

// ------------------------------------------------------------------------
// Fix for cmplx4 read (CQ220361) on Windows* OS. Regular routine with return
// value doesn't work.
// Let's return the read value through the additional parameter.
#if (KMP_OS_WINDOWS)

#define OP_CRITICAL_READ_WRK(OP, LCK_ID)                                       \
  __kmp_acquire_atomic_lock(&ATOMIC_LOCK##LCK_ID, gtid);                       \
                                                                               \
  (*out) = (*loc);                                                             \
                                                                               \
  __kmp_release_atomic_lock(&ATOMIC_LOCK##LCK_ID, gtid);
// ------------------------------------------------------------------------
#ifdef KMP_GOMP_COMPAT
#define OP_GOMP_CRITICAL_READ_WRK(OP, FLAG)                                    \
  if ((FLAG) && (__kmp_atomic_mode == 2)) {                                    \
    KMP_CHECK_GTID;                                                            \
    OP_CRITICAL_READ_WRK(OP, 0);                                               \
  }
#else
#define OP_GOMP_CRITICAL_READ_WRK(OP, FLAG)
#endif /* KMP_GOMP_COMPAT */
// ------------------------------------------------------------------------
#define ATOMIC_BEGIN_READ_WRK(TYPE_ID, OP_ID, TYPE)                            \
  void __kmpc_atomic_##TYPE_ID##_##OP_ID(TYPE *out, ident_t *id_ref, int gtid, \
                                         TYPE *loc) {                          \
    KMP_DEBUG_ASSERT(__kmp_init_serial);                                       \
    KA_TRACE(100, ("__kmpc_atomic_" #TYPE_ID "_" #OP_ID ": T#%d\n", gtid));

// ------------------------------------------------------------------------
#define ATOMIC_CRITICAL_READ_WRK(TYPE_ID, OP_ID, TYPE, OP, LCK_ID, GOMP_FLAG)  \
  ATOMIC_BEGIN_READ_WRK(TYPE_ID, OP_ID, TYPE)                                  \
  OP_GOMP_CRITICAL_READ_WRK(OP## =, GOMP_FLAG) /* send assignment */           \
  OP_CRITICAL_READ_WRK(OP, LCK_ID) /* send assignment */                       \
  }

#endif // KMP_OS_WINDOWS

// ------------------------------------------------------------------------
//                  TYPE_ID,OP_ID, TYPE,      OP, GOMP_FLAG
ATOMIC_FIXED_READ(fixed4, rd, kmp_int32, 32, +, 0) // __kmpc_atomic_fixed4_rd
ATOMIC_FIXED_READ(fixed8, rd, kmp_int64, 64, +,
                  KMP_ARCH_X86) // __kmpc_atomic_fixed8_rd
ATOMIC_CMPXCHG_READ(float4, rd, kmp_real32, 32, +,
                    KMP_ARCH_X86) // __kmpc_atomic_float4_rd
ATOMIC_CMPXCHG_READ(float8, rd, kmp_real64, 64, +,
                    KMP_ARCH_X86) // __kmpc_atomic_float8_rd

// !!! TODO: Remove lock operations for "char" since it can't be non-atomic
ATOMIC_CMPXCHG_READ(fixed1, rd, kmp_int8, 8, +,
                    KMP_ARCH_X86) // __kmpc_atomic_fixed1_rd
ATOMIC_CMPXCHG_READ(fixed2, rd, kmp_int16, 16, +,
                    KMP_ARCH_X86) // __kmpc_atomic_fixed2_rd

ATOMIC_CRITICAL_READ(float10, rd, long double, +, 10r,
                     1) // __kmpc_atomic_float10_rd
#if KMP_HAVE_QUAD
ATOMIC_CRITICAL_READ(float16, rd, QUAD_LEGACY, +, 16r,
                     1) // __kmpc_atomic_float16_rd
#endif // KMP_HAVE_QUAD

// Fix for CQ220361 on Windows* OS
#if (KMP_OS_WINDOWS)
ATOMIC_CRITICAL_READ_WRK(cmplx4, rd, kmp_cmplx32, +, 8c,
                         1) // __kmpc_atomic_cmplx4_rd
#else
ATOMIC_CRITICAL_READ(cmplx4, rd, kmp_cmplx32, +, 8c,
                     1) // __kmpc_atomic_cmplx4_rd
#endif
ATOMIC_CRITICAL_READ(cmplx8, rd, kmp_cmplx64, +, 16c,
                     1) // __kmpc_atomic_cmplx8_rd
ATOMIC_CRITICAL_READ(cmplx10, rd, kmp_cmplx80, +, 20c,
                     1) // __kmpc_atomic_cmplx10_rd
#if KMP_HAVE_QUAD
ATOMIC_CRITICAL_READ(cmplx16, rd, CPLX128_LEG, +, 32c,
                     1) // __kmpc_atomic_cmplx16_rd
#if (KMP_ARCH_X86)
ATOMIC_CRITICAL_READ(float16, a16_rd, Quad_a16_t, +, 16r,
                     1) // __kmpc_atomic_float16_a16_rd
ATOMIC_CRITICAL_READ(cmplx16, a16_rd, kmp_cmplx128_a16_t, +, 32c,
                     1) // __kmpc_atomic_cmplx16_a16_rd
#endif
#endif

// ------------------------------------------------------------------------
// Atomic WRITE routines

#define ATOMIC_XCHG_WR(TYPE_ID, OP_ID, TYPE, BITS, OP, GOMP_FLAG)              \
  ATOMIC_BEGIN(TYPE_ID, OP_ID, TYPE, void)                                     \
  OP_GOMP_CRITICAL(OP, GOMP_FLAG)                                              \
  KMP_XCHG_FIXED##BITS(lhs, rhs);                                              \
  }
// ------------------------------------------------------------------------
#define ATOMIC_XCHG_FLOAT_WR(TYPE_ID, OP_ID, TYPE, BITS, OP, GOMP_FLAG)        \
  ATOMIC_BEGIN(TYPE_ID, OP_ID, TYPE, void)                                     \
  OP_GOMP_CRITICAL(OP, GOMP_FLAG)                                              \
  KMP_XCHG_REAL##BITS(lhs, rhs);                                               \
  }

// ------------------------------------------------------------------------
// Operation on *lhs, rhs using "compare_and_store" routine
//     TYPE    - operands' type
//     BITS    - size in bits, used to distinguish low level calls
//     OP      - operator
// Note: temp_val introduced in order to force the compiler to read
//       *lhs only once (w/o it the compiler reads *lhs twice)
#define OP_CMPXCHG_WR(TYPE, BITS, OP)                                          \
  {                                                                            \
    TYPE KMP_ATOMIC_VOLATILE temp_val;                                         \
    TYPE old_value, new_value;                                                 \
    temp_val = *lhs;                                                           \
    old_value = temp_val;                                                      \
    new_value = rhs;                                                           \
    while (!KMP_COMPARE_AND_STORE_ACQ##BITS(                                   \
        (kmp_int##BITS *)lhs, *VOLATILE_CAST(kmp_int##BITS *) & old_value,     \
        *VOLATILE_CAST(kmp_int##BITS *) & new_value)) {                        \
      KMP_CPU_PAUSE();                                                         \
                                                                               \
      temp_val = *lhs;                                                         \
      old_value = temp_val;                                                    \
      new_value = rhs;                                                         \
    }                                                                          \
  }

// -------------------------------------------------------------------------
#define ATOMIC_CMPXCHG_WR(TYPE_ID, OP_ID, TYPE, BITS, OP, GOMP_FLAG)           \
  ATOMIC_BEGIN(TYPE_ID, OP_ID, TYPE, void)                                     \
  OP_GOMP_CRITICAL(OP, GOMP_FLAG)                                              \
  OP_CMPXCHG_WR(TYPE, BITS, OP)                                                \
  }

// ------------------------------------------------------------------------
// Routines for Extended types: long double, _Quad, complex flavours (use
// critical section)
//     TYPE_ID, OP_ID, TYPE - detailed above
//     OP      - operator
//     LCK_ID  - lock identifier, used to possibly distinguish lock variable
#define ATOMIC_CRITICAL_WR(TYPE_ID, OP_ID, TYPE, OP, LCK_ID, GOMP_FLAG)        \
  ATOMIC_BEGIN(TYPE_ID, OP_ID, TYPE, void)                                     \
  OP_GOMP_CRITICAL(OP, GOMP_FLAG) /* send assignment */                        \
  OP_CRITICAL(OP, LCK_ID) /* send assignment */                                \
  }
// -------------------------------------------------------------------------

ATOMIC_XCHG_WR(fixed1, wr, kmp_int8, 8, =,
               KMP_ARCH_X86) // __kmpc_atomic_fixed1_wr
ATOMIC_XCHG_WR(fixed2, wr, kmp_int16, 16, =,
               KMP_ARCH_X86) // __kmpc_atomic_fixed2_wr
ATOMIC_XCHG_WR(fixed4, wr, kmp_int32, 32, =,
               KMP_ARCH_X86) // __kmpc_atomic_fixed4_wr
#if (KMP_ARCH_X86)
ATOMIC_CMPXCHG_WR(fixed8, wr, kmp_int64, 64, =,
                  KMP_ARCH_X86) // __kmpc_atomic_fixed8_wr
#else
ATOMIC_XCHG_WR(fixed8, wr, kmp_int64, 64, =,
               KMP_ARCH_X86) // __kmpc_atomic_fixed8_wr
#endif

ATOMIC_XCHG_FLOAT_WR(float4, wr, kmp_real32, 32, =,
                     KMP_ARCH_X86) // __kmpc_atomic_float4_wr
#if (KMP_ARCH_X86)
ATOMIC_CMPXCHG_WR(float8, wr, kmp_real64, 64, =,
                  KMP_ARCH_X86) // __kmpc_atomic_float8_wr
#else
ATOMIC_XCHG_FLOAT_WR(float8, wr, kmp_real64, 64, =,
                     KMP_ARCH_X86) // __kmpc_atomic_float8_wr
#endif

ATOMIC_CRITICAL_WR(float10, wr, long double, =, 10r,
                   1) // __kmpc_atomic_float10_wr
#if KMP_HAVE_QUAD
ATOMIC_CRITICAL_WR(float16, wr, QUAD_LEGACY, =, 16r,
                   1) // __kmpc_atomic_float16_wr
#endif
ATOMIC_CRITICAL_WR(cmplx4, wr, kmp_cmplx32, =, 8c, 1) // __kmpc_atomic_cmplx4_wr
ATOMIC_CRITICAL_WR(cmplx8, wr, kmp_cmplx64, =, 16c,
                   1) // __kmpc_atomic_cmplx8_wr
ATOMIC_CRITICAL_WR(cmplx10, wr, kmp_cmplx80, =, 20c,
                   1) // __kmpc_atomic_cmplx10_wr
#if KMP_HAVE_QUAD
ATOMIC_CRITICAL_WR(cmplx16, wr, CPLX128_LEG, =, 32c,
                   1) // __kmpc_atomic_cmplx16_wr
#if (KMP_ARCH_X86)
ATOMIC_CRITICAL_WR(float16, a16_wr, Quad_a16_t, =, 16r,
                   1) // __kmpc_atomic_float16_a16_wr
ATOMIC_CRITICAL_WR(cmplx16, a16_wr, kmp_cmplx128_a16_t, =, 32c,
                   1) // __kmpc_atomic_cmplx16_a16_wr
#endif
#endif

// ------------------------------------------------------------------------
// Atomic CAPTURE routines

// Beginning of a definition (provides name, parameters, gebug trace)
//     TYPE_ID - operands type and size (fixed*, fixed*u for signed, unsigned
//     fixed)
//     OP_ID   - operation identifier (add, sub, mul, ...)
//     TYPE    - operands' type
#define ATOMIC_BEGIN_CPT(TYPE_ID, OP_ID, TYPE, RET_TYPE)                       \
  RET_TYPE __kmpc_atomic_##TYPE_ID##_##OP_ID(ident_t *id_ref, int gtid,        \
                                             TYPE *lhs, TYPE rhs, int flag) {  \
    KMP_DEBUG_ASSERT(__kmp_init_serial);                                       \
    KA_TRACE(100, ("__kmpc_atomic_" #TYPE_ID "_" #OP_ID ": T#%d\n", gtid));

// -------------------------------------------------------------------------
// Operation on *lhs, rhs bound by critical section
//     OP     - operator (it's supposed to contain an assignment)
//     LCK_ID - lock identifier
// Note: don't check gtid as it should always be valid
// 1, 2-byte - expect valid parameter, other - check before this macro
#define OP_CRITICAL_CPT(OP, LCK_ID)                                            \
  __kmp_acquire_atomic_lock(&ATOMIC_LOCK##LCK_ID, gtid);                       \
                                                                               \
  if (flag) {                                                                  \
    (*lhs) OP rhs;                                                             \
    new_value = (*lhs);                                                        \
  } else {                                                                     \
    new_value = (*lhs);                                                        \
    (*lhs) OP rhs;                                                             \
  }                                                                            \
                                                                               \
  __kmp_release_atomic_lock(&ATOMIC_LOCK##LCK_ID, gtid);                       \
  return new_value;

// ------------------------------------------------------------------------
#ifdef KMP_GOMP_COMPAT
#define OP_GOMP_CRITICAL_CPT(OP, FLAG)                                         \
  if ((FLAG) && (__kmp_atomic_mode == 2)) {                                    \
    KMP_CHECK_GTID;                                                            \
    OP_CRITICAL_CPT(OP## =, 0);                                                \
  }
#else
#define OP_GOMP_CRITICAL_CPT(OP, FLAG)
#endif /* KMP_GOMP_COMPAT */

// ------------------------------------------------------------------------
// Operation on *lhs, rhs using "compare_and_store" routine
//     TYPE    - operands' type
//     BITS    - size in bits, used to distinguish low level calls
//     OP      - operator
// Note: temp_val introduced in order to force the compiler to read
//       *lhs only once (w/o it the compiler reads *lhs twice)
#define OP_CMPXCHG_CPT(TYPE, BITS, OP)                                         \
  {                                                                            \
    TYPE KMP_ATOMIC_VOLATILE temp_val;                                         \
    TYPE old_value, new_value;                                                 \
    temp_val = *lhs;                                                           \
    old_value = temp_val;                                                      \
    new_value = old_value OP rhs;                                              \
    while (!KMP_COMPARE_AND_STORE_ACQ##BITS(                                   \
        (kmp_int##BITS *)lhs, *VOLATILE_CAST(kmp_int##BITS *) & old_value,     \
        *VOLATILE_CAST(kmp_int##BITS *) & new_value)) {                        \
      KMP_CPU_PAUSE();                                                         \
                                                                               \
      temp_val = *lhs;                                                         \
      old_value = temp_val;                                                    \
      new_value = old_value OP rhs;                                            \
    }                                                                          \
    if (flag) {                                                                \
      return new_value;                                                        \
    } else                                                                     \
      return old_value;                                                        \
  }

// -------------------------------------------------------------------------
#define ATOMIC_CMPXCHG_CPT(TYPE_ID, OP_ID, TYPE, BITS, OP, GOMP_FLAG)          \
  ATOMIC_BEGIN_CPT(TYPE_ID, OP_ID, TYPE, TYPE)                                 \
  TYPE new_value;                                                              \
  OP_GOMP_CRITICAL_CPT(OP, GOMP_FLAG)                                          \
  OP_CMPXCHG_CPT(TYPE, BITS, OP)                                               \
  }

// -------------------------------------------------------------------------
#define ATOMIC_FIXED_ADD_CPT(TYPE_ID, OP_ID, TYPE, BITS, OP, GOMP_FLAG)        \
  ATOMIC_BEGIN_CPT(TYPE_ID, OP_ID, TYPE, TYPE)                                 \
  TYPE old_value, new_value;                                                   \
  OP_GOMP_CRITICAL_CPT(OP, GOMP_FLAG)                                          \
  /* OP used as a sign for subtraction: (lhs-rhs) --> (lhs+-rhs) */            \
  old_value = KMP_TEST_THEN_ADD##BITS(lhs, OP rhs);                            \
  if (flag) {                                                                  \
    return old_value OP rhs;                                                   \
  } else                                                                       \
    return old_value;                                                          \
  }
// -------------------------------------------------------------------------

ATOMIC_FIXED_ADD_CPT(fixed4, add_cpt, kmp_int32, 32, +,
                     0) // __kmpc_atomic_fixed4_add_cpt
ATOMIC_FIXED_ADD_CPT(fixed4, sub_cpt, kmp_int32, 32, -,
                     0) // __kmpc_atomic_fixed4_sub_cpt
ATOMIC_FIXED_ADD_CPT(fixed8, add_cpt, kmp_int64, 64, +,
                     KMP_ARCH_X86) // __kmpc_atomic_fixed8_add_cpt
ATOMIC_FIXED_ADD_CPT(fixed8, sub_cpt, kmp_int64, 64, -,
                     KMP_ARCH_X86) // __kmpc_atomic_fixed8_sub_cpt

ATOMIC_CMPXCHG_CPT(float4, add_cpt, kmp_real32, 32, +,
                   KMP_ARCH_X86) // __kmpc_atomic_float4_add_cpt
ATOMIC_CMPXCHG_CPT(float4, sub_cpt, kmp_real32, 32, -,
                   KMP_ARCH_X86) // __kmpc_atomic_float4_sub_cpt
ATOMIC_CMPXCHG_CPT(float8, add_cpt, kmp_real64, 64, +,
                   KMP_ARCH_X86) // __kmpc_atomic_float8_add_cpt
ATOMIC_CMPXCHG_CPT(float8, sub_cpt, kmp_real64, 64, -,
                   KMP_ARCH_X86) // __kmpc_atomic_float8_sub_cpt

// ------------------------------------------------------------------------
// Entries definition for integer operands
//     TYPE_ID - operands type and size (fixed4, float4)
//     OP_ID   - operation identifier (add, sub, mul, ...)
//     TYPE    - operand type
//     BITS    - size in bits, used to distinguish low level calls
//     OP      - operator (used in critical section)
//               TYPE_ID,OP_ID,  TYPE,   BITS,OP,GOMP_FLAG
// ------------------------------------------------------------------------
// Routines for ATOMIC integer operands, other operators
// ------------------------------------------------------------------------
//              TYPE_ID,OP_ID, TYPE,          OP,  GOMP_FLAG
ATOMIC_CMPXCHG_CPT(fixed1, add_cpt, kmp_int8, 8, +,
                   KMP_ARCH_X86) // __kmpc_atomic_fixed1_add_cpt
ATOMIC_CMPXCHG_CPT(fixed1, andb_cpt, kmp_int8, 8, &,
                   0) // __kmpc_atomic_fixed1_andb_cpt
ATOMIC_CMPXCHG_CPT(fixed1, div_cpt, kmp_int8, 8, /,
                   KMP_ARCH_X86) // __kmpc_atomic_fixed1_div_cpt
ATOMIC_CMPXCHG_CPT(fixed1u, div_cpt, kmp_uint8, 8, /,
                   KMP_ARCH_X86) // __kmpc_atomic_fixed1u_div_cpt
ATOMIC_CMPXCHG_CPT(fixed1, mul_cpt, kmp_int8, 8, *,
                   KMP_ARCH_X86) // __kmpc_atomic_fixed1_mul_cpt
ATOMIC_CMPXCHG_CPT(fixed1, orb_cpt, kmp_int8, 8, |,
                   0) // __kmpc_atomic_fixed1_orb_cpt
ATOMIC_CMPXCHG_CPT(fixed1, shl_cpt, kmp_int8, 8, <<,
                   KMP_ARCH_X86) // __kmpc_atomic_fixed1_shl_cpt
ATOMIC_CMPXCHG_CPT(fixed1, shr_cpt, kmp_int8, 8, >>,
                   KMP_ARCH_X86) // __kmpc_atomic_fixed1_shr_cpt
ATOMIC_CMPXCHG_CPT(fixed1u, shr_cpt, kmp_uint8, 8, >>,
                   KMP_ARCH_X86) // __kmpc_atomic_fixed1u_shr_cpt
ATOMIC_CMPXCHG_CPT(fixed1, sub_cpt, kmp_int8, 8, -,
                   KMP_ARCH_X86) // __kmpc_atomic_fixed1_sub_cpt
ATOMIC_CMPXCHG_CPT(fixed1, xor_cpt, kmp_int8, 8, ^,
                   0) // __kmpc_atomic_fixed1_xor_cpt
ATOMIC_CMPXCHG_CPT(fixed2, add_cpt, kmp_int16, 16, +,
                   KMP_ARCH_X86) // __kmpc_atomic_fixed2_add_cpt
ATOMIC_CMPXCHG_CPT(fixed2, andb_cpt, kmp_int16, 16, &,
                   0) // __kmpc_atomic_fixed2_andb_cpt
ATOMIC_CMPXCHG_CPT(fixed2, div_cpt, kmp_int16, 16, /,
                   KMP_ARCH_X86) // __kmpc_atomic_fixed2_div_cpt
ATOMIC_CMPXCHG_CPT(fixed2u, div_cpt, kmp_uint16, 16, /,
                   KMP_ARCH_X86) // __kmpc_atomic_fixed2u_div_cpt
ATOMIC_CMPXCHG_CPT(fixed2, mul_cpt, kmp_int16, 16, *,
                   KMP_ARCH_X86) // __kmpc_atomic_fixed2_mul_cpt
ATOMIC_CMPXCHG_CPT(fixed2, orb_cpt, kmp_int16, 16, |,
                   0) // __kmpc_atomic_fixed2_orb_cpt
ATOMIC_CMPXCHG_CPT(fixed2, shl_cpt, kmp_int16, 16, <<,
                   KMP_ARCH_X86) // __kmpc_atomic_fixed2_shl_cpt
ATOMIC_CMPXCHG_CPT(fixed2, shr_cpt, kmp_int16, 16, >>,
                   KMP_ARCH_X86) // __kmpc_atomic_fixed2_shr_cpt
ATOMIC_CMPXCHG_CPT(fixed2u, shr_cpt, kmp_uint16, 16, >>,
                   KMP_ARCH_X86) // __kmpc_atomic_fixed2u_shr_cpt
ATOMIC_CMPXCHG_CPT(fixed2, sub_cpt, kmp_int16, 16, -,
                   KMP_ARCH_X86) // __kmpc_atomic_fixed2_sub_cpt
ATOMIC_CMPXCHG_CPT(fixed2, xor_cpt, kmp_int16, 16, ^,
                   0) // __kmpc_atomic_fixed2_xor_cpt
ATOMIC_CMPXCHG_CPT(fixed4, andb_cpt, kmp_int32, 32, &,
                   0) // __kmpc_atomic_fixed4_andb_cpt
ATOMIC_CMPXCHG_CPT(fixed4, div_cpt, kmp_int32, 32, /,
                   KMP_ARCH_X86) // __kmpc_atomic_fixed4_div_cpt
ATOMIC_CMPXCHG_CPT(fixed4u, div_cpt, kmp_uint32, 32, /,
                   KMP_ARCH_X86) // __kmpc_atomic_fixed4u_div_cpt
ATOMIC_CMPXCHG_CPT(fixed4, mul_cpt, kmp_int32, 32, *,
                   KMP_ARCH_X86) // __kmpc_atomic_fixed4_mul_cpt
ATOMIC_CMPXCHG_CPT(fixed4, orb_cpt, kmp_int32, 32, |,
                   0) // __kmpc_atomic_fixed4_orb_cpt
ATOMIC_CMPXCHG_CPT(fixed4, shl_cpt, kmp_int32, 32, <<,
                   KMP_ARCH_X86) // __kmpc_atomic_fixed4_shl_cpt
ATOMIC_CMPXCHG_CPT(fixed4, shr_cpt, kmp_int32, 32, >>,
                   KMP_ARCH_X86) // __kmpc_atomic_fixed4_shr_cpt
ATOMIC_CMPXCHG_CPT(fixed4u, shr_cpt, kmp_uint32, 32, >>,
                   KMP_ARCH_X86) // __kmpc_atomic_fixed4u_shr_cpt
ATOMIC_CMPXCHG_CPT(fixed4, xor_cpt, kmp_int32, 32, ^,
                   0) // __kmpc_atomic_fixed4_xor_cpt
ATOMIC_CMPXCHG_CPT(fixed8, andb_cpt, kmp_int64, 64, &,
                   KMP_ARCH_X86) // __kmpc_atomic_fixed8_andb_cpt
ATOMIC_CMPXCHG_CPT(fixed8, div_cpt, kmp_int64, 64, /,
                   KMP_ARCH_X86) // __kmpc_atomic_fixed8_div_cpt
ATOMIC_CMPXCHG_CPT(fixed8u, div_cpt, kmp_uint64, 64, /,
                   KMP_ARCH_X86) // __kmpc_atomic_fixed8u_div_cpt
ATOMIC_CMPXCHG_CPT(fixed8, mul_cpt, kmp_int64, 64, *,
                   KMP_ARCH_X86) // __kmpc_atomic_fixed8_mul_cpt
ATOMIC_CMPXCHG_CPT(fixed8, orb_cpt, kmp_int64, 64, |,
                   KMP_ARCH_X86) // __kmpc_atomic_fixed8_orb_cpt
ATOMIC_CMPXCHG_CPT(fixed8, shl_cpt, kmp_int64, 64, <<,
                   KMP_ARCH_X86) // __kmpc_atomic_fixed8_shl_cpt
ATOMIC_CMPXCHG_CPT(fixed8, shr_cpt, kmp_int64, 64, >>,
                   KMP_ARCH_X86) // __kmpc_atomic_fixed8_shr_cpt
ATOMIC_CMPXCHG_CPT(fixed8u, shr_cpt, kmp_uint64, 64, >>,
                   KMP_ARCH_X86) // __kmpc_atomic_fixed8u_shr_cpt
ATOMIC_CMPXCHG_CPT(fixed8, xor_cpt, kmp_int64, 64, ^,
                   KMP_ARCH_X86) // __kmpc_atomic_fixed8_xor_cpt
ATOMIC_CMPXCHG_CPT(float4, div_cpt, kmp_real32, 32, /,
                   KMP_ARCH_X86) // __kmpc_atomic_float4_div_cpt
ATOMIC_CMPXCHG_CPT(float4, mul_cpt, kmp_real32, 32, *,
                   KMP_ARCH_X86) // __kmpc_atomic_float4_mul_cpt
ATOMIC_CMPXCHG_CPT(float8, div_cpt, kmp_real64, 64, /,
                   KMP_ARCH_X86) // __kmpc_atomic_float8_div_cpt
ATOMIC_CMPXCHG_CPT(float8, mul_cpt, kmp_real64, 64, *,
                   KMP_ARCH_X86) // __kmpc_atomic_float8_mul_cpt
//              TYPE_ID,OP_ID, TYPE,          OP,  GOMP_FLAG

// CAPTURE routines for mixed types RHS=float16
#if KMP_HAVE_QUAD

// Beginning of a definition (provides name, parameters, gebug trace)
//     TYPE_ID - operands type and size (fixed*, fixed*u for signed, unsigned
//     fixed)
//     OP_ID   - operation identifier (add, sub, mul, ...)
//     TYPE    - operands' type
#define ATOMIC_BEGIN_CPT_MIX(TYPE_ID, OP_ID, TYPE, RTYPE_ID, RTYPE)            \
  TYPE __kmpc_atomic_##TYPE_ID##_##OP_ID##_##RTYPE_ID(                         \
      ident_t *id_ref, int gtid, TYPE *lhs, RTYPE rhs, int flag) {             \
    KMP_DEBUG_ASSERT(__kmp_init_serial);                                       \
    KA_TRACE(100,                                                              \
             ("__kmpc_atomic_" #TYPE_ID "_" #OP_ID "_" #RTYPE_ID ": T#%d\n",   \
              gtid));

// -------------------------------------------------------------------------
#define ATOMIC_CMPXCHG_CPT_MIX(TYPE_ID, TYPE, OP_ID, BITS, OP, RTYPE_ID,       \
                               RTYPE, LCK_ID, MASK, GOMP_FLAG)                 \
  ATOMIC_BEGIN_CPT_MIX(TYPE_ID, OP_ID, TYPE, RTYPE_ID, RTYPE)                  \
  TYPE new_value;                                                              \
  OP_GOMP_CRITICAL_CPT(OP, GOMP_FLAG)                                          \
  OP_CMPXCHG_CPT(TYPE, BITS, OP)                                               \
  }

// -------------------------------------------------------------------------
#define ATOMIC_CRITICAL_CPT_MIX(TYPE_ID, TYPE, OP_ID, OP, RTYPE_ID, RTYPE,     \
                                LCK_ID, GOMP_FLAG)                             \
  ATOMIC_BEGIN_CPT_MIX(TYPE_ID, OP_ID, TYPE, RTYPE_ID, RTYPE)                  \
  TYPE new_value;                                                              \
  OP_GOMP_CRITICAL_CPT(OP, GOMP_FLAG) /* send assignment */                    \
  OP_CRITICAL_CPT(OP## =, LCK_ID) /* send assignment */                        \
  }

ATOMIC_CMPXCHG_CPT_MIX(fixed1, char, add_cpt, 8, +, fp, _Quad, 1i, 0,
                       KMP_ARCH_X86) // __kmpc_atomic_fixed1_add_cpt_fp
ATOMIC_CMPXCHG_CPT_MIX(fixed1u, uchar, add_cpt, 8, +, fp, _Quad, 1i, 0,
                       KMP_ARCH_X86) // __kmpc_atomic_fixed1u_add_cpt_fp
ATOMIC_CMPXCHG_CPT_MIX(fixed1, char, sub_cpt, 8, -, fp, _Quad, 1i, 0,
                       KMP_ARCH_X86) // __kmpc_atomic_fixed1_sub_cpt_fp
ATOMIC_CMPXCHG_CPT_MIX(fixed1u, uchar, sub_cpt, 8, -, fp, _Quad, 1i, 0,
                       KMP_ARCH_X86) // __kmpc_atomic_fixed1u_sub_cpt_fp
ATOMIC_CMPXCHG_CPT_MIX(fixed1, char, mul_cpt, 8, *, fp, _Quad, 1i, 0,
                       KMP_ARCH_X86) // __kmpc_atomic_fixed1_mul_cpt_fp
ATOMIC_CMPXCHG_CPT_MIX(fixed1u, uchar, mul_cpt, 8, *, fp, _Quad, 1i, 0,
                       KMP_ARCH_X86) // __kmpc_atomic_fixed1u_mul_cpt_fp
ATOMIC_CMPXCHG_CPT_MIX(fixed1, char, div_cpt, 8, /, fp, _Quad, 1i, 0,
                       KMP_ARCH_X86) // __kmpc_atomic_fixed1_div_cpt_fp
ATOMIC_CMPXCHG_CPT_MIX(fixed1u, uchar, div_cpt, 8, /, fp, _Quad, 1i, 0,
                       KMP_ARCH_X86) // __kmpc_atomic_fixed1u_div_cpt_fp

ATOMIC_CMPXCHG_CPT_MIX(fixed2, short, add_cpt, 16, +, fp, _Quad, 2i, 1,
                       KMP_ARCH_X86) // __kmpc_atomic_fixed2_add_cpt_fp
ATOMIC_CMPXCHG_CPT_MIX(fixed2u, ushort, add_cpt, 16, +, fp, _Quad, 2i, 1,
                       KMP_ARCH_X86) // __kmpc_atomic_fixed2u_add_cpt_fp
ATOMIC_CMPXCHG_CPT_MIX(fixed2, short, sub_cpt, 16, -, fp, _Quad, 2i, 1,
                       KMP_ARCH_X86) // __kmpc_atomic_fixed2_sub_cpt_fp
ATOMIC_CMPXCHG_CPT_MIX(fixed2u, ushort, sub_cpt, 16, -, fp, _Quad, 2i, 1,
                       KMP_ARCH_X86) // __kmpc_atomic_fixed2u_sub_cpt_fp
ATOMIC_CMPXCHG_CPT_MIX(fixed2, short, mul_cpt, 16, *, fp, _Quad, 2i, 1,
                       KMP_ARCH_X86) // __kmpc_atomic_fixed2_mul_cpt_fp
ATOMIC_CMPXCHG_CPT_MIX(fixed2u, ushort, mul_cpt, 16, *, fp, _Quad, 2i, 1,
                       KMP_ARCH_X86) // __kmpc_atomic_fixed2u_mul_cpt_fp
ATOMIC_CMPXCHG_CPT_MIX(fixed2, short, div_cpt, 16, /, fp, _Quad, 2i, 1,
                       KMP_ARCH_X86) // __kmpc_atomic_fixed2_div_cpt_fp
ATOMIC_CMPXCHG_CPT_MIX(fixed2u, ushort, div_cpt, 16, /, fp, _Quad, 2i, 1,
                       KMP_ARCH_X86) // __kmpc_atomic_fixed2u_div_cpt_fp

ATOMIC_CMPXCHG_CPT_MIX(fixed4, kmp_int32, add_cpt, 32, +, fp, _Quad, 4i, 3,
                       0) // __kmpc_atomic_fixed4_add_cpt_fp
ATOMIC_CMPXCHG_CPT_MIX(fixed4u, kmp_uint32, add_cpt, 32, +, fp, _Quad, 4i, 3,
                       0) // __kmpc_atomic_fixed4u_add_cpt_fp
ATOMIC_CMPXCHG_CPT_MIX(fixed4, kmp_int32, sub_cpt, 32, -, fp, _Quad, 4i, 3,
                       0) // __kmpc_atomic_fixed4_sub_cpt_fp
ATOMIC_CMPXCHG_CPT_MIX(fixed4u, kmp_uint32, sub_cpt, 32, -, fp, _Quad, 4i, 3,
                       0) // __kmpc_atomic_fixed4u_sub_cpt_fp
ATOMIC_CMPXCHG_CPT_MIX(fixed4, kmp_int32, mul_cpt, 32, *, fp, _Quad, 4i, 3,
                       0) // __kmpc_atomic_fixed4_mul_cpt_fp
ATOMIC_CMPXCHG_CPT_MIX(fixed4u, kmp_uint32, mul_cpt, 32, *, fp, _Quad, 4i, 3,
                       0) // __kmpc_atomic_fixed4u_mul_cpt_fp
ATOMIC_CMPXCHG_CPT_MIX(fixed4, kmp_int32, div_cpt, 32, /, fp, _Quad, 4i, 3,
                       0) // __kmpc_atomic_fixed4_div_cpt_fp
ATOMIC_CMPXCHG_CPT_MIX(fixed4u, kmp_uint32, div_cpt, 32, /, fp, _Quad, 4i, 3,
                       0) // __kmpc_atomic_fixed4u_div_cpt_fp

ATOMIC_CMPXCHG_CPT_MIX(fixed8, kmp_int64, add_cpt, 64, +, fp, _Quad, 8i, 7,
                       KMP_ARCH_X86) // __kmpc_atomic_fixed8_add_cpt_fp
ATOMIC_CMPXCHG_CPT_MIX(fixed8u, kmp_uint64, add_cpt, 64, +, fp, _Quad, 8i, 7,
                       KMP_ARCH_X86) // __kmpc_atomic_fixed8u_add_cpt_fp
ATOMIC_CMPXCHG_CPT_MIX(fixed8, kmp_int64, sub_cpt, 64, -, fp, _Quad, 8i, 7,
                       KMP_ARCH_X86) // __kmpc_atomic_fixed8_sub_cpt_fp
ATOMIC_CMPXCHG_CPT_MIX(fixed8u, kmp_uint64, sub_cpt, 64, -, fp, _Quad, 8i, 7,
                       KMP_ARCH_X86) // __kmpc_atomic_fixed8u_sub_cpt_fp
ATOMIC_CMPXCHG_CPT_MIX(fixed8, kmp_int64, mul_cpt, 64, *, fp, _Quad, 8i, 7,
                       KMP_ARCH_X86) // __kmpc_atomic_fixed8_mul_cpt_fp
ATOMIC_CMPXCHG_CPT_MIX(fixed8u, kmp_uint64, mul_cpt, 64, *, fp, _Quad, 8i, 7,
                       KMP_ARCH_X86) // __kmpc_atomic_fixed8u_mul_cpt_fp
ATOMIC_CMPXCHG_CPT_MIX(fixed8, kmp_int64, div_cpt, 64, /, fp, _Quad, 8i, 7,
                       KMP_ARCH_X86) // __kmpc_atomic_fixed8_div_cpt_fp
ATOMIC_CMPXCHG_CPT_MIX(fixed8u, kmp_uint64, div_cpt, 64, /, fp, _Quad, 8i, 7,
                       KMP_ARCH_X86) // __kmpc_atomic_fixed8u_div_cpt_fp

ATOMIC_CMPXCHG_CPT_MIX(float4, kmp_real32, add_cpt, 32, +, fp, _Quad, 4r, 3,
                       KMP_ARCH_X86) // __kmpc_atomic_float4_add_cpt_fp
ATOMIC_CMPXCHG_CPT_MIX(float4, kmp_real32, sub_cpt, 32, -, fp, _Quad, 4r, 3,
                       KMP_ARCH_X86) // __kmpc_atomic_float4_sub_cpt_fp
ATOMIC_CMPXCHG_CPT_MIX(float4, kmp_real32, mul_cpt, 32, *, fp, _Quad, 4r, 3,
                       KMP_ARCH_X86) // __kmpc_atomic_float4_mul_cpt_fp
ATOMIC_CMPXCHG_CPT_MIX(float4, kmp_real32, div_cpt, 32, /, fp, _Quad, 4r, 3,
                       KMP_ARCH_X86) // __kmpc_atomic_float4_div_cpt_fp

ATOMIC_CMPXCHG_CPT_MIX(float8, kmp_real64, add_cpt, 64, +, fp, _Quad, 8r, 7,
                       KMP_ARCH_X86) // __kmpc_atomic_float8_add_cpt_fp
ATOMIC_CMPXCHG_CPT_MIX(float8, kmp_real64, sub_cpt, 64, -, fp, _Quad, 8r, 7,
                       KMP_ARCH_X86) // __kmpc_atomic_float8_sub_cpt_fp
ATOMIC_CMPXCHG_CPT_MIX(float8, kmp_real64, mul_cpt, 64, *, fp, _Quad, 8r, 7,
                       KMP_ARCH_X86) // __kmpc_atomic_float8_mul_cpt_fp
ATOMIC_CMPXCHG_CPT_MIX(float8, kmp_real64, div_cpt, 64, /, fp, _Quad, 8r, 7,
                       KMP_ARCH_X86) // __kmpc_atomic_float8_div_cpt_fp

ATOMIC_CRITICAL_CPT_MIX(float10, long double, add_cpt, +, fp, _Quad, 10r,
                        1) // __kmpc_atomic_float10_add_cpt_fp
ATOMIC_CRITICAL_CPT_MIX(float10, long double, sub_cpt, -, fp, _Quad, 10r,
                        1) // __kmpc_atomic_float10_sub_cpt_fp
ATOMIC_CRITICAL_CPT_MIX(float10, long double, mul_cpt, *, fp, _Quad, 10r,
                        1) // __kmpc_atomic_float10_mul_cpt_fp
ATOMIC_CRITICAL_CPT_MIX(float10, long double, div_cpt, /, fp, _Quad, 10r,
                        1) // __kmpc_atomic_float10_div_cpt_fp

#endif // KMP_HAVE_QUAD

// ------------------------------------------------------------------------
// Routines for C/C++ Reduction operators && and ||

// -------------------------------------------------------------------------
// Operation on *lhs, rhs bound by critical section
//     OP     - operator (it's supposed to contain an assignment)
//     LCK_ID - lock identifier
// Note: don't check gtid as it should always be valid
// 1, 2-byte - expect valid parameter, other - check before this macro
#define OP_CRITICAL_L_CPT(OP, LCK_ID)                                          \
  __kmp_acquire_atomic_lock(&ATOMIC_LOCK##LCK_ID, gtid);                       \
                                                                               \
  if (flag) {                                                                  \
    new_value OP rhs;                                                          \
  } else                                                                       \
    new_value = (*lhs);                                                        \
                                                                               \
  __kmp_release_atomic_lock(&ATOMIC_LOCK##LCK_ID, gtid);

// ------------------------------------------------------------------------
#ifdef KMP_GOMP_COMPAT
#define OP_GOMP_CRITICAL_L_CPT(OP, FLAG)                                       \
  if ((FLAG) && (__kmp_atomic_mode == 2)) {                                    \
    KMP_CHECK_GTID;                                                            \
    OP_CRITICAL_L_CPT(OP, 0);                                                  \
    return new_value;                                                          \
  }
#else
#define OP_GOMP_CRITICAL_L_CPT(OP, FLAG)
#endif /* KMP_GOMP_COMPAT */

// ------------------------------------------------------------------------
// Need separate macros for &&, || because there is no combined assignment
#define ATOMIC_CMPX_L_CPT(TYPE_ID, OP_ID, TYPE, BITS, OP, GOMP_FLAG)           \
  ATOMIC_BEGIN_CPT(TYPE_ID, OP_ID, TYPE, TYPE)                                 \
  TYPE new_value;                                                              \
  OP_GOMP_CRITICAL_L_CPT(= *lhs OP, GOMP_FLAG)                                 \
  OP_CMPXCHG_CPT(TYPE, BITS, OP)                                               \
  }

ATOMIC_CMPX_L_CPT(fixed1, andl_cpt, char, 8, &&,
                  KMP_ARCH_X86) // __kmpc_atomic_fixed1_andl_cpt
ATOMIC_CMPX_L_CPT(fixed1, orl_cpt, char, 8, ||,
                  KMP_ARCH_X86) // __kmpc_atomic_fixed1_orl_cpt
ATOMIC_CMPX_L_CPT(fixed2, andl_cpt, short, 16, &&,
                  KMP_ARCH_X86) // __kmpc_atomic_fixed2_andl_cpt
ATOMIC_CMPX_L_CPT(fixed2, orl_cpt, short, 16, ||,
                  KMP_ARCH_X86) // __kmpc_atomic_fixed2_orl_cpt
ATOMIC_CMPX_L_CPT(fixed4, andl_cpt, kmp_int32, 32, &&,
                  0) // __kmpc_atomic_fixed4_andl_cpt
ATOMIC_CMPX_L_CPT(fixed4, orl_cpt, kmp_int32, 32, ||,
                  0) // __kmpc_atomic_fixed4_orl_cpt
ATOMIC_CMPX_L_CPT(fixed8, andl_cpt, kmp_int64, 64, &&,
                  KMP_ARCH_X86) // __kmpc_atomic_fixed8_andl_cpt
ATOMIC_CMPX_L_CPT(fixed8, orl_cpt, kmp_int64, 64, ||,
                  KMP_ARCH_X86) // __kmpc_atomic_fixed8_orl_cpt

// -------------------------------------------------------------------------
// Routines for Fortran operators that matched no one in C:
// MAX, MIN, .EQV., .NEQV.
// Operators .AND., .OR. are covered by __kmpc_atomic_*_{andl,orl}_cpt
// Intrinsics IAND, IOR, IEOR are covered by __kmpc_atomic_*_{andb,orb,xor}_cpt

// -------------------------------------------------------------------------
// MIN and MAX need separate macros
// OP - operator to check if we need any actions?
#define MIN_MAX_CRITSECT_CPT(OP, LCK_ID)                                       \
  __kmp_acquire_atomic_lock(&ATOMIC_LOCK##LCK_ID, gtid);                       \
                                                                               \
  if (*lhs OP rhs) { /* still need actions? */                                 \
    old_value = *lhs;                                                          \
    *lhs = rhs;                                                                \
    if (flag)                                                                  \
      new_value = rhs;                                                         \
    else                                                                       \
      new_value = old_value;                                                   \
  }                                                                            \
  __kmp_release_atomic_lock(&ATOMIC_LOCK##LCK_ID, gtid);                       \
  return new_value;

// -------------------------------------------------------------------------
#ifdef KMP_GOMP_COMPAT
#define GOMP_MIN_MAX_CRITSECT_CPT(OP, FLAG)                                    \
  if ((FLAG) && (__kmp_atomic_mode == 2)) {                                    \
    KMP_CHECK_GTID;                                                            \
    MIN_MAX_CRITSECT_CPT(OP, 0);                                               \
  }
#else
#define GOMP_MIN_MAX_CRITSECT_CPT(OP, FLAG)
#endif /* KMP_GOMP_COMPAT */

// -------------------------------------------------------------------------
#define MIN_MAX_CMPXCHG_CPT(TYPE, BITS, OP)                                    \
  {                                                                            \
    TYPE KMP_ATOMIC_VOLATILE temp_val;                                         \
    /*TYPE old_value; */                                                       \
    temp_val = *lhs;                                                           \
    old_value = temp_val;                                                      \
    while (old_value OP rhs && /* still need actions? */                       \
           !KMP_COMPARE_AND_STORE_ACQ##BITS(                                   \
               (kmp_int##BITS *)lhs,                                           \
               *VOLATILE_CAST(kmp_int##BITS *) & old_value,                    \
               *VOLATILE_CAST(kmp_int##BITS *) & rhs)) {                       \
      KMP_CPU_PAUSE();                                                         \
      temp_val = *lhs;                                                         \
      old_value = temp_val;                                                    \
    }                                                                          \
    if (flag)                                                                  \
      return rhs;                                                              \
    else                                                                       \
      return old_value;                                                        \
  }

// -------------------------------------------------------------------------
// 1-byte, 2-byte operands - use critical section
#define MIN_MAX_CRITICAL_CPT(TYPE_ID, OP_ID, TYPE, OP, LCK_ID, GOMP_FLAG)      \
  ATOMIC_BEGIN_CPT(TYPE_ID, OP_ID, TYPE, TYPE)                                 \
  TYPE new_value, old_value;                                                   \
  if (*lhs OP rhs) { /* need actions? */                                       \
    GOMP_MIN_MAX_CRITSECT_CPT(OP, GOMP_FLAG)                                   \
    MIN_MAX_CRITSECT_CPT(OP, LCK_ID)                                           \
  }                                                                            \
  return *lhs;                                                                 \
  }

#define MIN_MAX_COMPXCHG_CPT(TYPE_ID, OP_ID, TYPE, BITS, OP, GOMP_FLAG)        \
  ATOMIC_BEGIN_CPT(TYPE_ID, OP_ID, TYPE, TYPE)                                 \
  TYPE new_value, old_value;                                                   \
  if (*lhs OP rhs) {                                                           \
    GOMP_MIN_MAX_CRITSECT_CPT(OP, GOMP_FLAG)                                   \
    MIN_MAX_CMPXCHG_CPT(TYPE, BITS, OP)                                        \
  }                                                                            \
  return *lhs;                                                                 \
  }

MIN_MAX_COMPXCHG_CPT(fixed1, max_cpt, char, 8, <,
                     KMP_ARCH_X86) // __kmpc_atomic_fixed1_max_cpt
MIN_MAX_COMPXCHG_CPT(fixed1, min_cpt, char, 8, >,
                     KMP_ARCH_X86) // __kmpc_atomic_fixed1_min_cpt
MIN_MAX_COMPXCHG_CPT(fixed2, max_cpt, short, 16, <,
                     KMP_ARCH_X86) // __kmpc_atomic_fixed2_max_cpt
MIN_MAX_COMPXCHG_CPT(fixed2, min_cpt, short, 16, >,
                     KMP_ARCH_X86) // __kmpc_atomic_fixed2_min_cpt
MIN_MAX_COMPXCHG_CPT(fixed4, max_cpt, kmp_int32, 32, <,
                     0) // __kmpc_atomic_fixed4_max_cpt
MIN_MAX_COMPXCHG_CPT(fixed4, min_cpt, kmp_int32, 32, >,
                     0) // __kmpc_atomic_fixed4_min_cpt
MIN_MAX_COMPXCHG_CPT(fixed8, max_cpt, kmp_int64, 64, <,
                     KMP_ARCH_X86) // __kmpc_atomic_fixed8_max_cpt
MIN_MAX_COMPXCHG_CPT(fixed8, min_cpt, kmp_int64, 64, >,
                     KMP_ARCH_X86) // __kmpc_atomic_fixed8_min_cpt
MIN_MAX_COMPXCHG_CPT(float4, max_cpt, kmp_real32, 32, <,
                     KMP_ARCH_X86) // __kmpc_atomic_float4_max_cpt
MIN_MAX_COMPXCHG_CPT(float4, min_cpt, kmp_real32, 32, >,
                     KMP_ARCH_X86) // __kmpc_atomic_float4_min_cpt
MIN_MAX_COMPXCHG_CPT(float8, max_cpt, kmp_real64, 64, <,
                     KMP_ARCH_X86) // __kmpc_atomic_float8_max_cpt
MIN_MAX_COMPXCHG_CPT(float8, min_cpt, kmp_real64, 64, >,
                     KMP_ARCH_X86) // __kmpc_atomic_float8_min_cpt
#if KMP_HAVE_QUAD
MIN_MAX_CRITICAL_CPT(float16, max_cpt, QUAD_LEGACY, <, 16r,
                     1) // __kmpc_atomic_float16_max_cpt
MIN_MAX_CRITICAL_CPT(float16, min_cpt, QUAD_LEGACY, >, 16r,
                     1) // __kmpc_atomic_float16_min_cpt
#if (KMP_ARCH_X86)
MIN_MAX_CRITICAL_CPT(float16, max_a16_cpt, Quad_a16_t, <, 16r,
                     1) // __kmpc_atomic_float16_max_a16_cpt
MIN_MAX_CRITICAL_CPT(float16, min_a16_cpt, Quad_a16_t, >, 16r,
                     1) // __kmpc_atomic_float16_mix_a16_cpt
#endif
#endif

// ------------------------------------------------------------------------
#ifdef KMP_GOMP_COMPAT
#define OP_GOMP_CRITICAL_EQV_CPT(OP, FLAG)                                     \
  if ((FLAG) && (__kmp_atomic_mode == 2)) {                                    \
    KMP_CHECK_GTID;                                                            \
    OP_CRITICAL_CPT(OP, 0);                                                    \
  }
#else
#define OP_GOMP_CRITICAL_EQV_CPT(OP, FLAG)
#endif /* KMP_GOMP_COMPAT */
// ------------------------------------------------------------------------
#define ATOMIC_CMPX_EQV_CPT(TYPE_ID, OP_ID, TYPE, BITS, OP, GOMP_FLAG)         \
  ATOMIC_BEGIN_CPT(TYPE_ID, OP_ID, TYPE, TYPE)                                 \
  TYPE new_value;                                                              \
  OP_GOMP_CRITICAL_EQV_CPT(^= ~, GOMP_FLAG) /* send assignment */              \
  OP_CMPXCHG_CPT(TYPE, BITS, OP)                                               \
  }

// ------------------------------------------------------------------------

ATOMIC_CMPXCHG_CPT(fixed1, neqv_cpt, kmp_int8, 8, ^,
                   KMP_ARCH_X86) // __kmpc_atomic_fixed1_neqv_cpt
ATOMIC_CMPXCHG_CPT(fixed2, neqv_cpt, kmp_int16, 16, ^,
                   KMP_ARCH_X86) // __kmpc_atomic_fixed2_neqv_cpt
ATOMIC_CMPXCHG_CPT(fixed4, neqv_cpt, kmp_int32, 32, ^,
                   KMP_ARCH_X86) // __kmpc_atomic_fixed4_neqv_cpt
ATOMIC_CMPXCHG_CPT(fixed8, neqv_cpt, kmp_int64, 64, ^,
                   KMP_ARCH_X86) // __kmpc_atomic_fixed8_neqv_cpt
ATOMIC_CMPX_EQV_CPT(fixed1, eqv_cpt, kmp_int8, 8, ^~,
                    KMP_ARCH_X86) // __kmpc_atomic_fixed1_eqv_cpt
ATOMIC_CMPX_EQV_CPT(fixed2, eqv_cpt, kmp_int16, 16, ^~,
                    KMP_ARCH_X86) // __kmpc_atomic_fixed2_eqv_cpt
ATOMIC_CMPX_EQV_CPT(fixed4, eqv_cpt, kmp_int32, 32, ^~,
                    KMP_ARCH_X86) // __kmpc_atomic_fixed4_eqv_cpt
ATOMIC_CMPX_EQV_CPT(fixed8, eqv_cpt, kmp_int64, 64, ^~,
                    KMP_ARCH_X86) // __kmpc_atomic_fixed8_eqv_cpt

// ------------------------------------------------------------------------
// Routines for Extended types: long double, _Quad, complex flavours (use
// critical section)
//     TYPE_ID, OP_ID, TYPE - detailed above
//     OP      - operator
//     LCK_ID  - lock identifier, used to possibly distinguish lock variable
#define ATOMIC_CRITICAL_CPT(TYPE_ID, OP_ID, TYPE, OP, LCK_ID, GOMP_FLAG)       \
  ATOMIC_BEGIN_CPT(TYPE_ID, OP_ID, TYPE, TYPE)                                 \
  TYPE new_value;                                                              \
  OP_GOMP_CRITICAL_CPT(OP, GOMP_FLAG) /* send assignment */                    \
  OP_CRITICAL_CPT(OP## =, LCK_ID) /* send assignment */                        \
  }

// ------------------------------------------------------------------------
// Workaround for cmplx4. Regular routines with return value don't work
// on Win_32e. Let's return captured values through the additional parameter.
#define OP_CRITICAL_CPT_WRK(OP, LCK_ID)                                        \
  __kmp_acquire_atomic_lock(&ATOMIC_LOCK##LCK_ID, gtid);                       \
                                                                               \
  if (flag) {                                                                  \
    (*lhs) OP rhs;                                                             \
    (*out) = (*lhs);                                                           \
  } else {                                                                     \
    (*out) = (*lhs);                                                           \
    (*lhs) OP rhs;                                                             \
  }                                                                            \
                                                                               \
  __kmp_release_atomic_lock(&ATOMIC_LOCK##LCK_ID, gtid);                       \
  return;
// ------------------------------------------------------------------------

#ifdef KMP_GOMP_COMPAT
#define OP_GOMP_CRITICAL_CPT_WRK(OP, FLAG)                                     \
  if ((FLAG) && (__kmp_atomic_mode == 2)) {                                    \
    KMP_CHECK_GTID;                                                            \
    OP_CRITICAL_CPT_WRK(OP## =, 0);                                            \
  }
#else
#define OP_GOMP_CRITICAL_CPT_WRK(OP, FLAG)
#endif /* KMP_GOMP_COMPAT */
// ------------------------------------------------------------------------

#define ATOMIC_BEGIN_WRK(TYPE_ID, OP_ID, TYPE)                                 \
  void __kmpc_atomic_##TYPE_ID##_##OP_ID(ident_t *id_ref, int gtid, TYPE *lhs, \
                                         TYPE rhs, TYPE *out, int flag) {      \
    KMP_DEBUG_ASSERT(__kmp_init_serial);                                       \
    KA_TRACE(100, ("__kmpc_atomic_" #TYPE_ID "_" #OP_ID ": T#%d\n", gtid));
// ------------------------------------------------------------------------

#define ATOMIC_CRITICAL_CPT_WRK(TYPE_ID, OP_ID, TYPE, OP, LCK_ID, GOMP_FLAG)   \
  ATOMIC_BEGIN_WRK(TYPE_ID, OP_ID, TYPE)                                       \
  OP_GOMP_CRITICAL_CPT_WRK(OP, GOMP_FLAG)                                      \
  OP_CRITICAL_CPT_WRK(OP## =, LCK_ID)                                          \
  }
// The end of workaround for cmplx4

/* ------------------------------------------------------------------------- */
// routines for long double type
ATOMIC_CRITICAL_CPT(float10, add_cpt, long double, +, 10r,
                    1) // __kmpc_atomic_float10_add_cpt
ATOMIC_CRITICAL_CPT(float10, sub_cpt, long double, -, 10r,
                    1) // __kmpc_atomic_float10_sub_cpt
ATOMIC_CRITICAL_CPT(float10, mul_cpt, long double, *, 10r,
                    1) // __kmpc_atomic_float10_mul_cpt
ATOMIC_CRITICAL_CPT(float10, div_cpt, long double, /, 10r,
                    1) // __kmpc_atomic_float10_div_cpt
#if KMP_HAVE_QUAD
// routines for _Quad type
ATOMIC_CRITICAL_CPT(float16, add_cpt, QUAD_LEGACY, +, 16r,
                    1) // __kmpc_atomic_float16_add_cpt
ATOMIC_CRITICAL_CPT(float16, sub_cpt, QUAD_LEGACY, -, 16r,
                    1) // __kmpc_atomic_float16_sub_cpt
ATOMIC_CRITICAL_CPT(float16, mul_cpt, QUAD_LEGACY, *, 16r,
                    1) // __kmpc_atomic_float16_mul_cpt
ATOMIC_CRITICAL_CPT(float16, div_cpt, QUAD_LEGACY, /, 16r,
                    1) // __kmpc_atomic_float16_div_cpt
#if (KMP_ARCH_X86)
ATOMIC_CRITICAL_CPT(float16, add_a16_cpt, Quad_a16_t, +, 16r,
                    1) // __kmpc_atomic_float16_add_a16_cpt
ATOMIC_CRITICAL_CPT(float16, sub_a16_cpt, Quad_a16_t, -, 16r,
                    1) // __kmpc_atomic_float16_sub_a16_cpt
ATOMIC_CRITICAL_CPT(float16, mul_a16_cpt, Quad_a16_t, *, 16r,
                    1) // __kmpc_atomic_float16_mul_a16_cpt
ATOMIC_CRITICAL_CPT(float16, div_a16_cpt, Quad_a16_t, /, 16r,
                    1) // __kmpc_atomic_float16_div_a16_cpt
#endif
#endif

// routines for complex types

// cmplx4 routines to return void
ATOMIC_CRITICAL_CPT_WRK(cmplx4, add_cpt, kmp_cmplx32, +, 8c,
                        1) // __kmpc_atomic_cmplx4_add_cpt
ATOMIC_CRITICAL_CPT_WRK(cmplx4, sub_cpt, kmp_cmplx32, -, 8c,
                        1) // __kmpc_atomic_cmplx4_sub_cpt
ATOMIC_CRITICAL_CPT_WRK(cmplx4, mul_cpt, kmp_cmplx32, *, 8c,
                        1) // __kmpc_atomic_cmplx4_mul_cpt
ATOMIC_CRITICAL_CPT_WRK(cmplx4, div_cpt, kmp_cmplx32, /, 8c,
                        1) // __kmpc_atomic_cmplx4_div_cpt

ATOMIC_CRITICAL_CPT(cmplx8, add_cpt, kmp_cmplx64, +, 16c,
                    1) // __kmpc_atomic_cmplx8_add_cpt
ATOMIC_CRITICAL_CPT(cmplx8, sub_cpt, kmp_cmplx64, -, 16c,
                    1) // __kmpc_atomic_cmplx8_sub_cpt
ATOMIC_CRITICAL_CPT(cmplx8, mul_cpt, kmp_cmplx64, *, 16c,
                    1) // __kmpc_atomic_cmplx8_mul_cpt
ATOMIC_CRITICAL_CPT(cmplx8, div_cpt, kmp_cmplx64, /, 16c,
                    1) // __kmpc_atomic_cmplx8_div_cpt
ATOMIC_CRITICAL_CPT(cmplx10, add_cpt, kmp_cmplx80, +, 20c,
                    1) // __kmpc_atomic_cmplx10_add_cpt
ATOMIC_CRITICAL_CPT(cmplx10, sub_cpt, kmp_cmplx80, -, 20c,
                    1) // __kmpc_atomic_cmplx10_sub_cpt
ATOMIC_CRITICAL_CPT(cmplx10, mul_cpt, kmp_cmplx80, *, 20c,
                    1) // __kmpc_atomic_cmplx10_mul_cpt
ATOMIC_CRITICAL_CPT(cmplx10, div_cpt, kmp_cmplx80, /, 20c,
                    1) // __kmpc_atomic_cmplx10_div_cpt
#if KMP_HAVE_QUAD
ATOMIC_CRITICAL_CPT(cmplx16, add_cpt, CPLX128_LEG, +, 32c,
                    1) // __kmpc_atomic_cmplx16_add_cpt
ATOMIC_CRITICAL_CPT(cmplx16, sub_cpt, CPLX128_LEG, -, 32c,
                    1) // __kmpc_atomic_cmplx16_sub_cpt
ATOMIC_CRITICAL_CPT(cmplx16, mul_cpt, CPLX128_LEG, *, 32c,
                    1) // __kmpc_atomic_cmplx16_mul_cpt
ATOMIC_CRITICAL_CPT(cmplx16, div_cpt, CPLX128_LEG, /, 32c,
                    1) // __kmpc_atomic_cmplx16_div_cpt
#if (KMP_ARCH_X86)
ATOMIC_CRITICAL_CPT(cmplx16, add_a16_cpt, kmp_cmplx128_a16_t, +, 32c,
                    1) // __kmpc_atomic_cmplx16_add_a16_cpt
ATOMIC_CRITICAL_CPT(cmplx16, sub_a16_cpt, kmp_cmplx128_a16_t, -, 32c,
                    1) // __kmpc_atomic_cmplx16_sub_a16_cpt
ATOMIC_CRITICAL_CPT(cmplx16, mul_a16_cpt, kmp_cmplx128_a16_t, *, 32c,
                    1) // __kmpc_atomic_cmplx16_mul_a16_cpt
ATOMIC_CRITICAL_CPT(cmplx16, div_a16_cpt, kmp_cmplx128_a16_t, /, 32c,
                    1) // __kmpc_atomic_cmplx16_div_a16_cpt
#endif
#endif

#if OMP_40_ENABLED

// OpenMP 4.0: v = x = expr binop x; { v = x; x = expr binop x; } { x = expr
// binop x; v = x; }  for non-commutative operations.
// Supported only on IA-32 architecture and Intel(R) 64

// -------------------------------------------------------------------------
// Operation on *lhs, rhs bound by critical section
//     OP     - operator (it's supposed to contain an assignment)
//     LCK_ID - lock identifier
// Note: don't check gtid as it should always be valid
// 1, 2-byte - expect valid parameter, other - check before this macro
#define OP_CRITICAL_CPT_REV(OP, LCK_ID)                                        \
  __kmp_acquire_atomic_lock(&ATOMIC_LOCK##LCK_ID, gtid);                       \
                                                                               \
  if (flag) {                                                                  \
    /*temp_val = (*lhs);*/                                                     \
    (*lhs) = (rhs)OP(*lhs);                                                    \
    new_value = (*lhs);                                                        \
  } else {                                                                     \
    new_value = (*lhs);                                                        \
    (*lhs) = (rhs)OP(*lhs);                                                    \
  }                                                                            \
  __kmp_release_atomic_lock(&ATOMIC_LOCK##LCK_ID, gtid);                       \
  return new_value;

// ------------------------------------------------------------------------
#ifdef KMP_GOMP_COMPAT
#define OP_GOMP_CRITICAL_CPT_REV(OP, FLAG)                                     \
  if ((FLAG) && (__kmp_atomic_mode == 2)) {                                    \
    KMP_CHECK_GTID;                                                            \
    OP_CRITICAL_CPT_REV(OP, 0);                                                \
  }
#else
#define OP_GOMP_CRITICAL_CPT_REV(OP, FLAG)
#endif /* KMP_GOMP_COMPAT */

// ------------------------------------------------------------------------
// Operation on *lhs, rhs using "compare_and_store" routine
//     TYPE    - operands' type
//     BITS    - size in bits, used to distinguish low level calls
//     OP      - operator
// Note: temp_val introduced in order to force the compiler to read
//       *lhs only once (w/o it the compiler reads *lhs twice)
#define OP_CMPXCHG_CPT_REV(TYPE, BITS, OP)                                     \
  {                                                                            \
    TYPE KMP_ATOMIC_VOLATILE temp_val;                                         \
    TYPE old_value, new_value;                                                 \
    temp_val = *lhs;                                                           \
    old_value = temp_val;                                                      \
    new_value = rhs OP old_value;                                              \
    while (!KMP_COMPARE_AND_STORE_ACQ##BITS(                                   \
        (kmp_int##BITS *)lhs, *VOLATILE_CAST(kmp_int##BITS *) & old_value,     \
        *VOLATILE_CAST(kmp_int##BITS *) & new_value)) {                        \
      KMP_CPU_PAUSE();                                                         \
                                                                               \
      temp_val = *lhs;                                                         \
      old_value = temp_val;                                                    \
      new_value = rhs OP old_value;                                            \
    }                                                                          \
    if (flag) {                                                                \
      return new_value;                                                        \
    } else                                                                     \
      return old_value;                                                        \
  }

// -------------------------------------------------------------------------
#define ATOMIC_CMPXCHG_CPT_REV(TYPE_ID, OP_ID, TYPE, BITS, OP, GOMP_FLAG)      \
  ATOMIC_BEGIN_CPT(TYPE_ID, OP_ID, TYPE, TYPE)                                 \
  TYPE new_value;                                                              \
  OP_GOMP_CRITICAL_CPT_REV(OP, GOMP_FLAG)                                      \
  OP_CMPXCHG_CPT_REV(TYPE, BITS, OP)                                           \
  }

ATOMIC_CMPXCHG_CPT_REV(fixed1, div_cpt_rev, kmp_int8, 8, /,
                       KMP_ARCH_X86) // __kmpc_atomic_fixed1_div_cpt_rev
ATOMIC_CMPXCHG_CPT_REV(fixed1u, div_cpt_rev, kmp_uint8, 8, /,
                       KMP_ARCH_X86) // __kmpc_atomic_fixed1u_div_cpt_rev
ATOMIC_CMPXCHG_CPT_REV(fixed1, shl_cpt_rev, kmp_int8, 8, <<,
                       KMP_ARCH_X86) // __kmpc_atomic_fixed1_shl_cpt_rev
ATOMIC_CMPXCHG_CPT_REV(fixed1, shr_cpt_rev, kmp_int8, 8, >>,
                       KMP_ARCH_X86) // __kmpc_atomic_fixed1_shr_cpt_rev
ATOMIC_CMPXCHG_CPT_REV(fixed1u, shr_cpt_rev, kmp_uint8, 8, >>,
                       KMP_ARCH_X86) // __kmpc_atomic_fixed1u_shr_cpt_rev
ATOMIC_CMPXCHG_CPT_REV(fixed1, sub_cpt_rev, kmp_int8, 8, -,
                       KMP_ARCH_X86) // __kmpc_atomic_fixed1_sub_cpt_rev
ATOMIC_CMPXCHG_CPT_REV(fixed2, div_cpt_rev, kmp_int16, 16, /,
                       KMP_ARCH_X86) // __kmpc_atomic_fixed2_div_cpt_rev
ATOMIC_CMPXCHG_CPT_REV(fixed2u, div_cpt_rev, kmp_uint16, 16, /,
                       KMP_ARCH_X86) // __kmpc_atomic_fixed2u_div_cpt_rev
ATOMIC_CMPXCHG_CPT_REV(fixed2, shl_cpt_rev, kmp_int16, 16, <<,
                       KMP_ARCH_X86) // __kmpc_atomic_fixed2_shl_cpt_rev
ATOMIC_CMPXCHG_CPT_REV(fixed2, shr_cpt_rev, kmp_int16, 16, >>,
                       KMP_ARCH_X86) // __kmpc_atomic_fixed2_shr_cpt_rev
ATOMIC_CMPXCHG_CPT_REV(fixed2u, shr_cpt_rev, kmp_uint16, 16, >>,
                       KMP_ARCH_X86) // __kmpc_atomic_fixed2u_shr_cpt_rev
ATOMIC_CMPXCHG_CPT_REV(fixed2, sub_cpt_rev, kmp_int16, 16, -,
                       KMP_ARCH_X86) // __kmpc_atomic_fixed2_sub_cpt_rev
ATOMIC_CMPXCHG_CPT_REV(fixed4, div_cpt_rev, kmp_int32, 32, /,
                       KMP_ARCH_X86) // __kmpc_atomic_fixed4_div_cpt_rev
ATOMIC_CMPXCHG_CPT_REV(fixed4u, div_cpt_rev, kmp_uint32, 32, /,
                       KMP_ARCH_X86) // __kmpc_atomic_fixed4u_div_cpt_rev
ATOMIC_CMPXCHG_CPT_REV(fixed4, shl_cpt_rev, kmp_int32, 32, <<,
                       KMP_ARCH_X86) // __kmpc_atomic_fixed4_shl_cpt_rev
ATOMIC_CMPXCHG_CPT_REV(fixed4, shr_cpt_rev, kmp_int32, 32, >>,
                       KMP_ARCH_X86) // __kmpc_atomic_fixed4_shr_cpt_rev
ATOMIC_CMPXCHG_CPT_REV(fixed4u, shr_cpt_rev, kmp_uint32, 32, >>,
                       KMP_ARCH_X86) // __kmpc_atomic_fixed4u_shr_cpt_rev
ATOMIC_CMPXCHG_CPT_REV(fixed4, sub_cpt_rev, kmp_int32, 32, -,
                       KMP_ARCH_X86) // __kmpc_atomic_fixed4_sub_cpt_rev
ATOMIC_CMPXCHG_CPT_REV(fixed8, div_cpt_rev, kmp_int64, 64, /,
                       KMP_ARCH_X86) // __kmpc_atomic_fixed8_div_cpt_rev
ATOMIC_CMPXCHG_CPT_REV(fixed8u, div_cpt_rev, kmp_uint64, 64, /,
                       KMP_ARCH_X86) // __kmpc_atomic_fixed8u_div_cpt_rev
ATOMIC_CMPXCHG_CPT_REV(fixed8, shl_cpt_rev, kmp_int64, 64, <<,
                       KMP_ARCH_X86) // __kmpc_atomic_fixed8_shl_cpt_rev
ATOMIC_CMPXCHG_CPT_REV(fixed8, shr_cpt_rev, kmp_int64, 64, >>,
                       KMP_ARCH_X86) // __kmpc_atomic_fixed8_shr_cpt_rev
ATOMIC_CMPXCHG_CPT_REV(fixed8u, shr_cpt_rev, kmp_uint64, 64, >>,
                       KMP_ARCH_X86) // __kmpc_atomic_fixed8u_shr_cpt_rev
ATOMIC_CMPXCHG_CPT_REV(fixed8, sub_cpt_rev, kmp_int64, 64, -,
                       KMP_ARCH_X86) // __kmpc_atomic_fixed8_sub_cpt_rev
ATOMIC_CMPXCHG_CPT_REV(float4, div_cpt_rev, kmp_real32, 32, /,
                       KMP_ARCH_X86) // __kmpc_atomic_float4_div_cpt_rev
ATOMIC_CMPXCHG_CPT_REV(float4, sub_cpt_rev, kmp_real32, 32, -,
                       KMP_ARCH_X86) // __kmpc_atomic_float4_sub_cpt_rev
ATOMIC_CMPXCHG_CPT_REV(float8, div_cpt_rev, kmp_real64, 64, /,
                       KMP_ARCH_X86) // __kmpc_atomic_float8_div_cpt_rev
ATOMIC_CMPXCHG_CPT_REV(float8, sub_cpt_rev, kmp_real64, 64, -,
                       KMP_ARCH_X86) // __kmpc_atomic_float8_sub_cpt_rev
//              TYPE_ID,OP_ID, TYPE,          OP,  GOMP_FLAG

// ------------------------------------------------------------------------
// Routines for Extended types: long double, _Quad, complex flavours (use
// critical section)
//     TYPE_ID, OP_ID, TYPE - detailed above
//     OP      - operator
//     LCK_ID  - lock identifier, used to possibly distinguish lock variable
#define ATOMIC_CRITICAL_CPT_REV(TYPE_ID, OP_ID, TYPE, OP, LCK_ID, GOMP_FLAG)   \
  ATOMIC_BEGIN_CPT(TYPE_ID, OP_ID, TYPE, TYPE)                                 \
  TYPE new_value;                                                              \
  /*printf("__kmp_atomic_mode = %d\n", __kmp_atomic_mode);*/                   \
  OP_GOMP_CRITICAL_CPT_REV(OP, GOMP_FLAG)                                      \
  OP_CRITICAL_CPT_REV(OP, LCK_ID)                                              \
  }

/* ------------------------------------------------------------------------- */
// routines for long double type
ATOMIC_CRITICAL_CPT_REV(float10, sub_cpt_rev, long double, -, 10r,
                        1) // __kmpc_atomic_float10_sub_cpt_rev
ATOMIC_CRITICAL_CPT_REV(float10, div_cpt_rev, long double, /, 10r,
                        1) // __kmpc_atomic_float10_div_cpt_rev
#if KMP_HAVE_QUAD
// routines for _Quad type
ATOMIC_CRITICAL_CPT_REV(float16, sub_cpt_rev, QUAD_LEGACY, -, 16r,
                        1) // __kmpc_atomic_float16_sub_cpt_rev
ATOMIC_CRITICAL_CPT_REV(float16, div_cpt_rev, QUAD_LEGACY, /, 16r,
                        1) // __kmpc_atomic_float16_div_cpt_rev
#if (KMP_ARCH_X86)
ATOMIC_CRITICAL_CPT_REV(float16, sub_a16_cpt_rev, Quad_a16_t, -, 16r,
                        1) // __kmpc_atomic_float16_sub_a16_cpt_rev
ATOMIC_CRITICAL_CPT_REV(float16, div_a16_cpt_rev, Quad_a16_t, /, 16r,
                        1) // __kmpc_atomic_float16_div_a16_cpt_rev
#endif
#endif

// routines for complex types

// ------------------------------------------------------------------------
// Workaround for cmplx4. Regular routines with return value don't work
// on Win_32e. Let's return captured values through the additional parameter.
#define OP_CRITICAL_CPT_REV_WRK(OP, LCK_ID)                                    \
  __kmp_acquire_atomic_lock(&ATOMIC_LOCK##LCK_ID, gtid);                       \
                                                                               \
  if (flag) {                                                                  \
    (*lhs) = (rhs)OP(*lhs);                                                    \
    (*out) = (*lhs);                                                           \
  } else {                                                                     \
    (*out) = (*lhs);                                                           \
    (*lhs) = (rhs)OP(*lhs);                                                    \
  }                                                                            \
                                                                               \
  __kmp_release_atomic_lock(&ATOMIC_LOCK##LCK_ID, gtid);                       \
  return;
// ------------------------------------------------------------------------

#ifdef KMP_GOMP_COMPAT
#define OP_GOMP_CRITICAL_CPT_REV_WRK(OP, FLAG)                                 \
  if ((FLAG) && (__kmp_atomic_mode == 2)) {                                    \
    KMP_CHECK_GTID;                                                            \
    OP_CRITICAL_CPT_REV_WRK(OP, 0);                                            \
  }
#else
#define OP_GOMP_CRITICAL_CPT_REV_WRK(OP, FLAG)
#endif /* KMP_GOMP_COMPAT */
// ------------------------------------------------------------------------

#define ATOMIC_CRITICAL_CPT_REV_WRK(TYPE_ID, OP_ID, TYPE, OP, LCK_ID,          \
                                    GOMP_FLAG)                                 \
  ATOMIC_BEGIN_WRK(TYPE_ID, OP_ID, TYPE)                                       \
  OP_GOMP_CRITICAL_CPT_REV_WRK(OP, GOMP_FLAG)                                  \
  OP_CRITICAL_CPT_REV_WRK(OP, LCK_ID)                                          \
  }
// The end of workaround for cmplx4

// !!! TODO: check if we need to return void for cmplx4 routines
// cmplx4 routines to return void
ATOMIC_CRITICAL_CPT_REV_WRK(cmplx4, sub_cpt_rev, kmp_cmplx32, -, 8c,
                            1) // __kmpc_atomic_cmplx4_sub_cpt_rev
ATOMIC_CRITICAL_CPT_REV_WRK(cmplx4, div_cpt_rev, kmp_cmplx32, /, 8c,
                            1) // __kmpc_atomic_cmplx4_div_cpt_rev

ATOMIC_CRITICAL_CPT_REV(cmplx8, sub_cpt_rev, kmp_cmplx64, -, 16c,
                        1) // __kmpc_atomic_cmplx8_sub_cpt_rev
ATOMIC_CRITICAL_CPT_REV(cmplx8, div_cpt_rev, kmp_cmplx64, /, 16c,
                        1) // __kmpc_atomic_cmplx8_div_cpt_rev
ATOMIC_CRITICAL_CPT_REV(cmplx10, sub_cpt_rev, kmp_cmplx80, -, 20c,
                        1) // __kmpc_atomic_cmplx10_sub_cpt_rev
ATOMIC_CRITICAL_CPT_REV(cmplx10, div_cpt_rev, kmp_cmplx80, /, 20c,
                        1) // __kmpc_atomic_cmplx10_div_cpt_rev
#if KMP_HAVE_QUAD
ATOMIC_CRITICAL_CPT_REV(cmplx16, sub_cpt_rev, CPLX128_LEG, -, 32c,
                        1) // __kmpc_atomic_cmplx16_sub_cpt_rev
ATOMIC_CRITICAL_CPT_REV(cmplx16, div_cpt_rev, CPLX128_LEG, /, 32c,
                        1) // __kmpc_atomic_cmplx16_div_cpt_rev
#if (KMP_ARCH_X86)
ATOMIC_CRITICAL_CPT_REV(cmplx16, sub_a16_cpt_rev, kmp_cmplx128_a16_t, -, 32c,
                        1) // __kmpc_atomic_cmplx16_sub_a16_cpt_rev
ATOMIC_CRITICAL_CPT_REV(cmplx16, div_a16_cpt_rev, kmp_cmplx128_a16_t, /, 32c,
                        1) // __kmpc_atomic_cmplx16_div_a16_cpt_rev
#endif
#endif

// Capture reverse for mixed type: RHS=float16
#if KMP_HAVE_QUAD

// Beginning of a definition (provides name, parameters, gebug trace)
//     TYPE_ID - operands type and size (fixed*, fixed*u for signed, unsigned
//     fixed)
//     OP_ID   - operation identifier (add, sub, mul, ...)
//     TYPE    - operands' type
// -------------------------------------------------------------------------
#define ATOMIC_CMPXCHG_CPT_REV_MIX(TYPE_ID, TYPE, OP_ID, BITS, OP, RTYPE_ID,   \
                                   RTYPE, LCK_ID, MASK, GOMP_FLAG)             \
  ATOMIC_BEGIN_CPT_MIX(TYPE_ID, OP_ID, TYPE, RTYPE_ID, RTYPE)                  \
  TYPE new_value;                                                              \
  OP_GOMP_CRITICAL_CPT_REV(OP, GOMP_FLAG)                                      \
  OP_CMPXCHG_CPT_REV(TYPE, BITS, OP)                                           \
  }

// -------------------------------------------------------------------------
#define ATOMIC_CRITICAL_CPT_REV_MIX(TYPE_ID, TYPE, OP_ID, OP, RTYPE_ID, RTYPE, \
                                    LCK_ID, GOMP_FLAG)                         \
  ATOMIC_BEGIN_CPT_MIX(TYPE_ID, OP_ID, TYPE, RTYPE_ID, RTYPE)                  \
  TYPE new_value;                                                              \
  OP_GOMP_CRITICAL_CPT_REV(OP, GOMP_FLAG) /* send assignment */                \
  OP_CRITICAL_CPT_REV(OP, LCK_ID) /* send assignment */                        \
  }

ATOMIC_CMPXCHG_CPT_REV_MIX(fixed1, char, sub_cpt_rev, 8, -, fp, _Quad, 1i, 0,
                           KMP_ARCH_X86) // __kmpc_atomic_fixed1_sub_cpt_rev_fp
ATOMIC_CMPXCHG_CPT_REV_MIX(fixed1u, uchar, sub_cpt_rev, 8, -, fp, _Quad, 1i, 0,
                           KMP_ARCH_X86) // __kmpc_atomic_fixed1u_sub_cpt_rev_fp
ATOMIC_CMPXCHG_CPT_REV_MIX(fixed1, char, div_cpt_rev, 8, /, fp, _Quad, 1i, 0,
                           KMP_ARCH_X86) // __kmpc_atomic_fixed1_div_cpt_rev_fp
ATOMIC_CMPXCHG_CPT_REV_MIX(fixed1u, uchar, div_cpt_rev, 8, /, fp, _Quad, 1i, 0,
                           KMP_ARCH_X86) // __kmpc_atomic_fixed1u_div_cpt_rev_fp

ATOMIC_CMPXCHG_CPT_REV_MIX(fixed2, short, sub_cpt_rev, 16, -, fp, _Quad, 2i, 1,
                           KMP_ARCH_X86) // __kmpc_atomic_fixed2_sub_cpt_rev_fp
ATOMIC_CMPXCHG_CPT_REV_MIX(fixed2u, ushort, sub_cpt_rev, 16, -, fp, _Quad, 2i,
                           1,
                           KMP_ARCH_X86) // __kmpc_atomic_fixed2u_sub_cpt_rev_fp
ATOMIC_CMPXCHG_CPT_REV_MIX(fixed2, short, div_cpt_rev, 16, /, fp, _Quad, 2i, 1,
                           KMP_ARCH_X86) // __kmpc_atomic_fixed2_div_cpt_rev_fp
ATOMIC_CMPXCHG_CPT_REV_MIX(fixed2u, ushort, div_cpt_rev, 16, /, fp, _Quad, 2i,
                           1,
                           KMP_ARCH_X86) // __kmpc_atomic_fixed2u_div_cpt_rev_fp

ATOMIC_CMPXCHG_CPT_REV_MIX(fixed4, kmp_int32, sub_cpt_rev, 32, -, fp, _Quad, 4i,
                           3, 0) // __kmpc_atomic_fixed4_sub_cpt_rev_fp
ATOMIC_CMPXCHG_CPT_REV_MIX(fixed4u, kmp_uint32, sub_cpt_rev, 32, -, fp, _Quad,
                           4i, 3, 0) // __kmpc_atomic_fixed4u_sub_cpt_rev_fp
ATOMIC_CMPXCHG_CPT_REV_MIX(fixed4, kmp_int32, div_cpt_rev, 32, /, fp, _Quad, 4i,
                           3, 0) // __kmpc_atomic_fixed4_div_cpt_rev_fp
ATOMIC_CMPXCHG_CPT_REV_MIX(fixed4u, kmp_uint32, div_cpt_rev, 32, /, fp, _Quad,
                           4i, 3, 0) // __kmpc_atomic_fixed4u_div_cpt_rev_fp

ATOMIC_CMPXCHG_CPT_REV_MIX(fixed8, kmp_int64, sub_cpt_rev, 64, -, fp, _Quad, 8i,
                           7,
                           KMP_ARCH_X86) // __kmpc_atomic_fixed8_sub_cpt_rev_fp
ATOMIC_CMPXCHG_CPT_REV_MIX(fixed8u, kmp_uint64, sub_cpt_rev, 64, -, fp, _Quad,
                           8i, 7,
                           KMP_ARCH_X86) // __kmpc_atomic_fixed8u_sub_cpt_rev_fp
ATOMIC_CMPXCHG_CPT_REV_MIX(fixed8, kmp_int64, div_cpt_rev, 64, /, fp, _Quad, 8i,
                           7,
                           KMP_ARCH_X86) // __kmpc_atomic_fixed8_div_cpt_rev_fp
ATOMIC_CMPXCHG_CPT_REV_MIX(fixed8u, kmp_uint64, div_cpt_rev, 64, /, fp, _Quad,
                           8i, 7,
                           KMP_ARCH_X86) // __kmpc_atomic_fixed8u_div_cpt_rev_fp

ATOMIC_CMPXCHG_CPT_REV_MIX(float4, kmp_real32, sub_cpt_rev, 32, -, fp, _Quad,
                           4r, 3,
                           KMP_ARCH_X86) // __kmpc_atomic_float4_sub_cpt_rev_fp
ATOMIC_CMPXCHG_CPT_REV_MIX(float4, kmp_real32, div_cpt_rev, 32, /, fp, _Quad,
                           4r, 3,
                           KMP_ARCH_X86) // __kmpc_atomic_float4_div_cpt_rev_fp

ATOMIC_CMPXCHG_CPT_REV_MIX(float8, kmp_real64, sub_cpt_rev, 64, -, fp, _Quad,
                           8r, 7,
                           KMP_ARCH_X86) // __kmpc_atomic_float8_sub_cpt_rev_fp
ATOMIC_CMPXCHG_CPT_REV_MIX(float8, kmp_real64, div_cpt_rev, 64, /, fp, _Quad,
                           8r, 7,
                           KMP_ARCH_X86) // __kmpc_atomic_float8_div_cpt_rev_fp

ATOMIC_CRITICAL_CPT_REV_MIX(float10, long double, sub_cpt_rev, -, fp, _Quad,
                            10r, 1) // __kmpc_atomic_float10_sub_cpt_rev_fp
ATOMIC_CRITICAL_CPT_REV_MIX(float10, long double, div_cpt_rev, /, fp, _Quad,
                            10r, 1) // __kmpc_atomic_float10_div_cpt_rev_fp

#endif // KMP_HAVE_QUAD

//   OpenMP 4.0 Capture-write (swap): {v = x; x = expr;}

#define ATOMIC_BEGIN_SWP(TYPE_ID, TYPE)                                        \
  TYPE __kmpc_atomic_##TYPE_ID##_swp(ident_t *id_ref, int gtid, TYPE *lhs,     \
                                     TYPE rhs) {                               \
    KMP_DEBUG_ASSERT(__kmp_init_serial);                                       \
    KA_TRACE(100, ("__kmpc_atomic_" #TYPE_ID "_swp: T#%d\n", gtid));

#define CRITICAL_SWP(LCK_ID)                                                   \
  __kmp_acquire_atomic_lock(&ATOMIC_LOCK##LCK_ID, gtid);                       \
                                                                               \
  old_value = (*lhs);                                                          \
  (*lhs) = rhs;                                                                \
                                                                               \
  __kmp_release_atomic_lock(&ATOMIC_LOCK##LCK_ID, gtid);                       \
  return old_value;

// ------------------------------------------------------------------------
#ifdef KMP_GOMP_COMPAT
#define GOMP_CRITICAL_SWP(FLAG)                                                \
  if ((FLAG) && (__kmp_atomic_mode == 2)) {                                    \
    KMP_CHECK_GTID;                                                            \
    CRITICAL_SWP(0);                                                           \
  }
#else
#define GOMP_CRITICAL_SWP(FLAG)
#endif /* KMP_GOMP_COMPAT */

#define ATOMIC_XCHG_SWP(TYPE_ID, TYPE, BITS, GOMP_FLAG)                        \
  ATOMIC_BEGIN_SWP(TYPE_ID, TYPE)                                              \
  TYPE old_value;                                                              \
  GOMP_CRITICAL_SWP(GOMP_FLAG)                                                 \
  old_value = KMP_XCHG_FIXED##BITS(lhs, rhs);                                  \
  return old_value;                                                            \
  }
// ------------------------------------------------------------------------
#define ATOMIC_XCHG_FLOAT_SWP(TYPE_ID, TYPE, BITS, GOMP_FLAG)                  \
  ATOMIC_BEGIN_SWP(TYPE_ID, TYPE)                                              \
  TYPE old_value;                                                              \
  GOMP_CRITICAL_SWP(GOMP_FLAG)                                                 \
  old_value = KMP_XCHG_REAL##BITS(lhs, rhs);                                   \
  return old_value;                                                            \
  }

// ------------------------------------------------------------------------
#define CMPXCHG_SWP(TYPE, BITS)                                                \
  {                                                                            \
    TYPE KMP_ATOMIC_VOLATILE temp_val;                                         \
    TYPE old_value, new_value;                                                 \
    temp_val = *lhs;                                                           \
    old_value = temp_val;                                                      \
    new_value = rhs;                                                           \
    while (!KMP_COMPARE_AND_STORE_ACQ##BITS(                                   \
        (kmp_int##BITS *)lhs, *VOLATILE_CAST(kmp_int##BITS *) & old_value,     \
        *VOLATILE_CAST(kmp_int##BITS *) & new_value)) {                        \
      KMP_CPU_PAUSE();                                                         \
                                                                               \
      temp_val = *lhs;                                                         \
      old_value = temp_val;                                                    \
      new_value = rhs;                                                         \
    }                                                                          \
    return old_value;                                                          \
  }

// -------------------------------------------------------------------------
#define ATOMIC_CMPXCHG_SWP(TYPE_ID, TYPE, BITS, GOMP_FLAG)                     \
  ATOMIC_BEGIN_SWP(TYPE_ID, TYPE)                                              \
  TYPE old_value;                                                              \
  GOMP_CRITICAL_SWP(GOMP_FLAG)                                                 \
  CMPXCHG_SWP(TYPE, BITS)                                                      \
  }

ATOMIC_XCHG_SWP(fixed1, kmp_int8, 8, KMP_ARCH_X86) // __kmpc_atomic_fixed1_swp
ATOMIC_XCHG_SWP(fixed2, kmp_int16, 16, KMP_ARCH_X86) // __kmpc_atomic_fixed2_swp
ATOMIC_XCHG_SWP(fixed4, kmp_int32, 32, KMP_ARCH_X86) // __kmpc_atomic_fixed4_swp

ATOMIC_XCHG_FLOAT_SWP(float4, kmp_real32, 32,
                      KMP_ARCH_X86) // __kmpc_atomic_float4_swp

#if (KMP_ARCH_X86)
ATOMIC_CMPXCHG_SWP(fixed8, kmp_int64, 64,
                   KMP_ARCH_X86) // __kmpc_atomic_fixed8_swp
ATOMIC_CMPXCHG_SWP(float8, kmp_real64, 64,
                   KMP_ARCH_X86) // __kmpc_atomic_float8_swp
#else
ATOMIC_XCHG_SWP(fixed8, kmp_int64, 64, KMP_ARCH_X86) // __kmpc_atomic_fixed8_swp
ATOMIC_XCHG_FLOAT_SWP(float8, kmp_real64, 64,
                      KMP_ARCH_X86) // __kmpc_atomic_float8_swp
#endif

// ------------------------------------------------------------------------
// Routines for Extended types: long double, _Quad, complex flavours (use
// critical section)
#define ATOMIC_CRITICAL_SWP(TYPE_ID, TYPE, LCK_ID, GOMP_FLAG)                  \
  ATOMIC_BEGIN_SWP(TYPE_ID, TYPE)                                              \
  TYPE old_value;                                                              \
  GOMP_CRITICAL_SWP(GOMP_FLAG)                                                 \
  CRITICAL_SWP(LCK_ID)                                                         \
  }

// ------------------------------------------------------------------------
// !!! TODO: check if we need to return void for cmplx4 routines
// Workaround for cmplx4. Regular routines with return value don't work
// on Win_32e. Let's return captured values through the additional parameter.

#define ATOMIC_BEGIN_SWP_WRK(TYPE_ID, TYPE)                                    \
  void __kmpc_atomic_##TYPE_ID##_swp(ident_t *id_ref, int gtid, TYPE *lhs,     \
                                     TYPE rhs, TYPE *out) {                    \
    KMP_DEBUG_ASSERT(__kmp_init_serial);                                       \
    KA_TRACE(100, ("__kmpc_atomic_" #TYPE_ID "_swp: T#%d\n", gtid));

#define CRITICAL_SWP_WRK(LCK_ID)                                               \
  __kmp_acquire_atomic_lock(&ATOMIC_LOCK##LCK_ID, gtid);                       \
                                                                               \
  tmp = (*lhs);                                                                \
  (*lhs) = (rhs);                                                              \
  (*out) = tmp;                                                                \
  __kmp_release_atomic_lock(&ATOMIC_LOCK##LCK_ID, gtid);                       \
  return;
// ------------------------------------------------------------------------

#ifdef KMP_GOMP_COMPAT
#define GOMP_CRITICAL_SWP_WRK(FLAG)                                            \
  if ((FLAG) && (__kmp_atomic_mode == 2)) {                                    \
    KMP_CHECK_GTID;                                                            \
    CRITICAL_SWP_WRK(0);                                                       \
  }
#else
#define GOMP_CRITICAL_SWP_WRK(FLAG)
#endif /* KMP_GOMP_COMPAT */
// ------------------------------------------------------------------------

#define ATOMIC_CRITICAL_SWP_WRK(TYPE_ID, TYPE, LCK_ID, GOMP_FLAG)              \
  ATOMIC_BEGIN_SWP_WRK(TYPE_ID, TYPE)                                          \
  TYPE tmp;                                                                    \
  GOMP_CRITICAL_SWP_WRK(GOMP_FLAG)                                             \
  CRITICAL_SWP_WRK(LCK_ID)                                                     \
  }
// The end of workaround for cmplx4

ATOMIC_CRITICAL_SWP(float10, long double, 10r, 1) // __kmpc_atomic_float10_swp
#if KMP_HAVE_QUAD
ATOMIC_CRITICAL_SWP(float16, QUAD_LEGACY, 16r, 1) // __kmpc_atomic_float16_swp
#endif
// cmplx4 routine to return void
ATOMIC_CRITICAL_SWP_WRK(cmplx4, kmp_cmplx32, 8c, 1) // __kmpc_atomic_cmplx4_swp

// ATOMIC_CRITICAL_SWP( cmplx4, kmp_cmplx32,  8c,   1 )           //
// __kmpc_atomic_cmplx4_swp

ATOMIC_CRITICAL_SWP(cmplx8, kmp_cmplx64, 16c, 1) // __kmpc_atomic_cmplx8_swp
ATOMIC_CRITICAL_SWP(cmplx10, kmp_cmplx80, 20c, 1) // __kmpc_atomic_cmplx10_swp
#if KMP_HAVE_QUAD
ATOMIC_CRITICAL_SWP(cmplx16, CPLX128_LEG, 32c, 1) // __kmpc_atomic_cmplx16_swp
#if (KMP_ARCH_X86)
ATOMIC_CRITICAL_SWP(float16_a16, Quad_a16_t, 16r,
                    1) // __kmpc_atomic_float16_a16_swp
ATOMIC_CRITICAL_SWP(cmplx16_a16, kmp_cmplx128_a16_t, 32c,
                    1) // __kmpc_atomic_cmplx16_a16_swp
#endif
#endif

// End of OpenMP 4.0 Capture

#endif // OMP_40_ENABLED

#endif // KMP_ARCH_X86 || KMP_ARCH_X86_64

#undef OP_CRITICAL

/* ------------------------------------------------------------------------ */
/* Generic atomic routines                                                  */

void __kmpc_atomic_1(ident_t *id_ref, int gtid, void *lhs, void *rhs,
                     void (*f)(void *, void *, void *)) {
  KMP_DEBUG_ASSERT(__kmp_init_serial);

  if (
#if KMP_ARCH_X86 && defined(KMP_GOMP_COMPAT)
      FALSE /* must use lock */
#else
      TRUE
#endif
      ) {
    kmp_int8 old_value, new_value;

    old_value = *(kmp_int8 *)lhs;
    (*f)(&new_value, &old_value, rhs);

    /* TODO: Should this be acquire or release? */
    while (!KMP_COMPARE_AND_STORE_ACQ8((kmp_int8 *)lhs, *(kmp_int8 *)&old_value,
                                       *(kmp_int8 *)&new_value)) {
      KMP_CPU_PAUSE();

      old_value = *(kmp_int8 *)lhs;
      (*f)(&new_value, &old_value, rhs);
    }

    return;
  } else {
// All 1-byte data is of integer data type.

#ifdef KMP_GOMP_COMPAT
    if (__kmp_atomic_mode == 2) {
      __kmp_acquire_atomic_lock(&__kmp_atomic_lock, gtid);
    } else
#endif /* KMP_GOMP_COMPAT */
      __kmp_acquire_atomic_lock(&__kmp_atomic_lock_1i, gtid);

    (*f)(lhs, lhs, rhs);

#ifdef KMP_GOMP_COMPAT
    if (__kmp_atomic_mode == 2) {
      __kmp_release_atomic_lock(&__kmp_atomic_lock, gtid);
    } else
#endif /* KMP_GOMP_COMPAT */
      __kmp_release_atomic_lock(&__kmp_atomic_lock_1i, gtid);
  }
}

void __kmpc_atomic_2(ident_t *id_ref, int gtid, void *lhs, void *rhs,
                     void (*f)(void *, void *, void *)) {
  if (
#if KMP_ARCH_X86 && defined(KMP_GOMP_COMPAT)
      FALSE /* must use lock */
#elif KMP_ARCH_X86 || KMP_ARCH_X86_64
      TRUE /* no alignment problems */
#else
      !((kmp_uintptr_t)lhs & 0x1) /* make sure address is 2-byte aligned */
#endif
      ) {
    kmp_int16 old_value, new_value;

    old_value = *(kmp_int16 *)lhs;
    (*f)(&new_value, &old_value, rhs);

    /* TODO: Should this be acquire or release? */
    while (!KMP_COMPARE_AND_STORE_ACQ16(
        (kmp_int16 *)lhs, *(kmp_int16 *)&old_value, *(kmp_int16 *)&new_value)) {
      KMP_CPU_PAUSE();

      old_value = *(kmp_int16 *)lhs;
      (*f)(&new_value, &old_value, rhs);
    }

    return;
  } else {
// All 2-byte data is of integer data type.

#ifdef KMP_GOMP_COMPAT
    if (__kmp_atomic_mode == 2) {
      __kmp_acquire_atomic_lock(&__kmp_atomic_lock, gtid);
    } else
#endif /* KMP_GOMP_COMPAT */
      __kmp_acquire_atomic_lock(&__kmp_atomic_lock_2i, gtid);

    (*f)(lhs, lhs, rhs);

#ifdef KMP_GOMP_COMPAT
    if (__kmp_atomic_mode == 2) {
      __kmp_release_atomic_lock(&__kmp_atomic_lock, gtid);
    } else
#endif /* KMP_GOMP_COMPAT */
      __kmp_release_atomic_lock(&__kmp_atomic_lock_2i, gtid);
  }
}

void __kmpc_atomic_4(ident_t *id_ref, int gtid, void *lhs, void *rhs,
                     void (*f)(void *, void *, void *)) {
  KMP_DEBUG_ASSERT(__kmp_init_serial);

  if (
// FIXME: On IA-32 architecture, gcc uses cmpxchg only for 4-byte ints.
// Gomp compatibility is broken if this routine is called for floats.
#if KMP_ARCH_X86 || KMP_ARCH_X86_64
      TRUE /* no alignment problems */
#else
      !((kmp_uintptr_t)lhs & 0x3) /* make sure address is 4-byte aligned */
#endif
      ) {
    kmp_int32 old_value, new_value;

    old_value = *(kmp_int32 *)lhs;
    (*f)(&new_value, &old_value, rhs);

    /* TODO: Should this be acquire or release? */
    while (!KMP_COMPARE_AND_STORE_ACQ32(
        (kmp_int32 *)lhs, *(kmp_int32 *)&old_value, *(kmp_int32 *)&new_value)) {
      KMP_CPU_PAUSE();

      old_value = *(kmp_int32 *)lhs;
      (*f)(&new_value, &old_value, rhs);
    }

    return;
  } else {
// Use __kmp_atomic_lock_4i for all 4-byte data,
// even if it isn't of integer data type.

#ifdef KMP_GOMP_COMPAT
    if (__kmp_atomic_mode == 2) {
      __kmp_acquire_atomic_lock(&__kmp_atomic_lock, gtid);
    } else
#endif /* KMP_GOMP_COMPAT */
      __kmp_acquire_atomic_lock(&__kmp_atomic_lock_4i, gtid);

    (*f)(lhs, lhs, rhs);

#ifdef KMP_GOMP_COMPAT
    if (__kmp_atomic_mode == 2) {
      __kmp_release_atomic_lock(&__kmp_atomic_lock, gtid);
    } else
#endif /* KMP_GOMP_COMPAT */
      __kmp_release_atomic_lock(&__kmp_atomic_lock_4i, gtid);
  }
}

void __kmpc_atomic_8(ident_t *id_ref, int gtid, void *lhs, void *rhs,
                     void (*f)(void *, void *, void *)) {
  KMP_DEBUG_ASSERT(__kmp_init_serial);
  if (

#if KMP_ARCH_X86 && defined(KMP_GOMP_COMPAT)
      FALSE /* must use lock */
#elif KMP_ARCH_X86 || KMP_ARCH_X86_64
      TRUE /* no alignment problems */
#else
      !((kmp_uintptr_t)lhs & 0x7) /* make sure address is 8-byte aligned */
#endif
      ) {
    kmp_int64 old_value, new_value;

    old_value = *(kmp_int64 *)lhs;
    (*f)(&new_value, &old_value, rhs);
    /* TODO: Should this be acquire or release? */
    while (!KMP_COMPARE_AND_STORE_ACQ64(
        (kmp_int64 *)lhs, *(kmp_int64 *)&old_value, *(kmp_int64 *)&new_value)) {
      KMP_CPU_PAUSE();

      old_value = *(kmp_int64 *)lhs;
      (*f)(&new_value, &old_value, rhs);
    }

    return;
  } else {
// Use __kmp_atomic_lock_8i for all 8-byte data,
// even if it isn't of integer data type.

#ifdef KMP_GOMP_COMPAT
    if (__kmp_atomic_mode == 2) {
      __kmp_acquire_atomic_lock(&__kmp_atomic_lock, gtid);
    } else
#endif /* KMP_GOMP_COMPAT */
      __kmp_acquire_atomic_lock(&__kmp_atomic_lock_8i, gtid);

    (*f)(lhs, lhs, rhs);

#ifdef KMP_GOMP_COMPAT
    if (__kmp_atomic_mode == 2) {
      __kmp_release_atomic_lock(&__kmp_atomic_lock, gtid);
    } else
#endif /* KMP_GOMP_COMPAT */
      __kmp_release_atomic_lock(&__kmp_atomic_lock_8i, gtid);
  }
}

void __kmpc_atomic_10(ident_t *id_ref, int gtid, void *lhs, void *rhs,
                      void (*f)(void *, void *, void *)) {
  KMP_DEBUG_ASSERT(__kmp_init_serial);

#ifdef KMP_GOMP_COMPAT
  if (__kmp_atomic_mode == 2) {
    __kmp_acquire_atomic_lock(&__kmp_atomic_lock, gtid);
  } else
#endif /* KMP_GOMP_COMPAT */
    __kmp_acquire_atomic_lock(&__kmp_atomic_lock_10r, gtid);

  (*f)(lhs, lhs, rhs);

#ifdef KMP_GOMP_COMPAT
  if (__kmp_atomic_mode == 2) {
    __kmp_release_atomic_lock(&__kmp_atomic_lock, gtid);
  } else
#endif /* KMP_GOMP_COMPAT */
    __kmp_release_atomic_lock(&__kmp_atomic_lock_10r, gtid);
}

void __kmpc_atomic_16(ident_t *id_ref, int gtid, void *lhs, void *rhs,
                      void (*f)(void *, void *, void *)) {
  KMP_DEBUG_ASSERT(__kmp_init_serial);

#ifdef KMP_GOMP_COMPAT
  if (__kmp_atomic_mode == 2) {
    __kmp_acquire_atomic_lock(&__kmp_atomic_lock, gtid);
  } else
#endif /* KMP_GOMP_COMPAT */
    __kmp_acquire_atomic_lock(&__kmp_atomic_lock_16c, gtid);

  (*f)(lhs, lhs, rhs);

#ifdef KMP_GOMP_COMPAT
  if (__kmp_atomic_mode == 2) {
    __kmp_release_atomic_lock(&__kmp_atomic_lock, gtid);
  } else
#endif /* KMP_GOMP_COMPAT */
    __kmp_release_atomic_lock(&__kmp_atomic_lock_16c, gtid);
}

void __kmpc_atomic_20(ident_t *id_ref, int gtid, void *lhs, void *rhs,
                      void (*f)(void *, void *, void *)) {
  KMP_DEBUG_ASSERT(__kmp_init_serial);

#ifdef KMP_GOMP_COMPAT
  if (__kmp_atomic_mode == 2) {
    __kmp_acquire_atomic_lock(&__kmp_atomic_lock, gtid);
  } else
#endif /* KMP_GOMP_COMPAT */
    __kmp_acquire_atomic_lock(&__kmp_atomic_lock_20c, gtid);

  (*f)(lhs, lhs, rhs);

#ifdef KMP_GOMP_COMPAT
  if (__kmp_atomic_mode == 2) {
    __kmp_release_atomic_lock(&__kmp_atomic_lock, gtid);
  } else
#endif /* KMP_GOMP_COMPAT */
    __kmp_release_atomic_lock(&__kmp_atomic_lock_20c, gtid);
}

void __kmpc_atomic_32(ident_t *id_ref, int gtid, void *lhs, void *rhs,
                      void (*f)(void *, void *, void *)) {
  KMP_DEBUG_ASSERT(__kmp_init_serial);

#ifdef KMP_GOMP_COMPAT
  if (__kmp_atomic_mode == 2) {
    __kmp_acquire_atomic_lock(&__kmp_atomic_lock, gtid);
  } else
#endif /* KMP_GOMP_COMPAT */
    __kmp_acquire_atomic_lock(&__kmp_atomic_lock_32c, gtid);

  (*f)(lhs, lhs, rhs);

#ifdef KMP_GOMP_COMPAT
  if (__kmp_atomic_mode == 2) {
    __kmp_release_atomic_lock(&__kmp_atomic_lock, gtid);
  } else
#endif /* KMP_GOMP_COMPAT */
    __kmp_release_atomic_lock(&__kmp_atomic_lock_32c, gtid);
}

// AC: same two routines as GOMP_atomic_start/end, but will be called by our
// compiler; duplicated in order to not use 3-party names in pure Intel code
// TODO: consider adding GTID parameter after consultation with Ernesto/Xinmin.
void __kmpc_atomic_start(void) {
  int gtid = __kmp_entry_gtid();
  KA_TRACE(20, ("__kmpc_atomic_start: T#%d\n", gtid));
  __kmp_acquire_atomic_lock(&__kmp_atomic_lock, gtid);
}

void __kmpc_atomic_end(void) {
  int gtid = __kmp_get_gtid();
  KA_TRACE(20, ("__kmpc_atomic_end: T#%d\n", gtid));
  __kmp_release_atomic_lock(&__kmp_atomic_lock, gtid);
}

/*!
@}
*/

// end of file
