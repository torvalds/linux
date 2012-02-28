/*
 * Copyright (C) 2003 - 2009 NetXen, Inc.
 * Copyright (C) 2009 - QLogic Corporation.
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
 * in the file called "COPYING".
 *
 */

#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/interrupt.h>
#include "netxen_nic_hw.h"

#include "netxen_nic.h"

#include <linux/dma-mapping.h>
#include <linux/if_vlan.h>
#include <net/ip.h>
#include <linux/ipv6.h>
#include <linux/inetdevice.h>
#include <linux/sysfs.h>
#include <linux/aer.h>

MODULE_DESCRIPTION("QLogic/NetXen (1/10) GbE Intelligent Ethernet Driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(NETXEN_NIC_LINUX_VERSIONID);
MODULE_FIRMWARE(NX_UNIFIED_ROMIMAGE_NAME);

char netxen_nic_driver_name[] = "netxen_nic";
static char netxen_nic_driver_string[] = "QLogic/NetXen Network Driver v"
    NETXEN_NIC_LINUX_VERSIONID;

static int port_mode = NETXEN_PORT_MODE_AUTO_NEG;

/* Default to restricted 1G auto-neg mode */
static int wol_port_mode = 5;

static int use_msi = 1;

static int use_msi_x = 1;

static int auto_fw_reset = AUTO_FW_RESET_ENABLED;
module_param(auto_fw_reset, int, 0644);
MODULE_PARM_DESC(auto_fw_reset,"Auto firmware reset (0=disabled, 1=enabled");

static int __devinit netxen_nic_probe(struct pci_dev *pdev,
		const struct pci_device_id *ent);
static void __devexit netxen_nic_remove(struct pci_dev *pdev);
static int netxen_nic_open(struct net_device *netdev);
static int netxen_nic_close(struct net_device *netdev);
static netdev_tx_t netxen_nic_xmit_frame(struct sk_buff *,
					       struct net_device *);
static void netxen_tx_timeout(struct net_device *netdev);
static void netxen_tx_timeout_task(struct work_struct *work);
static void netxen_fw_poll_work(struct work_struct *work);
static void netxen_schedule_work(struct netxen_adapter *adapter,
		work_func_t func, int delay);
static void netxen_cancel_fw_work(struct netxen_adapter *adapter);
static int netxen_nic_poll(struct napi_struct *napi, int budget);
#ifdef CONFIG_NET_POLL_CONTROLLER
static void netxen_nic_poll_controller(struct net_device *netdev);
#endif

static void netxen_create_sysfs_entries(struct netxen_adapter *adapter);
static void netxen_remove_sysfs_entries(struct netxen_adapter *adapter);
static void netxen_create_diag_entries(struct netxen_adapter *adapter);
static void netxen_remove_diag_entries(struct netxen_adapter *adapter);

static int nx_dev_request_aer(struct netxen_adapter *adapter);
static int nx_decr_dev_ref_cnt(struct netxen_adapter *adapter);
static int netxen_can_start_firmware(struct netxen_adapter *adapter);

static irqreturn_t netxen_intr(int irq, void *data);
static irqreturn_t netxen_msi_intr(int irq, void *data);
static irqreturn_t netxen_msix_intr(int irq, void *data);

static void netxen_free_vlan_ip_list(struct netxen_adapter *);
static void netxen_restore_indev_addr(struct net_device *dev, unsigned long);
static struct rtnl_link_stats64 *netxen_nic_get_stats(struct net_device *dev,
						      struct rtnl_link_stats64 *stats);
static int netxen_nic_set_mac(struct net_device *netdev, void *p);

/*  PCI Device ID Table  */
#define ENTRY(device) \
	{PCI_DEVICE(PCI_VENDOR_ID_NETXEN, (device)), \
	.class = PCI_CLASS_NETWORK_ETHERNET << 8, .class_mask = ~0}

static DEFINE_PCI_DEVICE_TABLE(netxen_pci_tbl) = {
	ENTRY(PCI_DEVICE_ID_NX2031_10GXSR),
	ENTRY(PCI_DEVICE_ID_NX2031_10GCX4),
	ENTRY(PCI_DEVICE_ID_NX2031_4GCU),
	ENTRY(PCI_DEVICE_ID_NX2031_IMEZ),
	ENTRY(PCI_DEVICE_ID_NX2031_HMEZ),
	ENTRY(PCI_DEVICE_ID_NX2031_XG_MGMT),
	ENTRY(PCI_DEVICE_ID_NX2031_XG_MGMT2),
	ENTRY(PCI_DEVICE_ID_NX3031),
	{0,}
};

MODULE_DEVICE_TABLE(pci, netxen_pci_tbl);

static uint32_t crb_cmd_producer[4] = {
	CRB_CMD_PRODUCER_OFFSET, CRB_CMD_PRODUCER_OFFSET_1,
	CRB_CMD_PRODUCER_OFFSET_2, CRB_CMD_PRODUCER_OFFSET_3
};

void
netxen_nic_update_cmd_producer(struct netxen_adapter *adapter,
		struct nx_host_tx_ring *tx_ring)
{
	NXWRIO(adapter, tx_ring->crb_cmd_producer, tx_ring->producer);
}

static uint32_t crb_cmd_consumer[4] = {
	CRB_CMD_CONSUMER_OFFSET, CRB_CMD_CONSUMER_OFFSET_1,
	CRB_CMD_CONSUMER_OFFSET_2, CRB_CMD_CONSUMER_OFFSET_3
};

static inline void
netxen_nic_update_cmd_consumer(struct netxen_adapter *adapter,
		struct nx_host_tx_ring *tx_ring)
{
	NXWRIO(adapter, tx_ring->crb_cmd_consumer, tx_ring->sw_consumer);
}

static uint32_t msi_tgt_status[8] = {
	ISR_INT_TARGET_STATUS, ISR_INT_TARGET_STATUS_F1,
	ISR_INT_TARGET_STATUS_F2, ISR_INT_TARGET_STATUS_F3,
	ISR_INT_TARGET_STATUS_F4, ISR_INT_TARGET_STATUS_F5,
	ISR_INT_TARGET_STATUS_F6, ISR_INT_TARGET_STATUS_F7
};

static struct netxen_legacy_intr_set legacy_intr[] = NX_LEGACY_INTR_CONFIG;

static inline void netxen_nic_disable_int(struct nx_host_sds_ring *sds_ring)
{
	struct netxen_adapter *adapter = sds_ring->adapter;

	NXWRIO(adapter, sds_ring->crb_intr_mask, 0);
}

static inline void netxen_nic_enable_int(struct nx_host_sds_ring *sds_ring)
{
	struct netxen_adapter *adapter = sds_ring->adapter;

	NXWRIO(adapter, sds_ring->crb_intr_mask, 0x1);

	if (!NETXEN_IS_MSI_FAMILY(adapter))
		NXWRIO(adapter, adapter->tgt_mask_reg, 0xfbff);
}

static int
netxen_alloc_sds_rings(struct netxen_recv_context *recv_ctx, int count)
{
	int size = sizeof(struct nx_host_sds_ring) * count;

	recv_ctx->sds_rings = kzalloc(size, GFP_KERNEL);

	return recv_ctx->sds_rings == NULL;
}

static void
netxen_free_sds_rings(struct netxen_recv_context *recv_ctx)
{
	if (recv_ctx->sds_rings != NULL)
		kfree(recv_ctx->sds_rings);

	recv_ctx->sds_rings = NULL;
}

static int
netxen_napi_add(struct netxen_adapter *adapter, struct net_device *netdev)
{
	int ring;
	struct nx_host_sds_ring *sds_ring;
	struct netxen_recv_context *recv_ctx = &adapter->recv_ctx;

	if (netxen_alloc_sds_rings(recv_ctx, adapter->max_sds_rings))
		return -ENOMEM;

	for (ring = 0; ring < adapter->max_sds_rings; ring++) {
		sds_ring = &recv_ctx->sds_rings[ring];
		netif_napi_add(netdev, &sds_ring->napi,
				netxen_nic_poll, NETXEN_NETDEV_WEIGHT);
	}

	return 0;
}

static void
netxen_napi_del(struct netxen_adapter *adapter)
{
	int ring;
	struct nx_host_sds_ring *sds_ring;
	struct netxen_recv_context *recv_ctx = &adapter->recv_ctx;

	for (ring = 0; ring < adapter->max_sds_rings; ring++) {
		sds_ring = &recv_ctx->sds_rings[ring];
		netif_napi_del(&sds_ring->napi);
	}

	netxen_free_sds_rings(&adapter->recv_ctx);
}

static void
netxen_napi_enable(struct netxen_adapter *adapter)
{
	int ring;
	struct nx_host_sds_ring *sds_ring;
	struct netxen_recv_context *recv_ctx = &adapter->recv_ctx;

	for (ring = 0; ring < adapter->max_sds_rings; ring++) {
		sds_ring = &recv_ctx->sds_rings[ring];
		napi_enable(&sds_ring->napi);
		netxen_nic_enable_int(sds_ring);
	}
}

static void
netxen_napi_disable(struct netxen_adapter *adapter)
{
	int ring;
	struct nx_host_sds_ring *sds_ring;
	struct netxen_recv_context *recv_ctx = &adapter->recv_ctx;

	for (ring = 0; ring < adapter->max_sds_rings; ring++) {
		sds_ring = &recv_ctx->sds_rings[ring];
		netxen_nic_disable_int(sds_ring);
		napi_synchronize(&sds_ring->napi);
		napi_disable(&sds_ring->napi);
	}
}

static int nx_set_dma_mask(struct netxen_adapter *adapter)
{
	struct pci_dev *pdev = adapter->pdev;
	uint64_t mask, cmask;

	adapter->pci_using_dac = 0;

	mask = DMA_BIT_MASK(32);
	cmask = DMA_BIT_MASK(32);

	if (NX_IS_REVISION_P2(adapter->ahw.revision_id)) {
#ifndef CONFIG_IA64
		mask = DMA_BIT_MASK(35);
#endif
	} else {
		mask = DMA_BIT_MASK(39);
		cmask = mask;
	}

	if (pci_set_dma_mask(pdev, mask) == 0 &&
		pci_set_consistent_dma_mask(pdev, cmask) == 0) {
		adapter->pci_using_dac = 1;
		return 0;
	}

	return -EIO;
}

/* Update addressable range if firmware supports it */
static int
nx_update_dma_mask(struct netxen_adapter *adapter)
{
	int change, shift, err;
	uint64_t mask, old_mask, old_cmask;
	struct pci_dev *pdev = adapter->pdev;

	change = 0;

	shift = NXRD32(adapter, CRB_DMA_SHIFT);
	if (shift > 32)
		return 0;

	if (NX_IS_REVISION_P3(adapter->ahw.revision_id) && (shift > 9))
		change = 1;
	else if ((adapter->ahw.revision_id == NX_P2_C1) && (shift <= 4))
		change = 1;

	if (change) {
		old_mask = pdev->dma_mask;
		old_cmask = pdev->dev.coherent_dma_mask;

		mask = DMA_BIT_MASK(32+shift);

		err = pci_set_dma_mask(pdev, mask);
		if (err)
			goto err_out;

		if (NX_IS_REVISION_P3(adapter->ahw.revision_id)) {

			err = pci_set_consistent_dma_mask(pdev, mask);
			if (err)
				goto err_out;
		}
		dev_info(&pdev->dev, "using %d-bit dma mask\n", 32+shift);
	}

	return 0;

err_out:
	pci_set_dma_mask(pdev, old_mask);
	pci_set_consistent_dma_mask(pdev, old_cmask);
	return err;
}

static int
netxen_check_hw_init(struct netxen_adapter *adapter, int first_boot)
{
	u32 val, timeout;

	if (first_boot == 0x55555555) {
		/* This is the first boot after power up */
		NXWR32(adapter, NETXEN_CAM_RAM(0x1fc), NETXEN_BDINFO_MAGIC);

		if (!NX_IS_REVISION_P2(adapter->ahw.revision_id))
			return 0;

		/* PCI bus master workaround */
		first_boot = NXRD32(adapter, NETXEN_PCIE_REG(0x4));
		if (!(first_boot & 0x4)) {
			first_boot |= 0x4;
			NXWR32(adapter, NETXEN_PCIE_REG(0x4), first_boot);
			NXRD32(adapter, NETXEN_PCIE_REG(0x4));
		}

		/* This is the first boot after power up */
		first_boot = NXRD32(adapter, NETXEN_ROMUSB_GLB_SW_RESET);
		if (first_boot != 0x80000f) {
			/* clear the register for future unloads/loads */
			NXWR32(adapter, NETXEN_CAM_RAM(0x1fc), 0);
			return -EIO;
		}

		/* Start P2 boot loader */
		val = NXRD32(adapter, NETXEN_ROMUSB_GLB_PEGTUNE_DONE);
		NXWR32(adapter, NETXEN_ROMUSB_GLB_PEGTUNE_DONE, val | 0x1);
		timeout = 0;
		do {
			msleep(1);
			val = NXRD32(adapter, NETXEN_CAM_RAM(0x1fc));

			if (++timeout > 5000)
				return -EIO;

		} while (val == NETXEN_BDINFO_MAGIC);
	}
	return 0;
}

static void netxen_set_port_mode(struct netxen_adapter *adapter)
{
	u32 val, data;

	val = adapter->ahw.board_type;
	if ((val == NETXEN_BRDTYPE_P3_HMEZ) ||
		(val == NETXEN_BRDTYPE_P3_XG_LOM)) {
		if (port_mode == NETXEN_PORT_MODE_802_3_AP) {
			data = NETXEN_PORT_MODE_802_3_AP;
			NXWR32(adapter, NETXEN_PORT_MODE_ADDR, data);
		} else if (port_mode == NETXEN_PORT_MODE_XG) {
			data = NETXEN_PORT_MODE_XG;
			NXWR32(adapter, NETXEN_PORT_MODE_ADDR, data);
		} else if (port_mode == NETXEN_PORT_MODE_AUTO_NEG_1G) {
			data = NETXEN_PORT_MODE_AUTO_NEG_1G;
			NXWR32(adapter, NETXEN_PORT_MODE_ADDR, data);
		} else if (port_mode == NETXEN_PORT_MODE_AUTO_NEG_XG) {
			data = NETXEN_PORT_MODE_AUTO_NEG_XG;
			NXWR32(adapter, NETXEN_PORT_MODE_ADDR, data);
		} else {
			data = NETXEN_PORT_MODE_AUTO_NEG;
			NXWR32(adapter, NETXEN_PORT_MODE_ADDR, data);
		}

		if ((wol_port_mode != NETXEN_PORT_MODE_802_3_AP) &&
			(wol_port_mode != NETXEN_PORT_MODE_XG) &&
			(wol_port_mode != NETXEN_PORT_MODE_AUTO_NEG_1G) &&
			(wol_port_mode != NETXEN_PORT_MODE_AUTO_NEG_XG)) {
			wol_port_mode = NETXEN_PORT_MODE_AUTO_NEG;
		}
		NXWR32(adapter, NETXEN_WOL_PORT_MODE, wol_port_mode);
	}
}

#define PCI_CAP_ID_GEN  0x10

static void netxen_pcie_strap_init(struct netxen_adapter *adapter)
{
	u32 pdevfuncsave;
	u32 c8c9value = 0;
	u32 chicken = 0;
	u32 control = 0;
	int i, pos;
	struct pci_dev *pdev;

	pdev = adapter->pdev;

	chicken = NXRD32(adapter, NETXEN_PCIE_REG(PCIE_CHICKEN3));
	/* clear chicken3.25:24 */
	chicken &= 0xFCFFFFFF;
	/*
	 * if gen1 and B0, set F1020 - if gen 2, do nothing
	 * if gen2 set to F1000
	 */
	pos = pci_find_capability(pdev, PCI_CAP_ID_GEN);
	if (pos == 0xC0) {
		pci_read_config_dword(pdev, pos + 0x10, &control);
		if ((control & 0x000F0000) != 0x00020000) {
			/*  set chicken3.24 if gen1 */
			chicken |= 0x01000000;
		}
		dev_info(&adapter->pdev->dev, "Gen2 strapping detected\n");
		c8c9value = 0xF1000;
	} else {
		/* set chicken3.24 if gen1 */
		chicken |= 0x01000000;
		dev_info(&adapter->pdev->dev, "Gen1 strapping detected\n");
		if (adapter->ahw.revision_id == NX_P3_B0)
			c8c9value = 0xF1020;
		else
			c8c9value = 0;
	}

	NXWR32(adapter, NETXEN_PCIE_REG(PCIE_CHICKEN3), chicken);

	if (!c8c9value)
		return;

	pdevfuncsave = pdev->devfn;
	if (pdevfuncsave & 0x07)
		return;

	for (i = 0; i < 8; i++) {
		pci_read_config_dword(pdev, pos + 8, &control);
		pci_read_config_dword(pdev, pos + 8, &control);
		pci_write_config_dword(pdev, pos + 8, c8c9value);
		pdev->devfn++;
	}
	pdev->devfn = pdevfuncsave;
}

static void netxen_set_msix_bit(struct pci_dev *pdev, int enable)
{
	u32 control;
	int pos;

	pos = pci_find_capability(pdev, PCI_CAP_ID_MSIX);
	if (pos) {
		pci_read_config_dword(pdev, pos, &control);
		if (enable)
			control |= PCI_MSIX_FLAGS_ENABLE;
		else
			control = 0;
		pci_write_config_dword(pdev, pos, control);
	}
}

static void netxen_init_msix_entries(struct netxen_adapter *adapter, int count)
{
	int i;

	for (i = 0; i < count; i++)
		adapter->msix_entries[i].entry = i;
}

static int
netxen_read_mac_addr(struct netxen_adapter *adapter)
{
	int i;
	unsigned char *p;
	u64 mac_addr;
	struct net_device *netdev = adapter->netdev;
	struct pci_dev *pdev = adapter->pdev;

	if (NX_IS_REVISION_P3(adapter->ahw.revision_id)) {
		if (netxen_p3_get_mac_addr(adapter, &mac_addr) != 0)
			return -EIO;
	} else {
		if (netxen_get_flash_mac_addr(adapter, &mac_addr) != 0)
			return -EIO;
	}

	p = (unsigned char *)&mac_addr;
	for (i = 0; i < 6; i++)
		netdev->dev_addr[i] = *(p + 5 - i);

	memcpy(netdev->perm_addr, netdev->dev_addr, netdev->addr_len);
	memcpy(adapter->mac_addr, netdev->dev_addr, netdev->addr_len);

	/* set station address */

	if (!is_valid_ether_addr(netdev->perm_addr))
		dev_warn(&pdev->dev, "Bad MAC address %pM.\n", netdev->dev_addr);

	return 0;
}

static int netxen_nic_set_mac(struct net_device *netdev, void *p)
{
	struct netxen_adapter *adapter = netdev_priv(netdev);
	struct sockaddr *addr = p;

	if (!is_valid_ether_addr(addr->sa_data))
		return -EINVAL;

	if (netif_running(netdev)) {
		netif_device_detach(netdev);
		netxen_napi_disable(adapter);
	}

	memcpy(adapter->mac_addr, addr->sa_data, netdev->addr_len);
	memcpy(netdev->dev_addr, addr->sa_data, netdev->addr_len);
	adapter->macaddr_set(adapter, addr->sa_data);

	if (netif_running(netdev)) {
		netif_device_attach(netdev);
		netxen_napi_enable(adapter);
	}
	return 0;
}

static void netxen_set_multicast_list(struct net_device *dev)
{
	struct netxen_adapter *adapter = netdev_priv(dev);

	adapter->set_multi(dev);
}

static netdev_features_t netxen_fix_features(struct net_device *dev,
	netdev_features_t features)
{
	if (!(features & NETIF_F_RXCSUM)) {
		netdev_info(dev, "disabling LRO as RXCSUM is off\n");

		features &= ~NETIF_F_LRO;
	}

	return features;
}

static int netxen_set_features(struct net_device *dev,
	netdev_features_t features)
{
	struct netxen_adapter *adapter = netdev_priv(dev);
	int hw_lro;

	if (!((dev->features ^ features) & NETIF_F_LRO))
		return 0;

	hw_lro = (features & NETIF_F_LRO) ? NETXEN_NIC_LRO_ENABLED
	         : NETXEN_NIC_LRO_DISABLED;

	if (netxen_config_hw_lro(adapter, hw_lro))
		return -EIO;

	if (!(features & NETIF_F_LRO) && netxen_send_lro_cleanup(adapter))
		return -EIO;

	return 0;
}

static const struct net_device_ops netxen_netdev_ops = {
	.ndo_open	   = netxen_nic_open,
	.ndo_stop	   = netxen_nic_close,
	.ndo_start_xmit    = netxen_nic_xmit_frame,
	.ndo_get_stats64   = netxen_nic_get_stats,
	.ndo_validate_addr = eth_validate_addr,
	.ndo_set_rx_mode   = netxen_set_multicast_list,
	.ndo_set_mac_address    = netxen_nic_set_mac,
	.ndo_change_mtu	   = netxen_nic_change_mtu,
	.ndo_tx_timeout	   = netxen_tx_timeout,
	.ndo_fix_features = netxen_fix_features,
	.ndo_set_features = netxen_set_features,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller = netxen_nic_poll_controller,
#endif
};

static void
netxen_setup_intr(struct netxen_adapter *adapter)
{
	struct netxen_legacy_intr_set *legacy_intrp;
	struct pci_dev *pdev = adapter->pdev;
	int err, num_msix;

	if (adapter->rss_supported) {
		num_msix = (num_online_cpus() >= MSIX_ENTRIES_PER_ADAPTER) ?
			MSIX_ENTRIES_PER_ADAPTER : 2;
	} else
		num_msix = 1;

	adapter->max_sds_rings = 1;

	adapter->flags &= ~(NETXEN_NIC_MSI_ENABLED | NETXEN_NIC_MSIX_ENABLED);

	if (adapter->ahw.revision_id >= NX_P3_B0)
		legacy_intrp = &legacy_intr[adapter->ahw.pci_func];
	else
		legacy_intrp = &legacy_intr[0];

	adapter->int_vec_bit = legacy_intrp->int_vec_bit;
	adapter->tgt_status_reg = netxen_get_ioaddr(adapter,
			legacy_intrp->tgt_status_reg);
	adapter->tgt_mask_reg = netxen_get_ioaddr(adapter,
			legacy_intrp->tgt_mask_reg);
	adapter->pci_int_reg = netxen_get_ioaddr(adapter,
			legacy_intrp->pci_int_reg);
	adapter->isr_int_vec = netxen_get_ioaddr(adapter, ISR_INT_VECTOR);

	if (adapter->ahw.revision_id >= NX_P3_B1)
		adapter->crb_int_state_reg = netxen_get_ioaddr(adapter,
			ISR_INT_STATE_REG);
	else
		adapter->crb_int_state_reg = netxen_get_ioaddr(adapter,
			CRB_INT_VECTOR);

	netxen_set_msix_bit(pdev, 0);

	if (adapter->msix_supported) {

		netxen_init_msix_entries(adapter, num_msix);
		err = pci_enable_msix(pdev, adapter->msix_entries, num_msix);
		if (err == 0) {
			adapter->flags |= NETXEN_NIC_MSIX_ENABLED;
			netxen_set_msix_bit(pdev, 1);

			if (adapter->rss_supported)
				adapter->max_sds_rings = num_msix;

			dev_info(&pdev->dev, "using msi-x interrupts\n");
			return;
		}

		if (err > 0)
			pci_disable_msix(pdev);

		/* fall through for msi */
	}

	if (use_msi && !pci_enable_msi(pdev)) {
		adapter->flags |= NETXEN_NIC_MSI_ENABLED;
		adapter->tgt_status_reg = netxen_get_ioaddr(adapter,
				msi_tgt_status[adapter->ahw.pci_func]);
		dev_info(&pdev->dev, "using msi interrupts\n");
		adapter->msix_entries[0].vector = pdev->irq;
		return;
	}

	dev_info(&pdev->dev, "using legacy interrupts\n");
	adapter->msix_entries[0].vector = pdev->irq;
}

static void
netxen_teardown_intr(struct netxen_adapter *adapter)
{
	if (adapter->flags & NETXEN_NIC_MSIX_ENABLED)
		pci_disable_msix(adapter->pdev);
	if (adapter->flags & NETXEN_NIC_MSI_ENABLED)
		pci_disable_msi(adapter->pdev);
}

static void
netxen_cleanup_pci_map(struct netxen_adapter *adapter)
{
	if (adapter->ahw.db_base != NULL)
		iounmap(adapter->ahw.db_base);
	if (adapter->ahw.pci_base0 != NULL)
		iounmap(adapter->ahw.pci_base0);
	if (adapter->ahw.pci_base1 != NULL)
		iounmap(adapter->ahw.pci_base1);
	if (adapter->ahw.pci_base2 != NULL)
		iounmap(adapter->ahw.pci_base2);
}

static int
netxen_setup_pci_map(struct netxen_adapter *adapter)
{
	void __iomem *db_ptr = NULL;

	resource_size_t mem_base, db_base;
	unsigned long mem_len, db_len = 0;

	struct pci_dev *pdev = adapter->pdev;
	int pci_func = adapter->ahw.pci_func;
	struct netxen_hardware_context *ahw = &adapter->ahw;

	int err = 0;

	/*
	 * Set the CRB window to invalid. If any register in window 0 is
	 * accessed it should set the window to 0 and then reset it to 1.
	 */
	adapter->ahw.crb_win = -1;
	adapter->ahw.ocm_win = -1;

	/* remap phys address */
	mem_base = pci_resource_start(pdev, 0);	/* 0 is for BAR 0 */
	mem_len = pci_resource_len(pdev, 0);

	/* 128 Meg of memory */
	if (mem_len == NETXEN_PCI_128MB_SIZE) {

		ahw->pci_base0 = ioremap(mem_base, FIRST_PAGE_GROUP_SIZE);
		ahw->pci_base1 = ioremap(mem_base + SECOND_PAGE_GROUP_START,
				SECOND_PAGE_GROUP_SIZE);
		ahw->pci_base2 = ioremap(mem_base + THIRD_PAGE_GROUP_START,
				THIRD_PAGE_GROUP_SIZE);
		if (ahw->pci_base0 == NULL || ahw->pci_base1 == NULL ||
						ahw->pci_base2 == NULL) {
			dev_err(&pdev->dev, "failed to map PCI bar 0\n");
			err = -EIO;
			goto err_out;
		}

		ahw->pci_len0 = FIRST_PAGE_GROUP_SIZE;

	} else if (mem_len == NETXEN_PCI_32MB_SIZE) {

		ahw->pci_base1 = ioremap(mem_base, SECOND_PAGE_GROUP_SIZE);
		ahw->pci_base2 = ioremap(mem_base + THIRD_PAGE_GROUP_START -
			SECOND_PAGE_GROUP_START, THIRD_PAGE_GROUP_SIZE);
		if (ahw->pci_base1 == NULL || ahw->pci_base2 == NULL) {
			dev_err(&pdev->dev, "failed to map PCI bar 0\n");
			err = -EIO;
			goto err_out;
		}

	} else if (mem_len == NETXEN_PCI_2MB_SIZE) {

		ahw->pci_base0 = pci_ioremap_bar(pdev, 0);
		if (ahw->pci_base0 == NULL) {
			dev_err(&pdev->dev, "failed to map PCI bar 0\n");
			return -EIO;
		}
		ahw->pci_len0 = mem_len;
	} else {
		return -EIO;
	}

	netxen_setup_hwops(adapter);

	dev_info(&pdev->dev, "%dMB memory map\n", (int)(mem_len>>20));

	if (NX_IS_REVISION_P3P(adapter->ahw.revision_id)) {
		adapter->ahw.ocm_win_crb = netxen_get_ioaddr(adapter,
			NETXEN_PCIX_PS_REG(PCIX_OCM_WINDOW_REG(pci_func)));

	} else if (NX_IS_REVISION_P3(adapter->ahw.revision_id)) {
		adapter->ahw.ocm_win_crb = netxen_get_ioaddr(adapter,
			NETXEN_PCIX_PS_REG(PCIE_MN_WINDOW_REG(pci_func)));
	}

	if (NX_IS_REVISION_P3(adapter->ahw.revision_id))
		goto skip_doorbell;

	db_base = pci_resource_start(pdev, 4);	/* doorbell is on bar 4 */
	db_len = pci_resource_len(pdev, 4);

	if (db_len == 0) {
		printk(KERN_ERR "%s: doorbell is disabled\n",
				netxen_nic_driver_name);
		err = -EIO;
		goto err_out;
	}

	db_ptr = ioremap(db_base, NETXEN_DB_MAPSIZE_BYTES);
	if (!db_ptr) {
		printk(KERN_ERR "%s: Failed to allocate doorbell map.",
				netxen_nic_driver_name);
		err = -EIO;
		goto err_out;
	}

skip_doorbell:
	adapter->ahw.db_base = db_ptr;
	adapter->ahw.db_len = db_len;
	return 0;

err_out:
	netxen_cleanup_pci_map(adapter);
	return err;
}

static void
netxen_check_options(struct netxen_adapter *adapter)
{
	u32 fw_major, fw_minor, fw_build;
	char brd_name[NETXEN_MAX_SHORT_NAME];
	char serial_num[32];
	int i, offset, val;
	int *ptr32;
	struct pci_dev *pdev = adapter->pdev;

	adapter->driver_mismatch = 0;

	ptr32 = (int *)&serial_num;
	offset = NX_FW_SERIAL_NUM_OFFSET;
	for (i = 0; i < 8; i++) {
		if (netxen_rom_fast_read(adapter, offset, &val) == -1) {
			dev_err(&pdev->dev, "error reading board info\n");
			adapter->driver_mismatch = 1;
			return;
		}
		ptr32[i] = cpu_to_le32(val);
		offset += sizeof(u32);
	}

	fw_major = NXRD32(adapter, NETXEN_FW_VERSION_MAJOR);
	fw_minor = NXRD32(adapter, NETXEN_FW_VERSION_MINOR);
	fw_build = NXRD32(adapter, NETXEN_FW_VERSION_SUB);

	adapter->fw_version = NETXEN_VERSION_CODE(fw_major, fw_minor, fw_build);

	if (adapter->portnum == 0) {
		get_brd_name_by_type(adapter->ahw.board_type, brd_name);

		pr_info("%s: %s Board S/N %s  Chip rev 0x%x\n",
				module_name(THIS_MODULE),
				brd_name, serial_num, adapter->ahw.revision_id);
	}

	if (adapter->fw_version < NETXEN_VERSION_CODE(3, 4, 216)) {
		adapter->driver_mismatch = 1;
		dev_warn(&pdev->dev, "firmware version %d.%d.%d unsupported\n",
				fw_major, fw_minor, fw_build);
		return;
	}

	if (NX_IS_REVISION_P3(adapter->ahw.revision_id)) {
		i = NXRD32(adapter, NETXEN_SRE_MISC);
		adapter->ahw.cut_through = (i & 0x8000) ? 1 : 0;
	}

	dev_info(&pdev->dev, "firmware v%d.%d.%d [%s]\n",
			fw_major, fw_minor, fw_build,
			adapter->ahw.cut_through ? "cut-through" : "legacy");

	if (adapter->fw_version >= NETXEN_VERSION_CODE(4, 0, 222))
		adapter->capabilities = NXRD32(adapter, CRB_FW_CAPABILITIES_1);

	if (adapter->ahw.port_type == NETXEN_NIC_XGBE) {
		adapter->num_rxd = DEFAULT_RCV_DESCRIPTORS_10G;
		adapter->num_jumbo_rxd = MAX_JUMBO_RCV_DESCRIPTORS_10G;
	} else if (adapter->ahw.port_type == NETXEN_NIC_GBE) {
		adapter->num_rxd = DEFAULT_RCV_DESCRIPTORS_1G;
		adapter->num_jumbo_rxd = MAX_JUMBO_RCV_DESCRIPTORS_1G;
	}

	adapter->msix_supported = 0;
	if (NX_IS_REVISION_P3(adapter->ahw.revision_id)) {
		adapter->msix_supported = !!use_msi_x;
		adapter->rss_supported = !!use_msi_x;
	} else {
		u32 flashed_ver = 0;
		netxen_rom_fast_read(adapter,
				NX_FW_VERSION_OFFSET, (int *)&flashed_ver);
		flashed_ver = NETXEN_DECODE_VERSION(flashed_ver);

		if (flashed_ver >= NETXEN_VERSION_CODE(3, 4, 336)) {
			switch (adapter->ahw.board_type) {
			case NETXEN_BRDTYPE_P2_SB31_10G:
			case NETXEN_BRDTYPE_P2_SB31_10G_CX4:
				adapter->msix_supported = !!use_msi_x;
				adapter->rss_supported = !!use_msi_x;
				break;
			default:
				break;
			}
		}
	}

	adapter->num_txd = MAX_CMD_DESCRIPTORS;

	if (NX_IS_REVISION_P2(adapter->ahw.revision_id)) {
		adapter->num_lro_rxd = MAX_LRO_RCV_DESCRIPTORS;
		adapter->max_rds_rings = 3;
	} else {
		adapter->num_lro_rxd = 0;
		adapter->max_rds_rings = 2;
	}
}

static int
netxen_start_firmware(struct netxen_adapter *adapter)
{
	int val, err, first_boot;
	struct pci_dev *pdev = adapter->pdev;

	/* required for NX2031 dummy dma */
	err = nx_set_dma_mask(adapter);
	if (err)
		return err;

	if (!netxen_can_start_firmware(adapter))
		goto wait_init;

	first_boot = NXRD32(adapter, NETXEN_CAM_RAM(0x1fc));

	err = netxen_check_hw_init(adapter, first_boot);
	if (err) {
		dev_err(&pdev->dev, "error in init HW init sequence\n");
		return err;
	}

	netxen_request_firmware(adapter);

	err = netxen_need_fw_reset(adapter);
	if (err < 0)
		goto err_out;
	if (err == 0)
		goto pcie_strap_init;

	if (first_boot != 0x55555555) {
		NXWR32(adapter, CRB_CMDPEG_STATE, 0);
		netxen_pinit_from_rom(adapter);
		msleep(1);
	}

	NXWR32(adapter, CRB_DMA_SHIFT, 0x55555555);
	NXWR32(adapter, NETXEN_PEG_HALT_STATUS1, 0);
	NXWR32(adapter, NETXEN_PEG_HALT_STATUS2, 0);

	if (NX_IS_REVISION_P3(adapter->ahw.revision_id))
		netxen_set_port_mode(adapter);

	err = netxen_load_firmware(adapter);
	if (err)
		goto err_out;

	netxen_release_firmware(adapter);

	if (NX_IS_REVISION_P2(adapter->ahw.revision_id)) {

		/* Initialize multicast addr pool owners */
		val = 0x7654;
		if (adapter->ahw.port_type == NETXEN_NIC_XGBE)
			val |= 0x0f000000;
		NXWR32(adapter, NETXEN_MAC_ADDR_CNTL_REG, val);

	}

	err = netxen_init_dummy_dma(adapter);
	if (err)
		goto err_out;

	/*
	 * Tell the hardware our version number.
	 */
	val = (_NETXEN_NIC_LINUX_MAJOR << 16)
		| ((_NETXEN_NIC_LINUX_MINOR << 8))
		| (_NETXEN_NIC_LINUX_SUBVERSION);
	NXWR32(adapter, CRB_DRIVER_VERSION, val);

pcie_strap_init:
	if (NX_IS_REVISION_P3(adapter->ahw.revision_id))
		netxen_pcie_strap_init(adapter);

wait_init:
	/* Handshake with the card before we register the devices. */
	err = netxen_phantom_init(adapter, NETXEN_NIC_PEG_TUNE);
	if (err) {
		netxen_free_dummy_dma(adapter);
		goto err_out;
	}

	NXWR32(adapter, NX_CRB_DEV_STATE, NX_DEV_READY);

	nx_update_dma_mask(adapter);

	netxen_check_options(adapter);

	adapter->need_fw_reset = 0;

	/* fall through and release firmware */

err_out:
	netxen_release_firmware(adapter);
	return err;
}

static int
netxen_nic_request_irq(struct netxen_adapter *adapter)
{
	irq_handler_t handler;
	struct nx_host_sds_ring *sds_ring;
	int err, ring;

	unsigned long flags = 0;
	struct net_device *netdev = adapter->netdev;
	struct netxen_recv_context *recv_ctx = &adapter->recv_ctx;

	if (adapter->flags & NETXEN_NIC_MSIX_ENABLED)
		handler = netxen_msix_intr;
	else if (adapter->flags & NETXEN_NIC_MSI_ENABLED)
		handler = netxen_msi_intr;
	else {
		flags |= IRQF_SHARED;
		handler = netxen_intr;
	}
	adapter->irq = netdev->irq;

	for (ring = 0; ring < adapter->max_sds_rings; ring++) {
		sds_ring = &recv_ctx->sds_rings[ring];
		sprintf(sds_ring->name, "%s[%d]", netdev->name, ring);
		err = request_irq(sds_ring->irq, handler,
				  flags, sds_ring->name, sds_ring);
		if (err)
			return err;
	}

	return 0;
}

static void
netxen_nic_free_irq(struct netxen_adapter *adapter)
{
	int ring;
	struct nx_host_sds_ring *sds_ring;

	struct netxen_recv_context *recv_ctx = &adapter->recv_ctx;

	for (ring = 0; ring < adapter->max_sds_rings; ring++) {
		sds_ring = &recv_ctx->sds_rings[ring];
		free_irq(sds_ring->irq, sds_ring);
	}
}

static void
netxen_nic_init_coalesce_defaults(struct netxen_adapter *adapter)
{
	adapter->coal.flags = NETXEN_NIC_INTR_DEFAULT;
	adapter->coal.normal.data.rx_time_us =
		NETXEN_DEFAULT_INTR_COALESCE_RX_TIME_US;
	adapter->coal.normal.data.rx_packets =
		NETXEN_DEFAULT_INTR_COALESCE_RX_PACKETS;
	adapter->coal.normal.data.tx_time_us =
		NETXEN_DEFAULT_INTR_COALESCE_TX_TIME_US;
	adapter->coal.normal.data.tx_packets =
		NETXEN_DEFAULT_INTR_COALESCE_TX_PACKETS;
}

/* with rtnl_lock */
static int
__netxen_nic_up(struct netxen_adapter *adapter, struct net_device *netdev)
{
	int err;

	if (adapter->is_up != NETXEN_ADAPTER_UP_MAGIC)
		return -EIO;

	err = adapter->init_port(adapter, adapter->physical_port);
	if (err) {
		printk(KERN_ERR "%s: Failed to initialize port %d\n",
				netxen_nic_driver_name, adapter->portnum);
		return err;
	}
	if (NX_IS_REVISION_P2(adapter->ahw.revision_id))
		adapter->macaddr_set(adapter, adapter->mac_addr);

	adapter->set_multi(netdev);
	adapter->set_mtu(adapter, netdev->mtu);

	adapter->ahw.linkup = 0;

	if (adapter->max_sds_rings > 1)
		netxen_config_rss(adapter, 1);

	if (NX_IS_REVISION_P3(adapter->ahw.revision_id))
		netxen_config_intr_coalesce(adapter);

	if (netdev->features & NETIF_F_LRO)
		netxen_config_hw_lro(adapter, NETXEN_NIC_LRO_ENABLED);

	netxen_napi_enable(adapter);

	if (adapter->capabilities & NX_FW_CAPABILITY_LINK_NOTIFICATION)
		netxen_linkevent_request(adapter, 1);
	else
		netxen_nic_set_link_parameters(adapter);

	set_bit(__NX_DEV_UP, &adapter->state);
	return 0;
}

/* Usage: During resume and firmware recovery module.*/

static inline int
netxen_nic_up(struct netxen_adapter *adapter, struct net_device *netdev)
{
	int err = 0;

	rtnl_lock();
	if (netif_running(netdev))
		err = __netxen_nic_up(adapter, netdev);
	rtnl_unlock();

	return err;
}

/* with rtnl_lock */
static void
__netxen_nic_down(struct netxen_adapter *adapter, struct net_device *netdev)
{
	if (adapter->is_up != NETXEN_ADAPTER_UP_MAGIC)
		return;

	if (!test_and_clear_bit(__NX_DEV_UP, &adapter->state))
		return;

	smp_mb();
	spin_lock(&adapter->tx_clean_lock);
	netif_carrier_off(netdev);
	netif_tx_disable(netdev);

	if (adapter->capabilities & NX_FW_CAPABILITY_LINK_NOTIFICATION)
		netxen_linkevent_request(adapter, 0);

	if (adapter->stop_port)
		adapter->stop_port(adapter);

	if (NX_IS_REVISION_P3(adapter->ahw.revision_id))
		netxen_p3_free_mac_list(adapter);

	adapter->set_promisc(adapter, NETXEN_NIU_NON_PROMISC_MODE);

	netxen_napi_disable(adapter);

	netxen_release_tx_buffers(adapter);
	spin_unlock(&adapter->tx_clean_lock);
}

/* Usage: During suspend and firmware recovery module */

static inline void
netxen_nic_down(struct netxen_adapter *adapter, struct net_device *netdev)
{
	rtnl_lock();
	if (netif_running(netdev))
		__netxen_nic_down(adapter, netdev);
	rtnl_unlock();

}

static int
netxen_nic_attach(struct netxen_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;
	struct pci_dev *pdev = adapter->pdev;
	int err, ring;
	struct nx_host_rds_ring *rds_ring;
	struct nx_host_tx_ring *tx_ring;

	if (adapter->is_up == NETXEN_ADAPTER_UP_MAGIC)
		return 0;

	err = netxen_init_firmware(adapter);
	if (err)
		return err;

	err = netxen_napi_add(adapter, netdev);
	if (err)
		return err;

	err = netxen_alloc_sw_resources(adapter);
	if (err) {
		printk(KERN_ERR "%s: Error in setting sw resources\n",
				netdev->name);
		return err;
	}

	err = netxen_alloc_hw_resources(adapter);
	if (err) {
		printk(KERN_ERR "%s: Error in setting hw resources\n",
				netdev->name);
		goto err_out_free_sw;
	}

	if (NX_IS_REVISION_P2(adapter->ahw.revision_id)) {
		tx_ring = adapter->tx_ring;
		tx_ring->crb_cmd_producer = netxen_get_ioaddr(adapter,
				crb_cmd_producer[adapter->portnum]);
		tx_ring->crb_cmd_consumer = netxen_get_ioaddr(adapter,
				crb_cmd_consumer[adapter->portnum]);

		tx_ring->producer = 0;
		tx_ring->sw_consumer = 0;

		netxen_nic_update_cmd_producer(adapter, tx_ring);
		netxen_nic_update_cmd_consumer(adapter, tx_ring);
	}

	for (ring = 0; ring < adapter->max_rds_rings; ring++) {
		rds_ring = &adapter->recv_ctx.rds_rings[ring];
		netxen_post_rx_buffers(adapter, ring, rds_ring);
	}

	err = netxen_nic_request_irq(adapter);
	if (err) {
		dev_err(&pdev->dev, "%s: failed to setup interrupt\n",
				netdev->name);
		goto err_out_free_rxbuf;
	}

	if (NX_IS_REVISION_P3(adapter->ahw.revision_id))
		netxen_nic_init_coalesce_defaults(adapter);

	netxen_create_sysfs_entries(adapter);

	adapter->is_up = NETXEN_ADAPTER_UP_MAGIC;
	return 0;

err_out_free_rxbuf:
	netxen_release_rx_buffers(adapter);
	netxen_free_hw_resources(adapter);
err_out_free_sw:
	netxen_free_sw_resources(adapter);
	return err;
}

static void
netxen_nic_detach(struct netxen_adapter *adapter)
{
	if (adapter->is_up != NETXEN_ADAPTER_UP_MAGIC)
		return;

	netxen_remove_sysfs_entries(adapter);

	netxen_free_hw_resources(adapter);
	netxen_release_rx_buffers(adapter);
	netxen_nic_free_irq(adapter);
	netxen_napi_del(adapter);
	netxen_free_sw_resources(adapter);

	adapter->is_up = 0;
}

int
netxen_nic_reset_context(struct netxen_adapter *adapter)
{
	int err = 0;
	struct net_device *netdev = adapter->netdev;

	if (test_and_set_bit(__NX_RESETTING, &adapter->state))
		return -EBUSY;

	if (adapter->is_up == NETXEN_ADAPTER_UP_MAGIC) {

		netif_device_detach(netdev);

		if (netif_running(netdev))
			__netxen_nic_down(adapter, netdev);

		netxen_nic_detach(adapter);

		if (netif_running(netdev)) {
			err = netxen_nic_attach(adapter);
			if (!err)
				err = __netxen_nic_up(adapter, netdev);

			if (err)
				goto done;
		}

		netif_device_attach(netdev);
	}

done:
	clear_bit(__NX_RESETTING, &adapter->state);
	return err;
}

static int
netxen_setup_netdev(struct netxen_adapter *adapter,
		struct net_device *netdev)
{
	int err = 0;
	struct pci_dev *pdev = adapter->pdev;

	adapter->mc_enabled = 0;
	if (NX_IS_REVISION_P3(adapter->ahw.revision_id))
		adapter->max_mc_count = 38;
	else
		adapter->max_mc_count = 16;

	netdev->netdev_ops	   = &netxen_netdev_ops;
	netdev->watchdog_timeo     = 5*HZ;

	netxen_nic_change_mtu(netdev, netdev->mtu);

	SET_ETHTOOL_OPS(netdev, &netxen_nic_ethtool_ops);

	netdev->hw_features = NETIF_F_SG | NETIF_F_IP_CSUM | NETIF_F_TSO |
	                      NETIF_F_RXCSUM;

	if (NX_IS_REVISION_P3(adapter->ahw.revision_id))
		netdev->hw_features |= NETIF_F_IPV6_CSUM | NETIF_F_TSO6;

	netdev->vlan_features |= netdev->hw_features;

	if (adapter->pci_using_dac) {
		netdev->features |= NETIF_F_HIGHDMA;
		netdev->vlan_features |= NETIF_F_HIGHDMA;
	}

	if (adapter->capabilities & NX_FW_CAPABILITY_FVLANTX)
		netdev->hw_features |= NETIF_F_HW_VLAN_TX;

	if (adapter->capabilities & NX_FW_CAPABILITY_HW_LRO)
		netdev->hw_features |= NETIF_F_LRO;

	netdev->features |= netdev->hw_features;

	netdev->irq = adapter->msix_entries[0].vector;

	INIT_WORK(&adapter->tx_timeout_task, netxen_tx_timeout_task);

	if (netxen_read_mac_addr(adapter))
		dev_warn(&pdev->dev, "failed to read mac addr\n");

	netif_carrier_off(netdev);

	err = register_netdev(netdev);
	if (err) {
		dev_err(&pdev->dev, "failed to register net device\n");
		return err;
	}

	return 0;
}

#ifdef CONFIG_PCIEAER
static void netxen_mask_aer_correctable(struct netxen_adapter *adapter)
{
	struct pci_dev *pdev = adapter->pdev;
	struct pci_dev *root = pdev->bus->self;
	u32 aer_pos;

	if (adapter->ahw.board_type != NETXEN_BRDTYPE_P3_4_GB_MM &&
		adapter->ahw.board_type != NETXEN_BRDTYPE_P3_10G_TP)
		return;

	if (root->pcie_type != PCI_EXP_TYPE_ROOT_PORT)
		return;

	aer_pos = pci_find_ext_capability(root, PCI_EXT_CAP_ID_ERR);
	if (!aer_pos)
		return;

	pci_write_config_dword(root, aer_pos + PCI_ERR_COR_MASK, 0xffff);
}
#endif

static int __devinit
netxen_nic_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct net_device *netdev = NULL;
	struct netxen_adapter *adapter = NULL;
	int i = 0, err;
	int pci_func_id = PCI_FUNC(pdev->devfn);
	uint8_t revision_id;
	u32 val;

	if (pdev->revision >= NX_P3_A0 && pdev->revision <= NX_P3_B1) {
		pr_warning("%s: chip revisions between 0x%x-0x%x "
				"will not be enabled.\n",
				module_name(THIS_MODULE), NX_P3_A0, NX_P3_B1);
		return -ENODEV;
	}

	if ((err = pci_enable_device(pdev)))
		return err;

	if (!(pci_resource_flags(pdev, 0) & IORESOURCE_MEM)) {
		err = -ENODEV;
		goto err_out_disable_pdev;
	}

	if ((err = pci_request_regions(pdev, netxen_nic_driver_name)))
		goto err_out_disable_pdev;

	if (NX_IS_REVISION_P3(pdev->revision))
		pci_enable_pcie_error_reporting(pdev);

	pci_set_master(pdev);

	netdev = alloc_etherdev(sizeof(struct netxen_adapter));
	if(!netdev) {
		dev_err(&pdev->dev, "failed to allocate net_device\n");
		err = -ENOMEM;
		goto err_out_free_res;
	}

	SET_NETDEV_DEV(netdev, &pdev->dev);

	adapter = netdev_priv(netdev);
	adapter->netdev  = netdev;
	adapter->pdev    = pdev;
	adapter->ahw.pci_func  = pci_func_id;

	revision_id = pdev->revision;
	adapter->ahw.revision_id = revision_id;

	rwlock_init(&adapter->ahw.crb_lock);
	spin_lock_init(&adapter->ahw.mem_lock);

	spin_lock_init(&adapter->tx_clean_lock);
	INIT_LIST_HEAD(&adapter->mac_list);
	INIT_LIST_HEAD(&adapter->vlan_ip_list);

	err = netxen_setup_pci_map(adapter);
	if (err)
		goto err_out_free_netdev;

	/* This will be reset for mezz cards  */
	adapter->portnum = pci_func_id;

	err = netxen_nic_get_board_info(adapter);
	if (err) {
		dev_err(&pdev->dev, "Error getting board config info.\n");
		goto err_out_iounmap;
	}

#ifdef CONFIG_PCIEAER
	netxen_mask_aer_correctable(adapter);
#endif

	/* Mezz cards have PCI function 0,2,3 enabled */
	switch (adapter->ahw.board_type) {
	case NETXEN_BRDTYPE_P2_SB31_10G_IMEZ:
	case NETXEN_BRDTYPE_P2_SB31_10G_HMEZ:
		if (pci_func_id >= 2)
			adapter->portnum = pci_func_id - 2;
		break;
	default:
		break;
	}

	err = netxen_check_flash_fw_compatibility(adapter);
	if (err)
		goto err_out_iounmap;

	if (adapter->portnum == 0) {
		val = NXRD32(adapter, NX_CRB_DEV_REF_COUNT);
		if (val != 0xffffffff && val != 0) {
			NXWR32(adapter, NX_CRB_DEV_REF_COUNT, 0);
			adapter->need_fw_reset = 1;
		}
	}

	err = netxen_start_firmware(adapter);
	if (err)
		goto err_out_decr_ref;

	/*
	 * See if the firmware gave us a virtual-physical port mapping.
	 */
	adapter->physical_port = adapter->portnum;
	if (NX_IS_REVISION_P2(adapter->ahw.revision_id)) {
		i = NXRD32(adapter, CRB_V2P(adapter->portnum));
		if (i != 0x55555555)
			adapter->physical_port = i;
	}

	netxen_nic_clear_stats(adapter);

	netxen_setup_intr(adapter);

	err = netxen_setup_netdev(adapter, netdev);
	if (err)
		goto err_out_disable_msi;

	pci_set_drvdata(pdev, adapter);

	netxen_schedule_work(adapter, netxen_fw_poll_work, FW_POLL_DELAY);

	switch (adapter->ahw.port_type) {
	case NETXEN_NIC_GBE:
		dev_info(&adapter->pdev->dev, "%s: GbE port initialized\n",
				adapter->netdev->name);
		break;
	case NETXEN_NIC_XGBE:
		dev_info(&adapter->pdev->dev, "%s: XGbE port initialized\n",
				adapter->netdev->name);
		break;
	}

	netxen_create_diag_entries(adapter);

	return 0;

err_out_disable_msi:
	netxen_teardown_intr(adapter);

	netxen_free_dummy_dma(adapter);

err_out_decr_ref:
	nx_decr_dev_ref_cnt(adapter);

err_out_iounmap:
	netxen_cleanup_pci_map(adapter);

err_out_free_netdev:
	free_netdev(netdev);

err_out_free_res:
	pci_release_regions(pdev);

err_out_disable_pdev:
	pci_set_drvdata(pdev, NULL);
	pci_disable_device(pdev);
	return err;
}

