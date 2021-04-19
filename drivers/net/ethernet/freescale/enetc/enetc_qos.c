// SPDX-License-Identifier: (GPL-2.0+ OR BSD-3-Clause)
/* Copyright 2019 NXP */

#include "enetc.h"

#include <net/pkt_sched.h>
#include <linux/math64.h>
#include <linux/refcount.h>
#include <net/pkt_cls.h>
#include <net/tc_act/tc_gate.h>

static u16 enetc_get_max_gcl_len(struct enetc_hw *hw)
{
	return enetc_rd(hw, ENETC_QBV_PTGCAPR_OFFSET)
		& ENETC_QBV_MAX_GCL_LEN_MASK;
}

void enetc_sched_speed_set(struct enetc_ndev_priv *priv, int speed)
{
	u32 old_speed = priv->speed;
	u32 pspeed;

	if (speed == old_speed)
		return;

	switch (speed) {
	case SPEED_1000:
		pspeed = ENETC_PMR_PSPEED_1000M;
		break;
	case SPEED_2500:
		pspeed = ENETC_PMR_PSPEED_2500M;
		break;
	case SPEED_100:
		pspeed = ENETC_PMR_PSPEED_100M;
		break;
	case SPEED_10:
	default:
		pspeed = ENETC_PMR_PSPEED_10M;
	}

	priv->speed = speed;
	enetc_port_wr(&priv->si->hw, ENETC_PMR,
		      (enetc_port_rd(&priv->si->hw, ENETC_PMR)
		      & (~ENETC_PMR_PSPEED_MASK))
		      | pspeed);
}

static int enetc_setup_taprio(struct net_device *ndev,
			      struct tc_taprio_qopt_offload *admin_conf)
{
	struct enetc_ndev_priv *priv = netdev_priv(ndev);
	struct enetc_cbd cbd = {.cmd = 0};
	struct tgs_gcl_conf *gcl_config;
	struct tgs_gcl_data *gcl_data;
	struct gce *gce;
	dma_addr_t dma;
	u16 data_size;
	u16 gcl_len;
	u32 tge;
	int err;
	int i;

	if (admin_conf->num_entries > enetc_get_max_gcl_len(&priv->si->hw))
		return -EINVAL;
	gcl_len = admin_conf->num_entries;

	tge = enetc_rd(&priv->si->hw, ENETC_QBV_PTGCR_OFFSET);
	if (!admin_conf->enable) {
		enetc_wr(&priv->si->hw,
			 ENETC_QBV_PTGCR_OFFSET,
			 tge & (~ENETC_QBV_TGE));
		return 0;
	}

	if (admin_conf->cycle_time > U32_MAX ||
	    admin_conf->cycle_time_extension > U32_MAX)
		return -EINVAL;

	/* Configure the (administrative) gate control list using the
	 * control BD descriptor.
	 */
	gcl_config = &cbd.gcl_conf;

	data_size = struct_size(gcl_data, entry, gcl_len);
	gcl_data = kzalloc(data_size, __GFP_DMA | GFP_KERNEL);
	if (!gcl_data)
		return -ENOMEM;

	gce = (struct gce *)(gcl_data + 1);

	/* Set all gates open as default */
	gcl_config->atc = 0xff;
	gcl_config->acl_len = cpu_to_le16(gcl_len);

	gcl_data->btl = cpu_to_le32(lower_32_bits(admin_conf->base_time));
	gcl_data->bth = cpu_to_le32(upper_32_bits(admin_conf->base_time));
	gcl_data->ct = cpu_to_le32(admin_conf->cycle_time);
	gcl_data->cte = cpu_to_le32(admin_conf->cycle_time_extension);

	for (i = 0; i < gcl_len; i++) {
		struct tc_taprio_sched_entry *temp_entry;
		struct gce *temp_gce = gce + i;

		temp_entry = &admin_conf->entries[i];

		temp_gce->gate = (u8)temp_entry->gate_mask;
		temp_gce->period = cpu_to_le32(temp_entry->interval);
	}

	cbd.length = cpu_to_le16(data_size);
	cbd.status_flags = 0;

	dma = dma_map_single(&priv->si->pdev->dev, gcl_data,
			     data_size, DMA_TO_DEVICE);
	if (dma_mapping_error(&priv->si->pdev->dev, dma)) {
		netdev_err(priv->si->ndev, "DMA mapping failed!\n");
		kfree(gcl_data);
		return -ENOMEM;
	}

	cbd.addr[0] = cpu_to_le32(lower_32_bits(dma));
	cbd.addr[1] = cpu_to_le32(upper_32_bits(dma));
	cbd.cls = BDCR_CMD_PORT_GCL;
	cbd.status_flags = 0;

	enetc_wr(&priv->si->hw, ENETC_QBV_PTGCR_OFFSET,
		 tge | ENETC_QBV_TGE);

	err = enetc_send_cmd(priv->si, &cbd);
	if (err)
		enetc_wr(&priv->si->hw,
			 ENETC_QBV_PTGCR_OFFSET,
			 tge & (~ENETC_QBV_TGE));

	dma_unmap_single(&priv->si->pdev->dev, dma, data_size, DMA_TO_DEVICE);
	kfree(gcl_data);

	return err;
}

int enetc_setup_tc_taprio(struct net_device *ndev, void *type_data)
{
	struct tc_taprio_qopt_offload *taprio = type_data;
	struct enetc_ndev_priv *priv = netdev_priv(ndev);
	int err;
	int i;

	/* TSD and Qbv are mutually exclusive in hardware */
	for (i = 0; i < priv->num_tx_rings; i++)
		if (priv->tx_ring[i]->tsd_enable)
			return -EBUSY;

	for (i = 0; i < priv->num_tx_rings; i++)
		enetc_set_bdr_prio(&priv->si->hw,
				   priv->tx_ring[i]->index,
				   taprio->enable ? i : 0);

	err = enetc_setup_taprio(ndev, taprio);

	if (err)
		for (i = 0; i < priv->num_tx_rings; i++)
			enetc_set_bdr_prio(&priv->si->hw,
					   priv->tx_ring[i]->index,
					   taprio->enable ? 0 : i);

	return err;
}

static u32 enetc_get_cbs_enable(struct enetc_hw *hw, u8 tc)
{
	return enetc_port_rd(hw, ENETC_PTCCBSR0(tc)) & ENETC_CBSE;
}

static u8 enetc_get_cbs_bw(struct enetc_hw *hw, u8 tc)
{
	return enetc_port_rd(hw, ENETC_PTCCBSR0(tc)) & ENETC_CBS_BW_MASK;
}

