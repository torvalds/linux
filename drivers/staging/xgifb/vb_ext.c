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
extern unsigned char XGI330_SoftSetting;
extern unsigned char XGI330_OutputSelect;
extern unsigned short XGI330_RGBSenseData2;
extern unsigned short XGI330_YCSenseData2;
extern unsigned short XGI330_VideoSenseData2;
void XGI_GetSenseStatus(struct xgi_hw_device_info *HwDeviceExtension,
		struct vb_device_info *pVBInfo);
unsigned char XGINew_GetPanelID(struct vb_device_info *pVBInfo);
unsigned short XGINew_SenseLCD(struct xgi_hw_device_info *,
		struct vb_device_info *pVBInfo);
unsigned char XGINew_GetLCDDDCInfo(
		struct xgi_hw_device_info *HwDeviceExtension,
		struct vb_device_info *pVBInfo);
void XGISetDPMS(struct xgi_hw_device_info *pXGIHWDE,
		unsigned long VESA_POWER_STATE);
unsigned char XGINew_BridgeIsEnable(struct xgi_hw_device_info *,
		struct vb_device_info *pVBInfo);
unsigned char XGINew_Sense(unsigned short tempbx, unsigned short tempcx,
		struct vb_device_info *pVBInfo);
unsigned char XGINew_SenseHiTV(struct xgi_hw_device_info *HwDeviceExtension,
		struct vb_device_info *pVBInfo);

/**************************************************************
 *********************** Dynamic Sense ************************
 *************************************************************/

void XGI_WaitDisplay(void);
unsigned char XGI_Is301C(struct vb_device_info *);
unsigned char XGI_Is301LV(struct vb_device_info *);

static unsigned char XGINew_Is301B(struct vb_device_info *pVBInfo)
{
	unsigned short flag;

	flag = XGINew_GetReg1(pVBInfo->Part4Port, 0x01);

	if (flag > 0x0B0)
		return 0; /* 301b */
	else
		return 1;
}

unsigned char XGI_Is301C(struct vb_device_info *pVBInfo)
{
	if ((XGINew_GetReg1(pVBInfo->Part4Port, 0x01) & 0xF0) == 0xC0)
		return 1;

	if (XGINew_GetReg1(pVBInfo->Part4Port, 0x01) >= 0xD0) {
		if (XGINew_GetReg1(pVBInfo->Part4Port, 0x39) == 0xE0)
			return 1;
	}

	return 0;
}

unsigned char XGI_Is301LV(struct vb_device_info *pVBInfo)
{
	if (XGINew_GetReg1(pVBInfo->Part4Port, 0x01) >= 0xD0) {
		if (XGINew_GetReg1(pVBInfo->Part4Port, 0x39) == 0xFF)
			return 1;
	}
	return 0;
}

unsigned char XGINew_Sense(unsigned short tempbx, unsigned short tempcx, struct vb_device_info *pVBInfo)
{
	unsigned short temp, i, tempch;

	temp = tempbx & 0xFF;
	XGINew_SetReg1(pVBInfo->Part4Port, 0x11, temp);
	temp = (tempbx & 0xFF00) >> 8;
	temp |= (tempcx & 0x00FF);
	XGINew_SetRegANDOR(pVBInfo->Part4Port, 0x10, ~0x1F, temp);

	for (i = 0; i < 10; i++)
		XGI_LongWait(pVBInfo);

	tempch = (tempcx & 0x7F00) >> 8;
	temp = XGINew_GetReg1(pVBInfo->Part4Port, 0x03);
	temp = temp ^ (0x0E);
	temp &= tempch;

	if (temp > 0)
		return 1;
	else
		return 0;
}

