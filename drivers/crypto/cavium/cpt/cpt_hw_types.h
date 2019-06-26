/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2016 Cavium, Inc.
 */

#ifndef __CPT_HW_TYPES_H
#define __CPT_HW_TYPES_H

#include "cpt_common.h"

/**
 * Enumeration cpt_comp_e
 *
 * CPT Completion Enumeration
 * Enumerates the values of CPT_RES_S[COMPCODE].
 */
enum cpt_comp_e {
	CPT_COMP_E_NOTDONE = 0x00,
	CPT_COMP_E_GOOD = 0x01,
	CPT_COMP_E_FAULT = 0x02,
	CPT_COMP_E_SWERR = 0x03,
	CPT_COMP_E_LAST_ENTRY = 0xFF
};

/**
 * Structure cpt_inst_s
 *
 * CPT Instruction Structure
 * This structure specifies the instruction layout. Instructions are
 * stored in memory as little-endian unless CPT()_PF_Q()_CTL[INST_BE] is set.
 * cpt_inst_s_s
 * Word 0
 * doneint:1 Done interrupt.
 *	0 = No interrupts related to this instruction.
 *	1 = When the instruction completes, CPT()_VQ()_DONE[DONE] will be
 *	incremented,and based on the rules described there an interrupt may
 *	occur.
 * Word 1
 * res_addr [127: 64] Result IOVA.
 *	If nonzero, specifies where to write CPT_RES_S.
 *	If zero, no result structure will be written.
 *	Address must be 16-byte aligned.
 *	Bits <63:49> are ignored by hardware; software should use a
 *	sign-extended bit <48> for forward compatibility.
 * Word 2
 *  grp:10 [171:162] If [WQ_PTR] is nonzero, the SSO guest-group to use when
 *	CPT submits work SSO.
 *	For the SSO to not discard the add-work request, FPA_PF_MAP() must map
 *	[GRP] and CPT()_PF_Q()_GMCTL[GMID] as valid.
 *  tt:2 [161:160] If [WQ_PTR] is nonzero, the SSO tag type to use when CPT
 *	submits work to SSO
 *  tag:32 [159:128] If [WQ_PTR] is nonzero, the SSO tag to use when CPT
 *	submits work to SSO.
 * Word 3
 *  wq_ptr [255:192] If [WQ_PTR] is nonzero, it is a pointer to a
 *	work-queue entry that CPT submits work to SSO after all context,
 *	output data, and result write operations are visible to other
 *	CNXXXX units and the cores. Bits <2:0> must be zero.
 *	Bits <63:49> are ignored by hardware; software should
 *	use a sign-extended bit <48> for forward compatibility.
 *	Internal:
 *	Bits <63:49>, <2:0> are ignored by hardware, treated as always 0x0.
 * Word 4
 *  ei0; [319:256] Engine instruction word 0. Passed to the AE/SE.
 * Word 5
 *  ei1; [383:320] Engine instruction word 1. Passed to the AE/SE.
 * Word 6
 *  ei2; [447:384] Engine instruction word 1. Passed to the AE/SE.
 * Word 7
 *  ei3; [511:448] Engine instruction word 1. Passed to the AE/SE.
 *
 */
union cpt_inst_s {
	u64 u[8];
	struct cpt_inst_s_s {
#if defined(__BIG_ENDIAN_BITFIELD) /* Word 0 - Big Endian */
		u64 reserved_17_63:47;
		u64 doneint:1;
		u64 reserved_0_1:16;
#else /* Word 0 - Little Endian */
		u64 reserved_0_15:16;
		u64 doneint:1;
		u64 reserved_17_63:47;
#endif /* Word 0 - End */
		u64 res_addr;
#if defined(__BIG_ENDIAN_BITFIELD) /* Word 2 - Big Endian */
		u64 reserved_172_19:20;
		u64 grp:10;
		u64 tt:2;
		u64 tag:32;
#else /* Word 2 - Little Endian */
		u64 tag:32;
		u64 tt:2;
		u64 grp:10;
		u64 reserved_172_191:20;
#endif /* Word 2 - End */
		u64 wq_ptr;
		u64 ei0;
		u64 ei1;
		u64 ei2;
		u64 ei3;
	} s;
};

