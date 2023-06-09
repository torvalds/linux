/*
 * Copyright (c) 2013-2015, Mellanox Technologies. All rights reserved.
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

#include <linux/kernel.h>
#include <linux/random.h>
#include <linux/vmalloc.h>
#include <linux/hardirq.h>
#include <linux/mlx5/driver.h>
#include <linux/kern_levels.h>
#include "mlx5_core.h"
#include "lib/eq.h"
#include "lib/mlx5.h"
#include "lib/pci_vsc.h"
#include "lib/tout.h"
#include "diag/fw_tracer.h"

enum {
	MAX_MISSES			= 3,
};

enum {
	MLX5_HEALTH_SYNDR_FW_ERR		= 0x1,
	MLX5_HEALTH_SYNDR_IRISC_ERR		= 0x7,
	MLX5_HEALTH_SYNDR_HW_UNRECOVERABLE_ERR	= 0x8,
	MLX5_HEALTH_SYNDR_CRC_ERR		= 0x9,
	MLX5_HEALTH_SYNDR_FETCH_PCI_ERR		= 0xa,
	MLX5_HEALTH_SYNDR_HW_FTL_ERR		= 0xb,
	MLX5_HEALTH_SYNDR_ASYNC_EQ_OVERRUN_ERR	= 0xc,
	MLX5_HEALTH_SYNDR_EQ_ERR		= 0xd,
	MLX5_HEALTH_SYNDR_EQ_INV		= 0xe,
	MLX5_HEALTH_SYNDR_FFSER_ERR		= 0xf,
	MLX5_HEALTH_SYNDR_HIGH_TEMP		= 0x10
};

enum {
	MLX5_DROP_NEW_HEALTH_WORK,
};

enum  {
	MLX5_SENSOR_NO_ERR		= 0,
	MLX5_SENSOR_PCI_COMM_ERR	= 1,
	MLX5_SENSOR_PCI_ERR		= 2,
	MLX5_SENSOR_NIC_DISABLED	= 3,
	MLX5_SENSOR_NIC_SW_RESET	= 4,
	MLX5_SENSOR_FW_SYND_RFR		= 5,
};

enum {
	MLX5_SEVERITY_MASK		= 0x7,
	MLX5_SEVERITY_VALID_MASK	= 0x8,
};

u8 mlx5_get_nic_state(struct mlx5_core_dev *dev)
{
	return (ioread32be(&dev->iseg->cmdq_addr_l_sz) >> 8) & 7;
}

void mlx5_set_nic_state(struct mlx5_core_dev *dev, u8 state)
{
	u32 cur_cmdq_addr_l_sz;

	cur_cmdq_addr_l_sz = ioread32be(&dev->iseg->cmdq_addr_l_sz);
	iowrite32be((cur_cmdq_addr_l_sz & 0xFFFFF000) |
		    state << MLX5_NIC_IFC_OFFSET,
		    &dev->iseg->cmdq_addr_l_sz);
}

static bool sensor_pci_not_working(struct mlx5_core_dev *dev)
{
	struct mlx5_core_health *health = &dev->priv.health;
	struct health_buffer __iomem *h = health->health;

	/* Offline PCI reads return 0xffffffff */
	return (ioread32be(&h->fw_ver) == 0xffffffff);
}

static int mlx5_health_get_rfr(u8 rfr_severity)
{
	return rfr_severity >> MLX5_RFR_BIT_OFFSET;
}

static bool sensor_fw_synd_rfr(struct mlx5_core_dev *dev)
{
	struct mlx5_core_health *health = &dev->priv.health;
	struct health_buffer __iomem *h = health->health;
	u8 synd = ioread8(&h->synd);
	u8 rfr;

	rfr = mlx5_health_get_rfr(ioread8(&h->rfr_severity));

	if (rfr && synd)
		mlx5_core_dbg(dev, "FW requests reset, synd: %d\n", synd);
	return rfr && synd;
}

u32 mlx5_health_check_fatal_sensors(struct mlx5_core_dev *dev)
{
	if (sensor_pci_not_working(dev))
		return MLX5_SENSOR_PCI_COMM_ERR;
	if (pci_channel_offline(dev->pdev))
		return MLX5_SENSOR_PCI_ERR;
	if (mlx5_get_nic_state(dev) == MLX5_NIC_IFC_DISABLED)
		return MLX5_SENSOR_NIC_DISABLED;
	if (mlx5_get_nic_state(dev) == MLX5_NIC_IFC_SW_RESET)
		return MLX5_SENSOR_NIC_SW_RESET;
	if (sensor_fw_synd_rfr(dev))
		return MLX5_SENSOR_FW_SYND_RFR;

	return MLX5_SENSOR_NO_ERR;
}

