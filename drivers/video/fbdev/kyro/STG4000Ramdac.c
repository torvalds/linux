/*
 *  linux/drivers/video/kyro/STG4000Ramdac.c
 *
 *  Copyright (C) 2002 STMicroelectronics
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <video/kyro.h>

#include "STG4000Reg.h"
#include "STG4000Interface.h"

static u32 STG_PIXEL_BUS_WIDTH = 128;	/* 128 bit bus width      */
static u32 REF_CLOCK = 14318;

int InitialiseRamdac(volatile STG4000REG __iomem * pSTGReg,
		     u32 displayDepth,
		     u32 displayWidth,
		     u32 displayHeight,
		     s32 HSyncPolarity,
		     s32 VSyncPolarity, u32 * pixelClock)
{
	u32 tmp = 0;
	u32 F = 0, R = 0, P = 0;
	u32 stride = 0;
	u32 ulPdiv = 0;
	u32 physicalPixelDepth = 0;
	/* Make sure DAC is in Reset */
	tmp = STG_READ_REG(SoftwareReset);

	if (tmp & 0x1) {
		CLEAR_BIT(1);
		STG_WRITE_REG(SoftwareReset, tmp);
	}

	/* Set Pixel Format */
	tmp = STG_READ_REG(DACPixelFormat);
	CLEAR_BITS_FRM_TO(0, 2);

	/* Set LUT not used from 16bpp to 32 bpp ??? */
	CLEAR_BITS_FRM_TO(8, 9);

	switch (displayDepth) {
	case 16:
		{
			physicalPixelDepth = 16;
			tmp |= _16BPP;
			break;
		}
	case 32:
		{
			/* Set for 32 bits per pixel */
			physicalPixelDepth = 32;
			tmp |= _32BPP;
			break;
		}
	default:
		return -EINVAL;
	}

	STG_WRITE_REG(DACPixelFormat, tmp);

	/* Workout Bus transfer bandwidth according to pixel format */
	ulPdiv = STG_PIXEL_BUS_WIDTH / physicalPixelDepth;

	/* Get Screen Stride in pixels */
	stride = displayWidth;

	/* Set Primary size info */
	tmp = STG_READ_REG(DACPrimSize);
	CLEAR_BITS_FRM_TO(0, 10);
	CLEAR_BITS_FRM_TO(12, 31);
	tmp |=
	    ((((displayHeight - 1) << 12) | (((displayWidth / ulPdiv) -
					      1) << 23))
	     | (stride / ulPdiv));
	STG_WRITE_REG(DACPrimSize, tmp);


	/* Set Pixel Clock */
	*pixelClock = ProgramClock(REF_CLOCK, *pixelClock, &F, &R, &P);

	/* Set DAC PLL Mode */
	tmp = STG_READ_REG(DACPLLMode);
	CLEAR_BITS_FRM_TO(0, 15);
	/* tmp |= ((P-1) | ((F-2) << 2) | ((R-2) << 11)); */
	tmp |= ((P) | ((F - 2) << 2) | ((R - 2) << 11));
	STG_WRITE_REG(DACPLLMode, tmp);

	/* Set Prim Address */
	tmp = STG_READ_REG(DACPrimAddress);
	CLEAR_BITS_FRM_TO(0, 20);
	CLEAR_BITS_FRM_TO(20, 31);
	STG_WRITE_REG(DACPrimAddress, tmp);

	/* Set Cursor details with HW Cursor disabled */
	tmp = STG_READ_REG(DACCursorCtrl);
	tmp &= ~SET_BIT(31);
	STG_WRITE_REG(DACCursorCtrl, tmp);

	tmp = STG_READ_REG(DACCursorAddr);
	CLEAR_BITS_FRM_TO(0, 20);
	STG_WRITE_REG(DACCursorAddr, tmp);

	/* Set Video Window */
	tmp = STG_READ_REG(DACVidWinStart);
	CLEAR_BITS_FRM_TO(0, 10);
	CLEAR_BITS_FRM_TO(16, 26);
	STG_WRITE_REG(DACVidWinStart, tmp);

	tmp = STG_READ_REG(DACVidWinEnd);
	CLEAR_BITS_FRM_TO(0, 10);
	CLEAR_BITS_FRM_TO(16, 26);
	STG_WRITE_REG(DACVidWinEnd, tmp);

	/* Set DAC Border Color to default */
	tmp = STG_READ_REG(DACBorderColor);
	CLEAR_BITS_FRM_TO(0, 23);
	STG_WRITE_REG(DACBorderColor, tmp);

	/* Set Graphics and Overlay Burst Control */
	STG_WRITE_REG(DACBurstCtrl, 0x0404);

	/* Set CRC Trigger to default */
	tmp = STG_READ_REG(DACCrcTrigger);
	CLEAR_BIT(0);
	STG_WRITE_REG(DACCrcTrigger, tmp);

	/* Set Video Port Control to default */
	tmp = STG_READ_REG(DigVidPortCtrl);
	CLEAR_BIT(8);
	CLEAR_BITS_FRM_TO(16, 27);
	CLEAR_BITS_FRM_TO(1, 3);
	CLEAR_BITS_FRM_TO(10, 11);
	STG_WRITE_REG(DigVidPortCtrl, tmp);

	return 0;
}

/* Ramdac control, turning output to the screen on and off */
void DisableRamdacOutput(volatile STG4000REG __iomem * pSTGReg)
{
	u32 tmp;

	/* Disable DAC for Graphics Stream Control */
	tmp = (STG_READ_REG(DACStreamCtrl)) & ~SET_BIT(0);
	STG_WRITE_REG(DACStreamCtrl, tmp);
}

void EnableRamdacOutput(volatile STG4000REG __iomem * pSTGReg)
{
	u32 tmp;

	/* Enable DAC for Graphics Stream Control */
	tmp = (STG_READ_REG(DACStreamCtrl)) | SET_BIT(0);
	STG_WRITE_REG(DACStreamCtrl, tmp);
}
