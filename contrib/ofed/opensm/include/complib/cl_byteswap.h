/*
 * Copyright (c) 2004-2006 Voltaire, Inc. All rights reserved.
 * Copyright (c) 2002-2005 Mellanox Technologies LTD. All rights reserved.
 * Copyright (c) 1996-2003 Intel Corporation. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

/*
 * Abstract:
 *	provides byteswapping utilities. Basic functions are obtained from
 *  platform specific implementations from byteswap_osd.h.
 */

#ifndef _CL_BYTESWAP_H_
#define _CL_BYTESWAP_H_

#include <string.h>
#include <complib/cl_byteswap_osd.h>

#ifdef __cplusplus
#  define BEGIN_C_DECLS extern "C" {
#  define END_C_DECLS   }
#else				/* !__cplusplus */
#  define BEGIN_C_DECLS
#  define END_C_DECLS
#endif				/* __cplusplus */

BEGIN_C_DECLS
/****h* Component Library/Byte Swapping
* NAME
*	Byte Swapping
*
* DESCRIPTION
*	The byte swapping functions and macros allow swapping bytes from network
*	byte order to host byte order.
*
*	All data transmitted between systems should be in network byte order.
*	In order to utilize such data, it must be converted to host byte order
*	before use.
*
* SEE ALSO
*	Functions:
*		cl_ntoh16, cl_hton16, cl_ntoh32, cl_hton32, cl_ntoh64, cl_hton64,
*		cl_ntoh
*
*	Macros:
*		CL_NTOH16, CL_HTON16, CL_NTOH32, CL_HTON32, CL_NTOH64, CL_HTON64
*********/
/*
 * The byteswap_osd.h provides the following macros.
 *		__LITTLE_ENDIAN
 *		__BIG_ENDIAN
 *		__BYTE_ORDER
 *
 * If the platform provides byte swapping functions, byteswap_osd.h also
 * provides the following macros.
 *		ntoh16, hton16
 *		ntoh32, hton32
 *		ntoh64, hton64
 */
#ifndef __BYTE_ORDER
#error "__BYTE_ORDER macro undefined. Missing in endian.h?"
#endif
#if __BYTE_ORDER == __LITTLE_ENDIAN
#define CPU_LE		1
#define CPU_BE		0
#else
#define CPU_LE		0
#define CPU_BE		1
#endif
/****d* Component Library: Byte Swapping/CL_NTOH16
* NAME
*	CL_NTOH16
*
* DESCRIPTION
*	The CL_NTOH16 macro converts a 16-bit value from network byte order to
*	host byte order.  The CL_NTOH16 macro will cause constant values to be
*	swapped by the pre-processor.  For variables, CL_NTOH16 is less efficient
*	than the cl_ntoh16 function.
*
* SYNOPSIS
*	CL_NTOH16( val );
*
* PARAMETERS
*	val
*		[in] 16-bit value to swap from network byte order to host byte order.
*
* RESULT
*	Value of val converted to host byte order.
*
* NOTES
*	This macro is analogous to CL_HTON16.
*
* SEE ALSO
*	Byte Swapping, CL_HTON16, CL_NTOH32, CL_NTOH64,
*	cl_ntoh16, cl_ntoh32, cl_ntoh64, cl_ntoh
*********/
/****d* Component Library: Byte Swapping/CL_HTON16
* NAME
*	CL_HTON16
*
* DESCRIPTION
*	The CL_HTON16 macro converts a 16-bit value from host byte order to
*	network byte order.  The CL_HTON16 macro will cause constant values to be
*	swapped by the pre-processor.  For variables, CL_HTON16 is less efficient
*	than the cl_hton16 function.
*
* SYNOPSIS
*	CL_HTON16( val );
*
* PARAMETERS
*	val
*		[in] 16-bit value to swap from host byte order to network byte order.
*
* RESULT
*	Value of val converted to network byte order.
*
* NOTES
*	This macro is analogous to CL_NTOH16.
*
* SEE ALSO
*	Byte Swapping, CL_NTOH16, CL_HTON32, CL_HTON64,
*	cl_hton16, cl_hton32, cl_hton64, cl_ntoh
*********/
#if CPU_LE
#define CL_NTOH16( x )		(uint16_t)(		\
			(((uint16_t)(x) & 0x00FF) << 8) |		\
			(((uint16_t)(x) & 0xFF00) >> 8) )
