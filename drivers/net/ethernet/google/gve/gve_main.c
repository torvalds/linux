// SPDX-License-Identifier: (GPL-2.0 OR MIT)
/* Google virtual Ethernet (gve) driver
 *
 * Copyright (C) 2015-2021 Google, Inc.
 */

#include <linux/bpf.h>
#include <linux/cpumask.h>
#include <linux/etherdevice.h>
#include <linux/filter.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/workqueue.h>
#include <linux/utsname.h>
#include <linux/version.h>
#include <net/sch_generic.h>
#include <net/xdp_sock_drv.h>
#include "gve.h"
#include "gve_dqo.h"
#include "gve_adminq.h"
#include "gve_register.h"
#include "gve_utils.h"

#define GVE_DEFAULT_RX_COPYBREAK	(256)

#define DEFAULT_MSG_LEVEL	(NETIF_MSG_DRV | NETIF_MSG_LINK)
#define GVE_VERSION		"1.0.0"
#define GVE_VERSION_PREFIX	"GVE-"

// Minimum amount of time between queue kicks in msec (10 seconds)
#define MIN_TX_TIMEOUT_GAP (1000 * 10)

char gve_driver_name[] = "gve";
const char gve_version_str[] = GVE_VERSION;
static const char gve_version_prefix[] = GVE_VERSION_PREFIX;

static int gve_verify_driver_compatibility(struct gve_priv *priv)
{
	int err;
	struct gve_driver_info *driver_info;
	dma_addr_t driver_info_bus;

	driver_info = dma_alloc_coherent(&priv->pdev->dev,
					 sizeof(struct gve_driver_info),
					 &driver_info_bus, GFP_KERNEL);
	if (!driver_info)
		return -ENOMEM;

	*driver_info = (struct gve_driver_info) {
		.os_type = 1, /* Linux */
		.os_version_major = cpu_to_be32(LINUX_VERSION_MAJOR),
		.os_version_minor = cpu_to_be32(LINUX_VERSION_SUBLEVEL),
		.os_version_sub = cpu_to_be32(LINUX_VERSION_PATCHLEVEL),
		.driver_capability_flags = {
			cpu_to_be64(GVE_DRIVER_CAPABILITY_FLAGS1),
			cpu_to_be64(GVE_DRIVER_CAPABILITY_FLAGS2),
			cpu_to_be64(GVE_DRIVER_CAPABILITY_FLAGS3),
			cpu_to_be64(GVE_DRIVER_CAPABILITY_FLAGS4),
		},
	};
	strscpy(driver_info->os_version_str1, utsname()->release,
		sizeof(driver_info->os_version_str1));
	strscpy(driver_info->os_version_str2, utsname()->version,
		sizeof(driver_info->os_version_str2));

	err = gve_adminq_verify_driver_compatibility(priv,
						     sizeof(struct gve_driver_info),
						     driver_info_bus);

	/* It's ok if the device doesn't support this */
	if (err == -EOPNOTSUPP)
		err = 0;

	dma_free_coherent(&priv->pdev->dev,
			  sizeof(struct gve_driver_info),
			  driver_info, driver_info_bus);
	return err;
}

static netdev_features_t gve_features_check(struct sk_buff *skb,
					    struct net_device *dev,
					    netdev_features_t features)
{
	struct gve_priv *priv = netdev_priv(dev);

	if (!gve_is_gqi(priv))
		return gve_features_check_dqo(skb, dev, features);

	return features;
}

static netdev_tx_t gve_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct gve_priv *priv = netdev_priv(dev);

	if (gve_is_gqi(priv))
		return gve_tx(skb, dev);
	else
		return gve_tx_dqo(skb, dev);
}

static void gve_get_stats(struct net_device *dev, struct rtnl_link_stats64 *s)
{
	struct gve_priv *priv = netdev_priv(dev);
	unsigned int start;
	u64 packets, bytes;
	int num_tx_queues;
	int ring;

	num_tx_queues = gve_num_tx_queues(priv);
	if (priv->rx) {
		for (ring = 0; ring < priv->rx_cfg.num_queues; ring++) {
			do {
				start =
				  u64_stats_fetch_begin(&priv->rx[ring].statss);
				packets = priv->rx[ring].rpackets;
				bytes = priv->rx[ring].rbytes;
			} while (u64_stats_fetch_retry(&priv->rx[ring].statss,
						       start));
			s->rx_packets += packets;
			s->rx_bytes += bytes;
		}
	}
	if (priv->tx) {
		for (ring = 0; ring < num_tx_queues; ring++) {
			do {
				start =
				  u64_stats_fetch_begin(&priv->tx[ring].statss);
				packets = priv->tx[ring].pkt_done;
				bytes = priv->tx[ring].bytes_done;
			} while (u64_stats_fetch_retry(&priv->tx[ring].statss,
						       start));
			s->tx_packets += packets;
			s->tx_bytes += bytes;
		}
	}
}

static int gve_alloc_counter_array(struct gve_priv *priv)
{
	priv->counter_array =
		dma_alloc_coherent(&priv->pdev->dev,
				   priv->num_event_counters *
				   sizeof(*priv->counter_array),
				   &priv->counter_array_bus, GFP_KERNEL);
	if (!priv->counter_array)
		return -ENOMEM;

	return 0;
}

static void gve_free_counter_array(struct gve_priv *priv)
{
	if (!priv->counter_array)
		return;

	dma_free_coherent(&priv->pdev->dev,
			  priv->num_event_counters *
			  sizeof(*priv->counter_array),
			  priv->counter_array, priv->counter_array_bus);
	priv->counter_array = NULL;
}

/* NIC requests to report stats */
static void gve_stats_report_task(struct work_struct *work)
{
	struct gve_priv *priv = container_of(work, struct gve_priv,
					     stats_report_task);
	if (gve_get_do_report_stats(priv)) {
		gve_handle_report_stats(priv);
		gve_clear_do_report_stats(priv);
	}
}

static void gve_stats_report_schedule(struct gve_priv *priv)
{
	if (!gve_get_probe_in_progress(priv) &&
	    !gve_get_reset_in_progress(priv)) {
		gve_set_do_report_stats(priv);
		queue_work(priv->gve_wq, &priv->stats_report_task);
	}
}

static void gve_stats_report_timer(struct timer_list *t)
{
	struct gve_priv *priv = from_timer(priv, t, stats_report_timer);

	mod_timer(&priv->stats_report_timer,
		  round_jiffies(jiffies +
		  msecs_to_jiffies(priv->stats_report_timer_period)));
	gve_stats_report_schedule(priv);
}

static int gve_alloc_stats_report(struct gve_priv *priv)
{
	int tx_stats_num, rx_stats_num;

	tx_stats_num = (GVE_TX_STATS_REPORT_NUM + NIC_TX_STATS_REPORT_NUM) *
		       gve_num_tx_queues(priv);
	rx_stats_num = (GVE_RX_STATS_REPORT_NUM + NIC_RX_STATS_REPORT_NUM) *
		       priv->rx_cfg.num_queues;
	priv->stats_report_len = struct_size(priv->stats_report, stats,
					     size_add(tx_stats_num, rx_stats_num));
	priv->stats_report =
		dma_alloc_coherent(&priv->pdev->dev, priv->stats_report_len,
				   &priv->stats_report_bus, GFP_KERNEL);
	if (!priv->stats_report)
		return -ENOMEM;
	/* Set up timer for the report-stats task */
	timer_setup(&priv->stats_report_timer, gve_stats_report_timer, 0);
	priv->stats_report_timer_period = GVE_STATS_REPORT_TIMER_PERIOD;
	return 0;
}

static void gve_free_stats_report(struct gve_priv *priv)
{
	if (!priv->stats_report)
		return;

	del_timer_sync(&priv->stats_report_timer);
	dma_free_coherent(&priv->pdev->dev, priv->stats_report_len,
			  priv->stats_report, priv->stats_report_bus);
	priv->stats_report = NULL;
}

static irqreturn_t gve_mgmnt_intr(int irq, void *arg)
{
	struct gve_priv *priv = arg;

	queue_work(priv->gve_wq, &priv->service_task);
	return IRQ_HANDLED;
}

static irqreturn_t gve_intr(int irq, void *arg)
{
	struct gve_notify_block *block = arg;
	struct gve_priv *priv = block->priv;

	iowrite32be(GVE_IRQ_MASK, gve_irq_doorbell(priv, block));
	napi_schedule_irqoff(&block->napi);
	return IRQ_HANDLED;
}

static irqreturn_t gve_intr_dqo(int irq, void *arg)
{
	struct gve_notify_block *block = arg;

	/* Interrupts are automatically masked */
	napi_schedule_irqoff(&block->napi);
	return IRQ_HANDLED;
}

int gve_napi_poll(struct napi_struct *napi, int budget)
{
	struct gve_notify_block *block;
	__be32 __iomem *irq_doorbell;
	bool reschedule = false;
	struct gve_priv *priv;
	int work_done = 0;

	block = container_of(napi, struct gve_notify_block, napi);
	priv = block->priv;

	if (block->tx) {
		if (block->tx->q_num < priv->tx_cfg.num_queues)
			reschedule |= gve_tx_poll(block, budget);
		else if (budget)
			reschedule |= gve_xdp_poll(block, budget);
	}

	if (!budget)
		return 0;

	if (block->rx) {
		work_done = gve_rx_poll(block, budget);
		reschedule |= work_done == budget;
	}

	if (reschedule)
		return budget;

       /* Complete processing - don't unmask irq if busy polling is enabled */
	if (likely(napi_complete_done(napi, work_done))) {
		irq_doorbell = gve_irq_doorbell(priv, block);
		iowrite32be(GVE_IRQ_ACK | GVE_IRQ_EVENT, irq_doorbell);

		/* Ensure IRQ ACK is visible before we check pending work.
		 * If queue had issued updates, it would be truly visible.
		 */
		mb();

		if (block->tx)
			reschedule |= gve_tx_clean_pending(priv, block->tx);
		if (block->rx)
			reschedule |= gve_rx_work_pending(block->rx);

		if (reschedule && napi_schedule(napi))
			iowrite32be(GVE_IRQ_MASK, irq_doorbell);
	}
	return work_done;
}

int gve_napi_poll_dqo(struct napi_struct *napi, int budget)
{
	struct gve_notify_block *block =
		container_of(napi, struct gve_notify_block, napi);
	struct gve_priv *priv = block->priv;
	bool reschedule = false;
	int work_done = 0;

	if (block->tx)
		reschedule |= gve_tx_poll_dqo(block, /*do_clean=*/true);

	if (!budget)
		return 0;

	if (block->rx) {
		work_done = gve_rx_poll_dqo(block, budget);
		reschedule |= work_done == budget;
	}

	if (reschedule)
		return budget;

	if (likely(napi_complete_done(napi, work_done))) {
		/* Enable interrupts again.
		 *
		 * We don't need to repoll afterwards because HW supports the
		 * PCI MSI-X PBA feature.
		 *
		 * Another interrupt would be triggered if a new event came in
		 * since the last one.
		 */
		gve_write_irq_doorbell_dqo(priv, block,
					   GVE_ITR_NO_UPDATE_DQO | GVE_ITR_ENABLE_BIT_DQO);
	}

	return work_done;
}

