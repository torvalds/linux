/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright(c) 2025 Intel Corporation */
#ifndef ADF_GEN6_RAS_H_
#define ADF_GEN6_RAS_H_

#include <linux/bits.h>

struct adf_ras_ops;

/* Error source registers */
#define ADF_GEN6_ERRSOU0	0x41A200
#define ADF_GEN6_ERRSOU1	0x41A204
#define ADF_GEN6_ERRSOU2	0x41A208
#define ADF_GEN6_ERRSOU3	0x41A20C

/* Error source mask registers */
#define ADF_GEN6_ERRMSK0	0x41A210
#define ADF_GEN6_ERRMSK1	0x41A214
#define ADF_GEN6_ERRMSK2	0x41A218
#define ADF_GEN6_ERRMSK3	0x41A21C

/* ERRSOU0 Correctable error mask */
#define ADF_GEN6_ERRSOU0_MASK				BIT(0)

#define ADF_GEN6_ERRSOU1_CPP0_MEUNC_BIT			BIT(0)
#define ADF_GEN6_ERRSOU1_CPP_CMDPARERR_BIT		BIT(1)
#define ADF_GEN6_ERRSOU1_RIMEM_PARERR_STS_BIT		BIT(2)
#define ADF_GEN6_ERRSOU1_TIMEM_PARERR_STS_BIT		BIT(3)
#define ADF_GEN6_ERRSOU1_SFICMD_PARERR_BIT	        BIT(4)

#define ADF_GEN6_ERRSOU1_MASK ( \
	(ADF_GEN6_ERRSOU1_CPP0_MEUNC_BIT)	| \
	(ADF_GEN6_ERRSOU1_CPP_CMDPARERR_BIT)	| \
	(ADF_GEN6_ERRSOU1_RIMEM_PARERR_STS_BIT)	| \
	(ADF_GEN6_ERRSOU1_TIMEM_PARERR_STS_BIT)	| \
	(ADF_GEN6_ERRSOU1_SFICMD_PARERR_BIT))

#define ADF_GEN6_ERRMSK1_CPP0_MEUNC_BIT			BIT(0)
#define ADF_GEN6_ERRMSK1_CPP_CMDPARERR_BIT		BIT(1)
#define ADF_GEN6_ERRMSK1_RIMEM_PARERR_STS_BIT		BIT(2)
#define ADF_GEN6_ERRMSK1_TIMEM_PARERR_STS_BIT		BIT(3)
#define ADF_GEN6_ERRMSK1_IOSFCMD_PARERR_BIT	        BIT(4)

#define ADF_GEN6_ERRMSK1_MASK ( \
	(ADF_GEN6_ERRMSK1_CPP0_MEUNC_BIT)	| \
	(ADF_GEN6_ERRMSK1_CPP_CMDPARERR_BIT)	| \
	(ADF_GEN6_ERRMSK1_RIMEM_PARERR_STS_BIT)	| \
	(ADF_GEN6_ERRMSK1_TIMEM_PARERR_STS_BIT)	| \
	(ADF_GEN6_ERRMSK1_IOSFCMD_PARERR_BIT))

/* HI AE Uncorrectable error log */
#define ADF_GEN6_HIAEUNCERRLOG_CPP0			0x41A300

/* HI AE Uncorrectable error log enable */
#define ADF_GEN6_HIAEUNCERRLOGENABLE_CPP0		0x41A320

/* HI AE Correctable error log */
#define ADF_GEN6_HIAECORERRLOG_CPP0			0x41A308

/* HI AE Correctable error log enable */
#define ADF_GEN6_HIAECORERRLOGENABLE_CPP0		0x41A318

/* HI CPP Agent Command parity error log */
#define ADF_GEN6_HICPPAGENTCMDPARERRLOG			0x41A310

/* HI CPP Agent command parity error logging enable */
#define ADF_GEN6_HICPPAGENTCMDPARERRLOGENABLE		0x41A314

#define ADF_6XXX_HICPPAGENTCMDPARERRLOG_MASK		0x1B

/* RI Memory parity error status register */
#define ADF_GEN6_RIMEM_PARERR_STS			0x41B128

