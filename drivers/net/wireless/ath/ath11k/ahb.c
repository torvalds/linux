// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) 2018-2019 The Linux Foundation. All rights reserved.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of_device.h>
#include <linux/of.h>
#include <linux/dma-mapping.h>
#include "ahb.h"
#include "debug.h"
#include <linux/remoteproc.h>

static const struct of_device_id ath11k_ahb_of_match[] = {
	/* TODO: Should we change the compatible string to something similar
	 * to one that ath10k uses?
	 */
	{ .compatible = "qcom,ipq8074-wifi",
	  .data = (void *)ATH11K_HW_IPQ8074,
	},
	{ }
};

MODULE_DEVICE_TABLE(of, ath11k_ahb_of_match);

/* Target firmware's Copy Engine configuration. */
static const struct ce_pipe_config target_ce_config_wlan[] = {
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
		.flags = __cpu_to_le32(0),
		.reserved = __cpu_to_le32(0),
	},

	/* CE6: Reserved for target autonomous hif_memcpy */
	{
		.pipenum = __cpu_to_le32(6),
		.pipedir = __cpu_to_le32(PIPEDIR_INOUT),
		.nentries = __cpu_to_le32(32),
		.nbytes_max = __cpu_to_le32(65535),
		.flags = __cpu_to_le32(CE_ATTR_FLAGS),
		.reserved = __cpu_to_le32(0),
	},

	/* CE7 used only by Host */
	{
		.pipenum = __cpu_to_le32(7),
		.pipedir = __cpu_to_le32(PIPEDIR_OUT),
		.nentries = __cpu_to_le32(32),
		.nbytes_max = __cpu_to_le32(2048),
		.flags = __cpu_to_le32(CE_ATTR_FLAGS),
		.reserved = __cpu_to_le32(0),
	},

	/* CE8 target->host used only by IPA */
	{
		.pipenum = __cpu_to_le32(8),
		.pipedir = __cpu_to_le32(PIPEDIR_INOUT),
		.nentries = __cpu_to_le32(32),
		.nbytes_max = __cpu_to_le32(65535),
		.flags = __cpu_to_le32(CE_ATTR_FLAGS),
		.reserved = __cpu_to_le32(0),
	},

	/* CE9 host->target HTT */
	{
		.pipenum = __cpu_to_le32(9),
		.pipedir = __cpu_to_le32(PIPEDIR_OUT),
		.nentries = __cpu_to_le32(32),
		.nbytes_max = __cpu_to_le32(2048),
		.flags = __cpu_to_le32(CE_ATTR_FLAGS),
		.reserved = __cpu_to_le32(0),
	},

	/* CE10 target->host HTT */
	{
		.pipenum = __cpu_to_le32(10),
		.pipedir = __cpu_to_le32(PIPEDIR_INOUT_H2H),
		.nentries = __cpu_to_le32(0),
		.nbytes_max = __cpu_to_le32(0),
		.flags = __cpu_to_le32(CE_ATTR_FLAGS),
		.reserved = __cpu_to_le32(0),
	},

	/* CE11 Not used */
	{
		.pipenum = __cpu_to_le32(0),
		.pipedir = __cpu_to_le32(0),
		.nentries = __cpu_to_le32(0),
		.nbytes_max = __cpu_to_le32(0),
		.flags = __cpu_to_le32(CE_ATTR_FLAGS),
		.reserved = __cpu_to_le32(0),
	},
};

/* Map from service/endpoint to Copy Engine.
 * This table is derived from the CE_PCI TABLE, above.
 * It is passed to the Target at startup for use by firmware.
 */
