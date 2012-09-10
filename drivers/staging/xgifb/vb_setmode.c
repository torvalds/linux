#include <linux/delay.h>
#include "XGIfb.h"

#include "vb_def.h"
#include "vb_util.h"
#include "vb_table.h"
#include "vb_setmode.h"

#define  IndexMask 0xff
#define TVCLKBASE_315_25 (TVCLKBASE_315 + 25)

static const unsigned short XGINew_VGA_DAC[] = {
	0x00, 0x10, 0x04, 0x14, 0x01, 0x11, 0x09, 0x15,
	0x2A, 0x3A, 0x2E, 0x3E, 0x2B, 0x3B, 0x2F, 0x3F,
	0x00, 0x05, 0x08, 0x0B, 0x0E, 0x11, 0x14, 0x18,
	0x1C, 0x20, 0x24, 0x28, 0x2D, 0x32, 0x38, 0x3F,
	0x00, 0x10, 0x1F, 0x2F, 0x3F, 0x1F, 0x27, 0x2F,
	0x37, 0x3F, 0x2D, 0x31, 0x36, 0x3A, 0x3F, 0x00,
	0x07, 0x0E, 0x15, 0x1C, 0x0E, 0x11, 0x15, 0x18,
	0x1C, 0x14, 0x16, 0x18, 0x1A, 0x1C, 0x00, 0x04,
	0x08, 0x0C, 0x10, 0x08, 0x0A, 0x0C, 0x0E, 0x10,
	0x0B, 0x0C, 0x0D, 0x0F, 0x10};

void InitTo330Pointer(unsigned char ChipType, struct vb_device_info *pVBInfo)
{
	pVBInfo->StandTable = (struct SiS_StandTable_S *) &XGI330_StandTable;
	pVBInfo->EModeIDTable = (struct XGI_ExtStruct *) XGI330_EModeIDTable;
	pVBInfo->RefIndex = (struct XGI_Ext2Struct *) XGI330_RefIndex;
	pVBInfo->XGINEWUB_CRT1Table
			= (struct XGI_CRT1TableStruct *) XGI_CRT1Table;

	pVBInfo->MCLKData = (struct SiS_MCLKData *) XGI340New_MCLKData;
	pVBInfo->ECLKData = (struct XGI_ECLKDataStruct *) XGI340_ECLKData;
	pVBInfo->VCLKData = (struct SiS_VCLKData *) XGI_VCLKData;
	pVBInfo->VBVCLKData = (struct SiS_VBVCLKData *) XGI_VBVCLKData;
	pVBInfo->ScreenOffset = XGI330_ScreenOffset;
	pVBInfo->StResInfo = (struct SiS_StResInfo_S *) XGI330_StResInfo;
	pVBInfo->ModeResInfo
			= (struct SiS_ModeResInfo_S *) XGI330_ModeResInfo;

	pVBInfo->LCDResInfo = 0;
	pVBInfo->LCDTypeInfo = 0;
	pVBInfo->LCDInfo = 0;
	pVBInfo->VBInfo = 0;
	pVBInfo->TVInfo = 0;

	pVBInfo->SR15 = XGI340_SR13;
	pVBInfo->CR40 = XGI340_cr41;
	pVBInfo->CR6B = XGI340_CR6B;
	pVBInfo->CR6E = XGI340_CR6E;
	pVBInfo->CR6F = XGI340_CR6F;
	pVBInfo->CR89 = XGI340_CR89;
	pVBInfo->AGPReg = XGI340_AGPReg;
	pVBInfo->SR16 = XGI340_SR16;

	pVBInfo->SR21 = 0xa3;
	pVBInfo->SR22 = 0xfb;

	pVBInfo->NTSCTiming = XGI330_NTSCTiming;
	pVBInfo->PALTiming = XGI330_PALTiming;
	pVBInfo->HiTVExtTiming = XGI330_HiTVExtTiming;
	pVBInfo->HiTVSt1Timing = XGI330_HiTVSt1Timing;
	pVBInfo->HiTVSt2Timing = XGI330_HiTVSt2Timing;
	pVBInfo->HiTVTextTiming = XGI330_HiTVTextTiming;
	pVBInfo->YPbPr750pTiming = XGI330_YPbPr750pTiming;
	pVBInfo->YPbPr525pTiming = XGI330_YPbPr525pTiming;
	pVBInfo->YPbPr525iTiming = XGI330_YPbPr525iTiming;
	pVBInfo->HiTVGroup3Data = XGI330_HiTVGroup3Data;
	pVBInfo->HiTVGroup3Simu = XGI330_HiTVGroup3Simu;
	pVBInfo->HiTVGroup3Text = XGI330_HiTVGroup3Text;
	pVBInfo->Ren525pGroup3 = XGI330_Ren525pGroup3;
	pVBInfo->Ren750pGroup3 = XGI330_Ren750pGroup3;

	pVBInfo->TimingH = (struct XGI_TimingHStruct *) XGI_TimingH;
	pVBInfo->TimingV = (struct XGI_TimingVStruct *) XGI_TimingV;
	pVBInfo->UpdateCRT1 = (struct XGI_XG21CRT1Struct *) XGI_UpdateCRT1Table;

	/* 310 customization related */
	if ((pVBInfo->VBType & VB_SIS301LV) || (pVBInfo->VBType & VB_SIS302LV))
		pVBInfo->LCDCapList = XGI_LCDDLCapList;
	else
		pVBInfo->LCDCapList = XGI_LCDCapList;

	if (ChipType >= XG20)
		pVBInfo->XGINew_CR97 = 0x10;

	if (ChipType == XG27) {
		unsigned char temp;
		pVBInfo->MCLKData
			= (struct SiS_MCLKData *) XGI27New_MCLKData;
		pVBInfo->CR40 = XGI27_cr41;
		pVBInfo->XGINew_CR97 = 0xc1;
		pVBInfo->SR15 = XG27_SR13;

		/*Z11m DDR*/
		temp = xgifb_reg_get(pVBInfo->P3c4, 0x3B);
		/* SR3B[7][3]MAA15 MAA11 (Power on Trapping) */
		if (((temp & 0x88) == 0x80) || ((temp & 0x88) == 0x08))
			pVBInfo->XGINew_CR97 = 0x80;
	}

}

static void XGI_SetSeqRegs(unsigned short ModeNo,
			   unsigned short ModeIdIndex,
			   struct vb_device_info *pVBInfo)
{
	unsigned char tempah, SRdata;
	unsigned short i, modeflag;

	modeflag = pVBInfo->EModeIDTable[ModeIdIndex].Ext_ModeFlag;

	xgifb_reg_set(pVBInfo->P3c4, 0x00, 0x03); /* Set SR0 */
	tempah = pVBInfo->StandTable->SR[0];

	i = XGI_SetCRT2ToLCDA;
	if (pVBInfo->VBInfo & XGI_SetCRT2ToLCDA) {
		tempah |= 0x01;
	} else {
		if (pVBInfo->VBInfo & (SetCRT2ToTV | SetCRT2ToLCD)) {
			if (pVBInfo->VBInfo & SetInSlaveMode)
				tempah |= 0x01;
		}
	}

	tempah |= 0x20; /* screen off */
	xgifb_reg_set(pVBInfo->P3c4, 0x01, tempah); /* Set SR1 */

	for (i = 02; i <= 04; i++) {
		/* Get SR2,3,4 from file */
		SRdata = pVBInfo->StandTable->SR[i - 1];
		xgifb_reg_set(pVBInfo->P3c4, i, SRdata); /* Set SR2 3 4 */
	}
}

static void XGI_SetCRTCRegs(struct xgi_hw_device_info *HwDeviceExtension,
			    struct vb_device_info *pVBInfo)
{
	unsigned char CRTCdata;
	unsigned short i;

	CRTCdata = (unsigned char) xgifb_reg_get(pVBInfo->P3d4, 0x11);
	CRTCdata &= 0x7f;
	xgifb_reg_set(pVBInfo->P3d4, 0x11, CRTCdata); /* Unlock CRTC */

	for (i = 0; i <= 0x18; i++) {
		/* Get CRTC from file */
		CRTCdata = pVBInfo->StandTable->CRTC[i];
		xgifb_reg_set(pVBInfo->P3d4, i, CRTCdata); /* Set CRTC(3d4) */
	}
}

static void XGI_SetATTRegs(unsigned short ModeNo,
			   unsigned short ModeIdIndex,
			   struct vb_device_info *pVBInfo)
{
	unsigned char ARdata;
	unsigned short i, modeflag;

	modeflag = pVBInfo->EModeIDTable[ModeIdIndex].Ext_ModeFlag;

	for (i = 0; i <= 0x13; i++) {
		ARdata = pVBInfo->StandTable->ATTR[i];

		if ((modeflag & Charx8Dot) && i == 0x13) { /* ifndef Dot9 */
			if (pVBInfo->VBInfo & XGI_SetCRT2ToLCDA) {
				ARdata = 0;
			} else {
				if ((pVBInfo->VBInfo &
				     (SetCRT2ToTV | SetCRT2ToLCD)) &&
				    (pVBInfo->VBInfo & SetInSlaveMode))
					ARdata = 0;
			}
		}

		inb(pVBInfo->P3da); /* reset 3da */
		outb(i, pVBInfo->P3c0); /* set index */
		outb(ARdata, pVBInfo->P3c0); /* set data */
	}

	inb(pVBInfo->P3da); /* reset 3da */
	outb(0x14, pVBInfo->P3c0); /* set index */
	outb(0x00, pVBInfo->P3c0); /* set data */
	inb(pVBInfo->P3da); /* Enable Attribute */
	outb(0x20, pVBInfo->P3c0);
}

static void XGI_SetGRCRegs(struct vb_device_info *pVBInfo)
{
	unsigned char GRdata;
	unsigned short i;

	for (i = 0; i <= 0x08; i++) {
		/* Get GR from file */
		GRdata = pVBInfo->StandTable->GRC[i];
		xgifb_reg_set(pVBInfo->P3ce, i, GRdata); /* Set GR(3ce) */
	}

	if (pVBInfo->ModeType > ModeVGA) {
		GRdata = (unsigned char) xgifb_reg_get(pVBInfo->P3ce, 0x05);
		GRdata &= 0xBF; /* 256 color disable */
		xgifb_reg_set(pVBInfo->P3ce, 0x05, GRdata);
	}
}

static void XGI_ClearExt1Regs(struct vb_device_info *pVBInfo)
{
	unsigned short i;

	for (i = 0x0A; i <= 0x0E; i++)
		xgifb_reg_set(pVBInfo->P3c4, i, 0x00); /* Clear SR0A-SR0E */
}

static unsigned char XGI_SetDefaultVCLK(struct vb_device_info *pVBInfo)
{

	xgifb_reg_and_or(pVBInfo->P3c4, 0x31, ~0x30, 0x20);
	xgifb_reg_set(pVBInfo->P3c4, 0x2B, pVBInfo->VCLKData[0].SR2B);
	xgifb_reg_set(pVBInfo->P3c4, 0x2C, pVBInfo->VCLKData[0].SR2C);

	xgifb_reg_and_or(pVBInfo->P3c4, 0x31, ~0x30, 0x10);
	xgifb_reg_set(pVBInfo->P3c4, 0x2B, pVBInfo->VCLKData[1].SR2B);
	xgifb_reg_set(pVBInfo->P3c4, 0x2C, pVBInfo->VCLKData[1].SR2C);

	xgifb_reg_and(pVBInfo->P3c4, 0x31, ~0x30);
	return 0;
}

static unsigned char XGI_AjustCRT2Rate(unsigned short ModeNo,
		unsigned short ModeIdIndex,
		unsigned short RefreshRateTableIndex, unsigned short *i,
		struct vb_device_info *pVBInfo)
{
	unsigned short tempax, tempbx, resinfo, modeflag, infoflag;

	modeflag = pVBInfo->EModeIDTable[ModeIdIndex].Ext_ModeFlag;
	resinfo = pVBInfo->EModeIDTable[ModeIdIndex].Ext_RESINFO;
	tempbx = pVBInfo->RefIndex[RefreshRateTableIndex + (*i)].ModeID;
	tempax = 0;

	if (pVBInfo->IF_DEF_LVDS == 0) {
		if (pVBInfo->VBInfo & SetCRT2ToRAMDAC) {
			tempax |= SupportRAMDAC2;

			if (pVBInfo->VBType & VB_XGI301C)
				tempax |= SupportCRT2in301C;
		}

		/* 301b */
		if (pVBInfo->VBInfo & (SetCRT2ToLCD | XGI_SetCRT2ToLCDA)) {
			tempax |= SupportLCD;

			if (pVBInfo->LCDResInfo != Panel_1280x1024 &&
			    pVBInfo->LCDResInfo != Panel_1280x960 &&
			    (pVBInfo->LCDInfo & LCDNonExpanding) &&
			    resinfo >= 9)
				return 0;
		}

		if (pVBInfo->VBInfo & SetCRT2ToHiVision) { /* for HiTV */
			tempax |= SupportHiVision;
			if ((pVBInfo->VBInfo & SetInSlaveMode) &&
			    ((resinfo == 4) ||
			     (resinfo == 3 &&
			      (pVBInfo->SetFlag & TVSimuMode)) ||
			     (resinfo > 7)))
					return 0;
		} else {
			if (pVBInfo->VBInfo & (SetCRT2ToAVIDEO |
					       SetCRT2ToSVIDEO |
					       SetCRT2ToSCART |
					       SetCRT2ToYPbPr525750 |
					       SetCRT2ToHiVision)) {
				tempax |= SupportTV;

				if (pVBInfo->VBType & (VB_SIS301B |
						       VB_SIS302B |
						       VB_SIS301LV |
						       VB_SIS302LV |
						       VB_XGI301C))
					tempax |= SupportTV1024;

				if (!(pVBInfo->VBInfo & TVSetPAL) &&
				    (modeflag & NoSupportSimuTV) &&
				    (pVBInfo->VBInfo & SetInSlaveMode) &&
				    (!(pVBInfo->VBInfo & SetNotSimuMode)))
					return 0;
			}
		}
	} else { /* for LVDS */
		if (pVBInfo->VBInfo & SetCRT2ToLCD) {
			tempax |= SupportLCD;

			if (resinfo > 0x08)
				return 0; /* 1024x768 */

			if (pVBInfo->LCDResInfo < Panel_1024x768) {
				if (resinfo > 0x07)
					return 0; /* 800x600 */

				if (resinfo == 0x04)
					return 0; /* 512x384 */
			}
		}
	}

	for (; pVBInfo->RefIndex[RefreshRateTableIndex + (*i)].ModeID ==
	       tempbx; (*i)--) {
		infoflag = pVBInfo->RefIndex[RefreshRateTableIndex + (*i)].
				Ext_InfoFlag;
		if (infoflag & tempax)
			return 1;

		if ((*i) == 0)
			break;
	}

	for ((*i) = 0;; (*i)++) {
		infoflag = pVBInfo->RefIndex[RefreshRateTableIndex + (*i)].
				Ext_InfoFlag;
		if (pVBInfo->RefIndex[RefreshRateTableIndex + (*i)].ModeID
				!= tempbx) {
			return 0;
		}

		if (infoflag & tempax)
			return 1;
	}
	return 1;
}

static void XGI_SetSync(unsigned short RefreshRateTableIndex,
		struct vb_device_info *pVBInfo)
{
	unsigned short sync, temp;

	/* di+0x00 */
	sync = pVBInfo->RefIndex[RefreshRateTableIndex].Ext_InfoFlag >> 8;
	sync &= 0xC0;
	temp = 0x2F;
	temp |= sync;
	outb(temp, pVBInfo->P3c2); /* Set Misc(3c2) */
}

static void XGI_SetCRT1Timing_H(struct vb_device_info *pVBInfo,
		struct xgi_hw_device_info *HwDeviceExtension)
{
	unsigned char data, data1, pushax;
	unsigned short i, j;

	/* unlock cr0-7 */
	data = (unsigned char) xgifb_reg_get(pVBInfo->P3d4, 0x11);
	data &= 0x7F;
	xgifb_reg_set(pVBInfo->P3d4, 0x11, data);

	data = pVBInfo->TimingH[0].data[0];
	xgifb_reg_set(pVBInfo->P3d4, 0, data);

	for (i = 0x01; i <= 0x04; i++) {
		data = pVBInfo->TimingH[0].data[i];
		xgifb_reg_set(pVBInfo->P3d4, (unsigned short) (i + 1), data);
	}

	for (i = 0x05; i <= 0x06; i++) {
		data = pVBInfo->TimingH[0].data[i];
		xgifb_reg_set(pVBInfo->P3c4, (unsigned short) (i + 6), data);
	}

	j = (unsigned char) xgifb_reg_get(pVBInfo->P3c4, 0x0e);
	j &= 0x1F;
	data = pVBInfo->TimingH[0].data[7];
	data &= 0xE0;
	data |= j;
	xgifb_reg_set(pVBInfo->P3c4, 0x0e, data);

	if (HwDeviceExtension->jChipType >= XG20) {
		data = (unsigned char) xgifb_reg_get(pVBInfo->P3d4, 0x04);
		data = data - 1;
		xgifb_reg_set(pVBInfo->P3d4, 0x04, data);
		data = (unsigned char) xgifb_reg_get(pVBInfo->P3d4, 0x05);
		data1 = data;
		data1 &= 0xE0;
		data &= 0x1F;
		if (data == 0) {
			pushax = data;
			data = (unsigned char) xgifb_reg_get(pVBInfo->P3c4,
					0x0c);
			data &= 0xFB;
			xgifb_reg_set(pVBInfo->P3c4, 0x0c, data);
			data = pushax;
		}
		data = data - 1;
		data |= data1;
		xgifb_reg_set(pVBInfo->P3d4, 0x05, data);
		data = (unsigned char) xgifb_reg_get(pVBInfo->P3c4, 0x0e);
		data = data >> 5;
		data = data + 3;
		if (data > 7)
			data = data - 7;
		data = data << 5;
		xgifb_reg_and_or(pVBInfo->P3c4, 0x0e, ~0xE0, data);
	}
}

static void XGI_SetCRT1Timing_V(unsigned short ModeIdIndex,
				unsigned short ModeNo,
				struct vb_device_info *pVBInfo)
{
	unsigned char data;
	unsigned short i, j;

	for (i = 0x00; i <= 0x01; i++) {
		data = pVBInfo->TimingV[0].data[i];
		xgifb_reg_set(pVBInfo->P3d4, (unsigned short) (i + 6), data);
	}

	for (i = 0x02; i <= 0x03; i++) {
		data = pVBInfo->TimingV[0].data[i];
		xgifb_reg_set(pVBInfo->P3d4, (unsigned short) (i + 0x0e), data);
	}

	for (i = 0x04; i <= 0x05; i++) {
		data = pVBInfo->TimingV[0].data[i];
		xgifb_reg_set(pVBInfo->P3d4, (unsigned short) (i + 0x11), data);
	}

	j = (unsigned char) xgifb_reg_get(pVBInfo->P3c4, 0x0a);
	j &= 0xC0;
	data = pVBInfo->TimingV[0].data[6];
	data &= 0x3F;
	data |= j;
	xgifb_reg_set(pVBInfo->P3c4, 0x0a, data);

	data = pVBInfo->TimingV[0].data[6];
	data &= 0x80;
	data = data >> 2;

	i = pVBInfo->EModeIDTable[ModeIdIndex].Ext_ModeFlag;
	i &= DoubleScanMode;
	if (i)
		data |= 0x80;

	j = (unsigned char) xgifb_reg_get(pVBInfo->P3d4, 0x09);
	j &= 0x5F;
	data |= j;
	xgifb_reg_set(pVBInfo->P3d4, 0x09, data);
}

static void XGI_SetCRT1CRTC(unsigned short ModeNo, unsigned short ModeIdIndex,
		unsigned short RefreshRateTableIndex,
		struct vb_device_info *pVBInfo,
		struct xgi_hw_device_info *HwDeviceExtension)
{
	unsigned char index, data;
	unsigned short i;

	/* Get index */
	index = pVBInfo->RefIndex[RefreshRateTableIndex].Ext_CRT1CRTC;
	index = index & IndexMask;

	data = (unsigned char) xgifb_reg_get(pVBInfo->P3d4, 0x11);
	data &= 0x7F;
	xgifb_reg_set(pVBInfo->P3d4, 0x11, data); /* Unlock CRTC */

	for (i = 0; i < 8; i++)
		pVBInfo->TimingH[0].data[i]
				= pVBInfo->XGINEWUB_CRT1Table[index].CR[i];

	for (i = 0; i < 7; i++)
		pVBInfo->TimingV[0].data[i]
				= pVBInfo->XGINEWUB_CRT1Table[index].CR[i + 8];

	XGI_SetCRT1Timing_H(pVBInfo, HwDeviceExtension);

	XGI_SetCRT1Timing_V(ModeIdIndex, ModeNo, pVBInfo);

	if (pVBInfo->ModeType > 0x03)
		xgifb_reg_set(pVBInfo->P3d4, 0x14, 0x4F);
}

/* --------------------------------------------------------------------- */
/* Function : XGI_SetXG21CRTC */
/* Input : Stand or enhance CRTC table */
/* Output : Fill CRT Hsync/Vsync to SR2E/SR2F/SR30/SR33/SR34/SR3F */
/* Description : Set LCD timing */
/* --------------------------------------------------------------------- */
static void XGI_SetXG21CRTC(unsigned short ModeNo, unsigned short ModeIdIndex,
		unsigned short RefreshRateTableIndex,
		struct vb_device_info *pVBInfo)
{
	unsigned char index, Tempax, Tempbx, Tempcx, Tempdx;
	unsigned short Temp1, Temp2, Temp3;

	index = pVBInfo->RefIndex[RefreshRateTableIndex].Ext_CRT1CRTC;
	/* Tempax: CR4 HRS */
	Tempax = pVBInfo->XGINEWUB_CRT1Table[index].CR[3];
	Tempcx = Tempax; /* Tempcx: HRS */
	/* SR2E[7:0]->HRS */
	xgifb_reg_set(pVBInfo->P3c4, 0x2E, Tempax);

	Tempdx = pVBInfo->XGINEWUB_CRT1Table[index].CR[5]; /* SRB */
	Tempdx &= 0xC0; /* Tempdx[7:6]: SRB[7:6] */
	Temp1 = Tempdx; /* Temp1[7:6]: HRS[9:8] */
	Temp1 <<= 2; /* Temp1[9:8]: HRS[9:8] */
	Temp1 |= Tempax; /* Temp1[9:0]: HRS[9:0] */

	Tempax = pVBInfo->XGINEWUB_CRT1Table[index].CR[4]; /* CR5 HRE */
	Tempax &= 0x1F; /* Tempax[4:0]: HRE[4:0] */

	Tempbx = pVBInfo->XGINEWUB_CRT1Table[index].CR[6]; /* SRC */
	Tempbx &= 0x04; /* Tempbx[2]: HRE[5] */
	Tempbx <<= 3; /* Tempbx[5]: HRE[5] */
	Tempax |= Tempbx; /* Tempax[5:0]: HRE[5:0] */

	Temp2 = Temp1 & 0x3C0; /* Temp2[9:6]: HRS[9:6] */
	Temp2 |= Tempax; /* Temp2[9:0]: HRE[9:0] */

	Tempcx &= 0x3F; /* Tempcx[5:0]: HRS[5:0] */
	if (Tempax < Tempcx) /* HRE < HRS */
		Temp2 |= 0x40; /* Temp2 + 0x40 */

	Temp2 &= 0xFF;
	Tempax = (unsigned char) Temp2; /* Tempax: HRE[7:0] */
	Tempax <<= 2; /* Tempax[7:2]: HRE[5:0] */
	Tempdx >>= 6; /* Tempdx[7:6]->[1:0] HRS[9:8] */
	Tempax |= Tempdx; /* HRE[5:0]HRS[9:8] */
	/* SR2F D[7:2]->HRE, D[1:0]->HRS */
	xgifb_reg_set(pVBInfo->P3c4, 0x2F, Tempax);
	xgifb_reg_and_or(pVBInfo->P3c4, 0x30, 0xE3, 00);

	/* CR10 VRS */
	Tempax = pVBInfo->XGINEWUB_CRT1Table[index].CR[10];
	Tempbx = Tempax; /* Tempbx: VRS */
	Tempax &= 0x01; /* Tempax[0]: VRS[0] */
	xgifb_reg_or(pVBInfo->P3c4, 0x33, Tempax); /* SR33[0]->VRS[0] */
	/* CR7[2][7] VRE */
	Tempax = pVBInfo->XGINEWUB_CRT1Table[index].CR[9];
	Tempcx = Tempbx >> 1; /* Tempcx[6:0]: VRS[7:1] */
	Tempdx = Tempax & 0x04; /* Tempdx[2]: CR7[2] */
	Tempdx <<= 5; /* Tempdx[7]: VRS[8] */
	Tempcx |= Tempdx; /* Tempcx[7:0]: VRS[8:1] */
	xgifb_reg_set(pVBInfo->P3c4, 0x34, Tempcx); /* SR34[8:1]->VRS */

	Temp1 = Tempdx; /* Temp1[7]: Tempdx[7] */
	Temp1 <<= 1; /* Temp1[8]: VRS[8] */
	Temp1 |= Tempbx; /* Temp1[8:0]: VRS[8:0] */
	Tempax &= 0x80;
	Temp2 = Tempax << 2; /* Temp2[9]: VRS[9] */
	Temp1 |= Temp2; /* Temp1[9:0]: VRS[9:0] */
	/* Tempax: SRA */
	Tempax = pVBInfo->XGINEWUB_CRT1Table[index].CR[14];
	Tempax &= 0x08; /* Tempax[3]: VRS[3] */
	Temp2 = Tempax;
	Temp2 <<= 7; /* Temp2[10]: VRS[10] */
	Temp1 |= Temp2; /* Temp1[10:0]: VRS[10:0] */

	/* Tempax: CR11 VRE */
	Tempax = pVBInfo->XGINEWUB_CRT1Table[index].CR[11];
	Tempax &= 0x0F; /* Tempax[3:0]: VRE[3:0] */
	/* Tempbx: SRA */
	Tempbx = pVBInfo->XGINEWUB_CRT1Table[index].CR[14];
	Tempbx &= 0x20; /* Tempbx[5]: VRE[5] */
	Tempbx >>= 1; /* Tempbx[4]: VRE[4] */
	Tempax |= Tempbx; /* Tempax[4:0]: VRE[4:0] */
	Temp2 = Temp1 & 0x7E0; /* Temp2[10:5]: VRS[10:5] */
	Temp2 |= Tempax; /* Temp2[10:5]: VRE[10:5] */

	Temp3 = Temp1 & 0x1F; /* Temp3[4:0]: VRS[4:0] */
	if (Tempax < Temp3) /* VRE < VRS */
		Temp2 |= 0x20; /* VRE + 0x20 */

	Temp2 &= 0xFF;
	Tempax = (unsigned char) Temp2; /* Tempax: VRE[7:0] */
	Tempax <<= 2; /* Tempax[7:0]; VRE[5:0]00 */
	Temp1 &= 0x600; /* Temp1[10:9]: VRS[10:9] */
	Temp1 >>= 9; /* Temp1[1:0]: VRS[10:9] */
	Tempbx = (unsigned char) Temp1;
	Tempax |= Tempbx; /* Tempax[7:0]: VRE[5:0]VRS[10:9] */
	Tempax &= 0x7F;
	/* SR3F D[7:2]->VRE D[1:0]->VRS */
	xgifb_reg_set(pVBInfo->P3c4, 0x3F, Tempax);
}

static void XGI_SetXG27CRTC(unsigned short ModeNo,
			    unsigned short ModeIdIndex,
			    unsigned short RefreshRateTableIndex,
			    struct vb_device_info *pVBInfo)
{
	unsigned short index, Tempax, Tempbx, Tempcx;

	index = pVBInfo->RefIndex[RefreshRateTableIndex].Ext_CRT1CRTC;
	/* Tempax: CR4 HRS */
	Tempax = pVBInfo->XGINEWUB_CRT1Table[index].CR[3];
	Tempbx = Tempax; /* Tempbx: HRS[7:0] */
	/* SR2E[7:0]->HRS */
	xgifb_reg_set(pVBInfo->P3c4, 0x2E, Tempax);

	/* SR0B */
	Tempax = pVBInfo->XGINEWUB_CRT1Table[index].CR[5];
	Tempax &= 0xC0; /* Tempax[7:6]: SR0B[7:6]: HRS[9:8]*/
	Tempbx |= (Tempax << 2); /* Tempbx: HRS[9:0] */

	Tempax = pVBInfo->XGINEWUB_CRT1Table[index].CR[4]; /* CR5 HRE */
	Tempax &= 0x1F; /* Tempax[4:0]: HRE[4:0] */
	Tempcx = Tempax; /* Tempcx: HRE[4:0] */

	Tempax = pVBInfo->XGINEWUB_CRT1Table[index].CR[6]; /* SRC */
	Tempax &= 0x04; /* Tempax[2]: HRE[5] */
	Tempax <<= 3; /* Tempax[5]: HRE[5] */
	Tempcx |= Tempax; /* Tempcx[5:0]: HRE[5:0] */

	Tempbx = Tempbx & 0x3C0; /* Tempbx[9:6]: HRS[9:6] */
	Tempbx |= Tempcx; /* Tempbx: HRS[9:6]HRE[5:0] */

	/* Tempax: CR4 HRS */
	Tempax = pVBInfo->XGINEWUB_CRT1Table[index].CR[3];
	Tempax &= 0x3F; /* Tempax: HRS[5:0] */
	if (Tempcx <= Tempax) /* HRE[5:0] < HRS[5:0] */
		Tempbx += 0x40; /* Tempbx= Tempbx + 0x40 : HRE[9:0]*/

	Tempax = pVBInfo->XGINEWUB_CRT1Table[index].CR[5]; /* SR0B */
	Tempax &= 0xC0; /* Tempax[7:6]: SR0B[7:6]: HRS[9:8]*/
	Tempax >>= 6; /* Tempax[1:0]: HRS[9:8]*/
	Tempax |= ((Tempbx << 2) & 0xFF); /* Tempax[7:2]: HRE[5:0] */
	/* SR2F [7:2][1:0]: HRE[5:0]HRS[9:8] */
	xgifb_reg_set(pVBInfo->P3c4, 0x2F, Tempax);
	xgifb_reg_and_or(pVBInfo->P3c4, 0x30, 0xE3, 00);

	/* CR10 VRS */
	Tempax = pVBInfo->XGINEWUB_CRT1Table[index].CR[10];
	/* SR34[7:0]->VRS[7:0] */
	xgifb_reg_set(pVBInfo->P3c4, 0x34, Tempax);

	Tempcx = Tempax; /* Tempcx <= VRS[7:0] */
	/* CR7[7][2] VRS[9][8] */
	Tempax = pVBInfo->XGINEWUB_CRT1Table[index].CR[9];
	Tempbx = Tempax; /* Tempbx <= CR07[7:0] */
	Tempax = Tempax & 0x04; /* Tempax[2]: CR7[2]: VRS[8] */
	Tempax >>= 2; /* Tempax[0]: VRS[8] */
	/* SR35[0]: VRS[8] */
	xgifb_reg_and_or(pVBInfo->P3c4, 0x35, ~0x01, Tempax);
	Tempcx |= (Tempax << 8); /* Tempcx <= VRS[8:0] */
	Tempcx |= ((Tempbx & 0x80) << 2); /* Tempcx <= VRS[9:0] */
	/* Tempax: SR0A */
	Tempax = pVBInfo->XGINEWUB_CRT1Table[index].CR[14];
	Tempax &= 0x08; /* SR0A[3] VRS[10] */
	Tempcx |= (Tempax << 7); /* Tempcx <= VRS[10:0] */

	/* Tempax: CR11 VRE */
	Tempax = pVBInfo->XGINEWUB_CRT1Table[index].CR[11];
	Tempax &= 0x0F; /* Tempax[3:0]: VRE[3:0] */
	/* Tempbx: SR0A */
	Tempbx = pVBInfo->XGINEWUB_CRT1Table[index].CR[14];
	Tempbx &= 0x20; /* Tempbx[5]: SR0A[5]: VRE[4] */
	Tempbx >>= 1; /* Tempbx[4]: VRE[4] */
	Tempax |= Tempbx; /* Tempax[4:0]: VRE[4:0] */
	Tempbx = Tempcx; /* Tempbx: VRS[10:0] */
	Tempbx &= 0x7E0; /* Tempbx[10:5]: VRS[10:5] */
	Tempbx |= Tempax; /* Tempbx: VRS[10:5]VRE[4:0] */

	if (Tempbx <= Tempcx) /* VRE <= VRS */
		Tempbx |= 0x20; /* VRE + 0x20 */

	/* Tempax: Tempax[7:0]; VRE[5:0]00 */
	Tempax = (Tempbx << 2) & 0xFF;
	/* SR3F[7:2]:VRE[5:0] */
	xgifb_reg_and_or(pVBInfo->P3c4, 0x3F, ~0xFC, Tempax);
	Tempax = Tempcx >> 8;
	/* SR35[2:0]:VRS[10:8] */
	xgifb_reg_and_or(pVBInfo->P3c4, 0x35, ~0x07, Tempax);
}