/* RI Memory parity error reporting enable */
#define ADF_GEN6_RI_MEM_PAR_ERR_EN0			0x41B12C

/*
 * RI Memory parity error mask
 * BIT(4) - ri_tlq_phdr parity error
 * BIT(5) - ri_tlq_pdata parity error
 * BIT(6) - ri_tlq_nphdr parity error
 * BIT(7) - ri_tlq_npdata parity error
 * BIT(8) - ri_tlq_cplhdr parity error
 * BIT(10) - BIT(13) - ri_tlq_cpldata[0:3] parity error
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
#define ADF_GEN6_RIMEM_PARERR_FATAL_MASK \
	(BIT(0) | BIT(1) | BIT(2) | BIT(4) | BIT(5) | BIT(6) | \
	 BIT(7) | BIT(8) | BIT(18) | BIT(19) | BIT(20) | BIT(21) | \
	 BIT(22) | BIT(23) | BIT(24) | BIT(25) | BIT(26) | BIT(27) | \
	 BIT(28) | BIT(30))

#define ADF_GEN6_RIMEM_PARERR_CERR_MASK \
	(BIT(10) | BIT(11) | BIT(12) | BIT(13))

/* TI CI parity status */
#define ADF_GEN6_TI_CI_PAR_STS				0x50060C

/* TI CI parity reporting mask */
#define ADF_GEN6_TI_CI_PAR_ERR_MASK			0x500608

/*
 * TI CI parity status mask
 * BIT(0) - CdCmdQ_sts patiry error status
 * BIT(1) - CdDataQ_sts parity error status
 * BIT(3) - CPP_SkidQ_sts parity error status
 */
#define ADF_GEN6_TI_CI_PAR_STS_MASK \
	(BIT(0) | BIT(1) | BIT(3))

/* TI PULLFUB parity status */
#define ADF_GEN6_TI_PULL0FUB_PAR_STS			0x500618

/* TI PULLFUB parity error reporting mask */
#define ADF_GEN6_TI_PULL0FUB_PAR_ERR_MASK		0x500614

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
#define ADF_GEN6_TI_PULL0FUB_PAR_STS_MASK \
	(BIT(0) | BIT(1) | BIT(2) | BIT(4) | BIT(5) | BIT(6) | BIT(7) | \
	 BIT(8) | BIT(9) | BIT(10) | BIT(11) | BIT(12) | BIT(13) | BIT(14))

/* TI PUSHUB parity status */
#define ADF_GEN6_TI_PUSHFUB_PAR_STS			0x500630

/* TI PUSHFUB parity error reporting mask */
#define ADF_GEN6_TI_PUSHFUB_PAR_ERR_MASK		0x50062C

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
#define ADF_GEN6_TI_PUSHFUB_PAR_STS_MASK \
	(BIT(0) | BIT(1) | BIT(2) | BIT(4) | BIT(5) | \
	 BIT(6) | BIT(7) | BIT(8) | BIT(9) | BIT(10))

/* TI CD parity status */
#define ADF_GEN6_TI_CD_PAR_STS				0x50063C

/* TI CD parity error mask */
#define ADF_GEN6_TI_CD_PAR_ERR_MASK			0x500638

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
#define ADF_GEN6_TI_CD_PAR_STS_MASK \
	(BIT(0) | BIT(1) | BIT(2) | BIT(3) | BIT(4) | BIT(5) | BIT(6) | \
	 BIT(7) | BIT(8) | BIT(9) | BIT(10) | BIT(11) | BIT(12) | BIT(13) | \
	 BIT(14) | BIT(15) | BIT(16) | BIT(17) | BIT(18) | BIT(19) | BIT(20) | \
	 BIT(21) | BIT(22) | BIT(23) | BIT(24) | BIT(25))

/* TI TRNSB parity status */
#define ADF_GEN6_TI_TRNSB_PAR_STS			0x500648

