// SPDX-License-Identifier: GPL-2.0+
/* Copyright (c) 2018-2019 Hisilicon Limited. */

#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/seq_file.h>
#include <linux/string_choices.h>

#include "hnae3.h"
#include "hns3_debugfs.h"
#include "hns3_enet.h"

static struct dentry *hns3_dbgfs_root;

static struct hns3_dbg_dentry_info hns3_dbg_dentry[] = {
	{
		.name = "tm"
	},
	{
		.name = "tx_bd_info"
	},
	{
		.name = "rx_bd_info"
	},
	{
		.name = "mac_list"
	},
	{
		.name = "reg"
	},
	{
		.name = "queue"
	},
	{
		.name = "fd"
	},
	/* keep common at the bottom and add new directory above */
	{
		.name = "common"
	},
};

static int hns3_dbg_bd_file_init(struct hnae3_handle *handle, u32 cmd);
static int hns3_dbg_common_init_t1(struct hnae3_handle *handle, u32 cmd);
static int hns3_dbg_common_init_t2(struct hnae3_handle *handle, u32 cmd);

static struct hns3_dbg_cmd_info hns3_dbg_cmd[] = {
	{
		.name = "tm_nodes",
		.cmd = HNAE3_DBG_CMD_TM_NODES,
		.dentry = HNS3_DBG_DENTRY_TM,
		.init = hns3_dbg_common_init_t2,
	},
	{
		.name = "tm_priority",
		.cmd = HNAE3_DBG_CMD_TM_PRI,
		.dentry = HNS3_DBG_DENTRY_TM,
		.init = hns3_dbg_common_init_t2,
	},
	{
		.name = "tm_qset",
		.cmd = HNAE3_DBG_CMD_TM_QSET,
		.dentry = HNS3_DBG_DENTRY_TM,
		.init = hns3_dbg_common_init_t2,
	},
	{
		.name = "tm_map",
		.cmd = HNAE3_DBG_CMD_TM_MAP,
		.dentry = HNS3_DBG_DENTRY_TM,
		.init = hns3_dbg_common_init_t2,
	},
	{
		.name = "tm_pg",
		.cmd = HNAE3_DBG_CMD_TM_PG,
		.dentry = HNS3_DBG_DENTRY_TM,
		.init = hns3_dbg_common_init_t2,
	},
	{
		.name = "tm_port",
		.cmd = HNAE3_DBG_CMD_TM_PORT,
		.dentry = HNS3_DBG_DENTRY_TM,
		.init = hns3_dbg_common_init_t2,
	},
	{
		.name = "tc_sch_info",
		.cmd = HNAE3_DBG_CMD_TC_SCH_INFO,
		.dentry = HNS3_DBG_DENTRY_TM,
		.init = hns3_dbg_common_init_t2,
	},
	{
		.name = "qos_pause_cfg",
		.cmd = HNAE3_DBG_CMD_QOS_PAUSE_CFG,
		.dentry = HNS3_DBG_DENTRY_TM,
		.init = hns3_dbg_common_init_t2,
	},
	{
		.name = "qos_pri_map",
		.cmd = HNAE3_DBG_CMD_QOS_PRI_MAP,
		.dentry = HNS3_DBG_DENTRY_TM,
		.init = hns3_dbg_common_init_t2,
	},
	{
		.name = "qos_dscp_map",
		.cmd = HNAE3_DBG_CMD_QOS_DSCP_MAP,
		.dentry = HNS3_DBG_DENTRY_TM,
		.init = hns3_dbg_common_init_t2,
	},
	{
		.name = "qos_buf_cfg",
		.cmd = HNAE3_DBG_CMD_QOS_BUF_CFG,
		.dentry = HNS3_DBG_DENTRY_TM,
		.init = hns3_dbg_common_init_t2,
	},
	{
		.name = "dev_info",
		.cmd = HNAE3_DBG_CMD_DEV_INFO,
		.dentry = HNS3_DBG_DENTRY_COMMON,
		.init = hns3_dbg_common_init_t1,
	},
	{
		.name = "tx_bd_queue",
		.cmd = HNAE3_DBG_CMD_TX_BD,
		.dentry = HNS3_DBG_DENTRY_TX_BD,
		.init = hns3_dbg_bd_file_init,
	},
	{
		.name = "rx_bd_queue",
		.cmd = HNAE3_DBG_CMD_RX_BD,
		.dentry = HNS3_DBG_DENTRY_RX_BD,
		.init = hns3_dbg_bd_file_init,
	},
	{
		.name = "uc",
		.cmd = HNAE3_DBG_CMD_MAC_UC,
		.dentry = HNS3_DBG_DENTRY_MAC,
		.init = hns3_dbg_common_init_t2,
	},
	{
		.name = "mc",
		.cmd = HNAE3_DBG_CMD_MAC_MC,
		.dentry = HNS3_DBG_DENTRY_MAC,
		.init = hns3_dbg_common_init_t2,
	},
	{
		.name = "mng_tbl",
		.cmd = HNAE3_DBG_CMD_MNG_TBL,
		.dentry = HNS3_DBG_DENTRY_COMMON,
		.init = hns3_dbg_common_init_t2,
	},
	{
		.name = "loopback",
		.cmd = HNAE3_DBG_CMD_LOOPBACK,
		.dentry = HNS3_DBG_DENTRY_COMMON,
		.init = hns3_dbg_common_init_t2,
	},
	{
		.name = "interrupt_info",
		.cmd = HNAE3_DBG_CMD_INTERRUPT_INFO,
		.dentry = HNS3_DBG_DENTRY_COMMON,
		.init = hns3_dbg_common_init_t2,
	},
	{
		.name = "reset_info",
		.cmd = HNAE3_DBG_CMD_RESET_INFO,
		.dentry = HNS3_DBG_DENTRY_COMMON,
		.init = hns3_dbg_common_init_t2,
	},
	{
		.name = "imp_info",
		.cmd = HNAE3_DBG_CMD_IMP_INFO,
		.dentry = HNS3_DBG_DENTRY_COMMON,
		.init = hns3_dbg_common_init_t2,
	},
	{
		.name = "ncl_config",
		.cmd = HNAE3_DBG_CMD_NCL_CONFIG,
		.dentry = HNS3_DBG_DENTRY_COMMON,
		.init = hns3_dbg_common_init_t2,
	},
	{
		.name = "mac_tnl_status",
		.cmd = HNAE3_DBG_CMD_MAC_TNL_STATUS,
		.dentry = HNS3_DBG_DENTRY_COMMON,
		.init = hns3_dbg_common_init_t2,
	},
	{
		.name = "bios_common",
		.cmd = HNAE3_DBG_CMD_REG_BIOS_COMMON,
		.dentry = HNS3_DBG_DENTRY_REG,
		.init = hns3_dbg_common_init_t2,
	},
	{
		.name = "ssu",
		.cmd = HNAE3_DBG_CMD_REG_SSU,
		.dentry = HNS3_DBG_DENTRY_REG,
		.init = hns3_dbg_common_init_t2,
	},
	{
		.name = "igu_egu",
		.cmd = HNAE3_DBG_CMD_REG_IGU_EGU,
		.dentry = HNS3_DBG_DENTRY_REG,
		.init = hns3_dbg_common_init_t2,
	},
	{
		.name = "rpu",
		.cmd = HNAE3_DBG_CMD_REG_RPU,
		.dentry = HNS3_DBG_DENTRY_REG,
		.init = hns3_dbg_common_init_t2,
	},
	{
		.name = "ncsi",
		.cmd = HNAE3_DBG_CMD_REG_NCSI,
		.dentry = HNS3_DBG_DENTRY_REG,
		.init = hns3_dbg_common_init_t2,
	},
	{
		.name = "rtc",
		.cmd = HNAE3_DBG_CMD_REG_RTC,
		.dentry = HNS3_DBG_DENTRY_REG,
		.init = hns3_dbg_common_init_t2,
	},
	{
		.name = "ppp",
		.cmd = HNAE3_DBG_CMD_REG_PPP,
		.dentry = HNS3_DBG_DENTRY_REG,
		.init = hns3_dbg_common_init_t2,
	},
	{
		.name = "rcb",
		.cmd = HNAE3_DBG_CMD_REG_RCB,
		.dentry = HNS3_DBG_DENTRY_REG,
		.init = hns3_dbg_common_init_t2,
	},
	{
		.name = "tqp",
		.cmd = HNAE3_DBG_CMD_REG_TQP,
		.dentry = HNS3_DBG_DENTRY_REG,
		.init = hns3_dbg_common_init_t2,
	},
	{
		.name = "mac",
		.cmd = HNAE3_DBG_CMD_REG_MAC,
		.dentry = HNS3_DBG_DENTRY_REG,
		.init = hns3_dbg_common_init_t2,
	},
	{
		.name = "dcb",
		.cmd = HNAE3_DBG_CMD_REG_DCB,
		.dentry = HNS3_DBG_DENTRY_REG,
		.init = hns3_dbg_common_init_t2,
	},
	{
		.name = "queue_map",
		.cmd = HNAE3_DBG_CMD_QUEUE_MAP,
		.dentry = HNS3_DBG_DENTRY_QUEUE,
		.init = hns3_dbg_common_init_t1,
	},
	{
		.name = "rx_queue_info",
		.cmd = HNAE3_DBG_CMD_RX_QUEUE_INFO,
		.dentry = HNS3_DBG_DENTRY_QUEUE,
		.init = hns3_dbg_common_init_t1,
	},
	{
		.name = "tx_queue_info",
		.cmd = HNAE3_DBG_CMD_TX_QUEUE_INFO,
		.dentry = HNS3_DBG_DENTRY_QUEUE,
		.init = hns3_dbg_common_init_t1,
	},
	{
		.name = "fd_tcam",
		.cmd = HNAE3_DBG_CMD_FD_TCAM,
		.dentry = HNS3_DBG_DENTRY_FD,
		.init = hns3_dbg_common_init_t2,
	},
	{
		.name = "service_task_info",
		.cmd = HNAE3_DBG_CMD_SERV_INFO,
		.dentry = HNS3_DBG_DENTRY_COMMON,
		.init = hns3_dbg_common_init_t2,
	},
	{
		.name = "vlan_config",
		.cmd = HNAE3_DBG_CMD_VLAN_CONFIG,
		.dentry = HNS3_DBG_DENTRY_COMMON,
		.init = hns3_dbg_common_init_t2,
	},
	{
		.name = "ptp_info",
		.cmd = HNAE3_DBG_CMD_PTP_INFO,
		.dentry = HNS3_DBG_DENTRY_COMMON,
		.init = hns3_dbg_common_init_t2,
	},
	{
		.name = "fd_counter",
		.cmd = HNAE3_DBG_CMD_FD_COUNTER,
		.dentry = HNS3_DBG_DENTRY_FD,
		.init = hns3_dbg_common_init_t2,
	},
	{
		.name = "umv_info",
		.cmd = HNAE3_DBG_CMD_UMV_INFO,
		.dentry = HNS3_DBG_DENTRY_COMMON,
		.init = hns3_dbg_common_init_t2,
	},
	{
		.name = "page_pool_info",
		.cmd = HNAE3_DBG_CMD_PAGE_POOL_INFO,
		.dentry = HNS3_DBG_DENTRY_COMMON,
		.init = hns3_dbg_common_init_t1,
	},
	{
		.name = "coalesce_info",
		.cmd = HNAE3_DBG_CMD_COAL_INFO,
		.dentry = HNS3_DBG_DENTRY_COMMON,
		.init = hns3_dbg_common_init_t1,
	},
};

