/*
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

#include "qlcnic.h"

#include <linux/dma-mapping.h>
#include <linux/if_vlan.h>
#include <net/ip.h>
#include <linux/ipv6.h>
#include <linux/inetdevice.h>
#include <linux/sysfs.h>

MODULE_DESCRIPTION("QLogic 10 GbE Converged Ethernet Driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(QLCNIC_LINUX_VERSIONID);
MODULE_FIRMWARE(QLCNIC_UNIFIED_ROMIMAGE_NAME);

char qlcnic_driver_name[] = "qlcnic";
static const char qlcnic_driver_string[] = "QLogic Converged Ethernet Driver v"
    QLCNIC_LINUX_VERSIONID;

static int port_mode = QLCNIC_PORT_MODE_AUTO_NEG;

/* Default to restricted 1G auto-neg mode */
static int wol_port_mode = 5;

static int use_msi = 1;
module_param(use_msi, int, 0644);
MODULE_PARM_DESC(use_msi, "MSI interrupt (0=disabled, 1=enabled");

static int use_msi_x = 1;
module_param(use_msi_x, int, 0644);
MODULE_PARM_DESC(use_msi_x, "MSI-X interrupt (0=disabled, 1=enabled");

static int auto_fw_reset = AUTO_FW_RESET_ENABLED;
module_param(auto_fw_reset, int, 0644);
MODULE_PARM_DESC(auto_fw_reset, "Auto firmware reset (0=disabled, 1=enabled");

static int __devinit qlcnic_probe(struct pci_dev *pdev,
		const struct pci_device_id *ent);
static void __devexit qlcnic_remove(struct pci_dev *pdev);
static int qlcnic_open(struct net_device *netdev);
static int qlcnic_close(struct net_device *netdev);
static void qlcnic_tx_timeout(struct net_device *netdev);
static void qlcnic_tx_timeout_task(struct work_struct *work);
static void qlcnic_attach_work(struct work_struct *work);
static void qlcnic_fwinit_work(struct work_struct *work);
static void qlcnic_fw_poll_work(struct work_struct *work);
static void qlcnic_schedule_work(struct qlcnic_adapter *adapter,
		work_func_t func, int delay);
static void qlcnic_cancel_fw_work(struct qlcnic_adapter *adapter);
static int qlcnic_poll(struct napi_struct *napi, int budget);
#ifdef CONFIG_NET_POLL_CONTROLLER
static void qlcnic_poll_controller(struct net_device *netdev);
#endif

static void qlcnic_create_sysfs_entries(struct qlcnic_adapter *adapter);
static void qlcnic_remove_sysfs_entries(struct qlcnic_adapter *adapter);
static void qlcnic_create_diag_entries(struct qlcnic_adapter *adapter);
static void qlcnic_remove_diag_entries(struct qlcnic_adapter *adapter);

static void qlcnic_clr_all_drv_state(struct qlcnic_adapter *adapter);
static int qlcnic_can_start_firmware(struct qlcnic_adapter *adapter);

static irqreturn_t qlcnic_tmp_intr(int irq, void *data);
static irqreturn_t qlcnic_intr(int irq, void *data);
static irqreturn_t qlcnic_msi_intr(int irq, void *data);
static irqreturn_t qlcnic_msix_intr(int irq, void *data);

static struct net_device_stats *qlcnic_get_stats(struct net_device *netdev);
static void qlcnic_config_indev_addr(struct net_device *dev, unsigned long);

/*  PCI Device ID Table  */
#define ENTRY(device) \
	{PCI_DEVICE(PCI_VENDOR_ID_QLOGIC, (device)), \
	.class = PCI_CLASS_NETWORK_ETHERNET << 8, .class_mask = ~0}

#define PCI_DEVICE_ID_QLOGIC_QLE824X  0x8020

static DEFINE_PCI_DEVICE_TABLE(qlcnic_pci_tbl) = {
	ENTRY(PCI_DEVICE_ID_QLOGIC_QLE824X),
	{0,}
};

MODULE_DEVICE_TABLE(pci, qlcnic_pci_tbl);


void
qlcnic_update_cmd_producer(struct qlcnic_adapter *adapter,
		struct qlcnic_host_tx_ring *tx_ring)
{
	writel(tx_ring->producer, tx_ring->crb_cmd_producer);

	if (qlcnic_tx_avail(tx_ring) <= TX_STOP_THRESH) {
		netif_stop_queue(adapter->netdev);
		smp_mb();
		adapter->stats.xmit_off++;
	}
}

static const u32 msi_tgt_status[8] = {
	ISR_INT_TARGET_STATUS, ISR_INT_TARGET_STATUS_F1,
	ISR_INT_TARGET_STATUS_F2, ISR_INT_TARGET_STATUS_F3,
	ISR_INT_TARGET_STATUS_F4, ISR_INT_TARGET_STATUS_F5,
	ISR_INT_TARGET_STATUS_F6, ISR_INT_TARGET_STATUS_F7
};

static const
struct qlcnic_legacy_intr_set legacy_intr[] = QLCNIC_LEGACY_INTR_CONFIG;

static inline void qlcnic_disable_int(struct qlcnic_host_sds_ring *sds_ring)
{
	writel(0, sds_ring->crb_intr_mask);
}

static inline void qlcnic_enable_int(struct qlcnic_host_sds_ring *sds_ring)
{
	struct qlcnic_adapter *adapter = sds_ring->adapter;

	writel(0x1, sds_ring->crb_intr_mask);

	if (!QLCNIC_IS_MSI_FAMILY(adapter))
		writel(0xfbff, adapter->tgt_mask_reg);
}

static int
qlcnic_alloc_sds_rings(struct qlcnic_recv_context *recv_ctx, int count)
{
	int size = sizeof(struct qlcnic_host_sds_ring) * count;

	recv_ctx->sds_rings = kzalloc(size, GFP_KERNEL);

	return (recv_ctx->sds_rings == NULL);
}

static void
qlcnic_free_sds_rings(struct qlcnic_recv_context *recv_ctx)
{
	if (recv_ctx->sds_rings != NULL)
		kfree(recv_ctx->sds_rings);

	recv_ctx->sds_rings = NULL;
}

static int
qlcnic_napi_add(struct qlcnic_adapter *adapter, struct net_device *netdev)
{
	int ring;
	struct qlcnic_host_sds_ring *sds_ring;
	struct qlcnic_recv_context *recv_ctx = &adapter->recv_ctx;

	if (qlcnic_alloc_sds_rings(recv_ctx, adapter->max_sds_rings))
		return -ENOMEM;

	for (ring = 0; ring < adapter->max_sds_rings; ring++) {
		sds_ring = &recv_ctx->sds_rings[ring];
		netif_napi_add(netdev, &sds_ring->napi,
				qlcnic_poll, QLCNIC_NETDEV_WEIGHT);
	}

	return 0;
}

static void
qlcnic_napi_del(struct qlcnic_adapter *adapter)
{
	int ring;
	struct qlcnic_host_sds_ring *sds_ring;
	struct qlcnic_recv_context *recv_ctx = &adapter->recv_ctx;

	for (ring = 0; ring < adapter->max_sds_rings; ring++) {
		sds_ring = &recv_ctx->sds_rings[ring];
		netif_napi_del(&sds_ring->napi);
	}

	qlcnic_free_sds_rings(&adapter->recv_ctx);
}

static void
qlcnic_napi_enable(struct qlcnic_adapter *adapter)
{
	int ring;
	struct qlcnic_host_sds_ring *sds_ring;
	struct qlcnic_recv_context *recv_ctx = &adapter->recv_ctx;

	for (ring = 0; ring < adapter->max_sds_rings; ring++) {
		sds_ring = &recv_ctx->sds_rings[ring];
		napi_enable(&sds_ring->napi);
		qlcnic_enable_int(sds_ring);
	}
}

static void
qlcnic_napi_disable(struct qlcnic_adapter *adapter)
{
	int ring;
	struct qlcnic_host_sds_ring *sds_ring;
	struct qlcnic_recv_context *recv_ctx = &adapter->recv_ctx;

	for (ring = 0; ring < adapter->max_sds_rings; ring++) {
		sds_ring = &recv_ctx->sds_rings[ring];
		qlcnic_disable_int(sds_ring);
		napi_synchronize(&sds_ring->napi);
		napi_disable(&sds_ring->napi);
	}
}

static void qlcnic_clear_stats(struct qlcnic_adapter *adapter)
{
	memset(&adapter->stats, 0, sizeof(adapter->stats));
	return;
}

static int qlcnic_set_dma_mask(struct qlcnic_adapter *adapter)
{
	struct pci_dev *pdev = adapter->pdev;
	u64 mask, cmask;

	adapter->pci_using_dac = 0;

	mask = DMA_BIT_MASK(39);
	cmask = mask;

	if (pci_set_dma_mask(pdev, mask) == 0 &&
			pci_set_consistent_dma_mask(pdev, cmask) == 0) {
		adapter->pci_using_dac = 1;
		return 0;
	}

	return -EIO;
}

/* Update addressable range if firmware supports it */
static int
qlcnic_update_dma_mask(struct qlcnic_adapter *adapter)
{
	int change, shift, err;
	u64 mask, old_mask, old_cmask;
	struct pci_dev *pdev = adapter->pdev;

	change = 0;

	shift = QLCRD32(adapter, CRB_DMA_SHIFT);
	if (shift > 32)
		return 0;

	if (shift > 9)
		change = 1;

	if (change) {
		old_mask = pdev->dma_mask;
		old_cmask = pdev->dev.coherent_dma_mask;

		mask = DMA_BIT_MASK(32+shift);

		err = pci_set_dma_mask(pdev, mask);
		if (err)
			goto err_out;

		err = pci_set_consistent_dma_mask(pdev, mask);
		if (err)
			goto err_out;
		dev_info(&pdev->dev, "using %d-bit dma mask\n", 32+shift);
	}

	return 0;

err_out:
	pci_set_dma_mask(pdev, old_mask);
	pci_set_consistent_dma_mask(pdev, old_cmask);
	return err;
}

