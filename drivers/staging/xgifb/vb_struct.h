#ifndef _VB_STRUCT_
#define _VB_STRUCT_
#include "../../video/sis/vstruct.h"

struct XGI_LVDSCRT1HDataStruct {
	unsigned char Reg[8];
};

struct XGI_LVDSCRT1VDataStruct {
	unsigned char Reg[7];
};

struct XGI_ExtStruct {
	unsigned char Ext_ModeID;
	unsigned short Ext_ModeFlag;
	unsigned short Ext_ModeInfo;
	unsigned char Ext_RESINFO;
	unsigned char VB_ExtTVYFilterIndex;
	unsigned char REFindex;
};

struct XGI_Ext2Struct {
	unsigned short Ext_InfoFlag;
	unsigned char Ext_CRT1CRTC;
	unsigned char Ext_CRTVCLK;
	unsigned char Ext_CRT2CRTC;
	unsigned char Ext_CRT2CRTC2;
	unsigned char  ModeID;
	unsigned short XRes;
	unsigned short YRes;
	/* unsigned short ROM_OFFSET; */
};

struct XGI_ECLKDataStruct {
	unsigned char SR2E, SR2F, SR30;
	unsigned short CLOCK;
};

/*add for new UNIVGABIOS*/
struct XGI_LCDDesStruct {
	unsigned short LCDHDES;
	unsigned short LCDHRS;
	unsigned short LCDVDES;
	unsigned short LCDVRS;
};

struct XGI_LCDDataTablStruct {
	unsigned char  PANELID;
	unsigned short MASK;
	unsigned short CAP;
	unsigned short DATAPTR;
};

struct XGI330_LVDSDataStruct {
	unsigned short VGAHT;
	unsigned short VGAVT;
	unsigned short LCDHT;
	unsigned short LCDVT;
};

struct XGI330_LCDDataDesStruct2 {
	unsigned short LCDHDES;
	unsigned short LCDHRS;
	unsigned short LCDVDES;
	unsigned short LCDVRS;
	unsigned short LCDHSync;
	unsigned short LCDVSync;
};

struct XGI330_LCDDataStruct {
	unsigned short RVBHCMAX;
	unsigned short RVBHCFACT;
	unsigned short VGAHT;
	unsigned short VGAVT;
	unsigned short LCDHT;
	unsigned short LCDVT;
};


struct XGI330_TVDataStruct {
	unsigned short RVBHCMAX;
	unsigned short RVBHCFACT;
	unsigned short VGAHT;
	unsigned short VGAVT;
	unsigned short TVHDE;
	unsigned short TVVDE;
	unsigned short RVBHRS;
	unsigned char FlickerMode;
	unsigned short HALFRVBHRS;
};

struct XGI330_LCDDataTablStruct {
	unsigned char  PANELID;
	unsigned short MASK;
	unsigned short CAP;
	unsigned short DATAPTR;
};

struct XGI330_TVDataTablStruct {
	unsigned short MASK;
	unsigned short CAP;
	unsigned short DATAPTR;
};


struct XGI330_CHTVDataStruct {
	unsigned short VGAHT;
	unsigned short VGAVT;
	unsigned short LCDHT;
	unsigned short LCDVT;
};

struct XGI_TimingHStruct {
	unsigned char data[8];
};

struct XGI_TimingVStruct {
	unsigned char data[7];
};

struct XGI_XG21CRT1Struct {
	unsigned char ModeID, CR02, CR03, CR15, CR16;
};

struct XGI330_LCDCapStruct {
	unsigned char	LCD_ID;
	unsigned short	LCD_Capability;
	unsigned char	LCD_SetFlag;
	unsigned char	LCD_DelayCompensation;
	unsigned char	LCD_HSyncWidth;
	unsigned char	LCD_VSyncWidth;
	unsigned char	LCD_VCLK;
	unsigned char	LCDA_VCLKData1;
	unsigned char	LCDA_VCLKData2;
	unsigned char	LCUCHAR_VCLKData1;
	unsigned char	LCUCHAR_VCLKData2;
	unsigned char	PSC_S1;
	unsigned char	PSC_S2;
	unsigned char	PSC_S3;
	unsigned char	PSC_S4;
	unsigned char	PSC_S5;
	unsigned char	PWD_2B;
	unsigned char	PWD_2C;
	unsigned char	PWD_2D;
	unsigned char	PWD_2E;
	unsigned char	PWD_2F;
	unsigned char	Spectrum_31;
	unsigned char	Spectrum_32;
	unsigned char	Spectrum_33;
	unsigned char	Spectrum_34;
};

struct XGI21_LVDSCapStruct {
	unsigned short LVDS_Capability;
	unsigned short LVDSHT;
	unsigned short LVDSVT;
	unsigned short LVDSHDE;
	unsigned short LVDSVDE;
	unsigned short LVDSHFP;
	unsigned short LVDSVFP;
	unsigned short LVDSHSYNC;
	unsigned short LVDSVSYNC;
	unsigned char  VCLKData1;
	unsigned char  VCLKData2;
	unsigned char  PSC_S1;
	unsigned char  PSC_S2;
	unsigned char  PSC_S3;
	unsigned char  PSC_S4;
	unsigned char  PSC_S5;
};

struct XGI_CRT1TableStruct {
	unsigned char CR[16];
};


struct XGI330_VCLKDataStruct {
	unsigned char SR2B, SR2C;
	unsigned short CLOCK;
};

struct XGI301C_Tap4TimingStruct {
	unsigned short DE;
	unsigned char  Reg[64];   /* C0-FF */
};

