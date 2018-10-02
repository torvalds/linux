/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2018, Intel Corporation. */

/* Machine-generated file */

#ifndef _ICE_HW_AUTOGEN_H_
#define _ICE_HW_AUTOGEN_H_

#define QTX_COMM_DBELL(_DBQM)			(0x002C0000 + ((_DBQM) * 4))
#define PF_FW_ARQBAH				0x00080180
#define PF_FW_ARQBAL				0x00080080
#define PF_FW_ARQH				0x00080380
#define PF_FW_ARQH_ARQH_M			ICE_M(0x3FF, 0)
#define PF_FW_ARQLEN				0x00080280
#define PF_FW_ARQLEN_ARQLEN_M			ICE_M(0x3FF, 0)
#define PF_FW_ARQLEN_ARQVFE_M			BIT(28)
#define PF_FW_ARQLEN_ARQOVFL_M			BIT(29)
#define PF_FW_ARQLEN_ARQCRIT_M			BIT(30)
#define PF_FW_ARQLEN_ARQENABLE_M		BIT(31)
#define PF_FW_ARQT				0x00080480
#define PF_FW_ATQBAH				0x00080100
#define PF_FW_ATQBAL				0x00080000
#define PF_FW_ATQH				0x00080300
#define PF_FW_ATQH_ATQH_M			ICE_M(0x3FF, 0)
#define PF_FW_ATQLEN				0x00080200
#define PF_FW_ATQLEN_ATQLEN_M			ICE_M(0x3FF, 0)
#define PF_FW_ATQLEN_ATQVFE_M			BIT(28)
#define PF_FW_ATQLEN_ATQOVFL_M			BIT(29)
#define PF_FW_ATQLEN_ATQCRIT_M			BIT(30)
#define PF_FW_ATQLEN_ATQENABLE_M		BIT(31)
#define PF_FW_ATQT				0x00080400
#define GLFLXP_RXDID_FLAGS(_i, _j)		(0x0045D000 + ((_i) * 4 + (_j) * 256))
#define GLFLXP_RXDID_FLAGS_FLEXIFLAG_4N_S	0
#define GLFLXP_RXDID_FLAGS_FLEXIFLAG_4N_M	ICE_M(0x3F, 0)
#define GLFLXP_RXDID_FLAGS_FLEXIFLAG_4N_1_S	8
#define GLFLXP_RXDID_FLAGS_FLEXIFLAG_4N_1_M	ICE_M(0x3F, 8)
#define GLFLXP_RXDID_FLAGS_FLEXIFLAG_4N_2_S	16
#define GLFLXP_RXDID_FLAGS_FLEXIFLAG_4N_2_M	ICE_M(0x3F, 16)
#define GLFLXP_RXDID_FLAGS_FLEXIFLAG_4N_3_S	24
#define GLFLXP_RXDID_FLAGS_FLEXIFLAG_4N_3_M	ICE_M(0x3F, 24)
#define GLFLXP_RXDID_FLX_WRD_0(_i)		(0x0045c800 + ((_i) * 4))
#define GLFLXP_RXDID_FLX_WRD_0_PROT_MDID_S	0
#define GLFLXP_RXDID_FLX_WRD_0_PROT_MDID_M	ICE_M(0xFF, 0)
#define GLFLXP_RXDID_FLX_WRD_0_RXDID_OPCODE_S	30
#define GLFLXP_RXDID_FLX_WRD_0_RXDID_OPCODE_M	ICE_M(0x3, 30)
#define GLFLXP_RXDID_FLX_WRD_1(_i)		(0x0045c900 + ((_i) * 4))
#define GLFLXP_RXDID_FLX_WRD_1_PROT_MDID_S	0
#define GLFLXP_RXDID_FLX_WRD_1_PROT_MDID_M	ICE_M(0xFF, 0)
#define GLFLXP_RXDID_FLX_WRD_1_RXDID_OPCODE_S	30
#define GLFLXP_RXDID_FLX_WRD_1_RXDID_OPCODE_M	ICE_M(0x3, 30)
#define GLFLXP_RXDID_FLX_WRD_2(_i)		(0x0045ca00 + ((_i) * 4))
#define GLFLXP_RXDID_FLX_WRD_2_PROT_MDID_S	0
#define GLFLXP_RXDID_FLX_WRD_2_PROT_MDID_M	ICE_M(0xFF, 0)
#define GLFLXP_RXDID_FLX_WRD_2_RXDID_OPCODE_S	30
#define GLFLXP_RXDID_FLX_WRD_2_RXDID_OPCODE_M	ICE_M(0x3, 30)
#define GLFLXP_RXDID_FLX_WRD_3(_i)		(0x0045cb00 + ((_i) * 4))
#define GLFLXP_RXDID_FLX_WRD_3_PROT_MDID_S	0
#define GLFLXP_RXDID_FLX_WRD_3_PROT_MDID_M	ICE_M(0xFF, 0)
#define GLFLXP_RXDID_FLX_WRD_3_RXDID_OPCODE_S	30
#define GLFLXP_RXDID_FLX_WRD_3_RXDID_OPCODE_M	ICE_M(0x3, 30)
#define QRXFLXP_CNTXT(_QRX)			(0x00480000 + ((_QRX) * 4))
#define QRXFLXP_CNTXT_RXDID_IDX_S		0
#define QRXFLXP_CNTXT_RXDID_IDX_M		ICE_M(0x3F, 0)
#define QRXFLXP_CNTXT_RXDID_PRIO_S		8
#define QRXFLXP_CNTXT_RXDID_PRIO_M		ICE_M(0x7, 8)
#define GLGEN_RSTAT				0x000B8188
#define GLGEN_RSTAT_DEVSTATE_M			ICE_M(0x3, 0)
#define GLGEN_RSTCTL				0x000B8180
#define GLGEN_RSTCTL_GRSTDEL_S			0
#define GLGEN_RSTCTL_GRSTDEL_M			ICE_M(0x3F, GLGEN_RSTCTL_GRSTDEL_S)
#define GLGEN_RSTAT_RESET_TYPE_S		2
#define GLGEN_RSTAT_RESET_TYPE_M		ICE_M(0x3, 2)
#define GLGEN_RTRIG				0x000B8190
#define GLGEN_RTRIG_CORER_M			BIT(0)
#define GLGEN_RTRIG_GLOBR_M			BIT(1)
#define GLGEN_STAT				0x000B612C
#define PFGEN_CTRL				0x00091000
#define PFGEN_CTRL_PFSWR_M			BIT(0)
#define PFGEN_STATE				0x00088000
#define PRTGEN_STATUS				0x000B8100
#define PFHMC_ERRORDATA				0x00520500
#define PFHMC_ERRORINFO				0x00520400
#define GLINT_DYN_CTL(_INT)			(0x00160000 + ((_INT) * 4))
#define GLINT_DYN_CTL_INTENA_M			BIT(0)
#define GLINT_DYN_CTL_CLEARPBA_M		BIT(1)
#define GLINT_DYN_CTL_SWINT_TRIG_M		BIT(2)
#define GLINT_DYN_CTL_ITR_INDX_S		3
#define GLINT_DYN_CTL_SW_ITR_INDX_M		ICE_M(0x3, 25)
#define GLINT_DYN_CTL_INTENA_MSK_M		BIT(31)
#define GLINT_ITR(_i, _INT)			(0x00154000 + ((_i) * 8192 + (_INT) * 4))
#define GLINT_RATE(_INT)			(0x0015A000 + ((_INT) * 4))
#define GLINT_RATE_INTRL_ENA_M			BIT(6)
#define PFINT_FW_CTL				0x0016C800
#define PFINT_FW_CTL_MSIX_INDX_M		ICE_M(0x7FF, 0)
#define PFINT_FW_CTL_ITR_INDX_S			11
#define PFINT_FW_CTL_ITR_INDX_M			ICE_M(0x3, 11)
#define PFINT_FW_CTL_CAUSE_ENA_M		BIT(30)
#define PFINT_OICR				0x0016CA00
#define PFINT_OICR_ECC_ERR_M			BIT(16)
#define PFINT_OICR_MAL_DETECT_M			BIT(19)
#define PFINT_OICR_GRST_M			BIT(20)
#define PFINT_OICR_PCI_EXCEPTION_M		BIT(21)
#define PFINT_OICR_HMC_ERR_M			BIT(26)
#define PFINT_OICR_PE_CRITERR_M			BIT(28)
#define PFINT_OICR_CTL				0x0016CA80
#define PFINT_OICR_CTL_MSIX_INDX_M		ICE_M(0x7FF, 0)
#define PFINT_OICR_CTL_ITR_INDX_S		11
#define PFINT_OICR_CTL_ITR_INDX_M		ICE_M(0x3, 11)
#define PFINT_OICR_CTL_CAUSE_ENA_M		BIT(30)
#define PFINT_OICR_ENA				0x0016C900
#define QINT_RQCTL(_QRX)			(0x00150000 + ((_QRX) * 4))
#define QINT_RQCTL_MSIX_INDX_S			0
#define QINT_RQCTL_ITR_INDX_S			11
#define QINT_RQCTL_CAUSE_ENA_M			BIT(30)
#define QINT_TQCTL(_DBQM)			(0x00140000 + ((_DBQM) * 4))
#define QINT_TQCTL_MSIX_INDX_S			0
#define QINT_TQCTL_ITR_INDX_S			11
#define QINT_TQCTL_CAUSE_ENA_M			BIT(30)
#define QRX_CONTEXT(_i, _QRX)			(0x00280000 + ((_i) * 8192 + (_QRX) * 4))
#define QRX_CTRL(_QRX)				(0x00120000 + ((_QRX) * 4))
#define QRX_CTRL_MAX_INDEX			2047
#define QRX_CTRL_QENA_REQ_S			0
#define QRX_CTRL_QENA_REQ_M			BIT(0)
#define QRX_CTRL_QENA_STAT_S			2
#define QRX_CTRL_QENA_STAT_M			BIT(2)
#define QRX_ITR(_QRX)				(0x00292000 + ((_QRX) * 4))
#define QRX_TAIL(_QRX)				(0x00290000 + ((_QRX) * 4))
#define QRX_TAIL_MAX_INDEX			2047
#define QRX_TAIL_TAIL_S				0
#define QRX_TAIL_TAIL_M				ICE_M(0x1FFF, 0)
#define GL_MDET_RX				0x00294C00
#define GL_MDET_RX_QNUM_S			0
#define GL_MDET_RX_QNUM_M			ICE_M(0x7FFF, 0)
#define GL_MDET_RX_VF_NUM_S			15
#define GL_MDET_RX_VF_NUM_M			ICE_M(0xFF, 15)
#define GL_MDET_RX_PF_NUM_S			23
#define GL_MDET_RX_PF_NUM_M			ICE_M(0x7, 23)
#define GL_MDET_RX_MAL_TYPE_S			26
#define GL_MDET_RX_MAL_TYPE_M			ICE_M(0x1F, 26)
#define GL_MDET_RX_VALID_M			BIT(31)
#define GL_MDET_TX_PQM				0x002D2E00
#define GL_MDET_TX_PQM_PF_NUM_S			0
#define GL_MDET_TX_PQM_PF_NUM_M			ICE_M(0x7, 0)
#define GL_MDET_TX_PQM_VF_NUM_S			4
#define GL_MDET_TX_PQM_VF_NUM_M			ICE_M(0xFF, 4)
#define GL_MDET_TX_PQM_QNUM_S			12
#define GL_MDET_TX_PQM_QNUM_M			ICE_M(0x3FFF, 12)
#define GL_MDET_TX_PQM_MAL_TYPE_S		26
#define GL_MDET_TX_PQM_MAL_TYPE_M		ICE_M(0x1F, 26)
#define GL_MDET_TX_PQM_VALID_M			BIT(31)
#define GL_MDET_TX_TCLAN			0x000FC068
#define GL_MDET_TX_TCLAN_QNUM_S			0
#define GL_MDET_TX_TCLAN_QNUM_M			ICE_M(0x7FFF, 0)
#define GL_MDET_TX_TCLAN_VF_NUM_S		15
#define GL_MDET_TX_TCLAN_VF_NUM_M		ICE_M(0xFF, 15)
#define GL_MDET_TX_TCLAN_PF_NUM_S		23
#define GL_MDET_TX_TCLAN_PF_NUM_M		ICE_M(0x7, 23)
#define GL_MDET_TX_TCLAN_MAL_TYPE_S		26
#define GL_MDET_TX_TCLAN_MAL_TYPE_M		ICE_M(0x1F, 26)
#define GL_MDET_TX_TCLAN_VALID_M		BIT(31)
#define PF_MDET_RX				0x00294280
#define PF_MDET_RX_VALID_M			BIT(0)
#define PF_MDET_TX_PQM				0x002D2C80
#define PF_MDET_TX_PQM_VALID_M			BIT(0)
#define PF_MDET_TX_TCLAN			0x000FC000
#define PF_MDET_TX_TCLAN_VALID_M		BIT(0)
#define GLNVM_FLA				0x000B6108
#define GLNVM_FLA_LOCKED_M			BIT(6)
#define GLNVM_GENS				0x000B6100
#define GLNVM_GENS_SR_SIZE_S			5
#define GLNVM_GENS_SR_SIZE_M			ICE_M(0x7, 5)
#define GLNVM_ULD				0x000B6008
#define GLNVM_ULD_CORER_DONE_M			BIT(3)
#define GLNVM_ULD_GLOBR_DONE_M			BIT(4)
#define PF_FUNC_RID				0x0009E880
#define PF_FUNC_RID_FUNC_NUM_S			0
#define PF_FUNC_RID_FUNC_NUM_M			ICE_M(0x7, 0)
#define GL_PWR_MODE_CTL				0x000B820C
#define GL_PWR_MODE_CTL_CAR_MAX_BW_S		30
#define GL_PWR_MODE_CTL_CAR_MAX_BW_M		ICE_M(0x3, 30)
#define GLPRT_BPRCH(_i)				(0x00381384 + ((_i) * 8))
#define GLPRT_BPRCL(_i)				(0x00381380 + ((_i) * 8))
#define GLPRT_BPTCH(_i)				(0x00381244 + ((_i) * 8))
#define GLPRT_BPTCL(_i)				(0x00381240 + ((_i) * 8))
#define GLPRT_CRCERRS(_i)			(0x00380100 + ((_i) * 8))
#define GLPRT_GORCH(_i)				(0x00380004 + ((_i) * 8))
#define GLPRT_GORCL(_i)				(0x00380000 + ((_i) * 8))
#define GLPRT_GOTCH(_i)				(0x00380B44 + ((_i) * 8))
#define GLPRT_GOTCL(_i)				(0x00380B40 + ((_i) * 8))
#define GLPRT_ILLERRC(_i)			(0x003801C0 + ((_i) * 8))
#define GLPRT_LXOFFRXC(_i)			(0x003802C0 + ((_i) * 8))
#define GLPRT_LXOFFTXC(_i)			(0x00381180 + ((_i) * 8))
#define GLPRT_LXONRXC(_i)			(0x00380280 + ((_i) * 8))
#define GLPRT_LXONTXC(_i)			(0x00381140 + ((_i) * 8))
#define GLPRT_MLFC(_i)				(0x00380040 + ((_i) * 8))
#define GLPRT_MPRCH(_i)				(0x00381344 + ((_i) * 8))
#define GLPRT_MPRCL(_i)				(0x00381340 + ((_i) * 8))
#define GLPRT_MPTCH(_i)				(0x00381204 + ((_i) * 8))
#define GLPRT_MPTCL(_i)				(0x00381200 + ((_i) * 8))
#define GLPRT_MRFC(_i)				(0x00380080 + ((_i) * 8))
#define GLPRT_PRC1023H(_i)			(0x00380A04 + ((_i) * 8))
#define GLPRT_PRC1023L(_i)			(0x00380A00 + ((_i) * 8))
#define GLPRT_PRC127H(_i)			(0x00380944 + ((_i) * 8))
#define GLPRT_PRC127L(_i)			(0x00380940 + ((_i) * 8))
#define GLPRT_PRC1522H(_i)			(0x00380A44 + ((_i) * 8))
#define GLPRT_PRC1522L(_i)			(0x00380A40 + ((_i) * 8))
#define GLPRT_PRC255H(_i)			(0x00380984 + ((_i) * 8))
#define GLPRT_PRC255L(_i)			(0x00380980 + ((_i) * 8))
#define GLPRT_PRC511H(_i)			(0x003809C4 + ((_i) * 8))
#define GLPRT_PRC511L(_i)			(0x003809C0 + ((_i) * 8))
#define GLPRT_PRC64H(_i)			(0x00380904 + ((_i) * 8))
#define GLPRT_PRC64L(_i)			(0x00380900 + ((_i) * 8))
#define GLPRT_PRC9522H(_i)			(0x00380A84 + ((_i) * 8))
#define GLPRT_PRC9522L(_i)			(0x00380A80 + ((_i) * 8))
#define GLPRT_PTC1023H(_i)			(0x00380C84 + ((_i) * 8))
#define GLPRT_PTC1023L(_i)			(0x00380C80 + ((_i) * 8))
#define GLPRT_PTC127H(_i)			(0x00380BC4 + ((_i) * 8))
#define GLPRT_PTC127L(_i)			(0x00380BC0 + ((_i) * 8))
#define GLPRT_PTC1522H(_i)			(0x00380CC4 + ((_i) * 8))
#define GLPRT_PTC1522L(_i)			(0x00380CC0 + ((_i) * 8))
#define GLPRT_PTC255H(_i)			(0x00380C04 + ((_i) * 8))
#define GLPRT_PTC255L(_i)			(0x00380C00 + ((_i) * 8))
#define GLPRT_PTC511H(_i)			(0x00380C44 + ((_i) * 8))
#define GLPRT_PTC511L(_i)			(0x00380C40 + ((_i) * 8))
#define GLPRT_PTC64H(_i)			(0x00380B84 + ((_i) * 8))
#define GLPRT_PTC64L(_i)			(0x00380B80 + ((_i) * 8))
#define GLPRT_PTC9522H(_i)			(0x00380D04 + ((_i) * 8))
#define GLPRT_PTC9522L(_i)			(0x00380D00 + ((_i) * 8))
#define GLPRT_RFC(_i)				(0x00380AC0 + ((_i) * 8))
#define GLPRT_RJC(_i)				(0x00380B00 + ((_i) * 8))
#define GLPRT_RLEC(_i)				(0x00380140 + ((_i) * 8))
#define GLPRT_ROC(_i)				(0x00380240 + ((_i) * 8))
#define GLPRT_RUC(_i)				(0x00380200 + ((_i) * 8))
#define GLPRT_TDOLD(_i)				(0x00381280 + ((_i) * 8))
#define GLPRT_UPRCH(_i)				(0x00381304 + ((_i) * 8))
#define GLPRT_UPRCL(_i)				(0x00381300 + ((_i) * 8))
#define GLPRT_UPTCH(_i)				(0x003811C4 + ((_i) * 8))
#define GLPRT_UPTCL(_i)				(0x003811C0 + ((_i) * 8))
#define GLV_BPRCH(_i)				(0x003B6004 + ((_i) * 8))
#define GLV_BPRCL(_i)				(0x003B6000 + ((_i) * 8))
#define GLV_BPTCH(_i)				(0x0030E004 + ((_i) * 8))
#define GLV_BPTCL(_i)				(0x0030E000 + ((_i) * 8))
#define GLV_GORCH(_i)				(0x003B0004 + ((_i) * 8))
#define GLV_GORCL(_i)				(0x003B0000 + ((_i) * 8))
#define GLV_GOTCH(_i)				(0x00300004 + ((_i) * 8))
#define GLV_GOTCL(_i)				(0x00300000 + ((_i) * 8))
#define GLV_MPRCH(_i)				(0x003B4004 + ((_i) * 8))
#define GLV_MPRCL(_i)				(0x003B4000 + ((_i) * 8))
#define GLV_MPTCH(_i)				(0x0030C004 + ((_i) * 8))
#define GLV_MPTCL(_i)				(0x0030C000 + ((_i) * 8))
#define GLV_RDPC(_i)				(0x00294C04 + ((_i) * 4))
#define GLV_TEPC(_VSI)				(0x00312000 + ((_VSI) * 4))
#define GLV_UPRCH(_i)				(0x003B2004 + ((_i) * 8))
#define GLV_UPRCL(_i)				(0x003B2000 + ((_i) * 8))
#define GLV_UPTCH(_i)				(0x0030A004 + ((_i) * 8))
#define GLV_UPTCL(_i)				(0x0030A000 + ((_i) * 8))
#define VSIQF_HKEY_MAX_INDEX			12

#endif /* _ICE_HW_AUTOGEN_H_ */
