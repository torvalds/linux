// SPDX-License-Identifier: GPL-2.0+
/* Copyright (c) 2018-2019 Hisilicon Limited. */

#include <linux/debugfs.h>
#include <linux/device.h>

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
static int hns3_dbg_common_file_init(struct hnae3_handle *handle, u32 cmd);

static struct hns3_dbg_cmd_info hns3_dbg_cmd[] = {
	{
		.name = "tm_nodes",
		.cmd = HNAE3_DBG_CMD_TM_NODES,
		.dentry = HNS3_DBG_DENTRY_TM,
		.buf_len = HNS3_DBG_READ_LEN,
		.init = hns3_dbg_common_file_init,
	},
	{
		.name = "tm_priority",
		.cmd = HNAE3_DBG_CMD_TM_PRI,
		.dentry = HNS3_DBG_DENTRY_TM,
		.buf_len = HNS3_DBG_READ_LEN,
		.init = hns3_dbg_common_file_init,
	},
	{
		.name = "tm_qset",
		.cmd = HNAE3_DBG_CMD_TM_QSET,
		.dentry = HNS3_DBG_DENTRY_TM,
		.buf_len = HNS3_DBG_READ_LEN,
		.init = hns3_dbg_common_file_init,
	},
	{
		.name = "tm_map",
		.cmd = HNAE3_DBG_CMD_TM_MAP,
		.dentry = HNS3_DBG_DENTRY_TM,
		.buf_len = HNS3_DBG_READ_LEN_1MB,
		.init = hns3_dbg_common_file_init,
	},
	{
		.name = "tm_pg",
		.cmd = HNAE3_DBG_CMD_TM_PG,
		.dentry = HNS3_DBG_DENTRY_TM,
		.buf_len = HNS3_DBG_READ_LEN,
		.init = hns3_dbg_common_file_init,
	},
	{
		.name = "tm_port",
		.cmd = HNAE3_DBG_CMD_TM_PORT,
		.dentry = HNS3_DBG_DENTRY_TM,
		.buf_len = HNS3_DBG_READ_LEN,
		.init = hns3_dbg_common_file_init,
	},
	{
		.name = "tc_sch_info",
		.cmd = HNAE3_DBG_CMD_TC_SCH_INFO,
		.dentry = HNS3_DBG_DENTRY_TM,
		.buf_len = HNS3_DBG_READ_LEN,
		.init = hns3_dbg_common_file_init,
	},
	{
		.name = "qos_pause_cfg",
		.cmd = HNAE3_DBG_CMD_QOS_PAUSE_CFG,
		.dentry = HNS3_DBG_DENTRY_TM,
		.buf_len = HNS3_DBG_READ_LEN,
		.init = hns3_dbg_common_file_init,
	},
	{
		.name = "qos_pri_map",
		.cmd = HNAE3_DBG_CMD_QOS_PRI_MAP,
		.dentry = HNS3_DBG_DENTRY_TM,
		.buf_len = HNS3_DBG_READ_LEN,
		.init = hns3_dbg_common_file_init,
	},
	{
		.name = "qos_dscp_map",
		.cmd = HNAE3_DBG_CMD_QOS_DSCP_MAP,
		.dentry = HNS3_DBG_DENTRY_TM,
		.buf_len = HNS3_DBG_READ_LEN,
		.init = hns3_dbg_common_file_init,
	},
	{
		.name = "qos_buf_cfg",
		.cmd = HNAE3_DBG_CMD_QOS_BUF_CFG,
		.dentry = HNS3_DBG_DENTRY_TM,
		.buf_len = HNS3_DBG_READ_LEN,
		.init = hns3_dbg_common_file_init,
	},
	{
		.name = "dev_info",
		.cmd = HNAE3_DBG_CMD_DEV_INFO,
		.dentry = HNS3_DBG_DENTRY_COMMON,
		.buf_len = HNS3_DBG_READ_LEN,
		.init = hns3_dbg_common_file_init,
	},
	{
		.name = "tx_bd_queue",
		.cmd = HNAE3_DBG_CMD_TX_BD,
		.dentry = HNS3_DBG_DENTRY_TX_BD,
		.buf_len = HNS3_DBG_READ_LEN_5MB,
		.init = hns3_dbg_bd_file_init,
	},
	{
		.name = "rx_bd_queue",
		.cmd = HNAE3_DBG_CMD_RX_BD,
		.dentry = HNS3_DBG_DENTRY_RX_BD,
		.buf_len = HNS3_DBG_READ_LEN_4MB,
		.init = hns3_dbg_bd_file_init,
	},
	{
		.name = "uc",
		.cmd = HNAE3_DBG_CMD_MAC_UC,
		.dentry = HNS3_DBG_DENTRY_MAC,
		.buf_len = HNS3_DBG_READ_LEN_128KB,
		.init = hns3_dbg_common_file_init,
	},
	{
		.name = "mc",
		.cmd = HNAE3_DBG_CMD_MAC_MC,
		.dentry = HNS3_DBG_DENTRY_MAC,
		.buf_len = HNS3_DBG_READ_LEN,
		.init = hns3_dbg_common_file_init,
	},
	{
		.name = "mng_tbl",
		.cmd = HNAE3_DBG_CMD_MNG_TBL,
		.dentry = HNS3_DBG_DENTRY_COMMON,
		.buf_len = HNS3_DBG_READ_LEN,
		.init = hns3_dbg_common_file_init,
	},
	{
		.name = "loopback",
		.cmd = HNAE3_DBG_CMD_LOOPBACK,
		.dentry = HNS3_DBG_DENTRY_COMMON,
		.buf_len = HNS3_DBG_READ_LEN,
		.init = hns3_dbg_common_file_init,
	},
	{
		.name = "interrupt_info",
		.cmd = HNAE3_DBG_CMD_INTERRUPT_INFO,
		.dentry = HNS3_DBG_DENTRY_COMMON,
		.buf_len = HNS3_DBG_READ_LEN,
		.init = hns3_dbg_common_file_init,
	},
	{
		.name = "reset_info",
		.cmd = HNAE3_DBG_CMD_RESET_INFO,
		.dentry = HNS3_DBG_DENTRY_COMMON,
		.buf_len = HNS3_DBG_READ_LEN,
		.init = hns3_dbg_common_file_init,
	},
	{
		.name = "imp_info",
		.cmd = HNAE3_DBG_CMD_IMP_INFO,
		.dentry = HNS3_DBG_DENTRY_COMMON,
		.buf_len = HNS3_DBG_READ_LEN,
		.init = hns3_dbg_common_file_init,
	},
	{
		.name = "ncl_config",
		.cmd = HNAE3_DBG_CMD_NCL_CONFIG,
		.dentry = HNS3_DBG_DENTRY_COMMON,
		.buf_len = HNS3_DBG_READ_LEN_128KB,
		.init = hns3_dbg_common_file_init,
	},
	{
		.name = "mac_tnl_status",
		.cmd = HNAE3_DBG_CMD_MAC_TNL_STATUS,
		.dentry = HNS3_DBG_DENTRY_COMMON,
		.buf_len = HNS3_DBG_READ_LEN,
		.init = hns3_dbg_common_file_init,
	},
	{
		.name = "bios_common",
		.cmd = HNAE3_DBG_CMD_REG_BIOS_COMMON,
		.dentry = HNS3_DBG_DENTRY_REG,
		.buf_len = HNS3_DBG_READ_LEN,
		.init = hns3_dbg_common_file_init,
	},
	{
		.name = "ssu",
		.cmd = HNAE3_DBG_CMD_REG_SSU,
		.dentry = HNS3_DBG_DENTRY_REG,
		.buf_len = HNS3_DBG_READ_LEN,
		.init = hns3_dbg_common_file_init,
	},
	{
		.name = "igu_egu",
		.cmd = HNAE3_DBG_CMD_REG_IGU_EGU,
		.dentry = HNS3_DBG_DENTRY_REG,
		.buf_len = HNS3_DBG_READ_LEN,
		.init = hns3_dbg_common_file_init,
	},
	{
		.name = "rpu",
		.cmd = HNAE3_DBG_CMD_REG_RPU,
		.dentry = HNS3_DBG_DENTRY_REG,
		.buf_len = HNS3_DBG_READ_LEN,
		.init = hns3_dbg_common_file_init,
	},
	{
		.name = "ncsi",
		.cmd = HNAE3_DBG_CMD_REG_NCSI,
		.dentry = HNS3_DBG_DENTRY_REG,
		.buf_len = HNS3_DBG_READ_LEN,
		.init = hns3_dbg_common_file_init,
	},
	{
		.name = "rtc",
		.cmd = HNAE3_DBG_CMD_REG_RTC,
		.dentry = HNS3_DBG_DENTRY_REG,
		.buf_len = HNS3_DBG_READ_LEN,
		.init = hns3_dbg_common_file_init,
	},
	{
		.name = "ppp",
		.cmd = HNAE3_DBG_CMD_REG_PPP,
		.dentry = HNS3_DBG_DENTRY_REG,
		.buf_len = HNS3_DBG_READ_LEN,
		.init = hns3_dbg_common_file_init,
	},
	{
		.name = "rcb",
		.cmd = HNAE3_DBG_CMD_REG_RCB,
		.dentry = HNS3_DBG_DENTRY_REG,
		.buf_len = HNS3_DBG_READ_LEN,
		.init = hns3_dbg_common_file_init,
	},
	{
		.name = "tqp",
		.cmd = HNAE3_DBG_CMD_REG_TQP,
		.dentry = HNS3_DBG_DENTRY_REG,
		.buf_len = HNS3_DBG_READ_LEN_128KB,
		.init = hns3_dbg_common_file_init,
	},
	{
		.name = "mac",
		.cmd = HNAE3_DBG_CMD_REG_MAC,
		.dentry = HNS3_DBG_DENTRY_REG,
		.buf_len = HNS3_DBG_READ_LEN,
		.init = hns3_dbg_common_file_init,
	},
	{
		.name = "dcb",
		.cmd = HNAE3_DBG_CMD_REG_DCB,
		.dentry = HNS3_DBG_DENTRY_REG,
		.buf_len = HNS3_DBG_READ_LEN,
		.init = hns3_dbg_common_file_init,
	},
	{
		.name = "queue_map",
		.cmd = HNAE3_DBG_CMD_QUEUE_MAP,
		.dentry = HNS3_DBG_DENTRY_QUEUE,
		.buf_len = HNS3_DBG_READ_LEN,
		.init = hns3_dbg_common_file_init,
	},
	{
		.name = "rx_queue_info",
		.cmd = HNAE3_DBG_CMD_RX_QUEUE_INFO,
		.dentry = HNS3_DBG_DENTRY_QUEUE,
		.buf_len = HNS3_DBG_READ_LEN_1MB,
		.init = hns3_dbg_common_file_init,
	},
	{
		.name = "tx_queue_info",
		.cmd = HNAE3_DBG_CMD_TX_QUEUE_INFO,
		.dentry = HNS3_DBG_DENTRY_QUEUE,
		.buf_len = HNS3_DBG_READ_LEN_1MB,
		.init = hns3_dbg_common_file_init,
	},
	{
		.name = "fd_tcam",
		.cmd = HNAE3_DBG_CMD_FD_TCAM,
		.dentry = HNS3_DBG_DENTRY_FD,
		.buf_len = HNS3_DBG_READ_LEN_1MB,
		.init = hns3_dbg_common_file_init,
	},
	{
		.name = "service_task_info",
		.cmd = HNAE3_DBG_CMD_SERV_INFO,
		.dentry = HNS3_DBG_DENTRY_COMMON,
		.buf_len = HNS3_DBG_READ_LEN,
		.init = hns3_dbg_common_file_init,
	},
	{
		.name = "vlan_config",
		.cmd = HNAE3_DBG_CMD_VLAN_CONFIG,
		.dentry = HNS3_DBG_DENTRY_COMMON,
		.buf_len = HNS3_DBG_READ_LEN,
		.init = hns3_dbg_common_file_init,
	},
	{
		.name = "ptp_info",
		.cmd = HNAE3_DBG_CMD_PTP_INFO,
		.dentry = HNS3_DBG_DENTRY_COMMON,
		.buf_len = HNS3_DBG_READ_LEN,
		.init = hns3_dbg_common_file_init,
	},
	{
		.name = "fd_counter",
		.cmd = HNAE3_DBG_CMD_FD_COUNTER,
		.dentry = HNS3_DBG_DENTRY_FD,
		.buf_len = HNS3_DBG_READ_LEN,
		.init = hns3_dbg_common_file_init,
	},
	{
		.name = "umv_info",
		.cmd = HNAE3_DBG_CMD_UMV_INFO,
		.dentry = HNS3_DBG_DENTRY_COMMON,
		.buf_len = HNS3_DBG_READ_LEN,
		.init = hns3_dbg_common_file_init,
	},
	{
		.name = "page_pool_info",
		.cmd = HNAE3_DBG_CMD_PAGE_POOL_INFO,
		.dentry = HNS3_DBG_DENTRY_COMMON,
		.buf_len = HNS3_DBG_READ_LEN,
		.init = hns3_dbg_common_file_init,
	},
	{
		.name = "coalesce_info",
		.cmd = HNAE3_DBG_CMD_COAL_INFO,
		.dentry = HNS3_DBG_DENTRY_COMMON,
		.buf_len = HNS3_DBG_READ_LEN_1MB,
		.init = hns3_dbg_common_file_init,
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

static const struct hns3_dbg_item coal_info_items[] = {
	{ "VEC_ID", 2 },
	{ "ALGO_STATE", 2 },
	{ "PROFILE_ID", 2 },
	{ "CQE_MODE", 2 },
	{ "TUNE_STATE", 2 },
	{ "STEPS_LEFT", 2 },
	{ "STEPS_RIGHT", 2 },
	{ "TIRED", 2 },
	{ "SW_GL", 2 },
	{ "SW_QL", 2 },
	{ "HW_GL", 2 },
	{ "HW_QL", 2 },
};

static const char * const dim_cqe_mode_str[] = { "EQE", "CQE" };
static const char * const dim_state_str[] = { "START", "IN_PROG", "APPLY" };
static const char * const
dim_tune_stat_str[] = { "ON_TOP", "TIRED", "RIGHT", "LEFT" };

static void hns3_dbg_fill_content(char *content, u16 len,
				  const struct hns3_dbg_item *items,
				  const char **result, u16 size)
{
#define HNS3_DBG_LINE_END_LEN	2
	char *pos = content;
	u16 item_len;
	u16 i;

	if (!len) {
		return;
	} else if (len <= HNS3_DBG_LINE_END_LEN) {
		*pos++ = '\0';
		return;
	}

	memset(content, ' ', len);
	len -= HNS3_DBG_LINE_END_LEN;

	for (i = 0; i < size; i++) {
		item_len = strlen(items[i].name) + items[i].interval;
		if (len < item_len)
			break;

		if (result) {
			if (item_len < strlen(result[i]))
				break;
			memcpy(pos, result[i], strlen(result[i]));
		} else {
			memcpy(pos, items[i].name, strlen(items[i].name));
		}
		pos += item_len;
		len -= item_len;
	}
	*pos++ = '\n';
	*pos++ = '\0';
}

static void hns3_get_coal_info(struct hns3_enet_tqp_vector *tqp_vector,
			       char **result, int i, bool is_tx)
{
	unsigned int gl_offset, ql_offset;
	struct hns3_enet_coalesce *coal;
	unsigned int reg_val;
	unsigned int j = 0;
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

	sprintf(result[j++], "%d", i);
	sprintf(result[j++], "%s", dim->state < ARRAY_SIZE(dim_state_str) ?
		dim_state_str[dim->state] : "unknown");
	sprintf(result[j++], "%u", dim->profile_ix);
	sprintf(result[j++], "%s", dim->mode < ARRAY_SIZE(dim_cqe_mode_str) ?
		dim_cqe_mode_str[dim->mode] : "unknown");
	sprintf(result[j++], "%s",
		dim->tune_state < ARRAY_SIZE(dim_tune_stat_str) ?
		dim_tune_stat_str[dim->tune_state] : "unknown");
	sprintf(result[j++], "%u", dim->steps_left);
	sprintf(result[j++], "%u", dim->steps_right);
	sprintf(result[j++], "%u", dim->tired);
	sprintf(result[j++], "%u", coal->int_gl);
	sprintf(result[j++], "%u", coal->int_ql);
	reg_val = readl(tqp_vector->mask_addr + gl_offset) &
		  HNS3_VECTOR_GL_MASK;
	sprintf(result[j++], "%u", reg_val);
	if (ql_enable) {
		reg_val = readl(tqp_vector->mask_addr + ql_offset) &
			  HNS3_VECTOR_QL_MASK;
		sprintf(result[j++], "%u", reg_val);
	} else {
		sprintf(result[j++], "NA");
	}
}

static void hns3_dump_coal_info(struct hnae3_handle *h, char *buf, int len,
				int *pos, bool is_tx)
{
	char data_str[ARRAY_SIZE(coal_info_items)][HNS3_DBG_DATA_STR_LEN];
	char *result[ARRAY_SIZE(coal_info_items)];
	struct hns3_enet_tqp_vector *tqp_vector;
	struct hns3_nic_priv *priv = h->priv;
	char content[HNS3_DBG_INFO_LEN];
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(coal_info_items); i++)
		result[i] = &data_str[i][0];

	*pos += scnprintf(buf + *pos, len - *pos,
			  "%s interrupt coalesce info:\n",
			  is_tx ? "tx" : "rx");
	hns3_dbg_fill_content(content, sizeof(content), coal_info_items,
			      NULL, ARRAY_SIZE(coal_info_items));
	*pos += scnprintf(buf + *pos, len - *pos, "%s", content);

	for (i = 0; i < priv->vector_num; i++) {
		tqp_vector = &priv->tqp_vector[i];
		hns3_get_coal_info(tqp_vector, result, i, is_tx);
		hns3_dbg_fill_content(content, sizeof(content), coal_info_items,
				      (const char **)result,
				      ARRAY_SIZE(coal_info_items));
		*pos += scnprintf(buf + *pos, len - *pos, "%s", content);
	}
}

static int hns3_dbg_coal_info(struct hnae3_handle *h, char *buf, int len)
{
	int pos = 0;

	hns3_dump_coal_info(h, buf, len, &pos, true);
	pos += scnprintf(buf + pos, len - pos, "\n");
	hns3_dump_coal_info(h, buf, len, &pos, false);

	return 0;
}

static const struct hns3_dbg_item tx_spare_info_items[] = {
	{ "QUEUE_ID", 2 },
	{ "COPYBREAK", 2 },
	{ "LEN", 7 },
	{ "NTU", 4 },
	{ "NTC", 4 },
	{ "LTC", 4 },
	{ "DMA", 17 },
};

static void hns3_dbg_tx_spare_info(struct hns3_enet_ring *ring, char *buf,
				   int len, u32 ring_num, int *pos)
{
	char data_str[ARRAY_SIZE(tx_spare_info_items)][HNS3_DBG_DATA_STR_LEN];
	struct hns3_tx_spare *tx_spare = ring->tx_spare;
	char *result[ARRAY_SIZE(tx_spare_info_items)];
	char content[HNS3_DBG_INFO_LEN];
	u32 i, j;

	if (!tx_spare) {
		*pos += scnprintf(buf + *pos, len - *pos,
				  "tx spare buffer is not enabled\n");
		return;
	}

	for (i = 0; i < ARRAY_SIZE(tx_spare_info_items); i++)
		result[i] = &data_str[i][0];

	*pos += scnprintf(buf + *pos, len - *pos, "tx spare buffer info\n");
	hns3_dbg_fill_content(content, sizeof(content), tx_spare_info_items,
			      NULL, ARRAY_SIZE(tx_spare_info_items));
	*pos += scnprintf(buf + *pos, len - *pos, "%s", content);

	for (i = 0; i < ring_num; i++) {
		j = 0;
		sprintf(result[j++], "%u", i);
		sprintf(result[j++], "%u", ring->tx_copybreak);
		sprintf(result[j++], "%u", tx_spare->len);
		sprintf(result[j++], "%u", tx_spare->next_to_use);
		sprintf(result[j++], "%u", tx_spare->next_to_clean);
		sprintf(result[j++], "%u", tx_spare->last_to_clean);
		sprintf(result[j++], "%pad", &tx_spare->dma);
		hns3_dbg_fill_content(content, sizeof(content),
				      tx_spare_info_items,
				      (const char **)result,
				      ARRAY_SIZE(tx_spare_info_items));
		*pos += scnprintf(buf + *pos, len - *pos, "%s", content);
	}
}

static const struct hns3_dbg_item rx_queue_info_items[] = {
	{ "QUEUE_ID", 2 },
	{ "BD_NUM", 2 },
	{ "BD_LEN", 2 },
	{ "TAIL", 2 },
	{ "HEAD", 2 },
	{ "FBDNUM", 2 },
	{ "PKTNUM", 5 },
	{ "COPYBREAK", 2 },
	{ "RING_EN", 2 },
	{ "RX_RING_EN", 2 },
	{ "BASE_ADDR", 10 },
};

static void hns3_dump_rx_queue_info(struct hns3_enet_ring *ring,
				    struct hnae3_ae_dev *ae_dev, char **result,
				    u32 index)
{
	u32 base_add_l, base_add_h;
	u32 j = 0;

	sprintf(result[j++], "%u", index);

	sprintf(result[j++], "%u", readl_relaxed(ring->tqp->io_base +
		HNS3_RING_RX_RING_BD_NUM_REG));

	sprintf(result[j++], "%u", readl_relaxed(ring->tqp->io_base +
		HNS3_RING_RX_RING_BD_LEN_REG));

	sprintf(result[j++], "%u", readl_relaxed(ring->tqp->io_base +
		HNS3_RING_RX_RING_TAIL_REG));

	sprintf(result[j++], "%u", readl_relaxed(ring->tqp->io_base +
		HNS3_RING_RX_RING_HEAD_REG));

	sprintf(result[j++], "%u", readl_relaxed(ring->tqp->io_base +
		HNS3_RING_RX_RING_FBDNUM_REG));

	sprintf(result[j++], "%u", readl_relaxed(ring->tqp->io_base +
		HNS3_RING_RX_RING_PKTNUM_RECORD_REG));
	sprintf(result[j++], "%u", ring->rx_copybreak);

	sprintf(result[j++], "%s", readl_relaxed(ring->tqp->io_base +
		HNS3_RING_EN_REG) ? "on" : "off");

	if (hnae3_ae_dev_tqp_txrx_indep_supported(ae_dev))
		sprintf(result[j++], "%s", readl_relaxed(ring->tqp->io_base +
			HNS3_RING_RX_EN_REG) ? "on" : "off");
	else
		sprintf(result[j++], "%s", "NA");

	base_add_h = readl_relaxed(ring->tqp->io_base +
					HNS3_RING_RX_RING_BASEADDR_H_REG);
	base_add_l = readl_relaxed(ring->tqp->io_base +
					HNS3_RING_RX_RING_BASEADDR_L_REG);
	sprintf(result[j++], "0x%08x%08x", base_add_h, base_add_l);
}

static int hns3_dbg_rx_queue_info(struct hnae3_handle *h,
				  char *buf, int len)
{
	char data_str[ARRAY_SIZE(rx_queue_info_items)][HNS3_DBG_DATA_STR_LEN];
	struct hnae3_ae_dev *ae_dev = pci_get_drvdata(h->pdev);
	char *result[ARRAY_SIZE(rx_queue_info_items)];
	struct hns3_nic_priv *priv = h->priv;
	char content[HNS3_DBG_INFO_LEN];
	struct hns3_enet_ring *ring;
	int pos = 0;
	u32 i;

	if (!priv->ring) {
		dev_err(&h->pdev->dev, "priv->ring is NULL\n");
		return -EFAULT;
	}

	for (i = 0; i < ARRAY_SIZE(rx_queue_info_items); i++)
		result[i] = &data_str[i][0];

	hns3_dbg_fill_content(content, sizeof(content), rx_queue_info_items,
			      NULL, ARRAY_SIZE(rx_queue_info_items));
	pos += scnprintf(buf + pos, len - pos, "%s", content);
	for (i = 0; i < h->kinfo.num_tqps; i++) {
		/* Each cycle needs to determine whether the instance is reset,
		 * to prevent reference to invalid memory. And need to ensure
		 * that the following code is executed within 100ms.
		 */
		if (!test_bit(HNS3_NIC_STATE_INITED, &priv->state) ||
		    test_bit(HNS3_NIC_STATE_RESETTING, &priv->state))
			return -EPERM;

		ring = &priv->ring[(u32)(i + h->kinfo.num_tqps)];
		hns3_dump_rx_queue_info(ring, ae_dev, result, i);
		hns3_dbg_fill_content(content, sizeof(content),
				      rx_queue_info_items,
				      (const char **)result,
				      ARRAY_SIZE(rx_queue_info_items));
		pos += scnprintf(buf + pos, len - pos, "%s", content);
	}

	return 0;
}

static const struct hns3_dbg_item tx_queue_info_items[] = {
	{ "QUEUE_ID", 2 },
	{ "BD_NUM", 2 },
	{ "TC", 2 },
	{ "TAIL", 2 },
	{ "HEAD", 2 },
	{ "FBDNUM", 2 },
	{ "OFFSET", 2 },
	{ "PKTNUM", 5 },
	{ "RING_EN", 2 },
	{ "TX_RING_EN", 2 },
	{ "BASE_ADDR", 10 },
};

static void hns3_dump_tx_queue_info(struct hns3_enet_ring *ring,
				    struct hnae3_ae_dev *ae_dev, char **result,
				    u32 index)
{
	u32 base_add_l, base_add_h;
	u32 j = 0;

	sprintf(result[j++], "%u", index);
	sprintf(result[j++], "%u", readl_relaxed(ring->tqp->io_base +
		HNS3_RING_TX_RING_BD_NUM_REG));

	sprintf(result[j++], "%u", readl_relaxed(ring->tqp->io_base +
		HNS3_RING_TX_RING_TC_REG));

	sprintf(result[j++], "%u", readl_relaxed(ring->tqp->io_base +
		HNS3_RING_TX_RING_TAIL_REG));

	sprintf(result[j++], "%u", readl_relaxed(ring->tqp->io_base +
		HNS3_RING_TX_RING_HEAD_REG));

	sprintf(result[j++], "%u", readl_relaxed(ring->tqp->io_base +
		HNS3_RING_TX_RING_FBDNUM_REG));

	sprintf(result[j++], "%u", readl_relaxed(ring->tqp->io_base +
		HNS3_RING_TX_RING_OFFSET_REG));

	sprintf(result[j++], "%u", readl_relaxed(ring->tqp->io_base +
		HNS3_RING_TX_RING_PKTNUM_RECORD_REG));

	sprintf(result[j++], "%s", readl_relaxed(ring->tqp->io_base +
		HNS3_RING_EN_REG) ? "on" : "off");

	if (hnae3_ae_dev_tqp_txrx_indep_supported(ae_dev))
		sprintf(result[j++], "%s", readl_relaxed(ring->tqp->io_base +
			HNS3_RING_TX_EN_REG) ? "on" : "off");
	else
		sprintf(result[j++], "%s", "NA");

	base_add_h = readl_relaxed(ring->tqp->io_base +
					HNS3_RING_TX_RING_BASEADDR_H_REG);
	base_add_l = readl_relaxed(ring->tqp->io_base +
					HNS3_RING_TX_RING_BASEADDR_L_REG);
	sprintf(result[j++], "0x%08x%08x", base_add_h, base_add_l);
}

static int hns3_dbg_tx_queue_info(struct hnae3_handle *h,
				  char *buf, int len)
{
	char data_str[ARRAY_SIZE(tx_queue_info_items)][HNS3_DBG_DATA_STR_LEN];
	struct hnae3_ae_dev *ae_dev = pci_get_drvdata(h->pdev);
	char *result[ARRAY_SIZE(tx_queue_info_items)];
	struct hns3_nic_priv *priv = h->priv;
	char content[HNS3_DBG_INFO_LEN];
	struct hns3_enet_ring *ring;
	int pos = 0;
	u32 i;

	if (!priv->ring) {
		dev_err(&h->pdev->dev, "priv->ring is NULL\n");
		return -EFAULT;
	}

	for (i = 0; i < ARRAY_SIZE(tx_queue_info_items); i++)
		result[i] = &data_str[i][0];

	hns3_dbg_fill_content(content, sizeof(content), tx_queue_info_items,
			      NULL, ARRAY_SIZE(tx_queue_info_items));
	pos += scnprintf(buf + pos, len - pos, "%s", content);

	for (i = 0; i < h->kinfo.num_tqps; i++) {
		/* Each cycle needs to determine whether the instance is reset,
		 * to prevent reference to invalid memory. And need to ensure
		 * that the following code is executed within 100ms.
		 */
		if (!test_bit(HNS3_NIC_STATE_INITED, &priv->state) ||
		    test_bit(HNS3_NIC_STATE_RESETTING, &priv->state))
			return -EPERM;

		ring = &priv->ring[i];
		hns3_dump_tx_queue_info(ring, ae_dev, result, i);
		hns3_dbg_fill_content(content, sizeof(content),
				      tx_queue_info_items,
				      (const char **)result,
				      ARRAY_SIZE(tx_queue_info_items));
		pos += scnprintf(buf + pos, len - pos, "%s", content);
	}

	hns3_dbg_tx_spare_info(ring, buf, len, h->kinfo.num_tqps, &pos);

	return 0;
}

static const struct hns3_dbg_item queue_map_items[] = {
	{ "local_queue_id", 2 },
	{ "global_queue_id", 2 },
	{ "vector_id", 2 },
};

static int hns3_dbg_queue_map(struct hnae3_handle *h, char *buf, int len)
{
	char data_str[ARRAY_SIZE(queue_map_items)][HNS3_DBG_DATA_STR_LEN];
	char *result[ARRAY_SIZE(queue_map_items)];
	struct hns3_nic_priv *priv = h->priv;
	char content[HNS3_DBG_INFO_LEN];
	int pos = 0;
	int j;
	u32 i;

	if (!h->ae_algo->ops->get_global_queue_id)
		return -EOPNOTSUPP;

	for (i = 0; i < ARRAY_SIZE(queue_map_items); i++)
		result[i] = &data_str[i][0];

	hns3_dbg_fill_content(content, sizeof(content), queue_map_items,
			      NULL, ARRAY_SIZE(queue_map_items));
	pos += scnprintf(buf + pos, len - pos, "%s", content);
	for (i = 0; i < h->kinfo.num_tqps; i++) {
		if (!priv->ring || !priv->ring[i].tqp_vector)
			continue;
		j = 0;
		sprintf(result[j++], "%u", i);
		sprintf(result[j++], "%u",
			h->ae_algo->ops->get_global_queue_id(h, i));
		sprintf(result[j++], "%d",
			priv->ring[i].tqp_vector->vector_irq);
		hns3_dbg_fill_content(content, sizeof(content), queue_map_items,
				      (const char **)result,
				      ARRAY_SIZE(queue_map_items));
		pos += scnprintf(buf + pos, len - pos, "%s", content);
	}

	return 0;
}

static const struct hns3_dbg_item rx_bd_info_items[] = {
	{ "BD_IDX", 3 },
	{ "L234_INFO", 2 },
	{ "PKT_LEN", 3 },
	{ "SIZE", 4 },
	{ "RSS_HASH", 4 },
	{ "FD_ID", 2 },
	{ "VLAN_TAG", 2 },
	{ "O_DM_VLAN_ID_FB", 2 },
	{ "OT_VLAN_TAG", 2 },
	{ "BD_BASE_INFO", 2 },
	{ "PTYPE", 2 },
	{ "HW_CSUM", 2 },
};

static void hns3_dump_rx_bd_info(struct hns3_nic_priv *priv,
				 struct hns3_desc *desc, char **result, int idx)
{
	unsigned int j = 0;

