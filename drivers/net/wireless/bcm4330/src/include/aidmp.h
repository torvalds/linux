/*
 * Broadcom AMBA Interconnect definitions.
 *
 * Copyright (C) 1999-2011, Broadcom Corporation
 * 
 *         Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 * 
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 * 
 *      Notwithstanding the above, under no circumstances may you combine this
 * software in any way with any other Broadcom software provided under a license
 * other than the GPL, without Broadcom's express prior written consent.
 *
 * $Id: aidmp.h,v 13.4.14.1 2010-03-09 18:40:06 Exp $
 */


#ifndef	_AIDMP_H
#define	_AIDMP_H


#define	MFGID_ARM		0x43b
#define	MFGID_BRCM		0x4bf
#define	MFGID_MIPS		0x4a7


#define	CC_SIM			0
#define	CC_EROM			1
#define	CC_CORESIGHT		9
#define	CC_VERIF		0xb
#define	CC_OPTIMO		0xd
#define	CC_GEN			0xe
#define	CC_PRIMECELL		0xf


#define	ER_EROMENTRY		0x000
#define	ER_REMAPCONTROL		0xe00
#define	ER_REMAPSELECT		0xe04
#define	ER_MASTERSELECT		0xe10
#define	ER_ITCR			0xf00
#define	ER_ITIP			0xf04


#define	ER_TAG			0xe
#define	ER_TAG1			0x6
#define	ER_VALID		1
#define	ER_CI			0
#define	ER_MP			2
#define	ER_ADD			4
#define	ER_END			0xe
#define	ER_BAD			0xffffffff


#define	CIA_MFG_MASK		0xfff00000
#define	CIA_MFG_SHIFT		20
#define	CIA_CID_MASK		0x000fff00
#define	CIA_CID_SHIFT		8
#define	CIA_CCL_MASK		0x000000f0
#define	CIA_CCL_SHIFT		4


#define	CIB_REV_MASK		0xff000000
#define	CIB_REV_SHIFT		24
#define	CIB_NSW_MASK		0x00f80000
#define	CIB_NSW_SHIFT		19
#define	CIB_NMW_MASK		0x0007c000
#define	CIB_NMW_SHIFT		14
#define	CIB_NSP_MASK		0x00003e00
#define	CIB_NSP_SHIFT		9
#define	CIB_NMP_MASK		0x000001f0
#define	CIB_NMP_SHIFT		4


#define	MPD_MUI_MASK		0x0000ff00
#define	MPD_MUI_SHIFT		8
#define	MPD_MP_MASK		0x000000f0
#define	MPD_MP_SHIFT		4


#define	AD_ADDR_MASK		0xfffff000
#define	AD_SP_MASK		0x00000f00
#define	AD_SP_SHIFT		8
#define	AD_ST_MASK		0x000000c0
#define	AD_ST_SHIFT		6
#define	AD_ST_SLAVE		0x00000000
#define	AD_ST_BRIDGE		0x00000040
#define	AD_ST_SWRAP		0x00000080
#define	AD_ST_MWRAP		0x000000c0
#define	AD_SZ_MASK		0x00000030
#define	AD_SZ_SHIFT		4
#define	AD_SZ_4K		0x00000000
#define	AD_SZ_8K		0x00000010
#define	AD_SZ_16K		0x00000020
#define	AD_SZ_SZD		0x00000030
#define	AD_AG32			0x00000008
#define	AD_ADDR_ALIGN		0x00000fff
#define	AD_SZ_BASE		0x00001000	


#define	SD_SZ_MASK		0xfffff000
#define	SD_SG32			0x00000008
#define	SD_SZ_ALIGN		0x00000fff


#ifndef _LANGUAGE_ASSEMBLY