int enetc_setup_tc_cbs(struct net_device *ndev, void *type_data)
{
	struct enetc_ndev_priv *priv = netdev_priv(ndev);
	struct tc_cbs_qopt_offload *cbs = type_data;
	u32 port_transmit_rate = priv->speed;
	u8 tc_nums = netdev_get_num_tc(ndev);
	struct enetc_si *si = priv->si;
	u32 hi_credit_bit, hi_credit_reg;
	u32 max_interference_size;
	u32 port_frame_max_size;
	u8 tc = cbs->queue;
	u8 prio_top, prio_next;
	int bw_sum = 0;
	u8 bw;

	prio_top = netdev_get_prio_tc_map(ndev, tc_nums - 1);
	prio_next = netdev_get_prio_tc_map(ndev, tc_nums - 2);

	/* Support highest prio and second prio tc in cbs mode */
	if (tc != prio_top && tc != prio_next)
		return -EOPNOTSUPP;

	if (!cbs->enable) {
		/* Make sure the other TC that are numerically
		 * lower than this TC have been disabled.
		 */
		if (tc == prio_top &&
		    enetc_get_cbs_enable(&si->hw, prio_next)) {
			dev_err(&ndev->dev,
				"Disable TC%d before disable TC%d\n",
				prio_next, tc);
			return -EINVAL;
		}

		enetc_port_wr(&si->hw, ENETC_PTCCBSR1(tc), 0);
		enetc_port_wr(&si->hw, ENETC_PTCCBSR0(tc), 0);

		return 0;
	}

	if (cbs->idleslope - cbs->sendslope != port_transmit_rate * 1000L ||
	    cbs->idleslope < 0 || cbs->sendslope > 0)
		return -EOPNOTSUPP;

	port_frame_max_size = ndev->mtu + VLAN_ETH_HLEN + ETH_FCS_LEN;

	bw = cbs->idleslope / (port_transmit_rate * 10UL);

	/* Make sure the other TC that are numerically
	 * higher than this TC have been enabled.
	 */
	if (tc == prio_next) {
		if (!enetc_get_cbs_enable(&si->hw, prio_top)) {
			dev_err(&ndev->dev,
				"Enable TC%d first before enable TC%d\n",
				prio_top, prio_next);
			return -EINVAL;
		}
		bw_sum += enetc_get_cbs_bw(&si->hw, prio_top);
	}

	if (bw_sum + bw >= 100) {
		dev_err(&ndev->dev,
			"The sum of all CBS Bandwidth can't exceed 100\n");
		return -EINVAL;
	}

	enetc_port_rd(&si->hw, ENETC_PTCMSDUR(tc));

	/* For top prio TC, the max_interfrence_size is maxSizedFrame.
	 *
	 * For next prio TC, the max_interfrence_size is calculated as below:
	 *
	 *      max_interference_size = M0 + Ma + Ra * M0 / (R0 - Ra)
	 *
	 *	- RA: idleSlope for AVB Class A
	 *	- R0: port transmit rate
	 *	- M0: maximum sized frame for the port
	 *	- MA: maximum sized frame for AVB Class A
	 */

	if (tc == prio_top) {
		max_interference_size = port_frame_max_size * 8;
	} else {
		u32 m0, ma, r0, ra;

		m0 = port_frame_max_size * 8;
		ma = enetc_port_rd(&si->hw, ENETC_PTCMSDUR(prio_top)) * 8;
		ra = enetc_get_cbs_bw(&si->hw, prio_top) *
			port_transmit_rate * 10000ULL;
		r0 = port_transmit_rate * 1000000ULL;
		max_interference_size = m0 + ma +
			(u32)div_u64((u64)ra * m0, r0 - ra);
	}

	/* hiCredit bits calculate by:
	 *
	 * maxSizedFrame * (idleSlope/portTxRate)
	 */
	hi_credit_bit = max_interference_size * bw / 100;

	/* hiCredit bits to hiCredit register need to calculated as:
	 *
	 * (enetClockFrequency / portTransmitRate) * 100
	 */
	hi_credit_reg = (u32)div_u64((ENETC_CLK * 100ULL) * hi_credit_bit,
				     port_transmit_rate * 1000000ULL);

	enetc_port_wr(&si->hw, ENETC_PTCCBSR1(tc), hi_credit_reg);

	/* Set bw register and enable this traffic class */
	enetc_port_wr(&si->hw, ENETC_PTCCBSR0(tc), bw | ENETC_CBSE);

	return 0;
}

int enetc_setup_tc_txtime(struct net_device *ndev, void *type_data)
{
	struct enetc_ndev_priv *priv = netdev_priv(ndev);
	struct tc_etf_qopt_offload *qopt = type_data;
	u8 tc_nums = netdev_get_num_tc(ndev);
	int tc;

	if (!tc_nums)
		return -EOPNOTSUPP;

	tc = qopt->queue;

	if (tc < 0 || tc >= priv->num_tx_rings)
		return -EINVAL;

	/* Do not support TXSTART and TX CSUM offload simutaniously */
	if (ndev->features & NETIF_F_CSUM_MASK)
		return -EBUSY;

	/* TSD and Qbv are mutually exclusive in hardware */
	if (enetc_rd(&priv->si->hw, ENETC_QBV_PTGCR_OFFSET) & ENETC_QBV_TGE)
		return -EBUSY;

	priv->tx_ring[tc]->tsd_enable = qopt->enable;
	enetc_port_wr(&priv->si->hw, ENETC_PTCTSDR(tc),
		      qopt->enable ? ENETC_TSDE : 0);

	return 0;
}

enum streamid_type {
	STREAMID_TYPE_RESERVED = 0,
	STREAMID_TYPE_NULL,
	STREAMID_TYPE_SMAC,
};

enum streamid_vlan_tagged {
	STREAMID_VLAN_RESERVED = 0,
	STREAMID_VLAN_TAGGED,
	STREAMID_VLAN_UNTAGGED,
	STREAMID_VLAN_ALL,
};

#define ENETC_PSFP_WILDCARD -1
#define HANDLE_OFFSET 100

enum forward_type {
	FILTER_ACTION_TYPE_PSFP = BIT(0),
	FILTER_ACTION_TYPE_ACL = BIT(1),
	FILTER_ACTION_TYPE_BOTH = GENMASK(1, 0),
};

/* This is for limit output type for input actions */
struct actions_fwd {
	u64 actions;
	u64 keys;	/* include the must needed keys */
	enum forward_type output;
};

