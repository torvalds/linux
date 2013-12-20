/*
 * Xilinx Axi Ethernet device driver
 *
 * Copyright (c) 2008 Nissin Systems Co., Ltd.,  Yoshio Kashiwagi
 * Copyright (c) 2005-2008 DLA Systems,  David H. Lynch Jr. <dhlii@dlasys.net>
 * Copyright (c) 2008-2009 Secret Lab Technologies Ltd.
 * Copyright (c) 2010 - 2011 Michal Simek <monstr@monstr.eu>
 * Copyright (c) 2010 - 2011 PetaLogix
 * Copyright (c) 2010 - 2012 Xilinx, Inc. All rights reserved.
 *
 * This is a driver for the Xilinx Axi Ethernet which is used in the Virtex6
 * and Spartan6.
 *
 * TODO:
 *  - Add Axi Fifo support.
 *  - Factor out Axi DMA code into separate driver.
 *  - Test and fix basic multicast filtering.
 *  - Add support for extended multicast filtering.
 *  - Test basic VLAN support.
 *  - Add support for extended VLAN support.
 */

#include <linux/delay.h>
#include <linux/etherdevice.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/of_mdio.h>
#include <linux/of_platform.h>
#include <linux/of_address.h>
#include <linux/skbuff.h>
#include <linux/spinlock.h>
#include <linux/phy.h>
#include <linux/mii.h>
#include <linux/ethtool.h>

#include "xilinx_axienet.h"

/* Descriptors defines for Tx and Rx DMA - 2^n for the best performance */
#define TX_BD_NUM		64
#define RX_BD_NUM		128

/* Must be shorter than length of ethtool_drvinfo.driver field to fit */
#define DRIVER_NAME		"xaxienet"
#define DRIVER_DESCRIPTION	"Xilinx Axi Ethernet driver"
#define DRIVER_VERSION		"1.00a"

#define AXIENET_REGS_N		32

/* Match table for of_platform binding */
static struct of_device_id axienet_of_match[] = {
	{ .compatible = "xlnx,axi-ethernet-1.00.a", },
	{ .compatible = "xlnx,axi-ethernet-1.01.a", },
	{ .compatible = "xlnx,axi-ethernet-2.01.a", },
	{},
};

MODULE_DEVICE_TABLE(of, axienet_of_match);

/* Option table for setting up Axi Ethernet hardware options */
static struct axienet_option axienet_options[] = {
	/* Turn on jumbo packet support for both Rx and Tx */
	{
		.opt = XAE_OPTION_JUMBO,
		.reg = XAE_TC_OFFSET,
		.m_or = XAE_TC_JUM_MASK,
	}, {
		.opt = XAE_OPTION_JUMBO,
		.reg = XAE_RCW1_OFFSET,
		.m_or = XAE_RCW1_JUM_MASK,
	}, { /* Turn on VLAN packet support for both Rx and Tx */
		.opt = XAE_OPTION_VLAN,
		.reg = XAE_TC_OFFSET,
		.m_or = XAE_TC_VLAN_MASK,
	}, {
		.opt = XAE_OPTION_VLAN,
		.reg = XAE_RCW1_OFFSET,
		.m_or = XAE_RCW1_VLAN_MASK,
	}, { /* Turn on FCS stripping on receive packets */
		.opt = XAE_OPTION_FCS_STRIP,
		.reg = XAE_RCW1_OFFSET,
		.m_or = XAE_RCW1_FCS_MASK,
	}, { /* Turn on FCS insertion on transmit packets */
		.opt = XAE_OPTION_FCS_INSERT,
		.reg = XAE_TC_OFFSET,
		.m_or = XAE_TC_FCS_MASK,
	}, { /* Turn off length/type field checking on receive packets */
		.opt = XAE_OPTION_LENTYPE_ERR,
		.reg = XAE_RCW1_OFFSET,
		.m_or = XAE_RCW1_LT_DIS_MASK,
	}, { /* Turn on Rx flow control */
		.opt = XAE_OPTION_FLOW_CONTROL,
		.reg = XAE_FCC_OFFSET,
		.m_or = XAE_FCC_FCRX_MASK,
	}, { /* Turn on Tx flow control */
		.opt = XAE_OPTION_FLOW_CONTROL,
		.reg = XAE_FCC_OFFSET,
		.m_or = XAE_FCC_FCTX_MASK,
	}, { /* Turn on promiscuous frame filtering */
		.opt = XAE_OPTION_PROMISC,
		.reg = XAE_FMI_OFFSET,
		.m_or = XAE_FMI_PM_MASK,
	}, { /* Enable transmitter */
		.opt = XAE_OPTION_TXEN,
		.reg = XAE_TC_OFFSET,
		.m_or = XAE_TC_TX_MASK,
	}, { /* Enable receiver */
		.opt = XAE_OPTION_RXEN,
		.reg = XAE_RCW1_OFFSET,
		.m_or = XAE_RCW1_RX_MASK,
	},
	{}
};

/**
 * axienet_dma_in32 - Memory mapped Axi DMA register read
 * @lp:		Pointer to axienet local structure
 * @reg:	Address offset from the base address of the Axi DMA core
 *
 * returns: The contents of the Axi DMA register
 *
 * This function returns the contents of the corresponding Axi DMA register.
 */
static inline u32 axienet_dma_in32(struct axienet_local *lp, off_t reg)
{
	return in_be32(lp->dma_regs + reg);
}

/**
 * axienet_dma_out32 - Memory mapped Axi DMA register write.
 * @lp:		Pointer to axienet local structure
 * @reg:	Address offset from the base address of the Axi DMA core
 * @value:	Value to be written into the Axi DMA register
 *
 * This function writes the desired value into the corresponding Axi DMA
 * register.
 */
static inline void axienet_dma_out32(struct axienet_local *lp,
				     off_t reg, u32 value)
{
	out_be32((lp->dma_regs + reg), value);
}

/**
 * axienet_dma_bd_release - Release buffer descriptor rings
 * @ndev:	Pointer to the net_device structure
 *
 * This function is used to release the descriptors allocated in
 * axienet_dma_bd_init. axienet_dma_bd_release is called when Axi Ethernet
 * driver stop api is called.
 */
static void axienet_dma_bd_release(struct net_device *ndev)
{
	int i;
	struct axienet_local *lp = netdev_priv(ndev);

	for (i = 0; i < RX_BD_NUM; i++) {
		dma_unmap_single(ndev->dev.parent, lp->rx_bd_v[i].phys,
				 lp->max_frm_size, DMA_FROM_DEVICE);
		dev_kfree_skb((struct sk_buff *)
			      (lp->rx_bd_v[i].sw_id_offset));
	}

	if (lp->rx_bd_v) {
		dma_free_coherent(ndev->dev.parent,
				  sizeof(*lp->rx_bd_v) * RX_BD_NUM,
				  lp->rx_bd_v,
				  lp->rx_bd_p);
	}
	if (lp->tx_bd_v) {
		dma_free_coherent(ndev->dev.parent,
				  sizeof(*lp->tx_bd_v) * TX_BD_NUM,
				  lp->tx_bd_v,
				  lp->tx_bd_p);
	}
}

/**
 * axienet_dma_bd_init - Setup buffer descriptor rings for Axi DMA
 * @ndev:	Pointer to the net_device structure
 *
 * returns: 0, on success
 *	    -ENOMEM, on failure
 *
 * This function is called to initialize the Rx and Tx DMA descriptor
 * rings. This initializes the descriptors with required default values
 * and is called when Axi Ethernet driver reset is called.
 */
