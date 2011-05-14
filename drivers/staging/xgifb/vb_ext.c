#include <linux/version.h>
#include <asm/io.h>
#include <linux/types.h>
#include "XGIfb.h"

#include "vb_def.h"
#include "vgatypes.h"
#include "vb_struct.h"
#include "vb_util.h"
#include "vb_setmode.h"
#include "vb_ext.h"

/**************************************************************
 *********************** Dynamic Sense ************************
 *************************************************************/

static unsigned char XGINew_Is301B(struct vb_device_info *pVBInfo)
{
	unsigned short flag;

	flag = xgifb_reg_get(pVBInfo->Part4Port, 0x01);

	if (flag > 0x0B0)
		return 0; /* 301b */
	else
		return 1;
}

static unsigned char XGINew_Sense(unsigned short tempbx, unsigned short tempcx, struct vb_device_info *pVBInfo)
{
	unsigned short temp, i, tempch;

	temp = tempbx & 0xFF;
	xgifb_reg_set(pVBInfo->Part4Port, 0x11, temp);
	temp = (tempbx & 0xFF00) >> 8;
	temp |= (tempcx & 0x00FF);
	xgifb_reg_and_or(pVBInfo->Part4Port, 0x10, ~0x1F, temp);

	for (i = 0; i < 10; i++)
		XGI_LongWait(pVBInfo);

	tempch = (tempcx & 0x7F00) >> 8;
	temp = xgifb_reg_get(pVBInfo->Part4Port, 0x03);
	temp = temp ^ (0x0E);
	temp &= tempch;

	if (temp > 0)
		return 1;
	else
		return 0;
}

static unsigned char XGINew_GetLCDDDCInfo(struct xgi_hw_device_info *HwDeviceExtension, struct vb_device_info *pVBInfo)
{
	unsigned short temp;

	/* add lcd sense */
	if (HwDeviceExtension->ulCRT2LCDType == LCD_UNKNOWN) {
		return 0;
	} else {
		temp = (unsigned short) HwDeviceExtension->ulCRT2LCDType;
		switch (HwDeviceExtension->ulCRT2LCDType) {
		case LCD_INVALID:
		case LCD_800x600:
		case LCD_1024x768:
		case LCD_1280x1024:
			break;

		case LCD_640x480:
		case LCD_1024x600:
		case LCD_1152x864:
		case LCD_1280x960:
		case LCD_1152x768:
			temp = 0;
			break;

		case LCD_1400x1050:
		case LCD_1280x768:
		case LCD_1600x1200:
			break;

		case LCD_1920x1440:
		case LCD_2048x1536:
			temp = 0;
			break;

		default:
			break;
		}
		xgifb_reg_and_or(pVBInfo->P3d4, 0x36, 0xF0, temp);
		return 1;
	}
}

