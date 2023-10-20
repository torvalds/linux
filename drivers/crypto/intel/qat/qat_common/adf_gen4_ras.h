/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright(c) 2023 Intel Corporation */
#ifndef ADF_GEN4_RAS_H_
#define ADF_GEN4_RAS_H_

#include <linux/bits.h>

struct adf_ras_ops;

/* ERRSOU0 Correctable error mask*/
#define ADF_GEN4_ERRSOU0_BIT				BIT(0)

/* HI AE Correctable error log */
#define ADF_GEN4_HIAECORERRLOG_CPP0			0x41A308

/* HI AE Correctable error log enable */
#define ADF_GEN4_HIAECORERRLOGENABLE_CPP0		0x41A318
#define ADF_GEN4_ERRSOU1_HIAEUNCERRLOG_CPP0_BIT		BIT(0)
#define ADF_GEN4_ERRSOU1_HICPPAGENTCMDPARERRLOG_BIT	BIT(1)
#define ADF_GEN4_ERRSOU1_RIMEM_PARERR_STS_BIT		BIT(2)
#define ADF_GEN4_ERRSOU1_TIMEM_PARERR_STS_BIT		BIT(3)
#define ADF_GEN4_ERRSOU1_RIMISCSTS_BIT			BIT(4)

#define ADF_GEN4_ERRSOU1_BITMASK ( \
	(ADF_GEN4_ERRSOU1_HIAEUNCERRLOG_CPP0_BIT)	| \
	(ADF_GEN4_ERRSOU1_HICPPAGENTCMDPARERRLOG_BIT)	| \
	(ADF_GEN4_ERRSOU1_RIMEM_PARERR_STS_BIT)	| \
	(ADF_GEN4_ERRSOU1_TIMEM_PARERR_STS_BIT)	| \
	(ADF_GEN4_ERRSOU1_RIMISCSTS_BIT))

/* HI AE Uncorrectable error log */
#define ADF_GEN4_HIAEUNCERRLOG_CPP0			0x41A300

/* HI AE Uncorrectable error log enable */
#define ADF_GEN4_HIAEUNCERRLOGENABLE_CPP0		0x41A320

/* HI CPP Agent Command parity error log */
#define ADF_GEN4_HICPPAGENTCMDPARERRLOG			0x41A310

/* HI CPP Agent Command parity error logging enable */
#define ADF_GEN4_HICPPAGENTCMDPARERRLOGENABLE		0x41A314

/* RI Memory parity error status register */
#define ADF_GEN4_RIMEM_PARERR_STS			0x41B128

/* RI Memory parity error reporting enable */
#define ADF_GEN4_RI_MEM_PAR_ERR_EN0			0x41B12C

/*
 * RI Memory parity error mask
 * BIT(0) - BIT(3) - ri_iosf_pdata_rxq[0:3] parity error
 * BIT(4) - ri_tlq_phdr parity error
 * BIT(5) - ri_tlq_pdata parity error
 * BIT(6) - ri_tlq_nphdr parity error
 * BIT(7) - ri_tlq_npdata parity error
 * BIT(8) - BIT(9) - ri_tlq_cplhdr[0:1] parity error
 * BIT(10) - BIT(17) - ri_tlq_cpldata[0:7] parity error
 * BIT(18) - set this bit to 1 to enable logging status to ri_mem_par_err_sts0
 * BIT(19) - ri_cds_cmd_fifo parity error
 * BIT(20) - ri_obc_ricpl_fifo parity error
 * BIT(21) - ri_obc_tiricpl_fifo parity error
 * BIT(22) - ri_obc_cppcpl_fifo parity error
 * BIT(23) - ri_obc_pendcpl_fifo parity error
 * BIT(24) - ri_cpp_cmd_fifo parity error
 * BIT(25) - ri_cds_ticmd_fifo parity error
 * BIT(26) - riti_cmd_fifo parity error
 * BIT(27) - ri_int_msixtbl parity error
 * BIT(28) - ri_int_imstbl parity error
 * BIT(30) - ri_kpt_fuses parity error
 */