static int axienet_dma_bd_init(struct net_device *ndev)
{
	u32 cr;
	int i;
	struct sk_buff *skb;
	struct axienet_local *lp = netdev_priv(ndev);

	/* Reset the indexes which are used for accessing the BDs */
	lp->tx_bd_ci = 0;
	lp->tx_bd_tail = 0;
	lp->rx_bd_ci = 0;

	/*
	 * Allocate the Tx and Rx buffer descriptors.
	 */
	lp->tx_bd_v = dma_alloc_coherent(ndev->dev.parent,
					 sizeof(*lp->tx_bd_v) * TX_BD_NUM,
					 &lp->tx_bd_p,
					 GFP_KERNEL | __GFP_ZERO);
	if (!lp->tx_bd_v)
		goto out;

	lp->rx_bd_v = dma_alloc_coherent(ndev->dev.parent,
					 sizeof(*lp->rx_bd_v) * RX_BD_NUM,
					 &lp->rx_bd_p,
					 GFP_KERNEL | __GFP_ZERO);
	if (!lp->rx_bd_v)
		goto out;

	for (i = 0; i < TX_BD_NUM; i++) {
		lp->tx_bd_v[i].next = lp->tx_bd_p +
				      sizeof(*lp->tx_bd_v) *
				      ((i + 1) % TX_BD_NUM);
	}

	for (i = 0; i < RX_BD_NUM; i++) {
		lp->rx_bd_v[i].next = lp->rx_bd_p +
				      sizeof(*lp->rx_bd_v) *
				      ((i + 1) % RX_BD_NUM);

		skb = netdev_alloc_skb_ip_align(ndev, lp->max_frm_size);
		if (!skb)
			goto out;

		lp->rx_bd_v[i].sw_id_offset = (u32) skb;
		lp->rx_bd_v[i].phys = dma_map_single(ndev->dev.parent,
						     skb->data,
						     lp->max_frm_size,
						     DMA_FROM_DEVICE);
		lp->rx_bd_v[i].cntrl = lp->max_frm_size;
	}

	/* Start updating the Rx channel control register */
	cr = axienet_dma_in32(lp, XAXIDMA_RX_CR_OFFSET);
	/* Update the interrupt coalesce count */
	cr = ((cr & ~XAXIDMA_COALESCE_MASK) |
	      ((lp->coalesce_count_rx) << XAXIDMA_COALESCE_SHIFT));
	/* Update the delay timer count */
	cr = ((cr & ~XAXIDMA_DELAY_MASK) |
	      (XAXIDMA_DFT_RX_WAITBOUND << XAXIDMA_DELAY_SHIFT));
	/* Enable coalesce, delay timer and error interrupts */
	cr |= XAXIDMA_IRQ_ALL_MASK;
	/* Write to the Rx channel control register */
	axienet_dma_out32(lp, XAXIDMA_RX_CR_OFFSET, cr);

	/* Start updating the Tx channel control register */
	cr = axienet_dma_in32(lp, XAXIDMA_TX_CR_OFFSET);
	/* Update the interrupt coalesce count */
	cr = (((cr & ~XAXIDMA_COALESCE_MASK)) |
	      ((lp->coalesce_count_tx) << XAXIDMA_COALESCE_SHIFT));
	/* Update the delay timer count */
	cr = (((cr & ~XAXIDMA_DELAY_MASK)) |
	      (XAXIDMA_DFT_TX_WAITBOUND << XAXIDMA_DELAY_SHIFT));
	/* Enable coalesce, delay timer and error interrupts */
	cr |= XAXIDMA_IRQ_ALL_MASK;
	/* Write to the Tx channel control register */
	axienet_dma_out32(lp, XAXIDMA_TX_CR_OFFSET, cr);

	/* Populate the tail pointer and bring the Rx Axi DMA engine out of
	 * halted state. This will make the Rx side ready for reception.
	 */
	axienet_dma_out32(lp, XAXIDMA_RX_CDESC_OFFSET, lp->rx_bd_p);
	cr = axienet_dma_in32(lp, XAXIDMA_RX_CR_OFFSET);
	axienet_dma_out32(lp, XAXIDMA_RX_CR_OFFSET,
			  cr | XAXIDMA_CR_RUNSTOP_MASK);
	axienet_dma_out32(lp, XAXIDMA_RX_TDESC_OFFSET, lp->rx_bd_p +
			  (sizeof(*lp->rx_bd_v) * (RX_BD_NUM - 1)));

	/* Write to the RS (Run-stop) bit in the Tx channel control register.
	 * Tx channel is now ready to run. But only after we write to the
	 * tail pointer register that the Tx channel will start transmitting.
	 */
	axienet_dma_out32(lp, XAXIDMA_TX_CDESC_OFFSET, lp->tx_bd_p);
	cr = axienet_dma_in32(lp, XAXIDMA_TX_CR_OFFSET);
	axienet_dma_out32(lp, XAXIDMA_TX_CR_OFFSET,
			  cr | XAXIDMA_CR_RUNSTOP_MASK);

	return 0;
out:
	axienet_dma_bd_release(ndev);
	return -ENOMEM;
}

/**
 * axienet_set_mac_address - Write the MAC address
 * @ndev:	Pointer to the net_device structure
 * @address:	6 byte Address to be written as MAC address
 *
 * This function is called to initialize the MAC address of the Axi Ethernet
 * core. It writes to the UAW0 and UAW1 registers of the core.
 */
static void axienet_set_mac_address(struct net_device *ndev, void *address)
{
	struct axienet_local *lp = netdev_priv(ndev);

	if (address)
		memcpy(ndev->dev_addr, address, ETH_ALEN);
	if (!is_valid_ether_addr(ndev->dev_addr))
		eth_random_addr(ndev->dev_addr);

	/* Set up unicast MAC address filter set its mac address */
	axienet_iow(lp, XAE_UAW0_OFFSET,
		    (ndev->dev_addr[0]) |
		    (ndev->dev_addr[1] << 8) |
		    (ndev->dev_addr[2] << 16) |
		    (ndev->dev_addr[3] << 24));
	axienet_iow(lp, XAE_UAW1_OFFSET,
		    (((axienet_ior(lp, XAE_UAW1_OFFSET)) &
		      ~XAE_UAW1_UNICASTADDR_MASK) |
		     (ndev->dev_addr[4] |
		     (ndev->dev_addr[5] << 8))));
}

/**
 * netdev_set_mac_address - Write the MAC address (from outside the driver)
 * @ndev:	Pointer to the net_device structure
 * @p:		6 byte Address to be written as MAC address
 *
 * returns: 0 for all conditions. Presently, there is no failure case.
 *
 * This function is called to initialize the MAC address of the Axi Ethernet
 * core. It calls the core specific axienet_set_mac_address. This is the
 * function that goes into net_device_ops structure entry ndo_set_mac_address.
 */
static int netdev_set_mac_address(struct net_device *ndev, void *p)
{
	struct sockaddr *addr = p;
	axienet_set_mac_address(ndev, addr->sa_data);
	return 0;
}

/**
 * axienet_set_multicast_list - Prepare the multicast table
 * @ndev:	Pointer to the net_device structure
 *
 * This function is called to initialize the multicast table during
 * initialization. The Axi Ethernet basic multicast support has a four-entry
 * multicast table which is initialized here. Additionally this function
 * goes into the net_device_ops structure entry ndo_set_multicast_list. This
 * means whenever the multicast table entries need to be updated this
 * function gets called.
 */
static void axienet_set_multicast_list(struct net_device *ndev)
{
	int i;
	u32 reg, af0reg, af1reg;
	struct axienet_local *lp = netdev_priv(ndev);

	if (ndev->flags & (IFF_ALLMULTI | IFF_PROMISC) ||
	    netdev_mc_count(ndev) > XAE_MULTICAST_CAM_TABLE_NUM) {
		/* We must make the kernel realize we had to move into
		 * promiscuous mode. If it was a promiscuous mode request
		 * the flag is already set. If not we set it.
		 */
		ndev->flags |= IFF_PROMISC;
		reg = axienet_ior(lp, XAE_FMI_OFFSET);
		reg |= XAE_FMI_PM_MASK;
		axienet_iow(lp, XAE_FMI_OFFSET, reg);
		dev_info(&ndev->dev, "Promiscuous mode enabled.\n");
	} else if (!netdev_mc_empty(ndev)) {
		struct netdev_hw_addr *ha;

		i = 0;
		netdev_for_each_mc_addr(ha, ndev) {
			if (i >= XAE_MULTICAST_CAM_TABLE_NUM)
				break;

			af0reg = (ha->addr[0]);
			af0reg |= (ha->addr[1] << 8);
			af0reg |= (ha->addr[2] << 16);
			af0reg |= (ha->addr[3] << 24);

			af1reg = (ha->addr[4]);
			af1reg |= (ha->addr[5] << 8);

			reg = axienet_ior(lp, XAE_FMI_OFFSET) & 0xFFFFFF00;
			reg |= i;

			axienet_iow(lp, XAE_FMI_OFFSET, reg);
			axienet_iow(lp, XAE_AF0_OFFSET, af0reg);
			axienet_iow(lp, XAE_AF1_OFFSET, af1reg);
			i++;
		}
	} else {
		reg = axienet_ior(lp, XAE_FMI_OFFSET);
		reg &= ~XAE_FMI_PM_MASK;

		axienet_iow(lp, XAE_FMI_OFFSET, reg);

		for (i = 0; i < XAE_MULTICAST_CAM_TABLE_NUM; i++) {
			reg = axienet_ior(lp, XAE_FMI_OFFSET) & 0xFFFFFF00;
			reg |= i;

			axienet_iow(lp, XAE_FMI_OFFSET, reg);
			axienet_iow(lp, XAE_AF0_OFFSET, 0);
			axienet_iow(lp, XAE_AF1_OFFSET, 0);
		}

		dev_info(&ndev->dev, "Promiscuous mode disabled.\n");
	}
}

/**
 * axienet_setoptions - Set an Axi Ethernet option
 * @ndev:	Pointer to the net_device structure
 * @options:	Option to be enabled/disabled
 *
 * The Axi Ethernet core has multiple features which can be selectively turned
 * on or off. The typical options could be jumbo frame option, basic VLAN
 * option, promiscuous mode option etc. This function is used to set or clear
 * these options in the Axi Ethernet hardware. This is done through
 * axienet_option structure .
 */