static void XGI_SetXG27FPBits(struct vb_device_info *pVBInfo)
{
	unsigned char temp;

	/* D[1:0] 01: 18bit, 00: dual 12, 10: single 24 */
	temp = xgifb_reg_get(pVBInfo->P3d4, 0x37);
	temp = (temp & 3) << 6;
	/* SR06[7]0: dual 12/1: single 24 [6] 18bit Dither <= 0 h/w recommend */
	xgifb_reg_and_or(pVBInfo->P3c4, 0x06, ~0xc0, temp & 0x80);
	/* SR09[7] enable FP output, SR09[6] 1: sigle 18bits, 0: 24bits */
	xgifb_reg_and_or(pVBInfo->P3c4, 0x09, ~0xc0, temp | 0x80);

}

static void xgifb_set_lcd(int chip_id,
			  struct vb_device_info *pVBInfo,
			  unsigned short RefreshRateTableIndex,
			  unsigned short ModeNo)
{
	unsigned short Data, Temp;
	unsigned short XGI_P3cc;

	XGI_P3cc = pVBInfo->P3cc;

	xgifb_reg_set(pVBInfo->P3d4, 0x2E, 0x00);
	xgifb_reg_set(pVBInfo->P3d4, 0x2F, 0x00);
	xgifb_reg_set(pVBInfo->P3d4, 0x46, 0x00);
	xgifb_reg_set(pVBInfo->P3d4, 0x47, 0x00);

	if (chip_id == XG27) {
		Temp = xgifb_reg_get(pVBInfo->P3d4, 0x37);
		if ((Temp & 0x03) == 0) { /* dual 12 */
			xgifb_reg_set(pVBInfo->P3d4, 0x46, 0x13);
			xgifb_reg_set(pVBInfo->P3d4, 0x47, 0x13);
		}
	}

	if (chip_id == XG27) {
		XGI_SetXG27FPBits(pVBInfo);
	} else {
		Temp = xgifb_reg_get(pVBInfo->P3d4, 0x37);
		if (Temp & 0x01) {
			/* 18 bits FP */
			xgifb_reg_or(pVBInfo->P3c4, 0x06, 0x40);
			xgifb_reg_or(pVBInfo->P3c4, 0x09, 0x40);
		}
	}

	xgifb_reg_or(pVBInfo->P3c4, 0x1E, 0x01); /* Negative blank polarity */

	xgifb_reg_and(pVBInfo->P3c4, 0x30, ~0x20); /* Hsync polarity */
	xgifb_reg_and(pVBInfo->P3c4, 0x35, ~0x80); /* Vsync polarity */

	Data = pVBInfo->RefIndex[RefreshRateTableIndex].Ext_InfoFlag;
	if (Data & 0x4000)
		/* Hsync polarity */
		xgifb_reg_or(pVBInfo->P3c4, 0x30, 0x20);
	if (Data & 0x8000)
		/* Vsync polarity */
		xgifb_reg_or(pVBInfo->P3c4, 0x35, 0x80);
}

/* --------------------------------------------------------------------- */
/* Function : XGI_UpdateXG21CRTC */
/* Input : */
/* Output : CRT1 CRTC */
/* Description : Modify CRT1 Hsync/Vsync to fix LCD mode timing */
/* --------------------------------------------------------------------- */
static void XGI_UpdateXG21CRTC(unsigned short ModeNo,
			       struct vb_device_info *pVBInfo,
			       unsigned short RefreshRateTableIndex)
{
	int index = -1;

	xgifb_reg_and(pVBInfo->P3d4, 0x11, 0x7F); /* Unlock CR0~7 */
	if (ModeNo == 0x2E &&
	    (pVBInfo->RefIndex[RefreshRateTableIndex].Ext_CRT1CRTC ==
						      RES640x480x60))
		index = 12;
	else if (ModeNo == 0x2E && (pVBInfo->RefIndex[RefreshRateTableIndex].
				Ext_CRT1CRTC == RES640x480x72))
		index = 13;
	else if (ModeNo == 0x2F)
		index = 14;
	else if (ModeNo == 0x50)
		index = 15;
	else if (ModeNo == 0x59)
		index = 16;

	if (index != -1) {
		xgifb_reg_set(pVBInfo->P3d4, 0x02,
				pVBInfo->UpdateCRT1[index].CR02);
		xgifb_reg_set(pVBInfo->P3d4, 0x03,
				pVBInfo->UpdateCRT1[index].CR03);
		xgifb_reg_set(pVBInfo->P3d4, 0x15,
				pVBInfo->UpdateCRT1[index].CR15);
		xgifb_reg_set(pVBInfo->P3d4, 0x16,
				pVBInfo->UpdateCRT1[index].CR16);
	}
}

static void XGI_SetCRT1DE(struct xgi_hw_device_info *HwDeviceExtension,
		unsigned short ModeNo, unsigned short ModeIdIndex,
		unsigned short RefreshRateTableIndex,
		struct vb_device_info *pVBInfo)
{
	unsigned short resindex, tempax, tempbx, tempcx, temp, modeflag;

	unsigned char data;

	resindex = pVBInfo->EModeIDTable[ModeIdIndex].Ext_RESINFO;

	modeflag = pVBInfo->EModeIDTable[ModeIdIndex].Ext_ModeFlag;
	tempax = pVBInfo->ModeResInfo[resindex].HTotal;
	tempbx = pVBInfo->ModeResInfo[resindex].VTotal;

	if (modeflag & HalfDCLK)
		tempax = tempax >> 1;

	if (modeflag & HalfDCLK)
		tempax = tempax << 1;

	temp = pVBInfo->RefIndex[RefreshRateTableIndex].Ext_InfoFlag;

	if (temp & InterlaceMode)
		tempbx = tempbx >> 1;

	if (modeflag & DoubleScanMode)
		tempbx = tempbx << 1;

	tempcx = 8;

	tempax /= tempcx;
	tempax -= 1;
	tempbx -= 1;
	tempcx = tempax;
	temp = (unsigned char) xgifb_reg_get(pVBInfo->P3d4, 0x11);
	data = (unsigned char) xgifb_reg_get(pVBInfo->P3d4, 0x11);
	data &= 0x7F;
	xgifb_reg_set(pVBInfo->P3d4, 0x11, data); /* Unlock CRTC */
	xgifb_reg_set(pVBInfo->P3d4, 0x01, (unsigned short) (tempcx & 0xff));
	xgifb_reg_and_or(pVBInfo->P3d4, 0x0b, ~0x0c,
			(unsigned short) ((tempcx & 0x0ff00) >> 10));
	xgifb_reg_set(pVBInfo->P3d4, 0x12, (unsigned short) (tempbx & 0xff));
	tempax = 0;
	tempbx = tempbx >> 8;

	if (tempbx & 0x01)
		tempax |= 0x02;

	if (tempbx & 0x02)
		tempax |= 0x40;

	xgifb_reg_and_or(pVBInfo->P3d4, 0x07, ~0x42, tempax);
	data = (unsigned char) xgifb_reg_get(pVBInfo->P3d4, 0x07);
	data &= 0xFF;
	tempax = 0;

	if (tempbx & 0x04)
		tempax |= 0x02;

	xgifb_reg_and_or(pVBInfo->P3d4, 0x0a, ~0x02, tempax);
	xgifb_reg_set(pVBInfo->P3d4, 0x11, temp);
}

static void XGI_SetCRT1Offset(unsigned short ModeNo,
			      unsigned short ModeIdIndex,
			      unsigned short RefreshRateTableIndex,
			      struct xgi_hw_device_info *HwDeviceExtension,
			      struct vb_device_info *pVBInfo)
{
	unsigned short temp, ah, al, temp2, i, DisplayUnit;

	/* GetOffset */
	temp = pVBInfo->EModeIDTable[ModeIdIndex].Ext_ModeInfo;
	temp = temp >> 8;
	temp = pVBInfo->ScreenOffset[temp];

	temp2 = pVBInfo->RefIndex[RefreshRateTableIndex].Ext_InfoFlag;
	temp2 &= InterlaceMode;

	if (temp2)
		temp = temp << 1;

	temp2 = pVBInfo->ModeType - ModeEGA;

	switch (temp2) {
	case 0:
		temp2 = 1;
		break;
	case 1:
		temp2 = 2;
		break;
	case 2:
		temp2 = 4;
		break;
	case 3:
		temp2 = 4;
		break;
	case 4:
		temp2 = 6;
		break;
	case 5:
		temp2 = 8;
		break;
	default:
		break;
	}

	if ((ModeNo >= 0x26) && (ModeNo <= 0x28))
		temp = temp * temp2 + temp2 / 2;
	else
		temp *= temp2;

	/* SetOffset */
	DisplayUnit = temp;
	temp2 = temp;
	temp = temp >> 8; /* ah */
	temp &= 0x0F;
	i = xgifb_reg_get(pVBInfo->P3c4, 0x0E);
	i &= 0xF0;
	i |= temp;
	xgifb_reg_set(pVBInfo->P3c4, 0x0E, i);

	temp = (unsigned char) temp2;
	temp &= 0xFF; /* al */
	xgifb_reg_set(pVBInfo->P3d4, 0x13, temp);

	/* SetDisplayUnit */
	temp2 = pVBInfo->RefIndex[RefreshRateTableIndex].Ext_InfoFlag;
	temp2 &= InterlaceMode;
	if (temp2)
		DisplayUnit >>= 1;

	DisplayUnit = DisplayUnit << 5;
	ah = (DisplayUnit & 0xff00) >> 8;
	al = DisplayUnit & 0x00ff;
	if (al == 0)
		ah += 1;
	else
		ah += 2;

	if (HwDeviceExtension->jChipType >= XG20)
		if ((ModeNo == 0x4A) | (ModeNo == 0x49))
			ah -= 1;

	xgifb_reg_set(pVBInfo->P3c4, 0x10, ah);
}

static unsigned short XGI_GetVCLK2Ptr(unsigned short ModeNo,
		unsigned short ModeIdIndex,
		unsigned short RefreshRateTableIndex,
		struct xgi_hw_device_info *HwDeviceExtension,
		struct vb_device_info *pVBInfo)
{
	unsigned short CRT2Index, VCLKIndex;
	unsigned short modeflag, resinfo;

	/* si+Ext_ResInfo */
	modeflag = pVBInfo->EModeIDTable[ModeIdIndex].Ext_ModeFlag;
	resinfo = pVBInfo->EModeIDTable[ModeIdIndex].Ext_RESINFO;
	CRT2Index = pVBInfo->RefIndex[RefreshRateTableIndex].Ext_CRT2CRTC;

	if (pVBInfo->IF_DEF_LVDS == 0) {
		CRT2Index = CRT2Index >> 6; /*  for LCD */
		if (pVBInfo->VBInfo &
		    (SetCRT2ToLCD | XGI_SetCRT2ToLCDA)) { /*301b*/
			if (pVBInfo->LCDResInfo != Panel_1024x768)
				/* LCDXlat2VCLK */
				VCLKIndex = VCLK108_2_315 + 5;
			else
				VCLKIndex = VCLK65_315 + 2; /* LCDXlat1VCLK */
		} else if (pVBInfo->VBInfo & SetCRT2ToHiVision) {
			if (pVBInfo->SetFlag & RPLLDIV2XO)
				VCLKIndex = TVCLKBASE_315_25 + HiTVVCLKDIV2;
			else
				VCLKIndex = TVCLKBASE_315_25 + HiTVVCLK;

			if (pVBInfo->SetFlag & TVSimuMode) {
				if (modeflag & Charx8Dot) {
					VCLKIndex = TVCLKBASE_315_25 +
							HiTVSimuVCLK;
				} else {
					VCLKIndex = TVCLKBASE_315_25 +
							HiTVTextVCLK;
				}
			}

			/* 301lv */
			if (pVBInfo->VBType & VB_SIS301LV) {
				if (pVBInfo->SetFlag & RPLLDIV2XO)
					VCLKIndex = YPbPr525iVCLK_2;
				else
					VCLKIndex = YPbPr525iVCLK;
			}
		} else if (pVBInfo->VBInfo & SetCRT2ToTV) {
			if (pVBInfo->SetFlag & RPLLDIV2XO)
				VCLKIndex = TVCLKBASE_315_25 + TVVCLKDIV2;
			else
				VCLKIndex = TVCLKBASE_315_25 + TVVCLK;
		} else { /* for CRT2 */
			/* di+Ext_CRTVCLK */
			VCLKIndex = pVBInfo->RefIndex[RefreshRateTableIndex].
								Ext_CRTVCLK;
			VCLKIndex &= IndexMask;
		}
	} else { /* LVDS */
		if ((pVBInfo->LCDResInfo == Panel_800x600) ||
		    (pVBInfo->LCDResInfo == Panel_320x480))
			VCLKIndex = VCLK40; /* LVDSXlat1VCLK */
		else
			VCLKIndex = VCLK65_315 + 2; /* LVDSXlat2VCLK,
						       LVDSXlat3VCLK  */
	}

	return VCLKIndex;
}

static void XGI_SetCRT1VCLK(unsigned short ModeNo,
			    unsigned short ModeIdIndex,
			    struct xgi_hw_device_info *HwDeviceExtension,
			    unsigned short RefreshRateTableIndex,
			    struct vb_device_info *pVBInfo)
{
	unsigned char index, data;
	unsigned short vclkindex;

	if (pVBInfo->IF_DEF_LVDS == 1) {
		index = pVBInfo->RefIndex[RefreshRateTableIndex].Ext_CRTVCLK;
		data = xgifb_reg_get(pVBInfo->P3c4, 0x31) & 0xCF;
		xgifb_reg_set(pVBInfo->P3c4, 0x31, data);
		xgifb_reg_set(pVBInfo->P3c4, 0x2B,
				pVBInfo->VCLKData[index].SR2B);
		xgifb_reg_set(pVBInfo->P3c4, 0x2C,
				pVBInfo->VCLKData[index].SR2C);
		xgifb_reg_set(pVBInfo->P3c4, 0x2D, 0x01);
	} else if ((pVBInfo->VBType & (VB_SIS301B | VB_SIS302B | VB_SIS301LV
			| VB_SIS302LV | VB_XGI301C)) && (pVBInfo->VBInfo
			& XGI_SetCRT2ToLCDA)) {
		vclkindex = XGI_GetVCLK2Ptr(ModeNo, ModeIdIndex,
				RefreshRateTableIndex, HwDeviceExtension,
				pVBInfo);
		data = xgifb_reg_get(pVBInfo->P3c4, 0x31) & 0xCF;
		xgifb_reg_set(pVBInfo->P3c4, 0x31, data);
		data = pVBInfo->VBVCLKData[vclkindex].Part4_A;
		xgifb_reg_set(pVBInfo->P3c4, 0x2B, data);
		data = pVBInfo->VBVCLKData[vclkindex].Part4_B;
		xgifb_reg_set(pVBInfo->P3c4, 0x2C, data);
		xgifb_reg_set(pVBInfo->P3c4, 0x2D, 0x01);
	} else {
		index = pVBInfo->RefIndex[RefreshRateTableIndex].Ext_CRTVCLK;
		data = xgifb_reg_get(pVBInfo->P3c4, 0x31) & 0xCF;
		xgifb_reg_set(pVBInfo->P3c4, 0x31, data);
		xgifb_reg_set(pVBInfo->P3c4, 0x2B,
				pVBInfo->VCLKData[index].SR2B);
		xgifb_reg_set(pVBInfo->P3c4, 0x2C,
				pVBInfo->VCLKData[index].SR2C);
		xgifb_reg_set(pVBInfo->P3c4, 0x2D, 0x01);
	}

	if (HwDeviceExtension->jChipType >= XG20) {
		if (pVBInfo->EModeIDTable[ModeIdIndex].Ext_ModeFlag &
		    HalfDCLK) {
			data = xgifb_reg_get(pVBInfo->P3c4, 0x2B);
			xgifb_reg_set(pVBInfo->P3c4, 0x2B, data);
			data = xgifb_reg_get(pVBInfo->P3c4, 0x2C);
			index = data;
			index &= 0xE0;
			data &= 0x1F;
			data = data << 1;
			data += 1;
			data |= index;
			xgifb_reg_set(pVBInfo->P3c4, 0x2C, data);
		}
	}
}

static void XGI_SetXG21FPBits(struct vb_device_info *pVBInfo)
{
	unsigned char temp;

	temp = xgifb_reg_get(pVBInfo->P3d4, 0x37); /* D[0] 1: 18bit */
	temp = (temp & 1) << 6;
	/* SR06[6] 18bit Dither */
	xgifb_reg_and_or(pVBInfo->P3c4, 0x06, ~0x40, temp);
	/* SR09[7] enable FP output, SR09[6] 1: sigle 18bits, 0: dual 12bits */
	xgifb_reg_and_or(pVBInfo->P3c4, 0x09, ~0xc0, temp | 0x80);

}

static void XGI_SetCRT1FIFO(unsigned short ModeNo,
		struct xgi_hw_device_info *HwDeviceExtension,
		struct vb_device_info *pVBInfo)
{
	unsigned short data;

	data = xgifb_reg_get(pVBInfo->P3c4, 0x3D);
	data &= 0xfe;
	xgifb_reg_set(pVBInfo->P3c4, 0x3D, data); /* diable auto-threshold */

	xgifb_reg_set(pVBInfo->P3c4, 0x08, 0x34);
	data = xgifb_reg_get(pVBInfo->P3c4, 0x09);
	data &= 0xC0;
	xgifb_reg_set(pVBInfo->P3c4, 0x09, data | 0x30);
	data = xgifb_reg_get(pVBInfo->P3c4, 0x3D);
	data |= 0x01;
	xgifb_reg_set(pVBInfo->P3c4, 0x3D, data);

	if (HwDeviceExtension->jChipType == XG21)
		XGI_SetXG21FPBits(pVBInfo); /* Fix SR9[7:6] can't read back */
}

static void XGI_SetVCLKState(struct xgi_hw_device_info *HwDeviceExtension,
		unsigned short ModeNo, unsigned short RefreshRateTableIndex,
		struct vb_device_info *pVBInfo)
{
	unsigned short data, data2 = 0;
	short VCLK;

	unsigned char index;

	index = pVBInfo->RefIndex[RefreshRateTableIndex].Ext_CRTVCLK;
	index &= IndexMask;
	VCLK = pVBInfo->VCLKData[index].CLOCK;

	data = xgifb_reg_get(pVBInfo->P3c4, 0x32);
	data &= 0xf3;
	if (VCLK >= 200)
		data |= 0x0c; /* VCLK > 200 */

	if (HwDeviceExtension->jChipType >= XG20)
		data &= ~0x04; /* 2 pixel mode */

	xgifb_reg_set(pVBInfo->P3c4, 0x32, data);

	if (HwDeviceExtension->jChipType < XG20) {
		data = xgifb_reg_get(pVBInfo->P3c4, 0x1F);
		data &= 0xE7;
		if (VCLK < 200)
			data |= 0x10;
		xgifb_reg_set(pVBInfo->P3c4, 0x1F, data);
	}

	data2 = 0x00;

	xgifb_reg_and_or(pVBInfo->P3c4, 0x07, 0xFC, data2);
	if (HwDeviceExtension->jChipType >= XG27)
		xgifb_reg_and_or(pVBInfo->P3c4, 0x40, 0xFC, data2 & 0x03);

}

static void XGI_SetCRT1ModeRegs(struct xgi_hw_device_info *HwDeviceExtension,
		unsigned short ModeNo, unsigned short ModeIdIndex,
		unsigned short RefreshRateTableIndex,
		struct vb_device_info *pVBInfo)
{
	unsigned short data, data2, data3, infoflag = 0, modeflag, resindex,
			xres;

	modeflag = pVBInfo->EModeIDTable[ModeIdIndex].Ext_ModeFlag;
	infoflag = pVBInfo->RefIndex[RefreshRateTableIndex].Ext_InfoFlag;

	if (xgifb_reg_get(pVBInfo->P3d4, 0x31) & 0x01)
		xgifb_reg_and_or(pVBInfo->P3c4, 0x1F, 0x3F, 0x00);

	data = infoflag;
	data2 = 0;
	data2 |= 0x02;
	data3 = pVBInfo->ModeType - ModeVGA;
	data3 = data3 << 2;
	data2 |= data3;
	data &= InterlaceMode;

	if (data)
		data2 |= 0x20;

	xgifb_reg_and_or(pVBInfo->P3c4, 0x06, ~0x3F, data2);
	resindex = pVBInfo->EModeIDTable[ModeIdIndex].Ext_RESINFO;
	xres = pVBInfo->ModeResInfo[resindex].HTotal; /* xres->ax */

	data = 0x0000;
	if (infoflag & InterlaceMode) {
		if (xres == 1024)
			data = 0x0035;
		else if (xres == 1280)
			data = 0x0048;
	}

	data2 = data & 0x00FF;
	xgifb_reg_and_or(pVBInfo->P3d4, 0x19, 0xFF, data2);
	data2 = (data & 0xFF00) >> 8;
	xgifb_reg_and_or(pVBInfo->P3d4, 0x19, 0xFC, data2);

	if (modeflag & HalfDCLK)
		xgifb_reg_and_or(pVBInfo->P3c4, 0x01, 0xF7, 0x08);

	data2 = 0;

	if (modeflag & LineCompareOff)
		data2 |= 0x08;

	xgifb_reg_and_or(pVBInfo->P3c4, 0x0F, ~0x48, data2);
	data = 0x60;
	data = data ^ 0x60;
	data = data ^ 0xA0;
	xgifb_reg_and_or(pVBInfo->P3c4, 0x21, 0x1F, data);

	XGI_SetVCLKState(HwDeviceExtension, ModeNo, RefreshRateTableIndex,
			pVBInfo);

	data = xgifb_reg_get(pVBInfo->P3d4, 0x31);

	if (HwDeviceExtension->jChipType == XG27) {
		if (data & 0x40)
			data = 0x2c;
		else
			data = 0x6c;
		xgifb_reg_set(pVBInfo->P3d4, 0x52, data);
		xgifb_reg_or(pVBInfo->P3d4, 0x51, 0x10);
	} else if (HwDeviceExtension->jChipType >= XG20) {
		if (data & 0x40)
			data = 0x33;
		else
			data = 0x73;
		xgifb_reg_set(pVBInfo->P3d4, 0x52, data);
		xgifb_reg_set(pVBInfo->P3d4, 0x51, 0x02);
	} else {
		if (data & 0x40)
			data = 0x2c;
		else
			data = 0x6c;
		xgifb_reg_set(pVBInfo->P3d4, 0x52, data);
	}

}

static void XGI_WriteDAC(unsigned short dl,
			 unsigned short ah,
			 unsigned short al,
			 unsigned short dh,
			 struct vb_device_info *pVBInfo)
{
	unsigned short temp, bh, bl;

	bh = ah;
	bl = al;

	if (dl != 0) {
		temp = bh;
		bh = dh;
		dh = temp;
		if (dl == 1) {
			temp = bl;
			bl = dh;
			dh = temp;
		} else {
			temp = bl;
			bl = bh;
			bh = temp;
		}
	}
	outb((unsigned short) dh, pVBInfo->P3c9);
	outb((unsigned short) bh, pVBInfo->P3c9);
	outb((unsigned short) bl, pVBInfo->P3c9);
}

static void XGI_LoadDAC(unsigned short ModeNo, unsigned short ModeIdIndex,
		struct vb_device_info *pVBInfo)
{
	unsigned short data, data2, i, k, m, n, o, si, di, bx, dl, al, ah, dh;
	const unsigned short *table = XGINew_VGA_DAC;

	outb(0xFF, pVBInfo->P3c6);
	outb(0x00, pVBInfo->P3c8);

	for (i = 0; i < 16; i++) {
		data = table[i];

		for (k = 0; k < 3; k++) {
			data2 = 0;

			if (data & 0x01)
				data2 = 0x2A;

			if (data & 0x02)
				data2 += 0x15;

			outb(data2, pVBInfo->P3c9);
			data = data >> 2;
		}
	}

	for (i = 16; i < 32; i++) {
		data = table[i];

		for (k = 0; k < 3; k++)
			outb(data, pVBInfo->P3c9);
	}

	si = 32;

	for (m = 0; m < 9; m++) {
		di = si;
		bx = si + 0x04;
		dl = 0;

		for (n = 0; n < 3; n++) {
			for (o = 0; o < 5; o++) {
				dh = table[si];
				ah = table[di];
				al = table[bx];
				si++;
				XGI_WriteDAC(dl, ah, al, dh, pVBInfo);
			}

			si -= 2;

			for (o = 0; o < 3; o++) {
				dh = table[bx];
				ah = table[di];
				al = table[si];
				si--;
				XGI_WriteDAC(dl, ah, al, dh, pVBInfo);
			}

			dl++;
		}

		si += 5;
	}
}

static void XGI_GetLVDSResInfo(unsigned short ModeNo,
			       unsigned short ModeIdIndex,
			       struct vb_device_info *pVBInfo)
{
	unsigned short resindex, xres, yres, modeflag;

	/* si+Ext_ResInfo */
	modeflag = pVBInfo->EModeIDTable[ModeIdIndex].Ext_RESINFO;

	/* si+Ext_ResInfo */
	resindex = pVBInfo->EModeIDTable[ModeIdIndex].Ext_RESINFO;

	xres = pVBInfo->ModeResInfo[resindex].HTotal;
	yres = pVBInfo->ModeResInfo[resindex].VTotal;

	if (modeflag & HalfDCLK)
		xres = xres << 1;

	if (modeflag & DoubleScanMode)
		yres = yres << 1;

	if (xres == 720)
		xres = 640;

	pVBInfo->VGAHDE = xres;
	pVBInfo->HDE = xres;
	pVBInfo->VGAVDE = yres;
	pVBInfo->VDE = yres;
}

static void *XGI_GetLcdPtr(unsigned short BX, unsigned short ModeNo,
		unsigned short ModeIdIndex,
		unsigned short RefreshRateTableIndex,
		struct vb_device_info *pVBInfo)
{
	unsigned short i, tempdx, tempcx, tempbx, tempal, modeflag, table;

	struct XGI330_LCDDataTablStruct *tempdi = NULL;

	tempbx = BX;

	modeflag = pVBInfo->EModeIDTable[ModeIdIndex].Ext_ModeFlag;
	tempal = pVBInfo->RefIndex[RefreshRateTableIndex].Ext_CRT2CRTC;

	tempal = tempal & 0x0f;

	if (tempbx <= 1) { /* ExpLink */
		tempal = pVBInfo->RefIndex[RefreshRateTableIndex].Ext_CRT2CRTC;

		if (pVBInfo->VBInfo & XGI_SetCRT2ToLCDA) {
			tempal = pVBInfo->RefIndex[RefreshRateTableIndex].
							Ext_CRT2CRTC2;
		}

		if (tempbx & 0x01)
			tempal = (tempal >> 4);

		tempal = (tempal & 0x0f);
	}

	tempcx = LCDLenList[tempbx];

	if (pVBInfo->LCDInfo & EnableScalingLCD) { /* ScaleLCD */
		if (tempbx == 5)
			tempcx = LCDDesDataLen2;
		else if (tempbx == 3)
			tempcx = LVDSDesDataLen2;
	}

	switch (tempbx) {
	case 0:
	case 1:
		tempdi = xgifb_epllcd_crt1;
		break;
	case 2:
		tempdi = XGI_EPLLCDDataPtr;
		break;
	case 3:
		tempdi = XGI_EPLLCDDesDataPtr;
		break;
	case 4:
		tempdi = XGI_LCDDataTable;
		break;
	case 5:
		tempdi = XGI_LCDDesDataTable;
		break;
	default:
		break;
	}

	if (tempdi == NULL) /* OEMUtil */
		return NULL;

	table = tempbx;
	i = 0;

	while (tempdi[i].PANELID != 0xff) {
		tempdx = pVBInfo->LCDResInfo;
		if (tempbx & 0x0080) { /* OEMUtil */
			tempbx &= (~0x0080);
			tempdx = pVBInfo->LCDTypeInfo;
		}

		if (pVBInfo->LCDInfo & EnableScalingLCD)
			tempdx &= (~PanelResInfo);

		if (tempdi[i].PANELID == tempdx) {
			tempbx = tempdi[i].MASK;
			tempdx = pVBInfo->LCDInfo;

			if (modeflag & HalfDCLK)
				tempdx |= SetLCDLowResolution;

			tempbx &= tempdx;
			if (tempbx == tempdi[i].CAP)
				break;
		}
		i++;
	}

	if (table == 0) {
		switch (tempdi[i].DATAPTR) {
		case 0:
			return &XGI_LVDSCRT11024x768_1_H[tempal];
			break;
		case 1:
			return &XGI_LVDSCRT11024x768_2_H[tempal];
			break;
		case 2:
			return &XGI_LVDSCRT11280x1024_1_H[tempal];
			break;
		case 3:
			return &XGI_LVDSCRT11280x1024_2_H[tempal];
			break;
		case 4:
			return &XGI_LVDSCRT11400x1050_1_H[tempal];
			break;
		case 5:
			return &XGI_LVDSCRT11400x1050_2_H[tempal];
			break;
		case 6:
			return &XGI_LVDSCRT11600x1200_1_H[tempal];
			break;
		case 7:
			return &XGI_LVDSCRT11024x768_1_Hx75[tempal];
			break;
		case 8:
			return &XGI_LVDSCRT11024x768_2_Hx75[tempal];
			break;
		case 9:
			return &XGI_LVDSCRT11280x1024_1_Hx75[tempal];
			break;
		case 10:
			return &XGI_LVDSCRT11280x1024_2_Hx75[tempal];
			break;
		default:
			break;
		}
	} else if (table == 1) {
		switch (tempdi[i].DATAPTR) {
		case 0:
			return &XGI_LVDSCRT11024x768_1_V[tempal];
			break;
		case 1:
			return &XGI_LVDSCRT11024x768_2_V[tempal];
			break;
		case 2:
			return &XGI_LVDSCRT11280x1024_1_V[tempal];
			break;
		case 3:
			return &XGI_LVDSCRT11280x1024_2_V[tempal];
			break;
		case 4:
			return &XGI_LVDSCRT11400x1050_1_V[tempal];
			break;
		case 5:
			return &XGI_LVDSCRT11400x1050_2_V[tempal];
			break;
		case 6:
			return &XGI_LVDSCRT11600x1200_1_V[tempal];
			break;
		case 7:
			return &XGI_LVDSCRT11024x768_1_Vx75[tempal];
			break;
		case 8:
			return &XGI_LVDSCRT11024x768_2_Vx75[tempal];
			break;
		case 9:
			return &XGI_LVDSCRT11280x1024_1_Vx75[tempal];
			break;
		case 10:
			return &XGI_LVDSCRT11280x1024_2_Vx75[tempal];
			break;
		default:
			break;
		}
	} else if (table == 2) {
		switch (tempdi[i].DATAPTR) {
		case 0:
			return &XGI_LVDS1024x768Data_1[tempal];
			break;
		case 1:
			return &XGI_LVDS1024x768Data_2[tempal];
			break;
		case 2:
			return &XGI_LVDS1280x1024Data_1[tempal];
			break;
		case 3:
			return &XGI_LVDS1280x1024Data_2[tempal];
			break;
		case 4:
			return &XGI_LVDS1400x1050Data_1[tempal];
			break;
		case 5:
			return &XGI_LVDS1400x1050Data_2[tempal];
			break;
		case 6:
			return &XGI_LVDS1600x1200Data_1[tempal];
			break;
		case 7:
			return &XGI_LVDSNoScalingData[tempal];
			break;
		case 8:
			return &XGI_LVDS1024x768Data_1x75[tempal];
			break;
		case 9:
			return &XGI_LVDS1024x768Data_2x75[tempal];
			break;
		case 10:
			return &XGI_LVDS1280x1024Data_1x75[tempal];
			break;
		case 11:
			return &XGI_LVDS1280x1024Data_2x75[tempal];
			break;
		case 12:
			return &XGI_LVDSNoScalingDatax75[tempal];
			break;
		default:
			break;
		}
	} else if (table == 3) {
		switch (tempdi[i].DATAPTR) {
		case 0:
			return &XGI_LVDS1024x768Des_1[tempal];
			break;
		case 1:
			return &XGI_LVDS1024x768Des_3[tempal];
			break;
		case 2:
			return &XGI_LVDS1024x768Des_2[tempal];
			break;
		case 3:
			return &XGI_LVDS1280x1024Des_1[tempal];
			break;
		case 4:
			return &XGI_LVDS1280x1024Des_2[tempal];
			break;
		case 5:
			return &XGI_LVDS1400x1050Des_1[tempal];
			break;
		case 6:
			return &XGI_LVDS1400x1050Des_2[tempal];
			break;
		case 7:
			return &XGI_LVDS1600x1200Des_1[tempal];
			break;
		case 8:
			return &XGI_LVDSNoScalingDesData[tempal];
			break;
		case 9:
			return &XGI_LVDS1024x768Des_1x75[tempal];
			break;
		case 10:
			return &XGI_LVDS1024x768Des_3x75[tempal];
			break;
		case 11:
			return &XGI_LVDS1024x768Des_2x75[tempal];
			break;
		case 12:
			return &XGI_LVDS1280x1024Des_1x75[tempal];
			break;
		case 13:
			return &XGI_LVDS1280x1024Des_2x75[tempal];
			break;
		case 14:
			return &XGI_LVDSNoScalingDesDatax75[tempal];
			break;
		default:
			break;
		}
	} else if (table == 4) {
		switch (tempdi[i].DATAPTR) {
		case 0:
			return &XGI_ExtLCD1024x768Data[tempal];
			break;
		case 1:
			return &XGI_StLCD1024x768Data[tempal];
			break;
		case 2:
			return &XGI_CetLCD1024x768Data[tempal];
			break;
		case 3:
			return &XGI_ExtLCD1280x1024Data[tempal];
			break;
		case 4:
			return &XGI_StLCD1280x1024Data[tempal];
			break;
		case 5:
			return &XGI_CetLCD1280x1024Data[tempal];
			break;
		case 6:
		case 7:
			return &xgifb_lcd_1400x1050[tempal];
			break;
		case 8:
			return &XGI_CetLCD1400x1050Data[tempal];
			break;
		case 9:
			return &XGI_ExtLCD1600x1200Data[tempal];
			break;
		case 10:
			return &XGI_StLCD1600x1200Data[tempal];
			break;
		case 11:
			return &XGI_NoScalingData[tempal];
			break;
		case 12:
			return &XGI_ExtLCD1024x768x75Data[tempal];
			break;
		case 13:
			return &XGI_ExtLCD1024x768x75Data[tempal];
			break;
		case 14:
			return &XGI_CetLCD1024x768x75Data[tempal];
			break;
		case 15:
		case 16:
			return &xgifb_lcd_1280x1024x75[tempal];
			break;
		case 17:
			return &XGI_CetLCD1280x1024x75Data[tempal];
			break;
		case 18:
			return &XGI_NoScalingDatax75[tempal];
			break;
		default:
			break;
		}
	} else if (table == 5) {
		switch (tempdi[i].DATAPTR) {
		case 0:
			return &XGI_ExtLCDDes1024x768Data[tempal];
			break;
		case 1:
			return &XGI_StLCDDes1024x768Data[tempal];
			break;
		case 2:
			return &XGI_CetLCDDes1024x768Data[tempal];
			break;
		case 3:
			if ((pVBInfo->VBType & VB_SIS301LV) ||
				(pVBInfo->VBType & VB_SIS302LV))
				return &XGI_ExtLCDDLDes1280x1024Data[tempal];
			else
				return &XGI_ExtLCDDes1280x1024Data[tempal];
			break;
		case 4:
			if ((pVBInfo->VBType & VB_SIS301LV) ||
			    (pVBInfo->VBType & VB_SIS302LV))
				return &XGI_StLCDDLDes1280x1024Data[tempal];
			else
				return &XGI_StLCDDes1280x1024Data[tempal];
			break;
		case 5:
			if ((pVBInfo->VBType & VB_SIS301LV) ||
			    (pVBInfo->VBType & VB_SIS302LV))
				return &XGI_CetLCDDLDes1280x1024Data[tempal];
			else
				return &XGI_CetLCDDes1280x1024Data[tempal];
			break;
		case 6:
		case 7:
			if ((pVBInfo->VBType & VB_SIS301LV) ||
			    (pVBInfo->VBType & VB_SIS302LV))
				return &xgifb_lcddldes_1400x1050[tempal];
			else
				return &xgifb_lcddes_1400x1050[tempal];
			break;
		case 8:
			return &XGI_CetLCDDes1400x1050Data[tempal];
			break;
		case 9:
			return &XGI_CetLCDDes1400x1050Data2[tempal];
			break;
		case 10:
			if ((pVBInfo->VBType & VB_SIS301LV) ||
			    (pVBInfo->VBType & VB_SIS302LV))
				return &XGI_ExtLCDDLDes1600x1200Data[tempal];
			else
				return &XGI_ExtLCDDes1600x1200Data[tempal];
			break;
		case 11:
			if ((pVBInfo->VBType & VB_SIS301LV) ||
			    (pVBInfo->VBType & VB_SIS302LV))
				return &XGI_StLCDDLDes1600x1200Data[tempal];
			else
				return &XGI_StLCDDes1600x1200Data[tempal];
			break;
		case 12:
			return &XGI_NoScalingDesData[tempal];
			break;
		case 13:
		case 14:
			return &xgifb_lcddes_1024x768x75[tempal];
			break;
		case 15:
			return &XGI_CetLCDDes1024x768x75Data[tempal];
			break;
		case 16:
		case 17:
			if ((pVBInfo->VBType & VB_SIS301LV) ||
			    (pVBInfo->VBType & VB_SIS302LV))
				return &xgifb_lcddldes_1280x1024x75[tempal];
			else
				return &xgifb_lcddes_1280x1024x75[tempal];
			break;
		case 18:
			if ((pVBInfo->VBType & VB_SIS301LV) ||
			    (pVBInfo->VBType & VB_SIS302LV))
				return &XGI_CetLCDDLDes1280x1024x75Data[tempal];
			else
				return &XGI_CetLCDDes1280x1024x75Data[tempal];
			break;
		case 19:
			return &XGI_NoScalingDesDatax75[tempal];
			break;
		default:
			break;
		}
	}
	return NULL;
}

