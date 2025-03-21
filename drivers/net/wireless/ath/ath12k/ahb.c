// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) 2018-2019 The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2025 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/dma-mapping.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include "ahb.h"
#include "debug.h"
#include "hif.h"

static const struct of_device_id ath12k_ahb_of_match[] = {
	{ .compatible = "qcom,ipq5332-wifi",
	  .data = (void *)ATH12K_HW_IPQ5332_HW10,
	},
	{ }
};

MODULE_DEVICE_TABLE(of, ath12k_ahb_of_match);

#define ATH12K_IRQ_CE0_OFFSET 4

static const char *irq_name[ATH12K_IRQ_NUM_MAX] = {
	"misc-pulse1",
	"misc-latch",
	"sw-exception",
	"watchdog",
	"ce0",
	"ce1",
	"ce2",
	"ce3",
	"ce4",
	"ce5",
	"ce6",
	"ce7",
	"ce8",
	"ce9",
	"ce10",
	"ce11",
	"host2wbm-desc-feed",
	"host2reo-re-injection",
	"host2reo-command",
	"host2rxdma-monitor-ring3",
	"host2rxdma-monitor-ring2",
	"host2rxdma-monitor-ring1",
	"reo2ost-exception",
	"wbm2host-rx-release",
	"reo2host-status",
	"reo2host-destination-ring4",
	"reo2host-destination-ring3",
	"reo2host-destination-ring2",
	"reo2host-destination-ring1",
	"rxdma2host-monitor-destination-mac3",
	"rxdma2host-monitor-destination-mac2",
	"rxdma2host-monitor-destination-mac1",
	"ppdu-end-interrupts-mac3",
	"ppdu-end-interrupts-mac2",
	"ppdu-end-interrupts-mac1",
	"rxdma2host-monitor-status-ring-mac3",
	"rxdma2host-monitor-status-ring-mac2",
	"rxdma2host-monitor-status-ring-mac1",
	"host2rxdma-host-buf-ring-mac3",
	"host2rxdma-host-buf-ring-mac2",
	"host2rxdma-host-buf-ring-mac1",
	"rxdma2host-destination-ring-mac3",
	"rxdma2host-destination-ring-mac2",
	"rxdma2host-destination-ring-mac1",
	"host2tcl-input-ring4",
	"host2tcl-input-ring3",
	"host2tcl-input-ring2",
	"host2tcl-input-ring1",
	"wbm2host-tx-completions-ring4",
	"wbm2host-tx-completions-ring3",
	"wbm2host-tx-completions-ring2",
	"wbm2host-tx-completions-ring1",
	"tcl2host-status-ring",
};

enum ext_irq_num {
	host2wbm_desc_feed = 16,
	host2reo_re_injection,
	host2reo_command,
	host2rxdma_monitor_ring3,
	host2rxdma_monitor_ring2,
	host2rxdma_monitor_ring1,
	reo2host_exception,
	wbm2host_rx_release,
	reo2host_status,
	reo2host_destination_ring4,
	reo2host_destination_ring3,
	reo2host_destination_ring2,
	reo2host_destination_ring1,
	rxdma2host_monitor_destination_mac3,
	rxdma2host_monitor_destination_mac2,
	rxdma2host_monitor_destination_mac1,
	ppdu_end_interrupts_mac3,
	ppdu_end_interrupts_mac2,
	ppdu_end_interrupts_mac1,
	rxdma2host_monitor_status_ring_mac3,
	rxdma2host_monitor_status_ring_mac2,
	rxdma2host_monitor_status_ring_mac1,
	host2rxdma_host_buf_ring_mac3,
	host2rxdma_host_buf_ring_mac2,
	host2rxdma_host_buf_ring_mac1,
	rxdma2host_destination_ring_mac3,
	rxdma2host_destination_ring_mac2,
	rxdma2host_destination_ring_mac1,
	host2tcl_input_ring4,
	host2tcl_input_ring3,
	host2tcl_input_ring2,
	host2tcl_input_ring1,
	wbm2host_tx_completions_ring4,
	wbm2host_tx_completions_ring3,
	wbm2host_tx_completions_ring2,
	wbm2host_tx_completions_ring1,
	tcl2host_status_ring,
};

