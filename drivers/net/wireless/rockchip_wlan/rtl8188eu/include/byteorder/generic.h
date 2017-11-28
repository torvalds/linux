/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
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
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 *
 ******************************************************************************/
#ifndef _LINUX_BYTEORDER_GENERIC_H
#define _LINUX_BYTEORDER_GENERIC_H

/*
 * linux/byteorder_generic.h
 * Generic Byte-reordering support
 *
 * Francois-Rene Rideau <fare@tunes.org> 19970707
 *    gathered all the good ideas from all asm-foo/byteorder.h into one file,
 *    cleaned them up.
 *    I hope it is compliant with non-GCC compilers.
 *    I decided to put __BYTEORDER_HAS_U64__ in byteorder.h,
 *    because I wasn't sure it would be ok to put it in types.h
 *    Upgraded it to 2.1.43
 * Francois-Rene Rideau <fare@tunes.org> 19971012
 *    Upgraded it to 2.1.57
 *    to please Linus T., replaced huge #ifdef's between little/big endian
 *    by nestedly #include'd files.
 * Francois-Rene Rideau <fare@tunes.org> 19971205
 *    Made it to 2.1.71; now a facelift:
 *    Put files under include/linux/byteorder/
 *    Split swab from generic support.
 *
 * TODO:
 *   = Regular kernel maintainers could also replace all these manual
 *    byteswap macros that remain, disseminated among drivers,
 *    after some grep or the sources...
 *   = Linus might want to rename all these macros and files to fit his taste,
 *    to fit his personal naming scheme.
 *   = it seems that a few drivers would also appreciate
 *    nybble swapping support...
 *   = every architecture could add their byteswap macro in asm/byteorder.h
 *    see how some architectures already do (i386, alpha, ppc, etc)
 *   = cpu_to_beXX and beXX_to_cpu might some day need to be well
 *    distinguished throughout the kernel. This is not the case currently,
 *    since little endian, big endian, and pdp endian machines needn't it.
 *    But this might be the case for, say, a port of Linux to 20/21 bit
 *    architectures (and F21 Linux addict around?).
 */

/*
 * The following macros are to be defined by <asm/byteorder.h>:
 *
 * Conversion of long and short int between network and host format
 *	ntohl(__u32 x)
 *	ntohs(__u16 x)
 *	htonl(__u32 x)
 *	htons(__u16 x)
 * It seems that some programs (which? where? or perhaps a standard? POSIX?)
 * might like the above to be functions, not macros (why?).
 * if that's true, then detect them, and take measures.
 * Anyway, the measure is: define only ___ntohl as a macro instead,
 * and in a separate file, have
 * unsigned long inline ntohl(x){return ___ntohl(x);}
 *
 * The same for constant arguments
 *	__constant_ntohl(__u32 x)
 *	__constant_ntohs(__u16 x)
 *	__constant_htonl(__u32 x)
 *	__constant_htons(__u16 x)
 *
 * Conversion of XX-bit integers (16- 32- or 64-)
 * between native CPU format and little/big endian format
 * 64-bit stuff only defined for proper architectures
 *	cpu_to_[bl]eXX(__uXX x)
 *	[bl]eXX_to_cpu(__uXX x)
 *
 * The same, but takes a pointer to the value to convert
 *	cpu_to_[bl]eXXp(__uXX x)
 *	[bl]eXX_to_cpup(__uXX x)
 *
 * The same, but change in situ
 *	cpu_to_[bl]eXXs(__uXX x)
 *	[bl]eXX_to_cpus(__uXX x)
 *
 * See asm-foo/byteorder.h for examples of how to provide
 * architecture-optimized versions
 *
 */


