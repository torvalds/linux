// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) 2018-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) 2021-2025 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/types.h>
#include <linux/bitops.h>
#include <linux/bitfield.h>

#include "debug.h"
#include "core.h"
#include "ce.h"
#include "hw.h"
#include "mhi.h"
#include "dp_rx.h"

static const guid_t wcn7850_uuid = GUID_INIT(0xf634f534, 0x6147, 0x11ec,
					     0x90, 0xd6, 0x02, 0x42,
					     0xac, 0x12, 0x00, 0x03);

static u8 ath12k_hw_qcn9274_mac_from_pdev_id(int pdev_idx)
{
	return pdev_idx;
}

static int ath12k_hw_mac_id_to_pdev_id_qcn9274(const struct ath12k_hw_params *hw,
					       int mac_id)
{
	return mac_id;
}

static int ath12k_hw_mac_id_to_srng_id_qcn9274(const struct ath12k_hw_params *hw,
					       int mac_id)
{
	return 0;
}

static u8 ath12k_hw_get_ring_selector_qcn9274(struct sk_buff *skb)
{
	return smp_processor_id();
}

static bool ath12k_dp_srng_is_comp_ring_qcn9274(int ring_num)
{
	if (ring_num < 3 || ring_num == 4)
		return true;

	return false;
}

static int ath12k_hw_mac_id_to_pdev_id_wcn7850(const struct ath12k_hw_params *hw,
					       int mac_id)
{
	return 0;
}

static int ath12k_hw_mac_id_to_srng_id_wcn7850(const struct ath12k_hw_params *hw,
					       int mac_id)
{
	return mac_id;
}

static u8 ath12k_hw_get_ring_selector_wcn7850(struct sk_buff *skb)
{
	return skb_get_queue_mapping(skb);
}

static bool ath12k_dp_srng_is_comp_ring_wcn7850(int ring_num)
{
	if (ring_num == 0 || ring_num == 2 || ring_num == 4)
		return true;

	return false;
}

static const struct ath12k_hw_ops qcn9274_ops = {
	.get_hw_mac_from_pdev_id = ath12k_hw_qcn9274_mac_from_pdev_id,
	.mac_id_to_pdev_id = ath12k_hw_mac_id_to_pdev_id_qcn9274,
	.mac_id_to_srng_id = ath12k_hw_mac_id_to_srng_id_qcn9274,
	.rxdma_ring_sel_config = ath12k_dp_rxdma_ring_sel_config_qcn9274,
	.get_ring_selector = ath12k_hw_get_ring_selector_qcn9274,
	.dp_srng_is_tx_comp_ring = ath12k_dp_srng_is_comp_ring_qcn9274,
};

static const struct ath12k_hw_ops wcn7850_ops = {
	.get_hw_mac_from_pdev_id = ath12k_hw_qcn9274_mac_from_pdev_id,
	.mac_id_to_pdev_id = ath12k_hw_mac_id_to_pdev_id_wcn7850,
	.mac_id_to_srng_id = ath12k_hw_mac_id_to_srng_id_wcn7850,
	.rxdma_ring_sel_config = ath12k_dp_rxdma_ring_sel_config_wcn7850,
	.get_ring_selector = ath12k_hw_get_ring_selector_wcn7850,
	.dp_srng_is_tx_comp_ring = ath12k_dp_srng_is_comp_ring_wcn7850,
};

#define ATH12K_TX_RING_MASK_0 0x1
#define ATH12K_TX_RING_MASK_1 0x2
#define ATH12K_TX_RING_MASK_2 0x4
#define ATH12K_TX_RING_MASK_3 0x8
#define ATH12K_TX_RING_MASK_4 0x10

#define ATH12K_RX_RING_MASK_0 0x1
#define ATH12K_RX_RING_MASK_1 0x2
#define ATH12K_RX_RING_MASK_2 0x4
#define ATH12K_RX_RING_MASK_3 0x8

#define ATH12K_RX_ERR_RING_MASK_0 0x1

#define ATH12K_RX_WBM_REL_RING_MASK_0 0x1

#define ATH12K_REO_STATUS_RING_MASK_0 0x1

#define ATH12K_HOST2RXDMA_RING_MASK_0 0x1

#define ATH12K_RX_MON_RING_MASK_0 0x1
#define ATH12K_RX_MON_RING_MASK_1 0x2
#define ATH12K_RX_MON_RING_MASK_2 0x4

#define ATH12K_TX_MON_RING_MASK_0 0x1
#define ATH12K_TX_MON_RING_MASK_1 0x2

#define ATH12K_RX_MON_STATUS_RING_MASK_0 0x1
#define ATH12K_RX_MON_STATUS_RING_MASK_1 0x2
#define ATH12K_RX_MON_STATUS_RING_MASK_2 0x4

/* Target firmware's Copy Engine configuration. */
static const struct ce_pipe_config ath12k_target_ce_config_wlan_qcn9274[] = {
	/* CE0: host->target HTC control and raw streams */
	{
		.pipenum = __cpu_to_le32(0),
		.pipedir = __cpu_to_le32(PIPEDIR_OUT),
		.nentries = __cpu_to_le32(32),
		.nbytes_max = __cpu_to_le32(2048),
		.flags = __cpu_to_le32(CE_ATTR_FLAGS),
		.reserved = __cpu_to_le32(0),
	},

	/* CE1: target->host HTT + HTC control */
	{
		.pipenum = __cpu_to_le32(1),
		.pipedir = __cpu_to_le32(PIPEDIR_IN),
		.nentries = __cpu_to_le32(32),
		.nbytes_max = __cpu_to_le32(2048),
		.flags = __cpu_to_le32(CE_ATTR_FLAGS),
		.reserved = __cpu_to_le32(0),
	},

	/* CE2: target->host WMI */
	{
		.pipenum = __cpu_to_le32(2),
		.pipedir = __cpu_to_le32(PIPEDIR_IN),
		.nentries = __cpu_to_le32(32),
		.nbytes_max = __cpu_to_le32(2048),
		.flags = __cpu_to_le32(CE_ATTR_FLAGS),
		.reserved = __cpu_to_le32(0),
	},

	/* CE3: host->target WMI (mac0) */
	{
		.pipenum = __cpu_to_le32(3),
		.pipedir = __cpu_to_le32(PIPEDIR_OUT),
		.nentries = __cpu_to_le32(32),
		.nbytes_max = __cpu_to_le32(2048),
		.flags = __cpu_to_le32(CE_ATTR_FLAGS),
		.reserved = __cpu_to_le32(0),
	},

	/* CE4: host->target HTT */
	{
		.pipenum = __cpu_to_le32(4),
		.pipedir = __cpu_to_le32(PIPEDIR_OUT),
		.nentries = __cpu_to_le32(256),
		.nbytes_max = __cpu_to_le32(256),
		.flags = __cpu_to_le32(CE_ATTR_FLAGS | CE_ATTR_DIS_INTR),
		.reserved = __cpu_to_le32(0),
	},

	/* CE5: target->host Pktlog */
	{
		.pipenum = __cpu_to_le32(5),
		.pipedir = __cpu_to_le32(PIPEDIR_IN),
		.nentries = __cpu_to_le32(32),
		.nbytes_max = __cpu_to_le32(2048),
		.flags = __cpu_to_le32(CE_ATTR_FLAGS),
		.reserved = __cpu_to_le32(0),
	},

	/* CE6: Reserved for target autonomous hif_memcpy */
	{
		.pipenum = __cpu_to_le32(6),
		.pipedir = __cpu_to_le32(PIPEDIR_INOUT),
		.nentries = __cpu_to_le32(32),
		.nbytes_max = __cpu_to_le32(16384),
		.flags = __cpu_to_le32(CE_ATTR_FLAGS),
		.reserved = __cpu_to_le32(0),
	},

	/* CE7: host->target WMI (mac1) */
	{
		.pipenum = __cpu_to_le32(7),
		.pipedir = __cpu_to_le32(PIPEDIR_OUT),
		.nentries = __cpu_to_le32(32),
		.nbytes_max = __cpu_to_le32(2048),
		.flags = __cpu_to_le32(CE_ATTR_FLAGS),
		.reserved = __cpu_to_le32(0),
	},

	/* CE8: Reserved for target autonomous hif_memcpy */
	{
		.pipenum = __cpu_to_le32(8),
		.pipedir = __cpu_to_le32(PIPEDIR_INOUT),
		.nentries = __cpu_to_le32(32),
		.nbytes_max = __cpu_to_le32(16384),
		.flags = __cpu_to_le32(CE_ATTR_FLAGS),
		.reserved = __cpu_to_le32(0),
	},

	/* CE9, 10 and 11: Reserved for MHI */

	/* CE12: Target CV prefetch */
	{
		.pipenum = __cpu_to_le32(12),
		.pipedir = __cpu_to_le32(PIPEDIR_OUT),
		.nentries = __cpu_to_le32(32),
		.nbytes_max = __cpu_to_le32(2048),
		.flags = __cpu_to_le32(CE_ATTR_FLAGS),
		.reserved = __cpu_to_le32(0),
	},

	/* CE13: Target CV prefetch */
	{
		.pipenum = __cpu_to_le32(13),
		.pipedir = __cpu_to_le32(PIPEDIR_OUT),
		.nentries = __cpu_to_le32(32),
		.nbytes_max = __cpu_to_le32(2048),
		.flags = __cpu_to_le32(CE_ATTR_FLAGS),
		.reserved = __cpu_to_le32(0),
	},

	/* CE14: WMI logging/CFR/Spectral/Radar */
	{
		.pipenum = __cpu_to_le32(14),
		.pipedir = __cpu_to_le32(PIPEDIR_IN),
		.nentries = __cpu_to_le32(32),
		.nbytes_max = __cpu_to_le32(2048),
		.flags = __cpu_to_le32(CE_ATTR_FLAGS),
		.reserved = __cpu_to_le32(0),
	},

	/* CE15: Reserved */
};