static int gve_alloc_notify_blocks(struct gve_priv *priv)
{
	int num_vecs_requested = priv->num_ntfy_blks + 1;
	unsigned int active_cpus;
	int vecs_enabled;
	int i, j;
	int err;

	priv->msix_vectors = kvcalloc(num_vecs_requested,
				      sizeof(*priv->msix_vectors), GFP_KERNEL);
	if (!priv->msix_vectors)
		return -ENOMEM;
	for (i = 0; i < num_vecs_requested; i++)
		priv->msix_vectors[i].entry = i;
	vecs_enabled = pci_enable_msix_range(priv->pdev, priv->msix_vectors,
					     GVE_MIN_MSIX, num_vecs_requested);
	if (vecs_enabled < 0) {
		dev_err(&priv->pdev->dev, "Could not enable min msix %d/%d\n",
			GVE_MIN_MSIX, vecs_enabled);
		err = vecs_enabled;
		goto abort_with_msix_vectors;
	}
	if (vecs_enabled != num_vecs_requested) {
		int new_num_ntfy_blks = (vecs_enabled - 1) & ~0x1;
		int vecs_per_type = new_num_ntfy_blks / 2;
		int vecs_left = new_num_ntfy_blks % 2;

		priv->num_ntfy_blks = new_num_ntfy_blks;
		priv->mgmt_msix_idx = priv->num_ntfy_blks;
		priv->tx_cfg.max_queues = min_t(int, priv->tx_cfg.max_queues,
						vecs_per_type);
		priv->rx_cfg.max_queues = min_t(int, priv->rx_cfg.max_queues,
						vecs_per_type + vecs_left);
		dev_err(&priv->pdev->dev,
			"Could not enable desired msix, only enabled %d, adjusting tx max queues to %d, and rx max queues to %d\n",
			vecs_enabled, priv->tx_cfg.max_queues,
			priv->rx_cfg.max_queues);
		if (priv->tx_cfg.num_queues > priv->tx_cfg.max_queues)
			priv->tx_cfg.num_queues = priv->tx_cfg.max_queues;
		if (priv->rx_cfg.num_queues > priv->rx_cfg.max_queues)
			priv->rx_cfg.num_queues = priv->rx_cfg.max_queues;
	}
	/* Half the notification blocks go to TX and half to RX */
	active_cpus = min_t(int, priv->num_ntfy_blks / 2, num_online_cpus());

	/* Setup Management Vector  - the last vector */
	snprintf(priv->mgmt_msix_name, sizeof(priv->mgmt_msix_name), "gve-mgmnt@pci:%s",
		 pci_name(priv->pdev));
	err = request_irq(priv->msix_vectors[priv->mgmt_msix_idx].vector,
			  gve_mgmnt_intr, 0, priv->mgmt_msix_name, priv);
	if (err) {
		dev_err(&priv->pdev->dev, "Did not receive management vector.\n");
		goto abort_with_msix_enabled;
	}
	priv->irq_db_indices =
		dma_alloc_coherent(&priv->pdev->dev,
				   priv->num_ntfy_blks *
				   sizeof(*priv->irq_db_indices),
				   &priv->irq_db_indices_bus, GFP_KERNEL);
	if (!priv->irq_db_indices) {
		err = -ENOMEM;
		goto abort_with_mgmt_vector;
	}

	priv->ntfy_blocks = kvzalloc(priv->num_ntfy_blks *
				     sizeof(*priv->ntfy_blocks), GFP_KERNEL);
	if (!priv->ntfy_blocks) {
		err = -ENOMEM;
		goto abort_with_irq_db_indices;
	}

	/* Setup the other blocks - the first n-1 vectors */
	for (i = 0; i < priv->num_ntfy_blks; i++) {
		struct gve_notify_block *block = &priv->ntfy_blocks[i];
		int msix_idx = i;

		snprintf(block->name, sizeof(block->name), "gve-ntfy-blk%d@pci:%s",
			 i, pci_name(priv->pdev));
		block->priv = priv;
		err = request_irq(priv->msix_vectors[msix_idx].vector,
				  gve_is_gqi(priv) ? gve_intr : gve_intr_dqo,
				  0, block->name, block);
		if (err) {
			dev_err(&priv->pdev->dev,
				"Failed to receive msix vector %d\n", i);
			goto abort_with_some_ntfy_blocks;
		}
		irq_set_affinity_hint(priv->msix_vectors[msix_idx].vector,
				      get_cpu_mask(i % active_cpus));
		block->irq_db_index = &priv->irq_db_indices[i].index;
	}
	return 0;
abort_with_some_ntfy_blocks:
	for (j = 0; j < i; j++) {
		struct gve_notify_block *block = &priv->ntfy_blocks[j];
		int msix_idx = j;

		irq_set_affinity_hint(priv->msix_vectors[msix_idx].vector,
				      NULL);
		free_irq(priv->msix_vectors[msix_idx].vector, block);
	}
	kvfree(priv->ntfy_blocks);
	priv->ntfy_blocks = NULL;
abort_with_irq_db_indices:
	dma_free_coherent(&priv->pdev->dev, priv->num_ntfy_blks *
			  sizeof(*priv->irq_db_indices),
			  priv->irq_db_indices, priv->irq_db_indices_bus);
	priv->irq_db_indices = NULL;
abort_with_mgmt_vector:
	free_irq(priv->msix_vectors[priv->mgmt_msix_idx].vector, priv);
abort_with_msix_enabled:
	pci_disable_msix(priv->pdev);
abort_with_msix_vectors:
	kvfree(priv->msix_vectors);
	priv->msix_vectors = NULL;
	return err;
}

static void gve_free_notify_blocks(struct gve_priv *priv)
{
	int i;

	if (!priv->msix_vectors)
		return;

	/* Free the irqs */
	for (i = 0; i < priv->num_ntfy_blks; i++) {
		struct gve_notify_block *block = &priv->ntfy_blocks[i];
		int msix_idx = i;

		irq_set_affinity_hint(priv->msix_vectors[msix_idx].vector,
				      NULL);
		free_irq(priv->msix_vectors[msix_idx].vector, block);
	}
	free_irq(priv->msix_vectors[priv->mgmt_msix_idx].vector, priv);
	kvfree(priv->ntfy_blocks);
	priv->ntfy_blocks = NULL;
	dma_free_coherent(&priv->pdev->dev, priv->num_ntfy_blks *
			  sizeof(*priv->irq_db_indices),
			  priv->irq_db_indices, priv->irq_db_indices_bus);
	priv->irq_db_indices = NULL;
	pci_disable_msix(priv->pdev);
	kvfree(priv->msix_vectors);
	priv->msix_vectors = NULL;
}

static int gve_setup_device_resources(struct gve_priv *priv)
{
	int err;

	err = gve_alloc_counter_array(priv);
	if (err)
		return err;
	err = gve_alloc_notify_blocks(priv);
	if (err)
		goto abort_with_counter;
	err = gve_alloc_stats_report(priv);
	if (err)
		goto abort_with_ntfy_blocks;
	err = gve_adminq_configure_device_resources(priv,
						    priv->counter_array_bus,
						    priv->num_event_counters,
						    priv->irq_db_indices_bus,
						    priv->num_ntfy_blks);
	if (unlikely(err)) {
		dev_err(&priv->pdev->dev,
			"could not setup device_resources: err=%d\n", err);
		err = -ENXIO;
		goto abort_with_stats_report;
	}

	if (!gve_is_gqi(priv)) {
		priv->ptype_lut_dqo = kvzalloc(sizeof(*priv->ptype_lut_dqo),
					       GFP_KERNEL);
		if (!priv->ptype_lut_dqo) {
			err = -ENOMEM;
			goto abort_with_stats_report;
		}
		err = gve_adminq_get_ptype_map_dqo(priv, priv->ptype_lut_dqo);
		if (err) {
			dev_err(&priv->pdev->dev,
				"Failed to get ptype map: err=%d\n", err);
			goto abort_with_ptype_lut;
		}
	}

	err = gve_adminq_report_stats(priv, priv->stats_report_len,
				      priv->stats_report_bus,
				      GVE_STATS_REPORT_TIMER_PERIOD);
	if (err)
		dev_err(&priv->pdev->dev,
			"Failed to report stats: err=%d\n", err);
	gve_set_device_resources_ok(priv);
	return 0;

abort_with_ptype_lut:
	kvfree(priv->ptype_lut_dqo);
	priv->ptype_lut_dqo = NULL;
abort_with_stats_report:
	gve_free_stats_report(priv);
abort_with_ntfy_blocks:
	gve_free_notify_blocks(priv);
abort_with_counter:
	gve_free_counter_array(priv);

	return err;
}

static void gve_trigger_reset(struct gve_priv *priv);

static void gve_teardown_device_resources(struct gve_priv *priv)
{
	int err;

	/* Tell device its resources are being freed */
	if (gve_get_device_resources_ok(priv)) {
		/* detach the stats report */
		err = gve_adminq_report_stats(priv, 0, 0x0, GVE_STATS_REPORT_TIMER_PERIOD);
		if (err) {
			dev_err(&priv->pdev->dev,
				"Failed to detach stats report: err=%d\n", err);
			gve_trigger_reset(priv);
		}
		err = gve_adminq_deconfigure_device_resources(priv);
		if (err) {
			dev_err(&priv->pdev->dev,
				"Could not deconfigure device resources: err=%d\n",
				err);
			gve_trigger_reset(priv);
		}
	}

	kvfree(priv->ptype_lut_dqo);
	priv->ptype_lut_dqo = NULL;

	gve_free_counter_array(priv);
	gve_free_notify_blocks(priv);
	gve_free_stats_report(priv);
	gve_clear_device_resources_ok(priv);
}

static int gve_unregister_qpl(struct gve_priv *priv, u32 i)
{
	int err;

	err = gve_adminq_unregister_page_list(priv, priv->qpls[i].id);
	if (err) {
		netif_err(priv, drv, priv->dev,
			  "Failed to unregister queue page list %d\n",
			  priv->qpls[i].id);
		return err;
	}

	priv->num_registered_pages -= priv->qpls[i].num_entries;
	return 0;
}

static int gve_register_qpl(struct gve_priv *priv, u32 i)
{
	int num_rx_qpls;
	int pages;
	int err;

	/* Rx QPLs succeed Tx QPLs in the priv->qpls array. */
	num_rx_qpls = gve_num_rx_qpls(&priv->rx_cfg, gve_is_qpl(priv));
	if (i >= gve_rx_start_qpl_id(&priv->tx_cfg) + num_rx_qpls) {
		netif_err(priv, drv, priv->dev,
			  "Cannot register nonexisting QPL at index %d\n", i);
		return -EINVAL;
	}

	pages = priv->qpls[i].num_entries;

	if (pages + priv->num_registered_pages > priv->max_registered_pages) {
		netif_err(priv, drv, priv->dev,
			  "Reached max number of registered pages %llu > %llu\n",
			  pages + priv->num_registered_pages,
			  priv->max_registered_pages);
		return -EINVAL;
	}

	err = gve_adminq_register_page_list(priv, &priv->qpls[i]);
	if (err) {
		netif_err(priv, drv, priv->dev,
			  "failed to register queue page list %d\n",
			  priv->qpls[i].id);
		/* This failure will trigger a reset - no need to clean
		 * up
		 */
		return err;
	}

	priv->num_registered_pages += pages;
	return 0;
}

static int gve_register_xdp_qpls(struct gve_priv *priv)
{
	int start_id;
	int err;
	int i;

	start_id = gve_xdp_tx_start_queue_id(priv);
	for (i = start_id; i < start_id + gve_num_xdp_qpls(priv); i++) {
		err = gve_register_qpl(priv, i);
		/* This failure will trigger a reset - no need to clean up */
		if (err)
			return err;
	}
	return 0;
}

static int gve_register_qpls(struct gve_priv *priv)
{
	int num_tx_qpls, num_rx_qpls;
	int start_id;
	int err;
	int i;

	num_tx_qpls = gve_num_tx_qpls(&priv->tx_cfg, gve_num_xdp_qpls(priv),
				      gve_is_qpl(priv));
	num_rx_qpls = gve_num_rx_qpls(&priv->rx_cfg, gve_is_qpl(priv));

	for (i = 0; i < num_tx_qpls; i++) {
		err = gve_register_qpl(priv, i);
		if (err)
			return err;
	}

	/* there might be a gap between the tx and rx qpl ids */
	start_id = gve_rx_start_qpl_id(&priv->tx_cfg);
	for (i = 0; i < num_rx_qpls; i++) {
		err = gve_register_qpl(priv, start_id + i);
		if (err)
			return err;
	}

	return 0;
}