static const struct service_to_pipe target_service_to_ce_map_wlan[] = {
	{
		.service_id = __cpu_to_le32(ATH11K_HTC_SVC_ID_WMI_DATA_VO),
		.pipedir = __cpu_to_le32(PIPEDIR_OUT),	/* out = UL = host -> target */
		.pipenum = __cpu_to_le32(3),
	},
	{
		.service_id = __cpu_to_le32(ATH11K_HTC_SVC_ID_WMI_DATA_VO),
		.pipedir = __cpu_to_le32(PIPEDIR_IN),	/* in = DL = target -> host */
		.pipenum = __cpu_to_le32(2),
	},
	{
		.service_id = __cpu_to_le32(ATH11K_HTC_SVC_ID_WMI_DATA_BK),
		.pipedir = __cpu_to_le32(PIPEDIR_OUT),	/* out = UL = host -> target */
		.pipenum = __cpu_to_le32(3),
	},
	{
		.service_id = __cpu_to_le32(ATH11K_HTC_SVC_ID_WMI_DATA_BK),
		.pipedir = __cpu_to_le32(PIPEDIR_IN),	/* in = DL = target -> host */
		.pipenum = __cpu_to_le32(2),
	},
	{
		.service_id = __cpu_to_le32(ATH11K_HTC_SVC_ID_WMI_DATA_BE),
		.pipedir = __cpu_to_le32(PIPEDIR_OUT),	/* out = UL = host -> target */
		.pipenum = __cpu_to_le32(3),
	},
	{
		.service_id = __cpu_to_le32(ATH11K_HTC_SVC_ID_WMI_DATA_BE),
		.pipedir = __cpu_to_le32(PIPEDIR_IN),	/* in = DL = target -> host */
		.pipenum = __cpu_to_le32(2),
	},
	{
		.service_id = __cpu_to_le32(ATH11K_HTC_SVC_ID_WMI_DATA_VI),
		.pipedir = __cpu_to_le32(PIPEDIR_OUT),	/* out = UL = host -> target */
		.pipenum = __cpu_to_le32(3),
	},
	{
		.service_id = __cpu_to_le32(ATH11K_HTC_SVC_ID_WMI_DATA_VI),
		.pipedir = __cpu_to_le32(PIPEDIR_IN),	/* in = DL = target -> host */
		.pipenum = __cpu_to_le32(2),
	},
	{
		.service_id = __cpu_to_le32(ATH11K_HTC_SVC_ID_WMI_CONTROL),
		.pipedir = __cpu_to_le32(PIPEDIR_OUT),	/* out = UL = host -> target */
		.pipenum = __cpu_to_le32(3),
	},
	{
		.service_id = __cpu_to_le32(ATH11K_HTC_SVC_ID_WMI_CONTROL),
		.pipedir = __cpu_to_le32(PIPEDIR_IN),	/* in = DL = target -> host */
		.pipenum = __cpu_to_le32(2),
	},
	{
		.service_id = __cpu_to_le32(ATH11K_HTC_SVC_ID_WMI_CONTROL_MAC1),
		.pipedir = __cpu_to_le32(PIPEDIR_OUT),	/* out = UL = host -> target */
		.pipenum = __cpu_to_le32(7),
	},
	{
		.service_id = __cpu_to_le32(ATH11K_HTC_SVC_ID_WMI_CONTROL_MAC1),
		.pipedir = __cpu_to_le32(PIPEDIR_IN),	/* in = DL = target -> host */
		.pipenum = __cpu_to_le32(2),
	},
	{
		.service_id = __cpu_to_le32(ATH11K_HTC_SVC_ID_WMI_CONTROL_MAC2),
		.pipedir = __cpu_to_le32(PIPEDIR_OUT),	/* out = UL = host -> target */
		.pipenum = __cpu_to_le32(9),
	},
	{
		.service_id = __cpu_to_le32(ATH11K_HTC_SVC_ID_WMI_CONTROL_MAC2),
		.pipedir = __cpu_to_le32(PIPEDIR_IN),	/* in = DL = target -> host */
		.pipenum = __cpu_to_le32(2),
	},
	{
		.service_id = __cpu_to_le32(ATH11K_HTC_SVC_ID_RSVD_CTRL),
		.pipedir = __cpu_to_le32(PIPEDIR_OUT),	/* out = UL = host -> target */
		.pipenum = __cpu_to_le32(0),
	},
	{
		.service_id = __cpu_to_le32(ATH11K_HTC_SVC_ID_RSVD_CTRL),
		.pipedir = __cpu_to_le32(PIPEDIR_IN),	/* in = DL = target -> host */
		.pipenum = __cpu_to_le32(1),
	},
	{ /* not used */
		.service_id = __cpu_to_le32(ATH11K_HTC_SVC_ID_TEST_RAW_STREAMS),
		.pipedir = __cpu_to_le32(PIPEDIR_OUT),	/* out = UL = host -> target */
		.pipenum = __cpu_to_le32(0),
	},
	{ /* not used */
		.service_id = __cpu_to_le32(ATH11K_HTC_SVC_ID_TEST_RAW_STREAMS),
		.pipedir = __cpu_to_le32(PIPEDIR_IN),	/* in = DL = target -> host */
		.pipenum = __cpu_to_le32(1),
	},
	{
		.service_id = __cpu_to_le32(ATH11K_HTC_SVC_ID_HTT_DATA_MSG),
		.pipedir = __cpu_to_le32(PIPEDIR_OUT),	/* out = UL = host -> target */
		.pipenum = __cpu_to_le32(4),
	},
	{
		.service_id = __cpu_to_le32(ATH11K_HTC_SVC_ID_HTT_DATA_MSG),
		.pipedir = __cpu_to_le32(PIPEDIR_IN),	/* in = DL = target -> host */
		.pipenum = __cpu_to_le32(1),
	},
	{
		.service_id = __cpu_to_le32(ATH11K_HTC_SVC_ID_PKT_LOG),
		.pipedir = __cpu_to_le32(PIPEDIR_IN),	/* in = DL = target -> host */
		.pipenum = __cpu_to_le32(5),
	},

	/* (Additions here) */

	{ /* terminator entry */ }
};

