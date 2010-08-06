#ifndef _VB_STRUCT_
#define _VB_STRUCT_

#ifdef _INITNEW_
#define EXTERN
#else
#define EXTERN extern
#endif




typedef struct _XGI_PanelDelayTblStruct
{
 UCHAR timer[2];
} XGI_PanelDelayTblStruct;

typedef struct _XGI_LCDDataStruct
{
 USHORT RVBHCMAX;
 USHORT RVBHCFACT;
 USHORT VGAHT;
 USHORT VGAVT;
 USHORT LCDHT;
 USHORT LCDVT;
} XGI_LCDDataStruct;


typedef struct _XGI_LVDSCRT1HDataStruct
{
 UCHAR Reg[8];
} XGI_LVDSCRT1HDataStruct;
typedef struct _XGI_LVDSCRT1VDataStruct
{
 UCHAR Reg[7];
} XGI_LVDSCRT1VDataStruct;


typedef struct _XGI_TVDataStruct
{
 USHORT RVBHCMAX;
 USHORT RVBHCFACT;
 USHORT VGAHT;
 USHORT VGAVT;
 USHORT TVHDE;
 USHORT TVVDE;
 USHORT RVBHRS;
 UCHAR FlickerMode;
 USHORT HALFRVBHRS;
 UCHAR RY1COE;
 UCHAR RY2COE;
 UCHAR RY3COE;
 UCHAR RY4COE;
} XGI_TVDataStruct;

typedef struct _XGI_LVDSDataStruct
{
 USHORT VGAHT;
 USHORT VGAVT;
 USHORT LCDHT;
 USHORT LCDVT;
} XGI_LVDSDataStruct;

typedef struct _XGI_LVDSDesStruct
{
 USHORT LCDHDES;
 USHORT LCDVDES;
} XGI_LVDSDesStruct;

typedef struct _XGI_LVDSCRT1DataStruct
{
 UCHAR CR[15];
} XGI_LVDSCRT1DataStruct;

/*add for LCDA*/


typedef struct _XGI_StStruct
{
 UCHAR St_ModeID;
 USHORT St_ModeFlag;
 UCHAR St_StTableIndex;
 UCHAR St_CRT2CRTC;
 UCHAR St_CRT2CRTC2;
 UCHAR St_ResInfo;
 UCHAR VB_StTVFlickerIndex;
 UCHAR VB_StTVEdgeIndex;
 UCHAR VB_StTVYFilterIndex;
} XGI_StStruct;

typedef struct _XGI_StandTableStruct
{
 UCHAR CRT_COLS;
 UCHAR ROWS;
 UCHAR CHAR_HEIGHT;
 USHORT CRT_LEN;
 UCHAR SR[4];
 UCHAR MISC;
 UCHAR CRTC[0x19];
 UCHAR ATTR[0x14];
 UCHAR GRC[9];
} XGI_StandTableStruct;

typedef struct _XGI_ExtStruct
{
 UCHAR Ext_ModeID;
 USHORT Ext_ModeFlag;
 USHORT Ext_ModeInfo;
 USHORT Ext_Point;
 USHORT Ext_VESAID;
 UCHAR Ext_VESAMEMSize;
 UCHAR Ext_RESINFO;
 UCHAR VB_ExtTVFlickerIndex;
 UCHAR VB_ExtTVEdgeIndex;
 UCHAR VB_ExtTVYFilterIndex;
 UCHAR REFindex;
} XGI_ExtStruct;

typedef struct _XGI_Ext2Struct
{
 USHORT Ext_InfoFlag;
 UCHAR Ext_CRT1CRTC;
 UCHAR Ext_CRTVCLK;
 UCHAR Ext_CRT2CRTC;
 UCHAR Ext_CRT2CRTC2;
 UCHAR  ModeID;
 USHORT XRes;
 USHORT YRes;
 /* USHORT ROM_OFFSET; */
} XGI_Ext2Struct;


typedef struct _XGI_MCLKDataStruct
{
 UCHAR SR28,SR29,SR2A;
 USHORT CLOCK;
} XGI_MCLKDataStruct;

typedef struct _XGI_ECLKDataStruct
{
 UCHAR SR2E,SR2F,SR30;
 USHORT CLOCK;
} XGI_ECLKDataStruct;

typedef struct _XGI_VCLKDataStruct
{
 UCHAR SR2B,SR2C;
 USHORT CLOCK;
} XGI_VCLKDataStruct;

typedef struct _XGI_VBVCLKDataStruct
{
 UCHAR Part4_A,Part4_B;
 USHORT CLOCK;
} XGI_VBVCLKDataStruct;

