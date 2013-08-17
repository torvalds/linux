/*************************************************************************/ /*!
@Title          PVRPDP VESA driver functions
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

#if !defined __VESAGTF_H__
#define __VESAGTF_H__

typedef struct _dlmode
{
	unsigned long		ulXExt;
	unsigned long		ulYExt;
	unsigned long		ulRefresh;
} DLMODE, *PDLMODE;

void InitSCB(PVRPDP_DEVINFO *psDevInfo);
void UnInitSCB(PVRPDP_DEVINFO *psDevInfo);
void ResetPDP1(PVRPDP_DEVINFO *psDevInfo);
void SelectBus(PVRPDP_DEVINFO *psDevInfo, unsigned long ulBus);
void SetSCBAddress (PVRPDP_DEVINFO *psDevInfo, unsigned long i2cAddr);
PDP_BOOL WriteManyBytes (PVRPDP_DEVINFO *psDevInfo, unsigned long i2cAddr, unsigned long ulHowMany, unsigned long *paWriteBlk);
PDP_BOOL ReadManyBytes(PVRPDP_DEVINFO *psDevInfo, unsigned long ulHowMany, unsigned long *paReadBlk);
PDP_BOOL ProgRead (PVRPDP_DEVINFO *psDevInfo, unsigned long uiDataAddr, unsigned long *puiData);
PDP_BOOL ProgWrite(PVRPDP_DEVINFO *psDevInfo, unsigned long uiDataAddr, unsigned long uiData);
void SetPLLtoOut(PVRPDP_DEVINFO *psDevInfo, unsigned long uiPLL, unsigned long uiOutput);
void SetOutputDiv(PVRPDP_DEVINFO *psDevInfo, unsigned long uiOutput, unsigned long uiValue);
void SetDconfig(PVRPDP_DEVINFO *psDevInfo, unsigned long uiNum, unsigned long uiValue);
void SetNconfigLower(PVRPDP_DEVINFO *psDevInfo, unsigned long uiNum, unsigned long uiValue);
void SetNconfigUpper(PVRPDP_DEVINFO *psDevInfo, unsigned long uiNum, unsigned long uiValue);
void SetRCC(PVRPDP_DEVINFO *psDevInfo, unsigned long uiInnerPLL, unsigned long uiRz, unsigned long uiCp, unsigned long uiCz);
void SetCoeffs(PVRPDP_DEVINFO *psDevInfo, unsigned long uiPLL, unsigned long uiD, unsigned long uiM);
void SysSetPDP1Clk (PVRPDP_DEVINFO *psDevInfo, unsigned long uiClkInHz);
void GTFCalcFromRefresh(PDLMODE psUser, PHVTIMEREGS psHVTRegs);

#if defined(__linux__)
typedef double PDP_DOUBLE;

#if defined(__i386__)
#include "asm/i387.h"
#else
#error "Unsupported architecture"
#endif

#define	PDP_FPU_BEGIN() kernel_fpu_begin()
#define	PDP_FPU_END() kernel_fpu_end()

static inline double fabs(double x)
{
	return (x >= 0) ? x : -x;
}

static inline double floor(double x)
{
	if (x != (x + 1))
	{
		BUG_ON(x > (double)LLONG_MAX || x < (double)LLONG_MIN);
		{ 
			long long lli = (long long)x;
			double rx = (double)lli;

			if (rx == x)
			{
				return x;
			}

			return (x >= 0) ? rx : (rx - 1);
		}
	}

	return x;
}

#else	/* defined(__linux__) */
typedef IMG_DOUBLE PDP_DOUBLE;

#define	PDP_FPU_BEGIN()
#define	PDP_FPU_END()
#endif	/* defined(__linux__) */

#endif /*  __VESAGTF_H__ */

