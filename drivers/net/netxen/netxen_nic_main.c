/*
 * Copyright (C) 2003 - 2006 NetXen, Inc.
 * All rights reserved.
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *                            
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *                                   
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston,
 * MA  02111-1307, USA.
 * 
 * The full GNU General Public License is included in this distribution
 * in the file called LICENSE.
 * 
 * Contact Information:
 *    info@netxen.com
 * NetXen,
 * 3965 Freedom Circle, Fourth floor,
 * Santa Clara, CA 95054
 *
 *
 *  Main source file for NetXen NIC Driver on Linux
 *
 */

#include "netxen_nic_hw.h"

#include "netxen_nic.h"
#define DEFINE_GLOBAL_RECV_CRB
#include "netxen_nic_phan_reg.h"
#include "netxen_nic_ioctl.h"

#include <linux/dma-mapping.h>
#include <linux/vmalloc.h>

MODULE_DESCRIPTION("NetXen Multi port (1/10) Gigabit Network Driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(NETXEN_NIC_LINUX_VERSIONID);

char netxen_nic_driver_name[] = "netxen";
static char netxen_nic_driver_string[] = "NetXen Network Driver version "
    NETXEN_NIC_LINUX_VERSIONID "-" NETXEN_NIC_BUILD_NO;

#define NETXEN_NETDEV_WEIGHT 120
#define NETXEN_ADAPTER_UP_MAGIC 777

/* Local functions to NetXen NIC driver */
static int __devinit netxen_nic_probe(struct pci_dev *pdev,
				      const struct pci_device_id *ent);
static void __devexit netxen_nic_remove(struct pci_dev *pdev);
static int netxen_nic_open(struct net_device *netdev);
static int netxen_nic_close(struct net_device *netdev);
static int netxen_nic_xmit_frame(struct sk_buff *, struct net_device *);
static void netxen_tx_timeout(struct net_device *netdev);
static void netxen_tx_timeout_task(struct net_device *netdev);
static void netxen_watchdog(unsigned long);
static int netxen_handle_int(struct netxen_adapter *, struct net_device *);
static int netxen_nic_ioctl(struct net_device *netdev,
			    struct ifreq *ifr, int cmd);
static int netxen_nic_poll(struct net_device *dev, int *budget);
#ifdef CONFIG_NET_POLL_CONTROLLER
static void netxen_nic_poll_controller(struct net_device *netdev);
#endif
static irqreturn_t netxen_intr(int irq, void *data);

/*  PCI Device ID Table  */
static struct pci_device_id netxen_pci_tbl[] __devinitdata = {
	{PCI_DEVICE(0x4040, 0x0001)},
	{PCI_DEVICE(0x4040, 0x0002)},
	{PCI_DEVICE(0x4040, 0x0003)},
	{PCI_DEVICE(0x4040, 0x0004)},
	{PCI_DEVICE(0x4040, 0x0005)},
	{0,}
};

MODULE_DEVICE_TABLE(pci, netxen_pci_tbl);

/*
 * netxen_nic_probe()
 *
 * The Linux system will invoke this after identifying the vendor ID and
 * device Id in the pci_tbl supported by this module.
 *
 * A quad port card has one operational PCI config space, (function 0),
 * which is used to access all four ports.
 *
 * This routine will initialize the adapter, and setup the global parameters
 * along with the port's specific structure.
 */
static int __devinit
netxen_nic_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct net_device *netdev = NULL;
	struct netxen_adapter *adapter = NULL;
	struct netxen_port *port = NULL;
	u8 __iomem *mem_ptr = NULL;
	unsigned long mem_base, mem_len;
	int pci_using_dac, i, err;
	int ring;
	struct netxen_recv_context *recv_ctx = NULL;
	struct netxen_rcv_desc_ctx *rcv_desc = NULL;
	struct netxen_cmd_buffer *cmd_buf_arr = NULL;
	u64 mac_addr[FLASH_NUM_PORTS + 1];
	int valid_mac;

	if ((err = pci_enable_device(pdev)))
		return err;
	if (!(pci_resource_flags(pdev, 0) & IORESOURCE_MEM)) {
		err = -ENODEV;
		goto err_out_disable_pdev;
	}

	if ((err = pci_request_regions(pdev, netxen_nic_driver_name)))
		goto err_out_disable_pdev;

	pci_set_master(pdev);
	if ((pci_set_dma_mask(pdev, DMA_64BIT_MASK) == 0) &&
	    (pci_set_consistent_dma_mask(pdev, DMA_64BIT_MASK) == 0))
		pci_using_dac = 1;
	else {
		if ((err = pci_set_dma_mask(pdev, DMA_32BIT_MASK)) ||
		    (err = pci_set_consistent_dma_mask(pdev, DMA_32BIT_MASK)))
			goto err_out_free_res;

		pci_using_dac = 0;
	}

	/* remap phys address */
	mem_base = pci_resource_start(pdev, 0);	/* 0 is for BAR 0 */
	mem_len = pci_resource_len(pdev, 0);

	/* 128 Meg of memory */
	mem_ptr = ioremap(mem_base, NETXEN_PCI_MAPSIZE_BYTES);
	if (mem_ptr == 0UL) {
		printk(KERN_ERR "%s: Cannot ioremap adapter memory aborting."
		       ":%p\n", netxen_nic_driver_name, mem_ptr);
		err = -EIO;
		goto err_out_free_res;
	}

/*
 *      Allocate a adapter structure which will manage all the initialization
 *      as well as the common resources for all ports...
 *      all the ports will have pointer to this adapter as well as Adapter
 *      will have pointers of all the ports structures.
 */

	/* One adapter structure for all 4 ports....   */
	adapter = kzalloc(sizeof(struct netxen_adapter), GFP_KERNEL);
	if (adapter == NULL) {
		printk(KERN_ERR "%s: Could not allocate adapter memory:%d\n",
		       netxen_nic_driver_name,
		       (int)sizeof(struct netxen_adapter));
		err = -ENOMEM;
		goto err_out_iounmap;
	}

	adapter->max_tx_desc_count = MAX_CMD_DESCRIPTORS;
	adapter->max_rx_desc_count = MAX_RCV_DESCRIPTORS;
	adapter->max_jumbo_rx_desc_count = MAX_JUMBO_RCV_DESCRIPTORS;

	pci_set_drvdata(pdev, adapter);

	cmd_buf_arr = (struct netxen_cmd_buffer *)vmalloc(TX_RINGSIZE);
	if (cmd_buf_arr == NULL) {
		err = -ENOMEM;
		goto err_out_free_adapter;
	}
	memset(cmd_buf_arr, 0, TX_RINGSIZE);

	for (i = 0; i < MAX_RCV_CTX; ++i) {
		recv_ctx = &adapter->recv_ctx[i];
		for (ring = 0; ring < NUM_RCV_DESC_RINGS; ring++) {
			rcv_desc = &recv_ctx->rcv_desc[ring];
			switch (RCV_DESC_TYPE(ring)) {
			case RCV_DESC_NORMAL:
				rcv_desc->max_rx_desc_count =
				    adapter->max_rx_desc_count;
				rcv_desc->flags = RCV_DESC_NORMAL;
				rcv_desc->dma_size = RX_DMA_MAP_LEN;
				rcv_desc->skb_size = MAX_RX_BUFFER_LENGTH;
				break;

			case RCV_DESC_JUMBO:
				rcv_desc->max_rx_desc_count =
				    adapter->max_jumbo_rx_desc_count;
				rcv_desc->flags = RCV_DESC_JUMBO;
				rcv_desc->dma_size = RX_JUMBO_DMA_MAP_LEN;
				rcv_desc->skb_size = MAX_RX_JUMBO_BUFFER_LENGTH;
				break;

			}
			rcv_desc->rx_buf_arr = (struct netxen_rx_buffer *)
			    vmalloc(RCV_BUFFSIZE);

			if (rcv_desc->rx_buf_arr == NULL) {
				err = -ENOMEM;
				goto err_out_free_rx_buffer;
			}
			memset(rcv_desc->rx_buf_arr, 0, RCV_BUFFSIZE);
		}

	}

	adapter->ops = kzalloc(sizeof(struct netxen_drvops), GFP_KERNEL);
	if (adapter->ops == NULL) {
		printk(KERN_ERR
		       "%s: Could not allocate memory for adapter->ops:%d\n",
		       netxen_nic_driver_name,
		       (int)sizeof(struct netxen_adapter));
		err = -ENOMEM;
		goto err_out_free_rx_buffer;
	}

	adapter->cmd_buf_arr = cmd_buf_arr;
	adapter->ahw.pci_base = mem_ptr;
	spin_lock_init(&adapter->tx_lock);
	spin_lock_init(&adapter->lock);
	/* initialize the buffers in adapter */
	netxen_initialize_adapter_sw(adapter);
	/*
	 * Set the CRB window to invalid. If any register in window 0 is
	 * accessed it should set the window to 0 and then reset it to 1.
	 */
	adapter->curr_window = 255;
	/*
	 *  Adapter in our case is quad port so initialize it before
	 *  initializing the ports
	 */
	netxen_initialize_adapter_hw(adapter);	/* initialize the adapter */

	netxen_initialize_adapter_ops(adapter);

	init_timer(&adapter->watchdog_timer);
	adapter->ahw.xg_linkup = 0;
	adapter->watchdog_timer.function = &netxen_watchdog;
	adapter->watchdog_timer.data = (unsigned long)adapter;
	INIT_WORK(&adapter->watchdog_task,
		  (void (*)(void *))netxen_watchdog_task, adapter);
	adapter->ahw.pdev = pdev;
	adapter->proc_cmd_buf_counter = 0;
	pci_read_config_byte(pdev, PCI_REVISION_ID, &adapter->ahw.revision_id);

	if (pci_enable_msi(pdev)) {
		adapter->flags &= ~NETXEN_NIC_MSI_ENABLED;
		printk(KERN_WARNING "%s: unable to allocate MSI interrupt"
		       " error\n", netxen_nic_driver_name);
	} else
		adapter->flags |= NETXEN_NIC_MSI_ENABLED;

	if (netxen_is_flash_supported(adapter) == 0 &&
	    netxen_get_flash_mac_addr(adapter, mac_addr) == 0)
		valid_mac = 1;
	else
		valid_mac = 0;

	/* initialize the all the ports */

	for (i = 0; i < adapter->ahw.max_ports; i++) {
		netdev = alloc_etherdev(sizeof(struct netxen_port));
		if (!netdev) {
			printk(KERN_ERR "%s: could not allocate netdev for port"
			       " %d\n", netxen_nic_driver_name, i + 1);
			goto err_out_free_dev;
		}

		SET_MODULE_OWNER(netdev);

		port = netdev_priv(netdev);
		port->netdev = netdev;
		port->pdev = pdev;
		port->adapter = adapter;
		port->portnum = i;	/* Gigabit port number from 0-3 */

		netdev->open = netxen_nic_open;
		netdev->stop = netxen_nic_close;
		netdev->hard_start_xmit = netxen_nic_xmit_frame;
		netdev->get_stats = netxen_nic_get_stats;
		netdev->set_multicast_list = netxen_nic_set_multi;
		netdev->set_mac_address = netxen_nic_set_mac;
		netdev->change_mtu = netxen_nic_change_mtu;
		netdev->do_ioctl = netxen_nic_ioctl;
		netdev->tx_timeout = netxen_tx_timeout;
		netdev->watchdog_timeo = HZ;

		SET_ETHTOOL_OPS(netdev, &netxen_nic_ethtool_ops);
		netdev->poll = netxen_nic_poll;
		netdev->weight = NETXEN_NETDEV_WEIGHT;
#ifdef CONFIG_NET_POLL_CONTROLLER
		netdev->poll_controller = netxen_nic_poll_controller;
#endif
		/* ScatterGather support */
		netdev->features = NETIF_F_SG;
		netdev->features |= NETIF_F_IP_CSUM;
		netdev->features |= NETIF_F_TSO;

		if (pci_using_dac)
			netdev->features |= NETIF_F_HIGHDMA;

		if (valid_mac) {
			unsigned char *p = (unsigned char *)&mac_addr[i];
			netdev->dev_addr[0] = *(p + 5);
			netdev->dev_addr[1] = *(p + 4);
			netdev->dev_addr[2] = *(p + 3);
			netdev->dev_addr[3] = *(p + 2);
			netdev->dev_addr[4] = *(p + 1);
			netdev->dev_addr[5] = *(p + 0);

			memcpy(netdev->perm_addr, netdev->dev_addr,
			       netdev->addr_len);
			if (!is_valid_ether_addr(netdev->perm_addr)) {
				printk(KERN_ERR "%s: Bad MAC address "
				       "%02x:%02x:%02x:%02x:%02x:%02x.\n",
				       netxen_nic_driver_name,
				       netdev->dev_addr[0],
				       netdev->dev_addr[1],
				       netdev->dev_addr[2],
				       netdev->dev_addr[3],
				       netdev->dev_addr[4],
				       netdev->dev_addr[5]);
			} else {
				if (adapter->ops->macaddr_set)
					adapter->ops->macaddr_set(port,
								  netdev->
								  dev_addr);
			}
		}
		INIT_WORK(&adapter->tx_timeout_task,
			  (void (*)(void *))netxen_tx_timeout_task, netdev);
		netif_carrier_off(netdev);
		netif_stop_queue(netdev);

		if ((err = register_netdev(netdev))) {
			printk(KERN_ERR "%s: register_netdev failed port #%d"
			       " aborting\n", netxen_nic_driver_name, i + 1);
			err = -EIO;
			free_netdev(netdev);
			goto err_out_free_dev;
		}
		adapter->port_count++;
		adapter->active_ports = 0;
		adapter->port[i] = port;
	}

	/*
	 * Initialize all the CRB registers here.
	 */
	/* Window = 1 */
	writel(0, NETXEN_CRB_NORMALIZE(adapter, CRB_CMD_PRODUCER_OFFSET));
	writel(0, NETXEN_CRB_NORMALIZE(adapter, CRB_CMD_CONSUMER_OFFSET));
	writel(0, NETXEN_CRB_NORMALIZE(adapter, CRB_HOST_CMD_ADDR_LO));

	netxen_phantom_init(adapter);
	/*
	 * delay a while to ensure that the Pegs are up & running.
	 * Otherwise, we might see some flaky behaviour.
	 */
	udelay(100);

	switch (adapter->ahw.board_type) {
	case NETXEN_NIC_GBE:
		printk("%s: QUAD GbE board initialized\n",
		       netxen_nic_driver_name);
		break;

	case NETXEN_NIC_XGBE:
		printk("%s: XGbE board initialized\n", netxen_nic_driver_name);
		break;
	}

	adapter->driver_mismatch = 0;

	return 0;

      err_out_free_dev:
	if (adapter->flags & NETXEN_NIC_MSI_ENABLED)
		pci_disable_msi(pdev);
	for (i = 0; i < adapter->port_count; i++) {
		port = adapter->port[i];
		if ((port) && (port->netdev)) {
			unregister_netdev(port->netdev);
			free_netdev(port->netdev);
		}
	}
	kfree(adapter->ops);

      err_out_free_rx_buffer:
	for (i = 0; i < MAX_RCV_CTX; ++i) {
		recv_ctx = &adapter->recv_ctx[i];
		for (ring = 0; ring < NUM_RCV_DESC_RINGS; ring++) {
			rcv_desc = &recv_ctx->rcv_desc[ring];
			if (rcv_desc->rx_buf_arr != NULL) {
				vfree(rcv_desc->rx_buf_arr);
				rcv_desc->rx_buf_arr = NULL;
			}
		}
	}

	vfree(cmd_buf_arr);

	kfree(adapter->port);

      err_out_free_adapter:
	pci_set_drvdata(pdev, NULL);
	kfree(adapter);

      err_out_iounmap:
	iounmap(mem_ptr);
      err_out_free_res:
	pci_release_regions(pdev);
      err_out_disable_pdev:
	pci_disable_device(pdev);
	return err;
}