#define ATH11K_IRQ_CE0_OFFSET 4

static const char *irq_name[ATH11K_IRQ_NUM_MAX] = {
	"misc-pulse1",
	"misc-latch",
	"sw-exception",
	"watchdog",
	"ce0",
	"ce1",
	"ce2",
	"ce3",
	"ce4",
	"ce5",
	"ce6",
	"ce7",
	"ce8",
	"ce9",
	"ce10",
	"ce11",
	"host2wbm-desc-feed",
	"host2reo-re-injection",
	"host2reo-command",
	"host2rxdma-monitor-ring3",
	"host2rxdma-monitor-ring2",
	"host2rxdma-monitor-ring1",
	"reo2ost-exception",
	"wbm2host-rx-release",
	"reo2host-status",
	"reo2host-destination-ring4",
	"reo2host-destination-ring3",
	"reo2host-destination-ring2",
	"reo2host-destination-ring1",
	"rxdma2host-monitor-destination-mac3",
	"rxdma2host-monitor-destination-mac2",
	"rxdma2host-monitor-destination-mac1",
	"ppdu-end-interrupts-mac3",
	"ppdu-end-interrupts-mac2",
	"ppdu-end-interrupts-mac1",
	"rxdma2host-monitor-status-ring-mac3",
	"rxdma2host-monitor-status-ring-mac2",
	"rxdma2host-monitor-status-ring-mac1",
	"host2rxdma-host-buf-ring-mac3",
	"host2rxdma-host-buf-ring-mac2",
	"host2rxdma-host-buf-ring-mac1",
	"rxdma2host-destination-ring-mac3",
	"rxdma2host-destination-ring-mac2",
	"rxdma2host-destination-ring-mac1",
	"host2tcl-input-ring4",
	"host2tcl-input-ring3",
	"host2tcl-input-ring2",
	"host2tcl-input-ring1",
	"wbm2host-tx-completions-ring3",
	"wbm2host-tx-completions-ring2",
	"wbm2host-tx-completions-ring1",
	"tcl2host-status-ring",
};

#define ATH11K_TX_RING_MASK_0 0x1
#define ATH11K_TX_RING_MASK_1 0x2
#define ATH11K_TX_RING_MASK_2 0x4

#define ATH11K_RX_RING_MASK_0 0x1
#define ATH11K_RX_RING_MASK_1 0x2
#define ATH11K_RX_RING_MASK_2 0x4
#define ATH11K_RX_RING_MASK_3 0x8

#define ATH11K_RX_ERR_RING_MASK_0 0x1

#define ATH11K_RX_WBM_REL_RING_MASK_0 0x1

#define ATH11K_REO_STATUS_RING_MASK_0 0x1

#define ATH11K_RXDMA2HOST_RING_MASK_0 0x1
#define ATH11K_RXDMA2HOST_RING_MASK_1 0x2
#define ATH11K_RXDMA2HOST_RING_MASK_2 0x4

#define ATH11K_HOST2RXDMA_RING_MASK_0 0x1
#define ATH11K_HOST2RXDMA_RING_MASK_1 0x2
#define ATH11K_HOST2RXDMA_RING_MASK_2 0x4

#define ATH11K_RX_MON_STATUS_RING_MASK_0 0x1
#define ATH11K_RX_MON_STATUS_RING_MASK_1 0x2
#define ATH11K_RX_MON_STATUS_RING_MASK_2 0x4

const u8 ath11k_tx_ring_mask[ATH11K_EXT_IRQ_GRP_NUM_MAX] = {
	ATH11K_TX_RING_MASK_0,
	ATH11K_TX_RING_MASK_1,
	ATH11K_TX_RING_MASK_2,
};

const u8 rx_mon_status_ring_mask[ATH11K_EXT_IRQ_GRP_NUM_MAX] = {
	0, 0, 0, 0,
	ATH11K_RX_MON_STATUS_RING_MASK_0,
	ATH11K_RX_MON_STATUS_RING_MASK_1,
	ATH11K_RX_MON_STATUS_RING_MASK_2,
};