static int lock_sem_sw_reset(struct mlx5_core_dev *dev, bool lock)
{
	enum mlx5_vsc_state state;
	int ret;

	if (!mlx5_core_is_pf(dev))
		return -EBUSY;

	/* Try to lock GW access, this stage doesn't return
	 * EBUSY because locked GW does not mean that other PF
	 * already started the reset.
	 */
	ret = mlx5_vsc_gw_lock(dev);
	if (ret == -EBUSY)
		return -EINVAL;
	if (ret)
		return ret;

	state = lock ? MLX5_VSC_LOCK : MLX5_VSC_UNLOCK;
	/* At this stage, if the return status == EBUSY, then we know
	 * for sure that another PF started the reset, so don't allow
	 * another reset.
	 */
	ret = mlx5_vsc_sem_set_space(dev, MLX5_SEMAPHORE_SW_RESET, state);
	if (ret)
		mlx5_core_warn(dev, "Failed to lock SW reset semaphore\n");

	/* Unlock GW access */
	mlx5_vsc_gw_unlock(dev);

	return ret;
}

static bool reset_fw_if_needed(struct mlx5_core_dev *dev)
{
	bool supported = (ioread32be(&dev->iseg->initializing) >>
			  MLX5_FW_RESET_SUPPORTED_OFFSET) & 1;
	u32 fatal_error;

	if (!supported)
		return false;

	/* The reset only needs to be issued by one PF. The health buffer is
	 * shared between all functions, and will be cleared during a reset.
	 * Check again to avoid a redundant 2nd reset. If the fatal errors was
	 * PCI related a reset won't help.
	 */
	fatal_error = mlx5_health_check_fatal_sensors(dev);
	if (fatal_error == MLX5_SENSOR_PCI_COMM_ERR ||
	    fatal_error == MLX5_SENSOR_NIC_DISABLED ||
	    fatal_error == MLX5_SENSOR_NIC_SW_RESET) {
		mlx5_core_warn(dev, "Not issuing FW reset. Either it's already done or won't help.");
		return false;
	}

	mlx5_core_warn(dev, "Issuing FW Reset\n");
	/* Write the NIC interface field to initiate the reset, the command
	 * interface address also resides here, don't overwrite it.
	 */
	mlx5_set_nic_state(dev, MLX5_NIC_IFC_SW_RESET);

	return true;
}

static void enter_error_state(struct mlx5_core_dev *dev, bool force)
{
	if (mlx5_health_check_fatal_sensors(dev) || force) { /* protected state setting */
		dev->state = MLX5_DEVICE_STATE_INTERNAL_ERROR;
		mlx5_cmd_flush(dev);
	}

	mlx5_notifier_call_chain(dev->priv.events, MLX5_DEV_EVENT_SYS_ERROR, (void *)1);
}

void mlx5_enter_error_state(struct mlx5_core_dev *dev, bool force)
{
	bool err_detected = false;

	/* Mark the device as fatal in order to abort FW commands */
	if ((mlx5_health_check_fatal_sensors(dev) || force) &&
	    dev->state == MLX5_DEVICE_STATE_UP) {
		dev->state = MLX5_DEVICE_STATE_INTERNAL_ERROR;
		err_detected = true;
	}
	mutex_lock(&dev->intf_state_mutex);
	if (!err_detected && dev->state == MLX5_DEVICE_STATE_INTERNAL_ERROR)
		goto unlock;/* a previous error is still being handled */

	enter_error_state(dev, force);
unlock:
	mutex_unlock(&dev->intf_state_mutex);
}

