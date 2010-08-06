/* $XFree86: xc/programs/Xserver/hw/xfree86/drivers/xgi/initdef.h,v 1.4 2000/12/02 01:16:17 dawes Exp $ */
#ifndef _INITDEF_
#define _INITDEF_

#ifndef NewScratch
#define NewScratch
#endif
/* shampoo */
#ifdef LINUX_KERNEL
#define SEQ_ADDRESS_PORT	  0x0014
#define SEQ_DATA_PORT		  0x0015
#define MISC_OUTPUT_REG_READ_PORT 0x001C
#define MISC_OUTPUT_REG_WRITE_PORT 0x0012
#define GRAPH_DATA_PORT		  0x1F
#define GRAPH_ADDRESS_PORT	  0x1E
#define XGI_MASK_DUAL_CHIP	  0x04  /* SR3A */
#define CRTC_ADDRESS_PORT_COLOR   0x0024
#define VIDEO_SUBSYSTEM_ENABLE_PORT 0x0013
#define PCI_COMMAND		0x04
#endif
/* ~shampoo */


#define VB_XGI301	      0x0001  /*301b*/
#define VB_XGI301B        0x0002
#define VB_XGI302B        0x0004
#define VB_XGI301LV     0x0008 /*301lv*/
#define VB_XGI302LV     0x0010
#define VB_XGI301C      0x0020       /* for 301C */
#define  VB_NoLCD        0x8000
/*end 301b*/

#define VB_YPbPrInfo     0x07          /*301lv*/
#define VB_YPbPr525i     0x00
#define VB_YPbPr525p     0x01
#define VB_YPbPr750p     0x02
#define VB_YPbPr1080i    0x03

/* #define CRT1Len 17 */
#define LVDSCRT1Len             15
#define CHTVRegDataLen          5

/* #define ModeInfoFlag 0x07 */
/* #define IsTextMode 0x07 */
/* #define ModeText 0x00 */
/* #define ModeCGA 0x01 */
/* #define ModeEGA 0x02 */
/* #define ModeVGA 0x03 */
/* #define Mode15Bpp 0x04 */
/* #define Mode16Bpp 0x05 */
/* #define Mode24Bpp 0x06 */
/* #define Mode32Bpp 0x07 */

/* #define DACInfoFlag 0x18 */
/* #define MemoryInfoFlag 0x1E0 */
/* #define MemorySizeShift 0x05 */

#define Charx8Dot               0x0200
#define LineCompareOff          0x0400
#define CRT2Mode                0x0800
#define HalfDCLK                0x1000
#define NoSupportSimuTV         0x2000
#define DoubleScanMode          0x8000

#define SupportAllCRT2          0x0078
#define SupportTV               0x0008
#define SupportHiVisionTV       0x0010
#define SupportLCD              0x0020
#define SupportRAMDAC2          0x0040
#define NoSupportTV             0x0070
#define NoSupportHiVisionTV     0x0060
#define NoSupportLCD            0x0058
#define SupportCHTV 		0x0800
#define SupportCRT2in301C       0x0100       /* for 301C */
#define SupportTV1024           0x0800  /*301b*/
#define SupportYPbPr            0x1000  /*301lv*/
#define InterlaceMode           0x0080
#define SyncPP                  0x0000
#define SyncPN                  0x4000
#define SyncNP                  0x8000
/* #define SyncNN 0xc000 */
#define ECLKindex0              0x0000
#define ECLKindex1              0x0100
#define ECLKindex2              0x0200
#define ECLKindex3              0x0300
#define ECLKindex4              0x0400

#define SetSimuScanMode         0x0001
#define SwitchToCRT2            0x0002
/* #define SetCRT2ToTV 0x009C */
#define SetCRT2ToAVIDEO         0x0004
#define SetCRT2ToSVIDEO         0x0008
#define SetCRT2ToSCART          0x0010
#define SetCRT2ToLCD            0x0020
#define SetCRT2ToRAMDAC         0x0040
#define SetCRT2ToHiVisionTV     0x0080
#define SetNTSCTV               0x0000
/* #define SetPALTV 0x0100 */
#define SetInSlaveMode          0x0200
#define SetNotSimuMode          0x0400
#define SetNotSimuTVMode        0x0400
#define SetDispDevSwitch        0x0800
#define LoadDACFlag             0x1000
#define DisableCRT2Display      0x2000
#define DriverMode              0x4000
#define HotKeySwitch            0x8000
#define SetCHTVOverScan  	0x8000
/* #define SetCRT2ToLCDA 0x8000 301b */
#define PanelRGB18Bit           0x0100
#define PanelRGB24Bit           0x0000

#define TVOverScan              0x10
#define TVOverScanShift         4
#define ClearBufferFlag         0x20
#define EnableDualEdge 		0x01		/*301b*/
#define SetToLCDA		0x02

#define YPbPrModeInfo           0x38
/* #define YPbPrMode525i 0x00 */
/* #define YPbPrMode525p 0x08 */
/* #define YPbPrMode750p 0x10 */
/* #define YPbPrMode1080i 0x18 */