/* TI TRNSB parity error reporting mask */
#define ADF_GEN6_TI_TRNSB_PAR_ERR_MASK			0x500644

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
#define ADF_GEN6_TI_TRNSB_PAR_STS_MASK \
	(BIT(0) | BIT(1) | BIT(2) | BIT(3) | BIT(4) | BIT(5) | BIT(6) | \
	 BIT(7) | BIT(8) | BIT(9) | BIT(10) | BIT(11) | BIT(12) | \
	 BIT(13) | BIT(14) | BIT(15) | BIT(16) | BIT(17) | BIT(18) | \
	 BIT(19) | BIT(20) | BIT(21))

/* Status register to log misc error on RI */
#define ADF_GEN6_RIMISCSTS				0x41B1B8

/* Status control register to log misc RI error */
#define ADF_GEN6_RIMISCCTL				0x41B1BC

/*
 * ERRSOU2 bit mask
 * BIT(0) - SSM Interrupt Mask
 * BIT(1) - CFC on CPP. ORed of CFC Push error and Pull error
 * BIT(2) - BIT(4) - CPP attention interrupts
 * BIT(18) - PM interrupt
 */
#define ADF_GEN6_ERRSOU2_SSM_ERR_BIT			BIT(0)
#define ADF_GEN6_ERRSOU2_CPP_CFC_ERR_STATUS_BIT	BIT(1)
#define ADF_GEN6_ERRSOU2_CPP_CFC_ATT_INT_MASK \
	(BIT(2) | BIT(3) | BIT(4))

#define ADF_GEN6_ERRSOU2_PM_INT_BIT			BIT(18)

#define ADF_GEN6_ERRSOU2_MASK \
	(ADF_GEN6_ERRSOU2_SSM_ERR_BIT | \
	 ADF_GEN6_ERRSOU2_CPP_CFC_ERR_STATUS_BIT)

#define ADF_GEN6_ERRSOU2_DIS_MASK \
	(ADF_GEN6_ERRSOU2_SSM_ERR_BIT | \
	 ADF_GEN6_ERRSOU2_CPP_CFC_ERR_STATUS_BIT | \
	 ADF_GEN6_ERRSOU2_CPP_CFC_ATT_INT_MASK)

#define ADF_GEN6_IAINTSTATSSM				0x28

/* IAINTSTATSSM error bit mask definitions */
#define ADF_GEN6_IAINTSTATSSM_SH_ERR_BIT		BIT(0)
#define ADF_GEN6_IAINTSTATSSM_PPERR_BIT			BIT(2)
#define ADF_GEN6_IAINTSTATSSM_SCMPAR_ERR_BIT		BIT(4)
#define ADF_GEN6_IAINTSTATSSM_CPPPAR_ERR_BIT		BIT(5)
#define ADF_GEN6_IAINTSTATSSM_RFPAR_ERR_BIT		BIT(6)
#define ADF_GEN6_IAINTSTATSSM_UNEXP_CPL_ERR_BIT		BIT(7)

#define ADF_GEN6_IAINTSTATSSM_MASK \
	(ADF_GEN6_IAINTSTATSSM_SH_ERR_BIT | \
	 ADF_GEN6_IAINTSTATSSM_PPERR_BIT | \
	 ADF_GEN6_IAINTSTATSSM_SCMPAR_ERR_BIT | \
	 ADF_GEN6_IAINTSTATSSM_CPPPAR_ERR_BIT | \
	 ADF_GEN6_IAINTSTATSSM_RFPAR_ERR_BIT | \
	 ADF_GEN6_IAINTSTATSSM_UNEXP_CPL_ERR_BIT)

#define ADF_GEN6_UERRSSMSH				0x18

/*
 * UERRSSMSH error bit mask definitions
 *
 * BIT(0) - Indicates one uncorrectable error
 * BIT(15) - Indicates multiple uncorrectable errors
 *	     in device shared memory
 */
#define ADF_GEN6_UERRSSMSH_MASK			(BIT(0) | BIT(15))

/*
 * CERRSSMSH error bit
 * BIT(0) - Indicates one correctable error
 */
#define ADF_GEN6_CERRSSMSH_ERROR_BIT			(BIT(0) | BIT(15) | BIT(24))
#define ADF_GEN6_CERRSSMSH				0x10

