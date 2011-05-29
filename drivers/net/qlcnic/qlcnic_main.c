/*
 * QLogic qlcnic NIC Driver
 * Copyright (c)  2009-2010 QLogic Corporation
 *
 * See LICENSE.qlcnic for copyright and licensing details.
 */

#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/interrupt.h>

#include "qlcnic.h"

#include <linux/swab.h>
#include <linux/dma-mapping.h>
#include <linux/if_vlan.h>
#include <net/ip.h>
#include <linux/ipv6.h>
#include <linux/inetdevice.h>
#include <linux/sysfs.h>
#include <linux/aer.h>

MODULE_DESCRIPTION("QLogic 1/10 GbE Converged/Intelligent Ethernet Driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(QLCNIC_LINUX_VERSIONID);
MODULE_FIRMWARE(QLCNIC_UNIFIED_ROMIMAGE_NAME);

char qlcnic_driver_name[] = "qlcnic";
static const char qlcnic_driver_string[] = "QLogic 1/10 GbE "
	"Converged/Intelligent Ethernet Driver v" QLCNIC_LINUX_VERSIONID;

static struct workqueue_struct *qlcnic_wq;
static int qlcnic_mac_learn;
module_param(qlcnic_mac_learn, int, 0444);
MODULE_PARM_DESC(qlcnic_mac_learn, "Mac Filter (0=disabled, 1=enabled)");

static int use_msi = 1;
module_param(use_msi, int, 0444);
MODULE_PARM_DESC(use_msi, "MSI interrupt (0=disabled, 1=enabled");

static int use_msi_x = 1;
module_param(use_msi_x, int, 0444);
MODULE_PARM_DESC(use_msi_x, "MSI-X interrupt (0=disabled, 1=enabled");

static int auto_fw_reset = 1;
module_param(auto_fw_reset, int, 0644);
MODULE_PARM_DESC(auto_fw_reset, "Auto firmware reset (0=disabled, 1=enabled");

static int load_fw_file;
module_param(load_fw_file, int, 0444);
MODULE_PARM_DESC(load_fw_file, "Load firmware from (0=flash, 1=file");

static int qlcnic_config_npars;
module_param(qlcnic_config_npars, int, 0444);
MODULE_PARM_DESC(qlcnic_config_npars, "Configure NPARs (0=disabled, 1=enabled");

static int __devinit qlcnic_probe(struct pci_dev *pdev,
		const struct pci_device_id *ent);
static void __devexit qlcnic_remove(struct pci_dev *pdev);
static int qlcnic_open(struct net_device *netdev);
static int qlcnic_close(struct net_device *netdev);
static void qlcnic_tx_timeout(struct net_device *netdev);
static void qlcnic_attach_work(struct work_struct *work);
static void qlcnic_fwinit_work(struct work_struct *work);
static void qlcnic_fw_poll_work(struct work_struct *work);
static void qlcnic_schedule_work(struct qlcnic_adapter *adapter,
		work_func_t func, int delay);
static void qlcnic_cancel_fw_work(struct qlcnic_adapter *adapter);
static int qlcnic_poll(struct napi_struct *napi, int budget);
static int qlcnic_rx_poll(struct napi_struct *napi, int budget);
#ifdef CONFIG_NET_POLL_CONTROLLER
static void qlcnic_poll_controller(struct net_device *netdev);
#endif

static void qlcnic_create_sysfs_entries(struct qlcnic_adapter *adapter);
static void qlcnic_remove_sysfs_entries(struct qlcnic_adapter *adapter);
static void qlcnic_create_diag_entries(struct qlcnic_adapter *adapter);
static void qlcnic_remove_diag_entries(struct qlcnic_adapter *adapter);

static void qlcnic_idc_debug_info(struct qlcnic_adapter *adapter, u8 encoding);
static void qlcnic_clr_all_drv_state(struct qlcnic_adapter *adapter, u8);
static int qlcnic_can_start_firmware(struct qlcnic_adapter *adapter);

static irqreturn_t qlcnic_tmp_intr(int irq, void *data);
static irqreturn_t qlcnic_intr(int irq, void *data);
static irqreturn_t qlcnic_msi_intr(int irq, void *data);
static irqreturn_t qlcnic_msix_intr(int irq, void *data);

static struct net_device_stats *qlcnic_get_stats(struct net_device *netdev);
static void qlcnic_restore_indev_addr(struct net_device *dev, unsigned long);
static int qlcnic_start_firmware(struct qlcnic_adapter *);

static void qlcnic_alloc_lb_filters_mem(struct qlcnic_adapter *adapter);
static void qlcnic_free_lb_filters_mem(struct qlcnic_adapter *adapter);
static void qlcnic_dev_set_npar_ready(struct qlcnic_adapter *);
static int qlcnicvf_config_led(struct qlcnic_adapter *, u32, u32);
static int qlcnicvf_config_bridged_mode(struct qlcnic_adapter *, u32);
static int qlcnicvf_start_firmware(struct qlcnic_adapter *);
static void qlcnic_set_netdev_features(struct qlcnic_adapter *,
				struct qlcnic_esw_func_cfg *);
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

	return recv_ctx->sds_rings == NULL;
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

		if (ring == adapter->max_sds_rings - 1)
			netif_napi_add(netdev, &sds_ring->napi, qlcnic_poll,
				QLCNIC_NETDEV_WEIGHT/adapter->max_sds_rings);
		else
			netif_napi_add(netdev, &sds_ring->napi,
				qlcnic_rx_poll, QLCNIC_NETDEV_WEIGHT*2);
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

	if (adapter->is_up != QLCNIC_ADAPTER_UP_MAGIC)
		return;

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

	if (adapter->is_up != QLCNIC_ADAPTER_UP_MAGIC)
		return;

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
	u8 mac_addr[ETH_ALEN];
	struct net_device *netdev = adapter->netdev;
	struct pci_dev *pdev = adapter->pdev;

	if (qlcnic_get_mac_address(adapter, mac_addr) != 0)
		return -EIO;

	memcpy(netdev->dev_addr, mac_addr, ETH_ALEN);
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

	if ((adapter->flags & QLCNIC_MAC_OVERRIDE_DISABLED))
		return -EOPNOTSUPP;

	if (!is_valid_ether_addr(addr->sa_data))
		return -EINVAL;

	if (test_bit(__QLCNIC_DEV_UP, &adapter->state)) {
		netif_device_detach(netdev);
		qlcnic_napi_disable(adapter);
	}

	memcpy(adapter->mac_addr, addr->sa_data, netdev->addr_len);
	memcpy(netdev->dev_addr, addr->sa_data, netdev->addr_len);
	qlcnic_set_multi(adapter->netdev);

	if (test_bit(__QLCNIC_DEV_UP, &adapter->state)) {
		netif_device_attach(netdev);
		qlcnic_napi_enable(adapter);
	}
	return 0;
}

static void qlcnic_vlan_rx_register(struct net_device *netdev,
		struct vlan_group *grp)
{
	struct qlcnic_adapter *adapter = netdev_priv(netdev);
	adapter->vlgrp = grp;
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
	.ndo_vlan_rx_register = qlcnic_vlan_rx_register,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller = qlcnic_poll_controller,
#endif
};

static struct qlcnic_nic_template qlcnic_ops = {
	.config_bridged_mode = qlcnic_config_bridged_mode,
	.config_led = qlcnic_config_led,
	.start_firmware = qlcnic_start_firmware
};

static struct qlcnic_nic_template qlcnic_vf_ops = {
	.config_bridged_mode = qlcnicvf_config_bridged_mode,
	.config_led = qlcnicvf_config_led,
	.start_firmware = qlcnicvf_start_firmware
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
qlcnic_init_pci_info(struct qlcnic_adapter *adapter)
{
	struct qlcnic_pci_info *pci_info;
	int i, ret = 0;
	u8 pfn;

	pci_info = kcalloc(QLCNIC_MAX_PCI_FUNC, sizeof(*pci_info), GFP_KERNEL);
	if (!pci_info)
		return -ENOMEM;

	adapter->npars = kzalloc(sizeof(struct qlcnic_npar_info) *
				QLCNIC_MAX_PCI_FUNC, GFP_KERNEL);
	if (!adapter->npars) {
		ret = -ENOMEM;
		goto err_pci_info;
	}

	adapter->eswitch = kzalloc(sizeof(struct qlcnic_eswitch) *
				QLCNIC_NIU_MAX_XG_PORTS, GFP_KERNEL);
	if (!adapter->eswitch) {
		ret = -ENOMEM;
		goto err_npars;
	}

	ret = qlcnic_get_pci_info(adapter, pci_info);
	if (ret)
		goto err_eswitch;

	for (i = 0; i < QLCNIC_MAX_PCI_FUNC; i++) {
		pfn = pci_info[i].id;
		if (pfn > QLCNIC_MAX_PCI_FUNC)
			return QL_STATUS_INVALID_PARAM;
		adapter->npars[pfn].active = (u8)pci_info[i].active;
		adapter->npars[pfn].type = (u8)pci_info[i].type;
		adapter->npars[pfn].phy_port = (u8)pci_info[i].default_port;
		adapter->npars[pfn].min_bw = pci_info[i].tx_min_bw;
		adapter->npars[pfn].max_bw = pci_info[i].tx_max_bw;
	}

	for (i = 0; i < QLCNIC_NIU_MAX_XG_PORTS; i++)
		adapter->eswitch[i].flags |= QLCNIC_SWITCH_ENABLE;

	kfree(pci_info);
	return 0;

err_eswitch:
	kfree(adapter->eswitch);
	adapter->eswitch = NULL;
err_npars:
	kfree(adapter->npars);
	adapter->npars = NULL;
err_pci_info:
	kfree(pci_info);

	return ret;
}

static int
qlcnic_set_function_modes(struct qlcnic_adapter *adapter)
{
	u8 id;
	u32 ref_count;
	int i, ret = 1;
	u32 data = QLCNIC_MGMT_FUNC;
	void __iomem *priv_op = adapter->ahw.pci_base0 + QLCNIC_DRV_OP_MODE;

	/* If other drivers are not in use set their privilege level */
	ref_count = QLCRD32(adapter, QLCNIC_CRB_DRV_ACTIVE);
	ret = qlcnic_api_lock(adapter);
	if (ret)
		goto err_lock;

	if (qlcnic_config_npars) {
		for (i = 0; i < QLCNIC_MAX_PCI_FUNC; i++) {
			id = i;
			if (adapter->npars[i].type != QLCNIC_TYPE_NIC ||
				id == adapter->ahw.pci_func)
				continue;
			data |= (qlcnic_config_npars &
					QLC_DEV_SET_DRV(0xf, id));
		}
	} else {
		data = readl(priv_op);
		data = (data & ~QLC_DEV_SET_DRV(0xf, adapter->ahw.pci_func)) |
			(QLC_DEV_SET_DRV(QLCNIC_MGMT_FUNC,
			adapter->ahw.pci_func));
	}
	writel(data, priv_op);
	qlcnic_api_unlock(adapter);
err_lock:
	return ret;
}

static void
qlcnic_check_vf(struct qlcnic_adapter *adapter)
{
	void __iomem *msix_base_addr;
	void __iomem *priv_op;
	u32 func;
	u32 msix_base;
	u32 op_mode, priv_level;

	/* Determine FW API version */
	adapter->fw_hal_version = readl(adapter->ahw.pci_base0 + QLCNIC_FW_API);

	/* Find PCI function number */
	pci_read_config_dword(adapter->pdev, QLCNIC_MSIX_TABLE_OFFSET, &func);
	msix_base_addr = adapter->ahw.pci_base0 + QLCNIC_MSIX_BASE;
	msix_base = readl(msix_base_addr);
	func = (func - msix_base)/QLCNIC_MSIX_TBL_PGSIZE;
	adapter->ahw.pci_func = func;

	/* Determine function privilege level */
	priv_op = adapter->ahw.pci_base0 + QLCNIC_DRV_OP_MODE;
	op_mode = readl(priv_op);
	if (op_mode == QLC_DEV_DRV_DEFAULT)
		priv_level = QLCNIC_MGMT_FUNC;
	else
		priv_level = QLC_DEV_GET_DRV(op_mode, adapter->ahw.pci_func);

	if (priv_level == QLCNIC_NON_PRIV_FUNC) {
		adapter->op_mode = QLCNIC_NON_PRIV_FUNC;
		dev_info(&adapter->pdev->dev,
			"HAL Version: %d Non Privileged function\n",
			adapter->fw_hal_version);
		adapter->nic_ops = &qlcnic_vf_ops;
	} else
		adapter->nic_ops = &qlcnic_ops;
}

static int
qlcnic_setup_pci_map(struct qlcnic_adapter *adapter)
{
	void __iomem *mem_ptr0 = NULL;
	resource_size_t mem_base;
	unsigned long mem_len, pci_len0 = 0;

	struct pci_dev *pdev = adapter->pdev;

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

	qlcnic_check_vf(adapter);

	adapter->ahw.ocm_win_crb = qlcnic_get_ioaddr(adapter,
		QLCNIC_PCIX_PS_REG(PCIX_OCM_WINDOW_REG(adapter->ahw.pci_func)));

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
				sprintf(name, "%pM: %s" ,
					adapter->mac_addr,
					qlcnic_boards[i].short_name);
				found = 1;
				break;
		}

	}

	if (!found)
		sprintf(name, "%pM Gigabit Ethernet", adapter->mac_addr);
}

