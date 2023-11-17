// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) 2019-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) 2021-2022, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include "core.h"
#include "pcic.h"
#include "debug.h"

static const char *irq_name[ATH11K_IRQ_NUM_MAX] = {
	"bhi",
	"mhi-er0",
	"mhi-er1",
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
	"wbm2host-tx-completions-ring3",
	"wbm2host-tx-completions-ring2",
	"wbm2host-tx-completions-ring1",
	"tcl2host-status-ring",
};

static const struct ath11k_msi_config ath11k_msi_config[] = {
	{
		.total_vectors = 32,
		.total_users = 4,
		.users = (struct ath11k_msi_user[]) {
			{ .name = "MHI", .num_vectors = 3, .base_vector = 0 },
			{ .name = "CE", .num_vectors = 10, .base_vector = 3 },
			{ .name = "WAKE", .num_vectors = 1, .base_vector = 13 },
			{ .name = "DP", .num_vectors = 18, .base_vector = 14 },
		},
		.hw_rev = ATH11K_HW_QCA6390_HW20,
	},
	{
		.total_vectors = 16,
		.total_users = 3,
		.users = (struct ath11k_msi_user[]) {
			{ .name = "MHI", .num_vectors = 3, .base_vector = 0 },
			{ .name = "CE", .num_vectors = 5, .base_vector = 3 },
			{ .name = "DP", .num_vectors = 8, .base_vector = 8 },
		},
		.hw_rev = ATH11K_HW_QCN9074_HW10,
	},
	{
		.total_vectors = 32,
		.total_users = 4,
		.users = (struct ath11k_msi_user[]) {
			{ .name = "MHI", .num_vectors = 3, .base_vector = 0 },
			{ .name = "CE", .num_vectors = 10, .base_vector = 3 },
			{ .name = "WAKE", .num_vectors = 1, .base_vector = 13 },
			{ .name = "DP", .num_vectors = 18, .base_vector = 14 },
		},
		.hw_rev = ATH11K_HW_WCN6855_HW20,
	},
	{
		.total_vectors = 32,
		.total_users = 4,
		.users = (struct ath11k_msi_user[]) {
			{ .name = "MHI", .num_vectors = 3, .base_vector = 0 },
			{ .name = "CE", .num_vectors = 10, .base_vector = 3 },
			{ .name = "WAKE", .num_vectors = 1, .base_vector = 13 },
			{ .name = "DP", .num_vectors = 18, .base_vector = 14 },
		},
		.hw_rev = ATH11K_HW_WCN6855_HW21,
	},
	{
		.total_vectors = 28,
		.total_users = 2,
		.users = (struct ath11k_msi_user[]) {
			{ .name = "CE", .num_vectors = 10, .base_vector = 0 },
			{ .name = "DP", .num_vectors = 18, .base_vector = 10 },
		},
		.hw_rev = ATH11K_HW_WCN6750_HW10,
	},
};

int ath11k_pcic_init_msi_config(struct ath11k_base *ab)
{
	const struct ath11k_msi_config *msi_config;
	int i;

	for (i = 0; i < ARRAY_SIZE(ath11k_msi_config); i++) {
		msi_config = &ath11k_msi_config[i];

		if (msi_config->hw_rev == ab->hw_rev)
			break;
	}

	if (i == ARRAY_SIZE(ath11k_msi_config)) {
		ath11k_err(ab, "failed to fetch msi config, unsupported hw version: 0x%x\n",
			   ab->hw_rev);
		return -EINVAL;
	}

	ab->pci.msi.config = msi_config;
	return 0;
}
EXPORT_SYMBOL(ath11k_pcic_init_msi_config);

static void __ath11k_pcic_write32(struct ath11k_base *ab, u32 offset, u32 value)
{
	if (offset < ATH11K_PCI_WINDOW_START)
		iowrite32(value, ab->mem  + offset);
	else
		ab->pci.ops->window_write32(ab, offset, value);
}

