/*
 * Copyright 2001 Mike Corrigan, IBM Corp
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#include <linux/types.h>
#include <linux/threads.h>
#include <linux/module.h>
#include <linux/bitops.h>
#include <asm/processor.h>
#include <asm/ptrace.h>
#include <asm/abs_addr.h>
#include <asm/lppaca.h>
#include <asm/paca.h>
#include <asm/iseries/lpar_map.h>
#include <asm/iseries/it_lp_queue.h>
#include <asm/iseries/alpaca.h>

#include "naca.h"
#include "vpd_areas.h"
#include "spcomm_area.h"
#include "ipl_parms.h"
#include "processor_vpd.h"
#include "release_data.h"
#include "it_exp_vpd_panel.h"
#include "it_lp_naca.h"

/* The HvReleaseData is the root of the information shared between
 * the hypervisor and Linux.
 */
const struct HvReleaseData hvReleaseData = {
	.xDesc = 0xc8a5d9c4,	/* "HvRD" ebcdic */
	.xSize = sizeof(struct HvReleaseData),
	.xVpdAreasPtrOffset = offsetof(struct naca_struct, xItVpdAreas),
	.xSlicNacaAddr = &naca,		/* 64-bit Naca address */
	.xMsNucDataOffset = LPARMAP_PHYS,
	.xFlags = HVREL_TAGSINACTIVE	/* tags inactive       */
					/* 64 bit              */
					/* shared processors   */
					/* HMT allowed         */
		  | 6,			/* TEMP: This allows non-GA driver */
	.xVrmIndex = 4,			/* We are v5r2m0               */
	.xMinSupportedPlicVrmIndex = 3,		/* v5r1m0 */
	.xMinCompatablePlicVrmIndex = 3,	/* v5r1m0 */
	.xVrmName = { 0xd3, 0x89, 0x95, 0xa4,	/* "Linux 2.4.64" ebcdic */
		0xa7, 0x40, 0xf2, 0x4b,
		0xf4, 0x4b, 0xf6, 0xf4 },
};

/*
 * The NACA.  The first dword of the naca is required by the iSeries
 * hypervisor to point to itVpdAreas.  The hypervisor finds the NACA
 * through the pointer in hvReleaseData.
 */
struct naca_struct naca = {
	.xItVpdAreas = &itVpdAreas,
	.xRamDisk = 0,
	.xRamDiskSize = 0,
};

struct ItLpRegSave {
	u32	xDesc;		// Eye catcher  "LpRS" ebcdic	000-003
	u16	xSize;		// Size of this class		004-005
	u8	xInUse;         // Area is live                 006-007
	u8	xRsvd1[9];	// Reserved			007-00F

	u8      xFixedRegSave[352]; // Fixed Register Save Area 010-16F
	u32	xCTRL;		// Control Register		170-173
	u32	xDEC;		// Decrementer			174-177
	u32	xFPSCR;		// FP Status and Control Reg	178-17B
	u32	xPVR;		// Processor Version Number	17C-17F

	u64	xMMCR0;		// Monitor Mode Control Reg 0	180-187
	u32	xPMC1;		// Perf Monitor Counter 1	188-18B
	u32	xPMC2;		// Perf Monitor Counter 2	18C-18F
	u32	xPMC3;		// Perf Monitor Counter 3	190-193
	u32	xPMC4;		// Perf Monitor Counter 4	194-197
	u32	xPIR;		// Processor ID Reg		198-19B

	u32	xMMCR1;		// Monitor Mode Control Reg 1	19C-19F
	u32	xMMCRA;		// Monitor Mode Control Reg A	1A0-1A3
	u32	xPMC5;		// Perf Monitor Counter 5	1A4-1A7
	u32	xPMC6;		// Perf Monitor Counter 6	1A8-1AB
	u32	xPMC7;		// Perf Monitor Counter 7	1AC-1AF
	u32	xPMC8;		// Perf Monitor Counter 8	1B0-1B3
	u32	xTSC;		// Thread Switch Control	1B4-1B7
	u32	xTST;		// Thread Switch Timeout	1B8-1BB
	u32	xRsvd;          // Reserved                     1BC-1BF