typedef struct _XGI_StResInfoStruct
{
 USHORT HTotal;
 USHORT VTotal;
} XGI_StResInfoStruct;

typedef struct _XGI_ModeResInfoStruct
{
 USHORT HTotal;
 USHORT VTotal;
 UCHAR  XChar;
 UCHAR  YChar;
} XGI_ModeResInfoStruct;

typedef struct _XGI_LCDNBDesStruct
{
  UCHAR NB[12];
} XGI_LCDNBDesStruct;
 /*add for new UNIVGABIOS*/
typedef struct _XGI_LCDDesStruct
{
 USHORT LCDHDES;
 USHORT LCDHRS;
 USHORT LCDVDES;
 USHORT LCDVRS;
} XGI_LCDDesStruct;

typedef struct _XGI_LCDDataTablStruct
{
 UCHAR  PANELID;
 USHORT MASK;
 USHORT CAP;
 USHORT DATAPTR;
} XGI_LCDDataTablStruct;

typedef struct _XGI_TVTablDataStruct
{
 USHORT MASK;
 USHORT CAP;
 USHORT DATAPTR;
} XGI_TVDataTablStruct;

typedef struct _XGI330_LCDDesDataStruct
{
 USHORT LCDHDES;
 USHORT LCDHRS;
 USHORT LCDVDES;
 USHORT LCDVRS;
} XGI330_LCDDataDesStruct;


typedef struct _XGI330_LVDSDataStruct
{
 USHORT VGAHT;
 USHORT VGAVT;
 USHORT LCDHT;
 USHORT LCDVT;
} XGI330_LVDSDataStruct;

typedef struct _XGI330_LCDDesDataStruct2
{
 USHORT LCDHDES;
 USHORT LCDHRS;
 USHORT LCDVDES;
 USHORT LCDVRS;
 USHORT LCDHSync;
 USHORT LCDVSync;
} XGI330_LCDDataDesStruct2;

typedef struct _XGI330_LCDDataStruct
{
 USHORT RVBHCMAX;
 USHORT RVBHCFACT;
 USHORT VGAHT;
 USHORT VGAVT;
 USHORT LCDHT;
 USHORT LCDVT;
} XGI330_LCDDataStruct;


typedef struct _XGI330_TVDataStruct
{
 USHORT RVBHCMAX;
 USHORT RVBHCFACT;
 USHORT VGAHT;
 USHORT VGAVT;
 USHORT TVHDE;
 USHORT TVVDE;
 USHORT RVBHRS;
 UCHAR FlickerMode;
 USHORT HALFRVBHRS;
} XGI330_TVDataStruct;

typedef struct _XGI330_LCDDataTablStruct
{
 UCHAR  PANELID;
 USHORT MASK;
 USHORT CAP;
 USHORT DATAPTR;
} XGI330_LCDDataTablStruct;

typedef struct _XGI330_TVDataTablStruct
{
 USHORT MASK;
 USHORT CAP;
 USHORT DATAPTR;
} XGI330_TVDataTablStruct;


typedef struct _XGI330_CHTVDataStruct
{
 USHORT VGAHT;
 USHORT VGAVT;
 USHORT LCDHT;
 USHORT LCDVT;
} XGI330_CHTVDataStruct;

typedef struct _XGI_TimingHStruct
{
  UCHAR data[8];
} XGI_TimingHStruct;

typedef struct _XGI_TimingVStruct
{
  UCHAR data[7];
} XGI_TimingVStruct;

typedef struct _XGI_CH7007TV_TimingHStruct
{
  UCHAR data[10];
} XGI_CH7007TV_TimingHStruct;

typedef struct _XGI_CH7007TV_TimingVStruct
{
  UCHAR data[10];
} XGI_CH7007TV_TimingVStruct;

typedef struct _XGI_XG21CRT1Struct
{
 UCHAR ModeID,CR02,CR03,CR15,CR16;
} XGI_XG21CRT1Struct;

typedef struct _XGI330_CHTVRegDataStruct
{
 UCHAR Reg[16];
} XGI330_CHTVRegDataStruct;

typedef struct _XGI330_LCDCapStruct
{
 		UCHAR      LCD_ID;
                USHORT     LCD_Capability;
                UCHAR      LCD_SetFlag;
                UCHAR      LCD_DelayCompensation;
                UCHAR      LCD_HSyncWidth;
                UCHAR      LCD_VSyncWidth;
                UCHAR      LCD_VCLK;
                UCHAR      LCDA_VCLKData1;
                UCHAR      LCDA_VCLKData2;
                UCHAR      LCUCHAR_VCLKData1;
                UCHAR      LCUCHAR_VCLKData2;
                UCHAR      PSC_S1;
                UCHAR      PSC_S2;
                UCHAR      PSC_S3;
                UCHAR      PSC_S4;
                UCHAR      PSC_S5;
                UCHAR      PWD_2B;
                UCHAR      PWD_2C;
                UCHAR      PWD_2D;
                UCHAR      PWD_2E;
                UCHAR      PWD_2F;
                UCHAR      Spectrum_31;
                UCHAR      Spectrum_32;
                UCHAR      Spectrum_33;
                UCHAR      Spectrum_34;
} XGI330_LCDCapStruct;

