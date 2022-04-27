/*************************************************************************/ /*!
@File
@Title          RGX build options
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

/* Each build option listed here is packed into a dword which provides up to
 *  log2(RGX_BUILD_OPTIONS_MASK_KM + 1) flags for KM and
 *  (32 - log2(RGX_BUILD_OPTIONS_MASK_KM + 1)) flags for UM.
 * The corresponding bit is set if the build option was enabled at compile
 * time.
 *
 * In order to extract the enabled build flags the INTERNAL_TEST switch should
 * be enabled in a client program which includes this header. Then the client
 * can test specific build flags by reading the bit value at
 *  ##OPTIONNAME##_SET_OFFSET
 * in RGX_BUILD_OPTIONS_KM or RGX_BUILD_OPTIONS.
 *
 * IMPORTANT: add new options to unused bits or define a new dword
 * (e.g. RGX_BUILD_OPTIONS_KM2 or RGX_BUILD_OPTIONS2) so that the bitfield
 * remains backwards compatible.
 */

#ifndef RGX_OPTIONS_H
#define RGX_OPTIONS_H

#define RGX_BUILD_OPTIONS_MASK_KM 0x0000FFFFUL

#define NO_HARDWARE_OPTION	"NO_HARDWARE  "
#if defined(NO_HARDWARE) || defined(INTERNAL_TEST)
	#define NO_HARDWARE_SET_OFFSET	OPTIONS_BIT0
	#define OPTIONS_BIT0		(0x1UL << 0)
	#if OPTIONS_BIT0 > RGX_BUILD_OPTIONS_MASK_KM
	#error "Bit exceeds reserved range"
	#endif
#else
	#define OPTIONS_BIT0		0x0UL
#endif /* NO_HARDWARE */

#define PDUMP_OPTION	"PDUMP  "
#if defined(PDUMP) || defined(INTERNAL_TEST)
	#define PDUMP_SET_OFFSET	OPTIONS_BIT1
	#define OPTIONS_BIT1		(0x1UL << 1)
	#if OPTIONS_BIT1 > RGX_BUILD_OPTIONS_MASK_KM
	#error "Bit exceeds reserved range"
	#endif
#else
	#define OPTIONS_BIT1		0x0UL
#endif /* PDUMP */

/* No longer used */
#define INTERNAL_TEST_OPTION	"INTERNAL_TEST  "
#if defined(INTERNAL_TEST)
	#define UNUSED_SET_OFFSET	OPTIONS_BIT2
	#define OPTIONS_BIT2		(0x1UL << 2)
	#if OPTIONS_BIT2 > RGX_BUILD_OPTIONS_MASK_KM
	#error "Bit exceeds reserved range"
	#endif
#else
	#define OPTIONS_BIT2		0x0UL
#endif

/* No longer used */
#define UNUSED_OPTION	" "
#if defined(INTERNAL_TEST)
	#define OPTIONS_BIT3		(0x1UL << 3)
	#define INTERNAL_TEST_OPTION	"INTERNAL_TEST  "
	#if OPTIONS_BIT3 > RGX_BUILD_OPTIONS_MASK_KM
	#error "Bit exceeds reserved range"
	#endif
#else
	#define OPTIONS_BIT3		0x0UL
#endif

#define SUPPORT_RGX_OPTION	" "
#if defined(SUPPORT_RGX) || defined(INTERNAL_TEST)
	#define SUPPORT_RGX_SET_OFFSET	OPTIONS_BIT4
	#define OPTIONS_BIT4		(0x1UL << 4)
	#if OPTIONS_BIT4 > RGX_BUILD_OPTIONS_MASK_KM
	#error "Bit exceeds reserved range"
	#endif
#else
	#define OPTIONS_BIT4		0x0UL
#endif /* SUPPORT_RGX */

#define SUPPORT_SECURE_EXPORT_OPTION	"SECURE_EXPORTS  "
#if defined(SUPPORT_SECURE_EXPORT) || defined(INTERNAL_TEST)
	#define SUPPORT_SECURE_EXPORT_SET_OFFSET	OPTIONS_BIT5
	#define OPTIONS_BIT5		(0x1UL << 5)
	#if OPTIONS_BIT5 > RGX_BUILD_OPTIONS_MASK_KM
	#error "Bit exceeds reserved range"
	#endif
#else
	#define OPTIONS_BIT5		0x0UL
#endif /* SUPPORT_SECURE_EXPORT */