static void
qlcnic_check_options(struct qlcnic_adapter *adapter)
{
	u32 fw_major, fw_minor, fw_build;
	struct pci_dev *pdev = adapter->pdev;

	fw_major = QLCRD32(adapter, QLCNIC_FW_VERSION_MAJOR);
	fw_minor = QLCRD32(adapter, QLCNIC_FW_VERSION_MINOR);
	fw_build = QLCRD32(adapter, QLCNIC_FW_VERSION_SUB);

	adapter->fw_version = QLCNIC_VERSION_CODE(fw_major, fw_minor, fw_build);

	dev_info(&pdev->dev, "firmware v%d.%d.%d\n",
			fw_major, fw_minor, fw_build);
	if (adapter->ahw.port_type == QLCNIC_XGBE) {
		if (adapter->flags & QLCNIC_ESWITCH_ENABLED) {
			adapter->num_rxd = DEFAULT_RCV_DESCRIPTORS_VF;
			adapter->max_rxd = MAX_RCV_DESCRIPTORS_VF;
		} else {
			adapter->num_rxd = DEFAULT_RCV_DESCRIPTORS_10G;
			adapter->max_rxd = MAX_RCV_DESCRIPTORS_10G;
		}

		adapter->num_jumbo_rxd = MAX_JUMBO_RCV_DESCRIPTORS_10G;
		adapter->max_jumbo_rxd = MAX_JUMBO_RCV_DESCRIPTORS_10G;

	} else if (adapter->ahw.port_type == QLCNIC_GBE) {
		adapter->num_rxd = DEFAULT_RCV_DESCRIPTORS_1G;
		adapter->num_jumbo_rxd = MAX_JUMBO_RCV_DESCRIPTORS_1G;
		adapter->max_jumbo_rxd = MAX_JUMBO_RCV_DESCRIPTORS_1G;
		adapter->max_rxd = MAX_RCV_DESCRIPTORS_1G;
	}

	adapter->msix_supported = !!use_msi_x;
	adapter->rss_supported = !!use_msi_x;

	adapter->num_txd = MAX_CMD_DESCRIPTORS;

	adapter->max_rds_rings = MAX_RDS_RINGS;
}

static int
qlcnic_initialize_nic(struct qlcnic_adapter *adapter)
{
	int err;
	struct qlcnic_info nic_info;

	err = qlcnic_get_nic_info(adapter, &nic_info, adapter->ahw.pci_func);
	if (err)
		return err;

	adapter->physical_port = (u8)nic_info.phys_port;
	adapter->switch_mode = nic_info.switch_mode;
	adapter->max_tx_ques = nic_info.max_tx_ques;
	adapter->max_rx_ques = nic_info.max_rx_ques;
	adapter->capabilities = nic_info.capabilities;
	adapter->max_mac_filters = nic_info.max_mac_filters;
	adapter->max_mtu = nic_info.max_mtu;

	if (adapter->capabilities & BIT_6)
		adapter->flags |= QLCNIC_ESWITCH_ENABLED;
	else
		adapter->flags &= ~QLCNIC_ESWITCH_ENABLED;

	return err;
}

static void
qlcnic_set_vlan_config(struct qlcnic_adapter *adapter,
		struct qlcnic_esw_func_cfg *esw_cfg)
{
	if (esw_cfg->discard_tagged)
		adapter->flags &= ~QLCNIC_TAGGING_ENABLED;
	else
		adapter->flags |= QLCNIC_TAGGING_ENABLED;

	if (esw_cfg->vlan_id)
		adapter->pvid = esw_cfg->vlan_id;
	else
		adapter->pvid = 0;
}

static void
qlcnic_set_eswitch_port_features(struct qlcnic_adapter *adapter,
		struct qlcnic_esw_func_cfg *esw_cfg)
{
	adapter->flags &= ~(QLCNIC_MACSPOOF | QLCNIC_MAC_OVERRIDE_DISABLED |
				QLCNIC_PROMISC_DISABLED);

	if (esw_cfg->mac_anti_spoof)
		adapter->flags |= QLCNIC_MACSPOOF;

	if (!esw_cfg->mac_override)
		adapter->flags |= QLCNIC_MAC_OVERRIDE_DISABLED;

	if (!esw_cfg->promisc_mode)
		adapter->flags |= QLCNIC_PROMISC_DISABLED;

	qlcnic_set_netdev_features(adapter, esw_cfg);
}

static int
qlcnic_set_eswitch_port_config(struct qlcnic_adapter *adapter)
{
	struct qlcnic_esw_func_cfg esw_cfg;

	if (!(adapter->flags & QLCNIC_ESWITCH_ENABLED))
		return 0;

	esw_cfg.pci_func = adapter->ahw.pci_func;
	if (qlcnic_get_eswitch_port_config(adapter, &esw_cfg))
			return -EIO;
	qlcnic_set_vlan_config(adapter, &esw_cfg);
	qlcnic_set_eswitch_port_features(adapter, &esw_cfg);

	return 0;
}

static void
qlcnic_set_netdev_features(struct qlcnic_adapter *adapter,
		struct qlcnic_esw_func_cfg *esw_cfg)
{
	struct net_device *netdev = adapter->netdev;
	unsigned long features, vlan_features;

	features = (NETIF_F_SG | NETIF_F_IP_CSUM |
			NETIF_F_IPV6_CSUM | NETIF_F_GRO);
	vlan_features = (NETIF_F_SG | NETIF_F_IP_CSUM |
			NETIF_F_IPV6_CSUM);

	if (adapter->capabilities & QLCNIC_FW_CAPABILITY_TSO) {
		features |= (NETIF_F_TSO | NETIF_F_TSO6);
		vlan_features |= (NETIF_F_TSO | NETIF_F_TSO6);
	}
	if (adapter->capabilities & QLCNIC_FW_CAPABILITY_HW_LRO)
		features |= NETIF_F_LRO;

	if (esw_cfg->offload_flags & BIT_0) {
		netdev->features |= features;
		adapter->rx_csum = 1;
		if (!(esw_cfg->offload_flags & BIT_1))
			netdev->features &= ~NETIF_F_TSO;
		if (!(esw_cfg->offload_flags & BIT_2))
			netdev->features &= ~NETIF_F_TSO6;
	} else {
		netdev->features &= ~features;
		adapter->rx_csum = 0;
	}

	netdev->vlan_features = (features & vlan_features);
}

static int
qlcnic_check_eswitch_mode(struct qlcnic_adapter *adapter)
{
	void __iomem *priv_op;
	u32 op_mode, priv_level;
	int err = 0;

	err = qlcnic_initialize_nic(adapter);
	if (err)
		return err;

	if (adapter->flags & QLCNIC_ADAPTER_INITIALIZED)
		return 0;

	priv_op = adapter->ahw.pci_base0 + QLCNIC_DRV_OP_MODE;
	op_mode = readl(priv_op);
	priv_level = QLC_DEV_GET_DRV(op_mode, adapter->ahw.pci_func);

	if (op_mode == QLC_DEV_DRV_DEFAULT)
		priv_level = QLCNIC_MGMT_FUNC;
	else
		priv_level = QLC_DEV_GET_DRV(op_mode, adapter->ahw.pci_func);

	if (adapter->flags & QLCNIC_ESWITCH_ENABLED) {
		if (priv_level == QLCNIC_MGMT_FUNC) {
			adapter->op_mode = QLCNIC_MGMT_FUNC;
			err = qlcnic_init_pci_info(adapter);
			if (err)
				return err;
			/* Set privilege level for other functions */
			qlcnic_set_function_modes(adapter);
			dev_info(&adapter->pdev->dev,
				"HAL Version: %d, Management function\n",
				adapter->fw_hal_version);
		} else if (priv_level == QLCNIC_PRIV_FUNC) {
			adapter->op_mode = QLCNIC_PRIV_FUNC;
			dev_info(&adapter->pdev->dev,
				"HAL Version: %d, Privileged function\n",
				adapter->fw_hal_version);
		}
	}

	adapter->flags |= QLCNIC_ADAPTER_INITIALIZED;

	return err;
}

static int
qlcnic_set_default_offload_settings(struct qlcnic_adapter *adapter)
{
	struct qlcnic_esw_func_cfg esw_cfg;
	struct qlcnic_npar_info *npar;
	u8 i;

	if (adapter->need_fw_reset)
		return 0;

	for (i = 0; i < QLCNIC_MAX_PCI_FUNC; i++) {
		if (adapter->npars[i].type != QLCNIC_TYPE_NIC)
			continue;
		memset(&esw_cfg, 0, sizeof(struct qlcnic_esw_func_cfg));
		esw_cfg.pci_func = i;
		esw_cfg.offload_flags = BIT_0;
		esw_cfg.mac_override = BIT_0;
		esw_cfg.promisc_mode = BIT_0;
		if (adapter->capabilities  & QLCNIC_FW_CAPABILITY_TSO)
			esw_cfg.offload_flags |= (BIT_1 | BIT_2);
		if (qlcnic_config_switch_port(adapter, &esw_cfg))
			return -EIO;
		npar = &adapter->npars[i];
		npar->pvid = esw_cfg.vlan_id;
		npar->mac_override = esw_cfg.mac_override;
		npar->mac_anti_spoof = esw_cfg.mac_anti_spoof;
		npar->discard_tagged = esw_cfg.discard_tagged;
		npar->promisc_mode = esw_cfg.promisc_mode;
		npar->offload_flags = esw_cfg.offload_flags;
	}

	return 0;
}

static int
qlcnic_reset_eswitch_config(struct qlcnic_adapter *adapter,
			struct qlcnic_npar_info *npar, int pci_func)
{
	struct qlcnic_esw_func_cfg esw_cfg;
	esw_cfg.op_mode = QLCNIC_PORT_DEFAULTS;
	esw_cfg.pci_func = pci_func;
	esw_cfg.vlan_id = npar->pvid;
	esw_cfg.mac_override = npar->mac_override;
	esw_cfg.discard_tagged = npar->discard_tagged;
	esw_cfg.mac_anti_spoof = npar->mac_anti_spoof;
	esw_cfg.offload_flags = npar->offload_flags;
	esw_cfg.promisc_mode = npar->promisc_mode;
	if (qlcnic_config_switch_port(adapter, &esw_cfg))
		return -EIO;

	esw_cfg.op_mode = QLCNIC_ADD_VLAN;
	if (qlcnic_config_switch_port(adapter, &esw_cfg))
		return -EIO;

	return 0;
}

static int
qlcnic_reset_npar_config(struct qlcnic_adapter *adapter)
{
	int i, err;
	struct qlcnic_npar_info *npar;
	struct qlcnic_info nic_info;

	if (!adapter->need_fw_reset)
		return 0;

	/* Set the NPAR config data after FW reset */
	for (i = 0; i < QLCNIC_MAX_PCI_FUNC; i++) {
		npar = &adapter->npars[i];
		if (npar->type != QLCNIC_TYPE_NIC)
			continue;
		err = qlcnic_get_nic_info(adapter, &nic_info, i);
		if (err)
			return err;
		nic_info.min_tx_bw = npar->min_bw;
		nic_info.max_tx_bw = npar->max_bw;
		err = qlcnic_set_nic_info(adapter, &nic_info);
		if (err)
			return err;

		if (npar->enable_pm) {
			err = qlcnic_config_port_mirroring(adapter,
							npar->dest_npar, 1, i);
			if (err)
				return err;
		}
		err = qlcnic_reset_eswitch_config(adapter, npar, i);
		if (err)
			return err;
	}
	return 0;
}

static int qlcnic_check_npar_opertional(struct qlcnic_adapter *adapter)
{
	u8 npar_opt_timeo = QLCNIC_DEV_NPAR_OPER_TIMEO;
	u32 npar_state;

	if (adapter->op_mode == QLCNIC_MGMT_FUNC)
		return 0;

	npar_state = QLCRD32(adapter, QLCNIC_CRB_DEV_NPAR_STATE);
	while (npar_state != QLCNIC_DEV_NPAR_OPER && --npar_opt_timeo) {
		msleep(1000);
		npar_state = QLCRD32(adapter, QLCNIC_CRB_DEV_NPAR_STATE);
	}
	if (!npar_opt_timeo) {
		dev_err(&adapter->pdev->dev,
			"Waiting for NPAR state to opertional timeout\n");
		return -EIO;
	}
	return 0;
}

