/*
 * Basic four-word fraction declaration and manipulation.
 *
 * When adding quadword support for 32 bit machines, we need
 * to be a little careful as double multiply uses some of these
 * macros: (in op-2.h)
 * _FP_MUL_MEAT_2_wide() uses _FP_FRAC_DECL_4, _FP_FRAC_WORD_4,
 * _FP_FRAC_ADD_4, _FP_FRAC_SRS_4
 * _FP_MUL_MEAT_2_gmp() uses _FP_FRAC_SRS_4 (and should use
 * _FP_FRAC_DECL_4: it appears to be broken and is not used
 * anywhere anyway. )
 *
 * I've now fixed all the macros that were here from the sparc64 code.
 * [*none* of the shift macros were correct!] -- PMM 02/1998
 *
 * The only quadword stuff that remains to be coded is:
 * 1) the conversion to/from ints, which requires
 * that we check (in op-common.h) that the following do the right thing
 * for quadwords: _FP_TO_INT(Q,4,r,X,rsz,rsg), _FP_FROM_INT(Q,4,X,r,rs,rt)
 * 2) multiply, divide and sqrt, which require:
 * _FP_MUL_MEAT_4_*(R,X,Y), _FP_DIV_MEAT_4_*(R,X,Y), _FP_SQRT_MEAT_4(R,S,T,X,q),
 * This also needs _FP_MUL_MEAT_Q and _FP_DIV_MEAT_Q to be defined to
 * some suitable _FP_MUL_MEAT_4_* macros in sfp-machine.h.
 * [we're free to choose whatever FP_MUL_MEAT_4_* macros we need for
 * these; they are used nowhere else. ]
 */