static void __devexit netxen_nic_remove(struct pci_dev *pdev)
{
	struct netxen_adapter *adapter;
	struct net_device *netdev;

	adapter = pci_get_drvdata(pdev);
	if (adapter == NULL)
		return;

	netdev = adapter->netdev;

	netxen_cancel_fw_work(adapter);

	unregister_netdev(netdev);

	cancel_work_sync(&adapter->tx_timeout_task);

	netxen_free_vlan_ip_list(adapter);
	netxen_nic_detach(adapter);

	nx_decr_dev_ref_cnt(adapter);

	if (adapter->portnum == 0)
		netxen_free_dummy_dma(adapter);

	clear_bit(__NX_RESETTING, &adapter->state);

	netxen_teardown_intr(adapter);

	netxen_remove_diag_entries(adapter);

	netxen_cleanup_pci_map(adapter);

	netxen_release_firmware(adapter);

	if (NX_IS_REVISION_P3(pdev->revision))
		pci_disable_pcie_error_reporting(pdev);

	pci_release_regions(pdev);
	pci_disable_device(pdev);
	pci_set_drvdata(pdev, NULL);

	free_netdev(netdev);
}

static void netxen_nic_detach_func(struct netxen_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;

	netif_device_detach(netdev);

	netxen_cancel_fw_work(adapter);

	if (netif_running(netdev))
		netxen_nic_down(adapter, netdev);

	cancel_work_sync(&adapter->tx_timeout_task);

	netxen_nic_detach(adapter);

	if (adapter->portnum == 0)
		netxen_free_dummy_dma(adapter);

	nx_decr_dev_ref_cnt(adapter);

	clear_bit(__NX_RESETTING, &adapter->state);
}