typedef volatile struct _aidmp {
	uint32	oobselina30;	
	uint32	oobselina74;	
	uint32	PAD[6];
	uint32	oobselinb30;	
	uint32	oobselinb74;	
	uint32	PAD[6];
	uint32	oobselinc30;	
	uint32	oobselinc74;	
	uint32	PAD[6];
	uint32	oobselind30;	
	uint32	oobselind74;	
	uint32	PAD[38];
	uint32	oobselouta30;	
	uint32	oobselouta74;	
	uint32	PAD[6];
	uint32	oobseloutb30;	
	uint32	oobseloutb74;	
	uint32	PAD[6];
	uint32	oobseloutc30;	
	uint32	oobseloutc74;	
	uint32	PAD[6];
	uint32	oobseloutd30;	
	uint32	oobseloutd74;	
	uint32	PAD[38];
	uint32	oobsynca;	
	uint32	oobseloutaen;	
	uint32	PAD[6];
	uint32	oobsyncb;	
	uint32	oobseloutben;	
	uint32	PAD[6];
	uint32	oobsyncc;	
	uint32	oobseloutcen;	
	uint32	PAD[6];
	uint32	oobsyncd;	
	uint32	oobseloutden;	
	uint32	PAD[38];
	uint32	oobaextwidth;	
	uint32	oobainwidth;	
	uint32	oobaoutwidth;	
	uint32	PAD[5];
	uint32	oobbextwidth;	
	uint32	oobbinwidth;	
	uint32	oobboutwidth;	
	uint32	PAD[5];
	uint32	oobcextwidth;	
	uint32	oobcinwidth;	
	uint32	oobcoutwidth;	
	uint32	PAD[5];
	uint32	oobdextwidth;	
	uint32	oobdinwidth;	
	uint32	oobdoutwidth;	
	uint32	PAD[37];
	uint32	ioctrlset;	
	uint32	ioctrlclear;	
	uint32	ioctrl;		
	uint32	PAD[61];
	uint32	iostatus;	
	uint32	PAD[127];
	uint32	ioctrlwidth;	
	uint32	iostatuswidth;	
	uint32	PAD[62];
	uint32	resetctrl;	
	uint32	resetstatus;	
	uint32	resetreadid;	
	uint32	resetwriteid;	
	uint32	PAD[60];
	uint32	errlogctrl;	
	uint32	errlogdone;	
	uint32	errlogstatus;	
	uint32	errlogaddrlo;	
	uint32	errlogaddrhi;	
	uint32	errlogid;	
	uint32	errloguser;	
	uint32	errlogflags;	
	uint32	PAD[56];
	uint32	intstatus;	
	uint32	PAD[127];
	uint32	config;		
	uint32	PAD[63];
	uint32	itcr;		
	uint32	PAD[3];
	uint32	itipooba;	
	uint32	itipoobb;	
	uint32	itipoobc;	
	uint32	itipoobd;	
	uint32	PAD[4];
	uint32	itipoobaout;	
	uint32	itipoobbout;	
	uint32	itipoobcout;	
	uint32	itipoobdout;	
	uint32	PAD[4];
	uint32	itopooba;	
	uint32	itopoobb;	
	uint32	itopoobc;	
	uint32	itopoobd;	
	uint32	PAD[4];
	uint32	itopoobain;	
	uint32	itopoobbin;	
	uint32	itopoobcin;	
	uint32	itopoobdin;	
	uint32	PAD[4];
	uint32	itopreset;	
	uint32	PAD[15];
	uint32	peripherialid4;	
	uint32	peripherialid5;	
	uint32	peripherialid6;	
	uint32	peripherialid7;	
	uint32	peripherialid0;	
	uint32	peripherialid1;	
	uint32	peripherialid2;	
	uint32	peripherialid3;	
	uint32	componentid0;	
	uint32	componentid1;	
	uint32	componentid2;	
	uint32	componentid3;	
} aidmp_t;

#endif 


#define	OOB_BUSCONFIG		0x020
#define	OOB_STATUSA		0x100
#define	OOB_STATUSB		0x104
#define	OOB_STATUSC		0x108
#define	OOB_STATUSD		0x10c
#define	OOB_ENABLEA0		0x200
#define	OOB_ENABLEA1		0x204
#define	OOB_ENABLEA2		0x208
#define	OOB_ENABLEA3		0x20c
#define	OOB_ENABLEB0		0x280
#define	OOB_ENABLEB1		0x284
#define	OOB_ENABLEB2		0x288
#define	OOB_ENABLEB3		0x28c
#define	OOB_ENABLEC0		0x300
#define	OOB_ENABLEC1		0x304
#define	OOB_ENABLEC2		0x308
#define	OOB_ENABLEC3		0x30c
#define	OOB_ENABLED0		0x380
#define	OOB_ENABLED1		0x384
#define	OOB_ENABLED2		0x388
#define	OOB_ENABLED3		0x38c
#define	OOB_ITCR		0xf00
#define	OOB_ITIPOOBA		0xf10
#define	OOB_ITIPOOBB		0xf14
#define	OOB_ITIPOOBC		0xf18
#define	OOB_ITIPOOBD		0xf1c
#define	OOB_ITOPOOBA		0xf30
#define	OOB_ITOPOOBB		0xf34
#define	OOB_ITOPOOBC		0xf38
#define	OOB_ITOPOOBD		0xf3c


