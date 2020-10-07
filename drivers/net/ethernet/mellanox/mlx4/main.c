/*
 * Copyright (c) 2004, 2005 Topspin Communications.  All rights reserved.
 * Copyright (c) 2005 Sun Microsystems, Inc. All rights reserved.
 * Copyright (c) 2005, 2006, 2007, 2008 Mellanox Technologies. All rights reserved.
 * Copyright (c) 2006, 2007 Cisco Systems, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
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

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/pci.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>
#include <linux/io-mapping.h>
#include <linux/delay.h>
#include <linux/kmod.h>
#include <linux/etherdevice.h>
#include <net/devlink.h>

#include <uapi/rdma/mlx4-abi.h>
#include <linux/mlx4/device.h>
#include <linux/mlx4/doorbell.h>

#include "mlx4.h"
#include "fw.h"
#include "icm.h"

MODULE_AUTHOR("Roland Dreier");
MODULE_DESCRIPTION("Mellanox ConnectX HCA low-level driver");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_VERSION(DRV_VERSION);

struct workqueue_struct *mlx4_wq;

#ifdef CONFIG_MLX4_DEBUG

int mlx4_debug_level; /* 0 by default */
module_param_named(debug_level, mlx4_debug_level, int, 0644);
MODULE_PARM_DESC(debug_level, "Enable debug tracing if > 0");

#endif /* CONFIG_MLX4_DEBUG */

#ifdef CONFIG_PCI_MSI

static int msi_x = 1;
module_param(msi_x, int, 0444);
MODULE_PARM_DESC(msi_x, "0 - don't use MSI-X, 1 - use MSI-X, >1 - limit number of MSI-X irqs to msi_x");

#else /* CONFIG_PCI_MSI */

#define msi_x (0)

#endif /* CONFIG_PCI_MSI */

static uint8_t num_vfs[3] = {0, 0, 0};
static int num_vfs_argc;
module_param_array(num_vfs, byte, &num_vfs_argc, 0444);
MODULE_PARM_DESC(num_vfs, "enable #num_vfs functions if num_vfs > 0\n"
			  "num_vfs=port1,port2,port1+2");

static uint8_t probe_vf[3] = {0, 0, 0};
static int probe_vfs_argc;
module_param_array(probe_vf, byte, &probe_vfs_argc, 0444);
MODULE_PARM_DESC(probe_vf, "number of vfs to probe by pf driver (num_vfs > 0)\n"
			   "probe_vf=port1,port2,port1+2");

static int mlx4_log_num_mgm_entry_size = MLX4_DEFAULT_MGM_LOG_ENTRY_SIZE;
module_param_named(log_num_mgm_entry_size,
			mlx4_log_num_mgm_entry_size, int, 0444);
MODULE_PARM_DESC(log_num_mgm_entry_size, "log mgm size, that defines the num"
					 " of qp per mcg, for example:"
					 " 10 gives 248.range: 7 <="
					 " log_num_mgm_entry_size <= 12."
					 " To activate device managed"
					 " flow steering when available, set to -1");

static bool enable_64b_cqe_eqe = true;
module_param(enable_64b_cqe_eqe, bool, 0444);
MODULE_PARM_DESC(enable_64b_cqe_eqe,
		 "Enable 64 byte CQEs/EQEs when the FW supports this (default: True)");

static bool enable_4k_uar;
module_param(enable_4k_uar, bool, 0444);
MODULE_PARM_DESC(enable_4k_uar,
		 "Enable using 4K UAR. Should not be enabled if have VFs which do not support 4K UARs (default: false)");

#define PF_CONTEXT_BEHAVIOUR_MASK	(MLX4_FUNC_CAP_64B_EQE_CQE | \
					 MLX4_FUNC_CAP_EQE_CQE_STRIDE | \
					 MLX4_FUNC_CAP_DMFS_A0_STATIC)

#define RESET_PERSIST_MASK_FLAGS	(MLX4_FLAG_SRIOV)

static char mlx4_version[] =
	DRV_NAME ": Mellanox ConnectX core driver v"
	DRV_VERSION "\n";

static const struct mlx4_profile default_profile = {
	.num_qp		= 1 << 18,
	.num_srq	= 1 << 16,
	.rdmarc_per_qp	= 1 << 4,
	.num_cq		= 1 << 16,
	.num_mcg	= 1 << 13,
	.num_mpt	= 1 << 19,
	.num_mtt	= 1 << 20, /* It is really num mtt segements */
};

static const struct mlx4_profile low_mem_profile = {
	.num_qp		= 1 << 17,
	.num_srq	= 1 << 6,
	.rdmarc_per_qp	= 1 << 4,
	.num_cq		= 1 << 8,
	.num_mcg	= 1 << 8,
	.num_mpt	= 1 << 9,
	.num_mtt	= 1 << 7,
};

static int log_num_mac = 7;
module_param_named(log_num_mac, log_num_mac, int, 0444);
MODULE_PARM_DESC(log_num_mac, "Log2 max number of MACs per ETH port (1-7)");

static int log_num_vlan;
module_param_named(log_num_vlan, log_num_vlan, int, 0444);
MODULE_PARM_DESC(log_num_vlan, "Log2 max number of VLANs per ETH port (0-7)");
/* Log2 max number of VLANs per ETH port (0-7) */
#define MLX4_LOG_NUM_VLANS 7
#define MLX4_MIN_LOG_NUM_VLANS 0
#define MLX4_MIN_LOG_NUM_MAC 1

static bool use_prio;
module_param_named(use_prio, use_prio, bool, 0444);
MODULE_PARM_DESC(use_prio, "Enable steering by VLAN priority on ETH ports (deprecated)");

int log_mtts_per_seg = ilog2(1);
module_param_named(log_mtts_per_seg, log_mtts_per_seg, int, 0444);
MODULE_PARM_DESC(log_mtts_per_seg, "Log2 number of MTT entries per segment "
		 "(0-7) (default: 0)");

static int port_type_array[2] = {MLX4_PORT_TYPE_NONE, MLX4_PORT_TYPE_NONE};
static int arr_argc = 2;
module_param_array(port_type_array, int, &arr_argc, 0444);
MODULE_PARM_DESC(port_type_array, "Array of port types: HW_DEFAULT (0) is default "
				"1 for IB, 2 for Ethernet");

struct mlx4_port_config {
	struct list_head list;
	enum mlx4_port_type port_type[MLX4_MAX_PORTS + 1];
	struct pci_dev *pdev;
};

static atomic_t pf_loading = ATOMIC_INIT(0);

static int mlx4_devlink_ierr_reset_get(struct devlink *devlink, u32 id,
				       struct devlink_param_gset_ctx *ctx)
{
	ctx->val.vbool = !!mlx4_internal_err_reset;
	return 0;
}

static int mlx4_devlink_ierr_reset_set(struct devlink *devlink, u32 id,
				       struct devlink_param_gset_ctx *ctx)
{
	mlx4_internal_err_reset = ctx->val.vbool;
	return 0;
}

static int mlx4_devlink_crdump_snapshot_get(struct devlink *devlink, u32 id,
					    struct devlink_param_gset_ctx *ctx)
{
	struct mlx4_priv *priv = devlink_priv(devlink);
	struct mlx4_dev *dev = &priv->dev;

	ctx->val.vbool = dev->persist->crdump.snapshot_enable;
	return 0;
}

static int mlx4_devlink_crdump_snapshot_set(struct devlink *devlink, u32 id,
					    struct devlink_param_gset_ctx *ctx)
{
	struct mlx4_priv *priv = devlink_priv(devlink);
	struct mlx4_dev *dev = &priv->dev;

	dev->persist->crdump.snapshot_enable = ctx->val.vbool;
	return 0;
}

static int
mlx4_devlink_max_macs_validate(struct devlink *devlink, u32 id,
			       union devlink_param_value val,
			       struct netlink_ext_ack *extack)
{
	u32 value = val.vu32;

	if (value < 1 || value > 128)
		return -ERANGE;

	if (!is_power_of_2(value)) {
		NL_SET_ERR_MSG_MOD(extack, "max_macs supported must be power of 2");
		return -EINVAL;
	}

	return 0;
}

enum mlx4_devlink_param_id {
	MLX4_DEVLINK_PARAM_ID_BASE = DEVLINK_PARAM_GENERIC_ID_MAX,
	MLX4_DEVLINK_PARAM_ID_ENABLE_64B_CQE_EQE,
	MLX4_DEVLINK_PARAM_ID_ENABLE_4K_UAR,
};

static const struct devlink_param mlx4_devlink_params[] = {
	DEVLINK_PARAM_GENERIC(INT_ERR_RESET,
			      BIT(DEVLINK_PARAM_CMODE_RUNTIME) |
			      BIT(DEVLINK_PARAM_CMODE_DRIVERINIT),
			      mlx4_devlink_ierr_reset_get,
			      mlx4_devlink_ierr_reset_set, NULL),
	DEVLINK_PARAM_GENERIC(MAX_MACS,
			      BIT(DEVLINK_PARAM_CMODE_DRIVERINIT),
			      NULL, NULL, mlx4_devlink_max_macs_validate),
	DEVLINK_PARAM_GENERIC(REGION_SNAPSHOT,
			      BIT(DEVLINK_PARAM_CMODE_RUNTIME) |
			      BIT(DEVLINK_PARAM_CMODE_DRIVERINIT),
			      mlx4_devlink_crdump_snapshot_get,
			      mlx4_devlink_crdump_snapshot_set, NULL),
	DEVLINK_PARAM_DRIVER(MLX4_DEVLINK_PARAM_ID_ENABLE_64B_CQE_EQE,
			     "enable_64b_cqe_eqe", DEVLINK_PARAM_TYPE_BOOL,
			     BIT(DEVLINK_PARAM_CMODE_DRIVERINIT),
			     NULL, NULL, NULL),
	DEVLINK_PARAM_DRIVER(MLX4_DEVLINK_PARAM_ID_ENABLE_4K_UAR,
			     "enable_4k_uar", DEVLINK_PARAM_TYPE_BOOL,
			     BIT(DEVLINK_PARAM_CMODE_DRIVERINIT),
			     NULL, NULL, NULL),
};

static void mlx4_devlink_set_params_init_values(struct devlink *devlink)
{
	union devlink_param_value value;

	value.vbool = !!mlx4_internal_err_reset;
	devlink_param_driverinit_value_set(devlink,
					   DEVLINK_PARAM_GENERIC_ID_INT_ERR_RESET,
					   value);

	value.vu32 = 1UL << log_num_mac;
	devlink_param_driverinit_value_set(devlink,
					   DEVLINK_PARAM_GENERIC_ID_MAX_MACS,
					   value);

	value.vbool = enable_64b_cqe_eqe;
	devlink_param_driverinit_value_set(devlink,
					   MLX4_DEVLINK_PARAM_ID_ENABLE_64B_CQE_EQE,
					   value);

	value.vbool = enable_4k_uar;
	devlink_param_driverinit_value_set(devlink,
					   MLX4_DEVLINK_PARAM_ID_ENABLE_4K_UAR,
					   value);

	value.vbool = false;
	devlink_param_driverinit_value_set(devlink,
					   DEVLINK_PARAM_GENERIC_ID_REGION_SNAPSHOT,
					   value);
}

static inline void mlx4_set_num_reserved_uars(struct mlx4_dev *dev,
					      struct mlx4_dev_cap *dev_cap)
{
	/* The reserved_uars is calculated by system page size unit.
	 * Therefore, adjustment is added when the uar page size is less
	 * than the system page size
	 */
	dev->caps.reserved_uars	=
		max_t(int,
		      mlx4_get_num_reserved_uar(dev),
		      dev_cap->reserved_uars /
			(1 << (PAGE_SHIFT - dev->uar_page_shift)));
}

int mlx4_check_port_params(struct mlx4_dev *dev,
			   enum mlx4_port_type *port_type)
{
	int i;

	if (!(dev->caps.flags & MLX4_DEV_CAP_FLAG_DPDP)) {
		for (i = 0; i < dev->caps.num_ports - 1; i++) {
			if (port_type[i] != port_type[i + 1]) {
				mlx4_err(dev, "Only same port types supported on this HCA, aborting\n");
				return -EOPNOTSUPP;
			}
		}
	}

	for (i = 0; i < dev->caps.num_ports; i++) {
		if (!(port_type[i] & dev->caps.supported_type[i+1])) {
			mlx4_err(dev, "Requested port type for port %d is not supported on this HCA\n",
				 i + 1);
			return -EOPNOTSUPP;
		}
	}
	return 0;
}

static void mlx4_set_port_mask(struct mlx4_dev *dev)
{
	int i;

	for (i = 1; i <= dev->caps.num_ports; ++i)
		dev->caps.port_mask[i] = dev->caps.port_type[i];
}

enum {
	MLX4_QUERY_FUNC_NUM_SYS_EQS = 1 << 0,
};

static int mlx4_query_func(struct mlx4_dev *dev, struct mlx4_dev_cap *dev_cap)
{
	int err = 0;
	struct mlx4_func func;

	if (dev->caps.flags2 & MLX4_DEV_CAP_FLAG2_SYS_EQS) {
		err = mlx4_QUERY_FUNC(dev, &func, 0);
		if (err) {
			mlx4_err(dev, "QUERY_DEV_CAP command failed, aborting.\n");
			return err;
		}
		dev_cap->max_eqs = func.max_eq;
		dev_cap->reserved_eqs = func.rsvd_eqs;
		dev_cap->reserved_uars = func.rsvd_uars;
		err |= MLX4_QUERY_FUNC_NUM_SYS_EQS;
	}
	return err;
}

static void mlx4_enable_cqe_eqe_stride(struct mlx4_dev *dev)
{
	struct mlx4_caps *dev_cap = &dev->caps;

	/* FW not supporting or cancelled by user */
	if (!(dev_cap->flags2 & MLX4_DEV_CAP_FLAG2_EQE_STRIDE) ||
	    !(dev_cap->flags2 & MLX4_DEV_CAP_FLAG2_CQE_STRIDE))
		return;

	/* Must have 64B CQE_EQE enabled by FW to use bigger stride
	 * When FW has NCSI it may decide not to report 64B CQE/EQEs
	 */
	if (!(dev_cap->flags & MLX4_DEV_CAP_FLAG_64B_EQE) ||
	    !(dev_cap->flags & MLX4_DEV_CAP_FLAG_64B_CQE)) {
		dev_cap->flags2 &= ~MLX4_DEV_CAP_FLAG2_CQE_STRIDE;
		dev_cap->flags2 &= ~MLX4_DEV_CAP_FLAG2_EQE_STRIDE;
		return;
	}

	if (cache_line_size() == 128 || cache_line_size() == 256) {
		mlx4_dbg(dev, "Enabling CQE stride cacheLine supported\n");
		/* Changing the real data inside CQE size to 32B */
		dev_cap->flags &= ~MLX4_DEV_CAP_FLAG_64B_CQE;
		dev_cap->flags &= ~MLX4_DEV_CAP_FLAG_64B_EQE;

		if (mlx4_is_master(dev))
			dev_cap->function_caps |= MLX4_FUNC_CAP_EQE_CQE_STRIDE;
	} else {
		if (cache_line_size() != 32  && cache_line_size() != 64)
			mlx4_dbg(dev, "Disabling CQE stride, cacheLine size unsupported\n");
		dev_cap->flags2 &= ~MLX4_DEV_CAP_FLAG2_CQE_STRIDE;
		dev_cap->flags2 &= ~MLX4_DEV_CAP_FLAG2_EQE_STRIDE;
	}
}

static int _mlx4_dev_port(struct mlx4_dev *dev, int port,
			  struct mlx4_port_cap *port_cap)
{
	dev->caps.vl_cap[port]	    = port_cap->max_vl;
	dev->caps.ib_mtu_cap[port]	    = port_cap->ib_mtu;
	dev->phys_caps.gid_phys_table_len[port]  = port_cap->max_gids;
	dev->phys_caps.pkey_phys_table_len[port] = port_cap->max_pkeys;
	/* set gid and pkey table operating lengths by default
	 * to non-sriov values
	 */
	dev->caps.gid_table_len[port]  = port_cap->max_gids;
	dev->caps.pkey_table_len[port] = port_cap->max_pkeys;
	dev->caps.port_width_cap[port] = port_cap->max_port_width;
	dev->caps.eth_mtu_cap[port]    = port_cap->eth_mtu;
	dev->caps.max_tc_eth	       = port_cap->max_tc_eth;
	dev->caps.def_mac[port]        = port_cap->def_mac;
	dev->caps.supported_type[port] = port_cap->supported_port_types;
	dev->caps.suggested_type[port] = port_cap->suggested_type;
	dev->caps.default_sense[port] = port_cap->default_sense;
	dev->caps.trans_type[port]	    = port_cap->trans_type;
	dev->caps.vendor_oui[port]     = port_cap->vendor_oui;
	dev->caps.wavelength[port]     = port_cap->wavelength;
	dev->caps.trans_code[port]     = port_cap->trans_code;

	return 0;
}

static int mlx4_dev_port(struct mlx4_dev *dev, int port,
			 struct mlx4_port_cap *port_cap)
{
	int err = 0;

	err = mlx4_QUERY_PORT(dev, port, port_cap);

	if (err)
		mlx4_err(dev, "QUERY_PORT command failed.\n");

	return err;
}

static inline void mlx4_enable_ignore_fcs(struct mlx4_dev *dev)
{
	if (!(dev->caps.flags2 & MLX4_DEV_CAP_FLAG2_IGNORE_FCS))
		return;

	if (mlx4_is_mfunc(dev)) {
		mlx4_dbg(dev, "SRIOV mode - Disabling Ignore FCS");
		dev->caps.flags2 &= ~MLX4_DEV_CAP_FLAG2_IGNORE_FCS;
		return;
	}

	if (!(dev->caps.flags & MLX4_DEV_CAP_FLAG_FCS_KEEP)) {
		mlx4_dbg(dev,
			 "Keep FCS is not supported - Disabling Ignore FCS");
		dev->caps.flags2 &= ~MLX4_DEV_CAP_FLAG2_IGNORE_FCS;
		return;
	}
}