const u8 ath11k_rx_ring_mask[ATH11K_EXT_IRQ_GRP_NUM_MAX] = {
	0, 0, 0, 0, 0, 0, 0,
	ATH11K_RX_RING_MASK_0,
	ATH11K_RX_RING_MASK_1,
	ATH11K_RX_RING_MASK_2,
	ATH11K_RX_RING_MASK_3,
};

const u8 ath11k_rx_err_ring_mask[ATH11K_EXT_IRQ_GRP_NUM_MAX] = {
	ATH11K_RX_ERR_RING_MASK_0,
};

const u8 ath11k_rx_wbm_rel_ring_mask[ATH11K_EXT_IRQ_GRP_NUM_MAX] = {
	ATH11K_RX_WBM_REL_RING_MASK_0,
};

const u8 ath11k_reo_status_ring_mask[ATH11K_EXT_IRQ_GRP_NUM_MAX] = {
	ATH11K_REO_STATUS_RING_MASK_0,
};

const u8 ath11k_rxdma2host_ring_mask[ATH11K_EXT_IRQ_GRP_NUM_MAX] = {
	ATH11K_RXDMA2HOST_RING_MASK_0,
	ATH11K_RXDMA2HOST_RING_MASK_1,
	ATH11K_RXDMA2HOST_RING_MASK_2,
};

const u8 ath11k_host2rxdma_ring_mask[ATH11K_EXT_IRQ_GRP_NUM_MAX] = {
	ATH11K_HOST2RXDMA_RING_MASK_0,
	ATH11K_HOST2RXDMA_RING_MASK_1,
	ATH11K_HOST2RXDMA_RING_MASK_2,
};

/* enum ext_irq_num - irq numbers that can be used by external modules
 * like datapath
 */
enum ext_irq_num {
	host2wbm_desc_feed = 16,
	host2reo_re_injection,
	host2reo_command,
	host2rxdma_monitor_ring3,
	host2rxdma_monitor_ring2,
	host2rxdma_monitor_ring1,
	reo2host_exception,
	wbm2host_rx_release,
	reo2host_status,
	reo2host_destination_ring4,
	reo2host_destination_ring3,
	reo2host_destination_ring2,
	reo2host_destination_ring1,
	rxdma2host_monitor_destination_mac3,
	rxdma2host_monitor_destination_mac2,
	rxdma2host_monitor_destination_mac1,
	ppdu_end_interrupts_mac3,
	ppdu_end_interrupts_mac2,
	ppdu_end_interrupts_mac1,
	rxdma2host_monitor_status_ring_mac3,
	rxdma2host_monitor_status_ring_mac2,
	rxdma2host_monitor_status_ring_mac1,
	host2rxdma_host_buf_ring_mac3,
	host2rxdma_host_buf_ring_mac2,
	host2rxdma_host_buf_ring_mac1,
	rxdma2host_destination_ring_mac3,
	rxdma2host_destination_ring_mac2,
	rxdma2host_destination_ring_mac1,
	host2tcl_input_ring4,
	host2tcl_input_ring3,
	host2tcl_input_ring2,
	host2tcl_input_ring1,
	wbm2host_tx_completions_ring3,
	wbm2host_tx_completions_ring2,
	wbm2host_tx_completions_ring1,
	tcl2host_status_ring,
};

static void ath11k_ahb_kill_tasklets(struct ath11k_base *ab)
{
	int i;

	for (i = 0; i < CE_COUNT; i++) {
		struct ath11k_ce_pipe *ce_pipe = &ab->ce.ce_pipe[i];

		if (ath11k_ce_get_attr_flags(i) & CE_ATTR_DIS_INTR)
			continue;

		tasklet_kill(&ce_pipe->intr_tq);
	}
}

static void ath11k_ahb_ext_grp_disable(struct ath11k_ext_irq_grp *irq_grp)
{
	int i;

	for (i = 0; i < irq_grp->num_irq; i++)
		disable_irq_nosync(irq_grp->ab->irq_num[irq_grp->irqs[i]]);
}

static void __ath11k_ahb_ext_irq_disable(struct ath11k_base *ab)
{
	struct sk_buff *skb;
	int i;

	for (i = 0; i < ATH11K_EXT_IRQ_GRP_NUM_MAX; i++) {
		struct ath11k_ext_irq_grp *irq_grp = &ab->ext_irq_grp[i];

		ath11k_ahb_ext_grp_disable(irq_grp);

		napi_synchronize(&irq_grp->napi);
		napi_disable(&irq_grp->napi);

		while ((skb = __skb_dequeue(&irq_grp->pending_q)))
			dev_kfree_skb_any(skb);
	}
}

