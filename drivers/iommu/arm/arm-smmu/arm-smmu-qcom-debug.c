// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/cleanup.h>
#include <linux/device.h>
#include <linux/interconnect.h>
#include <linux/firmware/qcom/qcom_scm.h>
#include <linux/iopoll.h>
#include <linux/list.h>
#include <linux/mod_devicetable.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/ratelimit.h>
#include <linux/spinlock.h>

#include "arm-smmu.h"
#include "arm-smmu-qcom.h"

#define TBU_DBG_TIMEOUT_US		100
#define DEBUG_AXUSER_REG		0x30
#define DEBUG_AXUSER_CDMID		GENMASK_ULL(43, 36)
#define DEBUG_AXUSER_CDMID_VAL		0xff
#define DEBUG_PAR_REG			0x28
#define DEBUG_PAR_FAULT_VAL		BIT(0)
#define DEBUG_PAR_PA			GENMASK_ULL(47, 12)
#define DEBUG_SID_HALT_REG		0x0
#define DEBUG_SID_HALT_VAL		BIT(16)
#define DEBUG_SID_HALT_SID		GENMASK(9, 0)
#define DEBUG_SR_HALT_ACK_REG		0x20
#define DEBUG_SR_HALT_ACK_VAL		BIT(1)
#define DEBUG_SR_ECATS_RUNNING_VAL	BIT(0)
#define DEBUG_TXN_AXCACHE		GENMASK(5, 2)
#define DEBUG_TXN_AXPROT		GENMASK(8, 6)
#define DEBUG_TXN_AXPROT_PRIV		0x1
#define DEBUG_TXN_AXPROT_NSEC		0x2
#define DEBUG_TXN_TRIGG_REG		0x18
#define DEBUG_TXN_TRIGGER		BIT(0)
#define DEBUG_VA_ADDR_REG		0x8

static LIST_HEAD(tbu_list);
static DEFINE_MUTEX(tbu_list_lock);
static DEFINE_SPINLOCK(atos_lock);

struct qcom_tbu {
	struct device *dev;
	struct device_node *smmu_np;
	u32 sid_range[2];
	struct list_head list;
	struct clk *clk;
	struct icc_path	*path;
	void __iomem *base;
	spinlock_t halt_lock; /* multiple halt or resume can't execute concurrently */
	int halt_count;
};

static struct qcom_smmu *to_qcom_smmu(struct arm_smmu_device *smmu)
{
	return container_of(smmu, struct qcom_smmu, smmu);
}

void qcom_smmu_tlb_sync_debug(struct arm_smmu_device *smmu)
{
	int ret;
	u32 tbu_pwr_status, sync_inv_ack, sync_inv_progress;
	struct qcom_smmu *qsmmu = container_of(smmu, struct qcom_smmu, smmu);
	const struct qcom_smmu_config *cfg;
	static DEFINE_RATELIMIT_STATE(rs, DEFAULT_RATELIMIT_INTERVAL,
				      DEFAULT_RATELIMIT_BURST);

	if (__ratelimit(&rs)) {
		dev_err(smmu->dev, "TLB sync timed out -- SMMU may be deadlocked\n");

		cfg = qsmmu->cfg;
		if (!cfg)
			return;

		ret = qcom_scm_io_readl(smmu->ioaddr + cfg->reg_offset[QCOM_SMMU_TBU_PWR_STATUS],
					&tbu_pwr_status);
		if (ret)
			dev_err(smmu->dev,
				"Failed to read TBU power status: %d\n", ret);

		ret = qcom_scm_io_readl(smmu->ioaddr + cfg->reg_offset[QCOM_SMMU_STATS_SYNC_INV_TBU_ACK],
					&sync_inv_ack);
		if (ret)
			dev_err(smmu->dev,
				"Failed to read TBU sync/inv ack status: %d\n", ret);

		ret = qcom_scm_io_readl(smmu->ioaddr + cfg->reg_offset[QCOM_SMMU_MMU2QSS_AND_SAFE_WAIT_CNTR],
					&sync_inv_progress);
		if (ret)
			dev_err(smmu->dev,
				"Failed to read TCU syn/inv progress: %d\n", ret);

		dev_err(smmu->dev,
			"TBU: power_status %#x sync_inv_ack %#x sync_inv_progress %#x\n",
			tbu_pwr_status, sync_inv_ack, sync_inv_progress);
	}
}