static struct hns3_dbg_cap_info hns3_dbg_cap[] = {
	{
		.name = "support FD",
		.cap_bit = HNAE3_DEV_SUPPORT_FD_B,
	}, {
		.name = "support GRO",
		.cap_bit = HNAE3_DEV_SUPPORT_GRO_B,
	}, {
		.name = "support FEC",
		.cap_bit = HNAE3_DEV_SUPPORT_FEC_B,
	}, {
		.name = "support UDP GSO",
		.cap_bit = HNAE3_DEV_SUPPORT_UDP_GSO_B,
	}, {
		.name = "support PTP",
		.cap_bit = HNAE3_DEV_SUPPORT_PTP_B,
	}, {
		.name = "support INT QL",
		.cap_bit = HNAE3_DEV_SUPPORT_INT_QL_B,
	}, {
		.name = "support HW TX csum",
		.cap_bit = HNAE3_DEV_SUPPORT_HW_TX_CSUM_B,
	}, {
		.name = "support UDP tunnel csum",
		.cap_bit = HNAE3_DEV_SUPPORT_UDP_TUNNEL_CSUM_B,
	}, {
		.name = "support TX push",
		.cap_bit = HNAE3_DEV_SUPPORT_TX_PUSH_B,
	}, {
		.name = "support imp-controlled PHY",
		.cap_bit = HNAE3_DEV_SUPPORT_PHY_IMP_B,
	}, {
		.name = "support imp-controlled RAS",
		.cap_bit = HNAE3_DEV_SUPPORT_RAS_IMP_B,
	}, {
		.name = "support rxd advanced layout",
		.cap_bit = HNAE3_DEV_SUPPORT_RXD_ADV_LAYOUT_B,
	}, {
		.name = "support port vlan bypass",
		.cap_bit = HNAE3_DEV_SUPPORT_PORT_VLAN_BYPASS_B,
	}, {
		.name = "support modify vlan filter state",
		.cap_bit = HNAE3_DEV_SUPPORT_VLAN_FLTR_MDF_B,
	}, {
		.name = "support FEC statistics",
		.cap_bit = HNAE3_DEV_SUPPORT_FEC_STATS_B,
	}, {
		.name = "support lane num",
		.cap_bit = HNAE3_DEV_SUPPORT_LANE_NUM_B,
	}, {
		.name = "support wake on lan",
		.cap_bit = HNAE3_DEV_SUPPORT_WOL_B,
	}, {
		.name = "support tm flush",
		.cap_bit = HNAE3_DEV_SUPPORT_TM_FLUSH_B,
	}, {
		.name = "support vf fault detect",
		.cap_bit = HNAE3_DEV_SUPPORT_VF_FAULT_B,
	}
};

