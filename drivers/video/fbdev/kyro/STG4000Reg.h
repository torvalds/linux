/*
 *  linux/drivers/video/kyro/STG4000Reg.h
 *
 *  Copyright (C) 2002 STMicroelectronics
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */

#ifndef _STG4000REG_H
#define _STG4000REG_H

#define DWFILL unsigned long :32
#define WFILL unsigned short :16

/*
 * Macros that access memory mapped card registers in PCI space
 * Add an appropriate section for your OS or processor architecture.
 */
#if defined(__KERNEL__)
#include <asm/page.h>
#include <asm/io.h>
#define STG_WRITE_REG(reg,data) (writel(data,&pSTGReg->reg))
#define STG_READ_REG(reg)      (readl(&pSTGReg->reg))
#else
#define STG_WRITE_REG(reg,data) (pSTGReg->reg = data)
#define STG_READ_REG(reg)      (pSTGReg->reg)
#endif /* __KERNEL__ */

#define SET_BIT(n) (1<<(n))
#define CLEAR_BIT(n) (tmp &= ~(1<<n))
#define CLEAR_BITS_FRM_TO(frm, to) \
{\
int i; \
    for(i = frm; i<= to; i++) \
	{ \
	    tmp &= ~(1<<i); \
	} \
}

#define CLEAR_BIT_2(n) (usTemp &= ~(1<<n))
#define CLEAR_BITS_FRM_TO_2(frm, to) \
{\
int i; \
    for(i = frm; i<= to; i++) \
	{ \
	    usTemp &= ~(1<<i); \
	} \
}

/* LUT select */
typedef enum _LUT_USES {
	NO_LUT = 0, RESERVED, GRAPHICS, OVERLAY
} LUT_USES;

/* Primary surface pixel format select */
typedef enum _PIXEL_FORMAT {
	_8BPP = 0, _15BPP, _16BPP, _24BPP, _32BPP
} PIXEL_FORMAT;

/* Overlay blending mode select */
typedef enum _BLEND_MODE {
	GRAPHICS_MODE = 0, COLOR_KEY, PER_PIXEL_ALPHA, GLOBAL_ALPHA,
	CK_PIXEL_ALPHA, CK_GLOBAL_ALPHA
} OVRL_BLEND_MODE;

/* Overlay Pixel format select */
typedef enum _OVRL_PIX_FORMAT {
	UYVY, VYUY, YUYV, YVYU
} OVRL_PIX_FORMAT;

