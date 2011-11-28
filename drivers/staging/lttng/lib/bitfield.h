#ifndef _BABELTRACE_BITFIELD_H
#define _BABELTRACE_BITFIELD_H

/*
 * BabelTrace
 *
 * Bitfields read/write functions.
 *
 * Copyright 2010 - Mathieu Desnoyers <mathieu.desnoyers@efficios.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 */

#include "../ltt-endian.h"

#ifndef CHAR_BIT
#define CHAR_BIT 8
#endif

/* We can't shift a int from 32 bit, >> 32 and << 32 on int is undefined */
#define _bt_piecewise_rshift(_v, _shift)				\
({									\
	typeof(_v) ___v = (_v);						\
	typeof(_shift) ___shift = (_shift);				\
	unsigned long sb = (___shift) / (sizeof(___v) * CHAR_BIT - 1);	\
	unsigned long final = (___shift) % (sizeof(___v) * CHAR_BIT - 1); \
									\
	for (; sb; sb--)						\
		___v >>= sizeof(___v) * CHAR_BIT - 1;			\
	___v >>= final;							\
})

#define _bt_piecewise_lshift(_v, _shift)				\
({									\
	typeof(_v) ___v = (_v);						\
	typeof(_shift) ___shift = (_shift);				\
	unsigned long sb = (___shift) / (sizeof(___v) * CHAR_BIT - 1);	\
	unsigned long final = (___shift) % (sizeof(___v) * CHAR_BIT - 1); \
									\
	for (; sb; sb--)						\
		___v <<= sizeof(___v) * CHAR_BIT - 1;			\
	___v <<= final;							\
})

#define _bt_is_signed_type(type)	(((type)(-1)) < 0)

#define _bt_unsigned_cast(type, v)					\
({									\
	(sizeof(v) < sizeof(type)) ?					\
		((type) (v)) & (~(~(type) 0 << (sizeof(v) * CHAR_BIT))) : \
		(type) (v);						\
})

/*
 * bt_bitfield_write - write integer to a bitfield in native endianness
 *
 * Save integer to the bitfield, which starts at the "start" bit, has "len"
 * bits.
 * The inside of a bitfield is from high bits to low bits.
 * Uses native endianness.
 * For unsigned "v", pad MSB with 0 if bitfield is larger than v.
 * For signed "v", sign-extend v if bitfield is larger than v.
 *
 * On little endian, bytes are placed from the less significant to the most
 * significant. Also, consecutive bitfields are placed from lower bits to higher
 * bits.
 *
 * On big endian, bytes are places from most significant to less significant.
 * Also, consecutive bitfields are placed from higher to lower bits.
 */

#define _bt_bitfield_write_le(_ptr, type, _start, _length, _v)		\
do {									\
	typeof(_v) __v = (_v);						\
	type *__ptr = (void *) (_ptr);					\
	unsigned long __start = (_start), __length = (_length);		\
	type mask, cmask;						\
	unsigned long ts = sizeof(type) * CHAR_BIT; /* type size */	\
	unsigned long start_unit, end_unit, this_unit;			\
	unsigned long end, cshift; /* cshift is "complement shift" */	\
									\
	if (!__length)							\
		break;							\
									\
	end = __start + __length;					\
	start_unit = __start / ts;					\
	end_unit = (end + (ts - 1)) / ts;				\
									\
	/* Trim v high bits */						\
	if (__length < sizeof(__v) * CHAR_BIT)				\
		__v &= ~((~(typeof(__v)) 0) << __length);		\
									\
	/* We can now append v with a simple "or", shift it piece-wise */ \
	this_unit = start_unit;						\
	if (start_unit == end_unit - 1) {				\
		mask = ~((~(type) 0) << (__start % ts));		\
		if (end % ts)						\
			mask |= (~(type) 0) << (end % ts);		\
		cmask = (type) __v << (__start % ts);			\
		cmask &= ~mask;						\
		__ptr[this_unit] &= mask;				\
		__ptr[this_unit] |= cmask;				\
		break;							\
	}								\
	if (__start % ts) {						\
		cshift = __start % ts;					\
		mask = ~((~(type) 0) << cshift);			\
		cmask = (type) __v << cshift;				\
		cmask &= ~mask;						\
		__ptr[this_unit] &= mask;				\
		__ptr[this_unit] |= cmask;				\
		__v = _bt_piecewise_rshift(__v, ts - cshift);		\
		__start += ts - cshift;					\
		this_unit++;						\
	}								\
	for (; this_unit < end_unit - 1; this_unit++) {			\
		__ptr[this_unit] = (type) __v;				\
		__v = _bt_piecewise_rshift(__v, ts);			\
		__start += ts;						\
	}								\
	if (end % ts) {							\
		mask = (~(type) 0) << (end % ts);			\
		cmask = (type) __v;					\
		cmask &= ~mask;						\
		__ptr[this_unit] &= mask;				\
		__ptr[this_unit] |= cmask;				\
	} else								\
		__ptr[this_unit] = (type) __v;				\
} while (0)