static struct SiS_TVData const *XGI_GetTVPtr(unsigned short ModeNo,
		unsigned short ModeIdIndex,
		unsigned short RefreshRateTableIndex,
		struct vb_device_info *pVBInfo)
{
	unsigned short i, tempdx, tempal, modeflag;

	modeflag = pVBInfo->EModeIDTable[ModeIdIndex].Ext_ModeFlag;
	tempal = pVBInfo->RefIndex[RefreshRateTableIndex].Ext_CRT2CRTC;
	tempal = tempal & 0x3f;
	tempdx = pVBInfo->TVInfo;

	if (pVBInfo->VBInfo & SetInSlaveMode)
		tempdx = tempdx | SetTVLockMode;

	if (modeflag & HalfDCLK)
		tempdx = tempdx | SetTVLowResolution;

	i = 0;

	while (XGI_TVDataTable[i].MASK != 0xffff) {
		if ((tempdx & XGI_TVDataTable[i].MASK) ==
			XGI_TVDataTable[i].CAP)
			break;
		i++;
	}

	return &XGI_TVDataTable[i].DATAPTR[tempal];
}

static void XGI_GetLVDSData(unsigned short ModeNo, unsigned short ModeIdIndex,
		unsigned short RefreshRateTableIndex,
		struct vb_device_info *pVBInfo)
{
	unsigned short tempbx;
	struct SiS_LVDSData *LCDPtr = NULL;

	tempbx = 2;

	if (pVBInfo->VBInfo & (SetCRT2ToLCD | XGI_SetCRT2ToLCDA)) {
		LCDPtr = (struct SiS_LVDSData *)XGI_GetLcdPtr(tempbx,
				ModeNo, ModeIdIndex, RefreshRateTableIndex,
				pVBInfo);
		pVBInfo->VGAHT = LCDPtr->VGAHT;
		pVBInfo->VGAVT = LCDPtr->VGAVT;
		pVBInfo->HT = LCDPtr->LCDHT;
		pVBInfo->VT = LCDPtr->LCDVT;
	}

	if (pVBInfo->VBInfo & (SetCRT2ToLCD | XGI_SetCRT2ToLCDA)) {
		if (!(pVBInfo->LCDInfo & (SetLCDtoNonExpanding
				| EnableScalingLCD))) {
			if ((pVBInfo->LCDResInfo == Panel_1024x768) ||
			    (pVBInfo->LCDResInfo == Panel_1024x768x75)) {
				pVBInfo->HDE = 1024;
				pVBInfo->VDE = 768;
			} else if ((pVBInfo->LCDResInfo == Panel_1280x1024) ||
				   (pVBInfo->LCDResInfo ==
					Panel_1280x1024x75)) {
				pVBInfo->HDE = 1280;
				pVBInfo->VDE = 1024;
			} else if (pVBInfo->LCDResInfo == Panel_1400x1050) {
				pVBInfo->HDE = 1400;
				pVBInfo->VDE = 1050;
			} else {
				pVBInfo->HDE = 1600;
				pVBInfo->VDE = 1200;
			}
		}
	}
}

static void XGI_ModCRT1Regs(unsigned short ModeNo, unsigned short ModeIdIndex,
		unsigned short RefreshRateTableIndex,
		struct xgi_hw_device_info *HwDeviceExtension,
		struct vb_device_info *pVBInfo)
{
	unsigned char index;
	unsigned short tempbx, i;
	struct XGI_LVDSCRT1HDataStruct *LCDPtr = NULL;
	struct XGI_LVDSCRT1VDataStruct *LCDPtr1 = NULL;

	index = pVBInfo->RefIndex[RefreshRateTableIndex].Ext_CRT2CRTC;
	index = index & IndexMask;

	tempbx = 0;

	if (pVBInfo->VBInfo & (SetCRT2ToLCD | XGI_SetCRT2ToLCDA)) {
		LCDPtr = (struct XGI_LVDSCRT1HDataStruct *)
				XGI_GetLcdPtr(tempbx, ModeNo,
					      ModeIdIndex,
					      RefreshRateTableIndex,
					      pVBInfo);

		for (i = 0; i < 8; i++)
			pVBInfo->TimingH[0].data[i] = LCDPtr[0].Reg[i];
	}

	XGI_SetCRT1Timing_H(pVBInfo, HwDeviceExtension);

	tempbx = 1;

	if (pVBInfo->VBInfo & (SetCRT2ToLCD | XGI_SetCRT2ToLCDA)) {
		LCDPtr1 = (struct XGI_LVDSCRT1VDataStruct *)
				XGI_GetLcdPtr(
					tempbx,
					ModeNo,
					ModeIdIndex,
					RefreshRateTableIndex,
					pVBInfo);
		for (i = 0; i < 7; i++)
			pVBInfo->TimingV[0].data[i] = LCDPtr1[0].Reg[i];
	}

	XGI_SetCRT1Timing_V(ModeIdIndex, ModeNo, pVBInfo);
}

static unsigned short XGI_GetLCDCapPtr(struct vb_device_info *pVBInfo)
{
	unsigned char tempal, tempah, tempbl, i;

	tempah = xgifb_reg_get(pVBInfo->P3d4, 0x36);
	tempal = tempah & 0x0F;
	tempah = tempah & 0xF0;
	i = 0;
	tempbl = pVBInfo->LCDCapList[i].LCD_ID;

	while (tempbl != 0xFF) {
		if (tempbl & 0x80) { /* OEMUtil */
			tempal = tempah;
			tempbl = tempbl & ~(0x80);
		}

		if (tempal == tempbl)
			break;

		i++;

		tempbl = pVBInfo->LCDCapList[i].LCD_ID;
	}

	return i;
}

static unsigned short XGI_GetLCDCapPtr1(struct vb_device_info *pVBInfo)
{
	unsigned short tempah, tempal, tempbl, i;

	tempal = pVBInfo->LCDResInfo;
	tempah = pVBInfo->LCDTypeInfo;

	i = 0;
	tempbl = pVBInfo->LCDCapList[i].LCD_ID;

	while (tempbl != 0xFF) {
		if ((tempbl & 0x80) && (tempbl != 0x80)) {
			tempal = tempah;
			tempbl &= ~0x80;
		}

		if (tempal == tempbl)
			break;

		i++;
		tempbl = pVBInfo->LCDCapList[i].LCD_ID;
	}

	if (tempbl == 0xFF) {
		pVBInfo->LCDResInfo = Panel_1024x768;
		pVBInfo->LCDTypeInfo = 0;
		i = 0;
	}

	return i;
}

static void XGI_GetLCDSync(unsigned short *HSyncWidth,
			   unsigned short *VSyncWidth,
			   struct vb_device_info *pVBInfo)
{
	unsigned short Index;

	Index = XGI_GetLCDCapPtr(pVBInfo);
	*HSyncWidth = pVBInfo->LCDCapList[Index].LCD_HSyncWidth;
	*VSyncWidth = pVBInfo->LCDCapList[Index].LCD_VSyncWidth;

	return;
}

static void XGI_SetLVDSRegs(unsigned short ModeNo, unsigned short ModeIdIndex,
		unsigned short RefreshRateTableIndex,
		struct vb_device_info *pVBInfo)
{
	unsigned short tempbx, tempax, tempcx, tempdx, push1, push2, modeflag;
	unsigned long temp, temp1, temp2, temp3, push3;
	struct XGI_LCDDesStruct *LCDPtr = NULL;
	struct XGI330_LCDDataDesStruct2 *LCDPtr1 = NULL;

	modeflag = pVBInfo->EModeIDTable[ModeIdIndex].Ext_ModeFlag;
	tempbx = 3;
	if (pVBInfo->LCDInfo & EnableScalingLCD)
		LCDPtr1 =
		    (struct XGI330_LCDDataDesStruct2 *)
				XGI_GetLcdPtr(
					  tempbx,
					  ModeNo,
					  ModeIdIndex,
					  RefreshRateTableIndex,
					  pVBInfo);
	else
		LCDPtr =
		    (struct XGI_LCDDesStruct *)
				XGI_GetLcdPtr(
					  tempbx,
					  ModeNo,
					  ModeIdIndex,
					  RefreshRateTableIndex,
					  pVBInfo);

	XGI_GetLCDSync(&tempax, &tempbx, pVBInfo);
	push1 = tempbx;
	push2 = tempax;

	/* GetLCDResInfo */
	if ((pVBInfo->LCDResInfo == Panel_1024x768) ||
	    (pVBInfo->LCDResInfo == Panel_1024x768x75)) {
		tempax = 1024;
		tempbx = 768;
	} else if ((pVBInfo->LCDResInfo == Panel_1280x1024) ||
		   (pVBInfo->LCDResInfo == Panel_1280x1024x75)) {
		tempax = 1280;
		tempbx = 1024;
	} else if (pVBInfo->LCDResInfo == Panel_1400x1050) {
		tempax = 1400;
		tempbx = 1050;
	} else {
		tempax = 1600;
		tempbx = 1200;
	}

	if (pVBInfo->LCDInfo & SetLCDtoNonExpanding) {
		pVBInfo->HDE = tempax;
		pVBInfo->VDE = tempbx;
		pVBInfo->VGAHDE = tempax;
		pVBInfo->VGAVDE = tempbx;
	}

	tempax = pVBInfo->HT;

	if (pVBInfo->LCDInfo & EnableScalingLCD)
		tempbx = LCDPtr1->LCDHDES;
	else
		tempbx = LCDPtr->LCDHDES;

	tempcx = pVBInfo->HDE;
	tempbx = tempbx & 0x0fff;
	tempcx += tempbx;

	if (tempcx >= tempax)
		tempcx -= tempax;

	xgifb_reg_set(pVBInfo->Part1Port, 0x1A, tempbx & 0x07);

	tempcx = tempcx >> 3;
	tempbx = tempbx >> 3;

	xgifb_reg_set(pVBInfo->Part1Port, 0x16,
			(unsigned short) (tempbx & 0xff));
	xgifb_reg_set(pVBInfo->Part1Port, 0x17,
			(unsigned short) (tempcx & 0xff));

	tempax = pVBInfo->HT;

	if (pVBInfo->LCDInfo & EnableScalingLCD)
		tempbx = LCDPtr1->LCDHRS;
	else
		tempbx = LCDPtr->LCDHRS;

	tempcx = push2;

	if (pVBInfo->LCDInfo & EnableScalingLCD)
		tempcx = LCDPtr1->LCDHSync;

	tempcx += tempbx;

	if (tempcx >= tempax)
		tempcx -= tempax;

	tempax = tempbx & 0x07;
	tempax = tempax >> 5;
	tempcx = tempcx >> 3;
	tempbx = tempbx >> 3;

	tempcx &= 0x1f;
	tempax |= tempcx;

	xgifb_reg_set(pVBInfo->Part1Port, 0x15, tempax);
	xgifb_reg_set(pVBInfo->Part1Port, 0x14,
			(unsigned short) (tempbx & 0xff));

	tempax = pVBInfo->VT;
	if (pVBInfo->LCDInfo & EnableScalingLCD)
		tempbx = LCDPtr1->LCDVDES;
	else
		tempbx = LCDPtr->LCDVDES;
	tempcx = pVBInfo->VDE;

	tempbx = tempbx & 0x0fff;
	tempcx += tempbx;
	if (tempcx >= tempax)
		tempcx -= tempax;

	xgifb_reg_set(pVBInfo->Part1Port, 0x1b,
			(unsigned short) (tempbx & 0xff));
	xgifb_reg_set(pVBInfo->Part1Port, 0x1c,
			(unsigned short) (tempcx & 0xff));

	tempbx = (tempbx >> 8) & 0x07;
	tempcx = (tempcx >> 8) & 0x07;

	xgifb_reg_set(pVBInfo->Part1Port, 0x1d,
			(unsigned short) ((tempcx << 3)
					| tempbx));

	tempax = pVBInfo->VT;
	if (pVBInfo->LCDInfo & EnableScalingLCD)
		tempbx = LCDPtr1->LCDVRS;
	else
		tempbx = LCDPtr->LCDVRS;

	tempcx = push1;

	if (pVBInfo->LCDInfo & EnableScalingLCD)
		tempcx = LCDPtr1->LCDVSync;

	tempcx += tempbx;
	if (tempcx >= tempax)
		tempcx -= tempax;

	xgifb_reg_set(pVBInfo->Part1Port, 0x18,
			(unsigned short) (tempbx & 0xff));
	xgifb_reg_and_or(pVBInfo->Part1Port, 0x19, ~0x0f,
			(unsigned short) (tempcx & 0x0f));

	tempax = ((tempbx >> 8) & 0x07) << 3;

	tempbx = pVBInfo->VGAVDE;
	if (tempbx != pVBInfo->VDE)
		tempax |= 0x40;

	if (pVBInfo->LCDInfo & XGI_EnableLVDSDDA)
		tempax |= 0x40;

	xgifb_reg_and_or(pVBInfo->Part1Port, 0x1a, 0x07,
				tempax);

	tempcx = pVBInfo->VGAVT;
	tempbx = pVBInfo->VDE;
	tempax = pVBInfo->VGAVDE;
	tempcx -= tempax;

	temp = tempax; /* 0430 ylshieh */
	temp1 = (temp << 18) / tempbx;

	tempdx = (unsigned short) ((temp << 18) % tempbx);

	if (tempdx != 0)
		temp1 += 1;

	temp2 = temp1;
	push3 = temp2;

	xgifb_reg_set(pVBInfo->Part1Port, 0x37,
			(unsigned short) (temp2 & 0xff));
	xgifb_reg_set(pVBInfo->Part1Port, 0x36,
			(unsigned short) ((temp2 >> 8) & 0xff));

	tempbx = (unsigned short) (temp2 >> 16);
	tempax = tempbx & 0x03;

	tempbx = pVBInfo->VGAVDE;
	if (tempbx == pVBInfo->VDE)
		tempax |= 0x04;

	xgifb_reg_set(pVBInfo->Part1Port, 0x35, tempax);

	if (pVBInfo->VBType & VB_XGI301C) {
		temp2 = push3;
		xgifb_reg_set(pVBInfo->Part4Port,
			      0x3c,
			      (unsigned short) (temp2 & 0xff));
		xgifb_reg_set(pVBInfo->Part4Port,
			      0x3b,
			      (unsigned short) ((temp2 >> 8) &
			      0xff));
		tempbx = (unsigned short) (temp2 >> 16);
		xgifb_reg_and_or(pVBInfo->Part4Port, 0x3a,
				~0xc0,
				(unsigned short) ((tempbx &
						   0xff) << 6));

		tempcx = pVBInfo->VGAVDE;
		if (tempcx == pVBInfo->VDE)
			xgifb_reg_and_or(pVBInfo->Part4Port,
					0x30, ~0x0c, 0x00);
		else
			xgifb_reg_and_or(pVBInfo->Part4Port,
					0x30, ~0x0c, 0x08);
	}

	tempcx = pVBInfo->VGAHDE;
	tempbx = pVBInfo->HDE;

	temp1 = tempcx << 16;

	tempax = (unsigned short) (temp1 / tempbx);

	if ((tempbx & 0xffff) == (tempcx & 0xffff))
		tempax = 65535;

	temp3 = tempax;
	temp1 = pVBInfo->VGAHDE << 16;

	temp1 /= temp3;
	temp3 = temp3 << 16;
	temp1 -= 1;

	temp3 = (temp3 & 0xffff0000) + (temp1 & 0xffff);

	tempax = (unsigned short) (temp3 & 0xff);
	xgifb_reg_set(pVBInfo->Part1Port, 0x1f, tempax);

	temp1 = pVBInfo->VGAVDE << 18;
	temp1 = temp1 / push3;
	tempbx = (unsigned short) (temp1 & 0xffff);

	if (pVBInfo->LCDResInfo == Panel_1024x768)
		tempbx -= 1;

	tempax = ((tempbx >> 8) & 0xff) << 3;
	tempax |= (unsigned short) ((temp3 >> 8) & 0x07);
	xgifb_reg_set(pVBInfo->Part1Port, 0x20,
			(unsigned short) (tempax & 0xff));
	xgifb_reg_set(pVBInfo->Part1Port, 0x21,
			(unsigned short) (tempbx & 0xff));

	temp3 = temp3 >> 16;

	if (modeflag & HalfDCLK)
		temp3 = temp3 >> 1;

	xgifb_reg_set(pVBInfo->Part1Port, 0x22,
			(unsigned short) ((temp3 >> 8) & 0xff));
	xgifb_reg_set(pVBInfo->Part1Port, 0x23,
			(unsigned short) (temp3 & 0xff));
}

/* --------------------------------------------------------------------- */
/* Function : XGI_GETLCDVCLKPtr */
/* Input : */
/* Output : al -> VCLK Index */
/* Description : */
/* --------------------------------------------------------------------- */
static void XGI_GetLCDVCLKPtr(unsigned char *di_0, unsigned char *di_1,
		struct vb_device_info *pVBInfo)
{
	unsigned short index;

	if (pVBInfo->VBInfo & (SetCRT2ToLCD | XGI_SetCRT2ToLCDA)) {
		index = XGI_GetLCDCapPtr1(pVBInfo);

		if (pVBInfo->VBInfo & SetCRT2ToLCD) { /* LCDB */
			*di_0 = pVBInfo->LCDCapList[index].LCUCHAR_VCLKData1;
			*di_1 = pVBInfo->LCDCapList[index].LCUCHAR_VCLKData2;
		} else { /* LCDA */
			*di_0 = pVBInfo->LCDCapList[index].LCDA_VCLKData1;
			*di_1 = pVBInfo->LCDCapList[index].LCDA_VCLKData2;
		}
	}
	return;
}

static unsigned char XGI_GetVCLKPtr(unsigned short RefreshRateTableIndex,
		unsigned short ModeNo, unsigned short ModeIdIndex,
		struct vb_device_info *pVBInfo)
{

	unsigned short index, modeflag;
	unsigned char tempal;

	/* si+Ext_ResInfo */
	modeflag = pVBInfo->EModeIDTable[ModeIdIndex].Ext_ModeFlag;

	if ((pVBInfo->SetFlag & ProgrammingCRT2) &&
	    (!(pVBInfo->LCDInfo & EnableScalingLCD))) { /* {LCDA/LCDB} */
		index = XGI_GetLCDCapPtr(pVBInfo);
		tempal = pVBInfo->LCDCapList[index].LCD_VCLK;

		if (pVBInfo->VBInfo & (SetCRT2ToLCD | XGI_SetCRT2ToLCDA))
			return tempal;

		/* {TV} */
		if (pVBInfo->VBType &
		    (VB_SIS301B |
		     VB_SIS302B |
		     VB_SIS301LV |
		     VB_SIS302LV |
		     VB_XGI301C)) {
			if (pVBInfo->VBInfo & SetCRT2ToHiVision) {
				tempal = TVCLKBASE_315 + HiTVVCLKDIV2;
				if (!(pVBInfo->TVInfo & RPLLDIV2XO))
					tempal = TVCLKBASE_315 + HiTVVCLK;
				if (pVBInfo->TVInfo & TVSimuMode) {
					tempal = TVCLKBASE_315 + HiTVSimuVCLK;
					if (!(modeflag & Charx8Dot))
						tempal = TVCLKBASE_315 +
								HiTVTextVCLK;

				}
				return tempal;
			}

			if (pVBInfo->TVInfo & TVSetYPbPr750p) {
				tempal = XGI_YPbPr750pVCLK;
				return tempal;
			}

			if (pVBInfo->TVInfo & TVSetYPbPr525p) {
				tempal = YPbPr525pVCLK;
				return tempal;
			}

			tempal = NTSC1024VCLK;

			if (!(pVBInfo->TVInfo & NTSC1024x768)) {
				tempal = TVCLKBASE_315 + TVVCLKDIV2;
				if (!(pVBInfo->TVInfo & RPLLDIV2XO))
					tempal = TVCLKBASE_315 + TVVCLK;
			}

			if (pVBInfo->VBInfo & SetCRT2ToTV)
				return tempal;
		}
	} /* {End of VB} */

	tempal = (unsigned char) inb((pVBInfo->P3ca + 0x02));
	tempal = tempal >> 2;
	tempal &= 0x03;

	/* for Dot8 Scaling LCD */
	if ((pVBInfo->LCDInfo & EnableScalingLCD) && (modeflag & Charx8Dot))
		tempal = tempal ^ tempal; /* ; set to VCLK25MHz always */

	tempal = pVBInfo->RefIndex[RefreshRateTableIndex].Ext_CRTVCLK;
	return tempal;
}

static void XGI_GetVCLKLen(unsigned char tempal, unsigned char *di_0,
		unsigned char *di_1, struct vb_device_info *pVBInfo)
{
	if (pVBInfo->VBType & (VB_SIS301 | VB_SIS301B | VB_SIS302B
			| VB_SIS301LV | VB_SIS302LV | VB_XGI301C)) {
		if ((!(pVBInfo->VBInfo & XGI_SetCRT2ToLCDA)) &&
		    (pVBInfo->SetFlag & ProgrammingCRT2)) {
			*di_0 = (unsigned char) XGI_VBVCLKData[tempal].SR2B;
			*di_1 = XGI_VBVCLKData[tempal].SR2C;
		}
	} else {
		*di_0 = XGI_VCLKData[tempal].SR2B;
		*di_1 = XGI_VCLKData[tempal].SR2C;
	}
}

static void XGI_SetCRT2ECLK(unsigned short ModeNo, unsigned short ModeIdIndex,
		unsigned short RefreshRateTableIndex,
		struct vb_device_info *pVBInfo)
{
	unsigned char di_0, di_1, tempal;
	int i;

	tempal = XGI_GetVCLKPtr(RefreshRateTableIndex, ModeNo, ModeIdIndex,
			pVBInfo);
	XGI_GetVCLKLen(tempal, &di_0, &di_1, pVBInfo);
	XGI_GetLCDVCLKPtr(&di_0, &di_1, pVBInfo);

	for (i = 0; i < 4; i++) {
		xgifb_reg_and_or(pVBInfo->P3d4, 0x31, ~0x30,
				(unsigned short) (0x10 * i));
		if ((!(pVBInfo->VBInfo & XGI_SetCRT2ToLCDA))
				&& (!(pVBInfo->VBInfo & SetInSlaveMode))) {
			xgifb_reg_set(pVBInfo->P3c4, 0x2e, di_0);
			xgifb_reg_set(pVBInfo->P3c4, 0x2f, di_1);
		} else {
			xgifb_reg_set(pVBInfo->P3c4, 0x2b, di_0);
			xgifb_reg_set(pVBInfo->P3c4, 0x2c, di_1);
		}
	}
}

static void XGI_UpdateModeInfo(struct xgi_hw_device_info *HwDeviceExtension,
		struct vb_device_info *pVBInfo)
{
	unsigned short tempcl, tempch, temp, tempbl, tempax;

	if (pVBInfo->VBType & (VB_SIS301B | VB_SIS302B | VB_SIS301LV
			| VB_SIS302LV | VB_XGI301C)) {
		tempcl = 0;
		tempch = 0;
		temp = xgifb_reg_get(pVBInfo->P3c4, 0x01);

		if (!(temp & 0x20)) {
			temp = xgifb_reg_get(pVBInfo->P3d4, 0x17);
			if (temp & 0x80) {
				temp = xgifb_reg_get(pVBInfo->P3d4, 0x53);
				if (!(temp & 0x40))
					tempcl |= ActiveCRT1;
			}
		}

		temp = xgifb_reg_get(pVBInfo->Part1Port, 0x2e);
		temp &= 0x0f;

		if (!(temp == 0x08)) {
			/* Check ChannelA */
			tempax = xgifb_reg_get(pVBInfo->Part1Port, 0x13);
			if (tempax & 0x04)
				tempcl = tempcl | ActiveLCD;

			temp &= 0x05;

			if (!(tempcl & ActiveLCD))
				if (temp == 0x01)
					tempcl |= ActiveCRT2;

			if (temp == 0x04)
				tempcl |= ActiveLCD;

			if (temp == 0x05) {
				temp = xgifb_reg_get(pVBInfo->Part2Port, 0x00);

				if (!(temp & 0x08))
					tempch |= ActiveAVideo;

				if (!(temp & 0x04))
					tempch |= ActiveSVideo;

				if (temp & 0x02)
					tempch |= ActiveSCART;

				if (pVBInfo->VBInfo & SetCRT2ToHiVision) {
					if (temp & 0x01)
						tempch |= ActiveHiTV;
				}

				if (pVBInfo->VBInfo & SetCRT2ToYPbPr525750) {
					temp = xgifb_reg_get(
							pVBInfo->Part2Port,
							0x4d);

					if (temp & 0x10)
						tempch |= ActiveYPbPr;
				}

				if (tempch != 0)
					tempcl |= ActiveTV;
			}
		}

		temp = xgifb_reg_get(pVBInfo->P3d4, 0x3d);
		if (tempcl & ActiveLCD) {
			if ((pVBInfo->SetFlag & ReserveTVOption)) {
				if (temp & ActiveTV)
					tempcl |= ActiveTV;
			}
		}
		temp = tempcl;
		tempbl = ~XGI_ModeSwitchStatus;
		xgifb_reg_and_or(pVBInfo->P3d4, 0x3d, tempbl, temp);

		if (!(pVBInfo->SetFlag & ReserveTVOption))
			xgifb_reg_set(pVBInfo->P3d4, 0x3e, tempch);
	} else {
		return;
	}
}

void XGI_GetVBType(struct vb_device_info *pVBInfo)
{
	unsigned short flag, tempbx, tempah;

	if (pVBInfo->IF_DEF_LVDS != 0)
		return;

	tempbx = VB_SIS302B;
	flag = xgifb_reg_get(pVBInfo->Part4Port, 0x00);
	if (flag == 0x02)
		goto finish;

	tempbx = VB_SIS301;
	flag = xgifb_reg_get(pVBInfo->Part4Port, 0x01);
	if (flag < 0xB0)
		goto finish;

	tempbx = VB_SIS301B;
	if (flag < 0xC0)
		goto bigger_than_0xB0;

	tempbx = VB_XGI301C;
	if (flag < 0xD0)
		goto bigger_than_0xB0;

	tempbx = VB_SIS301LV;
	if (flag < 0xE0)
		goto bigger_than_0xB0;

	tempbx = VB_SIS302LV;
	tempah = xgifb_reg_get(pVBInfo->Part4Port, 0x39);
	if (tempah != 0xFF)
		tempbx = VB_XGI301C;

bigger_than_0xB0:
	if (tempbx & (VB_SIS301B | VB_SIS302B)) {
		flag = xgifb_reg_get(pVBInfo->Part4Port, 0x23);
		if (!(flag & 0x02))
			tempbx = tempbx | VB_NoLCD;
	}

finish:
	pVBInfo->VBType = tempbx;
}

static void XGI_GetVBInfo(unsigned short ModeNo, unsigned short ModeIdIndex,
		struct xgi_hw_device_info *HwDeviceExtension,
		struct vb_device_info *pVBInfo)
{
	unsigned short tempax, push, tempbx, temp, modeflag;

	modeflag = pVBInfo->EModeIDTable[ModeIdIndex].Ext_ModeFlag;
	pVBInfo->SetFlag = 0;
	pVBInfo->ModeType = modeflag & ModeTypeMask;
	tempbx = 0;

	if (!(pVBInfo->VBType & 0xFFFF))
		return;

	/* Check Display Device */
	temp = xgifb_reg_get(pVBInfo->P3d4, 0x30);
	tempbx = tempbx | temp;
	temp = xgifb_reg_get(pVBInfo->P3d4, 0x31);
	push = temp;
	push = push << 8;
	tempax = temp << 8;
	tempbx = tempbx | tempax;
	temp = (SetCRT2ToDualEdge | SetCRT2ToYPbPr525750 | XGI_SetCRT2ToLCDA
		| SetInSlaveMode | DisableCRT2Display);
	temp = 0xFFFF ^ temp;
	tempbx &= temp;

	temp = xgifb_reg_get(pVBInfo->P3d4, 0x38);

	if (pVBInfo->IF_DEF_LVDS == 0) {
		if (pVBInfo->VBType &
		    (VB_SIS302B |
		     VB_SIS301LV |
		     VB_SIS302LV |
		     VB_XGI301C)) {
			if (temp & EnableDualEdge) {
				tempbx |= SetCRT2ToDualEdge;
				if (temp & SetToLCDA)
					tempbx |= XGI_SetCRT2ToLCDA;
			}
		}
	}

	if (pVBInfo->IF_DEF_YPbPr == 1) {
		if (((pVBInfo->IF_DEF_LVDS == 0) &&
		     ((pVBInfo->VBType & VB_SIS301LV) ||
		      (pVBInfo->VBType & VB_SIS302LV) ||
		      (pVBInfo->VBType & VB_XGI301C)))) {
			if (temp & SetYPbPr) {
				if (pVBInfo->IF_DEF_HiVision == 1) {
					/* shampoo add for new
					 * scratch */
					temp = xgifb_reg_get(
						pVBInfo->P3d4,
						0x35);
					temp &= YPbPrMode;
					tempbx |= SetCRT2ToHiVision;

					if (temp != YPbPrMode1080i) {
						tempbx &=
							(~SetCRT2ToHiVision);
						tempbx |=
							SetCRT2ToYPbPr525750;
					}
				}
			}
		}
	}

