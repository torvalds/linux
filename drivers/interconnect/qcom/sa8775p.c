// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021-2022, Qualcomm Innovation Center, Inc. All rights reserved.
 * Copyright (c) 2023, Linaro Limited
 */

#include <linux/device.h>
#include <linux/interconnect.h>
#include <linux/interconnect-provider.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <dt-bindings/interconnect/qcom,sa8775p-rpmh.h>

#include "bcm-voter.h"
#include "icc-rpmh.h"

#define SA8775P_MASTER_GPU_TCU				0
#define SA8775P_MASTER_PCIE_TCU				1
#define SA8775P_MASTER_SYS_TCU				2
#define SA8775P_MASTER_APPSS_PROC			3
#define SA8775P_MASTER_LLCC				4
#define SA8775P_MASTER_CNOC_LPASS_AG_NOC		5
#define SA8775P_MASTER_GIC_AHB				6
#define SA8775P_MASTER_CDSP_NOC_CFG			7
#define SA8775P_MASTER_CDSPB_NOC_CFG			8
#define SA8775P_MASTER_QDSS_BAM				9
#define SA8775P_MASTER_QUP_0				10
#define SA8775P_MASTER_QUP_1				11
#define SA8775P_MASTER_QUP_2				12
#define SA8775P_MASTER_A1NOC_SNOC			13
#define SA8775P_MASTER_A2NOC_SNOC			14
#define SA8775P_MASTER_CAMNOC_HF			15
#define SA8775P_MASTER_CAMNOC_ICP			16
#define SA8775P_MASTER_CAMNOC_SF			17
#define SA8775P_MASTER_COMPUTE_NOC			18
#define SA8775P_MASTER_COMPUTE_NOC_1			19
#define SA8775P_MASTER_CNOC_A2NOC			20
#define SA8775P_MASTER_CNOC_DC_NOC			21
#define SA8775P_MASTER_GEM_NOC_CFG			22
#define SA8775P_MASTER_GEM_NOC_CNOC			23
#define SA8775P_MASTER_GEM_NOC_PCIE_SNOC		24
#define SA8775P_MASTER_GPDSP_SAIL			25
#define SA8775P_MASTER_GFX3D				26
#define SA8775P_MASTER_LPASS_ANOC			27
#define SA8775P_MASTER_MDP0				28
#define SA8775P_MASTER_MDP1				29
#define SA8775P_MASTER_MDP_CORE1_0			30
#define SA8775P_MASTER_MDP_CORE1_1			31
#define SA8775P_MASTER_MNOC_HF_MEM_NOC			32
#define SA8775P_MASTER_CNOC_MNOC_HF_CFG			33
#define SA8775P_MASTER_MNOC_SF_MEM_NOC			34
#define SA8775P_MASTER_CNOC_MNOC_SF_CFG			35
#define SA8775P_MASTER_ANOC_PCIE_GEM_NOC		36
#define SA8775P_MASTER_SNOC_CFG				37
#define SA8775P_MASTER_SNOC_GC_MEM_NOC			38
#define SA8775P_MASTER_SNOC_SF_MEM_NOC			39
#define SA8775P_MASTER_VIDEO_P0				40
#define SA8775P_MASTER_VIDEO_P1				41
#define SA8775P_MASTER_VIDEO_PROC			42
#define SA8775P_MASTER_VIDEO_V_PROC			43
#define SA8775P_MASTER_QUP_CORE_0			44
#define SA8775P_MASTER_QUP_CORE_1			45
#define SA8775P_MASTER_QUP_CORE_2			46
#define SA8775P_MASTER_QUP_CORE_3			47
#define SA8775P_MASTER_CRYPTO_CORE0			48
#define SA8775P_MASTER_CRYPTO_CORE1			49
#define SA8775P_MASTER_DSP0				50
#define SA8775P_MASTER_DSP1				51
#define SA8775P_MASTER_IPA				52
#define SA8775P_MASTER_LPASS_PROC			53
#define SA8775P_MASTER_CDSP_PROC			54
#define SA8775P_MASTER_CDSP_PROC_B			55
#define SA8775P_MASTER_PIMEM				56
#define SA8775P_MASTER_QUP_3				57
#define SA8775P_MASTER_EMAC				58
#define SA8775P_MASTER_EMAC_1				59
#define SA8775P_MASTER_GIC				60
#define SA8775P_MASTER_PCIE_0				61
#define SA8775P_MASTER_PCIE_1				62
#define SA8775P_MASTER_QDSS_ETR_0			63
#define SA8775P_MASTER_QDSS_ETR_1			64
#define SA8775P_MASTER_SDC				65
#define SA8775P_MASTER_UFS_CARD				66
#define SA8775P_MASTER_UFS_MEM				67
#define SA8775P_MASTER_USB2				68
#define SA8775P_MASTER_USB3_0				69
#define SA8775P_MASTER_USB3_1				70
#define SA8775P_SLAVE_EBI1				512
#define SA8775P_SLAVE_AHB2PHY_0				513
#define SA8775P_SLAVE_AHB2PHY_1				514
#define SA8775P_SLAVE_AHB2PHY_2				515
#define SA8775P_SLAVE_AHB2PHY_3				516
#define SA8775P_SLAVE_ANOC_THROTTLE_CFG			517
#define SA8775P_SLAVE_AOSS				518
#define SA8775P_SLAVE_APPSS				519
#define SA8775P_SLAVE_BOOT_ROM				520
#define SA8775P_SLAVE_CAMERA_CFG			521
#define SA8775P_SLAVE_CAMERA_NRT_THROTTLE_CFG		522
#define SA8775P_SLAVE_CAMERA_RT_THROTTLE_CFG		523
#define SA8775P_SLAVE_CLK_CTL				524
#define SA8775P_SLAVE_CDSP_CFG				525
#define SA8775P_SLAVE_CDSP1_CFG				526
#define SA8775P_SLAVE_RBCPR_CX_CFG			527
#define SA8775P_SLAVE_RBCPR_MMCX_CFG			528
#define SA8775P_SLAVE_RBCPR_MX_CFG			529
#define SA8775P_SLAVE_CPR_NSPCX				530
#define SA8775P_SLAVE_CRYPTO_0_CFG			531
#define SA8775P_SLAVE_CX_RDPM				532
#define SA8775P_SLAVE_DISPLAY_CFG			533
#define SA8775P_SLAVE_DISPLAY_RT_THROTTLE_CFG		534
#define SA8775P_SLAVE_DISPLAY1_CFG			535
#define SA8775P_SLAVE_DISPLAY1_RT_THROTTLE_CFG		536
#define SA8775P_SLAVE_EMAC_CFG				537
#define SA8775P_SLAVE_EMAC1_CFG				538
#define SA8775P_SLAVE_GP_DSP0_CFG			539
#define SA8775P_SLAVE_GP_DSP1_CFG			540
#define SA8775P_SLAVE_GPDSP0_THROTTLE_CFG		541
#define SA8775P_SLAVE_GPDSP1_THROTTLE_CFG		542
#define SA8775P_SLAVE_GPU_TCU_THROTTLE_CFG		543
#define SA8775P_SLAVE_GFX3D_CFG				544
#define SA8775P_SLAVE_HWKM				545
#define SA8775P_SLAVE_IMEM_CFG				546
#define SA8775P_SLAVE_IPA_CFG				547
#define SA8775P_SLAVE_IPC_ROUTER_CFG			548
#define SA8775P_SLAVE_LLCC_CFG				549
#define SA8775P_SLAVE_LPASS				550
#define SA8775P_SLAVE_LPASS_CORE_CFG			551
#define SA8775P_SLAVE_LPASS_LPI_CFG			552
#define SA8775P_SLAVE_LPASS_MPU_CFG			553
#define SA8775P_SLAVE_LPASS_THROTTLE_CFG		554
#define SA8775P_SLAVE_LPASS_TOP_CFG			555
#define SA8775P_SLAVE_MX_RDPM				556
#define SA8775P_SLAVE_MXC_RDPM				557
#define SA8775P_SLAVE_PCIE_0_CFG			558
#define SA8775P_SLAVE_PCIE_1_CFG			559
#define SA8775P_SLAVE_PCIE_RSC_CFG			560
#define SA8775P_SLAVE_PCIE_TCU_THROTTLE_CFG		561
#define SA8775P_SLAVE_PCIE_THROTTLE_CFG			562
#define SA8775P_SLAVE_PDM				563
#define SA8775P_SLAVE_PIMEM_CFG				564
#define SA8775P_SLAVE_PKA_WRAPPER_CFG			565
#define SA8775P_SLAVE_QDSS_CFG				566
#define SA8775P_SLAVE_QM_CFG				567
#define SA8775P_SLAVE_QM_MPU_CFG			568
#define SA8775P_SLAVE_QUP_0				569
#define SA8775P_SLAVE_QUP_1				570
#define SA8775P_SLAVE_QUP_2				571
#define SA8775P_SLAVE_QUP_3				572
#define SA8775P_SLAVE_SAIL_THROTTLE_CFG			573
#define SA8775P_SLAVE_SDC1				574
#define SA8775P_SLAVE_SECURITY				575
#define SA8775P_SLAVE_SNOC_THROTTLE_CFG			576
#define SA8775P_SLAVE_TCSR				577
#define SA8775P_SLAVE_TLMM				578
#define SA8775P_SLAVE_TSC_CFG				579
#define SA8775P_SLAVE_UFS_CARD_CFG			580
#define SA8775P_SLAVE_UFS_MEM_CFG			581
#define SA8775P_SLAVE_USB2				582
#define SA8775P_SLAVE_USB3_0				583
#define SA8775P_SLAVE_USB3_1				584
#define SA8775P_SLAVE_VENUS_CFG				585
#define SA8775P_SLAVE_VENUS_CVP_THROTTLE_CFG		586
#define SA8775P_SLAVE_VENUS_V_CPU_THROTTLE_CFG		587
#define SA8775P_SLAVE_VENUS_VCODEC_THROTTLE_CFG		588
#define SA8775P_SLAVE_A1NOC_SNOC			589
#define SA8775P_SLAVE_A2NOC_SNOC			590
#define SA8775P_SLAVE_DDRSS_CFG				591
#define SA8775P_SLAVE_GEM_NOC_CNOC			592
#define SA8775P_SLAVE_GEM_NOC_CFG			593
#define SA8775P_SLAVE_SNOC_GEM_NOC_GC			594
#define SA8775P_SLAVE_SNOC_GEM_NOC_SF			595
#define SA8775P_SLAVE_GP_DSP_SAIL_NOC			596
#define SA8775P_SLAVE_GPDSP_NOC_CFG			597
#define SA8775P_SLAVE_HCP_A				598
#define SA8775P_SLAVE_LLCC				599
#define SA8775P_SLAVE_MNOC_HF_MEM_NOC			600
#define SA8775P_SLAVE_MNOC_SF_MEM_NOC			601
#define SA8775P_SLAVE_CNOC_MNOC_HF_CFG			602
#define SA8775P_SLAVE_CNOC_MNOC_SF_CFG			603
#define SA8775P_SLAVE_CDSP_MEM_NOC			604
#define SA8775P_SLAVE_CDSPB_MEM_NOC			605
#define SA8775P_SLAVE_HCP_B				606
#define SA8775P_SLAVE_GEM_NOC_PCIE_CNOC			607
#define SA8775P_SLAVE_PCIE_ANOC_CFG			608
#define SA8775P_SLAVE_ANOC_PCIE_GEM_NOC			609
#define SA8775P_SLAVE_SNOC_CFG				610
#define SA8775P_SLAVE_LPASS_SNOC			611
#define SA8775P_SLAVE_QUP_CORE_0			612
#define SA8775P_SLAVE_QUP_CORE_1			613
#define SA8775P_SLAVE_QUP_CORE_2			614
#define SA8775P_SLAVE_QUP_CORE_3			615
#define SA8775P_SLAVE_BOOT_IMEM				616
#define SA8775P_SLAVE_IMEM				617
#define SA8775P_SLAVE_PIMEM				618
#define SA8775P_SLAVE_SERVICE_NSP_NOC			619
#define SA8775P_SLAVE_SERVICE_NSPB_NOC			620
#define SA8775P_SLAVE_SERVICE_GEM_NOC_1			621
#define SA8775P_SLAVE_SERVICE_MNOC_HF			622
#define SA8775P_SLAVE_SERVICE_MNOC_SF			623
#define SA8775P_SLAVE_SERVICES_LPASS_AML_NOC		624
#define SA8775P_SLAVE_SERVICE_LPASS_AG_NOC		625
#define SA8775P_SLAVE_SERVICE_GEM_NOC_2			626
#define SA8775P_SLAVE_SERVICE_SNOC			627
#define SA8775P_SLAVE_SERVICE_GEM_NOC			628
#define SA8775P_SLAVE_SERVICE_GEM_NOC2			629
#define SA8775P_SLAVE_PCIE_0				630
#define SA8775P_SLAVE_PCIE_1				631
#define SA8775P_SLAVE_QDSS_STM				632
#define SA8775P_SLAVE_TCU				633