static u32 ath12k_ahb_read32(struct ath12k_base *ab, u32 offset)
{
	if (ab->ce_remap && offset < HAL_SEQ_WCSS_CMEM_OFFSET)
		return ioread32(ab->mem_ce + offset);
	return ioread32(ab->mem + offset);
}

static void ath12k_ahb_write32(struct ath12k_base *ab, u32 offset,
			       u32 value)
{
	if (ab->ce_remap && offset < HAL_SEQ_WCSS_CMEM_OFFSET)
		iowrite32(value, ab->mem_ce + offset);
	else
		iowrite32(value, ab->mem + offset);
}

static void ath12k_ahb_cancel_workqueue(struct ath12k_base *ab)
{
	int i;

	for (i = 0; i < ab->hw_params->ce_count; i++) {
		struct ath12k_ce_pipe *ce_pipe = &ab->ce.ce_pipe[i];

		if (ath12k_ce_get_attr_flags(ab, i) & CE_ATTR_DIS_INTR)
			continue;

		cancel_work_sync(&ce_pipe->intr_wq);
	}
}

static void ath12k_ahb_ext_grp_disable(struct ath12k_ext_irq_grp *irq_grp)
{
	int i;

	for (i = 0; i < irq_grp->num_irq; i++)
		disable_irq_nosync(irq_grp->ab->irq_num[irq_grp->irqs[i]]);
}

static void __ath12k_ahb_ext_irq_disable(struct ath12k_base *ab)
{
	int i;

	for (i = 0; i < ATH12K_EXT_IRQ_GRP_NUM_MAX; i++) {
		struct ath12k_ext_irq_grp *irq_grp = &ab->ext_irq_grp[i];

		ath12k_ahb_ext_grp_disable(irq_grp);
		if (irq_grp->napi_enabled) {
			napi_synchronize(&irq_grp->napi);
			napi_disable(&irq_grp->napi);
			irq_grp->napi_enabled = false;
		}
	}
}

static void ath12k_ahb_ext_grp_enable(struct ath12k_ext_irq_grp *irq_grp)
{
	int i;

	for (i = 0; i < irq_grp->num_irq; i++)
		enable_irq(irq_grp->ab->irq_num[irq_grp->irqs[i]]);
}

static void ath12k_ahb_setbit32(struct ath12k_base *ab, u8 bit, u32 offset)
{
	u32 val;

	val = ath12k_ahb_read32(ab, offset);
	ath12k_ahb_write32(ab, offset, val | BIT(bit));
}

static void ath12k_ahb_clearbit32(struct ath12k_base *ab, u8 bit, u32 offset)
{
	u32 val;

	val = ath12k_ahb_read32(ab, offset);
	ath12k_ahb_write32(ab, offset, val & ~BIT(bit));
}

static void ath12k_ahb_ce_irq_enable(struct ath12k_base *ab, u16 ce_id)
{
	const struct ce_attr *ce_attr;
	const struct ce_ie_addr *ce_ie_addr = ab->hw_params->ce_ie_addr;
	u32 ie1_reg_addr, ie2_reg_addr, ie3_reg_addr;

	ie1_reg_addr = ce_ie_addr->ie1_reg_addr;
	ie2_reg_addr = ce_ie_addr->ie2_reg_addr;
	ie3_reg_addr = ce_ie_addr->ie3_reg_addr;

	ce_attr = &ab->hw_params->host_ce_config[ce_id];
	if (ce_attr->src_nentries)
		ath12k_ahb_setbit32(ab, ce_id, ie1_reg_addr);

	if (ce_attr->dest_nentries) {
		ath12k_ahb_setbit32(ab, ce_id, ie2_reg_addr);
		ath12k_ahb_setbit32(ab, ce_id + CE_HOST_IE_3_SHIFT,
				    ie3_reg_addr);
	}
}