static void qlcnic_set_port_mode(struct qlcnic_adapter *adapter)
{
	u32 val, data;

	val = adapter->ahw.board_type;
	if ((val == QLCNIC_BRDTYPE_P3_HMEZ) ||
		(val == QLCNIC_BRDTYPE_P3_XG_LOM)) {
		if (port_mode == QLCNIC_PORT_MODE_802_3_AP) {
			data = QLCNIC_PORT_MODE_802_3_AP;
			QLCWR32(adapter, QLCNIC_PORT_MODE_ADDR, data);
		} else if (port_mode == QLCNIC_PORT_MODE_XG) {
			data = QLCNIC_PORT_MODE_XG;
			QLCWR32(adapter, QLCNIC_PORT_MODE_ADDR, data);
		} else if (port_mode == QLCNIC_PORT_MODE_AUTO_NEG_1G) {
			data = QLCNIC_PORT_MODE_AUTO_NEG_1G;
			QLCWR32(adapter, QLCNIC_PORT_MODE_ADDR, data);
		} else if (port_mode == QLCNIC_PORT_MODE_AUTO_NEG_XG) {
			data = QLCNIC_PORT_MODE_AUTO_NEG_XG;
			QLCWR32(adapter, QLCNIC_PORT_MODE_ADDR, data);
		} else {
			data = QLCNIC_PORT_MODE_AUTO_NEG;
			QLCWR32(adapter, QLCNIC_PORT_MODE_ADDR, data);
		}

		if ((wol_port_mode != QLCNIC_PORT_MODE_802_3_AP) &&
			(wol_port_mode != QLCNIC_PORT_MODE_XG) &&
			(wol_port_mode != QLCNIC_PORT_MODE_AUTO_NEG_1G) &&
			(wol_port_mode != QLCNIC_PORT_MODE_AUTO_NEG_XG)) {
			wol_port_mode = QLCNIC_PORT_MODE_AUTO_NEG;
		}
		QLCWR32(adapter, QLCNIC_WOL_PORT_MODE, wol_port_mode);
	}
}

static void qlcnic_set_msix_bit(struct pci_dev *pdev, int enable)
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

static void qlcnic_init_msix_entries(struct qlcnic_adapter *adapter, int count)
{
	int i;

	for (i = 0; i < count; i++)
		adapter->msix_entries[i].entry = i;
}

static int
qlcnic_read_mac_addr(struct qlcnic_adapter *adapter)
{
	int i;
	unsigned char *p;
	u64 mac_addr;
	struct net_device *netdev = adapter->netdev;
	struct pci_dev *pdev = adapter->pdev;

	if (qlcnic_get_mac_addr(adapter, &mac_addr) != 0)
		return -EIO;

	p = (unsigned char *)&mac_addr;
	for (i = 0; i < 6; i++)
		netdev->dev_addr[i] = *(p + 5 - i);

	memcpy(netdev->perm_addr, netdev->dev_addr, netdev->addr_len);
	memcpy(adapter->mac_addr, netdev->dev_addr, netdev->addr_len);

	/* set station address */

	if (!is_valid_ether_addr(netdev->perm_addr))
		dev_warn(&pdev->dev, "Bad MAC address %pM.\n",
					netdev->dev_addr);

	return 0;
}

static int qlcnic_set_mac(struct net_device *netdev, void *p)
{
	struct qlcnic_adapter *adapter = netdev_priv(netdev);
	struct sockaddr *addr = p;

	if (!is_valid_ether_addr(addr->sa_data))
		return -EINVAL;

	if (netif_running(netdev)) {
		netif_device_detach(netdev);
		qlcnic_napi_disable(adapter);
	}

	memcpy(adapter->mac_addr, addr->sa_data, netdev->addr_len);
	memcpy(netdev->dev_addr, addr->sa_data, netdev->addr_len);
	qlcnic_set_multi(adapter->netdev);

	if (netif_running(netdev)) {
		netif_device_attach(netdev);
		qlcnic_napi_enable(adapter);
	}
	return 0;
}

static const struct net_device_ops qlcnic_netdev_ops = {
	.ndo_open	   = qlcnic_open,
	.ndo_stop	   = qlcnic_close,
	.ndo_start_xmit    = qlcnic_xmit_frame,
	.ndo_get_stats	   = qlcnic_get_stats,
	.ndo_validate_addr = eth_validate_addr,
	.ndo_set_multicast_list = qlcnic_set_multi,
	.ndo_set_mac_address    = qlcnic_set_mac,
	.ndo_change_mtu	   = qlcnic_change_mtu,
	.ndo_tx_timeout	   = qlcnic_tx_timeout,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller = qlcnic_poll_controller,
#endif
};