#define MLX4_A0_STEERING_TABLE_SIZE	256
static int mlx4_dev_cap(struct mlx4_dev *dev, struct mlx4_dev_cap *dev_cap)
{
	int err;
	int i;

	err = mlx4_QUERY_DEV_CAP(dev, dev_cap);
	if (err) {
		mlx4_err(dev, "QUERY_DEV_CAP command failed, aborting\n");
		return err;
	}
	mlx4_dev_cap_dump(dev, dev_cap);

	if (dev_cap->min_page_sz > PAGE_SIZE) {
		mlx4_err(dev, "HCA minimum page size of %d bigger than kernel PAGE_SIZE of %ld, aborting\n",
			 dev_cap->min_page_sz, PAGE_SIZE);
		return -ENODEV;
	}
	if (dev_cap->num_ports > MLX4_MAX_PORTS) {
		mlx4_err(dev, "HCA has %d ports, but we only support %d, aborting\n",
			 dev_cap->num_ports, MLX4_MAX_PORTS);
		return -ENODEV;
	}

	if (dev_cap->uar_size > pci_resource_len(dev->persist->pdev, 2)) {
		mlx4_err(dev, "HCA reported UAR size of 0x%x bigger than PCI resource 2 size of 0x%llx, aborting\n",
			 dev_cap->uar_size,
			 (unsigned long long)
			 pci_resource_len(dev->persist->pdev, 2));
		return -ENODEV;
	}

	dev->caps.num_ports	     = dev_cap->num_ports;
	dev->caps.num_sys_eqs = dev_cap->num_sys_eqs;
	dev->phys_caps.num_phys_eqs = dev_cap->flags2 & MLX4_DEV_CAP_FLAG2_SYS_EQS ?
				      dev->caps.num_sys_eqs :
				      MLX4_MAX_EQ_NUM;
	for (i = 1; i <= dev->caps.num_ports; ++i) {
		err = _mlx4_dev_port(dev, i, dev_cap->port_cap + i);
		if (err) {
			mlx4_err(dev, "QUERY_PORT command failed, aborting\n");
			return err;
		}
	}

	dev->caps.uar_page_size	     = PAGE_SIZE;
	dev->caps.num_uars	     = dev_cap->uar_size / PAGE_SIZE;
	dev->caps.local_ca_ack_delay = dev_cap->local_ca_ack_delay;
	dev->caps.bf_reg_size	     = dev_cap->bf_reg_size;
	dev->caps.bf_regs_per_page   = dev_cap->bf_regs_per_page;
	dev->caps.max_sq_sg	     = dev_cap->max_sq_sg;
	dev->caps.max_rq_sg	     = dev_cap->max_rq_sg;
	dev->caps.max_wqes	     = dev_cap->max_qp_sz;
	dev->caps.max_qp_init_rdma   = dev_cap->max_requester_per_qp;
	dev->caps.max_srq_wqes	     = dev_cap->max_srq_sz;
	dev->caps.max_srq_sge	     = dev_cap->max_rq_sg - 1;
	dev->caps.reserved_srqs	     = dev_cap->reserved_srqs;
	dev->caps.max_sq_desc_sz     = dev_cap->max_sq_desc_sz;
	dev->caps.max_rq_desc_sz     = dev_cap->max_rq_desc_sz;
	/*
	 * Subtract 1 from the limit because we need to allocate a
	 * spare CQE to enable resizing the CQ.
	 */
	dev->caps.max_cqes	     = dev_cap->max_cq_sz - 1;
	dev->caps.reserved_cqs	     = dev_cap->reserved_cqs;
	dev->caps.reserved_eqs	     = dev_cap->reserved_eqs;
	dev->caps.reserved_mtts      = dev_cap->reserved_mtts;
	dev->caps.reserved_mrws	     = dev_cap->reserved_mrws;

	dev->caps.reserved_pds	     = dev_cap->reserved_pds;
	dev->caps.reserved_xrcds     = (dev->caps.flags & MLX4_DEV_CAP_FLAG_XRC) ?
					dev_cap->reserved_xrcds : 0;
	dev->caps.max_xrcds          = (dev->caps.flags & MLX4_DEV_CAP_FLAG_XRC) ?
					dev_cap->max_xrcds : 0;
	dev->caps.mtt_entry_sz       = dev_cap->mtt_entry_sz;

	dev->caps.max_msg_sz         = dev_cap->max_msg_sz;
	dev->caps.page_size_cap	     = ~(u32) (dev_cap->min_page_sz - 1);
	dev->caps.flags		     = dev_cap->flags;
	dev->caps.flags2	     = dev_cap->flags2;
	dev->caps.bmme_flags	     = dev_cap->bmme_flags;
	dev->caps.reserved_lkey	     = dev_cap->reserved_lkey;
	dev->caps.stat_rate_support  = dev_cap->stat_rate_support;
	dev->caps.max_gso_sz	     = dev_cap->max_gso_sz;
	dev->caps.max_rss_tbl_sz     = dev_cap->max_rss_tbl_sz;
	dev->caps.wol_port[1]          = dev_cap->wol_port[1];
	dev->caps.wol_port[2]          = dev_cap->wol_port[2];
	dev->caps.health_buffer_addrs  = dev_cap->health_buffer_addrs;

	/* Save uar page shift */
	if (!mlx4_is_slave(dev)) {
		/* Virtual PCI function needs to determine UAR page size from
		 * firmware. Only master PCI function can set the uar page size
		 */
		if (enable_4k_uar || !dev->persist->num_vfs)
			dev->uar_page_shift = DEFAULT_UAR_PAGE_SHIFT;
		else
			dev->uar_page_shift = PAGE_SHIFT;

		mlx4_set_num_reserved_uars(dev, dev_cap);
	}

	if (dev->caps.flags2 & MLX4_DEV_CAP_FLAG2_PHV_EN) {
		struct mlx4_init_hca_param hca_param;

		memset(&hca_param, 0, sizeof(hca_param));
		err = mlx4_QUERY_HCA(dev, &hca_param);
		/* Turn off PHV_EN flag in case phv_check_en is set.
		 * phv_check_en is a HW check that parse the packet and verify
		 * phv bit was reported correctly in the wqe. To allow QinQ
		 * PHV_EN flag should be set and phv_check_en must be cleared
		 * otherwise QinQ packets will be drop by the HW.
		 */
		if (err || hca_param.phv_check_en)
			dev->caps.flags2 &= ~MLX4_DEV_CAP_FLAG2_PHV_EN;
	}

	/* Sense port always allowed on supported devices for ConnectX-1 and -2 */
	if (mlx4_priv(dev)->pci_dev_data & MLX4_PCI_DEV_FORCE_SENSE_PORT)
		dev->caps.flags |= MLX4_DEV_CAP_FLAG_SENSE_SUPPORT;
	/* Don't do sense port on multifunction devices (for now at least) */
	if (mlx4_is_mfunc(dev))
		dev->caps.flags &= ~MLX4_DEV_CAP_FLAG_SENSE_SUPPORT;

	if (mlx4_low_memory_profile()) {
		dev->caps.log_num_macs  = MLX4_MIN_LOG_NUM_MAC;
		dev->caps.log_num_vlans = MLX4_MIN_LOG_NUM_VLANS;
	} else {
		dev->caps.log_num_macs  = log_num_mac;
		dev->caps.log_num_vlans = MLX4_LOG_NUM_VLANS;
	}

	for (i = 1; i <= dev->caps.num_ports; ++i) {
		dev->caps.port_type[i] = MLX4_PORT_TYPE_NONE;
		if (dev->caps.supported_type[i]) {
			/* if only ETH is supported - assign ETH */
			if (dev->caps.supported_type[i] == MLX4_PORT_TYPE_ETH)
				dev->caps.port_type[i] = MLX4_PORT_TYPE_ETH;
			/* if only IB is supported, assign IB */
			else if (dev->caps.supported_type[i] ==
				 MLX4_PORT_TYPE_IB)
				dev->caps.port_type[i] = MLX4_PORT_TYPE_IB;
			else {
				/* if IB and ETH are supported, we set the port
				 * type according to user selection of port type;
				 * if user selected none, take the FW hint */
				if (port_type_array[i - 1] == MLX4_PORT_TYPE_NONE)
					dev->caps.port_type[i] = dev->caps.suggested_type[i] ?
						MLX4_PORT_TYPE_ETH : MLX4_PORT_TYPE_IB;
				else
					dev->caps.port_type[i] = port_type_array[i - 1];
			}
		}
		/*
		 * Link sensing is allowed on the port if 3 conditions are true:
		 * 1. Both protocols are supported on the port.
		 * 2. Different types are supported on the port
		 * 3. FW declared that it supports link sensing
		 */
		mlx4_priv(dev)->sense.sense_allowed[i] =
			((dev->caps.supported_type[i] == MLX4_PORT_TYPE_AUTO) &&
			 (dev->caps.flags & MLX4_DEV_CAP_FLAG_DPDP) &&
			 (dev->caps.flags & MLX4_DEV_CAP_FLAG_SENSE_SUPPORT));

		/*
		 * If "default_sense" bit is set, we move the port to "AUTO" mode
		 * and perform sense_port FW command to try and set the correct
		 * port type from beginning
		 */
		if (mlx4_priv(dev)->sense.sense_allowed[i] && dev->caps.default_sense[i]) {
			enum mlx4_port_type sensed_port = MLX4_PORT_TYPE_NONE;
			dev->caps.possible_type[i] = MLX4_PORT_TYPE_AUTO;
			mlx4_SENSE_PORT(dev, i, &sensed_port);
			if (sensed_port != MLX4_PORT_TYPE_NONE)
				dev->caps.port_type[i] = sensed_port;
		} else {
			dev->caps.possible_type[i] = dev->caps.port_type[i];
		}

		if (dev->caps.log_num_macs > dev_cap->port_cap[i].log_max_macs) {
			dev->caps.log_num_macs = dev_cap->port_cap[i].log_max_macs;
			mlx4_warn(dev, "Requested number of MACs is too much for port %d, reducing to %d\n",
				  i, 1 << dev->caps.log_num_macs);
		}
		if (dev->caps.log_num_vlans > dev_cap->port_cap[i].log_max_vlans) {
			dev->caps.log_num_vlans = dev_cap->port_cap[i].log_max_vlans;
			mlx4_warn(dev, "Requested number of VLANs is too much for port %d, reducing to %d\n",
				  i, 1 << dev->caps.log_num_vlans);
		}
	}

	if (mlx4_is_master(dev) && (dev->caps.num_ports == 2) &&
	    (port_type_array[0] == MLX4_PORT_TYPE_IB) &&
	    (port_type_array[1] == MLX4_PORT_TYPE_ETH)) {
		mlx4_warn(dev,
			  "Granular QoS per VF not supported with IB/Eth configuration\n");
		dev->caps.flags2 &= ~MLX4_DEV_CAP_FLAG2_QOS_VPP;
	}

	dev->caps.max_counters = dev_cap->max_counters;

	dev->caps.reserved_qps_cnt[MLX4_QP_REGION_FW] = dev_cap->reserved_qps;
	dev->caps.reserved_qps_cnt[MLX4_QP_REGION_ETH_ADDR] =
		dev->caps.reserved_qps_cnt[MLX4_QP_REGION_FC_ADDR] =
		(1 << dev->caps.log_num_macs) *
		(1 << dev->caps.log_num_vlans) *
		dev->caps.num_ports;
	dev->caps.reserved_qps_cnt[MLX4_QP_REGION_FC_EXCH] = MLX4_NUM_FEXCH;

	if (dev_cap->dmfs_high_rate_qpn_base > 0 &&
	    dev->caps.flags2 & MLX4_DEV_CAP_FLAG2_FS_EN)
		dev->caps.dmfs_high_rate_qpn_base = dev_cap->dmfs_high_rate_qpn_base;
	else
		dev->caps.dmfs_high_rate_qpn_base =
			dev->caps.reserved_qps_cnt[MLX4_QP_REGION_FW];

	if (dev_cap->dmfs_high_rate_qpn_range > 0 &&
	    dev->caps.flags2 & MLX4_DEV_CAP_FLAG2_FS_EN) {
		dev->caps.dmfs_high_rate_qpn_range = dev_cap->dmfs_high_rate_qpn_range;
		dev->caps.dmfs_high_steer_mode = MLX4_STEERING_DMFS_A0_DEFAULT;
		dev->caps.flags2 |= MLX4_DEV_CAP_FLAG2_FS_A0;
	} else {
		dev->caps.dmfs_high_steer_mode = MLX4_STEERING_DMFS_A0_NOT_SUPPORTED;
		dev->caps.dmfs_high_rate_qpn_base =
			dev->caps.reserved_qps_cnt[MLX4_QP_REGION_FW];
		dev->caps.dmfs_high_rate_qpn_range = MLX4_A0_STEERING_TABLE_SIZE;
	}

	dev->caps.rl_caps = dev_cap->rl_caps;

	dev->caps.reserved_qps_cnt[MLX4_QP_REGION_RSS_RAW_ETH] =
		dev->caps.dmfs_high_rate_qpn_range;

	dev->caps.reserved_qps = dev->caps.reserved_qps_cnt[MLX4_QP_REGION_FW] +
		dev->caps.reserved_qps_cnt[MLX4_QP_REGION_ETH_ADDR] +
		dev->caps.reserved_qps_cnt[MLX4_QP_REGION_FC_ADDR] +
		dev->caps.reserved_qps_cnt[MLX4_QP_REGION_FC_EXCH];

	dev->caps.sqp_demux = (mlx4_is_master(dev)) ? MLX4_MAX_NUM_SLAVES : 0;

	if (!enable_64b_cqe_eqe && !mlx4_is_slave(dev)) {
		if (dev_cap->flags &
		    (MLX4_DEV_CAP_FLAG_64B_CQE | MLX4_DEV_CAP_FLAG_64B_EQE)) {
			mlx4_warn(dev, "64B EQEs/CQEs supported by the device but not enabled\n");
			dev->caps.flags &= ~MLX4_DEV_CAP_FLAG_64B_CQE;
			dev->caps.flags &= ~MLX4_DEV_CAP_FLAG_64B_EQE;
		}

		if (dev_cap->flags2 &
		    (MLX4_DEV_CAP_FLAG2_CQE_STRIDE |
		     MLX4_DEV_CAP_FLAG2_EQE_STRIDE)) {
			mlx4_warn(dev, "Disabling EQE/CQE stride per user request\n");
			dev_cap->flags2 &= ~MLX4_DEV_CAP_FLAG2_CQE_STRIDE;
			dev_cap->flags2 &= ~MLX4_DEV_CAP_FLAG2_EQE_STRIDE;
		}
	}

	if ((dev->caps.flags &
	    (MLX4_DEV_CAP_FLAG_64B_CQE | MLX4_DEV_CAP_FLAG_64B_EQE)) &&
	    mlx4_is_master(dev))
		dev->caps.function_caps |= MLX4_FUNC_CAP_64B_EQE_CQE;

	if (!mlx4_is_slave(dev)) {
		mlx4_enable_cqe_eqe_stride(dev);
		dev->caps.alloc_res_qp_mask =
			(dev->caps.bf_reg_size ? MLX4_RESERVE_ETH_BF_QP : 0) |
			MLX4_RESERVE_A0_QP;

		if (!(dev->caps.flags2 & MLX4_DEV_CAP_FLAG2_ETS_CFG) &&
		    dev->caps.flags & MLX4_DEV_CAP_FLAG_SET_ETH_SCHED) {
			mlx4_warn(dev, "Old device ETS support detected\n");
			mlx4_warn(dev, "Consider upgrading device FW.\n");
			dev->caps.flags2 |= MLX4_DEV_CAP_FLAG2_ETS_CFG;
		}

	} else {
		dev->caps.alloc_res_qp_mask = 0;
	}

	mlx4_enable_ignore_fcs(dev);

	return 0;
}

/*The function checks if there are live vf, return the num of them*/
static int mlx4_how_many_lives_vf(struct mlx4_dev *dev)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	struct mlx4_slave_state *s_state;
	int i;
	int ret = 0;

	for (i = 1/*the ppf is 0*/; i < dev->num_slaves; ++i) {
		s_state = &priv->mfunc.master.slave_state[i];
		if (s_state->active && s_state->last_cmd !=
		    MLX4_COMM_CMD_RESET) {
			mlx4_warn(dev, "%s: slave: %d is still active\n",
				  __func__, i);
			ret++;
		}
	}
	return ret;
}

int mlx4_get_parav_qkey(struct mlx4_dev *dev, u32 qpn, u32 *qkey)
{
	u32 qk = MLX4_RESERVED_QKEY_BASE;

	if (qpn >= dev->phys_caps.base_tunnel_sqpn + 8 * MLX4_MFUNC_MAX ||
	    qpn < dev->phys_caps.base_proxy_sqpn)
		return -EINVAL;

	if (qpn >= dev->phys_caps.base_tunnel_sqpn)
		/* tunnel qp */
		qk += qpn - dev->phys_caps.base_tunnel_sqpn;
	else
		qk += qpn - dev->phys_caps.base_proxy_sqpn;
	*qkey = qk;
	return 0;
}
EXPORT_SYMBOL(mlx4_get_parav_qkey);

void mlx4_sync_pkey_table(struct mlx4_dev *dev, int slave, int port, int i, int val)
{
	struct mlx4_priv *priv = container_of(dev, struct mlx4_priv, dev);

	if (!mlx4_is_master(dev))
		return;

	priv->virt2phys_pkey[slave][port - 1][i] = val;
}
EXPORT_SYMBOL(mlx4_sync_pkey_table);

void mlx4_put_slave_node_guid(struct mlx4_dev *dev, int slave, __be64 guid)
{
	struct mlx4_priv *priv = container_of(dev, struct mlx4_priv, dev);

	if (!mlx4_is_master(dev))
		return;

	priv->slave_node_guids[slave] = guid;
}
EXPORT_SYMBOL(mlx4_put_slave_node_guid);

__be64 mlx4_get_slave_node_guid(struct mlx4_dev *dev, int slave)
{
	struct mlx4_priv *priv = container_of(dev, struct mlx4_priv, dev);

	if (!mlx4_is_master(dev))
		return 0;

	return priv->slave_node_guids[slave];
}
EXPORT_SYMBOL(mlx4_get_slave_node_guid);

int mlx4_is_slave_active(struct mlx4_dev *dev, int slave)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	struct mlx4_slave_state *s_slave;

	if (!mlx4_is_master(dev))
		return 0;

	s_slave = &priv->mfunc.master.slave_state[slave];
	return !!s_slave->active;
}
EXPORT_SYMBOL(mlx4_is_slave_active);

void mlx4_handle_eth_header_mcast_prio(struct mlx4_net_trans_rule_hw_ctrl *ctrl,
				       struct _rule_hw *eth_header)
{
	if (is_multicast_ether_addr(eth_header->eth.dst_mac) ||
	    is_broadcast_ether_addr(eth_header->eth.dst_mac)) {
		struct mlx4_net_trans_rule_hw_eth *eth =
			(struct mlx4_net_trans_rule_hw_eth *)eth_header;
		struct _rule_hw *next_rule = (struct _rule_hw *)(eth + 1);
		bool last_rule = next_rule->size == 0 && next_rule->id == 0 &&
			next_rule->rsvd == 0;

		if (last_rule)
			ctrl->prio = cpu_to_be16(MLX4_DOMAIN_NIC);
	}
}
EXPORT_SYMBOL(mlx4_handle_eth_header_mcast_prio);

static void slave_adjust_steering_mode(struct mlx4_dev *dev,
				       struct mlx4_dev_cap *dev_cap,
				       struct mlx4_init_hca_param *hca_param)
{
	dev->caps.steering_mode = hca_param->steering_mode;
	if (dev->caps.steering_mode == MLX4_STEERING_MODE_DEVICE_MANAGED) {
		dev->caps.num_qp_per_mgm = dev_cap->fs_max_num_qp_per_entry;
		dev->caps.fs_log_max_ucast_qp_range_size =
			dev_cap->fs_log_max_ucast_qp_range_size;
	} else
		dev->caps.num_qp_per_mgm =
			4 * ((1 << hca_param->log_mc_entry_sz)/16 - 2);

	mlx4_dbg(dev, "Steering mode is: %s\n",
		 mlx4_steering_mode_str(dev->caps.steering_mode));
}

static void mlx4_slave_destroy_special_qp_cap(struct mlx4_dev *dev)
{
	kfree(dev->caps.spec_qps);
	dev->caps.spec_qps = NULL;
}

static int mlx4_slave_special_qp_cap(struct mlx4_dev *dev)
{
	struct mlx4_func_cap *func_cap = NULL;
	struct mlx4_caps *caps = &dev->caps;
	int i, err = 0;

	func_cap = kzalloc(sizeof(*func_cap), GFP_KERNEL);
	caps->spec_qps = kcalloc(caps->num_ports, sizeof(*caps->spec_qps), GFP_KERNEL);

	if (!func_cap || !caps->spec_qps) {
		mlx4_err(dev, "Failed to allocate memory for special qps cap\n");
		err = -ENOMEM;
		goto err_mem;
	}

	for (i = 1; i <= caps->num_ports; ++i) {
		err = mlx4_QUERY_FUNC_CAP(dev, i, func_cap);
		if (err) {
			mlx4_err(dev, "QUERY_FUNC_CAP port command failed for port %d, aborting (%d)\n",
				 i, err);
			goto err_mem;
		}
		caps->spec_qps[i - 1] = func_cap->spec_qps;
		caps->port_mask[i] = caps->port_type[i];
		caps->phys_port_id[i] = func_cap->phys_port_id;
		err = mlx4_get_slave_pkey_gid_tbl_len(dev, i,
						      &caps->gid_table_len[i],
						      &caps->pkey_table_len[i]);
		if (err) {
			mlx4_err(dev, "QUERY_PORT command failed for port %d, aborting (%d)\n",
				 i, err);
			goto err_mem;
		}
	}

err_mem:
	if (err)
		mlx4_slave_destroy_special_qp_cap(dev);
	kfree(func_cap);
	return err;
}

