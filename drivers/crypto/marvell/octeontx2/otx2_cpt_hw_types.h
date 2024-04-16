/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2020 Marvell.
 */

#ifndef __OTX2_CPT_HW_TYPES_H
#define __OTX2_CPT_HW_TYPES_H

#include <linux/types.h>

/* Device IDs */
#define OTX2_CPT_PCI_PF_DEVICE_ID 0xA0FD
#define OTX2_CPT_PCI_VF_DEVICE_ID 0xA0FE
#define CN10K_CPT_PCI_PF_DEVICE_ID 0xA0F2
#define CN10K_CPT_PCI_VF_DEVICE_ID 0xA0F3

/* Mailbox interrupts offset */
#define OTX2_CPT_PF_MBOX_INT	6
#define OTX2_CPT_PF_INT_VEC_E_MBOXX(x, a) ((x) + (a))

/* Maximum supported microcode groups */
#define OTX2_CPT_MAX_ENGINE_GROUPS 8

/* CPT instruction size in bytes */
#define OTX2_CPT_INST_SIZE	64
/*
 * CPT VF MSIX vectors and their offsets
 */
#define OTX2_CPT_VF_MSIX_VECTORS 1
#define OTX2_CPT_VF_INTR_MBOX_MASK BIT(0)
#define CN10K_CPT_VF_MBOX_REGION  (0xC0000)

/* CPT LF MSIX vectors */
#define OTX2_CPT_LF_MSIX_VECTORS 2

/* OcteonTX2 CPT PF registers */
#define OTX2_CPT_PF_CONSTANTS           (0x0)
#define OTX2_CPT_PF_RESET               (0x100)
#define OTX2_CPT_PF_DIAG                (0x120)
#define OTX2_CPT_PF_BIST_STATUS         (0x160)
#define OTX2_CPT_PF_ECC0_CTL            (0x200)
#define OTX2_CPT_PF_ECC0_FLIP           (0x210)
#define OTX2_CPT_PF_ECC0_INT            (0x220)
#define OTX2_CPT_PF_ECC0_INT_W1S        (0x230)
#define OTX2_CPT_PF_ECC0_ENA_W1S        (0x240)
#define OTX2_CPT_PF_ECC0_ENA_W1C        (0x250)
#define OTX2_CPT_PF_MBOX_INTX(b)        (0x400 | (b) << 3)
#define OTX2_CPT_PF_MBOX_INT_W1SX(b)    (0x420 | (b) << 3)
#define OTX2_CPT_PF_MBOX_ENA_W1CX(b)    (0x440 | (b) << 3)
#define OTX2_CPT_PF_MBOX_ENA_W1SX(b)    (0x460 | (b) << 3)
#define OTX2_CPT_PF_EXEC_INT            (0x500)
#define OTX2_CPT_PF_EXEC_INT_W1S        (0x520)
#define OTX2_CPT_PF_EXEC_ENA_W1C        (0x540)
#define OTX2_CPT_PF_EXEC_ENA_W1S        (0x560)
#define OTX2_CPT_PF_GX_EN(b)            (0x600 | (b) << 3)
#define OTX2_CPT_PF_EXEC_INFO           (0x700)
#define OTX2_CPT_PF_EXEC_BUSY           (0x800)
#define OTX2_CPT_PF_EXEC_INFO0          (0x900)
#define OTX2_CPT_PF_EXEC_INFO1          (0x910)
#define OTX2_CPT_PF_INST_REQ_PC         (0x10000)
#define OTX2_CPT_PF_INST_LATENCY_PC     (0x10020)
#define OTX2_CPT_PF_RD_REQ_PC           (0x10040)
#define OTX2_CPT_PF_RD_LATENCY_PC       (0x10060)
#define OTX2_CPT_PF_RD_UC_PC            (0x10080)
#define OTX2_CPT_PF_ACTIVE_CYCLES_PC    (0x10100)
#define OTX2_CPT_PF_EXE_CTL             (0x4000000)
#define OTX2_CPT_PF_EXE_STATUS          (0x4000008)
#define OTX2_CPT_PF_EXE_CLK             (0x4000010)
#define OTX2_CPT_PF_EXE_DBG_CTL         (0x4000018)
#define OTX2_CPT_PF_EXE_DBG_DATA        (0x4000020)
#define OTX2_CPT_PF_EXE_BIST_STATUS     (0x4000028)
#define OTX2_CPT_PF_EXE_REQ_TIMER       (0x4000030)
#define OTX2_CPT_PF_EXE_MEM_CTL         (0x4000038)
#define OTX2_CPT_PF_EXE_PERF_CTL        (0x4001000)
#define OTX2_CPT_PF_EXE_DBG_CNTX(b)     (0x4001100 | (b) << 3)
#define OTX2_CPT_PF_EXE_PERF_EVENT_CNT  (0x4001180)
#define OTX2_CPT_PF_EXE_EPCI_INBX_CNT(b)  (0x4001200 | (b) << 3)
#define OTX2_CPT_PF_EXE_EPCI_OUTBX_CNT(b) (0x4001240 | (b) << 3)
#define OTX2_CPT_PF_ENGX_UCODE_BASE(b)  (0x4002000 | (b) << 3)
#define OTX2_CPT_PF_QX_CTL(b)           (0x8000000 | (b) << 20)
#define OTX2_CPT_PF_QX_GMCTL(b)         (0x8000020 | (b) << 20)
#define OTX2_CPT_PF_QX_CTL2(b)          (0x8000100 | (b) << 20)
#define OTX2_CPT_PF_VFX_MBOXX(b, c)     (0x8001000 | (b) << 20 | \
					 (c) << 8)

