/*
 * Copyright (C) 2015 Netronome Systems, Inc.
 *
 * This software is dual licensed under the GNU General License Version 2,
 * June 1991 as shown in the file COPYING in the top-level directory of this
 * source tree or the BSD 2-Clause License provided below.  You have the
 * option to license this software under the complete terms of either license.
 *
 * The BSD 2-Clause License:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      1. Redistributions of source code must retain the above
 *         copyright notice, this list of conditions and the following
 *         disclaimer.
 *
 *      2. Redistributions in binary form must reproduce the above
 *         copyright notice, this list of conditions and the following
 *         disclaimer in the documentation and/or other materials
 *         provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/*
 * nfp_netvf_main.c
 * Netronome virtual function network device driver: Main entry point
 * Author: Jason McMullan <jason.mcmullan@netronome.com>
 *         Rolf Neugebauer <rolf.neugebauer@netronome.com>
 */

#include <linux/version.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/etherdevice.h>

#include "nfp_net_ctrl.h"
#include "nfp_net.h"

const char nfp_net_driver_name[] = "nfp_netvf";
const char nfp_net_driver_version[] = "0.1";
#define PCI_DEVICE_NFP6000VF		0x6003
static const struct pci_device_id nfp_netvf_pci_device_ids[] = {
	{ PCI_VENDOR_ID_NETRONOME, PCI_DEVICE_NFP6000VF,
	  PCI_VENDOR_ID_NETRONOME, PCI_ANY_ID,
	  PCI_ANY_ID, 0,
	},
	{ 0, } /* Required last entry. */
};
MODULE_DEVICE_TABLE(pci, nfp_netvf_pci_device_ids);

static void nfp_netvf_get_mac_addr(struct nfp_net *nn)
{
	u8 mac_addr[ETH_ALEN];

	put_unaligned_be32(nn_readl(nn, NFP_NET_CFG_MACADDR + 0), &mac_addr[0]);
	/* We can't do readw for NFP-3200 compatibility */
	put_unaligned_be16(nn_readl(nn, NFP_NET_CFG_MACADDR + 4) >> 16,
			   &mac_addr[4]);

	if (!is_valid_ether_addr(mac_addr)) {
		eth_hw_addr_random(nn->netdev);
		return;
	}

	ether_addr_copy(nn->netdev->dev_addr, mac_addr);
	ether_addr_copy(nn->netdev->perm_addr, mac_addr);
}