static void axienet_setoptions(struct net_device *ndev, u32 options)
{
	int reg;
	struct axienet_local *lp = netdev_priv(ndev);
	struct axienet_option *tp = &axienet_options[0];

	while (tp->opt) {
		reg = ((axienet_ior(lp, tp->reg)) & ~(tp->m_or));
		if (options & tp->opt)
			reg |= tp->m_or;
		axienet_iow(lp, tp->reg, reg);
		tp++;
	}

	lp->options |= options;
}

static void __axienet_device_reset(struct axienet_local *lp,
				   struct device *dev, off_t offset)
{
	u32 timeout;
	/* Reset Axi DMA. This would reset Axi Ethernet core as well. The reset
	 * process of Axi DMA takes a while to complete as all pending
	 * commands/transfers will be flushed or completed during this
	 * reset process.
	 */
	axienet_dma_out32(lp, offset, XAXIDMA_CR_RESET_MASK);
	timeout = DELAY_OF_ONE_MILLISEC;
	while (axienet_dma_in32(lp, offset) & XAXIDMA_CR_RESET_MASK) {
		udelay(1);
		if (--timeout == 0) {
			dev_err(dev,
				"axienet_device_reset DMA reset timeout!\n");
			break;
		}
	}
}

/**
 * axienet_device_reset - Reset and initialize the Axi Ethernet hardware.
 * @ndev:	Pointer to the net_device structure
 *
 * This function is called to reset and initialize the Axi Ethernet core. This
 * is typically called during initialization. It does a reset of the Axi DMA
 * Rx/Tx channels and initializes the Axi DMA BDs. Since Axi DMA reset lines
 * areconnected to Axi Ethernet reset lines, this in turn resets the Axi
 * Ethernet core. No separate hardware reset is done for the Axi Ethernet
 * core.
 */
static void axienet_device_reset(struct net_device *ndev)
{
	u32 axienet_status;
	struct axienet_local *lp = netdev_priv(ndev);

	__axienet_device_reset(lp, &ndev->dev, XAXIDMA_TX_CR_OFFSET);
	__axienet_device_reset(lp, &ndev->dev, XAXIDMA_RX_CR_OFFSET);

	lp->max_frm_size = XAE_MAX_VLAN_FRAME_SIZE;
	lp->options |= XAE_OPTION_VLAN;
	lp->options &= (~XAE_OPTION_JUMBO);

	if ((ndev->mtu > XAE_MTU) &&
		(ndev->mtu <= XAE_JUMBO_MTU)) {
		lp->max_frm_size = ndev->mtu + VLAN_ETH_HLEN +
					XAE_TRL_SIZE;

		if (lp->max_frm_size <= lp->rxmem)
			lp->options |= XAE_OPTION_JUMBO;
	}

	if (axienet_dma_bd_init(ndev)) {
		dev_err(&ndev->dev,
			"axienet_device_reset descriptor "
			"allocation failed\n");
	}

	axienet_status = axienet_ior(lp, XAE_RCW1_OFFSET);
	axienet_status &= ~XAE_RCW1_RX_MASK;
	axienet_iow(lp, XAE_RCW1_OFFSET, axienet_status);

	axienet_status = axienet_ior(lp, XAE_IP_OFFSET);
	if (axienet_status & XAE_INT_RXRJECT_MASK)
		axienet_iow(lp, XAE_IS_OFFSET, XAE_INT_RXRJECT_MASK);

	axienet_iow(lp, XAE_FCC_OFFSET, XAE_FCC_FCRX_MASK);

	/* Sync default options with HW but leave receiver and
	 * transmitter disabled.
	 */
	axienet_setoptions(ndev, lp->options &
			   ~(XAE_OPTION_TXEN | XAE_OPTION_RXEN));
	axienet_set_mac_address(ndev, NULL);
	axienet_set_multicast_list(ndev);
	axienet_setoptions(ndev, lp->options);

	ndev->trans_start = jiffies;
}

/**
 * axienet_adjust_link - Adjust the PHY link speed/duplex.
 * @ndev:	Pointer to the net_device structure
 *
 * This function is called to change the speed and duplex setting after
 * auto negotiation is done by the PHY. This is the function that gets
 * registered with the PHY interface through the "of_phy_connect" call.
 */
static void axienet_adjust_link(struct net_device *ndev)
{
	u32 emmc_reg;
	u32 link_state;
	u32 setspeed = 1;
	struct axienet_local *lp = netdev_priv(ndev);
	struct phy_device *phy = lp->phy_dev;

	link_state = phy->speed | (phy->duplex << 1) | phy->link;
	if (lp->last_link != link_state) {
		if ((phy->speed == SPEED_10) || (phy->speed == SPEED_100)) {
			if (lp->phy_type == XAE_PHY_TYPE_1000BASE_X)
				setspeed = 0;
		} else {
			if ((phy->speed == SPEED_1000) &&
			    (lp->phy_type == XAE_PHY_TYPE_MII))
				setspeed = 0;
		}

		if (setspeed == 1) {
			emmc_reg = axienet_ior(lp, XAE_EMMC_OFFSET);
			emmc_reg &= ~XAE_EMMC_LINKSPEED_MASK;

			switch (phy->speed) {
			case SPEED_1000:
				emmc_reg |= XAE_EMMC_LINKSPD_1000;
				break;
			case SPEED_100:
				emmc_reg |= XAE_EMMC_LINKSPD_100;
				break;
			case SPEED_10:
				emmc_reg |= XAE_EMMC_LINKSPD_10;
				break;
			default:
				dev_err(&ndev->dev,
					"Speed other than 10, 100 "
					"or 1Gbps is not supported\n");
				break;
			}

			axienet_iow(lp, XAE_EMMC_OFFSET, emmc_reg);
			lp->last_link = link_state;
			phy_print_status(phy);
		} else {
			dev_err(&ndev->dev,
				"Error setting Axi Ethernet mac speed\n");
		}
	}
}

/**
 * axienet_start_xmit_done - Invoked once a transmit is completed by the
 * Axi DMA Tx channel.
 * @ndev:	Pointer to the net_device structure
 *
 * This function is invoked from the Axi DMA Tx isr to notify the completion
 * of transmit operation. It clears fields in the corresponding Tx BDs and
 * unmaps the corresponding buffer so that CPU can regain ownership of the
 * buffer. It finally invokes "netif_wake_queue" to restart transmission if
 * required.
 */
static void axienet_start_xmit_done(struct net_device *ndev)
{
	u32 size = 0;
	u32 packets = 0;
	struct axienet_local *lp = netdev_priv(ndev);
	struct axidma_bd *cur_p;
	unsigned int status = 0;

	cur_p = &lp->tx_bd_v[lp->tx_bd_ci];
	status = cur_p->status;
	while (status & XAXIDMA_BD_STS_COMPLETE_MASK) {
		dma_unmap_single(ndev->dev.parent, cur_p->phys,
				(cur_p->cntrl & XAXIDMA_BD_CTRL_LENGTH_MASK),
				DMA_TO_DEVICE);
		if (cur_p->app4)
			dev_kfree_skb_irq((struct sk_buff *)cur_p->app4);
		/*cur_p->phys = 0;*/
		cur_p->app0 = 0;
		cur_p->app1 = 0;
		cur_p->app2 = 0;
		cur_p->app4 = 0;
		cur_p->status = 0;

		size += status & XAXIDMA_BD_STS_ACTUAL_LEN_MASK;
		packets++;

		++lp->tx_bd_ci;
		lp->tx_bd_ci %= TX_BD_NUM;
		cur_p = &lp->tx_bd_v[lp->tx_bd_ci];
		status = cur_p->status;
	}

	ndev->stats.tx_packets += packets;
	ndev->stats.tx_bytes += size;
	netif_wake_queue(ndev);
}

/**
 * axienet_check_tx_bd_space - Checks if a BD/group of BDs are currently busy
 * @lp:		Pointer to the axienet_local structure
 * @num_frag:	The number of BDs to check for
 *
 * returns: 0, on success
 *	    NETDEV_TX_BUSY, if any of the descriptors are not free
 *
 * This function is invoked before BDs are allocated and transmission starts.
 * This function returns 0 if a BD or group of BDs can be allocated for
 * transmission. If the BD or any of the BDs are not free the function
 * returns a busy status. This is invoked from axienet_start_xmit.
 */
static inline int axienet_check_tx_bd_space(struct axienet_local *lp,
					    int num_frag)
{
	struct axidma_bd *cur_p;
	cur_p = &lp->tx_bd_v[(lp->tx_bd_tail + num_frag) % TX_BD_NUM];
	if (cur_p->status & XAXIDMA_BD_STS_ALL_MASK)
		return NETDEV_TX_BUSY;
	return 0;
}

/**
 * axienet_start_xmit - Starts the transmission.
 * @skb:	sk_buff pointer that contains data to be Txed.
 * @ndev:	Pointer to net_device structure.
 *
 * returns: NETDEV_TX_OK, on success
 *	    NETDEV_TX_BUSY, if any of the descriptors are not free
 *
 * This function is invoked from upper layers to initiate transmission. The
 * function uses the next available free BDs and populates their fields to
 * start the transmission. Additionally if checksum offloading is supported,
 * it populates AXI Stream Control fields with appropriate values.
 */
