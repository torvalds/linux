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

/*
 * ERRSOU2 bit mask
 * BIT(0) - SSM Interrupt Mask
 * BIT(1) - CFC on CPP. ORed of CFC Push error and Pull error
 * BIT(2) - BIT(4) - CPP attention interrupts, deprecated on gen4 devices
 * BIT(18) - PM interrupt
 */
#define ADF_GEN4_ERRSOU2_SSM_ERR_BIT			BIT(0)
#define ADF_GEN4_ERRSOU2_CPP_CFC_ERR_STATUS_BIT	BIT(1)
#define ADF_GEN4_ERRSOU2_CPP_CFC_ATT_INT_BITMASK \
	(BIT(2) | BIT(3) | BIT(4))

#define ADF_GEN4_ERRSOU2_PM_INT_BIT			BIT(18)

#define ADF_GEN4_ERRSOU2_BITMASK \
	(ADF_GEN4_ERRSOU2_SSM_ERR_BIT | \
	 ADF_GEN4_ERRSOU2_CPP_CFC_ERR_STATUS_BIT)

#define ADF_GEN4_ERRSOU2_DIS_BITMASK \
	(ADF_GEN4_ERRSOU2_SSM_ERR_BIT | \
	 ADF_GEN4_ERRSOU2_CPP_CFC_ERR_STATUS_BIT | \
	 ADF_GEN4_ERRSOU2_CPP_CFC_ATT_INT_BITMASK)

#define ADF_GEN4_IAINTSTATSSM				0x28

/* IAINTSTATSSM error bit mask definitions */
#define ADF_GEN4_IAINTSTATSSM_UERRSSMSH_BIT		BIT(0)
#define ADF_GEN4_IAINTSTATSSM_CERRSSMSH_BIT		BIT(1)
#define ADF_GEN4_IAINTSTATSSM_PPERR_BIT			BIT(2)
#define ADF_GEN4_IAINTSTATSSM_SLICEHANG_ERR_BIT		BIT(3)
#define ADF_GEN4_IAINTSTATSSM_SPPPARERR_BIT		BIT(4)
#define ADF_GEN4_IAINTSTATSSM_SSMCPPERR_BIT		BIT(5)
#define ADF_GEN4_IAINTSTATSSM_SSMSOFTERRORPARITY_BIT	BIT(6)
#define ADF_GEN4_IAINTSTATSSM_SER_ERR_SSMSH_CERR_BIT	BIT(7)
#define ADF_GEN4_IAINTSTATSSM_SER_ERR_SSMSH_UNCERR_BIT	BIT(8)

#define ADF_GEN4_IAINTSTATSSM_BITMASK \
	(ADF_GEN4_IAINTSTATSSM_UERRSSMSH_BIT | \
	 ADF_GEN4_IAINTSTATSSM_CERRSSMSH_BIT | \
	 ADF_GEN4_IAINTSTATSSM_PPERR_BIT | \
	 ADF_GEN4_IAINTSTATSSM_SLICEHANG_ERR_BIT | \
	 ADF_GEN4_IAINTSTATSSM_SPPPARERR_BIT | \
	 ADF_GEN4_IAINTSTATSSM_SSMCPPERR_BIT | \
	 ADF_GEN4_IAINTSTATSSM_SSMSOFTERRORPARITY_BIT | \
	 ADF_GEN4_IAINTSTATSSM_SER_ERR_SSMSH_CERR_BIT | \
	 ADF_GEN4_IAINTSTATSSM_SER_ERR_SSMSH_UNCERR_BIT)

#define ADF_GEN4_UERRSSMSH				0x18

/*
 * UERRSSMSH error bit masks definitions
 *
 * BIT(0) - Indicates one uncorrectable error
 * BIT(15) - Indicates multiple uncorrectable errors
 *	     in device shared memory
 */
#define ADF_GEN4_UERRSSMSH_BITMASK			(BIT(0) | BIT(15))

#define ADF_GEN4_UERRSSMSHAD				0x1C

#define ADF_GEN4_CERRSSMSH				0x10

/*
 * CERRSSMSH error bit
 * BIT(0) - Indicates one correctable error
 */
#define ADF_GEN4_CERRSSMSH_ERROR_BIT			BIT(0)

#define ADF_GEN4_CERRSSMSHAD				0x14

/* SSM error handling features enable register */
#define ADF_GEN4_SSMFEATREN				0x198

/*
 * Disable SSM error detection and reporting features
 * enabled by device driver on RAS initialization
 *
 * following bits should be cleared :
 * BIT(4)  - Disable parity for CPP parity
 * BIT(12) - Disable logging push/pull data error in pperr register.
 * BIT(16) - BIT(23) - Disable parity for SPPs
 * BIT(24) - BIT(27) - Disable parity for SPPs, if it's supported on the device.
 */