void XGISetDPMS(struct xgi_hw_device_info *pXGIHWDE, unsigned long VESA_POWER_STATE)
{
	unsigned short ModeNo, ModeIdIndex;
	unsigned char temp;
	struct vb_device_info VBINF;
	struct vb_device_info *pVBInfo = &VBINF;
	pVBInfo->BaseAddr = (unsigned long) pXGIHWDE->pjIOAddress;
	pVBInfo->ROMAddr = pXGIHWDE->pjVirtualRomBase;

	pVBInfo->IF_DEF_LVDS = 0;
	pVBInfo->IF_DEF_CH7005 = 0;
	pVBInfo->IF_DEF_HiVision = 1;
	pVBInfo->IF_DEF_LCDA = 1;
	pVBInfo->IF_DEF_CH7017 = 0;
	pVBInfo->IF_DEF_YPbPr = 1;
	pVBInfo->IF_DEF_CRT2Monitor = 0;
	pVBInfo->IF_DEF_VideoCapture = 0;
	pVBInfo->IF_DEF_ScaleLCD = 0;
	pVBInfo->IF_DEF_OEMUtil = 0;
	pVBInfo->IF_DEF_PWD = 0;

	InitTo330Pointer(pXGIHWDE->jChipType, pVBInfo);
	ReadVBIOSTablData(pXGIHWDE->jChipType, pVBInfo);

	pVBInfo->P3c4 = pVBInfo->BaseAddr + 0x14;
	pVBInfo->P3d4 = pVBInfo->BaseAddr + 0x24;
	pVBInfo->P3c0 = pVBInfo->BaseAddr + 0x10;
	pVBInfo->P3ce = pVBInfo->BaseAddr + 0x1e;
	pVBInfo->P3c2 = pVBInfo->BaseAddr + 0x12;
	pVBInfo->P3ca = pVBInfo->BaseAddr + 0x1a;
	pVBInfo->P3c6 = pVBInfo->BaseAddr + 0x16;
	pVBInfo->P3c7 = pVBInfo->BaseAddr + 0x17;
	pVBInfo->P3c8 = pVBInfo->BaseAddr + 0x18;
	pVBInfo->P3c9 = pVBInfo->BaseAddr + 0x19;
	pVBInfo->P3da = pVBInfo->BaseAddr + 0x2A;
	pVBInfo->Part0Port = pVBInfo->BaseAddr + XGI_CRT2_PORT_00;
	pVBInfo->Part1Port = pVBInfo->BaseAddr + XGI_CRT2_PORT_04;
	pVBInfo->Part2Port = pVBInfo->BaseAddr + XGI_CRT2_PORT_10;
	pVBInfo->Part3Port = pVBInfo->BaseAddr + XGI_CRT2_PORT_12;
	pVBInfo->Part4Port = pVBInfo->BaseAddr + XGI_CRT2_PORT_14;
	pVBInfo->Part5Port = pVBInfo->BaseAddr + XGI_CRT2_PORT_14 + 2;

	if (pXGIHWDE->jChipType == XG27) {
		if ((XGINew_GetReg1(pVBInfo->P3d4, 0x38) & 0xE0) == 0xC0) {
			if (XGINew_GetReg1(pVBInfo->P3d4, 0x30) & 0x20)
				pVBInfo->IF_DEF_LVDS = 1;
		}
	}

	if (pVBInfo->IF_DEF_CH7007 == 0)
		XGINew_SetModeScratch(pXGIHWDE, pVBInfo);

	XGINew_SetReg1(pVBInfo->P3c4, 0x05, 0x86); /* 1.Openkey */
	XGI_UnLockCRT2(pXGIHWDE, pVBInfo);
	ModeNo = XGINew_GetReg1(pVBInfo->P3d4, 0x34);
	XGI_SearchModeID(ModeNo, &ModeIdIndex, pVBInfo);
	XGI_GetVGAType(pXGIHWDE, pVBInfo);

	if ((pXGIHWDE->ujVBChipID == VB_CHIP_301) || (pXGIHWDE->ujVBChipID == VB_CHIP_302) || (pVBInfo->IF_DEF_CH7007 == 1)) {
		XGI_GetVBType(pVBInfo);
		XGI_GetVBInfo(ModeNo, ModeIdIndex, pXGIHWDE, pVBInfo);
		XGI_GetTVInfo(ModeNo, ModeIdIndex, pVBInfo);
		XGI_GetLCDInfo(ModeNo, ModeIdIndex, pVBInfo);
	}

	if (VESA_POWER_STATE == 0x00000400)
		XGINew_SetReg1(pVBInfo->Part4Port, 0x31, (unsigned char) (XGINew_GetReg1(pVBInfo->Part4Port, 0x31) & 0xFE));
	else
		XGINew_SetReg1(pVBInfo->Part4Port, 0x31, (unsigned char) (XGINew_GetReg1(pVBInfo->Part4Port, 0x31) | 0x01));

	temp = (unsigned char) XGINew_GetReg1(pVBInfo->P3c4, 0x1f);
	temp &= 0x3f;
	switch (VESA_POWER_STATE) {
	case 0x00000000: /* on */
		if ((pXGIHWDE->ujVBChipID == VB_CHIP_301) || (pXGIHWDE->ujVBChipID == VB_CHIP_302)) {
			XGINew_SetReg1(pVBInfo->P3c4, 0x1f, (unsigned char) (temp | 0x00));
			XGI_EnableBridge(pXGIHWDE, pVBInfo);
		} else {
			if (pXGIHWDE->jChipType == XG21) {
				if (pVBInfo->IF_DEF_LVDS == 1) {
					XGI_XG21BLSignalVDD(0x01, 0x01, pVBInfo); /* LVDS VDD on */
					XGI_XG21SetPanelDelay(2, pVBInfo);
				}
			}
			if (pXGIHWDE->jChipType == XG27) {
				if (pVBInfo->IF_DEF_LVDS == 1) {
					XGI_XG27BLSignalVDD(0x01, 0x01, pVBInfo); /* LVDS VDD on */
					XGI_XG21SetPanelDelay(2, pVBInfo);
				}
			}
			XGINew_SetRegANDOR(pVBInfo->P3c4, 0x1F, ~0xC0, 0x00);
			XGINew_SetRegAND(pVBInfo->P3c4, 0x01, ~0x20); /* CRT on */

			if (pXGIHWDE->jChipType == XG21) {
				temp = XGINew_GetReg1(pVBInfo->P3d4, 0x38);
				if (temp & 0xE0) {
					XGINew_SetRegANDOR(pVBInfo->P3c4, 0x09, ~0x80, 0x80); /* DVO ON */
					XGI_SetXG21FPBits(pVBInfo);
					XGINew_SetRegAND(pVBInfo->P3d4, 0x4A, ~0x20); /* Enable write GPIOF */
					/* XGINew_SetRegANDOR(pVBInfo->P3d4, 0x48, ~0x20, 0x20); *//* LCD Display ON */
				}
				XGI_XG21BLSignalVDD(0x20, 0x20, pVBInfo); /* LVDS signal on */
				XGI_DisplayOn(pXGIHWDE, pVBInfo);
			}
			if (pXGIHWDE->jChipType == XG27) {
				temp = XGINew_GetReg1(pVBInfo->P3d4, 0x38);
				if (temp & 0xE0) {
					XGINew_SetRegANDOR(pVBInfo->P3c4, 0x09, ~0x80, 0x80); /* DVO ON */
					XGI_SetXG27FPBits(pVBInfo);
					XGINew_SetRegAND(pVBInfo->P3d4, 0x4A, ~0x20); /* Enable write GPIOF */
					/* XGINew_SetRegANDOR(pVBInfo->P3d4, 0x48, ~0x20, 0x20); *//* LCD Display ON */
				}
				XGI_XG27BLSignalVDD(0x20, 0x20, pVBInfo); /* LVDS signal on */
				XGI_DisplayOn(pXGIHWDE, pVBInfo);
			}
		}
		break;

	case 0x00000100: /* standby */
		if (pXGIHWDE->jChipType >= XG21)
			XGI_DisplayOff(pXGIHWDE, pVBInfo);
		XGINew_SetReg1(pVBInfo->P3c4, 0x1f, (unsigned char) (temp | 0x40));
		break;

	case 0x00000200: /* suspend */
		if (pXGIHWDE->jChipType == XG21) {
			XGI_DisplayOff(pXGIHWDE, pVBInfo);
			XGI_XG21BLSignalVDD(0x20, 0x00, pVBInfo); /* LVDS signal off */
		}
		if (pXGIHWDE->jChipType == XG27) {
			XGI_DisplayOff(pXGIHWDE, pVBInfo);
			XGI_XG27BLSignalVDD(0x20, 0x00, pVBInfo); /* LVDS signal off */
		}
		XGINew_SetReg1(pVBInfo->P3c4, 0x1f, (unsigned char) (temp | 0x80));
		break;

	case 0x00000400: /* off */
		if ((pXGIHWDE->ujVBChipID == VB_CHIP_301) || (pXGIHWDE->ujVBChipID == VB_CHIP_302)) {
			XGINew_SetReg1(pVBInfo->P3c4, 0x1f, (unsigned char) (temp | 0xc0));
			XGI_DisableBridge(pXGIHWDE, pVBInfo);
		} else {
			if (pXGIHWDE->jChipType == XG21) {
				XGI_DisplayOff(pXGIHWDE, pVBInfo);

				XGI_XG21BLSignalVDD(0x20, 0x00, pVBInfo); /* LVDS signal off */

				temp = XGINew_GetReg1(pVBInfo->P3d4, 0x38);
				if (temp & 0xE0) {
					XGINew_SetRegAND(pVBInfo->P3c4, 0x09, ~0x80); /* DVO Off */
					XGINew_SetRegAND(pVBInfo->P3d4, 0x4A, ~0x20); /* Enable write GPIOF */
					/* XGINew_SetRegAND(pVBInfo->P3d4, 0x48, ~0x20); *//* LCD Display OFF */
				}
			}
			if (pXGIHWDE->jChipType == XG27) {
				XGI_DisplayOff(pXGIHWDE, pVBInfo);

				XGI_XG27BLSignalVDD(0x20, 0x00, pVBInfo); /* LVDS signal off */

				temp = XGINew_GetReg1(pVBInfo->P3d4, 0x38);
				if (temp & 0xE0)
					XGINew_SetRegAND(pVBInfo->P3c4, 0x09, ~0x80); /* DVO Off */
			}
			XGINew_SetRegANDOR(pVBInfo->P3c4, 0x1F, ~0xC0, 0xC0);
			XGINew_SetRegOR(pVBInfo->P3c4, 0x01, 0x20); /* CRT Off */

			if ((pXGIHWDE->jChipType == XG21) && (pVBInfo->IF_DEF_LVDS == 1)) {
				XGI_XG21SetPanelDelay(4, pVBInfo);
				XGI_XG21BLSignalVDD(0x01, 0x00, pVBInfo); /* LVDS VDD off */
				XGI_XG21SetPanelDelay(5, pVBInfo);
			}
			if ((pXGIHWDE->jChipType == XG27) && (pVBInfo->IF_DEF_LVDS == 1)) {
				XGI_XG21SetPanelDelay(4, pVBInfo);
				XGI_XG27BLSignalVDD(0x01, 0x00, pVBInfo); /* LVDS VDD off */
				XGI_XG21SetPanelDelay(5, pVBInfo);
			}
		}
		break;

	default:
		break;
	}
	XGI_LockCRT2(pXGIHWDE, pVBInfo);
}