static int axienet_start_xmit(struct sk_buff *skb, struct net_device *ndev)
{
	u32 ii;
	u32 num_frag;
	u32 csum_start_off;
	u32 csum_index_off;
	skb_frag_t *frag;
	dma_addr_t tail_p;
	struct axienet_local *lp = netdev_priv(ndev);
	struct axidma_bd *cur_p;

	num_frag = skb_shinfo(skb)->nr_frags;
	cur_p = &lp->tx_bd_v[lp->tx_bd_tail];

	if (axienet_check_tx_bd_space(lp, num_frag)) {
		if (!netif_queue_stopped(ndev))
			netif_stop_queue(ndev);
		return NETDEV_TX_BUSY;
	}

	if (skb->ip_summed == CHECKSUM_PARTIAL) {
		if (lp->features & XAE_FEATURE_FULL_TX_CSUM) {
			/* Tx Full Checksum Offload Enabled */
			cur_p->app0 |= 2;
		} else if (lp->features & XAE_FEATURE_PARTIAL_RX_CSUM) {
			csum_start_off = skb_transport_offset(skb);
			csum_index_off = csum_start_off + skb->csum_offset;
			/* Tx Partial Checksum Offload Enabled */
			cur_p->app0 |= 1;
			cur_p->app1 = (csum_start_off << 16) | csum_index_off;
		}
	} else if (skb->ip_summed == CHECKSUM_UNNECESSARY) {
		cur_p->app0 |= 2; /* Tx Full Checksum Offload Enabled */
	}

	cur_p->cntrl = skb_headlen(skb) | XAXIDMA_BD_CTRL_TXSOF_MASK;
	cur_p->phys = dma_map_single(ndev->dev.parent, skb->data,
				     skb_headlen(skb), DMA_TO_DEVICE);

	for (ii = 0; ii < num_frag; ii++) {
		++lp->tx_bd_tail;
		lp->tx_bd_tail %= TX_BD_NUM;
		cur_p = &lp->tx_bd_v[lp->tx_bd_tail];
		frag = &skb_shinfo(skb)->frags[ii];
		cur_p->phys = dma_map_single(ndev->dev.parent,
					     skb_frag_address(frag),
					     skb_frag_size(frag),
					     DMA_TO_DEVICE);
		cur_p->cntrl = skb_frag_size(frag);
	}

	cur_p->cntrl |= XAXIDMA_BD_CTRL_TXEOF_MASK;
	cur_p->app4 = (unsigned long)skb;

	tail_p = lp->tx_bd_p + sizeof(*lp->tx_bd_v) * lp->tx_bd_tail;
	wmb();

	/* Start the transfer */
	axienet_dma_out32(lp, XAXIDMA_TX_TDESC_OFFSET, tail_p);
	++lp->tx_bd_tail;
	lp->tx_bd_tail %= TX_BD_NUM;

	return NETDEV_TX_OK;
}

/**
 * axienet_recv - Is called from Axi DMA Rx Isr to complete the received
 *		  BD processing.
 * @ndev:	Pointer to net_device structure.
 *
 * This function is invoked from the Axi DMA Rx isr to process the Rx BDs. It
 * does minimal processing and invokes "netif_rx" to complete further
 * processing.
 */
static void axienet_recv(struct net_device *ndev)
{
	u32 length;
	u32 csumstatus;
	u32 size = 0;
	u32 packets = 0;
	dma_addr_t tail_p = 0;
	struct axienet_local *lp = netdev_priv(ndev);
	struct sk_buff *skb, *new_skb;
	struct axidma_bd *cur_p;

	rmb();
	cur_p = &lp->rx_bd_v[lp->rx_bd_ci];

	while ((cur_p->status & XAXIDMA_BD_STS_COMPLETE_MASK)) {
		tail_p = lp->rx_bd_p + sizeof(*lp->rx_bd_v) * lp->rx_bd_ci;
		skb = (struct sk_buff *) (cur_p->sw_id_offset);
		length = cur_p->app4 & 0x0000FFFF;

		dma_unmap_single(ndev->dev.parent, cur_p->phys,
				 lp->max_frm_size,
				 DMA_FROM_DEVICE);

		skb_put(skb, length);
		skb->protocol = eth_type_trans(skb, ndev);
		/*skb_checksum_none_assert(skb);*/
		skb->ip_summed = CHECKSUM_NONE;

		/* if we're doing Rx csum offload, set it up */
		if (lp->features & XAE_FEATURE_FULL_RX_CSUM) {
			csumstatus = (cur_p->app2 &
				      XAE_FULL_CSUM_STATUS_MASK) >> 3;
			if ((csumstatus == XAE_IP_TCP_CSUM_VALIDATED) ||
			    (csumstatus == XAE_IP_UDP_CSUM_VALIDATED)) {
				skb->ip_summed = CHECKSUM_UNNECESSARY;
			}
		} else if ((lp->features & XAE_FEATURE_PARTIAL_RX_CSUM) != 0 &&
			   skb->protocol == __constant_htons(ETH_P_IP) &&
			   skb->len > 64) {
			skb->csum = be32_to_cpu(cur_p->app3 & 0xFFFF);
			skb->ip_summed = CHECKSUM_COMPLETE;
		}

		netif_rx(skb);

		size += length;
		packets++;

		new_skb = netdev_alloc_skb_ip_align(ndev, lp->max_frm_size);
		if (!new_skb)
			return;

		cur_p->phys = dma_map_single(ndev->dev.parent, new_skb->data,
					     lp->max_frm_size,
					     DMA_FROM_DEVICE);
		cur_p->cntrl = lp->max_frm_size;
		cur_p->status = 0;
		cur_p->sw_id_offset = (u32) new_skb;

		++lp->rx_bd_ci;
		lp->rx_bd_ci %= RX_BD_NUM;
		cur_p = &lp->rx_bd_v[lp->rx_bd_ci];
	}

	ndev->stats.rx_packets += packets;
	ndev->stats.rx_bytes += size;

	if (tail_p)
		axienet_dma_out32(lp, XAXIDMA_RX_TDESC_OFFSET, tail_p);
}

/**
 * axienet_tx_irq - Tx Done Isr.
 * @irq:	irq number
 * @_ndev:	net_device pointer
 *
 * returns: IRQ_HANDLED for all cases.
 *
 * This is the Axi DMA Tx done Isr. It invokes "axienet_start_xmit_done"
 * to complete the BD processing.
 */
static irqreturn_t axienet_tx_irq(int irq, void *_ndev)
{
	u32 cr;
	unsigned int status;
	struct net_device *ndev = _ndev;
	struct axienet_local *lp = netdev_priv(ndev);

	status = axienet_dma_in32(lp, XAXIDMA_TX_SR_OFFSET);
	if (status & (XAXIDMA_IRQ_IOC_MASK | XAXIDMA_IRQ_DELAY_MASK)) {
		axienet_dma_out32(lp, XAXIDMA_TX_SR_OFFSET, status);
		axienet_start_xmit_done(lp->ndev);
		goto out;
	}
	if (!(status & XAXIDMA_IRQ_ALL_MASK))
		dev_err(&ndev->dev, "No interrupts asserted in Tx path");
	if (status & XAXIDMA_IRQ_ERROR_MASK) {
		dev_err(&ndev->dev, "DMA Tx error 0x%x\n", status);
		dev_err(&ndev->dev, "Current BD is at: 0x%x\n",
			(lp->tx_bd_v[lp->tx_bd_ci]).phys);

		cr = axienet_dma_in32(lp, XAXIDMA_TX_CR_OFFSET);
		/* Disable coalesce, delay timer and error interrupts */
		cr &= (~XAXIDMA_IRQ_ALL_MASK);
		/* Write to the Tx channel control register */
		axienet_dma_out32(lp, XAXIDMA_TX_CR_OFFSET, cr);

		cr = axienet_dma_in32(lp, XAXIDMA_RX_CR_OFFSET);
		/* Disable coalesce, delay timer and error interrupts */
		cr &= (~XAXIDMA_IRQ_ALL_MASK);
		/* Write to the Rx channel control register */
		axienet_dma_out32(lp, XAXIDMA_RX_CR_OFFSET, cr);

		tasklet_schedule(&lp->dma_err_tasklet);
		axienet_dma_out32(lp, XAXIDMA_TX_SR_OFFSET, status);
	}
out:
	return IRQ_HANDLED;
}

/**
 * axienet_rx_irq - Rx Isr.
 * @irq:	irq number
 * @_ndev:	net_device pointer
 *
 * returns: IRQ_HANDLED for all cases.
 *
 * This is the Axi DMA Rx Isr. It invokes "axienet_recv" to complete the BD
 * processing.
 */