void mlx5_error_sw_reset(struct mlx5_core_dev *dev)
{
	unsigned long end, delay_ms = mlx5_tout_ms(dev, PCI_TOGGLE);
	int lock = -EBUSY;

	mutex_lock(&dev->intf_state_mutex);
	if (dev->state != MLX5_DEVICE_STATE_INTERNAL_ERROR)
		goto unlock;

	mlx5_core_err(dev, "start\n");

	if (mlx5_health_check_fatal_sensors(dev) == MLX5_SENSOR_FW_SYND_RFR) {
		/* Get cr-dump and reset FW semaphore */
		lock = lock_sem_sw_reset(dev, true);

		if (lock == -EBUSY) {
			delay_ms = mlx5_tout_ms(dev, FULL_CRDUMP);
			goto recover_from_sw_reset;
		}
		/* Execute SW reset */
		reset_fw_if_needed(dev);
	}

recover_from_sw_reset:
	/* Recover from SW reset */
	end = jiffies + msecs_to_jiffies(delay_ms);
	do {
		if (mlx5_get_nic_state(dev) == MLX5_NIC_IFC_DISABLED)
			break;

		msleep(20);
	} while (!time_after(jiffies, end));

	if (mlx5_get_nic_state(dev) != MLX5_NIC_IFC_DISABLED) {
		dev_err(&dev->pdev->dev, "NIC IFC still %d after %lums.\n",
			mlx5_get_nic_state(dev), delay_ms);
	}

	/* Release FW semaphore if you are the lock owner */
	if (!lock)
		lock_sem_sw_reset(dev, false);

	mlx5_core_err(dev, "end\n");

unlock:
	mutex_unlock(&dev->intf_state_mutex);
}

static void mlx5_handle_bad_state(struct mlx5_core_dev *dev)
{
	u8 nic_interface = mlx5_get_nic_state(dev);

	switch (nic_interface) {
	case MLX5_NIC_IFC_FULL:
		mlx5_core_warn(dev, "Expected to see disabled NIC but it is full driver\n");
		break;

	case MLX5_NIC_IFC_DISABLED:
		mlx5_core_warn(dev, "starting teardown\n");
		break;

	case MLX5_NIC_IFC_NO_DRAM_NIC:
		mlx5_core_warn(dev, "Expected to see disabled NIC but it is no dram nic\n");
		break;

	case MLX5_NIC_IFC_SW_RESET:
		/* The IFC mode field is 3 bits, so it will read 0x7 in 2 cases:
		 * 1. PCI has been disabled (ie. PCI-AER, PF driver unloaded
		 *    and this is a VF), this is not recoverable by SW reset.
		 *    Logging of this is handled elsewhere.
		 * 2. FW reset has been issued by another function, driver can
		 *    be reloaded to recover after the mode switches to
		 *    MLX5_NIC_IFC_DISABLED.
		 */
		if (dev->priv.health.fatal_error != MLX5_SENSOR_PCI_COMM_ERR)
			mlx5_core_warn(dev, "NIC SW reset in progress\n");
		break;

	default:
		mlx5_core_warn(dev, "Expected to see disabled NIC but it is has invalid value %d\n",
			       nic_interface);
	}

	mlx5_disable_device(dev);
}

int mlx5_health_wait_pci_up(struct mlx5_core_dev *dev)
{
	unsigned long end;

	end = jiffies + msecs_to_jiffies(mlx5_tout_ms(dev, FW_RESET));
	while (sensor_pci_not_working(dev)) {
		if (time_after(jiffies, end))
			return -ETIMEDOUT;
		msleep(100);
	}
	return 0;
}

static int mlx5_health_try_recover(struct mlx5_core_dev *dev)
{
	mlx5_core_warn(dev, "handling bad device here\n");
	mlx5_handle_bad_state(dev);
	if (mlx5_health_wait_pci_up(dev)) {
		mlx5_core_err(dev, "health recovery flow aborted, PCI reads still not working\n");
		return -EIO;
	}
	mlx5_core_err(dev, "starting health recovery flow\n");
	if (mlx5_recover_device(dev) || mlx5_health_check_fatal_sensors(dev)) {
		mlx5_core_err(dev, "health recovery failed\n");
		return -EIO;
	}

	mlx5_core_info(dev, "health recovery succeeded\n");
	return 0;
}

static const char *hsynd_str(u8 synd)
{
	switch (synd) {
	case MLX5_HEALTH_SYNDR_FW_ERR:
		return "firmware internal error";
	case MLX5_HEALTH_SYNDR_IRISC_ERR:
		return "irisc not responding";
	case MLX5_HEALTH_SYNDR_HW_UNRECOVERABLE_ERR:
		return "unrecoverable hardware error";
	case MLX5_HEALTH_SYNDR_CRC_ERR:
		return "firmware CRC error";
	case MLX5_HEALTH_SYNDR_FETCH_PCI_ERR:
		return "ICM fetch PCI error";
	case MLX5_HEALTH_SYNDR_HW_FTL_ERR:
		return "HW fatal error\n";
	case MLX5_HEALTH_SYNDR_ASYNC_EQ_OVERRUN_ERR:
		return "async EQ buffer overrun";
	case MLX5_HEALTH_SYNDR_EQ_ERR:
		return "EQ error";
	case MLX5_HEALTH_SYNDR_EQ_INV:
		return "Invalid EQ referenced";
	case MLX5_HEALTH_SYNDR_FFSER_ERR:
		return "FFSER error";
	case MLX5_HEALTH_SYNDR_HIGH_TEMP:
		return "High temperature";
	default:
		return "unrecognized error";
	}
}

