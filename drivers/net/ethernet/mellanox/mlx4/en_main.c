/*
 * Copyright (c) 2007 Mellanox Technologies. All rights reserved.
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
 *
 */

#include <linux/cpumask.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/netdevice.h>
#include <linux/slab.h>

#include <linux/mlx4/driver.h>
#include <linux/mlx4/device.h>
#include <linux/mlx4/cmd.h>

#include "mlx4_en.h"

MODULE_AUTHOR("Liran Liss, Yevgeny Petrilin");
MODULE_DESCRIPTION("Mellanox ConnectX HCA Ethernet driver");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_VERSION(DRV_VERSION);

static const char mlx4_en_version[] =
	DRV_NAME ": Mellanox ConnectX HCA Ethernet driver v"
	DRV_VERSION "\n";

#define MLX4_EN_PARM_INT(X, def_val, desc) \
	static unsigned int X = def_val;\
	module_param(X , uint, 0444); \
	MODULE_PARM_DESC(X, desc);


/*
 * Device scope module parameters
 */

/* Enable RSS UDP traffic */
MLX4_EN_PARM_INT(udp_rss, 1,
		 "Enable RSS for incoming UDP traffic or disabled (0)");

/* Priority pausing */
MLX4_EN_PARM_INT(pfctx, 0, "Priority based Flow Control policy on TX[7:0]."
			   " Per priority bit mask");
MLX4_EN_PARM_INT(pfcrx, 0, "Priority based Flow Control policy on RX[7:0]."
			   " Per priority bit mask");

MLX4_EN_PARM_INT(inline_thold, MAX_INLINE,
		 "Threshold for using inline data (range: 17-104, default: 104)");

#define MAX_PFC_TX     0xff
#define MAX_PFC_RX     0xff

void en_print(const char *level, const struct mlx4_en_priv *priv,
	      const char *format, ...)
{
	va_list args;
	struct va_format vaf;

	va_start(args, format);

	vaf.fmt = format;
	vaf.va = &args;
	if (priv->registered)
		printk("%s%s: %s: %pV",
		       level, DRV_NAME, priv->dev->name, &vaf);
	else
		printk("%s%s: %s: Port %d: %pV",
		       level, DRV_NAME, dev_name(&priv->mdev->pdev->dev),
		       priv->port, &vaf);
	va_end(args);
}

void mlx4_en_update_loopback_state(struct net_device *dev,
				   netdev_features_t features)
{
	struct mlx4_en_priv *priv = netdev_priv(dev);

	if (features & NETIF_F_LOOPBACK)
		priv->ctrl_flags |= cpu_to_be32(MLX4_WQE_CTRL_FORCE_LOOPBACK);
	else
		priv->ctrl_flags &= cpu_to_be32(~MLX4_WQE_CTRL_FORCE_LOOPBACK);

	priv->flags &= ~(MLX4_EN_FLAG_RX_FILTER_NEEDED|
			MLX4_EN_FLAG_ENABLE_HW_LOOPBACK);

	/* Drop the packet if SRIOV is not enabled
	 * and not performing the selftest or flb disabled
	 */
	if (mlx4_is_mfunc(priv->mdev->dev) &&
	    !(features & NETIF_F_LOOPBACK) && !priv->validate_loopback)
		priv->flags |= MLX4_EN_FLAG_RX_FILTER_NEEDED;

	/* Set dmac in Tx WQE if we are in SRIOV mode or if loopback selftest
	 * is requested
	 */
	if (mlx4_is_mfunc(priv->mdev->dev) || priv->validate_loopback)
		priv->flags |= MLX4_EN_FLAG_ENABLE_HW_LOOPBACK;

	mutex_lock(&priv->mdev->state_lock);
	if ((priv->mdev->dev->caps.flags2 &
	     MLX4_DEV_CAP_FLAG2_UPDATE_QP_SRC_CHECK_LB) &&
	    priv->rss_map.indir_qp && priv->rss_map.indir_qp->qpn) {
		int i;
		int err = 0;
		int loopback = !!(features & NETIF_F_LOOPBACK);

		for (i = 0; i < priv->rx_ring_num; i++) {
			int ret;

			ret = mlx4_en_change_mcast_lb(priv,
						      &priv->rss_map.qps[i],
						      loopback);
			if (!err)
				err = ret;
		}
		if (err)
			mlx4_warn(priv->mdev, "failed to change mcast loopback\n");
	}
	mutex_unlock(&priv->mdev->state_lock);
}