#define SetSCARTOutput          0x01
#define BoardTVType             0x02
#define  EnablePALMN           0x40
/* #define ProgrammingCRT2 0x01 */
/* #define TVSimuMode 0x02 */
/* #define RPLLDIV2XO 0x04 */
/* #define LCDVESATiming 0x08 */
/* #define EnableLVDSDDA 0x10 */
#define SetDispDevSwitchFlag    0x20
#define CheckWinDos             0x40
#define SetJDOSMode             0x80

#define Panel320x480              0x07/*fstn*/
/* [ycchen] 02/12/03 Modify for Multi-Sync. LCD Support */
#define PanelResInfo            0x1F	/* CR36 Panel Type/LCDResInfo */
#define PanelRefInfo            0x60
#define Panel800x600            0x01
#define Panel1024x768           0x02
#define Panel1024x768x75        0x22
#define Panel1280x1024          0x03
#define Panel1280x1024x75       0x23
#define Panel640x480            0x04
#define Panel1024x600           0x05
#define Panel1152x864           0x06
#define Panel1280x960           0x07
#define Panel1152x768           0x08
#define Panel1400x1050          0x09
#define Panel1280x768           0x0A
#define Panel1600x1200          0x0B

#define PanelRef60Hz            0x00
#define PanelRef75Hz            0x20
#define LCDRGB18Bit             0x01

#define ExtChipTrumpion         0x06
#define ExtChipCH7005           0x08
#define ExtChipMitacTV          0x0a
#define LCDNonExpanding         0x10
#define LCDNonExpandingShift    4
#define LCDSync                 0x20
#define LCDSyncBit              0xe0
#define LCDSyncShift            6

/* #define DDC2DelayTime 300 */

#define CRT2DisplayFlag         0x2000
/* #define LCDDataLen 8 */
/* #define HiTVDataLen 12 */
/* #define TVDataLen 16 */
/* #define SetPALTV 0x0100 */
#define HalfDCLK                0x1000
#define NTSCHT                  1716
#define NTSCVT                  525
#define PALHT                   1728
#define PALVT                   625
#define StHiTVHT                892
#define StHiTVVT                1126
#define StHiTextTVHT            1000
#define StHiTextTVVT            1126
#define ExtHiTVHT               2100
#define ExtHiTVVT               1125

#define St750pTVHT              1716
#define St750pTVVT               525
#define Ext750pTVHT             1716
#define Ext750pTVVT              525
#define St525pTVHT              1716
#define St525pTVVT               525
#define Ext525pTVHT             1716
#define Ext525pTVVT              525
#define St525iTVHT              1716
#define St525iTVVT               525
#define Ext525iTVHT             1716
#define Ext525iTVVT              525

#define VCLKStartFreq           25
#define SoftDramType            0x80
#define VCLK40                  0x04

#define VCLK162             	0x21

#define LCDRGB18Bit             0x01
#define LoadDACFlag             0x1000
#define AfterLockCRT2           0x4000
#define SetCRT2ToAVIDEO         0x0004
#define SetCRT2ToSCART          0x0010
#define Ext2StructSize          5


#define YPbPr525iVCLK           0x03B
#define YPbPr525iVCLK_2         0x03A

#define SwitchToCRT2            0x0002
/* #define LCDVESATiming 0x08 */
#define SetSCARTOutput          0x01
#define AVIDEOSense             0x01
#define SVIDEOSense             0x02
#define SCARTSense              0x04
#define LCDSense                0x08
#define Monitor1Sense           0x20
#define Monitor2Sense           0x10
#define HiTVSense               0x40
#define BoardTVType             0x02
#define HotPlugFunction         0x08
#define StStructSize            0x06


#define XGI_CRT2_PORT_00        0x00 - 0x030
#define XGI_CRT2_PORT_04        0x04 - 0x030
#define XGI_CRT2_PORT_10        0x10 - 0x30
#define XGI_CRT2_PORT_12        0x12 - 0x30
#define XGI_CRT2_PORT_14        0x14 - 0x30


#define LCDNonExpanding         0x10
#define ADR_CRT2PtrData         0x20E
#define offset_Zurac            0x210
#define ADR_LVDSDesPtrData      0x212
#define ADR_LVDSCRT1DataPtr     0x214
#define ADR_CHTVVCLKPtr         0x216
#define ADR_CHTVRegDataPtr      0x218

#define LVDSDataLen             6
/* #define EnableLVDSDDA 0x10 */
/* #define LVDSDesDataLen 3 */
#define ActiveNonExpanding      0x40
#define ActiveNonExpandingShift 6
/* #define ActivePAL 0x20 */
#define ActivePALShift          5
/* #define ModeSwitchStatus 0x0F */
#define SoftTVType              0x40
#define SoftSettingAddr         0x52
#define ModeSettingAddr         0x53

/* #define SelectCRT1Rate 0x4 */

#define _PanelType00             0x00
#define _PanelType01             0x08
#define _PanelType02             0x10
#define _PanelType03             0x18
#define _PanelType04             0x20
#define _PanelType05             0x28
#define _PanelType06             0x30
#define _PanelType07             0x38
#define _PanelType08             0x40
#define _PanelType09             0x48
#define _PanelType0A             0x50
#define _PanelType0B             0x58
#define _PanelType0C             0x60
#define _PanelType0D             0x68
#define _PanelType0E             0x70
#define _PanelType0F             0x78