static const char *mlx5_loglevel_str(int level)
{
	switch (level) {
	case LOGLEVEL_EMERG:
		return "EMERGENCY";
	case LOGLEVEL_ALERT:
		return "ALERT";
	case LOGLEVEL_CRIT:
		return "CRITICAL";
	case LOGLEVEL_ERR:
		return "ERROR";
	case LOGLEVEL_WARNING:
		return "WARNING";
	case LOGLEVEL_NOTICE:
		return "NOTICE";
	case LOGLEVEL_INFO:
		return "INFO";
	case LOGLEVEL_DEBUG:
		return "DEBUG";
	}
	return "Unknown log level";
}

static int mlx5_health_get_severity(u8 rfr_severity)
{
	return rfr_severity & MLX5_SEVERITY_VALID_MASK ?
	       rfr_severity & MLX5_SEVERITY_MASK : LOGLEVEL_ERR;
}

static void print_health_info(struct mlx5_core_dev *dev)
{
	struct mlx5_core_health *health = &dev->priv.health;
	struct health_buffer __iomem *h = health->health;
	u8 rfr_severity;
	int severity;
	int i;

	/* If the syndrome is 0, the device is OK and no need to print buffer */
	if (!ioread8(&h->synd))
		return;

	if (ioread32be(&h->fw_ver) == 0xFFFFFFFF) {
		mlx5_log(dev, LOGLEVEL_ERR, "PCI slot is unavailable\n");
		return;
	}

	rfr_severity = ioread8(&h->rfr_severity);
	severity  = mlx5_health_get_severity(rfr_severity);
	mlx5_log(dev, severity, "Health issue observed, %s, severity(%d) %s:\n",
		 hsynd_str(ioread8(&h->synd)), severity, mlx5_loglevel_str(severity));

	for (i = 0; i < ARRAY_SIZE(h->assert_var); i++)
		mlx5_log(dev, severity, "assert_var[%d] 0x%08x\n", i,
			 ioread32be(h->assert_var + i));

	mlx5_log(dev, severity, "assert_exit_ptr 0x%08x\n", ioread32be(&h->assert_exit_ptr));
	mlx5_log(dev, severity, "assert_callra 0x%08x\n", ioread32be(&h->assert_callra));
	mlx5_log(dev, severity, "fw_ver %d.%d.%d", fw_rev_maj(dev), fw_rev_min(dev),
		 fw_rev_sub(dev));
	mlx5_log(dev, severity, "time %u\n", ioread32be(&h->time));
	mlx5_log(dev, severity, "hw_id 0x%08x\n", ioread32be(&h->hw_id));
	mlx5_log(dev, severity, "rfr %d\n", mlx5_health_get_rfr(rfr_severity));
	mlx5_log(dev, severity, "severity %d (%s)\n", severity, mlx5_loglevel_str(severity));
	mlx5_log(dev, severity, "irisc_index %d\n", ioread8(&h->irisc_index));
	mlx5_log(dev, severity, "synd 0x%x: %s\n", ioread8(&h->synd),
		 hsynd_str(ioread8(&h->synd)));
	mlx5_log(dev, severity, "ext_synd 0x%04x\n", ioread16be(&h->ext_synd));
	mlx5_log(dev, severity, "raw fw_ver 0x%08x\n", ioread32be(&h->fw_ver));
}

static int
mlx5_fw_reporter_diagnose(struct devlink_health_reporter *reporter,
			  struct devlink_fmsg *fmsg,
			  struct netlink_ext_ack *extack)
{
	struct mlx5_core_dev *dev = devlink_health_reporter_priv(reporter);
	struct mlx5_core_health *health = &dev->priv.health;
	struct health_buffer __iomem *h = health->health;
	u8 synd;
	int err;

	synd = ioread8(&h->synd);
	err = devlink_fmsg_u8_pair_put(fmsg, "Syndrome", synd);
	if (err || !synd)
		return err;
	return devlink_fmsg_string_pair_put(fmsg, "Description", hsynd_str(synd));
}

struct mlx5_fw_reporter_ctx {
	u8 err_synd;
	int miss_counter;
};