static void mlx4_en_get_profile(struct mlx4_en_dev *mdev)
{
	struct mlx4_en_profile *params = &mdev->profile;
	int i;

	params->udp_rss = udp_rss;
	params->max_num_tx_rings_p_up = mlx4_low_memory_profile() ?
		MLX4_EN_MIN_TX_RING_P_UP :
		min_t(int, num_online_cpus(), MLX4_EN_MAX_TX_RING_P_UP);

	if (params->udp_rss && !(mdev->dev->caps.flags
					& MLX4_DEV_CAP_FLAG_UDP_RSS)) {
		mlx4_warn(mdev, "UDP RSS is not supported on this device\n");
		params->udp_rss = 0;
	}
	for (i = 1; i <= MLX4_MAX_PORTS; i++) {
		params->prof[i].rx_pause = !(pfcrx || pfctx);
		params->prof[i].rx_ppp = pfcrx;
		params->prof[i].tx_pause = !(pfcrx || pfctx);
		params->prof[i].tx_ppp = pfctx;
		if (mlx4_low_memory_profile()) {
			params->prof[i].tx_ring_size = MLX4_EN_MIN_TX_SIZE;
			params->prof[i].rx_ring_size = MLX4_EN_MIN_RX_SIZE;
		} else {
			params->prof[i].tx_ring_size = MLX4_EN_DEF_TX_RING_SIZE;
			params->prof[i].rx_ring_size = MLX4_EN_DEF_RX_RING_SIZE;
		}
		params->prof[i].num_up = MLX4_EN_NUM_UP_LOW;
		params->prof[i].num_tx_rings_p_up = params->max_num_tx_rings_p_up;
		params->prof[i].tx_ring_num[TX] = params->max_num_tx_rings_p_up *
			params->prof[i].num_up;
		params->prof[i].rss_rings = 0;
		params->prof[i].inline_thold = inline_thold;
	}
}

static int mlx4_en_event(struct notifier_block *this, unsigned long event,
			 void *param)
{
	struct mlx4_en_dev *mdev =
		container_of(this, struct mlx4_en_dev, mlx_nb);
	struct mlx4_dev *dev = mdev->dev;
	struct mlx4_en_priv *priv;
	int port;

	switch (event) {
	case MLX4_DEV_EVENT_CATASTROPHIC_ERROR:
	case MLX4_DEV_EVENT_PORT_MGMT_CHANGE:
	case MLX4_DEV_EVENT_SLAVE_INIT:
	case MLX4_DEV_EVENT_SLAVE_SHUTDOWN:
		break;
	default:
		port = *(int *)param;
		break;
	}

	switch (event) {
	case MLX4_DEV_EVENT_PORT_UP:
	case MLX4_DEV_EVENT_PORT_DOWN:
		if (!mdev->pndev[port])
			return NOTIFY_DONE;
		priv = netdev_priv(mdev->pndev[port]);
		/* To prevent races, we poll the link state in a separate
		  task rather than changing it here */
		priv->link_state = event;
		queue_work(mdev->workqueue, &priv->linkstate_task);
		break;

	case MLX4_DEV_EVENT_CATASTROPHIC_ERROR:
		mlx4_err(mdev, "Internal error detected, restarting device\n");
		break;

	case MLX4_DEV_EVENT_PORT_MGMT_CHANGE:
	case MLX4_DEV_EVENT_SLAVE_INIT:
	case MLX4_DEV_EVENT_SLAVE_SHUTDOWN:
		break;
	default:
		if (port < 1 || port > dev->caps.num_ports ||
		    !mdev->pndev[port])
			return NOTIFY_DONE;
		mlx4_warn(mdev, "Unhandled event %d for port %d\n", (int)event,
			  port);
	}

	return NOTIFY_DONE;
}