#define SUPPORT_INSECURE_EXPORT_OPTION	"INSECURE_EXPORTS  "
#if defined(SUPPORT_INSECURE_EXPORT) || defined(INTERNAL_TEST)
	#define SUPPORT_INSECURE_EXPORT_SET_OFFSET	OPTIONS_BIT6
	#define OPTIONS_BIT6		(0x1UL << 6)
	#if OPTIONS_BIT6 > RGX_BUILD_OPTIONS_MASK_KM
	#error "Bit exceeds reserved range"
	#endif
#else
	#define OPTIONS_BIT6		0x0UL
#endif /* SUPPORT_INSECURE_EXPORT */

#define SUPPORT_VFP_OPTION	"VFP  "
#if defined(SUPPORT_VFP) || defined(INTERNAL_TEST)
	#define SUPPORT_VFP_SET_OFFSET	OPTIONS_BIT7
	#define OPTIONS_BIT7		(0x1UL << 7)
	#if OPTIONS_BIT7 > RGX_BUILD_OPTIONS_MASK_KM
	#error "Bit exceeds reserved range"
	#endif
#else
	#define OPTIONS_BIT7		0x0UL
#endif /* SUPPORT_VFP */

#define SUPPORT_WORKLOAD_ESTIMATION_OPTION	"WORKLOAD_ESTIMATION  "
#if defined(SUPPORT_WORKLOAD_ESTIMATION) || defined(INTERNAL_TEST)
	#define SUPPORT_WORKLOAD_ESTIMATION_OFFSET	OPTIONS_BIT8
	#define OPTIONS_BIT8		(0x1UL << 8)
	#if OPTIONS_BIT8 > RGX_BUILD_OPTIONS_MASK_KM
	#error "Bit exceeds reserved range"
	#endif
#else
	#define OPTIONS_BIT8		0x0UL
#endif /* SUPPORT_WORKLOAD_ESTIMATION */
#define OPTIONS_WORKLOAD_ESTIMATION_MASK	(0x1UL << 8)

#define SUPPORT_PDVFS_OPTION	"PDVFS  "
#if defined(SUPPORT_PDVFS) || defined(INTERNAL_TEST)
	#define SUPPORT_PDVFS_OFFSET	OPTIONS_BIT9
	#define OPTIONS_BIT9		(0x1UL << 9)
	#if OPTIONS_BIT9 > RGX_BUILD_OPTIONS_MASK_KM
	#error "Bit exceeds reserved range"
	#endif
#else
	#define OPTIONS_BIT9		0x0UL
#endif /* SUPPORT_PDVFS */
#define OPTIONS_PDVFS_MASK	(0x1UL << 9)

#define DEBUG_OPTION	"DEBUG  "
#if defined(DEBUG) || defined(INTERNAL_TEST)
	#define DEBUG_SET_OFFSET	OPTIONS_BIT10
	#define OPTIONS_BIT10		(0x1UL << 10)
	#if OPTIONS_BIT10 > RGX_BUILD_OPTIONS_MASK_KM
	#error "Bit exceeds reserved range"
	#endif
#else
	#define OPTIONS_BIT10		0x0UL
#endif /* DEBUG */
/* The bit position of this should be the same as DEBUG_SET_OFFSET option
 * when defined.
 */
#define OPTIONS_DEBUG_MASK	(0x1UL << 10)

#define SUPPORT_BUFFER_SYNC_OPTION	"BUFFER_SYNC  "
#if defined(SUPPORT_BUFFER_SYNC) || defined(INTERNAL_TEST)
	#define SUPPORT_BUFFER_SYNC_SET_OFFSET	OPTIONS_BIT11
	#define OPTIONS_BIT11		(0x1UL << 11)
	#if OPTIONS_BIT11 > RGX_BUILD_OPTIONS_MASK_KM
	#error "Bit exceeds reserved range"
	#endif
#else
	#define OPTIONS_BIT11		0x0UL
#endif /* SUPPORT_BUFFER_SYNC */

#define SUPPORT_AUTOVZ_OPTION	"AUTOVZ  "
#if defined(SUPPORT_AUTOVZ)
	#define SUPPORT_AUTOVZ_OFFSET OPTIONS_BIT12
	#define OPTIONS_BIT12     (0x1UL << 12)
	#if OPTIONS_BIT12 > RGX_BUILD_OPTIONS_MASK_KM
	#error "Bit exceeds reserved range"
	#endif
#else
	#define OPTIONS_BIT12     0x0UL
#endif /* SUPPORT_AUTOVZ */

