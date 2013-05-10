/*
 *  linux/drivers/video/kyro/STG4000VTG.c
 *
 *  Copyright (C) 2002 STMicroelectronics
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */

#include <linux/types.h>
#include <video/kyro.h>

#include "STG4000Reg.h"
#include "STG4000Interface.h"

void DisableVGA(volatile STG4000REG __iomem *pSTGReg)
{
	u32 tmp;
	volatile u32 count = 0, i;

	/* Reset the VGA registers */
	tmp = STG_READ_REG(SoftwareReset);
	CLEAR_BIT(8);
	STG_WRITE_REG(SoftwareReset, tmp);

	/* Just for Delay */
	for (i = 0; i < 1000; i++) {
		count++;
	}

	/* Pull-out the VGA registers from reset */
	tmp = STG_READ_REG(SoftwareReset);
	tmp |= SET_BIT(8);
	STG_WRITE_REG(SoftwareReset, tmp);
}

void StopVTG(volatile STG4000REG __iomem *pSTGReg)
{
	u32 tmp = 0;

	/* Stop Ver and Hor Sync Generator */
	tmp = (STG_READ_REG(DACSyncCtrl)) | SET_BIT(0) | SET_BIT(2);
	CLEAR_BIT(31);
	STG_WRITE_REG(DACSyncCtrl, tmp);
}

void StartVTG(volatile STG4000REG __iomem *pSTGReg)
{
	u32 tmp = 0;

	/* Start Ver and Hor Sync Generator */
	tmp = ((STG_READ_REG(DACSyncCtrl)) | SET_BIT(31));
	CLEAR_BIT(0);
	CLEAR_BIT(2);
	STG_WRITE_REG(DACSyncCtrl, tmp);
}

void SetupVTG(volatile STG4000REG __iomem *pSTGReg,
	      const struct kyrofb_info * pTiming)
{
	u32 tmp = 0;
	u32 margins = 0;
	u32 ulBorder;
	u32 xRes = pTiming->XRES;
	u32 yRes = pTiming->YRES;

	/* Horizontal */
	u32 HAddrTime, HRightBorder, HLeftBorder;
	u32 HBackPorcStrt, HFrontPorchStrt, HTotal,
	    HLeftBorderStrt, HRightBorderStrt, HDisplayStrt;

	/* Vertical */
	u32 VDisplayStrt, VBottomBorder, VTopBorder;
	u32 VBackPorchStrt, VTotal, VTopBorderStrt,
	    VFrontPorchStrt, VBottomBorderStrt, VAddrTime;

	/* Need to calculate the right border */
	if ((xRes == 640) && (yRes == 480)) {
		if ((pTiming->VFREQ == 60) || (pTiming->VFREQ == 72)) {
			margins = 8;
		}
	}

	/* Work out the Border */
	ulBorder =
	    (pTiming->HTot -
	     (pTiming->HST + (pTiming->HBP - margins) + xRes +
	      (pTiming->HFP - margins))) >> 1;

	/* Border the same for Vertical and Horizontal */
	VBottomBorder = HLeftBorder = VTopBorder = HRightBorder = ulBorder;

    /************ Get Timing values for Horizontal ******************/
	HAddrTime = xRes;
	HBackPorcStrt = pTiming->HST;
	HTotal = pTiming->HTot;
	HDisplayStrt =
	    pTiming->HST + (pTiming->HBP - margins) + HLeftBorder;
	HLeftBorderStrt = HDisplayStrt - HLeftBorder;
	HFrontPorchStrt =
	    pTiming->HST + (pTiming->HBP - margins) + HLeftBorder +
	    HAddrTime + HRightBorder;
	HRightBorderStrt = HFrontPorchStrt - HRightBorder;

    /************ Get Timing values for Vertical ******************/
	VAddrTime = yRes;
	VBackPorchStrt = pTiming->VST;
	VTotal = pTiming->VTot;
	VDisplayStrt =
	    pTiming->VST + (pTiming->VBP - margins) + VTopBorder;
	VTopBorderStrt = VDisplayStrt - VTopBorder;
	VFrontPorchStrt =
	    pTiming->VST + (pTiming->VBP - margins) + VTopBorder +
	    VAddrTime + VBottomBorder;
	VBottomBorderStrt = VFrontPorchStrt - VBottomBorder;

	/* Set Hor Timing 1, 2, 3 */
	tmp = STG_READ_REG(DACHorTim1);
	CLEAR_BITS_FRM_TO(0, 11);
	CLEAR_BITS_FRM_TO(16, 27);
	tmp |= (HTotal) | (HBackPorcStrt << 16);
	STG_WRITE_REG(DACHorTim1, tmp);

	tmp = STG_READ_REG(DACHorTim2);
	CLEAR_BITS_FRM_TO(0, 11);
	CLEAR_BITS_FRM_TO(16, 27);
	tmp |= (HDisplayStrt << 16) | HLeftBorderStrt;
	STG_WRITE_REG(DACHorTim2, tmp);

	tmp = STG_READ_REG(DACHorTim3);
	CLEAR_BITS_FRM_TO(0, 11);
	CLEAR_BITS_FRM_TO(16, 27);
	tmp |= (HFrontPorchStrt << 16) | HRightBorderStrt;
	STG_WRITE_REG(DACHorTim3, tmp);

	/* Set Ver Timing 1, 2, 3 */
	tmp = STG_READ_REG(DACVerTim1);
	CLEAR_BITS_FRM_TO(0, 11);
	CLEAR_BITS_FRM_TO(16, 27);
	tmp |= (VBackPorchStrt << 16) | (VTotal);
	STG_WRITE_REG(DACVerTim1, tmp);

	tmp = STG_READ_REG(DACVerTim2);
	CLEAR_BITS_FRM_TO(0, 11);
	CLEAR_BITS_FRM_TO(16, 27);
	tmp |= (VDisplayStrt << 16) | VTopBorderStrt;
	STG_WRITE_REG(DACVerTim2, tmp);

	tmp = STG_READ_REG(DACVerTim3);
	CLEAR_BITS_FRM_TO(0, 11);
	CLEAR_BITS_FRM_TO(16, 27);
	tmp |= (VFrontPorchStrt << 16) | VBottomBorderStrt;
	STG_WRITE_REG(DACVerTim3, tmp);

	/* Set Verical and Horizontal Polarity */
	tmp = STG_READ_REG(DACSyncCtrl) | SET_BIT(3) | SET_BIT(1);

	if ((pTiming->HSP > 0) && (pTiming->VSP < 0)) {	/* +hsync -vsync */
		tmp &= ~0x8;
	} else if ((pTiming->HSP < 0) && (pTiming->VSP > 0)) {	/* -hsync +vsync */
		tmp &= ~0x2;
	} else if ((pTiming->HSP < 0) && (pTiming->VSP < 0)) {	/* -hsync -vsync */
		tmp &= ~0xA;
	} else if ((pTiming->HSP > 0) && (pTiming->VSP > 0)) {	/* +hsync -vsync */
		tmp &= ~0x0;
	}

	STG_WRITE_REG(DACSyncCtrl, tmp);
}