static irqreturn_t axienet_rx_irq(int irq, void *_ndev)
{
	u32 cr;
	unsigned int status;
	struct net_device *ndev = _ndev;
	struct axienet_local *lp = netdev_priv(ndev);

	status = axienet_dma_in32(lp, XAXIDMA_RX_SR_OFFSET);
	if (status & (XAXIDMA_IRQ_IOC_MASK | XAXIDMA_IRQ_DELAY_MASK)) {
		axienet_dma_out32(lp, XAXIDMA_RX_SR_OFFSET, status);
		axienet_recv(lp->ndev);
		goto out;
	}
	if (!(status & XAXIDMA_IRQ_ALL_MASK))
		dev_err(&ndev->dev, "No interrupts asserted in Rx path");
	if (status & XAXIDMA_IRQ_ERROR_MASK) {
		dev_err(&ndev->dev, "DMA Rx error 0x%x\n", status);
		dev_err(&ndev->dev, "Current BD is at: 0x%x\n",
			(lp->rx_bd_v[lp->rx_bd_ci]).phys);

		cr = axienet_dma_in32(lp, XAXIDMA_TX_CR_OFFSET);
		/* Disable coalesce, delay timer and error interrupts */
		cr &= (~XAXIDMA_IRQ_ALL_MASK);
		/* Finally write to the Tx channel control register */
		axienet_dma_out32(lp, XAXIDMA_TX_CR_OFFSET, cr);

		cr = axienet_dma_in32(lp, XAXIDMA_RX_CR_OFFSET);
		/* Disable coalesce, delay timer and error interrupts */
		cr &= (~XAXIDMA_IRQ_ALL_MASK);
		/* write to the Rx channel control register */
		axienet_dma_out32(lp, XAXIDMA_RX_CR_OFFSET, cr);

		tasklet_schedule(&lp->dma_err_tasklet);
		axienet_dma_out32(lp, XAXIDMA_RX_SR_OFFSET, status);
	}
out:
	return IRQ_HANDLED;
}

static void axienet_dma_err_handler(unsigned long data);

/**
 * axienet_open - Driver open routine.
 * @ndev:	Pointer to net_device structure
 *
 * returns: 0, on success.
 *	    -ENODEV, if PHY cannot be connected to
 *	    non-zero error value on failure
 *
 * This is the driver open routine. It calls phy_start to start the PHY
 * device. It also allocates interrupt service routines, enables the
 * interrupt lines and ISR handling. Axi Ethernet core is reset through Axi
 * DMA core. Buffer descriptors are initialized.
 */
static int axienet_open(struct net_device *ndev)
{
	int ret, mdio_mcreg;
	struct axienet_local *lp = netdev_priv(ndev);

	dev_dbg(&ndev->dev, "axienet_open()\n");

	mdio_mcreg = axienet_ior(lp, XAE_MDIO_MC_OFFSET);
	ret = axienet_mdio_wait_until_ready(lp);
	if (ret < 0)
		return ret;
	/* Disable the MDIO interface till Axi Ethernet Reset is completed.
	 * When we do an Axi Ethernet reset, it resets the complete core
	 * including the MDIO. If MDIO is not disabled when the reset
	 * process is started, MDIO will be broken afterwards.
	 */
	axienet_iow(lp, XAE_MDIO_MC_OFFSET,
		    (mdio_mcreg & (~XAE_MDIO_MC_MDIOEN_MASK)));
	axienet_device_reset(ndev);
	/* Enable the MDIO */
	axienet_iow(lp, XAE_MDIO_MC_OFFSET, mdio_mcreg);
	ret = axienet_mdio_wait_until_ready(lp);
	if (ret < 0)
		return ret;

	if (lp->phy_node) {
		if (lp->phy_type == XAE_PHY_TYPE_GMII) {
			lp->phy_dev = of_phy_connect(lp->ndev, lp->phy_node,
					     axienet_adjust_link, 0,
					     PHY_INTERFACE_MODE_GMII);
		} else if (lp->phy_type == XAE_PHY_TYPE_RGMII_2_0) {
			lp->phy_dev = of_phy_connect(lp->ndev, lp->phy_node,
					     axienet_adjust_link, 0,
					     PHY_INTERFACE_MODE_RGMII_ID);
		}

		if (!lp->phy_dev)
			dev_err(lp->dev, "of_phy_connect() failed\n");
		else
			phy_start(lp->phy_dev);
	}

	/* Enable tasklets for Axi DMA error handling */
	tasklet_init(&lp->dma_err_tasklet, axienet_dma_err_handler,
		     (unsigned long) lp);

	/* Enable interrupts for Axi DMA Tx */
	ret = request_irq(lp->tx_irq, axienet_tx_irq, 0, ndev->name, ndev);
	if (ret)
		goto err_tx_irq;
	/* Enable interrupts for Axi DMA Rx */
	ret = request_irq(lp->rx_irq, axienet_rx_irq, 0, ndev->name, ndev);
	if (ret)
		goto err_rx_irq;

	return 0;

err_rx_irq:
	free_irq(lp->tx_irq, ndev);
err_tx_irq:
	if (lp->phy_dev)
		phy_disconnect(lp->phy_dev);
	lp->phy_dev = NULL;
	tasklet_kill(&lp->dma_err_tasklet);
	dev_err(lp->dev, "request_irq() failed\n");
	return ret;
}

/**
 * axienet_stop - Driver stop routine.
 * @ndev:	Pointer to net_device structure
 *
 * returns: 0, on success.
 *
 * This is the driver stop routine. It calls phy_disconnect to stop the PHY
 * device. It also removes the interrupt handlers and disables the interrupts.
 * The Axi DMA Tx/Rx BDs are released.
 */
static int axienet_stop(struct net_device *ndev)
{
	u32 cr;
	struct axienet_local *lp = netdev_priv(ndev);

	dev_dbg(&ndev->dev, "axienet_close()\n");

	cr = axienet_dma_in32(lp, XAXIDMA_RX_CR_OFFSET);
	axienet_dma_out32(lp, XAXIDMA_RX_CR_OFFSET,
			  cr & (~XAXIDMA_CR_RUNSTOP_MASK));
	cr = axienet_dma_in32(lp, XAXIDMA_TX_CR_OFFSET);
	axienet_dma_out32(lp, XAXIDMA_TX_CR_OFFSET,
			  cr & (~XAXIDMA_CR_RUNSTOP_MASK));
	axienet_setoptions(ndev, lp->options &
			   ~(XAE_OPTION_TXEN | XAE_OPTION_RXEN));

	tasklet_kill(&lp->dma_err_tasklet);

	free_irq(lp->tx_irq, ndev);
	free_irq(lp->rx_irq, ndev);

	if (lp->phy_dev)
		phy_disconnect(lp->phy_dev);
	lp->phy_dev = NULL;

	axienet_dma_bd_release(ndev);
	return 0;
}

/**
 * axienet_change_mtu - Driver change mtu routine.
 * @ndev:	Pointer to net_device structure
 * @new_mtu:	New mtu value to be applied
 *
 * returns: Always returns 0 (success).
 *
 * This is the change mtu driver routine. It checks if the Axi Ethernet
 * hardware supports jumbo frames before changing the mtu. This can be
 * called only when the device is not up.
 */
static int axienet_change_mtu(struct net_device *ndev, int new_mtu)
{
	struct axienet_local *lp = netdev_priv(ndev);

	if (netif_running(ndev))
		return -EBUSY;

	if ((new_mtu + VLAN_ETH_HLEN +
		XAE_TRL_SIZE) > lp->rxmem)
		return -EINVAL;

	if ((new_mtu > XAE_JUMBO_MTU) || (new_mtu < 64))
		return -EINVAL;

	ndev->mtu = new_mtu;

	return 0;
}

#ifdef CONFIG_NET_POLL_CONTROLLER
/**
 * axienet_poll_controller - Axi Ethernet poll mechanism.
 * @ndev:	Pointer to net_device structure
 *
 * This implements Rx/Tx ISR poll mechanisms. The interrupts are disabled
 * prior to polling the ISRs and are enabled back after the polling is done.
 */
static void axienet_poll_controller(struct net_device *ndev)
{
	struct axienet_local *lp = netdev_priv(ndev);
	disable_irq(lp->tx_irq);
	disable_irq(lp->rx_irq);
	axienet_rx_irq(lp->tx_irq, ndev);
	axienet_tx_irq(lp->rx_irq, ndev);
	enable_irq(lp->tx_irq);
	enable_irq(lp->rx_irq);
}
#endif

/* Ioctl MII Interface */
static int axienet_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
	struct axienet_local *priv = netdev_priv(dev);

	if (!netif_running(dev))
		return -EINVAL;

	if (!priv->phy_dev)
		return -EOPNOTSUPP;

	return phy_mii_ioctl(priv->phy_dev, rq, cmd);
}

