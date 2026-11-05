// SPDX-License-Identifier: (GPL-2.0 OR MIT)
/* Google virtual Ethernet (gve) driver
 *
 * Copyright (C) 2015-2024 Google LLC
 */

#include <linux/bitmap.h>
#include <linux/bpf.h>
#include <linux/cpumask.h>
#include <linux/etherdevice.h>
#include <linux/filter.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/math64.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/workqueue.h>
#include <net/netdev_queues.h>
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

static int gve_alloc_flow_rule_caches(struct gve_priv *priv)
{
	struct gve_flow_rules_cache *flow_rules_cache = &priv->flow_rules_cache;
	int err = 0;

	if (!priv->max_flow_rules)
		return 0;

	flow_rules_cache->rules_cache =
		kvzalloc_objs(*flow_rules_cache->rules_cache,
			      GVE_FLOW_RULES_CACHE_SIZE);
	if (!flow_rules_cache->rules_cache) {
		dev_err(&priv->pdev->dev, "Cannot alloc flow rules cache\n");
		return -ENOMEM;
	}

	flow_rules_cache->rule_ids_cache =
		kvcalloc(GVE_FLOW_RULE_IDS_CACHE_SIZE, sizeof(*flow_rules_cache->rule_ids_cache),
			 GFP_KERNEL);
	if (!flow_rules_cache->rule_ids_cache) {
		dev_err(&priv->pdev->dev, "Cannot alloc flow rule ids cache\n");
		err = -ENOMEM;
		goto free_rules_cache;
	}

	return 0;

free_rules_cache:
	kvfree(flow_rules_cache->rules_cache);
	flow_rules_cache->rules_cache = NULL;
	return err;
}

static void gve_free_flow_rule_caches(struct gve_priv *priv)
{
	struct gve_flow_rules_cache *flow_rules_cache = &priv->flow_rules_cache;

	kvfree(flow_rules_cache->rule_ids_cache);
	flow_rules_cache->rule_ids_cache = NULL;
	kvfree(flow_rules_cache->rules_cache);
	flow_rules_cache->rules_cache = NULL;
}

static int gve_alloc_rss_config_cache(struct gve_priv *priv)
{
	struct gve_rss_config *rss_config = &priv->rss_config;

	if (!priv->cache_rss_config)
		return 0;

	rss_config->hash_key = kcalloc(priv->rss_key_size,
				       sizeof(rss_config->hash_key[0]),
				       GFP_KERNEL);
	if (!rss_config->hash_key)
		return -ENOMEM;

	rss_config->hash_lut = kcalloc(priv->rss_lut_size,
				       sizeof(rss_config->hash_lut[0]),
				       GFP_KERNEL);
	if (!rss_config->hash_lut)
		goto free_rss_key_cache;

	return 0;

free_rss_key_cache:
	kfree(rss_config->hash_key);
	rss_config->hash_key = NULL;
	return -ENOMEM;
}