/**
 * Structure cpt_res_s
 *
 * CPT Result Structure
 * The CPT coprocessor writes the result structure after it completes a
 * CPT_INST_S instruction. The result structure is exactly 16 bytes, and
 * each instruction completion produces exactly one result structure.
 *
 * This structure is stored in memory as little-endian unless
 * CPT()_PF_Q()_CTL[INST_BE] is set.
 * cpt_res_s_s
 * Word 0
 *  doneint:1 [16:16] Done interrupt. This bit is copied from the
 *	corresponding instruction's CPT_INST_S[DONEINT].
 *  compcode:8 [7:0] Indicates completion/error status of the CPT coprocessor
 *	for the	associated instruction, as enumerated by CPT_COMP_E.
 *	Core software may write the memory location containing [COMPCODE] to
 *	0x0 before ringing the doorbell, and then poll for completion by
 *	checking for a nonzero value.
 *	Once the core observes a nonzero [COMPCODE] value in this case,the CPT
 *	coprocessor will have also completed L2/DRAM write operations.
 * Word 1
 *  reserved
 *
 */
union cpt_res_s {
	u64 u[2];
	struct cpt_res_s_s {
#if defined(__BIG_ENDIAN_BITFIELD) /* Word 0 - Big Endian */
		u64 reserved_17_63:47;
		u64 doneint:1;
		u64 reserved_8_15:8;
		u64 compcode:8;
#else /* Word 0 - Little Endian */
		u64 compcode:8;
		u64 reserved_8_15:8;
		u64 doneint:1;
		u64 reserved_17_63:47;
#endif /* Word 0 - End */
		u64 reserved_64_127;
	} s;
};

/**
 * Register (NCB) cpt#_pf_bist_status
 *
 * CPT PF Control Bist Status Register
 * This register has the BIST status of memories. Each bit is the BIST result
 * of an individual memory (per bit, 0 = pass and 1 = fail).
 * cptx_pf_bist_status_s
 * Word0
 *  bstatus [29:0](RO/H) BIST status. One bit per memory, enumerated by
 *	CPT_RAMS_E.
 */
union cptx_pf_bist_status {
	u64 u;
	struct cptx_pf_bist_status_s {
#if defined(__BIG_ENDIAN_BITFIELD) /* Word 0 - Big Endian */
		u64 reserved_30_63:34;
		u64 bstatus:30;
#else /* Word 0 - Little Endian */
		u64 bstatus:30;
		u64 reserved_30_63:34;
#endif /* Word 0 - End */
	} s;
};

/**
 * Register (NCB) cpt#_pf_constants
 *
 * CPT PF Constants Register
 * This register contains implementation-related parameters of CPT in CNXXXX.
 * cptx_pf_constants_s
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
union cptx_pf_constants {
	u64 u;
	struct cptx_pf_constants_s {
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

/**
 * Register (NCB) cpt#_pf_exe_bist_status
 *
 * CPT PF Engine Bist Status Register
 * This register has the BIST status of each engine.  Each bit is the
 * BIST result of an individual engine (per bit, 0 = pass and 1 = fail).
 * cptx_pf_exe_bist_status_s
 * Word0
 *  reserved_48_63:16 [63:48] reserved
 *  bstatus:48 [47:0](RO/H) BIST status. One bit per engine.
 *
 */
union cptx_pf_exe_bist_status {
	u64 u;
	struct cptx_pf_exe_bist_status_s {
#if defined(__BIG_ENDIAN_BITFIELD) /* Word 0 - Big Endian */
		u64 reserved_48_63:16;
		u64 bstatus:48;
#else /* Word 0 - Little Endian */
		u64 bstatus:48;
		u64 reserved_48_63:16;
#endif /* Word 0 - End */
	} s;
};

