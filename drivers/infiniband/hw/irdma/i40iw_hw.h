/* SPDX-License-Identifier: GPL-2.0 or Linux-OpenIB */
/* Copyright (c) 2015 - 2021 Intel Corporation */
#ifndef I40IW_HW_H
#define I40IW_HW_H
#define I40E_VFPE_CQPTAIL1            0x0000A000 /* Reset: VFR */
#define I40E_VFPE_CQPDB1              0x0000BC00 /* Reset: VFR */
#define I40E_VFPE_CCQPSTATUS1         0x0000B800 /* Reset: VFR */
#define I40E_VFPE_CCQPHIGH1           0x00009800 /* Reset: VFR */
#define I40E_VFPE_CCQPLOW1            0x0000AC00 /* Reset: VFR */
#define I40E_VFPE_CQARM1              0x0000B400 /* Reset: VFR */
#define I40E_VFPE_CQACK1              0x0000B000 /* Reset: VFR */
#define I40E_VFPE_AEQALLOC1           0x0000A400 /* Reset: VFR */
#define I40E_VFPE_CQPERRCODES1        0x00009C00 /* Reset: VFR */
#define I40E_VFPE_WQEALLOC1           0x0000C000 /* Reset: VFR */
#define I40E_VFINT_DYN_CTLN(_INTVF)   (0x00024800 + ((_INTVF) * 4)) /* _i=0...511 */ /* Reset: VFR */

#define I40E_PFPE_CQPTAIL             0x00008080 /* Reset: PFR */

#define I40E_PFPE_CQPDB               0x00008000 /* Reset: PFR */
#define I40E_PFPE_CCQPSTATUS          0x00008100 /* Reset: PFR */
#define I40E_PFPE_CCQPHIGH            0x00008200 /* Reset: PFR */
#define I40E_PFPE_CCQPLOW             0x00008180 /* Reset: PFR */
#define I40E_PFPE_CQARM               0x00131080 /* Reset: PFR */
#define I40E_PFPE_CQACK               0x00131100 /* Reset: PFR */
#define I40E_PFPE_AEQALLOC            0x00131180 /* Reset: PFR */
#define I40E_PFPE_CQPERRCODES         0x00008880 /* Reset: PFR */
#define I40E_PFPE_WQEALLOC            0x00138C00 /* Reset: PFR */
#define I40E_GLPCI_LBARCTRL           0x000BE484 /* Reset: POR */
#define I40E_GLPE_CPUSTATUS0          0x0000D040 /* Reset: PE_CORER */
#define I40E_GLPE_CPUSTATUS1          0x0000D044 /* Reset: PE_CORER */
#define I40E_GLPE_CPUSTATUS2          0x0000D048 /* Reset: PE_CORER */
#define I40E_GLPE_CRITERR             0x000B4000 /* Reset: PE_CORER */
#define I40E_PFHMC_PDINV              0x000C0300 /* Reset: PFR */
#define I40E_GLHMC_VFPDINV(_i)        (0x000C8300 + ((_i) * 4)) /* _i=0...31 */ /* Reset: CORER */
#define I40E_PFINT_DYN_CTLN(_INTPF)   (0x00034800 + ((_INTPF) * 4)) /* _i=0...511 */	/* Reset: PFR */
#define I40E_PFINT_AEQCTL             0x00038700 /* Reset: CORER */

#define I40E_GLPES_PFIP4RXDISCARD(_i)            (0x00010600 + ((_i) * 4)) /* _i=0...15 */ /* Reset: PE_CORER */
#define I40E_GLPES_PFIP4RXTRUNC(_i)              (0x00010700 + ((_i) * 4)) /* _i=0...15 */ /* Reset: PE_CORER */
#define I40E_GLPES_PFIP4TXNOROUTE(_i)            (0x00012E00 + ((_i) * 4)) /* _i=0...15 */ /* Reset: PE_CORER */
#define I40E_GLPES_PFIP6RXDISCARD(_i)            (0x00011200 + ((_i) * 4)) /* _i=0...15 */ /* Reset: PE_CORER */
#define I40E_GLPES_PFIP6RXTRUNC(_i)              (0x00011300 + ((_i) * 4)) /* _i=0...15 */ /* Reset: PE_CORER */

