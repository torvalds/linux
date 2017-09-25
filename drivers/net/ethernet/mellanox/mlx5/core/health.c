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
	MLX5_NIC_IFC_FULL		= 0,
	MLX5_NIC_IFC_DISABLED		= 1,
	MLX5_NIC_IFC_NO_DRAM_NIC	= 2,
	MLX5_NIC_IFC_INVALID		= 3
};

enum {
	MLX5_DROP_NEW_HEALTH_WORK,
	MLX5_DROP_NEW_RECOVERY_WORK,
};

static u8 get_nic_state(struct mlx5_core_dev *dev)
{
	return (ioread32be(&dev->iseg->cmdq_addr_l_sz) >> 8) & 3;
}

static void trigger_cmd_completions(struct mlx5_core_dev *dev)
{
	unsigned long flags;
	u64 vector;

	/* wait for pending handlers to complete */
	synchronize_irq(pci_irq_vector(dev->pdev, MLX5_EQ_VEC_CMD));
	spin_lock_irqsave(&dev->cmd.alloc_lock, flags);
	vector = ~dev->cmd.bitmask & ((1ul << (1 << dev->cmd.log_sz)) - 1);
	if (!vector)
		goto no_trig;

	vector |= MLX5_TRIGGERED_CMD_COMP;
	spin_unlock_irqrestore(&dev->cmd.alloc_lock, flags);

	mlx5_core_dbg(dev, "vector 0x%llx\n", vector);
	mlx5_cmd_comp_handler(dev, vector, true);
	return;

no_trig:
	spin_unlock_irqrestore(&dev->cmd.alloc_lock, flags);
}

static int in_fatal(struct mlx5_core_dev *dev)
{
	struct mlx5_core_health *health = &dev->priv.health;
	struct health_buffer __iomem *h = health->health;

	if (get_nic_state(dev) == MLX5_NIC_IFC_DISABLED)
		return 1;

	if (ioread32be(&h->fw_ver) == 0xffffffff)
		return 1;

	return 0;
}

void mlx5_enter_error_state(struct mlx5_core_dev *dev, bool force)
{
	mutex_lock(&dev->intf_state_mutex);
	if (dev->state == MLX5_DEVICE_STATE_INTERNAL_ERROR)
		goto unlock;

	mlx5_core_err(dev, "start\n");
	if (pci_channel_offline(dev->pdev) || in_fatal(dev) || force) {
		dev->state = MLX5_DEVICE_STATE_INTERNAL_ERROR;
		trigger_cmd_completions(dev);
	}

	mlx5_core_event(dev, MLX5_DEV_EVENT_SYS_ERROR, 0);
	mlx5_core_err(dev, "end\n");

unlock:
	mutex_unlock(&dev->intf_state_mutex);
}

static void mlx5_handle_bad_state(struct mlx5_core_dev *dev)
{
	u8 nic_interface = get_nic_state(dev);

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
	default:
		mlx5_core_warn(dev, "Expected to see disabled NIC but it is has invalid value %d\n",
			       nic_interface);
	}

	mlx5_disable_device(dev);
}

static void health_recover(struct work_struct *work)
{
	struct mlx5_core_health *health;
	struct delayed_work *dwork;
	struct mlx5_core_dev *dev;
	struct mlx5_priv *priv;
	u8 nic_state;

	dwork = container_of(work, struct delayed_work, work);
	health = container_of(dwork, struct mlx5_core_health, recover_work);
	priv = container_of(health, struct mlx5_priv, health);
	dev = container_of(priv, struct mlx5_core_dev, priv);

	nic_state = get_nic_state(dev);
	if (nic_state == MLX5_NIC_IFC_INVALID) {
		dev_err(&dev->pdev->dev, "health recovery flow aborted since the nic state is invalid\n");
		return;
	}

	dev_err(&dev->pdev->dev, "starting health recovery flow\n");
	mlx5_recover_device(dev);
}