static int
qlcnic_set_mgmt_operations(struct qlcnic_adapter *adapter)
{
	int err;

	if (!(adapter->flags & QLCNIC_ESWITCH_ENABLED) ||
		    adapter->op_mode != QLCNIC_MGMT_FUNC)
		return 0;

	err = qlcnic_set_default_offload_settings(adapter);
	if (err)
		return err;

	err = qlcnic_reset_npar_config(adapter);
	if (err)
		return err;

	qlcnic_dev_set_npar_ready(adapter);

	return err;
}

static int
qlcnic_start_firmware(struct qlcnic_adapter *adapter)
{
	int err;

	err = qlcnic_can_start_firmware(adapter);
	if (err < 0)
		return err;
	else if (!err)
		goto check_fw_status;

	if (load_fw_file)
		qlcnic_request_firmware(adapter);
	else {
		err = qlcnic_check_flash_fw_ver(adapter);
		if (err)
			goto err_out;

		adapter->fw_type = QLCNIC_FLASH_ROMIMAGE;
	}

	err = qlcnic_need_fw_reset(adapter);
	if (err == 0)
		goto check_fw_status;

	err = qlcnic_pinit_from_rom(adapter);
	if (err)
		goto err_out;

	err = qlcnic_load_firmware(adapter);
	if (err)
		goto err_out;

	qlcnic_release_firmware(adapter);
	QLCWR32(adapter, CRB_DRIVER_VERSION, QLCNIC_DRIVER_VERSION);

check_fw_status:
	err = qlcnic_check_fw_status(adapter);
	if (err)
		goto err_out;

	QLCWR32(adapter, QLCNIC_CRB_DEV_STATE, QLCNIC_DEV_READY);
	qlcnic_idc_debug_info(adapter, 1);

	err = qlcnic_check_eswitch_mode(adapter);
	if (err) {
		dev_err(&adapter->pdev->dev,
			"Memory allocation failed for eswitch\n");
		goto err_out;
	}
	err = qlcnic_set_mgmt_operations(adapter);
	if (err)
		goto err_out;

	qlcnic_check_options(adapter);
	adapter->need_fw_reset = 0;

	qlcnic_release_firmware(adapter);
	return 0;

err_out:
	QLCWR32(adapter, QLCNIC_CRB_DEV_STATE, QLCNIC_DEV_FAILED);
	dev_err(&adapter->pdev->dev, "Device state set to failed\n");

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
	int ring;
	struct qlcnic_host_rds_ring *rds_ring;

	if (adapter->is_up != QLCNIC_ADAPTER_UP_MAGIC)
		return -EIO;

	if (test_bit(__QLCNIC_DEV_UP, &adapter->state))
		return 0;
	if (qlcnic_set_eswitch_port_config(adapter))
		return -EIO;

	if (qlcnic_fw_create_ctx(adapter))
		return -EIO;

	for (ring = 0; ring < adapter->max_rds_rings; ring++) {
		rds_ring = &adapter->recv_ctx.rds_rings[ring];
		qlcnic_post_rx_buffers(adapter, ring, rds_ring);
	}

	qlcnic_set_multi(netdev);
	qlcnic_fw_cmd_set_mtu(adapter, netdev->mtu);

	adapter->ahw.linkup = 0;

	if (adapter->max_sds_rings > 1)
		qlcnic_config_rss(adapter, 1);

	qlcnic_config_intr_coalesce(adapter);

	if (netdev->features & NETIF_F_LRO)
		qlcnic_config_hw_lro(adapter, QLCNIC_LRO_ENABLED);

	qlcnic_napi_enable(adapter);

	qlcnic_linkevent_request(adapter, 1);

	adapter->reset_context = 0;
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

	if (adapter->fhash.fnum)
		qlcnic_delete_lb_filters(adapter);

	qlcnic_nic_set_promisc(adapter, QLCNIC_NIU_NON_PROMISC_MODE);

	qlcnic_napi_disable(adapter);

	qlcnic_fw_destroy_ctx(adapter);

	qlcnic_reset_rx_buffers_list(adapter);
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
	int err;

	if (adapter->is_up == QLCNIC_ADAPTER_UP_MAGIC)
		return 0;

	err = qlcnic_napi_add(adapter, netdev);
	if (err)
		return err;

	err = qlcnic_alloc_sw_resources(adapter);
	if (err) {
		dev_err(&pdev->dev, "Error in setting sw resources\n");
		goto err_out_napi_del;
	}

	err = qlcnic_alloc_hw_resources(adapter);
	if (err) {
		dev_err(&pdev->dev, "Error in setting hw resources\n");
		goto err_out_free_sw;
	}

	err = qlcnic_request_irq(adapter);
	if (err) {
		dev_err(&pdev->dev, "failed to setup interrupt\n");
		goto err_out_free_hw;
	}

	qlcnic_init_coalesce_defaults(adapter);

	qlcnic_create_sysfs_entries(adapter);

	adapter->is_up = QLCNIC_ADAPTER_UP_MAGIC;
	return 0;

err_out_free_hw:
	qlcnic_free_hw_resources(adapter);
err_out_free_sw:
	qlcnic_free_sw_resources(adapter);
err_out_napi_del:
	qlcnic_napi_del(adapter);
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

	clear_bit(__QLCNIC_DEV_UP, &adapter->state);
	if (adapter->diag_test == QLCNIC_INTERRUPT_TEST) {
		for (ring = 0; ring < adapter->max_sds_rings; ring++) {
			sds_ring = &adapter->recv_ctx.sds_rings[ring];
			qlcnic_disable_int(sds_ring);
		}
	}

	qlcnic_fw_destroy_ctx(adapter);

	qlcnic_detach(adapter);

	adapter->diag_test = 0;
	adapter->max_sds_rings = max_sds_rings;

	if (qlcnic_attach(adapter))
		goto out;

	if (netif_running(netdev))
		__qlcnic_up(adapter, netdev);
out:
	netif_device_attach(netdev);
}

int qlcnic_diag_alloc_res(struct net_device *netdev, int test)
{
	struct qlcnic_adapter *adapter = netdev_priv(netdev);
	struct qlcnic_host_sds_ring *sds_ring;
	struct qlcnic_host_rds_ring *rds_ring;
	int ring;
	int ret;

	netif_device_detach(netdev);

	if (netif_running(netdev))
		__qlcnic_down(adapter, netdev);

	qlcnic_detach(adapter);

	adapter->max_sds_rings = 1;
	adapter->diag_test = test;

	ret = qlcnic_attach(adapter);
	if (ret) {
		netif_device_attach(netdev);
		return ret;
	}

	ret = qlcnic_fw_create_ctx(adapter);
	if (ret) {
		qlcnic_detach(adapter);
		netif_device_attach(netdev);
		return ret;
	}

	for (ring = 0; ring < adapter->max_rds_rings; ring++) {
		rds_ring = &adapter->recv_ctx.rds_rings[ring];
		qlcnic_post_rx_buffers(adapter, ring, rds_ring);
	}

	if (adapter->diag_test == QLCNIC_INTERRUPT_TEST) {
		for (ring = 0; ring < adapter->max_sds_rings; ring++) {
			sds_ring = &adapter->recv_ctx.sds_rings[ring];
			qlcnic_enable_int(sds_ring);
		}
	}
	set_bit(__QLCNIC_DEV_UP, &adapter->state);

	return 0;
}

/* Reset context in hardware only */
static int
qlcnic_reset_hw_context(struct qlcnic_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;

	if (test_and_set_bit(__QLCNIC_RESETTING, &adapter->state))
		return -EBUSY;

	netif_device_detach(netdev);

	qlcnic_down(adapter, netdev);

	qlcnic_up(adapter, netdev);

	netif_device_attach(netdev);

	clear_bit(__QLCNIC_RESETTING, &adapter->state);
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
				__qlcnic_up(adapter, netdev);
		}

		netif_device_attach(netdev);
	}

	clear_bit(__QLCNIC_RESETTING, &adapter->state);
	return err;
}

static int
qlcnic_setup_netdev(struct qlcnic_adapter *adapter,
		struct net_device *netdev, u8 pci_using_dac)
{
	int err;
	struct pci_dev *pdev = adapter->pdev;

	adapter->rx_csum = 1;
	adapter->mc_enabled = 0;
	adapter->max_mc_count = 38;

	netdev->netdev_ops	   = &qlcnic_netdev_ops;
	netdev->watchdog_timeo     = 5*HZ;

	qlcnic_change_mtu(netdev, netdev->mtu);

	SET_ETHTOOL_OPS(netdev, &qlcnic_ethtool_ops);

	netdev->features |= (NETIF_F_SG | NETIF_F_IP_CSUM |
		NETIF_F_IPV6_CSUM | NETIF_F_GRO | NETIF_F_HW_VLAN_RX);
	netdev->vlan_features |= (NETIF_F_SG | NETIF_F_IP_CSUM |
		NETIF_F_IPV6_CSUM);

	if (adapter->capabilities & QLCNIC_FW_CAPABILITY_TSO) {
		netdev->features |= (NETIF_F_TSO | NETIF_F_TSO6);
		netdev->vlan_features |= (NETIF_F_TSO | NETIF_F_TSO6);
	}

	if (pci_using_dac) {
		netdev->features |= NETIF_F_HIGHDMA;
		netdev->vlan_features |= NETIF_F_HIGHDMA;
	}

	if (adapter->capabilities & QLCNIC_FW_CAPABILITY_FVLANTX)
		netdev->features |= (NETIF_F_HW_VLAN_TX);

	if (adapter->capabilities & QLCNIC_FW_CAPABILITY_HW_LRO)
		netdev->features |= NETIF_F_LRO;
	netdev->irq = adapter->msix_entries[0].vector;

	netif_carrier_off(netdev);

	err = register_netdev(netdev);
	if (err) {
		dev_err(&pdev->dev, "failed to register net device\n");
		return err;
	}

	return 0;
}

static int qlcnic_set_dma_mask(struct pci_dev *pdev, u8 *pci_using_dac)
{
	if (!pci_set_dma_mask(pdev, DMA_BIT_MASK(64)) &&
			!pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(64)))
		*pci_using_dac = 1;
	else if (!pci_set_dma_mask(pdev, DMA_BIT_MASK(32)) &&
			!pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(32)))
		*pci_using_dac = 0;
	else {
		dev_err(&pdev->dev, "Unable to set DMA mask, aborting\n");
		return -EIO;
	}

	return 0;
}

static int __devinit
qlcnic_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct net_device *netdev = NULL;
	struct qlcnic_adapter *adapter = NULL;
	int err;
	uint8_t revision_id;
	uint8_t pci_using_dac;
	char brd_name[QLCNIC_MAX_BOARD_NAME_LEN];

	err = pci_enable_device(pdev);
	if (err)
		return err;

	if (!(pci_resource_flags(pdev, 0) & IORESOURCE_MEM)) {
		err = -ENODEV;
		goto err_out_disable_pdev;
	}

	err = qlcnic_set_dma_mask(pdev, &pci_using_dac);
	if (err)
		goto err_out_disable_pdev;

	err = pci_request_regions(pdev, qlcnic_driver_name);
	if (err)
		goto err_out_disable_pdev;

	pci_set_master(pdev);
	pci_enable_pcie_error_reporting(pdev);

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
	adapter->dev_rst_time = jiffies;

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
	adapter->portnum = adapter->ahw.pci_func;

	err = qlcnic_get_board_info(adapter);
	if (err) {
		dev_err(&pdev->dev, "Error getting board config info.\n");
		goto err_out_iounmap;
	}

	err = qlcnic_setup_idc_param(adapter);
	if (err)
		goto err_out_iounmap;

	adapter->flags |= QLCNIC_NEED_FLR;

	err = adapter->nic_ops->start_firmware(adapter);
	if (err) {
		dev_err(&pdev->dev, "Loading fw failed.Please Reboot\n");
		goto err_out_decr_ref;
	}

	if (qlcnic_read_mac_addr(adapter))
		dev_warn(&pdev->dev, "failed to read mac addr\n");

	if (adapter->portnum == 0) {
		get_brd_name(adapter, brd_name);

		pr_info("%s: %s Board Chip rev 0x%x\n",
				module_name(THIS_MODULE),
				brd_name, adapter->ahw.revision_id);
	}

	qlcnic_clear_stats(adapter);

	qlcnic_setup_intr(adapter);

	err = qlcnic_setup_netdev(adapter, netdev, pci_using_dac);
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

	qlcnic_alloc_lb_filters_mem(adapter);
	qlcnic_create_diag_entries(adapter);

	return 0;

