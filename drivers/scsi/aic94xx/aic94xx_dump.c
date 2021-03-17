// SPDX-License-Identifier: GPL-2.0-only
/*
 * Aic94xx SAS/SATA driver dump interface.
 *
 * Copyright (C) 2004 Adaptec, Inc.  All rights reserved.
 * Copyright (C) 2004 David Chaw <david_chaw@adaptec.com>
 * Copyright (C) 2005 Luben Tuikov <luben_tuikov@adaptec.com>
 *
 * 2005/07/14/LT  Complete overhaul of this file.  Update pages, register
 * locations, names, etc.  Make use of macros.  Print more information.
 * Print all cseq and lseq mip and mdp.
 */

#include <linux/pci.h>
#include "aic94xx.h"
#include "aic94xx_reg.h"
#include "aic94xx_reg_def.h"
#include "aic94xx_sas.h"

#include "aic94xx_dump.h"

#ifdef ASD_DEBUG

#define MD(x)	    (1 << (x))
#define MODE_COMMON (1 << 31)
#define MODE_0_7    (0xFF)

static const struct lseq_cio_regs {
	char	*name;
	u32	offs;
	u8	width;
	u32	mode;
} LSEQmCIOREGS[] = {
	{"LmMnSCBPTR",    0x20, 16, MD(0)|MD(1)|MD(2)|MD(3)|MD(4) },
	{"LmMnDDBPTR",    0x22, 16, MD(0)|MD(1)|MD(2)|MD(3)|MD(4) },
	{"LmREQMBX",      0x30, 32, MODE_COMMON },
	{"LmRSPMBX",      0x34, 32, MODE_COMMON },
	{"LmMnINT",       0x38, 32, MODE_0_7 },
	{"LmMnINTEN",     0x3C, 32, MODE_0_7 },
	{"LmXMTPRIMD",    0x40, 32, MODE_COMMON },
	{"LmXMTPRIMCS",   0x44,  8, MODE_COMMON },
	{"LmCONSTAT",     0x45,  8, MODE_COMMON },
	{"LmMnDMAERRS",   0x46,  8, MD(0)|MD(1) },
	{"LmMnSGDMAERRS", 0x47,  8, MD(0)|MD(1) },
	{"LmMnEXPHDRP",   0x48,  8, MD(0) },
	{"LmMnSASAALIGN", 0x48,  8, MD(1) },
	{"LmMnMSKHDRP",   0x49,  8, MD(0) },
	{"LmMnSTPALIGN",  0x49,  8, MD(1) },
	{"LmMnRCVHDRP",   0x4A,  8, MD(0) },
	{"LmMnXMTHDRP",   0x4A,  8, MD(1) },
	{"LmALIGNMODE",   0x4B,  8, MD(1) },
	{"LmMnEXPRCVCNT", 0x4C, 32, MD(0) },
	{"LmMnXMTCNT",    0x4C, 32, MD(1) },
	{"LmMnCURRTAG",   0x54, 16, MD(0) },
	{"LmMnPREVTAG",   0x56, 16, MD(0) },
	{"LmMnACKOFS",    0x58,  8, MD(1) },
	{"LmMnXFRLVL",    0x59,  8, MD(0)|MD(1) },
	{"LmMnSGDMACTL",  0x5A,  8, MD(0)|MD(1) },
	{"LmMnSGDMASTAT", 0x5B,  8, MD(0)|MD(1) },
	{"LmMnDDMACTL",   0x5C,  8, MD(0)|MD(1) },
	{"LmMnDDMASTAT",  0x5D,  8, MD(0)|MD(1) },
	{"LmMnDDMAMODE",  0x5E, 16, MD(0)|MD(1) },
	{"LmMnPIPECTL",   0x61,  8, MD(0)|MD(1) },
	{"LmMnACTSCB",    0x62, 16, MD(0)|MD(1) },
	{"LmMnSGBHADR",   0x64,  8, MD(0)|MD(1) },
	{"LmMnSGBADR",    0x65,  8, MD(0)|MD(1) },
	{"LmMnSGDCNT",    0x66,  8, MD(0)|MD(1) },
	{"LmMnSGDMADR",   0x68, 32, MD(0)|MD(1) },
	{"LmMnSGDMADR",   0x6C, 32, MD(0)|MD(1) },
	{"LmMnXFRCNT",    0x70, 32, MD(0)|MD(1) },
	{"LmMnXMTCRC",    0x74, 32, MD(1) },
	{"LmCURRTAG",     0x74, 16, MD(0) },
	{"LmPREVTAG",     0x76, 16, MD(0) },
	{"LmMnDPSEL",     0x7B,  8, MD(0)|MD(1) },
	{"LmDPTHSTAT",    0x7C,  8, MODE_COMMON },
	{"LmMnHOLDLVL",   0x7D,  8, MD(0) },
	{"LmMnSATAFS",    0x7E,  8, MD(1) },
	{"LmMnCMPLTSTAT", 0x7F,  8, MD(0)|MD(1) },
	{"LmPRMSTAT0",    0x80, 32, MODE_COMMON },
	{"LmPRMSTAT1",    0x84, 32, MODE_COMMON },
	{"LmGPRMINT",     0x88,  8, MODE_COMMON },
        {"LmMnCURRSCB",   0x8A, 16, MD(0) },
	{"LmPRMICODE",    0x8C, 32, MODE_COMMON },
	{"LmMnRCVCNT",    0x90, 16, MD(0) },
	{"LmMnBUFSTAT",   0x92, 16, MD(0) },
	{"LmMnXMTHDRSIZE",0x92,  8, MD(1) },
	{"LmMnXMTSIZE",   0x93,  8, MD(1) },
	{"LmMnTGTXFRCNT", 0x94, 32, MD(0) },
	{"LmMnEXPROFS",   0x98, 32, MD(0) },
	{"LmMnXMTROFS",   0x98, 32, MD(1) },
	{"LmMnRCVROFS",   0x9C, 32, MD(0) },
	{"LmCONCTL",      0xA0, 16, MODE_COMMON },
	{"LmBITLTIMER",   0xA2, 16, MODE_COMMON },
	{"LmWWNLOW",      0xA8, 32, MODE_COMMON },
	{"LmWWNHIGH",     0xAC, 32, MODE_COMMON },
	{"LmMnFRMERR",    0xB0, 32, MD(0) },
	{"LmMnFRMERREN",  0xB4, 32, MD(0) },
	{"LmAWTIMER",     0xB8, 16, MODE_COMMON },
	{"LmAWTCTL",      0xBA,  8, MODE_COMMON },
	{"LmMnHDRCMPS",   0xC0, 32, MD(0) },
	{"LmMnXMTSTAT",   0xC4,  8, MD(1) },
	{"LmHWTSTATEN",   0xC5,  8, MODE_COMMON },
	{"LmMnRRDYRC",    0xC6,  8, MD(0) },
        {"LmMnRRDYTC",    0xC6,  8, MD(1) },
	{"LmHWTSTAT",     0xC7,  8, MODE_COMMON },
	{"LmMnDATABUFADR",0xC8, 16, MD(0)|MD(1) },
	{"LmDWSSTATUS",   0xCB,  8, MODE_COMMON },
	{"LmMnACTSTAT",   0xCE, 16, MD(0)|MD(1) },
	{"LmMnREQSCB",    0xD2, 16, MD(0)|MD(1) },
	{"LmXXXPRIM",     0xD4, 32, MODE_COMMON },
	{"LmRCVASTAT",    0xD9,  8, MODE_COMMON },
	{"LmINTDIS1",     0xDA,  8, MODE_COMMON },
	{"LmPSTORESEL",   0xDB,  8, MODE_COMMON },
	{"LmPSTORE",      0xDC, 32, MODE_COMMON },
	{"LmPRIMSTAT0EN", 0xE0, 32, MODE_COMMON },
	{"LmPRIMSTAT1EN", 0xE4, 32, MODE_COMMON },
	{"LmDONETCTL",    0xF2, 16, MODE_COMMON },
	{NULL, 0, 0, 0 }
};
/*
static struct lseq_cio_regs LSEQmOOBREGS[] = {
   {"OOB_BFLTR"        ,0x100, 8, MD(5)},
   {"OOB_INIT_MIN"     ,0x102,16, MD(5)},
   {"OOB_INIT_MAX"     ,0x104,16, MD(5)},
   {"OOB_INIT_NEG"     ,0x106,16, MD(5)},
   {"OOB_SAS_MIN"      ,0x108,16, MD(5)},
   {"OOB_SAS_MAX"      ,0x10A,16, MD(5)},
   {"OOB_SAS_NEG"      ,0x10C,16, MD(5)},
   {"OOB_WAKE_MIN"     ,0x10E,16, MD(5)},
   {"OOB_WAKE_MAX"     ,0x110,16, MD(5)},
   {"OOB_WAKE_NEG"     ,0x112,16, MD(5)},
   {"OOB_IDLE_MAX"     ,0x114,16, MD(5)},
   {"OOB_BURST_MAX"    ,0x116,16, MD(5)},
   {"OOB_XMIT_BURST"   ,0x118, 8, MD(5)},
   {"OOB_SEND_PAIRS"   ,0x119, 8, MD(5)},
   {"OOB_INIT_IDLE"    ,0x11A, 8, MD(5)},
   {"OOB_INIT_NEGO"    ,0x11C, 8, MD(5)},
   {"OOB_SAS_IDLE"     ,0x11E, 8, MD(5)},
   {"OOB_SAS_NEGO"     ,0x120, 8, MD(5)},
   {"OOB_WAKE_IDLE"    ,0x122, 8, MD(5)},
   {"OOB_WAKE_NEGO"    ,0x124, 8, MD(5)},
   {"OOB_DATA_KBITS"   ,0x126, 8, MD(5)},
   {"OOB_BURST_DATA"   ,0x128,32, MD(5)},
   {"OOB_ALIGN_0_DATA" ,0x12C,32, MD(5)},
   {"OOB_ALIGN_1_DATA" ,0x130,32, MD(5)},
   {"OOB_SYNC_DATA"    ,0x134,32, MD(5)},
   {"OOB_D10_2_DATA"   ,0x138,32, MD(5)},
   {"OOB_PHY_RST_CNT"  ,0x13C,32, MD(5)},
   {"OOB_SIG_GEN"      ,0x140, 8, MD(5)},
   {"OOB_XMIT"         ,0x141, 8, MD(5)},
   {"FUNCTION_MAKS"    ,0x142, 8, MD(5)},
   {"OOB_MODE"         ,0x143, 8, MD(5)},
   {"CURRENT_STATUS"   ,0x144, 8, MD(5)},
   {"SPEED_MASK"       ,0x145, 8, MD(5)},
   {"PRIM_COUNT"       ,0x146, 8, MD(5)},
   {"OOB_SIGNALS"      ,0x148, 8, MD(5)},
   {"OOB_DATA_DET"     ,0x149, 8, MD(5)},
   {"OOB_TIME_OUT"     ,0x14C, 8, MD(5)},
   {"OOB_TIMER_ENABLE" ,0x14D, 8, MD(5)},
   {"OOB_STATUS"       ,0x14E, 8, MD(5)},
   {"HOT_PLUG_DELAY"   ,0x150, 8, MD(5)},
   {"RCD_DELAY"        ,0x151, 8, MD(5)},
   {"COMSAS_TIMER"     ,0x152, 8, MD(5)},
   {"SNTT_DELAY"       ,0x153, 8, MD(5)},
   {"SPD_CHNG_DELAY"   ,0x154, 8, MD(5)},
   {"SNLT_DELAY"       ,0x155, 8, MD(5)},
   {"SNWT_DELAY"       ,0x156, 8, MD(5)},
   {"ALIGN_DELAY"      ,0x157, 8, MD(5)},
   {"INT_ENABLE_0"     ,0x158, 8, MD(5)},
   {"INT_ENABLE_1"     ,0x159, 8, MD(5)},
   {"INT_ENABLE_2"     ,0x15A, 8, MD(5)},
   {"INT_ENABLE_3"     ,0x15B, 8, MD(5)},
   {"OOB_TEST_REG"     ,0x15C, 8, MD(5)},
   {"PHY_CONTROL_0"    ,0x160, 8, MD(5)},
   {"PHY_CONTROL_1"    ,0x161, 8, MD(5)},
   {"PHY_CONTROL_2"    ,0x162, 8, MD(5)},
   {"PHY_CONTROL_3"    ,0x163, 8, MD(5)},
   {"PHY_OOB_CAL_TX"   ,0x164, 8, MD(5)},
   {"PHY_OOB_CAL_RX"   ,0x165, 8, MD(5)},
   {"OOB_PHY_CAL_TX"   ,0x166, 8, MD(5)},
   {"OOB_PHY_CAL_RX"   ,0x167, 8, MD(5)},
   {"PHY_CONTROL_4"    ,0x168, 8, MD(5)},
   {"PHY_TEST"         ,0x169, 8, MD(5)},
   {"PHY_PWR_CTL"      ,0x16A, 8, MD(5)},
   {"PHY_PWR_DELAY"    ,0x16B, 8, MD(5)},
   {"OOB_SM_CON"       ,0x16C, 8, MD(5)},
   {"ADDR_TRAP_1"      ,0x16D, 8, MD(5)},
   {"ADDR_NEXT_1"      ,0x16E, 8, MD(5)},
   {"NEXT_ST_1"        ,0x16F, 8, MD(5)},
   {"OOB_SM_STATE"     ,0x170, 8, MD(5)},
   {"ADDR_TRAP_2"      ,0x171, 8, MD(5)},
   {"ADDR_NEXT_2"      ,0x172, 8, MD(5)},
   {"NEXT_ST_2"        ,0x173, 8, MD(5)},
   {NULL, 0, 0, 0 }
};
*/
#define STR_8BIT   "   %30s[0x%04x]:0x%02x\n"
#define STR_16BIT  "   %30s[0x%04x]:0x%04x\n"
#define STR_32BIT  "   %30s[0x%04x]:0x%08x\n"
#define STR_64BIT  "   %30s[0x%04x]:0x%llx\n"

