// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
/* Copyright (C) 2015-2018 Netronome Systems, Inc. */

/*
 * nfp_netvf_main.c
 * Netronome virtual function network device driver: Main entry point
 * Author: Jason McMullan <jason.mcmullan@netronome.com>
 *         Rolf Neugebauer <rolf.neugebauer@netronome.com>
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/etherdevice.h>

#include "nfpcore/nfp_dev.h"
#include "nfp_net_ctrl.h"
#include "nfp_net.h"
#include "nfp_main.h"

/**
 * struct nfp_net_vf - NFP VF-specific device structure
 * @nn:		NFP Net structure for this device
 * @irq_entries: Pre-allocated array of MSI-X entries
 * @q_bar:	Pointer to mapped QC memory (NULL if TX/RX mapped directly)
 * @ddir:	Per-device debugfs directory
 */
struct nfp_net_vf {
	struct nfp_net *nn;

	struct msix_entry irq_entries[NFP_NET_NON_Q_VECTORS +
				      NFP_NET_MAX_TX_RINGS];
	u8 __iomem *q_bar;

	struct dentry *ddir;
};

static const char nfp_net_driver_name[] = "nfp_netvf";

static const struct pci_device_id nfp_netvf_pci_device_ids[] = {
	{ PCI_VENDOR_ID_NETRONOME, PCI_DEVICE_ID_NFP3800_VF,
	  PCI_VENDOR_ID_NETRONOME, PCI_ANY_ID,
	  PCI_ANY_ID, 0, NFP_DEV_NFP3800_VF,
	},
	{ PCI_VENDOR_ID_NETRONOME, PCI_DEVICE_ID_NFP6000_VF,
	  PCI_VENDOR_ID_NETRONOME, PCI_ANY_ID,
	  PCI_ANY_ID, 0, NFP_DEV_NFP6000_VF,
	},
	{ PCI_VENDOR_ID_CORIGINE, PCI_DEVICE_ID_NFP3800_VF,
	  PCI_VENDOR_ID_CORIGINE, PCI_ANY_ID,
	  PCI_ANY_ID, 0, NFP_DEV_NFP3800_VF,
	},
	{ PCI_VENDOR_ID_CORIGINE, PCI_DEVICE_ID_NFP6000_VF,
	  PCI_VENDOR_ID_CORIGINE, PCI_ANY_ID,
	  PCI_ANY_ID, 0, NFP_DEV_NFP6000_VF,
	},
	{ 0, } /* Required last entry. */
};
MODULE_DEVICE_TABLE(pci, nfp_netvf_pci_device_ids);

static void nfp_netvf_get_mac_addr(struct nfp_net *nn)
{
	u8 mac_addr[ETH_ALEN];

	put_unaligned_be32(nn_readl(nn, NFP_NET_CFG_MACADDR + 0), &mac_addr[0]);
	put_unaligned_be16(nn_readw(nn, NFP_NET_CFG_MACADDR + 6), &mac_addr[4]);

	if (!is_valid_ether_addr(mac_addr)) {
		eth_hw_addr_random(nn->dp.netdev);
		return;
	}

	eth_hw_addr_set(nn->dp.netdev, mac_addr);
	ether_addr_copy(nn->dp.netdev->perm_addr, mac_addr);
}