#define I40E_GLPES_PFRDMAVBNDLO(_i)              (0x00014800 + ((_i) * 8)) /* _i=0...15 */ /* Reset: PE_CORER */
#define I40E_GLPES_PFIP4TXMCOCTSLO(_i)           (0x00012000 + ((_i) * 8)) /* _i=0...15 */ /* Reset: PE_CORER */
#define I40E_GLPES_PFIP6RXMCOCTSLO(_i)           (0x00011600 + ((_i) * 8)) /* _i=0...15 */ /* Reset: PE_CORER */
#define I40E_GLPES_PFIP6TXMCOCTSLO(_i)           (0x00012A00 + ((_i) * 8)) /* _i=0...15 */ /* Reset: PE_CORER */
#define I40E_GLPES_PFUDPRXPKTSLO(_i)             (0x00013800 + ((_i) * 8)) /* _i=0...15 */ /* Reset: PE_CORER */
#define I40E_GLPES_PFUDPTXPKTSLO(_i)             (0x00013A00 + ((_i) * 8)) /* _i=0...15 */ /* Reset: PE_CORER */

#define I40E_GLPES_PFIP6TXNOROUTE(_i)            (0x00012F00 + ((_i) * 4)) /* _i=0...15 */ /* Reset: PE_CORER */
#define I40E_GLPES_PFTCPRTXSEG(_i)               (0x00013600 + ((_i) * 4)) /* _i=0...15 */ /* Reset: PE_CORER */
#define I40E_GLPES_PFTCPRXOPTERR(_i)             (0x00013200 + ((_i) * 4)) /* _i=0...15 */ /* Reset: PE_CORER */
#define I40E_GLPES_PFTCPRXPROTOERR(_i)           (0x00013300 + ((_i) * 4)) /* _i=0...15 */ /* Reset: PE_CORER */
#define I40E_GLPES_PFRXVLANERR(_i)               (0x00010000 + ((_i) * 4)) /* _i=0...15 */ /* Reset: PE_CORER */
#define I40E_GLPES_PFIP4RXOCTSLO(_i)             (0x00010200 + ((_i) * 8)) /* _i=0...15 */ /* Reset: PE_CORER */
#define I40E_GLPES_PFIP4RXPKTSLO(_i)             (0x00010400 + ((_i) * 8)) /* _i=0...15 */ /* Reset: PE_CORER */
#define I40E_GLPES_PFIP4RXFRAGSLO(_i)            (0x00010800 + ((_i) * 8)) /* _i=0...15 */ /* Reset: PE_CORER */
#define I40E_GLPES_PFIP4RXMCPKTSLO(_i)           (0x00010C00 + ((_i) * 8)) /* _i=0...15 */ /* Reset: PE_CORER */
#define I40E_GLPES_PFIP4TXOCTSLO(_i)             (0x00011A00 + ((_i) * 8)) /* _i=0...15 */ /* Reset: PE_CORER */
#define I40E_GLPES_PFIP4TXPKTSLO(_i)             (0x00011C00 + ((_i) * 8)) /* _i=0...15 */ /* Reset: PE_CORER */
#define I40E_GLPES_PFIP4TXFRAGSLO(_i)            (0x00011E00 + ((_i) * 8)) /* _i=0...15 */ /* Reset: PE_CORER */
#define I40E_GLPES_PFIP4TXMCPKTSLO(_i)           (0x00012200 + ((_i) * 8)) /* _i=0...15 */ /* Reset: PE_CORER */
#define I40E_GLPES_PFIP6RXOCTSLO(_i)             (0x00010E00 + ((_i) * 8)) /* _i=0...15 */ /* Reset: PE_CORER */
#define I40E_GLPES_PFIP6RXPKTSLO(_i)             (0x00011000 + ((_i) * 8)) /* _i=0...15 */ /* Reset: PE_CORER */
#define I40E_GLPES_PFIP6RXFRAGSLO(_i)            (0x00011400 + ((_i) * 8)) /* _i=0...15 */ /* Reset: PE_CORER */
#define I40E_GLPES_PFIP6TXOCTSLO(_i)             (0x00012400 + ((_i) * 8)) /* _i=0...15 */ /* Reset: PE_CORER */
#define I40E_GLPES_PFIP6TXPKTSLO(_i)             (0x00012600 + ((_i) * 8)) /* _i=0...15 */ /* Reset: PE_CORER */
#define I40E_GLPES_PFIP6TXFRAGSLO(_i)            (0x00012800 + ((_i) * 8)) /* _i=0...15 */ /* Reset: PE_CORER */
#define I40E_GLPES_PFIP6TXMCPKTSLO(_i)           (0x00012C00 + ((_i) * 8)) /* _i=0...15 */ /* Reset: PE_CORER */
#define I40E_GLPES_PFTCPTXSEGLO(_i)              (0x00013400 + ((_i) * 8)) /* _i=0...15 */ /* Reset: PE_CORER */
#define I40E_GLPES_PFRDMARXRDSLO(_i)             (0x00013E00 + ((_i) * 8)) /* _i=0...15 */ /* Reset: PE_CORER */
#define I40E_GLPES_PFRDMARXSNDSLO(_i)            (0x00014000 + ((_i) * 8)) /* _i=0...15 */ /* Reset: PE_CORER */
#define I40E_GLPES_PFRDMARXWRSLO(_i)             (0x00013C00 + ((_i) * 8)) /* _i=0...15 */ /* Reset: PE_CORER */
#define I40E_GLPES_PFRDMATXRDSLO(_i)             (0x00014400 + ((_i) * 8)) /* _i=0...15 */ /* Reset: PE_CORER */
#define I40E_GLPES_PFRDMATXSNDSLO(_i)            (0x00014600 + ((_i) * 8)) /* _i=0...15 */ /* Reset: PE_CORER */
#define I40E_GLPES_PFRDMATXWRSLO(_i)             (0x00014200 + ((_i) * 8)) /* _i=0...15 */ /* Reset: PE_CORER */
#define I40E_GLPES_PFIP4RXMCOCTSLO(_i)           (0x00010A00 + ((_i) * 8)) /* _i=0...15 */ /* Reset: PE_CORER */
#define I40E_GLPES_PFIP6RXMCPKTSLO(_i)           (0x00011800 + ((_i) * 8)) /* _i=0...15 */ /* Reset: PE_CORER */
#define I40E_GLPES_PFTCPRXSEGSLO(_i)             (0x00013000 + ((_i) * 8)) /* _i=0...15 */ /* Reset: PE_CORER */
#define I40E_GLPES_PFRDMAVINVLO(_i)              (0x00014A00 + ((_i) * 8)) /* _i=0...15 */ /* Reset: PE_CORER */