#define ADF_GEN6_INTMASKSSM				0x0

/*
 * Error reporting mask in INTMASKSSM
 * BIT(0) - Shared memory uncorrectable interrupt mask
 * BIT(2) - PPERR interrupt mask
 * BIT(4) - SCM parity error interrupt mask
 * BIT(5) - CPP parity error interrupt mask
 * BIT(6) - SHRAM RF parity error interrupt mask
 * BIT(7) - AXI unexpected completion error mask
 */
#define ADF_GEN6_INTMASKSSM_MASK	\
	(BIT(0) | BIT(2) | BIT(4) | BIT(5) | BIT(6) | BIT(7))

/* CPP push or pull error */
#define ADF_GEN6_PPERR					0x8

#define ADF_GEN6_PPERR_MASK				(BIT(0) | BIT(1))

/*
 * SSM_FERR_STATUS error bit mask definitions
 */
#define ADF_GEN6_SCM_PAR_ERR_MASK			BIT(5)
#define ADF_GEN6_CPP_PAR_ERR_MASK			(BIT(0) | BIT(1) | BIT(2))
#define ADF_GEN6_UNEXP_CPL_ERR_MASK			(BIT(3) | BIT(4) | BIT(10) | BIT(11))
#define ADF_GEN6_RF_PAR_ERR_MASK			BIT(16)

#define ADF_GEN6_SSM_FERR_STATUS			0x9C

#define ADF_GEN6_CPP_CFC_ERR_STATUS			0x640C04

/*
 * BIT(0) - Indicates one or more CPP CFC errors
 * BIT(1) - Indicates multiple CPP CFC errors
 * BIT(7) - Indicates CPP CFC command parity error type
 * BIT(8) - Indicates CPP CFC data parity error type
 */
#define ADF_GEN6_CPP_CFC_ERR_STATUS_ERR_BIT		BIT(0)
#define ADF_GEN6_CPP_CFC_ERR_STATUS_MERR_BIT		BIT(1)
#define ADF_GEN6_CPP_CFC_ERR_STATUS_CMDPAR_BIT		BIT(7)
#define ADF_GEN6_CPP_CFC_ERR_STATUS_DATAPAR_BIT		BIT(8)
#define ADF_GEN6_CPP_CFC_FATAL_ERR_BIT		\
	(ADF_GEN6_CPP_CFC_ERR_STATUS_ERR_BIT |	\
	 ADF_GEN6_CPP_CFC_ERR_STATUS_MERR_BIT)

/*
 * BIT(0) - Enables CFC to detect and log a push/pull data error
 * BIT(1) - Enables CFC to generate interrupt to PCIEP for a CPP error
 * BIT(4) - When 1 parity detection is disabled
 * BIT(5) - When 1 parity detection is disabled on CPP command bus
 * BIT(6) - When 1 parity detection is disabled on CPP push/pull bus
 * BIT(9) - When 1 RF parity error detection is disabled
 */
#define ADF_GEN6_CPP_CFC_ERR_CTRL_MASK		(BIT(0) | BIT(1))

#define ADF_GEN6_CPP_CFC_ERR_CTRL_DIS_MASK \
	(BIT(4) | BIT(5) | BIT(6) | BIT(9) | BIT(10))

#define ADF_GEN6_CPP_CFC_ERR_CTRL			0x640C00

/*
 * BIT(0) - Clears bit(0) of ADF_GEN6_CPP_CFC_ERR_STATUS
 *	    when an error is reported on CPP
 * BIT(1) - Clears bit(1) of ADF_GEN6_CPP_CFC_ERR_STATUS
 *	    when multiple errors are reported on CPP
 * BIT(2) - Clears bit(2) of ADF_GEN6_CPP_CFC_ERR_STATUS
 *	    when attention interrupt is reported
 */
#define ADF_GEN6_CPP_CFC_ERR_STATUS_CLR_MASK		(BIT(0) | BIT(1) | BIT(2))
#define ADF_GEN6_CPP_CFC_ERR_STATUS_CLR			0x640C08

