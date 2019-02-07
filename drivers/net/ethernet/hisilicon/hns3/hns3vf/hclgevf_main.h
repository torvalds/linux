/* SPDX-License-Identifier: GPL-2.0+ */
/* Copyright (c) 2016-2017 Hisilicon Limited. */

#ifndef __HCLGEVF_MAIN_H
#define __HCLGEVF_MAIN_H
#include <linux/fs.h>
#include <linux/types.h>
#include "hclge_mbx.h"
#include "hclgevf_cmd.h"
#include "hnae3.h"

#define HCLGEVF_MOD_VERSION "1.0"
#define HCLGEVF_DRIVER_NAME "hclgevf"

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

/* bar registers for cmdq */
#define HCLGEVF_CMDQ_TX_ADDR_L_REG		0x27000
#define HCLGEVF_CMDQ_TX_ADDR_H_REG		0x27004
#define HCLGEVF_CMDQ_TX_DEPTH_REG		0x27008
#define HCLGEVF_CMDQ_TX_TAIL_REG		0x27010
#define HCLGEVF_CMDQ_TX_HEAD_REG		0x27014
#define HCLGEVF_CMDQ_RX_ADDR_L_REG		0x27018
#define HCLGEVF_CMDQ_RX_ADDR_H_REG		0x2701C
#define HCLGEVF_CMDQ_RX_DEPTH_REG		0x27020
#define HCLGEVF_CMDQ_RX_TAIL_REG		0x27024
#define HCLGEVF_CMDQ_RX_HEAD_REG		0x27028
#define HCLGEVF_CMDQ_INTR_SRC_REG		0x27100
#define HCLGEVF_CMDQ_INTR_STS_REG		0x27104
#define HCLGEVF_CMDQ_INTR_EN_REG		0x27108
#define HCLGEVF_CMDQ_INTR_GEN_REG		0x2710C

/* bar registers for common func */
#define HCLGEVF_GRO_EN_REG			0x28000

/* bar registers for rcb */
#define HCLGEVF_RING_RX_ADDR_L_REG		0x80000
#define HCLGEVF_RING_RX_ADDR_H_REG		0x80004
#define HCLGEVF_RING_RX_BD_NUM_REG		0x80008
#define HCLGEVF_RING_RX_BD_LENGTH_REG		0x8000C
#define HCLGEVF_RING_RX_MERGE_EN_REG		0x80014
#define HCLGEVF_RING_RX_TAIL_REG		0x80018
#define HCLGEVF_RING_RX_HEAD_REG		0x8001C
#define HCLGEVF_RING_RX_FBD_NUM_REG		0x80020
#define HCLGEVF_RING_RX_OFFSET_REG		0x80024
#define HCLGEVF_RING_RX_FBD_OFFSET_REG		0x80028
#define HCLGEVF_RING_RX_STASH_REG		0x80030
#define HCLGEVF_RING_RX_BD_ERR_REG		0x80034
#define HCLGEVF_RING_TX_ADDR_L_REG		0x80040
#define HCLGEVF_RING_TX_ADDR_H_REG		0x80044
#define HCLGEVF_RING_TX_BD_NUM_REG		0x80048
#define HCLGEVF_RING_TX_PRIORITY_REG		0x8004C
#define HCLGEVF_RING_TX_TC_REG			0x80050
#define HCLGEVF_RING_TX_MERGE_EN_REG		0x80054
#define HCLGEVF_RING_TX_TAIL_REG		0x80058
#define HCLGEVF_RING_TX_HEAD_REG		0x8005C
#define HCLGEVF_RING_TX_FBD_NUM_REG		0x80060
#define HCLGEVF_RING_TX_OFFSET_REG		0x80064
#define HCLGEVF_RING_TX_EBD_NUM_REG		0x80068
#define HCLGEVF_RING_TX_EBD_OFFSET_REG		0x80070
#define HCLGEVF_RING_TX_BD_ERR_REG		0x80074
#define HCLGEVF_RING_EN_REG			0x80090

/* bar registers for tqp interrupt */
#define HCLGEVF_TQP_INTR_CTRL_REG		0x20000
#define HCLGEVF_TQP_INTR_GL0_REG		0x20100
#define HCLGEVF_TQP_INTR_GL1_REG		0x20200
#define HCLGEVF_TQP_INTR_GL2_REG		0x20300
#define HCLGEVF_TQP_INTR_RL_REG			0x20900

/* Vector0 interrupt CMDQ event source register(RW) */
#define HCLGEVF_VECTOR0_CMDQ_SRC_REG	0x27100
/* CMDQ register bits for RX event(=MBX event) */
#define HCLGEVF_VECTOR0_RX_CMDQ_INT_B	1
/* RST register bits for RESET event */
#define HCLGEVF_VECTOR0_RST_INT_B	2

