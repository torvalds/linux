/*************************************************************************/ /*!
@File
@Title          RGX Common Types and Defines Header
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Common types and definitions for RGX software
@License        Dual MIT/GPLv2

The contents of this file are subject to the MIT license as set out below.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

Alternatively, the contents of this file may be used under the terms of
the GNU General Public License Version 2 ("GPL") in which case the provisions
of GPL are applicable instead of those above.

If you wish to allow use of your version of this file only under the terms of
GPL, and not to allow others to use your version of this file under the terms
of the MIT license, indicate your decision by deleting the provisions above
and replace them with the notice and other provisions required by GPL as set
out in the file called "GPL-COPYING" included in this distribution. If you do
not delete the provisions above, a recipient may use your version of this file
under the terms of either the MIT license or GPL.

This License is also included in this distribution in the file called
"MIT-COPYING".

EXCEPT AS OTHERWISE STATED IN A NEGOTIATED AGREEMENT: (A) THE SOFTWARE IS
PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
PURPOSE AND NONINFRINGEMENT; AND (B) IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/ /**************************************************************************/
#ifndef RGX_COMMON_ASSERTS_H
#define RGX_COMMON_ASSERTS_H

#if defined(__cplusplus)
extern "C" {
#endif

/*! This macro represents a mask of LSBs that must be zero on data structure
 * sizes and offsets to ensure they are 8-byte granular on types shared between
 * the FW and host driver */
#define RGX_FW_ALIGNMENT_LSB (7U)

/*! Macro to test structure size alignment */
#define RGX_FW_STRUCT_SIZE_ASSERT(_a)	\
	static_assert((sizeof(_a) & RGX_FW_ALIGNMENT_LSB) == 0U,	\
				  "Size of " #_a " is not properly aligned")

/*! Macro to test structure member alignment */
#define RGX_FW_STRUCT_OFFSET_ASSERT(_a, _b)	\
	static_assert((offsetof(_a, _b) & RGX_FW_ALIGNMENT_LSB) == 0U,	\
				  "Offset of " #_a "." #_b " is not properly aligned")

#if defined(__cplusplus)
}
#endif

#endif /* RGX_COMMON_ASSERTS_H */

/******************************************************************************
 End of file
******************************************************************************/
