/* SPDX-License-Identifier: GPL-2.0
 * Marvell OcteonTX CPT driver
 *
 * Copyright (C) 2019 Marvell International Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __OTX_CPT_HW_TYPES_H
#define __OTX_CPT_HW_TYPES_H

#include <linux/types.h>

/* Device IDs */
#define OTX_CPT_PCI_PF_DEVICE_ID 0xa040

#define OTX_CPT_PCI_PF_SUBSYS_ID 0xa340

/* Configuration and status registers are in BAR0 on OcteonTX platform */
#define OTX_CPT_PF_PCI_CFG_BAR	0
/* Mailbox interrupts offset */
#define OTX_CPT_PF_MBOX_INT	3
#define OTX_CPT_PF_INT_VEC_E_MBOXX(x, a) ((x) + (a))
/* Number of MSIX supported in PF */
#define OTX_CPT_PF_MSIX_VECTORS 4
/* Maximum supported microcode groups */
#define OTX_CPT_MAX_ENGINE_GROUPS 8

/* OcteonTX CPT PF registers */
#define OTX_CPT_PF_CONSTANTS		(0x0ll)
#define OTX_CPT_PF_RESET		(0x100ll)
#define OTX_CPT_PF_DIAG			(0x120ll)
#define OTX_CPT_PF_BIST_STATUS		(0x160ll)
#define OTX_CPT_PF_ECC0_CTL		(0x200ll)
#define OTX_CPT_PF_ECC0_FLIP		(0x210ll)
#define OTX_CPT_PF_ECC0_INT		(0x220ll)
#define OTX_CPT_PF_ECC0_INT_W1S		(0x230ll)
#define OTX_CPT_PF_ECC0_ENA_W1S		(0x240ll)
#define OTX_CPT_PF_ECC0_ENA_W1C		(0x250ll)
#define OTX_CPT_PF_MBOX_INTX(b)		(0x400ll | (u64)(b) << 3)
#define OTX_CPT_PF_MBOX_INT_W1SX(b)	(0x420ll | (u64)(b) << 3)
#define OTX_CPT_PF_MBOX_ENA_W1CX(b)	(0x440ll | (u64)(b) << 3)
#define OTX_CPT_PF_MBOX_ENA_W1SX(b)	(0x460ll | (u64)(b) << 3)
#define OTX_CPT_PF_EXEC_INT		(0x500ll)
#define OTX_CPT_PF_EXEC_INT_W1S		(0x520ll)
#define OTX_CPT_PF_EXEC_ENA_W1C		(0x540ll)
#define OTX_CPT_PF_EXEC_ENA_W1S		(0x560ll)
#define OTX_CPT_PF_GX_EN(b)		(0x600ll | (u64)(b) << 3)
#define OTX_CPT_PF_EXEC_INFO		(0x700ll)
#define OTX_CPT_PF_EXEC_BUSY		(0x800ll)
#define OTX_CPT_PF_EXEC_INFO0		(0x900ll)
#define OTX_CPT_PF_EXEC_INFO1		(0x910ll)
#define OTX_CPT_PF_INST_REQ_PC		(0x10000ll)
#define OTX_CPT_PF_INST_LATENCY_PC	(0x10020ll)
#define OTX_CPT_PF_RD_REQ_PC		(0x10040ll)
#define OTX_CPT_PF_RD_LATENCY_PC	(0x10060ll)
#define OTX_CPT_PF_RD_UC_PC		(0x10080ll)
#define OTX_CPT_PF_ACTIVE_CYCLES_PC	(0x10100ll)
#define OTX_CPT_PF_EXE_CTL		(0x4000000ll)
#define OTX_CPT_PF_EXE_STATUS		(0x4000008ll)
#define OTX_CPT_PF_EXE_CLK		(0x4000010ll)
#define OTX_CPT_PF_EXE_DBG_CTL		(0x4000018ll)
#define OTX_CPT_PF_EXE_DBG_DATA		(0x4000020ll)
#define OTX_CPT_PF_EXE_BIST_STATUS	(0x4000028ll)
#define OTX_CPT_PF_EXE_REQ_TIMER	(0x4000030ll)
#define OTX_CPT_PF_EXE_MEM_CTL		(0x4000038ll)
#define OTX_CPT_PF_EXE_PERF_CTL		(0x4001000ll)
#define OTX_CPT_PF_EXE_DBG_CNTX(b)	(0x4001100ll | (u64)(b) << 3)
#define OTX_CPT_PF_EXE_PERF_EVENT_CNT	(0x4001180ll)
#define OTX_CPT_PF_EXE_EPCI_INBX_CNT(b)	(0x4001200ll | (u64)(b) << 3)
#define OTX_CPT_PF_EXE_EPCI_OUTBX_CNT(b) (0x4001240ll | (u64)(b) << 3)
#define OTX_CPT_PF_ENGX_UCODE_BASE(b)	(0x4002000ll | (u64)(b) << 3)
#define OTX_CPT_PF_QX_CTL(b)		(0x8000000ll | (u64)(b) << 20)
#define OTX_CPT_PF_QX_GMCTL(b)		(0x8000020ll | (u64)(b) << 20)
#define OTX_CPT_PF_QX_CTL2(b)		(0x8000100ll | (u64)(b) << 20)
#define OTX_CPT_PF_VFX_MBOXX(b, c)	(0x8001000ll | (u64)(b) << 20 | \
					 (u64)(c) << 8)

