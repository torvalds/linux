// SPDX-License-Identifier: GPL-2.0+
/* Copyright (c) 2018 Quantenna Communications, Inc. All rights reserved. */

#include <linux/module.h>
#include <linux/printk.h>
#include <linux/pci.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>
#include <linux/netdevice.h>
#include <linux/seq_file.h>
#include <linux/workqueue.h>
#include <linux/completion.h>

#include "pcie_priv.h"
#include "bus.h"
#include "shm_ipc.h"
#include "core.h"
#include "debug.h"
#include "util.h"
#include "qtn_hw_ids.h"

#define QTN_SYSCTL_BAR	0
#define QTN_SHMEM_BAR	2
#define QTN_DMA_BAR	3

#define QTN_PCIE_MAX_FW_BUFSZ		(1 * 1024 * 1024)

static bool use_msi = true;
module_param(use_msi, bool, 0644);
MODULE_PARM_DESC(use_msi, "set 0 to use legacy interrupt");

static unsigned int tx_bd_size_param;
module_param(tx_bd_size_param, uint, 0644);
MODULE_PARM_DESC(tx_bd_size_param, "Tx descriptors queue size");

static unsigned int rx_bd_size_param = 256;
module_param(rx_bd_size_param, uint, 0644);
MODULE_PARM_DESC(rx_bd_size_param, "Rx descriptors queue size");

static u8 flashboot = 1;
module_param(flashboot, byte, 0644);
MODULE_PARM_DESC(flashboot, "set to 0 to use FW binary file on FS");

static unsigned int fw_blksize_param = QTN_PCIE_MAX_FW_BUFSZ;
module_param(fw_blksize_param, uint, 0644);
MODULE_PARM_DESC(fw_blksize_param, "firmware loading block size in bytes");

#define DRV_NAME	"qtnfmac_pcie"

int qtnf_pcie_control_tx(struct qtnf_bus *bus, struct sk_buff *skb)
{
	struct qtnf_pcie_bus_priv *priv = get_bus_priv(bus);
	int ret;

	ret = qtnf_shm_ipc_send(&priv->shm_ipc_ep_in, skb->data, skb->len);

	if (ret == -ETIMEDOUT) {
		pr_err("EP firmware is dead\n");
		bus->fw_state = QTNF_FW_STATE_DEAD;
	}

	return ret;
}

int qtnf_pcie_alloc_skb_array(struct qtnf_pcie_bus_priv *priv)
{
	struct sk_buff **vaddr;
	int len;

	len = priv->tx_bd_num * sizeof(*priv->tx_skb) +
		priv->rx_bd_num * sizeof(*priv->rx_skb);
	vaddr = devm_kzalloc(&priv->pdev->dev, len, GFP_KERNEL);

	if (!vaddr)
		return -ENOMEM;

	priv->tx_skb = vaddr;

	vaddr += priv->tx_bd_num;
	priv->rx_skb = vaddr;

	return 0;
}

static void qtnf_pcie_bringup_fw_async(struct qtnf_bus *bus)
{
	struct qtnf_pcie_bus_priv *priv = get_bus_priv(bus);
	struct pci_dev *pdev = priv->pdev;

	get_device(&pdev->dev);
	schedule_work(&bus->fw_work);
}

static int qtnf_dbg_mps_show(struct seq_file *s, void *data)
{
	struct qtnf_bus *bus = dev_get_drvdata(s->private);
	struct qtnf_pcie_bus_priv *priv = get_bus_priv(bus);

	seq_printf(s, "%d\n", pcie_get_mps(priv->pdev));

	return 0;
}

static int qtnf_dbg_msi_show(struct seq_file *s, void *data)
{
	struct qtnf_bus *bus = dev_get_drvdata(s->private);
	struct qtnf_pcie_bus_priv *priv = get_bus_priv(bus);

	seq_printf(s, "%u\n", priv->msi_enabled);

	return 0;
}

