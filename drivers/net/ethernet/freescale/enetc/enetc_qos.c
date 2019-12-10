// SPDX-License-Identifier: (GPL-2.0+ OR BSD-3-Clause)
/* Copyright 2019 NXP */

#include "enetc.h"

#include <net/pkt_sched.h>
#include <linux/math64.h>

static u16 enetc_get_max_gcl_len(struct enetc_hw *hw)
{
	return enetc_rd(hw, ENETC_QBV_PTGCAPR_OFFSET)
		& ENETC_QBV_MAX_GCL_LEN_MASK;
}

void enetc_sched_speed_set(struct net_device *ndev)
{
	struct enetc_ndev_priv *priv = netdev_priv(ndev);
	struct phy_device *phydev = ndev->phydev;
	u32 old_speed = priv->speed;
	u32 speed, pspeed;

	if (phydev->speed == old_speed)
		return;

	speed = phydev->speed;
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
		netdev_err(ndev, "Qbv PSPEED set speed link down.\n");
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

	if (!admin_conf->base_time) {
		gcl_data->btl =
			cpu_to_le32(enetc_rd(&priv->si->hw, ENETC_SICTR0));
		gcl_data->bth =
			cpu_to_le32(enetc_rd(&priv->si->hw, ENETC_SICTR1));
	} else {
		gcl_data->btl =
			cpu_to_le32(lower_32_bits(admin_conf->base_time));
		gcl_data->bth =
			cpu_to_le32(upper_32_bits(admin_conf->base_time));
	}

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

	cbd.addr[0] = lower_32_bits(dma);
	cbd.addr[1] = upper_32_bits(dma);
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