static const char * const dim_cqe_mode_str[] = { "EQE", "CQE" };
static const char * const dim_state_str[] = { "START", "IN_PROG", "APPLY" };
static const char * const
dim_tune_stat_str[] = { "ON_TOP", "TIRED", "RIGHT", "LEFT" };

static void hns3_get_coal_info(struct hns3_enet_tqp_vector *tqp_vector,
			       struct seq_file *s, int i, bool is_tx)
{
	unsigned int gl_offset, ql_offset;
	struct hns3_enet_coalesce *coal;
	unsigned int reg_val;
	struct dim *dim;
	bool ql_enable;

	if (is_tx) {
		coal = &tqp_vector->tx_group.coal;
		dim = &tqp_vector->tx_group.dim;
		gl_offset = HNS3_VECTOR_GL1_OFFSET;
		ql_offset = HNS3_VECTOR_TX_QL_OFFSET;
		ql_enable = tqp_vector->tx_group.coal.ql_enable;
	} else {
		coal = &tqp_vector->rx_group.coal;
		dim = &tqp_vector->rx_group.dim;
		gl_offset = HNS3_VECTOR_GL0_OFFSET;
		ql_offset = HNS3_VECTOR_RX_QL_OFFSET;
		ql_enable = tqp_vector->rx_group.coal.ql_enable;
	}