/*
 * Register (NCB) otx_cpt#_pf_bist_status
 *
 * CPT PF Control Bist Status Register
 * This register has the BIST status of memories. Each bit is the BIST result
 * of an individual memory (per bit, 0 = pass and 1 = fail).
 * otx_cptx_pf_bist_status_s
 * Word0
 *  bstatus [29:0](RO/H) BIST status. One bit per memory, enumerated by
 *	CPT_RAMS_E.
 */
union otx_cptx_pf_bist_status {
	u64 u;
	struct otx_cptx_pf_bist_status_s {
#if defined(__BIG_ENDIAN_BITFIELD) /* Word 0 - Big Endian */
		u64 reserved_30_63:34;
		u64 bstatus:30;
#else /* Word 0 - Little Endian */
		u64 bstatus:30;
		u64 reserved_30_63:34;
#endif /* Word 0 - End */
	} s;
};

/*
 * Register (NCB) otx_cpt#_pf_constants
 *
 * CPT PF Constants Register
 * This register contains implementation-related parameters of CPT in CNXXXX.
 * otx_cptx_pf_constants_s
 * Word 0
 *  reserved_40_63:24 [63:40] Reserved.
 *  epcis:8 [39:32](RO) Number of EPCI busses.
 *  grps:8 [31:24](RO) Number of engine groups implemented.
 *  ae:8 [23:16](RO/H) Number of AEs. In CNXXXX, for CPT0 returns 0x0,
 *	for CPT1 returns 0x18, or less if there are fuse-disables.
 *  se:8 [15:8](RO/H) Number of SEs. In CNXXXX, for CPT0 returns 0x30,
 *	or less if there are fuse-disables, for CPT1 returns 0x0.
 *  vq:8 [7:0](RO) Number of VQs.
 */
union otx_cptx_pf_constants {
	u64 u;
	struct otx_cptx_pf_constants_s {
#if defined(__BIG_ENDIAN_BITFIELD) /* Word 0 - Big Endian */
		u64 reserved_40_63:24;
		u64 epcis:8;
		u64 grps:8;
		u64 ae:8;
		u64 se:8;
		u64 vq:8;
#else /* Word 0 - Little Endian */
		u64 vq:8;
		u64 se:8;
		u64 ae:8;
		u64 grps:8;
		u64 epcis:8;
		u64 reserved_40_63:24;
#endif /* Word 0 - End */
	} s;
};

/*
 * Register (NCB) otx_cpt#_pf_exe_bist_status
 *
 * CPT PF Engine Bist Status Register
 * This register has the BIST status of each engine.  Each bit is the
 * BIST result of an individual engine (per bit, 0 = pass and 1 = fail).
 * otx_cptx_pf_exe_bist_status_s
 * Word0
 *  reserved_48_63:16 [63:48] reserved
 *  bstatus:48 [47:0](RO/H) BIST status. One bit per engine.
 *
 */
union otx_cptx_pf_exe_bist_status {
	u64 u;
	struct otx_cptx_pf_exe_bist_status_s {
#if defined(__BIG_ENDIAN_BITFIELD) /* Word 0 - Big Endian */
		u64 reserved_48_63:16;
		u64 bstatus:48;
#else /* Word 0 - Little Endian */
		u64 bstatus:48;
		u64 reserved_48_63:16;
#endif /* Word 0 - End */
	} s;
};