#define ADF_GEN4_RIMEM_PARERR_STS_UNCERR_BITMASK \
	(BIT(0) | BIT(1) | BIT(2) | BIT(3) | BIT(5) | \
	 BIT(7) | BIT(10) | BIT(11) | BIT(12) | BIT(13) | \
	 BIT(14) | BIT(15) | BIT(16) | BIT(17) | BIT(18) | BIT(19) | \
	 BIT(20) | BIT(21) | BIT(22) | BIT(23) | BIT(24) | BIT(25) | \
	 BIT(26) | BIT(27) | BIT(28) | BIT(30))

#define ADF_GEN4_RIMEM_PARERR_STS_FATAL_BITMASK \
	(BIT(4) | BIT(6) | BIT(8) | BIT(9))

/* TI CI parity status */
#define ADF_GEN4_TI_CI_PAR_STS				0x50060C

/* TI CI parity reporting mask */
#define ADF_GEN4_TI_CI_PAR_ERR_MASK			0x500608

/*
 * TI CI parity status mask
 * BIT(0) - CdCmdQ_sts patiry error status
 * BIT(1) - CdDataQ_sts parity error status
 * BIT(3) - CPP_SkidQ_sts parity error status
 * BIT(7) - CPP_SkidQ_sc_sts parity error status
 */
#define ADF_GEN4_TI_CI_PAR_STS_BITMASK \
	(BIT(0) | BIT(1) | BIT(3) | BIT(7))

/* TI PULLFUB parity status */
#define ADF_GEN4_TI_PULL0FUB_PAR_STS			0x500618

/* TI PULLFUB parity error reporting mask */
#define ADF_GEN4_TI_PULL0FUB_PAR_ERR_MASK		0x500614

/*
 * TI PULLFUB parity status mask
 * BIT(0) - TrnPullReqQ_sts parity status
 * BIT(1) - TrnSharedDataQ_sts parity status
 * BIT(2) - TrnPullReqDataQ_sts parity status
 * BIT(4) - CPP_CiPullReqQ_sts parity status
 * BIT(5) - CPP_TrnPullReqQ_sts parity status
 * BIT(6) - CPP_PullidQ_sts parity status
 * BIT(7) - CPP_WaitDataQ_sts parity status
 * BIT(8) - CPP_CdDataQ_sts parity status
 * BIT(9) - CPP_TrnDataQP0_sts parity status
 * BIT(10) - BIT(11) - CPP_TrnDataQRF[00:01]_sts parity status
 * BIT(12) - CPP_TrnDataQP1_sts parity status
 * BIT(13) - BIT(14) - CPP_TrnDataQRF[10:11]_sts parity status
 */
#define ADF_GEN4_TI_PULL0FUB_PAR_STS_BITMASK \
	(BIT(0) | BIT(1) | BIT(2) | BIT(4) | BIT(5) | BIT(6) | BIT(7) | \
	 BIT(8) | BIT(9) | BIT(10) | BIT(11) | BIT(12) | BIT(13) | BIT(14))

/* TI PUSHUB parity status */
#define ADF_GEN4_TI_PUSHFUB_PAR_STS			0x500630

/* TI PUSHFUB parity error reporting mask */
#define ADF_GEN4_TI_PUSHFUB_PAR_ERR_MASK		0x50062C

/*
 * TI PUSHUB parity status mask
 * BIT(0) - SbPushReqQ_sts parity status
 * BIT(1) - BIT(2) - SbPushDataQ[0:1]_sts parity status
 * BIT(4) - CPP_CdPushReqQ_sts parity status
 * BIT(5) - BIT(6) - CPP_CdPushDataQ[0:1]_sts parity status
 * BIT(7) - CPP_SbPushReqQ_sts parity status
 * BIT(8) - CPP_SbPushDataQP_sts parity status
 * BIT(9) - BIT(10) - CPP_SbPushDataQRF[0:1]_sts parity status
 */
#define ADF_GEN4_TI_PUSHFUB_PAR_STS_BITMASK \
	(BIT(0) | BIT(1) | BIT(2) | BIT(4) | BIT(5) | \
	 BIT(6) | BIT(7) | BIT(8) | BIT(9) | BIT(10))