/**
 * Register (NCB) cpt#_pf_q#_ctl
 *
 * CPT Queue Control Register
 * This register configures queues. This register should be changed only
 * when quiescent (see CPT()_VQ()_INPROG[INFLIGHT]).
 * cptx_pf_qx_ctl_s
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
union cptx_pf_qx_ctl {
	u64 u;
	struct cptx_pf_qx_ctl_s {
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

/**
 * Register (NCB) cpt#_vq#_saddr
 *
 * CPT Queue Starting Buffer Address Registers
 * These registers set the instruction buffer starting address.
 * cptx_vqx_saddr_s
 * Word0
 *  reserved_49_63:15 [63:49] Reserved.
 *  ptr:43 [48:6](R/W/H) Instruction buffer IOVA <48:6> (64-byte aligned).
 *	When written, it is the initial buffer starting address; when read,
 *	it is the next read pointer to be requested from L2C. The PTR field
 *	is overwritten with the next pointer each time that the command buffer
 *	segment is exhausted. New commands will then be read from the newly
 *	specified command buffer pointer.
 *  reserved_0_5:6 [5:0] Reserved.
 *
 */
union cptx_vqx_saddr {
	u64 u;
	struct cptx_vqx_saddr_s {
#if defined(__BIG_ENDIAN_BITFIELD) /* Word 0 - Big Endian */
		u64 reserved_49_63:15;
		u64 ptr:43;
		u64 reserved_0_5:6;
#else /* Word 0 - Little Endian */
		u64 reserved_0_5:6;
		u64 ptr:43;
		u64 reserved_49_63:15;
#endif /* Word 0 - End */
	} s;
};

/**
 * Register (NCB) cpt#_vq#_misc_ena_w1s
 *
 * CPT Queue Misc Interrupt Enable Set Register
 * This register sets interrupt enable bits.
 * cptx_vqx_misc_ena_w1s_s
 * Word0
 * reserved_5_63:59 [63:5] Reserved.
 * swerr:1 [4:4](R/W1S/H) Reads or sets enable for
 *	CPT(0..1)_VQ(0..63)_MISC_INT[SWERR].
 * nwrp:1 [3:3](R/W1S/H) Reads or sets enable for
 *	CPT(0..1)_VQ(0..63)_MISC_INT[NWRP].
 * irde:1 [2:2](R/W1S/H) Reads or sets enable for
 *	CPT(0..1)_VQ(0..63)_MISC_INT[IRDE].
 * dovf:1 [1:1](R/W1S/H) Reads or sets enable for
 *	CPT(0..1)_VQ(0..63)_MISC_INT[DOVF].
 * mbox:1 [0:0](R/W1S/H) Reads or sets enable for
 *	CPT(0..1)_VQ(0..63)_MISC_INT[MBOX].
 *
 */
union cptx_vqx_misc_ena_w1s {
	u64 u;
	struct cptx_vqx_misc_ena_w1s_s {
#if defined(__BIG_ENDIAN_BITFIELD) /* Word 0 - Big Endian */
		u64 reserved_5_63:59;
		u64 swerr:1;
		u64 nwrp:1;
		u64 irde:1;
		u64 dovf:1;
		u64 mbox:1;
#else /* Word 0 - Little Endian */
		u64 mbox:1;
		u64 dovf:1;
		u64 irde:1;
		u64 nwrp:1;
		u64 swerr:1;
		u64 reserved_5_63:59;
#endif /* Word 0 - End */
	} s;
};