static void __devexit netxen_nic_remove(struct pci_dev *pdev)
{
	struct netxen_adapter *adapter;
	struct netxen_port *port;
	struct netxen_rx_buffer *buffer;
	struct netxen_recv_context *recv_ctx;
	struct netxen_rcv_desc_ctx *rcv_desc;
	int i;
	int ctxid, ring;

	adapter = pci_get_drvdata(pdev);
	if (adapter == NULL)
		return;

	netxen_nic_stop_all_ports(adapter);
	/* leave the hw in the same state as reboot */
	netxen_pinit_from_rom(adapter, 0);
	udelay(500);
	netxen_load_firmware(adapter);

	if ((adapter->flags & NETXEN_NIC_MSI_ENABLED))
		netxen_nic_disable_int(adapter);

	udelay(500);		/* Delay for a while to drain the DMA engines */
	for (i = 0; i < adapter->port_count; i++) {
		port = adapter->port[i];
		if ((port) && (port->netdev)) {
			unregister_netdev(port->netdev);
			free_netdev(port->netdev);
		}
	}

	if ((adapter->flags & NETXEN_NIC_MSI_ENABLED))
		pci_disable_msi(pdev);
	pci_set_drvdata(pdev, NULL);
	if (adapter->is_up == NETXEN_ADAPTER_UP_MAGIC)
		netxen_free_hw_resources(adapter);

	iounmap(adapter->ahw.pci_base);

	pci_release_regions(pdev);
	pci_disable_device(pdev);

	for (ctxid = 0; ctxid < MAX_RCV_CTX; ++ctxid) {
		recv_ctx = &adapter->recv_ctx[ctxid];
		for (ring = 0; ring < NUM_RCV_DESC_RINGS; ring++) {
			rcv_desc = &recv_ctx->rcv_desc[ring];
			for (i = 0; i < rcv_desc->max_rx_desc_count; ++i) {
				buffer = &(rcv_desc->rx_buf_arr[i]);
				if (buffer->state == NETXEN_BUFFER_FREE)
					continue;
				pci_unmap_single(pdev, buffer->dma,
						 rcv_desc->dma_size,
						 PCI_DMA_FROMDEVICE);
				if (buffer->skb != NULL)
					dev_kfree_skb_any(buffer->skb);
			}
			vfree(rcv_desc->rx_buf_arr);
		}
	}

	vfree(adapter->cmd_buf_arr);
	kfree(adapter->ops);
	kfree(adapter);
}