#define PRIMARY_VGA       0     /* 1: XGI is primary vga 0:XGI is secondary vga */
#define BIOSIDCodeAddr          0x235
#define OEMUtilIDCodeAddr       0x237
#define VBModeIDTableAddr       0x239
#define OEMTVPtrAddr            0x241
#define PhaseTableAddr          0x243
#define NTSCFilterTableAddr     0x245
#define PALFilterTableAddr      0x247
#define OEMLCDPtr_1Addr         0x249
#define OEMLCDPtr_2Addr         0x24B
#define LCDHPosTable_1Addr      0x24D
#define LCDHPosTable_2Addr      0x24F
#define LCDVPosTable_1Addr      0x251
#define LCDVPosTable_2Addr      0x253
#define OEMLCDPIDTableAddr      0x255

#define VBModeStructSize        5
#define PhaseTableSize          4
#define FilterTableSize         4
#define LCDHPosTableSize        7
#define LCDVPosTableSize        5
#define OEMLVDSPIDTableSize     4
#define LVDSHPosTableSize       4
#define LVDSVPosTableSize       6

#define VB_ModeID               0
#define VB_TVTableIndex         1
#define VB_LCDTableIndex        2
#define VB_LCDHIndex            3
#define VB_LCDVIndex            4

#define OEMLCDEnable            0x0001
#define OEMLCDDelayEnable       0x0002
#define OEMLCDPOSEnable         0x0004
#define OEMTVEnable             0x0100
#define OEMTVDelayEnable        0x0200
#define OEMTVFlickerEnable      0x0400
#define OEMTVPhaseEnable        0x0800
#define OEMTVFilterEnable       0x1000

#define OEMLCDPanelIDSupport    0x0080

/* #define LCDVESATiming 0x0001 //LCD Info CR37 */
/* #define EnableLVDSDDA 0x0002 */
#define EnableScalingLCD        0x0008
#define SetPWDEnable            0x0004
#define SetLCDtoNonExpanding    0x0010
/* #define SetLCDPolarity 0x00E0 */
#define SetLCDDualLink          0x0100
#define SetLCDLowResolution     0x0200
#define SetLCDStdMode           0x0400
#define SetTVStdMode            0x0200
#define SetTVLowResolution      0x0400
/* =============================================================
   for 310
============================================================== */
#define SoftDRAMType        0x80
#define SoftSetting_OFFSET  0x52
#define SR07_OFFSET  0x7C
#define SR15_OFFSET  0x7D
#define SR16_OFFSET  0x81
#define SR17_OFFSET  0x85
#define SR19_OFFSET  0x8D
#define SR1F_OFFSET  0x99
#define SR21_OFFSET  0x9A
#define SR22_OFFSET  0x9B
#define SR23_OFFSET  0x9C
#define SR24_OFFSET  0x9D
#define SR25_OFFSET  0x9E
#define SR31_OFFSET  0x9F
#define SR32_OFFSET  0xA0
#define SR33_OFFSET  0xA1

#define CR40_OFFSET  0xA2
#define SR25_1_OFFSET  0xF6
#define CR49_OFFSET  0xF7

#define VB310Data_1_2_Offset  0xB6
#define VB310Data_4_D_Offset  0xB7
#define VB310Data_4_E_Offset  0xB8
#define VB310Data_4_10_Offset 0xBB

#define RGBSenseDataOffset    0xBD
#define YCSenseDataOffset     0xBF
#define VideoSenseDataOffset  0xC1
#define OutputSelectOffset    0xF3

#define ECLK_MCLK_DISTANCE  0x14
#define VBIOSTablePointerStart    0x200
#define StandTablePtrOffset       VBIOSTablePointerStart+0x02
#define EModeIDTablePtrOffset     VBIOSTablePointerStart+0x04
#define CRT1TablePtrOffset        VBIOSTablePointerStart+0x06
#define ScreenOffsetPtrOffset     VBIOSTablePointerStart+0x08
#define VCLKDataPtrOffset         VBIOSTablePointerStart+0x0A
#define MCLKDataPtrOffset         VBIOSTablePointerStart+0x0E
#define CRT2PtrDataPtrOffset      VBIOSTablePointerStart+0x10
#define TVAntiFlickPtrOffset      VBIOSTablePointerStart+0x12
#define TVDelayPtr1Offset         VBIOSTablePointerStart+0x14
#define TVPhaseIncrPtr1Offset     VBIOSTablePointerStart+0x16
#define TVYFilterPtr1Offset       VBIOSTablePointerStart+0x18
#define LCDDelayPtr1Offset        VBIOSTablePointerStart+0x20
#define TVEdgePtr1Offset          VBIOSTablePointerStart+0x24
#define CRT2Delay1Offset          VBIOSTablePointerStart+0x28
#define LCDDataDesOffset     VBIOSTablePointerStart-0x02
#define LCDDataPtrOffset          VBIOSTablePointerStart+0x2A
#define LCDDesDataPtrOffset     VBIOSTablePointerStart+0x2C
#define LCDDataList		VBIOSTablePointerStart+0x22	/* add for GetLCDPtr */
#define TVDataList		VBIOSTablePointerStart+0x36	/* add for GetTVPtr */
/*  */
/* Modify from 310.inc */
/*  */
/*  */


