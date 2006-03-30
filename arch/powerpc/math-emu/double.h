/*
 * Definitions for IEEE Double Precision
 */

#if _FP_W_TYPE_SIZE < 32
#error "Here's a nickel kid.  Go buy yourself a real computer."
#endif

#if _FP_W_TYPE_SIZE < 64
#define _FP_FRACTBITS_D		(2 * _FP_W_TYPE_SIZE)
#else
#define _FP_FRACTBITS_D		_FP_W_TYPE_SIZE
#endif

#define _FP_FRACBITS_D		53
#define _FP_FRACXBITS_D		(_FP_FRACTBITS_D - _FP_FRACBITS_D)
#define _FP_WFRACBITS_D		(_FP_WORKBITS + _FP_FRACBITS_D)
#define _FP_WFRACXBITS_D	(_FP_FRACTBITS_D - _FP_WFRACBITS_D)
#define _FP_EXPBITS_D		11
#define _FP_EXPBIAS_D		1023
#define _FP_EXPMAX_D		2047

#define _FP_QNANBIT_D		\
	((_FP_W_TYPE)1 << ((_FP_FRACBITS_D-2) % _FP_W_TYPE_SIZE))
#define _FP_IMPLBIT_D		\
	((_FP_W_TYPE)1 << ((_FP_FRACBITS_D-1) % _FP_W_TYPE_SIZE))
#define _FP_OVERFLOW_D		\
	((_FP_W_TYPE)1 << (_FP_WFRACBITS_D % _FP_W_TYPE_SIZE))

#if _FP_W_TYPE_SIZE < 64

union _FP_UNION_D
{
  double flt;
  struct {
#if __BYTE_ORDER == __BIG_ENDIAN
    unsigned sign  : 1;
    unsigned exp   : _FP_EXPBITS_D;
    unsigned frac1 : _FP_FRACBITS_D - (_FP_IMPLBIT_D != 0) - _FP_W_TYPE_SIZE;
    unsigned frac0 : _FP_W_TYPE_SIZE;
#else
    unsigned frac0 : _FP_W_TYPE_SIZE;
    unsigned frac1 : _FP_FRACBITS_D - (_FP_IMPLBIT_D != 0) - _FP_W_TYPE_SIZE;
    unsigned exp   : _FP_EXPBITS_D;
    unsigned sign  : 1;
#endif
  } bits __attribute__((packed));
};

#define FP_DECL_D(X)		_FP_DECL(2,X)
#define FP_UNPACK_RAW_D(X,val)	_FP_UNPACK_RAW_2(D,X,val)
#define FP_PACK_RAW_D(val,X)	_FP_PACK_RAW_2(D,val,X)

#define FP_UNPACK_D(X,val)		\
  do {					\
    _FP_UNPACK_RAW_2(D,X,val);		\
    _FP_UNPACK_CANONICAL(D,2,X);	\
  } while (0)

#define FP_PACK_D(val,X)		\
  do {					\
    _FP_PACK_CANONICAL(D,2,X);		\
    _FP_PACK_RAW_2(D,val,X);		\
  } while (0)

#define FP_NEG_D(R,X)		_FP_NEG(D,2,R,X)
#define FP_ADD_D(R,X,Y)		_FP_ADD(D,2,R,X,Y)
#define FP_SUB_D(R,X,Y)		_FP_SUB(D,2,R,X,Y)
#define FP_MUL_D(R,X,Y)		_FP_MUL(D,2,R,X,Y)
#define FP_DIV_D(R,X,Y)		_FP_DIV(D,2,R,X,Y)
#define FP_SQRT_D(R,X)		_FP_SQRT(D,2,R,X)

#define FP_CMP_D(r,X,Y,un)	_FP_CMP(D,2,r,X,Y,un)
#define FP_CMP_EQ_D(r,X,Y)	_FP_CMP_EQ(D,2,r,X,Y)

#define FP_TO_INT_D(r,X,rsz,rsg)  _FP_TO_INT(D,2,r,X,rsz,rsg)
#define FP_FROM_INT_D(X,r,rs,rt)  _FP_FROM_INT(D,2,X,r,rs,rt)

#else

union _FP_UNION_D
{
  double flt;
  struct {
#if __BYTE_ORDER == __BIG_ENDIAN
    unsigned sign : 1;
    unsigned exp  : _FP_EXPBITS_D;
    unsigned long frac : _FP_FRACBITS_D - (_FP_IMPLBIT_D != 0);
#else
    unsigned long frac : _FP_FRACBITS_D - (_FP_IMPLBIT_D != 0);
    unsigned exp  : _FP_EXPBITS_D;
    unsigned sign : 1;
#endif
  } bits __attribute__((packed));
};

#define FP_DECL_D(X)		_FP_DECL(1,X)
#define FP_UNPACK_RAW_D(X,val)	_FP_UNPACK_RAW_1(D,X,val)
#define FP_PACK_RAW_D(val,X)	_FP_PACK_RAW_1(D,val,X)

#define FP_UNPACK_D(X,val)		\
  do {					\
    _FP_UNPACK_RAW_1(D,X,val);		\
    _FP_UNPACK_CANONICAL(D,1,X);	\
  } while (0)

#define FP_PACK_D(val,X)		\
  do {					\
    _FP_PACK_CANONICAL(D,1,X);		\
    _FP_PACK_RAW_1(D,val,X);		\
  } while (0)

#define FP_NEG_D(R,X)		_FP_NEG(D,1,R,X)
#define FP_ADD_D(R,X,Y)		_FP_ADD(D,1,R,X,Y)
#define FP_SUB_D(R,X,Y)		_FP_SUB(D,1,R,X,Y)
#define FP_MUL_D(R,X,Y)		_FP_MUL(D,1,R,X,Y)
#define FP_DIV_D(R,X,Y)		_FP_DIV(D,1,R,X,Y)
#define FP_SQRT_D(R,X)		_FP_SQRT(D,1,R,X)

/* The implementation of _FP_MUL_D and _FP_DIV_D should be chosen by
   the target machine.  */

#define FP_CMP_D(r,X,Y,un)	_FP_CMP(D,1,r,X,Y,un)
#define FP_CMP_EQ_D(r,X,Y)	_FP_CMP_EQ(D,1,r,X,Y)

#define FP_TO_INT_D(r,X,rsz,rsg)  _FP_TO_INT(D,1,r,X,rsz,rsg)
#define FP_FROM_INT_D(X,r,rs,rt)  _FP_FROM_INT(D,1,X,r,rs,rt)

#endif /* W_TYPE_SIZE < 64 */