static void gve_free_rss_config_cache(struct gve_priv *priv)
{
	struct gve_rss_config *rss_config = &priv->rss_config;

	kfree(rss_config->hash_key);
	kfree(rss_config->hash_lut);

	memset(rss_config, 0, sizeof(*rss_config));
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

static int gve_alloc_irq_db_indices(struct gve_priv *priv)
{
	priv->irq_db_indices =
		dma_alloc_coherent(&priv->pdev->dev,
				   priv->num_ntfy_blks *
				   sizeof(*priv->irq_db_indices),
				   &priv->irq_db_indices_bus, GFP_KERNEL);
	if (!priv->irq_db_indices)
		return -ENOMEM;

	return 0;
}

static void gve_free_irq_db_indices(struct gve_priv *priv)
{
	if (!priv->irq_db_indices)
		return;

	dma_free_coherent(&priv->pdev->dev, priv->num_ntfy_blks *
			  sizeof(*priv->irq_db_indices),
			  priv->irq_db_indices, priv->irq_db_indices_bus);
	priv->irq_db_indices = NULL;
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
	struct gve_priv *priv = timer_container_of(priv, t,
						   stats_report_timer);

	mod_timer(&priv->stats_report_timer,
		  round_jiffies(jiffies +
		  msecs_to_jiffies(priv->stats_report_timer_period)));
	gve_stats_report_schedule(priv);
}

static int gve_alloc_stats_report(struct gve_priv *priv)
{
	int tx_stats_num, rx_stats_num;

	tx_stats_num = (GVE_TX_STATS_REPORT_NUM + NIC_TX_STATS_REPORT_NUM) *
				priv->tx_cfg.max_queues;
	rx_stats_num = (GVE_RX_STATS_REPORT_NUM + NIC_RX_STATS_REPORT_NUM) *
				priv->rx_cfg.max_queues;
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

	timer_delete_sync(&priv->stats_report_timer);
	cancel_work_sync(&priv->stats_report_task);
	dma_free_coherent(&priv->pdev->dev, priv->stats_report_len,
			  priv->stats_report, priv->stats_report_bus);
	priv->stats_report = NULL;
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

static int gve_is_napi_on_home_cpu(struct gve_priv *priv, u32 irq)
{
	int cpu_curr = smp_processor_id();
	const struct cpumask *aff_mask;

	aff_mask = irq_get_effective_affinity_mask(irq);
	if (unlikely(!aff_mask))
		return 1;

	return cpumask_test_cpu(cpu_curr, aff_mask);
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

		/* Poll XSK TX as part of RX NAPI. Setup re-poll based on max of
		 * TX and RX work done.
		 */
		if (priv->xdp_prog)
			work_done = max_t(int, work_done,
					  gve_xsk_tx_poll(block, budget));

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

	if (block->tx) {
		if (block->tx->q_num < priv->tx_cfg.num_queues)
			reschedule |= gve_tx_poll_dqo(block, /*do_clean=*/true);
		else
			reschedule |= gve_xdp_poll_dqo(block);
	}

	if (!budget)
		return 0;

	if (block->rx) {
		work_done = gve_rx_poll_dqo(block, budget);

		/* Poll XSK TX as part of RX NAPI. Setup re-poll based on if
		 * either datapath has more work to do.
		 */
		if (priv->xdp_prog)
			reschedule |= gve_xsk_tx_poll_dqo(block, budget);
		reschedule |= work_done == budget;
	}

	if (reschedule) {
		/* Reschedule by returning budget only if already on the correct
		 * cpu.
		 */
		if (likely(gve_is_napi_on_home_cpu(priv, block->irq)))
			return budget;

		/* If not on the cpu with which this queue's irq has affinity
		 * with, we avoid rescheduling napi and arm the irq instead so
		 * that napi gets rescheduled back eventually onto the right
		 * cpu.
		 */
		if (work_done == budget)
			work_done--;
	}

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

static void gve_free_notify_blocks(struct gve_priv *priv)
{
	pci_disable_msix(priv->pdev);
	kvfree(priv->ntfy_blocks);
	priv->ntfy_blocks = NULL;
	kvfree(priv->msix_vectors);
	priv->msix_vectors = NULL;
}

static const struct cpumask *gve_get_node_mask(struct gve_priv *priv)
{
	if (priv->numa_node == NUMA_NO_NODE)
		return cpu_all_mask;
	else
		return cpumask_of_node(priv->numa_node);
}

static int gve_alloc_notify_blocks(struct gve_priv *priv)
{
	int num_vecs_requested = priv->num_ntfy_blks + 1;
	int vecs_enabled;
	int err;
	int i;

	priv->msix_vectors = kvzalloc_objs(*priv->msix_vectors,
					   num_vecs_requested);
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
		goto abort;
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

	priv->ntfy_blocks = kvzalloc(priv->num_ntfy_blks *
				     sizeof(*priv->ntfy_blocks), GFP_KERNEL);
	if (!priv->ntfy_blocks) {
		err = -ENOMEM;
		goto abort;
	}

	return 0;
abort:
	gve_free_notify_blocks(priv);
	return err;
}

static void gve_teardown_notify_blocks(struct gve_priv *priv)
{
	const struct gve_ctrl_ops *ops = priv->ctrl_ops;
	int i;

	ops->teardown_mgmt_irq(priv);

	if (!priv->ntfy_blocks)
		return;

	for (i = 0; i < priv->num_ntfy_blks; i++) {
		struct gve_notify_block *block = &priv->ntfy_blocks[i];
		int msix_idx = gve_ntfy_to_msix_idx(priv, i);

		if (!block->irq_requested)
			continue;

		irq_set_affinity_hint(priv->msix_vectors[msix_idx].vector,
				      NULL);
		free_irq(priv->msix_vectors[msix_idx].vector, block);
		block->irq_requested = false;
	}
}

static int gve_setup_notify_blocks(struct gve_priv *priv)
{
	const struct gve_ctrl_ops *ops = priv->ctrl_ops;
	const struct cpumask *node_mask;
	unsigned int cur_cpu;
	int i;
	int err;

	/* Setup Management Vector */
	err = ops->setup_mgmt_irq(priv);
	if (err) {
		dev_err(&priv->pdev->dev,
			"Did not receive management vector.\n");
		return err;
	}

	node_mask = gve_get_node_mask(priv);
	cur_cpu = cpumask_first(node_mask);
	for (i = 0; i < priv->num_ntfy_blks; i++) {
		struct gve_notify_block *block = &priv->ntfy_blocks[i];
		int msix_idx = gve_ntfy_to_msix_idx(priv, i);

		snprintf(block->name, sizeof(block->name), "gve-ntfy-blk%d@pci:%s",
			 i, pci_name(priv->pdev));
		block->priv = priv;
		err = request_irq(priv->msix_vectors[msix_idx].vector,
				  gve_is_gqi(priv) ? gve_intr : gve_intr_dqo,
				  IRQF_NO_AUTOEN, block->name, block);
		if (err) {
			dev_err(&priv->pdev->dev,
				"Failed to receive msix vector %d\n", msix_idx);
			goto abort;
		}
		block->irq = priv->msix_vectors[msix_idx].vector;
		block->irq_requested = true;
		irq_set_affinity_and_hint(block->irq,
					  cpumask_of(cur_cpu));

		cur_cpu = cpumask_next(cur_cpu, node_mask);
		/* Wrap once CPUs in the node have been exhausted, or when
		 * starting RX queue affinities. TX and RX queues of the same
		 * index share affinity.
		 */
		if (cur_cpu >= nr_cpu_ids || (i + 1) == priv->tx_cfg.max_queues)
			cur_cpu = cpumask_first(node_mask);
	}
	return 0;

abort:
	gve_teardown_notify_blocks(priv);
	return err;
}

static void gve_tx_get_curr_alloc_cfg(struct gve_priv *priv,
				      struct gve_tx_alloc_rings_cfg *cfg)
{
	cfg->qcfg = &priv->tx_cfg;
	cfg->raw_addressing = !gve_is_qpl(priv);
	cfg->ring_size = priv->tx_desc_cnt;
	cfg->pages_per_qpl = priv->tx_pages_per_qpl;
	cfg->num_xdp_rings = cfg->qcfg->num_xdp_queues;
	cfg->tx = priv->tx;
}

static void gve_rx_get_curr_alloc_cfg(struct gve_priv *priv,
				      struct gve_rx_alloc_rings_cfg *cfg)
{
	cfg->qcfg_rx = &priv->rx_cfg;
	cfg->qcfg_tx = &priv->tx_cfg;
	cfg->raw_addressing = !gve_is_qpl(priv);
	cfg->enable_header_split = priv->header_split_enabled;
	cfg->ring_size = priv->rx_desc_cnt;
	cfg->pages_per_qpl = priv->rx_pages_per_qpl;
	cfg->packet_buffer_size = priv->rx_cfg.packet_buffer_size;
	cfg->rx = priv->rx;
	cfg->xdp = !!cfg->qcfg_tx->num_xdp_queues;
}

void gve_get_curr_alloc_cfgs(struct gve_priv *priv,
			     struct gve_tx_alloc_rings_cfg *tx_alloc_cfg,
			     struct gve_rx_alloc_rings_cfg *rx_alloc_cfg)
{
	gve_tx_get_curr_alloc_cfg(priv, tx_alloc_cfg);
	gve_rx_get_curr_alloc_cfg(priv, rx_alloc_cfg);
}

static void gve_queues_mem_free(struct gve_priv *priv,
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

static void gve_queues_mem_remove(struct gve_priv *priv)
{
	struct gve_tx_alloc_rings_cfg tx_alloc_cfg = {0};
	struct gve_rx_alloc_rings_cfg rx_alloc_cfg = {0};

	gve_get_curr_alloc_cfgs(priv, &tx_alloc_cfg, &rx_alloc_cfg);
	gve_queues_mem_free(priv, &tx_alloc_cfg, &rx_alloc_cfg);
	priv->tx = NULL;
	priv->rx = NULL;
}

static void gve_free_control_plane_resources(struct gve_priv *priv)
{
	bitmap_free(priv->xsk_pools);
	priv->xsk_pools = NULL;

	kvfree(priv->ptype_lut_dqo);
	priv->ptype_lut_dqo = NULL;

	gve_teardown_clock(priv);
	gve_free_irq_db_indices(priv);
	gve_free_counter_array(priv);
	gve_free_notify_blocks(priv);
	gve_free_stats_report(priv);
	gve_free_rss_config_cache(priv);
	gve_free_flow_rule_caches(priv);
}

static int gve_alloc_control_plane_resources(struct gve_priv *priv)
{
	int err;

	err = gve_alloc_flow_rule_caches(priv);
	if (err)
		return err;
	err = gve_alloc_rss_config_cache(priv);
	if (err)
		goto abort;
	err = gve_alloc_notify_blocks(priv);
	if (err)
		goto abort;
	err = gve_alloc_stats_report(priv);
	if (err)
		goto abort;
	err = gve_alloc_counter_array(priv);
	if (err)
		goto abort;
	err = gve_alloc_irq_db_indices(priv);
	if (err)
		goto abort;

	if (!gve_is_gqi(priv)) {
		priv->ptype_lut_dqo = kvzalloc_obj(*priv->ptype_lut_dqo,
						   GFP_KERNEL);
		if (!priv->ptype_lut_dqo) {
			err = -ENOMEM;
			goto abort;
		}
	}

	priv->xsk_pools = bitmap_zalloc(priv->rx_cfg.max_queues, GFP_KERNEL);
	if (!priv->xsk_pools) {
		err = -ENOMEM;
		goto abort;
	}

	return 0;
abort:
	gve_free_control_plane_resources(priv);
	return err;
}

static int gve_setup_control_plane_resources(struct gve_priv *priv)
{
	const struct gve_ctrl_ops *ops = priv->ctrl_ops;
	int err;

	err = ops->request_db_info(priv);
	if (err) {
		dev_err(&priv->pdev->dev, "Failed to get db info");
		return err;
	}

	if (!gve_is_gqi(priv)) {
		err = ops->get_ptype_map(priv);
		if (err) {
			dev_err(&priv->pdev->dev,
				"Failed to get ptype map: err=%d\n", err);
			goto release_db_resources;
		}
	}

	if (priv->nic_timestamp_supported) {
		err = gve_init_clock(priv);
		if (err) {
			dev_warn(&priv->pdev->dev, "Failed to init clock, continuing without PTP support");
			err = 0;
		}
	}

	err = gve_init_rss_config(priv, priv->rx_cfg.num_queues);
	if (err) {
		dev_err(&priv->pdev->dev, "Failed to init RSS config");
		goto release_db_resources;
	}

	err = ops->setup_stats_report(priv, priv->stats_report_len,
				      priv->stats_report_bus,
				      GVE_STATS_REPORT_TIMER_PERIOD);
	if (err)
		dev_err(&priv->pdev->dev,
			"Failed to report stats: err=%d\n", err);

	gve_set_device_resources_ok(priv);
	return 0;

release_db_resources:
	ops->release_db_resources(priv);
	return err;
}

/**
 * gve_teardown_control_plane_resources() - Request the device to release any
 * shared allocated resources.
 *
 * @priv: Pointer to the GVE private device data structure.
 *
 * If any part of the teardown step fails, the failure is documented, but is
 * otherwise ignored. It is expected that a device reset is triggered
 * immediately after tearing down device resources, which would clear any
 * lingering state on the device.
 */
static void gve_teardown_control_plane_resources(struct gve_priv *priv)
{
	const struct gve_ctrl_ops *ops = priv->ctrl_ops;
	int err;

	if (priv->ptp)
		ptp_cancel_worker_sync(priv->ptp->clock);

	/* Tell device its resources are being freed */
	if (gve_get_device_resources_ok(priv)) {
		err = gve_flow_rules_reset(priv);
		if (err)
			dev_err(&priv->pdev->dev,
				"Failed to reset flow rules: err=%d\n", err);
		/* detach the stats report */
		err = ops->setup_stats_report(priv, 0, 0x0,
					      GVE_STATS_REPORT_TIMER_PERIOD);
		if (err)
			dev_err(&priv->pdev->dev,
				"Failed to detach stats report: err=%d\n", err);
		ops->release_db_resources(priv);
	}

	gve_clear_device_resources_ok(priv);
}

/**
 * gve_reset_device() - Reset the device
 *
 * @priv: Pointer to the GVE private device data structure.
 *
 * Once this returns, the device is guaranteed not to access any memory it
 * shares with the driver, so it is safe for the caller to recycle it. Device
 * commands in gve_teardown_control_plane_resources() can fail, in which case
 * the hardware reset triggered by gve_adminq_free() is the only such
 * guarantee.
 */
static void gve_reset_device(struct gve_priv *priv)
{
	gve_teardown_control_plane_resources(priv);
	gve_adminq_free(priv);
}

static void gve_teardown_device(struct gve_priv *priv)
{
	gve_reset_device(priv);
	/* Free any resources shared with the device only after we have a
	 * guarantee that the device will not try to access such resources.
	 */
	gve_teardown_notify_blocks(priv);
	gve_free_control_plane_resources(priv);
	gve_queues_mem_remove(priv);
}

static int gve_unregister_qpl(struct gve_priv *priv,
			      struct gve_queue_page_list *qpl)
{
	int err;

	if (!qpl)
		return 0;

	err = gve_adminq_unregister_page_list(priv, qpl->id);
	if (err) {
		netif_err(priv, drv, priv->dev,
			  "Failed to unregister queue page list %d\n",
			  qpl->id);
		return err;
	}

	priv->num_registered_pages -= qpl->num_entries;
	return 0;
}

static int gve_register_qpl(struct gve_priv *priv,
			    struct gve_queue_page_list *qpl)
{
	int pages;
	int err;

	if (!qpl)
		return 0;

	pages = qpl->num_entries;

	if (pages + priv->num_registered_pages > priv->max_registered_pages) {
		netif_err(priv, drv, priv->dev,
			  "Reached max number of registered pages %llu > %llu\n",
			  pages + priv->num_registered_pages,
			  priv->max_registered_pages);
		return -EINVAL;
	}

	err = gve_adminq_register_page_list(priv, qpl);
	if (err) {
		netif_err(priv, drv, priv->dev,
			  "failed to register queue page list %d\n",
			  qpl->id);
		return err;
	}

	priv->num_registered_pages += pages;
	return 0;
}

static struct gve_queue_page_list *gve_tx_get_qpl(struct gve_priv *priv, int idx)
{
	struct gve_tx_ring *tx = &priv->tx[idx];

	if (gve_is_gqi(priv))
		return tx->tx_fifo.qpl;
	else
		return tx->dqo.qpl;
}

static struct gve_queue_page_list *gve_rx_get_qpl(struct gve_priv *priv, int idx)
{
	struct gve_rx_ring *rx = &priv->rx[idx];

	if (gve_is_gqi(priv))
		return rx->data.qpl;
	else
		return rx->dqo.qpl;
}

static int gve_register_qpls(struct gve_priv *priv)
{
	int num_tx_qpls, num_rx_qpls;
	int err;
	int i;

	num_tx_qpls = gve_num_tx_qpls(&priv->tx_cfg, gve_is_qpl(priv));
	num_rx_qpls = gve_num_rx_qpls(&priv->rx_cfg, gve_is_qpl(priv));

	for (i = 0; i < num_tx_qpls; i++) {
		err = gve_register_qpl(priv, gve_tx_get_qpl(priv, i));
		if (err)
			return err;
	}

	for (i = 0; i < num_rx_qpls; i++) {
		err = gve_register_qpl(priv, gve_rx_get_qpl(priv, i));
		if (err)
			return err;
	}

	return 0;
}

static int gve_unregister_qpls(struct gve_priv *priv)
{
	int num_tx_qpls, num_rx_qpls;
	int err;
	int i;

	num_tx_qpls = gve_num_tx_qpls(&priv->tx_cfg, gve_is_qpl(priv));
	num_rx_qpls = gve_num_rx_qpls(&priv->rx_cfg, gve_is_qpl(priv));

	for (i = 0; i < num_tx_qpls; i++) {
		err = gve_unregister_qpl(priv, gve_tx_get_qpl(priv, i));
		/* This failure will trigger a reset - no need to clean */
		if (err)
			return err;
	}

	for (i = 0; i < num_rx_qpls; i++) {
		err = gve_unregister_qpl(priv, gve_rx_get_qpl(priv, i));
		/* This failure will trigger a reset - no need to clean */
		if (err)
			return err;
	}
	return 0;
}

static int gve_create_rings(struct gve_priv *priv)
{
	int err;
	int i;

	err = priv->ctrl_ops->create_queues(priv);
	if (err) {
		/* This failure will trigger a reset - no need to clean
		 * up
		 */
		return err;
	}

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
	for (i = start_id; i < start_id + priv->tx_cfg.num_xdp_queues; i++) {
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

static void gve_tx_stop_rings(struct gve_priv *priv, int num_rings)
{
	int i;

	if (!priv->tx)
		return;

	for (i = 0; i < num_rings; i++) {
		if (gve_is_gqi(priv))
			gve_tx_stop_ring_gqi(priv, i);
		else
			gve_tx_stop_ring_dqo(priv, i);
	}
}

static void gve_tx_start_rings(struct gve_priv *priv, int num_rings)
{
	int i;

	for (i = 0; i < num_rings; i++) {
		if (gve_is_gqi(priv))
			gve_tx_start_ring_gqi(priv, i);
		else
			gve_tx_start_ring_dqo(priv, i);
	}
}

void gve_update_num_qpl_pages(struct gve_priv *priv,
			      struct gve_rx_alloc_rings_cfg *rx_alloc_cfg,
			      struct gve_tx_alloc_rings_cfg *tx_alloc_cfg)
{
	u64 ideal_tx_pages, ideal_rx_pages;
	u16 tx_num_queues, rx_num_queues;
	u64 max_pages, tx_pages;

	if (priv->queue_format == GVE_GQI_QPL_FORMAT) {
		rx_alloc_cfg->pages_per_qpl = rx_alloc_cfg->ring_size;
	} else if (priv->queue_format == GVE_DQO_QPL_FORMAT) {
		/*
		 * We want 2 pages per RX descriptor and half a page per TX
		 * descriptor, which means the fraction ideal_tx_pages /
		 * (ideal_tx_pages + ideal_rx_pages) of the pages we allocate
		 * should be for TX. Shrink proportionally as necessary to avoid
		 * allocating more than max_registered_pages total pages.
		 */
		tx_num_queues = tx_alloc_cfg->qcfg->num_queues;
		rx_num_queues = rx_alloc_cfg->qcfg_rx->num_queues;

		ideal_tx_pages = tx_alloc_cfg->ring_size * tx_num_queues / 2;
		ideal_rx_pages = rx_alloc_cfg->ring_size * rx_num_queues * 2;
		max_pages = min(priv->max_registered_pages,
				ideal_tx_pages + ideal_rx_pages);

		tx_pages = div64_u64(max_pages * ideal_tx_pages,
				     ideal_tx_pages + ideal_rx_pages);
		tx_alloc_cfg->pages_per_qpl = div_u64(tx_pages, tx_num_queues);
		rx_alloc_cfg->pages_per_qpl = div_u64(max_pages - tx_pages,
						      rx_num_queues);
	}
}

static int gve_queues_mem_alloc(struct gve_priv *priv,
				struct gve_tx_alloc_rings_cfg *tx_alloc_cfg,
				struct gve_rx_alloc_rings_cfg *rx_alloc_cfg)
{
	int err;

	gve_update_num_qpl_pages(priv, rx_alloc_cfg, tx_alloc_cfg);

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

static int gve_destroy_rings(struct gve_priv *priv)
{
	int err;

	err = priv->ctrl_ops->destroy_queues(priv);

	/* A failure will trigger a reset - no need to clean up */
	return err;
}

int gve_alloc_page(struct gve_priv *priv, struct device *dev,
		   struct page **page, dma_addr_t *dma,
		   enum dma_data_direction dir, gfp_t gfp_flags)
{
	*page = alloc_pages_node(priv->numa_node, gfp_flags, 0);
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

struct gve_queue_page_list *gve_alloc_queue_page_list(struct gve_priv *priv,
						      u32 id, int pages)
{
	struct gve_queue_page_list *qpl;
	int err;
	int i;

	qpl = kvzalloc_obj(*qpl);
	if (!qpl)
		return NULL;

	qpl->id = id;
	qpl->num_entries = 0;
	qpl->pages = kvzalloc_objs(*qpl->pages, pages);
	if (!qpl->pages)
		goto abort;

	qpl->page_buses = kvzalloc_objs(*qpl->page_buses, pages);
	if (!qpl->page_buses)
		goto abort;

	for (i = 0; i < pages; i++) {
		err = gve_alloc_page(priv, &priv->pdev->dev, &qpl->pages[i],
				     &qpl->page_buses[i],
				     gve_qpl_dma_dir(priv, id), GFP_KERNEL);
		if (err)
			goto abort;
		qpl->num_entries++;
	}

	return qpl;

abort:
	gve_free_queue_page_list(priv, qpl, id);
	return NULL;
}

void gve_free_page(struct device *dev, struct page *page, dma_addr_t dma,
		   enum dma_data_direction dir)
{
	if (!dma_mapping_error(dev, dma))
		dma_unmap_page(dev, dma, PAGE_SIZE, dir);
	if (page)
		put_page(page);
}

void gve_free_queue_page_list(struct gve_priv *priv,
			      struct gve_queue_page_list *qpl,
			      u32 id)
{
	int i;

	if (!qpl)
		return;
	if (!qpl->pages)
		goto free_qpl;
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
free_qpl:
	kvfree(qpl);
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

static void gve_turndown(struct gve_priv *priv);
static void gve_turnup(struct gve_priv *priv);

static void gve_unreg_xsk_pool(struct gve_priv *priv, u16 qid)
{
	struct gve_rx_ring *rx;

	if (!priv->rx)
		return;

	rx = &priv->rx[qid];
	rx->xsk_pool = NULL;
	if (xdp_rxq_info_is_reg(&rx->xdp_rxq))
		xdp_rxq_info_unreg_mem_model(&rx->xdp_rxq);

	if (!priv->tx)
		return;
	priv->tx[gve_xdp_tx_queue_id(priv, qid)].xsk_pool = NULL;
}

static int gve_reg_xsk_pool(struct gve_priv *priv, struct net_device *dev,
			    struct xsk_buff_pool *pool, u16 qid)
{
	struct gve_rx_ring *rx;
	u16 tx_qid;
	int err;

	rx = &priv->rx[qid];
	err = xdp_rxq_info_reg_mem_model(&rx->xdp_rxq,
					 MEM_TYPE_XSK_BUFF_POOL, pool);
	if (err) {
		gve_unreg_xsk_pool(priv, qid);
		return err;
	}

	rx->xsk_pool = pool;

	tx_qid = gve_xdp_tx_queue_id(priv, qid);
	priv->tx[tx_qid].xsk_pool = pool;

	return 0;
}

static void gve_unreg_xdp_info(struct gve_priv *priv)
{
	int i;

	if (!priv->tx_cfg.num_xdp_queues || !priv->rx)
		return;

	for (i = 0; i < priv->rx_cfg.num_queues; i++) {
		struct gve_rx_ring *rx = &priv->rx[i];

		if (xdp_rxq_info_is_reg(&rx->xdp_rxq))
			xdp_rxq_info_unreg(&rx->xdp_rxq);

		gve_unreg_xsk_pool(priv, i);
	}
}

static struct xsk_buff_pool *gve_get_xsk_pool(struct gve_priv *priv, int qid)
{
	if (!test_bit(qid, priv->xsk_pools))
		return NULL;

	return xsk_get_pool_from_qid(priv->dev, qid);
}

static int gve_reg_xdp_info(struct gve_priv *priv, struct net_device *dev)
{
	struct napi_struct *napi;
	struct gve_rx_ring *rx;
	int err = 0;
	int i;

	if (!priv->tx_cfg.num_xdp_queues)
		return 0;

	for (i = 0; i < priv->rx_cfg.num_queues; i++) {
		struct xsk_buff_pool *xsk_pool;

		rx = &priv->rx[i];
		napi = &priv->ntfy_blocks[rx->ntfy_id].napi;

		err = xdp_rxq_info_reg(&rx->xdp_rxq, dev, i,
				       napi->napi_id);
		if (err)
			goto err;

		xsk_pool = gve_get_xsk_pool(priv, i);
		if (xsk_pool)
			err = gve_reg_xsk_pool(priv, dev, xsk_pool, i);
		else if (gve_is_qpl(priv))
			err = xdp_rxq_info_reg_mem_model(&rx->xdp_rxq,
							 MEM_TYPE_PAGE_SHARED,
							 NULL);
		else
			err = xdp_rxq_info_reg_mem_model(&rx->xdp_rxq,
							 MEM_TYPE_PAGE_POOL,
							 rx->dqo.page_pool);
		if (err)
			goto err;
	}
	return 0;

err:
	gve_unreg_xdp_info(priv);
	return err;
}

static void gve_drain_page_cache(struct gve_priv *priv)
{
	int i;

	if (!priv->rx)
		return;
	for (i = 0; i < priv->rx_cfg.num_queues; i++)
		page_frag_cache_drain(&priv->rx[i].page_cache);
}

static void gve_rx_start_ring(struct gve_priv *priv, int i)
{
	if (gve_is_gqi(priv))
		gve_rx_start_ring_gqi(priv, i);
	else
		gve_rx_start_ring_dqo(priv, i);
}

static void gve_rx_start_rings(struct gve_priv *priv, int num_rings)
{
	int i;

	for (i = 0; i < num_rings; i++)
		gve_rx_start_ring(priv, i);
}

static void gve_rx_stop_ring(struct gve_priv *priv, int i)
{
	if (gve_is_gqi(priv))
		gve_rx_stop_ring_gqi(priv, i);
	else
		gve_rx_stop_ring_dqo(priv, i);
}

static void gve_rx_stop_rings(struct gve_priv *priv, int num_rings)
{
	int i;

	if (!priv->rx)
		return;

	for (i = 0; i < num_rings; i++)
		gve_rx_stop_ring(priv, i);
}

static void gve_queues_stop(struct gve_priv *priv)
{
	gve_unreg_xdp_info(priv);
	gve_drain_page_cache(priv);

	timer_delete_sync(&priv->stats_report_timer);
	cancel_work_sync(&priv->stats_report_task);

	gve_tx_stop_rings(priv, gve_num_tx_queues(priv));
	gve_rx_stop_rings(priv, priv->rx_cfg.num_queues);
}

/* The passed-in queue memory is stored into priv and the queues are made live.
 * No memory is allocated. Passed-in memory is freed on errors.
 */
static int gve_queues_start(struct gve_priv *priv,
			    struct gve_tx_alloc_rings_cfg *tx_alloc_cfg,
			    struct gve_rx_alloc_rings_cfg *rx_alloc_cfg)
{
	struct net_device *dev = priv->dev;
	int err;

	/* Record new resources into priv */
	priv->tx = tx_alloc_cfg->tx;
	priv->rx = rx_alloc_cfg->rx;

	/* Record new configs into priv */
	priv->tx_cfg = *tx_alloc_cfg->qcfg;
	priv->tx_cfg.num_xdp_queues = tx_alloc_cfg->num_xdp_rings;
	priv->rx_cfg = *rx_alloc_cfg->qcfg_rx;
	priv->tx_desc_cnt = tx_alloc_cfg->ring_size;
	priv->rx_desc_cnt = rx_alloc_cfg->ring_size;
	priv->tx_pages_per_qpl = tx_alloc_cfg->pages_per_qpl;
	priv->rx_pages_per_qpl = rx_alloc_cfg->pages_per_qpl;
	priv->header_split_enabled = rx_alloc_cfg->enable_header_split;
	priv->rx_cfg.packet_buffer_size = rx_alloc_cfg->packet_buffer_size;

	gve_tx_start_rings(priv, gve_num_tx_queues(priv));
	gve_rx_start_rings(priv, rx_alloc_cfg->qcfg_rx->num_queues);
	gve_init_sync_stats(priv);

	err = netif_set_real_num_tx_queues(dev, priv->tx_cfg.num_queues);
	if (err)
		goto stop_rings;
	err = netif_set_real_num_rx_queues(dev, priv->rx_cfg.num_queues);
	if (err)
		goto stop_rings;

	err = gve_reg_xdp_info(priv, dev);
	if (err)
		goto stop_rings;

	if (rx_alloc_cfg->reset_rss) {
		err = gve_init_rss_config(priv, priv->rx_cfg.num_queues);
		if (err)
			goto reset;
	}

	err = gve_register_qpls(priv);
	if (err)
		goto reset;

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
		goto stop_rings;

	/* Attempt to reset. If reset is successful, gve_queues_start was
	 * successful with the new config.
	 */
	return gve_reset(priv, false);
stop_rings:
	gve_queues_stop(priv);
	return err;
}

static int gve_open(struct net_device *dev)
{
	struct gve_tx_alloc_rings_cfg tx_alloc_cfg = {0};
	struct gve_rx_alloc_rings_cfg rx_alloc_cfg = {0};
	struct gve_priv *priv = netdev_priv(dev);
	int err;

	if (!gve_get_device_resources_ok(priv)) {
		dev_err(&priv->pdev->dev,
			"Attempting to open netdev without resources. Device must be reset.");
		return -ENODEV;
	}

	gve_get_curr_alloc_cfgs(priv, &tx_alloc_cfg, &rx_alloc_cfg);

	err = gve_queues_mem_alloc(priv, &tx_alloc_cfg, &rx_alloc_cfg);
	if (err)
		return err;

	err = gve_queues_start(priv, &tx_alloc_cfg, &rx_alloc_cfg);
	if (err) {
		gve_queues_mem_remove(priv);
		return err;
	}

	return 0;
}

static int gve_close(struct net_device *dev)
{
	struct gve_priv *priv = netdev_priv(dev);
	int err;

	gve_turndown(priv);

	/* Surrender to reset if the queue destroying adminq cmds fail. Reset
	 * will not re-enable the interface.
	 */
	if (gve_get_device_rings_ok(priv)) {
		gve_clear_device_rings_ok(priv);
		err = gve_destroy_rings(priv);
		if (err)
			goto reset;
		err = gve_unregister_qpls(priv);
		if (err)
			goto reset;
	}

	gve_queues_stop(priv);
	gve_queues_mem_remove(priv);
	priv->interface_down_cnt++;
	return 0;

reset:
	err = gve_reset(priv, true);
	return err;
}

static void gve_handle_link_status(struct gve_priv *priv)
{
	bool link_up;

	if (!gve_get_napi_enabled(priv))
		return;

	link_up = READ_ONCE(priv->link_up);
	if (link_up == netif_carrier_ok(priv->dev))
		return;

	if (link_up) {
		netdev_info(priv->dev, "Device link is up.\n");
		netif_carrier_on(priv->dev);
	} else {
		netdev_info(priv->dev, "Device link is down.\n");
		netif_carrier_off(priv->dev);
	}
}

static int gve_configure_rings_xdp(struct gve_priv *priv,
				   u16 num_xdp_rings)
{
	struct gve_tx_alloc_rings_cfg tx_alloc_cfg = {0};
	struct gve_rx_alloc_rings_cfg rx_alloc_cfg = {0};

	gve_get_curr_alloc_cfgs(priv, &tx_alloc_cfg, &rx_alloc_cfg);
	tx_alloc_cfg.num_xdp_rings = num_xdp_rings;

	rx_alloc_cfg.xdp = !!num_xdp_rings;
	return gve_adjust_config(priv, &tx_alloc_cfg, &rx_alloc_cfg);
}

static int gve_set_xdp(struct gve_priv *priv, struct bpf_prog *prog,
		       struct netlink_ext_ack *extack)
{
	struct bpf_prog *old_prog;
	int err = 0;

	old_prog = READ_ONCE(priv->xdp_prog);
	if (!netif_running(priv->dev)) {
		WRITE_ONCE(priv->xdp_prog, prog);
		if (old_prog)
			bpf_prog_put(old_prog);

		/* Update priv XDP queue configuration */
		priv->tx_cfg.num_xdp_queues = priv->xdp_prog ?
			priv->rx_cfg.num_queues : 0;
		return 0;
	}

	if (!old_prog && prog)
		err = gve_configure_rings_xdp(priv, priv->rx_cfg.num_queues);
	else if (old_prog && !prog)
		err = gve_configure_rings_xdp(priv, 0);

	if (err)
		goto out;

	WRITE_ONCE(priv->xdp_prog, prog);
	if (old_prog)
		bpf_prog_put(old_prog);

out:
	priv->ctrl_ops->report_link_status(priv);
	gve_handle_link_status(priv);
	return err;
}

static int gve_xdp_xmit(struct net_device *dev, int n,
			struct xdp_frame **frames, u32 flags)
{
	struct gve_priv *priv = netdev_priv(dev);

	if (priv->queue_format == GVE_GQI_QPL_FORMAT)
		return gve_xdp_xmit_gqi(dev, n, frames, flags);
	else if (priv->queue_format == GVE_DQO_RDA_FORMAT)
		return gve_xdp_xmit_dqo(dev, n, frames, flags);

	return -EOPNOTSUPP;
}

static int gve_xsk_pool_enable(struct net_device *dev,
			       struct xsk_buff_pool *pool,
			       u16 qid)
{
	struct gve_priv *priv = netdev_priv(dev);
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

	set_bit(qid, priv->xsk_pools);

	/* If XDP prog is not installed or interface is down, return. */
	if (!priv->xdp_prog || !netif_running(dev))
		return 0;

	err = gve_reg_xsk_pool(priv, dev, pool, qid);
	if (err)
		goto err_xsk_pool_dma_mapped;

	/* Stop and start RDA queues to repost buffers. */
	if (!gve_is_qpl(priv)) {
		err = gve_configure_rings_xdp(priv, priv->rx_cfg.num_queues);
		if (err)
			goto err_xsk_pool_registered;
	}
	return 0;

err_xsk_pool_registered:
	gve_unreg_xsk_pool(priv, qid);
err_xsk_pool_dma_mapped:
	clear_bit(qid, priv->xsk_pools);
	xsk_pool_dma_unmap(pool,
			   DMA_ATTR_SKIP_CPU_SYNC |
			   DMA_ATTR_WEAK_ORDERING);
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
	int err;

	if (qid >= priv->rx_cfg.num_queues)
		return -EINVAL;

	clear_bit(qid, priv->xsk_pools);

	pool = xsk_get_pool_from_qid(dev, qid);
	if (pool)
		xsk_pool_dma_unmap(pool,
				   DMA_ATTR_SKIP_CPU_SYNC |
				   DMA_ATTR_WEAK_ORDERING);

	if (!netif_running(dev) || !priv->tx_cfg.num_xdp_queues)
		return 0;

	/* Stop and start RDA queues to repost buffers. */
	if (!gve_is_qpl(priv) && priv->xdp_prog) {
		err = gve_configure_rings_xdp(priv, priv->rx_cfg.num_queues);
		if (err)
			return err;
	}

	napi_rx = &priv->ntfy_blocks[priv->rx[qid].ntfy_id].napi;
	napi_disable_locked(napi_rx); /* make sure current rx poll is done */

	tx_qid = gve_xdp_tx_queue_id(priv, qid);
	napi_tx = &priv->ntfy_blocks[priv->tx[tx_qid].ntfy_id].napi;
	napi_disable_locked(napi_tx); /* make sure current tx poll is done */

	gve_unreg_xsk_pool(priv, qid);
	smp_mb(); /* Make sure it is visible to the workers on datapath */

	napi_enable_locked(napi_rx);
	napi_enable_locked(napi_tx);
	if (gve_is_gqi(priv)) {
		if (gve_rx_work_pending(&priv->rx[qid]))
			napi_schedule(napi_rx);

		if (gve_tx_clean_pending(priv, &priv->tx[tx_qid]))
			napi_schedule(napi_tx);
	}

	return 0;
}

static int gve_xsk_wakeup(struct net_device *dev, u32 queue_id, u32 flags)
{
	struct gve_priv *priv = netdev_priv(dev);
	struct napi_struct *napi;

	if (!gve_get_napi_enabled(priv))
		return -ENETDOWN;

	if (queue_id >= priv->rx_cfg.num_queues || !priv->xdp_prog)
		return -EINVAL;

	napi = &priv->ntfy_blocks[gve_rx_idx_to_ntfy(priv, queue_id)].napi;
	if (!napi_if_scheduled_mark_missed(napi)) {
		/* Call local_bh_enable to trigger SoftIRQ processing */
		local_bh_disable();
		napi_schedule(napi);
		local_bh_enable();
	}

	return 0;
}

static int gve_verify_xdp_configuration(struct net_device *dev,
					struct netlink_ext_ack *extack)
{
	struct gve_priv *priv = netdev_priv(dev);
	u16 max_xdp_mtu;

	if (dev->features & NETIF_F_GRO_HW) {
		NL_SET_ERR_MSG_MOD(extack,
				   "XDP is not supported when HW-GRO is on.");
		return -EOPNOTSUPP;
	}

	if (priv->header_split_enabled) {
		NL_SET_ERR_MSG_MOD(extack,
				   "XDP is not supported when header-data split is enabled.");
		return -EOPNOTSUPP;
	}

	if (priv->rx_cfg.packet_buffer_size != SZ_2K) {
		NL_SET_ERR_MSG_FMT_MOD(extack,
				       "XDP is not supported for Rx buf len %d, only %d supported.",
				       priv->rx_cfg.packet_buffer_size, SZ_2K);
		return -EOPNOTSUPP;
	}

	max_xdp_mtu = priv->rx_cfg.packet_buffer_size - sizeof(struct ethhdr);
	if (priv->queue_format == GVE_GQI_QPL_FORMAT)
		max_xdp_mtu -= GVE_RX_PAD;

	if (dev->mtu > max_xdp_mtu) {
		NL_SET_ERR_MSG_FMT_MOD(extack,
				       "XDP is not supported for mtu %d.",
				       dev->mtu);
		return -EOPNOTSUPP;
	}

	if (priv->rx_cfg.num_queues != priv->tx_cfg.num_queues ||
	    (2 * priv->tx_cfg.num_queues > priv->tx_cfg.max_queues)) {
		netdev_warn(dev,
			    "XDP load failed: The number of configured RX queues %d should be equal to the number of configured TX queues %d and the number of configured RX/TX queues should be less than or equal to half the maximum number of RX/TX queues %d.",
			    priv->rx_cfg.num_queues, priv->tx_cfg.num_queues,
			    priv->tx_cfg.max_queues);
		NL_SET_ERR_MSG_MOD(extack,
				   "XDP load failed: The number of configured RX queues should be equal to the number of configured TX queues and the number of configured RX/TX queues should be less than or equal to half the maximum number of RX/TX queues");
		return -EINVAL;
	}
	return 0;
}

static int gve_xdp(struct net_device *dev, struct netdev_bpf *xdp)
{
	struct gve_priv *priv = netdev_priv(dev);
	int err;

	err = gve_verify_xdp_configuration(dev, xdp->extack);
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

int gve_init_rss_config(struct gve_priv *priv, u16 num_queues)
{
	const struct gve_ctrl_ops *ops = priv->ctrl_ops;
	struct gve_rss_config *rss_config = &priv->rss_config;
	struct ethtool_rxfh_param rxfh = {0};
	u16 i;

	if (!priv->cache_rss_config)
		return 0;

	for (i = 0; i < priv->rss_lut_size; i++)
		rss_config->hash_lut[i] =
			ethtool_rxfh_indir_default(i, num_queues);

	netdev_rss_key_fill(rss_config->hash_key, priv->rss_key_size);

	rxfh.hfunc = ETH_RSS_HASH_TOP;

	return ops->configure_rss(priv, &rxfh);
}

int gve_flow_rules_reset(struct gve_priv *priv)
{
	const struct gve_ctrl_ops *ops = priv->ctrl_ops;

	if (!priv->max_flow_rules)
		return 0;

	return ops->reset_flow_rules(priv);
}

int gve_adjust_config(struct gve_priv *priv,
		      struct gve_tx_alloc_rings_cfg *tx_alloc_cfg,
		      struct gve_rx_alloc_rings_cfg *rx_alloc_cfg)
{
	int err;

	/* Allocate resources for the new configuration */
	err = gve_queues_mem_alloc(priv, tx_alloc_cfg, rx_alloc_cfg);
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
		gve_queues_mem_free(priv, tx_alloc_cfg, rx_alloc_cfg);
		return err;
	}

	/* Bring the device back up again with the new resources. */
	err = gve_queues_start(priv, tx_alloc_cfg, rx_alloc_cfg);
	if (err) {
		netif_err(priv, drv, priv->dev,
			  "Adjust config failed to start new queues, !!! DISABLING ALL QUEUES !!!\n");
		gve_queues_mem_remove(priv);
		gve_turndown(priv);
		return err;
	}

	return 0;
}

int gve_adjust_queues(struct gve_priv *priv,
		      struct gve_rx_queue_config new_rx_config,
		      struct gve_tx_queue_config new_tx_config,
		      bool reset_rss)
{
	struct gve_tx_alloc_rings_cfg tx_alloc_cfg = {0};
	struct gve_rx_alloc_rings_cfg rx_alloc_cfg = {0};
	int err;

	gve_get_curr_alloc_cfgs(priv, &tx_alloc_cfg, &rx_alloc_cfg);

	/* Relay the new config from ethtool */
	tx_alloc_cfg.qcfg = &new_tx_config;
	rx_alloc_cfg.qcfg_tx = &new_tx_config;
	rx_alloc_cfg.qcfg_rx = &new_rx_config;
	rx_alloc_cfg.reset_rss = reset_rss;

	if (netif_running(priv->dev)) {
		err = gve_adjust_config(priv, &tx_alloc_cfg, &rx_alloc_cfg);
		return err;
	}
	/* Set the config for the next up. */
	if (reset_rss) {
		err = gve_init_rss_config(priv, new_rx_config.num_queues);
		if (err)
			return err;
	}
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

		if (!gve_tx_was_added_to_block(priv, idx))
			continue;

		if (idx < priv->tx_cfg.num_queues)
			netif_queue_set_napi(priv->dev, idx,
					     NETDEV_QUEUE_TYPE_TX, NULL);

		napi_disable_locked(&block->napi);
	}
	for (idx = 0; idx < priv->rx_cfg.num_queues; idx++) {
		int ntfy_idx = gve_rx_idx_to_ntfy(priv, idx);
		struct gve_notify_block *block = &priv->ntfy_blocks[ntfy_idx];

		if (!gve_rx_was_added_to_block(priv, idx))
			continue;

		netif_queue_set_napi(priv->dev, idx, NETDEV_QUEUE_TYPE_RX,
				     NULL);
		napi_disable_locked(&block->napi);
	}

	/* Stop tx queues */
	netif_tx_disable(priv->dev);

	xdp_features_clear_redirect_target_locked(priv->dev);

	gve_clear_napi_enabled(priv);
	gve_clear_report_stats(priv);

	/* Make sure that all traffic is finished processing. */
	synchronize_net();
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

		if (!gve_tx_was_added_to_block(priv, idx))
			continue;

		napi_enable_locked(&block->napi);

		if (idx < priv->tx_cfg.num_queues)
			netif_queue_set_napi(priv->dev, idx,
					     NETDEV_QUEUE_TYPE_TX,
					     &block->napi);

		if (gve_is_gqi(priv)) {
			iowrite32be(0, gve_irq_doorbell(priv, block));
		} else {
			gve_set_itr_coalesce_usecs_dqo(priv, block,
						       priv->tx_coalesce_usecs);
		}

		/* Any descs written by the NIC before this barrier will be
		 * handled by the one-off napi schedule below. Whereas any
		 * descs after the barrier will generate interrupts.
		 */
		mb();
		napi_schedule(&block->napi);
	}
	for (idx = 0; idx < priv->rx_cfg.num_queues; idx++) {
		int ntfy_idx = gve_rx_idx_to_ntfy(priv, idx);
		struct gve_notify_block *block = &priv->ntfy_blocks[ntfy_idx];

		if (!gve_rx_was_added_to_block(priv, idx))
			continue;

		napi_enable_locked(&block->napi);
		netif_queue_set_napi(priv->dev, idx, NETDEV_QUEUE_TYPE_RX,
				     &block->napi);

		if (gve_is_gqi(priv)) {
			iowrite32be(0, gve_irq_doorbell(priv, block));
		} else {
			gve_set_itr_coalesce_usecs_dqo(priv, block,
						       priv->rx_coalesce_usecs);
		}

		/* Any descs written by the NIC before this barrier will be
		 * handled by the one-off napi schedule below. Whereas any
		 * descs after the barrier will generate interrupts.
		 */
		mb();
		napi_schedule(&block->napi);
	}

	if (priv->tx_cfg.num_xdp_queues && gve_supports_xdp_xmit(priv))
		xdp_features_set_redirect_target_locked(priv->dev, false);

	gve_set_napi_enabled(priv);
}

static void gve_turnup_and_check_status(struct gve_priv *priv)
{
	const struct gve_ctrl_ops *ops = priv->ctrl_ops;

	gve_turnup(priv);
	ops->report_link_status(priv);
	gve_handle_link_status(priv);
}

static struct gve_notify_block *gve_get_tx_notify_block(struct gve_priv *priv,
							unsigned int txqueue)
{
	u32 ntfy_idx;

	if (txqueue > priv->tx_cfg.num_queues)
		return NULL;

	ntfy_idx = gve_tx_idx_to_ntfy(priv, txqueue);
	if (ntfy_idx >= priv->num_ntfy_blks)
		return NULL;

	return &priv->ntfy_blocks[ntfy_idx];
}

static bool gve_tx_timeout_try_q_kick(struct gve_priv *priv,
				      unsigned int txqueue)
{
	struct gve_notify_block *block;
	u32 current_time;

	block = gve_get_tx_notify_block(priv, txqueue);

	if (!block)
		return false;

	current_time = jiffies_to_msecs(jiffies);
	if (block->tx->last_kick_msec + MIN_TX_TIMEOUT_GAP > current_time)
		return false;

	netdev_info(priv->dev, "Kicking queue %d", txqueue);
	napi_schedule(&block->napi);
	block->tx->last_kick_msec = current_time;
	return true;
}

static void gve_tx_timeout(struct net_device *dev, unsigned int txqueue)
{
	struct gve_notify_block *block;
	struct gve_priv *priv;

	netdev_info(dev, "Timeout on tx queue, %d", txqueue);
	priv = netdev_priv(dev);

	if (!gve_tx_timeout_try_q_kick(priv, txqueue))
		gve_schedule_reset(priv);

	block = gve_get_tx_notify_block(priv, txqueue);
	if (block)
		block->tx->queue_timeout++;
	priv->tx_timeo_cnt++;
}

/* Header split is only supported on DQ RDA queue format. If XDP is enabled,
 * header split is not allowed.
 */
bool gve_header_split_supported(const struct gve_priv *priv)
{
	return priv->header_buf_size &&
		priv->queue_format == GVE_DQO_RDA_FORMAT && !priv->xdp_prog;
}

int gve_set_rx_buf_len_config(struct gve_priv *priv, u32 rx_buf_len,
			      struct netlink_ext_ack *extack,
			      struct gve_rx_alloc_rings_cfg *rx_alloc_cfg)
{
	u32 old_rx_buf_len = rx_alloc_cfg->packet_buffer_size;

	if (rx_buf_len == old_rx_buf_len)
		return 0;

	/* device options may not always contain support for 4K buffers */
	if (!gve_is_dqo(priv) || priv->max_rx_buffer_size < SZ_4K) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Modifying Rx buf len is not supported");
		return -EOPNOTSUPP;
	}

	if (priv->xdp_prog && rx_buf_len != SZ_2K) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Rx buf len can only be 2048 when XDP is on");
		return -EINVAL;
	}

	if (rx_buf_len != SZ_2K && rx_buf_len != SZ_4K) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Rx buf len can only be 2048 or 4096");
		return -EINVAL;
	}
	rx_alloc_cfg->packet_buffer_size = rx_buf_len;

	return 0;
}

int gve_set_hsplit_config(struct gve_priv *priv, u8 tcp_data_split,
			  struct gve_rx_alloc_rings_cfg *rx_alloc_cfg)
{
	bool enable_hdr_split;

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

	rx_alloc_cfg->enable_header_split = enable_hdr_split;

	return 0;
}

static int gve_set_features(struct net_device *netdev,
			    netdev_features_t features)
{
	const netdev_features_t orig_features = netdev->features;
	struct gve_tx_alloc_rings_cfg tx_alloc_cfg = {0};
	struct gve_rx_alloc_rings_cfg rx_alloc_cfg = {0};
	struct gve_priv *priv = netdev_priv(netdev);
	int err;

	gve_get_curr_alloc_cfgs(priv, &tx_alloc_cfg, &rx_alloc_cfg);

	if ((netdev->features & NETIF_F_GRO_HW) !=
	    (features & NETIF_F_GRO_HW)) {
		netdev->features ^= NETIF_F_GRO_HW;
		if (priv->xdp_prog && (netdev->features & NETIF_F_GRO_HW)) {
			netdev_warn(netdev,
				    "HW-GRO is not supported when XDP is on.");
			err = -EOPNOTSUPP;
			goto revert_features;
		}
		if (netif_running(netdev)) {
			err = gve_adjust_config(priv, &tx_alloc_cfg, &rx_alloc_cfg);
			if (err)
				goto revert_features;
		}
	}
	if ((netdev->features & NETIF_F_NTUPLE) && !(features & NETIF_F_NTUPLE)) {
		err = gve_flow_rules_reset(priv);
		if (err) {
			if (err == -ETIME)
				gve_schedule_reset(priv);
			goto revert_features;
		}
	}

	return 0;

revert_features:
	netdev->features = orig_features;
	return err;
}

static int gve_get_ts_config(struct net_device *dev,
			     struct kernel_hwtstamp_config *kernel_config)
{
	struct gve_priv *priv = netdev_priv(dev);

	*kernel_config = priv->ts_config;
	return 0;
}

static int gve_set_ts_config(struct net_device *dev,
			     struct kernel_hwtstamp_config *kernel_config,
			     struct netlink_ext_ack *extack)
{
	struct gve_priv *priv = netdev_priv(dev);

	if (kernel_config->tx_type != HWTSTAMP_TX_OFF) {
		NL_SET_ERR_MSG_MOD(extack, "TX timestamping is not supported");
		return -ERANGE;
	}

	if (kernel_config->rx_filter != HWTSTAMP_FILTER_NONE) {
		if (!gve_is_clock_enabled(priv)) {
			NL_SET_ERR_MSG_MOD(extack,
					   "RX timestamping is not supported");
			kernel_config->rx_filter = HWTSTAMP_FILTER_NONE;
			return -EOPNOTSUPP;
		}

		kernel_config->rx_filter = HWTSTAMP_FILTER_ALL;
	}

	priv->ts_config.rx_filter = kernel_config->rx_filter;

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
	.ndo_hwtstamp_get	=	gve_get_ts_config,
	.ndo_hwtstamp_set	=	gve_set_ts_config,
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
		netdev_lock(priv->dev);
		if (gve_get_do_reset(priv))
			gve_reset(priv, false);
		netdev_unlock(priv->dev);
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
				.value = cpu_to_be64(priv->rx[idx].fill_cnt),
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
	const struct gve_ctrl_ops *ops = priv->ctrl_ops;
	u32 status = ioread32be(&priv->reg_bar0->device_status);

	gve_handle_status(priv, status);

	gve_handle_reset(priv);
	ops->report_link_status(priv);
	gve_handle_link_status(priv);
}

static void gve_set_netdev_xdp_features(struct gve_priv *priv)
{
	xdp_features_t xdp_features;

	if (priv->queue_format == GVE_GQI_QPL_FORMAT) {
		xdp_features = NETDEV_XDP_ACT_BASIC;
		xdp_features |= NETDEV_XDP_ACT_REDIRECT;
		xdp_features |= NETDEV_XDP_ACT_XSK_ZEROCOPY;
	} else if (priv->queue_format == GVE_DQO_RDA_FORMAT) {
		xdp_features = NETDEV_XDP_ACT_BASIC;
		xdp_features |= NETDEV_XDP_ACT_REDIRECT;
		xdp_features |= NETDEV_XDP_ACT_XSK_ZEROCOPY;
	} else {
		xdp_features = 0;
	}

	xdp_set_features_flag_locked(priv->dev, xdp_features);
}

static const struct xdp_metadata_ops gve_xdp_metadata_ops = {
	.xmo_rx_timestamp	= gve_xdp_rx_timestamp,
};

static void gve_set_desc_cnt(struct gve_priv *priv)
{
	struct gve_device_info *device_info = &priv->device_info;

	priv->tx_desc_cnt = device_info->default_tx_ring_size;
	priv->rx_desc_cnt = device_info->default_rx_ring_size;
	priv->max_tx_desc_cnt = device_info->max_tx_ring_size;
	priv->max_rx_desc_cnt = device_info->max_rx_ring_size;
	priv->min_tx_desc_cnt = device_info->min_tx_ring_size;
	priv->min_rx_desc_cnt = device_info->min_rx_ring_size;
}

static void gve_set_queue_properties(struct gve_priv *priv)
{
	struct gve_device_info *device_info = &priv->device_info;

	gve_set_desc_cnt(priv);
	priv->max_registered_pages = device_info->max_registered_pages;
	priv->tx_pages_per_qpl = device_info->tx_pages_per_qpl;
}

static int gve_set_mtu(struct gve_priv *priv)
{
	struct gve_device_info *device_info = &priv->device_info;
	u16 mtu;

	mtu = device_info->max_mtu;
	if (mtu < ETH_MIN_MTU) {
		dev_err(&priv->pdev->dev, "MTU %d below minimum MTU\n", mtu);
		return -EINVAL;
	}
	priv->dev->max_mtu = mtu;
	priv->dev->mtu = mtu;

	return 0;
}

static void gve_set_mac(struct gve_priv *priv)
{
	struct gve_device_info *device_info = &priv->device_info;
	u8 *mac;

	mac = device_info->mac;
	eth_hw_addr_set(priv->dev, mac);
	dev_info(&priv->pdev->dev, "MAC addr: %pM\n", mac);
}

static void gve_set_buf_sizes(struct gve_priv *priv)
{
	struct gve_device_info *device_info = &priv->device_info;

	if (device_info->max_rx_buffer_size)
		priv->max_rx_buffer_size = device_info->max_rx_buffer_size;

	if (gve_is_dqo(priv) &&
	    priv->max_rx_buffer_size > GVE_DEFAULT_RX_BUFFER_SIZE)
		priv->rx_cfg.packet_buffer_size = priv->max_rx_buffer_size;

	if (device_info->header_buf_size)
		priv->header_buf_size = device_info->header_buf_size;
}

static int gve_setup_device(struct gve_priv *priv)
{
	int err;

	priv->num_registered_pages = 0;

	gve_set_netdev_xdp_features(priv);
	if (!gve_is_gqi(priv))
		priv->dev->xdp_metadata_ops = &gve_xdp_metadata_ops;

	err = gve_alloc_control_plane_resources(priv);
	if (err)
		return err;

	err = gve_setup_control_plane_resources(priv);
	if (err)
		return err;

	err = gve_setup_notify_blocks(priv);
	if (err)
		return err;
	return 0;
}

static const struct gve_ctrl_ops gve_adminq_ops = {
	.map_db_bar		= gve_adminq_map_db_bar,
	.unmap_db_bar		= gve_adminq_unmap_db_bar,
	.set_num_queues		= gve_adminq_set_num_queues,
	.set_num_ntfy_blks	= gve_adminq_set_num_ntfy_blks,
	.get_ptype_map		= gve_adminq_get_ptype_map_dqo,
	.reset_flow_rules	= gve_adminq_reset_flow_rules,
	.setup_stats_report	= gve_adminq_report_stats,
	.configure_rss		= gve_adminq_configure_rss,
	.request_db_info	= gve_adminq_request_db_info,
	.release_db_resources	= gve_adminq_release_db_resources,
	.setup_mgmt_irq		= gve_adminq_setup_mgmt_irq,
	.teardown_mgmt_irq	= gve_adminq_teardown_mgmt_irq,
	.create_queues		= gve_adminq_create_queues,
	.destroy_queues		= gve_adminq_destroy_queues,
	.report_link_status	= gve_adminq_report_link_status,
	.report_link_speed	= gve_adminq_report_link_speed,
};

static int gve_init_priv(struct gve_priv *priv)
{
	struct gve_device_info *device_info = &priv->device_info;
	int err;

	priv->queue_format = priv->device_info.queue_format;

	err = priv->ctrl_ops->set_num_ntfy_blks(priv);
	if (err) {
		dev_err(&priv->pdev->dev,
			"Could not setup notify blocks: err=%d\n", err);
		return err;
	}

	priv->ctrl_ops->set_num_queues(priv);
	dev_info(&priv->pdev->dev, "TX queues %d, RX queues %d\n",
		 priv->tx_cfg.num_queues, priv->rx_cfg.num_queues);
	dev_info(&priv->pdev->dev, "Max TX queues %d, Max RX queues %d\n",
		 priv->tx_cfg.max_queues, priv->rx_cfg.max_queues);

	if (gve_is_dqo(priv)) {
		/* DQO supports HW-GRO and UDP_GSO */
		u64 additional_features = NETIF_F_GRO_HW | NETIF_F_GSO_UDP_L4;

		priv->dev->hw_features |= additional_features;
		priv->dev->features |= additional_features;

		priv->tx_coalesce_usecs = GVE_TX_IRQ_RATELIMIT_US_DQO;
		priv->rx_coalesce_usecs = GVE_RX_IRQ_RATELIMIT_US_DQO;

		/* Big TCP is only supported on DQO */
		netif_set_tso_max_size(priv->dev, GVE_DQO_TX_MAX);
	}

	if (gve_set_mtu(priv))
		return -EINVAL;

	priv->num_event_counters = device_info->num_event_counters;

	gve_set_mac(priv);

	gve_set_queue_properties(priv);
	priv->modify_ring_size_enabled = device_info->modify_ring_size_enabled;

	gve_set_buf_sizes(priv);

	priv->max_flow_rules = device_info->max_flow_rules;
	if (priv->max_flow_rules)
		priv->dev->hw_features |= NETIF_F_NTUPLE;

	priv->rss_key_size = device_info->rss_key_size;
	priv->rss_lut_size = device_info->rss_lut_size;
	priv->cache_rss_config = device_info->cache_rss_config;

	priv->numa_node = dev_to_node(&priv->pdev->dev);
	priv->tx_cfg.num_xdp_queues = 0;
	priv->rx_copybreak = GVE_DEFAULT_RX_COPYBREAK;
	priv->ts_config.tx_type = HWTSTAMP_TX_OFF;
	priv->ts_config.rx_filter = HWTSTAMP_FILTER_NONE;
	priv->nic_timestamp_supported = device_info->nic_timestamp_supported;
	return 0;
}

static int gve_recover(struct gve_priv *priv, bool setup_queues)
{
	int err;

	err = gve_adminq_init(priv);
	if (err) {
		dev_err(&priv->pdev->dev,
			"Failed to alloc admin queue: err=%d\n", err);
		goto teardown_device;
	}

	err = gve_adminq_verify_driver_compatibility(priv);
	if (err) {
		dev_err(&priv->pdev->dev,
			"Could not verify driver compatibility: err=%d\n", err);
		goto teardown_device;
	}

	err = gve_setup_device(priv);
	if (err)
		goto teardown_device;

	if (setup_queues) {
		/* On failure, hold on to the control plane to give a
		 * chance for the queues to be brought up later.
		 */
		err = gve_open(priv->dev);
		if (err) {
			dev_err(&priv->pdev->dev,
				"Failed to start queues: err=%d, !!! DISABLING ALL QUEUES !!!\n",
				err);
			return err;
		}
	}

	/* undo any detach from an earlier failure */
	netif_device_attach(priv->dev);

	return 0;

teardown_device:
	dev_err(&priv->pdev->dev, "Recover failed: err=%d, detaching device\n",
		err);
	netif_device_detach(priv->dev);
	gve_teardown_device(priv);
	return err;
}

int gve_reset(struct gve_priv *priv, bool skip_queue_setup)
{
	bool was_up = netif_running(priv->dev);
	int err;

	if (gve_get_reset_in_progress(priv))
		return 0;

	dev_info(&priv->pdev->dev, "Performing reset\n");
	gve_clear_do_reset(priv);
	gve_set_reset_in_progress(priv);

	if (was_up) {
		gve_turndown(priv);
		if (gve_get_device_rings_ok(priv)) {
			gve_clear_device_rings_ok(priv);
			gve_destroy_rings(priv);
			gve_unregister_qpls(priv);
		}
	}

	disable_work(&priv->service_task);
	gve_reset_device(priv);
	gve_queues_stop(priv);
	gve_queues_mem_remove(priv);
	gve_teardown_notify_blocks(priv);
	gve_free_control_plane_resources(priv);

	enable_work(&priv->service_task);
	err = gve_recover(priv, was_up && !skip_queue_setup);
	if (err)
		dev_info(&priv->pdev->dev,
			 "Failed to recover in reset: %d\n", err);

	gve_clear_reset_in_progress(priv);
	priv->reset_cnt++;
	priv->interface_up_cnt = 0;
	priv->interface_down_cnt = 0;
	priv->stats_report_trigger_cnt = 0;
	return err;
}

void gve_adminq_write_version(u8 __iomem *driver_version_register)
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

static int gve_rx_queue_stop(struct net_device *dev, void *per_q_mem, int idx)
{
	struct gve_priv *priv = netdev_priv(dev);
	struct gve_rx_ring *gve_per_q_mem;
	int err;

	if (!priv->rx)
		return -EAGAIN;

	/* Destroying queue 0 while other queues exist is not supported in DQO */
	if (!gve_is_gqi(priv) && idx == 0)
		return -ERANGE;

	/* Single-queue destruction requires quiescence on all queues */
	gve_turndown(priv);

	/* This failure will trigger a reset - no need to clean up */
	err = gve_adminq_destroy_single_rx_queue(priv, idx);
	if (err)
		return err;

	if (gve_is_qpl(priv)) {
		/* This failure will trigger a reset - no need to clean up */
		err = gve_unregister_qpl(priv, gve_rx_get_qpl(priv, idx));
		if (err)
			return err;
	}

	gve_rx_stop_ring(priv, idx);

	/* Turn the unstopped queues back up */
	gve_turnup_and_check_status(priv);

	gve_per_q_mem = (struct gve_rx_ring *)per_q_mem;
	*gve_per_q_mem = priv->rx[idx];
	memset(&priv->rx[idx], 0, sizeof(priv->rx[idx]));
	return 0;
}

static void gve_rx_queue_mem_free(struct net_device *dev, void *per_q_mem)
{
	struct gve_priv *priv = netdev_priv(dev);
	struct gve_rx_alloc_rings_cfg cfg = {0};
	struct gve_rx_ring *gve_per_q_mem;

	gve_per_q_mem = (struct gve_rx_ring *)per_q_mem;
	gve_rx_get_curr_alloc_cfg(priv, &cfg);

	if (gve_is_gqi(priv))
		gve_rx_free_ring_gqi(priv, gve_per_q_mem, &cfg);
	else
		gve_rx_free_ring_dqo(priv, gve_per_q_mem, &cfg);
}

static int gve_rx_queue_mem_alloc(struct net_device *dev,
				  struct netdev_queue_config *qcfg,
				  void *per_q_mem, int idx)
{
	struct gve_priv *priv = netdev_priv(dev);
	struct gve_rx_alloc_rings_cfg cfg = {0};
	struct gve_rx_ring *gve_per_q_mem;
	int err;

	if (!priv->rx)
		return -EAGAIN;

	gve_per_q_mem = (struct gve_rx_ring *)per_q_mem;
	gve_rx_get_curr_alloc_cfg(priv, &cfg);

	if (gve_is_gqi(priv))
		err = gve_rx_alloc_ring_gqi(priv, &cfg, gve_per_q_mem, idx);
	else
		err = gve_rx_alloc_ring_dqo(priv, &cfg, gve_per_q_mem, idx);

	return err;
}

static int gve_rx_queue_start(struct net_device *dev,
			      struct netdev_queue_config *qcfg,
			      void *per_q_mem, int idx)
{
	struct gve_priv *priv = netdev_priv(dev);
	struct gve_rx_ring *gve_per_q_mem;
	int err;

	if (!priv->rx)
		return -EAGAIN;

	gve_per_q_mem = (struct gve_rx_ring *)per_q_mem;
	priv->rx[idx] = *gve_per_q_mem;

	/* Single-queue creation requires quiescence on all queues */
	gve_turndown(priv);

	gve_rx_start_ring(priv, idx);

	if (gve_is_qpl(priv)) {
		/* This failure will trigger a reset - no need to clean up */
		err = gve_register_qpl(priv, gve_rx_get_qpl(priv, idx));
		if (err)
			goto abort;
	}

	/* This failure will trigger a reset - no need to clean up */
	err = gve_adminq_create_single_rx_queue(priv, idx);
	if (err)
		goto abort;

	if (gve_is_gqi(priv))
		gve_rx_write_doorbell(priv, &priv->rx[idx]);
	else
		gve_rx_post_buffers_dqo(&priv->rx[idx]);

	/* Turn the unstopped queues back up */
	gve_turnup_and_check_status(priv);
	return 0;

abort:
	gve_rx_stop_ring(priv, idx);

	/* All failures in this func result in a reset, by clearing the struct
	 * at idx, we prevent a double free when that reset runs. The reset,
	 * which needs the rtnl lock, will not run till this func returns and
	 * its caller gives up the lock.
	 */
	memset(&priv->rx[idx], 0, sizeof(priv->rx[idx]));
	return err;
}

static const struct netdev_queue_mgmt_ops gve_queue_mgmt_ops = {
	.ndo_queue_mem_size	=	sizeof(struct gve_rx_ring),
	.ndo_queue_mem_alloc	=	gve_rx_queue_mem_alloc,
	.ndo_queue_mem_free	=	gve_rx_queue_mem_free,
	.ndo_queue_start	=	gve_rx_queue_start,
	.ndo_queue_stop		=	gve_rx_queue_stop,
};

static void gve_get_rx_queue_stats(struct net_device *dev, int idx,
				   struct netdev_queue_stats_rx *rx_stats)
{
	struct gve_priv *priv = netdev_priv(dev);
	struct gve_rx_ring *rx = &priv->rx[idx];
	unsigned int start;

	do {
		start = u64_stats_fetch_begin(&rx->statss);
		rx_stats->packets = rx->rpackets;
		rx_stats->bytes = rx->rbytes;
		rx_stats->alloc_fail = rx->rx_skb_alloc_fail +
				       rx->rx_buf_alloc_fail;
	} while (u64_stats_fetch_retry(&rx->statss, start));
}

static void gve_get_tx_queue_stats(struct net_device *dev, int idx,
				   struct netdev_queue_stats_tx *tx_stats)
{
	struct gve_priv *priv = netdev_priv(dev);
	struct gve_tx_ring *tx = &priv->tx[idx];
	unsigned int start;

	do {
		start = u64_stats_fetch_begin(&tx->statss);
		tx_stats->packets = tx->pkt_done;
		tx_stats->bytes = tx->bytes_done;
	} while (u64_stats_fetch_retry(&tx->statss, start));
}

static void gve_get_base_stats(struct net_device *dev,
			       struct netdev_queue_stats_rx *rx,
			       struct netdev_queue_stats_tx *tx)
{
	rx->packets = 0;
	rx->bytes = 0;
	rx->alloc_fail = 0;

	tx->packets = 0;
	tx->bytes = 0;
}

static const struct netdev_stat_ops gve_stat_ops = {
	.get_queue_stats_rx	= gve_get_rx_queue_stats,
	.get_queue_stats_tx	= gve_get_tx_queue_stats,
	.get_base_stats		= gve_get_base_stats,
};

static int gve_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	int max_tx_queues, max_rx_queues;
	struct net_device *dev;
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

	/* Get max queues to alloc etherdev */
	max_tx_queues = ioread32be(&reg_bar->max_tx_queues);
	max_rx_queues = ioread32be(&reg_bar->max_rx_queues);
	/* Alloc and setup the netdev and priv */
	dev = alloc_etherdev_mqs(sizeof(*priv), max_tx_queues, max_rx_queues);
	if (!dev) {
		dev_err(&pdev->dev, "could not allocate netdev\n");
		err = -ENOMEM;
		goto abort_with_reg_bar;
	}
	SET_NETDEV_DEV(dev, &pdev->dev);
	pci_set_drvdata(pdev, dev);
	dev->ethtool_ops = &gve_ethtool_ops;
	dev->netdev_ops = &gve_netdev_ops;
	dev->queue_mgmt_ops = &gve_queue_mgmt_ops;
	dev->stat_ops = &gve_stat_ops;

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
	priv->service_task_flags = 0x0;
	priv->state_flags = 0x0;
	priv->ethtool_flags = 0x0;
	priv->rx_cfg.packet_buffer_size = GVE_DEFAULT_RX_BUFFER_SIZE;
	priv->max_rx_buffer_size = GVE_DEFAULT_RX_BUFFER_SIZE;

	err = gve_adminq_init(priv);
	if (err) {
		dev_err(&priv->pdev->dev,
			"Failed to alloc admin queue: err=%d\n", err);
		goto abort_with_netdev;
	}

	priv->device_info.queue_format = GVE_QUEUE_FORMAT_UNSPECIFIED;
	err = gve_adminq_get_device_properties(priv);
	if (err)
		goto abort_with_adminq;

	/* Set adminq ctrl ops */
	priv->ctrl_ops = &gve_adminq_ops;

	err = priv->ctrl_ops->map_db_bar(priv);
	if (err)
		goto abort_with_adminq;

	gve_set_probe_in_progress(priv);
	priv->gve_wq = alloc_ordered_workqueue("gve", 0);
	if (!priv->gve_wq) {
		dev_err(&pdev->dev, "Could not allocate workqueue");
		err = -ENOMEM;
		goto abort_with_unmap_db_bar;
	}
	INIT_WORK(&priv->service_task, gve_service_task);
	INIT_WORK(&priv->stats_report_task, gve_stats_report_task);
	priv->tx_cfg.max_queues = max_tx_queues;
	priv->rx_cfg.max_queues = max_rx_queues;

	err = gve_init_priv(priv);
	if (err)
		goto abort_with_wq;

	err = gve_setup_device(priv);
	if (err) {
		dev_err(&priv->pdev->dev,
			"Could not setup device: err=%d\n", err);
		goto abort_teardown_device;
	}

	if (!gve_is_gqi(priv) && !gve_is_qpl(priv))
		dev->netmem_tx = NETMEM_TX_DMA;

	err = register_netdev(dev);
	if (err)
		goto abort_teardown_device;

	dev_info(&pdev->dev, "GVE version %s\n", gve_version_str);
	dev_info(&pdev->dev, "GVE queue format %d\n", (int)priv->queue_format);
	gve_clear_probe_in_progress(priv);
	queue_work(priv->gve_wq, &priv->service_task);
	return 0;

abort_teardown_device:
	disable_work_sync(&priv->service_task);
	gve_teardown_device(priv);

abort_with_wq:
	destroy_workqueue(priv->gve_wq);

abort_with_unmap_db_bar:
	priv->ctrl_ops->unmap_db_bar(priv);

abort_with_adminq:
	gve_adminq_free(priv);

abort_with_netdev:
	free_netdev(dev);

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
	void __iomem *reg_bar = priv->reg_bar0;

	unregister_netdev(netdev);
	disable_work_sync(&priv->service_task);
	gve_teardown_device(priv);
	destroy_workqueue(priv->gve_wq);
	priv->ctrl_ops->unmap_db_bar(priv);
	free_netdev(netdev);
	pci_iounmap(pdev, reg_bar);
	pci_release_regions(pdev);
	pci_disable_device(pdev);
}

static void gve_shutdown(struct pci_dev *pdev)
{
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct gve_priv *priv = netdev_priv(netdev);
	bool was_up = netif_running(priv->dev);

	disable_work_sync(&priv->service_task);

	rtnl_lock();
	netdev_lock(netdev);
	if (was_up)
		gve_close(priv->dev);

	/* detach here because gve_close() might attach in recovery */
	netif_device_detach(netdev);
	gve_teardown_device(priv);
	netdev_unlock(netdev);
	rtnl_unlock();
}

static int gve_suspend(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct gve_priv *priv = netdev_priv(netdev);
	bool was_up = netif_running(priv->dev);

	disable_work_sync(&priv->service_task);

	priv->suspend_cnt++;
	rtnl_lock();
	netdev_lock(netdev);
	if (was_up)
		gve_close(priv->dev);
	gve_teardown_device(priv);
	priv->up_before_suspend = was_up;
	netdev_unlock(netdev);
	rtnl_unlock();
	return 0;
}

static int gve_resume(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct gve_priv *priv = netdev_priv(netdev);
	int err;

	priv->resume_cnt++;
	rtnl_lock();
	netdev_lock(netdev);
	enable_work(&priv->service_task);
	err = gve_recover(priv, priv->up_before_suspend);
	netdev_unlock(netdev);
	rtnl_unlock();
	return err;
}

static DEFINE_SIMPLE_DEV_PM_OPS(gve_pm_ops, gve_suspend, gve_resume);

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
	.driver.pm	= pm_sleep_ptr(&gve_pm_ops),
};

module_pci_driver(gve_driver);

MODULE_DEVICE_TABLE(pci, gve_id_table);
MODULE_AUTHOR("Google, Inc.");
MODULE_DESCRIPTION("Google Virtual NIC Driver");
MODULE_LICENSE("Dual MIT/GPL");
MODULE_VERSION(GVE_VERSION);
