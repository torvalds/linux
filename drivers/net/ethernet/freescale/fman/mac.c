/* Copyright 2008-2015 Freescale Semiconductor, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *	 notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *	 notice, this list of conditions and the following disclaimer in the
 *	 documentation and/or other materials provided with the distribution.
 *     * Neither the name of Freescale Semiconductor nor the
 *	 names of its contributors may be used to endorse or promote products
 *	 derived from this software without specific prior written permission.
 *
 *
 * ALTERNATIVELY, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") as published by the Free Software
 * Foundation, either version 2 of that License or (at your option) any
 * later version.
 *
 * THIS SOFTWARE IS PROVIDED BY Freescale Semiconductor ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL Freescale Semiconductor BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/init.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/of_net.h>
#include <linux/of_mdio.h>
#include <linux/device.h>
#include <linux/phy.h>
#include <linux/netdevice.h>
#include <linux/phy_fixed.h>
#include <linux/etherdevice.h>
#include <linux/libfdt_env.h>

#include "mac.h"
#include "fman_mac.h"
#include "fman_dtsec.h"
#include "fman_tgec.h"
#include "fman_memac.h"

MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("FSL FMan MAC API based driver");

struct mac_priv_s {
	struct device			*dev;
	void __iomem			*vaddr;
	u8				cell_index;
	struct fman			*fman;
	struct device_node		*internal_phy_node;
	/* List of multicast addresses */
	struct list_head		mc_addr_list;
	struct platform_device		*eth_dev;
	struct fixed_phy_status		*fixed_link;
	u16				speed;
	u16				max_speed;

	int (*enable)(struct fman_mac *mac_dev, enum comm_mode mode);
	int (*disable)(struct fman_mac *mac_dev, enum comm_mode mode);
};

struct mac_address {
	u8 addr[ETH_ALEN];
	struct list_head list;
};

static void mac_exception(void *handle, enum fman_mac_exceptions ex)
{
	struct mac_device	*mac_dev;
	struct mac_priv_s	*priv;

	mac_dev = handle;
	priv = mac_dev->priv;

	if (ex == FM_MAC_EX_10G_RX_FIFO_OVFL) {
		/* don't flag RX FIFO after the first */
		mac_dev->set_exception(mac_dev->fman_mac,
				       FM_MAC_EX_10G_RX_FIFO_OVFL, false);
		dev_err(priv->dev, "10G MAC got RX FIFO Error = %x\n", ex);
	}

	dev_dbg(priv->dev, "%s:%s() -> %d\n", KBUILD_BASENAME ".c",
		__func__, ex);
}

static int set_fman_mac_params(struct mac_device *mac_dev,
			       struct fman_mac_params *params)
{
	struct mac_priv_s *priv = mac_dev->priv;

	params->base_addr = (typeof(params->base_addr))
		devm_ioremap(priv->dev, mac_dev->res->start,
			     resource_size(mac_dev->res));
	if (!params->base_addr)
		return -ENOMEM;

	memcpy(&params->addr, mac_dev->addr, sizeof(mac_dev->addr));
	params->max_speed	= priv->max_speed;
	params->phy_if		= mac_dev->phy_if;
	params->basex_if	= false;
	params->mac_id		= priv->cell_index;
	params->fm		= (void *)priv->fman;
	params->exception_cb	= mac_exception;
	params->event_cb	= mac_exception;
	params->dev_id		= mac_dev;
	params->internal_phy_node = priv->internal_phy_node;

	return 0;
}

static int tgec_initialization(struct mac_device *mac_dev)
{
	int err;
	struct mac_priv_s	*priv;
	struct fman_mac_params	params;
	u32			version;

	priv = mac_dev->priv;

	err = set_fman_mac_params(mac_dev, &params);
	if (err)
		goto _return;

	mac_dev->fman_mac = tgec_config(&params);
	if (!mac_dev->fman_mac) {
		err = -EINVAL;
		goto _return;
	}

	err = tgec_cfg_max_frame_len(mac_dev->fman_mac, fman_get_max_frm());
	if (err < 0)
		goto _return_fm_mac_free;

	err = tgec_init(mac_dev->fman_mac);
	if (err < 0)
		goto _return_fm_mac_free;

	/* For 10G MAC, disable Tx ECC exception */
	err = mac_dev->set_exception(mac_dev->fman_mac,
				     FM_MAC_EX_10G_TX_ECC_ER, false);
	if (err < 0)
		goto _return_fm_mac_free;

	err = tgec_get_version(mac_dev->fman_mac, &version);
	if (err < 0)
		goto _return_fm_mac_free;

	dev_info(priv->dev, "FMan XGEC version: 0x%08x\n", version);

	goto _return;

_return_fm_mac_free:
	tgec_free(mac_dev->fman_mac);

_return:
	return err;
}