static void ath12k_ahb_ce_irq_disable(struct ath12k_base *ab, u16 ce_id)
{
	const struct ce_attr *ce_attr;
	const struct ce_ie_addr *ce_ie_addr = ab->hw_params->ce_ie_addr;
	u32 ie1_reg_addr, ie2_reg_addr, ie3_reg_addr;

	ie1_reg_addr = ce_ie_addr->ie1_reg_addr;
	ie2_reg_addr = ce_ie_addr->ie2_reg_addr;
	ie3_reg_addr = ce_ie_addr->ie3_reg_addr;

	ce_attr = &ab->hw_params->host_ce_config[ce_id];
	if (ce_attr->src_nentries)
		ath12k_ahb_clearbit32(ab, ce_id, ie1_reg_addr);

	if (ce_attr->dest_nentries) {
		ath12k_ahb_clearbit32(ab, ce_id, ie2_reg_addr);
		ath12k_ahb_clearbit32(ab, ce_id + CE_HOST_IE_3_SHIFT,
				      ie3_reg_addr);
	}
}

static void ath12k_ahb_sync_ce_irqs(struct ath12k_base *ab)
{
	int i;
	int irq_idx;

	for (i = 0; i < ab->hw_params->ce_count; i++) {
		if (ath12k_ce_get_attr_flags(ab, i) & CE_ATTR_DIS_INTR)
			continue;

		irq_idx = ATH12K_IRQ_CE0_OFFSET + i;
		synchronize_irq(ab->irq_num[irq_idx]);
	}
}

static void ath12k_ahb_sync_ext_irqs(struct ath12k_base *ab)
{
	int i, j;
	int irq_idx;

	for (i = 0; i < ATH12K_EXT_IRQ_GRP_NUM_MAX; i++) {
		struct ath12k_ext_irq_grp *irq_grp = &ab->ext_irq_grp[i];

		for (j = 0; j < irq_grp->num_irq; j++) {
			irq_idx = irq_grp->irqs[j];
			synchronize_irq(ab->irq_num[irq_idx]);
		}
	}
}

static void ath12k_ahb_ce_irqs_enable(struct ath12k_base *ab)
{
	int i;

	for (i = 0; i < ab->hw_params->ce_count; i++) {
		if (ath12k_ce_get_attr_flags(ab, i) & CE_ATTR_DIS_INTR)
			continue;
		ath12k_ahb_ce_irq_enable(ab, i);
	}
}

static void ath12k_ahb_ce_irqs_disable(struct ath12k_base *ab)
{
	int i;

	for (i = 0; i < ab->hw_params->ce_count; i++) {
		if (ath12k_ce_get_attr_flags(ab, i) & CE_ATTR_DIS_INTR)
			continue;
		ath12k_ahb_ce_irq_disable(ab, i);
	}
}

static int ath12k_ahb_start(struct ath12k_base *ab)
{
	ath12k_ahb_ce_irqs_enable(ab);
	ath12k_ce_rx_post_buf(ab);

	return 0;
}

static void ath12k_ahb_ext_irq_enable(struct ath12k_base *ab)
{
	struct ath12k_ext_irq_grp *irq_grp;
	int i;

	for (i = 0; i < ATH12K_EXT_IRQ_GRP_NUM_MAX; i++) {
		irq_grp = &ab->ext_irq_grp[i];
		if (!irq_grp->napi_enabled) {
			napi_enable(&irq_grp->napi);
			irq_grp->napi_enabled = true;
		}
		ath12k_ahb_ext_grp_enable(irq_grp);
	}
}

static void ath12k_ahb_ext_irq_disable(struct ath12k_base *ab)
{
	__ath12k_ahb_ext_irq_disable(ab);
	ath12k_ahb_sync_ext_irqs(ab);
}

static void ath12k_ahb_stop(struct ath12k_base *ab)
{
	if (!test_bit(ATH12K_FLAG_CRASH_FLUSH, &ab->dev_flags))
		ath12k_ahb_ce_irqs_disable(ab);
	ath12k_ahb_sync_ce_irqs(ab);
	ath12k_ahb_cancel_workqueue(ab);
	del_timer_sync(&ab->rx_replenish_retry);
	ath12k_ce_cleanup_pipes(ab);
}

