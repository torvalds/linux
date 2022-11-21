#ifndef __LKL_BITSPERLONG_H
#define __LKL_BITSPERLONG_H

#include <uapi/asm/bitsperlong.h>

#define BITS_PER_LONG __BITS_PER_LONG

#define BITS_PER_LONG_LONG 64

#define small_const_nbits(nbits) \
	(__builtin_constant_p(nbits) && (nbits) <= BITS_PER_LONG && (nbits) > 0)

#endif