static int netxen_nic_attach_func(struct pci_dev *pdev)
{
	struct netxen_adapter *adapter = pci_get_drvdata(pdev);
	struct net_device *netdev = adapter->netdev;
	int err;

	err = pci_enable_device(pdev);
	if (err)
		return err;

	pci_set_power_state(pdev, PCI_D0);
	pci_set_master(pdev);
	pci_restore_state(pdev);

	adapter->ahw.crb_win = -1;
	adapter->ahw.ocm_win = -1;

	err = netxen_start_firmware(adapter);
	if (err) {
		dev_err(&pdev->dev, "failed to start firmware\n");
		return err;
	}

	if (netif_running(netdev)) {
		err = netxen_nic_attach(adapter);
		if (err)
			goto err_out;

		err = netxen_nic_up(adapter, netdev);
		if (err)
			goto err_out_detach;

		netxen_restore_indev_addr(netdev, NETDEV_UP);
	}

	netif_device_attach(netdev);
	netxen_schedule_work(adapter, netxen_fw_poll_work, FW_POLL_DELAY);
	return 0;

err_out_detach:
	netxen_nic_detach(adapter);
err_out:
	nx_decr_dev_ref_cnt(adapter);
	return err;
}

static pci_ers_result_t netxen_io_error_detected(struct pci_dev *pdev,
						pci_channel_state_t state)
{
	struct netxen_adapter *adapter = pci_get_drvdata(pdev);

	if (state == pci_channel_io_perm_failure)
		return PCI_ERS_RESULT_DISCONNECT;

	if (nx_dev_request_aer(adapter))
		return PCI_ERS_RESULT_RECOVERED;

	netxen_nic_detach_func(adapter);

	pci_disable_device(pdev);

	return PCI_ERS_RESULT_NEED_RESET;
}