void ath11k_pcic_write32(struct ath11k_base *ab, u32 offset, u32 value)
{
	int ret = 0;
	bool wakeup_required;

	/* for offset beyond BAR + 4K - 32, may
	 * need to wakeup the device to access.
	 */
	wakeup_required = test_bit(ATH11K_FLAG_DEVICE_INIT_DONE, &ab->dev_flags) &&
			  offset >= ATH11K_PCI_ACCESS_ALWAYS_OFF;
	if (wakeup_required && ab->pci.ops->wakeup)
		ret = ab->pci.ops->wakeup(ab);

	__ath11k_pcic_write32(ab, offset, value);

	if (wakeup_required && !ret && ab->pci.ops->release)
		ab->pci.ops->release(ab);
}
EXPORT_SYMBOL(ath11k_pcic_write32);

static u32 __ath11k_pcic_read32(struct ath11k_base *ab, u32 offset)
{
	u32 val;

	if (offset < ATH11K_PCI_WINDOW_START)
		val = ioread32(ab->mem + offset);
	else
		val = ab->pci.ops->window_read32(ab, offset);

	return val;
}

u32 ath11k_pcic_read32(struct ath11k_base *ab, u32 offset)
{
	int ret = 0;
	u32 val;
	bool wakeup_required;

	/* for offset beyond BAR + 4K - 32, may
	 * need to wakeup the device to access.
	 */
	wakeup_required = test_bit(ATH11K_FLAG_DEVICE_INIT_DONE, &ab->dev_flags) &&
			  offset >= ATH11K_PCI_ACCESS_ALWAYS_OFF;
	if (wakeup_required && ab->pci.ops->wakeup)
		ret = ab->pci.ops->wakeup(ab);

	val = __ath11k_pcic_read32(ab, offset);

	if (wakeup_required && !ret && ab->pci.ops->release)
		ab->pci.ops->release(ab);

	return val;
}
EXPORT_SYMBOL(ath11k_pcic_read32);

int ath11k_pcic_read(struct ath11k_base *ab, void *buf, u32 start, u32 end)
{
	int ret = 0;
	bool wakeup_required;
	u32 *data = buf;
	u32 i;

	/* for offset beyond BAR + 4K - 32, may
	 * need to wakeup the device to access.
	 */
	wakeup_required = test_bit(ATH11K_FLAG_DEVICE_INIT_DONE, &ab->dev_flags) &&
			  end >= ATH11K_PCI_ACCESS_ALWAYS_OFF;
	if (wakeup_required && ab->pci.ops->wakeup) {
		ret = ab->pci.ops->wakeup(ab);
		if (ret) {
			ath11k_warn(ab,
				    "wakeup failed, data may be invalid: %d",
				    ret);
			/* Even though wakeup() failed, continue processing rather
			 * than returning because some parts of the data may still
			 * be valid and useful in some cases, e.g. could give us
			 * some clues on firmware crash.
			 * Mislead due to invalid data could be avoided because we
			 * are aware of the wakeup failure.
			 */
		}
	}

	for (i = start; i < end + 1; i += 4)
		*data++ = __ath11k_pcic_read32(ab, i);

	if (wakeup_required && ab->pci.ops->release)
		ab->pci.ops->release(ab);

	return 0;
}
EXPORT_SYMBOL(ath11k_pcic_read);

void ath11k_pcic_get_msi_address(struct ath11k_base *ab, u32 *msi_addr_lo,
				 u32 *msi_addr_hi)
{
	*msi_addr_lo = ab->pci.msi.addr_lo;
	*msi_addr_hi = ab->pci.msi.addr_hi;
}
EXPORT_SYMBOL(ath11k_pcic_get_msi_address);

int ath11k_pcic_get_user_msi_assignment(struct ath11k_base *ab, char *user_name,
					int *num_vectors, u32 *user_base_data,
					u32 *base_vector)
{
	const struct ath11k_msi_config *msi_config = ab->pci.msi.config;
	int idx;

	for (idx = 0; idx < msi_config->total_users; idx++) {
		if (strcmp(user_name, msi_config->users[idx].name) == 0) {
			*num_vectors = msi_config->users[idx].num_vectors;
			*base_vector =  msi_config->users[idx].base_vector;
			*user_base_data = *base_vector + ab->pci.msi.ep_base_data;

			ath11k_dbg(ab, ATH11K_DBG_PCI,
				   "msi assignment %s num_vectors %d user_base_data %u base_vector %u\n",
				   user_name, *num_vectors, *user_base_data,
				   *base_vector);

			return 0;
		}
	}

	ath11k_err(ab, "Failed to find MSI assignment for %s!\n", user_name);

	return -EINVAL;
}
EXPORT_SYMBOL(ath11k_pcic_get_user_msi_assignment);