static int mlx4_slave_cap(struct mlx4_dev *dev)
{
	int			   err;
	u32			   page_size;
	struct mlx4_dev_cap	   *dev_cap = NULL;
	struct mlx4_func_cap	   *func_cap = NULL;
	struct mlx4_init_hca_param *hca_param = NULL;

	hca_param = kzalloc(sizeof(*hca_param), GFP_KERNEL);
	func_cap = kzalloc(sizeof(*func_cap), GFP_KERNEL);
	dev_cap = kzalloc(sizeof(*dev_cap), GFP_KERNEL);
	if (!hca_param || !func_cap || !dev_cap) {
		mlx4_err(dev, "Failed to allocate memory for slave_cap\n");
		err = -ENOMEM;
		goto free_mem;
	}

	err = mlx4_QUERY_HCA(dev, hca_param);
	if (err) {
		mlx4_err(dev, "QUERY_HCA command failed, aborting\n");
		goto free_mem;
	}

	/* fail if the hca has an unknown global capability
	 * at this time global_caps should be always zeroed
	 */
	if (hca_param->global_caps) {
		mlx4_err(dev, "Unknown hca global capabilities\n");
		err = -EINVAL;
		goto free_mem;
	}

	dev->caps.hca_core_clock = hca_param->hca_core_clock;

	dev->caps.max_qp_dest_rdma = 1 << hca_param->log_rd_per_qp;
	err = mlx4_dev_cap(dev, dev_cap);
	if (err) {
		mlx4_err(dev, "QUERY_DEV_CAP command failed, aborting\n");
		goto free_mem;
	}

	err = mlx4_QUERY_FW(dev);
	if (err)
		mlx4_err(dev, "QUERY_FW command failed: could not get FW version\n");

	page_size = ~dev->caps.page_size_cap + 1;
	mlx4_warn(dev, "HCA minimum page size:%d\n", page_size);
	if (page_size > PAGE_SIZE) {
		mlx4_err(dev, "HCA minimum page size of %d bigger than kernel PAGE_SIZE of %ld, aborting\n",
			 page_size, PAGE_SIZE);
		err = -ENODEV;
		goto free_mem;
	}

	/* Set uar_page_shift for VF */
	dev->uar_page_shift = hca_param->uar_page_sz + 12;

	/* Make sure the master uar page size is valid */
	if (dev->uar_page_shift > PAGE_SHIFT) {
		mlx4_err(dev,
			 "Invalid configuration: uar page size is larger than system page size\n");
		err = -ENODEV;
		goto free_mem;
	}

	/* Set reserved_uars based on the uar_page_shift */
	mlx4_set_num_reserved_uars(dev, dev_cap);

	/* Although uar page size in FW differs from system page size,
	 * upper software layers (mlx4_ib, mlx4_en and part of mlx4_core)
	 * still works with assumption that uar page size == system page size
	 */
	dev->caps.uar_page_size = PAGE_SIZE;

	err = mlx4_QUERY_FUNC_CAP(dev, 0, func_cap);
	if (err) {
		mlx4_err(dev, "QUERY_FUNC_CAP general command failed, aborting (%d)\n",
			 err);
		goto free_mem;
	}

	if ((func_cap->pf_context_behaviour | PF_CONTEXT_BEHAVIOUR_MASK) !=
	    PF_CONTEXT_BEHAVIOUR_MASK) {
		mlx4_err(dev, "Unknown pf context behaviour %x known flags %x\n",
			 func_cap->pf_context_behaviour,
			 PF_CONTEXT_BEHAVIOUR_MASK);
		err = -EINVAL;
		goto free_mem;
	}

	dev->caps.num_ports		= func_cap->num_ports;
	dev->quotas.qp			= func_cap->qp_quota;
	dev->quotas.srq			= func_cap->srq_quota;
	dev->quotas.cq			= func_cap->cq_quota;
	dev->quotas.mpt			= func_cap->mpt_quota;
	dev->quotas.mtt			= func_cap->mtt_quota;
	dev->caps.num_qps		= 1 << hca_param->log_num_qps;
	dev->caps.num_srqs		= 1 << hca_param->log_num_srqs;
	dev->caps.num_cqs		= 1 << hca_param->log_num_cqs;
	dev->caps.num_mpts		= 1 << hca_param->log_mpt_sz;
	dev->caps.num_eqs		= func_cap->max_eq;
	dev->caps.reserved_eqs		= func_cap->reserved_eq;
	dev->caps.reserved_lkey		= func_cap->reserved_lkey;
	dev->caps.num_pds               = MLX4_NUM_PDS;
	dev->caps.num_mgms              = 0;
	dev->caps.num_amgms             = 0;

	if (dev->caps.num_ports > MLX4_MAX_PORTS) {
		mlx4_err(dev, "HCA has %d ports, but we only support %d, aborting\n",
			 dev->caps.num_ports, MLX4_MAX_PORTS);
		err = -ENODEV;
		goto free_mem;
	}

	mlx4_replace_zero_macs(dev);

	err = mlx4_slave_special_qp_cap(dev);
	if (err) {
		mlx4_err(dev, "Set special QP caps failed. aborting\n");
		goto free_mem;
	}

	if (dev->caps.uar_page_size * (dev->caps.num_uars -
				       dev->caps.reserved_uars) >
				       pci_resource_len(dev->persist->pdev,
							2)) {
		mlx4_err(dev, "HCA reported UAR region size of 0x%x bigger than PCI resource 2 size of 0x%llx, aborting\n",
			 dev->caps.uar_page_size * dev->caps.num_uars,
			 (unsigned long long)
			 pci_resource_len(dev->persist->pdev, 2));
		err = -ENOMEM;
		goto err_mem;
	}

	if (hca_param->dev_cap_enabled & MLX4_DEV_CAP_64B_EQE_ENABLED) {
		dev->caps.eqe_size   = 64;
		dev->caps.eqe_factor = 1;
	} else {
		dev->caps.eqe_size   = 32;
		dev->caps.eqe_factor = 0;
	}

	if (hca_param->dev_cap_enabled & MLX4_DEV_CAP_64B_CQE_ENABLED) {
		dev->caps.cqe_size   = 64;
		dev->caps.userspace_caps |= MLX4_USER_DEV_CAP_LARGE_CQE;
	} else {
		dev->caps.cqe_size   = 32;
	}

	if (hca_param->dev_cap_enabled & MLX4_DEV_CAP_EQE_STRIDE_ENABLED) {
		dev->caps.eqe_size = hca_param->eqe_size;
		dev->caps.eqe_factor = 0;
	}

	if (hca_param->dev_cap_enabled & MLX4_DEV_CAP_CQE_STRIDE_ENABLED) {
		dev->caps.cqe_size = hca_param->cqe_size;
		/* User still need to know when CQE > 32B */
		dev->caps.userspace_caps |= MLX4_USER_DEV_CAP_LARGE_CQE;
	}

	dev->caps.flags2 &= ~MLX4_DEV_CAP_FLAG2_TS;
	mlx4_warn(dev, "Timestamping is not supported in slave mode\n");

	dev->caps.flags2 &= ~MLX4_DEV_CAP_FLAG2_USER_MAC_EN;
	mlx4_dbg(dev, "User MAC FW update is not supported in slave mode\n");

	slave_adjust_steering_mode(dev, dev_cap, hca_param);
	mlx4_dbg(dev, "RSS support for IP fragments is %s\n",
		 hca_param->rss_ip_frags ? "on" : "off");

	if (func_cap->extra_flags & MLX4_QUERY_FUNC_FLAGS_BF_RES_QP &&
	    dev->caps.bf_reg_size)
		dev->caps.alloc_res_qp_mask |= MLX4_RESERVE_ETH_BF_QP;

	if (func_cap->extra_flags & MLX4_QUERY_FUNC_FLAGS_A0_RES_QP)
		dev->caps.alloc_res_qp_mask |= MLX4_RESERVE_A0_QP;

err_mem:
	if (err)
		mlx4_slave_destroy_special_qp_cap(dev);
free_mem:
	kfree(hca_param);
	kfree(func_cap);
	kfree(dev_cap);
	return err;
}

static void mlx4_request_modules(struct mlx4_dev *dev)
{
	int port;
	int has_ib_port = false;
	int has_eth_port = false;
#define EN_DRV_NAME	"mlx4_en"
#define IB_DRV_NAME	"mlx4_ib"

	for (port = 1; port <= dev->caps.num_ports; port++) {
		if (dev->caps.port_type[port] == MLX4_PORT_TYPE_IB)
			has_ib_port = true;
		else if (dev->caps.port_type[port] == MLX4_PORT_TYPE_ETH)
			has_eth_port = true;
	}

	if (has_eth_port)
		request_module_nowait(EN_DRV_NAME);
	if (has_ib_port || (dev->caps.flags & MLX4_DEV_CAP_FLAG_IBOE))
		request_module_nowait(IB_DRV_NAME);
}

/*
 * Change the port configuration of the device.
 * Every user of this function must hold the port mutex.
 */
int mlx4_change_port_types(struct mlx4_dev *dev,
			   enum mlx4_port_type *port_types)
{
	int err = 0;
	int change = 0;
	int port;

	for (port = 0; port <  dev->caps.num_ports; port++) {
		/* Change the port type only if the new type is different
		 * from the current, and not set to Auto */
		if (port_types[port] != dev->caps.port_type[port + 1])
			change = 1;
	}
	if (change) {
		mlx4_unregister_device(dev);
		for (port = 1; port <= dev->caps.num_ports; port++) {
			mlx4_CLOSE_PORT(dev, port);
			dev->caps.port_type[port] = port_types[port - 1];
			err = mlx4_SET_PORT(dev, port, -1);
			if (err) {
				mlx4_err(dev, "Failed to set port %d, aborting\n",
					 port);
				goto out;
			}
		}
		mlx4_set_port_mask(dev);
		err = mlx4_register_device(dev);
		if (err) {
			mlx4_err(dev, "Failed to register device\n");
			goto out;
		}
		mlx4_request_modules(dev);
	}

out:
	return err;
}

static ssize_t show_port_type(struct device *dev,
			      struct device_attribute *attr,
			      char *buf)
{
	struct mlx4_port_info *info = container_of(attr, struct mlx4_port_info,
						   port_attr);
	struct mlx4_dev *mdev = info->dev;
	char type[8];

	sprintf(type, "%s",
		(mdev->caps.port_type[info->port] == MLX4_PORT_TYPE_IB) ?
		"ib" : "eth");
	if (mdev->caps.possible_type[info->port] == MLX4_PORT_TYPE_AUTO)
		sprintf(buf, "auto (%s)\n", type);
	else
		sprintf(buf, "%s\n", type);

	return strlen(buf);
}

static int __set_port_type(struct mlx4_port_info *info,
			   enum mlx4_port_type port_type)
{
	struct mlx4_dev *mdev = info->dev;
	struct mlx4_priv *priv = mlx4_priv(mdev);
	enum mlx4_port_type types[MLX4_MAX_PORTS];
	enum mlx4_port_type new_types[MLX4_MAX_PORTS];
	int i;
	int err = 0;

	if ((port_type & mdev->caps.supported_type[info->port]) != port_type) {
		mlx4_err(mdev,
			 "Requested port type for port %d is not supported on this HCA\n",
			 info->port);
		return -EOPNOTSUPP;
	}

	mlx4_stop_sense(mdev);
	mutex_lock(&priv->port_mutex);
	info->tmp_type = port_type;

	/* Possible type is always the one that was delivered */
	mdev->caps.possible_type[info->port] = info->tmp_type;

	for (i = 0; i < mdev->caps.num_ports; i++) {
		types[i] = priv->port[i+1].tmp_type ? priv->port[i+1].tmp_type :
					mdev->caps.possible_type[i+1];
		if (types[i] == MLX4_PORT_TYPE_AUTO)
			types[i] = mdev->caps.port_type[i+1];
	}

	if (!(mdev->caps.flags & MLX4_DEV_CAP_FLAG_DPDP) &&
	    !(mdev->caps.flags & MLX4_DEV_CAP_FLAG_SENSE_SUPPORT)) {
		for (i = 1; i <= mdev->caps.num_ports; i++) {
			if (mdev->caps.possible_type[i] == MLX4_PORT_TYPE_AUTO) {
				mdev->caps.possible_type[i] = mdev->caps.port_type[i];
				err = -EOPNOTSUPP;
			}
		}
	}
	if (err) {
		mlx4_err(mdev, "Auto sensing is not supported on this HCA. Set only 'eth' or 'ib' for both ports (should be the same)\n");
		goto out;
	}

	mlx4_do_sense_ports(mdev, new_types, types);

	err = mlx4_check_port_params(mdev, new_types);
	if (err)
		goto out;

	/* We are about to apply the changes after the configuration
	 * was verified, no need to remember the temporary types
	 * any more */
	for (i = 0; i < mdev->caps.num_ports; i++)
		priv->port[i + 1].tmp_type = 0;

	err = mlx4_change_port_types(mdev, new_types);

out:
	mlx4_start_sense(mdev);
	mutex_unlock(&priv->port_mutex);

	return err;
}

static ssize_t set_port_type(struct device *dev,
			     struct device_attribute *attr,
			     const char *buf, size_t count)
{
	struct mlx4_port_info *info = container_of(attr, struct mlx4_port_info,
						   port_attr);
	struct mlx4_dev *mdev = info->dev;
	enum mlx4_port_type port_type;
	static DEFINE_MUTEX(set_port_type_mutex);
	int err;

	mutex_lock(&set_port_type_mutex);

	if (!strcmp(buf, "ib\n")) {
		port_type = MLX4_PORT_TYPE_IB;
	} else if (!strcmp(buf, "eth\n")) {
		port_type = MLX4_PORT_TYPE_ETH;
	} else if (!strcmp(buf, "auto\n")) {
		port_type = MLX4_PORT_TYPE_AUTO;
	} else {
		mlx4_err(mdev, "%s is not supported port type\n", buf);
		err = -EINVAL;
		goto err_out;
	}

	err = __set_port_type(info, port_type);

err_out:
	mutex_unlock(&set_port_type_mutex);

	return err ? err : count;
}

enum ibta_mtu {
	IB_MTU_256  = 1,
	IB_MTU_512  = 2,
	IB_MTU_1024 = 3,
	IB_MTU_2048 = 4,
	IB_MTU_4096 = 5
};

static inline int int_to_ibta_mtu(int mtu)
{
	switch (mtu) {
	case 256:  return IB_MTU_256;
	case 512:  return IB_MTU_512;
	case 1024: return IB_MTU_1024;
	case 2048: return IB_MTU_2048;
	case 4096: return IB_MTU_4096;
	default: return -1;
	}
}

static inline int ibta_mtu_to_int(enum ibta_mtu mtu)
{
	switch (mtu) {
	case IB_MTU_256:  return  256;
	case IB_MTU_512:  return  512;
	case IB_MTU_1024: return 1024;
	case IB_MTU_2048: return 2048;
	case IB_MTU_4096: return 4096;
	default: return -1;
	}
}

static ssize_t show_port_ib_mtu(struct device *dev,
			     struct device_attribute *attr,
			     char *buf)
{
	struct mlx4_port_info *info = container_of(attr, struct mlx4_port_info,
						   port_mtu_attr);
	struct mlx4_dev *mdev = info->dev;

	if (mdev->caps.port_type[info->port] == MLX4_PORT_TYPE_ETH)
		mlx4_warn(mdev, "port level mtu is only used for IB ports\n");

	sprintf(buf, "%d\n",
			ibta_mtu_to_int(mdev->caps.port_ib_mtu[info->port]));
	return strlen(buf);
}

static ssize_t set_port_ib_mtu(struct device *dev,
			     struct device_attribute *attr,
			     const char *buf, size_t count)
{
	struct mlx4_port_info *info = container_of(attr, struct mlx4_port_info,
						   port_mtu_attr);
	struct mlx4_dev *mdev = info->dev;
	struct mlx4_priv *priv = mlx4_priv(mdev);
	int err, port, mtu, ibta_mtu = -1;

	if (mdev->caps.port_type[info->port] == MLX4_PORT_TYPE_ETH) {
		mlx4_warn(mdev, "port level mtu is only used for IB ports\n");
		return -EINVAL;
	}

	err = kstrtoint(buf, 0, &mtu);
	if (!err)
		ibta_mtu = int_to_ibta_mtu(mtu);

	if (err || ibta_mtu < 0) {
		mlx4_err(mdev, "%s is invalid IBTA mtu\n", buf);
		return -EINVAL;
	}

	mdev->caps.port_ib_mtu[info->port] = ibta_mtu;

	mlx4_stop_sense(mdev);
	mutex_lock(&priv->port_mutex);
	mlx4_unregister_device(mdev);
	for (port = 1; port <= mdev->caps.num_ports; port++) {
		mlx4_CLOSE_PORT(mdev, port);
		err = mlx4_SET_PORT(mdev, port, -1);
		if (err) {
			mlx4_err(mdev, "Failed to set port %d, aborting\n",
				 port);
			goto err_set_port;
		}
	}
	err = mlx4_register_device(mdev);
err_set_port:
	mutex_unlock(&priv->port_mutex);
	mlx4_start_sense(mdev);
	return err ? err : count;
}

/* bond for multi-function device */
#define MAX_MF_BOND_ALLOWED_SLAVES 63
static int mlx4_mf_bond(struct mlx4_dev *dev)
{
	int err = 0;
	int nvfs;
	struct mlx4_slaves_pport slaves_port1;
	struct mlx4_slaves_pport slaves_port2;
	DECLARE_BITMAP(slaves_port_1_2, MLX4_MFUNC_MAX);

	slaves_port1 = mlx4_phys_to_slaves_pport(dev, 1);
	slaves_port2 = mlx4_phys_to_slaves_pport(dev, 2);
	bitmap_and(slaves_port_1_2,
		   slaves_port1.slaves, slaves_port2.slaves,
		   dev->persist->num_vfs + 1);

	/* only single port vfs are allowed */
	if (bitmap_weight(slaves_port_1_2, dev->persist->num_vfs + 1) > 1) {
		mlx4_warn(dev, "HA mode unsupported for dual ported VFs\n");
		return -EINVAL;
	}

	/* number of virtual functions is number of total functions minus one
	 * physical function for each port.
	 */
	nvfs = bitmap_weight(slaves_port1.slaves, dev->persist->num_vfs + 1) +
		bitmap_weight(slaves_port2.slaves, dev->persist->num_vfs + 1) - 2;

	/* limit on maximum allowed VFs */
	if (nvfs > MAX_MF_BOND_ALLOWED_SLAVES) {
		mlx4_warn(dev, "HA mode is not supported for %d VFs (max %d are allowed)\n",
			  nvfs, MAX_MF_BOND_ALLOWED_SLAVES);
		return -EINVAL;
	}

	if (dev->caps.steering_mode != MLX4_STEERING_MODE_DEVICE_MANAGED) {
		mlx4_warn(dev, "HA mode unsupported for NON DMFS steering\n");
		return -EINVAL;
	}

	err = mlx4_bond_mac_table(dev);
	if (err)
		return err;
	err = mlx4_bond_vlan_table(dev);
	if (err)
		goto err1;
	err = mlx4_bond_fs_rules(dev);
	if (err)
		goto err2;

	return 0;
err2:
	(void)mlx4_unbond_vlan_table(dev);
err1:
	(void)mlx4_unbond_mac_table(dev);
	return err;
}

static int mlx4_mf_unbond(struct mlx4_dev *dev)
{
	int ret, ret1;

	ret = mlx4_unbond_fs_rules(dev);
	if (ret)
		mlx4_warn(dev, "multifunction unbond for flow rules failed (%d)\n", ret);
	ret1 = mlx4_unbond_mac_table(dev);
	if (ret1) {
		mlx4_warn(dev, "multifunction unbond for MAC table failed (%d)\n", ret1);
		ret = ret1;
	}
	ret1 = mlx4_unbond_vlan_table(dev);
	if (ret1) {
		mlx4_warn(dev, "multifunction unbond for VLAN table failed (%d)\n", ret1);
		ret = ret1;
	}
	return ret;
}

int mlx4_bond(struct mlx4_dev *dev)
{
	int ret = 0;
	struct mlx4_priv *priv = mlx4_priv(dev);

	mutex_lock(&priv->bond_mutex);

	if (!mlx4_is_bonded(dev)) {
		ret = mlx4_do_bond(dev, true);
		if (ret)
			mlx4_err(dev, "Failed to bond device: %d\n", ret);
		if (!ret && mlx4_is_master(dev)) {
			ret = mlx4_mf_bond(dev);
			if (ret) {
				mlx4_err(dev, "bond for multifunction failed\n");
				mlx4_do_bond(dev, false);
			}
		}
	}

	mutex_unlock(&priv->bond_mutex);
	if (!ret)
		mlx4_dbg(dev, "Device is bonded\n");

	return ret;
}
EXPORT_SYMBOL_GPL(mlx4_bond);

int mlx4_unbond(struct mlx4_dev *dev)
{
	int ret = 0;
	struct mlx4_priv *priv = mlx4_priv(dev);

	mutex_lock(&priv->bond_mutex);

	if (mlx4_is_bonded(dev)) {
		int ret2 = 0;

		ret = mlx4_do_bond(dev, false);
		if (ret)
			mlx4_err(dev, "Failed to unbond device: %d\n", ret);
		if (mlx4_is_master(dev))
			ret2 = mlx4_mf_unbond(dev);
		if (ret2) {
			mlx4_warn(dev, "Failed to unbond device for multifunction (%d)\n", ret2);
			ret = ret2;
		}
	}

	mutex_unlock(&priv->bond_mutex);
	if (!ret)
		mlx4_dbg(dev, "Device is unbonded\n");

	return ret;
}
EXPORT_SYMBOL_GPL(mlx4_unbond);


int mlx4_port_map_set(struct mlx4_dev *dev, struct mlx4_port_map *v2p)
{
	u8 port1 = v2p->port1;
	u8 port2 = v2p->port2;
	struct mlx4_priv *priv = mlx4_priv(dev);
	int err;

	if (!(dev->caps.flags2 & MLX4_DEV_CAP_FLAG2_PORT_REMAP))
		return -EOPNOTSUPP;

	mutex_lock(&priv->bond_mutex);

	/* zero means keep current mapping for this port */
	if (port1 == 0)
		port1 = priv->v2p.port1;
	if (port2 == 0)
		port2 = priv->v2p.port2;

	if ((port1 < 1) || (port1 > MLX4_MAX_PORTS) ||
	    (port2 < 1) || (port2 > MLX4_MAX_PORTS) ||
	    (port1 == 2 && port2 == 1)) {
		/* besides boundary checks cross mapping makes
		 * no sense and therefore not allowed */
		err = -EINVAL;
	} else if ((port1 == priv->v2p.port1) &&
		 (port2 == priv->v2p.port2)) {
		err = 0;
	} else {
		err = mlx4_virt2phy_port_map(dev, port1, port2);
		if (!err) {
			mlx4_dbg(dev, "port map changed: [%d][%d]\n",
				 port1, port2);
			priv->v2p.port1 = port1;
			priv->v2p.port2 = port2;
		} else {
			mlx4_err(dev, "Failed to change port mape: %d\n", err);
		}
	}

	mutex_unlock(&priv->bond_mutex);
	return err;
}
EXPORT_SYMBOL_GPL(mlx4_port_map_set);

static int mlx4_load_fw(struct mlx4_dev *dev)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	int err;

	priv->fw.fw_icm = mlx4_alloc_icm(dev, priv->fw.fw_pages,
					 GFP_HIGHUSER | __GFP_NOWARN, 0);
	if (!priv->fw.fw_icm) {
		mlx4_err(dev, "Couldn't allocate FW area, aborting\n");
		return -ENOMEM;
	}

	err = mlx4_MAP_FA(dev, priv->fw.fw_icm);
	if (err) {
		mlx4_err(dev, "MAP_FA command failed, aborting\n");
		goto err_free;
	}

	err = mlx4_RUN_FW(dev);
	if (err) {
		mlx4_err(dev, "RUN_FW command failed, aborting\n");
		goto err_unmap_fa;
	}

	return 0;

err_unmap_fa:
	mlx4_UNMAP_FA(dev);

err_free:
	mlx4_free_icm(dev, priv->fw.fw_icm, 0);
	return err;
}