#define PRINT_REG_8bit(_ha, _n, _r) asd_printk(STR_8BIT, #_n, _n,      \
					     asd_read_reg_byte(_ha, _r))
#define PRINT_REG_16bit(_ha, _n, _r) asd_printk(STR_16BIT, #_n, _n,     \
					      asd_read_reg_word(_ha, _r))
#define PRINT_REG_32bit(_ha, _n, _r) asd_printk(STR_32BIT, #_n, _n,      \
					      asd_read_reg_dword(_ha, _r))

#define PRINT_CREG_8bit(_ha, _n) asd_printk(STR_8BIT, #_n, _n,      \
					     asd_read_reg_byte(_ha, C##_n))
#define PRINT_CREG_16bit(_ha, _n) asd_printk(STR_16BIT, #_n, _n,     \
					      asd_read_reg_word(_ha, C##_n))
#define PRINT_CREG_32bit(_ha, _n) asd_printk(STR_32BIT, #_n, _n,      \
					      asd_read_reg_dword(_ha, C##_n))

#define MSTR_8BIT   "   Mode:%02d %30s[0x%04x]:0x%02x\n"
#define MSTR_16BIT  "   Mode:%02d %30s[0x%04x]:0x%04x\n"
#define MSTR_32BIT  "   Mode:%02d %30s[0x%04x]:0x%08x\n"

#define PRINT_MREG_8bit(_ha, _m, _n, _r) asd_printk(MSTR_8BIT, _m, #_n, _n,   \
					     asd_read_reg_byte(_ha, _r))
#define PRINT_MREG_16bit(_ha, _m, _n, _r) asd_printk(MSTR_16BIT, _m, #_n, _n, \
					      asd_read_reg_word(_ha, _r))
#define PRINT_MREG_32bit(_ha, _m, _n, _r) asd_printk(MSTR_32BIT, _m, #_n, _n, \
					      asd_read_reg_dword(_ha, _r))