/*
 * ERRSOU3 bit masks
 * BIT(0) - indicates error response order overflow and/or BME error
 * BIT(1) - indicates RI push/pull error
 * BIT(2) - indicates TI push/pull error
 * BIT(5) - indicates TI pull parity error
 * BIT(6) - indicates RI push parity error
 * BIT(7) - indicates VFLR interrupt
 * BIT(8) - indicates ring pair interrupts for ATU detected fault
 * BIT(9) - indicates rate limiting error
 */
#define ADF_GEN6_ERRSOU3_TIMISCSTS_BIT			BIT(0)
#define ADF_GEN6_ERRSOU3_RICPPINTSTS_MASK		(BIT(1) | BIT(6))
#define ADF_GEN6_ERRSOU3_TICPPINTSTS_MASK		(BIT(2) | BIT(5))
#define ADF_GEN6_ERRSOU3_VFLRNOTIFY_BIT			BIT(7)
#define ADF_GEN6_ERRSOU3_ATUFAULTSTATUS_BIT		BIT(8)
#define ADF_GEN6_ERRSOU3_RLTERROR_BIT			BIT(9)
#define ADF_GEN6_ERRSOU3_TC_VC_MAP_ERROR_BIT		BIT(16)
#define ADF_GEN6_ERRSOU3_PCIE_DEVHALT_BIT		BIT(17)
#define ADF_GEN6_ERRSOU3_PG_REQ_DEVHALT_BIT		BIT(18)
#define ADF_GEN6_ERRSOU3_XLT_CPL_DEVHALT_BIT		BIT(19)
#define ADF_GEN6_ERRSOU3_TI_INT_ERR_DEVHALT_BIT		BIT(20)

#define ADF_GEN6_ERRSOU3_MASK ( \
	(ADF_GEN6_ERRSOU3_TIMISCSTS_BIT) | \
	(ADF_GEN6_ERRSOU3_RICPPINTSTS_MASK) | \
	(ADF_GEN6_ERRSOU3_TICPPINTSTS_MASK) | \
	(ADF_GEN6_ERRSOU3_VFLRNOTIFY_BIT) | \
	(ADF_GEN6_ERRSOU3_ATUFAULTSTATUS_BIT) | \
	(ADF_GEN6_ERRSOU3_RLTERROR_BIT) | \
	(ADF_GEN6_ERRSOU3_TC_VC_MAP_ERROR_BIT) | \
	(ADF_GEN6_ERRSOU3_PCIE_DEVHALT_BIT) | \
	(ADF_GEN6_ERRSOU3_PG_REQ_DEVHALT_BIT) | \
	(ADF_GEN6_ERRSOU3_XLT_CPL_DEVHALT_BIT) | \
	(ADF_GEN6_ERRSOU3_TI_INT_ERR_DEVHALT_BIT))

#define ADF_GEN6_ERRSOU3_DIS_MASK ( \
	(ADF_GEN6_ERRSOU3_TIMISCSTS_BIT) | \
	(ADF_GEN6_ERRSOU3_RICPPINTSTS_MASK) | \
	(ADF_GEN6_ERRSOU3_TICPPINTSTS_MASK) | \
	(ADF_GEN6_ERRSOU3_VFLRNOTIFY_BIT) | \
	(ADF_GEN6_ERRSOU3_ATUFAULTSTATUS_BIT) | \
	(ADF_GEN6_ERRSOU3_RLTERROR_BIT) | \
	(ADF_GEN6_ERRSOU3_TC_VC_MAP_ERROR_BIT))

/* Rate limiting error log register */
#define ADF_GEN6_RLT_ERRLOG				0x508814

#define ADF_GEN6_RLT_ERRLOG_MASK	(BIT(0) | BIT(1) | BIT(2) | BIT(3))

/* TI misc status register */
#define ADF_GEN6_TIMISCSTS				0x50054C

/* TI misc error reporting mask */
#define ADF_GEN6_TIMISCCTL				0x500548