void XGI_GetSenseStatus(struct xgi_hw_device_info *HwDeviceExtension, struct vb_device_info *pVBInfo)
{
	unsigned short tempax = 0, tempbx, tempcx, temp, P2reg0 = 0, SenseModeNo = 0,
			OutputSelect = *pVBInfo->pOutputSelect, ModeIdIndex, i;
	pVBInfo->BaseAddr = (unsigned long) HwDeviceExtension->pjIOAddress;

	if (pVBInfo->IF_DEF_LVDS == 1) {
		tempax = XGINew_GetReg1(pVBInfo->P3c4, 0x1A); /* ynlai 02/27/2002 */
		tempbx = XGINew_GetReg1(pVBInfo->P3c4, 0x1B);
		tempax = ((tempax & 0xFE) >> 1) | (tempbx << 8);
		if (tempax == 0x00) { /* Get Panel id from DDC */
			temp = XGINew_GetLCDDDCInfo(HwDeviceExtension, pVBInfo);
			if (temp == 1) { /* LCD connect */
				XGINew_SetRegANDOR(pVBInfo->P3d4, 0x39, 0xFF, 0x01); /* set CR39 bit0="1" */
				XGINew_SetRegANDOR(pVBInfo->P3d4, 0x37, 0xEF, 0x00); /* clean CR37 bit4="0" */
				temp = LCDSense;
			} else { /* LCD don't connect */
				temp = 0;
			}
		} else {
			XGINew_GetPanelID(pVBInfo);
			temp = LCDSense;
		}

		tempbx = ~(LCDSense | AVIDEOSense | SVIDEOSense);
		XGINew_SetRegANDOR(pVBInfo->P3d4, 0x32, tempbx, temp);
	} else { /* for 301 */
		if (pVBInfo->VBInfo & SetCRT2ToHiVisionTV) { /* for HiVision */
			tempax = XGINew_GetReg1(pVBInfo->P3c4, 0x38);
			temp = tempax & 0x01;
			tempax = XGINew_GetReg1(pVBInfo->P3c4, 0x3A);
			temp = temp | (tempax & 0x02);
			XGINew_SetRegANDOR(pVBInfo->P3d4, 0x32, 0xA0, temp);
		} else {
			if (XGI_BridgeIsOn(pVBInfo)) {
				P2reg0 = XGINew_GetReg1(pVBInfo->Part2Port, 0x00);
				if (!XGINew_BridgeIsEnable(HwDeviceExtension, pVBInfo)) {
					SenseModeNo = 0x2e;
					/* XGINew_SetReg1(pVBInfo->P3d4, 0x30, 0x41); */
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
					XGINew_SetRegANDOR(pVBInfo->P3c4, 0x01, 0xDF, 0x20); /* Display Off 0212 */
					for (i = 0; i < 20; i++)
						XGI_LongWait(pVBInfo);
				}
				XGINew_SetReg1(pVBInfo->Part2Port, 0x00, 0x1c);
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
					XGINew_SetRegOR(pVBInfo->Part4Port, 0x0d, 0x04);

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

			XGINew_SetRegANDOR(pVBInfo->P3d4, 0x32, ~0xDF, tempax);
			XGINew_SetReg1(pVBInfo->Part2Port, 0x00, P2reg0);

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

	if ((HwDeviceExtension->jChipType >= XG20) || (HwDeviceExtension->jChipType >= XG40))
		temp = 0;
	else
		temp = XGINew_GetPanelID(pVBInfo);

	if (!temp)
		temp = XGINew_GetLCDDDCInfo(HwDeviceExtension, pVBInfo);

	return temp;
}

unsigned char XGINew_GetLCDDDCInfo(struct xgi_hw_device_info *HwDeviceExtension, struct vb_device_info *pVBInfo)
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
		XGINew_SetRegANDOR(pVBInfo->P3d4, 0x36, 0xF0, temp);
		return 1;
	}
}

unsigned char XGINew_GetPanelID(struct vb_device_info *pVBInfo)
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

	tempax = XGINew_GetReg1(pVBInfo->P3c4, 0x1A);
	tempbx = tempax & 0x1E;

	if (tempax == 0)
		return 0;
	else {
		/*
		if (!(tempax & 0x10)) {
			if (pVBInfo->IF_DEF_LVDS == 1) {
				tempbx = 0;
				temp = XGINew_GetReg1(pVBInfo->P3c4, 0x38);
				if (temp & 0x40)
					tempbx |= 0x08;
				if (temp & 0x20)
					tempbx |= 0x02;
				if (temp & 0x01)
					tempbx |= 0x01;

				temp = XGINew_GetReg1(pVBInfo->P3c4, 0x39);
				if (temp & 0x80)
					tempbx |= 0x04;
			 } else {
				return(0);
			 }
		}
		*/

		tempbx = tempbx >> 1;
		temp = tempbx & 0x00F;
		XGINew_SetReg1(pVBInfo->P3d4, 0x36, temp);
		tempbx--;
		tempbx = PanelTypeTable[tempbx];

		temp = (tempbx & 0xFF00) >> 8;
		XGINew_SetRegANDOR(pVBInfo->P3d4, 0x37, ~(LCDSyncBit
				| LCDRGB18Bit), temp);
		return 1;
	}
}