#else
#define CL_NTOH16( x )	(x)
#endif
#define CL_HTON16				CL_NTOH16
/****f* Component Library: Byte Swapping/cl_ntoh16
* NAME
*	cl_ntoh16
*
* DESCRIPTION
*	The cl_ntoh16 function converts a 16-bit value from network byte order to
*	host byte order.
*
* SYNOPSIS
*	uint16_t
*	cl_ntoh16(
*		IN	const uint16_t	val );
*
* PARAMETERS
*	val
*		[in] Value to swap from network byte order to host byte order.
*
* RETURN VALUE
*	Value of val converted to host byte order.
*
* NOTES
*	This function is analogous to cl_hton16.
*
* SEE ALSO
*	Byte Swapping, cl_hton16, cl_ntoh32, cl_ntoh64, cl_ntoh
*********/
/****f* Component Library: Byte Swapping/cl_hton16
* NAME
*	cl_hton16
*
* DESCRIPTION
*	The cl_hton16 function converts a 16-bit value from host byte order to
*	network byte order.
*
* SYNOPSIS
*	uint16_t
*	cl_hton16(
*		IN	const uint16_t	val );
*
* PARAMETERS
*	val
*		[in] Value to swap from host byte order to network byte order .
*
* RETURN VALUE
*	Value of val converted to network byte order.
*
* NOTES
*	This function is analogous to cl_ntoh16.
*
* SEE ALSO
*	Byte Swapping, cl_ntoh16, cl_hton32, cl_hton64, cl_ntoh
*********/
#ifndef cl_ntoh16
#define cl_ntoh16	CL_NTOH16
#define cl_hton16	CL_HTON16
#endif
/****d* Component Library: Byte Swapping/CL_NTOH32
* NAME
*	CL_NTOH32
*
* DESCRIPTION
*	The CL_NTOH32 macro converts a 32-bit value from network byte order to
*	host byte order.  The CL_NTOH32 macro will cause constant values to be
*	swapped by the pre-processor.  For variables, CL_NTOH32 is less efficient
*	than the cl_ntoh32 function.
*
* SYNOPSIS
*	CL_NTOH32( val );
*
* PARAMETERS
*	val
*		[in] 32-bit value to swap from network byte order to host byte order.
*
* RESULT
*	Value of val converted to host byte order.
*
* NOTES
*	This macro is analogous to CL_HTON32.
*
* SEE ALSO
*	Byte Swapping, CL_HTON32, CL_NTOH16, CL_NTOH64,
*	cl_ntoh16, cl_ntoh32, cl_ntoh64, cl_ntoh
*********/
/****d* Component Library: Byte Swapping/CL_HTON32
* NAME
*	CL_HTON32
*
* DESCRIPTION
*	The CL_HTON32 macro converts a 32-bit value from host byte order to
*	network byte order.  The CL_HTON32 macro will cause constant values to be
*	swapped by the pre-processor.  For variables, CL_HTON32 is less efficient
*	than the cl_hton32 function.
*
* SYNOPSIS
*	CL_HTON32( val );
*
* PARAMETERS
*	val
*		[in] 32-bit value to swap from host byte order to network byte order.
*
* RESULT
*	Value of val converted to network byte order.
*
* NOTES
*	This macro is analogous to CL_NTOH32.
*
* SEE ALSO
*	Byte Swapping, CL_NTOH32, CL_HTON16, CL_HTON64,
*	cl_hton16, cl_hton32, cl_hton64, cl_ntoh
*********/
#if CPU_LE
#define CL_NTOH32( x )		(uint32_t)(			\
			(((uint32_t)(x) & 0x000000FF) << 24) |	\
			(((uint32_t)(x) & 0x0000FF00) << 8) |	\
			(((uint32_t)(x) & 0x00FF0000) >> 8) |	\
			(((uint32_t)(x) & 0xFF000000) >> 24) )