/*
 * TI Misc error reporting control mask
 * BIT(0) - Enables error detection and logging in TIMISCSTS register
 * BIT(1) - It has effect only when SRIOV enabled, this bit is 0 by default
 * BIT(2) - Enables the D-F-x counter within the dispatch arbiter
 *	    to start based on the command triggered from
 * BIT(30) - Disables VFLR functionality
 * bits 1, 2 and 30 value should be preserved and not meant to be changed
 * within RAS.
 */
#define ADF_GEN6_TIMISCCTL_BIT				BIT(0)
#define ADF_GEN6_TIMSCCTL_RELAY_MASK			(BIT(1) | BIT(2) | BIT(30))

/* RI CPP interface status register */
#define ADF_GEN6_RICPPINTSTS				0x41A330

/*
 * Uncorrectable error mask in RICPPINTSTS register
 * BIT(0) - RI asserted the CPP error signal during a push
 * BIT(1) - RI detected the CPP error signal asserted during a pull
 * BIT(2) - RI detected a push data parity error
 * BIT(3) - RI detected a push valid parity error
 */
#define ADF_GEN6_RICPPINTSTS_MASK			(BIT(0) | BIT(1) | BIT(2) | BIT(3))

/* RI CPP interface register control */
#define ADF_GEN6_RICPPINTCTL				0x41A32C

/*
 * Control bit mask for RICPPINTCTL register
 * BIT(0) - value of 1 enables error detection and reporting
 *	    on the RI CPP Push interface
 * BIT(1) - value of 1 enables error detection and reporting
 *	    on the RI CPP Pull interface
 * BIT(2) - value of 1 enables error detection and reporting
 *	    on the RI Parity
 * BIT(3) - value of 1 enable checking parity on CPP
 */
#define ADF_GEN6_RICPPINTCTL_MASK \
	(BIT(0) | BIT(1) | BIT(2) | BIT(3) | BIT(4))

/* TI CPP interface status register */
#define ADF_GEN6_TICPPINTSTS				0x50053C

/*
 * Uncorrectable error mask in TICPPINTSTS register
 * BIT(0) - value of 1 indicates that the TI asserted
 *	    the CPP error signal during a push
 * BIT(1) - value of 1 indicates that the TI detected
 *	    the CPP error signal asserted during a pull
 * BIT(2) - value of 1 indicates that the TI detected
 *	    a pull data parity error
 */
#define ADF_GEN6_TICPPINTSTS_MASK			(BIT(0) | BIT(1) | BIT(2))

/* TI CPP interface status register control */
#define ADF_GEN6_TICPPINTCTL				0x500538

/*
 * Control bit mask for TICPPINTCTL register
 * BIT(0) - value of 1 enables error detection and reporting on
 *	    the TI CPP Push interface
 * BIT(1) - value of 1 enables error detection and reporting on
 *	    the TI CPP Push interface
 * BIT(2) - value of 1 enables parity error detection and logging on
 *	    the TI CPP Pull interface
 * BIT(3) - value of 1 enables CPP CMD and Pull Data parity checking
 */
#define ADF_GEN6_TICPPINTCTL_MASK	\
	(BIT(0) | BIT(1) | BIT(2) | BIT(3) | BIT(4))

/* ATU fault status register */
#define ADF_GEN6_ATUFAULTSTATUS(i)			(0x506000 + ((i) * 0x4))

#define ADF_GEN6_ATUFAULTSTATUS_BIT			BIT(0)

/* Command parity error detected on IOSFP command to QAT */
#define ADF_GEN6_RIMISCSTS_BIT				BIT(0)

#define ADF_GEN6_GENSTS					0x41A220
#define ADF_GEN6_GENSTS_DEVICE_STATE_MASK		GENMASK(1, 0)
#define ADF_GEN6_GENSTS_RESET_TYPE_MASK			GENMASK(3, 2)
#define ADF_GEN6_GENSTS_PFLR				0x1
#define ADF_GEN6_GENSTS_COLD_RESET			0x3
#define ADF_GEN6_GENSTS_DEVHALT				0x1

void adf_gen6_init_ras_ops(struct adf_ras_ops *ras_ops);

#endif /* ADF_GEN6_RAS_H_ */
