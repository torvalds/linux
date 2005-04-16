/*
 * Basic two-word fraction declaration and manipulation.
 */

#define _FP_FRAC_DECL_2(X)	_FP_W_TYPE X##_f0, X##_f1
#define _FP_FRAC_COPY_2(D,S)	(D##_f0 = S##_f0, D##_f1 = S##_f1)
#define _FP_FRAC_SET_2(X,I)	__FP_FRAC_SET_2(X, I)
#define _FP_FRAC_HIGH_2(X)	(X##_f1)
#define _FP_FRAC_LOW_2(X)	(X##_f0)
#define _FP_FRAC_WORD_2(X,w)	(X##_f##w)

#define _FP_FRAC_SLL_2(X,N)						\
  do {									\
    if ((N) < _FP_W_TYPE_SIZE)						\
      {									\
        if (__builtin_constant_p(N) && (N) == 1) 			\
          {								\
            X##_f1 = X##_f1 + X##_f1 + (((_FP_WS_TYPE)(X##_f0)) < 0);	\
            X##_f0 += X##_f0;						\
          }								\
        else								\
          {								\
	    X##_f1 = X##_f1 << (N) | X##_f0 >> (_FP_W_TYPE_SIZE - (N));	\
	    X##_f0 <<= (N);						\
	  }								\
      }									\
    else								\
      {									\
	X##_f1 = X##_f0 << ((N) - _FP_W_TYPE_SIZE);			\
	X##_f0 = 0;							\
      }									\
  } while (0)

#define _FP_FRAC_SRL_2(X,N)						\
  do {									\
    if ((N) < _FP_W_TYPE_SIZE)						\
      {									\
	X##_f0 = X##_f0 >> (N) | X##_f1 << (_FP_W_TYPE_SIZE - (N));	\
	X##_f1 >>= (N);							\
      }									\
    else								\
      {									\
	X##_f0 = X##_f1 >> ((N) - _FP_W_TYPE_SIZE);			\
	X##_f1 = 0;							\
      }									\
  } while (0)