struct vb_device_info {
	unsigned char  ISXPDOS;
	unsigned long   P3c4, P3d4, P3c0, P3ce, P3c2, P3cc;
	unsigned long   P3ca, P3c6, P3c7, P3c8, P3c9, P3da;
	unsigned long   Part0Port, Part1Port, Part2Port;
	unsigned long   Part3Port, Part4Port, Part5Port;
	unsigned short   RVBHCFACT, RVBHCMAX, RVBHRS;
	unsigned short   VGAVT, VGAHT, VGAVDE, VGAHDE;
	unsigned short   VT, HT, VDE, HDE;
	unsigned short   LCDHRS, LCDVRS, LCDHDES, LCDVDES;

	unsigned short   ModeType;
	/* ,IF_DEF_FSTN; add for dstn */
	unsigned short   IF_DEF_LVDS, IF_DEF_TRUMPION, IF_DEF_DSTN;
	unsigned short   IF_DEF_CRT2Monitor;
	unsigned short   IF_DEF_LCDA, IF_DEF_YPbPr;
	unsigned short   IF_DEF_ExpLink;
	unsigned short   IF_DEF_HiVision;
	unsigned short   LCDResInfo, LCDTypeInfo, VBType;/*301b*/
	unsigned short   VBInfo, TVInfo, LCDInfo;
	unsigned short   VBExtInfo;/*301lv*/
	unsigned short   SetFlag;
	unsigned short   NewFlickerMode;
	unsigned short   SelectCRT2Rate;

	void __iomem *FBAddr;
	unsigned long BaseAddr;

	unsigned char (*CR6B)[4];
	unsigned char (*CR6E)[4];
	unsigned char (*CR6F)[32];
	unsigned char (*CR89)[2];

	unsigned char (*SR15)[8];
	unsigned char (*CR40)[8];

	unsigned char  *pSoftSetting;
	unsigned char  *pOutputSelect;

	unsigned short *pRGBSenseData;
	unsigned short *pRGBSenseData2; /*301b*/
	unsigned short *pVideoSenseData;
	unsigned short *pVideoSenseData2;
	unsigned short *pYCSenseData;
	unsigned short *pYCSenseData2;

	unsigned char  *pSR07;
	unsigned char  *CR49;
	unsigned char  *pSR1F;
	unsigned char  *AGPReg;
	unsigned char  *SR16;
	unsigned char  *pSR21;
	unsigned char  *pSR22;
	unsigned char  *pSR23;
	unsigned char  *pSR24;
	unsigned char  *SR25;
	unsigned char  *pSR31;
	unsigned char  *pSR32;
	unsigned char  *pSR33;
	unsigned char  *pSR36;      /* alan 12/07/2006 */
	unsigned char  *pCRCF;
	unsigned char  *pCRD0;      /* alan 12/07/2006 */
	unsigned char  *pCRDE;      /* alan 12/07/2006 */
	unsigned char  *pCR8F;      /* alan 12/07/2006 */
	unsigned char  *pSR40;      /* alan 12/07/2006 */
	unsigned char  *pSR41;      /* alan 12/07/2006 */
	unsigned char  *pDVOSetting;
	unsigned char  *pCR2E;
	unsigned char  *pCR2F;
	unsigned char  *pCR46;
	unsigned char  *pCR47;
	unsigned char  *pCRT2Data_1_2;
	unsigned char  *pCRT2Data_4_D;
	unsigned char  *pCRT2Data_4_E;
	unsigned char  *pCRT2Data_4_10;
	struct SiS_MCLKData  *MCLKData;
	struct XGI_ECLKDataStruct  *ECLKData;

	unsigned char   *XGI_TVDelayList;
	unsigned char   *XGI_TVDelayList2;
	unsigned char   *NTSCTiming;
	unsigned char   *PALTiming;
	unsigned char   *HiTVExtTiming;
	unsigned char   *HiTVSt1Timing;
	unsigned char   *HiTVSt2Timing;
	unsigned char   *HiTVTextTiming;
	unsigned char   *YPbPr750pTiming;
	unsigned char   *YPbPr525pTiming;
	unsigned char   *YPbPr525iTiming;
	unsigned char   *HiTVGroup3Data;
	unsigned char   *HiTVGroup3Simu;
	unsigned char   *HiTVGroup3Text;
	unsigned char   *Ren525pGroup3;
	unsigned char   *Ren750pGroup3;
	unsigned char   *ScreenOffset;
	unsigned char   *pXGINew_DRAMTypeDefinition;
	unsigned char   *pXGINew_I2CDefinition ;
	unsigned char   *pXGINew_CR97 ;

	struct XGI330_LCDCapStruct  *LCDCapList;

	struct XGI_TimingHStruct  *TimingH;
	struct XGI_TimingVStruct  *TimingV;

	struct SiS_StandTable_S  *StandTable;
	struct XGI_ExtStruct         *EModeIDTable;
	struct XGI_Ext2Struct        *RefIndex;
	/* XGINew_CRT1TableStruct *CRT1Table; */
	struct XGI_CRT1TableStruct    *XGINEWUB_CRT1Table;
	struct SiS_VCLKData    *VCLKData;
	struct SiS_VBVCLKData  *VBVCLKData;
	struct SiS_StResInfo_S   *StResInfo;
	struct SiS_ModeResInfo_S *ModeResInfo;
	struct XGI_XG21CRT1Struct	  *UpdateCRT1;

	int ram_type;
	int ram_channel;
	int ram_bus;
};  /* _struct vb_device_info */

#endif /* _VB_STRUCT_ */