#define		ShowMsgFlag                  0x20               /* SoftSetting */
#define		ShowVESAFlag                 0x10
#define		HotPlugFunction              0x08
#define		ModeSoftSetting              0x04
#define		TVSoftSetting                0x02
#define		LCDSoftSetting               0x01

#define		GatingCRTinLCDA              0x10
#define		SetHiTVOutput                0x08
#define		SetYPbPrOutput               0x04
#define		BoardTVType                  0x02
#define		SetSCARTOutput               0x01

#define		ModeSettingYPbPr             0x02               /* TVModeSetting, Others as same as CR30 */

/* TVModeSetting same as CR35 */

/* LCDModeSetting same as CR37 */

#define		EnableNewTVFont              0x10               /* MiscCapability */

#define		EnableLCDOutput              0x80               /* LCDCfgSetting */

#define		SoftDRAMType                 0x80               /* DRAMSetting */
#define		SoftDRAMConfig               0x40
#define		MosSelDRAMType               0x20
#define		SDRAM                        000h
#define		SGRAM                        0x01
#define		ESDRAM                       0x02

#define		EnableAGPCfgSetting          0x01               /* AGPCfgSetting */

/* ---------------- SetMode Stack */
#define		CRT1Len                      15
#define		VCLKLen                      4
#define		DefThreshold                 0x0100
#define		ExtRegsSize                  (57+8+37+70+63+28+768+1)/64+1

#define		VGA_XGI315                   0x0001       /* VGA Type Info */
#define		VGA_SNewis315e                  0x0002       /* 315 series */
#define		VGA_XGI550                   0x0004
#define		VGA_XGI640                   0x0008
#define		VGA_XGI740                   0x0010
#define		VGA_XGI650                   0x0020
#define		VGA_XGI650M                  0x0040
#define		VGA_XGI651                   0x0080
#define		VGA_XGI340                   0x0001       /* 340 series */
#define		VGA_XGI330                   0x0001       /* 330 series */
#define		VGA_XGI660                   0x0001       /* 660 series */

#define		VB_XGI301                    0x0001       /* VB Type Info */
#define		VB_XGI301B                   0x0002       /* 301 series */
#define		VB_XGI302B                   0x0004
#define		VB_NoLCD                     0x8000
#define		VB_XGI301LV                  0x0008
#define		VB_XGI302LV                  0x0010
#define		VB_LVDS_NS                   0x0001       /* 3rd party chip */
#define		VB_CH7017                    0x0002
#define         VB_CH7007                    0x0080       /* [Billy] 07/05/03 */
/* #define VB_LVDS_SI 0x0004 */

#define		ModeInfoFlag                 0x0007
#define		IsTextMode                   0x0007
#define		ModeText                     0x0000
#define		ModeCGA                      0x0001
#define		ModeEGA                      0x0002       /* 16 colors mode */
#define		ModeVGA                      0x0003       /* 256 colors mode */
#define		Mode15Bpp                    0x0004       /* 15 Bpp Color Mode */
#define		Mode16Bpp                    0x0005       /* 16 Bpp Color Mode */
#define		Mode24Bpp                    0x0006       /* 24 Bpp Color Mode */
#define		Mode32Bpp                    0x0007       /* 32 Bpp Color Mode */

#define		DACInfoFlag                  0x0018
#define		MONODAC                      0x0000
#define		CGADAC                       0x0008
#define		EGADAC                       0x0010
#define		VGADAC                       0x0018

#define		MemoryInfoFlag               0x01e0
#define		MemorySizeShift              5
#define		Need1MSize                   0x0000
#define		Need2MSize                   0x0020
#define		Need4MSize                   0x0060
#define		Need8MSize                   0x00e0
#define		Need16MSize                  0x01e0

#define		Charx8Dot                    0x0200
#define		LineCompareOff               0x0400
#define		CRT2Mode                     0x0800
#define		HalfDCLK                     0x1000
#define		NoSupportSimuTV              0x2000
#define		DoubleScanMode               0x8000

/* -------------- Ext_InfoFlag */
#define		SupportModeInfo              0x0007
#define		Support256                   0x0003
#define		Support15Bpp                 0x0004
#define		Support16Bpp                 0x0005
#define		Support24Bpp                 0x0006
#define		Support32Bpp                 0x0007

#define		SupportAllCRT2               0x0078
#define		SupportTV                    0x0008
#define		SupportHiVisionTV            0x0010
#define		SupportLCD                   0x0020
#define		SupportRAMDAC2               0x0040
#define		NoSupportTV                  0x0070
#define		NoSupportHiVisionTV          0x0060
#define		NoSupportLCD                 0x0058
#define		SupportTV1024                0x0800       /* 301btest */
#define		SupportYPbPr                 0x1000       /* 301lv */
#define		InterlaceMode                0x0080
#define		SyncPP                       0x0000
#define		SyncPN                       0x4000
#define		SyncNP                       0x8000
#define		SyncNN                       0xC000