static void ath12k_ahb_init_qmi_ce_config(struct ath12k_base *ab)
{
	struct ath12k_qmi_ce_cfg *cfg = &ab->qmi.ce_cfg;

	cfg->tgt_ce_len = ab->hw_params->target_ce_count;
	cfg->tgt_ce = ab->hw_params->target_ce_config;
	cfg->svc_to_ce_map_len = ab->hw_params->svc_to_ce_map_len;
	cfg->svc_to_ce_map = ab->hw_params->svc_to_ce_map;
	ab->qmi.service_ins_id = ab->hw_params->qmi_service_ins_id;
}

static void ath12k_ahb_ce_workqueue(struct work_struct *work)
{
	struct ath12k_ce_pipe *ce_pipe = from_work(ce_pipe, work, intr_wq);

	ath12k_ce_per_engine_service(ce_pipe->ab, ce_pipe->pipe_num);

	ath12k_ahb_ce_irq_enable(ce_pipe->ab, ce_pipe->pipe_num);
}

static irqreturn_t ath12k_ahb_ce_interrupt_handler(int irq, void *arg)
{
	struct ath12k_ce_pipe *ce_pipe = arg;

	/* last interrupt received for this CE */
	ce_pipe->timestamp = jiffies;

	ath12k_ahb_ce_irq_disable(ce_pipe->ab, ce_pipe->pipe_num);

	queue_work(system_bh_wq, &ce_pipe->intr_wq);

	return IRQ_HANDLED;
}

static int ath12k_ahb_ext_grp_napi_poll(struct napi_struct *napi, int budget)
{
	struct ath12k_ext_irq_grp *irq_grp = container_of(napi,
						struct ath12k_ext_irq_grp,
						napi);
	struct ath12k_base *ab = irq_grp->ab;
	int work_done;

	work_done = ath12k_dp_service_srng(ab, irq_grp, budget);
	if (work_done < budget) {
		napi_complete_done(napi, work_done);
		ath12k_ahb_ext_grp_enable(irq_grp);
	}

	if (work_done > budget)
		work_done = budget;

	return work_done;
}

static irqreturn_t ath12k_ahb_ext_interrupt_handler(int irq, void *arg)
{
	struct ath12k_ext_irq_grp *irq_grp = arg;

	/* last interrupt received for this group */
	irq_grp->timestamp = jiffies;

	ath12k_ahb_ext_grp_disable(irq_grp);

	napi_schedule(&irq_grp->napi);

	return IRQ_HANDLED;
}