static void ath11k_ahb_ext_grp_enable(struct ath11k_ext_irq_grp *irq_grp)
{
	int i;

	for (i = 0; i < irq_grp->num_irq; i++)
		enable_irq(irq_grp->ab->irq_num[irq_grp->irqs[i]]);
}

static void ath11k_ahb_setbit32(struct ath11k_base *ab, u8 bit, u32 offset)
{
	u32 val;

	val = ath11k_ahb_read32(ab, offset);
	ath11k_ahb_write32(ab, offset, val | BIT(bit));
}

static void ath11k_ahb_clearbit32(struct ath11k_base *ab, u8 bit, u32 offset)
{
	u32 val;

	val = ath11k_ahb_read32(ab, offset);
	ath11k_ahb_write32(ab, offset, val & ~BIT(bit));
}

static void ath11k_ahb_ce_irq_enable(struct ath11k_base *ab, u16 ce_id)
{
	const struct ce_pipe_config *ce_config;

	ce_config = &target_ce_config_wlan[ce_id];
	if (__le32_to_cpu(ce_config->pipedir) & PIPEDIR_OUT)
		ath11k_ahb_setbit32(ab, ce_id, CE_HOST_IE_ADDRESS);

	if (__le32_to_cpu(ce_config->pipedir) & PIPEDIR_IN) {
		ath11k_ahb_setbit32(ab, ce_id, CE_HOST_IE_2_ADDRESS);
		ath11k_ahb_setbit32(ab, ce_id + CE_HOST_IE_3_SHIFT,
				    CE_HOST_IE_3_ADDRESS);
	}
}

static void ath11k_ahb_ce_irq_disable(struct ath11k_base *ab, u16 ce_id)
{
	const struct ce_pipe_config *ce_config;

	ce_config = &target_ce_config_wlan[ce_id];
	if (__le32_to_cpu(ce_config->pipedir) & PIPEDIR_OUT)
		ath11k_ahb_clearbit32(ab, ce_id, CE_HOST_IE_ADDRESS);

	if (__le32_to_cpu(ce_config->pipedir) & PIPEDIR_IN) {
		ath11k_ahb_clearbit32(ab, ce_id, CE_HOST_IE_2_ADDRESS);
		ath11k_ahb_clearbit32(ab, ce_id + CE_HOST_IE_3_SHIFT,
				      CE_HOST_IE_3_ADDRESS);
	}
}

static void ath11k_ahb_sync_ce_irqs(struct ath11k_base *ab)
{
	int i;
	int irq_idx;

	for (i = 0; i < CE_COUNT; i++) {
		if (ath11k_ce_get_attr_flags(i) & CE_ATTR_DIS_INTR)
			continue;

		irq_idx = ATH11K_IRQ_CE0_OFFSET + i;
		synchronize_irq(ab->irq_num[irq_idx]);
	}
}

static void ath11k_ahb_sync_ext_irqs(struct ath11k_base *ab)
{
	int i, j;
	int irq_idx;

	for (i = 0; i < ATH11K_EXT_IRQ_GRP_NUM_MAX; i++) {
		struct ath11k_ext_irq_grp *irq_grp = &ab->ext_irq_grp[i];

		for (j = 0; j < irq_grp->num_irq; j++) {
			irq_idx = irq_grp->irqs[j];
			synchronize_irq(ab->irq_num[irq_idx]);
		}
	}
}

static void ath11k_ahb_ce_irqs_enable(struct ath11k_base *ab)
{
	int i;

	for (i = 0; i < CE_COUNT; i++) {
		if (ath11k_ce_get_attr_flags(i) & CE_ATTR_DIS_INTR)
			continue;
		ath11k_ahb_ce_irq_enable(ab, i);
	}
}

static void ath11k_ahb_ce_irqs_disable(struct ath11k_base *ab)
{
	int i;

	for (i = 0; i < CE_COUNT; i++) {
		if (ath11k_ce_get_attr_flags(i) & CE_ATTR_DIS_INTR)
			continue;
		ath11k_ahb_ce_irq_disable(ab, i);
	}
}

int ath11k_ahb_start(struct ath11k_base *ab)
{
	ath11k_ahb_ce_irqs_enable(ab);
	ath11k_ce_rx_post_buf(ab);

	return 0;
}

void ath11k_ahb_ext_irq_enable(struct ath11k_base *ab)
{
	int i;

	for (i = 0; i < ATH11K_EXT_IRQ_GRP_NUM_MAX; i++) {
		struct ath11k_ext_irq_grp *irq_grp = &ab->ext_irq_grp[i];

		napi_enable(&irq_grp->napi);
		ath11k_ahb_ext_grp_enable(irq_grp);
	}
}