static int qtnf_dbg_shm_stats(struct seq_file *s, void *data)
{
	struct qtnf_bus *bus = dev_get_drvdata(s->private);
	struct qtnf_pcie_bus_priv *priv = get_bus_priv(bus);

	seq_printf(s, "shm_ipc_ep_in.tx_packet_count(%zu)\n",
		   priv->shm_ipc_ep_in.tx_packet_count);
	seq_printf(s, "shm_ipc_ep_in.rx_packet_count(%zu)\n",
		   priv->shm_ipc_ep_in.rx_packet_count);
	seq_printf(s, "shm_ipc_ep_out.tx_packet_count(%zu)\n",
		   priv->shm_ipc_ep_out.tx_timeout_count);
	seq_printf(s, "shm_ipc_ep_out.rx_packet_count(%zu)\n",
		   priv->shm_ipc_ep_out.rx_packet_count);

	return 0;
}

int qtnf_pcie_fw_boot_done(struct qtnf_bus *bus)
{
	int ret;

	bus->fw_state = QTNF_FW_STATE_BOOT_DONE;
	ret = qtnf_core_attach(bus);
	if (ret) {
		pr_err("failed to attach core\n");
	} else {
		qtnf_debugfs_init(bus, DRV_NAME);
		qtnf_debugfs_add_entry(bus, "mps", qtnf_dbg_mps_show);
		qtnf_debugfs_add_entry(bus, "msi_enabled", qtnf_dbg_msi_show);
		qtnf_debugfs_add_entry(bus, "shm_stats", qtnf_dbg_shm_stats);
	}

	return ret;
}

static void qtnf_tune_pcie_mps(struct pci_dev *pdev)
{
	struct pci_dev *parent;
	int mps_p, mps_o, mps_m, mps;
	int ret;

	/* current mps */
	mps_o = pcie_get_mps(pdev);

	/* maximum supported mps */
	mps_m = 128 << pdev->pcie_mpss;

	/* suggested new mps value */
	mps = mps_m;

	if (pdev->bus && pdev->bus->self) {
		/* parent (bus) mps */
		parent = pdev->bus->self;

		if (pci_is_pcie(parent)) {
			mps_p = pcie_get_mps(parent);
			mps = min(mps_m, mps_p);
		}
	}

	ret = pcie_set_mps(pdev, mps);
	if (ret) {
		pr_err("failed to set mps to %d, keep using current %d\n",
		       mps, mps_o);
		return;
	}

	pr_debug("set mps to %d (was %d, max %d)\n", mps, mps_o, mps_m);
}

static void qtnf_pcie_init_irq(struct qtnf_pcie_bus_priv *priv, bool use_msi)
{
	struct pci_dev *pdev = priv->pdev;

	/* fall back to legacy INTx interrupts by default */
	priv->msi_enabled = 0;

	/* check if MSI capability is available */
	if (use_msi) {
		if (!pci_enable_msi(pdev)) {
			pr_debug("enabled MSI interrupt\n");
			priv->msi_enabled = 1;
		} else {
			pr_warn("failed to enable MSI interrupts");
		}
	}

	if (!priv->msi_enabled) {
		pr_warn("legacy PCIE interrupts enabled\n");
		pci_intx(pdev, 1);
	}
}

static void __iomem *qtnf_map_bar(struct pci_dev *pdev, u8 index)
{
	void __iomem *vaddr;
	dma_addr_t busaddr;
	size_t len;
	int ret;

	ret = pcim_iomap_regions(pdev, 1 << index, "qtnfmac_pcie");
	if (ret)
		return IOMEM_ERR_PTR(ret);

	busaddr = pci_resource_start(pdev, index);
	len = pci_resource_len(pdev, index);
	vaddr = pcim_iomap_table(pdev)[index];
	if (!vaddr)
		return IOMEM_ERR_PTR(-ENOMEM);

	pr_debug("BAR%u vaddr=0x%p busaddr=%pad len=%u\n",
		 index, vaddr, &busaddr, (int)len);

	return vaddr;
}

static void qtnf_pcie_control_rx_callback(void *arg, const u8 __iomem *buf,
					  size_t len)
{
	struct qtnf_pcie_bus_priv *priv = arg;
	struct qtnf_bus *bus = pci_get_drvdata(priv->pdev);
	struct sk_buff *skb;

	if (unlikely(len == 0)) {
		pr_warn("zero length packet received\n");
		return;
	}

	skb = __dev_alloc_skb(len, GFP_KERNEL);

	if (unlikely(!skb)) {
		pr_err("failed to allocate skb\n");
		return;
	}

	memcpy_fromio(skb_put(skb, len), buf, len);

	qtnf_trans_handle_rx_ctl_packet(bus, skb);
}

