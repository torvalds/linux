#ifndef __DML_INLINE_DEFS_H__
#define __DML_INLINE_DEFS_H__
#include "dml_common_defs.h"
#include "../calcs/dcn_calc_math.h"

static inline double dml_min(double a, double b)
{
	return (double) dcn_bw_min2(a, b);
}

static inline double dml_max(double a, double b)
{
	return (double) dcn_bw_max2(a, b);
}

static inline double dml_ceil(double a)
{
	return (double) dcn_bw_ceil2(a, 1);
}

static inline double dml_floor(double a)
{
	return (double) dcn_bw_floor2(a, 1);
}

static inline int dml_log2(double x)
{
	return dml_round((double)dcn_bw_log(x, 2));
}

static inline double dml_pow(double a, int exp)
{
	return (double) dcn_bw_pow(a, exp);
}

static inline double dml_fmod(double f, int val)
{
	return (double) dcn_bw_mod(f, val);
}

static inline double dml_ceil_2(double f)
{
	return (double) dcn_bw_ceil2(f, 2);
}

static inline double dml_ceil_ex(double x, double granularity)
{
	return (double) dcn_bw_ceil2(x, granularity);
}

static inline double dml_floor_ex(double x, double granularity)
{
	return (double) dcn_bw_floor2(x, granularity);
}

static inline double dml_log(double x, double base)
{
	return (double) dcn_bw_log(x, base);
}

#endif