/* Target firmware's Copy Engine configuration. */
static const struct ce_pipe_config ath12k_target_ce_config_wlan_wcn7850[] = {
	/* CE0: host->target HTC control and raw streams */
	{
		.pipenum = __cpu_to_le32(0),
		.pipedir = __cpu_to_le32(PIPEDIR_OUT),
		.nentries = __cpu_to_le32(32),
		.nbytes_max = __cpu_to_le32(2048),
		.flags = __cpu_to_le32(CE_ATTR_FLAGS),
		.reserved = __cpu_to_le32(0),
	},

	/* CE1: target->host HTT + HTC control */
	{
		.pipenum = __cpu_to_le32(1),
		.pipedir = __cpu_to_le32(PIPEDIR_IN),
		.nentries = __cpu_to_le32(32),
		.nbytes_max = __cpu_to_le32(2048),
		.flags = __cpu_to_le32(CE_ATTR_FLAGS),
		.reserved = __cpu_to_le32(0),
	},

	/* CE2: target->host WMI */
	{
		.pipenum = __cpu_to_le32(2),
		.pipedir = __cpu_to_le32(PIPEDIR_IN),
		.nentries = __cpu_to_le32(32),
		.nbytes_max = __cpu_to_le32(2048),
		.flags = __cpu_to_le32(CE_ATTR_FLAGS),
		.reserved = __cpu_to_le32(0),
	},

	/* CE3: host->target WMI */
	{
		.pipenum = __cpu_to_le32(3),
		.pipedir = __cpu_to_le32(PIPEDIR_OUT),
		.nentries = __cpu_to_le32(32),
		.nbytes_max = __cpu_to_le32(2048),
		.flags = __cpu_to_le32(CE_ATTR_FLAGS),
		.reserved = __cpu_to_le32(0),
	},

	/* CE4: host->target HTT */
	{
		.pipenum = __cpu_to_le32(4),
		.pipedir = __cpu_to_le32(PIPEDIR_OUT),
		.nentries = __cpu_to_le32(256),
		.nbytes_max = __cpu_to_le32(256),
		.flags = __cpu_to_le32(CE_ATTR_FLAGS | CE_ATTR_DIS_INTR),
		.reserved = __cpu_to_le32(0),
	},

	/* CE5: target->host Pktlog */
	{
		.pipenum = __cpu_to_le32(5),
		.pipedir = __cpu_to_le32(PIPEDIR_IN),
		.nentries = __cpu_to_le32(32),
		.nbytes_max = __cpu_to_le32(2048),
		.flags = __cpu_to_le32(CE_ATTR_FLAGS),
		.reserved = __cpu_to_le32(0),
	},

	/* CE6: Reserved for target autonomous hif_memcpy */
	{
		.pipenum = __cpu_to_le32(6),
		.pipedir = __cpu_to_le32(PIPEDIR_INOUT),
		.nentries = __cpu_to_le32(32),
		.nbytes_max = __cpu_to_le32(16384),
		.flags = __cpu_to_le32(CE_ATTR_FLAGS),
		.reserved = __cpu_to_le32(0),
	},

	/* CE7 used only by Host */
	{
		.pipenum = __cpu_to_le32(7),
		.pipedir = __cpu_to_le32(PIPEDIR_INOUT_H2H),
		.nentries = __cpu_to_le32(0),
		.nbytes_max = __cpu_to_le32(0),
		.flags = __cpu_to_le32(CE_ATTR_FLAGS | CE_ATTR_DIS_INTR),
		.reserved = __cpu_to_le32(0),
	},

	/* CE8 target->host used only by IPA */
	{
		.pipenum = __cpu_to_le32(8),
		.pipedir = __cpu_to_le32(PIPEDIR_INOUT),
		.nentries = __cpu_to_le32(32),
		.nbytes_max = __cpu_to_le32(16384),
		.flags = __cpu_to_le32(CE_ATTR_FLAGS),
		.reserved = __cpu_to_le32(0),
	},
	/* CE 9, 10, 11 are used by MHI driver */
};

/* Map from service/endpoint to Copy Engine.
 * This table is derived from the CE_PCI TABLE, above.
 * It is passed to the Target at startup for use by firmware.
 */
static const struct service_to_pipe ath12k_target_service_to_ce_map_wlan_qcn9274[] = {
	{
		__cpu_to_le32(ATH12K_HTC_SVC_ID_WMI_DATA_VO),
		__cpu_to_le32(PIPEDIR_OUT),	/* out = UL = host -> target */
		__cpu_to_le32(3),
	},
	{
		__cpu_to_le32(ATH12K_HTC_SVC_ID_WMI_DATA_VO),
		__cpu_to_le32(PIPEDIR_IN),	/* in = DL = target -> host */
		__cpu_to_le32(2),
	},
	{
		__cpu_to_le32(ATH12K_HTC_SVC_ID_WMI_DATA_BK),
		__cpu_to_le32(PIPEDIR_OUT),	/* out = UL = host -> target */
		__cpu_to_le32(3),
	},
	{
		__cpu_to_le32(ATH12K_HTC_SVC_ID_WMI_DATA_BK),
		__cpu_to_le32(PIPEDIR_IN),	/* in = DL = target -> host */
		__cpu_to_le32(2),
	},
	{
		__cpu_to_le32(ATH12K_HTC_SVC_ID_WMI_DATA_BE),
		__cpu_to_le32(PIPEDIR_OUT),	/* out = UL = host -> target */
		__cpu_to_le32(3),
	},
	{
		__cpu_to_le32(ATH12K_HTC_SVC_ID_WMI_DATA_BE),
		__cpu_to_le32(PIPEDIR_IN),	/* in = DL = target -> host */
		__cpu_to_le32(2),
	},
	{
		__cpu_to_le32(ATH12K_HTC_SVC_ID_WMI_DATA_VI),
		__cpu_to_le32(PIPEDIR_OUT),	/* out = UL = host -> target */
		__cpu_to_le32(3),
	},
	{
		__cpu_to_le32(ATH12K_HTC_SVC_ID_WMI_DATA_VI),
		__cpu_to_le32(PIPEDIR_IN),	/* in = DL = target -> host */
		__cpu_to_le32(2),
	},
	{
		__cpu_to_le32(ATH12K_HTC_SVC_ID_WMI_CONTROL),
		__cpu_to_le32(PIPEDIR_OUT),	/* out = UL = host -> target */
		__cpu_to_le32(3),
	},
	{
		__cpu_to_le32(ATH12K_HTC_SVC_ID_WMI_CONTROL),
		__cpu_to_le32(PIPEDIR_IN),	/* in = DL = target -> host */
		__cpu_to_le32(2),
	},
	{
		__cpu_to_le32(ATH12K_HTC_SVC_ID_RSVD_CTRL),
		__cpu_to_le32(PIPEDIR_OUT),	/* out = UL = host -> target */
		__cpu_to_le32(0),
	},
	{
		__cpu_to_le32(ATH12K_HTC_SVC_ID_RSVD_CTRL),
		__cpu_to_le32(PIPEDIR_IN),	/* in = DL = target -> host */
		__cpu_to_le32(1),
	},
	{
		__cpu_to_le32(ATH12K_HTC_SVC_ID_TEST_RAW_STREAMS),
		__cpu_to_le32(PIPEDIR_OUT),	/* out = UL = host -> target */
		__cpu_to_le32(0),
	},
	{
		__cpu_to_le32(ATH12K_HTC_SVC_ID_TEST_RAW_STREAMS),
		__cpu_to_le32(PIPEDIR_IN),	/* in = DL = target -> host */
		__cpu_to_le32(1),
	},
	{
		__cpu_to_le32(ATH12K_HTC_SVC_ID_HTT_DATA_MSG),
		__cpu_to_le32(PIPEDIR_OUT),	/* out = UL = host -> target */
		__cpu_to_le32(4),
	},
	{
		__cpu_to_le32(ATH12K_HTC_SVC_ID_HTT_DATA_MSG),
		__cpu_to_le32(PIPEDIR_IN),	/* in = DL = target -> host */
		__cpu_to_le32(1),
	},
	{
		__cpu_to_le32(ATH12K_HTC_SVC_ID_WMI_CONTROL_MAC1),
		__cpu_to_le32(PIPEDIR_OUT),	/* out = UL = host -> target */
		__cpu_to_le32(7),
	},
	{
		__cpu_to_le32(ATH12K_HTC_SVC_ID_WMI_CONTROL_MAC1),
		__cpu_to_le32(PIPEDIR_IN),	/* in = DL = target -> host */
		__cpu_to_le32(2),
	},
	{
		__cpu_to_le32(ATH12K_HTC_SVC_ID_PKT_LOG),
		__cpu_to_le32(PIPEDIR_IN),	/* in = DL = target -> host */
		__cpu_to_le32(5),
	},
	{
		__cpu_to_le32(ATH12K_HTC_SVC_ID_WMI_CONTROL_DIAG),
		__cpu_to_le32(PIPEDIR_IN),	/* in = DL = target -> host */
		__cpu_to_le32(14),
	},

	/* (Additions here) */

	{ /* must be last */
		__cpu_to_le32(0),
		__cpu_to_le32(0),
		__cpu_to_le32(0),
	},
};

