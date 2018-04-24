/* SPDX-License-Identifier: GPL-2.0+ */
/* Copyright (c) 2016-2017 Hisilicon Limited. */

#ifndef __HCLGEVF_MAIN_H
#define __HCLGEVF_MAIN_H
#include <linux/fs.h>
#include <linux/types.h>
#include "hclge_mbx.h"
#include "hclgevf_cmd.h"
#include "hnae3.h"

#define HCLGEVF_MOD_VERSION "v1.0"
#define HCLGEVF_DRIVER_NAME "hclgevf"

#define HCLGEVF_ROCEE_VECTOR_NUM	0
#define HCLGEVF_MISC_VECTOR_NUM		0

#define HCLGEVF_INVALID_VPORT		0xffff

/* This number in actual depends upon the total number of VFs
 * created by physical function. But the maximum number of
 * possible vector-per-VF is {VFn(1-32), VECTn(32 + 1)}.
 */
#define HCLGEVF_MAX_VF_VECTOR_NUM	(32 + 1)

#define HCLGEVF_VECTOR_REG_BASE		0x20000
#define HCLGEVF_MISC_VECTOR_REG_BASE	0x20400
#define HCLGEVF_VECTOR_REG_OFFSET	0x4
#define HCLGEVF_VECTOR_VF_OFFSET		0x100000

/* Vector0 interrupt CMDQ event source register(RW) */
#define HCLGEVF_VECTOR0_CMDQ_SRC_REG	0x27100
/* CMDQ register bits for RX event(=MBX event) */
#define HCLGEVF_VECTOR0_RX_CMDQ_INT_B	1

#define HCLGEVF_TQP_RESET_TRY_TIMES	10
/* Reset related Registers */
#define HCLGEVF_FUN_RST_ING		0x20C00
#define HCLGEVF_FUN_RST_ING_B		0

#define HCLGEVF_RSS_IND_TBL_SIZE		512
#define HCLGEVF_RSS_SET_BITMAP_MSK	0xffff
#define HCLGEVF_RSS_KEY_SIZE		40
#define HCLGEVF_RSS_HASH_ALGO_TOEPLITZ	0
#define HCLGEVF_RSS_HASH_ALGO_SIMPLE	1
#define HCLGEVF_RSS_HASH_ALGO_SYMMETRIC	2
#define HCLGEVF_RSS_HASH_ALGO_MASK	0xf
#define HCLGEVF_RSS_CFG_TBL_NUM \
	(HCLGEVF_RSS_IND_TBL_SIZE / HCLGEVF_RSS_CFG_TBL_SIZE)

/* states of hclgevf device & tasks */
enum hclgevf_states {
	/* device states */
	HCLGEVF_STATE_DOWN,
	HCLGEVF_STATE_DISABLED,
	/* task states */
	HCLGEVF_STATE_SERVICE_SCHED,
	HCLGEVF_STATE_RST_SERVICE_SCHED,
	HCLGEVF_STATE_RST_HANDLING,
	HCLGEVF_STATE_MBX_SERVICE_SCHED,
	HCLGEVF_STATE_MBX_HANDLING,
};

#define HCLGEVF_MPF_ENBALE 1

struct hclgevf_mac {
	u8 mac_addr[ETH_ALEN];
	int link;
	u8 duplex;
	u32 speed;
};

struct hclgevf_hw {
	void __iomem *io_base;
	int num_vec;
	struct hclgevf_cmq cmq;
	struct hclgevf_mac mac;
	void *hdev; /* hchgevf device it is part of */
};

/* TQP stats */
struct hlcgevf_tqp_stats {
	/* query_tqp_tx_queue_statistics ,opcode id:  0x0B03 */
	u64 rcb_tx_ring_pktnum_rcd; /* 32bit */
	/* query_tqp_rx_queue_statistics ,opcode id:  0x0B13 */
	u64 rcb_rx_ring_pktnum_rcd; /* 32bit */
};