struct psfp_streamfilter_counters {
	u64 matching_frames_count;
	u64 passing_frames_count;
	u64 not_passing_frames_count;
	u64 passing_sdu_count;
	u64 not_passing_sdu_count;
	u64 red_frames_count;
};

struct enetc_streamid {
	u32 index;
	union {
		u8 src_mac[6];
		u8 dst_mac[6];
	};
	u8 filtertype;
	u16 vid;
	u8 tagged;
	s32 handle;
};

struct enetc_psfp_filter {
	u32 index;
	s32 handle;
	s8 prio;
	u32 maxsdu;
	u32 gate_id;
	s32 meter_id;
	refcount_t refcount;
	struct hlist_node node;
};

struct enetc_psfp_gate {
	u32 index;
	s8 init_ipv;
	u64 basetime;
	u64 cycletime;
	u64 cycletimext;
	u32 num_entries;
	refcount_t refcount;
	struct hlist_node node;
	struct action_gate_entry entries[];
};

/* Only enable the green color frame now
 * Will add eir and ebs color blind, couple flag etc when
 * policing action add more offloading parameters
 */
struct enetc_psfp_meter {
	u32 index;
	u32 cir;
	u32 cbs;
	refcount_t refcount;
	struct hlist_node node;
};

#define ENETC_PSFP_FLAGS_FMI BIT(0)

struct enetc_stream_filter {
	struct enetc_streamid sid;
	u32 sfi_index;
	u32 sgi_index;
	u32 flags;
	u32 fmi_index;
	struct flow_stats stats;
	struct hlist_node node;
};

struct enetc_psfp {
	unsigned long dev_bitmap;
	unsigned long *psfp_sfi_bitmap;
	struct hlist_head stream_list;
	struct hlist_head psfp_filter_list;
	struct hlist_head psfp_gate_list;
	struct hlist_head psfp_meter_list;
	spinlock_t psfp_lock; /* spinlock for the struct enetc_psfp r/w */
};

static struct actions_fwd enetc_act_fwd[] = {
	{
		BIT(FLOW_ACTION_GATE),
		BIT(FLOW_DISSECTOR_KEY_ETH_ADDRS),
		FILTER_ACTION_TYPE_PSFP
	},
	{
		BIT(FLOW_ACTION_POLICE) |
		BIT(FLOW_ACTION_GATE),
		BIT(FLOW_DISSECTOR_KEY_ETH_ADDRS),
		FILTER_ACTION_TYPE_PSFP
	},
	/* example for ACL actions */
	{
		BIT(FLOW_ACTION_DROP),
		0,
		FILTER_ACTION_TYPE_ACL
	}
};

static struct enetc_psfp epsfp = {
	.psfp_sfi_bitmap = NULL,
};

static LIST_HEAD(enetc_block_cb_list);

/* Stream Identity Entry Set Descriptor */
static int enetc_streamid_hw_set(struct enetc_ndev_priv *priv,
				 struct enetc_streamid *sid,
				 u8 enable)
{
	struct enetc_cbd cbd = {.cmd = 0};
	struct streamid_data *si_data;
	struct streamid_conf *si_conf;
	u16 data_size;
	dma_addr_t dma;
	int err;

	if (sid->index >= priv->psfp_cap.max_streamid)
		return -EINVAL;

	if (sid->filtertype != STREAMID_TYPE_NULL &&
	    sid->filtertype != STREAMID_TYPE_SMAC)
		return -EOPNOTSUPP;

	/* Disable operation before enable */
	cbd.index = cpu_to_le16((u16)sid->index);
	cbd.cls = BDCR_CMD_STREAM_IDENTIFY;
	cbd.status_flags = 0;

	data_size = sizeof(struct streamid_data);
	si_data = kzalloc(data_size, __GFP_DMA | GFP_KERNEL);
	cbd.length = cpu_to_le16(data_size);

	dma = dma_map_single(&priv->si->pdev->dev, si_data,
			     data_size, DMA_FROM_DEVICE);
	if (dma_mapping_error(&priv->si->pdev->dev, dma)) {
		netdev_err(priv->si->ndev, "DMA mapping failed!\n");
		kfree(si_data);
		return -ENOMEM;
	}

	cbd.addr[0] = cpu_to_le32(lower_32_bits(dma));
	cbd.addr[1] = cpu_to_le32(upper_32_bits(dma));
	eth_broadcast_addr(si_data->dmac);
	si_data->vid_vidm_tg = (ENETC_CBDR_SID_VID_MASK
			       + ((0x3 << 14) | ENETC_CBDR_SID_VIDM));

	si_conf = &cbd.sid_set;
	/* Only one port supported for one entry, set itself */
	si_conf->iports = cpu_to_le32(1 << enetc_pf_to_port(priv->si->pdev));
	si_conf->id_type = 1;
	si_conf->oui[2] = 0x0;
	si_conf->oui[1] = 0x80;
	si_conf->oui[0] = 0xC2;

	err = enetc_send_cmd(priv->si, &cbd);
	if (err)
		return -EINVAL;

	if (!enable) {
		kfree(si_data);
		return 0;
	}

	/* Enable the entry overwrite again incase space flushed by hardware */
	memset(&cbd, 0, sizeof(cbd));

	cbd.index = cpu_to_le16((u16)sid->index);
	cbd.cmd = 0;
	cbd.cls = BDCR_CMD_STREAM_IDENTIFY;
	cbd.status_flags = 0;

	si_conf->en = 0x80;
	si_conf->stream_handle = cpu_to_le32(sid->handle);
	si_conf->iports = cpu_to_le32(1 << enetc_pf_to_port(priv->si->pdev));
	si_conf->id_type = sid->filtertype;
	si_conf->oui[2] = 0x0;
	si_conf->oui[1] = 0x80;
	si_conf->oui[0] = 0xC2;

	memset(si_data, 0, data_size);

	cbd.length = cpu_to_le16(data_size);

	cbd.addr[0] = cpu_to_le32(lower_32_bits(dma));
	cbd.addr[1] = cpu_to_le32(upper_32_bits(dma));

	/* VIDM default to be 1.
	 * VID Match. If set (b1) then the VID must match, otherwise
	 * any VID is considered a match. VIDM setting is only used
	 * when TG is set to b01.
	 */
	if (si_conf->id_type == STREAMID_TYPE_NULL) {
		ether_addr_copy(si_data->dmac, sid->dst_mac);
		si_data->vid_vidm_tg = (sid->vid & ENETC_CBDR_SID_VID_MASK) +
				       ((((u16)(sid->tagged) & 0x3) << 14)
				       | ENETC_CBDR_SID_VIDM);
	} else if (si_conf->id_type == STREAMID_TYPE_SMAC) {
		ether_addr_copy(si_data->smac, sid->src_mac);
		si_data->vid_vidm_tg = (sid->vid & ENETC_CBDR_SID_VID_MASK) +
				       ((((u16)(sid->tagged) & 0x3) << 14)
				       | ENETC_CBDR_SID_VIDM);
	}

	err = enetc_send_cmd(priv->si, &cbd);
	kfree(si_data);

	return err;
}