static const struct service_to_pipe ath12k_target_service_to_ce_map_wlan_wcn7850[] = {
	{
		__cpu_to_le32(ATH12K_HTC_SVC_ID_WMI_DATA_VO),
		__cpu_to_le32(PIPEDIR_OUT),	/* out = UL = host -> target */
		__cpu_to_le32(3),
	},
	{
		__cpu_to_le32(ATH12K_HTC_SVC_ID_WMI_DATA_VO),
		__cpu_to_le32(PIPEDIR_IN),	/* in = DL = target -> host */
		__cpu_to_le32(2),
	},
	{
		__cpu_to_le32(ATH12K_HTC_SVC_ID_WMI_DATA_BK),
		__cpu_to_le32(PIPEDIR_OUT),	/* out = UL = host -> target */
		__cpu_to_le32(3),
	},
	{
		__cpu_to_le32(ATH12K_HTC_SVC_ID_WMI_DATA_BK),
		__cpu_to_le32(PIPEDIR_IN),	/* in = DL = target -> host */
		__cpu_to_le32(2),
	},
	{
		__cpu_to_le32(ATH12K_HTC_SVC_ID_WMI_DATA_BE),
		__cpu_to_le32(PIPEDIR_OUT),	/* out = UL = host -> target */
		__cpu_to_le32(3),
	},
	{
		__cpu_to_le32(ATH12K_HTC_SVC_ID_WMI_DATA_BE),
		__cpu_to_le32(PIPEDIR_IN),	/* in = DL = target -> host */
		__cpu_to_le32(2),
	},
	{
		__cpu_to_le32(ATH12K_HTC_SVC_ID_WMI_DATA_VI),
		__cpu_to_le32(PIPEDIR_OUT),	/* out = UL = host -> target */
		__cpu_to_le32(3),
	},
	{
		__cpu_to_le32(ATH12K_HTC_SVC_ID_WMI_DATA_VI),
		__cpu_to_le32(PIPEDIR_IN),	/* in = DL = target -> host */
		__cpu_to_le32(2),
	},
	{
		__cpu_to_le32(ATH12K_HTC_SVC_ID_WMI_CONTROL),
		__cpu_to_le32(PIPEDIR_OUT),	/* out = UL = host -> target */
		__cpu_to_le32(3),
	},
	{
		__cpu_to_le32(ATH12K_HTC_SVC_ID_WMI_CONTROL),
		__cpu_to_le32(PIPEDIR_IN),	/* in = DL = target -> host */
		__cpu_to_le32(2),
	},
	{
		__cpu_to_le32(ATH12K_HTC_SVC_ID_RSVD_CTRL),
		__cpu_to_le32(PIPEDIR_OUT),	/* out = UL = host -> target */
		__cpu_to_le32(0),
	},
	{
		__cpu_to_le32(ATH12K_HTC_SVC_ID_RSVD_CTRL),
		__cpu_to_le32(PIPEDIR_IN),	/* in = DL = target -> host */
		__cpu_to_le32(2),
	},
	{
		__cpu_to_le32(ATH12K_HTC_SVC_ID_HTT_DATA_MSG),
		__cpu_to_le32(PIPEDIR_OUT),	/* out = UL = host -> target */
		__cpu_to_le32(4),
	},
	{
		__cpu_to_le32(ATH12K_HTC_SVC_ID_HTT_DATA_MSG),
		__cpu_to_le32(PIPEDIR_IN),	/* in = DL = target -> host */
		__cpu_to_le32(1),
	},

	/* (Additions here) */

	{ /* must be last */
		__cpu_to_le32(0),
		__cpu_to_le32(0),
		__cpu_to_le32(0),
	},
};

static const struct ce_pipe_config ath12k_target_ce_config_wlan_ipq5332[] = {
	/* host->target HTC control and raw streams */
	{
		.pipenum = __cpu_to_le32(0),
		.pipedir = __cpu_to_le32(PIPEDIR_OUT),
		.nentries = __cpu_to_le32(32),
		.nbytes_max = __cpu_to_le32(2048),
		.flags = __cpu_to_le32(CE_ATTR_FLAGS),
		.reserved = __cpu_to_le32(0),
	},
	/* target->host HTT */
	{
		.pipenum = __cpu_to_le32(1),
		.pipedir = __cpu_to_le32(PIPEDIR_IN),
		.nentries = __cpu_to_le32(32),
		.nbytes_max = __cpu_to_le32(2048),
		.flags = __cpu_to_le32(CE_ATTR_FLAGS),
		.reserved = __cpu_to_le32(0),
	},
	/* target->host WMI  + HTC control */
	{
		.pipenum = __cpu_to_le32(2),
		.pipedir = __cpu_to_le32(PIPEDIR_IN),
		.nentries = __cpu_to_le32(32),
		.nbytes_max = __cpu_to_le32(2048),
		.flags = __cpu_to_le32(CE_ATTR_FLAGS),
		.reserved = __cpu_to_le32(0),
	},
	/* host->target WMI */
	{
		.pipenum = __cpu_to_le32(3),
		.pipedir = __cpu_to_le32(PIPEDIR_OUT),
		.nentries = __cpu_to_le32(32),
		.nbytes_max = __cpu_to_le32(2048),
		.flags = __cpu_to_le32(CE_ATTR_FLAGS),
		.reserved = __cpu_to_le32(0),
	},
	/* host->target HTT */
	{
		.pipenum = __cpu_to_le32(4),
		.pipedir = __cpu_to_le32(PIPEDIR_OUT),
		.nentries = __cpu_to_le32(256),
		.nbytes_max = __cpu_to_le32(256),
		.flags = __cpu_to_le32(CE_ATTR_FLAGS | CE_ATTR_DIS_INTR),
		.reserved = __cpu_to_le32(0),
	},
	/* Target -> host PKTLOG */
	{
		.pipenum = __cpu_to_le32(5),
		.pipedir = __cpu_to_le32(PIPEDIR_IN),
		.nentries = __cpu_to_le32(32),
		.nbytes_max = __cpu_to_le32(2048),
		.flags = __cpu_to_le32(CE_ATTR_FLAGS),
		.reserved = __cpu_to_le32(0),
	},
	/* Reserved for target autonomous HIF_memcpy */
	{
		.pipenum = __cpu_to_le32(6),
		.pipedir = __cpu_to_le32(PIPEDIR_INOUT),
		.nentries = __cpu_to_le32(32),
		.nbytes_max = __cpu_to_le32(16384),
		.flags = __cpu_to_le32(CE_ATTR_FLAGS),
		.reserved = __cpu_to_le32(0),
	},
	/* CE7 Reserved for CV Prefetch */
	{
		.pipenum = __cpu_to_le32(7),
		.pipedir = __cpu_to_le32(PIPEDIR_OUT),
		.nentries = __cpu_to_le32(32),
		.nbytes_max = __cpu_to_le32(2048),
		.flags = __cpu_to_le32(CE_ATTR_FLAGS),
		.reserved = __cpu_to_le32(0),
	},
	/* CE8 Reserved for target generic HIF memcpy */
	{
		.pipenum = __cpu_to_le32(8),
		.pipedir = __cpu_to_le32(PIPEDIR_INOUT),
		.nentries = __cpu_to_le32(32),
		.nbytes_max = __cpu_to_le32(16384),
		.flags = __cpu_to_le32(CE_ATTR_FLAGS),
		.reserved = __cpu_to_le32(0),
	},
	/* CE9 WMI logging/CFR/Spectral/Radar/ */
	{
		.pipenum = __cpu_to_le32(9),
		.pipedir = __cpu_to_le32(PIPEDIR_IN),
		.nentries = __cpu_to_le32(32),
		.nbytes_max = __cpu_to_le32(2048),
		.flags = __cpu_to_le32(CE_ATTR_FLAGS),
		.reserved = __cpu_to_le32(0),
	},
	/* Unused TBD */
	{
		.pipenum = __cpu_to_le32(10),
		.pipedir = __cpu_to_le32(PIPEDIR_NONE),
		.nentries = __cpu_to_le32(0),
		.nbytes_max = __cpu_to_le32(0),
		.flags = __cpu_to_le32(0),
		.reserved = __cpu_to_le32(0),
	},
	/* Unused TBD */
	{
		.pipenum = __cpu_to_le32(11),
		.pipedir = __cpu_to_le32(PIPEDIR_NONE),
		.nentries = __cpu_to_le32(0),
		.nbytes_max = __cpu_to_le32(0),
		.flags = __cpu_to_le32(0),
		.reserved = __cpu_to_le32(0),
	},
};