static const struct net_device_ops axienet_netdev_ops = {
	.ndo_open = axienet_open,
	.ndo_stop = axienet_stop,
	.ndo_start_xmit = axienet_start_xmit,
	.ndo_change_mtu	= axienet_change_mtu,
	.ndo_set_mac_address = netdev_set_mac_address,
	.ndo_validate_addr = eth_validate_addr,
	.ndo_set_rx_mode = axienet_set_multicast_list,
	.ndo_do_ioctl = axienet_ioctl,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller = axienet_poll_controller,
#endif
};

/**
 * axienet_ethtools_get_settings - Get Axi Ethernet settings related to PHY.
 * @ndev:	Pointer to net_device structure
 * @ecmd:	Pointer to ethtool_cmd structure
 *
 * This implements ethtool command for getting PHY settings. If PHY could
 * not be found, the function returns -ENODEV. This function calls the
 * relevant PHY ethtool API to get the PHY settings.
 * Issue "ethtool ethX" under linux prompt to execute this function.
 */
static int axienet_ethtools_get_settings(struct net_device *ndev,
					 struct ethtool_cmd *ecmd)
{
	struct axienet_local *lp = netdev_priv(ndev);
	struct phy_device *phydev = lp->phy_dev;
	if (!phydev)
		return -ENODEV;
	return phy_ethtool_gset(phydev, ecmd);
}

/**
 * axienet_ethtools_set_settings - Set PHY settings as passed in the argument.
 * @ndev:	Pointer to net_device structure
 * @ecmd:	Pointer to ethtool_cmd structure
 *
 * This implements ethtool command for setting various PHY settings. If PHY
 * could not be found, the function returns -ENODEV. This function calls the
 * relevant PHY ethtool API to set the PHY.
 * Issue e.g. "ethtool -s ethX speed 1000" under linux prompt to execute this
 * function.
 */
static int axienet_ethtools_set_settings(struct net_device *ndev,
					 struct ethtool_cmd *ecmd)
{
	struct axienet_local *lp = netdev_priv(ndev);
	struct phy_device *phydev = lp->phy_dev;
	if (!phydev)
		return -ENODEV;
	return phy_ethtool_sset(phydev, ecmd);
}

/**
 * axienet_ethtools_get_drvinfo - Get various Axi Ethernet driver information.
 * @ndev:	Pointer to net_device structure
 * @ed:		Pointer to ethtool_drvinfo structure
 *
 * This implements ethtool command for getting the driver information.
 * Issue "ethtool -i ethX" under linux prompt to execute this function.
 */
static void axienet_ethtools_get_drvinfo(struct net_device *ndev,
					 struct ethtool_drvinfo *ed)
{
	strlcpy(ed->driver, DRIVER_NAME, sizeof(ed->driver));
	strlcpy(ed->version, DRIVER_VERSION, sizeof(ed->version));
	ed->regdump_len = sizeof(u32) * AXIENET_REGS_N;
}

/**
 * axienet_ethtools_get_regs_len - Get the total regs length present in the
 *				   AxiEthernet core.
 * @ndev:	Pointer to net_device structure
 *
 * This implements ethtool command for getting the total register length
 * information.
 */
static int axienet_ethtools_get_regs_len(struct net_device *ndev)
{
	return sizeof(u32) * AXIENET_REGS_N;
}

/**
 * axienet_ethtools_get_regs - Dump the contents of all registers present
 *			       in AxiEthernet core.
 * @ndev:	Pointer to net_device structure
 * @regs:	Pointer to ethtool_regs structure
 * @ret:	Void pointer used to return the contents of the registers.
 *
 * This implements ethtool command for getting the Axi Ethernet register dump.
 * Issue "ethtool -d ethX" to execute this function.
 */
static void axienet_ethtools_get_regs(struct net_device *ndev,
				      struct ethtool_regs *regs, void *ret)
{
	u32 *data = (u32 *) ret;
	size_t len = sizeof(u32) * AXIENET_REGS_N;
	struct axienet_local *lp = netdev_priv(ndev);

	regs->version = 0;
	regs->len = len;

	memset(data, 0, len);
	data[0] = axienet_ior(lp, XAE_RAF_OFFSET);
	data[1] = axienet_ior(lp, XAE_TPF_OFFSET);
	data[2] = axienet_ior(lp, XAE_IFGP_OFFSET);
	data[3] = axienet_ior(lp, XAE_IS_OFFSET);
	data[4] = axienet_ior(lp, XAE_IP_OFFSET);
	data[5] = axienet_ior(lp, XAE_IE_OFFSET);
	data[6] = axienet_ior(lp, XAE_TTAG_OFFSET);
	data[7] = axienet_ior(lp, XAE_RTAG_OFFSET);
	data[8] = axienet_ior(lp, XAE_UAWL_OFFSET);
	data[9] = axienet_ior(lp, XAE_UAWU_OFFSET);
	data[10] = axienet_ior(lp, XAE_TPID0_OFFSET);
	data[11] = axienet_ior(lp, XAE_TPID1_OFFSET);
	data[12] = axienet_ior(lp, XAE_PPST_OFFSET);
	data[13] = axienet_ior(lp, XAE_RCW0_OFFSET);
	data[14] = axienet_ior(lp, XAE_RCW1_OFFSET);
	data[15] = axienet_ior(lp, XAE_TC_OFFSET);
	data[16] = axienet_ior(lp, XAE_FCC_OFFSET);
	data[17] = axienet_ior(lp, XAE_EMMC_OFFSET);
	data[18] = axienet_ior(lp, XAE_PHYC_OFFSET);
	data[19] = axienet_ior(lp, XAE_MDIO_MC_OFFSET);
	data[20] = axienet_ior(lp, XAE_MDIO_MCR_OFFSET);
	data[21] = axienet_ior(lp, XAE_MDIO_MWD_OFFSET);
	data[22] = axienet_ior(lp, XAE_MDIO_MRD_OFFSET);
	data[23] = axienet_ior(lp, XAE_MDIO_MIS_OFFSET);
	data[24] = axienet_ior(lp, XAE_MDIO_MIP_OFFSET);
	data[25] = axienet_ior(lp, XAE_MDIO_MIE_OFFSET);
	data[26] = axienet_ior(lp, XAE_MDIO_MIC_OFFSET);
	data[27] = axienet_ior(lp, XAE_UAW0_OFFSET);
	data[28] = axienet_ior(lp, XAE_UAW1_OFFSET);
	data[29] = axienet_ior(lp, XAE_FMI_OFFSET);
	data[30] = axienet_ior(lp, XAE_AF0_OFFSET);
	data[31] = axienet_ior(lp, XAE_AF1_OFFSET);
}

/**
 * axienet_ethtools_get_pauseparam - Get the pause parameter setting for
 *				     Tx and Rx paths.
 * @ndev:	Pointer to net_device structure
 * @epauseparm:	Pointer to ethtool_pauseparam structure.
 *
 * This implements ethtool command for getting axi ethernet pause frame
 * setting. Issue "ethtool -a ethX" to execute this function.
 */
static void
axienet_ethtools_get_pauseparam(struct net_device *ndev,
				struct ethtool_pauseparam *epauseparm)
{
	u32 regval;
	struct axienet_local *lp = netdev_priv(ndev);
	epauseparm->autoneg  = 0;
	regval = axienet_ior(lp, XAE_FCC_OFFSET);
	epauseparm->tx_pause = regval & XAE_FCC_FCTX_MASK;
	epauseparm->rx_pause = regval & XAE_FCC_FCRX_MASK;
}

/**
 * axienet_ethtools_set_pauseparam - Set device pause parameter(flow control)
 *				     settings.
 * @ndev:	Pointer to net_device structure
 * @epauseparm:Pointer to ethtool_pauseparam structure
 *
 * This implements ethtool command for enabling flow control on Rx and Tx
 * paths. Issue "ethtool -A ethX tx on|off" under linux prompt to execute this
 * function.
 */
static int
axienet_ethtools_set_pauseparam(struct net_device *ndev,
				struct ethtool_pauseparam *epauseparm)
{
	u32 regval = 0;
	struct axienet_local *lp = netdev_priv(ndev);

	if (netif_running(ndev)) {
		dev_err(&ndev->dev,
			"%s: Please stop netif before configuration\n",
			ndev->name);
		return -EFAULT;
	}

	regval = axienet_ior(lp, XAE_FCC_OFFSET);
	if (epauseparm->tx_pause)
		regval |= XAE_FCC_FCTX_MASK;
	else
		regval &= ~XAE_FCC_FCTX_MASK;
	if (epauseparm->rx_pause)
		regval |= XAE_FCC_FCRX_MASK;
	else
		regval &= ~XAE_FCC_FCRX_MASK;
	axienet_iow(lp, XAE_FCC_OFFSET, regval);

	return 0;
}

/**
 * axienet_ethtools_get_coalesce - Get DMA interrupt coalescing count.
 * @ndev:	Pointer to net_device structure
 * @ecoalesce:	Pointer to ethtool_coalesce structure
 *
 * This implements ethtool command for getting the DMA interrupt coalescing
 * count on Tx and Rx paths. Issue "ethtool -c ethX" under linux prompt to
 * execute this function.
 */
