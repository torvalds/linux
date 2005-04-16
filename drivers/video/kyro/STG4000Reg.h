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
 * Add an appropraite section for your OS or processor architecture.
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
	volatile unsigned long Thread0Enable;	/* 0x0000 */
	volatile unsigned long Thread1Enable;	/* 0x0004 */
	volatile unsigned long Thread0Recover;	/* 0x0008 */
	volatile unsigned long Thread1Recover;	/* 0x000C */
	volatile unsigned long Thread0Step;	/* 0x0010 */
	volatile unsigned long Thread1Step;	/* 0x0014 */
	volatile unsigned long VideoInStatus;	/* 0x0018 */
	volatile unsigned long Core2InSignStart;	/* 0x001C */
	volatile unsigned long Core1ResetVector;	/* 0x0020 */
	volatile unsigned long Core1ROMOffset;	/* 0x0024 */
	volatile unsigned long Core1ArbiterPriority;	/* 0x0028 */
	volatile unsigned long VideoInControl;	/* 0x002C */
	volatile unsigned long VideoInReg0CtrlA;	/* 0x0030 */
	volatile unsigned long VideoInReg0CtrlB;	/* 0x0034 */
	volatile unsigned long VideoInReg1CtrlA;	/* 0x0038 */
	volatile unsigned long VideoInReg1CtrlB;	/* 0x003C */
	volatile unsigned long Thread0Kicker;	/* 0x0040 */
	volatile unsigned long Core2InputSign;	/* 0x0044 */
	volatile unsigned long Thread0ProgCtr;	/* 0x0048 */
	volatile unsigned long Thread1ProgCtr;	/* 0x004C */
	volatile unsigned long Thread1Kicker;	/* 0x0050 */
	volatile unsigned long GPRegister1;	/* 0x0054 */
	volatile unsigned long GPRegister2;	/* 0x0058 */
	volatile unsigned long GPRegister3;	/* 0x005C */
	volatile unsigned long GPRegister4;	/* 0x0060 */
	volatile unsigned long SerialIntA;	/* 0x0064 */

	volatile unsigned long Fill0[6];	/* GAP 0x0068 - 0x007C */

	volatile unsigned long SoftwareReset;	/* 0x0080 */
	volatile unsigned long SerialIntB;	/* 0x0084 */

	volatile unsigned long Fill1[37];	/* GAP 0x0088 - 0x011C */

	volatile unsigned long ROMELQV;	/* 0x011C */
	volatile unsigned long WLWH;	/* 0x0120 */
	volatile unsigned long ROMELWL;	/* 0x0124 */

	volatile unsigned long dwFill_1;	/* GAP 0x0128 */

	volatile unsigned long IntStatus;	/* 0x012C */
	volatile unsigned long IntMask;	/* 0x0130 */
	volatile unsigned long IntClear;	/* 0x0134 */

	volatile unsigned long Fill2[6];	/* GAP 0x0138 - 0x014C */

	volatile unsigned long ROMGPIOA;	/* 0x0150 */
	volatile unsigned long ROMGPIOB;	/* 0x0154 */
	volatile unsigned long ROMGPIOC;	/* 0x0158 */
	volatile unsigned long ROMGPIOD;	/* 0x015C */

	volatile unsigned long Fill3[2];	/* GAP 0x0160 - 0x0168 */

	volatile unsigned long AGPIntID;	/* 0x0168 */
	volatile unsigned long AGPIntClassCode;	/* 0x016C */
	volatile unsigned long AGPIntBIST;	/* 0x0170 */
	volatile unsigned long AGPIntSSID;	/* 0x0174 */
	volatile unsigned long AGPIntPMCSR;	/* 0x0178 */
	volatile unsigned long VGAFrameBufBase;	/* 0x017C */
	volatile unsigned long VGANotify;	/* 0x0180 */
	volatile unsigned long DACPLLMode;	/* 0x0184 */
	volatile unsigned long Core1VideoClockDiv;	/* 0x0188 */
	volatile unsigned long AGPIntStat;	/* 0x018C */

	/*
	   volatile unsigned long Fill4[0x0400/4 - 0x0190/4]; //GAP 0x0190 - 0x0400
	   volatile unsigned long Fill5[0x05FC/4 - 0x0400/4]; //GAP 0x0400 - 0x05FC Fog Table
	   volatile unsigned long Fill6[0x0604/4 - 0x0600/4]; //GAP 0x0600 - 0x0604
	   volatile unsigned long Fill7[0x0680/4 - 0x0608/4]; //GAP 0x0608 - 0x0680
	   volatile unsigned long Fill8[0x07FC/4 - 0x0684/4]; //GAP 0x0684 - 0x07FC
	 */
	volatile unsigned long Fill4[412];	/* 0x0190 - 0x07FC */

	volatile unsigned long TACtrlStreamBase;	/* 0x0800 */
	volatile unsigned long TAObjDataBase;	/* 0x0804 */
	volatile unsigned long TAPtrDataBase;	/* 0x0808 */
	volatile unsigned long TARegionDataBase;	/* 0x080C */
	volatile unsigned long TATailPtrBase;	/* 0x0810 */
	volatile unsigned long TAPtrRegionSize;	/* 0x0814 */
	volatile unsigned long TAConfiguration;	/* 0x0818 */
	volatile unsigned long TAObjDataStartAddr;	/* 0x081C */
	volatile unsigned long TAObjDataEndAddr;	/* 0x0820 */
	volatile unsigned long TAXScreenClip;	/* 0x0824 */
	volatile unsigned long TAYScreenClip;	/* 0x0828 */
	volatile unsigned long TARHWClamp;	/* 0x082C */
	volatile unsigned long TARHWCompare;	/* 0x0830 */
	volatile unsigned long TAStart;	/* 0x0834 */
	volatile unsigned long TAObjReStart;	/* 0x0838 */
	volatile unsigned long TAPtrReStart;	/* 0x083C */
	volatile unsigned long TAStatus1;	/* 0x0840 */
	volatile unsigned long TAStatus2;	/* 0x0844 */
	volatile unsigned long TAIntStatus;	/* 0x0848 */
	volatile unsigned long TAIntMask;	/* 0x084C */

	volatile unsigned long Fill5[235];	/* GAP 0x0850 - 0x0BF8 */

	volatile unsigned long TextureAddrThresh;	/* 0x0BFC */
	volatile unsigned long Core1Translation;	/* 0x0C00 */
	volatile unsigned long TextureAddrReMap;	/* 0x0C04 */
	volatile unsigned long RenderOutAGPRemap;	/* 0x0C08 */
	volatile unsigned long _3DRegionReadTrans;	/* 0x0C0C */
	volatile unsigned long _3DPtrReadTrans;	/* 0x0C10 */
	volatile unsigned long _3DParamReadTrans;	/* 0x0C14 */
	volatile unsigned long _3DRegionReadThresh;	/* 0x0C18 */
	volatile unsigned long _3DPtrReadThresh;	/* 0x0C1C */
	volatile unsigned long _3DParamReadThresh;	/* 0x0C20 */
	volatile unsigned long _3DRegionReadAGPRemap;	/* 0x0C24 */
	volatile unsigned long _3DPtrReadAGPRemap;	/* 0x0C28 */
	volatile unsigned long _3DParamReadAGPRemap;	/* 0x0C2C */
	volatile unsigned long ZBufferAGPRemap;	/* 0x0C30 */
	volatile unsigned long TAIndexAGPRemap;	/* 0x0C34 */
	volatile unsigned long TAVertexAGPRemap;	/* 0x0C38 */
	volatile unsigned long TAUVAddrTrans;	/* 0x0C3C */
	volatile unsigned long TATailPtrCacheTrans;	/* 0x0C40 */
	volatile unsigned long TAParamWriteTrans;	/* 0x0C44 */
	volatile unsigned long TAPtrWriteTrans;	/* 0x0C48 */
	volatile unsigned long TAParamWriteThresh;	/* 0x0C4C */
	volatile unsigned long TAPtrWriteThresh;	/* 0x0C50 */
	volatile unsigned long TATailPtrCacheAGPRe;	/* 0x0C54 */
	volatile unsigned long TAParamWriteAGPRe;	/* 0x0C58 */
	volatile unsigned long TAPtrWriteAGPRe;	/* 0x0C5C */
	volatile unsigned long SDRAMArbiterConf;	/* 0x0C60 */
	volatile unsigned long SDRAMConf0;	/* 0x0C64 */
	volatile unsigned long SDRAMConf1;	/* 0x0C68 */
	volatile unsigned long SDRAMConf2;	/* 0x0C6C */
	volatile unsigned long SDRAMRefresh;	/* 0x0C70 */
	volatile unsigned long SDRAMPowerStat;	/* 0x0C74 */

	volatile unsigned long Fill6[2];	/* GAP 0x0C78 - 0x0C7C */

	volatile unsigned long RAMBistData;	/* 0x0C80 */
	volatile unsigned long RAMBistCtrl;	/* 0x0C84 */
	volatile unsigned long FIFOBistKey;	/* 0x0C88 */
	volatile unsigned long RAMBistResult;	/* 0x0C8C */
	volatile unsigned long FIFOBistResult;	/* 0x0C90 */

	/*
	   volatile unsigned long Fill11[0x0CBC/4 - 0x0C94/4]; //GAP 0x0C94 - 0x0CBC
	   volatile unsigned long Fill12[0x0CD0/4 - 0x0CC0/4]; //GAP 0x0CC0 - 0x0CD0 3DRegisters
	 */

	volatile unsigned long Fill7[16];	/* 0x0c94 - 0x0cd0 */

	volatile unsigned long SDRAMAddrSign;	/* 0x0CD4 */
	volatile unsigned long SDRAMDataSign;	/* 0x0CD8 */
	volatile unsigned long SDRAMSignConf;	/* 0x0CDC */

	/* DWFILL; //GAP 0x0CE0 */
	volatile unsigned long dwFill_2;

	volatile unsigned long ISPSignature;	/* 0x0CE4 */

	volatile unsigned long Fill8[454];	/*GAP 0x0CE8 - 0x13FC */

	volatile unsigned long DACPrimAddress;	/* 0x1400 */
	volatile unsigned long DACPrimSize;	/* 0x1404 */
	volatile unsigned long DACCursorAddr;	/* 0x1408 */
	volatile unsigned long DACCursorCtrl;	/* 0x140C */
	volatile unsigned long DACOverlayAddr;	/* 0x1410 */
	volatile unsigned long DACOverlayUAddr;	/* 0x1414 */
	volatile unsigned long DACOverlayVAddr;	/* 0x1418 */
	volatile unsigned long DACOverlaySize;	/* 0x141C */
	volatile unsigned long DACOverlayVtDec;	/* 0x1420 */

	volatile unsigned long Fill9[9];	/* GAP 0x1424 - 0x1444 */

	volatile unsigned long DACVerticalScal;	/* 0x1448 */
	volatile unsigned long DACPixelFormat;	/* 0x144C */
	volatile unsigned long DACHorizontalScal;	/* 0x1450 */
	volatile unsigned long DACVidWinStart;	/* 0x1454 */
	volatile unsigned long DACVidWinEnd;	/* 0x1458 */
	volatile unsigned long DACBlendCtrl;	/* 0x145C */
	volatile unsigned long DACHorTim1;	/* 0x1460 */
	volatile unsigned long DACHorTim2;	/* 0x1464 */
	volatile unsigned long DACHorTim3;	/* 0x1468 */
	volatile unsigned long DACVerTim1;	/* 0x146C */
	volatile unsigned long DACVerTim2;	/* 0x1470 */
	volatile unsigned long DACVerTim3;	/* 0x1474 */
	volatile unsigned long DACBorderColor;	/* 0x1478 */
	volatile unsigned long DACSyncCtrl;	/* 0x147C */
	volatile unsigned long DACStreamCtrl;	/* 0x1480 */
	volatile unsigned long DACLUTAddress;	/* 0x1484 */
	volatile unsigned long DACLUTData;	/* 0x1488 */
	volatile unsigned long DACBurstCtrl;	/* 0x148C */
	volatile unsigned long DACCrcTrigger;	/* 0x1490 */
	volatile unsigned long DACCrcDone;	/* 0x1494 */
	volatile unsigned long DACCrcResult1;	/* 0x1498 */
	volatile unsigned long DACCrcResult2;	/* 0x149C */
	volatile unsigned long DACLinecount;	/* 0x14A0 */

	volatile unsigned long Fill10[151];	/*GAP 0x14A4 - 0x16FC */

	volatile unsigned long DigVidPortCtrl;	/* 0x1700 */
	volatile unsigned long DigVidPortStat;	/* 0x1704 */

	/*
	   volatile unsigned long Fill11[0x1FFC/4 - 0x1708/4]; //GAP 0x1708 - 0x1FFC
	   volatile unsigned long Fill17[0x3000/4 - 0x2FFC/4]; //GAP 0x2000 - 0x2FFC ALUT
	 */

	volatile unsigned long Fill11[1598];

	/* DWFILL; //GAP 0x3000          ALUT 256MB offset */
	volatile unsigned long Fill_3;

} STG4000REG;

#endif /* _STG4000REG_H */