static struct qcom_tbu *qcom_find_tbu(struct qcom_smmu *qsmmu, u32 sid)
{
	struct qcom_tbu *tbu;
	u32 start, end;

	guard(mutex)(&tbu_list_lock);

	if (list_empty(&tbu_list))
		return NULL;

	list_for_each_entry(tbu, &tbu_list, list) {
		start = tbu->sid_range[0];
		end = start + tbu->sid_range[1];

		if (qsmmu->smmu.dev->of_node == tbu->smmu_np &&
		    start <= sid && sid < end)
			return tbu;
	}
	dev_err(qsmmu->smmu.dev, "Unable to find TBU for sid 0x%x\n", sid);

	return NULL;
}

static int qcom_tbu_halt(struct qcom_tbu *tbu, struct arm_smmu_domain *smmu_domain)
{
	struct arm_smmu_device *smmu = smmu_domain->smmu;
	int ret = 0, idx = smmu_domain->cfg.cbndx;
	u32 val, fsr, status;

	guard(spinlock_irqsave)(&tbu->halt_lock);
	if (tbu->halt_count) {
		tbu->halt_count++;
		return ret;
	}

	val = readl_relaxed(tbu->base + DEBUG_SID_HALT_REG);
	val |= DEBUG_SID_HALT_VAL;
	writel_relaxed(val, tbu->base + DEBUG_SID_HALT_REG);

	fsr = arm_smmu_cb_read(smmu, idx, ARM_SMMU_CB_FSR);
	if ((fsr & ARM_SMMU_CB_FSR_FAULT) && (fsr & ARM_SMMU_CB_FSR_SS)) {
		u32 sctlr_orig, sctlr;

		/*
		 * We are in a fault. Our request to halt the bus will not
		 * complete until transactions in front of us (such as the fault
		 * itself) have completed. Disable iommu faults and terminate
		 * any existing transactions.
		 */
		sctlr_orig = arm_smmu_cb_read(smmu, idx, ARM_SMMU_CB_SCTLR);
		sctlr = sctlr_orig & ~(ARM_SMMU_SCTLR_CFCFG | ARM_SMMU_SCTLR_CFIE);
		arm_smmu_cb_write(smmu, idx, ARM_SMMU_CB_SCTLR, sctlr);
		arm_smmu_cb_write(smmu, idx, ARM_SMMU_CB_FSR, fsr);
		arm_smmu_cb_write(smmu, idx, ARM_SMMU_CB_RESUME, ARM_SMMU_RESUME_TERMINATE);
		arm_smmu_cb_write(smmu, idx, ARM_SMMU_CB_SCTLR, sctlr_orig);
	}

	if (readl_poll_timeout_atomic(tbu->base + DEBUG_SR_HALT_ACK_REG, status,
				      (status & DEBUG_SR_HALT_ACK_VAL),
				      0, TBU_DBG_TIMEOUT_US)) {
		dev_err(tbu->dev, "Timeout while trying to halt TBU!\n");
		ret = -ETIMEDOUT;

		val = readl_relaxed(tbu->base + DEBUG_SID_HALT_REG);
		val &= ~DEBUG_SID_HALT_VAL;
		writel_relaxed(val, tbu->base + DEBUG_SID_HALT_REG);

		return ret;
	}

	tbu->halt_count = 1;

	return ret;
}

static void qcom_tbu_resume(struct qcom_tbu *tbu)
{
	u32 val;

	guard(spinlock_irqsave)(&tbu->halt_lock);
	if (!tbu->halt_count) {
		WARN(1, "%s: halt_count is 0", dev_name(tbu->dev));
		return;
	}

	if (tbu->halt_count > 1) {
		tbu->halt_count--;
		return;
	}

	val = readl_relaxed(tbu->base + DEBUG_SID_HALT_REG);
	val &= ~DEBUG_SID_HALT_VAL;
	writel_relaxed(val, tbu->base + DEBUG_SID_HALT_REG);

	tbu->halt_count = 0;
}

static phys_addr_t qcom_tbu_trigger_atos(struct arm_smmu_domain *smmu_domain,
					 struct qcom_tbu *tbu, dma_addr_t iova, u32 sid)
{
	bool atos_timedout = false;
	phys_addr_t phys = 0;
	ktime_t timeout;
	u64 val;