static int dtsec_initialization(struct mac_device *mac_dev)
{
	int			err;
	struct mac_priv_s	*priv;
	struct fman_mac_params	params;
	u32			version;

	priv = mac_dev->priv;

	err = set_fman_mac_params(mac_dev, &params);
	if (err)
		goto _return;

	mac_dev->fman_mac = dtsec_config(&params);
	if (!mac_dev->fman_mac) {
		err = -EINVAL;
		goto _return;
	}

	err = dtsec_cfg_max_frame_len(mac_dev->fman_mac, fman_get_max_frm());
	if (err < 0)
		goto _return_fm_mac_free;

	err = dtsec_cfg_pad_and_crc(mac_dev->fman_mac, true);
	if (err < 0)
		goto _return_fm_mac_free;

	err = dtsec_init(mac_dev->fman_mac);
	if (err < 0)
		goto _return_fm_mac_free;

	/* For 1G MAC, disable by default the MIB counters overflow interrupt */
	err = mac_dev->set_exception(mac_dev->fman_mac,
				     FM_MAC_EX_1G_RX_MIB_CNT_OVFL, false);
	if (err < 0)
		goto _return_fm_mac_free;

	err = dtsec_get_version(mac_dev->fman_mac, &version);
	if (err < 0)
		goto _return_fm_mac_free;

	dev_info(priv->dev, "FMan dTSEC version: 0x%08x\n", version);

	goto _return;

_return_fm_mac_free:
	dtsec_free(mac_dev->fman_mac);

_return:
	return err;
}

static int memac_initialization(struct mac_device *mac_dev)
{
	int			 err;
	struct mac_priv_s	*priv;
	struct fman_mac_params	 params;

	priv = mac_dev->priv;

	err = set_fman_mac_params(mac_dev, &params);
	if (err)
		goto _return;

	if (priv->max_speed == SPEED_10000)
		params.phy_if = PHY_INTERFACE_MODE_XGMII;

	mac_dev->fman_mac = memac_config(&params);
	if (!mac_dev->fman_mac) {
		err = -EINVAL;
		goto _return;
	}

	err = memac_cfg_max_frame_len(mac_dev->fman_mac, fman_get_max_frm());
	if (err < 0)
		goto _return_fm_mac_free;

	err = memac_cfg_reset_on_init(mac_dev->fman_mac, true);
	if (err < 0)
		goto _return_fm_mac_free;

	err = memac_cfg_fixed_link(mac_dev->fman_mac, priv->fixed_link);
	if (err < 0)
		goto _return_fm_mac_free;

	err = memac_init(mac_dev->fman_mac);
	if (err < 0)
		goto _return_fm_mac_free;

	dev_info(priv->dev, "FMan MEMAC\n");

	goto _return;

_return_fm_mac_free:
	memac_free(mac_dev->fman_mac);

_return:
	return err;
}

static int start(struct mac_device *mac_dev)
{
	int	 err;
	struct phy_device *phy_dev = mac_dev->phy_dev;
	struct mac_priv_s *priv = mac_dev->priv;

	err = priv->enable(mac_dev->fman_mac, COMM_MODE_RX_AND_TX);
	if (!err && phy_dev)
		phy_start(phy_dev);

	return err;
}

static int stop(struct mac_device *mac_dev)
{
	struct mac_priv_s *priv = mac_dev->priv;

	if (mac_dev->phy_dev)
		phy_stop(mac_dev->phy_dev);

	return priv->disable(mac_dev->fman_mac, COMM_MODE_RX_AND_TX);
}