typedef struct _XGI21_LVDSCapStruct
{
                USHORT     LVDS_Capability;
                USHORT     LVDSHT;
                USHORT     LVDSVT;
                USHORT     LVDSHDE;
                USHORT     LVDSVDE;
                USHORT     LVDSHFP;
                USHORT     LVDSVFP;
                USHORT     LVDSHSYNC;
                USHORT     LVDSVSYNC;
                UCHAR      VCLKData1;
                UCHAR      VCLKData2;
                UCHAR      PSC_S1;
                UCHAR      PSC_S2;
                UCHAR      PSC_S3;
                UCHAR      PSC_S4;
                UCHAR      PSC_S5;
} XGI21_LVDSCapStruct;

typedef struct _XGI_CRT1TableStruct
{
  UCHAR CR[16];
} XGI_CRT1TableStruct;


typedef struct _XGI330_VCLKDataStruct
{
    UCHAR SR2B,SR2C;
    USHORT CLOCK;
} XGI330_VCLKDataStruct;

typedef struct _XGI301C_Tap4TimingStruct
{
    USHORT DE;
    UCHAR  Reg[64];   /* C0-FF */
} XGI301C_Tap4TimingStruct;

typedef struct _XGI_New_StandTableStruct
{
	UCHAR  CRT_COLS;
	UCHAR  ROWS;
	UCHAR  CHAR_HEIGHT;
	USHORT CRT_LEN;
	UCHAR  SR[4];
	UCHAR  MISC;
	UCHAR  CRTC[0x19];
	UCHAR  ATTR[0x14];
	UCHAR  GRC[9];
} XGI_New_StandTableStruct;

typedef UCHAR DRAM8Type[8];
typedef UCHAR DRAM4Type[4];
typedef UCHAR DRAM32Type[32];
typedef UCHAR DRAM2Type[2];

typedef struct _VB_DEVICE_INFO  VB_DEVICE_INFO;
typedef VB_DEVICE_INFO *	PVB_DEVICE_INFO;

struct _VB_DEVICE_INFO
{
    BOOLEAN  ISXPDOS;
    ULONG   P3c4,P3d4,P3c0,P3ce,P3c2,P3cc;
    ULONG   P3ca,P3c6,P3c7,P3c8,P3c9,P3da;
    ULONG   Part0Port,Part1Port,Part2Port;
    ULONG   Part3Port,Part4Port,Part5Port;
    USHORT   RVBHCFACT,RVBHCMAX,RVBHRS;
    USHORT   VGAVT,VGAHT,VGAVDE,VGAHDE;
    USHORT   VT,HT,VDE,HDE;
    USHORT   LCDHRS,LCDVRS,LCDHDES,LCDVDES;

    USHORT   ModeType;
    USHORT   IF_DEF_LVDS,IF_DEF_TRUMPION,IF_DEF_DSTN;/* ,IF_DEF_FSTN; add for dstn */
    USHORT   IF_DEF_CRT2Monitor,IF_DEF_VideoCapture;
    USHORT   IF_DEF_LCDA,IF_DEF_CH7017,IF_DEF_YPbPr,IF_DEF_ScaleLCD,IF_DEF_OEMUtil,IF_DEF_PWD;
    USHORT   IF_DEF_ExpLink;
    USHORT   IF_DEF_CH7005,IF_DEF_HiVision;
    USHORT   IF_DEF_CH7007; /* Billy 2007/05/03 */
    USHORT   LCDResInfo,LCDTypeInfo, VBType;/*301b*/
    USHORT   VBInfo,TVInfo,LCDInfo, Set_VGAType;
    USHORT   VBExtInfo;/*301lv*/
    USHORT   SetFlag;
    USHORT   NewFlickerMode;
    USHORT   SelectCRT2Rate;

    PUCHAR ROMAddr;
    PUCHAR FBAddr;
    ULONG BaseAddr;
    ULONG RelIO;

    DRAM4Type  *CR6B;
    DRAM4Type  *CR6E;
    DRAM32Type *CR6F;
    DRAM2Type  *CR89;