/*
 * Called when a network interface is made active
 * @returns 0 on success, negative value on failure
 */
static int netxen_nic_open(struct net_device *netdev)
{
	struct netxen_port *port = netdev_priv(netdev);
	struct netxen_adapter *adapter = port->adapter;
	struct netxen_rcv_desc_ctx *rcv_desc;
	int err = 0;
	int ctx, ring;

	if (adapter->is_up != NETXEN_ADAPTER_UP_MAGIC) {
		err = netxen_init_firmware(adapter);
		if (err != 0) {
			printk(KERN_ERR "Failed to init firmware\n");
			return -EIO;
		}
		netxen_nic_flash_print(adapter);

		/* setup all the resources for the Phantom... */
		/* this include the descriptors for rcv, tx, and status */
		netxen_nic_clear_stats(adapter);
		err = netxen_nic_hw_resources(adapter);
		if (err) {
			printk(KERN_ERR "Error in setting hw resources:%d\n",
			       err);
			return err;
		}
		if (adapter->ops->init_port
		    && adapter->ops->init_port(adapter, port->portnum) != 0) {
			printk(KERN_ERR "%s: Failed to initialize port %d\n",
			       netxen_nic_driver_name, port->portnum);
			netxen_free_hw_resources(adapter);
			return -EIO;
		}
		if (adapter->ops->init_niu)
			adapter->ops->init_niu(adapter);
		for (ctx = 0; ctx < MAX_RCV_CTX; ++ctx) {
			for (ring = 0; ring < NUM_RCV_DESC_RINGS; ring++) {
				rcv_desc =
				    &adapter->recv_ctx[ctx].rcv_desc[ring];
				netxen_post_rx_buffers(adapter, ctx, ring);
			}
		}
		adapter->is_up = NETXEN_ADAPTER_UP_MAGIC;
	}
	adapter->active_ports++;
	if (adapter->active_ports == 1) {
		err = request_irq(adapter->ahw.pdev->irq, &netxen_intr,
				  SA_SHIRQ | SA_SAMPLE_RANDOM, netdev->name,
				  adapter);
		if (err) {
			printk(KERN_ERR "request_irq failed with: %d\n", err);
			adapter->active_ports--;
			return err;
		}
		adapter->irq = adapter->ahw.pdev->irq;
		if (!adapter->driver_mismatch)
			mod_timer(&adapter->watchdog_timer, jiffies);

		netxen_nic_enable_int(adapter);
	}

	/* Done here again so that even if phantom sw overwrote it,
	 * we set it */
	if (adapter->ops->macaddr_set)
		adapter->ops->macaddr_set(port, netdev->dev_addr);
	netxen_nic_set_link_parameters(port);

	netxen_nic_set_multi(netdev);
	if (!adapter->driver_mismatch)
		netif_start_queue(netdev);

	return 0;
}