/* can also be used for MD when the register is mode aware already */
#define PRINT_MIS_byte(_ha, _n) asd_printk(STR_8BIT, #_n,CSEQ_##_n-CMAPPEDSCR,\
                                           asd_read_reg_byte(_ha, CSEQ_##_n))
#define PRINT_MIS_word(_ha, _n) asd_printk(STR_16BIT,#_n,CSEQ_##_n-CMAPPEDSCR,\
                                           asd_read_reg_word(_ha, CSEQ_##_n))
#define PRINT_MIS_dword(_ha, _n)                      \
        asd_printk(STR_32BIT,#_n,CSEQ_##_n-CMAPPEDSCR,\
                   asd_read_reg_dword(_ha, CSEQ_##_n))
#define PRINT_MIS_qword(_ha, _n)                                       \
        asd_printk(STR_64BIT, #_n,CSEQ_##_n-CMAPPEDSCR,                \
                   (unsigned long long)(((u64)asd_read_reg_dword(_ha, CSEQ_##_n))     \
                 | (((u64)asd_read_reg_dword(_ha, (CSEQ_##_n)+4))<<32)))

#define CMDP_REG(_n, _m) (_m*(CSEQ_PAGE_SIZE*2)+CSEQ_##_n)
#define PRINT_CMDP_word(_ha, _n) \
asd_printk("%20s 0x%04x 0x%04x 0x%04x 0x%04x 0x%04x 0x%04x 0x%04x 0x%04x\n", \
	#_n, \
	asd_read_reg_word(_ha, CMDP_REG(_n, 0)), \
	asd_read_reg_word(_ha, CMDP_REG(_n, 1)), \
	asd_read_reg_word(_ha, CMDP_REG(_n, 2)), \
	asd_read_reg_word(_ha, CMDP_REG(_n, 3)), \
	asd_read_reg_word(_ha, CMDP_REG(_n, 4)), \
	asd_read_reg_word(_ha, CMDP_REG(_n, 5)), \
	asd_read_reg_word(_ha, CMDP_REG(_n, 6)), \
	asd_read_reg_word(_ha, CMDP_REG(_n, 7)))

#define PRINT_CMDP_byte(_ha, _n) \
asd_printk("%20s 0x%04x 0x%04x 0x%04x 0x%04x 0x%04x 0x%04x 0x%04x 0x%04x\n", \
	#_n, \
	asd_read_reg_byte(_ha, CMDP_REG(_n, 0)), \
	asd_read_reg_byte(_ha, CMDP_REG(_n, 1)), \
	asd_read_reg_byte(_ha, CMDP_REG(_n, 2)), \
	asd_read_reg_byte(_ha, CMDP_REG(_n, 3)), \
	asd_read_reg_byte(_ha, CMDP_REG(_n, 4)), \
	asd_read_reg_byte(_ha, CMDP_REG(_n, 5)), \
	asd_read_reg_byte(_ha, CMDP_REG(_n, 6)), \
	asd_read_reg_byte(_ha, CMDP_REG(_n, 7)))

static void asd_dump_cseq_state(struct asd_ha_struct *asd_ha)
{
	int mode;

	asd_printk("CSEQ STATE\n");

	asd_printk("ARP2 REGISTERS\n");

	PRINT_CREG_32bit(asd_ha, ARP2CTL);
	PRINT_CREG_32bit(asd_ha, ARP2INT);
	PRINT_CREG_32bit(asd_ha, ARP2INTEN);
	PRINT_CREG_8bit(asd_ha, MODEPTR);
	PRINT_CREG_8bit(asd_ha, ALTMODE);
	PRINT_CREG_8bit(asd_ha, FLAG);
	PRINT_CREG_8bit(asd_ha, ARP2INTCTL);
	PRINT_CREG_16bit(asd_ha, STACK);
	PRINT_CREG_16bit(asd_ha, PRGMCNT);
	PRINT_CREG_16bit(asd_ha, ACCUM);
	PRINT_CREG_16bit(asd_ha, SINDEX);
	PRINT_CREG_16bit(asd_ha, DINDEX);
	PRINT_CREG_8bit(asd_ha, SINDIR);
	PRINT_CREG_8bit(asd_ha, DINDIR);
	PRINT_CREG_8bit(asd_ha, JUMLDIR);
	PRINT_CREG_8bit(asd_ha, ARP2HALTCODE);
	PRINT_CREG_16bit(asd_ha, CURRADDR);
	PRINT_CREG_16bit(asd_ha, LASTADDR);
	PRINT_CREG_16bit(asd_ha, NXTLADDR);

	asd_printk("IOP REGISTERS\n");

	PRINT_REG_32bit(asd_ha, BISTCTL1, CBISTCTL);
	PRINT_CREG_32bit(asd_ha, MAPPEDSCR);

	asd_printk("CIO REGISTERS\n");

	for (mode = 0; mode < 9; mode++)
		PRINT_MREG_16bit(asd_ha, mode, MnSCBPTR, CMnSCBPTR(mode));
	PRINT_MREG_16bit(asd_ha, 15, MnSCBPTR, CMnSCBPTR(15));

	for (mode = 0; mode < 9; mode++)
		PRINT_MREG_16bit(asd_ha, mode, MnDDBPTR, CMnDDBPTR(mode));
	PRINT_MREG_16bit(asd_ha, 15, MnDDBPTR, CMnDDBPTR(15));

	for (mode = 0; mode < 8; mode++)
		PRINT_MREG_32bit(asd_ha, mode, MnREQMBX, CMnREQMBX(mode));
	for (mode = 0; mode < 8; mode++)
		PRINT_MREG_32bit(asd_ha, mode, MnRSPMBX, CMnRSPMBX(mode));
	for (mode = 0; mode < 8; mode++)
		PRINT_MREG_32bit(asd_ha, mode, MnINT, CMnINT(mode));
	for (mode = 0; mode < 8; mode++)
		PRINT_MREG_32bit(asd_ha, mode, MnINTEN, CMnINTEN(mode));

	PRINT_CREG_8bit(asd_ha, SCRATCHPAGE);
	for (mode = 0; mode < 8; mode++)
		PRINT_MREG_8bit(asd_ha, mode, MnSCRATCHPAGE,
				CMnSCRATCHPAGE(mode));

	PRINT_REG_32bit(asd_ha, CLINKCON, CLINKCON);
	PRINT_REG_8bit(asd_ha, CCONMSK, CCONMSK);
	PRINT_REG_8bit(asd_ha, CCONEXIST, CCONEXIST);
	PRINT_REG_16bit(asd_ha, CCONMODE, CCONMODE);
	PRINT_REG_32bit(asd_ha, CTIMERCALC, CTIMERCALC);
	PRINT_REG_8bit(asd_ha, CINTDIS, CINTDIS);

	asd_printk("SCRATCH MEMORY\n");

	asd_printk("MIP 4 >>>>>\n");
	PRINT_MIS_word(asd_ha, Q_EXE_HEAD);
	PRINT_MIS_word(asd_ha, Q_EXE_TAIL);
	PRINT_MIS_word(asd_ha, Q_DONE_HEAD);
	PRINT_MIS_word(asd_ha, Q_DONE_TAIL);
	PRINT_MIS_word(asd_ha, Q_SEND_HEAD);
	PRINT_MIS_word(asd_ha, Q_SEND_TAIL);
	PRINT_MIS_word(asd_ha, Q_DMA2CHIM_HEAD);
	PRINT_MIS_word(asd_ha, Q_DMA2CHIM_TAIL);
	PRINT_MIS_word(asd_ha, Q_COPY_HEAD);
	PRINT_MIS_word(asd_ha, Q_COPY_TAIL);
	PRINT_MIS_word(asd_ha, REG0);
	PRINT_MIS_word(asd_ha, REG1);
	PRINT_MIS_dword(asd_ha, REG2);
	PRINT_MIS_byte(asd_ha, LINK_CTL_Q_MAP);
	PRINT_MIS_byte(asd_ha, MAX_CSEQ_MODE);
	PRINT_MIS_byte(asd_ha, FREE_LIST_HACK_COUNT);

	asd_printk("MIP 5 >>>>\n");
	PRINT_MIS_qword(asd_ha, EST_NEXUS_REQ_QUEUE);
	PRINT_MIS_qword(asd_ha, EST_NEXUS_REQ_COUNT);
	PRINT_MIS_word(asd_ha, Q_EST_NEXUS_HEAD);
	PRINT_MIS_word(asd_ha, Q_EST_NEXUS_TAIL);
	PRINT_MIS_word(asd_ha, NEED_EST_NEXUS_SCB);
	PRINT_MIS_byte(asd_ha, EST_NEXUS_REQ_HEAD);
	PRINT_MIS_byte(asd_ha, EST_NEXUS_REQ_TAIL);
	PRINT_MIS_byte(asd_ha, EST_NEXUS_SCB_OFFSET);

	asd_printk("MIP 6 >>>>\n");
	PRINT_MIS_word(asd_ha, INT_ROUT_RET_ADDR0);
	PRINT_MIS_word(asd_ha, INT_ROUT_RET_ADDR1);
	PRINT_MIS_word(asd_ha, INT_ROUT_SCBPTR);
	PRINT_MIS_byte(asd_ha, INT_ROUT_MODE);
	PRINT_MIS_byte(asd_ha, ISR_SCRATCH_FLAGS);
	PRINT_MIS_word(asd_ha, ISR_SAVE_SINDEX);
	PRINT_MIS_word(asd_ha, ISR_SAVE_DINDEX);
	PRINT_MIS_word(asd_ha, Q_MONIRTT_HEAD);
	PRINT_MIS_word(asd_ha, Q_MONIRTT_TAIL);
	PRINT_MIS_byte(asd_ha, FREE_SCB_MASK);
	PRINT_MIS_word(asd_ha, BUILTIN_FREE_SCB_HEAD);
	PRINT_MIS_word(asd_ha, BUILTIN_FREE_SCB_TAIL);
	PRINT_MIS_word(asd_ha, EXTENDED_FREE_SCB_HEAD);
	PRINT_MIS_word(asd_ha, EXTENDED_FREE_SCB_TAIL);

	asd_printk("MIP 7 >>>>\n");
	PRINT_MIS_qword(asd_ha, EMPTY_REQ_QUEUE);
	PRINT_MIS_qword(asd_ha, EMPTY_REQ_COUNT);
	PRINT_MIS_word(asd_ha, Q_EMPTY_HEAD);
	PRINT_MIS_word(asd_ha, Q_EMPTY_TAIL);
	PRINT_MIS_word(asd_ha, NEED_EMPTY_SCB);
	PRINT_MIS_byte(asd_ha, EMPTY_REQ_HEAD);
	PRINT_MIS_byte(asd_ha, EMPTY_REQ_TAIL);
	PRINT_MIS_byte(asd_ha, EMPTY_SCB_OFFSET);
	PRINT_MIS_word(asd_ha, PRIMITIVE_DATA);
	PRINT_MIS_dword(asd_ha, TIMEOUT_CONST);

	asd_printk("MDP 0 >>>>\n");
	asd_printk("%-20s %6s %6s %6s %6s %6s %6s %6s %6s\n",
		   "Mode: ", "0", "1", "2", "3", "4", "5", "6", "7");
	PRINT_CMDP_word(asd_ha, LRM_SAVE_SINDEX);
	PRINT_CMDP_word(asd_ha, LRM_SAVE_SCBPTR);
	PRINT_CMDP_word(asd_ha, Q_LINK_HEAD);
	PRINT_CMDP_word(asd_ha, Q_LINK_TAIL);
	PRINT_CMDP_byte(asd_ha, LRM_SAVE_SCRPAGE);

	asd_printk("MDP 0 Mode 8 >>>>\n");
	PRINT_MIS_word(asd_ha, RET_ADDR);
	PRINT_MIS_word(asd_ha, RET_SCBPTR);
	PRINT_MIS_word(asd_ha, SAVE_SCBPTR);
	PRINT_MIS_word(asd_ha, EMPTY_TRANS_CTX);
	PRINT_MIS_word(asd_ha, RESP_LEN);
	PRINT_MIS_word(asd_ha, TMF_SCBPTR);
	PRINT_MIS_word(asd_ha, GLOBAL_PREV_SCB);
	PRINT_MIS_word(asd_ha, GLOBAL_HEAD);
	PRINT_MIS_word(asd_ha, CLEAR_LU_HEAD);
	PRINT_MIS_byte(asd_ha, TMF_OPCODE);
	PRINT_MIS_byte(asd_ha, SCRATCH_FLAGS);
	PRINT_MIS_word(asd_ha, HSB_SITE);
	PRINT_MIS_word(asd_ha, FIRST_INV_SCB_SITE);
	PRINT_MIS_word(asd_ha, FIRST_INV_DDB_SITE);

	asd_printk("MDP 1 Mode 8 >>>>\n");
	PRINT_MIS_qword(asd_ha, LUN_TO_CLEAR);
	PRINT_MIS_qword(asd_ha, LUN_TO_CHECK);

	asd_printk("MDP 2 Mode 8 >>>>\n");
	PRINT_MIS_qword(asd_ha, HQ_NEW_POINTER);
	PRINT_MIS_qword(asd_ha, HQ_DONE_BASE);
	PRINT_MIS_dword(asd_ha, HQ_DONE_POINTER);
	PRINT_MIS_byte(asd_ha, HQ_DONE_PASS);
}

#define PRINT_LREG_8bit(_h, _lseq, _n) \
        asd_printk(STR_8BIT, #_n, _n, asd_read_reg_byte(_h, Lm##_n(_lseq)))
#define PRINT_LREG_16bit(_h, _lseq, _n) \
        asd_printk(STR_16BIT, #_n, _n, asd_read_reg_word(_h, Lm##_n(_lseq)))
#define PRINT_LREG_32bit(_h, _lseq, _n) \
        asd_printk(STR_32BIT, #_n, _n, asd_read_reg_dword(_h, Lm##_n(_lseq)))

#define PRINT_LMIP_byte(_h, _lseq, _n)                              \
	asd_printk(STR_8BIT, #_n, LmSEQ_##_n(_lseq)-LmSCRATCH(_lseq), \
		   asd_read_reg_byte(_h, LmSEQ_##_n(_lseq)))
#define PRINT_LMIP_word(_h, _lseq, _n)                              \
	asd_printk(STR_16BIT, #_n, LmSEQ_##_n(_lseq)-LmSCRATCH(_lseq), \
		   asd_read_reg_word(_h, LmSEQ_##_n(_lseq)))
#define PRINT_LMIP_dword(_h, _lseq, _n)                             \
	asd_printk(STR_32BIT, #_n, LmSEQ_##_n(_lseq)-LmSCRATCH(_lseq), \
		   asd_read_reg_dword(_h, LmSEQ_##_n(_lseq)))
#define PRINT_LMIP_qword(_h, _lseq, _n)                                \
	asd_printk(STR_64BIT, #_n, LmSEQ_##_n(_lseq)-LmSCRATCH(_lseq), \
		 (unsigned long long)(((unsigned long long) \
		 asd_read_reg_dword(_h, LmSEQ_##_n(_lseq))) \
	          | (((unsigned long long) \
		 asd_read_reg_dword(_h, LmSEQ_##_n(_lseq)+4))<<32)))

static void asd_print_lseq_cio_reg(struct asd_ha_struct *asd_ha,
				   u32 lseq_cio_addr, int i)
{
	switch (LSEQmCIOREGS[i].width) {
	case 8:
		asd_printk("%20s[0x%x]: 0x%02x\n", LSEQmCIOREGS[i].name,
			   LSEQmCIOREGS[i].offs,
			   asd_read_reg_byte(asd_ha, lseq_cio_addr +
					     LSEQmCIOREGS[i].offs));

		break;
	case 16:
		asd_printk("%20s[0x%x]: 0x%04x\n", LSEQmCIOREGS[i].name,
			   LSEQmCIOREGS[i].offs,
			   asd_read_reg_word(asd_ha, lseq_cio_addr +
					     LSEQmCIOREGS[i].offs));

		break;
	case 32:
		asd_printk("%20s[0x%x]: 0x%08x\n", LSEQmCIOREGS[i].name,
			   LSEQmCIOREGS[i].offs,
			   asd_read_reg_dword(asd_ha, lseq_cio_addr +
					      LSEQmCIOREGS[i].offs));
		break;
	}
}

static void asd_dump_lseq_state(struct asd_ha_struct *asd_ha, int lseq)
{
	u32 moffs;
	int mode;

	asd_printk("LSEQ %d STATE\n", lseq);

	asd_printk("LSEQ%d: ARP2 REGISTERS\n", lseq);
	PRINT_LREG_32bit(asd_ha, lseq, ARP2CTL);
	PRINT_LREG_32bit(asd_ha, lseq, ARP2INT);
	PRINT_LREG_32bit(asd_ha, lseq, ARP2INTEN);
	PRINT_LREG_8bit(asd_ha, lseq, MODEPTR);
	PRINT_LREG_8bit(asd_ha, lseq, ALTMODE);
	PRINT_LREG_8bit(asd_ha, lseq, FLAG);
	PRINT_LREG_8bit(asd_ha, lseq, ARP2INTCTL);
	PRINT_LREG_16bit(asd_ha, lseq, STACK);
	PRINT_LREG_16bit(asd_ha, lseq, PRGMCNT);
	PRINT_LREG_16bit(asd_ha, lseq, ACCUM);
	PRINT_LREG_16bit(asd_ha, lseq, SINDEX);
	PRINT_LREG_16bit(asd_ha, lseq, DINDEX);
	PRINT_LREG_8bit(asd_ha, lseq, SINDIR);
	PRINT_LREG_8bit(asd_ha, lseq, DINDIR);
	PRINT_LREG_8bit(asd_ha, lseq, JUMLDIR);
	PRINT_LREG_8bit(asd_ha, lseq, ARP2HALTCODE);
	PRINT_LREG_16bit(asd_ha, lseq, CURRADDR);
	PRINT_LREG_16bit(asd_ha, lseq, LASTADDR);
	PRINT_LREG_16bit(asd_ha, lseq, NXTLADDR);

	asd_printk("LSEQ%d: IOP REGISTERS\n", lseq);

	PRINT_LREG_32bit(asd_ha, lseq, MODECTL);
	PRINT_LREG_32bit(asd_ha, lseq, DBGMODE);
	PRINT_LREG_32bit(asd_ha, lseq, CONTROL);
	PRINT_REG_32bit(asd_ha, BISTCTL0, LmBISTCTL0(lseq));
	PRINT_REG_32bit(asd_ha, BISTCTL1, LmBISTCTL1(lseq));

	asd_printk("LSEQ%d: CIO REGISTERS\n", lseq);
	asd_printk("Mode common:\n");

	for (mode = 0; mode < 8; mode++) {
		u32 lseq_cio_addr = LmSEQ_PHY_BASE(mode, lseq);
		int i;

		for (i = 0; LSEQmCIOREGS[i].name; i++)
			if (LSEQmCIOREGS[i].mode == MODE_COMMON)
				asd_print_lseq_cio_reg(asd_ha,lseq_cio_addr,i);
	}

	asd_printk("Mode unique:\n");
	for (mode = 0; mode < 8; mode++) {
		u32 lseq_cio_addr = LmSEQ_PHY_BASE(mode, lseq);
		int i;

		asd_printk("Mode %d\n", mode);
		for  (i = 0; LSEQmCIOREGS[i].name; i++) {
			if (!(LSEQmCIOREGS[i].mode & (1 << mode)))
				continue;
			asd_print_lseq_cio_reg(asd_ha, lseq_cio_addr, i);
		}
	}

	asd_printk("SCRATCH MEMORY\n");

	asd_printk("LSEQ%d MIP 0 >>>>\n", lseq);
	PRINT_LMIP_word(asd_ha, lseq, Q_TGTXFR_HEAD);
	PRINT_LMIP_word(asd_ha, lseq, Q_TGTXFR_TAIL);
	PRINT_LMIP_byte(asd_ha, lseq, LINK_NUMBER);
	PRINT_LMIP_byte(asd_ha, lseq, SCRATCH_FLAGS);
	PRINT_LMIP_dword(asd_ha, lseq, CONNECTION_STATE);
	PRINT_LMIP_word(asd_ha, lseq, CONCTL);
	PRINT_LMIP_byte(asd_ha, lseq, CONSTAT);
	PRINT_LMIP_byte(asd_ha, lseq, CONNECTION_MODES);
	PRINT_LMIP_word(asd_ha, lseq, REG1_ISR);
	PRINT_LMIP_word(asd_ha, lseq, REG2_ISR);
	PRINT_LMIP_word(asd_ha, lseq, REG3_ISR);
	PRINT_LMIP_qword(asd_ha, lseq,REG0_ISR);

	asd_printk("LSEQ%d MIP 1 >>>>\n", lseq);
	PRINT_LMIP_word(asd_ha, lseq, EST_NEXUS_SCBPTR0);
	PRINT_LMIP_word(asd_ha, lseq, EST_NEXUS_SCBPTR1);
	PRINT_LMIP_word(asd_ha, lseq, EST_NEXUS_SCBPTR2);
	PRINT_LMIP_word(asd_ha, lseq, EST_NEXUS_SCBPTR3);
	PRINT_LMIP_byte(asd_ha, lseq, EST_NEXUS_SCB_OPCODE0);
	PRINT_LMIP_byte(asd_ha, lseq, EST_NEXUS_SCB_OPCODE1);
	PRINT_LMIP_byte(asd_ha, lseq, EST_NEXUS_SCB_OPCODE2);
	PRINT_LMIP_byte(asd_ha, lseq, EST_NEXUS_SCB_OPCODE3);
	PRINT_LMIP_byte(asd_ha, lseq, EST_NEXUS_SCB_HEAD);
	PRINT_LMIP_byte(asd_ha, lseq, EST_NEXUS_SCB_TAIL);
	PRINT_LMIP_byte(asd_ha, lseq, EST_NEXUS_BUF_AVAIL);
	PRINT_LMIP_dword(asd_ha, lseq, TIMEOUT_CONST);
	PRINT_LMIP_word(asd_ha, lseq, ISR_SAVE_SINDEX);
	PRINT_LMIP_word(asd_ha, lseq, ISR_SAVE_DINDEX);

	asd_printk("LSEQ%d MIP 2 >>>>\n", lseq);
	PRINT_LMIP_word(asd_ha, lseq, EMPTY_SCB_PTR0);
	PRINT_LMIP_word(asd_ha, lseq, EMPTY_SCB_PTR1);
	PRINT_LMIP_word(asd_ha, lseq, EMPTY_SCB_PTR2);
	PRINT_LMIP_word(asd_ha, lseq, EMPTY_SCB_PTR3);
	PRINT_LMIP_byte(asd_ha, lseq, EMPTY_SCB_OPCD0);
	PRINT_LMIP_byte(asd_ha, lseq, EMPTY_SCB_OPCD1);
	PRINT_LMIP_byte(asd_ha, lseq, EMPTY_SCB_OPCD2);
	PRINT_LMIP_byte(asd_ha, lseq, EMPTY_SCB_OPCD3);
	PRINT_LMIP_byte(asd_ha, lseq, EMPTY_SCB_HEAD);
	PRINT_LMIP_byte(asd_ha, lseq, EMPTY_SCB_TAIL);
	PRINT_LMIP_byte(asd_ha, lseq, EMPTY_BUFS_AVAIL);

	asd_printk("LSEQ%d MIP 3 >>>>\n", lseq);
	PRINT_LMIP_dword(asd_ha, lseq, DEV_PRES_TMR_TOUT_CONST);
	PRINT_LMIP_dword(asd_ha, lseq, SATA_INTERLOCK_TIMEOUT);
	PRINT_LMIP_dword(asd_ha, lseq, SRST_ASSERT_TIMEOUT);
	PRINT_LMIP_dword(asd_ha, lseq, RCV_FIS_TIMEOUT);
	PRINT_LMIP_dword(asd_ha, lseq, ONE_MILLISEC_TIMEOUT);
	PRINT_LMIP_dword(asd_ha, lseq, TEN_MS_COMINIT_TIMEOUT);
	PRINT_LMIP_dword(asd_ha, lseq, SMP_RCV_TIMEOUT);

	for (mode = 0; mode < 3; mode++) {
		asd_printk("LSEQ%d MDP 0 MODE %d >>>>\n", lseq, mode);
		moffs = mode * LSEQ_MODE_SCRATCH_SIZE;

		asd_printk(STR_16BIT, "RET_ADDR", 0,
			   asd_read_reg_word(asd_ha, LmSEQ_RET_ADDR(lseq)
					     + moffs));
		asd_printk(STR_16BIT, "REG0_MODE", 2,
			   asd_read_reg_word(asd_ha, LmSEQ_REG0_MODE(lseq)
					     + moffs));
		asd_printk(STR_16BIT, "MODE_FLAGS", 4,
			   asd_read_reg_word(asd_ha, LmSEQ_MODE_FLAGS(lseq)
					     + moffs));
		asd_printk(STR_16BIT, "RET_ADDR2", 0x6,
			   asd_read_reg_word(asd_ha, LmSEQ_RET_ADDR2(lseq)
					     + moffs));
		asd_printk(STR_16BIT, "RET_ADDR1", 0x8,
			   asd_read_reg_word(asd_ha, LmSEQ_RET_ADDR1(lseq)
					     + moffs));
		asd_printk(STR_8BIT, "OPCODE_TO_CSEQ", 0xB,
			   asd_read_reg_byte(asd_ha, LmSEQ_OPCODE_TO_CSEQ(lseq)
					     + moffs));
		asd_printk(STR_16BIT, "DATA_TO_CSEQ", 0xC,
			   asd_read_reg_word(asd_ha, LmSEQ_DATA_TO_CSEQ(lseq)
					     + moffs));
	}

	asd_printk("LSEQ%d MDP 0 MODE 5 >>>>\n", lseq);
	moffs = LSEQ_MODE5_PAGE0_OFFSET;
	asd_printk(STR_16BIT, "RET_ADDR", 0,
		   asd_read_reg_word(asd_ha, LmSEQ_RET_ADDR(lseq) + moffs));
	asd_printk(STR_16BIT, "REG0_MODE", 2,
		   asd_read_reg_word(asd_ha, LmSEQ_REG0_MODE(lseq) + moffs));
	asd_printk(STR_16BIT, "MODE_FLAGS", 4,
		   asd_read_reg_word(asd_ha, LmSEQ_MODE_FLAGS(lseq) + moffs));
	asd_printk(STR_16BIT, "RET_ADDR2", 0x6,
		   asd_read_reg_word(asd_ha, LmSEQ_RET_ADDR2(lseq) + moffs));
	asd_printk(STR_16BIT, "RET_ADDR1", 0x8,
		   asd_read_reg_word(asd_ha, LmSEQ_RET_ADDR1(lseq) + moffs));
	asd_printk(STR_8BIT, "OPCODE_TO_CSEQ", 0xB,
	   asd_read_reg_byte(asd_ha, LmSEQ_OPCODE_TO_CSEQ(lseq) + moffs));
	asd_printk(STR_16BIT, "DATA_TO_CSEQ", 0xC,
	   asd_read_reg_word(asd_ha, LmSEQ_DATA_TO_CSEQ(lseq) + moffs));

	asd_printk("LSEQ%d MDP 0 MODE 0 >>>>\n", lseq);
	PRINT_LMIP_word(asd_ha, lseq, FIRST_INV_DDB_SITE);
	PRINT_LMIP_word(asd_ha, lseq, EMPTY_TRANS_CTX);
	PRINT_LMIP_word(asd_ha, lseq, RESP_LEN);
	PRINT_LMIP_word(asd_ha, lseq, FIRST_INV_SCB_SITE);
	PRINT_LMIP_dword(asd_ha, lseq, INTEN_SAVE);
	PRINT_LMIP_byte(asd_ha, lseq, LINK_RST_FRM_LEN);
	PRINT_LMIP_byte(asd_ha, lseq, LINK_RST_PROTOCOL);
	PRINT_LMIP_byte(asd_ha, lseq, RESP_STATUS);
	PRINT_LMIP_byte(asd_ha, lseq, LAST_LOADED_SGE);
	PRINT_LMIP_byte(asd_ha, lseq, SAVE_SCBPTR);

	asd_printk("LSEQ%d MDP 0 MODE 1 >>>>\n", lseq);
	PRINT_LMIP_word(asd_ha, lseq, Q_XMIT_HEAD);
	PRINT_LMIP_word(asd_ha, lseq, M1_EMPTY_TRANS_CTX);
	PRINT_LMIP_word(asd_ha, lseq, INI_CONN_TAG);
	PRINT_LMIP_byte(asd_ha, lseq, FAILED_OPEN_STATUS);
	PRINT_LMIP_byte(asd_ha, lseq, XMIT_REQUEST_TYPE);
	PRINT_LMIP_byte(asd_ha, lseq, M1_RESP_STATUS);
	PRINT_LMIP_byte(asd_ha, lseq, M1_LAST_LOADED_SGE);
	PRINT_LMIP_word(asd_ha, lseq, M1_SAVE_SCBPTR);

	asd_printk("LSEQ%d MDP 0 MODE 2 >>>>\n", lseq);
	PRINT_LMIP_word(asd_ha, lseq, PORT_COUNTER);
	PRINT_LMIP_word(asd_ha, lseq, PM_TABLE_PTR);
	PRINT_LMIP_word(asd_ha, lseq, SATA_INTERLOCK_TMR_SAVE);
	PRINT_LMIP_word(asd_ha, lseq, IP_BITL);
	PRINT_LMIP_word(asd_ha, lseq, COPY_SMP_CONN_TAG);
	PRINT_LMIP_byte(asd_ha, lseq, P0M2_OFFS1AH);

	asd_printk("LSEQ%d MDP 0 MODE 4/5 >>>>\n", lseq);
	PRINT_LMIP_byte(asd_ha, lseq, SAVED_OOB_STATUS);
	PRINT_LMIP_byte(asd_ha, lseq, SAVED_OOB_MODE);
	PRINT_LMIP_word(asd_ha, lseq, Q_LINK_HEAD);
	PRINT_LMIP_byte(asd_ha, lseq, LINK_RST_ERR);
	PRINT_LMIP_byte(asd_ha, lseq, SAVED_OOB_SIGNALS);
	PRINT_LMIP_byte(asd_ha, lseq, SAS_RESET_MODE);
	PRINT_LMIP_byte(asd_ha, lseq, LINK_RESET_RETRY_COUNT);
	PRINT_LMIP_byte(asd_ha, lseq, NUM_LINK_RESET_RETRIES);
	PRINT_LMIP_word(asd_ha, lseq, OOB_INT_ENABLES);
	PRINT_LMIP_word(asd_ha, lseq, NOTIFY_TIMER_TIMEOUT);
	PRINT_LMIP_word(asd_ha, lseq, NOTIFY_TIMER_DOWN_COUNT);

	asd_printk("LSEQ%d MDP 1 MODE 0 >>>>\n", lseq);
	PRINT_LMIP_qword(asd_ha, lseq, SG_LIST_PTR_ADDR0);
	PRINT_LMIP_qword(asd_ha, lseq, SG_LIST_PTR_ADDR1);

	asd_printk("LSEQ%d MDP 1 MODE 1 >>>>\n", lseq);
	PRINT_LMIP_qword(asd_ha, lseq, M1_SG_LIST_PTR_ADDR0);
	PRINT_LMIP_qword(asd_ha, lseq, M1_SG_LIST_PTR_ADDR1);

	asd_printk("LSEQ%d MDP 1 MODE 2 >>>>\n", lseq);
	PRINT_LMIP_dword(asd_ha, lseq, INVALID_DWORD_COUNT);
	PRINT_LMIP_dword(asd_ha, lseq, DISPARITY_ERROR_COUNT);
	PRINT_LMIP_dword(asd_ha, lseq, LOSS_OF_SYNC_COUNT);

	asd_printk("LSEQ%d MDP 1 MODE 4/5 >>>>\n", lseq);
	PRINT_LMIP_dword(asd_ha, lseq, FRAME_TYPE_MASK);
	PRINT_LMIP_dword(asd_ha, lseq, HASHED_SRC_ADDR_MASK_PRINT);
	PRINT_LMIP_byte(asd_ha, lseq, NUM_FILL_BYTES_MASK);
	PRINT_LMIP_word(asd_ha, lseq, TAG_MASK);
	PRINT_LMIP_word(asd_ha, lseq, TARGET_PORT_XFER_TAG);
	PRINT_LMIP_dword(asd_ha, lseq, DATA_OFFSET);

	asd_printk("LSEQ%d MDP 2 MODE 0 >>>>\n", lseq);
	PRINT_LMIP_dword(asd_ha, lseq, SMP_RCV_TIMER_TERM_TS);
	PRINT_LMIP_byte(asd_ha, lseq, DEVICE_BITS);
	PRINT_LMIP_word(asd_ha, lseq, SDB_DDB);
	PRINT_LMIP_word(asd_ha, lseq, SDB_NUM_TAGS);
	PRINT_LMIP_word(asd_ha, lseq, SDB_CURR_TAG);

	asd_printk("LSEQ%d MDP 2 MODE 1 >>>>\n", lseq);
	PRINT_LMIP_qword(asd_ha, lseq, TX_ID_ADDR_FRAME);
	PRINT_LMIP_dword(asd_ha, lseq, OPEN_TIMER_TERM_TS);
	PRINT_LMIP_dword(asd_ha, lseq, SRST_AS_TIMER_TERM_TS);
	PRINT_LMIP_dword(asd_ha, lseq, LAST_LOADED_SG_EL);

	asd_printk("LSEQ%d MDP 2 MODE 2 >>>>\n", lseq);
	PRINT_LMIP_dword(asd_ha, lseq, CLOSE_TIMER_TERM_TS);
	PRINT_LMIP_dword(asd_ha, lseq, BREAK_TIMER_TERM_TS);
	PRINT_LMIP_dword(asd_ha, lseq, DWS_RESET_TIMER_TERM_TS);
	PRINT_LMIP_dword(asd_ha, lseq, SATA_INTERLOCK_TIMER_TERM_TS);
	PRINT_LMIP_dword(asd_ha, lseq, MCTL_TIMER_TERM_TS);

	asd_printk("LSEQ%d MDP 2 MODE 4/5 >>>>\n", lseq);
	PRINT_LMIP_dword(asd_ha, lseq, COMINIT_TIMER_TERM_TS);
	PRINT_LMIP_dword(asd_ha, lseq, RCV_ID_TIMER_TERM_TS);
	PRINT_LMIP_dword(asd_ha, lseq, RCV_FIS_TIMER_TERM_TS);
	PRINT_LMIP_dword(asd_ha, lseq, DEV_PRES_TIMER_TERM_TS);
}

#if 0

/**
 * asd_dump_ddb_site -- dump a CSEQ DDB site
 * @asd_ha: pointer to host adapter structure
 * @site_no: site number of interest
 */
void asd_dump_target_ddb(struct asd_ha_struct *asd_ha, u16 site_no)
{
	if (site_no >= asd_ha->hw_prof.max_ddbs)
		return;

#define DDB_FIELDB(__name)                                        \
	asd_ddbsite_read_byte(asd_ha, site_no,                    \
			      offsetof(struct asd_ddb_ssp_smp_target_port, __name))
#define DDB2_FIELDB(__name)                                       \
	asd_ddbsite_read_byte(asd_ha, site_no,                    \
			      offsetof(struct asd_ddb_stp_sata_target_port, __name))
#define DDB_FIELDW(__name)                                        \
	asd_ddbsite_read_word(asd_ha, site_no,                    \
			      offsetof(struct asd_ddb_ssp_smp_target_port, __name))

#define DDB_FIELDD(__name)                                         \
	asd_ddbsite_read_dword(asd_ha, site_no,                    \
			       offsetof(struct asd_ddb_ssp_smp_target_port, __name))

	asd_printk("DDB: 0x%02x\n", site_no);
	asd_printk("conn_type: 0x%02x\n", DDB_FIELDB(conn_type));
	asd_printk("conn_rate: 0x%02x\n", DDB_FIELDB(conn_rate));
	asd_printk("init_conn_tag: 0x%04x\n", be16_to_cpu(DDB_FIELDW(init_conn_tag)));
	asd_printk("send_queue_head: 0x%04x\n", be16_to_cpu(DDB_FIELDW(send_queue_head)));
	asd_printk("sq_suspended: 0x%02x\n", DDB_FIELDB(sq_suspended));
	asd_printk("DDB Type: 0x%02x\n", DDB_FIELDB(ddb_type));
	asd_printk("AWT Default: 0x%04x\n", DDB_FIELDW(awt_def));
	asd_printk("compat_features: 0x%02x\n", DDB_FIELDB(compat_features));
	asd_printk("Pathway Blocked Count: 0x%02x\n",
		   DDB_FIELDB(pathway_blocked_count));
	asd_printk("arb_wait_time: 0x%04x\n", DDB_FIELDW(arb_wait_time));
	asd_printk("more_compat_features: 0x%08x\n",
		   DDB_FIELDD(more_compat_features));
	asd_printk("Conn Mask: 0x%02x\n", DDB_FIELDB(conn_mask));
	asd_printk("flags: 0x%02x\n", DDB_FIELDB(flags));
	asd_printk("flags2: 0x%02x\n", DDB2_FIELDB(flags2));
	asd_printk("ExecQ Tail: 0x%04x\n",DDB_FIELDW(exec_queue_tail));
	asd_printk("SendQ Tail: 0x%04x\n",DDB_FIELDW(send_queue_tail));
	asd_printk("Active Task Count: 0x%04x\n",
		   DDB_FIELDW(active_task_count));
	asd_printk("ITNL Reason: 0x%02x\n", DDB_FIELDB(itnl_reason));
	asd_printk("ITNL Timeout Const: 0x%04x\n", DDB_FIELDW(itnl_timeout));
	asd_printk("ITNL timestamp: 0x%08x\n", DDB_FIELDD(itnl_timestamp));
}

void asd_dump_ddb_0(struct asd_ha_struct *asd_ha)
{
#define DDB0_FIELDB(__name)                                  \
	asd_ddbsite_read_byte(asd_ha, 0,                     \
			      offsetof(struct asd_ddb_seq_shared, __name))
#define DDB0_FIELDW(__name)                                  \
	asd_ddbsite_read_word(asd_ha, 0,                     \
			      offsetof(struct asd_ddb_seq_shared, __name))

#define DDB0_FIELDD(__name)                                  \
	asd_ddbsite_read_dword(asd_ha,0 ,                    \
			       offsetof(struct asd_ddb_seq_shared, __name))

#define DDB0_FIELDA(__name, _o)                              \
	asd_ddbsite_read_byte(asd_ha, 0,                     \
			      offsetof(struct asd_ddb_seq_shared, __name)+_o)


	asd_printk("DDB: 0\n");
	asd_printk("q_free_ddb_head:%04x\n", DDB0_FIELDW(q_free_ddb_head));
	asd_printk("q_free_ddb_tail:%04x\n", DDB0_FIELDW(q_free_ddb_tail));
	asd_printk("q_free_ddb_cnt:%04x\n",  DDB0_FIELDW(q_free_ddb_cnt));
	asd_printk("q_used_ddb_head:%04x\n", DDB0_FIELDW(q_used_ddb_head));
	asd_printk("q_used_ddb_tail:%04x\n", DDB0_FIELDW(q_used_ddb_tail));
	asd_printk("shared_mem_lock:%04x\n", DDB0_FIELDW(shared_mem_lock));
	asd_printk("smp_conn_tag:%04x\n",    DDB0_FIELDW(smp_conn_tag));
	asd_printk("est_nexus_buf_cnt:%04x\n", DDB0_FIELDW(est_nexus_buf_cnt));
	asd_printk("est_nexus_buf_thresh:%04x\n",
		   DDB0_FIELDW(est_nexus_buf_thresh));
	asd_printk("conn_not_active:%02x\n", DDB0_FIELDB(conn_not_active));
	asd_printk("phy_is_up:%02x\n",       DDB0_FIELDB(phy_is_up));
	asd_printk("port_map_by_links:%02x %02x %02x %02x "
		   "%02x %02x %02x %02x\n",
		   DDB0_FIELDA(port_map_by_links, 0),
		   DDB0_FIELDA(port_map_by_links, 1),
		   DDB0_FIELDA(port_map_by_links, 2),
		   DDB0_FIELDA(port_map_by_links, 3),
		   DDB0_FIELDA(port_map_by_links, 4),
		   DDB0_FIELDA(port_map_by_links, 5),
		   DDB0_FIELDA(port_map_by_links, 6),
		   DDB0_FIELDA(port_map_by_links, 7));
}

static void asd_dump_scb_site(struct asd_ha_struct *asd_ha, u16 site_no)
{

#define SCB_FIELDB(__name)                                                 \
	asd_scbsite_read_byte(asd_ha, site_no, sizeof(struct scb_header)   \
			      + offsetof(struct initiate_ssp_task, __name))
#define SCB_FIELDW(__name)                                                 \
	asd_scbsite_read_word(asd_ha, site_no, sizeof(struct scb_header)   \
			      + offsetof(struct initiate_ssp_task, __name))
#define SCB_FIELDD(__name)                                                 \
	asd_scbsite_read_dword(asd_ha, site_no, sizeof(struct scb_header)  \
			       + offsetof(struct initiate_ssp_task, __name))

	asd_printk("Total Xfer Len: 0x%08x.\n", SCB_FIELDD(total_xfer_len));
	asd_printk("Frame Type: 0x%02x.\n", SCB_FIELDB(ssp_frame.frame_type));
	asd_printk("Tag: 0x%04x.\n", SCB_FIELDW(ssp_frame.tag));
	asd_printk("Target Port Xfer Tag: 0x%04x.\n",
		   SCB_FIELDW(ssp_frame.tptt));
	asd_printk("Data Offset: 0x%08x.\n", SCB_FIELDW(ssp_frame.data_offs));
	asd_printk("Retry Count: 0x%02x.\n", SCB_FIELDB(retry_count));
}

/**
 * asd_dump_scb_sites -- dump currently used CSEQ SCB sites
 * @asd_ha: pointer to host adapter struct
 */
void asd_dump_scb_sites(struct asd_ha_struct *asd_ha)
{
	u16	site_no;

	for (site_no = 0; site_no < asd_ha->hw_prof.max_scbs; site_no++) {
		u8 opcode;

		if (!SCB_SITE_VALID(site_no))
			continue;

		/* We are only interested in SCB sites currently used.
		 */
		opcode = asd_scbsite_read_byte(asd_ha, site_no,
					       offsetof(struct scb_header,
							opcode));
		if (opcode == 0xFF)
			continue;

		asd_printk("\nSCB: 0x%x\n", site_no);
		asd_dump_scb_site(asd_ha, site_no);
	}
}

#endif  /*  0  */

/**
 * ads_dump_seq_state -- dump CSEQ and LSEQ states
 * @asd_ha: pointer to host adapter structure
 * @lseq_mask: mask of LSEQs of interest
 */
void asd_dump_seq_state(struct asd_ha_struct *asd_ha, u8 lseq_mask)
{
	int lseq;

	asd_dump_cseq_state(asd_ha);

	if (lseq_mask != 0)
		for_each_sequencer(lseq_mask, lseq_mask, lseq)
			asd_dump_lseq_state(asd_ha, lseq);
}

void asd_dump_frame_rcvd(struct asd_phy *phy,
			 struct done_list_struct *dl)
{
	unsigned long flags;
	int i;

	switch ((dl->status_block[1] & 0x70) >> 3) {
	case SAS_PROTOCOL_STP:
		ASD_DPRINTK("STP proto device-to-host FIS:\n");
		break;
	default:
	case SAS_PROTOCOL_SSP:
		ASD_DPRINTK("SAS proto IDENTIFY:\n");
		break;
	}
	spin_lock_irqsave(&phy->sas_phy.frame_rcvd_lock, flags);
	for (i = 0; i < phy->sas_phy.frame_rcvd_size; i+=4)
		ASD_DPRINTK("%02x: %02x %02x %02x %02x\n",
			    i,
			    phy->frame_rcvd[i],
			    phy->frame_rcvd[i+1],
			    phy->frame_rcvd[i+2],
			    phy->frame_rcvd[i+3]);
	spin_unlock_irqrestore(&phy->sas_phy.frame_rcvd_lock, flags);
}

#if 0

static void asd_dump_scb(struct asd_ascb *ascb, int ind)
{
	asd_printk("scb%d: vaddr: 0x%p, dma_handle: 0x%llx, next: 0x%llx, "
		   "index:%d, opcode:0x%02x\n",
		   ind, ascb->dma_scb.vaddr,
		   (unsigned long long)ascb->dma_scb.dma_handle,
		   (unsigned long long)
		   le64_to_cpu(ascb->scb->header.next_scb),
		   le16_to_cpu(ascb->scb->header.index),
		   ascb->scb->header.opcode);
}

void asd_dump_scb_list(struct asd_ascb *ascb, int num)
{
	int i = 0;

	asd_printk("dumping %d scbs:\n", num);

	asd_dump_scb(ascb, i++);
	--num;

	if (num > 0 && !list_empty(&ascb->list)) {
		struct list_head *el;

		list_for_each(el, &ascb->list) {
			struct asd_ascb *s = list_entry(el, struct asd_ascb,
							list);
			asd_dump_scb(s, i++);
			if (--num <= 0)
				break;
		}
	}
}

#endif  /*  0  */

#endif /* ASD_DEBUG */