	u64	xACCR;		// Address Compare Control Reg	1C0-1C7
	u64	xIMR;		// Instruction Match Register	1C8-1CF
	u64	xSDR1;		// Storage Description Reg 1	1D0-1D7
	u64	xSPRG0;		// Special Purpose Reg General0	1D8-1DF
	u64	xSPRG1;		// Special Purpose Reg General1	1E0-1E7
	u64	xSPRG2;		// Special Purpose Reg General2	1E8-1EF
	u64	xSPRG3;		// Special Purpose Reg General3	1F0-1F7
	u64	xTB;		// Time Base Register		1F8-1FF

	u64	xFPR[32];	// Floating Point Registers	200-2FF

	u64	xMSR;		// Machine State Register	300-307
	u64	xNIA;		// Next Instruction Address	308-30F

	u64	xDABR;		// Data Address Breakpoint Reg	310-317
	u64	xIABR;		// Inst Address Breakpoint Reg	318-31F

	u64	xHID0;		// HW Implementation Dependent0	320-327

	u64	xHID4;		// HW Implementation Dependent4	328-32F
	u64	xSCOMd;		// SCON Data Reg (SPRG4)	330-337
	u64	xSCOMc;		// SCON Command Reg (SPRG5)	338-33F
	u64	xSDAR;		// Sample Data Address Register	340-347
	u64	xSIAR;		// Sample Inst Address Register	348-34F

	u8	xRsvd3[176];	// Reserved			350-3FF
};

extern void system_reset_iSeries(void);
extern void machine_check_iSeries(void);
extern void data_access_iSeries(void);
extern void instruction_access_iSeries(void);
extern void hardware_interrupt_iSeries(void);
extern void alignment_iSeries(void);
extern void program_check_iSeries(void);
extern void fp_unavailable_iSeries(void);
extern void decrementer_iSeries(void);
extern void trap_0a_iSeries(void);
extern void trap_0b_iSeries(void);
extern void system_call_iSeries(void);
extern void single_step_iSeries(void);
extern void trap_0e_iSeries(void);
extern void performance_monitor_iSeries(void);
extern void data_access_slb_iSeries(void);
extern void instruction_access_slb_iSeries(void);

struct ItLpNaca itLpNaca = {
	.xDesc = 0xd397d581,		/* "LpNa" ebcdic */
	.xSize = 0x0400,		/* size of ItLpNaca */
	.xIntHdlrOffset = 0x0300,	/* offset to int array */
	.xMaxIntHdlrEntries = 19,	/* # ents */
	.xPrimaryLpIndex = 0,		/* Part # of primary */
	.xServiceLpIndex = 0,		/* Part # of serv */
	.xLpIndex = 0,			/* Part # of me */
	.xMaxLpQueues = 0,		/* # of LP queues */
	.xLpQueueOffset = 0x100,	/* offset of start of LP queues */
	.xPirEnvironMode = 0,		/* Piranha stuff */
	.xPirConsoleMode = 0,
	.xPirDasdMode = 0,
	.flags = 0,
	.xSpVpdFormat = 0,
	.xIntProcRatio = 0,
	.xPlicVrmIndex = 0,		/* VRM index of PLIC */
	.xMinSupportedSlicVrmInd = 0,	/* min supported SLIC */
	.xMinCompatableSlicVrmInd = 0,	/* min compat SLIC */
	.xLoadAreaAddr = 0,		/* 64-bit addr of load area */
	.xLoadAreaChunks = 0,		/* chunks for load area */
	.xPaseSysCallCRMask = 0,	/* PASE mask */
	.xSlicSegmentTablePtr = 0,	/* seg table */
	.xOldLpQueue = { 0 },		/* Old LP Queue */
	.xInterruptHdlr = {
		(u64)system_reset_iSeries,	/* 0x100 System Reset */
		(u64)machine_check_iSeries,	/* 0x200 Machine Check */
		(u64)data_access_iSeries,	/* 0x300 Data Access */
		(u64)instruction_access_iSeries, /* 0x400 Instruction Access */
		(u64)hardware_interrupt_iSeries, /* 0x500 External */
		(u64)alignment_iSeries,		/* 0x600 Alignment */
		(u64)program_check_iSeries,	/* 0x700 Program Check */
		(u64)fp_unavailable_iSeries,	/* 0x800 FP Unavailable */
		(u64)decrementer_iSeries,	/* 0x900 Decrementer */
		(u64)trap_0a_iSeries,		/* 0xa00 Trap 0A */
		(u64)trap_0b_iSeries,		/* 0xb00 Trap 0B */
		(u64)system_call_iSeries,	/* 0xc00 System Call */
		(u64)single_step_iSeries,	/* 0xd00 Single Step */
		(u64)trap_0e_iSeries,		/* 0xe00 Trap 0E */
		(u64)performance_monitor_iSeries,/* 0xf00 Performance Monitor */
		0,				/* int 0x1000 */
		0,				/* int 0x1010 */
		0,				/* int 0x1020 CPU ctls */
		(u64)hardware_interrupt_iSeries, /* SC Ret Hdlr */
		(u64)data_access_slb_iSeries,	/* 0x380 D-SLB */
		(u64)instruction_access_slb_iSeries /* 0x480 I-SLB */
	}
};