/*
 * netxen_nic_close - Disables a network interface entry point
 */
static int netxen_nic_close(struct net_device *netdev)
{
	struct netxen_port *port = netdev_priv(netdev);
	struct netxen_adapter *adapter = port->adapter;
	int i, j;
	struct netxen_cmd_buffer *cmd_buff;
	struct netxen_skb_frag *buffrag;

	netif_carrier_off(netdev);
	netif_stop_queue(netdev);

	/* disable phy_ints */
	if (adapter->ops->disable_phy_interrupts)
		adapter->ops->disable_phy_interrupts(adapter, port->portnum);

	adapter->active_ports--;

	if (!adapter->active_ports) {
		netxen_nic_disable_int(adapter);
		if (adapter->irq)
			free_irq(adapter->irq, adapter);
		cmd_buff = adapter->cmd_buf_arr;
		for (i = 0; i < adapter->max_tx_desc_count; i++) {
			buffrag = cmd_buff->frag_array;
			if (buffrag->dma) {
				pci_unmap_single(port->pdev, buffrag->dma,
						 buffrag->length,
						 PCI_DMA_TODEVICE);
				buffrag->dma = (u64) NULL;
			}
			for (j = 0; j < cmd_buff->frag_count; j++) {
				buffrag++;
				if (buffrag->dma) {
					pci_unmap_page(port->pdev,
						       buffrag->dma,
						       buffrag->length,
						       PCI_DMA_TODEVICE);
					buffrag->dma = (u64) NULL;
				}
			}
			/* Free the skb we received in netxen_nic_xmit_frame */
			if (cmd_buff->skb) {
				dev_kfree_skb_any(cmd_buff->skb);
				cmd_buff->skb = NULL;
			}
			cmd_buff++;
		}
		del_timer_sync(&adapter->watchdog_timer);
	}

	return 0;
}