#define HCLGEVF_TQP_RESET_TRY_TIMES	10
/* Reset related Registers */
#define HCLGEVF_RST_ING			0x20C00
#define HCLGEVF_FUN_RST_ING_BIT		BIT(0)
#define HCLGEVF_GLOBAL_RST_ING_BIT	BIT(5)
#define HCLGEVF_CORE_RST_ING_BIT	BIT(6)
#define HCLGEVF_IMP_RST_ING_BIT		BIT(7)
#define HCLGEVF_RST_ING_BITS \
	(HCLGEVF_FUN_RST_ING_BIT | HCLGEVF_GLOBAL_RST_ING_BIT | \
	 HCLGEVF_CORE_RST_ING_BIT | HCLGEVF_IMP_RST_ING_BIT)

#define HCLGEVF_RSS_IND_TBL_SIZE		512
#define HCLGEVF_RSS_SET_BITMAP_MSK	0xffff
#define HCLGEVF_RSS_KEY_SIZE		40
#define HCLGEVF_RSS_HASH_ALGO_TOEPLITZ	0
#define HCLGEVF_RSS_HASH_ALGO_SIMPLE	1
#define HCLGEVF_RSS_HASH_ALGO_SYMMETRIC	2
#define HCLGEVF_RSS_HASH_ALGO_MASK	0xf
#define HCLGEVF_RSS_CFG_TBL_NUM \
	(HCLGEVF_RSS_IND_TBL_SIZE / HCLGEVF_RSS_CFG_TBL_SIZE)
#define HCLGEVF_RSS_INPUT_TUPLE_OTHER	GENMASK(3, 0)
#define HCLGEVF_RSS_INPUT_TUPLE_SCTP	GENMASK(4, 0)
#define HCLGEVF_D_PORT_BIT		BIT(0)
#define HCLGEVF_S_PORT_BIT		BIT(1)
#define HCLGEVF_D_IP_BIT		BIT(2)
#define HCLGEVF_S_IP_BIT		BIT(3)
#define HCLGEVF_V_TAG_BIT		BIT(4)

enum hclgevf_evt_cause {
	HCLGEVF_VECTOR0_EVENT_RST,
	HCLGEVF_VECTOR0_EVENT_MBX,
	HCLGEVF_VECTOR0_EVENT_OTHER,
};

/* states of hclgevf device & tasks */
enum hclgevf_states {
	/* device states */
	HCLGEVF_STATE_DOWN,
	HCLGEVF_STATE_DISABLED,
	HCLGEVF_STATE_IRQ_INITED,
	/* task states */
	HCLGEVF_STATE_SERVICE_SCHED,
	HCLGEVF_STATE_RST_SERVICE_SCHED,
	HCLGEVF_STATE_RST_HANDLING,
	HCLGEVF_STATE_MBX_SERVICE_SCHED,
	HCLGEVF_STATE_MBX_HANDLING,
	HCLGEVF_STATE_CMD_DISABLE,
};

#define HCLGEVF_MPF_ENBALE 1

struct hclgevf_mac {
	u8 media_type;
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

struct hclgevf_rss_tuple_cfg {
	u8 ipv4_tcp_en;
	u8 ipv4_udp_en;
	u8 ipv4_sctp_en;
	u8 ipv4_fragment_en;
	u8 ipv6_tcp_en;
	u8 ipv6_udp_en;
	u8 ipv6_sctp_en;
	u8 ipv6_fragment_en;
};

struct hclgevf_rss_cfg {
	u8  rss_hash_key[HCLGEVF_RSS_KEY_SIZE]; /* user configured hash keys */
	u32 hash_algo;
	u32 rss_size;
	u8 hw_tc_map;
	u8  rss_indirection_tbl[HCLGEVF_RSS_IND_TBL_SIZE]; /* shadow table */
	struct hclgevf_rss_tuple_cfg rss_tuple_sets;
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
	unsigned long flr_state;
	unsigned long default_reset_request;
	unsigned long last_reset_time;
	enum hnae3_reset_type reset_level;
	unsigned long reset_pending;
	enum hnae3_reset_type reset_type;

#define HCLGEVF_RESET_REQUESTED		0
#define HCLGEVF_RESET_PENDING		1
	unsigned long reset_state;	/* requested, pending */
	unsigned long reset_count;	/* the number of reset has been done */
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
	u16 num_roce_msix;	/* Num of roce vectors for this VF */
	u16 roce_base_msix_offset;
	int roce_base_vector;
	u32 base_msi_vector;
	u16 *vector_status;
	int *vector_irq;

	bool mbx_event_pending;
	struct hclgevf_mbx_resp_status mbx_resp; /* mailbox response */
	struct hclgevf_mbx_arq_ring arq; /* mailbox async rx queue */

	struct timer_list service_timer;
	struct timer_list keep_alive_timer;
	struct work_struct service_task;
	struct work_struct keep_alive_task;
	struct work_struct rst_service_task;
	struct work_struct mbx_service_task;

	struct hclgevf_tqp *htqp;

	struct hnae3_handle nic;
	struct hnae3_handle roce;

	struct hnae3_client *nic_client;
	struct hnae3_client *roce_client;
	u32 flag;
};

static inline bool hclgevf_is_reset_pending(struct hclgevf_dev *hdev)
{
	return !!hdev->reset_pending;
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