	tempax = push; /* restore CR31 */

	if (pVBInfo->IF_DEF_LVDS == 0) {
		if (pVBInfo->IF_DEF_YPbPr == 1) {
			if (pVBInfo->IF_DEF_HiVision == 1)
				temp = 0x09FC;
			else
				temp = 0x097C;
		} else {
			if (pVBInfo->IF_DEF_HiVision == 1)
				temp = 0x01FC;
			else
				temp = 0x017C;
		}
	} else { /* 3nd party chip */
		temp = SetCRT2ToLCD;
	}

	if (!(tempbx & temp)) {
		tempax |= DisableCRT2Display;
		tempbx = 0;
	}

	if (!(pVBInfo->VBType & VB_NoLCD)) {
		if (tempbx & XGI_SetCRT2ToLCDA) {
			if (tempbx & SetSimuScanMode)
				tempbx &= (~(SetCRT2ToLCD |
					     SetCRT2ToRAMDAC |
					     SwitchCRT2));
			else
				tempbx &= (~(SetCRT2ToLCD |
					     SetCRT2ToRAMDAC |
					     SetCRT2ToTV |
					     SwitchCRT2));
		}
	}

	/* shampoo add */
	/* for driver abnormal */
	if (!(tempbx & (SwitchCRT2 | SetSimuScanMode))) {
		if (pVBInfo->IF_DEF_CRT2Monitor == 1) {
			if (tempbx & SetCRT2ToRAMDAC) {
				tempbx &= (0xFF00 |
					   SetCRT2ToRAMDAC |
					   SwitchCRT2 |
					   SetSimuScanMode);
				tempbx &= (0x00FF | (~SetCRT2ToYPbPr525750));
			}
		} else {
			tempbx &= (~(SetCRT2ToRAMDAC |
				     SetCRT2ToLCD |
				     SetCRT2ToTV));
		}
	}

	if (!(pVBInfo->VBType & VB_NoLCD)) {
		if (tempbx & SetCRT2ToLCD) {
			tempbx &= (0xFF00 |
				   SetCRT2ToLCD |
				   SwitchCRT2 |
				   SetSimuScanMode);
			tempbx &= (0x00FF | (~SetCRT2ToYPbPr525750));
		}
	}

	if (tempbx & SetCRT2ToSCART) {
		tempbx &= (0xFF00 |
			   SetCRT2ToSCART |
			   SwitchCRT2 |
			   SetSimuScanMode);
		tempbx &= (0x00FF | (~SetCRT2ToYPbPr525750));
	}

	if (pVBInfo->IF_DEF_YPbPr == 1) {
		if (tempbx & SetCRT2ToYPbPr525750)
			tempbx &= (0xFF00 |
				   SwitchCRT2 |
				   SetSimuScanMode);
	}

	if (pVBInfo->IF_DEF_HiVision == 1) {
		if (tempbx & SetCRT2ToHiVision)
			tempbx &= (0xFF00 |
				   SetCRT2ToHiVision |
				   SwitchCRT2 |
				   SetSimuScanMode);
	}

	if (tempax & DisableCRT2Display) { /* Set Display Device Info */
		if (!(tempbx & (SwitchCRT2 | SetSimuScanMode)))
			tempbx = DisableCRT2Display;
	}

	if (!(tempbx & DisableCRT2Display)) {
		if ((!(tempbx & DriverMode)) ||
		    (!(modeflag & CRT2Mode))) {
			if (!(tempbx & XGI_SetCRT2ToLCDA))
				tempbx |= (SetInSlaveMode |
					   SetSimuScanMode);
		}

		/* LCD+TV can't support in slave mode
		 * (Force LCDA+TV->LCDB) */
		if ((tempbx & SetInSlaveMode) &&
		    (tempbx & XGI_SetCRT2ToLCDA)) {
			tempbx ^= (SetCRT2ToLCD |
				   XGI_SetCRT2ToLCDA |
				   SetCRT2ToDualEdge);
			pVBInfo->SetFlag |= ReserveTVOption;
		}
	}

	pVBInfo->VBInfo = tempbx;
}

static void XGI_GetTVInfo(unsigned short ModeNo, unsigned short ModeIdIndex,
		struct vb_device_info *pVBInfo)
{
	unsigned short temp, tempbx = 0, resinfo = 0, modeflag, index1;

	tempbx = 0;
	resinfo = 0;

	if (pVBInfo->VBInfo & SetCRT2ToTV) {
		modeflag = pVBInfo->EModeIDTable[ModeIdIndex].Ext_ModeFlag;
		resinfo = pVBInfo->EModeIDTable[ModeIdIndex].Ext_RESINFO;

		if (pVBInfo->VBInfo & SetCRT2ToTV) {
			temp = xgifb_reg_get(pVBInfo->P3d4, 0x35);
			tempbx = temp;
			if (tempbx & TVSetPAL) {
				tempbx &= (SetCHTVOverScan |
					   TVSetPALM |
					   TVSetPALN |
					   TVSetPAL);
				if (tempbx & TVSetPALM)
					/* set to NTSC if PAL-M */
					tempbx &= ~TVSetPAL;
			} else
				tempbx &= (SetCHTVOverScan |
					   TVSetNTSCJ |
					   TVSetPAL);
		}

		if (pVBInfo->IF_DEF_LVDS == 0) {
			if (pVBInfo->VBInfo & SetCRT2ToSCART)
				tempbx |= TVSetPAL;
		}

		if (pVBInfo->IF_DEF_YPbPr == 1) {
			if (pVBInfo->VBInfo & SetCRT2ToYPbPr525750) {
				index1 = xgifb_reg_get(pVBInfo->P3d4, 0x35);
				index1 &= YPbPrMode;

				if (index1 == YPbPrMode525i)
					tempbx |= TVSetYPbPr525i;

				if (index1 == YPbPrMode525p)
					tempbx = tempbx | TVSetYPbPr525p;
				if (index1 == YPbPrMode750p)
					tempbx = tempbx | TVSetYPbPr750p;
			}
		}

		if (pVBInfo->IF_DEF_HiVision == 1) {
			if (pVBInfo->VBInfo & SetCRT2ToHiVision)
				tempbx = tempbx | TVSetHiVision | TVSetPAL;
		}

		if (pVBInfo->IF_DEF_LVDS == 0) { /* shampoo */
			if ((pVBInfo->VBInfo & SetInSlaveMode) &&
			    (!(pVBInfo->VBInfo & SetNotSimuMode)))
				tempbx |= TVSimuMode;

			if (!(tempbx & TVSetPAL) &&
			    (modeflag > 13) &&
			    (resinfo == 8)) /* NTSC 1024x768, */
				tempbx |= NTSC1024x768;

			tempbx |= RPLLDIV2XO;

			if (pVBInfo->VBInfo & SetCRT2ToHiVision) {
				if (pVBInfo->VBInfo & SetInSlaveMode)
					tempbx &= (~RPLLDIV2XO);
			} else {
				if (tempbx &
				    (TVSetYPbPr525p | TVSetYPbPr750p))
					tempbx &= (~RPLLDIV2XO);
				else if (!(pVBInfo->VBType &
					 (VB_SIS301B |
					  VB_SIS302B |
					  VB_SIS301LV |
					  VB_SIS302LV |
					  VB_XGI301C))) {
					if (tempbx & TVSimuMode)
						tempbx &= (~RPLLDIV2XO);
				}
			}
		}
	}
	pVBInfo->TVInfo = tempbx;
}

static unsigned char XGI_GetLCDInfo(unsigned short ModeNo,
		unsigned short ModeIdIndex, struct vb_device_info *pVBInfo)
{
	unsigned short temp, tempax, tempbx, modeflag, resinfo = 0, LCDIdIndex;

	pVBInfo->LCDResInfo = 0;
	pVBInfo->LCDTypeInfo = 0;
	pVBInfo->LCDInfo = 0;

	modeflag = pVBInfo->EModeIDTable[ModeIdIndex].Ext_ModeFlag;
	/* si+Ext_ResInfo // */
	resinfo = pVBInfo->EModeIDTable[ModeIdIndex].Ext_RESINFO;
	temp = xgifb_reg_get(pVBInfo->P3d4, 0x36); /* Get LCD Res.Info */
	tempbx = temp & 0x0F;

	if (tempbx == 0)
		tempbx = Panel_1024x768; /* default */

	/* LCD75 */
	if ((tempbx == Panel_1024x768) || (tempbx == Panel_1280x1024)) {
		if (pVBInfo->VBInfo & DriverMode) {
			tempax = xgifb_reg_get(pVBInfo->P3d4, 0x33);
			if (pVBInfo->VBInfo & XGI_SetCRT2ToLCDA)
				tempax &= 0x0F;
			else
				tempax = tempax >> 4;

			if ((resinfo == 6) || (resinfo == 9)) {
				if (tempax >= 3)
					tempbx |= PanelRef75Hz;
			} else if ((resinfo == 7) || (resinfo == 8)) {
				if (tempax >= 4)
					tempbx |= PanelRef75Hz;
			}
		}
	}

	pVBInfo->LCDResInfo = tempbx;

	/* End of LCD75 */

	if (!(pVBInfo->VBInfo & (SetCRT2ToLCD | XGI_SetCRT2ToLCDA)))
		return 0;

	tempbx = 0;

	temp = xgifb_reg_get(pVBInfo->P3d4, 0x37);

	temp &= (ScalingLCD | LCDNonExpanding | LCDSyncBit | SetPWDEnable);

	tempbx |= temp;

	LCDIdIndex = XGI_GetLCDCapPtr1(pVBInfo);

	tempax = pVBInfo->LCDCapList[LCDIdIndex].LCD_Capability;

	if (pVBInfo->IF_DEF_LVDS == 0) { /* shampoo */
		if (((pVBInfo->VBType & VB_SIS302LV) || (pVBInfo->VBType
				& VB_XGI301C)) && (tempax & XGI_LCDDualLink)) {
			tempbx |= SetLCDDualLink;
		}
	}

	if (pVBInfo->IF_DEF_LVDS == 0) {
		if ((pVBInfo->LCDResInfo == Panel_1400x1050) && (pVBInfo->VBInfo
				& SetCRT2ToLCD) && (resinfo == 9) &&
				(!(tempbx & EnableScalingLCD)))
			/*
			 * set to center in 1280x1024 LCDB
			 * for Panel_1400x1050
			 */
			tempbx |= SetLCDtoNonExpanding;
	}

	if (pVBInfo->VBInfo & SetInSlaveMode) {
		if (pVBInfo->VBInfo & SetNotSimuMode)
			tempbx |= XGI_LCDVESATiming;
	} else {
		tempbx |= XGI_LCDVESATiming;
	}

	pVBInfo->LCDInfo = tempbx;

	return 1;
}

unsigned char XGI_SearchModeID(unsigned short ModeNo,
		unsigned short *ModeIdIndex, struct vb_device_info *pVBInfo)
{
	for (*ModeIdIndex = 0;; (*ModeIdIndex)++) {
		if (pVBInfo->EModeIDTable[*ModeIdIndex].Ext_ModeID == ModeNo)
			break;
		if (pVBInfo->EModeIDTable[*ModeIdIndex].Ext_ModeID == 0xFF)
			return 0;
	}

	return 1;
}

static unsigned char XG21GPIODataTransfer(unsigned char ujDate)
{
	unsigned char ujRet = 0;
	unsigned char i = 0;

	for (i = 0; i < 8; i++) {
		ujRet = ujRet << 1;
		ujRet |= (ujDate >> i) & 1;
	}

	return ujRet;
}

/*----------------------------------------------------------------------------*/
/* output                                                                     */
/*      bl[5] : LVDS signal                                                   */
/*      bl[1] : LVDS backlight                                                */
/*      bl[0] : LVDS VDD                                                      */
/*----------------------------------------------------------------------------*/
static unsigned char XGI_XG21GetPSCValue(struct vb_device_info *pVBInfo)
{
	unsigned char CR4A, temp;

	CR4A = xgifb_reg_get(pVBInfo->P3d4, 0x4A);
	xgifb_reg_and(pVBInfo->P3d4, 0x4A, ~0x23); /* enable GPIO write */

	temp = xgifb_reg_get(pVBInfo->P3d4, 0x48);

	temp = XG21GPIODataTransfer(temp);
	temp &= 0x23;
	xgifb_reg_set(pVBInfo->P3d4, 0x4A, CR4A);
	return temp;
}

/*----------------------------------------------------------------------------*/
/* output                                                                     */
/*      bl[5] : LVDS signal                                                   */
/*      bl[1] : LVDS backlight                                                */
/*      bl[0] : LVDS VDD                                                      */
/*----------------------------------------------------------------------------*/
static unsigned char XGI_XG27GetPSCValue(struct vb_device_info *pVBInfo)
{
	unsigned char CR4A, CRB4, temp;

	CR4A = xgifb_reg_get(pVBInfo->P3d4, 0x4A);
	xgifb_reg_and(pVBInfo->P3d4, 0x4A, ~0x0C); /* enable GPIO write */

	temp = xgifb_reg_get(pVBInfo->P3d4, 0x48);

	temp &= 0x0C;
	temp >>= 2;
	xgifb_reg_set(pVBInfo->P3d4, 0x4A, CR4A);
	CRB4 = xgifb_reg_get(pVBInfo->P3d4, 0xB4);
	temp |= ((CRB4 & 0x04) << 3);
	return temp;
}

/*----------------------------------------------------------------------------*/
/* input                                                                      */
/*      bl[5] : 1;LVDS signal on                                              */
/*      bl[1] : 1;LVDS backlight on                                           */
/*      bl[0] : 1:LVDS VDD on                                                 */
/*      bh: 100000b : clear bit 5, to set bit5                                */
/*          000010b : clear bit 1, to set bit1                                */
/*          000001b : clear bit 0, to set bit0                                */
/*----------------------------------------------------------------------------*/
static void XGI_XG21BLSignalVDD(unsigned short tempbh, unsigned short tempbl,
		struct vb_device_info *pVBInfo)
{
	unsigned char CR4A, temp;

	CR4A = xgifb_reg_get(pVBInfo->P3d4, 0x4A);
	tempbh &= 0x23;
	tempbl &= 0x23;
	xgifb_reg_and(pVBInfo->P3d4, 0x4A, ~tempbh); /* enable GPIO write */

	if (tempbh & 0x20) {
		temp = (tempbl >> 4) & 0x02;

		/* CR B4[1] */
		xgifb_reg_and_or(pVBInfo->P3d4, 0xB4, ~0x02, temp);

	}

	temp = xgifb_reg_get(pVBInfo->P3d4, 0x48);

	temp = XG21GPIODataTransfer(temp);
	temp &= ~tempbh;
	temp |= tempbl;
	xgifb_reg_set(pVBInfo->P3d4, 0x48, temp);
}

static void XGI_XG27BLSignalVDD(unsigned short tempbh, unsigned short tempbl,
		struct vb_device_info *pVBInfo)
{
	unsigned char CR4A, temp;
	unsigned short tempbh0, tempbl0;

	tempbh0 = tempbh;
	tempbl0 = tempbl;
	tempbh0 &= 0x20;
	tempbl0 &= 0x20;
	tempbh0 >>= 3;
	tempbl0 >>= 3;

	if (tempbh & 0x20) {
		temp = (tempbl >> 4) & 0x02;

		/* CR B4[1] */
		xgifb_reg_and_or(pVBInfo->P3d4, 0xB4, ~0x02, temp);

	}
	xgifb_reg_and_or(pVBInfo->P3d4, 0xB4, ~tempbh0, tempbl0);

	CR4A = xgifb_reg_get(pVBInfo->P3d4, 0x4A);
	tempbh &= 0x03;
	tempbl &= 0x03;
	tempbh <<= 2;
	tempbl <<= 2; /* GPIOC,GPIOD */
	xgifb_reg_and(pVBInfo->P3d4, 0x4A, ~tempbh); /* enable GPIO write */
	xgifb_reg_and_or(pVBInfo->P3d4, 0x48, ~tempbh, tempbl);
}

static void XGI_DisplayOn(struct xgifb_video_info *xgifb_info,
		struct xgi_hw_device_info *pXGIHWDE,
		struct vb_device_info *pVBInfo)
{

	xgifb_reg_and_or(pVBInfo->P3c4, 0x01, 0xDF, 0x00);
	if (pXGIHWDE->jChipType == XG21) {
		if (pVBInfo->IF_DEF_LVDS == 1) {
			if (!(XGI_XG21GetPSCValue(pVBInfo) & 0x1)) {
				/* LVDS VDD on */
				XGI_XG21BLSignalVDD(0x01, 0x01, pVBInfo);
				mdelay(xgifb_info->lvds_data.PSC_S2);
			}
			if (!(XGI_XG21GetPSCValue(pVBInfo) & 0x20))
				/* LVDS signal on */
				XGI_XG21BLSignalVDD(0x20, 0x20, pVBInfo);
			mdelay(xgifb_info->lvds_data.PSC_S3);
			/* LVDS backlight on */
			XGI_XG21BLSignalVDD(0x02, 0x02, pVBInfo);
		} else {
			/* DVO/DVI signal on */
			XGI_XG21BLSignalVDD(0x20, 0x20, pVBInfo);
		}

	}

	if (pXGIHWDE->jChipType == XG27) {
		if (pVBInfo->IF_DEF_LVDS == 1) {
			if (!(XGI_XG27GetPSCValue(pVBInfo) & 0x1)) {
				/* LVDS VDD on */
				XGI_XG27BLSignalVDD(0x01, 0x01, pVBInfo);
				mdelay(xgifb_info->lvds_data.PSC_S2);
			}
			if (!(XGI_XG27GetPSCValue(pVBInfo) & 0x20))
				/* LVDS signal on */
				XGI_XG27BLSignalVDD(0x20, 0x20, pVBInfo);
			mdelay(xgifb_info->lvds_data.PSC_S3);
			/* LVDS backlight on */
			XGI_XG27BLSignalVDD(0x02, 0x02, pVBInfo);
		} else {
			/* DVO/DVI signal on */
			XGI_XG27BLSignalVDD(0x20, 0x20, pVBInfo);
		}

	}
}

void XGI_DisplayOff(struct xgifb_video_info *xgifb_info,
		struct xgi_hw_device_info *pXGIHWDE,
		struct vb_device_info *pVBInfo)
{

	if (pXGIHWDE->jChipType == XG21) {
		if (pVBInfo->IF_DEF_LVDS == 1) {
			/* LVDS backlight off */
			XGI_XG21BLSignalVDD(0x02, 0x00, pVBInfo);
			mdelay(xgifb_info->lvds_data.PSC_S3);
		} else {
			/* DVO/DVI signal off */
			XGI_XG21BLSignalVDD(0x20, 0x00, pVBInfo);
		}
	}

	if (pXGIHWDE->jChipType == XG27) {
		if ((XGI_XG27GetPSCValue(pVBInfo) & 0x2)) {
			/* LVDS backlight off */
			XGI_XG27BLSignalVDD(0x02, 0x00, pVBInfo);
			mdelay(xgifb_info->lvds_data.PSC_S3);
		}

		if (pVBInfo->IF_DEF_LVDS == 0)
			/* DVO/DVI signal off */
			XGI_XG27BLSignalVDD(0x20, 0x00, pVBInfo);
	}

	xgifb_reg_and_or(pVBInfo->P3c4, 0x01, 0xDF, 0x20);
}

static void XGI_WaitDisply(struct vb_device_info *pVBInfo)
{
	while ((inb(pVBInfo->P3da) & 0x01))
		break;

	while (!(inb(pVBInfo->P3da) & 0x01))
		break;
}

static void XGI_AutoThreshold(struct vb_device_info *pVBInfo)
{
	xgifb_reg_or(pVBInfo->Part1Port, 0x01, 0x40);
}

static void XGI_SaveCRT2Info(unsigned short ModeNo,
			     struct vb_device_info *pVBInfo)
{
	unsigned short temp1, temp2;

	/* reserve CR34 for CRT1 Mode No */
	xgifb_reg_set(pVBInfo->P3d4, 0x34, ModeNo);
	temp1 = (pVBInfo->VBInfo & SetInSlaveMode) >> 8;
	temp2 = ~(SetInSlaveMode >> 8);
	xgifb_reg_and_or(pVBInfo->P3d4, 0x31, temp2, temp1);
}

static void XGI_GetCRT2ResInfo(unsigned short ModeNo,
			       unsigned short ModeIdIndex,
			       struct vb_device_info *pVBInfo)
{
	unsigned short xres, yres, modeflag, resindex;

	resindex = pVBInfo->EModeIDTable[ModeIdIndex].Ext_RESINFO;
	xres = pVBInfo->ModeResInfo[resindex].HTotal; /* xres->ax */
	yres = pVBInfo->ModeResInfo[resindex].VTotal; /* yres->bx */
	/* si+St_ModeFlag */
	modeflag = pVBInfo->EModeIDTable[ModeIdIndex].Ext_ModeFlag;

	if (modeflag & HalfDCLK)
		xres *= 2;

	if (modeflag & DoubleScanMode)
		yres *= 2;

	if (!(pVBInfo->VBInfo & SetCRT2ToLCD))
		goto exit;

	if (pVBInfo->IF_DEF_LVDS == 0) {
		if (pVBInfo->LCDResInfo == Panel_1600x1200) {
			if (!(pVBInfo->LCDInfo & XGI_LCDVESATiming)) {
				if (yres == 1024)
					yres = 1056;
			}
		}

		if (pVBInfo->LCDResInfo == Panel_1280x1024) {
			if (yres == 400)
				yres = 405;
			else if (yres == 350)
				yres = 360;

			if (pVBInfo->LCDInfo & XGI_LCDVESATiming) {
				if (yres == 360)
					yres = 375;
			}
		}

		if (pVBInfo->LCDResInfo == Panel_1024x768) {
			if (!(pVBInfo->LCDInfo & XGI_LCDVESATiming)) {
				if (!(pVBInfo->LCDInfo & LCDNonExpanding)) {
					if (yres == 350)
						yres = 357;
					else if (yres == 400)
						yres = 420;
					else if (yres == 480)
						yres = 525;
				}
			}
		}
	}

	if (xres == 720)
		xres = 640;

exit:
	pVBInfo->VGAHDE = xres;
	pVBInfo->HDE = xres;
	pVBInfo->VGAVDE = yres;
	pVBInfo->VDE = yres;
}

static unsigned char XGI_IsLCDDualLink(struct vb_device_info *pVBInfo)
{

	if ((pVBInfo->VBInfo & (SetCRT2ToLCD | XGI_SetCRT2ToLCDA)) &&
			(pVBInfo->LCDInfo & SetLCDDualLink)) /* shampoo0129 */
		return 1;

	return 0;
}

static void XGI_GetRAMDAC2DATA(unsigned short ModeNo,
			       unsigned short ModeIdIndex,
			       unsigned short RefreshRateTableIndex,
			       struct vb_device_info *pVBInfo)
{
	unsigned short tempax, tempbx, temp1, temp2, modeflag = 0, tempcx,
			CRT1Index;

	pVBInfo->RVBHCMAX = 1;
	pVBInfo->RVBHCFACT = 1;
	modeflag = pVBInfo->EModeIDTable[ModeIdIndex].Ext_ModeFlag;
	CRT1Index = pVBInfo->RefIndex[RefreshRateTableIndex].Ext_CRT1CRTC;
	CRT1Index &= IndexMask;
	temp1 = (unsigned short) pVBInfo->XGINEWUB_CRT1Table[CRT1Index].CR[0];
	temp2 = (unsigned short) pVBInfo->XGINEWUB_CRT1Table[CRT1Index].CR[5];
	tempax = (temp1 & 0xFF) | ((temp2 & 0x03) << 8);
	tempbx = (unsigned short) pVBInfo->XGINEWUB_CRT1Table[CRT1Index].CR[8];
	tempcx = (unsigned short)
			pVBInfo->XGINEWUB_CRT1Table[CRT1Index].CR[14] << 8;
	tempcx &= 0x0100;
	tempcx = tempcx << 2;
	tempbx |= tempcx;
	temp1 = (unsigned short) pVBInfo->XGINEWUB_CRT1Table[CRT1Index].CR[9];

	if (temp1 & 0x01)
		tempbx |= 0x0100;

	if (temp1 & 0x20)
		tempbx |= 0x0200;
	tempax += 5;

	if (modeflag & Charx8Dot)
		tempax *= 8;
	else
		tempax *= 9;

	pVBInfo->VGAHT = tempax;
	pVBInfo->HT = tempax;
	tempbx++;
	pVBInfo->VGAVT = tempbx;
	pVBInfo->VT = tempbx;
}

static void XGI_GetCRT2Data(unsigned short ModeNo, unsigned short ModeIdIndex,
		unsigned short RefreshRateTableIndex,
		struct vb_device_info *pVBInfo)
{
	unsigned short tempax = 0, tempbx, modeflag, resinfo;

	struct SiS_LCDData *LCDPtr = NULL;

	/* si+Ext_ResInfo */
	modeflag = pVBInfo->EModeIDTable[ModeIdIndex].Ext_ModeFlag;
	resinfo = pVBInfo->EModeIDTable[ModeIdIndex].Ext_RESINFO;
	pVBInfo->NewFlickerMode = 0;
	pVBInfo->RVBHRS = 50;

	if (pVBInfo->VBInfo & SetCRT2ToRAMDAC) {
		XGI_GetRAMDAC2DATA(ModeNo, ModeIdIndex, RefreshRateTableIndex,
				pVBInfo);
		return;
	}

	tempbx = 4;

	if (pVBInfo->VBInfo & (SetCRT2ToLCD | XGI_SetCRT2ToLCDA)) {
		LCDPtr = (struct SiS_LCDData *) XGI_GetLcdPtr(tempbx,
				ModeNo, ModeIdIndex, RefreshRateTableIndex,
				pVBInfo);

		pVBInfo->RVBHCMAX = LCDPtr->RVBHCMAX;
		pVBInfo->RVBHCFACT = LCDPtr->RVBHCFACT;
		pVBInfo->VGAHT = LCDPtr->VGAHT;
		pVBInfo->VGAVT = LCDPtr->VGAVT;
		pVBInfo->HT = LCDPtr->LCDHT;
		pVBInfo->VT = LCDPtr->LCDVT;

		if (pVBInfo->LCDResInfo == Panel_1024x768) {
			tempax = 1024;
			tempbx = 768;

			if (!(pVBInfo->LCDInfo & XGI_LCDVESATiming)) {
				if (pVBInfo->VGAVDE == 357)
					tempbx = 527;
				else if (pVBInfo->VGAVDE == 420)
					tempbx = 620;
				else if (pVBInfo->VGAVDE == 525)
					tempbx = 775;
				else if (pVBInfo->VGAVDE == 600)
					tempbx = 775;
				else
					tempbx = 768;
			} else
				tempbx = 768;
		} else if (pVBInfo->LCDResInfo == Panel_1024x768x75) {
			tempax = 1024;
			tempbx = 768;
		} else if (pVBInfo->LCDResInfo == Panel_1280x1024) {
			tempax = 1280;
			if (pVBInfo->VGAVDE == 360)
				tempbx = 768;
			else if (pVBInfo->VGAVDE == 375)
				tempbx = 800;
			else if (pVBInfo->VGAVDE == 405)
				tempbx = 864;
			else
				tempbx = 1024;
		} else if (pVBInfo->LCDResInfo == Panel_1280x1024x75) {
			tempax = 1280;
			tempbx = 1024;
		} else if (pVBInfo->LCDResInfo == Panel_1280x960) {
			tempax = 1280;
			if (pVBInfo->VGAVDE == 350)
				tempbx = 700;
			else if (pVBInfo->VGAVDE == 400)
				tempbx = 800;
			else if (pVBInfo->VGAVDE == 1024)
				tempbx = 960;
			else
				tempbx = 960;
		} else if (pVBInfo->LCDResInfo == Panel_1400x1050) {
			tempax = 1400;
			tempbx = 1050;

			if (pVBInfo->VGAVDE == 1024) {
				tempax = 1280;
				tempbx = 1024;
			}
		} else if (pVBInfo->LCDResInfo == Panel_1600x1200) {
			tempax = 1600;
			tempbx = 1200; /* alan 10/14/2003 */
			if (!(pVBInfo->LCDInfo & XGI_LCDVESATiming)) {
				if (pVBInfo->VGAVDE == 350)
					tempbx = 875;
				else if (pVBInfo->VGAVDE == 400)
					tempbx = 1000;
			}
		}

		if (pVBInfo->LCDInfo & LCDNonExpanding) {
			tempax = pVBInfo->VGAHDE;
			tempbx = pVBInfo->VGAVDE;
		}

		pVBInfo->HDE = tempax;
		pVBInfo->VDE = tempbx;
		return;
	}

	if (pVBInfo->VBInfo & (SetCRT2ToTV)) {
		struct SiS_TVData const *TVPtr;

		TVPtr = XGI_GetTVPtr(ModeNo, ModeIdIndex, RefreshRateTableIndex,
				     pVBInfo);

		pVBInfo->RVBHCMAX = TVPtr->RVBHCMAX;
		pVBInfo->RVBHCFACT = TVPtr->RVBHCFACT;
		pVBInfo->VGAHT = TVPtr->VGAHT;
		pVBInfo->VGAVT = TVPtr->VGAVT;
		pVBInfo->HDE = TVPtr->TVHDE;
		pVBInfo->VDE = TVPtr->TVVDE;
		pVBInfo->RVBHRS = TVPtr->RVBHRS;
		pVBInfo->NewFlickerMode = TVPtr->FlickerMode;

		if (pVBInfo->VBInfo & SetCRT2ToHiVision) {
			if (resinfo == 0x08)
				pVBInfo->NewFlickerMode = 0x40;
			else if (resinfo == 0x09)
				pVBInfo->NewFlickerMode = 0x40;
			else if (resinfo == 0x12)
				pVBInfo->NewFlickerMode = 0x40;

			if (pVBInfo->VGAVDE == 350)
				pVBInfo->TVInfo |= TVSimuMode;

			tempax = ExtHiTVHT;
			tempbx = ExtHiTVVT;

			if (pVBInfo->VBInfo & SetInSlaveMode) {
				if (pVBInfo->TVInfo & TVSimuMode) {
					tempax = StHiTVHT;
					tempbx = StHiTVVT;

					if (!(modeflag & Charx8Dot)) {
						tempax = StHiTextTVHT;
						tempbx = StHiTextTVVT;
					}
				}
			}
		} else if (pVBInfo->VBInfo & SetCRT2ToYPbPr525750) {
			if (pVBInfo->TVInfo & TVSetYPbPr750p) {
				tempax = YPbPrTV750pHT; /* Ext750pTVHT */
				tempbx = YPbPrTV750pVT; /* Ext750pTVVT */
			}

			if (pVBInfo->TVInfo & TVSetYPbPr525p) {
				tempax = YPbPrTV525pHT; /* Ext525pTVHT */
				tempbx = YPbPrTV525pVT; /* Ext525pTVVT */
			} else if (pVBInfo->TVInfo & TVSetYPbPr525i) {
				tempax = YPbPrTV525iHT; /* Ext525iTVHT */
				tempbx = YPbPrTV525iVT; /* Ext525iTVVT */
				if (pVBInfo->TVInfo & NTSC1024x768)
					tempax = NTSC1024x768HT;
			}
		} else {
			tempax = PALHT;
			tempbx = PALVT;
			if (!(pVBInfo->TVInfo & TVSetPAL)) {
				tempax = NTSCHT;
				tempbx = NTSCVT;
				if (pVBInfo->TVInfo & NTSC1024x768)
					tempax = NTSC1024x768HT;
			}
		}

		pVBInfo->HT = tempax;
		pVBInfo->VT = tempbx;
		return;
	}
}

static void XGI_SetCRT2VCLK(unsigned short ModeNo, unsigned short ModeIdIndex,
		unsigned short RefreshRateTableIndex,
		struct vb_device_info *pVBInfo)
{
	unsigned char di_0, di_1, tempal;

	tempal = XGI_GetVCLKPtr(RefreshRateTableIndex, ModeNo, ModeIdIndex,
			pVBInfo);
	XGI_GetVCLKLen(tempal, &di_0, &di_1, pVBInfo);
	XGI_GetLCDVCLKPtr(&di_0, &di_1, pVBInfo);

	if (pVBInfo->VBType & VB_SIS301) { /* shampoo 0129 */
		/* 301 */
		xgifb_reg_set(pVBInfo->Part4Port, 0x0A, 0x10);
		xgifb_reg_set(pVBInfo->Part4Port, 0x0B, di_1);
		xgifb_reg_set(pVBInfo->Part4Port, 0x0A, di_0);
	} else { /* 301b/302b/301lv/302lv */
		xgifb_reg_set(pVBInfo->Part4Port, 0x0A, di_0);
		xgifb_reg_set(pVBInfo->Part4Port, 0x0B, di_1);
	}

	xgifb_reg_set(pVBInfo->Part4Port, 0x00, 0x12);

	if (pVBInfo->VBInfo & SetCRT2ToRAMDAC)
		xgifb_reg_or(pVBInfo->Part4Port, 0x12, 0x28);
	else
		xgifb_reg_or(pVBInfo->Part4Port, 0x12, 0x08);
}

static unsigned short XGI_GetColorDepth(unsigned short ModeNo,
		unsigned short ModeIdIndex, struct vb_device_info *pVBInfo)
{
	unsigned short ColorDepth[6] = { 1, 2, 4, 4, 6, 8 };
	short index;
	unsigned short modeflag;

	modeflag = pVBInfo->EModeIDTable[ModeIdIndex].Ext_ModeFlag;
	index = (modeflag & ModeTypeMask) - ModeEGA;

	if (index < 0)
		index = 0;

	return ColorDepth[index];
}