static int gve_unregister_xdp_qpls(struct gve_priv *priv)
{
	int start_id;
	int err;
	int i;

	start_id = gve_xdp_tx_start_queue_id(priv);
	for (i = start_id; i < start_id + gve_num_xdp_qpls(priv); i++) {
		err = gve_unregister_qpl(priv, i);
		/* This failure will trigger a reset - no need to clean */
		if (err)
			return err;
	}
	return 0;
}

static int gve_unregister_qpls(struct gve_priv *priv)
{
	int num_tx_qpls, num_rx_qpls;
	int start_id;
	int err;
	int i;

	num_tx_qpls = gve_num_tx_qpls(&priv->tx_cfg, gve_num_xdp_qpls(priv),
				      gve_is_qpl(priv));
	num_rx_qpls = gve_num_rx_qpls(&priv->rx_cfg, gve_is_qpl(priv));

	for (i = 0; i < num_tx_qpls; i++) {
		err = gve_unregister_qpl(priv, i);
		/* This failure will trigger a reset - no need to clean */
		if (err)
			return err;
	}

	start_id = gve_rx_start_qpl_id(&priv->tx_cfg);
	for (i = 0; i < num_rx_qpls; i++) {
		err = gve_unregister_qpl(priv, start_id + i);
		/* This failure will trigger a reset - no need to clean */
		if (err)
			return err;
	}
	return 0;
}

static int gve_create_xdp_rings(struct gve_priv *priv)
{
	int err;

	err = gve_adminq_create_tx_queues(priv,
					  gve_xdp_tx_start_queue_id(priv),
					  priv->num_xdp_queues);
	if (err) {
		netif_err(priv, drv, priv->dev, "failed to create %d XDP tx queues\n",
			  priv->num_xdp_queues);
		/* This failure will trigger a reset - no need to clean
		 * up
		 */
		return err;
	}
	netif_dbg(priv, drv, priv->dev, "created %d XDP tx queues\n",
		  priv->num_xdp_queues);

	return 0;
}

static int gve_create_rings(struct gve_priv *priv)
{
	int num_tx_queues = gve_num_tx_queues(priv);
	int err;
	int i;

	err = gve_adminq_create_tx_queues(priv, 0, num_tx_queues);
	if (err) {
		netif_err(priv, drv, priv->dev, "failed to create %d tx queues\n",
			  num_tx_queues);
		/* This failure will trigger a reset - no need to clean
		 * up
		 */
		return err;
	}
	netif_dbg(priv, drv, priv->dev, "created %d tx queues\n",
		  num_tx_queues);

	err = gve_adminq_create_rx_queues(priv, priv->rx_cfg.num_queues);
	if (err) {
		netif_err(priv, drv, priv->dev, "failed to create %d rx queues\n",
			  priv->rx_cfg.num_queues);
		/* This failure will trigger a reset - no need to clean
		 * up
		 */
		return err;
	}
	netif_dbg(priv, drv, priv->dev, "created %d rx queues\n",
		  priv->rx_cfg.num_queues);

	if (gve_is_gqi(priv)) {
		/* Rx data ring has been prefilled with packet buffers at queue
		 * allocation time.
		 *
		 * Write the doorbell to provide descriptor slots and packet
		 * buffers to the NIC.
		 */
		for (i = 0; i < priv->rx_cfg.num_queues; i++)
			gve_rx_write_doorbell(priv, &priv->rx[i]);
	} else {
		for (i = 0; i < priv->rx_cfg.num_queues; i++) {
			/* Post buffers and ring doorbell. */
			gve_rx_post_buffers_dqo(&priv->rx[i]);
		}
	}

	return 0;
}

static void init_xdp_sync_stats(struct gve_priv *priv)
{
	int start_id = gve_xdp_tx_start_queue_id(priv);
	int i;

	/* Init stats */
	for (i = start_id; i < start_id + priv->num_xdp_queues; i++) {
		int ntfy_idx = gve_tx_idx_to_ntfy(priv, i);

		u64_stats_init(&priv->tx[i].statss);
		priv->tx[i].ntfy_id = ntfy_idx;
	}
}

static void gve_init_sync_stats(struct gve_priv *priv)
{
	int i;

	for (i = 0; i < priv->tx_cfg.num_queues; i++)
		u64_stats_init(&priv->tx[i].statss);

	/* Init stats for XDP TX queues */
	init_xdp_sync_stats(priv);

	for (i = 0; i < priv->rx_cfg.num_queues; i++)
		u64_stats_init(&priv->rx[i].statss);
}

static void gve_tx_get_curr_alloc_cfg(struct gve_priv *priv,
				      struct gve_tx_alloc_rings_cfg *cfg)
{
	cfg->qcfg = &priv->tx_cfg;
	cfg->raw_addressing = !gve_is_qpl(priv);
	cfg->qpls = priv->qpls;
	cfg->qpl_cfg = &priv->qpl_cfg;
	cfg->ring_size = priv->tx_desc_cnt;
	cfg->start_idx = 0;
	cfg->num_rings = gve_num_tx_queues(priv);
	cfg->tx = priv->tx;
}

static void gve_tx_stop_rings(struct gve_priv *priv, int start_id, int num_rings)
{
	int i;

	if (!priv->tx)
		return;

	for (i = start_id; i < start_id + num_rings; i++) {
		if (gve_is_gqi(priv))
			gve_tx_stop_ring_gqi(priv, i);
		else
			gve_tx_stop_ring_dqo(priv, i);
	}
}

static void gve_tx_start_rings(struct gve_priv *priv, int start_id,
			       int num_rings)
{
	int i;

	for (i = start_id; i < start_id + num_rings; i++) {
		if (gve_is_gqi(priv))
			gve_tx_start_ring_gqi(priv, i);
		else
			gve_tx_start_ring_dqo(priv, i);
	}
}

static int gve_alloc_xdp_rings(struct gve_priv *priv)
{
	struct gve_tx_alloc_rings_cfg cfg = {0};
	int err = 0;

	if (!priv->num_xdp_queues)
		return 0;

	gve_tx_get_curr_alloc_cfg(priv, &cfg);
	cfg.start_idx = gve_xdp_tx_start_queue_id(priv);
	cfg.num_rings = priv->num_xdp_queues;

	err = gve_tx_alloc_rings_gqi(priv, &cfg);
	if (err)
		return err;

	gve_tx_start_rings(priv, cfg.start_idx, cfg.num_rings);
	init_xdp_sync_stats(priv);

	return 0;
}

static int gve_alloc_rings(struct gve_priv *priv,
			   struct gve_tx_alloc_rings_cfg *tx_alloc_cfg,
			   struct gve_rx_alloc_rings_cfg *rx_alloc_cfg)
{
	int err;

	if (gve_is_gqi(priv))
		err = gve_tx_alloc_rings_gqi(priv, tx_alloc_cfg);
	else
		err = gve_tx_alloc_rings_dqo(priv, tx_alloc_cfg);
	if (err)
		return err;

	if (gve_is_gqi(priv))
		err = gve_rx_alloc_rings_gqi(priv, rx_alloc_cfg);
	else
		err = gve_rx_alloc_rings_dqo(priv, rx_alloc_cfg);
	if (err)
		goto free_tx;

	return 0;

free_tx:
	if (gve_is_gqi(priv))
		gve_tx_free_rings_gqi(priv, tx_alloc_cfg);
	else
		gve_tx_free_rings_dqo(priv, tx_alloc_cfg);
	return err;
}

static int gve_destroy_xdp_rings(struct gve_priv *priv)
{
	int start_id;
	int err;

	start_id = gve_xdp_tx_start_queue_id(priv);
	err = gve_adminq_destroy_tx_queues(priv,
					   start_id,
					   priv->num_xdp_queues);
	if (err) {
		netif_err(priv, drv, priv->dev,
			  "failed to destroy XDP queues\n");
		/* This failure will trigger a reset - no need to clean up */
		return err;
	}
	netif_dbg(priv, drv, priv->dev, "destroyed XDP queues\n");

	return 0;
}

static int gve_destroy_rings(struct gve_priv *priv)
{
	int num_tx_queues = gve_num_tx_queues(priv);
	int err;

	err = gve_adminq_destroy_tx_queues(priv, 0, num_tx_queues);
	if (err) {
		netif_err(priv, drv, priv->dev,
			  "failed to destroy tx queues\n");
		/* This failure will trigger a reset - no need to clean up */
		return err;
	}
	netif_dbg(priv, drv, priv->dev, "destroyed tx queues\n");
	err = gve_adminq_destroy_rx_queues(priv, priv->rx_cfg.num_queues);
	if (err) {
		netif_err(priv, drv, priv->dev,
			  "failed to destroy rx queues\n");
		/* This failure will trigger a reset - no need to clean up */
		return err;
	}
	netif_dbg(priv, drv, priv->dev, "destroyed rx queues\n");
	return 0;
}

static void gve_free_xdp_rings(struct gve_priv *priv)
{
	struct gve_tx_alloc_rings_cfg cfg = {0};

	gve_tx_get_curr_alloc_cfg(priv, &cfg);
	cfg.start_idx = gve_xdp_tx_start_queue_id(priv);
	cfg.num_rings = priv->num_xdp_queues;

	if (priv->tx) {
		gve_tx_stop_rings(priv, cfg.start_idx, cfg.num_rings);
		gve_tx_free_rings_gqi(priv, &cfg);
	}
}

static void gve_free_rings(struct gve_priv *priv,
			   struct gve_tx_alloc_rings_cfg *tx_cfg,
			   struct gve_rx_alloc_rings_cfg *rx_cfg)
{
	if (gve_is_gqi(priv)) {
		gve_tx_free_rings_gqi(priv, tx_cfg);
		gve_rx_free_rings_gqi(priv, rx_cfg);
	} else {
		gve_tx_free_rings_dqo(priv, tx_cfg);
		gve_rx_free_rings_dqo(priv, rx_cfg);
	}
}

int gve_alloc_page(struct gve_priv *priv, struct device *dev,
		   struct page **page, dma_addr_t *dma,
		   enum dma_data_direction dir, gfp_t gfp_flags)
{
	*page = alloc_page(gfp_flags);
	if (!*page) {
		priv->page_alloc_fail++;
		return -ENOMEM;
	}
	*dma = dma_map_page(dev, *page, 0, PAGE_SIZE, dir);
	if (dma_mapping_error(dev, *dma)) {
		priv->dma_mapping_error++;
		put_page(*page);
		return -ENOMEM;
	}
	return 0;
}

static int gve_alloc_queue_page_list(struct gve_priv *priv,
				     struct gve_queue_page_list *qpl,
				     u32 id, int pages)
{
	int err;
	int i;

	qpl->id = id;
	qpl->num_entries = 0;
	qpl->pages = kvcalloc(pages, sizeof(*qpl->pages), GFP_KERNEL);
	/* caller handles clean up */
	if (!qpl->pages)
		return -ENOMEM;
	qpl->page_buses = kvcalloc(pages, sizeof(*qpl->page_buses), GFP_KERNEL);
	/* caller handles clean up */
	if (!qpl->page_buses)
		return -ENOMEM;

	for (i = 0; i < pages; i++) {
		err = gve_alloc_page(priv, &priv->pdev->dev, &qpl->pages[i],
				     &qpl->page_buses[i],
				     gve_qpl_dma_dir(priv, id), GFP_KERNEL);
		/* caller handles clean up */
		if (err)
			return -ENOMEM;
		qpl->num_entries++;
	}

	return 0;
}