#else
#define CL_NTOH32( x )		(x)
#endif
#define CL_HTON32	CL_NTOH32
/****f* Component Library: Byte Swapping/cl_ntoh32
* NAME
*	cl_ntoh32
*
* DESCRIPTION
*	The cl_ntoh32 function converts a 32-bit value from network byte order to
*	host byte order.
*
* SYNOPSIS
*	uint32_t
*	cl_ntoh32(
*		IN	const uint32_t	val );
*
* PARAMETERS
*	val
*		[in] Value to swap from network byte order to host byte order.
*
* RETURN VALUE
*	Value of val converted in host byte order.
*
* NOTES
*	This function is analogous to cl_hton32.
*
* SEE ALSO
*	Byte Swapping, cl_hton32, cl_ntoh16, cl_ntoh64, cl_ntoh
*********/
/****f* Component Library: Byte Swapping/cl_hton32
* NAME
*	cl_hton32
*
* DESCRIPTION
*	The cl_hton32 function converts a 32-bit value from host byte order to
*	network byte order.
*
* SYNOPSIS
*	uint32_t
*	cl_hton32(
*		IN	const uint32_t	val );
*
* PARAMETERS
*	val
*		[in] Value to swap from host byte order to network byte order .
*
* RETURN VALUE
*	Value of val converted to network byte order.
*
* NOTES
*	This function is analogous to cl_ntoh32.
*
* SEE ALSO
*	Byte Swapping, cl_ntoh32, cl_hton16, cl_hton64, cl_ntoh
*********/
#ifndef cl_ntoh32
#define cl_ntoh32	CL_NTOH32
#define cl_hton32	CL_HTON32
#endif
/****d* Component Library: Byte Swapping/CL_NTOH64
* NAME
*	CL_NTOH64
*
* DESCRIPTION
*	The CL_NTOH64 macro converts a 64-bit value from network byte order to
*	host byte order.  The CL_NTOH64 macro will cause constant values to be
*	swapped by the pre-processor.  For variables, CL_NTOH64 is less efficient
*	than the cl_ntoh64 function.
*
* SYNOPSIS
*	CL_NTOH64( val );
*
* PARAMETERS
*	val
*		[in] 64-bit value to swap from network byte order to host byte order.
*
* RESULT
*	Value of val converted to host byte order.
*
* NOTES
*	This macro is analogous to CL_HTON64.
*
* SEE ALSO
*	Byte Swapping, CL_HTON64, CL_NTOH16, CL_NTOH32,
*	cl_ntoh16, cl_ntoh32, cl_ntoh64, cl_ntoh
*********/
/****d* Component Library: Byte Swapping/CL_HTON64
* NAME
*	CL_HTON64
*
* DESCRIPTION
*	The CL_HTON64 macro converts a 64-bit value from host byte order to
*	network byte order.  The CL_HTON64 macro will cause constant values to be
*	swapped by the pre-processor.  For variables, CL_HTON64 is less efficient
*	than the cl_hton64 function.
*
* SYNOPSIS
*	CL_HTON64( val );
*
* PARAMETERS
*	val
*		[in] 64-bit value to swap from host byte order to network byte order.
*
* RESULT
*	Value of val converted to network byte order.
*
* NOTES
*	This macro is analogous to CL_NTOH64.
*
* SEE ALSO
*	Byte Swapping, CL_NTOH64, CL_HTON16, CL_HTON32,
*	cl_hton16, cl_hton32, cl_hton64, cl_ntoh
*********/
#if CPU_LE
#define CL_NTOH64( x )		(uint64_t)(					\
			(((uint64_t)(x) & 0x00000000000000FFULL) << 56) |	\
			(((uint64_t)(x) & 0x000000000000FF00ULL) << 40) |	\
			(((uint64_t)(x) & 0x0000000000FF0000ULL) << 24) |	\
			(((uint64_t)(x) & 0x00000000FF000000ULL) << 8 ) |	\
			(((uint64_t)(x) & 0x000000FF00000000ULL) >> 8 ) |	\
			(((uint64_t)(x) & 0x0000FF0000000000ULL) >> 24) |	\
			(((uint64_t)(x) & 0x00FF000000000000ULL) >> 40) |	\
			(((uint64_t)(x) & 0xFF00000000000000ULL) >> 56) )