/* -------------- SetMode Stack/Scratch */
#define		SetSimuScanMode              0x0001       /* VBInfo/CR30 & CR31 */
#define		SwitchToCRT2                 0x0002
#define		SetCRT2ToTV1                 0x009C
#define		SetCRT2ToTV                  0x089C
#define		SetCRT2ToAVIDEO              0x0004
#define		SetCRT2ToSVIDEO              0x0008
#define		SetCRT2ToSCART               0x0010
#define		SetCRT2ToLCD                 0x0020
#define		SetCRT2ToRAMDAC              0x0040
#define		SetCRT2ToHiVisionTV          0x0080
#define		SetCRT2ToLCDA                0x0100
#define		SetInSlaveMode               0x0200
#define		SetNotSimuMode               0x0400
#define		HKEventMode                  0x0800
#define		SetCRT2ToYPbPr               0x0800
#define		LoadDACFlag                  0x1000
#define		DisableCRT2Display           0x2000
#define		DriverMode                   0x4000
#define		SetCRT2ToDualEdge            0x8000
#define		HotKeySwitch                 0x8000

#define		ProgrammingCRT2              0x0001       /* Set Flag */
#define		EnableVCMode                 0x0002
#define		SetHKEventMode               0x0004
#define		ReserveTVOption              0x0008
#define		DisableRelocateIO            0x0010
#define		Win9xDOSMode                 0x0020
#define		JDOSMode                     0x0040
/* #define SetWin9xforJap 0x0080 // not used now */
/* #define SetWin9xforKorea 0x0100 // not used now */
#define		GatingCRT                    0x0800
#define		DisableChB                   0x1000
#define		EnableChB                    0x2000
#define		DisableChA                   0x4000
#define		EnableChA                    0x8000

#define		SetNTSCTV                    0x0000       /* TV Info */
#define		SetPALTV                     0x0001
#define		SetNTSCJ                     0x0002
#define		SetPALMTV                    0x0004
#define		SetPALNTV                    0x0008
#define		SetCHTVUnderScan             0x0000
/* #define SetCHTVOverScan 0x0010 */
#define		SetYPbPrMode525i             0x0020
#define		SetYPbPrMode525p             0x0040
#define		SetYPbPrMode750p             0x0080
#define		SetYPbPrMode1080i            0x0100
#define		SetTVStdMode                 0x0200
#define		SetTVLowResolution           0x0400
#define		SetTVSimuMode                0x0800
#define		TVSimuMode                   0x0800
#define		RPLLDIV2XO                   0x1000
#define		NTSC1024x768                 0x2000
#define		SetTVLockMode                0x4000

#define		LCDVESATiming                0x0001       /* LCD Info/CR37 */
#define		EnableLVDSDDA                0x0002
#define		EnableScalingLCD             0x0008
#define		SetPWDEnable                 0x0004
#define		SetLCDtoNonExpanding         0x0010
#define		SetLCDPolarity               0x00e0
#define		SetLCDDualLink               0x0100
#define		SetLCDLowResolution          0x0200
#define		SetLCDStdMode                0x0400

#define		DefaultLCDCap                0x80ea       /* LCD Capability shampoo */
#define		RLVDSDHL00                   0x0000
#define		RLVDSDHL01                   0x0001
#define		RLVDSDHL10                   0x0002       /* default */
#define		RLVDSDHL11                   0x0003
#define		EnableLCD24bpp               0x0004       /* default */
#define		DisableLCD24bpp              0x0000
#define		RLVDSClkSFT0                 0x0000
#define		RLVDSClkSFT1                 0x0008       /* default */
#define		EnableLVDSDCBal              0x0010
#define		DisableLVDSDCBal             0x0000       /* default */
#define		SinglePolarity               0x0020       /* default */
#define		MultiPolarity                0x0000
#define		LCDPolarity                  0x00c0       /* default: SyncNN */
#define		LCDSingleLink                0x0000       /* default */
#define		LCDDualLink                  0x0100
#define		EnableSpectrum               0x0200
#define		DisableSpectrum              0x0000       /* default */
#define		PWDEnable                    0x0400
#define		PWDDisable                   0x0000       /* default */
#define		PWMEnable                    0x0800
#define		PWMDisable                   0x0000       /* default */
#define		EnableVBCLKDRVLOW            0x4000
#define		EnableVBCLKDRVHigh           0x0000       /* default */
#define		EnablePLLSPLOW               0x8000
#define		EnablePLLSPHigh              0x0000       /* default */

#define		LCDBToA                      0x20               /* LCD SetFlag */
#define		StLCDBToA                    0x40
#define		LockLCDBToA                  0x80
#define 	LCDToFull             	     0x10
#define		AVIDEOSense                  0x01               /* CR32 */
#define		SVIDEOSense                  0x02
#define		SCARTSense                   0x04
#define		LCDSense                     0x08
#define		Monitor2Sense                0x10
#define		Monitor1Sense                0x20
#define		HiTVSense                    0x40

#ifdef                   NewScratch
#define		YPbPrSense                   0x80    /* NEW SCRATCH */
#endif

#define		TVSense                      0xc7

#define		TVOverScan                   0x10               /* CR35 */
#define		TVOverScanShift              4

