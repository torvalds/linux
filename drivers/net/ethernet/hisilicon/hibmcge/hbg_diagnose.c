// SPDX-License-Identifier: GPL-2.0+
// Copyright (c) 2025 Hisilicon Limited.

#include <linux/iopoll.h>
#include <linux/phy.h>
#include "hbg_common.h"
#include "hbg_ethtool.h"
#include "hbg_hw.h"
#include "hbg_diagnose.h"

#define HBG_MSG_DATA_MAX_NUM	64

struct hbg_diagnose_message {
	u32 opcode;
	u32 status;
	u32 data_num;
	struct hbg_priv *priv;

	u32 data[HBG_MSG_DATA_MAX_NUM];
};

#define HBG_HW_PUSH_WAIT_TIMEOUT_US	(2 * 1000 * 1000)
#define HBG_HW_PUSH_WAIT_INTERVAL_US	(1 * 1000)

enum hbg_push_cmd {
	HBG_PUSH_CMD_IRQ = 0,
	HBG_PUSH_CMD_STATS,
	HBG_PUSH_CMD_LINK,
};

struct hbg_push_stats_info {
	/* id is used to match the name of the current stats item.
	 * and is used for pretty print on BMC
	 */
	u32 id;
	u64 offset;
};

struct hbg_push_irq_info {
	/* id is used to match the name of the current irq.
	 * and is used for pretty print on BMC
	 */
	u32 id;
	u32 mask;
};

#define HBG_PUSH_IRQ_I(name, id) {id, HBG_INT_MSK_##name##_B}
static const struct hbg_push_irq_info hbg_push_irq_list[] = {
	HBG_PUSH_IRQ_I(RX, 0),
	HBG_PUSH_IRQ_I(TX, 1),
	HBG_PUSH_IRQ_I(TX_PKT_CPL, 2),
	HBG_PUSH_IRQ_I(MAC_MII_FIFO_ERR, 3),
	HBG_PUSH_IRQ_I(MAC_PCS_RX_FIFO_ERR, 4),
	HBG_PUSH_IRQ_I(MAC_PCS_TX_FIFO_ERR, 5),
	HBG_PUSH_IRQ_I(MAC_APP_RX_FIFO_ERR, 6),
	HBG_PUSH_IRQ_I(MAC_APP_TX_FIFO_ERR, 7),
	HBG_PUSH_IRQ_I(SRAM_PARITY_ERR, 8),
	HBG_PUSH_IRQ_I(TX_AHB_ERR, 9),
	HBG_PUSH_IRQ_I(RX_BUF_AVL, 10),
	HBG_PUSH_IRQ_I(REL_BUF_ERR, 11),
	HBG_PUSH_IRQ_I(TXCFG_AVL, 12),
	HBG_PUSH_IRQ_I(TX_DROP, 13),
	HBG_PUSH_IRQ_I(RX_DROP, 14),
	HBG_PUSH_IRQ_I(RX_AHB_ERR, 15),
	HBG_PUSH_IRQ_I(MAC_FIFO_ERR, 16),
	HBG_PUSH_IRQ_I(RBREQ_ERR, 17),
	HBG_PUSH_IRQ_I(WE_ERR, 18),
};