/**
 * Register (NCB) cpt#_vq#_doorbell
 *
 * CPT Queue Doorbell Registers
 * Doorbells for the CPT instruction queues.
 * cptx_vqx_doorbell_s
 * Word0
 *  reserved_20_63:44 [63:20] Reserved.
 *  dbell_cnt:20 [19:0](R/W/H) Number of instruction queue 64-bit words to add
 *	to the CPT instruction doorbell count. Readback value is the the
 *	current number of pending doorbell requests. If counter overflows
 *	CPT()_VQ()_MISC_INT[DBELL_DOVF] is set. To reset the count back to
 *	zero, write one to clear CPT()_VQ()_MISC_INT_ENA_W1C[DBELL_DOVF],
 *	then write a value of 2^20 minus the read [DBELL_CNT], then write one
 *	to CPT()_VQ()_MISC_INT_W1C[DBELL_DOVF] and
 *	CPT()_VQ()_MISC_INT_ENA_W1S[DBELL_DOVF]. Must be a multiple of 8.
 *	All CPT instructions are 8 words and require a doorbell count of
 *	multiple of 8.
 */
union cptx_vqx_doorbell {
	u64 u;
	struct cptx_vqx_doorbell_s {
#if defined(__BIG_ENDIAN_BITFIELD) /* Word 0 - Big Endian */
		u64 reserved_20_63:44;
		u64 dbell_cnt:20;
#else /* Word 0 - Little Endian */
		u64 dbell_cnt:20;
		u64 reserved_20_63:44;
#endif /* Word 0 - End */
	} s;
};

/**
 * Register (NCB) cpt#_vq#_inprog
 *
 * CPT Queue In Progress Count Registers
 * These registers contain the per-queue instruction in flight registers.
 * cptx_vqx_inprog_s
 * Word0
 *  reserved_8_63:56 [63:8] Reserved.
 *  inflight:8 [7:0](RO/H) Inflight count. Counts the number of instructions
 *	for the VF for which CPT is fetching, executing or responding to
 *	instructions. However this does not include any interrupts that are
 *	awaiting software handling (CPT()_VQ()_DONE[DONE] != 0x0).
 *	A queue may not be reconfigured until:
 *	1. CPT()_VQ()_CTL[ENA] is cleared by software.
 *	2. [INFLIGHT] is polled until equals to zero.
 */
union cptx_vqx_inprog {
	u64 u;
	struct cptx_vqx_inprog_s {
#if defined(__BIG_ENDIAN_BITFIELD) /* Word 0 - Big Endian */
		u64 reserved_8_63:56;
		u64 inflight:8;
#else /* Word 0 - Little Endian */
		u64 inflight:8;
		u64 reserved_8_63:56;
#endif /* Word 0 - End */
	} s;
};

/**
 * Register (NCB) cpt#_vq#_misc_int
 *
 * CPT Queue Misc Interrupt Register
 * These registers contain the per-queue miscellaneous interrupts.
 * cptx_vqx_misc_int_s
 * Word 0
 *  reserved_5_63:59 [63:5] Reserved.
 *  swerr:1 [4:4](R/W1C/H) Software error from engines.
 *  nwrp:1  [3:3](R/W1C/H) NCB result write response error.
 *  irde:1  [2:2](R/W1C/H) Instruction NCB read response error.
 *  dovf:1 [1:1](R/W1C/H) Doorbell overflow.
 *  mbox:1 [0:0](R/W1C/H) PF to VF mailbox interrupt. Set when
 *	CPT()_VF()_PF_MBOX(0) is written.
 *
 */
union cptx_vqx_misc_int {
	u64 u;
	struct cptx_vqx_misc_int_s {
#if defined(__BIG_ENDIAN_BITFIELD) /* Word 0 - Big Endian */
		u64 reserved_5_63:59;
		u64 swerr:1;
		u64 nwrp:1;
		u64 irde:1;
		u64 dovf:1;
		u64 mbox:1;
#else /* Word 0 - Little Endian */
		u64 mbox:1;
		u64 dovf:1;
		u64 irde:1;
		u64 nwrp:1;
		u64 swerr:1;
		u64 reserved_5_63:59;
#endif /* Word 0 - End */
	} s;
};

