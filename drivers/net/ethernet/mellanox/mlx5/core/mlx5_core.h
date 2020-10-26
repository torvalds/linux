/*
 * Copyright (c) 2013-2015, Mellanox Technologies, Ltd.  All rights reserved.
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

#ifndef __MLX5_CORE_H__
#define __MLX5_CORE_H__

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/if_link.h>
#include <linux/firmware.h>
#include <linux/mlx5/cq.h>
#include <linux/mlx5/fs.h>
#include <linux/mlx5/driver.h>

#define DRIVER_NAME "mlx5_core"
#define DRIVER_VERSION "5.0-0"

extern uint mlx5_core_debug_mask;

#define mlx5_core_dbg(__dev, format, ...)				\
	dev_dbg((__dev)->device, "%s:%d:(pid %d): " format,		\
		 __func__, __LINE__, current->pid,			\
		 ##__VA_ARGS__)

#define mlx5_core_dbg_once(__dev, format, ...)		\
	dev_dbg_once((__dev)->device,		\
		     "%s:%d:(pid %d): " format,		\
		     __func__, __LINE__, current->pid,	\
		     ##__VA_ARGS__)

#define mlx5_core_dbg_mask(__dev, mask, format, ...)		\
do {								\
	if ((mask) & mlx5_core_debug_mask)			\
		mlx5_core_dbg(__dev, format, ##__VA_ARGS__);	\
} while (0)

#define mlx5_core_err(__dev, format, ...)			\
	dev_err((__dev)->device, "%s:%d:(pid %d): " format,	\
		__func__, __LINE__, current->pid,		\
	       ##__VA_ARGS__)

#define mlx5_core_err_rl(__dev, format, ...)			\
	dev_err_ratelimited((__dev)->device,			\
			    "%s:%d:(pid %d): " format,		\
			    __func__, __LINE__, current->pid,	\
			    ##__VA_ARGS__)

#define mlx5_core_warn(__dev, format, ...)			\
	dev_warn((__dev)->device, "%s:%d:(pid %d): " format,	\
		 __func__, __LINE__, current->pid,		\
		 ##__VA_ARGS__)

#define mlx5_core_warn_once(__dev, format, ...)				\
	dev_warn_once((__dev)->device, "%s:%d:(pid %d): " format,	\
		      __func__, __LINE__, current->pid,			\
		      ##__VA_ARGS__)

#define mlx5_core_warn_rl(__dev, format, ...)			\
	dev_warn_ratelimited((__dev)->device,			\
			     "%s:%d:(pid %d): " format,		\
			     __func__, __LINE__, current->pid,	\
			     ##__VA_ARGS__)

#define mlx5_core_info(__dev, format, ...)		\
	dev_info((__dev)->device, format, ##__VA_ARGS__)

#define mlx5_core_info_rl(__dev, format, ...)			\
	dev_info_ratelimited((__dev)->device,			\
			     "%s:%d:(pid %d): " format,		\
			     __func__, __LINE__, current->pid,	\
			     ##__VA_ARGS__)

static inline struct device *mlx5_core_dma_dev(struct mlx5_core_dev *dev)
{
	return &dev->pdev->dev;
}

enum {
	MLX5_CMD_DATA, /* print command payload only */
	MLX5_CMD_TIME, /* print command execution time */
};

enum {
	MLX5_DRIVER_STATUS_ABORTED = 0xfe,
	MLX5_DRIVER_SYND = 0xbadd00de,
};

enum mlx5_semaphore_space_address {
	MLX5_SEMAPHORE_SPACE_DOMAIN     = 0xA,
	MLX5_SEMAPHORE_SW_RESET         = 0x20,
};