/* Stream Filter Instance Set Descriptor */
static int enetc_streamfilter_hw_set(struct enetc_ndev_priv *priv,
				     struct enetc_psfp_filter *sfi,
				     u8 enable)
{
	struct enetc_cbd cbd = {.cmd = 0};
	struct sfi_conf *sfi_config;

	cbd.index = cpu_to_le16(sfi->index);
	cbd.cls = BDCR_CMD_STREAM_FILTER;
	cbd.status_flags = 0x80;
	cbd.length = cpu_to_le16(1);

	sfi_config = &cbd.sfi_conf;
	if (!enable)
		goto exit;

	sfi_config->en = 0x80;

	if (sfi->handle >= 0) {
		sfi_config->stream_handle =
			cpu_to_le32(sfi->handle);
		sfi_config->sthm |= 0x80;
	}

	sfi_config->sg_inst_table_index = cpu_to_le16(sfi->gate_id);
	sfi_config->input_ports =
		cpu_to_le32(1 << enetc_pf_to_port(priv->si->pdev));

	/* The priority value which may be matched against the
	 * frame’s priority value to determine a match for this entry.
	 */
	if (sfi->prio >= 0)
		sfi_config->multi |= (sfi->prio & 0x7) | 0x8;

	/* Filter Type. Identifies the contents of the MSDU/FM_INST_INDEX
	 * field as being either an MSDU value or an index into the Flow
	 * Meter Instance table.
	 */
	if (sfi->maxsdu) {
		sfi_config->msdu =
		cpu_to_le16(sfi->maxsdu);
		sfi_config->multi |= 0x40;
	}

	if (sfi->meter_id >= 0) {
		sfi_config->fm_inst_table_index = cpu_to_le16(sfi->meter_id);
		sfi_config->multi |= 0x80;
	}

exit:
	return enetc_send_cmd(priv->si, &cbd);
}

static int enetc_streamcounter_hw_get(struct enetc_ndev_priv *priv,
				      u32 index,
				      struct psfp_streamfilter_counters *cnt)
{
	struct enetc_cbd cbd = { .cmd = 2 };
	struct sfi_counter_data *data_buf;
	dma_addr_t dma;
	u16 data_size;
	int err;

	cbd.index = cpu_to_le16((u16)index);
	cbd.cmd = 2;
	cbd.cls = BDCR_CMD_STREAM_FILTER;
	cbd.status_flags = 0;

	data_size = sizeof(struct sfi_counter_data);
	data_buf = kzalloc(data_size, __GFP_DMA | GFP_KERNEL);
	if (!data_buf)
		return -ENOMEM;

	dma = dma_map_single(&priv->si->pdev->dev, data_buf,
			     data_size, DMA_FROM_DEVICE);
	if (dma_mapping_error(&priv->si->pdev->dev, dma)) {
		netdev_err(priv->si->ndev, "DMA mapping failed!\n");
		err = -ENOMEM;
		goto exit;
	}
	cbd.addr[0] = cpu_to_le32(lower_32_bits(dma));
	cbd.addr[1] = cpu_to_le32(upper_32_bits(dma));

	cbd.length = cpu_to_le16(data_size);

	err = enetc_send_cmd(priv->si, &cbd);
	if (err)
		goto exit;

	cnt->matching_frames_count = ((u64)data_buf->matchh << 32) +
				     data_buf->matchl;

	cnt->not_passing_sdu_count = ((u64)data_buf->msdu_droph << 32) +
				     data_buf->msdu_dropl;

	cnt->passing_sdu_count = cnt->matching_frames_count
				- cnt->not_passing_sdu_count;

	cnt->not_passing_frames_count =
				((u64)data_buf->stream_gate_droph << 32) +
				data_buf->stream_gate_dropl;

	cnt->passing_frames_count = cnt->matching_frames_count -
				    cnt->not_passing_sdu_count -
				    cnt->not_passing_frames_count;

	cnt->red_frames_count =	((u64)data_buf->flow_meter_droph << 32)	+
				data_buf->flow_meter_dropl;

exit:
	kfree(data_buf);
	return err;
}

static u64 get_ptp_now(struct enetc_hw *hw)
{
	u64 now_lo, now_hi, now;

	now_lo = enetc_rd(hw, ENETC_SICTR0);
	now_hi = enetc_rd(hw, ENETC_SICTR1);
	now = now_lo | now_hi << 32;

	return now;
}

static int get_start_ns(u64 now, u64 cycle, u64 *start)
{
	u64 n;

	if (!cycle)
		return -EFAULT;

	n = div64_u64(now, cycle);

	*start = (n + 1) * cycle;

	return 0;
}