unsigned char XGINew_BridgeIsEnable(struct xgi_hw_device_info *HwDeviceExtension, struct vb_device_info *pVBInfo)
{
	unsigned short flag;

	if (XGI_BridgeIsOn(pVBInfo) == 0) {
		flag = XGINew_GetReg1(pVBInfo->Part1Port, 0x0);

		if (flag & 0x050)
			return 1;
		else
			return 0;

	}
	return 0;
}

unsigned char XGINew_SenseHiTV(struct xgi_hw_device_info *HwDeviceExtension, struct vb_device_info *pVBInfo)
{
	unsigned short tempbx, tempcx, temp, i, tempch;

	tempbx = *pVBInfo->pYCSenseData2;

	tempcx = 0x0604;

	temp = tempbx & 0xFF;
	XGINew_SetReg1(pVBInfo->Part4Port, 0x11, temp);
	temp = (tempbx & 0xFF00) >> 8;
	temp |= (tempcx & 0x00FF);
	XGINew_SetRegANDOR(pVBInfo->Part4Port, 0x10, ~0x1F, temp);

	for (i = 0; i < 10; i++)
		XGI_LongWait(pVBInfo);

	tempch = (tempcx & 0xFF00) >> 8;
	temp = XGINew_GetReg1(pVBInfo->Part4Port, 0x03);
	temp = temp ^ (0x0E);
	temp &= tempch;

	if (temp != tempch)
		return 0;

	tempbx = *pVBInfo->pVideoSenseData2;

	tempcx = 0x0804;
	temp = tempbx & 0xFF;
	XGINew_SetReg1(pVBInfo->Part4Port, 0x11, temp);
	temp = (tempbx & 0xFF00) >> 8;
	temp |= (tempcx & 0x00FF);
	XGINew_SetRegANDOR(pVBInfo->Part4Port, 0x10, ~0x1F, temp);

	for (i = 0; i < 10; i++)
		XGI_LongWait(pVBInfo);

	tempch = (tempcx & 0xFF00) >> 8;
	temp = XGINew_GetReg1(pVBInfo->Part4Port, 0x03);
	temp = temp ^ (0x0E);
	temp &= tempch;

	if (temp != tempch) {
		return 0;
	} else {
		tempbx = 0x3FF;
		tempcx = 0x0804;
		temp = tempbx & 0xFF;
		XGINew_SetReg1(pVBInfo->Part4Port, 0x11, temp);
		temp = (tempbx & 0xFF00) >> 8;
		temp |= (tempcx & 0x00FF);
		XGINew_SetRegANDOR(pVBInfo->Part4Port, 0x10, ~0x1F, temp);

		for (i = 0; i < 10; i++)
			XGI_LongWait(pVBInfo);

		tempch = (tempcx & 0xFF00) >> 8;
		temp = XGINew_GetReg1(pVBInfo->Part4Port, 0x03);
		temp = temp ^ (0x0E);
		temp &= tempch;

		if (temp != tempch)
			return 1;
		else
			return 0;
	}
}