static int netxen_nic_xmit_frame(struct sk_buff *skb, struct net_device *netdev)
{
	struct netxen_port *port = netdev_priv(netdev);
	struct netxen_adapter *adapter = port->adapter;
	struct netxen_hardware_context *hw = &adapter->ahw;
	unsigned int first_seg_len = skb->len - skb->data_len;
	struct netxen_skb_frag *buffrag;
	unsigned int i;

	u32 producer = 0;
	u32 saved_producer = 0;
	struct cmd_desc_type0 *hwdesc;
	int k;
	struct netxen_cmd_buffer *pbuf = NULL;
	unsigned int tries = 0;
	static int dropped_packet = 0;
	int frag_count;
	u32 local_producer = 0;
	u32 max_tx_desc_count = 0;
	u32 last_cmd_consumer = 0;
	int no_of_desc;

	port->stats.xmitcalled++;
	frag_count = skb_shinfo(skb)->nr_frags + 1;

	if (unlikely(skb->len <= 0)) {
		dev_kfree_skb_any(skb);
		port->stats.badskblen++;
		return NETDEV_TX_OK;
	}

	if (frag_count > MAX_BUFFERS_PER_CMD) {
		printk("%s: %s netxen_nic_xmit_frame: frag_count (%d)"
		       "too large, can handle only %d frags\n",
		       netxen_nic_driver_name, netdev->name,
		       frag_count, MAX_BUFFERS_PER_CMD);
		port->stats.txdropped++;
		if ((++dropped_packet & 0xff) == 0xff)
			printk("%s: %s droppped packets = %d\n",
			       netxen_nic_driver_name, netdev->name,
			       dropped_packet);

		return NETDEV_TX_OK;
	}

	/*
	 * Everything is set up. Now, we just need to transmit it out.
	 * Note that we have to copy the contents of buffer over to
	 * right place. Later on, this can be optimized out by de-coupling the
	 * producer index from the buffer index.
	 */
      retry_getting_window:
	spin_lock_bh(&adapter->tx_lock);
	if (adapter->total_threads == MAX_XMIT_PRODUCERS) {
		spin_unlock_bh(&adapter->tx_lock);
		/*
		 * Yield CPU
		 */
		if (!in_atomic())
			schedule();
		else {
			for (i = 0; i < 20; i++)
				cpu_relax();	/*This a nop instr on i386 */
		}
		goto retry_getting_window;
	}
	local_producer = adapter->cmd_producer;
	/* There 4 fragments per descriptor */
	no_of_desc = (frag_count + 3) >> 2;
	if (skb_shinfo(skb)->gso_size > 0) {
		no_of_desc++;
		if (((skb->nh.iph)->ihl * sizeof(u32)) +
		    ((skb->h.th)->doff * sizeof(u32)) +
		    sizeof(struct ethhdr) >
		    (sizeof(struct cmd_desc_type0) - NET_IP_ALIGN)) {
			no_of_desc++;
		}
	}
	k = adapter->cmd_producer;
	max_tx_desc_count = adapter->max_tx_desc_count;
	last_cmd_consumer = adapter->last_cmd_consumer;
	if ((k + no_of_desc) >=
	    ((last_cmd_consumer <= k) ? last_cmd_consumer + max_tx_desc_count :
	     last_cmd_consumer)) {
		spin_unlock_bh(&adapter->tx_lock);
		if (tries == 0) {
			local_bh_disable();
			netxen_process_cmd_ring((unsigned long)adapter);
			local_bh_enable();
			++tries;
			goto retry_getting_window;
		} else {
			port->stats.nocmddescriptor++;
			DPRINTK(ERR, "No command descriptors available,"
				" producer = %d, consumer = %d count=%llu,"
				" dropping packet\n", producer,
				adapter->last_cmd_consumer,
				port->stats.nocmddescriptor);

			spin_lock_bh(&adapter->tx_lock);
			netif_stop_queue(netdev);
			port->flags |= NETXEN_NETDEV_STATUS;
			spin_unlock_bh(&adapter->tx_lock);
			return NETDEV_TX_BUSY;
		}
	}
	k = get_index_range(k, max_tx_desc_count, no_of_desc);
	adapter->cmd_producer = k;
	adapter->total_threads++;
	adapter->num_threads++;

	spin_unlock_bh(&adapter->tx_lock);
	/* Copy the descriptors into the hardware    */
	producer = local_producer;
	saved_producer = producer;
	hwdesc = &hw->cmd_desc_head[producer];
	memset(hwdesc, 0, sizeof(struct cmd_desc_type0));
	/* Take skb->data itself */
	pbuf = &adapter->cmd_buf_arr[producer];
	if (skb_shinfo(skb)->gso_size > 0) {
		pbuf->mss = skb_shinfo(skb)->gso_size;
		hwdesc->mss = skb_shinfo(skb)->gso_size;
	} else {
		pbuf->mss = 0;
		hwdesc->mss = 0;
	}
	pbuf->no_of_descriptors = no_of_desc;
	pbuf->total_length = skb->len;
	pbuf->skb = skb;
	pbuf->cmd = TX_ETHER_PKT;
	pbuf->frag_count = frag_count;
	pbuf->port = port->portnum;
	buffrag = &pbuf->frag_array[0];
	buffrag->dma = pci_map_single(port->pdev, skb->data, first_seg_len,
				      PCI_DMA_TODEVICE);
	buffrag->length = first_seg_len;
	CMD_DESC_TOTAL_LENGTH_WRT(hwdesc, skb->len);
	hwdesc->num_of_buffers = frag_count;
	hwdesc->opcode = TX_ETHER_PKT;

	CMD_DESC_PORT_WRT(hwdesc, port->portnum);
	hwdesc->buffer1_length = cpu_to_le16(first_seg_len);
	hwdesc->addr_buffer1 = cpu_to_le64(buffrag->dma);

	for (i = 1, k = 1; i < frag_count; i++, k++) {
		struct skb_frag_struct *frag;
		int len, temp_len;
		unsigned long offset;
		dma_addr_t temp_dma;

		/* move to next desc. if there is a need */
		if ((i & 0x3) == 0) {
			k = 0;
			producer = get_next_index(producer,
						  adapter->max_tx_desc_count);
			hwdesc = &hw->cmd_desc_head[producer];
			memset(hwdesc, 0, sizeof(struct cmd_desc_type0));
		}
		frag = &skb_shinfo(skb)->frags[i - 1];
		len = frag->size;
		offset = frag->page_offset;

		temp_len = len;
		temp_dma = pci_map_page(port->pdev, frag->page, offset,
					len, PCI_DMA_TODEVICE);

		buffrag++;
		buffrag->dma = temp_dma;
		buffrag->length = temp_len;

		DPRINTK(INFO, "for loop. i=%d k=%d\n", i, k);
		switch (k) {
		case 0:
			hwdesc->buffer1_length = cpu_to_le16(temp_len);
			hwdesc->addr_buffer1 = cpu_to_le64(temp_dma);
			break;
		case 1:
			hwdesc->buffer2_length = cpu_to_le16(temp_len);
			hwdesc->addr_buffer2 = cpu_to_le64(temp_dma);
			break;
		case 2:
			hwdesc->buffer3_length = cpu_to_le16(temp_len);
			hwdesc->addr_buffer3 = cpu_to_le64(temp_dma);
			break;
		case 3:
			hwdesc->buffer4_length = temp_len;
			hwdesc->addr_buffer4 = cpu_to_le64(temp_dma);
			break;
		}
		frag++;
	}
	producer = get_next_index(producer, adapter->max_tx_desc_count);

	/* might change opcode to TX_TCP_LSO */
	netxen_tso_check(adapter, &hw->cmd_desc_head[saved_producer], skb);

	/* For LSO, we need to copy the MAC/IP/TCP headers into
	 * the descriptor ring
	 */
	if (hw->cmd_desc_head[saved_producer].opcode == TX_TCP_LSO) {
		int hdr_len, first_hdr_len, more_hdr;
		hdr_len = hw->cmd_desc_head[saved_producer].total_hdr_length;
		if (hdr_len > (sizeof(struct cmd_desc_type0) - NET_IP_ALIGN)) {
			first_hdr_len =
			    sizeof(struct cmd_desc_type0) - NET_IP_ALIGN;
			more_hdr = 1;
		} else {
			first_hdr_len = hdr_len;
			more_hdr = 0;
		}
		/* copy the MAC/IP/TCP headers to the cmd descriptor list */
		hwdesc = &hw->cmd_desc_head[producer];

		/* copy the first 64 bytes */
		memcpy(((void *)hwdesc) + NET_IP_ALIGN,
		       (void *)(skb->data), first_hdr_len);
		producer = get_next_index(producer, max_tx_desc_count);

		if (more_hdr) {
			hwdesc = &hw->cmd_desc_head[producer];
			/* copy the next 64 bytes - should be enough except
			 * for pathological case
			 */
			memcpy((void *)hwdesc, (void *)(skb->data) +
			       first_hdr_len, hdr_len - first_hdr_len);
			producer = get_next_index(producer, max_tx_desc_count);
		}
	}
	spin_lock_bh(&adapter->tx_lock);
	port->stats.txbytes +=
	    CMD_DESC_TOTAL_LENGTH(&hw->cmd_desc_head[saved_producer]);
	/* Code to update the adapter considering how many producer threads
	   are currently working */
	if ((--adapter->num_threads) == 0) {
		/* This is the last thread */
		u32 crb_producer = adapter->cmd_producer;
		writel(crb_producer,
		       NETXEN_CRB_NORMALIZE(adapter, CRB_CMD_PRODUCER_OFFSET));
		wmb();
		adapter->total_threads = 0;
	} else {
		u32 crb_producer = 0;
		crb_producer =
		    readl(NETXEN_CRB_NORMALIZE
			  (adapter, CRB_CMD_PRODUCER_OFFSET));
		if (crb_producer == local_producer) {
			crb_producer = get_index_range(crb_producer,
						       max_tx_desc_count,
						       no_of_desc);
			writel(crb_producer,
			       NETXEN_CRB_NORMALIZE(adapter,
						    CRB_CMD_PRODUCER_OFFSET));
			wmb();
		}
	}

	port->stats.xmitfinished++;
	spin_unlock_bh(&adapter->tx_lock);

	netdev->trans_start = jiffies;

	DPRINTK(INFO, "wrote CMD producer %x to phantom\n", producer);

	DPRINTK(INFO, "Done. Send\n");
	return NETDEV_TX_OK;
}