#else
#define CL_NTOH64( x )		(x)
#endif
#define CL_HTON64				CL_NTOH64
/****f* Component Library: Byte Swapping/cl_ntoh64
* NAME
*	cl_ntoh64
*
* DESCRIPTION
*	The cl_ntoh64 function converts a 64-bit value from network byte order to
*	host byte order.
*
* SYNOPSIS
*	uint64_t
*	cl_ntoh64(
*		IN	const uint64_t	val );
*
* PARAMETERS
*	val
*		[in] Value to swap from network byte order to host byte order.
*
* RETURN VALUE
*	Value of val converted in host byte order.
*
* NOTES
*	This function is analogous to cl_hton64.
*
* SEE ALSO
*	Byte Swapping, cl_hton64, cl_ntoh16, cl_ntoh32, cl_ntoh
*********/
/****f* Component Library: Byte Swapping/cl_hton64
* NAME
*	cl_hton64
*
* DESCRIPTION
*	The cl_hton64 function converts a 64-bit value from host byte order to
*	network byte order.
*
* SYNOPSIS
*	uint64_t
*	cl_hton64(
*		IN	const uint64_t	val );
*
* PARAMETERS
*	val
*		[in] Value to swap from host byte order to network byte order .
*
* RETURN VALUE
*	Value of val converted to network byte order.
*
* NOTES
*	This function is analogous to cl_ntoh64.
*
* SEE ALSO
*	Byte Swapping, cl_ntoh64, cl_hton16, cl_hton32, cl_ntoh
*********/
#ifndef cl_ntoh64
#define cl_ntoh64	CL_NTOH64
#define cl_hton64	CL_HTON64
#endif
/****f* Component Library: Byte Swapping/cl_ntoh
* NAME
*	cl_ntoh
*
* DESCRIPTION
*	The cl_ntoh function converts a value from network byte order to
*	host byte order.
*
* SYNOPSIS
*/
static inline void
cl_ntoh(OUT char *const p_dest,
	IN const char *const p_src, IN const uint8_t size)
{
#if CPU_LE
	uint8_t i;
	char temp;

	if (p_src == p_dest) {
		/* Swap in place if source and destination are the same. */
		for (i = 0; i < size / 2; i++) {
			temp = p_dest[i];
			p_dest[i] = p_src[size - 1 - i];
			p_dest[size - 1 - i] = temp;
		}
	} else {
		for (i = 0; i < size; i++)
			p_dest[i] = p_src[size - 1 - i];
	}
#else
	/*
	 * If the source and destination are not the same, copy the source to
	 * the destination.
	 */
	if (p_src != p_dest)
		memcpy(p_dest, p_src, size);
#endif
}

/*
* PARAMETERS
*	p_dest
*		[in] Pointer to a byte array to contain the converted value of p_src.
*
*	p_src
*		[in] Pointer to a byte array to be converted from network byte
*		ordering.
*
*	size
*		[in] Number of bytes to swap.p_dest
*
* RETURN VALUE
*	This function does not return a value.
*
* NOTES
*	cl_ntoh can perform in place swapping if both p_src and p_dest point to
*	the same buffer.
*
* SEE ALSO
*	Byte Swapping, cl_ntoh16, cl_ntoh32, cl_ntoh64
*********/

END_C_DECLS
#endif				/* _CL_BYTESWAP_H_ */