struct hclgevf_tqp {
	struct device *dev;	/* device for DMA mapping */
	struct hnae3_queue q;
	struct hlcgevf_tqp_stats tqp_stats;
	u16 index;		/* global index in a NIC controller */

	bool alloced;
};

struct hclgevf_cfg {
	u8 vmdq_vport_num;
	u8 tc_num;
	u16 tqp_desc_num;
	u16 rx_buf_len;
	u8 phy_addr;
	u8 media_type;
	u8 mac_addr[ETH_ALEN];
	u32 numa_node_map;
};

struct hclgevf_rss_cfg {
	u8  rss_hash_key[HCLGEVF_RSS_KEY_SIZE]; /* user configured hash keys */
	u32 hash_algo;
	u32 rss_size;
	u8 hw_tc_map;
	u8  rss_indirection_tbl[HCLGEVF_RSS_IND_TBL_SIZE]; /* shadow table */
};

struct hclgevf_misc_vector {
	u8 __iomem *addr;
	int vector_irq;
};

struct hclgevf_dev {
	struct pci_dev *pdev;
	struct hnae3_ae_dev *ae_dev;
	struct hclgevf_hw hw;
	struct hclgevf_misc_vector misc_vector;
	struct hclgevf_rss_cfg rss_cfg;
	unsigned long state;

#define HCLGEVF_RESET_REQUESTED		0
#define HCLGEVF_RESET_PENDING		1
	unsigned long reset_state;	/* requested, pending */
	u32 reset_attempts;

	u32 fw_version;
	u16 num_tqps;		/* num task queue pairs of this PF */

	u16 alloc_rss_size;	/* allocated RSS task queue */
	u16 rss_size_max;	/* HW defined max RSS task queue */

	u16 num_alloc_vport;	/* num vports this driver supports */
	u32 numa_node_mask;
	u16 rx_buf_len;
	u16 num_desc;
	u8 hw_tc_map;

	u16 num_msi;
	u16 num_msi_left;
	u16 num_msi_used;
	u32 base_msi_vector;
	u16 *vector_status;
	int *vector_irq;

	bool accept_mta_mc; /* whether to accept mta filter multicast */
	bool mbx_event_pending;
	struct hclgevf_mbx_resp_status mbx_resp; /* mailbox response */
	struct hclgevf_mbx_arq_ring arq; /* mailbox async rx queue */

	struct timer_list service_timer;
	struct work_struct service_task;
	struct work_struct rst_service_task;
	struct work_struct mbx_service_task;

	struct hclgevf_tqp *htqp;

	struct hnae3_handle nic;
	struct hnae3_handle roce;

	struct hnae3_client *nic_client;
	struct hnae3_client *roce_client;
	u32 flag;
};

static inline bool hclgevf_dev_ongoing_reset(struct hclgevf_dev *hdev)
{
	return (hdev &&
		(test_bit(HCLGEVF_STATE_RST_HANDLING, &hdev->state)) &&
		(hdev->nic.reset_level == HNAE3_VF_RESET));
}

static inline bool hclgevf_dev_ongoing_full_reset(struct hclgevf_dev *hdev)
{
	return (hdev &&
		(test_bit(HCLGEVF_STATE_RST_HANDLING, &hdev->state)) &&
		(hdev->nic.reset_level == HNAE3_VF_FULL_RESET));
}

int hclgevf_send_mbx_msg(struct hclgevf_dev *hdev, u16 code, u16 subcode,
			 const u8 *msg_data, u8 msg_len, bool need_resp,
			 u8 *resp_data, u16 resp_len);
void hclgevf_mbx_handler(struct hclgevf_dev *hdev);
void hclgevf_mbx_async_handler(struct hclgevf_dev *hdev);

void hclgevf_update_link_status(struct hclgevf_dev *hdev, int link_state);
void hclgevf_update_speed_duplex(struct hclgevf_dev *hdev, u32 speed,
				 u8 duplex);
void hclgevf_reset_task_schedule(struct hclgevf_dev *hdev);
void hclgevf_mbx_task_schedule(struct hclgevf_dev *hdev);
#endif
