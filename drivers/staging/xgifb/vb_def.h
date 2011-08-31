/* $XFree86: xc/programs/Xserver/hw/xfree86/drivers/xgi/initdef.h
 * ,v 1.4 2000/12/02 01:16:17 dawes Exp $*/
#ifndef _INITDEF_
#define _INITDEF_

#define VB_XGI301C      0x0020 /* for 301C */
/*end 301b*/

#define VB_YPbPr525p    0x01
#define VB_YPbPr750p    0x02
#define VB_YPbPr1080i   0x03

#define LVDSCRT1Len     15

#define SupportCHTV             0x0800
#define SupportCRT2in301C       0x0100  /* for 301C */
#define SetCHTVOverScan         0x8000
#define PanelRGB18Bit           0x0100
#define PanelRGB24Bit           0x0000

#define Panel320x480            0x07 /*fstn*/
/* [ycchen] 02/12/03 Modify for Multi-Sync. LCD Support */
#define PanelResInfo            0x1F /* CR36 Panel Type/LCDResInfo */
#define Panel800x600            0x01
#define Panel1024x768           0x02
#define Panel1024x768x75        0x22
#define Panel1280x1024          0x03
#define Panel1280x1024x75       0x23
#define Panel640x480            0x04
#define Panel1280x960           0x07
#define Panel1400x1050          0x09
#define Panel1600x1200          0x0B

#define PanelRef60Hz            0x00
#define PanelRef75Hz            0x20

#define CRT2DisplayFlag         0x2000

#define YPbPr525iVCLK           0x03B
#define YPbPr525iVCLK_2         0x03A

#define XGI_CRT2_PORT_00        (0x00 - 0x030)
#define XGI_CRT2_PORT_04        (0x04 - 0x030)
#define XGI_CRT2_PORT_10        (0x10 - 0x30)
#define XGI_CRT2_PORT_12        (0x12 - 0x30)
#define XGI_CRT2_PORT_14        (0x14 - 0x30)

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

/* =============================================================
   for 310
============================================================== */
/* add LCDDataList for GetLCDPtr */
#define LCDDataList               (VBIOSTablePointerStart+0x22)
/*  */
/* Modify from 310.inc */
/*  */
/*  */

#define ModeSoftSetting              0x04

#define BoardTVType                  0x02

#define SoftDRAMType                 0x80    /* DRAMSetting */

/* ---------------- SetMode Stack */
#define CRT1Len           15
#define VCLKLen           4
#define VGA_XGI340        0x0001       /* 340 series */

#define VB_XGI301         0x0001       /* VB Type Info */
#define VB_XGI301B        0x0002       /* 301 series */
#define VB_XGI302B        0x0004
#define VB_NoLCD          0x8000
#define VB_XGI301LV       0x0008
#define VB_XGI302LV       0x0010
#define VB_LVDS_NS        0x0001       /* 3rd party chip */

#define ModeInfoFlag      0x0007
#define ModeText          0x0000
#define ModeEGA           0x0002    /* 16 colors mode */
#define ModeVGA           0x0003    /* 256 colors mode */

#define DACInfoFlag       0x0018

#define MemoryInfoFlag    0x01e0
#define MemorySizeShift   5

#define Charx8Dot         0x0200
#define LineCompareOff    0x0400
#define CRT2Mode          0x0800
#define HalfDCLK          0x1000
#define NoSupportSimuTV   0x2000
#define DoubleScanMode    0x8000

/* -------------- Ext_InfoFlag */
#define Support16Bpp        0x0005
#define Support32Bpp        0x0007

#define SupportAllCRT2      0x0078
#define SupportTV           0x0008
#define SupportHiVisionTV   0x0010
#define SupportLCD          0x0020
#define SupportRAMDAC2      0x0040
#define NoSupportTV         0x0070
#define NoSupportHiVisionTV 0x0060
#define NoSupportLCD        0x0058
#define SupportTV1024       0x0800 /* 301btest */
#define SupportYPbPr        0x1000 /* 301lv */
#define InterlaceMode       0x0080
#define SyncPP              0x0000
#define SyncPN              0x4000
#define SyncNP              0x8000
#define SyncNN              0xC000

/* -------------- SetMode Stack/Scratch */
#define SetSimuScanMode     0x0001    /* VBInfo/CR30 & CR31 */
#define SwitchToCRT2        0x0002
#define SetCRT2ToTV         0x089C
#define SetCRT2ToAVIDEO     0x0004
#define SetCRT2ToSVIDEO     0x0008
#define SetCRT2ToSCART      0x0010
#define SetCRT2ToLCD        0x0020
#define SetCRT2ToRAMDAC     0x0040
#define SetCRT2ToHiVisionTV 0x0080
#define SetCRT2ToLCDA       0x0100
#define SetInSlaveMode      0x0200
#define SetNotSimuMode      0x0400
#define SetCRT2ToYPbPr      0x0800
#define LoadDACFlag         0x1000
#define DisableCRT2Display  0x2000
#define DriverMode          0x4000
#define SetCRT2ToDualEdge   0x8000