/*
 * Register (NCB) otx_cpt#_pf_q#_ctl
 *
 * CPT Queue Control Register
 * This register configures queues. This register should be changed only
 * when quiescent (see CPT()_VQ()_INPROG[INFLIGHT]).
 * otx_cptx_pf_qx_ctl_s
 * Word0
 *  reserved_60_63:4 [63:60] reserved.
 *  aura:12; [59:48](R/W) Guest-aura for returning this queue's
 *	instruction-chunk buffers to FPA. Only used when [INST_FREE] is set.
 *	For the FPA to not discard the request, FPA_PF_MAP() must map
 *	[AURA] and CPT()_PF_Q()_GMCTL[GMID] as valid.
 *  reserved_45_47:3 [47:45] reserved.
 *  size:13 [44:32](R/W) Command-buffer size, in number of 64-bit words per
 *	command buffer segment. Must be 8*n + 1, where n is the number of
 *	instructions per buffer segment.
 *  reserved_11_31:21 [31:11] Reserved.
 *  cont_err:1 [10:10](R/W) Continue on error.
 *	0 = When CPT()_VQ()_MISC_INT[NWRP], CPT()_VQ()_MISC_INT[IRDE] or
 *	CPT()_VQ()_MISC_INT[DOVF] are set by hardware or software via
 *	CPT()_VQ()_MISC_INT_W1S, then CPT()_VQ()_CTL[ENA] is cleared.  Due to
 *	pipelining, additional instructions may have been processed between the
 *	instruction causing the error and the next instruction in the disabled
 *	queue (the instruction at CPT()_VQ()_SADDR).
 *	1 = Ignore errors and continue processing instructions.
 *	For diagnostic use only.
 *  inst_free:1 [9:9](R/W) Instruction FPA free. When set, when CPT reaches the
 *	end of an instruction chunk, that chunk will be freed to the FPA.
 *  inst_be:1 [8:8](R/W) Instruction big-endian control. When set, instructions,
 *	instruction next chunk pointers, and result structures are stored in
 *	big-endian format in memory.
 *  iqb_ldwb:1 [7:7](R/W) Instruction load don't write back.
 *	0 = The hardware issues NCB transient load (LDT) towards the cache,
 *	which if the line hits and is is dirty will cause the line to be
 *	written back before being replaced.
 *	1 = The hardware issues NCB LDWB read-and-invalidate command towards
 *	the cache when fetching the last word of instructions; as a result the
 *	line will not be written back when replaced.  This improves
 *	performance, but software must not read the instructions after they are
 *	posted to the hardware.	Reads that do not consume the last word of a
 *	cache line always use LDI.
 *  reserved_4_6:3 [6:4] Reserved.
 *  grp:3; [3:1](R/W) Engine group.
 *  pri:1; [0:0](R/W) Queue priority.
 *	1 = This queue has higher priority. Round-robin between higher
 *	priority queues.
 *	0 = This queue has lower priority. Round-robin between lower
 *	priority queues.
 */
union otx_cptx_pf_qx_ctl {
	u64 u;
	struct otx_cptx_pf_qx_ctl_s {
#if defined(__BIG_ENDIAN_BITFIELD) /* Word 0 - Big Endian */
		u64 reserved_60_63:4;
		u64 aura:12;
		u64 reserved_45_47:3;
		u64 size:13;
		u64 reserved_11_31:21;
		u64 cont_err:1;
		u64 inst_free:1;
		u64 inst_be:1;
		u64 iqb_ldwb:1;
		u64 reserved_4_6:3;
		u64 grp:3;
		u64 pri:1;
#else /* Word 0 - Little Endian */
		u64 pri:1;
		u64 grp:3;
		u64 reserved_4_6:3;
		u64 iqb_ldwb:1;
		u64 inst_be:1;
		u64 inst_free:1;
		u64 cont_err:1;
		u64 reserved_11_31:21;
		u64 size:13;
		u64 reserved_45_47:3;
		u64 aura:12;
		u64 reserved_60_63:4;
#endif /* Word 0 - End */
	} s;
};
#endif /* __OTX_CPT_HW_TYPES_H */