	seq_printf(s, "%-8d", i);
	seq_printf(s, "%-12s", dim->state < ARRAY_SIZE(dim_state_str) ?
		   dim_state_str[dim->state] : "unknown");
	seq_printf(s, "%-12u", dim->profile_ix);
	seq_printf(s, "%-10s", dim->mode < ARRAY_SIZE(dim_cqe_mode_str) ?
		   dim_cqe_mode_str[dim->mode] : "unknown");
	seq_printf(s, "%-12s", dim->tune_state < ARRAY_SIZE(dim_tune_stat_str) ?
		   dim_tune_stat_str[dim->tune_state] : "unknown");
	seq_printf(s, "%-12u%-13u%-7u%-7u%-7u", dim->steps_left,
		   dim->steps_right, dim->tired, coal->int_gl, coal->int_ql);
	reg_val = readl(tqp_vector->mask_addr + gl_offset) &
		  HNS3_VECTOR_GL_MASK;
	seq_printf(s, "%-7u", reg_val);
	if (ql_enable) {
		reg_val = readl(tqp_vector->mask_addr + ql_offset) &
			  HNS3_VECTOR_QL_MASK;
		seq_printf(s, "%u\n", reg_val);
	} else {
		seq_puts(s, "NA\n");
	}
}

static void hns3_dump_coal_info(struct seq_file *s, bool is_tx)
{
	struct hnae3_handle *h = hnae3_seq_file_to_handle(s);
	struct hns3_enet_tqp_vector *tqp_vector;
	struct hns3_nic_priv *priv = h->priv;
	unsigned int i;

	seq_printf(s, "%s interrupt coalesce info:\n", is_tx ? "tx" : "rx");

	seq_puts(s, "VEC_ID  ALGO_STATE  PROFILE_ID  CQE_MODE  TUNE_STATE  ");
	seq_puts(s, "STEPS_LEFT  STEPS_RIGHT  TIRED  SW_GL  SW_QL  ");
	seq_puts(s, "HW_GL  HW_QL\n");

	for (i = 0; i < priv->vector_num; i++) {
		tqp_vector = &priv->tqp_vector[i];
		hns3_get_coal_info(tqp_vector, s, i, is_tx);
	}
}

static int hns3_dbg_coal_info(struct seq_file *s, void *data)
{
	hns3_dump_coal_info(s, true);
	seq_puts(s, "\n");
	hns3_dump_coal_info(s, false);

	return 0;
}

static void hns3_dump_rx_queue_info(struct hns3_enet_ring *ring,
				    struct seq_file *s, u32 index)
{
	struct hnae3_ae_dev *ae_dev = hnae3_seq_file_to_ae_dev(s);
	void __iomem *base = ring->tqp->io_base;
	u32 base_add_l, base_add_h;

	seq_printf(s, "%-10u", index);
	seq_printf(s, "%-8u",
		   readl_relaxed(base + HNS3_RING_RX_RING_BD_NUM_REG));
	seq_printf(s, "%-8u",
		   readl_relaxed(base + HNS3_RING_RX_RING_BD_LEN_REG));
	seq_printf(s, "%-6u",
		   readl_relaxed(base + HNS3_RING_RX_RING_TAIL_REG));
	seq_printf(s, "%-6u",
		   readl_relaxed(base + HNS3_RING_RX_RING_HEAD_REG));
	seq_printf(s, "%-8u",
		   readl_relaxed(base + HNS3_RING_RX_RING_FBDNUM_REG));
	seq_printf(s, "%-11u", readl_relaxed(base +
		   HNS3_RING_RX_RING_PKTNUM_RECORD_REG));
	seq_printf(s, "%-11u", ring->rx_copybreak);
	seq_printf(s, "%-9s",
		   str_on_off(readl_relaxed(base + HNS3_RING_EN_REG)));

	if (hnae3_ae_dev_tqp_txrx_indep_supported(ae_dev))
		seq_printf(s, "%-12s", str_on_off(readl_relaxed(base +
						  HNS3_RING_RX_EN_REG)));
	else
		seq_printf(s, "%-12s", "NA");

	base_add_h = readl_relaxed(base + HNS3_RING_RX_RING_BASEADDR_H_REG);
	base_add_l = readl_relaxed(base + HNS3_RING_RX_RING_BASEADDR_L_REG);
	seq_printf(s, "0x%08x%08x\n", base_add_h, base_add_l);
}

static int hns3_dbg_rx_queue_info(struct seq_file *s, void *data)
{
	struct hnae3_handle *h = hnae3_seq_file_to_handle(s);
	struct hns3_nic_priv *priv = h->priv;
	struct hns3_enet_ring *ring;
	u32 i;

	if (!priv->ring) {
		dev_err(&h->pdev->dev, "priv->ring is NULL\n");
		return -EFAULT;
	}

	seq_puts(s, "QUEUE_ID  BD_NUM  BD_LEN  TAIL  HEAD  FBDNUM  ");
	seq_puts(s, "PKTNUM     COPYBREAK  RING_EN  RX_RING_EN  BASE_ADDR\n");

	for (i = 0; i < h->kinfo.num_tqps; i++) {
		/* Each cycle needs to determine whether the instance is reset,
		 * to prevent reference to invalid memory. And need to ensure
		 * that the following code is executed within 100ms.
		 */
		if (!test_bit(HNS3_NIC_STATE_INITED, &priv->state) ||
		    test_bit(HNS3_NIC_STATE_RESETTING, &priv->state))
			return -EPERM;

		ring = &priv->ring[(u32)(i + h->kinfo.num_tqps)];
		hns3_dump_rx_queue_info(ring, s, i);
	}

	return 0;
}