static int set_multi(struct net_device *net_dev, struct mac_device *mac_dev)
{
	struct mac_priv_s	*priv;
	struct mac_address	*old_addr, *tmp;
	struct netdev_hw_addr	*ha;
	int			err;
	enet_addr_t		*addr;

	priv = mac_dev->priv;

	/* Clear previous address list */
	list_for_each_entry_safe(old_addr, tmp, &priv->mc_addr_list, list) {
		addr = (enet_addr_t *)old_addr->addr;
		err = mac_dev->remove_hash_mac_addr(mac_dev->fman_mac, addr);
		if (err < 0)
			return err;

		list_del(&old_addr->list);
		kfree(old_addr);
	}

	/* Add all the addresses from the new list */
	netdev_for_each_mc_addr(ha, net_dev) {
		addr = (enet_addr_t *)ha->addr;
		err = mac_dev->add_hash_mac_addr(mac_dev->fman_mac, addr);
		if (err < 0)
			return err;

		tmp = kmalloc(sizeof(*tmp), GFP_ATOMIC);
		if (!tmp)
			return -ENOMEM;

		ether_addr_copy(tmp->addr, ha->addr);
		list_add(&tmp->list, &priv->mc_addr_list);
	}
	return 0;
}

/**
 * fman_set_mac_active_pause
 * @mac_dev:	A pointer to the MAC device
 * @rx:		Pause frame setting for RX
 * @tx:		Pause frame setting for TX
 *
 * Set the MAC RX/TX PAUSE frames settings
 *
 * Avoid redundant calls to FMD, if the MAC driver already contains the desired
 * active PAUSE settings. Otherwise, the new active settings should be reflected
 * in FMan.
 *
 * Return: 0 on success; Error code otherwise.
 */
int fman_set_mac_active_pause(struct mac_device *mac_dev, bool rx, bool tx)
{
	struct fman_mac *fman_mac = mac_dev->fman_mac;
	int err = 0;

	if (rx != mac_dev->rx_pause_active) {
		err = mac_dev->set_rx_pause(fman_mac, rx);
		if (likely(err == 0))
			mac_dev->rx_pause_active = rx;
	}

	if (tx != mac_dev->tx_pause_active) {
		u16 pause_time = (tx ? FSL_FM_PAUSE_TIME_ENABLE :
					 FSL_FM_PAUSE_TIME_DISABLE);

		err = mac_dev->set_tx_pause(fman_mac, 0, pause_time, 0);

		if (likely(err == 0))
			mac_dev->tx_pause_active = tx;
	}

	return err;
}
EXPORT_SYMBOL(fman_set_mac_active_pause);

/**
 * fman_get_pause_cfg
 * @mac_dev:	A pointer to the MAC device
 * @rx_pause:	Return value for RX setting
 * @tx_pause:	Return value for TX setting
 *
 * Determine the MAC RX/TX PAUSE frames settings based on PHY
 * autonegotiation or values set by eththool.
 *
 * Return: Pointer to FMan device.
 */
void fman_get_pause_cfg(struct mac_device *mac_dev, bool *rx_pause,
			bool *tx_pause)
{
	struct phy_device *phy_dev = mac_dev->phy_dev;
	u16 lcl_adv, rmt_adv;
	u8 flowctrl;

	*rx_pause = *tx_pause = false;

	if (!phy_dev->duplex)
		return;

	/* If PAUSE autonegotiation is disabled, the TX/RX PAUSE settings
	 * are those set by ethtool.
	 */
	if (!mac_dev->autoneg_pause) {
		*rx_pause = mac_dev->rx_pause_req;
		*tx_pause = mac_dev->tx_pause_req;
		return;
	}

	/* Else if PAUSE autonegotiation is enabled, the TX/RX PAUSE
	 * settings depend on the result of the link negotiation.
	 */

	/* get local capabilities */
	lcl_adv = linkmode_adv_to_lcl_adv_t(phy_dev->advertising);

	/* get link partner capabilities */
	rmt_adv = 0;
	if (phy_dev->pause)
		rmt_adv |= LPA_PAUSE_CAP;
	if (phy_dev->asym_pause)
		rmt_adv |= LPA_PAUSE_ASYM;

	/* Calculate TX/RX settings based on local and peer advertised
	 * symmetric/asymmetric PAUSE capabilities.
	 */
	flowctrl = mii_resolve_flowctrl_fdx(lcl_adv, rmt_adv);
	if (flowctrl & FLOW_CTRL_RX)
		*rx_pause = true;
	if (flowctrl & FLOW_CTRL_TX)
		*tx_pause = true;
}
EXPORT_SYMBOL(fman_get_pause_cfg);

static void adjust_link_void(struct mac_device *mac_dev)
{
}