#if defined(PLATFORM_LINUX) || defined(PLATFORM_WINDOWS) || defined(PLATFORM_MPIXEL) || defined(PLATFORM_FREEBSD)
	/*
	* inside the kernel, we can use nicknames;
	* outside of it, we must avoid POSIX namespace pollution...
	*/
	#define cpu_to_le64 __cpu_to_le64
	#define le64_to_cpu __le64_to_cpu
	#define cpu_to_le32 __cpu_to_le32
	#define le32_to_cpu __le32_to_cpu
	#define cpu_to_le16 __cpu_to_le16
	#define le16_to_cpu __le16_to_cpu
	#define cpu_to_be64 __cpu_to_be64
	#define be64_to_cpu __be64_to_cpu
	#define cpu_to_be32 __cpu_to_be32
	#define be32_to_cpu __be32_to_cpu
	#define cpu_to_be16 __cpu_to_be16
	#define be16_to_cpu __be16_to_cpu
	#define cpu_to_le64p __cpu_to_le64p
	#define le64_to_cpup __le64_to_cpup
	#define cpu_to_le32p __cpu_to_le32p
	#define le32_to_cpup __le32_to_cpup
	#define cpu_to_le16p __cpu_to_le16p
	#define le16_to_cpup __le16_to_cpup
	#define cpu_to_be64p __cpu_to_be64p
	#define be64_to_cpup __be64_to_cpup
	#define cpu_to_be32p __cpu_to_be32p
	#define be32_to_cpup __be32_to_cpup
	#define cpu_to_be16p __cpu_to_be16p
	#define be16_to_cpup __be16_to_cpup
	#define cpu_to_le64s __cpu_to_le64s
	#define le64_to_cpus __le64_to_cpus
	#define cpu_to_le32s __cpu_to_le32s
	#define le32_to_cpus __le32_to_cpus
	#define cpu_to_le16s __cpu_to_le16s
	#define le16_to_cpus __le16_to_cpus
	#define cpu_to_be64s __cpu_to_be64s
	#define be64_to_cpus __be64_to_cpus
	#define cpu_to_be32s __cpu_to_be32s
	#define be32_to_cpus __be32_to_cpus
	#define cpu_to_be16s __cpu_to_be16s
	#define be16_to_cpus __be16_to_cpus
#endif


/*
 * Handle ntohl and suches. These have various compatibility
 * issues - like we want to give the prototype even though we
 * also have a macro for them in case some strange program
 * wants to take the address of the thing or something..
 *
 * Note that these used to return a "long" in libc5, even though
 * long is often 64-bit these days.. Thus the casts.
 *
 * They have to be macros in order to do the constant folding
 * correctly - if the argument passed into a inline function
 * it is no longer constant according to gcc..
 */

#undef ntohl
#undef ntohs
#undef htonl
#undef htons

/*
 * Do the prototypes. Somebody might want to take the
 * address or some such sick thing..
 */
#if defined(PLATFORM_LINUX) || (defined(__GLIBC__) && __GLIBC__ >= 2)
	extern __u32			ntohl(__u32);
	extern __u32			htonl(__u32);
#else /* defined(PLATFORM_LINUX) || (defined (__GLIBC__) && __GLIBC__ >= 2) */
	#ifndef PLATFORM_FREEBSD
		extern unsigned long int	ntohl(unsigned long int);
		extern unsigned long int	htonl(unsigned long int);
	#endif
#endif
#ifndef PLATFORM_FREEBSD
	extern unsigned short int	ntohs(unsigned short int);
	extern unsigned short int	htons(unsigned short int);
#endif

#if defined(__GNUC__) && (__GNUC__ >= 2) && defined(__OPTIMIZE__) || defined(PLATFORM_MPIXEL)

	#define ___htonl(x) __cpu_to_be32(x)
	#define ___htons(x) __cpu_to_be16(x)
	#define ___ntohl(x) __be32_to_cpu(x)
	#define ___ntohs(x) __be16_to_cpu(x)

	#if defined(PLATFORM_LINUX) || (defined(__GLIBC__) && __GLIBC__ >= 2)
		#define htonl(x) ___htonl(x)
		#define ntohl(x) ___ntohl(x)
	#else
		#define htonl(x) ((unsigned long)___htonl(x))
		#define ntohl(x) ((unsigned long)___ntohl(x))
	#endif
	#define htons(x) ___htons(x)
	#define ntohs(x) ___ntohs(x)

#endif /* OPTIMIZE */


#if defined(PLATFORM_WINDOWS)

	#define htonl(x) __cpu_to_be32(x)
	#define ntohl(x) __be32_to_cpu(x)
	#define htons(x) __cpu_to_be16(x)
	#define ntohs(x) __be16_to_cpu(x)


#endif

#endif /* _LINUX_BYTEORDER_GENERIC_H */