void ath11k_pcic_get_ce_msi_idx(struct ath11k_base *ab, u32 ce_id, u32 *msi_idx)
{
	u32 i, msi_data_idx;

	for (i = 0, msi_data_idx = 0; i < ab->hw_params.ce_count; i++) {
		if (ath11k_ce_get_attr_flags(ab, i) & CE_ATTR_DIS_INTR)
			continue;

		if (ce_id == i)
			break;

		msi_data_idx++;
	}
	*msi_idx = msi_data_idx;
}
EXPORT_SYMBOL(ath11k_pcic_get_ce_msi_idx);

static void ath11k_pcic_free_ext_irq(struct ath11k_base *ab)
{
	int i, j;

	for (i = 0; i < ATH11K_EXT_IRQ_GRP_NUM_MAX; i++) {
		struct ath11k_ext_irq_grp *irq_grp = &ab->ext_irq_grp[i];

		for (j = 0; j < irq_grp->num_irq; j++)
			free_irq(ab->irq_num[irq_grp->irqs[j]], irq_grp);

		netif_napi_del(&irq_grp->napi);
	}
}

void ath11k_pcic_free_irq(struct ath11k_base *ab)
{
	int i, irq_idx;

	for (i = 0; i < ab->hw_params.ce_count; i++) {
		if (ath11k_ce_get_attr_flags(ab, i) & CE_ATTR_DIS_INTR)
			continue;
		irq_idx = ATH11K_PCI_IRQ_CE0_OFFSET + i;
		free_irq(ab->irq_num[irq_idx], &ab->ce.ce_pipe[i]);
	}

	ath11k_pcic_free_ext_irq(ab);
}
EXPORT_SYMBOL(ath11k_pcic_free_irq);

static void ath11k_pcic_ce_irq_enable(struct ath11k_base *ab, u16 ce_id)
{
	u32 irq_idx;

	/* In case of one MSI vector, we handle irq enable/disable in a
	 * uniform way since we only have one irq
	 */
	if (!test_bit(ATH11K_FLAG_MULTI_MSI_VECTORS, &ab->dev_flags))
		return;

	irq_idx = ATH11K_PCI_IRQ_CE0_OFFSET + ce_id;
	enable_irq(ab->irq_num[irq_idx]);
}

static void ath11k_pcic_ce_irq_disable(struct ath11k_base *ab, u16 ce_id)
{
	u32 irq_idx;

	/* In case of one MSI vector, we handle irq enable/disable in a
	 * uniform way since we only have one irq
	 */
	if (!test_bit(ATH11K_FLAG_MULTI_MSI_VECTORS, &ab->dev_flags))
		return;

	irq_idx = ATH11K_PCI_IRQ_CE0_OFFSET + ce_id;
	disable_irq_nosync(ab->irq_num[irq_idx]);
}

static void ath11k_pcic_ce_irqs_disable(struct ath11k_base *ab)
{
	int i;

	clear_bit(ATH11K_FLAG_CE_IRQ_ENABLED, &ab->dev_flags);

	for (i = 0; i < ab->hw_params.ce_count; i++) {
		if (ath11k_ce_get_attr_flags(ab, i) & CE_ATTR_DIS_INTR)
			continue;
		ath11k_pcic_ce_irq_disable(ab, i);
	}
}

static void ath11k_pcic_sync_ce_irqs(struct ath11k_base *ab)
{
	int i;
	int irq_idx;

	for (i = 0; i < ab->hw_params.ce_count; i++) {
		if (ath11k_ce_get_attr_flags(ab, i) & CE_ATTR_DIS_INTR)
			continue;

		irq_idx = ATH11K_PCI_IRQ_CE0_OFFSET + i;
		synchronize_irq(ab->irq_num[irq_idx]);
	}
}