/* Stream Gate Instance Set Descriptor */
static int enetc_streamgate_hw_set(struct enetc_ndev_priv *priv,
				   struct enetc_psfp_gate *sgi,
				   u8 enable)
{
	struct enetc_cbd cbd = { .cmd = 0 };
	struct sgi_table *sgi_config;
	struct sgcl_conf *sgcl_config;
	struct sgcl_data *sgcl_data;
	struct sgce *sgce;
	dma_addr_t dma;
	u16 data_size;
	int err, i;
	u64 now;

	cbd.index = cpu_to_le16(sgi->index);
	cbd.cmd = 0;
	cbd.cls = BDCR_CMD_STREAM_GCL;
	cbd.status_flags = 0x80;

	/* disable */
	if (!enable)
		return enetc_send_cmd(priv->si, &cbd);

	if (!sgi->num_entries)
		return 0;

	if (sgi->num_entries > priv->psfp_cap.max_psfp_gatelist ||
	    !sgi->cycletime)
		return -EINVAL;

	/* enable */
	sgi_config = &cbd.sgi_table;

	/* Keep open before gate list start */
	sgi_config->ocgtst = 0x80;

	sgi_config->oipv = (sgi->init_ipv < 0) ?
				0x0 : ((sgi->init_ipv & 0x7) | 0x8);

	sgi_config->en = 0x80;

	/* Basic config */
	err = enetc_send_cmd(priv->si, &cbd);
	if (err)
		return -EINVAL;

	memset(&cbd, 0, sizeof(cbd));

	cbd.index = cpu_to_le16(sgi->index);
	cbd.cmd = 1;
	cbd.cls = BDCR_CMD_STREAM_GCL;
	cbd.status_flags = 0;

	sgcl_config = &cbd.sgcl_conf;

	sgcl_config->acl_len = (sgi->num_entries - 1) & 0x3;

	data_size = struct_size(sgcl_data, sgcl, sgi->num_entries);

	sgcl_data = kzalloc(data_size, __GFP_DMA | GFP_KERNEL);
	if (!sgcl_data)
		return -ENOMEM;

	cbd.length = cpu_to_le16(data_size);

	dma = dma_map_single(&priv->si->pdev->dev,
			     sgcl_data, data_size,
			     DMA_FROM_DEVICE);
	if (dma_mapping_error(&priv->si->pdev->dev, dma)) {
		netdev_err(priv->si->ndev, "DMA mapping failed!\n");
		kfree(sgcl_data);
		return -ENOMEM;
	}

	cbd.addr[0] = cpu_to_le32(lower_32_bits(dma));
	cbd.addr[1] = cpu_to_le32(upper_32_bits(dma));

	sgce = &sgcl_data->sgcl[0];

	sgcl_config->agtst = 0x80;

	sgcl_data->ct = sgi->cycletime;
	sgcl_data->cte = sgi->cycletimext;

	if (sgi->init_ipv >= 0)
		sgcl_config->aipv = (sgi->init_ipv & 0x7) | 0x8;

	for (i = 0; i < sgi->num_entries; i++) {
		struct action_gate_entry *from = &sgi->entries[i];
		struct sgce *to = &sgce[i];

		if (from->gate_state)
			to->multi |= 0x10;

		if (from->ipv >= 0)
			to->multi |= ((from->ipv & 0x7) << 5) | 0x08;

		if (from->maxoctets >= 0) {
			to->multi |= 0x01;
			to->msdu[0] = from->maxoctets & 0xFF;
			to->msdu[1] = (from->maxoctets >> 8) & 0xFF;
			to->msdu[2] = (from->maxoctets >> 16) & 0xFF;
		}

		to->interval = from->interval;
	}

	/* If basetime is less than now, calculate start time */
	now = get_ptp_now(&priv->si->hw);

	if (sgi->basetime < now) {
		u64 start;

		err = get_start_ns(now, sgi->cycletime, &start);
		if (err)
			goto exit;
		sgcl_data->btl = lower_32_bits(start);
		sgcl_data->bth = upper_32_bits(start);
	} else {
		u32 hi, lo;

		hi = upper_32_bits(sgi->basetime);
		lo = lower_32_bits(sgi->basetime);
		sgcl_data->bth = hi;
		sgcl_data->btl = lo;
	}

	err = enetc_send_cmd(priv->si, &cbd);

exit:
	kfree(sgcl_data);

	return err;
}

static int enetc_flowmeter_hw_set(struct enetc_ndev_priv *priv,
				  struct enetc_psfp_meter *fmi,
				  u8 enable)
{
	struct enetc_cbd cbd = { .cmd = 0 };
	struct fmi_conf *fmi_config;
	u64 temp = 0;

	cbd.index = cpu_to_le16((u16)fmi->index);
	cbd.cls = BDCR_CMD_FLOW_METER;
	cbd.status_flags = 0x80;

	if (!enable)
		return enetc_send_cmd(priv->si, &cbd);

	fmi_config = &cbd.fmi_conf;
	fmi_config->en = 0x80;

	if (fmi->cir) {
		temp = (u64)8000 * fmi->cir;
		temp = div_u64(temp, 3725);
	}

	fmi_config->cir = cpu_to_le32((u32)temp);
	fmi_config->cbs = cpu_to_le32(fmi->cbs);

	/* Default for eir ebs disable */
	fmi_config->eir = 0;
	fmi_config->ebs = 0;

	/* Default:
	 * mark red disable
	 * drop on yellow disable
	 * color mode disable
	 * couple flag disable
	 */
	fmi_config->conf = 0;

	return enetc_send_cmd(priv->si, &cbd);
}

static struct enetc_stream_filter *enetc_get_stream_by_index(u32 index)
{
	struct enetc_stream_filter *f;

	hlist_for_each_entry(f, &epsfp.stream_list, node)
		if (f->sid.index == index)
			return f;

	return NULL;
}

static struct enetc_psfp_gate *enetc_get_gate_by_index(u32 index)
{
	struct enetc_psfp_gate *g;

	hlist_for_each_entry(g, &epsfp.psfp_gate_list, node)
		if (g->index == index)
			return g;

	return NULL;
}

static struct enetc_psfp_filter *enetc_get_filter_by_index(u32 index)
{
	struct enetc_psfp_filter *s;

	hlist_for_each_entry(s, &epsfp.psfp_filter_list, node)
		if (s->index == index)
			return s;

	return NULL;
}

static struct enetc_psfp_meter *enetc_get_meter_by_index(u32 index)
{
	struct enetc_psfp_meter *m;

	hlist_for_each_entry(m, &epsfp.psfp_meter_list, node)
		if (m->index == index)
			return m;

	return NULL;
}

static struct enetc_psfp_filter
	*enetc_psfp_check_sfi(struct enetc_psfp_filter *sfi)
{
	struct enetc_psfp_filter *s;

	hlist_for_each_entry(s, &epsfp.psfp_filter_list, node)
		if (s->gate_id == sfi->gate_id &&
		    s->prio == sfi->prio &&
		    s->maxsdu == sfi->maxsdu &&
		    s->meter_id == sfi->meter_id)
			return s;

	return NULL;
}

static int enetc_get_free_index(struct enetc_ndev_priv *priv)
{
	u32 max_size = priv->psfp_cap.max_psfp_filter;
	unsigned long index;

	index = find_first_zero_bit(epsfp.psfp_sfi_bitmap, max_size);
	if (index == max_size)
		return -1;

	return index;
}

static void stream_filter_unref(struct enetc_ndev_priv *priv, u32 index)
{
	struct enetc_psfp_filter *sfi;
	u8 z;

	sfi = enetc_get_filter_by_index(index);
	WARN_ON(!sfi);
	z = refcount_dec_and_test(&sfi->refcount);

	if (z) {
		enetc_streamfilter_hw_set(priv, sfi, false);
		hlist_del(&sfi->node);
		kfree(sfi);
		clear_bit(index, epsfp.psfp_sfi_bitmap);
	}
}

