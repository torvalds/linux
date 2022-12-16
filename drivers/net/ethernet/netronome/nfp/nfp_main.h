/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/* Copyright (C) 2015-2018 Netronome Systems, Inc. */

/*
 * nfp_main.h
 * Author: Jason McMullan <jason.mcmullan@netronome.com>
 */

#ifndef NFP_MAIN_H
#define NFP_MAIN_H

#include <linux/ethtool.h>
#include <linux/list.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/workqueue.h>
#include <net/devlink.h>

struct dentry;
struct device;
struct pci_dev;

struct nfp_cpp;
struct nfp_cpp_area;
struct nfp_eth_table;
struct nfp_hwinfo;
struct nfp_mip;
struct nfp_net;
struct nfp_nsp_identify;
struct nfp_eth_media_buf;
struct nfp_port;
struct nfp_rtsym;
struct nfp_rtsym_table;
struct nfp_shared_buf;

/**
 * struct nfp_dumpspec - NFP FW dump specification structure
 * @size:	Size of the data
 * @data:	Sequence of TLVs, each being an instruction to dump some data
 *		from FW
 */
struct nfp_dumpspec {
	u32 size;
	u8 data[];
};

/**
 * struct nfp_pf - NFP PF-specific device structure
 * @pdev:		Backpointer to PCI device
 * @dev_info:		NFP ASIC params
 * @cpp:		Pointer to the CPP handle
 * @app:		Pointer to the APP handle
 * @data_vnic_bar:	Pointer to the CPP area for the data vNICs' BARs
 * @ctrl_vnic_bar:	Pointer to the CPP area for the ctrl vNIC's BAR
 * @qc_area:		Pointer to the CPP area for the queues
 * @mac_stats_bar:	Pointer to the CPP area for the MAC stats
 * @mac_stats_mem:	Pointer to mapped MAC stats area
 * @vf_cfg_bar:		Pointer to the CPP area for the VF configuration BAR
 * @vf_cfg_mem:		Pointer to mapped VF configuration area
 * @vfcfg_tbl2_area:	Pointer to the CPP area for the VF config table
 * @vfcfg_tbl2:		Pointer to mapped VF config table
 * @mbox:		RTSym of per-PCI PF mailbox (under devlink lock)
 * @irq_entries:	Array of MSI-X entries for all vNICs
 * @limit_vfs:		Number of VFs supported by firmware (~0 for PCI limit)
 * @num_vfs:		Number of SR-IOV VFs enabled
 * @fw_loaded:		Is the firmware loaded?
 * @unload_fw_on_remove:Do we need to unload firmware on driver removal?
 * @ctrl_vnic:		Pointer to the control vNIC if available
 * @mip:		MIP handle
 * @rtbl:		RTsym table
 * @hwinfo:		HWInfo table
 * @dumpspec:		Debug dump specification
 * @dump_flag:		Store dump flag between set_dump and get_dump_flag
 * @dump_len:		Store dump length between set_dump and get_dump_flag
 * @eth_tbl:		NSP ETH table
 * @nspi:		NSP identification info
 * @hwmon_dev:		pointer to hwmon device
 * @ddir:		Per-device debugfs directory
 * @max_data_vnics:	Number of data vNICs app firmware supports
 * @num_vnics:		Number of vNICs spawned
 * @vnics:		Linked list of vNIC structures (struct nfp_net)
 * @ports:		Linked list of port structures (struct nfp_port)
 * @wq:			Workqueue for running works which need to grab @lock
 * @port_refresh_work:	Work entry for taking netdevs out
 * @shared_bufs:	Array of shared buffer structures if FW has any SBs
 * @num_shared_bufs:	Number of elements in @shared_bufs
 *
 * Fields which may change after proble are protected by devlink instance lock.
 */
struct nfp_pf {
	struct pci_dev *pdev;
	const struct nfp_dev_info *dev_info;

	struct nfp_cpp *cpp;

	struct nfp_app *app;