#define	AI_OOBSELINA30		0x000
#define	AI_OOBSELINA74		0x004
#define	AI_OOBSELINB30		0x020
#define	AI_OOBSELINB74		0x024
#define	AI_OOBSELINC30		0x040
#define	AI_OOBSELINC74		0x044
#define	AI_OOBSELIND30		0x060
#define	AI_OOBSELIND74		0x064
#define	AI_OOBSELOUTA30		0x100
#define	AI_OOBSELOUTA74		0x104
#define	AI_OOBSELOUTB30		0x120
#define	AI_OOBSELOUTB74		0x124
#define	AI_OOBSELOUTC30		0x140
#define	AI_OOBSELOUTC74		0x144
#define	AI_OOBSELOUTD30		0x160
#define	AI_OOBSELOUTD74		0x164
#define	AI_OOBSYNCA		0x200
#define	AI_OOBSELOUTAEN		0x204
#define	AI_OOBSYNCB		0x220
#define	AI_OOBSELOUTBEN		0x224
#define	AI_OOBSYNCC		0x240
#define	AI_OOBSELOUTCEN		0x244
#define	AI_OOBSYNCD		0x260
#define	AI_OOBSELOUTDEN		0x264
#define	AI_OOBAEXTWIDTH		0x300
#define	AI_OOBAINWIDTH		0x304
#define	AI_OOBAOUTWIDTH		0x308
#define	AI_OOBBEXTWIDTH		0x320
#define	AI_OOBBINWIDTH		0x324
#define	AI_OOBBOUTWIDTH		0x328
#define	AI_OOBCEXTWIDTH		0x340
#define	AI_OOBCINWIDTH		0x344
#define	AI_OOBCOUTWIDTH		0x348
#define	AI_OOBDEXTWIDTH		0x360
#define	AI_OOBDINWIDTH		0x364
#define	AI_OOBDOUTWIDTH		0x368


#define	AI_IOCTRLSET		0x400
#define	AI_IOCTRLCLEAR		0x404
#define	AI_IOCTRL		0x408
#define	AI_IOSTATUS		0x500
#define	AI_RESETCTRL		0x800
#define	AI_RESETSTATUS		0x804


#define	AI_IOCTRLWIDTH		0x700
#define	AI_IOSTATUSWIDTH	0x704

#define	AI_RESETREADID		0x808
#define	AI_RESETWRITEID		0x80c
#define	AI_ERRLOGCTRL		0xa00
#define	AI_ERRLOGDONE		0xa04
#define	AI_ERRLOGSTATUS		0xa08
#define	AI_ERRLOGADDRLO		0xa0c
#define	AI_ERRLOGADDRHI		0xa10
#define	AI_ERRLOGID		0xa14
#define	AI_ERRLOGUSER		0xa18
#define	AI_ERRLOGFLAGS		0xa1c
#define	AI_INTSTATUS		0xa00
#define	AI_CONFIG		0xe00
#define	AI_ITCR			0xf00
#define	AI_ITIPOOBA		0xf10
#define	AI_ITIPOOBB		0xf14
#define	AI_ITIPOOBC		0xf18
#define	AI_ITIPOOBD		0xf1c
#define	AI_ITIPOOBAOUT		0xf30
#define	AI_ITIPOOBBOUT		0xf34
#define	AI_ITIPOOBCOUT		0xf38
#define	AI_ITIPOOBDOUT		0xf3c
#define	AI_ITOPOOBA		0xf50
#define	AI_ITOPOOBB		0xf54
#define	AI_ITOPOOBC		0xf58
#define	AI_ITOPOOBD		0xf5c
#define	AI_ITOPOOBAIN		0xf70
#define	AI_ITOPOOBBIN		0xf74
#define	AI_ITOPOOBCIN		0xf78
#define	AI_ITOPOOBDIN		0xf7c
#define	AI_ITOPRESET		0xf90
#define	AI_PERIPHERIALID4	0xfd0
#define	AI_PERIPHERIALID5	0xfd4
#define	AI_PERIPHERIALID6	0xfd8
#define	AI_PERIPHERIALID7	0xfdc
#define	AI_PERIPHERIALID0	0xfe0
#define	AI_PERIPHERIALID1	0xfe4
#define	AI_PERIPHERIALID2	0xfe8
#define	AI_PERIPHERIALID3	0xfec
#define	AI_COMPONENTID0		0xff0
#define	AI_COMPONENTID1		0xff4
#define	AI_COMPONENTID2		0xff8
#define	AI_COMPONENTID3		0xffc


#define	AIRC_RESET		1


#define	AICFG_OOB		0x00000020
#define	AICFG_IOS		0x00000010
#define	AICFG_IOC		0x00000008
#define	AICFG_TO		0x00000004
#define	AICFG_ERRL		0x00000002
#define	AICFG_RST		0x00000001


#define OOB_SEL_OUTEN_B_5	15
#define OOB_SEL_OUTEN_B_6	23

#endif	