err_out_disable_msi:
	qlcnic_teardown_intr(adapter);

err_out_decr_ref:
	qlcnic_clr_all_drv_state(adapter, 0);

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

	qlcnic_detach(adapter);

	if (adapter->npars != NULL)
		kfree(adapter->npars);
	if (adapter->eswitch != NULL)
		kfree(adapter->eswitch);

	qlcnic_clr_all_drv_state(adapter, 0);

	clear_bit(__QLCNIC_RESETTING, &adapter->state);

	qlcnic_free_lb_filters_mem(adapter);

	qlcnic_teardown_intr(adapter);

	qlcnic_remove_diag_entries(adapter);

	qlcnic_cleanup_pci_map(adapter);

	qlcnic_release_firmware(adapter);

	pci_disable_pcie_error_reporting(pdev);
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

	qlcnic_clr_all_drv_state(adapter, 0);

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

	err = adapter->nic_ops->start_firmware(adapter);
	if (err) {
		dev_err(&pdev->dev, "failed to start firmware\n");
		return err;
	}

	if (netif_running(netdev)) {
		err = qlcnic_up(adapter, netdev);
		if (err)
			goto done;

		qlcnic_restore_indev_addr(netdev, NETDEV_UP);
	}
done:
	netif_device_attach(netdev);
	qlcnic_schedule_work(adapter, qlcnic_fw_poll_work, FW_POLL_DELAY);
	return 0;
}
#endif