static void netxen_watchdog(unsigned long v)
{
	struct netxen_adapter *adapter = (struct netxen_adapter *)v;
	schedule_work(&adapter->watchdog_task);
}

static void netxen_tx_timeout(struct net_device *netdev)
{
	struct netxen_port *port = (struct netxen_port *)netdev_priv(netdev);
	struct netxen_adapter *adapter = port->adapter;

	schedule_work(&adapter->tx_timeout_task);
}

static void netxen_tx_timeout_task(struct net_device *netdev)
{
	struct netxen_port *port = (struct netxen_port *)netdev_priv(netdev);
	unsigned long flags;

	printk(KERN_ERR "%s %s: transmit timeout, resetting.\n",
	       netxen_nic_driver_name, netdev->name);

	spin_lock_irqsave(&port->adapter->lock, flags);
	netxen_nic_close(netdev);
	netxen_nic_open(netdev);
	spin_unlock_irqrestore(&port->adapter->lock, flags);
	netdev->trans_start = jiffies;
	netif_wake_queue(netdev);
}

static int
netxen_handle_int(struct netxen_adapter *adapter, struct net_device *netdev)
{
	u32 ret = 0;

	DPRINTK(INFO, "Entered handle ISR\n");

	adapter->stats.ints++;

	if (!(adapter->flags & NETXEN_NIC_MSI_ENABLED)) {
		int count = 0;
		u32 mask;
		netxen_nic_disable_int(adapter);
		/* Window = 0 or 1 */
		do {
			writel(0xffffffff, (void __iomem *)
			       (adapter->ahw.pci_base + ISR_INT_TARGET_STATUS));
			mask = readl((void __iomem *)
				     (adapter->ahw.pci_base + ISR_INT_VECTOR));
		} while (((mask & 0x80) != 0) && (++count < 32));
		if ((mask & 0x80) != 0)
			printk("Could not disable interrupt completely\n");

	}
	adapter->stats.hostints++;

	if (netxen_nic_rx_has_work(adapter) || netxen_nic_tx_has_work(adapter)) {
		if (netif_rx_schedule_prep(netdev)) {
			/*
			 * Interrupts are already disabled.
			 */
			__netif_rx_schedule(netdev);
		} else {
			static unsigned int intcount = 0;
			if ((++intcount & 0xfff) == 0xfff)
				printk(KERN_ERR
				       "%s: %s interrupt %d while in poll\n",
				       netxen_nic_driver_name, netdev->name,
				       intcount);
		}
		ret = 1;
	}

	if (ret == 0) {
		netxen_nic_enable_int(adapter);
	}

	return ret;
}