static int ath12k_ahb_config_ext_irq(struct ath12k_base *ab)
{
	const struct ath12k_hw_ring_mask *ring_mask;
	struct ath12k_ext_irq_grp *irq_grp;
	const struct hal_ops *hal_ops;
	int i, j, irq, irq_idx, ret;
	u32 num_irq;

	ring_mask = ab->hw_params->ring_mask;
	hal_ops = ab->hw_params->hal_ops;
	for (i = 0; i < ATH12K_EXT_IRQ_GRP_NUM_MAX; i++) {
		irq_grp = &ab->ext_irq_grp[i];
		num_irq = 0;

		irq_grp->ab = ab;
		irq_grp->grp_id = i;

		irq_grp->napi_ndev = alloc_netdev_dummy(0);
		if (!irq_grp->napi_ndev)
			return -ENOMEM;

		netif_napi_add(irq_grp->napi_ndev, &irq_grp->napi,
			       ath12k_ahb_ext_grp_napi_poll);

		for (j = 0; j < ATH12K_EXT_IRQ_NUM_MAX; j++) {
			/* For TX ring, ensure that the ring mask and the
			 * tcl_to_wbm_rbm_map point to the same ring number.
			 */
			if (ring_mask->tx[i] &
			    BIT(hal_ops->tcl_to_wbm_rbm_map[j].wbm_ring_num)) {
				irq_grp->irqs[num_irq++] =
					wbm2host_tx_completions_ring1 - j;
			}

			if (ring_mask->rx[i] & BIT(j)) {
				irq_grp->irqs[num_irq++] =
					reo2host_destination_ring1 - j;
			}

			if (ring_mask->rx_err[i] & BIT(j))
				irq_grp->irqs[num_irq++] = reo2host_exception;

			if (ring_mask->rx_wbm_rel[i] & BIT(j))
				irq_grp->irqs[num_irq++] = wbm2host_rx_release;

			if (ring_mask->reo_status[i] & BIT(j))
				irq_grp->irqs[num_irq++] = reo2host_status;

			if (ring_mask->rx_mon_dest[i] & BIT(j))
				irq_grp->irqs[num_irq++] =
					rxdma2host_monitor_destination_mac1;
		}

		irq_grp->num_irq = num_irq;

		for (j = 0; j < irq_grp->num_irq; j++) {
			irq_idx = irq_grp->irqs[j];

			irq = platform_get_irq_byname(ab->pdev,
						      irq_name[irq_idx]);
			ab->irq_num[irq_idx] = irq;
			irq_set_status_flags(irq, IRQ_NOAUTOEN | IRQ_DISABLE_UNLAZY);
			ret = devm_request_irq(ab->dev, irq,
					       ath12k_ahb_ext_interrupt_handler,
					       IRQF_TRIGGER_RISING,
					       irq_name[irq_idx], irq_grp);
			if (ret)
				ath12k_warn(ab, "failed request_irq for %d\n", irq);
		}
	}

	return 0;
}

static int ath12k_ahb_config_irq(struct ath12k_base *ab)
{
	int irq, irq_idx, i;
	int ret;

	/* Configure CE irqs */
	for (i = 0; i < ab->hw_params->ce_count; i++) {
		struct ath12k_ce_pipe *ce_pipe = &ab->ce.ce_pipe[i];

		if (ath12k_ce_get_attr_flags(ab, i) & CE_ATTR_DIS_INTR)
			continue;

		irq_idx = ATH12K_IRQ_CE0_OFFSET + i;

		INIT_WORK(&ce_pipe->intr_wq, ath12k_ahb_ce_workqueue);
		irq = platform_get_irq_byname(ab->pdev, irq_name[irq_idx]);
		ret = devm_request_irq(ab->dev, irq, ath12k_ahb_ce_interrupt_handler,
				       IRQF_TRIGGER_RISING, irq_name[irq_idx],
				       ce_pipe);
		if (ret)
			return ret;

		ab->irq_num[irq_idx] = irq;
	}

	/* Configure external interrupts */
	ret = ath12k_ahb_config_ext_irq(ab);

	return ret;
}

static int ath12k_ahb_map_service_to_pipe(struct ath12k_base *ab, u16 service_id,
					  u8 *ul_pipe, u8 *dl_pipe)
{
	const struct service_to_pipe *entry;
	bool ul_set = false, dl_set = false;
	u32 pipedir;
	int i;

	for (i = 0; i < ab->hw_params->svc_to_ce_map_len; i++) {
		entry = &ab->hw_params->svc_to_ce_map[i];

		if (__le32_to_cpu(entry->service_id) != service_id)
			continue;

		pipedir = __le32_to_cpu(entry->pipedir);
		if (pipedir == PIPEDIR_IN || pipedir == PIPEDIR_INOUT) {
			WARN_ON(dl_set);
			*dl_pipe = __le32_to_cpu(entry->pipenum);
			dl_set = true;
		}

		if (pipedir == PIPEDIR_OUT || pipedir == PIPEDIR_INOUT) {
			WARN_ON(ul_set);
			*ul_pipe = __le32_to_cpu(entry->pipenum);
			ul_set = true;
		}
	}

	if (WARN_ON(!ul_set || !dl_set))
		return -ENOENT;

	return 0;
}