	/* Set address and stream-id */
	val = readq_relaxed(tbu->base + DEBUG_SID_HALT_REG);
	val &= ~DEBUG_SID_HALT_SID;
	val |= FIELD_PREP(DEBUG_SID_HALT_SID, sid);
	writeq_relaxed(val, tbu->base + DEBUG_SID_HALT_REG);
	writeq_relaxed(iova, tbu->base + DEBUG_VA_ADDR_REG);
	val = FIELD_PREP(DEBUG_AXUSER_CDMID, DEBUG_AXUSER_CDMID_VAL);
	writeq_relaxed(val, tbu->base + DEBUG_AXUSER_REG);

	/* Write-back read and write-allocate */
	val = FIELD_PREP(DEBUG_TXN_AXCACHE, 0xf);

	/* Non-secure access */
	val |= FIELD_PREP(DEBUG_TXN_AXPROT, DEBUG_TXN_AXPROT_NSEC);

	/* Privileged access */
	val |= FIELD_PREP(DEBUG_TXN_AXPROT, DEBUG_TXN_AXPROT_PRIV);

	val |= DEBUG_TXN_TRIGGER;
	writeq_relaxed(val, tbu->base + DEBUG_TXN_TRIGG_REG);

	timeout = ktime_add_us(ktime_get(), TBU_DBG_TIMEOUT_US);
	for (;;) {
		val = readl_relaxed(tbu->base + DEBUG_SR_HALT_ACK_REG);
		if (!(val & DEBUG_SR_ECATS_RUNNING_VAL))
			break;
		val = readl_relaxed(tbu->base + DEBUG_PAR_REG);
		if (val & DEBUG_PAR_FAULT_VAL)
			break;
		if (ktime_compare(ktime_get(), timeout) > 0) {
			atos_timedout = true;
			break;
		}
	}

	val = readq_relaxed(tbu->base + DEBUG_PAR_REG);
	if (val & DEBUG_PAR_FAULT_VAL)
		dev_err(tbu->dev, "ATOS generated a fault interrupt! PAR = %llx, SID=0x%x\n",
			val, sid);
	else if (atos_timedout)
		dev_err_ratelimited(tbu->dev, "ATOS translation timed out!\n");
	else
		phys = FIELD_GET(DEBUG_PAR_PA, val);

	/* Reset hardware */
	writeq_relaxed(0, tbu->base + DEBUG_TXN_TRIGG_REG);
	writeq_relaxed(0, tbu->base + DEBUG_VA_ADDR_REG);
	val = readl_relaxed(tbu->base + DEBUG_SID_HALT_REG);
	val &= ~DEBUG_SID_HALT_SID;
	writel_relaxed(val, tbu->base + DEBUG_SID_HALT_REG);

	return phys;
}

static phys_addr_t qcom_iova_to_phys(struct arm_smmu_domain *smmu_domain,
				     dma_addr_t iova, u32 sid)
{
	struct arm_smmu_device *smmu = smmu_domain->smmu;
	struct qcom_smmu *qsmmu = to_qcom_smmu(smmu);
	int idx = smmu_domain->cfg.cbndx;
	struct qcom_tbu *tbu;
	u32 sctlr_orig, sctlr;
	phys_addr_t phys = 0;
	int attempt = 0;
	int ret;
	u64 fsr;

	tbu = qcom_find_tbu(qsmmu, sid);
	if (!tbu)
		return 0;

	ret = icc_set_bw(tbu->path, 0, UINT_MAX);
	if (ret)
		return ret;

	ret = clk_prepare_enable(tbu->clk);
	if (ret)
		goto disable_icc;

	ret = qcom_tbu_halt(tbu, smmu_domain);
	if (ret)
		goto disable_clk;

	/*
	 * ATOS/ECATS can trigger the fault interrupt, so disable it temporarily
	 * and check for an interrupt manually.
	 */
	sctlr_orig = arm_smmu_cb_read(smmu, idx, ARM_SMMU_CB_SCTLR);
	sctlr = sctlr_orig & ~(ARM_SMMU_SCTLR_CFCFG | ARM_SMMU_SCTLR_CFIE);
	arm_smmu_cb_write(smmu, idx, ARM_SMMU_CB_SCTLR, sctlr);

	fsr = arm_smmu_cb_read(smmu, idx, ARM_SMMU_CB_FSR);
	if (fsr & ARM_SMMU_CB_FSR_FAULT) {
		/* Clear pending interrupts */
		arm_smmu_cb_write(smmu, idx, ARM_SMMU_CB_FSR, fsr);

		/*
		 * TBU halt takes care of resuming any stalled transcation.
		 * Kept it here for completeness sake.
		 */
		if (fsr & ARM_SMMU_CB_FSR_SS)
			arm_smmu_cb_write(smmu, idx, ARM_SMMU_CB_RESUME,
					  ARM_SMMU_RESUME_TERMINATE);
	}

	/* Only one concurrent atos operation */
	scoped_guard(spinlock_irqsave, &atos_lock) {
		/*
		 * If the translation fails, attempt the lookup more time."
		 */
		do {
			phys = qcom_tbu_trigger_atos(smmu_domain, tbu, iova, sid);

			fsr = arm_smmu_cb_read(smmu, idx, ARM_SMMU_CB_FSR);
			if (fsr & ARM_SMMU_CB_FSR_FAULT) {
				/* Clear pending interrupts */
				arm_smmu_cb_write(smmu, idx, ARM_SMMU_CB_FSR, fsr);

				if (fsr & ARM_SMMU_CB_FSR_SS)
					arm_smmu_cb_write(smmu, idx, ARM_SMMU_CB_RESUME,
							  ARM_SMMU_RESUME_TERMINATE);
			}
		} while (!phys && attempt++ < 2);

		arm_smmu_cb_write(smmu, idx, ARM_SMMU_CB_SCTLR, sctlr_orig);
	}
	qcom_tbu_resume(tbu);

	/* Read to complete prior write transcations */
	readl_relaxed(tbu->base + DEBUG_SR_HALT_ACK_REG);

disable_clk:
	clk_disable_unprepare(tbu->clk);
disable_icc:
	icc_set_bw(tbu->path, 0, 0);

	return phys;
}