#define HBG_PUSH_STATS_I(name, id) {id, HBG_STATS_FIELD_OFF(name)}
static const struct hbg_push_stats_info hbg_push_stats_list[] = {
	HBG_PUSH_STATS_I(rx_desc_drop, 0),
	HBG_PUSH_STATS_I(rx_desc_l2_err_cnt, 1),
	HBG_PUSH_STATS_I(rx_desc_pkt_len_err_cnt, 2),
	HBG_PUSH_STATS_I(rx_desc_l3_wrong_head_cnt, 3),
	HBG_PUSH_STATS_I(rx_desc_l3_csum_err_cnt, 4),
	HBG_PUSH_STATS_I(rx_desc_l3_len_err_cnt, 5),
	HBG_PUSH_STATS_I(rx_desc_l3_zero_ttl_cnt, 6),
	HBG_PUSH_STATS_I(rx_desc_l3_other_cnt, 7),
	HBG_PUSH_STATS_I(rx_desc_l4_err_cnt, 8),
	HBG_PUSH_STATS_I(rx_desc_l4_wrong_head_cnt, 9),
	HBG_PUSH_STATS_I(rx_desc_l4_len_err_cnt, 10),
	HBG_PUSH_STATS_I(rx_desc_l4_csum_err_cnt, 11),
	HBG_PUSH_STATS_I(rx_desc_l4_zero_port_num_cnt, 12),
	HBG_PUSH_STATS_I(rx_desc_l4_other_cnt, 13),
	HBG_PUSH_STATS_I(rx_desc_frag_cnt, 14),
	HBG_PUSH_STATS_I(rx_desc_ip_ver_err_cnt, 15),
	HBG_PUSH_STATS_I(rx_desc_ipv4_pkt_cnt, 16),
	HBG_PUSH_STATS_I(rx_desc_ipv6_pkt_cnt, 17),
	HBG_PUSH_STATS_I(rx_desc_no_ip_pkt_cnt, 18),
	HBG_PUSH_STATS_I(rx_desc_ip_pkt_cnt, 19),
	HBG_PUSH_STATS_I(rx_desc_tcp_pkt_cnt, 20),
	HBG_PUSH_STATS_I(rx_desc_udp_pkt_cnt, 21),
	HBG_PUSH_STATS_I(rx_desc_vlan_pkt_cnt, 22),
	HBG_PUSH_STATS_I(rx_desc_icmp_pkt_cnt, 23),
	HBG_PUSH_STATS_I(rx_desc_arp_pkt_cnt, 24),
	HBG_PUSH_STATS_I(rx_desc_rarp_pkt_cnt, 25),
	HBG_PUSH_STATS_I(rx_desc_multicast_pkt_cnt, 26),
	HBG_PUSH_STATS_I(rx_desc_broadcast_pkt_cnt, 27),
	HBG_PUSH_STATS_I(rx_desc_ipsec_pkt_cnt, 28),
	HBG_PUSH_STATS_I(rx_desc_ip_opt_pkt_cnt, 29),
	HBG_PUSH_STATS_I(rx_desc_key_not_match_cnt, 30),
	HBG_PUSH_STATS_I(rx_octets_total_ok_cnt, 31),
	HBG_PUSH_STATS_I(rx_uc_pkt_cnt, 32),
	HBG_PUSH_STATS_I(rx_mc_pkt_cnt, 33),
	HBG_PUSH_STATS_I(rx_bc_pkt_cnt, 34),
	HBG_PUSH_STATS_I(rx_vlan_pkt_cnt, 35),
	HBG_PUSH_STATS_I(rx_octets_bad_cnt, 36),
	HBG_PUSH_STATS_I(rx_octets_total_filt_cnt, 37),
	HBG_PUSH_STATS_I(rx_filt_pkt_cnt, 38),
	HBG_PUSH_STATS_I(rx_trans_pkt_cnt, 39),
	HBG_PUSH_STATS_I(rx_framesize_64, 40),
	HBG_PUSH_STATS_I(rx_framesize_65_127, 41),
	HBG_PUSH_STATS_I(rx_framesize_128_255, 42),
	HBG_PUSH_STATS_I(rx_framesize_256_511, 43),
	HBG_PUSH_STATS_I(rx_framesize_512_1023, 44),
	HBG_PUSH_STATS_I(rx_framesize_1024_1518, 45),
	HBG_PUSH_STATS_I(rx_framesize_bt_1518, 46),
	HBG_PUSH_STATS_I(rx_fcs_error_cnt, 47),
	HBG_PUSH_STATS_I(rx_data_error_cnt, 48),
	HBG_PUSH_STATS_I(rx_align_error_cnt, 49),
	HBG_PUSH_STATS_I(rx_frame_long_err_cnt, 50),
	HBG_PUSH_STATS_I(rx_jabber_err_cnt, 51),
	HBG_PUSH_STATS_I(rx_pause_macctl_frame_cnt, 52),
	HBG_PUSH_STATS_I(rx_unknown_macctl_frame_cnt, 53),
	HBG_PUSH_STATS_I(rx_frame_very_long_err_cnt, 54),
	HBG_PUSH_STATS_I(rx_frame_runt_err_cnt, 55),
	HBG_PUSH_STATS_I(rx_frame_short_err_cnt, 56),
	HBG_PUSH_STATS_I(rx_overflow_cnt, 57),
	HBG_PUSH_STATS_I(rx_bufrq_err_cnt, 58),
	HBG_PUSH_STATS_I(rx_we_err_cnt, 59),
	HBG_PUSH_STATS_I(rx_overrun_cnt, 60),
	HBG_PUSH_STATS_I(rx_lengthfield_err_cnt, 61),
	HBG_PUSH_STATS_I(rx_fail_comma_cnt, 62),
	HBG_PUSH_STATS_I(rx_dma_err_cnt, 63),
	HBG_PUSH_STATS_I(rx_fifo_less_empty_thrsld_cnt, 64),
	HBG_PUSH_STATS_I(tx_octets_total_ok_cnt, 65),
	HBG_PUSH_STATS_I(tx_uc_pkt_cnt, 66),
	HBG_PUSH_STATS_I(tx_mc_pkt_cnt, 67),
	HBG_PUSH_STATS_I(tx_bc_pkt_cnt, 68),
	HBG_PUSH_STATS_I(tx_vlan_pkt_cnt, 69),
	HBG_PUSH_STATS_I(tx_octets_bad_cnt, 70),
	HBG_PUSH_STATS_I(tx_trans_pkt_cnt, 71),
	HBG_PUSH_STATS_I(tx_pause_frame_cnt, 72),
	HBG_PUSH_STATS_I(tx_framesize_64, 73),
	HBG_PUSH_STATS_I(tx_framesize_65_127, 74),
	HBG_PUSH_STATS_I(tx_framesize_128_255, 75),
	HBG_PUSH_STATS_I(tx_framesize_256_511, 76),
	HBG_PUSH_STATS_I(tx_framesize_512_1023, 77),
	HBG_PUSH_STATS_I(tx_framesize_1024_1518, 78),
	HBG_PUSH_STATS_I(tx_framesize_bt_1518, 79),
	HBG_PUSH_STATS_I(tx_underrun_err_cnt, 80),
	HBG_PUSH_STATS_I(tx_add_cs_fail_cnt, 81),
	HBG_PUSH_STATS_I(tx_bufrl_err_cnt, 82),
	HBG_PUSH_STATS_I(tx_crc_err_cnt, 83),
	HBG_PUSH_STATS_I(tx_drop_cnt, 84),
	HBG_PUSH_STATS_I(tx_excessive_length_drop_cnt, 85),
	HBG_PUSH_STATS_I(tx_dma_err_cnt, 86),
	HBG_PUSH_STATS_I(reset_fail_cnt, 87),
};