/**
 * Register (NCB) cpt#_vq#_done_ack
 *
 * CPT Queue Done Count Ack Registers
 * This register is written by software to acknowledge interrupts.
 * cptx_vqx_done_ack_s
 * Word0
 *  reserved_20_63:44 [63:20] Reserved.
 *  done_ack:20 [19:0](R/W/H) Number of decrements to CPT()_VQ()_DONE[DONE].
 *	Reads CPT()_VQ()_DONE[DONE]. Written by software to acknowledge
 *	interrupts. If CPT()_VQ()_DONE[DONE] is still nonzero the interrupt
 *	will be re-sent if the conditions described in CPT()_VQ()_DONE[DONE]
 *	are satisfied.
 *
 */
union cptx_vqx_done_ack {
	u64 u;
	struct cptx_vqx_done_ack_s {
#if defined(__BIG_ENDIAN_BITFIELD) /* Word 0 - Big Endian */
		u64 reserved_20_63:44;
		u64 done_ack:20;
#else /* Word 0 - Little Endian */
		u64 done_ack:20;
		u64 reserved_20_63:44;
#endif /* Word 0 - End */
	} s;
};

/**
 * Register (NCB) cpt#_vq#_done
 *
 * CPT Queue Done Count Registers
 * These registers contain the per-queue instruction done count.
 * cptx_vqx_done_s
 * Word0
 *  reserved_20_63:44 [63:20] Reserved.
 *  done:20 [19:0](R/W/H) Done count. When CPT_INST_S[DONEINT] set and that
 *	instruction completes, CPT()_VQ()_DONE[DONE] is incremented when the
 *	instruction finishes. Write to this field are for diagnostic use only;
 *	instead software writes CPT()_VQ()_DONE_ACK with the number of
 *	decrements for this field.
 *	Interrupts are sent as follows:
 *	* When CPT()_VQ()_DONE[DONE] = 0, then no results are pending, the
 *	interrupt coalescing timer is held to zero, and an interrupt is not
 *	sent.
 *	* When CPT()_VQ()_DONE[DONE] != 0, then the interrupt coalescing timer
 *	counts. If the counter is >= CPT()_VQ()_DONE_WAIT[TIME_WAIT]*1024, or
 *	CPT()_VQ()_DONE[DONE] >= CPT()_VQ()_DONE_WAIT[NUM_WAIT], i.e. enough
 *	time has passed or enough results have arrived, then the interrupt is
 *	sent.
 *	* When CPT()_VQ()_DONE_ACK is written (or CPT()_VQ()_DONE is written
 *	but this is not typical), the interrupt coalescing timer restarts.
 *	Note after decrementing this interrupt equation is recomputed,
 *	for example if CPT()_VQ()_DONE[DONE] >= CPT()_VQ()_DONE_WAIT[NUM_WAIT]
 *	and because the timer is zero, the interrupt will be resent immediately.
 *	(This covers the race case between software acknowledging an interrupt
 *	and a result returning.)
 *	* When CPT()_VQ()_DONE_ENA_W1S[DONE] = 0, interrupts are not sent,
 *	but the counting described above still occurs.
 *	Since CPT instructions complete out-of-order, if software is using
 *	completion interrupts the suggested scheme is to request a DONEINT on
 *	each request, and when an interrupt arrives perform a "greedy" scan for
 *	completions; even if a later command is acknowledged first this will
 *	not result in missing a completion.
 *	Software is responsible for making sure [DONE] does not overflow;
 *	for example by insuring there are not more than 2^20-1 instructions in
 *	flight that may request interrupts.
 *
 */
union cptx_vqx_done {
	u64 u;
	struct cptx_vqx_done_s {
#if defined(__BIG_ENDIAN_BITFIELD) /* Word 0 - Big Endian */
		u64 reserved_20_63:44;
		u64 done:20;
#else /* Word 0 - Little Endian */
		u64 done:20;
		u64 reserved_20_63:44;
#endif /* Word 0 - End */
	} s;
};