static void hns3_dump_tx_queue_info(struct hns3_enet_ring *ring,
				    struct seq_file *s, u32 index)
{
	struct hnae3_ae_dev *ae_dev = hnae3_seq_file_to_ae_dev(s);
	void __iomem *base = ring->tqp->io_base;
	u32 base_add_l, base_add_h;

	seq_printf(s, "%-10u", index);
	seq_printf(s, "%-8u",
		   readl_relaxed(base + HNS3_RING_TX_RING_BD_NUM_REG));
	seq_printf(s, "%-4u", readl_relaxed(base + HNS3_RING_TX_RING_TC_REG));
	seq_printf(s, "%-6u", readl_relaxed(base + HNS3_RING_TX_RING_TAIL_REG));
	seq_printf(s, "%-6u", readl_relaxed(base + HNS3_RING_TX_RING_HEAD_REG));
	seq_printf(s, "%-8u",
		   readl_relaxed(base + HNS3_RING_TX_RING_FBDNUM_REG));
	seq_printf(s, "%-8u",
		   readl_relaxed(base + HNS3_RING_TX_RING_OFFSET_REG));
	seq_printf(s, "%-11u",
		   readl_relaxed(base + HNS3_RING_TX_RING_PKTNUM_RECORD_REG));
	seq_printf(s, "%-9s",
		   str_on_off(readl_relaxed(base + HNS3_RING_EN_REG)));

	if (hnae3_ae_dev_tqp_txrx_indep_supported(ae_dev))
		seq_printf(s, "%-12s",
			   str_on_off(readl_relaxed(base +
						    HNS3_RING_TX_EN_REG)));
	else
		seq_printf(s, "%-12s", "NA");

	base_add_h = readl_relaxed(base + HNS3_RING_TX_RING_BASEADDR_H_REG);
	base_add_l = readl_relaxed(base + HNS3_RING_TX_RING_BASEADDR_L_REG);
	seq_printf(s, "0x%08x%08x\n", base_add_h, base_add_l);
}

static int hns3_dbg_tx_queue_info(struct seq_file *s, void *data)
{
	struct hnae3_handle *h = hnae3_seq_file_to_handle(s);
	struct hns3_nic_priv *priv = h->priv;
	struct hns3_enet_ring *ring;
	u32 i;

	if (!priv->ring) {
		dev_err(&h->pdev->dev, "priv->ring is NULL\n");
		return -EFAULT;
	}

	seq_puts(s, "QUEUE_ID  BD_NUM  TC  TAIL  HEAD  FBDNUM  OFFSET  ");
	seq_puts(s, "PKTNUM     RING_EN  TX_RING_EN  BASE_ADDR\n");

	for (i = 0; i < h->kinfo.num_tqps; i++) {
		/* Each cycle needs to determine whether the instance is reset,
		 * to prevent reference to invalid memory. And need to ensure
		 * that the following code is executed within 100ms.
		 */
		if (!test_bit(HNS3_NIC_STATE_INITED, &priv->state) ||
		    test_bit(HNS3_NIC_STATE_RESETTING, &priv->state))
			return -EPERM;

		ring = &priv->ring[i];
		hns3_dump_tx_queue_info(ring, s, i);
	}

	return 0;
}

static int hns3_dbg_queue_map(struct seq_file *s, void *data)
{
	struct hnae3_handle *h = hnae3_seq_file_to_handle(s);
	struct hns3_nic_priv *priv = h->priv;
	u32 i;

	if (!h->ae_algo->ops->get_global_queue_id)
		return -EOPNOTSUPP;

	seq_puts(s, "local_queue_id  global_queue_id  vector_id\n");

	for (i = 0; i < h->kinfo.num_tqps; i++) {
		if (!priv->ring || !priv->ring[i].tqp_vector)
			continue;
		seq_printf(s, "%-16u%-17u%d\n", i,
			   h->ae_algo->ops->get_global_queue_id(h, i),
			   priv->ring[i].tqp_vector->vector_irq);
	}

	return 0;
}

static void hns3_dump_rx_bd_info(struct hns3_nic_priv *priv,
				 struct hns3_desc *desc, struct seq_file *s,
				 int idx)
{
	seq_printf(s, "%-9d%#-11x%-10u%-8u%#-12x%-7u%-10u%-17u%-13u%#-14x",
		   idx, le32_to_cpu(desc->rx.l234_info),
		   le16_to_cpu(desc->rx.pkt_len), le16_to_cpu(desc->rx.size),
		   le32_to_cpu(desc->rx.rss_hash), le16_to_cpu(desc->rx.fd_id),
		   le16_to_cpu(desc->rx.vlan_tag),
		   le16_to_cpu(desc->rx.o_dm_vlan_id_fb),
		   le16_to_cpu(desc->rx.ot_vlan_tag),
		   le32_to_cpu(desc->rx.bd_base_info));

	if (test_bit(HNS3_NIC_STATE_RXD_ADV_LAYOUT_ENABLE, &priv->state)) {
		u32 ol_info = le32_to_cpu(desc->rx.ol_info);

		seq_printf(s, "%-7lu%-9u\n",
			   hnae3_get_field(ol_info, HNS3_RXD_PTYPE_M,
					   HNS3_RXD_PTYPE_S),
			   le16_to_cpu(desc->csum));
	} else {
		seq_puts(s, "NA     NA\n");
	}
}