#ifdef                   NewScratch
#define		NTSCMode                     0x00
#define		PALMode                      0x00
#define		NTSCJMode                    0x02
#define		PALMNMode                    0x0c
#define		YPbPrMode                    0xe0
#define		YPbPrMode525i                0x00
#define		YPbPrMode525p                0x20
#define		YPbPrMode750p                0x40
#define		YPbPrMode1080i               0x60
#else                    /* Old Scratch */
#define		ClearBufferFlag              0x20
#endif


#define		LCDRGB18Bit                  0x01               /* CR37 */
#define		LCDNonExpanding              0x10
#define		LCDNonExpandingShift         4
#define		LCDSync                      0x20
#define		LCDSyncBit                   0xe0               /* H/V polarity & sync ID */
#define		LCDSyncShift                 6

#ifdef                   NewScratch
#define		ScalingLCD                   0x08
#else                    /* Old Scratch */
#define		ExtChipType                  0x0e
#define		ExtChip301                   0x02
#define		ExtChipLVDS                  0x04
#define		ExtChipCH7019                0x06
#define		ScalingLCD                   0x10
#endif

#define		EnableDualEdge               0x01               /* CR38 */
#define		SetToLCDA                    0x02
#ifdef                   NewScratch
#define		SetYPbPr                     0x04
#define		DisableChannelA              0x08
#define		DisableChannelB              0x10
#define		ExtChipType                  0xe0
#define		ExtChip301                   0x20
#define		ExtChipLVDS                  0x40
#define		ExtChipCH7019                0x60
#else                    /* Old Scratch */
#define		YPbPrSense                   0x04
#define		SetYPbPr                     0x08
#define		YPbPrMode                    0x30
#define		YPbPrMode525i                0x00
#define		YPbPrMode525p                0x10
#define		YPbPrMode750p                0x20
#define		YPbPrMode1080i               0x30
#define		PALMNMode                    0xc0
#endif

#define		BacklightControlBit          0x01               /* CR3A */
#define		Win9xforJap                  0x40
#define		Win9xforKorea                0x80

#define		ForceMDBits                  0x07               /* CR3B */
#define		ForceMD_JDOS                 0x00
#define		ForceMD_640x400T             0x01
#define		ForceMD_640x350T             0x02
#define		ForceMD_720x400T             0x03
#define		ForceMD_640x480E             0x04
#define		ForceMD_640x400E             0x05
#define		ForceP1Bit                   0x10
#define		ForceP2Bit                   0x20
#define		EnableForceMDinBIOS          0x40
#define		EnableForceMDinDrv           0x80

#ifdef                   NewScratch                      /* New Scratch */
/* ---------------------- VUMA Information */
#define		LCDSettingFromCMOS           0x04               /* CR3C */
#define		TVSettingFromCMOS            0x08
#define		DisplayDeviceFromCMOS        0x10
#define		HKSupportInSBIOS             0x20
#define		OSDSupportInSBIOS            0x40
#define		DisableLogo                  0x80

/* ---------------------- HK Evnet Definition */
#define		HKEvent                      0x0f               /* CR3D */
#define		HK_ModeSwitch                0x01
#define		HK_Expanding                 0x02
#define		HK_OverScan                  0x03
#define		HK_Brightness                0x04
#define		HK_Contrast                  0x05
#define		HK_Mute                      0x06
#define		HK_Volume                    0x07
#define		ModeSwitchStatus             0xf0
#define		ActiveCRT1                   0x10
#define		ActiveLCD                    0x0020
#define		ActiveTV                     0x40
#define		ActiveCRT2                   0x80

#define		TVSwitchStatus               0x1f               /* CR3E */
#define		ActiveAVideo                 0x01
#define		ActiveSVideo                 0x02
#define		ActiveSCART                  0x04
#define		ActiveHiTV                   0x08
#define		ActiveYPbPr                  0x10

#define		EnableHKEvent                0x01               /* CR3F */
#define		EnableOSDEvent               0x02
#define		StartOSDEvent                0x04
#define		IgnoreHKEvent                0x08
#define		IgnoreOSDEvent               0x10
#else                    /* Old Scratch */
#define		OSD_SBIOS                    0x02       /* SR17 */
#define		DisableLogo                  0x04
#define		SelectKDOS                   0x08
#define		KorWinMode                   0x10
#define		KorMode3Bit                  0x0020
#define		PSCCtrlBit                  0x40
#define		NPSCCtrlBitShift             6
#define		BlueScreenBit                0x80

#define		HKEvent                      0x0f       /* CR79 */
#define		HK_ModeSwitch                0x01
#define		HK_Expanding                 0x02
#define		HK_OverScan                  0x03
#define		HK_Brightness                0x04
#define		HK_Contrast                  0x05
#define		HK_Mute                      0x06
#define		HK_Volume                    0x07
#define		ActivePAL                    0x0020
#define		ActivePALShift               5
#define		ActiveNonExpanding           0x40
#define		ActiveNonExpandingShift      6
#define		ActiveOverScan               0x80
#define		ActiveOverScanShift          7