#define _FP_FRAC_DECL_4(X)	_FP_W_TYPE X##_f[4]
#define _FP_FRAC_COPY_4(D,S)			\
  (D##_f[0] = S##_f[0], D##_f[1] = S##_f[1],	\
   D##_f[2] = S##_f[2], D##_f[3] = S##_f[3])
/* The _FP_FRAC_SET_n(X,I) macro is intended for use with another
 * macro such as _FP_ZEROFRAC_n which returns n comma separated values.
 * The result is that we get an expansion of __FP_FRAC_SET_n(X,I0,I1,I2,I3)
 * which just assigns the In values to the array X##_f[].
 * This is why the number of parameters doesn't appear to match
 * at first glance...      -- PMM
 */
#define _FP_FRAC_SET_4(X,I)	__FP_FRAC_SET_4(X, I)
#define _FP_FRAC_HIGH_4(X)	(X##_f[3])
#define _FP_FRAC_LOW_4(X)	(X##_f[0])
#define _FP_FRAC_WORD_4(X,w)	(X##_f[w])

#define _FP_FRAC_SLL_4(X,N)						\
  do {									\
    _FP_I_TYPE _up, _down, _skip, _i;					\
    _skip = (N) / _FP_W_TYPE_SIZE;					\
    _up = (N) % _FP_W_TYPE_SIZE;					\
    _down = _FP_W_TYPE_SIZE - _up;					\
    for (_i = 3; _i > _skip; --_i)					\
      X##_f[_i] = X##_f[_i-_skip] << _up | X##_f[_i-_skip-1] >> _down;	\
/* bugfixed: was X##_f[_i] <<= _up;  -- PMM 02/1998 */                  \
    X##_f[_i] = X##_f[0] << _up; 	                                \
    for (--_i; _i >= 0; --_i)						\
      X##_f[_i] = 0;							\
  } while (0)

/* This one was broken too */
#define _FP_FRAC_SRL_4(X,N)						\
  do {									\
    _FP_I_TYPE _up, _down, _skip, _i;					\
    _skip = (N) / _FP_W_TYPE_SIZE;					\
    _down = (N) % _FP_W_TYPE_SIZE;					\
    _up = _FP_W_TYPE_SIZE - _down;					\
    for (_i = 0; _i < 3-_skip; ++_i)					\
      X##_f[_i] = X##_f[_i+_skip] >> _down | X##_f[_i+_skip+1] << _up;	\
    X##_f[_i] = X##_f[3] >> _down;			         	\
    for (++_i; _i < 4; ++_i)						\
      X##_f[_i] = 0;							\
  } while (0)


/* Right shift with sticky-lsb.
 * What this actually means is that we do a standard right-shift,
 * but that if any of the bits that fall off the right hand side
 * were one then we always set the LSbit.
 */
#define _FP_FRAC_SRS_4(X,N,size)					\
  do {									\
    _FP_I_TYPE _up, _down, _skip, _i;					\
    _FP_W_TYPE _s;							\
    _skip = (N) / _FP_W_TYPE_SIZE;					\
    _down = (N) % _FP_W_TYPE_SIZE;					\
    _up = _FP_W_TYPE_SIZE - _down;					\
    for (_s = _i = 0; _i < _skip; ++_i)					\
      _s |= X##_f[_i];							\
    _s |= X##_f[_i] << _up;						\
/* s is now != 0 if we want to set the LSbit */                         \
    for (_i = 0; _i < 3-_skip; ++_i)					\
      X##_f[_i] = X##_f[_i+_skip] >> _down | X##_f[_i+_skip+1] << _up;	\
    X##_f[_i] = X##_f[3] >> _down;					\
    for (++_i; _i < 4; ++_i)						\
      X##_f[_i] = 0;							\
    /* don't fix the LSB until the very end when we're sure f[0] is stable */ \
    X##_f[0] |= (_s != 0);                                              \
  } while (0)

#define _FP_FRAC_ADD_4(R,X,Y)						\
  __FP_FRAC_ADD_4(R##_f[3], R##_f[2], R##_f[1], R##_f[0],		\
		  X##_f[3], X##_f[2], X##_f[1], X##_f[0],		\
		  Y##_f[3], Y##_f[2], Y##_f[1], Y##_f[0])

#define _FP_FRAC_SUB_4(R,X,Y)                                           \
  __FP_FRAC_SUB_4(R##_f[3], R##_f[2], R##_f[1], R##_f[0],		\
		  X##_f[3], X##_f[2], X##_f[1], X##_f[0],		\
		  Y##_f[3], Y##_f[2], Y##_f[1], Y##_f[0])

#define _FP_FRAC_ADDI_4(X,I)                                            \
  __FP_FRAC_ADDI_4(X##_f[3], X##_f[2], X##_f[1], X##_f[0], I)

#define _FP_ZEROFRAC_4  0,0,0,0
#define _FP_MINFRAC_4   0,0,0,1

#define _FP_FRAC_ZEROP_4(X)     ((X##_f[0] | X##_f[1] | X##_f[2] | X##_f[3]) == 0)
#define _FP_FRAC_NEGP_4(X)      ((_FP_WS_TYPE)X##_f[3] < 0)
#define _FP_FRAC_OVERP_4(fs,X)  (X##_f[0] & _FP_OVERFLOW_##fs)

#define _FP_FRAC_EQ_4(X,Y)                              \
 (X##_f[0] == Y##_f[0] && X##_f[1] == Y##_f[1]          \
  && X##_f[2] == Y##_f[2] && X##_f[3] == Y##_f[3])

#define _FP_FRAC_GT_4(X,Y)                              \
 (X##_f[3] > Y##_f[3] ||                                \
  (X##_f[3] == Y##_f[3] && (X##_f[2] > Y##_f[2] ||      \
   (X##_f[2] == Y##_f[2] && (X##_f[1] > Y##_f[1] ||     \
    (X##_f[1] == Y##_f[1] && X##_f[0] > Y##_f[0])       \
   ))                                                   \
  ))                                                    \
 )

#define _FP_FRAC_GE_4(X,Y)                              \
 (X##_f[3] > Y##_f[3] ||                                \
  (X##_f[3] == Y##_f[3] && (X##_f[2] > Y##_f[2] ||      \
   (X##_f[2] == Y##_f[2] && (X##_f[1] > Y##_f[1] ||     \
    (X##_f[1] == Y##_f[1] && X##_f[0] >= Y##_f[0])      \
   ))                                                   \
  ))                                                    \
 )


#define _FP_FRAC_CLZ_4(R,X)             \
  do {                                  \
    if (X##_f[3])                       \
    {                                   \
        __FP_CLZ(R,X##_f[3]);           \
    }                                   \
    else if (X##_f[2])                  \
    {                                   \
        __FP_CLZ(R,X##_f[2]);           \
        R += _FP_W_TYPE_SIZE;           \
    }                                   \
    else if (X##_f[1])                  \
    {                                   \
        __FP_CLZ(R,X##_f[2]);           \
        R += _FP_W_TYPE_SIZE*2;         \
    }                                   \
    else                                \
    {                                   \
        __FP_CLZ(R,X##_f[0]);           \
        R += _FP_W_TYPE_SIZE*3;         \
    }                                   \
  } while(0)


#define _FP_UNPACK_RAW_4(fs, X, val)                            \
  do {                                                          \
    union _FP_UNION_##fs _flo; _flo.flt = (val);        	\
    X##_f[0] = _flo.bits.frac0;                                 \
    X##_f[1] = _flo.bits.frac1;                                 \
    X##_f[2] = _flo.bits.frac2;                                 \
    X##_f[3] = _flo.bits.frac3;                                 \
    X##_e  = _flo.bits.exp;                                     \
    X##_s  = _flo.bits.sign;                                    \
  } while (0)

#define _FP_PACK_RAW_4(fs, val, X)                              \
  do {                                                          \
    union _FP_UNION_##fs _flo;					\
    _flo.bits.frac0 = X##_f[0];                                 \
    _flo.bits.frac1 = X##_f[1];                                 \
    _flo.bits.frac2 = X##_f[2];                                 \
    _flo.bits.frac3 = X##_f[3];                                 \
    _flo.bits.exp   = X##_e;                                    \
    _flo.bits.sign  = X##_s;                                    \
    (val) = _flo.flt;                                   	\
  } while (0)


/*
 * Internals
 */

#define __FP_FRAC_SET_4(X,I3,I2,I1,I0)					\
  (X##_f[3] = I3, X##_f[2] = I2, X##_f[1] = I1, X##_f[0] = I0)

#ifndef __FP_FRAC_ADD_4
#define __FP_FRAC_ADD_4(r3,r2,r1,r0,x3,x2,x1,x0,y3,y2,y1,y0)		\
  (r0 = x0 + y0,							\
   r1 = x1 + y1 + (r0 < x0),						\
   r2 = x2 + y2 + (r1 < x1),						\
   r3 = x3 + y3 + (r2 < x2))
#endif

#ifndef __FP_FRAC_SUB_4
#define __FP_FRAC_SUB_4(r3,r2,r1,r0,x3,x2,x1,x0,y3,y2,y1,y0)		\
  (r0 = x0 - y0,                                                        \
   r1 = x1 - y1 - (r0 > x0),                                            \
   r2 = x2 - y2 - (r1 > x1),                                            \
   r3 = x3 - y3 - (r2 > x2))
#endif

#ifndef __FP_FRAC_ADDI_4
/* I always wanted to be a lisp programmer :-> */
#define __FP_FRAC_ADDI_4(x3,x2,x1,x0,i)                                 \
  (x3 += ((x2 += ((x1 += ((x0 += i) < x0)) < x1) < x2)))
#endif

/* Convert FP values between word sizes. This appears to be more
 * complicated than I'd have expected it to be, so these might be
 * wrong... These macros are in any case somewhat bogus because they
 * use information about what various FRAC_n variables look like
 * internally [eg, that 2 word vars are X_f0 and x_f1]. But so do
 * the ones in op-2.h and op-1.h.
 */
#define _FP_FRAC_CONV_1_4(dfs, sfs, D, S)                               \
   do {                                                                 \
     _FP_FRAC_SRS_4(S, (_FP_WFRACBITS_##sfs - _FP_WFRACBITS_##dfs),     \
                        _FP_WFRACBITS_##sfs);                           \
     D##_f = S##_f[0];                                                   \
  } while (0)

#define _FP_FRAC_CONV_2_4(dfs, sfs, D, S)                               \
   do {                                                                 \
     _FP_FRAC_SRS_4(S, (_FP_WFRACBITS_##sfs - _FP_WFRACBITS_##dfs),     \
                        _FP_WFRACBITS_##sfs);                           \
     D##_f0 = S##_f[0];                                                  \
     D##_f1 = S##_f[1];                                                  \
  } while (0)

/* Assembly/disassembly for converting to/from integral types.
 * No shifting or overflow handled here.
 */
/* Put the FP value X into r, which is an integer of size rsize. */
#define _FP_FRAC_ASSEMBLE_4(r, X, rsize)                                \
  do {                                                                  \
    if (rsize <= _FP_W_TYPE_SIZE)                                       \
      r = X##_f[0];                                                     \
    else if (rsize <= 2*_FP_W_TYPE_SIZE)                                \
    {                                                                   \
      r = X##_f[1];                                                     \
      r <<= _FP_W_TYPE_SIZE;                                            \
      r += X##_f[0];                                                    \
    }                                                                   \
    else                                                                \
    {                                                                   \
      /* I'm feeling lazy so we deal with int == 3words (implausible)*/ \
      /* and int == 4words as a single case.                         */ \
      r = X##_f[3];                                                     \
      r <<= _FP_W_TYPE_SIZE;                                            \
      r += X##_f[2];                                                    \
      r <<= _FP_W_TYPE_SIZE;                                            \
      r += X##_f[1];                                                    \
      r <<= _FP_W_TYPE_SIZE;                                            \
      r += X##_f[0];                                                    \
    }                                                                   \
  } while (0)

/* "No disassemble Number Five!" */
/* move an integer of size rsize into X's fractional part. We rely on
 * the _f[] array consisting of words of size _FP_W_TYPE_SIZE to avoid
 * having to mask the values we store into it.
 */
#define _FP_FRAC_DISASSEMBLE_4(X, r, rsize)                             \
  do {                                                                  \
    X##_f[0] = r;                                                       \
    X##_f[1] = (rsize <= _FP_W_TYPE_SIZE ? 0 : r >> _FP_W_TYPE_SIZE);   \
    X##_f[2] = (rsize <= 2*_FP_W_TYPE_SIZE ? 0 : r >> 2*_FP_W_TYPE_SIZE); \
    X##_f[3] = (rsize <= 3*_FP_W_TYPE_SIZE ? 0 : r >> 3*_FP_W_TYPE_SIZE); \
  } while (0)

#define _FP_FRAC_CONV_4_1(dfs, sfs, D, S)                               \
   do {                                                                 \
     D##_f[0] = S##_f;                                                  \
     D##_f[1] = D##_f[2] = D##_f[3] = 0;                                \
     _FP_FRAC_SLL_4(D, (_FP_WFRACBITS_##dfs - _FP_WFRACBITS_##sfs));    \
   } while (0)

#define _FP_FRAC_CONV_4_2(dfs, sfs, D, S)                               \
   do {                                                                 \
     D##_f[0] = S##_f0;                                                 \
     D##_f[1] = S##_f1;                                                 \
     D##_f[2] = D##_f[3] = 0;                                           \
     _FP_FRAC_SLL_4(D, (_FP_WFRACBITS_##dfs - _FP_WFRACBITS_##sfs));    \
   } while (0)

/* FIXME! This has to be written */
#define _FP_SQRT_MEAT_4(R, S, T, X, q)