static pci_ers_result_t netxen_io_slot_reset(struct pci_dev *pdev)
{
	int err = 0;

	err = netxen_nic_attach_func(pdev);

	return err ? PCI_ERS_RESULT_DISCONNECT : PCI_ERS_RESULT_RECOVERED;
}

static void netxen_io_resume(struct pci_dev *pdev)
{
	pci_cleanup_aer_uncorrect_error_status(pdev);
}

static void netxen_nic_shutdown(struct pci_dev *pdev)
{
	struct netxen_adapter *adapter = pci_get_drvdata(pdev);

	netxen_nic_detach_func(adapter);

	if (pci_save_state(pdev))
		return;

	if (netxen_nic_wol_supported(adapter)) {
		pci_enable_wake(pdev, PCI_D3cold, 1);
		pci_enable_wake(pdev, PCI_D3hot, 1);
	}

	pci_disable_device(pdev);
}

#ifdef CONFIG_PM
static int
netxen_nic_suspend(struct pci_dev *pdev, pm_message_t state)
{
	struct netxen_adapter *adapter = pci_get_drvdata(pdev);
	int retval;

	netxen_nic_detach_func(adapter);

	retval = pci_save_state(pdev);
	if (retval)
		return retval;

	if (netxen_nic_wol_supported(adapter)) {
		pci_enable_wake(pdev, PCI_D3cold, 1);
		pci_enable_wake(pdev, PCI_D3hot, 1);
	}

	pci_disable_device(pdev);
	pci_set_power_state(pdev, pci_choose_state(pdev, state));

	return 0;
}