#define ADF_GEN4_SSMFEATREN_DIS_BITMASK \
	(BIT(0) | BIT(1) | BIT(2) | BIT(3) | BIT(5) | BIT(6) | BIT(7) | \
	 BIT(8) | BIT(9) | BIT(10) | BIT(11) | BIT(13) | BIT(14) | BIT(15))

#define ADF_GEN4_INTMASKSSM				0x0

/*
 * Error reporting mask in INTMASKSSM
 * BIT(0) - Shared memory uncorrectable interrupt mask
 * BIT(1) - Shared memory correctable interrupt mask
 * BIT(2) - PPERR interrupt mask
 * BIT(3) - CPP parity error Interrupt mask
 * BIT(4) - SSM interrupt generated by SER correctable error mask
 * BIT(5) - SSM interrupt generated by SER uncorrectable error
 *	    - not stop and scream - mask
 */
#define ADF_GEN4_INTMASKSSM_BITMASK \
	(BIT(0) | BIT(1) | BIT(2) | BIT(3) | BIT(4) | BIT(5))

/* CPP push or pull error */
#define ADF_GEN4_PPERR					0x8

#define ADF_GEN4_PPERR_BITMASK				(BIT(0) | BIT(1))

#define ADF_GEN4_PPERRID				0xC

/* Slice hang handling related registers */
#define ADF_GEN4_SLICEHANGSTATUS_ATH_CPH		0x84
#define ADF_GEN4_SLICEHANGSTATUS_CPR_XLT		0x88
#define ADF_GEN4_SLICEHANGSTATUS_DCPR_UCS		0x90
#define ADF_GEN4_SLICEHANGSTATUS_WAT_WCP		0x8C
#define ADF_GEN4_SLICEHANGSTATUS_PKE			0x94

#define ADF_GEN4_SHINTMASKSSM_ATH_CPH			0xF0
#define ADF_GEN4_SHINTMASKSSM_CPR_XLT			0xF4
#define ADF_GEN4_SHINTMASKSSM_DCPR_UCS			0xFC
#define ADF_GEN4_SHINTMASKSSM_WAT_WCP			0xF8
#define ADF_GEN4_SHINTMASKSSM_PKE			0x100

/* SPP pull cmd parity err_*slice* CSR */
#define ADF_GEN4_SPPPULLCMDPARERR_ATH_CPH		0x1A4
#define ADF_GEN4_SPPPULLCMDPARERR_CPR_XLT		0x1A8
#define ADF_GEN4_SPPPULLCMDPARERR_DCPR_UCS		0x1B0
#define ADF_GEN4_SPPPULLCMDPARERR_PKE			0x1B4
#define ADF_GEN4_SPPPULLCMDPARERR_WAT_WCP		0x1AC

/* SPP pull data parity err_*slice* CSR */
#define ADF_GEN4_SPPPULLDATAPARERR_ATH_CPH		0x1BC
#define ADF_GEN4_SPPPULLDATAPARERR_CPR_XLT		0x1C0
#define ADF_GEN4_SPPPULLDATAPARERR_DCPR_UCS		0x1C8
#define ADF_GEN4_SPPPULLDATAPARERR_PKE			0x1CC
#define ADF_GEN4_SPPPULLDATAPARERR_WAT_WCP		0x1C4

/* SPP push cmd parity err_*slice* CSR */
#define ADF_GEN4_SPPPUSHCMDPARERR_ATH_CPH		0x1D4
#define ADF_GEN4_SPPPUSHCMDPARERR_CPR_XLT		0x1D8
#define ADF_GEN4_SPPPUSHCMDPARERR_DCPR_UCS		0x1E0
#define ADF_GEN4_SPPPUSHCMDPARERR_PKE			0x1E4
#define ADF_GEN4_SPPPUSHCMDPARERR_WAT_WCP		0x1DC

/* SPP push data parity err_*slice* CSR */
#define ADF_GEN4_SPPPUSHDATAPARERR_ATH_CPH		0x1EC
#define ADF_GEN4_SPPPUSHDATAPARERR_CPR_XLT		0x1F0
#define ADF_GEN4_SPPPUSHDATAPARERR_DCPR_UCS		0x1F8
#define ADF_GEN4_SPPPUSHDATAPARERR_PKE			0x1FC
#define ADF_GEN4_SPPPUSHDATAPARERR_WAT_WCP		0x1F4