#define _bt_bitfield_write_be(_ptr, type, _start, _length, _v)		\
do {									\
	typeof(_v) __v = (_v);						\
	type *__ptr = (void *) (_ptr);					\
	unsigned long __start = (_start), __length = (_length);		\
	type mask, cmask;						\
	unsigned long ts = sizeof(type) * CHAR_BIT; /* type size */	\
	unsigned long start_unit, end_unit, this_unit;			\
	unsigned long end, cshift; /* cshift is "complement shift" */	\
									\
	if (!__length)							\
		break;							\
									\
	end = __start + __length;					\
	start_unit = __start / ts;					\
	end_unit = (end + (ts - 1)) / ts;				\
									\
	/* Trim v high bits */						\
	if (__length < sizeof(__v) * CHAR_BIT)				\
		__v &= ~((~(typeof(__v)) 0) << __length);		\
									\
	/* We can now append v with a simple "or", shift it piece-wise */ \
	this_unit = end_unit - 1;					\
	if (start_unit == end_unit - 1) {				\
		mask = ~((~(type) 0) << ((ts - (end % ts)) % ts));	\
		if (__start % ts)					\
			mask |= (~((type) 0)) << (ts - (__start % ts));	\
		cmask = (type) __v << ((ts - (end % ts)) % ts);		\
		cmask &= ~mask;						\
		__ptr[this_unit] &= mask;				\
		__ptr[this_unit] |= cmask;				\
		break;							\
	}								\
	if (end % ts) {							\
		cshift = end % ts;					\
		mask = ~((~(type) 0) << (ts - cshift));			\
		cmask = (type) __v << (ts - cshift);			\
		cmask &= ~mask;						\
		__ptr[this_unit] &= mask;				\
		__ptr[this_unit] |= cmask;				\
		__v = _bt_piecewise_rshift(__v, cshift);		\
		end -= cshift;						\
		this_unit--;						\
	}								\
	for (; (long) this_unit >= (long) start_unit + 1; this_unit--) { \
		__ptr[this_unit] = (type) __v;				\
		__v = _bt_piecewise_rshift(__v, ts);			\
		end -= ts;						\
	}								\
	if (__start % ts) {						\
		mask = (~(type) 0) << (ts - (__start % ts));		\
		cmask = (type) __v;					\
		cmask &= ~mask;						\
		__ptr[this_unit] &= mask;				\
		__ptr[this_unit] |= cmask;				\
	} else								\
		__ptr[this_unit] = (type) __v;				\
} while (0)

/*
 * bt_bitfield_write - write integer to a bitfield in native endianness
 * bt_bitfield_write_le - write integer to a bitfield in little endian
 * bt_bitfield_write_be - write integer to a bitfield in big endian
 */

#if (__BYTE_ORDER == __LITTLE_ENDIAN)

#define bt_bitfield_write(ptr, type, _start, _length, _v)		\
	_bt_bitfield_write_le(ptr, type, _start, _length, _v)

#define bt_bitfield_write_le(ptr, type, _start, _length, _v)		\
	_bt_bitfield_write_le(ptr, type, _start, _length, _v)
	