static int
netxen_nic_resume(struct pci_dev *pdev)
{
	return netxen_nic_attach_func(pdev);
}
#endif

static int netxen_nic_open(struct net_device *netdev)
{
	struct netxen_adapter *adapter = netdev_priv(netdev);
	int err = 0;

	if (adapter->driver_mismatch)
		return -EIO;

	err = netxen_nic_attach(adapter);
	if (err)
		return err;

	err = __netxen_nic_up(adapter, netdev);
	if (err)
		goto err_out;

	netif_start_queue(netdev);

	return 0;

err_out:
	netxen_nic_detach(adapter);
	return err;
}

/*
 * netxen_nic_close - Disables a network interface entry point
 */
static int netxen_nic_close(struct net_device *netdev)
{
	struct netxen_adapter *adapter = netdev_priv(netdev);

	__netxen_nic_down(adapter, netdev);
	return 0;
}

static void
netxen_tso_check(struct net_device *netdev,
		struct nx_host_tx_ring *tx_ring,
		struct cmd_desc_type0 *first_desc,
		struct sk_buff *skb)
{
	u8 opcode = TX_ETHER_PKT;
	__be16 protocol = skb->protocol;
	u16 flags = 0, vid = 0;
	u32 producer;
	int copied, offset, copy_len, hdr_len = 0, tso = 0, vlan_oob = 0;
	struct cmd_desc_type0 *hwdesc;
	struct vlan_ethhdr *vh;

	if (protocol == cpu_to_be16(ETH_P_8021Q)) {

		vh = (struct vlan_ethhdr *)skb->data;
		protocol = vh->h_vlan_encapsulated_proto;
		flags = FLAGS_VLAN_TAGGED;

	} else if (vlan_tx_tag_present(skb)) {

		flags = FLAGS_VLAN_OOB;
		vid = vlan_tx_tag_get(skb);
		netxen_set_tx_vlan_tci(first_desc, vid);
		vlan_oob = 1;
	}

	if ((netdev->features & (NETIF_F_TSO | NETIF_F_TSO6)) &&
			skb_shinfo(skb)->gso_size > 0) {

		hdr_len = skb_transport_offset(skb) + tcp_hdrlen(skb);

		first_desc->mss = cpu_to_le16(skb_shinfo(skb)->gso_size);
		first_desc->total_hdr_length = hdr_len;
		if (vlan_oob) {
			first_desc->total_hdr_length += VLAN_HLEN;
			first_desc->tcp_hdr_offset = VLAN_HLEN;
			first_desc->ip_hdr_offset = VLAN_HLEN;
			/* Only in case of TSO on vlan device */
			flags |= FLAGS_VLAN_TAGGED;
		}

		opcode = (protocol == cpu_to_be16(ETH_P_IPV6)) ?
				TX_TCP_LSO6 : TX_TCP_LSO;
		tso = 1;

	} else if (skb->ip_summed == CHECKSUM_PARTIAL) {
		u8 l4proto;

		if (protocol == cpu_to_be16(ETH_P_IP)) {
			l4proto = ip_hdr(skb)->protocol;

			if (l4proto == IPPROTO_TCP)
				opcode = TX_TCP_PKT;
			else if(l4proto == IPPROTO_UDP)
				opcode = TX_UDP_PKT;
		} else if (protocol == cpu_to_be16(ETH_P_IPV6)) {
			l4proto = ipv6_hdr(skb)->nexthdr;

			if (l4proto == IPPROTO_TCP)
				opcode = TX_TCPV6_PKT;
			else if(l4proto == IPPROTO_UDP)
				opcode = TX_UDPV6_PKT;
		}
	}

	first_desc->tcp_hdr_offset += skb_transport_offset(skb);
	first_desc->ip_hdr_offset += skb_network_offset(skb);
	netxen_set_tx_flags_opcode(first_desc, flags, opcode);

	if (!tso)
		return;

	/* For LSO, we need to copy the MAC/IP/TCP headers into
	 * the descriptor ring
	 */
	producer = tx_ring->producer;
	copied = 0;
	offset = 2;

	if (vlan_oob) {
		/* Create a TSO vlan header template for firmware */

		hwdesc = &tx_ring->desc_head[producer];
		tx_ring->cmd_buf_arr[producer].skb = NULL;

		copy_len = min((int)sizeof(struct cmd_desc_type0) - offset,
				hdr_len + VLAN_HLEN);

		vh = (struct vlan_ethhdr *)((char *)hwdesc + 2);
		skb_copy_from_linear_data(skb, vh, 12);
		vh->h_vlan_proto = htons(ETH_P_8021Q);
		vh->h_vlan_TCI = htons(vid);
		skb_copy_from_linear_data_offset(skb, 12,
				(char *)vh + 16, copy_len - 16);

		copied = copy_len - VLAN_HLEN;
		offset = 0;

		producer = get_next_index(producer, tx_ring->num_desc);
	}

	while (copied < hdr_len) {

		copy_len = min((int)sizeof(struct cmd_desc_type0) - offset,
				(hdr_len - copied));

		hwdesc = &tx_ring->desc_head[producer];
		tx_ring->cmd_buf_arr[producer].skb = NULL;

		skb_copy_from_linear_data_offset(skb, copied,
				 (char *)hwdesc + offset, copy_len);

		copied += copy_len;
		offset = 0;

		producer = get_next_index(producer, tx_ring->num_desc);
	}

