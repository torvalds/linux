// SPDX-License-Identifier: GPL-2.0
/* Copyright Sunplus Technology Co., Ltd.
 *       All rights reserved.
 */

#include <linux/platform_device.h>
#include <linux/nvmem-consumer.h>
#include <linux/etherdevice.h>
#include <linux/netdevice.h>
#include <linux/spinlock.h>
#include <linux/of_net.h>
#include <linux/reset.h>
#include <linux/clk.h>
#include <linux/of.h>

#include "spl2sw_register.h"
#include "spl2sw_define.h"
#include "spl2sw_desc.h"
#include "spl2sw_mdio.h"
#include "spl2sw_phy.h"
#include "spl2sw_int.h"
#include "spl2sw_mac.h"

/* net device operations */
static int spl2sw_ethernet_open(struct net_device *ndev)
{
	struct spl2sw_mac *mac = netdev_priv(ndev);
	struct spl2sw_common *comm = mac->comm;
	u32 mask;

	netdev_dbg(ndev, "Open port = %x\n", mac->lan_port);

	comm->enable |= mac->lan_port;

	spl2sw_mac_hw_start(comm);

	/* Enable TX and RX interrupts */
	mask = readl(comm->l2sw_reg_base + L2SW_SW_INT_MASK_0);
	mask &= ~(MAC_INT_TX | MAC_INT_RX);
	writel(mask, comm->l2sw_reg_base + L2SW_SW_INT_MASK_0);

	phy_start(ndev->phydev);

	netif_start_queue(ndev);

	return 0;
}

static int spl2sw_ethernet_stop(struct net_device *ndev)
{
	struct spl2sw_mac *mac = netdev_priv(ndev);
	struct spl2sw_common *comm = mac->comm;

	netif_stop_queue(ndev);

	comm->enable &= ~mac->lan_port;

	phy_stop(ndev->phydev);

	spl2sw_mac_hw_stop(comm);

	return 0;
}

static netdev_tx_t spl2sw_ethernet_start_xmit(struct sk_buff *skb,
					      struct net_device *ndev)
{
	struct spl2sw_mac *mac = netdev_priv(ndev);
	struct spl2sw_common *comm = mac->comm;
	struct spl2sw_skb_info *skbinfo;
	struct spl2sw_mac_desc *txdesc;
	unsigned long flags;
	u32 mapping;
	u32 tx_pos;
	u32 cmd1;
	u32 cmd2;

	if (unlikely(comm->tx_desc_full == 1)) {
		/* No TX descriptors left. Wait for tx interrupt. */
		netdev_dbg(ndev, "TX descriptor queue full when xmit!\n");
		return NETDEV_TX_BUSY;
	}

	/* If skb size is shorter than ETH_ZLEN (60), pad it with 0. */
	if (unlikely(skb->len < ETH_ZLEN)) {
		if (skb_padto(skb, ETH_ZLEN))
			return NETDEV_TX_OK;

		skb_put(skb, ETH_ZLEN - skb->len);
	}

	mapping = dma_map_single(&comm->pdev->dev, skb->data,
				 skb->len, DMA_TO_DEVICE);
	if (dma_mapping_error(&comm->pdev->dev, mapping)) {
		ndev->stats.tx_errors++;
		dev_kfree_skb(skb);
		return NETDEV_TX_OK;
	}

	spin_lock_irqsave(&comm->tx_lock, flags);

	tx_pos = comm->tx_pos;
	txdesc = &comm->tx_desc[tx_pos];
	skbinfo = &comm->tx_temp_skb_info[tx_pos];
	skbinfo->mapping = mapping;
	skbinfo->len = skb->len;
	skbinfo->skb = skb;

	/* Set up a TX descriptor */
	cmd1 = TXD_OWN | TXD_SOP | TXD_EOP | (mac->to_vlan << 12) |
	       (skb->len & TXD_PKT_LEN);
	cmd2 = skb->len & TXD_BUF_LEN1;

	if (tx_pos == (TX_DESC_NUM - 1))
		cmd2 |= TXD_EOR;

	txdesc->addr1 = skbinfo->mapping;
	txdesc->cmd2 = cmd2;
	wmb();	/* Set TXD_OWN after other fields are effective. */
	txdesc->cmd1 = cmd1;

	/* Move tx_pos to next position */
	tx_pos = ((tx_pos + 1) == TX_DESC_NUM) ? 0 : tx_pos + 1;

	if (unlikely(tx_pos == comm->tx_done_pos)) {
		netif_stop_queue(ndev);
		comm->tx_desc_full = 1;
	}
	comm->tx_pos = tx_pos;
	wmb();		/* make sure settings are effective. */

	/* Trigger mac to transmit */
	writel(MAC_TRIG_L_SOC0, comm->l2sw_reg_base + L2SW_CPU_TX_TRIG);