/* TI CD parity status */
#define ADF_GEN4_TI_CD_PAR_STS				0x50063C

/* TI CD parity error mask */
#define ADF_GEN4_TI_CD_PAR_ERR_MASK			0x500638

/*
 * TI CD parity status mask
 * BIT(0) - BIT(15) - CtxMdRam[0:15]_sts parity status
 * BIT(16) - Leaf2ClusterRam_sts parity status
 * BIT(17) - BIT(18) - Ring2LeafRam[0:1]_sts parity status
 * BIT(19) - VirtualQ_sts parity status
 * BIT(20) - DtRdQ_sts parity status
 * BIT(21) - DtWrQ_sts parity status
 * BIT(22) - RiCmdQ_sts parity status
 * BIT(23) - BypassQ_sts parity status
 * BIT(24) - DtRdQ_sc_sts parity status
 * BIT(25) - DtWrQ_sc_sts parity status
 */
#define ADF_GEN4_TI_CD_PAR_STS_BITMASK \
	(BIT(0) | BIT(1) | BIT(2) | BIT(3) | BIT(4) | BIT(5) | BIT(6) | \
	 BIT(7) | BIT(8) | BIT(9) | BIT(10) | BIT(11) | BIT(12) | BIT(13) | \
	 BIT(14) | BIT(15) | BIT(16) | BIT(17) | BIT(18) | BIT(19) | BIT(20) | \
	 BIT(21) | BIT(22) | BIT(23) | BIT(24) | BIT(25))

/* TI TRNSB parity status */
#define ADF_GEN4_TI_TRNSB_PAR_STS			0x500648

/* TI TRNSB Parity error reporting mask */
#define ADF_GEN4_TI_TRNSB_PAR_ERR_MASK			0x500644

/*
 * TI TRNSB parity status mask
 * BIT(0) - TrnPHdrQP_sts parity status
 * BIT(1) - TrnPHdrQRF_sts parity status
 * BIT(2) - TrnPDataQP_sts parity status
 * BIT(3) - BIT(6) - TrnPDataQRF[0:3]_sts parity status
 * BIT(7) - TrnNpHdrQP_sts parity status
 * BIT(8) - BIT(9) - TrnNpHdrQRF[0:1]_sts parity status
 * BIT(10) - TrnCplHdrQ_sts parity status
 * BIT(11) - TrnPutObsReqQ_sts parity status
 * BIT(12) - TrnPushReqQ_sts parity status
 * BIT(13) - SbSplitIdRam_sts parity status
 * BIT(14) - SbReqCountQ_sts parity status
 * BIT(15) - SbCplTrkRam_sts parity status
 * BIT(16) - SbGetObsReqQ_sts parity status
 * BIT(17) - SbEpochIdQ_sts parity status
 * BIT(18) - SbAtCplHdrQ_sts parity status
 * BIT(19) - SbAtCplDataQ_sts parity status
 * BIT(20) - SbReqCountRam_sts parity status
 * BIT(21) - SbAtCplHdrQ_sc_sts parity status
 */
#define ADF_GEN4_TI_TRNSB_PAR_STS_BITMASK \
	(BIT(0) | BIT(1) | BIT(2) | BIT(3) | BIT(4) | BIT(5) | BIT(6) | \
	 BIT(7) | BIT(8) | BIT(9) | BIT(10) | BIT(11) | BIT(12) | \
	 BIT(13) | BIT(14) | BIT(15) | BIT(16) | BIT(17) | BIT(18) | \
	 BIT(19) | BIT(20) | BIT(21))

/* Status register to log misc error on RI */
#define ADF_GEN4_RIMISCSTS				0x41B1B8

/* Status control register to log misc RI error */
#define ADF_GEN4_RIMISCCTL				0x41B1BC

/* Command Parity error detected on IOSFP Command to QAT */
#define ADF_GEN4_RIMISCSTS_BIT				BIT(0)

void adf_gen4_init_ras_ops(struct adf_ras_ops *ras_ops);

#endif /* ADF_GEN4_RAS_H_ */
