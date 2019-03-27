/* Software floating-point emulation. Common operations.
   Copyright (C) 1997,1998,1999,2006,2007 Free Software Foundation, Inc.
   This file is part of the GNU C Library.
   Contributed by Richard Henderson (rth@cygnus.com),
		  Jakub Jelinek (jj@ultra.linux.cz),
		  David S. Miller (davem@redhat.com) and
		  Peter Maydell (pmaydell@chiark.greenend.org.uk).

   The GNU C Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.

   In addition to the permissions in the GNU Lesser General Public
   License, the Free Software Foundation gives you unlimited
   permission to link the compiled version of this file into
   combinations with other programs, and to distribute those
   combinations without any restriction coming from the use of this
   file.  (The Lesser General Public License restrictions do apply in
   other respects; for example, they cover modification of the file,
   and distribution when not linked into a combine executable.)

   The GNU C Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with the GNU C Library; if not, write to the Free
   Software Foundation, 51 Franklin Street, Fifth Floor, Boston,
   MA 02110-1301, USA.  */

#define _FP_DECL(wc, X)						\
  _FP_I_TYPE X##_c __attribute__((unused)), X##_s, X##_e;	\
  _FP_FRAC_DECL_##wc(X)

/*
 * Finish truely unpacking a native fp value by classifying the kind
 * of fp value and normalizing both the exponent and the fraction.
 */