	sprintf(result[j++], "%d", idx);
	sprintf(result[j++], "%#x", le32_to_cpu(desc->rx.l234_info));
	sprintf(result[j++], "%u", le16_to_cpu(desc->rx.pkt_len));
	sprintf(result[j++], "%u", le16_to_cpu(desc->rx.size));
	sprintf(result[j++], "%#x", le32_to_cpu(desc->rx.rss_hash));
	sprintf(result[j++], "%u", le16_to_cpu(desc->rx.fd_id));
	sprintf(result[j++], "%u", le16_to_cpu(desc->rx.vlan_tag));
	sprintf(result[j++], "%u", le16_to_cpu(desc->rx.o_dm_vlan_id_fb));
	sprintf(result[j++], "%u", le16_to_cpu(desc->rx.ot_vlan_tag));
	sprintf(result[j++], "%#x", le32_to_cpu(desc->rx.bd_base_info));
	if (test_bit(HNS3_NIC_STATE_RXD_ADV_LAYOUT_ENABLE, &priv->state)) {
		u32 ol_info = le32_to_cpu(desc->rx.ol_info);

		sprintf(result[j++], "%5lu", hnae3_get_field(ol_info,
							     HNS3_RXD_PTYPE_M,
							     HNS3_RXD_PTYPE_S));
		sprintf(result[j++], "%7u", le16_to_cpu(desc->csum));
	} else {
		sprintf(result[j++], "NA");
		sprintf(result[j++], "NA");
	}
}

static int hns3_dbg_rx_bd_info(struct hns3_dbg_data *d, char *buf, int len)
{
	char data_str[ARRAY_SIZE(rx_bd_info_items)][HNS3_DBG_DATA_STR_LEN];
	struct hns3_nic_priv *priv = d->handle->priv;
	char *result[ARRAY_SIZE(rx_bd_info_items)];
	char content[HNS3_DBG_INFO_LEN];
	struct hns3_enet_ring *ring;
	struct hns3_desc *desc;
	unsigned int i;
	int pos = 0;

	if (d->qid >= d->handle->kinfo.num_tqps) {
		dev_err(&d->handle->pdev->dev,
			"queue%u is not in use\n", d->qid);
		return -EINVAL;
	}

	for (i = 0; i < ARRAY_SIZE(rx_bd_info_items); i++)
		result[i] = &data_str[i][0];

	pos += scnprintf(buf + pos, len - pos,
			  "Queue %u rx bd info:\n", d->qid);
	hns3_dbg_fill_content(content, sizeof(content), rx_bd_info_items,
			      NULL, ARRAY_SIZE(rx_bd_info_items));
	pos += scnprintf(buf + pos, len - pos, "%s", content);

	ring = &priv->ring[d->qid + d->handle->kinfo.num_tqps];
	for (i = 0; i < ring->desc_num; i++) {
		desc = &ring->desc[i];

		hns3_dump_rx_bd_info(priv, desc, result, i);
		hns3_dbg_fill_content(content, sizeof(content),
				      rx_bd_info_items, (const char **)result,
				      ARRAY_SIZE(rx_bd_info_items));
		pos += scnprintf(buf + pos, len - pos, "%s", content);
	}

	return 0;
}

static const struct hns3_dbg_item tx_bd_info_items[] = {
	{ "BD_IDX", 2 },
	{ "ADDRESS", 13 },
	{ "VLAN_TAG", 2 },
	{ "SIZE", 2 },
	{ "T_CS_VLAN_TSO", 2 },
	{ "OT_VLAN_TAG", 3 },
	{ "TV", 5 },
	{ "OLT_VLAN_LEN", 2 },
	{ "PAYLEN_OL4CS", 2 },
	{ "BD_FE_SC_VLD", 2 },
	{ "MSS_HW_CSUM", 0 },
};

static void hns3_dump_tx_bd_info(struct hns3_desc *desc, char **result, int idx)
{
	unsigned int j = 0;

	sprintf(result[j++], "%d", idx);
	sprintf(result[j++], "%#llx", le64_to_cpu(desc->addr));
	sprintf(result[j++], "%u", le16_to_cpu(desc->tx.vlan_tag));
	sprintf(result[j++], "%u", le16_to_cpu(desc->tx.send_size));
	sprintf(result[j++], "%#x",
		le32_to_cpu(desc->tx.type_cs_vlan_tso_len));
	sprintf(result[j++], "%u", le16_to_cpu(desc->tx.outer_vlan_tag));
	sprintf(result[j++], "%u", le16_to_cpu(desc->tx.tv));
	sprintf(result[j++], "%u",
		le32_to_cpu(desc->tx.ol_type_vlan_len_msec));
	sprintf(result[j++], "%#x", le32_to_cpu(desc->tx.paylen_ol4cs));
	sprintf(result[j++], "%#x", le16_to_cpu(desc->tx.bdtp_fe_sc_vld_ra_ri));
	sprintf(result[j++], "%u", le16_to_cpu(desc->tx.mss_hw_csum));
}

static int hns3_dbg_tx_bd_info(struct hns3_dbg_data *d, char *buf, int len)
{
	char data_str[ARRAY_SIZE(tx_bd_info_items)][HNS3_DBG_DATA_STR_LEN];
	struct hns3_nic_priv *priv = d->handle->priv;
	char *result[ARRAY_SIZE(tx_bd_info_items)];
	char content[HNS3_DBG_INFO_LEN];
	struct hns3_enet_ring *ring;
	struct hns3_desc *desc;
	unsigned int i;
	int pos = 0;

	if (d->qid >= d->handle->kinfo.num_tqps) {
		dev_err(&d->handle->pdev->dev,
			"queue%u is not in use\n", d->qid);
		return -EINVAL;
	}

	for (i = 0; i < ARRAY_SIZE(tx_bd_info_items); i++)
		result[i] = &data_str[i][0];

	pos += scnprintf(buf + pos, len - pos,
			  "Queue %u tx bd info:\n", d->qid);
	hns3_dbg_fill_content(content, sizeof(content), tx_bd_info_items,
			      NULL, ARRAY_SIZE(tx_bd_info_items));
	pos += scnprintf(buf + pos, len - pos, "%s", content);

	ring = &priv->ring[d->qid];
	for (i = 0; i < ring->desc_num; i++) {
		desc = &ring->desc[i];

		hns3_dump_tx_bd_info(desc, result, i);
		hns3_dbg_fill_content(content, sizeof(content),
				      tx_bd_info_items, (const char **)result,
				      ARRAY_SIZE(tx_bd_info_items));
		pos += scnprintf(buf + pos, len - pos, "%s", content);
	}

	return 0;
}

static void
hns3_dbg_dev_caps(struct hnae3_handle *h, char *buf, int len, int *pos)
{
	struct hnae3_ae_dev *ae_dev = pci_get_drvdata(h->pdev);
	const char * const str[] = {"no", "yes"};
	unsigned long *caps = ae_dev->caps;
	u32 i, state;

	*pos += scnprintf(buf + *pos, len - *pos, "dev capability:\n");

	for (i = 0; i < ARRAY_SIZE(hns3_dbg_cap); i++) {
		state = test_bit(hns3_dbg_cap[i].cap_bit, caps);
		*pos += scnprintf(buf + *pos, len - *pos, "%s: %s\n",
				  hns3_dbg_cap[i].name, str[state]);
	}

	*pos += scnprintf(buf + *pos, len - *pos, "\n");
}

static void
hns3_dbg_dev_specs(struct hnae3_handle *h, char *buf, int len, int *pos)
{
	struct hnae3_ae_dev *ae_dev = pci_get_drvdata(h->pdev);
	struct hnae3_dev_specs *dev_specs = &ae_dev->dev_specs;
	struct hnae3_knic_private_info *kinfo = &h->kinfo;
	struct net_device *dev = kinfo->netdev;

	*pos += scnprintf(buf + *pos, len - *pos, "dev_spec:\n");
	*pos += scnprintf(buf + *pos, len - *pos, "MAC entry num: %u\n",
			  dev_specs->mac_entry_num);
	*pos += scnprintf(buf + *pos, len - *pos, "MNG entry num: %u\n",
			  dev_specs->mng_entry_num);
	*pos += scnprintf(buf + *pos, len - *pos, "MAX non tso bd num: %u\n",
			  dev_specs->max_non_tso_bd_num);
	*pos += scnprintf(buf + *pos, len - *pos, "RSS ind tbl size: %u\n",
			  dev_specs->rss_ind_tbl_size);
	*pos += scnprintf(buf + *pos, len - *pos, "RSS key size: %u\n",
			  dev_specs->rss_key_size);
	*pos += scnprintf(buf + *pos, len - *pos, "RSS size: %u\n",
			  kinfo->rss_size);
	*pos += scnprintf(buf + *pos, len - *pos, "Allocated RSS size: %u\n",
			  kinfo->req_rss_size);
	*pos += scnprintf(buf + *pos, len - *pos,
			  "Task queue pairs numbers: %u\n",
			  kinfo->num_tqps);
	*pos += scnprintf(buf + *pos, len - *pos, "RX buffer length: %u\n",
			  kinfo->rx_buf_len);
	*pos += scnprintf(buf + *pos, len - *pos, "Desc num per TX queue: %u\n",
			  kinfo->num_tx_desc);
	*pos += scnprintf(buf + *pos, len - *pos, "Desc num per RX queue: %u\n",
			  kinfo->num_rx_desc);
	*pos += scnprintf(buf + *pos, len - *pos,
			  "Total number of enabled TCs: %u\n",
			  kinfo->tc_info.num_tc);
	*pos += scnprintf(buf + *pos, len - *pos, "MAX INT QL: %u\n",
			  dev_specs->int_ql_max);
	*pos += scnprintf(buf + *pos, len - *pos, "MAX INT GL: %u\n",
			  dev_specs->max_int_gl);
	*pos += scnprintf(buf + *pos, len - *pos, "MAX TM RATE: %u\n",
			  dev_specs->max_tm_rate);
	*pos += scnprintf(buf + *pos, len - *pos, "MAX QSET number: %u\n",
			  dev_specs->max_qset_num);
	*pos += scnprintf(buf + *pos, len - *pos, "umv size: %u\n",
			  dev_specs->umv_size);
	*pos += scnprintf(buf + *pos, len - *pos, "mc mac size: %u\n",
			  dev_specs->mc_mac_size);
	*pos += scnprintf(buf + *pos, len - *pos, "MAC statistics number: %u\n",
			  dev_specs->mac_stats_num);
	*pos += scnprintf(buf + *pos, len - *pos,
			  "TX timeout threshold: %d seconds\n",
			  dev->watchdog_timeo / HZ);
}

static int hns3_dbg_dev_info(struct hnae3_handle *h, char *buf, int len)
{
	int pos = 0;

	hns3_dbg_dev_caps(h, buf, len, &pos);

	hns3_dbg_dev_specs(h, buf, len, &pos);

	return 0;
}

static const struct hns3_dbg_item page_pool_info_items[] = {
	{ "QUEUE_ID", 2 },
	{ "ALLOCATE_CNT", 2 },
	{ "FREE_CNT", 6 },
	{ "POOL_SIZE(PAGE_NUM)", 2 },
	{ "ORDER", 2 },
	{ "NUMA_ID", 2 },
	{ "MAX_LEN", 2 },
};

static void hns3_dump_page_pool_info(struct hns3_enet_ring *ring,
				     char **result, u32 index)
{
	u32 j = 0;

	sprintf(result[j++], "%u", index);
	sprintf(result[j++], "%u",
		READ_ONCE(ring->page_pool->pages_state_hold_cnt));
	sprintf(result[j++], "%d",
		atomic_read(&ring->page_pool->pages_state_release_cnt));
	sprintf(result[j++], "%u", ring->page_pool->p.pool_size);
	sprintf(result[j++], "%u", ring->page_pool->p.order);
	sprintf(result[j++], "%d", ring->page_pool->p.nid);
	sprintf(result[j++], "%uK", ring->page_pool->p.max_len / 1024);
}

static int
hns3_dbg_page_pool_info(struct hnae3_handle *h, char *buf, int len)
{
	char data_str[ARRAY_SIZE(page_pool_info_items)][HNS3_DBG_DATA_STR_LEN];
	char *result[ARRAY_SIZE(page_pool_info_items)];
	struct hns3_nic_priv *priv = h->priv;
	char content[HNS3_DBG_INFO_LEN];
	struct hns3_enet_ring *ring;
	int pos = 0;
	u32 i;

	if (!priv->ring) {
		dev_err(&h->pdev->dev, "priv->ring is NULL\n");
		return -EFAULT;
	}

	if (!priv->ring[h->kinfo.num_tqps].page_pool) {
		dev_err(&h->pdev->dev, "page pool is not initialized\n");
		return -EFAULT;
	}

	for (i = 0; i < ARRAY_SIZE(page_pool_info_items); i++)
		result[i] = &data_str[i][0];

	hns3_dbg_fill_content(content, sizeof(content), page_pool_info_items,
			      NULL, ARRAY_SIZE(page_pool_info_items));
	pos += scnprintf(buf + pos, len - pos, "%s", content);
	for (i = 0; i < h->kinfo.num_tqps; i++) {
		if (!test_bit(HNS3_NIC_STATE_INITED, &priv->state) ||
		    test_bit(HNS3_NIC_STATE_RESETTING, &priv->state))
			return -EPERM;
		ring = &priv->ring[(u32)(i + h->kinfo.num_tqps)];
		hns3_dump_page_pool_info(ring, result, i);
		hns3_dbg_fill_content(content, sizeof(content),
				      page_pool_info_items,
				      (const char **)result,
				      ARRAY_SIZE(page_pool_info_items));
		pos += scnprintf(buf + pos, len - pos, "%s", content);
	}

	return 0;
}

static int hns3_dbg_get_cmd_index(struct hns3_dbg_data *dbg_data, u32 *index)
{
	u32 i;

	for (i = 0; i < ARRAY_SIZE(hns3_dbg_cmd); i++) {
		if (hns3_dbg_cmd[i].cmd == dbg_data->cmd) {
			*index = i;
			return 0;
		}
	}

	dev_err(&dbg_data->handle->pdev->dev, "unknown command(%d)\n",
		dbg_data->cmd);
	return -EINVAL;
}

static const struct hns3_dbg_func hns3_dbg_cmd_func[] = {
	{
		.cmd = HNAE3_DBG_CMD_QUEUE_MAP,
		.dbg_dump = hns3_dbg_queue_map,
	},
	{
		.cmd = HNAE3_DBG_CMD_DEV_INFO,
		.dbg_dump = hns3_dbg_dev_info,
	},
	{
		.cmd = HNAE3_DBG_CMD_TX_BD,
		.dbg_dump_bd = hns3_dbg_tx_bd_info,
	},
	{
		.cmd = HNAE3_DBG_CMD_RX_BD,
		.dbg_dump_bd = hns3_dbg_rx_bd_info,
	},
	{
		.cmd = HNAE3_DBG_CMD_RX_QUEUE_INFO,
		.dbg_dump = hns3_dbg_rx_queue_info,
	},
	{
		.cmd = HNAE3_DBG_CMD_TX_QUEUE_INFO,
		.dbg_dump = hns3_dbg_tx_queue_info,
	},
	{
		.cmd = HNAE3_DBG_CMD_PAGE_POOL_INFO,
		.dbg_dump = hns3_dbg_page_pool_info,
	},
	{
		.cmd = HNAE3_DBG_CMD_COAL_INFO,
		.dbg_dump = hns3_dbg_coal_info,
	},
};

static int hns3_dbg_read_cmd(struct hns3_dbg_data *dbg_data,
			     enum hnae3_dbg_cmd cmd, char *buf, int len)
{
	const struct hnae3_ae_ops *ops = dbg_data->handle->ae_algo->ops;
	const struct hns3_dbg_func *cmd_func;
	u32 i;