#define ProgrammingCRT2     0x0001       /* Set Flag */
#define ReserveTVOption     0x0008
#define GatingCRT           0x0800
#define DisableChB          0x1000
#define EnableChB           0x2000
#define DisableChA          0x4000
#define EnableChA           0x8000

#define SetNTSCTV           0x0000 /* TV Info */
#define SetPALTV            0x0001
#define SetNTSCJ            0x0002
#define SetPALMTV           0x0004
#define SetPALNTV           0x0008
#define SetYPbPrMode525i     0x0020
#define SetYPbPrMode525p     0x0040
#define SetYPbPrMode750p     0x0080
#define SetYPbPrMode1080i    0x0100
#define SetTVLowResolution   0x0400
#define TVSimuMode           0x0800
#define RPLLDIV2XO           0x1000
#define NTSC1024x768         0x2000
#define SetTVLockMode        0x4000

#define LCDVESATiming        0x0001 /* LCD Info/CR37 */
#define EnableLVDSDDA        0x0002
#define EnableScalingLCD     0x0008
#define SetPWDEnable         0x0004
#define SetLCDtoNonExpanding 0x0010
#define SetLCDDualLink       0x0100
#define SetLCDLowResolution  0x0200
#define SetLCDStdMode        0x0400

/* LCD Capability shampoo */
#define DefaultLCDCap        0x80ea
#define EnableLCD24bpp       0x0004 /* default */
#define DisableLCD24bpp      0x0000
#define LCDPolarity          0x00c0 /* default: SyncNN */
#define LCDDualLink          0x0100
#define EnableSpectrum       0x0200
#define PWDEnable            0x0400
#define EnableVBCLKDRVLOW    0x4000
#define EnablePLLSPLOW       0x8000

#define LCDBToA              0x20   /* LCD SetFlag */
#define StLCDBToA            0x40
#define LockLCDBToA          0x80
#define   LCDToFull          0x10
#define AVIDEOSense          0x01   /* CR32 */
#define SVIDEOSense          0x02
#define SCARTSense           0x04
#define LCDSense             0x08
#define Monitor2Sense        0x10
#define Monitor1Sense        0x20
#define HiTVSense            0x40

#define YPbPrSense           0x80   /* NEW SCRATCH */

#define TVSense              0xc7

#define TVOverScan           0x10               /* CR35 */

#define YPbPrMode            0xe0
#define YPbPrMode525i        0x00
#define YPbPrMode525p        0x20
#define YPbPrMode750p        0x40
#define YPbPrMode1080i       0x60


#define LCDRGB18Bit          0x01               /* CR37 */
#define LCDNonExpanding      0x10
#define LCDSync              0x20
#define LCDSyncBit           0xe0 /* H/V polarity & sync ID */

#define ScalingLCD           0x08

#define EnableDualEdge       0x01 /* CR38 */
#define SetToLCDA            0x02
#define SetYPbPr             0x04

/* ---------------------- VUMA Information */
#define DisplayDeviceFromCMOS 0x10

/* ---------------------- HK Evnet Definition */
#define ModeSwitchStatus      0xf0
#define ActiveCRT1            0x10
#define ActiveLCD             0x0020
#define ActiveTV              0x40
#define ActiveCRT2            0x80

#define ActiveAVideo          0x01
#define ActiveSVideo          0x02
#define ActiveSCART           0x04
#define ActiveHiTV            0x08
#define ActiveYPbPr           0x10

/* --------------------------------------------------------- */
/* translated from asm code 301def.h */
/*  */
/* --------------------------------------------------------- */
#define LCDDataLen           8
#define TVDataLen            12
#define LVDSCRT1Len_H        8
#define LVDSCRT1Len_V        7
#define LVDSDataLen          6
#define LVDSDesDataLen       6
#define LCDDesDataLen        6
#define LVDSDesDataLen2      8
#define LCDDesDataLen2       8
#define CHTVRegLen           16

#define StHiTVHT             892
#define StHiTVVT             1126
#define StHiTextTVHT         1000
#define StHiTextTVVT         1126
#define ExtHiTVHT            2100
#define ExtHiTVVT            1125
#define NTSCHT               1716
#define NTSCVT                525
#define NTSC1024x768HT       1908
#define PALHT                1728
#define PALVT                 625

#define YPbPrTV525iHT        1716 /* YPbPr */
#define YPbPrTV525iVT         525
#define YPbPrTV525pHT        1716
#define YPbPrTV525pVT         525
#define YPbPrTV750pHT        1650
#define YPbPrTV750pVT         750

#define CRT2Delay1           0x04 /* XGI301 */
#define CRT2Delay2           0x0A /* 301B,302 */