	spin_unlock_irqrestore(&comm->tx_lock, flags);
	return NETDEV_TX_OK;
}

static void spl2sw_ethernet_set_rx_mode(struct net_device *ndev)
{
	struct spl2sw_mac *mac = netdev_priv(ndev);

	spl2sw_mac_rx_mode_set(mac);
}

static int spl2sw_ethernet_set_mac_address(struct net_device *ndev, void *addr)
{
	struct spl2sw_mac *mac = netdev_priv(ndev);
	int err;

	err = eth_mac_addr(ndev, addr);
	if (err)
		return err;

	/* Delete the old MAC address */
	netdev_dbg(ndev, "Old Ethernet (MAC) address = %pM\n", mac->mac_addr);
	if (is_valid_ether_addr(mac->mac_addr)) {
		err = spl2sw_mac_addr_del(mac);
		if (err)
			return err;
	}

	/* Set the MAC address */
	ether_addr_copy(mac->mac_addr, ndev->dev_addr);
	return spl2sw_mac_addr_add(mac);
}

static void spl2sw_ethernet_tx_timeout(struct net_device *ndev, unsigned int txqueue)
{
	struct spl2sw_mac *mac = netdev_priv(ndev);
	struct spl2sw_common *comm = mac->comm;
	unsigned long flags;
	int i;

	netdev_err(ndev, "TX timed out!\n");
	ndev->stats.tx_errors++;

	spin_lock_irqsave(&comm->tx_lock, flags);

	for (i = 0; i < MAX_NETDEV_NUM; i++)
		if (comm->ndev[i])
			netif_stop_queue(comm->ndev[i]);

	spl2sw_mac_soft_reset(comm);

	/* Accept TX packets again. */
	for (i = 0; i < MAX_NETDEV_NUM; i++)
		if (comm->ndev[i]) {
			netif_trans_update(comm->ndev[i]);
			netif_wake_queue(comm->ndev[i]);
		}

	spin_unlock_irqrestore(&comm->tx_lock, flags);
}

static const struct net_device_ops netdev_ops = {
	.ndo_open = spl2sw_ethernet_open,
	.ndo_stop = spl2sw_ethernet_stop,
	.ndo_start_xmit = spl2sw_ethernet_start_xmit,
	.ndo_set_rx_mode = spl2sw_ethernet_set_rx_mode,
	.ndo_set_mac_address = spl2sw_ethernet_set_mac_address,
	.ndo_eth_ioctl = phy_do_ioctl,
	.ndo_tx_timeout = spl2sw_ethernet_tx_timeout,
};

static void spl2sw_check_mac_vendor_id_and_convert(u8 *mac_addr)
{
	/* Byte order of MAC address of some samples are reversed.
	 * Check vendor id and convert byte order if it is wrong.
	 * OUI of Sunplus: fc:4b:bc
	 */
	if (mac_addr[5] == 0xfc && mac_addr[4] == 0x4b && mac_addr[3] == 0xbc &&
	    (mac_addr[0] != 0xfc || mac_addr[1] != 0x4b || mac_addr[2] != 0xbc)) {

		swap(mac_addr[0], mac_addr[5]);
		swap(mac_addr[1], mac_addr[4]);
		swap(mac_addr[2], mac_addr[3]);
	}
}

static int spl2sw_nvmem_get_mac_address(struct device *dev, struct device_node *np,
					void *addrbuf)
{
	struct nvmem_cell *cell;
	ssize_t len;
	u8 *mac;

	/* Get nvmem cell of mac-address from dts. */
	cell = of_nvmem_cell_get(np, "mac-address");
	if (IS_ERR(cell))
		return PTR_ERR(cell);

	/* Read mac address from nvmem cell. */
	mac = nvmem_cell_read(cell, &len);
	nvmem_cell_put(cell);
	if (IS_ERR(mac))
		return PTR_ERR(mac);

	if (len != ETH_ALEN) {
		kfree(mac);
		dev_info(dev, "Invalid length of mac address in nvmem!\n");
		return -EINVAL;
	}

	/* Byte order of some samples are reversed.
	 * Convert byte order here.
	 */
	spl2sw_check_mac_vendor_id_and_convert(mac);

	/* Check if mac address is valid */
	if (!is_valid_ether_addr(mac)) {
		dev_info(dev, "Invalid mac address in nvmem (%pM)!\n", mac);
		kfree(mac);
		return -EINVAL;
	}

	ether_addr_copy(addrbuf, mac);
	kfree(mac);
	return 0;
}

static u32 spl2sw_init_netdev(struct platform_device *pdev, u8 *mac_addr,
			      struct net_device **r_ndev)
{
	struct net_device *ndev;
	struct spl2sw_mac *mac;
	int ret;