/* ----------------------------------------------------------------------------
 *       Description: Get Panel support
 *	O/P	   :
 *            BL: Panel ID=81h for no scaler LVDS
 *		     BH: Panel enhanced Mode Count
 *		     CX: Panel H. resolution
 *		     DX: PAnel V. resolution
 * ----------------------------------------------------------------------------
 */
static void XGI_XG21Fun14Sub70(struct vb_device_info *pVBInfo, PX86_REGS pBiosArguments)
{
	unsigned short ModeIdIndex;
	unsigned short ModeNo;

	unsigned short EModeCount;
	unsigned short lvdstableindex;

	lvdstableindex = XGI_GetLVDSOEMTableIndex(pVBInfo);
	pBiosArguments->h.bl = 0x81;
	pBiosArguments->x.cx = pVBInfo->XG21_LVDSCapList[lvdstableindex].LVDSHDE;
	pBiosArguments->x.dx = pVBInfo->XG21_LVDSCapList[lvdstableindex].LVDSVDE;
	EModeCount = 0;

	pBiosArguments->x.ax = 0x0014;
	for (ModeIdIndex = 0;; ModeIdIndex++) {
		ModeNo = pVBInfo->EModeIDTable[ModeIdIndex].Ext_ModeID;
		if (pVBInfo->EModeIDTable[ModeIdIndex].Ext_ModeID == 0xFF) {
			pBiosArguments->h.bh = (unsigned char) EModeCount;
			return;
		}
		if (!XGI_XG21CheckLVDSMode(ModeNo, ModeIdIndex, pVBInfo))
			continue;

		EModeCount++;
	}
}