    DRAM8Type  *SR15; /* pointer : point to array */
    DRAM8Type  *CR40;
    UCHAR  *pSoftSetting;
    UCHAR  *pOutputSelect;

    USHORT *pRGBSenseData;
    USHORT *pRGBSenseData2; /*301b*/
    USHORT *pVideoSenseData;
    USHORT *pVideoSenseData2;
    USHORT *pYCSenseData;
    USHORT *pYCSenseData2;

    UCHAR  *pSR07;
    UCHAR  *CR49;
    UCHAR  *pSR1F;
    UCHAR  *AGPReg;
    UCHAR  *SR16;
    UCHAR  *pSR21;
    UCHAR  *pSR22;
    UCHAR  *pSR23;
    UCHAR  *pSR24;
    UCHAR  *SR25;
    UCHAR  *pSR31;
    UCHAR  *pSR32;
    UCHAR  *pSR33;
    UCHAR  *pSR36;      /* alan 12/07/2006 */
    UCHAR  *pCRCF;
    UCHAR  *pCRD0;      /* alan 12/07/2006 */
    UCHAR  *pCRDE;      /* alan 12/07/2006 */
    UCHAR  *pCR8F;      /* alan 12/07/2006 */
    UCHAR  *pSR40;      /* alan 12/07/2006 */
    UCHAR  *pSR41;      /* alan 12/07/2006 */
    UCHAR  *pDVOSetting;
    UCHAR  *pCR2E;
    UCHAR  *pCR2F;
    UCHAR  *pCR46;
    UCHAR  *pCR47;
    UCHAR  *pCRT2Data_1_2;
    UCHAR  *pCRT2Data_4_D;
    UCHAR  *pCRT2Data_4_E;
    UCHAR  *pCRT2Data_4_10;
    XGI_MCLKDataStruct  *MCLKData;
    XGI_ECLKDataStruct  *ECLKData;

    UCHAR   *XGI_TVDelayList;
    UCHAR   *XGI_TVDelayList2;
    UCHAR   *CHTVVCLKUNTSC;
    UCHAR   *CHTVVCLKONTSC;
    UCHAR   *CHTVVCLKUPAL;
    UCHAR   *CHTVVCLKOPAL;
    UCHAR   *NTSCTiming;
    UCHAR   *PALTiming;
    UCHAR   *HiTVExtTiming;
    UCHAR   *HiTVSt1Timing;
    UCHAR   *HiTVSt2Timing;
    UCHAR   *HiTVTextTiming;
    UCHAR   *YPbPr750pTiming;
    UCHAR   *YPbPr525pTiming;
    UCHAR   *YPbPr525iTiming;
    UCHAR   *HiTVGroup3Data;
    UCHAR   *HiTVGroup3Simu;
    UCHAR   *HiTVGroup3Text;
    UCHAR   *Ren525pGroup3;
    UCHAR   *Ren750pGroup3;
    UCHAR   *ScreenOffset;
    UCHAR   *pXGINew_DRAMTypeDefinition;
    UCHAR   *pXGINew_I2CDefinition ;
    UCHAR   *pXGINew_CR97 ;

    XGI330_LCDCapStruct  *LCDCapList;
    XGI21_LVDSCapStruct  *XG21_LVDSCapList;

    XGI_TimingHStruct  *TimingH;
    XGI_TimingVStruct  *TimingV;

    XGI_StStruct          *SModeIDTable;
    XGI_StandTableStruct  *StandTable;
    XGI_ExtStruct         *EModeIDTable;
    XGI_Ext2Struct        *RefIndex;
    /* XGINew_CRT1TableStruct *CRT1Table; */
    XGI_CRT1TableStruct    *XGINEWUB_CRT1Table;
    XGI_VCLKDataStruct    *VCLKData;
    XGI_VBVCLKDataStruct  *VBVCLKData;
    XGI_StResInfoStruct   *StResInfo;
    XGI_ModeResInfoStruct *ModeResInfo;
    XGI_XG21CRT1Struct	  *UpdateCRT1;
};  /* _VB_DEVICE_INFO */


typedef struct
{
    USHORT    Horizontal_ACTIVE;
    USHORT    Horizontal_FP;
    USHORT    Horizontal_SYNC;
    USHORT    Horizontal_BP;
    USHORT    Vertical_ACTIVE;
    USHORT    Vertical_FP;
    USHORT    Vertical_SYNC;
    USHORT    Vertical_BP;
    double    DCLK;
    UCHAR     FrameRate;
    UCHAR     Interlace;
    USHORT    Margin;
} TimingInfo;

#define _VB_STRUCT_
#endif /* _VB_STRUCT_ */