static int
mlx5_fw_reporter_ctx_pairs_put(struct devlink_fmsg *fmsg,
			       struct mlx5_fw_reporter_ctx *fw_reporter_ctx)
{
	int err;

	err = devlink_fmsg_u8_pair_put(fmsg, "syndrome",
				       fw_reporter_ctx->err_synd);
	if (err)
		return err;
	err = devlink_fmsg_u32_pair_put(fmsg, "fw_miss_counter",
					fw_reporter_ctx->miss_counter);
	if (err)
		return err;
	return 0;
}

static int
mlx5_fw_reporter_heath_buffer_data_put(struct mlx5_core_dev *dev,
				       struct devlink_fmsg *fmsg)
{
	struct mlx5_core_health *health = &dev->priv.health;
	struct health_buffer __iomem *h = health->health;
	u8 rfr_severity;
	int err;
	int i;

	if (!ioread8(&h->synd))
		return 0;

	err = devlink_fmsg_pair_nest_start(fmsg, "health buffer");
	if (err)
		return err;
	err = devlink_fmsg_obj_nest_start(fmsg);
	if (err)
		return err;
	err = devlink_fmsg_arr_pair_nest_start(fmsg, "assert_var");
	if (err)
		return err;

	for (i = 0; i < ARRAY_SIZE(h->assert_var); i++) {
		err = devlink_fmsg_u32_put(fmsg, ioread32be(h->assert_var + i));
		if (err)
			return err;
	}
	err = devlink_fmsg_arr_pair_nest_end(fmsg);
	if (err)
		return err;
	err = devlink_fmsg_u32_pair_put(fmsg, "assert_exit_ptr",
					ioread32be(&h->assert_exit_ptr));
	if (err)
		return err;
	err = devlink_fmsg_u32_pair_put(fmsg, "assert_callra",
					ioread32be(&h->assert_callra));
	if (err)
		return err;
	err = devlink_fmsg_u32_pair_put(fmsg, "time", ioread32be(&h->time));
	if (err)
		return err;
	err = devlink_fmsg_u32_pair_put(fmsg, "hw_id", ioread32be(&h->hw_id));
	if (err)
		return err;
	rfr_severity = ioread8(&h->rfr_severity);
	err = devlink_fmsg_u8_pair_put(fmsg, "rfr", mlx5_health_get_rfr(rfr_severity));
	if (err)
		return err;
	err = devlink_fmsg_u8_pair_put(fmsg, "severity", mlx5_health_get_severity(rfr_severity));
	if (err)
		return err;
	err = devlink_fmsg_u8_pair_put(fmsg, "irisc_index",
				       ioread8(&h->irisc_index));
	if (err)
		return err;
	err = devlink_fmsg_u8_pair_put(fmsg, "synd", ioread8(&h->synd));
	if (err)
		return err;
	err = devlink_fmsg_u32_pair_put(fmsg, "ext_synd",
					ioread16be(&h->ext_synd));
	if (err)
		return err;
	err = devlink_fmsg_u32_pair_put(fmsg, "raw_fw_ver",
					ioread32be(&h->fw_ver));
	if (err)
		return err;
	err = devlink_fmsg_obj_nest_end(fmsg);
	if (err)
		return err;
	return devlink_fmsg_pair_nest_end(fmsg);
}

static int
mlx5_fw_reporter_dump(struct devlink_health_reporter *reporter,
		      struct devlink_fmsg *fmsg, void *priv_ctx,
		      struct netlink_ext_ack *extack)
{
	struct mlx5_core_dev *dev = devlink_health_reporter_priv(reporter);
	int err;

	err = mlx5_fw_tracer_trigger_core_dump_general(dev);
	if (err)
		return err;

	if (priv_ctx) {
		struct mlx5_fw_reporter_ctx *fw_reporter_ctx = priv_ctx;

		err = mlx5_fw_reporter_ctx_pairs_put(fmsg, fw_reporter_ctx);
		if (err)
			return err;
	}

	err = mlx5_fw_reporter_heath_buffer_data_put(dev, fmsg);
	if (err)
		return err;
	return mlx5_fw_tracer_get_saved_traces_objects(dev->tracer, fmsg);
}

