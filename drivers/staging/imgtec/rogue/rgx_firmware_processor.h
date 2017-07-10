/*************************************************************************/ /*!
@File           rgx_firmware_processor.h
@Title
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Platform       RGX
@Description    Generic include file for firmware processors (META and MIPS)
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


#if !defined(RGX_FIRMWARE_PROCESSOR_H)
#define RGX_FIRMWARE_PROCESSOR_H

#include "km/rgxdefs_km.h"

#include "rgx_meta.h"
#include "rgx_mips.h"

/* Processor independent need to be defined here common for all processors */
typedef enum
{
	FW_PERF_CONF_NONE = 0,
	FW_PERF_CONF_ICACHE = 1,
	FW_PERF_CONF_DCACHE = 2,
	FW_PERF_CONF_POLLS = 3,
	FW_PERF_CONF_CUSTOM_TIMER = 4,
	FW_PERF_CONF_JTLB_INSTR = 5,
	FW_PERF_CONF_INSTRUCTIONS = 6
} FW_PERF_CONF;

#if !defined(__KERNEL__)
	#if defined(RGX_FEATURE_MIPS)

		#define FW_CORE_ID_VALUE                                      RGXMIPSFW_CORE_ID_VALUE
		#define RGXFW_PROCESSOR                                       RGXFW_PROCESSOR_MIPS

		/* Firmware to host interrupts defines */
		#define RGXFW_CR_IRQ_STATUS                                   RGX_CR_MIPS_WRAPPER_IRQ_STATUS
		#define RGXFW_CR_IRQ_STATUS_EVENT_EN                          RGX_CR_MIPS_WRAPPER_IRQ_STATUS_EVENT_EN
		#define RGXFW_CR_IRQ_CLEAR                                    RGX_CR_MIPS_WRAPPER_IRQ_CLEAR
		#define RGXFW_CR_IRQ_CLEAR_MASK                               RGX_CR_MIPS_WRAPPER_IRQ_CLEAR_EVENT_EN

	#else

		#define RGXFW_PROCESSOR         RGXFW_PROCESSOR_META

		/* Firmware to host interrupts defines */
		#define RGXFW_CR_IRQ_STATUS           RGX_CR_META_SP_MSLVIRQSTATUS
		#define RGXFW_CR_IRQ_STATUS_EVENT_EN  RGX_CR_META_SP_MSLVIRQSTATUS_TRIGVECT2_EN
		#define RGXFW_CR_IRQ_CLEAR            RGX_CR_META_SP_MSLVIRQSTATUS
		#define RGXFW_CR_IRQ_CLEAR_MASK       (RGX_CR_META_SP_MSLVIRQSTATUS_TRIGVECT2_CLRMSK & \
													RGX_CR_META_SP_MSLVIRQSTATUS_MASKFULL)

	#endif
#endif

#endif /* RGX_FIRMWARE_PROCESSOR_H */