static int qlcnic_open(struct net_device *netdev)
{
	struct qlcnic_adapter *adapter = netdev_priv(netdev);
	int err;

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
qlcnic_alloc_lb_filters_mem(struct qlcnic_adapter *adapter)
{
	void *head;
	int i;

	if (!qlcnic_mac_learn)
		return;

	spin_lock_init(&adapter->mac_learn_lock);

	head = kcalloc(QLCNIC_LB_MAX_FILTERS, sizeof(struct hlist_head),
								GFP_KERNEL);
	if (!head)
		return;

	adapter->fhash.fmax = QLCNIC_LB_MAX_FILTERS;
	adapter->fhash.fhead = (struct hlist_head *)head;

	for (i = 0; i < adapter->fhash.fmax; i++)
		INIT_HLIST_HEAD(&adapter->fhash.fhead[i]);
}

static void qlcnic_free_lb_filters_mem(struct qlcnic_adapter *adapter)
{
	if (adapter->fhash.fmax && adapter->fhash.fhead)
		kfree(adapter->fhash.fhead);

	adapter->fhash.fhead = NULL;
	adapter->fhash.fmax = 0;
}

static void qlcnic_change_filter(struct qlcnic_adapter *adapter,
		u64 uaddr, __le16 vlan_id, struct qlcnic_host_tx_ring *tx_ring)
{
	struct cmd_desc_type0 *hwdesc;
	struct qlcnic_nic_req *req;
	struct qlcnic_mac_req *mac_req;
	struct qlcnic_vlan_req *vlan_req;
	u32 producer;
	u64 word;

	producer = tx_ring->producer;
	hwdesc = &tx_ring->desc_head[tx_ring->producer];

	req = (struct qlcnic_nic_req *)hwdesc;
	memset(req, 0, sizeof(struct qlcnic_nic_req));
	req->qhdr = cpu_to_le64(QLCNIC_REQUEST << 23);

	word = QLCNIC_MAC_EVENT | ((u64)(adapter->portnum) << 16);
	req->req_hdr = cpu_to_le64(word);

	mac_req = (struct qlcnic_mac_req *)&(req->words[0]);
	mac_req->op = vlan_id ? QLCNIC_MAC_VLAN_ADD : QLCNIC_MAC_ADD;
	memcpy(mac_req->mac_addr, &uaddr, ETH_ALEN);

	vlan_req = (struct qlcnic_vlan_req *)&req->words[1];
	vlan_req->vlan_id = vlan_id;

	tx_ring->producer = get_next_index(producer, tx_ring->num_desc);
}

#define QLCNIC_MAC_HASH(MAC)\
	((((MAC) & 0x70000) >> 0x10) | (((MAC) & 0x70000000000ULL) >> 0x25))

static void
qlcnic_send_filter(struct qlcnic_adapter *adapter,
		struct qlcnic_host_tx_ring *tx_ring,
		struct cmd_desc_type0 *first_desc,
		struct sk_buff *skb)
{
	struct ethhdr *phdr = (struct ethhdr *)(skb->data);
	struct qlcnic_filter *fil, *tmp_fil;
	struct hlist_node *tmp_hnode, *n;
	struct hlist_head *head;
	u64 src_addr = 0;
	__le16 vlan_id = 0;
	u8 hindex;

	if (!compare_ether_addr(phdr->h_source, adapter->mac_addr))
		return;

	if (adapter->fhash.fnum >= adapter->fhash.fmax)
		return;

	/* Only NPAR capable devices support vlan based learning*/
	if (adapter->flags & QLCNIC_ESWITCH_ENABLED)
		vlan_id = first_desc->vlan_TCI;
	memcpy(&src_addr, phdr->h_source, ETH_ALEN);
	hindex = QLCNIC_MAC_HASH(src_addr) & (QLCNIC_LB_MAX_FILTERS - 1);
	head = &(adapter->fhash.fhead[hindex]);

	hlist_for_each_entry_safe(tmp_fil, tmp_hnode, n, head, fnode) {
		if (!memcmp(tmp_fil->faddr, &src_addr, ETH_ALEN) &&
			    tmp_fil->vlan_id == vlan_id) {

			if (jiffies >
			    (QLCNIC_READD_AGE * HZ + tmp_fil->ftime))
				qlcnic_change_filter(adapter, src_addr, vlan_id,
								tx_ring);
			tmp_fil->ftime = jiffies;
			return;
		}
	}

	fil = kzalloc(sizeof(struct qlcnic_filter), GFP_ATOMIC);
	if (!fil)
		return;

	qlcnic_change_filter(adapter, src_addr, vlan_id, tx_ring);

	fil->ftime = jiffies;
	fil->vlan_id = vlan_id;
	memcpy(fil->faddr, &src_addr, ETH_ALEN);
	spin_lock(&adapter->mac_learn_lock);
	hlist_add_head(&(fil->fnode), head);
	adapter->fhash.fnum++;
	spin_unlock(&adapter->mac_learn_lock);
}

static void
qlcnic_tso_check(struct net_device *netdev,
		struct qlcnic_host_tx_ring *tx_ring,
		struct cmd_desc_type0 *first_desc,
		struct sk_buff *skb)
{
	u8 opcode = TX_ETHER_PKT;
	__be16 protocol = skb->protocol;
	u16 flags = 0;
	int copied, offset, copy_len, hdr_len = 0, tso = 0;
	struct cmd_desc_type0 *hwdesc;
	struct vlan_ethhdr *vh;
	struct qlcnic_adapter *adapter = netdev_priv(netdev);
	u32 producer = tx_ring->producer;
	__le16 vlan_oob = first_desc->flags_opcode &
				cpu_to_le16(FLAGS_VLAN_OOB);

	if (*(skb->data) & BIT_0) {
		flags |= BIT_0;
		memcpy(&first_desc->eth_addr, skb->data, ETH_ALEN);
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
		vh->h_vlan_TCI = (__be16)swab16((u16)first_desc->vlan_TCI);

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

static int
qlcnic_check_tx_tagging(struct qlcnic_adapter *adapter,
			struct sk_buff *skb,
			struct cmd_desc_type0 *first_desc)
{
	u8 opcode = 0;
	u16 flags = 0;
	__be16 protocol = skb->protocol;
	struct vlan_ethhdr *vh;

	if (protocol == cpu_to_be16(ETH_P_8021Q)) {
		vh = (struct vlan_ethhdr *)skb->data;
		protocol = vh->h_vlan_encapsulated_proto;
		flags = FLAGS_VLAN_TAGGED;
		qlcnic_set_tx_vlan_tci(first_desc, ntohs(vh->h_vlan_TCI));
	} else if (vlan_tx_tag_present(skb)) {
		flags = FLAGS_VLAN_OOB;
		qlcnic_set_tx_vlan_tci(first_desc, vlan_tx_tag_get(skb));
	}
	if (unlikely(adapter->pvid)) {
		if (first_desc->vlan_TCI &&
				!(adapter->flags & QLCNIC_TAGGING_ENABLED))
			return -EIO;
		if (first_desc->vlan_TCI &&
				(adapter->flags & QLCNIC_TAGGING_ENABLED))
			goto set_flags;

		flags = FLAGS_VLAN_OOB;
		qlcnic_set_tx_vlan_tci(first_desc, adapter->pvid);
	}
set_flags:
	qlcnic_set_tx_flags_opcode(first_desc, flags, opcode);
	return 0;
}

static inline void
qlcnic_clear_cmddesc(u64 *desc)
{
	desc[0] = 0ULL;
	desc[2] = 0ULL;
	desc[7] = 0ULL;
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
	struct ethhdr *phdr;
	int delta = 0;
	int i, k;

	u32 producer;
	int frag_count, no_of_desc;
	u32 num_txd = tx_ring->num_desc;

	if (!test_bit(__QLCNIC_DEV_UP, &adapter->state)) {
		netif_stop_queue(netdev);
		return NETDEV_TX_BUSY;
	}

	if (adapter->flags & QLCNIC_MACSPOOF) {
		phdr = (struct ethhdr *)skb->data;
		if (compare_ether_addr(phdr->h_source,
					adapter->mac_addr))
			goto drop_packet;
	}

	frag_count = skb_shinfo(skb)->nr_frags + 1;
	/* 14 frags supported for normal packet and
	 * 32 frags supported for TSO packet
	 */
	if (!skb_is_gso(skb) && frag_count > QLCNIC_MAX_FRAGS_PER_TX) {

		for (i = 0; i < (frag_count - QLCNIC_MAX_FRAGS_PER_TX); i++)
			delta += skb_shinfo(skb)->frags[i].size;

		if (!__pskb_pull_tail(skb, delta))
			goto drop_packet;

		frag_count = 1 + skb_shinfo(skb)->nr_frags;
	}

	/* 4 fragments per cmd des */
	no_of_desc = (frag_count + 3) >> 2;

	if (unlikely(qlcnic_tx_avail(tx_ring) <= TX_STOP_THRESH)) {
		netif_stop_queue(netdev);
		smp_mb();
		if (qlcnic_tx_avail(tx_ring) > TX_STOP_THRESH)
			netif_start_queue(netdev);
		else {
			adapter->stats.xmit_off++;
			return NETDEV_TX_BUSY;
		}
	}

	producer = tx_ring->producer;
	pbuf = &tx_ring->cmd_buf_arr[producer];

	pdev = adapter->pdev;

	first_desc = hwdesc = &tx_ring->desc_head[producer];
	qlcnic_clear_cmddesc((u64 *)hwdesc);

	if (qlcnic_check_tx_tagging(adapter, skb, first_desc))
		goto drop_packet;

	if (qlcnic_map_tx_skb(pdev, skb, pbuf)) {
		adapter->stats.tx_dma_map_error++;
		goto drop_packet;
	}

	pbuf->skb = skb;
	pbuf->frag_count = frag_count;

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

	if (qlcnic_mac_learn)
		qlcnic_send_filter(adapter, tx_ring, first_desc, skb);

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
		netdev_info(netdev, "NIC Link is down\n");
		adapter->ahw.linkup = 0;
		if (netif_running(netdev)) {
			netif_carrier_off(netdev);
			netif_stop_queue(netdev);
		}
	} else if (!adapter->ahw.linkup && linkup) {
		netdev_info(netdev, "NIC Link is up\n");
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

	if (++adapter->tx_timeo_cnt >= QLCNIC_MAX_TX_TIMEOUTS)
		adapter->need_fw_reset = 1;
	else
		adapter->reset_context = 1;
}

static struct net_device_stats *qlcnic_get_stats(struct net_device *netdev)
{
	struct qlcnic_adapter *adapter = netdev_priv(netdev);
	struct net_device_stats *stats = &netdev->stats;

	stats->rx_packets = adapter->stats.rx_pkts + adapter->stats.lro_pkts;
	stats->tx_packets = adapter->stats.xmitfinished;
	stats->rx_bytes = adapter->stats.rxbytes + adapter->stats.lrobytes;
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
			if (qlcnic_tx_avail(tx_ring) > TX_STOP_THRESH) {
				netif_wake_queue(netdev);
				adapter->stats.xmit_on++;
			}
		}
		adapter->tx_timeo_cnt = 0;
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

static int qlcnic_rx_poll(struct napi_struct *napi, int budget)
{
	struct qlcnic_host_sds_ring *sds_ring =
		container_of(napi, struct qlcnic_host_sds_ring, napi);

	struct qlcnic_adapter *adapter = sds_ring->adapter;
	int work_done;

	work_done = qlcnic_process_rcv_ring(sds_ring, budget);

	if (work_done < budget) {
		napi_complete(&sds_ring->napi);
		if (test_bit(__QLCNIC_DEV_UP, &adapter->state))
			qlcnic_enable_int(sds_ring);
	}

	return work_done;
}

#ifdef CONFIG_NET_POLL_CONTROLLER
static void qlcnic_poll_controller(struct net_device *netdev)
{
	int ring;
	struct qlcnic_host_sds_ring *sds_ring;
	struct qlcnic_adapter *adapter = netdev_priv(netdev);
	struct qlcnic_recv_context *recv_ctx = &adapter->recv_ctx;

	disable_irq(adapter->irq);
	for (ring = 0; ring < adapter->max_sds_rings; ring++) {
		sds_ring = &recv_ctx->sds_rings[ring];
		qlcnic_intr(adapter->irq, sds_ring);
	}
	enable_irq(adapter->irq);
}
#endif

static void
qlcnic_idc_debug_info(struct qlcnic_adapter *adapter, u8 encoding)
{
	u32 val;

	val = adapter->portnum & 0xf;
	val |= encoding << 7;
	val |= (jiffies - adapter->dev_rst_time) << 8;

	QLCWR32(adapter, QLCNIC_CRB_DRV_SCRATCH, val);
	adapter->dev_rst_time = jiffies;
}

static int
qlcnic_set_drv_state(struct qlcnic_adapter *adapter, u8 state)
{
	u32  val;

	WARN_ON(state != QLCNIC_DEV_NEED_RESET &&
			state != QLCNIC_DEV_NEED_QUISCENT);

	if (qlcnic_api_lock(adapter))
		return -EIO;

	val = QLCRD32(adapter, QLCNIC_CRB_DRV_STATE);

	if (state == QLCNIC_DEV_NEED_RESET)
		QLC_DEV_SET_RST_RDY(val, adapter->portnum);
	else if (state == QLCNIC_DEV_NEED_QUISCENT)
		QLC_DEV_SET_QSCNT_RDY(val, adapter->portnum);

	QLCWR32(adapter, QLCNIC_CRB_DRV_STATE, val);

	qlcnic_api_unlock(adapter);

	return 0;
}

static int
qlcnic_clr_drv_state(struct qlcnic_adapter *adapter)
{
	u32  val;

	if (qlcnic_api_lock(adapter))
		return -EBUSY;

	val = QLCRD32(adapter, QLCNIC_CRB_DRV_STATE);
	QLC_DEV_CLR_RST_QSCNT(val, adapter->portnum);
	QLCWR32(adapter, QLCNIC_CRB_DRV_STATE, val);

	qlcnic_api_unlock(adapter);

	return 0;
}

static void
qlcnic_clr_all_drv_state(struct qlcnic_adapter *adapter, u8 failed)
{
	u32  val;

	if (qlcnic_api_lock(adapter))
		goto err;

	val = QLCRD32(adapter, QLCNIC_CRB_DRV_ACTIVE);
	QLC_DEV_CLR_REF_CNT(val, adapter->portnum);
	QLCWR32(adapter, QLCNIC_CRB_DRV_ACTIVE, val);

	if (failed) {
		QLCWR32(adapter, QLCNIC_CRB_DEV_STATE, QLCNIC_DEV_FAILED);
		dev_info(&adapter->pdev->dev,
				"Device state set to Failed. Please Reboot\n");
	} else if (!(val & 0x11111111))
		QLCWR32(adapter, QLCNIC_CRB_DEV_STATE, QLCNIC_DEV_COLD);

	val = QLCRD32(adapter, QLCNIC_CRB_DRV_STATE);
	QLC_DEV_CLR_RST_QSCNT(val, adapter->portnum);
	QLCWR32(adapter, QLCNIC_CRB_DRV_STATE, val);

	qlcnic_api_unlock(adapter);
err:
	adapter->fw_fail_cnt = 0;
	clear_bit(__QLCNIC_START_FW, &adapter->state);
	clear_bit(__QLCNIC_RESETTING, &adapter->state);
}

/* Grab api lock, before checking state */
static int
qlcnic_check_drv_state(struct qlcnic_adapter *adapter)
{
	int act, state;

	state = QLCRD32(adapter, QLCNIC_CRB_DRV_STATE);
	act = QLCRD32(adapter, QLCNIC_CRB_DRV_ACTIVE);

	if (((state & 0x11111111) == (act & 0x11111111)) ||
			((act & 0x11111111) == ((state >> 1) & 0x11111111)))
		return 0;
	else
		return 1;
}

static int qlcnic_check_idc_ver(struct qlcnic_adapter *adapter)
{
	u32 val = QLCRD32(adapter, QLCNIC_CRB_DRV_IDC_VER);

	if (val != QLCNIC_DRV_IDC_VER) {
		dev_warn(&adapter->pdev->dev, "IDC Version mismatch, driver's"
			" idc ver = %x; reqd = %x\n", QLCNIC_DRV_IDC_VER, val);
	}

	return 0;
}

static int
qlcnic_can_start_firmware(struct qlcnic_adapter *adapter)
{
	u32 val, prev_state;
	u8 dev_init_timeo = adapter->dev_init_timeo;
	u8 portnum = adapter->portnum;
	u8 ret;

	if (test_and_clear_bit(__QLCNIC_START_FW, &adapter->state))
		return 1;

	if (qlcnic_api_lock(adapter))
		return -1;

	val = QLCRD32(adapter, QLCNIC_CRB_DRV_ACTIVE);
	if (!(val & (1 << (portnum * 4)))) {
		QLC_DEV_SET_REF_CNT(val, portnum);
		QLCWR32(adapter, QLCNIC_CRB_DRV_ACTIVE, val);
	}

	prev_state = QLCRD32(adapter, QLCNIC_CRB_DEV_STATE);
	QLCDB(adapter, HW, "Device state = %u\n", prev_state);

	switch (prev_state) {
	case QLCNIC_DEV_COLD:
		QLCWR32(adapter, QLCNIC_CRB_DEV_STATE, QLCNIC_DEV_INITIALIZING);
		QLCWR32(adapter, QLCNIC_CRB_DRV_IDC_VER, QLCNIC_DRV_IDC_VER);
		qlcnic_idc_debug_info(adapter, 0);
		qlcnic_api_unlock(adapter);
		return 1;

	case QLCNIC_DEV_READY:
		ret = qlcnic_check_idc_ver(adapter);
		qlcnic_api_unlock(adapter);
		return ret;

	case QLCNIC_DEV_NEED_RESET:
		val = QLCRD32(adapter, QLCNIC_CRB_DRV_STATE);
		QLC_DEV_SET_RST_RDY(val, portnum);
		QLCWR32(adapter, QLCNIC_CRB_DRV_STATE, val);
		break;

	case QLCNIC_DEV_NEED_QUISCENT:
		val = QLCRD32(adapter, QLCNIC_CRB_DRV_STATE);
		QLC_DEV_SET_QSCNT_RDY(val, portnum);
		QLCWR32(adapter, QLCNIC_CRB_DRV_STATE, val);
		break;

	case QLCNIC_DEV_FAILED:
		dev_err(&adapter->pdev->dev, "Device in failed state.\n");
		qlcnic_api_unlock(adapter);
		return -1;

	case QLCNIC_DEV_INITIALIZING:
	case QLCNIC_DEV_QUISCENT:
		break;
	}

	qlcnic_api_unlock(adapter);

	do {
		msleep(1000);
		prev_state = QLCRD32(adapter, QLCNIC_CRB_DEV_STATE);

		if (prev_state == QLCNIC_DEV_QUISCENT)
			continue;
	} while ((prev_state != QLCNIC_DEV_READY) && --dev_init_timeo);

	if (!dev_init_timeo) {
		dev_err(&adapter->pdev->dev,
			"Waiting for device to initialize timeout\n");
		return -1;
	}

	if (qlcnic_api_lock(adapter))
		return -1;

	val = QLCRD32(adapter, QLCNIC_CRB_DRV_STATE);
	QLC_DEV_CLR_RST_QSCNT(val, portnum);
	QLCWR32(adapter, QLCNIC_CRB_DRV_STATE, val);

	ret = qlcnic_check_idc_ver(adapter);
	qlcnic_api_unlock(adapter);

	return ret;
}

static void
qlcnic_fwinit_work(struct work_struct *work)
{
	struct qlcnic_adapter *adapter = container_of(work,
			struct qlcnic_adapter, fw_work.work);
	u32 dev_state = 0xf;

	if (qlcnic_api_lock(adapter))
		goto err_ret;

	dev_state = QLCRD32(adapter, QLCNIC_CRB_DEV_STATE);
	if (dev_state == QLCNIC_DEV_QUISCENT ||
	    dev_state == QLCNIC_DEV_NEED_QUISCENT) {
		qlcnic_api_unlock(adapter);
		qlcnic_schedule_work(adapter, qlcnic_fwinit_work,
						FW_POLL_DELAY * 2);
		return;
	}

	if (adapter->op_mode == QLCNIC_NON_PRIV_FUNC) {
		qlcnic_api_unlock(adapter);
		goto wait_npar;
	}

	if (adapter->fw_wait_cnt++ > adapter->reset_ack_timeo) {
		dev_err(&adapter->pdev->dev, "Reset:Failed to get ack %d sec\n",
					adapter->reset_ack_timeo);
		goto skip_ack_check;
	}

	if (!qlcnic_check_drv_state(adapter)) {
skip_ack_check:
		dev_state = QLCRD32(adapter, QLCNIC_CRB_DEV_STATE);

		if (dev_state == QLCNIC_DEV_NEED_RESET) {
			QLCWR32(adapter, QLCNIC_CRB_DEV_STATE,
						QLCNIC_DEV_INITIALIZING);
			set_bit(__QLCNIC_START_FW, &adapter->state);
			QLCDB(adapter, DRV, "Restarting fw\n");
			qlcnic_idc_debug_info(adapter, 0);
		}

		qlcnic_api_unlock(adapter);

		if (!adapter->nic_ops->start_firmware(adapter)) {
			qlcnic_schedule_work(adapter, qlcnic_attach_work, 0);
			adapter->fw_wait_cnt = 0;
			return;
		}
		goto err_ret;
	}

	qlcnic_api_unlock(adapter);

wait_npar:
	dev_state = QLCRD32(adapter, QLCNIC_CRB_DEV_STATE);
	QLCDB(adapter, HW, "Func waiting: Device state=%u\n", dev_state);

	switch (dev_state) {
	case QLCNIC_DEV_READY:
		if (!adapter->nic_ops->start_firmware(adapter)) {
			qlcnic_schedule_work(adapter, qlcnic_attach_work, 0);
			adapter->fw_wait_cnt = 0;
			return;
		}
	case QLCNIC_DEV_FAILED:
		break;
	default:
		qlcnic_schedule_work(adapter,
			qlcnic_fwinit_work, FW_POLL_DELAY);
		return;
	}

err_ret:
	dev_err(&adapter->pdev->dev, "Fwinit work failed state=%u "
		"fw_wait_cnt=%u\n", dev_state, adapter->fw_wait_cnt);
	netif_device_attach(adapter->netdev);
	qlcnic_clr_all_drv_state(adapter, 0);
}

static void
qlcnic_detach_work(struct work_struct *work)
{
	struct qlcnic_adapter *adapter = container_of(work,
			struct qlcnic_adapter, fw_work.work);
	struct net_device *netdev = adapter->netdev;
	u32 status;

	netif_device_detach(netdev);

	/* Dont grab rtnl lock during Quiscent mode */
	if (adapter->dev_state == QLCNIC_DEV_NEED_QUISCENT) {
		if (netif_running(netdev))
			__qlcnic_down(adapter, netdev);
	} else
		qlcnic_down(adapter, netdev);

	status = QLCRD32(adapter, QLCNIC_PEG_HALT_STATUS1);

	if (status & QLCNIC_RCODE_FATAL_ERROR)
		goto err_ret;

	if (adapter->temp == QLCNIC_TEMP_PANIC)
		goto err_ret;

	if (qlcnic_set_drv_state(adapter, adapter->dev_state))
		goto err_ret;

	adapter->fw_wait_cnt = 0;

	qlcnic_schedule_work(adapter, qlcnic_fwinit_work, FW_POLL_DELAY);

	return;

err_ret:
	dev_err(&adapter->pdev->dev, "detach failed; status=%d temp=%d\n",
			status, adapter->temp);
	netif_device_attach(netdev);
	qlcnic_clr_all_drv_state(adapter, 1);
}

/*Transit NPAR state to NON Operational */
static void
qlcnic_set_npar_non_operational(struct qlcnic_adapter *adapter)
{
	u32 state;

	state = QLCRD32(adapter, QLCNIC_CRB_DEV_NPAR_STATE);
	if (state == QLCNIC_DEV_NPAR_NON_OPER)
		return;

	if (qlcnic_api_lock(adapter))
		return;
	QLCWR32(adapter, QLCNIC_CRB_DEV_NPAR_STATE, QLCNIC_DEV_NPAR_NON_OPER);
	qlcnic_api_unlock(adapter);
}

/*Transit to RESET state from READY state only */
static void
qlcnic_dev_request_reset(struct qlcnic_adapter *adapter)
{
	u32 state;

	adapter->need_fw_reset = 1;
	if (qlcnic_api_lock(adapter))
		return;

	state = QLCRD32(adapter, QLCNIC_CRB_DEV_STATE);

	if (state == QLCNIC_DEV_READY) {
		QLCWR32(adapter, QLCNIC_CRB_DEV_STATE, QLCNIC_DEV_NEED_RESET);
		QLCDB(adapter, DRV, "NEED_RESET state set\n");
		qlcnic_idc_debug_info(adapter, 0);
	}

	QLCWR32(adapter, QLCNIC_CRB_DEV_NPAR_STATE, QLCNIC_DEV_NPAR_NON_OPER);
	qlcnic_api_unlock(adapter);
}

/* Transit to NPAR READY state from NPAR NOT READY state */
static void
qlcnic_dev_set_npar_ready(struct qlcnic_adapter *adapter)
{
	if (qlcnic_api_lock(adapter))
		return;

	QLCWR32(adapter, QLCNIC_CRB_DEV_NPAR_STATE, QLCNIC_DEV_NPAR_OPER);
	QLCDB(adapter, DRV, "NPAR operational state set\n");

	qlcnic_api_unlock(adapter);
}

static void
qlcnic_schedule_work(struct qlcnic_adapter *adapter,
		work_func_t func, int delay)
{
	if (test_bit(__QLCNIC_AER, &adapter->state))
		return;

	INIT_DELAYED_WORK(&adapter->fw_work, func);
	queue_delayed_work(qlcnic_wq, &adapter->fw_work,
					round_jiffies_relative(delay));
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
	u32 npar_state;

	if (adapter->op_mode != QLCNIC_MGMT_FUNC) {
		npar_state = QLCRD32(adapter, QLCNIC_CRB_DEV_NPAR_STATE);
		if (adapter->fw_wait_cnt++ > QLCNIC_DEV_NPAR_OPER_TIMEO)
			qlcnic_clr_all_drv_state(adapter, 0);
		else if (npar_state != QLCNIC_DEV_NPAR_OPER)
			qlcnic_schedule_work(adapter, qlcnic_attach_work,
							FW_POLL_DELAY);
		else
			goto attach;
		QLCDB(adapter, DRV, "Waiting for NPAR state to operational\n");
		return;
	}
attach:
	if (netif_running(netdev)) {
		if (qlcnic_up(adapter, netdev))
			goto done;

		qlcnic_restore_indev_addr(netdev, NETDEV_UP);
	}

done:
	netif_device_attach(netdev);
	adapter->fw_fail_cnt = 0;
	clear_bit(__QLCNIC_RESETTING, &adapter->state);

	if (!qlcnic_clr_drv_state(adapter))
		qlcnic_schedule_work(adapter, qlcnic_fw_poll_work,
							FW_POLL_DELAY);
}

static int
qlcnic_check_health(struct qlcnic_adapter *adapter)
{
	u32 state = 0, heartbeat;
	struct net_device *netdev = adapter->netdev;

	if (qlcnic_check_temp(adapter))
		goto detach;

	if (adapter->need_fw_reset)
		qlcnic_dev_request_reset(adapter);

	state = QLCRD32(adapter, QLCNIC_CRB_DEV_STATE);
	if (state == QLCNIC_DEV_NEED_RESET) {
		qlcnic_set_npar_non_operational(adapter);
		adapter->need_fw_reset = 1;
	} else if (state == QLCNIC_DEV_NEED_QUISCENT)
		goto detach;

	heartbeat = QLCRD32(adapter, QLCNIC_PEG_ALIVE_COUNTER);
	if (heartbeat != adapter->heartbeat) {
		adapter->heartbeat = heartbeat;
		adapter->fw_fail_cnt = 0;
		if (adapter->need_fw_reset)
			goto detach;

		if (adapter->reset_context && auto_fw_reset) {
			qlcnic_reset_hw_context(adapter);
			adapter->netdev->trans_start = jiffies;
		}

		return 0;
	}

	if (++adapter->fw_fail_cnt < FW_FAIL_THRESH)
		return 0;

	qlcnic_dev_request_reset(adapter);

	if (auto_fw_reset)
		clear_bit(__QLCNIC_FW_ATTACHED, &adapter->state);

	dev_info(&netdev->dev, "firmware hang detected\n");

detach:
	adapter->dev_state = (state == QLCNIC_DEV_NEED_QUISCENT) ? state :
		QLCNIC_DEV_NEED_RESET;

	if (auto_fw_reset &&
		!test_and_set_bit(__QLCNIC_RESETTING, &adapter->state)) {

		qlcnic_schedule_work(adapter, qlcnic_detach_work, 0);
		QLCDB(adapter, DRV, "fw recovery scheduled.\n");
	}

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

	if (adapter->fhash.fnum)
		qlcnic_prune_lb_filters(adapter);

reschedule:
	qlcnic_schedule_work(adapter, qlcnic_fw_poll_work, FW_POLL_DELAY);
}

static int qlcnic_is_first_func(struct pci_dev *pdev)
{
	struct pci_dev *oth_pdev;
	int val = pdev->devfn;

	while (val-- > 0) {
		oth_pdev = pci_get_domain_bus_and_slot(pci_domain_nr
			(pdev->bus), pdev->bus->number,
			PCI_DEVFN(PCI_SLOT(pdev->devfn), val));
		if (!oth_pdev)
			continue;

		if (oth_pdev->current_state != PCI_D3cold) {
			pci_dev_put(oth_pdev);
			return 0;
		}
		pci_dev_put(oth_pdev);
	}
	return 1;
}

static int qlcnic_attach_func(struct pci_dev *pdev)
{
	int err, first_func;
	struct qlcnic_adapter *adapter = pci_get_drvdata(pdev);
	struct net_device *netdev = adapter->netdev;

	pdev->error_state = pci_channel_io_normal;

	err = pci_enable_device(pdev);
	if (err)
		return err;

	pci_set_power_state(pdev, PCI_D0);
	pci_set_master(pdev);
	pci_restore_state(pdev);

	first_func = qlcnic_is_first_func(pdev);

	if (qlcnic_api_lock(adapter))
		return -EINVAL;

	if (adapter->op_mode != QLCNIC_NON_PRIV_FUNC && first_func) {
		adapter->need_fw_reset = 1;
		set_bit(__QLCNIC_START_FW, &adapter->state);
		QLCWR32(adapter, QLCNIC_CRB_DEV_STATE, QLCNIC_DEV_INITIALIZING);
		QLCDB(adapter, DRV, "Restarting fw\n");
	}
	qlcnic_api_unlock(adapter);

	err = adapter->nic_ops->start_firmware(adapter);
	if (err)
		return err;

	qlcnic_clr_drv_state(adapter);
	qlcnic_setup_intr(adapter);

	if (netif_running(netdev)) {
		err = qlcnic_attach(adapter);
		if (err) {
			qlcnic_clr_all_drv_state(adapter, 1);
			clear_bit(__QLCNIC_AER, &adapter->state);
			netif_device_attach(netdev);
			return err;
		}

		err = qlcnic_up(adapter, netdev);
		if (err)
			goto done;

		qlcnic_restore_indev_addr(netdev, NETDEV_UP);
	}
 done:
	netif_device_attach(netdev);
	return err;
}

static pci_ers_result_t qlcnic_io_error_detected(struct pci_dev *pdev,
						pci_channel_state_t state)
{
	struct qlcnic_adapter *adapter = pci_get_drvdata(pdev);
	struct net_device *netdev = adapter->netdev;

	if (state == pci_channel_io_perm_failure)
		return PCI_ERS_RESULT_DISCONNECT;

	if (state == pci_channel_io_normal)
		return PCI_ERS_RESULT_RECOVERED;

	set_bit(__QLCNIC_AER, &adapter->state);
	netif_device_detach(netdev);

	cancel_delayed_work_sync(&adapter->fw_work);

	if (netif_running(netdev))
		qlcnic_down(adapter, netdev);

	qlcnic_detach(adapter);
	qlcnic_teardown_intr(adapter);

	clear_bit(__QLCNIC_RESETTING, &adapter->state);

	pci_save_state(pdev);
	pci_disable_device(pdev);

	return PCI_ERS_RESULT_NEED_RESET;
}

static pci_ers_result_t qlcnic_io_slot_reset(struct pci_dev *pdev)
{
	return qlcnic_attach_func(pdev) ? PCI_ERS_RESULT_DISCONNECT :
				PCI_ERS_RESULT_RECOVERED;
}

static void qlcnic_io_resume(struct pci_dev *pdev)
{
	struct qlcnic_adapter *adapter = pci_get_drvdata(pdev);

	pci_cleanup_aer_uncorrect_error_status(pdev);

	if (QLCRD32(adapter, QLCNIC_CRB_DEV_STATE) == QLCNIC_DEV_READY &&
	    test_and_clear_bit(__QLCNIC_AER, &adapter->state))
		qlcnic_schedule_work(adapter, qlcnic_fw_poll_work,
						FW_POLL_DELAY);
}

static int
qlcnicvf_start_firmware(struct qlcnic_adapter *adapter)
{
	int err;

	err = qlcnic_can_start_firmware(adapter);
	if (err)
		return err;

	err = qlcnic_check_npar_opertional(adapter);
	if (err)
		return err;

	err = qlcnic_initialize_nic(adapter);
	if (err)
		return err;

	qlcnic_check_options(adapter);

	err = qlcnic_set_eswitch_port_config(adapter);
	if (err)
		return err;

	adapter->need_fw_reset = 0;

	return err;
}

static int
qlcnicvf_config_bridged_mode(struct qlcnic_adapter *adapter, u32 enable)
{
	return -EOPNOTSUPP;
}

static int
qlcnicvf_config_led(struct qlcnic_adapter *adapter, u32 state, u32 rate)
{
	return -EOPNOTSUPP;
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

	if (!test_bit(__QLCNIC_DEV_UP, &adapter->state))
		goto err_out;

	if (strict_strtoul(buf, 2, &new))
		goto err_out;

	if (!adapter->nic_ops->config_bridged_mode(adapter, !!new))
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
	size_t crb_size = 4;

	if (!(adapter->flags & QLCNIC_DIAG_ENABLED))
		return -EIO;

	if (offset < QLCNIC_PCI_CRBSPACE) {
		if (ADDR_IN_RANGE(offset, QLCNIC_PCI_CAMQM,
					QLCNIC_PCI_CAMQM_END))
			crb_size = 8;
		else
			return -EINVAL;
	}

	if ((size != crb_size) || (offset & (crb_size-1)))
		return  -EINVAL;

	return 0;
}

static ssize_t
qlcnic_sysfs_read_crb(struct file *filp, struct kobject *kobj,
		struct bin_attribute *attr,
		char *buf, loff_t offset, size_t size)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct qlcnic_adapter *adapter = dev_get_drvdata(dev);
	u32 data;
	u64 qmdata;
	int ret;

	ret = qlcnic_sysfs_validate_crb(adapter, offset, size);
	if (ret != 0)
		return ret;

	if (ADDR_IN_RANGE(offset, QLCNIC_PCI_CAMQM, QLCNIC_PCI_CAMQM_END)) {
		qlcnic_pci_camqm_read_2M(adapter, offset, &qmdata);
		memcpy(buf, &qmdata, size);
	} else {
		data = QLCRD32(adapter, offset);
		memcpy(buf, &data, size);
	}
	return size;
}

static ssize_t
qlcnic_sysfs_write_crb(struct file *filp, struct kobject *kobj,
		struct bin_attribute *attr,
		char *buf, loff_t offset, size_t size)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct qlcnic_adapter *adapter = dev_get_drvdata(dev);
	u32 data;
	u64 qmdata;
	int ret;

	ret = qlcnic_sysfs_validate_crb(adapter, offset, size);
	if (ret != 0)
		return ret;

	if (ADDR_IN_RANGE(offset, QLCNIC_PCI_CAMQM, QLCNIC_PCI_CAMQM_END)) {
		memcpy(&qmdata, buf, size);
		qlcnic_pci_camqm_write_2M(adapter, offset, qmdata);
	} else {
		memcpy(&data, buf, size);
		QLCWR32(adapter, offset, data);
	}
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
qlcnic_sysfs_read_mem(struct file *filp, struct kobject *kobj,
		struct bin_attribute *attr,
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
qlcnic_sysfs_write_mem(struct file *filp, struct kobject *kobj,
		struct bin_attribute *attr,
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

static int
validate_pm_config(struct qlcnic_adapter *adapter,
			struct qlcnic_pm_func_cfg *pm_cfg, int count)
{

	u8 src_pci_func, s_esw_id, d_esw_id;
	u8 dest_pci_func;
	int i;

	for (i = 0; i < count; i++) {
		src_pci_func = pm_cfg[i].pci_func;
		dest_pci_func = pm_cfg[i].dest_npar;
		if (src_pci_func >= QLCNIC_MAX_PCI_FUNC
				|| dest_pci_func >= QLCNIC_MAX_PCI_FUNC)
			return QL_STATUS_INVALID_PARAM;

		if (adapter->npars[src_pci_func].type != QLCNIC_TYPE_NIC)
			return QL_STATUS_INVALID_PARAM;

		if (adapter->npars[dest_pci_func].type != QLCNIC_TYPE_NIC)
			return QL_STATUS_INVALID_PARAM;

		s_esw_id = adapter->npars[src_pci_func].phy_port;
		d_esw_id = adapter->npars[dest_pci_func].phy_port;

		if (s_esw_id != d_esw_id)
			return QL_STATUS_INVALID_PARAM;

	}
	return 0;

}

static ssize_t
qlcnic_sysfs_write_pm_config(struct file *filp, struct kobject *kobj,
	struct bin_attribute *attr, char *buf, loff_t offset, size_t size)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct qlcnic_adapter *adapter = dev_get_drvdata(dev);
	struct qlcnic_pm_func_cfg *pm_cfg;
	u32 id, action, pci_func;
	int count, rem, i, ret;

	count	= size / sizeof(struct qlcnic_pm_func_cfg);
	rem	= size % sizeof(struct qlcnic_pm_func_cfg);
	if (rem)
		return QL_STATUS_INVALID_PARAM;

	pm_cfg = (struct qlcnic_pm_func_cfg *) buf;

	ret = validate_pm_config(adapter, pm_cfg, count);
	if (ret)
		return ret;
	for (i = 0; i < count; i++) {
		pci_func = pm_cfg[i].pci_func;
		action = !!pm_cfg[i].action;
		id = adapter->npars[pci_func].phy_port;
		ret = qlcnic_config_port_mirroring(adapter, id,
						action, pci_func);
		if (ret)
			return ret;
	}

	for (i = 0; i < count; i++) {
		pci_func = pm_cfg[i].pci_func;
		id = adapter->npars[pci_func].phy_port;
		adapter->npars[pci_func].enable_pm = !!pm_cfg[i].action;
		adapter->npars[pci_func].dest_npar = id;
	}
	return size;
}

static ssize_t
qlcnic_sysfs_read_pm_config(struct file *filp, struct kobject *kobj,
	struct bin_attribute *attr, char *buf, loff_t offset, size_t size)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct qlcnic_adapter *adapter = dev_get_drvdata(dev);
	struct qlcnic_pm_func_cfg pm_cfg[QLCNIC_MAX_PCI_FUNC];
	int i;

	if (size != sizeof(pm_cfg))
		return QL_STATUS_INVALID_PARAM;

	for (i = 0; i < QLCNIC_MAX_PCI_FUNC; i++) {
		if (adapter->npars[i].type != QLCNIC_TYPE_NIC)
			continue;
		pm_cfg[i].action = adapter->npars[i].enable_pm;
		pm_cfg[i].dest_npar = 0;
		pm_cfg[i].pci_func = i;
	}
	memcpy(buf, &pm_cfg, size);

	return size;
}

static int
validate_esw_config(struct qlcnic_adapter *adapter,
	struct qlcnic_esw_func_cfg *esw_cfg, int count)
{
	u32 op_mode;
	u8 pci_func;
	int i;

	op_mode = readl(adapter->ahw.pci_base0 + QLCNIC_DRV_OP_MODE);

	for (i = 0; i < count; i++) {
		pci_func = esw_cfg[i].pci_func;
		if (pci_func >= QLCNIC_MAX_PCI_FUNC)
			return QL_STATUS_INVALID_PARAM;

		if (adapter->op_mode == QLCNIC_MGMT_FUNC)
			if (adapter->npars[pci_func].type != QLCNIC_TYPE_NIC)
				return QL_STATUS_INVALID_PARAM;

		switch (esw_cfg[i].op_mode) {
		case QLCNIC_PORT_DEFAULTS:
			if (QLC_DEV_GET_DRV(op_mode, pci_func) !=
						QLCNIC_NON_PRIV_FUNC) {
				if (esw_cfg[i].mac_anti_spoof != 0)
					return QL_STATUS_INVALID_PARAM;
				if (esw_cfg[i].mac_override != 1)
					return QL_STATUS_INVALID_PARAM;
				if (esw_cfg[i].promisc_mode != 1)
					return QL_STATUS_INVALID_PARAM;
			}
			break;
		case QLCNIC_ADD_VLAN:
			if (!IS_VALID_VLAN(esw_cfg[i].vlan_id))
				return QL_STATUS_INVALID_PARAM;
			if (!esw_cfg[i].op_type)
				return QL_STATUS_INVALID_PARAM;
			break;
		case QLCNIC_DEL_VLAN:
			if (!esw_cfg[i].op_type)
				return QL_STATUS_INVALID_PARAM;
			break;
		default:
			return QL_STATUS_INVALID_PARAM;
		}
	}
	return 0;
}

static ssize_t
qlcnic_sysfs_write_esw_config(struct file *file, struct kobject *kobj,
	struct bin_attribute *attr, char *buf, loff_t offset, size_t size)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct qlcnic_adapter *adapter = dev_get_drvdata(dev);
	struct qlcnic_esw_func_cfg *esw_cfg;
	struct qlcnic_npar_info *npar;
	int count, rem, i, ret;
	u8 pci_func, op_mode = 0;

	count	= size / sizeof(struct qlcnic_esw_func_cfg);
	rem	= size % sizeof(struct qlcnic_esw_func_cfg);
	if (rem)
		return QL_STATUS_INVALID_PARAM;

	esw_cfg = (struct qlcnic_esw_func_cfg *) buf;
	ret = validate_esw_config(adapter, esw_cfg, count);
	if (ret)
		return ret;

	for (i = 0; i < count; i++) {
		if (adapter->op_mode == QLCNIC_MGMT_FUNC)
			if (qlcnic_config_switch_port(adapter, &esw_cfg[i]))
				return QL_STATUS_INVALID_PARAM;

		if (adapter->ahw.pci_func != esw_cfg[i].pci_func)
			continue;

		op_mode = esw_cfg[i].op_mode;
		qlcnic_get_eswitch_port_config(adapter, &esw_cfg[i]);
		esw_cfg[i].op_mode = op_mode;
		esw_cfg[i].pci_func = adapter->ahw.pci_func;

		switch (esw_cfg[i].op_mode) {
		case QLCNIC_PORT_DEFAULTS:
			qlcnic_set_eswitch_port_features(adapter, &esw_cfg[i]);
			break;
		case QLCNIC_ADD_VLAN:
			qlcnic_set_vlan_config(adapter, &esw_cfg[i]);
			break;
		case QLCNIC_DEL_VLAN:
			esw_cfg[i].vlan_id = 0;
			qlcnic_set_vlan_config(adapter, &esw_cfg[i]);
			break;
		}
	}

	if (adapter->op_mode != QLCNIC_MGMT_FUNC)
		goto out;

	for (i = 0; i < count; i++) {
		pci_func = esw_cfg[i].pci_func;
		npar = &adapter->npars[pci_func];
		switch (esw_cfg[i].op_mode) {
		case QLCNIC_PORT_DEFAULTS:
			npar->promisc_mode = esw_cfg[i].promisc_mode;
			npar->mac_override = esw_cfg[i].mac_override;
			npar->offload_flags = esw_cfg[i].offload_flags;
			npar->mac_anti_spoof = esw_cfg[i].mac_anti_spoof;
			npar->discard_tagged = esw_cfg[i].discard_tagged;
			break;
		case QLCNIC_ADD_VLAN:
			npar->pvid = esw_cfg[i].vlan_id;
			break;
		case QLCNIC_DEL_VLAN:
			npar->pvid = 0;
			break;
		}
	}
out:
	return size;
}

static ssize_t
qlcnic_sysfs_read_esw_config(struct file *file, struct kobject *kobj,
	struct bin_attribute *attr, char *buf, loff_t offset, size_t size)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct qlcnic_adapter *adapter = dev_get_drvdata(dev);
	struct qlcnic_esw_func_cfg esw_cfg[QLCNIC_MAX_PCI_FUNC];
	u8 i;

	if (size != sizeof(esw_cfg))
		return QL_STATUS_INVALID_PARAM;

	for (i = 0; i < QLCNIC_MAX_PCI_FUNC; i++) {
		if (adapter->npars[i].type != QLCNIC_TYPE_NIC)
			continue;
		esw_cfg[i].pci_func = i;
		if (qlcnic_get_eswitch_port_config(adapter, &esw_cfg[i]))
			return QL_STATUS_INVALID_PARAM;
	}
	memcpy(buf, &esw_cfg, size);

	return size;
}