static int hns3_dbg_rx_bd_info(struct seq_file *s, void *private)
{
	struct hns3_dbg_data *data = s->private;
	struct hnae3_handle *h = data->handle;
	struct hns3_nic_priv *priv = h->priv;
	struct hns3_enet_ring *ring;
	struct hns3_desc *desc;
	unsigned int i;

	if (data->qid >= h->kinfo.num_tqps) {
		dev_err(&h->pdev->dev, "queue%u is not in use\n", data->qid);
		return -EINVAL;
	}

	seq_printf(s, "Queue %u rx bd info:\n", data->qid);
	seq_puts(s, "BD_IDX   L234_INFO  PKT_LEN   SIZE    ");
	seq_puts(s, "RSS_HASH    FD_ID  VLAN_TAG  O_DM_VLAN_ID_FB  ");
	seq_puts(s, "OT_VLAN_TAG  BD_BASE_INFO  PTYPE  HW_CSUM\n");

	ring = &priv->ring[data->qid + data->handle->kinfo.num_tqps];
	for (i = 0; i < ring->desc_num; i++) {
		desc = &ring->desc[i];

		hns3_dump_rx_bd_info(priv, desc, s, i);
	}

	return 0;
}

static void hns3_dump_tx_bd_info(struct hns3_desc *desc, struct seq_file *s,
				 int idx)
{
	seq_printf(s, "%-8d%#-20llx%-10u%-6u%#-15x%-14u%-7u%-16u%#-14x%#-14x%-11u\n",
		   idx, le64_to_cpu(desc->addr),
		   le16_to_cpu(desc->tx.vlan_tag),
		   le16_to_cpu(desc->tx.send_size),
		   le32_to_cpu(desc->tx.type_cs_vlan_tso_len),
		   le16_to_cpu(desc->tx.outer_vlan_tag),
		   le16_to_cpu(desc->tx.tv),
		   le32_to_cpu(desc->tx.ol_type_vlan_len_msec),
		   le32_to_cpu(desc->tx.paylen_ol4cs),
		   le16_to_cpu(desc->tx.bdtp_fe_sc_vld_ra_ri),
		   le16_to_cpu(desc->tx.mss_hw_csum));
}

static int hns3_dbg_tx_bd_info(struct seq_file *s, void *private)
{
	struct hns3_dbg_data *data = s->private;
	struct hnae3_handle *h = data->handle;
	struct hns3_nic_priv *priv = h->priv;
	struct hns3_enet_ring *ring;
	struct hns3_desc *desc;
	unsigned int i;

	if (data->qid >= h->kinfo.num_tqps) {
		dev_err(&h->pdev->dev, "queue%u is not in use\n", data->qid);
		return -EINVAL;
	}

	seq_printf(s, "Queue %u tx bd info:\n", data->qid);
	seq_puts(s, "BD_IDX  ADDRESS             VLAN_TAG  SIZE  ");
	seq_puts(s, "T_CS_VLAN_TSO  OT_VLAN_TAG   TV     OLT_VLAN_LEN  ");
	seq_puts(s, "PAYLEN_OL4CS  BD_FE_SC_VLD   MSS_HW_CSUM\n");

	ring = &priv->ring[data->qid];
	for (i = 0; i < ring->desc_num; i++) {
		desc = &ring->desc[i];

		hns3_dump_tx_bd_info(desc, s, i);
	}

	return 0;
}

static void hns3_dbg_dev_caps(struct hnae3_handle *h, struct seq_file *s)
{
	struct hnae3_ae_dev *ae_dev = hns3_get_ae_dev(h);
	unsigned long *caps = ae_dev->caps;
	u32 i, state;

	seq_puts(s, "dev capability:\n");

	for (i = 0; i < ARRAY_SIZE(hns3_dbg_cap); i++) {
		state = test_bit(hns3_dbg_cap[i].cap_bit, caps);
		seq_printf(s, "%s: %s\n", hns3_dbg_cap[i].name,
			   str_yes_no(state));
	}

	seq_puts(s, "\n");
}