static void mlx4_en_remove(struct auxiliary_device *adev)
{
	struct mlx4_adev *madev = container_of(adev, struct mlx4_adev, adev);
	struct mlx4_dev *dev = madev->mdev;
	struct mlx4_en_dev *mdev = auxiliary_get_drvdata(adev);
	int i;

	mlx4_unregister_event_notifier(dev, &mdev->mlx_nb);

	mutex_lock(&mdev->state_lock);
	mdev->device_up = false;
	mutex_unlock(&mdev->state_lock);

	mlx4_foreach_port(i, dev, MLX4_PORT_TYPE_ETH)
		if (mdev->pndev[i])
			mlx4_en_destroy_netdev(mdev->pndev[i]);

	destroy_workqueue(mdev->workqueue);
	(void) mlx4_mr_free(dev, &mdev->mr);
	iounmap(mdev->uar_map);
	mlx4_uar_free(dev, &mdev->priv_uar);
	mlx4_pd_free(dev, mdev->priv_pdn);
	if (mdev->netdev_nb.notifier_call)
		unregister_netdevice_notifier(&mdev->netdev_nb);
	kfree(mdev);
}

static int mlx4_en_probe(struct auxiliary_device *adev,
			 const struct auxiliary_device_id *id)
{
	struct mlx4_adev *madev = container_of(adev, struct mlx4_adev, adev);
	struct mlx4_dev *dev = madev->mdev;
	struct mlx4_en_dev *mdev;
	int err, i;

	printk_once(KERN_INFO "%s", mlx4_en_version);

	mdev = kzalloc(sizeof(*mdev), GFP_KERNEL);
	if (!mdev) {
		err = -ENOMEM;
		goto err_free_res;
	}

	err = mlx4_pd_alloc(dev, &mdev->priv_pdn);
	if (err)
		goto err_free_dev;

	err = mlx4_uar_alloc(dev, &mdev->priv_uar);
	if (err)
		goto err_pd;

	mdev->uar_map = ioremap((phys_addr_t) mdev->priv_uar.pfn << PAGE_SHIFT,
				PAGE_SIZE);
	if (!mdev->uar_map) {
		err = -ENOMEM;
		goto err_uar;
	}
	spin_lock_init(&mdev->uar_lock);

	mdev->dev = dev;
	mdev->dma_device = &dev->persist->pdev->dev;
	mdev->pdev = dev->persist->pdev;
	mdev->device_up = false;

	mdev->LSO_support = !!(dev->caps.flags & (1 << 15));
	if (!mdev->LSO_support)
		mlx4_warn(mdev, "LSO not supported, please upgrade to later FW version to enable LSO\n");

	err = mlx4_mr_alloc(mdev->dev, mdev->priv_pdn, 0, ~0ull,
			    MLX4_PERM_LOCAL_WRITE | MLX4_PERM_LOCAL_READ, 0, 0,
			    &mdev->mr);
	if (err) {
		mlx4_err(mdev, "Failed allocating memory region\n");
		goto err_map;
	}
	err = mlx4_mr_enable(mdev->dev, &mdev->mr);
	if (err) {
		mlx4_err(mdev, "Failed enabling memory region\n");
		goto err_mr;
	}

	/* Build device profile according to supplied module parameters */
	mlx4_en_get_profile(mdev);

	/* Configure which ports to start according to module parameters */
	mdev->port_cnt = 0;
	mlx4_foreach_port(i, dev, MLX4_PORT_TYPE_ETH)
		mdev->port_cnt++;

	/* Set default number of RX rings*/
	mlx4_en_set_num_rx_rings(mdev);

	/* Create our own workqueue for reset/multicast tasks
	 * Note: we cannot use the shared workqueue because of deadlocks caused
	 *       by the rtnl lock */
	mdev->workqueue = create_singlethread_workqueue("mlx4_en");
	if (!mdev->workqueue) {
		err = -ENOMEM;
		goto err_mr;
	}

	/* At this stage all non-port specific tasks are complete:
	 * mark the card state as up */
	mutex_init(&mdev->state_lock);
	mdev->device_up = true;

	/* register mlx4 core notifier */
	mdev->mlx_nb.notifier_call = mlx4_en_event;
	err = mlx4_register_event_notifier(dev, &mdev->mlx_nb);
	WARN(err, "failed to register mlx4 event notifier (%d)", err);

	/* Setup ports */

	/* Create a netdev for each port */
	mlx4_foreach_port(i, dev, MLX4_PORT_TYPE_ETH) {
		mlx4_info(mdev, "Activating port:%d\n", i);
		if (mlx4_en_init_netdev(mdev, i, &mdev->profile.prof[i]))
			mdev->pndev[i] = NULL;
	}

	/* register netdev notifier */
	mdev->netdev_nb.notifier_call = mlx4_en_netdev_event;
	if (register_netdevice_notifier(&mdev->netdev_nb)) {
		mdev->netdev_nb.notifier_call = NULL;
		mlx4_err(mdev, "Failed to create netdev notifier\n");
	}

	auxiliary_set_drvdata(adev, mdev);
	return 0;

err_mr:
	(void) mlx4_mr_free(dev, &mdev->mr);
err_map:
	if (mdev->uar_map)
		iounmap(mdev->uar_map);
err_uar:
	mlx4_uar_free(dev, &mdev->priv_uar);
err_pd:
	mlx4_pd_free(dev, mdev->priv_pdn);
err_free_dev:
	kfree(mdev);
err_free_res:
	return err;
}