static unsigned short XGI_GetOffset(unsigned short ModeNo,
				    unsigned short ModeIdIndex,
		unsigned short RefreshRateTableIndex,
		struct xgi_hw_device_info *HwDeviceExtension,
		struct vb_device_info *pVBInfo)
{
	unsigned short temp, colordepth, modeinfo, index, infoflag,
			ColorDepth[] = { 0x01, 0x02, 0x04 };

	modeinfo = pVBInfo->EModeIDTable[ModeIdIndex].Ext_ModeInfo;
	infoflag = pVBInfo->RefIndex[RefreshRateTableIndex].Ext_InfoFlag;

	index = (modeinfo >> 8) & 0xFF;

	temp = pVBInfo->ScreenOffset[index];

	if (infoflag & InterlaceMode)
		temp = temp << 1;

	colordepth = XGI_GetColorDepth(ModeNo, ModeIdIndex, pVBInfo);

	if ((ModeNo >= 0x7C) && (ModeNo <= 0x7E)) {
		temp = ModeNo - 0x7C;
		colordepth = ColorDepth[temp];
		temp = 0x6B;
		if (infoflag & InterlaceMode)
			temp = temp << 1;
		return temp * colordepth;
	} else {
		return temp * colordepth;
	}
}

static void XGI_SetCRT2Offset(unsigned short ModeNo,
		unsigned short ModeIdIndex,
		unsigned short RefreshRateTableIndex,
		struct xgi_hw_device_info *HwDeviceExtension,
		struct vb_device_info *pVBInfo)
{
	unsigned short offset;
	unsigned char temp;

	if (pVBInfo->VBInfo & SetInSlaveMode)
		return;

	offset = XGI_GetOffset(ModeNo, ModeIdIndex, RefreshRateTableIndex,
			HwDeviceExtension, pVBInfo);
	temp = (unsigned char) (offset & 0xFF);
	xgifb_reg_set(pVBInfo->Part1Port, 0x07, temp);
	temp = (unsigned char) ((offset & 0xFF00) >> 8);
	xgifb_reg_set(pVBInfo->Part1Port, 0x09, temp);
	temp = (unsigned char) (((offset >> 3) & 0xFF) + 1);
	xgifb_reg_set(pVBInfo->Part1Port, 0x03, temp);
}

static void XGI_SetCRT2FIFO(struct vb_device_info *pVBInfo)
{
	/* threshold high ,disable auto threshold */
	xgifb_reg_set(pVBInfo->Part1Port, 0x01, 0x3B);
	/* threshold low default 04h */
	xgifb_reg_and_or(pVBInfo->Part1Port, 0x02, ~(0x3F), 0x04);
}

static void XGI_PreSetGroup1(unsigned short ModeNo, unsigned short ModeIdIndex,
		struct xgi_hw_device_info *HwDeviceExtension,
		unsigned short RefreshRateTableIndex,
		struct vb_device_info *pVBInfo)
{
	unsigned short tempcx = 0, CRT1Index = 0, resinfo = 0;

	CRT1Index = pVBInfo->RefIndex[RefreshRateTableIndex].Ext_CRT1CRTC;
	CRT1Index &= IndexMask;
	resinfo = pVBInfo->EModeIDTable[ModeIdIndex].Ext_RESINFO;

	XGI_SetCRT2Offset(ModeNo, ModeIdIndex, RefreshRateTableIndex,
			HwDeviceExtension, pVBInfo);
	XGI_SetCRT2FIFO(pVBInfo);

	for (tempcx = 4; tempcx < 7; tempcx++)
		xgifb_reg_set(pVBInfo->Part1Port, tempcx, 0x0);

	xgifb_reg_set(pVBInfo->Part1Port, 0x50, 0x00);
	xgifb_reg_set(pVBInfo->Part1Port, 0x02, 0x44); /* temp 0206 */
}

static void XGI_SetGroup1(unsigned short ModeNo, unsigned short ModeIdIndex,
		struct xgi_hw_device_info *HwDeviceExtension,
		unsigned short RefreshRateTableIndex,
		struct vb_device_info *pVBInfo)
{
	unsigned short temp = 0, tempax = 0, tempbx = 0, tempcx = 0,
			pushbx = 0, CRT1Index = 0, modeflag, resinfo = 0;

	CRT1Index = pVBInfo->RefIndex[RefreshRateTableIndex].Ext_CRT1CRTC;
	CRT1Index &= IndexMask;
	resinfo = pVBInfo->EModeIDTable[ModeIdIndex].Ext_RESINFO;
	modeflag = pVBInfo->EModeIDTable[ModeIdIndex].Ext_ModeFlag;

	/* bainy change table name */
	if (modeflag & HalfDCLK) {
		/* BTVGA2HT 0x08,0x09 */
		temp = (pVBInfo->VGAHT / 2 - 1) & 0x0FF;
		xgifb_reg_set(pVBInfo->Part1Port, 0x08, temp);
		temp = (((pVBInfo->VGAHT / 2 - 1) & 0xFF00) >> 8) << 4;
		xgifb_reg_and_or(pVBInfo->Part1Port, 0x09, ~0x0F0, temp);
		/* BTVGA2HDEE 0x0A,0x0C */
		temp = (pVBInfo->VGAHDE / 2 + 16) & 0x0FF;
		xgifb_reg_set(pVBInfo->Part1Port, 0x0A, temp);
		tempcx = ((pVBInfo->VGAHT - pVBInfo->VGAHDE) / 2) >> 2;
		pushbx = pVBInfo->VGAHDE / 2 + 16;
		tempcx = tempcx >> 1;
		tempbx = pushbx + tempcx; /* bx BTVGA@HRS 0x0B,0x0C */
		tempcx += tempbx;

		if (pVBInfo->VBInfo & SetCRT2ToRAMDAC) {
			tempbx = pVBInfo->XGINEWUB_CRT1Table[CRT1Index].CR[4];
			tempbx |= ((pVBInfo->
					XGINEWUB_CRT1Table[CRT1Index].CR[14] &
						0xC0) << 2);
			tempbx = (tempbx - 3) << 3; /* (VGAHRS-3)*8 */
			tempcx = pVBInfo->XGINEWUB_CRT1Table[CRT1Index].CR[5];
			tempcx &= 0x1F;
			temp = pVBInfo->XGINEWUB_CRT1Table[CRT1Index].CR[15];
			temp = (temp & 0x04) << (5 - 2); /* VGAHRE D[5] */
			tempcx = ((tempcx | temp) - 3) << 3; /* (VGAHRE-3)*8 */
		}

		tempbx += 4;
		tempcx += 4;

		if (tempcx > (pVBInfo->VGAHT / 2))
			tempcx = pVBInfo->VGAHT / 2;

		temp = tempbx & 0x00FF;

		xgifb_reg_set(pVBInfo->Part1Port, 0x0B, temp);
	} else {
		temp = (pVBInfo->VGAHT - 1) & 0x0FF; /* BTVGA2HT 0x08,0x09 */
		xgifb_reg_set(pVBInfo->Part1Port, 0x08, temp);
		temp = (((pVBInfo->VGAHT - 1) & 0xFF00) >> 8) << 4;
		xgifb_reg_and_or(pVBInfo->Part1Port, 0x09, ~0x0F0, temp);
		/* BTVGA2HDEE 0x0A,0x0C */
		temp = (pVBInfo->VGAHDE + 16) & 0x0FF;
		xgifb_reg_set(pVBInfo->Part1Port, 0x0A, temp);
		tempcx = (pVBInfo->VGAHT - pVBInfo->VGAHDE) >> 2; /* cx */
		pushbx = pVBInfo->VGAHDE + 16;
		tempcx = tempcx >> 1;
		tempbx = pushbx + tempcx; /* bx BTVGA@HRS 0x0B,0x0C */
		tempcx += tempbx;

		if (pVBInfo->VBInfo & SetCRT2ToRAMDAC) {
			tempbx = pVBInfo->XGINEWUB_CRT1Table[CRT1Index].CR[3];
			tempbx |= ((pVBInfo->
					XGINEWUB_CRT1Table[CRT1Index].CR[5] &
						0xC0) << 2);
			tempbx = (tempbx - 3) << 3; /* (VGAHRS-3)*8 */
			tempcx = pVBInfo->XGINEWUB_CRT1Table[CRT1Index].CR[4];
			tempcx &= 0x1F;
			temp = pVBInfo->XGINEWUB_CRT1Table[CRT1Index].CR[6];
			temp = (temp & 0x04) << (5 - 2); /* VGAHRE D[5] */
			tempcx = ((tempcx | temp) - 3) << 3; /* (VGAHRE-3)*8 */
			tempbx += 16;
			tempcx += 16;
		}

		if (tempcx > pVBInfo->VGAHT)
			tempcx = pVBInfo->VGAHT;

		temp = tempbx & 0x00FF;
		xgifb_reg_set(pVBInfo->Part1Port, 0x0B, temp);
	}

	tempax = (tempax & 0x00FF) | (tempbx & 0xFF00);
	tempbx = pushbx;
	tempbx = (tempbx & 0x00FF) | ((tempbx & 0xFF00) << 4);
	tempax |= (tempbx & 0xFF00);
	temp = (tempax & 0xFF00) >> 8;
	xgifb_reg_set(pVBInfo->Part1Port, 0x0C, temp);
	temp = tempcx & 0x00FF;
	xgifb_reg_set(pVBInfo->Part1Port, 0x0D, temp);
	tempcx = (pVBInfo->VGAVT - 1);
	temp = tempcx & 0x00FF;

	xgifb_reg_set(pVBInfo->Part1Port, 0x0E, temp);
	tempbx = pVBInfo->VGAVDE - 1;
	temp = tempbx & 0x00FF;
	xgifb_reg_set(pVBInfo->Part1Port, 0x0F, temp);
	temp = ((tempbx & 0xFF00) << 3) >> 8;
	temp |= ((tempcx & 0xFF00) >> 8);
	xgifb_reg_set(pVBInfo->Part1Port, 0x12, temp);

	tempax = pVBInfo->VGAVDE;
	tempbx = pVBInfo->VGAVDE;
	tempcx = pVBInfo->VGAVT;
	/* BTVGA2VRS 0x10,0x11 */
	tempbx = (pVBInfo->VGAVT + pVBInfo->VGAVDE) >> 1;
	/* BTVGA2VRE 0x11 */
	tempcx = ((pVBInfo->VGAVT - pVBInfo->VGAVDE) >> 4) + tempbx + 1;

	if (pVBInfo->VBInfo & SetCRT2ToRAMDAC) {
		tempbx = pVBInfo->XGINEWUB_CRT1Table[CRT1Index].CR[10];
		temp = pVBInfo->XGINEWUB_CRT1Table[CRT1Index].CR[9];

		if (temp & 0x04)
			tempbx |= 0x0100;

		if (temp & 0x080)
			tempbx |= 0x0200;

		temp = pVBInfo->XGINEWUB_CRT1Table[CRT1Index].CR[14];

		if (temp & 0x08)
			tempbx |= 0x0400;

		temp = pVBInfo->XGINEWUB_CRT1Table[CRT1Index].CR[11];
		tempcx = (tempcx & 0xFF00) | (temp & 0x00FF);
	}

	temp = tempbx & 0x00FF;
	xgifb_reg_set(pVBInfo->Part1Port, 0x10, temp);
	temp = ((tempbx & 0xFF00) >> 8) << 4;
	temp = ((tempcx & 0x000F) | (temp));
	xgifb_reg_set(pVBInfo->Part1Port, 0x11, temp);
	tempax = 0;

	if (modeflag & DoubleScanMode)
		tempax |= 0x80;

	if (modeflag & HalfDCLK)
		tempax |= 0x40;

	xgifb_reg_and_or(pVBInfo->Part1Port, 0x2C, ~0x0C0, tempax);
}

static unsigned short XGI_GetVGAHT2(struct vb_device_info *pVBInfo)
{
	unsigned long tempax, tempbx;

	tempbx = ((pVBInfo->VGAVT - pVBInfo->VGAVDE) * pVBInfo->RVBHCMAX)
			& 0xFFFF;
	tempax = (pVBInfo->VT - pVBInfo->VDE) * pVBInfo->RVBHCFACT;
	tempax = (tempax * pVBInfo->HT) / tempbx;

	return (unsigned short) tempax;
}

static void XGI_SetLockRegs(unsigned short ModeNo, unsigned short ModeIdIndex,
		struct xgi_hw_device_info *HwDeviceExtension,
		unsigned short RefreshRateTableIndex,
		struct vb_device_info *pVBInfo)
{
	unsigned short push1, push2, tempax, tempbx = 0, tempcx, temp, resinfo,
			modeflag, CRT1Index;

	/* si+Ext_ResInfo */
	modeflag = pVBInfo->EModeIDTable[ModeIdIndex].Ext_ModeFlag;
	resinfo = pVBInfo->EModeIDTable[ModeIdIndex].Ext_RESINFO;
	CRT1Index = pVBInfo->RefIndex[RefreshRateTableIndex].Ext_CRT1CRTC;
	CRT1Index &= IndexMask;

	if (!(pVBInfo->VBInfo & SetInSlaveMode))
		return;

	temp = 0xFF; /* set MAX HT */
	xgifb_reg_set(pVBInfo->Part1Port, 0x03, temp);
	tempcx = 0x08;

	if (pVBInfo->VBType & (VB_SIS301LV | VB_SIS302LV | VB_XGI301C))
		modeflag |= Charx8Dot;

	tempax = pVBInfo->VGAHDE; /* 0x04 Horizontal Display End */

	if (modeflag & HalfDCLK)
		tempax = tempax >> 1;

	tempax = (tempax / tempcx) - 1;
	tempbx |= ((tempax & 0x00FF) << 8);
	temp = tempax & 0x00FF;
	xgifb_reg_set(pVBInfo->Part1Port, 0x04, temp);

	temp = (tempbx & 0xFF00) >> 8;

	if (pVBInfo->VBInfo & SetCRT2ToTV) {
		if (!(pVBInfo->VBType & (VB_SIS301B | VB_SIS302B | VB_SIS301LV
				| VB_SIS302LV | VB_XGI301C)))
			temp += 2;

		if ((pVBInfo->VBInfo & SetCRT2ToHiVision) &&
		    !(pVBInfo->VBType & VB_SIS301LV) && (resinfo == 7))
				temp -= 2;
	}

	/* 0x05 Horizontal Display Start */
	xgifb_reg_set(pVBInfo->Part1Port, 0x05, temp);
	/* 0x06 Horizontal Blank end */
	xgifb_reg_set(pVBInfo->Part1Port, 0x06, 0x03);

	if (!(pVBInfo->VBInfo & DisableCRT2Display)) { /* 030226 bainy */
		if (pVBInfo->VBInfo & SetCRT2ToTV)
			tempax = pVBInfo->VGAHT;
		else
			tempax = XGI_GetVGAHT2(pVBInfo);
	}

	if (tempax >= pVBInfo->VGAHT)
		tempax = pVBInfo->VGAHT;

	if (modeflag & HalfDCLK)
		tempax = tempax >> 1;

	tempax = (tempax / tempcx) - 5;
	tempcx = tempax; /* 20030401 0x07 horizontal Retrace Start */
	if (pVBInfo->VBInfo & SetCRT2ToHiVision) {
		temp = (tempbx & 0x00FF) - 1;
		if (!(modeflag & HalfDCLK)) {
			temp -= 6;
			if (pVBInfo->TVInfo & TVSimuMode) {
				temp -= 4;
				temp -= 10;
			}
		}
	} else {
		tempbx = (tempbx & 0xFF00) >> 8;
		tempcx = (tempcx + tempbx) >> 1;
		temp = (tempcx & 0x00FF) + 2;

		if (pVBInfo->VBInfo & SetCRT2ToTV) {
			temp -= 1;
			if (!(modeflag & HalfDCLK)) {
				if ((modeflag & Charx8Dot)) {
					temp += 4;
					if (pVBInfo->VGAHDE >= 800)
						temp -= 6;
				}
			}
		} else if (!(modeflag & HalfDCLK)) {
			temp -= 4;
			if (pVBInfo->LCDResInfo != Panel_1280x960 &&
			    pVBInfo->VGAHDE >= 800) {
				temp -= 7;
				if (pVBInfo->VGAHDE >= 1280 &&
				    pVBInfo->LCDResInfo != Panel_1280x960 &&
				    (pVBInfo->LCDInfo & LCDNonExpanding))
					temp += 28;
			}
		}
	}

	/* 0x07 Horizontal Retrace Start */
	xgifb_reg_set(pVBInfo->Part1Port, 0x07, temp);
	/* 0x08 Horizontal Retrace End */
	xgifb_reg_set(pVBInfo->Part1Port, 0x08, 0);

	if (pVBInfo->VBInfo & SetCRT2ToTV) {
		if (pVBInfo->TVInfo & TVSimuMode) {
			if (ModeNo == 0x50) {
				if (pVBInfo->TVInfo & SetNTSCTV) {
					xgifb_reg_set(pVBInfo->Part1Port,
							0x07, 0x30);
					xgifb_reg_set(pVBInfo->Part1Port,
							0x08, 0x03);
				} else {
					xgifb_reg_set(pVBInfo->Part1Port,
							0x07, 0x2f);
					xgifb_reg_set(pVBInfo->Part1Port,
							0x08, 0x02);
				}
			}
		}
	}

	xgifb_reg_set(pVBInfo->Part1Port, 0x18, 0x03); /* 0x18 SR0B */
	xgifb_reg_and_or(pVBInfo->Part1Port, 0x19, 0xF0, 0x00);
	xgifb_reg_set(pVBInfo->Part1Port, 0x09, 0xFF); /* 0x09 Set Max VT */

	tempbx = pVBInfo->VGAVT;
	push1 = tempbx;
	tempcx = 0x121;
	tempbx = pVBInfo->VGAVDE; /* 0x0E Virtical Display End */

	if (tempbx == 357)
		tempbx = 350;
	if (tempbx == 360)
		tempbx = 350;
	if (tempbx == 375)
		tempbx = 350;
	if (tempbx == 405)
		tempbx = 400;
	if (tempbx == 525)
		tempbx = 480;

	push2 = tempbx;

	if (pVBInfo->VBInfo & SetCRT2ToLCD) {
		if (pVBInfo->LCDResInfo == Panel_1024x768) {
			if (!(pVBInfo->LCDInfo & XGI_LCDVESATiming)) {
				if (tempbx == 350)
					tempbx += 5;
				if (tempbx == 480)
					tempbx += 5;
			}
		}
	}
	tempbx--;
	temp = tempbx & 0x00FF;
	tempbx--;
	temp = tempbx & 0x00FF;
	/* 0x10 vertical Blank Start */
	xgifb_reg_set(pVBInfo->Part1Port, 0x10, temp);
	tempbx = push2;
	tempbx--;
	temp = tempbx & 0x00FF;
	xgifb_reg_set(pVBInfo->Part1Port, 0x0E, temp);

	if (tempbx & 0x0100)
		tempcx |= 0x0002;

	tempax = 0x000B;

	if (modeflag & DoubleScanMode)
		tempax |= 0x08000;

	if (tempbx & 0x0200)
		tempcx |= 0x0040;

	temp = (tempax & 0xFF00) >> 8;
	xgifb_reg_set(pVBInfo->Part1Port, 0x0B, temp);

	if (tempbx & 0x0400)
		tempcx |= 0x0600;

	/* 0x11 Vertival Blank End */
	xgifb_reg_set(pVBInfo->Part1Port, 0x11, 0x00);

	tempax = push1;
	tempax -= tempbx; /* 0x0C Vertical Retrace Start */
	tempax = tempax >> 2;
	push1 = tempax; /* push ax */

	if (resinfo != 0x09) {
		tempax = tempax << 1;
		tempbx += tempax;
	}

	if (pVBInfo->VBInfo & SetCRT2ToHiVision) {
		if ((pVBInfo->VBType & VB_SIS301LV) &&
		    !(pVBInfo->TVInfo & TVSetHiVision)) {
			if ((pVBInfo->TVInfo & TVSimuMode) &&
			    (pVBInfo->TVInfo & TVSetPAL)) {
				if (!(pVBInfo->VBType & VB_SIS301LV) ||
				    !(pVBInfo->TVInfo &
				      (TVSetYPbPr525p |
				       TVSetYPbPr750p |
				       TVSetHiVision)))
					tempbx += 40;
			}
		} else {
			tempbx -= 10;
		}
	} else {
		if (pVBInfo->TVInfo & TVSimuMode) {
			if (pVBInfo->TVInfo & TVSetPAL) {
				if (pVBInfo->VBType & VB_SIS301LV) {
					if (!(pVBInfo->TVInfo &
					    (TVSetYPbPr525p |
					     TVSetYPbPr750p |
					     TVSetHiVision)))
						tempbx += 40;
				} else {
					tempbx += 40;
				}
			}
		}
	}
	tempax = push1;
	tempax = tempax >> 2;
	tempax++;
	tempax += tempbx;
	push1 = tempax; /* push ax */

	if ((pVBInfo->TVInfo & TVSetPAL)) {
		if (tempbx <= 513) {
			if (tempax >= 513)
				tempbx = 513;
		}
	}

	temp = tempbx & 0x00FF;
	xgifb_reg_set(pVBInfo->Part1Port, 0x0C, temp);
	tempbx--;
	temp = tempbx & 0x00FF;
	xgifb_reg_set(pVBInfo->Part1Port, 0x10, temp);

	if (tempbx & 0x0100)
		tempcx |= 0x0008;

	if (tempbx & 0x0200)
		xgifb_reg_and_or(pVBInfo->Part1Port, 0x0B, 0x0FF, 0x20);

	tempbx++;

	if (tempbx & 0x0100)
		tempcx |= 0x0004;

	if (tempbx & 0x0200)
		tempcx |= 0x0080;

	if (tempbx & 0x0400)
		tempcx |= 0x0C00;

	tempbx = push1; /* pop ax */
	temp = tempbx & 0x00FF;
	temp &= 0x0F;
	/* 0x0D vertical Retrace End */
	xgifb_reg_set(pVBInfo->Part1Port, 0x0D, temp);

	if (tempbx & 0x0010)
		tempcx |= 0x2000;

	temp = tempcx & 0x00FF;
	xgifb_reg_set(pVBInfo->Part1Port, 0x0A, temp); /* 0x0A CR07 */
	temp = (tempcx & 0x0FF00) >> 8;
	xgifb_reg_set(pVBInfo->Part1Port, 0x17, temp); /* 0x17 SR0A */
	tempax = modeflag;
	temp = (tempax & 0xFF00) >> 8;

	temp = (temp >> 1) & 0x09;

	if (pVBInfo->VBType & (VB_SIS301LV | VB_SIS302LV | VB_XGI301C))
		temp |= 0x01;

	xgifb_reg_set(pVBInfo->Part1Port, 0x16, temp); /* 0x16 SR01 */
	xgifb_reg_set(pVBInfo->Part1Port, 0x0F, 0); /* 0x0F CR14 */
	xgifb_reg_set(pVBInfo->Part1Port, 0x12, 0); /* 0x12 CR17 */

	if (pVBInfo->LCDInfo & LCDRGB18Bit)
		temp = 0x80;
	else
		temp = 0x00;

	xgifb_reg_set(pVBInfo->Part1Port, 0x1A, temp); /* 0x1A SR0E */

	return;
}

static void XGI_SetGroup2(unsigned short ModeNo, unsigned short ModeIdIndex,
		unsigned short RefreshRateTableIndex,
		struct xgi_hw_device_info *HwDeviceExtension,
		struct vb_device_info *pVBInfo)
{
	unsigned short i, j, tempax, tempbx, tempcx, temp, push1, push2,
			modeflag, resinfo, crt2crtc;
	unsigned char *TimingPoint;

	unsigned long longtemp, tempeax, tempebx, temp2, tempecx;

	/* si+Ext_ResInfo */
	modeflag = pVBInfo->EModeIDTable[ModeIdIndex].Ext_ModeFlag;
	resinfo = pVBInfo->EModeIDTable[ModeIdIndex].Ext_RESINFO;
	crt2crtc = pVBInfo->RefIndex[RefreshRateTableIndex].Ext_CRT2CRTC;

	tempax = 0;

	if (!(pVBInfo->VBInfo & SetCRT2ToAVIDEO))
		tempax |= 0x0800;

	if (!(pVBInfo->VBInfo & SetCRT2ToSVIDEO))
		tempax |= 0x0400;

	if (pVBInfo->VBInfo & SetCRT2ToSCART)
		tempax |= 0x0200;

	if (!(pVBInfo->TVInfo & TVSetPAL))
		tempax |= 0x1000;

	if (pVBInfo->VBInfo & SetCRT2ToHiVision)
		tempax |= 0x0100;

	if (pVBInfo->TVInfo & (TVSetYPbPr525p | TVSetYPbPr750p))
		tempax &= 0xfe00;

	tempax = (tempax & 0xff00) >> 8;

	xgifb_reg_set(pVBInfo->Part2Port, 0x0, tempax);
	TimingPoint = pVBInfo->NTSCTiming;

	if (pVBInfo->TVInfo & TVSetPAL)
		TimingPoint = pVBInfo->PALTiming;

	if (pVBInfo->VBInfo & SetCRT2ToHiVision) {
		TimingPoint = pVBInfo->HiTVExtTiming;

		if (pVBInfo->VBInfo & SetInSlaveMode)
			TimingPoint = pVBInfo->HiTVSt2Timing;

		if (pVBInfo->SetFlag & TVSimuMode)
			TimingPoint = pVBInfo->HiTVSt1Timing;

		if (!(modeflag & Charx8Dot))
			TimingPoint = pVBInfo->HiTVTextTiming;
	}

	if (pVBInfo->VBInfo & SetCRT2ToYPbPr525750) {
		if (pVBInfo->TVInfo & TVSetYPbPr525i)
			TimingPoint = pVBInfo->YPbPr525iTiming;

		if (pVBInfo->TVInfo & TVSetYPbPr525p)
			TimingPoint = pVBInfo->YPbPr525pTiming;

		if (pVBInfo->TVInfo & TVSetYPbPr750p)
			TimingPoint = pVBInfo->YPbPr750pTiming;
	}

	for (i = 0x01, j = 0; i <= 0x2D; i++, j++)
		xgifb_reg_set(pVBInfo->Part2Port, i, TimingPoint[j]);

	for (i = 0x39; i <= 0x45; i++, j++)
		/* di->temp2[j] */
		xgifb_reg_set(pVBInfo->Part2Port, i, TimingPoint[j]);

	if (pVBInfo->VBInfo & SetCRT2ToTV)
		xgifb_reg_and_or(pVBInfo->Part2Port, 0x3A, 0x1F, 0x00);

	temp = pVBInfo->NewFlickerMode;
	temp &= 0x80;
	xgifb_reg_and_or(pVBInfo->Part2Port, 0x0A, 0xFF, temp);

	if (pVBInfo->VBInfo & SetCRT2ToHiVision)
		tempax = 950;

	if (pVBInfo->TVInfo & TVSetPAL)
		tempax = 520;
	else
		tempax = 440;

	if (pVBInfo->VDE <= tempax) {
		tempax -= pVBInfo->VDE;
		tempax = tempax >> 2;
		tempax = (tempax & 0x00FF) | ((tempax & 0x00FF) << 8);
		push1 = tempax;
		temp = (tempax & 0xFF00) >> 8;
		temp += (unsigned short) TimingPoint[0];

		if (pVBInfo->VBType & (VB_SIS301B | VB_SIS302B | VB_SIS301LV
				| VB_SIS302LV | VB_XGI301C)) {
			if (pVBInfo->VBInfo & (SetCRT2ToAVIDEO
					| SetCRT2ToSVIDEO | SetCRT2ToSCART
					| SetCRT2ToYPbPr525750)) {
				tempcx = pVBInfo->VGAHDE;
				if (tempcx >= 1024) {
					temp = 0x17; /* NTSC */
					if (pVBInfo->TVInfo & TVSetPAL)
						temp = 0x19; /* PAL */
				}
			}
		}

		xgifb_reg_set(pVBInfo->Part2Port, 0x01, temp);
		tempax = push1;
		temp = (tempax & 0xFF00) >> 8;
		temp += TimingPoint[1];

		if (pVBInfo->VBType & (VB_SIS301B | VB_SIS302B | VB_SIS301LV
				| VB_SIS302LV | VB_XGI301C)) {
			if ((pVBInfo->VBInfo & (SetCRT2ToAVIDEO
					| SetCRT2ToSVIDEO | SetCRT2ToSCART
					| SetCRT2ToYPbPr525750))) {
				tempcx = pVBInfo->VGAHDE;
				if (tempcx >= 1024) {
					temp = 0x1D; /* NTSC */
					if (pVBInfo->TVInfo & TVSetPAL)
						temp = 0x52; /* PAL */
				}
			}
		}
		xgifb_reg_set(pVBInfo->Part2Port, 0x02, temp);
	}

	/* 301b */
	tempcx = pVBInfo->HT;

	if (XGI_IsLCDDualLink(pVBInfo))
		tempcx = tempcx >> 1;

	tempcx -= 2;
	temp = tempcx & 0x00FF;
	xgifb_reg_set(pVBInfo->Part2Port, 0x1B, temp);

	temp = (tempcx & 0xFF00) >> 8;
	xgifb_reg_and_or(pVBInfo->Part2Port, 0x1D, ~0x0F, temp);

	tempcx = pVBInfo->HT >> 1;
	push1 = tempcx; /* push cx */
	tempcx += 7;

	if (pVBInfo->VBInfo & SetCRT2ToHiVision)
		tempcx -= 4;

	temp = tempcx & 0x00FF;
	temp = temp << 4;
	xgifb_reg_and_or(pVBInfo->Part2Port, 0x22, 0x0F, temp);

	tempbx = TimingPoint[j] | ((TimingPoint[j + 1]) << 8);
	tempbx += tempcx;
	push2 = tempbx;
	temp = tempbx & 0x00FF;
	xgifb_reg_set(pVBInfo->Part2Port, 0x24, temp);
	temp = (tempbx & 0xFF00) >> 8;
	temp = temp << 4;
	xgifb_reg_and_or(pVBInfo->Part2Port, 0x25, 0x0F, temp);

	tempbx = push2;
	tempbx = tempbx + 8;
	if (pVBInfo->VBInfo & SetCRT2ToHiVision) {
		tempbx = tempbx - 4;
		tempcx = tempbx;
	}

	temp = (tempbx & 0x00FF) << 4;
	xgifb_reg_and_or(pVBInfo->Part2Port, 0x29, 0x0F, temp);

	j += 2;
	tempcx += (TimingPoint[j] | ((TimingPoint[j + 1]) << 8));
	temp = tempcx & 0x00FF;
	xgifb_reg_set(pVBInfo->Part2Port, 0x27, temp);
	temp = ((tempcx & 0xFF00) >> 8) << 4;
	xgifb_reg_and_or(pVBInfo->Part2Port, 0x28, 0x0F, temp);

	tempcx += 8;
	if (pVBInfo->VBInfo & SetCRT2ToHiVision)
		tempcx -= 4;

	temp = tempcx & 0xFF;
	temp = temp << 4;
	xgifb_reg_and_or(pVBInfo->Part2Port, 0x2A, 0x0F, temp);

	tempcx = push1; /* pop cx */
	j += 2;
	temp = TimingPoint[j] | ((TimingPoint[j + 1]) << 8);
	tempcx -= temp;
	temp = tempcx & 0x00FF;
	temp = temp << 4;
	xgifb_reg_and_or(pVBInfo->Part2Port, 0x2D, 0x0F, temp);

	tempcx -= 11;

	if (!(pVBInfo->VBInfo & SetCRT2ToTV)) {
		tempax = XGI_GetVGAHT2(pVBInfo);
		tempcx = tempax - 1;
	}
	temp = tempcx & 0x00FF;
	xgifb_reg_set(pVBInfo->Part2Port, 0x2E, temp);

	tempbx = pVBInfo->VDE;

	if (pVBInfo->VGAVDE == 360)
		tempbx = 746;
	if (pVBInfo->VGAVDE == 375)
		tempbx = 746;
	if (pVBInfo->VGAVDE == 405)
		tempbx = 853;

	if (pVBInfo->VBInfo & SetCRT2ToTV) {
		if (pVBInfo->VBType &
		    (VB_SIS301LV | VB_SIS302LV | VB_XGI301C)) {
			if (!(pVBInfo->TVInfo &
			    (TVSetYPbPr525p | TVSetYPbPr750p)))
				tempbx = tempbx >> 1;
		} else
			tempbx = tempbx >> 1;
	}

	tempbx -= 2;
	temp = tempbx & 0x00FF;

	if (pVBInfo->VBInfo & SetCRT2ToHiVision) {
		if (pVBInfo->VBType & VB_SIS301LV) {
			if (pVBInfo->TVInfo & TVSetHiVision) {
				if (pVBInfo->VBInfo & SetInSlaveMode) {
					if (ModeNo == 0x2f)
						temp += 1;
				}
			}
		} else {
			if (pVBInfo->VBInfo & SetInSlaveMode) {
				if (ModeNo == 0x2f)
					temp += 1;
			}
		}
	}

	xgifb_reg_set(pVBInfo->Part2Port, 0x2F, temp);

	temp = (tempcx & 0xFF00) >> 8;
	temp |= ((tempbx & 0xFF00) >> 8) << 6;