	/* Allocate the devices, and also allocate spl2sw_mac,
	 * we can get it by netdev_priv().
	 */
	ndev = devm_alloc_etherdev(&pdev->dev, sizeof(*mac));
	if (!ndev) {
		*r_ndev = NULL;
		return -ENOMEM;
	}
	SET_NETDEV_DEV(ndev, &pdev->dev);
	ndev->netdev_ops = &netdev_ops;
	mac = netdev_priv(ndev);
	mac->ndev = ndev;
	ether_addr_copy(mac->mac_addr, mac_addr);

	eth_hw_addr_set(ndev, mac_addr);
	dev_info(&pdev->dev, "Ethernet (MAC) address = %pM\n", mac_addr);

	ret = register_netdev(ndev);
	if (ret) {
		dev_err(&pdev->dev, "Failed to register net device \"%s\"!\n",
			ndev->name);
		*r_ndev = NULL;
		return ret;
	}
	netdev_dbg(ndev, "Registered net device \"%s\" successfully.\n", ndev->name);

	*r_ndev = ndev;
	return 0;
}

static struct device_node *spl2sw_get_eth_child_node(struct device_node *ether_np, int id)
{
	struct device_node *port_np;
	int port_id;

	for_each_child_of_node(ether_np, port_np) {
		/* It is not a 'port' node, continue. */
		if (strcmp(port_np->name, "port"))
			continue;

		if (of_property_read_u32(port_np, "reg", &port_id) < 0)
			continue;

		if (port_id == id)
			return port_np;
	}

	/* Not found! */
	return NULL;
}

static int spl2sw_probe(struct platform_device *pdev)
{
	struct device_node *eth_ports_np;
	struct device_node *port_np;
	struct spl2sw_common *comm;
	struct device_node *phy_np;
	phy_interface_t phy_mode;
	struct net_device *ndev;
	struct spl2sw_mac *mac;
	u8 mac_addr[ETH_ALEN];
	int irq, i, ret;

	if (platform_get_drvdata(pdev))
		return -ENODEV;

	/* Allocate memory for 'spl2sw_common' area. */
	comm = devm_kzalloc(&pdev->dev, sizeof(*comm), GFP_KERNEL);
	if (!comm)
		return -ENOMEM;

	comm->pdev = pdev;
	platform_set_drvdata(pdev, comm);

	spin_lock_init(&comm->tx_lock);
	spin_lock_init(&comm->mdio_lock);
	spin_lock_init(&comm->int_mask_lock);

	/* Get memory resource 0 from dts. */
	comm->l2sw_reg_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(comm->l2sw_reg_base))
		return PTR_ERR(comm->l2sw_reg_base);

	/* Get irq resource from dts. */
	ret = platform_get_irq(pdev, 0);
	if (ret < 0)
		return ret;
	irq = ret;

	/* Get clock controller. */
	comm->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(comm->clk)) {
		dev_err_probe(&pdev->dev, PTR_ERR(comm->clk),
			      "Failed to retrieve clock controller!\n");
		return PTR_ERR(comm->clk);
	}

	/* Get reset controller. */
	comm->rstc = devm_reset_control_get_exclusive(&pdev->dev, NULL);
	if (IS_ERR(comm->rstc)) {
		dev_err_probe(&pdev->dev, PTR_ERR(comm->rstc),
			      "Failed to retrieve reset controller!\n");
		return PTR_ERR(comm->rstc);
	}

	/* Enable clock. */
	ret = clk_prepare_enable(comm->clk);
	if (ret)
		return ret;
	udelay(1);

	/* Reset MAC */
	reset_control_assert(comm->rstc);
	udelay(1);
	reset_control_deassert(comm->rstc);
	usleep_range(1000, 2000);

	/* Request irq. */
	ret = devm_request_irq(&pdev->dev, irq, spl2sw_ethernet_interrupt, 0,
			       dev_name(&pdev->dev), comm);
	if (ret) {
		dev_err(&pdev->dev, "Failed to request irq #%d!\n", irq);
		goto out_clk_disable;
	}

	/* Initialize TX and RX descriptors. */
	ret = spl2sw_descs_init(comm);
	if (ret) {
		dev_err(&pdev->dev, "Fail to initialize mac descriptors!\n");
		spl2sw_descs_free(comm);
		goto out_clk_disable;
	}

	/* Initialize MAC. */
	spl2sw_mac_init(comm);

	/* Initialize mdio bus */
	ret = spl2sw_mdio_init(comm);
	if (ret) {
		dev_err(&pdev->dev, "Failed to initialize mdio bus!\n");
		goto out_clk_disable;
	}

	/* Get child node ethernet-ports. */
	eth_ports_np = of_get_child_by_name(pdev->dev.of_node, "ethernet-ports");
	if (!eth_ports_np) {
		dev_err(&pdev->dev, "No ethernet-ports child node found!\n");
		ret = -ENODEV;
		goto out_free_mdio;
	}

	for (i = 0; i < MAX_NETDEV_NUM; i++) {
		/* Get port@i of node ethernet-ports. */
		port_np = spl2sw_get_eth_child_node(eth_ports_np, i);
		if (!port_np)
			continue;

		/* Get phy-mode. */
		if (of_get_phy_mode(port_np, &phy_mode)) {
			dev_err(&pdev->dev, "Failed to get phy-mode property of port@%d!\n",
				i);
			continue;
		}

		/* Get phy-handle. */
		phy_np = of_parse_phandle(port_np, "phy-handle", 0);
		if (!phy_np) {
			dev_err(&pdev->dev, "Failed to get phy-handle property of port@%d!\n",
				i);
			continue;
		}

		/* Get mac-address from nvmem. */
		ret = spl2sw_nvmem_get_mac_address(&pdev->dev, port_np, mac_addr);
		if (ret == -EPROBE_DEFER) {
			goto out_unregister_dev;
		} else if (ret) {
			dev_info(&pdev->dev, "Generate a random mac address!\n");
			eth_random_addr(mac_addr);
		}

		/* Initialize the net device. */
		ret = spl2sw_init_netdev(pdev, mac_addr, &ndev);
		if (ret)
			goto out_unregister_dev;

		ndev->irq = irq;
		comm->ndev[i] = ndev;
		mac = netdev_priv(ndev);
		mac->phy_node = phy_np;
		mac->phy_mode = phy_mode;
		mac->comm = comm;

		mac->lan_port = 0x1 << i;	/* forward to port i */
		mac->to_vlan = 0x1 << i;	/* vlan group: i     */
		mac->vlan_id = i;		/* vlan group: i     */

		/* Set MAC address */
		ret = spl2sw_mac_addr_add(mac);
		if (ret)
			goto out_unregister_dev;

		spl2sw_mac_rx_mode_set(mac);
	}

	/* Find first valid net device. */
	for (i = 0; i < MAX_NETDEV_NUM; i++) {
		if (comm->ndev[i])
			break;
	}
	if (i >= MAX_NETDEV_NUM) {
		dev_err(&pdev->dev, "No valid ethernet port!\n");
		ret = -ENODEV;
		goto out_free_mdio;
	}

	/* Save first valid net device */
	ndev = comm->ndev[i];

	ret = spl2sw_phy_connect(comm);
	if (ret) {
		netdev_err(ndev, "Failed to connect phy!\n");
		goto out_unregister_dev;
	}

	/* Add and enable napi. */
	netif_napi_add(ndev, &comm->rx_napi, spl2sw_rx_poll);
	napi_enable(&comm->rx_napi);
	netif_napi_add_tx(ndev, &comm->tx_napi, spl2sw_tx_poll);
	napi_enable(&comm->tx_napi);
	return 0;