static void mlx5_fw_reporter_err_work(struct work_struct *work)
{
	struct mlx5_fw_reporter_ctx fw_reporter_ctx;
	struct mlx5_core_health *health;

	health = container_of(work, struct mlx5_core_health, report_work);

	if (IS_ERR_OR_NULL(health->fw_reporter))
		return;

	fw_reporter_ctx.err_synd = health->synd;
	fw_reporter_ctx.miss_counter = health->miss_counter;
	if (fw_reporter_ctx.err_synd) {
		devlink_health_report(health->fw_reporter,
				      "FW syndrome reported", &fw_reporter_ctx);
		return;
	}
	if (fw_reporter_ctx.miss_counter)
		devlink_health_report(health->fw_reporter,
				      "FW miss counter reported",
				      &fw_reporter_ctx);
}

static const struct devlink_health_reporter_ops mlx5_fw_reporter_ops = {
		.name = "fw",
		.diagnose = mlx5_fw_reporter_diagnose,
		.dump = mlx5_fw_reporter_dump,
};

static int
mlx5_fw_fatal_reporter_recover(struct devlink_health_reporter *reporter,
			       void *priv_ctx,
			       struct netlink_ext_ack *extack)
{
	struct mlx5_core_dev *dev = devlink_health_reporter_priv(reporter);

	return mlx5_health_try_recover(dev);
}

static int
mlx5_fw_fatal_reporter_dump(struct devlink_health_reporter *reporter,
			    struct devlink_fmsg *fmsg, void *priv_ctx,
			    struct netlink_ext_ack *extack)
{
	struct mlx5_core_dev *dev = devlink_health_reporter_priv(reporter);
	u32 crdump_size = dev->priv.health.crdump_size;
	u32 *cr_data;
	int err;

	if (!mlx5_core_is_pf(dev))
		return -EPERM;

	cr_data = kvmalloc(crdump_size, GFP_KERNEL);
	if (!cr_data)
		return -ENOMEM;
	err = mlx5_crdump_collect(dev, cr_data);
	if (err)
		goto free_data;

	if (priv_ctx) {
		struct mlx5_fw_reporter_ctx *fw_reporter_ctx = priv_ctx;

		err = mlx5_fw_reporter_ctx_pairs_put(fmsg, fw_reporter_ctx);
		if (err)
			goto free_data;
	}

	err = devlink_fmsg_binary_pair_put(fmsg, "crdump_data", cr_data, crdump_size);

free_data:
	kvfree(cr_data);
	return err;
}

static void mlx5_fw_fatal_reporter_err_work(struct work_struct *work)
{
	struct mlx5_fw_reporter_ctx fw_reporter_ctx;
	struct mlx5_core_health *health;
	struct mlx5_core_dev *dev;
	struct devlink *devlink;
	struct mlx5_priv *priv;

	health = container_of(work, struct mlx5_core_health, fatal_report_work);
	priv = container_of(health, struct mlx5_priv, health);
	dev = container_of(priv, struct mlx5_core_dev, priv);
	devlink = priv_to_devlink(dev);

	mutex_lock(&dev->intf_state_mutex);
	if (test_bit(MLX5_DROP_NEW_HEALTH_WORK, &health->flags)) {
		mlx5_core_err(dev, "health works are not permitted at this stage\n");
		mutex_unlock(&dev->intf_state_mutex);
		return;
	}
	mutex_unlock(&dev->intf_state_mutex);
	enter_error_state(dev, false);
	if (IS_ERR_OR_NULL(health->fw_fatal_reporter)) {
		devl_lock(devlink);
		if (mlx5_health_try_recover(dev))
			mlx5_core_err(dev, "health recovery failed\n");
		devl_unlock(devlink);
		return;
	}
	fw_reporter_ctx.err_synd = health->synd;
	fw_reporter_ctx.miss_counter = health->miss_counter;
	if (devlink_health_report(health->fw_fatal_reporter,
				  "FW fatal error reported", &fw_reporter_ctx) == -ECANCELED) {
		/* If recovery wasn't performed, due to grace period,
		 * unload the driver. This ensures that the driver
		 * closes all its resources and it is not subjected to
		 * requests from the kernel.
		 */
		mlx5_core_err(dev, "Driver is in error state. Unloading\n");
		mlx5_unload_one(dev, false);
	}
}

static const struct devlink_health_reporter_ops mlx5_fw_fatal_reporter_ops = {
		.name = "fw_fatal",
		.recover = mlx5_fw_fatal_reporter_recover,
		.dump = mlx5_fw_fatal_reporter_dump,
};

#define MLX5_FW_REPORTER_ECPF_GRACEFUL_PERIOD 180000
#define MLX5_FW_REPORTER_PF_GRACEFUL_PERIOD 60000
#define MLX5_FW_REPORTER_VF_GRACEFUL_PERIOD 30000
#define MLX5_FW_REPORTER_DEFAULT_GRACEFUL_PERIOD MLX5_FW_REPORTER_VF_GRACEFUL_PERIOD