	if (!(pVBInfo->VBInfo & SetCRT2ToHiVision)) {
		if (pVBInfo->VBType & VB_SIS301LV) {
			if (pVBInfo->TVInfo & TVSetHiVision) {
				temp |= 0x10;

				if (!(pVBInfo->VBInfo & SetCRT2ToSVIDEO))
					temp |= 0x20;
			}
		} else {
			temp |= 0x10;
			if (!(pVBInfo->VBInfo & SetCRT2ToSVIDEO))
				temp |= 0x20;
		}
	}

	xgifb_reg_set(pVBInfo->Part2Port, 0x30, temp);

	if (pVBInfo->VBType & (VB_SIS301B | VB_SIS302B | VB_SIS301LV
			| VB_SIS302LV | VB_XGI301C)) { /* TV gatingno */
		tempbx = pVBInfo->VDE;
		tempcx = tempbx - 2;

		if (pVBInfo->VBInfo & SetCRT2ToTV) {
			if (!(pVBInfo->TVInfo & (TVSetYPbPr525p
					| TVSetYPbPr750p)))
				tempbx = tempbx >> 1;
		}

		if (pVBInfo->VBType & (VB_SIS302LV | VB_XGI301C)) {
			temp = 0;
			if (tempcx & 0x0400)
				temp |= 0x20;

			if (tempbx & 0x0400)
				temp |= 0x40;

			xgifb_reg_set(pVBInfo->Part4Port, 0x10, temp);
		}

		temp = (((tempbx - 3) & 0x0300) >> 8) << 5;
		xgifb_reg_set(pVBInfo->Part2Port, 0x46, temp);
		temp = (tempbx - 3) & 0x00FF;
		xgifb_reg_set(pVBInfo->Part2Port, 0x47, temp);
	}

	tempbx = tempbx & 0x00FF;

	if (!(modeflag & HalfDCLK)) {
		tempcx = pVBInfo->VGAHDE;
		if (tempcx >= pVBInfo->HDE) {
			tempbx |= 0x2000;
			tempax &= 0x00FF;
		}
	}

	tempcx = 0x0101;

	if (pVBInfo->VBInfo & SetCRT2ToTV) { /*301b*/
		if (pVBInfo->VGAHDE >= 1024) {
			tempcx = 0x1920;
			if (pVBInfo->VGAHDE >= 1280) {
				tempcx = 0x1420;
				tempbx = tempbx & 0xDFFF;
			}
		}
	}

	if (!(tempbx & 0x2000)) {
		if (modeflag & HalfDCLK)
			tempcx = (tempcx & 0xFF00) | ((tempcx & 0x00FF) << 1);

		push1 = tempbx;
		tempeax = pVBInfo->VGAHDE;
		tempebx = (tempcx & 0xFF00) >> 8;
		longtemp = tempeax * tempebx;
		tempecx = tempcx & 0x00FF;
		longtemp = longtemp / tempecx;

		/* 301b */
		tempecx = 8 * 1024;

		if (pVBInfo->VBType & (VB_SIS301B | VB_SIS302B | VB_SIS301LV
				| VB_SIS302LV | VB_XGI301C)) {
			tempecx = tempecx * 8;
		}

		longtemp = longtemp * tempecx;
		tempecx = pVBInfo->HDE;
		temp2 = longtemp % tempecx;
		tempeax = longtemp / tempecx;
		if (temp2 != 0)
			tempeax += 1;

		tempax = (unsigned short) tempeax;

		/* 301b */
		if (pVBInfo->VBType & (VB_SIS301B | VB_SIS302B | VB_SIS301LV
				| VB_SIS302LV | VB_XGI301C)) {
			tempcx = ((tempax & 0xFF00) >> 5) >> 8;
		}
		/* end 301b */

		tempbx = push1;
		tempbx = (unsigned short) (((tempeax & 0x0000FF00) & 0x1F00)
				| (tempbx & 0x00FF));
		tempax = (unsigned short) (((tempeax & 0x000000FF) << 8)
				| (tempax & 0x00FF));
		temp = (tempax & 0xFF00) >> 8;
	} else {
		temp = (tempax & 0x00FF) >> 8;
	}

	xgifb_reg_set(pVBInfo->Part2Port, 0x44, temp);
	temp = (tempbx & 0xFF00) >> 8;
	xgifb_reg_and_or(pVBInfo->Part2Port, 0x45, ~0x03F, temp);
	temp = tempcx & 0x00FF;

	if (tempbx & 0x2000)
		temp = 0;

	if (!(pVBInfo->VBInfo & SetCRT2ToLCD))
		temp |= 0x18;

	xgifb_reg_and_or(pVBInfo->Part2Port, 0x46, ~0x1F, temp);
	if (pVBInfo->TVInfo & TVSetPAL) {
		tempbx = 0x0382;
		tempcx = 0x007e;
	} else {
		tempbx = 0x0369;
		tempcx = 0x0061;
	}

	temp = tempbx & 0x00FF;
	xgifb_reg_set(pVBInfo->Part2Port, 0x4b, temp);
	temp = tempcx & 0x00FF;
	xgifb_reg_set(pVBInfo->Part2Port, 0x4c, temp);

	temp = ((tempcx & 0xFF00) >> 8) & 0x03;
	temp = temp << 2;
	temp |= ((tempbx & 0xFF00) >> 8) & 0x03;

	if (pVBInfo->VBInfo & SetCRT2ToYPbPr525750) {
		temp |= 0x10;

		if (pVBInfo->TVInfo & TVSetYPbPr525p)
			temp |= 0x20;

		if (pVBInfo->TVInfo & TVSetYPbPr750p)
			temp |= 0x60;
	}

	xgifb_reg_set(pVBInfo->Part2Port, 0x4d, temp);
	temp = xgifb_reg_get(pVBInfo->Part2Port, 0x43); /* 301b change */
	xgifb_reg_set(pVBInfo->Part2Port, 0x43, (unsigned short) (temp - 3));

	if (!(pVBInfo->TVInfo & (TVSetYPbPr525p | TVSetYPbPr750p))) {
		if (pVBInfo->TVInfo & NTSC1024x768) {
			TimingPoint = XGI_NTSC1024AdjTime;
			for (i = 0x1c, j = 0; i <= 0x30; i++, j++) {
				xgifb_reg_set(pVBInfo->Part2Port, i,
						TimingPoint[j]);
			}
			xgifb_reg_set(pVBInfo->Part2Port, 0x43, 0x72);
		}
	}

	/* Modify for 301C PALM Support */
	if (pVBInfo->VBType & VB_XGI301C) {
		if (pVBInfo->TVInfo & TVSetPALM)
			xgifb_reg_and_or(pVBInfo->Part2Port, 0x4E, ~0x08,
					0x08); /* PALM Mode */
	}

	if (pVBInfo->TVInfo & TVSetPALM) {
		tempax = (unsigned char) xgifb_reg_get(pVBInfo->Part2Port,
				0x01);
		tempax--;
		xgifb_reg_and(pVBInfo->Part2Port, 0x01, tempax);

		xgifb_reg_and(pVBInfo->Part2Port, 0x00, 0xEF);
	}

	if (pVBInfo->VBInfo & SetCRT2ToHiVision) {
		if (!(pVBInfo->VBInfo & SetInSlaveMode))
			xgifb_reg_set(pVBInfo->Part2Port, 0x0B, 0x00);
	}

	if (pVBInfo->VBInfo & SetCRT2ToTV)
		return;
}

static void XGI_SetLCDRegs(unsigned short ModeNo, unsigned short ModeIdIndex,
		struct xgi_hw_device_info *HwDeviceExtension,
		unsigned short RefreshRateTableIndex,
		struct vb_device_info *pVBInfo)
{
	unsigned short push1, push2, pushbx, tempax, tempbx, tempcx, temp,
			tempah, tempbh, tempch, resinfo, modeflag, CRT1Index;

	struct XGI_LCDDesStruct *LCDBDesPtr = NULL;

	/* si+Ext_ResInfo */
	modeflag = pVBInfo->EModeIDTable[ModeIdIndex].Ext_ModeFlag;
	resinfo = pVBInfo->EModeIDTable[ModeIdIndex].Ext_RESINFO;
	CRT1Index = pVBInfo->RefIndex[RefreshRateTableIndex].Ext_CRT1CRTC;
	CRT1Index &= IndexMask;

	if (!(pVBInfo->VBInfo & SetCRT2ToLCD))
		return;

	tempbx = pVBInfo->HDE; /* RHACTE=HDE-1 */

	if (XGI_IsLCDDualLink(pVBInfo))
		tempbx = tempbx >> 1;

	tempbx -= 1;
	temp = tempbx & 0x00FF;
	xgifb_reg_set(pVBInfo->Part2Port, 0x2C, temp);
	temp = (tempbx & 0xFF00) >> 8;
	temp = temp << 4;
	xgifb_reg_and_or(pVBInfo->Part2Port, 0x2B, 0x0F, temp);
	temp = 0x01;

	xgifb_reg_set(pVBInfo->Part2Port, 0x0B, temp);
	tempbx = pVBInfo->VDE; /* RTVACTEO=(VDE-1)&0xFF */
	push1 = tempbx;
	tempbx--;
	temp = tempbx & 0x00FF;
	xgifb_reg_set(pVBInfo->Part2Port, 0x03, temp);
	temp = ((tempbx & 0xFF00) >> 8) & 0x07;
	xgifb_reg_and_or(pVBInfo->Part2Port, 0x0C, ~0x07, temp);

	tempcx = pVBInfo->VT - 1;
	push2 = tempcx + 1;
	temp = tempcx & 0x00FF; /* RVTVT=VT-1 */
	xgifb_reg_set(pVBInfo->Part2Port, 0x19, temp);
	temp = (tempcx & 0xFF00) >> 8;
	temp = temp << 5;
	xgifb_reg_set(pVBInfo->Part2Port, 0x1A, temp);
	xgifb_reg_and_or(pVBInfo->Part2Port, 0x09, 0xF0, 0x00);
	xgifb_reg_and_or(pVBInfo->Part2Port, 0x0A, 0xF0, 0x00);
	xgifb_reg_and_or(pVBInfo->Part2Port, 0x17, 0xFB, 0x00);
	xgifb_reg_and_or(pVBInfo->Part2Port, 0x18, 0xDF, 0x00);

	/* Customized LCDB Des no add */
	tempbx = 5;
	LCDBDesPtr = (struct XGI_LCDDesStruct *) XGI_GetLcdPtr(tempbx, ModeNo,
			ModeIdIndex, RefreshRateTableIndex, pVBInfo);
	tempah = pVBInfo->LCDResInfo;
	tempah &= PanelResInfo;

	if ((tempah == Panel_1024x768) || (tempah == Panel_1024x768x75)) {
		tempbx = 1024;
		tempcx = 768;
	} else if ((tempah == Panel_1280x1024) ||
		   (tempah == Panel_1280x1024x75)) {
		tempbx = 1280;
		tempcx = 1024;
	} else if (tempah == Panel_1400x1050) {
		tempbx = 1400;
		tempcx = 1050;
	} else {
		tempbx = 1600;
		tempcx = 1200;
	}

	if (pVBInfo->LCDInfo & EnableScalingLCD) {
		tempbx = pVBInfo->HDE;
		tempcx = pVBInfo->VDE;
	}

	pushbx = tempbx;
	tempax = pVBInfo->VT;
	pVBInfo->LCDHDES = LCDBDesPtr->LCDHDES;
	pVBInfo->LCDHRS = LCDBDesPtr->LCDHRS;
	pVBInfo->LCDVDES = LCDBDesPtr->LCDVDES;
	pVBInfo->LCDVRS = LCDBDesPtr->LCDVRS;
	tempbx = pVBInfo->LCDVDES;
	tempcx += tempbx;

	if (tempcx >= tempax)
		tempcx -= tempax; /* lcdvdes */

	temp = tempbx & 0x00FF; /* RVEQ1EQ=lcdvdes */
	xgifb_reg_set(pVBInfo->Part2Port, 0x05, temp);
	temp = tempcx & 0x00FF;
	xgifb_reg_set(pVBInfo->Part2Port, 0x06, temp);
	tempch = ((tempcx & 0xFF00) >> 8) & 0x07;
	tempbh = ((tempbx & 0xFF00) >> 8) & 0x07;
	tempah = tempch;
	tempah = tempah << 3;
	tempah |= tempbh;
	xgifb_reg_set(pVBInfo->Part2Port, 0x02, tempah);

	/* getlcdsync() */
	XGI_GetLCDSync(&tempax, &tempbx, pVBInfo);
	tempcx = tempbx;
	tempax = pVBInfo->VT;
	tempbx = pVBInfo->LCDVRS;

	tempcx += tempbx;
	if (tempcx >= tempax)
		tempcx -= tempax;

	temp = tempbx & 0x00FF; /* RTVACTEE=lcdvrs */
	xgifb_reg_set(pVBInfo->Part2Port, 0x04, temp);
	temp = (tempbx & 0xFF00) >> 8;
	temp = temp << 4;
	temp |= (tempcx & 0x000F);
	xgifb_reg_set(pVBInfo->Part2Port, 0x01, temp);
	tempcx = pushbx;
	tempax = pVBInfo->HT;
	tempbx = pVBInfo->LCDHDES;
	tempbx &= 0x0FFF;

	if (XGI_IsLCDDualLink(pVBInfo)) {
		tempax = tempax >> 1;
		tempbx = tempbx >> 1;
		tempcx = tempcx >> 1;
	}

	if (pVBInfo->VBType & VB_SIS302LV)
		tempbx += 1;

	if (pVBInfo->VBType & VB_XGI301C) /* tap4 */
		tempbx += 1;

	tempcx += tempbx;

	if (tempcx >= tempax)
		tempcx -= tempax;

	temp = tempbx & 0x00FF;
	xgifb_reg_set(pVBInfo->Part2Port, 0x1F, temp); /* RHBLKE=lcdhdes */
	temp = ((tempbx & 0xFF00) >> 8) << 4;
	xgifb_reg_set(pVBInfo->Part2Port, 0x20, temp);
	temp = tempcx & 0x00FF;
	xgifb_reg_set(pVBInfo->Part2Port, 0x23, temp); /* RHEQPLE=lcdhdee */
	temp = (tempcx & 0xFF00) >> 8;
	xgifb_reg_set(pVBInfo->Part2Port, 0x25, temp);

	XGI_GetLCDSync(&tempax, &tempbx, pVBInfo);
	tempcx = tempax;
	tempax = pVBInfo->HT;
	tempbx = pVBInfo->LCDHRS;
	if (XGI_IsLCDDualLink(pVBInfo)) {
		tempax = tempax >> 1;
		tempbx = tempbx >> 1;
		tempcx = tempcx >> 1;
	}

	if (pVBInfo->VBType & VB_SIS302LV)
		tempbx += 1;

	tempcx += tempbx;

	if (tempcx >= tempax)
		tempcx -= tempax;

	temp = tempbx & 0x00FF; /* RHBURSTS=lcdhrs */
	xgifb_reg_set(pVBInfo->Part2Port, 0x1C, temp);

	temp = (tempbx & 0xFF00) >> 8;
	temp = temp << 4;
	xgifb_reg_and_or(pVBInfo->Part2Port, 0x1D, ~0x0F0, temp);
	temp = tempcx & 0x00FF; /* RHSYEXP2S=lcdhre */
	xgifb_reg_set(pVBInfo->Part2Port, 0x21, temp);

	if (!(pVBInfo->LCDInfo & XGI_LCDVESATiming)) {
		if (pVBInfo->VGAVDE == 525) {
			if (pVBInfo->VBType & (VB_SIS301B | VB_SIS302B
					| VB_SIS301LV | VB_SIS302LV
					| VB_XGI301C)) {
				temp = 0xC6;
			} else
				temp = 0xC4;

			xgifb_reg_set(pVBInfo->Part2Port, 0x2f, temp);
			xgifb_reg_set(pVBInfo->Part2Port, 0x30, 0xB3);
		}

		if (pVBInfo->VGAVDE == 420) {
			if (pVBInfo->VBType & (VB_SIS301B | VB_SIS302B
					| VB_SIS301LV | VB_SIS302LV
					| VB_XGI301C)) {
				temp = 0x4F;
			} else
				temp = 0x4E;
			xgifb_reg_set(pVBInfo->Part2Port, 0x2f, temp);
		}
	}
}

/* --------------------------------------------------------------------- */
/* Function : XGI_GetTap4Ptr */
/* Input : */
/* Output : di -> Tap4 Reg. Setting Pointer */
/* Description : */
/* --------------------------------------------------------------------- */
static struct XGI301C_Tap4TimingStruct *XGI_GetTap4Ptr(unsigned short tempcx,
		struct vb_device_info *pVBInfo)
{
	unsigned short tempax, tempbx, i;

	struct XGI301C_Tap4TimingStruct *Tap4TimingPtr;

	if (tempcx == 0) {
		tempax = pVBInfo->VGAHDE;
		tempbx = pVBInfo->HDE;
	} else {
		tempax = pVBInfo->VGAVDE;
		tempbx = pVBInfo->VDE;
	}

	if (tempax <= tempbx)
		return &xgifb_tap4_timing[0];
	else
		Tap4TimingPtr = xgifb_ntsc_525_tap4_timing; /* NTSC */

	if (pVBInfo->TVInfo & TVSetPAL)
		Tap4TimingPtr = PALTap4Timing;

	if (pVBInfo->VBInfo & SetCRT2ToYPbPr525750) {
		if ((pVBInfo->TVInfo & TVSetYPbPr525i) ||
			(pVBInfo->TVInfo & TVSetYPbPr525p))
			Tap4TimingPtr = xgifb_ntsc_525_tap4_timing;
		if (pVBInfo->TVInfo & TVSetYPbPr750p)
			Tap4TimingPtr = YPbPr750pTap4Timing;
	}

	if (pVBInfo->VBInfo & SetCRT2ToHiVision)
		Tap4TimingPtr = xgifb_tap4_timing;

	i = 0;
	while (Tap4TimingPtr[i].DE != 0xFFFF) {
		if (Tap4TimingPtr[i].DE == tempax)
			break;
		i++;
	}
	return &Tap4TimingPtr[i];
}

static void XGI_SetTap4Regs(struct vb_device_info *pVBInfo)
{
	unsigned short i, j;

	struct XGI301C_Tap4TimingStruct *Tap4TimingPtr;

	if (!(pVBInfo->VBType & VB_XGI301C))
		return;

	Tap4TimingPtr = XGI_GetTap4Ptr(0, pVBInfo); /* Set Horizontal Scaling */
	for (i = 0x80, j = 0; i <= 0xBF; i++, j++)
		xgifb_reg_set(pVBInfo->Part2Port, i, Tap4TimingPtr->Reg[j]);

	if ((pVBInfo->VBInfo & SetCRT2ToTV) &&
	    (!(pVBInfo->VBInfo & SetCRT2ToHiVision))) {
		/* Set Vertical Scaling */
		Tap4TimingPtr = XGI_GetTap4Ptr(1, pVBInfo);
		for (i = 0xC0, j = 0; i < 0xFF; i++, j++)
			xgifb_reg_set(pVBInfo->Part2Port,
				      i,
				      Tap4TimingPtr->Reg[j]);
	}

	if ((pVBInfo->VBInfo & SetCRT2ToTV) &&
	    (!(pVBInfo->VBInfo & SetCRT2ToHiVision)))
		/* Enable V.Scaling */
		xgifb_reg_and_or(pVBInfo->Part2Port, 0x4E, ~0x14, 0x04);
	else
		/* Enable H.Scaling */
		xgifb_reg_and_or(pVBInfo->Part2Port, 0x4E, ~0x14, 0x10);
}

static void XGI_SetGroup3(unsigned short ModeNo, unsigned short ModeIdIndex,
		struct vb_device_info *pVBInfo)
{
	unsigned short i;
	unsigned char *tempdi;
	unsigned short modeflag;

	/* si+Ext_ResInfo */
	modeflag = pVBInfo->EModeIDTable[ModeIdIndex].Ext_ModeFlag;

	xgifb_reg_set(pVBInfo->Part3Port, 0x00, 0x00);
	if (pVBInfo->TVInfo & TVSetPAL) {
		xgifb_reg_set(pVBInfo->Part3Port, 0x13, 0xFA);
		xgifb_reg_set(pVBInfo->Part3Port, 0x14, 0xC8);
	} else {
		xgifb_reg_set(pVBInfo->Part3Port, 0x13, 0xF5);
		xgifb_reg_set(pVBInfo->Part3Port, 0x14, 0xB7);
	}

	if (!(pVBInfo->VBInfo & SetCRT2ToTV))
		return;

	if (pVBInfo->TVInfo & TVSetPALM) {
		xgifb_reg_set(pVBInfo->Part3Port, 0x13, 0xFA);
		xgifb_reg_set(pVBInfo->Part3Port, 0x14, 0xC8);
		xgifb_reg_set(pVBInfo->Part3Port, 0x3D, 0xA8);
	}

	if ((pVBInfo->VBInfo & SetCRT2ToHiVision) || (pVBInfo->VBInfo
			& SetCRT2ToYPbPr525750)) {
		if (pVBInfo->TVInfo & TVSetYPbPr525i)
			return;

		tempdi = pVBInfo->HiTVGroup3Data;
		if (pVBInfo->SetFlag & TVSimuMode) {
			tempdi = pVBInfo->HiTVGroup3Simu;
			if (!(modeflag & Charx8Dot))
				tempdi = pVBInfo->HiTVGroup3Text;
		}

		if (pVBInfo->TVInfo & TVSetYPbPr525p)
			tempdi = pVBInfo->Ren525pGroup3;

		if (pVBInfo->TVInfo & TVSetYPbPr750p)
			tempdi = pVBInfo->Ren750pGroup3;

		for (i = 0; i <= 0x3E; i++)
			xgifb_reg_set(pVBInfo->Part3Port, i, tempdi[i]);

		if (pVBInfo->VBType & VB_XGI301C) { /* Marcovision */
			if (pVBInfo->TVInfo & TVSetYPbPr525p)
				xgifb_reg_set(pVBInfo->Part3Port, 0x28, 0x3f);
		}
	}
	return;
} /* {end of XGI_SetGroup3} */

static void XGI_SetGroup4(unsigned short ModeNo, unsigned short ModeIdIndex,
		unsigned short RefreshRateTableIndex,
		struct xgi_hw_device_info *HwDeviceExtension,
		struct vb_device_info *pVBInfo)
{
	unsigned short tempax, tempcx, tempbx, modeflag, temp, temp2;

	unsigned long tempebx, tempeax, templong;

	/* si+Ext_ResInfo */
	modeflag = pVBInfo->EModeIDTable[ModeIdIndex].Ext_ModeFlag;
	temp = pVBInfo->RVBHCFACT;
	xgifb_reg_set(pVBInfo->Part4Port, 0x13, temp);

	tempbx = pVBInfo->RVBHCMAX;
	temp = tempbx & 0x00FF;
	xgifb_reg_set(pVBInfo->Part4Port, 0x14, temp);
	temp2 = ((tempbx & 0xFF00) >> 8) << 7;
	tempcx = pVBInfo->VGAHT - 1;
	temp = tempcx & 0x00FF;
	xgifb_reg_set(pVBInfo->Part4Port, 0x16, temp);

	temp = ((tempcx & 0xFF00) >> 8) << 3;
	temp2 |= temp;

	tempcx = pVBInfo->VGAVT - 1;
	if (!(pVBInfo->VBInfo & SetCRT2ToTV))
		tempcx -= 5;

	temp = tempcx & 0x00FF;
	xgifb_reg_set(pVBInfo->Part4Port, 0x17, temp);
	temp = temp2 | ((tempcx & 0xFF00) >> 8);
	xgifb_reg_set(pVBInfo->Part4Port, 0x15, temp);
	xgifb_reg_or(pVBInfo->Part4Port, 0x0D, 0x08);
	tempcx = pVBInfo->VBInfo;
	tempbx = pVBInfo->VGAHDE;

	if (modeflag & HalfDCLK)
		tempbx = tempbx >> 1;

	if (XGI_IsLCDDualLink(pVBInfo))
		tempbx = tempbx >> 1;

	if (tempcx & SetCRT2ToHiVision) {
		temp = 0;
		if (tempbx <= 1024)
			temp = 0xA0;
		if (tempbx == 1280)
			temp = 0xC0;
	} else if (tempcx & SetCRT2ToTV) {
		temp = 0xA0;
		if (tempbx <= 800)
			temp = 0x80;
	} else {
		temp = 0x80;
		if (pVBInfo->VBInfo & SetCRT2ToLCD) {
			temp = 0;
			if (tempbx > 800)
				temp = 0x60;
		}
	}

	if (pVBInfo->TVInfo & (TVSetYPbPr525p | TVSetYPbPr750p)) {
		temp = 0x00;
		if (pVBInfo->VGAHDE == 1280)
			temp = 0x40;
		if (pVBInfo->VGAHDE == 1024)
			temp = 0x20;
	}
	xgifb_reg_and_or(pVBInfo->Part4Port, 0x0E, ~0xEF, temp);

	tempebx = pVBInfo->VDE;

	if (tempcx & SetCRT2ToHiVision) {
		if (!(temp & 0xE000))
			tempbx = tempbx >> 1;
	}

	tempcx = pVBInfo->RVBHRS;
	temp = tempcx & 0x00FF;
	xgifb_reg_set(pVBInfo->Part4Port, 0x18, temp);

	tempeax = pVBInfo->VGAVDE;
	tempcx |= 0x04000;

	if (tempeax <= tempebx) {
		tempcx = (tempcx & (~0x4000));
		tempeax = pVBInfo->VGAVDE;
	} else {
		tempeax -= tempebx;
	}

	templong = (tempeax * 256 * 1024) % tempebx;
	tempeax = (tempeax * 256 * 1024) / tempebx;
	tempebx = tempeax;

	if (templong != 0)
		tempebx++;

	temp = (unsigned short) (tempebx & 0x000000FF);
	xgifb_reg_set(pVBInfo->Part4Port, 0x1B, temp);

	temp = (unsigned short) ((tempebx & 0x0000FF00) >> 8);
	xgifb_reg_set(pVBInfo->Part4Port, 0x1A, temp);
	tempbx = (unsigned short) (tempebx >> 16);
	temp = tempbx & 0x00FF;
	temp = temp << 4;
	temp |= ((tempcx & 0xFF00) >> 8);
	xgifb_reg_set(pVBInfo->Part4Port, 0x19, temp);

	/* 301b */
	if (pVBInfo->VBType & (VB_SIS301B | VB_SIS302B | VB_SIS301LV
			| VB_SIS302LV | VB_XGI301C)) {
		temp = 0x0028;
		xgifb_reg_set(pVBInfo->Part4Port, 0x1C, temp);
		tempax = pVBInfo->VGAHDE;
		if (modeflag & HalfDCLK)
			tempax = tempax >> 1;

		if (XGI_IsLCDDualLink(pVBInfo))
			tempax = tempax >> 1;

		if (pVBInfo->VBInfo & SetCRT2ToLCD) {
			if (tempax > 800)
				tempax -= 800;
		} else {
			if (pVBInfo->VGAHDE > 800) {
				if (pVBInfo->VGAHDE == 1024)
					tempax = (tempax * 25 / 32) - 1;
				else
					tempax = (tempax * 20 / 32) - 1;
			}
		}
		tempax -= 1;

		temp = (tempax & 0xFF00) >> 8;
		temp = ((temp & 0x0003) << 4);
		xgifb_reg_set(pVBInfo->Part4Port, 0x1E, temp);
		temp = (tempax & 0x00FF);
		xgifb_reg_set(pVBInfo->Part4Port, 0x1D, temp);

		if (pVBInfo->VBInfo & (SetCRT2ToTV | SetCRT2ToHiVision)) {
			if (pVBInfo->VGAHDE > 800)
				xgifb_reg_or(pVBInfo->Part4Port, 0x1E, 0x08);

		}
		temp = 0x0036;

		if (pVBInfo->VBInfo & SetCRT2ToTV) {
			if (!(pVBInfo->TVInfo & (NTSC1024x768
					| TVSetYPbPr525p | TVSetYPbPr750p
					| TVSetHiVision))) {
				temp |= 0x0001;
				if ((pVBInfo->VBInfo & SetInSlaveMode)
						&& (!(pVBInfo->TVInfo
								& TVSimuMode)))
					temp &= (~0x0001);
			}
		}

		xgifb_reg_and_or(pVBInfo->Part4Port, 0x1F, 0x00C0, temp);
		tempbx = pVBInfo->HT;
		if (XGI_IsLCDDualLink(pVBInfo))
			tempbx = tempbx >> 1;
		tempbx = (tempbx >> 1) - 2;
		temp = ((tempbx & 0x0700) >> 8) << 3;
		xgifb_reg_and_or(pVBInfo->Part4Port, 0x21, 0x00C0, temp);
		temp = tempbx & 0x00FF;
		xgifb_reg_set(pVBInfo->Part4Port, 0x22, temp);
	}
	/* end 301b */

	if (pVBInfo->ISXPDOS == 0)
		XGI_SetCRT2VCLK(ModeNo, ModeIdIndex, RefreshRateTableIndex,
				pVBInfo);
}

static void XGINew_EnableCRT2(struct vb_device_info *pVBInfo)
{
	xgifb_reg_and_or(pVBInfo->P3c4, 0x1E, 0xFF, 0x20);
}

static void XGI_SetGroup5(unsigned short ModeNo, unsigned short ModeIdIndex,
		struct vb_device_info *pVBInfo)
{
	unsigned short Pindex, Pdata;

	Pindex = pVBInfo->Part5Port;
	Pdata = pVBInfo->Part5Port + 1;
	if (pVBInfo->ModeType == ModeVGA) {
		if (!(pVBInfo->VBInfo & (SetInSlaveMode | LoadDACFlag
				| DisableCRT2Display))) {
			XGINew_EnableCRT2(pVBInfo);
		}
	}
	return;
}

static void XGI_EnableGatingCRT(struct xgi_hw_device_info *HwDeviceExtension,
		struct vb_device_info *pVBInfo)
{
	xgifb_reg_and_or(pVBInfo->P3d4, 0x63, 0xBF, 0x40);
}

static void XGI_DisableGatingCRT(struct xgi_hw_device_info *HwDeviceExtension,
		struct vb_device_info *pVBInfo)
{

	xgifb_reg_and_or(pVBInfo->P3d4, 0x63, 0xBF, 0x00);
}

static unsigned char XGI_XG21CheckLVDSMode(struct xgifb_video_info *xgifb_info,
		unsigned short ModeNo, unsigned short ModeIdIndex,
		struct vb_device_info *pVBInfo)
{
	unsigned short xres, yres, colordepth, modeflag, resindex;

	resindex = pVBInfo->EModeIDTable[ModeIdIndex].Ext_RESINFO;
	xres = pVBInfo->ModeResInfo[resindex].HTotal; /* xres->ax */
	yres = pVBInfo->ModeResInfo[resindex].VTotal; /* yres->bx */
	/* si+St_ModeFlag */
	modeflag = pVBInfo->EModeIDTable[ModeIdIndex].Ext_ModeFlag;

	if (!(modeflag & Charx8Dot)) {
		xres /= 9;
		xres *= 8;
	}

	if ((ModeNo > 0x13) && (modeflag & HalfDCLK))
		xres *= 2;

	if ((ModeNo > 0x13) && (modeflag & DoubleScanMode))
		yres *= 2;

	if (xres > xgifb_info->lvds_data.LVDSHDE)
		return 0;

	if (yres > xgifb_info->lvds_data.LVDSVDE)
		return 0;

	if (xres != xgifb_info->lvds_data.LVDSHDE ||
	    yres != xgifb_info->lvds_data.LVDSVDE) {
		colordepth = XGI_GetColorDepth(ModeNo, ModeIdIndex, pVBInfo);
		if (colordepth > 2)
			return 0;
	}
	return 1;
}

static void xgifb_set_lvds(struct xgifb_video_info *xgifb_info,
			   int chip_id,
			   unsigned short ModeNo,
			   unsigned short ModeIdIndex,
			   struct vb_device_info *pVBInfo)
{
	unsigned char temp, Miscdata;
	unsigned short xres, yres, modeflag, resindex;
	unsigned short LVDSHT, LVDSHBS, LVDSHRS, LVDSHRE, LVDSHBE;
	unsigned short LVDSVT, LVDSVBS, LVDSVRS, LVDSVRE, LVDSVBE;
	unsigned short value;

	temp = (unsigned char) ((xgifb_info->lvds_data.LVDS_Capability &
				(LCDPolarity << 8)) >> 8);
	temp &= LCDPolarity;
	Miscdata = (unsigned char) inb(pVBInfo->P3cc);

	outb((Miscdata & 0x3F) | temp, pVBInfo->P3c2);

	temp = xgifb_info->lvds_data.LVDS_Capability & LCDPolarity;
	/* SR35[7] FP VSync polarity */
	xgifb_reg_and_or(pVBInfo->P3c4, 0x35, ~0x80, temp & 0x80);
	/* SR30[5] FP HSync polarity */
	xgifb_reg_and_or(pVBInfo->P3c4, 0x30, ~0x20, (temp & 0x40) >> 1);

	if (chip_id == XG27)
		XGI_SetXG27FPBits(pVBInfo);
	else
		XGI_SetXG21FPBits(pVBInfo);

	resindex = pVBInfo->EModeIDTable[ModeIdIndex].Ext_RESINFO;
	xres = pVBInfo->ModeResInfo[resindex].HTotal; /* xres->ax */
	yres = pVBInfo->ModeResInfo[resindex].VTotal; /* yres->bx */
	/* si+St_ModeFlag */
	modeflag = pVBInfo->EModeIDTable[ModeIdIndex].Ext_ModeFlag;