#define _FP_UNPACK_CANONICAL(fs, wc, X)					\
do {									\
  switch (X##_e)							\
  {									\
  default:								\
    _FP_FRAC_HIGH_RAW_##fs(X) |= _FP_IMPLBIT_##fs;			\
    _FP_FRAC_SLL_##wc(X, _FP_WORKBITS);					\
    X##_e -= _FP_EXPBIAS_##fs;						\
    X##_c = FP_CLS_NORMAL;						\
    break;								\
									\
  case 0:								\
    if (_FP_FRAC_ZEROP_##wc(X))						\
      X##_c = FP_CLS_ZERO;						\
    else								\
      {									\
	/* a denormalized number */					\
	_FP_I_TYPE _shift;						\
	_FP_FRAC_CLZ_##wc(_shift, X);					\
	_shift -= _FP_FRACXBITS_##fs;					\
	_FP_FRAC_SLL_##wc(X, (_shift+_FP_WORKBITS));			\
	X##_e -= _FP_EXPBIAS_##fs - 1 + _shift;				\
	X##_c = FP_CLS_NORMAL;						\
	FP_SET_EXCEPTION(FP_EX_DENORM);					\
      }									\
    break;								\
									\
  case _FP_EXPMAX_##fs:							\
    if (_FP_FRAC_ZEROP_##wc(X))						\
      X##_c = FP_CLS_INF;						\
    else								\
      {									\
	X##_c = FP_CLS_NAN;						\
	/* Check for signaling NaN */					\
	if (!(_FP_FRAC_HIGH_RAW_##fs(X) & _FP_QNANBIT_##fs))		\
	  FP_SET_EXCEPTION(FP_EX_INVALID);				\
      }									\
    break;								\
  }									\
} while (0)

/* Finish unpacking an fp value in semi-raw mode: the mantissa is
   shifted by _FP_WORKBITS but the implicit MSB is not inserted and
   other classification is not done.  */
#define _FP_UNPACK_SEMIRAW(fs, wc, X)	_FP_FRAC_SLL_##wc(X, _FP_WORKBITS)

/* A semi-raw value has overflowed to infinity.  Adjust the mantissa
   and exponent appropriately.  */
#define _FP_OVERFLOW_SEMIRAW(fs, wc, X)			\
do {							\
  if (FP_ROUNDMODE == FP_RND_NEAREST			\
      || (FP_ROUNDMODE == FP_RND_PINF && !X##_s)	\
      || (FP_ROUNDMODE == FP_RND_MINF && X##_s))	\
    {							\
      X##_e = _FP_EXPMAX_##fs;				\
      _FP_FRAC_SET_##wc(X, _FP_ZEROFRAC_##wc);		\
    }							\
  else							\
    {							\
      X##_e = _FP_EXPMAX_##fs - 1;			\
      _FP_FRAC_SET_##wc(X, _FP_MAXFRAC_##wc);		\
    }							\
    FP_SET_EXCEPTION(FP_EX_INEXACT);			\
    FP_SET_EXCEPTION(FP_EX_OVERFLOW);			\
} while (0)

/* Check for a semi-raw value being a signaling NaN and raise the
   invalid exception if so.  */
#define _FP_CHECK_SIGNAN_SEMIRAW(fs, wc, X)			\
do {								\
  if (X##_e == _FP_EXPMAX_##fs					\
      && !_FP_FRAC_ZEROP_##wc(X)				\
      && !(_FP_FRAC_HIGH_##fs(X) & _FP_QNANBIT_SH_##fs))	\
    FP_SET_EXCEPTION(FP_EX_INVALID);				\
} while (0)

/* Choose a NaN result from an operation on two semi-raw NaN
   values.  */
#define _FP_CHOOSENAN_SEMIRAW(fs, wc, R, X, Y, OP)			\
do {									\
  /* _FP_CHOOSENAN expects raw values, so shift as required.  */	\
  _FP_FRAC_SRL_##wc(X, _FP_WORKBITS);					\
  _FP_FRAC_SRL_##wc(Y, _FP_WORKBITS);					\
  _FP_CHOOSENAN(fs, wc, R, X, Y, OP);					\
  _FP_FRAC_SLL_##wc(R, _FP_WORKBITS);					\
} while (0)

/* Test whether a biased exponent is normal (not zero or maximum).  */
#define _FP_EXP_NORMAL(fs, wc, X)	(((X##_e + 1) & _FP_EXPMAX_##fs) > 1)

/* Prepare to pack an fp value in semi-raw mode: the mantissa is
   rounded and shifted right, with the rounding possibly increasing
   the exponent (including changing a finite value to infinity).  */
#define _FP_PACK_SEMIRAW(fs, wc, X)				\
do {								\
  _FP_ROUND(wc, X);						\
  if (_FP_FRAC_HIGH_##fs(X)					\
      & (_FP_OVERFLOW_##fs >> 1))				\
    {								\
      _FP_FRAC_HIGH_##fs(X) &= ~(_FP_OVERFLOW_##fs >> 1);	\
      X##_e++;							\
      if (X##_e == _FP_EXPMAX_##fs)				\
	_FP_OVERFLOW_SEMIRAW(fs, wc, X);			\
    }								\
  _FP_FRAC_SRL_##wc(X, _FP_WORKBITS);				\
  if (!_FP_EXP_NORMAL(fs, wc, X) && !_FP_FRAC_ZEROP_##wc(X))	\
    {								\
      if (X##_e == 0)						\
	FP_SET_EXCEPTION(FP_EX_UNDERFLOW);			\
      else							\
	{							\
	  if (!_FP_KEEPNANFRACP)				\
	    {							\
	      _FP_FRAC_SET_##wc(X, _FP_NANFRAC_##fs);		\
	      X##_s = _FP_NANSIGN_##fs;				\
	    }							\
	  else							\
	    _FP_FRAC_HIGH_RAW_##fs(X) |= _FP_QNANBIT_##fs;	\
	}							\
    }								\
} while (0)

/*
 * Before packing the bits back into the native fp result, take care
 * of such mundane things as rounding and overflow.  Also, for some
 * kinds of fp values, the original parts may not have been fully
 * extracted -- but that is ok, we can regenerate them now.
 */

#define _FP_PACK_CANONICAL(fs, wc, X)				\
do {								\
  switch (X##_c)						\
  {								\
  case FP_CLS_NORMAL:						\
    X##_e += _FP_EXPBIAS_##fs;					\
    if (X##_e > 0)						\
      {								\
	_FP_ROUND(wc, X);					\
	if (_FP_FRAC_OVERP_##wc(fs, X))				\
	  {							\
	    _FP_FRAC_CLEAR_OVERP_##wc(fs, X);			\
	    X##_e++;						\
	  }							\
	_FP_FRAC_SRL_##wc(X, _FP_WORKBITS);			\
	if (X##_e >= _FP_EXPMAX_##fs)				\
	  {							\
	    /* overflow */					\
	    switch (FP_ROUNDMODE)				\
	      {							\
	      case FP_RND_NEAREST:				\
		X##_c = FP_CLS_INF;				\
		break;						\
	      case FP_RND_PINF:					\
		if (!X##_s) X##_c = FP_CLS_INF;			\
		break;						\
	      case FP_RND_MINF:					\
		if (X##_s) X##_c = FP_CLS_INF;			\
		break;						\
	      }							\
	    if (X##_c == FP_CLS_INF)				\
	      {							\
		/* Overflow to infinity */			\
		X##_e = _FP_EXPMAX_##fs;			\
		_FP_FRAC_SET_##wc(X, _FP_ZEROFRAC_##wc);	\
	      }							\
	    else						\
	      {							\
		/* Overflow to maximum normal */		\
		X##_e = _FP_EXPMAX_##fs - 1;			\
		_FP_FRAC_SET_##wc(X, _FP_MAXFRAC_##wc);		\
	      }							\
	    FP_SET_EXCEPTION(FP_EX_OVERFLOW);			\
            FP_SET_EXCEPTION(FP_EX_INEXACT);			\
	  }							\
      }								\
    else							\
      {								\
	/* we've got a denormalized number */			\
	X##_e = -X##_e + 1;					\
	if (X##_e <= _FP_WFRACBITS_##fs)			\
	  {							\
	    _FP_FRAC_SRS_##wc(X, X##_e, _FP_WFRACBITS_##fs);	\
	    _FP_ROUND(wc, X);					\
	    if (_FP_FRAC_HIGH_##fs(X)				\
		& (_FP_OVERFLOW_##fs >> 1))			\
	      {							\
	        X##_e = 1;					\
	        _FP_FRAC_SET_##wc(X, _FP_ZEROFRAC_##wc);	\
	      }							\
	    else						\
	      {							\
		X##_e = 0;					\
		_FP_FRAC_SRL_##wc(X, _FP_WORKBITS);		\
		FP_SET_EXCEPTION(FP_EX_UNDERFLOW);		\
	      }							\
	  }							\
	else							\
	  {							\
	    /* underflow to zero */				\
	    X##_e = 0;						\
	    if (!_FP_FRAC_ZEROP_##wc(X))			\
	      {							\
	        _FP_FRAC_SET_##wc(X, _FP_MINFRAC_##wc);		\
	        _FP_ROUND(wc, X);				\
	        _FP_FRAC_LOW_##wc(X) >>= (_FP_WORKBITS);	\
	      }							\
	    FP_SET_EXCEPTION(FP_EX_UNDERFLOW);			\
	  }							\
      }								\
    break;							\
								\
  case FP_CLS_ZERO:						\
    X##_e = 0;							\
    _FP_FRAC_SET_##wc(X, _FP_ZEROFRAC_##wc);			\
    break;							\
								\
  case FP_CLS_INF:						\
    X##_e = _FP_EXPMAX_##fs;					\
    _FP_FRAC_SET_##wc(X, _FP_ZEROFRAC_##wc);			\
    break;							\
								\
  case FP_CLS_NAN:						\
    X##_e = _FP_EXPMAX_##fs;					\
    if (!_FP_KEEPNANFRACP)					\
      {								\
	_FP_FRAC_SET_##wc(X, _FP_NANFRAC_##fs);			\
	X##_s = _FP_NANSIGN_##fs;				\
      }								\
    else							\
      _FP_FRAC_HIGH_RAW_##fs(X) |= _FP_QNANBIT_##fs;		\
    break;							\
  }								\
} while (0)

/* This one accepts raw argument and not cooked,  returns
 * 1 if X is a signaling NaN.
 */
#define _FP_ISSIGNAN(fs, wc, X)					\
({								\
  int __ret = 0;						\
  if (X##_e == _FP_EXPMAX_##fs)					\
    {								\
      if (!_FP_FRAC_ZEROP_##wc(X)				\
	  && !(_FP_FRAC_HIGH_RAW_##fs(X) & _FP_QNANBIT_##fs))	\
	__ret = 1;						\
    }								\
  __ret;							\
})





/* Addition on semi-raw values.  */
#define _FP_ADD_INTERNAL(fs, wc, R, X, Y, OP)				 \
do {									 \
  if (X##_s == Y##_s)							 \
    {									 \
      /* Addition.  */							 \
      R##_s = X##_s;							 \
      int ediff = X##_e - Y##_e;					 \
      if (ediff > 0)							 \
	{								 \
	  R##_e = X##_e;						 \
	  if (Y##_e == 0)						 \
	    {								 \
	      /* Y is zero or denormalized.  */				 \
	      if (_FP_FRAC_ZEROP_##wc(Y))				 \
		{							 \
		  _FP_CHECK_SIGNAN_SEMIRAW(fs, wc, X);			 \
		  _FP_FRAC_COPY_##wc(R, X);				 \
		  goto add_done;					 \
		}							 \
	      else							 \
		{							 \
		  FP_SET_EXCEPTION(FP_EX_DENORM);			 \
		  ediff--;						 \
		  if (ediff == 0)					 \
		    {							 \
		      _FP_FRAC_ADD_##wc(R, X, Y);			 \
		      goto add3;					 \
		    }							 \
		  if (X##_e == _FP_EXPMAX_##fs)				 \
		    {							 \
		      _FP_CHECK_SIGNAN_SEMIRAW(fs, wc, X);		 \
		      _FP_FRAC_COPY_##wc(R, X);				 \
		      goto add_done;					 \
		    }							 \
		  goto add1;						 \
		}							 \
	    }								 \
	  else if (X##_e == _FP_EXPMAX_##fs)				 \
	    {								 \
	      /* X is NaN or Inf, Y is normal.  */			 \
	      _FP_CHECK_SIGNAN_SEMIRAW(fs, wc, X);			 \
	      _FP_FRAC_COPY_##wc(R, X);					 \
	      goto add_done;						 \
	    }								 \
									 \
	  /* Insert implicit MSB of Y.  */				 \
	  _FP_FRAC_HIGH_##fs(Y) |= _FP_IMPLBIT_SH_##fs;			 \
									 \
	add1:								 \
	  /* Shift the mantissa of Y to the right EDIFF steps;		 \
	     remember to account later for the implicit MSB of X.  */	 \
	  if (ediff <= _FP_WFRACBITS_##fs)				 \
	    _FP_FRAC_SRS_##wc(Y, ediff, _FP_WFRACBITS_##fs);		 \
	  else if (!_FP_FRAC_ZEROP_##wc(Y))				 \
	    _FP_FRAC_SET_##wc(Y, _FP_MINFRAC_##wc);			 \
	  _FP_FRAC_ADD_##wc(R, X, Y);					 \
	}								 \
      else if (ediff < 0)						 \
	{								 \
	  ediff = -ediff;						 \
	  R##_e = Y##_e;						 \
	  if (X##_e == 0)						 \
	    {								 \
	      /* X is zero or denormalized.  */				 \
	      if (_FP_FRAC_ZEROP_##wc(X))				 \
		{							 \
		  _FP_CHECK_SIGNAN_SEMIRAW(fs, wc, Y);			 \
		  _FP_FRAC_COPY_##wc(R, Y);				 \
		  goto add_done;					 \
		}							 \
	      else							 \
		{							 \
		  FP_SET_EXCEPTION(FP_EX_DENORM);			 \
		  ediff--;						 \
		  if (ediff == 0)					 \
		    {							 \
		      _FP_FRAC_ADD_##wc(R, Y, X);			 \
		      goto add3;					 \
		    }							 \
		  if (Y##_e == _FP_EXPMAX_##fs)				 \
		    {							 \
		      _FP_CHECK_SIGNAN_SEMIRAW(fs, wc, Y);		 \
		      _FP_FRAC_COPY_##wc(R, Y);				 \
		      goto add_done;					 \
		    }							 \
		  goto add2;						 \
		}							 \
	    }								 \
	  else if (Y##_e == _FP_EXPMAX_##fs)				 \
	    {								 \
	      /* Y is NaN or Inf, X is normal.  */			 \
	      _FP_CHECK_SIGNAN_SEMIRAW(fs, wc, Y);			 \
	      _FP_FRAC_COPY_##wc(R, Y);					 \
	      goto add_done;						 \
	    }								 \
									 \
	  /* Insert implicit MSB of X.  */				 \
	  _FP_FRAC_HIGH_##fs(X) |= _FP_IMPLBIT_SH_##fs;			 \
									 \
	add2:								 \
	  /* Shift the mantissa of X to the right EDIFF steps;		 \
	     remember to account later for the implicit MSB of Y.  */	 \
	  if (ediff <= _FP_WFRACBITS_##fs)				 \
	    _FP_FRAC_SRS_##wc(X, ediff, _FP_WFRACBITS_##fs);		 \
	  else if (!_FP_FRAC_ZEROP_##wc(X))				 \
	    _FP_FRAC_SET_##wc(X, _FP_MINFRAC_##wc);			 \
	  _FP_FRAC_ADD_##wc(R, Y, X);					 \
	}								 \
      else								 \
	{								 \
	  /* ediff == 0.  */						 \
	  if (!_FP_EXP_NORMAL(fs, wc, X))				 \
	    {								 \
	      if (X##_e == 0)						 \
		{							 \
		  /* X and Y are zero or denormalized.  */		 \
		  R##_e = 0;						 \
		  if (_FP_FRAC_ZEROP_##wc(X))				 \
		    {							 \
		      if (!_FP_FRAC_ZEROP_##wc(Y))			 \
			FP_SET_EXCEPTION(FP_EX_DENORM);			 \
		      _FP_FRAC_COPY_##wc(R, Y);				 \
		      goto add_done;					 \
		    }							 \
		  else if (_FP_FRAC_ZEROP_##wc(Y))			 \
		    {							 \
		      FP_SET_EXCEPTION(FP_EX_DENORM);			 \
		      _FP_FRAC_COPY_##wc(R, X);				 \
		      goto add_done;					 \
		    }							 \
		  else							 \
		    {							 \
		      FP_SET_EXCEPTION(FP_EX_DENORM);			 \
		      _FP_FRAC_ADD_##wc(R, X, Y);			 \
		      if (_FP_FRAC_HIGH_##fs(R) & _FP_IMPLBIT_SH_##fs)	 \
			{						 \
			  /* Normalized result.  */			 \
			  _FP_FRAC_HIGH_##fs(R)				 \
			    &= ~(_FP_W_TYPE)_FP_IMPLBIT_SH_##fs;	 \
			  R##_e = 1;					 \
			}						 \
		      goto add_done;					 \
		    }							 \
		}							 \
	      else							 \
		{							 \
		  /* X and Y are NaN or Inf.  */			 \
		  _FP_CHECK_SIGNAN_SEMIRAW(fs, wc, X);			 \
		  _FP_CHECK_SIGNAN_SEMIRAW(fs, wc, Y);			 \
		  R##_e = _FP_EXPMAX_##fs;				 \
		  if (_FP_FRAC_ZEROP_##wc(X))				 \
		    _FP_FRAC_COPY_##wc(R, Y);				 \
		  else if (_FP_FRAC_ZEROP_##wc(Y))			 \
		    _FP_FRAC_COPY_##wc(R, X);				 \
		  else							 \
		    _FP_CHOOSENAN_SEMIRAW(fs, wc, R, X, Y, OP);		 \
		  goto add_done;					 \
		}							 \
	    }								 \
	  /* The exponents of X and Y, both normal, are equal.  The	 \
	     implicit MSBs will always add to increase the		 \
	     exponent.  */						 \
	  _FP_FRAC_ADD_##wc(R, X, Y);					 \
	  R##_e = X##_e + 1;						 \
	  _FP_FRAC_SRS_##wc(R, 1, _FP_WFRACBITS_##fs);			 \
	  if (R##_e == _FP_EXPMAX_##fs)					 \
	    /* Overflow to infinity (depending on rounding mode).  */	 \
	    _FP_OVERFLOW_SEMIRAW(fs, wc, R);				 \
	  goto add_done;						 \
	}								 \
    add3:								 \
      if (_FP_FRAC_HIGH_##fs(R) & _FP_IMPLBIT_SH_##fs)			 \
	{								 \
	  /* Overflow.  */						 \
	  _FP_FRAC_HIGH_##fs(R) &= ~(_FP_W_TYPE)_FP_IMPLBIT_SH_##fs;	 \
	  R##_e++;							 \
	  _FP_FRAC_SRS_##wc(R, 1, _FP_WFRACBITS_##fs);			 \
	  if (R##_e == _FP_EXPMAX_##fs)					 \
	    /* Overflow to infinity (depending on rounding mode).  */	 \
	    _FP_OVERFLOW_SEMIRAW(fs, wc, R);				 \
	}								 \
    add_done: ;								 \
    }									 \
  else									 \
    {									 \
      /* Subtraction.  */						 \
      int ediff = X##_e - Y##_e;					 \
      if (ediff > 0)							 \
	{								 \
	  R##_e = X##_e;						 \
	  R##_s = X##_s;						 \
	  if (Y##_e == 0)						 \
	    {								 \
	      /* Y is zero or denormalized.  */				 \
	      if (_FP_FRAC_ZEROP_##wc(Y))				 \
		{							 \
		  _FP_CHECK_SIGNAN_SEMIRAW(fs, wc, X);			 \
		  _FP_FRAC_COPY_##wc(R, X);				 \
		  goto sub_done;					 \
		}							 \
	      else							 \
		{							 \
		  FP_SET_EXCEPTION(FP_EX_DENORM);			 \
		  ediff--;						 \
		  if (ediff == 0)					 \
		    {							 \
		      _FP_FRAC_SUB_##wc(R, X, Y);			 \
		      goto sub3;					 \
		    }							 \
		  if (X##_e == _FP_EXPMAX_##fs)				 \
		    {							 \
		      _FP_CHECK_SIGNAN_SEMIRAW(fs, wc, X);		 \
		      _FP_FRAC_COPY_##wc(R, X);				 \
		      goto sub_done;					 \
		    }							 \
		  goto sub1;						 \
		}							 \
	    }								 \
	  else if (X##_e == _FP_EXPMAX_##fs)				 \
	    {								 \
	      /* X is NaN or Inf, Y is normal.  */			 \
	      _FP_CHECK_SIGNAN_SEMIRAW(fs, wc, X);			 \
	      _FP_FRAC_COPY_##wc(R, X);					 \
	      goto sub_done;						 \
	    }								 \
									 \
	  /* Insert implicit MSB of Y.  */				 \
	  _FP_FRAC_HIGH_##fs(Y) |= _FP_IMPLBIT_SH_##fs;			 \
									 \
	sub1:								 \
	  /* Shift the mantissa of Y to the right EDIFF steps;		 \
	     remember to account later for the implicit MSB of X.  */	 \
	  if (ediff <= _FP_WFRACBITS_##fs)				 \
	    _FP_FRAC_SRS_##wc(Y, ediff, _FP_WFRACBITS_##fs);		 \
	  else if (!_FP_FRAC_ZEROP_##wc(Y))				 \
	    _FP_FRAC_SET_##wc(Y, _FP_MINFRAC_##wc);			 \
	  _FP_FRAC_SUB_##wc(R, X, Y);					 \
	}								 \
      else if (ediff < 0)						 \
	{								 \
	  ediff = -ediff;						 \
	  R##_e = Y##_e;						 \
	  R##_s = Y##_s;						 \
	  if (X##_e == 0)						 \
	    {								 \
	      /* X is zero or denormalized.  */				 \
	      if (_FP_FRAC_ZEROP_##wc(X))				 \
		{							 \
		  _FP_CHECK_SIGNAN_SEMIRAW(fs, wc, Y);			 \
		  _FP_FRAC_COPY_##wc(R, Y);				 \
		  goto sub_done;					 \
		}							 \
	      else							 \
		{							 \
		  FP_SET_EXCEPTION(FP_EX_DENORM);			 \
		  ediff--;						 \
		  if (ediff == 0)					 \
		    {							 \
		      _FP_FRAC_SUB_##wc(R, Y, X);			 \
		      goto sub3;					 \
		    }							 \
		  if (Y##_e == _FP_EXPMAX_##fs)				 \
		    {							 \
		      _FP_CHECK_SIGNAN_SEMIRAW(fs, wc, Y);		 \
		      _FP_FRAC_COPY_##wc(R, Y);				 \
		      goto sub_done;					 \
		    }							 \
		  goto sub2;						 \
		}							 \
	    }								 \
	  else if (Y##_e == _FP_EXPMAX_##fs)				 \
	    {								 \
	      /* Y is NaN or Inf, X is normal.  */			 \
	      _FP_CHECK_SIGNAN_SEMIRAW(fs, wc, Y);			 \
	      _FP_FRAC_COPY_##wc(R, Y);					 \
	      goto sub_done;						 \
	    }								 \
									 \
	  /* Insert implicit MSB of X.  */				 \
	  _FP_FRAC_HIGH_##fs(X) |= _FP_IMPLBIT_SH_##fs;			 \
									 \
	sub2:								 \
	  /* Shift the mantissa of X to the right EDIFF steps;		 \
	     remember to account later for the implicit MSB of Y.  */	 \
	  if (ediff <= _FP_WFRACBITS_##fs)				 \
	    _FP_FRAC_SRS_##wc(X, ediff, _FP_WFRACBITS_##fs);		 \
	  else if (!_FP_FRAC_ZEROP_##wc(X))				 \
	    _FP_FRAC_SET_##wc(X, _FP_MINFRAC_##wc);			 \
	  _FP_FRAC_SUB_##wc(R, Y, X);					 \
	}								 \
      else								 \
	{								 \
	  /* ediff == 0.  */						 \
	  if (!_FP_EXP_NORMAL(fs, wc, X))				 \
	    {								 \
	      if (X##_e == 0)						 \
		{							 \
		  /* X and Y are zero or denormalized.  */		 \
		  R##_e = 0;						 \
		  if (_FP_FRAC_ZEROP_##wc(X))				 \
		    {							 \
		      _FP_FRAC_COPY_##wc(R, Y);				 \
		      if (_FP_FRAC_ZEROP_##wc(Y))			 \
			R##_s = (FP_ROUNDMODE == FP_RND_MINF);		 \
		      else						 \
			{						 \
			  FP_SET_EXCEPTION(FP_EX_DENORM);		 \
			  R##_s = Y##_s;				 \
			}						 \
		      goto sub_done;					 \
		    }							 \
		  else if (_FP_FRAC_ZEROP_##wc(Y))			 \
		    {							 \
		      FP_SET_EXCEPTION(FP_EX_DENORM);			 \
		      _FP_FRAC_COPY_##wc(R, X);				 \
		      R##_s = X##_s;					 \
		      goto sub_done;					 \
		    }							 \
		  else							 \
		    {							 \
		      FP_SET_EXCEPTION(FP_EX_DENORM);			 \
		      _FP_FRAC_SUB_##wc(R, X, Y);			 \
		      R##_s = X##_s;					 \
		      if (_FP_FRAC_HIGH_##fs(R) & _FP_IMPLBIT_SH_##fs)	 \
			{						 \
			  /* |X| < |Y|, negate result.  */		 \
			  _FP_FRAC_SUB_##wc(R, Y, X);			 \
			  R##_s = Y##_s;				 \
			}						 \
		      else if (_FP_FRAC_ZEROP_##wc(R))			 \
			R##_s = (FP_ROUNDMODE == FP_RND_MINF);		 \
		      goto sub_done;					 \
		    }							 \
		}							 \
	      else							 \
		{							 \
		  /* X and Y are NaN or Inf, of opposite signs.  */	 \
		  _FP_CHECK_SIGNAN_SEMIRAW(fs, wc, X);			 \
		  _FP_CHECK_SIGNAN_SEMIRAW(fs, wc, Y);			 \
		  R##_e = _FP_EXPMAX_##fs;				 \
		  if (_FP_FRAC_ZEROP_##wc(X))				 \
		    {							 \
		      if (_FP_FRAC_ZEROP_##wc(Y))			 \
			{						 \
			  /* Inf - Inf.  */				 \
			  R##_s = _FP_NANSIGN_##fs;			 \
			  _FP_FRAC_SET_##wc(R, _FP_NANFRAC_##fs);	 \
			  _FP_FRAC_SLL_##wc(R, _FP_WORKBITS);		 \
			  FP_SET_EXCEPTION(FP_EX_INVALID);		 \
			}						 \
		      else						 \
			{						 \
			  /* Inf - NaN.  */				 \
			  R##_s = Y##_s;				 \
			  _FP_FRAC_COPY_##wc(R, Y);			 \
			}						 \
		    }							 \
		  else							 \
		    {							 \
		      if (_FP_FRAC_ZEROP_##wc(Y))			 \
			{						 \
			  /* NaN - Inf.  */				 \
			  R##_s = X##_s;				 \
			  _FP_FRAC_COPY_##wc(R, X);			 \
			}						 \
		      else						 \
			{						 \
			  /* NaN - NaN.  */				 \
			  _FP_CHOOSENAN_SEMIRAW(fs, wc, R, X, Y, OP);	 \
			}						 \
		    }							 \
		  goto sub_done;					 \
		}							 \
	    }								 \
	  /* The exponents of X and Y, both normal, are equal.  The	 \
	     implicit MSBs cancel.  */					 \
	  R##_e = X##_e;						 \
	  _FP_FRAC_SUB_##wc(R, X, Y);					 \
	  R##_s = X##_s;						 \
	  if (_FP_FRAC_HIGH_##fs(R) & _FP_IMPLBIT_SH_##fs)		 \
	    {								 \
	      /* |X| < |Y|, negate result.  */				 \
	      _FP_FRAC_SUB_##wc(R, Y, X);				 \
	      R##_s = Y##_s;						 \
	    }								 \
	  else if (_FP_FRAC_ZEROP_##wc(R))				 \
	    {								 \
	      R##_e = 0;						 \
	      R##_s = (FP_ROUNDMODE == FP_RND_MINF);			 \
	      goto sub_done;						 \
	    }								 \
	  goto norm;							 \
	}								 \
    sub3:								 \
      if (_FP_FRAC_HIGH_##fs(R) & _FP_IMPLBIT_SH_##fs)			 \
	{								 \
	  int diff;							 \
	  /* Carry into most significant bit of larger one of X and Y,	 \
	     canceling it; renormalize.  */				 \
	  _FP_FRAC_HIGH_##fs(R) &= _FP_IMPLBIT_SH_##fs - 1;		 \
	norm:								 \
	  _FP_FRAC_CLZ_##wc(diff, R);					 \
	  diff -= _FP_WFRACXBITS_##fs;					 \
	  _FP_FRAC_SLL_##wc(R, diff);					 \
	  if (R##_e <= diff)						 \
	    {								 \
	      /* R is denormalized.  */					 \
	      diff = diff - R##_e + 1;					 \
	      _FP_FRAC_SRS_##wc(R, diff, _FP_WFRACBITS_##fs);		 \
	      R##_e = 0;						 \
	    }								 \
	  else								 \
	    {								 \
	      R##_e -= diff;						 \
	      _FP_FRAC_HIGH_##fs(R) &= ~(_FP_W_TYPE)_FP_IMPLBIT_SH_##fs; \
	    }								 \
	}								 \
    sub_done: ;								 \
    }									 \
} while (0)

#define _FP_ADD(fs, wc, R, X, Y) _FP_ADD_INTERNAL(fs, wc, R, X, Y, '+')
#define _FP_SUB(fs, wc, R, X, Y)					    \
  do {									    \
    if (!(Y##_e == _FP_EXPMAX_##fs && !_FP_FRAC_ZEROP_##wc(Y))) Y##_s ^= 1; \
    _FP_ADD_INTERNAL(fs, wc, R, X, Y, '-');				    \
  } while (0)


/*
 * Main negation routine.  FIXME -- when we care about setting exception
 * bits reliably, this will not do.  We should examine all of the fp classes.
 */

#define _FP_NEG(fs, wc, R, X)		\
  do {					\
    _FP_FRAC_COPY_##wc(R, X);		\
    R##_c = X##_c;			\
    R##_e = X##_e;			\
    R##_s = 1 ^ X##_s;			\
  } while (0)


/*
 * Main multiplication routine.  The input values should be cooked.
 */

#define _FP_MUL(fs, wc, R, X, Y)			\
do {							\
  R##_s = X##_s ^ Y##_s;				\
  switch (_FP_CLS_COMBINE(X##_c, Y##_c))		\
  {							\
  case _FP_CLS_COMBINE(FP_CLS_NORMAL,FP_CLS_NORMAL):	\
    R##_c = FP_CLS_NORMAL;				\
    R##_e = X##_e + Y##_e + 1;				\
							\
    _FP_MUL_MEAT_##fs(R,X,Y);				\
							\
    if (_FP_FRAC_OVERP_##wc(fs, R))			\
      _FP_FRAC_SRS_##wc(R, 1, _FP_WFRACBITS_##fs);	\
    else						\
      R##_e--;						\
    break;						\
							\
  case _FP_CLS_COMBINE(FP_CLS_NAN,FP_CLS_NAN):		\
    _FP_CHOOSENAN(fs, wc, R, X, Y, '*');		\
    break;						\
							\
  case _FP_CLS_COMBINE(FP_CLS_NAN,FP_CLS_NORMAL):	\
  case _FP_CLS_COMBINE(FP_CLS_NAN,FP_CLS_INF):		\
  case _FP_CLS_COMBINE(FP_CLS_NAN,FP_CLS_ZERO):		\
    R##_s = X##_s;					\
							\
  case _FP_CLS_COMBINE(FP_CLS_INF,FP_CLS_INF):		\
  case _FP_CLS_COMBINE(FP_CLS_INF,FP_CLS_NORMAL):	\
  case _FP_CLS_COMBINE(FP_CLS_ZERO,FP_CLS_NORMAL):	\
  case _FP_CLS_COMBINE(FP_CLS_ZERO,FP_CLS_ZERO):	\
    _FP_FRAC_COPY_##wc(R, X);				\
    R##_c = X##_c;					\
    break;						\
							\
  case _FP_CLS_COMBINE(FP_CLS_NORMAL,FP_CLS_NAN):	\
  case _FP_CLS_COMBINE(FP_CLS_INF,FP_CLS_NAN):		\
  case _FP_CLS_COMBINE(FP_CLS_ZERO,FP_CLS_NAN):		\
    R##_s = Y##_s;					\
							\
  case _FP_CLS_COMBINE(FP_CLS_NORMAL,FP_CLS_INF):	\
  case _FP_CLS_COMBINE(FP_CLS_NORMAL,FP_CLS_ZERO):	\
    _FP_FRAC_COPY_##wc(R, Y);				\
    R##_c = Y##_c;					\
    break;						\
							\
  case _FP_CLS_COMBINE(FP_CLS_INF,FP_CLS_ZERO):		\
  case _FP_CLS_COMBINE(FP_CLS_ZERO,FP_CLS_INF):		\
    R##_s = _FP_NANSIGN_##fs;				\
    R##_c = FP_CLS_NAN;					\
    _FP_FRAC_SET_##wc(R, _FP_NANFRAC_##fs);		\
    FP_SET_EXCEPTION(FP_EX_INVALID);			\
    break;						\
							\
  default:						\
    abort();						\
  }							\
} while (0)


/*
 * Main division routine.  The input values should be cooked.
 */

#define _FP_DIV(fs, wc, R, X, Y)			\
do {							\
  R##_s = X##_s ^ Y##_s;				\
  switch (_FP_CLS_COMBINE(X##_c, Y##_c))		\
  {							\
  case _FP_CLS_COMBINE(FP_CLS_NORMAL,FP_CLS_NORMAL):	\
    R##_c = FP_CLS_NORMAL;				\
    R##_e = X##_e - Y##_e;				\
							\
    _FP_DIV_MEAT_##fs(R,X,Y);				\
    break;						\
							\
  case _FP_CLS_COMBINE(FP_CLS_NAN,FP_CLS_NAN):		\
    _FP_CHOOSENAN(fs, wc, R, X, Y, '/');		\
    break;						\
							\
  case _FP_CLS_COMBINE(FP_CLS_NAN,FP_CLS_NORMAL):	\
  case _FP_CLS_COMBINE(FP_CLS_NAN,FP_CLS_INF):		\
  case _FP_CLS_COMBINE(FP_CLS_NAN,FP_CLS_ZERO):		\
    R##_s = X##_s;					\
    _FP_FRAC_COPY_##wc(R, X);				\
    R##_c = X##_c;					\
    break;						\
							\
  case _FP_CLS_COMBINE(FP_CLS_NORMAL,FP_CLS_NAN):	\
  case _FP_CLS_COMBINE(FP_CLS_INF,FP_CLS_NAN):		\
  case _FP_CLS_COMBINE(FP_CLS_ZERO,FP_CLS_NAN):		\
    R##_s = Y##_s;					\
    _FP_FRAC_COPY_##wc(R, Y);				\
    R##_c = Y##_c;					\
    break;						\
							\
  case _FP_CLS_COMBINE(FP_CLS_NORMAL,FP_CLS_INF):	\
  case _FP_CLS_COMBINE(FP_CLS_ZERO,FP_CLS_INF):		\
  case _FP_CLS_COMBINE(FP_CLS_ZERO,FP_CLS_NORMAL):	\
    R##_c = FP_CLS_ZERO;				\
    break;						\
							\
  case _FP_CLS_COMBINE(FP_CLS_NORMAL,FP_CLS_ZERO):	\
    FP_SET_EXCEPTION(FP_EX_DIVZERO);			\
  case _FP_CLS_COMBINE(FP_CLS_INF,FP_CLS_ZERO):		\
  case _FP_CLS_COMBINE(FP_CLS_INF,FP_CLS_NORMAL):	\
    R##_c = FP_CLS_INF;					\
    break;						\
							\
  case _FP_CLS_COMBINE(FP_CLS_INF,FP_CLS_INF):		\
  case _FP_CLS_COMBINE(FP_CLS_ZERO,FP_CLS_ZERO):	\
    R##_s = _FP_NANSIGN_##fs;				\
    R##_c = FP_CLS_NAN;					\
    _FP_FRAC_SET_##wc(R, _FP_NANFRAC_##fs);		\
    FP_SET_EXCEPTION(FP_EX_INVALID);			\
    break;						\
							\
  default:						\
    abort();						\
  }							\
} while (0)


/*
 * Main differential comparison routine.  The inputs should be raw not
 * cooked.  The return is -1,0,1 for normal values, 2 otherwise.
 */

#define _FP_CMP(fs, wc, ret, X, Y, un)					\
  do {									\
    /* NANs are unordered */						\
    if ((X##_e == _FP_EXPMAX_##fs && !_FP_FRAC_ZEROP_##wc(X))		\
	|| (Y##_e == _FP_EXPMAX_##fs && !_FP_FRAC_ZEROP_##wc(Y)))	\
      {									\
	ret = un;							\
      }									\
    else								\
      {									\
	int __is_zero_x;						\
	int __is_zero_y;						\
									\
	__is_zero_x = (!X##_e && _FP_FRAC_ZEROP_##wc(X)) ? 1 : 0;	\
	__is_zero_y = (!Y##_e && _FP_FRAC_ZEROP_##wc(Y)) ? 1 : 0;	\
									\
	if (__is_zero_x && __is_zero_y)					\
		ret = 0;						\
	else if (__is_zero_x)						\
		ret = Y##_s ? 1 : -1;					\
	else if (__is_zero_y)						\
		ret = X##_s ? -1 : 1;					\
	else if (X##_s != Y##_s)					\
	  ret = X##_s ? -1 : 1;						\
	else if (X##_e > Y##_e)						\
	  ret = X##_s ? -1 : 1;						\
	else if (X##_e < Y##_e)						\
	  ret = X##_s ? 1 : -1;						\
	else if (_FP_FRAC_GT_##wc(X, Y))				\
	  ret = X##_s ? -1 : 1;						\
	else if (_FP_FRAC_GT_##wc(Y, X))				\
	  ret = X##_s ? 1 : -1;						\
	else								\
	  ret = 0;							\
      }									\
  } while (0)


/* Simplification for strict equality.  */

#define _FP_CMP_EQ(fs, wc, ret, X, Y)					    \
  do {									    \
    /* NANs are unordered */						    \
    if ((X##_e == _FP_EXPMAX_##fs && !_FP_FRAC_ZEROP_##wc(X))		    \
	|| (Y##_e == _FP_EXPMAX_##fs && !_FP_FRAC_ZEROP_##wc(Y)))	    \
      {									    \
	ret = 1;							    \
      }									    \
    else								    \
      {									    \
	ret = !(X##_e == Y##_e						    \
		&& _FP_FRAC_EQ_##wc(X, Y)				    \
		&& (X##_s == Y##_s || (!X##_e && _FP_FRAC_ZEROP_##wc(X)))); \
      }									    \
  } while (0)

/* Version to test unordered.  */

#define _FP_CMP_UNORD(fs, wc, ret, X, Y)				\
  do {									\
    ret = ((X##_e == _FP_EXPMAX_##fs && !_FP_FRAC_ZEROP_##wc(X))	\
	   || (Y##_e == _FP_EXPMAX_##fs && !_FP_FRAC_ZEROP_##wc(Y)));	\
  } while (0)

/*
 * Main square root routine.  The input value should be cooked.
 */

#define _FP_SQRT(fs, wc, R, X)						\
do {									\
    _FP_FRAC_DECL_##wc(T); _FP_FRAC_DECL_##wc(S);			\
    _FP_W_TYPE q;							\
    switch (X##_c)							\
    {									\
    case FP_CLS_NAN:							\
	_FP_FRAC_COPY_##wc(R, X);					\
	R##_s = X##_s;							\
    	R##_c = FP_CLS_NAN;						\
    	break;								\
    case FP_CLS_INF:							\
    	if (X##_s)							\
    	  {								\
    	    R##_s = _FP_NANSIGN_##fs;					\
	    R##_c = FP_CLS_NAN; /* NAN */				\
	    _FP_FRAC_SET_##wc(R, _FP_NANFRAC_##fs);			\
	    FP_SET_EXCEPTION(FP_EX_INVALID);				\
    	  }								\
    	else								\
    	  {								\
    	    R##_s = 0;							\
    	    R##_c = FP_CLS_INF; /* sqrt(+inf) = +inf */			\
    	  }								\
    	break;								\
    case FP_CLS_ZERO:							\
	R##_s = X##_s;							\
	R##_c = FP_CLS_ZERO; /* sqrt(+-0) = +-0 */			\
	break;								\
    case FP_CLS_NORMAL:							\
    	R##_s = 0;							\
        if (X##_s)							\
          {								\
	    R##_c = FP_CLS_NAN; /* sNAN */				\
	    R##_s = _FP_NANSIGN_##fs;					\
	    _FP_FRAC_SET_##wc(R, _FP_NANFRAC_##fs);			\
	    FP_SET_EXCEPTION(FP_EX_INVALID);				\
	    break;							\
          }								\
    	R##_c = FP_CLS_NORMAL;						\
        if (X##_e & 1)							\
          _FP_FRAC_SLL_##wc(X, 1);					\
        R##_e = X##_e >> 1;						\
        _FP_FRAC_SET_##wc(S, _FP_ZEROFRAC_##wc);			\
        _FP_FRAC_SET_##wc(R, _FP_ZEROFRAC_##wc);			\
        q = _FP_OVERFLOW_##fs >> 1;					\
        _FP_SQRT_MEAT_##wc(R, S, T, X, q);				\
    }									\
  } while (0)

/*
 * Convert from FP to integer.  Input is raw.
 */

/* RSIGNED can have following values:
 * 0:  the number is required to be 0..(2^rsize)-1, if not, NV is set plus
 *     the result is either 0 or (2^rsize)-1 depending on the sign in such
 *     case.
 * 1:  the number is required to be -(2^(rsize-1))..(2^(rsize-1))-1, if not,
 *     NV is set plus the result is either -(2^(rsize-1)) or (2^(rsize-1))-1
 *     depending on the sign in such case.
 * -1: the number is required to be -(2^(rsize-1))..(2^rsize)-1, if not, NV is
 *     set plus the result is either -(2^(rsize-1)) or (2^(rsize-1))-1
 *     depending on the sign in such case.
 */
#define _FP_TO_INT(fs, wc, r, X, rsize, rsigned)			\
do {									\
  if (X##_e < _FP_EXPBIAS_##fs)						\
    {									\
      r = 0;								\
      if (X##_e == 0)							\
	{								\
	  if (!_FP_FRAC_ZEROP_##wc(X))					\
	    {								\
	      FP_SET_EXCEPTION(FP_EX_INEXACT);				\
	      FP_SET_EXCEPTION(FP_EX_DENORM);				\
	    }								\
	}								\
      else								\
	FP_SET_EXCEPTION(FP_EX_INEXACT);				\
    }									\
  else if (X##_e >= _FP_EXPBIAS_##fs + rsize - (rsigned > 0 || X##_s)	\
	   || (!rsigned && X##_s))					\
    {									\
      /* Overflow or converting to the most negative integer.  */	\
      if (rsigned)							\
	{								\
	  r = 1;							\
	  r <<= rsize - 1;						\
	  r -= 1 - X##_s;						\
	} else {							\
	  r = 0;							\
	  if (X##_s)							\
	    r = ~r;							\
	}								\
									\
      if (rsigned && X##_s && X##_e == _FP_EXPBIAS_##fs + rsize - 1)	\
	{								\
	  /* Possibly converting to most negative integer; check the	\
	     mantissa.  */						\
	  int inexact = 0;						\
	  (void)((_FP_FRACBITS_##fs > rsize)				\
		 ? ({ _FP_FRAC_SRST_##wc(X, inexact,			\
					 _FP_FRACBITS_##fs - rsize,	\
					 _FP_FRACBITS_##fs); 0; })	\
		 : 0);							\
	  if (!_FP_FRAC_ZEROP_##wc(X))					\
	    FP_SET_EXCEPTION(FP_EX_INVALID);				\
	  else if (inexact)						\
	    FP_SET_EXCEPTION(FP_EX_INEXACT);				\
	}								\
      else								\
	FP_SET_EXCEPTION(FP_EX_INVALID);				\
    }									\
  else									\
    {									\
      _FP_FRAC_HIGH_RAW_##fs(X) |= _FP_IMPLBIT_##fs;			\
      if (X##_e >= _FP_EXPBIAS_##fs + _FP_FRACBITS_##fs - 1)		\
	{								\
	  _FP_FRAC_ASSEMBLE_##wc(r, X, rsize);				\
	  r <<= X##_e - _FP_EXPBIAS_##fs - _FP_FRACBITS_##fs + 1;	\
	}								\
      else								\
	{								\
	  int inexact;							\
	  _FP_FRAC_SRST_##wc(X, inexact,				\
			    (_FP_FRACBITS_##fs + _FP_EXPBIAS_##fs - 1	\
			     - X##_e),					\
			    _FP_FRACBITS_##fs);				\
	  if (inexact)							\
	    FP_SET_EXCEPTION(FP_EX_INEXACT);				\
	  _FP_FRAC_ASSEMBLE_##wc(r, X, rsize);				\
	}								\
      if (rsigned && X##_s)						\
	r = -r;								\
    }									\
} while (0)

/* Convert integer to fp.  Output is raw.  RTYPE is unsigned even if
   input is signed.  */
#define _FP_FROM_INT(fs, wc, X, r, rsize, rtype)			     \
  do {									     \
    if (r)								     \
      {									     \
	rtype ur_;							     \
									     \
	if ((X##_s = (r < 0)))						     \
	  r = -(rtype)r;						     \
									     \
	ur_ = (rtype) r;						     \
	(void)((rsize <= _FP_W_TYPE_SIZE)				     \
	       ? ({							     \
		    int lz_;						     \
		    __FP_CLZ(lz_, (_FP_W_TYPE)ur_);			     \
		    X##_e = _FP_EXPBIAS_##fs + _FP_W_TYPE_SIZE - 1 - lz_;    \
		  })							     \
	       : ((rsize <= 2 * _FP_W_TYPE_SIZE)			     \
		  ? ({							     \
		       int lz_;						     \
		       __FP_CLZ_2(lz_, (_FP_W_TYPE)(ur_ >> _FP_W_TYPE_SIZE), \
				  (_FP_W_TYPE)ur_);			     \
		       X##_e = (_FP_EXPBIAS_##fs + 2 * _FP_W_TYPE_SIZE - 1   \
				- lz_);					     \
		     })							     \
		  : (abort(), 0)));					     \
									     \
	if (rsize - 1 + _FP_EXPBIAS_##fs >= _FP_EXPMAX_##fs		     \
	    && X##_e >= _FP_EXPMAX_##fs)				     \
	  {								     \
	    /* Exponent too big; overflow to infinity.  (May also	     \
	       happen after rounding below.)  */			     \
	    _FP_OVERFLOW_SEMIRAW(fs, wc, X);				     \
	    goto pack_semiraw;						     \
	  }								     \
									     \
	if (rsize <= _FP_FRACBITS_##fs					     \
	    || X##_e < _FP_EXPBIAS_##fs + _FP_FRACBITS_##fs)		     \
	  {								     \
	    /* Exactly representable; shift left.  */			     \
	    _FP_FRAC_DISASSEMBLE_##wc(X, ur_, rsize);			     \
	    _FP_FRAC_SLL_##wc(X, (_FP_EXPBIAS_##fs			     \
				  + _FP_FRACBITS_##fs - 1 - X##_e));	     \
	  }								     \
	else								     \
	  {								     \
	    /* More bits in integer than in floating type; need to	     \
	       round.  */						     \
	    if (_FP_EXPBIAS_##fs + _FP_WFRACBITS_##fs - 1 < X##_e)	     \
	      ur_ = ((ur_ >> (X##_e - _FP_EXPBIAS_##fs			     \
			      - _FP_WFRACBITS_##fs + 1))		     \
		     | ((ur_ << (rsize - (X##_e - _FP_EXPBIAS_##fs	     \
					  - _FP_WFRACBITS_##fs + 1)))	     \
			!= 0));						     \
	    _FP_FRAC_DISASSEMBLE_##wc(X, ur_, rsize);			     \
	    if ((_FP_EXPBIAS_##fs + _FP_WFRACBITS_##fs - 1 - X##_e) > 0)     \
	      _FP_FRAC_SLL_##wc(X, (_FP_EXPBIAS_##fs			     \
				    + _FP_WFRACBITS_##fs - 1 - X##_e));	     \
	    _FP_FRAC_HIGH_##fs(X) &= ~(_FP_W_TYPE)_FP_IMPLBIT_SH_##fs;	     \
	  pack_semiraw:							     \
	    _FP_PACK_SEMIRAW(fs, wc, X);				     \
	  }								     \
      }									     \
    else								     \
      {									     \
	X##_s = 0;							     \
	X##_e = 0;							     \
	_FP_FRAC_SET_##wc(X, _FP_ZEROFRAC_##wc);			     \
      }									     \
  } while (0)


/* Extend from a narrower floating-point format to a wider one.  Input
   and output are raw.  */
#define FP_EXTEND(dfs,sfs,dwc,swc,D,S)					 \
do {									 \
  if (_FP_FRACBITS_##dfs < _FP_FRACBITS_##sfs				 \
      || (_FP_EXPMAX_##dfs - _FP_EXPBIAS_##dfs				 \
	  < _FP_EXPMAX_##sfs - _FP_EXPBIAS_##sfs)			 \
      || (_FP_EXPBIAS_##dfs < _FP_EXPBIAS_##sfs + _FP_FRACBITS_##sfs - 1 \
	  && _FP_EXPBIAS_##dfs != _FP_EXPBIAS_##sfs))			 \
    abort();								 \
  D##_s = S##_s;							 \
  _FP_FRAC_COPY_##dwc##_##swc(D, S);					 \
  if (_FP_EXP_NORMAL(sfs, swc, S))					 \
    {									 \
      D##_e = S##_e + _FP_EXPBIAS_##dfs - _FP_EXPBIAS_##sfs;		 \
      _FP_FRAC_SLL_##dwc(D, (_FP_FRACBITS_##dfs - _FP_FRACBITS_##sfs));	 \
    }									 \
  else									 \
    {									 \
      if (S##_e == 0)							 \
	{								 \
	  if (_FP_FRAC_ZEROP_##swc(S))					 \
	    D##_e = 0;							 \
	  else if (_FP_EXPBIAS_##dfs					 \
		   < _FP_EXPBIAS_##sfs + _FP_FRACBITS_##sfs - 1)	 \
	    {								 \
	      FP_SET_EXCEPTION(FP_EX_DENORM);				 \
	      _FP_FRAC_SLL_##dwc(D, (_FP_FRACBITS_##dfs			 \
				     - _FP_FRACBITS_##sfs));		 \
	      D##_e = 0;						 \
	    }								 \
	  else								 \
	    {								 \
	      int _lz;							 \
	      FP_SET_EXCEPTION(FP_EX_DENORM);				 \
	      _FP_FRAC_CLZ_##swc(_lz, S);				 \
	      _FP_FRAC_SLL_##dwc(D,					 \
				 _lz + _FP_FRACBITS_##dfs		 \
				 - _FP_FRACTBITS_##sfs);		 \
	      D##_e = (_FP_EXPBIAS_##dfs - _FP_EXPBIAS_##sfs + 1	 \
		       + _FP_FRACXBITS_##sfs - _lz);			 \
	    }								 \
	}								 \
      else								 \
	{								 \
	  D##_e = _FP_EXPMAX_##dfs;					 \
	  if (!_FP_FRAC_ZEROP_##swc(S))					 \
	    {								 \
	      if (!(_FP_FRAC_HIGH_RAW_##sfs(S) & _FP_QNANBIT_##sfs))	 \
		FP_SET_EXCEPTION(FP_EX_INVALID);			 \
	      _FP_FRAC_SLL_##dwc(D, (_FP_FRACBITS_##dfs			 \
				     - _FP_FRACBITS_##sfs));		 \
	    }								 \
	}								 \
    }									 \
} while (0)

/* Truncate from a wider floating-point format to a narrower one.
   Input and output are semi-raw.  */
#define FP_TRUNC(dfs,sfs,dwc,swc,D,S)					     \
do {									     \
  if (_FP_FRACBITS_##sfs < _FP_FRACBITS_##dfs				     \
      || (_FP_EXPBIAS_##sfs < _FP_EXPBIAS_##dfs + _FP_FRACBITS_##dfs - 1     \
	  && _FP_EXPBIAS_##sfs != _FP_EXPBIAS_##dfs))			     \
    abort();								     \
  D##_s = S##_s;							     \
  if (_FP_EXP_NORMAL(sfs, swc, S))					     \
    {									     \
      D##_e = S##_e + _FP_EXPBIAS_##dfs - _FP_EXPBIAS_##sfs;		     \
      if (D##_e >= _FP_EXPMAX_##dfs)					     \
	_FP_OVERFLOW_SEMIRAW(dfs, dwc, D);				     \
      else								     \
	{								     \
	  if (D##_e <= 0)						     \
	    {								     \
	      if (D##_e < 1 - _FP_FRACBITS_##dfs)			     \
		{							     \
		  _FP_FRAC_SET_##swc(S, _FP_ZEROFRAC_##swc);		     \
		  _FP_FRAC_LOW_##swc(S) |= 1;				     \
		}							     \
	      else							     \
		{							     \
		  _FP_FRAC_HIGH_##sfs(S) |= _FP_IMPLBIT_SH_##sfs;	     \
		  _FP_FRAC_SRS_##swc(S, (_FP_WFRACBITS_##sfs		     \
					 - _FP_WFRACBITS_##dfs + 1 - D##_e), \
				     _FP_WFRACBITS_##sfs);		     \
		}							     \
	      D##_e = 0;						     \
	    }								     \
	  else								     \
	    _FP_FRAC_SRS_##swc(S, (_FP_WFRACBITS_##sfs			     \
				   - _FP_WFRACBITS_##dfs),		     \
			       _FP_WFRACBITS_##sfs);			     \
	  _FP_FRAC_COPY_##dwc##_##swc(D, S);				     \
	}								     \
    }									     \
  else									     \
    {									     \
      if (S##_e == 0)							     \
	{								     \
	  D##_e = 0;							     \
	  if (_FP_FRAC_ZEROP_##swc(S))					     \
	    _FP_FRAC_SET_##dwc(D, _FP_ZEROFRAC_##dwc);			     \
	  else								     \
	    {								     \
	      FP_SET_EXCEPTION(FP_EX_DENORM);				     \
	      if (_FP_EXPBIAS_##sfs					     \
		  < _FP_EXPBIAS_##dfs + _FP_FRACBITS_##dfs - 1)		     \
		{							     \
		  _FP_FRAC_SRS_##swc(S, (_FP_WFRACBITS_##sfs		     \
					 - _FP_WFRACBITS_##dfs),	     \
				     _FP_WFRACBITS_##sfs);		     \
		  _FP_FRAC_COPY_##dwc##_##swc(D, S);			     \
		}							     \
	      else							     \
		{							     \
		  _FP_FRAC_SET_##dwc(D, _FP_ZEROFRAC_##dwc);		     \
		  _FP_FRAC_LOW_##dwc(D) |= 1;				     \
		}							     \
	    }								     \
	}								     \
      else								     \
	{								     \
	  D##_e = _FP_EXPMAX_##dfs;					     \
	  if (_FP_FRAC_ZEROP_##swc(S))					     \
	    _FP_FRAC_SET_##dwc(D, _FP_ZEROFRAC_##dwc);			     \
	  else								     \
	    {								     \
	      _FP_CHECK_SIGNAN_SEMIRAW(sfs, swc, S);			     \
	      _FP_FRAC_SRL_##swc(S, (_FP_WFRACBITS_##sfs		     \
				     - _FP_WFRACBITS_##dfs));		     \
	      _FP_FRAC_COPY_##dwc##_##swc(D, S);			     \
	      /* Semi-raw NaN must have all workbits cleared.  */	     \
	      _FP_FRAC_LOW_##dwc(D)					     \
		&= ~(_FP_W_TYPE) ((1 << _FP_WORKBITS) - 1);		     \
	      _FP_FRAC_HIGH_##dfs(D) |= _FP_QNANBIT_SH_##dfs;		     \
	    }								     \
	}								     \
    }									     \
} while (0)

/*
 * Helper primitives.
 */

/* Count leading zeros in a word.  */

#ifndef __FP_CLZ
/* GCC 3.4 and later provide the builtins for us.  */
#define __FP_CLZ(r, x)							      \
  do {									      \
    if (sizeof (_FP_W_TYPE) == sizeof (unsigned int))			      \
      r = __builtin_clz (x);						      \
    else if (sizeof (_FP_W_TYPE) == sizeof (unsigned long))		      \
      r = __builtin_clzl (x);						      \
    else if (sizeof (_FP_W_TYPE) == sizeof (unsigned long long))	      \
      r = __builtin_clzll (x);						      \
    else								      \
      abort ();								      \
  } while (0)
#endif /* ndef __FP_CLZ */

#define _FP_DIV_HELP_imm(q, r, n, d)		\
  do {						\
    q = n / d, r = n % d;			\
  } while (0)


/* A restoring bit-by-bit division primitive.  */

#define _FP_DIV_MEAT_N_loop(fs, wc, R, X, Y)				\
  do {									\
    int count = _FP_WFRACBITS_##fs;					\
    _FP_FRAC_DECL_##wc (u);						\
    _FP_FRAC_DECL_##wc (v);						\
    _FP_FRAC_COPY_##wc (u, X);						\
    _FP_FRAC_COPY_##wc (v, Y);						\
    _FP_FRAC_SET_##wc (R, _FP_ZEROFRAC_##wc);				\
    /* Normalize U and V.  */						\
    _FP_FRAC_SLL_##wc (u, _FP_WFRACXBITS_##fs);				\
    _FP_FRAC_SLL_##wc (v, _FP_WFRACXBITS_##fs);				\
    /* First round.  Since the operands are normalized, either the	\
       first or second bit will be set in the fraction.  Produce a	\
       normalized result by checking which and adjusting the loop	\
       count and exponent accordingly.  */				\
    if (_FP_FRAC_GE_1 (u, v))						\
      {									\
	_FP_FRAC_SUB_##wc (u, u, v);					\
	_FP_FRAC_LOW_##wc (R) |= 1;					\
	count--;							\
      }									\
    else								\
      R##_e--;								\
    /* Subsequent rounds.  */						\
    do {								\
      int msb = (_FP_WS_TYPE) _FP_FRAC_HIGH_##wc (u) < 0;		\
      _FP_FRAC_SLL_##wc (u, 1);						\
      _FP_FRAC_SLL_##wc (R, 1);						\
      if (msb || _FP_FRAC_GE_1 (u, v))					\
	{								\
	  _FP_FRAC_SUB_##wc (u, u, v);					\
	  _FP_FRAC_LOW_##wc (R) |= 1;					\
	}								\
    } while (--count > 0);						\
    /* If there's anything left in U, the result is inexact.  */	\
    _FP_FRAC_LOW_##wc (R) |= !_FP_FRAC_ZEROP_##wc (u);			\
  } while (0)

#define _FP_DIV_MEAT_1_loop(fs, R, X, Y)  _FP_DIV_MEAT_N_loop (fs, 1, R, X, Y)
#define _FP_DIV_MEAT_2_loop(fs, R, X, Y)  _FP_DIV_MEAT_N_loop (fs, 2, R, X, Y)
#define _FP_DIV_MEAT_4_loop(fs, R, X, Y)  _FP_DIV_MEAT_N_loop (fs, 4, R, X, Y)