int mlx5_query_hca_caps(struct mlx5_core_dev *dev);
int mlx5_query_board_id(struct mlx5_core_dev *dev);
int mlx5_cmd_init_hca(struct mlx5_core_dev *dev, uint32_t *sw_owner_id);
int mlx5_cmd_teardown_hca(struct mlx5_core_dev *dev);
int mlx5_cmd_force_teardown_hca(struct mlx5_core_dev *dev);
int mlx5_cmd_fast_teardown_hca(struct mlx5_core_dev *dev);
void mlx5_enter_error_state(struct mlx5_core_dev *dev, bool force);
void mlx5_error_sw_reset(struct mlx5_core_dev *dev);
u32 mlx5_health_check_fatal_sensors(struct mlx5_core_dev *dev);
int mlx5_health_wait_pci_up(struct mlx5_core_dev *dev);
void mlx5_disable_device(struct mlx5_core_dev *dev);
void mlx5_recover_device(struct mlx5_core_dev *dev);
int mlx5_sriov_init(struct mlx5_core_dev *dev);
void mlx5_sriov_cleanup(struct mlx5_core_dev *dev);
int mlx5_sriov_attach(struct mlx5_core_dev *dev);
void mlx5_sriov_detach(struct mlx5_core_dev *dev);
int mlx5_core_sriov_configure(struct pci_dev *dev, int num_vfs);
int mlx5_core_enable_hca(struct mlx5_core_dev *dev, u16 func_id);
int mlx5_core_disable_hca(struct mlx5_core_dev *dev, u16 func_id);
int mlx5_create_scheduling_element_cmd(struct mlx5_core_dev *dev, u8 hierarchy,
				       void *context, u32 *element_id);
int mlx5_modify_scheduling_element_cmd(struct mlx5_core_dev *dev, u8 hierarchy,
				       void *context, u32 element_id,
				       u32 modify_bitmask);
int mlx5_destroy_scheduling_element_cmd(struct mlx5_core_dev *dev, u8 hierarchy,
					u32 element_id);
int mlx5_wait_for_pages(struct mlx5_core_dev *dev, int *pages);

void mlx5_cmd_trigger_completions(struct mlx5_core_dev *dev);
void mlx5_cmd_flush(struct mlx5_core_dev *dev);
void mlx5_cq_debugfs_init(struct mlx5_core_dev *dev);
void mlx5_cq_debugfs_cleanup(struct mlx5_core_dev *dev);

int mlx5_query_pcam_reg(struct mlx5_core_dev *dev, u32 *pcam, u8 feature_group,
			u8 access_reg_group);
int mlx5_query_mcam_reg(struct mlx5_core_dev *dev, u32 *mcap, u8 feature_group,
			u8 access_reg_group);
int mlx5_query_qcam_reg(struct mlx5_core_dev *mdev, u32 *qcam,
			u8 feature_group, u8 access_reg_group);

void mlx5_lag_add(struct mlx5_core_dev *dev, struct net_device *netdev);
void mlx5_lag_remove(struct mlx5_core_dev *dev);

int mlx5_irq_table_init(struct mlx5_core_dev *dev);
void mlx5_irq_table_cleanup(struct mlx5_core_dev *dev);
int mlx5_irq_table_create(struct mlx5_core_dev *dev);
void mlx5_irq_table_destroy(struct mlx5_core_dev *dev);
int mlx5_irq_attach_nb(struct mlx5_irq_table *irq_table, int vecidx,
		       struct notifier_block *nb);
int mlx5_irq_detach_nb(struct mlx5_irq_table *irq_table, int vecidx,
		       struct notifier_block *nb);
struct cpumask *
mlx5_irq_get_affinity_mask(struct mlx5_irq_table *irq_table, int vecidx);
struct cpu_rmap *mlx5_irq_get_rmap(struct mlx5_irq_table *table);
int mlx5_irq_get_num_comp(struct mlx5_irq_table *table);

int mlx5_events_init(struct mlx5_core_dev *dev);
void mlx5_events_cleanup(struct mlx5_core_dev *dev);
void mlx5_events_start(struct mlx5_core_dev *dev);
void mlx5_events_stop(struct mlx5_core_dev *dev);