static void ath11k_pcic_ce_tasklet(struct tasklet_struct *t)
{
	struct ath11k_ce_pipe *ce_pipe = from_tasklet(ce_pipe, t, intr_tq);
	int irq_idx = ATH11K_PCI_IRQ_CE0_OFFSET + ce_pipe->pipe_num;

	ath11k_ce_per_engine_service(ce_pipe->ab, ce_pipe->pipe_num);

	enable_irq(ce_pipe->ab->irq_num[irq_idx]);
}

static irqreturn_t ath11k_pcic_ce_interrupt_handler(int irq, void *arg)
{
	struct ath11k_ce_pipe *ce_pipe = arg;
	struct ath11k_base *ab = ce_pipe->ab;
	int irq_idx = ATH11K_PCI_IRQ_CE0_OFFSET + ce_pipe->pipe_num;

	if (!test_bit(ATH11K_FLAG_CE_IRQ_ENABLED, &ab->dev_flags))
		return IRQ_HANDLED;

	/* last interrupt received for this CE */
	ce_pipe->timestamp = jiffies;

	disable_irq_nosync(ab->irq_num[irq_idx]);

	tasklet_schedule(&ce_pipe->intr_tq);

	return IRQ_HANDLED;
}

static void ath11k_pcic_ext_grp_disable(struct ath11k_ext_irq_grp *irq_grp)
{
	struct ath11k_base *ab = irq_grp->ab;
	int i;

	/* In case of one MSI vector, we handle irq enable/disable
	 * in a uniform way since we only have one irq
	 */
	if (!test_bit(ATH11K_FLAG_MULTI_MSI_VECTORS, &ab->dev_flags))
		return;

	for (i = 0; i < irq_grp->num_irq; i++)
		disable_irq_nosync(irq_grp->ab->irq_num[irq_grp->irqs[i]]);
}

static void __ath11k_pcic_ext_irq_disable(struct ath11k_base *sc)
{
	int i;

	clear_bit(ATH11K_FLAG_EXT_IRQ_ENABLED, &sc->dev_flags);

	for (i = 0; i < ATH11K_EXT_IRQ_GRP_NUM_MAX; i++) {
		struct ath11k_ext_irq_grp *irq_grp = &sc->ext_irq_grp[i];

		ath11k_pcic_ext_grp_disable(irq_grp);

		if (irq_grp->napi_enabled) {
			napi_synchronize(&irq_grp->napi);
			napi_disable(&irq_grp->napi);
			irq_grp->napi_enabled = false;
		}
	}
}

static void ath11k_pcic_ext_grp_enable(struct ath11k_ext_irq_grp *irq_grp)
{
	struct ath11k_base *ab = irq_grp->ab;
	int i;

	/* In case of one MSI vector, we handle irq enable/disable in a
	 * uniform way since we only have one irq
	 */
	if (!test_bit(ATH11K_FLAG_MULTI_MSI_VECTORS, &ab->dev_flags))
		return;

	for (i = 0; i < irq_grp->num_irq; i++)
		enable_irq(irq_grp->ab->irq_num[irq_grp->irqs[i]]);
}

void ath11k_pcic_ext_irq_enable(struct ath11k_base *ab)
{
	int i;

	for (i = 0; i < ATH11K_EXT_IRQ_GRP_NUM_MAX; i++) {
		struct ath11k_ext_irq_grp *irq_grp = &ab->ext_irq_grp[i];

		if (!irq_grp->napi_enabled) {
			napi_enable(&irq_grp->napi);
			irq_grp->napi_enabled = true;
		}
		ath11k_pcic_ext_grp_enable(irq_grp);
	}

	set_bit(ATH11K_FLAG_EXT_IRQ_ENABLED, &ab->dev_flags);
}
EXPORT_SYMBOL(ath11k_pcic_ext_irq_enable);

static void ath11k_pcic_sync_ext_irqs(struct ath11k_base *ab)
{
	int i, j, irq_idx;

	for (i = 0; i < ATH11K_EXT_IRQ_GRP_NUM_MAX; i++) {
		struct ath11k_ext_irq_grp *irq_grp = &ab->ext_irq_grp[i];

		for (j = 0; j < irq_grp->num_irq; j++) {
			irq_idx = irq_grp->irqs[j];
			synchronize_irq(ab->irq_num[irq_idx]);
		}
	}
}