	tx_ring->producer = producer;
	barrier();
}

static int
netxen_map_tx_skb(struct pci_dev *pdev,
		struct sk_buff *skb, struct netxen_cmd_buffer *pbuf)
{
	struct netxen_skb_frag *nf;
	struct skb_frag_struct *frag;
	int i, nr_frags;
	dma_addr_t map;

	nr_frags = skb_shinfo(skb)->nr_frags;
	nf = &pbuf->frag_array[0];

	map = pci_map_single(pdev, skb->data,
			skb_headlen(skb), PCI_DMA_TODEVICE);
	if (pci_dma_mapping_error(pdev, map))
		goto out_err;

	nf->dma = map;
	nf->length = skb_headlen(skb);

	for (i = 0; i < nr_frags; i++) {
		frag = &skb_shinfo(skb)->frags[i];
		nf = &pbuf->frag_array[i+1];

		map = skb_frag_dma_map(&pdev->dev, frag, 0, skb_frag_size(frag),
				       DMA_TO_DEVICE);
		if (dma_mapping_error(&pdev->dev, map))
			goto unwind;

		nf->dma = map;
		nf->length = skb_frag_size(frag);
	}

	return 0;

unwind:
	while (--i >= 0) {
		nf = &pbuf->frag_array[i+1];
		pci_unmap_page(pdev, nf->dma, nf->length, PCI_DMA_TODEVICE);
	}

	nf = &pbuf->frag_array[0];
	pci_unmap_single(pdev, nf->dma, skb_headlen(skb), PCI_DMA_TODEVICE);

out_err:
	return -ENOMEM;
}

static inline void
netxen_clear_cmddesc(u64 *desc)
{
	desc[0] = 0ULL;
	desc[2] = 0ULL;
}

static netdev_tx_t
netxen_nic_xmit_frame(struct sk_buff *skb, struct net_device *netdev)
{
	struct netxen_adapter *adapter = netdev_priv(netdev);
	struct nx_host_tx_ring *tx_ring = adapter->tx_ring;
	struct netxen_cmd_buffer *pbuf;
	struct netxen_skb_frag *buffrag;
	struct cmd_desc_type0 *hwdesc, *first_desc;
	struct pci_dev *pdev;
	int i, k;
	int delta = 0;
	struct skb_frag_struct *frag;

	u32 producer;
	int frag_count, no_of_desc;
	u32 num_txd = tx_ring->num_desc;

	frag_count = skb_shinfo(skb)->nr_frags + 1;

	/* 14 frags supported for normal packet and
	 * 32 frags supported for TSO packet
	 */
	if (!skb_is_gso(skb) && frag_count > NETXEN_MAX_FRAGS_PER_TX) {

		for (i = 0; i < (frag_count - NETXEN_MAX_FRAGS_PER_TX); i++) {
			frag = &skb_shinfo(skb)->frags[i];
			delta += skb_frag_size(frag);
		}

		if (!__pskb_pull_tail(skb, delta))
			goto drop_packet;

		frag_count = 1 + skb_shinfo(skb)->nr_frags;
	}
	/* 4 fragments per cmd des */
	no_of_desc = (frag_count + 3) >> 2;

	if (unlikely(netxen_tx_avail(tx_ring) <= TX_STOP_THRESH)) {
		netif_stop_queue(netdev);
		smp_mb();
		if (netxen_tx_avail(tx_ring) > TX_STOP_THRESH)
			netif_start_queue(netdev);
		else
			return NETDEV_TX_BUSY;
	}

	producer = tx_ring->producer;
	pbuf = &tx_ring->cmd_buf_arr[producer];

	pdev = adapter->pdev;

	if (netxen_map_tx_skb(pdev, skb, pbuf))
		goto drop_packet;

	pbuf->skb = skb;
	pbuf->frag_count = frag_count;

	first_desc = hwdesc = &tx_ring->desc_head[producer];
	netxen_clear_cmddesc((u64 *)hwdesc);

	netxen_set_tx_frags_len(first_desc, frag_count, skb->len);
	netxen_set_tx_port(first_desc, adapter->portnum);

	for (i = 0; i < frag_count; i++) {

		k = i % 4;

		if ((k == 0) && (i > 0)) {
			/* move to next desc.*/
			producer = get_next_index(producer, num_txd);
			hwdesc = &tx_ring->desc_head[producer];
			netxen_clear_cmddesc((u64 *)hwdesc);
			tx_ring->cmd_buf_arr[producer].skb = NULL;
		}

		buffrag = &pbuf->frag_array[i];

		hwdesc->buffer_length[k] = cpu_to_le16(buffrag->length);
		switch (k) {
		case 0:
			hwdesc->addr_buffer1 = cpu_to_le64(buffrag->dma);
			break;
		case 1:
			hwdesc->addr_buffer2 = cpu_to_le64(buffrag->dma);
			break;
		case 2:
			hwdesc->addr_buffer3 = cpu_to_le64(buffrag->dma);
			break;
		case 3:
			hwdesc->addr_buffer4 = cpu_to_le64(buffrag->dma);
			break;
		}
	}

	tx_ring->producer = get_next_index(producer, num_txd);

	netxen_tso_check(netdev, tx_ring, first_desc, skb);

	adapter->stats.txbytes += skb->len;
	adapter->stats.xmitcalled++;

	netxen_nic_update_cmd_producer(adapter, tx_ring);

	return NETDEV_TX_OK;

drop_packet:
	adapter->stats.txdropped++;
	dev_kfree_skb_any(skb);
	return NETDEV_TX_OK;
}

static int netxen_nic_check_temp(struct netxen_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;
	uint32_t temp, temp_state, temp_val;
	int rv = 0;

	temp = NXRD32(adapter, CRB_TEMP_STATE);

	temp_state = nx_get_temp_state(temp);
	temp_val = nx_get_temp_val(temp);

	if (temp_state == NX_TEMP_PANIC) {
		printk(KERN_ALERT
		       "%s: Device temperature %d degrees C exceeds"
		       " maximum allowed. Hardware has been shut down.\n",
		       netdev->name, temp_val);
		rv = 1;
	} else if (temp_state == NX_TEMP_WARN) {
		if (adapter->temp == NX_TEMP_NORMAL) {
			printk(KERN_ALERT
			       "%s: Device temperature %d degrees C "
			       "exceeds operating range."
			       " Immediate action needed.\n",
			       netdev->name, temp_val);
		}
	} else {
		if (adapter->temp == NX_TEMP_WARN) {
			printk(KERN_INFO
			       "%s: Device temperature is now %d degrees C"
			       " in normal range.\n", netdev->name,
			       temp_val);
		}
	}
	adapter->temp = temp_state;
	return rv;
}

void netxen_advert_link_change(struct netxen_adapter *adapter, int linkup)
{
	struct net_device *netdev = adapter->netdev;

	if (adapter->ahw.linkup && !linkup) {
		printk(KERN_INFO "%s: %s NIC Link is down\n",
		       netxen_nic_driver_name, netdev->name);
		adapter->ahw.linkup = 0;
		if (netif_running(netdev)) {
			netif_carrier_off(netdev);
			netif_stop_queue(netdev);
		}
		adapter->link_changed = !adapter->has_link_events;
	} else if (!adapter->ahw.linkup && linkup) {
		printk(KERN_INFO "%s: %s NIC Link is up\n",
		       netxen_nic_driver_name, netdev->name);
		adapter->ahw.linkup = 1;
		if (netif_running(netdev)) {
			netif_carrier_on(netdev);
			netif_wake_queue(netdev);
		}
		adapter->link_changed = !adapter->has_link_events;
	}
}

static void netxen_nic_handle_phy_intr(struct netxen_adapter *adapter)
{
	u32 val, port, linkup;

	port = adapter->physical_port;

	if (NX_IS_REVISION_P3(adapter->ahw.revision_id)) {
		val = NXRD32(adapter, CRB_XG_STATE_P3);
		val = XG_LINK_STATE_P3(adapter->ahw.pci_func, val);
		linkup = (val == XG_LINK_UP_P3);
	} else {
		val = NXRD32(adapter, CRB_XG_STATE);
		val = (val >> port*8) & 0xff;
		linkup = (val == XG_LINK_UP);
	}

	netxen_advert_link_change(adapter, linkup);
}

static void netxen_tx_timeout(struct net_device *netdev)
{
	struct netxen_adapter *adapter = netdev_priv(netdev);

	if (test_bit(__NX_RESETTING, &adapter->state))
		return;

	dev_err(&netdev->dev, "transmit timeout, resetting.\n");
	schedule_work(&adapter->tx_timeout_task);
}

static void netxen_tx_timeout_task(struct work_struct *work)
{
	struct netxen_adapter *adapter =
		container_of(work, struct netxen_adapter, tx_timeout_task);

	if (!netif_running(adapter->netdev))
		return;

	if (test_and_set_bit(__NX_RESETTING, &adapter->state))
		return;

	if (++adapter->tx_timeo_cnt >= NX_MAX_TX_TIMEOUTS)
		goto request_reset;

	rtnl_lock();
	if (NX_IS_REVISION_P2(adapter->ahw.revision_id)) {
		/* try to scrub interrupt */
		netxen_napi_disable(adapter);

		netxen_napi_enable(adapter);

		netif_wake_queue(adapter->netdev);

		clear_bit(__NX_RESETTING, &adapter->state);
	} else {
		clear_bit(__NX_RESETTING, &adapter->state);
		if (netxen_nic_reset_context(adapter)) {
			rtnl_unlock();
			goto request_reset;
		}
	}
	adapter->netdev->trans_start = jiffies;
	rtnl_unlock();
	return;

request_reset:
	adapter->need_fw_reset = 1;
	clear_bit(__NX_RESETTING, &adapter->state);
}

static struct rtnl_link_stats64 *netxen_nic_get_stats(struct net_device *netdev,
						      struct rtnl_link_stats64 *stats)
{
	struct netxen_adapter *adapter = netdev_priv(netdev);

	stats->rx_packets = adapter->stats.rx_pkts + adapter->stats.lro_pkts;
	stats->tx_packets = adapter->stats.xmitfinished;
	stats->rx_bytes = adapter->stats.rxbytes;
	stats->tx_bytes = adapter->stats.txbytes;
	stats->rx_dropped = adapter->stats.rxdropped;
	stats->tx_dropped = adapter->stats.txdropped;

	return stats;
}

static irqreturn_t netxen_intr(int irq, void *data)
{
	struct nx_host_sds_ring *sds_ring = data;
	struct netxen_adapter *adapter = sds_ring->adapter;
	u32 status = 0;

	status = readl(adapter->isr_int_vec);

	if (!(status & adapter->int_vec_bit))
		return IRQ_NONE;

	if (NX_IS_REVISION_P3(adapter->ahw.revision_id)) {
		/* check interrupt state machine, to be sure */
		status = readl(adapter->crb_int_state_reg);
		if (!ISR_LEGACY_INT_TRIGGERED(status))
			return IRQ_NONE;

	} else {
		unsigned long our_int = 0;

		our_int = readl(adapter->crb_int_state_reg);

		/* not our interrupt */
		if (!test_and_clear_bit((7 + adapter->portnum), &our_int))
			return IRQ_NONE;

		/* claim interrupt */
		writel((our_int & 0xffffffff), adapter->crb_int_state_reg);

		/* clear interrupt */
		netxen_nic_disable_int(sds_ring);
	}

	writel(0xffffffff, adapter->tgt_status_reg);
	/* read twice to ensure write is flushed */
	readl(adapter->isr_int_vec);
	readl(adapter->isr_int_vec);

	napi_schedule(&sds_ring->napi);

	return IRQ_HANDLED;
}

static irqreturn_t netxen_msi_intr(int irq, void *data)
{
	struct nx_host_sds_ring *sds_ring = data;
	struct netxen_adapter *adapter = sds_ring->adapter;

	/* clear interrupt */
	writel(0xffffffff, adapter->tgt_status_reg);

	napi_schedule(&sds_ring->napi);
	return IRQ_HANDLED;
}

static irqreturn_t netxen_msix_intr(int irq, void *data)
{
	struct nx_host_sds_ring *sds_ring = data;

	napi_schedule(&sds_ring->napi);
	return IRQ_HANDLED;
}