void qtnf_pcie_init_shm_ipc(struct qtnf_pcie_bus_priv *priv,
			    struct qtnf_shm_ipc_region __iomem *ipc_tx_reg,
			    struct qtnf_shm_ipc_region __iomem *ipc_rx_reg,
			    const struct qtnf_shm_ipc_int *ipc_int)
{
	const struct qtnf_shm_ipc_rx_callback rx_callback = {
					qtnf_pcie_control_rx_callback, priv };

	qtnf_shm_ipc_init(&priv->shm_ipc_ep_in, QTNF_SHM_IPC_OUTBOUND,
			  ipc_tx_reg, priv->workqueue,
			  ipc_int, &rx_callback);
	qtnf_shm_ipc_init(&priv->shm_ipc_ep_out, QTNF_SHM_IPC_INBOUND,
			  ipc_rx_reg, priv->workqueue,
			  ipc_int, &rx_callback);
}

static int qtnf_pcie_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct qtnf_pcie_bus_priv *pcie_priv;
	struct qtnf_bus *bus;
	void __iomem *sysctl_bar;
	void __iomem *epmem_bar;
	void __iomem *dmareg_bar;
	unsigned int chipid;
	int ret;

	if (!pci_is_pcie(pdev)) {
		pr_err("device %s is not PCI Express\n", pci_name(pdev));
		return -EIO;
	}

	qtnf_tune_pcie_mps(pdev);

	ret = pcim_enable_device(pdev);
	if (ret) {
		pr_err("failed to init PCI device %x\n", pdev->device);
		return ret;
	}

	pci_set_master(pdev);

	sysctl_bar = qtnf_map_bar(pdev, QTN_SYSCTL_BAR);
	if (IS_ERR(sysctl_bar)) {
		pr_err("failed to map BAR%u\n", QTN_SYSCTL_BAR);
		return ret;
	}

	dmareg_bar = qtnf_map_bar(pdev, QTN_DMA_BAR);
	if (IS_ERR(dmareg_bar)) {
		pr_err("failed to map BAR%u\n", QTN_DMA_BAR);
		return ret;
	}

	epmem_bar = qtnf_map_bar(pdev, QTN_SHMEM_BAR);
	if (IS_ERR(epmem_bar)) {
		pr_err("failed to map BAR%u\n", QTN_SHMEM_BAR);
		return ret;
	}

	chipid = qtnf_chip_id_get(sysctl_bar);

	pr_info("identified device: %s\n", qtnf_chipid_to_string(chipid));

	switch (chipid) {
	case QTN_CHIP_ID_PEARL:
	case QTN_CHIP_ID_PEARL_B:
	case QTN_CHIP_ID_PEARL_C:
		bus = qtnf_pcie_pearl_alloc(pdev);
		break;
	case QTN_CHIP_ID_TOPAZ:
		bus = qtnf_pcie_topaz_alloc(pdev);
		break;
	default:
		pr_err("unsupported chip ID 0x%x\n", chipid);
		return -ENOTSUPP;
	}

	if (!bus)
		return -ENOMEM;

	pcie_priv = get_bus_priv(bus);
	pci_set_drvdata(pdev, bus);
	bus->dev = &pdev->dev;
	bus->fw_state = QTNF_FW_STATE_DETACHED;
	pcie_priv->pdev = pdev;
	pcie_priv->tx_stopped = 0;
	pcie_priv->rx_bd_num = rx_bd_size_param;
	pcie_priv->flashboot = flashboot;

	if (fw_blksize_param > QTN_PCIE_MAX_FW_BUFSZ)
		pcie_priv->fw_blksize =  QTN_PCIE_MAX_FW_BUFSZ;
	else
		pcie_priv->fw_blksize = fw_blksize_param;

	mutex_init(&bus->bus_lock);
	spin_lock_init(&pcie_priv->tx_lock);
	spin_lock_init(&pcie_priv->tx_reclaim_lock);

	pcie_priv->tx_full_count = 0;
	pcie_priv->tx_done_count = 0;
	pcie_priv->pcie_irq_count = 0;
	pcie_priv->tx_reclaim_done = 0;
	pcie_priv->tx_reclaim_req = 0;
	pcie_priv->tx_eapol = 0;

	pcie_priv->workqueue = create_singlethread_workqueue("QTNF_PCIE");
	if (!pcie_priv->workqueue) {
		pr_err("failed to alloc bus workqueue\n");
		return -ENODEV;
	}

	ret = dma_set_mask_and_coherent(&pdev->dev,
					pcie_priv->dma_mask_get_cb());
	if (ret) {
		pr_err("PCIE DMA coherent mask init failed 0x%llx\n",
		       pcie_priv->dma_mask_get_cb());
		goto error;
	}

	init_dummy_netdev(&bus->mux_dev);
	qtnf_pcie_init_irq(pcie_priv, use_msi);
	pcie_priv->sysctl_bar = sysctl_bar;
	pcie_priv->dmareg_bar = dmareg_bar;
	pcie_priv->epmem_bar = epmem_bar;
	pci_save_state(pdev);

	ret = pcie_priv->probe_cb(bus, tx_bd_size_param);
	if (ret)
		goto error;

	qtnf_pcie_bringup_fw_async(bus);
	return 0;

