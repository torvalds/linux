/*
 * Copyright (C) 2015-2017 Netronome Systems, Inc.
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
 * nfp_net_main.c
 * Netronome network device driver: Main entry point
 * Authors: Jakub Kicinski <jakub.kicinski@netronome.com>
 *          Alejandro Lucero <alejandro.lucero@netronome.com>
 *          Jason McMullan <jason.mcmullan@netronome.com>
 *          Rolf Neugebauer <rolf.neugebauer@netronome.com>
 */

#include <linux/etherdevice.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/pci_regs.h>
#include <linux/msi.h>
#include <linux/random.h>

#include "nfpcore/nfp.h"
#include "nfpcore/nfp_cpp.h"
#include "nfpcore/nfp_nffw.h"
#include "nfpcore/nfp_nsp_eth.h"
#include "nfpcore/nfp6000_pcie.h"

#include "nfp_net_ctrl.h"
#include "nfp_net.h"
#include "nfp_main.h"

#define NFP_PF_CSR_SLICE_SIZE	(32 * 1024)

static int nfp_is_ready(struct nfp_cpp *cpp)
{
	const char *cp;
	long state;
	int err;

	cp = nfp_hwinfo_lookup(cpp, "board.state");
	if (!cp)
		return 0;

	err = kstrtol(cp, 0, &state);
	if (err < 0)
		return 0;

	return state == 15;
}

/**
 * nfp_net_map_area() - Help function to map an area
 * @cpp:    NFP CPP handler
 * @name:   Name for the area
 * @target: CPP target
 * @addr:   CPP address
 * @size:   Size of the area
 * @area:   Area handle (returned).
 *
 * This function is primarily to simplify the code in the main probe
 * function. To undo the effect of this functions call
 * @nfp_cpp_area_release_free(*area);
 *
 * Return: Pointer to memory mapped area or ERR_PTR
 */
static u8 __iomem *nfp_net_map_area(struct nfp_cpp *cpp,
				    const char *name, int isl, int target,
				    unsigned long long addr, unsigned long size,
				    struct nfp_cpp_area **area)
{
	u8 __iomem *res;
	u32 dest;
	int err;

	dest = NFP_CPP_ISLAND_ID(target, NFP_CPP_ACTION_RW, 0, isl);

	*area = nfp_cpp_area_alloc_with_name(cpp, dest, name, addr, size);
	if (!*area) {
		err = -EIO;
		goto err_area;
	}

	err = nfp_cpp_area_acquire(*area);
	if (err < 0)
		goto err_acquire;

	res = nfp_cpp_area_iomem(*area);
	if (!res) {
		err = -EIO;
		goto err_map;
	}

	return res;

err_map:
	nfp_cpp_area_release(*area);
err_acquire:
	nfp_cpp_area_free(*area);
err_area:
	return (u8 __iomem *)ERR_PTR(err);
}

static void
nfp_net_get_mac_addr_hwinfo(struct nfp_net *nn, struct nfp_cpp *cpp,
			    unsigned int id)
{
	u8 mac_addr[ETH_ALEN];
	const char *mac_str;
	char name[32];

	snprintf(name, sizeof(name), "eth%d.mac", id);

	mac_str = nfp_hwinfo_lookup(cpp, name);
	if (!mac_str) {
		dev_warn(&nn->pdev->dev,
			 "Can't lookup MAC address. Generate\n");
		eth_hw_addr_random(nn->netdev);
		return;
	}

	if (sscanf(mac_str, "%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx",
		   &mac_addr[0], &mac_addr[1], &mac_addr[2],
		   &mac_addr[3], &mac_addr[4], &mac_addr[5]) != 6) {
		dev_warn(&nn->pdev->dev,
			 "Can't parse MAC address (%s). Generate.\n", mac_str);
		eth_hw_addr_random(nn->netdev);
		return;
	}

	ether_addr_copy(nn->netdev->dev_addr, mac_addr);
	ether_addr_copy(nn->netdev->perm_addr, mac_addr);
}