	if (!(modeflag & Charx8Dot))
		xres = xres * 8 / 9;

	LVDSHT = xgifb_info->lvds_data.LVDSHT;

	LVDSHBS = xres + (xgifb_info->lvds_data.LVDSHDE - xres) / 2;

	if (LVDSHBS > LVDSHT)
		LVDSHBS -= LVDSHT;

	LVDSHRS = LVDSHBS + xgifb_info->lvds_data.LVDSHFP;
	if (LVDSHRS > LVDSHT)
		LVDSHRS -= LVDSHT;

	LVDSHRE = LVDSHRS + xgifb_info->lvds_data.LVDSHSYNC;
	if (LVDSHRE > LVDSHT)
		LVDSHRE -= LVDSHT;

	LVDSHBE = LVDSHBS + LVDSHT - xgifb_info->lvds_data.LVDSHDE;

	LVDSVT = xgifb_info->lvds_data.LVDSVT;

	LVDSVBS = yres + (xgifb_info->lvds_data.LVDSVDE - yres) / 2;
	if (modeflag & DoubleScanMode)
		LVDSVBS += yres / 2;

	if (LVDSVBS > LVDSVT)
		LVDSVBS -= LVDSVT;

	LVDSVRS = LVDSVBS + xgifb_info->lvds_data.LVDSVFP;
	if (LVDSVRS > LVDSVT)
		LVDSVRS -= LVDSVT;

	LVDSVRE = LVDSVRS + xgifb_info->lvds_data.LVDSVSYNC;
	if (LVDSVRE > LVDSVT)
		LVDSVRE -= LVDSVT;

	LVDSVBE = LVDSVBS + LVDSVT - xgifb_info->lvds_data.LVDSVDE;

	temp = (unsigned char) xgifb_reg_get(pVBInfo->P3d4, 0x11);
	xgifb_reg_set(pVBInfo->P3d4, 0x11, temp & 0x7f); /* Unlock CRTC */

	if (!(modeflag & Charx8Dot))
		xgifb_reg_or(pVBInfo->P3c4, 0x1, 0x1);

	/* HT SR0B[1:0] CR00 */
	value = (LVDSHT >> 3) - 5;
	xgifb_reg_and_or(pVBInfo->P3c4, 0x0B, ~0x03, (value & 0x300) >> 8);
	xgifb_reg_set(pVBInfo->P3d4, 0x0, (value & 0xFF));

	/* HBS SR0B[5:4] CR02 */
	value = (LVDSHBS >> 3) - 1;
	xgifb_reg_and_or(pVBInfo->P3c4, 0x0B, ~0x30, (value & 0x300) >> 4);
	xgifb_reg_set(pVBInfo->P3d4, 0x2, (value & 0xFF));

	/* HBE SR0C[1:0] CR05[7] CR03[4:0] */
	value = (LVDSHBE >> 3) - 1;
	xgifb_reg_and_or(pVBInfo->P3c4, 0x0C, ~0x03, (value & 0xC0) >> 6);
	xgifb_reg_and_or(pVBInfo->P3d4, 0x05, ~0x80, (value & 0x20) << 2);
	xgifb_reg_and_or(pVBInfo->P3d4, 0x03, ~0x1F, value & 0x1F);

	/* HRS SR0B[7:6] CR04 */
	value = (LVDSHRS >> 3) + 2;
	xgifb_reg_and_or(pVBInfo->P3c4, 0x0B, ~0xC0, (value & 0x300) >> 2);
	xgifb_reg_set(pVBInfo->P3d4, 0x4, (value & 0xFF));

	/* Panel HRS SR2F[1:0] SR2E[7:0]  */
	value--;
	xgifb_reg_and_or(pVBInfo->P3c4, 0x2F, ~0x03, (value & 0x300) >> 8);
	xgifb_reg_set(pVBInfo->P3c4, 0x2E, (value & 0xFF));

	/* HRE SR0C[2] CR05[4:0] */
	value = (LVDSHRE >> 3) + 2;
	xgifb_reg_and_or(pVBInfo->P3c4, 0x0C, ~0x04, (value & 0x20) >> 3);
	xgifb_reg_and_or(pVBInfo->P3d4, 0x05, ~0x1F, value & 0x1F);

	/* Panel HRE SR2F[7:2]  */
	value--;
	xgifb_reg_and_or(pVBInfo->P3c4, 0x2F, ~0xFC, value << 2);

	/* VT SR0A[0] CR07[5][0] CR06 */
	value = LVDSVT - 2;
	xgifb_reg_and_or(pVBInfo->P3c4, 0x0A, ~0x01, (value & 0x400) >> 10);
	xgifb_reg_and_or(pVBInfo->P3d4, 0x07, ~0x20, (value & 0x200) >> 4);
	xgifb_reg_and_or(pVBInfo->P3d4, 0x07, ~0x01, (value & 0x100) >> 8);
	xgifb_reg_set(pVBInfo->P3d4, 0x06, (value & 0xFF));

	/* VBS SR0A[2] CR09[5] CR07[3] CR15 */
	value = LVDSVBS - 1;
	xgifb_reg_and_or(pVBInfo->P3c4, 0x0A, ~0x04, (value & 0x400) >> 8);
	xgifb_reg_and_or(pVBInfo->P3d4, 0x09, ~0x20, (value & 0x200) >> 4);
	xgifb_reg_and_or(pVBInfo->P3d4, 0x07, ~0x08, (value & 0x100) >> 5);
	xgifb_reg_set(pVBInfo->P3d4, 0x15, (value & 0xFF));

	/* VBE SR0A[4] CR16 */
	value = LVDSVBE - 1;
	xgifb_reg_and_or(pVBInfo->P3c4, 0x0A, ~0x10, (value & 0x100) >> 4);
	xgifb_reg_set(pVBInfo->P3d4, 0x16, (value & 0xFF));

	/* VRS SR0A[3] CR7[7][2] CR10 */
	value = LVDSVRS - 1;
	xgifb_reg_and_or(pVBInfo->P3c4, 0x0A, ~0x08, (value & 0x400) >> 7);
	xgifb_reg_and_or(pVBInfo->P3d4, 0x07, ~0x80, (value & 0x200) >> 2);
	xgifb_reg_and_or(pVBInfo->P3d4, 0x07, ~0x04, (value & 0x100) >> 6);
	xgifb_reg_set(pVBInfo->P3d4, 0x10, (value & 0xFF));

	if (chip_id == XG27) {
		/* Panel VRS SR35[2:0] SR34[7:0] */
		xgifb_reg_and_or(pVBInfo->P3c4, 0x35, ~0x07,
					(value & 0x700) >> 8);
		xgifb_reg_set(pVBInfo->P3c4, 0x34, value & 0xFF);
	} else {
		/* Panel VRS SR3F[1:0] SR34[7:0] SR33[0] */
		xgifb_reg_and_or(pVBInfo->P3c4, 0x3F, ~0x03,
					(value & 0x600) >> 9);
		xgifb_reg_set(pVBInfo->P3c4, 0x34, (value >> 1) & 0xFF);
		xgifb_reg_and_or(pVBInfo->P3d4, 0x33, ~0x01, value & 0x01);
	}

	/* VRE SR0A[5] CR11[3:0] */
	value = LVDSVRE - 1;
	xgifb_reg_and_or(pVBInfo->P3c4, 0x0A, ~0x20, (value & 0x10) << 1);
	xgifb_reg_and_or(pVBInfo->P3d4, 0x11, ~0x0F, value & 0x0F);

	/* Panel VRE SR3F[7:2] */
	if (chip_id == XG27)
		xgifb_reg_and_or(pVBInfo->P3c4, 0x3F, ~0xFC,
					(value << 2) & 0xFC);
	else
		/* SR3F[7] has to be 0, h/w bug */
		xgifb_reg_and_or(pVBInfo->P3c4, 0x3F, ~0xFC,
					(value << 2) & 0x7C);

	for (temp = 0, value = 0; temp < 3; temp++) {

		xgifb_reg_and_or(pVBInfo->P3c4, 0x31, ~0x30, value);
		xgifb_reg_set(pVBInfo->P3c4,
			      0x2B, xgifb_info->lvds_data.VCLKData1);
		xgifb_reg_set(pVBInfo->P3c4,
			      0x2C, xgifb_info->lvds_data.VCLKData2);
		value += 0x10;
	}

	if (!(modeflag & Charx8Dot)) {
		inb(pVBInfo->P3da); /* reset 3da */
		outb(0x13, pVBInfo->P3c0); /* set index */
		/* set data, panning = 0, shift left 1 dot*/
		outb(0x00, pVBInfo->P3c0);

		inb(pVBInfo->P3da); /* Enable Attribute */
		outb(0x20, pVBInfo->P3c0);

		inb(pVBInfo->P3da); /* reset 3da */
	}

}

/* --------------------------------------------------------------------- */
/* Function : XGI_IsLCDON */
/* Input : */
/* Output : 0 : Skip PSC Control */
/* 1: Disable PSC */
/* Description : */
/* --------------------------------------------------------------------- */
static unsigned char XGI_IsLCDON(struct vb_device_info *pVBInfo)
{
	unsigned short tempax;

	tempax = pVBInfo->VBInfo;
	if (tempax & SetCRT2ToDualEdge)
		return 0;
	else if (tempax & (DisableCRT2Display | SwitchCRT2 | SetSimuScanMode))
		return 1;

	return 0;
}

/* --------------------------------------------------------------------- */
/* Function : XGI_DisableChISLCD */
/* Input : */
/* Output : 0 -> Not LCD Mode */
/* Description : */
/* --------------------------------------------------------------------- */
static unsigned char XGI_DisableChISLCD(struct vb_device_info *pVBInfo)
{
	unsigned short tempbx, tempah;

	tempbx = pVBInfo->SetFlag & (DisableChA | DisableChB);
	tempah = ~((unsigned short) xgifb_reg_get(pVBInfo->Part1Port, 0x2E));

	if (tempbx & (EnableChA | DisableChA)) {
		if (!(tempah & 0x08)) /* Chk LCDA Mode */
			return 0;
	}

	if (!(tempbx & (EnableChB | DisableChB)))
		return 0;

	if (tempah & 0x01) /* Chk LCDB Mode */
		return 1;

	return 0;
}

/* --------------------------------------------------------------------- */
/* Function : XGI_EnableChISLCD */
/* Input : */
/* Output : 0 -> Not LCD mode */
/* Description : */
/* --------------------------------------------------------------------- */
static unsigned char XGI_EnableChISLCD(struct vb_device_info *pVBInfo)
{
	unsigned short tempbx, tempah;

	tempbx = pVBInfo->SetFlag & (EnableChA | EnableChB);
	tempah = ~((unsigned short) xgifb_reg_get(pVBInfo->Part1Port, 0x2E));

	if (tempbx & (EnableChA | DisableChA)) {
		if (!(tempah & 0x08)) /* Chk LCDA Mode */
			return 0;
	}

	if (!(tempbx & (EnableChB | DisableChB)))
		return 0;

	if (tempah & 0x01) /* Chk LCDB Mode */
		return 1;

	return 0;
}

static void XGI_DisableBridge(struct xgifb_video_info *xgifb_info,
		struct xgi_hw_device_info *HwDeviceExtension,
		struct vb_device_info *pVBInfo)
{
	unsigned short tempah = 0;

	if (pVBInfo->VBType & (VB_SIS301B | VB_SIS302B | VB_SIS301LV
			| VB_SIS302LV | VB_XGI301C)) {
		tempah = 0x3F;
		if (!(pVBInfo->VBInfo &
		    (DisableCRT2Display | SetSimuScanMode))) {
			if (pVBInfo->VBInfo & XGI_SetCRT2ToLCDA) {
				if (pVBInfo->VBInfo & SetCRT2ToDualEdge) {
					tempah = 0x7F; /* Disable Channel A */
					if (!(pVBInfo->VBInfo &
					      XGI_SetCRT2ToLCDA))
						/* Disable Channel B */
						tempah = 0xBF;

					if (pVBInfo->SetFlag & DisableChB)
						/* force to disable Cahnnel */
						tempah &= 0xBF;

					if (pVBInfo->SetFlag & DisableChA)
						/* Force to disable Channel B */
						tempah &= 0x7F;
				}
			}
		}

		/* disable part4_1f */
		xgifb_reg_and(pVBInfo->Part4Port, 0x1F, tempah);

		if (pVBInfo->VBType & (VB_SIS302LV | VB_XGI301C)) {
			if (((pVBInfo->VBInfo &
			      (SetCRT2ToLCD | XGI_SetCRT2ToLCDA)))
			    || (XGI_DisableChISLCD(pVBInfo))
			    || (XGI_IsLCDON(pVBInfo)))
				/* LVDS Driver power down */
				xgifb_reg_or(pVBInfo->Part4Port, 0x30, 0x80);
		}

		if ((pVBInfo->SetFlag & DisableChA) || (pVBInfo->VBInfo
				& (DisableCRT2Display | XGI_SetCRT2ToLCDA
						| SetSimuScanMode))) {
			if (pVBInfo->SetFlag & GatingCRT)
				XGI_EnableGatingCRT(HwDeviceExtension, pVBInfo);
			XGI_DisplayOff(xgifb_info, HwDeviceExtension, pVBInfo);
		}

		if (pVBInfo->VBInfo & XGI_SetCRT2ToLCDA) {
			if ((pVBInfo->SetFlag & DisableChA) || (pVBInfo->VBInfo
					& XGI_SetCRT2ToLCDA))
				/* Power down */
				xgifb_reg_and(pVBInfo->Part1Port, 0x1e, 0xdf);
		}

		/* disable TV as primary VGA swap */
		xgifb_reg_and(pVBInfo->P3c4, 0x32, 0xdf);

		if ((pVBInfo->VBInfo & (SetSimuScanMode | SetCRT2ToDualEdge)))
			xgifb_reg_and(pVBInfo->Part2Port, 0x00, 0xdf);

		if ((pVBInfo->SetFlag & DisableChB) ||
		    (pVBInfo->VBInfo &
			(DisableCRT2Display | SetSimuScanMode)) ||
		    ((!(pVBInfo->VBInfo & XGI_SetCRT2ToLCDA)) &&
		    (pVBInfo->VBInfo &
			(SetCRT2ToRAMDAC | SetCRT2ToLCD | SetCRT2ToTV))))
			xgifb_reg_or(pVBInfo->Part1Port, 0x00, 0x80);

		if ((pVBInfo->SetFlag & DisableChB) ||
		    (pVBInfo->VBInfo &
			(DisableCRT2Display | SetSimuScanMode)) ||
		    (!(pVBInfo->VBInfo & XGI_SetCRT2ToLCDA)) ||
		    (pVBInfo->VBInfo &
			(SetCRT2ToRAMDAC | SetCRT2ToLCD | SetCRT2ToTV))) {
			/* save Part1 index 0 */
			tempah = xgifb_reg_get(pVBInfo->Part1Port, 0x00);
			/* BTDAC = 1, avoid VB reset */
			xgifb_reg_or(pVBInfo->Part1Port, 0x00, 0x10);
			/* disable CRT2 */
			xgifb_reg_and(pVBInfo->Part1Port, 0x1E, 0xDF);
			/* restore Part1 index 0 */
			xgifb_reg_set(pVBInfo->Part1Port, 0x00, tempah);
		}
	} else { /* {301} */
		if (pVBInfo->VBInfo & (SetCRT2ToLCD | SetCRT2ToTV)) {
			xgifb_reg_or(pVBInfo->Part1Port, 0x00, 0x80);
			/* Disable CRT2 */
			xgifb_reg_and(pVBInfo->Part1Port, 0x1E, 0xDF);
			/* Disable TV asPrimary VGA swap */
			xgifb_reg_and(pVBInfo->P3c4, 0x32, 0xDF);
		}

		if (pVBInfo->VBInfo & (DisableCRT2Display | XGI_SetCRT2ToLCDA
				| SetSimuScanMode))
			XGI_DisplayOff(xgifb_info, HwDeviceExtension, pVBInfo);
	}
}

/* --------------------------------------------------------------------- */
/* Function : XGI_GetTVPtrIndex */
/* Input : */
/* Output : */
/* Description : bx 0 : ExtNTSC */
/* 1 : StNTSC */
/* 2 : ExtPAL */
/* 3 : StPAL */
/* 4 : ExtHiTV */
/* 5 : StHiTV */
/* 6 : Ext525i */
/* 7 : St525i */
/* 8 : Ext525p */
/* 9 : St525p */
/* A : Ext750p */
/* B : St750p */
/* --------------------------------------------------------------------- */
static unsigned short XGI_GetTVPtrIndex(struct vb_device_info *pVBInfo)
{
	unsigned short tempbx = 0;

	if (pVBInfo->TVInfo & TVSetPAL)
		tempbx = 2;
	if (pVBInfo->TVInfo & TVSetHiVision)
		tempbx = 4;
	if (pVBInfo->TVInfo & TVSetYPbPr525i)
		tempbx = 6;
	if (pVBInfo->TVInfo & TVSetYPbPr525p)
		tempbx = 8;
	if (pVBInfo->TVInfo & TVSetYPbPr750p)
		tempbx = 10;
	if (pVBInfo->TVInfo & TVSimuMode)
		tempbx++;

	return tempbx;
}

/* --------------------------------------------------------------------- */
/* Function : XGI_GetTVPtrIndex2 */
/* Input : */
/* Output : bx 0 : NTSC */
/* 1 : PAL */
/* 2 : PALM */
/* 3 : PALN */
/* 4 : NTSC1024x768 */
/* 5 : PAL-M 1024x768 */
/* 6-7: reserved */
/* cl 0 : YFilter1 */
/* 1 : YFilter2 */
/* ch 0 : 301A */
/* 1 : 301B/302B/301LV/302LV */
/* Description : */
/* --------------------------------------------------------------------- */
static void XGI_GetTVPtrIndex2(unsigned short *tempbx, unsigned char *tempcl,
		unsigned char *tempch, struct vb_device_info *pVBInfo)
{
	*tempbx = 0;
	*tempcl = 0;
	*tempch = 0;

	if (pVBInfo->TVInfo & TVSetPAL)
		*tempbx = 1;

	if (pVBInfo->TVInfo & TVSetPALM)
		*tempbx = 2;

	if (pVBInfo->TVInfo & TVSetPALN)
		*tempbx = 3;

	if (pVBInfo->TVInfo & NTSC1024x768) {
		*tempbx = 4;
		if (pVBInfo->TVInfo & TVSetPALM)
			*tempbx = 5;
	}

	if (pVBInfo->VBType & (VB_SIS301B | VB_SIS302B | VB_SIS301LV
			| VB_SIS302LV | VB_XGI301C)) {
		if ((!(pVBInfo->VBInfo & SetInSlaveMode)) || (pVBInfo->TVInfo
				& TVSimuMode)) {
			*tempbx += 8;
			*tempcl += 1;
		}
	}

	if (pVBInfo->VBType & (VB_SIS301B | VB_SIS302B | VB_SIS301LV
			| VB_SIS302LV | VB_XGI301C))
		(*tempch)++;
}

static void XGI_SetDelayComp(struct vb_device_info *pVBInfo)
{
	unsigned char tempah, tempbl, tempbh;

	if (pVBInfo->VBType & (VB_SIS301B | VB_SIS302B | VB_SIS301LV
			| VB_SIS302LV | VB_XGI301C)) {
		if (pVBInfo->VBInfo & (SetCRT2ToLCD | XGI_SetCRT2ToLCDA
				| SetCRT2ToTV | SetCRT2ToRAMDAC)) {
			tempbh = 0;
			tempbl = XGI301TVDelay;

			if (pVBInfo->VBInfo & SetCRT2ToDualEdge)
				tempbl = tempbl >> 4;
			if (pVBInfo->VBInfo &
			    (SetCRT2ToLCD | XGI_SetCRT2ToLCDA)) {
				tempbh = XGI301LCDDelay;

				if (!(pVBInfo->VBInfo & XGI_SetCRT2ToLCDA))
					tempbl = tempbh;
			}

			tempbl &= 0x0F;
			tempbh &= 0xF0;
			tempah = xgifb_reg_get(pVBInfo->Part1Port, 0x2D);

			if (pVBInfo->VBInfo & (SetCRT2ToRAMDAC | SetCRT2ToLCD
					| SetCRT2ToTV)) { /* Channel B */
				tempah &= 0xF0;
				tempah |= tempbl;
			}

			if (pVBInfo->VBInfo & XGI_SetCRT2ToLCDA) {
				/* Channel A */
				tempah &= 0x0F;
				tempah |= tempbh;
			}
			xgifb_reg_set(pVBInfo->Part1Port, 0x2D, tempah);
		}
	} else if (pVBInfo->IF_DEF_LVDS == 1) {
		tempbl = 0;
		tempbh = 0;
		if (pVBInfo->VBInfo & SetCRT2ToLCD) {
			tempah = XGI301LCDDelay;
			tempah &= 0x0f;
			tempah = tempah << 4;
			xgifb_reg_and_or(pVBInfo->Part1Port, 0x2D, 0x0f,
					tempah);
		}
	}
}

static void XGI_SetLCDCap_A(unsigned short tempcx,
			    struct vb_device_info *pVBInfo)
{
	unsigned short temp;

	temp = xgifb_reg_get(pVBInfo->P3d4, 0x37);

	if (temp & LCDRGB18Bit) {
		xgifb_reg_and_or(pVBInfo->Part1Port, 0x19, 0x0F,
				/* Enable Dither */
				(unsigned short) (0x20 | (tempcx & 0x00C0)));
		xgifb_reg_and_or(pVBInfo->Part1Port, 0x1A, 0x7F, 0x80);
	} else {
		xgifb_reg_and_or(pVBInfo->Part1Port, 0x19, 0x0F,
				(unsigned short) (0x30 | (tempcx & 0x00C0)));
		xgifb_reg_and_or(pVBInfo->Part1Port, 0x1A, 0x7F, 0x00);
	}
}

/* --------------------------------------------------------------------- */
/* Function : XGI_SetLCDCap_B */
/* Input : cx -> LCD Capability */
/* Output : */
/* Description : */
/* --------------------------------------------------------------------- */
static void XGI_SetLCDCap_B(unsigned short tempcx,
			    struct vb_device_info *pVBInfo)
{
	if (tempcx & EnableLCD24bpp) /* 24bits */
		xgifb_reg_and_or(pVBInfo->Part2Port, 0x1A, 0xE0,
				(unsigned short) (((tempcx & 0x00ff) >> 6)
						| 0x0c));
	else
		xgifb_reg_and_or(pVBInfo->Part2Port, 0x1A, 0xE0,
				(unsigned short) (((tempcx & 0x00ff) >> 6)
						| 0x18)); /* Enable Dither */
}

static void XGI_LongWait(struct vb_device_info *pVBInfo)
{
	unsigned short i;

	i = xgifb_reg_get(pVBInfo->P3c4, 0x1F);

	if (!(i & 0xC0)) {
		for (i = 0; i < 0xFFFF; i++) {
			if (!(inb(pVBInfo->P3da) & 0x08))
				break;
		}

		for (i = 0; i < 0xFFFF; i++) {
			if ((inb(pVBInfo->P3da) & 0x08))
				break;
		}
	}
}

static void SetSpectrum(struct vb_device_info *pVBInfo)
{
	unsigned short index;

	index = XGI_GetLCDCapPtr(pVBInfo);

	/* disable down spectrum D[4] */
	xgifb_reg_and(pVBInfo->Part4Port, 0x30, 0x8F);
	XGI_LongWait(pVBInfo);
	xgifb_reg_or(pVBInfo->Part4Port, 0x30, 0x20); /* reset spectrum */
	XGI_LongWait(pVBInfo);

	xgifb_reg_set(pVBInfo->Part4Port, 0x31,
			pVBInfo->LCDCapList[index].Spectrum_31);
	xgifb_reg_set(pVBInfo->Part4Port, 0x32,
			pVBInfo->LCDCapList[index].Spectrum_32);
	xgifb_reg_set(pVBInfo->Part4Port, 0x33,
			pVBInfo->LCDCapList[index].Spectrum_33);
	xgifb_reg_set(pVBInfo->Part4Port, 0x34,
			pVBInfo->LCDCapList[index].Spectrum_34);
	XGI_LongWait(pVBInfo);
	xgifb_reg_or(pVBInfo->Part4Port, 0x30, 0x40); /* enable spectrum */
}

static void XGI_SetLCDCap(struct vb_device_info *pVBInfo)
{
	unsigned short tempcx;

	tempcx = pVBInfo->LCDCapList[XGI_GetLCDCapPtr(pVBInfo)].LCD_Capability;

	if (pVBInfo->VBType &
	    (VB_SIS301B |
	     VB_SIS302B |
	     VB_SIS301LV |
	     VB_SIS302LV |
	     VB_XGI301C)) { /* 301LV/302LV only */
		if (pVBInfo->VBType &
		    (VB_SIS301LV | VB_SIS302LV | VB_XGI301C)) {
			/* Set 301LV Capability */
			xgifb_reg_set(pVBInfo->Part4Port, 0x24,
					(unsigned char) (tempcx & 0x1F));
		}
		/* VB Driving */
		xgifb_reg_and_or(pVBInfo->Part4Port, 0x0D,
				~((EnableVBCLKDRVLOW | EnablePLLSPLOW) >> 8),
				(unsigned short) ((tempcx & (EnableVBCLKDRVLOW
						| EnablePLLSPLOW)) >> 8));
	}

	if (pVBInfo->VBType & (VB_SIS301B | VB_SIS302B | VB_SIS301LV
			| VB_SIS302LV | VB_XGI301C)) {
		if (pVBInfo->VBInfo & SetCRT2ToLCD)
			XGI_SetLCDCap_B(tempcx, pVBInfo);
		else if (pVBInfo->VBInfo & XGI_SetCRT2ToLCDA)
			XGI_SetLCDCap_A(tempcx, pVBInfo);

		if (pVBInfo->VBType & (VB_SIS302LV | VB_XGI301C)) {
			if (tempcx & EnableSpectrum)
				SetSpectrum(pVBInfo);
		}
	} else {
		/* LVDS,CH7017 */
		XGI_SetLCDCap_A(tempcx, pVBInfo);
	}
}

/* --------------------------------------------------------------------- */
/* Function : XGI_SetAntiFlicker */
/* Input : */
/* Output : */
/* Description : Set TV Customized Param. */
/* --------------------------------------------------------------------- */
static void XGI_SetAntiFlicker(unsigned short ModeNo,
			       unsigned short ModeIdIndex,
			       struct vb_device_info *pVBInfo)
{
	unsigned short tempbx;

	unsigned char tempah;

	if (pVBInfo->TVInfo & (TVSetYPbPr525p | TVSetYPbPr750p))
		return;

	tempbx = XGI_GetTVPtrIndex(pVBInfo);
	tempbx &= 0xFE;
	tempah = TVAntiFlickList[tempbx];
	tempah = tempah << 4;

	xgifb_reg_and_or(pVBInfo->Part2Port, 0x0A, 0x8F, tempah);
}

static void XGI_SetEdgeEnhance(unsigned short ModeNo,
			       unsigned short ModeIdIndex,
			       struct vb_device_info *pVBInfo)
{
	unsigned short tempbx;

	unsigned char tempah;

	tempbx = XGI_GetTVPtrIndex(pVBInfo);
	tempbx &= 0xFE;
	tempah = TVEdgeList[tempbx];
	tempah = tempah << 5;

	xgifb_reg_and_or(pVBInfo->Part2Port, 0x3A, 0x1F, tempah);
}

static void XGI_SetPhaseIncr(struct vb_device_info *pVBInfo)
{
	unsigned short tempbx;

	unsigned char tempcl, tempch;

	unsigned long tempData;

	XGI_GetTVPtrIndex2(&tempbx, &tempcl, &tempch, pVBInfo); /* bx, cl, ch */
	tempData = TVPhaseList[tempbx];

	xgifb_reg_set(pVBInfo->Part2Port, 0x31, (unsigned short) (tempData
			& 0x000000FF));
	xgifb_reg_set(pVBInfo->Part2Port, 0x32, (unsigned short) ((tempData
			& 0x0000FF00) >> 8));
	xgifb_reg_set(pVBInfo->Part2Port, 0x33, (unsigned short) ((tempData
			& 0x00FF0000) >> 16));
	xgifb_reg_set(pVBInfo->Part2Port, 0x34, (unsigned short) ((tempData
			& 0xFF000000) >> 24));
}

static void XGI_SetYFilter(unsigned short ModeNo, unsigned short ModeIdIndex,
		struct vb_device_info *pVBInfo)
{
	unsigned short tempbx, index;

	unsigned char tempcl, tempch, tempal, *filterPtr;

	XGI_GetTVPtrIndex2(&tempbx, &tempcl, &tempch, pVBInfo); /* bx, cl, ch */

	switch (tempbx) {
	case 0x00:
	case 0x04:
		filterPtr = NTSCYFilter1;
		break;

	case 0x01:
		filterPtr = PALYFilter1;
		break;

	case 0x02:
	case 0x05:
	case 0x0D:
	case 0x03:
		filterPtr = xgifb_palmn_yfilter1;
		break;

	case 0x08:
	case 0x0C:
	case 0x0A:
	case 0x0B:
	case 0x09:
		filterPtr = xgifb_yfilter2;
		break;

	default:
		return;
	}

	tempal = pVBInfo->EModeIDTable[ModeIdIndex].VB_ExtTVYFilterIndex;
	if (tempcl == 0)
		index = tempal * 4;
	else
		index = tempal * 7;

	if ((tempcl == 0) && (tempch == 1)) {
		xgifb_reg_set(pVBInfo->Part2Port, 0x35, 0);
		xgifb_reg_set(pVBInfo->Part2Port, 0x36, 0);
		xgifb_reg_set(pVBInfo->Part2Port, 0x37, 0);
		xgifb_reg_set(pVBInfo->Part2Port, 0x38, filterPtr[index++]);
	} else {
		xgifb_reg_set(pVBInfo->Part2Port, 0x35, filterPtr[index++]);
		xgifb_reg_set(pVBInfo->Part2Port, 0x36, filterPtr[index++]);
		xgifb_reg_set(pVBInfo->Part2Port, 0x37, filterPtr[index++]);
		xgifb_reg_set(pVBInfo->Part2Port, 0x38, filterPtr[index++]);
	}

	if (pVBInfo->VBType & (VB_SIS301B | VB_SIS302B | VB_SIS301LV
			| VB_SIS302LV | VB_XGI301C)) {
		xgifb_reg_set(pVBInfo->Part2Port, 0x48, filterPtr[index++]);
		xgifb_reg_set(pVBInfo->Part2Port, 0x49, filterPtr[index++]);
		xgifb_reg_set(pVBInfo->Part2Port, 0x4A, filterPtr[index++]);
	}
}

/* --------------------------------------------------------------------- */
/* Function : XGI_OEM310Setting */
/* Input : */
/* Output : */
/* Description : Customized Param. for 301 */
/* --------------------------------------------------------------------- */
static void XGI_OEM310Setting(unsigned short ModeNo,
			      unsigned short ModeIdIndex,
			      struct vb_device_info *pVBInfo)
{
	XGI_SetDelayComp(pVBInfo);

	if (pVBInfo->VBInfo & (SetCRT2ToLCD | XGI_SetCRT2ToLCDA))
		XGI_SetLCDCap(pVBInfo);

	if (pVBInfo->VBInfo & SetCRT2ToTV) {
		XGI_SetPhaseIncr(pVBInfo);
		XGI_SetYFilter(ModeNo, ModeIdIndex, pVBInfo);
		XGI_SetAntiFlicker(ModeNo, ModeIdIndex, pVBInfo);

		if (pVBInfo->VBType & VB_SIS301)
			XGI_SetEdgeEnhance(ModeNo, ModeIdIndex, pVBInfo);
	}
}

/* --------------------------------------------------------------------- */
/* Function : XGI_SetCRT2ModeRegs */
/* Input : */
/* Output : */
/* Description : Origin code for crt2group */
/* --------------------------------------------------------------------- */
static void XGI_SetCRT2ModeRegs(unsigned short ModeNo,
		struct xgi_hw_device_info *HwDeviceExtension,
		struct vb_device_info *pVBInfo)
{
	unsigned short tempbl;
	short tempcl;

	unsigned char tempah;

	tempah = 0;
	if (!(pVBInfo->VBInfo & DisableCRT2Display)) {
		tempah = xgifb_reg_get(pVBInfo->Part1Port, 0x00);
		tempah &= ~0x10; /* BTRAMDAC */
		tempah |= 0x40; /* BTRAM */

		if (pVBInfo->VBInfo & (SetCRT2ToRAMDAC | SetCRT2ToTV
				| SetCRT2ToLCD)) {
			tempah = 0x40; /* BTDRAM */
			tempcl = pVBInfo->ModeType;
			tempcl -= ModeVGA;
			if (tempcl >= 0) {
				/* BT Color */
				tempah = (0x008 >> tempcl);
				if (tempah == 0)
					tempah = 1;
				tempah |= 0x040;
			}
			if (pVBInfo->VBInfo & SetInSlaveMode)
				tempah ^= 0x50; /* BTDAC */
		}
	}

	xgifb_reg_set(pVBInfo->Part1Port, 0x00, tempah);
	tempah = 0x08;
	tempbl = 0xf0;

	if (pVBInfo->VBInfo & DisableCRT2Display)
		goto reg_and_or;

	tempah = 0x00;
	tempbl = 0xff;