static void
qlcnic_setup_intr(struct qlcnic_adapter *adapter)
{
	const struct qlcnic_legacy_intr_set *legacy_intrp;
	struct pci_dev *pdev = adapter->pdev;
	int err, num_msix;

	if (adapter->rss_supported) {
		num_msix = (num_online_cpus() >= MSIX_ENTRIES_PER_ADAPTER) ?
			MSIX_ENTRIES_PER_ADAPTER : 2;
	} else
		num_msix = 1;

	adapter->max_sds_rings = 1;

	adapter->flags &= ~(QLCNIC_MSI_ENABLED | QLCNIC_MSIX_ENABLED);

	legacy_intrp = &legacy_intr[adapter->ahw.pci_func];

	adapter->int_vec_bit = legacy_intrp->int_vec_bit;
	adapter->tgt_status_reg = qlcnic_get_ioaddr(adapter,
			legacy_intrp->tgt_status_reg);
	adapter->tgt_mask_reg = qlcnic_get_ioaddr(adapter,
			legacy_intrp->tgt_mask_reg);
	adapter->isr_int_vec = qlcnic_get_ioaddr(adapter, ISR_INT_VECTOR);

	adapter->crb_int_state_reg = qlcnic_get_ioaddr(adapter,
			ISR_INT_STATE_REG);

	qlcnic_set_msix_bit(pdev, 0);

	if (adapter->msix_supported) {

		qlcnic_init_msix_entries(adapter, num_msix);
		err = pci_enable_msix(pdev, adapter->msix_entries, num_msix);
		if (err == 0) {
			adapter->flags |= QLCNIC_MSIX_ENABLED;
			qlcnic_set_msix_bit(pdev, 1);

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
		adapter->flags |= QLCNIC_MSI_ENABLED;
		adapter->tgt_status_reg = qlcnic_get_ioaddr(adapter,
				msi_tgt_status[adapter->ahw.pci_func]);
		dev_info(&pdev->dev, "using msi interrupts\n");
		adapter->msix_entries[0].vector = pdev->irq;
		return;
	}

	dev_info(&pdev->dev, "using legacy interrupts\n");
	adapter->msix_entries[0].vector = pdev->irq;
}

static void
qlcnic_teardown_intr(struct qlcnic_adapter *adapter)
{
	if (adapter->flags & QLCNIC_MSIX_ENABLED)
		pci_disable_msix(adapter->pdev);
	if (adapter->flags & QLCNIC_MSI_ENABLED)
		pci_disable_msi(adapter->pdev);
}

static void
qlcnic_cleanup_pci_map(struct qlcnic_adapter *adapter)
{
	if (adapter->ahw.pci_base0 != NULL)
		iounmap(adapter->ahw.pci_base0);
}

static int
qlcnic_setup_pci_map(struct qlcnic_adapter *adapter)
{
	void __iomem *mem_ptr0 = NULL;
	resource_size_t mem_base;
	unsigned long mem_len, pci_len0 = 0;

	struct pci_dev *pdev = adapter->pdev;
	int pci_func = adapter->ahw.pci_func;

	/*
	 * Set the CRB window to invalid. If any register in window 0 is
	 * accessed it should set the window to 0 and then reset it to 1.
	 */
	adapter->ahw.crb_win = -1;
	adapter->ahw.ocm_win = -1;

	/* remap phys address */
	mem_base = pci_resource_start(pdev, 0);	/* 0 is for BAR 0 */
	mem_len = pci_resource_len(pdev, 0);

	if (mem_len == QLCNIC_PCI_2MB_SIZE) {

		mem_ptr0 = pci_ioremap_bar(pdev, 0);
		if (mem_ptr0 == NULL) {
			dev_err(&pdev->dev, "failed to map PCI bar 0\n");
			return -EIO;
		}
		pci_len0 = mem_len;
	} else {
		return -EIO;
	}

	dev_info(&pdev->dev, "%dMB memory map\n", (int)(mem_len>>20));

	adapter->ahw.pci_base0 = mem_ptr0;
	adapter->ahw.pci_len0 = pci_len0;

	adapter->ahw.ocm_win_crb = qlcnic_get_ioaddr(adapter,
		QLCNIC_PCIX_PS_REG(PCIX_OCM_WINDOW_REG(pci_func)));

	return 0;
}

static void get_brd_name(struct qlcnic_adapter *adapter, char *name)
{
	struct pci_dev *pdev = adapter->pdev;
	int i, found = 0;

	for (i = 0; i < NUM_SUPPORTED_BOARDS; ++i) {
		if (qlcnic_boards[i].vendor == pdev->vendor &&
			qlcnic_boards[i].device == pdev->device &&
			qlcnic_boards[i].sub_vendor == pdev->subsystem_vendor &&
			qlcnic_boards[i].sub_device == pdev->subsystem_device) {
				strcpy(name, qlcnic_boards[i].short_name);
				found = 1;
				break;
		}

	}

	if (!found)
		name = "Unknown";
}

static void
qlcnic_check_options(struct qlcnic_adapter *adapter)
{
	u32 fw_major, fw_minor, fw_build;
	char brd_name[QLCNIC_MAX_BOARD_NAME_LEN];
	char serial_num[32];
	int i, offset, val;
	int *ptr32;
	struct pci_dev *pdev = adapter->pdev;

	adapter->driver_mismatch = 0;

	ptr32 = (int *)&serial_num;
	offset = QLCNIC_FW_SERIAL_NUM_OFFSET;
	for (i = 0; i < 8; i++) {
		if (qlcnic_rom_fast_read(adapter, offset, &val) == -1) {
			dev_err(&pdev->dev, "error reading board info\n");
			adapter->driver_mismatch = 1;
			return;
		}
		ptr32[i] = cpu_to_le32(val);
		offset += sizeof(u32);
	}

	fw_major = QLCRD32(adapter, QLCNIC_FW_VERSION_MAJOR);
	fw_minor = QLCRD32(adapter, QLCNIC_FW_VERSION_MINOR);
	fw_build = QLCRD32(adapter, QLCNIC_FW_VERSION_SUB);

	adapter->fw_version = QLCNIC_VERSION_CODE(fw_major, fw_minor, fw_build);

	if (adapter->portnum == 0) {
		get_brd_name(adapter, brd_name);

		pr_info("%s: %s Board Chip rev 0x%x\n",
				module_name(THIS_MODULE),
				brd_name, adapter->ahw.revision_id);
	}

	if (adapter->fw_version < QLCNIC_VERSION_CODE(3, 4, 216)) {
		adapter->driver_mismatch = 1;
		dev_warn(&pdev->dev, "firmware version %d.%d.%d unsupported\n",
				fw_major, fw_minor, fw_build);
		return;
	}

	i = QLCRD32(adapter, QLCNIC_SRE_MISC);
	adapter->ahw.cut_through = (i & 0x8000) ? 1 : 0;

	dev_info(&pdev->dev, "firmware v%d.%d.%d [%s]\n",
			fw_major, fw_minor, fw_build,
			adapter->ahw.cut_through ? "cut-through" : "legacy");

	if (adapter->fw_version >= QLCNIC_VERSION_CODE(4, 0, 222))
		adapter->capabilities = QLCRD32(adapter, CRB_FW_CAPABILITIES_1);

	adapter->flags &= ~QLCNIC_LRO_ENABLED;

	if (adapter->ahw.port_type == QLCNIC_XGBE) {
		adapter->num_rxd = DEFAULT_RCV_DESCRIPTORS_10G;
		adapter->num_jumbo_rxd = MAX_JUMBO_RCV_DESCRIPTORS_10G;
	} else if (adapter->ahw.port_type == QLCNIC_GBE) {
		adapter->num_rxd = DEFAULT_RCV_DESCRIPTORS_1G;
		adapter->num_jumbo_rxd = MAX_JUMBO_RCV_DESCRIPTORS_1G;
	}

	adapter->msix_supported = !!use_msi_x;
	adapter->rss_supported = !!use_msi_x;

	adapter->num_txd = MAX_CMD_DESCRIPTORS;

	adapter->num_lro_rxd = 0;
	adapter->max_rds_rings = 2;
}

static int
qlcnic_start_firmware(struct qlcnic_adapter *adapter)
{
	int val, err, first_boot;

	err = qlcnic_set_dma_mask(adapter);
	if (err)
		return err;

	if (!qlcnic_can_start_firmware(adapter))
		goto wait_init;

	first_boot = QLCRD32(adapter, QLCNIC_CAM_RAM(0x1fc));
	if (first_boot == 0x55555555)
		/* This is the first boot after power up */
		QLCWR32(adapter, QLCNIC_CAM_RAM(0x1fc), QLCNIC_BDINFO_MAGIC);

	qlcnic_request_firmware(adapter);

	err = qlcnic_need_fw_reset(adapter);
	if (err < 0)
		goto err_out;
	if (err == 0)
		goto wait_init;

	if (first_boot != 0x55555555) {
		QLCWR32(adapter, CRB_CMDPEG_STATE, 0);
		qlcnic_pinit_from_rom(adapter);
		msleep(1);
	}

	QLCWR32(adapter, CRB_DMA_SHIFT, 0x55555555);
	QLCWR32(adapter, QLCNIC_PEG_HALT_STATUS1, 0);
	QLCWR32(adapter, QLCNIC_PEG_HALT_STATUS2, 0);

	qlcnic_set_port_mode(adapter);

	err = qlcnic_load_firmware(adapter);
	if (err)
		goto err_out;

	qlcnic_release_firmware(adapter);

	val = (_QLCNIC_LINUX_MAJOR << 16)
		| ((_QLCNIC_LINUX_MINOR << 8))
		| (_QLCNIC_LINUX_SUBVERSION);
	QLCWR32(adapter, CRB_DRIVER_VERSION, val);

wait_init:
	/* Handshake with the card before we register the devices. */
	err = qlcnic_phantom_init(adapter);
	if (err)
		goto err_out;

	QLCWR32(adapter, QLCNIC_CRB_DEV_STATE, QLCNIC_DEV_READY);

	qlcnic_update_dma_mask(adapter);

	qlcnic_check_options(adapter);

	adapter->need_fw_reset = 0;

	/* fall through and release firmware */

err_out:
	qlcnic_release_firmware(adapter);
	return err;
}

static int
qlcnic_request_irq(struct qlcnic_adapter *adapter)
{
	irq_handler_t handler;
	struct qlcnic_host_sds_ring *sds_ring;
	int err, ring;

	unsigned long flags = 0;
	struct net_device *netdev = adapter->netdev;
	struct qlcnic_recv_context *recv_ctx = &adapter->recv_ctx;

	if (adapter->diag_test == QLCNIC_INTERRUPT_TEST) {
		handler = qlcnic_tmp_intr;
		if (!QLCNIC_IS_MSI_FAMILY(adapter))
			flags |= IRQF_SHARED;

	} else {
		if (adapter->flags & QLCNIC_MSIX_ENABLED)
			handler = qlcnic_msix_intr;
		else if (adapter->flags & QLCNIC_MSI_ENABLED)
			handler = qlcnic_msi_intr;
		else {
			flags |= IRQF_SHARED;
			handler = qlcnic_intr;
		}
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
qlcnic_free_irq(struct qlcnic_adapter *adapter)
{
	int ring;
	struct qlcnic_host_sds_ring *sds_ring;

	struct qlcnic_recv_context *recv_ctx = &adapter->recv_ctx;

	for (ring = 0; ring < adapter->max_sds_rings; ring++) {
		sds_ring = &recv_ctx->sds_rings[ring];
		free_irq(sds_ring->irq, sds_ring);
	}
}

static void
qlcnic_init_coalesce_defaults(struct qlcnic_adapter *adapter)
{
	adapter->coal.flags = QLCNIC_INTR_DEFAULT;
	adapter->coal.normal.data.rx_time_us =
		QLCNIC_DEFAULT_INTR_COALESCE_RX_TIME_US;
	adapter->coal.normal.data.rx_packets =
		QLCNIC_DEFAULT_INTR_COALESCE_RX_PACKETS;
	adapter->coal.normal.data.tx_time_us =
		QLCNIC_DEFAULT_INTR_COALESCE_TX_TIME_US;
	adapter->coal.normal.data.tx_packets =
		QLCNIC_DEFAULT_INTR_COALESCE_TX_PACKETS;
}

static int
__qlcnic_up(struct qlcnic_adapter *adapter, struct net_device *netdev)
{
	if (adapter->is_up != QLCNIC_ADAPTER_UP_MAGIC)
		return -EIO;

	qlcnic_set_multi(netdev);
	qlcnic_fw_cmd_set_mtu(adapter, netdev->mtu);

	adapter->ahw.linkup = 0;

	if (adapter->max_sds_rings > 1)
		qlcnic_config_rss(adapter, 1);

	qlcnic_config_intr_coalesce(adapter);

	if (adapter->capabilities & QLCNIC_FW_CAPABILITY_HW_LRO)
		qlcnic_config_hw_lro(adapter, QLCNIC_LRO_ENABLED);

	qlcnic_napi_enable(adapter);

	qlcnic_linkevent_request(adapter, 1);

	set_bit(__QLCNIC_DEV_UP, &adapter->state);
	return 0;
}

/* Usage: During resume and firmware recovery module.*/

static int
qlcnic_up(struct qlcnic_adapter *adapter, struct net_device *netdev)
{
	int err = 0;

	rtnl_lock();
	if (netif_running(netdev))
		err = __qlcnic_up(adapter, netdev);
	rtnl_unlock();

	return err;
}

static void
__qlcnic_down(struct qlcnic_adapter *adapter, struct net_device *netdev)
{
	if (adapter->is_up != QLCNIC_ADAPTER_UP_MAGIC)
		return;

	if (!test_and_clear_bit(__QLCNIC_DEV_UP, &adapter->state))
		return;

	smp_mb();
	spin_lock(&adapter->tx_clean_lock);
	netif_carrier_off(netdev);
	netif_tx_disable(netdev);

	qlcnic_free_mac_list(adapter);

	qlcnic_nic_set_promisc(adapter, QLCNIC_NIU_NON_PROMISC_MODE);

	qlcnic_napi_disable(adapter);

	qlcnic_release_tx_buffers(adapter);
	spin_unlock(&adapter->tx_clean_lock);
}

/* Usage: During suspend and firmware recovery module */

static void
qlcnic_down(struct qlcnic_adapter *adapter, struct net_device *netdev)
{
	rtnl_lock();
	if (netif_running(netdev))
		__qlcnic_down(adapter, netdev);
	rtnl_unlock();

}

static int
qlcnic_attach(struct qlcnic_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;
	struct pci_dev *pdev = adapter->pdev;
	int err, ring;
	struct qlcnic_host_rds_ring *rds_ring;

	if (adapter->is_up == QLCNIC_ADAPTER_UP_MAGIC)
		return 0;

	err = qlcnic_init_firmware(adapter);
	if (err)
		return err;

	err = qlcnic_napi_add(adapter, netdev);
	if (err)
		return err;

	err = qlcnic_alloc_sw_resources(adapter);
	if (err) {
		dev_err(&pdev->dev, "Error in setting sw resources\n");
		return err;
	}

	err = qlcnic_alloc_hw_resources(adapter);
	if (err) {
		dev_err(&pdev->dev, "Error in setting hw resources\n");
		goto err_out_free_sw;
	}


	for (ring = 0; ring < adapter->max_rds_rings; ring++) {
		rds_ring = &adapter->recv_ctx.rds_rings[ring];
		qlcnic_post_rx_buffers(adapter, ring, rds_ring);
	}

	err = qlcnic_request_irq(adapter);
	if (err) {
		dev_err(&pdev->dev, "failed to setup interrupt\n");
		goto err_out_free_rxbuf;
	}

	qlcnic_init_coalesce_defaults(adapter);

	qlcnic_create_sysfs_entries(adapter);

	adapter->is_up = QLCNIC_ADAPTER_UP_MAGIC;
	return 0;

err_out_free_rxbuf:
	qlcnic_release_rx_buffers(adapter);
	qlcnic_free_hw_resources(adapter);
err_out_free_sw:
	qlcnic_free_sw_resources(adapter);
	return err;
}

static void
qlcnic_detach(struct qlcnic_adapter *adapter)
{
	if (adapter->is_up != QLCNIC_ADAPTER_UP_MAGIC)
		return;

	qlcnic_remove_sysfs_entries(adapter);

	qlcnic_free_hw_resources(adapter);
	qlcnic_release_rx_buffers(adapter);
	qlcnic_free_irq(adapter);
	qlcnic_napi_del(adapter);
	qlcnic_free_sw_resources(adapter);

	adapter->is_up = 0;
}

void qlcnic_diag_free_res(struct net_device *netdev, int max_sds_rings)
{
	struct qlcnic_adapter *adapter = netdev_priv(netdev);
	struct qlcnic_host_sds_ring *sds_ring;
	int ring;

	if (adapter->diag_test == QLCNIC_INTERRUPT_TEST) {
		for (ring = 0; ring < adapter->max_sds_rings; ring++) {
			sds_ring = &adapter->recv_ctx.sds_rings[ring];
			qlcnic_disable_int(sds_ring);
		}
	}

	qlcnic_detach(adapter);

	adapter->diag_test = 0;
	adapter->max_sds_rings = max_sds_rings;

	if (qlcnic_attach(adapter))
		return;

	if (netif_running(netdev))
		__qlcnic_up(adapter, netdev);

	netif_device_attach(netdev);
}

int qlcnic_diag_alloc_res(struct net_device *netdev, int test)
{
	struct qlcnic_adapter *adapter = netdev_priv(netdev);
	struct qlcnic_host_sds_ring *sds_ring;
	int ring;
	int ret;

	netif_device_detach(netdev);

	if (netif_running(netdev))
		__qlcnic_down(adapter, netdev);

	qlcnic_detach(adapter);

	adapter->max_sds_rings = 1;
	adapter->diag_test = test;

	ret = qlcnic_attach(adapter);
	if (ret)
		return ret;

	if (adapter->diag_test == QLCNIC_INTERRUPT_TEST) {
		for (ring = 0; ring < adapter->max_sds_rings; ring++) {
			sds_ring = &adapter->recv_ctx.sds_rings[ring];
			qlcnic_enable_int(sds_ring);
		}
	}

	return 0;
}

int
qlcnic_reset_context(struct qlcnic_adapter *adapter)
{
	int err = 0;
	struct net_device *netdev = adapter->netdev;

	if (test_and_set_bit(__QLCNIC_RESETTING, &adapter->state))
		return -EBUSY;

	if (adapter->is_up == QLCNIC_ADAPTER_UP_MAGIC) {

		netif_device_detach(netdev);

		if (netif_running(netdev))
			__qlcnic_down(adapter, netdev);

		qlcnic_detach(adapter);

		if (netif_running(netdev)) {
			err = qlcnic_attach(adapter);
			if (!err)
				err = __qlcnic_up(adapter, netdev);

			if (err)
				goto done;
		}

		netif_device_attach(netdev);
	}

done:
	clear_bit(__QLCNIC_RESETTING, &adapter->state);
	return err;
}

static int
qlcnic_setup_netdev(struct qlcnic_adapter *adapter,
		struct net_device *netdev)
{
	int err;
	struct pci_dev *pdev = adapter->pdev;

	adapter->rx_csum = 1;
	adapter->mc_enabled = 0;
	adapter->max_mc_count = 38;

	netdev->netdev_ops	   = &qlcnic_netdev_ops;
	netdev->watchdog_timeo     = 2*HZ;

	qlcnic_change_mtu(netdev, netdev->mtu);

	SET_ETHTOOL_OPS(netdev, &qlcnic_ethtool_ops);

	netdev->features |= (NETIF_F_SG | NETIF_F_IP_CSUM | NETIF_F_TSO);
	netdev->features |= (NETIF_F_GRO);
	netdev->vlan_features |= (NETIF_F_SG | NETIF_F_IP_CSUM | NETIF_F_TSO);

	netdev->features |= (NETIF_F_IPV6_CSUM | NETIF_F_TSO6);
	netdev->vlan_features |= (NETIF_F_IPV6_CSUM | NETIF_F_TSO6);

	if (adapter->pci_using_dac) {
		netdev->features |= NETIF_F_HIGHDMA;
		netdev->vlan_features |= NETIF_F_HIGHDMA;
	}

	if (adapter->capabilities & QLCNIC_FW_CAPABILITY_FVLANTX)
		netdev->features |= (NETIF_F_HW_VLAN_TX);

	if (adapter->capabilities & QLCNIC_FW_CAPABILITY_HW_LRO)
		netdev->features |= NETIF_F_LRO;

	netdev->irq = adapter->msix_entries[0].vector;

	INIT_WORK(&adapter->tx_timeout_task, qlcnic_tx_timeout_task);

	if (qlcnic_read_mac_addr(adapter))
		dev_warn(&pdev->dev, "failed to read mac addr\n");

	netif_carrier_off(netdev);
	netif_stop_queue(netdev);

	err = register_netdev(netdev);
	if (err) {
		dev_err(&pdev->dev, "failed to register net device\n");
		return err;
	}

	return 0;
}

static int __devinit
qlcnic_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct net_device *netdev = NULL;
	struct qlcnic_adapter *adapter = NULL;
	int err;
	int pci_func_id = PCI_FUNC(pdev->devfn);
	uint8_t revision_id;

	err = pci_enable_device(pdev);
	if (err)
		return err;

	if (!(pci_resource_flags(pdev, 0) & IORESOURCE_MEM)) {
		err = -ENODEV;
		goto err_out_disable_pdev;
	}

	err = pci_request_regions(pdev, qlcnic_driver_name);
	if (err)
		goto err_out_disable_pdev;

	pci_set_master(pdev);

	netdev = alloc_etherdev(sizeof(struct qlcnic_adapter));
	if (!netdev) {
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
	mutex_init(&adapter->ahw.mem_lock);

	spin_lock_init(&adapter->tx_clean_lock);
	INIT_LIST_HEAD(&adapter->mac_list);

	err = qlcnic_setup_pci_map(adapter);
	if (err)
		goto err_out_free_netdev;

	/* This will be reset for mezz cards  */
	adapter->portnum = pci_func_id;

	err = qlcnic_get_board_info(adapter);
	if (err) {
		dev_err(&pdev->dev, "Error getting board config info.\n");
		goto err_out_iounmap;
	}


	err = qlcnic_start_firmware(adapter);
	if (err)
		goto err_out_decr_ref;

	/*
	 * See if the firmware gave us a virtual-physical port mapping.
	 */
	adapter->physical_port = adapter->portnum;

	qlcnic_clear_stats(adapter);

	qlcnic_setup_intr(adapter);

	err = qlcnic_setup_netdev(adapter, netdev);
	if (err)
		goto err_out_disable_msi;

	pci_set_drvdata(pdev, adapter);

	qlcnic_schedule_work(adapter, qlcnic_fw_poll_work, FW_POLL_DELAY);

	switch (adapter->ahw.port_type) {
	case QLCNIC_GBE:
		dev_info(&adapter->pdev->dev, "%s: GbE port initialized\n",
				adapter->netdev->name);
		break;
	case QLCNIC_XGBE:
		dev_info(&adapter->pdev->dev, "%s: XGbE port initialized\n",
				adapter->netdev->name);
		break;
	}

	qlcnic_create_diag_entries(adapter);

	return 0;

err_out_disable_msi:
	qlcnic_teardown_intr(adapter);

err_out_decr_ref:
	qlcnic_clr_all_drv_state(adapter);

err_out_iounmap:
	qlcnic_cleanup_pci_map(adapter);

err_out_free_netdev:
	free_netdev(netdev);

err_out_free_res:
	pci_release_regions(pdev);

err_out_disable_pdev:
	pci_set_drvdata(pdev, NULL);
	pci_disable_device(pdev);
	return err;
}

static void __devexit qlcnic_remove(struct pci_dev *pdev)
{
	struct qlcnic_adapter *adapter;
	struct net_device *netdev;

	adapter = pci_get_drvdata(pdev);
	if (adapter == NULL)
		return;

	netdev = adapter->netdev;

	qlcnic_cancel_fw_work(adapter);

	unregister_netdev(netdev);

	cancel_work_sync(&adapter->tx_timeout_task);

	qlcnic_detach(adapter);

	qlcnic_clr_all_drv_state(adapter);

	clear_bit(__QLCNIC_RESETTING, &adapter->state);

	qlcnic_teardown_intr(adapter);

	qlcnic_remove_diag_entries(adapter);

	qlcnic_cleanup_pci_map(adapter);

	qlcnic_release_firmware(adapter);

	pci_release_regions(pdev);
	pci_disable_device(pdev);
	pci_set_drvdata(pdev, NULL);

	free_netdev(netdev);
}
static int __qlcnic_shutdown(struct pci_dev *pdev)
{
	struct qlcnic_adapter *adapter = pci_get_drvdata(pdev);
	struct net_device *netdev = adapter->netdev;
	int retval;

	netif_device_detach(netdev);

	qlcnic_cancel_fw_work(adapter);

	if (netif_running(netdev))
		qlcnic_down(adapter, netdev);

	cancel_work_sync(&adapter->tx_timeout_task);

	qlcnic_detach(adapter);

	qlcnic_clr_all_drv_state(adapter);

	clear_bit(__QLCNIC_RESETTING, &adapter->state);

	retval = pci_save_state(pdev);
	if (retval)
		return retval;

	if (qlcnic_wol_supported(adapter)) {
		pci_enable_wake(pdev, PCI_D3cold, 1);
		pci_enable_wake(pdev, PCI_D3hot, 1);
	}

	return 0;
}

static void qlcnic_shutdown(struct pci_dev *pdev)
{
	if (__qlcnic_shutdown(pdev))
		return;

	pci_disable_device(pdev);
}

#ifdef CONFIG_PM
static int
qlcnic_suspend(struct pci_dev *pdev, pm_message_t state)
{
	int retval;

	retval = __qlcnic_shutdown(pdev);
	if (retval)
		return retval;

	pci_set_power_state(pdev, pci_choose_state(pdev, state));
	return 0;
}

static int
qlcnic_resume(struct pci_dev *pdev)
{
	struct qlcnic_adapter *adapter = pci_get_drvdata(pdev);
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

	err = qlcnic_start_firmware(adapter);
	if (err) {
		dev_err(&pdev->dev, "failed to start firmware\n");
		return err;
	}

	if (netif_running(netdev)) {
		err = qlcnic_attach(adapter);
		if (err)
			goto err_out;

		err = qlcnic_up(adapter, netdev);
		if (err)
			goto err_out_detach;


		qlcnic_config_indev_addr(netdev, NETDEV_UP);
	}

	netif_device_attach(netdev);
	qlcnic_schedule_work(adapter, qlcnic_fw_poll_work, FW_POLL_DELAY);
	return 0;

err_out_detach:
	qlcnic_detach(adapter);
err_out:
	qlcnic_clr_all_drv_state(adapter);
	return err;
}
#endif

static int qlcnic_open(struct net_device *netdev)
{
	struct qlcnic_adapter *adapter = netdev_priv(netdev);
	int err;

	if (adapter->driver_mismatch)
		return -EIO;

	err = qlcnic_attach(adapter);
	if (err)
		return err;

	err = __qlcnic_up(adapter, netdev);
	if (err)
		goto err_out;

	netif_start_queue(netdev);

	return 0;

err_out:
	qlcnic_detach(adapter);
	return err;
}

/*
 * qlcnic_close - Disables a network interface entry point
 */
static int qlcnic_close(struct net_device *netdev)
{
	struct qlcnic_adapter *adapter = netdev_priv(netdev);

	__qlcnic_down(adapter, netdev);
	return 0;
}

static void
qlcnic_tso_check(struct net_device *netdev,
		struct qlcnic_host_tx_ring *tx_ring,
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
	struct qlcnic_adapter *adapter = netdev_priv(netdev);

	if (protocol == cpu_to_be16(ETH_P_8021Q)) {

		vh = (struct vlan_ethhdr *)skb->data;
		protocol = vh->h_vlan_encapsulated_proto;
		flags = FLAGS_VLAN_TAGGED;

	} else if (vlan_tx_tag_present(skb)) {

		flags = FLAGS_VLAN_OOB;
		vid = vlan_tx_tag_get(skb);
		qlcnic_set_tx_vlan_tci(first_desc, vid);
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
			else if (l4proto == IPPROTO_UDP)
				opcode = TX_UDP_PKT;
		} else if (protocol == cpu_to_be16(ETH_P_IPV6)) {
			l4proto = ipv6_hdr(skb)->nexthdr;

			if (l4proto == IPPROTO_TCP)
				opcode = TX_TCPV6_PKT;
			else if (l4proto == IPPROTO_UDP)
				opcode = TX_UDPV6_PKT;
		}
	}

	first_desc->tcp_hdr_offset += skb_transport_offset(skb);
	first_desc->ip_hdr_offset += skb_network_offset(skb);
	qlcnic_set_tx_flags_opcode(first_desc, flags, opcode);

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
	adapter->stats.lso_frames++;
}

static int
qlcnic_map_tx_skb(struct pci_dev *pdev,
		struct sk_buff *skb, struct qlcnic_cmd_buffer *pbuf)
{
	struct qlcnic_skb_frag *nf;
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

		map = pci_map_page(pdev, frag->page, frag->page_offset,
				frag->size, PCI_DMA_TODEVICE);
		if (pci_dma_mapping_error(pdev, map))
			goto unwind;

		nf->dma = map;
		nf->length = frag->size;
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
qlcnic_clear_cmddesc(u64 *desc)
{
	desc[0] = 0ULL;
	desc[2] = 0ULL;
}

netdev_tx_t
qlcnic_xmit_frame(struct sk_buff *skb, struct net_device *netdev)
{
	struct qlcnic_adapter *adapter = netdev_priv(netdev);
	struct qlcnic_host_tx_ring *tx_ring = adapter->tx_ring;
	struct qlcnic_cmd_buffer *pbuf;
	struct qlcnic_skb_frag *buffrag;
	struct cmd_desc_type0 *hwdesc, *first_desc;
	struct pci_dev *pdev;
	int i, k;

	u32 producer;
	int frag_count, no_of_desc;
	u32 num_txd = tx_ring->num_desc;

	frag_count = skb_shinfo(skb)->nr_frags + 1;

	/* 4 fragments per cmd des */
	no_of_desc = (frag_count + 3) >> 2;

	if (unlikely(no_of_desc + 2 > qlcnic_tx_avail(tx_ring))) {
		netif_stop_queue(netdev);
		adapter->stats.xmit_off++;
		return NETDEV_TX_BUSY;
	}

	producer = tx_ring->producer;
	pbuf = &tx_ring->cmd_buf_arr[producer];

	pdev = adapter->pdev;

	if (qlcnic_map_tx_skb(pdev, skb, pbuf))
		goto drop_packet;

	pbuf->skb = skb;
	pbuf->frag_count = frag_count;

	first_desc = hwdesc = &tx_ring->desc_head[producer];
	qlcnic_clear_cmddesc((u64 *)hwdesc);

	qlcnic_set_tx_frags_len(first_desc, frag_count, skb->len);
	qlcnic_set_tx_port(first_desc, adapter->portnum);

	for (i = 0; i < frag_count; i++) {

		k = i % 4;

		if ((k == 0) && (i > 0)) {
			/* move to next desc.*/
			producer = get_next_index(producer, num_txd);
			hwdesc = &tx_ring->desc_head[producer];
			qlcnic_clear_cmddesc((u64 *)hwdesc);
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

	qlcnic_tso_check(netdev, tx_ring, first_desc, skb);

	qlcnic_update_cmd_producer(adapter, tx_ring);

	adapter->stats.txbytes += skb->len;
	adapter->stats.xmitcalled++;

	return NETDEV_TX_OK;

drop_packet:
	adapter->stats.txdropped++;
	dev_kfree_skb_any(skb);
	return NETDEV_TX_OK;
}

static int qlcnic_check_temp(struct qlcnic_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;
	u32 temp, temp_state, temp_val;
	int rv = 0;

	temp = QLCRD32(adapter, CRB_TEMP_STATE);

	temp_state = qlcnic_get_temp_state(temp);
	temp_val = qlcnic_get_temp_val(temp);

	if (temp_state == QLCNIC_TEMP_PANIC) {
		dev_err(&netdev->dev,
		       "Device temperature %d degrees C exceeds"
		       " maximum allowed. Hardware has been shut down.\n",
		       temp_val);
		rv = 1;
	} else if (temp_state == QLCNIC_TEMP_WARN) {
		if (adapter->temp == QLCNIC_TEMP_NORMAL) {
			dev_err(&netdev->dev,
			       "Device temperature %d degrees C "
			       "exceeds operating range."
			       " Immediate action needed.\n",
			       temp_val);
		}
	} else {
		if (adapter->temp == QLCNIC_TEMP_WARN) {
			dev_info(&netdev->dev,
			       "Device temperature is now %d degrees C"
			       " in normal range.\n", temp_val);
		}
	}
	adapter->temp = temp_state;
	return rv;
}

void qlcnic_advert_link_change(struct qlcnic_adapter *adapter, int linkup)
{
	struct net_device *netdev = adapter->netdev;

	if (adapter->ahw.linkup && !linkup) {
		dev_info(&netdev->dev, "NIC Link is down\n");
		adapter->ahw.linkup = 0;
		if (netif_running(netdev)) {
			netif_carrier_off(netdev);
			netif_stop_queue(netdev);
		}
	} else if (!adapter->ahw.linkup && linkup) {
		dev_info(&netdev->dev, "NIC Link is up\n");
		adapter->ahw.linkup = 1;
		if (netif_running(netdev)) {
			netif_carrier_on(netdev);
			netif_wake_queue(netdev);
		}
	}
}

static void qlcnic_tx_timeout(struct net_device *netdev)
{
	struct qlcnic_adapter *adapter = netdev_priv(netdev);

	if (test_bit(__QLCNIC_RESETTING, &adapter->state))
		return;

	dev_err(&netdev->dev, "transmit timeout, resetting.\n");
	schedule_work(&adapter->tx_timeout_task);
}

static void qlcnic_tx_timeout_task(struct work_struct *work)
{
	struct qlcnic_adapter *adapter =
		container_of(work, struct qlcnic_adapter, tx_timeout_task);

	if (!netif_running(adapter->netdev))
		return;

	if (test_and_set_bit(__QLCNIC_RESETTING, &adapter->state))
		return;

	if (++adapter->tx_timeo_cnt >= QLCNIC_MAX_TX_TIMEOUTS)
		goto request_reset;

	clear_bit(__QLCNIC_RESETTING, &adapter->state);
	if (!qlcnic_reset_context(adapter)) {
		adapter->netdev->trans_start = jiffies;
		return;

		/* context reset failed, fall through for fw reset */
	}

request_reset:
	adapter->need_fw_reset = 1;
	clear_bit(__QLCNIC_RESETTING, &adapter->state);
}

static struct net_device_stats *qlcnic_get_stats(struct net_device *netdev)
{
	struct qlcnic_adapter *adapter = netdev_priv(netdev);
	struct net_device_stats *stats = &netdev->stats;

	memset(stats, 0, sizeof(*stats));

	stats->rx_packets = adapter->stats.rx_pkts + adapter->stats.lro_pkts;
	stats->tx_packets = adapter->stats.xmitfinished;
	stats->rx_bytes = adapter->stats.rxbytes;
	stats->tx_bytes = adapter->stats.txbytes;
	stats->rx_dropped = adapter->stats.rxdropped;
	stats->tx_dropped = adapter->stats.txdropped;

	return stats;
}

static irqreturn_t qlcnic_clear_legacy_intr(struct qlcnic_adapter *adapter)
{
	u32 status;

	status = readl(adapter->isr_int_vec);

	if (!(status & adapter->int_vec_bit))
		return IRQ_NONE;

	/* check interrupt state machine, to be sure */
	status = readl(adapter->crb_int_state_reg);
	if (!ISR_LEGACY_INT_TRIGGERED(status))
		return IRQ_NONE;

	writel(0xffffffff, adapter->tgt_status_reg);
	/* read twice to ensure write is flushed */
	readl(adapter->isr_int_vec);
	readl(adapter->isr_int_vec);

	return IRQ_HANDLED;
}

static irqreturn_t qlcnic_tmp_intr(int irq, void *data)
{
	struct qlcnic_host_sds_ring *sds_ring = data;
	struct qlcnic_adapter *adapter = sds_ring->adapter;

	if (adapter->flags & QLCNIC_MSIX_ENABLED)
		goto done;
	else if (adapter->flags & QLCNIC_MSI_ENABLED) {
		writel(0xffffffff, adapter->tgt_status_reg);
		goto done;
	}

	if (qlcnic_clear_legacy_intr(adapter) == IRQ_NONE)
		return IRQ_NONE;

done:
	adapter->diag_cnt++;
	qlcnic_enable_int(sds_ring);
	return IRQ_HANDLED;
}

static irqreturn_t qlcnic_intr(int irq, void *data)
{
	struct qlcnic_host_sds_ring *sds_ring = data;
	struct qlcnic_adapter *adapter = sds_ring->adapter;

	if (qlcnic_clear_legacy_intr(adapter) == IRQ_NONE)
		return IRQ_NONE;

	napi_schedule(&sds_ring->napi);

	return IRQ_HANDLED;
}

static irqreturn_t qlcnic_msi_intr(int irq, void *data)
{
	struct qlcnic_host_sds_ring *sds_ring = data;
	struct qlcnic_adapter *adapter = sds_ring->adapter;

	/* clear interrupt */
	writel(0xffffffff, adapter->tgt_status_reg);

	napi_schedule(&sds_ring->napi);
	return IRQ_HANDLED;
}

static irqreturn_t qlcnic_msix_intr(int irq, void *data)
{
	struct qlcnic_host_sds_ring *sds_ring = data;

	napi_schedule(&sds_ring->napi);
	return IRQ_HANDLED;
}

static int qlcnic_process_cmd_ring(struct qlcnic_adapter *adapter)
{
	u32 sw_consumer, hw_consumer;
	int count = 0, i;
	struct qlcnic_cmd_buffer *buffer;
	struct pci_dev *pdev = adapter->pdev;
	struct net_device *netdev = adapter->netdev;
	struct qlcnic_skb_frag *frag;
	int done;
	struct qlcnic_host_tx_ring *tx_ring = adapter->tx_ring;

	if (!spin_trylock(&adapter->tx_clean_lock))
		return 1;

	sw_consumer = tx_ring->sw_consumer;
	hw_consumer = le32_to_cpu(*(tx_ring->hw_consumer));

	while (sw_consumer != hw_consumer) {
		buffer = &tx_ring->cmd_buf_arr[sw_consumer];
		if (buffer->skb) {
			frag = &buffer->frag_array[0];
			pci_unmap_single(pdev, frag->dma, frag->length,
					 PCI_DMA_TODEVICE);
			frag->dma = 0ULL;
			for (i = 1; i < buffer->frag_count; i++) {
				frag++;
				pci_unmap_page(pdev, frag->dma, frag->length,
					       PCI_DMA_TODEVICE);
				frag->dma = 0ULL;
			}

			adapter->stats.xmitfinished++;
			dev_kfree_skb_any(buffer->skb);
			buffer->skb = NULL;
		}

		sw_consumer = get_next_index(sw_consumer, tx_ring->num_desc);
		if (++count >= MAX_STATUS_HANDLE)
			break;
	}

	if (count && netif_running(netdev)) {
		tx_ring->sw_consumer = sw_consumer;

		smp_mb();

		if (netif_queue_stopped(netdev) && netif_carrier_ok(netdev)) {
			__netif_tx_lock(tx_ring->txq, smp_processor_id());
			if (qlcnic_tx_avail(tx_ring) > TX_STOP_THRESH) {
				netif_wake_queue(netdev);
				adapter->tx_timeo_cnt = 0;
				adapter->stats.xmit_on++;
			}
			__netif_tx_unlock(tx_ring->txq);
		}
	}
	/*
	 * If everything is freed up to consumer then check if the ring is full
	 * If the ring is full then check if more needs to be freed and
	 * schedule the call back again.
	 *
	 * This happens when there are 2 CPUs. One could be freeing and the
	 * other filling it. If the ring is full when we get out of here and
	 * the card has already interrupted the host then the host can miss the
	 * interrupt.
	 *
	 * There is still a possible race condition and the host could miss an
	 * interrupt. The card has to take care of this.
	 */
	hw_consumer = le32_to_cpu(*(tx_ring->hw_consumer));
	done = (sw_consumer == hw_consumer);
	spin_unlock(&adapter->tx_clean_lock);

	return done;
}

static int qlcnic_poll(struct napi_struct *napi, int budget)
{
	struct qlcnic_host_sds_ring *sds_ring =
		container_of(napi, struct qlcnic_host_sds_ring, napi);

	struct qlcnic_adapter *adapter = sds_ring->adapter;

	int tx_complete;
	int work_done;

	tx_complete = qlcnic_process_cmd_ring(adapter);

	work_done = qlcnic_process_rcv_ring(sds_ring, budget);

	if ((work_done < budget) && tx_complete) {
		napi_complete(&sds_ring->napi);
		if (test_bit(__QLCNIC_DEV_UP, &adapter->state))
			qlcnic_enable_int(sds_ring);
	}

	return work_done;
}

#ifdef CONFIG_NET_POLL_CONTROLLER
static void qlcnic_poll_controller(struct net_device *netdev)
{
	struct qlcnic_adapter *adapter = netdev_priv(netdev);
	disable_irq(adapter->irq);
	qlcnic_intr(adapter->irq, adapter);
	enable_irq(adapter->irq);
}
#endif

static void
qlcnic_set_drv_state(struct qlcnic_adapter *adapter, int state)
{
	u32  val;

	WARN_ON(state != QLCNIC_DEV_NEED_RESET &&
			state != QLCNIC_DEV_NEED_QUISCENT);

	if (qlcnic_api_lock(adapter))
		return ;

	val = QLCRD32(adapter, QLCNIC_CRB_DRV_STATE);

	if (state == QLCNIC_DEV_NEED_RESET)
		val |= ((u32)0x1 << (adapter->portnum * 4));
	else if (state == QLCNIC_DEV_NEED_QUISCENT)
		val |= ((u32)0x1 << ((adapter->portnum * 4) + 1));

	QLCWR32(adapter, QLCNIC_CRB_DRV_STATE, val);

	qlcnic_api_unlock(adapter);
}

static int
qlcnic_clr_drv_state(struct qlcnic_adapter *adapter)
{
	u32  val;

	if (qlcnic_api_lock(adapter))
		return -EBUSY;

	val = QLCRD32(adapter, QLCNIC_CRB_DRV_STATE);
	val &= ~((u32)0x3 << (adapter->portnum * 4));
	QLCWR32(adapter, QLCNIC_CRB_DRV_STATE, val);

	qlcnic_api_unlock(adapter);

	return 0;
}

static void
qlcnic_clr_all_drv_state(struct qlcnic_adapter *adapter)
{
	u32  val;

	if (qlcnic_api_lock(adapter))
		goto err;

	val = QLCRD32(adapter, QLCNIC_CRB_DEV_REF_COUNT);
	val &= ~((u32)0x1 << (adapter->portnum * 4));
	QLCWR32(adapter, QLCNIC_CRB_DEV_REF_COUNT, val);

	if (!(val & 0x11111111))
		QLCWR32(adapter, QLCNIC_CRB_DEV_STATE, QLCNIC_DEV_COLD);

	val = QLCRD32(adapter, QLCNIC_CRB_DRV_STATE);
	val &= ~((u32)0x3 << (adapter->portnum * 4));
	QLCWR32(adapter, QLCNIC_CRB_DRV_STATE, val);

	qlcnic_api_unlock(adapter);
err:
	adapter->fw_fail_cnt = 0;
	clear_bit(__QLCNIC_START_FW, &adapter->state);
	clear_bit(__QLCNIC_RESETTING, &adapter->state);
}

static int
qlcnic_check_drv_state(struct qlcnic_adapter *adapter)
{
	int act, state;

	state = QLCRD32(adapter, QLCNIC_CRB_DRV_STATE);
	act = QLCRD32(adapter, QLCNIC_CRB_DEV_REF_COUNT);

	if (((state & 0x11111111) == (act & 0x11111111)) ||
			((act & 0x11111111) == ((state >> 1) & 0x11111111)))
		return 0;
	else
		return 1;
}

static int
qlcnic_can_start_firmware(struct qlcnic_adapter *adapter)
{
	u32 val, prev_state;
	int cnt = 0;
	int portnum = adapter->portnum;

	if (qlcnic_api_lock(adapter))
		return -1;

	val = QLCRD32(adapter, QLCNIC_CRB_DEV_REF_COUNT);
	if (!(val & ((int)0x1 << (portnum * 4)))) {
		val |= ((u32)0x1 << (portnum * 4));
		QLCWR32(adapter, QLCNIC_CRB_DEV_REF_COUNT, val);
	} else if (test_and_clear_bit(__QLCNIC_START_FW, &adapter->state)) {
		goto start_fw;
	}

	prev_state = QLCRD32(adapter, QLCNIC_CRB_DEV_STATE);

	switch (prev_state) {
	case QLCNIC_DEV_COLD:
start_fw:
		QLCWR32(adapter, QLCNIC_CRB_DEV_STATE, QLCNIC_DEV_INITALIZING);
		qlcnic_api_unlock(adapter);
		return 1;

	case QLCNIC_DEV_READY:
		qlcnic_api_unlock(adapter);
		return 0;

	case QLCNIC_DEV_NEED_RESET:
		val = QLCRD32(adapter, QLCNIC_CRB_DRV_STATE);
		val |= ((u32)0x1 << (portnum * 4));
		QLCWR32(adapter, QLCNIC_CRB_DRV_STATE, val);
		break;

	case QLCNIC_DEV_NEED_QUISCENT:
		val = QLCRD32(adapter, QLCNIC_CRB_DRV_STATE);
		val |= ((u32)0x1 << ((portnum * 4) + 1));
		QLCWR32(adapter, QLCNIC_CRB_DRV_STATE, val);
		break;

	case QLCNIC_DEV_FAILED:
		qlcnic_api_unlock(adapter);
		return -1;
	}

	qlcnic_api_unlock(adapter);
	msleep(1000);
	while ((QLCRD32(adapter, QLCNIC_CRB_DEV_STATE) != QLCNIC_DEV_READY) &&
			++cnt < 20)
		msleep(1000);

	if (cnt >= 20)
		return -1;

	if (qlcnic_api_lock(adapter))
		return -1;

	val = QLCRD32(adapter, QLCNIC_CRB_DRV_STATE);
	val &= ~((u32)0x3 << (portnum * 4));
	QLCWR32(adapter, QLCNIC_CRB_DRV_STATE, val);

	qlcnic_api_unlock(adapter);

	return 0;
}

static void
qlcnic_fwinit_work(struct work_struct *work)
{
	struct qlcnic_adapter *adapter = container_of(work,
			struct qlcnic_adapter, fw_work.work);
	int dev_state;

	if (++adapter->fw_wait_cnt > FW_POLL_THRESH)
		goto err_ret;

	if (test_bit(__QLCNIC_START_FW, &adapter->state)) {

		if (qlcnic_check_drv_state(adapter)) {
			qlcnic_schedule_work(adapter,
					qlcnic_fwinit_work, FW_POLL_DELAY);
			return;
		}

		if (!qlcnic_start_firmware(adapter)) {
			qlcnic_schedule_work(adapter, qlcnic_attach_work, 0);
			return;
		}

		goto err_ret;
	}

	dev_state = QLCRD32(adapter, QLCNIC_CRB_DEV_STATE);
	switch (dev_state) {
	case QLCNIC_DEV_READY:
		if (!qlcnic_start_firmware(adapter)) {
			qlcnic_schedule_work(adapter, qlcnic_attach_work, 0);
			return;
		}
	case QLCNIC_DEV_FAILED:
		break;

	default:
		qlcnic_schedule_work(adapter,
			qlcnic_fwinit_work, 2 * FW_POLL_DELAY);
		return;
	}

err_ret:
	qlcnic_clr_all_drv_state(adapter);
}

static void
qlcnic_detach_work(struct work_struct *work)
{
	struct qlcnic_adapter *adapter = container_of(work,
			struct qlcnic_adapter, fw_work.work);
	struct net_device *netdev = adapter->netdev;
	u32 status;

	netif_device_detach(netdev);

	qlcnic_down(adapter, netdev);

	rtnl_lock();
	qlcnic_detach(adapter);
	rtnl_unlock();

	status = QLCRD32(adapter, QLCNIC_PEG_HALT_STATUS1);

	if (status & QLCNIC_RCODE_FATAL_ERROR)
		goto err_ret;

	if (adapter->temp == QLCNIC_TEMP_PANIC)
		goto err_ret;

	qlcnic_set_drv_state(adapter, adapter->dev_state);

	adapter->fw_wait_cnt = 0;

	qlcnic_schedule_work(adapter, qlcnic_fwinit_work, FW_POLL_DELAY);

	return;

err_ret:
	qlcnic_clr_all_drv_state(adapter);

}

static void
qlcnic_dev_request_reset(struct qlcnic_adapter *adapter)
{
	u32 state;

	if (qlcnic_api_lock(adapter))
		return;

	state = QLCRD32(adapter, QLCNIC_CRB_DEV_STATE);

	if (state != QLCNIC_DEV_INITALIZING && state != QLCNIC_DEV_NEED_RESET) {
		QLCWR32(adapter, QLCNIC_CRB_DEV_STATE, QLCNIC_DEV_NEED_RESET);
		set_bit(__QLCNIC_START_FW, &adapter->state);
	}

	qlcnic_api_unlock(adapter);
}

static void
qlcnic_schedule_work(struct qlcnic_adapter *adapter,
		work_func_t func, int delay)
{
	INIT_DELAYED_WORK(&adapter->fw_work, func);
	schedule_delayed_work(&adapter->fw_work, round_jiffies_relative(delay));
}

static void
qlcnic_cancel_fw_work(struct qlcnic_adapter *adapter)
{
	while (test_and_set_bit(__QLCNIC_RESETTING, &adapter->state))
		msleep(10);

	cancel_delayed_work_sync(&adapter->fw_work);
}

static void
qlcnic_attach_work(struct work_struct *work)
{
	struct qlcnic_adapter *adapter = container_of(work,
				struct qlcnic_adapter, fw_work.work);
	struct net_device *netdev = adapter->netdev;
	int err;

	if (netif_running(netdev)) {
		err = qlcnic_attach(adapter);
		if (err)
			goto done;

		err = qlcnic_up(adapter, netdev);
		if (err) {
			qlcnic_detach(adapter);
			goto done;
		}

		qlcnic_config_indev_addr(netdev, NETDEV_UP);
	}

	netif_device_attach(netdev);

done:
	adapter->fw_fail_cnt = 0;
	clear_bit(__QLCNIC_RESETTING, &adapter->state);

	if (!qlcnic_clr_drv_state(adapter))
		qlcnic_schedule_work(adapter, qlcnic_fw_poll_work,
							FW_POLL_DELAY);
}

static int
qlcnic_check_health(struct qlcnic_adapter *adapter)
{
	u32 state = 0, heartbit;
	struct net_device *netdev = adapter->netdev;

	if (qlcnic_check_temp(adapter))
		goto detach;

	if (adapter->need_fw_reset) {
		qlcnic_dev_request_reset(adapter);
		goto detach;
	}

	state = QLCRD32(adapter, QLCNIC_CRB_DEV_STATE);
	if (state == QLCNIC_DEV_NEED_RESET || state == QLCNIC_DEV_NEED_QUISCENT)
		adapter->need_fw_reset = 1;

	heartbit = QLCRD32(adapter, QLCNIC_PEG_ALIVE_COUNTER);
	if (heartbit != adapter->heartbit) {
		adapter->heartbit = heartbit;
		adapter->fw_fail_cnt = 0;
		if (adapter->need_fw_reset)
			goto detach;
		return 0;
	}

	if (++adapter->fw_fail_cnt < FW_FAIL_THRESH)
		return 0;

	qlcnic_dev_request_reset(adapter);

	clear_bit(__QLCNIC_FW_ATTACHED, &adapter->state);

	dev_info(&netdev->dev, "firmware hang detected\n");

detach:
	adapter->dev_state = (state == QLCNIC_DEV_NEED_QUISCENT) ? state :
		QLCNIC_DEV_NEED_RESET;

	if ((auto_fw_reset == AUTO_FW_RESET_ENABLED) &&
			!test_and_set_bit(__QLCNIC_RESETTING, &adapter->state))
		qlcnic_schedule_work(adapter, qlcnic_detach_work, 0);

	return 1;
}

static void
qlcnic_fw_poll_work(struct work_struct *work)
{
	struct qlcnic_adapter *adapter = container_of(work,
				struct qlcnic_adapter, fw_work.work);

	if (test_bit(__QLCNIC_RESETTING, &adapter->state))
		goto reschedule;


	if (qlcnic_check_health(adapter))
		return;

reschedule:
	qlcnic_schedule_work(adapter, qlcnic_fw_poll_work, FW_POLL_DELAY);
}

static ssize_t
qlcnic_store_bridged_mode(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	struct qlcnic_adapter *adapter = dev_get_drvdata(dev);
	unsigned long new;
	int ret = -EINVAL;

	if (!(adapter->capabilities & QLCNIC_FW_CAPABILITY_BDG))
		goto err_out;

	if (adapter->is_up != QLCNIC_ADAPTER_UP_MAGIC)
		goto err_out;

	if (strict_strtoul(buf, 2, &new))
		goto err_out;

	if (!qlcnic_config_bridged_mode(adapter, !!new))
		ret = len;

err_out:
	return ret;
}

static ssize_t
qlcnic_show_bridged_mode(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct qlcnic_adapter *adapter = dev_get_drvdata(dev);
	int bridged_mode = 0;

	if (adapter->capabilities & QLCNIC_FW_CAPABILITY_BDG)
		bridged_mode = !!(adapter->flags & QLCNIC_BRIDGE_ENABLED);

	return sprintf(buf, "%d\n", bridged_mode);
}

static struct device_attribute dev_attr_bridged_mode = {
       .attr = {.name = "bridged_mode", .mode = (S_IRUGO | S_IWUSR)},
       .show = qlcnic_show_bridged_mode,
       .store = qlcnic_store_bridged_mode,
};

static ssize_t
qlcnic_store_diag_mode(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	struct qlcnic_adapter *adapter = dev_get_drvdata(dev);
	unsigned long new;

	if (strict_strtoul(buf, 2, &new))
		return -EINVAL;

	if (!!new != !!(adapter->flags & QLCNIC_DIAG_ENABLED))
		adapter->flags ^= QLCNIC_DIAG_ENABLED;

	return len;
}

static ssize_t
qlcnic_show_diag_mode(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct qlcnic_adapter *adapter = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n",
			!!(adapter->flags & QLCNIC_DIAG_ENABLED));
}

static struct device_attribute dev_attr_diag_mode = {
	.attr = {.name = "diag_mode", .mode = (S_IRUGO | S_IWUSR)},
	.show = qlcnic_show_diag_mode,
	.store = qlcnic_store_diag_mode,
};

static int
qlcnic_sysfs_validate_crb(struct qlcnic_adapter *adapter,
		loff_t offset, size_t size)
{
	if (!(adapter->flags & QLCNIC_DIAG_ENABLED))
		return -EIO;

	if ((size != 4) || (offset & 0x3))
		return  -EINVAL;

	if (offset < QLCNIC_PCI_CRBSPACE)
		return -EINVAL;

	return 0;
}

static ssize_t
qlcnic_sysfs_read_crb(struct kobject *kobj, struct bin_attribute *attr,
		char *buf, loff_t offset, size_t size)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct qlcnic_adapter *adapter = dev_get_drvdata(dev);
	u32 data;
	int ret;

	ret = qlcnic_sysfs_validate_crb(adapter, offset, size);
	if (ret != 0)
		return ret;

	data = QLCRD32(adapter, offset);
	memcpy(buf, &data, size);
	return size;
}

static ssize_t
qlcnic_sysfs_write_crb(struct kobject *kobj, struct bin_attribute *attr,
		char *buf, loff_t offset, size_t size)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct qlcnic_adapter *adapter = dev_get_drvdata(dev);
	u32 data;
	int ret;

	ret = qlcnic_sysfs_validate_crb(adapter, offset, size);
	if (ret != 0)
		return ret;

	memcpy(&data, buf, size);
	QLCWR32(adapter, offset, data);
	return size;
}

static int
qlcnic_sysfs_validate_mem(struct qlcnic_adapter *adapter,
		loff_t offset, size_t size)
{
	if (!(adapter->flags & QLCNIC_DIAG_ENABLED))
		return -EIO;

	if ((size != 8) || (offset & 0x7))
		return  -EIO;

	return 0;
}

static ssize_t
qlcnic_sysfs_read_mem(struct kobject *kobj, struct bin_attribute *attr,
		char *buf, loff_t offset, size_t size)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct qlcnic_adapter *adapter = dev_get_drvdata(dev);
	u64 data;
	int ret;

	ret = qlcnic_sysfs_validate_mem(adapter, offset, size);
	if (ret != 0)
		return ret;

	if (qlcnic_pci_mem_read_2M(adapter, offset, &data))
		return -EIO;

	memcpy(buf, &data, size);

	return size;
}

static ssize_t
qlcnic_sysfs_write_mem(struct kobject *kobj, struct bin_attribute *attr,
		char *buf, loff_t offset, size_t size)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct qlcnic_adapter *adapter = dev_get_drvdata(dev);
	u64 data;
	int ret;

	ret = qlcnic_sysfs_validate_mem(adapter, offset, size);
	if (ret != 0)
		return ret;

	memcpy(&data, buf, size);

	if (qlcnic_pci_mem_write_2M(adapter, offset, data))
		return -EIO;

	return size;
}