#define VCLK25_175           0x00
#define VCLK28_322           0x01
#define VCLK31_5             0x02
#define VCLK36               0x03
#define VCLK40               0x04
#define VCLK43_163           0x05
#define VCLK44_9             0x06
#define VCLK49_5             0x07
#define VCLK50               0x08
#define VCLK52_406           0x09
#define VCLK56_25            0x0A
#define VCLK65               0x0B
#define VCLK68_179           0x0D
#define VCLK72_852           0x0E
#define VCLK75               0x0F
#define VCLK78_75            0x11
#define VCLK79_411           0x12
#define VCLK83_95            0x13
#define VCLK86_6             0x15
#define VCLK94_5             0x16
#define VCLK108_2            0x19
#define VCLK113_309          0x1B
#define VCLK116_406          0x1C
#define VCLK135_5            0x1E
#define VCLK139_054          0x1F
#define VCLK157_5            0x20
#define VCLK162              0x21
#define VCLK175              0x22
#define VCLK189              0x23
#define VCLK202_5            0x25
#define VCLK229_5            0x26
#define VCLK234              0x27
#define VCLK254_817          0x29
#define VCLK266_952          0x2B
#define VCLK269_655          0x2C
#define VCLK277_015          0x2E
#define VCLK291_132          0x30
#define VCLK291_766          0x31
#define VCLK315_195          0x33
#define VCLK323_586          0x34
#define VCLK330_615          0x35
#define VCLK340_477          0x37
#define VCLK375_847          0x38
#define VCLK388_631          0x39
#define VCLK125_999          0x51
#define VCLK148_5            0x52
#define VCLK217_325          0x55
#define YPbPr750pVCLK        0x57

#define TVVCLKDIV2           0x3A
#define TVVCLK               0x3B
#define HiTVVCLKDIV2         0x3C
#define HiTVVCLK             0x3D
#define HiTVSimuVCLK         0x3E
#define HiTVTextVCLK         0x3F
#define VCLK39_77            0x40
#define YPbPr525pVCLK           0x3A
#define NTSC1024VCLK         0x41
#define VCLK35_2             0x49 /* ; 800x480 */
#define VCLK122_61           0x4A
#define VCLK80_350           0x4B
#define VCLK107_385          0x4C

#define RES320x200           0x00
#define RES320x240           0x01
#define RES400x300           0x02
#define RES512x384           0x03
#define RES640x400           0x04
#define RES640x480x60        0x05
#define RES640x480x72        0x06
#define RES640x480x75        0x07
#define RES640x480x85        0x08
#define RES640x480x100       0x09
#define RES640x480x120       0x0A
#define RES640x480x160       0x0B
#define RES640x480x200       0x0C
#define RES800x600x56        0x0D
#define RES800x600x60        0x0E
#define RES800x600x72        0x0F
#define RES800x600x75        0x10
#define RES800x600x85        0x11
#define RES800x600x100       0x12
#define RES800x600x120       0x13
#define RES800x600x160       0x14
#define RES1024x768x43       0x15
#define RES1024x768x60       0x16
#define RES1024x768x70       0x17
#define RES1024x768x75       0x18
#define RES1024x768x85       0x19
#define RES1024x768x100      0x1A
#define RES1024x768x120      0x1B
#define RES1280x1024x43      0x1C
#define RES1280x1024x60      0x1D
#define RES1280x1024x75      0x1E
#define RES1280x1024x85      0x1F
#define RES1600x1200x60      0x20
#define RES1600x1200x65      0x21
#define RES1600x1200x70      0x22
#define RES1600x1200x75      0x23
#define RES1600x1200x85      0x24
#define RES1600x1200x100     0x25
#define RES1600x1200x120     0x26
#define RES1920x1440x60      0x27
#define RES1920x1440x65      0x28
#define RES1920x1440x70      0x29
#define RES1920x1440x75      0x2A
#define RES1920x1440x85      0x2B
#define RES1920x1440x100     0x2C
#define RES2048x1536x60      0x2D
#define RES2048x1536x65      0x2E
#define RES2048x1536x70      0x2F
#define RES2048x1536x75      0x30
#define RES2048x1536x85      0x31
#define RES800x480x60        0x32
#define RES800x480x75        0x33
#define RES800x480x85        0x34
#define RES1024x576x60       0x35
#define RES1024x576x75       0x36
#define RES1024x576x85       0x37
#define RES1280x720x60       0x38
#define RES1280x720x75       0x39
#define RES1280x720x85       0x3A
#define RES1280x960x60       0x3B
#define RES720x480x60        0x3C
#define RES720x576x56        0x3D
#define RES856x480x79I       0x3E
#define RES856x480x60        0x3F
#define RES1280x768x60       0x40
#define RES1400x1050x60      0x41
#define RES1152x864x60       0x42
#define RES1152x864x75       0x43
#define RES1024x768x160      0x44
#define RES1280x960x75       0x45
#define RES1280x960x85       0x46
#define RES1280x960x120      0x47

#endif