void gve_free_page(struct device *dev, struct page *page, dma_addr_t dma,
		   enum dma_data_direction dir)
{
	if (!dma_mapping_error(dev, dma))
		dma_unmap_page(dev, dma, PAGE_SIZE, dir);
	if (page)
		put_page(page);
}

static void gve_free_queue_page_list(struct gve_priv *priv,
				     struct gve_queue_page_list *qpl,
				     int id)
{
	int i;

	if (!qpl->pages)
		return;
	if (!qpl->page_buses)
		goto free_pages;

	for (i = 0; i < qpl->num_entries; i++)
		gve_free_page(&priv->pdev->dev, qpl->pages[i],
			      qpl->page_buses[i], gve_qpl_dma_dir(priv, id));

	kvfree(qpl->page_buses);
	qpl->page_buses = NULL;
free_pages:
	kvfree(qpl->pages);
	qpl->pages = NULL;
}

static void gve_free_n_qpls(struct gve_priv *priv,
			    struct gve_queue_page_list *qpls,
			    int start_id,
			    int num_qpls)
{
	int i;

	for (i = start_id; i < start_id + num_qpls; i++)
		gve_free_queue_page_list(priv, &qpls[i], i);
}

static int gve_alloc_n_qpls(struct gve_priv *priv,
			    struct gve_queue_page_list *qpls,
			    int page_count,
			    int start_id,
			    int num_qpls)
{
	int err;
	int i;

	for (i = start_id; i < start_id + num_qpls; i++) {
		err = gve_alloc_queue_page_list(priv, &qpls[i], i, page_count);
		if (err)
			goto free_qpls;
	}

	return 0;

free_qpls:
	/* Must include the failing QPL too for gve_alloc_queue_page_list fails
	 * without cleaning up.
	 */
	gve_free_n_qpls(priv, qpls, start_id, i - start_id + 1);
	return err;
}

static int gve_alloc_qpls(struct gve_priv *priv,
			  struct gve_qpls_alloc_cfg *cfg)
{
	int max_queues = cfg->tx_cfg->max_queues + cfg->rx_cfg->max_queues;
	int rx_start_id, tx_num_qpls, rx_num_qpls;
	struct gve_queue_page_list *qpls;
	int page_count;
	int err;

	if (cfg->raw_addressing)
		return 0;

	qpls = kvcalloc(max_queues, sizeof(*qpls), GFP_KERNEL);
	if (!qpls)
		return -ENOMEM;

	cfg->qpl_cfg->qpl_map_size = BITS_TO_LONGS(max_queues) *
		sizeof(unsigned long) * BITS_PER_BYTE;
	cfg->qpl_cfg->qpl_id_map = kvcalloc(BITS_TO_LONGS(max_queues),
					    sizeof(unsigned long), GFP_KERNEL);
	if (!cfg->qpl_cfg->qpl_id_map) {
		err = -ENOMEM;
		goto free_qpl_array;
	}

	/* Allocate TX QPLs */
	page_count = priv->tx_pages_per_qpl;
	tx_num_qpls = gve_num_tx_qpls(cfg->tx_cfg, cfg->num_xdp_queues,
				      gve_is_qpl(priv));
	err = gve_alloc_n_qpls(priv, qpls, page_count, 0, tx_num_qpls);
	if (err)
		goto free_qpl_map;

	/* Allocate RX QPLs */
	rx_start_id = gve_rx_start_qpl_id(cfg->tx_cfg);
	/* For GQI_QPL number of pages allocated have 1:1 relationship with
	 * number of descriptors. For DQO, number of pages required are
	 * more than descriptors (because of out of order completions).
	 */
	page_count = cfg->is_gqi ? priv->rx_data_slot_cnt : priv->rx_pages_per_qpl;
	rx_num_qpls = gve_num_rx_qpls(cfg->rx_cfg, gve_is_qpl(priv));
	err = gve_alloc_n_qpls(priv, qpls, page_count, rx_start_id, rx_num_qpls);
	if (err)
		goto free_tx_qpls;

	cfg->qpls = qpls;
	return 0;

free_tx_qpls:
	gve_free_n_qpls(priv, qpls, 0, tx_num_qpls);
free_qpl_map:
	kvfree(cfg->qpl_cfg->qpl_id_map);
	cfg->qpl_cfg->qpl_id_map = NULL;
free_qpl_array:
	kvfree(qpls);
	return err;
}

static void gve_free_qpls(struct gve_priv *priv,
			  struct gve_qpls_alloc_cfg *cfg)
{
	int max_queues = cfg->tx_cfg->max_queues + cfg->rx_cfg->max_queues;
	struct gve_queue_page_list *qpls = cfg->qpls;
	int i;

	if (!qpls)
		return;

	kvfree(cfg->qpl_cfg->qpl_id_map);
	cfg->qpl_cfg->qpl_id_map = NULL;

	for (i = 0; i < max_queues; i++)
		gve_free_queue_page_list(priv, &qpls[i], i);

	kvfree(qpls);
	cfg->qpls = NULL;
}

/* Use this to schedule a reset when the device is capable of continuing
 * to handle other requests in its current state. If it is not, do a reset
 * in thread instead.
 */
void gve_schedule_reset(struct gve_priv *priv)
{
	gve_set_do_reset(priv);
	queue_work(priv->gve_wq, &priv->service_task);
}

static void gve_reset_and_teardown(struct gve_priv *priv, bool was_up);
static int gve_reset_recovery(struct gve_priv *priv, bool was_up);
static void gve_turndown(struct gve_priv *priv);
static void gve_turnup(struct gve_priv *priv);

static int gve_reg_xdp_info(struct gve_priv *priv, struct net_device *dev)
{
	struct napi_struct *napi;
	struct gve_rx_ring *rx;
	int err = 0;
	int i, j;
	u32 tx_qid;

	if (!priv->num_xdp_queues)
		return 0;

	for (i = 0; i < priv->rx_cfg.num_queues; i++) {
		rx = &priv->rx[i];
		napi = &priv->ntfy_blocks[rx->ntfy_id].napi;

		err = xdp_rxq_info_reg(&rx->xdp_rxq, dev, i,
				       napi->napi_id);
		if (err)
			goto err;
		err = xdp_rxq_info_reg_mem_model(&rx->xdp_rxq,
						 MEM_TYPE_PAGE_SHARED, NULL);
		if (err)
			goto err;
		rx->xsk_pool = xsk_get_pool_from_qid(dev, i);
		if (rx->xsk_pool) {
			err = xdp_rxq_info_reg(&rx->xsk_rxq, dev, i,
					       napi->napi_id);
			if (err)
				goto err;
			err = xdp_rxq_info_reg_mem_model(&rx->xsk_rxq,
							 MEM_TYPE_XSK_BUFF_POOL, NULL);
			if (err)
				goto err;
			xsk_pool_set_rxq_info(rx->xsk_pool,
					      &rx->xsk_rxq);
		}
	}

	for (i = 0; i < priv->num_xdp_queues; i++) {
		tx_qid = gve_xdp_tx_queue_id(priv, i);
		priv->tx[tx_qid].xsk_pool = xsk_get_pool_from_qid(dev, i);
	}
	return 0;

err:
	for (j = i; j >= 0; j--) {
		rx = &priv->rx[j];
		if (xdp_rxq_info_is_reg(&rx->xdp_rxq))
			xdp_rxq_info_unreg(&rx->xdp_rxq);
		if (xdp_rxq_info_is_reg(&rx->xsk_rxq))
			xdp_rxq_info_unreg(&rx->xsk_rxq);
	}
	return err;
}

static void gve_unreg_xdp_info(struct gve_priv *priv)
{
	int i, tx_qid;

	if (!priv->num_xdp_queues)
		return;

	for (i = 0; i < priv->rx_cfg.num_queues; i++) {
		struct gve_rx_ring *rx = &priv->rx[i];

		xdp_rxq_info_unreg(&rx->xdp_rxq);
		if (rx->xsk_pool) {
			xdp_rxq_info_unreg(&rx->xsk_rxq);
			rx->xsk_pool = NULL;
		}
	}

	for (i = 0; i < priv->num_xdp_queues; i++) {
		tx_qid = gve_xdp_tx_queue_id(priv, i);
		priv->tx[tx_qid].xsk_pool = NULL;
	}
}

static void gve_drain_page_cache(struct gve_priv *priv)
{
	int i;

	for (i = 0; i < priv->rx_cfg.num_queues; i++)
		page_frag_cache_drain(&priv->rx[i].page_cache);
}

static void gve_qpls_get_curr_alloc_cfg(struct gve_priv *priv,
					struct gve_qpls_alloc_cfg *cfg)
{
	  cfg->raw_addressing = !gve_is_qpl(priv);
	  cfg->is_gqi = gve_is_gqi(priv);
	  cfg->num_xdp_queues = priv->num_xdp_queues;
	  cfg->qpl_cfg = &priv->qpl_cfg;
	  cfg->tx_cfg = &priv->tx_cfg;
	  cfg->rx_cfg = &priv->rx_cfg;
	  cfg->qpls = priv->qpls;
}

static void gve_rx_get_curr_alloc_cfg(struct gve_priv *priv,
				      struct gve_rx_alloc_rings_cfg *cfg)
{
	cfg->qcfg = &priv->rx_cfg;
	cfg->qcfg_tx = &priv->tx_cfg;
	cfg->raw_addressing = !gve_is_qpl(priv);
	cfg->enable_header_split = priv->header_split_enabled;
	cfg->qpls = priv->qpls;
	cfg->qpl_cfg = &priv->qpl_cfg;
	cfg->ring_size = priv->rx_desc_cnt;
	cfg->packet_buffer_size = gve_is_gqi(priv) ?
				  GVE_DEFAULT_RX_BUFFER_SIZE :
				  priv->data_buffer_size_dqo;
	cfg->rx = priv->rx;
}

static void gve_get_curr_alloc_cfgs(struct gve_priv *priv,
				    struct gve_qpls_alloc_cfg *qpls_alloc_cfg,
				    struct gve_tx_alloc_rings_cfg *tx_alloc_cfg,
				    struct gve_rx_alloc_rings_cfg *rx_alloc_cfg)
{
	gve_qpls_get_curr_alloc_cfg(priv, qpls_alloc_cfg);
	gve_tx_get_curr_alloc_cfg(priv, tx_alloc_cfg);
	gve_rx_get_curr_alloc_cfg(priv, rx_alloc_cfg);
}

static void gve_rx_start_rings(struct gve_priv *priv, int num_rings)
{
	int i;

	for (i = 0; i < num_rings; i++) {
		if (gve_is_gqi(priv))
			gve_rx_start_ring_gqi(priv, i);
		else
			gve_rx_start_ring_dqo(priv, i);
	}
}

static void gve_rx_stop_rings(struct gve_priv *priv, int num_rings)
{
	int i;

	if (!priv->rx)
		return;

	for (i = 0; i < num_rings; i++) {
		if (gve_is_gqi(priv))
			gve_rx_stop_ring_gqi(priv, i);
		else
			gve_rx_stop_ring_dqo(priv, i);
	}
}

static void gve_queues_mem_free(struct gve_priv *priv,
				struct gve_qpls_alloc_cfg *qpls_alloc_cfg,
				struct gve_tx_alloc_rings_cfg *tx_alloc_cfg,
				struct gve_rx_alloc_rings_cfg *rx_alloc_cfg)
{
	gve_free_rings(priv, tx_alloc_cfg, rx_alloc_cfg);
	gve_free_qpls(priv, qpls_alloc_cfg);
}

static int gve_queues_mem_alloc(struct gve_priv *priv,
				struct gve_qpls_alloc_cfg *qpls_alloc_cfg,
				struct gve_tx_alloc_rings_cfg *tx_alloc_cfg,
				struct gve_rx_alloc_rings_cfg *rx_alloc_cfg)
{
	int err;