/* Right shift with sticky-lsb.  */
#define _FP_FRAC_SRS_2(X,N,sz)						\
  do {									\
    if ((N) < _FP_W_TYPE_SIZE)						\
      {									\
	X##_f0 = (X##_f1 << (_FP_W_TYPE_SIZE - (N)) | X##_f0 >> (N) |	\
		  (__builtin_constant_p(N) && (N) == 1			\
		   ? X##_f0 & 1						\
		   : (X##_f0 << (_FP_W_TYPE_SIZE - (N))) != 0));	\
	X##_f1 >>= (N);							\
      }									\
    else								\
      {									\
	X##_f0 = (X##_f1 >> ((N) - _FP_W_TYPE_SIZE) |			\
	          (((X##_f1 << (sz - (N))) | X##_f0) != 0));		\
	X##_f1 = 0;							\
      }									\
  } while (0)

#define _FP_FRAC_ADDI_2(X,I) \
  __FP_FRAC_ADDI_2(X##_f1, X##_f0, I)

#define _FP_FRAC_ADD_2(R,X,Y) \
  __FP_FRAC_ADD_2(R##_f1, R##_f0, X##_f1, X##_f0, Y##_f1, Y##_f0)

#define _FP_FRAC_SUB_2(R,X,Y) \
  __FP_FRAC_SUB_2(R##_f1, R##_f0, X##_f1, X##_f0, Y##_f1, Y##_f0)

#define _FP_FRAC_CLZ_2(R,X)	\
  do {				\
    if (X##_f1)			\
      __FP_CLZ(R,X##_f1);	\
    else 			\
    {				\
      __FP_CLZ(R,X##_f0);	\
      R += _FP_W_TYPE_SIZE;	\
    }				\
  } while(0)

/* Predicates */
#define _FP_FRAC_NEGP_2(X)	((_FP_WS_TYPE)X##_f1 < 0)
#define _FP_FRAC_ZEROP_2(X)	((X##_f1 | X##_f0) == 0)
#define _FP_FRAC_OVERP_2(fs,X)	(X##_f1 & _FP_OVERFLOW_##fs)
#define _FP_FRAC_EQ_2(X, Y)	(X##_f1 == Y##_f1 && X##_f0 == Y##_f0)
#define _FP_FRAC_GT_2(X, Y)	\
  ((X##_f1 > Y##_f1) || (X##_f1 == Y##_f1 && X##_f0 > Y##_f0))
#define _FP_FRAC_GE_2(X, Y)	\
  ((X##_f1 > Y##_f1) || (X##_f1 == Y##_f1 && X##_f0 >= Y##_f0))

#define _FP_ZEROFRAC_2		0, 0
#define _FP_MINFRAC_2		0, 1

/*
 * Internals
 */

#define __FP_FRAC_SET_2(X,I1,I0)	(X##_f0 = I0, X##_f1 = I1)

#define __FP_CLZ_2(R, xh, xl)	\
  do {				\
    if (xh)			\
      __FP_CLZ(R,xl);		\
    else 			\
    {				\
      __FP_CLZ(R,xl);		\
      R += _FP_W_TYPE_SIZE;	\
    }				\
  } while(0)

#if 0

#ifndef __FP_FRAC_ADDI_2
#define __FP_FRAC_ADDI_2(xh, xl, i) \
  (xh += ((xl += i) < i))
#endif
#ifndef __FP_FRAC_ADD_2
#define __FP_FRAC_ADD_2(rh, rl, xh, xl, yh, yl) \
  (rh = xh + yh + ((rl = xl + yl) < xl))
#endif
#ifndef __FP_FRAC_SUB_2
#define __FP_FRAC_SUB_2(rh, rl, xh, xl, yh, yl) \
  (rh = xh - yh - ((rl = xl - yl) > xl))
#endif

#else

#undef __FP_FRAC_ADDI_2
#define __FP_FRAC_ADDI_2(xh, xl, i)	add_ssaaaa(xh, xl, xh, xl, 0, i)
#undef __FP_FRAC_ADD_2
#define __FP_FRAC_ADD_2			add_ssaaaa
#undef __FP_FRAC_SUB_2
#define __FP_FRAC_SUB_2			sub_ddmmss

#endif

/*
 * Unpack the raw bits of a native fp value.  Do not classify or
 * normalize the data.
 */

#define _FP_UNPACK_RAW_2(fs, X, val)			\
  do {							\
    union _FP_UNION_##fs _flo; _flo.flt = (val);	\
							\
    X##_f0 = _flo.bits.frac0;				\
    X##_f1 = _flo.bits.frac1;				\
    X##_e  = _flo.bits.exp;				\
    X##_s  = _flo.bits.sign;				\
  } while (0)


/*
 * Repack the raw bits of a native fp value.
 */

#define _FP_PACK_RAW_2(fs, val, X)			\
  do {							\
    union _FP_UNION_##fs _flo;				\
							\
    _flo.bits.frac0 = X##_f0;				\
    _flo.bits.frac1 = X##_f1;				\
    _flo.bits.exp   = X##_e;				\
    _flo.bits.sign  = X##_s;				\
							\
    (val) = _flo.flt;					\
  } while (0)


/*
 * Multiplication algorithms:
 */

/* Given a 1W * 1W => 2W primitive, do the extended multiplication.  */

#define _FP_MUL_MEAT_2_wide(fs, R, X, Y, doit)				\
  do {									\
    _FP_FRAC_DECL_4(_z); _FP_FRAC_DECL_2(_b); _FP_FRAC_DECL_2(_c);	\
									\
    doit(_FP_FRAC_WORD_4(_z,1), _FP_FRAC_WORD_4(_z,0), X##_f0, Y##_f0); \
    doit(_b_f1, _b_f0, X##_f0, Y##_f1);					\
    doit(_c_f1, _c_f0, X##_f1, Y##_f0);					\
    doit(_FP_FRAC_WORD_4(_z,3), _FP_FRAC_WORD_4(_z,2), X##_f1, Y##_f1); \
									\
    __FP_FRAC_ADD_4(_FP_FRAC_WORD_4(_z,3),_FP_FRAC_WORD_4(_z,2),	\
		    _FP_FRAC_WORD_4(_z,1),_FP_FRAC_WORD_4(_z,0),	\
		    0, _b_f1, _b_f0, 0,					\
		    _FP_FRAC_WORD_4(_z,3),_FP_FRAC_WORD_4(_z,2),	\
		    _FP_FRAC_WORD_4(_z,1),_FP_FRAC_WORD_4(_z,0));	\
    __FP_FRAC_ADD_4(_FP_FRAC_WORD_4(_z,3),_FP_FRAC_WORD_4(_z,2),	\
		    _FP_FRAC_WORD_4(_z,1),_FP_FRAC_WORD_4(_z,0),	\
		    0, _c_f1, _c_f0, 0,					\
		    _FP_FRAC_WORD_4(_z,3),_FP_FRAC_WORD_4(_z,2),	\
		    _FP_FRAC_WORD_4(_z,1),_FP_FRAC_WORD_4(_z,0));	\
									\
    /* Normalize since we know where the msb of the multiplicands	\
       were (bit B), we know that the msb of the of the product is	\
       at either 2B or 2B-1.  */					\
    _FP_FRAC_SRS_4(_z, _FP_WFRACBITS_##fs-1, 2*_FP_WFRACBITS_##fs);	\
    R##_f0 = _FP_FRAC_WORD_4(_z,0);					\
    R##_f1 = _FP_FRAC_WORD_4(_z,1);					\
  } while (0)

/* This next macro appears to be totally broken. Fortunately nowhere
 * seems to use it :-> The problem is that we define _z[4] but
 * then use it in _FP_FRAC_SRS_4, which will attempt to access
 * _z_f[n] which will cause an error. The fix probably involves
 * declaring it with _FP_FRAC_DECL_4, see previous macro. -- PMM 02/1998
 */
#define _FP_MUL_MEAT_2_gmp(fs, R, X, Y)					\
  do {									\
    _FP_W_TYPE _x[2], _y[2], _z[4];					\
    _x[0] = X##_f0; _x[1] = X##_f1;					\
    _y[0] = Y##_f0; _y[1] = Y##_f1;					\
									\
    mpn_mul_n(_z, _x, _y, 2);						\
									\
    /* Normalize since we know where the msb of the multiplicands	\
       were (bit B), we know that the msb of the of the product is	\
       at either 2B or 2B-1.  */					\
    _FP_FRAC_SRS_4(_z, _FP_WFRACBITS##_fs-1, 2*_FP_WFRACBITS_##fs);	\
    R##_f0 = _z[0];							\
    R##_f1 = _z[1];							\
  } while (0)


/*
 * Division algorithms:
 * This seems to be giving me difficulties -- PMM
 * Look, NetBSD seems to be able to comment algorithms. Can't you?
 * I've thrown printks at the problem.
 * This now appears to work, but I still don't really know why.
 * Also, I don't think the result is properly normalised...
 */

#define _FP_DIV_MEAT_2_udiv_64(fs, R, X, Y)				\
  do {									\
    extern void _fp_udivmodti4(_FP_W_TYPE q[2], _FP_W_TYPE r[2],	\
			       _FP_W_TYPE n1, _FP_W_TYPE n0,		\
			       _FP_W_TYPE d1, _FP_W_TYPE d0);		\
    _FP_W_TYPE _n_f3, _n_f2, _n_f1, _n_f0, _r_f1, _r_f0;		\
    _FP_W_TYPE _q_f1, _q_f0, _m_f1, _m_f0;				\
    _FP_W_TYPE _rmem[2], _qmem[2];					\
    /* I think this check is to ensure that the result is normalised.   \
     * Assuming X,Y normalised (ie in [1.0,2.0)) X/Y will be in         \
     * [0.5,2.0). Furthermore, it will be less than 1.0 iff X < Y.      \
     * In this case we tweak things. (this is based on comments in      \
     * the NetBSD FPU emulation code. )                                 \
     * We know X,Y are normalised because we ensure this as part of     \
     * the unpacking process. -- PMM                                    \
     */									\
    if (_FP_FRAC_GT_2(X, Y))						\
      {									\
/*	R##_e++; */							\
	_n_f3 = X##_f1 >> 1;						\
	_n_f2 = X##_f1 << (_FP_W_TYPE_SIZE - 1) | X##_f0 >> 1;		\
	_n_f1 = X##_f0 << (_FP_W_TYPE_SIZE - 1);			\
	_n_f0 = 0;							\
      }									\
    else								\
      {									\
	R##_e--;							\
	_n_f3 = X##_f1;							\
	_n_f2 = X##_f0;							\
	_n_f1 = _n_f0 = 0;						\
      }									\
									\
    /* Normalize, i.e. make the most significant bit of the 		\
       denominator set.  CHANGED: - 1 to nothing -- PMM */		\
    _FP_FRAC_SLL_2(Y, _FP_WFRACXBITS_##fs /* -1 */);			\
									\
    /* Do the 256/128 bit division given the 128-bit _fp_udivmodtf4 	\
       primitive snagged from libgcc2.c.  */				\
									\
    _fp_udivmodti4(_qmem, _rmem, _n_f3, _n_f2, 0, Y##_f1);		\
    _q_f1 = _qmem[0];							\
    umul_ppmm(_m_f1, _m_f0, _q_f1, Y##_f0);				\
    _r_f1 = _rmem[0];							\
    _r_f0 = _n_f1;							\
    if (_FP_FRAC_GT_2(_m, _r))						\
      {									\
	_q_f1--;							\
	_FP_FRAC_ADD_2(_r, _r, Y);					\
	if (_FP_FRAC_GE_2(_r, Y) && _FP_FRAC_GT_2(_m, _r))		\
	  {								\
	    _q_f1--;							\
	    _FP_FRAC_ADD_2(_r, _r, Y);					\
	  }								\
      }									\
    _FP_FRAC_SUB_2(_r, _r, _m);						\
									\
    _fp_udivmodti4(_qmem, _rmem, _r_f1, _r_f0, 0, Y##_f1);		\
    _q_f0 = _qmem[0];							\
    umul_ppmm(_m_f1, _m_f0, _q_f0, Y##_f0);				\
    _r_f1 = _rmem[0];							\
    _r_f0 = _n_f0;							\
    if (_FP_FRAC_GT_2(_m, _r))						\
      {									\
	_q_f0--;							\
	_FP_FRAC_ADD_2(_r, _r, Y);					\
	if (_FP_FRAC_GE_2(_r, Y) && _FP_FRAC_GT_2(_m, _r))		\
	  {								\
	    _q_f0--;							\
	    _FP_FRAC_ADD_2(_r, _r, Y);					\
	  }								\
      }									\
    _FP_FRAC_SUB_2(_r, _r, _m);						\
									\
    R##_f1 = _q_f1;							\
    R##_f0 = _q_f0 | ((_r_f1 | _r_f0) != 0);				\
    /* adjust so answer is normalized again. I'm not sure what the 	\
     * final sz param should be. In practice it's never used since      \
     * N is 1 which is always going to be < _FP_W_TYPE_SIZE...		\
     */									\
    /* _FP_FRAC_SRS_2(R,1,_FP_WFRACBITS_##fs);	*/			\
  } while (0)


#define _FP_DIV_MEAT_2_gmp(fs, R, X, Y)					\
  do {									\
    _FP_W_TYPE _x[4], _y[2], _z[4];					\
    _y[0] = Y##_f0; _y[1] = Y##_f1;					\
    _x[0] = _x[3] = 0;							\
    if (_FP_FRAC_GT_2(X, Y))						\
      {									\
	R##_e++;							\
	_x[1] = (X##_f0 << (_FP_WFRACBITS-1 - _FP_W_TYPE_SIZE) |	\
		 X##_f1 >> (_FP_W_TYPE_SIZE -				\
			    (_FP_WFRACBITS-1 - _FP_W_TYPE_SIZE)));	\
	_x[2] = X##_f1 << (_FP_WFRACBITS-1 - _FP_W_TYPE_SIZE);		\
      }									\
    else								\
      {									\
	_x[1] = (X##_f0 << (_FP_WFRACBITS - _FP_W_TYPE_SIZE) |		\
		 X##_f1 >> (_FP_W_TYPE_SIZE -				\
			    (_FP_WFRACBITS - _FP_W_TYPE_SIZE)));	\
	_x[2] = X##_f1 << (_FP_WFRACBITS - _FP_W_TYPE_SIZE);		\
      }									\
									\
    (void) mpn_divrem (_z, 0, _x, 4, _y, 2);				\
    R##_f1 = _z[1];							\
    R##_f0 = _z[0] | ((_x[0] | _x[1]) != 0);				\
  } while (0)


/*
 * Square root algorithms:
 * We have just one right now, maybe Newton approximation
 * should be added for those machines where division is fast.
 */

#define _FP_SQRT_MEAT_2(R, S, T, X, q)			\
  do {							\
    while (q)						\
      {							\
        T##_f1 = S##_f1 + q;				\
        if (T##_f1 <= X##_f1)				\
          {						\
            S##_f1 = T##_f1 + q;			\
            X##_f1 -= T##_f1;				\
            R##_f1 += q;				\
          }						\
        _FP_FRAC_SLL_2(X, 1);				\
        q >>= 1;					\
      }							\
    q = (_FP_W_TYPE)1 << (_FP_W_TYPE_SIZE - 1);		\
    while (q)						\
      {							\
        T##_f0 = S##_f0 + q;				\
        T##_f1 = S##_f1;				\
        if (T##_f1 < X##_f1 || 				\
            (T##_f1 == X##_f1 && T##_f0 < X##_f0))	\
          {						\
            S##_f0 = T##_f0 + q;			\
            if (((_FP_WS_TYPE)T##_f0) < 0 &&		\
                ((_FP_WS_TYPE)S##_f0) >= 0)		\
              S##_f1++;					\
            _FP_FRAC_SUB_2(X, X, T);			\
            R##_f0 += q;				\
          }						\
        _FP_FRAC_SLL_2(X, 1);				\
        q >>= 1;					\
      }							\
  } while (0)


/*
 * Assembly/disassembly for converting to/from integral types.
 * No shifting or overflow handled here.
 */

#define _FP_FRAC_ASSEMBLE_2(r, X, rsize)	\
  do {						\
    if (rsize <= _FP_W_TYPE_SIZE)		\
      r = X##_f0;				\
    else					\
      {						\
	r = X##_f1;				\
	r <<= _FP_W_TYPE_SIZE;			\
	r += X##_f0;				\
      }						\
  } while (0)

#define _FP_FRAC_DISASSEMBLE_2(X, r, rsize)				\
  do {									\
    X##_f0 = r;								\
    X##_f1 = (rsize <= _FP_W_TYPE_SIZE ? 0 : r >> _FP_W_TYPE_SIZE);	\
  } while (0)

/*
 * Convert FP values between word sizes
 */

#define _FP_FRAC_CONV_1_2(dfs, sfs, D, S)				\
  do {									\
    _FP_FRAC_SRS_2(S, (_FP_WFRACBITS_##sfs - _FP_WFRACBITS_##dfs),	\
		   _FP_WFRACBITS_##sfs);				\
    D##_f = S##_f0;							\
  } while (0)

#define _FP_FRAC_CONV_2_1(dfs, sfs, D, S)				\
  do {									\
    D##_f0 = S##_f;							\
    D##_f1 = 0;								\
    _FP_FRAC_SLL_2(D, (_FP_WFRACBITS_##dfs - _FP_WFRACBITS_##sfs));	\
  } while (0)

