/*
 * Copyright (C) 2003 - 2009 NetXen, Inc.
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
 * NetXen Inc,
 * 18922 Forge Drive
 * Cupertino, CA 95014-0701
 *
 */

#include <linux/vmalloc.h>
#include <linux/interrupt.h>
#include "netxen_nic_hw.h"

#include "netxen_nic.h"
#include "netxen_nic_phan_reg.h"

#include <linux/dma-mapping.h>
#include <linux/if_vlan.h>
#include <net/ip.h>
#include <linux/ipv6.h>

MODULE_DESCRIPTION("NetXen Multi port (1/10) Gigabit Network Driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(NETXEN_NIC_LINUX_VERSIONID);

char netxen_nic_driver_name[] = "netxen_nic";
static char netxen_nic_driver_string[] = "NetXen Network Driver version "
    NETXEN_NIC_LINUX_VERSIONID;

static int port_mode = NETXEN_PORT_MODE_AUTO_NEG;

/* Default to restricted 1G auto-neg mode */
static int wol_port_mode = 5;

static int use_msi = 1;

static int use_msi_x = 1;

/* Local functions to NetXen NIC driver */
static int __devinit netxen_nic_probe(struct pci_dev *pdev,
		const struct pci_device_id *ent);
static void __devexit netxen_nic_remove(struct pci_dev *pdev);
static int netxen_nic_open(struct net_device *netdev);
static int netxen_nic_close(struct net_device *netdev);
static int netxen_nic_xmit_frame(struct sk_buff *, struct net_device *);
static void netxen_tx_timeout(struct net_device *netdev);
static void netxen_tx_timeout_task(struct work_struct *work);
static void netxen_watchdog(unsigned long);
static int netxen_nic_poll(struct napi_struct *napi, int budget);
#ifdef CONFIG_NET_POLL_CONTROLLER
static void netxen_nic_poll_controller(struct net_device *netdev);
#endif
static irqreturn_t netxen_intr(int irq, void *data);
static irqreturn_t netxen_msi_intr(int irq, void *data);
static irqreturn_t netxen_msix_intr(int irq, void *data);

/*  PCI Device ID Table  */
#define ENTRY(device) \
	{PCI_DEVICE(PCI_VENDOR_ID_NETXEN, (device)), \
	.class = PCI_CLASS_NETWORK_ETHERNET << 8, .class_mask = ~0}

static struct pci_device_id netxen_pci_tbl[] __devinitdata = {
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

static struct workqueue_struct *netxen_workq;
#define SCHEDULE_WORK(tp)	queue_work(netxen_workq, tp)
#define FLUSH_SCHEDULED_WORK()	flush_workqueue(netxen_workq)

static void netxen_watchdog(unsigned long);

static uint32_t crb_cmd_producer[4] = {
	CRB_CMD_PRODUCER_OFFSET, CRB_CMD_PRODUCER_OFFSET_1,
	CRB_CMD_PRODUCER_OFFSET_2, CRB_CMD_PRODUCER_OFFSET_3
};

void
netxen_nic_update_cmd_producer(struct netxen_adapter *adapter,
		struct nx_host_tx_ring *tx_ring, u32 producer)
{
	adapter->pci_write_normalize(adapter,
			tx_ring->crb_cmd_producer, producer);
}

static uint32_t crb_cmd_consumer[4] = {
	CRB_CMD_CONSUMER_OFFSET, CRB_CMD_CONSUMER_OFFSET_1,
	CRB_CMD_CONSUMER_OFFSET_2, CRB_CMD_CONSUMER_OFFSET_3
};

static inline void
netxen_nic_update_cmd_consumer(struct netxen_adapter *adapter,
		struct nx_host_tx_ring *tx_ring, u32 consumer)
{
	adapter->pci_write_normalize(adapter,
			tx_ring->crb_cmd_consumer, consumer);
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

	adapter->pci_write_normalize(adapter, sds_ring->crb_intr_mask, 0);
}

static inline void netxen_nic_enable_int(struct nx_host_sds_ring *sds_ring)
{
	struct netxen_adapter *adapter = sds_ring->adapter;

	adapter->pci_write_normalize(adapter, sds_ring->crb_intr_mask, 0x1);

	if (!NETXEN_IS_MSI_FAMILY(adapter))
		adapter->pci_write_immediate(adapter,
				adapter->legacy_intr.tgt_mask_reg, 0xfbff);
}

static void
netxen_napi_add(struct netxen_adapter *adapter, struct net_device *netdev)
{
	int ring;
	struct nx_host_sds_ring *sds_ring;
	struct netxen_recv_context *recv_ctx = &adapter->recv_ctx;

	if (adapter->flags & NETXEN_NIC_MSIX_ENABLED)
		adapter->max_sds_rings = (num_online_cpus() >= 4) ? 4 : 2;
	else
		adapter->max_sds_rings = 1;

	for (ring = 0; ring < adapter->max_sds_rings; ring++) {
		sds_ring = &recv_ctx->sds_rings[ring];
		netif_napi_add(netdev, &sds_ring->napi,
				netxen_nic_poll, NETXEN_NETDEV_WEIGHT);
	}
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
		napi_disable(&sds_ring->napi);
	}
}

static int nx_set_dma_mask(struct netxen_adapter *adapter, uint8_t revision_id)
{
	struct pci_dev *pdev = adapter->pdev;
	uint64_t mask, cmask;

	adapter->pci_using_dac = 0;

	mask = DMA_BIT_MASK(32);
	/*
	 * Consistent DMA mask is set to 32 bit because it cannot be set to
	 * 35 bits. For P3 also leave it at 32 bits for now. Only the rings
	 * come off this pool.
	 */
	cmask = DMA_BIT_MASK(32);

#ifndef CONFIG_IA64
	if (revision_id >= NX_P3_B0)
		mask = DMA_BIT_MASK(39);
	else if (revision_id == NX_P2_C1)
		mask = DMA_BIT_MASK(35);
#endif
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
	uint64_t mask, old_mask;
	struct pci_dev *pdev = adapter->pdev;

	change = 0;

	shift = netxen_nic_reg_read(adapter, CRB_DMA_SHIFT);
	if (shift >= 32)
		return 0;

	if (NX_IS_REVISION_P3(adapter->ahw.revision_id) && (shift > 9))
		change = 1;
	else if ((adapter->ahw.revision_id == NX_P2_C1) && (shift <= 4))
		change = 1;

	if (change) {
		old_mask = pdev->dma_mask;
		mask = (1ULL<<(32+shift)) - 1;

		err = pci_set_dma_mask(pdev, mask);
		if (err)
			return pci_set_dma_mask(pdev, old_mask);
	}

	return 0;
}

static void netxen_check_options(struct netxen_adapter *adapter)
{
	if (adapter->ahw.port_type == NETXEN_NIC_XGBE)
		adapter->num_rxd = MAX_RCV_DESCRIPTORS_10G;
	else if (adapter->ahw.port_type == NETXEN_NIC_GBE)
		adapter->num_rxd = MAX_RCV_DESCRIPTORS_1G;

	if (NX_IS_REVISION_P3(adapter->ahw.revision_id))
		adapter->msix_supported = !!use_msi_x;
	else
		adapter->msix_supported = 0;

	adapter->num_txd = MAX_CMD_DESCRIPTORS_HOST;
	adapter->num_jumbo_rxd = MAX_JUMBO_RCV_DESCRIPTORS;
	adapter->num_lro_rxd = MAX_LRO_RCV_DESCRIPTORS;

	return;
}

static int
netxen_check_hw_init(struct netxen_adapter *adapter, int first_boot)
{
	u32 val, timeout;

	if (first_boot == 0x55555555) {
		/* This is the first boot after power up */
		adapter->pci_write_normalize(adapter,
			NETXEN_CAM_RAM(0x1fc), NETXEN_BDINFO_MAGIC);

		if (!NX_IS_REVISION_P2(adapter->ahw.revision_id))
			return 0;

		/* PCI bus master workaround */
		adapter->hw_read_wx(adapter,
			NETXEN_PCIE_REG(0x4), &first_boot, 4);
		if (!(first_boot & 0x4)) {
			first_boot |= 0x4;
			adapter->hw_write_wx(adapter,
				NETXEN_PCIE_REG(0x4), &first_boot, 4);
			adapter->hw_read_wx(adapter,
				NETXEN_PCIE_REG(0x4), &first_boot, 4);
		}

		/* This is the first boot after power up */
		adapter->hw_read_wx(adapter,
			NETXEN_ROMUSB_GLB_SW_RESET, &first_boot, 4);
		if (first_boot != 0x80000f) {
			/* clear the register for future unloads/loads */
			adapter->pci_write_normalize(adapter,
					NETXEN_CAM_RAM(0x1fc), 0);
			return -EIO;
		}

		/* Start P2 boot loader */
		val = adapter->pci_read_normalize(adapter,
				NETXEN_ROMUSB_GLB_PEGTUNE_DONE);
		adapter->pci_write_normalize(adapter,
				NETXEN_ROMUSB_GLB_PEGTUNE_DONE, val | 0x1);
		timeout = 0;
		do {
			msleep(1);
			val = adapter->pci_read_normalize(adapter,
					NETXEN_CAM_RAM(0x1fc));

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
			adapter->hw_write_wx(adapter,
				NETXEN_PORT_MODE_ADDR, &data, 4);
		} else if (port_mode == NETXEN_PORT_MODE_XG) {
			data = NETXEN_PORT_MODE_XG;
			adapter->hw_write_wx(adapter,
				NETXEN_PORT_MODE_ADDR, &data, 4);
		} else if (port_mode == NETXEN_PORT_MODE_AUTO_NEG_1G) {
			data = NETXEN_PORT_MODE_AUTO_NEG_1G;
			adapter->hw_write_wx(adapter,
				NETXEN_PORT_MODE_ADDR, &data, 4);
		} else if (port_mode == NETXEN_PORT_MODE_AUTO_NEG_XG) {
			data = NETXEN_PORT_MODE_AUTO_NEG_XG;
			adapter->hw_write_wx(adapter,
				NETXEN_PORT_MODE_ADDR, &data, 4);
		} else {
			data = NETXEN_PORT_MODE_AUTO_NEG;
			adapter->hw_write_wx(adapter,
				NETXEN_PORT_MODE_ADDR, &data, 4);
		}

		if ((wol_port_mode != NETXEN_PORT_MODE_802_3_AP) &&
			(wol_port_mode != NETXEN_PORT_MODE_XG) &&
			(wol_port_mode != NETXEN_PORT_MODE_AUTO_NEG_1G) &&
			(wol_port_mode != NETXEN_PORT_MODE_AUTO_NEG_XG)) {
			wol_port_mode = NETXEN_PORT_MODE_AUTO_NEG;
		}
		adapter->hw_write_wx(adapter, NETXEN_WOL_PORT_MODE,
			&wol_port_mode, 4);
	}
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

static void netxen_init_msix_entries(struct netxen_adapter *adapter)
{
	int i;

	for (i = 0; i < MSIX_ENTRIES_PER_ADAPTER; i++)
		adapter->msix_entries[i].entry = i;
}

static int
netxen_read_mac_addr(struct netxen_adapter *adapter)
{
	int i;
	unsigned char *p;
	__le64 mac_addr;
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

	/* set station address */

	if (!is_valid_ether_addr(netdev->perm_addr))
		dev_warn(&pdev->dev, "Bad MAC address %pM.\n", netdev->dev_addr);
	else
		adapter->macaddr_set(adapter, netdev->dev_addr);

	return 0;
}

static void netxen_set_multicast_list(struct net_device *dev)
{
	struct netxen_adapter *adapter = netdev_priv(dev);

	if (NX_IS_REVISION_P3(adapter->ahw.revision_id))
		netxen_p3_nic_set_multi(dev);
	else
		netxen_p2_nic_set_multi(dev);
}

static const struct net_device_ops netxen_netdev_ops = {
	.ndo_open	   = netxen_nic_open,
	.ndo_stop	   = netxen_nic_close,
	.ndo_start_xmit    = netxen_nic_xmit_frame,
	.ndo_get_stats	   = netxen_nic_get_stats,
	.ndo_validate_addr = eth_validate_addr,
	.ndo_set_multicast_list = netxen_set_multicast_list,
	.ndo_set_mac_address    = netxen_nic_set_mac,
	.ndo_change_mtu	   = netxen_nic_change_mtu,
	.ndo_tx_timeout	   = netxen_tx_timeout,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller = netxen_nic_poll_controller,
#endif
};

static void
netxen_setup_intr(struct netxen_adapter *adapter)
{
	struct netxen_legacy_intr_set *legacy_intrp;
	struct pci_dev *pdev = adapter->pdev;

	adapter->flags &= ~(NETXEN_NIC_MSI_ENABLED | NETXEN_NIC_MSIX_ENABLED);

	if (adapter->ahw.revision_id >= NX_P3_B0)
		legacy_intrp = &legacy_intr[adapter->ahw.pci_func];
	else
		legacy_intrp = &legacy_intr[0];
	adapter->legacy_intr.int_vec_bit = legacy_intrp->int_vec_bit;
	adapter->legacy_intr.tgt_status_reg = legacy_intrp->tgt_status_reg;
	adapter->legacy_intr.tgt_mask_reg = legacy_intrp->tgt_mask_reg;
	adapter->legacy_intr.pci_int_reg = legacy_intrp->pci_int_reg;

	netxen_set_msix_bit(pdev, 0);

	if (adapter->msix_supported) {

		netxen_init_msix_entries(adapter);
		if (pci_enable_msix(pdev, adapter->msix_entries,
					MSIX_ENTRIES_PER_ADAPTER))
			goto request_msi;

		adapter->flags |= NETXEN_NIC_MSIX_ENABLED;
		netxen_set_msix_bit(pdev, 1);
		dev_info(&pdev->dev, "using msi-x interrupts\n");

	} else {
request_msi:
		if (use_msi && !pci_enable_msi(pdev)) {
			adapter->flags |= NETXEN_NIC_MSI_ENABLED;
			dev_info(&pdev->dev, "using msi interrupts\n");
		} else
			dev_info(&pdev->dev, "using legacy interrupts\n");
		adapter->msix_entries[0].vector = pdev->irq;
	}
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
	void __iomem *mem_ptr0 = NULL;
	void __iomem *mem_ptr1 = NULL;
	void __iomem *mem_ptr2 = NULL;
	void __iomem *db_ptr = NULL;

	unsigned long mem_base, mem_len, db_base, db_len = 0, pci_len0 = 0;

	struct pci_dev *pdev = adapter->pdev;
	int pci_func = adapter->ahw.pci_func;

	int err = 0;

	/*
	 * Set the CRB window to invalid. If any register in window 0 is
	 * accessed it should set the window to 0 and then reset it to 1.
	 */
	adapter->curr_window = 255;
	adapter->ahw.qdr_sn_window = -1;
	adapter->ahw.ddr_mn_window = -1;

	/* remap phys address */
	mem_base = pci_resource_start(pdev, 0);	/* 0 is for BAR 0 */
	mem_len = pci_resource_len(pdev, 0);
	pci_len0 = 0;

	adapter->hw_write_wx = netxen_nic_hw_write_wx_128M;
	adapter->hw_read_wx = netxen_nic_hw_read_wx_128M;
	adapter->pci_read_immediate = netxen_nic_pci_read_immediate_128M;
	adapter->pci_write_immediate = netxen_nic_pci_write_immediate_128M;
	adapter->pci_read_normalize = netxen_nic_pci_read_normalize_128M;
	adapter->pci_write_normalize = netxen_nic_pci_write_normalize_128M;
	adapter->pci_set_window = netxen_nic_pci_set_window_128M;
	adapter->pci_mem_read = netxen_nic_pci_mem_read_128M;
	adapter->pci_mem_write = netxen_nic_pci_mem_write_128M;

	/* 128 Meg of memory */
	if (mem_len == NETXEN_PCI_128MB_SIZE) {
		mem_ptr0 = ioremap(mem_base, FIRST_PAGE_GROUP_SIZE);
		mem_ptr1 = ioremap(mem_base + SECOND_PAGE_GROUP_START,
				SECOND_PAGE_GROUP_SIZE);
		mem_ptr2 = ioremap(mem_base + THIRD_PAGE_GROUP_START,
				THIRD_PAGE_GROUP_SIZE);
	} else if (mem_len == NETXEN_PCI_32MB_SIZE) {
		mem_ptr1 = ioremap(mem_base, SECOND_PAGE_GROUP_SIZE);
		mem_ptr2 = ioremap(mem_base + THIRD_PAGE_GROUP_START -
			SECOND_PAGE_GROUP_START, THIRD_PAGE_GROUP_SIZE);
	} else if (mem_len == NETXEN_PCI_2MB_SIZE) {
		adapter->hw_write_wx = netxen_nic_hw_write_wx_2M;
		adapter->hw_read_wx = netxen_nic_hw_read_wx_2M;
		adapter->pci_read_immediate = netxen_nic_pci_read_immediate_2M;
		adapter->pci_write_immediate =
			netxen_nic_pci_write_immediate_2M;
		adapter->pci_read_normalize = netxen_nic_pci_read_normalize_2M;
		adapter->pci_write_normalize =
			netxen_nic_pci_write_normalize_2M;
		adapter->pci_set_window = netxen_nic_pci_set_window_2M;
		adapter->pci_mem_read = netxen_nic_pci_mem_read_2M;
		adapter->pci_mem_write = netxen_nic_pci_mem_write_2M;

		mem_ptr0 = pci_ioremap_bar(pdev, 0);
		if (mem_ptr0 == NULL) {
			dev_err(&pdev->dev, "failed to map PCI bar 0\n");
			return -EIO;
		}
		pci_len0 = mem_len;

		adapter->ahw.ddr_mn_window = 0;
		adapter->ahw.qdr_sn_window = 0;

		adapter->ahw.mn_win_crb = 0x100000 + PCIX_MN_WINDOW +
			(pci_func * 0x20);
		adapter->ahw.ms_win_crb = 0x100000 + PCIX_SN_WINDOW;
		if (pci_func < 4)
			adapter->ahw.ms_win_crb += (pci_func * 0x20);
		else
			adapter->ahw.ms_win_crb +=
					0xA0 + ((pci_func - 4) * 0x10);
	} else {
		return -EIO;
	}

	dev_info(&pdev->dev, "%dMB memory map\n", (int)(mem_len>>20));

	adapter->ahw.pci_base0 = mem_ptr0;
	adapter->ahw.pci_len0 = pci_len0;
	adapter->ahw.pci_base1 = mem_ptr1;
	adapter->ahw.pci_base2 = mem_ptr2;

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

static int
netxen_start_firmware(struct netxen_adapter *adapter)
{
	int val, err, first_boot;
	struct pci_dev *pdev = adapter->pdev;

	int first_driver = 0;
	if (NX_IS_REVISION_P3(adapter->ahw.revision_id)) {
		if (adapter->ahw.pci_func == 0)
			first_driver = 1;
	} else {
		if (adapter->portnum == 0)
			first_driver = 1;
	}

	if (!first_driver)
		return 0;

	first_boot = adapter->pci_read_normalize(adapter,
			NETXEN_CAM_RAM(0x1fc));

	err = netxen_check_hw_init(adapter, first_boot);
	if (err) {
		dev_err(&pdev->dev, "error in init HW init sequence\n");
		return err;
	}

	if (first_boot != 0x55555555) {
		adapter->pci_write_normalize(adapter,
					CRB_CMDPEG_STATE, 0);
		netxen_pinit_from_rom(adapter, 0);
		msleep(1);
	}

	netxen_nic_reg_write(adapter, CRB_DMA_SHIFT, 0x55555555);
	if (NX_IS_REVISION_P3(adapter->ahw.revision_id))
		netxen_set_port_mode(adapter);

	netxen_load_firmware(adapter);

	if (NX_IS_REVISION_P2(adapter->ahw.revision_id)) {

		/* Initialize multicast addr pool owners */
		val = 0x7654;
		if (adapter->ahw.port_type == NETXEN_NIC_XGBE)
			val |= 0x0f000000;
		netxen_crb_writelit_adapter(adapter,
				NETXEN_MAC_ADDR_CNTL_REG, val);

	}

	err = netxen_initialize_adapter_offload(adapter);
	if (err)
		return err;

	/*
	 * Tell the hardware our version number.
	 */
	val = (_NETXEN_NIC_LINUX_MAJOR << 16)
		| ((_NETXEN_NIC_LINUX_MINOR << 8))
		| (_NETXEN_NIC_LINUX_SUBVERSION);
	adapter->pci_write_normalize(adapter, CRB_DRIVER_VERSION, val);

	/* Handshake with the card before we register the devices. */
	err = netxen_phantom_init(adapter, NETXEN_NIC_PEG_TUNE);
	if (err) {
		netxen_free_adapter_offload(adapter);
		return err;
	}

	return 0;
}

static int
netxen_nic_request_irq(struct netxen_adapter *adapter)
{
	irq_handler_t handler;
	struct nx_host_sds_ring *sds_ring;
	int err, ring;

	unsigned long flags = IRQF_SAMPLE_RANDOM;
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
		sprintf(sds_ring->name, "%16s[%d]", netdev->name, ring);
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

static int
netxen_nic_up(struct netxen_adapter *adapter, struct net_device *netdev)
{
	int err;

	err = adapter->init_port(adapter, adapter->physical_port);
	if (err) {
		printk(KERN_ERR "%s: Failed to initialize port %d\n",
				netxen_nic_driver_name, adapter->portnum);
		return err;
	}
	adapter->macaddr_set(adapter, netdev->dev_addr);

	netxen_nic_set_link_parameters(adapter);

	netxen_set_multicast_list(netdev);
	if (adapter->set_mtu)
		adapter->set_mtu(adapter, netdev->mtu);

	adapter->ahw.linkup = 0;
	mod_timer(&adapter->watchdog_timer, jiffies);

	netxen_napi_enable(adapter);

	if (adapter->max_sds_rings > 1)
		netxen_config_rss(adapter, 1);

	if (NX_IS_REVISION_P3(adapter->ahw.revision_id))
		netxen_linkevent_request(adapter, 1);

	return 0;
}

static void
netxen_nic_down(struct netxen_adapter *adapter, struct net_device *netdev)
{
	netif_carrier_off(netdev);
	netif_stop_queue(netdev);
	netxen_napi_disable(adapter);

	if (adapter->stop_port)
		adapter->stop_port(adapter);

	netxen_release_tx_buffers(adapter);

	FLUSH_SCHEDULED_WORK();
	del_timer_sync(&adapter->watchdog_timer);
}


static int
netxen_nic_attach(struct netxen_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;
	struct pci_dev *pdev = adapter->pdev;
	int err, ring;
	struct nx_host_rds_ring *rds_ring;
	struct nx_host_tx_ring *tx_ring;

	err = netxen_init_firmware(adapter);
	if (err != 0) {
		printk(KERN_ERR "Failed to init firmware\n");
		return -EIO;
	}

	if (adapter->fw_major < 4)
		adapter->max_rds_rings = 3;
	else
		adapter->max_rds_rings = 2;

	err = netxen_alloc_sw_resources(adapter);
	if (err) {
		printk(KERN_ERR "%s: Error in setting sw resources\n",
				netdev->name);
		return err;
	}

	netxen_nic_clear_stats(adapter);

	err = netxen_alloc_hw_resources(adapter);
	if (err) {
		printk(KERN_ERR "%s: Error in setting hw resources\n",
				netdev->name);
		goto err_out_free_sw;
	}

	if (adapter->fw_major < 4) {
		tx_ring = &adapter->tx_ring;
		tx_ring->crb_cmd_producer = crb_cmd_producer[adapter->portnum];
		tx_ring->crb_cmd_consumer = crb_cmd_consumer[adapter->portnum];

		netxen_nic_update_cmd_producer(adapter, tx_ring, 0);
		netxen_nic_update_cmd_consumer(adapter, tx_ring, 0);
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
	netxen_nic_free_irq(adapter);

	netxen_release_rx_buffers(adapter);
	netxen_free_hw_resources(adapter);
	netxen_free_sw_resources(adapter);

	adapter->is_up = 0;
}

static int __devinit
netxen_nic_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct net_device *netdev = NULL;
	struct netxen_adapter *adapter = NULL;
	int i = 0, err;
	int pci_func_id = PCI_FUNC(pdev->devfn);
	uint8_t revision_id;

	if (pdev->class != 0x020000) {
		printk(KERN_DEBUG "NetXen function %d, class %x will not "
				"be enabled.\n",pci_func_id, pdev->class);
		return -ENODEV;
	}

	if (pdev->revision >= NX_P3_A0 && pdev->revision < NX_P3_B1) {
		printk(KERN_WARNING "NetXen chip revisions between 0x%x-0x%x"
				"will not be enabled.\n",
				NX_P3_A0, NX_P3_B1);
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

	pci_set_master(pdev);

	netdev = alloc_etherdev(sizeof(struct netxen_adapter));
	if(!netdev) {
		printk(KERN_ERR"%s: Failed to allocate memory for the "
				"device block.Check system memory resource"
				" usage.\n", netxen_nic_driver_name);
		goto err_out_free_res;
	}

	SET_NETDEV_DEV(netdev, &pdev->dev);

	adapter = netdev_priv(netdev);
	adapter->netdev  = netdev;
	adapter->pdev    = pdev;
	adapter->ahw.pci_func  = pci_func_id;

	revision_id = pdev->revision;
	adapter->ahw.revision_id = revision_id;

	err = nx_set_dma_mask(adapter, revision_id);
	if (err)
		goto err_out_free_netdev;

	rwlock_init(&adapter->adapter_lock);
	spin_lock_init(&adapter->tx_clean_lock);

	err = netxen_setup_pci_map(adapter);
	if (err)
		goto err_out_free_netdev;

	/* This will be reset for mezz cards  */
	adapter->portnum = pci_func_id;
	adapter->rx_csum = 1;
	adapter->mc_enabled = 0;
	if (NX_IS_REVISION_P3(revision_id))
		adapter->max_mc_count = 38;
	else
		adapter->max_mc_count = 16;

	netdev->netdev_ops	   = &netxen_netdev_ops;
	netdev->watchdog_timeo     = 2*HZ;

	netxen_nic_change_mtu(netdev, netdev->mtu);

	SET_ETHTOOL_OPS(netdev, &netxen_nic_ethtool_ops);

	netdev->features |= (NETIF_F_SG | NETIF_F_IP_CSUM | NETIF_F_TSO);
	netdev->vlan_features |= (NETIF_F_SG | NETIF_F_IP_CSUM | NETIF_F_TSO);

	if (NX_IS_REVISION_P3(revision_id)) {
		netdev->features |= (NETIF_F_IPV6_CSUM | NETIF_F_TSO6);
		netdev->vlan_features |= (NETIF_F_IPV6_CSUM | NETIF_F_TSO6);
	}

	if (adapter->pci_using_dac) {
		netdev->features |= NETIF_F_HIGHDMA;
		netdev->vlan_features |= NETIF_F_HIGHDMA;
	}

	if (netxen_nic_get_board_info(adapter) != 0) {
		printk("%s: Error getting board config info.\n",
				netxen_nic_driver_name);
		err = -EIO;
		goto err_out_iounmap;
	}

	netxen_initialize_adapter_ops(adapter);

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

	err = netxen_start_firmware(adapter);
	if (err)
		goto err_out_iounmap;

	nx_update_dma_mask(adapter);

	netxen_nic_get_firmware_info(adapter);

	/*
	 * See if the firmware gave us a virtual-physical port mapping.
	 */
	adapter->physical_port = adapter->portnum;
	if (adapter->fw_major < 4) {
		i = adapter->pci_read_normalize(adapter,
				CRB_V2P(adapter->portnum));
		if (i != 0x55555555)
			adapter->physical_port = i;
	}

	netxen_check_options(adapter);

	netxen_setup_intr(adapter);

	netdev->irq = adapter->msix_entries[0].vector;

	netxen_napi_add(adapter, netdev);

	init_timer(&adapter->watchdog_timer);
	adapter->watchdog_timer.function = &netxen_watchdog;
	adapter->watchdog_timer.data = (unsigned long)adapter;
	INIT_WORK(&adapter->watchdog_task, netxen_watchdog_task);
	INIT_WORK(&adapter->tx_timeout_task, netxen_tx_timeout_task);

	err = netxen_read_mac_addr(adapter);
	if (err)
		dev_warn(&pdev->dev, "failed to read mac addr\n");

	netif_carrier_off(netdev);
	netif_stop_queue(netdev);

	if ((err = register_netdev(netdev))) {
		printk(KERN_ERR "%s: register_netdev failed port #%d"
			       " aborting\n", netxen_nic_driver_name,
			       adapter->portnum);
		err = -EIO;
		goto err_out_disable_msi;
	}

	pci_set_drvdata(pdev, adapter);

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

	return 0;

err_out_disable_msi:
	netxen_teardown_intr(adapter);

	netxen_free_adapter_offload(adapter);

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

	unregister_netdev(netdev);

	if (adapter->is_up == NETXEN_ADAPTER_UP_MAGIC) {
		netxen_nic_detach(adapter);

		if (NX_IS_REVISION_P3(adapter->ahw.revision_id))
			netxen_p3_free_mac_list(adapter);
	}

	if (adapter->portnum == 0)
		netxen_free_adapter_offload(adapter);

	netxen_teardown_intr(adapter);

	netxen_cleanup_pci_map(adapter);

	pci_release_regions(pdev);
	pci_disable_device(pdev);
	pci_set_drvdata(pdev, NULL);

	free_netdev(netdev);
}

static int
netxen_nic_suspend(struct pci_dev *pdev, pm_message_t state)
{

	struct netxen_adapter *adapter = pci_get_drvdata(pdev);
	struct net_device *netdev = adapter->netdev;

	netif_device_detach(netdev);

	if (netif_running(netdev))
		netxen_nic_down(adapter, netdev);

	if (adapter->is_up == NETXEN_ADAPTER_UP_MAGIC)
		netxen_nic_detach(adapter);

	pci_save_state(pdev);

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
	struct netxen_adapter *adapter = pci_get_drvdata(pdev);
	struct net_device *netdev = adapter->netdev;
	int err;

	pci_set_power_state(pdev, PCI_D0);
	pci_restore_state(pdev);

	err = pci_enable_device(pdev);
	if (err)
		return err;

	adapter->curr_window = 255;

	err = netxen_start_firmware(adapter);
	if (err) {
		dev_err(&pdev->dev, "failed to start firmware\n");
		return err;
	}

	if (netif_running(netdev)) {
		err = netxen_nic_attach(adapter);
		if (err)
			return err;

		err = netxen_nic_up(adapter, netdev);
		if (err)
			return err;

		netif_device_attach(netdev);
	}

	return 0;
}

static int netxen_nic_open(struct net_device *netdev)
{
	struct netxen_adapter *adapter = netdev_priv(netdev);
	int err = 0;

	if (adapter->driver_mismatch)
		return -EIO;

	if (adapter->is_up != NETXEN_ADAPTER_UP_MAGIC) {
		err = netxen_nic_attach(adapter);
		if (err)
			return err;
	}

	err = netxen_nic_up(adapter, netdev);
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

	netxen_nic_down(adapter, netdev);
	return 0;
}

static bool netxen_tso_check(struct net_device *netdev,
		      struct cmd_desc_type0 *desc, struct sk_buff *skb)
{
	bool tso = false;
	u8 opcode = TX_ETHER_PKT;
	__be16 protocol = skb->protocol;
	u16 flags = 0;

	if (protocol == cpu_to_be16(ETH_P_8021Q)) {
		struct vlan_ethhdr *vh = (struct vlan_ethhdr *)skb->data;
		protocol = vh->h_vlan_encapsulated_proto;
		flags = FLAGS_VLAN_TAGGED;
	}

	if ((netdev->features & (NETIF_F_TSO | NETIF_F_TSO6)) &&
			skb_shinfo(skb)->gso_size > 0) {

		desc->mss = cpu_to_le16(skb_shinfo(skb)->gso_size);
		desc->total_hdr_length =
			skb_transport_offset(skb) + tcp_hdrlen(skb);

		opcode = (protocol == cpu_to_be16(ETH_P_IPV6)) ?
				TX_TCP_LSO6 : TX_TCP_LSO;
		tso = true;

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
	desc->tcp_hdr_offset = skb_transport_offset(skb);
	desc->ip_hdr_offset = skb_network_offset(skb);
	netxen_set_tx_flags_opcode(desc, flags, opcode);
	return tso;
}

static void
netxen_clean_tx_dma_mapping(struct pci_dev *pdev,
		struct netxen_cmd_buffer *pbuf, int last)
{
	int k;
	struct netxen_skb_frag *buffrag;

	buffrag = &pbuf->frag_array[0];
	pci_unmap_single(pdev, buffrag->dma,
			buffrag->length, PCI_DMA_TODEVICE);

	for (k = 1; k < last; k++) {
		buffrag = &pbuf->frag_array[k];
		pci_unmap_page(pdev, buffrag->dma,
			buffrag->length, PCI_DMA_TODEVICE);
	}
}

static inline void
netxen_clear_cmddesc(u64 *desc)
{
	int i;
	for (i = 0; i < 8; i++)
		desc[i] = 0ULL;
}

static int
netxen_nic_xmit_frame(struct sk_buff *skb, struct net_device *netdev)
{
	struct netxen_adapter *adapter = netdev_priv(netdev);
	struct nx_host_tx_ring *tx_ring = &adapter->tx_ring;
	unsigned int first_seg_len = skb->len - skb->data_len;
	struct netxen_cmd_buffer *pbuf;
	struct netxen_skb_frag *buffrag;
	struct cmd_desc_type0 *hwdesc;
	struct pci_dev *pdev = adapter->pdev;
	dma_addr_t temp_dma;
	int i, k;

	u32 producer, consumer;
	int frag_count, no_of_desc;
	u32 num_txd = tx_ring->num_desc;
	bool is_tso = false;

	frag_count = skb_shinfo(skb)->nr_frags + 1;

	/* 4 fragments per cmd des */
	no_of_desc = (frag_count + 3) >> 2;

	producer = tx_ring->producer;
	smp_mb();
	consumer = tx_ring->sw_consumer;
	if ((no_of_desc+2) > find_diff_among(producer, consumer, num_txd)) {
		netif_stop_queue(netdev);
		smp_mb();
		return NETDEV_TX_BUSY;
	}

	hwdesc = &tx_ring->desc_head[producer];
	netxen_clear_cmddesc((u64 *)hwdesc);
	pbuf = &tx_ring->cmd_buf_arr[producer];

	is_tso = netxen_tso_check(netdev, hwdesc, skb);

	pbuf->skb = skb;
	pbuf->frag_count = frag_count;
	buffrag = &pbuf->frag_array[0];
	temp_dma = pci_map_single(pdev, skb->data, first_seg_len,
				      PCI_DMA_TODEVICE);
	if (pci_dma_mapping_error(pdev, temp_dma))
		goto drop_packet;

	buffrag->dma = temp_dma;
	buffrag->length = first_seg_len;
	netxen_set_tx_frags_len(hwdesc, frag_count, skb->len);
	netxen_set_tx_port(hwdesc, adapter->portnum);

	hwdesc->buffer_length[0] = cpu_to_le16(first_seg_len);
	hwdesc->addr_buffer1 = cpu_to_le64(buffrag->dma);

	for (i = 1, k = 1; i < frag_count; i++, k++) {
		struct skb_frag_struct *frag;
		int len, temp_len;
		unsigned long offset;

		/* move to next desc. if there is a need */
		if ((i & 0x3) == 0) {
			k = 0;
			producer = get_next_index(producer, num_txd);
			hwdesc = &tx_ring->desc_head[producer];
			netxen_clear_cmddesc((u64 *)hwdesc);
			pbuf = &tx_ring->cmd_buf_arr[producer];
			pbuf->skb = NULL;
		}
		frag = &skb_shinfo(skb)->frags[i - 1];
		len = frag->size;
		offset = frag->page_offset;

		temp_len = len;
		temp_dma = pci_map_page(pdev, frag->page, offset,
					len, PCI_DMA_TODEVICE);
		if (pci_dma_mapping_error(pdev, temp_dma)) {
			netxen_clean_tx_dma_mapping(pdev, pbuf, i);
			goto drop_packet;
		}

		buffrag++;
		buffrag->dma = temp_dma;
		buffrag->length = temp_len;

		hwdesc->buffer_length[k] = cpu_to_le16(temp_len);
		switch (k) {
		case 0:
			hwdesc->addr_buffer1 = cpu_to_le64(temp_dma);
			break;
		case 1:
			hwdesc->addr_buffer2 = cpu_to_le64(temp_dma);
			break;
		case 2:
			hwdesc->addr_buffer3 = cpu_to_le64(temp_dma);
			break;
		case 3:
			hwdesc->addr_buffer4 = cpu_to_le64(temp_dma);
			break;
		}
		frag++;
	}
	producer = get_next_index(producer, num_txd);

	/* For LSO, we need to copy the MAC/IP/TCP headers into
	 * the descriptor ring
	 */
	if (is_tso) {
		int hdr_len, first_hdr_len, more_hdr;
		hdr_len = skb_transport_offset(skb) + tcp_hdrlen(skb);
		if (hdr_len > (sizeof(struct cmd_desc_type0) - 2)) {
			first_hdr_len = sizeof(struct cmd_desc_type0) - 2;
			more_hdr = 1;
		} else {
			first_hdr_len = hdr_len;
			more_hdr = 0;
		}
		/* copy the MAC/IP/TCP headers to the cmd descriptor list */
		hwdesc = &tx_ring->desc_head[producer];
		pbuf = &tx_ring->cmd_buf_arr[producer];
		pbuf->skb = NULL;

		/* copy the first 64 bytes */
		memcpy(((void *)hwdesc) + 2,
		       (void *)(skb->data), first_hdr_len);
		producer = get_next_index(producer, num_txd);

		if (more_hdr) {
			hwdesc = &tx_ring->desc_head[producer];
			pbuf = &tx_ring->cmd_buf_arr[producer];
			pbuf->skb = NULL;
			/* copy the next 64 bytes - should be enough except
			 * for pathological case
			 */
			skb_copy_from_linear_data_offset(skb, first_hdr_len,
							 hwdesc,
							 (hdr_len -
							  first_hdr_len));
			producer = get_next_index(producer, num_txd);
		}
	}

	tx_ring->producer = producer;
	adapter->stats.txbytes += skb->len;

	netxen_nic_update_cmd_producer(adapter, tx_ring, producer);

	adapter->stats.xmitcalled++;
	netdev->trans_start = jiffies;

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

	temp = adapter->pci_read_normalize(adapter, CRB_TEMP_STATE);

	temp_state = nx_get_temp_state(temp);
	temp_val = nx_get_temp_val(temp);

	if (temp_state == NX_TEMP_PANIC) {
		printk(KERN_ALERT
		       "%s: Device temperature %d degrees C exceeds"
		       " maximum allowed. Hardware has been shut down.\n",
		       netxen_nic_driver_name, temp_val);

		netif_carrier_off(netdev);
		netif_stop_queue(netdev);
		rv = 1;
	} else if (temp_state == NX_TEMP_WARN) {
		if (adapter->temp == NX_TEMP_NORMAL) {
			printk(KERN_ALERT
			       "%s: Device temperature %d degrees C "
			       "exceeds operating range."
			       " Immediate action needed.\n",
			       netxen_nic_driver_name, temp_val);
		}
	} else {
		if (adapter->temp == NX_TEMP_WARN) {
			printk(KERN_INFO
			       "%s: Device temperature is now %d degrees C"
			       " in normal range.\n", netxen_nic_driver_name,
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

		if (!adapter->has_link_events)
			netxen_nic_set_link_parameters(adapter);

	} else if (!adapter->ahw.linkup && linkup) {
		printk(KERN_INFO "%s: %s NIC Link is up\n",
		       netxen_nic_driver_name, netdev->name);
		adapter->ahw.linkup = 1;
		if (netif_running(netdev)) {
			netif_carrier_on(netdev);
			netif_wake_queue(netdev);
		}

		if (!adapter->has_link_events)
			netxen_nic_set_link_parameters(adapter);
	}
}

static void netxen_nic_handle_phy_intr(struct netxen_adapter *adapter)
{
	u32 val, port, linkup;

	port = adapter->physical_port;

	if (NX_IS_REVISION_P3(adapter->ahw.revision_id)) {
		val = adapter->pci_read_normalize(adapter, CRB_XG_STATE_P3);
		val = XG_LINK_STATE_P3(adapter->ahw.pci_func, val);
		linkup = (val == XG_LINK_UP_P3);
	} else {
		val = adapter->pci_read_normalize(adapter, CRB_XG_STATE);
		if (adapter->ahw.port_type == NETXEN_NIC_GBE)
			linkup = (val >> port) & 1;
		else {
			val = (val >> port*8) & 0xff;
			linkup = (val == XG_LINK_UP);
		}
	}

	netxen_advert_link_change(adapter, linkup);
}

static void netxen_watchdog(unsigned long v)
{
	struct netxen_adapter *adapter = (struct netxen_adapter *)v;

	SCHEDULE_WORK(&adapter->watchdog_task);
}

void netxen_watchdog_task(struct work_struct *work)
{
	struct netxen_adapter *adapter =
		container_of(work, struct netxen_adapter, watchdog_task);

	if ((adapter->portnum  == 0) && netxen_nic_check_temp(adapter))
		return;

	if (!adapter->has_link_events)
		netxen_nic_handle_phy_intr(adapter);

	if (netif_running(adapter->netdev))
		mod_timer(&adapter->watchdog_timer, jiffies + 2 * HZ);
}

static void netxen_tx_timeout(struct net_device *netdev)
{
	struct netxen_adapter *adapter = (struct netxen_adapter *)
						netdev_priv(netdev);
	SCHEDULE_WORK(&adapter->tx_timeout_task);
}

static void netxen_tx_timeout_task(struct work_struct *work)
{
	struct netxen_adapter *adapter =
		container_of(work, struct netxen_adapter, tx_timeout_task);

	printk(KERN_ERR "%s %s: transmit timeout, resetting.\n",
	       netxen_nic_driver_name, adapter->netdev->name);

	netxen_napi_disable(adapter);

	adapter->netdev->trans_start = jiffies;

	netxen_napi_enable(adapter);
	netif_wake_queue(adapter->netdev);
}

struct net_device_stats *netxen_nic_get_stats(struct net_device *netdev)
{
	struct netxen_adapter *adapter = netdev_priv(netdev);
	struct net_device_stats *stats = &adapter->net_stats;

	memset(stats, 0, sizeof(*stats));

	stats->rx_packets = adapter->stats.no_rcv;
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

	status = adapter->pci_read_immediate(adapter, ISR_INT_VECTOR);

	if (!(status & adapter->legacy_intr.int_vec_bit))
		return IRQ_NONE;

	if (adapter->ahw.revision_id >= NX_P3_B1) {
		/* check interrupt state machine, to be sure */
		status = adapter->pci_read_immediate(adapter,
				ISR_INT_STATE_REG);
		if (!ISR_LEGACY_INT_TRIGGERED(status))
			return IRQ_NONE;

	} else {
		unsigned long our_int = 0;

		our_int = adapter->pci_read_normalize(adapter, CRB_INT_VECTOR);

		/* not our interrupt */
		if (!test_and_clear_bit((7 + adapter->portnum), &our_int))
			return IRQ_NONE;

		/* claim interrupt */
		adapter->pci_write_normalize(adapter,
				CRB_INT_VECTOR, (our_int & 0xffffffff));
	}

	/* clear interrupt */
	if (adapter->fw_major < 4)
		netxen_nic_disable_int(sds_ring);

	adapter->pci_write_immediate(adapter,
			adapter->legacy_intr.tgt_status_reg,
			0xffffffff);
	/* read twice to ensure write is flushed */
	adapter->pci_read_immediate(adapter, ISR_INT_VECTOR);
	adapter->pci_read_immediate(adapter, ISR_INT_VECTOR);

	napi_schedule(&sds_ring->napi);

	return IRQ_HANDLED;
}

static irqreturn_t netxen_msi_intr(int irq, void *data)
{
	struct nx_host_sds_ring *sds_ring = data;
	struct netxen_adapter *adapter = sds_ring->adapter;

	/* clear interrupt */
	adapter->pci_write_immediate(adapter,
			msi_tgt_status[adapter->ahw.pci_func], 0xffffffff);

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
		netxen_nic_enable_int(sds_ring);
	}

	return work_done;
}

#ifdef CONFIG_NET_POLL_CONTROLLER
static void netxen_nic_poll_controller(struct net_device *netdev)
{
	struct netxen_adapter *adapter = netdev_priv(netdev);
	disable_irq(adapter->irq);
	netxen_intr(adapter->irq, adapter);
	enable_irq(adapter->irq);
}
#endif

static struct pci_driver netxen_driver = {
	.name = netxen_nic_driver_name,
	.id_table = netxen_pci_tbl,
	.probe = netxen_nic_probe,
	.remove = __devexit_p(netxen_nic_remove),
	.suspend = netxen_nic_suspend,
	.resume = netxen_nic_resume
};

/* Driver Registration on NetXen card    */

static int __init netxen_init_module(void)
{
	printk(KERN_INFO "%s\n", netxen_nic_driver_string);

	if ((netxen_workq = create_singlethread_workqueue("netxen")) == NULL)
		return -ENOMEM;

	return pci_register_driver(&netxen_driver);
}

module_init(netxen_init_module);

static void __exit netxen_exit_module(void)
{
	pci_unregister_driver(&netxen_driver);
	destroy_workqueue(netxen_workq);
}

module_exit(netxen_exit_module);
