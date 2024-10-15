// SPDX-License-Identifier: GPL-2.0+
// Copyright (c) 2024 Hisilicon Limited.

#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/iopoll.h>
#include <linux/minmax.h>
#include "hbg_common.h"
#include "hbg_hw.h"
#include "hbg_reg.h"

#define HBG_HW_EVENT_WAIT_TIMEOUT_US	(2 * 1000 * 1000)
#define HBG_HW_EVENT_WAIT_INTERVAL_US	(10 * 1000)

static bool hbg_hw_spec_is_valid(struct hbg_priv *priv)
{
	return hbg_reg_read(priv, HBG_REG_SPEC_VALID_ADDR) &&
	       !hbg_reg_read(priv, HBG_REG_EVENT_REQ_ADDR);
}

int hbg_hw_event_notify(struct hbg_priv *priv,
			enum hbg_hw_event_type event_type)
{
	bool is_valid;
	int ret;

	if (test_and_set_bit(HBG_NIC_STATE_EVENT_HANDLING, &priv->state))
		return -EBUSY;

	/* notify */
	hbg_reg_write(priv, HBG_REG_EVENT_REQ_ADDR, event_type);

	ret = read_poll_timeout(hbg_hw_spec_is_valid, is_valid, is_valid,
				HBG_HW_EVENT_WAIT_INTERVAL_US,
				HBG_HW_EVENT_WAIT_TIMEOUT_US,
				HBG_HW_EVENT_WAIT_INTERVAL_US, priv);

	clear_bit(HBG_NIC_STATE_EVENT_HANDLING, &priv->state);

	if (ret)
		dev_err(&priv->pdev->dev,
			"event %d wait timeout\n", event_type);

	return ret;
}

static int hbg_hw_dev_specs_init(struct hbg_priv *priv)
{
	struct hbg_dev_specs *specs = &priv->dev_specs;
	u64 mac_addr;

	if (!hbg_hw_spec_is_valid(priv)) {
		dev_err(&priv->pdev->dev, "dev_specs not init\n");
		return -EINVAL;
	}

	specs->mac_id = hbg_reg_read(priv, HBG_REG_MAC_ID_ADDR);
	specs->phy_addr = hbg_reg_read(priv, HBG_REG_PHY_ID_ADDR);
	specs->mdio_frequency = hbg_reg_read(priv, HBG_REG_MDIO_FREQ_ADDR);
	specs->max_mtu = hbg_reg_read(priv, HBG_REG_MAX_MTU_ADDR);
	specs->min_mtu = hbg_reg_read(priv, HBG_REG_MIN_MTU_ADDR);
	specs->vlan_layers = hbg_reg_read(priv, HBG_REG_VLAN_LAYERS_ADDR);
	specs->rx_fifo_num = hbg_reg_read(priv, HBG_REG_RX_FIFO_NUM_ADDR);
	specs->tx_fifo_num = hbg_reg_read(priv, HBG_REG_TX_FIFO_NUM_ADDR);
	mac_addr = hbg_reg_read64(priv, HBG_REG_MAC_ADDR_ADDR);
	u64_to_ether_addr(mac_addr, (u8 *)specs->mac_addr.sa_data);

	if (!is_valid_ether_addr((u8 *)specs->mac_addr.sa_data))
		return -EADDRNOTAVAIL;

	return 0;
}

int hbg_hw_init(struct hbg_priv *priv)
{
	return hbg_hw_dev_specs_init(priv);
}