out_unregister_dev:
	for (i = 0; i < MAX_NETDEV_NUM; i++)
		if (comm->ndev[i])
			unregister_netdev(comm->ndev[i]);

out_free_mdio:
	spl2sw_mdio_remove(comm);

out_clk_disable:
	clk_disable_unprepare(comm->clk);
	return ret;
}

static void spl2sw_remove(struct platform_device *pdev)
{
	struct spl2sw_common *comm;
	int i;

	comm = platform_get_drvdata(pdev);

	spl2sw_phy_remove(comm);

	/* Unregister and free net device. */
	for (i = 0; i < MAX_NETDEV_NUM; i++)
		if (comm->ndev[i])
			unregister_netdev(comm->ndev[i]);

	comm->enable = 0;
	spl2sw_mac_hw_stop(comm);
	spl2sw_descs_free(comm);

	/* Disable and delete napi. */
	napi_disable(&comm->rx_napi);
	netif_napi_del(&comm->rx_napi);
	napi_disable(&comm->tx_napi);
	netif_napi_del(&comm->tx_napi);

	spl2sw_mdio_remove(comm);

	clk_disable_unprepare(comm->clk);
}

static const struct of_device_id spl2sw_of_match[] = {
	{.compatible = "sunplus,sp7021-emac"},
	{ /* sentinel */ }
};

MODULE_DEVICE_TABLE(of, spl2sw_of_match);

static struct platform_driver spl2sw_driver = {
	.probe = spl2sw_probe,
	.remove = spl2sw_remove,
	.driver = {
		.name = "sp7021_emac",
		.of_match_table = spl2sw_of_match,
	},
};

module_platform_driver(spl2sw_driver);

MODULE_AUTHOR("Wells Lu <wellslutw@gmail.com>");
MODULE_DESCRIPTION("Sunplus Dual 10M/100M Ethernet driver");
MODULE_LICENSE("GPL");
