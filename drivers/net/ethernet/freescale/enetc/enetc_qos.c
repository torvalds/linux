// SPDX-License-Identifier: (GPL-2.0+ OR BSD-3-Clause)
/* Copyright 2019 NXP */

#include "enetc.h"

#include <net/pkt_sched.h>

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