/* OcteonTX2 CPT LF registers */
#define OTX2_CPT_LF_CTL                 (0x10)
#define OTX2_CPT_LF_DONE_WAIT           (0x30)
#define OTX2_CPT_LF_INPROG              (0x40)
#define OTX2_CPT_LF_DONE                (0x50)
#define OTX2_CPT_LF_DONE_ACK            (0x60)
#define OTX2_CPT_LF_DONE_INT_ENA_W1S    (0x90)
#define OTX2_CPT_LF_DONE_INT_ENA_W1C    (0xa0)
#define OTX2_CPT_LF_MISC_INT            (0xb0)
#define OTX2_CPT_LF_MISC_INT_W1S        (0xc0)
#define OTX2_CPT_LF_MISC_INT_ENA_W1S    (0xd0)
#define OTX2_CPT_LF_MISC_INT_ENA_W1C    (0xe0)
#define OTX2_CPT_LF_Q_BASE              (0xf0)
#define OTX2_CPT_LF_Q_SIZE              (0x100)
#define OTX2_CPT_LF_Q_INST_PTR          (0x110)
#define OTX2_CPT_LF_Q_GRP_PTR           (0x120)
#define OTX2_CPT_LF_NQX(a)              (0x400 | (a) << 3)
#define OTX2_CPT_RVU_FUNC_BLKADDR_SHIFT 20
/* LMT LF registers */
#define OTX2_CPT_LMT_LFBASE             BIT_ULL(OTX2_CPT_RVU_FUNC_BLKADDR_SHIFT)
#define OTX2_CPT_LMT_LF_LMTLINEX(a)     (OTX2_CPT_LMT_LFBASE | 0x000 | \
					 (a) << 12)
/* RVU VF registers */
#define OTX2_RVU_VF_INT                 (0x20)
#define OTX2_RVU_VF_INT_W1S             (0x28)
#define OTX2_RVU_VF_INT_ENA_W1S         (0x30)
#define OTX2_RVU_VF_INT_ENA_W1C         (0x38)

/*
 * Enumeration otx2_cpt_ucode_error_code_e
 *
 * Enumerates ucode errors
 */
enum otx2_cpt_ucode_comp_code_e {
	OTX2_CPT_UCC_SUCCESS = 0x00,
	OTX2_CPT_UCC_INVALID_OPCODE = 0x01,

	/* Scatter gather */
	OTX2_CPT_UCC_SG_WRITE_LENGTH = 0x02,
	OTX2_CPT_UCC_SG_LIST = 0x03,
	OTX2_CPT_UCC_SG_NOT_SUPPORTED = 0x04,

};

/*
 * Enumeration otx2_cpt_comp_e
 *
 * OcteonTX2 CPT Completion Enumeration
 * Enumerates the values of CPT_RES_S[COMPCODE].
 */