static void adjust_link_dtsec(struct mac_device *mac_dev)
{
	struct phy_device *phy_dev = mac_dev->phy_dev;
	struct fman_mac *fman_mac;
	bool rx_pause, tx_pause;
	int err;

	fman_mac = mac_dev->fman_mac;
	if (!phy_dev->link) {
		dtsec_restart_autoneg(fman_mac);

		return;
	}

	dtsec_adjust_link(fman_mac, phy_dev->speed);
	fman_get_pause_cfg(mac_dev, &rx_pause, &tx_pause);
	err = fman_set_mac_active_pause(mac_dev, rx_pause, tx_pause);
	if (err < 0)
		dev_err(mac_dev->priv->dev, "fman_set_mac_active_pause() = %d\n",
			err);
}

static void adjust_link_memac(struct mac_device *mac_dev)
{
	struct phy_device *phy_dev = mac_dev->phy_dev;
	struct fman_mac *fman_mac;
	bool rx_pause, tx_pause;
	int err;

	fman_mac = mac_dev->fman_mac;
	memac_adjust_link(fman_mac, phy_dev->speed);

	fman_get_pause_cfg(mac_dev, &rx_pause, &tx_pause);
	err = fman_set_mac_active_pause(mac_dev, rx_pause, tx_pause);
	if (err < 0)
		dev_err(mac_dev->priv->dev, "fman_set_mac_active_pause() = %d\n",
			err);
}

static void setup_dtsec(struct mac_device *mac_dev)
{
	mac_dev->init			= dtsec_initialization;
	mac_dev->set_promisc		= dtsec_set_promiscuous;
	mac_dev->change_addr		= dtsec_modify_mac_address;
	mac_dev->add_hash_mac_addr	= dtsec_add_hash_mac_address;
	mac_dev->remove_hash_mac_addr	= dtsec_del_hash_mac_address;
	mac_dev->set_tx_pause		= dtsec_set_tx_pause_frames;
	mac_dev->set_rx_pause		= dtsec_accept_rx_pause_frames;
	mac_dev->set_exception		= dtsec_set_exception;
	mac_dev->set_allmulti		= dtsec_set_allmulti;
	mac_dev->set_tstamp		= dtsec_set_tstamp;
	mac_dev->set_multi		= set_multi;
	mac_dev->start			= start;
	mac_dev->stop			= stop;
	mac_dev->adjust_link            = adjust_link_dtsec;
	mac_dev->priv->enable		= dtsec_enable;
	mac_dev->priv->disable		= dtsec_disable;
}

static void setup_tgec(struct mac_device *mac_dev)
{
	mac_dev->init			= tgec_initialization;
	mac_dev->set_promisc		= tgec_set_promiscuous;
	mac_dev->change_addr		= tgec_modify_mac_address;
	mac_dev->add_hash_mac_addr	= tgec_add_hash_mac_address;
	mac_dev->remove_hash_mac_addr	= tgec_del_hash_mac_address;
	mac_dev->set_tx_pause		= tgec_set_tx_pause_frames;
	mac_dev->set_rx_pause		= tgec_accept_rx_pause_frames;
	mac_dev->set_exception		= tgec_set_exception;
	mac_dev->set_allmulti		= tgec_set_allmulti;
	mac_dev->set_tstamp		= tgec_set_tstamp;
	mac_dev->set_multi		= set_multi;
	mac_dev->start			= start;
	mac_dev->stop			= stop;
	mac_dev->adjust_link            = adjust_link_void;
	mac_dev->priv->enable		= tgec_enable;
	mac_dev->priv->disable		= tgec_disable;
}

static void setup_memac(struct mac_device *mac_dev)
{
	mac_dev->init			= memac_initialization;
	mac_dev->set_promisc		= memac_set_promiscuous;
	mac_dev->change_addr		= memac_modify_mac_address;
	mac_dev->add_hash_mac_addr	= memac_add_hash_mac_address;
	mac_dev->remove_hash_mac_addr	= memac_del_hash_mac_address;
	mac_dev->set_tx_pause		= memac_set_tx_pause_frames;
	mac_dev->set_rx_pause		= memac_accept_rx_pause_frames;
	mac_dev->set_exception		= memac_set_exception;
	mac_dev->set_allmulti		= memac_set_allmulti;
	mac_dev->set_tstamp		= memac_set_tstamp;
	mac_dev->set_multi		= set_multi;
	mac_dev->start			= start;
	mac_dev->stop			= stop;
	mac_dev->adjust_link            = adjust_link_memac;
	mac_dev->priv->enable		= memac_enable;
	mac_dev->priv->disable		= memac_disable;
}