/* May be filled in by the hypervisor so cannot end up in the BSS */
static struct ItIplParmsReal xItIplParmsReal __attribute__((__section__(".data")));

/* May be filled in by the hypervisor so cannot end up in the BSS */
struct ItExtVpdPanel xItExtVpdPanel __attribute__((__section__(".data")));

#define maxPhysicalProcessors 32

struct IoHriProcessorVpd xIoHriProcessorVpd[maxPhysicalProcessors] = {
	{
		.xInstCacheOperandSize = 32,
		.xDataCacheOperandSize = 32,
		.xProcFreq     = 50000000,
		.xTimeBaseFreq = 50000000,
		.xPVR = 0x3600
	}
};

/* Space for Main Store Vpd 27,200 bytes */
/* May be filled in by the hypervisor so cannot end up in the BSS */
u64    xMsVpd[3400] __attribute__((__section__(".data")));

/* Space for Recovery Log Buffer */
/* May be filled in by the hypervisor so cannot end up in the BSS */
static u64    xRecoveryLogBuffer[32] __attribute__((__section__(".data")));

static const struct SpCommArea xSpCommArea = {
	.xDesc = 0xE2D7C3C2,
	.xFormat = 1,
};

static const struct ItLpRegSave iseries_reg_save[] = {
	[0 ... (NR_CPUS-1)] = {
		.xDesc = 0xd397d9e2,	/* "LpRS" */
		.xSize = sizeof(struct ItLpRegSave),
	},
};

#define ALPACA_INIT(number)						\
{									\
	.lppaca_ptr = &lppaca[number],					\
	.reg_save_ptr = &iseries_reg_save[number],			\
}

const struct alpaca alpaca[] = {
	ALPACA_INIT( 0),
#if NR_CPUS > 1
	ALPACA_INIT( 1), ALPACA_INIT( 2), ALPACA_INIT( 3),
#if NR_CPUS > 4
	ALPACA_INIT( 4), ALPACA_INIT( 5), ALPACA_INIT( 6), ALPACA_INIT( 7),
#if NR_CPUS > 8
	ALPACA_INIT( 8), ALPACA_INIT( 9), ALPACA_INIT(10), ALPACA_INIT(11),
	ALPACA_INIT(12), ALPACA_INIT(13), ALPACA_INIT(14), ALPACA_INIT(15),
	ALPACA_INIT(16), ALPACA_INIT(17), ALPACA_INIT(18), ALPACA_INIT(19),
	ALPACA_INIT(20), ALPACA_INIT(21), ALPACA_INIT(22), ALPACA_INIT(23),
	ALPACA_INIT(24), ALPACA_INIT(25), ALPACA_INIT(26), ALPACA_INIT(27),
	ALPACA_INIT(28), ALPACA_INIT(29), ALPACA_INIT(30), ALPACA_INIT(31),
#if NR_CPUS > 32
	ALPACA_INIT(32), ALPACA_INIT(33), ALPACA_INIT(34), ALPACA_INIT(35),
	ALPACA_INIT(36), ALPACA_INIT(37), ALPACA_INIT(38), ALPACA_INIT(39),
	ALPACA_INIT(40), ALPACA_INIT(41), ALPACA_INIT(42), ALPACA_INIT(43),
	ALPACA_INIT(44), ALPACA_INIT(45), ALPACA_INIT(46), ALPACA_INIT(47),
	ALPACA_INIT(48), ALPACA_INIT(49), ALPACA_INIT(50), ALPACA_INIT(51),
	ALPACA_INIT(52), ALPACA_INIT(53), ALPACA_INIT(54), ALPACA_INIT(55),
	ALPACA_INIT(56), ALPACA_INIT(57), ALPACA_INIT(58), ALPACA_INIT(59),
	ALPACA_INIT(60), ALPACA_INIT(61), ALPACA_INIT(62), ALPACA_INIT(63),
#endif
#endif
#endif
#endif
};