	err = gve_alloc_qpls(priv, qpls_alloc_cfg);
	if (err) {
		netif_err(priv, drv, priv->dev, "Failed to alloc QPLs\n");
		return err;
	}
	tx_alloc_cfg->qpls = qpls_alloc_cfg->qpls;
	rx_alloc_cfg->qpls = qpls_alloc_cfg->qpls;
	err = gve_alloc_rings(priv, tx_alloc_cfg, rx_alloc_cfg);
	if (err) {
		netif_err(priv, drv, priv->dev, "Failed to alloc rings\n");
		goto free_qpls;
	}

	return 0;

free_qpls:
	gve_free_qpls(priv, qpls_alloc_cfg);
	return err;
}

static void gve_queues_mem_remove(struct gve_priv *priv)
{
	struct gve_tx_alloc_rings_cfg tx_alloc_cfg = {0};
	struct gve_rx_alloc_rings_cfg rx_alloc_cfg = {0};
	struct gve_qpls_alloc_cfg qpls_alloc_cfg = {0};

	gve_get_curr_alloc_cfgs(priv, &qpls_alloc_cfg,
				&tx_alloc_cfg, &rx_alloc_cfg);
	gve_queues_mem_free(priv, &qpls_alloc_cfg,
			    &tx_alloc_cfg, &rx_alloc_cfg);
	priv->qpls = NULL;
	priv->tx = NULL;
	priv->rx = NULL;
}

/* The passed-in queue memory is stored into priv and the queues are made live.
 * No memory is allocated. Passed-in memory is freed on errors.
 */
static int gve_queues_start(struct gve_priv *priv,
			    struct gve_qpls_alloc_cfg *qpls_alloc_cfg,
			    struct gve_tx_alloc_rings_cfg *tx_alloc_cfg,
			    struct gve_rx_alloc_rings_cfg *rx_alloc_cfg)
{
	struct net_device *dev = priv->dev;
	int err;

	/* Record new resources into priv */
	priv->qpls = qpls_alloc_cfg->qpls;
	priv->tx = tx_alloc_cfg->tx;
	priv->rx = rx_alloc_cfg->rx;

	/* Record new configs into priv */
	priv->qpl_cfg = *qpls_alloc_cfg->qpl_cfg;
	priv->tx_cfg = *tx_alloc_cfg->qcfg;
	priv->rx_cfg = *rx_alloc_cfg->qcfg;
	priv->tx_desc_cnt = tx_alloc_cfg->ring_size;
	priv->rx_desc_cnt = rx_alloc_cfg->ring_size;

	if (priv->xdp_prog)
		priv->num_xdp_queues = priv->rx_cfg.num_queues;
	else
		priv->num_xdp_queues = 0;

	gve_tx_start_rings(priv, 0, tx_alloc_cfg->num_rings);
	gve_rx_start_rings(priv, rx_alloc_cfg->qcfg->num_queues);
	gve_init_sync_stats(priv);

	err = netif_set_real_num_tx_queues(dev, priv->tx_cfg.num_queues);
	if (err)
		goto stop_and_free_rings;
	err = netif_set_real_num_rx_queues(dev, priv->rx_cfg.num_queues);
	if (err)
		goto stop_and_free_rings;

	err = gve_reg_xdp_info(priv, dev);
	if (err)
		goto stop_and_free_rings;

	err = gve_register_qpls(priv);
	if (err)
		goto reset;

	priv->header_split_enabled = rx_alloc_cfg->enable_header_split;
	priv->data_buffer_size_dqo = rx_alloc_cfg->packet_buffer_size;

	err = gve_create_rings(priv);
	if (err)
		goto reset;

	gve_set_device_rings_ok(priv);

	if (gve_get_report_stats(priv))
		mod_timer(&priv->stats_report_timer,
			  round_jiffies(jiffies +
				msecs_to_jiffies(priv->stats_report_timer_period)));

	gve_turnup(priv);
	queue_work(priv->gve_wq, &priv->service_task);
	priv->interface_up_cnt++;
	return 0;

reset:
	if (gve_get_reset_in_progress(priv))
		goto stop_and_free_rings;
	gve_reset_and_teardown(priv, true);
	/* if this fails there is nothing we can do so just ignore the return */
	gve_reset_recovery(priv, false);
	/* return the original error */
	return err;
stop_and_free_rings:
	gve_tx_stop_rings(priv, 0, gve_num_tx_queues(priv));
	gve_rx_stop_rings(priv, priv->rx_cfg.num_queues);
	gve_queues_mem_remove(priv);
	return err;
}

static int gve_open(struct net_device *dev)
{
	struct gve_tx_alloc_rings_cfg tx_alloc_cfg = {0};
	struct gve_rx_alloc_rings_cfg rx_alloc_cfg = {0};
	struct gve_qpls_alloc_cfg qpls_alloc_cfg = {0};
	struct gve_priv *priv = netdev_priv(dev);
	int err;

	gve_get_curr_alloc_cfgs(priv, &qpls_alloc_cfg,
				&tx_alloc_cfg, &rx_alloc_cfg);

	err = gve_queues_mem_alloc(priv, &qpls_alloc_cfg,
				   &tx_alloc_cfg, &rx_alloc_cfg);
	if (err)
		return err;

	/* No need to free on error: ownership of resources is lost after
	 * calling gve_queues_start.
	 */
	err = gve_queues_start(priv, &qpls_alloc_cfg,
			       &tx_alloc_cfg, &rx_alloc_cfg);
	if (err)
		return err;

	return 0;
}

static int gve_queues_stop(struct gve_priv *priv)
{
	int err;

	netif_carrier_off(priv->dev);
	if (gve_get_device_rings_ok(priv)) {
		gve_turndown(priv);
		gve_drain_page_cache(priv);
		err = gve_destroy_rings(priv);
		if (err)
			goto err;
		err = gve_unregister_qpls(priv);
		if (err)
			goto err;
		gve_clear_device_rings_ok(priv);
	}
	del_timer_sync(&priv->stats_report_timer);

	gve_unreg_xdp_info(priv);

	gve_tx_stop_rings(priv, 0, gve_num_tx_queues(priv));
	gve_rx_stop_rings(priv, priv->rx_cfg.num_queues);

	priv->interface_down_cnt++;
	return 0;

err:
	/* This must have been called from a reset due to the rtnl lock
	 * so just return at this point.
	 */
	if (gve_get_reset_in_progress(priv))
		return err;
	/* Otherwise reset before returning */
	gve_reset_and_teardown(priv, true);
	return gve_reset_recovery(priv, false);
}

static int gve_close(struct net_device *dev)
{
	struct gve_priv *priv = netdev_priv(dev);
	int err;

	err = gve_queues_stop(priv);
	if (err)
		return err;

	gve_queues_mem_remove(priv);
	return 0;
}

static int gve_remove_xdp_queues(struct gve_priv *priv)
{
	int qpl_start_id;
	int err;

	qpl_start_id = gve_xdp_tx_start_queue_id(priv);

	err = gve_destroy_xdp_rings(priv);
	if (err)
		return err;

	err = gve_unregister_xdp_qpls(priv);
	if (err)
		return err;

	gve_unreg_xdp_info(priv);
	gve_free_xdp_rings(priv);

	gve_free_n_qpls(priv, priv->qpls, qpl_start_id, gve_num_xdp_qpls(priv));
	priv->num_xdp_queues = 0;
	return 0;
}

static int gve_add_xdp_queues(struct gve_priv *priv)
{
	int start_id;
	int err;

	priv->num_xdp_queues = priv->rx_cfg.num_queues;

	start_id = gve_xdp_tx_start_queue_id(priv);
	err = gve_alloc_n_qpls(priv, priv->qpls, priv->tx_pages_per_qpl,
			       start_id, gve_num_xdp_qpls(priv));
	if (err)
		goto err;

	err = gve_alloc_xdp_rings(priv);
	if (err)
		goto free_xdp_qpls;

	err = gve_reg_xdp_info(priv, priv->dev);
	if (err)
		goto free_xdp_rings;

	err = gve_register_xdp_qpls(priv);
	if (err)
		goto free_xdp_rings;

	err = gve_create_xdp_rings(priv);
	if (err)
		goto free_xdp_rings;

	return 0;

free_xdp_rings:
	gve_free_xdp_rings(priv);
free_xdp_qpls:
	gve_free_n_qpls(priv, priv->qpls, start_id, gve_num_xdp_qpls(priv));
err:
	priv->num_xdp_queues = 0;
	return err;
}

static void gve_handle_link_status(struct gve_priv *priv, bool link_status)
{
	if (!gve_get_napi_enabled(priv))
		return;

	if (link_status == netif_carrier_ok(priv->dev))
		return;

	if (link_status) {
		netdev_info(priv->dev, "Device link is up.\n");
		netif_carrier_on(priv->dev);
	} else {
		netdev_info(priv->dev, "Device link is down.\n");
		netif_carrier_off(priv->dev);
	}
}

static int gve_set_xdp(struct gve_priv *priv, struct bpf_prog *prog,
		       struct netlink_ext_ack *extack)
{
	struct bpf_prog *old_prog;
	int err = 0;
	u32 status;

	old_prog = READ_ONCE(priv->xdp_prog);
	if (!netif_carrier_ok(priv->dev)) {
		WRITE_ONCE(priv->xdp_prog, prog);
		if (old_prog)
			bpf_prog_put(old_prog);
		return 0;
	}

	gve_turndown(priv);
	if (!old_prog && prog) {
		// Allocate XDP TX queues if an XDP program is
		// being installed
		err = gve_add_xdp_queues(priv);
		if (err)
			goto out;
	} else if (old_prog && !prog) {
		// Remove XDP TX queues if an XDP program is
		// being uninstalled
		err = gve_remove_xdp_queues(priv);
		if (err)
			goto out;
	}
	WRITE_ONCE(priv->xdp_prog, prog);
	if (old_prog)
		bpf_prog_put(old_prog);

out:
	gve_turnup(priv);
	status = ioread32be(&priv->reg_bar0->device_status);
	gve_handle_link_status(priv, GVE_DEVICE_STATUS_LINK_STATUS_MASK & status);
	return err;
}

static int gve_xsk_pool_enable(struct net_device *dev,
			       struct xsk_buff_pool *pool,
			       u16 qid)
{
	struct gve_priv *priv = netdev_priv(dev);
	struct napi_struct *napi;
	struct gve_rx_ring *rx;
	int tx_qid;
	int err;

	if (qid >= priv->rx_cfg.num_queues) {
		dev_err(&priv->pdev->dev, "xsk pool invalid qid %d", qid);
		return -EINVAL;
	}
	if (xsk_pool_get_rx_frame_size(pool) <
	     priv->dev->max_mtu + sizeof(struct ethhdr)) {
		dev_err(&priv->pdev->dev, "xsk pool frame_len too small");
		return -EINVAL;
	}

	err = xsk_pool_dma_map(pool, &priv->pdev->dev,
			       DMA_ATTR_SKIP_CPU_SYNC | DMA_ATTR_WEAK_ORDERING);
	if (err)
		return err;

	/* If XDP prog is not installed, return */
	if (!priv->xdp_prog)
		return 0;

	rx = &priv->rx[qid];
	napi = &priv->ntfy_blocks[rx->ntfy_id].napi;
	err = xdp_rxq_info_reg(&rx->xsk_rxq, dev, qid, napi->napi_id);
	if (err)
		goto err;

	err = xdp_rxq_info_reg_mem_model(&rx->xsk_rxq,
					 MEM_TYPE_XSK_BUFF_POOL, NULL);
	if (err)
		goto err;

	xsk_pool_set_rxq_info(pool, &rx->xsk_rxq);
	rx->xsk_pool = pool;

	tx_qid = gve_xdp_tx_queue_id(priv, qid);
	priv->tx[tx_qid].xsk_pool = pool;

	return 0;
err:
	if (xdp_rxq_info_is_reg(&rx->xsk_rxq))
		xdp_rxq_info_unreg(&rx->xsk_rxq);

	xsk_pool_dma_unmap(pool,
			   DMA_ATTR_SKIP_CPU_SYNC | DMA_ATTR_WEAK_ORDERING);
	return err;
}

