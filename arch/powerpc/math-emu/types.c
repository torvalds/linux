#include "soft-fp.h"
#include "double.h"
#include "single.h"

void
fp_unpack_d(long *_s, unsigned long *_f1, unsigned long *_f0,
	    long *_e, long *_c, void *val)
{
	FP_DECL_D(X);

	__FP_UNPACK_RAW_2(D, X, val);

	_FP_UNPACK_CANONICAL(D, 2, X);

	*_s = X_s;
	*_f1 = X_f1;
	*_f0 = X_f0;
	*_e = X_e;
	*_c = X_c;
}

int
fp_pack_d(void *val, long X_s, unsigned long X_f1,
	  unsigned long X_f0, long X_e, long X_c)
{
	int exc;

	exc = _FP_PACK_CANONICAL(D, 2, X);
	if (!exc || !__FPU_TRAP_P(exc))
		__FP_PACK_RAW_2(D, val, X);
	return exc;
}

int
fp_pack_ds(void *val, long X_s, unsigned long X_f1,
	   unsigned long X_f0, long X_e, long X_c)
{
	FP_DECL_S(__X);
	int exc;

	FP_CONV(S, D, 1, 2, __X, X);
	exc = _FP_PACK_CANONICAL(S, 1, __X);
	if (!exc || !__FPU_TRAP_P(exc)) {
		_FP_UNPACK_CANONICAL(S, 1, __X);
		FP_CONV(D, S, 2, 1, X, __X);
		exc |= _FP_PACK_CANONICAL(D, 2, X);
		if (!exc || !__FPU_TRAP_P(exc))
			__FP_PACK_RAW_2(D, val, X);
	}
	return exc;
}