static int nfp_netvf_pci_probe(struct pci_dev *pdev,
			       const struct pci_device_id *pci_id)
{
	const struct nfp_dev_info *dev_info;
	struct nfp_net_fw_version fw_ver;
	int max_tx_rings, max_rx_rings;
	u32 tx_bar_off, rx_bar_off;
	u32 tx_bar_sz, rx_bar_sz;
	int tx_bar_no, rx_bar_no;
	struct nfp_net_vf *vf;
	unsigned int num_irqs;
	u8 __iomem *ctrl_bar;
	struct nfp_net *nn;
	u32 startq;
	int stride;
	int err;

	dev_info = &nfp_dev_info[pci_id->driver_data];

	vf = kzalloc(sizeof(*vf), GFP_KERNEL);
	if (!vf)
		return -ENOMEM;
	pci_set_drvdata(pdev, vf);

	err = pci_enable_device_mem(pdev);
	if (err)
		goto err_free_vf;

	err = pci_request_regions(pdev, nfp_net_driver_name);
	if (err) {
		dev_err(&pdev->dev, "Unable to allocate device memory.\n");
		goto err_pci_disable;
	}

	pci_set_master(pdev);

	err = dma_set_mask_and_coherent(&pdev->dev, dev_info->dma_mask);
	if (err)
		goto err_pci_regions;

	/* Map the Control BAR.
	 *
	 * Irrespective of the advertised BAR size we only map the
	 * first NFP_NET_CFG_BAR_SZ of the BAR.  This keeps the code
	 * the identical for PF and VF drivers.
	 */
	ctrl_bar = ioremap(pci_resource_start(pdev, NFP_NET_CTRL_BAR),
				   NFP_NET_CFG_BAR_SZ);
	if (!ctrl_bar) {
		dev_err(&pdev->dev,
			"Failed to map resource %d\n", NFP_NET_CTRL_BAR);
		err = -EIO;
		goto err_pci_regions;
	}

	nfp_net_get_fw_version(&fw_ver, ctrl_bar);
	if (fw_ver.extend & NFP_NET_CFG_VERSION_RESERVED_MASK ||
	    fw_ver.class != NFP_NET_CFG_VERSION_CLASS_GENERIC) {
		dev_err(&pdev->dev, "Unknown Firmware ABI %d.%d.%d.%d\n",
			fw_ver.extend, fw_ver.class,
			fw_ver.major, fw_ver.minor);
		err = -EINVAL;
		goto err_ctrl_unmap;
	}

	/* Determine stride */
	if (nfp_net_fw_ver_eq(&fw_ver, 0, 0, 0, 1)) {
		stride = 2;
		tx_bar_no = NFP_NET_Q0_BAR;
		rx_bar_no = NFP_NET_Q1_BAR;
		dev_warn(&pdev->dev, "OBSOLETE Firmware detected - VF isolation not available\n");
	} else {
		switch (fw_ver.major) {
		case 1 ... 5:
			stride = 4;
			tx_bar_no = NFP_NET_Q0_BAR;
			rx_bar_no = tx_bar_no;
			break;
		default:
			dev_err(&pdev->dev, "Unsupported Firmware ABI %d.%d.%d.%d\n",
				fw_ver.extend, fw_ver.class,
				fw_ver.major, fw_ver.minor);
			err = -EINVAL;
			goto err_ctrl_unmap;
		}
	}

	/* Find out how many rings are supported */
	max_tx_rings = readl(ctrl_bar + NFP_NET_CFG_MAX_TXRINGS);
	max_rx_rings = readl(ctrl_bar + NFP_NET_CFG_MAX_RXRINGS);

	tx_bar_sz = NFP_QCP_QUEUE_ADDR_SZ * max_tx_rings * stride;
	rx_bar_sz = NFP_QCP_QUEUE_ADDR_SZ * max_rx_rings * stride;

	/* Sanity checks */
	if (tx_bar_sz > pci_resource_len(pdev, tx_bar_no)) {
		dev_err(&pdev->dev,
			"TX BAR too small for number of TX rings. Adjusting\n");
		tx_bar_sz = pci_resource_len(pdev, tx_bar_no);
		max_tx_rings = (tx_bar_sz / NFP_QCP_QUEUE_ADDR_SZ) / 2;
	}
	if (rx_bar_sz > pci_resource_len(pdev, rx_bar_no)) {
		dev_err(&pdev->dev,
			"RX BAR too small for number of RX rings. Adjusting\n");
		rx_bar_sz = pci_resource_len(pdev, rx_bar_no);
		max_rx_rings = (rx_bar_sz / NFP_QCP_QUEUE_ADDR_SZ) / 2;
	}

	startq = readl(ctrl_bar + NFP_NET_CFG_START_TXQ);
	tx_bar_off = nfp_qcp_queue_offset(dev_info, startq);
	startq = readl(ctrl_bar + NFP_NET_CFG_START_RXQ);
	rx_bar_off = nfp_qcp_queue_offset(dev_info, startq);

	/* Allocate and initialise the netdev */
	nn = nfp_net_alloc(pdev, dev_info, ctrl_bar, true,
			   max_tx_rings, max_rx_rings);
	if (IS_ERR(nn)) {
		err = PTR_ERR(nn);
		goto err_ctrl_unmap;
	}
	vf->nn = nn;

	nn->dp.is_vf = 1;
	nn->stride_tx = stride;
	nn->stride_rx = stride;

	if (rx_bar_no == tx_bar_no) {
		u32 bar_off, bar_sz;
		resource_size_t map_addr;

		/* Make a single overlapping BAR mapping */
		if (tx_bar_off < rx_bar_off)
			bar_off = tx_bar_off;
		else
			bar_off = rx_bar_off;

		if ((tx_bar_off + tx_bar_sz) > (rx_bar_off + rx_bar_sz))
			bar_sz = (tx_bar_off + tx_bar_sz) - bar_off;
		else
			bar_sz = (rx_bar_off + rx_bar_sz) - bar_off;

		map_addr = pci_resource_start(pdev, tx_bar_no) + bar_off;
		vf->q_bar = ioremap(map_addr, bar_sz);
		if (!vf->q_bar) {
			nn_err(nn, "Failed to map resource %d\n", tx_bar_no);
			err = -EIO;
			goto err_netdev_free;
		}

		/* TX queues */
		nn->tx_bar = vf->q_bar + (tx_bar_off - bar_off);
		/* RX queues */
		nn->rx_bar = vf->q_bar + (rx_bar_off - bar_off);
	} else {
		resource_size_t map_addr;

		/* TX queues */
		map_addr = pci_resource_start(pdev, tx_bar_no) + tx_bar_off;
		nn->tx_bar = ioremap(map_addr, tx_bar_sz);
		if (!nn->tx_bar) {
			nn_err(nn, "Failed to map resource %d\n", tx_bar_no);
			err = -EIO;
			goto err_netdev_free;
		}

		/* RX queues */
		map_addr = pci_resource_start(pdev, rx_bar_no) + rx_bar_off;
		nn->rx_bar = ioremap(map_addr, rx_bar_sz);
		if (!nn->rx_bar) {
			nn_err(nn, "Failed to map resource %d\n", rx_bar_no);
			err = -EIO;
			goto err_unmap_tx;
		}
	}

	nfp_netvf_get_mac_addr(nn);

	num_irqs = nfp_net_irqs_alloc(pdev, vf->irq_entries,
				      NFP_NET_MIN_VNIC_IRQS,
				      NFP_NET_NON_Q_VECTORS +
				      nn->dp.num_r_vecs);
	if (!num_irqs) {
		nn_warn(nn, "Unable to allocate MSI-X Vectors. Exiting\n");
		err = -EIO;
		goto err_unmap_rx;
	}
	nfp_net_irqs_assign(nn, vf->irq_entries, num_irqs);

	err = nfp_net_init(nn);
	if (err)
		goto err_irqs_disable;

	nfp_net_info(nn);
	vf->ddir = nfp_net_debugfs_device_add(pdev);
	nfp_net_debugfs_vnic_add(nn, vf->ddir);

	return 0;

err_irqs_disable:
	nfp_net_irqs_disable(pdev);
err_unmap_rx:
	if (!vf->q_bar)
		iounmap(nn->rx_bar);
err_unmap_tx:
	if (!vf->q_bar)
		iounmap(nn->tx_bar);
	else
		iounmap(vf->q_bar);
err_netdev_free:
	nfp_net_free(nn);
err_ctrl_unmap:
	iounmap(ctrl_bar);
err_pci_regions:
	pci_release_regions(pdev);
err_pci_disable:
	pci_disable_device(pdev);
err_free_vf:
	pci_set_drvdata(pdev, NULL);
	kfree(vf);
	return err;
}