static int netxen_nic_poll(struct napi_struct *napi, int budget)
{
	struct nx_host_sds_ring *sds_ring =
		container_of(napi, struct nx_host_sds_ring, napi);

	struct netxen_adapter *adapter = sds_ring->adapter;

	int tx_complete;
	int work_done;

	tx_complete = netxen_process_cmd_ring(adapter);

	work_done = netxen_process_rcv_ring(sds_ring, budget);

	if ((work_done < budget) && tx_complete) {
		napi_complete(&sds_ring->napi);
		if (test_bit(__NX_DEV_UP, &adapter->state))
			netxen_nic_enable_int(sds_ring);
	}

	return work_done;
}

#ifdef CONFIG_NET_POLL_CONTROLLER
static void netxen_nic_poll_controller(struct net_device *netdev)
{
	int ring;
	struct nx_host_sds_ring *sds_ring;
	struct netxen_adapter *adapter = netdev_priv(netdev);
	struct netxen_recv_context *recv_ctx = &adapter->recv_ctx;

	disable_irq(adapter->irq);
	for (ring = 0; ring < adapter->max_sds_rings; ring++) {
		sds_ring = &recv_ctx->sds_rings[ring];
		netxen_intr(adapter->irq, sds_ring);
	}
	enable_irq(adapter->irq);
}
#endif

static int
nx_incr_dev_ref_cnt(struct netxen_adapter *adapter)
{
	int count;
	if (netxen_api_lock(adapter))
		return -EIO;

	count = NXRD32(adapter, NX_CRB_DEV_REF_COUNT);

	NXWR32(adapter, NX_CRB_DEV_REF_COUNT, ++count);

	netxen_api_unlock(adapter);
	return count;
}

static int
nx_decr_dev_ref_cnt(struct netxen_adapter *adapter)
{
	int count;
	if (netxen_api_lock(adapter))
		return -EIO;

	count = NXRD32(adapter, NX_CRB_DEV_REF_COUNT);
	WARN_ON(count == 0);

	NXWR32(adapter, NX_CRB_DEV_REF_COUNT, --count);

	if (count == 0)
		NXWR32(adapter, NX_CRB_DEV_STATE, NX_DEV_COLD);

	netxen_api_unlock(adapter);
	return count;
}

static int
nx_dev_request_aer(struct netxen_adapter *adapter)
{
	u32 state;
	int ret = -EINVAL;

	if (netxen_api_lock(adapter))
		return ret;

	state = NXRD32(adapter, NX_CRB_DEV_STATE);

	if (state == NX_DEV_NEED_AER)
		ret = 0;
	else if (state == NX_DEV_READY) {
		NXWR32(adapter, NX_CRB_DEV_STATE, NX_DEV_NEED_AER);
		ret = 0;
	}

	netxen_api_unlock(adapter);
	return ret;
}

static int
nx_dev_request_reset(struct netxen_adapter *adapter)
{
	u32 state;
	int ret = -EINVAL;

	if (netxen_api_lock(adapter))
		return ret;

	state = NXRD32(adapter, NX_CRB_DEV_STATE);

	if (state == NX_DEV_NEED_RESET)
		ret = 0;
	else if (state != NX_DEV_INITALIZING && state != NX_DEV_NEED_AER) {
		NXWR32(adapter, NX_CRB_DEV_STATE, NX_DEV_NEED_RESET);
		ret = 0;
	}

	netxen_api_unlock(adapter);

	return ret;
}

static int
netxen_can_start_firmware(struct netxen_adapter *adapter)
{
	int count;
	int can_start = 0;

	if (netxen_api_lock(adapter))
		return 0;

	count = NXRD32(adapter, NX_CRB_DEV_REF_COUNT);

	if ((count < 0) || (count >= NX_MAX_PCI_FUNC))
		count = 0;

	if (count == 0) {
		can_start = 1;
		NXWR32(adapter, NX_CRB_DEV_STATE, NX_DEV_INITALIZING);
	}

	NXWR32(adapter, NX_CRB_DEV_REF_COUNT, ++count);

	netxen_api_unlock(adapter);

	return can_start;
}

static void
netxen_schedule_work(struct netxen_adapter *adapter,
		work_func_t func, int delay)
{
	INIT_DELAYED_WORK(&adapter->fw_work, func);
	schedule_delayed_work(&adapter->fw_work, delay);
}

static void
netxen_cancel_fw_work(struct netxen_adapter *adapter)
{
	while (test_and_set_bit(__NX_RESETTING, &adapter->state))
		msleep(10);

	cancel_delayed_work_sync(&adapter->fw_work);
}

static void
netxen_attach_work(struct work_struct *work)
{
	struct netxen_adapter *adapter = container_of(work,
				struct netxen_adapter, fw_work.work);
	struct net_device *netdev = adapter->netdev;
	int err = 0;

	if (netif_running(netdev)) {
		err = netxen_nic_attach(adapter);
		if (err)
			goto done;

		err = netxen_nic_up(adapter, netdev);
		if (err) {
			netxen_nic_detach(adapter);
			goto done;
		}

		netxen_restore_indev_addr(netdev, NETDEV_UP);
	}

	netif_device_attach(netdev);

done:
	adapter->fw_fail_cnt = 0;
	clear_bit(__NX_RESETTING, &adapter->state);
	netxen_schedule_work(adapter, netxen_fw_poll_work, FW_POLL_DELAY);
}

static void
netxen_fwinit_work(struct work_struct *work)
{
	struct netxen_adapter *adapter = container_of(work,
				struct netxen_adapter, fw_work.work);
	int dev_state;

	dev_state = NXRD32(adapter, NX_CRB_DEV_STATE);

	switch (dev_state) {
	case NX_DEV_COLD:
	case NX_DEV_READY:
		if (!netxen_start_firmware(adapter)) {
			netxen_schedule_work(adapter, netxen_attach_work, 0);
			return;
		}
		break;

	case NX_DEV_NEED_RESET:
	case NX_DEV_INITALIZING:
		if (++adapter->fw_wait_cnt < FW_POLL_THRESH) {
			netxen_schedule_work(adapter,
					netxen_fwinit_work, 2 * FW_POLL_DELAY);
			return;
		}

	case NX_DEV_FAILED:
	default:
		nx_incr_dev_ref_cnt(adapter);
		break;
	}

	clear_bit(__NX_RESETTING, &adapter->state);
}

static void
netxen_detach_work(struct work_struct *work)
{
	struct netxen_adapter *adapter = container_of(work,
				struct netxen_adapter, fw_work.work);
	struct net_device *netdev = adapter->netdev;
	int ref_cnt, delay;
	u32 status;

	netif_device_detach(netdev);

	netxen_nic_down(adapter, netdev);

	rtnl_lock();
	netxen_nic_detach(adapter);
	rtnl_unlock();

	status = NXRD32(adapter, NETXEN_PEG_HALT_STATUS1);

	if (status & NX_RCODE_FATAL_ERROR)
		goto err_ret;

	if (adapter->temp == NX_TEMP_PANIC)
		goto err_ret;

	ref_cnt = nx_decr_dev_ref_cnt(adapter);

	if (ref_cnt == -EIO)
		goto err_ret;

	delay = (ref_cnt == 0) ? 0 : (2 * FW_POLL_DELAY);

	adapter->fw_wait_cnt = 0;
	netxen_schedule_work(adapter, netxen_fwinit_work, delay);

	return;

err_ret:
	clear_bit(__NX_RESETTING, &adapter->state);
}

static int
netxen_check_health(struct netxen_adapter *adapter)
{
	u32 state, heartbit;
	struct net_device *netdev = adapter->netdev;

	state = NXRD32(adapter, NX_CRB_DEV_STATE);
	if (state == NX_DEV_NEED_AER)
		return 0;

	if (netxen_nic_check_temp(adapter))
		goto detach;

	if (adapter->need_fw_reset) {
		if (nx_dev_request_reset(adapter))
			return 0;
		goto detach;
	}

	/* NX_DEV_NEED_RESET, this state can be marked in two cases
	 * 1. Tx timeout 2. Fw hang
	 * Send request to destroy context in case of tx timeout only
	 * and doesn't required in case of Fw hang
	 */
	if (state == NX_DEV_NEED_RESET) {
		adapter->need_fw_reset = 1;
		if (NX_IS_REVISION_P2(adapter->ahw.revision_id))
			goto detach;
	}

	if (NX_IS_REVISION_P2(adapter->ahw.revision_id))
		return 0;

	heartbit = NXRD32(adapter, NETXEN_PEG_ALIVE_COUNTER);
	if (heartbit != adapter->heartbit) {
		adapter->heartbit = heartbit;
		adapter->fw_fail_cnt = 0;
		if (adapter->need_fw_reset)
			goto detach;
		return 0;
	}

	if (++adapter->fw_fail_cnt < FW_FAIL_THRESH)
		return 0;

	if (nx_dev_request_reset(adapter))
		return 0;

	clear_bit(__NX_FW_ATTACHED, &adapter->state);

	dev_info(&netdev->dev, "firmware hang detected\n");

detach:
	if ((auto_fw_reset == AUTO_FW_RESET_ENABLED) &&
			!test_and_set_bit(__NX_RESETTING, &adapter->state))
		netxen_schedule_work(adapter, netxen_detach_work, 0);
	return 1;
}

static void
netxen_fw_poll_work(struct work_struct *work)
{
	struct netxen_adapter *adapter = container_of(work,
				struct netxen_adapter, fw_work.work);

	if (test_bit(__NX_RESETTING, &adapter->state))
		goto reschedule;

	if (test_bit(__NX_DEV_UP, &adapter->state)) {
		if (!adapter->has_link_events) {

			netxen_nic_handle_phy_intr(adapter);

			if (adapter->link_changed)
				netxen_nic_set_link_parameters(adapter);
		}
	}

	if (netxen_check_health(adapter))
		return;

reschedule:
	netxen_schedule_work(adapter, netxen_fw_poll_work, FW_POLL_DELAY);
}

static ssize_t
netxen_store_bridged_mode(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	struct net_device *net = to_net_dev(dev);
	struct netxen_adapter *adapter = netdev_priv(net);
	unsigned long new;
	int ret = -EINVAL;

	if (!(adapter->capabilities & NX_FW_CAPABILITY_BDG))
		goto err_out;

	if (adapter->is_up != NETXEN_ADAPTER_UP_MAGIC)
		goto err_out;

	if (strict_strtoul(buf, 2, &new))
		goto err_out;

	if (!netxen_config_bridged_mode(adapter, !!new))
		ret = len;

err_out:
	return ret;
}

static ssize_t
netxen_show_bridged_mode(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct net_device *net = to_net_dev(dev);
	struct netxen_adapter *adapter;
	int bridged_mode = 0;

	adapter = netdev_priv(net);

	if (adapter->capabilities & NX_FW_CAPABILITY_BDG)
		bridged_mode = !!(adapter->flags & NETXEN_NIC_BRIDGE_ENABLED);

	return sprintf(buf, "%d\n", bridged_mode);
}

static struct device_attribute dev_attr_bridged_mode = {
       .attr = {.name = "bridged_mode", .mode = (S_IRUGO | S_IWUSR)},
       .show = netxen_show_bridged_mode,
       .store = netxen_store_bridged_mode,
};

static ssize_t
netxen_store_diag_mode(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	struct netxen_adapter *adapter = dev_get_drvdata(dev);
	unsigned long new;

	if (strict_strtoul(buf, 2, &new))
		return -EINVAL;

	if (!!new != !!(adapter->flags & NETXEN_NIC_DIAG_ENABLED))
		adapter->flags ^= NETXEN_NIC_DIAG_ENABLED;

	return len;
}

static ssize_t
netxen_show_diag_mode(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct netxen_adapter *adapter = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n",
			!!(adapter->flags & NETXEN_NIC_DIAG_ENABLED));
}

static struct device_attribute dev_attr_diag_mode = {
	.attr = {.name = "diag_mode", .mode = (S_IRUGO | S_IWUSR)},
	.show = netxen_show_diag_mode,
	.store = netxen_store_diag_mode,
};

static int
netxen_sysfs_validate_crb(struct netxen_adapter *adapter,
		loff_t offset, size_t size)
{
	size_t crb_size = 4;

	if (!(adapter->flags & NETXEN_NIC_DIAG_ENABLED))
		return -EIO;

	if (offset < NETXEN_PCI_CRBSPACE) {
		if (NX_IS_REVISION_P2(adapter->ahw.revision_id))
			return -EINVAL;

		if (ADDR_IN_RANGE(offset, NETXEN_PCI_CAMQM,
						NETXEN_PCI_CAMQM_2M_END))
			crb_size = 8;
		else
			return -EINVAL;
	}

	if ((size != crb_size) || (offset & (crb_size-1)))
		return  -EINVAL;

	return 0;
}