static struct qcom_icc_node qxm_qup3 = {
	.name = "qxm_qup3",
	.id = SA8775P_MASTER_QUP_3,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SA8775P_SLAVE_A1NOC_SNOC },
};

static struct qcom_icc_node xm_emac_0 = {
	.name = "xm_emac_0",
	.id = SA8775P_MASTER_EMAC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SA8775P_SLAVE_A1NOC_SNOC },
};

static struct qcom_icc_node xm_emac_1 = {
	.name = "xm_emac_1",
	.id = SA8775P_MASTER_EMAC_1,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SA8775P_SLAVE_A1NOC_SNOC },
};

static struct qcom_icc_node xm_sdc1 = {
	.name = "xm_sdc1",
	.id = SA8775P_MASTER_SDC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SA8775P_SLAVE_A1NOC_SNOC },
};

static struct qcom_icc_node xm_ufs_mem = {
	.name = "xm_ufs_mem",
	.id = SA8775P_MASTER_UFS_MEM,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SA8775P_SLAVE_A1NOC_SNOC },
};

static struct qcom_icc_node xm_usb2_2 = {
	.name = "xm_usb2_2",
	.id = SA8775P_MASTER_USB2,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SA8775P_SLAVE_A1NOC_SNOC },
};

static struct qcom_icc_node xm_usb3_0 = {
	.name = "xm_usb3_0",
	.id = SA8775P_MASTER_USB3_0,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SA8775P_SLAVE_A1NOC_SNOC },
};

static struct qcom_icc_node xm_usb3_1 = {
	.name = "xm_usb3_1",
	.id = SA8775P_MASTER_USB3_1,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SA8775P_SLAVE_A1NOC_SNOC },
};

static struct qcom_icc_node qhm_qdss_bam = {
	.name = "qhm_qdss_bam",
	.id = SA8775P_MASTER_QDSS_BAM,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SA8775P_SLAVE_A2NOC_SNOC },
};

static struct qcom_icc_node qhm_qup0 = {
	.name = "qhm_qup0",
	.id = SA8775P_MASTER_QUP_0,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SA8775P_SLAVE_A2NOC_SNOC },
};

static struct qcom_icc_node qhm_qup1 = {
	.name = "qhm_qup1",
	.id = SA8775P_MASTER_QUP_1,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SA8775P_SLAVE_A2NOC_SNOC },
};

static struct qcom_icc_node qhm_qup2 = {
	.name = "qhm_qup2",
	.id = SA8775P_MASTER_QUP_2,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SA8775P_SLAVE_A2NOC_SNOC },
};

static struct qcom_icc_node qnm_cnoc_datapath = {
	.name = "qnm_cnoc_datapath",
	.id = SA8775P_MASTER_CNOC_A2NOC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SA8775P_SLAVE_A2NOC_SNOC },
};

static struct qcom_icc_node qxm_crypto_0 = {
	.name = "qxm_crypto_0",
	.id = SA8775P_MASTER_CRYPTO_CORE0,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SA8775P_SLAVE_A2NOC_SNOC },
};

static struct qcom_icc_node qxm_crypto_1 = {
	.name = "qxm_crypto_1",
	.id = SA8775P_MASTER_CRYPTO_CORE1,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SA8775P_SLAVE_A2NOC_SNOC },
};

static struct qcom_icc_node qxm_ipa = {
	.name = "qxm_ipa",
	.id = SA8775P_MASTER_IPA,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SA8775P_SLAVE_A2NOC_SNOC },
};

static struct qcom_icc_node xm_qdss_etr_0 = {
	.name = "xm_qdss_etr_0",
	.id = SA8775P_MASTER_QDSS_ETR_0,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SA8775P_SLAVE_A2NOC_SNOC },
};

static struct qcom_icc_node xm_qdss_etr_1 = {
	.name = "xm_qdss_etr_1",
	.id = SA8775P_MASTER_QDSS_ETR_1,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SA8775P_SLAVE_A2NOC_SNOC },
};

static struct qcom_icc_node xm_ufs_card = {
	.name = "xm_ufs_card",
	.id = SA8775P_MASTER_UFS_CARD,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SA8775P_SLAVE_A2NOC_SNOC },
};

static struct qcom_icc_node qup0_core_master = {
	.name = "qup0_core_master",
	.id = SA8775P_MASTER_QUP_CORE_0,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SA8775P_SLAVE_QUP_CORE_0 },
};

static struct qcom_icc_node qup1_core_master = {
	.name = "qup1_core_master",
	.id = SA8775P_MASTER_QUP_CORE_1,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SA8775P_SLAVE_QUP_CORE_1 },
};

static struct qcom_icc_node qup2_core_master = {
	.name = "qup2_core_master",
	.id = SA8775P_MASTER_QUP_CORE_2,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SA8775P_SLAVE_QUP_CORE_2 },
};

static struct qcom_icc_node qup3_core_master = {
	.name = "qup3_core_master",
	.id = SA8775P_MASTER_QUP_CORE_3,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SA8775P_SLAVE_QUP_CORE_3 },
};

static struct qcom_icc_node qnm_gemnoc_cnoc = {
	.name = "qnm_gemnoc_cnoc",
	.id = SA8775P_MASTER_GEM_NOC_CNOC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 82,
	.links = { SA8775P_SLAVE_AHB2PHY_0,
		   SA8775P_SLAVE_AHB2PHY_1,
		   SA8775P_SLAVE_AHB2PHY_2,
		   SA8775P_SLAVE_AHB2PHY_3,
		   SA8775P_SLAVE_ANOC_THROTTLE_CFG,
		   SA8775P_SLAVE_AOSS,
		   SA8775P_SLAVE_APPSS,
		   SA8775P_SLAVE_BOOT_ROM,
		   SA8775P_SLAVE_CAMERA_CFG,
		   SA8775P_SLAVE_CAMERA_NRT_THROTTLE_CFG,
		   SA8775P_SLAVE_CAMERA_RT_THROTTLE_CFG,
		   SA8775P_SLAVE_CLK_CTL,
		   SA8775P_SLAVE_CDSP_CFG,
		   SA8775P_SLAVE_CDSP1_CFG,
		   SA8775P_SLAVE_RBCPR_CX_CFG,
		   SA8775P_SLAVE_RBCPR_MMCX_CFG,
		   SA8775P_SLAVE_RBCPR_MX_CFG,
		   SA8775P_SLAVE_CPR_NSPCX,
		   SA8775P_SLAVE_CRYPTO_0_CFG,
		   SA8775P_SLAVE_CX_RDPM,
		   SA8775P_SLAVE_DISPLAY_CFG,
		   SA8775P_SLAVE_DISPLAY_RT_THROTTLE_CFG,
		   SA8775P_SLAVE_DISPLAY1_CFG,
		   SA8775P_SLAVE_DISPLAY1_RT_THROTTLE_CFG,
		   SA8775P_SLAVE_EMAC_CFG,
		   SA8775P_SLAVE_EMAC1_CFG,
		   SA8775P_SLAVE_GP_DSP0_CFG,
		   SA8775P_SLAVE_GP_DSP1_CFG,
		   SA8775P_SLAVE_GPDSP0_THROTTLE_CFG,
		   SA8775P_SLAVE_GPDSP1_THROTTLE_CFG,
		   SA8775P_SLAVE_GPU_TCU_THROTTLE_CFG,
		   SA8775P_SLAVE_GFX3D_CFG,
		   SA8775P_SLAVE_HWKM,
		   SA8775P_SLAVE_IMEM_CFG,
		   SA8775P_SLAVE_IPA_CFG,
		   SA8775P_SLAVE_IPC_ROUTER_CFG,
		   SA8775P_SLAVE_LPASS,
		   SA8775P_SLAVE_LPASS_THROTTLE_CFG,
		   SA8775P_SLAVE_MX_RDPM,
		   SA8775P_SLAVE_MXC_RDPM,
		   SA8775P_SLAVE_PCIE_0_CFG,
		   SA8775P_SLAVE_PCIE_1_CFG,
		   SA8775P_SLAVE_PCIE_RSC_CFG,
		   SA8775P_SLAVE_PCIE_TCU_THROTTLE_CFG,
		   SA8775P_SLAVE_PCIE_THROTTLE_CFG,
		   SA8775P_SLAVE_PDM,
		   SA8775P_SLAVE_PIMEM_CFG,
		   SA8775P_SLAVE_PKA_WRAPPER_CFG,
		   SA8775P_SLAVE_QDSS_CFG,
		   SA8775P_SLAVE_QM_CFG,
		   SA8775P_SLAVE_QM_MPU_CFG,
		   SA8775P_SLAVE_QUP_0,
		   SA8775P_SLAVE_QUP_1,
		   SA8775P_SLAVE_QUP_2,
		   SA8775P_SLAVE_QUP_3,
		   SA8775P_SLAVE_SAIL_THROTTLE_CFG,
		   SA8775P_SLAVE_SDC1,
		   SA8775P_SLAVE_SECURITY,
		   SA8775P_SLAVE_SNOC_THROTTLE_CFG,
		   SA8775P_SLAVE_TCSR,
		   SA8775P_SLAVE_TLMM,
		   SA8775P_SLAVE_TSC_CFG,
		   SA8775P_SLAVE_UFS_CARD_CFG,
		   SA8775P_SLAVE_UFS_MEM_CFG,
		   SA8775P_SLAVE_USB2,
		   SA8775P_SLAVE_USB3_0,
		   SA8775P_SLAVE_USB3_1,
		   SA8775P_SLAVE_VENUS_CFG,
		   SA8775P_SLAVE_VENUS_CVP_THROTTLE_CFG,
		   SA8775P_SLAVE_VENUS_V_CPU_THROTTLE_CFG,
		   SA8775P_SLAVE_VENUS_VCODEC_THROTTLE_CFG,
		   SA8775P_SLAVE_DDRSS_CFG,
		   SA8775P_SLAVE_GPDSP_NOC_CFG,
		   SA8775P_SLAVE_CNOC_MNOC_HF_CFG,
		   SA8775P_SLAVE_CNOC_MNOC_SF_CFG,
		   SA8775P_SLAVE_PCIE_ANOC_CFG,
		   SA8775P_SLAVE_SNOC_CFG,
		   SA8775P_SLAVE_BOOT_IMEM,
		   SA8775P_SLAVE_IMEM,
		   SA8775P_SLAVE_PIMEM,
		   SA8775P_SLAVE_QDSS_STM,
		   SA8775P_SLAVE_TCU
	},
};