/* Register Table */
typedef struct {
	/* 0h  */
	volatile u32 Thread0Enable;	/* 0x0000 */
	volatile u32 Thread1Enable;	/* 0x0004 */
	volatile u32 Thread0Recover;	/* 0x0008 */
	volatile u32 Thread1Recover;	/* 0x000C */
	volatile u32 Thread0Step;	/* 0x0010 */
	volatile u32 Thread1Step;	/* 0x0014 */
	volatile u32 VideoInStatus;	/* 0x0018 */
	volatile u32 Core2InSignStart;	/* 0x001C */
	volatile u32 Core1ResetVector;	/* 0x0020 */
	volatile u32 Core1ROMOffset;	/* 0x0024 */
	volatile u32 Core1ArbiterPriority;	/* 0x0028 */
	volatile u32 VideoInControl;	/* 0x002C */
	volatile u32 VideoInReg0CtrlA;	/* 0x0030 */
	volatile u32 VideoInReg0CtrlB;	/* 0x0034 */
	volatile u32 VideoInReg1CtrlA;	/* 0x0038 */
	volatile u32 VideoInReg1CtrlB;	/* 0x003C */
	volatile u32 Thread0Kicker;	/* 0x0040 */
	volatile u32 Core2InputSign;	/* 0x0044 */
	volatile u32 Thread0ProgCtr;	/* 0x0048 */
	volatile u32 Thread1ProgCtr;	/* 0x004C */
	volatile u32 Thread1Kicker;	/* 0x0050 */
	volatile u32 GPRegister1;	/* 0x0054 */
	volatile u32 GPRegister2;	/* 0x0058 */
	volatile u32 GPRegister3;	/* 0x005C */
	volatile u32 GPRegister4;	/* 0x0060 */
	volatile u32 SerialIntA;	/* 0x0064 */

	volatile u32 Fill0[6];	/* GAP 0x0068 - 0x007C */

	volatile u32 SoftwareReset;	/* 0x0080 */
	volatile u32 SerialIntB;	/* 0x0084 */

	volatile u32 Fill1[37];	/* GAP 0x0088 - 0x011C */

	volatile u32 ROMELQV;	/* 0x011C */
	volatile u32 WLWH;	/* 0x0120 */
	volatile u32 ROMELWL;	/* 0x0124 */

	volatile u32 dwFill_1;	/* GAP 0x0128 */

	volatile u32 IntStatus;	/* 0x012C */
	volatile u32 IntMask;	/* 0x0130 */
	volatile u32 IntClear;	/* 0x0134 */

	volatile u32 Fill2[6];	/* GAP 0x0138 - 0x014C */

	volatile u32 ROMGPIOA;	/* 0x0150 */
	volatile u32 ROMGPIOB;	/* 0x0154 */
	volatile u32 ROMGPIOC;	/* 0x0158 */
	volatile u32 ROMGPIOD;	/* 0x015C */

	volatile u32 Fill3[2];	/* GAP 0x0160 - 0x0168 */

	volatile u32 AGPIntID;	/* 0x0168 */
	volatile u32 AGPIntClassCode;	/* 0x016C */
	volatile u32 AGPIntBIST;	/* 0x0170 */
	volatile u32 AGPIntSSID;	/* 0x0174 */
	volatile u32 AGPIntPMCSR;	/* 0x0178 */
	volatile u32 VGAFrameBufBase;	/* 0x017C */
	volatile u32 VGANotify;	/* 0x0180 */
	volatile u32 DACPLLMode;	/* 0x0184 */
	volatile u32 Core1VideoClockDiv;	/* 0x0188 */
	volatile u32 AGPIntStat;	/* 0x018C */

	/*
	   volatile u32 Fill4[0x0400/4 - 0x0190/4]; //GAP 0x0190 - 0x0400
	   volatile u32 Fill5[0x05FC/4 - 0x0400/4]; //GAP 0x0400 - 0x05FC Fog Table
	   volatile u32 Fill6[0x0604/4 - 0x0600/4]; //GAP 0x0600 - 0x0604
	   volatile u32 Fill7[0x0680/4 - 0x0608/4]; //GAP 0x0608 - 0x0680
	   volatile u32 Fill8[0x07FC/4 - 0x0684/4]; //GAP 0x0684 - 0x07FC
	 */
	volatile u32 Fill4[412];	/* 0x0190 - 0x07FC */

	volatile u32 TACtrlStreamBase;	/* 0x0800 */
	volatile u32 TAObjDataBase;	/* 0x0804 */
	volatile u32 TAPtrDataBase;	/* 0x0808 */
	volatile u32 TARegionDataBase;	/* 0x080C */
	volatile u32 TATailPtrBase;	/* 0x0810 */
	volatile u32 TAPtrRegionSize;	/* 0x0814 */
	volatile u32 TAConfiguration;	/* 0x0818 */
	volatile u32 TAObjDataStartAddr;	/* 0x081C */
	volatile u32 TAObjDataEndAddr;	/* 0x0820 */
	volatile u32 TAXScreenClip;	/* 0x0824 */
	volatile u32 TAYScreenClip;	/* 0x0828 */
	volatile u32 TARHWClamp;	/* 0x082C */
	volatile u32 TARHWCompare;	/* 0x0830 */
	volatile u32 TAStart;	/* 0x0834 */
	volatile u32 TAObjReStart;	/* 0x0838 */
	volatile u32 TAPtrReStart;	/* 0x083C */
	volatile u32 TAStatus1;	/* 0x0840 */
	volatile u32 TAStatus2;	/* 0x0844 */
	volatile u32 TAIntStatus;	/* 0x0848 */
	volatile u32 TAIntMask;	/* 0x084C */

	volatile u32 Fill5[235];	/* GAP 0x0850 - 0x0BF8 */

	volatile u32 TextureAddrThresh;	/* 0x0BFC */
	volatile u32 Core1Translation;	/* 0x0C00 */
	volatile u32 TextureAddrReMap;	/* 0x0C04 */
	volatile u32 RenderOutAGPRemap;	/* 0x0C08 */
	volatile u32 _3DRegionReadTrans;	/* 0x0C0C */
	volatile u32 _3DPtrReadTrans;	/* 0x0C10 */
	volatile u32 _3DParamReadTrans;	/* 0x0C14 */
	volatile u32 _3DRegionReadThresh;	/* 0x0C18 */
	volatile u32 _3DPtrReadThresh;	/* 0x0C1C */
	volatile u32 _3DParamReadThresh;	/* 0x0C20 */
	volatile u32 _3DRegionReadAGPRemap;	/* 0x0C24 */
	volatile u32 _3DPtrReadAGPRemap;	/* 0x0C28 */
	volatile u32 _3DParamReadAGPRemap;	/* 0x0C2C */
	volatile u32 ZBufferAGPRemap;	/* 0x0C30 */
	volatile u32 TAIndexAGPRemap;	/* 0x0C34 */
	volatile u32 TAVertexAGPRemap;	/* 0x0C38 */
	volatile u32 TAUVAddrTrans;	/* 0x0C3C */
	volatile u32 TATailPtrCacheTrans;	/* 0x0C40 */
	volatile u32 TAParamWriteTrans;	/* 0x0C44 */
	volatile u32 TAPtrWriteTrans;	/* 0x0C48 */
	volatile u32 TAParamWriteThresh;	/* 0x0C4C */
	volatile u32 TAPtrWriteThresh;	/* 0x0C50 */
	volatile u32 TATailPtrCacheAGPRe;	/* 0x0C54 */
	volatile u32 TAParamWriteAGPRe;	/* 0x0C58 */
	volatile u32 TAPtrWriteAGPRe;	/* 0x0C5C */
	volatile u32 SDRAMArbiterConf;	/* 0x0C60 */
	volatile u32 SDRAMConf0;	/* 0x0C64 */
	volatile u32 SDRAMConf1;	/* 0x0C68 */
	volatile u32 SDRAMConf2;	/* 0x0C6C */
	volatile u32 SDRAMRefresh;	/* 0x0C70 */
	volatile u32 SDRAMPowerStat;	/* 0x0C74 */

	volatile u32 Fill6[2];	/* GAP 0x0C78 - 0x0C7C */

	volatile u32 RAMBistData;	/* 0x0C80 */
	volatile u32 RAMBistCtrl;	/* 0x0C84 */
	volatile u32 FIFOBistKey;	/* 0x0C88 */
	volatile u32 RAMBistResult;	/* 0x0C8C */
	volatile u32 FIFOBistResult;	/* 0x0C90 */

	/*
	   volatile u32 Fill11[0x0CBC/4 - 0x0C94/4]; //GAP 0x0C94 - 0x0CBC
	   volatile u32 Fill12[0x0CD0/4 - 0x0CC0/4]; //GAP 0x0CC0 - 0x0CD0 3DRegisters
	 */

	volatile u32 Fill7[16];	/* 0x0c94 - 0x0cd0 */

	volatile u32 SDRAMAddrSign;	/* 0x0CD4 */
	volatile u32 SDRAMDataSign;	/* 0x0CD8 */
	volatile u32 SDRAMSignConf;	/* 0x0CDC */

	/* DWFILL; //GAP 0x0CE0 */
	volatile u32 dwFill_2;

	volatile u32 ISPSignature;	/* 0x0CE4 */

	volatile u32 Fill8[454];	/*GAP 0x0CE8 - 0x13FC */

	volatile u32 DACPrimAddress;	/* 0x1400 */
	volatile u32 DACPrimSize;	/* 0x1404 */
	volatile u32 DACCursorAddr;	/* 0x1408 */
	volatile u32 DACCursorCtrl;	/* 0x140C */
	volatile u32 DACOverlayAddr;	/* 0x1410 */
	volatile u32 DACOverlayUAddr;	/* 0x1414 */
	volatile u32 DACOverlayVAddr;	/* 0x1418 */
	volatile u32 DACOverlaySize;	/* 0x141C */
	volatile u32 DACOverlayVtDec;	/* 0x1420 */

	volatile u32 Fill9[9];	/* GAP 0x1424 - 0x1444 */

	volatile u32 DACVerticalScal;	/* 0x1448 */
	volatile u32 DACPixelFormat;	/* 0x144C */
	volatile u32 DACHorizontalScal;	/* 0x1450 */
	volatile u32 DACVidWinStart;	/* 0x1454 */
	volatile u32 DACVidWinEnd;	/* 0x1458 */
	volatile u32 DACBlendCtrl;	/* 0x145C */
	volatile u32 DACHorTim1;	/* 0x1460 */
	volatile u32 DACHorTim2;	/* 0x1464 */
	volatile u32 DACHorTim3;	/* 0x1468 */
	volatile u32 DACVerTim1;	/* 0x146C */
	volatile u32 DACVerTim2;	/* 0x1470 */
	volatile u32 DACVerTim3;	/* 0x1474 */
	volatile u32 DACBorderColor;	/* 0x1478 */
	volatile u32 DACSyncCtrl;	/* 0x147C */
	volatile u32 DACStreamCtrl;	/* 0x1480 */
	volatile u32 DACLUTAddress;	/* 0x1484 */
	volatile u32 DACLUTData;	/* 0x1488 */
	volatile u32 DACBurstCtrl;	/* 0x148C */
	volatile u32 DACCrcTrigger;	/* 0x1490 */
	volatile u32 DACCrcDone;	/* 0x1494 */
	volatile u32 DACCrcResult1;	/* 0x1498 */
	volatile u32 DACCrcResult2;	/* 0x149C */
	volatile u32 DACLinecount;	/* 0x14A0 */

	volatile u32 Fill10[151];	/*GAP 0x14A4 - 0x16FC */

	volatile u32 DigVidPortCtrl;	/* 0x1700 */
	volatile u32 DigVidPortStat;	/* 0x1704 */

	/*
	   volatile u32 Fill11[0x1FFC/4 - 0x1708/4]; //GAP 0x1708 - 0x1FFC
	   volatile u32 Fill17[0x3000/4 - 0x2FFC/4]; //GAP 0x2000 - 0x2FFC ALUT
	 */

	volatile u32 Fill11[1598];

	/* DWFILL; //GAP 0x3000          ALUT 256MB offset */
	volatile u32 Fill_3;

} STG4000REG;

#endif /* _STG4000REG_H */