static void stream_gate_unref(struct enetc_ndev_priv *priv, u32 index)
{
	struct enetc_psfp_gate *sgi;
	u8 z;

	sgi = enetc_get_gate_by_index(index);
	WARN_ON(!sgi);
	z = refcount_dec_and_test(&sgi->refcount);
	if (z) {
		enetc_streamgate_hw_set(priv, sgi, false);
		hlist_del(&sgi->node);
		kfree(sgi);
	}
}

static void flow_meter_unref(struct enetc_ndev_priv *priv, u32 index)
{
	struct enetc_psfp_meter *fmi;
	u8 z;

	fmi = enetc_get_meter_by_index(index);
	WARN_ON(!fmi);
	z = refcount_dec_and_test(&fmi->refcount);
	if (z) {
		enetc_flowmeter_hw_set(priv, fmi, false);
		hlist_del(&fmi->node);
		kfree(fmi);
	}
}

static void remove_one_chain(struct enetc_ndev_priv *priv,
			     struct enetc_stream_filter *filter)
{
	if (filter->flags & ENETC_PSFP_FLAGS_FMI)
		flow_meter_unref(priv, filter->fmi_index);

	stream_gate_unref(priv, filter->sgi_index);
	stream_filter_unref(priv, filter->sfi_index);

	hlist_del(&filter->node);
	kfree(filter);
}

static int enetc_psfp_hw_set(struct enetc_ndev_priv *priv,
			     struct enetc_streamid *sid,
			     struct enetc_psfp_filter *sfi,
			     struct enetc_psfp_gate *sgi,
			     struct enetc_psfp_meter *fmi)
{
	int err;

	err = enetc_streamid_hw_set(priv, sid, true);
	if (err)
		return err;

	if (sfi) {
		err = enetc_streamfilter_hw_set(priv, sfi, true);
		if (err)
			goto revert_sid;
	}

	err = enetc_streamgate_hw_set(priv, sgi, true);
	if (err)
		goto revert_sfi;

	if (fmi) {
		err = enetc_flowmeter_hw_set(priv, fmi, true);
		if (err)
			goto revert_sgi;
	}

	return 0;

revert_sgi:
	enetc_streamgate_hw_set(priv, sgi, false);
revert_sfi:
	if (sfi)
		enetc_streamfilter_hw_set(priv, sfi, false);
revert_sid:
	enetc_streamid_hw_set(priv, sid, false);
	return err;
}

static struct actions_fwd *enetc_check_flow_actions(u64 acts,
						    unsigned int inputkeys)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(enetc_act_fwd); i++)
		if (acts == enetc_act_fwd[i].actions &&
		    inputkeys & enetc_act_fwd[i].keys)
			return &enetc_act_fwd[i];

	return NULL;
}

static int enetc_psfp_parse_clsflower(struct enetc_ndev_priv *priv,
				      struct flow_cls_offload *f)
{
	struct flow_action_entry *entryg = NULL, *entryp = NULL;
	struct flow_rule *rule = flow_cls_offload_flow_rule(f);
	struct netlink_ext_ack *extack = f->common.extack;
	struct enetc_stream_filter *filter, *old_filter;
	struct enetc_psfp_meter *fmi = NULL, *old_fmi;
	struct enetc_psfp_filter *sfi, *old_sfi;
	struct enetc_psfp_gate *sgi, *old_sgi;
	struct flow_action_entry *entry;
	struct action_gate_entry *e;
	u8 sfi_overwrite = 0;
	int entries_size;
	int i, err;

	if (f->common.chain_index >= priv->psfp_cap.max_streamid) {
		NL_SET_ERR_MSG_MOD(extack, "No Stream identify resource!");
		return -ENOSPC;
	}

	flow_action_for_each(i, entry, &rule->action)
		if (entry->id == FLOW_ACTION_GATE)
			entryg = entry;
		else if (entry->id == FLOW_ACTION_POLICE)
			entryp = entry;

	/* Not support without gate action */
	if (!entryg)
		return -EINVAL;

	filter = kzalloc(sizeof(*filter), GFP_KERNEL);
	if (!filter)
		return -ENOMEM;