static void hns3_dbg_dev_specs(struct hnae3_handle *h, struct seq_file *s)
{
	struct hnae3_ae_dev *ae_dev = pci_get_drvdata(h->pdev);
	struct hnae3_dev_specs *dev_specs = &ae_dev->dev_specs;
	struct hnae3_knic_private_info *kinfo = &h->kinfo;
	struct net_device *dev = kinfo->netdev;

	seq_puts(s, "dev_spec:\n");
	seq_printf(s, "MAC entry num: %u\n", dev_specs->mac_entry_num);
	seq_printf(s, "MNG entry num: %u\n", dev_specs->mng_entry_num);
	seq_printf(s, "MAX non tso bd num: %u\n",
		   dev_specs->max_non_tso_bd_num);
	seq_printf(s, "RSS ind tbl size: %u\n", dev_specs->rss_ind_tbl_size);
	seq_printf(s, "RSS key size: %u\n", dev_specs->rss_key_size);
	seq_printf(s, "RSS size: %u\n", kinfo->rss_size);
	seq_printf(s, "Allocated RSS size: %u\n", kinfo->req_rss_size);
	seq_printf(s, "Task queue pairs numbers: %u\n", kinfo->num_tqps);
	seq_printf(s, "RX buffer length: %u\n", kinfo->rx_buf_len);
	seq_printf(s, "Desc num per TX queue: %u\n", kinfo->num_tx_desc);
	seq_printf(s, "Desc num per RX queue: %u\n", kinfo->num_rx_desc);
	seq_printf(s, "Total number of enabled TCs: %u\n",
		   kinfo->tc_info.num_tc);
	seq_printf(s, "MAX INT QL: %u\n", dev_specs->int_ql_max);
	seq_printf(s, "MAX INT GL: %u\n", dev_specs->max_int_gl);
	seq_printf(s, "MAX TM RATE: %u\n", dev_specs->max_tm_rate);
	seq_printf(s, "MAX QSET number: %u\n", dev_specs->max_qset_num);
	seq_printf(s, "umv size: %u\n", dev_specs->umv_size);
	seq_printf(s, "mc mac size: %u\n", dev_specs->mc_mac_size);
	seq_printf(s, "MAC statistics number: %u\n", dev_specs->mac_stats_num);
	seq_printf(s, "TX timeout threshold: %d seconds\n",
		   dev->watchdog_timeo / HZ);
	seq_printf(s, "mac tunnel number: %u\n", dev_specs->tnl_num);
	seq_printf(s, "Hilink Version: %u\n", dev_specs->hilink_version);
}

static int hns3_dbg_dev_info(struct seq_file *s, void *data)
{
	struct hnae3_handle *h = hnae3_seq_file_to_handle(s);

	hns3_dbg_dev_caps(h, s);
	hns3_dbg_dev_specs(h, s);

	return 0;
}

static void hns3_dump_page_pool_info(struct hns3_enet_ring *ring,
				     struct seq_file *s, u32 index)
{
	seq_printf(s, "%-10u%-14u%-14d%-21u%-7u%-9d%uK\n",
		   index,
		   READ_ONCE(ring->page_pool->pages_state_hold_cnt),
		   atomic_read(&ring->page_pool->pages_state_release_cnt),
		   ring->page_pool->p.pool_size,
		   ring->page_pool->p.order,
		   ring->page_pool->p.nid,
		   ring->page_pool->p.max_len / 1024);
}

static int hns3_dbg_page_pool_info(struct seq_file *s, void *data)
{
	struct hnae3_handle *h = hnae3_seq_file_to_handle(s);
	struct hns3_nic_priv *priv = h->priv;
	struct hns3_enet_ring *ring;
	u32 i;

	if (!priv->ring) {
		dev_err(&h->pdev->dev, "priv->ring is NULL\n");
		return -EFAULT;
	}

	if (!priv->ring[h->kinfo.num_tqps].page_pool) {
		dev_err(&h->pdev->dev, "page pool is not initialized\n");
		return -EFAULT;
	}

	seq_puts(s, "QUEUE_ID  ALLOCATE_CNT  FREE_CNT      ");
	seq_puts(s, "POOL_SIZE(PAGE_NUM)  ORDER  NUMA_ID  MAX_LEN\n");

	for (i = 0; i < h->kinfo.num_tqps; i++) {
		if (!test_bit(HNS3_NIC_STATE_INITED, &priv->state) ||
		    test_bit(HNS3_NIC_STATE_RESETTING, &priv->state))
			return -EPERM;

		ring = &priv->ring[(u32)(i + h->kinfo.num_tqps)];
		hns3_dump_page_pool_info(ring, s, i);
	}

	return 0;
}

static int hns3_dbg_bd_info_show(struct seq_file *s, void *private)
{
	struct hns3_dbg_data *data = s->private;
	struct hnae3_handle *h = data->handle;
	struct hns3_nic_priv *priv = h->priv;

	if (!test_bit(HNS3_NIC_STATE_INITED, &priv->state) ||
	    test_bit(HNS3_NIC_STATE_RESETTING, &priv->state))
		return -EBUSY;

	if (data->cmd == HNAE3_DBG_CMD_TX_BD)
		return hns3_dbg_tx_bd_info(s, private);
	else if (data->cmd == HNAE3_DBG_CMD_RX_BD)
		return hns3_dbg_rx_bd_info(s, private);

	return -EOPNOTSUPP;
}
DEFINE_SHOW_ATTRIBUTE(hns3_dbg_bd_info);