#define SUPPORT_AUTOVZ_HW_REGS_OPTION	"AUTOVZ_HW_REGS  "
#if defined(SUPPORT_AUTOVZ_HW_REGS)
	#define SUPPORT_AUTOVZ_HW_REGS_OFFSET OPTIONS_BIT13
	#define OPTIONS_BIT13     (0x1UL << 13)
	#if OPTIONS_BIT13 > RGX_BUILD_OPTIONS_MASK_KM
	#error "Bit exceeds reserved range"
	#endif
#else
	#define OPTIONS_BIT13     0x0UL
#endif /* SUPPORT_AUTOVZ_HW_REGS */

#define RGX_FW_IRQ_OS_COUNTERS_OPTION	"FW_IRQ_OS_COUNTERS  "
#if defined(RGX_FW_IRQ_OS_COUNTERS) || defined(INTERNAL_TEST)
	#define SUPPORT_FW_IRQ_REG_COUNTERS		OPTIONS_BIT14
	#define OPTIONS_BIT14		(0x1UL << 14)
	#if OPTIONS_BIT14 > RGX_BUILD_OPTIONS_MASK_KM
	#error "Bit exceeds reserved range"
	#endif
#else
	#define OPTIONS_BIT14		0x0UL
#endif /* RGX_FW_IRQ_OS_COUNTERS */

#define VALIDATION_EN_MASK	(0x1UL << 15)
#define SUPPORT_VALIDATION_OPTION	"VALIDATION  "
#if defined(SUPPORT_VALIDATION)
	#define SUPPORT_VALIDATION_OFFSET		OPTIONS_BIT15
	#define OPTIONS_BIT15		(0x1UL << 15)
	#if OPTIONS_BIT15 > RGX_BUILD_OPTIONS_MASK_KM
	#error "Bit exceeds reserved range"
	#endif
#else
	#define OPTIONS_BIT15		0x0UL
#endif /* SUPPORT_VALIDATION */

#define RGX_BUILD_OPTIONS_KM	\
	(OPTIONS_BIT0  |\
	 OPTIONS_BIT1  |\
	 OPTIONS_BIT2  |\
	 OPTIONS_BIT3  |\
	 OPTIONS_BIT4  |\
	 OPTIONS_BIT6  |\
	 OPTIONS_BIT7  |\
	 OPTIONS_BIT8  |\
	 OPTIONS_BIT9  |\
	 OPTIONS_BIT10 |\
	 OPTIONS_BIT11 |\
	 OPTIONS_BIT12 |\
	 OPTIONS_BIT13 |\
	 OPTIONS_BIT14 |\
	 OPTIONS_BIT15)

#define RGX_BUILD_OPTIONS_LIST	\
	{ \
		NO_HARDWARE_OPTION, \
		PDUMP_OPTION, \
		INTERNAL_TEST_OPTION, \
		UNUSED_OPTION, \
		SUPPORT_RGX_OPTION, \
		SUPPORT_SECURE_EXPORT_OPTION, \
		SUPPORT_INSECURE_EXPORT_OPTION, \
		SUPPORT_VFP_OPTION, \
		SUPPORT_WORKLOAD_ESTIMATION_OPTION, \
		SUPPORT_PDVFS_OPTION, \
		DEBUG_OPTION, \
		SUPPORT_BUFFER_SYNC_OPTION, \
		SUPPORT_AUTOVZ_OPTION, \
		SUPPORT_AUTOVZ_HW_REGS_OPTION, \
		RGX_FW_IRQ_OS_COUNTERS_OPTION, \
		SUPPORT_VALIDATION_OPTION \
	}

#define RGX_BUILD_OPTIONS_MASK_FW \
	(RGX_BUILD_OPTIONS_MASK_KM & \
	 ~OPTIONS_BIT11)

#define OPTIONS_BIT31		(0x1UL << 31)
#if OPTIONS_BIT31 <= RGX_BUILD_OPTIONS_MASK_KM
#error "Bit exceeds reserved range"
#endif
#define SUPPORT_PERCONTEXT_FREELIST_SET_OFFSET	OPTIONS_BIT31

#define RGX_BUILD_OPTIONS (RGX_BUILD_OPTIONS_KM | OPTIONS_BIT31)

#define OPTIONS_STRICT (RGX_BUILD_OPTIONS &                  \
                        ~(OPTIONS_DEBUG_MASK               | \
                          OPTIONS_WORKLOAD_ESTIMATION_MASK | \
                          OPTIONS_PDVFS_MASK))

#endif /* RGX_OPTIONS_H */