static struct qcom_icc_node qnm_gemnoc_pcie = {
	.name = "qnm_gemnoc_pcie",
	.id = SA8775P_MASTER_GEM_NOC_PCIE_SNOC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 2,
	.links = { SA8775P_SLAVE_PCIE_0,
		   SA8775P_SLAVE_PCIE_1
	},
};

static struct qcom_icc_node qnm_cnoc_dc_noc = {
	.name = "qnm_cnoc_dc_noc",
	.id = SA8775P_MASTER_CNOC_DC_NOC,
	.channels = 1,
	.buswidth = 4,
	.num_links = 2,
	.links = { SA8775P_SLAVE_LLCC_CFG,
		   SA8775P_SLAVE_GEM_NOC_CFG
	},
};

static struct qcom_icc_node alm_gpu_tcu = {
	.name = "alm_gpu_tcu",
	.id = SA8775P_MASTER_GPU_TCU,
	.channels = 1,
	.buswidth = 8,
	.num_links = 2,
	.links = { SA8775P_SLAVE_GEM_NOC_CNOC,
		   SA8775P_SLAVE_LLCC
	},
};

static struct qcom_icc_node alm_pcie_tcu = {
	.name = "alm_pcie_tcu",
	.id = SA8775P_MASTER_PCIE_TCU,
	.channels = 1,
	.buswidth = 8,
	.num_links = 2,
	.links = { SA8775P_SLAVE_GEM_NOC_CNOC,
		   SA8775P_SLAVE_LLCC
	},
};

static struct qcom_icc_node alm_sys_tcu = {
	.name = "alm_sys_tcu",
	.id = SA8775P_MASTER_SYS_TCU,
	.channels = 1,
	.buswidth = 8,
	.num_links = 2,
	.links = { SA8775P_SLAVE_GEM_NOC_CNOC,
		   SA8775P_SLAVE_LLCC
	},
};

static struct qcom_icc_node chm_apps = {
	.name = "chm_apps",
	.id = SA8775P_MASTER_APPSS_PROC,
	.channels = 4,
	.buswidth = 32,
	.num_links = 3,
	.links = { SA8775P_SLAVE_GEM_NOC_CNOC,
		   SA8775P_SLAVE_LLCC,
		   SA8775P_SLAVE_GEM_NOC_PCIE_CNOC
	},
};

static struct qcom_icc_node qnm_cmpnoc0 = {
	.name = "qnm_cmpnoc0",
	.id = SA8775P_MASTER_COMPUTE_NOC,
	.channels = 2,
	.buswidth = 32,
	.num_links = 2,
	.links = { SA8775P_SLAVE_GEM_NOC_CNOC,
		   SA8775P_SLAVE_LLCC
	},
};

static struct qcom_icc_node qnm_cmpnoc1 = {
	.name = "qnm_cmpnoc1",
	.id = SA8775P_MASTER_COMPUTE_NOC_1,
	.channels = 2,
	.buswidth = 32,
	.num_links = 2,
	.links = { SA8775P_SLAVE_GEM_NOC_CNOC,
		   SA8775P_SLAVE_LLCC
	},
};

static struct qcom_icc_node qnm_gemnoc_cfg = {
	.name = "qnm_gemnoc_cfg",
	.id = SA8775P_MASTER_GEM_NOC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 4,
	.links = { SA8775P_SLAVE_SERVICE_GEM_NOC_1,
		   SA8775P_SLAVE_SERVICE_GEM_NOC_2,
		   SA8775P_SLAVE_SERVICE_GEM_NOC,
		   SA8775P_SLAVE_SERVICE_GEM_NOC2
	},
};

static struct qcom_icc_node qnm_gpdsp_sail = {
	.name = "qnm_gpdsp_sail",
	.id = SA8775P_MASTER_GPDSP_SAIL,
	.channels = 1,
	.buswidth = 16,
	.num_links = 2,
	.links = { SA8775P_SLAVE_GEM_NOC_CNOC,
		   SA8775P_SLAVE_LLCC
	},
};

static struct qcom_icc_node qnm_gpu = {
	.name = "qnm_gpu",
	.id = SA8775P_MASTER_GFX3D,
	.channels = 2,
	.buswidth = 32,
	.num_links = 2,
	.links = { SA8775P_SLAVE_GEM_NOC_CNOC,
		   SA8775P_SLAVE_LLCC
	},
};

static struct qcom_icc_node qnm_mnoc_hf = {
	.name = "qnm_mnoc_hf",
	.id = SA8775P_MASTER_MNOC_HF_MEM_NOC,
	.channels = 2,
	.buswidth = 32,
	.num_links = 2,
	.links = { SA8775P_SLAVE_LLCC,
		   SA8775P_SLAVE_GEM_NOC_PCIE_CNOC
	},
};

static struct qcom_icc_node qnm_mnoc_sf = {
	.name = "qnm_mnoc_sf",
	.id = SA8775P_MASTER_MNOC_SF_MEM_NOC,
	.channels = 2,
	.buswidth = 32,
	.num_links = 3,
	.links = { SA8775P_SLAVE_GEM_NOC_CNOC,
		   SA8775P_SLAVE_LLCC,
		   SA8775P_SLAVE_GEM_NOC_PCIE_CNOC
	},
};

static struct qcom_icc_node qnm_pcie = {
	.name = "qnm_pcie",
	.id = SA8775P_MASTER_ANOC_PCIE_GEM_NOC,
	.channels = 1,
	.buswidth = 32,
	.num_links = 2,
	.links = { SA8775P_SLAVE_GEM_NOC_CNOC,
		   SA8775P_SLAVE_LLCC
	},
};

static struct qcom_icc_node qnm_snoc_gc = {
	.name = "qnm_snoc_gc",
	.id = SA8775P_MASTER_SNOC_GC_MEM_NOC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SA8775P_SLAVE_LLCC },
};

static struct qcom_icc_node qnm_snoc_sf = {
	.name = "qnm_snoc_sf",
	.id = SA8775P_MASTER_SNOC_SF_MEM_NOC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 3,
	.links = { SA8775P_SLAVE_GEM_NOC_CNOC,
		   SA8775P_SLAVE_LLCC,
		   SA8775P_SLAVE_GEM_NOC_PCIE_CNOC },
};

static struct qcom_icc_node qxm_dsp0 = {
	.name = "qxm_dsp0",
	.id = SA8775P_MASTER_DSP0,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SA8775P_SLAVE_GP_DSP_SAIL_NOC },
};

static struct qcom_icc_node qxm_dsp1 = {
	.name = "qxm_dsp1",
	.id = SA8775P_MASTER_DSP1,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SA8775P_SLAVE_GP_DSP_SAIL_NOC },
};

static struct qcom_icc_node qhm_config_noc = {
	.name = "qhm_config_noc",
	.id = SA8775P_MASTER_CNOC_LPASS_AG_NOC,
	.channels = 1,
	.buswidth = 4,
	.num_links = 6,
	.links = { SA8775P_SLAVE_LPASS_CORE_CFG,
		   SA8775P_SLAVE_LPASS_LPI_CFG,
		   SA8775P_SLAVE_LPASS_MPU_CFG,
		   SA8775P_SLAVE_LPASS_TOP_CFG,
		   SA8775P_SLAVE_SERVICES_LPASS_AML_NOC,
		   SA8775P_SLAVE_SERVICE_LPASS_AG_NOC
	},
};

static struct qcom_icc_node qxm_lpass_dsp = {
	.name = "qxm_lpass_dsp",
	.id = SA8775P_MASTER_LPASS_PROC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 4,
	.links = { SA8775P_SLAVE_LPASS_TOP_CFG,
		   SA8775P_SLAVE_LPASS_SNOC,
		   SA8775P_SLAVE_SERVICES_LPASS_AML_NOC,
		   SA8775P_SLAVE_SERVICE_LPASS_AG_NOC
	},
};

static struct qcom_icc_node llcc_mc = {
	.name = "llcc_mc",
	.id = SA8775P_MASTER_LLCC,
	.channels = 8,
	.buswidth = 4,
	.num_links = 1,
	.links = { SA8775P_SLAVE_EBI1 },
};

static struct qcom_icc_node qnm_camnoc_hf = {
	.name = "qnm_camnoc_hf",
	.id = SA8775P_MASTER_CAMNOC_HF,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SA8775P_SLAVE_MNOC_HF_MEM_NOC },
};

static struct qcom_icc_node qnm_camnoc_icp = {
	.name = "qnm_camnoc_icp",
	.id = SA8775P_MASTER_CAMNOC_ICP,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SA8775P_SLAVE_MNOC_SF_MEM_NOC },
};

static struct qcom_icc_node qnm_camnoc_sf = {
	.name = "qnm_camnoc_sf",
	.id = SA8775P_MASTER_CAMNOC_SF,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SA8775P_SLAVE_MNOC_SF_MEM_NOC },
};

static struct qcom_icc_node qnm_mdp0_0 = {
	.name = "qnm_mdp0_0",
	.id = SA8775P_MASTER_MDP0,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SA8775P_SLAVE_MNOC_HF_MEM_NOC },
};

static struct qcom_icc_node qnm_mdp0_1 = {
	.name = "qnm_mdp0_1",
	.id = SA8775P_MASTER_MDP1,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SA8775P_SLAVE_MNOC_HF_MEM_NOC },
};

static struct qcom_icc_node qnm_mdp1_0 = {
	.name = "qnm_mdp1_0",
	.id = SA8775P_MASTER_MDP_CORE1_0,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SA8775P_SLAVE_MNOC_HF_MEM_NOC },
};

static struct qcom_icc_node qnm_mdp1_1 = {
	.name = "qnm_mdp1_1",
	.id = SA8775P_MASTER_MDP_CORE1_1,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SA8775P_SLAVE_MNOC_HF_MEM_NOC },
};

static struct qcom_icc_node qnm_mnoc_hf_cfg = {
	.name = "qnm_mnoc_hf_cfg",
	.id = SA8775P_MASTER_CNOC_MNOC_HF_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SA8775P_SLAVE_SERVICE_MNOC_HF },
};

static struct qcom_icc_node qnm_mnoc_sf_cfg = {
	.name = "qnm_mnoc_sf_cfg",
	.id = SA8775P_MASTER_CNOC_MNOC_SF_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SA8775P_SLAVE_SERVICE_MNOC_SF },
};

static struct qcom_icc_node qnm_video0 = {
	.name = "qnm_video0",
	.id = SA8775P_MASTER_VIDEO_P0,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SA8775P_SLAVE_MNOC_SF_MEM_NOC },
};

static struct qcom_icc_node qnm_video1 = {
	.name = "qnm_video1",
	.id = SA8775P_MASTER_VIDEO_P1,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SA8775P_SLAVE_MNOC_SF_MEM_NOC },
};

static struct qcom_icc_node qnm_video_cvp = {
	.name = "qnm_video_cvp",
	.id = SA8775P_MASTER_VIDEO_PROC,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SA8775P_SLAVE_MNOC_SF_MEM_NOC },
};

static struct qcom_icc_node qnm_video_v_cpu = {
	.name = "qnm_video_v_cpu",
	.id = SA8775P_MASTER_VIDEO_V_PROC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SA8775P_SLAVE_MNOC_SF_MEM_NOC },
};