static const struct service_to_pipe ath12k_target_service_to_ce_map_wlan_ipq5332[] = {
	{
		__cpu_to_le32(ATH12K_HTC_SVC_ID_WMI_DATA_VO),
		__cpu_to_le32(PIPEDIR_OUT),
		__cpu_to_le32(3),
	},
	{
		__cpu_to_le32(ATH12K_HTC_SVC_ID_WMI_DATA_VO),
		__cpu_to_le32(PIPEDIR_IN),
		__cpu_to_le32(2),
	},
	{
		__cpu_to_le32(ATH12K_HTC_SVC_ID_WMI_DATA_BK),
		__cpu_to_le32(PIPEDIR_OUT),
		__cpu_to_le32(3),
	},
	{
		__cpu_to_le32(ATH12K_HTC_SVC_ID_WMI_DATA_BK),
		__cpu_to_le32(PIPEDIR_IN),
		__cpu_to_le32(2),
	},
	{
		__cpu_to_le32(ATH12K_HTC_SVC_ID_WMI_DATA_BE),
		__cpu_to_le32(PIPEDIR_OUT),
		__cpu_to_le32(3),
	},
	{
		__cpu_to_le32(ATH12K_HTC_SVC_ID_WMI_DATA_BE),
		__cpu_to_le32(PIPEDIR_IN),
		__cpu_to_le32(2),
	},
	{
		__cpu_to_le32(ATH12K_HTC_SVC_ID_WMI_DATA_VI),
		__cpu_to_le32(PIPEDIR_OUT),
		__cpu_to_le32(3),
	},
	{
		__cpu_to_le32(ATH12K_HTC_SVC_ID_WMI_DATA_VI),
		__cpu_to_le32(PIPEDIR_IN),
		__cpu_to_le32(2),
	},
	{
		__cpu_to_le32(ATH12K_HTC_SVC_ID_WMI_CONTROL),
		__cpu_to_le32(PIPEDIR_OUT),
		__cpu_to_le32(3),
	},
	{
		__cpu_to_le32(ATH12K_HTC_SVC_ID_WMI_CONTROL),
		__cpu_to_le32(PIPEDIR_IN),
		__cpu_to_le32(2),
	},
	{
		__cpu_to_le32(ATH12K_HTC_SVC_ID_RSVD_CTRL),
		__cpu_to_le32(PIPEDIR_OUT),
		__cpu_to_le32(0),
	},
	{
		__cpu_to_le32(ATH12K_HTC_SVC_ID_RSVD_CTRL),
		__cpu_to_le32(PIPEDIR_IN),
		__cpu_to_le32(1),
	},
	{
		__cpu_to_le32(ATH12K_HTC_SVC_ID_TEST_RAW_STREAMS),
		__cpu_to_le32(PIPEDIR_OUT),
		__cpu_to_le32(0),
	},
	{
		__cpu_to_le32(ATH12K_HTC_SVC_ID_TEST_RAW_STREAMS),
		__cpu_to_le32(PIPEDIR_IN),
		__cpu_to_le32(1),
	},
	{
		__cpu_to_le32(ATH12K_HTC_SVC_ID_HTT_DATA_MSG),
		__cpu_to_le32(PIPEDIR_OUT),
		__cpu_to_le32(4),
	},
	{
		__cpu_to_le32(ATH12K_HTC_SVC_ID_HTT_DATA_MSG),
		__cpu_to_le32(PIPEDIR_IN),
		__cpu_to_le32(1),
	},
	{
		__cpu_to_le32(ATH12K_HTC_SVC_ID_PKT_LOG),
		__cpu_to_le32(PIPEDIR_IN),
		__cpu_to_le32(5),
	},
	{
		__cpu_to_le32(ATH12K_HTC_SVC_ID_WMI_CONTROL_DIAG),
		__cpu_to_le32(PIPEDIR_IN),
		__cpu_to_le32(9),
	},
	/* (Additions here) */

	{ /* must be last */
		__cpu_to_le32(0),
		__cpu_to_le32(0),
		__cpu_to_le32(0),
	},
};

static const struct ath12k_hw_ring_mask ath12k_hw_ring_mask_qcn9274 = {
	.tx  = {
		ATH12K_TX_RING_MASK_0,
		ATH12K_TX_RING_MASK_1,
		ATH12K_TX_RING_MASK_2,
		ATH12K_TX_RING_MASK_3,
	},
	.rx_mon_dest = {
		0, 0, 0, 0,
		0, 0, 0, 0,
		ATH12K_RX_MON_RING_MASK_0,
		ATH12K_RX_MON_RING_MASK_1,
		ATH12K_RX_MON_RING_MASK_2,
	},
	.rx = {
		0, 0, 0, 0,
		ATH12K_RX_RING_MASK_0,
		ATH12K_RX_RING_MASK_1,
		ATH12K_RX_RING_MASK_2,
		ATH12K_RX_RING_MASK_3,
	},
	.rx_err = {
		0, 0, 0,
		ATH12K_RX_ERR_RING_MASK_0,
	},
	.rx_wbm_rel = {
		0, 0, 0,
		ATH12K_RX_WBM_REL_RING_MASK_0,
	},
	.reo_status = {
		0, 0, 0,
		ATH12K_REO_STATUS_RING_MASK_0,
	},
	.host2rxdma = {
		0, 0, 0,
		ATH12K_HOST2RXDMA_RING_MASK_0,
	},
	.tx_mon_dest = {
		0, 0, 0,
	},
};

static const struct ath12k_hw_ring_mask ath12k_hw_ring_mask_ipq5332 = {
	.tx  = {
		ATH12K_TX_RING_MASK_0,
		ATH12K_TX_RING_MASK_1,
		ATH12K_TX_RING_MASK_2,
		ATH12K_TX_RING_MASK_3,
	},
	.rx_mon_dest = {
		0, 0, 0, 0, 0, 0, 0, 0,
		ATH12K_RX_MON_RING_MASK_0,
	},
	.rx = {
		0, 0, 0, 0,
		ATH12K_RX_RING_MASK_0,
		ATH12K_RX_RING_MASK_1,
		ATH12K_RX_RING_MASK_2,
		ATH12K_RX_RING_MASK_3,
	},
	.rx_err = {
		0, 0, 0,
		ATH12K_RX_ERR_RING_MASK_0,
	},
	.rx_wbm_rel = {
		0, 0, 0,
		ATH12K_RX_WBM_REL_RING_MASK_0,
	},
	.reo_status = {
		0, 0, 0,
		ATH12K_REO_STATUS_RING_MASK_0,
	},
	.host2rxdma = {
		0, 0, 0,
		ATH12K_HOST2RXDMA_RING_MASK_0,
	},
	.tx_mon_dest = {
		ATH12K_TX_MON_RING_MASK_0,
		ATH12K_TX_MON_RING_MASK_1,
	},
};

static const struct ath12k_hw_ring_mask ath12k_hw_ring_mask_wcn7850 = {
	.tx  = {
		ATH12K_TX_RING_MASK_0,
		ATH12K_TX_RING_MASK_1,
		ATH12K_TX_RING_MASK_2,
	},
	.rx_mon_dest = {
	},
	.rx_mon_status = {
		0, 0, 0, 0,
		ATH12K_RX_MON_STATUS_RING_MASK_0,
		ATH12K_RX_MON_STATUS_RING_MASK_1,
		ATH12K_RX_MON_STATUS_RING_MASK_2,
	},
	.rx = {
		0, 0, 0,
		ATH12K_RX_RING_MASK_0,
		ATH12K_RX_RING_MASK_1,
		ATH12K_RX_RING_MASK_2,
		ATH12K_RX_RING_MASK_3,
	},
	.rx_err = {
		ATH12K_RX_ERR_RING_MASK_0,
	},
	.rx_wbm_rel = {
		ATH12K_RX_WBM_REL_RING_MASK_0,
	},
	.reo_status = {
		ATH12K_REO_STATUS_RING_MASK_0,
	},
	.host2rxdma = {
	},
	.tx_mon_dest = {
	},
};