static int axienet_ethtools_get_coalesce(struct net_device *ndev,
					 struct ethtool_coalesce *ecoalesce)
{
	u32 regval = 0;
	struct axienet_local *lp = netdev_priv(ndev);
	regval = axienet_dma_in32(lp, XAXIDMA_RX_CR_OFFSET);
	ecoalesce->rx_max_coalesced_frames = (regval & XAXIDMA_COALESCE_MASK)
					     >> XAXIDMA_COALESCE_SHIFT;
	regval = axienet_dma_in32(lp, XAXIDMA_TX_CR_OFFSET);
	ecoalesce->tx_max_coalesced_frames = (regval & XAXIDMA_COALESCE_MASK)
					     >> XAXIDMA_COALESCE_SHIFT;
	return 0;
}

/**
 * axienet_ethtools_set_coalesce - Set DMA interrupt coalescing count.
 * @ndev:	Pointer to net_device structure
 * @ecoalesce:	Pointer to ethtool_coalesce structure
 *
 * This implements ethtool command for setting the DMA interrupt coalescing
 * count on Tx and Rx paths. Issue "ethtool -C ethX rx-frames 5" under linux
 * prompt to execute this function.
 */
static int axienet_ethtools_set_coalesce(struct net_device *ndev,
					 struct ethtool_coalesce *ecoalesce)
{
	struct axienet_local *lp = netdev_priv(ndev);

	if (netif_running(ndev)) {
		dev_err(&ndev->dev,
			"%s: Please stop netif before configuration\n",
			ndev->name);
		return -EFAULT;
	}

	if ((ecoalesce->rx_coalesce_usecs) ||
	    (ecoalesce->rx_coalesce_usecs_irq) ||
	    (ecoalesce->rx_max_coalesced_frames_irq) ||
	    (ecoalesce->tx_coalesce_usecs) ||
	    (ecoalesce->tx_coalesce_usecs_irq) ||
	    (ecoalesce->tx_max_coalesced_frames_irq) ||
	    (ecoalesce->stats_block_coalesce_usecs) ||
	    (ecoalesce->use_adaptive_rx_coalesce) ||
	    (ecoalesce->use_adaptive_tx_coalesce) ||
	    (ecoalesce->pkt_rate_low) ||
	    (ecoalesce->rx_coalesce_usecs_low) ||
	    (ecoalesce->rx_max_coalesced_frames_low) ||
	    (ecoalesce->tx_coalesce_usecs_low) ||
	    (ecoalesce->tx_max_coalesced_frames_low) ||
	    (ecoalesce->pkt_rate_high) ||
	    (ecoalesce->rx_coalesce_usecs_high) ||
	    (ecoalesce->rx_max_coalesced_frames_high) ||
	    (ecoalesce->tx_coalesce_usecs_high) ||
	    (ecoalesce->tx_max_coalesced_frames_high) ||
	    (ecoalesce->rate_sample_interval))
		return -EOPNOTSUPP;
	if (ecoalesce->rx_max_coalesced_frames)
		lp->coalesce_count_rx = ecoalesce->rx_max_coalesced_frames;
	if (ecoalesce->tx_max_coalesced_frames)
		lp->coalesce_count_tx = ecoalesce->tx_max_coalesced_frames;

	return 0;
}

static struct ethtool_ops axienet_ethtool_ops = {
	.get_settings   = axienet_ethtools_get_settings,
	.set_settings   = axienet_ethtools_set_settings,
	.get_drvinfo    = axienet_ethtools_get_drvinfo,
	.get_regs_len   = axienet_ethtools_get_regs_len,
	.get_regs       = axienet_ethtools_get_regs,
	.get_link       = ethtool_op_get_link,
	.get_pauseparam = axienet_ethtools_get_pauseparam,
	.set_pauseparam = axienet_ethtools_set_pauseparam,
	.get_coalesce   = axienet_ethtools_get_coalesce,
	.set_coalesce   = axienet_ethtools_set_coalesce,
};

/**
 * axienet_dma_err_handler - Tasklet handler for Axi DMA Error
 * @data:	Data passed
 *
 * Resets the Axi DMA and Axi Ethernet devices, and reconfigures the
 * Tx/Rx BDs.
 */
static void axienet_dma_err_handler(unsigned long data)
{
	u32 axienet_status;
	u32 cr, i;
	int mdio_mcreg;
	struct axienet_local *lp = (struct axienet_local *) data;
	struct net_device *ndev = lp->ndev;
	struct axidma_bd *cur_p;

	axienet_setoptions(ndev, lp->options &
			   ~(XAE_OPTION_TXEN | XAE_OPTION_RXEN));
	mdio_mcreg = axienet_ior(lp, XAE_MDIO_MC_OFFSET);
	axienet_mdio_wait_until_ready(lp);
	/* Disable the MDIO interface till Axi Ethernet Reset is completed.
	 * When we do an Axi Ethernet reset, it resets the complete core
	 * including the MDIO. So if MDIO is not disabled when the reset
	 * process is started, MDIO will be broken afterwards.
	 */
	axienet_iow(lp, XAE_MDIO_MC_OFFSET, (mdio_mcreg &
		    ~XAE_MDIO_MC_MDIOEN_MASK));

	__axienet_device_reset(lp, &ndev->dev, XAXIDMA_TX_CR_OFFSET);
	__axienet_device_reset(lp, &ndev->dev, XAXIDMA_RX_CR_OFFSET);

	axienet_iow(lp, XAE_MDIO_MC_OFFSET, mdio_mcreg);
	axienet_mdio_wait_until_ready(lp);

	for (i = 0; i < TX_BD_NUM; i++) {
		cur_p = &lp->tx_bd_v[i];
		if (cur_p->phys)
			dma_unmap_single(ndev->dev.parent, cur_p->phys,
					 (cur_p->cntrl &
					  XAXIDMA_BD_CTRL_LENGTH_MASK),
					 DMA_TO_DEVICE);
		if (cur_p->app4)
			dev_kfree_skb_irq((struct sk_buff *) cur_p->app4);
		cur_p->phys = 0;
		cur_p->cntrl = 0;
		cur_p->status = 0;
		cur_p->app0 = 0;
		cur_p->app1 = 0;
		cur_p->app2 = 0;
		cur_p->app3 = 0;
		cur_p->app4 = 0;
		cur_p->sw_id_offset = 0;
	}

	for (i = 0; i < RX_BD_NUM; i++) {
		cur_p = &lp->rx_bd_v[i];
		cur_p->status = 0;
		cur_p->app0 = 0;
		cur_p->app1 = 0;
		cur_p->app2 = 0;
		cur_p->app3 = 0;
		cur_p->app4 = 0;
	}

	lp->tx_bd_ci = 0;
	lp->tx_bd_tail = 0;
	lp->rx_bd_ci = 0;

	/* Start updating the Rx channel control register */
	cr = axienet_dma_in32(lp, XAXIDMA_RX_CR_OFFSET);
	/* Update the interrupt coalesce count */
	cr = ((cr & ~XAXIDMA_COALESCE_MASK) |
	      (XAXIDMA_DFT_RX_THRESHOLD << XAXIDMA_COALESCE_SHIFT));
	/* Update the delay timer count */
	cr = ((cr & ~XAXIDMA_DELAY_MASK) |
	      (XAXIDMA_DFT_RX_WAITBOUND << XAXIDMA_DELAY_SHIFT));
	/* Enable coalesce, delay timer and error interrupts */
	cr |= XAXIDMA_IRQ_ALL_MASK;
	/* Finally write to the Rx channel control register */
	axienet_dma_out32(lp, XAXIDMA_RX_CR_OFFSET, cr);

	/* Start updating the Tx channel control register */
	cr = axienet_dma_in32(lp, XAXIDMA_TX_CR_OFFSET);
	/* Update the interrupt coalesce count */
	cr = (((cr & ~XAXIDMA_COALESCE_MASK)) |
	      (XAXIDMA_DFT_TX_THRESHOLD << XAXIDMA_COALESCE_SHIFT));
	/* Update the delay timer count */
	cr = (((cr & ~XAXIDMA_DELAY_MASK)) |
	      (XAXIDMA_DFT_TX_WAITBOUND << XAXIDMA_DELAY_SHIFT));
	/* Enable coalesce, delay timer and error interrupts */
	cr |= XAXIDMA_IRQ_ALL_MASK;
	/* Finally write to the Tx channel control register */
	axienet_dma_out32(lp, XAXIDMA_TX_CR_OFFSET, cr);

	/* Populate the tail pointer and bring the Rx Axi DMA engine out of
	 * halted state. This will make the Rx side ready for reception.
	 */
	axienet_dma_out32(lp, XAXIDMA_RX_CDESC_OFFSET, lp->rx_bd_p);
	cr = axienet_dma_in32(lp, XAXIDMA_RX_CR_OFFSET);
	axienet_dma_out32(lp, XAXIDMA_RX_CR_OFFSET,
			  cr | XAXIDMA_CR_RUNSTOP_MASK);
	axienet_dma_out32(lp, XAXIDMA_RX_TDESC_OFFSET, lp->rx_bd_p +
			  (sizeof(*lp->rx_bd_v) * (RX_BD_NUM - 1)));

	/* Write to the RS (Run-stop) bit in the Tx channel control register.
	 * Tx channel is now ready to run. But only after we write to the
	 * tail pointer register that the Tx channel will start transmitting
	 */
	axienet_dma_out32(lp, XAXIDMA_TX_CDESC_OFFSET, lp->tx_bd_p);
	cr = axienet_dma_in32(lp, XAXIDMA_TX_CR_OFFSET);
	axienet_dma_out32(lp, XAXIDMA_TX_CR_OFFSET,
			  cr | XAXIDMA_CR_RUNSTOP_MASK);

	axienet_status = axienet_ior(lp, XAE_RCW1_OFFSET);
	axienet_status &= ~XAE_RCW1_RX_MASK;
	axienet_iow(lp, XAE_RCW1_OFFSET, axienet_status);

	axienet_status = axienet_ior(lp, XAE_IP_OFFSET);
	if (axienet_status & XAE_INT_RXRJECT_MASK)
		axienet_iow(lp, XAE_IS_OFFSET, XAE_INT_RXRJECT_MASK);
	axienet_iow(lp, XAE_FCC_OFFSET, XAE_FCC_FCRX_MASK);

	/* Sync default options with HW but leave receiver and
	 * transmitter disabled.
	 */
	axienet_setoptions(ndev, lp->options &
			   ~(XAE_OPTION_TXEN | XAE_OPTION_RXEN));
	axienet_set_mac_address(ndev, NULL);
	axienet_set_multicast_list(ndev);
	axienet_setoptions(ndev, lp->options);
}

