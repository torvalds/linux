/*
 * Copyright 2001 Mike Corrigan, IBM Corp
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#include <linux/config.h>
#include <linux/types.h>
#include <linux/threads.h>
#include <linux/module.h>
#include <linux/bitops.h>
#include <asm/processor.h>
#include <asm/ptrace.h>
#include <asm/abs_addr.h>
#include <asm/iseries/it_lp_naca.h>
#include <asm/lppaca.h>
#include <asm/iseries/it_lp_reg_save.h>
#include <asm/paca.h>
#include <asm/iseries/lpar_map.h>
#include <asm/iseries/it_exp_vpd_panel.h>
#include <asm/iseries/it_lp_queue.h>

#include "naca.h"
#include "vpd_areas.h"
#include "spcomm_area.h"
#include "ipl_parms.h"
#include "processor_vpd.h"
#include "release_data.h"

/* The HvReleaseData is the root of the information shared between
 * the hypervisor and Linux.
 */
struct HvReleaseData hvReleaseData = {
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
EXPORT_SYMBOL(itLpNaca);

/* May be filled in by the hypervisor so cannot end up in the BSS */
struct ItIplParmsReal xItIplParmsReal __attribute__((__section__(".data")));

/* May be filled in by the hypervisor so cannot end up in the BSS */
struct ItExtVpdPanel xItExtVpdPanel __attribute__((__section__(".data")));
EXPORT_SYMBOL(xItExtVpdPanel);

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
u64    xRecoveryLogBuffer[32] __attribute__((__section__(".data")));

struct SpCommArea xSpCommArea = {
	.xDesc = 0xE2D7C3C2,
	.xFormat = 1,
};

/* The LparMap data is now located at offset 0x6000 in head.S
 * It was put there so that the HvReleaseData could address it
 * with a 32-bit offset as required by the iSeries hypervisor
 *
 * The Naca has a pointer to the ItVpdAreas.  The hypervisor finds
 * the Naca via the HvReleaseData area.  The HvReleaseData has the
 * offset into the Naca of the pointer to the ItVpdAreas.
 */
struct ItVpdAreas itVpdAreas = {
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
		sizeof(struct paca_struct),	/*       4 length of Paca  */
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
		&paca[0],		/*       4 first Paca */
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

struct ItLpRegSave iseries_reg_save[] = {
	[0 ... (NR_CPUS-1)] = {
		.xDesc = 0xd397d9e2,	/* "LpRS" */
		.xSize = sizeof(struct ItLpRegSave),
	},
};