void ath11k_pcic_ext_irq_disable(struct ath11k_base *ab)
{
	__ath11k_pcic_ext_irq_disable(ab);
	ath11k_pcic_sync_ext_irqs(ab);
}
EXPORT_SYMBOL(ath11k_pcic_ext_irq_disable);

static int ath11k_pcic_ext_grp_napi_poll(struct napi_struct *napi, int budget)
{
	struct ath11k_ext_irq_grp *irq_grp = container_of(napi,
						struct ath11k_ext_irq_grp,
						napi);
	struct ath11k_base *ab = irq_grp->ab;
	int work_done;
	int i;

	work_done = ath11k_dp_service_srng(ab, irq_grp, budget);
	if (work_done < budget) {
		napi_complete_done(napi, work_done);
		for (i = 0; i < irq_grp->num_irq; i++)
			enable_irq(irq_grp->ab->irq_num[irq_grp->irqs[i]]);
	}

	if (work_done > budget)
		work_done = budget;

	return work_done;
}

static irqreturn_t ath11k_pcic_ext_interrupt_handler(int irq, void *arg)
{
	struct ath11k_ext_irq_grp *irq_grp = arg;
	struct ath11k_base *ab = irq_grp->ab;
	int i;

	if (!test_bit(ATH11K_FLAG_EXT_IRQ_ENABLED, &ab->dev_flags))
		return IRQ_HANDLED;

	ath11k_dbg(irq_grp->ab, ATH11K_DBG_PCI, "ext irq %d\n", irq);

	/* last interrupt received for this group */
	irq_grp->timestamp = jiffies;

	for (i = 0; i < irq_grp->num_irq; i++)
		disable_irq_nosync(irq_grp->ab->irq_num[irq_grp->irqs[i]]);

	napi_schedule(&irq_grp->napi);

	return IRQ_HANDLED;
}

static int
ath11k_pcic_get_msi_irq(struct ath11k_base *ab, unsigned int vector)
{
	return ab->pci.ops->get_msi_irq(ab, vector);
}

static int ath11k_pcic_ext_irq_config(struct ath11k_base *ab)
{
	int i, j, ret, num_vectors = 0;
	u32 user_base_data = 0, base_vector = 0;
	unsigned long irq_flags;

	ret = ath11k_pcic_get_user_msi_assignment(ab, "DP", &num_vectors,
						  &user_base_data,
						  &base_vector);
	if (ret < 0)
		return ret;

	irq_flags = IRQF_SHARED;
	if (!test_bit(ATH11K_FLAG_MULTI_MSI_VECTORS, &ab->dev_flags))
		irq_flags |= IRQF_NOBALANCING;

	for (i = 0; i < ATH11K_EXT_IRQ_GRP_NUM_MAX; i++) {
		struct ath11k_ext_irq_grp *irq_grp = &ab->ext_irq_grp[i];
		u32 num_irq = 0;

		irq_grp->ab = ab;
		irq_grp->grp_id = i;
		init_dummy_netdev(&irq_grp->napi_ndev);
		netif_napi_add(&irq_grp->napi_ndev, &irq_grp->napi,
			       ath11k_pcic_ext_grp_napi_poll);

		if (ab->hw_params.ring_mask->tx[i] ||
		    ab->hw_params.ring_mask->rx[i] ||
		    ab->hw_params.ring_mask->rx_err[i] ||
		    ab->hw_params.ring_mask->rx_wbm_rel[i] ||
		    ab->hw_params.ring_mask->reo_status[i] ||
		    ab->hw_params.ring_mask->rxdma2host[i] ||
		    ab->hw_params.ring_mask->host2rxdma[i] ||
		    ab->hw_params.ring_mask->rx_mon_status[i]) {
			num_irq = 1;
		}

		irq_grp->num_irq = num_irq;
		irq_grp->irqs[0] = ATH11K_PCI_IRQ_DP_OFFSET + i;

		for (j = 0; j < irq_grp->num_irq; j++) {
			int irq_idx = irq_grp->irqs[j];
			int vector = (i % num_vectors) + base_vector;
			int irq = ath11k_pcic_get_msi_irq(ab, vector);

			if (irq < 0)
				return irq;

			ab->irq_num[irq_idx] = irq;

			ath11k_dbg(ab, ATH11K_DBG_PCI,
				   "irq %d group %d\n", irq, i);

			irq_set_status_flags(irq, IRQ_DISABLE_UNLAZY);
			ret = request_irq(irq, ath11k_pcic_ext_interrupt_handler,
					  irq_flags, "DP_EXT_IRQ", irq_grp);
			if (ret) {
				ath11k_err(ab, "failed request irq %d: %d\n",
					   vector, ret);
				return ret;
			}
		}
		ath11k_pcic_ext_grp_disable(irq_grp);
	}

	return 0;
}