static phys_addr_t qcom_smmu_iova_to_phys_hard(struct arm_smmu_domain *smmu_domain, dma_addr_t iova)
{
	struct arm_smmu_device *smmu = smmu_domain->smmu;
	int idx = smmu_domain->cfg.cbndx;
	u32 frsynra;
	u16 sid;

	frsynra = arm_smmu_gr1_read(smmu, ARM_SMMU_GR1_CBFRSYNRA(idx));
	sid = FIELD_GET(ARM_SMMU_CBFRSYNRA_SID, frsynra);

	return qcom_iova_to_phys(smmu_domain, iova, sid);
}

static phys_addr_t qcom_smmu_verify_fault(struct arm_smmu_domain *smmu_domain, dma_addr_t iova, u32 fsr)
{
	struct io_pgtable *iop = io_pgtable_ops_to_pgtable(smmu_domain->pgtbl_ops);
	struct arm_smmu_device *smmu = smmu_domain->smmu;
	phys_addr_t phys_post_tlbiall;
	phys_addr_t phys;

	phys = qcom_smmu_iova_to_phys_hard(smmu_domain, iova);
	io_pgtable_tlb_flush_all(iop);
	phys_post_tlbiall = qcom_smmu_iova_to_phys_hard(smmu_domain, iova);

	if (phys != phys_post_tlbiall) {
		dev_err(smmu->dev,
			"ATOS results differed across TLBIALL... (before: %pa after: %pa)\n",
			&phys, &phys_post_tlbiall);
	}

	return (phys == 0 ? phys_post_tlbiall : phys);
}