	for (i = 0; i < ARRAY_SIZE(hns3_dbg_cmd_func); i++) {
		if (cmd == hns3_dbg_cmd_func[i].cmd) {
			cmd_func = &hns3_dbg_cmd_func[i];
			if (cmd_func->dbg_dump)
				return cmd_func->dbg_dump(dbg_data->handle, buf,
							  len);
			else
				return cmd_func->dbg_dump_bd(dbg_data, buf,
							     len);
		}
	}

	if (!ops->dbg_read_cmd)
		return -EOPNOTSUPP;

	return ops->dbg_read_cmd(dbg_data->handle, cmd, buf, len);
}

static ssize_t hns3_dbg_read(struct file *filp, char __user *buffer,
			     size_t count, loff_t *ppos)
{
	struct hns3_dbg_data *dbg_data = filp->private_data;
	struct hnae3_handle *handle = dbg_data->handle;
	struct hns3_nic_priv *priv = handle->priv;
	ssize_t size = 0;
	char **save_buf;
	char *read_buf;
	u32 index;
	int ret;

	ret = hns3_dbg_get_cmd_index(dbg_data, &index);
	if (ret)
		return ret;

	mutex_lock(&handle->dbgfs_lock);
	save_buf = &handle->dbgfs_buf[index];

	if (!test_bit(HNS3_NIC_STATE_INITED, &priv->state) ||
	    test_bit(HNS3_NIC_STATE_RESETTING, &priv->state)) {
		ret = -EBUSY;
		goto out;
	}

	if (*save_buf) {
		read_buf = *save_buf;
	} else {
		read_buf = kvzalloc(hns3_dbg_cmd[index].buf_len, GFP_KERNEL);
		if (!read_buf) {
			ret = -ENOMEM;
			goto out;
		}

		/* save the buffer addr until the last read operation */
		*save_buf = read_buf;

		/* get data ready for the first time to read */
		ret = hns3_dbg_read_cmd(dbg_data, hns3_dbg_cmd[index].cmd,
					read_buf, hns3_dbg_cmd[index].buf_len);
		if (ret)
			goto out;
	}

	size = simple_read_from_buffer(buffer, count, ppos, read_buf,
				       strlen(read_buf));
	if (size > 0) {
		mutex_unlock(&handle->dbgfs_lock);
		return size;
	}

out:
	/* free the buffer for the last read operation */
	if (*save_buf) {
		kvfree(*save_buf);
		*save_buf = NULL;
	}

	mutex_unlock(&handle->dbgfs_lock);
	return ret;
}

static const struct file_operations hns3_dbg_fops = {
	.owner = THIS_MODULE,
	.open  = simple_open,
	.read  = hns3_dbg_read,
};

static int hns3_dbg_bd_file_init(struct hnae3_handle *handle, u32 cmd)
{
	struct dentry *entry_dir;
	struct hns3_dbg_data *data;
	u16 max_queue_num;
	unsigned int i;

	entry_dir = hns3_dbg_dentry[hns3_dbg_cmd[cmd].dentry].dentry;
	max_queue_num = hns3_get_max_available_channels(handle);
	data = devm_kzalloc(&handle->pdev->dev, max_queue_num * sizeof(*data),
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
				    &hns3_dbg_fops);
	}