static int mlx4_init_cmpt_table(struct mlx4_dev *dev, u64 cmpt_base,
				int cmpt_entry_sz)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	int err;
	int num_eqs;

	err = mlx4_init_icm_table(dev, &priv->qp_table.cmpt_table,
				  cmpt_base +
				  ((u64) (MLX4_CMPT_TYPE_QP *
					  cmpt_entry_sz) << MLX4_CMPT_SHIFT),
				  cmpt_entry_sz, dev->caps.num_qps,
				  dev->caps.reserved_qps_cnt[MLX4_QP_REGION_FW],
				  0, 0);
	if (err)
		goto err;

	err = mlx4_init_icm_table(dev, &priv->srq_table.cmpt_table,
				  cmpt_base +
				  ((u64) (MLX4_CMPT_TYPE_SRQ *
					  cmpt_entry_sz) << MLX4_CMPT_SHIFT),
				  cmpt_entry_sz, dev->caps.num_srqs,
				  dev->caps.reserved_srqs, 0, 0);
	if (err)
		goto err_qp;

	err = mlx4_init_icm_table(dev, &priv->cq_table.cmpt_table,
				  cmpt_base +
				  ((u64) (MLX4_CMPT_TYPE_CQ *
					  cmpt_entry_sz) << MLX4_CMPT_SHIFT),
				  cmpt_entry_sz, dev->caps.num_cqs,
				  dev->caps.reserved_cqs, 0, 0);
	if (err)
		goto err_srq;

	num_eqs = dev->phys_caps.num_phys_eqs;
	err = mlx4_init_icm_table(dev, &priv->eq_table.cmpt_table,
				  cmpt_base +
				  ((u64) (MLX4_CMPT_TYPE_EQ *
					  cmpt_entry_sz) << MLX4_CMPT_SHIFT),
				  cmpt_entry_sz, num_eqs, num_eqs, 0, 0);
	if (err)
		goto err_cq;

	return 0;

err_cq:
	mlx4_cleanup_icm_table(dev, &priv->cq_table.cmpt_table);

err_srq:
	mlx4_cleanup_icm_table(dev, &priv->srq_table.cmpt_table);

err_qp:
	mlx4_cleanup_icm_table(dev, &priv->qp_table.cmpt_table);

err:
	return err;
}

static int mlx4_init_icm(struct mlx4_dev *dev, struct mlx4_dev_cap *dev_cap,
			 struct mlx4_init_hca_param *init_hca, u64 icm_size)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	u64 aux_pages;
	int num_eqs;
	int err;

	err = mlx4_SET_ICM_SIZE(dev, icm_size, &aux_pages);
	if (err) {
		mlx4_err(dev, "SET_ICM_SIZE command failed, aborting\n");
		return err;
	}

	mlx4_dbg(dev, "%lld KB of HCA context requires %lld KB aux memory\n",
		 (unsigned long long) icm_size >> 10,
		 (unsigned long long) aux_pages << 2);

	priv->fw.aux_icm = mlx4_alloc_icm(dev, aux_pages,
					  GFP_HIGHUSER | __GFP_NOWARN, 0);
	if (!priv->fw.aux_icm) {
		mlx4_err(dev, "Couldn't allocate aux memory, aborting\n");
		return -ENOMEM;
	}

	err = mlx4_MAP_ICM_AUX(dev, priv->fw.aux_icm);
	if (err) {
		mlx4_err(dev, "MAP_ICM_AUX command failed, aborting\n");
		goto err_free_aux;
	}

	err = mlx4_init_cmpt_table(dev, init_hca->cmpt_base, dev_cap->cmpt_entry_sz);
	if (err) {
		mlx4_err(dev, "Failed to map cMPT context memory, aborting\n");
		goto err_unmap_aux;
	}


	num_eqs = dev->phys_caps.num_phys_eqs;
	err = mlx4_init_icm_table(dev, &priv->eq_table.table,
				  init_hca->eqc_base, dev_cap->eqc_entry_sz,
				  num_eqs, num_eqs, 0, 0);
	if (err) {
		mlx4_err(dev, "Failed to map EQ context memory, aborting\n");
		goto err_unmap_cmpt;
	}

	/*
	 * Reserved MTT entries must be aligned up to a cacheline
	 * boundary, since the FW will write to them, while the driver
	 * writes to all other MTT entries. (The variable
	 * dev->caps.mtt_entry_sz below is really the MTT segment
	 * size, not the raw entry size)
	 */
	dev->caps.reserved_mtts =
		ALIGN(dev->caps.reserved_mtts * dev->caps.mtt_entry_sz,
		      dma_get_cache_alignment()) / dev->caps.mtt_entry_sz;

	err = mlx4_init_icm_table(dev, &priv->mr_table.mtt_table,
				  init_hca->mtt_base,
				  dev->caps.mtt_entry_sz,
				  dev->caps.num_mtts,
				  dev->caps.reserved_mtts, 1, 0);
	if (err) {
		mlx4_err(dev, "Failed to map MTT context memory, aborting\n");
		goto err_unmap_eq;
	}

	err = mlx4_init_icm_table(dev, &priv->mr_table.dmpt_table,
				  init_hca->dmpt_base,
				  dev_cap->dmpt_entry_sz,
				  dev->caps.num_mpts,
				  dev->caps.reserved_mrws, 1, 1);
	if (err) {
		mlx4_err(dev, "Failed to map dMPT context memory, aborting\n");
		goto err_unmap_mtt;
	}

	err = mlx4_init_icm_table(dev, &priv->qp_table.qp_table,
				  init_hca->qpc_base,
				  dev_cap->qpc_entry_sz,
				  dev->caps.num_qps,
				  dev->caps.reserved_qps_cnt[MLX4_QP_REGION_FW],
				  0, 0);
	if (err) {
		mlx4_err(dev, "Failed to map QP context memory, aborting\n");
		goto err_unmap_dmpt;
	}

	err = mlx4_init_icm_table(dev, &priv->qp_table.auxc_table,
				  init_hca->auxc_base,
				  dev_cap->aux_entry_sz,
				  dev->caps.num_qps,
				  dev->caps.reserved_qps_cnt[MLX4_QP_REGION_FW],
				  0, 0);
	if (err) {
		mlx4_err(dev, "Failed to map AUXC context memory, aborting\n");
		goto err_unmap_qp;
	}

	err = mlx4_init_icm_table(dev, &priv->qp_table.altc_table,
				  init_hca->altc_base,
				  dev_cap->altc_entry_sz,
				  dev->caps.num_qps,
				  dev->caps.reserved_qps_cnt[MLX4_QP_REGION_FW],
				  0, 0);
	if (err) {
		mlx4_err(dev, "Failed to map ALTC context memory, aborting\n");
		goto err_unmap_auxc;
	}

	err = mlx4_init_icm_table(dev, &priv->qp_table.rdmarc_table,
				  init_hca->rdmarc_base,
				  dev_cap->rdmarc_entry_sz << priv->qp_table.rdmarc_shift,
				  dev->caps.num_qps,
				  dev->caps.reserved_qps_cnt[MLX4_QP_REGION_FW],
				  0, 0);
	if (err) {
		mlx4_err(dev, "Failed to map RDMARC context memory, aborting\n");
		goto err_unmap_altc;
	}

	err = mlx4_init_icm_table(dev, &priv->cq_table.table,
				  init_hca->cqc_base,
				  dev_cap->cqc_entry_sz,
				  dev->caps.num_cqs,
				  dev->caps.reserved_cqs, 0, 0);
	if (err) {
		mlx4_err(dev, "Failed to map CQ context memory, aborting\n");
		goto err_unmap_rdmarc;
	}

	err = mlx4_init_icm_table(dev, &priv->srq_table.table,
				  init_hca->srqc_base,
				  dev_cap->srq_entry_sz,
				  dev->caps.num_srqs,
				  dev->caps.reserved_srqs, 0, 0);
	if (err) {
		mlx4_err(dev, "Failed to map SRQ context memory, aborting\n");
		goto err_unmap_cq;
	}

	/*
	 * For flow steering device managed mode it is required to use
	 * mlx4_init_icm_table. For B0 steering mode it's not strictly
	 * required, but for simplicity just map the whole multicast
	 * group table now.  The table isn't very big and it's a lot
	 * easier than trying to track ref counts.
	 */
	err = mlx4_init_icm_table(dev, &priv->mcg_table.table,
				  init_hca->mc_base,
				  mlx4_get_mgm_entry_size(dev),
				  dev->caps.num_mgms + dev->caps.num_amgms,
				  dev->caps.num_mgms + dev->caps.num_amgms,
				  0, 0);
	if (err) {
		mlx4_err(dev, "Failed to map MCG context memory, aborting\n");
		goto err_unmap_srq;
	}

	return 0;

err_unmap_srq:
	mlx4_cleanup_icm_table(dev, &priv->srq_table.table);

err_unmap_cq:
	mlx4_cleanup_icm_table(dev, &priv->cq_table.table);

err_unmap_rdmarc:
	mlx4_cleanup_icm_table(dev, &priv->qp_table.rdmarc_table);

err_unmap_altc:
	mlx4_cleanup_icm_table(dev, &priv->qp_table.altc_table);

err_unmap_auxc:
	mlx4_cleanup_icm_table(dev, &priv->qp_table.auxc_table);

err_unmap_qp:
	mlx4_cleanup_icm_table(dev, &priv->qp_table.qp_table);

err_unmap_dmpt:
	mlx4_cleanup_icm_table(dev, &priv->mr_table.dmpt_table);

err_unmap_mtt:
	mlx4_cleanup_icm_table(dev, &priv->mr_table.mtt_table);

err_unmap_eq:
	mlx4_cleanup_icm_table(dev, &priv->eq_table.table);

err_unmap_cmpt:
	mlx4_cleanup_icm_table(dev, &priv->eq_table.cmpt_table);
	mlx4_cleanup_icm_table(dev, &priv->cq_table.cmpt_table);
	mlx4_cleanup_icm_table(dev, &priv->srq_table.cmpt_table);
	mlx4_cleanup_icm_table(dev, &priv->qp_table.cmpt_table);

err_unmap_aux:
	mlx4_UNMAP_ICM_AUX(dev);

err_free_aux:
	mlx4_free_icm(dev, priv->fw.aux_icm, 0);

	return err;
}

static void mlx4_free_icms(struct mlx4_dev *dev)
{
	struct mlx4_priv *priv = mlx4_priv(dev);

	mlx4_cleanup_icm_table(dev, &priv->mcg_table.table);
	mlx4_cleanup_icm_table(dev, &priv->srq_table.table);
	mlx4_cleanup_icm_table(dev, &priv->cq_table.table);
	mlx4_cleanup_icm_table(dev, &priv->qp_table.rdmarc_table);
	mlx4_cleanup_icm_table(dev, &priv->qp_table.altc_table);
	mlx4_cleanup_icm_table(dev, &priv->qp_table.auxc_table);
	mlx4_cleanup_icm_table(dev, &priv->qp_table.qp_table);
	mlx4_cleanup_icm_table(dev, &priv->mr_table.dmpt_table);
	mlx4_cleanup_icm_table(dev, &priv->mr_table.mtt_table);
	mlx4_cleanup_icm_table(dev, &priv->eq_table.table);
	mlx4_cleanup_icm_table(dev, &priv->eq_table.cmpt_table);
	mlx4_cleanup_icm_table(dev, &priv->cq_table.cmpt_table);
	mlx4_cleanup_icm_table(dev, &priv->srq_table.cmpt_table);
	mlx4_cleanup_icm_table(dev, &priv->qp_table.cmpt_table);

	mlx4_UNMAP_ICM_AUX(dev);
	mlx4_free_icm(dev, priv->fw.aux_icm, 0);
}

static void mlx4_slave_exit(struct mlx4_dev *dev)
{
	struct mlx4_priv *priv = mlx4_priv(dev);

	mutex_lock(&priv->cmd.slave_cmd_mutex);
	if (mlx4_comm_cmd(dev, MLX4_COMM_CMD_RESET, 0, MLX4_COMM_CMD_NA_OP,
			  MLX4_COMM_TIME))
		mlx4_warn(dev, "Failed to close slave function\n");
	mutex_unlock(&priv->cmd.slave_cmd_mutex);
}

static int map_bf_area(struct mlx4_dev *dev)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	resource_size_t bf_start;
	resource_size_t bf_len;
	int err = 0;

	if (!dev->caps.bf_reg_size)
		return -ENXIO;

	bf_start = pci_resource_start(dev->persist->pdev, 2) +
			(dev->caps.num_uars << PAGE_SHIFT);
	bf_len = pci_resource_len(dev->persist->pdev, 2) -
			(dev->caps.num_uars << PAGE_SHIFT);
	priv->bf_mapping = io_mapping_create_wc(bf_start, bf_len);
	if (!priv->bf_mapping)
		err = -ENOMEM;

	return err;
}

static void unmap_bf_area(struct mlx4_dev *dev)
{
	if (mlx4_priv(dev)->bf_mapping)
		io_mapping_free(mlx4_priv(dev)->bf_mapping);
}

u64 mlx4_read_clock(struct mlx4_dev *dev)
{
	u32 clockhi, clocklo, clockhi1;
	u64 cycles;
	int i;
	struct mlx4_priv *priv = mlx4_priv(dev);

	for (i = 0; i < 10; i++) {
		clockhi = swab32(readl(priv->clock_mapping));
		clocklo = swab32(readl(priv->clock_mapping + 4));
		clockhi1 = swab32(readl(priv->clock_mapping));
		if (clockhi == clockhi1)
			break;
	}

	cycles = (u64) clockhi << 32 | (u64) clocklo;

	return cycles;
}
EXPORT_SYMBOL_GPL(mlx4_read_clock);


static int map_internal_clock(struct mlx4_dev *dev)
{
	struct mlx4_priv *priv = mlx4_priv(dev);

	priv->clock_mapping =
		ioremap(pci_resource_start(dev->persist->pdev,
					   priv->fw.clock_bar) +
			priv->fw.clock_offset, MLX4_CLOCK_SIZE);

	if (!priv->clock_mapping)
		return -ENOMEM;

	return 0;
}

int mlx4_get_internal_clock_params(struct mlx4_dev *dev,
				   struct mlx4_clock_params *params)
{
	struct mlx4_priv *priv = mlx4_priv(dev);

	if (mlx4_is_slave(dev))
		return -EOPNOTSUPP;

	if (!params)
		return -EINVAL;

	params->bar = priv->fw.clock_bar;
	params->offset = priv->fw.clock_offset;
	params->size = MLX4_CLOCK_SIZE;

	return 0;
}
EXPORT_SYMBOL_GPL(mlx4_get_internal_clock_params);

static void unmap_internal_clock(struct mlx4_dev *dev)
{
	struct mlx4_priv *priv = mlx4_priv(dev);

	if (priv->clock_mapping)
		iounmap(priv->clock_mapping);
}

static void mlx4_close_hca(struct mlx4_dev *dev)
{
	unmap_internal_clock(dev);
	unmap_bf_area(dev);
	if (mlx4_is_slave(dev))
		mlx4_slave_exit(dev);
	else {
		mlx4_CLOSE_HCA(dev, 0);
		mlx4_free_icms(dev);
	}
}

static void mlx4_close_fw(struct mlx4_dev *dev)
{
	if (!mlx4_is_slave(dev)) {
		mlx4_UNMAP_FA(dev);
		mlx4_free_icm(dev, mlx4_priv(dev)->fw.fw_icm, 0);
	}
}

static int mlx4_comm_check_offline(struct mlx4_dev *dev)
{
#define COMM_CHAN_OFFLINE_OFFSET 0x09

	u32 comm_flags;
	u32 offline_bit;
	unsigned long end;
	struct mlx4_priv *priv = mlx4_priv(dev);

	end = msecs_to_jiffies(MLX4_COMM_OFFLINE_TIME_OUT) + jiffies;
	while (time_before(jiffies, end)) {
		comm_flags = swab32(readl((__iomem char *)priv->mfunc.comm +
					  MLX4_COMM_CHAN_FLAGS));
		offline_bit = (comm_flags &
			       (u32)(1 << COMM_CHAN_OFFLINE_OFFSET));
		if (!offline_bit)
			return 0;

		/* If device removal has been requested,
		 * do not continue retrying.
		 */
		if (dev->persist->interface_state &
		    MLX4_INTERFACE_STATE_NOWAIT)
			break;

		/* There are cases as part of AER/Reset flow that PF needs
		 * around 100 msec to load. We therefore sleep for 100 msec
		 * to allow other tasks to make use of that CPU during this
		 * time interval.
		 */
		msleep(100);
	}
	mlx4_err(dev, "Communication channel is offline.\n");
	return -EIO;
}

static void mlx4_reset_vf_support(struct mlx4_dev *dev)
{
#define COMM_CHAN_RST_OFFSET 0x1e

	struct mlx4_priv *priv = mlx4_priv(dev);
	u32 comm_rst;
	u32 comm_caps;

	comm_caps = swab32(readl((__iomem char *)priv->mfunc.comm +
				 MLX4_COMM_CHAN_CAPS));
	comm_rst = (comm_caps & (u32)(1 << COMM_CHAN_RST_OFFSET));

	if (comm_rst)
		dev->caps.vf_caps |= MLX4_VF_CAP_FLAG_RESET;
}

static int mlx4_init_slave(struct mlx4_dev *dev)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	u64 dma = (u64) priv->mfunc.vhcr_dma;
	int ret_from_reset = 0;
	u32 slave_read;
	u32 cmd_channel_ver;

	if (atomic_read(&pf_loading)) {
		mlx4_warn(dev, "PF is not ready - Deferring probe\n");
		return -EPROBE_DEFER;
	}

	mutex_lock(&priv->cmd.slave_cmd_mutex);
	priv->cmd.max_cmds = 1;
	if (mlx4_comm_check_offline(dev)) {
		mlx4_err(dev, "PF is not responsive, skipping initialization\n");
		goto err_offline;
	}

	mlx4_reset_vf_support(dev);
	mlx4_warn(dev, "Sending reset\n");
	ret_from_reset = mlx4_comm_cmd(dev, MLX4_COMM_CMD_RESET, 0,
				       MLX4_COMM_CMD_NA_OP, MLX4_COMM_TIME);
	/* if we are in the middle of flr the slave will try
	 * NUM_OF_RESET_RETRIES times before leaving.*/
	if (ret_from_reset) {
		if (MLX4_DELAY_RESET_SLAVE == ret_from_reset) {
			mlx4_warn(dev, "slave is currently in the middle of FLR - Deferring probe\n");
			mutex_unlock(&priv->cmd.slave_cmd_mutex);
			return -EPROBE_DEFER;
		} else
			goto err;
	}

	/* check the driver version - the slave I/F revision
	 * must match the master's */
	slave_read = swab32(readl(&priv->mfunc.comm->slave_read));
	cmd_channel_ver = mlx4_comm_get_version();

	if (MLX4_COMM_GET_IF_REV(cmd_channel_ver) !=
		MLX4_COMM_GET_IF_REV(slave_read)) {
		mlx4_err(dev, "slave driver version is not supported by the master\n");
		goto err;
	}

	mlx4_warn(dev, "Sending vhcr0\n");
	if (mlx4_comm_cmd(dev, MLX4_COMM_CMD_VHCR0, dma >> 48,
			     MLX4_COMM_CMD_NA_OP, MLX4_COMM_TIME))
		goto err;
	if (mlx4_comm_cmd(dev, MLX4_COMM_CMD_VHCR1, dma >> 32,
			     MLX4_COMM_CMD_NA_OP, MLX4_COMM_TIME))
		goto err;
	if (mlx4_comm_cmd(dev, MLX4_COMM_CMD_VHCR2, dma >> 16,
			     MLX4_COMM_CMD_NA_OP, MLX4_COMM_TIME))
		goto err;
	if (mlx4_comm_cmd(dev, MLX4_COMM_CMD_VHCR_EN, dma,
			  MLX4_COMM_CMD_NA_OP, MLX4_COMM_TIME))
		goto err;

	mutex_unlock(&priv->cmd.slave_cmd_mutex);
	return 0;

err:
	mlx4_comm_cmd(dev, MLX4_COMM_CMD_RESET, 0, MLX4_COMM_CMD_NA_OP, 0);
err_offline:
	mutex_unlock(&priv->cmd.slave_cmd_mutex);
	return -EIO;
}

static void mlx4_parav_master_pf_caps(struct mlx4_dev *dev)
{
	int i;

	for (i = 1; i <= dev->caps.num_ports; i++) {
		if (dev->caps.port_type[i] == MLX4_PORT_TYPE_ETH)
			dev->caps.gid_table_len[i] =
				mlx4_get_slave_num_gids(dev, 0, i);
		else
			dev->caps.gid_table_len[i] = 1;
		dev->caps.pkey_table_len[i] =
			dev->phys_caps.pkey_phys_table_len[i] - 1;
	}
}

static int choose_log_fs_mgm_entry_size(int qp_per_entry)
{
	int i = MLX4_MIN_MGM_LOG_ENTRY_SIZE;

	for (i = MLX4_MIN_MGM_LOG_ENTRY_SIZE; i <= MLX4_MAX_MGM_LOG_ENTRY_SIZE;
	      i++) {
		if (qp_per_entry <= 4 * ((1 << i) / 16 - 2))
			break;
	}

	return (i <= MLX4_MAX_MGM_LOG_ENTRY_SIZE) ? i : -1;
}

static const char *dmfs_high_rate_steering_mode_str(int dmfs_high_steer_mode)
{
	switch (dmfs_high_steer_mode) {
	case MLX4_STEERING_DMFS_A0_DEFAULT:
		return "default performance";

	case MLX4_STEERING_DMFS_A0_DYNAMIC:
		return "dynamic hybrid mode";

	case MLX4_STEERING_DMFS_A0_STATIC:
		return "performance optimized for limited rule configuration (static)";

	case MLX4_STEERING_DMFS_A0_DISABLE:
		return "disabled performance optimized steering";

	case MLX4_STEERING_DMFS_A0_NOT_SUPPORTED:
		return "performance optimized steering not supported";

	default:
		return "Unrecognized mode";
	}
}

#define MLX4_DMFS_A0_STEERING			(1UL << 2)