static int gve_xsk_pool_disable(struct net_device *dev,
				u16 qid)
{
	struct gve_priv *priv = netdev_priv(dev);
	struct napi_struct *napi_rx;
	struct napi_struct *napi_tx;
	struct xsk_buff_pool *pool;
	int tx_qid;

	pool = xsk_get_pool_from_qid(dev, qid);
	if (!pool)
		return -EINVAL;
	if (qid >= priv->rx_cfg.num_queues)
		return -EINVAL;

	/* If XDP prog is not installed, unmap DMA and return */
	if (!priv->xdp_prog)
		goto done;

	tx_qid = gve_xdp_tx_queue_id(priv, qid);
	if (!netif_running(dev)) {
		priv->rx[qid].xsk_pool = NULL;
		xdp_rxq_info_unreg(&priv->rx[qid].xsk_rxq);
		priv->tx[tx_qid].xsk_pool = NULL;
		goto done;
	}

	napi_rx = &priv->ntfy_blocks[priv->rx[qid].ntfy_id].napi;
	napi_disable(napi_rx); /* make sure current rx poll is done */

	napi_tx = &priv->ntfy_blocks[priv->tx[tx_qid].ntfy_id].napi;
	napi_disable(napi_tx); /* make sure current tx poll is done */

	priv->rx[qid].xsk_pool = NULL;
	xdp_rxq_info_unreg(&priv->rx[qid].xsk_rxq);
	priv->tx[tx_qid].xsk_pool = NULL;
	smp_mb(); /* Make sure it is visible to the workers on datapath */

	napi_enable(napi_rx);
	if (gve_rx_work_pending(&priv->rx[qid]))
		napi_schedule(napi_rx);

	napi_enable(napi_tx);
	if (gve_tx_clean_pending(priv, &priv->tx[tx_qid]))
		napi_schedule(napi_tx);

done:
	xsk_pool_dma_unmap(pool,
			   DMA_ATTR_SKIP_CPU_SYNC | DMA_ATTR_WEAK_ORDERING);
	return 0;
}

static int gve_xsk_wakeup(struct net_device *dev, u32 queue_id, u32 flags)
{
	struct gve_priv *priv = netdev_priv(dev);
	int tx_queue_id = gve_xdp_tx_queue_id(priv, queue_id);

	if (queue_id >= priv->rx_cfg.num_queues || !priv->xdp_prog)
		return -EINVAL;

	if (flags & XDP_WAKEUP_TX) {
		struct gve_tx_ring *tx = &priv->tx[tx_queue_id];
		struct napi_struct *napi =
			&priv->ntfy_blocks[tx->ntfy_id].napi;

		if (!napi_if_scheduled_mark_missed(napi)) {
			/* Call local_bh_enable to trigger SoftIRQ processing */
			local_bh_disable();
			napi_schedule(napi);
			local_bh_enable();
		}

		tx->xdp_xsk_wakeup++;
	}

	return 0;
}

static int verify_xdp_configuration(struct net_device *dev)
{
	struct gve_priv *priv = netdev_priv(dev);

	if (dev->features & NETIF_F_LRO) {
		netdev_warn(dev, "XDP is not supported when LRO is on.\n");
		return -EOPNOTSUPP;
	}

	if (priv->queue_format != GVE_GQI_QPL_FORMAT) {
		netdev_warn(dev, "XDP is not supported in mode %d.\n",
			    priv->queue_format);
		return -EOPNOTSUPP;
	}

	if (dev->mtu > GVE_DEFAULT_RX_BUFFER_SIZE - sizeof(struct ethhdr) - GVE_RX_PAD) {
		netdev_warn(dev, "XDP is not supported for mtu %d.\n",
			    dev->mtu);
		return -EOPNOTSUPP;
	}

	if (priv->rx_cfg.num_queues != priv->tx_cfg.num_queues ||
	    (2 * priv->tx_cfg.num_queues > priv->tx_cfg.max_queues)) {
		netdev_warn(dev, "XDP load failed: The number of configured RX queues %d should be equal to the number of configured TX queues %d and the number of configured RX/TX queues should be less than or equal to half the maximum number of RX/TX queues %d",
			    priv->rx_cfg.num_queues,
			    priv->tx_cfg.num_queues,
			    priv->tx_cfg.max_queues);
		return -EINVAL;
	}
	return 0;
}

static int gve_xdp(struct net_device *dev, struct netdev_bpf *xdp)
{
	struct gve_priv *priv = netdev_priv(dev);
	int err;

	err = verify_xdp_configuration(dev);
	if (err)
		return err;
	switch (xdp->command) {
	case XDP_SETUP_PROG:
		return gve_set_xdp(priv, xdp->prog, xdp->extack);
	case XDP_SETUP_XSK_POOL:
		if (xdp->xsk.pool)
			return gve_xsk_pool_enable(dev, xdp->xsk.pool, xdp->xsk.queue_id);
		else
			return gve_xsk_pool_disable(dev, xdp->xsk.queue_id);
	default:
		return -EINVAL;
	}
}

static int gve_adjust_config(struct gve_priv *priv,
			     struct gve_qpls_alloc_cfg *qpls_alloc_cfg,
			     struct gve_tx_alloc_rings_cfg *tx_alloc_cfg,
			     struct gve_rx_alloc_rings_cfg *rx_alloc_cfg)
{
	int err;

	/* Allocate resources for the new confiugration */
	err = gve_queues_mem_alloc(priv, qpls_alloc_cfg,
				   tx_alloc_cfg, rx_alloc_cfg);
	if (err) {
		netif_err(priv, drv, priv->dev,
			  "Adjust config failed to alloc new queues");
		return err;
	}

	/* Teardown the device and free existing resources */
	err = gve_close(priv->dev);
	if (err) {
		netif_err(priv, drv, priv->dev,
			  "Adjust config failed to close old queues");
		gve_queues_mem_free(priv, qpls_alloc_cfg,
				    tx_alloc_cfg, rx_alloc_cfg);
		return err;
	}

	/* Bring the device back up again with the new resources. */
	err = gve_queues_start(priv, qpls_alloc_cfg,
			       tx_alloc_cfg, rx_alloc_cfg);
	if (err) {
		netif_err(priv, drv, priv->dev,
			  "Adjust config failed to start new queues, !!! DISABLING ALL QUEUES !!!\n");
		/* No need to free on error: ownership of resources is lost after
		 * calling gve_queues_start.
		 */
		gve_turndown(priv);
		return err;
	}

	return 0;
}

int gve_adjust_queues(struct gve_priv *priv,
		      struct gve_queue_config new_rx_config,
		      struct gve_queue_config new_tx_config)
{
	struct gve_tx_alloc_rings_cfg tx_alloc_cfg = {0};
	struct gve_rx_alloc_rings_cfg rx_alloc_cfg = {0};
	struct gve_qpls_alloc_cfg qpls_alloc_cfg = {0};
	struct gve_qpl_config new_qpl_cfg;
	int err;

	gve_get_curr_alloc_cfgs(priv, &qpls_alloc_cfg,
				&tx_alloc_cfg, &rx_alloc_cfg);

	/* qpl_cfg is not read-only, it contains a map that gets updated as
	 * rings are allocated, which is why we cannot use the yet unreleased
	 * one in priv.
	 */
	qpls_alloc_cfg.qpl_cfg = &new_qpl_cfg;
	tx_alloc_cfg.qpl_cfg = &new_qpl_cfg;
	rx_alloc_cfg.qpl_cfg = &new_qpl_cfg;

	/* Relay the new config from ethtool */
	qpls_alloc_cfg.tx_cfg = &new_tx_config;
	tx_alloc_cfg.qcfg = &new_tx_config;
	rx_alloc_cfg.qcfg_tx = &new_tx_config;
	qpls_alloc_cfg.rx_cfg = &new_rx_config;
	rx_alloc_cfg.qcfg = &new_rx_config;
	tx_alloc_cfg.num_rings = new_tx_config.num_queues;

	if (netif_carrier_ok(priv->dev)) {
		err = gve_adjust_config(priv, &qpls_alloc_cfg,
					&tx_alloc_cfg, &rx_alloc_cfg);
		return err;
	}
	/* Set the config for the next up. */
	priv->tx_cfg = new_tx_config;
	priv->rx_cfg = new_rx_config;

	return 0;
}

static void gve_turndown(struct gve_priv *priv)
{
	int idx;

	if (netif_carrier_ok(priv->dev))
		netif_carrier_off(priv->dev);

	if (!gve_get_napi_enabled(priv))
		return;

	/* Disable napi to prevent more work from coming in */
	for (idx = 0; idx < gve_num_tx_queues(priv); idx++) {
		int ntfy_idx = gve_tx_idx_to_ntfy(priv, idx);
		struct gve_notify_block *block = &priv->ntfy_blocks[ntfy_idx];

		napi_disable(&block->napi);
	}
	for (idx = 0; idx < priv->rx_cfg.num_queues; idx++) {
		int ntfy_idx = gve_rx_idx_to_ntfy(priv, idx);
		struct gve_notify_block *block = &priv->ntfy_blocks[ntfy_idx];

		napi_disable(&block->napi);
	}

	/* Stop tx queues */
	netif_tx_disable(priv->dev);

	gve_clear_napi_enabled(priv);
	gve_clear_report_stats(priv);
}

static void gve_turnup(struct gve_priv *priv)
{
	int idx;

	/* Start the tx queues */
	netif_tx_start_all_queues(priv->dev);

	/* Enable napi and unmask interrupts for all queues */
	for (idx = 0; idx < gve_num_tx_queues(priv); idx++) {
		int ntfy_idx = gve_tx_idx_to_ntfy(priv, idx);
		struct gve_notify_block *block = &priv->ntfy_blocks[ntfy_idx];

		napi_enable(&block->napi);
		if (gve_is_gqi(priv)) {
			iowrite32be(0, gve_irq_doorbell(priv, block));
		} else {
			gve_set_itr_coalesce_usecs_dqo(priv, block,
						       priv->tx_coalesce_usecs);
		}
	}
	for (idx = 0; idx < priv->rx_cfg.num_queues; idx++) {
		int ntfy_idx = gve_rx_idx_to_ntfy(priv, idx);
		struct gve_notify_block *block = &priv->ntfy_blocks[ntfy_idx];

		napi_enable(&block->napi);
		if (gve_is_gqi(priv)) {
			iowrite32be(0, gve_irq_doorbell(priv, block));
		} else {
			gve_set_itr_coalesce_usecs_dqo(priv, block,
						       priv->rx_coalesce_usecs);
		}
	}

	gve_set_napi_enabled(priv);
}

static void gve_tx_timeout(struct net_device *dev, unsigned int txqueue)
{
	struct gve_notify_block *block;
	struct gve_tx_ring *tx = NULL;
	struct gve_priv *priv;
	u32 last_nic_done;
	u32 current_time;
	u32 ntfy_idx;

	netdev_info(dev, "Timeout on tx queue, %d", txqueue);
	priv = netdev_priv(dev);
	if (txqueue > priv->tx_cfg.num_queues)
		goto reset;

	ntfy_idx = gve_tx_idx_to_ntfy(priv, txqueue);
	if (ntfy_idx >= priv->num_ntfy_blks)
		goto reset;

	block = &priv->ntfy_blocks[ntfy_idx];
	tx = block->tx;

	current_time = jiffies_to_msecs(jiffies);
	if (tx->last_kick_msec + MIN_TX_TIMEOUT_GAP > current_time)
		goto reset;

	/* Check to see if there are missed completions, which will allow us to
	 * kick the queue.
	 */
	last_nic_done = gve_tx_load_event_counter(priv, tx);
	if (last_nic_done - tx->done) {
		netdev_info(dev, "Kicking queue %d", txqueue);
		iowrite32be(GVE_IRQ_MASK, gve_irq_doorbell(priv, block));
		napi_schedule(&block->napi);
		tx->last_kick_msec = current_time;
		goto out;
	} // Else reset.

reset:
	gve_schedule_reset(priv);

out:
	if (tx)
		tx->queue_timeout++;
	priv->tx_timeo_cnt++;
}