static const struct ath12k_hw_regs qcn9274_v1_regs = {
	/* SW2TCL(x) R0 ring configuration address */
	.hal_tcl1_ring_id = 0x00000908,
	.hal_tcl1_ring_misc = 0x00000910,
	.hal_tcl1_ring_tp_addr_lsb = 0x0000091c,
	.hal_tcl1_ring_tp_addr_msb = 0x00000920,
	.hal_tcl1_ring_consumer_int_setup_ix0 = 0x00000930,
	.hal_tcl1_ring_consumer_int_setup_ix1 = 0x00000934,
	.hal_tcl1_ring_msi1_base_lsb = 0x00000948,
	.hal_tcl1_ring_msi1_base_msb = 0x0000094c,
	.hal_tcl1_ring_msi1_data = 0x00000950,
	.hal_tcl_ring_base_lsb = 0x00000b58,
	.hal_tcl1_ring_base_lsb = 0x00000900,
	.hal_tcl1_ring_base_msb = 0x00000904,
	.hal_tcl2_ring_base_lsb = 0x00000978,

	/* TCL STATUS ring address */
	.hal_tcl_status_ring_base_lsb = 0x00000d38,

	.hal_wbm_idle_ring_base_lsb = 0x00000d0c,
	.hal_wbm_idle_ring_misc_addr = 0x00000d1c,
	.hal_wbm_r0_idle_list_cntl_addr = 0x00000210,
	.hal_wbm_r0_idle_list_size_addr = 0x00000214,
	.hal_wbm_scattered_ring_base_lsb = 0x00000220,
	.hal_wbm_scattered_ring_base_msb = 0x00000224,
	.hal_wbm_scattered_desc_head_info_ix0 = 0x00000230,
	.hal_wbm_scattered_desc_head_info_ix1 = 0x00000234,
	.hal_wbm_scattered_desc_tail_info_ix0 = 0x00000240,
	.hal_wbm_scattered_desc_tail_info_ix1 = 0x00000244,
	.hal_wbm_scattered_desc_ptr_hp_addr = 0x0000024c,

	.hal_wbm_sw_release_ring_base_lsb = 0x0000034c,
	.hal_wbm_sw1_release_ring_base_lsb = 0x000003c4,
	.hal_wbm0_release_ring_base_lsb = 0x00000dd8,
	.hal_wbm1_release_ring_base_lsb = 0x00000e50,

	/* PCIe base address */
	.pcie_qserdes_sysclk_en_sel = 0x01e0c0a8,
	.pcie_pcs_osc_dtct_config_base = 0x01e0d45c,

	/* PPE release ring address */
	.hal_ppe_rel_ring_base = 0x0000043c,

	/* REO DEST ring address */
	.hal_reo2_ring_base = 0x0000055c,
	.hal_reo1_misc_ctrl_addr = 0x00000b7c,
	.hal_reo1_sw_cookie_cfg0 = 0x00000050,
	.hal_reo1_sw_cookie_cfg1 = 0x00000054,
	.hal_reo1_qdesc_lut_base0 = 0x00000058,
	.hal_reo1_qdesc_lut_base1 = 0x0000005c,
	.hal_reo1_ring_base_lsb = 0x000004e4,
	.hal_reo1_ring_base_msb = 0x000004e8,
	.hal_reo1_ring_id = 0x000004ec,
	.hal_reo1_ring_misc = 0x000004f4,
	.hal_reo1_ring_hp_addr_lsb = 0x000004f8,
	.hal_reo1_ring_hp_addr_msb = 0x000004fc,
	.hal_reo1_ring_producer_int_setup = 0x00000508,
	.hal_reo1_ring_msi1_base_lsb = 0x0000052C,
	.hal_reo1_ring_msi1_base_msb = 0x00000530,
	.hal_reo1_ring_msi1_data = 0x00000534,
	.hal_reo1_aging_thres_ix0 = 0x00000b08,
	.hal_reo1_aging_thres_ix1 = 0x00000b0c,
	.hal_reo1_aging_thres_ix2 = 0x00000b10,
	.hal_reo1_aging_thres_ix3 = 0x00000b14,

	/* REO Exception ring address */
	.hal_reo2_sw0_ring_base = 0x000008a4,

	/* REO Reinject ring address */
	.hal_sw2reo_ring_base = 0x00000304,
	.hal_sw2reo1_ring_base = 0x0000037c,

	/* REO cmd ring address */
	.hal_reo_cmd_ring_base = 0x0000028c,

	/* REO status ring address */
	.hal_reo_status_ring_base = 0x00000a84,

	/* CE base address */
	.hal_umac_ce0_src_reg_base = 0x01b80000,
	.hal_umac_ce0_dest_reg_base = 0x01b81000,
	.hal_umac_ce1_src_reg_base = 0x01b82000,
	.hal_umac_ce1_dest_reg_base = 0x01b83000,

	.gcc_gcc_pcie_hot_rst = 0x1e38338,
};

static const struct ath12k_hw_regs qcn9274_v2_regs = {
	/* SW2TCL(x) R0 ring configuration address */
	.hal_tcl1_ring_id = 0x00000908,
	.hal_tcl1_ring_misc = 0x00000910,
	.hal_tcl1_ring_tp_addr_lsb = 0x0000091c,
	.hal_tcl1_ring_tp_addr_msb = 0x00000920,
	.hal_tcl1_ring_consumer_int_setup_ix0 = 0x00000930,
	.hal_tcl1_ring_consumer_int_setup_ix1 = 0x00000934,
	.hal_tcl1_ring_msi1_base_lsb = 0x00000948,
	.hal_tcl1_ring_msi1_base_msb = 0x0000094c,
	.hal_tcl1_ring_msi1_data = 0x00000950,
	.hal_tcl_ring_base_lsb = 0x00000b58,
	.hal_tcl1_ring_base_lsb = 0x00000900,
	.hal_tcl1_ring_base_msb = 0x00000904,
	.hal_tcl2_ring_base_lsb = 0x00000978,

	/* TCL STATUS ring address */
	.hal_tcl_status_ring_base_lsb = 0x00000d38,

	/* WBM idle link ring address */
	.hal_wbm_idle_ring_base_lsb = 0x00000d3c,
	.hal_wbm_idle_ring_misc_addr = 0x00000d4c,
	.hal_wbm_r0_idle_list_cntl_addr = 0x00000240,
	.hal_wbm_r0_idle_list_size_addr = 0x00000244,
	.hal_wbm_scattered_ring_base_lsb = 0x00000250,
	.hal_wbm_scattered_ring_base_msb = 0x00000254,
	.hal_wbm_scattered_desc_head_info_ix0 = 0x00000260,
	.hal_wbm_scattered_desc_head_info_ix1 = 0x00000264,
	.hal_wbm_scattered_desc_tail_info_ix0 = 0x00000270,
	.hal_wbm_scattered_desc_tail_info_ix1 = 0x00000274,
	.hal_wbm_scattered_desc_ptr_hp_addr = 0x0000027c,

	/* SW2WBM release ring address */
	.hal_wbm_sw_release_ring_base_lsb = 0x0000037c,
	.hal_wbm_sw1_release_ring_base_lsb = 0x000003f4,

	/* WBM2SW release ring address */
	.hal_wbm0_release_ring_base_lsb = 0x00000e08,
	.hal_wbm1_release_ring_base_lsb = 0x00000e80,

	/* PCIe base address */
	.pcie_qserdes_sysclk_en_sel = 0x01e0c0a8,
	.pcie_pcs_osc_dtct_config_base = 0x01e0d45c,

	/* PPE release ring address */
	.hal_ppe_rel_ring_base = 0x0000046c,

	/* REO DEST ring address */
	.hal_reo2_ring_base = 0x00000578,
	.hal_reo1_misc_ctrl_addr = 0x00000b9c,
	.hal_reo1_sw_cookie_cfg0 = 0x0000006c,
	.hal_reo1_sw_cookie_cfg1 = 0x00000070,
	.hal_reo1_qdesc_lut_base0 = 0x00000074,
	.hal_reo1_qdesc_lut_base1 = 0x00000078,
	.hal_reo1_qdesc_addr = 0x0000007c,
	.hal_reo1_qdesc_max_peerid = 0x00000088,
	.hal_reo1_ring_base_lsb = 0x00000500,
	.hal_reo1_ring_base_msb = 0x00000504,
	.hal_reo1_ring_id = 0x00000508,
	.hal_reo1_ring_misc = 0x00000510,
	.hal_reo1_ring_hp_addr_lsb = 0x00000514,
	.hal_reo1_ring_hp_addr_msb = 0x00000518,
	.hal_reo1_ring_producer_int_setup = 0x00000524,
	.hal_reo1_ring_msi1_base_lsb = 0x00000548,
	.hal_reo1_ring_msi1_base_msb = 0x0000054C,
	.hal_reo1_ring_msi1_data = 0x00000550,
	.hal_reo1_aging_thres_ix0 = 0x00000B28,
	.hal_reo1_aging_thres_ix1 = 0x00000B2C,
	.hal_reo1_aging_thres_ix2 = 0x00000B30,
	.hal_reo1_aging_thres_ix3 = 0x00000B34,

	/* REO Exception ring address */
	.hal_reo2_sw0_ring_base = 0x000008c0,

	/* REO Reinject ring address */
	.hal_sw2reo_ring_base = 0x00000320,
	.hal_sw2reo1_ring_base = 0x00000398,

	/* REO cmd ring address */
	.hal_reo_cmd_ring_base = 0x000002A8,

	/* REO status ring address */
	.hal_reo_status_ring_base = 0x00000aa0,

	/* CE base address */
	.hal_umac_ce0_src_reg_base = 0x01b80000,
	.hal_umac_ce0_dest_reg_base = 0x01b81000,
	.hal_umac_ce1_src_reg_base = 0x01b82000,
	.hal_umac_ce1_dest_reg_base = 0x01b83000,

	.gcc_gcc_pcie_hot_rst = 0x1e38338,
};