	filter->sid.index = f->common.chain_index;

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_ETH_ADDRS)) {
		struct flow_match_eth_addrs match;

		flow_rule_match_eth_addrs(rule, &match);

		if (!is_zero_ether_addr(match.mask->dst) &&
		    !is_zero_ether_addr(match.mask->src)) {
			NL_SET_ERR_MSG_MOD(extack,
					   "Cannot match on both source and destination MAC");
			err = -EINVAL;
			goto free_filter;
		}

		if (!is_zero_ether_addr(match.mask->dst)) {
			if (!is_broadcast_ether_addr(match.mask->dst)) {
				NL_SET_ERR_MSG_MOD(extack,
						   "Masked matching on destination MAC not supported");
				err = -EINVAL;
				goto free_filter;
			}
			ether_addr_copy(filter->sid.dst_mac, match.key->dst);
			filter->sid.filtertype = STREAMID_TYPE_NULL;
		}

		if (!is_zero_ether_addr(match.mask->src)) {
			if (!is_broadcast_ether_addr(match.mask->src)) {
				NL_SET_ERR_MSG_MOD(extack,
						   "Masked matching on source MAC not supported");
				err = -EINVAL;
				goto free_filter;
			}
			ether_addr_copy(filter->sid.src_mac, match.key->src);
			filter->sid.filtertype = STREAMID_TYPE_SMAC;
		}
	} else {
		NL_SET_ERR_MSG_MOD(extack, "Unsupported, must include ETH_ADDRS");
		err = -EINVAL;
		goto free_filter;
	}

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_VLAN)) {
		struct flow_match_vlan match;

		flow_rule_match_vlan(rule, &match);
		if (match.mask->vlan_priority) {
			if (match.mask->vlan_priority !=
			    (VLAN_PRIO_MASK >> VLAN_PRIO_SHIFT)) {
				NL_SET_ERR_MSG_MOD(extack, "Only full mask is supported for VLAN priority");
				err = -EINVAL;
				goto free_filter;
			}
		}

		if (match.mask->vlan_id) {
			if (match.mask->vlan_id != VLAN_VID_MASK) {
				NL_SET_ERR_MSG_MOD(extack, "Only full mask is supported for VLAN id");
				err = -EINVAL;
				goto free_filter;
			}

			filter->sid.vid = match.key->vlan_id;
			if (!filter->sid.vid)
				filter->sid.tagged = STREAMID_VLAN_UNTAGGED;
			else
				filter->sid.tagged = STREAMID_VLAN_TAGGED;
		}
	} else {
		filter->sid.tagged = STREAMID_VLAN_ALL;
	}

	/* parsing gate action */
	if (entryg->gate.index >= priv->psfp_cap.max_psfp_gate) {
		NL_SET_ERR_MSG_MOD(extack, "No Stream Gate resource!");
		err = -ENOSPC;
		goto free_filter;
	}

	if (entryg->gate.num_entries >= priv->psfp_cap.max_psfp_gatelist) {
		NL_SET_ERR_MSG_MOD(extack, "No Stream Gate resource!");
		err = -ENOSPC;
		goto free_filter;
	}

	entries_size = struct_size(sgi, entries, entryg->gate.num_entries);
	sgi = kzalloc(entries_size, GFP_KERNEL);
	if (!sgi) {
		err = -ENOMEM;
		goto free_filter;
	}

	refcount_set(&sgi->refcount, 1);
	sgi->index = entryg->gate.index;
	sgi->init_ipv = entryg->gate.prio;
	sgi->basetime = entryg->gate.basetime;
	sgi->cycletime = entryg->gate.cycletime;
	sgi->num_entries = entryg->gate.num_entries;

	e = sgi->entries;
	for (i = 0; i < entryg->gate.num_entries; i++) {
		e[i].gate_state = entryg->gate.entries[i].gate_state;
		e[i].interval = entryg->gate.entries[i].interval;
		e[i].ipv = entryg->gate.entries[i].ipv;
		e[i].maxoctets = entryg->gate.entries[i].maxoctets;
	}

	filter->sgi_index = sgi->index;

	sfi = kzalloc(sizeof(*sfi), GFP_KERNEL);
	if (!sfi) {
		err = -ENOMEM;
		goto free_gate;
	}

	refcount_set(&sfi->refcount, 1);
	sfi->gate_id = sgi->index;
	sfi->meter_id = ENETC_PSFP_WILDCARD;

	/* Flow meter and max frame size */
	if (entryp) {
		if (entryp->police.rate_pkt_ps) {
			NL_SET_ERR_MSG_MOD(extack, "QoS offload not support packets per second");
			err = -EOPNOTSUPP;
			goto free_sfi;
		}
		if (entryp->police.burst) {
			fmi = kzalloc(sizeof(*fmi), GFP_KERNEL);
			if (!fmi) {
				err = -ENOMEM;
				goto free_sfi;
			}
			refcount_set(&fmi->refcount, 1);
			fmi->cir = entryp->police.rate_bytes_ps;
			fmi->cbs = entryp->police.burst;
			fmi->index = entryp->police.index;
			filter->flags |= ENETC_PSFP_FLAGS_FMI;
			filter->fmi_index = fmi->index;
			sfi->meter_id = fmi->index;
		}

		if (entryp->police.mtu)
			sfi->maxsdu = entryp->police.mtu;
	}

	/* prio ref the filter prio */
	if (f->common.prio && f->common.prio <= BIT(3))
		sfi->prio = f->common.prio - 1;
	else
		sfi->prio = ENETC_PSFP_WILDCARD;

	old_sfi = enetc_psfp_check_sfi(sfi);
	if (!old_sfi) {
		int index;

		index = enetc_get_free_index(priv);
		if (sfi->handle < 0) {
			NL_SET_ERR_MSG_MOD(extack, "No Stream Filter resource!");
			err = -ENOSPC;
			goto free_fmi;
		}

		sfi->index = index;
		sfi->handle = index + HANDLE_OFFSET;
		/* Update the stream filter handle also */
		filter->sid.handle = sfi->handle;
		filter->sfi_index = sfi->index;
		sfi_overwrite = 0;
	} else {
		filter->sfi_index = old_sfi->index;
		filter->sid.handle = old_sfi->handle;
		sfi_overwrite = 1;
	}

	err = enetc_psfp_hw_set(priv, &filter->sid,
				sfi_overwrite ? NULL : sfi, sgi, fmi);
	if (err)
		goto free_fmi;

	spin_lock(&epsfp.psfp_lock);
	if (filter->flags & ENETC_PSFP_FLAGS_FMI) {
		old_fmi = enetc_get_meter_by_index(filter->fmi_index);
		if (old_fmi) {
			fmi->refcount = old_fmi->refcount;
			refcount_set(&fmi->refcount,
				     refcount_read(&old_fmi->refcount) + 1);
			hlist_del(&old_fmi->node);
			kfree(old_fmi);
		}
		hlist_add_head(&fmi->node, &epsfp.psfp_meter_list);
	}

	/* Remove the old node if exist and update with a new node */
	old_sgi = enetc_get_gate_by_index(filter->sgi_index);
	if (old_sgi) {
		refcount_set(&sgi->refcount,
			     refcount_read(&old_sgi->refcount) + 1);
		hlist_del(&old_sgi->node);
		kfree(old_sgi);
	}

	hlist_add_head(&sgi->node, &epsfp.psfp_gate_list);

	if (!old_sfi) {
		hlist_add_head(&sfi->node, &epsfp.psfp_filter_list);
		set_bit(sfi->index, epsfp.psfp_sfi_bitmap);
	} else {
		kfree(sfi);
		refcount_inc(&old_sfi->refcount);
	}

	old_filter = enetc_get_stream_by_index(filter->sid.index);
	if (old_filter)
		remove_one_chain(priv, old_filter);

	filter->stats.lastused = jiffies;
	hlist_add_head(&filter->node, &epsfp.stream_list);

	spin_unlock(&epsfp.psfp_lock);

	return 0;

free_fmi:
	kfree(fmi);
free_sfi:
	kfree(sfi);
free_gate:
	kfree(sgi);
free_filter:
	kfree(filter);

	return err;
}

static int enetc_config_clsflower(struct enetc_ndev_priv *priv,
				  struct flow_cls_offload *cls_flower)
{
	struct flow_rule *rule = flow_cls_offload_flow_rule(cls_flower);
	struct netlink_ext_ack *extack = cls_flower->common.extack;
	struct flow_dissector *dissector = rule->match.dissector;
	struct flow_action *action = &rule->action;
	struct flow_action_entry *entry;
	struct actions_fwd *fwd;
	u64 actions = 0;
	int i, err;

	if (!flow_action_has_entries(action)) {
		NL_SET_ERR_MSG_MOD(extack, "At least one action is needed");
		return -EINVAL;
	}

	flow_action_for_each(i, entry, action)
		actions |= BIT(entry->id);

	fwd = enetc_check_flow_actions(actions, dissector->used_keys);
	if (!fwd) {
		NL_SET_ERR_MSG_MOD(extack, "Unsupported filter type!");
		return -EOPNOTSUPP;
	}