#define bt_bitfield_write_be(ptr, type, _start, _length, _v)		\
	_bt_bitfield_write_be(ptr, unsigned char, _start, _length, _v)

#elif (__BYTE_ORDER == __BIG_ENDIAN)

#define bt_bitfield_write(ptr, type, _start, _length, _v)		\
	_bt_bitfield_write_be(ptr, type, _start, _length, _v)

#define bt_bitfield_write_le(ptr, type, _start, _length, _v)		\
	_bt_bitfield_write_le(ptr, unsigned char, _start, _length, _v)
	
#define bt_bitfield_write_be(ptr, type, _start, _length, _v)		\
	_bt_bitfield_write_be(ptr, type, _start, _length, _v)

#else /* (BYTE_ORDER == PDP_ENDIAN) */

#error "Byte order not supported"

#endif

#define _bt_bitfield_read_le(_ptr, type, _start, _length, _vptr)	\
do {									\
	typeof(*(_vptr)) *__vptr = (_vptr);				\
	typeof(*__vptr) __v;						\
	type *__ptr = (void *) (_ptr);					\
	unsigned long __start = (_start), __length = (_length);		\
	type mask, cmask;						\
	unsigned long ts = sizeof(type) * CHAR_BIT; /* type size */	\
	unsigned long start_unit, end_unit, this_unit;			\
	unsigned long end, cshift; /* cshift is "complement shift" */	\
									\
	if (!__length) {						\
		*__vptr = 0;						\
		break;							\
	}								\
									\
	end = __start + __length;					\
	start_unit = __start / ts;					\
	end_unit = (end + (ts - 1)) / ts;				\
									\
	this_unit = end_unit - 1;					\
	if (_bt_is_signed_type(typeof(__v))				\
	    && (__ptr[this_unit] & ((type) 1 << ((end % ts ? : ts) - 1)))) \
		__v = ~(typeof(__v)) 0;					\
	else								\
		__v = 0;						\
	if (start_unit == end_unit - 1) {				\
		cmask = __ptr[this_unit];				\
		cmask >>= (__start % ts);				\
		if ((end - __start) % ts) {				\
			mask = ~((~(type) 0) << (end - __start));	\
			cmask &= mask;					\
		}							\
		__v = _bt_piecewise_lshift(__v, end - __start);		\
		__v |= _bt_unsigned_cast(typeof(__v), cmask);		\
		*__vptr = __v;						\
		break;							\
	}								\
	if (end % ts) {							\
		cshift = end % ts;					\
		mask = ~((~(type) 0) << cshift);			\
		cmask = __ptr[this_unit];				\
		cmask &= mask;						\
		__v = _bt_piecewise_lshift(__v, cshift);		\
		__v |= _bt_unsigned_cast(typeof(__v), cmask);		\
		end -= cshift;						\
		this_unit--;						\
	}								\
	for (; (long) this_unit >= (long) start_unit + 1; this_unit--) { \
		__v = _bt_piecewise_lshift(__v, ts);			\
		__v |= _bt_unsigned_cast(typeof(__v), __ptr[this_unit]);\
		end -= ts;						\
	}								\
	if (__start % ts) {						\
		mask = ~((~(type) 0) << (ts - (__start % ts)));		\
		cmask = __ptr[this_unit];				\
		cmask >>= (__start % ts);				\
		cmask &= mask;						\
		__v = _bt_piecewise_lshift(__v, ts - (__start % ts));	\
		__v |= _bt_unsigned_cast(typeof(__v), cmask);		\
	} else {							\
		__v = _bt_piecewise_lshift(__v, ts);			\
		__v |= _bt_unsigned_cast(typeof(__v), __ptr[this_unit]);\
	}								\
	*__vptr = __v;							\
} while (0)

