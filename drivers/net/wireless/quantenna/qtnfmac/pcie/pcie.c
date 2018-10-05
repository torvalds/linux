// SPDX-License-Identifier: GPL-2.0+
/* Copyright (c) 2018 Quantenna Communications, Inc. All rights reserved. */

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

#undef pr_fmt
#define pr_fmt(fmt)	"qtnf_pcie: %s: " fmt, __func__

#define QTN_SYSCTL_BAR	0
#define QTN_SHMEM_BAR	2
#define QTN_DMA_BAR	3

int qtnf_pcie_control_tx(struct qtnf_bus *bus, struct sk_buff *skb)
{
	struct qtnf_pcie_bus_priv *priv = get_bus_priv(bus);
	int ret;

	ret = qtnf_shm_ipc_send(&priv->shm_ipc_ep_in, skb->data, skb->len);

	if (ret == -ETIMEDOUT) {
		pr_err("EP firmware is dead\n");
		bus->fw_state = QTNF_FW_STATE_EP_DEAD;
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

void qtnf_pcie_bringup_fw_async(struct qtnf_bus *bus)
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

	seq_printf(s, "%d\n", priv->mps);

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

void qtnf_pcie_fw_boot_done(struct qtnf_bus *bus, bool boot_success,
			    const char *drv_name)
{
	struct qtnf_pcie_bus_priv *priv = get_bus_priv(bus);
	struct pci_dev *pdev = priv->pdev;
	int ret;

	if (boot_success) {
		bus->fw_state = QTNF_FW_STATE_FW_DNLD_DONE;

		ret = qtnf_core_attach(bus);
		if (ret) {
			pr_err("failed to attach core\n");
			boot_success = false;
		}
	}

	if (boot_success) {
		qtnf_debugfs_init(bus, drv_name);
		qtnf_debugfs_add_entry(bus, "mps", qtnf_dbg_mps_show);
		qtnf_debugfs_add_entry(bus, "msi_enabled", qtnf_dbg_msi_show);
		qtnf_debugfs_add_entry(bus, "shm_stats", qtnf_dbg_shm_stats);
	} else {
		bus->fw_state = QTNF_FW_STATE_DETACHED;
	}

	put_device(&pdev->dev);
}

static void qtnf_tune_pcie_mps(struct qtnf_pcie_bus_priv *priv)
{
	struct pci_dev *pdev = priv->pdev;
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
		priv->mps = mps_o;
		return;
	}

	pr_debug("set mps to %d (was %d, max %d)\n", mps, mps_o, mps_m);
	priv->mps = mps;
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

static void __iomem *qtnf_map_bar(struct qtnf_pcie_bus_priv *priv, u8 index)
{
	void __iomem *vaddr;
	dma_addr_t busaddr;
	size_t len;
	int ret;

	ret = pcim_iomap_regions(priv->pdev, 1 << index, "qtnfmac_pcie");
	if (ret)
		return IOMEM_ERR_PTR(ret);

	busaddr = pci_resource_start(priv->pdev, index);
	len = pci_resource_len(priv->pdev, index);
	vaddr = pcim_iomap_table(priv->pdev)[index];
	if (!vaddr)
		return IOMEM_ERR_PTR(-ENOMEM);

	pr_debug("BAR%u vaddr=0x%p busaddr=%pad len=%u\n",
		 index, vaddr, &busaddr, (int)len);

	return vaddr;
}

static int qtnf_pcie_init_memory(struct qtnf_pcie_bus_priv *priv)
{
	int ret = -ENOMEM;

	priv->sysctl_bar = qtnf_map_bar(priv, QTN_SYSCTL_BAR);
	if (IS_ERR(priv->sysctl_bar)) {
		pr_err("failed to map BAR%u\n", QTN_SYSCTL_BAR);
		return ret;
	}

	priv->dmareg_bar = qtnf_map_bar(priv, QTN_DMA_BAR);
	if (IS_ERR(priv->dmareg_bar)) {
		pr_err("failed to map BAR%u\n", QTN_DMA_BAR);
		return ret;
	}

	priv->epmem_bar = qtnf_map_bar(priv, QTN_SHMEM_BAR);
	if (IS_ERR(priv->epmem_bar)) {
		pr_err("failed to map BAR%u\n", QTN_SHMEM_BAR);
		return ret;
	}

	return 0;
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

int qtnf_pcie_probe(struct pci_dev *pdev, size_t priv_size,
		    const struct qtnf_bus_ops *bus_ops, u64 dma_mask,
		    bool use_msi)
{
	struct qtnf_pcie_bus_priv *pcie_priv;
	struct qtnf_bus *bus;
	int ret;

	bus = devm_kzalloc(&pdev->dev,
			   sizeof(*bus) + priv_size, GFP_KERNEL);
	if (!bus)
		return -ENOMEM;

	pcie_priv = get_bus_priv(bus);

	pci_set_drvdata(pdev, bus);
	bus->bus_ops = bus_ops;
	bus->dev = &pdev->dev;
	bus->fw_state = QTNF_FW_STATE_RESET;
	pcie_priv->pdev = pdev;
	pcie_priv->tx_stopped = 0;

	mutex_init(&bus->bus_lock);
	spin_lock_init(&pcie_priv->tx_lock);
	spin_lock_init(&pcie_priv->tx_reclaim_lock);

	pcie_priv->tx_full_count = 0;
	pcie_priv->tx_done_count = 0;
	pcie_priv->pcie_irq_count = 0;
	pcie_priv->tx_reclaim_done = 0;
	pcie_priv->tx_reclaim_req = 0;

	pcie_priv->workqueue = create_singlethread_workqueue("QTNF_PCIE");
	if (!pcie_priv->workqueue) {
		pr_err("failed to alloc bus workqueue\n");
		ret = -ENODEV;
		goto err_init;
	}

	init_dummy_netdev(&bus->mux_dev);

	if (!pci_is_pcie(pdev)) {
		pr_err("device %s is not PCI Express\n", pci_name(pdev));
		ret = -EIO;
		goto err_base;
	}

	qtnf_tune_pcie_mps(pcie_priv);

	ret = pcim_enable_device(pdev);
	if (ret) {
		pr_err("failed to init PCI device %x\n", pdev->device);
		goto err_base;
	} else {
		pr_debug("successful init of PCI device %x\n", pdev->device);
	}

	ret = dma_set_mask_and_coherent(&pdev->dev, dma_mask);
	if (ret) {
		pr_err("PCIE DMA coherent mask init failed\n");
		goto err_base;
	}

	pci_set_master(pdev);
	qtnf_pcie_init_irq(pcie_priv, use_msi);

	ret = qtnf_pcie_init_memory(pcie_priv);
	if (ret < 0) {
		pr_err("PCIE memory init failed\n");
		goto err_base;
	}

	pci_save_state(pdev);

	return 0;

err_base:
	flush_workqueue(pcie_priv->workqueue);
	destroy_workqueue(pcie_priv->workqueue);
err_init:
	pci_set_drvdata(pdev, NULL);

	return ret;
}

static void qtnf_pcie_free_shm_ipc(struct qtnf_pcie_bus_priv *priv)
{
	qtnf_shm_ipc_free(&priv->shm_ipc_ep_in);
	qtnf_shm_ipc_free(&priv->shm_ipc_ep_out);
}

void qtnf_pcie_remove(struct qtnf_bus *bus, struct qtnf_pcie_bus_priv *priv)
{
	cancel_work_sync(&bus->fw_work);

	if (bus->fw_state == QTNF_FW_STATE_ACTIVE ||
	    bus->fw_state == QTNF_FW_STATE_EP_DEAD)
		qtnf_core_detach(bus);

	netif_napi_del(&bus->mux_napi);
	flush_workqueue(priv->workqueue);
	destroy_workqueue(priv->workqueue);
	tasklet_kill(&priv->reclaim_tq);

	qtnf_pcie_free_shm_ipc(priv);
	qtnf_debugfs_remove(bus);
	pci_set_drvdata(priv->pdev, NULL);
}