void ath11k_ahb_ext_irq_disable(struct ath11k_base *ab)
{
	__ath11k_ahb_ext_irq_disable(ab);
	ath11k_ahb_sync_ext_irqs(ab);
}

void ath11k_ahb_stop(struct ath11k_base *ab)
{
	if (!test_bit(ATH11K_FLAG_CRASH_FLUSH, &ab->dev_flags))
		ath11k_ahb_ce_irqs_disable(ab);
	ath11k_ahb_sync_ce_irqs(ab);
	ath11k_ahb_kill_tasklets(ab);
	del_timer_sync(&ab->rx_replenish_retry);
	ath11k_ce_cleanup_pipes(ab);
}

int ath11k_ahb_power_up(struct ath11k_base *ab)
{
	int ret;

	ret = rproc_boot(ab->tgt_rproc);
	if (ret)
		ath11k_err(ab, "failed to boot the remote processor Q6\n");

	return ret;
}

void ath11k_ahb_power_down(struct ath11k_base *ab)
{
	rproc_shutdown(ab->tgt_rproc);
}

static void ath11k_ahb_init_qmi_ce_config(struct ath11k_base *ab)
{
	struct ath11k_qmi_ce_cfg *cfg = &ab->qmi.ce_cfg;

	cfg->tgt_ce_len = ARRAY_SIZE(target_ce_config_wlan) - 1;
	cfg->tgt_ce = target_ce_config_wlan;
	cfg->svc_to_ce_map_len = ARRAY_SIZE(target_service_to_ce_map_wlan);
	cfg->svc_to_ce_map = target_service_to_ce_map_wlan;
}

static void ath11k_ahb_free_ext_irq(struct ath11k_base *ab)
{
	int i, j;

	for (i = 0; i < ATH11K_EXT_IRQ_GRP_NUM_MAX; i++) {
		struct ath11k_ext_irq_grp *irq_grp = &ab->ext_irq_grp[i];

		for (j = 0; j < irq_grp->num_irq; j++)
			free_irq(ab->irq_num[irq_grp->irqs[j]], irq_grp);
	}
}

static void ath11k_ahb_free_irq(struct ath11k_base *ab)
{
	int irq_idx;
	int i;

	for (i = 0; i < CE_COUNT; i++) {
		if (ath11k_ce_get_attr_flags(i) & CE_ATTR_DIS_INTR)
			continue;
		irq_idx = ATH11K_IRQ_CE0_OFFSET + i;
		free_irq(ab->irq_num[irq_idx], &ab->ce.ce_pipe[i]);
	}

	ath11k_ahb_free_ext_irq(ab);
}

static void ath11k_ahb_ce_tasklet(unsigned long data)
{
	struct ath11k_ce_pipe *ce_pipe = (struct ath11k_ce_pipe *)data;

	ath11k_ce_per_engine_service(ce_pipe->ab, ce_pipe->pipe_num);

	ath11k_ahb_ce_irq_enable(ce_pipe->ab, ce_pipe->pipe_num);
}

static irqreturn_t ath11k_ahb_ce_interrupt_handler(int irq, void *arg)
{
	struct ath11k_ce_pipe *ce_pipe = arg;

	ath11k_ahb_ce_irq_disable(ce_pipe->ab, ce_pipe->pipe_num);

	tasklet_schedule(&ce_pipe->intr_tq);

	return IRQ_HANDLED;
}

static int ath11k_ahb_ext_grp_napi_poll(struct napi_struct *napi, int budget)
{
	struct ath11k_ext_irq_grp *irq_grp = container_of(napi,
						struct ath11k_ext_irq_grp,
						napi);
	struct ath11k_base *ab = irq_grp->ab;
	int work_done;

	work_done = ath11k_dp_service_srng(ab, irq_grp, budget);
	if (work_done < budget) {
		napi_complete_done(napi, work_done);
		ath11k_ahb_ext_grp_enable(irq_grp);
	}

	if (work_done > budget)
		work_done = budget;

	return work_done;
}

static irqreturn_t ath11k_ahb_ext_interrupt_handler(int irq, void *arg)
{
	struct ath11k_ext_irq_grp *irq_grp = arg;

	ath11k_ahb_ext_grp_disable(irq_grp);

	napi_schedule(&irq_grp->napi);

	return IRQ_HANDLED;
}