/**
 * nfp_net_get_mac_addr() - Get the MAC address.
 * @nn:       NFP Network structure
 * @pf:	      NFP PF device structure
 * @id:	      NFP port id
 *
 * First try to get the MAC address from NSP ETH table. If that
 * fails try HWInfo.  As a last resort generate a random address.
 */
static void
nfp_net_get_mac_addr(struct nfp_net *nn, struct nfp_pf *pf, unsigned int id)
{
	int i;

	for (i = 0; pf->eth_tbl && i < pf->eth_tbl->count; i++)
		if (pf->eth_tbl->ports[i].eth_index == id) {
			const u8 *mac_addr = pf->eth_tbl->ports[i].mac_addr;

			ether_addr_copy(nn->netdev->dev_addr, mac_addr);
			ether_addr_copy(nn->netdev->perm_addr, mac_addr);
			return;
		}

	nfp_net_get_mac_addr_hwinfo(nn, pf->cpp, id);
}

static unsigned int nfp_net_pf_get_num_ports(struct nfp_pf *pf)
{
	char name[256];
	u16 interface;
	int pcie_pf;
	int err = 0;
	u64 val;

	interface = nfp_cpp_interface(pf->cpp);
	pcie_pf = NFP_CPP_INTERFACE_UNIT_of(interface);

	snprintf(name, sizeof(name), "nfd_cfg_pf%d_num_ports", pcie_pf);

	val = nfp_rtsym_read_le(pf->cpp, name, &err);
	/* Default to one port */
	if (err) {
		if (err != -ENOENT)
			nfp_err(pf->cpp, "Unable to read adapter port count\n");
		val = 1;
	}

	return val;
}

static unsigned int
nfp_net_pf_total_qcs(struct nfp_pf *pf, void __iomem *ctrl_bar,
		     unsigned int stride, u32 start_off, u32 num_off)
{
	unsigned int i, min_qc, max_qc;

	min_qc = readl(ctrl_bar + start_off);
	max_qc = min_qc;

	for (i = 0; i < pf->num_ports; i++) {
		/* To make our lives simpler only accept configuration where
		 * queues are allocated to PFs in order (queues of PFn all have
		 * indexes lower than PFn+1).
		 */
		if (max_qc > readl(ctrl_bar + start_off))
			return 0;

		max_qc = readl(ctrl_bar + start_off);
		max_qc += readl(ctrl_bar + num_off) * stride;
		ctrl_bar += NFP_PF_CSR_SLICE_SIZE;
	}

	return max_qc - min_qc;
}

static u8 __iomem *nfp_net_pf_map_ctrl_bar(struct nfp_pf *pf)
{
	const struct nfp_rtsym *ctrl_sym;
	u8 __iomem *ctrl_bar;
	char pf_symbol[256];
	u16 interface;
	int pcie_pf;

	interface = nfp_cpp_interface(pf->cpp);
	pcie_pf = NFP_CPP_INTERFACE_UNIT_of(interface);

	snprintf(pf_symbol, sizeof(pf_symbol), "_pf%d_net_bar0", pcie_pf);

	ctrl_sym = nfp_rtsym_lookup(pf->cpp, pf_symbol);
	if (!ctrl_sym) {
		dev_err(&pf->pdev->dev,
			"Failed to find PF BAR0 symbol %s\n", pf_symbol);
		return NULL;
	}

	if (ctrl_sym->size < pf->num_ports * NFP_PF_CSR_SLICE_SIZE) {
		dev_err(&pf->pdev->dev,
			"PF BAR0 too small to contain %d ports\n",
			pf->num_ports);
		return NULL;
	}

	ctrl_bar = nfp_net_map_area(pf->cpp, "net.ctrl",
				    ctrl_sym->domain, ctrl_sym->target,
				    ctrl_sym->addr, ctrl_sym->size,
				    &pf->ctrl_area);
	if (IS_ERR(ctrl_bar)) {
		dev_err(&pf->pdev->dev, "Failed to map PF BAR0: %ld\n",
			PTR_ERR(ctrl_bar));
		return NULL;
	}

	return ctrl_bar;
}