#define _bt_bitfield_read_be(_ptr, type, _start, _length, _vptr)	\
do {									\
	typeof(*(_vptr)) *__vptr = (_vptr);				\
	typeof(*__vptr) __v;						\
	type *__ptr = (void *) (_ptr);					\
	unsigned long __start = (_start), __length = (_length);		\
	type mask, cmask;						\
	unsigned long ts = sizeof(type) * CHAR_BIT; /* type size */	\
	unsigned long start_unit, end_unit, this_unit;			\
	unsigned long end, cshift; /* cshift is "complement shift" */	\
									\
	if (!__length) {						\
		*__vptr = 0;						\
		break;							\
	}								\
									\
	end = __start + __length;					\
	start_unit = __start / ts;					\
	end_unit = (end + (ts - 1)) / ts;				\
									\
	this_unit = start_unit;						\
	if (_bt_is_signed_type(typeof(__v))				\
	    && (__ptr[this_unit] & ((type) 1 << (ts - (__start % ts) - 1)))) \
		__v = ~(typeof(__v)) 0;					\
	else								\
		__v = 0;						\
	if (start_unit == end_unit - 1) {				\
		cmask = __ptr[this_unit];				\
		cmask >>= (ts - (end % ts)) % ts;			\
		if ((end - __start) % ts) {				\
			mask = ~((~(type) 0) << (end - __start));	\
			cmask &= mask;					\
		}							\
		__v = _bt_piecewise_lshift(__v, end - __start);		\
		__v |= _bt_unsigned_cast(typeof(__v), cmask);		\
		*__vptr = __v;						\
		break;							\
	}								\
	if (__start % ts) {						\
		cshift = __start % ts;					\
		mask = ~((~(type) 0) << (ts - cshift));			\
		cmask = __ptr[this_unit];				\
		cmask &= mask;						\
		__v = _bt_piecewise_lshift(__v, ts - cshift);		\
		__v |= _bt_unsigned_cast(typeof(__v), cmask);		\
		__start += ts - cshift;					\
		this_unit++;						\
	}								\
	for (; this_unit < end_unit - 1; this_unit++) {			\
		__v = _bt_piecewise_lshift(__v, ts);			\
		__v |= _bt_unsigned_cast(typeof(__v), __ptr[this_unit]);\
		__start += ts;						\
	}								\
	if (end % ts) {							\
		mask = ~((~(type) 0) << (end % ts));			\
		cmask = __ptr[this_unit];				\
		cmask >>= ts - (end % ts);				\
		cmask &= mask;						\
		__v = _bt_piecewise_lshift(__v, end % ts);		\
		__v |= _bt_unsigned_cast(typeof(__v), cmask);		\
	} else {							\
		__v = _bt_piecewise_lshift(__v, ts);			\
		__v |= _bt_unsigned_cast(typeof(__v), __ptr[this_unit]);\
	}								\
	*__vptr = __v;							\
} while (0)

/*
 * bt_bitfield_read - read integer from a bitfield in native endianness
 * bt_bitfield_read_le - read integer from a bitfield in little endian
 * bt_bitfield_read_be - read integer from a bitfield in big endian
 */

#if (__BYTE_ORDER == __LITTLE_ENDIAN)

#define bt_bitfield_read(_ptr, type, _start, _length, _vptr)		\
	_bt_bitfield_read_le(_ptr, type, _start, _length, _vptr)

#define bt_bitfield_read_le(_ptr, type, _start, _length, _vptr)		\
	_bt_bitfield_read_le(_ptr, type, _start, _length, _vptr)
	
#define bt_bitfield_read_be(_ptr, type, _start, _length, _vptr)		\
	_bt_bitfield_read_be(_ptr, unsigned char, _start, _length, _vptr)

#elif (__BYTE_ORDER == __BIG_ENDIAN)

#define bt_bitfield_read(_ptr, type, _start, _length, _vptr)		\
	_bt_bitfield_read_be(_ptr, type, _start, _length, _vptr)

#define bt_bitfield_read_le(_ptr, type, _start, _length, _vptr)		\
	_bt_bitfield_read_le(_ptr, unsigned char, _start, _length, _vptr)
	
#define bt_bitfield_read_be(_ptr, type, _start, _length, _vptr)		\
	_bt_bitfield_read_be(_ptr, type, _start, _length, _vptr)

#else /* (__BYTE_ORDER == __PDP_ENDIAN) */

#error "Byte order not supported"

#endif

#endif /* _BABELTRACE_BITFIELD_H */