/**
 * Register (NCB) cpt#_vq#_done_wait
 *
 * CPT Queue Done Interrupt Coalescing Wait Registers
 * Specifies the per queue interrupt coalescing settings.
 * cptx_vqx_done_wait_s
 * Word0
 *  reserved_48_63:16 [63:48] Reserved.
 *  time_wait:16; [47:32](R/W) Time hold-off. When CPT()_VQ()_DONE[DONE] = 0
 *	or CPT()_VQ()_DONE_ACK is written a timer is cleared. When the timer
 *	reaches [TIME_WAIT]*1024 then interrupt coalescing ends.
 *	see CPT()_VQ()_DONE[DONE]. If 0x0, time coalescing is disabled.
 *  reserved_20_31:12 [31:20] Reserved.
 *  num_wait:20 [19:0](R/W) Number of messages hold-off.
 *	When CPT()_VQ()_DONE[DONE] >= [NUM_WAIT] then interrupt coalescing ends
 *	see CPT()_VQ()_DONE[DONE]. If 0x0, same behavior as 0x1.
 *
 */
union cptx_vqx_done_wait {
	u64 u;
	struct cptx_vqx_done_wait_s {
#if defined(__BIG_ENDIAN_BITFIELD) /* Word 0 - Big Endian */
		u64 reserved_48_63:16;
		u64 time_wait:16;
		u64 reserved_20_31:12;
		u64 num_wait:20;
#else /* Word 0 - Little Endian */
		u64 num_wait:20;
		u64 reserved_20_31:12;
		u64 time_wait:16;
		u64 reserved_48_63:16;
#endif /* Word 0 - End */
	} s;
};

/**
 * Register (NCB) cpt#_vq#_done_ena_w1s
 *
 * CPT Queue Done Interrupt Enable Set Registers
 * Write 1 to these registers will enable the DONEINT interrupt for the queue.
 * cptx_vqx_done_ena_w1s_s
 * Word0
 *  reserved_1_63:63 [63:1] Reserved.
 *  done:1 [0:0](R/W1S/H) Write 1 will enable DONEINT for this queue.
 *	Write 0 has no effect. Read will return the enable bit.
 */
union cptx_vqx_done_ena_w1s {
	u64 u;
	struct cptx_vqx_done_ena_w1s_s {
#if defined(__BIG_ENDIAN_BITFIELD) /* Word 0 - Big Endian */
		u64 reserved_1_63:63;
		u64 done:1;
#else /* Word 0 - Little Endian */
		u64 done:1;
		u64 reserved_1_63:63;
#endif /* Word 0 - End */
	} s;
};

/**
 * Register (NCB) cpt#_vq#_ctl
 *
 * CPT VF Queue Control Registers
 * This register configures queues. This register should be changed (other than
 * clearing [ENA]) only when quiescent (see CPT()_VQ()_INPROG[INFLIGHT]).
 * cptx_vqx_ctl_s
 * Word0
 *  reserved_1_63:63 [63:1] Reserved.
 *  ena:1 [0:0](R/W/H) Enables the logical instruction queue.
 *	See also CPT()_PF_Q()_CTL[CONT_ERR] and	CPT()_VQ()_INPROG[INFLIGHT].
 *	1 = Queue is enabled.
 *	0 = Queue is disabled.
 */
union cptx_vqx_ctl {
	u64 u;
	struct cptx_vqx_ctl_s {
#if defined(__BIG_ENDIAN_BITFIELD) /* Word 0 - Big Endian */
		u64 reserved_1_63:63;
		u64 ena:1;
#else /* Word 0 - Little Endian */
		u64 ena:1;
		u64 reserved_1_63:63;
#endif /* Word 0 - End */
	} s;
};
#endif /*__CPT_HW_TYPES_H*/