static const struct ath12k_hif_ops ath12k_ahb_hif_ops_ipq5332 = {
	.start = ath12k_ahb_start,
	.stop = ath12k_ahb_stop,
	.read32 = ath12k_ahb_read32,
	.write32 = ath12k_ahb_write32,
	.irq_enable = ath12k_ahb_ext_irq_enable,
	.irq_disable = ath12k_ahb_ext_irq_disable,
	.map_service_to_pipe = ath12k_ahb_map_service_to_pipe,
};

static int ath12k_ahb_resource_init(struct ath12k_base *ab)
{
	struct ath12k_ahb *ab_ahb = ath12k_ab_to_ahb(ab);
	struct platform_device *pdev = ab->pdev;
	struct resource *mem_res;
	int ret;

	ab->mem = devm_platform_get_and_ioremap_resource(pdev, 0, &mem_res);
	if (IS_ERR(ab->mem)) {
		ret = dev_err_probe(&pdev->dev, PTR_ERR(ab->mem), "ioremap error\n");
		goto out;
	}

	ab->mem_len = resource_size(mem_res);

	if (ab->hw_params->ce_remap) {
		const struct ce_remap *ce_remap = ab->hw_params->ce_remap;
		/* CE register space is moved out of WCSS and the space is not
		 * contiguous, hence remapping the CE registers to a new space
		 * for accessing them.
		 */
		ab->mem_ce = ioremap(ce_remap->base, ce_remap->size);
		if (IS_ERR(ab->mem_ce)) {
			dev_err(&pdev->dev, "ce ioremap error\n");
			ret = -ENOMEM;
			goto err_mem_unmap;
		}
		ab->ce_remap = true;
		ab->ce_remap_base_addr = HAL_IPQ5332_CE_WFSS_REG_BASE;
	}

	ab_ahb->xo_clk = devm_clk_get(ab->dev, "xo");
	if (IS_ERR(ab_ahb->xo_clk)) {
		ret = dev_err_probe(&pdev->dev, PTR_ERR(ab_ahb->xo_clk),
				    "failed to get xo clock\n");
		goto err_mem_ce_unmap;
	}

	ret = clk_prepare_enable(ab_ahb->xo_clk);
	if (ret) {
		dev_err(&pdev->dev, "failed to enable gcc_xo_clk: %d\n", ret);
		goto err_clock_deinit;
	}

	return 0;

err_clock_deinit:
	devm_clk_put(ab->dev, ab_ahb->xo_clk);

err_mem_ce_unmap:
	ab_ahb->xo_clk = NULL;
	if (ab->hw_params->ce_remap)
		iounmap(ab->mem_ce);

err_mem_unmap:
	ab->mem_ce = NULL;
	devm_iounmap(ab->dev, ab->mem);

out:
	ab->mem = NULL;
	return ret;
}

static void ath12k_ahb_resource_deinit(struct ath12k_base *ab)
{
	struct ath12k_ahb *ab_ahb = ath12k_ab_to_ahb(ab);

	if (ab->mem)
		devm_iounmap(ab->dev, ab->mem);

	if (ab->mem_ce)
		iounmap(ab->mem_ce);

	ab->mem = NULL;
	ab->mem_ce = NULL;

	clk_disable_unprepare(ab_ahb->xo_clk);
	devm_clk_put(ab->dev, ab_ahb->xo_clk);
	ab_ahb->xo_clk = NULL;
}