#define I40IW_DB_ADDR_OFFSET    (4 * 1024 * 1024 - 64 * 1024)

#define I40IW_VF_DB_ADDR_OFFSET (64 * 1024)

#define I40E_PFINT_LNKLSTN(_INTPF)           (0x00035000 + ((_INTPF) * 4)) /* _i=0...511 */ /* Reset: PFR */
#define I40E_PFINT_LNKLSTN_MAX_INDEX         511
#define I40E_PFINT_LNKLSTN_FIRSTQ_INDX GENMASK(10, 0)
#define I40E_PFINT_LNKLSTN_FIRSTQ_TYPE GENMASK(12, 11)

#define I40E_PFINT_CEQCTL(_INTPF)          (0x00036800 + ((_INTPF) * 4)) /* _i=0...511 */ /* Reset: CORER */
#define I40E_PFINT_CEQCTL_MAX_INDEX        511

/* shifts/masks for FLD_[LS/RS]_64 macros used in device table */
#define I40E_PFINT_CEQCTL_MSIX_INDX_S 0
#define I40E_PFINT_CEQCTL_MSIX_INDX GENMASK(7, 0)
#define I40E_PFINT_CEQCTL_ITR_INDX_S 11
#define I40E_PFINT_CEQCTL_ITR_INDX GENMASK(12, 11)
#define I40E_PFINT_CEQCTL_MSIX0_INDX_S 13
#define I40E_PFINT_CEQCTL_MSIX0_INDX GENMASK(15, 13)
#define I40E_PFINT_CEQCTL_NEXTQ_INDX_S 16
#define I40E_PFINT_CEQCTL_NEXTQ_INDX GENMASK(26, 16)
#define I40E_PFINT_CEQCTL_NEXTQ_TYPE_S 27
#define I40E_PFINT_CEQCTL_NEXTQ_TYPE GENMASK(28, 27)
#define I40E_PFINT_CEQCTL_CAUSE_ENA_S 30
#define I40E_PFINT_CEQCTL_CAUSE_ENA BIT(30)
#define I40E_PFINT_CEQCTL_INTEVENT_S 31
#define I40E_PFINT_CEQCTL_INTEVENT BIT(31)
#define I40E_CQPSQ_STAG_PDID_S 48
#define I40E_CQPSQ_STAG_PDID GENMASK_ULL(62, 48)
#define I40E_PFPE_CCQPSTATUS_CCQP_DONE_S 0
#define I40E_PFPE_CCQPSTATUS_CCQP_DONE BIT_ULL(0)
#define I40E_PFPE_CCQPSTATUS_CCQP_ERR_S 31
#define I40E_PFPE_CCQPSTATUS_CCQP_ERR BIT_ULL(31)
#define I40E_PFINT_DYN_CTLN_ITR_INDX_S 3
#define I40E_PFINT_DYN_CTLN_ITR_INDX GENMASK(4, 3)
#define I40E_PFINT_DYN_CTLN_INTENA_S 0
#define I40E_PFINT_DYN_CTLN_INTENA BIT(0)
#define I40E_CQPSQ_CQ_CEQID_S 24
#define I40E_CQPSQ_CQ_CEQID GENMASK(30, 24)
#define I40E_CQPSQ_CQ_CQID_S 0
#define I40E_CQPSQ_CQ_CQID GENMASK_ULL(15, 0)
#define I40E_COMMIT_FPM_CQCNT_S 0
#define I40E_COMMIT_FPM_CQCNT GENMASK_ULL(17, 0)