#define		ModeSwitchStatus             0x0b       /* SR15 */
#define		ActiveCRT1                   0x01
#define		ActiveLCD                    0x02
#define		ActiveCRT2                   0x08

#define		TVSwitchStatus               0xf0       /* SR16 */
#define		TVConfigShift                3
#define		ActiveTV                     0x01
#define		ActiveYPbPr                  0x04
#define		ActiveAVideo                 0x10
#define		ActiveSVideo                 0x0020
#define		ActiveSCART                  0x40
#define		ActiveHiTV                   0x80

#define		EnableHKEvent                0x01       /* CR7A */
#define		EnableOSDEvent               0x02
#define		StartOSDEvent                0x04
#define		CMOSSupport                  0x08
#define		HotKeySupport                0x10
#define		IngoreHKOSDEvent             0x20
#endif

/* //------------- Misc. Definition */
#define		SelectCRT1Rate               00h
/* #define SelectCRT2Rate 04h */

#define		DDC1DelayTime                1000
#ifdef           TRUMPION
#define		DDC2DelayTime                15
#else
#define		DDC2DelayTime                150
#endif

#define		R_FACTOR                     04Dh
#define		G_FACTOR                     097h
#define		B_FACTOR                     01Ch
/* --------------------------------------------------------- */
/* translated from asm code 301def.h */
/*  */
/* --------------------------------------------------------- */
#define		LCDDataLen                   8
#define		HiTVDataLen                  12
#define		TVDataLen                    12
#define		LVDSCRT1Len_H                8
#define		LVDSCRT1Len_V                7
#define		LVDSDataLen                  6
#define		LVDSDesDataLen               6
#define		LCDDesDataLen                6
#define		LVDSDesDataLen2              8
#define		LCDDesDataLen2               8
#define		CHTVRegLen                   16
#define		CHLVRegLen                   12

#define		StHiTVHT                     892
#define		StHiTVVT                     1126
#define		StHiTextTVHT                 1000
#define		StHiTextTVVT                 1126
#define		ExtHiTVHT                    2100
#define		ExtHiTVVT                    1125
#define		NTSCHT                       1716
#define		NTSCVT                        525
#define		NTSC1024x768HT               1908
#define		NTSC1024x768VT                525
#define		PALHT                        1728
#define		PALVT                         625

#define		YPbPrTV525iHT                1716            /* YPbPr */
#define		YPbPrTV525iVT                 525
#define		YPbPrTV525pHT                1716
#define		YPbPrTV525pVT                 525
#define		YPbPrTV750pHT                1650
#define		YPbPrTV750pVT                 750

#define		CRT2VCLKSel                  0xc0

#define		CRT2Delay1      	     0x04            /* XGI301 */
#define		CRT2Delay2      	     0x0A            /* 301B,302 */


#define		VCLK25_175           0x00
#define		VCLK28_322           0x01
#define		VCLK31_5             0x02
#define		VCLK36               0x03
#define		VCLK40               0x04
#define		VCLK43_163           0x05
#define		VCLK44_9             0x06
#define		VCLK49_5             0x07
#define		VCLK50               0x08
#define		VCLK52_406           0x09
#define		VCLK56_25            0x0A
#define		VCLK65               0x0B
#define		VCLK67_765           0x0C
#define		VCLK68_179           0x0D
#define		VCLK72_852           0x0E
#define		VCLK75               0x0F
#define		VCLK75_8             0x10
#define		VCLK78_75            0x11
#define		VCLK79_411           0x12
#define		VCLK83_95            0x13
#define		VCLK84_8             0x14
#define		VCLK86_6             0x15
#define		VCLK94_5             0x16
#define		VCLK104_998          0x17
#define		VCLK105_882          0x18
#define		VCLK108_2            0x19
#define		VCLK109_175          0x1A
#define		VCLK113_309          0x1B
#define		VCLK116_406          0x1C
#define		VCLK132_258          0x1D
#define		VCLK135_5            0x1E
#define		VCLK139_054          0x1F
#define		VCLK157_5            0x20
#define		VCLK162              0x21
#define		VCLK175              0x22
#define		VCLK189              0x23
#define		VCLK194_4            0x24
#define		VCLK202_5            0x25
#define		VCLK229_5            0x26
#define		VCLK234              0x27
#define		VCLK252_699          0x28
#define		VCLK254_817          0x29
#define		VCLK265_728          0x2A
#define		VCLK266_952          0x2B
#define		VCLK269_655          0x2C
#define		VCLK272_042          0x2D
#define		VCLK277_015          0x2E
#define		VCLK286_359          0x2F
#define		VCLK291_132          0x30
#define		VCLK291_766          0x31
#define		VCLK309_789          0x32
#define		VCLK315_195          0x33
#define		VCLK323_586          0x34
#define		VCLK330_615          0x35
#define		VCLK332_177          0x36
#define		VCLK340_477          0x37
#define		VCLK375_847          0x38
#define		VCLK388_631          0x39
#define		VCLK125_999          0x51
#define		VCLK148_5            0x52
#define		VCLK178_992          0x54
#define		VCLK217_325          0x55
#define		VCLK299_505          0x56
#define		YPbPr750pVCLK        0x57