/* ----------------------------------------------------------------------------
 *
 *       Description: Get Panel mode ID for enhanced mode
 *	I/P	   : BH: EModeIndex ( which < Panel enhanced Mode Count )
 *	O/P	   :
 *            BL: Mode ID
 *		     CX: H. resolution of the assigned by the index
 *		     DX: V. resolution of the assigned by the index
 *
 * ----------------------------------------------------------------------------
 */

static void XGI_XG21Fun14Sub71(struct vb_device_info *pVBInfo, PX86_REGS pBiosArguments)
{

	unsigned short EModeCount;
	unsigned short ModeIdIndex, resindex;
	unsigned short ModeNo;
	unsigned short EModeIndex = pBiosArguments->h.bh;

	EModeCount = 0;
	for (ModeIdIndex = 0;; ModeIdIndex++) {
		ModeNo = pVBInfo->EModeIDTable[ModeIdIndex].Ext_ModeID;
		if (pVBInfo->EModeIDTable[ModeIdIndex].Ext_ModeID == 0xFF) {
			pBiosArguments->x.ax = 0x0114;
			return;
		}
		if (!XGI_XG21CheckLVDSMode(ModeNo, ModeIdIndex, pVBInfo))
			continue;

		if (EModeCount == EModeIndex) {
			resindex = XGI_GetResInfo(ModeNo, ModeIdIndex, pVBInfo);
			pBiosArguments->h.bl = (unsigned char) ModeNo;
			pBiosArguments->x.cx = pVBInfo->ModeResInfo[resindex].HTotal; /* xres->ax */
			pBiosArguments->x.dx = pVBInfo->ModeResInfo[resindex].VTotal; /* yres->bx */
			pBiosArguments->x.ax = 0x0014;
		}
		EModeCount++;

	}

}