static void nfp_net_pf_free_netdevs(struct nfp_pf *pf)
{
	struct nfp_net *nn;

	while (!list_empty(&pf->ports)) {
		nn = list_first_entry(&pf->ports, struct nfp_net, port_list);
		list_del(&nn->port_list);

		nfp_net_netdev_free(nn);
	}
}

static struct nfp_net *
nfp_net_pf_alloc_port_netdev(struct nfp_pf *pf, void __iomem *ctrl_bar,
			     void __iomem *tx_bar, void __iomem *rx_bar,
			     int stride, struct nfp_net_fw_version *fw_ver)
{
	u32 n_tx_rings, n_rx_rings;
	struct nfp_net *nn;

	n_tx_rings = readl(ctrl_bar + NFP_NET_CFG_MAX_TXRINGS);
	n_rx_rings = readl(ctrl_bar + NFP_NET_CFG_MAX_RXRINGS);

	/* Allocate and initialise the netdev */
	nn = nfp_net_netdev_alloc(pf->pdev, n_tx_rings, n_rx_rings);
	if (IS_ERR(nn))
		return nn;

	nn->cpp = pf->cpp;
	nn->fw_ver = *fw_ver;
	nn->ctrl_bar = ctrl_bar;
	nn->tx_bar = tx_bar;
	nn->rx_bar = rx_bar;
	nn->is_vf = 0;
	nn->stride_rx = stride;
	nn->stride_tx = stride;

	return nn;
}

static int
nfp_net_pf_init_port_netdev(struct nfp_pf *pf, struct nfp_net *nn,
			    unsigned int id)
{
	int err;

	/* Get MAC address */
	nfp_net_get_mac_addr(nn, pf, id);

	/* Get ME clock frequency from ctrl BAR
	 * XXX for now frequency is hardcoded until we figure out how
	 * to get the value from nfp-hwinfo into ctrl bar
	 */
	nn->me_freq_mhz = 1200;

	err = nfp_net_netdev_init(nn->netdev);
	if (err)
		return err;

	nfp_net_debugfs_port_add(nn, pf->ddir, id);

	nfp_net_info(nn);

	return 0;
}

static int
nfp_net_pf_alloc_netdevs(struct nfp_pf *pf, void __iomem *ctrl_bar,
			 void __iomem *tx_bar, void __iomem *rx_bar,
			 int stride, struct nfp_net_fw_version *fw_ver)
{
	u32 prev_tx_base, prev_rx_base, tgt_tx_base, tgt_rx_base;
	struct nfp_net *nn;
	unsigned int i;
	int err;

	prev_tx_base = readl(ctrl_bar + NFP_NET_CFG_START_TXQ);
	prev_rx_base = readl(ctrl_bar + NFP_NET_CFG_START_RXQ);

	for (i = 0; i < pf->num_ports; i++) {
		tgt_tx_base = readl(ctrl_bar + NFP_NET_CFG_START_TXQ);
		tgt_rx_base = readl(ctrl_bar + NFP_NET_CFG_START_RXQ);
		tx_bar += (tgt_tx_base - prev_tx_base) * NFP_QCP_QUEUE_ADDR_SZ;
		rx_bar += (tgt_rx_base - prev_rx_base) * NFP_QCP_QUEUE_ADDR_SZ;
		prev_tx_base = tgt_tx_base;
		prev_rx_base = tgt_rx_base;

		nn = nfp_net_pf_alloc_port_netdev(pf, ctrl_bar, tx_bar, rx_bar,
						  stride, fw_ver);
		if (IS_ERR(nn)) {
			err = PTR_ERR(nn);
			goto err_free_prev;
		}
		list_add_tail(&nn->port_list, &pf->ports);

		ctrl_bar += NFP_PF_CSR_SLICE_SIZE;
	}

	return 0;

err_free_prev:
	nfp_net_pf_free_netdevs(pf);
	return err;
}