static const struct ath12k_hw_regs ipq5332_regs = {
	/* SW2TCL(x) R0 ring configuration address */
	.hal_tcl1_ring_id = 0x00000918,
	.hal_tcl1_ring_misc = 0x00000920,
	.hal_tcl1_ring_tp_addr_lsb = 0x0000092c,
	.hal_tcl1_ring_tp_addr_msb = 0x00000930,
	.hal_tcl1_ring_consumer_int_setup_ix0 = 0x00000940,
	.hal_tcl1_ring_consumer_int_setup_ix1 = 0x00000944,
	.hal_tcl1_ring_msi1_base_lsb = 0x00000958,
	.hal_tcl1_ring_msi1_base_msb = 0x0000095c,
	.hal_tcl1_ring_base_lsb = 0x00000910,
	.hal_tcl1_ring_base_msb = 0x00000914,
	.hal_tcl1_ring_msi1_data = 0x00000960,
	.hal_tcl2_ring_base_lsb = 0x00000988,
	.hal_tcl_ring_base_lsb = 0x00000b68,

	/* TCL STATUS ring address */
	.hal_tcl_status_ring_base_lsb = 0x00000d48,

	/* REO DEST ring address */
	.hal_reo2_ring_base = 0x00000578,
	.hal_reo1_misc_ctrl_addr = 0x00000b9c,
	.hal_reo1_sw_cookie_cfg0 = 0x0000006c,
	.hal_reo1_sw_cookie_cfg1 = 0x00000070,
	.hal_reo1_qdesc_lut_base0 = 0x00000074,
	.hal_reo1_qdesc_lut_base1 = 0x00000078,
	.hal_reo1_ring_base_lsb = 0x00000500,
	.hal_reo1_ring_base_msb = 0x00000504,
	.hal_reo1_ring_id = 0x00000508,
	.hal_reo1_ring_misc = 0x00000510,
	.hal_reo1_ring_hp_addr_lsb = 0x00000514,
	.hal_reo1_ring_hp_addr_msb = 0x00000518,
	.hal_reo1_ring_producer_int_setup = 0x00000524,
	.hal_reo1_ring_msi1_base_lsb = 0x00000548,
	.hal_reo1_ring_msi1_base_msb = 0x0000054C,
	.hal_reo1_ring_msi1_data = 0x00000550,
	.hal_reo1_aging_thres_ix0 = 0x00000B28,
	.hal_reo1_aging_thres_ix1 = 0x00000B2C,
	.hal_reo1_aging_thres_ix2 = 0x00000B30,
	.hal_reo1_aging_thres_ix3 = 0x00000B34,

	/* REO Exception ring address */
	.hal_reo2_sw0_ring_base = 0x000008c0,

	/* REO Reinject ring address */
	.hal_sw2reo_ring_base = 0x00000320,
	.hal_sw2reo1_ring_base = 0x00000398,

	/* REO cmd ring address */
	.hal_reo_cmd_ring_base = 0x000002A8,

	/* REO status ring address */
	.hal_reo_status_ring_base = 0x00000aa0,

	/* WBM idle link ring address */
	.hal_wbm_idle_ring_base_lsb = 0x00000d3c,
	.hal_wbm_idle_ring_misc_addr = 0x00000d4c,
	.hal_wbm_r0_idle_list_cntl_addr = 0x00000240,
	.hal_wbm_r0_idle_list_size_addr = 0x00000244,
	.hal_wbm_scattered_ring_base_lsb = 0x00000250,
	.hal_wbm_scattered_ring_base_msb = 0x00000254,
	.hal_wbm_scattered_desc_head_info_ix0 = 0x00000260,
	.hal_wbm_scattered_desc_head_info_ix1   = 0x00000264,
	.hal_wbm_scattered_desc_tail_info_ix0 = 0x00000270,
	.hal_wbm_scattered_desc_tail_info_ix1 = 0x00000274,
	.hal_wbm_scattered_desc_ptr_hp_addr = 0x0000027c,

	/* SW2WBM release ring address */
	.hal_wbm_sw_release_ring_base_lsb = 0x0000037c,

	/* WBM2SW release ring address */
	.hal_wbm0_release_ring_base_lsb = 0x00000e08,
	.hal_wbm1_release_ring_base_lsb = 0x00000e80,

	/* PPE release ring address */
	.hal_ppe_rel_ring_base = 0x0000046c,

	/* CE address */
	.hal_umac_ce0_src_reg_base = 0x00740000 -
		HAL_IPQ5332_CE_WFSS_REG_BASE,
	.hal_umac_ce0_dest_reg_base = 0x00741000 -
		HAL_IPQ5332_CE_WFSS_REG_BASE,
	.hal_umac_ce1_src_reg_base = 0x00742000 -
		HAL_IPQ5332_CE_WFSS_REG_BASE,
	.hal_umac_ce1_dest_reg_base = 0x00743000 -
		HAL_IPQ5332_CE_WFSS_REG_BASE,
};