static unsigned char XGINew_GetPanelID(struct vb_device_info *pVBInfo)
{
	unsigned short PanelTypeTable[16] = { SyncNN | PanelRGB18Bit
			| Panel800x600  | _PanelType00, SyncNN | PanelRGB18Bit
			| Panel1024x768 | _PanelType01, SyncNN | PanelRGB18Bit
			| Panel800x600  | _PanelType02, SyncNN | PanelRGB18Bit
			| Panel640x480  | _PanelType03, SyncNN | PanelRGB18Bit
			| Panel1024x768 | _PanelType04, SyncNN | PanelRGB18Bit
			| Panel1024x768 | _PanelType05, SyncNN | PanelRGB18Bit
			| Panel1024x768 | _PanelType06, SyncNN | PanelRGB24Bit
			| Panel1024x768 | _PanelType07, SyncNN | PanelRGB18Bit
			| Panel800x600  | _PanelType08, SyncNN | PanelRGB18Bit
			| Panel1024x768 | _PanelType09, SyncNN | PanelRGB18Bit
			| Panel800x600  | _PanelType0A, SyncNN | PanelRGB18Bit
			| Panel1024x768 | _PanelType0B, SyncNN | PanelRGB18Bit
			| Panel1024x768 | _PanelType0C, SyncNN | PanelRGB24Bit
			| Panel1024x768 | _PanelType0D, SyncNN | PanelRGB18Bit
			| Panel1024x768 | _PanelType0E, SyncNN | PanelRGB18Bit
			| Panel1024x768 | _PanelType0F };
	unsigned short tempax, tempbx, temp;
	/* unsigned short return_flag; */

	tempax = xgifb_reg_get(pVBInfo->P3c4, 0x1A);
	tempbx = tempax & 0x1E;

	if (tempax == 0)
		return 0;
	else {
		/*
		if (!(tempax & 0x10)) {
			if (pVBInfo->IF_DEF_LVDS == 1) {
				tempbx = 0;
				temp = xgifb_reg_get(pVBInfo->P3c4, 0x38);
				if (temp & 0x40)
					tempbx |= 0x08;
				if (temp & 0x20)
					tempbx |= 0x02;
				if (temp & 0x01)
					tempbx |= 0x01;

				temp = xgifb_reg_get(pVBInfo->P3c4, 0x39);
				if (temp & 0x80)
					tempbx |= 0x04;
			 } else {
				return(0);
			 }
		}
		*/

		tempbx = tempbx >> 1;
		temp = tempbx & 0x00F;
		xgifb_reg_set(pVBInfo->P3d4, 0x36, temp);
		tempbx--;
		tempbx = PanelTypeTable[tempbx];

		temp = (tempbx & 0xFF00) >> 8;
		xgifb_reg_and_or(pVBInfo->P3d4, 0x37, ~(LCDSyncBit
				| LCDRGB18Bit), temp);
		return 1;
	}
}

static unsigned char XGINew_BridgeIsEnable(struct xgi_hw_device_info *HwDeviceExtension, struct vb_device_info *pVBInfo)
{
	unsigned short flag;

	if (XGI_BridgeIsOn(pVBInfo) == 0) {
		flag = xgifb_reg_get(pVBInfo->Part1Port, 0x0);

		if (flag & 0x050)
			return 1;
		else
			return 0;

	}
	return 0;
}

static unsigned char XGINew_SenseHiTV(struct xgi_hw_device_info *HwDeviceExtension, struct vb_device_info *pVBInfo)
{
	unsigned short tempbx, tempcx, temp, i, tempch;

	tempbx = *pVBInfo->pYCSenseData2;

	tempcx = 0x0604;

	temp = tempbx & 0xFF;
	xgifb_reg_set(pVBInfo->Part4Port, 0x11, temp);
	temp = (tempbx & 0xFF00) >> 8;
	temp |= (tempcx & 0x00FF);
	xgifb_reg_and_or(pVBInfo->Part4Port, 0x10, ~0x1F, temp);

	for (i = 0; i < 10; i++)
		XGI_LongWait(pVBInfo);

	tempch = (tempcx & 0xFF00) >> 8;
	temp = xgifb_reg_get(pVBInfo->Part4Port, 0x03);
	temp = temp ^ (0x0E);
	temp &= tempch;

	if (temp != tempch)
		return 0;

	tempbx = *pVBInfo->pVideoSenseData2;

	tempcx = 0x0804;
	temp = tempbx & 0xFF;
	xgifb_reg_set(pVBInfo->Part4Port, 0x11, temp);
	temp = (tempbx & 0xFF00) >> 8;
	temp |= (tempcx & 0x00FF);
	xgifb_reg_and_or(pVBInfo->Part4Port, 0x10, ~0x1F, temp);

	for (i = 0; i < 10; i++)
		XGI_LongWait(pVBInfo);

	tempch = (tempcx & 0xFF00) >> 8;
	temp = xgifb_reg_get(pVBInfo->Part4Port, 0x03);
	temp = temp ^ (0x0E);
	temp &= tempch;

	if (temp != tempch) {
		return 0;
	} else {
		tempbx = 0x3FF;
		tempcx = 0x0804;
		temp = tempbx & 0xFF;
		xgifb_reg_set(pVBInfo->Part4Port, 0x11, temp);
		temp = (tempbx & 0xFF00) >> 8;
		temp |= (tempcx & 0x00FF);
		xgifb_reg_and_or(pVBInfo->Part4Port, 0x10, ~0x1F, temp);

		for (i = 0; i < 10; i++)
			XGI_LongWait(pVBInfo);

		tempch = (tempcx & 0xFF00) >> 8;
		temp = xgifb_reg_get(pVBInfo->Part4Port, 0x03);
		temp = temp ^ (0x0E);
		temp &= tempch;

		if (temp != tempch)
			return 1;
		else
			return 0;
	}
}