enum otx2_cpt_comp_e {
	OTX2_CPT_COMP_E_NOTDONE = 0x00,
	OTX2_CPT_COMP_E_GOOD = 0x01,
	OTX2_CPT_COMP_E_FAULT = 0x02,
	OTX2_CPT_COMP_E_HWERR = 0x04,
	OTX2_CPT_COMP_E_INSTERR = 0x05,
	OTX2_CPT_COMP_E_WARN = 0x06
};

/*
 * Enumeration otx2_cpt_vf_int_vec_e
 *
 * OcteonTX2 CPT VF MSI-X Vector Enumeration
 * Enumerates the MSI-X interrupt vectors.
 */
enum otx2_cpt_vf_int_vec_e {
	OTX2_CPT_VF_INT_VEC_E_MBOX = 0x00
};

/*
 * Enumeration otx2_cpt_lf_int_vec_e
 *
 * OcteonTX2 CPT LF MSI-X Vector Enumeration
 * Enumerates the MSI-X interrupt vectors.
 */
enum otx2_cpt_lf_int_vec_e {
	OTX2_CPT_LF_INT_VEC_E_MISC = 0x00,
	OTX2_CPT_LF_INT_VEC_E_DONE = 0x01
};

/*
 * Structure otx2_cpt_inst_s
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
union otx2_cpt_inst_s {
	u64 u[8];

	struct {
		/* Word 0 */
		u64 nixtxl:3;
		u64 doneint:1;
		u64 nixtx_addr:60;
		/* Word 1 */
		u64 res_addr;
		/* Word 2 */
		u64 tag:32;
		u64 tt:2;
		u64 grp:10;
		u64 reserved_172_175:4;
		u64 rvu_pf_func:16;
		/* Word 3 */
		u64 qord:1;
		u64 reserved_194_193:2;
		u64 wq_ptr:61;
		/* Word 4 */
		u64 ei0;
		/* Word 5 */
		u64 ei1;
		/* Word 6 */
		u64 ei2;
		/* Word 7 */
		u64 ei3;
	} s;
};

/*
 * Structure otx2_cpt_res_s
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
union otx2_cpt_res_s {
	u64 u[2];

	struct cn9k_cpt_res_s {
		u64 compcode:8;
		u64 uc_compcode:8;
		u64 doneint:1;
		u64 reserved_17_63:47;
		u64 reserved_64_127;
	} s;

	struct cn10k_cpt_res_s {
		u64 compcode:7;
		u64 doneint:1;
		u64 uc_compcode:8;
		u64 rlen:16;
		u64 spi:32;
		u64 esn;
	} cn10k;
};

/*
 * Register (RVU_PF_BAR0) cpt#_af_constants1
 *
 * CPT AF Constants Register
 * This register contains implementation-related parameters of CPT.
 */
union otx2_cptx_af_constants1 {
	u64 u;
	struct otx2_cptx_af_constants1_s {
		u64 se:16;
		u64 ie:16;
		u64 ae:16;
		u64 reserved_48_63:16;
	} s;
};

/*
 * RVU_PFVF_BAR2 - cpt_lf_misc_int
 *
 * This register contain the per-queue miscellaneous interrupts.
 *
 */
union otx2_cptx_lf_misc_int {
	u64 u;
	struct otx2_cptx_lf_misc_int_s {
		u64 reserved_0:1;
		u64 nqerr:1;
		u64 irde:1;
		u64 nwrp:1;
		u64 reserved_4:1;
		u64 hwerr:1;
		u64 fault:1;
		u64 reserved_7_63:57;
	} s;
};

/*
 * RVU_PFVF_BAR2 - cpt_lf_misc_int_ena_w1s
 *
 * This register sets interrupt enable bits.
 *
 */
union otx2_cptx_lf_misc_int_ena_w1s {
	u64 u;
	struct otx2_cptx_lf_misc_int_ena_w1s_s {
		u64 reserved_0:1;
		u64 nqerr:1;
		u64 irde:1;
		u64 nwrp:1;
		u64 reserved_4:1;
		u64 hwerr:1;
		u64 fault:1;
		u64 reserved_7_63:57;
	} s;
};

/*
 * RVU_PFVF_BAR2 - cpt_lf_ctl
 *
 * This register configures the queue.
 *
 * When the queue is not execution-quiescent (see CPT_LF_INPROG[EENA,INFLIGHT]),
 * software must only write this register with [ENA]=0.
 */