static void nfp_netvf_pci_remove(struct pci_dev *pdev)
{
	struct nfp_net_vf *vf;
	struct nfp_net *nn;

	vf = pci_get_drvdata(pdev);
	if (!vf)
		return;

	nn = vf->nn;

	/* Note, the order is slightly different from above as we need
	 * to keep the nn pointer around till we have freed everything.
	 */
	nfp_net_debugfs_dir_clean(&nn->debugfs_dir);
	nfp_net_debugfs_dir_clean(&vf->ddir);

	nfp_net_clean(nn);

	nfp_net_irqs_disable(pdev);

	if (!vf->q_bar) {
		iounmap(nn->rx_bar);
		iounmap(nn->tx_bar);
	} else {
		iounmap(vf->q_bar);
	}
	iounmap(nn->dp.ctrl_bar);

	nfp_net_free(nn);

	pci_release_regions(pdev);
	pci_disable_device(pdev);

	pci_set_drvdata(pdev, NULL);
	kfree(vf);
}

struct pci_driver nfp_netvf_pci_driver = {
	.name        = nfp_net_driver_name,
	.id_table    = nfp_netvf_pci_device_ids,
	.probe       = nfp_netvf_pci_probe,
	.remove      = nfp_netvf_pci_remove,
	.shutdown    = nfp_netvf_pci_remove,
};