/* ----------------------------------------------------------------------------
 *
 *       Description: Validate Panel modes ID support
 *	I/P	   :
 *            BL: ModeID
 *	O/P	   :
 *		     CX: H. resolution of the assigned by the index
 *		     DX: V. resolution of the assigned by the index
 *
 * ----------------------------------------------------------------------------
 */
static void XGI_XG21Fun14Sub72(struct vb_device_info *pVBInfo, PX86_REGS pBiosArguments)
{
	unsigned short ModeIdIndex, resindex;
	unsigned short ModeNo;

	ModeNo = pBiosArguments->h.bl;
	XGI_SearchModeID(ModeNo, &ModeIdIndex, pVBInfo);
	if (!XGI_XG21CheckLVDSMode(ModeNo, ModeIdIndex, pVBInfo)) {
		pBiosArguments->x.cx = 0;
		pBiosArguments->x.dx = 0;
		pBiosArguments->x.ax = 0x0114;
		return;
	}
	resindex = XGI_GetResInfo(ModeNo, ModeIdIndex, pVBInfo);
	if (ModeNo <= 0x13) {
		pBiosArguments->x.cx = pVBInfo->StResInfo[resindex].HTotal;
		pBiosArguments->x.dx = pVBInfo->StResInfo[resindex].VTotal;
	} else {
		pBiosArguments->x.cx = pVBInfo->ModeResInfo[resindex].HTotal; /* xres->ax */
		pBiosArguments->x.dx = pVBInfo->ModeResInfo[resindex].VTotal; /* yres->bx */
	}

	pBiosArguments->x.ax = 0x0014;

}

/* ----------------------------------------------------------------------------
 *
 *	Description: Get Customized Panel misc. information support
 *	I/P	   : Select
 *		     to get panel horizontal timing
 *			 to get panel vertical timing
 *			 to get channel clock parameter
 *            to get panel misc information
 *
 *	O/P	   :
 *		     BL: for input Select = 0 ;
 *                       BX: *Value1 = Horizontal total
 *                       CX: *Value2 = Horizontal front porch
 *                       DX: *Value2 = Horizontal sync width
 *		     BL: for input Select = 1 ;
 *                       BX: *Value1 = Vertical total
 *                       CX: *Value2 = Vertical front porch
 *                       DX: *Value2 = Vertical sync width
 *            BL: for input Select = 2 ;
 *                       BX: Value1 = The first CLK parameter
 *                       CX: Value2 = The second CLK parameter
 *		     BL: for input Select = 4 ;
 *                       BX[15]: *Value1 D[15] VESA V. Polarity
 *                       BX[14]: *Value1 D[14] VESA H. Polarity
 *                       BX[7]: *Value1 D[7] Panel V. Polarity
 *                       BX[6]: *Value1 D[6] Panel H. Polarity
 * ----------------------------------------------------------------------------
 */