	if (!(pVBInfo->VBInfo & (SetCRT2ToRAMDAC | SetCRT2ToTV |
				 SetCRT2ToLCD | XGI_SetCRT2ToLCDA)))
		goto reg_and_or;

	if ((pVBInfo->VBInfo & XGI_SetCRT2ToLCDA) &&
	    (!(pVBInfo->VBInfo & SetSimuScanMode))) {
		tempbl &= 0xf7;
		tempah |= 0x01;
		goto reg_and_or;
	}

	if (pVBInfo->VBInfo & XGI_SetCRT2ToLCDA) {
		tempbl &= 0xf7;
		tempah |= 0x01;
	}

	if (!(pVBInfo->VBInfo & (SetCRT2ToRAMDAC | SetCRT2ToTV | SetCRT2ToLCD)))
		goto reg_and_or;

	tempbl &= 0xf8;
	tempah = 0x01;

	if (!(pVBInfo->VBInfo & SetInSlaveMode))
		tempah |= 0x02;

	if (!(pVBInfo->VBInfo & SetCRT2ToRAMDAC)) {
		tempah = tempah ^ 0x05;
		if (!(pVBInfo->VBInfo & SetCRT2ToLCD))
			tempah = tempah ^ 0x01;
	}

	if (!(pVBInfo->VBInfo & SetCRT2ToDualEdge))
		tempah |= 0x08;

reg_and_or:
	xgifb_reg_and_or(pVBInfo->Part1Port, 0x2e, tempbl, tempah);

	if (pVBInfo->VBInfo & (SetCRT2ToRAMDAC | SetCRT2ToTV | SetCRT2ToLCD
			| XGI_SetCRT2ToLCDA)) {
		tempah &= (~0x08);
		if ((pVBInfo->ModeType == ModeVGA) && (!(pVBInfo->VBInfo
				& SetInSlaveMode))) {
			tempah |= 0x010;
		}
		tempah |= 0x080;

		if (pVBInfo->VBInfo & SetCRT2ToTV) {
			tempah |= 0x020;
			if (pVBInfo->VBInfo & DriverMode)
				tempah = tempah ^ 0x20;
		}

		xgifb_reg_and_or(pVBInfo->Part4Port, 0x0D, ~0x0BF, tempah);
		tempah = 0;

		if (pVBInfo->LCDInfo & SetLCDDualLink)
			tempah |= 0x40;

		if (pVBInfo->VBInfo & SetCRT2ToTV) {
			if (pVBInfo->TVInfo & RPLLDIV2XO)
				tempah |= 0x40;
		}

		if ((pVBInfo->LCDResInfo == Panel_1280x1024)
				|| (pVBInfo->LCDResInfo == Panel_1280x1024x75))
			tempah |= 0x80;

		if (pVBInfo->LCDResInfo == Panel_1280x960)
			tempah |= 0x80;

		xgifb_reg_set(pVBInfo->Part4Port, 0x0C, tempah);
	}

	if (pVBInfo->VBType & (VB_SIS301B | VB_SIS302B | VB_SIS301LV
			| VB_SIS302LV | VB_XGI301C)) {
		tempah = 0;
		tempbl = 0xfb;

		if (pVBInfo->VBInfo & SetCRT2ToDualEdge) {
			tempbl = 0xff;
			if (pVBInfo->VBInfo & XGI_SetCRT2ToLCDA)
				tempah |= 0x04; /* shampoo 0129 */
		}

		xgifb_reg_and_or(pVBInfo->Part1Port, 0x13, tempbl, tempah);
		tempah = 0x00;
		tempbl = 0xcf;
		if (!(pVBInfo->VBInfo & DisableCRT2Display)) {
			if (pVBInfo->VBInfo & SetCRT2ToDualEdge)
				tempah |= 0x30;
		}

		xgifb_reg_and_or(pVBInfo->Part1Port, 0x2c, tempbl, tempah);
		tempah = 0;
		tempbl = 0x3f;

		if (!(pVBInfo->VBInfo & DisableCRT2Display)) {
			if (pVBInfo->VBInfo & SetCRT2ToDualEdge)
				tempah |= 0xc0;
		}
		xgifb_reg_and_or(pVBInfo->Part4Port, 0x21, tempbl, tempah);
	}

	tempah = 0;
	tempbl = 0x7f;
	if (!(pVBInfo->VBInfo & XGI_SetCRT2ToLCDA)) {
		tempbl = 0xff;
		if (!(pVBInfo->VBInfo & SetCRT2ToDualEdge))
			tempah |= 0x80;
	}

	xgifb_reg_and_or(pVBInfo->Part4Port, 0x23, tempbl, tempah);

	if (pVBInfo->VBType & (VB_SIS302LV | VB_XGI301C)) {
		if (pVBInfo->LCDInfo & SetLCDDualLink) {
			xgifb_reg_or(pVBInfo->Part4Port, 0x27, 0x20);
			xgifb_reg_or(pVBInfo->Part4Port, 0x34, 0x10);
		}
	}
}


void XGI_UnLockCRT2(struct xgi_hw_device_info *HwDeviceExtension,
		struct vb_device_info *pVBInfo)
{

	xgifb_reg_and_or(pVBInfo->Part1Port, 0x2f, 0xFF, 0x01);

}

void XGI_LockCRT2(struct xgi_hw_device_info *HwDeviceExtension,
		struct vb_device_info *pVBInfo)
{

	xgifb_reg_and_or(pVBInfo->Part1Port, 0x2F, 0xFE, 0x00);

}

unsigned char XGI_BridgeIsOn(struct vb_device_info *pVBInfo)
{
	unsigned short flag;

	if (pVBInfo->IF_DEF_LVDS == 1) {
		return 1;
	} else {
		flag = xgifb_reg_get(pVBInfo->Part4Port, 0x00);
		if ((flag == 1) || (flag == 2))
			return 1; /* 301b */
		else
			return 0;
	}
}

unsigned short XGI_GetRatePtrCRT2(struct xgi_hw_device_info *pXGIHWDE,
		unsigned short ModeNo, unsigned short ModeIdIndex,
		struct vb_device_info *pVBInfo)
{
	short LCDRefreshIndex[] = { 0x00, 0x00, 0x03, 0x01 },
			LCDARefreshIndex[] = { 0x00, 0x00, 0x03, 0x01, 0x01,
					0x01, 0x01 };

	unsigned short RefreshRateTableIndex, i, modeflag, index, temp;

	modeflag = pVBInfo->EModeIDTable[ModeIdIndex].Ext_ModeFlag;

	index = xgifb_reg_get(pVBInfo->P3d4, 0x33);
	index = index >> pVBInfo->SelectCRT2Rate;
	index &= 0x0F;

	if (pVBInfo->LCDInfo & LCDNonExpanding)
		index = 0;

	if (index > 0)
		index--;

	if (pVBInfo->SetFlag & ProgrammingCRT2) {
		if (pVBInfo->VBInfo & (SetCRT2ToLCD | XGI_SetCRT2ToLCDA)) {
			if (pVBInfo->IF_DEF_LVDS == 0) {
				if (pVBInfo->VBType & (VB_SIS301B | VB_SIS302B
						| VB_SIS301LV | VB_SIS302LV
						| VB_XGI301C))
					/* 301b */
					temp = LCDARefreshIndex[
						pVBInfo->LCDResInfo & 0x0F];
				else
					temp = LCDRefreshIndex[
						pVBInfo->LCDResInfo & 0x0F];

				if (index > temp)
					index = temp;
			} else {
				index = 0;
			}
		}
	}

	RefreshRateTableIndex = pVBInfo->EModeIDTable[ModeIdIndex].REFindex;
	ModeNo = pVBInfo->RefIndex[RefreshRateTableIndex].ModeID;
	if (pXGIHWDE->jChipType >= XG20) { /* for XG20, XG21, XG27 */
		if ((pVBInfo->RefIndex[RefreshRateTableIndex].XRes == 800) &&
		    (pVBInfo->RefIndex[RefreshRateTableIndex].YRes == 600)) {
			index++;
		}
		/* do the similar adjustment like XGISearchCRT1Rate() */
		if ((pVBInfo->RefIndex[RefreshRateTableIndex].XRes == 1024) &&
		    (pVBInfo->RefIndex[RefreshRateTableIndex].YRes == 768)) {
			index++;
		}
		if ((pVBInfo->RefIndex[RefreshRateTableIndex].XRes == 1280) &&
		    (pVBInfo->RefIndex[RefreshRateTableIndex].YRes == 1024)) {
			index++;
		}
	}

	i = 0;
	do {
		if (pVBInfo->RefIndex[RefreshRateTableIndex + i].
			ModeID != ModeNo)
			break;
		temp = pVBInfo->RefIndex[RefreshRateTableIndex + i].
			Ext_InfoFlag;
		temp &= ModeTypeMask;
		if (temp < pVBInfo->ModeType)
			break;
		i++;
		index--;

	} while (index != 0xFFFF);
	if (!(pVBInfo->VBInfo & SetCRT2ToRAMDAC)) {
		if (pVBInfo->VBInfo & SetInSlaveMode) {
			temp = pVBInfo->RefIndex[RefreshRateTableIndex + i - 1].
				Ext_InfoFlag;
			if (temp & InterlaceMode)
				i++;
		}
	}
	i--;
	if ((pVBInfo->SetFlag & ProgrammingCRT2)) {
		temp = XGI_AjustCRT2Rate(ModeNo, ModeIdIndex,
				RefreshRateTableIndex, &i, pVBInfo);
	}
	return RefreshRateTableIndex + i;
}

static void XGI_SetLCDAGroup(unsigned short ModeNo, unsigned short ModeIdIndex,
		struct xgi_hw_device_info *HwDeviceExtension,
		struct vb_device_info *pVBInfo)
{
	unsigned short RefreshRateTableIndex;

	pVBInfo->SetFlag |= ProgrammingCRT2;
	RefreshRateTableIndex = XGI_GetRatePtrCRT2(HwDeviceExtension, ModeNo,
			ModeIdIndex, pVBInfo);
	XGI_GetLVDSResInfo(ModeNo, ModeIdIndex, pVBInfo);
	XGI_GetLVDSData(ModeNo, ModeIdIndex, RefreshRateTableIndex, pVBInfo);
	XGI_ModCRT1Regs(ModeNo, ModeIdIndex, RefreshRateTableIndex,
			HwDeviceExtension, pVBInfo);
	XGI_SetLVDSRegs(ModeNo, ModeIdIndex, RefreshRateTableIndex, pVBInfo);
	XGI_SetCRT2ECLK(ModeNo, ModeIdIndex, RefreshRateTableIndex, pVBInfo);
}

static unsigned char XGI_SetCRT2Group301(unsigned short ModeNo,
		struct xgi_hw_device_info *HwDeviceExtension,
		struct vb_device_info *pVBInfo)
{
	unsigned short tempbx, ModeIdIndex, RefreshRateTableIndex;

	tempbx = pVBInfo->VBInfo;
	pVBInfo->SetFlag |= ProgrammingCRT2;
	XGI_SearchModeID(ModeNo, &ModeIdIndex, pVBInfo);
	pVBInfo->SelectCRT2Rate = 4;
	RefreshRateTableIndex = XGI_GetRatePtrCRT2(HwDeviceExtension, ModeNo,
			ModeIdIndex, pVBInfo);
	XGI_SaveCRT2Info(ModeNo, pVBInfo);
	XGI_GetCRT2ResInfo(ModeNo, ModeIdIndex, pVBInfo);
	XGI_GetCRT2Data(ModeNo, ModeIdIndex, RefreshRateTableIndex, pVBInfo);
	XGI_PreSetGroup1(ModeNo, ModeIdIndex, HwDeviceExtension,
			RefreshRateTableIndex, pVBInfo);
	XGI_SetGroup1(ModeNo, ModeIdIndex, HwDeviceExtension,
			RefreshRateTableIndex, pVBInfo);
	XGI_SetLockRegs(ModeNo, ModeIdIndex, HwDeviceExtension,
			RefreshRateTableIndex, pVBInfo);
	XGI_SetGroup2(ModeNo, ModeIdIndex, RefreshRateTableIndex,
			HwDeviceExtension, pVBInfo);
	XGI_SetLCDRegs(ModeNo, ModeIdIndex, HwDeviceExtension,
			RefreshRateTableIndex, pVBInfo);
	XGI_SetTap4Regs(pVBInfo);
	XGI_SetGroup3(ModeNo, ModeIdIndex, pVBInfo);
	XGI_SetGroup4(ModeNo, ModeIdIndex, RefreshRateTableIndex,
			HwDeviceExtension, pVBInfo);
	XGI_SetCRT2VCLK(ModeNo, ModeIdIndex, RefreshRateTableIndex, pVBInfo);
	XGI_SetGroup5(ModeNo, ModeIdIndex, pVBInfo);
	XGI_AutoThreshold(pVBInfo);
	return 1;
}

void XGI_SenseCRT1(struct vb_device_info *pVBInfo)
{
	unsigned char CRTCData[17] = { 0x5F, 0x4F, 0x50, 0x82, 0x55, 0x81,
			0x0B, 0x3E, 0xE9, 0x0B, 0xDF, 0xE7, 0x04, 0x00, 0x00,
			0x05, 0x00 };

	unsigned char SR01 = 0, SR1F = 0, SR07 = 0, SR06 = 0;

	unsigned char CR17, CR63, SR31;
	unsigned short temp;
	unsigned char DAC_TEST_PARMS[3] = { 0x0F, 0x0F, 0x0F };

	int i;
	xgifb_reg_set(pVBInfo->P3c4, 0x05, 0x86);

	/* to fix XG42 single LCD sense to CRT+LCD */
	xgifb_reg_set(pVBInfo->P3d4, 0x57, 0x4A);
	xgifb_reg_set(pVBInfo->P3d4, 0x53, (unsigned char) (xgifb_reg_get(
			pVBInfo->P3d4, 0x53) | 0x02));

	SR31 = (unsigned char) xgifb_reg_get(pVBInfo->P3c4, 0x31);
	CR63 = (unsigned char) xgifb_reg_get(pVBInfo->P3d4, 0x63);
	SR01 = (unsigned char) xgifb_reg_get(pVBInfo->P3c4, 0x01);

	xgifb_reg_set(pVBInfo->P3c4, 0x01, (unsigned char) (SR01 & 0xDF));
	xgifb_reg_set(pVBInfo->P3d4, 0x63, (unsigned char) (CR63 & 0xBF));

	CR17 = (unsigned char) xgifb_reg_get(pVBInfo->P3d4, 0x17);
	xgifb_reg_set(pVBInfo->P3d4, 0x17, (unsigned char) (CR17 | 0x80));

	SR1F = (unsigned char) xgifb_reg_get(pVBInfo->P3c4, 0x1F);
	xgifb_reg_set(pVBInfo->P3c4, 0x1F, (unsigned char) (SR1F | 0x04));

	SR07 = (unsigned char) xgifb_reg_get(pVBInfo->P3c4, 0x07);
	xgifb_reg_set(pVBInfo->P3c4, 0x07, (unsigned char) (SR07 & 0xFB));
	SR06 = (unsigned char) xgifb_reg_get(pVBInfo->P3c4, 0x06);
	xgifb_reg_set(pVBInfo->P3c4, 0x06, (unsigned char) (SR06 & 0xC3));

	xgifb_reg_set(pVBInfo->P3d4, 0x11, 0x00);

	for (i = 0; i < 8; i++)
		xgifb_reg_set(pVBInfo->P3d4, (unsigned short) i, CRTCData[i]);

	for (i = 8; i < 11; i++)
		xgifb_reg_set(pVBInfo->P3d4, (unsigned short) (i + 8),
				CRTCData[i]);

	for (i = 11; i < 13; i++)
		xgifb_reg_set(pVBInfo->P3d4, (unsigned short) (i + 4),
				CRTCData[i]);

	for (i = 13; i < 16; i++)
		xgifb_reg_set(pVBInfo->P3c4, (unsigned short) (i - 3),
				CRTCData[i]);

	xgifb_reg_set(pVBInfo->P3c4, 0x0E, (unsigned char) (CRTCData[16]
			& 0xE0));

	xgifb_reg_set(pVBInfo->P3c4, 0x31, 0x00);
	xgifb_reg_set(pVBInfo->P3c4, 0x2B, 0x1B);
	xgifb_reg_set(pVBInfo->P3c4, 0x2C, 0xE1);

	outb(0x00, pVBInfo->P3c8);

	for (i = 0; i < 256; i++) {
		outb((unsigned char) DAC_TEST_PARMS[0], (pVBInfo->P3c8 + 1));
		outb((unsigned char) DAC_TEST_PARMS[1], (pVBInfo->P3c8 + 1));
		outb((unsigned char) DAC_TEST_PARMS[2], (pVBInfo->P3c8 + 1));
	}

	mdelay(1);

	XGI_WaitDisply(pVBInfo);
	temp = inb(pVBInfo->P3c2);

	if (temp & 0x10)
		xgifb_reg_and_or(pVBInfo->P3d4, 0x32, 0xDF, 0x20);
	else
		xgifb_reg_and_or(pVBInfo->P3d4, 0x32, 0xDF, 0x00);

	/* avoid display something, set BLACK DAC if not restore DAC */
	outb(0x00, pVBInfo->P3c8);

	for (i = 0; i < 256; i++) {
		outb(0, (pVBInfo->P3c8 + 1));
		outb(0, (pVBInfo->P3c8 + 1));
		outb(0, (pVBInfo->P3c8 + 1));
	}

	xgifb_reg_set(pVBInfo->P3c4, 0x01, SR01);
	xgifb_reg_set(pVBInfo->P3d4, 0x63, CR63);
	xgifb_reg_set(pVBInfo->P3c4, 0x31, SR31);

	xgifb_reg_set(pVBInfo->P3d4, 0x53, (unsigned char) (xgifb_reg_get(
			pVBInfo->P3d4, 0x53) & 0xFD));
	xgifb_reg_set(pVBInfo->P3c4, 0x1F, (unsigned char) SR1F);
}

static void XGI_EnableBridge(struct xgifb_video_info *xgifb_info,
		struct xgi_hw_device_info *HwDeviceExtension,
		struct vb_device_info *pVBInfo)
{
	unsigned short tempah;

	if (pVBInfo->VBType & (VB_SIS301B | VB_SIS302B | VB_SIS301LV
			| VB_SIS302LV | VB_XGI301C)) {
		if (!(pVBInfo->SetFlag & DisableChA)) {
			if (pVBInfo->SetFlag & EnableChA) {
				/* Power on */
				xgifb_reg_set(pVBInfo->Part1Port, 0x1E, 0x20);
			} else {
				if (pVBInfo->VBInfo & SetCRT2ToDualEdge) {
					/* Power on */
					xgifb_reg_set(pVBInfo->Part1Port,
							0x1E, 0x20);
				}
			}
		}

		if (!(pVBInfo->SetFlag & DisableChB)) {
			if ((pVBInfo->SetFlag & EnableChB) || (pVBInfo->VBInfo
					& (SetCRT2ToLCD | SetCRT2ToTV
							| SetCRT2ToRAMDAC))) {
				tempah = (unsigned char) xgifb_reg_get(
						pVBInfo->P3c4, 0x32);
				tempah &= 0xDF;
				if (pVBInfo->VBInfo & SetInSlaveMode) {
					if (!(pVBInfo->VBInfo &
					      SetCRT2ToRAMDAC))
						tempah |= 0x20;
				}
				xgifb_reg_set(pVBInfo->P3c4, 0x32, tempah);
				xgifb_reg_or(pVBInfo->P3c4, 0x1E, 0x20);

				tempah = (unsigned char) xgifb_reg_get(
						pVBInfo->Part1Port, 0x2E);

				if (!(tempah & 0x80))
					xgifb_reg_or(pVBInfo->Part1Port,
							0x2E, 0x80);
				xgifb_reg_and(pVBInfo->Part1Port, 0x00, 0x7F);
			}
		}

		if ((pVBInfo->SetFlag & (EnableChA | EnableChB))
				|| (!(pVBInfo->VBInfo & DisableCRT2Display))) {
			xgifb_reg_and_or(pVBInfo->Part2Port, 0x00, ~0xE0,
					0x20); /* shampoo 0129 */
			if (pVBInfo->VBType & (VB_SIS302LV | VB_XGI301C)) {
				if (!XGI_DisableChISLCD(pVBInfo)) {
					if (XGI_EnableChISLCD(pVBInfo) ||
					    (pVBInfo->VBInfo &
					    (SetCRT2ToLCD | XGI_SetCRT2ToLCDA)))
						/* LVDS PLL power on */
						xgifb_reg_and(
							pVBInfo->Part4Port,
							0x2A,
							0x7F);
				}
				/* LVDS Driver power on */
				xgifb_reg_and(pVBInfo->Part4Port, 0x30, 0x7F);
			}
		}

		tempah = 0x00;

		if (!(pVBInfo->VBInfo & DisableCRT2Display)) {
			tempah = 0xc0;

			if (!(pVBInfo->VBInfo & SetSimuScanMode) &&
			    (pVBInfo->VBInfo & XGI_SetCRT2ToLCDA) &&
			    (pVBInfo->VBInfo & SetCRT2ToDualEdge)) {
				tempah = tempah & 0x40;
				if (pVBInfo->VBInfo & XGI_SetCRT2ToLCDA)
					tempah = tempah ^ 0xC0;

				if (pVBInfo->SetFlag & DisableChB)
					tempah &= 0xBF;

				if (pVBInfo->SetFlag &  DisableChA)
					tempah &= 0x7F;

				if (pVBInfo->SetFlag &  EnableChB)
					tempah |= 0x40;

				if (pVBInfo->SetFlag &  EnableChA)
					tempah |= 0x80;
			}
		}

		/* EnablePart4_1F */
		xgifb_reg_or(pVBInfo->Part4Port, 0x1F, tempah);

		if (!(pVBInfo->SetFlag & DisableChA)) {
			if (!(pVBInfo->SetFlag & GatingCRT)) {
				XGI_DisableGatingCRT(HwDeviceExtension,
						     pVBInfo);
				XGI_DisplayOn(xgifb_info, HwDeviceExtension,
						pVBInfo);
			}
		}
	} /* 301 */
	else { /* LVDS */
		if (pVBInfo->VBInfo & (SetCRT2ToTV | SetCRT2ToLCD
				| XGI_SetCRT2ToLCDA))
			/* enable CRT2 */
			xgifb_reg_or(pVBInfo->Part1Port, 0x1E, 0x20);

		tempah = (unsigned char) xgifb_reg_get(pVBInfo->Part1Port,
				0x2E);
		if (!(tempah & 0x80))
			xgifb_reg_or(pVBInfo->Part1Port, 0x2E, 0x80);

		xgifb_reg_and(pVBInfo->Part1Port, 0x00, 0x7F);
		XGI_DisplayOn(xgifb_info, HwDeviceExtension, pVBInfo);
	} /* End of VB */
}

static void XGI_SetCRT1Group(struct xgifb_video_info *xgifb_info,
		struct xgi_hw_device_info *HwDeviceExtension,
		unsigned short ModeNo, unsigned short ModeIdIndex,
		struct vb_device_info *pVBInfo)
{
	unsigned short RefreshRateTableIndex, temp;

	XGI_SetSeqRegs(ModeNo, ModeIdIndex, pVBInfo);
	outb(pVBInfo->StandTable->MISC, pVBInfo->P3c2);
	XGI_SetCRTCRegs(HwDeviceExtension, pVBInfo);
	XGI_SetATTRegs(ModeNo, ModeIdIndex, pVBInfo);
	XGI_SetGRCRegs(pVBInfo);
	XGI_ClearExt1Regs(pVBInfo);

	if (HwDeviceExtension->jChipType == XG27) {
		if (pVBInfo->IF_DEF_LVDS == 0)
			XGI_SetDefaultVCLK(pVBInfo);
	}

	temp = ~ProgrammingCRT2;
	pVBInfo->SetFlag &= temp;
	pVBInfo->SelectCRT2Rate = 0;

	if (pVBInfo->VBType & (VB_SIS301B | VB_SIS302B | VB_SIS301LV
			| VB_SIS302LV | VB_XGI301C)) {
		if (pVBInfo->VBInfo & (SetSimuScanMode | XGI_SetCRT2ToLCDA
				| SetInSlaveMode)) {
			pVBInfo->SetFlag |= ProgrammingCRT2;
		}
	}

	RefreshRateTableIndex = XGI_GetRatePtrCRT2(HwDeviceExtension, ModeNo,
			ModeIdIndex, pVBInfo);
	if (RefreshRateTableIndex != 0xFFFF) {
		XGI_SetSync(RefreshRateTableIndex, pVBInfo);
		XGI_SetCRT1CRTC(ModeNo, ModeIdIndex, RefreshRateTableIndex,
				pVBInfo, HwDeviceExtension);
		XGI_SetCRT1DE(HwDeviceExtension, ModeNo, ModeIdIndex,
				RefreshRateTableIndex, pVBInfo);
		XGI_SetCRT1Offset(ModeNo, ModeIdIndex, RefreshRateTableIndex,
				HwDeviceExtension, pVBInfo);
		XGI_SetCRT1VCLK(ModeNo, ModeIdIndex, HwDeviceExtension,
				RefreshRateTableIndex, pVBInfo);
	}

	if (HwDeviceExtension->jChipType >= XG21) {
		temp = xgifb_reg_get(pVBInfo->P3d4, 0x38);
		if (temp & 0xA0) {

			if (HwDeviceExtension->jChipType == XG27)
				XGI_SetXG27CRTC(ModeNo, ModeIdIndex,
						RefreshRateTableIndex, pVBInfo);
			else
				XGI_SetXG21CRTC(ModeNo, ModeIdIndex,
						RefreshRateTableIndex, pVBInfo);

			XGI_UpdateXG21CRTC(ModeNo, pVBInfo,
					RefreshRateTableIndex);

			xgifb_set_lcd(HwDeviceExtension->jChipType,
					pVBInfo, RefreshRateTableIndex, ModeNo);

			if (pVBInfo->IF_DEF_LVDS == 1)
				xgifb_set_lvds(xgifb_info,
						HwDeviceExtension->jChipType,
						ModeNo, ModeIdIndex, pVBInfo);
		}
	}

	pVBInfo->SetFlag &= (~ProgrammingCRT2);
	XGI_SetCRT1FIFO(ModeNo, HwDeviceExtension, pVBInfo);
	XGI_SetCRT1ModeRegs(HwDeviceExtension, ModeNo, ModeIdIndex,
			RefreshRateTableIndex, pVBInfo);
	XGI_LoadDAC(ModeNo, ModeIdIndex, pVBInfo);
}

unsigned char XGISetModeNew(struct xgifb_video_info *xgifb_info,
			struct xgi_hw_device_info *HwDeviceExtension,
			unsigned short ModeNo)
{
	unsigned short ModeIdIndex;
	struct vb_device_info VBINF;
	struct vb_device_info *pVBInfo = &VBINF;
	pVBInfo->BaseAddr = xgifb_info->vga_base;
	pVBInfo->IF_DEF_LVDS = 0;

	if (HwDeviceExtension->jChipType >= XG20) {
		pVBInfo->IF_DEF_YPbPr = 0;
		pVBInfo->IF_DEF_HiVision = 0;
		pVBInfo->IF_DEF_CRT2Monitor = 0;
		pVBInfo->VBType = 0; /*set VBType default 0*/
	} else {
		pVBInfo->IF_DEF_YPbPr = 1;
		pVBInfo->IF_DEF_HiVision = 1;
		pVBInfo->IF_DEF_CRT2Monitor = 1;
	}

	pVBInfo->P3c4 = pVBInfo->BaseAddr + 0x14;
	pVBInfo->P3d4 = pVBInfo->BaseAddr + 0x24;
	pVBInfo->P3c0 = pVBInfo->BaseAddr + 0x10;
	pVBInfo->P3ce = pVBInfo->BaseAddr + 0x1e;
	pVBInfo->P3c2 = pVBInfo->BaseAddr + 0x12;
	pVBInfo->P3cc = pVBInfo->BaseAddr + 0x1C;
	pVBInfo->P3ca = pVBInfo->BaseAddr + 0x1a;
	pVBInfo->P3c6 = pVBInfo->BaseAddr + 0x16;
	pVBInfo->P3c7 = pVBInfo->BaseAddr + 0x17;
	pVBInfo->P3c8 = pVBInfo->BaseAddr + 0x18;
	pVBInfo->P3c9 = pVBInfo->BaseAddr + 0x19;
	pVBInfo->P3da = pVBInfo->BaseAddr + 0x2A;
	pVBInfo->Part0Port = pVBInfo->BaseAddr + XGI_CRT2_PORT_00;
	pVBInfo->Part1Port = pVBInfo->BaseAddr + SIS_CRT2_PORT_04;
	pVBInfo->Part2Port = pVBInfo->BaseAddr + SIS_CRT2_PORT_10;
	pVBInfo->Part3Port = pVBInfo->BaseAddr + SIS_CRT2_PORT_12;
	pVBInfo->Part4Port = pVBInfo->BaseAddr + SIS_CRT2_PORT_14;
	pVBInfo->Part5Port = pVBInfo->BaseAddr + SIS_CRT2_PORT_14 + 2;

	/* for x86 Linux, XG21 LVDS */
	if (HwDeviceExtension->jChipType == XG21) {
		if ((xgifb_reg_get(pVBInfo->P3d4, 0x38) & 0xE0) == 0xC0)
			pVBInfo->IF_DEF_LVDS = 1;
	}
	if (HwDeviceExtension->jChipType == XG27) {
		if ((xgifb_reg_get(pVBInfo->P3d4, 0x38) & 0xE0) == 0xC0) {
			if (xgifb_reg_get(pVBInfo->P3d4, 0x30) & 0x20)
				pVBInfo->IF_DEF_LVDS = 1;
		}
	}

	if (HwDeviceExtension->jChipType < XG20)
		XGI_GetVBType(pVBInfo);

	InitTo330Pointer(HwDeviceExtension->jChipType, pVBInfo);
	if (ModeNo & 0x80)
		ModeNo = ModeNo & 0x7F;
	xgifb_reg_set(pVBInfo->P3c4, 0x05, 0x86);

	if (HwDeviceExtension->jChipType < XG20)
		XGI_UnLockCRT2(HwDeviceExtension, pVBInfo);

	XGI_SearchModeID(ModeNo, &ModeIdIndex, pVBInfo);

	if (HwDeviceExtension->jChipType < XG20) {
		XGI_GetVBInfo(ModeNo, ModeIdIndex, HwDeviceExtension, pVBInfo);
		XGI_GetTVInfo(ModeNo, ModeIdIndex, pVBInfo);
		XGI_GetLCDInfo(ModeNo, ModeIdIndex, pVBInfo);
		XGI_DisableBridge(xgifb_info, HwDeviceExtension, pVBInfo);

		if (pVBInfo->VBInfo & (SetSimuScanMode | XGI_SetCRT2ToLCDA)) {
			XGI_SetCRT1Group(xgifb_info, HwDeviceExtension, ModeNo,
					ModeIdIndex, pVBInfo);

			if (pVBInfo->VBInfo & XGI_SetCRT2ToLCDA) {
				XGI_SetLCDAGroup(ModeNo, ModeIdIndex,
						HwDeviceExtension, pVBInfo);
			}
		} else {
			if (!(pVBInfo->VBInfo & SwitchCRT2)) {
				XGI_SetCRT1Group(xgifb_info,
						HwDeviceExtension, ModeNo,
						ModeIdIndex, pVBInfo);
				if (pVBInfo->VBInfo & XGI_SetCRT2ToLCDA) {
					XGI_SetLCDAGroup(ModeNo, ModeIdIndex,
							HwDeviceExtension,
							pVBInfo);
				}
			}
		}

		if (pVBInfo->VBInfo & (SetSimuScanMode | SwitchCRT2)) {
			switch (HwDeviceExtension->ujVBChipID) {
			case VB_CHIP_301:
				XGI_SetCRT2Group301(ModeNo, HwDeviceExtension,
						pVBInfo); /*add for CRT2 */
				break;

			case VB_CHIP_302:
				XGI_SetCRT2Group301(ModeNo, HwDeviceExtension,
						pVBInfo); /*add for CRT2 */
				break;

			default:
				break;
			}
		}

		XGI_SetCRT2ModeRegs(ModeNo, HwDeviceExtension, pVBInfo);
		XGI_OEM310Setting(ModeNo, ModeIdIndex, pVBInfo); /*0212*/
		XGI_EnableBridge(xgifb_info, HwDeviceExtension, pVBInfo);
	} /* !XG20 */
	else {
		if (pVBInfo->IF_DEF_LVDS == 1)
			if (!XGI_XG21CheckLVDSMode(xgifb_info, ModeNo,
						   ModeIdIndex,
						   pVBInfo))
				return 0;

		pVBInfo->ModeType = pVBInfo->EModeIDTable[ModeIdIndex].
						Ext_ModeFlag & ModeTypeMask;

		pVBInfo->SetFlag = 0;
		pVBInfo->VBInfo = DisableCRT2Display;

		XGI_DisplayOff(xgifb_info, HwDeviceExtension, pVBInfo);

		XGI_SetCRT1Group(xgifb_info, HwDeviceExtension, ModeNo,
				ModeIdIndex, pVBInfo);

		XGI_DisplayOn(xgifb_info, HwDeviceExtension, pVBInfo);
	}

	XGI_UpdateModeInfo(HwDeviceExtension, pVBInfo);

	if (HwDeviceExtension->jChipType < XG20)
		XGI_LockCRT2(HwDeviceExtension, pVBInfo);

	return 1;
}