static int nfp_netvf_pci_probe(struct pci_dev *pdev,
			       const struct pci_device_id *pci_id)
{
	struct nfp_net_fw_version fw_ver;
	int max_tx_rings, max_rx_rings;
	u32 tx_bar_off, rx_bar_off;
	u32 tx_bar_sz, rx_bar_sz;
	int tx_bar_no, rx_bar_no;
	u8 __iomem *ctrl_bar;
	struct nfp_net *nn;
	int is_nfp3200;
	u32 startq;
	int stride;
	int err;

	err = pci_enable_device_mem(pdev);
	if (err)
		return err;

	err = pci_request_regions(pdev, nfp_net_driver_name);
	if (err) {
		dev_err(&pdev->dev, "Unable to allocate device memory.\n");
		goto err_pci_disable;
	}

	switch (pdev->device) {
	case PCI_DEVICE_NFP6000VF:
		is_nfp3200 = 0;
		break;
	default:
		err = -ENODEV;
		goto err_pci_regions;
	}

	pci_set_master(pdev);

	err = dma_set_mask_and_coherent(&pdev->dev,
					DMA_BIT_MASK(NFP_NET_MAX_DMA_BITS));
	if (err)
		goto err_pci_regions;

	/* Map the Control BAR.
	 *
	 * Irrespective of the advertised BAR size we only map the
	 * first NFP_NET_CFG_BAR_SZ of the BAR.  This keeps the code
	 * the identical for PF and VF drivers.
	 */
	ctrl_bar = ioremap_nocache(pci_resource_start(pdev, NFP_NET_CTRL_BAR),
				   NFP_NET_CFG_BAR_SZ);
	if (!ctrl_bar) {
		dev_err(&pdev->dev,
			"Failed to map resource %d\n", NFP_NET_CTRL_BAR);
		err = -EIO;
		goto err_pci_regions;
	}

	nfp_net_get_fw_version(&fw_ver, ctrl_bar);
	if (fw_ver.class != NFP_NET_CFG_VERSION_CLASS_GENERIC) {
		dev_err(&pdev->dev, "Unknown Firmware ABI %d.%d.%d.%d\n",
			fw_ver.resv, fw_ver.class, fw_ver.major, fw_ver.minor);
		err = -EINVAL;
		goto err_ctrl_unmap;
	}

	/* Determine stride */
	if (nfp_net_fw_ver_eq(&fw_ver, 0, 0, 0, 0) ||
	    nfp_net_fw_ver_eq(&fw_ver, 0, 0, 0, 1) ||
	    nfp_net_fw_ver_eq(&fw_ver, 0, 0, 0x12, 0x48)) {
		stride = 2;
		tx_bar_no = NFP_NET_Q0_BAR;
		rx_bar_no = NFP_NET_Q1_BAR;
		dev_warn(&pdev->dev, "OBSOLETE Firmware detected - VF isolation not available\n");
	} else {
		switch (fw_ver.major) {
		case 1 ... 3:
			if (is_nfp3200) {
				stride = 2;
				tx_bar_no = NFP_NET_Q0_BAR;
				rx_bar_no = NFP_NET_Q1_BAR;
			} else {
				stride = 4;
				tx_bar_no = NFP_NET_Q0_BAR;
				rx_bar_no = tx_bar_no;
			}
			break;
		default:
			dev_err(&pdev->dev, "Unsupported Firmware ABI %d.%d.%d.%d\n",
				fw_ver.resv, fw_ver.class,
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

	/* XXX Implement a workaround for THB-350 here.  Ideally, we
	 * have a different PCI ID for A rev VFs.
	 */
	switch (pdev->device) {
	case PCI_DEVICE_NFP6000VF:
		startq = readl(ctrl_bar + NFP_NET_CFG_START_TXQ);
		tx_bar_off = NFP_PCIE_QUEUE(startq);
		startq = readl(ctrl_bar + NFP_NET_CFG_START_RXQ);
		rx_bar_off = NFP_PCIE_QUEUE(startq);
		break;
	default:
		err = -ENODEV;
		goto err_ctrl_unmap;
	}

	/* Allocate and initialise the netdev */
	nn = nfp_net_netdev_alloc(pdev, max_tx_rings, max_rx_rings);
	if (IS_ERR(nn)) {
		err = PTR_ERR(nn);
		goto err_ctrl_unmap;
	}

	nn->fw_ver = fw_ver;
	nn->ctrl_bar = ctrl_bar;
	nn->is_vf = 1;
	nn->is_nfp3200 = is_nfp3200;
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
		nn->q_bar = ioremap_nocache(map_addr, bar_sz);
		if (!nn->q_bar) {
			nn_err(nn, "Failed to map resource %d\n", tx_bar_no);
			err = -EIO;
			goto err_netdev_free;
		}

		/* TX queues */
		nn->tx_bar = nn->q_bar + (tx_bar_off - bar_off);
		/* RX queues */
		nn->rx_bar = nn->q_bar + (rx_bar_off - bar_off);
	} else {
		resource_size_t map_addr;

		/* TX queues */
		map_addr = pci_resource_start(pdev, tx_bar_no) + tx_bar_off;
		nn->tx_bar = ioremap_nocache(map_addr, tx_bar_sz);
		if (!nn->tx_bar) {
			nn_err(nn, "Failed to map resource %d\n", tx_bar_no);
			err = -EIO;
			goto err_netdev_free;
		}

		/* RX queues */
		map_addr = pci_resource_start(pdev, rx_bar_no) + rx_bar_off;
		nn->rx_bar = ioremap_nocache(map_addr, rx_bar_sz);
		if (!nn->rx_bar) {
			nn_err(nn, "Failed to map resource %d\n", rx_bar_no);
			err = -EIO;
			goto err_unmap_tx;
		}
	}

	nfp_netvf_get_mac_addr(nn);

	err = nfp_net_irqs_alloc(nn);
	if (!err) {
		nn_warn(nn, "Unable to allocate MSI-X Vectors. Exiting\n");
		err = -EIO;
		goto err_unmap_rx;
	}

	/* Get ME clock frequency from ctrl BAR
	 * XXX for now frequency is hardcoded until we figure out how
	 * to get the value from nfp-hwinfo into ctrl bar
	 */
	nn->me_freq_mhz = 1200;

	err = nfp_net_netdev_init(nn->netdev);
	if (err)
		goto err_irqs_disable;

	pci_set_drvdata(pdev, nn);

	nfp_net_info(nn);
	nfp_net_debugfs_adapter_add(nn);

	return 0;

err_irqs_disable:
	nfp_net_irqs_disable(nn);
err_unmap_rx:
	if (!nn->q_bar)
		iounmap(nn->rx_bar);
err_unmap_tx:
	if (!nn->q_bar)
		iounmap(nn->tx_bar);
	else
		iounmap(nn->q_bar);
err_netdev_free:
	pci_set_drvdata(pdev, NULL);
	nfp_net_netdev_free(nn);
err_ctrl_unmap:
	iounmap(ctrl_bar);
err_pci_regions:
	pci_release_regions(pdev);
err_pci_disable:
	pci_disable_device(pdev);
	return err;
}

static void nfp_netvf_pci_remove(struct pci_dev *pdev)
{
	struct nfp_net *nn = pci_get_drvdata(pdev);

	/* Note, the order is slightly different from above as we need
	 * to keep the nn pointer around till we have freed everything.
	 */
	nfp_net_debugfs_adapter_del(nn);

	nfp_net_netdev_clean(nn->netdev);

	nfp_net_irqs_disable(nn);

	if (!nn->q_bar) {
		iounmap(nn->rx_bar);
		iounmap(nn->tx_bar);
	} else {
		iounmap(nn->q_bar);
	}
	iounmap(nn->ctrl_bar);

	pci_set_drvdata(pdev, NULL);

	nfp_net_netdev_free(nn);

	pci_release_regions(pdev);
	pci_disable_device(pdev);
}

static struct pci_driver nfp_netvf_pci_driver = {
	.name        = nfp_net_driver_name,
	.id_table    = nfp_netvf_pci_device_ids,
	.probe       = nfp_netvf_pci_probe,
	.remove      = nfp_netvf_pci_remove,
};

static int __init nfp_netvf_init(void)
{
	int err;

	pr_info("%s: NFP VF Network driver, Copyright (C) 2014-2015 Netronome Systems\n",
		nfp_net_driver_name);

	nfp_net_debugfs_create();
	err = pci_register_driver(&nfp_netvf_pci_driver);
	if (err) {
		nfp_net_debugfs_destroy();
		return err;
	}

	return 0;
}

static void __exit nfp_netvf_exit(void)
{
	pci_unregister_driver(&nfp_netvf_pci_driver);
	nfp_net_debugfs_destroy();
}

module_init(nfp_netvf_init);
module_exit(nfp_netvf_exit);

MODULE_AUTHOR("Netronome Systems <oss-drivers@netronome.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("NFP VF network device driver");