static struct bin_attribute bin_attr_crb = {
	.attr = {.name = "crb", .mode = (S_IRUGO | S_IWUSR)},
	.size = 0,
	.read = qlcnic_sysfs_read_crb,
	.write = qlcnic_sysfs_write_crb,
};

static struct bin_attribute bin_attr_mem = {
	.attr = {.name = "mem", .mode = (S_IRUGO | S_IWUSR)},
	.size = 0,
	.read = qlcnic_sysfs_read_mem,
	.write = qlcnic_sysfs_write_mem,
};

static void
qlcnic_create_sysfs_entries(struct qlcnic_adapter *adapter)
{
	struct device *dev = &adapter->pdev->dev;

	if (adapter->capabilities & QLCNIC_FW_CAPABILITY_BDG)
		if (device_create_file(dev, &dev_attr_bridged_mode))
			dev_warn(dev,
				"failed to create bridged_mode sysfs entry\n");
}

static void
qlcnic_remove_sysfs_entries(struct qlcnic_adapter *adapter)
{
	struct device *dev = &adapter->pdev->dev;

	if (adapter->capabilities & QLCNIC_FW_CAPABILITY_BDG)
		device_remove_file(dev, &dev_attr_bridged_mode);
}

static void
qlcnic_create_diag_entries(struct qlcnic_adapter *adapter)
{
	struct device *dev = &adapter->pdev->dev;

	if (device_create_file(dev, &dev_attr_diag_mode))
		dev_info(dev, "failed to create diag_mode sysfs entry\n");
	if (device_create_bin_file(dev, &bin_attr_crb))
		dev_info(dev, "failed to create crb sysfs entry\n");
	if (device_create_bin_file(dev, &bin_attr_mem))
		dev_info(dev, "failed to create mem sysfs entry\n");
}