int ath11k_pcic_config_irq(struct ath11k_base *ab)
{
	struct ath11k_ce_pipe *ce_pipe;
	u32 msi_data_start;
	u32 msi_data_count, msi_data_idx;
	u32 msi_irq_start;
	unsigned int msi_data;
	int irq, i, ret, irq_idx;
	unsigned long irq_flags;

	ret = ath11k_pcic_get_user_msi_assignment(ab, "CE", &msi_data_count,
						  &msi_data_start, &msi_irq_start);
	if (ret)
		return ret;

	irq_flags = IRQF_SHARED;
	if (!test_bit(ATH11K_FLAG_MULTI_MSI_VECTORS, &ab->dev_flags))
		irq_flags |= IRQF_NOBALANCING;

	/* Configure CE irqs */
	for (i = 0, msi_data_idx = 0; i < ab->hw_params.ce_count; i++) {
		if (ath11k_ce_get_attr_flags(ab, i) & CE_ATTR_DIS_INTR)
			continue;

		msi_data = (msi_data_idx % msi_data_count) + msi_irq_start;
		irq = ath11k_pcic_get_msi_irq(ab, msi_data);
		if (irq < 0)
			return irq;

		ce_pipe = &ab->ce.ce_pipe[i];

		irq_idx = ATH11K_PCI_IRQ_CE0_OFFSET + i;

		tasklet_setup(&ce_pipe->intr_tq, ath11k_pcic_ce_tasklet);

		ret = request_irq(irq, ath11k_pcic_ce_interrupt_handler,
				  irq_flags, irq_name[irq_idx], ce_pipe);
		if (ret) {
			ath11k_err(ab, "failed to request irq %d: %d\n",
				   irq_idx, ret);
			return ret;
		}

		ab->irq_num[irq_idx] = irq;
		msi_data_idx++;

		ath11k_pcic_ce_irq_disable(ab, i);
	}

	ret = ath11k_pcic_ext_irq_config(ab);
	if (ret)
		return ret;

	return 0;
}
EXPORT_SYMBOL(ath11k_pcic_config_irq);

void ath11k_pcic_ce_irqs_enable(struct ath11k_base *ab)
{
	int i;

	set_bit(ATH11K_FLAG_CE_IRQ_ENABLED, &ab->dev_flags);

	for (i = 0; i < ab->hw_params.ce_count; i++) {
		if (ath11k_ce_get_attr_flags(ab, i) & CE_ATTR_DIS_INTR)
			continue;
		ath11k_pcic_ce_irq_enable(ab, i);
	}
}
EXPORT_SYMBOL(ath11k_pcic_ce_irqs_enable);

static void ath11k_pcic_kill_tasklets(struct ath11k_base *ab)
{
	int i;

	for (i = 0; i < ab->hw_params.ce_count; i++) {
		struct ath11k_ce_pipe *ce_pipe = &ab->ce.ce_pipe[i];

		if (ath11k_ce_get_attr_flags(ab, i) & CE_ATTR_DIS_INTR)
			continue;

		tasklet_kill(&ce_pipe->intr_tq);
	}
}

void ath11k_pcic_ce_irq_disable_sync(struct ath11k_base *ab)
{
	ath11k_pcic_ce_irqs_disable(ab);
	ath11k_pcic_sync_ce_irqs(ab);
	ath11k_pcic_kill_tasklets(ab);
}
EXPORT_SYMBOL(ath11k_pcic_ce_irq_disable_sync);

