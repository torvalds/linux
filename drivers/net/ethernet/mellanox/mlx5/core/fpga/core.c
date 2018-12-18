/*
 * Copyright (c) 2017, Mellanox Technologies. All rights reserved.
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
#include <linux/etherdevice.h>
#include <linux/mlx5/driver.h>

#include "mlx5_core.h"
#include "lib/mlx5.h"
#include "fpga/core.h"
#include "fpga/conn.h"

static const char *const mlx5_fpga_error_strings[] = {
	"Null Syndrome",
	"Corrupted DDR",
	"Flash Timeout",
	"Internal Link Error",
	"Watchdog HW Failure",
	"I2C Failure",
	"Image Changed",
	"Temperature Critical",
};

static const char * const mlx5_fpga_qp_error_strings[] = {
	"Null Syndrome",
	"Retry Counter Expired",
	"RNR Expired",
};
static struct mlx5_fpga_device *mlx5_fpga_device_alloc(void)
{
	struct mlx5_fpga_device *fdev = NULL;

	fdev = kzalloc(sizeof(*fdev), GFP_KERNEL);
	if (!fdev)
		return NULL;

	spin_lock_init(&fdev->state_lock);
	fdev->state = MLX5_FPGA_STATUS_NONE;
	return fdev;
}

static const char *mlx5_fpga_image_name(enum mlx5_fpga_image image)
{
	switch (image) {
	case MLX5_FPGA_IMAGE_USER:
		return "user";
	case MLX5_FPGA_IMAGE_FACTORY:
		return "factory";
	default:
		return "unknown";
	}
}

static const char *mlx5_fpga_device_name(u32 device)
{
	switch (device) {
	case MLX5_FPGA_DEVICE_KU040:
		return "ku040";
	case MLX5_FPGA_DEVICE_KU060:
		return "ku060";
	case MLX5_FPGA_DEVICE_KU060_2:
		return "ku060_2";
	case MLX5_FPGA_DEVICE_UNKNOWN:
	default:
		return "unknown";
	}
}

static int mlx5_fpga_device_load_check(struct mlx5_fpga_device *fdev)
{
	struct mlx5_fpga_query query;
	int err;

	err = mlx5_fpga_query(fdev->mdev, &query);
	if (err) {
		mlx5_fpga_err(fdev, "Failed to query status: %d\n", err);
		return err;
	}

	fdev->last_admin_image = query.admin_image;
	fdev->last_oper_image = query.oper_image;

	mlx5_fpga_dbg(fdev, "Status %u; Admin image %u; Oper image %u\n",
		      query.status, query.admin_image, query.oper_image);

	if (query.status != MLX5_FPGA_STATUS_SUCCESS) {
		mlx5_fpga_err(fdev, "%s image failed to load; status %u\n",
			      mlx5_fpga_image_name(fdev->last_oper_image),
			      query.status);
		return -EIO;
	}

	return 0;
}

static int mlx5_fpga_device_brb(struct mlx5_fpga_device *fdev)
{
	int err;
	struct mlx5_core_dev *mdev = fdev->mdev;

	err = mlx5_fpga_ctrl_op(mdev, MLX5_FPGA_CTRL_OPERATION_SANDBOX_BYPASS_ON);
	if (err) {
		mlx5_fpga_err(fdev, "Failed to set bypass on: %d\n", err);
		return err;
	}
	err = mlx5_fpga_ctrl_op(mdev, MLX5_FPGA_CTRL_OPERATION_RESET_SANDBOX);
	if (err) {
		mlx5_fpga_err(fdev, "Failed to reset SBU: %d\n", err);
		return err;
	}
	err = mlx5_fpga_ctrl_op(mdev, MLX5_FPGA_CTRL_OPERATION_SANDBOX_BYPASS_OFF);
	if (err) {
		mlx5_fpga_err(fdev, "Failed to set bypass off: %d\n", err);
		return err;
	}
	return 0;
}

int mlx5_fpga_device_start(struct mlx5_core_dev *mdev)
{
	struct mlx5_fpga_device *fdev = mdev->fpga;
	unsigned int max_num_qps;
	unsigned long flags;
	u32 fpga_device_id;
	int err;

	if (!fdev)
		return 0;

	err = mlx5_fpga_device_load_check(fdev);
	if (err)
		goto out;

	err = mlx5_fpga_caps(fdev->mdev);
	if (err)
		goto out;

	fpga_device_id = MLX5_CAP_FPGA(fdev->mdev, fpga_device);
	mlx5_fpga_info(fdev, "%s:%u; %s image, version %u; SBU %06x:%04x version %d\n",
		       mlx5_fpga_device_name(fpga_device_id),
		       fpga_device_id,
		       mlx5_fpga_image_name(fdev->last_oper_image),
		       MLX5_CAP_FPGA(fdev->mdev, image_version),
		       MLX5_CAP_FPGA(fdev->mdev, ieee_vendor_id),
		       MLX5_CAP_FPGA(fdev->mdev, sandbox_product_id),
		       MLX5_CAP_FPGA(fdev->mdev, sandbox_product_version));

	max_num_qps = MLX5_CAP_FPGA(mdev, shell_caps.max_num_qps);
	if (!max_num_qps) {
		mlx5_fpga_err(fdev, "FPGA reports 0 QPs in SHELL_CAPS\n");
		err = -ENOTSUPP;
		goto out;
	}

	err = mlx5_core_reserve_gids(mdev, max_num_qps);
	if (err)
		goto out;

	err = mlx5_fpga_conn_device_init(fdev);
	if (err)
		goto err_rsvd_gid;

	if (fdev->last_oper_image == MLX5_FPGA_IMAGE_USER) {
		err = mlx5_fpga_device_brb(fdev);
		if (err)
			goto err_conn_init;
	}

	goto out;

err_conn_init:
	mlx5_fpga_conn_device_cleanup(fdev);

err_rsvd_gid:
	mlx5_core_unreserve_gids(mdev, max_num_qps);
out:
	spin_lock_irqsave(&fdev->state_lock, flags);
	fdev->state = err ? MLX5_FPGA_STATUS_FAILURE : MLX5_FPGA_STATUS_SUCCESS;
	spin_unlock_irqrestore(&fdev->state_lock, flags);
	return err;
}

int mlx5_fpga_init(struct mlx5_core_dev *mdev)
{
	struct mlx5_fpga_device *fdev = NULL;

	if (!MLX5_CAP_GEN(mdev, fpga)) {
		mlx5_core_dbg(mdev, "FPGA capability not present\n");
		return 0;
	}

	mlx5_core_dbg(mdev, "Initializing FPGA\n");

	fdev = mlx5_fpga_device_alloc();
	if (!fdev)
		return -ENOMEM;

	fdev->mdev = mdev;
	mdev->fpga = fdev;

	return 0;
}

void mlx5_fpga_device_stop(struct mlx5_core_dev *mdev)
{
	struct mlx5_fpga_device *fdev = mdev->fpga;
	unsigned int max_num_qps;
	unsigned long flags;
	int err;

	if (!fdev)
		return;

	spin_lock_irqsave(&fdev->state_lock, flags);
	if (fdev->state != MLX5_FPGA_STATUS_SUCCESS) {
		spin_unlock_irqrestore(&fdev->state_lock, flags);
		return;
	}
	fdev->state = MLX5_FPGA_STATUS_NONE;
	spin_unlock_irqrestore(&fdev->state_lock, flags);

	if (fdev->last_oper_image == MLX5_FPGA_IMAGE_USER) {
		err = mlx5_fpga_ctrl_op(mdev, MLX5_FPGA_CTRL_OPERATION_SANDBOX_BYPASS_ON);
		if (err)
			mlx5_fpga_err(fdev, "Failed to re-set SBU bypass on: %d\n",
				      err);
	}

	mlx5_fpga_conn_device_cleanup(fdev);
	max_num_qps = MLX5_CAP_FPGA(mdev, shell_caps.max_num_qps);
	mlx5_core_unreserve_gids(mdev, max_num_qps);
}

void mlx5_fpga_cleanup(struct mlx5_core_dev *mdev)
{
	struct mlx5_fpga_device *fdev = mdev->fpga;

	mlx5_fpga_device_stop(mdev);
	kfree(fdev);
	mdev->fpga = NULL;
}

static const char *mlx5_fpga_syndrome_to_string(u8 syndrome)
{
	if (syndrome < ARRAY_SIZE(mlx5_fpga_error_strings))
		return mlx5_fpga_error_strings[syndrome];
	return "Unknown";
}

static const char *mlx5_fpga_qp_syndrome_to_string(u8 syndrome)
{
	if (syndrome < ARRAY_SIZE(mlx5_fpga_qp_error_strings))
		return mlx5_fpga_qp_error_strings[syndrome];
	return "Unknown";
}

void mlx5_fpga_event(struct mlx5_core_dev *mdev, u8 event, void *data)
{
	struct mlx5_fpga_device *fdev = mdev->fpga;
	const char *event_name;
	bool teardown = false;
	unsigned long flags;
	u32 fpga_qpn;
	u8 syndrome;

	switch (event) {
	case MLX5_EVENT_TYPE_FPGA_ERROR:
		syndrome = MLX5_GET(fpga_error_event, data, syndrome);
		event_name = mlx5_fpga_syndrome_to_string(syndrome);
		break;
	case MLX5_EVENT_TYPE_FPGA_QP_ERROR:
		syndrome = MLX5_GET(fpga_qp_error_event, data, syndrome);
		event_name = mlx5_fpga_qp_syndrome_to_string(syndrome);
		fpga_qpn = MLX5_GET(fpga_qp_error_event, data, fpga_qpn);
		break;
	default:
		mlx5_fpga_warn_ratelimited(fdev, "Unexpected event %u\n",
					   event);
		return;
	}

	spin_lock_irqsave(&fdev->state_lock, flags);
	switch (fdev->state) {
	case MLX5_FPGA_STATUS_SUCCESS:
		mlx5_fpga_warn(fdev, "Error %u: %s\n", syndrome, event_name);
		teardown = true;
		break;
	default:
		mlx5_fpga_warn_ratelimited(fdev, "Unexpected error event %u: %s\n",
					   syndrome, event_name);
	}
	spin_unlock_irqrestore(&fdev->state_lock, flags);
	/* We tear-down the card's interfaces and functionality because
	 * the FPGA bump-on-the-wire is misbehaving and we lose ability
	 * to communicate with the network. User may still be able to
	 * recover by re-programming or debugging the FPGA
	 */
	if (teardown)
		mlx5_trigger_health_work(fdev->mdev);
}