static struct qcom_icc_node qhm_nsp_noc_config = {
	.name = "qhm_nsp_noc_config",
	.id = SA8775P_MASTER_CDSP_NOC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SA8775P_SLAVE_SERVICE_NSP_NOC },
};

static struct qcom_icc_node qxm_nsp = {
	.name = "qxm_nsp",
	.id = SA8775P_MASTER_CDSP_PROC,
	.channels = 2,
	.buswidth = 32,
	.num_links = 2,
	.links = { SA8775P_SLAVE_HCP_A, SLAVE_CDSP_MEM_NOC },
};

static struct qcom_icc_node qhm_nspb_noc_config = {
	.name = "qhm_nspb_noc_config",
	.id = SA8775P_MASTER_CDSPB_NOC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SA8775P_SLAVE_SERVICE_NSPB_NOC },
};

static struct qcom_icc_node qxm_nspb = {
	.name = "qxm_nspb",
	.id = SA8775P_MASTER_CDSP_PROC_B,
	.channels = 2,
	.buswidth = 32,
	.num_links = 2,
	.links = { SA8775P_SLAVE_HCP_B, SLAVE_CDSPB_MEM_NOC },
};

static struct qcom_icc_node xm_pcie3_0 = {
	.name = "xm_pcie3_0",
	.id = SA8775P_MASTER_PCIE_0,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SA8775P_SLAVE_ANOC_PCIE_GEM_NOC },
};

static struct qcom_icc_node xm_pcie3_1 = {
	.name = "xm_pcie3_1",
	.id = SA8775P_MASTER_PCIE_1,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SA8775P_SLAVE_ANOC_PCIE_GEM_NOC },
};

static struct qcom_icc_node qhm_gic = {
	.name = "qhm_gic",
	.id = SA8775P_MASTER_GIC_AHB,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SA8775P_SLAVE_SNOC_GEM_NOC_SF },
};

static struct qcom_icc_node qnm_aggre1_noc = {
	.name = "qnm_aggre1_noc",
	.id = SA8775P_MASTER_A1NOC_SNOC,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SA8775P_SLAVE_SNOC_GEM_NOC_SF },
};

static struct qcom_icc_node qnm_aggre2_noc = {
	.name = "qnm_aggre2_noc",
	.id = SA8775P_MASTER_A2NOC_SNOC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SA8775P_SLAVE_SNOC_GEM_NOC_SF },
};

static struct qcom_icc_node qnm_lpass_noc = {
	.name = "qnm_lpass_noc",
	.id = SA8775P_MASTER_LPASS_ANOC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SA8775P_SLAVE_SNOC_GEM_NOC_SF },
};

static struct qcom_icc_node qnm_snoc_cfg = {
	.name = "qnm_snoc_cfg",
	.id = SA8775P_MASTER_SNOC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SA8775P_SLAVE_SERVICE_SNOC },
};

static struct qcom_icc_node qxm_pimem = {
	.name = "qxm_pimem",
	.id = SA8775P_MASTER_PIMEM,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SA8775P_SLAVE_SNOC_GEM_NOC_GC },
};

static struct qcom_icc_node xm_gic = {
	.name = "xm_gic",
	.id = SA8775P_MASTER_GIC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SA8775P_SLAVE_SNOC_GEM_NOC_GC },
};

static struct qcom_icc_node qns_a1noc_snoc = {
	.name = "qns_a1noc_snoc",
	.id = SA8775P_SLAVE_A1NOC_SNOC,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SA8775P_MASTER_A1NOC_SNOC },
};

static struct qcom_icc_node qns_a2noc_snoc = {
	.name = "qns_a2noc_snoc",
	.id = SA8775P_SLAVE_A2NOC_SNOC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SA8775P_MASTER_A2NOC_SNOC },
};