static int
validate_npar_config(struct qlcnic_adapter *adapter,
				struct qlcnic_npar_func_cfg *np_cfg, int count)
{
	u8 pci_func, i;

	for (i = 0; i < count; i++) {
		pci_func = np_cfg[i].pci_func;
		if (pci_func >= QLCNIC_MAX_PCI_FUNC)
			return QL_STATUS_INVALID_PARAM;

		if (adapter->npars[pci_func].type != QLCNIC_TYPE_NIC)
			return QL_STATUS_INVALID_PARAM;

		if (!IS_VALID_BW(np_cfg[i].min_bw) ||
		    !IS_VALID_BW(np_cfg[i].max_bw))
			return QL_STATUS_INVALID_PARAM;
	}
	return 0;
}

static ssize_t
qlcnic_sysfs_write_npar_config(struct file *file, struct kobject *kobj,
	struct bin_attribute *attr, char *buf, loff_t offset, size_t size)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct qlcnic_adapter *adapter = dev_get_drvdata(dev);
	struct qlcnic_info nic_info;
	struct qlcnic_npar_func_cfg *np_cfg;
	int i, count, rem, ret;
	u8 pci_func;

	count	= size / sizeof(struct qlcnic_npar_func_cfg);
	rem	= size % sizeof(struct qlcnic_npar_func_cfg);
	if (rem)
		return QL_STATUS_INVALID_PARAM;

	np_cfg = (struct qlcnic_npar_func_cfg *) buf;
	ret = validate_npar_config(adapter, np_cfg, count);
	if (ret)
		return ret;

	for (i = 0; i < count ; i++) {
		pci_func = np_cfg[i].pci_func;
		ret = qlcnic_get_nic_info(adapter, &nic_info, pci_func);
		if (ret)
			return ret;
		nic_info.pci_func = pci_func;
		nic_info.min_tx_bw = np_cfg[i].min_bw;
		nic_info.max_tx_bw = np_cfg[i].max_bw;
		ret = qlcnic_set_nic_info(adapter, &nic_info);
		if (ret)
			return ret;
		adapter->npars[i].min_bw = nic_info.min_tx_bw;
		adapter->npars[i].max_bw = nic_info.max_tx_bw;
	}

	return size;

}
static ssize_t
qlcnic_sysfs_read_npar_config(struct file *file, struct kobject *kobj,
	struct bin_attribute *attr, char *buf, loff_t offset, size_t size)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct qlcnic_adapter *adapter = dev_get_drvdata(dev);
	struct qlcnic_info nic_info;
	struct qlcnic_npar_func_cfg np_cfg[QLCNIC_MAX_PCI_FUNC];
	int i, ret;

	if (size != sizeof(np_cfg))
		return QL_STATUS_INVALID_PARAM;

	for (i = 0; i < QLCNIC_MAX_PCI_FUNC ; i++) {
		if (adapter->npars[i].type != QLCNIC_TYPE_NIC)
			continue;
		ret = qlcnic_get_nic_info(adapter, &nic_info, i);
		if (ret)
			return ret;

		np_cfg[i].pci_func = i;
		np_cfg[i].op_mode = (u8)nic_info.op_mode;
		np_cfg[i].port_num = nic_info.phys_port;
		np_cfg[i].fw_capab = nic_info.capabilities;
		np_cfg[i].min_bw = nic_info.min_tx_bw ;
		np_cfg[i].max_bw = nic_info.max_tx_bw;
		np_cfg[i].max_tx_queues = nic_info.max_tx_ques;
		np_cfg[i].max_rx_queues = nic_info.max_rx_ques;
	}
	memcpy(buf, &np_cfg, size);
	return size;
}