static void choose_steering_mode(struct mlx4_dev *dev,
				 struct mlx4_dev_cap *dev_cap)
{
	if (mlx4_log_num_mgm_entry_size <= 0) {
		if ((-mlx4_log_num_mgm_entry_size) & MLX4_DMFS_A0_STEERING) {
			if (dev->caps.dmfs_high_steer_mode ==
			    MLX4_STEERING_DMFS_A0_NOT_SUPPORTED)
				mlx4_err(dev, "DMFS high rate mode not supported\n");
			else
				dev->caps.dmfs_high_steer_mode =
					MLX4_STEERING_DMFS_A0_STATIC;
		}
	}

	if (mlx4_log_num_mgm_entry_size <= 0 &&
	    dev_cap->flags2 & MLX4_DEV_CAP_FLAG2_FS_EN &&
	    (!mlx4_is_mfunc(dev) ||
	     (dev_cap->fs_max_num_qp_per_entry >=
	     (dev->persist->num_vfs + 1))) &&
	    choose_log_fs_mgm_entry_size(dev_cap->fs_max_num_qp_per_entry) >=
		MLX4_MIN_MGM_LOG_ENTRY_SIZE) {
		dev->oper_log_mgm_entry_size =
			choose_log_fs_mgm_entry_size(dev_cap->fs_max_num_qp_per_entry);
		dev->caps.steering_mode = MLX4_STEERING_MODE_DEVICE_MANAGED;
		dev->caps.num_qp_per_mgm = dev_cap->fs_max_num_qp_per_entry;
		dev->caps.fs_log_max_ucast_qp_range_size =
			dev_cap->fs_log_max_ucast_qp_range_size;
	} else {
		if (dev->caps.dmfs_high_steer_mode !=
		    MLX4_STEERING_DMFS_A0_NOT_SUPPORTED)
			dev->caps.dmfs_high_steer_mode = MLX4_STEERING_DMFS_A0_DISABLE;
		if (dev->caps.flags & MLX4_DEV_CAP_FLAG_VEP_UC_STEER &&
		    dev->caps.flags & MLX4_DEV_CAP_FLAG_VEP_MC_STEER)
			dev->caps.steering_mode = MLX4_STEERING_MODE_B0;
		else {
			dev->caps.steering_mode = MLX4_STEERING_MODE_A0;

			if (dev->caps.flags & MLX4_DEV_CAP_FLAG_VEP_UC_STEER ||
			    dev->caps.flags & MLX4_DEV_CAP_FLAG_VEP_MC_STEER)
				mlx4_warn(dev, "Must have both UC_STEER and MC_STEER flags set to use B0 steering - falling back to A0 steering mode\n");
		}
		dev->oper_log_mgm_entry_size =
			mlx4_log_num_mgm_entry_size > 0 ?
			mlx4_log_num_mgm_entry_size :
			MLX4_DEFAULT_MGM_LOG_ENTRY_SIZE;
		dev->caps.num_qp_per_mgm = mlx4_get_qp_per_mgm(dev);
	}
	mlx4_dbg(dev, "Steering mode is: %s, oper_log_mgm_entry_size = %d, modparam log_num_mgm_entry_size = %d\n",
		 mlx4_steering_mode_str(dev->caps.steering_mode),
		 dev->oper_log_mgm_entry_size,
		 mlx4_log_num_mgm_entry_size);
}

static void choose_tunnel_offload_mode(struct mlx4_dev *dev,
				       struct mlx4_dev_cap *dev_cap)
{
	if (dev->caps.steering_mode == MLX4_STEERING_MODE_DEVICE_MANAGED &&
	    dev_cap->flags2 & MLX4_DEV_CAP_FLAG2_VXLAN_OFFLOADS)
		dev->caps.tunnel_offload_mode = MLX4_TUNNEL_OFFLOAD_MODE_VXLAN;
	else
		dev->caps.tunnel_offload_mode = MLX4_TUNNEL_OFFLOAD_MODE_NONE;

	mlx4_dbg(dev, "Tunneling offload mode is: %s\n",  (dev->caps.tunnel_offload_mode
		 == MLX4_TUNNEL_OFFLOAD_MODE_VXLAN) ? "vxlan" : "none");
}

static int mlx4_validate_optimized_steering(struct mlx4_dev *dev)
{
	int i;
	struct mlx4_port_cap port_cap;

	if (dev->caps.dmfs_high_steer_mode == MLX4_STEERING_DMFS_A0_NOT_SUPPORTED)
		return -EINVAL;

	for (i = 1; i <= dev->caps.num_ports; i++) {
		if (mlx4_dev_port(dev, i, &port_cap)) {
			mlx4_err(dev,
				 "QUERY_DEV_CAP command failed, can't verify DMFS high rate steering.\n");
		} else if ((dev->caps.dmfs_high_steer_mode !=
			    MLX4_STEERING_DMFS_A0_DEFAULT) &&
			   (port_cap.dmfs_optimized_state ==
			    !!(dev->caps.dmfs_high_steer_mode ==
			    MLX4_STEERING_DMFS_A0_DISABLE))) {
			mlx4_err(dev,
				 "DMFS high rate steer mode differ, driver requested %s but %s in FW.\n",
				 dmfs_high_rate_steering_mode_str(
					dev->caps.dmfs_high_steer_mode),
				 (port_cap.dmfs_optimized_state ?
					"enabled" : "disabled"));
		}
	}

	return 0;
}

static int mlx4_init_fw(struct mlx4_dev *dev)
{
	struct mlx4_mod_stat_cfg   mlx4_cfg;
	int err = 0;

	if (!mlx4_is_slave(dev)) {
		err = mlx4_QUERY_FW(dev);
		if (err) {
			if (err == -EACCES)
				mlx4_info(dev, "non-primary physical function, skipping\n");
			else
				mlx4_err(dev, "QUERY_FW command failed, aborting\n");
			return err;
		}

		err = mlx4_load_fw(dev);
		if (err) {
			mlx4_err(dev, "Failed to start FW, aborting\n");
			return err;
		}

		mlx4_cfg.log_pg_sz_m = 1;
		mlx4_cfg.log_pg_sz = 0;
		err = mlx4_MOD_STAT_CFG(dev, &mlx4_cfg);
		if (err)
			mlx4_warn(dev, "Failed to override log_pg_sz parameter\n");
	}

	return err;
}

static int mlx4_init_hca(struct mlx4_dev *dev)
{
	struct mlx4_priv	  *priv = mlx4_priv(dev);
	struct mlx4_init_hca_param *init_hca = NULL;
	struct mlx4_dev_cap	  *dev_cap = NULL;
	struct mlx4_adapter	   adapter;
	struct mlx4_profile	   profile;
	u64 icm_size;
	struct mlx4_config_dev_params params;
	int err;

	if (!mlx4_is_slave(dev)) {
		dev_cap = kzalloc(sizeof(*dev_cap), GFP_KERNEL);
		init_hca = kzalloc(sizeof(*init_hca), GFP_KERNEL);

		if (!dev_cap || !init_hca) {
			err = -ENOMEM;
			goto out_free;
		}

		err = mlx4_dev_cap(dev, dev_cap);
		if (err) {
			mlx4_err(dev, "QUERY_DEV_CAP command failed, aborting\n");
			goto out_free;
		}

		choose_steering_mode(dev, dev_cap);
		choose_tunnel_offload_mode(dev, dev_cap);

		if (dev->caps.dmfs_high_steer_mode == MLX4_STEERING_DMFS_A0_STATIC &&
		    mlx4_is_master(dev))
			dev->caps.function_caps |= MLX4_FUNC_CAP_DMFS_A0_STATIC;

		err = mlx4_get_phys_port_id(dev);
		if (err)
			mlx4_err(dev, "Fail to get physical port id\n");

		if (mlx4_is_master(dev))
			mlx4_parav_master_pf_caps(dev);

		if (mlx4_low_memory_profile()) {
			mlx4_info(dev, "Running from within kdump kernel. Using low memory profile\n");
			profile = low_mem_profile;
		} else {
			profile = default_profile;
		}
		if (dev->caps.steering_mode ==
		    MLX4_STEERING_MODE_DEVICE_MANAGED)
			profile.num_mcg = MLX4_FS_NUM_MCG;

		icm_size = mlx4_make_profile(dev, &profile, dev_cap,
					     init_hca);
		if ((long long) icm_size < 0) {
			err = icm_size;
			goto out_free;
		}

		if (enable_4k_uar || !dev->persist->num_vfs) {
			init_hca->log_uar_sz = ilog2(dev->caps.num_uars) +
						    PAGE_SHIFT - DEFAULT_UAR_PAGE_SHIFT;
			init_hca->uar_page_sz = DEFAULT_UAR_PAGE_SHIFT - 12;
		} else {
			init_hca->log_uar_sz = ilog2(dev->caps.num_uars);
			init_hca->uar_page_sz = PAGE_SHIFT - 12;
		}

		init_hca->mw_enabled = 0;
		if (dev->caps.flags & MLX4_DEV_CAP_FLAG_MEM_WINDOW ||
		    dev->caps.bmme_flags & MLX4_BMME_FLAG_TYPE_2_WIN)
			init_hca->mw_enabled = INIT_HCA_TPT_MW_ENABLE;

		err = mlx4_init_icm(dev, dev_cap, init_hca, icm_size);
		if (err)
			goto out_free;

		err = mlx4_INIT_HCA(dev, init_hca);
		if (err) {
			mlx4_err(dev, "INIT_HCA command failed, aborting\n");
			goto err_free_icm;
		}

		if (dev_cap->flags2 & MLX4_DEV_CAP_FLAG2_SYS_EQS) {
			err = mlx4_query_func(dev, dev_cap);
			if (err < 0) {
				mlx4_err(dev, "QUERY_FUNC command failed, aborting.\n");
				goto err_close;
			} else if (err & MLX4_QUERY_FUNC_NUM_SYS_EQS) {
				dev->caps.num_eqs = dev_cap->max_eqs;
				dev->caps.reserved_eqs = dev_cap->reserved_eqs;
				dev->caps.reserved_uars = dev_cap->reserved_uars;
			}
		}

		/*
		 * If TS is supported by FW
		 * read HCA frequency by QUERY_HCA command
		 */
		if (dev->caps.flags2 & MLX4_DEV_CAP_FLAG2_TS) {
			err = mlx4_QUERY_HCA(dev, init_hca);
			if (err) {
				mlx4_err(dev, "QUERY_HCA command failed, disable timestamp\n");
				dev->caps.flags2 &= ~MLX4_DEV_CAP_FLAG2_TS;
			} else {
				dev->caps.hca_core_clock =
					init_hca->hca_core_clock;
			}

			/* In case we got HCA frequency 0 - disable timestamping
			 * to avoid dividing by zero
			 */
			if (!dev->caps.hca_core_clock) {
				dev->caps.flags2 &= ~MLX4_DEV_CAP_FLAG2_TS;
				mlx4_err(dev,
					 "HCA frequency is 0 - timestamping is not supported\n");
			} else if (map_internal_clock(dev)) {
				/*
				 * Map internal clock,
				 * in case of failure disable timestamping
				 */
				dev->caps.flags2 &= ~MLX4_DEV_CAP_FLAG2_TS;
				mlx4_err(dev, "Failed to map internal clock. Timestamping is not supported\n");
			}
		}

		if (dev->caps.dmfs_high_steer_mode !=
		    MLX4_STEERING_DMFS_A0_NOT_SUPPORTED) {
			if (mlx4_validate_optimized_steering(dev))
				mlx4_warn(dev, "Optimized steering validation failed\n");

			if (dev->caps.dmfs_high_steer_mode ==
			    MLX4_STEERING_DMFS_A0_DISABLE) {
				dev->caps.dmfs_high_rate_qpn_base =
					dev->caps.reserved_qps_cnt[MLX4_QP_REGION_FW];
				dev->caps.dmfs_high_rate_qpn_range =
					MLX4_A0_STEERING_TABLE_SIZE;
			}

			mlx4_info(dev, "DMFS high rate steer mode is: %s\n",
				  dmfs_high_rate_steering_mode_str(
					dev->caps.dmfs_high_steer_mode));
		}
	} else {
		err = mlx4_init_slave(dev);
		if (err) {
			if (err != -EPROBE_DEFER)
				mlx4_err(dev, "Failed to initialize slave\n");
			return err;
		}

		err = mlx4_slave_cap(dev);
		if (err) {
			mlx4_err(dev, "Failed to obtain slave caps\n");
			goto err_close;
		}
	}

	if (map_bf_area(dev))
		mlx4_dbg(dev, "Failed to map blue flame area\n");

	/*Only the master set the ports, all the rest got it from it.*/
	if (!mlx4_is_slave(dev))
		mlx4_set_port_mask(dev);

	err = mlx4_QUERY_ADAPTER(dev, &adapter);
	if (err) {
		mlx4_err(dev, "QUERY_ADAPTER command failed, aborting\n");
		goto unmap_bf;
	}

	/* Query CONFIG_DEV parameters */
	err = mlx4_config_dev_retrieval(dev, &params);
	if (err && err != -EOPNOTSUPP) {
		mlx4_err(dev, "Failed to query CONFIG_DEV parameters\n");
	} else if (!err) {
		dev->caps.rx_checksum_flags_port[1] = params.rx_csum_flags_port_1;
		dev->caps.rx_checksum_flags_port[2] = params.rx_csum_flags_port_2;
	}
	priv->eq_table.inta_pin = adapter.inta_pin;
	memcpy(dev->board_id, adapter.board_id, sizeof(dev->board_id));

	err = 0;
	goto out_free;

unmap_bf:
	unmap_internal_clock(dev);
	unmap_bf_area(dev);

	if (mlx4_is_slave(dev))
		mlx4_slave_destroy_special_qp_cap(dev);

err_close:
	if (mlx4_is_slave(dev))
		mlx4_slave_exit(dev);
	else
		mlx4_CLOSE_HCA(dev, 0);

err_free_icm:
	if (!mlx4_is_slave(dev))
		mlx4_free_icms(dev);

out_free:
	kfree(dev_cap);
	kfree(init_hca);

	return err;
}

static int mlx4_init_counters_table(struct mlx4_dev *dev)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	int nent_pow2;

	if (!(dev->caps.flags & MLX4_DEV_CAP_FLAG_COUNTERS))
		return -ENOENT;

	if (!dev->caps.max_counters)
		return -ENOSPC;

	nent_pow2 = roundup_pow_of_two(dev->caps.max_counters);
	/* reserve last counter index for sink counter */
	return mlx4_bitmap_init(&priv->counters_bitmap, nent_pow2,
				nent_pow2 - 1, 0,
				nent_pow2 - dev->caps.max_counters + 1);
}

static void mlx4_cleanup_counters_table(struct mlx4_dev *dev)
{
	if (!(dev->caps.flags & MLX4_DEV_CAP_FLAG_COUNTERS))
		return;

	if (!dev->caps.max_counters)
		return;

	mlx4_bitmap_cleanup(&mlx4_priv(dev)->counters_bitmap);
}

static void mlx4_cleanup_default_counters(struct mlx4_dev *dev)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	int port;

	for (port = 0; port < dev->caps.num_ports; port++)
		if (priv->def_counter[port] != -1)
			mlx4_counter_free(dev,  priv->def_counter[port]);
}

static int mlx4_allocate_default_counters(struct mlx4_dev *dev)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	int port, err = 0;
	u32 idx;

	for (port = 0; port < dev->caps.num_ports; port++)
		priv->def_counter[port] = -1;

	for (port = 0; port < dev->caps.num_ports; port++) {
		err = mlx4_counter_alloc(dev, &idx, MLX4_RES_USAGE_DRIVER);

		if (!err || err == -ENOSPC) {
			priv->def_counter[port] = idx;
			err = 0;
		} else if (err == -ENOENT) {
			err = 0;
			continue;
		} else if (mlx4_is_slave(dev) && err == -EINVAL) {
			priv->def_counter[port] = MLX4_SINK_COUNTER_INDEX(dev);
			mlx4_warn(dev, "can't allocate counter from old PF driver, using index %d\n",
				  MLX4_SINK_COUNTER_INDEX(dev));
			err = 0;
		} else {
			mlx4_err(dev, "%s: failed to allocate default counter port %d err %d\n",
				 __func__, port + 1, err);
			mlx4_cleanup_default_counters(dev);
			return err;
		}

		mlx4_dbg(dev, "%s: default counter index %d for port %d\n",
			 __func__, priv->def_counter[port], port + 1);
	}

	return err;
}

int __mlx4_counter_alloc(struct mlx4_dev *dev, u32 *idx)
{
	struct mlx4_priv *priv = mlx4_priv(dev);

	if (!(dev->caps.flags & MLX4_DEV_CAP_FLAG_COUNTERS))
		return -ENOENT;

	*idx = mlx4_bitmap_alloc(&priv->counters_bitmap);
	if (*idx == -1) {
		*idx = MLX4_SINK_COUNTER_INDEX(dev);
		return -ENOSPC;
	}

	return 0;
}

int mlx4_counter_alloc(struct mlx4_dev *dev, u32 *idx, u8 usage)
{
	u32 in_modifier = RES_COUNTER | (((u32)usage & 3) << 30);
	u64 out_param;
	int err;

	if (mlx4_is_mfunc(dev)) {
		err = mlx4_cmd_imm(dev, 0, &out_param, in_modifier,
				   RES_OP_RESERVE, MLX4_CMD_ALLOC_RES,
				   MLX4_CMD_TIME_CLASS_A, MLX4_CMD_WRAPPED);
		if (!err)
			*idx = get_param_l(&out_param);
		if (WARN_ON(err == -ENOSPC))
			err = -EINVAL;
		return err;
	}
	return __mlx4_counter_alloc(dev, idx);
}
EXPORT_SYMBOL_GPL(mlx4_counter_alloc);

static int __mlx4_clear_if_stat(struct mlx4_dev *dev,
				u8 counter_index)
{
	struct mlx4_cmd_mailbox *if_stat_mailbox;
	int err;
	u32 if_stat_in_mod = (counter_index & 0xff) | MLX4_QUERY_IF_STAT_RESET;

	if_stat_mailbox = mlx4_alloc_cmd_mailbox(dev);
	if (IS_ERR(if_stat_mailbox))
		return PTR_ERR(if_stat_mailbox);

	err = mlx4_cmd_box(dev, 0, if_stat_mailbox->dma, if_stat_in_mod, 0,
			   MLX4_CMD_QUERY_IF_STAT, MLX4_CMD_TIME_CLASS_C,
			   MLX4_CMD_NATIVE);

	mlx4_free_cmd_mailbox(dev, if_stat_mailbox);
	return err;
}

void __mlx4_counter_free(struct mlx4_dev *dev, u32 idx)
{
	if (!(dev->caps.flags & MLX4_DEV_CAP_FLAG_COUNTERS))
		return;

	if (idx == MLX4_SINK_COUNTER_INDEX(dev))
		return;

	__mlx4_clear_if_stat(dev, idx);

	mlx4_bitmap_free(&mlx4_priv(dev)->counters_bitmap, idx, MLX4_USE_RR);
	return;
}

void mlx4_counter_free(struct mlx4_dev *dev, u32 idx)
{
	u64 in_param = 0;

	if (mlx4_is_mfunc(dev)) {
		set_param_l(&in_param, idx);
		mlx4_cmd(dev, in_param, RES_COUNTER, RES_OP_RESERVE,
			 MLX4_CMD_FREE_RES, MLX4_CMD_TIME_CLASS_A,
			 MLX4_CMD_WRAPPED);
		return;
	}
	__mlx4_counter_free(dev, idx);
}
EXPORT_SYMBOL_GPL(mlx4_counter_free);

int mlx4_get_default_counter_index(struct mlx4_dev *dev, int port)
{
	struct mlx4_priv *priv = mlx4_priv(dev);

	return priv->def_counter[port - 1];
}
EXPORT_SYMBOL_GPL(mlx4_get_default_counter_index);

void mlx4_set_admin_guid(struct mlx4_dev *dev, __be64 guid, int entry, int port)
{
	struct mlx4_priv *priv = mlx4_priv(dev);

	priv->mfunc.master.vf_admin[entry].vport[port].guid = guid;
}
EXPORT_SYMBOL_GPL(mlx4_set_admin_guid);

__be64 mlx4_get_admin_guid(struct mlx4_dev *dev, int entry, int port)
{
	struct mlx4_priv *priv = mlx4_priv(dev);

	return priv->mfunc.master.vf_admin[entry].vport[port].guid;
}
EXPORT_SYMBOL_GPL(mlx4_get_admin_guid);

void mlx4_set_random_admin_guid(struct mlx4_dev *dev, int entry, int port)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	__be64 guid;

	/* hw GUID */
	if (entry == 0)
		return;

	get_random_bytes((char *)&guid, sizeof(guid));
	guid &= ~(cpu_to_be64(1ULL << 56));
	guid |= cpu_to_be64(1ULL << 57);
	priv->mfunc.master.vf_admin[entry].vport[port].guid = guid;
}