/* Accelerator SPP parity error mask registers */
#define ADF_GEN4_SPPPARERRMSK_ATH_CPH			0x204
#define ADF_GEN4_SPPPARERRMSK_CPR_XLT			0x208
#define ADF_GEN4_SPPPARERRMSK_DCPR_UCS			0x210
#define ADF_GEN4_SPPPARERRMSK_PKE			0x214
#define ADF_GEN4_SPPPARERRMSK_WAT_WCP			0x20C

#define ADF_GEN4_SSMCPPERR				0x224

/*
 * Uncorrectable error mask in SSMCPPERR
 * BIT(0) - indicates CPP command parity error
 * BIT(1) - indicates CPP Main Push PPID parity error
 * BIT(2) - indicates CPP Main ePPID parity error
 * BIT(3) - indicates CPP Main push data parity error
 * BIT(4) - indicates CPP Main Pull PPID parity error
 * BIT(5) - indicates CPP target pull data parity error
 */
#define ADF_GEN4_SSMCPPERR_FATAL_BITMASK \
	(BIT(0) | BIT(1) | BIT(4))

#define ADF_GEN4_SSMCPPERR_UNCERR_BITMASK \
	(BIT(2) | BIT(3) | BIT(5))

#define ADF_GEN4_SSMSOFTERRORPARITY_SRC			0x9C
#define ADF_GEN4_SSMSOFTERRORPARITYMASK_SRC		0xB8

#define ADF_GEN4_SSMSOFTERRORPARITY_ATH_CPH		0xA0
#define ADF_GEN4_SSMSOFTERRORPARITYMASK_ATH_CPH		0xBC

#define ADF_GEN4_SSMSOFTERRORPARITY_CPR_XLT		0xA4
#define ADF_GEN4_SSMSOFTERRORPARITYMASK_CPR_XLT		0xC0

#define ADF_GEN4_SSMSOFTERRORPARITY_DCPR_UCS		0xAC
#define ADF_GEN4_SSMSOFTERRORPARITYMASK_DCPR_UCS	0xC8

#define ADF_GEN4_SSMSOFTERRORPARITY_PKE			0xB0
#define ADF_GEN4_SSMSOFTERRORPARITYMASK_PKE		0xCC

#define ADF_GEN4_SSMSOFTERRORPARITY_WAT_WCP		0xA8
#define ADF_GEN4_SSMSOFTERRORPARITYMASK_WAT_WCP		0xC4

/* RF parity error detected in SharedRAM */
#define ADF_GEN4_SSMSOFTERRORPARITY_SRC_BIT		BIT(0)

#define ADF_GEN4_SER_ERR_SSMSH				0x44C

/*
 * Fatal error mask in SER_ERR_SSMSH
 * BIT(0) - Indicates an uncorrectable error has occurred in the
 *          accelerator controller command RFs
 * BIT(2) - Parity error occurred in the bank SPP fifos
 * BIT(3) - Indicates Parity error occurred in following fifos in
 *          the design
 * BIT(4) - Parity error occurred in flops in the design
 * BIT(5) - Uncorrectable error has occurred in the
 *	    target push and pull data register flop
 * BIT(7) - Indicates Parity error occurred in the Resource Manager
 *	    pending lock request fifos
 * BIT(8) - Indicates Parity error occurred in the Resource Manager
 *	    MECTX command queues logic
 * BIT(9) - Indicates Parity error occurred in the Resource Manager
 *	    MECTX sigdone fifo flops
 * BIT(10) - Indicates an uncorrectable error has occurred in the
 *	     Resource Manager MECTX command RFs
 * BIT(14) - Parity error occurred in Buffer Manager sigdone FIFO
 */
 #define ADF_GEN4_SER_ERR_SSMSH_FATAL_BITMASK \
	 (BIT(0) | BIT(2) | BIT(3) | BIT(4) | BIT(5) | BIT(7) | \
	  BIT(8) | BIT(9) | BIT(10) | BIT(14))

/*
 * Uncorrectable error mask in SER_ERR_SSMSH
 * BIT(12) Parity error occurred in Buffer Manager pool 0
 * BIT(13) Parity error occurred in Buffer Manager pool 1
 */
#define ADF_GEN4_SER_ERR_SSMSH_UNCERR_BITMASK \
	(BIT(12) | BIT(13))

/*
 * Correctable error mask in SER_ERR_SSMSH
 * BIT(1) - Indicates a correctable Error has occurred
 *	    in the slice controller command RFs
 * BIT(6) - Indicates a correctable Error has occurred in
 *	    the target push and pull data RFs
 * BIT(11) - Indicates an correctable Error has occurred in
 *	     the Resource Manager MECTX command RFs
 */