#define DTSEC_SUPPORTED \
	(SUPPORTED_10baseT_Half \
	| SUPPORTED_10baseT_Full \
	| SUPPORTED_100baseT_Half \
	| SUPPORTED_100baseT_Full \
	| SUPPORTED_Autoneg \
	| SUPPORTED_Pause \
	| SUPPORTED_Asym_Pause \
	| SUPPORTED_MII)

static DEFINE_MUTEX(eth_lock);

static const u16 phy2speed[] = {
	[PHY_INTERFACE_MODE_MII]		= SPEED_100,
	[PHY_INTERFACE_MODE_GMII]		= SPEED_1000,
	[PHY_INTERFACE_MODE_SGMII]		= SPEED_1000,
	[PHY_INTERFACE_MODE_TBI]		= SPEED_1000,
	[PHY_INTERFACE_MODE_RMII]		= SPEED_100,
	[PHY_INTERFACE_MODE_RGMII]		= SPEED_1000,
	[PHY_INTERFACE_MODE_RGMII_ID]		= SPEED_1000,
	[PHY_INTERFACE_MODE_RGMII_RXID]	= SPEED_1000,
	[PHY_INTERFACE_MODE_RGMII_TXID]	= SPEED_1000,
	[PHY_INTERFACE_MODE_RTBI]		= SPEED_1000,
	[PHY_INTERFACE_MODE_QSGMII]		= SPEED_1000,
	[PHY_INTERFACE_MODE_XGMII]		= SPEED_10000
};

static struct platform_device *dpaa_eth_add_device(int fman_id,
						   struct mac_device *mac_dev)
{
	struct platform_device *pdev;
	struct dpaa_eth_data data;
	struct mac_priv_s	*priv;
	static int dpaa_eth_dev_cnt;
	int ret;

	priv = mac_dev->priv;

	data.mac_dev = mac_dev;
	data.mac_hw_id = priv->cell_index;
	data.fman_hw_id = fman_id;

	mutex_lock(&eth_lock);
	pdev = platform_device_alloc("dpaa-ethernet", dpaa_eth_dev_cnt);
	if (!pdev) {
		ret = -ENOMEM;
		goto no_mem;
	}

	pdev->dev.parent = priv->dev;

	ret = platform_device_add_data(pdev, &data, sizeof(data));
	if (ret)
		goto err;

	ret = platform_device_add(pdev);
	if (ret)
		goto err;

	dpaa_eth_dev_cnt++;
	mutex_unlock(&eth_lock);

	return pdev;

err:
	platform_device_put(pdev);
no_mem:
	mutex_unlock(&eth_lock);

	return ERR_PTR(ret);
}

static const struct of_device_id mac_match[] = {
	{ .compatible	= "fsl,fman-dtsec" },
	{ .compatible	= "fsl,fman-xgec" },
	{ .compatible	= "fsl,fman-memac" },
	{}
};
MODULE_DEVICE_TABLE(of, mac_match);