/* The LparMap data is now located at offset 0x6000 in head.S
 * It was put there so that the HvReleaseData could address it
 * with a 32-bit offset as required by the iSeries hypervisor
 *
 * The Naca has a pointer to the ItVpdAreas.  The hypervisor finds
 * the Naca via the HvReleaseData area.  The HvReleaseData has the
 * offset into the Naca of the pointer to the ItVpdAreas.
 */
const struct ItVpdAreas itVpdAreas = {
	.xSlicDesc = 0xc9a3e5c1,		/* "ItVA" */
	.xSlicSize = sizeof(struct ItVpdAreas),
	.xSlicVpdEntries = ItVpdMaxEntries,	/* # VPD array entries */
	.xSlicDmaEntries = ItDmaMaxEntries,	/* # DMA array entries */
	.xSlicMaxLogicalProcs = NR_CPUS * 2,	/* Max logical procs */
	.xSlicMaxPhysicalProcs = maxPhysicalProcessors,	/* Max physical procs */
	.xSlicDmaToksOffset = offsetof(struct ItVpdAreas, xPlicDmaToks),
	.xSlicVpdAdrsOffset = offsetof(struct ItVpdAreas, xSlicVpdAdrs),
	.xSlicDmaLensOffset = offsetof(struct ItVpdAreas, xPlicDmaLens),
	.xSlicVpdLensOffset = offsetof(struct ItVpdAreas, xSlicVpdLens),
	.xSlicMaxSlotLabels = 0,		/* max slot labels */
	.xSlicMaxLpQueues = 1,			/* max LP queues */
	.xPlicDmaLens = { 0 },			/* DMA lengths */
	.xPlicDmaToks = { 0 },			/* DMA tokens */
	.xSlicVpdLens = {			/* VPD lengths */
	        0,0,0,		        /*  0 - 2 */
		sizeof(xItExtVpdPanel), /*       3 Extended VPD   */
		sizeof(struct alpaca),	/*       4 length of (fake) Paca  */
		0,			/*       5 */
		sizeof(struct ItIplParmsReal),/* 6 length of IPL parms */
		26992,			/*	 7 length of MS VPD */
		0,			/*       8 */
		sizeof(struct ItLpNaca),/*       9 length of LP Naca */
		0,			/*	10 */
		256,			/*	11 length of Recovery Log Buf */
		sizeof(struct SpCommArea), /*   12 length of SP Comm Area */
		0,0,0,			/* 13 - 15 */
		sizeof(struct IoHriProcessorVpd),/* 16 length of Proc Vpd */
		0,0,0,0,0,0,		/* 17 - 22  */
		sizeof(struct hvlpevent_queue),	/* 23 length of Lp Queue */
		0,0			/* 24 - 25 */
		},
	.xSlicVpdAdrs = {			/* VPD addresses */
		0,0,0,			/*	 0 -  2 */
		&xItExtVpdPanel,        /*       3 Extended VPD */
		&alpaca[0],		/*       4 first (fake) Paca */
		0,			/*       5 */
		&xItIplParmsReal,	/*	 6 IPL parms */
		&xMsVpd,		/*	 7 MS Vpd */
		0,			/*       8 */
		&itLpNaca,		/*       9 LpNaca */
		0,			/*	10 */
		&xRecoveryLogBuffer,	/*	11 Recovery Log Buffer */
		&xSpCommArea,		/*	12 SP Comm Area */
		0,0,0,			/* 13 - 15 */
		&xIoHriProcessorVpd,	/*      16 Proc Vpd */
		0,0,0,0,0,0,		/* 17 - 22 */
		&hvlpevent_queue,	/*      23 Lp Queue */
		0,0
	}
};