static void
qlcnic_remove_diag_entries(struct qlcnic_adapter *adapter)
{
	struct device *dev = &adapter->pdev->dev;

	device_remove_file(dev, &dev_attr_diag_mode);
	device_remove_bin_file(dev, &bin_attr_crb);
	device_remove_bin_file(dev, &bin_attr_mem);
}

#ifdef CONFIG_INET

#define is_qlcnic_netdev(dev) (dev->netdev_ops == &qlcnic_netdev_ops)

static int
qlcnic_destip_supported(struct qlcnic_adapter *adapter)
{
	if (adapter->ahw.cut_through)
		return 0;

	return 1;
}

static void
qlcnic_config_indev_addr(struct net_device *dev, unsigned long event)
{
	struct in_device *indev;
	struct qlcnic_adapter *adapter = netdev_priv(dev);

	if (!qlcnic_destip_supported(adapter))
		return;

	indev = in_dev_get(dev);
	if (!indev)
		return;

	for_ifa(indev) {
		switch (event) {
		case NETDEV_UP:
			qlcnic_config_ipaddr(adapter,
					ifa->ifa_address, QLCNIC_IP_UP);
			break;
		case NETDEV_DOWN:
			qlcnic_config_ipaddr(adapter,
					ifa->ifa_address, QLCNIC_IP_DOWN);
			break;
		default:
			break;
		}
	} endfor_ifa(indev);

	in_dev_put(indev);
	return;
}