static int hbg_push_msg_send(struct hbg_priv *priv,
			     struct hbg_diagnose_message *msg)
{
	u32 header = 0;
	u32 i;

	if (msg->data_num == 0)
		return 0;

	for (i = 0; i < msg->data_num && i < HBG_MSG_DATA_MAX_NUM; i++)
		hbg_reg_write(priv,
			      HBG_REG_MSG_DATA_BASE_ADDR + i * sizeof(u32),
			      msg->data[i]);

	hbg_field_modify(header, HBG_REG_MSG_HEADER_OPCODE_M, msg->opcode);
	hbg_field_modify(header, HBG_REG_MSG_HEADER_DATA_NUM_M,  msg->data_num);
	hbg_field_modify(header, HBG_REG_MSG_HEADER_RESP_CODE_M, ETIMEDOUT);

	/* start status */
	hbg_field_modify(header, HBG_REG_MSG_HEADER_STATUS_M, 1);

	/* write header msg to start push */
	hbg_reg_write(priv, HBG_REG_MSG_HEADER_ADDR, header);

	/* wait done */
	readl_poll_timeout(priv->io_base + HBG_REG_MSG_HEADER_ADDR, header,
			   !FIELD_GET(HBG_REG_MSG_HEADER_STATUS_M, header),
			   HBG_HW_PUSH_WAIT_INTERVAL_US,
			   HBG_HW_PUSH_WAIT_TIMEOUT_US);

	msg->status = FIELD_GET(HBG_REG_MSG_HEADER_STATUS_M, header);
	return -(int)FIELD_GET(HBG_REG_MSG_HEADER_RESP_CODE_M, header);
}

static int hbg_push_data(struct hbg_priv *priv,
			 u32 opcode, u32 *data, u32 data_num)
{
	struct hbg_diagnose_message msg = {0};
	u32 data_left_num;
	u32 i, j;
	int ret;

	msg.priv = priv;
	msg.opcode = opcode;
	for (i = 0; i < data_num / HBG_MSG_DATA_MAX_NUM + 1; i++) {
		if (i * HBG_MSG_DATA_MAX_NUM >= data_num)
			break;

		data_left_num = data_num - i * HBG_MSG_DATA_MAX_NUM;
		for (j = 0; j < data_left_num && j < HBG_MSG_DATA_MAX_NUM; j++)
			msg.data[j] = data[i * HBG_MSG_DATA_MAX_NUM + j];

		msg.data_num = j;
		ret = hbg_push_msg_send(priv, &msg);
		if (ret)
			return ret;
	}

	return 0;
}