static ssize_t
qlcnic_sysfs_get_port_stats(struct file *file, struct kobject *kobj,
	struct bin_attribute *attr, char *buf, loff_t offset, size_t size)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct qlcnic_adapter *adapter = dev_get_drvdata(dev);
	struct qlcnic_esw_statistics port_stats;
	int ret;

	if (size != sizeof(struct qlcnic_esw_statistics))
		return QL_STATUS_INVALID_PARAM;

	if (offset >= QLCNIC_MAX_PCI_FUNC)
		return QL_STATUS_INVALID_PARAM;

	memset(&port_stats, 0, size);
	ret = qlcnic_get_port_stats(adapter, offset, QLCNIC_QUERY_RX_COUNTER,
								&port_stats.rx);
	if (ret)
		return ret;

	ret = qlcnic_get_port_stats(adapter, offset, QLCNIC_QUERY_TX_COUNTER,
								&port_stats.tx);
	if (ret)
		return ret;

	memcpy(buf, &port_stats, size);
	return size;
}

static ssize_t
qlcnic_sysfs_get_esw_stats(struct file *file, struct kobject *kobj,
	struct bin_attribute *attr, char *buf, loff_t offset, size_t size)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct qlcnic_adapter *adapter = dev_get_drvdata(dev);
	struct qlcnic_esw_statistics esw_stats;
	int ret;

	if (size != sizeof(struct qlcnic_esw_statistics))
		return QL_STATUS_INVALID_PARAM;

	if (offset >= QLCNIC_NIU_MAX_XG_PORTS)
		return QL_STATUS_INVALID_PARAM;

	memset(&esw_stats, 0, size);
	ret = qlcnic_get_eswitch_stats(adapter, offset, QLCNIC_QUERY_RX_COUNTER,
								&esw_stats.rx);
	if (ret)
		return ret;

	ret = qlcnic_get_eswitch_stats(adapter, offset, QLCNIC_QUERY_TX_COUNTER,
								&esw_stats.tx);
	if (ret)
		return ret;

	memcpy(buf, &esw_stats, size);
	return size;
}