static void mlx5_fw_reporters_create(struct mlx5_core_dev *dev)
{
	struct mlx5_core_health *health = &dev->priv.health;
	struct devlink *devlink = priv_to_devlink(dev);
	u64 grace_period;

	if (mlx5_core_is_ecpf(dev)) {
		grace_period = MLX5_FW_REPORTER_ECPF_GRACEFUL_PERIOD;
	} else if (mlx5_core_is_pf(dev)) {
		grace_period = MLX5_FW_REPORTER_PF_GRACEFUL_PERIOD;
	} else {
		/* VF or SF */
		grace_period = MLX5_FW_REPORTER_DEFAULT_GRACEFUL_PERIOD;
	}

	health->fw_reporter =
		devlink_health_reporter_create(devlink, &mlx5_fw_reporter_ops,
					       0, dev);
	if (IS_ERR(health->fw_reporter))
		mlx5_core_warn(dev, "Failed to create fw reporter, err = %ld\n",
			       PTR_ERR(health->fw_reporter));

	health->fw_fatal_reporter =
		devlink_health_reporter_create(devlink,
					       &mlx5_fw_fatal_reporter_ops,
					       grace_period,
					       dev);
	if (IS_ERR(health->fw_fatal_reporter))
		mlx5_core_warn(dev, "Failed to create fw fatal reporter, err = %ld\n",
			       PTR_ERR(health->fw_fatal_reporter));
}

static void mlx5_fw_reporters_destroy(struct mlx5_core_dev *dev)
{
	struct mlx5_core_health *health = &dev->priv.health;

	if (!IS_ERR_OR_NULL(health->fw_reporter))
		devlink_health_reporter_destroy(health->fw_reporter);

	if (!IS_ERR_OR_NULL(health->fw_fatal_reporter))
		devlink_health_reporter_destroy(health->fw_fatal_reporter);
}

static unsigned long get_next_poll_jiffies(struct mlx5_core_dev *dev)
{
	unsigned long next;

	get_random_bytes(&next, sizeof(next));
	next %= HZ;
	next += jiffies + msecs_to_jiffies(mlx5_tout_ms(dev, HEALTH_POLL_INTERVAL));

	return next;
}

void mlx5_trigger_health_work(struct mlx5_core_dev *dev)
{
	struct mlx5_core_health *health = &dev->priv.health;
	unsigned long flags;

	spin_lock_irqsave(&health->wq_lock, flags);
	if (!test_bit(MLX5_DROP_NEW_HEALTH_WORK, &health->flags))
		queue_work(health->wq, &health->fatal_report_work);
	else
		mlx5_core_err(dev, "new health works are not permitted at this stage\n");
	spin_unlock_irqrestore(&health->wq_lock, flags);
}

#define MLX5_MSEC_PER_HOUR (MSEC_PER_SEC * 60 * 60)
static void mlx5_health_log_ts_update(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	u32 out[MLX5_ST_SZ_DW(mrtc_reg)] = {};
	u32 in[MLX5_ST_SZ_DW(mrtc_reg)] = {};
	struct mlx5_core_health *health;
	struct mlx5_core_dev *dev;
	struct mlx5_priv *priv;
	u64 now_us;

	health = container_of(dwork, struct mlx5_core_health, update_fw_log_ts_work);
	priv = container_of(health, struct mlx5_priv, health);
	dev = container_of(priv, struct mlx5_core_dev, priv);

	now_us =  ktime_to_us(ktime_get_real());

	MLX5_SET(mrtc_reg, in, time_h, now_us >> 32);
	MLX5_SET(mrtc_reg, in, time_l, now_us & 0xFFFFFFFF);
	mlx5_core_access_reg(dev, in, sizeof(in), out, sizeof(out), MLX5_REG_MRTC, 0, 1);

	queue_delayed_work(health->wq, &health->update_fw_log_ts_work,
			   msecs_to_jiffies(MLX5_MSEC_PER_HOUR));
}