#define I40E_VSIQF_CTL(_VSI)             (0x0020D800 + ((_VSI) * 4))

enum i40iw_device_caps_const {
	I40IW_MAX_WQ_FRAGMENT_COUNT		= 3,
	I40IW_MAX_SGE_RD			= 1,
	I40IW_MAX_PUSH_PAGE_COUNT		= 0,
	I40IW_MAX_INLINE_DATA_SIZE		= 48,
	I40IW_MAX_IRD_SIZE			= 63,
	I40IW_MAX_ORD_SIZE			= 127,
	I40IW_MAX_WQ_ENTRIES			= 2048,
	I40IW_MAX_WQE_SIZE_RQ			= 128,
	I40IW_MAX_PDS				= 32768,
	I40IW_MAX_STATS_COUNT			= 16,
	I40IW_MAX_CQ_SIZE			= 1048575,
	I40IW_MAX_OUTBOUND_MSG_SIZE		= 2147483647,
	I40IW_MAX_INBOUND_MSG_SIZE		= 2147483647,
	I40IW_MIN_WQ_SIZE                       = 4 /* WQEs */,
};

#define I40IW_QP_WQE_MIN_SIZE   32
#define I40IW_QP_WQE_MAX_SIZE   128
#define I40IW_MAX_RQ_WQE_SHIFT  2
#define I40IW_MAX_QUANTA_PER_WR 2

#define I40IW_QP_SW_MAX_SQ_QUANTA 2048
#define I40IW_QP_SW_MAX_RQ_QUANTA 16384
#define I40IW_QP_SW_MAX_WQ_QUANTA 2048
#define I40IW_MAX_QP_WRS ((I40IW_QP_SW_MAX_SQ_QUANTA - IRDMA_SQ_RSVD) / I40IW_MAX_QUANTA_PER_WR)
#define I40IW_FIRST_VF_FPM_ID 16
#define QUEUE_TYPE_CEQ        2
#define NULL_QUEUE_INDEX      0x7FF

void i40iw_init_hw(struct irdma_sc_dev *dev);
#endif /* I40IW_HW_H */
