/*************************************************************************/ /*!
@File
@Title          RGX KM build options
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
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

/* Each build option listed here is packed into a dword which
 * provides up to log2(RGX_BUILD_OPTIONS_MASK_KM + 1) flags.
 * The corresponding bit is set if the build option 
 * was enabled at compile time.
 *
 * In order to extract the enabled build flags the INTERNAL_TEST
 * switch should be enabled in a client program which includes this
 * header. Then the client can test specific build flags by reading
 * the bit value at ##OPTIONNAME##_SET_OFFSET in RGX_BUILD_OPTIONS_KM.
 *
 * IMPORTANT: add new options to unused bits or define a new dword
 * (e.g. RGX_BUILD_OPTIONS_KM2) so that the bitfield remains backwards
 * compatible.
 */

#define RGX_BUILD_OPTIONS_MASK_KM 0x0000FFFFUL

#if defined(NO_HARDWARE) || defined (INTERNAL_TEST)
	#define NO_HARDWARE_SET_OFFSET	OPTIONS_BIT0
	#define OPTIONS_BIT0		(0x1ul << 0)
	#if OPTIONS_BIT0 > RGX_BUILD_OPTIONS_MASK_KM
	#error "Bit exceeds reserved range"
	#endif
#else
	#define OPTIONS_BIT0		0x0
#endif /* NO_HARDWARE */


#if defined(PDUMP) || defined (INTERNAL_TEST)
	#define PDUMP_SET_OFFSET	OPTIONS_BIT1
	#define OPTIONS_BIT1		(0x1ul << 1)
	#if OPTIONS_BIT1 > RGX_BUILD_OPTIONS_MASK_KM
	#error "Bit exceeds reserved range"
	#endif
#else
	#define OPTIONS_BIT1		0x0
#endif /* PDUMP */


#if defined(SUPPORT_META_SLAVE_BOOT) || defined (INTERNAL_TEST)
	#define SUPPORT_META_SLAVE_BOOT_SET_OFFSET	OPTIONS_BIT2
	#define OPTIONS_BIT2		(0x1ul << 2)
	#if OPTIONS_BIT2 > RGX_BUILD_OPTIONS_MASK_KM
	#error "Bit exceeds reserved range"
	#endif
#else
	#define OPTIONS_BIT2		0x0
#endif /* SUPPORT_META_SLAVE_BOOT */


#if defined(SUPPORT_MMU_FREELIST) || defined (INTERNAL_TEST)
	#define SUPPORT_MMU_FREELIST_SET_OFFSET	OPTIONS_BIT3
	#define OPTIONS_BIT3		(0x1ul << 3)
	#if OPTIONS_BIT3 > RGX_BUILD_OPTIONS_MASK_KM
	#error "Bit exceeds reserved range"
	#endif
#else
	#define OPTIONS_BIT3		0x0
#endif /* SUPPORT_MMU_FREELIST */


#if defined(SUPPORT_PMMIF) || defined (INTERNAL_TEST)
	#define SUPPORT_PMMIF_SET_OFFSET	OPTIONS_BIT4
	#define OPTIONS_BIT4		(0x1ul << 4)
	#if OPTIONS_BIT4 > RGX_BUILD_OPTIONS_MASK_KM
	#error "Bit exceeds reserved range"
	#endif
#else
	#define OPTIONS_BIT4		0x0
#endif /* SUPPORT_PMMIF */


#if defined(SUPPORT_RGX) || defined (INTERNAL_TEST)
	#define SUPPORT_RGX_SET_OFFSET	OPTIONS_BIT5
	#define OPTIONS_BIT5		(0x1ul << 5)
	#if OPTIONS_BIT5 > RGX_BUILD_OPTIONS_MASK_KM
	#error "Bit exceeds reserved range"
	#endif
#else
	#define OPTIONS_BIT5		0x0
#endif /* SUPPORT_RGX */


#if defined(SUPPORT_SECURE_EXPORT) || defined (INTERNAL_TEST)
	#define SUPPORT_SECURE_EXPORT_SET_OFFSET	OPTIONS_BIT6
	#define OPTIONS_BIT6		(0x1ul << 6)
	#if OPTIONS_BIT6 > RGX_BUILD_OPTIONS_MASK_KM
	#error "Bit exceeds reserved range"
	#endif
#else
	#define OPTIONS_BIT6		0x0
#endif /* SUPPORT_SECURE_EXPORT */


#if defined(SUPPORT_INSECURE_EXPORT) || defined (INTERNAL_TEST)
	#define SUPPORT_INSECURE_EXPORT_SET_OFFSET	OPTIONS_BIT7
	#define OPTIONS_BIT7		(0x1ul << 7)
	#if OPTIONS_BIT7 > RGX_BUILD_OPTIONS_MASK_KM
	#error "Bit exceeds reserved range"
	#endif
#else
	#define OPTIONS_BIT7		0x0
#endif /* SUPPORT_INSECURE_EXPORT */


#if defined(SUPPORT_VFP) || defined (INTERNAL_TEST)
	#define SUPPORT_VFP_SET_OFFSET	OPTIONS_BIT8
	#define OPTIONS_BIT8		(0x1ul << 8)
	#if OPTIONS_BIT8 > RGX_BUILD_OPTIONS_MASK_KM
	#error "Bit exceeds reserved range"
	#endif
#else
	#define OPTIONS_BIT8		0x0
#endif /* SUPPORT_VFP */


#if defined(SUPPORT_DRM) || defined (INTERNAL_TEST)
	#define SUPPORT_DRM_SET_OFFSET	OPTIONS_BIT9
	#define OPTIONS_BIT9		(0x1ul << 9)
	#if OPTIONS_BIT9 > RGX_BUILD_OPTIONS_MASK_KM
	#error "Bit exceeds reserved range"
	#endif
#else
	#define OPTIONS_BIT9		0x0
#endif /* SUPPORT_DRM */


#if defined(SUPPORT_ION) || defined (INTERNAL_TEST)
	#define SUPPORT_ION_SET_OFFSET	OPTIONS_BIT10
	#define OPTIONS_BIT10		(0x1ul << 10)
	#if OPTIONS_BIT10 > RGX_BUILD_OPTIONS_MASK_KM
	#error "Bit exceeds reserved range"
	#endif
#else
	#define OPTIONS_BIT10		0x0
#endif /* SUPPORT_ION */

#if defined(DEBUG) || defined (INTERNAL_TEST)
	#define DEBUG_SET_OFFSET	OPTIONS_BIT11
	#define OPTIONS_BIT11		(0x1ul << 11)
	#if OPTIONS_BIT11 > RGX_BUILD_OPTIONS_MASK_KM
	#error "Bit exceeds reserved range"
	#endif
#else
	#define OPTIONS_BIT11		0x0
#endif /* DEBUG */


#define RGX_BUILD_OPTIONS_KM	\
	OPTIONS_BIT0  |\
	OPTIONS_BIT1  |\
	OPTIONS_BIT2  |\
	OPTIONS_BIT3  |\
	OPTIONS_BIT4  |\
	OPTIONS_BIT6  |\
	OPTIONS_BIT7  |\
	OPTIONS_BIT8  |\
	OPTIONS_BIT9  |\
	OPTIONS_BIT10 |\
	OPTIONS_BIT11