	return 0;
}

static int
hns3_dbg_common_file_init(struct hnae3_handle *handle, u32 cmd)
{
	struct hns3_dbg_data *data;
	struct dentry *entry_dir;

	data = devm_kzalloc(&handle->pdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->handle = handle;
	data->cmd = hns3_dbg_cmd[cmd].cmd;
	entry_dir = hns3_dbg_dentry[hns3_dbg_cmd[cmd].dentry].dentry;
	debugfs_create_file(hns3_dbg_cmd[cmd].name, 0400, entry_dir,
			    data, &hns3_dbg_fops);

	return 0;
}

int hns3_dbg_init(struct hnae3_handle *handle)
{
	struct hnae3_ae_dev *ae_dev = pci_get_drvdata(handle->pdev);
	const char *name = pci_name(handle->pdev);
	int ret;
	u32 i;

	handle->dbgfs_buf = devm_kcalloc(&handle->pdev->dev,
					 ARRAY_SIZE(hns3_dbg_cmd),
					 sizeof(*handle->dbgfs_buf),
					 GFP_KERNEL);
	if (!handle->dbgfs_buf)
		return -ENOMEM;

	hns3_dbg_dentry[HNS3_DBG_DENTRY_COMMON].dentry =
				debugfs_create_dir(name, hns3_dbgfs_root);
	handle->hnae3_dbgfs = hns3_dbg_dentry[HNS3_DBG_DENTRY_COMMON].dentry;

	for (i = 0; i < HNS3_DBG_DENTRY_COMMON; i++)
		hns3_dbg_dentry[i].dentry =
			debugfs_create_dir(hns3_dbg_dentry[i].name,
					   handle->hnae3_dbgfs);

	mutex_init(&handle->dbgfs_lock);

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
	mutex_destroy(&handle->dbgfs_lock);
	return ret;
}

void hns3_dbg_uninit(struct hnae3_handle *handle)
{
	u32 i;

	debugfs_remove_recursive(handle->hnae3_dbgfs);
	handle->hnae3_dbgfs = NULL;

	for (i = 0; i < ARRAY_SIZE(hns3_dbg_cmd); i++)
		if (handle->dbgfs_buf[i]) {
			kvfree(handle->dbgfs_buf[i]);
			handle->dbgfs_buf[i] = NULL;
		}

	mutex_destroy(&handle->dbgfs_lock);
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