static int hns3_dbg_bd_file_init(struct hnae3_handle *handle, u32 cmd)
{
	struct hns3_dbg_data *data;
	struct dentry *entry_dir;
	u16 max_queue_num;
	unsigned int i;

	entry_dir = hns3_dbg_dentry[hns3_dbg_cmd[cmd].dentry].dentry;
	max_queue_num = hns3_get_max_available_channels(handle);
	data = devm_kcalloc(&handle->pdev->dev, max_queue_num, sizeof(*data),
			    GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	for (i = 0; i < max_queue_num; i++) {
		char name[HNS3_DBG_FILE_NAME_LEN];

		data[i].handle = handle;
		data[i].cmd = hns3_dbg_cmd[cmd].cmd;
		data[i].qid = i;
		sprintf(name, "%s%u", hns3_dbg_cmd[cmd].name, i);
		debugfs_create_file(name, 0400, entry_dir, &data[i],
				    &hns3_dbg_bd_info_fops);
	}

	return 0;
}

static int hns3_dbg_common_init_t1(struct hnae3_handle *handle, u32 cmd)
{
	struct device *dev = &handle->pdev->dev;
	struct dentry *entry_dir;
	read_func func = NULL;

	switch (hns3_dbg_cmd[cmd].cmd) {
	case HNAE3_DBG_CMD_TX_QUEUE_INFO:
		func = hns3_dbg_tx_queue_info;
		break;
	case HNAE3_DBG_CMD_RX_QUEUE_INFO:
		func = hns3_dbg_rx_queue_info;
		break;
	case HNAE3_DBG_CMD_QUEUE_MAP:
		func = hns3_dbg_queue_map;
		break;
	case HNAE3_DBG_CMD_PAGE_POOL_INFO:
		func = hns3_dbg_page_pool_info;
		break;
	case HNAE3_DBG_CMD_COAL_INFO:
		func = hns3_dbg_coal_info;
		break;
	case HNAE3_DBG_CMD_DEV_INFO:
		func = hns3_dbg_dev_info;
		break;
	default:
		return -EINVAL;
	}

	entry_dir = hns3_dbg_dentry[hns3_dbg_cmd[cmd].dentry].dentry;
	debugfs_create_devm_seqfile(dev, hns3_dbg_cmd[cmd].name, entry_dir,
				    func);

	return 0;
}

static int hns3_dbg_common_init_t2(struct hnae3_handle *handle, u32 cmd)
{
	const struct hnae3_ae_ops *ops = hns3_get_ops(handle);
	struct device *dev = &handle->pdev->dev;
	struct dentry *entry_dir;
	read_func func;
	int ret;

	if (!ops->dbg_get_read_func)
		return 0;

	ret = ops->dbg_get_read_func(handle, hns3_dbg_cmd[cmd].cmd, &func);
	if (ret)
		return ret;

	entry_dir = hns3_dbg_dentry[hns3_dbg_cmd[cmd].dentry].dentry;
	debugfs_create_devm_seqfile(dev, hns3_dbg_cmd[cmd].name, entry_dir,
				    func);

	return 0;
}

int hns3_dbg_init(struct hnae3_handle *handle)
{
	struct hnae3_ae_dev *ae_dev = hns3_get_ae_dev(handle);
	const char *name = pci_name(handle->pdev);
	int ret;
	u32 i;

	hns3_dbg_dentry[HNS3_DBG_DENTRY_COMMON].dentry =
				debugfs_create_dir(name, hns3_dbgfs_root);
	handle->hnae3_dbgfs = hns3_dbg_dentry[HNS3_DBG_DENTRY_COMMON].dentry;

	for (i = 0; i < HNS3_DBG_DENTRY_COMMON; i++)
		hns3_dbg_dentry[i].dentry =
			debugfs_create_dir(hns3_dbg_dentry[i].name,
					   handle->hnae3_dbgfs);

	for (i = 0; i < ARRAY_SIZE(hns3_dbg_cmd); i++) {
		if ((hns3_dbg_cmd[i].cmd == HNAE3_DBG_CMD_TM_NODES &&
		     ae_dev->dev_version <= HNAE3_DEVICE_VERSION_V2) ||
		    (hns3_dbg_cmd[i].cmd == HNAE3_DBG_CMD_PTP_INFO &&
		     !test_bit(HNAE3_DEV_SUPPORT_PTP_B, ae_dev->caps)))
			continue;

		if (!hns3_dbg_cmd[i].init) {
			dev_err(&handle->pdev->dev,
				"cmd %s lack of init func\n",
				hns3_dbg_cmd[i].name);
			ret = -EINVAL;
			goto out;
		}

		ret = hns3_dbg_cmd[i].init(handle, i);
		if (ret) {
			dev_err(&handle->pdev->dev, "failed to init cmd %s\n",
				hns3_dbg_cmd[i].name);
			goto out;
		}
	}

	return 0;

out:
	debugfs_remove_recursive(handle->hnae3_dbgfs);
	handle->hnae3_dbgfs = NULL;
	return ret;
}

void hns3_dbg_uninit(struct hnae3_handle *handle)
{
	debugfs_remove_recursive(handle->hnae3_dbgfs);
	handle->hnae3_dbgfs = NULL;
}

void hns3_dbg_register_debugfs(const char *debugfs_dir_name)
{
	hns3_dbgfs_root = debugfs_create_dir(debugfs_dir_name, NULL);
}

void hns3_dbg_unregister_debugfs(void)
{
	debugfs_remove_recursive(hns3_dbgfs_root);
	hns3_dbgfs_root = NULL;
}