u16 gve_get_pkt_buf_size(const struct gve_priv *priv, bool enable_hsplit)
{
	if (enable_hsplit && priv->max_rx_buffer_size >= GVE_MAX_RX_BUFFER_SIZE)
		return GVE_MAX_RX_BUFFER_SIZE;
	else
		return GVE_DEFAULT_RX_BUFFER_SIZE;
}

/* header-split is not supported on non-DQO_RDA yet even if device advertises it */
bool gve_header_split_supported(const struct gve_priv *priv)
{
	return priv->header_buf_size && priv->queue_format == GVE_DQO_RDA_FORMAT;
}

int gve_set_hsplit_config(struct gve_priv *priv, u8 tcp_data_split)
{
	struct gve_tx_alloc_rings_cfg tx_alloc_cfg = {0};
	struct gve_rx_alloc_rings_cfg rx_alloc_cfg = {0};
	struct gve_qpls_alloc_cfg qpls_alloc_cfg = {0};
	bool enable_hdr_split;
	int err = 0;

	if (tcp_data_split == ETHTOOL_TCP_DATA_SPLIT_UNKNOWN)
		return 0;

	if (!gve_header_split_supported(priv)) {
		dev_err(&priv->pdev->dev, "Header-split not supported\n");
		return -EOPNOTSUPP;
	}

	if (tcp_data_split == ETHTOOL_TCP_DATA_SPLIT_ENABLED)
		enable_hdr_split = true;
	else
		enable_hdr_split = false;

	if (enable_hdr_split == priv->header_split_enabled)
		return 0;

	gve_get_curr_alloc_cfgs(priv, &qpls_alloc_cfg,
				&tx_alloc_cfg, &rx_alloc_cfg);

	rx_alloc_cfg.enable_header_split = enable_hdr_split;
	rx_alloc_cfg.packet_buffer_size = gve_get_pkt_buf_size(priv, enable_hdr_split);

	if (netif_running(priv->dev))
		err = gve_adjust_config(priv, &qpls_alloc_cfg,
					&tx_alloc_cfg, &rx_alloc_cfg);
	return err;
}

static int gve_set_features(struct net_device *netdev,
			    netdev_features_t features)
{
	const netdev_features_t orig_features = netdev->features;
	struct gve_tx_alloc_rings_cfg tx_alloc_cfg = {0};
	struct gve_rx_alloc_rings_cfg rx_alloc_cfg = {0};
	struct gve_qpls_alloc_cfg qpls_alloc_cfg = {0};
	struct gve_priv *priv = netdev_priv(netdev);
	struct gve_qpl_config new_qpl_cfg;
	int err;

	gve_get_curr_alloc_cfgs(priv, &qpls_alloc_cfg,
				&tx_alloc_cfg, &rx_alloc_cfg);
	/* qpl_cfg is not read-only, it contains a map that gets updated as
	 * rings are allocated, which is why we cannot use the yet unreleased
	 * one in priv.
	 */
	qpls_alloc_cfg.qpl_cfg = &new_qpl_cfg;
	tx_alloc_cfg.qpl_cfg = &new_qpl_cfg;
	rx_alloc_cfg.qpl_cfg = &new_qpl_cfg;

	if ((netdev->features & NETIF_F_LRO) != (features & NETIF_F_LRO)) {
		netdev->features ^= NETIF_F_LRO;
		if (netif_carrier_ok(netdev)) {
			err = gve_adjust_config(priv, &qpls_alloc_cfg,
						&tx_alloc_cfg, &rx_alloc_cfg);
			if (err) {
				/* Revert the change on error. */
				netdev->features = orig_features;
				return err;
			}
		}
	}

	return 0;
}

static const struct net_device_ops gve_netdev_ops = {
	.ndo_start_xmit		=	gve_start_xmit,
	.ndo_features_check	=	gve_features_check,
	.ndo_open		=	gve_open,
	.ndo_stop		=	gve_close,
	.ndo_get_stats64	=	gve_get_stats,
	.ndo_tx_timeout         =       gve_tx_timeout,
	.ndo_set_features	=	gve_set_features,
	.ndo_bpf		=	gve_xdp,
	.ndo_xdp_xmit		=	gve_xdp_xmit,
	.ndo_xsk_wakeup		=	gve_xsk_wakeup,
};

static void gve_handle_status(struct gve_priv *priv, u32 status)
{
	if (GVE_DEVICE_STATUS_RESET_MASK & status) {
		dev_info(&priv->pdev->dev, "Device requested reset.\n");
		gve_set_do_reset(priv);
	}
	if (GVE_DEVICE_STATUS_REPORT_STATS_MASK & status) {
		priv->stats_report_trigger_cnt++;
		gve_set_do_report_stats(priv);
	}
}

static void gve_handle_reset(struct gve_priv *priv)
{
	/* A service task will be scheduled at the end of probe to catch any
	 * resets that need to happen, and we don't want to reset until
	 * probe is done.
	 */
	if (gve_get_probe_in_progress(priv))
		return;

	if (gve_get_do_reset(priv)) {
		rtnl_lock();
		gve_reset(priv, false);
		rtnl_unlock();
	}
}

void gve_handle_report_stats(struct gve_priv *priv)
{
	struct stats *stats = priv->stats_report->stats;
	int idx, stats_idx = 0;
	unsigned int start = 0;
	u64 tx_bytes;

	if (!gve_get_report_stats(priv))
		return;

	be64_add_cpu(&priv->stats_report->written_count, 1);
	/* tx stats */
	if (priv->tx) {
		for (idx = 0; idx < gve_num_tx_queues(priv); idx++) {
			u32 last_completion = 0;
			u32 tx_frames = 0;

			/* DQO doesn't currently support these metrics. */
			if (gve_is_gqi(priv)) {
				last_completion = priv->tx[idx].done;
				tx_frames = priv->tx[idx].req;
			}

			do {
				start = u64_stats_fetch_begin(&priv->tx[idx].statss);
				tx_bytes = priv->tx[idx].bytes_done;
			} while (u64_stats_fetch_retry(&priv->tx[idx].statss, start));
			stats[stats_idx++] = (struct stats) {
				.stat_name = cpu_to_be32(TX_WAKE_CNT),
				.value = cpu_to_be64(priv->tx[idx].wake_queue),
				.queue_id = cpu_to_be32(idx),
			};
			stats[stats_idx++] = (struct stats) {
				.stat_name = cpu_to_be32(TX_STOP_CNT),
				.value = cpu_to_be64(priv->tx[idx].stop_queue),
				.queue_id = cpu_to_be32(idx),
			};
			stats[stats_idx++] = (struct stats) {
				.stat_name = cpu_to_be32(TX_FRAMES_SENT),
				.value = cpu_to_be64(tx_frames),
				.queue_id = cpu_to_be32(idx),
			};
			stats[stats_idx++] = (struct stats) {
				.stat_name = cpu_to_be32(TX_BYTES_SENT),
				.value = cpu_to_be64(tx_bytes),
				.queue_id = cpu_to_be32(idx),
			};
			stats[stats_idx++] = (struct stats) {
				.stat_name = cpu_to_be32(TX_LAST_COMPLETION_PROCESSED),
				.value = cpu_to_be64(last_completion),
				.queue_id = cpu_to_be32(idx),
			};
			stats[stats_idx++] = (struct stats) {
				.stat_name = cpu_to_be32(TX_TIMEOUT_CNT),
				.value = cpu_to_be64(priv->tx[idx].queue_timeout),
				.queue_id = cpu_to_be32(idx),
			};
		}
	}
	/* rx stats */
	if (priv->rx) {
		for (idx = 0; idx < priv->rx_cfg.num_queues; idx++) {
			stats[stats_idx++] = (struct stats) {
				.stat_name = cpu_to_be32(RX_NEXT_EXPECTED_SEQUENCE),
				.value = cpu_to_be64(priv->rx[idx].desc.seqno),
				.queue_id = cpu_to_be32(idx),
			};
			stats[stats_idx++] = (struct stats) {
				.stat_name = cpu_to_be32(RX_BUFFERS_POSTED),
				.value = cpu_to_be64(priv->rx[0].fill_cnt),
				.queue_id = cpu_to_be32(idx),
			};
		}
	}
}

/* Handle NIC status register changes, reset requests and report stats */
static void gve_service_task(struct work_struct *work)
{
	struct gve_priv *priv = container_of(work, struct gve_priv,
					     service_task);
	u32 status = ioread32be(&priv->reg_bar0->device_status);

	gve_handle_status(priv, status);

	gve_handle_reset(priv);
	gve_handle_link_status(priv, GVE_DEVICE_STATUS_LINK_STATUS_MASK & status);
}

static void gve_set_netdev_xdp_features(struct gve_priv *priv)
{
	if (priv->queue_format == GVE_GQI_QPL_FORMAT) {
		priv->dev->xdp_features = NETDEV_XDP_ACT_BASIC;
		priv->dev->xdp_features |= NETDEV_XDP_ACT_REDIRECT;
		priv->dev->xdp_features |= NETDEV_XDP_ACT_NDO_XMIT;
		priv->dev->xdp_features |= NETDEV_XDP_ACT_XSK_ZEROCOPY;
	} else {
		priv->dev->xdp_features = 0;
	}
}

static int gve_init_priv(struct gve_priv *priv, bool skip_describe_device)
{
	int num_ntfy;
	int err;

	/* Set up the adminq */
	err = gve_adminq_alloc(&priv->pdev->dev, priv);
	if (err) {
		dev_err(&priv->pdev->dev,
			"Failed to alloc admin queue: err=%d\n", err);
		return err;
	}

	err = gve_verify_driver_compatibility(priv);
	if (err) {
		dev_err(&priv->pdev->dev,
			"Could not verify driver compatibility: err=%d\n", err);
		goto err;
	}

	priv->num_registered_pages = 0;

	if (skip_describe_device)
		goto setup_device;

	priv->queue_format = GVE_QUEUE_FORMAT_UNSPECIFIED;
	/* Get the initial information we need from the device */
	err = gve_adminq_describe_device(priv);
	if (err) {
		dev_err(&priv->pdev->dev,
			"Could not get device information: err=%d\n", err);
		goto err;
	}
	priv->dev->mtu = priv->dev->max_mtu;
	num_ntfy = pci_msix_vec_count(priv->pdev);
	if (num_ntfy <= 0) {
		dev_err(&priv->pdev->dev,
			"could not count MSI-x vectors: err=%d\n", num_ntfy);
		err = num_ntfy;
		goto err;
	} else if (num_ntfy < GVE_MIN_MSIX) {
		dev_err(&priv->pdev->dev, "gve needs at least %d MSI-x vectors, but only has %d\n",
			GVE_MIN_MSIX, num_ntfy);
		err = -EINVAL;
		goto err;
	}

	/* Big TCP is only supported on DQ*/
	if (!gve_is_gqi(priv))
		netif_set_tso_max_size(priv->dev, GVE_DQO_TX_MAX);

	priv->rx_copybreak = GVE_DEFAULT_RX_COPYBREAK;
	/* gvnic has one Notification Block per MSI-x vector, except for the
	 * management vector
	 */
	priv->num_ntfy_blks = (num_ntfy - 1) & ~0x1;
	priv->mgmt_msix_idx = priv->num_ntfy_blks;

	priv->tx_cfg.max_queues =
		min_t(int, priv->tx_cfg.max_queues, priv->num_ntfy_blks / 2);
	priv->rx_cfg.max_queues =
		min_t(int, priv->rx_cfg.max_queues, priv->num_ntfy_blks / 2);

	priv->tx_cfg.num_queues = priv->tx_cfg.max_queues;
	priv->rx_cfg.num_queues = priv->rx_cfg.max_queues;
	if (priv->default_num_queues > 0) {
		priv->tx_cfg.num_queues = min_t(int, priv->default_num_queues,
						priv->tx_cfg.num_queues);
		priv->rx_cfg.num_queues = min_t(int, priv->default_num_queues,
						priv->rx_cfg.num_queues);
	}

	dev_info(&priv->pdev->dev, "TX queues %d, RX queues %d\n",
		 priv->tx_cfg.num_queues, priv->rx_cfg.num_queues);
	dev_info(&priv->pdev->dev, "Max TX queues %d, Max RX queues %d\n",
		 priv->tx_cfg.max_queues, priv->rx_cfg.max_queues);

	if (!gve_is_gqi(priv)) {
		priv->tx_coalesce_usecs = GVE_TX_IRQ_RATELIMIT_US_DQO;
		priv->rx_coalesce_usecs = GVE_RX_IRQ_RATELIMIT_US_DQO;
	}

setup_device:
	gve_set_netdev_xdp_features(priv);
	err = gve_setup_device_resources(priv);
	if (!err)
		return 0;
err:
	gve_adminq_free(&priv->pdev->dev, priv);
	return err;
}