static void XGI_XG21Fun14Sub73(struct vb_device_info *pVBInfo, PX86_REGS pBiosArguments)
{
	unsigned char Select;

	unsigned short lvdstableindex;

	lvdstableindex = XGI_GetLVDSOEMTableIndex(pVBInfo);
	Select = pBiosArguments->h.bl;

	switch (Select) {
	case 0:
		pBiosArguments->x.bx = pVBInfo->XG21_LVDSCapList[lvdstableindex].LVDSHT;
		pBiosArguments->x.cx = pVBInfo->XG21_LVDSCapList[lvdstableindex].LVDSHFP;
		pBiosArguments->x.dx = pVBInfo->XG21_LVDSCapList[lvdstableindex].LVDSHSYNC;
		break;
	case 1:
		pBiosArguments->x.bx = pVBInfo->XG21_LVDSCapList[lvdstableindex].LVDSVT;
		pBiosArguments->x.cx = pVBInfo->XG21_LVDSCapList[lvdstableindex].LVDSVFP;
		pBiosArguments->x.dx = pVBInfo->XG21_LVDSCapList[lvdstableindex].LVDSVSYNC;
		break;
	case 2:
		pBiosArguments->x.bx = pVBInfo->XG21_LVDSCapList[lvdstableindex].VCLKData1;
		pBiosArguments->x.cx = pVBInfo->XG21_LVDSCapList[lvdstableindex].VCLKData2;
		break;
	case 4:
		pBiosArguments->x.bx = pVBInfo->XG21_LVDSCapList[lvdstableindex].LVDS_Capability;
		break;
	}

	pBiosArguments->x.ax = 0x0014;
}

void XGI_XG21Fun14(struct xgi_hw_device_info *pXGIHWDE, PX86_REGS pBiosArguments)
{
	struct vb_device_info VBINF;
	struct vb_device_info *pVBInfo = &VBINF;

	pVBInfo->IF_DEF_LVDS = 0;
	pVBInfo->IF_DEF_CH7005 = 0;
	pVBInfo->IF_DEF_HiVision = 1;
	pVBInfo->IF_DEF_LCDA = 1;
	pVBInfo->IF_DEF_CH7017 = 0;
	pVBInfo->IF_DEF_YPbPr = 1;
	pVBInfo->IF_DEF_CRT2Monitor = 0;
	pVBInfo->IF_DEF_VideoCapture = 0;
	pVBInfo->IF_DEF_ScaleLCD = 0;
	pVBInfo->IF_DEF_OEMUtil = 0;
	pVBInfo->IF_DEF_PWD = 0;

	InitTo330Pointer(pXGIHWDE->jChipType, pVBInfo);
	ReadVBIOSTablData(pXGIHWDE->jChipType, pVBInfo);

	pVBInfo->P3c4 = pVBInfo->BaseAddr + 0x14;
	pVBInfo->P3d4 = pVBInfo->BaseAddr + 0x24;
	pVBInfo->P3c0 = pVBInfo->BaseAddr + 0x10;
	pVBInfo->P3ce = pVBInfo->BaseAddr + 0x1e;
	pVBInfo->P3c2 = pVBInfo->BaseAddr + 0x12;
	pVBInfo->P3ca = pVBInfo->BaseAddr + 0x1a;
	pVBInfo->P3c6 = pVBInfo->BaseAddr + 0x16;
	pVBInfo->P3c7 = pVBInfo->BaseAddr + 0x17;
	pVBInfo->P3c8 = pVBInfo->BaseAddr + 0x18;
	pVBInfo->P3c9 = pVBInfo->BaseAddr + 0x19;
	pVBInfo->P3da = pVBInfo->BaseAddr + 0x2A;
	pVBInfo->Part0Port = pVBInfo->BaseAddr + XGI_CRT2_PORT_00;
	pVBInfo->Part1Port = pVBInfo->BaseAddr + XGI_CRT2_PORT_04;
	pVBInfo->Part2Port = pVBInfo->BaseAddr + XGI_CRT2_PORT_10;
	pVBInfo->Part3Port = pVBInfo->BaseAddr + XGI_CRT2_PORT_12;
	pVBInfo->Part4Port = pVBInfo->BaseAddr + XGI_CRT2_PORT_14;
	pVBInfo->Part5Port = pVBInfo->BaseAddr + XGI_CRT2_PORT_14 + 2;

	switch (pBiosArguments->x.ax) {
	case 0x1470:
		XGI_XG21Fun14Sub70(pVBInfo, pBiosArguments);
		break;
	case 0x1471:
		XGI_XG21Fun14Sub71(pVBInfo, pBiosArguments);
		break;
	case 0x1472:
		XGI_XG21Fun14Sub72(pVBInfo, pBiosArguments);
		break;
	case 0x1473:
		XGI_XG21Fun14Sub73(pVBInfo, pBiosArguments);
		break;
	}
}