/*
 * netxen_intr - Interrupt Handler
 * @irq: interrupt number
 * data points to adapter stucture (which may be handling more than 1 port
 */
irqreturn_t netxen_intr(int irq, void *data)
{
	struct netxen_adapter *adapter;
	struct netxen_port *port;
	struct net_device *netdev;
	int i;

	if (unlikely(!irq)) {
		return IRQ_NONE;	/* Not our interrupt */
	}

	adapter = (struct netxen_adapter *)data;
	for (i = 0; i < adapter->ahw.max_ports; i++) {
		port = adapter->port[i];
		netdev = port->netdev;

		/* process our status queue (for all 4 ports) */
		netxen_handle_int(adapter, netdev);
	}

	return IRQ_HANDLED;
}

static int netxen_nic_poll(struct net_device *netdev, int *budget)
{
	struct netxen_port *port = (struct netxen_port *)netdev_priv(netdev);
	struct netxen_adapter *adapter = port->adapter;
	int work_to_do = min(*budget, netdev->quota);
	int done = 1;
	int ctx;
	int this_work_done;

	DPRINTK(INFO, "polling for %d descriptors\n", *budget);
	port->stats.polled++;

	adapter->work_done = 0;
	for (ctx = 0; ctx < MAX_RCV_CTX; ++ctx) {
		/*
		 * Fairness issue. This will give undue weight to the
		 * receive context 0.
		 */

		/*
		 * To avoid starvation, we give each of our receivers,
		 * a fraction of the quota. Sometimes, it might happen that we
		 * have enough quota to process every packet, but since all the
		 * packets are on one context, it gets only half of the quota,
		 * and ends up not processing it.
		 */
		this_work_done = netxen_process_rcv_ring(adapter, ctx,
							 work_to_do /
							 MAX_RCV_CTX);
		adapter->work_done += this_work_done;
	}

	netdev->quota -= adapter->work_done;
	*budget -= adapter->work_done;

	if (adapter->work_done >= work_to_do
	    && netxen_nic_rx_has_work(adapter) != 0)
		done = 0;

	netxen_process_cmd_ring((unsigned long)adapter);

	DPRINTK(INFO, "new work_done: %d work_to_do: %d\n",
		adapter->work_done, work_to_do);
	if (done) {
		netif_rx_complete(netdev);
		netxen_nic_enable_int(adapter);
	}

	return (done ? 0 : 1);
}