static int qlcnic_netdev_event(struct notifier_block *this,
				 unsigned long event, void *ptr)
{
	struct qlcnic_adapter *adapter;
	struct net_device *dev = (struct net_device *)ptr;

recheck:
	if (dev == NULL)
		goto done;

	if (dev->priv_flags & IFF_802_1Q_VLAN) {
		dev = vlan_dev_real_dev(dev);
		goto recheck;
	}

	if (!is_qlcnic_netdev(dev))
		goto done;

	adapter = netdev_priv(dev);

	if (!adapter)
		goto done;

	if (adapter->is_up != QLCNIC_ADAPTER_UP_MAGIC)
		goto done;

	qlcnic_config_indev_addr(dev, event);
done:
	return NOTIFY_DONE;
}

static int
qlcnic_inetaddr_event(struct notifier_block *this,
		unsigned long event, void *ptr)
{
	struct qlcnic_adapter *adapter;
	struct net_device *dev;

	struct in_ifaddr *ifa = (struct in_ifaddr *)ptr;

	dev = ifa->ifa_dev ? ifa->ifa_dev->dev : NULL;

recheck:
	if (dev == NULL || !netif_running(dev))
		goto done;

	if (dev->priv_flags & IFF_802_1Q_VLAN) {
		dev = vlan_dev_real_dev(dev);
		goto recheck;
	}

	if (!is_qlcnic_netdev(dev))
		goto done;

	adapter = netdev_priv(dev);

	if (!adapter || !qlcnic_destip_supported(adapter))
		goto done;

	if (adapter->is_up != QLCNIC_ADAPTER_UP_MAGIC)
		goto done;

	switch (event) {
	case NETDEV_UP:
		qlcnic_config_ipaddr(adapter, ifa->ifa_address, QLCNIC_IP_UP);
		break;
	case NETDEV_DOWN:
		qlcnic_config_ipaddr(adapter, ifa->ifa_address, QLCNIC_IP_DOWN);
		break;
	default:
		break;
	}

done:
	return NOTIFY_DONE;
}