static const struct ath12k_hw_regs wcn7850_regs = {
	/* SW2TCL(x) R0 ring configuration address */
	.hal_tcl1_ring_id = 0x00000908,
	.hal_tcl1_ring_misc = 0x00000910,
	.hal_tcl1_ring_tp_addr_lsb = 0x0000091c,
	.hal_tcl1_ring_tp_addr_msb = 0x00000920,
	.hal_tcl1_ring_consumer_int_setup_ix0 = 0x00000930,
	.hal_tcl1_ring_consumer_int_setup_ix1 = 0x00000934,
	.hal_tcl1_ring_msi1_base_lsb = 0x00000948,
	.hal_tcl1_ring_msi1_base_msb = 0x0000094c,
	.hal_tcl1_ring_msi1_data = 0x00000950,
	.hal_tcl_ring_base_lsb = 0x00000b58,
	.hal_tcl1_ring_base_lsb = 0x00000900,
	.hal_tcl1_ring_base_msb = 0x00000904,
	.hal_tcl2_ring_base_lsb = 0x00000978,

	/* TCL STATUS ring address */
	.hal_tcl_status_ring_base_lsb = 0x00000d38,

	.hal_wbm_idle_ring_base_lsb = 0x00000d3c,
	.hal_wbm_idle_ring_misc_addr = 0x00000d4c,
	.hal_wbm_r0_idle_list_cntl_addr = 0x00000240,
	.hal_wbm_r0_idle_list_size_addr = 0x00000244,
	.hal_wbm_scattered_ring_base_lsb = 0x00000250,
	.hal_wbm_scattered_ring_base_msb = 0x00000254,
	.hal_wbm_scattered_desc_head_info_ix0 = 0x00000260,
	.hal_wbm_scattered_desc_head_info_ix1 = 0x00000264,
	.hal_wbm_scattered_desc_tail_info_ix0 = 0x00000270,
	.hal_wbm_scattered_desc_tail_info_ix1 = 0x00000274,
	.hal_wbm_scattered_desc_ptr_hp_addr = 0x00000027c,

	.hal_wbm_sw_release_ring_base_lsb = 0x0000037c,
	.hal_wbm_sw1_release_ring_base_lsb = 0x00000284,
	.hal_wbm0_release_ring_base_lsb = 0x00000e08,
	.hal_wbm1_release_ring_base_lsb = 0x00000e80,

	/* PCIe base address */
	.pcie_qserdes_sysclk_en_sel = 0x01e0e0a8,
	.pcie_pcs_osc_dtct_config_base = 0x01e0f45c,

	/* PPE release ring address */
	.hal_ppe_rel_ring_base = 0x0000043c,

	/* REO DEST ring address */
	.hal_reo2_ring_base = 0x0000055c,
	.hal_reo1_misc_ctrl_addr = 0x00000b7c,
	.hal_reo1_sw_cookie_cfg0 = 0x00000050,
	.hal_reo1_sw_cookie_cfg1 = 0x00000054,
	.hal_reo1_qdesc_lut_base0 = 0x00000058,
	.hal_reo1_qdesc_lut_base1 = 0x0000005c,
	.hal_reo1_ring_base_lsb = 0x000004e4,
	.hal_reo1_ring_base_msb = 0x000004e8,
	.hal_reo1_ring_id = 0x000004ec,
	.hal_reo1_ring_misc = 0x000004f4,
	.hal_reo1_ring_hp_addr_lsb = 0x000004f8,
	.hal_reo1_ring_hp_addr_msb = 0x000004fc,
	.hal_reo1_ring_producer_int_setup = 0x00000508,
	.hal_reo1_ring_msi1_base_lsb = 0x0000052C,
	.hal_reo1_ring_msi1_base_msb = 0x00000530,
	.hal_reo1_ring_msi1_data = 0x00000534,
	.hal_reo1_aging_thres_ix0 = 0x00000b08,
	.hal_reo1_aging_thres_ix1 = 0x00000b0c,
	.hal_reo1_aging_thres_ix2 = 0x00000b10,
	.hal_reo1_aging_thres_ix3 = 0x00000b14,

	/* REO Exception ring address */
	.hal_reo2_sw0_ring_base = 0x000008a4,

	/* REO Reinject ring address */
	.hal_sw2reo_ring_base = 0x00000304,
	.hal_sw2reo1_ring_base = 0x0000037c,

	/* REO cmd ring address */
	.hal_reo_cmd_ring_base = 0x0000028c,

	/* REO status ring address */
	.hal_reo_status_ring_base = 0x00000a84,

	/* CE base address */
	.hal_umac_ce0_src_reg_base = 0x01b80000,
	.hal_umac_ce0_dest_reg_base = 0x01b81000,
	.hal_umac_ce1_src_reg_base = 0x01b82000,
	.hal_umac_ce1_dest_reg_base = 0x01b83000,

	.gcc_gcc_pcie_hot_rst = 0x1e40304,
};

static const struct ath12k_hw_hal_params ath12k_hw_hal_params_qcn9274 = {
	.rx_buf_rbm = HAL_RX_BUF_RBM_SW3_BM,
	.wbm2sw_cc_enable = HAL_WBM_SW_COOKIE_CONV_CFG_WBM2SW0_EN |
			    HAL_WBM_SW_COOKIE_CONV_CFG_WBM2SW1_EN |
			    HAL_WBM_SW_COOKIE_CONV_CFG_WBM2SW2_EN |
			    HAL_WBM_SW_COOKIE_CONV_CFG_WBM2SW3_EN |
			    HAL_WBM_SW_COOKIE_CONV_CFG_WBM2SW4_EN,
};

static const struct ath12k_hw_hal_params ath12k_hw_hal_params_wcn7850 = {
	.rx_buf_rbm = HAL_RX_BUF_RBM_SW1_BM,
	.wbm2sw_cc_enable = HAL_WBM_SW_COOKIE_CONV_CFG_WBM2SW0_EN |
			    HAL_WBM_SW_COOKIE_CONV_CFG_WBM2SW2_EN |
			    HAL_WBM_SW_COOKIE_CONV_CFG_WBM2SW3_EN |
			    HAL_WBM_SW_COOKIE_CONV_CFG_WBM2SW4_EN,
};

static const struct ath12k_hw_hal_params ath12k_hw_hal_params_ipq5332 = {
	.rx_buf_rbm = HAL_RX_BUF_RBM_SW3_BM,
	.wbm2sw_cc_enable = HAL_WBM_SW_COOKIE_CONV_CFG_WBM2SW0_EN |
			    HAL_WBM_SW_COOKIE_CONV_CFG_WBM2SW1_EN |
			    HAL_WBM_SW_COOKIE_CONV_CFG_WBM2SW2_EN |
			    HAL_WBM_SW_COOKIE_CONV_CFG_WBM2SW3_EN |
			    HAL_WBM_SW_COOKIE_CONV_CFG_WBM2SW4_EN,
};

static const struct ce_ie_addr ath12k_ce_ie_addr_ipq5332 = {
	.ie1_reg_addr = CE_HOST_IE_ADDRESS - HAL_IPQ5332_CE_WFSS_REG_BASE,
	.ie2_reg_addr = CE_HOST_IE_2_ADDRESS - HAL_IPQ5332_CE_WFSS_REG_BASE,
	.ie3_reg_addr = CE_HOST_IE_3_ADDRESS - HAL_IPQ5332_CE_WFSS_REG_BASE,
};

static const struct ce_remap ath12k_ce_remap_ipq5332 = {
	.base = HAL_IPQ5332_CE_WFSS_REG_BASE,
	.size = HAL_IPQ5332_CE_SIZE,
};