irqreturn_t qcom_smmu_context_fault(int irq, void *dev)
{
	struct arm_smmu_domain *smmu_domain = dev;
	struct io_pgtable_ops *ops = smmu_domain->pgtbl_ops;
	struct arm_smmu_device *smmu = smmu_domain->smmu;
	struct arm_smmu_context_fault_info cfi;
	u32 resume = 0;
	int idx = smmu_domain->cfg.cbndx;
	phys_addr_t phys_soft;
	int ret, tmp;

	static DEFINE_RATELIMIT_STATE(_rs,
				      DEFAULT_RATELIMIT_INTERVAL,
				      DEFAULT_RATELIMIT_BURST);

	arm_smmu_read_context_fault_info(smmu, idx, &cfi);

	if (!(cfi.fsr & ARM_SMMU_CB_FSR_FAULT))
		return IRQ_NONE;

	if (list_empty(&tbu_list)) {
		ret = report_iommu_fault(&smmu_domain->domain, NULL, cfi.iova,
					 cfi.fsynr & ARM_SMMU_CB_FSYNR0_WNR ? IOMMU_FAULT_WRITE : IOMMU_FAULT_READ);

		if (ret == -ENOSYS)
			arm_smmu_print_context_fault_info(smmu, idx, &cfi);

		arm_smmu_cb_write(smmu, idx, ARM_SMMU_CB_FSR, cfi.fsr);
		return IRQ_HANDLED;
	}

	phys_soft = ops->iova_to_phys(ops, cfi.iova);

	tmp = report_iommu_fault(&smmu_domain->domain, NULL, cfi.iova,
				 cfi.fsynr & ARM_SMMU_CB_FSYNR0_WNR ? IOMMU_FAULT_WRITE : IOMMU_FAULT_READ);
	if (!tmp || tmp == -EBUSY) {
		ret = IRQ_HANDLED;
		resume = ARM_SMMU_RESUME_TERMINATE;
	} else {
		phys_addr_t phys_atos = qcom_smmu_verify_fault(smmu_domain, cfi.iova, cfi.fsr);

		if (__ratelimit(&_rs)) {
			arm_smmu_print_context_fault_info(smmu, idx, &cfi);

			dev_err(smmu->dev,
				"soft iova-to-phys=%pa\n", &phys_soft);
			if (!phys_soft)
				dev_err(smmu->dev,
					"SOFTWARE TABLE WALK FAILED! Looks like %s accessed an unmapped address!\n",
					dev_name(smmu->dev));
			if (phys_atos)
				dev_err(smmu->dev, "hard iova-to-phys (ATOS)=%pa\n",
					&phys_atos);
			else
				dev_err(smmu->dev, "hard iova-to-phys (ATOS) failed\n");
		}
		ret = IRQ_NONE;
		resume = ARM_SMMU_RESUME_TERMINATE;
	}

	/*
	 * If the client returns -EBUSY, do not clear FSR and do not RESUME
	 * if stalled. This is required to keep the IOMMU client stalled on
	 * the outstanding fault. This gives the client a chance to take any
	 * debug action and then terminate the stalled transaction.
	 * So, the sequence in case of stall on fault should be:
	 * 1) Do not clear FSR or write to RESUME here
	 * 2) Client takes any debug action
	 * 3) Client terminates the stalled transaction and resumes the IOMMU
	 * 4) Client clears FSR. The FSR should only be cleared after 3) and
	 *    not before so that the fault remains outstanding. This ensures
	 *    SCTLR.HUPCF has the desired effect if subsequent transactions also
	 *    need to be terminated.
	 */
	if (tmp != -EBUSY) {
		/* Clear the faulting FSR */
		arm_smmu_cb_write(smmu, idx, ARM_SMMU_CB_FSR, cfi.fsr);

		/* Retry or terminate any stalled transactions */
		if (cfi.fsr & ARM_SMMU_CB_FSR_SS)
			arm_smmu_cb_write(smmu, idx, ARM_SMMU_CB_RESUME, resume);
	}

	return ret;
}

int qcom_tbu_probe(struct platform_device *pdev)
{
	struct of_phandle_args args = { .args_count = 2 };
	struct device_node *np = pdev->dev.of_node;
	struct device *dev = &pdev->dev;
	struct qcom_tbu *tbu;

	tbu = devm_kzalloc(dev, sizeof(*tbu), GFP_KERNEL);
	if (!tbu)
		return -ENOMEM;

	tbu->dev = dev;
	INIT_LIST_HEAD(&tbu->list);
	spin_lock_init(&tbu->halt_lock);

	if (of_parse_phandle_with_args(np, "qcom,stream-id-range", "#iommu-cells", 0, &args)) {
		dev_err(dev, "Cannot parse the 'qcom,stream-id-range' DT property\n");
		return -EINVAL;
	}

	tbu->smmu_np =  args.np;
	tbu->sid_range[0] = args.args[0];
	tbu->sid_range[1] = args.args[1];
	of_node_put(args.np);

	tbu->base = devm_of_iomap(dev, np, 0, NULL);
	if (IS_ERR(tbu->base))
		return PTR_ERR(tbu->base);

	tbu->clk = devm_clk_get_optional(dev, NULL);
	if (IS_ERR(tbu->clk))
		return PTR_ERR(tbu->clk);

	tbu->path = devm_of_icc_get(dev, NULL);
	if (IS_ERR(tbu->path))
		return PTR_ERR(tbu->path);

	guard(mutex)(&tbu_list_lock);
	list_add_tail(&tbu->list, &tbu_list);

	return 0;
}