void ath11k_pcic_stop(struct ath11k_base *ab)
{
	ath11k_pcic_ce_irq_disable_sync(ab);
	ath11k_ce_cleanup_pipes(ab);
}
EXPORT_SYMBOL(ath11k_pcic_stop);

int ath11k_pcic_start(struct ath11k_base *ab)
{
	set_bit(ATH11K_FLAG_DEVICE_INIT_DONE, &ab->dev_flags);

	ath11k_pcic_ce_irqs_enable(ab);
	ath11k_ce_rx_post_buf(ab);

	return 0;
}
EXPORT_SYMBOL(ath11k_pcic_start);

int ath11k_pcic_map_service_to_pipe(struct ath11k_base *ab, u16 service_id,
				    u8 *ul_pipe, u8 *dl_pipe)
{
	const struct service_to_pipe *entry;
	bool ul_set = false, dl_set = false;
	int i;

	for (i = 0; i < ab->hw_params.svc_to_ce_map_len; i++) {
		entry = &ab->hw_params.svc_to_ce_map[i];

		if (__le32_to_cpu(entry->service_id) != service_id)
			continue;

		switch (__le32_to_cpu(entry->pipedir)) {
		case PIPEDIR_NONE:
			break;
		case PIPEDIR_IN:
			WARN_ON(dl_set);
			*dl_pipe = __le32_to_cpu(entry->pipenum);
			dl_set = true;
			break;
		case PIPEDIR_OUT:
			WARN_ON(ul_set);
			*ul_pipe = __le32_to_cpu(entry->pipenum);
			ul_set = true;
			break;
		case PIPEDIR_INOUT:
			WARN_ON(dl_set);
			WARN_ON(ul_set);
			*dl_pipe = __le32_to_cpu(entry->pipenum);
			*ul_pipe = __le32_to_cpu(entry->pipenum);
			dl_set = true;
			ul_set = true;
			break;
		}
	}

	if (WARN_ON(!ul_set || !dl_set))
		return -ENOENT;

	return 0;
}
EXPORT_SYMBOL(ath11k_pcic_map_service_to_pipe);

int ath11k_pcic_register_pci_ops(struct ath11k_base *ab,
				 const struct ath11k_pci_ops *pci_ops)
{
	if (!pci_ops)
		return 0;

	/* Return error if mandatory pci_ops callbacks are missing */
	if (!pci_ops->get_msi_irq || !pci_ops->window_write32 ||
	    !pci_ops->window_read32)
		return -EINVAL;

	ab->pci.ops = pci_ops;
	return 0;
}
EXPORT_SYMBOL(ath11k_pcic_register_pci_ops);

void ath11k_pci_enable_ce_irqs_except_wake_irq(struct ath11k_base *ab)
{
	int i;

	for (i = 0; i < ab->hw_params.ce_count; i++) {
		if (ath11k_ce_get_attr_flags(ab, i) & CE_ATTR_DIS_INTR ||
		    i == ATH11K_PCI_CE_WAKE_IRQ)
			continue;
		ath11k_pcic_ce_irq_enable(ab, i);
	}
}
EXPORT_SYMBOL(ath11k_pci_enable_ce_irqs_except_wake_irq);

void ath11k_pci_disable_ce_irqs_except_wake_irq(struct ath11k_base *ab)
{
	int i;
	int irq_idx;
	struct ath11k_ce_pipe *ce_pipe;

	for (i = 0; i < ab->hw_params.ce_count; i++) {
		ce_pipe = &ab->ce.ce_pipe[i];
		irq_idx = ATH11K_PCI_IRQ_CE0_OFFSET + i;

		if (ath11k_ce_get_attr_flags(ab, i) & CE_ATTR_DIS_INTR ||
		    i == ATH11K_PCI_CE_WAKE_IRQ)
			continue;

		disable_irq_nosync(ab->irq_num[irq_idx]);
		synchronize_irq(ab->irq_num[irq_idx]);
		tasklet_kill(&ce_pipe->intr_tq);
	}
}
EXPORT_SYMBOL(ath11k_pci_disable_ce_irqs_except_wake_irq);