	if (fwd->output & FILTER_ACTION_TYPE_PSFP) {
		err = enetc_psfp_parse_clsflower(priv, cls_flower);
		if (err) {
			NL_SET_ERR_MSG_MOD(extack, "Invalid PSFP inputs");
			return err;
		}
	} else {
		NL_SET_ERR_MSG_MOD(extack, "Unsupported actions");
		return -EOPNOTSUPP;
	}

	return 0;
}

static int enetc_psfp_destroy_clsflower(struct enetc_ndev_priv *priv,
					struct flow_cls_offload *f)
{
	struct enetc_stream_filter *filter;
	struct netlink_ext_ack *extack = f->common.extack;
	int err;

	if (f->common.chain_index >= priv->psfp_cap.max_streamid) {
		NL_SET_ERR_MSG_MOD(extack, "No Stream identify resource!");
		return -ENOSPC;
	}

	filter = enetc_get_stream_by_index(f->common.chain_index);
	if (!filter)
		return -EINVAL;

	err = enetc_streamid_hw_set(priv, &filter->sid, false);
	if (err)
		return err;

	remove_one_chain(priv, filter);

	return 0;
}

static int enetc_destroy_clsflower(struct enetc_ndev_priv *priv,
				   struct flow_cls_offload *f)
{
	return enetc_psfp_destroy_clsflower(priv, f);
}

static int enetc_psfp_get_stats(struct enetc_ndev_priv *priv,
				struct flow_cls_offload *f)
{
	struct psfp_streamfilter_counters counters = {};
	struct enetc_stream_filter *filter;
	struct flow_stats stats = {};
	int err;

	filter = enetc_get_stream_by_index(f->common.chain_index);
	if (!filter)
		return -EINVAL;

	err = enetc_streamcounter_hw_get(priv, filter->sfi_index, &counters);
	if (err)
		return -EINVAL;

	spin_lock(&epsfp.psfp_lock);
	stats.pkts = counters.matching_frames_count +
		     counters.not_passing_sdu_count -
		     filter->stats.pkts;
	stats.drops = counters.not_passing_frames_count +
		      counters.not_passing_sdu_count +
		      counters.red_frames_count -
		      filter->stats.drops;
	stats.lastused = filter->stats.lastused;
	filter->stats.pkts += stats.pkts;
	filter->stats.drops += stats.drops;
	spin_unlock(&epsfp.psfp_lock);

	flow_stats_update(&f->stats, 0x0, stats.pkts, stats.drops,
			  stats.lastused, FLOW_ACTION_HW_STATS_DELAYED);

	return 0;
}

static int enetc_setup_tc_cls_flower(struct enetc_ndev_priv *priv,
				     struct flow_cls_offload *cls_flower)
{
	switch (cls_flower->command) {
	case FLOW_CLS_REPLACE:
		return enetc_config_clsflower(priv, cls_flower);
	case FLOW_CLS_DESTROY:
		return enetc_destroy_clsflower(priv, cls_flower);
	case FLOW_CLS_STATS:
		return enetc_psfp_get_stats(priv, cls_flower);
	default:
		return -EOPNOTSUPP;
	}
}

static inline void clean_psfp_sfi_bitmap(void)
{
	bitmap_free(epsfp.psfp_sfi_bitmap);
	epsfp.psfp_sfi_bitmap = NULL;
}

static void clean_stream_list(void)
{
	struct enetc_stream_filter *s;
	struct hlist_node *tmp;

	hlist_for_each_entry_safe(s, tmp, &epsfp.stream_list, node) {
		hlist_del(&s->node);
		kfree(s);
	}
}

static void clean_sfi_list(void)
{
	struct enetc_psfp_filter *sfi;
	struct hlist_node *tmp;

	hlist_for_each_entry_safe(sfi, tmp, &epsfp.psfp_filter_list, node) {
		hlist_del(&sfi->node);
		kfree(sfi);
	}
}

static void clean_sgi_list(void)
{
	struct enetc_psfp_gate *sgi;
	struct hlist_node *tmp;

	hlist_for_each_entry_safe(sgi, tmp, &epsfp.psfp_gate_list, node) {
		hlist_del(&sgi->node);
		kfree(sgi);
	}
}

static void clean_psfp_all(void)
{
	/* Disable all list nodes and free all memory */
	clean_sfi_list();
	clean_sgi_list();
	clean_stream_list();
	epsfp.dev_bitmap = 0;
	clean_psfp_sfi_bitmap();
}

int enetc_setup_tc_block_cb(enum tc_setup_type type, void *type_data,
			    void *cb_priv)
{
	struct net_device *ndev = cb_priv;

	if (!tc_can_offload(ndev))
		return -EOPNOTSUPP;

	switch (type) {
	case TC_SETUP_CLSFLOWER:
		return enetc_setup_tc_cls_flower(netdev_priv(ndev), type_data);
	default:
		return -EOPNOTSUPP;
	}
}

int enetc_psfp_init(struct enetc_ndev_priv *priv)
{
	if (epsfp.psfp_sfi_bitmap)
		return 0;

	epsfp.psfp_sfi_bitmap = bitmap_zalloc(priv->psfp_cap.max_psfp_filter,
					      GFP_KERNEL);
	if (!epsfp.psfp_sfi_bitmap)
		return -ENOMEM;

	spin_lock_init(&epsfp.psfp_lock);

	if (list_empty(&enetc_block_cb_list))
		epsfp.dev_bitmap = 0;

	return 0;
}

int enetc_psfp_clean(struct enetc_ndev_priv *priv)
{
	if (!list_empty(&enetc_block_cb_list))
		return -EBUSY;

	clean_psfp_all();

	return 0;
}

int enetc_setup_tc_psfp(struct net_device *ndev, void *type_data)
{
	struct enetc_ndev_priv *priv = netdev_priv(ndev);
	struct flow_block_offload *f = type_data;
	int err;

	err = flow_block_cb_setup_simple(f, &enetc_block_cb_list,
					 enetc_setup_tc_block_cb,
					 ndev, ndev, true);
	if (err)
		return err;

	switch (f->command) {
	case FLOW_BLOCK_BIND:
		set_bit(enetc_pf_to_port(priv->si->pdev), &epsfp.dev_bitmap);
		break;
	case FLOW_BLOCK_UNBIND:
		clear_bit(enetc_pf_to_port(priv->si->pdev), &epsfp.dev_bitmap);
		if (!epsfp.dev_bitmap)
			clean_psfp_all();
		break;
	}

	return 0;
}