static int ath11k_ahb_ext_irq_config(struct ath11k_base *ab)
{
	int i, j;
	int irq;
	int ret;

	for (i = 0; i < ATH11K_EXT_IRQ_GRP_NUM_MAX; i++) {
		struct ath11k_ext_irq_grp *irq_grp = &ab->ext_irq_grp[i];
		u32 num_irq = 0;

		irq_grp->ab = ab;
		irq_grp->grp_id = i;
		init_dummy_netdev(&irq_grp->napi_ndev);
		netif_napi_add(&irq_grp->napi_ndev, &irq_grp->napi,
			       ath11k_ahb_ext_grp_napi_poll, NAPI_POLL_WEIGHT);
		__skb_queue_head_init(&irq_grp->pending_q);

		for (j = 0; j < ATH11K_EXT_IRQ_NUM_MAX; j++) {
			if (ath11k_tx_ring_mask[i] & BIT(j)) {
				irq_grp->irqs[num_irq++] =
					wbm2host_tx_completions_ring1 - j;
			}

			if (ath11k_rx_ring_mask[i] & BIT(j)) {
				irq_grp->irqs[num_irq++] =
					reo2host_destination_ring1 - j;
			}

			if (ath11k_rx_err_ring_mask[i] & BIT(j))
				irq_grp->irqs[num_irq++] = reo2host_exception;

			if (ath11k_rx_wbm_rel_ring_mask[i] & BIT(j))
				irq_grp->irqs[num_irq++] = wbm2host_rx_release;

			if (ath11k_reo_status_ring_mask[i] & BIT(j))
				irq_grp->irqs[num_irq++] = reo2host_status;

			if (j < MAX_RADIOS) {
				if (ath11k_rxdma2host_ring_mask[i] & BIT(j)) {
					irq_grp->irqs[num_irq++] =
						rxdma2host_destination_ring_mac1
						- ath11k_core_get_hw_mac_id(ab, j);
				}

				if (ath11k_host2rxdma_ring_mask[i] & BIT(j)) {
					irq_grp->irqs[num_irq++] =
						host2rxdma_host_buf_ring_mac1
						- ath11k_core_get_hw_mac_id(ab, j);
				}

				if (rx_mon_status_ring_mask[i] & BIT(j)) {
					irq_grp->irqs[num_irq++] =
						ppdu_end_interrupts_mac1 -
						ath11k_core_get_hw_mac_id(ab, j);
					irq_grp->irqs[num_irq++] =
						rxdma2host_monitor_status_ring_mac1 -
						ath11k_core_get_hw_mac_id(ab, j);
				}
			}
		}
		irq_grp->num_irq = num_irq;

		for (j = 0; j < irq_grp->num_irq; j++) {
			int irq_idx = irq_grp->irqs[j];

			irq = platform_get_irq_byname(ab->pdev,
						      irq_name[irq_idx]);
			ab->irq_num[irq_idx] = irq;
			irq_set_status_flags(irq, IRQ_NOAUTOEN);
			ret = request_irq(irq, ath11k_ahb_ext_interrupt_handler,
					  IRQF_TRIGGER_RISING,
					  irq_name[irq_idx], irq_grp);
			if (ret) {
				ath11k_err(ab, "failed request_irq for %d\n",
					   irq);
			}
		}
	}

	return 0;
}

static int ath11k_ahb_config_irq(struct ath11k_base *ab)
{
	int irq, irq_idx, i;
	int ret;

	/* Configure CE irqs */
	for (i = 0; i < CE_COUNT; i++) {
		struct ath11k_ce_pipe *ce_pipe = &ab->ce.ce_pipe[i];

		if (ath11k_ce_get_attr_flags(i) & CE_ATTR_DIS_INTR)
			continue;

		irq_idx = ATH11K_IRQ_CE0_OFFSET + i;

		tasklet_init(&ce_pipe->intr_tq, ath11k_ahb_ce_tasklet,
			     (unsigned long)ce_pipe);
		irq = platform_get_irq_byname(ab->pdev, irq_name[irq_idx]);
		ret = request_irq(irq, ath11k_ahb_ce_interrupt_handler,
				  IRQF_TRIGGER_RISING, irq_name[irq_idx],
				  ce_pipe);
		if (ret)
			return ret;

		ab->irq_num[irq_idx] = irq;
	}

	/* Configure external interrupts */
	ret = ath11k_ahb_ext_irq_config(ab);

	return ret;
}