static int hbg_push_data_u64(struct hbg_priv *priv, u32 opcode,
			     u64 *data, u32 data_num)
{
	/* The length of u64 is twice that of u32,
	 * the data_num must be multiplied by 2.
	 */
	return hbg_push_data(priv, opcode, (u32 *)data, data_num * 2);
}

static u64 hbg_get_irq_stats(struct hbg_vector *vectors, u32 mask)
{
	u32 i = 0;

	for (i = 0; i < vectors->info_array_len; i++)
		if (vectors->info_array[i].mask == mask)
			return vectors->stats_array[i];

	return 0;
}

static int hbg_push_irq_cnt(struct hbg_priv *priv)
{
	/* An id needs to be added for each data.
	 * Therefore, the data_num must be multiplied by 2.
	 */
	u32 data_num = ARRAY_SIZE(hbg_push_irq_list) * 2;
	struct hbg_vector *vectors = &priv->vectors;
	const struct hbg_push_irq_info *info;
	u32 i, j = 0;
	u64 *data;
	int ret;

	data = kcalloc(data_num, sizeof(u64), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	/* An id needs to be added for each data.
	 * So i + 2 for each loop.
	 */
	for (i = 0; i < data_num; i += 2) {
		info = &hbg_push_irq_list[j++];
		data[i] = info->id;
		data[i + 1] = hbg_get_irq_stats(vectors, info->mask);
	}

	ret = hbg_push_data_u64(priv, HBG_PUSH_CMD_IRQ, data, data_num);
	kfree(data);
	return ret;
}

static int hbg_push_link_status(struct hbg_priv *priv)
{
	u32 link_status[2];

	/* phy link status */
	link_status[0] = priv->mac.phydev->link;
	/* mac link status */
	link_status[1] = hbg_reg_read_field(priv, HBG_REG_AN_NEG_STATE_ADDR,
					    HBG_REG_AN_NEG_STATE_NP_LINK_OK_B);

	return hbg_push_data(priv, HBG_PUSH_CMD_LINK,
			     link_status, ARRAY_SIZE(link_status));
}

static int hbg_push_stats(struct hbg_priv *priv)
{
	/* An id needs to be added for each data.
	 * Therefore, the data_num must be multiplied by 2.
	 */
	u64 data_num = ARRAY_SIZE(hbg_push_stats_list) * 2;
	struct hbg_stats *stats = &priv->stats;
	const struct hbg_push_stats_info *info;
	u32 i, j = 0;
	u64 *data;
	int ret;

	data = kcalloc(data_num, sizeof(u64), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	/* An id needs to be added for each data.
	 * So i + 2 for each loop.
	 */
	for (i = 0; i < data_num; i += 2) {
		info = &hbg_push_stats_list[j++];
		data[i] = info->id;
		data[i + 1] = HBG_STATS_R(stats, info->offset);
	}

	ret = hbg_push_data_u64(priv, HBG_PUSH_CMD_STATS, data, data_num);
	kfree(data);
	return ret;
}

void hbg_diagnose_message_push(struct hbg_priv *priv)
{
	int ret;

	if (test_bit(HBG_NIC_STATE_RESETTING, &priv->state))
		return;

	/* only 1 is the right value */
	if (hbg_reg_read(priv, HBG_REG_PUSH_REQ_ADDR) != 1)
		return;

	ret = hbg_push_irq_cnt(priv);
	if (ret) {
		dev_err(&priv->pdev->dev,
			"failed to push irq cnt, ret = %d\n", ret);
		goto push_done;
	}

	ret = hbg_push_link_status(priv);
	if (ret) {
		dev_err(&priv->pdev->dev,
			"failed to push link status, ret = %d\n", ret);
		goto push_done;
	}

	ret = hbg_push_stats(priv);
	if (ret)
		dev_err(&priv->pdev->dev,
			"failed to push stats, ret = %d\n", ret);

push_done:
	hbg_reg_write(priv, HBG_REG_PUSH_REQ_ADDR, 0);
}