static int mlx4_setup_hca(struct mlx4_dev *dev)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	int err;
	int port;
	__be32 ib_port_default_caps;

	err = mlx4_init_uar_table(dev);
	if (err) {
		mlx4_err(dev, "Failed to initialize user access region table, aborting\n");
		return err;
	}

	err = mlx4_uar_alloc(dev, &priv->driver_uar);
	if (err) {
		mlx4_err(dev, "Failed to allocate driver access region, aborting\n");
		goto err_uar_table_free;
	}

	priv->kar = ioremap((phys_addr_t) priv->driver_uar.pfn << PAGE_SHIFT, PAGE_SIZE);
	if (!priv->kar) {
		mlx4_err(dev, "Couldn't map kernel access region, aborting\n");
		err = -ENOMEM;
		goto err_uar_free;
	}

	err = mlx4_init_pd_table(dev);
	if (err) {
		mlx4_err(dev, "Failed to initialize protection domain table, aborting\n");
		goto err_kar_unmap;
	}

	err = mlx4_init_xrcd_table(dev);
	if (err) {
		mlx4_err(dev, "Failed to initialize reliable connection domain table, aborting\n");
		goto err_pd_table_free;
	}

	err = mlx4_init_mr_table(dev);
	if (err) {
		mlx4_err(dev, "Failed to initialize memory region table, aborting\n");
		goto err_xrcd_table_free;
	}

	if (!mlx4_is_slave(dev)) {
		err = mlx4_init_mcg_table(dev);
		if (err) {
			mlx4_err(dev, "Failed to initialize multicast group table, aborting\n");
			goto err_mr_table_free;
		}
		err = mlx4_config_mad_demux(dev);
		if (err) {
			mlx4_err(dev, "Failed in config_mad_demux, aborting\n");
			goto err_mcg_table_free;
		}
	}

	err = mlx4_init_eq_table(dev);
	if (err) {
		mlx4_err(dev, "Failed to initialize event queue table, aborting\n");
		goto err_mcg_table_free;
	}

	err = mlx4_cmd_use_events(dev);
	if (err) {
		mlx4_err(dev, "Failed to switch to event-driven firmware commands, aborting\n");
		goto err_eq_table_free;
	}

	err = mlx4_NOP(dev);
	if (err) {
		if (dev->flags & MLX4_FLAG_MSI_X) {
			mlx4_warn(dev, "NOP command failed to generate MSI-X interrupt IRQ %d)\n",
				  priv->eq_table.eq[MLX4_EQ_ASYNC].irq);
			mlx4_warn(dev, "Trying again without MSI-X\n");
		} else {
			mlx4_err(dev, "NOP command failed to generate interrupt (IRQ %d), aborting\n",
				 priv->eq_table.eq[MLX4_EQ_ASYNC].irq);
			mlx4_err(dev, "BIOS or ACPI interrupt routing problem?\n");
		}

		goto err_cmd_poll;
	}

	mlx4_dbg(dev, "NOP command IRQ test passed\n");

	err = mlx4_init_cq_table(dev);
	if (err) {
		mlx4_err(dev, "Failed to initialize completion queue table, aborting\n");
		goto err_cmd_poll;
	}

	err = mlx4_init_srq_table(dev);
	if (err) {
		mlx4_err(dev, "Failed to initialize shared receive queue table, aborting\n");
		goto err_cq_table_free;
	}

	err = mlx4_init_qp_table(dev);
	if (err) {
		mlx4_err(dev, "Failed to initialize queue pair table, aborting\n");
		goto err_srq_table_free;
	}

	if (!mlx4_is_slave(dev)) {
		err = mlx4_init_counters_table(dev);
		if (err && err != -ENOENT) {
			mlx4_err(dev, "Failed to initialize counters table, aborting\n");
			goto err_qp_table_free;
		}
	}

	err = mlx4_allocate_default_counters(dev);
	if (err) {
		mlx4_err(dev, "Failed to allocate default counters, aborting\n");
		goto err_counters_table_free;
	}

	if (!mlx4_is_slave(dev)) {
		for (port = 1; port <= dev->caps.num_ports; port++) {
			ib_port_default_caps = 0;
			err = mlx4_get_port_ib_caps(dev, port,
						    &ib_port_default_caps);
			if (err)
				mlx4_warn(dev, "failed to get port %d default ib capabilities (%d). Continuing with caps = 0\n",
					  port, err);
			dev->caps.ib_port_def_cap[port] = ib_port_default_caps;

			/* initialize per-slave default ib port capabilities */
			if (mlx4_is_master(dev)) {
				int i;
				for (i = 0; i < dev->num_slaves; i++) {
					if (i == mlx4_master_func_num(dev))
						continue;
					priv->mfunc.master.slave_state[i].ib_cap_mask[port] =
						ib_port_default_caps;
				}
			}

			if (mlx4_is_mfunc(dev))
				dev->caps.port_ib_mtu[port] = IB_MTU_2048;
			else
				dev->caps.port_ib_mtu[port] = IB_MTU_4096;

			err = mlx4_SET_PORT(dev, port, mlx4_is_master(dev) ?
					    dev->caps.pkey_table_len[port] : -1);
			if (err) {
				mlx4_err(dev, "Failed to set port %d, aborting\n",
					 port);
				goto err_default_countes_free;
			}
		}
	}

	return 0;

err_default_countes_free:
	mlx4_cleanup_default_counters(dev);

err_counters_table_free:
	if (!mlx4_is_slave(dev))
		mlx4_cleanup_counters_table(dev);

err_qp_table_free:
	mlx4_cleanup_qp_table(dev);

err_srq_table_free:
	mlx4_cleanup_srq_table(dev);

err_cq_table_free:
	mlx4_cleanup_cq_table(dev);

err_cmd_poll:
	mlx4_cmd_use_polling(dev);

err_eq_table_free:
	mlx4_cleanup_eq_table(dev);

err_mcg_table_free:
	if (!mlx4_is_slave(dev))
		mlx4_cleanup_mcg_table(dev);

err_mr_table_free:
	mlx4_cleanup_mr_table(dev);

err_xrcd_table_free:
	mlx4_cleanup_xrcd_table(dev);

err_pd_table_free:
	mlx4_cleanup_pd_table(dev);

err_kar_unmap:
	iounmap(priv->kar);

err_uar_free:
	mlx4_uar_free(dev, &priv->driver_uar);

err_uar_table_free:
	mlx4_cleanup_uar_table(dev);
	return err;
}

static int mlx4_init_affinity_hint(struct mlx4_dev *dev, int port, int eqn)
{
	int requested_cpu = 0;
	struct mlx4_priv *priv = mlx4_priv(dev);
	struct mlx4_eq *eq;
	int off = 0;
	int i;

	if (eqn > dev->caps.num_comp_vectors)
		return -EINVAL;

	for (i = 1; i < port; i++)
		off += mlx4_get_eqs_per_port(dev, i);

	requested_cpu = eqn - off - !!(eqn > MLX4_EQ_ASYNC);

	/* Meaning EQs are shared, and this call comes from the second port */
	if (requested_cpu < 0)
		return 0;

	eq = &priv->eq_table.eq[eqn];

	if (!zalloc_cpumask_var(&eq->affinity_mask, GFP_KERNEL))
		return -ENOMEM;

	cpumask_set_cpu(requested_cpu, eq->affinity_mask);

	return 0;
}

static void mlx4_enable_msi_x(struct mlx4_dev *dev)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	struct msix_entry *entries;
	int i;
	int port = 0;

	if (msi_x) {
		int nreq = min3(dev->caps.num_ports *
				(int)num_online_cpus() + 1,
				dev->caps.num_eqs - dev->caps.reserved_eqs,
				MAX_MSIX);

		if (msi_x > 1)
			nreq = min_t(int, nreq, msi_x);

		entries = kcalloc(nreq, sizeof(*entries), GFP_KERNEL);
		if (!entries)
			goto no_msi;

		for (i = 0; i < nreq; ++i)
			entries[i].entry = i;

		nreq = pci_enable_msix_range(dev->persist->pdev, entries, 2,
					     nreq);

		if (nreq < 0 || nreq < MLX4_EQ_ASYNC) {
			kfree(entries);
			goto no_msi;
		}
		/* 1 is reserved for events (asyncrounous EQ) */
		dev->caps.num_comp_vectors = nreq - 1;

		priv->eq_table.eq[MLX4_EQ_ASYNC].irq = entries[0].vector;
		bitmap_zero(priv->eq_table.eq[MLX4_EQ_ASYNC].actv_ports.ports,
			    dev->caps.num_ports);

		for (i = 0; i < dev->caps.num_comp_vectors + 1; i++) {
			if (i == MLX4_EQ_ASYNC)
				continue;

			priv->eq_table.eq[i].irq =
				entries[i + 1 - !!(i > MLX4_EQ_ASYNC)].vector;

			if (MLX4_IS_LEGACY_EQ_MODE(dev->caps)) {
				bitmap_fill(priv->eq_table.eq[i].actv_ports.ports,
					    dev->caps.num_ports);
				/* We don't set affinity hint when there
				 * aren't enough EQs
				 */
			} else {
				set_bit(port,
					priv->eq_table.eq[i].actv_ports.ports);
				if (mlx4_init_affinity_hint(dev, port + 1, i))
					mlx4_warn(dev, "Couldn't init hint cpumask for EQ %d\n",
						  i);
			}
			/* We divide the Eqs evenly between the two ports.
			 * (dev->caps.num_comp_vectors / dev->caps.num_ports)
			 * refers to the number of Eqs per port
			 * (i.e eqs_per_port). Theoretically, we would like to
			 * write something like (i + 1) % eqs_per_port == 0.
			 * However, since there's an asynchronous Eq, we have
			 * to skip over it by comparing this condition to
			 * !!((i + 1) > MLX4_EQ_ASYNC).
			 */
			if ((dev->caps.num_comp_vectors > dev->caps.num_ports) &&
			    ((i + 1) %
			     (dev->caps.num_comp_vectors / dev->caps.num_ports)) ==
			    !!((i + 1) > MLX4_EQ_ASYNC))
				/* If dev->caps.num_comp_vectors < dev->caps.num_ports,
				 * everything is shared anyway.
				 */
				port++;
		}

		dev->flags |= MLX4_FLAG_MSI_X;

		kfree(entries);
		return;
	}

no_msi:
	dev->caps.num_comp_vectors = 1;

	BUG_ON(MLX4_EQ_ASYNC >= 2);
	for (i = 0; i < 2; ++i) {
		priv->eq_table.eq[i].irq = dev->persist->pdev->irq;
		if (i != MLX4_EQ_ASYNC) {
			bitmap_fill(priv->eq_table.eq[i].actv_ports.ports,
				    dev->caps.num_ports);
		}
	}
}

static int mlx4_init_port_info(struct mlx4_dev *dev, int port)
{
	struct devlink *devlink = priv_to_devlink(mlx4_priv(dev));
	struct mlx4_port_info *info = &mlx4_priv(dev)->port[port];
	int err;

	err = devlink_port_register(devlink, &info->devlink_port, port);
	if (err)
		return err;

	/* Ethernet and IB drivers will normally set the port type,
	 * but if they are not built set the type now to prevent
	 * devlink_port_type_warn() from firing.
	 */
	if (!IS_ENABLED(CONFIG_MLX4_EN) &&
	    dev->caps.port_type[port] == MLX4_PORT_TYPE_ETH)
		devlink_port_type_eth_set(&info->devlink_port, NULL);
	else if (!IS_ENABLED(CONFIG_MLX4_INFINIBAND) &&
		 dev->caps.port_type[port] == MLX4_PORT_TYPE_IB)
		devlink_port_type_ib_set(&info->devlink_port, NULL);

	info->dev = dev;
	info->port = port;
	if (!mlx4_is_slave(dev)) {
		mlx4_init_mac_table(dev, &info->mac_table);
		mlx4_init_vlan_table(dev, &info->vlan_table);
		mlx4_init_roce_gid_table(dev, &info->gid_table);
		info->base_qpn = mlx4_get_base_qpn(dev, port);
	}

	sprintf(info->dev_name, "mlx4_port%d", port);
	info->port_attr.attr.name = info->dev_name;
	if (mlx4_is_mfunc(dev)) {
		info->port_attr.attr.mode = 0444;
	} else {
		info->port_attr.attr.mode = 0644;
		info->port_attr.store     = set_port_type;
	}
	info->port_attr.show      = show_port_type;
	sysfs_attr_init(&info->port_attr.attr);

	err = device_create_file(&dev->persist->pdev->dev, &info->port_attr);
	if (err) {
		mlx4_err(dev, "Failed to create file for port %d\n", port);
		devlink_port_unregister(&info->devlink_port);
		info->port = -1;
		return err;
	}

	sprintf(info->dev_mtu_name, "mlx4_port%d_mtu", port);
	info->port_mtu_attr.attr.name = info->dev_mtu_name;
	if (mlx4_is_mfunc(dev)) {
		info->port_mtu_attr.attr.mode = 0444;
	} else {
		info->port_mtu_attr.attr.mode = 0644;
		info->port_mtu_attr.store     = set_port_ib_mtu;
	}
	info->port_mtu_attr.show      = show_port_ib_mtu;
	sysfs_attr_init(&info->port_mtu_attr.attr);

	err = device_create_file(&dev->persist->pdev->dev,
				 &info->port_mtu_attr);
	if (err) {
		mlx4_err(dev, "Failed to create mtu file for port %d\n", port);
		device_remove_file(&info->dev->persist->pdev->dev,
				   &info->port_attr);
		devlink_port_unregister(&info->devlink_port);
		info->port = -1;
		return err;
	}

	return 0;
}

static void mlx4_cleanup_port_info(struct mlx4_port_info *info)
{
	if (info->port < 0)
		return;

	device_remove_file(&info->dev->persist->pdev->dev, &info->port_attr);
	device_remove_file(&info->dev->persist->pdev->dev,
			   &info->port_mtu_attr);
	devlink_port_unregister(&info->devlink_port);

#ifdef CONFIG_RFS_ACCEL
	free_irq_cpu_rmap(info->rmap);
	info->rmap = NULL;
#endif
}

static int mlx4_init_steering(struct mlx4_dev *dev)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	int num_entries = dev->caps.num_ports;
	int i, j;

	priv->steer = kcalloc(num_entries, sizeof(struct mlx4_steer),
			      GFP_KERNEL);
	if (!priv->steer)
		return -ENOMEM;

	for (i = 0; i < num_entries; i++)
		for (j = 0; j < MLX4_NUM_STEERS; j++) {
			INIT_LIST_HEAD(&priv->steer[i].promisc_qps[j]);
			INIT_LIST_HEAD(&priv->steer[i].steer_entries[j]);
		}
	return 0;
}

static void mlx4_clear_steering(struct mlx4_dev *dev)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	struct mlx4_steer_index *entry, *tmp_entry;
	struct mlx4_promisc_qp *pqp, *tmp_pqp;
	int num_entries = dev->caps.num_ports;
	int i, j;

	for (i = 0; i < num_entries; i++) {
		for (j = 0; j < MLX4_NUM_STEERS; j++) {
			list_for_each_entry_safe(pqp, tmp_pqp,
						 &priv->steer[i].promisc_qps[j],
						 list) {
				list_del(&pqp->list);
				kfree(pqp);
			}
			list_for_each_entry_safe(entry, tmp_entry,
						 &priv->steer[i].steer_entries[j],
						 list) {
				list_del(&entry->list);
				list_for_each_entry_safe(pqp, tmp_pqp,
							 &entry->duplicates,
							 list) {
					list_del(&pqp->list);
					kfree(pqp);
				}
				kfree(entry);
			}
		}
	}
	kfree(priv->steer);
}

static int extended_func_num(struct pci_dev *pdev)
{
	return PCI_SLOT(pdev->devfn) * 8 + PCI_FUNC(pdev->devfn);
}

#define MLX4_OWNER_BASE	0x8069c
#define MLX4_OWNER_SIZE	4

static int mlx4_get_ownership(struct mlx4_dev *dev)
{
	void __iomem *owner;
	u32 ret;

	if (pci_channel_offline(dev->persist->pdev))
		return -EIO;

	owner = ioremap(pci_resource_start(dev->persist->pdev, 0) +
			MLX4_OWNER_BASE,
			MLX4_OWNER_SIZE);
	if (!owner) {
		mlx4_err(dev, "Failed to obtain ownership bit\n");
		return -ENOMEM;
	}

	ret = readl(owner);
	iounmap(owner);
	return (int) !!ret;
}

static void mlx4_free_ownership(struct mlx4_dev *dev)
{
	void __iomem *owner;

	if (pci_channel_offline(dev->persist->pdev))
		return;

	owner = ioremap(pci_resource_start(dev->persist->pdev, 0) +
			MLX4_OWNER_BASE,
			MLX4_OWNER_SIZE);
	if (!owner) {
		mlx4_err(dev, "Failed to obtain ownership bit\n");
		return;
	}
	writel(0, owner);
	msleep(1000);
	iounmap(owner);
}

#define SRIOV_VALID_STATE(flags) (!!((flags) & MLX4_FLAG_SRIOV)	==\
				  !!((flags) & MLX4_FLAG_MASTER))

static u64 mlx4_enable_sriov(struct mlx4_dev *dev, struct pci_dev *pdev,
			     u8 total_vfs, int existing_vfs, int reset_flow)
{
	u64 dev_flags = dev->flags;
	int err = 0;
	int fw_enabled_sriov_vfs = min(pci_sriov_get_totalvfs(pdev),
					MLX4_MAX_NUM_VF);

	if (reset_flow) {
		dev->dev_vfs = kcalloc(total_vfs, sizeof(*dev->dev_vfs),
				       GFP_KERNEL);
		if (!dev->dev_vfs)
			goto free_mem;
		return dev_flags;
	}

	atomic_inc(&pf_loading);
	if (dev->flags &  MLX4_FLAG_SRIOV) {
		if (existing_vfs != total_vfs) {
			mlx4_err(dev, "SR-IOV was already enabled, but with num_vfs (%d) different than requested (%d)\n",
				 existing_vfs, total_vfs);
			total_vfs = existing_vfs;
		}
	}

	dev->dev_vfs = kcalloc(total_vfs, sizeof(*dev->dev_vfs), GFP_KERNEL);
	if (NULL == dev->dev_vfs) {
		mlx4_err(dev, "Failed to allocate memory for VFs\n");
		goto disable_sriov;
	}

	if (!(dev->flags &  MLX4_FLAG_SRIOV)) {
		if (total_vfs > fw_enabled_sriov_vfs) {
			mlx4_err(dev, "requested vfs (%d) > available vfs (%d). Continuing without SR_IOV\n",
				 total_vfs, fw_enabled_sriov_vfs);
			err = -ENOMEM;
			goto disable_sriov;
		}
		mlx4_warn(dev, "Enabling SR-IOV with %d VFs\n", total_vfs);
		err = pci_enable_sriov(pdev, total_vfs);
	}
	if (err) {
		mlx4_err(dev, "Failed to enable SR-IOV, continuing without SR-IOV (err = %d)\n",
			 err);
		goto disable_sriov;
	} else {
		mlx4_warn(dev, "Running in master mode\n");
		dev_flags |= MLX4_FLAG_SRIOV |
			MLX4_FLAG_MASTER;
		dev_flags &= ~MLX4_FLAG_SLAVE;
		dev->persist->num_vfs = total_vfs;
	}
	return dev_flags;

disable_sriov:
	atomic_dec(&pf_loading);
free_mem:
	dev->persist->num_vfs = 0;
	kfree(dev->dev_vfs);
	dev->dev_vfs = NULL;
	return dev_flags & ~MLX4_FLAG_MASTER;
}

enum {
	MLX4_DEV_CAP_CHECK_NUM_VFS_ABOVE_64 = -1,
};

static int mlx4_check_dev_cap(struct mlx4_dev *dev, struct mlx4_dev_cap *dev_cap,
			      int *nvfs)
{
	int requested_vfs = nvfs[0] + nvfs[1] + nvfs[2];
	/* Checking for 64 VFs as a limitation of CX2 */
	if (!(dev_cap->flags2 & MLX4_DEV_CAP_FLAG2_80_VFS) &&
	    requested_vfs >= 64) {
		mlx4_err(dev, "Requested %d VFs, but FW does not support more than 64\n",
			 requested_vfs);
		return MLX4_DEV_CAP_CHECK_NUM_VFS_ABOVE_64;
	}
	return 0;
}

static int mlx4_pci_enable_device(struct mlx4_dev *dev)
{
	struct pci_dev *pdev = dev->persist->pdev;
	int err = 0;

	mutex_lock(&dev->persist->pci_status_mutex);
	if (dev->persist->pci_status == MLX4_PCI_STATUS_DISABLED) {
		err = pci_enable_device(pdev);
		if (!err)
			dev->persist->pci_status = MLX4_PCI_STATUS_ENABLED;
	}
	mutex_unlock(&dev->persist->pci_status_mutex);

	return err;
}

static void mlx4_pci_disable_device(struct mlx4_dev *dev)
{
	struct pci_dev *pdev = dev->persist->pdev;

	mutex_lock(&dev->persist->pci_status_mutex);
	if (dev->persist->pci_status == MLX4_PCI_STATUS_ENABLED) {
		pci_disable_device(pdev);
		dev->persist->pci_status = MLX4_PCI_STATUS_DISABLED;
	}
	mutex_unlock(&dev->persist->pci_status_mutex);
}