#define		TVVCLKDIV2              0x3A
#define		TVVCLK                  0x3B
#define		HiTVVCLKDIV2          0x3C
#define		HiTVVCLK              0x3D
#define		HiTVSimuVCLK          0x3E
#define		HiTVTextVCLK          0x3F
#define		VCLK39_77              0x40
/* #define YPbPr750pVCLK 0x0F */
#define		YPbPr525pVCLK           0x3A
/* #define ;;YPbPr525iVCLK 0x3B */
/* #define ;;YPbPr525iVCLK_2 0x3A */
#define		NTSC1024VCLK         0x41
#define		VCLK25_175_41        0x42                  /* ; ScaleLCD */
#define		VCLK25_175_42        0x43
#define		VCLK28_322_43        0x44
#define		VCLK40_44            0x45
#define		VCLKQVGA_1           0x46                   /* ; QVGA */
#define		VCLKQVGA_2           0x47
#define		VCLKQVGA_3           0x48
#define		VCLK35_2             0x49                    /* ; 800x480 */
#define		VCLK122_61           0x4A
#define		VCLK80_350           0x4B
#define		VCLK107_385          0x4C

#define		CHTVVCLK30_2         0x50                 /* ;;CHTV */
#define		CHTVVCLK28_1         0x51
#define		CHTVVCLK43_6         0x52
#define		CHTVVCLK26_4         0x53
#define		CHTVVCLK24_6         0x54
#define		CHTVVCLK47_8         0x55
#define		CHTVVCLK31_5         0x56
#define		CHTVVCLK26_2         0x57
#define		CHTVVCLK39           0x58
#define		CHTVVCLK36           0x59

#define		CH7007TVVCLK30_2     0x00                 /* [Billy] 2007/05/18 For CH7007 */
#define		CH7007TVVCLK28_1     0x01
#define		CH7007TVVCLK43_6     0x02
#define		CH7007TVVCLK26_4     0x03
#define		CH7007TVVCLK24_6     0x04
#define		CH7007TVVCLK47_8     0x05
#define		CH7007TVVCLK31_5     0x06
#define		CH7007TVVCLK26_2     0x07
#define		CH7007TVVCLK39       0x08
#define		CH7007TVVCLK36       0x09

#define		RES320x200                   0x00
#define		RES320x240                   0x01
#define		RES400x300                   0x02
#define		RES512x384                   0x03
#define		RES640x400                   0x04
#define		RES640x480x60                0x05
#define		RES640x480x72                0x06
#define		RES640x480x75                0x07
#define		RES640x480x85                0x08
#define		RES640x480x100               0x09
#define		RES640x480x120               0x0A
#define		RES640x480x160               0x0B
#define		RES640x480x200               0x0C
#define		RES800x600x56                0x0D
#define		RES800x600x60                0x0E
#define		RES800x600x72                0x0F
#define		RES800x600x75                0x10
#define		RES800x600x85                0x11
#define		RES800x600x100               0x12
#define		RES800x600x120               0x13
#define		RES800x600x160               0x14
#define		RES1024x768x43               0x15
#define		RES1024x768x60               0x16
#define		RES1024x768x70               0x17
#define		RES1024x768x75               0x18
#define		RES1024x768x85               0x19
#define		RES1024x768x100              0x1A
#define		RES1024x768x120              0x1B
#define		RES1280x1024x43              0x1C
#define		RES1280x1024x60              0x1D
#define		RES1280x1024x75              0x1E
#define		RES1280x1024x85              0x1F
#define		RES1600x1200x60              0x20
#define		RES1600x1200x65              0x21
#define		RES1600x1200x70              0x22
#define		RES1600x1200x75              0x23
#define		RES1600x1200x85              0x24
#define		RES1600x1200x100             0x25
#define		RES1600x1200x120             0x26
#define		RES1920x1440x60              0x27
#define		RES1920x1440x65              0x28
#define		RES1920x1440x70              0x29
#define		RES1920x1440x75              0x2A
#define		RES1920x1440x85              0x2B
#define		RES1920x1440x100             0x2C
#define		RES2048x1536x60              0x2D
#define		RES2048x1536x65              0x2E
#define		RES2048x1536x70              0x2F
#define		RES2048x1536x75              0x30
#define		RES2048x1536x85              0x31
#define		RES800x480x60                0x32
#define		RES800x480x75                0x33
#define		RES800x480x85                0x34
#define		RES1024x576x60               0x35
#define		RES1024x576x75               0x36
#define		RES1024x576x85               0x37
#define		RES1280x720x60               0x38
#define		RES1280x720x75               0x39
#define		RES1280x720x85               0x3A
#define		RES1280x960x60               0x3B
#define		RES720x480x60                0x3C
#define		RES720x576x56                0x3D
#define		RES856x480x79I               0x3E
#define		RES856x480x60                0x3F
#define		RES1280x768x60               0x40
#define		RES1400x1050x60              0x41
#define		RES1152x864x60               0x42
#define		RES1152x864x75               0x43
#define		RES1024x768x160              0x44
#define		RES1280x960x75               0x45
#define		RES1280x960x85               0x46
#define		RES1280x960x120              0x47

#define 	LFBDRAMTrap                  0x30
#endif