static const struct ath12k_hw_params ath12k_hw_params[] = {
	{
		.name = "qcn9274 hw1.0",
		.hw_rev = ATH12K_HW_QCN9274_HW10,
		.fw = {
			.dir = "QCN9274/hw1.0",
			.board_size = 256 * 1024,
			.cal_offset = 128 * 1024,
			.m3_loader = ath12k_m3_fw_loader_driver,
		},
		.max_radios = 1,
		.single_pdev_only = false,
		.qmi_service_ins_id = ATH12K_QMI_WLFW_SERVICE_INS_ID_V01_QCN9274,
		.internal_sleep_clock = false,

		.hw_ops = &qcn9274_ops,
		.ring_mask = &ath12k_hw_ring_mask_qcn9274,
		.regs = &qcn9274_v1_regs,

		.host_ce_config = ath12k_host_ce_config_qcn9274,
		.ce_count = 16,
		.target_ce_config = ath12k_target_ce_config_wlan_qcn9274,
		.target_ce_count = 12,
		.svc_to_ce_map = ath12k_target_service_to_ce_map_wlan_qcn9274,
		.svc_to_ce_map_len = 18,

		.hal_params = &ath12k_hw_hal_params_qcn9274,

		.rxdma1_enable = false,
		.num_rxdma_per_pdev = 1,
		.num_rxdma_dst_ring = 0,
		.rx_mac_buf_ring = false,
		.vdev_start_delay = false,

		.interface_modes = BIT(NL80211_IFTYPE_STATION) |
					BIT(NL80211_IFTYPE_AP) |
					BIT(NL80211_IFTYPE_MESH_POINT) |
					BIT(NL80211_IFTYPE_AP_VLAN),
		.supports_monitor = false,

		.idle_ps = false,
		.download_calib = true,
		.supports_suspend = false,
		.tcl_ring_retry = true,
		.reoq_lut_support = true,
		.supports_shadow_regs = false,

		.num_tcl_banks = 48,
		.max_tx_ring = 4,

		.mhi_config = &ath12k_mhi_config_qcn9274,

		.wmi_init = ath12k_wmi_init_qcn9274,

		.hal_ops = &hal_qcn9274_ops,

		.qmi_cnss_feature_bitmap = BIT(CNSS_QDSS_CFG_MISS_V01),

		.rfkill_pin = 0,
		.rfkill_cfg = 0,
		.rfkill_on_level = 0,

		.rddm_size = 0x600000,

		.def_num_link = 0,
		.max_mlo_peer = 256,

		.otp_board_id_register = QCN9274_QFPROM_RAW_RFA_PDET_ROW13_LSB,

		.supports_sta_ps = false,

		.acpi_guid = NULL,
		.supports_dynamic_smps_6ghz = true,

		.iova_mask = 0,

		.supports_aspm = false,

		.ce_ie_addr = NULL,
		.ce_remap = NULL,
		.bdf_addr_offset = 0,

		.current_cc_support = false,

		.dp_primary_link_only = true,
	},
	{
		.name = "wcn7850 hw2.0",
		.hw_rev = ATH12K_HW_WCN7850_HW20,

		.fw = {
			.dir = "WCN7850/hw2.0",
			.board_size = 256 * 1024,
			.cal_offset = 256 * 1024,
			.m3_loader = ath12k_m3_fw_loader_driver,
		},

		.max_radios = 1,
		.single_pdev_only = true,
		.qmi_service_ins_id = ATH12K_QMI_WLFW_SERVICE_INS_ID_V01_WCN7850,
		.internal_sleep_clock = true,

		.hw_ops = &wcn7850_ops,
		.ring_mask = &ath12k_hw_ring_mask_wcn7850,
		.regs = &wcn7850_regs,

		.host_ce_config = ath12k_host_ce_config_wcn7850,
		.ce_count = 9,
		.target_ce_config = ath12k_target_ce_config_wlan_wcn7850,
		.target_ce_count = 9,
		.svc_to_ce_map = ath12k_target_service_to_ce_map_wlan_wcn7850,
		.svc_to_ce_map_len = 14,

		.hal_params = &ath12k_hw_hal_params_wcn7850,

		.rxdma1_enable = false,
		.num_rxdma_per_pdev = 2,
		.num_rxdma_dst_ring = 1,
		.rx_mac_buf_ring = true,
		.vdev_start_delay = true,

		.interface_modes = BIT(NL80211_IFTYPE_STATION) |
				   BIT(NL80211_IFTYPE_AP) |
				   BIT(NL80211_IFTYPE_P2P_DEVICE) |
				   BIT(NL80211_IFTYPE_P2P_CLIENT) |
				   BIT(NL80211_IFTYPE_P2P_GO),
		.supports_monitor = true,

		.idle_ps = true,
		.download_calib = false,
		.supports_suspend = true,
		.tcl_ring_retry = false,
		.reoq_lut_support = false,
		.supports_shadow_regs = true,

		.num_tcl_banks = 7,
		.max_tx_ring = 3,

		.mhi_config = &ath12k_mhi_config_wcn7850,

		.wmi_init = ath12k_wmi_init_wcn7850,

		.hal_ops = &hal_wcn7850_ops,

		.qmi_cnss_feature_bitmap = BIT(CNSS_QDSS_CFG_MISS_V01) |
					   BIT(CNSS_PCIE_PERST_NO_PULL_V01),

		.rfkill_pin = 48,
		.rfkill_cfg = 0,
		.rfkill_on_level = 1,

		.rddm_size = 0x780000,

		.def_num_link = 2,
		.max_mlo_peer = 32,

		.otp_board_id_register = 0,

		.supports_sta_ps = true,

		.acpi_guid = &wcn7850_uuid,
		.supports_dynamic_smps_6ghz = false,

		.iova_mask = ATH12K_PCIE_MAX_PAYLOAD_SIZE - 1,

		.supports_aspm = true,

		.ce_ie_addr = NULL,
		.ce_remap = NULL,
		.bdf_addr_offset = 0,

		.current_cc_support = true,

		.dp_primary_link_only = false,
	},
	{
		.name = "qcn9274 hw2.0",
		.hw_rev = ATH12K_HW_QCN9274_HW20,
		.fw = {
			.dir = "QCN9274/hw2.0",
			.board_size = 256 * 1024,
			.cal_offset = 128 * 1024,
			.m3_loader = ath12k_m3_fw_loader_driver,
		},
		.max_radios = 2,
		.single_pdev_only = false,
		.qmi_service_ins_id = ATH12K_QMI_WLFW_SERVICE_INS_ID_V01_QCN9274,
		.internal_sleep_clock = false,

		.hw_ops = &qcn9274_ops,
		.ring_mask = &ath12k_hw_ring_mask_qcn9274,
		.regs = &qcn9274_v2_regs,

		.host_ce_config = ath12k_host_ce_config_qcn9274,
		.ce_count = 16,
		.target_ce_config = ath12k_target_ce_config_wlan_qcn9274,
		.target_ce_count = 12,
		.svc_to_ce_map = ath12k_target_service_to_ce_map_wlan_qcn9274,
		.svc_to_ce_map_len = 18,

		.hal_params = &ath12k_hw_hal_params_qcn9274,

		.rxdma1_enable = true,
		.num_rxdma_per_pdev = 1,
		.num_rxdma_dst_ring = 0,
		.rx_mac_buf_ring = false,
		.vdev_start_delay = false,

		.interface_modes = BIT(NL80211_IFTYPE_STATION) |
					BIT(NL80211_IFTYPE_AP) |
					BIT(NL80211_IFTYPE_MESH_POINT) |
					BIT(NL80211_IFTYPE_AP_VLAN),
		.supports_monitor = true,

		.idle_ps = false,
		.download_calib = true,
		.supports_suspend = false,
		.tcl_ring_retry = true,
		.reoq_lut_support = true,
		.supports_shadow_regs = false,

		.num_tcl_banks = 48,
		.max_tx_ring = 4,

		.mhi_config = &ath12k_mhi_config_qcn9274,

		.wmi_init = ath12k_wmi_init_qcn9274,

		.hal_ops = &hal_qcn9274_ops,

		.qmi_cnss_feature_bitmap = BIT(CNSS_QDSS_CFG_MISS_V01),

		.rfkill_pin = 0,
		.rfkill_cfg = 0,
		.rfkill_on_level = 0,

		.rddm_size = 0x600000,

		.def_num_link = 0,
		.max_mlo_peer = 256,

		.otp_board_id_register = QCN9274_QFPROM_RAW_RFA_PDET_ROW13_LSB,

		.supports_sta_ps = false,

		.acpi_guid = NULL,
		.supports_dynamic_smps_6ghz = true,

		.iova_mask = 0,

		.supports_aspm = false,

		.ce_ie_addr = NULL,
		.ce_remap = NULL,
		.bdf_addr_offset = 0,

		.current_cc_support = false,

		.dp_primary_link_only = true,
	},
	{
		.name = "ipq5332 hw1.0",
		.hw_rev = ATH12K_HW_IPQ5332_HW10,
		.fw = {
			.dir = "IPQ5332/hw1.0",
			.board_size = 256 * 1024,
			.cal_offset = 128 * 1024,
			.m3_loader = ath12k_m3_fw_loader_remoteproc,
		},
		.max_radios = 1,
		.single_pdev_only = false,
		.qmi_service_ins_id = ATH12K_QMI_WLFW_SERVICE_INS_ID_V01_IPQ5332,
		.internal_sleep_clock = false,

		.hw_ops = &qcn9274_ops,
		.regs = &ipq5332_regs,
		.ring_mask = &ath12k_hw_ring_mask_ipq5332,

		.host_ce_config = ath12k_host_ce_config_ipq5332,
		.ce_count = 12,
		.target_ce_config = ath12k_target_ce_config_wlan_ipq5332,
		.target_ce_count = 12,
		.svc_to_ce_map = ath12k_target_service_to_ce_map_wlan_ipq5332,
		.svc_to_ce_map_len = 18,

		.hal_params = &ath12k_hw_hal_params_ipq5332,

		.rxdma1_enable = false,
		.num_rxdma_per_pdev = 1,
		.num_rxdma_dst_ring = 0,
		.rx_mac_buf_ring = false,
		.vdev_start_delay = false,

		.interface_modes = BIT(NL80211_IFTYPE_STATION) |
				   BIT(NL80211_IFTYPE_AP) |
				   BIT(NL80211_IFTYPE_MESH_POINT),
		.supports_monitor = false,

		.idle_ps = false,
		.download_calib = true,
		.supports_suspend = false,
		.tcl_ring_retry = true,
		.reoq_lut_support = false,
		.supports_shadow_regs = false,

		.num_tcl_banks = 48,
		.max_tx_ring = 4,

		.wmi_init = &ath12k_wmi_init_qcn9274,

		.hal_ops = &hal_qcn9274_ops,

		.qmi_cnss_feature_bitmap = BIT(CNSS_QDSS_CFG_MISS_V01),

		.rfkill_pin = 0,
		.rfkill_cfg = 0,
		.rfkill_on_level = 0,

		.rddm_size = 0,

		.def_num_link = 0,
		.max_mlo_peer = 256,

		.otp_board_id_register = 0,

		.supports_sta_ps = false,

		.acpi_guid = NULL,
		.supports_dynamic_smps_6ghz = false,
		.iova_mask = 0,
		.supports_aspm = false,

		.ce_ie_addr = &ath12k_ce_ie_addr_ipq5332,
		.ce_remap = &ath12k_ce_remap_ipq5332,
		.bdf_addr_offset = 0xC00000,

		.dp_primary_link_only = true,
	},
};

int ath12k_hw_init(struct ath12k_base *ab)
{
	const struct ath12k_hw_params *hw_params = NULL;
	int i;

	for (i = 0; i < ARRAY_SIZE(ath12k_hw_params); i++) {
		hw_params = &ath12k_hw_params[i];

		if (hw_params->hw_rev == ab->hw_rev)
			break;
	}

	if (i == ARRAY_SIZE(ath12k_hw_params)) {
		ath12k_err(ab, "Unsupported hardware version: 0x%x\n", ab->hw_rev);
		return -EINVAL;
	}

	ab->hw_params = hw_params;

	ath12k_info(ab, "Hardware name: %s\n", ab->hw_params->name);

	return 0;
}
