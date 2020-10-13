// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2019-2020 ARM Limited or its affiliates. */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/clk.h>
#include <linux/hw_random.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/interrupt.h>
#include <linux/irqreturn.h>
#include <linux/workqueue.h>
#include <linux/circ_buf.h>
#include <linux/completion.h>
#include <linux/of.h>
#include <linux/bitfield.h>
#include <linux/fips.h>

#include "cctrng.h"

#define CC_REG_LOW(name)  (name ## _BIT_SHIFT)
#define CC_REG_HIGH(name) (CC_REG_LOW(name) + name ## _BIT_SIZE - 1)
#define CC_GENMASK(name)  GENMASK(CC_REG_HIGH(name), CC_REG_LOW(name))

#define CC_REG_FLD_GET(reg_name, fld_name, reg_val)     \
	(FIELD_GET(CC_GENMASK(CC_ ## reg_name ## _ ## fld_name), reg_val))

#define CC_HW_RESET_LOOP_COUNT 10
#define CC_TRNG_SUSPEND_TIMEOUT 3000

/* data circular buffer in words must be:
 *  - of a power-of-2 size (limitation of circ_buf.h macros)
 *  - at least 6, the size generated in the EHR according to HW implementation
 */
#define CCTRNG_DATA_BUF_WORDS 32

/* The timeout for the TRNG operation should be calculated with the formula:
 * Timeout = EHR_NUM * VN_COEFF * EHR_LENGTH * SAMPLE_CNT * SCALE_VALUE
 * while:
 *  - SAMPLE_CNT is input value from the characterisation process
 *  - all the rest are constants
 */
#define EHR_NUM 1
#define VN_COEFF 4
#define EHR_LENGTH CC_TRNG_EHR_IN_BITS
#define SCALE_VALUE 2
#define CCTRNG_TIMEOUT(smpl_cnt) \
	(EHR_NUM * VN_COEFF * EHR_LENGTH * smpl_cnt * SCALE_VALUE)

struct cctrng_drvdata {
	struct platform_device *pdev;
	void __iomem *cc_base;
	struct clk *clk;
	struct hwrng rng;
	u32 active_rosc;
	/* Sampling interval for each ring oscillator:
	 * count of ring oscillator cycles between consecutive bits sampling.
	 * Value of 0 indicates non-valid rosc
	 */
	u32 smpl_ratio[CC_TRNG_NUM_OF_ROSCS];

	u32 data_buf[CCTRNG_DATA_BUF_WORDS];
	struct circ_buf circ;
	struct work_struct compwork;
	struct work_struct startwork;

	/* pending_hw - 1 when HW is pending, 0 when it is idle */
	atomic_t pending_hw;

	/* protects against multiple concurrent consumers of data_buf */
	spinlock_t read_lock;
};


/* functions for write/read CC registers */
static inline void cc_iowrite(struct cctrng_drvdata *drvdata, u32 reg, u32 val)
{
	iowrite32(val, (drvdata->cc_base + reg));
}
static inline u32 cc_ioread(struct cctrng_drvdata *drvdata, u32 reg)
{
	return ioread32(drvdata->cc_base + reg);
}


static int cc_trng_pm_get(struct device *dev)
{
	int rc = 0;

	rc = pm_runtime_get_sync(dev);

	/* pm_runtime_get_sync() can return 1 as a valid return code */
	return (rc == 1 ? 0 : rc);
}

static void cc_trng_pm_put_suspend(struct device *dev)
{
	int rc = 0;

	pm_runtime_mark_last_busy(dev);
	rc = pm_runtime_put_autosuspend(dev);
	if (rc)
		dev_err(dev, "pm_runtime_put_autosuspend returned %x\n", rc);
}

static int cc_trng_pm_init(struct cctrng_drvdata *drvdata)
{
	struct device *dev = &(drvdata->pdev->dev);

	/* must be before the enabling to avoid redundant suspending */
	pm_runtime_set_autosuspend_delay(dev, CC_TRNG_SUSPEND_TIMEOUT);
	pm_runtime_use_autosuspend(dev);
	/* set us as active - note we won't do PM ops until cc_trng_pm_go()! */
	return pm_runtime_set_active(dev);
}

static void cc_trng_pm_go(struct cctrng_drvdata *drvdata)
{
	struct device *dev = &(drvdata->pdev->dev);

	/* enable the PM module*/
	pm_runtime_enable(dev);
}

static void cc_trng_pm_fini(struct cctrng_drvdata *drvdata)
{
	struct device *dev = &(drvdata->pdev->dev);

	pm_runtime_disable(dev);
}


static inline int cc_trng_parse_sampling_ratio(struct cctrng_drvdata *drvdata)
{
	struct device *dev = &(drvdata->pdev->dev);
	struct device_node *np = drvdata->pdev->dev.of_node;
	int rc;
	int i;
	/* ret will be set to 0 if at least one rosc has (sampling ratio > 0) */
	int ret = -EINVAL;

	rc = of_property_read_u32_array(np, "arm,rosc-ratio",
					drvdata->smpl_ratio,
					CC_TRNG_NUM_OF_ROSCS);
	if (rc) {
		/* arm,rosc-ratio was not found in device tree */
		return rc;
	}

	/* verify that at least one rosc has (sampling ratio > 0) */
	for (i = 0; i < CC_TRNG_NUM_OF_ROSCS; ++i) {
		dev_dbg(dev, "rosc %d sampling ratio %u",
			i, drvdata->smpl_ratio[i]);

		if (drvdata->smpl_ratio[i] > 0)
			ret = 0;
	}

	return ret;
}

static int cc_trng_change_rosc(struct cctrng_drvdata *drvdata)
{
	struct device *dev = &(drvdata->pdev->dev);

	dev_dbg(dev, "cctrng change rosc (was %d)\n", drvdata->active_rosc);
	drvdata->active_rosc += 1;

	while (drvdata->active_rosc < CC_TRNG_NUM_OF_ROSCS) {
		if (drvdata->smpl_ratio[drvdata->active_rosc] > 0)
			return 0;

		drvdata->active_rosc += 1;
	}
	return -EINVAL;
}


static void cc_trng_enable_rnd_source(struct cctrng_drvdata *drvdata)
{
	u32 max_cycles;

	/* Set watchdog threshold to maximal allowed time (in CPU cycles) */
	max_cycles = CCTRNG_TIMEOUT(drvdata->smpl_ratio[drvdata->active_rosc]);
	cc_iowrite(drvdata, CC_RNG_WATCHDOG_VAL_REG_OFFSET, max_cycles);

	/* enable the RND source */
	cc_iowrite(drvdata, CC_RND_SOURCE_ENABLE_REG_OFFSET, 0x1);

	/* unmask RNG interrupts */
	cc_iowrite(drvdata, CC_RNG_IMR_REG_OFFSET, (u32)~CC_RNG_INT_MASK);
}


/* increase circular data buffer index (head/tail) */
static inline void circ_idx_inc(int *idx, int bytes)
{
	*idx += (bytes + 3) >> 2;
	*idx &= (CCTRNG_DATA_BUF_WORDS - 1);
}

static inline size_t circ_buf_space(struct cctrng_drvdata *drvdata)
{
	return CIRC_SPACE(drvdata->circ.head,
			  drvdata->circ.tail, CCTRNG_DATA_BUF_WORDS);

}

static int cctrng_read(struct hwrng *rng, void *data, size_t max, bool wait)
{
	/* current implementation ignores "wait" */

	struct cctrng_drvdata *drvdata = (struct cctrng_drvdata *)rng->priv;
	struct device *dev = &(drvdata->pdev->dev);
	u32 *buf = (u32 *)drvdata->circ.buf;
	size_t copied = 0;
	size_t cnt_w;
	size_t size;
	size_t left;

	if (!spin_trylock(&drvdata->read_lock)) {
		/* concurrent consumers from data_buf cannot be served */
		dev_dbg_ratelimited(dev, "unable to hold lock\n");
		return 0;
	}

	/* copy till end of data buffer (without wrap back) */
	cnt_w = CIRC_CNT_TO_END(drvdata->circ.head,
				drvdata->circ.tail, CCTRNG_DATA_BUF_WORDS);
	size = min((cnt_w<<2), max);
	memcpy(data, &(buf[drvdata->circ.tail]), size);
	copied = size;
	circ_idx_inc(&drvdata->circ.tail, size);
	/* copy rest of data in data buffer */
	left = max - copied;
	if (left > 0) {
		cnt_w = CIRC_CNT(drvdata->circ.head,
				 drvdata->circ.tail, CCTRNG_DATA_BUF_WORDS);
		size = min((cnt_w<<2), left);
		memcpy(data, &(buf[drvdata->circ.tail]), size);
		copied += size;
		circ_idx_inc(&drvdata->circ.tail, size);
	}

	spin_unlock(&drvdata->read_lock);

	if (circ_buf_space(drvdata) >= CC_TRNG_EHR_IN_WORDS) {
		if (atomic_cmpxchg(&drvdata->pending_hw, 0, 1) == 0) {
			/* re-check space in buffer to avoid potential race */
			if (circ_buf_space(drvdata) >= CC_TRNG_EHR_IN_WORDS) {
				/* increment device's usage counter */
				int rc = cc_trng_pm_get(dev);

				if (rc) {
					dev_err(dev,
						"cc_trng_pm_get returned %x\n",
						rc);
					return rc;
				}

				/* schedule execution of deferred work handler
				 * for filling of data buffer
				 */
				schedule_work(&drvdata->startwork);
			} else {
				atomic_set(&drvdata->pending_hw, 0);
			}
		}
	}

	return copied;
}

static void cc_trng_hw_trigger(struct cctrng_drvdata *drvdata)
{
	u32 tmp_smpl_cnt = 0;
	struct device *dev = &(drvdata->pdev->dev);

	dev_dbg(dev, "cctrng hw trigger.\n");

	/* enable the HW RND clock */
	cc_iowrite(drvdata, CC_RNG_CLK_ENABLE_REG_OFFSET, 0x1);

	/* do software reset */
	cc_iowrite(drvdata, CC_RNG_SW_RESET_REG_OFFSET, 0x1);
	/* in order to verify that the reset has completed,
	 * the sample count need to be verified
	 */
	do {
		/* enable the HW RND clock   */
		cc_iowrite(drvdata, CC_RNG_CLK_ENABLE_REG_OFFSET, 0x1);

		/* set sampling ratio (rng_clocks) between consecutive bits */
		cc_iowrite(drvdata, CC_SAMPLE_CNT1_REG_OFFSET,
			   drvdata->smpl_ratio[drvdata->active_rosc]);

		/* read the sampling ratio  */
		tmp_smpl_cnt = cc_ioread(drvdata, CC_SAMPLE_CNT1_REG_OFFSET);

	} while (tmp_smpl_cnt != drvdata->smpl_ratio[drvdata->active_rosc]);

	/* disable the RND source for setting new parameters in HW */
	cc_iowrite(drvdata, CC_RND_SOURCE_ENABLE_REG_OFFSET, 0);

	cc_iowrite(drvdata, CC_RNG_ICR_REG_OFFSET, 0xFFFFFFFF);

	cc_iowrite(drvdata, CC_TRNG_CONFIG_REG_OFFSET, drvdata->active_rosc);

	/* Debug Control register: set to 0 - no bypasses */
	cc_iowrite(drvdata, CC_TRNG_DEBUG_CONTROL_REG_OFFSET, 0);

	cc_trng_enable_rnd_source(drvdata);
}

static void cc_trng_compwork_handler(struct work_struct *w)
{
	u32 isr = 0;
	u32 ehr_valid = 0;
	struct cctrng_drvdata *drvdata =
			container_of(w, struct cctrng_drvdata, compwork);
	struct device *dev = &(drvdata->pdev->dev);
	int i;

	/* stop DMA and the RNG source */
	cc_iowrite(drvdata, CC_RNG_DMA_ENABLE_REG_OFFSET, 0);
	cc_iowrite(drvdata, CC_RND_SOURCE_ENABLE_REG_OFFSET, 0);

	/* read RNG_ISR and check for errors */
	isr = cc_ioread(drvdata, CC_RNG_ISR_REG_OFFSET);
	ehr_valid = CC_REG_FLD_GET(RNG_ISR, EHR_VALID, isr);
	dev_dbg(dev, "Got RNG_ISR=0x%08X (EHR_VALID=%u)\n", isr, ehr_valid);

	if (fips_enabled && CC_REG_FLD_GET(RNG_ISR, CRNGT_ERR, isr)) {
		fips_fail_notify();
		/* FIPS error is fatal */
		panic("Got HW CRNGT error while fips is enabled!\n");
	}

	/* Clear all pending RNG interrupts */
	cc_iowrite(drvdata, CC_RNG_ICR_REG_OFFSET, isr);


	if (!ehr_valid) {
		/* in case of AUTOCORR/TIMEOUT error, try the next ROSC */
		if (CC_REG_FLD_GET(RNG_ISR, AUTOCORR_ERR, isr) ||
				CC_REG_FLD_GET(RNG_ISR, WATCHDOG, isr)) {
			dev_dbg(dev, "cctrng autocorr/timeout error.\n");
			goto next_rosc;
		}

		/* in case of VN error, ignore it */
	}

	/* read EHR data from registers */
	for (i = 0; i < CC_TRNG_EHR_IN_WORDS; i++) {
		/* calc word ptr in data_buf */
		u32 *buf = (u32 *)drvdata->circ.buf;

		buf[drvdata->circ.head] = cc_ioread(drvdata,
				CC_EHR_DATA_0_REG_OFFSET + (i*sizeof(u32)));

		/* EHR_DATA registers are cleared on read. In case 0 value was
		 * returned, restart the entropy collection.
		 */
		if (buf[drvdata->circ.head] == 0) {
			dev_dbg(dev, "Got 0 value in EHR. active_rosc %u\n",
				drvdata->active_rosc);
			goto next_rosc;
		}

		circ_idx_inc(&drvdata->circ.head, 1<<2);
	}

	atomic_set(&drvdata->pending_hw, 0);

	/* continue to fill data buffer if needed */
	if (circ_buf_space(drvdata) >= CC_TRNG_EHR_IN_WORDS) {
		if (atomic_cmpxchg(&drvdata->pending_hw, 0, 1) == 0) {
			/* Re-enable rnd source */
			cc_trng_enable_rnd_source(drvdata);
			return;
		}
	}

	cc_trng_pm_put_suspend(dev);

	dev_dbg(dev, "compwork handler done\n");
	return;

next_rosc:
	if ((circ_buf_space(drvdata) >= CC_TRNG_EHR_IN_WORDS) &&
			(cc_trng_change_rosc(drvdata) == 0)) {
		/* trigger trng hw with next rosc */
		cc_trng_hw_trigger(drvdata);
	} else {
		atomic_set(&drvdata->pending_hw, 0);
		cc_trng_pm_put_suspend(dev);
	}
}

static irqreturn_t cc_isr(int irq, void *dev_id)
{
	struct cctrng_drvdata *drvdata = (struct cctrng_drvdata *)dev_id;
	struct device *dev = &(drvdata->pdev->dev);
	u32 irr;

	/* if driver suspended return, probably shared interrupt */
	if (pm_runtime_suspended(dev))
		return IRQ_NONE;

	/* read the interrupt status */
	irr = cc_ioread(drvdata, CC_HOST_RGF_IRR_REG_OFFSET);
	dev_dbg(dev, "Got IRR=0x%08X\n", irr);

	if (irr == 0) /* Probably shared interrupt line */
		return IRQ_NONE;

	/* clear interrupt - must be before processing events */
	cc_iowrite(drvdata, CC_HOST_RGF_ICR_REG_OFFSET, irr);

	/* RNG interrupt - most probable */
	if (irr & CC_HOST_RNG_IRQ_MASK) {
		/* Mask RNG interrupts - will be unmasked in deferred work */
		cc_iowrite(drvdata, CC_RNG_IMR_REG_OFFSET, 0xFFFFFFFF);

		/* We clear RNG interrupt here,
		 * to avoid it from firing as we'll unmask RNG interrupts.
		 */
		cc_iowrite(drvdata, CC_HOST_RGF_ICR_REG_OFFSET,
			   CC_HOST_RNG_IRQ_MASK);

		irr &= ~CC_HOST_RNG_IRQ_MASK;

		/* schedule execution of deferred work handler */
		schedule_work(&drvdata->compwork);
	}

	if (irr) {
		dev_dbg_ratelimited(dev,
				"IRR includes unknown cause bits (0x%08X)\n",
				irr);
		/* Just warning */
	}

	return IRQ_HANDLED;
}

static void cc_trng_startwork_handler(struct work_struct *w)
{
	struct cctrng_drvdata *drvdata =
			container_of(w, struct cctrng_drvdata, startwork);

	drvdata->active_rosc = 0;
	cc_trng_hw_trigger(drvdata);
}


static int cc_trng_clk_init(struct cctrng_drvdata *drvdata)
{
	struct clk *clk;
	struct device *dev = &(drvdata->pdev->dev);
	int rc = 0;

	clk = devm_clk_get_optional(dev, NULL);
	if (IS_ERR(clk))
		return dev_err_probe(dev, PTR_ERR(clk),
				     "Error getting clock\n");

	drvdata->clk = clk;

	rc = clk_prepare_enable(drvdata->clk);
	if (rc) {
		dev_err(dev, "Failed to enable clock\n");
		return rc;
	}

	return 0;
}

static void cc_trng_clk_fini(struct cctrng_drvdata *drvdata)
{
	clk_disable_unprepare(drvdata->clk);
}


static int cctrng_probe(struct platform_device *pdev)
{
	struct resource *req_mem_cc_regs = NULL;
	struct cctrng_drvdata *drvdata;
	struct device *dev = &pdev->dev;
	int rc = 0;
	u32 val;
	int irq;

	drvdata = devm_kzalloc(dev, sizeof(*drvdata), GFP_KERNEL);
	if (!drvdata)
		return -ENOMEM;

	drvdata->rng.name = devm_kstrdup(dev, dev_name(dev), GFP_KERNEL);
	if (!drvdata->rng.name)
		return -ENOMEM;

	drvdata->rng.read = cctrng_read;
	drvdata->rng.priv = (unsigned long)drvdata;
	drvdata->rng.quality = CC_TRNG_QUALITY;

	platform_set_drvdata(pdev, drvdata);
	drvdata->pdev = pdev;

	drvdata->circ.buf = (char *)drvdata->data_buf;

	/* Get device resources */
	/* First CC registers space */
	req_mem_cc_regs = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	/* Map registers space */
	drvdata->cc_base = devm_ioremap_resource(dev, req_mem_cc_regs);
	if (IS_ERR(drvdata->cc_base)) {
		dev_err(dev, "Failed to ioremap registers");
		return PTR_ERR(drvdata->cc_base);
	}

	dev_dbg(dev, "Got MEM resource (%s): %pR\n", req_mem_cc_regs->name,
		req_mem_cc_regs);
	dev_dbg(dev, "CC registers mapped from %pa to 0x%p\n",
		&req_mem_cc_regs->start, drvdata->cc_base);

	/* Then IRQ */
	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(dev, "Failed getting IRQ resource\n");
		return irq;
	}

	/* parse sampling rate from device tree */
	rc = cc_trng_parse_sampling_ratio(drvdata);
	if (rc) {
		dev_err(dev, "Failed to get legal sampling ratio for rosc\n");
		return rc;
	}

	rc = cc_trng_clk_init(drvdata);
	if (rc) {
		dev_err(dev, "cc_trng_clk_init failed\n");
		return rc;
	}

	INIT_WORK(&drvdata->compwork, cc_trng_compwork_handler);
	INIT_WORK(&drvdata->startwork, cc_trng_startwork_handler);
	spin_lock_init(&drvdata->read_lock);

	/* register the driver isr function */
	rc = devm_request_irq(dev, irq, cc_isr, IRQF_SHARED, "cctrng", drvdata);
	if (rc) {
		dev_err(dev, "Could not register to interrupt %d\n", irq);
		goto post_clk_err;
	}
	dev_dbg(dev, "Registered to IRQ: %d\n", irq);

	/* Clear all pending interrupts */
	val = cc_ioread(drvdata, CC_HOST_RGF_IRR_REG_OFFSET);
	dev_dbg(dev, "IRR=0x%08X\n", val);
	cc_iowrite(drvdata, CC_HOST_RGF_ICR_REG_OFFSET, val);

	/* unmask HOST RNG interrupt */
	cc_iowrite(drvdata, CC_HOST_RGF_IMR_REG_OFFSET,
		   cc_ioread(drvdata, CC_HOST_RGF_IMR_REG_OFFSET) &
		   ~CC_HOST_RNG_IRQ_MASK);

	/* init PM */
	rc = cc_trng_pm_init(drvdata);
	if (rc) {
		dev_err(dev, "cc_trng_pm_init failed\n");
		goto post_clk_err;
	}

	/* increment device's usage counter */
	rc = cc_trng_pm_get(dev);
	if (rc) {
		dev_err(dev, "cc_trng_pm_get returned %x\n", rc);
		goto post_pm_err;
	}

	/* set pending_hw to verify that HW won't be triggered from read */
	atomic_set(&drvdata->pending_hw, 1);

	/* registration of the hwrng device */
	rc = hwrng_register(&drvdata->rng);
	if (rc) {
		dev_err(dev, "Could not register hwrng device.\n");
		goto post_pm_err;
	}

	/* trigger HW to start generate data */
	drvdata->active_rosc = 0;
	cc_trng_hw_trigger(drvdata);

	/* All set, we can allow auto-suspend */
	cc_trng_pm_go(drvdata);

	dev_info(dev, "ARM cctrng device initialized\n");

	return 0;

post_pm_err:
	cc_trng_pm_fini(drvdata);

post_clk_err:
	cc_trng_clk_fini(drvdata);

	return rc;
}

static int cctrng_remove(struct platform_device *pdev)
{
	struct cctrng_drvdata *drvdata = platform_get_drvdata(pdev);
	struct device *dev = &pdev->dev;

	dev_dbg(dev, "Releasing cctrng resources...\n");

	hwrng_unregister(&drvdata->rng);

	cc_trng_pm_fini(drvdata);

	cc_trng_clk_fini(drvdata);

	dev_info(dev, "ARM cctrng device terminated\n");

	return 0;
}

static int __maybe_unused cctrng_suspend(struct device *dev)
{
	struct cctrng_drvdata *drvdata = dev_get_drvdata(dev);

	dev_dbg(dev, "set HOST_POWER_DOWN_EN\n");
	cc_iowrite(drvdata, CC_HOST_POWER_DOWN_EN_REG_OFFSET,
			POWER_DOWN_ENABLE);

	clk_disable_unprepare(drvdata->clk);

	return 0;
}

static bool cctrng_wait_for_reset_completion(struct cctrng_drvdata *drvdata)
{
	unsigned int val;
	unsigned int i;

	for (i = 0; i < CC_HW_RESET_LOOP_COUNT; i++) {
		/* in cc7x3 NVM_IS_IDLE indicates that CC reset is
		 *  completed and device is fully functional
		 */
		val = cc_ioread(drvdata, CC_NVM_IS_IDLE_REG_OFFSET);
		if (val & BIT(CC_NVM_IS_IDLE_VALUE_BIT_SHIFT)) {
			/* hw indicate reset completed */
			return true;
		}
		/* allow scheduling other process on the processor */
		schedule();
	}
	/* reset not completed */
	return false;
}

static int __maybe_unused cctrng_resume(struct device *dev)
{
	struct cctrng_drvdata *drvdata = dev_get_drvdata(dev);
	int rc;

	dev_dbg(dev, "unset HOST_POWER_DOWN_EN\n");
	/* Enables the device source clk */
	rc = clk_prepare_enable(drvdata->clk);
	if (rc) {
		dev_err(dev, "failed getting clock back on. We're toast.\n");
		return rc;
	}

	/* wait for Cryptocell reset completion */
	if (!cctrng_wait_for_reset_completion(drvdata)) {
		dev_err(dev, "Cryptocell reset not completed");
		return -EBUSY;
	}

	/* unmask HOST RNG interrupt */
	cc_iowrite(drvdata, CC_HOST_RGF_IMR_REG_OFFSET,
		   cc_ioread(drvdata, CC_HOST_RGF_IMR_REG_OFFSET) &
		   ~CC_HOST_RNG_IRQ_MASK);

	cc_iowrite(drvdata, CC_HOST_POWER_DOWN_EN_REG_OFFSET,
		   POWER_DOWN_DISABLE);

	return 0;
}

static UNIVERSAL_DEV_PM_OPS(cctrng_pm, cctrng_suspend, cctrng_resume, NULL);

static const struct of_device_id arm_cctrng_dt_match[] = {
	{ .compatible = "arm,cryptocell-713-trng", },
	{ .compatible = "arm,cryptocell-703-trng", },
	{},
};
MODULE_DEVICE_TABLE(of, arm_cctrng_dt_match);

static struct platform_driver cctrng_driver = {
	.driver = {
		.name = "cctrng",
		.of_match_table = arm_cctrng_dt_match,
		.pm = &cctrng_pm,
	},
	.probe = cctrng_probe,
	.remove = cctrng_remove,
};

static int __init cctrng_mod_init(void)
{
	/* Compile time assertion checks */
	BUILD_BUG_ON(CCTRNG_DATA_BUF_WORDS < 6);
	BUILD_BUG_ON((CCTRNG_DATA_BUF_WORDS & (CCTRNG_DATA_BUF_WORDS-1)) != 0);

	return platform_driver_register(&cctrng_driver);
}
module_init(cctrng_mod_init);

static void __exit cctrng_mod_exit(void)
{
	platform_driver_unregister(&cctrng_driver);
}
module_exit(cctrng_mod_exit);

/* Module description */
MODULE_DESCRIPTION("ARM CryptoCell TRNG Driver");
MODULE_AUTHOR("ARM");
MODULE_LICENSE("GPL v2");