static int mlx4_load_one(struct pci_dev *pdev, int pci_dev_data,
			 int total_vfs, int *nvfs, struct mlx4_priv *priv,
			 int reset_flow)
{
	struct mlx4_dev *dev;
	unsigned sum = 0;
	int err;
	int port;
	int i;
	struct mlx4_dev_cap *dev_cap = NULL;
	int existing_vfs = 0;

	dev = &priv->dev;

	INIT_LIST_HEAD(&priv->ctx_list);
	spin_lock_init(&priv->ctx_lock);

	mutex_init(&priv->port_mutex);
	mutex_init(&priv->bond_mutex);

	INIT_LIST_HEAD(&priv->pgdir_list);
	mutex_init(&priv->pgdir_mutex);
	spin_lock_init(&priv->cmd.context_lock);

	INIT_LIST_HEAD(&priv->bf_list);
	mutex_init(&priv->bf_mutex);

	dev->rev_id = pdev->revision;
	dev->numa_node = dev_to_node(&pdev->dev);

	/* Detect if this device is a virtual function */
	if (pci_dev_data & MLX4_PCI_DEV_IS_VF) {
		mlx4_warn(dev, "Detected virtual function - running in slave mode\n");
		dev->flags |= MLX4_FLAG_SLAVE;
	} else {
		/* We reset the device and enable SRIOV only for physical
		 * devices.  Try to claim ownership on the device;
		 * if already taken, skip -- do not allow multiple PFs */
		err = mlx4_get_ownership(dev);
		if (err) {
			if (err < 0)
				return err;
			else {
				mlx4_warn(dev, "Multiple PFs not yet supported - Skipping PF\n");
				return -EINVAL;
			}
		}

		atomic_set(&priv->opreq_count, 0);
		INIT_WORK(&priv->opreq_task, mlx4_opreq_action);

		/*
		 * Now reset the HCA before we touch the PCI capabilities or
		 * attempt a firmware command, since a boot ROM may have left
		 * the HCA in an undefined state.
		 */
		err = mlx4_reset(dev);
		if (err) {
			mlx4_err(dev, "Failed to reset HCA, aborting\n");
			goto err_sriov;
		}

		if (total_vfs) {
			dev->flags = MLX4_FLAG_MASTER;
			existing_vfs = pci_num_vf(pdev);
			if (existing_vfs)
				dev->flags |= MLX4_FLAG_SRIOV;
			dev->persist->num_vfs = total_vfs;
		}
	}

	/* on load remove any previous indication of internal error,
	 * device is up.
	 */
	dev->persist->state = MLX4_DEVICE_STATE_UP;

slave_start:
	err = mlx4_cmd_init(dev);
	if (err) {
		mlx4_err(dev, "Failed to init command interface, aborting\n");
		goto err_sriov;
	}

	/* In slave functions, the communication channel must be initialized
	 * before posting commands. Also, init num_slaves before calling
	 * mlx4_init_hca */
	if (mlx4_is_mfunc(dev)) {
		if (mlx4_is_master(dev)) {
			dev->num_slaves = MLX4_MAX_NUM_SLAVES;

		} else {
			dev->num_slaves = 0;
			err = mlx4_multi_func_init(dev);
			if (err) {
				mlx4_err(dev, "Failed to init slave mfunc interface, aborting\n");
				goto err_cmd;
			}
		}
	}

	err = mlx4_init_fw(dev);
	if (err) {
		mlx4_err(dev, "Failed to init fw, aborting.\n");
		goto err_mfunc;
	}

	if (mlx4_is_master(dev)) {
		/* when we hit the goto slave_start below, dev_cap already initialized */
		if (!dev_cap) {
			dev_cap = kzalloc(sizeof(*dev_cap), GFP_KERNEL);

			if (!dev_cap) {
				err = -ENOMEM;
				goto err_fw;
			}

			err = mlx4_QUERY_DEV_CAP(dev, dev_cap);
			if (err) {
				mlx4_err(dev, "QUERY_DEV_CAP command failed, aborting.\n");
				goto err_fw;
			}

			if (mlx4_check_dev_cap(dev, dev_cap, nvfs))
				goto err_fw;

			if (!(dev_cap->flags2 & MLX4_DEV_CAP_FLAG2_SYS_EQS)) {
				u64 dev_flags = mlx4_enable_sriov(dev, pdev,
								  total_vfs,
								  existing_vfs,
								  reset_flow);

				mlx4_close_fw(dev);
				mlx4_cmd_cleanup(dev, MLX4_CMD_CLEANUP_ALL);
				dev->flags = dev_flags;
				if (!SRIOV_VALID_STATE(dev->flags)) {
					mlx4_err(dev, "Invalid SRIOV state\n");
					goto err_sriov;
				}
				err = mlx4_reset(dev);
				if (err) {
					mlx4_err(dev, "Failed to reset HCA, aborting.\n");
					goto err_sriov;
				}
				goto slave_start;
			}
		} else {
			/* Legacy mode FW requires SRIOV to be enabled before
			 * doing QUERY_DEV_CAP, since max_eq's value is different if
			 * SRIOV is enabled.
			 */
			memset(dev_cap, 0, sizeof(*dev_cap));
			err = mlx4_QUERY_DEV_CAP(dev, dev_cap);
			if (err) {
				mlx4_err(dev, "QUERY_DEV_CAP command failed, aborting.\n");
				goto err_fw;
			}

			if (mlx4_check_dev_cap(dev, dev_cap, nvfs))
				goto err_fw;
		}
	}

	err = mlx4_init_hca(dev);
	if (err) {
		if (err == -EACCES) {
			/* Not primary Physical function
			 * Running in slave mode */
			mlx4_cmd_cleanup(dev, MLX4_CMD_CLEANUP_ALL);
			/* We're not a PF */
			if (dev->flags & MLX4_FLAG_SRIOV) {
				if (!existing_vfs)
					pci_disable_sriov(pdev);
				if (mlx4_is_master(dev) && !reset_flow)
					atomic_dec(&pf_loading);
				dev->flags &= ~MLX4_FLAG_SRIOV;
			}
			if (!mlx4_is_slave(dev))
				mlx4_free_ownership(dev);
			dev->flags |= MLX4_FLAG_SLAVE;
			dev->flags &= ~MLX4_FLAG_MASTER;
			goto slave_start;
		} else
			goto err_fw;
	}

	if (mlx4_is_master(dev) && (dev_cap->flags2 & MLX4_DEV_CAP_FLAG2_SYS_EQS)) {
		u64 dev_flags = mlx4_enable_sriov(dev, pdev, total_vfs,
						  existing_vfs, reset_flow);

		if ((dev->flags ^ dev_flags) & (MLX4_FLAG_MASTER | MLX4_FLAG_SLAVE)) {
			mlx4_cmd_cleanup(dev, MLX4_CMD_CLEANUP_VHCR);
			dev->flags = dev_flags;
			err = mlx4_cmd_init(dev);
			if (err) {
				/* Only VHCR is cleaned up, so could still
				 * send FW commands
				 */
				mlx4_err(dev, "Failed to init VHCR command interface, aborting\n");
				goto err_close;
			}
		} else {
			dev->flags = dev_flags;
		}

		if (!SRIOV_VALID_STATE(dev->flags)) {
			mlx4_err(dev, "Invalid SRIOV state\n");
			goto err_close;
		}
	}

	/* check if the device is functioning at its maximum possible speed.
	 * No return code for this call, just warn the user in case of PCI
	 * express device capabilities are under-satisfied by the bus.
	 */
	if (!mlx4_is_slave(dev))
		pcie_print_link_status(dev->persist->pdev);

	/* In master functions, the communication channel must be initialized
	 * after obtaining its address from fw */
	if (mlx4_is_master(dev)) {
		if (dev->caps.num_ports < 2 &&
		    num_vfs_argc > 1) {
			err = -EINVAL;
			mlx4_err(dev,
				 "Error: Trying to configure VFs on port 2, but HCA has only %d physical ports\n",
				 dev->caps.num_ports);
			goto err_close;
		}
		memcpy(dev->persist->nvfs, nvfs, sizeof(dev->persist->nvfs));

		for (i = 0;
		     i < sizeof(dev->persist->nvfs)/
		     sizeof(dev->persist->nvfs[0]); i++) {
			unsigned j;

			for (j = 0; j < dev->persist->nvfs[i]; ++sum, ++j) {
				dev->dev_vfs[sum].min_port = i < 2 ? i + 1 : 1;
				dev->dev_vfs[sum].n_ports = i < 2 ? 1 :
					dev->caps.num_ports;
			}
		}

		/* In master functions, the communication channel
		 * must be initialized after obtaining its address from fw
		 */
		err = mlx4_multi_func_init(dev);
		if (err) {
			mlx4_err(dev, "Failed to init master mfunc interface, aborting.\n");
			goto err_close;
		}
	}

	err = mlx4_alloc_eq_table(dev);
	if (err)
		goto err_master_mfunc;

	bitmap_zero(priv->msix_ctl.pool_bm, MAX_MSIX);
	mutex_init(&priv->msix_ctl.pool_lock);

	mlx4_enable_msi_x(dev);
	if ((mlx4_is_mfunc(dev)) &&
	    !(dev->flags & MLX4_FLAG_MSI_X)) {
		err = -EOPNOTSUPP;
		mlx4_err(dev, "INTx is not supported in multi-function mode, aborting\n");
		goto err_free_eq;
	}

	if (!mlx4_is_slave(dev)) {
		err = mlx4_init_steering(dev);
		if (err)
			goto err_disable_msix;
	}

	mlx4_init_quotas(dev);

	err = mlx4_setup_hca(dev);
	if (err == -EBUSY && (dev->flags & MLX4_FLAG_MSI_X) &&
	    !mlx4_is_mfunc(dev)) {
		dev->flags &= ~MLX4_FLAG_MSI_X;
		dev->caps.num_comp_vectors = 1;
		pci_disable_msix(pdev);
		err = mlx4_setup_hca(dev);
	}

	if (err)
		goto err_steer;

	/* When PF resources are ready arm its comm channel to enable
	 * getting commands
	 */
	if (mlx4_is_master(dev)) {
		err = mlx4_ARM_COMM_CHANNEL(dev);
		if (err) {
			mlx4_err(dev, " Failed to arm comm channel eq: %x\n",
				 err);
			goto err_steer;
		}
	}

	for (port = 1; port <= dev->caps.num_ports; port++) {
		err = mlx4_init_port_info(dev, port);
		if (err)
			goto err_port;
	}

	priv->v2p.port1 = 1;
	priv->v2p.port2 = 2;

	err = mlx4_register_device(dev);
	if (err)
		goto err_port;

	mlx4_request_modules(dev);

	mlx4_sense_init(dev);
	mlx4_start_sense(dev);

	priv->removed = 0;

	if (mlx4_is_master(dev) && dev->persist->num_vfs && !reset_flow)
		atomic_dec(&pf_loading);

	kfree(dev_cap);
	return 0;

err_port:
	for (--port; port >= 1; --port)
		mlx4_cleanup_port_info(&priv->port[port]);

	mlx4_cleanup_default_counters(dev);
	if (!mlx4_is_slave(dev))
		mlx4_cleanup_counters_table(dev);
	mlx4_cleanup_qp_table(dev);
	mlx4_cleanup_srq_table(dev);
	mlx4_cleanup_cq_table(dev);
	mlx4_cmd_use_polling(dev);
	mlx4_cleanup_eq_table(dev);
	mlx4_cleanup_mcg_table(dev);
	mlx4_cleanup_mr_table(dev);
	mlx4_cleanup_xrcd_table(dev);
	mlx4_cleanup_pd_table(dev);
	mlx4_cleanup_uar_table(dev);

err_steer:
	if (!mlx4_is_slave(dev))
		mlx4_clear_steering(dev);

err_disable_msix:
	if (dev->flags & MLX4_FLAG_MSI_X)
		pci_disable_msix(pdev);

err_free_eq:
	mlx4_free_eq_table(dev);

err_master_mfunc:
	if (mlx4_is_master(dev)) {
		mlx4_free_resource_tracker(dev, RES_TR_FREE_STRUCTS_ONLY);
		mlx4_multi_func_cleanup(dev);
	}

	if (mlx4_is_slave(dev))
		mlx4_slave_destroy_special_qp_cap(dev);

err_close:
	mlx4_close_hca(dev);

err_fw:
	mlx4_close_fw(dev);

err_mfunc:
	if (mlx4_is_slave(dev))
		mlx4_multi_func_cleanup(dev);

err_cmd:
	mlx4_cmd_cleanup(dev, MLX4_CMD_CLEANUP_ALL);

err_sriov:
	if (dev->flags & MLX4_FLAG_SRIOV && !existing_vfs) {
		pci_disable_sriov(pdev);
		dev->flags &= ~MLX4_FLAG_SRIOV;
	}

	if (mlx4_is_master(dev) && dev->persist->num_vfs && !reset_flow)
		atomic_dec(&pf_loading);

	kfree(priv->dev.dev_vfs);

	if (!mlx4_is_slave(dev))
		mlx4_free_ownership(dev);

	kfree(dev_cap);
	return err;
}

static int __mlx4_init_one(struct pci_dev *pdev, int pci_dev_data,
			   struct mlx4_priv *priv)
{
	int err;
	int nvfs[MLX4_MAX_PORTS + 1] = {0, 0, 0};
	int prb_vf[MLX4_MAX_PORTS + 1] = {0, 0, 0};
	const int param_map[MLX4_MAX_PORTS + 1][MLX4_MAX_PORTS + 1] = {
		{2, 0, 0}, {0, 1, 2}, {0, 1, 2} };
	unsigned total_vfs = 0;
	unsigned int i;

	pr_info(DRV_NAME ": Initializing %s\n", pci_name(pdev));

	err = mlx4_pci_enable_device(&priv->dev);
	if (err) {
		dev_err(&pdev->dev, "Cannot enable PCI device, aborting\n");
		return err;
	}

	/* Due to requirement that all VFs and the PF are *guaranteed* 2 MACS
	 * per port, we must limit the number of VFs to 63 (since their are
	 * 128 MACs)
	 */
	for (i = 0; i < ARRAY_SIZE(nvfs) && i < num_vfs_argc;
	     total_vfs += nvfs[param_map[num_vfs_argc - 1][i]], i++) {
		nvfs[param_map[num_vfs_argc - 1][i]] = num_vfs[i];
		if (nvfs[i] < 0) {
			dev_err(&pdev->dev, "num_vfs module parameter cannot be negative\n");
			err = -EINVAL;
			goto err_disable_pdev;
		}
	}
	for (i = 0; i < ARRAY_SIZE(prb_vf) && i < probe_vfs_argc;
	     i++) {
		prb_vf[param_map[probe_vfs_argc - 1][i]] = probe_vf[i];
		if (prb_vf[i] < 0 || prb_vf[i] > nvfs[i]) {
			dev_err(&pdev->dev, "probe_vf module parameter cannot be negative or greater than num_vfs\n");
			err = -EINVAL;
			goto err_disable_pdev;
		}
	}
	if (total_vfs > MLX4_MAX_NUM_VF) {
		dev_err(&pdev->dev,
			"Requested more VF's (%d) than allowed by hw (%d)\n",
			total_vfs, MLX4_MAX_NUM_VF);
		err = -EINVAL;
		goto err_disable_pdev;
	}

	for (i = 0; i < MLX4_MAX_PORTS; i++) {
		if (nvfs[i] + nvfs[2] > MLX4_MAX_NUM_VF_P_PORT) {
			dev_err(&pdev->dev,
				"Requested more VF's (%d) for port (%d) than allowed by driver (%d)\n",
				nvfs[i] + nvfs[2], i + 1,
				MLX4_MAX_NUM_VF_P_PORT);
			err = -EINVAL;
			goto err_disable_pdev;
		}
	}

	/* Check for BARs. */
	if (!(pci_dev_data & MLX4_PCI_DEV_IS_VF) &&
	    !(pci_resource_flags(pdev, 0) & IORESOURCE_MEM)) {
		dev_err(&pdev->dev, "Missing DCS, aborting (driver_data: 0x%x, pci_resource_flags(pdev, 0):0x%lx)\n",
			pci_dev_data, pci_resource_flags(pdev, 0));
		err = -ENODEV;
		goto err_disable_pdev;
	}
	if (!(pci_resource_flags(pdev, 2) & IORESOURCE_MEM)) {
		dev_err(&pdev->dev, "Missing UAR, aborting\n");
		err = -ENODEV;
		goto err_disable_pdev;
	}

	err = pci_request_regions(pdev, DRV_NAME);
	if (err) {
		dev_err(&pdev->dev, "Couldn't get PCI resources, aborting\n");
		goto err_disable_pdev;
	}

	pci_set_master(pdev);

	err = pci_set_dma_mask(pdev, DMA_BIT_MASK(64));
	if (err) {
		dev_warn(&pdev->dev, "Warning: couldn't set 64-bit PCI DMA mask\n");
		err = pci_set_dma_mask(pdev, DMA_BIT_MASK(32));
		if (err) {
			dev_err(&pdev->dev, "Can't set PCI DMA mask, aborting\n");
			goto err_release_regions;
		}
	}
	err = pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(64));
	if (err) {
		dev_warn(&pdev->dev, "Warning: couldn't set 64-bit consistent PCI DMA mask\n");
		err = pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(32));
		if (err) {
			dev_err(&pdev->dev, "Can't set consistent PCI DMA mask, aborting\n");
			goto err_release_regions;
		}
	}

	/* Allow large DMA segments, up to the firmware limit of 1 GB */
	dma_set_max_seg_size(&pdev->dev, 1024 * 1024 * 1024);
	/* Detect if this device is a virtual function */
	if (pci_dev_data & MLX4_PCI_DEV_IS_VF) {
		/* When acting as pf, we normally skip vfs unless explicitly
		 * requested to probe them.
		 */
		if (total_vfs) {
			unsigned vfs_offset = 0;

			for (i = 0; i < ARRAY_SIZE(nvfs) &&
			     vfs_offset + nvfs[i] < extended_func_num(pdev);
			     vfs_offset += nvfs[i], i++)
				;
			if (i == ARRAY_SIZE(nvfs)) {
				err = -ENODEV;
				goto err_release_regions;
			}
			if ((extended_func_num(pdev) - vfs_offset)
			    > prb_vf[i]) {
				dev_warn(&pdev->dev, "Skipping virtual function:%d\n",
					 extended_func_num(pdev));
				err = -ENODEV;
				goto err_release_regions;
			}
		}
	}

	err = mlx4_crdump_init(&priv->dev);
	if (err)
		goto err_release_regions;

	err = mlx4_catas_init(&priv->dev);
	if (err)
		goto err_crdump;

	err = mlx4_load_one(pdev, pci_dev_data, total_vfs, nvfs, priv, 0);
	if (err)
		goto err_catas;

	return 0;

err_catas:
	mlx4_catas_end(&priv->dev);

err_crdump:
	mlx4_crdump_end(&priv->dev);

err_release_regions:
	pci_release_regions(pdev);

err_disable_pdev:
	mlx4_pci_disable_device(&priv->dev);
	return err;
}

static int mlx4_devlink_port_type_set(struct devlink_port *devlink_port,
				      enum devlink_port_type port_type)
{
	struct mlx4_port_info *info = container_of(devlink_port,
						   struct mlx4_port_info,
						   devlink_port);
	enum mlx4_port_type mlx4_port_type;

	switch (port_type) {
	case DEVLINK_PORT_TYPE_AUTO:
		mlx4_port_type = MLX4_PORT_TYPE_AUTO;
		break;
	case DEVLINK_PORT_TYPE_ETH:
		mlx4_port_type = MLX4_PORT_TYPE_ETH;
		break;
	case DEVLINK_PORT_TYPE_IB:
		mlx4_port_type = MLX4_PORT_TYPE_IB;
		break;
	default:
		return -EOPNOTSUPP;
	}

	return __set_port_type(info, mlx4_port_type);
}

static void mlx4_devlink_param_load_driverinit_values(struct devlink *devlink)
{
	struct mlx4_priv *priv = devlink_priv(devlink);
	struct mlx4_dev *dev = &priv->dev;
	struct mlx4_fw_crdump *crdump = &dev->persist->crdump;
	union devlink_param_value saved_value;
	int err;

	err = devlink_param_driverinit_value_get(devlink,
						 DEVLINK_PARAM_GENERIC_ID_INT_ERR_RESET,
						 &saved_value);
	if (!err && mlx4_internal_err_reset != saved_value.vbool) {
		mlx4_internal_err_reset = saved_value.vbool;
		/* Notify on value changed on runtime configuration mode */
		devlink_param_value_changed(devlink,
					    DEVLINK_PARAM_GENERIC_ID_INT_ERR_RESET);
	}
	err = devlink_param_driverinit_value_get(devlink,
						 DEVLINK_PARAM_GENERIC_ID_MAX_MACS,
						 &saved_value);
	if (!err)
		log_num_mac = order_base_2(saved_value.vu32);
	err = devlink_param_driverinit_value_get(devlink,
						 MLX4_DEVLINK_PARAM_ID_ENABLE_64B_CQE_EQE,
						 &saved_value);
	if (!err)
		enable_64b_cqe_eqe = saved_value.vbool;
	err = devlink_param_driverinit_value_get(devlink,
						 MLX4_DEVLINK_PARAM_ID_ENABLE_4K_UAR,
						 &saved_value);
	if (!err)
		enable_4k_uar = saved_value.vbool;
	err = devlink_param_driverinit_value_get(devlink,
						 DEVLINK_PARAM_GENERIC_ID_REGION_SNAPSHOT,
						 &saved_value);
	if (!err && crdump->snapshot_enable != saved_value.vbool) {
		crdump->snapshot_enable = saved_value.vbool;
		devlink_param_value_changed(devlink,
					    DEVLINK_PARAM_GENERIC_ID_REGION_SNAPSHOT);
	}
}

static void mlx4_restart_one_down(struct pci_dev *pdev);
static int mlx4_restart_one_up(struct pci_dev *pdev, bool reload,
			       struct devlink *devlink);

static int mlx4_devlink_reload_down(struct devlink *devlink, bool netns_change,
				    enum devlink_reload_action action,
				    enum devlink_reload_limit limit,
				    struct netlink_ext_ack *extack)
{
	struct mlx4_priv *priv = devlink_priv(devlink);
	struct mlx4_dev *dev = &priv->dev;
	struct mlx4_dev_persistent *persist = dev->persist;

	if (netns_change) {
		NL_SET_ERR_MSG_MOD(extack, "Namespace change is not supported");
		return -EOPNOTSUPP;
	}
	if (persist->num_vfs)
		mlx4_warn(persist->dev, "Reload performed on PF, will cause reset on operating Virtual Functions\n");
	mlx4_restart_one_down(persist->pdev);
	return 0;
}

static int mlx4_devlink_reload_up(struct devlink *devlink, enum devlink_reload_action action,
				  enum devlink_reload_limit limit, u32 *actions_performed,
				  struct netlink_ext_ack *extack)
{
	struct mlx4_priv *priv = devlink_priv(devlink);
	struct mlx4_dev *dev = &priv->dev;
	struct mlx4_dev_persistent *persist = dev->persist;
	int err;

