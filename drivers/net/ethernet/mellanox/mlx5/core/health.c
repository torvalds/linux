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
#include <linux/module.h>
#include <linux/random.h>
#include <linux/vmalloc.h>
#include <linux/hardirq.h>
#include <linux/mlx5/driver.h>
#include <linux/mlx5/cmd.h>
#include "mlx5_core.h"
#include "lib/eq.h"
#include "lib/mlx5.h"
#include "lib/pci_vsc.h"
#include "diag/fw_tracer.h"

enum {
	MLX5_HEALTH_POLL_INTERVAL	= 2 * HZ,
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

static bool sensor_fw_synd_rfr(struct mlx5_core_dev *dev)
{
	struct mlx5_core_health *health = &dev->priv.health;
	struct health_buffer __iomem *h = health->health;
	u32 rfr = ioread32be(&h->rfr) >> MLX5_RFR_OFFSET;
	u8 synd = ioread8(&h->synd);

	if (rfr && synd)
		mlx5_core_dbg(dev, "FW requests reset, synd: %d\n", synd);
	return rfr && synd;
}

static u32 check_fatal_sensors(struct mlx5_core_dev *dev)
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
	 * Check again to avoid a redundant 2nd reset. If the fatal erros was
	 * PCI related a reset won't help.
	 */
	fatal_error = check_fatal_sensors(dev);
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

void mlx5_enter_error_state(struct mlx5_core_dev *dev, bool force)
{
	mutex_lock(&dev->intf_state_mutex);
	if (dev->state == MLX5_DEVICE_STATE_INTERNAL_ERROR)
		goto unlock;
	if (dev->state == MLX5_DEVICE_STATE_UNINITIALIZED) {
		dev->state = MLX5_DEVICE_STATE_INTERNAL_ERROR;
		goto unlock;
	}

	if (check_fatal_sensors(dev) || force) {
		dev->state = MLX5_DEVICE_STATE_INTERNAL_ERROR;
		mlx5_cmd_flush(dev);
	}

	mlx5_notifier_call_chain(dev->priv.events, MLX5_DEV_EVENT_SYS_ERROR, (void *)1);
unlock:
	mutex_unlock(&dev->intf_state_mutex);
}

#define MLX5_CRDUMP_WAIT_MS	60000
#define MLX5_FW_RESET_WAIT_MS	1000
void mlx5_error_sw_reset(struct mlx5_core_dev *dev)
{
	unsigned long end, delay_ms = MLX5_FW_RESET_WAIT_MS;
	int lock = -EBUSY;

	mutex_lock(&dev->intf_state_mutex);
	if (dev->state != MLX5_DEVICE_STATE_INTERNAL_ERROR)
		goto unlock;

	mlx5_core_err(dev, "start\n");

	if (check_fatal_sensors(dev) == MLX5_SENSOR_FW_SYND_RFR) {
		/* Get cr-dump and reset FW semaphore */
		lock = lock_sem_sw_reset(dev, true);

		if (lock == -EBUSY) {
			delay_ms = MLX5_CRDUMP_WAIT_MS;
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

		cond_resched();
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

/* How much time to wait until health resetting the driver (in msecs) */
#define MLX5_RECOVERY_WAIT_MSECS 60000
static int mlx5_health_try_recover(struct mlx5_core_dev *dev)
{
	unsigned long end;

	mlx5_core_warn(dev, "handling bad device here\n");
	mlx5_handle_bad_state(dev);
	end = jiffies + msecs_to_jiffies(MLX5_RECOVERY_WAIT_MSECS);
	while (sensor_pci_not_working(dev)) {
		if (time_after(jiffies, end)) {
			mlx5_core_err(dev,
				      "health recovery flow aborted, PCI reads still not working\n");
			return -EIO;
		}
		msleep(100);
	}

	mlx5_core_err(dev, "starting health recovery flow\n");
	mlx5_recover_device(dev);
	if (!test_bit(MLX5_INTERFACE_STATE_UP, &dev->intf_state) ||
	    check_fatal_sensors(dev)) {
		mlx5_core_err(dev, "health recovery failed\n");
		return -EIO;
	}
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

static void print_health_info(struct mlx5_core_dev *dev)
{
	struct mlx5_core_health *health = &dev->priv.health;
	struct health_buffer __iomem *h = health->health;
	char fw_str[18];
	u32 fw;
	int i;

	/* If the syndrome is 0, the device is OK and no need to print buffer */
	if (!ioread8(&h->synd))
		return;

	for (i = 0; i < ARRAY_SIZE(h->assert_var); i++)
		mlx5_core_err(dev, "assert_var[%d] 0x%08x\n", i,
			      ioread32be(h->assert_var + i));

	mlx5_core_err(dev, "assert_exit_ptr 0x%08x\n",
		      ioread32be(&h->assert_exit_ptr));
	mlx5_core_err(dev, "assert_callra 0x%08x\n",
		      ioread32be(&h->assert_callra));
	sprintf(fw_str, "%d.%d.%d", fw_rev_maj(dev), fw_rev_min(dev), fw_rev_sub(dev));
	mlx5_core_err(dev, "fw_ver %s\n", fw_str);
	mlx5_core_err(dev, "hw_id 0x%08x\n", ioread32be(&h->hw_id));
	mlx5_core_err(dev, "irisc_index %d\n", ioread8(&h->irisc_index));
	mlx5_core_err(dev, "synd 0x%x: %s\n", ioread8(&h->synd),
		      hsynd_str(ioread8(&h->synd)));
	mlx5_core_err(dev, "ext_synd 0x%04x\n", ioread16be(&h->ext_synd));
	fw = ioread32be(&h->fw_ver);
	mlx5_core_err(dev, "raw fw_ver 0x%08x\n", fw);
}

static int
mlx5_fw_reporter_diagnose(struct devlink_health_reporter *reporter,
			  struct devlink_fmsg *fmsg)
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
	err = devlink_fmsg_u32_pair_put(fmsg, "hw_id", ioread32be(&h->hw_id));
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
		      struct devlink_fmsg *fmsg, void *priv_ctx)
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
				      "FW syndrom reported", &fw_reporter_ctx);
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
			       void *priv_ctx)
{
	struct mlx5_core_dev *dev = devlink_health_reporter_priv(reporter);

	return mlx5_health_try_recover(dev);
}

#define MLX5_CR_DUMP_CHUNK_SIZE 256
static int
mlx5_fw_fatal_reporter_dump(struct devlink_health_reporter *reporter,
			    struct devlink_fmsg *fmsg, void *priv_ctx)
{
	struct mlx5_core_dev *dev = devlink_health_reporter_priv(reporter);
	u32 crdump_size = dev->priv.health.crdump_size;
	u32 *cr_data;
	u32 data_size;
	u32 offset;
	int err;

	if (!mlx5_core_is_pf(dev))
		return -EPERM;

	cr_data = kvmalloc(crdump_size, GFP_KERNEL);
	if (!cr_data)
		return -ENOMEM;
	err = mlx5_crdump_collect(dev, cr_data);
	if (err)
		return err;

	if (priv_ctx) {
		struct mlx5_fw_reporter_ctx *fw_reporter_ctx = priv_ctx;

		err = mlx5_fw_reporter_ctx_pairs_put(fmsg, fw_reporter_ctx);
		if (err)
			goto free_data;
	}

	err = devlink_fmsg_arr_pair_nest_start(fmsg, "crdump_data");
	if (err)
		goto free_data;
	for (offset = 0; offset < crdump_size; offset += data_size) {
		if (crdump_size - offset < MLX5_CR_DUMP_CHUNK_SIZE)
			data_size = crdump_size - offset;
		else
			data_size = MLX5_CR_DUMP_CHUNK_SIZE;
		err = devlink_fmsg_binary_put(fmsg, cr_data, data_size);
		if (err)
			goto free_data;
	}
	err = devlink_fmsg_arr_pair_nest_end(fmsg);

free_data:
	kfree(cr_data);
	return err;
}

static void mlx5_fw_fatal_reporter_err_work(struct work_struct *work)
{
	struct mlx5_fw_reporter_ctx fw_reporter_ctx;
	struct mlx5_core_health *health;
	struct mlx5_core_dev *dev;
	struct mlx5_priv *priv;

	health = container_of(work, struct mlx5_core_health, fatal_report_work);
	priv = container_of(health, struct mlx5_priv, health);
	dev = container_of(priv, struct mlx5_core_dev, priv);

	mlx5_enter_error_state(dev, false);
	if (IS_ERR_OR_NULL(health->fw_fatal_reporter)) {
		if (mlx5_health_try_recover(dev))
			mlx5_core_err(dev, "health recovery failed\n");
		return;
	}
	fw_reporter_ctx.err_synd = health->synd;
	fw_reporter_ctx.miss_counter = health->miss_counter;
	devlink_health_report(health->fw_fatal_reporter,
			      "FW fatal error reported", &fw_reporter_ctx);
}

static const struct devlink_health_reporter_ops mlx5_fw_fatal_reporter_ops = {
		.name = "fw_fatal",
		.recover = mlx5_fw_fatal_reporter_recover,
		.dump = mlx5_fw_fatal_reporter_dump,
};

#define MLX5_REPORTER_FW_GRACEFUL_PERIOD 1200000
static void mlx5_fw_reporters_create(struct mlx5_core_dev *dev)
{
	struct mlx5_core_health *health = &dev->priv.health;
	struct devlink *devlink = priv_to_devlink(dev);

	health->fw_reporter =
		devlink_health_reporter_create(devlink, &mlx5_fw_reporter_ops,
					       0, false, dev);
	if (IS_ERR(health->fw_reporter))
		mlx5_core_warn(dev, "Failed to create fw reporter, err = %ld\n",
			       PTR_ERR(health->fw_reporter));

	health->fw_fatal_reporter =
		devlink_health_reporter_create(devlink,
					       &mlx5_fw_fatal_reporter_ops,
					       MLX5_REPORTER_FW_GRACEFUL_PERIOD,
					       true, dev);
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

static unsigned long get_next_poll_jiffies(void)
{
	unsigned long next;

	get_random_bytes(&next, sizeof(next));
	next %= HZ;
	next += jiffies + MLX5_HEALTH_POLL_INTERVAL;

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

	fatal_error = check_fatal_sensors(dev);

	if (fatal_error && !health->fatal_error) {
		mlx5_core_err(dev, "Fatal error %u detected\n", fatal_error);
		dev->priv.health.fatal_error = fatal_error;
		print_health_info(dev);
		mlx5_trigger_health_work(dev);
	}

out:
	mod_timer(&health->timer, get_next_poll_jiffies());
}

void mlx5_start_health_poll(struct mlx5_core_dev *dev)
{
	struct mlx5_core_health *health = &dev->priv.health;

	timer_setup(&health->timer, poll_health, 0);
	health->fatal_error = MLX5_SENSOR_NO_ERR;
	clear_bit(MLX5_DROP_NEW_HEALTH_WORK, &health->flags);
	health->health = &dev->iseg->health;
	health->health_counter = &dev->iseg->health_counter;

	health->timer.expires = round_jiffies(jiffies + MLX5_HEALTH_POLL_INTERVAL);
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

void mlx5_drain_health_wq(struct mlx5_core_dev *dev)
{
	struct mlx5_core_health *health = &dev->priv.health;
	unsigned long flags;

	spin_lock_irqsave(&health->wq_lock, flags);
	set_bit(MLX5_DROP_NEW_HEALTH_WORK, &health->flags);
	spin_unlock_irqrestore(&health->wq_lock, flags);
	cancel_work_sync(&health->report_work);
	cancel_work_sync(&health->fatal_report_work);
}

void mlx5_health_flush(struct mlx5_core_dev *dev)
{
	struct mlx5_core_health *health = &dev->priv.health;

	flush_workqueue(health->wq);
}

void mlx5_health_cleanup(struct mlx5_core_dev *dev)
{
	struct mlx5_core_health *health = &dev->priv.health;

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

	return 0;

out_err:
	mlx5_fw_reporters_destroy(dev);
	return -ENOMEM;
}