/* How much time to wait until health resetting the driver (in msecs) */
#define MLX5_RECOVERY_DELAY_MSECS 60000
static void health_care(struct work_struct *work)
{
	unsigned long recover_delay = msecs_to_jiffies(MLX5_RECOVERY_DELAY_MSECS);
	struct mlx5_core_health *health;
	struct mlx5_core_dev *dev;
	struct mlx5_priv *priv;
	unsigned long flags;

	health = container_of(work, struct mlx5_core_health, work);
	priv = container_of(health, struct mlx5_priv, health);
	dev = container_of(priv, struct mlx5_core_dev, priv);
	mlx5_core_warn(dev, "handling bad device here\n");
	mlx5_handle_bad_state(dev);

	spin_lock_irqsave(&health->wq_lock, flags);
	if (!test_bit(MLX5_DROP_NEW_RECOVERY_WORK, &health->flags))
		schedule_delayed_work(&health->recover_work, recover_delay);
	else
		dev_err(&dev->pdev->dev,
			"new health works are not permitted at this stage\n");
	spin_unlock_irqrestore(&health->wq_lock, flags);
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

	/* If the syndrom is 0, the device is OK and no need to print buffer */
	if (!ioread8(&h->synd))
		return;

	for (i = 0; i < ARRAY_SIZE(h->assert_var); i++)
		dev_err(&dev->pdev->dev, "assert_var[%d] 0x%08x\n", i, ioread32be(h->assert_var + i));

	dev_err(&dev->pdev->dev, "assert_exit_ptr 0x%08x\n", ioread32be(&h->assert_exit_ptr));
	dev_err(&dev->pdev->dev, "assert_callra 0x%08x\n", ioread32be(&h->assert_callra));
	sprintf(fw_str, "%d.%d.%d", fw_rev_maj(dev), fw_rev_min(dev), fw_rev_sub(dev));
	dev_err(&dev->pdev->dev, "fw_ver %s\n", fw_str);
	dev_err(&dev->pdev->dev, "hw_id 0x%08x\n", ioread32be(&h->hw_id));
	dev_err(&dev->pdev->dev, "irisc_index %d\n", ioread8(&h->irisc_index));
	dev_err(&dev->pdev->dev, "synd 0x%x: %s\n", ioread8(&h->synd), hsynd_str(ioread8(&h->synd)));
	dev_err(&dev->pdev->dev, "ext_synd 0x%04x\n", ioread16be(&h->ext_synd));
	fw = ioread32be(&h->fw_ver);
	dev_err(&dev->pdev->dev, "raw fw_ver 0x%08x\n", fw);
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
		queue_work(health->wq, &health->work);
	else
		dev_err(&dev->pdev->dev,
			"new health works are not permitted at this stage\n");
	spin_unlock_irqrestore(&health->wq_lock, flags);
}

static void poll_health(unsigned long data)
{
	struct mlx5_core_dev *dev = (struct mlx5_core_dev *)data;
	struct mlx5_core_health *health = &dev->priv.health;
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
		dev_err(&dev->pdev->dev, "device's health compromised - reached miss count\n");
		print_health_info(dev);
	}

	if (in_fatal(dev) && !health->sick) {
		health->sick = true;
		print_health_info(dev);
		mlx5_trigger_health_work(dev);
	}

out:
	mod_timer(&health->timer, get_next_poll_jiffies());
}

void mlx5_start_health_poll(struct mlx5_core_dev *dev)
{
	struct mlx5_core_health *health = &dev->priv.health;

	init_timer(&health->timer);
	health->sick = 0;
	clear_bit(MLX5_DROP_NEW_HEALTH_WORK, &health->flags);
	clear_bit(MLX5_DROP_NEW_RECOVERY_WORK, &health->flags);
	health->health = &dev->iseg->health;
	health->health_counter = &dev->iseg->health_counter;

	health->timer.data = (unsigned long)dev;
	health->timer.function = poll_health;
	health->timer.expires = round_jiffies(jiffies + MLX5_HEALTH_POLL_INTERVAL);
	add_timer(&health->timer);
}

void mlx5_stop_health_poll(struct mlx5_core_dev *dev)
{
	struct mlx5_core_health *health = &dev->priv.health;

	del_timer_sync(&health->timer);
}

void mlx5_drain_health_wq(struct mlx5_core_dev *dev)
{
	struct mlx5_core_health *health = &dev->priv.health;
	unsigned long flags;

	spin_lock_irqsave(&health->wq_lock, flags);
	set_bit(MLX5_DROP_NEW_HEALTH_WORK, &health->flags);
	set_bit(MLX5_DROP_NEW_RECOVERY_WORK, &health->flags);
	spin_unlock_irqrestore(&health->wq_lock, flags);
	cancel_delayed_work_sync(&health->recover_work);
	cancel_work_sync(&health->work);
}

void mlx5_drain_health_recovery(struct mlx5_core_dev *dev)
{
	struct mlx5_core_health *health = &dev->priv.health;

	spin_lock(&health->wq_lock);
	set_bit(MLX5_DROP_NEW_RECOVERY_WORK, &health->flags);
	spin_unlock(&health->wq_lock);
	cancel_delayed_work_sync(&dev->priv.health.recover_work);
}

void mlx5_health_cleanup(struct mlx5_core_dev *dev)
{
	struct mlx5_core_health *health = &dev->priv.health;

	destroy_workqueue(health->wq);
}

int mlx5_health_init(struct mlx5_core_dev *dev)
{
	struct mlx5_core_health *health;
	char *name;

	health = &dev->priv.health;
	name = kmalloc(64, GFP_KERNEL);
	if (!name)
		return -ENOMEM;

	strcpy(name, "mlx5_health");
	strcat(name, dev_name(&dev->pdev->dev));
	health->wq = create_singlethread_workqueue(name);
	kfree(name);
	if (!health->wq)
		return -ENOMEM;
	spin_lock_init(&health->wq_lock);
	INIT_WORK(&health->work, health_care);
	INIT_DELAYED_WORK(&health->recover_work, health_recover);

	return 0;
}