static int ath12k_ahb_probe(struct platform_device *pdev)
{
	struct ath12k_base *ab;
	const struct ath12k_hif_ops *hif_ops;
	enum ath12k_hw_rev hw_rev;
	u32 addr;
	int ret;

	ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
	if (ret) {
		dev_err(&pdev->dev, "Failed to set 32-bit coherent dma\n");
		return ret;
	}

	ab = ath12k_core_alloc(&pdev->dev, sizeof(struct ath12k_ahb),
			       ATH12K_BUS_AHB);
	if (!ab)
		return -ENOMEM;

	hw_rev = (enum ath12k_hw_rev)(kernel_ulong_t)of_device_get_match_data(&pdev->dev);
	switch (hw_rev) {
	case ATH12K_HW_IPQ5332_HW10:
		hif_ops = &ath12k_ahb_hif_ops_ipq5332;
		break;
	default:
		ret = -EOPNOTSUPP;
		goto err_core_free;
	}

	ab->hif.ops = hif_ops;
	ab->pdev = pdev;
	ab->hw_rev = hw_rev;
	platform_set_drvdata(pdev, ab);

	/* Set fixed_mem_region to true for platforms that support fixed memory
	 * reservation from DT. If memory is reserved from DT for FW, ath12k driver
	 * need not to allocate memory.
	 */
	if (!of_property_read_u32(ab->dev->of_node, "memory-region", &addr))
		set_bit(ATH12K_FLAG_FIXED_MEM_REGION, &ab->dev_flags);

	ret = ath12k_core_pre_init(ab);
	if (ret)
		goto err_core_free;

	ret = ath12k_ahb_resource_init(ab);
	if (ret)
		goto err_core_free;

	ret = ath12k_hal_srng_init(ab);
	if (ret)
		goto err_resource_deinit;

	ret = ath12k_ce_alloc_pipes(ab);
	if (ret) {
		ath12k_err(ab, "failed to allocate ce pipes: %d\n", ret);
		goto err_hal_srng_deinit;
	}

	ath12k_ahb_init_qmi_ce_config(ab);

	ret = ath12k_ahb_config_irq(ab);
	if (ret) {
		ath12k_err(ab, "failed to configure irq: %d\n", ret);
		goto err_ce_free;
	}

	ret = ath12k_core_init(ab);
	if (ret) {
		ath12k_err(ab, "failed to init core: %d\n", ret);
		goto err_ce_free;
	}

	return 0;

err_ce_free:
	ath12k_ce_free_pipes(ab);

err_hal_srng_deinit:
	ath12k_hal_srng_deinit(ab);

err_resource_deinit:
	ath12k_ahb_resource_deinit(ab);

err_core_free:
	ath12k_core_free(ab);
	platform_set_drvdata(pdev, NULL);

	return ret;
}

static void ath12k_ahb_remove_prepare(struct ath12k_base *ab)
{
	unsigned long left;

	if (test_bit(ATH12K_FLAG_RECOVERY, &ab->dev_flags)) {
		left = wait_for_completion_timeout(&ab->driver_recovery,
						   ATH12K_AHB_RECOVERY_TIMEOUT);
		if (!left)
			ath12k_warn(ab, "failed to receive recovery response completion\n");
	}

	set_bit(ATH12K_FLAG_UNREGISTERING, &ab->dev_flags);
	cancel_work_sync(&ab->restart_work);
	cancel_work_sync(&ab->qmi.event_work);
}

static void ath12k_ahb_free_resources(struct ath12k_base *ab)
{
	struct platform_device *pdev = ab->pdev;

	ath12k_hal_srng_deinit(ab);
	ath12k_ce_free_pipes(ab);
	ath12k_ahb_resource_deinit(ab);
	ath12k_core_free(ab);
	platform_set_drvdata(pdev, NULL);
}

static void ath12k_ahb_remove(struct platform_device *pdev)
{
	struct ath12k_base *ab = platform_get_drvdata(pdev);

	if (test_bit(ATH12K_FLAG_QMI_FAIL, &ab->dev_flags)) {
		ath12k_qmi_deinit_service(ab);
		goto qmi_fail;
	}

	ath12k_ahb_remove_prepare(ab);
	ath12k_core_deinit(ab);

qmi_fail:
	ath12k_ahb_free_resources(ab);
}

static struct platform_driver ath12k_ahb_driver = {
	.driver         = {
		.name   = "ath12k_ahb",
		.of_match_table = ath12k_ahb_of_match,
	},
	.probe  = ath12k_ahb_probe,
	.remove = ath12k_ahb_remove,
};

int ath12k_ahb_init(void)
{
	return platform_driver_register(&ath12k_ahb_driver);
}

void ath12k_ahb_exit(void)
{
	platform_driver_unregister(&ath12k_ahb_driver);
}