error:
	flush_workqueue(pcie_priv->workqueue);
	destroy_workqueue(pcie_priv->workqueue);
	pci_set_drvdata(pdev, NULL);
	return ret;
}

static void qtnf_pcie_free_shm_ipc(struct qtnf_pcie_bus_priv *priv)
{
	qtnf_shm_ipc_free(&priv->shm_ipc_ep_in);
	qtnf_shm_ipc_free(&priv->shm_ipc_ep_out);
}

static void qtnf_pcie_remove(struct pci_dev *dev)
{
	struct qtnf_pcie_bus_priv *priv;
	struct qtnf_bus *bus;

	bus = pci_get_drvdata(dev);
	if (!bus)
		return;

	priv = get_bus_priv(bus);

	cancel_work_sync(&bus->fw_work);

	if (qtnf_fw_is_attached(bus))
		qtnf_core_detach(bus);

	netif_napi_del(&bus->mux_napi);
	flush_workqueue(priv->workqueue);
	destroy_workqueue(priv->workqueue);
	tasklet_kill(&priv->reclaim_tq);

	qtnf_pcie_free_shm_ipc(priv);
	qtnf_debugfs_remove(bus);
	priv->remove_cb(bus);
	pci_set_drvdata(priv->pdev, NULL);
}

#ifdef CONFIG_PM_SLEEP
static int qtnf_pcie_suspend(struct device *dev)
{
	struct qtnf_pcie_bus_priv *priv;
	struct qtnf_bus *bus;

	bus = dev_get_drvdata(dev);
	if (!bus)
		return -EFAULT;

	priv = get_bus_priv(bus);
	return priv->suspend_cb(bus);
}

static int qtnf_pcie_resume(struct device *dev)
{
	struct qtnf_pcie_bus_priv *priv;
	struct qtnf_bus *bus;

	bus = dev_get_drvdata(dev);
	if (!bus)
		return -EFAULT;

	priv = get_bus_priv(bus);
	return priv->resume_cb(bus);
}

/* Power Management Hooks */
static SIMPLE_DEV_PM_OPS(qtnf_pcie_pm_ops, qtnf_pcie_suspend,
			 qtnf_pcie_resume);
#endif

static const struct pci_device_id qtnf_pcie_devid_table[] = {
	{
		PCIE_VENDOR_ID_QUANTENNA, PCIE_DEVICE_ID_QSR,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0,
	},
	{ },
};

MODULE_DEVICE_TABLE(pci, qtnf_pcie_devid_table);

static struct pci_driver qtnf_pcie_drv_data = {
	.name = DRV_NAME,
	.id_table = qtnf_pcie_devid_table,
	.probe = qtnf_pcie_probe,
	.remove = qtnf_pcie_remove,
#ifdef CONFIG_PM_SLEEP
	.driver = {
		.pm = &qtnf_pcie_pm_ops,
	},
#endif
};

static int __init qtnf_pcie_register(void)
{
	return pci_register_driver(&qtnf_pcie_drv_data);
}

static void __exit qtnf_pcie_exit(void)
{
	pci_unregister_driver(&qtnf_pcie_drv_data);
}

module_init(qtnf_pcie_register);
module_exit(qtnf_pcie_exit);

MODULE_AUTHOR("Quantenna Communications");
MODULE_DESCRIPTION("Quantenna PCIe bus driver for 802.11 wireless LAN.");
MODULE_LICENSE("GPL");