static ssize_t
qlcnic_sysfs_clear_esw_stats(struct file *file, struct kobject *kobj,
	struct bin_attribute *attr, char *buf, loff_t offset, size_t size)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct qlcnic_adapter *adapter = dev_get_drvdata(dev);
	int ret;

	if (offset >= QLCNIC_NIU_MAX_XG_PORTS)
		return QL_STATUS_INVALID_PARAM;

	ret = qlcnic_clear_esw_stats(adapter, QLCNIC_STATS_ESWITCH, offset,
						QLCNIC_QUERY_RX_COUNTER);
	if (ret)
		return ret;

	ret = qlcnic_clear_esw_stats(adapter, QLCNIC_STATS_ESWITCH, offset,
						QLCNIC_QUERY_TX_COUNTER);
	if (ret)
		return ret;

	return size;
}

static ssize_t
qlcnic_sysfs_clear_port_stats(struct file *file, struct kobject *kobj,
	struct bin_attribute *attr, char *buf, loff_t offset, size_t size)
{

	struct device *dev = container_of(kobj, struct device, kobj);
	struct qlcnic_adapter *adapter = dev_get_drvdata(dev);
	int ret;

	if (offset >= QLCNIC_MAX_PCI_FUNC)
		return QL_STATUS_INVALID_PARAM;

	ret = qlcnic_clear_esw_stats(adapter, QLCNIC_STATS_PORT, offset,
						QLCNIC_QUERY_RX_COUNTER);
	if (ret)
		return ret;

	ret = qlcnic_clear_esw_stats(adapter, QLCNIC_STATS_PORT, offset,
						QLCNIC_QUERY_TX_COUNTER);
	if (ret)
		return ret;

	return size;
}

static ssize_t
qlcnic_sysfs_read_pci_config(struct file *file, struct kobject *kobj,
	struct bin_attribute *attr, char *buf, loff_t offset, size_t size)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct qlcnic_adapter *adapter = dev_get_drvdata(dev);
	struct qlcnic_pci_func_cfg pci_cfg[QLCNIC_MAX_PCI_FUNC];
	struct qlcnic_pci_info *pci_info;
	int i, ret;

	if (size != sizeof(pci_cfg))
		return QL_STATUS_INVALID_PARAM;

	pci_info = kcalloc(QLCNIC_MAX_PCI_FUNC, sizeof(*pci_info), GFP_KERNEL);
	if (!pci_info)
		return -ENOMEM;

	ret = qlcnic_get_pci_info(adapter, pci_info);
	if (ret) {
		kfree(pci_info);
		return ret;
	}

	for (i = 0; i < QLCNIC_MAX_PCI_FUNC ; i++) {
		pci_cfg[i].pci_func = pci_info[i].id;
		pci_cfg[i].func_type = pci_info[i].type;
		pci_cfg[i].port_num = pci_info[i].default_port;
		pci_cfg[i].min_bw = pci_info[i].tx_min_bw;
		pci_cfg[i].max_bw = pci_info[i].tx_max_bw;
		memcpy(&pci_cfg[i].def_mac_addr, &pci_info[i].mac, ETH_ALEN);
	}
	memcpy(buf, &pci_cfg, size);
	kfree(pci_info);
	return size;
}
static struct bin_attribute bin_attr_npar_config = {
	.attr = {.name = "npar_config", .mode = (S_IRUGO | S_IWUSR)},
	.size = 0,
	.read = qlcnic_sysfs_read_npar_config,
	.write = qlcnic_sysfs_write_npar_config,
};

static struct bin_attribute bin_attr_pci_config = {
	.attr = {.name = "pci_config", .mode = (S_IRUGO | S_IWUSR)},
	.size = 0,
	.read = qlcnic_sysfs_read_pci_config,
	.write = NULL,
};

static struct bin_attribute bin_attr_port_stats = {
	.attr = {.name = "port_stats", .mode = (S_IRUGO | S_IWUSR)},
	.size = 0,
	.read = qlcnic_sysfs_get_port_stats,
	.write = qlcnic_sysfs_clear_port_stats,
};

static struct bin_attribute bin_attr_esw_stats = {
	.attr = {.name = "esw_stats", .mode = (S_IRUGO | S_IWUSR)},
	.size = 0,
	.read = qlcnic_sysfs_get_esw_stats,
	.write = qlcnic_sysfs_clear_esw_stats,
};

static struct bin_attribute bin_attr_esw_config = {
	.attr = {.name = "esw_config", .mode = (S_IRUGO | S_IWUSR)},
	.size = 0,
	.read = qlcnic_sysfs_read_esw_config,
	.write = qlcnic_sysfs_write_esw_config,
};

static struct bin_attribute bin_attr_pm_config = {
	.attr = {.name = "pm_config", .mode = (S_IRUGO | S_IWUSR)},
	.size = 0,
	.read = qlcnic_sysfs_read_pm_config,
	.write = qlcnic_sysfs_write_pm_config,
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

	if (device_create_bin_file(dev, &bin_attr_port_stats))
		dev_info(dev, "failed to create port stats sysfs entry");

	if (adapter->op_mode == QLCNIC_NON_PRIV_FUNC)
		return;
	if (device_create_file(dev, &dev_attr_diag_mode))
		dev_info(dev, "failed to create diag_mode sysfs entry\n");
	if (device_create_bin_file(dev, &bin_attr_crb))
		dev_info(dev, "failed to create crb sysfs entry\n");
	if (device_create_bin_file(dev, &bin_attr_mem))
		dev_info(dev, "failed to create mem sysfs entry\n");
	if (!(adapter->flags & QLCNIC_ESWITCH_ENABLED))
		return;
	if (device_create_bin_file(dev, &bin_attr_esw_config))
		dev_info(dev, "failed to create esw config sysfs entry");
	if (adapter->op_mode != QLCNIC_MGMT_FUNC)
		return;
	if (device_create_bin_file(dev, &bin_attr_pci_config))
		dev_info(dev, "failed to create pci config sysfs entry");
	if (device_create_bin_file(dev, &bin_attr_npar_config))
		dev_info(dev, "failed to create npar config sysfs entry");
	if (device_create_bin_file(dev, &bin_attr_pm_config))
		dev_info(dev, "failed to create pm config sysfs entry");
	if (device_create_bin_file(dev, &bin_attr_esw_stats))
		dev_info(dev, "failed to create eswitch stats sysfs entry");
}

static void
qlcnic_remove_diag_entries(struct qlcnic_adapter *adapter)
{
	struct device *dev = &adapter->pdev->dev;

	device_remove_bin_file(dev, &bin_attr_port_stats);

	if (adapter->op_mode == QLCNIC_NON_PRIV_FUNC)
		return;
	device_remove_file(dev, &dev_attr_diag_mode);
	device_remove_bin_file(dev, &bin_attr_crb);
	device_remove_bin_file(dev, &bin_attr_mem);
	if (!(adapter->flags & QLCNIC_ESWITCH_ENABLED))
		return;
	device_remove_bin_file(dev, &bin_attr_esw_config);
	if (adapter->op_mode != QLCNIC_MGMT_FUNC)
		return;
	device_remove_bin_file(dev, &bin_attr_pci_config);
	device_remove_bin_file(dev, &bin_attr_npar_config);
	device_remove_bin_file(dev, &bin_attr_pm_config);
	device_remove_bin_file(dev, &bin_attr_esw_stats);
}

#ifdef CONFIG_INET

#define is_qlcnic_netdev(dev) (dev->netdev_ops == &qlcnic_netdev_ops)

static void
qlcnic_config_indev_addr(struct qlcnic_adapter *adapter,
			struct net_device *dev, unsigned long event)
{
	struct in_device *indev;

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
}

static void
qlcnic_restore_indev_addr(struct net_device *netdev, unsigned long event)
{
	struct qlcnic_adapter *adapter = netdev_priv(netdev);
	struct net_device *dev;
	u16 vid;

	qlcnic_config_indev_addr(adapter, netdev, event);

	if (!adapter->vlgrp)
		return;

	for (vid = 0; vid < VLAN_N_VID; vid++) {
		dev = vlan_group_get_device(adapter->vlgrp, vid);
		if (!dev)
			continue;

		qlcnic_config_indev_addr(adapter, dev, event);
	}
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

	if (!test_bit(__QLCNIC_DEV_UP, &adapter->state))
		goto done;

	qlcnic_config_indev_addr(adapter, dev, event);
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

	if (!test_bit(__QLCNIC_DEV_UP, &adapter->state))
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
qlcnic_restore_indev_addr(struct net_device *dev, unsigned long event)
{ }
#endif
static struct pci_error_handlers qlcnic_err_handler = {
	.error_detected = qlcnic_io_error_detected,
	.slot_reset = qlcnic_io_slot_reset,
	.resume = qlcnic_io_resume,
};

static struct pci_driver qlcnic_driver = {
	.name = qlcnic_driver_name,
	.id_table = qlcnic_pci_tbl,
	.probe = qlcnic_probe,
	.remove = __devexit_p(qlcnic_remove),
#ifdef CONFIG_PM
	.suspend = qlcnic_suspend,
	.resume = qlcnic_resume,
#endif
	.shutdown = qlcnic_shutdown,
	.err_handler = &qlcnic_err_handler

};

static int __init qlcnic_init_module(void)
{
	int ret;

	printk(KERN_INFO "%s\n", qlcnic_driver_string);

	qlcnic_wq = create_singlethread_workqueue("qlcnic");
	if (qlcnic_wq == NULL) {
		printk(KERN_ERR "qlcnic: cannot create workqueue\n");
		return -ENOMEM;
	}

#ifdef CONFIG_INET
	register_netdevice_notifier(&qlcnic_netdev_cb);
	register_inetaddr_notifier(&qlcnic_inetaddr_cb);
#endif

	ret = pci_register_driver(&qlcnic_driver);
	if (ret) {
#ifdef CONFIG_INET
		unregister_inetaddr_notifier(&qlcnic_inetaddr_cb);
		unregister_netdevice_notifier(&qlcnic_netdev_cb);
#endif
		destroy_workqueue(qlcnic_wq);
	}

	return ret;
}

module_init(qlcnic_init_module);

static void __exit qlcnic_exit_module(void)
{

	pci_unregister_driver(&qlcnic_driver);

#ifdef CONFIG_INET
	unregister_inetaddr_notifier(&qlcnic_inetaddr_cb);
	unregister_netdevice_notifier(&qlcnic_netdev_cb);
#endif
	destroy_workqueue(qlcnic_wq);
}

module_exit(qlcnic_exit_module);