	struct nfp_cpp_area *data_vnic_bar;
	struct nfp_cpp_area *ctrl_vnic_bar;
	struct nfp_cpp_area *qc_area;
	struct nfp_cpp_area *mac_stats_bar;
	u8 __iomem *mac_stats_mem;
	struct nfp_cpp_area *vf_cfg_bar;
	u8 __iomem *vf_cfg_mem;
	struct nfp_cpp_area *vfcfg_tbl2_area;
	u8 __iomem *vfcfg_tbl2;

	const struct nfp_rtsym *mbox;

	struct msix_entry *irq_entries;

	unsigned int limit_vfs;
	unsigned int num_vfs;

	bool fw_loaded;
	bool unload_fw_on_remove;

	struct nfp_net *ctrl_vnic;

	const struct nfp_mip *mip;
	struct nfp_rtsym_table *rtbl;
	struct nfp_hwinfo *hwinfo;
	struct nfp_dumpspec *dumpspec;
	u32 dump_flag;
	u32 dump_len;
	struct nfp_eth_table *eth_tbl;
	struct nfp_nsp_identify *nspi;

	struct device *hwmon_dev;

	struct dentry *ddir;

	unsigned int max_data_vnics;
	unsigned int num_vnics;

	struct list_head vnics;
	struct list_head ports;

	struct workqueue_struct *wq;
	struct work_struct port_refresh_work;

	struct nfp_shared_buf *shared_bufs;
	unsigned int num_shared_bufs;
};

extern struct pci_driver nfp_netvf_pci_driver;

extern const struct devlink_ops nfp_devlink_ops;

int nfp_net_pci_probe(struct nfp_pf *pf);
void nfp_net_pci_remove(struct nfp_pf *pf);

int nfp_hwmon_register(struct nfp_pf *pf);
void nfp_hwmon_unregister(struct nfp_pf *pf);

void
nfp_net_get_mac_addr(struct nfp_pf *pf, struct net_device *netdev,
		     struct nfp_port *port);

bool nfp_ctrl_tx(struct nfp_net *nn, struct sk_buff *skb);

int nfp_pf_rtsym_read_optional(struct nfp_pf *pf, const char *format,
			       unsigned int default_val);
int nfp_net_pf_get_app_id(struct nfp_pf *pf);
u8 __iomem *
nfp_pf_map_rtsym(struct nfp_pf *pf, const char *name, const char *sym_fmt,
		 unsigned int min_size, struct nfp_cpp_area **area);
int nfp_mbox_cmd(struct nfp_pf *pf, u32 cmd, void *in_data, u64 in_length,
		 void *out_data, u64 out_length);
int nfp_flash_update_common(struct nfp_pf *pf, const struct firmware *fw,
			    struct netlink_ext_ack *extack);

enum nfp_dump_diag {
	NFP_DUMP_NSP_DIAG = 0,
};

struct nfp_dumpspec *
nfp_net_dump_load_dumpspec(struct nfp_cpp *cpp, struct nfp_rtsym_table *rtbl);
s64 nfp_net_dump_calculate_size(struct nfp_pf *pf, struct nfp_dumpspec *spec,
				u32 flag);
int nfp_net_dump_populate_buffer(struct nfp_pf *pf, struct nfp_dumpspec *spec,
				 struct ethtool_dump *dump_param, void *dest);

int nfp_shared_buf_register(struct nfp_pf *pf);
void nfp_shared_buf_unregister(struct nfp_pf *pf);
int nfp_shared_buf_pool_get(struct nfp_pf *pf, unsigned int sb, u16 pool_index,
			    struct devlink_sb_pool_info *pool_info);
int nfp_shared_buf_pool_set(struct nfp_pf *pf, unsigned int sb,
			    u16 pool_index, u32 size,
			    enum devlink_sb_threshold_type threshold_type);

int nfp_devlink_params_register(struct nfp_pf *pf);
void nfp_devlink_params_unregister(struct nfp_pf *pf);

unsigned int nfp_net_lr2speed(unsigned int linkrate);
unsigned int nfp_net_speed2lr(unsigned int speed);
#endif /* NFP_MAIN_H */
