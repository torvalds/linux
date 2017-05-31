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
#include <linux/lockdep.h>
#include <linux/pci.h>
#include <linux/pci_regs.h>
#include <linux/msi.h>
#include <linux/random.h>
#include <linux/rtnetlink.h>

#include "nfpcore/nfp.h"
#include "nfpcore/nfp_cpp.h"
#include "nfpcore/nfp_nffw.h"
#include "nfpcore/nfp_nsp.h"
#include "nfpcore/nfp6000_pcie.h"
#include "nfp_app.h"
#include "nfp_net_ctrl.h"
#include "nfp_net.h"
#include "nfp_main.h"
#include "nfp_port.h"

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

/**
 * nfp_net_get_mac_addr() - Get the MAC address.
 * @nn:       NFP Network structure
 * @cpp:      NFP CPP handle
 * @id:	      NFP port id
 *
 * First try to get the MAC address from NSP ETH table. If that
 * fails try HWInfo.  As a last resort generate a random address.
 */
void
nfp_net_get_mac_addr(struct nfp_net *nn, struct nfp_cpp *cpp, unsigned int id)
{
	struct nfp_eth_table_port *eth_port;
	struct nfp_net_dp *dp = &nn->dp;
	u8 mac_addr[ETH_ALEN];
	const char *mac_str;
	char name[32];

	eth_port = __nfp_port_get_eth_port(nn->port);
	if (eth_port) {
		ether_addr_copy(dp->netdev->dev_addr, eth_port->mac_addr);
		ether_addr_copy(dp->netdev->perm_addr, eth_port->mac_addr);
		return;
	}

	snprintf(name, sizeof(name), "eth%d.mac", id);

	mac_str = nfp_hwinfo_lookup(cpp, name);
	if (!mac_str) {
		dev_warn(dp->dev, "Can't lookup MAC address. Generate\n");
		eth_hw_addr_random(dp->netdev);
		return;
	}

	if (sscanf(mac_str, "%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx",
		   &mac_addr[0], &mac_addr[1], &mac_addr[2],
		   &mac_addr[3], &mac_addr[4], &mac_addr[5]) != 6) {
		dev_warn(dp->dev,
			 "Can't parse MAC address (%s). Generate.\n", mac_str);
		eth_hw_addr_random(dp->netdev);
		return;
	}

	ether_addr_copy(dp->netdev->dev_addr, mac_addr);
	ether_addr_copy(dp->netdev->perm_addr, mac_addr);
}

struct nfp_eth_table_port *
nfp_net_find_port(struct nfp_eth_table *eth_tbl, unsigned int id)
{
	int i;

	for (i = 0; eth_tbl && i < eth_tbl->count; i++)
		if (eth_tbl->ports[i].eth_index == id)
			return &eth_tbl->ports[i];

	return NULL;
}

static int
nfp_net_pf_rtsym_read_optional(struct nfp_pf *pf, const char *format,
			       unsigned int default_val)
{
	char name[256];
	int err = 0;
	u64 val;

	snprintf(name, sizeof(name), format, nfp_cppcore_pcie_unit(pf->cpp));

	val = nfp_rtsym_read_le(pf->cpp, name, &err);
	if (err) {
		if (err == -ENOENT)
			return default_val;
		nfp_err(pf->cpp, "Unable to read symbol %s\n", name);
		return err;
	}

	return val;
}

static int nfp_net_pf_get_num_ports(struct nfp_pf *pf)
{
	return nfp_net_pf_rtsym_read_optional(pf, "nfd_cfg_pf%u_num_ports", 1);
}

static int nfp_net_pf_get_app_id(struct nfp_pf *pf)
{
	return nfp_net_pf_rtsym_read_optional(pf, "_pf%u_net_app_id",
					      NFP_APP_CORE_NIC);
}

