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
#include "lib/devcom.h"

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

#define ACCESS_KEY_LEN  32
#define FT_ID_FT_TYPE_OFFSET 24

struct mlx5_cmd_allow_other_vhca_access_attr {
	u16 obj_type;
	u32 obj_id;
	u8 access_key[ACCESS_KEY_LEN];
};

struct mlx5_cmd_alias_obj_create_attr {
	u32 obj_id;
	u16 vhca_id;
	u16 obj_type;
	u8 access_key[ACCESS_KEY_LEN];
};

static inline void mlx5_printk(struct mlx5_core_dev *dev, int level, const char *format, ...)
{
	struct device *device = dev->device;
	struct va_format vaf;
	va_list args;

	if (WARN_ONCE(level < LOGLEVEL_EMERG || level > LOGLEVEL_DEBUG,
		      "Level %d is out of range, set to default level\n", level))
		level = LOGLEVEL_DEFAULT;

	va_start(args, format);
	vaf.fmt = format;
	vaf.va = &args;

	dev_printk_emit(level, device, "%s %s: %pV", dev_driver_string(device), dev_name(device),
			&vaf);
	va_end(args);
}

#define mlx5_log(__dev, level, format, ...)			\
	mlx5_printk(__dev, level, "%s:%d:(pid %d): " format,	\
		    __func__, __LINE__, current->pid,		\
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

#define MLX5_DEFAULT_PROF       2
#define MLX5_SF_PROF		3
#define MLX5_NUM_FW_CMD_THREADS 8
#define MLX5_DEV_MAX_WQS	MLX5_NUM_FW_CMD_THREADS

static inline int mlx5_flexible_inlen(struct mlx5_core_dev *dev, size_t fixed,
				      size_t item_size, size_t num_items,
				      const char *func, int line)
{
	int inlen;

	if (fixed > INT_MAX || item_size > INT_MAX || num_items > INT_MAX) {
		mlx5_core_err(dev, "%s: %s:%d: input values too big: %zu + %zu * %zu\n",
			      __func__, func, line, fixed, item_size, num_items);
		return -ENOMEM;
	}

	if (check_mul_overflow((int)item_size, (int)num_items, &inlen)) {
		mlx5_core_err(dev, "%s: %s:%d: multiplication overflow: %zu + %zu * %zu\n",
			      __func__, func, line, fixed, item_size, num_items);
		return -ENOMEM;
	}

	if (check_add_overflow((int)fixed, inlen, &inlen)) {
		mlx5_core_err(dev, "%s: %s:%d: addition overflow: %zu + %zu * %zu\n",
			      __func__, func, line, fixed, item_size, num_items);
		return -ENOMEM;
	}

	return inlen;
}

#define MLX5_FLEXIBLE_INLEN(dev, fixed, item_size, num_items) \
	mlx5_flexible_inlen(dev, fixed, item_size, num_items, __func__, __LINE__)

int mlx5_core_get_caps(struct mlx5_core_dev *dev, enum mlx5_cap_type cap_type);
int mlx5_core_get_caps_mode(struct mlx5_core_dev *dev, enum mlx5_cap_type cap_type,
			    enum mlx5_cap_mode cap_mode);
int mlx5_query_hca_caps(struct mlx5_core_dev *dev);
int mlx5_query_board_id(struct mlx5_core_dev *dev);
int mlx5_query_module_num(struct mlx5_core_dev *dev, int *module_num);
int mlx5_cmd_init(struct mlx5_core_dev *dev);
void mlx5_cmd_cleanup(struct mlx5_core_dev *dev);
int mlx5_cmd_enable(struct mlx5_core_dev *dev);
void mlx5_cmd_disable(struct mlx5_core_dev *dev);
void mlx5_cmd_set_state(struct mlx5_core_dev *dev,
			enum mlx5_cmdif_state cmdif_state);
int mlx5_cmd_init_hca(struct mlx5_core_dev *dev, u32 *sw_owner_id);
int mlx5_cmd_teardown_hca(struct mlx5_core_dev *dev);
int mlx5_cmd_force_teardown_hca(struct mlx5_core_dev *dev);
int mlx5_cmd_fast_teardown_hca(struct mlx5_core_dev *dev);
void mlx5_enter_error_state(struct mlx5_core_dev *dev, bool force);
void mlx5_error_sw_reset(struct mlx5_core_dev *dev);
u32 mlx5_health_check_fatal_sensors(struct mlx5_core_dev *dev);
int mlx5_health_wait_pci_up(struct mlx5_core_dev *dev);
void mlx5_disable_device(struct mlx5_core_dev *dev);
int mlx5_recover_device(struct mlx5_core_dev *dev);
int mlx5_sriov_init(struct mlx5_core_dev *dev);
void mlx5_sriov_cleanup(struct mlx5_core_dev *dev);
int mlx5_sriov_attach(struct mlx5_core_dev *dev);
void mlx5_sriov_detach(struct mlx5_core_dev *dev);
int mlx5_core_sriov_configure(struct pci_dev *dev, int num_vfs);
void mlx5_sriov_disable(struct pci_dev *pdev, bool num_vf_change);
int mlx5_core_sriov_set_msix_vec_count(struct pci_dev *vf, int msix_vec_count);
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

void mlx5_cmd_flush(struct mlx5_core_dev *dev);
void mlx5_cq_debugfs_init(struct mlx5_core_dev *dev);
void mlx5_cq_debugfs_cleanup(struct mlx5_core_dev *dev);

int mlx5_query_pcam_reg(struct mlx5_core_dev *dev, u32 *pcam, u8 feature_group,
			u8 access_reg_group);
int mlx5_query_mcam_reg(struct mlx5_core_dev *dev, u32 *mcap, u8 feature_group,
			u8 access_reg_group);
int mlx5_query_qcam_reg(struct mlx5_core_dev *mdev, u32 *qcam,
			u8 feature_group, u8 access_reg_group);
int mlx5_query_mpir_reg(struct mlx5_core_dev *dev, u32 *mpir);

void mlx5_lag_add_netdev(struct mlx5_core_dev *dev, struct net_device *netdev);
void mlx5_lag_remove_netdev(struct mlx5_core_dev *dev, struct net_device *netdev);
void mlx5_lag_add_mdev(struct mlx5_core_dev *dev);
void mlx5_lag_remove_mdev(struct mlx5_core_dev *dev);
void mlx5_lag_disable_change(struct mlx5_core_dev *dev);
void mlx5_lag_enable_change(struct mlx5_core_dev *dev);

int mlx5_events_init(struct mlx5_core_dev *dev);
void mlx5_events_cleanup(struct mlx5_core_dev *dev);
void mlx5_events_start(struct mlx5_core_dev *dev);
void mlx5_events_stop(struct mlx5_core_dev *dev);

int mlx5_adev_idx_alloc(void);
void mlx5_adev_idx_free(int idx);
void mlx5_adev_cleanup(struct mlx5_core_dev *dev);
int mlx5_adev_init(struct mlx5_core_dev *dev);

int mlx5_attach_device(struct mlx5_core_dev *dev);
void mlx5_detach_device(struct mlx5_core_dev *dev, bool suspend);
int mlx5_register_device(struct mlx5_core_dev *dev);
void mlx5_unregister_device(struct mlx5_core_dev *dev);
void mlx5_dev_set_lightweight(struct mlx5_core_dev *dev);
bool mlx5_dev_is_lightweight(struct mlx5_core_dev *dev);

void mlx5_fw_reporters_create(struct mlx5_core_dev *dev);
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

#ifdef CONFIG_MLX5_CORE_EN
int mlx5e_init(void);
void mlx5e_cleanup(void);
#else
static inline int mlx5e_init(void){ return 0; }
static inline void mlx5e_cleanup(void){}
#endif

static inline bool mlx5_sriov_is_enabled(struct mlx5_core_dev *dev)
{
	return pci_num_vf(dev->pdev) ? true : false;
}

int mlx5_rescan_drivers_locked(struct mlx5_core_dev *dev);
static inline int mlx5_rescan_drivers(struct mlx5_core_dev *dev)
{
	int ret;

	mlx5_devcom_comp_lock(dev->priv.hca_devcom_comp);
	ret = mlx5_rescan_drivers_locked(dev);
	mlx5_devcom_comp_unlock(dev->priv.hca_devcom_comp);
	return ret;
}

u8 mlx5_get_nic_state(struct mlx5_core_dev *dev);
void mlx5_set_nic_state(struct mlx5_core_dev *dev, u8 state);

static inline bool mlx5_core_is_sf(const struct mlx5_core_dev *dev)
{
	return dev->coredev_type == MLX5_COREDEV_SF;
}

static inline struct auxiliary_device *
mlx5_sf_coredev_to_adev(struct mlx5_core_dev *mdev)
{
	return container_of(mdev->device, struct auxiliary_device, dev);
}

int mlx5_mdev_init(struct mlx5_core_dev *dev, int profile_idx);
void mlx5_mdev_uninit(struct mlx5_core_dev *dev);
int mlx5_init_one(struct mlx5_core_dev *dev);
int mlx5_init_one_devl_locked(struct mlx5_core_dev *dev);
void mlx5_uninit_one(struct mlx5_core_dev *dev);
void mlx5_unload_one(struct mlx5_core_dev *dev, bool suspend);
void mlx5_unload_one_devl_locked(struct mlx5_core_dev *dev, bool suspend);
int mlx5_load_one(struct mlx5_core_dev *dev, bool recovery);
int mlx5_load_one_devl_locked(struct mlx5_core_dev *dev, bool recovery);
int mlx5_init_one_light(struct mlx5_core_dev *dev);
void mlx5_uninit_one_light(struct mlx5_core_dev *dev);
void mlx5_unload_one_light(struct mlx5_core_dev *dev);

int mlx5_vport_set_other_func_cap(struct mlx5_core_dev *dev, const void *hca_cap, u16 vport,
				  u16 opmod);
#define mlx5_vport_get_other_func_general_cap(dev, vport, out)		\
	mlx5_vport_get_other_func_cap(dev, vport, out, MLX5_CAP_GENERAL)

static inline u32 mlx5_sriov_get_vf_total_msix(struct pci_dev *pdev)
{
	struct mlx5_core_dev *dev = pci_get_drvdata(pdev);

	return MLX5_CAP_GEN_MAX(dev, num_total_dynamic_vf_msix);
}

bool mlx5_eth_supported(struct mlx5_core_dev *dev);
bool mlx5_rdma_supported(struct mlx5_core_dev *dev);
bool mlx5_vnet_supported(struct mlx5_core_dev *dev);
bool mlx5_same_hw_devs(struct mlx5_core_dev *dev, struct mlx5_core_dev *peer_dev);
int mlx5_cmd_allow_other_vhca_access(struct mlx5_core_dev *dev,
				     struct mlx5_cmd_allow_other_vhca_access_attr *attr);
int mlx5_cmd_alias_obj_create(struct mlx5_core_dev *dev,
			      struct mlx5_cmd_alias_obj_create_attr *alias_attr,
			      u32 *obj_id);
int mlx5_cmd_alias_obj_destroy(struct mlx5_core_dev *dev, u32 obj_id, u16 obj_type);

static inline u16 mlx5_core_ec_vf_vport_base(const struct mlx5_core_dev *dev)
{
	return MLX5_CAP_GEN_2(dev, ec_vf_vport_base);
}

static inline u16 mlx5_core_ec_sriov_enabled(const struct mlx5_core_dev *dev)
{
	return mlx5_core_is_ecpf(dev) && mlx5_core_ec_vf_vport_base(dev);
}

static inline bool mlx5_core_is_ec_vf_vport(const struct mlx5_core_dev *dev, u16 vport_num)
{
	int base_vport = mlx5_core_ec_vf_vport_base(dev);
	int max_vport = base_vport + mlx5_core_max_ec_vfs(dev);

	if (!mlx5_core_ec_sriov_enabled(dev))
		return false;

	return (vport_num >= base_vport && vport_num < max_vport);
}

static inline int mlx5_vport_to_func_id(const struct mlx5_core_dev *dev, u16 vport, bool ec_vf_func)
{
	return ec_vf_func ? vport - mlx5_core_ec_vf_vport_base(dev) + 1
			  : vport;
}

static inline int mlx5_max_eq_cap_get(const struct mlx5_core_dev *dev)
{
	if (MLX5_CAP_GEN_2(dev, max_num_eqs_24b))
		return MLX5_CAP_GEN_2(dev, max_num_eqs_24b);

	if (MLX5_CAP_GEN(dev, max_num_eqs))
		return MLX5_CAP_GEN(dev, max_num_eqs);

	return 1 << MLX5_CAP_GEN(dev, log_max_eq);
}
#endif /* __MLX5_CORE_H__ */
