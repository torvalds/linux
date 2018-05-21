/******************************************************************************
 *
 * Copyright(c) 2007 - 2017 Realtek Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 *****************************************************************************/
#ifndef _LINUX_BYTEORDER_SWAB_H
#define _LINUX_BYTEORDER_SWAB_H

#if !defined(CONFIG_PLATFORM_MSTAR)
#ifndef __u16
	typedef unsigned short __u16;
#endif

#ifndef __u32
	typedef unsigned int	__u32;
#endif

#ifndef __u8
	typedef unsigned char __u8;
#endif

#ifndef __u64
	typedef unsigned long long	__u64;
#endif


__inline static __u16  ___swab16(__u16 x)
{
	__u16 __x = x;
	return
		 (__u16)(
			 (((__u16)(__x)&(__u16)0x00ffU) << 8) |
			 (((__u16)(__x)&(__u16)0xff00U) >> 8));

}

__inline static __u32  ___swab32(__u32 x)
{
	__u32 __x = (x);
	return  (__u32)(
			(((__u32)(__x)&(__u32)0x000000ffUL) << 24) |
			(((__u32)(__x)&(__u32)0x0000ff00UL) <<  8) |
			(((__u32)(__x)&(__u32)0x00ff0000UL) >>  8) |
			(((__u32)(__x)&(__u32)0xff000000UL) >> 24));
}

__inline static __u64  ___swab64(__u64 x)
{
	__u64 __x = (x);

	return
		 (__u64)(\
		 (__u64)(((__u64)(__x)&(__u64)0x00000000000000ffULL) << 56) | \
		 (__u64)(((__u64)(__x)&(__u64)0x000000000000ff00ULL) << 40) | \
		 (__u64)(((__u64)(__x)&(__u64)0x0000000000ff0000ULL) << 24) | \
		 (__u64)(((__u64)(__x)&(__u64)0x00000000ff000000ULL) <<  8) | \
		 (__u64)(((__u64)(__x)&(__u64)0x000000ff00000000ULL) >>  8) | \
		 (__u64)(((__u64)(__x)&(__u64)0x0000ff0000000000ULL) >> 24) | \
		 (__u64)(((__u64)(__x)&(__u64)0x00ff000000000000ULL) >> 40) | \
		 (__u64)(((__u64)(__x)&(__u64)0xff00000000000000ULL) >> 56));
	\
}
#endif /* CONFIG_PLATFORM_MSTAR */

#ifndef __arch__swab16
__inline static __u16 __arch__swab16(__u16 x)
{
	return ___swab16(x);
}

#endif

#ifndef __arch__swab32
__inline static __u32 __arch__swab32(__u32 x)
{
	__u32 __tmp = (x) ;
	return ___swab32(__tmp);
}
#endif

#ifndef __arch__swab64

__inline static __u64 __arch__swab64(__u64 x)
{
	__u64 __tmp = (x) ;
	return ___swab64(__tmp);
}


#endif

#ifndef __swab16
	#define __swab16(x) __fswab16(x)
	#define __swab32(x) __fswab32(x)
	#define __swab64(x) __fswab64(x)
#endif /* __swab16 */

#ifdef PLATFORM_FREEBSD
	__inline static __u16 __fswab16(__u16 x)
#else
	__inline static const __u16 __fswab16(__u16 x)
#endif /* PLATFORM_FREEBSD */
{
	return __arch__swab16(x);
}
#ifdef PLATFORM_FREEBSD
	__inline static __u32 __fswab32(__u32 x)
#else
	__inline static const __u32 __fswab32(__u32 x)
#endif /* PLATFORM_FREEBSD */
{
	return __arch__swab32(x);
}

#if defined(PLATFORM_LINUX) || defined(PLATFORM_WINDOWS)
	#define swab16 __swab16
	#define swab32 __swab32
	#define swab64 __swab64
	#define swab16p __swab16p
	#define swab32p __swab32p
	#define swab64p __swab64p
	#define swab16s __swab16s
	#define swab32s __swab32s
	#define swab64s __swab64s
#endif

#endif /* _LINUX_BYTEORDER_SWAB_H */