/**
 * axienet_probe - Axi Ethernet probe function.
 * @pdev:		Pointer to platform device structure.
 *
 * returns: 0, on success
 *	    Non-zero error value on failure.
 *
 * This is the probe routine for Axi Ethernet driver. This is called before
 * any other driver routines are invoked. It allocates and sets up the
 * Ethernet device. Parses through device tree and populates fields of
 * axienet_local. It registers the Ethernet device.
 */
static int axienet_probe(struct platform_device *pdev)
{
	int ret;
	struct device_node *np;
	struct axienet_local *lp;
	struct net_device *ndev;
	u8 mac_addr[6];
	struct resource *ethres, dmares;
	u32 value;

	ndev = alloc_etherdev(sizeof(*lp));
	if (!ndev)
		return -ENOMEM;

	ether_setup(ndev);
	platform_set_drvdata(pdev, ndev);

	SET_NETDEV_DEV(ndev, &pdev->dev);
	ndev->flags &= ~IFF_MULTICAST;  /* clear multicast */
	ndev->features = 0;
	ndev->netdev_ops = &axienet_netdev_ops;
	ndev->ethtool_ops = &axienet_ethtool_ops;

	lp = netdev_priv(ndev);
	lp->ndev = ndev;
	lp->dev = &pdev->dev;
	lp->options = XAE_OPTION_DEFAULTS;
	/* Map device registers */
	ethres = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	lp->regs = devm_ioremap_resource(&pdev->dev, ethres);
	if (!lp->regs) {
		dev_err(&pdev->dev, "could not map Axi Ethernet regs.\n");
		ret = -ENOMEM;
		goto free_netdev;
	}

	/* Setup checksum offload, but default to off if not specified */
	lp->features = 0;

	ret = of_property_read_u32(pdev->dev.of_node, "xlnx,txcsum", &value);
	if (!ret) {
		dev_info(&pdev->dev, "TX_CSUM %d\n", value);

		switch (value) {
		case 1:
			lp->csum_offload_on_tx_path =
				XAE_FEATURE_PARTIAL_TX_CSUM;
			lp->features |= XAE_FEATURE_PARTIAL_TX_CSUM;
			/* Can checksum TCP/UDP over IPv4. */
			ndev->features |= NETIF_F_IP_CSUM | NETIF_F_SG;
			break;
		case 2:
			lp->csum_offload_on_tx_path =
				XAE_FEATURE_FULL_TX_CSUM;
			lp->features |= XAE_FEATURE_FULL_TX_CSUM;
			/* Can checksum TCP/UDP over IPv4. */
			ndev->features |= NETIF_F_IP_CSUM | NETIF_F_SG;
			break;
		default:
			lp->csum_offload_on_tx_path = XAE_NO_CSUM_OFFLOAD;
		}
	}
	ret = of_property_read_u32(pdev->dev.of_node, "xlnx,rxcsum", &value);
	if (!ret) {
		dev_info(&pdev->dev, "RX_CSUM %d\n", value);

		switch (value) {
		case 1:
			lp->csum_offload_on_rx_path =
				XAE_FEATURE_PARTIAL_RX_CSUM;
			lp->features |= XAE_FEATURE_PARTIAL_RX_CSUM;
			break;
		case 2:
			lp->csum_offload_on_rx_path =
				XAE_FEATURE_FULL_RX_CSUM;
			lp->features |= XAE_FEATURE_FULL_RX_CSUM;
			break;
		default:
			lp->csum_offload_on_rx_path = XAE_NO_CSUM_OFFLOAD;
		}
	}
	/* For supporting jumbo frames, the Axi Ethernet hardware must have
	 * a larger Rx/Tx Memory. Typically, the size must be large so that
	 * we can enable jumbo option and start supporting jumbo frames.
	 * Here we check for memory allocated for Rx/Tx in the hardware from
	 * the device-tree and accordingly set flags.
	 */
	of_property_read_u32(pdev->dev.of_node, "xlnx,rxmem", &lp->rxmem);
	of_property_read_u32(pdev->dev.of_node, "xlnx,temac-type",
					&lp->temac_type);
	of_property_read_u32(pdev->dev.of_node, "xlnx,phy-type",
					&lp->phy_type);

	/* Find the DMA node, map the DMA registers, and decode DMA IRQs */
	np = of_parse_phandle(pdev->dev.of_node, "axistream-connected", 0);
	if (IS_ERR(np)) {
		dev_err(&pdev->dev, "could not find DMA node\n");
		ret = PTR_ERR(np);
		goto free_netdev;
	}
	ret = of_address_to_resource(np, 0, &dmares);
	if (ret) {
		dev_err(&pdev->dev, "unable to get DMA resource\n");
		goto free_netdev;
	}
	lp->dma_regs = devm_ioremap_resource(&pdev->dev, &dmares);
	if (!lp->dma_regs) {
		dev_err(&pdev->dev, "could not map DMA regs\n");
		ret = -ENOMEM;
		goto free_netdev;
	}
	lp->rx_irq = irq_of_parse_and_map(np, 1);
	lp->tx_irq = irq_of_parse_and_map(np, 0);
	of_node_put(np);
	if ((lp->rx_irq <= 0) || (lp->tx_irq <= 0)) {
		dev_err(&pdev->dev, "could not determine irqs\n");
		ret = -ENOMEM;
		goto free_netdev;
	}

	/* Retrieve the MAC address */
	ret = of_property_read_u8_array(pdev->dev.of_node,
			"local-mac-address", mac_addr, 6);
	if (ret) {
		dev_err(&pdev->dev, "could not find MAC address\n");
		goto free_netdev;
	}
	axienet_set_mac_address(ndev, (void *) mac_addr);

	lp->coalesce_count_rx = XAXIDMA_DFT_RX_THRESHOLD;
	lp->coalesce_count_tx = XAXIDMA_DFT_TX_THRESHOLD;

	lp->phy_node = of_parse_phandle(pdev->dev.of_node, "phy-handle", 0);
	if (lp->phy_node) {
		ret = axienet_mdio_setup(lp, pdev->dev.of_node);
		if (ret)
			dev_warn(&pdev->dev, "error registering MDIO bus\n");
	}

	ret = register_netdev(lp->ndev);
	if (ret) {
		dev_err(lp->dev, "register_netdev() error (%i)\n", ret);
		goto free_netdev;
	}

	return 0;

free_netdev:
	free_netdev(ndev);

	return ret;
}

static int axienet_remove(struct platform_device *pdev)
{
	struct net_device *ndev = platform_get_drvdata(pdev);
	struct axienet_local *lp = netdev_priv(ndev);

	axienet_mdio_teardown(lp);
	unregister_netdev(ndev);

	if (lp->phy_node)
		of_node_put(lp->phy_node);
	lp->phy_node = NULL;

	free_netdev(ndev);

	return 0;
}

static struct platform_driver axienet_driver = {
	.probe = axienet_probe,
	.remove = axienet_remove,
	.driver = {
		 .owner = THIS_MODULE,
		 .name = "xilinx_axienet",
		 .of_match_table = axienet_of_match,
	},
};

module_platform_driver(axienet_driver);

MODULE_DESCRIPTION("Xilinx Axi Ethernet driver");
MODULE_AUTHOR("Xilinx");
MODULE_LICENSE("GPL");