	*actions_performed = BIT(DEVLINK_RELOAD_ACTION_DRIVER_REINIT);
	err = mlx4_restart_one_up(persist->pdev, true, devlink);
	if (err)
		mlx4_err(persist->dev, "mlx4_restart_one_up failed, ret=%d\n",
			 err);

	return err;
}

static const struct devlink_ops mlx4_devlink_ops = {
	.port_type_set	= mlx4_devlink_port_type_set,
	.reload_actions = BIT(DEVLINK_RELOAD_ACTION_DRIVER_REINIT),
	.reload_down	= mlx4_devlink_reload_down,
	.reload_up	= mlx4_devlink_reload_up,
};

static int mlx4_init_one(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct devlink *devlink;
	struct mlx4_priv *priv;
	struct mlx4_dev *dev;
	int ret;

	printk_once(KERN_INFO "%s", mlx4_version);

	devlink = devlink_alloc(&mlx4_devlink_ops, sizeof(*priv));
	if (!devlink)
		return -ENOMEM;
	priv = devlink_priv(devlink);

	dev       = &priv->dev;
	dev->persist = kzalloc(sizeof(*dev->persist), GFP_KERNEL);
	if (!dev->persist) {
		ret = -ENOMEM;
		goto err_devlink_free;
	}
	dev->persist->pdev = pdev;
	dev->persist->dev = dev;
	pci_set_drvdata(pdev, dev->persist);
	priv->pci_dev_data = id->driver_data;
	mutex_init(&dev->persist->device_state_mutex);
	mutex_init(&dev->persist->interface_state_mutex);
	mutex_init(&dev->persist->pci_status_mutex);

	ret = devlink_register(devlink, &pdev->dev);
	if (ret)
		goto err_persist_free;
	ret = devlink_params_register(devlink, mlx4_devlink_params,
				      ARRAY_SIZE(mlx4_devlink_params));
	if (ret)
		goto err_devlink_unregister;
	mlx4_devlink_set_params_init_values(devlink);
	ret =  __mlx4_init_one(pdev, id->driver_data, priv);
	if (ret)
		goto err_params_unregister;

	devlink_params_publish(devlink);
	devlink_reload_enable(devlink);
	pci_save_state(pdev);
	return 0;

err_params_unregister:
	devlink_params_unregister(devlink, mlx4_devlink_params,
				  ARRAY_SIZE(mlx4_devlink_params));
err_devlink_unregister:
	devlink_unregister(devlink);
err_persist_free:
	kfree(dev->persist);
err_devlink_free:
	devlink_free(devlink);
	return ret;
}

static void mlx4_clean_dev(struct mlx4_dev *dev)
{
	struct mlx4_dev_persistent *persist = dev->persist;
	struct mlx4_priv *priv = mlx4_priv(dev);
	unsigned long	flags = (dev->flags & RESET_PERSIST_MASK_FLAGS);

	memset(priv, 0, sizeof(*priv));
	priv->dev.persist = persist;
	priv->dev.flags = flags;
}

static void mlx4_unload_one(struct pci_dev *pdev)
{
	struct mlx4_dev_persistent *persist = pci_get_drvdata(pdev);
	struct mlx4_dev  *dev  = persist->dev;
	struct mlx4_priv *priv = mlx4_priv(dev);
	int               pci_dev_data;
	int p, i;

	if (priv->removed)
		return;

	/* saving current ports type for further use */
	for (i = 0; i < dev->caps.num_ports; i++) {
		dev->persist->curr_port_type[i] = dev->caps.port_type[i + 1];
		dev->persist->curr_port_poss_type[i] = dev->caps.
						       possible_type[i + 1];
	}

	pci_dev_data = priv->pci_dev_data;

	mlx4_stop_sense(dev);
	mlx4_unregister_device(dev);

	for (p = 1; p <= dev->caps.num_ports; p++) {
		mlx4_cleanup_port_info(&priv->port[p]);
		mlx4_CLOSE_PORT(dev, p);
	}

	if (mlx4_is_master(dev))
		mlx4_free_resource_tracker(dev,
					   RES_TR_FREE_SLAVES_ONLY);

	mlx4_cleanup_default_counters(dev);
	if (!mlx4_is_slave(dev))
		mlx4_cleanup_counters_table(dev);
	mlx4_cleanup_qp_table(dev);
	mlx4_cleanup_srq_table(dev);
	mlx4_cleanup_cq_table(dev);
	mlx4_cmd_use_polling(dev);
	mlx4_cleanup_eq_table(dev);
	mlx4_cleanup_mcg_table(dev);
	mlx4_cleanup_mr_table(dev);
	mlx4_cleanup_xrcd_table(dev);
	mlx4_cleanup_pd_table(dev);

	if (mlx4_is_master(dev))
		mlx4_free_resource_tracker(dev,
					   RES_TR_FREE_STRUCTS_ONLY);

	iounmap(priv->kar);
	mlx4_uar_free(dev, &priv->driver_uar);
	mlx4_cleanup_uar_table(dev);
	if (!mlx4_is_slave(dev))
		mlx4_clear_steering(dev);
	mlx4_free_eq_table(dev);
	if (mlx4_is_master(dev))
		mlx4_multi_func_cleanup(dev);
	mlx4_close_hca(dev);
	mlx4_close_fw(dev);
	if (mlx4_is_slave(dev))
		mlx4_multi_func_cleanup(dev);
	mlx4_cmd_cleanup(dev, MLX4_CMD_CLEANUP_ALL);

	if (dev->flags & MLX4_FLAG_MSI_X)
		pci_disable_msix(pdev);

	if (!mlx4_is_slave(dev))
		mlx4_free_ownership(dev);

	mlx4_slave_destroy_special_qp_cap(dev);
	kfree(dev->dev_vfs);

	mlx4_clean_dev(dev);
	priv->pci_dev_data = pci_dev_data;
	priv->removed = 1;
}

static void mlx4_remove_one(struct pci_dev *pdev)
{
	struct mlx4_dev_persistent *persist = pci_get_drvdata(pdev);
	struct mlx4_dev  *dev  = persist->dev;
	struct mlx4_priv *priv = mlx4_priv(dev);
	struct devlink *devlink = priv_to_devlink(priv);
	int active_vfs = 0;

	devlink_reload_disable(devlink);

	if (mlx4_is_slave(dev))
		persist->interface_state |= MLX4_INTERFACE_STATE_NOWAIT;

	mutex_lock(&persist->interface_state_mutex);
	persist->interface_state |= MLX4_INTERFACE_STATE_DELETION;
	mutex_unlock(&persist->interface_state_mutex);

	/* Disabling SR-IOV is not allowed while there are active vf's */
	if (mlx4_is_master(dev) && dev->flags & MLX4_FLAG_SRIOV) {
		active_vfs = mlx4_how_many_lives_vf(dev);
		if (active_vfs) {
			pr_warn("Removing PF when there are active VF's !!\n");
			pr_warn("Will not disable SR-IOV.\n");
		}
	}

	/* device marked to be under deletion running now without the lock
	 * letting other tasks to be terminated
	 */
	if (persist->interface_state & MLX4_INTERFACE_STATE_UP)
		mlx4_unload_one(pdev);
	else
		mlx4_info(dev, "%s: interface is down\n", __func__);
	mlx4_catas_end(dev);
	mlx4_crdump_end(dev);
	if (dev->flags & MLX4_FLAG_SRIOV && !active_vfs) {
		mlx4_warn(dev, "Disabling SR-IOV\n");
		pci_disable_sriov(pdev);
	}

	pci_release_regions(pdev);
	mlx4_pci_disable_device(dev);
	devlink_params_unregister(devlink, mlx4_devlink_params,
				  ARRAY_SIZE(mlx4_devlink_params));
	devlink_unregister(devlink);
	kfree(dev->persist);
	devlink_free(devlink);
}

static int restore_current_port_types(struct mlx4_dev *dev,
				      enum mlx4_port_type *types,
				      enum mlx4_port_type *poss_types)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	int err, i;

	mlx4_stop_sense(dev);

	mutex_lock(&priv->port_mutex);
	for (i = 0; i < dev->caps.num_ports; i++)
		dev->caps.possible_type[i + 1] = poss_types[i];
	err = mlx4_change_port_types(dev, types);
	mlx4_start_sense(dev);
	mutex_unlock(&priv->port_mutex);

	return err;
}

static void mlx4_restart_one_down(struct pci_dev *pdev)
{
	mlx4_unload_one(pdev);
}

static int mlx4_restart_one_up(struct pci_dev *pdev, bool reload,
			       struct devlink *devlink)
{
	struct mlx4_dev_persistent *persist = pci_get_drvdata(pdev);
	struct mlx4_dev	 *dev  = persist->dev;
	struct mlx4_priv *priv = mlx4_priv(dev);
	int nvfs[MLX4_MAX_PORTS + 1] = {0, 0, 0};
	int pci_dev_data, err, total_vfs;

	pci_dev_data = priv->pci_dev_data;
	total_vfs = dev->persist->num_vfs;
	memcpy(nvfs, dev->persist->nvfs, sizeof(dev->persist->nvfs));

	if (reload)
		mlx4_devlink_param_load_driverinit_values(devlink);
	err = mlx4_load_one(pdev, pci_dev_data, total_vfs, nvfs, priv, 1);
	if (err) {
		mlx4_err(dev, "%s: ERROR: mlx4_load_one failed, pci_name=%s, err=%d\n",
			 __func__, pci_name(pdev), err);
		return err;
	}

	err = restore_current_port_types(dev, dev->persist->curr_port_type,
					 dev->persist->curr_port_poss_type);
	if (err)
		mlx4_err(dev, "could not restore original port types (%d)\n",
			 err);

	return err;
}

int mlx4_restart_one(struct pci_dev *pdev)
{
	mlx4_restart_one_down(pdev);
	return mlx4_restart_one_up(pdev, false, NULL);
}

#define MLX_SP(id) { PCI_VDEVICE(MELLANOX, id), MLX4_PCI_DEV_FORCE_SENSE_PORT }
#define MLX_VF(id) { PCI_VDEVICE(MELLANOX, id), MLX4_PCI_DEV_IS_VF }
#define MLX_GN(id) { PCI_VDEVICE(MELLANOX, id), 0 }

static const struct pci_device_id mlx4_pci_table[] = {
#ifdef CONFIG_MLX4_CORE_GEN2
	/* MT25408 "Hermon" */
	MLX_SP(PCI_DEVICE_ID_MELLANOX_HERMON_SDR),	/* SDR */
	MLX_SP(PCI_DEVICE_ID_MELLANOX_HERMON_DDR),	/* DDR */
	MLX_SP(PCI_DEVICE_ID_MELLANOX_HERMON_QDR),	/* QDR */
	MLX_SP(PCI_DEVICE_ID_MELLANOX_HERMON_DDR_GEN2), /* DDR Gen2 */
	MLX_SP(PCI_DEVICE_ID_MELLANOX_HERMON_QDR_GEN2),	/* QDR Gen2 */
	MLX_SP(PCI_DEVICE_ID_MELLANOX_HERMON_EN),	/* EN 10GigE */
	MLX_SP(PCI_DEVICE_ID_MELLANOX_HERMON_EN_GEN2),  /* EN 10GigE Gen2 */
	/* MT25458 ConnectX EN 10GBASE-T */
	MLX_SP(PCI_DEVICE_ID_MELLANOX_CONNECTX_EN),
	MLX_SP(PCI_DEVICE_ID_MELLANOX_CONNECTX_EN_T_GEN2),	/* Gen2 */
	/* MT26468 ConnectX EN 10GigE PCIe Gen2*/
	MLX_SP(PCI_DEVICE_ID_MELLANOX_CONNECTX_EN_GEN2),
	/* MT26438 ConnectX EN 40GigE PCIe Gen2 5GT/s */
	MLX_SP(PCI_DEVICE_ID_MELLANOX_CONNECTX_EN_5_GEN2),
	/* MT26478 ConnectX2 40GigE PCIe Gen2 */
	MLX_SP(PCI_DEVICE_ID_MELLANOX_CONNECTX2),
	/* MT25400 Family [ConnectX-2] */
	MLX_VF(0x1002),					/* Virtual Function */
#endif /* CONFIG_MLX4_CORE_GEN2 */
	/* MT27500 Family [ConnectX-3] */
	MLX_GN(PCI_DEVICE_ID_MELLANOX_CONNECTX3),
	MLX_VF(0x1004),					/* Virtual Function */
	MLX_GN(0x1005),					/* MT27510 Family */
	MLX_GN(0x1006),					/* MT27511 Family */
	MLX_GN(PCI_DEVICE_ID_MELLANOX_CONNECTX3_PRO),	/* MT27520 Family */
	MLX_GN(0x1008),					/* MT27521 Family */
	MLX_GN(0x1009),					/* MT27530 Family */
	MLX_GN(0x100a),					/* MT27531 Family */
	MLX_GN(0x100b),					/* MT27540 Family */
	MLX_GN(0x100c),					/* MT27541 Family */
	MLX_GN(0x100d),					/* MT27550 Family */
	MLX_GN(0x100e),					/* MT27551 Family */
	MLX_GN(0x100f),					/* MT27560 Family */
	MLX_GN(0x1010),					/* MT27561 Family */

	/*
	 * See the mellanox_check_broken_intx_masking() quirk when
	 * adding devices
	 */

	{ 0, }
};

MODULE_DEVICE_TABLE(pci, mlx4_pci_table);

static pci_ers_result_t mlx4_pci_err_detected(struct pci_dev *pdev,
					      pci_channel_state_t state)
{
	struct mlx4_dev_persistent *persist = pci_get_drvdata(pdev);

	mlx4_err(persist->dev, "mlx4_pci_err_detected was called\n");
	mlx4_enter_error_state(persist);

	mutex_lock(&persist->interface_state_mutex);
	if (persist->interface_state & MLX4_INTERFACE_STATE_UP)
		mlx4_unload_one(pdev);

	mutex_unlock(&persist->interface_state_mutex);
	if (state == pci_channel_io_perm_failure)
		return PCI_ERS_RESULT_DISCONNECT;

	mlx4_pci_disable_device(persist->dev);
	return PCI_ERS_RESULT_NEED_RESET;
}

static pci_ers_result_t mlx4_pci_slot_reset(struct pci_dev *pdev)
{
	struct mlx4_dev_persistent *persist = pci_get_drvdata(pdev);
	struct mlx4_dev	 *dev  = persist->dev;
	int err;

	mlx4_err(dev, "mlx4_pci_slot_reset was called\n");
	err = mlx4_pci_enable_device(dev);
	if (err) {
		mlx4_err(dev, "Can not re-enable device, err=%d\n", err);
		return PCI_ERS_RESULT_DISCONNECT;
	}

	pci_set_master(pdev);
	pci_restore_state(pdev);
	pci_save_state(pdev);
	return PCI_ERS_RESULT_RECOVERED;
}

static void mlx4_pci_resume(struct pci_dev *pdev)
{
	struct mlx4_dev_persistent *persist = pci_get_drvdata(pdev);
	struct mlx4_dev	 *dev  = persist->dev;
	struct mlx4_priv *priv = mlx4_priv(dev);
	int nvfs[MLX4_MAX_PORTS + 1] = {0, 0, 0};
	int total_vfs;
	int err;

	mlx4_err(dev, "%s was called\n", __func__);
	total_vfs = dev->persist->num_vfs;
	memcpy(nvfs, dev->persist->nvfs, sizeof(dev->persist->nvfs));

	mutex_lock(&persist->interface_state_mutex);
	if (!(persist->interface_state & MLX4_INTERFACE_STATE_UP)) {
		err = mlx4_load_one(pdev, priv->pci_dev_data, total_vfs, nvfs,
				    priv, 1);
		if (err) {
			mlx4_err(dev, "%s: mlx4_load_one failed, err=%d\n",
				 __func__,  err);
			goto end;
		}

		err = restore_current_port_types(dev, dev->persist->
						 curr_port_type, dev->persist->
						 curr_port_poss_type);
		if (err)
			mlx4_err(dev, "could not restore original port types (%d)\n", err);
	}
end:
	mutex_unlock(&persist->interface_state_mutex);

}

static void mlx4_shutdown(struct pci_dev *pdev)
{
	struct mlx4_dev_persistent *persist = pci_get_drvdata(pdev);
	struct mlx4_dev *dev = persist->dev;

	mlx4_info(persist->dev, "mlx4_shutdown was called\n");
	mutex_lock(&persist->interface_state_mutex);
	if (persist->interface_state & MLX4_INTERFACE_STATE_UP)
		mlx4_unload_one(pdev);
	mutex_unlock(&persist->interface_state_mutex);
	mlx4_pci_disable_device(dev);
}

static const struct pci_error_handlers mlx4_err_handler = {
	.error_detected = mlx4_pci_err_detected,
	.slot_reset     = mlx4_pci_slot_reset,
	.resume		= mlx4_pci_resume,
};

static int __maybe_unused mlx4_suspend(struct device *dev_d)
{
	struct pci_dev *pdev = to_pci_dev(dev_d);
	struct mlx4_dev_persistent *persist = pci_get_drvdata(pdev);
	struct mlx4_dev	*dev = persist->dev;

	mlx4_err(dev, "suspend was called\n");
	mutex_lock(&persist->interface_state_mutex);
	if (persist->interface_state & MLX4_INTERFACE_STATE_UP)
		mlx4_unload_one(pdev);
	mutex_unlock(&persist->interface_state_mutex);

	return 0;
}

static int __maybe_unused mlx4_resume(struct device *dev_d)
{
	struct pci_dev *pdev = to_pci_dev(dev_d);
	struct mlx4_dev_persistent *persist = pci_get_drvdata(pdev);
	struct mlx4_dev	*dev = persist->dev;
	struct mlx4_priv *priv = mlx4_priv(dev);
	int nvfs[MLX4_MAX_PORTS + 1] = {0, 0, 0};
	int total_vfs;
	int ret = 0;

	mlx4_err(dev, "resume was called\n");
	total_vfs = dev->persist->num_vfs;
	memcpy(nvfs, dev->persist->nvfs, sizeof(dev->persist->nvfs));

	mutex_lock(&persist->interface_state_mutex);
	if (!(persist->interface_state & MLX4_INTERFACE_STATE_UP)) {
		ret = mlx4_load_one(pdev, priv->pci_dev_data, total_vfs,
				    nvfs, priv, 1);
		if (!ret) {
			ret = restore_current_port_types(dev,
					dev->persist->curr_port_type,
					dev->persist->curr_port_poss_type);
			if (ret)
				mlx4_err(dev, "resume: could not restore original port types (%d)\n", ret);
		}
	}
	mutex_unlock(&persist->interface_state_mutex);

	return ret;
}

static SIMPLE_DEV_PM_OPS(mlx4_pm_ops, mlx4_suspend, mlx4_resume);

static struct pci_driver mlx4_driver = {
	.name		= DRV_NAME,
	.id_table	= mlx4_pci_table,
	.probe		= mlx4_init_one,
	.shutdown	= mlx4_shutdown,
	.remove		= mlx4_remove_one,
	.driver.pm	= &mlx4_pm_ops,
	.err_handler    = &mlx4_err_handler,
};

static int __init mlx4_verify_params(void)
{
	if (msi_x < 0) {
		pr_warn("mlx4_core: bad msi_x: %d\n", msi_x);
		return -1;
	}

	if ((log_num_mac < 0) || (log_num_mac > 7)) {
		pr_warn("mlx4_core: bad num_mac: %d\n", log_num_mac);
		return -1;
	}

	if (log_num_vlan != 0)
		pr_warn("mlx4_core: log_num_vlan - obsolete module param, using %d\n",
			MLX4_LOG_NUM_VLANS);

	if (use_prio != 0)
		pr_warn("mlx4_core: use_prio - obsolete module param, ignored\n");

	if ((log_mtts_per_seg < 0) || (log_mtts_per_seg > 7)) {
		pr_warn("mlx4_core: bad log_mtts_per_seg: %d\n",
			log_mtts_per_seg);
		return -1;
	}

	/* Check if module param for ports type has legal combination */
	if (port_type_array[0] == false && port_type_array[1] == true) {
		pr_warn("Module parameter configuration ETH/IB is not supported. Switching to default configuration IB/IB\n");
		port_type_array[0] = true;
	}

	if (mlx4_log_num_mgm_entry_size < -7 ||
	    (mlx4_log_num_mgm_entry_size > 0 &&
	     (mlx4_log_num_mgm_entry_size < MLX4_MIN_MGM_LOG_ENTRY_SIZE ||
	      mlx4_log_num_mgm_entry_size > MLX4_MAX_MGM_LOG_ENTRY_SIZE))) {
		pr_warn("mlx4_core: mlx4_log_num_mgm_entry_size (%d) not in legal range (-7..0 or %d..%d)\n",
			mlx4_log_num_mgm_entry_size,
			MLX4_MIN_MGM_LOG_ENTRY_SIZE,
			MLX4_MAX_MGM_LOG_ENTRY_SIZE);
		return -1;
	}

	return 0;
}

static int __init mlx4_init(void)
{
	int ret;

	if (mlx4_verify_params())
		return -EINVAL;


	mlx4_wq = create_singlethread_workqueue("mlx4");
	if (!mlx4_wq)
		return -ENOMEM;

	ret = pci_register_driver(&mlx4_driver);
	if (ret < 0)
		destroy_workqueue(mlx4_wq);
	return ret < 0 ? ret : 0;
}

static void __exit mlx4_cleanup(void)
{
	pci_unregister_driver(&mlx4_driver);
	destroy_workqueue(mlx4_wq);
}

module_init(mlx4_init);
module_exit(mlx4_cleanup);