static int
nfp_net_pf_spawn_netdevs(struct nfp_pf *pf,
			 void __iomem *ctrl_bar, void __iomem *tx_bar,
			 void __iomem *rx_bar, int stride,
			 struct nfp_net_fw_version *fw_ver)
{
	unsigned int id, wanted_irqs, num_irqs, ports_left, irqs_left;
	struct nfp_net *nn;
	int err;

	/* Allocate the netdevs and do basic init */
	err = nfp_net_pf_alloc_netdevs(pf, ctrl_bar, tx_bar, rx_bar,
				       stride, fw_ver);
	if (err)
		return err;

	/* Get MSI-X vectors */
	wanted_irqs = 0;
	list_for_each_entry(nn, &pf->ports, port_list)
		wanted_irqs += NFP_NET_NON_Q_VECTORS + nn->num_r_vecs;
	pf->irq_entries = kcalloc(wanted_irqs, sizeof(*pf->irq_entries),
				  GFP_KERNEL);
	if (!pf->irq_entries) {
		err = -ENOMEM;
		goto err_nn_free;
	}

	num_irqs = nfp_net_irqs_alloc(pf->pdev, pf->irq_entries,
				      NFP_NET_MIN_PORT_IRQS * pf->num_ports,
				      wanted_irqs);
	if (!num_irqs) {
		nn_warn(nn, "Unable to allocate MSI-X Vectors. Exiting\n");
		err = -ENOMEM;
		goto err_vec_free;
	}

	/* Distribute IRQs to ports */
	irqs_left = num_irqs;
	ports_left = pf->num_ports;
	list_for_each_entry(nn, &pf->ports, port_list) {
		unsigned int n;

		n = DIV_ROUND_UP(irqs_left, ports_left);
		nfp_net_irqs_assign(nn, &pf->irq_entries[num_irqs - irqs_left],
				    n);
		irqs_left -= n;
		ports_left--;
	}

	/* Finish netdev init and register */
	id = 0;
	list_for_each_entry(nn, &pf->ports, port_list) {
		err = nfp_net_pf_init_port_netdev(pf, nn, id);
		if (err)
			goto err_prev_deinit;

		id++;
	}

	return 0;

err_prev_deinit:
	list_for_each_entry_continue_reverse(nn, &pf->ports, port_list) {
		nfp_net_debugfs_dir_clean(&nn->debugfs_dir);
		nfp_net_netdev_clean(nn->netdev);
	}
	nfp_net_irqs_disable(pf->pdev);
err_vec_free:
	kfree(pf->irq_entries);
err_nn_free:
	nfp_net_pf_free_netdevs(pf);
	return err;
}

/*
 * PCI device functions
 */