#ifdef CONFIG_NET_POLL_CONTROLLER
static void netxen_nic_poll_controller(struct net_device *netdev)
{
	struct netxen_port *port = netdev_priv(netdev);
	struct netxen_adapter *adapter = port->adapter;
	disable_irq(adapter->irq);
	netxen_intr(adapter->irq, adapter);
	enable_irq(adapter->irq);
}
#endif
/*
 * netxen_nic_ioctl ()    We provide the tcl/phanmon support through these
 * ioctls.
 */
static int
netxen_nic_ioctl(struct net_device *netdev, struct ifreq *ifr, int cmd)
{
	int err = 0;
	struct netxen_port *port = netdev_priv(netdev);
	struct netxen_adapter *adapter = port->adapter;

	DPRINTK(INFO, "doing ioctl for %s\n", netdev->name);
	switch (cmd) {
	case NETXEN_NIC_CMD:
		err = netxen_nic_do_ioctl(adapter, (void *)ifr->ifr_data, port);
		break;

	case NETXEN_NIC_NAME:
		DPRINTK(INFO, "ioctl cmd for NetXen\n");
		if (ifr->ifr_data) {
			put_user(port->portnum, (u16 __user *) ifr->ifr_data);
		}
		break;

	default:
		DPRINTK(INFO, "ioctl cmd %x not supported\n", cmd);
		err = -EOPNOTSUPP;
		break;
	}

	return err;
}

static struct pci_driver netxen_driver = {
	.name = netxen_nic_driver_name,
	.id_table = netxen_pci_tbl,
	.probe = netxen_nic_probe,
	.remove = __devexit_p(netxen_nic_remove)
};

/* Driver Registration on NetXen card    */

static int __init netxen_init_module(void)
{
	printk(KERN_INFO "%s \n", netxen_nic_driver_string);

	return pci_module_init(&netxen_driver);
}

module_init(netxen_init_module);

static void __exit netxen_exit_module(void)
{
	/*
	 * Wait for some time to allow the dma to drain, if any.
	 */
	mdelay(5);
	pci_unregister_driver(&netxen_driver);
}

module_exit(netxen_exit_module);