static int mac_probe(struct platform_device *_of_dev)
{
	int			 err, i, nph;
	struct device		*dev;
	struct device_node	*mac_node, *dev_node;
	struct mac_device	*mac_dev;
	struct platform_device	*of_dev;
	struct resource		 res;
	struct mac_priv_s	*priv;
	const u8		*mac_addr;
	u32			 val;
	u8			fman_id;
	phy_interface_t          phy_if;

	dev = &_of_dev->dev;
	mac_node = dev->of_node;

	mac_dev = devm_kzalloc(dev, sizeof(*mac_dev), GFP_KERNEL);
	if (!mac_dev) {
		err = -ENOMEM;
		goto _return;
	}
	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv) {
		err = -ENOMEM;
		goto _return;
	}

	/* Save private information */
	mac_dev->priv = priv;
	priv->dev = dev;

	if (of_device_is_compatible(mac_node, "fsl,fman-dtsec")) {
		setup_dtsec(mac_dev);
		priv->internal_phy_node = of_parse_phandle(mac_node,
							  "tbi-handle", 0);
	} else if (of_device_is_compatible(mac_node, "fsl,fman-xgec")) {
		setup_tgec(mac_dev);
	} else if (of_device_is_compatible(mac_node, "fsl,fman-memac")) {
		setup_memac(mac_dev);
		priv->internal_phy_node = of_parse_phandle(mac_node,
							  "pcsphy-handle", 0);
	} else {
		dev_err(dev, "MAC node (%pOF) contains unsupported MAC\n",
			mac_node);
		err = -EINVAL;
		goto _return;
	}

	INIT_LIST_HEAD(&priv->mc_addr_list);

	/* Get the FM node */
	dev_node = of_get_parent(mac_node);
	if (!dev_node) {
		dev_err(dev, "of_get_parent(%pOF) failed\n",
			mac_node);
		err = -EINVAL;
		goto _return_of_get_parent;
	}

	of_dev = of_find_device_by_node(dev_node);
	if (!of_dev) {
		dev_err(dev, "of_find_device_by_node(%pOF) failed\n", dev_node);
		err = -EINVAL;
		goto _return_of_node_put;
	}

	/* Get the FMan cell-index */
	err = of_property_read_u32(dev_node, "cell-index", &val);
	if (err) {
		dev_err(dev, "failed to read cell-index for %pOF\n", dev_node);
		err = -EINVAL;
		goto _return_of_node_put;
	}
	/* cell-index 0 => FMan id 1 */
	fman_id = (u8)(val + 1);

	priv->fman = fman_bind(&of_dev->dev);
	if (!priv->fman) {
		dev_err(dev, "fman_bind(%pOF) failed\n", dev_node);
		err = -ENODEV;
		goto _return_of_node_put;
	}

	of_node_put(dev_node);

	/* Get the address of the memory mapped registers */
	err = of_address_to_resource(mac_node, 0, &res);
	if (err < 0) {
		dev_err(dev, "of_address_to_resource(%pOF) = %d\n",
			mac_node, err);
		goto _return_of_get_parent;
	}

	mac_dev->res = __devm_request_region(dev,
					     fman_get_mem_region(priv->fman),
					     res.start, resource_size(&res),
					     "mac");
	if (!mac_dev->res) {
		dev_err(dev, "__devm_request_mem_region(mac) failed\n");
		err = -EBUSY;
		goto _return_of_get_parent;
	}

	priv->vaddr = devm_ioremap(dev, mac_dev->res->start,
				   resource_size(mac_dev->res));
	if (!priv->vaddr) {
		dev_err(dev, "devm_ioremap() failed\n");
		err = -EIO;
		goto _return_of_get_parent;
	}

	if (!of_device_is_available(mac_node)) {
		err = -ENODEV;
		goto _return_of_get_parent;
	}

	/* Get the cell-index */
	err = of_property_read_u32(mac_node, "cell-index", &val);
	if (err) {
		dev_err(dev, "failed to read cell-index for %pOF\n", mac_node);
		err = -EINVAL;
		goto _return_of_get_parent;
	}
	priv->cell_index = (u8)val;

	/* Get the MAC address */
	mac_addr = of_get_mac_address(mac_node);
	if (IS_ERR(mac_addr))
		dev_warn(dev, "of_get_mac_address(%pOF) failed\n", mac_node);
	else
		ether_addr_copy(mac_dev->addr, mac_addr);

	/* Get the port handles */
	nph = of_count_phandle_with_args(mac_node, "fsl,fman-ports", NULL);
	if (unlikely(nph < 0)) {
		dev_err(dev, "of_count_phandle_with_args(%pOF, fsl,fman-ports) failed\n",
			mac_node);
		err = nph;
		goto _return_of_get_parent;
	}

	if (nph != ARRAY_SIZE(mac_dev->port)) {
		dev_err(dev, "Not supported number of fman-ports handles of mac node %pOF from device tree\n",
			mac_node);
		err = -EINVAL;
		goto _return_of_get_parent;
	}

	for (i = 0; i < ARRAY_SIZE(mac_dev->port); i++) {
		/* Find the port node */
		dev_node = of_parse_phandle(mac_node, "fsl,fman-ports", i);
		if (!dev_node) {
			dev_err(dev, "of_parse_phandle(%pOF, fsl,fman-ports) failed\n",
				mac_node);
			err = -EINVAL;
			goto _return_of_node_put;
		}

		of_dev = of_find_device_by_node(dev_node);
		if (!of_dev) {
			dev_err(dev, "of_find_device_by_node(%pOF) failed\n",
				dev_node);
			err = -EINVAL;
			goto _return_of_node_put;
		}

		mac_dev->port[i] = fman_port_bind(&of_dev->dev);
		if (!mac_dev->port[i]) {
			dev_err(dev, "dev_get_drvdata(%pOF) failed\n",
				dev_node);
			err = -EINVAL;
			goto _return_of_node_put;
		}
		of_node_put(dev_node);
	}

	/* Get the PHY connection type */
	err = of_get_phy_mode(mac_node, &phy_if);
	if (err) {
		dev_warn(dev,
			 "of_get_phy_mode() for %pOF failed. Defaulting to SGMII\n",
			 mac_node);
		phy_if = PHY_INTERFACE_MODE_SGMII;
	}
	mac_dev->phy_if = phy_if;

	priv->speed		= phy2speed[mac_dev->phy_if];
	priv->max_speed		= priv->speed;
	mac_dev->if_support	= DTSEC_SUPPORTED;
	/* We don't support half-duplex in SGMII mode */
	if (mac_dev->phy_if == PHY_INTERFACE_MODE_SGMII)
		mac_dev->if_support &= ~(SUPPORTED_10baseT_Half |
					SUPPORTED_100baseT_Half);

	/* Gigabit support (no half-duplex) */
	if (priv->max_speed == 1000)
		mac_dev->if_support |= SUPPORTED_1000baseT_Full;

	/* The 10G interface only supports one mode */
	if (mac_dev->phy_if == PHY_INTERFACE_MODE_XGMII)
		mac_dev->if_support = SUPPORTED_10000baseT_Full;

	/* Get the rest of the PHY information */
	mac_dev->phy_node = of_parse_phandle(mac_node, "phy-handle", 0);
	if (!mac_dev->phy_node && of_phy_is_fixed_link(mac_node)) {
		struct phy_device *phy;

		err = of_phy_register_fixed_link(mac_node);
		if (err)
			goto _return_of_get_parent;

		priv->fixed_link = kzalloc(sizeof(*priv->fixed_link),
					   GFP_KERNEL);
		if (!priv->fixed_link) {
			err = -ENOMEM;
			goto _return_of_get_parent;
		}

		mac_dev->phy_node = of_node_get(mac_node);
		phy = of_phy_find_device(mac_dev->phy_node);
		if (!phy) {
			err = -EINVAL;
			of_node_put(mac_dev->phy_node);
			goto _return_of_get_parent;
		}

		priv->fixed_link->link = phy->link;
		priv->fixed_link->speed = phy->speed;
		priv->fixed_link->duplex = phy->duplex;
		priv->fixed_link->pause = phy->pause;
		priv->fixed_link->asym_pause = phy->asym_pause;

		put_device(&phy->mdio.dev);
	}

	err = mac_dev->init(mac_dev);
	if (err < 0) {
		dev_err(dev, "mac_dev->init() = %d\n", err);
		of_node_put(mac_dev->phy_node);
		goto _return_of_get_parent;
	}

	/* pause frame autonegotiation enabled */
	mac_dev->autoneg_pause = true;

	/* By intializing the values to false, force FMD to enable PAUSE frames
	 * on RX and TX
	 */
	mac_dev->rx_pause_req = true;
	mac_dev->tx_pause_req = true;
	mac_dev->rx_pause_active = false;
	mac_dev->tx_pause_active = false;
	err = fman_set_mac_active_pause(mac_dev, true, true);
	if (err < 0)
		dev_err(dev, "fman_set_mac_active_pause() = %d\n", err);

	if (!IS_ERR(mac_addr))
		dev_info(dev, "FMan MAC address: %pM\n", mac_dev->addr);

	priv->eth_dev = dpaa_eth_add_device(fman_id, mac_dev);
	if (IS_ERR(priv->eth_dev)) {
		dev_err(dev, "failed to add Ethernet platform device for MAC %d\n",
			priv->cell_index);
		priv->eth_dev = NULL;
	}

	goto _return;

_return_of_node_put:
	of_node_put(dev_node);
_return_of_get_parent:
	kfree(priv->fixed_link);
_return:
	return err;
}

static int mac_remove(struct platform_device *pdev)
{
	struct mac_device *mac_dev = platform_get_drvdata(pdev);

	platform_device_unregister(mac_dev->priv->eth_dev);
	return 0;
}

static struct platform_driver mac_driver = {
	.driver = {
		.name		= KBUILD_MODNAME,
		.of_match_table	= mac_match,
	},
	.probe		= mac_probe,
	.remove		= mac_remove,
};

builtin_platform_driver(mac_driver);