union otx2_cptx_lf_ctl {
	u64 u;
	struct otx2_cptx_lf_ctl_s {
		u64 ena:1;
		u64 fc_ena:1;
		u64 fc_up_crossing:1;
		u64 reserved_3:1;
		u64 fc_hyst_bits:4;
		u64 reserved_8_63:56;
	} s;
};

/*
 * RVU_PFVF_BAR2 - cpt_lf_done_wait
 *
 * This register specifies the per-queue interrupt coalescing settings.
 */
union otx2_cptx_lf_done_wait {
	u64 u;
	struct otx2_cptx_lf_done_wait_s {
		u64 num_wait:20;
		u64 reserved_20_31:12;
		u64 time_wait:16;
		u64 reserved_48_63:16;
	} s;
};

/*
 * RVU_PFVF_BAR2 - cpt_lf_done
 *
 * This register contain the per-queue instruction done count.
 */
union otx2_cptx_lf_done {
	u64 u;
	struct otx2_cptx_lf_done_s {
		u64 done:20;
		u64 reserved_20_63:44;
	} s;
};

/*
 * RVU_PFVF_BAR2 - cpt_lf_inprog
 *
 * These registers contain the per-queue instruction in flight registers.
 *
 */
union otx2_cptx_lf_inprog {
	u64 u;
	struct otx2_cptx_lf_inprog_s {
		u64 inflight:9;
		u64 reserved_9_15:7;
		u64 eena:1;
		u64 grp_drp:1;
		u64 reserved_18_30:13;
		u64 grb_partial:1;
		u64 grb_cnt:8;
		u64 gwb_cnt:8;
		u64 reserved_48_63:16;
	} s;
};

/*
 * RVU_PFVF_BAR2 - cpt_lf_q_base
 *
 * CPT initializes these CSR fields to these values on any CPT_LF_Q_BASE write:
 * _ CPT_LF_Q_INST_PTR[XQ_XOR]=0.
 * _ CPT_LF_Q_INST_PTR[NQ_PTR]=2.
 * _ CPT_LF_Q_INST_PTR[DQ_PTR]=2.
 * _ CPT_LF_Q_GRP_PTR[XQ_XOR]=0.
 * _ CPT_LF_Q_GRP_PTR[NQ_PTR]=1.
 * _ CPT_LF_Q_GRP_PTR[DQ_PTR]=1.
 */
union otx2_cptx_lf_q_base {
	u64 u;
	struct otx2_cptx_lf_q_base_s {
		u64 fault:1;
		u64 reserved_1_6:6;
		u64 addr:46;
		u64 reserved_53_63:11;
	} s;
};

/*
 * RVU_PFVF_BAR2 - cpt_lf_q_size
 *
 * CPT initializes these CSR fields to these values on any CPT_LF_Q_SIZE write:
 * _ CPT_LF_Q_INST_PTR[XQ_XOR]=0.
 * _ CPT_LF_Q_INST_PTR[NQ_PTR]=2.
 * _ CPT_LF_Q_INST_PTR[DQ_PTR]=2.
 * _ CPT_LF_Q_GRP_PTR[XQ_XOR]=0.
 * _ CPT_LF_Q_GRP_PTR[NQ_PTR]=1.
 * _ CPT_LF_Q_GRP_PTR[DQ_PTR]=1.
 */
union otx2_cptx_lf_q_size {
	u64 u;
	struct otx2_cptx_lf_q_size_s {
		u64 size_div40:15;
		u64 reserved_15_63:49;
	} s;
};

/*
 * RVU_PF_BAR0 - cpt_af_lf_ctl
 *
 * This register configures queues. This register should be written only
 * when the queue is execution-quiescent (see CPT_LF_INPROG[INFLIGHT]).
 */
union otx2_cptx_af_lf_ctrl {
	u64 u;
	struct otx2_cptx_af_lf_ctrl_s {
		u64 pri:1;
		u64 reserved_1_8:8;
		u64 pf_func_inst:1;
		u64 cont_err:1;
		u64 reserved_11_15:5;
		u64 nixtx_en:1;
		u64 reserved_17_47:31;
		u64 grp:8;
		u64 reserved_56_63:8;
	} s;
};

#endif /* __OTX2_CPT_HW_TYPES_H */