int ath11k_ahb_map_service_to_pipe(struct ath11k_base *ab, u16 service_id,
				   u8 *ul_pipe, u8 *dl_pipe)
{
	const struct service_to_pipe *entry;
	bool ul_set = false, dl_set = false;
	int i;

	for (i = 0; i < ARRAY_SIZE(target_service_to_ce_map_wlan); i++) {
		entry = &target_service_to_ce_map_wlan[i];

		if (__le32_to_cpu(entry->service_id) != service_id)
			continue;

		switch (__le32_to_cpu(entry->pipedir)) {
		case PIPEDIR_NONE:
			break;
		case PIPEDIR_IN:
			WARN_ON(dl_set);
			*dl_pipe = __le32_to_cpu(entry->pipenum);
			dl_set = true;
			break;
		case PIPEDIR_OUT:
			WARN_ON(ul_set);
			*ul_pipe = __le32_to_cpu(entry->pipenum);
			ul_set = true;
			break;
		case PIPEDIR_INOUT:
			WARN_ON(dl_set);
			WARN_ON(ul_set);
			*dl_pipe = __le32_to_cpu(entry->pipenum);
			*ul_pipe = __le32_to_cpu(entry->pipenum);
			dl_set = true;
			ul_set = true;
			break;
		}
	}

	if (WARN_ON(!ul_set || !dl_set))
		return -ENOENT;

	return 0;
}

static int ath11k_ahb_probe(struct platform_device *pdev)
{
	struct ath11k_base *ab;
	const struct of_device_id *of_id;
	struct resource *mem_res;
	void __iomem *mem;
	int ret;

	of_id = of_match_device(ath11k_ahb_of_match, &pdev->dev);
	if (!of_id) {
		dev_err(&pdev->dev, "failed to find matching device tree id\n");
		return -EINVAL;
	}

	mem_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!mem_res) {
		dev_err(&pdev->dev, "failed to get IO memory resource\n");
		return -ENXIO;
	}

	mem = devm_ioremap_resource(&pdev->dev, mem_res);
	if (IS_ERR(mem)) {
		dev_err(&pdev->dev, "ioremap error\n");
		return PTR_ERR(mem);
	}

	ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
	if (ret) {
		dev_err(&pdev->dev, "failed to set 32-bit consistent dma\n");
		return ret;
	}

	ab = ath11k_core_alloc(&pdev->dev);
	if (!ab) {
		dev_err(&pdev->dev, "failed to allocate ath11k base\n");
		return -ENOMEM;
	}

	ab->pdev = pdev;
	ab->hw_rev = (enum ath11k_hw_rev)of_id->data;
	ab->mem = mem;
	ab->mem_len = resource_size(mem_res);
	platform_set_drvdata(pdev, ab);

	ret = ath11k_hal_srng_init(ab);
	if (ret)
		goto err_core_free;

	ret = ath11k_ce_alloc_pipes(ab);
	if (ret) {
		ath11k_err(ab, "failed to allocate ce pipes: %d\n", ret);
		goto err_hal_srng_deinit;
	}

	ath11k_ahb_init_qmi_ce_config(ab);

	ret = ath11k_ahb_config_irq(ab);
	if (ret) {
		ath11k_err(ab, "failed to configure irq: %d\n", ret);
		goto err_ce_free;
	}

	ret = ath11k_core_init(ab);
	if (ret) {
		ath11k_err(ab, "failed to init core: %d\n", ret);
		goto err_ce_free;
	}

	return 0;

err_ce_free:
	ath11k_ce_free_pipes(ab);

err_hal_srng_deinit:
	ath11k_hal_srng_deinit(ab);

err_core_free:
	ath11k_core_free(ab);
	platform_set_drvdata(pdev, NULL);

	return ret;
}

static int ath11k_ahb_remove(struct platform_device *pdev)
{
	struct ath11k_base *ab = platform_get_drvdata(pdev);

	reinit_completion(&ab->driver_recovery);

	if (test_bit(ATH11K_FLAG_RECOVERY, &ab->dev_flags))
		wait_for_completion_timeout(&ab->driver_recovery,
					    ATH11K_AHB_RECOVERY_TIMEOUT);

	set_bit(ATH11K_FLAG_UNREGISTERING, &ab->dev_flags);
	cancel_work_sync(&ab->restart_work);

	ath11k_core_deinit(ab);
	ath11k_ahb_free_irq(ab);

	ath11k_hal_srng_deinit(ab);
	ath11k_ce_free_pipes(ab);
	ath11k_core_free(ab);
	platform_set_drvdata(pdev, NULL);

	return 0;
}

static struct platform_driver ath11k_ahb_driver = {
	.driver         = {
		.name   = "ath11k",
		.of_match_table = ath11k_ahb_of_match,
	},
	.probe  = ath11k_ahb_probe,
	.remove = ath11k_ahb_remove,
};

int ath11k_ahb_init(void)
{
	return platform_driver_register(&ath11k_ahb_driver);
}

void ath11k_ahb_exit(void)
{
	platform_driver_unregister(&ath11k_ahb_driver);
}