static struct notifier_block	qlcnic_netdev_cb = {
	.notifier_call = qlcnic_netdev_event,
};

static struct notifier_block qlcnic_inetaddr_cb = {
	.notifier_call = qlcnic_inetaddr_event,
};
#else
static void
qlcnic_config_indev_addr(struct net_device *dev, unsigned long event)
{ }
#endif

static struct pci_driver qlcnic_driver = {
	.name = qlcnic_driver_name,
	.id_table = qlcnic_pci_tbl,
	.probe = qlcnic_probe,
	.remove = __devexit_p(qlcnic_remove),
#ifdef CONFIG_PM
	.suspend = qlcnic_suspend,
	.resume = qlcnic_resume,
#endif
	.shutdown = qlcnic_shutdown
};

static int __init qlcnic_init_module(void)
{

	printk(KERN_INFO "%s\n", qlcnic_driver_string);

#ifdef CONFIG_INET
	register_netdevice_notifier(&qlcnic_netdev_cb);
	register_inetaddr_notifier(&qlcnic_inetaddr_cb);
#endif


	return pci_register_driver(&qlcnic_driver);
}

module_init(qlcnic_init_module);

static void __exit qlcnic_exit_module(void)
{

	pci_unregister_driver(&qlcnic_driver);

#ifdef CONFIG_INET
	unregister_inetaddr_notifier(&qlcnic_inetaddr_cb);
	unregister_netdevice_notifier(&qlcnic_netdev_cb);
#endif
}

module_exit(qlcnic_exit_module);