void mlx5_add_device(struct mlx5_interface *intf, struct mlx5_priv *priv);
void mlx5_remove_device(struct mlx5_interface *intf, struct mlx5_priv *priv);
void mlx5_attach_device(struct mlx5_core_dev *dev);
void mlx5_detach_device(struct mlx5_core_dev *dev);
bool mlx5_device_registered(struct mlx5_core_dev *dev);
void mlx5_register_device(struct mlx5_core_dev *dev);
void mlx5_unregister_device(struct mlx5_core_dev *dev);
void mlx5_add_dev_by_protocol(struct mlx5_core_dev *dev, int protocol);
void mlx5_remove_dev_by_protocol(struct mlx5_core_dev *dev, int protocol);
struct mlx5_core_dev *mlx5_get_next_phys_dev(struct mlx5_core_dev *dev);
void mlx5_dev_list_lock(void);
void mlx5_dev_list_unlock(void);
int mlx5_dev_list_trylock(void);

bool mlx5_lag_intf_add(struct mlx5_interface *intf, struct mlx5_priv *priv);

int mlx5_query_mtpps(struct mlx5_core_dev *dev, u32 *mtpps, u32 mtpps_size);
int mlx5_set_mtpps(struct mlx5_core_dev *mdev, u32 *mtpps, u32 mtpps_size);
int mlx5_query_mtppse(struct mlx5_core_dev *mdev, u8 pin, u8 *arm, u8 *mode);
int mlx5_set_mtppse(struct mlx5_core_dev *mdev, u8 pin, u8 arm, u8 mode);

struct mlx5_dm *mlx5_dm_create(struct mlx5_core_dev *dev);
void mlx5_dm_cleanup(struct mlx5_core_dev *dev);

#define MLX5_PPS_CAP(mdev) (MLX5_CAP_GEN((mdev), pps) &&		\
			    MLX5_CAP_GEN((mdev), pps_modify) &&		\
			    MLX5_CAP_MCAM_FEATURE((mdev), mtpps_fs) &&	\
			    MLX5_CAP_MCAM_FEATURE((mdev), mtpps_enh_out_per_adj))

int mlx5_firmware_flash(struct mlx5_core_dev *dev, const struct firmware *fw,
			struct netlink_ext_ack *extack);
int mlx5_fw_version_query(struct mlx5_core_dev *dev,
			  u32 *running_ver, u32 *stored_ver);

void mlx5e_init(void);
void mlx5e_cleanup(void);

static inline bool mlx5_sriov_is_enabled(struct mlx5_core_dev *dev)
{
	return pci_num_vf(dev->pdev) ? true : false;
}

static inline int mlx5_lag_is_lacp_owner(struct mlx5_core_dev *dev)
{
	/* LACP owner conditions:
	 * 1) Function is physical.
	 * 2) LAG is supported by FW.
	 * 3) LAG is managed by driver (currently the only option).
	 */
	return  MLX5_CAP_GEN(dev, vport_group_manager) &&
		   (MLX5_CAP_GEN(dev, num_lag_ports) > 1) &&
		    MLX5_CAP_GEN(dev, lag_master);
}

void mlx5_reload_interface(struct mlx5_core_dev *mdev, int protocol);
void mlx5_lag_update(struct mlx5_core_dev *dev);

enum {
	MLX5_NIC_IFC_FULL		= 0,
	MLX5_NIC_IFC_DISABLED		= 1,
	MLX5_NIC_IFC_NO_DRAM_NIC	= 2,
	MLX5_NIC_IFC_SW_RESET		= 7
};

u8 mlx5_get_nic_state(struct mlx5_core_dev *dev);
void mlx5_set_nic_state(struct mlx5_core_dev *dev, u8 state);

void mlx5_unload_one(struct mlx5_core_dev *dev, bool cleanup);
int mlx5_load_one(struct mlx5_core_dev *dev, bool boot);
#endif /* __MLX5_CORE_H__ */