#define ADF_GEN4_SER_ERR_SSMSH_CERR_BITMASK \
	(BIT(1) | BIT(6) | BIT(11))

/* SSM shared memory SER error reporting mask */
#define ADF_GEN4_SER_EN_SSMSH				0x450

/*
 * SSM SER error reporting mask in SER_en_err_ssmsh
 * BIT(0) - Enables uncorrectable Error detection in :
 *	    1) slice controller command RFs.
 *	    2) target push/pull data registers
 * BIT(1) - Enables correctable Error detection in :
 *	    1) slice controller command RFs
 *	    2) target push/pull data registers
 * BIT(2) - Enables Parity error detection in
 *	    1) bank SPP fifos
 *	    2) gen4_pull_id_queue
 *	    3) gen4_push_id_queue
 *	    4) AE_pull_sigdn_fifo
 *	    5) DT_push_sigdn_fifo
 *	    6) slx_push_sigdn_fifo
 *	    7) secure_push_cmd_fifo
 *	    8) secure_pull_cmd_fifo
 *	    9) Head register in FIFO wrapper
 *	    10) current_cmd in individual push queue
 *	    11) current_cmd in individual pull queue
 *	    12) push_command_rxp arbitrated in ssm_push_cmd_queues
 *	    13) pull_command_rxp arbitrated in ssm_pull_cmd_queues
 * BIT(3) - Enables uncorrectable Error detection in
 *	    the resource manager mectx cmd RFs.
 * BIT(4) - Enables correctable error detection in the Resource Manager
 *	    mectx command RFs
 * BIT(5) - Enables Parity error detection in
 *	    1) resource manager lock request fifo
 *	    2) mectx cmdqueues logic
 *	    3) mectx sigdone fifo
 * BIT(6) - Enables Parity error detection in Buffer Manager pools
 *	    and sigdone fifo
 */
#define ADF_GEN4_SER_EN_SSMSH_BITMASK \
	(BIT(0) | BIT(1) | BIT(2) | BIT(3) | BIT(4) | BIT(5) | BIT(6))

#define ADF_GEN4_CPP_CFC_ERR_STATUS			0x640C04

/*
 * BIT(1) - Indicates multiple CPP CFC errors
 * BIT(7) - Indicates CPP CFC command parity error type
 * BIT(8) - Indicated CPP CFC data parity error type
 */
#define ADF_GEN4_CPP_CFC_ERR_STATUS_MERR_BIT		BIT(1)
#define ADF_GEN4_CPP_CFC_ERR_STATUS_CMDPAR_BIT		BIT(7)
#define ADF_GEN4_CPP_CFC_ERR_STATUS_DATAPAR_BIT		BIT(8)

/*
 * BIT(0) - Enables CFC to detect and log push/pull data error
 * BIT(1) - Enables CFC to generate interrupt to PCIEP for CPP error
 * BIT(4) - When 1 Parity detection is disabled
 * BIT(5) - When 1 Parity detection is disabled on CPP command bus
 * BIT(6) - When 1 Parity detection is disabled on CPP push/pull bus
 * BIT(9) - When 1 RF parity error detection is disabled
 */
#define ADF_GEN4_CPP_CFC_ERR_CTRL_BITMASK		(BIT(0) | BIT(1))

#define ADF_GEN4_CPP_CFC_ERR_CTRL_DIS_BITMASK \
	(BIT(4) | BIT(5) | BIT(6) | BIT(9) | BIT(10))

#define ADF_GEN4_CPP_CFC_ERR_CTRL			0x640C00

/*
 * BIT(0) - Clears bit(0) of ADF_GEN4_CPP_CFC_ERR_STATUS
 *	    when an error is reported on CPP
 * BIT(1) - Clears bit(1) of ADF_GEN4_CPP_CFC_ERR_STATUS
 *	    when multiple errors are reported on CPP
 * BIT(2) - Clears bit(2) of ADF_GEN4_CPP_CFC_ERR_STATUS
 *	    when attention interrupt is reported
 */
#define ADF_GEN4_CPP_CFC_ERR_STATUS_CLR_BITMASK (BIT(0) | BIT(1) | BIT(2))
#define ADF_GEN4_CPP_CFC_ERR_STATUS_CLR			0x640C08

#define ADF_GEN4_CPP_CFC_ERR_PPID_LO			0x640C0C
#define ADF_GEN4_CPP_CFC_ERR_PPID_HI			0x640C10

/* Command Parity error detected on IOSFP Command to QAT */
#define ADF_GEN4_RIMISCSTS_BIT				BIT(0)

void adf_gen4_init_ras_ops(struct adf_ras_ops *ras_ops);

#endif /* ADF_GEN4_RAS_H_ */