static void poll_health(struct timer_list *t)
{
	struct mlx5_core_dev *dev = from_timer(dev, t, priv.health.timer);
	struct mlx5_core_health *health = &dev->priv.health;
	struct health_buffer __iomem *h = health->health;
	u32 fatal_error;
	u8 prev_synd;
	u32 count;

	if (dev->state == MLX5_DEVICE_STATE_INTERNAL_ERROR)
		goto out;

	fatal_error = mlx5_health_check_fatal_sensors(dev);

	if (fatal_error && !health->fatal_error) {
		mlx5_core_err(dev, "Fatal error %u detected\n", fatal_error);
		dev->priv.health.fatal_error = fatal_error;
		print_health_info(dev);
		dev->state = MLX5_DEVICE_STATE_INTERNAL_ERROR;
		mlx5_trigger_health_work(dev);
		return;
	}

	count = ioread32be(health->health_counter);
	if (count == health->prev)
		++health->miss_counter;
	else
		health->miss_counter = 0;

	health->prev = count;
	if (health->miss_counter == MAX_MISSES) {
		mlx5_core_err(dev, "device's health compromised - reached miss count\n");
		print_health_info(dev);
		queue_work(health->wq, &health->report_work);
	}

	prev_synd = health->synd;
	health->synd = ioread8(&h->synd);
	if (health->synd && health->synd != prev_synd)
		queue_work(health->wq, &health->report_work);

out:
	mod_timer(&health->timer, get_next_poll_jiffies(dev));
}

void mlx5_start_health_poll(struct mlx5_core_dev *dev)
{
	u64 poll_interval_ms =  mlx5_tout_ms(dev, HEALTH_POLL_INTERVAL);
	struct mlx5_core_health *health = &dev->priv.health;

	timer_setup(&health->timer, poll_health, 0);
	health->fatal_error = MLX5_SENSOR_NO_ERR;
	clear_bit(MLX5_DROP_NEW_HEALTH_WORK, &health->flags);
	health->health = &dev->iseg->health;
	health->health_counter = &dev->iseg->health_counter;

	health->timer.expires = jiffies + msecs_to_jiffies(poll_interval_ms);
	add_timer(&health->timer);
}

void mlx5_stop_health_poll(struct mlx5_core_dev *dev, bool disable_health)
{
	struct mlx5_core_health *health = &dev->priv.health;
	unsigned long flags;

	if (disable_health) {
		spin_lock_irqsave(&health->wq_lock, flags);
		set_bit(MLX5_DROP_NEW_HEALTH_WORK, &health->flags);
		spin_unlock_irqrestore(&health->wq_lock, flags);
	}

	del_timer_sync(&health->timer);
}

void mlx5_start_health_fw_log_up(struct mlx5_core_dev *dev)
{
	struct mlx5_core_health *health = &dev->priv.health;

	if (mlx5_core_is_pf(dev) && MLX5_CAP_MCAM_REG(dev, mrtc))
		queue_delayed_work(health->wq, &health->update_fw_log_ts_work, 0);
}

void mlx5_drain_health_wq(struct mlx5_core_dev *dev)
{
	struct mlx5_core_health *health = &dev->priv.health;
	unsigned long flags;

	spin_lock_irqsave(&health->wq_lock, flags);
	set_bit(MLX5_DROP_NEW_HEALTH_WORK, &health->flags);
	spin_unlock_irqrestore(&health->wq_lock, flags);
	cancel_delayed_work_sync(&health->update_fw_log_ts_work);
	cancel_work_sync(&health->report_work);
	cancel_work_sync(&health->fatal_report_work);
}

void mlx5_health_cleanup(struct mlx5_core_dev *dev)
{
	struct mlx5_core_health *health = &dev->priv.health;

	cancel_delayed_work_sync(&health->update_fw_log_ts_work);
	destroy_workqueue(health->wq);
	mlx5_fw_reporters_destroy(dev);
}

int mlx5_health_init(struct mlx5_core_dev *dev)
{
	struct mlx5_core_health *health;
	char *name;

	mlx5_fw_reporters_create(dev);

	health = &dev->priv.health;
	name = kmalloc(64, GFP_KERNEL);
	if (!name)
		goto out_err;

	strcpy(name, "mlx5_health");
	strcat(name, dev_name(dev->device));
	health->wq = create_singlethread_workqueue(name);
	kfree(name);
	if (!health->wq)
		goto out_err;
	spin_lock_init(&health->wq_lock);
	INIT_WORK(&health->fatal_report_work, mlx5_fw_fatal_reporter_err_work);
	INIT_WORK(&health->report_work, mlx5_fw_reporter_err_work);
	INIT_DELAYED_WORK(&health->update_fw_log_ts_work, mlx5_health_log_ts_update);

	return 0;

out_err:
	mlx5_fw_reporters_destroy(dev);
	return -ENOMEM;
}
