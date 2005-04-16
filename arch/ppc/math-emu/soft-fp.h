#ifndef SOFT_FP_H
#define SOFT_FP_H

#include "sfp-machine.h"

#define _FP_WORKBITS		3
#define _FP_WORK_LSB		((_FP_W_TYPE)1 << 3)
#define _FP_WORK_ROUND		((_FP_W_TYPE)1 << 2)
#define _FP_WORK_GUARD		((_FP_W_TYPE)1 << 1)
#define _FP_WORK_STICKY		((_FP_W_TYPE)1 << 0)

#ifndef FP_RND_NEAREST
# define FP_RND_NEAREST		0
# define FP_RND_ZERO		1
# define FP_RND_PINF		2
# define FP_RND_MINF		3
#ifndef FP_ROUNDMODE
# define FP_ROUNDMODE		FP_RND_NEAREST
#endif
#endif

#define _FP_ROUND_NEAREST(wc, X)			\
({  int __ret = 0;					\
    int __frac = _FP_FRAC_LOW_##wc(X) & 15;		\
    if (__frac & 7) {					\
      __ret = EFLAG_INEXACT;				\
      if ((__frac & 7) != _FP_WORK_ROUND)		\
        _FP_FRAC_ADDI_##wc(X, _FP_WORK_ROUND);		\
      else if (__frac & _FP_WORK_LSB)			\
        _FP_FRAC_ADDI_##wc(X, _FP_WORK_ROUND);		\
    }							\
    __ret;						\
})

#define _FP_ROUND_ZERO(wc, X)				\
({  int __ret = 0;					\
    if (_FP_FRAC_LOW_##wc(X) & 7)			\
      __ret = EFLAG_INEXACT;				\
    __ret;						\
})

#define _FP_ROUND_PINF(wc, X)				\
({  int __ret = EFLAG_INEXACT;				\
    if (!X##_s && (_FP_FRAC_LOW_##wc(X) & 7))		\
      _FP_FRAC_ADDI_##wc(X, _FP_WORK_LSB);		\
    else __ret = 0;					\
    __ret;						\
})

#define _FP_ROUND_MINF(wc, X)				\
({  int __ret = EFLAG_INEXACT;				\
    if (X##_s && (_FP_FRAC_LOW_##wc(X) & 7))		\
      _FP_FRAC_ADDI_##wc(X, _FP_WORK_LSB);		\
    else __ret = 0;					\
    __ret;						\
})

#define _FP_ROUND(wc, X)			\
({	int __ret = 0;				\
	switch (FP_ROUNDMODE)			\
	{					\
	  case FP_RND_NEAREST:			\
	    __ret |= _FP_ROUND_NEAREST(wc,X);	\
	    break;				\
	  case FP_RND_ZERO:			\
	    __ret |= _FP_ROUND_ZERO(wc,X);	\
	    break;				\
	  case FP_RND_PINF:			\
	    __ret |= _FP_ROUND_PINF(wc,X);	\
	    break;				\
	  case FP_RND_MINF:			\
	    __ret |= _FP_ROUND_MINF(wc,X);	\
	    break;				\
	};					\
	__ret;					\
})

#define FP_CLS_NORMAL		0
#define FP_CLS_ZERO		1
#define FP_CLS_INF		2
#define FP_CLS_NAN		3

#define _FP_CLS_COMBINE(x,y)	(((x) << 2) | (y))

#include "op-1.h"
#include "op-2.h"
#include "op-4.h"
#include "op-common.h"

/* Sigh.  Silly things longlong.h needs.  */
#define UWtype		_FP_W_TYPE
#define W_TYPE_SIZE	_FP_W_TYPE_SIZE

typedef int SItype __attribute__((mode(SI)));
typedef int DItype __attribute__((mode(DI)));
typedef unsigned int USItype __attribute__((mode(SI)));
typedef unsigned int UDItype __attribute__((mode(DI)));
#if _FP_W_TYPE_SIZE == 32
typedef unsigned int UHWtype __attribute__((mode(HI)));
#elif _FP_W_TYPE_SIZE == 64
typedef USItype UHWtype;
#endif

#endif