void XGI_GetSenseStatus(struct xgi_hw_device_info *HwDeviceExtension, struct vb_device_info *pVBInfo)
{
	unsigned short tempax = 0, tempbx, tempcx, temp, P2reg0 = 0, SenseModeNo = 0,
			OutputSelect = *pVBInfo->pOutputSelect, ModeIdIndex, i;
	pVBInfo->BaseAddr = (unsigned long) HwDeviceExtension->pjIOAddress;

	if (pVBInfo->IF_DEF_LVDS == 1) {
		tempax = xgifb_reg_get(pVBInfo->P3c4, 0x1A); /* ynlai 02/27/2002 */
		tempbx = xgifb_reg_get(pVBInfo->P3c4, 0x1B);
		tempax = ((tempax & 0xFE) >> 1) | (tempbx << 8);
		if (tempax == 0x00) { /* Get Panel id from DDC */
			temp = XGINew_GetLCDDDCInfo(HwDeviceExtension, pVBInfo);
			if (temp == 1) { /* LCD connect */
				xgifb_reg_and_or(pVBInfo->P3d4, 0x39, 0xFF, 0x01); /* set CR39 bit0="1" */
				xgifb_reg_and_or(pVBInfo->P3d4, 0x37, 0xEF, 0x00); /* clean CR37 bit4="0" */
				temp = LCDSense;
			} else { /* LCD don't connect */
				temp = 0;
			}
		} else {
			XGINew_GetPanelID(pVBInfo);
			temp = LCDSense;
		}

		tempbx = ~(LCDSense | AVIDEOSense | SVIDEOSense);
		xgifb_reg_and_or(pVBInfo->P3d4, 0x32, tempbx, temp);
	} else { /* for 301 */
		if (pVBInfo->VBInfo & SetCRT2ToHiVisionTV) { /* for HiVision */
			tempax = xgifb_reg_get(pVBInfo->P3c4, 0x38);
			temp = tempax & 0x01;
			tempax = xgifb_reg_get(pVBInfo->P3c4, 0x3A);
			temp = temp | (tempax & 0x02);
			xgifb_reg_and_or(pVBInfo->P3d4, 0x32, 0xA0, temp);
		} else {
			if (XGI_BridgeIsOn(pVBInfo)) {
				P2reg0 = xgifb_reg_get(pVBInfo->Part2Port, 0x00);
				if (!XGINew_BridgeIsEnable(HwDeviceExtension, pVBInfo)) {
					SenseModeNo = 0x2e;
					/* xgifb_reg_set(pVBInfo->P3d4, 0x30, 0x41); */
					/* XGISetModeNew(HwDeviceExtension, 0x2e); // ynlai InitMode */

					temp = XGI_SearchModeID(SenseModeNo, &ModeIdIndex, pVBInfo);
					XGI_GetVGAType(HwDeviceExtension, pVBInfo);
					XGI_GetVBType(pVBInfo);
					pVBInfo->SetFlag = 0x00;
					pVBInfo->ModeType = ModeVGA;
					pVBInfo->VBInfo = SetCRT2ToRAMDAC | LoadDACFlag | SetInSlaveMode;
					XGI_GetLCDInfo(0x2e, ModeIdIndex, pVBInfo);
					XGI_GetTVInfo(0x2e, ModeIdIndex, pVBInfo);
					XGI_EnableBridge(HwDeviceExtension, pVBInfo);
					XGI_SetCRT2Group301(SenseModeNo, HwDeviceExtension, pVBInfo);
					XGI_SetCRT2ModeRegs(0x2e, HwDeviceExtension, pVBInfo);
					/* XGI_DisableBridge( HwDeviceExtension, pVBInfo ) ; */
					xgifb_reg_and_or(pVBInfo->P3c4, 0x01, 0xDF, 0x20); /* Display Off 0212 */
					for (i = 0; i < 20; i++)
						XGI_LongWait(pVBInfo);
				}
				xgifb_reg_set(pVBInfo->Part2Port, 0x00, 0x1c);
				tempax = 0;
				tempbx = *pVBInfo->pRGBSenseData;

				if (!(XGINew_Is301B(pVBInfo)))
					tempbx = *pVBInfo->pRGBSenseData2;

				tempcx = 0x0E08;
				if (XGINew_Sense(tempbx, tempcx, pVBInfo)) {
					if (XGINew_Sense(tempbx, tempcx, pVBInfo))
						tempax |= Monitor2Sense;
				}

				if (pVBInfo->VBType & VB_XGI301C)
					xgifb_reg_or(pVBInfo->Part4Port, 0x0d, 0x04);

				if (XGINew_SenseHiTV(HwDeviceExtension, pVBInfo)) { /* add by kuku for Multi-adapter sense HiTV */
					tempax |= HiTVSense;
					if ((pVBInfo->VBType & VB_XGI301C))
						tempax ^= (HiTVSense | YPbPrSense);
				}

				if (!(tempax & (HiTVSense | YPbPrSense))) { /* start */

					tempbx = *pVBInfo->pYCSenseData;

					if (!(XGINew_Is301B(pVBInfo)))
						tempbx = *pVBInfo->pYCSenseData2;

					tempcx = 0x0604;
					if (XGINew_Sense(tempbx, tempcx, pVBInfo)) {
						if (XGINew_Sense(tempbx, tempcx, pVBInfo))
							tempax |= SVIDEOSense;
					}

					if (OutputSelect & BoardTVType) {
						tempbx = *pVBInfo->pVideoSenseData;

						if (!(XGINew_Is301B(pVBInfo)))
							tempbx = *pVBInfo->pVideoSenseData2;

						tempcx = 0x0804;
						if (XGINew_Sense(tempbx, tempcx, pVBInfo)) {
							if (XGINew_Sense(tempbx, tempcx, pVBInfo))
								tempax |= AVIDEOSense;
						}
					} else {
						if (!(tempax & SVIDEOSense)) {
							tempbx = *pVBInfo->pVideoSenseData;

							if (!(XGINew_Is301B(pVBInfo)))
								tempbx = *pVBInfo->pVideoSenseData2;

							tempcx = 0x0804;
							if (XGINew_Sense(tempbx, tempcx, pVBInfo)) {
								if (XGINew_Sense(tempbx, tempcx, pVBInfo))
									tempax |= AVIDEOSense;
							}
						}
					}
				}
			} /* end */
			if (!(tempax & Monitor2Sense)) {
				if (XGINew_SenseLCD(HwDeviceExtension, pVBInfo))
					tempax |= LCDSense;
			}
			tempbx = 0;
			tempcx = 0;
			XGINew_Sense(tempbx, tempcx, pVBInfo);

			xgifb_reg_and_or(pVBInfo->P3d4, 0x32, ~0xDF, tempax);
			xgifb_reg_set(pVBInfo->Part2Port, 0x00, P2reg0);

			if (!(P2reg0 & 0x20)) {
				pVBInfo->VBInfo = DisableCRT2Display;
				/* XGI_SetCRT2Group301(SenseModeNo, HwDeviceExtension, pVBInfo); */
			}
		}
	}
	XGI_DisableBridge(HwDeviceExtension, pVBInfo); /* shampoo 0226 */

}

unsigned short XGINew_SenseLCD(struct xgi_hw_device_info *HwDeviceExtension, struct vb_device_info *pVBInfo)
{
	/* unsigned short SoftSetting ; */
	unsigned short temp;

	temp = XGINew_GetLCDDDCInfo(HwDeviceExtension, pVBInfo);

	return temp;
}