static void gve_teardown_priv_resources(struct gve_priv *priv)
{
	gve_teardown_device_resources(priv);
	gve_adminq_free(&priv->pdev->dev, priv);
}

static void gve_trigger_reset(struct gve_priv *priv)
{
	/* Reset the device by releasing the AQ */
	gve_adminq_release(priv);
}

static void gve_reset_and_teardown(struct gve_priv *priv, bool was_up)
{
	gve_trigger_reset(priv);
	/* With the reset having already happened, close cannot fail */
	if (was_up)
		gve_close(priv->dev);
	gve_teardown_priv_resources(priv);
}

static int gve_reset_recovery(struct gve_priv *priv, bool was_up)
{
	int err;

	err = gve_init_priv(priv, true);
	if (err)
		goto err;
	if (was_up) {
		err = gve_open(priv->dev);
		if (err)
			goto err;
	}
	return 0;
err:
	dev_err(&priv->pdev->dev, "Reset failed! !!! DISABLING ALL QUEUES !!!\n");
	gve_turndown(priv);
	return err;
}

int gve_reset(struct gve_priv *priv, bool attempt_teardown)
{
	bool was_up = netif_carrier_ok(priv->dev);
	int err;

	dev_info(&priv->pdev->dev, "Performing reset\n");
	gve_clear_do_reset(priv);
	gve_set_reset_in_progress(priv);
	/* If we aren't attempting to teardown normally, just go turndown and
	 * reset right away.
	 */
	if (!attempt_teardown) {
		gve_turndown(priv);
		gve_reset_and_teardown(priv, was_up);
	} else {
		/* Otherwise attempt to close normally */
		if (was_up) {
			err = gve_close(priv->dev);
			/* If that fails reset as we did above */
			if (err)
				gve_reset_and_teardown(priv, was_up);
		}
		/* Clean up any remaining resources */
		gve_teardown_priv_resources(priv);
	}

	/* Set it all back up */
	err = gve_reset_recovery(priv, was_up);
	gve_clear_reset_in_progress(priv);
	priv->reset_cnt++;
	priv->interface_up_cnt = 0;
	priv->interface_down_cnt = 0;
	priv->stats_report_trigger_cnt = 0;
	return err;
}

static void gve_write_version(u8 __iomem *driver_version_register)
{
	const char *c = gve_version_prefix;

	while (*c) {
		writeb(*c, driver_version_register);
		c++;
	}

	c = gve_version_str;
	while (*c) {
		writeb(*c, driver_version_register);
		c++;
	}
	writeb('\n', driver_version_register);
}

static int gve_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	int max_tx_queues, max_rx_queues;
	struct net_device *dev;
	__be32 __iomem *db_bar;
	struct gve_registers __iomem *reg_bar;
	struct gve_priv *priv;
	int err;

	err = pci_enable_device(pdev);
	if (err)
		return err;

	err = pci_request_regions(pdev, gve_driver_name);
	if (err)
		goto abort_with_enabled;

	pci_set_master(pdev);

	err = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));
	if (err) {
		dev_err(&pdev->dev, "Failed to set dma mask: err=%d\n", err);
		goto abort_with_pci_region;
	}

	reg_bar = pci_iomap(pdev, GVE_REGISTER_BAR, 0);
	if (!reg_bar) {
		dev_err(&pdev->dev, "Failed to map pci bar!\n");
		err = -ENOMEM;
		goto abort_with_pci_region;
	}

	db_bar = pci_iomap(pdev, GVE_DOORBELL_BAR, 0);
	if (!db_bar) {
		dev_err(&pdev->dev, "Failed to map doorbell bar!\n");
		err = -ENOMEM;
		goto abort_with_reg_bar;
	}

	gve_write_version(&reg_bar->driver_version);
	/* Get max queues to alloc etherdev */
	max_tx_queues = ioread32be(&reg_bar->max_tx_queues);
	max_rx_queues = ioread32be(&reg_bar->max_rx_queues);
	/* Alloc and setup the netdev and priv */
	dev = alloc_etherdev_mqs(sizeof(*priv), max_tx_queues, max_rx_queues);
	if (!dev) {
		dev_err(&pdev->dev, "could not allocate netdev\n");
		err = -ENOMEM;
		goto abort_with_db_bar;
	}
	SET_NETDEV_DEV(dev, &pdev->dev);
	pci_set_drvdata(pdev, dev);
	dev->ethtool_ops = &gve_ethtool_ops;
	dev->netdev_ops = &gve_netdev_ops;

	/* Set default and supported features.
	 *
	 * Features might be set in other locations as well (such as
	 * `gve_adminq_describe_device`).
	 */
	dev->hw_features = NETIF_F_HIGHDMA;
	dev->hw_features |= NETIF_F_SG;
	dev->hw_features |= NETIF_F_HW_CSUM;
	dev->hw_features |= NETIF_F_TSO;
	dev->hw_features |= NETIF_F_TSO6;
	dev->hw_features |= NETIF_F_TSO_ECN;
	dev->hw_features |= NETIF_F_RXCSUM;
	dev->hw_features |= NETIF_F_RXHASH;
	dev->features = dev->hw_features;
	dev->watchdog_timeo = 5 * HZ;
	dev->min_mtu = ETH_MIN_MTU;
	netif_carrier_off(dev);

	priv = netdev_priv(dev);
	priv->dev = dev;
	priv->pdev = pdev;
	priv->msg_enable = DEFAULT_MSG_LEVEL;
	priv->reg_bar0 = reg_bar;
	priv->db_bar2 = db_bar;
	priv->service_task_flags = 0x0;
	priv->state_flags = 0x0;
	priv->ethtool_flags = 0x0;
	priv->data_buffer_size_dqo = GVE_DEFAULT_RX_BUFFER_SIZE;
	priv->max_rx_buffer_size = GVE_DEFAULT_RX_BUFFER_SIZE;

	gve_set_probe_in_progress(priv);
	priv->gve_wq = alloc_ordered_workqueue("gve", 0);
	if (!priv->gve_wq) {
		dev_err(&pdev->dev, "Could not allocate workqueue");
		err = -ENOMEM;
		goto abort_with_netdev;
	}
	INIT_WORK(&priv->service_task, gve_service_task);
	INIT_WORK(&priv->stats_report_task, gve_stats_report_task);
	priv->tx_cfg.max_queues = max_tx_queues;
	priv->rx_cfg.max_queues = max_rx_queues;

	err = gve_init_priv(priv, false);
	if (err)
		goto abort_with_wq;

	err = register_netdev(dev);
	if (err)
		goto abort_with_gve_init;

	dev_info(&pdev->dev, "GVE version %s\n", gve_version_str);
	dev_info(&pdev->dev, "GVE queue format %d\n", (int)priv->queue_format);
	gve_clear_probe_in_progress(priv);
	queue_work(priv->gve_wq, &priv->service_task);
	return 0;

abort_with_gve_init:
	gve_teardown_priv_resources(priv);

abort_with_wq:
	destroy_workqueue(priv->gve_wq);

abort_with_netdev:
	free_netdev(dev);

abort_with_db_bar:
	pci_iounmap(pdev, db_bar);

abort_with_reg_bar:
	pci_iounmap(pdev, reg_bar);

abort_with_pci_region:
	pci_release_regions(pdev);

abort_with_enabled:
	pci_disable_device(pdev);
	return err;
}

static void gve_remove(struct pci_dev *pdev)
{
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct gve_priv *priv = netdev_priv(netdev);
	__be32 __iomem *db_bar = priv->db_bar2;
	void __iomem *reg_bar = priv->reg_bar0;

	unregister_netdev(netdev);
	gve_teardown_priv_resources(priv);
	destroy_workqueue(priv->gve_wq);
	free_netdev(netdev);
	pci_iounmap(pdev, db_bar);
	pci_iounmap(pdev, reg_bar);
	pci_release_regions(pdev);
	pci_disable_device(pdev);
}

static void gve_shutdown(struct pci_dev *pdev)
{
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct gve_priv *priv = netdev_priv(netdev);
	bool was_up = netif_carrier_ok(priv->dev);

	rtnl_lock();
	if (was_up && gve_close(priv->dev)) {
		/* If the dev was up, attempt to close, if close fails, reset */
		gve_reset_and_teardown(priv, was_up);
	} else {
		/* If the dev wasn't up or close worked, finish tearing down */
		gve_teardown_priv_resources(priv);
	}
	rtnl_unlock();
}

#ifdef CONFIG_PM
static int gve_suspend(struct pci_dev *pdev, pm_message_t state)
{
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct gve_priv *priv = netdev_priv(netdev);
	bool was_up = netif_carrier_ok(priv->dev);

	priv->suspend_cnt++;
	rtnl_lock();
	if (was_up && gve_close(priv->dev)) {
		/* If the dev was up, attempt to close, if close fails, reset */
		gve_reset_and_teardown(priv, was_up);
	} else {
		/* If the dev wasn't up or close worked, finish tearing down */
		gve_teardown_priv_resources(priv);
	}
	priv->up_before_suspend = was_up;
	rtnl_unlock();
	return 0;
}

static int gve_resume(struct pci_dev *pdev)
{
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct gve_priv *priv = netdev_priv(netdev);
	int err;

	priv->resume_cnt++;
	rtnl_lock();
	err = gve_reset_recovery(priv, priv->up_before_suspend);
	rtnl_unlock();
	return err;
}
#endif /* CONFIG_PM */

static const struct pci_device_id gve_id_table[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_GOOGLE, PCI_DEV_ID_GVNIC) },
	{ }
};

static struct pci_driver gve_driver = {
	.name		= gve_driver_name,
	.id_table	= gve_id_table,
	.probe		= gve_probe,
	.remove		= gve_remove,
	.shutdown	= gve_shutdown,
#ifdef CONFIG_PM
	.suspend        = gve_suspend,
	.resume         = gve_resume,
#endif
};

module_pci_driver(gve_driver);

MODULE_DEVICE_TABLE(pci, gve_id_table);
MODULE_AUTHOR("Google, Inc.");
MODULE_DESCRIPTION("Google Virtual NIC Driver");
MODULE_LICENSE("Dual MIT/GPL");
MODULE_VERSION(GVE_VERSION);