static const struct auxiliary_device_id mlx4_en_id_table[] = {
	{ .name = MLX4_ADEV_NAME ".eth" },
	{},
};

MODULE_DEVICE_TABLE(auxiliary, mlx4_en_id_table);

static struct mlx4_adrv mlx4_en_adrv = {
	.adrv = {
		.name	= "eth",
		.probe	= mlx4_en_probe,
		.remove	= mlx4_en_remove,
		.id_table = mlx4_en_id_table,
	},
	.protocol	= MLX4_PROT_ETH,
};

static void mlx4_en_verify_params(void)
{
	if (pfctx > MAX_PFC_TX) {
		pr_warn("mlx4_en: WARNING: illegal module parameter pfctx 0x%x - should be in range 0-0x%x, will be changed to default (0)\n",
			pfctx, MAX_PFC_TX);
		pfctx = 0;
	}

	if (pfcrx > MAX_PFC_RX) {
		pr_warn("mlx4_en: WARNING: illegal module parameter pfcrx 0x%x - should be in range 0-0x%x, will be changed to default (0)\n",
			pfcrx, MAX_PFC_RX);
		pfcrx = 0;
	}

	if (inline_thold < MIN_PKT_LEN || inline_thold > MAX_INLINE) {
		pr_warn("mlx4_en: WARNING: illegal module parameter inline_thold %d - should be in range %d-%d, will be changed to default (%d)\n",
			inline_thold, MIN_PKT_LEN, MAX_INLINE, MAX_INLINE);
		inline_thold = MAX_INLINE;
	}
}

static int __init mlx4_en_init(void)
{
	mlx4_en_verify_params();
	mlx4_en_init_ptys2ethtool_map();

	return mlx4_register_auxiliary_driver(&mlx4_en_adrv);
}

static void __exit mlx4_en_cleanup(void)
{
	mlx4_unregister_auxiliary_driver(&mlx4_en_adrv);
}

module_init(mlx4_en_init);
module_exit(mlx4_en_cleanup);