static unsigned int
nfp_net_pf_total_qcs(struct nfp_pf *pf, void __iomem *ctrl_bar,
		     unsigned int stride, u32 start_off, u32 num_off)
{
	unsigned int i, min_qc, max_qc;

	min_qc = readl(ctrl_bar + start_off);
	max_qc = min_qc;

	for (i = 0; i < pf->max_data_vnics; i++) {
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

	snprintf(pf_symbol, sizeof(pf_symbol), "_pf%u_net_bar0",
		 nfp_cppcore_pcie_unit(pf->cpp));

	ctrl_sym = nfp_rtsym_lookup(pf->cpp, pf_symbol);
	if (!ctrl_sym) {
		dev_err(&pf->pdev->dev,
			"Failed to find PF BAR0 symbol %s\n", pf_symbol);
		return NULL;
	}

	if (ctrl_sym->size < pf->max_data_vnics * NFP_PF_CSR_SLICE_SIZE) {
		dev_err(&pf->pdev->dev,
			"PF BAR0 too small to contain %d vNICs\n",
			pf->max_data_vnics);
		return NULL;
	}

	ctrl_bar = nfp_net_map_area(pf->cpp, "net.ctrl",
				    ctrl_sym->domain, ctrl_sym->target,
				    ctrl_sym->addr, ctrl_sym->size,
				    &pf->data_vnic_bar);
	if (IS_ERR(ctrl_bar)) {
		dev_err(&pf->pdev->dev, "Failed to map PF BAR0: %ld\n",
			PTR_ERR(ctrl_bar));
		return NULL;
	}

	return ctrl_bar;
}

static void nfp_net_pf_free_vnic(struct nfp_pf *pf, struct nfp_net *nn)
{
	nfp_port_free(nn->port);
	list_del(&nn->vnic_list);
	pf->num_vnics--;
	nfp_net_free(nn);
}

static void nfp_net_pf_free_vnics(struct nfp_pf *pf)
{
	struct nfp_net *nn;

	while (!list_empty(&pf->vnics)) {
		nn = list_first_entry(&pf->vnics, struct nfp_net, vnic_list);
		nfp_net_pf_free_vnic(pf, nn);
	}
}

static struct nfp_net *
nfp_net_pf_alloc_vnic(struct nfp_pf *pf, void __iomem *ctrl_bar,
		      void __iomem *tx_bar, void __iomem *rx_bar,
		      int stride, struct nfp_net_fw_version *fw_ver,
		      unsigned int eth_id)
{
	u32 n_tx_rings, n_rx_rings;
	struct nfp_net *nn;
	int err;

	n_tx_rings = readl(ctrl_bar + NFP_NET_CFG_MAX_TXRINGS);
	n_rx_rings = readl(ctrl_bar + NFP_NET_CFG_MAX_RXRINGS);

	/* Allocate and initialise the vNIC */
	nn = nfp_net_alloc(pf->pdev, n_tx_rings, n_rx_rings);
	if (IS_ERR(nn))
		return nn;

	nn->app = pf->app;
	nn->fw_ver = *fw_ver;
	nn->dp.ctrl_bar = ctrl_bar;
	nn->tx_bar = tx_bar;
	nn->rx_bar = rx_bar;
	nn->dp.is_vf = 0;
	nn->stride_rx = stride;
	nn->stride_tx = stride;

	err = nfp_app_vnic_init(pf->app, nn, eth_id);
	if (err) {
		nfp_net_free(nn);
		return ERR_PTR(err);
	}

	pf->num_vnics++;
	list_add_tail(&nn->vnic_list, &pf->vnics);

	return nn;
}

static int
nfp_net_pf_init_vnic(struct nfp_pf *pf, struct nfp_net *nn, unsigned int id)
{
	int err;

	/* Get ME clock frequency from ctrl BAR
	 * XXX for now frequency is hardcoded until we figure out how
	 * to get the value from nfp-hwinfo into ctrl bar
	 */
	nn->me_freq_mhz = 1200;

	err = nfp_net_init(nn);
	if (err)
		return err;

	nfp_net_debugfs_vnic_add(nn, pf->ddir, id);

	if (nn->port) {
		err = nfp_devlink_port_register(pf->app, nn->port);
		if (err)
			goto err_dfs_clean;
	}

	nfp_net_info(nn);

	return 0;

err_dfs_clean:
	nfp_net_debugfs_dir_clean(&nn->debugfs_dir);
	nfp_net_clean(nn);
	return err;
}

static int
nfp_net_pf_alloc_vnics(struct nfp_pf *pf, void __iomem *ctrl_bar,
		       void __iomem *tx_bar, void __iomem *rx_bar,
		       int stride, struct nfp_net_fw_version *fw_ver)
{
	u32 prev_tx_base, prev_rx_base, tgt_tx_base, tgt_rx_base;
	struct nfp_net *nn;
	unsigned int i;
	int err;

	prev_tx_base = readl(ctrl_bar + NFP_NET_CFG_START_TXQ);
	prev_rx_base = readl(ctrl_bar + NFP_NET_CFG_START_RXQ);

	for (i = 0; i < pf->max_data_vnics; i++) {
		tgt_tx_base = readl(ctrl_bar + NFP_NET_CFG_START_TXQ);
		tgt_rx_base = readl(ctrl_bar + NFP_NET_CFG_START_RXQ);
		tx_bar += (tgt_tx_base - prev_tx_base) * NFP_QCP_QUEUE_ADDR_SZ;
		rx_bar += (tgt_rx_base - prev_rx_base) * NFP_QCP_QUEUE_ADDR_SZ;
		prev_tx_base = tgt_tx_base;
		prev_rx_base = tgt_rx_base;

		nn = nfp_net_pf_alloc_vnic(pf, ctrl_bar, tx_bar, rx_bar,
					   stride, fw_ver, i);
		if (IS_ERR(nn)) {
			err = PTR_ERR(nn);
			goto err_free_prev;
		}

		ctrl_bar += NFP_PF_CSR_SLICE_SIZE;

		/* Kill the vNIC if app init marked it as invalid */
		if (nn->port && nn->port->type == NFP_PORT_INVALID) {
			nfp_net_pf_free_vnic(pf, nn);
			continue;
		}
	}

	if (list_empty(&pf->vnics))
		return -ENODEV;

	return 0;

err_free_prev:
	nfp_net_pf_free_vnics(pf);
	return err;
}

static void nfp_net_pf_clean_vnic(struct nfp_pf *pf, struct nfp_net *nn)
{
	if (nn->port)
		nfp_devlink_port_unregister(nn->port);
	nfp_net_debugfs_dir_clean(&nn->debugfs_dir);
	nfp_net_clean(nn);
}

static int
nfp_net_pf_spawn_vnics(struct nfp_pf *pf,
		       void __iomem *ctrl_bar, void __iomem *tx_bar,
		       void __iomem *rx_bar, int stride,
		       struct nfp_net_fw_version *fw_ver)
{
	unsigned int id, wanted_irqs, num_irqs, vnics_left, irqs_left;
	struct nfp_net *nn;
	int err;

	/* Allocate the vnics and do basic init */
	err = nfp_net_pf_alloc_vnics(pf, ctrl_bar, tx_bar, rx_bar,
				     stride, fw_ver);
	if (err)
		return err;

	/* Get MSI-X vectors */
	wanted_irqs = 0;
	list_for_each_entry(nn, &pf->vnics, vnic_list)
		wanted_irqs += NFP_NET_NON_Q_VECTORS + nn->dp.num_r_vecs;
	pf->irq_entries = kcalloc(wanted_irqs, sizeof(*pf->irq_entries),
				  GFP_KERNEL);
	if (!pf->irq_entries) {
		err = -ENOMEM;
		goto err_nn_free;
	}

	num_irqs = nfp_net_irqs_alloc(pf->pdev, pf->irq_entries,
				      NFP_NET_MIN_VNIC_IRQS * pf->num_vnics,
				      wanted_irqs);
	if (!num_irqs) {
		nn_warn(nn, "Unable to allocate MSI-X Vectors. Exiting\n");
		err = -ENOMEM;
		goto err_vec_free;
	}

	/* Distribute IRQs to vNICs */
	irqs_left = num_irqs;
	vnics_left = pf->num_vnics;
	list_for_each_entry(nn, &pf->vnics, vnic_list) {
		unsigned int n;

		n = DIV_ROUND_UP(irqs_left, vnics_left);
		nfp_net_irqs_assign(nn, &pf->irq_entries[num_irqs - irqs_left],
				    n);
		irqs_left -= n;
		vnics_left--;
	}

	/* Finish vNIC init and register */
	id = 0;
	list_for_each_entry(nn, &pf->vnics, vnic_list) {
		err = nfp_net_pf_init_vnic(pf, nn, id);
		if (err)
			goto err_prev_deinit;

		id++;
	}

	return 0;

err_prev_deinit:
	list_for_each_entry_continue_reverse(nn, &pf->vnics, vnic_list)
		nfp_net_pf_clean_vnic(pf, nn);
	nfp_net_irqs_disable(pf->pdev);
err_vec_free:
	kfree(pf->irq_entries);
err_nn_free:
	nfp_net_pf_free_vnics(pf);
	return err;
}

static int nfp_net_pf_app_init(struct nfp_pf *pf)
{
	int err;

	pf->app = nfp_app_alloc(pf, nfp_net_pf_get_app_id(pf));
	if (IS_ERR(pf->app))
		return PTR_ERR(pf->app);

	err = nfp_app_init(pf->app);
	if (err)
		goto err_free;

	return 0;

err_free:
	nfp_app_free(pf->app);
	return err;
}

static void nfp_net_pf_app_clean(struct nfp_pf *pf)
{
	nfp_app_free(pf->app);
	pf->app = NULL;
}

static void nfp_net_pci_remove_finish(struct nfp_pf *pf)
{
	nfp_net_debugfs_dir_clean(&pf->ddir);

	nfp_net_irqs_disable(pf->pdev);
	kfree(pf->irq_entries);

	nfp_net_pf_app_clean(pf);

	nfp_cpp_area_release_free(pf->rx_area);
	nfp_cpp_area_release_free(pf->tx_area);
	nfp_cpp_area_release_free(pf->data_vnic_bar);
}

static int
nfp_net_eth_port_update(struct nfp_cpp *cpp, struct nfp_port *port,
			struct nfp_eth_table *eth_table)
{
	struct nfp_eth_table_port *eth_port;

	ASSERT_RTNL();

	eth_port = nfp_net_find_port(eth_table, port->eth_id);
	if (!eth_port) {
		set_bit(NFP_PORT_CHANGED, &port->flags);
		nfp_warn(cpp, "Warning: port #%d not present after reconfig\n",
			 port->eth_id);
		return -EIO;
	}
	if (eth_port->override_changed) {
		nfp_warn(cpp, "Port #%d config changed, unregistering. Reboot required before port will be operational again.\n", port->eth_id);
		port->type = NFP_PORT_INVALID;
	}

	memcpy(port->eth_port, eth_port, sizeof(*eth_port));

	return 0;
}

int nfp_net_refresh_port_table_sync(struct nfp_pf *pf)
{
	struct nfp_eth_table *eth_table;
	struct nfp_net *nn, *next;
	struct nfp_port *port;

	lockdep_assert_held(&pf->lock);

	/* Check for nfp_net_pci_remove() racing against us */
	if (list_empty(&pf->vnics))
		return 0;

	/* Update state of all ports */
	rtnl_lock();
	list_for_each_entry(port, &pf->ports, port_list)
		clear_bit(NFP_PORT_CHANGED, &port->flags);

	eth_table = nfp_eth_read_ports(pf->cpp);
	if (!eth_table) {
		list_for_each_entry(port, &pf->ports, port_list)
			if (__nfp_port_get_eth_port(port))
				set_bit(NFP_PORT_CHANGED, &port->flags);
		rtnl_unlock();
		nfp_err(pf->cpp, "Error refreshing port config!\n");
		return -EIO;
	}

	list_for_each_entry(port, &pf->ports, port_list)
		if (__nfp_port_get_eth_port(port))
			nfp_net_eth_port_update(pf->cpp, port, eth_table);
	rtnl_unlock();

	kfree(eth_table);

	/* Shoot off the ports which became invalid */
	list_for_each_entry_safe(nn, next, &pf->vnics, vnic_list) {
		if (!nn->port || nn->port->type != NFP_PORT_INVALID)
			continue;

		nfp_net_pf_clean_vnic(pf, nn);
		nfp_net_pf_free_vnic(pf, nn);
	}

	if (list_empty(&pf->vnics))
		nfp_net_pci_remove_finish(pf);

	return 0;
}

static void nfp_net_refresh_vnics(struct work_struct *work)
{
	struct nfp_pf *pf = container_of(work, struct nfp_pf,
					 port_refresh_work);

	mutex_lock(&pf->lock);
	nfp_net_refresh_port_table_sync(pf);
	mutex_unlock(&pf->lock);
}

void nfp_net_refresh_port_table(struct nfp_port *port)
{
	struct nfp_pf *pf = port->app->pf;

	set_bit(NFP_PORT_CHANGED, &port->flags);

	schedule_work(&pf->port_refresh_work);
}

int nfp_net_refresh_eth_port(struct nfp_port *port)
{
	struct nfp_cpp *cpp = port->app->cpp;
	struct nfp_eth_table *eth_table;
	int ret;

	clear_bit(NFP_PORT_CHANGED, &port->flags);

	eth_table = nfp_eth_read_ports(cpp);
	if (!eth_table) {
		set_bit(NFP_PORT_CHANGED, &port->flags);
		nfp_err(cpp, "Error refreshing port state table!\n");
		return -EIO;
	}

	ret = nfp_net_eth_port_update(cpp, port, eth_table);

	kfree(eth_table);

	return ret;
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

	INIT_WORK(&pf->port_refresh_work, nfp_net_refresh_vnics);

	/* Verify that the board has completed initialization */
	if (!nfp_is_ready(pf->cpp)) {
		nfp_err(pf->cpp, "NFP is not ready for NIC operation.\n");
		return -EINVAL;
	}

	mutex_lock(&pf->lock);
	pf->max_data_vnics = nfp_net_pf_get_num_ports(pf);
	if ((int)pf->max_data_vnics < 0) {
		err = pf->max_data_vnics;
		goto err_unlock;
	}

	ctrl_bar = nfp_net_pf_map_ctrl_bar(pf);
	if (!ctrl_bar) {
		err = pf->fw_loaded ? -EINVAL : -EPROBE_DEFER;
		goto err_unlock;
	}

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

	err = nfp_net_pf_app_init(pf);
	if (err)
		goto err_unmap_rx;

	pf->ddir = nfp_net_debugfs_device_add(pf->pdev);

	err = nfp_net_pf_spawn_vnics(pf, ctrl_bar, tx_bar, rx_bar,
				     stride, &fw_ver);
	if (err)
		goto err_clean_ddir;

	mutex_unlock(&pf->lock);

	return 0;

err_clean_ddir:
	nfp_net_debugfs_dir_clean(&pf->ddir);
	nfp_net_pf_app_clean(pf);
err_unmap_rx:
	nfp_cpp_area_release_free(pf->rx_area);
err_unmap_tx:
	nfp_cpp_area_release_free(pf->tx_area);
err_ctrl_unmap:
	nfp_cpp_area_release_free(pf->data_vnic_bar);
err_unlock:
	mutex_unlock(&pf->lock);
	return err;
}

void nfp_net_pci_remove(struct nfp_pf *pf)
{
	struct nfp_net *nn;

	mutex_lock(&pf->lock);
	if (list_empty(&pf->vnics))
		goto out;

	list_for_each_entry(nn, &pf->vnics, vnic_list)
		nfp_net_pf_clean_vnic(pf, nn);

	nfp_net_pf_free_vnics(pf);

	nfp_net_pci_remove_finish(pf);
out:
	mutex_unlock(&pf->lock);

	cancel_work_sync(&pf->port_refresh_work);
}