static struct qcom_icc_node qup0_core_slave = {
	.name = "qup0_core_slave",
	.id = SA8775P_SLAVE_QUP_CORE_0,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qup1_core_slave = {
	.name = "qup1_core_slave",
	.id = SA8775P_SLAVE_QUP_CORE_1,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qup2_core_slave = {
	.name = "qup2_core_slave",
	.id = SA8775P_SLAVE_QUP_CORE_2,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qup3_core_slave = {
	.name = "qup3_core_slave",
	.id = SA8775P_SLAVE_QUP_CORE_3,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_ahb2phy0 = {
	.name = "qhs_ahb2phy0",
	.id = SA8775P_SLAVE_AHB2PHY_0,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_ahb2phy1 = {
	.name = "qhs_ahb2phy1",
	.id = SA8775P_SLAVE_AHB2PHY_1,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_ahb2phy2 = {
	.name = "qhs_ahb2phy2",
	.id = SA8775P_SLAVE_AHB2PHY_2,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_ahb2phy3 = {
	.name = "qhs_ahb2phy3",
	.id = SA8775P_SLAVE_AHB2PHY_3,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_anoc_throttle_cfg = {
	.name = "qhs_anoc_throttle_cfg",
	.id = SA8775P_SLAVE_ANOC_THROTTLE_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_aoss = {
	.name = "qhs_aoss",
	.id = SA8775P_SLAVE_AOSS,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_apss = {
	.name = "qhs_apss",
	.id = SA8775P_SLAVE_APPSS,
	.channels = 1,
	.buswidth = 8,
};

static struct qcom_icc_node qhs_boot_rom = {
	.name = "qhs_boot_rom",
	.id = SA8775P_SLAVE_BOOT_ROM,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_camera_cfg = {
	.name = "qhs_camera_cfg",
	.id = SA8775P_SLAVE_CAMERA_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_camera_nrt_throttle_cfg = {
	.name = "qhs_camera_nrt_throttle_cfg",
	.id = SA8775P_SLAVE_CAMERA_NRT_THROTTLE_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_camera_rt_throttle_cfg = {
	.name = "qhs_camera_rt_throttle_cfg",
	.id = SA8775P_SLAVE_CAMERA_RT_THROTTLE_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_clk_ctl = {
	.name = "qhs_clk_ctl",
	.id = SA8775P_SLAVE_CLK_CTL,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_compute0_cfg = {
	.name = "qhs_compute0_cfg",
	.id = SA8775P_SLAVE_CDSP_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SA8775P_MASTER_CDSP_NOC_CFG },
};

static struct qcom_icc_node qhs_compute1_cfg = {
	.name = "qhs_compute1_cfg",
	.id = SA8775P_SLAVE_CDSP1_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SA8775P_MASTER_CDSPB_NOC_CFG },
};

static struct qcom_icc_node qhs_cpr_cx = {
	.name = "qhs_cpr_cx",
	.id = SA8775P_SLAVE_RBCPR_CX_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_cpr_mmcx = {
	.name = "qhs_cpr_mmcx",
	.id = SA8775P_SLAVE_RBCPR_MMCX_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_cpr_mx = {
	.name = "qhs_cpr_mx",
	.id = SA8775P_SLAVE_RBCPR_MX_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_cpr_nspcx = {
	.name = "qhs_cpr_nspcx",
	.id = SA8775P_SLAVE_CPR_NSPCX,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_crypto0_cfg = {
	.name = "qhs_crypto0_cfg",
	.id = SA8775P_SLAVE_CRYPTO_0_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_cx_rdpm = {
	.name = "qhs_cx_rdpm",
	.id = SA8775P_SLAVE_CX_RDPM,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_display0_cfg = {
	.name = "qhs_display0_cfg",
	.id = SA8775P_SLAVE_DISPLAY_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_display0_rt_throttle_cfg = {
	.name = "qhs_display0_rt_throttle_cfg",
	.id = SA8775P_SLAVE_DISPLAY_RT_THROTTLE_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_display1_cfg = {
	.name = "qhs_display1_cfg",
	.id = SA8775P_SLAVE_DISPLAY1_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_display1_rt_throttle_cfg = {
	.name = "qhs_display1_rt_throttle_cfg",
	.id = SA8775P_SLAVE_DISPLAY1_RT_THROTTLE_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_emac0_cfg = {
	.name = "qhs_emac0_cfg",
	.id = SA8775P_SLAVE_EMAC_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_emac1_cfg = {
	.name = "qhs_emac1_cfg",
	.id = SA8775P_SLAVE_EMAC1_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_gp_dsp0_cfg = {
	.name = "qhs_gp_dsp0_cfg",
	.id = SA8775P_SLAVE_GP_DSP0_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_gp_dsp1_cfg = {
	.name = "qhs_gp_dsp1_cfg",
	.id = SA8775P_SLAVE_GP_DSP1_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_gpdsp0_throttle_cfg = {
	.name = "qhs_gpdsp0_throttle_cfg",
	.id = SA8775P_SLAVE_GPDSP0_THROTTLE_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_gpdsp1_throttle_cfg = {
	.name = "qhs_gpdsp1_throttle_cfg",
	.id = SA8775P_SLAVE_GPDSP1_THROTTLE_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_gpu_tcu_throttle_cfg = {
	.name = "qhs_gpu_tcu_throttle_cfg",
	.id = SA8775P_SLAVE_GPU_TCU_THROTTLE_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_gpuss_cfg = {
	.name = "qhs_gpuss_cfg",
	.id = SA8775P_SLAVE_GFX3D_CFG,
	.channels = 1,
	.buswidth = 8,
};

static struct qcom_icc_node qhs_hwkm = {
	.name = "qhs_hwkm",
	.id = SA8775P_SLAVE_HWKM,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_imem_cfg = {
	.name = "qhs_imem_cfg",
	.id = SA8775P_SLAVE_IMEM_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_ipa = {
	.name = "qhs_ipa",
	.id = SA8775P_SLAVE_IPA_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_ipc_router = {
	.name = "qhs_ipc_router",
	.id = SA8775P_SLAVE_IPC_ROUTER_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_lpass_cfg = {
	.name = "qhs_lpass_cfg",
	.id = SA8775P_SLAVE_LPASS,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SA8775P_MASTER_CNOC_LPASS_AG_NOC },
};

static struct qcom_icc_node qhs_lpass_throttle_cfg = {
	.name = "qhs_lpass_throttle_cfg",
	.id = SA8775P_SLAVE_LPASS_THROTTLE_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_mx_rdpm = {
	.name = "qhs_mx_rdpm",
	.id = SA8775P_SLAVE_MX_RDPM,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_mxc_rdpm = {
	.name = "qhs_mxc_rdpm",
	.id = SA8775P_SLAVE_MXC_RDPM,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_pcie0_cfg = {
	.name = "qhs_pcie0_cfg",
	.id = SA8775P_SLAVE_PCIE_0_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_pcie1_cfg = {
	.name = "qhs_pcie1_cfg",
	.id = SA8775P_SLAVE_PCIE_1_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_pcie_rsc_cfg = {
	.name = "qhs_pcie_rsc_cfg",
	.id = SA8775P_SLAVE_PCIE_RSC_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_pcie_tcu_throttle_cfg = {
	.name = "qhs_pcie_tcu_throttle_cfg",
	.id = SA8775P_SLAVE_PCIE_TCU_THROTTLE_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_pcie_throttle_cfg = {
	.name = "qhs_pcie_throttle_cfg",
	.id = SA8775P_SLAVE_PCIE_THROTTLE_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_pdm = {
	.name = "qhs_pdm",
	.id = SA8775P_SLAVE_PDM,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_pimem_cfg = {
	.name = "qhs_pimem_cfg",
	.id = SA8775P_SLAVE_PIMEM_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_pke_wrapper_cfg = {
	.name = "qhs_pke_wrapper_cfg",
	.id = SA8775P_SLAVE_PKA_WRAPPER_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_qdss_cfg = {
	.name = "qhs_qdss_cfg",
	.id = SA8775P_SLAVE_QDSS_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_qm_cfg = {
	.name = "qhs_qm_cfg",
	.id = SA8775P_SLAVE_QM_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_qm_mpu_cfg = {
	.name = "qhs_qm_mpu_cfg",
	.id = SA8775P_SLAVE_QM_MPU_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_qup0 = {
	.name = "qhs_qup0",
	.id = SA8775P_SLAVE_QUP_0,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_qup1 = {
	.name = "qhs_qup1",
	.id = SA8775P_SLAVE_QUP_1,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_qup2 = {
	.name = "qhs_qup2",
	.id = SA8775P_SLAVE_QUP_2,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_qup3 = {
	.name = "qhs_qup3",
	.id = SA8775P_SLAVE_QUP_3,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_sail_throttle_cfg = {
	.name = "qhs_sail_throttle_cfg",
	.id = SA8775P_SLAVE_SAIL_THROTTLE_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_sdc1 = {
	.name = "qhs_sdc1",
	.id = SA8775P_SLAVE_SDC1,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_security = {
	.name = "qhs_security",
	.id = SA8775P_SLAVE_SECURITY,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_snoc_throttle_cfg = {
	.name = "qhs_snoc_throttle_cfg",
	.id = SA8775P_SLAVE_SNOC_THROTTLE_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_tcsr = {
	.name = "qhs_tcsr",
	.id = SA8775P_SLAVE_TCSR,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_tlmm = {
	.name = "qhs_tlmm",
	.id = SA8775P_SLAVE_TLMM,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_tsc_cfg = {
	.name = "qhs_tsc_cfg",
	.id = SA8775P_SLAVE_TSC_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_ufs_card_cfg = {
	.name = "qhs_ufs_card_cfg",
	.id = SA8775P_SLAVE_UFS_CARD_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_ufs_mem_cfg = {
	.name = "qhs_ufs_mem_cfg",
	.id = SA8775P_SLAVE_UFS_MEM_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_usb2_0 = {
	.name = "qhs_usb2_0",
	.id = SA8775P_SLAVE_USB2,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_usb3_0 = {
	.name = "qhs_usb3_0",
	.id = SA8775P_SLAVE_USB3_0,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_usb3_1 = {
	.name = "qhs_usb3_1",
	.id = SA8775P_SLAVE_USB3_1,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_venus_cfg = {
	.name = "qhs_venus_cfg",
	.id = SA8775P_SLAVE_VENUS_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_venus_cvp_throttle_cfg = {
	.name = "qhs_venus_cvp_throttle_cfg",
	.id = SA8775P_SLAVE_VENUS_CVP_THROTTLE_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_venus_v_cpu_throttle_cfg = {
	.name = "qhs_venus_v_cpu_throttle_cfg",
	.id = SA8775P_SLAVE_VENUS_V_CPU_THROTTLE_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_venus_vcodec_throttle_cfg = {
	.name = "qhs_venus_vcodec_throttle_cfg",
	.id = SA8775P_SLAVE_VENUS_VCODEC_THROTTLE_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qns_ddrss_cfg = {
	.name = "qns_ddrss_cfg",
	.id = SA8775P_SLAVE_DDRSS_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SA8775P_MASTER_CNOC_DC_NOC },
};

static struct qcom_icc_node qns_gpdsp_noc_cfg = {
	.name = "qns_gpdsp_noc_cfg",
	.id = SA8775P_SLAVE_GPDSP_NOC_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qns_mnoc_hf_cfg = {
	.name = "qns_mnoc_hf_cfg",
	.id = SA8775P_SLAVE_CNOC_MNOC_HF_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SA8775P_MASTER_CNOC_MNOC_HF_CFG },
};

static struct qcom_icc_node qns_mnoc_sf_cfg = {
	.name = "qns_mnoc_sf_cfg",
	.id = SA8775P_SLAVE_CNOC_MNOC_SF_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SA8775P_MASTER_CNOC_MNOC_SF_CFG },
};

static struct qcom_icc_node qns_pcie_anoc_cfg = {
	.name = "qns_pcie_anoc_cfg",
	.id = SA8775P_SLAVE_PCIE_ANOC_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qns_snoc_cfg = {
	.name = "qns_snoc_cfg",
	.id = SA8775P_SLAVE_SNOC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SA8775P_MASTER_SNOC_CFG },
};

static struct qcom_icc_node qxs_boot_imem = {
	.name = "qxs_boot_imem",
	.id = SA8775P_SLAVE_BOOT_IMEM,
	.channels = 1,
	.buswidth = 16,
};

static struct qcom_icc_node qxs_imem = {
	.name = "qxs_imem",
	.id = SA8775P_SLAVE_IMEM,
	.channels = 1,
	.buswidth = 8,
};

static struct qcom_icc_node qxs_pimem = {
	.name = "qxs_pimem",
	.id = SA8775P_SLAVE_PIMEM,
	.channels = 1,
	.buswidth = 8,
};

static struct qcom_icc_node xs_pcie_0 = {
	.name = "xs_pcie_0",
	.id = SA8775P_SLAVE_PCIE_0,
	.channels = 1,
	.buswidth = 16,
};

static struct qcom_icc_node xs_pcie_1 = {
	.name = "xs_pcie_1",
	.id = SA8775P_SLAVE_PCIE_1,
	.channels = 1,
	.buswidth = 32,
};

static struct qcom_icc_node xs_qdss_stm = {
	.name = "xs_qdss_stm",
	.id = SA8775P_SLAVE_QDSS_STM,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node xs_sys_tcu_cfg = {
	.name = "xs_sys_tcu_cfg",
	.id = SA8775P_SLAVE_TCU,
	.channels = 1,
	.buswidth = 8,
};

static struct qcom_icc_node qhs_llcc = {
	.name = "qhs_llcc",
	.id = SA8775P_SLAVE_LLCC_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qns_gemnoc = {
	.name = "qns_gemnoc",
	.id = SA8775P_SLAVE_GEM_NOC_CFG,
	.channels = 1,
	.buswidth = 4,
	.num_links = 1,
	.links = { SA8775P_MASTER_GEM_NOC_CFG },
};

static struct qcom_icc_node qns_gem_noc_cnoc = {
	.name = "qns_gem_noc_cnoc",
	.id = SA8775P_SLAVE_GEM_NOC_CNOC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SA8775P_MASTER_GEM_NOC_CNOC },
};

static struct qcom_icc_node qns_llcc = {
	.name = "qns_llcc",
	.id = SA8775P_SLAVE_LLCC,
	.channels = 6,
	.buswidth = 16,
	.num_links = 1,
	.links = { SA8775P_MASTER_LLCC },
};

static struct qcom_icc_node qns_pcie = {
	.name = "qns_pcie",
	.id = SA8775P_SLAVE_GEM_NOC_PCIE_CNOC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SA8775P_MASTER_GEM_NOC_PCIE_SNOC },
};

static struct qcom_icc_node srvc_even_gemnoc = {
	.name = "srvc_even_gemnoc",
	.id = SA8775P_SLAVE_SERVICE_GEM_NOC_1,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node srvc_odd_gemnoc = {
	.name = "srvc_odd_gemnoc",
	.id = SA8775P_SLAVE_SERVICE_GEM_NOC_2,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node srvc_sys_gemnoc = {
	.name = "srvc_sys_gemnoc",
	.id = SA8775P_SLAVE_SERVICE_GEM_NOC,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node srvc_sys_gemnoc_2 = {
	.name = "srvc_sys_gemnoc_2",
	.id = SA8775P_SLAVE_SERVICE_GEM_NOC2,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qns_gp_dsp_sail_noc = {
	.name = "qns_gp_dsp_sail_noc",
	.id = SA8775P_SLAVE_GP_DSP_SAIL_NOC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SA8775P_MASTER_GPDSP_SAIL },
};

static struct qcom_icc_node qhs_lpass_core = {
	.name = "qhs_lpass_core",
	.id = SA8775P_SLAVE_LPASS_CORE_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_lpass_lpi = {
	.name = "qhs_lpass_lpi",
	.id = SA8775P_SLAVE_LPASS_LPI_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_lpass_mpu = {
	.name = "qhs_lpass_mpu",
	.id = SA8775P_SLAVE_LPASS_MPU_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qhs_lpass_top = {
	.name = "qhs_lpass_top",
	.id = SA8775P_SLAVE_LPASS_TOP_CFG,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qns_sysnoc = {
	.name = "qns_sysnoc",
	.id = SA8775P_SLAVE_LPASS_SNOC,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SA8775P_MASTER_LPASS_ANOC },
};

static struct qcom_icc_node srvc_niu_aml_noc = {
	.name = "srvc_niu_aml_noc",
	.id = SA8775P_SLAVE_SERVICES_LPASS_AML_NOC,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node srvc_niu_lpass_agnoc = {
	.name = "srvc_niu_lpass_agnoc",
	.id = SA8775P_SLAVE_SERVICE_LPASS_AG_NOC,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node ebi = {
	.name = "ebi",
	.id = SA8775P_SLAVE_EBI1,
	.channels = 8,
	.buswidth = 4,
};

static struct qcom_icc_node qns_mem_noc_hf = {
	.name = "qns_mem_noc_hf",
	.id = SA8775P_SLAVE_MNOC_HF_MEM_NOC,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { SA8775P_MASTER_MNOC_HF_MEM_NOC },
};

static struct qcom_icc_node qns_mem_noc_sf = {
	.name = "qns_mem_noc_sf",
	.id = SA8775P_SLAVE_MNOC_SF_MEM_NOC,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { SA8775P_MASTER_MNOC_SF_MEM_NOC },
};

static struct qcom_icc_node srvc_mnoc_hf = {
	.name = "srvc_mnoc_hf",
	.id = SA8775P_SLAVE_SERVICE_MNOC_HF,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node srvc_mnoc_sf = {
	.name = "srvc_mnoc_sf",
	.id = SA8775P_SLAVE_SERVICE_MNOC_SF,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qns_hcp = {
	.name = "qns_hcp",
	.id = SA8775P_SLAVE_HCP_A,
	.channels = 2,
	.buswidth = 32,
};

static struct qcom_icc_node qns_nsp_gemnoc = {
	.name = "qns_nsp_gemnoc",
	.id = SA8775P_SLAVE_CDSP_MEM_NOC,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { SA8775P_MASTER_COMPUTE_NOC },
};

static struct qcom_icc_node service_nsp_noc = {
	.name = "service_nsp_noc",
	.id = SA8775P_SLAVE_SERVICE_NSP_NOC,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qns_nspb_gemnoc = {
	.name = "qns_nspb_gemnoc",
	.id = SA8775P_SLAVE_CDSPB_MEM_NOC,
	.channels = 2,
	.buswidth = 32,
	.num_links = 1,
	.links = { SA8775P_MASTER_COMPUTE_NOC_1 },
};

static struct qcom_icc_node qns_nspb_hcp = {
	.name = "qns_nspb_hcp",
	.id = SA8775P_SLAVE_HCP_B,
	.channels = 2,
	.buswidth = 32,
};

static struct qcom_icc_node service_nspb_noc = {
	.name = "service_nspb_noc",
	.id = SA8775P_SLAVE_SERVICE_NSPB_NOC,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_node qns_pcie_mem_noc = {
	.name = "qns_pcie_mem_noc",
	.id = SA8775P_SLAVE_ANOC_PCIE_GEM_NOC,
	.channels = 1,
	.buswidth = 32,
	.num_links = 1,
	.links = { SA8775P_MASTER_ANOC_PCIE_GEM_NOC },
};

static struct qcom_icc_node qns_gemnoc_gc = {
	.name = "qns_gemnoc_gc",
	.id = SA8775P_SLAVE_SNOC_GEM_NOC_GC,
	.channels = 1,
	.buswidth = 8,
	.num_links = 1,
	.links = { SA8775P_MASTER_SNOC_GC_MEM_NOC },
};

static struct qcom_icc_node qns_gemnoc_sf = {
	.name = "qns_gemnoc_sf",
	.id = SA8775P_SLAVE_SNOC_GEM_NOC_SF,
	.channels = 1,
	.buswidth = 16,
	.num_links = 1,
	.links = { SA8775P_MASTER_SNOC_SF_MEM_NOC },
};

static struct qcom_icc_node srvc_snoc = {
	.name = "srvc_snoc",
	.id = SA8775P_SLAVE_SERVICE_SNOC,
	.channels = 1,
	.buswidth = 4,
};

static struct qcom_icc_bcm bcm_acv = {
	.name = "ACV",
	.enable_mask = 0x8,
	.num_nodes = 1,
	.nodes = { &ebi },
};

static struct qcom_icc_bcm bcm_ce0 = {
	.name = "CE0",
	.num_nodes = 2,
	.nodes = { &qxm_crypto_0, &qxm_crypto_1 },
};

static struct qcom_icc_bcm bcm_cn0 = {
	.name = "CN0",
	.keepalive = true,
	.num_nodes = 2,
	.nodes = { &qnm_gemnoc_cnoc, &qnm_gemnoc_pcie },
};

static struct qcom_icc_bcm bcm_cn1 = {
	.name = "CN1",
	.num_nodes = 76,
	.nodes = { &qhs_ahb2phy0, &qhs_ahb2phy1,
		   &qhs_ahb2phy2, &qhs_ahb2phy3,
		   &qhs_anoc_throttle_cfg, &qhs_aoss,
		   &qhs_apss, &qhs_boot_rom,
		   &qhs_camera_cfg, &qhs_camera_nrt_throttle_cfg,
		   &qhs_camera_rt_throttle_cfg, &qhs_clk_ctl,
		   &qhs_compute0_cfg, &qhs_compute1_cfg,
		   &qhs_cpr_cx, &qhs_cpr_mmcx,
		   &qhs_cpr_mx, &qhs_cpr_nspcx,
		   &qhs_crypto0_cfg, &qhs_cx_rdpm,
		   &qhs_display0_cfg, &qhs_display0_rt_throttle_cfg,
		   &qhs_display1_cfg, &qhs_display1_rt_throttle_cfg,
		   &qhs_emac0_cfg, &qhs_emac1_cfg,
		   &qhs_gp_dsp0_cfg, &qhs_gp_dsp1_cfg,
		   &qhs_gpdsp0_throttle_cfg, &qhs_gpdsp1_throttle_cfg,
		   &qhs_gpu_tcu_throttle_cfg, &qhs_gpuss_cfg,
		   &qhs_hwkm, &qhs_imem_cfg,
		   &qhs_ipa, &qhs_ipc_router,
		   &qhs_lpass_cfg, &qhs_lpass_throttle_cfg,
		   &qhs_mx_rdpm, &qhs_mxc_rdpm,
		   &qhs_pcie0_cfg, &qhs_pcie1_cfg,
		   &qhs_pcie_rsc_cfg, &qhs_pcie_tcu_throttle_cfg,
		   &qhs_pcie_throttle_cfg, &qhs_pdm,
		   &qhs_pimem_cfg, &qhs_pke_wrapper_cfg,
		   &qhs_qdss_cfg, &qhs_qm_cfg,
		   &qhs_qm_mpu_cfg, &qhs_sail_throttle_cfg,
		   &qhs_sdc1, &qhs_security,
		   &qhs_snoc_throttle_cfg, &qhs_tcsr,
		   &qhs_tlmm, &qhs_tsc_cfg,
		   &qhs_ufs_card_cfg, &qhs_ufs_mem_cfg,
		   &qhs_usb2_0, &qhs_usb3_0,
		   &qhs_usb3_1, &qhs_venus_cfg,
		   &qhs_venus_cvp_throttle_cfg, &qhs_venus_v_cpu_throttle_cfg,
		   &qhs_venus_vcodec_throttle_cfg, &qns_ddrss_cfg,
		   &qns_gpdsp_noc_cfg, &qns_mnoc_hf_cfg,
		   &qns_mnoc_sf_cfg, &qns_pcie_anoc_cfg,
		   &qns_snoc_cfg, &qxs_boot_imem,
		   &qxs_imem, &xs_sys_tcu_cfg },
};

static struct qcom_icc_bcm bcm_cn2 = {
	.name = "CN2",
	.num_nodes = 4,
	.nodes = { &qhs_qup0, &qhs_qup1,
		   &qhs_qup2, &qhs_qup3 },
};

static struct qcom_icc_bcm bcm_cn3 = {
	.name = "CN3",
	.num_nodes = 2,
	.nodes = { &xs_pcie_0, &xs_pcie_1 },
};

static struct qcom_icc_bcm bcm_gna0 = {
	.name = "GNA0",
	.num_nodes = 1,
	.nodes = { &qxm_dsp0 },
};

static struct qcom_icc_bcm bcm_gnb0 = {
	.name = "GNB0",
	.num_nodes = 1,
	.nodes = { &qxm_dsp1 },
};

static struct qcom_icc_bcm bcm_mc0 = {
	.name = "MC0",
	.keepalive = true,
	.num_nodes = 1,
	.nodes = { &ebi },
};

static struct qcom_icc_bcm bcm_mm0 = {
	.name = "MM0",
	.keepalive = true,
	.num_nodes = 5,
	.nodes = { &qnm_camnoc_hf, &qnm_mdp0_0,
		   &qnm_mdp0_1, &qnm_mdp1_0,
		   &qns_mem_noc_hf },
};

static struct qcom_icc_bcm bcm_mm1 = {
	.name = "MM1",
	.num_nodes = 7,
	.nodes = { &qnm_camnoc_icp, &qnm_camnoc_sf,
		   &qnm_video0, &qnm_video1,
		   &qnm_video_cvp, &qnm_video_v_cpu,
		   &qns_mem_noc_sf },
};

static struct qcom_icc_bcm bcm_nsa0 = {
	.name = "NSA0",
	.num_nodes = 2,
	.nodes = { &qns_hcp, &qns_nsp_gemnoc },
};

static struct qcom_icc_bcm bcm_nsa1 = {
	.name = "NSA1",
	.num_nodes = 1,
	.nodes = { &qxm_nsp },
};

static struct qcom_icc_bcm bcm_nsb0 = {
	.name = "NSB0",
	.num_nodes = 2,
	.nodes = { &qns_nspb_gemnoc, &qns_nspb_hcp },
};

static struct qcom_icc_bcm bcm_nsb1 = {
	.name = "NSB1",
	.num_nodes = 1,
	.nodes = { &qxm_nspb },
};

static struct qcom_icc_bcm bcm_pci0 = {
	.name = "PCI0",
	.num_nodes = 1,
	.nodes = { &qns_pcie_mem_noc },
};

static struct qcom_icc_bcm bcm_qup0 = {
	.name = "QUP0",
	.vote_scale = 1,
	.num_nodes = 1,
	.nodes = { &qup0_core_slave },
};

static struct qcom_icc_bcm bcm_qup1 = {
	.name = "QUP1",
	.vote_scale = 1,
	.num_nodes = 1,
	.nodes = { &qup1_core_slave },
};

static struct qcom_icc_bcm bcm_qup2 = {
	.name = "QUP2",
	.vote_scale = 1,
	.num_nodes = 2,
	.nodes = { &qup2_core_slave, &qup3_core_slave },
};

static struct qcom_icc_bcm bcm_sh0 = {
	.name = "SH0",
	.keepalive = true,
	.num_nodes = 1,
	.nodes = { &qns_llcc },
};

static struct qcom_icc_bcm bcm_sh2 = {
	.name = "SH2",
	.num_nodes = 1,
	.nodes = { &chm_apps },
};

static struct qcom_icc_bcm bcm_sn0 = {
	.name = "SN0",
	.keepalive = true,
	.num_nodes = 1,
	.nodes = { &qns_gemnoc_sf },
};

static struct qcom_icc_bcm bcm_sn1 = {
	.name = "SN1",
	.num_nodes = 1,
	.nodes = { &qns_gemnoc_gc },
};

static struct qcom_icc_bcm bcm_sn2 = {
	.name = "SN2",
	.num_nodes = 1,
	.nodes = { &qxs_pimem },
};

static struct qcom_icc_bcm bcm_sn3 = {
	.name = "SN3",
	.num_nodes = 2,
	.nodes = { &qns_a1noc_snoc, &qnm_aggre1_noc },
};

static struct qcom_icc_bcm bcm_sn4 = {
	.name = "SN4",
	.num_nodes = 2,
	.nodes = { &qns_a2noc_snoc, &qnm_aggre2_noc },
};

static struct qcom_icc_bcm bcm_sn9 = {
	.name = "SN9",
	.num_nodes = 2,
	.nodes = { &qns_sysnoc, &qnm_lpass_noc },
};

static struct qcom_icc_bcm bcm_sn10 = {
	.name = "SN10",
	.num_nodes = 1,
	.nodes = { &xs_qdss_stm },
};

static struct qcom_icc_bcm *aggre1_noc_bcms[] = {
	&bcm_sn3,
};

static struct qcom_icc_node *aggre1_noc_nodes[] = {
	[MASTER_QUP_3] = &qxm_qup3,
	[MASTER_EMAC] = &xm_emac_0,
	[MASTER_EMAC_1] = &xm_emac_1,
	[MASTER_SDC] = &xm_sdc1,
	[MASTER_UFS_MEM] = &xm_ufs_mem,
	[MASTER_USB2] = &xm_usb2_2,
	[MASTER_USB3_0] = &xm_usb3_0,
	[MASTER_USB3_1] = &xm_usb3_1,
	[SLAVE_A1NOC_SNOC] = &qns_a1noc_snoc,
};

static const struct qcom_icc_desc sa8775p_aggre1_noc = {
	.nodes = aggre1_noc_nodes,
	.num_nodes = ARRAY_SIZE(aggre1_noc_nodes),
	.bcms = aggre1_noc_bcms,
	.num_bcms = ARRAY_SIZE(aggre1_noc_bcms),
};

static struct qcom_icc_bcm *aggre2_noc_bcms[] = {
	&bcm_ce0,
	&bcm_sn4,
};

static struct qcom_icc_node *aggre2_noc_nodes[] = {
	[MASTER_QDSS_BAM] = &qhm_qdss_bam,
	[MASTER_QUP_0] = &qhm_qup0,
	[MASTER_QUP_1] = &qhm_qup1,
	[MASTER_QUP_2] = &qhm_qup2,
	[MASTER_CNOC_A2NOC] = &qnm_cnoc_datapath,
	[MASTER_CRYPTO_CORE0] = &qxm_crypto_0,
	[MASTER_CRYPTO_CORE1] = &qxm_crypto_1,
	[MASTER_IPA] = &qxm_ipa,
	[MASTER_QDSS_ETR_0] = &xm_qdss_etr_0,
	[MASTER_QDSS_ETR_1] = &xm_qdss_etr_1,
	[MASTER_UFS_CARD] = &xm_ufs_card,
	[SLAVE_A2NOC_SNOC] = &qns_a2noc_snoc,
};

static const struct qcom_icc_desc sa8775p_aggre2_noc = {
	.nodes = aggre2_noc_nodes,
	.num_nodes = ARRAY_SIZE(aggre2_noc_nodes),
	.bcms = aggre2_noc_bcms,
	.num_bcms = ARRAY_SIZE(aggre2_noc_bcms),
};

static struct qcom_icc_bcm *clk_virt_bcms[] = {
	&bcm_qup0,
	&bcm_qup1,
	&bcm_qup2,
};

static struct qcom_icc_node *clk_virt_nodes[] = {
	[MASTER_QUP_CORE_0] = &qup0_core_master,
	[MASTER_QUP_CORE_1] = &qup1_core_master,
	[MASTER_QUP_CORE_2] = &qup2_core_master,
	[MASTER_QUP_CORE_3] = &qup3_core_master,
	[SLAVE_QUP_CORE_0] = &qup0_core_slave,
	[SLAVE_QUP_CORE_1] = &qup1_core_slave,
	[SLAVE_QUP_CORE_2] = &qup2_core_slave,
	[SLAVE_QUP_CORE_3] = &qup3_core_slave,
};

static const struct qcom_icc_desc sa8775p_clk_virt = {
	.nodes = clk_virt_nodes,
	.num_nodes = ARRAY_SIZE(clk_virt_nodes),
	.bcms = clk_virt_bcms,
	.num_bcms = ARRAY_SIZE(clk_virt_bcms),
};

static struct qcom_icc_bcm *config_noc_bcms[] = {
	&bcm_cn0,
	&bcm_cn1,
	&bcm_cn2,
	&bcm_cn3,
	&bcm_sn2,
	&bcm_sn10,
};

static struct qcom_icc_node *config_noc_nodes[] = {
	[MASTER_GEM_NOC_CNOC] = &qnm_gemnoc_cnoc,
	[MASTER_GEM_NOC_PCIE_SNOC] = &qnm_gemnoc_pcie,
	[SLAVE_AHB2PHY_0] = &qhs_ahb2phy0,
	[SLAVE_AHB2PHY_1] = &qhs_ahb2phy1,
	[SLAVE_AHB2PHY_2] = &qhs_ahb2phy2,
	[SLAVE_AHB2PHY_3] = &qhs_ahb2phy3,
	[SLAVE_ANOC_THROTTLE_CFG] = &qhs_anoc_throttle_cfg,
	[SLAVE_AOSS] = &qhs_aoss,
	[SLAVE_APPSS] = &qhs_apss,
	[SLAVE_BOOT_ROM] = &qhs_boot_rom,
	[SLAVE_CAMERA_CFG] = &qhs_camera_cfg,
	[SLAVE_CAMERA_NRT_THROTTLE_CFG] = &qhs_camera_nrt_throttle_cfg,
	[SLAVE_CAMERA_RT_THROTTLE_CFG] = &qhs_camera_rt_throttle_cfg,
	[SLAVE_CLK_CTL] = &qhs_clk_ctl,
	[SLAVE_CDSP_CFG] = &qhs_compute0_cfg,
	[SLAVE_CDSP1_CFG] = &qhs_compute1_cfg,
	[SLAVE_RBCPR_CX_CFG] = &qhs_cpr_cx,
	[SLAVE_RBCPR_MMCX_CFG] = &qhs_cpr_mmcx,
	[SLAVE_RBCPR_MX_CFG] = &qhs_cpr_mx,
	[SLAVE_CPR_NSPCX] = &qhs_cpr_nspcx,
	[SLAVE_CRYPTO_0_CFG] = &qhs_crypto0_cfg,
	[SLAVE_CX_RDPM] = &qhs_cx_rdpm,
	[SLAVE_DISPLAY_CFG] = &qhs_display0_cfg,
	[SLAVE_DISPLAY_RT_THROTTLE_CFG] = &qhs_display0_rt_throttle_cfg,
	[SLAVE_DISPLAY1_CFG] = &qhs_display1_cfg,
	[SLAVE_DISPLAY1_RT_THROTTLE_CFG] = &qhs_display1_rt_throttle_cfg,
	[SLAVE_EMAC_CFG] = &qhs_emac0_cfg,
	[SLAVE_EMAC1_CFG] = &qhs_emac1_cfg,
	[SLAVE_GP_DSP0_CFG] = &qhs_gp_dsp0_cfg,
	[SLAVE_GP_DSP1_CFG] = &qhs_gp_dsp1_cfg,
	[SLAVE_GPDSP0_THROTTLE_CFG] = &qhs_gpdsp0_throttle_cfg,
	[SLAVE_GPDSP1_THROTTLE_CFG] = &qhs_gpdsp1_throttle_cfg,
	[SLAVE_GPU_TCU_THROTTLE_CFG] = &qhs_gpu_tcu_throttle_cfg,
	[SLAVE_GFX3D_CFG] = &qhs_gpuss_cfg,
	[SLAVE_HWKM] = &qhs_hwkm,
	[SLAVE_IMEM_CFG] = &qhs_imem_cfg,
	[SLAVE_IPA_CFG] = &qhs_ipa,
	[SLAVE_IPC_ROUTER_CFG] = &qhs_ipc_router,
	[SLAVE_LPASS] = &qhs_lpass_cfg,
	[SLAVE_LPASS_THROTTLE_CFG] = &qhs_lpass_throttle_cfg,
	[SLAVE_MX_RDPM] = &qhs_mx_rdpm,
	[SLAVE_MXC_RDPM] = &qhs_mxc_rdpm,
	[SLAVE_PCIE_0_CFG] = &qhs_pcie0_cfg,
	[SLAVE_PCIE_1_CFG] = &qhs_pcie1_cfg,
	[SLAVE_PCIE_RSC_CFG] = &qhs_pcie_rsc_cfg,
	[SLAVE_PCIE_TCU_THROTTLE_CFG] = &qhs_pcie_tcu_throttle_cfg,
	[SLAVE_PCIE_THROTTLE_CFG] = &qhs_pcie_throttle_cfg,
	[SLAVE_PDM] = &qhs_pdm,
	[SLAVE_PIMEM_CFG] = &qhs_pimem_cfg,
	[SLAVE_PKA_WRAPPER_CFG] = &qhs_pke_wrapper_cfg,
	[SLAVE_QDSS_CFG] = &qhs_qdss_cfg,
	[SLAVE_QM_CFG] = &qhs_qm_cfg,
	[SLAVE_QM_MPU_CFG] = &qhs_qm_mpu_cfg,
	[SLAVE_QUP_0] = &qhs_qup0,
	[SLAVE_QUP_1] = &qhs_qup1,
	[SLAVE_QUP_2] = &qhs_qup2,
	[SLAVE_QUP_3] = &qhs_qup3,
	[SLAVE_SAIL_THROTTLE_CFG] = &qhs_sail_throttle_cfg,
	[SLAVE_SDC1] = &qhs_sdc1,
	[SLAVE_SECURITY] = &qhs_security,
	[SLAVE_SNOC_THROTTLE_CFG] = &qhs_snoc_throttle_cfg,
	[SLAVE_TCSR] = &qhs_tcsr,
	[SLAVE_TLMM] = &qhs_tlmm,
	[SLAVE_TSC_CFG] = &qhs_tsc_cfg,
	[SLAVE_UFS_CARD_CFG] = &qhs_ufs_card_cfg,
	[SLAVE_UFS_MEM_CFG] = &qhs_ufs_mem_cfg,
	[SLAVE_USB2] = &qhs_usb2_0,
	[SLAVE_USB3_0] = &qhs_usb3_0,
	[SLAVE_USB3_1] = &qhs_usb3_1,
	[SLAVE_VENUS_CFG] = &qhs_venus_cfg,
	[SLAVE_VENUS_CVP_THROTTLE_CFG] = &qhs_venus_cvp_throttle_cfg,
	[SLAVE_VENUS_V_CPU_THROTTLE_CFG] = &qhs_venus_v_cpu_throttle_cfg,
	[SLAVE_VENUS_VCODEC_THROTTLE_CFG] = &qhs_venus_vcodec_throttle_cfg,
	[SLAVE_DDRSS_CFG] = &qns_ddrss_cfg,
	[SLAVE_GPDSP_NOC_CFG] = &qns_gpdsp_noc_cfg,
	[SLAVE_CNOC_MNOC_HF_CFG] = &qns_mnoc_hf_cfg,
	[SLAVE_CNOC_MNOC_SF_CFG] = &qns_mnoc_sf_cfg,
	[SLAVE_PCIE_ANOC_CFG] = &qns_pcie_anoc_cfg,
	[SLAVE_SNOC_CFG] = &qns_snoc_cfg,
	[SLAVE_BOOT_IMEM] = &qxs_boot_imem,
	[SLAVE_IMEM] = &qxs_imem,
	[SLAVE_PIMEM] = &qxs_pimem,
	[SLAVE_PCIE_0] = &xs_pcie_0,
	[SLAVE_PCIE_1] = &xs_pcie_1,
	[SLAVE_QDSS_STM] = &xs_qdss_stm,
	[SLAVE_TCU] = &xs_sys_tcu_cfg,
};

static const struct qcom_icc_desc sa8775p_config_noc = {
	.nodes = config_noc_nodes,
	.num_nodes = ARRAY_SIZE(config_noc_nodes),
	.bcms = config_noc_bcms,
	.num_bcms = ARRAY_SIZE(config_noc_bcms),
};

static struct qcom_icc_bcm *dc_noc_bcms[] = {
};

static struct qcom_icc_node *dc_noc_nodes[] = {
	[MASTER_CNOC_DC_NOC] = &qnm_cnoc_dc_noc,
	[SLAVE_LLCC_CFG] = &qhs_llcc,
	[SLAVE_GEM_NOC_CFG] = &qns_gemnoc,
};

static const struct qcom_icc_desc sa8775p_dc_noc = {
	.nodes = dc_noc_nodes,
	.num_nodes = ARRAY_SIZE(dc_noc_nodes),
	.bcms = dc_noc_bcms,
	.num_bcms = ARRAY_SIZE(dc_noc_bcms),
};

static struct qcom_icc_bcm *gem_noc_bcms[] = {
	&bcm_sh0,
	&bcm_sh2,
};

static struct qcom_icc_node *gem_noc_nodes[] = {
	[MASTER_GPU_TCU] = &alm_gpu_tcu,
	[MASTER_PCIE_TCU] = &alm_pcie_tcu,
	[MASTER_SYS_TCU] = &alm_sys_tcu,
	[MASTER_APPSS_PROC] = &chm_apps,
	[MASTER_COMPUTE_NOC] = &qnm_cmpnoc0,
	[MASTER_COMPUTE_NOC_1] = &qnm_cmpnoc1,
	[MASTER_GEM_NOC_CFG] = &qnm_gemnoc_cfg,
	[MASTER_GPDSP_SAIL] = &qnm_gpdsp_sail,
	[MASTER_GFX3D] = &qnm_gpu,
	[MASTER_MNOC_HF_MEM_NOC] = &qnm_mnoc_hf,
	[MASTER_MNOC_SF_MEM_NOC] = &qnm_mnoc_sf,
	[MASTER_ANOC_PCIE_GEM_NOC] = &qnm_pcie,
	[MASTER_SNOC_GC_MEM_NOC] = &qnm_snoc_gc,
	[MASTER_SNOC_SF_MEM_NOC] = &qnm_snoc_sf,
	[SLAVE_GEM_NOC_CNOC] = &qns_gem_noc_cnoc,
	[SLAVE_LLCC] = &qns_llcc,
	[SLAVE_GEM_NOC_PCIE_CNOC] = &qns_pcie,
	[SLAVE_SERVICE_GEM_NOC_1] = &srvc_even_gemnoc,
	[SLAVE_SERVICE_GEM_NOC_2] = &srvc_odd_gemnoc,
	[SLAVE_SERVICE_GEM_NOC] = &srvc_sys_gemnoc,
	[SLAVE_SERVICE_GEM_NOC2] = &srvc_sys_gemnoc_2,
};

static const struct qcom_icc_desc sa8775p_gem_noc = {
	.nodes = gem_noc_nodes,
	.num_nodes = ARRAY_SIZE(gem_noc_nodes),
	.bcms = gem_noc_bcms,
	.num_bcms = ARRAY_SIZE(gem_noc_bcms),
};

static struct qcom_icc_bcm *gpdsp_anoc_bcms[] = {
	&bcm_gna0,
	&bcm_gnb0,
};

static struct qcom_icc_node *gpdsp_anoc_nodes[] = {
	[MASTER_DSP0] = &qxm_dsp0,
	[MASTER_DSP1] = &qxm_dsp1,
	[SLAVE_GP_DSP_SAIL_NOC] = &qns_gp_dsp_sail_noc,
};

static const struct qcom_icc_desc sa8775p_gpdsp_anoc = {
	.nodes = gpdsp_anoc_nodes,
	.num_nodes = ARRAY_SIZE(gpdsp_anoc_nodes),
	.bcms = gpdsp_anoc_bcms,
	.num_bcms = ARRAY_SIZE(gpdsp_anoc_bcms),
};

static struct qcom_icc_bcm *lpass_ag_noc_bcms[] = {
	&bcm_sn9,
};

static struct qcom_icc_node *lpass_ag_noc_nodes[] = {
	[MASTER_CNOC_LPASS_AG_NOC] = &qhm_config_noc,
	[MASTER_LPASS_PROC] = &qxm_lpass_dsp,
	[SLAVE_LPASS_CORE_CFG] = &qhs_lpass_core,
	[SLAVE_LPASS_LPI_CFG] = &qhs_lpass_lpi,
	[SLAVE_LPASS_MPU_CFG] = &qhs_lpass_mpu,
	[SLAVE_LPASS_TOP_CFG] = &qhs_lpass_top,
	[SLAVE_LPASS_SNOC] = &qns_sysnoc,
	[SLAVE_SERVICES_LPASS_AML_NOC] = &srvc_niu_aml_noc,
	[SLAVE_SERVICE_LPASS_AG_NOC] = &srvc_niu_lpass_agnoc,
};

static const struct qcom_icc_desc sa8775p_lpass_ag_noc = {
	.nodes = lpass_ag_noc_nodes,
	.num_nodes = ARRAY_SIZE(lpass_ag_noc_nodes),
	.bcms = lpass_ag_noc_bcms,
	.num_bcms = ARRAY_SIZE(lpass_ag_noc_bcms),
};

static struct qcom_icc_bcm *mc_virt_bcms[] = {
	&bcm_acv,
	&bcm_mc0,
};

static struct qcom_icc_node *mc_virt_nodes[] = {
	[MASTER_LLCC] = &llcc_mc,
	[SLAVE_EBI1] = &ebi,
};

static const struct qcom_icc_desc sa8775p_mc_virt = {
	.nodes = mc_virt_nodes,
	.num_nodes = ARRAY_SIZE(mc_virt_nodes),
	.bcms = mc_virt_bcms,
	.num_bcms = ARRAY_SIZE(mc_virt_bcms),
};

static struct qcom_icc_bcm *mmss_noc_bcms[] = {
	&bcm_mm0,
	&bcm_mm1,
};

static struct qcom_icc_node *mmss_noc_nodes[] = {
	[MASTER_CAMNOC_HF] = &qnm_camnoc_hf,
	[MASTER_CAMNOC_ICP] = &qnm_camnoc_icp,
	[MASTER_CAMNOC_SF] = &qnm_camnoc_sf,
	[MASTER_MDP0] = &qnm_mdp0_0,
	[MASTER_MDP1] = &qnm_mdp0_1,
	[MASTER_MDP_CORE1_0] = &qnm_mdp1_0,
	[MASTER_MDP_CORE1_1] = &qnm_mdp1_1,
	[MASTER_CNOC_MNOC_HF_CFG] = &qnm_mnoc_hf_cfg,
	[MASTER_CNOC_MNOC_SF_CFG] = &qnm_mnoc_sf_cfg,
	[MASTER_VIDEO_P0] = &qnm_video0,
	[MASTER_VIDEO_P1] = &qnm_video1,
	[MASTER_VIDEO_PROC] = &qnm_video_cvp,
	[MASTER_VIDEO_V_PROC] = &qnm_video_v_cpu,
	[SLAVE_MNOC_HF_MEM_NOC] = &qns_mem_noc_hf,
	[SLAVE_MNOC_SF_MEM_NOC] = &qns_mem_noc_sf,
	[SLAVE_SERVICE_MNOC_HF] = &srvc_mnoc_hf,
	[SLAVE_SERVICE_MNOC_SF] = &srvc_mnoc_sf,
};

static const struct qcom_icc_desc sa8775p_mmss_noc = {
	.nodes = mmss_noc_nodes,
	.num_nodes = ARRAY_SIZE(mmss_noc_nodes),
	.bcms = mmss_noc_bcms,
	.num_bcms = ARRAY_SIZE(mmss_noc_bcms),
};

static struct qcom_icc_bcm *nspa_noc_bcms[] = {
	&bcm_nsa0,
	&bcm_nsa1,
};

static struct qcom_icc_node *nspa_noc_nodes[] = {
	[MASTER_CDSP_NOC_CFG] = &qhm_nsp_noc_config,
	[MASTER_CDSP_PROC] = &qxm_nsp,
	[SLAVE_HCP_A] = &qns_hcp,
	[SLAVE_CDSP_MEM_NOC] = &qns_nsp_gemnoc,
	[SLAVE_SERVICE_NSP_NOC] = &service_nsp_noc,
};

static const struct qcom_icc_desc sa8775p_nspa_noc = {
	.nodes = nspa_noc_nodes,
	.num_nodes = ARRAY_SIZE(nspa_noc_nodes),
	.bcms = nspa_noc_bcms,
	.num_bcms = ARRAY_SIZE(nspa_noc_bcms),
};

static struct qcom_icc_bcm *nspb_noc_bcms[] = {
	&bcm_nsb0,
	&bcm_nsb1,
};

static struct qcom_icc_node *nspb_noc_nodes[] = {
	[MASTER_CDSPB_NOC_CFG] = &qhm_nspb_noc_config,
	[MASTER_CDSP_PROC_B] = &qxm_nspb,
	[SLAVE_CDSPB_MEM_NOC] = &qns_nspb_gemnoc,
	[SLAVE_HCP_B] = &qns_nspb_hcp,
	[SLAVE_SERVICE_NSPB_NOC] = &service_nspb_noc,
};

static const struct qcom_icc_desc sa8775p_nspb_noc = {
	.nodes = nspb_noc_nodes,
	.num_nodes = ARRAY_SIZE(nspb_noc_nodes),
	.bcms = nspb_noc_bcms,
	.num_bcms = ARRAY_SIZE(nspb_noc_bcms),
};

static struct qcom_icc_bcm *pcie_anoc_bcms[] = {
	&bcm_pci0,
};

static struct qcom_icc_node *pcie_anoc_nodes[] = {
	[MASTER_PCIE_0] = &xm_pcie3_0,
	[MASTER_PCIE_1] = &xm_pcie3_1,
	[SLAVE_ANOC_PCIE_GEM_NOC] = &qns_pcie_mem_noc,
};

static const struct qcom_icc_desc sa8775p_pcie_anoc = {
	.nodes = pcie_anoc_nodes,
	.num_nodes = ARRAY_SIZE(pcie_anoc_nodes),
	.bcms = pcie_anoc_bcms,
	.num_bcms = ARRAY_SIZE(pcie_anoc_bcms),
};

static struct qcom_icc_bcm *system_noc_bcms[] = {
	&bcm_sn0,
	&bcm_sn1,
	&bcm_sn3,
	&bcm_sn4,
	&bcm_sn9,
};

static struct qcom_icc_node *system_noc_nodes[] = {
	[MASTER_GIC_AHB] = &qhm_gic,
	[MASTER_A1NOC_SNOC] = &qnm_aggre1_noc,
	[MASTER_A2NOC_SNOC] = &qnm_aggre2_noc,
	[MASTER_LPASS_ANOC] = &qnm_lpass_noc,
	[MASTER_SNOC_CFG] = &qnm_snoc_cfg,
	[MASTER_PIMEM] = &qxm_pimem,
	[MASTER_GIC] = &xm_gic,
	[SLAVE_SNOC_GEM_NOC_GC] = &qns_gemnoc_gc,
	[SLAVE_SNOC_GEM_NOC_SF] = &qns_gemnoc_sf,
	[SLAVE_SERVICE_SNOC] = &srvc_snoc,
};

static const struct qcom_icc_desc sa8775p_system_noc = {
	.nodes = system_noc_nodes,
	.num_nodes = ARRAY_SIZE(system_noc_nodes),
	.bcms = system_noc_bcms,
	.num_bcms = ARRAY_SIZE(system_noc_bcms),
};

static const struct of_device_id qnoc_of_match[] = {
	{ .compatible = "qcom,sa8775p-aggre1-noc", .data = &sa8775p_aggre1_noc, },
	{ .compatible = "qcom,sa8775p-aggre2-noc", .data = &sa8775p_aggre2_noc, },
	{ .compatible = "qcom,sa8775p-clk-virt", .data = &sa8775p_clk_virt, },
	{ .compatible = "qcom,sa8775p-config-noc", .data = &sa8775p_config_noc, },
	{ .compatible = "qcom,sa8775p-dc-noc", .data = &sa8775p_dc_noc, },
	{ .compatible = "qcom,sa8775p-gem-noc", .data = &sa8775p_gem_noc, },
	{ .compatible = "qcom,sa8775p-gpdsp-anoc", .data = &sa8775p_gpdsp_anoc, },
	{ .compatible = "qcom,sa8775p-lpass-ag-noc", .data = &sa8775p_lpass_ag_noc, },
	{ .compatible = "qcom,sa8775p-mc-virt", .data = &sa8775p_mc_virt, },
	{ .compatible = "qcom,sa8775p-mmss-noc", .data = &sa8775p_mmss_noc, },
	{ .compatible = "qcom,sa8775p-nspa-noc", .data = &sa8775p_nspa_noc, },
	{ .compatible = "qcom,sa8775p-nspb-noc", .data = &sa8775p_nspb_noc, },
	{ .compatible = "qcom,sa8775p-pcie-anoc", .data = &sa8775p_pcie_anoc, },
	{ .compatible = "qcom,sa8775p-system-noc", .data = &sa8775p_system_noc, },
	{ }
};
MODULE_DEVICE_TABLE(of, qnoc_of_match);

static struct platform_driver qnoc_driver = {
	.probe = qcom_icc_rpmh_probe,
	.remove_new = qcom_icc_rpmh_remove,
	.driver = {
		.name = "qnoc-sa8775p",
		.of_match_table = qnoc_of_match,
		.sync_state = icc_sync_state,
	},
};

static int __init qnoc_driver_init(void)
{
	return platform_driver_register(&qnoc_driver);
}
core_initcall(qnoc_driver_init);

static void __exit qnoc_driver_exit(void)
{
	platform_driver_unregister(&qnoc_driver);
}
module_exit(qnoc_driver_exit);

MODULE_DESCRIPTION("Qualcomm Technologies, Inc. SA8775P NoC driver");
MODULE_LICENSE("GPL");