int nfp_net_pci_probe(struct nfp_pf *pf)
{
	u8 __iomem *ctrl_bar, *tx_bar, *rx_bar;
	u32 total_tx_qcs, total_rx_qcs;
	struct nfp_net_fw_version fw_ver;
	u32 tx_area_sz, rx_area_sz;
	u32 start_q;
	int stride;
	int err;

	/* Verify that the board has completed initialization */
	if (!nfp_is_ready(pf->cpp)) {
		nfp_err(pf->cpp, "NFP is not ready for NIC operation.\n");
		return -EINVAL;
	}

	pf->num_ports = nfp_net_pf_get_num_ports(pf);

	ctrl_bar = nfp_net_pf_map_ctrl_bar(pf);
	if (!ctrl_bar)
		return pf->fw_loaded ? -EINVAL : -EPROBE_DEFER;

	nfp_net_get_fw_version(&fw_ver, ctrl_bar);
	if (fw_ver.resv || fw_ver.class != NFP_NET_CFG_VERSION_CLASS_GENERIC) {
		nfp_err(pf->cpp, "Unknown Firmware ABI %d.%d.%d.%d\n",
			fw_ver.resv, fw_ver.class, fw_ver.major, fw_ver.minor);
		err = -EINVAL;
		goto err_ctrl_unmap;
	}

	/* Determine stride */
	if (nfp_net_fw_ver_eq(&fw_ver, 0, 0, 0, 1)) {
		stride = 2;
		nfp_warn(pf->cpp, "OBSOLETE Firmware detected - VF isolation not available\n");
	} else {
		switch (fw_ver.major) {
		case 1 ... 4:
			stride = 4;
			break;
		default:
			nfp_err(pf->cpp, "Unsupported Firmware ABI %d.%d.%d.%d\n",
				fw_ver.resv, fw_ver.class,
				fw_ver.major, fw_ver.minor);
			err = -EINVAL;
			goto err_ctrl_unmap;
		}
	}

	/* Find how many QC structs need to be mapped */
	total_tx_qcs = nfp_net_pf_total_qcs(pf, ctrl_bar, stride,
					    NFP_NET_CFG_START_TXQ,
					    NFP_NET_CFG_MAX_TXRINGS);
	total_rx_qcs = nfp_net_pf_total_qcs(pf, ctrl_bar, stride,
					    NFP_NET_CFG_START_RXQ,
					    NFP_NET_CFG_MAX_RXRINGS);
	if (!total_tx_qcs || !total_rx_qcs) {
		nfp_err(pf->cpp, "Invalid PF QC configuration [%d,%d]\n",
			total_tx_qcs, total_rx_qcs);
		err = -EINVAL;
		goto err_ctrl_unmap;
	}

	tx_area_sz = NFP_QCP_QUEUE_ADDR_SZ * total_tx_qcs;
	rx_area_sz = NFP_QCP_QUEUE_ADDR_SZ * total_rx_qcs;

	/* Map TX queues */
	start_q = readl(ctrl_bar + NFP_NET_CFG_START_TXQ);
	tx_bar = nfp_net_map_area(pf->cpp, "net.tx", 0, 0,
				  NFP_PCIE_QUEUE(start_q),
				  tx_area_sz, &pf->tx_area);
	if (IS_ERR(tx_bar)) {
		nfp_err(pf->cpp, "Failed to map TX area.\n");
		err = PTR_ERR(tx_bar);
		goto err_ctrl_unmap;
	}

	/* Map RX queues */
	start_q = readl(ctrl_bar + NFP_NET_CFG_START_RXQ);
	rx_bar = nfp_net_map_area(pf->cpp, "net.rx", 0, 0,
				  NFP_PCIE_QUEUE(start_q),
				  rx_area_sz, &pf->rx_area);
	if (IS_ERR(rx_bar)) {
		nfp_err(pf->cpp, "Failed to map RX area.\n");
		err = PTR_ERR(rx_bar);
		goto err_unmap_tx;
	}

	pf->ddir = nfp_net_debugfs_device_add(pf->pdev);

	err = nfp_net_pf_spawn_netdevs(pf, ctrl_bar, tx_bar, rx_bar,
				       stride, &fw_ver);
	if (err)
		goto err_clean_ddir;

	return 0;

err_clean_ddir:
	nfp_net_debugfs_dir_clean(&pf->ddir);
	nfp_cpp_area_release_free(pf->rx_area);
err_unmap_tx:
	nfp_cpp_area_release_free(pf->tx_area);
err_ctrl_unmap:
	nfp_cpp_area_release_free(pf->ctrl_area);
	return err;
}

void nfp_net_pci_remove(struct nfp_pf *pf)
{
	struct nfp_net *nn;

	list_for_each_entry(nn, &pf->ports, port_list) {
		nfp_net_debugfs_dir_clean(&nn->debugfs_dir);

		nfp_net_netdev_clean(nn->netdev);
	}

	nfp_net_pf_free_netdevs(pf);

	nfp_net_debugfs_dir_clean(&pf->ddir);

	nfp_net_irqs_disable(pf->pdev);
	kfree(pf->irq_entries);

	nfp_cpp_area_release_free(pf->rx_area);
	nfp_cpp_area_release_free(pf->tx_area);
	nfp_cpp_area_release_free(pf->ctrl_area);
}