static ssize_t
netxen_sysfs_read_crb(struct file *filp, struct kobject *kobj,
		struct bin_attribute *attr,
		char *buf, loff_t offset, size_t size)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct netxen_adapter *adapter = dev_get_drvdata(dev);
	u32 data;
	u64 qmdata;
	int ret;

	ret = netxen_sysfs_validate_crb(adapter, offset, size);
	if (ret != 0)
		return ret;

	if (NX_IS_REVISION_P3(adapter->ahw.revision_id) &&
		ADDR_IN_RANGE(offset, NETXEN_PCI_CAMQM,
					NETXEN_PCI_CAMQM_2M_END)) {
		netxen_pci_camqm_read_2M(adapter, offset, &qmdata);
		memcpy(buf, &qmdata, size);
	} else {
		data = NXRD32(adapter, offset);
		memcpy(buf, &data, size);
	}

	return size;
}

static ssize_t
netxen_sysfs_write_crb(struct file *filp, struct kobject *kobj,
		struct bin_attribute *attr,
		char *buf, loff_t offset, size_t size)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct netxen_adapter *adapter = dev_get_drvdata(dev);
	u32 data;
	u64 qmdata;
	int ret;

	ret = netxen_sysfs_validate_crb(adapter, offset, size);
	if (ret != 0)
		return ret;

	if (NX_IS_REVISION_P3(adapter->ahw.revision_id) &&
		ADDR_IN_RANGE(offset, NETXEN_PCI_CAMQM,
					NETXEN_PCI_CAMQM_2M_END)) {
		memcpy(&qmdata, buf, size);
		netxen_pci_camqm_write_2M(adapter, offset, qmdata);
	} else {
		memcpy(&data, buf, size);
		NXWR32(adapter, offset, data);
	}

	return size;
}

static int
netxen_sysfs_validate_mem(struct netxen_adapter *adapter,
		loff_t offset, size_t size)
{
	if (!(adapter->flags & NETXEN_NIC_DIAG_ENABLED))
		return -EIO;

	if ((size != 8) || (offset & 0x7))
		return  -EIO;

	return 0;
}

static ssize_t
netxen_sysfs_read_mem(struct file *filp, struct kobject *kobj,
		struct bin_attribute *attr,
		char *buf, loff_t offset, size_t size)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct netxen_adapter *adapter = dev_get_drvdata(dev);
	u64 data;
	int ret;

	ret = netxen_sysfs_validate_mem(adapter, offset, size);
	if (ret != 0)
		return ret;

	if (adapter->pci_mem_read(adapter, offset, &data))
		return -EIO;

	memcpy(buf, &data, size);

	return size;
}

static ssize_t netxen_sysfs_write_mem(struct file *filp, struct kobject *kobj,
		struct bin_attribute *attr, char *buf,
		loff_t offset, size_t size)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct netxen_adapter *adapter = dev_get_drvdata(dev);
	u64 data;
	int ret;

	ret = netxen_sysfs_validate_mem(adapter, offset, size);
	if (ret != 0)
		return ret;

	memcpy(&data, buf, size);

	if (adapter->pci_mem_write(adapter, offset, data))
		return -EIO;

	return size;
}


static struct bin_attribute bin_attr_crb = {
	.attr = {.name = "crb", .mode = (S_IRUGO | S_IWUSR)},
	.size = 0,
	.read = netxen_sysfs_read_crb,
	.write = netxen_sysfs_write_crb,
};

static struct bin_attribute bin_attr_mem = {
	.attr = {.name = "mem", .mode = (S_IRUGO | S_IWUSR)},
	.size = 0,
	.read = netxen_sysfs_read_mem,
	.write = netxen_sysfs_write_mem,
};


static void
netxen_create_sysfs_entries(struct netxen_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;
	struct device *dev = &netdev->dev;

	if (adapter->capabilities & NX_FW_CAPABILITY_BDG) {
		/* bridged_mode control */
		if (device_create_file(dev, &dev_attr_bridged_mode)) {
			dev_warn(&netdev->dev,
				"failed to create bridged_mode sysfs entry\n");
		}
	}
}

static void
netxen_remove_sysfs_entries(struct netxen_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;
	struct device *dev = &netdev->dev;

	if (adapter->capabilities & NX_FW_CAPABILITY_BDG)
		device_remove_file(dev, &dev_attr_bridged_mode);
}

static void
netxen_create_diag_entries(struct netxen_adapter *adapter)
{
	struct pci_dev *pdev = adapter->pdev;
	struct device *dev;

	dev = &pdev->dev;
	if (device_create_file(dev, &dev_attr_diag_mode))
		dev_info(dev, "failed to create diag_mode sysfs entry\n");
	if (device_create_bin_file(dev, &bin_attr_crb))
		dev_info(dev, "failed to create crb sysfs entry\n");
	if (device_create_bin_file(dev, &bin_attr_mem))
		dev_info(dev, "failed to create mem sysfs entry\n");
}


static void
netxen_remove_diag_entries(struct netxen_adapter *adapter)
{
	struct pci_dev *pdev = adapter->pdev;
	struct device *dev = &pdev->dev;

	device_remove_file(dev, &dev_attr_diag_mode);
	device_remove_bin_file(dev, &bin_attr_crb);
	device_remove_bin_file(dev, &bin_attr_mem);
}

#ifdef CONFIG_INET

#define is_netxen_netdev(dev) (dev->netdev_ops == &netxen_netdev_ops)

static int
netxen_destip_supported(struct netxen_adapter *adapter)
{
	if (NX_IS_REVISION_P2(adapter->ahw.revision_id))
		return 0;

	if (adapter->ahw.cut_through)
		return 0;

	return 1;
}

static void
netxen_free_vlan_ip_list(struct netxen_adapter *adapter)
{
	struct nx_vlan_ip_list  *cur;
	struct list_head *head = &adapter->vlan_ip_list;

	while (!list_empty(head)) {
		cur = list_entry(head->next, struct nx_vlan_ip_list, list);
		netxen_config_ipaddr(adapter, cur->ip_addr, NX_IP_DOWN);
		list_del(&cur->list);
		kfree(cur);
	}

}
static void
netxen_list_config_vlan_ip(struct netxen_adapter *adapter,
		struct in_ifaddr *ifa, unsigned long event)
{
	struct net_device *dev;
	struct nx_vlan_ip_list *cur, *tmp_cur;
	struct list_head *head;

	dev = ifa->ifa_dev ? ifa->ifa_dev->dev : NULL;

	if (dev == NULL)
		return;

	if (!is_vlan_dev(dev))
		return;

	switch (event) {
	case NX_IP_UP:
		list_for_each(head, &adapter->vlan_ip_list) {
			cur = list_entry(head, struct nx_vlan_ip_list, list);

			if (cur->ip_addr == ifa->ifa_address)
				return;
		}

		cur = kzalloc(sizeof(struct nx_vlan_ip_list), GFP_ATOMIC);
		if (cur == NULL) {
			printk(KERN_ERR "%s: failed to add vlan ip to list\n",
					adapter->netdev->name);
			return;
		}

		cur->ip_addr = ifa->ifa_address;
		list_add_tail(&cur->list, &adapter->vlan_ip_list);
		break;
	case NX_IP_DOWN:
		list_for_each_entry_safe(cur, tmp_cur,
					&adapter->vlan_ip_list, list) {
			if (cur->ip_addr == ifa->ifa_address) {
				list_del(&cur->list);
				kfree(cur);
				break;
			}
		}
	}
}
static void
netxen_config_indev_addr(struct netxen_adapter *adapter,
		struct net_device *dev, unsigned long event)
{
	struct in_device *indev;

	if (!netxen_destip_supported(adapter))
		return;

	indev = in_dev_get(dev);
	if (!indev)
		return;

	for_ifa(indev) {
		switch (event) {
		case NETDEV_UP:
			netxen_config_ipaddr(adapter,
					ifa->ifa_address, NX_IP_UP);
			netxen_list_config_vlan_ip(adapter, ifa, NX_IP_UP);
			break;
		case NETDEV_DOWN:
			netxen_config_ipaddr(adapter,
					ifa->ifa_address, NX_IP_DOWN);
			netxen_list_config_vlan_ip(adapter, ifa, NX_IP_DOWN);
			break;
		default:
			break;
		}
	} endfor_ifa(indev);

	in_dev_put(indev);
}

static void
netxen_restore_indev_addr(struct net_device *netdev, unsigned long event)

{
	struct netxen_adapter *adapter = netdev_priv(netdev);
	struct nx_vlan_ip_list *pos, *tmp_pos;
	unsigned long ip_event;

	ip_event = (event == NETDEV_UP) ? NX_IP_UP : NX_IP_DOWN;
	netxen_config_indev_addr(adapter, netdev, event);

	list_for_each_entry_safe(pos, tmp_pos, &adapter->vlan_ip_list, list) {
		netxen_config_ipaddr(adapter, pos->ip_addr, ip_event);
	}
}

static int netxen_netdev_event(struct notifier_block *this,
				 unsigned long event, void *ptr)
{
	struct netxen_adapter *adapter;
	struct net_device *dev = (struct net_device *)ptr;
	struct net_device *orig_dev = dev;

recheck:
	if (dev == NULL)
		goto done;

	if (dev->priv_flags & IFF_802_1Q_VLAN) {
		dev = vlan_dev_real_dev(dev);
		goto recheck;
	}

	if (!is_netxen_netdev(dev))
		goto done;

	adapter = netdev_priv(dev);

	if (!adapter)
		goto done;

	if (adapter->is_up != NETXEN_ADAPTER_UP_MAGIC)
		goto done;

	netxen_config_indev_addr(adapter, orig_dev, event);
done:
	return NOTIFY_DONE;
}

static int
netxen_inetaddr_event(struct notifier_block *this,
		unsigned long event, void *ptr)
{
	struct netxen_adapter *adapter;
	struct net_device *dev;

	struct in_ifaddr *ifa = (struct in_ifaddr *)ptr;

	dev = ifa->ifa_dev ? ifa->ifa_dev->dev : NULL;

recheck:
	if (dev == NULL)
		goto done;

	if (dev->priv_flags & IFF_802_1Q_VLAN) {
		dev = vlan_dev_real_dev(dev);
		goto recheck;
	}

	if (!is_netxen_netdev(dev))
		goto done;

	adapter = netdev_priv(dev);

	if (!adapter || !netxen_destip_supported(adapter))
		goto done;

	if (adapter->is_up != NETXEN_ADAPTER_UP_MAGIC)
		goto done;

	switch (event) {
	case NETDEV_UP:
		netxen_config_ipaddr(adapter, ifa->ifa_address, NX_IP_UP);
		netxen_list_config_vlan_ip(adapter, ifa, NX_IP_UP);
		break;
	case NETDEV_DOWN:
		netxen_config_ipaddr(adapter, ifa->ifa_address, NX_IP_DOWN);
		netxen_list_config_vlan_ip(adapter, ifa, NX_IP_DOWN);
		break;
	default:
		break;
	}

done:
	return NOTIFY_DONE;
}

static struct notifier_block	netxen_netdev_cb = {
	.notifier_call = netxen_netdev_event,
};

static struct notifier_block netxen_inetaddr_cb = {
	.notifier_call = netxen_inetaddr_event,
};
#else
static void
netxen_restore_indev_addr(struct net_device *dev, unsigned long event)
{ }
static void
netxen_free_vlan_ip_list(struct netxen_adapter *adapter)
{ }
#endif

static struct pci_error_handlers netxen_err_handler = {
	.error_detected = netxen_io_error_detected,
	.slot_reset = netxen_io_slot_reset,
	.resume = netxen_io_resume,
};

static struct pci_driver netxen_driver = {
	.name = netxen_nic_driver_name,
	.id_table = netxen_pci_tbl,
	.probe = netxen_nic_probe,
	.remove = __devexit_p(netxen_nic_remove),
#ifdef CONFIG_PM
	.suspend = netxen_nic_suspend,
	.resume = netxen_nic_resume,
#endif
	.shutdown = netxen_nic_shutdown,
	.err_handler = &netxen_err_handler
};

static int __init netxen_init_module(void)
{
	printk(KERN_INFO "%s\n", netxen_nic_driver_string);

#ifdef CONFIG_INET
	register_netdevice_notifier(&netxen_netdev_cb);
	register_inetaddr_notifier(&netxen_inetaddr_cb);
#endif
	return pci_register_driver(&netxen_driver);
}

module_init(netxen_init_module);

static void __exit netxen_exit_module(void)
{
	pci_unregister_driver(&netxen_driver);

#ifdef CONFIG_INET
	unregister_inetaddr_notifier(&netxen_inetaddr_cb);
	unregister_netdevice_notifier(&netxen_netdev_cb);
#endif
}

module_exit(netxen_exit_module);
